#include "../tools/hd_http_init.h"
#include <assert.h>
enum Scenario { GOOD, REFUSED, TRANSPORT_LOST, MALFORMED, NEGATIVE_COUNT,
    TOO_MANY_PARAMS, NONOK_SUCCESS, TRANSPORT_WITH_OK, CANCEL_DURING, DEVICE_CANCEL_DURING,
    DISCONNECT_DURING };
static enum Scenario scenario;
static ZuneDevice device;
static volatile sig_atomic_t stop;
static int calls, checks;
static int full_mode, full_failure_step, full_failure_kind;

int ptp_transaction(ptp_session_t *s, uint16_t opcode, uint32_t *params,
        int count, uint16_t flags, uint8_t *send, uint64_t send_len,
        uint8_t **receive, uint32_t *receive_len, uint16_t *response,
        uint32_t *reply, int *reply_count) {
    if(full_mode && opcode!=0x9231){
        const uint16_t ops[]={0x9231,0x9217,0x9218,0x9217,0x9219,0x922f};
        assert(calls>=1&&calls<6&&opcode==ops[calls]);
        assert(!send&&!send_len&&receive&&receive_len&&response&&reply&&reply_count);
        if(opcode==0x9217)assert(count==1&&params&&params[0]==1&&flags==PTP_DP_GETDATA);
        else if(opcode==0x922f)assert(!count&&!params&&flags==PTP_DP_GETDATA_OPTIONAL);
        else assert(count==3&&params&&params[0]==0&&params[1]==0&&params[2]==5000&&flags==PTP_DP_GETDATA_OPTIONAL);
        ++calls;*response=s->last_response=PTP_RC_OK;
        *reply_count=(opcode==0x9218||opcode==0x9219)?3:0;
        reply[0]=17;reply[1]=19;reply[2]=23; /* Unknown semantics, do not require zeros. */
        *receive_len=opcode==0x9217?237000:opcode==0x9218?480:opcode==0x9219?4:1036;
        *receive=calloc(1,*receive_len);assert(*receive);
        if(opcode==0x9217){memcpy(*receive,"ZMDB",4);memcpy(*receive+4,"PRIVATE-DATABASE-CONTENTS",25);}
        if(calls==full_failure_step){
            switch(full_failure_kind){
                case 1:*response=s->last_response=0x2005;return -1;
                case 2:*response=s->last_response=PTP_ERROR_IO;return -1;
                case 3:*reply_count=1000000;break;
                case 4:stop=1;break;
                case 5:device.cancel_requested=1;break;
                case 6:device.disconnecting=1;break;
                case 7:*response=s->last_response=0x2005;break;
                case 8:free(*receive);*receive=NULL;break;
                case 9:(*receive)[0]='X';break;
            }
        }
        return 0;
    }
    assert(s == &device.ptp && opcode == 0x9231 && params == NULL && count == 0);
    assert(flags == PTP_DP_SENDDATA && send_len == 258 && send);
    assert(receive == NULL && receive_len == NULL && response && reply && reply_count);
    for (size_t i = 0; i < send_len; ++i) assert(send[i] == 0);
    ++calls;
    s->last_response = *response = PTP_RC_OK; *reply_count = 0;
    if (scenario == REFUSED || scenario == NONOK_SUCCESS) {
        s->last_response = *response = 0x2005;
        return scenario == REFUSED ? -1 : 0;
    }
    if (scenario == TRANSPORT_LOST) { s->last_response = *response = PTP_ERROR_IO; return -1; }
    if (scenario == TRANSPORT_WITH_OK) return -1;
    if (scenario == MALFORMED) { *reply_count = 1; reply[0] = 0x12345678; }
    if (scenario == NEGATIVE_COUNT) *reply_count = -1;
    if (scenario == TOO_MANY_PARAMS) { *reply_count = 1000000; for (int i=0;i<5;++i) reply[i] = i+1; }
    if (scenario == CANCEL_DURING) stop = 1;
    if (scenario == DEVICE_CANCEL_DURING) device.cancel_requested = 1;
    if (scenario == DISCONNECT_DURING) device.disconnecting = 1;
    return 0;
}
static void check(int ok, const char *message) {
    if (!ok) { fprintf(stderr, "FAIL %s\n", message); exit(1); }
    printf("PASS %s\n", message); ++checks;
}
static void reset(enum Scenario next) {
    memset(&device,0,sizeof(device)); device.device_family = ZUNE_FAMILY_PAVO;
    device.mtpz_authenticated = 1; device.serial = "SERIAL-MUST-NOT-APPEAR";
    scenario = next; calls = 0; stop = 0;
    full_mode=full_failure_step=full_failure_kind=0;
}
static char *report(int expect_success, const char *status, HdHttpInitResult *result) {
    char *text = NULL; size_t len = 0; FILE *out = open_memstream(&text,&len); assert(out);
    int rc = hd_http_init_run(&device,out,&stop,result); fclose(out);
    check((rc == 0) == expect_success && !strcmp(result->status,status) && strstr(text,status), status);
    check(!strstr(text,device.serial), "no device serial emitted");
    if (!expect_success) check(!strstr(text,"status\tcomplete"), "failure never claims completion");
    return text;
}
static void full_tests(void){
    reset(GOOD);full_mode=1;
    char *text=NULL;size_t length=0;FILE *out=open_memstream(&text,&length);assert(out);
    check(hd_http_init_full(&device,out,&stop)==0&&calls==6,"full reference prefix exact six-operation order and flags");
    fclose(out);
    check(strstr(text,"full-init-status\tcomplete")&&strstr(text,"data-bytes=237000")&&strstr(text,"data-bytes=480")&&
          strstr(text,"0x00000017"),"actual lengths and nonzero unknown response parameter values reported");
    check(!strstr(text,"PRIVATE-DATABASE")&&!strstr(text,device.serial),"full prefix never logs received private database contents");
    check(strstr(text,"without a preallocation cap")!=NULL,"mandatory database receive allocation limit reported truthfully");free(text);
    for(int step=2;step<=6;++step){
        for(int kind=1;kind<=8;++kind){
            reset(GOOD);full_mode=1;full_failure_step=step;full_failure_kind=kind;
            text=NULL;length=0;out=open_memstream(&text,&length);assert(out);
            check(hd_http_init_full(&device,out,&stop)!=0&&calls==step,"failure or cancellation stops prefix immediately without retry");
            fclose(out);
            check(!strstr(text,"full-init-status\tcomplete"),"partial prefix never claims full completion");
            if(kind==3)check(length<4096,"malformed response parameter count has bounded report");
            free(text);
        }
    }
    reset(GOOD);full_mode=1;full_failure_step=2;full_failure_kind=9;
    out=tmpfile();assert(out);check(hd_http_init_full(&device,out,&stop)!=0&&calls==2,"mandatory database receive requires ZMDB header");fclose(out);
    reset(REFUSED);full_mode=1;out=tmpfile();assert(out);
    check(hd_http_init_full(&device,out,&stop)!=0&&calls==1,"9231 refusal prevents every subsequent initialization read");fclose(out);
    for(int kind=1;kind<=8;++kind){
        reset(GOOD);full_mode=1;full_failure_step=3;full_failure_kind=kind;
        int uncertain=99;out=tmpfile();assert(out);
        check(hd_http_init_full_ex(&device,out,&stop,&uncertain)!=0&&calls==3,"status entrypoint stops at failing prefix transaction");
        check(uncertain==(kind==2||kind==3||kind==6),"prefix uncertainty distinguishes transport/malformed/disconnect from complete refusal and safe cancellation");
        fclose(out);
    }
    reset(GOOD);full_mode=1;device.device_family=ZUNE_FAMILY_DRACO;out=tmpfile();assert(out);
    check(hd_http_init_full(&device,out,&stop)!=0&&!calls,"full prefix HD guard before all operations");
    device.device_family=ZUNE_FAMILY_PAVO;device.mtpz_authenticated=0;
    check(hd_http_init_full(&device,out,&stop)!=0&&!calls,"full prefix authentication guard before operations");
    device.mtpz_authenticated=1;stop=1;
    check(hd_http_init_full(&device,out,&stop)!=0&&!calls,"full prefix cancellation guard before operations");fclose(out);
    reset(GOOD);full_mode=1;out=fopen("/dev/full","w");assert(out);
    check(hd_http_init_full(&device,out,&stop)!=0&&!calls,"full prefix unwritable report prevents operations");fclose(out);
}
int main(void) {
    HdHttpInitResult result;
    reset(GOOD); char *text = report(1,"complete",&result);
    check(calls==1 && result.attempted==1 && result.response==PTP_RC_OK && result.response_parameter_count==0,
          "exact single allowlisted request with zero parameters and 258 zero payload bytes"); free(text);
    reset(TRANSPORT_LOST);FILE *status_out=tmpfile();assert(status_out);
    check(hd_http_init_run(&device,status_out,&stop,&result)!=0&&result.transport_uncertain,"single init transport loss propagates uncertainty");fclose(status_out);
    reset(REFUSED);status_out=tmpfile();assert(status_out);
    check(hd_http_init_run(&device,status_out,&stop,&result)!=0&&!result.transport_uncertain,"single init complete refusal remains safe to close");fclose(status_out);
    const struct { enum Scenario mode; const char *status; } cases[] = {
        {REFUSED,"transaction-failed"},{TRANSPORT_LOST,"transaction-failed"},
        {MALFORMED,"unexpected-response-parameters"},{NEGATIVE_COUNT,"unexpected-response-parameters"},
        {TOO_MANY_PARAMS,"unexpected-response-parameters"},{NONOK_SUCCESS,"non-ok-response"},
        {TRANSPORT_WITH_OK,"transaction-failed"},{CANCEL_DURING,"cancelled-after-request"},
        {DEVICE_CANCEL_DURING,"cancelled-after-request"},{DISCONNECT_DURING,"cancelled-after-request"}
    };
    for (size_t i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        reset(cases[i].mode); text=report(0,cases[i].status,&result);
        check(calls==1 && result.attempted, "failed/cancelled request retained without retry or undo");
        if (scenario==REFUSED) check(strstr(text,"response\t0x2005")!=NULL, "actual refusal code retained");
        if (scenario==TRANSPORT_LOST) check(strstr(text,"response\t0x02FF")!=NULL, "actual transport code retained");
        if (scenario==TOO_MANY_PARAMS) check(strlen(text)<1024 && strstr(text,"response-parameter\t4\t0x00000005") &&
                 !strstr(text,"response-parameter\t5\t"), "malformed reply count output bounded to actual array");
        free(text);
    }
    const uint8_t families[]={ZUNE_FAMILY_KEEL,ZUNE_FAMILY_DRACO,0xff};
    for(size_t i=0;i<sizeof(families);++i){reset(GOOD);device.device_family=families[i];free(report(0,"not-zune-hd",&result));check(!calls&&!result.attempted,"non-HD guard before all device access");}
    reset(GOOD);device.mtpz_authenticated=0;free(report(0,"not-authenticated",&result));check(!calls&&!result.attempted,"authentication guard before device access");
    reset(GOOD);stop=1;free(report(0,"cancelled",&result));check(!calls&&!result.attempted,"signal cancellation before device access");
    reset(GOOD);device.cancel_requested=1;free(report(0,"cancelled",&result));check(!calls&&!result.attempted,"device cancellation before access");
    reset(GOOD);device.disconnecting=1;free(report(0,"cancelled",&result));check(!calls&&!result.attempted,"disconnect guard before access");
    reset(GOOD);FILE *out=fopen("/dev/full","w");assert(out);
    check(hd_http_init_run(&device,out,&stop,&result)!=0 && !calls && !result.attempted && !strcmp(result.status,"output-failed"),
          "unwritable initial report prevents device mutation");fclose(out);
    out=tmpfile();assert(out);
    check(hd_http_init_run(NULL,out,&stop,&result)!=0 && !result.attempted,"NULL device refused");
    check(hd_http_init_run(&device,NULL,&stop,&result)!=0 && !result.attempted,"NULL output refused");
    check(hd_http_init_run(&device,out,&stop,NULL)!=0 && !calls,"required result output refused when missing");
    check(hd_http_init_run(&device,out,NULL,&result)==0 && calls==1,"optional signal flag can be absent");
    reset(GOOD);check(hd_http_init(&device,out,&stop)==0 && calls==1,"CLI wrapper performs exactly one successful request");
    fclose(out);
    full_tests();
    printf("HD HTTP init: %d checks passed; no USB\n",checks);
    return 0;
}
