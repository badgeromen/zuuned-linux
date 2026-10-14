/* Native audiobook parts. Protocol attribution and Keel evidence:
 * docs/SPOKEN_MEDIA_RESEARCH.md. No music album/artist objects are created. */
#include "zune_internal.h"
#include <fcntl.h>
#include <sys/mman.h>

static int book_ready(ZuneDevice *d) {
  if (!d || !d->mtpz_authenticated || d->disconnecting || d->cancel_requested) {
    zune_set_error(
        "Audiobook operation requires an authenticated, active device");
    return 0;
  }
  /* Keep the model boundary independent from per-part schema/readback checks. */
  if (d->device_family != ZUNE_FAMILY_KEEL && d->device_family != ZUNE_FAMILY_PAVO &&
      d->device_family != ZUNE_FAMILY_DRACO) {
    zune_set_error(
        "Native audiobook sync requires Zune 30, 80/120 or HD");
    return 0;
  }
  return 1;
}
static int book_text(const char *s, int required) {
  if (!s || (required && !*s))
    return 0;
  if (!*s)
    return !required;
  uint32_t n = 0;
  uint8_t *raw = mtp_string_to_ucs2(s, &n);
  char *back = raw ? mtp_ucs2_to_string(raw, n) : NULL;
  int ok = back && !strcmp(s, back);
  free(raw);
  free(back);
  return ok;
}
static int book_input(const ZuneAudiobookPartInput *p) {
  if (!p || !p->book_key || strlen(p->book_key) != 64 || !p->part_number ||
      !p->duration_ms || !book_text(p->book_title, 1) ||
      !book_text(p->title, 1) || !book_text(p->author, 0)) {
    zune_set_error("Invalid audiobook identity or metadata");
    return 0;
  }
  for (int i = 0; i < 64; ++i)
    if (!strchr("0123456789abcdef", p->book_key[i])) {
      zune_set_error("Audiobook key must be a lowercase SHA-256 identity");
      return 0;
    }
  return 1;
}
static char *book_read_text(ZuneDevice *d, uint32_t id, uint16_t prop) {
  uint8_t *raw = NULL;
  uint32_t n = 0;
  char *out = NULL;
  if (!mtp_get_object_prop_value(&d->ptp, id, prop, &raw, &n) && raw) {
    if (n == 1 && !raw[0])
      out = strdup("");
    else if (n >= 3 && raw[0] && n == 1u + 2u * raw[0] && !raw[n - 1] &&
             !raw[n - 2])
      out = mtp_ucs2_to_string(raw, n);
  }
  free(raw);
  if (!out)
    zune_set_error("Unreadable audiobook text property 0x%04X", prop);
  return out;
}
static int book_number(ZuneDevice *d, uint32_t id, uint16_t prop, int width,
                       uint32_t *v) {
  uint8_t *raw = NULL;
  uint32_t n = 0;
  *v = 0;
  int ok = !mtp_get_object_prop_value(&d->ptp, id, prop, &raw, &n) && raw &&
           n == (uint32_t)width;
  if (ok)
    for (int i = 0; i < width; ++i)
      *v |= (uint32_t)raw[i] << (8 * i);
  free(raw);
  if (!ok)
    zune_set_error("Unreadable audiobook numeric property 0x%04X", prop);
  return ok;
}
static int book_inventory(ZuneDevice *d, uint16_t format, uint32_t **ids,
                          int *n) {
  *ids = NULL;
  *n = 0;
  if (mtp_get_object_handles(&d->ptp, d->storage_id, format, 0, ids, n) ||
      *n < 0 || *n > 100000 || (*n && !*ids)) {
    free(*ids);
    *ids = NULL;
    zune_set_error("Incomplete audiobook inventory; no creation permitted");
    return 0;
  }
  return 1;
}
static int book_folders(ZuneDevice *d, const char *key, uint32_t *root,
                        uint32_t *folder) {
  *root = *folder = 0;
  uint32_t *ids = NULL;
  int n = 0;
  if (!book_inventory(d, MTP_OFC_Association, &ids, &n))
    return 0;
  char name[80];
  snprintf(name, sizeof(name), "ZUUNED-%s", key);
  int ok = 1;
  for (int i = 0; ok && i < n; ++i) {
    mtp_object_info_t info = {0};
    if (!book_ready(d) || mtp_get_object_info(&d->ptp, ids[i], &info) ||
        !info.filename)
      ok = 0;
    else if (!strcmp(info.filename, "OverDrive") &&
             (info.parent_object == 0 || info.parent_object == UINT32_MAX)) {
      if (*root)
        ok = 0;
      else
        *root = ids[i];
    } else if (!strcmp(info.filename, name)) {
      if (*folder)
        ok = 0;
      else
        *folder = ids[i];
    }
    mtp_free_object_info(&info);
  }
  free(ids);
  if (ok && *folder) {
    mtp_object_info_t info = {0};
    ok = *root && !mtp_get_object_info(&d->ptp, *folder, &info) &&
         info.parent_object == *root;
    mtp_free_object_info(&info);
  }
  if (!ok)
    zune_set_error("Ambiguous or unreadable audiobook folders");
  return ok;
}
int zune_find_audiobook_part(ZuneDevice *d, const ZuneAudiobookPartInput *p,
                             uint32_t *folder, uint32_t *out) {
  if (folder)
    *folder = 0;
  if (out)
    *out = 0;
  if (!folder || !out || !book_ready(d) || !book_input(p))
    return -1;
  uint32_t root = 0, dir = 0, *ids = NULL, match = 0;
  int n = 0, ok = 1;
  if (!book_folders(d, p->book_key, &root, &dir) ||
      !book_inventory(d, 0, &ids, &n))
    return -1;
  for (int i = 0; ok && i < n; ++i) {
    if (!book_ready(d)) {
      ok = 0;
      break;
    }
    /* HD omits audiobook MP3s from format-filtered enumeration. Its complete
     * wildcard inventory includes native schema 0x12; filter only after read. */
    if ((ids[i] >> 24) != 0x12)
      continue;
    mtp_object_info_t info = {0};
    if (mtp_get_object_info(&d->ptp, ids[i], &info)) {
      ok = 0;
      break;
    }
    uint32_t parent = info.parent_object;
    mtp_free_object_info(&info);
    char *name = book_read_text(d, ids[i], 0xDA9A);
    if (!name) {
      ok = 0;
      break;
    }
    int same_name = !strcmp(name, p->book_title);
    free(name);
    /* Firmware groups by book name. Refuse merging unrelated editions or
     * authors with the same title, including books uploaded elsewhere. */
    if (same_name && parent != dir) {
      zune_set_error("A different audiobook with this title is already on the "
                     "Zune; choose a distinct book title");
      ok = 0;
      break;
    }
    if (!dir || parent != dir)
      continue;
    if (!same_name) {
      zune_set_error("Device book name differs from this saved identity");
      ok = 0;
      break;
    }
    uint32_t number = 0;
    if (!book_number(d, ids[i], 0xDC8B, 2, &number)) {
      ok = 0;
      break;
    }
    if (number != p->part_number)
      continue;
    char *title = book_read_text(d, ids[i], 0xDC44),
         *author = book_read_text(d, ids[i], 0xDC46);
    uint32_t duration = 0, category = 0;
    ok = title && author && !strcmp(title, p->title) &&
         !strcmp(author, p->author) &&
         book_number(d, ids[i], 0xDC89, 4, &duration) &&
         duration == p->duration_ms &&
         book_number(d, ids[i], 0xDC95, 2, &category) &&
         (category == 0 || category == 18) && !match;
    free(title);
    free(author);
    if (ok)
      match = ids[i];
    else
      zune_set_error("Existing audiobook part is ambiguous or conflicts with "
                     "local metadata");
  }
  free(ids);
  if (!ok)
    return -1;
  *folder = dir;
  *out = match;
  return 0;
}
int zune_smuggle_audiobook_part(ZuneDevice *d, const char *path,
                                const ZuneAudiobookPartInput *p,
                                uint32_t *out) {
  if (out)
    *out = 0;
  if (!out || !book_ready(d) || !book_input(p) || !path)
    return -1;
  uint32_t folder = 0, existing = 0;
  if (zune_find_audiobook_part(d, p, &folder, &existing))
    return -1;
  int fd = open(path, O_RDONLY);
  struct stat st;
  if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
      (uint64_t)st.st_size > UINT32_MAX) {
    if (fd >= 0)
      close(fd);
    zune_set_error("Prepared audiobook MP3 must be nonempty and under 4 GiB");
    return -1;
  }
  if (existing) {
    mtp_object_info_t info = {0};
    int ok = !mtp_get_object_info(&d->ptp, existing, &info) &&
             info.object_format == MTP_OFC_MP3 &&
             info.object_compressed_size == (uint32_t)st.st_size;
    mtp_free_object_info(&info);
    close(fd);
    if (!ok) {
      zune_set_error(
          "Existing audiobook part size conflicts with prepared audio");
      return -1;
    }
    *out = existing;
    return 0;
  }
  void *data = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (data == MAP_FAILED) {
    zune_set_error("Cannot map prepared audiobook audio");
    return -1;
  }
  char filename[32];
  snprintf(filename, sizeof(filename), "Part %05u.mp3", p->part_number);
  mtp_prop_entry_t props[] = {
      {.prop_code = 0xDC46,
       .data_type = PTP_DTC_STR,
       .value.str = (char *)p->author},
      {.prop_code = 0xDC44,
       .data_type = PTP_DTC_STR,
       .value.str = (char *)p->title},
      {.prop_code = 0xDA9A,
       .data_type = PTP_DTC_STR,
       .value.str = (char *)p->book_title},
      {.prop_code = 0xDC07, .data_type = PTP_DTC_STR, .value.str = filename},
      {.prop_code = 0xDC8B,
       .data_type = PTP_DTC_UINT16,
       .value.u16 = p->part_number},
      {.prop_code = 0xDC95, .data_type = PTP_DTC_UINT16, .value.u16 = 18},
      {.prop_code = 0xDC89,
       .data_type = PTP_DTC_UINT32,
       .value.u32 = p->duration_ms}};
  int ok = 1;
  for (int i = 0; ok && i < 7; ++i) {
    uint8_t *raw = NULL;
    uint32_t n = 0;
    ok = book_ready(d) &&
         !mtp_get_prop_desc(&d->ptp, props[i].prop_code, MTP_OFC_MP3, &raw,
                            &n) &&
         raw && n >= 5 && (raw[0] | (raw[1] << 8)) == props[i].prop_code &&
         (raw[2] | (raw[3] << 8)) == props[i].data_type && raw[4] == 1;
    free(raw);
  }
  uint32_t id = 0;
  if (!ok) {
    zune_set_error("Audiobook descriptor check failed");
    goto done;
  }
  if (!folder) {
    uint32_t root = 0;
    if (!book_folders(d, p->book_key, &root, &folder)) {
      ok = 0;
      goto done;
    }
    if (!root)
      root = zune_forge_folder(d, "OverDrive", UINT32_MAX);
    if (!root || root == UINT32_MAX) {
      ok = 0;
      goto done;
    }
    char name[80];
    snprintf(name, sizeof(name), "ZUUNED-%s", p->book_key);
    if (!folder)
      folder = zune_forge_folder(d, name, root);
    if (!folder || folder == UINT32_MAX) {
      ok = 0;
      goto done;
    }
  }
  ok = !mtp_send_object_prop_list(&d->ptp, d->storage_id, folder, MTP_OFC_MP3,
                                  (uint64_t)st.st_size, props, 7, &id);
  if (!id || id == UINT32_MAX) {
    ok = 0;
    id = 0;
    goto done;
  }
  *out = id; /* Retain exact identity on every subsequent failure; no automatic
                purge. */
  if (!ok || !book_ready(d) ||
      mtp_send_object(&d->ptp, data, (uint32_t)st.st_size)) {
    ok = 0;
    goto done;
  }
  {
    uint32_t verified_folder = 0, verified_id = 0;
    ok = !zune_find_audiobook_part(d, p, &verified_folder, &verified_id) &&
         verified_folder == folder && verified_id == id;
    mtp_object_info_t info = {0};
    if (ok)
      ok = !mtp_get_object_info(&d->ptp, id, &info) &&
           info.object_format == MTP_OFC_MP3 &&
           info.object_compressed_size == (uint32_t)st.st_size;
    mtp_free_object_info(&info);
  }
done:
  munmap(data, (size_t)st.st_size);
  if (!ok) {
    zune_set_error("Audiobook send/readback failed (0x%04X); inspect returned "
                   "ID before retrying",
                   d->ptp.last_response);
    return -1;
  }
  return 0;
}
