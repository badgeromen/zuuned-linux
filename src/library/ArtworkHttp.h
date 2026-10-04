#pragma once

#include <QByteArray>
#include <QNetworkReply>
#include <QString>
#include <QUrl>

// Blocking artwork transport, called only on workers. MusicBrainz calls
// share application-wide pacing and bounded HTTP429/503 retries.
namespace ArtworkHttp {
struct Response {
    QByteArray body, retryAfter;
    int status = 0;
    QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
    bool success() const {
        return networkError == QNetworkReply::NoError && status >= 200 && status < 300;
    }
};
Response get(const QUrl &url, bool musicBrainz = false);
// Terminal process shutdown: abort active transport and refuse queued requests.
// Call before draining workers, while QCoreApplication is still alive.
void beginShutdown();
QByteArray getBody(const QUrl &url, bool musicBrainz, QString *error = nullptr);
QString userMessage(const Response &response, bool musicBrainz = false);
// Returns -1 for an absent/invalid header. HTTP dates and delay-seconds
// are accepted; very long server delays stop automatic retries.
qint64 retryAfterMs(const QByteArray &header);
}
