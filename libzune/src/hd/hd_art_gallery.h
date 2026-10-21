#ifndef ZUNETOOL_HD_ART_GALLERY_H
#define ZUNETOOL_HD_ART_GALLERY_H
/* Shared immutable artwork validation and research file-loading helpers.
 * Full JPEG decoding precedes USB callbacks. Caller owns all image buffers. */
#include "hd_image_server.h"
#include <stdlib.h>
#include <strings.h>
#ifdef USE_LIBAV
#include <libavcodec/avcodec.h>
#endif
typedef struct {
    HdImageAsset asset[HDIS_ASSETS];
    uint8_t *owned[HDIS_ASSETS];
    size_t count, primary;
    size_t gallery_count, gallery_indices[HDIS_GALLERY_MAX];
    int separate_roles;
} HdArtGallery;
static void hd_art_gallery_release(HdArtGallery *g) {
    if(!g)return;
    for(size_t i=0;i<HDIS_ASSETS;i++)free(g->owned[i]);
    memset(g,0,sizeof(*g));
}
static int hd_art_jpeg_decode(const uint8_t *data,size_t size) {
#ifdef USE_LIBAV
    unsigned width=0,height=0;
    if(!data||size<4||size>128*1024)return -1;
    hdis_jpeg_dimensions(data,size,&width,&height);
    if(!width||!height||width>4096||height>4096||(uint64_t)width*height>4194304)return -1;
    const AVCodec *codec=avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    AVCodecContext *ctx=codec?avcodec_alloc_context3(codec):NULL;
    AVPacket *packet=av_packet_alloc();AVFrame *frame=av_frame_alloc();int result=-1;
    if(!ctx||!packet||!frame)goto done;
    ctx->max_pixels=4194304;ctx->thread_count=1;
    ctx->err_recognition=AV_EF_CRCCHECK|AV_EF_BITSTREAM|AV_EF_BUFFER|AV_EF_EXPLODE;
    if(avcodec_open2(ctx,codec,NULL)<0||av_new_packet(packet,(int)size)<0)goto done;
    memcpy(packet->data,data,size);
    if(avcodec_send_packet(ctx,packet)<0||avcodec_receive_frame(ctx,frame)<0||frame->decode_error_flags||
       frame->width!=(int)width||frame->height!=(int)height)goto done;
    result=0;
 done:
    av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&ctx);return result;
#else
    (void)data;(void)size;return -1;
#endif
}
/* Background bytes and ID come from the already validated legacy initializer.
 * Gallery paths and primary are untrusted CLI input. Any failure releases only
 * our additional buffers; the caller always owns the background. */
static int hd_art_gallery_load(HdArtGallery *g,const HdImageServer *background,
                               const char *const *ids,const char *const *paths,
                               size_t extras,const char *primary,
                               uint8_t *(*load)(const char *,size_t *)) {
    if(!g||!background||!load||!extras||extras>=HDIS_LEGACY_ASSETS||!ids||!paths)return -1;
    memset(g,0,sizeof(*g));g->count=extras+1;
    memcpy(g->asset[0].guid,background->image_guid,37);
    g->asset[0].jpeg=background->jpeg;g->asset[0].jpeg_n=background->jpeg_n;
    if(hd_art_jpeg_decode(background->jpeg,background->jpeg_n))goto fail;
    for(size_t i=0;i<extras;i++){
        if(!ids[i]||strlen(ids[i])!=36||!paths[i]||!*paths[i])goto fail;
        memcpy(g->asset[i+1].guid,ids[i],37);
        g->owned[i+1]=load(paths[i],&g->asset[i+1].jpeg_n);
        g->asset[i+1].jpeg=g->owned[i+1];
        if(!g->owned[i+1]||hd_art_jpeg_decode(g->asset[i+1].jpeg,g->asset[i+1].jpeg_n))goto fail;
    }
    if(primary){
        size_t i;for(i=0;i<g->count;i++)if(!strcasecmp(primary,g->asset[i].guid))break;
        if(i==g->count)goto fail;
        g->primary=i;
    }
    return 0;
 fail:
    hd_art_gallery_release(g);return -1;
}
/* Explicit roles: positional background, 1..6 ordered collection members and
 * one separate primary thumbnail. Full JPEG decoding precedes any device open. */
static int hd_art_gallery_load_roles(HdArtGallery *g,const HdImageServer *background,
        const char *const *ids,const char *const *paths,size_t count,
        const char *thumbnail_id,const char *thumbnail_path,
        uint8_t *(*load)(const char *,size_t *)) {
    if(!g||!background||!load||!ids||!paths||!count||count>HDIS_GALLERY_MAX||
       !thumbnail_id||strlen(thumbnail_id)!=36||!thumbnail_path||!*thumbnail_path)return -1;
    memset(g,0,sizeof(*g));g->count=count+2;g->primary=count+1;
    g->separate_roles=1;g->gallery_count=count;
    memcpy(g->asset[0].guid,background->assets[background->background_index].guid,37);
    g->asset[0].jpeg=background->jpeg;g->asset[0].jpeg_n=background->jpeg_n;
    if(hd_art_jpeg_decode(background->jpeg,background->jpeg_n))goto fail;
    for(size_t i=1;i<g->count;i++){
        const char *id=i==g->primary?thumbnail_id:ids[i-1];
        const char *path=i==g->primary?thumbnail_path:paths[i-1];
        if(!id||strlen(id)!=36||!path||!*path)goto fail;
        memcpy(g->asset[i].guid,id,37);
        g->owned[i]=load(path,&g->asset[i].jpeg_n);g->asset[i].jpeg=g->owned[i];
        if(!g->owned[i]||hd_art_jpeg_decode(g->asset[i].jpeg,g->asset[i].jpeg_n))goto fail;
        if(i!=g->primary)g->gallery_indices[i-1]=i;
    }
    return 0;
 fail:
    hd_art_gallery_release(g);return -1;
}
static int hd_art_gallery_configure(HdImageServer *s,const HdArtGallery *g) {
    if(!g)return -1;
    return g->separate_roles?
        hd_image_server_set_asset_roles(s,g->asset,g->count,0,g->primary,g->gallery_indices,g->gallery_count):
        hd_image_server_set_assets(s,g->asset,g->count,0,g->primary);
}

#endif
