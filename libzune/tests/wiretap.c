/* zune_wiretap / zune_set_chatter against the real log router and a real
 * library failure path (mtp.c with ptp_transaction replaced). Mode "env-*"
 * checks the ZUNE_CHATTER start-up level in a fresh process. */
#include "../src/zune_internal.h"
#include <assert.h>
#include <pthread.h>

static int checks;
static uint8_t reply[16];
static uint32_t reply_len;

int ptp_transaction(ptp_session_t *s, uint16_t opcode, uint32_t *params, int nparams,
                    uint16_t flags, uint8_t *send_data, uint64_t send_len,
                    uint8_t **recv_data, uint32_t *recv_len, uint16_t *response_code,
                    uint32_t *resp_params, int *resp_nparams) {
    (void)opcode; (void)params; (void)nparams; (void)flags; (void)send_data;
    (void)send_len; (void)resp_params; (void)resp_nparams;
    s->last_response = PTP_RC_OK;
    if (response_code) *response_code = PTP_RC_OK;
    *recv_data = malloc(reply_len);
    memcpy(*recv_data, reply, reply_len);
    *recv_len = reply_len;
    return 0;
}

static void check(int ok, const char *name) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); exit(1); }
    printf("PASS: %s\n", name);
    checks++;
}

/* ---- Recording tap ---- */

static pthread_mutex_t seen_lock = PTHREAD_MUTEX_INITIALIZER;
static int seen;
static ZuneChatter seen_level;
static char seen_tag[32], seen_message[4096];

static void record(ZuneChatter level, const char *tag, const char *message, void *ctx) {
    pthread_mutex_lock(&seen_lock);
    seen++;
    seen_level = level;
    snprintf(seen_tag, sizeof(seen_tag), "%s", tag);
    snprintf(seen_message, sizeof(seen_message), "%s", message);
    (*(int *)ctx)++;
    pthread_mutex_unlock(&seen_lock);
}

static void reset(void) { seen = 0; seen_tag[0] = seen_message[0] = '\0'; }

/* A real failure: a storage ID list one entry short. */
static void truncated_storage_reply(void) {
    ptp_session_t s;
    memset(&s, 0, sizeof(s));
    memset(reply, 0, sizeof(reply));
    reply[0] = 3;            /* three IDs promised */
    reply_len = 8;           /* one present */
    uint32_t *ids = NULL;
    int count = 0;
    assert(mtp_get_storage_ids(&s, &ids, &count) == -1);
}

/* Default sink output for one log call, captured from stderr. */
static size_t capture_default(char *out, size_t size, void (*emit)(void)) {
    fflush(stderr);
    int saved = dup(STDERR_FILENO);
    char path[] = "/tmp/libzune-wiretap-XXXXXX";
    int fd = mkstemp(path);
    assert(saved >= 0 && fd >= 0);
    dup2(fd, STDERR_FILENO);
    emit();
    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    lseek(fd, 0, SEEK_SET);
    ssize_t n = read(fd, out, size - 1);
    close(fd);
    unlink(path);
    out[n > 0 ? n : 0] = '\0';
    return n > 0 ? (size_t)n : 0;
}

static void emit_talk(void) { ZLOG(ZUNE_CHATTER_TALK, "libzune", "connected: %s", "Zune"); }

/* Replacing the tap from inside a callback must not deadlock. */
static int replaced;
static void replace_self(ZuneChatter level, const char *tag, const char *message, void *ctx) {
    (void)level; (void)tag; (void)message; (void)ctx;
    replaced++;
    zune_wiretap(NULL, NULL);
}

static int thread_count;
static void *log_many(void *arg) {
    (void)arg;
    for (int i = 0; i < 500; i++) ZLOG(ZUNE_CHATTER_TALK, "libzune", "thread line %d", i);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "env-scream")) {
        int calls = 0;
        zune_wiretap(record, &calls);
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "suppressed");
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "delivered");
        check(calls == 1 && !strcmp(seen_message, "delivered"),
              "ZUNE_CHATTER=scream sets the start-up level");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "env-whisper")) {
        int calls = 0;
        zune_wiretap(record, &calls);
        ZLOG(ZUNE_CHATTER_WHISPER, "ptp", "detail");
        check(calls == 1 && !strcmp(seen_tag, "ptp"), "ZUNE_CHATTER=WHISPER (any case) enables debug");
        return 0;
    }

    int calls = 0;
    zune_wiretap(record, &calls);
    reset();
    truncated_storage_reply();
    check(seen == 1 && seen_level == ZUNE_CHATTER_SCREAM && !strcmp(seen_tag, "mtp")
          && !strcmp(seen_message, "get_storage_ids: truncated (count=3, len=8)"),
          "a real failure reaches the tap as SCREAM / mtp / exact text");

    reset();
    ZLOG(ZUNE_CHATTER_WHISPER, "ptp", "per-chunk detail");
    ZLOG(ZUNE_CHATTER_TALK, "libzune", "lifecycle");
    check(seen == 1 && seen_level == ZUNE_CHATTER_TALK, "default level TALK hides WHISPER");

    zune_set_chatter(ZUNE_CHATTER_SHOUT);
    reset();
    ZLOG(ZUNE_CHATTER_TALK, "libzune", "lifecycle");
    ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "retrying");
    ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "failed");
    check(seen == 2 && seen_level == ZUNE_CHATTER_SCREAM, "SHOUT passes SHOUT and SCREAM only");

    zune_set_chatter(ZUNE_CHATTER_SILENT);
    reset();
    ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "failed");
    truncated_storage_reply();
    check(seen == 0, "SILENT suppresses everything");
    zune_set_chatter((ZuneChatter)99);
    ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "failed");
    check(seen == 0, "an invalid level is ignored");

    zune_set_chatter(ZUNE_CHATTER_WHISPER);
    reset();
    char long_value[3000];
    memset(long_value, 'x', sizeof(long_value) - 1);
    long_value[sizeof(long_value) - 1] = '\0';
    ZLOG(ZUNE_CHATTER_WHISPER, "libzune", "path=%s", long_value);
    check(seen == 1 && strlen(seen_message) == 5 + sizeof(long_value) - 1,
          "messages longer than the stack buffer arrive whole");

    zune_set_chatter(ZUNE_CHATTER_TALK);
    zune_wiretap(NULL, NULL);
    reset();
    char text[256];
    capture_default(text, sizeof(text), emit_talk);
    check(seen == 0 && !strcmp(text, "[libzune] connected: Zune\n"),
          "NULL restores the default sink in the historical [tag] format");
    zune_set_chatter(ZUNE_CHATTER_SILENT);
    capture_default(text, sizeof(text), emit_talk);
    check(text[0] == '\0', "SILENT also silences the default sink");
    zune_set_chatter(ZUNE_CHATTER_TALK);

    zune_wiretap(replace_self, NULL);
    ZLOG(ZUNE_CHATTER_TALK, "libzune", "first");
    capture_default(text, sizeof(text), emit_talk);
    check(replaced == 1 && !strcmp(text, "[libzune] connected: Zune\n"),
          "a callback may replace the tap without deadlock");

    zune_wiretap(record, &thread_count);
    pthread_t threads[4];
    for (int i = 0; i < 4; i++) pthread_create(&threads[i], NULL, log_many, NULL);
    for (int i = 0; i < 4; i++) pthread_join(threads[i], NULL);
    check(thread_count == 2000, "concurrent threads each deliver every message");
    zune_wiretap(NULL, NULL);

    printf("Wiretap: %d checks passed; no USB\n", checks);
    return 0;
}
