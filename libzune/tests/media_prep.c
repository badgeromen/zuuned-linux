/* Real-ffmpeg checks of photo preparation and video thumbnails on inputs
 * found by the October 19 Draco format matrix: an animated GIF, and a clip
 * shorter than the thumbnail seek. The wrapper inspects the outputs. */
#include "../src/zune_internal.h"
#include <stdarg.h>
#include <sys/stat.h>

void zune_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}
void zune_clear_error(void) {}

static int exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* media_prep <animated.gif> <still.png> <short-clip> <long-clip> <not-video> <dir> */
int main(int argc, char **argv) {
    if (argc != 7) return 2;
    zune_set_chatter(ZUNE_CHATTER_SILENT);
    const char *photos[] = { argv[1], argv[2] };
    const char *names[] = { "animated-gif", "still-png" };
    for (int i = 0; i < 2; i++) {
        char *out = zune_arm_photo(photos[i]);
        printf("photo %s %s\n", names[i], out ? out : "-");   /* the wrapper probes and removes it */
        free(out);
    }
    const char *clips[] = { argv[3], argv[4] };
    const char *clip_names[] = { "short-clip", "long-clip" };
    char thumb[1024];
    for (int i = 0; i < 2; i++) {
        snprintf(thumb, sizeof(thumb), "%s/%s.jpg", argv[6], clip_names[i]);
        int rc = zune_snap_thumb(clips[i], thumb);
        printf("thumb %s %s\n", clip_names[i], rc == 0 ? thumb : "-");
    }
    /* A file ffmpeg cannot decode fails and leaves no output behind. */
    snprintf(thumb, sizeof(thumb), "%s/not-video.jpg", argv[6]);
    int rc = zune_snap_thumb(argv[5], thumb);
    printf("failure %s %d %d\n", "not-video", rc, exists(thumb));
    return 0;
}
