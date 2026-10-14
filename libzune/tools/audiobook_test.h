/* Isolated native audiobook research gate, not a public transfer API.
 * Protocol lead: AFTL 04b0f447e116; independently implemented against our MTP C
 * layer. See SPOKEN_MEDIA_RESEARCH.md. Requires exclusive device ownership.
 * Included after podcast_test.h for inventory/byte-comparison helpers. */
#ifndef ZUNE_AUDIOBOOK_TEST_H
#define ZUNE_AUDIOBOOK_TEST_H
#include "zune_internal.h"
#include <signal.h>
#include <strings.h>
#include "podcast_test.h"

static uint8_t *book_test_load(const char *path, uint32_t limit,
                               uint32_t *size) {
  *size = 0;
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  struct stat st;
  if (fstat(fileno(f), &st) || !S_ISREG(st.st_mode) || st.st_size < 4 ||
      (uint64_t)st.st_size > limit) {
    fclose(f);
    return NULL;
  }
  uint8_t *data = malloc((size_t)st.st_size);
  int ok = data &&
           fread(data, 1, (size_t)st.st_size, f) == (size_t)st.st_size &&
           fgetc(f) == EOF && !ferror(f);
  fclose(f);
  if (!ok) {
    free(data);
    return NULL;
  }
  *size = (uint32_t)st.st_size;
  return data;
}

static int book_test_inventory(ZuneDevice *dev, uint16_t format, uint32_t **ids,
                               int *count) {
  *ids = NULL;
  *count = 0;
  int r =
      mtp_get_object_handles(&dev->ptp, dev->storage_id, format, 0, ids, count);
  return r || *count < 0 || *count > 100000 || (*count && !*ids) ? -1 : 0;
}

static int book_test_verify(ZuneDevice *dev, uint32_t id, mtp_prop_entry_t *p) {
  uint8_t *raw = NULL;
  uint32_t n = 0;
  if (mtp_get_object_prop_value(&dev->ptp, id, p->prop_code, &raw, &n)) {
    free(raw);
    return 0;
  }
  int same = 0;
  if (raw && p->data_type == PTP_DTC_STR && n >= 3 && raw[0] &&
      n == 1u + 2u * raw[0] && raw[n - 1] == 0 && raw[n - 2] == 0) {
    char *s = mtp_ucs2_to_string(raw, n);
    same = s && !strcmp(s, p->value.str);
    fprintf(stderr, "[audiobook-test] property=0x%04X text=%s match=%d\n",
            p->prop_code, s ? s : "(unreadable)", same);
    free(s);
  } else if (raw && (n == 2 || n == 4)) {
    uint32_t v = raw[0] | ((uint32_t)raw[1] << 8);
    if (n == 4)
      v |= ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);
    same = (p->data_type == PTP_DTC_UINT16 && n == 2 && v == p->value.u16) ||
           (p->data_type == PTP_DTC_UINT32 && n == 4 && v == p->value.u32);
    /* Native book firmware may normalize classification. Record the
     * value; only its native book-track schema can substitute for 18. */
    if (p->prop_code == MTP_OPC_MetaGenre && n == 2 && v == 0 &&
        (dev->device_family == ZUNE_FAMILY_KEEL || dev->device_family == ZUNE_FAMILY_PAVO || dev->device_family == ZUNE_FAMILY_DRACO) && (id >> 24) == 0x12)
      same = 1;
    fprintf(stderr, "[audiobook-test] property=0x%04X value=%u match=%d\n",
            p->prop_code, v, same);
  }
  free(raw);
  return same;
}

static int cmd_audiobook_test(int argc, char **argv) {
  int retain = argc == 6 && !strcmp(argv[5], "--retain");
  if (argc != 5 && !retain) {
    fprintf(stderr, "usage: zunetool audiobook-test <part1.mp3> <part2.mp3> "
                    "<cover.jpg> [--retain]\n"
                    "Each MP3 <=60 seconds / 2 MiB; cover <=1 MiB. Creates "
                    "isolated book; default cleanup.\n");
    return 2;
  }
  uint8_t *audio[2] = {0}, *cover = NULL;
  uint32_t sizes[2] = {0}, durations[2] = {0}, cover_size = 0;
  int result = 2;
  for (int i = 0; i < 2; ++i) {
    audio[i] = book_test_load(argv[i + 2], 2 * 1024 * 1024, &sizes[i]);
    ZuneMetadata m = {0};
    int valid = audio[i] &&
                (!memcmp(audio[i], "ID3", 3) ||
                 (audio[i][0] == 0xff && (audio[i][1] & 0xe6) == 0xe2)) &&
                !zune_probe(argv[i + 2], &m) && m.duration_ms > 0 &&
                m.duration_ms <= 60000;
    durations[i] = m.duration_ms;
    zune_free_metadata(&m);
    if (!valid)
      goto input_done;
  }
  cover = book_test_load(argv[4], 1024 * 1024, &cover_size);
  if (!cover || cover[0] != 0xff || cover[1] != 0xd8)
    goto input_done;
  result = 1;
  ZuneDevice *dev = zune_breach();
  if (!dev || !dev->mtpz_authenticated) {
    if (dev)
      zune_sever(dev);
    fprintf(stderr, "[audiobook-test] Authentication incomplete; no writes\n");
    goto input_done;
  }
  uint32_t *before = NULL, *folders = NULL;
  int nb = 0, nf = 0;
  uint32_t created[4] = {0}, root = 0;
  int ok = 0, uncertain = 0, mutated = 0;
  char name[120];
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  snprintf(name, sizeof(name), "ZUUNED Book Test %ld-%ld", (long)now.tv_sec,
           (long)now.tv_nsec);
  podcast_test_interrupted = 0;
  void (*old_int)(int) = signal(SIGINT, podcast_test_interrupt);
  void (*old_term)(int) = signal(SIGTERM, podcast_test_interrupt);
  if (book_test_inventory(dev, 0, &before, &nb) ||
      book_test_inventory(dev, MTP_OFC_Association, &folders, &nf))
    goto done;
  /* Check descriptor types before creating anything. No inferred setters for
   * AlbumName/AlbumArtist, which this Classic advertises read-only. */
  uint16_t codes[] = {0xDC44, 0xDC46, 0xDA9A, 0xDC07,
                      0xDC8B, 0xDC95, 0xDC89, 0xDC86};
  uint16_t types[] = {PTP_DTC_STR,    PTP_DTC_STR,    PTP_DTC_STR,
                      PTP_DTC_STR,    PTP_DTC_UINT16, PTP_DTC_UINT16,
                      PTP_DTC_UINT32, 0x4002 /* AUINT8 */};
  for (unsigned i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
    uint8_t *d = NULL;
    uint32_t n = 0;
    int r = mtp_get_prop_desc(&dev->ptp, codes[i], MTP_OFC_MP3, &d, &n);
    int valid = !r && d && n >= 5 && (d[0] | (d[1] << 8)) == codes[i] &&
                (d[2] | (d[3] << 8)) == types[i] && d[4] == 1;
    free(d);
    if (!valid || podcast_test_interrupted)
      goto done;
  }
  for (int i = 0; i < nf; ++i) {
    mtp_object_info_t info = {0};
    int r = mtp_get_object_info(&dev->ptp, folders[i], &info);
    int match = !r && info.filename && !strcmp(info.filename, "OverDrive") &&
                (info.parent_object == 0 || info.parent_object == UINT32_MAX);
    mtp_free_object_info(&info);
    if (r || (match && root))
      goto done;
    if (match)
      root = folders[i];
  }
  if (podcast_test_interrupted)
    goto done;
  if (!root) {
    mutated = 1;
    root = zune_forge_folder(dev, "OverDrive", UINT32_MAX);
    if (!podcast_test_new_id(root, before, nb, folders, nf))
      goto done;
    created[0] = root;
  }
  mutated = 1;
  uint32_t id = zune_forge_folder(dev, name, root);
  if (!podcast_test_new_id(id, before, nb, created, 4))
    goto done;
  created[1] = id;
  fprintf(stderr, "[audiobook-test] book=%s root=%u folder=%u\n", name, root,
          created[1]);
  for (int i = 0; i < 2; ++i) {
    if (podcast_test_interrupted)
      goto done;
    char title[40], filename[40];
    snprintf(title, sizeof(title), "%02d Chapter %s", i + 1, i ? "Two" : "One");
    snprintf(filename, sizeof(filename), "%02d.mp3", i + 1);
    mtp_prop_entry_t props[] = {
        {.prop_code = 0xDC46, .data_type = PTP_DTC_STR, .value.str = "ZUUNED"},
        {.prop_code = 0xDC44, .data_type = PTP_DTC_STR, .value.str = title},
        {.prop_code = 0xDA9A, .data_type = PTP_DTC_STR, .value.str = name},
        {.prop_code = 0xDC07, .data_type = PTP_DTC_STR, .value.str = filename},
        {.prop_code = 0xDC8B,
         .data_type = PTP_DTC_UINT16,
         .value.u16 = (uint16_t)(i + 1)},
        {.prop_code = 0xDC95, .data_type = PTP_DTC_UINT16, .value.u16 = 18},
        {.prop_code = 0xDC89,
         .data_type = PTP_DTC_UINT32,
         .value.u32 = durations[i]}};
    id = 0;
    int sent = mtp_send_object_prop_list(&dev->ptp, dev->storage_id, created[1],
                                         MTP_OFC_MP3, sizes[i], props, 7, &id);
    if (!podcast_test_new_id(id, before, nb, created, 4)) {
      uncertain = 1;
      goto done; /* Never purge a folder after an unknown creation result. */
    }
    created[i + 2] = id;
    if (sent || mtp_send_object(&dev->ptp, audio[i], sizes[i]))
      goto done;
    fprintf(stderr, "[audiobook-test] uploaded part=%d id=%u schema=0x%02X\n",
            i + 1, id, id >> 24);
    int metadata_ok = 1;
    for (int j = 0; j < 7; ++j) {
      if (j == 3)
        continue; /* Firmware-derived filename isn't identity. */
      if (!book_test_verify(dev, id, &props[j]))
        metadata_ok = 0;
    }
    if ((dev->device_family == ZUNE_FAMILY_KEEL || dev->device_family == ZUNE_FAMILY_PAVO || dev->device_family == ZUNE_FAMILY_DRACO) && (id >> 24) != 0x12)
      metadata_ok = 0;
    mtp_object_info_t info = {0};
    int read = mtp_get_object_info(&dev->ptp, id, &info);
    int info_ok = !read && info.object_compressed_size == sizes[i] &&
                  info.object_format == MTP_OFC_MP3 &&
                  info.parent_object == created[1];
    mtp_free_object_info(&info);
    uint8_t *bytes = NULL;
    uint32_t n = 0;
    read = mtp_get_object(&dev->ptp, id, &bytes, &n);
    int same = !read && bytes && n == sizes[i] && !memcmp(bytes, audio[i], n);
    free(bytes);
    fprintf(stderr,
            "[audiobook-test] part=%d metadata=%d info=%d exact-audio=%d\n",
            i + 1, metadata_ok, info_ok, same);
    if (!metadata_ok || !info_ok || !same || podcast_test_interrupted)
      goto done;
    if (zune_brand(dev, id, cover, cover_size))
      goto done;
    bytes = NULL;
    n = 0;
    read = mtp_get_representative_sample(&dev->ptp, id, &bytes, &n);
    same = !read && bytes && n == cover_size && !memcmp(bytes, cover, n);
    free(bytes);
    fprintf(stderr, "[audiobook-test] part=%d exact-cover=%d\n", i + 1, same);
    if (!same)
      goto done;
  }
  {
    uint32_t *after = NULL;
    int na = 0;
    int preserved = !book_test_inventory(dev, 0, &after, &na);
    for (int i = 0; preserved && i < nb; ++i) {
      int found = 0;
      for (int j = 0; j < na; ++j)
        if (before[i] == after[j])
          found = 1;
      if (!found)
        preserved = 0;
    }
    free(after);
    fprintf(stderr,
            "[audiobook-test] original wildcard inventory preserved=%d\n",
            preserved);
    if (!preserved)
      goto done;
  }
  ok = !podcast_test_interrupted;
done:
  if (!ok)
    fprintf(stderr, "[audiobook-test] FAIL autopsy=0x%04X error=%s\n",
            zune_autopsy(dev),
            zune_get_error() ? zune_get_error() : "gate assertion failed");
  if (!(ok && retain)) {
    for (int i = 3; i >= 0; --i) {
      if (!created[i])
        continue;
      if (i < 2 && uncertain)
        continue;
      int r = zune_purge_track(dev, created[i]);
      fprintf(stderr, "[audiobook-test] cleanup id=%u result=%d\n", created[i],
              r);
      if (r) {
        uncertain = 1;
        ok = 0;
      } else
        created[i] = 0;
    }
  }
  if (created[0] || created[1] || created[2] || created[3])
    fprintf(stderr,
            "[audiobook-test] retained new handles root=%u folder=%u part1=%u "
            "part2=%u\n",
            created[0], created[1], created[2], created[3]);
  if (mutated && mtp_vendor_operation(&dev->ptp, 0x9108, NULL, 0, NULL, NULL))
    ok = 0;
  free(before);
  free(folders);
  zune_sever(dev);
  if (old_int != SIG_ERR)
    signal(SIGINT, old_int);
  if (old_term != SIG_ERR)
    signal(SIGTERM, old_term);
  fprintf(stderr,
          "[audiobook-test] %s; category, playback, ordering and resume "
          "require device-screen testing\n",
          ok ? "PASS" : "FAIL");
  result = ok ? 0 : 1;
input_done:
  if (result == 2)
    fprintf(stderr,
            "[audiobook-test] Invalid test inputs; no connection made\n");
  free(audio[0]);
  free(audio[1]);
  free(cover);
  return result;
}
#endif
