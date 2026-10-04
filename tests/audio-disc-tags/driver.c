#include "libav_transcode.h"
#include "id3_rewrite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    char *out = strcmp(argv[1], "audio") == 0
        ? zuuned_transcode_audio_with_disc(argv[2], "Album Owner", atoi(argv[3]), NULL, NULL)
        : zuuned_retag_mp3_with_disc(argv[2], "Album Owner", atoi(argv[3]));
    if (!out) return 1;
    puts(out); free(out); return 0;
}
