#include "library/VideoNaming.h"
#include "library/TmdbClient.cpp"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto parse = [](const QString &path, const QString &type) {
        const auto p = VideoNaming::parseIdentity(path, type);
        printf("parse [%s] %s => category='%s' series='%s' S%dE%d\n", qPrintable(type), qPrintable(path),
            qPrintable(p.category), qPrintable(p.series), p.season, p.episode);
    };
    parse("/tv/Show/Season 2/Show E03.mkv", "tv");
    parse("/tv/Show/Specials/Show E03.mkv", "tv");
    parse("/tv/Show/Season 2/Show 2x03.mkv", "tv");
    parse("/tv/Show/Season 2/03 - Title.mkv", "tv");
    parse("/tv/Show/Season 2/03-Title.mkv", "tv");
    parse("/tv/Show/Season 2/Show.S02E03E04.mkv", "tv");
    parse("/tv/Breaking.Bad.S01E01.mkv", "tv");
    parse("/tv/The.Wire.S01E01.mkv", "tv");
    parse("/media/Show/S01E01.mkv", "all");
    parse("/media/Star Wars Episode 4.mkv", "all");
    parse("/movies/Star Wars Episode 4.mkv", "movies");
    parse("/tv/Show/Season.2/Show.S02E03.mkv", "tv");
    for (const QString name : {QString("1917.mkv"), QString("1984.mkv"), QString("2001 A Space Odyssey.mkv"),
            QString("Blade Runner 2049.mkv"), QString("1917 (2019).mkv"), QString("The Final Cut (2004).mkv"),
            QString("Mission Impossible - FALLOUT.mkv")}) {
        const QString cleaned = VideoNaming::cleanFilenameForSearch(name);
        QString year;
        const QString query = splitTrailingYear(cleaned, &year);
        printf("query %s => cleaned='%s' actualTMDBQuery='%s' year='%s'\n", qPrintable(name),
            qPrintable(cleaned), qPrintable(query), qPrintable(year));
    }
    const double parenthesized = VideoNaming::scoreCandidate("Dark (2017)", "", "Dark", "2017", 100);
    const double unrelated = VideoNaming::scoreCandidate("Dark (2017)", "", "Dark Matter", "2024", 100);
    printf("candidate Dark (2017): correct=%.3f unrelated=%.3f threshold=%.3f\n", parenthesized, unrelated, VideoNaming::kAcceptThreshold);
}
