#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
static int64_t fake_time;
static int64_t test_now(void) { return fake_time; }
static void test_pause(void) { fake_time += 250; }
#define HD_TUNNEL_NOW_MS test_now
#define HD_TUNNEL_PAUSE test_pause
#include "../src/hd_artwork.c"

static unsigned checks, writes, calls, ready_calls;
static int inventory_fail, fail_commit, stale_readback, cancel_in_ready;
static int create_params=3, network_mode, polls, ends, closes, refs_fail;
static uint32_t existing_id;
static const char *existing_name;
static uint8_t existing_guid[16];
static const char *name = "Disturbed";
static const char *guid = "7bcdd03e-5c07-41f6-be29-8f6c2ee49294";
static char error_text[512];
void zune_set_error(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(error_text,sizeof(error_text),fmt,ap); va_end(ap); }
void zune_clear_error(void) { error_text[0] = 0; }
int mtp_get_object_handles(ptp_session_t *s,uint32_t storage,uint32_t format,uint32_t parent,uint32_t **ids,int *count) {
    (void)s;(void)storage;(void)format;(void)parent; calls++;
    if(format==MTP_OFC_Association){*count=1;*ids=malloc(4);**ids=99;return 0;}
    *count=existing_id?1:0; *ids=existing_id?malloc(4):NULL; if(*ids)**ids=existing_id;
    return inventory_fail?-1:0;
}
int mtp_get_object_prop_value(ptp_session_t *s,uint32_t id,uint16_t prop,uint8_t **data,uint32_t *length) {
    (void)s;(void)id; calls++;
    if(id==99) {
        if(prop==MTP_OPC_ObjectFileName){uint8_t wire[511];size_t n=0;hd_name_wire("Artists",wire,&n);*data=malloc(n);memcpy(*data,wire,n);*length=(uint32_t)n;return 0;}
        *length=prop==MTP_OPC_ObjectFormat?2:4;*data=calloc(1,*length);
        if(prop==MTP_OPC_StorageID)(*data)[0]=123;
        if(prop==MTP_OPC_ObjectFormat){(*data)[0]=1;(*data)[1]=0x30;}return 0;
    }
    if(prop==MTP_OPC_Name || prop==MTP_OPC_Artist){uint8_t wire[511];size_t n=0;hd_name_wire(stale_readback?"other":existing_name,wire,&n);*data=malloc(n);memcpy(*data,wire,n);*length=(uint32_t)n;return 0;}
    if(prop==0xda97){*length=16;*data=malloc(16);memcpy(*data,existing_guid,16);return 0;}
    return -1;
}
int mtp_get_object_references(ptp_session_t *s,uint32_t id,uint32_t **refs,int *count) {
    (void)s;(void)id;*refs=malloc(4);**refs=123;*count=1;return refs_fail?-1:0;
}
void zune_free_albums(ZuneAlbumObject *albums,int n) {
    if(!albums)return;
    for(int i=0;i<n;i++){free(albums[i].name);free(albums[i].artist);free(albums[i].track_ids);}free(albums);
}
int mtp_send_object(ptp_session_t *s,const uint8_t *bytes,uint32_t size) {
    (void)bytes;(void)size;writes++;calls++;s->last_response=fail_commit?PTP_ERROR_IO:PTP_RC_OK;return fail_commit?-1:0;
}
int ptp_transaction(ptp_session_t *s,uint16_t op,uint32_t *params,int count,uint16_t flags,
    uint8_t *send,uint64_t send_len,uint8_t **data,uint32_t *data_len,
    uint16_t *response,uint32_t *reply,int *reply_count) {
    (void)count;(void)flags;(void)data;(void)data_len;calls++;*response=PTP_RC_OK;s->last_response=PTP_RC_OK;
    if(op==MTP_OC_SendObjectPropList){
        checks++;assert(send_len>100&&send[0]==4);writes++;
        assert(params[2]==MTP_OFC_AbstractArtist&&!params[3]&&!params[4]);
        reply[0]=params[0];reply[1]=params[1];reply[2]=777;*reply_count=create_params;
        existing_id=777;existing_name=name;hd_guid(guid,existing_guid);return 0;
    }
    if(network_mode) {
        *reply_count=0;if(data)*data=NULL;if(data_len)*data_len=0;
        if(op==0x9231)return 0;
        if(op==0x9217){*data=malloc(4);memcpy(*data,"ZMDB",4);*data_len=4;return 0;}
        if(op==0x9218||op==0x9219){*reply_count=3;memset(reply,0,12);return 0;}
        if(op==0x922f)return 0;
        if(op==0x922b){*reply_count=3;reply[0]=reply[1]=0;reply[2]=2;if(params[1]==2)closes++;return 0;}
        if(op==0x9230){if(params[0]==2)ends++;return 0;}
        if(op==0x922c){*reply_count=4;reply[0]=reply[2]=0;reply[1]=3;reply[3]=(uint32_t)send_len;return 0;}
        if(op==0x922d){
            *reply_count=3;reply[0]=reply[2]=0;reply[1]=3;polls++;
            uint8_t wire[2048],packet[32];size_t n=0;
            if(polls==1){memcpy(wire,"CLIENT",6);n=6;}
            else if(polls==2)n=hd_network_encode(0xc021,hd_network_lcp_request,14,wire,sizeof(wire));
            else if(polls==3){memcpy(packet,hd_network_lcp_request,14);packet[0]=2;n=hd_network_encode(0xc021,packet,14,wire,sizeof(wire));}
            else if(polls==4){const uint8_t peer[]={1,3,0,16,3,6,192,168,55,101,129,6,192,168,55,100};n=hd_network_encode(0x8021,peer,sizeof(peer),wire,sizeof(wire));}
            else if(polls==5){memcpy(packet,hd_network_ipcp_request,10);packet[0]=2;n=hd_network_encode(0x8021,packet,10,wire,sizeof(wire));}
            if(n){*data=malloc(n);memcpy(*data,wire,n);*data_len=(uint32_t)n;}return 0;
        }
    }
    /* No unplanned USB in software rejection cases. */
    assert(!"unexpected PTP operation");return -1;
}
static void check(int ok){checks++;assert(ok);}
static ZuneDevice device(void){ZuneDevice d={0};d.device_family=ZUNE_FAMILY_PAVO;d.mtpz_authenticated=1;d.storage_id=123;d.serial="serial";return d;}
static void reset(void){existing_id=0;existing_name=name;memset(existing_guid,0,16);calls=writes=0;inventory_fail=fail_commit=stale_readback=refs_fail=0;create_params=3;network_mode=polls=ends=closes=0;fake_time=0;ready_calls=0;cancel_in_ready=0;}
static int ready(ZuneDevice *dev,void *context){(void)context;ready_calls++;if(cancel_in_ready)dev->cancel_requested=1;return 0;}
static int uncertain_ready(ZuneDevice *dev,void *context){(void)dev;(void)context;ready_calls++;return -2;}
static int emit_ok(void *context,const uint8_t *data,size_t size){(void)context;(void)data;(void)size;return 0;}
int main(int argc,char **argv){
    check(argc==2);FILE*f=fopen(argv[1],"rb");check(f!=NULL);uint8_t jpeg[131072];size_t jpeg_size=fread(jpeg,1,sizeof(jpeg),f);fclose(f);
    ZuneHDArtConfig c={0};c.version=1;c.artist_name=name;c.catalog_guid=guid;c.duration_ms=5000;
    c.background=(ZuneHDArtImage){"7bcdd03e-5c07-41f6-be29-8f6c2ee49295",jpeg,jpeg_size};
    c.thumbnail=(ZuneHDArtImage){"6dc1cf5d-bce3-4c60-92d6-e71e4521e01b",jpeg,jpeg_size};
    check(zune_hd_art_validate(&c)==ZUNE_HD_ART_OK);check(calls==0);
    ZuneHDArtConfig bad=c;bad.version=2;check(zune_hd_art_validate(&bad)==ZUNE_HD_ART_INVALID);
    bad=c;bad.gallery_count=7;check(zune_hd_art_validate(&bad)==ZUNE_HD_ART_INVALID);
    bad=c;bad.duration_ms=360001;check(zune_hd_art_validate(&bad)==ZUNE_HD_ART_INVALID);
    bad=c;bad.catalog_guid="00000000-0000-0000-0000-000000000000";check(zune_hd_art_validate(&bad)==ZUNE_HD_ART_INVALID);
    bad=c;bad.thumbnail.guid=bad.background.guid;check(zune_hd_art_validate(&bad)==ZUNE_HD_ART_INVALID);
    bad=c;bad.background.guid=c.thumbnail.guid;check(zune_hd_art_validate(&bad)==ZUNE_HD_ART_INVALID);
    bad=c;bad.background.size=131073;check(zune_hd_art_validate(&bad)==ZUNE_HD_ART_INVALID);
    bad=c;bad.biography="\xc0\x80";bad.biography_size=2;check(zune_hd_art_validate(&bad)==ZUNE_HD_ART_INVALID);
    bad=c;bad.biography=" \n\t";bad.biography_size=3;check(zune_hd_art_validate(&bad)==ZUNE_HD_ART_INVALID);
    bad=c;bad.biography="bad\0bio";bad.biography_size=7;check(zune_hd_art_validate(&bad)==ZUNE_HD_ART_INVALID);
    bad=c;bad.biography="<script>Not markup & UTF-8 é</script>";bad.biography_size=strlen(bad.biography);check(zune_hd_art_validate(&bad)==ZUNE_HD_ART_OK);
    uint8_t malformed[]={255,216,0,0,255,217};bad=c;bad.background.jpeg=malformed;bad.background.size=sizeof(malformed);check(zune_hd_art_validate(&bad)==ZUNE_HD_ART_INVALID);
    ZuneHDArtResult report;ZuneDevice d=device();d.device_family=ZUNE_FAMILY_KEEL;
    check(zune_hd_art_serve(&d,&c,ready,NULL,NULL,NULL,&report)==ZUNE_HD_ART_UNSUPPORTED);check(calls==0&&report.safe_to_continue);
    ZuneHDArtistResult artist;check(zune_hd_art_ensure_artist(&d,name,guid,&artist)==ZUNE_HD_ART_UNSUPPORTED);check(calls==0);
    d=device();d.cancel_requested=1;check(zune_hd_art_serve(&d,&c,ready,NULL,NULL,NULL,&report)==ZUNE_HD_ART_CANCELLED);check(calls==0);
    d=device();check(zune_hd_art_serve(&d,&bad,ready,NULL,NULL,NULL,&report)==ZUNE_HD_ART_INVALID);check(calls==0);
    d=device();d.hd_art_active=1;check(zune_hd_art_serve(&d,&c,ready,NULL,NULL,NULL,&report)==ZUNE_HD_ART_INVALID);check(!report.safe_to_continue&&calls==0);
    reset();d=device();inventory_fail=1;check(zune_hd_art_ensure_artist(&d,name,guid,&artist)==ZUNE_HD_ART_IO_ERROR);check(!writes&&!artist.write_attempted);
    reset();d=device();existing_id=100;check(zune_hd_art_ensure_artist(&d,name,guid,&artist)==ZUNE_HD_ART_IDENTITY_CONFLICT);check(!writes&&!artist.write_attempted);
    reset();d=device();existing_id=100;existing_name=" DISTURBED ";hd_guid(guid,existing_guid);check(zune_hd_art_ensure_artist(&d,name,guid,&artist)==ZUNE_HD_ART_OK);check(artist.handle==100&&!artist.created&&!writes);
    reset();d=device();existing_id=100;existing_name="Other Artist";hd_guid(guid,existing_guid);check(zune_hd_art_ensure_artist(&d,name,guid,&artist)==ZUNE_HD_ART_IDENTITY_CONFLICT);check(!writes);
    reset();d=device();check(zune_hd_art_ensure_artist(&d,name,guid,&artist)==ZUNE_HD_ART_OK);check(artist.handle==777&&artist.created&&artist.write_attempted&&writes==2);
    unsigned previous=writes;check(zune_hd_art_ensure_artist(&d,name,guid,&artist)==ZUNE_HD_ART_OK);check(writes==previous&&!artist.created);
    reset();d=device();fail_commit=1;check(zune_hd_art_ensure_artist(&d,name,guid,&artist)==ZUNE_HD_ART_IO_ERROR);check(artist.created&&artist.handle==777&&artist.transport_uncertain&&d.hd_transport_uncertain);
    previous=calls;check(zune_hd_art_ensure_artist(&d,name,guid,&artist)==ZUNE_HD_ART_INVALID);check(calls==previous);
    reset();d=device();stale_readback=1;check(zune_hd_art_ensure_artist(&d,name,guid,&artist)==ZUNE_HD_ART_IO_ERROR);check(artist.created&&artist.write_attempted&&artist.handle==777&&writes==2);
    reset();d=device();create_params=4;check(zune_hd_art_ensure_artist(&d,name,guid,&artist)==ZUNE_HD_ART_OK);check(artist.created&&writes==2);
    reset();d=device();create_params=0;check(zune_hd_art_ensure_artist(&d,name,guid,&artist)==ZUNE_HD_ART_IO_ERROR);check(artist.write_attempted&&artist.transport_uncertain&&writes==1);
    reset();d=device();network_mode=1;
    check(zune_hd_art_serve(&d,&c,ready,NULL,NULL,NULL,&report)==ZUNE_HD_ART_PARTIAL);
    check(report.ready_called&&report.network_closed&&report.safe_to_continue&&!report.transport_uncertain&&ends==1&&closes==1);
    check(!report.assets[0].acknowledged&&!report.biography_acknowledged&&ready_calls==1);
    reset();d=device();network_mode=1;cancel_in_ready=1;
    check(zune_hd_art_serve(&d,&c,ready,NULL,NULL,NULL,&report)==ZUNE_HD_ART_CANCELLED);
    check(!report.safe_to_continue&&report.transport_uncertain&&d.hd_transport_uncertain&&!ends&&!closes);
    reset();d=device();network_mode=1;
    check(zune_hd_art_serve(&d,&c,uncertain_ready,NULL,NULL,NULL,&report)==ZUNE_HD_ART_IO_ERROR);
    check(!report.safe_to_continue&&report.transport_uncertain&&d.hd_transport_uncertain&&!ends&&!closes);
    reset();d=device();ZuneAlbumObject *albums=NULL;int album_count=0;
    check(!zune_get_albums_checked(&d,&albums,&album_count)&&!albums&&!album_count);
    existing_id=100;check(!zune_get_albums_checked(&d,&albums,&album_count)&&album_count==1&&albums[0].track_count==1);zune_free_albums(albums,album_count);
    refs_fail=1;check(zune_get_albums_checked(&d,&albums,&album_count)==-1&&!albums&&!album_count);
    refs_fail=0;inventory_fail=1;check(zune_get_albums_checked(&d,&albums,&album_count)==-1&&!albums&&!album_count);
    reset();d=device();HdPublicSession *s=calloc(1,sizeof(*s));check(s!=NULL);s->dev=&d;s->result=&report;memset(&report,0,sizeof(report));s->ready=ready;
    check(!hd_prepare(&c,&s->server,emit_ok,NULL));HdNetworkState network={0};network.local_ipcp=network.peer_ipcp=1;
    check(!hd_public_ready(s,&network));check(!hd_public_ready(s,&network));check(ready_calls==1&&report.ready_called);
    check(!hd_complete(s,1000));s->server.asset_completed[0]=1;s->server.distinct_assets_completed=1;s->server.image_completed=1;
    check(!hd_complete(s,2000));check(report.assets[0].acknowledged==1&&!report.assets[1].acknowledged);
    s->server.asset_completed[1]=1;s->server.distinct_assets_completed=2;s->server.image_completed=2;
    check(!hd_complete(s,3000));check(hd_complete(s,6000));s->biography_required=1;check(!hd_complete(s,9000));
    s->server.biography_completed=1;check(!hd_complete(s,9000));check(hd_complete(s,12000));
    s->server.flow[0].active=1;check(!hd_complete(s,14000));s->server.flow[0].active=0;
    memset(&report,0,sizeof(report));s->ready=uncertain_ready;check(hd_public_ready(s,&network)==-1);check(network.callback_transport_uncertain);
    free(s);
    /* Supplementary characters survive exact UTF-16 name encoding. */
    uint8_t wire[511];size_t n=0;check(!hd_name_wire("Artist 🎸",wire,&n));char *decoded=hd_read_name(wire,n);check(decoded&&!strcmp(decoded,"Artist 🎸"));free(decoded);
    printf("HD public API: %u checks passed\n",checks);return 0;
}
