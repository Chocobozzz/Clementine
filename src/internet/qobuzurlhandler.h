#ifndef QOBUZHANDLER_H
#define QOBUZHANDLER_H

#include "core/urlhandler.h"

class QobuzService;

class QobuzUrlHandler : public UrlHandler {
  Q_OBJECT
 public:
  QobuzUrlHandler(QobuzService* service, QObject* parent = nullptr);

  QString scheme() const { return "qobuz"; }
  QIcon icon() const { return QIcon(":/providers/qobuz.png"); }
  LoadResult StartLoading(const QUrl& url);

 private:

  QobuzService* service_;
};

#endif  // QOBUZHANDLER_H
