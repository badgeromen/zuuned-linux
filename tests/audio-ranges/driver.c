#include "libav_transcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static float last;
static int cancel;
static int invalid_progress;
static void progress(float value, void *unused)
{
    (void)unused;
    if (value < last || value < 0 || value > 1) invalid_progress = 1;
    last = value;
    if (cancel && value > 0.1f) zuuned_transcode_abort();
}
int main(int argc, char **argv)
{
    if (argc != 6) return 2;
    cancel = strcmp(argv[5], "cancel") == 0;
    if (!strcmp(argv[5], "pre-cancel")) zuuned_transcode_abort();
    char *path = zuuned_transcode_audio_range(argv[1], strtoll(argv[2], NULL, 10),
                                             strtoll(argv[3], NULL, 10), progress, NULL);
    int expect_failure = strcmp(argv[5], "ok") != 0;
    if (invalid_progress || (path == NULL) != expect_failure) {
        fprintf(stderr, "Unexpected result (%s), progress=%g\n", path ? "success" : "failure", last);
        if (path) { unlink(path); free(path); }
        return 1;
    }
    if (!path) return 0;
    int result = last != 1 || rename(path, argv[4]) != 0;
    if (result) unlink(path);
    free(path);
    return result;
}
