#pragma once

#include <QString>
#include <QByteArray>
#include <QVector>
#include <atomic>

struct AudiobookChapter {
    int sourceIndex = 0;
    QString title;
    qint64 startMs = 0, endMs = 0;
};

struct AudiobookPart {
    QString path, bookTitle, partTitle, author, narrator, codec, container;
    bool bookTitleFromTag = false;
    QByteArray embeddedArtwork; // bounded original attached picture; decoded by the UI worker
    qint64 bytes = 0, durationMs = 0;
    int discNumber = 0, partNumber = 0; // zero means unknown, never guessed
    QVector<AudiobookChapter> chapters;
};

namespace Audiobooks {
// Local regular audio files only. Native FFmpeg, no subprocess/network or file
// mutation. Worker-thread API; cancellation and a 15-second deadline interrupt
// probing. Chapters retain source order/times; malformed timelines fail without
// partial output. A successful probe is NOT evidence of Zune codec/book support.
bool probePart(const QString &path, AudiobookPart *out, QString *error,
               const std::atomic_bool *cancel = nullptr);
}
