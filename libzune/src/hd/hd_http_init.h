/* Isolated experimental 0x9231 request, not a proven HTTP initialization API.
 * Sequence lead: XuneSyncLibrary c2a6eebeac34d1a9e4c805b1ad51ec25895dfdc4,
 * NetworkManager::InitializeHTTPSubsystem; android-file-transfer-linux
 * eb36f23ee213475c77bcecaa0898732f251e80b1, Session::Operation9231.
 * Independently encoded request: no parameters, SENDDATA, 258 zero bytes.
 * The single-request entrypoint never initializes another subsystem. Neither
 * entrypoint retries requests or invents undo.
 * A completed request does not prove artist-image fetching/display. */
#ifndef ZUNETOOL_HD_HTTP_INIT_H
#define ZUNETOOL_HD_HTTP_INIT_H
#include "zune_internal.h"
#include "hd_cli_teardown.h"
#include <signal.h>

typedef struct {
    int attempted;
    int transport_uncertain;
    int transaction_result;
    uint16_t response;
    int response_parameter_count;
    uint32_t response_parameters[5];
    const char *status; /* static string, no ownership transfer */
} HdHttpInitResult;

/* One synchronous transaction on the caller's exclusively owned connection.
 * Return0 only for OK with zero response parameters, uncancelled and report
 * successfully flushed. Result retains an attempted operation even on error.
 * Existing PTP transport timeouts apply; stop is observed before/after it. */
static int hd_http_init_run(ZuneDevice *dev, FILE *out,
                           volatile sig_atomic_t *stop, HdHttpInitResult *result) {
    if (!result) return -1;
    memset(result, 0, sizeof(*result));
    result->transaction_result = -1;
    result->status = "invalid-input";
    if (!dev || !out) return -1;
    fprintf(out, "hd-http-init-v1\nscope\texperimental single 0x9231 request; image behavior unverified\n");
    if (dev->device_family != ZUNE_FAMILY_PAVO) result->status = "not-zune-hd";
    else if (!dev->mtpz_authenticated) result->status = "not-authenticated";
    else if (ZUNE_FLAG_GET(dev->cancel_requested) || ZUNE_FLAG_GET(dev->disconnecting) || (stop && *stop)) result->status = "cancelled";
    else {
        /* Do not mutate device state when the initial report cannot be saved. */
        if (fflush(out) || ferror(out)) { result->status = "output-failed"; return -1; }
        uint8_t payload[258] = {0};
        result->attempted = 1;
        result->transaction_result = ptp_transaction(&dev->ptp, 0x9231, NULL, 0,
            PTP_DP_SENDDATA, payload, sizeof(payload), NULL, NULL,
            &result->response, result->response_parameters, &result->response_parameter_count);
        result->transport_uncertain = hd_cli_transaction_uncertain(result->transaction_result, result->response) ||
            result->response_parameter_count < 0 || result->response_parameter_count > 5 ||
            (result->response == PTP_RC_OK && result->response_parameter_count != 0) || ZUNE_FLAG_GET(dev->disconnecting);
        if (result->transaction_result != 0) result->status = "transaction-failed";
        else if (result->response != PTP_RC_OK) result->status = "non-ok-response";
        else if (result->response_parameter_count != 0) result->status = "unexpected-response-parameters";
        else if (ZUNE_FLAG_GET(dev->cancel_requested) || ZUNE_FLAG_GET(dev->disconnecting) || (stop && *stop)) result->status = "cancelled-after-request";
        else result->status = "complete";
    }
    fprintf(out, "attempted\t%d\n", result->attempted);
    if (result->attempted) {
        fprintf(out, "transaction-result\t%d\nresponse\t0x%04X\nresponse-parameter-count\t%d\n",
                result->transaction_result, result->response, result->response_parameter_count);
        /* Even a malformed count cannot cause unbounded reads or output. */
        int count = result->response_parameter_count;
        if (count > 5) count = 5;
        for (int i = 0; i < count; ++i)
            fprintf(out, "response-parameter\t%d\t0x%08X\n", i, result->response_parameters[i]);
    }
    fprintf(out, "status\t%s\n", result->status);
    if (fflush(out) || ferror(out)) { result->status = "output-failed"; return -1; }
    return strcmp(result->status, "complete") ? -1 : 0;
}
static inline int hd_http_init(ZuneDevice *dev, FILE *out, volatile sig_atomic_t *stop) {
    HdHttpInitResult result;
    return hd_http_init_run(dev, out, stop, &result);
}

/* Explicit experiment matching the pinned reference initialization prefix.
 * Our newzune capture independently shows 9218 frames5297/5300/5304 (480
 * received bytes, three response parameters) and9219 frames5339/5342/5346
 * (four bytes, three parameters). Their parameter semantics are unknown:
 * report values, validate shape, do not assume every successful value is zero.
 * 9217 is the complete private ZMDB; drain/free it without logging contents.
 * Mandatory GETDATA uses the existing transport's uncapped preallocation;
 * this helper does not claim a bounded-memory transport for the full DB. */
static int hd_http_init_full_ex(ZuneDevice *dev, FILE *out, volatile sig_atomic_t *stop,
                                int *transport_uncertain) {
    if (transport_uncertain) *transport_uncertain = 0;
    if (!dev || !out) return -1;
    fprintf(out, "hd-http-init-full-v1\nscope\texperimental reference prefix; no artist writes\n");
    if (dev->device_family != ZUNE_FAMILY_PAVO || !dev->mtpz_authenticated ||
        ZUNE_FLAG_GET(dev->cancel_requested) || ZUNE_FLAG_GET(dev->disconnecting) || (stop && *stop)) {
        fprintf(out, "full-init-status\tguard-refused\n");
        fflush(out); return -1;
    }
    if (fflush(out) || ferror(out)) return -1;
    HdHttpInitResult initial;
    int initial_result = hd_http_init_run(dev, out, stop, &initial);
    if (transport_uncertain) *transport_uncertain = initial.transport_uncertain;
    if (initial_result) {
        fprintf(out, "full-init-status\t9231-failed\n");fflush(out);return -1;
    }
    const uint16_t operations[] = {0x9217,0x9218,0x9217,0x9219,0x922f};
    fprintf(out, "receive-policy\t9217 mandatory GETDATA uses existing transport allocation without a preallocation cap; other receives capped at64KiB\n");
    for (unsigned step=0;step<sizeof(operations)/sizeof(*operations);++step) {
        if (ZUNE_FLAG_GET(dev->cancel_requested) || ZUNE_FLAG_GET(dev->disconnecting) || (stop && *stop)) {
            fprintf(out,"full-init-status\tcancelled-before-step\n");fflush(out);return -1;
        }
        if (fflush(out) || ferror(out)) return -1;
        uint16_t op=operations[step];
        uint32_t params[3]={0,0,5000};int param_count=3;
        uint16_t flags=PTP_DP_GETDATA_OPTIONAL;
        if(op==0x9217){params[0]=1;param_count=1;flags=PTP_DP_GETDATA;}
        else if(op==0x922f)param_count=0;
        uint8_t *data=NULL;uint32_t length=0,reply[5]={0};int reply_count=0;
        uint16_t response=0;
        int rc=ptp_transaction(&dev->ptp,op,param_count?params:NULL,param_count,flags,
                               NULL,0,&data,&length,&response,reply,&reply_count);
        fprintf(out,"full-init-step\t%u\topcode=0x%04X\ttransaction-result=%d\tresponse=0x%04X\tdata-bytes=%u\tresponse-parameter-count=%d\n",
                step+2,op,rc,response,length,reply_count);
        for(int i=0;i<reply_count&&i<5;++i)
            fprintf(out,"full-init-parameter\t0x%04X\t%d\t0x%08X\n",op,i,reply[i]);
        if (transport_uncertain && (hd_cli_transaction_uncertain(rc,response) ||
            reply_count<0 || reply_count>5 || ZUNE_FLAG_GET(dev->disconnecting) ||
            (response==PTP_RC_OK && reply_count!=((op==0x9218||op==0x9219)?3:0))))
            *transport_uncertain=1;
        const char *failure=NULL;
        if(rc!=0)failure="transaction-failed";
        else if(response!=PTP_RC_OK)failure="non-ok-response";
        else if(reply_count!=((op==0x9218||op==0x9219)?3:0))failure="unexpected-response-parameters";
        else if(length&&!data)failure="missing-response-data";
        else if(op==0x9217&&(!data||length<4||memcmp(data,"ZMDB",4)))failure="invalid-zmdb-header";
        else if(op!=0x9217&&length>65536)failure="oversized-optional-response";
        else if(ZUNE_FLAG_GET(dev->cancel_requested)||ZUNE_FLAG_GET(dev->disconnecting)||(stop&&*stop))failure="cancelled-after-step";
        free(data); /* includes all failure paths and private DB bytes */
        if(failure){fprintf(out,"full-init-status\t%s\n",failure);fflush(out);return -1;}
        if(fflush(out)||ferror(out))return -1;
    }
    fprintf(out,"full-init-status\tcomplete; network open remains separate\n");
    return fflush(out)||ferror(out)?-1:0;
}
static inline int hd_http_init_full(ZuneDevice *dev, FILE *out, volatile sig_atomic_t *stop) {
    return hd_http_init_full_ex(dev,out,stop,NULL);
}
#endif
