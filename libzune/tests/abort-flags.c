/* T3.6: zune_abort from one thread while another polls the flag (as a running
 * transfer does through the PTP abort pointer) must not be a data race.
 * Built with ThreadSanitizer, which reports races on plain or volatile ints. */
#include "../src/zune_internal.h"
#include <pthread.h>
#include <unistd.h>

static int checks, failures;
static void check(int ok, const char *name) {
    ++checks;
    if (!ok) ++failures;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
}

static ZuneDevice dev;
static long polls;

static void *transfer(void *arg) {
    (void)arg;
    while (!zune_is_aborted(&dev)) ++polls;
    return NULL;
}

int main(void) {
    for (int round = 0; round < 3; round++) {
        pthread_t worker;
        polls = 0;
        pthread_create(&worker, NULL, transfer, NULL);
        usleep(20000);
        zune_abort(&dev);               /* e.g. the app's cancel button */
        pthread_join(worker, NULL);
        zune_clear_abort(&dev);
    }
    check(!zune_is_aborted(&dev) && polls > 0, "abort is seen by a polling thread and cleared afterwards");
    printf("Abort flags: %d checks, %d failures; no USB\n", checks, failures);
    return failures ? 1 : 0;
}
