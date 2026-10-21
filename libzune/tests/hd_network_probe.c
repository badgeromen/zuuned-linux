#include <stdint.h>
static int64_t test_now(void);
static void test_pause(void);
#define HD_TUNNEL_NOW_MS test_now
#define HD_TUNNEL_PAUSE test_pause
#include "../tools/hd_network_probe.h"
#include "../tools/hd_image_server.h"
#include <assert.h>
static int checks,emissions,calls,polls,closes,ends,client_replies;
static int64_t now;
static uint8_t emitted[2048];static size_t emitted_len;static uint16_t emitted_proto;
static int64_t test_now(void){return now;}
static void test_pause(void){now+=250;}
static void check(int ok,const char *label){if(!ok){fprintf(stderr,"FAIL %s\n",label);exit(1);}printf("PASS %s\n",label);++checks;}
static int save(void *ctx,uint16_t proto,const uint8_t *p,size_t n){(void)ctx;assert(n<=sizeof(emitted));memcpy(emitted,p,n);emitted_len=n;emitted_proto=proto;emissions++;return 0;}
static void init(HdNetworkState *s,FILE *out){memset(s,0,sizeof(*s));s->out=out;s->send=save;emissions=0;}
static void set_length(uint8_t *p,size_t n){p[2]=n>>8;p[3]=n;}
static void ip_packet(uint8_t p[40],int tcp){
 memset(p,0,40);p[0]=0x45;p[2]=0;p[3]=tcp?40:28;p[8]=64;p[9]=tcp?6:17;
 memcpy(p+12,hd_network_peer_ip,4);memcpy(p+16,hd_network_host_ip,4);
 p[20]=0xc0;p[21]=0x29;p[22]=0xc3;p[23]=0x1d;
 if(tcp){p[32]=0x50;p[33]=2;}else {p[24]=0;p[25]=8;}
 uint16_t sum=hd_network_checksum(p,20);p[10]=sum>>8;p[11]=sum;
}
static const uint8_t peer_ipcp[]={1,3,0,16,3,6,192,168,55,101,129,6,127,0,0,1};
static void negotiate(HdNetworkState *s){
 assert(!hd_network_packet(s,0xc021,hd_network_lcp_request,sizeof(hd_network_lcp_request)));
 uint8_t ack[14];memcpy(ack,hd_network_lcp_request,14);ack[0]=2;
 assert(!hd_network_packet(s,0xc021,ack,14));
 assert(!hd_network_packet(s,0x8021,peer_ipcp,sizeof(peer_ipcp)));
 uint8_t ip_ack[10];memcpy(ip_ack,hd_network_ipcp_request,10);ip_ack[0]=2;
 assert(!hd_network_packet(s,0x8021,ip_ack,10));
}
static int callback(HdNetworkState *s,void *ctx,const uint8_t *p,size_t n){(void)s;(void)p;assert(n==40);++*(int*)ctx;return 1;}
static void state_tests(FILE *out){
 HdNetworkState s;init(&s,out);
 check(!hd_network_packet(&s,0xc021,hd_network_lcp_request,14)&&emissions==2&&s.peer_lcp&&!s.local_lcp,"peer LCP ACK and independent host request");
 uint8_t ack[14];memcpy(ack,hd_network_lcp_request,14);ack[0]=2;ack[1]=2;
 check(hd_network_packet(&s,0xc021,ack,14)!=0&&!s.local_lcp,"wrong LCP ACK identifier rejected");ack[1]=1;
 check(!hd_network_packet(&s,0xc021,ack,14)&&s.local_lcp&&emitted_proto==0x8021,"exact LCP ACK starts IPCP");
 uint8_t request[]={1,2,0,34,3,6,0,0,0,0,129,6,0,0,0,0,130,6,0,0,0,0,131,6,0,0,0,0,132,6,0,0,0,0};
 check(!hd_network_packet(&s,0x8021,request,sizeof(request))&&emitted_len==16&&emitted[0]==3&&!s.peer_ipcp,"captured zero address/DNS request gets bounded Nak");
 check(!memcmp(emitted+6,hd_network_peer_ip,4)&&emitted[12]==127&&emitted[15]==1,"Nak assigns exact captured addresses");
 s.dns_to_host=1;check(!hd_network_packet(&s,0x8021,request,sizeof(request))&&!memcmp(emitted+12,hd_network_host_ip,4),"explicit local DNS mode uses host address");s.dns_to_host=0;
 check(!hd_network_packet(&s,0x8021,peer_ipcp,sizeof(peer_ipcp))&&s.peer_ipcp&&emitted[0]==2,"peer chosen IP/DNS ACKed");
 uint8_t ip_ack[10];memcpy(ip_ack,hd_network_ipcp_request,10);ip_ack[0]=2;ip_ack[9]=99;
 check(hd_network_packet(&s,0x8021,ip_ack,10)!=0&&!s.local_ipcp,"modified host-IP ACK refused");ip_ack[9]=100;
 check(!hd_network_packet(&s,0x8021,ip_ack,10)&&s.local_ipcp,"host-IP exact ACK completes IPCP");
 uint8_t ccp[]={1,0,0,10,18,6,0,0,0,0};
 check(!hd_network_packet(&s,0x80fd,ccp,10)&&emitted[0]==4&&emitted_len==10,"CCP compression options rejected verbatim");
 const uint8_t empty[]={1,1,0,4};check(!hd_network_packet(&s,0x80fd,empty,4)&&emitted[0]==2,"empty CCP does not enable compression");
 uint8_t ip[40];ip_packet(ip,1);int callback_count=0;s.ipv4=callback;s.ipv4_context=&callback_count;
 check(!hd_network_packet(&s,0x21,ip,40)&&s.tcp_packets==1&&callback_count==1&&s.callback_complete,"valid TCP SYN reaches optional image-service callback");
 s.ipv4=NULL;ip[10]^=1;check(hd_network_packet(&s,0x21,ip,40)!=0,"bad IPv4 checksum refused");ip[10]^=1;
 ip[3]=39;check(hd_network_packet(&s,0x21,ip,40)!=0,"IP total length exactness enforced");
 ip_packet(ip,0);check(!hd_network_packet(&s,0x21,ip,28)&&s.udp_packets==1,"UDP header summary accepted");
 ip[25]=7;check(hd_network_packet(&s,0x21,ip,28)!=0,"UDP length mismatch rejected");
 init(&s,out);ip_packet(ip,1);check(hd_network_packet(&s,0x21,ip,40)!=0,"IP traffic before negotiated IPCP refused");
 for(unsigned i=0;i<9;++i){int rc=hd_network_packet(&s,0xc021,hd_network_lcp_request,14);check((rc==0)==(i<8),"LCP request count bounded");}
 init(&s,out);negotiate(&s);
 uint8_t unsupported[]={1,4,0,10,2,6,0,0x2d,15,1};
 check(!hd_network_packet(&s,0x8021,unsupported,sizeof(unsupported))&&emitted[0]==4&&!s.peer_ipcp,"IP header compression option rejected, previous peer state invalidated");
 uint8_t duplicate[]={1,4,0,16,3,6,192,168,55,101,3,6,192,168,55,101};
 check(hd_network_packet(&s,0x8021,duplicate,16)!=0,"duplicate IPCP options refused");
 for(size_t n=0;n<14;++n){init(&s,out);check(hd_network_packet(&s,0xc021,hd_network_lcp_request,n)!=0,"all truncated LCP lengths rejected");}
 init(&s,out);s.sends=HD_NETWORK_SEND_MAX;check(hd_network_packet(&s,0xc021,hd_network_lcp_request,14)!=0,"outbound message budget enforced");
}
static void framing_tests(FILE *out){
 /* Actual capture3182 wire bytes, independent from our encoder. */
 static const uint8_t captured[]={0x7e,0xff,0x7d,0x23,0xc0,0x21,0x7d,0x21,0x7d,0x20,0x7d,0x20,0x7d,0x2e,0x7d,0x22,0x7d,0x26,0x7d,0x20,0x7d,0x20,0x7d,0x20,0x7d,0x20,0x7d,0x27,0x7d,0x22,0x7d,0x28,0x7d,0x22,0xdd,0x31,0x7e};
 HdNetworkState s;
 for(size_t split=0;split<=sizeof(captured);++split){init(&s,out);check(!hd_network_feed(&s,captured,split)&&!hd_network_feed(&s,captured+split,sizeof(captured)-split)&&s.peer_lcp,"actual HD LCP frame handles every split boundary");}
 uint8_t wire[4096];uint8_t request[14];memcpy(request,hd_network_lcp_request,14);request[1]=0;
 size_t n=hd_network_encode(0xc021,request,14,wire,sizeof(wire));
 check(n==sizeof(captured)&&!memcmp(wire,captured,n),"PPP encoding matches independent capture bytes");
 init(&s,out);wire[n-2]^=1;check(hd_network_feed(&s,wire,n)!=0,"FCS corruption rejected");wire[n-2]^=1;
 memcpy(wire+n,wire,n);init(&s,out);check(!hd_network_feed(&s,wire,n*2)&&s.lcp_requests==2,"coalesced complete frames processed serially");
 init(&s,out);uint8_t abort[]={0x7e,0x7d,0x7e};check(hd_network_feed(&s,abort,3)!=0,"invalid escape termination rejected");
 init(&s,out);memset(wire,0x40,HD_NETWORK_FRAME_MAX+2);wire[0]=0x7e;check(hd_network_feed(&s,wire,HD_NETWORK_FRAME_MAX+2)!=0,"frame memory bound enforced");
}
static void diagnostic_tests(void){
 FILE *out=tmpfile();assert(out);HdNetworkState s={0};uint8_t bytes[80];
 memset(bytes,0xab,sizeof(bytes));s.frame_length=9;s.frame_active=1;
 hd_network_unexpected(out,"unexpected-client-signal",bytes,sizeof(bytes),3,&s);
 fflush(out);rewind(out);char line[1024];assert(fgets(line,sizeof(line),out));
 char *hex=strstr(line,"first-bytes=");assert(hex);hex+=strlen("first-bytes=");
 char *end=strchr(hex,'\t');
 check(end&&end-hex==128&&!strncmp(hex,"abababab",8)&&strstr(end,"truncated=yes"),
       "unexpected tunnel evidence contains exactly first64 bytes, no unbounded dump");
 check(strstr(line,"bytes=80\tclient-bytes=3\tframe-bytes=9\tframe-active=1")!=NULL,
       "stream diagnostic preserves handshake and frame context");
 fclose(out);
}
static int startup_reply(void *context){++*(unsigned*)context;return 0;}
static void startup_tests(FILE *out){
 uint8_t stream[256];memcpy(stream,"CLIENTCLIENT",12);
 size_t frame=hd_network_encode(0xc021,hd_network_lcp_request,14,stream+12,sizeof(stream)-12);
 assert(frame);HdNetworkState s;HdNetworkStartup startup;unsigned replies;
 for(size_t split=0;split<=12+frame;++split){
  init(&s,out);memset(&startup,0,sizeof(startup));replies=0;
  check(!hd_network_startup_feed(&s,&startup,stream,split,startup_reply,&replies)&&
        !hd_network_startup_feed(&s,&startup,stream+split,12+frame-split,startup_reply,&replies)&&
        replies==2&&startup.ppp_started&&s.peer_lcp,
        "repeated CLIENT and coalesced PPP preserve every split boundary");
 }
 init(&s,out);memset(&startup,0,sizeof(startup));replies=0;
 for(unsigned i=0;i<HD_NETWORK_CLIENT_MAX;++i)
  check(!hd_network_startup_feed(&s,&startup,(const uint8_t*)"CLIENT",6,startup_reply,&replies),
        "bounded startup CLIENT retransmission gets serialized reply");
 check(hd_network_startup_feed(&s,&startup,(const uint8_t*)"CLIENT",6,startup_reply,&replies)!=0&&replies==HD_NETWORK_CLIENT_MAX,
       "fifth CLIENT refused without additional reply");
 check(!hd_network_startup_feed(&s,&startup,stream+12,frame,startup_reply,&replies)&&startup.ppp_started,
       "PPP may start after maximum allowed CLIENT replies");
 check(hd_network_startup_feed(&s,&startup,(const uint8_t*)"CLIENT\x7e",7,startup_reply,&replies)!=0&&replies==HD_NETWORK_CLIENT_MAX,
       "CLIENT inside established PPP is rejected, never re-handshaken");
 init(&s,out);memset(&startup,0,sizeof(startup));replies=0;
 check(hd_network_startup_feed(&s,&startup,stream+12,frame,startup_reply,&replies)!=0&&!replies,
       "PPP before CLIENT is refused");
 check(!hd_network_startup_feed(&s,&startup,(const uint8_t*)"CLI",3,startup_reply,&replies)&&
       hd_network_startup_feed(&s,&startup,(const uint8_t*)"XNT",3,startup_reply,&replies)!=0&&!replies,
       "malformed partial CLIENT fails without reply");
}
/* Full CLI transport simulation: only tunnel controls/send/poll allowed. */
static int mode;
int ptp_transaction(ptp_session_t *session,uint16_t op,uint32_t *params,int count,uint16_t flags,uint8_t *send,uint64_t send_len,uint8_t **data,uint32_t *len,uint16_t *rc,uint32_t *reply,int *reply_count){
 assert(op==0x922b||op==0x9230||op==0x922c||op==0x922d);calls++;*rc=session->last_response=0x2001;*reply_count=0;if(data)*data=NULL;if(len)*len=0;
 if(op==0x922b){assert(flags==PTP_DP_SENDDATA&&count==3&&params[0]==3);*reply_count=3;reply[0]=reply[1]=0;reply[2]=2;if(params[1]==2){assert(!send_len);closes++;}else assert(send_len==260);}
 else if(op==0x9230){assert(flags==PTP_DP_NODATA&&count==1);if(params[0]==2){ends++;if(mode==3){*rc=session->last_response=PTP_ERROR_IO;return -1;}if(mode==4){*rc=session->last_response=0x2005;return -1;}}}
 else if(op==0x922c){assert(flags==PTP_DP_SENDDATA&&count==2&&params[0]==3&&params[1]==3);if(send_len==12&&!memcmp(send,"CLIENTSERVER",12))++client_replies;*reply_count=4;reply[0]=reply[2]=0;reply[1]=3;reply[3]=send_len;}
 else {assert(flags==PTP_DP_GETDATA_OPTIONAL&&count==2&&params[0]==3&&params[1]==3);*reply_count=3;reply[0]=reply[2]=0;reply[1]=3;polls++;
  if(mode==1)return 0;
  if(mode==2){*rc=session->last_response=0;return -1;}
  if(mode==5){*reply_count=1;return 0;}
  if(mode==6){*rc=session->last_response=PTP_ERROR_TIMEOUT;return -1;}
  uint8_t p[64],wire[2048];size_t n=0;
  int packet_poll=polls;
  if(mode==9&&polls>2)--packet_poll;
  if(mode==10&&polls>1)++packet_poll;
  if(polls==1){memcpy(wire,mode==7?"CLIXNT":"CLIENT",6);n=6;
   if(mode==10)n+=hd_network_encode(0xc021,hd_network_lcp_request,14,wire+6,sizeof(wire)-6);}
  else if(mode==9&&polls==2){memcpy(wire,"CLIENT",6);n=6;}
  else if(mode==8){memcpy(wire,"CLIENT",6);n=6;}
  else if(packet_poll==2)n=hd_network_encode(0xc021,hd_network_lcp_request,14,wire,sizeof(wire));
  else if(packet_poll==3){memcpy(p,hd_network_lcp_request,14);p[0]=2;n=hd_network_encode(0xc021,p,14,wire,sizeof(wire));}
  else if(packet_poll==4)n=hd_network_encode(0x8021,peer_ipcp,sizeof(peer_ipcp),wire,sizeof(wire));
  else if(packet_poll==5){memcpy(p,hd_network_ipcp_request,10);p[0]=2;n=hd_network_encode(0x8021,p,10,wire,sizeof(wire));}
  else if(packet_poll==6){ip_packet(p,1);n=hd_network_encode(0x21,p,40,wire,sizeof(wire));}
  else assert(0);
  *data=malloc(n);assert(*data);memcpy(*data,wire,n);*len=n;
 }
 (void)send;return 0;
}
static int callback_mode;
static int lifecycle_tick(HdNetworkState *network,void *context,int64_t tick_now){
 (void)tick_now;volatile sig_atomic_t *stop=context;
 assert(network->local_ipcp&&network->peer_ipcp&&!network->frame_length);
 if(callback_mode==1){network->callback_transport_uncertain=1;return -1;}
 if(callback_mode==2)*stop=1;
 if(callback_mode==3)now=10000;
 return 0;
}
static int budget_tick(HdNetworkState *network,void *context,int64_t tick_now){
 (void)context;(void)tick_now;
 assert(network->send_limit==HD_NETWORK_ART_SEND_MAX);
 network->sends=network->send_limit;
 return hd_network_emit(network,0xc021,hd_network_lcp_request,sizeof(hd_network_lcp_request));
}
static void run_transport(FILE *out){
 ZuneDevice dev={0};dev.device_family=ZUNE_FAMILY_PAVO;dev.mtpz_authenticated=1;volatile sig_atomic_t stop=0;
 calls=polls=closes=ends=mode=0;now=0;
 check(!hd_network_probe_run(&dev,out,&stop,NULL,NULL,0)&&polls==6&&closes==1&&ends==1,"full mocked bidirectional PPP to TCP observation and clean close");
 calls=polls=closes=ends=0;mode=1;now=0;
 check(hd_network_probe_run(&dev,out,&stop,NULL,NULL,0)!=0&&polls<=240&&closes==1&&ends==1,"idle deadline still closes tunnel");
 calls=polls=closes=ends=0;now=0;
 check(hd_network_probe_run_ex(&dev,out,&stop,NULL,NULL,NULL,0,360000)!=0&&polls==1440&&closes==1&&ends==1,"six minute observation stays bounded and closes tunnel");
 calls=0;
 check(hd_network_probe_run_ex(&dev,out,&stop,NULL,NULL,NULL,0,360001)!=0&&!calls,"overlong observation rejected before USB");
 calls=polls=closes=ends=0;mode=2;now=0;
 check(hd_network_probe_run(&dev,out,&stop,NULL,NULL,0)!=0&&!closes&&!ends,"lost transport response prevents subsequent tunnel commands");
 int uncertain=99;
 for(int test_mode=0;test_mode<=6;++test_mode){
  calls=polls=closes=ends=0;mode=test_mode;now=0;uncertain=99;
  int rc=hd_network_probe_run_ex_status(&dev,out,&stop,NULL,NULL,NULL,0,10000,&uncertain);
  check(uncertain==(test_mode==2||test_mode==3||test_mode==5||test_mode==6),"explicit uncertainty distinguishes healthy completion/refusal/deadline from lost or malformed transport");
  if(test_mode==0)check(!rc&&closes==1&&ends==1,"successful status path keeps orderly cleanup");
  if(test_mode==3)check(rc&&!closes&&ends==1,"lost mode-end response prevents stream-close request");
  if(test_mode==4)check(rc&&closes==1&&ends==1,"complete cleanup refusal allows remaining orderly close");
  if(test_mode==5||test_mode==6)check(rc&&!closes&&!ends,"malformed or local timeout response prevents follow-up commands");
 }
 for(callback_mode=1;callback_mode<=3;++callback_mode){
  calls=polls=closes=ends=mode=0;now=0;stop=0;uncertain=99;int clean=99;
  check(hd_network_probe_run_ex_lifecycle(&dev,out,&stop,NULL,lifecycle_tick,(void*)&stop,0,10000,&uncertain,&clean)!=0,"callback failure/cancel/deadline stops network loop");
  if(callback_mode==1)check(uncertain&&!clean&&!closes&&!ends,"uncertain binding callback forbids all cleanup PTP transactions");
  else check(!uncertain&&clean&&closes==1&&ends==1,"healthy callback signal/deadline completes tunnel teardown for caller rollback");
 }
 stop=0;
 {
  FILE *log=tmpfile();assert(log);calls=polls=closes=ends=mode=0;now=0;int clean=0;
  check(hd_network_probe_run_ex_lifecycle(&dev,log,&stop,callback,budget_tick,NULL,0,10000,&uncertain,&clean)!=0&&clean&&!uncertain,
        "outbound cap fails observation while preserving clean healthy teardown");
  fflush(log);rewind(log);char report[8192];size_t used=fread(report,1,sizeof(report)-1,log);report[used]=0;
  check(strstr(report,"status\toutbound-packet-budget-exhausted")&&
        strstr(report,"outbound-budget\tsends=8192\tlimit=8192\texhausted=yes")&&
        !strstr(report,"invalid-or-unsupported-network-packet"),
        "budget failure reports its real reason and counts without blaming valid input");
  fclose(log);
 }
 stop=0;
 for(int test_mode=7;test_mode<=8;++test_mode){
  FILE *log=tmpfile();assert(log);calls=polls=closes=ends=0;mode=test_mode;now=0;
  check(hd_network_probe_run(&dev,log,&stop,NULL,NULL,0)!=0&&closes==1&&ends==1,
        "unexpected handshake and post-handshake text remain rejected with clean close");
  fflush(log);rewind(log);char report[8192];size_t used=fread(report,1,sizeof(report)-1,log);report[used]=0;
  check(strstr(report,"network-stream-diagnostic\tstage=unexpected-client-signal")!=NULL&&
        strstr(report,test_mode==7?"first-bytes=434c49584e54":"first-bytes=434c49454e54")!=NULL,
        "both stream rejection paths include bounded actual bytes");
  fclose(log);
 }
 for(int test_mode=9;test_mode<=10;++test_mode){
  calls=polls=closes=ends=client_replies=0;mode=test_mode;now=0;
  check(!hd_network_probe_run(&dev,out,&stop,NULL,NULL,0)&&closes==1&&ends==1&&
        client_replies==(test_mode==9?2:1)&&polls==(test_mode==9?7:5),
        "observed repeated CLIENT or coalesced CLIENT/PPP completes full transport negotiation");
 }
 stop=0;
 calls=0;dev.device_family=ZUNE_FAMILY_DRACO;check(hd_network_probe_run(&dev,out,&stop,NULL,NULL,0)!=0&&!calls,"non-HD refused before access");
 dev.device_family=ZUNE_FAMILY_PAVO;dev.mtpz_authenticated=0;check(hd_network_probe_run(&dev,out,&stop,NULL,NULL,0)!=0&&!calls,"unauthenticated refused before access");
 dev.mtpz_authenticated=1;stop=1;check(hd_network_probe_run(&dev,out,&stop,NULL,NULL,0)!=0&&!calls,"cancel before start refuses access");
}
static int image_emit(void *ctx,const uint8_t *p,size_t n){return hd_network_emit(ctx,0x21,p,n);}
static int image_input(HdNetworkState *s,void *ctx,const uint8_t *p,size_t n){(void)s;return hd_image_server_packet(ctx,p,n);}
static void observed_ack_budget_test(FILE *out){
 /* Exact failed PPP frame from album-resume-clean.txt: valid packet, not an
  * unsupported protocol. Replay an outstanding HTTP segment at its seq/ACK. */
 static const uint8_t wire[]={0x7e,0x21,0x45,0x00,0x00,0x28,0x03,0x89,0x40,0x00,0x80,0x06,0x07,0x2d,0xc0,0xa8,0x37,0x65,0xc0,0xa8,0x37,0x64,0xc0,0x62,0x00,0x50,0x0e,0xf9,0x8c,0x1e,0x51,0xce,0x1b,0x59,0x50,0x10,0x81,0xd0,0x74,0xf8,0x00,0x00,0x57,0x4f,0x7e};
 uint8_t body[2000]={0};body[0]=255;body[1]=216;body[1998]=255;body[1999]=217;
 for(unsigned mode=0;mode<3;mode++){
  HdNetworkState network;init(&network,out);network.local_ipcp=network.peer_ipcp=network.local_lcp=network.peer_lcp=1;
  HdImageServer image;
  assert(!hd_image_server_init(&image,0xc0a83764,"01234567-89ab-cdef-0123-456789abcdef",body,sizeof(body),image_emit,&network));
  HdImageFlow *flow=&image.flow[0];flow->active=flow->established=flow->pending=flow->response=flow->image=1;
  flow->peer_ip=0xc0a83765;flow->peer_port=49250;flow->local_port=80;
  flow->receive_next=0x0ef98c1e;flow->send_next=flow->pending_end=0x51ce1b59;
  flow->mss=1000;flow->body=body;flow->body_n=sizeof(body);
  network.ipv4=image_input;network.ipv4_context=&image;
  network.sends=mode==2?HD_NETWORK_ART_SEND_MAX:HD_NETWORK_SEND_MAX;
  network.send_limit=mode?HD_NETWORK_ART_SEND_MAX:HD_NETWORK_SEND_MAX;
  int rc=hd_network_feed(&network,wire,sizeof(wire));
  if(mode==1){
   check(!rc&&!network.send_budget_exhausted&&network.sends==513&&emissions==1,
         "exact observed valid ACK proceeds past legacy512 with bounded artwork budget");
   check(emitted_len==1040&&!hdis_transport_sum(emitted,emitted+20,emitted_len-20,6),
         "observed ACK emits correctly checksummed next1000 image bytes");
  }else check(rc&&network.send_budget_exhausted&&!emissions,
               mode==0?"observed valid ACK reproduces old512 send-cap failure without malformed packet":"artwork8192 hard cap still refuses output explicitly");
  check(!image.image_completed&&!image.distinct_assets_completed,
        "budget handling never fabricates completed image evidence");
 }
}

int main(void){FILE *out=tmpfile();assert(out);state_tests(out);observed_ack_budget_test(out);framing_tests(out);diagnostic_tests();startup_tests(out);run_transport(out);fclose(out);printf("HD network probe: %d checks passed; no USB\n",checks);return 0;}
