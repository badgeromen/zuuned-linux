/* One existing artist, journal first, one write after a complete IPCP frame.
 * No USB thread and no object creation. Call only from the serialized CLI. */
#ifndef ZUNETOOL_HD_ART_BIND_READY_H
#define ZUNETOOL_HD_ART_BIND_READY_H
#include "hd_art_journal.h"
#include "hd_cli_teardown.h"
#include <signal.h>
typedef struct {
    ZuneDevice *dev;
    FILE *out;
    volatile sig_atomic_t *stop;
    HdArtistBindingSnapshot snapshot;
    int prepared, attempted, applied, uncertain, restore_attempted;
    int64_t deadline;
    int64_t (*now_fn)(void);
    HdArtistBindingResult (*snapshot_fn)(ZuneDevice *,uint32_t,const char *,HdArtistBindingSnapshot *);
    int (*save_fn)(const char *,const HdArtistBindingSnapshot *);
    HdArtistBindingResult (*apply_fn)(ZuneDevice *,const HdArtistBindingSnapshot *,const uint8_t *,int (*)(void *),void *);
    HdArtistBindingResult (*restore_fn)(ZuneDevice *,const HdArtistBindingSnapshot *,const uint8_t *);
} HdArtReadyBinding;

static inline int hd_art_ready_options(const char *handle,const char *backup,
                                      const char *guid,const char *artist,int explicit_artist,uint32_t *id) {
    if(!handle||!*handle||handle[0]<'1'||handle[0]>'9'||!backup||!*backup||
       !guid||strcmp(guid,hd_art_test_guid)||!explicit_artist||!artist||!*artist||!id)return -1;
    char *end=NULL;errno=0;unsigned long value=strtoul(handle,&end,10);
    if(errno||!end||*end||!value||value>=UINT32_MAX)return -1;
    *id=(uint32_t)value;return 0;
}
static inline int64_t hd_art_ready_now(void) {
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
    return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static inline void hd_art_ready_init(HdArtReadyBinding *b,ZuneDevice *dev,FILE *out,volatile sig_atomic_t *stop) {
    memset(b,0,sizeof(*b));b->dev=dev;b->out=out;b->stop=stop;
    b->snapshot_fn=hd_artist_binding_snapshot;b->save_fn=hd_art_journal_save;
    b->now_fn=hd_art_ready_now;
    b->apply_fn=hd_artist_binding_apply_guarded;b->restore_fn=hd_artist_binding_restore;
}
static inline int hd_art_ready_cancelled(const HdArtReadyBinding *b) {
    return !b->dev||ZUNE_FLAG_GET(b->dev->cancel_requested)||ZUNE_FLAG_GET(b->dev->disconnecting)||(b->stop&&*b->stop);
}
static inline int hd_art_ready_write_allowed(void *context) {
    HdArtReadyBinding *b=context;
    if(hd_art_ready_cancelled(b)||b->deadline<=0||!b->now_fn)return 0;
    int64_t now=b->now_fn();return now>=0&&now<b->deadline;
}
static inline int hd_art_ready_prepare(HdArtReadyBinding *b,uint32_t handle,const char *artist,const char *backup) {
    if(!b||!b->out||b->prepared||hd_art_ready_cancelled(b))return -1;
    HdArtistBindingResult result=b->snapshot_fn(b->dev,handle,artist,&b->snapshot);
    if(result!=HD_ART_BIND_CONFIRMED){
        b->uncertain=hd_cli_transaction_uncertain(-1,b->dev->ptp.last_response);
        /* A rejected identity with a complete OK read is not transport loss. */
        if(b->dev->ptp.last_response==PTP_RC_OK)b->uncertain=0;
        fprintf(b->out,"ready-binding-snapshot\t%s\n",hd_art_binding_result(result));return -1;
    }
    if(hd_art_ready_cancelled(b)||b->save_fn(backup,&b->snapshot)){
        fprintf(b->out,"ready-binding-journal\tfailed-or-cancelled; no write attempted\n");return -1;
    }
    b->prepared=1;
    fprintf(b->out,"ready-binding-journal\tdurable; waiting for complete bidirectional IPCP\n");
    return fflush(b->out)||ferror(b->out)?-1:0;
}
/* Readiness is checked by the outer tick after complete input framing and its
 * deadline check. Signal cancellation is checked again before the first read. */
static inline int hd_art_ready_tick(HdArtReadyBinding *b,int ready) {
    if(!b)return 0;
    if(hd_art_ready_cancelled(b))return -1;
    if(!ready)return 0;
    if(!b->prepared||!hd_art_ready_write_allowed(b))return -1;
    if(b->attempted)return b->applied?0:-1;
    if(fflush(b->out)||ferror(b->out))return -1;
    b->attempted=1;
    HdArtistBindingResult result=b->apply_fn(b->dev,&b->snapshot,hd_art_test_guid_wire,hd_art_ready_write_allowed,b);
    b->applied=result==HD_ART_BIND_CONFIRMED;
    b->uncertain=result==HD_ART_BIND_UNCERTAIN || ZUNE_FLAG_GET(b->dev->disconnecting) ||
        (result==HD_ART_BIND_REJECTED && b->dev->ptp.last_response!=PTP_RC_OK &&
         hd_cli_transaction_uncertain(-1,b->dev->ptp.last_response));
    fprintf(b->out,"ready-binding-apply\t%s\nresponse\t0x%04X\n",hd_art_binding_result(result),b->dev->ptp.last_response);
    if(fflush(b->out)||ferror(b->out))return -1;
    return b->applied?0:-1;
}
/* Called AFTER tunnel teardown, even after a healthy observation deadline or
 * SIGINT. The separate signal flag does not inhibit rollback. Actual device
 * cancellation/disconnection is never cleared or overridden. */
static inline int hd_art_ready_finish(HdArtReadyBinding *b,int network_clean,int transport_uncertain) {
    if(!b)return 0;
    if(transport_uncertain)b->uncertain=1;
    if(!b->applied)return b->uncertain?-1:0;
    if(b->restore_attempted)return -1;
    if(!network_clean||b->uncertain||!b->dev||ZUNE_FLAG_GET(b->dev->cancel_requested)||ZUNE_FLAG_GET(b->dev->disconnecting)){
        fprintf(b->out,"ready-binding-restore\tnot-attempted; keep recovery journal\n");fflush(b->out);return -1;
    }
    b->restore_attempted=1;
    HdArtistBindingResult result=b->restore_fn(b->dev,&b->snapshot,hd_art_test_guid_wire);
    if(result==HD_ART_BIND_UNCERTAIN || ZUNE_FLAG_GET(b->dev->disconnecting) ||
       (result==HD_ART_BIND_REJECTED && b->dev->ptp.last_response!=PTP_RC_OK &&
        hd_cli_transaction_uncertain(-1,b->dev->ptp.last_response)))b->uncertain=1;
    fprintf(b->out,"ready-binding-restore\t%s\nresponse\t0x%04X\n",hd_art_binding_result(result),b->dev->ptp.last_response);
    if(result!=HD_ART_BIND_CONFIRMED)fprintf(b->out,"ready-binding-recovery\tkeep journal; no retry\n");
    fflush(b->out);return result==HD_ART_BIND_CONFIRMED?0:-1;
}
#endif
