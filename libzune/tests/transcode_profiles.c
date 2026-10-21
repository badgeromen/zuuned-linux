/* Real-ffmpeg check of every zune_arm_video profile on a 4:4:4 source,
 * the input x264's baseline profile cannot take as-is. Each output is
 * inspected with ffprobe by the wrapper. */
#include "../src/zune_internal.h"
#include <stdarg.h>

void zune_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}
void zune_clear_error(void) {}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    static const struct { ZuneModel model; const char *name; } profiles[] = {
        { ZUNE_MODEL_30, "zune30" }, { ZUNE_MODEL_80, "zune80" },
        { ZUNE_MODEL_HD, "zunehd" }, { ZUNE_MODEL_HD_720P, "zunehd720" },
    };
    zune_set_chatter(ZUNE_CHATTER_SCREAM);
    for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
        char *out = zune_arm_video(argv[1], profiles[i].model);
        if (!out) { printf("FAIL %s: transcode failed\n", profiles[i].name); return 1; }
        printf("%s %s\n", profiles[i].name, out);   /* the wrapper probes and removes it */
        free(out);
    }
    return 0;
}
