// Local inspection gate. No USB, network, database writes or auto-import.
#include "AudiobookProbe.h"
#include "PodcastFeed.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    QString error;
    QJsonObject result;
    if (args.size() == 4 && args[1] == u"rss") {
        QFile file(args[2]);
        if (!file.open(QIODevice::ReadOnly) || file.size() > Podcasts::MaxFeedBytes) {
            fprintf(stderr, "Cannot read RSS file within %lld MiB limit\n",
                    static_cast<long long>(Podcasts::MaxFeedBytes / (1024 * 1024)));
            return 1;
        }
        PodcastFeed feed;
        if (!Podcasts::parseRss(file.read(Podcasts::MaxFeedBytes + 1), QUrl(args[3]), &feed, &error)) {
            fprintf(stderr, "%s\n", qPrintable(error)); return 1;
        }
        QJsonArray episodes;
        for (const auto &e : feed.episodes) episodes.append(QJsonObject{
            {"identity", e.identity}, {"guid", e.guid}, {"title", e.title}, {"author", e.author},
            {"url", e.enclosureUrl.toString(QUrl::FullyEncoded)}, {"type", e.mimeType},
            {"bytes", e.enclosureBytes}, {"durationMs", e.durationMs},
            {"published", e.published.toString(Qt::ISODate)}, {"artwork", e.artworkUrl.toString()}});
        result = {{"title", feed.title}, {"author", feed.author}, {"source", feed.sourceUrl.toString()},
                  {"artwork", feed.artworkUrl.toString()}, {"skippedItems", feed.skippedItems}, {"episodes", episodes}};
    } else if (args.size() == 3 && args[1] == u"book") {
        AudiobookPart part;
        if (!Audiobooks::probePart(args[2], &part, &error)) { fprintf(stderr, "%s\n", qPrintable(error)); return 1; }
        QJsonArray chapters;
        for (const auto &c : part.chapters) chapters.append(QJsonObject{
            {"index", c.sourceIndex}, {"title", c.title}, {"startMs", c.startMs}, {"endMs", c.endMs}});
        result = {{"path", part.path}, {"book", part.bookTitle}, {"title", part.partTitle},
                  {"author", part.author}, {"narrator", part.narrator}, {"codec", part.codec},
                  {"container", part.container}, {"durationMs", part.durationMs}, {"bytes", part.bytes},
                  {"disc", part.discNumber}, {"part", part.partNumber}, {"chapters", chapters}};
    } else {
        fprintf(stderr, "usage: spokenmediatool rss <local-feed.xml> <response-url>\n"
                        "       spokenmediatool book <local-audio-file>\n");
        return 2;
    }
    const auto json = QJsonDocument(result).toJson();
    return fwrite(json.constData(), 1, size_t(json.size()), stdout) == size_t(json.size()) ? 0 : 1;
}
