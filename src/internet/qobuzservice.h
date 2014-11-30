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

#ifndef QOBUZSERVICE_H
#define QOBUZSERVICE_H

#include "internetmodel.h"
#include "internetservice.h"

class NetworkAccessManager;
class SearchBoxWidget;

class QMenu;
class QNetworkReply;

class QobuzService : public InternetService {
  Q_OBJECT
 public:
  QobuzService(Application* app, InternetModel* parent);
  ~QobuzService();

  enum Quality { NONE = 0, MP3 = 5, FLAC = 6 };
  enum Error { NO_ERROR = 200, BAD_REQUEST = 400, UNAUTHORIZED = 401, REQUEST_FAILED = 402, NOT_FOUND = 404, SERVER_ERROR = 500 };

  enum Role {
    Role_UserPlaylistId = InternetModel::RoleCount,
    Role_PlaylistType
  };

  enum PlaylistType {
    UserPlaylist = Qt::UserRole,
    UserFavorites,
    FeaturedPlaylist
  };


  // Internet Service methods
  QStandardItem* CreateRootItem();
  void LazyPopulate(QStandardItem* parent);
  void ShowContextMenu(const QPoint& global_pos);
  QWidget* HeaderWidget() const;
  QList<QAction*> playlistitem_actions(const Song& song);

  void Connect(const QString &username, const QString &password);
  bool IsLoggedIn() const;
  void Logout();

  int SimpleSearch(const QString& query);

  QUrl GetStreamingUrlFromSongId(const QString& id);

  void AddUserFavoriteSong(int song_id);
  void DeletePlaylist(int playlist_id);
  void RenamePlaylist(int playlist_id);
  void SetPlaylistSongs(int playlist_id, const QList<int>& songs_ids);
  void RefreshPlaylist(int playlist_id);
  void RemoveFromFavorites(const QList<int>& songs_ids_to_remove);
  void RemoveFromPlaylist(int playlist_id,
                          const QList<int>& songs_ids_to_remove);

  Quality quality() const { return quality_; }
  void set_quality(const Quality& quality) { quality_ = quality; }

  static const char* kServiceName;
  static const char* kSettingsGroup;

 signals:
  void SimpleSearchResults(int id, SongList songs);
  void Connected();
  void ReplyError(int status_code);
  void NotPremium();
  void PlaylistRetrieved();

 public slots:
  void ShowConfig();

 private slots:
  void ConnectFinished(QNetworkReply* reply);

  void UserFavoritesRetrieved(QNetworkReply* reply);
  void FeaturedPlaylistsRetrieved(QNetworkReply* reply);
  void UserPlaylistsRetrieved(QNetworkReply* reply);
  void PlaylistRetrieved(QNetworkReply* reply, int playlist_id, int request_id);

  void UserFavoriteSongAdded(QNetworkReply* reply, int task_id);
  void RemoveCurrentFromFavorites();
  void SongsRemovedFromFavorites(QNetworkReply* reply, int task_id);
  void RemoveCurrentFromPlaylist();
  void AddCurrentSongToPlaylist(QAction* action);

  void CreateNewPlaylist();
  void NewPlaylistCreated(QNetworkReply *reply, const QString& name);
  void DeleteCurrentPlaylist();
  void PlaylistDeleted(QNetworkReply *reply, int playlist_id);
  void RenameCurrentPlaylist();
  void PlaylistRenamed(QNetworkReply* reply, int playlist_id, const QString& new_name);
  void PlaylistSongsSet(QNetworkReply* reply, int playlist_id, int task_id);

  void Search(const QString& text, bool now = false);
  void DoSearch();
  void SearchFinished(QNetworkReply* reply);
  void SimpleSearchFinished(QNetworkReply* reply, int id);

  void Homepage();

  void AddCurrentSongToUserFavorites() {
    AddUserFavoriteSong(current_song_id_);
  }

 private:
  struct PlaylistInfo {
    PlaylistInfo() {}
    PlaylistInfo(int id, QString name, QStandardItem* item)
        : id_(id), name_(name), item_(item) {}

    bool operator<(const PlaylistInfo other) const {
      return name_.localeAwareCompare(other.name_) < 0;
    }

    int id_;
    QString name_;
    QStandardItem* item_;
    QList<int> songs_ids_;
  };

  void LoadCredentialsIfEmpty();
  void ClearSearchResults();
  QStandardItem* CreatePlaylistItem(const QString& playlist_name, int playlist_id);

  void RetrieveUserData();
  void RetrieveUserFavorites();
  void RetrieveFeaturedPlaylists();
  void RetrieveUserPlaylists();
  void RetrievePlaylist(int playlist_id, QStandardItem* playlist_item);

  void EnsuredConnected();
  void EnsureItemsCreated();
  void EnsureMenuCreated();

  int ExtractSongIdFromUrl(const QUrl& url);
  QString SongsIdsToStringParameter(const QList<int> & songs);

  QNetworkReply* CreateRequest(const QString& ressource_name,
                               const QList<QPair<QString, QString> >& params, bool add_auth_header = false);

  // Convenient function for extracting result from reply
  QVariantMap ExtractResult(QNetworkReply* reply);
  SongList ExtractSongs(const QVariant &result);
  Song ExtractSong(const QVariantMap& result_song);

  // Keep all of our playlist in map to retrieve them easily
  QMap<int, PlaylistInfo> featured_playlists_info_;
  QMap<int, PlaylistInfo> user_playlists_info_;

  QStandardItem* root_;
  QStandardItem* search_;
  QStandardItem* user_favorites_;
  QStandardItem* user_playlists_;
  QStandardItem* featured_playlists_;

  NetworkAccessManager* network_;

  QMenu* context_menu_;
  SearchBoxWidget* search_box_;
  QTimer* search_delay_;
  QString pending_search_;

  // Request IDs
  int next_pending_search_id_;
  int task_search_id_;
  int next_pending_playlist_id_;
  int task_featured_playlists_id_;
  int task_user_playlists_id_;
  int task_user_favorites_id_;
  QSet<int> pending_retrieve_playlists_;

  // Used to execute actions when the user select a song in the playlist
  int current_song_id_;

  QString access_token_;
  QString user_id_;
  Quality quality_;

  QList<QAction*> playlistitem_actions_;
  QAction* create_playlist_;
  QAction* delete_playlist_;
  QAction* rename_playlist_;
  QAction* remove_from_playlist_;
  QAction* remove_from_favorites_;

  static const int kSongSearchLimit;
  static const int kSongSimpleSearchLimit;
  static const int kSearchDelayMsec;

};

#endif  // QOBUZSERVICE_H
