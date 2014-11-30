/* This file is part of Clementine.
   Copyright 2012, David Sansome <me@davidsansome.com>

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qobuzservice.h"

#include <QMenu>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QDesktopServices>
#include <QInputDialog>
#include <QMessageBox>

#include <qjson/parser.h>

#include "searchboxwidget.h"

#include "core/application.h"
#include "core/closure.h"
#include "core/mergedproxymodel.h"
#include "core/network.h"
#include "core/song.h"
#include "core/taskmanager.h"
#include "core/timeconstants.h"
#include "core/utilities.h"
#include "globalsearch/globalsearch.h"
#include "ui/iconloader.h"
#include "core/waitforsignal.h"
#include "internet/qobuzurlhandler.h"
#include "globalsearch/qobuzsearchprovider.h"
#include "core/player.h"

const char* QobuzService::kServiceName = "Qobuz";
const char* QobuzService::kSettingsGroup = "Qobuz";

const int QobuzService::kSearchDelayMsec = 400;
const int QobuzService::kSongSearchLimit = 100;
const int QobuzService::kSongSimpleSearchLimit = 30;

namespace {
const char* kHomepage = "www.qobuz.com";

const char* kAppId = "";
const char* kAppSecret = "";

const char* kBaseUrl = "http://www.qobuz.com/api.json/0.2";

const char* kAuthTokenUrl = "/user/login";

const char* kUserPlaylists = "/playlist/getUserPlaylists";
const char* kFeaturedPlaylists = "/playlist/getFeatured";
const char* kGetPlaylist = "/playlist/get";
const char* kCreatePlaylist = "/playlist/create";
const char* kUpdatePlaylist = "/playlist/update";
const char* kDeletePlaylist = "/playlist/delete";

const char* kUserFavorites = "/favorite/getUserFavorites";
const char* kUserAddFavorite = "/favorite/create";
const char* kUserDeleteFavorite = "/favorite/delete";

const char* kSearch = "/catalog/search";

const char* kSteamUrl = "/track/getFileUrl";
}

typedef QPair<QString, QString> Param;

QobuzService::QobuzService(Application* app, InternetModel* parent)
  : InternetService(kServiceName, app, parent, parent),
    root_(nullptr),
    search_(nullptr),
    user_favorites_(nullptr),
    user_playlists_(nullptr),
    featured_playlists_(nullptr),
    network_(new NetworkAccessManager(this)),
    context_menu_(nullptr),
    search_box_(new SearchBoxWidget(this)),
    search_delay_(new QTimer(this)),
    next_pending_search_id_(0),
    task_search_id_(0),
    next_pending_playlist_id_(0),
    task_featured_playlists_id_(0),
    task_user_playlists_id_(0),
    task_user_favorites_id_(0),
    quality_(Quality::MP3),
    create_playlist_(nullptr),
    delete_playlist_(nullptr),
    rename_playlist_(nullptr),
    remove_from_playlist_(nullptr),
    remove_from_favorites_(nullptr)
{

  search_delay_->setInterval(kSearchDelayMsec);
  search_delay_->setSingleShot(true);
  connect(search_delay_, SIGNAL(timeout()), SLOT(DoSearch()));

  QobuzSearchProvider* search_provider =
      new QobuzSearchProvider(app_, this);
  search_provider->Init(this);
  app_->global_search()->AddProvider(search_provider);

  app->player()->RegisterUrlHandler(new QobuzUrlHandler(this, this));

  connect(search_box_, SIGNAL(TextChanged(QString)), SLOT(Search(QString)));

  LoadCredentialsIfEmpty();
}

QobuzService::~QobuzService() {}

QStandardItem* QobuzService::CreateRootItem() {
  root_ = new QStandardItem(QIcon(":providers/qobuz.png"), kServiceName);
  root_->setData(true, InternetModel::Role_CanLazyLoad);
  root_->setData(InternetModel::PlayBehaviour_DoubleClickAction,
                 InternetModel::Role_PlayBehaviour);
  return root_;
}

void QobuzService::LazyPopulate(QStandardItem* item) {
  switch (item->data(InternetModel::Role_Type).toInt()) {
  case InternetModel::Type_Service: {
    EnsuredConnected();
    break;
  }
  default:
    break;
  }
}

void QobuzService::EnsuredConnected() {
  if (access_token_.isEmpty()) {
    ShowConfig();
  } else {
    EnsureItemsCreated();
  }
}

void QobuzService::EnsureItemsCreated() {
  if (IsLoggedIn() && !search_) {
    search_ =
        new QStandardItem(IconLoader::Load("edit-find"), tr("Search results"));
    search_->setToolTip(
          tr("Start typing something on the search box above to "
             "fill this search results list"));
    search_->setData(InternetModel::PlayBehaviour_MultipleItems,
                     InternetModel::Role_PlayBehaviour);
    root_->appendRow(search_);
  }

  if (!user_favorites_ && !featured_playlists_ && !user_playlists_ && IsLoggedIn()) {
    user_favorites_ =
        new QStandardItem(QIcon(":/last.fm/love.png"), tr("Favorites"));
    user_favorites_->setData(InternetModel::Type_UserPlaylist,
                             InternetModel::Role_Type);
    user_favorites_->setData(UserFavorites, Role_PlaylistType);
    user_favorites_->setData(true, InternetModel::Role_CanLazyLoad);
    user_favorites_->setData(true, InternetModel::Role_CanBeModified);
    user_favorites_->setData(InternetModel::PlayBehaviour_MultipleItems,
                             InternetModel::Role_PlayBehaviour);
    root_->appendRow(user_favorites_);

    featured_playlists_ =
        new QStandardItem(QIcon(":/star-on.png"), tr("Featured playlists"));
    root_->appendRow(featured_playlists_);

    user_playlists_ =
        new QStandardItem(QIcon(":/icons/svg/musical-note.svg"), tr("Playlists"));
    root_->appendRow(user_playlists_);

    RetrieveUserData();
  }
}

QWidget* QobuzService::HeaderWidget() const {
  if(IsLoggedIn()) {
    return search_box_;
  }

  return nullptr;
}

void QobuzService::ShowConfig() {
  app_->OpenSettingsDialogAtPage(SettingsDialog::Page_Qobuz);
}

void QobuzService::Connect(const QString & username, const QString & password) {

  QList<Param> parameters;
  parameters << Param("password", password) << Param("username", username);
  QNetworkReply* reply = CreateRequest(kAuthTokenUrl, parameters);
  NewClosure(reply, SIGNAL(finished()), this,
             SLOT(ConnectFinished(QNetworkReply*)), reply);
}

void QobuzService::ConnectFinished(QNetworkReply* reply) {
  reply->deleteLater();

  QVariantMap response = ExtractResult(reply);

  access_token_ = response["user_auth_token"].toString();
  if (access_token_.isEmpty()) {
    return;
  }

  QString user_mail = response["user"].toMap()["email"].toString();
  user_id_ = response["user"].toMap()["id"].toString();

  QVariantMap account_parameters = response["user"].toMap()["credential"].toMap()["parameters"].toMap();
  bool lossless_streaming = account_parameters["lossless_streaming"].toBool();
  bool lossy_streaming = account_parameters["lossy_streaming"].toBool();

  // If the user don't have a premium account he can't listen music (actually just extracts)
  if(!lossy_streaming) {
    emit NotPremium();
    Logout();
    return;
  }

  QSettings s;
  s.beginGroup(kSettingsGroup);
  s.setValue("access_token", access_token_);
  s.setValue("user_id", user_id_);
  s.setValue("user_mail", user_mail);

  // <=> User can listen FLAC files
  s.setValue("hifi_subscriber", lossless_streaming);

  emit Connected();

  EnsureItemsCreated();
}

void QobuzService::LoadCredentialsIfEmpty() {
  if (access_token_.isEmpty()) {
    QSettings s;
    s.beginGroup(kSettingsGroup);
    if (!s.contains("access_token")) {
      return;
    }

    access_token_ = s.value("access_token").toString();
    user_id_ = s.value("user_id").toString();
    quality_ = Quality(s.value("quality").toInt());
  }
}

bool QobuzService::IsLoggedIn() const {
  return !access_token_.isEmpty();
}

void QobuzService::Logout() {
  QSettings s;
  s.beginGroup(kSettingsGroup);

  int task_id = app_->task_manager()->StartTask(tr("Qobuz logout..."));

  // We have to wait any task not finished
  while(app_->task_manager()->ContainsTask(task_featured_playlists_id_)
        || app_->task_manager()->ContainsTask(task_search_id_)
        || app_->task_manager()->ContainsTask(task_user_favorites_id_)
        || app_->task_manager()->ContainsTask(task_user_playlists_id_))
  {
    WaitForSignal(app_->task_manager(), SIGNAL(TasksChanged()));
  }

  while(!pending_retrieve_playlists_.isEmpty()) {
    WaitForSignal(this, SIGNAL(PlaylistRetrieved()));
  }

  access_token_.clear();
  user_id_.clear();
  quality_ = NONE;
  s.remove("access_token");
  s.remove("user_id");
  s.remove("quality");

  if (featured_playlists_ != nullptr) {
    root_->removeRow(featured_playlists_->row());
    featured_playlists_ = nullptr;
  }
  if (user_favorites_ != nullptr) {
    root_->removeRow(user_favorites_->row());
    user_favorites_ = nullptr;
  }
  if (user_playlists_ != nullptr) {
    root_->removeRow(user_playlists_->row());
    user_playlists_ = nullptr;
  }
  if(search_ != nullptr) {
    root_->removeRow(search_->row());
    search_ = nullptr;
  }

  featured_playlists_info_.clear();
  user_playlists_info_.clear();

  pending_search_.clear();
  app_->task_manager()->SetTaskFinished(task_id);
}

void QobuzService::RetrieveUserData() {
  LoadCredentialsIfEmpty();
  RetrieveFeaturedPlaylists();
  RetrieveUserFavorites();
  RetrieveUserPlaylists();
}

void QobuzService::RetrieveUserFavorites() {
  task_user_favorites_id_ =
      app_->task_manager()->StartTask(tr("Getting Qobuz user favorites songs"));

  QList<Param> parameters;
  parameters << Param("type", "tracks") << Param("user_id", user_id_);
  QNetworkReply* reply = CreateRequest(kUserFavorites, parameters, true);
  NewClosure(reply, SIGNAL(finished()), this,
             SLOT(UserFavoritesRetrieved(QNetworkReply*)), reply);
}

void QobuzService::UserFavoritesRetrieved(QNetworkReply* reply) {
  reply->deleteLater();

  // Remove previous data
  user_favorites_->removeRows(0, user_favorites_->rowCount());

  SongList songs = ExtractSongs(ExtractResult(reply)["tracks"]);
  // Fill favorites list
  for (const Song& song : songs) {
    QStandardItem* child = CreateSongItem(song);
    user_favorites_->appendRow(child);
  }

  app_->task_manager()->SetTaskFinished(task_user_favorites_id_);
}

void QobuzService::RetrieveFeaturedPlaylists() {
  task_featured_playlists_id_ =
      app_->task_manager()->StartTask(tr("Getting Qobuz featured playlists"));

  QList<Param> parameters;
  parameters << Param("type", "editor-picks") << Param("user_id", user_id_);
  QNetworkReply* reply = CreateRequest(kFeaturedPlaylists, parameters, true);
  NewClosure(reply, SIGNAL(finished()), this,
             SLOT(FeaturedPlaylistsRetrieved(QNetworkReply*)), reply);
}

void QobuzService::FeaturedPlaylistsRetrieved(QNetworkReply* reply) {
  reply->deleteLater();

  QVariantMap response = ExtractResult(reply);
  QVariantList items = response["playlists"].toMap()["items"].toList();

  // Build every playlist
  for (const QVariant& item : items) {
    QVariantMap playlist_map = item.toMap();
    int playlist_id = playlist_map["id"].toInt();
    QString playlist_name = playlist_map["name"].toString();

    QStandardItem* playlist_item = CreatePlaylistItem(playlist_name, playlist_id);
    // Modify the default comportment of playlists built by CreatePlaylistItem : they can't be modified
    playlist_item->setData(FeaturedPlaylist, Role_PlaylistType);
    playlist_item->setData(false, InternetModel::Role_CanBeModified);

    PlaylistInfo playlist_info(playlist_id, playlist_name, playlist_item);
    featured_playlists_info_.insert(
          playlist_id, playlist_info);
    featured_playlists_->appendRow(playlist_item);

    RefreshPlaylist(playlist_id);
  }

  app_->task_manager()->SetTaskFinished(task_featured_playlists_id_);
}


void QobuzService::RetrieveUserPlaylists() {
  task_user_playlists_id_ =
      app_->task_manager()->StartTask(tr("Getting Qobuz user playlists"));

  QList<Param> parameters;
  parameters << Param("user_id", user_id_);
  QNetworkReply* reply = CreateRequest(kUserPlaylists, parameters, true);
  NewClosure(reply, SIGNAL(finished()), this,
             SLOT(UserPlaylistsRetrieved(QNetworkReply*)), reply);
}

void QobuzService::UserPlaylistsRetrieved(QNetworkReply* reply) {
  reply->deleteLater();

  QVariantMap response = ExtractResult(reply);
  QVariantList items = response["playlists"].toMap()["items"].toList();

  // Build every playlist
  for (const QVariant& item : items) {
    QVariantMap playlist_map = item.toMap();
    int playlist_id = playlist_map["id"].toInt();
    QString playlist_name = playlist_map["name"].toString();

    QStandardItem* playlist_item = CreatePlaylistItem(playlist_name, playlist_id);
    // Modify the default comportment the playlist if it doesn't own to the user and it is not collaborative : they can't be modified
    if(playlist_map["owner"].toMap()["id"].toString() != user_id_ && !playlist_map["is_collaborative"].toBool()) {
      playlist_item->setData(FeaturedPlaylist, Role_PlaylistType);
      playlist_item->setData(false, InternetModel::Role_CanBeModified);
    }

    user_playlists_->appendRow(playlist_item);

    PlaylistInfo playlist_info(playlist_id, playlist_name, playlist_item);
    user_playlists_info_.insert(playlist_id, playlist_info);

    RefreshPlaylist(playlist_id);
  }

  app_->task_manager()->SetTaskFinished(task_user_playlists_id_);
}

void QobuzService::Search(const QString& text, bool now) {
  pending_search_ = text;

  if (!task_search_id_) {
    task_search_id_ =
        app_->task_manager()->StartTask(tr("Searching on Qobuz"));
  }

  // If there is no text (e.g. user cleared search box), we don't need to do a
  // real query that will return nothing: we can clear the playlist now
  if (text.isEmpty()) {
    search_delay_->stop();
    ClearSearchResults();
    return;
  }

  if (now) {
    search_delay_->stop();
    DoSearch();
  } else {
    search_delay_->start();
  }
}

void QobuzService::DoSearch() {
  ClearSearchResults();

  QList<Param> parameters;
  parameters << Param("limit", QString::number(kSongSearchLimit)) << Param("query", pending_search_) << Param("type", "tracks");
  QNetworkReply* reply = CreateRequest(kSearch, parameters, true);
  NewClosure(reply, SIGNAL(finished()), this,
             SLOT(SearchFinished(QNetworkReply*)), reply);
}

void QobuzService::SearchFinished(QNetworkReply* reply) {
  reply->deleteLater();

  SongList songs = ExtractSongs(ExtractResult(reply)["tracks"]);
  // Fill search results list
  for (const Song& song : songs) {
    QStandardItem* child = CreateSongItem(song);
    search_->appendRow(child);
  }

  QModelIndex index = model()->merged_model()->mapFromSource(search_->index());
  ScrollToIndex(index);

  app_->task_manager()->SetTaskFinished(task_search_id_);
  task_search_id_ = 0;
}

void QobuzService::ClearSearchResults() {
  if (search_) {
    search_->removeRows(0, search_->rowCount());
  }
}

int QobuzService::SimpleSearch(const QString& text) {
  QList<Param> parameters;
  parameters << Param("limit", QString::number(kSongSimpleSearchLimit)) << Param("query", text) << Param("type", "tracks");

  QNetworkReply* reply = CreateRequest(kSearch, parameters, true);
  const int id = next_pending_search_id_++;
  NewClosure(reply, SIGNAL(finished()), this,
             SLOT(SimpleSearchFinished(QNetworkReply*, int)), reply, id);
  return id;
}

void QobuzService::SimpleSearchFinished(QNetworkReply* reply, int id) {
  reply->deleteLater();

  SongList songs = ExtractSongs(ExtractResult(reply)["tracks"]);
  emit SimpleSearchResults(id, songs);
}

void QobuzService::EnsureMenuCreated() {
  if (!context_menu_) {
    context_menu_ = new QMenu;
    context_menu_->addActions(GetPlaylistActions());

    create_playlist_ = context_menu_->addAction(
          IconLoader::Load("list-add"), tr("Create a new Qobuz playlist"),
          this, SLOT(CreateNewPlaylist()));
    delete_playlist_ = context_menu_->addAction(
          IconLoader::Load("edit-delete"), tr("Delete Qobuz playlist"),
          this, SLOT(DeleteCurrentPlaylist()));
    rename_playlist_ = context_menu_->addAction(
          IconLoader::Load("edit-rename"), tr("Rename Qobuz playlist"),
          this, SLOT(RenameCurrentPlaylist()));

    context_menu_->addSeparator();

    remove_from_playlist_ = context_menu_->addAction(
          IconLoader::Load("list-remove"), tr("Remove from playlist"), this,
          SLOT(RemoveCurrentFromPlaylist()));
    remove_from_favorites_ = context_menu_->addAction(
          IconLoader::Load("list-remove"), tr("Remove from favorites"), this,
          SLOT(RemoveCurrentFromFavorites()));

    context_menu_->addSeparator();

    context_menu_->addAction(IconLoader::Load("download"),
                             tr("Open %1 in browser").arg("Qobuz.com"),
                             this, SLOT(Homepage()));
  }
}

void QobuzService::ShowContextMenu(const QPoint& global_pos) {
  EnsureMenuCreated();

  // Check if we should display actions
  bool display_delete_playlist_action = false,
      display_remove_from_playlist_action = false,
      display_remove_from_favorites_action = false;

  QModelIndex index(model()->current_index());

  // If it's a user playlist
  if (index.data(InternetModel::Role_Type).toInt() ==
      InternetModel::Type_UserPlaylist &&
      index.data(Role_PlaylistType).toInt() == UserPlaylist) {
    display_delete_playlist_action = true;
  }

  // We check parent's type (instead of index type) because we want to enable
  // 'remove' actions for items which are inside a playlist
  int parent_type = index.parent().data(InternetModel::Role_Type).toInt();
  if (parent_type == InternetModel::Type_UserPlaylist) {
    int parent_playlist_type = index.parent().data(Role_PlaylistType).toInt();
    if (parent_playlist_type == UserFavorites) {
      display_remove_from_favorites_action = true;
    }
    else if (parent_playlist_type == UserPlaylist) {
      display_remove_from_playlist_action = true;
    }
  }

  delete_playlist_->setVisible(display_delete_playlist_action);
  rename_playlist_->setVisible(display_delete_playlist_action);
  remove_from_playlist_->setVisible(display_remove_from_playlist_action);
  remove_from_favorites_->setVisible(display_remove_from_favorites_action);

  context_menu_->popup(global_pos);
}

QStandardItem* QobuzService::CreatePlaylistItem(const QString& playlist_name, int playlist_id) {
  QStandardItem* item = new QStandardItem(playlist_name);
  // By default this is a user playlist
  item->setData(InternetModel::Type_UserPlaylist, InternetModel::Role_Type);
  item->setData(UserPlaylist, Role_PlaylistType);
  item->setData(true, InternetModel::Role_CanLazyLoad);
  item->setData(true, InternetModel::Role_CanBeModified);
  item->setData(InternetModel::PlayBehaviour_MultipleItems,
                InternetModel::Role_PlayBehaviour);
  item->setData(playlist_id, Role_UserPlaylistId);

  return item;
}

QNetworkReply* QobuzService::CreateRequest(const QString& ressource_name,
                                           const QList<Param>& params, bool add_auth_header) {

  QUrl url(QString(kBaseUrl) + ressource_name);

  for (const Param& param : params) {
    url.addQueryItem(param.first, param.second);
  }

  qLog(Debug) << "Request Url: " << url.toEncoded();

  QNetworkRequest req(url);
  req.setRawHeader("x-app-id", kAppId);
  if(add_auth_header) {
    req.setRawHeader("x-user-auth-token", access_token_.toUtf8());
  }
  req.setRawHeader("Accept", "application/json");
  QNetworkReply* reply = network_->get(req);
  return reply;
}

QVariantMap QobuzService::ExtractResult(QNetworkReply* reply) {
  int status_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

  if (status_code != Error::NO_ERROR) {
    qLog(Error) << "Error when retrieving Qobuz results:" << reply->errorString() << QString(" (%1)").arg(reply->error());

    // Problem with the authentification
    if (status_code == Error::UNAUTHORIZED) {
      Logout();
    }

    emit ReplyError(status_code);
    return QVariantMap();
  }

  QJson::Parser parser;
  bool ok;
  QVariant result = parser.parse(reply, &ok);

  if (!ok) {
    qLog(Error) << "Error while parsing Qobuz result";
  }

  return result.toMap();
}

void QobuzService::PlaylistRetrieved(QNetworkReply* reply, int playlist_id, int request_id) {
  reply->deleteLater();

  if (!pending_retrieve_playlists_.remove(request_id)) {
    // This request has been canceled. Stop here
    return;
  }

  PlaylistInfo* playlist_info = featured_playlists_info_.contains(playlist_id)
      ? &featured_playlists_info_[playlist_id]
        : &user_playlists_info_[playlist_id];
  playlist_info->item_->removeRows(0, playlist_info->item_->rowCount());

  QVariantMap result = ExtractResult(reply);

  SongList songs = ExtractSongs(result["tracks"]);

  // Clear the playlist info
  playlist_info->songs_ids_.clear();

  for (const Song& song : songs) {
    QStandardItem* child = CreateSongItem(song);
    child->setData(playlist_info->id_, Role_UserPlaylistId);
    child->setData(true, InternetModel::Role_CanBeModified);

    playlist_info->item_->appendRow(child);

    // Keep in mind this playlist
    playlist_info->songs_ids_ << ExtractSongIdFromUrl(song.url());
  }

  emit PlaylistRetrieved();
}

SongList QobuzService::ExtractSongs(const QVariant& result) {
  SongList songs;

  QVariantList items = result.toMap()["items"].toList();

  for (const QVariant& item : items) {
    Song song = ExtractSong(item.toMap());

    if (song.is_valid()) {
      songs << song;
    }
  }

  return songs;
}

Song QobuzService::ExtractSong(const QVariantMap& result_song) {
  Song song;

  if (!result_song.isEmpty() && result_song["streamable"].toBool()) {
    QUrl url;
    url.setScheme("qobuz");
    url.setPath(result_song["id"].toString());
    song.set_url(url);

    QString artist = result_song["performer"].toMap()["name"].toString();
    song.set_artist(artist);

    QString title = result_song["title"].toString();
    song.set_title(title);

    QVariant q_duration = result_song["duration"];
    quint64 duration = q_duration.toULongLong() * kNsecPerSec;
    song.set_length_nanosec(duration);

    int track_number = result_song["track_number"].toInt();
    song.set_playcount(track_number);

    // Album fields
    QVariantMap result_album = result_song["album"].toMap();

    QString album = result_album["title"].toString();
    song.set_album(album);

    QString genre = result_album["genre"].toMap()["name"].toString();
    song.set_genre(genre);

    QString cover = result_album["image"].toMap()["large"].toString();
    song.set_art_automatic(cover);

    QString album_artist = result_song["album"].toMap()["artist"].toMap()["name"].toString();
    song.set_albumartist(album_artist);

    int released_at = result_album["released_at"].toInt();
    QDateTime release_date;
    release_date.setTime_t(released_at);
    song.set_year(release_date.date().year());

    song.set_valid(true);
  }


  return song;
}

QUrl QobuzService::GetStreamingUrlFromSongId(const QString &id) {
  QList<Param> parameters;
  // /!\ Alphabetical !!!
  parameters << Param("format_id", QString::number(quality_)) << Param("intent", "stream") << Param("track_id", id);

  // See https://github.com/Qobuz/api-documentation#signed-requests-authentification-
  QString request_sig = QString(kSteamUrl).replace("/", "");
  QString request_ts = QString::number(time(nullptr));

  for(Param p : parameters) {
    request_sig += p.first;
    request_sig += p.second;
  }

  request_sig += request_ts;
  request_sig += kAppSecret;

  request_sig = QCryptographicHash::hash(request_sig.toLocal8Bit(), QCryptographicHash::Md5).toHex();

  parameters << Param("request_ts", request_ts) << Param("request_sig", request_sig);

  QNetworkReply *reply = CreateRequest(kSteamUrl, parameters, true);
  WaitForSignal(reply, SIGNAL(finished()));
  reply->deleteLater();

  QVariant result = ExtractResult(reply);
  return result.toMap()["url"].toUrl();
}

void QobuzService::Homepage() {
  QDesktopServices::openUrl(QUrl(kHomepage));
}

QList<QAction*> QobuzService::playlistitem_actions(const Song& song) {

  // Clear previous actions
  while (!playlistitem_actions_.isEmpty()) {
    QAction* action = playlistitem_actions_.takeFirst();
    delete action->menu();
    delete action;
  }

  // Create an 'add to favorites' action
  QAction* add_to_favorites = new QAction(
        QIcon(":/last.fm/love.png"), tr("Add to Qobuz favorites"), this);
  connect(add_to_favorites, SIGNAL(triggered()),
          SLOT(AddCurrentSongToUserFavorites()));
  playlistitem_actions_.append(add_to_favorites);

  // Create a menu with 'add to playlist' actions for each Qobuz playlist
  QAction* add_to_playlists = new QAction(
        IconLoader::Load("list-add"), tr("Add to Qobuz playlists"), this);
  QMenu* playlists_menu = new QMenu();
  for (const PlaylistInfo& playlist_info : user_playlists_info_.values()) {
    // Check the playlist can be modified
    if(!playlist_info.item_->data(InternetModel::Role_CanBeModified).toBool()) {
      continue;
    }

    QAction* add_to_playlist = new QAction(playlist_info.name_, this);
    add_to_playlist->setData(playlist_info.id_);
    playlists_menu->addAction(add_to_playlist);
  }
  connect(playlists_menu, SIGNAL(triggered(QAction*)),
          SLOT(AddCurrentSongToPlaylist(QAction*)));
  add_to_playlists->setMenu(playlists_menu);
  playlistitem_actions_.append(add_to_playlists);

  // Remember the current song id
  current_song_id_ = ExtractSongIdFromUrl(song.url());

  return playlistitem_actions_;
}


int QobuzService::ExtractSongIdFromUrl(const QUrl& url) {
  return url.path().toInt();
}


void QobuzService::CreateNewPlaylist() {
  QString name =
      QInputDialog::getText(nullptr, tr("Create a new Qobuz playlist"),
                            tr("Name"), QLineEdit::Normal);
  if (name.isEmpty()) {
    return;
  }

  QList<Param> parameters;
  parameters << Param("name", name);
  QNetworkReply* reply = CreateRequest(kCreatePlaylist, parameters, true);
  NewClosure(reply, SIGNAL(finished()), this,
             SLOT(NewPlaylistCreated(QNetworkReply*, const QString&)), reply,
             name);
}

void QobuzService::NewPlaylistCreated(QNetworkReply* reply,
                                      const QString& name) {
  reply->deleteLater();
  QVariantMap result = ExtractResult(reply);
  if (!result["id"].isValid()) {
    qLog(Warning) << "Qobuz CreateNewPlaylist failed";
    return;
  }

  int playlist_id = result["id"].toInt();
  // Get the name from the server and not from the parameter we sent to help us to detect a potential bug
  QString name_from_server = result["name"].toString();

  QStandardItem* new_playlist_item = CreatePlaylistItem(name_from_server, playlist_id);
  PlaylistInfo playlist_info(playlist_id, name_from_server, new_playlist_item);
  //  playlist_info.item_ = new_playlist_item;

  // Add the playlist
  user_playlists_->appendRow(new_playlist_item);
  user_playlists_info_.insert(playlist_id, playlist_info);
}


void QobuzService::DeleteCurrentPlaylist() {
  const QModelIndex& index(model()->current_index());
  if (index.data(InternetModel::Role_Type).toInt() !=
      InternetModel::Type_UserPlaylist ||
      index.data(Role_PlaylistType).toInt() != UserPlaylist) {
    return;
  }


  int playlist_id = model()->current_index().data(Role_UserPlaylistId).toInt();
  DeletePlaylist(playlist_id);
}

void QobuzService::DeletePlaylist(int playlist_id) {
  if (!user_playlists_info_.contains(playlist_id)) {
    return;
  }

  std::unique_ptr<QMessageBox> confirmation_dialog(
        new QMessageBox(QMessageBox::Question, tr("Delete Qobuz playlist"),
                        tr("Are you sure you want to delete this playlist?"),
                        QMessageBox::Yes | QMessageBox::Cancel));
  if (confirmation_dialog->exec() != QMessageBox::Yes) {
    return;
  }

  QList<Param> parameters;
  parameters << Param("playlist_id", QString::number(playlist_id));
  QNetworkReply* reply = CreateRequest(kDeletePlaylist, parameters, true);
  NewClosure(reply, SIGNAL(finished()), this,
             SLOT(PlaylistDeleted(QNetworkReply*, int)), reply, playlist_id);
}

void QobuzService::PlaylistDeleted(QNetworkReply* reply,
                                   int playlist_id) {
  reply->deleteLater();

  QVariantMap result = ExtractResult(reply);
  if (result["status"].toString() != "success") {
    qLog(Warning) << "Qobuz DeletePlaylist failed";
    return;
  }

  if (!user_playlists_info_.contains(playlist_id)) {
    return;
  }

  PlaylistInfo playlist_info = user_playlists_info_.take(playlist_id);
  user_playlists_->removeRow(playlist_info.item_->row());
}

void QobuzService::RenameCurrentPlaylist() {
  const QModelIndex& index(model()->current_index());

  if (index.data(InternetModel::Role_Type).toInt() !=
      InternetModel::Type_UserPlaylist ||
      index.data(Role_PlaylistType).toInt() != UserPlaylist) {
    return;
  }

  const int playlist_id = index.data(Role_UserPlaylistId).toInt();
  RenamePlaylist(playlist_id);
}

void QobuzService::RenamePlaylist(int playlist_id) {
  if (!user_playlists_info_.contains(playlist_id)) {
    return;
  }

  const QString& old_name = user_playlists_info_[playlist_id].name_;
  QString new_name =
      QInputDialog::getText(nullptr, tr("Rename \"%1\" playlist").arg(old_name),
                            tr("Name"), QLineEdit::Normal, old_name);
  if (new_name.isEmpty()) {
    return;
  }

  QList<Param> parameters;
  parameters << Param("playlist_id", QString::number(playlist_id)) << Param("name", new_name);
  QNetworkReply* reply = CreateRequest(kUpdatePlaylist, parameters, true);
  NewClosure(reply, SIGNAL(finished()), this,
             SLOT(PlaylistRenamed(QNetworkReply*, int, const QString&)), reply,
             playlist_id, new_name);
}

void QobuzService::PlaylistRenamed(QNetworkReply* reply, int playlist_id,
                                   const QString& new_name) {
  reply->deleteLater();

  QVariantMap result = ExtractResult(reply);
  if (!result["id"].isValid()) {
    qLog(Warning) << "Qobuz RenamePlaylist failed";
    return;
  }

  if (!user_playlists_info_.contains(playlist_id)) {
    return;
  }

  // Update our playlist too
  PlaylistInfo& playlist_info = user_playlists_info_[playlist_id];
  playlist_info.name_ = new_name;
  playlist_info.item_->setText(new_name);
}

void QobuzService::AddUserFavoriteSong(int song_id) {
  int task_id = app_->task_manager()->StartTask(tr("Adding song to favorites"));

  QList<Param> parameters;
  parameters << Param("track_ids", QString::number(song_id));

  QNetworkReply* reply = CreateRequest(kUserAddFavorite, parameters, true);
  NewClosure(reply, SIGNAL(finished()), this,
             SLOT(UserFavoriteSongAdded(QNetworkReply*, int)), reply, task_id);
}

void QobuzService::UserFavoriteSongAdded(QNetworkReply *reply, int task_id) {
  reply->deleteLater();

  QVariantMap result = ExtractResult(reply);
  if (result["status"].toString() != "success") {
    qLog(Warning) << "Qobuz AddUserFavoriteSong failed";
    return;
  }

  RetrieveUserFavorites();

  app_->task_manager()->SetTaskFinished(task_id);
}


void QobuzService::RemoveCurrentFromFavorites() {
  const QModelIndexList& indexes(model()->selected_indexes());
  QList<int> songs_ids;
  for (const QModelIndex& index : indexes) {

    int song_id = ExtractSongIdFromUrl(index.data(InternetModel::Role_Url).toUrl());
    if (song_id) {
      songs_ids << song_id;
    }
  }

  RemoveFromFavorites(songs_ids);
}

void QobuzService::RemoveFromFavorites(const QList<int> &songs_ids_to_remove) {
  if (songs_ids_to_remove.isEmpty()) return;

  int task_id = app_->task_manager()->StartTask(tr("Removing songs to favorites"));

  // Convert song ids to string
  QString songs_ids_string = SongsIdsToStringParameter(songs_ids_to_remove);

  QList<Param> parameters;
  parameters << Param("track_ids", songs_ids_string);

  QNetworkReply* reply = CreateRequest(kUserDeleteFavorite, parameters, true);
  NewClosure(reply, SIGNAL(finished()), this,
             SLOT(SongsRemovedFromFavorites(QNetworkReply*,int)), reply, task_id);
}

void QobuzService::SongsRemovedFromFavorites(QNetworkReply* reply,
                                             int task_id) {
  reply->deleteLater();

  QVariantMap result = ExtractResult(reply);
  if (result["status"] != "success") {
    qLog(Warning) << "Qobuz RemoveUserFavoriteSongs failed";
    return;
  }

  RetrieveUserFavorites();

  app_->task_manager()->SetTaskFinished(task_id);
}


void QobuzService::AddCurrentSongToPlaylist(QAction* action) {
  int playlist_id = action->data().toInt();
  if (!user_playlists_info_.contains(playlist_id)) {
    return;
  }

  // Get the current playlist's songs
  PlaylistInfo playlist = user_playlists_info_[playlist_id];
  QList<int> songs_ids = playlist.songs_ids_;
  songs_ids << current_song_id_;

  SetPlaylistSongs(playlist_id, songs_ids);
}

void QobuzService::SetPlaylistSongs(int playlist_id,
                                    const QList<int>& songs_ids) {
  // If we are still retrieving playlists songs, don't update playlist: don't
  // take the risk to erase all (not yet retrieved) playlist's songs.
  if (!pending_retrieve_playlists_.isEmpty()) return;
  int task_id =
      app_->task_manager()->StartTask(tr("Update Quobuz playlist"));

  QString songs_ids_string = SongsIdsToStringParameter(songs_ids);

  QList<Param> parameters;

  parameters << Param("playlist_id", QString::number(playlist_id))
             << Param("track_ids", songs_ids_string);

  QNetworkReply* reply = CreateRequest(kUpdatePlaylist, parameters, true);

  NewClosure(reply, SIGNAL(finished()), this,
             SLOT(PlaylistSongsSet(QNetworkReply*, int, int)), reply,
             playlist_id, task_id);
}

void QobuzService::PlaylistSongsSet(QNetworkReply* reply, int playlist_id,
                                    int task_id) {
  reply->deleteLater();

  QVariantMap result = ExtractResult(reply);

  // If the server doesn't return the playlist id (with other informations), there is a problem
  if (result["id"].toInt() != playlist_id) {
    qLog(Warning) << "Qobuz SetPlaylistSongs failed";
    return;
  }

  RefreshPlaylist(playlist_id);

  app_->task_manager()->SetTaskFinished(task_id);
}

void QobuzService::RefreshPlaylist(int playlist_id) {
  QList<Param> parameters;
  parameters << Param("extra", "tracks") << Param("playlist_id", QString::number(playlist_id));
  QNetworkReply* reply = CreateRequest(kGetPlaylist, parameters, true);

  int id = next_pending_playlist_id_++;
  pending_retrieve_playlists_.insert(id);

  NewClosure(reply, SIGNAL(finished()),
             this, SLOT(PlaylistRetrieved(QNetworkReply*, int, int)),
             reply, playlist_id, id);
}


void QobuzService::RemoveCurrentFromPlaylist() {
  const QModelIndexList& indexes(model()->selected_indexes());
  QMap<int, QList<int> > playlists_songs_ids;

  for (const QModelIndex& index : indexes) {
    // Check if it's a user playlist
    if (index.parent().data(InternetModel::Role_Type).toInt() !=
        InternetModel::Type_UserPlaylist) {
      continue;
    }

    int playlist_id = index.data(Role_UserPlaylistId).toInt();
    int song_id = ExtractSongIdFromUrl(index.data(InternetModel::Role_Url).toUrl());
    if (song_id) {
      playlists_songs_ids[playlist_id] << song_id;
    }
  }

  for (auto it = playlists_songs_ids.constBegin(); it != playlists_songs_ids.constEnd(); ++it) {
    RemoveFromPlaylist(it.key(), it.value());
  }
}

void QobuzService::RemoveFromPlaylist(
    int playlist_id, const QList<int>& songs_ids_to_remove) {
  if (!user_playlists_info_.contains(playlist_id)) {
    return;
  }

  QList<int> songs_ids = user_playlists_info_[playlist_id].songs_ids_;
  for (const int song_id : songs_ids_to_remove) {
    songs_ids.removeOne(song_id);
  }

  SetPlaylistSongs(playlist_id, songs_ids);
}

QString QobuzService::SongsIdsToStringParameter(const QList<int> &songs) {
  QString songs_ids_string;
  int i;
  for (i = 0; i < songs.length() - 1; ++i) {
    songs_ids_string += QString::number(songs.at(i)) + ",";
  }
  // Don't forget the last one
  songs_ids_string += QString::number(songs.at(i));

  return songs_ids_string;
}
