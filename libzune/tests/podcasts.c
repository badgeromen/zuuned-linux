/* Real podcast operations and MTP packing; only PTP/device transport is fake. */
#include "../src/zune_internal.h"
#include "../src/mtp_text.h"
#include <assert.h>

typedef struct { uint16_t code, type; uint8_t *data; uint32_t size; } Prop;
typedef struct { uint32_t id, parent, size; uint16_t format; int live, count; Prop props[16]; } Object;
static Object objects[20];
static int used, creates, commits, deletes, calls, checks;
static int series_handle_queries, episode_handle_queries;
static uint16_t fail_op, fail_prop;
static int wrong_descriptor, corrupt_readback, fail_delete, bad_handles, bad_created_handle, normalize_filename, classic_metadata;
static char error_text[512];
static uint32_t read32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t read16(const uint8_t *p) { return p[0]|(p[1]<<8); }
static void put32(uint8_t *p, uint32_t v) { for(int i=0;i<4;++i)p[i]=v>>(8*i); }
static void check(int good,const char *label) { if(!good){fprintf(stderr,"FAIL: %s (%s)\n",label,error_text);exit(1);}printf("PASS: %s\n",label);++checks; }
void zune_set_error(const char *fmt, ...) { va_list ap;va_start(ap,fmt);vsnprintf(error_text,sizeof(error_text),fmt,ap);va_end(ap); }
const char *zune_get_error(void) { return error_text; }
static Object *object(uint32_t id) { for(int i=0;i<used;++i)if(objects[i].live&&objects[i].id==id)return &objects[i];return NULL; }
static Prop *prop(Object *o,uint16_t code) { if(o)for(int i=0;i<o->count;++i)if(o->props[i].code==code)return &o->props[i];return NULL; }
static void reply(const void *p,uint32_t n,uint8_t **d,uint32_t *len) { *d=malloc(n?n:1);assert(*d);memcpy(*d,p,n);*len=n; }
static void reset(void) {
 for(int i=0;i<used;++i)for(int j=0;j<objects[i].count;++j)free(objects[i].props[j].data);
 memset(objects,0,sizeof(objects));used=creates=commits=deletes=calls=0;series_handle_queries=episode_handle_queries=0;
 fail_op=fail_prop=0;wrong_descriptor=corrupt_readback=fail_delete=bad_handles=bad_created_handle=normalize_filename=classic_metadata=0;
}
static void duplicate(uint32_t id) {
 Object *o=object(id);assert(o&&used<20);Object *copy=&objects[used++];*copy=*o;copy->id=100+used;
 for(int i=0;i<o->count;++i){copy->props[i].data=malloc(o->props[i].size);memcpy(copy->props[i].data,o->props[i].data,o->props[i].size);}
}
static uint16_t type_for(uint16_t code) {
 switch(code){case 0xda9c:case 0xda9b:return PTP_DTC_UINT8;
 case 0xda9d:case 0xda9e:case 0xdd62:case MTP_OPC_Duration:return PTP_DTC_UINT32;
 case MTP_OPC_MetaGenre:return PTP_DTC_UINT16;
 case 0xdd60:case MTP_OPC_Description:return PTP_DTC_AUINT16;
 default:return PTP_DTC_STR;}
}
int ptp_transaction(ptp_session_t *s,uint16_t op,uint32_t *params,int nparams,
 uint16_t flags,uint8_t *send,uint64_t send_len,uint8_t **recv,uint32_t *recv_len,
 uint16_t *response,uint32_t *rp,int *rn)
{
 ++calls;if(recv)*recv=NULL;if(recv_len)*recv_len=0;
 s->last_response=PTP_RC_OK;
 if(op==fail_op||(op==MTP_OC_GetObjectPropValue&&params[1]==fail_prop)||(fail_delete&&op==PTP_OC_DeleteObject))s->last_response=0x200f;
 if(response)*response=s->last_response;
 if(s->last_response!=PTP_RC_OK)return -1;
 switch(op){
 case PTP_OC_GetObjectHandles: {
  assert(flags==PTP_DP_GETDATA&&nparams==3);
  if(params[1]==0xba0b)++series_handle_queries;
  if(params[1]==MTP_OFC_MP3)++episode_handle_queries;
  uint8_t bytes[84]={0};uint32_t n=0;
  if(bad_handles==1){reply(bytes,0,recv,recv_len);return 0;}
  if(bad_handles==2){put32(bytes,0xffffffff);reply(bytes,4,recv,recv_len);return 0;}
  for(int i=0;i<used;++i)if(objects[i].live&&objects[i].format==params[1]&&(!params[2]||objects[i].parent==params[2]))put32(bytes+4+4*n++,objects[i].id);
  put32(bytes,n);reply(bytes,4+4*n,recv,recv_len);return 0;
 }
 case MTP_OC_GetObjectPropsSupported: {
  const uint16_t codes[]={0xda9c,0xda9d,0xdd60,0xda9e,0xdd62,0xda9b,ZUNE_OPC_SeriesName,MTP_OPC_Name,MTP_OPC_Artist,MTP_OPC_ObjectFileName,MTP_OPC_Duration,MTP_OPC_MetaGenre,MTP_OPC_Description,0xdc47};
  uint8_t bytes[4+sizeof(codes)]={0};put32(bytes,sizeof(codes)/2);
  for(size_t i=0;i<sizeof(codes)/2;++i){bytes[4+i*2]=codes[i];bytes[5+i*2]=codes[i]>>8;}
  reply(bytes,sizeof(bytes),recv,recv_len);return 0;
 }
 case MTP_OC_GetObjectPropDesc: {
  uint16_t type=wrong_descriptor?PTP_DTC_STR:type_for(params[0]);uint8_t b[16]={params[0]&255,params[0]>>8,type&255,type>>8,1};
  reply(b,sizeof(b),recv,recv_len);return 0;
 }
 case MTP_OC_SendObjectPropList: {
  assert(nparams==5&&flags==PTP_DP_SENDDATA&&used<20&&params[3]==0);
  Object *o=&objects[used++];o->id=100+used;o->parent=params[1];o->format=params[2];o->size=params[4];o->live=1;o->count=read32(send);
  if(classic_metadata&&o->format==MTP_OFC_MP3)o->id|=classic_metadata==2?0x01000000:0x10000000;
  assert(o->count<=16);uint64_t off=4;
  for(int i=0;i<o->count;++i){
   Prop *p=&o->props[i];assert(off+8<=send_len&&read32(send+off)==0);p->code=read16(send+off+4);p->type=read16(send+off+6);off+=8;
   if(p->type==PTP_DTC_UINT8)p->size=1;
   else if(p->type==PTP_DTC_UINT16)p->size=2;
   else if(p->type==PTP_DTC_UINT32)p->size=4;
   else if(p->type==PTP_DTC_STR)p->size=1+2*send[off];
   else if(p->type==PTP_DTC_AUINT16)p->size=4+2*read32(send+off);
   else abort();
   assert(off+p->size<=send_len);p->data=malloc(p->size);assert(p->data);memcpy(p->data,send+off,p->size);off+=p->size;
  }
  assert(off==send_len);++creates;rp[0]=1;rp[1]=o->parent;rp[2]=bad_created_handle?UINT32_MAX:o->id;*rn=3;return 0;
 }
 case PTP_OC_SendObject:
  assert(used&&send_len==objects[used-1].size);++commits;return 0;
 case MTP_OC_GetObjectPropValue: {
  Prop *p=prop(object(params[0]),params[1]);if(!p)return -1;
  if(normalize_filename&&object(params[0])->format==MTP_OFC_MP3&&params[1]==MTP_OPC_ObjectFileName) {
   if (normalize_filename >= 3) {
    Prop *name = prop(object(params[0]), MTP_OPC_Name);
    char *title = mtp_ucs2_to_string(name->data, name->size);
    char filename[1024];
    snprintf(filename, sizeof(filename), "%s%s", normalize_filename == 4 ? "Wrong? | title" : title,
             normalize_filename == 5 ? ".wav" : ".mp3");
    *recv = mtp_string_to_ucs2(filename, recv_len); free(title); return 0;
   }
   *recv=mtp_string_to_ucs2(normalize_filename==2?"../invalid.mp3":"Firmware assigned name.mp3",recv_len);return 0;
  }
  if(classic_metadata&&object(params[0])->format==MTP_OFC_MP3) {
   if(params[1]==MTP_OPC_MetaGenre){uint8_t zero[2]={0};reply(zero,2,recv,recv_len);return 0;}
   if(params[1]==ZUNE_OPC_SeriesName){*recv=mtp_string_to_ucs2("",recv_len);return 0;}
   if(params[1]==0xdc47){*recv=mtp_string_to_ucs2("20261008T120000.0",recv_len);return 0;}
  }
  reply(p->data,p->size,recv,recv_len);
  if(corrupt_readback&&(corrupt_readback==1||object(params[0])->format==MTP_OFC_MP3)&&params[1]==MTP_OPC_Name&&p->size>2)(*recv)[1]^=1;
  return 0;
 }
 case PTP_OC_GetObjectInfo: {
  Object *o=object(params[0]);assert(o);uint8_t b[55]={0};put32(b,1);b[4]=o->format;b[5]=o->format>>8;put32(b+8,o->size);put32(b+38,o->parent);
  reply(b,sizeof(b),recv,recv_len);return 0;
 }
 case PTP_OC_DeleteObject: {
  Object *o=object(params[0]);assert(o);o->live=0;++deletes;return 0;
 }
 default:fprintf(stderr,"Unexpected operation 0x%04X\n",op);abort();
 }
}
static uint32_t forge(ZuneDevice *dev,const char *feed) { uint32_t id=0;int r=zune_forge_podcast_series(dev,11,"show.ser","Les histoires","Auteur",feed,&id);assert(r==0);return id; }
int main(void)
{
 ZuneDevice dev={0};dev.storage_id=1;
 uint32_t unauth_id=123;
 ZunePodcastSeries *unauth_series=NULL;int unauth_count=123;
 check(zune_get_podcast_series(&dev,&unauth_series,&unauth_count)<0&&!unauth_series&&!unauth_count&&!calls,"unauthenticated inventory cannot report an empty library");
 check(zune_forge_podcast_series(&dev,11,"show.ser","Show","","https://example.test/feed",&unauth_id)<0&&!unauth_id&&!calls,"failed authentication blocks series writes before USB");
 check(zune_smuggle_podcast_episode(&dev,22,"missing.mp3",NULL,&unauth_id)<0&&!unauth_id&&!calls,"failed authentication blocks episode writes before USB");
 dev.mtpz_authenticated=1;
 uint32_t n=0;const char *unicode="https://example.test/épisode/世界/🎧";
 uint8_t *wire=mtp_text_to_array(unicode,&n);char *back=mtp_array_to_text(wire,n);
 check(back&&!strcmp(back,unicode),"AUINT16 roundtrip preserves accents, CJK and surrogate pairs");free(back);free(wire);
 const char *bad[]={"\xc0\xaf","\xed\xa0\x80","\xf4\x90\x80\x80","\xf0\x9f","\xe2\x82","\x80"};
 int rejected=0;for(size_t i=0;i<sizeof(bad)/sizeof(*bad);++i){wire=mtp_text_to_array(bad[i],&n);rejected+=wire==NULL;free(wire);}
 check(rejected==6,"malformed, overlong, surrogate and truncated UTF-8 rejected");
 uint8_t bad16[]={1,0,0,0,0,0xd8};check(mtp_array_to_text(bad16,sizeof(bad16))==NULL,"unpaired UTF-16 surrogate rejected");
 uint8_t nul16[]={3,0,0,0,'a',0,0,0,'b',0};check(mtp_array_to_text(nul16,sizeof(nul16))==NULL,"embedded UTF-16 NUL cannot truncate identity");
 uint8_t terminated16[]={3,0,0,0,'a',0,'b',0,0,0};back=mtp_array_to_text(terminated16,sizeof(terminated16));
 check(back&&!strcmp(back,"ab"),"one counted terminal NUL preserves complete text");free(back);
 uint8_t double_nul[]={4,0,0,0,'a',0,'b',0,0,0,0,0};
 check(mtp_array_to_text(double_nul,sizeof(double_nul))==NULL,"multiple trailing terminators remain malformed");
 char *long_feed=malloc(2049);memset(long_feed,'a',2048);memcpy(long_feed,"https://example.test/",21);long_feed[2048]=0;
 uint32_t show=forge(&dev,long_feed);
 Prop *feed=prop(object(show),0xdd60);check(feed&&feed->type==PTP_DTC_AUINT16&&read32(feed->data)==2049&&creates==1&&commits==1,"long feed URL uses a 32-bit unit count including terminator and zero-byte series commit");
 uint32_t again=0;check(zune_forge_podcast_series(&dev,11,"different.ser","Renamed","Else",long_feed,&again)==0&&again==show&&creates==1,"same feed reuses series without overwriting manual names");
 ZunePodcastSeries *series=NULL;int count=0;
 check(zune_get_podcast_series(&dev,&series,&count)==0&&count==1&&!strcmp(series[0].feed_url,long_feed),"typed series inventory reads complete feed identity");zune_free_podcast_series(series,count);
 uint32_t other=forge(&dev,"https://example.test/another");check(other!=show&&creates==2,"same title with different feed remains a separate show");
 duplicate(show);int before=creates;
 check(zune_forge_podcast_series(&dev,11,"show.ser","Les histoires","Auteur",long_feed,&again)<0&&again==0&&creates==before,"ambiguous existing feed blocks creation");
 reset();fail_op=PTP_OC_GetObjectHandles;
 check(zune_forge_podcast_series(&dev,11,"show.ser","Show","",long_feed,&again)<0&&!creates,"failed inventory cannot create duplicate series");
 reset();bad_handles=1;
 check(zune_forge_podcast_series(&dev,11,"show.ser","Show","",long_feed,&again)<0&&!creates,"missing handle payload is not an empty device");
 reset();bad_handles=2;
 check(zune_forge_podcast_series(&dev,11,"show.ser","Show","",long_feed,&again)<0&&!creates,"wrapped handle count is rejected without memory access");
 reset();wrong_descriptor=1;
 check(zune_forge_podcast_series(&dev,11,"show.ser","Show","",long_feed,&again)<0&&!creates,"incompatible descriptor type prevents writes");
 reset();bad_created_handle=1;
 check(zune_forge_podcast_series(&dev,11,"show.ser","Show","",long_feed,&again)<0&&!again&&!deletes&&!commits,"reserved all-object handle can never reach cleanup or commit");
 reset();fail_op=PTP_OC_SendObject;
 check(zune_forge_podcast_series(&dev,11,"show.ser","Show","",long_feed,&again)==ZUNE_PODCAST_FAILED_CLEANED&&!again&&deletes==1&&dev.ptp.last_response==0x200f,"failed commit removes its own orphan and preserves original response");
 reset();fail_op=PTP_OC_SendObject;fail_delete=1;
 check(zune_forge_podcast_series(&dev,11,"show.ser","Show","",long_feed,&again)==-1&&again&&object(again),"failed cleanup exposes orphan handle for recovery");
 reset();corrupt_readback=1;
 check(zune_forge_podcast_series(&dev,11,"show.ser","Show","",long_feed,&again)<0&&!again&&deletes==1,"incorrect series readback fails and cleans only new object");
 reset();dev.cancel_requested=1;
 check(zune_forge_podcast_series(&dev,11,"show.ser","Show","",long_feed,&again)<0&&!calls,"cancelled creation performs no USB requests");dev.cancel_requested=0;
 check(zune_forge_podcast_series(&dev,11,"../show.ser","Show","",long_feed,&again)<0&&!calls,"unsafe wire filename rejected before USB");
 reset();show=forge(&dev,"https://example.test/feed");
 char path[]="/tmp/zuuned-podcast-fixture-XXXXXX";int fd=mkstemp(path);assert(fd>=0);assert(write(fd,"fixture MP3 payload",19)==19);close(fd);
 char description[2049];memset(description,'x',2048);description[2048]=0;
 ZunePodcastEpisodeInput ep={.filename="episode.mp3",.title="Episode one",.author="Auteur",.series_name="Les histoires",.source_url=unicode,.description=description,.date_authored="20261008T120000Z",.series_id=show,.duration_ms=10000};
 uint32_t episode=0;
 normalize_filename=1;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&episode)==0&&episode&&creates==2&&commits==2,"audio episode transfers and verifies properties and object size");
 Prop *genre=prop(object(episode),MTP_OPC_MetaGenre);Prop *link=prop(object(episode),0xda9e);Prop *desc=prop(object(episode),MTP_OPC_Description);
 check(genre&&read16(genre->data)==64&&link&&read32(link->data)==show&&desc&&read32(desc->data)==2049,"episode carries podcast category, series link and full description");
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)==0&&again==episode&&creates==2,"repeat episode sync reuses exact series and source URL");
 ep.source_url="https://example.test/episode-two";
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)==0&&again!=episode&&creates==3,"same-title episodes with different source URLs remain distinct");
 ep.source_url=unicode;ep.title="Conflict";before=creates;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)<0&&!again&&creates==before,"same episode URL with conflicting title fails without writes");ep.title="Episode one";
 duplicate(episode);
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)<0&&!again&&creates==before,"duplicate matching episode objects fail explicitly");
 reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;fail_op=PTP_OC_SendObject;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)==ZUNE_PODCAST_FAILED_CLEANED&&!again&&deletes==1&&object(show),"failed episode upload preserves its existing show");
 fail_op=0;fail_prop=0xdd60;before=creates;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)<0&&creates==before,"unreadable feed identity blocks upload");
 fail_prop=0;ep.series_id=9999;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)<0&&creates==before,"stale series selection cannot receive an episode");
 ep.series_id=show;ep.date_authored="not-a-date";before=calls;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)<0&&calls==before,"invalid date rejected before USB");
 ep.date_authored="20260229T120000Z";
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)<0&&calls==before,"invalid calendar date rejected before USB");
 ep.date_authored="20261008T240000Z";
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)<0&&calls==before,"invalid time rejected before USB");
 reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;ep.date_authored="20261008T120000Z";normalize_filename=2;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)<0&&!again&&deletes==1&&object(show),"unsafe firmware filename fails and cleans only its episode");
 reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;classic_metadata=1;normalize_filename=1;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&episode)==0&&episode>>24==0x10,"Keel podcast schema verifies normalized filename, empty redundant name, zero MetaGenre and timestamp");
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)==0&&again==episode&&creates==2,"Keel normalized episode is reused without duplicate creation");
 const char *punctuated[] = {
  "What the HECK is happening with the PS5 right now? | Nontendo Podcast #190",
  "Episode: \"Quotes\" / Slash \\ Backslash? | caf\xC3\xA9 #190"
 };
 const ZuneDeviceFamily families[] = {ZUNE_FAMILY_KEEL, ZUNE_FAMILY_PAVO, ZUNE_FAMILY_DRACO};
 for (int family = 0; family < 3; ++family) {
  dev.device_family = families[family];
  for (int t = 0; t < 2; ++t) {
   reset(); show=forge(&dev,"https://example.test/feed"); ep.series_id=show;
   classic_metadata=1; normalize_filename=3; ep.title=punctuated[t];
   check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&episode)==0 && episode>>24==0x10,
         "native podcast verifies exact title-derived filename with punctuation and Unicode");
   check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)==0 && again==episode && creates==2,
         "title-derived filename does not change exact episode reuse");
  }
 }
 dev.device_family=ZUNE_FAMILY_KEEL;
 char long_title[241]; memset(long_title,'A',239);long_title[239]='?';long_title[240]=0;
 reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;
 classic_metadata=1;normalize_filename=3;ep.title=long_title;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&episode)==0,
       "bounded title-derived metadata can exceed outgoing basename length without truncation");
 ep.title=punctuated[0];
 for (int mode = 4; mode <= 5; ++mode) {
  reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;classic_metadata=1;normalize_filename=mode;
  check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)==ZUNE_PODCAST_FAILED_CLEANED && !again && deletes==1,
        "unrelated punctuation filename or wrong suffix still fails and cleans its new episode");
 }
 reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;normalize_filename=3;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)==ZUNE_PODCAST_FAILED_CLEANED && !again,
       "non-native schema cannot use title-derived punctuation exception");
 reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;classic_metadata=1;normalize_filename=3;corrupt_readback=2;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)==ZUNE_PODCAST_FAILED_CLEANED && !again,
       "matching title-derived filename never excuses wrong title readback");
 ep.filename="Unsafe? | name.mp3";before=calls;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)<0 && calls==before,
       "outgoing wire filename punctuation remains rejected before USB");
 ep.filename="episode.mp3";ep.title="Episode one";
 dev.device_family=ZUNE_FAMILY_PAVO;reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;classic_metadata=1;normalize_filename=1;
 check(zune_smuggle_podcast_episode(&dev,11,path,&ep,&episode)==0&&episode>>24==0x10,"Pavo native podcast accepts observed zero MetaGenre and empty redundant SeriesName");
 check(zune_smuggle_podcast_episode(&dev,11,path,&ep,&again)==0&&again==episode&&creates==2,"Pavo normalized podcast repeat reuses exact episode");
 uint32_t hd_folder=0,hd_show=0,hd_episode=0;
 check(zune_find_podcast_episode(&dev,"https://example.test/feed",ep.source_url,&hd_folder,&hd_show,&hd_episode)==0&&hd_episode==episode&&hd_show==show&&hd_folder==11,"Pavo normalized podcast lookup preserves source and series identity");
 fail_prop=0xda9e;
 check(zune_find_podcast_episode(&dev,"https://example.test/feed",ep.source_url,&hd_folder,&hd_show,&hd_episode)==-1&&!hd_episode,"Pavo schema never excuses unreadable series link");fail_prop=0;
 ep.source_url="https://example.test/new";corrupt_readback=2;
 check(zune_smuggle_podcast_episode(&dev,11,path,&ep,&again)==ZUNE_PODCAST_FAILED_CLEANED&&!again&&deletes==1&&object(episode),"Pavo wrong title is still rejected and only the new object is cleaned");
 corrupt_readback=0;ep.source_url=unicode;
 dev.device_family=ZUNE_FAMILY_PAVO;reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;classic_metadata=2;
 check(zune_smuggle_podcast_episode(&dev,11,path,&ep,&again)==ZUNE_PODCAST_FAILED_CLEANED&&!again&&deletes==1,"Pavo ordinary music schema cannot use podcast normalization");
 reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;classic_metadata=1;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&episode)==0,"Pavo fixture restores a native episode for unknown-family refusal");
 dev.device_family=ZUNE_FAMILY_UNKNOWN;before=creates;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)<0&&creates==before,"unknown device cannot use Keel schema readback rules");
 dev.device_family=ZUNE_FAMILY_KEEL;reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;classic_metadata=2;
 check(zune_smuggle_podcast_episode(&dev,22,path,&ep,&again)<0&&!again&&deletes==1&&object(show),"ordinary music schema cannot pass as a podcast when MetaGenre reads zero");
 reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;
 check(zune_smuggle_podcast_episode(&dev,11,path,&ep,&episode)==0,"queue lookup fixture uploaded");
 uint32_t folder=99,found_show=99,found_ep=99;before=creates;
 check(zune_find_podcast_episode(&dev,"https://example.test/feed",ep.source_url,&folder,&found_show,&found_ep)==0&&folder==11&&found_show==show&&found_ep==episode&&creates==before,"read-only queue admission finds exact live episode and series parent");
 check(zune_find_podcast_episode(&dev,"https://example.test/feed","https://example.test/absent",&folder,&found_show,&found_ep)==0&&folder==11&&found_show==show&&!found_ep,"different episode URL remains eligible");
 check(zune_find_podcast_episode(&dev,"https://example.test/absent",ep.source_url,&folder,&found_show,&found_ep)==0&&!folder&&!found_show&&!found_ep,"different feed cannot reuse a same-title series");
 fail_op=PTP_OC_GetObjectInfo;
 check(zune_find_podcast_episode(&dev,"https://example.test/feed",ep.source_url,&folder,&found_show,&found_ep)<0&&!folder&&!found_show&&!found_ep&&creates==before,"unreadable parent fails closed without partial outputs or writes");fail_op=0;
 fail_prop=0xda9e;
 check(zune_find_podcast_episode(&dev,"https://example.test/feed",ep.source_url,&folder,&found_show,&found_ep)<0&&!found_ep,"unreadable episode link cannot mean absent");fail_prop=0;
 duplicate(episode);
 check(zune_find_podcast_episode(&dev,"https://example.test/feed",ep.source_url,&folder,&found_show,&found_ep)<0&&!found_ep&&creates==before,"ambiguous live episodes block queue admission");
 duplicate(show);
 check(zune_find_podcast_episode(&dev,"https://example.test/feed",ep.source_url,&folder,&found_show,&found_ep)<0&&!found_show,"ambiguous feed series block queue admission");
 dev.mtpz_authenticated=0;
 check(zune_find_podcast_episode(&dev,"https://example.test/feed",ep.source_url,&folder,&found_show,&found_ep)<0&&!found_ep,"unauthenticated lookup never reports an empty device");dev.mtpz_authenticated=1;
 /* Bulk identity reads scale with physical inventory, never local candidates. */
 reset();dev.device_family=ZUNE_FAMILY_KEEL;
 ZunePodcastEpisodeSource *sources=(void*)1;int source_count=123;
 check(!zune_get_podcast_episode_sources(&dev,&sources,&source_count)&&!sources&&!source_count,
       "bulk source inventory distinguishes known empty");
 dev.mtpz_authenticated=0;before=calls;
 check(zune_get_podcast_episode_sources(&dev,&sources,&source_count)<0&&!sources&&!source_count&&calls==before,
       "bulk source authentication before USB");dev.mtpz_authenticated=1;
 dev.cancel_requested=1;
 check(zune_get_podcast_episode_sources(&dev,&sources,&source_count)<0&&!sources&&!source_count&&calls==before,
       "bulk source cancellation before USB");dev.cancel_requested=0;
 dev.device_family=ZUNE_FAMILY_UNKNOWN;
 check(zune_get_podcast_episode_sources(&dev,&sources,&source_count)<0&&!sources&&!source_count&&calls==before,
       "bulk source rejects untested model");dev.device_family=ZUNE_FAMILY_KEEL;
 show=forge(&dev,"https://example.test/feed");ep.series_id=show;ep.source_url=unicode;
 check(!zune_smuggle_podcast_episode(&dev,11,path,&ep,&episode),"bulk source fixture episode");
 uint32_t show2=forge(&dev,"https://example.test/feed-two");ep.series_id=show2;
 check(!zune_smuggle_podcast_episode(&dev,11,path,&ep,&again),"different feed may own same source URL");
 series_handle_queries=episode_handle_queries=0;before=creates;int old_deletes=deletes,old_commits=commits;
 check(!zune_get_podcast_episode_sources(&dev,&sources,&source_count)&&source_count==2&&
       series_handle_queries==1&&episode_handle_queries==1&&creates==before&&deletes==old_deletes&&commits==old_commits,
       "bulk source reads series once and shared folder once without mutation");
 check(sources[0].parent_id==11&&sources[0].series_id==show&&sources[1].series_id==show2&&
       !strcmp(sources[0].source_url,unicode)&&!strcmp(sources[1].source_url,unicode)&&
       sources[0].source_url!=sources[1].source_url,
       "bulk source exact feed and Unicode source ownership survive same-title shows");
 zune_free_podcast_episode_sources(sources,source_count);sources=NULL;
 fail_prop=0xda9e;
 check(zune_get_podcast_episode_sources(&dev,&sources,&source_count)<0&&!sources&&!source_count,
       "bulk unreadable series link fails without partial output");fail_prop=0;
 fail_prop=0xdd60;
 check(zune_get_podcast_episode_sources(&dev,&sources,&source_count)<0&&!sources&&!source_count,
       "bulk unreadable feed or source fails closed");fail_prop=0;
 fail_op=PTP_OC_GetObjectInfo;
 check(zune_get_podcast_episode_sources(&dev,&sources,&source_count)<0&&!sources&&!source_count,
       "bulk parent read failure discards identity map");fail_op=0;
 Prop *source_prop=prop(object(again),0xdd60);uint8_t saved_source=source_prop->data[4];source_prop->data[4]=0;
 check(zune_get_podcast_episode_sources(&dev,&sources,&source_count)<0&&!sources&&!source_count,
       "later malformed source frees previous owned output");source_prop->data[4]=saved_source;
 Prop *series_prop=prop(object(again),0xda9e);put32(series_prop->data,0);
 check(!zune_get_podcast_episode_sources(&dev,&sources,&source_count)&&source_count==1&&sources[0].item_id==episode,
       "unrelated MP3 link never becomes a podcast identity");zune_free_podcast_episode_sources(sources,source_count);sources=NULL;put32(series_prop->data,show2);
 duplicate(episode);
 check(zune_get_podcast_episode_sources(&dev,&sources,&source_count)<0&&!sources&&!source_count,
       "duplicate exact episode source rejects whole bulk map");
 duplicate(show);
 check(zune_get_podcast_episode_sources(&dev,&sources,&source_count)<0&&!sources&&!source_count,
       "duplicate feed series rejects whole bulk map");
 reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;ep.source_url=unicode;
 classic_metadata=1;normalize_filename=1;dev.device_family=ZUNE_FAMILY_PAVO;
 check(!zune_smuggle_podcast_episode(&dev,11,path,&ep,&episode)&&
       !zune_get_podcast_episode_sources(&dev,&sources,&source_count)&&source_count==1&&sources[0].item_id==episode,
       "Pavo zero MetaGenre native episode classified in bulk source map");zune_free_podcast_episode_sources(sources,source_count);sources=NULL;
 object(episode)->id=(object(episode)->id&0xffffff)|0x01000000;
 check(zune_get_podcast_episode_sources(&dev,&sources,&source_count)<0&&!sources&&!source_count,
       "music schema cannot use zero MetaGenre podcast normalization in bulk");
 /* Draco reproduces the second-generation GUI readback, independently of state parsing. */
 dev.device_family=ZUNE_FAMILY_DRACO;
 reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;
 classic_metadata=1;normalize_filename=3;ep.title=punctuated[0];
 check(!zune_smuggle_podcast_episode(&dev,11,path,&ep,&episode),
       "Draco accepts observed schema 0x10, zero genre, empty series and title filename");
 uint32_t draco_folder=0,draco_show=0,draco_episode=0;
 check(!zune_find_podcast_episode(&dev,"https://example.test/feed",ep.source_url,&draco_folder,&draco_show,&draco_episode)&&
       draco_folder==11&&draco_show==show&&draco_episode==episode,
       "Draco exact lookup recognizes uploaded episode without a duplicate");
 check(!zune_get_podcast_episode_sources(&dev,&sources,&source_count)&&source_count==1&&
       sources[0].item_id==episode&&!strcmp(sources[0].source_url,ep.source_url),
       "Draco source map retains exact identity despite normalized metadata");
 zune_free_podcast_episode_sources(sources,source_count);sources=NULL;
 fail_prop=0xda9e;
 check(zune_find_podcast_episode(&dev,"https://example.test/feed",ep.source_url,&draco_folder,&draco_show,&draco_episode)<0&&!draco_episode,
       "Draco unreadable series link never means absent");fail_prop=0;
 for (int mode=0;mode<4;++mode) {
  reset();show=forge(&dev,"https://example.test/feed");ep.series_id=show;
  classic_metadata=mode==0?2:1;normalize_filename=mode==2?4:mode==3?5:3;corrupt_readback=mode==1?2:0;
  check(zune_smuggle_podcast_episode(&dev,11,path,&ep,&again)==ZUNE_PODCAST_FAILED_CLEANED&&!again&&deletes==1&&object(show),
        "Draco wrong schema, title, filename or suffix still rejects and cleans only its new episode");
 }
 unlink(path);free(long_feed);reset();printf("Podcasts: %d checks passed; no USB\n",checks);
}
