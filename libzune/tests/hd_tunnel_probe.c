#include <stdint.h>
#include <stddef.h>
static int64_t fake_now(void);
static void fake_pause(void);
#define HD_TUNNEL_NOW_MS fake_now
#define HD_TUNNEL_PAUSE fake_pause
#include "../tools/hd_tunnel_probe.h"
#include <assert.h>
static const uint8_t captured_request[] = {
    0x7e,0xff,0x7d,0x23,0xc0,0x21,0x7d,0x21,0x7d,0x20,0x7d,0x20,0x7d,0x2e,
    0x7d,0x22,0x7d,0x26,0x7d,0x20,0x7d,0x20,0x7d,0x20,0x7d,0x20,
    0x7d,0x27,0x7d,0x22,0x7d,0x28,0x7d,0x22,0xdd,0x31,0x7e};
static const uint8_t captured_ack[] = {
    0x7e,0xff,0x7d,0x23,0xc0,0x21,0x7d,0x22,0x7d,0x20,0x7d,0x20,0x7d,0x2e,
    0x7d,0x22,0x7d,0x26,0x7d,0x20,0x7d,0x20,0x7d,0x20,0x7d,0x20,
    0x7d,0x27,0x7d,0x22,0x7d,0x28,0x7d,0x22,0xe3,0xb2,0x7e};
enum Scenario { NORMAL, IDLE_FIRST, SPLIT_CLIENT, SPLIT_FRAME, COALESCED,
    NO_CLIENT, BAD_CLIENT, BAD_FCS, PARTIAL_FOREVER, LARGE_POLL, MISSING_DATA,
    OPEN_REFUSED, MODE_REFUSED, POLL_REFUSED, SEND_REFUSED, ACK_REFUSED,
    LOST_OPEN, LOST_POLL, BAD_OPEN_PARAMS, BAD_POLL_PARAMS, SHORT_SEND,
    END_REFUSED, CLOSE_REFUSED, LOST_END, INTERRUPT, DEVICE_CANCEL, DISCONNECT,
    DEADLINE, CLOCK_ERROR, STALLED_CLOCK };
static enum Scenario scenario;
static int64_t now;
static int calls, polls, sends, ends, closes, checks;
static ZuneDevice device;
static volatile sig_atomic_t stop;
static int64_t fake_now(void) { return scenario == CLOCK_ERROR ? -1 : now; }
static void fake_pause(void) {
    if (scenario != STALLED_CLOCK) now += scenario == DEADLINE ? 20001 : 250;
}
static void check(int ok, const char *label) {
    if (!ok) { fprintf(stderr,"FAIL %s\n",label); exit(1); }
    printf("PASS %s\n",label); checks++;
}
static void payload(uint8_t **out, uint32_t *len, const uint8_t *p, size_t n) {
    *out = malloc(n ? n : 1); assert(*out); *len = n;
    if (n) memcpy(*out,p,n);
}
int ptp_transaction(ptp_session_t *s, uint16_t op, uint32_t *params,
        int count, uint16_t flags, uint8_t *send, uint64_t send_len,
        uint8_t **data, uint32_t *len, uint16_t *rc,
        uint32_t *reply, int *reply_count) {
    assert(op == 0x922b || op == 0x9230 || op == 0x922c || op == 0x922d);
    assert(!device.disconnecting && !device.cancel_requested);
    calls++; s->last_response = *rc = PTP_RC_OK;
    *reply_count = 0;
    if (data) *data = NULL;
    if (len) *len = 0;
    if (op == 0x922b) {
        assert(count == 3 && params[0] == 3 && params[2] == 0 && flags == PTP_DP_SENDDATA);
        *reply_count = 3; reply[0] = reply[1] = 0; reply[2] = 2;
        if (params[1] == 1) {
            assert(send_len == 260 && send && send[0] == 2 && send[1] == 0x1d && send[2] == 0xc3);
            for (unsigned i = 3; i < 260; ++i) assert(send[i] == 0);
            if (scenario == OPEN_REFUSED) goto refused;
            if (scenario == LOST_OPEN) goto lost;
            if (scenario == BAD_OPEN_PARAMS) reply[2] = 9;
        } else {
            assert(params[1] == 2 && !send_len && !send); closes++;
            if (scenario == CLOSE_REFUSED) goto refused;
        }
    } else if (op == 0x9230) {
        assert(count == 1 && flags == PTP_DP_NODATA && !send && !send_len);
        if (params[0] == 1) { if (scenario == MODE_REFUSED) goto refused; }
        else {
            assert(params[0] == 2); ends++;
            if (scenario == END_REFUSED) goto refused;
            if (scenario == LOST_END) goto lost;
        }
    } else if (op == 0x922c) {
        assert(count == 2 && params[0] == 3 && params[1] == 3 && flags == PTP_DP_SENDDATA);
        *reply_count = 4; reply[0] = reply[2] = 0; reply[1] = 3; reply[3] = send_len;
        sends++;
        if (sends == 1) {
            assert(send_len == 12 && !memcmp(send,"CLIENTSERVER",12));
            if (scenario == SEND_REFUSED) goto refused;
            if (scenario == SHORT_SEND) --reply[3];
        } else {
            assert(sends == 2 && send_len == sizeof(captured_ack) && !memcmp(send,captured_ack,send_len));
            if (scenario == ACK_REFUSED) goto refused;
        }
    } else {
        assert(count == 2 && params[0] == 3 && params[1] == 3 && flags == PTP_DP_GETDATA_OPTIONAL && !send && !send_len);
        *reply_count = 3; reply[0] = reply[2] = 0; reply[1] = 3; polls++;
        if (scenario == POLL_REFUSED) goto refused;
        if (scenario == LOST_POLL) goto lost;
        if (scenario == BAD_POLL_PARAMS) reply[0] = 2;
        if (scenario == INTERRUPT) { stop = 1; return 0; }
        if (scenario == DEVICE_CANCEL) { device.cancel_requested = 1; return 0; }
        if (scenario == DISCONNECT) { device.disconnecting = 1; return 0; }
        if (scenario == LARGE_POLL) { *len = HD_TUNNEL_READ_MAX+1; *data = calloc(1,*len); return 0; }
        if (scenario == MISSING_DATA) { *len = 6; return 0; }
        if (scenario == NO_CLIENT || scenario == DEADLINE || scenario == STALLED_CLOCK ||
            (scenario == IDLE_FIRST && polls == 1)) return 0;
        if (!sends) {
            if (scenario == BAD_CLIENT) payload(data,len,(uint8_t*)"CL!ENT",6);
            else if (scenario == SPLIT_CLIENT) payload(data,len,(uint8_t*)(polls == 1 ? "CLI" : "ENT"),3);
            else payload(data,len,(uint8_t*)"CLIENT",6);
        } else {
            if (scenario == SPLIT_FRAME) {
                if (polls == 2) payload(data,len,captured_request,3);
                else payload(data,len,captured_request+3,sizeof(captured_request)-3);
            } else if (scenario == PARTIAL_FOREVER) {
                if (polls == 2) payload(data,len,captured_request,3);
            } else if (scenario == COALESCED) {
                *len = 2*sizeof(captured_request); *data = malloc(*len); assert(*data);
                memcpy(*data,captured_request,sizeof(captured_request));
                memcpy(*data+sizeof(captured_request),captured_request,sizeof(captured_request));
            } else {
                payload(data,len,captured_request,sizeof(captured_request));
                if (scenario == BAD_FCS) (*data)[34] ^= 1;
            }
        }
    }
    return 0;
refused:
    s->last_response = *rc = 0x2005; return -1;
lost:
    s->last_response = *rc = 0; return -1;
}
static void reset(enum Scenario next) {
    memset(&device,0,sizeof(device)); device.device_family = ZUNE_FAMILY_PAVO;
    device.mtpz_authenticated = 1;
    scenario = next; calls = polls = sends = ends = closes = 0; now = 100; stop = 0;
}
static char *run(int ack, int success, const char *marker) {
    char *text = NULL; size_t len = 0; FILE *out = open_memstream(&text,&len); assert(out);
    int rc = hd_tunnel_probe_report(&device,ack,out,&stop); fclose(out);
    check((rc == 0) == success && strstr(text,marker),marker);
    if (!success) check(!strstr(text,"status\tcomplete"),"failed gate never reports complete");
    return text;
}
static void codec_tests(void) {
    for (size_t split = 0; split <= sizeof(captured_request); ++split) {
        HdTunnelDecoder d = {0};
        check(!hd_tunnel_feed(&d,captured_request,split) &&
              !hd_tunnel_feed(&d,captured_request+split,sizeof(captured_request)-split) && d.seen,
              "captured LCP frame accepts every split boundary");
        uint8_t ack[128]; size_t n = hd_tunnel_ack(&d,ack,sizeof(ack));
        check(n == sizeof(captured_ack) && !memcmp(ack,captured_ack,n),"ACK exactly matches independent Windows capture");
    }
    HdTunnelDecoder d = {0}; uint8_t ack[4];
    check(!hd_tunnel_ack(&d,ack,sizeof(ack)),"no ACK without valid complete request");
    check(!hd_tunnel_feed(&d,captured_request,sizeof(captured_request)) && !hd_tunnel_ack(&d,ack,sizeof(ack)),
          "ACK output capacity enforced");
    uint8_t bad[sizeof(captured_request)]; memcpy(bad,captured_request,sizeof(bad)); bad[34] ^= 1;
    memset(&d,0,sizeof(d)); check(hd_tunnel_feed(&d,bad,sizeof(bad)) != 0,"FCS corruption rejected");
    memset(&d,0,sizeof(d)); const uint8_t abort_frame[] = {0x7e,0x7d,0x7e};
    check(hd_tunnel_feed(&d,abort_frame,sizeof(abort_frame)) != 0,"escape before flag rejected");
    memset(&d,0,sizeof(d)); uint8_t oversized[HD_TUNNEL_FRAME_MAX+2]; memset(oversized,0x40,sizeof(oversized)); oversized[0] = 0x7e;
    check(hd_tunnel_feed(&d,oversized,sizeof(oversized)) != 0,"decoded frame bound enforced");
    memset(&d,0,sizeof(d)); check(hd_tunnel_feed(&d,(uint8_t*)"noise",5) != 0,"unframed garbage rejected");
    /* Mutate decoded LCP and recalculate FCS, so protocol validation, rather
     * than checksum rejection, is exercised independently. */
    const struct { unsigned offset; uint8_t value; } mutations[] = {
        {0,0},{2,0x80},{4,2},{6,1},{7,15},{8,1},{9,0},{9,255},{10,1},{14,2},{15,3},{16,7}
    };
    for (unsigned i=0; i<sizeof(mutations)/sizeof(*mutations); ++i) {
        HdTunnelDecoder a = {0}; assert(!hd_tunnel_feed(&a,captured_request,sizeof(captured_request)));
        a.length = a.request_length+2; memcpy(a.frame,a.request,a.request_length);
        a.frame[mutations[i].offset] = mutations[i].value;
        uint16_t fcs = (uint16_t)~hd_tunnel_fcs(a.frame,a.request_length);
        a.frame[a.request_length]=fcs; a.frame[a.request_length+1]=fcs>>8; a.seen=0;
        check(hd_tunnel_lcp(&a) != 0,"valid-FCS malformed or unsupported LCP rejected");
    }
}
int main(void) {
    codec_tests();
    const enum Scenario valid[] = {NORMAL,IDLE_FIRST,SPLIT_CLIENT,SPLIT_FRAME,COALESCED};
    for (unsigned i=0;i<sizeof(valid)/sizeof(*valid);++i) {
        reset(valid[i]); free(run(1,1,"status\tcomplete"));
        check(sends==2 && ends==1 && closes==1,"single ACK and confirmed cleanup");
    }
    reset(NORMAL); free(run(0,1,"lcp-ack\tnot-requested")); check(sends==1,"default mode does not ACK");
    const struct {enum Scenario s;const char *marker;} failures[]={
        {NO_CLIENT,"deadline"},{BAD_CLIENT,"unexpected-client-signal"},{BAD_FCS,"invalid-or-unsupported-lcp"},
        {PARTIAL_FOREVER,"deadline"},{LARGE_POLL,"invalid-poll-data"},{MISSING_DATA,"invalid-poll-data"},
        {OPEN_REFUSED,"transaction-failed"},{MODE_REFUSED,"transaction-failed"},{POLL_REFUSED,"transaction-failed"},
        {SEND_REFUSED,"transaction-failed"},{ACK_REFUSED,"transaction-failed"},{BAD_OPEN_PARAMS,"unexpected-response-parameters"},
        {BAD_POLL_PARAMS,"unexpected-response-parameters"},{SHORT_SEND,"unexpected-response-parameters"},
        {END_REFUSED,"cleanup-failed"},{CLOSE_REFUSED,"cleanup-failed"},{INTERRUPT,"cancelled"},
        {DEADLINE,"deadline"},{STALLED_CLOCK,"poll-limit-or-incomplete-frame"}
    };
    for(unsigned i=0;i<sizeof(failures)/sizeof(*failures);++i){
        reset(failures[i].s); free(run(1,0,failures[i].marker));
        check(ends==1 && closes==1,"failure/cancellation still attempts ordered close once");
        check(polls<=HD_TUNNEL_POLL_MAX,"poll transaction bound enforced");
    }
    const enum Scenario uncertain[]={LOST_OPEN,LOST_POLL,DEVICE_CANCEL,DISCONNECT};
    for(unsigned i=0;i<sizeof(uncertain)/sizeof(*uncertain);++i){
        reset(uncertain[i]);free(run(1,0,"cleanup\tnot-sent"));
        check(!ends && !closes,"wounded/disconnected/cancelled device never receives follow-up commands");
    }
    reset(LOST_END);free(run(1,0,"cleanup-failed"));check(ends==1 && !closes,"lost end response prevents subsequent close");
    reset(NORMAL);device.device_family=ZUNE_FAMILY_KEEL;free(run(0,0,"not-zune-hd"));check(!calls,"non-HD never touched");
    reset(NORMAL);device.mtpz_authenticated=0;free(run(0,0,"not-authenticated"));check(!calls,"unauthenticated never touched");
    reset(NORMAL);stop=1;free(run(0,0,"cancelled"));check(!calls,"initial cancellation has no side effects");
    reset(CLOCK_ERROR);free(run(0,0,"clock-failed"));check(!calls,"clock error has no side effects");
    reset(NORMAL);FILE *out=fopen("/dev/full","w");assert(out);
    check(hd_tunnel_probe_report(&device,0,out,&stop)!=0,"output failure returns failure");fclose(out);
    check(ends==1 && closes==1,"output failure does not bypass cleanup");
    printf("HD tunnel probe: %d checks passed; no USB\n",checks);
}
