/* Experimental native PPP gate. Independent implementation from albumsync
 * frames3182..3286; standard PPP/IP checks. XuneSyncLibrary c2a6eeb inspected
 * for sequence/proxy leads only. No third-party implementation copied.
 * No host interfaces, sockets, routing, artist metadata or catalog writes. */
#ifndef ZUNETOOL_HD_NETWORK_PROBE_H
#define ZUNETOOL_HD_NETWORK_PROBE_H
#include "hd_tunnel_probe.h"
#include "hd_cli_teardown.h"
#define HD_NETWORK_FRAME_MAX 2048
#define HD_NETWORK_SEND_MAX 512
/* Six 128 KiB assets plus bounded retransmission/metadata need more than 512
 * segments. Artwork callbacks retain a hard packet cap and existing deadline. */
#define HD_NETWORK_ART_SEND_MAX 8192

typedef struct HdNetworkState HdNetworkState;
typedef int (*HdNetworkSend)(void *, uint16_t, const uint8_t *, size_t);
typedef int (*HdNetworkIPv4)(HdNetworkState *, void *, const uint8_t *, size_t);
typedef int (*HdNetworkTick)(HdNetworkState *, void *, int64_t);
struct HdNetworkState {
    FILE *out;
    HdNetworkSend send;
    void *send_context;
    HdNetworkIPv4 ipv4;
    HdNetworkTick tick;
    void *ipv4_context;
    int dns_to_host;
    int peer_lcp, local_lcp, host_lcp_sent, peer_ipcp, local_ipcp, host_ipcp_sent;
    unsigned lcp_requests, ipcp_requests, sends, ipv4_packets, tcp_packets, udp_packets;
    unsigned send_limit;
    int send_budget_exhausted;
    int callback_complete;
    int callback_transport_uncertain;
    uint8_t frame[HD_NETWORK_FRAME_MAX];
    size_t frame_length;
    int frame_active, frame_escaped;
};
static const uint8_t hd_network_host_ip[4] = {192,168,55,100};
static const uint8_t hd_network_peer_ip[4] = {192,168,55,101};
static const uint8_t hd_network_lcp_request[] = {1,1,0,14,2,6,0,0,0,0,7,2,8,2};
static const uint8_t hd_network_ipcp_request[] = {1,2,0,10,3,6,192,168,55,100};
static uint16_t hd_network_be16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]<<8)|p[1]; }
/* This diagnostic receives only the authenticated tunnel's 0x922d data,
 * never MTPZ/authentication packets. Keep malformed stream evidence bounded. */
static void hd_network_unexpected(FILE *out,const char *stage,const uint8_t *data,
                                  size_t length,size_t client,const HdNetworkState *state) {
    size_t shown=data?(length<64?length:64):0;
    fprintf(out,"network-stream-diagnostic\tstage=%s\tbytes=%zu\tclient-bytes=%zu"
                "\tframe-bytes=%zu\tframe-active=%d\tframe-escaped=%d\tfirst-bytes=",
            stage,length,client,state->frame_length,state->frame_active,state->frame_escaped);
    for(size_t i=0;i<shown;++i)fprintf(out,"%02x",data[i]);
    fprintf(out,"\ttruncated=%s\n",shown<length?"yes":"no");
}
static uint16_t hd_network_checksum(const uint8_t *p, size_t n) {
    uint32_t sum=0;
    for (size_t i=0;i<n;i+=2) sum += (uint32_t)p[i]<<8 | (i+1<n?p[i+1]:0);
    while(sum>>16) sum=(sum&0xffff)+(sum>>16);
    return (uint16_t)~sum;
}
static int hd_network_emit(HdNetworkState *s, uint16_t protocol, const uint8_t *p, size_t n) {
    if (!s || !s->send || !p || !n || n>HD_NETWORK_FRAME_MAX-6) return -1;
    unsigned limit=s->send_limit?s->send_limit:HD_NETWORK_SEND_MAX;
    if(limit>HD_NETWORK_ART_SEND_MAX)limit=HD_NETWORK_ART_SEND_MAX;
    if(s->sends>=limit){s->send_budget_exhausted=1;return -1;}
    if(protocol!=0xc021 && protocol!=0x8021 && protocol!=0x80fd && protocol!=0x21) return -1;
    if(protocol==0x21 && (!s->local_ipcp || !s->peer_ipcp)) return -1;
    ++s->sends;
    return s->send(s->send_context,protocol,p,n);
}
static int hd_network_reply(HdNetworkState *s,uint16_t proto,const uint8_t *p,size_t n,uint8_t code) {
    uint8_t reply[HD_NETWORK_FRAME_MAX];
    if(n<4 || n>sizeof(reply)) return -1;
    memcpy(reply,p,n);reply[0]=code;
    return hd_network_emit(s,proto,reply,n);
}
static int hd_network_ip_summary(HdNetworkState *s,const uint8_t *p,size_t n) {
    if(!s->local_ipcp || !s->peer_ipcp || n<20 || (p[0]>>4)!=4) return -1;
    size_t header=(p[0]&15)*4u;
    if(header<20 || header>n || hd_network_be16(p+2)!=n ||
       hd_network_checksum(p,header)!=0 || (hd_network_be16(p+6)&0xbfff) || !p[8] ||
       memcmp(p+12,hd_network_peer_ip,4)) return -1;
    const uint8_t *t=p+header;size_t remaining=n-header;
    ++s->ipv4_packets;
    if(p[9]==6) {
        if(remaining<20 || (t[12]>>4)*4u<20 || (t[12]>>4)*4u>remaining) return -1;
        ++s->tcp_packets;
        fprintf(s->out,"ipv4-tcp\tsource-port=%u\tdestination-port=%u\tflags=0x%02x\tpayload-bytes=%zu\n",
                hd_network_be16(t),hd_network_be16(t+2),t[13],remaining-(t[12]>>4)*4u);
    } else if(p[9]==17) {
        if(remaining<8 || hd_network_be16(t+4)!=remaining) return -1;
        ++s->udp_packets;
        fprintf(s->out,"ipv4-udp\tsource-port=%u\tdestination-port=%u\tpayload-bytes=%zu\n",
                hd_network_be16(t),hd_network_be16(t+2),remaining-8);
    } else fprintf(s->out,"ipv4-other\tprotocol=%u\tbytes=%zu\n",p[9],n);
    if(s->ipv4) {
        int result=s->ipv4(s,s->ipv4_context,p,n);
        if(result<0) return -1;
        if(result>0) s->callback_complete=1;
    }
    return 0;
}
static int hd_network_packet(HdNetworkState *s,uint16_t protocol,const uint8_t *p,size_t n) {
    if(!s || !p || n>HD_NETWORK_FRAME_MAX-6) return -1;
    if(protocol==0x21) return hd_network_ip_summary(s,p,n);
    if(n<4 || hd_network_be16(p+2)!=n) return -1;
    if(protocol==0xc021) {
        if(p[0]==1) {
            if(++s->lcp_requests>8 || n!=sizeof(hd_network_lcp_request) ||
               memcmp(p+2,hd_network_lcp_request+2,n-2)) return -1;
            s->peer_lcp=0;
            if(hd_network_reply(s,protocol,p,n,2)) return -1;
            s->peer_lcp=1;
            if(!s->host_lcp_sent) {
                if(hd_network_emit(s,protocol,hd_network_lcp_request,sizeof(hd_network_lcp_request))) return -1;
                s->host_lcp_sent=1;
            }
        } else if(p[0]==2) {
            if(!s->host_lcp_sent || n!=sizeof(hd_network_lcp_request) ||
               memcmp(p+1,hd_network_lcp_request+1,n-1)) return -1;
            s->local_lcp=1;
        } else if(p[0]==9 && s->local_lcp && s->peer_lcp && n>=8) {
            if(hd_network_reply(s,protocol,p,n,10)) return -1;
        } else return -1;
        if(s->local_lcp && s->peer_lcp && !s->host_ipcp_sent) {
            if(hd_network_emit(s,0x8021,hd_network_ipcp_request,sizeof(hd_network_ipcp_request))) return -1;
            s->host_ipcp_sent=1;
            fprintf(s->out,"lcp\tbidirectional-complete; no payload compression\n");
        }
        return 0;
    }
    if(!s->local_lcp || !s->peer_lcp) return -1;
    if(protocol==0x80fd) {
        if(p[0]==1) {
            for(size_t i=4;i<n;) { if(n-i<2 || p[i+1]<2 || p[i+1]>n-i) return -1; i+=p[i+1]; }
            /* Reject every offered compression option. Empty options means
             * no compression, safe to ACK. We never propose compression. */
            if(hd_network_reply(s,protocol,p,n,n==4?2:4)) return -1;
            fprintf(s->out,"ccp\t%s\n",n==4?"empty-options-acknowledged":"compression-options-rejected");
            return 0;
        }
        if(p[0]==5) return hd_network_reply(s,protocol,p,n,6);
        return -1;
    }
    if(protocol!=0x8021) return -1;
    if(p[0]==2) {
        if(!s->host_ipcp_sent || n!=sizeof(hd_network_ipcp_request) ||
           memcmp(p+1,hd_network_ipcp_request+1,n-1)) return -1;
        s->local_ipcp=1;
    } else if(p[0]==1) {
        if(++s->ipcp_requests>12) return -1;
        uint8_t rejected[256]={4,p[1],0,4},nak[32]={3,p[1],0,4};size_t rn=4,nn=4;
        unsigned seen[256]={0};int has_ip=0;
        s->peer_ipcp=0;
        for(size_t i=4;i<n;) {
            if(n-i<2 || p[i+1]<2 || p[i+1]>n-i || seen[p[i]]++) return -1;
            size_t len=p[i+1];uint8_t type=p[i];const uint8_t *wanted=NULL;
            uint8_t dns[4]={127,0,0,1};if(s->dns_to_host) memcpy(dns,hd_network_host_ip,4);
            if(type==3 && len==6) { has_ip=1;wanted=hd_network_peer_ip; }
            else if(type==129 && len==6) wanted=dns;
            else if((type==130 || type==131 || type==132) && len==6 && !p[i+2] && !p[i+3] && !p[i+4] && !p[i+5]) { /* accepted unused zero */ }
            else { if(rn+len>sizeof(rejected)) return -1;memcpy(rejected+rn,p+i,len);rn+=len; }
            if(wanted && memcmp(p+i+2,wanted,4)) {
                if(nn+6>sizeof(nak)) return -1;
                nak[nn++]=type;nak[nn++]=6;memcpy(nak+nn,wanted,4);nn+=4;
            }
            i+=len;
        }
        if(!has_ip) { nak[nn++]=3;nak[nn++]=6;memcpy(nak+nn,hd_network_peer_ip,4);nn+=4; }
        if(rn>4) { rejected[2]=rn>>8;rejected[3]=rn;return hd_network_emit(s,protocol,rejected,rn); }
        if(nn>4) { nak[2]=nn>>8;nak[3]=nn;return hd_network_emit(s,protocol,nak,nn); }
        if(hd_network_reply(s,protocol,p,n,2)) return -1;
        s->peer_ipcp=1;
    } else return -1; /* No guessing on NAK/Reject of our fixed host identity. */
    if(s->local_ipcp && s->peer_ipcp) fprintf(s->out,"ipcp\tbidirectional-complete\n");
    return 0;
}
static int hd_network_frame(HdNetworkState *s) {
    uint8_t *p=s->frame;size_t n=s->frame_length,offset=0;
    if(n<4 || hd_tunnel_fcs(p,n)!=0xf0b8) return -1;
    n-=2;
    if(n>=2 && p[0]==0xff && p[1]==3) offset=2;
    else if(!s->local_lcp || !s->peer_lcp) return -1;
    if(offset>=n) return -1;
    uint16_t proto=p[offset++];
    if(!(proto&1)) { if(offset>=n) return -1;proto=(uint16_t)(proto<<8)|p[offset++]; }
    else if(!s->local_lcp || !s->peer_lcp) return -1;
    return hd_network_packet(s,proto,p+offset,n-offset);
}
static int hd_network_feed(HdNetworkState *s,const uint8_t *p,size_t n) {
    if(!s || (!p&&n) || n>65536) return -1;
    for(size_t i=0;i<n;++i) {
        uint8_t c=p[i];
        if(c==0x7e) {
            if(s->frame_escaped) return -1;
            if(s->frame_active && s->frame_length && hd_network_frame(s)) return -1;
            s->frame_active=1;s->frame_length=0;continue;
        }
        if(!s->frame_active) return -1;
        if(s->frame_escaped) {c^=0x20;s->frame_escaped=0;}
        else if(c==0x7d) {s->frame_escaped=1;continue;}
        if(s->frame_length==sizeof(s->frame)) return -1;
        s->frame[s->frame_length++]=c;
    }
    return 0;
}
/* Pavo was observed repeating CLIENT after CLIENTSERVER, before any PPP.
 * Poll payloads are a byte stream: preserve partial tokens and pass any first
 * HDLC flag plus its remainder intact to the existing strict PPP decoder. */
typedef struct {
    size_t client_bytes;
    unsigned replies;
    int ppp_started;
} HdNetworkStartup;
#define HD_NETWORK_CLIENT_MAX 4
static int hd_network_startup_feed(HdNetworkState *s,HdNetworkStartup *startup,
                                   const uint8_t *data,size_t length,
                                   int (*reply)(void *),void *context) {
    if(!s||!startup||(!data&&length)||length>65536||!reply)return -1;
    if(startup->ppp_started)return hd_network_feed(s,data,length);
    for(size_t i=0;i<length;++i){
        if(data[i]==0x7e && !startup->client_bytes && startup->replies){
            startup->ppp_started=1;
            return hd_network_feed(s,data+i,length-i);
        }
        if(startup->replies>=HD_NETWORK_CLIENT_MAX ||
           data[i]!=(uint8_t)"CLIENT"[startup->client_bytes])return -1;
        if(++startup->client_bytes==6){
            if(reply(context))return -1;
            startup->client_bytes=0;
            ++startup->replies;
            fprintf(s->out,"client-handshake\tcomplete\treply=%u\n",startup->replies);
        }
    }
    return 0;
}
static int hd_network_client_reply(void *context){
    HdTunnelProbe *p=context;uint32_t params[2]={3,3};
    uint8_t text[]="CLIENTSERVER";
    return hd_tunnel_transaction(p,0x922c,params,2,PTP_DP_SENDDATA,text,12,NULL,NULL,0);
}
/* Always send full address/control and protocol fields. PPP compression is
 * optional even after PFC/ACFC negotiated. Escape all controls conservatively. */
static size_t hd_network_encode(uint16_t proto,const uint8_t *p,size_t n,uint8_t *out,size_t cap) {
    uint8_t plain[HD_NETWORK_FRAME_MAX];
    if(!p || !out || n>sizeof(plain)-6 || cap<2) return 0;
    plain[0]=0xff;plain[1]=3;plain[2]=proto>>8;plain[3]=proto;memcpy(plain+4,p,n);
    uint16_t fcs=(uint16_t)~hd_tunnel_fcs(plain,n+4);plain[n+4]=fcs;plain[n+5]=fcs>>8;
    size_t used=0;out[used++]=0x7e;
    for(size_t i=0;i<n+6;++i) {
        uint8_t c=plain[i];int escape=c<0x20||c==0x7d||c==0x7e;
        if(used+(escape?2u:1u)+1>cap) return 0;
        if(escape){out[used++]=0x7d;c^=0x20;}out[used++]=c;
    }
    out[used++]=0x7e;return used;
}
static int hd_network_usb_send(void *ctx,uint16_t proto,const uint8_t *p,size_t n) {
    HdTunnelProbe *probe=ctx;uint8_t wire[2*HD_NETWORK_FRAME_MAX+2];
    size_t length=hd_network_encode(proto,p,n,wire,sizeof(wire));uint32_t params[2]={3,3};
    if(!length) return -1;
    int result=hd_tunnel_transaction(probe,0x922c,params,2,PTP_DP_SENDDATA,wire,length,NULL,NULL,0);
    if(result && (hd_cli_transaction_uncertain(result,probe->failed_response) ||
                 !strcmp(probe->failure,"unexpected-response-parameters"))) probe->wounded=1;
    return result;
}
/* Optional callback may serve bounded IPv4 requests entirely in-process. A
 * return1 requests completion;0 continues;-1 fails. No callback in plain probe.
 * Deadline bounds scheduling, not an in-flight USB transaction. */
static int hd_network_probe_run_detail(ZuneDevice *dev,FILE *out,volatile sig_atomic_t *stop,
                               HdNetworkIPv4 callback,HdNetworkTick tick,void *context,
                               int dns_to_host,unsigned duration_ms,int *transport_uncertain,int *network_clean,const char **failure_out) {
    if(failure_out)*failure_out="invalid-input";
    if(network_clean)*network_clean=0;
    if(transport_uncertain)*transport_uncertain=0;
    if(!dev||!out||duration_ms<1000||duration_ms>360000) return -1;
    HdTunnelProbe p={dev,out,stop,0,0,0,"not-started"};
    HdNetworkState state={0};state.out=out;state.send=hd_network_usb_send;state.send_context=&p;
    state.ipv4=callback;state.tick=tick;state.ipv4_context=context;state.dns_to_host=dns_to_host;
    state.send_limit=callback?HD_NETWORK_ART_SEND_MAX:HD_NETWORK_SEND_MAX;
    int opened=0,passed=0,cleanup=1;uint8_t *data=NULL;uint32_t length=0;
    fprintf(out,"hd-network-probe-v1\nscope\texperimental PPP/IP observation; optional serialized callback\n");
    if(dev->device_family!=ZUNE_FAMILY_PAVO){p.failure="not-zune-hd";goto done;}
    if(!dev->mtpz_authenticated){p.failure="not-authenticated";goto done;}
    int64_t start=HD_TUNNEL_NOW_MS();if(start<0){p.failure="clock-failed";goto done;}
    p.deadline=start+duration_ms;if(!hd_tunnel_ready(&p))goto done;
    uint8_t control[260]={2,0x1d,0xc3};uint32_t params[3]={3,1,0};opened=1;
    if(hd_tunnel_transaction(&p,0x922b,params,3,PTP_DP_SENDDATA,control,sizeof(control),NULL,NULL,0))goto done;
    params[0]=1;if(hd_tunnel_transaction(&p,0x9230,params,1,PTP_DP_NODATA,NULL,0,NULL,NULL,0))goto done;
    HdNetworkStartup startup={0};
    for(unsigned poll=0;poll<duration_ms/250+1;++poll){
        params[0]=params[1]=3;
        if(hd_tunnel_transaction(&p,0x922d,params,2,PTP_DP_GETDATA_OPTIONAL,NULL,0,&data,&length,0))goto done;
        if(!hd_tunnel_ready(&p))goto done;
        if(length>65536||(length&&!data)){p.failure="invalid-poll-data";goto done;}
        if(hd_network_startup_feed(&state,&startup,data,length,hd_network_client_reply,&p)){
            const char *stage=state.send_budget_exhausted?"outbound-packet-budget-exhausted":
                startup.ppp_started?"invalid-or-unsupported-network-packet":"unexpected-client-signal";
            hd_network_unexpected(out,stage,data,length,startup.client_bytes,&state);
            if(!p.failed_response && !p.wounded)p.failure=stage;
            goto done;
        }
        free(data);data=NULL;
        if(!hd_tunnel_ready(&p))goto done;
        if(state.tick && state.local_ipcp && state.peer_ipcp){
            int tick_result=state.tick(&state,state.ipv4_context,HD_TUNNEL_NOW_MS());
            if(tick_result<0){p.failure=state.send_budget_exhausted?"outbound-packet-budget-exhausted":"network-callback-failed";goto done;}
            if(tick_result>0)state.callback_complete=1;
        }
        if(!state.frame_length&&!state.frame_escaped&&state.local_ipcp&&state.peer_ipcp&&
           (callback?state.callback_complete:(state.tcp_packets||state.ipv4_packets>=8))){passed=1;break;}
        if(!hd_tunnel_ready(&p))goto done;
        HD_TUNNEL_PAUSE();
    }
    if(!passed){p.failure="observation-incomplete";}
 done:
    free(data);
    if(state.callback_transport_uncertain)p.wounded=1;
    const char *failure=p.failure;uint16_t response=p.failed_response;
    if(p.failed_response && hd_cli_transaction_uncertain(-1,p.failed_response))p.wounded=1;
    if(!strcmp(p.failure,"unexpected-response-parameters"))p.wounded=1;
    if(opened){
        if(p.wounded||ZUNE_FLAG_GET(dev->disconnecting)||ZUNE_FLAG_GET(dev->cancel_requested)){cleanup=0;fprintf(out,"cleanup\tnot-sent; uncertain transport\n");}
        else {uint32_t end=2,close[3]={3,2,0};
            if(hd_tunnel_transaction(&p,0x9230,&end,1,PTP_DP_NODATA,NULL,0,NULL,NULL,1))cleanup=0;
            if(p.failed_response && hd_cli_transaction_uncertain(-1,p.failed_response))p.wounded=1;
            if(!strcmp(p.failure,"unexpected-response-parameters"))p.wounded=1;
            if(p.wounded||hd_tunnel_transaction(&p,0x922b,close,3,PTP_DP_SENDDATA,NULL,0,NULL,NULL,1))cleanup=0;
            fprintf(out,"cleanup\t%s\n",cleanup?"mode-ended-and-stream-closed":"failed");
            if(!cleanup)fprintf(out,"cleanup-response\t0x%04X\n",p.failed_response);}
    }
    if(network_clean)*network_clean=opened&&cleanup;
    if(transport_uncertain && (p.wounded || ZUNE_FLAG_GET(dev->disconnecting) || ZUNE_FLAG_GET(dev->cancel_requested) ||
       (p.failed_response && hd_cli_transaction_uncertain(-1,p.failed_response)) ||
       !strcmp(p.failure,"unexpected-response-parameters"))) *transport_uncertain=1;
    fprintf(out,"observed\tipv4=%u\ttcp=%u\tudp=%u\n",state.ipv4_packets,state.tcp_packets,state.udp_packets);
    fprintf(out,"outbound-budget\tsends=%u\tlimit=%u\texhausted=%s\n",state.sends,state.send_limit,state.send_budget_exhausted?"yes":"no");
    fprintf(out,"status\t%s\n",passed&&cleanup?"complete":passed?"cleanup-failed":failure);
    if(!passed)fprintf(out,"response\t0x%04X\n",response);
    if(failure_out)*failure_out=passed&&cleanup?"complete":passed?"cleanup-failed":failure;
    return fflush(out)||ferror(out)||!passed||!cleanup?-1:0;
}
static int hd_network_probe_run_ex_lifecycle(ZuneDevice *dev,FILE *out,volatile sig_atomic_t *stop,
                               HdNetworkIPv4 callback,HdNetworkTick tick,void *context,
                               int dns_to_host,unsigned duration_ms,int *transport_uncertain,int *network_clean) {
    return hd_network_probe_run_detail(dev,out,stop,callback,tick,context,dns_to_host,
        duration_ms,transport_uncertain,network_clean,NULL);
}
static int hd_network_probe_run_ex_status(ZuneDevice *dev,FILE *out,volatile sig_atomic_t *stop,
                               HdNetworkIPv4 callback,HdNetworkTick tick,void *context,
                               int dns_to_host,unsigned duration_ms,int *transport_uncertain) {
    return hd_network_probe_run_ex_lifecycle(dev,out,stop,callback,tick,context,dns_to_host,duration_ms,transport_uncertain,NULL);
}
static int hd_network_probe_run_ex(ZuneDevice *dev,FILE *out,volatile sig_atomic_t *stop,
                               HdNetworkIPv4 callback,HdNetworkTick tick,void *context,
                               int dns_to_host,unsigned duration_ms) {
    return hd_network_probe_run_ex_status(dev,out,stop,callback,tick,context,dns_to_host,duration_ms,NULL);
}
static inline int hd_network_probe_run(ZuneDevice *dev,FILE *out,volatile sig_atomic_t *stop,
                               HdNetworkIPv4 callback,void *context,int dns_to_host) {
    return hd_network_probe_run_ex(dev,out,stop,callback,NULL,context,dns_to_host,60000);
}
#endif
