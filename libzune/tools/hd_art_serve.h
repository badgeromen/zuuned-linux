/* Native, bounded local JPEG serving experiment. No host networking. */
#ifndef ZUNETOOL_HD_ART_SERVE_H
#define ZUNETOOL_HD_ART_SERVE_H
#include "hd_network_probe.h"
#include "hd_image_server.h"
#include "hd_art_gallery.h"
#include "hd_art_bind_ready.h"
#include "hd_art_first_sync.h"
#include "hd_art_album_sync.h"
typedef struct {
    HdImageServer server;
    HdNetworkState *network;
    HdArtReadyBinding *binding;
    HdFirstSync *first_sync;
    HdAlbumSync *album_sync;
    unsigned requests, dns;
    int multiimage, biography_required;
    int64_t observation_end, last_activity;
    unsigned acknowledged;
} HdArtServing;
static int hd_art_emit(void *context,const uint8_t *p,size_t n) {
    HdArtServing *art=context;
    return art->network ? hd_network_emit(art->network,0x21,p,n) : -1;
}
static void hd_art_log(HdArtServing *art,FILE *out) {
    if(art->dns!=art->server.dns_queries) {
        fprintf(out,"dns-query\t%s\n",art->server.last_dns);art->dns=art->server.dns_queries;
    }
    if(art->requests!=art->server.requests) {
        /* Render controls as '?' rather than terminal escape sequences. */
        fputs("http-request\t",out);
        for(const unsigned char *p=(const unsigned char*)art->server.last_request;*p;++p)
            fputc(*p>=32 && *p<127 ? *p : '?',out);
        fputc('\n',out);art->requests=art->server.requests;
    }
    fflush(out);
}
/* A gallery observation has its own cutoff before the transport deadline.
 * All counts are transport evidence only. Missing gallery requests are reported
 * explicitly; they never become fabricated completions. */
static int hd_art_observation_complete(HdArtServing *art,int64_t now) {
    if((art->first_sync&&art->first_sync->phase<6)||
       (art->album_sync&&!art->album_sync->graph_ready))return 0;
    if(art->biography_required ? !art->server.biography_completed : !art->server.background_completed)return 0;
    if((art->first_sync||art->album_sync)&&!art->server.background_completed)return 0;
    if(!art->multiimage&&!art->biography_required)return 1;
    if(now<0)return 0;
    if(art->server.image_completed+art->server.biography_completed!=art->acknowledged){
        art->acknowledged=art->server.image_completed+art->server.biography_completed;art->last_activity=now;
    }
    if(now>=art->observation_end)return 1;
    /* A repeated HTTP request may still be sending after each distinct asset
     * completed once. Quiet early exit must never truncate that active flow. */
    for(size_t i=0;i<HDIS_FLOWS;i++)if(art->server.flow[i].active)return 0;
    /* Existing-artist biography refresh can rely on cached gallery images.
     * Fresh creation must allow delayed gallery requests to finish as well. */
    int gallery_required=art->multiimage &&
        (!art->biography_required || art->first_sync || art->album_sync);
    return (!gallery_required || (art->server.separate_roles ?
            art->server.distinct_gallery_completed==art->server.gallery_count :
            art->server.distinct_assets_completed==art->server.asset_count)) &&
           now-art->last_activity>=3000;
}
/* Full payload verification follows clean network teardown and has an
 * independent bounded budget. Never revive canceled or uncertain work. */
static int hd_art_album_proof_budget(HdAlbumSync *album,int clean,int uncertain,int network_result,int64_t now) {
    if(!album||!clean||uncertain||network_result||now<0||now>INT64_MAX-120000||
       !album->dev||ZUNE_FLAG_GET(album->dev->cancel_requested)||ZUNE_FLAG_GET(album->dev->disconnecting)||
       (album->stop&&*album->stop))return 0;
    album->deadline=now+120000;return 1;
}
static int hd_art_packet(HdNetworkState *network,void *context,const uint8_t *p,size_t n) {
    HdArtServing *art=context;art->network=network;
    if(hd_image_server_packet(&art->server,p,n)) return -1;
    if(art->requests!=art->server.requests)art->last_activity=HD_TUNNEL_NOW_MS();
    hd_art_log(art,network->out);
    return hd_art_observation_complete(art,HD_TUNNEL_NOW_MS());
}
static int hd_art_tick(HdNetworkState *network,void *context,int64_t now) {
    HdArtServing *art=context;art->network=network;
    if(now<0)return -1;
    if(art->binding){
        int ready=network->local_ipcp&&network->peer_ipcp&&!network->frame_length&&!network->frame_escaped;
        int result=hd_art_ready_tick(art->binding,ready);
        if(art->binding->uncertain)network->callback_transport_uncertain=1;
        if(result)return -1;
    }
    if(art->first_sync){
        int ready=network->local_ipcp&&network->peer_ipcp&&!network->frame_length&&!network->frame_escaped;
        int result=hd_first_tick(art->first_sync,ready);
        if(art->first_sync->uncertain)network->callback_transport_uncertain=1;
        if(result)return -1;
    }
    if(art->album_sync){
        int ready=network->local_ipcp&&network->peer_ipcp&&!network->frame_length&&!network->frame_escaped;
        int result=hd_album_tick(art->album_sync,ready);
        if(art->album_sync->uncertain)network->callback_transport_uncertain=1;
        if(result)return -1;
    }
    if(hd_image_server_tick(&art->server,(uint64_t)now)) return -1;
    return hd_art_observation_complete(art,now);
}
static uint8_t *hd_art_load_jpeg(const char *path,size_t *size) {
    FILE *f=fopen(path,"rb"); if(!f)return NULL;
    uint8_t *bytes=NULL;long n;
    if(fseek(f,0,SEEK_END) || (n=ftell(f))<4 || n>128*1024 || fseek(f,0,SEEK_SET)) goto done;
    bytes=malloc((size_t)n);if(!bytes)goto done;
    if(fread(bytes,1,(size_t)n,f)!=(size_t)n || fgetc(f)!=EOF || ferror(f) ||
       bytes[0]!=255 || bytes[1]!=216 || bytes[n-2]!=255 || bytes[n-1]!=217) {
        free(bytes);bytes=NULL;goto done;
    }
    *size=(size_t)n;
 done:
    fclose(f);return bytes;
}
/* Read the complete bounded UTF-8 source before connection. Server validation
 * rejects embedded NULs/invalid XML characters; no runtime file lookups occur. */
static int hd_art_load_biography(const char *path,char text[HDIS_BIO_MAX+1],size_t *size) {
    FILE *f=fopen(path,"rb");if(!f)return -1;
    size_t n=fread(text,1,HDIS_BIO_MAX+1,f);
    int failed=ferror(f)||!n||n>HDIS_BIO_MAX;
    if(!failed && fgetc(f)!=EOF)failed=1;
    if(ferror(f))failed=1;
    fclose(f);
    if(!failed) {
        size_t i=0;while(i<n && (text[i]==' '||text[i]=='\t'||text[i]=='\r'||text[i]=='\n'))i++;
        if(i==n)failed=1;
    }
    if(failed)return -1;
    text[n]=0;*size=n;return 0;
}
static int hd_art_serve_experiment_content(ZuneDevice *dev,const char *guid,const char *artist,const uint8_t *jpeg,size_t size,
                        unsigned duration_ms,volatile sig_atomic_t *stop,FILE *out,int *transport_uncertain,HdArtReadyBinding *binding,HdFirstSync *first_sync,const HdArtGallery *gallery,HdAlbumSync *album_sync,const char *biography,size_t biography_n) {
    if(transport_uncertain)*transport_uncertain=0;
    HdArtServing art={0};art.binding=binding;art.first_sync=first_sync;art.album_sync=album_sync;
    if(hd_image_server_init(&art.server,0xc0a83764,guid,jpeg,size,hd_art_emit,&art))return -1;
    if(gallery && hd_art_gallery_configure(&art.server,gallery))return -1;
    if(hd_image_server_set_artist(&art.server,artist))return -1;
    if(biography && hd_image_server_set_biography(&art.server,biography,biography_n))return -1;
    art.biography_required=biography!=NULL;
    if(gallery)fprintf(out,"gallery-config\tassets=%zu\tbackground=%s\tprimary=%s\tobservation-ms=%u\tcleanup-reserve-ms=%u\n",
        art.server.asset_count,art.server.assets[art.server.background_index].guid,
        art.server.assets[art.server.primary_index].guid,duration_ms>=3000?duration_ms-1000:duration_ms-500,duration_ms>=3000?1000:500);
    int64_t now=HD_TUNNEL_NOW_MS();if(now<0)return -1;
    art.server.now=(uint64_t)now;art.multiimage=gallery!=NULL;
    art.observation_end=now+duration_ms-(duration_ms>=3000?1000:500);art.last_activity=now;
    if(binding)binding->deadline=now+duration_ms;
    if(first_sync)first_sync->deadline=now+duration_ms;
    if(album_sync)album_sync->deadline=now+duration_ms;
    int uncertain=0,network_clean=0;
    int result=hd_network_probe_run_ex_lifecycle(dev,out,stop,hd_art_packet,hd_art_tick,&art,1,duration_ms,&uncertain,&network_clean);
    if(binding){
        if(hd_art_ready_finish(binding,network_clean,uncertain))result=-1;
        if(binding->uncertain)uncertain=1;
    }
    if(first_sync){
        if(hd_first_complete(first_sync,network_clean,uncertain,art.server.background_completed,!result))result=-1;
        if(first_sync->uncertain)uncertain=1;
    }
    if(album_sync){
        if(hd_art_album_proof_budget(album_sync,network_clean,uncertain,result,HD_TUNNEL_NOW_MS()))
            fprintf(out,"album-final-proof-budget\tmilliseconds=120000; network closed cleanly\n");
        if(hd_album_complete(album_sync,network_clean,uncertain,art.server.background_completed,!result))result=-1;
        if(album_sync->uncertain)uncertain=1;
    }
    if(transport_uncertain)*transport_uncertain=uncertain;
    fprintf(out,"artwork-result\timages-acknowledged=%u\thttp-requests=%u\tdns-queries=%u\n",
            art.server.image_completed,art.server.requests,art.server.dns_queries);
    fprintf(out,"background-result\tacknowledged=%u\n",art.server.background_completed);
    fprintf(out,"biography-result\tconfigured=%s\trequests=%u\tacknowledged=%u\tdevice-display=unverified\n",
        biography?"yes":"no",art.server.biography_requests,art.server.biography_completed);
    if(gallery){
        fprintf(out,"asset-summary\tassets=%zu\tdistinct-ids-acknowledged=%u\tdistinct-contents-acknowledged=%u\tall-assets-acknowledged=%s\tdevice-display=unverified\n",
            art.server.asset_count,art.server.distinct_assets_completed,art.server.distinct_contents_completed,
            art.server.distinct_assets_completed==art.server.asset_count?"yes":"no");
        fprintf(out,"gallery-result\tmembers=%zu\tdistinct-members-acknowledged=%u\tmember-transfers-acknowledged=%u\tall-members-acknowledged=%s\troles=%s\n",
            art.server.gallery_count,art.server.distinct_gallery_completed,art.server.gallery_completed,art.server.distinct_gallery_completed==art.server.gallery_count?"yes":"no",art.server.separate_roles?"separate":"legacy");
        for(size_t i=0;i<art.server.asset_count;i++)
            fprintf(out,"asset-result\tid=%s\trequests=%u\ttransfers-acknowledged=%u\n",art.server.assets[i].guid,art.server.asset_requests[i],art.server.asset_completed[i]);
    }
    fprintf(out,"server-diagnostics\trejected=%u\ttimed-out=%u\tflow-capacity=%u\tpeak-flows=%u\tcapacity-refused=%u\n",art.server.rejected,art.server.timed_out,HDIS_FLOWS,art.server.peak_flows,art.server.capacity_refused);
    if(art.server.fwlink_diagnostic_responses)
        fprintf(out,"fwlink-diagnostic\tresponses=%u; HTML approximation only, secure clock unsupported\n",
                art.server.fwlink_diagnostic_responses);
    fflush(out);
    return result || (biography ? !art.server.biography_completed : !art.server.background_completed) ||
        (first_sync && first_sync->phase<6) || (album_sync&&!album_sync->graph_ready) ? -1 : 0;
}
/* Preserve the image-only research API and its background completion contract. */
static inline int hd_art_serve_experiment_work(ZuneDevice *dev,const char *guid,const char *artist,const uint8_t *jpeg,size_t size,
                        unsigned duration_ms,volatile sig_atomic_t *stop,FILE *out,int *transport_uncertain,HdArtReadyBinding *binding,HdFirstSync *first_sync,const HdArtGallery *gallery,HdAlbumSync *album_sync) {
    return hd_art_serve_experiment_content(dev,guid,artist,jpeg,size,duration_ms,stop,out,transport_uncertain,binding,first_sync,gallery,album_sync,NULL,0);
}
static inline int hd_art_serve_experiment_assets(ZuneDevice *dev,const char *guid,const char *artist,const uint8_t *jpeg,size_t size,
                        unsigned duration_ms,volatile sig_atomic_t *stop,FILE *out,int *transport_uncertain,HdArtReadyBinding *binding,HdFirstSync *first_sync,const HdArtGallery *gallery) {
    return hd_art_serve_experiment_work(dev,guid,artist,jpeg,size,duration_ms,stop,out,transport_uncertain,binding,first_sync,gallery,NULL);
}
static inline int hd_art_serve_experiment(ZuneDevice *dev,const char *guid,const char *artist,const uint8_t *jpeg,size_t size,
                        unsigned duration_ms,volatile sig_atomic_t *stop,FILE *out,int *transport_uncertain,HdArtReadyBinding *binding,HdFirstSync *first_sync) {
    return hd_art_serve_experiment_assets(dev,guid,artist,jpeg,size,duration_ms,stop,out,transport_uncertain,binding,first_sync,NULL);
}
static inline int hd_art_serve_bound(ZuneDevice *dev,const char *guid,const char *artist,const uint8_t *jpeg,size_t size,
                        unsigned duration_ms,volatile sig_atomic_t *stop,FILE *out,int *transport_uncertain,HdArtReadyBinding *binding) {
    return hd_art_serve_experiment(dev,guid,artist,jpeg,size,duration_ms,stop,out,transport_uncertain,binding,NULL);
}
static inline int hd_art_serve_ex(ZuneDevice *dev,const char *guid,const char *artist,const uint8_t *jpeg,size_t size,
                        unsigned duration_ms,volatile sig_atomic_t *stop,FILE *out,int *transport_uncertain) {
    return hd_art_serve_bound(dev,guid,artist,jpeg,size,duration_ms,stop,out,transport_uncertain,NULL);
}
static inline int hd_art_serve(ZuneDevice *dev,const char *guid,const char *artist,const uint8_t *jpeg,size_t size,
                        unsigned duration_ms,volatile sig_atomic_t *stop,FILE *out) {
    return hd_art_serve_ex(dev,guid,artist,jpeg,size,duration_ms,stop,out,NULL);
}
#endif
