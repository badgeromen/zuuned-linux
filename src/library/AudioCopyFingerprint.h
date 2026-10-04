#pragma once
#include <QCryptographicHash>
#include <QFile>
#include <QString>
#include <atomic>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
}

// Copy evidence, not acoustic similarity. Read every encoded audio packet,
// excluding tags and attached artwork. A different encoding remains independent.
namespace AudioCopyFingerprint {
inline QString read(const QString &path, const std::atomic_bool &cancel) {
    if (cancel.load()) return {};
    AVFormatContext *format = avformat_alloc_context();
    if (!format) return {};
    format->interrupt_callback = {[](void *opaque) -> int {
        return static_cast<const std::atomic_bool *>(opaque)->load() ? 1 : 0;
    }, const_cast<std::atomic_bool *>(&cancel)};
    const QByteArray filename = QFile::encodeName(path);
    if (avformat_open_input(&format, filename.constData(), nullptr, nullptr) < 0) {
        avformat_free_context(format);
        return {};
    }
    struct Close { AVFormatContext *&p; ~Close() { avformat_close_input(&p); } } close{format};
    if (cancel.load() || avformat_find_stream_info(format, nullptr) < 0) return {};
    int selected = -1;
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        if (format->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) continue;
        if (selected >= 0) return {}; // Ambiguous multi-audio container.
        selected = int(i);
    }
    if (selected < 0) return {};
    const auto *par = format->streams[selected]->codecpar;
    char layout[256]{};
    if (par->sample_rate <= 0 || par->ch_layout.nb_channels <= 0
        || av_channel_layout_describe(&par->ch_layout, layout, sizeof layout) < 0) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const QByteArray descriptor = QByteArray("zuuned-audio-copy-v1\n")
        + QByteArray::number(par->codec_id) + '\n' + QByteArray::number(par->sample_rate)
        + '\n' + layout + '\n' + QByteArray::number(par->bits_per_raw_sample) + '\n';
    hash.addData(descriptor);
    // Codec initialization affects decoding. FLAC STREAMINFO contains size/MD5
    // hints, not tags; copied streams preserve it. Never trust its MD5 alone.
    if (par->extradata_size < 0) return {};
    hash.addData(QByteArray::number(par->extradata_size) + ":");
    if (par->extradata_size > 0) {
        if (!par->extradata || par->extradata_size > 16 * 1024 * 1024) return {};
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(par->extradata), par->extradata_size));
    }
    AVPacket *packet = av_packet_alloc();
    if (!packet) return {};
    struct Free { AVPacket *&p; ~Free() { av_packet_free(&p); } } free{packet};
    quint64 bytes = 0;
    int result = 0;
    while (!cancel.load() && (result = av_read_frame(format, packet)) >= 0) {
        if (packet->stream_index == selected) {
            if ((packet->flags & AV_PKT_FLAG_CORRUPT) || packet->size < 0
                || (packet->size > 0 && !packet->data)) return {};
            hash.addData(QByteArray::number(packet->size) + ":");
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(packet->data), packet->size));
            bytes += quint64(packet->size);
        }
        av_packet_unref(packet);
    }
    if (cancel.load() || result != AVERROR_EOF || !bytes
        || (format->pb && format->pb->error < 0 && format->pb->error != AVERROR_EOF)) return {};
    return QStringLiteral("audio-v1:") + QString::fromLatin1(hash.result().toHex());
}
}
