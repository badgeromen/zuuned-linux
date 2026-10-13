#include "AudiobookProbe.h"

#include <QDeadlineTimer>
#include <QFile>
#include <QFileInfo>
#include <limits>
#include <sys/stat.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
}

namespace {
struct Input {
    QFile file;
    QDeadlineTimer deadline{15000};
    const std::atomic_bool *cancel = nullptr;
    bool stopped() const { return deadline.hasExpired() || (cancel && cancel->load()); }
};
int interrupted(void *opaque) { return static_cast<Input *>(opaque)->stopped(); }
int readBytes(void *opaque, uint8_t *buffer, int size)
{
    auto &input = *static_cast<Input *>(opaque);
    if (input.stopped()) return AVERROR_EXIT;
    const auto n = input.file.read(reinterpret_cast<char *>(buffer), size);
    return n < 0 ? AVERROR(EIO) : n == 0 ? AVERROR_EOF : int(n);
}
int64_t seekBytes(void *opaque, int64_t offset, int whence)
{
    auto &input = *static_cast<Input *>(opaque);
    if (input.stopped()) return AVERROR_EXIT;
    if (whence == AVSEEK_SIZE) return input.file.size();
    whence &= ~AVSEEK_FORCE;
    qint64 origin = 0;
    if (whence == SEEK_CUR) origin = input.file.pos();
    else if (whence == SEEK_END) origin = input.file.size();
    else if (whence != SEEK_SET) return AVERROR(EINVAL);
    if (offset > std::numeric_limits<qint64>::max() - origin || offset < -origin)
        return AVERROR(EINVAL);
    const auto position = origin + offset;
    return input.file.seek(position) ? position : AVERROR(EIO);
}
int denyExternalOpen(AVFormatContext *, AVIOContext **, const char *, int, AVDictionary **)
{
    // Container references must not open other files or remote resources.
    return AVERROR(EPERM);
}
QString tag(AVFormatContext *format, AVStream *stream, const char *name)
{
    for (AVDictionary *tags : {format->metadata, stream->metadata}) {
        const auto *entry = av_dict_get(tags, name, nullptr, 0);
        if (entry) {
            const auto value = QString::fromUtf8(entry->value).trimmed();
            if (!value.isEmpty()) return value;
        }
    }
    return {};
}
int number(const QString &tag)
{
    bool ok = false;
    const auto value = tag.section(u'/', 0, 0).trimmed().toInt(&ok);
    return ok && value > 0 ? value : 0;
}
qint64 milliseconds(int64_t value, AVRational base)
{
    if (value == AV_NOPTS_VALUE || value < 0 || base.num <= 0 || base.den <= 0) return -1;
    const auto result = av_rescale_q(value, base, AVRational{1, 1000});
    return result >= 0 && result != INT64_MAX ? result : -1;
}
}

bool Audiobooks::probePart(const QString &path, AudiobookPart *out, QString *error,
                           const std::atomic_bool *cancel)
{
    if (out) *out = {};
    if (error) error->clear();
    const auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    const QFileInfo info(path);
    if (!out || !info.isFile() || info.size() <= 0)
        return fail(QStringLiteral("Select a readable local audio file"));
    Input input;
    input.cancel = cancel;
    input.file.setFileName(info.canonicalFilePath());
    if (input.stopped()) return fail(QStringLiteral("Audiobook probe cancelled"));
    if (!input.file.open(QIODevice::ReadOnly)) return fail(input.file.errorString());
    struct stat before{};
    if (fstat(input.file.handle(), &before) || !S_ISREG(before.st_mode))
        return fail(QStringLiteral("Audio source is not a regular file"));
    auto *buffer = static_cast<unsigned char *>(av_malloc(32768));
    auto *io = buffer ? avio_alloc_context(buffer, 32768, 0, &input, readBytes, nullptr, seekBytes) : nullptr;
    if (!io) { av_free(buffer); return fail(QStringLiteral("Cannot allocate audio reader")); }
    auto *format = avformat_alloc_context();
    auto release = [&] {
        if (format) avformat_close_input(&format);
        // FFmpeg can replace the supplied buffer during probing.
        av_freep(&io->buffer);
        avio_context_free(&io);
    };
    if (!format) { release(); return fail(QStringLiteral("Cannot allocate audio probe")); }
    format->pb = io;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    format->interrupt_callback = {interrupted, &input};
    format->io_open = denyExternalOpen;
    AVDictionary *options = nullptr;
    av_dict_set(&options, "format_whitelist", "mp3,mov,asf,wav,flac,ogg,aac", 0);
    av_dict_set(&options, "protocol_whitelist", "", 0);
    av_dict_set(&options, "probesize", "8388608", 0);
    av_dict_set(&options, "analyzeduration", "5000000", 0);
    int result = avformat_open_input(&format, nullptr, nullptr, &options);
    av_dict_free(&options);
    if (result >= 0) result = avformat_find_stream_info(format, nullptr);
    const int audio = result >= 0 ? av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) : -1;
    if (audio < 0 || input.stopped()) {
        release();
        return fail(input.stopped() ? QStringLiteral("Audiobook probe cancelled or timed out")
                                   : QStringLiteral("File could not be read as supported unprotected audio"));
    }
    AudiobookPart part;
    const auto *parameters = format->streams[audio]->codecpar;
    auto *stream = format->streams[audio];
    part.path = info.canonicalFilePath();
    part.bytes = before.st_size;
    part.codec = QString::fromUtf8(avcodec_get_name(parameters->codec_id));
    part.container = QString::fromUtf8(format->iformat->name);
    part.bookTitle = tag(format, stream, "album");
    part.bookTitleFromTag = !part.bookTitle.isEmpty();
    part.partTitle = tag(format, stream, "title");
    if (part.partTitle.isEmpty()) part.partTitle = info.completeBaseName();
    if (part.bookTitle.isEmpty()) part.bookTitle = part.partTitle;
    part.author = tag(format, stream, "artist");
    if (part.author.isEmpty()) part.author = tag(format, stream, "album_artist");
    part.narrator = tag(format, stream, "narrator");
    part.discNumber = number(tag(format, stream, "disc"));
    part.partNumber = number(tag(format, stream, "track"));
    part.durationMs = milliseconds(stream->duration, stream->time_base);
    if (part.durationMs <= 0) part.durationMs = milliseconds(format->duration, AVRational{1, AV_TIME_BASE});
    bool valid = part.durationMs > 0 && format->nb_chapters <= 10000;
    qint64 end = 0;
    for (unsigned i = 0; valid && i < format->nb_chapters; ++i) {
        const auto *chapter = format->chapters[i];
        AudiobookChapter c;
        c.sourceIndex = int(i);
        c.startMs = milliseconds(chapter->start, chapter->time_base);
        c.endMs = milliseconds(chapter->end, chapter->time_base);
        const auto *title = av_dict_get(chapter->metadata, "title", nullptr, 0);
        if (title) c.title = QString::fromUtf8(title->value);
        // One millisecond allows rational timestamp rounding, not a lost chapter.
        valid = c.startMs >= end && c.endMs > c.startMs && c.endMs <= part.durationMs + 1;
        end = c.endMs;
        part.chapters.append(c);
    }
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        const auto *s = format->streams[i];
        if ((s->disposition & AV_DISPOSITION_ATTACHED_PIC) &&
            s->attached_pic.size > 0 && s->attached_pic.size <= 10 * 1024 * 1024) {
            part.embeddedArtwork = QByteArray(reinterpret_cast<const char *>(s->attached_pic.data), s->attached_pic.size);
            break;
        }
    }
    struct stat after{};
    const bool unchanged = fstat(input.file.handle(), &after) == 0 &&
        before.st_size == after.st_size && before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
        before.st_mtim.tv_nsec == after.st_mtim.tv_nsec && before.st_ctim.tv_sec == after.st_ctim.tv_sec &&
        before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
    release();
    if (input.stopped()) return fail(QStringLiteral("Audiobook probe cancelled or timed out"));
    if (!unchanged) return fail(QStringLiteral("Audio file changed during inspection; retry import"));
    if (!valid) return fail(QStringLiteral("Audio duration or chapter timeline is invalid"));
    *out = std::move(part);
    return true;
}
