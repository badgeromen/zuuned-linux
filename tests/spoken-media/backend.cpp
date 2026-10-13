#include "spoken/AudiobookProbe.h"
#include "spoken/PodcastFeed.h"
#include <QCoreApplication>
#include <QFile>
#include <cstdio>

static int failed = 0, checks = 0;
static void check(bool ok, const char *name) { ++checks; failed += !ok; printf("%s %s\n", ok ? "PASS" : "FAIL", name); }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QUrl source("https://example.test/feed/index.xml");
    const QByteArray start = R"(<rss version="2.0" xmlns:i="http://www.itunes.com/dtds/podcast-1.0.dtd"><channel><title>Signal &amp; Noise</title>)";
    const QByteArray finish = "</channel></rss>";
    const auto item = [](const QByteArray &guid, const QByteArray &url, const QByteArray &extra = {}) {
        return "<item><title>Same title</title><guid>" + guid + "</guid><enclosure url=\"" + url +
               "\" type=\"audio/mpeg\" length=\"42\"/>" + extra + "</item>";
    };
    const auto a = item("Opaque-ID", "episode.mp3?token=one&amp;x=2", "<i:duration>1:02:03</i:duration><pubDate>Tue, 07 Oct 2025 12:00:00 GMT</pubDate>");
    PodcastFeed feed;
    QString error;
    auto parse = [&](const QByteArray &bytes) { return Podcasts::parseRss(bytes, source, &feed, &error); };
    check(parse(start + a + "<i:author>Creator</i:author><i:image href=\"/art.jpg\"/>" + finish) &&
          feed.title == u"Signal & Noise" && feed.episodes.size() == 1, "RSS namespaces and XML escaping");
    check(feed.episodes[0].identity == u"guid:Opaque-ID" && feed.episodes[0].enclosureUrl.toString() ==
          u"https://example.test/feed/episode.mp3?token=one&x=2", "opaque GUID and complete relative enclosure query retained");
    check(feed.episodes[0].durationMs == 3723000 && feed.episodes[0].published.isValid() &&
          feed.episodes[0].enclosureBytes == 42, "duration, publication and enclosure size");
    check(feed.episodes[0].author == u"Creator" && feed.episodes[0].artworkUrl == QUrl("https://example.test/art.jpg"),
          "show metadata after items is inherited");
    check(feed.episodes[0].episodeNumber == 0, "missing episode number stays unknown");
    check(parse(start + item("numbered", "/a.mp3", "<i:episode>190</i:episode>") + finish) &&
          feed.episodes[0].episodeNumber == 190, "feed episode number is independent of list position");
    check(parse(start + item("numbered", "/a.mp3", "<i:episode> 00190 </i:episode>") + finish) &&
          feed.episodes[0].episodeNumber == 190, "episode number accepts whitespace and leading zeroes");
    for (const auto &value : {"", "0", "-1", "+190", "1.5", "Episode 190", "2147483648"}) {
        check(parse(start + item("numbered", "/a.mp3", QByteArray("<i:episode>") + value + "</i:episode>") + finish) &&
              feed.episodes[0].episodeNumber == 0, "invalid episode number stays unknown");
    }
    check(parse(start + item("numbered", "/a.mp3", "<episode>190</episode><x:episode xmlns:x=\"urn:other\">191</x:episode>") + finish) &&
          feed.episodes[0].episodeNumber == 0, "unrelated episode elements cannot supply numbering");
    check(parse(start + a + item("different-ID", "second.mp3") + finish) && feed.episodes.size() == 2,
          "identical titles with different IDs remain separate");
    check(parse(start + a + a + finish) && feed.episodes.size() == 1, "identical repeated episode collapsed");
    check(!parse(start + a + item("Opaque-ID", "different.mp3") + finish) && feed.episodes.isEmpty() && !error.isEmpty(),
          "conflicting duplicate GUID fails atomically");
    check(parse(start + item("", "/no-guid.mp3") + finish) && feed.episodes[0].identity == u"url:https://example.test/no-guid.mp3",
          "missing GUID uses complete enclosure identity");
    check(parse(start + item("Opaque-ID", "/changed.mp3") + finish) && feed.episodes[0].identity == u"guid:Opaque-ID",
          "GUID stable across enclosure change on refresh");
    check(parse(start + "<item><title>Article</title></item>" + item("bad", "file:///etc/passwd") + finish) &&
          feed.episodes.isEmpty() && feed.skippedItems == 2, "nonmedia items and non-HTTP enclosures are counted as skipped");
    check(!parse("<!DOCTYPE rss [<!ENTITY x 'expanded'>]>" + start + a + finish), "DTD rejected");
    check(!parse(start + a + "</channel>") && feed.episodes.isEmpty(), "truncated feed cannot publish partial rows");
    check(!parse(start + a + finish + "<extra/>"), "extra document root rejected");
    check(!parse("<feed xmlns=\"http://www.w3.org/2005/Atom\"/>"), "unsupported feed format reported");
    check(!parse(QByteArray(Podcasts::MaxFeedBytes + 1, 'x')), "input size bounded");
    check(!parse(start + "<description>" + QByteArray(65537, 'x') + "</description>" + finish), "text size bounded");
    QByteArray nesting;
    for (int i = 0; i < 33; ++i) nesting += "<x>";
    for (int i = 0; i < 33; ++i) nesting += "</x>";
    check(!parse(start + nesting + finish), "unknown extension nesting bounded");
    check(parse(start + item("duration", "/a.mp3", "<i:duration>1:99</i:duration>") + finish) &&
          feed.episodes[0].durationMs == -1, "invalid duration stays unknown");
    check(Podcasts::mediaUrl("https://user:pass@example.test/a.mp3", source).isEmpty(), "credential-bearing URL rejected");
    check(parse(start + "<item xml:base=\"https://cdn.example.test/media/\"><title>A</title><enclosure url=\"a.mp3\" type=\"audio/mpeg\"/></item>" + finish) &&
          feed.episodes[0].enclosureUrl == QUrl("https://cdn.example.test/media/a.mp3"), "xml base honored");
    check(!parse(start + item("ambiguous", "/a.mp3", "<enclosure url=\"/b.mp3\" type=\"audio/mpeg\"/>") + finish),
          "multiple conflicting enclosures reported");
    check(parse(start + "<x:title xmlns:x=\"urn:other\">Wrong</x:title>" + a + finish) && feed.title == u"Signal & Noise",
          "unrelated extension names cannot replace RSS metadata");
    check(parse(start + item("秘密-🎧", "/episode.mp3") + finish) && feed.episodes[0].guid == QString::fromUtf8("秘密-🎧"),
          "Unicode identities preserved");
    check(parse(start + item("description", "/a.mp3", "<c:encoded xmlns:c=\"http://purl.org/rss/1.0/modules/content/\"><![CDATA[<p>Full text</p>]]></c:encoded><description>Summary</description>") + finish) &&
          feed.episodes[0].description == u"<p>Full text</p>", "encoded description remains inert and wins over summary");
    check(parse(start + item("date", "/a.mp3", "<pubDate>Tue, 07 Oct 2025 12:00:00 -0500</pubDate>") + finish) &&
          feed.episodes[0].published.toString(Qt::ISODate) == u"2025-10-07T17:00:00Z", "numeric publication timezone converted to UTC");
    QByteArray many = start;
    for (int i = 0; i <= Podcasts::MaxEpisodes; ++i) many += "<item/>";
    check(!parse(many + finish), "episode count bounded including skipped items");

    if (argc != 4) return 2;
    AudiobookPart book;
    check(Audiobooks::probePart(QString::fromLocal8Bit(argv[1]), &book, &error) && book.chapters.size() == 2 && book.codec == u"aac",
          "native M4B probe keeps both chapters");
    if (book.chapters.size() == 2) {
        check(book.chapters[0].title == u"Opening" && book.chapters[0].startMs == 0 && book.chapters[0].endMs == 1000 &&
              book.chapters[1].title == u"Next" && book.chapters[1].startMs == 1000 && book.chapters[1].endMs == 2000,
              "chapter titles, source order and exact times retained");
        check(book.bookTitle == u"Test Book" && book.author == u"Test Author", "book identity retained from file tags");
    }
    check(Audiobooks::probePart(QString::fromLocal8Bit(argv[2]), &book, &error) && book.partNumber == 12 && book.discNumber == 2 &&
          book.chapters.isEmpty() && book.codec == u"mp3", "multipart MP3 disc and track tags preserved without invented chapters");
    std::atomic_bool cancel{true};
    check(!Audiobooks::probePart(QString::fromLocal8Bit(argv[1]), &book, &error, &cancel) && book.path.isEmpty(),
          "cancelled probe returns no stale output");
    check(!Audiobooks::probePart(QString::fromLocal8Bit(argv[3]), &book, &error), "playlist files cannot open remote audio");
    check(!Audiobooks::probePart("https://example.test/book.m4b", &book, &error), "URL rejected by local-only book probe");
    check(!Audiobooks::probePart("/tmp", &book, &error), "directories rejected");
    printf("%d checks, %d failures\n", checks, failed);
    return failed ? 1 : 0;
}
