/*
 * libav_transcode.c — In-process transcoding via libav* + LAME
 *
 * Shared logic with ZuunedMac (Services/Transcode/libav_transcode.c);
 * platform differences live behind #ifdef __APPLE__:
 *   macOS: MPVKit xcframeworks, h264_videotoolbox HW encode
 *   Linux: system FFmpeg, libx264 SW encode (wmv2/wmav2 stock)
 *
 * Replaces ALL popen(ffmpeg)/Process(ffmpeg) calls with direct C API usage.
 */

#include "libav_transcode.h"
#include "id3_rewrite.h"
#include "disc_tag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* libav* headers (via MPVKit xcframeworks, symlinked in vendor/ffmpeg-include) */
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/dict.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/hwcontext.h>
#include <libavutil/audio_fifo.h>

/* LAME MP3 encoder (standalone library) */
#include <lame/lame.h>

#define LOG_PREFIX "[zuuned-transcode]"

/* ---- Cancellation ----
 * One transcode runs at a time in the app, so a single flag suffices.
 * The sync engine sets it on cancel; both encode loops poll it per
 * input packet and bail (goto cleanup → unlink temp → return NULL).
 * Cleared by the engine at sync START — not here — so a cancel landing
 * between two queue entries still kills the next transcode. */
static volatile int g_transcode_abort = 0;

void zuuned_transcode_abort(void)       { g_transcode_abort = 1; }
void zuuned_transcode_clear_abort(void) { g_transcode_abort = 0; }

/* ---- Hardware decode toggle ----
 * Default ON. The sync engine mirrors the Settings "hwDecode" flag here
 * before each sync; transcodetool leaves the default. Global (not
 * per-call) — concurrent transcodes share the same policy. */
/* 0 sw · 1 auto (NVDEC→VAAPI→sw) · 2 NVDEC only · 3 VAAPI only */
static volatile int g_hw_decode = 1;
void zuuned_transcode_set_hw_decode(int mode) { g_hw_decode = mode; }

/* ---- Helper: build temp output path ---- */

static char *make_temp_path(const char *input_path, const char *suffix, const char *ext)
{
    const char *base = strrchr(input_path, '/');
    base = base ? base + 1 : input_path;
    const char *dot = strrchr(base, '.');
    size_t base_len = dot ? (size_t)(dot - base) : strlen(base);

    char path[512];
    snprintf(path, sizeof(path), "/tmp/zuuned_%s_%.*s_%d.%s",
             suffix, (int)base_len, base, (int)getpid(), ext);
    return strdup(path);
}

/* Container metadata wins; Ogg and several other demuxers keep tags on
 * the selected audio stream instead. Never merge tags from another stream. */
static const char *audio_tag(AVFormatContext *format, AVStream *stream, const char *key)
{
    AVDictionaryEntry *tag = av_dict_get(format->metadata, key, NULL, 0);
    if (tag && tag->value[0]) return tag->value;
    tag = av_dict_get(stream->metadata, key, NULL, 0);
    return tag && tag->value[0] ? tag->value : NULL;
}

/* ==================================================================== */
/*  Audio Transcoding: any format → MP3 320kbps CBR                     */
/* ==================================================================== */

char *zuuned_transcode_audio(const char *input_path, const char *album_artist,
                              zuuned_progress_cb progress, void *userdata)
{
    return zuuned_transcode_audio_with_disc(input_path, album_artist, 0, progress, userdata);
}

char *zuuned_transcode_audio_with_disc(const char *input_path, const char *album_artist,
                                      int disc_number, zuuned_progress_cb progress, void *userdata)
{
    if (!input_path) return NULL;

    char *output_path = make_temp_path(input_path, "audio", "mp3");
    if (!output_path) return NULL;

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *dec_ctx = NULL;
    SwrContext *swr_ctx = NULL;
    lame_t lame = NULL;
    FILE *fout = NULL;
    AVFrame *frame = NULL;
    AVPacket *pkt = NULL;
    int ret = -1;

    /* Open input */
    if (avformat_open_input(&fmt_ctx, input_path, NULL, NULL) < 0) {
        fprintf(stderr, LOG_PREFIX " failed to open: %s\n", input_path);
        goto cleanup;
    }
    avformat_find_stream_info(fmt_ctx, NULL);

    /* Find best audio stream */
    int audio_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_idx < 0) {
        fprintf(stderr, LOG_PREFIX " no audio stream in: %s\n", input_path);
        goto cleanup;
    }

    AVStream *audio_stream = fmt_ctx->streams[audio_idx];
    const AVCodec *decoder = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (!decoder) {
        fprintf(stderr, LOG_PREFIX " no decoder for codec %d\n", audio_stream->codecpar->codec_id);
        goto cleanup;
    }

    dec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(dec_ctx, audio_stream->codecpar);
    if (avcodec_open2(dec_ctx, decoder, NULL) < 0) {
        fprintf(stderr, LOG_PREFIX " failed to open decoder\n");
        goto cleanup;
    }

    /* Set up resampler: input format → 44100 Hz stereo s16 */
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    int out_sample_rate = 44100;

    if (swr_alloc_set_opts2(&swr_ctx,
                            &out_ch_layout, AV_SAMPLE_FMT_S16, out_sample_rate,
                            &dec_ctx->ch_layout, dec_ctx->sample_fmt, dec_ctx->sample_rate,
                            0, NULL) < 0 || swr_init(swr_ctx) < 0) {
        fprintf(stderr, LOG_PREFIX " failed to init resampler\n");
        goto cleanup;
    }

    /* Initialize LAME encoder */
    lame = lame_init();
    if (!lame) {
        fprintf(stderr, LOG_PREFIX " lame_init failed\n");
        goto cleanup;
    }
    lame_set_in_samplerate(lame, out_sample_rate);
    lame_set_num_channels(lame, 2);
    lame_set_VBR(lame, vbr_off);    /* CBR mode */
    lame_set_brate(lame, 320);       /* 320 kbps */
    lame_set_quality(lame, 2);       /* High quality */
    lame_set_bWriteVbrTag(lame, 0);  /* No Xing header (we write ID3 manually) */
    if (lame_init_params(lame) < 0) {
        fprintf(stderr, LOG_PREFIX " lame_init_params failed\n");
        goto cleanup;
    }

    /* Open output file */
    fout = fopen(output_path, "wb");
    if (!fout) {
        fprintf(stderr, LOG_PREFIX " can't create: %s\n", output_path);
        goto cleanup;
    }

    /* Reserve space for ID3v2.3 header (we'll write it after encoding) */
    /* For now, just write raw MP3 frames. We'll prepend the ID3 tag after. */
    /* Actually, we'll write a placeholder and seek back later. */

    /* Encode loop */
    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    if (!frame || !pkt) goto cleanup;

    double duration = (fmt_ctx->duration > 0) ? (double)fmt_ctx->duration / AV_TIME_BASE : 0;
    uint8_t mp3_buf[16384]; /* LAME output buffer */

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (g_transcode_abort) {
            fprintf(stderr, LOG_PREFIX " audio transcode ABORTED\n");
            av_packet_unref(pkt);
            goto cleanup;   /* ret != 0 → temp unlinked, NULL returned */
        }
        if (pkt->stream_index != audio_idx) {
            av_packet_unref(pkt);
            continue;
        }

        if (avcodec_send_packet(dec_ctx, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }

        while (avcodec_receive_frame(dec_ctx, frame) >= 0) {
            /* Resample to s16 stereo */
            int out_samples = swr_get_out_samples(swr_ctx, frame->nb_samples);
            int16_t *pcm_buf = malloc(out_samples * 2 * sizeof(int16_t));
            if (!pcm_buf) continue;

            uint8_t *out_ptr = (uint8_t *)pcm_buf;
            int converted = swr_convert(swr_ctx, &out_ptr, out_samples,
                                        (const uint8_t **)frame->extended_data, frame->nb_samples);
            /* Feed LAME in ≤4096-sample chunks — codecs with large
             * superframes (wmav2 decodes thousands of samples per frame)
             * overflow LAME's internal buffer in a single call
             * (assertion mf_size <= MFSIZE). */
            for (int off = 0; off < converted; off += 4096) {
                int n = converted - off;
                if (n > 4096) n = 4096;
                int mp3_bytes = lame_encode_buffer_interleaved(
                    lame, pcm_buf + (size_t)off * 2, n, mp3_buf, sizeof(mp3_buf));
                if (mp3_bytes > 0) fwrite(mp3_buf, 1, mp3_bytes, fout);
            }
            free(pcm_buf);

            /* Progress callback */
            if (progress && duration > 0) {
                double pts = frame->pts * av_q2d(audio_stream->time_base);
                float frac = (float)(pts / duration);
                if (frac > 0.99f) frac = 0.99f;
                if (frac > 0) progress(frac, userdata);
            }
        }

        av_packet_unref(pkt);
    }

    /* Flush decoder */
    avcodec_send_packet(dec_ctx, NULL);
    while (avcodec_receive_frame(dec_ctx, frame) >= 0) {
        int out_samples = swr_get_out_samples(swr_ctx, frame->nb_samples);
        int16_t *pcm_buf = malloc(out_samples * 2 * sizeof(int16_t));
        if (!pcm_buf) continue;
        uint8_t *out_ptr = (uint8_t *)pcm_buf;
        int converted = swr_convert(swr_ctx, &out_ptr, out_samples,
                                    (const uint8_t **)frame->extended_data, frame->nb_samples);
        for (int off = 0; off < converted; off += 4096) {
            int n = converted - off;
            if (n > 4096) n = 4096;
            int mp3_bytes = lame_encode_buffer_interleaved(
                lame, pcm_buf + (size_t)off * 2, n, mp3_buf, sizeof(mp3_buf));
            if (mp3_bytes > 0) fwrite(mp3_buf, 1, mp3_bytes, fout);
        }
        free(pcm_buf);
    }

    /* Flush resampler */
    {
        int16_t flush_pcm[8192];
        uint8_t *flush_ptr = (uint8_t *)flush_pcm;
        int flushed = swr_convert(swr_ctx, &flush_ptr, 4096, NULL, 0);
        if (flushed > 0) {
            int mp3_bytes = lame_encode_buffer_interleaved(
                lame, flush_pcm, flushed, mp3_buf, sizeof(mp3_buf));
            if (mp3_bytes > 0) fwrite(mp3_buf, 1, mp3_bytes, fout);
        }
    }

    /* Flush LAME encoder */
    {
        int final_bytes = lame_encode_flush(lame, mp3_buf, sizeof(mp3_buf));
        if (final_bytes > 0) fwrite(mp3_buf, 1, final_bytes, fout);
    }

    fclose(fout);
    fout = NULL;

    /* Now prepend ID3v2.3 tag with metadata from input file.
     * Strategy: write temp MP3 data to temp1, then write final file with ID3+data.
     * Since we already wrote the raw MP3 data, we need to rebuild with the tag. */
    {
        /* Read the raw MP3 we just wrote */
        FILE *raw = fopen(output_path, "rb");
        if (!raw) goto cleanup;
        fseek(raw, 0, SEEK_END);
        long raw_size = ftell(raw);
        fseek(raw, 0, SEEK_SET);
        uint8_t *raw_data = malloc(raw_size);
        if (!raw_data) { fclose(raw); goto cleanup; }
        fread(raw_data, 1, raw_size, raw);
        fclose(raw);

        /* Build ID3v2.3 tag with metadata from input */
        FILE *final_fp = fopen(output_path, "wb");
        if (!final_fp) { free(raw_data); goto cleanup; }

        /* Collect metadata from input format context */
        const char *title = NULL, *artist = NULL, *album = NULL;
        const char *genre = NULL, *track = NULL, *year = NULL;
        AVDictionaryEntry *tag;
        title = audio_tag(fmt_ctx, audio_stream, "title");
        artist = audio_tag(fmt_ctx, audio_stream, "artist");
        album = audio_tag(fmt_ctx, audio_stream, "album");
        genre = audio_tag(fmt_ctx, audio_stream, "genre");
        track = audio_tag(fmt_ctx, audio_stream, "track");
        year = audio_tag(fmt_ctx, audio_stream, "date");

        char disc[48] = {0};
        if (disc_number > 0) snprintf(disc, sizeof(disc), "%d", disc_number);
        else {
            AVDictionary *sources[] = {fmt_ctx->metadata, audio_stream->metadata};
            const char *keys[] = {"disc", "discnumber"};
            for (int source = 0; source < 2 && !disc[0]; ++source)
                for (int key = 0; key < 2 && !disc[0]; ++key) {
                    tag = av_dict_get(sources[source], keys[key], NULL, 0);
                    if (tag) normalize_disc_tag(tag->value, disc);
                }
        }

        /* Override artist with album_artist so Zune groups tracks under one
         * artist instead of splitting on per-track "feat. X" strings. */
        if (album_artist && album_artist[0]) artist = album_artist;

        /* Write simple ID3v2.3 tag */
        /* Calculate frame sizes: each text frame = 10 (header) + 1 (encoding) + strlen */
        uint32_t frames_size = 0;
        struct { const char *id; const char *val; } tags[] = {
            {"TIT2", title}, {"TPE1", artist}, {"TALB", album},
            {"TCON", genre}, {"TRCK", track}, {"TYER", year}, {"TPOS", disc},
        };
        int ntags = sizeof(tags) / sizeof(tags[0]);
        for (int i = 0; i < ntags; i++) {
            if (tags[i].val && tags[i].val[0])
                frames_size += 10 + 1 + (uint32_t)strlen(tags[i].val);
        }

        uint32_t padding = 1024;
        uint32_t tag_content = frames_size + padding;

        /* ID3v2.3 header */
        uint8_t hdr[10] = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 0};
        hdr[6] = (tag_content >> 21) & 0x7F;
        hdr[7] = (tag_content >> 14) & 0x7F;
        hdr[8] = (tag_content >> 7)  & 0x7F;
        hdr[9] = tag_content & 0x7F;
        fwrite(hdr, 1, 10, final_fp);

        /* Write text frames */
        for (int i = 0; i < ntags; i++) {
            if (!tags[i].val || !tags[i].val[0]) continue;
            uint32_t data_len = 1 + (uint32_t)strlen(tags[i].val);
            uint8_t fhdr[10];
            memcpy(fhdr, tags[i].id, 4);
            fhdr[4] = (data_len >> 24) & 0xFF;
            fhdr[5] = (data_len >> 16) & 0xFF;
            fhdr[6] = (data_len >> 8)  & 0xFF;
            fhdr[7] = data_len & 0xFF;
            fhdr[8] = 0; fhdr[9] = 0;
            fwrite(fhdr, 1, 10, final_fp);
            uint8_t enc = 0x00; /* ISO-8859-1 */
            fwrite(&enc, 1, 1, final_fp);
            fwrite(tags[i].val, 1, strlen(tags[i].val), final_fp);
        }

        /* Padding */
        uint8_t zeros[1024];
        memset(zeros, 0, sizeof(zeros));
        fwrite(zeros, 1, padding, final_fp);

        /* Raw MP3 data */
        fwrite(raw_data, 1, raw_size, final_fp);

        /* ID3v1 tag */
        uint8_t v1[128];
        memset(v1, 0, sizeof(v1));
        memcpy(v1, "TAG", 3);
        if (title)  strncpy((char *)v1 + 3,  title,  30);
        if (artist) strncpy((char *)v1 + 33, artist, 30);
        if (album)  strncpy((char *)v1 + 63, album,  30);
        if (year)   strncpy((char *)v1 + 93, year,   4);
        if (track) {
            int t = atoi(track);
            if (t > 0 && t < 256) { v1[125] = 0; v1[126] = (uint8_t)t; }
        }
        v1[127] = 0xFF;
        fwrite(v1, 1, 128, final_fp);

        fclose(final_fp);
        free(raw_data);
    }

    ret = 0;
    if (progress) progress(1.0f, userdata);
    fprintf(stderr, LOG_PREFIX " audio transcode OK: %s\n", output_path);

cleanup:
    if (pkt) av_packet_free(&pkt);
    if (frame) av_frame_free(&frame);
    if (lame) lame_close(lame);
    if (swr_ctx) swr_free(&swr_ctx);
    if (dec_ctx) avcodec_free_context(&dec_ctx);
    if (fmt_ctx) avformat_close_input(&fmt_ctx);
    if (fout) fclose(fout);

    if (ret != 0) {
        unlink(output_path);
        free(output_path);
        return NULL;
    }
    return output_path;
}

/* ==================================================================== */
/*  Video Transcoding: any format → H.264/AAC MP4 (or WMV for Zune 30) */
/* ==================================================================== */

#ifdef __APPLE__
/* VideoToolbox hardware pixel format selector.
 * Called by the decoder to choose between SW and HW output formats.
 * Returns VT format if available, falls back to first SW format. */
static enum AVPixelFormat get_vt_hw_format(AVCodecContext *ctx,
                                            const enum AVPixelFormat *pix_fmts)
{
    (void)ctx;
    for (const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == AV_PIX_FMT_VIDEOTOOLBOX)
            return AV_PIX_FMT_VIDEOTOOLBOX;
    }
    return pix_fmts[0];
}
#endif /* __APPLE__ */

#ifndef __APPLE__
/* NVDEC (CUDA) pixel format selector — Linux mirror of the VT one. */
static enum AVPixelFormat get_cuda_hw_format(AVCodecContext *ctx,
                                             const enum AVPixelFormat *pix_fmts)
{
    (void)ctx;
    for (const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == AV_PIX_FMT_CUDA)
            return AV_PIX_FMT_CUDA;
    }
    return pix_fmts[0];
}

/* VAAPI (AMD/Intel) pixel format selector */
static enum AVPixelFormat get_vaapi_hw_format(AVCodecContext *ctx,
                                              const enum AVPixelFormat *pix_fmts)
{
    (void)ctx;
    for (const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == AV_PIX_FMT_VAAPI)
            return AV_PIX_FMT_VAAPI;
    }
    return pix_fmts[0];
}
#endif /* !__APPLE__ */

/* Platform H.264 encoder: VideoToolbox HW on macOS, libx264 SW on Linux.
 * (Linux hardware encode — VAAPI/NVENC — can come later; software x264
 * at Zune resolutions is plenty fast.) */
#ifdef __APPLE__
#define ZUUNED_H264_ENCODER "h264_videotoolbox"
#else
#define ZUUNED_H264_ENCODER "libx264"
#endif

/* Video profile parameters */
typedef struct {
    int width, height;
    int video_bitrate;
    int audio_bitrate;
    int audio_sample_rate;
    const char *h264_profile;
    const char *h264_level;
    const char *encoder_name;   /* "h264_videotoolbox" or "wmv2" */
    const char *format_name;    /* "mp4" or "asf" */
    const char *file_ext;       /* "mp4" or "wmv" */
} VideoProfileParams;

static const VideoProfileParams PROFILES[] = {
    /* 0: Zune 30 — wmv2 + wmav2 in ASF (stock in system FFmpeg on Linux;
     *    on macOS requires the forked MPVKit) */
    { 320, 240, 800000, 128000, 44100, NULL, NULL, "wmv2", "asf", "wmv" },
    /* 1: Zune 4/8/16/80/120 */
    { 320, 240, 768000, 128000, 44100, "baseline", "2.1", ZUUNED_H264_ENCODER, "mp4", "mp4" },
    /* 2: Zune HD */
    { 480, 272, 2500000, 192000, 44100, "baseline", "3.1", ZUUNED_H264_ENCODER, "mp4", "mp4" },
    /* 3: Zune HD 720p */
    { 1280, 720, 8000000, 192000, 44100, "baseline", "3.1", ZUUNED_H264_ENCODER, "mp4", "mp4" },
};

char *zuuned_transcode_video(const char *input_path, int profile,
                              const char *series, int season, int episode,
                              const char *audio_lang,
                              zuuned_progress_cb progress, void *userdata)
{
    if (!input_path || profile < 0 || profile > 3) return NULL;
    const VideoProfileParams *pp = &PROFILES[profile];

    char *output_path = make_temp_path(input_path, "vid", pp->file_ext);
    if (!output_path) return NULL;

    /* Contexts */
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVCodecContext *vdec_ctx = NULL, *adec_ctx = NULL;
    AVCodecContext *venc_ctx = NULL, *aenc_ctx = NULL;
    struct SwsContext *sws_ctx = NULL;
    SwrContext *swr_ctx = NULL;
    AVBufferRef *hw_device_ref = NULL;   /* encoder HW (VideoToolbox) */
    AVBufferRef *hw_decode_ref = NULL;   /* decoder HW (NVDEC on Linux) */
    AVAudioFifo *audio_fifo = NULL;
    AVFrame *dec_frame = NULL, *sws_frame = NULL, *pad_frame = NULL, *swr_frame = NULL;
    AVPacket *pkt = NULL, *out_pkt = NULL;
    int ret = -1;
    int video_stream_idx = -1, audio_stream_idx = -1;
    int out_video_idx = -1, out_audio_idx = -1;
    int scaled_w = 0, scaled_h = 0, pad_x = 0, pad_y = 0;
    int64_t total_duration = 0;

    /* Open input */
    if (avformat_open_input(&ifmt_ctx, input_path, NULL, NULL) < 0) {
        fprintf(stderr, LOG_PREFIX " video: can't open %s\n", input_path);
        goto vcleanup;
    }
    avformat_find_stream_info(ifmt_ctx, NULL);
    total_duration = ifmt_ctx->duration;

    /* Find video stream */
    video_stream_idx = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_idx < 0) {
        fprintf(stderr, LOG_PREFIX " video: no video stream\n");
        goto vcleanup;
    }

    /* Find audio stream (optionally by language) */
    if (audio_lang) {
        for (int i = 0; i < (int)ifmt_ctx->nb_streams; i++) {
            if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                AVDictionaryEntry *lang_tag = av_dict_get(ifmt_ctx->streams[i]->metadata, "language", NULL, 0);
                if (lang_tag && strcmp(lang_tag->value, audio_lang) == 0) {
                    audio_stream_idx = i;
                    break;
                }
            }
        }
    }
    if (audio_stream_idx < 0) {
        audio_stream_idx = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    }

    /* Open video decoder — try VideoToolbox HW, fallback to multi-thread SW */
    {
        AVStream *vs = ifmt_ctx->streams[video_stream_idx];
        const AVCodec *vdec = avcodec_find_decoder(vs->codecpar->codec_id);
        if (!vdec) goto vcleanup;
        vdec_ctx = avcodec_alloc_context3(vdec);
        avcodec_parameters_to_context(vdec_ctx, vs->codecpar);

        vdec_ctx->thread_count = 0; /* All CPU cores for SW path */

#ifdef __APPLE__
        /* Try VideoToolbox HW decode (reuse encoder's hw_device_ref) */
        if (hw_device_ref) {
            vdec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ref);
            vdec_ctx->get_format = get_vt_hw_format;
        }
#else
        /* NVDEC: 1080p HEVC decode is the dominant transcode cost and
         * the GPU's dedicated decoder does it nearly for free. Device
         * create fails harmlessly on boxes without NVIDIA/libcuda —
         * we fall through to the threaded SW path. */
        if (g_hw_decode) {
            const int mode = g_hw_decode;
            if (mode == 1 || mode == 2) {
                int hw_ret = av_hwdevice_ctx_create(&hw_decode_ref,
                                                    AV_HWDEVICE_TYPE_CUDA,
                                                    NULL, NULL, 0);
                if (hw_ret == 0 && hw_decode_ref) {
                    vdec_ctx->hw_device_ctx = av_buffer_ref(hw_decode_ref);
                    vdec_ctx->get_format = get_cuda_hw_format;
                } else {
                    fprintf(stderr, LOG_PREFIX
                            " video: CUDA device unavailable (%d)\n", hw_ret);
                }
            }
            if (!vdec_ctx->hw_device_ctx && (mode == 1 || mode == 3)) {
                int hw_ret = av_hwdevice_ctx_create(&hw_decode_ref,
                                                    AV_HWDEVICE_TYPE_VAAPI,
                                                    NULL, NULL, 0);
                if (hw_ret == 0 && hw_decode_ref) {
                    vdec_ctx->hw_device_ctx = av_buffer_ref(hw_decode_ref);
                    vdec_ctx->get_format = get_vaapi_hw_format;
                } else {
                    fprintf(stderr, LOG_PREFIX
                            " video: VAAPI device unavailable (%d)\n", hw_ret);
                }
            }
            if (!vdec_ctx->hw_device_ctx)
                fprintf(stderr, LOG_PREFIX
                        " video: no HW decode (mode %d) — SW decode\n", mode);
        }
#endif

        if (avcodec_open2(vdec_ctx, vdec, NULL) < 0) {
            /* HW decode failed — retry pure SW */
            if (vdec_ctx->hw_device_ctx) {
                fprintf(stderr, LOG_PREFIX " video: HW decode failed, retrying SW\n");
                avcodec_free_context(&vdec_ctx);
                vdec_ctx = avcodec_alloc_context3(vdec);
                avcodec_parameters_to_context(vdec_ctx, vs->codecpar);
                vdec_ctx->thread_count = 0;
                if (avcodec_open2(vdec_ctx, vdec, NULL) < 0) goto vcleanup;
                fprintf(stderr, LOG_PREFIX " video: decoder OK (SW multi-thread, %dx%d)\n",
                        vdec_ctx->width, vdec_ctx->height);
            } else {
                goto vcleanup;
            }
        } else {
            int is_hw = (vdec_ctx->hw_device_ctx != NULL);
#ifdef __APPLE__
            const char *hw_name = "VideoToolbox HW";
#else
            const char *hw_name = "NVDEC (CUDA) HW";
#endif
            fprintf(stderr, LOG_PREFIX " video: decoder OK (%s, %dx%d)\n",
                    is_hw ? hw_name : "SW multi-thread",
                    vdec_ctx->width, vdec_ctx->height);
        }
    }

    /* Open audio decoder */
    if (audio_stream_idx >= 0) {
        AVStream *as = ifmt_ctx->streams[audio_stream_idx];
        const AVCodec *adec = avcodec_find_decoder(as->codecpar->codec_id);
        if (adec) {
            adec_ctx = avcodec_alloc_context3(adec);
            avcodec_parameters_to_context(adec_ctx, as->codecpar);
            adec_ctx->thread_count = 0; /* All CPU cores */
            if (avcodec_open2(adec_ctx, adec, NULL) < 0) {
                fprintf(stderr, LOG_PREFIX " video: audio decoder failed to open\n");
                avcodec_free_context(&adec_ctx);
                adec_ctx = NULL;
            } else {
                fprintf(stderr, LOG_PREFIX " video: audio decoder OK (%s, %dHz, %dch)\n",
                        adec->name, adec_ctx->sample_rate, adec_ctx->ch_layout.nb_channels);
            }
        } else {
            fprintf(stderr, LOG_PREFIX " video: no audio decoder for codec %d\n",
                    as->codecpar->codec_id);
        }
    }

    /* Open output format */
    if (avformat_alloc_output_context2(&ofmt_ctx, NULL, pp->format_name, output_path) < 0) {
        fprintf(stderr, LOG_PREFIX " video: can't create output context\n");
        goto vcleanup;
    }

    /* Set up video encoder */
    {
        const AVCodec *venc = avcodec_find_encoder_by_name(pp->encoder_name);
        if (!venc) {
            fprintf(stderr, LOG_PREFIX " video: encoder '%s' not available\n", pp->encoder_name);
            goto vcleanup;
        }
        venc_ctx = avcodec_alloc_context3(venc);
        venc_ctx->width = pp->width;
        venc_ctx->height = pp->height;
        venc_ctx->bit_rate = pp->video_bitrate;

        /* Use source video's actual framerate for A/V sync.
         * Hardcoding 30fps causes desync with 24fps/23.976fps sources. */
        AVRational src_fps = av_guess_frame_rate(ifmt_ctx, ifmt_ctx->streams[video_stream_idx], NULL);
        if (src_fps.num <= 0 || src_fps.den <= 0) {
            src_fps = (AVRational){30, 1};
        }
        venc_ctx->time_base = av_inv_q(src_fps);
        venc_ctx->framerate = src_fps;
        fprintf(stderr, LOG_PREFIX " video: source fps=%d/%d\n", src_fps.num, src_fps.den);

        /* Pixel format + hardware context for VideoToolbox encoders */
        int is_videotoolbox = (strstr(pp->encoder_name, "videotoolbox") != NULL);
#ifdef __APPLE__
        if (is_videotoolbox) {
            /* Create VideoToolbox hardware device context.
             * This creates the underlying VTCompressionSession.
             * Without it, avcodec_open2 returns EINVAL. */
            int hw_ret = av_hwdevice_ctx_create(&hw_device_ref,
                                                 AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
                                                 NULL, NULL, 0);
            if (hw_ret < 0 || !hw_device_ref) {
                fprintf(stderr, LOG_PREFIX " video: can't create VideoToolbox device ctx\n");
                goto vcleanup;
            }
            venc_ctx->hw_device_ctx = av_buffer_ref(hw_device_ref);
            venc_ctx->pix_fmt = AV_PIX_FMT_NV12;

            /* Only set profile — let VT auto-negotiate level, GOP, B-frames.
             * VT rejects explicit gop_size, max_b_frames, and string levels like "2.1". */
            if (pp->h264_profile) {
                av_opt_set(venc_ctx->priv_data, "profile", pp->h264_profile, 0);
            }
            av_opt_set(venc_ctx->priv_data, "realtime", "false", 0);
            av_opt_set_int(venc_ctx->priv_data, "allow_sw", 1, 0);
        } else
#else
        (void)is_videotoolbox;
#endif
        {
            /* Software encoders (libx264, wmv2, mpeg4): full control over
             * parameters */
            venc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            venc_ctx->gop_size = 30;
            venc_ctx->max_b_frames = 0;
            if (pp->h264_profile) {
                av_opt_set(venc_ctx->priv_data, "profile", pp->h264_profile, 0);
            }
            if (pp->h264_level) {
                av_opt_set(venc_ctx->priv_data, "level", pp->h264_level, 0);
            }
            if (strcmp(pp->encoder_name, "libx264") == 0) {
                av_opt_set(venc_ctx->priv_data, "preset", "fast", 0);
            }
        }

        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            venc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        int venc_ret = avcodec_open2(venc_ctx, venc, NULL);
        if (venc_ret < 0) {
            char errbuf[128];
            av_strerror(venc_ret, errbuf, sizeof(errbuf));
            fprintf(stderr, LOG_PREFIX " video: can't open encoder '%s': %s (pix_fmt=%d, %dx%d)\n",
                    pp->encoder_name, errbuf, venc_ctx->pix_fmt, pp->width, pp->height);
            goto vcleanup;
        }
        fprintf(stderr, LOG_PREFIX " video: encoder '%s' opened OK (pix_fmt=%d, %dx%d)\n",
                pp->encoder_name, venc_ctx->pix_fmt, pp->width, pp->height);

        AVStream *out_vs = avformat_new_stream(ofmt_ctx, NULL);
        avcodec_parameters_from_context(out_vs->codecpar, venc_ctx);
        out_vs->time_base = venc_ctx->time_base;
        out_video_idx = out_vs->index;
    }

    /* Set up audio encoder (AAC) */
    if (adec_ctx) {
        const char *audio_enc_name = (profile == ZUUNED_PROFILE_ZUNE30) ? "wmav2" : "aac";
        const AVCodec *aenc = avcodec_find_encoder_by_name(audio_enc_name);
        if (!aenc) aenc = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (aenc) {
            aenc_ctx = avcodec_alloc_context3(aenc);
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            av_channel_layout_copy(&aenc_ctx->ch_layout, &stereo);
            aenc_ctx->sample_rate = pp->audio_sample_rate;
            aenc_ctx->bit_rate = pp->audio_bitrate;
#if LIBAVCODEC_VERSION_MAJOR >= 62
            /* FFmpeg 8+: AVCodec.sample_fmts removed — query instead */
            {
                const enum AVSampleFormat *sfmts = NULL;
                int nb_sfmts = 0;
                if (avcodec_get_supported_config(NULL, aenc,
                        AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                        (const void **)&sfmts, &nb_sfmts) < 0 ||
                    !sfmts || nb_sfmts <= 0)
                    aenc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
                else
                    aenc_ctx->sample_fmt = sfmts[0];
            }
#else
            aenc_ctx->sample_fmt = aenc->sample_fmts ? aenc->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
#endif
            aenc_ctx->time_base = (AVRational){1, pp->audio_sample_rate};

            if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                aenc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            if (avcodec_open2(aenc_ctx, aenc, NULL) < 0) {
                fprintf(stderr, LOG_PREFIX " video: audio encoder '%s' FAILED to open\n", audio_enc_name);
                avcodec_free_context(&aenc_ctx);
                aenc_ctx = NULL;
            } else {
                fprintf(stderr, LOG_PREFIX " video: audio encoder OK (%s, %dHz, frame_size=%d, fmt=%d)\n",
                        aenc->name, aenc_ctx->sample_rate, aenc_ctx->frame_size, aenc_ctx->sample_fmt);

                AVStream *out_as = avformat_new_stream(ofmt_ctx, NULL);
                avcodec_parameters_from_context(out_as->codecpar, aenc_ctx);
                out_as->time_base = aenc_ctx->time_base;
                out_audio_idx = out_as->index;

                /* Resampler: input format → encoder format */
                AVChannelLayout out_ch = AV_CHANNEL_LAYOUT_STEREO;
                if (swr_alloc_set_opts2(&swr_ctx,
                                        &out_ch, aenc_ctx->sample_fmt, aenc_ctx->sample_rate,
                                        &adec_ctx->ch_layout, adec_ctx->sample_fmt, adec_ctx->sample_rate,
                                        0, NULL) < 0 || swr_init(swr_ctx) < 0) {
                    fprintf(stderr, LOG_PREFIX " video: audio resampler FAILED\n");
                    swr_free(&swr_ctx);
                } else {
                    fprintf(stderr, LOG_PREFIX " video: audio resampler OK\n");
                    /* FIFO buffers resampled audio into encoder frame_size chunks */
                    audio_fifo = av_audio_fifo_alloc(aenc_ctx->sample_fmt,
                                                      aenc_ctx->ch_layout.nb_channels,
                                                      aenc_ctx->frame_size * 4);
                    if (!audio_fifo)
                        fprintf(stderr, LOG_PREFIX " video: audio FIFO alloc FAILED\n");
                }
            }
        } else {
            fprintf(stderr, LOG_PREFIX " video: audio encoder '%s' not found\n", audio_enc_name);
        }
    }

    /* Set metadata */
    if (series && series[0] && episode > 0) {
        av_dict_set(&ofmt_ctx->metadata, "show", series, 0);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", season);
        av_dict_set(&ofmt_ctx->metadata, "season_number", buf, 0);
        snprintf(buf, sizeof(buf), "%d", episode);
        av_dict_set(&ofmt_ctx->metadata, "episode_sort", buf, 0);
    }

    /* Open output file */
    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmt_ctx->pb, output_path, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, LOG_PREFIX " video: can't open output file\n");
            goto vcleanup;
        }
    }

    /* Set faststart for MP4 */
    AVDictionary *mux_opts = NULL;
    if (strcmp(pp->format_name, "mp4") == 0) {
        av_dict_set(&mux_opts, "movflags", "+faststart", 0);
    }

    if (avformat_write_header(ofmt_ctx, &mux_opts) < 0) {
        fprintf(stderr, LOG_PREFIX " video: write_header failed\n");
        av_dict_free(&mux_opts);
        goto vcleanup;
    }
    av_dict_free(&mux_opts);

    /* Video scaler — deferred to first decoded frame (HW decode changes pix_fmt) */
    /* sws_ctx starts NULL, initialized lazily in the decode loop */

    /* Allocate frames */
    dec_frame = av_frame_alloc();
    sws_frame = av_frame_alloc();
    swr_frame = av_frame_alloc();
    pkt = av_packet_alloc();
    out_pkt = av_packet_alloc();
    if (!dec_frame || !sws_frame || !swr_frame || !pkt || !out_pkt) goto vcleanup;

    sws_frame->format = venc_ctx->pix_fmt;
    sws_frame->width = pp->width;
    sws_frame->height = pp->height;
    av_frame_get_buffer(sws_frame, 0);

    /* Main transcode loop */
    int64_t video_pts = 0;
    int64_t audio_pts = 0;

    fprintf(stderr, LOG_PREFIX " video: starting transcode (duration=%.1fs)\n",
            total_duration > 0 ? (double)total_duration / AV_TIME_BASE : -1.0);

    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        if (g_transcode_abort) {
            fprintf(stderr, LOG_PREFIX " video transcode ABORTED\n");
            av_packet_unref(pkt);
            goto vcleanup;   /* ret != 0 → temp unlinked, NULL returned */
        }
        if (pkt->stream_index == video_stream_idx) {
            /* Decode video frame */
            avcodec_send_packet(vdec_ctx, pkt);
            while (avcodec_receive_frame(vdec_ctx, dec_frame) >= 0) {
                AVFrame *sw_frame = dec_frame;
                AVFrame *hw_download = NULL;

                /* If HW decoded, download from GPU to a software frame.
                 * CUDA: leave format unset so FFmpeg picks the native
                 * transfer format (NV12 for 8-bit, P010 for 10-bit HEVC)
                 * — swscale converts either downstream. */
#ifdef __APPLE__
                if (dec_frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
                    hw_download = av_frame_alloc();
                    hw_download->format = AV_PIX_FMT_NV12;
                    if (av_hwframe_transfer_data(hw_download, dec_frame, 0) < 0) {
                        av_frame_free(&hw_download);
                        continue;
                    }
                    sw_frame = hw_download;
                }
#else
                if (dec_frame->format == AV_PIX_FMT_CUDA
                    || dec_frame->format == AV_PIX_FMT_VAAPI) {
                    hw_download = av_frame_alloc();
                    if (av_hwframe_transfer_data(hw_download, dec_frame, 0) < 0) {
                        av_frame_free(&hw_download);
                        continue;
                    }
                    sw_frame = hw_download;
                }
#endif

                /* Lazy-init scaler on first frame (now we know actual pix_fmt).
                 * Compute letterbox/pillarbox to preserve source aspect ratio. */
                if (!sws_ctx) {
                    int src_w = sw_frame->width;
                    int src_h = sw_frame->height;
                    double src_ar = (double)src_w / src_h;
                    double dst_ar = (double)pp->width / pp->height;

                    if (src_ar > dst_ar) {
                        /* Source is wider — letterbox (black bars top/bottom) */
                        scaled_w = pp->width;
                        scaled_h = (int)(pp->width / src_ar);
                    } else {
                        /* Source is taller — pillarbox (black bars left/right) */
                        scaled_h = pp->height;
                        scaled_w = (int)(pp->height * src_ar);
                    }
                    /* Round to even (required by most codecs) */
                    scaled_w &= ~1;
                    scaled_h &= ~1;
                    if (scaled_w < 2) scaled_w = 2;
                    if (scaled_h < 2) scaled_h = 2;

                    /* A 1-2px aspect sliver is invisible as a stretch but
                     * fatal as a bar: the pad offset rounds to 0 and the
                     * scaler leaves the last row(s) UNWRITTEN — the
                     * multicolored garbage line at the bottom of every
                     * letterboxed video. Snap to full frame instead. */
                    if (pp->width - scaled_w <= 2) scaled_w = pp->width;
                    if (pp->height - scaled_h <= 2) scaled_h = pp->height;

                    pad_x = (pp->width - scaled_w) / 2;
                    pad_y = (pp->height - scaled_h) / 2;
                    /* Ensure even offsets for YUV chroma alignment */
                    pad_x &= ~1;
                    pad_y &= ~1;

                    sws_ctx = sws_getContext(
                        src_w, src_h, sw_frame->format,
                        scaled_w, scaled_h, venc_ctx->pix_fmt,
                        SWS_BILINEAR, NULL, NULL, NULL);
                    if (!sws_ctx) {
                        fprintf(stderr, LOG_PREFIX " video: sws_getContext failed (src_fmt=%d)\n",
                                sw_frame->format);
                        if (hw_download) av_frame_free(&hw_download);
                        goto vcleanup;
                    }

                    /* Allocate intermediate scaled frame (may be smaller than output).
                     * Keyed on DIMENSION MISMATCH, not pad offsets: an odd
                     * gap can produce mismatched dims with zero offset, and
                     * the direct-scale path would leave unwritten rows. */
                    if (scaled_w != pp->width || scaled_h != pp->height) {
                        pad_frame = av_frame_alloc();
                        pad_frame->format = venc_ctx->pix_fmt;
                        pad_frame->width = scaled_w;
                        pad_frame->height = scaled_h;
                        av_frame_get_buffer(pad_frame, 0);
                    }

                    fprintf(stderr, LOG_PREFIX " video: scaler OK (%dx%d fmt=%d -> %dx%d, pad to %dx%d offset +%d+%d)\n",
                            src_w, src_h, sw_frame->format,
                            scaled_w, scaled_h, pp->width, pp->height, pad_x, pad_y);
                }

                /* Scale + letterbox/pillarbox. Blit path whenever the
                 * scaled image is smaller than the frame in EITHER
                 * dimension — the black fill covers every row/column
                 * the scaler doesn't write. */
                av_frame_make_writable(sws_frame);

                if (pad_frame) {
                    /* Scale into intermediate frame, then blit centered into black output */
                    av_frame_make_writable(pad_frame);
                    sws_scale(sws_ctx,
                              (const uint8_t * const *)sw_frame->data, sw_frame->linesize,
                              0, sw_frame->height,
                              pad_frame->data, pad_frame->linesize);

                    /* Fill output frame with black (Y=0, U=V=128 for YUV420P / NV12) */
                    memset(sws_frame->data[0], 0, sws_frame->linesize[0] * pp->height);
                    if (venc_ctx->pix_fmt == AV_PIX_FMT_NV12) {
                        memset(sws_frame->data[1], 128, sws_frame->linesize[1] * (pp->height / 2));
                    } else {
                        /* YUV420P: separate U and V planes */
                        memset(sws_frame->data[1], 128, sws_frame->linesize[1] * (pp->height / 2));
                        memset(sws_frame->data[2], 128, sws_frame->linesize[2] * (pp->height / 2));
                    }

                    /* Copy scaled image centered into output frame */
                    for (int y = 0; y < scaled_h; y++) {
                        memcpy(sws_frame->data[0] + (y + pad_y) * sws_frame->linesize[0] + pad_x,
                               pad_frame->data[0] + y * pad_frame->linesize[0],
                               scaled_w);
                    }
                    if (venc_ctx->pix_fmt == AV_PIX_FMT_NV12) {
                        /* NV12: interleaved UV plane */
                        for (int y = 0; y < scaled_h / 2; y++) {
                            memcpy(sws_frame->data[1] + (y + pad_y / 2) * sws_frame->linesize[1] + pad_x,
                                   pad_frame->data[1] + y * pad_frame->linesize[1],
                                   scaled_w);
                        }
                    } else {
                        /* YUV420P: separate U and V planes */
                        for (int y = 0; y < scaled_h / 2; y++) {
                            memcpy(sws_frame->data[1] + (y + pad_y / 2) * sws_frame->linesize[1] + pad_x / 2,
                                   pad_frame->data[1] + y * pad_frame->linesize[1],
                                   scaled_w / 2);
                            memcpy(sws_frame->data[2] + (y + pad_y / 2) * sws_frame->linesize[2] + pad_x / 2,
                                   pad_frame->data[2] + y * pad_frame->linesize[2],
                                   scaled_w / 2);
                        }
                    }
                } else {
                    /* No padding needed — source AR matches target AR */
                    sws_scale(sws_ctx,
                              (const uint8_t * const *)sw_frame->data, sw_frame->linesize,
                              0, sw_frame->height,
                              sws_frame->data, sws_frame->linesize);
                }

                if (hw_download) av_frame_free(&hw_download);

                /* Pass through source PTS for correct A/V sync.
                 * Rescale from source stream timebase to encoder timebase. */
                int64_t orig_pts = dec_frame->pts;
                if (orig_pts != AV_NOPTS_VALUE) {
                    sws_frame->pts = av_rescale_q(orig_pts,
                                                    ifmt_ctx->streams[video_stream_idx]->time_base,
                                                    venc_ctx->time_base);
                } else {
                    sws_frame->pts = video_pts;
                }
                video_pts++;

                /* Encode */
                avcodec_send_frame(venc_ctx, sws_frame);
                while (avcodec_receive_packet(venc_ctx, out_pkt) >= 0) {
                    av_packet_rescale_ts(out_pkt, venc_ctx->time_base,
                                         ofmt_ctx->streams[out_video_idx]->time_base);
                    out_pkt->stream_index = out_video_idx;
                    av_interleaved_write_frame(ofmt_ctx, out_pkt);
                }

                /* Progress logging — every 1000 frames (was 100, too noisy) */
                if (video_pts % 1000 == 0 && video_pts > 0) {
                    double pts_sec = (dec_frame->pts != AV_NOPTS_VALUE)
                        ? dec_frame->pts * av_q2d(ifmt_ctx->streams[video_stream_idx]->time_base) : 0;
                    fprintf(stderr, LOG_PREFIX " video: %lld frames (%.1fs / %.1fs)\n",
                            (long long)video_pts, pts_sec,
                            total_duration > 0 ? (double)total_duration / AV_TIME_BASE : 0.0);
                }

                /* Progress callback */
                if (progress && total_duration > 0 && dec_frame->pts != AV_NOPTS_VALUE) {
                    double pts_sec = dec_frame->pts * av_q2d(ifmt_ctx->streams[video_stream_idx]->time_base);
                    float frac = (float)(pts_sec / ((double)total_duration / AV_TIME_BASE));
                    if (frac > 0.99f) frac = 0.99f;
                    if (frac > 0) progress(frac, userdata);
                }
            }
        } else if (pkt->stream_index == audio_stream_idx && adec_ctx && aenc_ctx && swr_ctx && audio_fifo) {
            /* Decode audio → resample → FIFO → encode in frame_size chunks */
            avcodec_send_packet(adec_ctx, pkt);
            while (avcodec_receive_frame(adec_ctx, dec_frame) >= 0) {
                /* Resample to encoder format */
                int out_samples = swr_get_out_samples(swr_ctx, dec_frame->nb_samples);
                uint8_t **resampled = NULL;
                int linesize;
                av_samples_alloc_array_and_samples(&resampled, &linesize,
                                                    aenc_ctx->ch_layout.nb_channels,
                                                    out_samples, aenc_ctx->sample_fmt, 0);
                int converted = swr_convert(swr_ctx, resampled, out_samples,
                                            (const uint8_t **)dec_frame->extended_data,
                                            dec_frame->nb_samples);
                if (converted > 0) {
                    av_audio_fifo_write(audio_fifo, (void **)resampled, converted);
                }
                if (resampled) av_freep(&resampled[0]);
                av_freep(&resampled);

                /* Drain FIFO in encoder frame_size chunks */
                while (av_audio_fifo_size(audio_fifo) >= aenc_ctx->frame_size) {
                    AVFrame *enc_frame = av_frame_alloc();
                    enc_frame->nb_samples = aenc_ctx->frame_size;
                    enc_frame->format = aenc_ctx->sample_fmt;
                    av_channel_layout_copy(&enc_frame->ch_layout, &aenc_ctx->ch_layout);
                    enc_frame->sample_rate = aenc_ctx->sample_rate;
                    av_frame_get_buffer(enc_frame, 0);

                    av_audio_fifo_read(audio_fifo, (void **)enc_frame->data, aenc_ctx->frame_size);
                    enc_frame->pts = audio_pts;
                    audio_pts += aenc_ctx->frame_size;

                    avcodec_send_frame(aenc_ctx, enc_frame);
                    while (avcodec_receive_packet(aenc_ctx, out_pkt) >= 0) {
                        av_packet_rescale_ts(out_pkt, aenc_ctx->time_base,
                                             ofmt_ctx->streams[out_audio_idx]->time_base);
                        out_pkt->stream_index = out_audio_idx;
                        av_interleaved_write_frame(ofmt_ctx, out_pkt);
                    }
                    av_frame_free(&enc_frame);
                }
            }
        }
        av_packet_unref(pkt);
    }

    fprintf(stderr, LOG_PREFIX " video: loop done — %lld video frames, %lld audio samples\n",
            (long long)video_pts, (long long)audio_pts);

    /* Flush video encoder */
    avcodec_send_frame(venc_ctx, NULL);
    while (avcodec_receive_packet(venc_ctx, out_pkt) >= 0) {
        av_packet_rescale_ts(out_pkt, venc_ctx->time_base,
                             ofmt_ctx->streams[out_video_idx]->time_base);
        out_pkt->stream_index = out_video_idx;
        av_interleaved_write_frame(ofmt_ctx, out_pkt);
    }

    /* Flush audio encoder */
    if (aenc_ctx) {
        avcodec_send_frame(aenc_ctx, NULL);
        while (avcodec_receive_packet(aenc_ctx, out_pkt) >= 0) {
            av_packet_rescale_ts(out_pkt, aenc_ctx->time_base,
                                 ofmt_ctx->streams[out_audio_idx]->time_base);
            out_pkt->stream_index = out_audio_idx;
            av_interleaved_write_frame(ofmt_ctx, out_pkt);
        }
    }

    av_write_trailer(ofmt_ctx);
    ret = 0;
    if (progress) progress(1.0f, userdata);
    fprintf(stderr, LOG_PREFIX " video transcode OK: %s\n", output_path);

vcleanup:
    if (out_pkt) av_packet_free(&out_pkt);
    if (pkt) av_packet_free(&pkt);
    if (swr_frame) av_frame_free(&swr_frame);
    if (sws_frame) av_frame_free(&sws_frame);
    if (pad_frame) av_frame_free(&pad_frame);
    if (dec_frame) av_frame_free(&dec_frame);
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (swr_ctx) swr_free(&swr_ctx);
    if (audio_fifo) av_audio_fifo_free(audio_fifo);
    if (aenc_ctx) avcodec_free_context(&aenc_ctx);
    if (venc_ctx) avcodec_free_context(&venc_ctx);
    if (hw_device_ref) av_buffer_unref(&hw_device_ref);
    if (hw_decode_ref) av_buffer_unref(&hw_decode_ref);
    if (adec_ctx) avcodec_free_context(&adec_ctx);
    if (vdec_ctx) avcodec_free_context(&vdec_ctx);
    if (ofmt_ctx) {
        if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&ofmt_ctx->pb);
        avformat_free_context(ofmt_ctx);
    }
    if (ifmt_ctx) avformat_close_input(&ifmt_ctx);

    if (ret != 0) {
        unlink(output_path);
        free(output_path);
        return NULL;
    }
    return output_path;
}

/* ==================================================================== */
/*  Probing                                                              */
/* ==================================================================== */

int zuuned_probe_duration(const char *filepath, double *duration_out)
{
    if (!filepath || !duration_out) return -1;

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, filepath, NULL, NULL) < 0) return -1;
    avformat_find_stream_info(fmt, NULL);

    if (fmt->duration > 0) {
        *duration_out = (double)fmt->duration / AV_TIME_BASE;
        avformat_close_input(&fmt);
        return 0;
    }

    avformat_close_input(&fmt);
    return -1;
}

int zuuned_probe_video_info(const char *filepath, double *duration_out,
                            int *width_out, int *height_out)
{
    if (!filepath || !duration_out || !width_out || !height_out) return -1;
    *duration_out = 0;
    *width_out = 0;
    *height_out = 0;

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, filepath, NULL, NULL) < 0) return -1;
    avformat_find_stream_info(fmt, NULL);

    if (fmt->duration > 0) {
        *duration_out = (double)fmt->duration / AV_TIME_BASE;
    }

    int vid_idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vid_idx >= 0) {
        *width_out = fmt->streams[vid_idx]->codecpar->width;
        *height_out = fmt->streams[vid_idx]->codecpar->height;
    }

    int ok = (*duration_out > 0 || *width_out > 0) ? 0 : -1;
    avformat_close_input(&fmt);
    return ok;
}

int zuuned_probe_audio_lang(const char *filepath, const char *lang, int *stream_idx_out)
{
    if (!filepath || !lang || !stream_idx_out) return -1;

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, filepath, NULL, NULL) < 0) return -1;
    avformat_find_stream_info(fmt, NULL);

    for (int i = 0; i < (int)fmt->nb_streams; i++) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            AVDictionaryEntry *tag = av_dict_get(fmt->streams[i]->metadata, "language", NULL, 0);
            if (tag && strcmp(tag->value, lang) == 0) {
                *stream_idx_out = i;
                avformat_close_input(&fmt);
                return 0;
            }
        }
    }

    avformat_close_input(&fmt);
    return -1;
}

int zuuned_probe_metadata(const char *filepath, struct ZuneMetadata *out)
{
    /* Delegate to zune_probe which already uses libav* when USE_LIBAV is set.
     * This function exists for callers that don't want to include zune.h.
     * Weak: lets this library link standalone (transcodetool) — resolves
     * to NULL unless libzune is linked in, in which case it delegates. */
    extern int zune_probe(const char *filepath, struct ZuneMetadata *out)
        __attribute__((weak));
    if (!zune_probe) return -1;
    return zune_probe(filepath, out);
}

/* ==================================================================== */
/*  Album Art Extraction                                                 */
/* ==================================================================== */

int zuuned_extract_art(const char *filepath, const char *output_jpeg_path,
                        int max_dimension)
{
    if (!filepath || !output_jpeg_path) return -1;
    if (max_dimension <= 0) max_dimension = 400;

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, filepath, NULL, NULL) < 0) return -1;
    avformat_find_stream_info(fmt, NULL);

    /* Find attached picture stream (album art) */
    int art_idx = -1;
    for (int i = 0; i < (int)fmt->nb_streams; i++) {
        if (fmt->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            art_idx = i;
            break;
        }
    }

    if (art_idx < 0) {
        avformat_close_input(&fmt);
        return -1;
    }

    AVPacket *art_pkt = &fmt->streams[art_idx]->attached_pic;
    if (!art_pkt->data || art_pkt->size <= 0) {
        avformat_close_input(&fmt);
        return -1;
    }

    /* Decode the image */
    const AVCodec *img_dec = avcodec_find_decoder(fmt->streams[art_idx]->codecpar->codec_id);
    if (!img_dec) { avformat_close_input(&fmt); return -1; }

    AVCodecContext *img_ctx = avcodec_alloc_context3(img_dec);
    avcodec_parameters_to_context(img_ctx, fmt->streams[art_idx]->codecpar);
    if (avcodec_open2(img_ctx, img_dec, NULL) < 0) {
        avcodec_free_context(&img_ctx);
        avformat_close_input(&fmt);
        return -1;
    }

    AVFrame *img_frame = av_frame_alloc();
    avcodec_send_packet(img_ctx, art_pkt);
    int got = avcodec_receive_frame(img_ctx, img_frame);
    if (got < 0) {
        av_frame_free(&img_frame);
        avcodec_free_context(&img_ctx);
        avformat_close_input(&fmt);
        return -1;
    }

    /* Scale to max_dimension */
    int src_w = img_frame->width, src_h = img_frame->height;
    int dst_w, dst_h;
    if (src_w >= src_h) {
        dst_w = (src_w > max_dimension) ? max_dimension : src_w;
        dst_h = dst_w * src_h / src_w;
    } else {
        dst_h = (src_h > max_dimension) ? max_dimension : src_h;
        dst_w = dst_h * src_w / src_h;
    }
    if (dst_w < 1) dst_w = 1;
    if (dst_h < 1) dst_h = 1;

    /* Scale */
    struct SwsContext *sws = sws_getContext(
        src_w, src_h, img_ctx->pix_fmt,
        dst_w, dst_h, AV_PIX_FMT_YUVJ420P,
        SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws) {
        av_frame_free(&img_frame);
        avcodec_free_context(&img_ctx);
        avformat_close_input(&fmt);
        return -1;
    }

    AVFrame *scaled = av_frame_alloc();
    scaled->format = AV_PIX_FMT_YUVJ420P;
    scaled->width = dst_w;
    scaled->height = dst_h;
    av_frame_get_buffer(scaled, 0);

    sws_scale(sws, (const uint8_t * const *)img_frame->data, img_frame->linesize,
              0, src_h, scaled->data, scaled->linesize);

    /* Encode as JPEG */
    const AVCodec *mjpeg_enc = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!mjpeg_enc) {
        sws_freeContext(sws);
        av_frame_free(&scaled);
        av_frame_free(&img_frame);
        avcodec_free_context(&img_ctx);
        avformat_close_input(&fmt);
        return -1;
    }

    AVCodecContext *jpg_ctx = avcodec_alloc_context3(mjpeg_enc);
    jpg_ctx->width = dst_w;
    jpg_ctx->height = dst_h;
    jpg_ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    jpg_ctx->time_base = (AVRational){1, 1};

    int result = -1;
    if (avcodec_open2(jpg_ctx, mjpeg_enc, NULL) >= 0) {
        scaled->pts = 0;
        avcodec_send_frame(jpg_ctx, scaled);
        avcodec_send_frame(jpg_ctx, NULL); /* flush */

        AVPacket *jpg_pkt = av_packet_alloc();
        if (avcodec_receive_packet(jpg_ctx, jpg_pkt) >= 0) {
            FILE *fp = fopen(output_jpeg_path, "wb");
            if (fp) {
                fwrite(jpg_pkt->data, 1, jpg_pkt->size, fp);
                fclose(fp);
                result = 0;
            }
        }
        av_packet_free(&jpg_pkt);
    }

    avcodec_free_context(&jpg_ctx);
    sws_freeContext(sws);
    av_frame_free(&scaled);
    av_frame_free(&img_frame);
    avcodec_free_context(&img_ctx);
    avformat_close_input(&fmt);

    return result;
}

int zuuned_extract_frame(const char *filepath, double at_seconds,
                         const char *output_jpeg_path, int max_width)
{
    if (!filepath || !output_jpeg_path) return -1;
    if (max_width <= 0) max_width = 400;
    if (at_seconds < 0) at_seconds = 0;

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, filepath, NULL, NULL) < 0) return -1;
    if (avformat_find_stream_info(fmt, NULL) < 0) {
        avformat_close_input(&fmt);
        return -1;
    }

    int vid_idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vid_idx < 0) {
        avformat_close_input(&fmt);
        return -1;
    }
    AVStream *vst = fmt->streams[vid_idx];

    /* Clamp the seek target inside the file, then seek to the nearest
     * preceding keyframe. On seek failure fall back to decoding from 0. */
    if (fmt->duration > 0) {
        double dur = (double)fmt->duration / AV_TIME_BASE;
        if (at_seconds > dur * 0.9) at_seconds = dur * 0.5;
    }
    int64_t seek_ts = (int64_t)(at_seconds * AV_TIME_BASE);
    if (av_seek_frame(fmt, -1, seek_ts, AVSEEK_FLAG_BACKWARD) < 0) {
        av_seek_frame(fmt, -1, 0, AVSEEK_FLAG_BACKWARD);
    }

    const AVCodec *dec = avcodec_find_decoder(vst->codecpar->codec_id);
    if (!dec) { avformat_close_input(&fmt); return -1; }

    AVCodecContext *dec_ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dec_ctx, vst->codecpar);
    if (avcodec_open2(dec_ctx, dec, NULL) < 0) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt);
        return -1;
    }

    /* Decode until the first complete frame after the seek point. Cap the
     * packet count so a corrupt file can't spin forever. */
    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    int got_frame = 0;
    int packets_read = 0;
    while (!got_frame && packets_read < 2048 && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vid_idx) {
            packets_read++;
            if (avcodec_send_packet(dec_ctx, pkt) >= 0 &&
                avcodec_receive_frame(dec_ctx, frame) >= 0) {
                got_frame = 1;
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);

    int result = -1;
    if (got_frame && frame->width > 0 && frame->height > 0) {
        int src_w = frame->width, src_h = frame->height;
        int dst_w = (src_w > max_width) ? max_width : src_w;
        int dst_h = dst_w * src_h / src_w;
        if (dst_w < 1) dst_w = 1;
        if (dst_h < 1) dst_h = 1;

        struct SwsContext *sws = sws_getContext(
            src_w, src_h, dec_ctx->pix_fmt,
            dst_w, dst_h, AV_PIX_FMT_YUVJ420P,
            SWS_BILINEAR, NULL, NULL, NULL);
        if (sws) {
            AVFrame *scaled = av_frame_alloc();
            scaled->format = AV_PIX_FMT_YUVJ420P;
            scaled->width = dst_w;
            scaled->height = dst_h;
            av_frame_get_buffer(scaled, 0);

            sws_scale(sws, (const uint8_t * const *)frame->data, frame->linesize,
                      0, src_h, scaled->data, scaled->linesize);

            const AVCodec *mjpeg_enc = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
            if (mjpeg_enc) {
                AVCodecContext *jpg_ctx = avcodec_alloc_context3(mjpeg_enc);
                jpg_ctx->width = dst_w;
                jpg_ctx->height = dst_h;
                jpg_ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
                jpg_ctx->time_base = (AVRational){1, 1};

                if (avcodec_open2(jpg_ctx, mjpeg_enc, NULL) >= 0) {
                    scaled->pts = 0;
                    avcodec_send_frame(jpg_ctx, scaled);
                    avcodec_send_frame(jpg_ctx, NULL); /* flush */

                    AVPacket *jpg_pkt = av_packet_alloc();
                    if (avcodec_receive_packet(jpg_ctx, jpg_pkt) >= 0) {
                        FILE *fp = fopen(output_jpeg_path, "wb");
                        if (fp) {
                            fwrite(jpg_pkt->data, 1, jpg_pkt->size, fp);
                            fclose(fp);
                            result = 0;
                        }
                    }
                    av_packet_free(&jpg_pkt);
                }
                avcodec_free_context(&jpg_ctx);
            }
            av_frame_free(&scaled);
            sws_freeContext(sws);
        }
    }

    av_frame_free(&frame);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt);

    return result;
}
