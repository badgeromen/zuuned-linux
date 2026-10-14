/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common.h"
int main(int argc, char **argv)
{
    if ((argc != 9 && argc != 10) || strcmp(argv[1], "--send")) {
        puts("Usage: audiobook_send --send prepared.mp3 book-key book-title author part-title part-number duration-ms [cover.jpg]\nSends ONE prepared part. Reuse the same persisted book key and reviewed order for all parts.");
        return argc == 2 && !strcmp(argv[1], "--help") ? 0 : 2;
    }
    uint32_t number, duration;
    uint8_t *cover = NULL; size_t cover_size = 0;
    if (strlen(argv[3]) != 64 || strspn(argv[3], "0123456789abcdef") != 64 ||
        !*argv[4] || !*argv[6] || positive_u32(argv[7], &number) || number > UINT16_MAX ||
        positive_u32(argv[8], &duration) || !prepared_file(argv[2]) ||
        load_cover(argc == 10 ? argv[9] : NULL, &cover, &cover_size)) {
        fputs("Invalid input: supply a 64-character lowercase hex book key, nonempty titles, part number 1-65535, positive 32-bit duration, a nonempty prepared MP3 under 4 GiB, and an optional JPEG up to 1 MiB.\n", stderr);
        return 2;
    }
    ZuneAudiobookPartInput part = {.book_key=argv[3], .book_title=argv[4],
        .author=argv[5], .title=argv[6], .part_number=(uint16_t)number, .duration_ms=duration};
    ZuneDeviceHandle dev = zune_breach();
    if (!dev) { free(cover); return failure(NULL, "Connect"); }
    uint32_t folder = 0, id = 0; int result = 1;
    if (zune_find_audiobook_part(dev, &part, &folder, &id)) { failure(dev, "Lookup failed; no send"); goto done; }
    /* Even a found part goes through the sender's metadata/byte-size reuse check. */
    id = 0;
    if (zune_smuggle_audiobook_part(dev, argv[2], &part, &id)) {
        failure(dev, "Send failed; no automatic retry or deletion");
        fprintf(stderr, "Review device serial=%s part=%" PRIu32 "; zero ID does not prove cleanup.\n", text(zune_get_serial(dev)), id);
        goto done;
    }
    printf("Part transferred or verified reusable: %" PRIu32 "\n", id);
    result = 0;
    if (cover && zune_brand(dev, id, cover, cover_size)) result = failure(dev, "Part retained; cover failed");
    if (zune_finalize(dev)) result = failure(dev, "Part retained; finalization failed");
    puts("Finalization requested; check presentation/playback on the device.");
done:
    zune_sever(dev); free(cover);
    return result;
}
