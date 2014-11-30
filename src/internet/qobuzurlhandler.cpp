#include "qobuzurlhandler.h"

#include "qobuzservice.h"

QobuzUrlHandler::QobuzUrlHandler(QobuzService* service, QObject* parent)
    : UrlHandler(parent), service_(service) {}

UrlHandler::LoadResult QobuzUrlHandler::StartLoading(const QUrl& url) {
  QString id = url.path();

  QUrl real_url = service_->GetStreamingUrlFromSongId(id);

  return LoadResult(url, LoadResult::TrackAvailable, real_url);
}
