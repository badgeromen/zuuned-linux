#include "../tools/hd_art_serve.h"
#include <assert.h>
static ZuneDevice dev;
static volatile sig_atomic_t stop;
static int checks,snapshots,saves,applies,restores,save_fail,snapshot_fail,apply_mode;
static int64_t now;
static HdArtistBindingResult apply_result,restore_result;
static int64_t clock_mock(void){return now;}
int ptp_transaction(ptp_session_t *p,uint16_t op,uint32_t *params,int n,uint16_t flags,
 uint8_t *data,uint64_t size,uint8_t **received,uint32_t *length,uint16_t *rc,uint32_t *reply,int *count){
 (void)p;(void)op;(void)params;(void)n;(void)flags;(void)data;(void)size;(void)received;(void)length;(void)rc;(void)reply;(void)count;
 assert(!"orchestration fixture must never access real transport");return -1;
}
static HdArtistBindingResult snapshot_mock(ZuneDevice *d,uint32_t id,const char *artist,HdArtistBindingSnapshot *s){
 assert(d==&dev&&id==7&&!strcmp(artist,"Artist")&&!saves&&!applies);++snapshots;
 memset(s,0,sizeof(*s));d->ptp.last_response=PTP_RC_OK;return snapshot_fail?HD_ART_BIND_REJECTED:HD_ART_BIND_CONFIRMED;
}
static int save_mock(const char *path,const HdArtistBindingSnapshot *s){
 assert(path&&!strcmp(path,"backup")&&s&&snapshots==1&&!applies);++saves;return save_fail?-1:0;
}
static HdArtistBindingResult apply_mock(ZuneDevice *d,const HdArtistBindingSnapshot *s,const uint8_t *guid,int(*allowed)(void*),void *context){
 assert(d==&dev&&s&&guid&&!memcmp(guid,hd_art_test_guid_wire,16)&&saves==1);++applies;
 if(apply_mode==1)stop=1;
 if(apply_mode==2)now=10000;
 if(!allowed(context))return HD_ART_BIND_REJECTED;
 d->ptp.last_response=apply_result==HD_ART_BIND_UNCERTAIN?PTP_ERROR_IO:PTP_RC_OK;
 return apply_result;
}
static HdArtistBindingResult restore_mock(ZuneDevice *d,const HdArtistBindingSnapshot *s,const uint8_t *guid){
 assert(d==&dev&&s&&guid&&applies==1);++restores;d->ptp.last_response=restore_result==HD_ART_BIND_UNCERTAIN?PTP_ERROR_IO:PTP_RC_OK;return restore_result;
}
static void check(int ok){assert(ok);++checks;}
static void setup(HdArtReadyBinding *b,FILE *out){
 memset(&dev,0,sizeof(dev));stop=0;now=1;snapshots=saves=applies=restores=save_fail=snapshot_fail=apply_mode=0;
 apply_result=restore_result=HD_ART_BIND_CONFIRMED;
 hd_art_ready_init(b,&dev,out,&stop);b->snapshot_fn=snapshot_mock;b->save_fn=save_mock;b->apply_fn=apply_mock;b->restore_fn=restore_mock;b->now_fn=clock_mock;b->deadline=10000;
}
static void prepare(HdArtReadyBinding *b){check(!hd_art_ready_prepare(b,7,"Artist","backup")&&snapshots==1&&saves==1&&!applies);}
int main(void){
 FILE *out=tmpfile();assert(out);HdArtReadyBinding b;uint32_t id=0;
 check(!hd_art_ready_options("7","backup",hd_art_test_guid,"Artist",1,&id)&&id==7);
 check(hd_art_ready_options("7","backup",hd_art_test_guid,"Artist",0,&id));
 check(hd_art_ready_options("7","backup","wrong","Artist",1,&id));
 check(hd_art_ready_options("7","",hd_art_test_guid,"Artist",1,&id));
 const char *invalid[]={"0","-1","+7","7x"," 7","4294967295","9999999999999999999999"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i)check(hd_art_ready_options(invalid[i],"backup",hd_art_test_guid,"Artist",1,&id));
 setup(&b,out);snapshot_fail=1;check(hd_art_ready_prepare(&b,7,"Artist","backup")&&!saves&&!applies&&!b.uncertain);
 setup(&b,out);save_fail=1;check(hd_art_ready_prepare(&b,7,"Artist","backup")&&!b.prepared&&!applies);
 setup(&b,out);stop=1;check(hd_art_ready_prepare(&b,7,"Artist","backup")&&!snapshots&&!saves);
 setup(&b,out);prepare(&b);check(!hd_art_ready_tick(&b,0)&&!applies);
 check(!hd_art_ready_tick(&b,1)&&applies==1&&b.applied);
 check(!hd_art_ready_tick(&b,0)&&!hd_art_ready_tick(&b,1)&&applies==1);
 stop=1;check(!hd_art_ready_finish(&b,1,0)&&restores==1); /* signal rollback */
 check(hd_art_ready_finish(&b,1,0)&&restores==1);
 setup(&b,out);prepare(&b);now=10000;check(hd_art_ready_tick(&b,1)&&!applies);
 setup(&b,out);prepare(&b);stop=1;check(hd_art_ready_tick(&b,1)&&!applies);
 for(int mode=1;mode<=2;++mode){setup(&b,out);prepare(&b);apply_mode=mode;check(hd_art_ready_tick(&b,1)&&applies==1&&!b.applied&&!b.uncertain);check(!hd_art_ready_finish(&b,1,0)&&!restores);}
 setup(&b,out);prepare(&b);apply_result=HD_ART_BIND_REFUSED;check(hd_art_ready_tick(&b,1)&&!b.uncertain);check(!hd_art_ready_finish(&b,1,0)&&!restores);
 setup(&b,out);prepare(&b);apply_result=HD_ART_BIND_UNCERTAIN;check(hd_art_ready_tick(&b,1)&&b.uncertain);check(hd_art_ready_finish(&b,0,1)&&!restores);
 for(int reason=0;reason<4;++reason){setup(&b,out);prepare(&b);check(!hd_art_ready_tick(&b,1));if(reason==2)dev.cancel_requested=1;if(reason==3)dev.disconnecting=1;check(hd_art_ready_finish(&b,reason!=0,reason==1)&&!restores);}
 setup(&b,out);prepare(&b);check(!hd_art_ready_tick(&b,1));restore_result=HD_ART_BIND_UNCERTAIN;check(hd_art_ready_finish(&b,1,0)&&restores==1&&b.uncertain);
 /* Actual serving tick gates the write until BOTH IPCP and complete framing. */
 setup(&b,out);prepare(&b);HdArtServing art={0};art.binding=&b;HdNetworkState network={0};network.out=out;
 check(!hd_art_tick(&network,&art,now)&&!applies);
 network.local_ipcp=1;check(!hd_art_tick(&network,&art,now)&&!applies);
 network.peer_ipcp=1;network.frame_length=1;check(!hd_art_tick(&network,&art,now)&&!applies);
 network.frame_length=0;network.frame_escaped=1;check(!hd_art_tick(&network,&art,now)&&!applies);
 network.frame_escaped=0;check(!hd_art_tick(&network,&art,now)&&applies==1);
 check(!hd_art_tick(&network,&art,now)&&applies==1);
 setup(&b,out);prepare(&b);art.binding=&b;apply_result=HD_ART_BIND_UNCERTAIN;
 check(hd_art_tick(&network,&art,now)&&network.callback_transport_uncertain&&!restores);
 fclose(out);printf("HD bind-on-ready: %d checks passed; no USB\n",checks);return 0;
}
