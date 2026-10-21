/* Private retained full-album experiment. No deletion or existing artist reuse.
 * Independent journal v1: never interpreted as a one-track first-sync journal.
 */
#ifndef ZUNETOOL_HD_ART_ALBUM_SYNC_H
#define ZUNETOOL_HD_ART_ALBUM_SYNC_H
#include "hd_art_first_sync.h"
#define HD_ALBUM_TRACK_MAX 64u
#define HD_ALBUM_FILE_MAX (32u * 1024u * 1024u)
#define HD_ALBUM_TOTAL_MAX (256u * 1024u * 1024u)
#define HD_ALBUM_OBJECT_MAX (HD_ALBUM_TRACK_MAX + 5u)
typedef struct {
  uint32_t handle, parent, storage, size, duration, disc, track;
  uint16_t format;
  uint8_t persistent[16];
  int proven;
  char name[512], filename[512];
} HdAlbumObject;
typedef struct {
  HdAlbumObject object;
  char path[4096];
  uint8_t *data;
} HdAlbumTrack;
typedef struct {
  ZuneDevice *dev;
  FILE *out;
  volatile sig_atomic_t *stop;
  int64_t (*now_fn)(void);
  int64_t deadline;
  char artist[512], album[512], year[5], guid[37];
  uint8_t guid_wire[16];
  HdFirstSync wire;
  HdAlbumObject objects[HD_ALBUM_OBJECT_MAX]; /* Music(if owned), folders,
                                                 artist, album, tracks */
  HdAlbumTrack tracks[HD_ALBUM_TRACK_MAX];
  size_t track_count, total_bytes, phase;
  uint8_t *cover;
  size_t cover_size;
  uint32_t *baseline;
  size_t baseline_count;
  int fd, prepared, failed, uncertain, graph_ready, retained, music_owned,
      cover_done, links_verified, resume_pending, recovery_verified;
  size_t resume_through;
  char recovery_source[4096];
  uint8_t recovery_journal_sha256[32];
  HdAlbumObject recovery_extras[2], replacement_original;
  int replace_missing_album, extras_recorded;
} HdAlbumSync;
/* Valid UTF-8 to counted UTF-16, including surrogate pairs. No replacement or
 * truncation. */
static size_t hd_album_string(const char *text, uint8_t out[511]) {
  if (!text || !*text)
    return 0;
  size_t units = 0;
  const uint8_t *p = (const uint8_t *)text;
  while (*p) {
    uint32_t c = *p++;
    unsigned rest = 0;
    if (c >= 0xc2 && c <= 0xdf) {
      c &= 31;
      rest = 1;
    } else if (c >= 0xe0 && c <= 0xef) {
      c &= 15;
      rest = 2;
    } else if (c >= 0xf0 && c <= 0xf4) {
      c &= 7;
      rest = 3;
    } else if (c >= 128)
      return 0;
    for (unsigned i = 0; i < rest; ++i) {
      if ((*p & 0xc0) != 0x80)
        return 0;
      c = (c << 6) | (*p++ & 63);
    }
    if ((rest == 1 && c < 128) || (rest == 2 && c < 2048) ||
        (rest == 3 && c < 65536) || c > 0x10ffff ||
        (c >= 0xd800 && c <= 0xdfff) || c < 32 || c == 127)
      return 0;
    if (units + (c >= 65536 ? 2u : 1u) > 254)
      return 0;
    if (c >= 65536) {
      c -= 65536;
      hd_first_u16(out + 1 + 2 * units++, 0xd800 + (c >> 10));
      c = 0xdc00 + (c & 1023);
    }
    hd_first_u16(out + 1 + 2 * units++, c);
  }
  out[0] = (uint8_t)(units + 1);
  hd_first_u16(out + 1 + 2 * units, 0);
  return 3 + 2 * units;
}
static int hd_album_text(char dst[512], const char *src) {
  uint8_t w[511];
  size_t n = strlen(src);
  if (n >= 512 || !hd_album_string(src, w))
    return -1;
  memcpy(dst, src, n + 1);
  return 0;
}
static int hd_album_number(const char *p, uint32_t min, uint32_t max,
                           uint32_t *out) {
  if (!p || !*p)
    return -1;
  uint64_t n = 0;
  for (; *p; ++p) {
    if (*p < '0' || *p > '9')
      return -1;
    n = n * 10 + (*p - '0');
    if (n > max)
      return -1;
  }
  if (n < min)
    return -1;
  *out = (uint32_t)n;
  return 0;
}
static int hd_album_read_file(const char *path, size_t max, uint8_t **data,
                              size_t *size) {
  *data = NULL;
  *size = 0;
  if (path[0] != '/')
    return -1;
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  struct stat a, b;
  if (fd < 0)
    return -1;
  int good = !fstat(fd, &a) && S_ISREG(a.st_mode) && a.st_size >= 4 &&
             (uint64_t)a.st_size <= max;
  uint8_t *p = good ? malloc((size_t)a.st_size) : NULL;
  if (!p) {
    close(fd);
    return -1;
  }
  size_t got = 0;
  while (got < (size_t)a.st_size) {
    ssize_t n = read(fd, p + got, (size_t)a.st_size - got);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      good = 0;
      break;
    }
    got += (size_t)n;
  }
  good = good && !fstat(fd, &b) && a.st_size == b.st_size &&
         a.st_mtim.tv_sec == b.st_mtim.tv_sec &&
         a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
         a.st_ctim.tv_sec == b.st_ctim.tv_sec &&
         a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
  close(fd);
  if (!good) {
    free(p);
    return -1;
  }
  *data = p;
  *size = got;
  return 0;
}
static void hd_album_release(HdAlbumSync *s) {
  if (s->fd >= 0)
    close(s->fd);
  s->fd = -1;
  for (size_t i = 0; i < HD_ALBUM_TRACK_MAX; ++i) {
    free(s->tracks[i].data);
    s->tracks[i].data = NULL;
  }
  free(s->cover);
  s->cover = NULL;
  free(s->baseline);
  s->baseline = NULL;
}
static int hd_album_local(HdAlbumSync *s, const char *plan) {
  memset(s, 0, sizeof(*s));
  s->fd = s->wire.fd = -1;
  FILE *f = fopen(plan, "rb");
  if (!f)
    return -1;
  char line[8192];
  unsigned row = 0;
  int rc = -1;
  char cover_path[4096] = {0};
  while (fgets(line, sizeof(line), f)) {
    size_t n = strlen(line);
    if (!n || line[n - 1] != '\n')
      goto done;
    line[--n] = 0;
    if (n && line[n - 1] == '\r')
      line[--n] = 0;
    if (++row == 1) {
      if (strcmp(line, "ZUUNED-HD-ALBUM\t1"))
        goto done;
      continue;
    }
    char *fields[6], *p = line;
    unsigned count = 0;
    while (count < 6) {
      fields[count++] = p;
      char *tab = strchr(p, '\t');
      if (count == 6 && tab)
        goto done;
      if (!tab)
        break;
      *tab = 0;
      p = tab + 1;
    }
    if (count == 6 && strchr(fields[5], '\t'))
      goto done;
    if (count == 2 && !strcmp(fields[0], "artist")) {
      if (s->artist[0] || hd_album_text(s->artist, fields[1]))
        goto done;
    } else if (count == 2 && !strcmp(fields[0], "album")) {
      if (s->album[0] || hd_album_text(s->album, fields[1]))
        goto done;
    } else if (count == 2 && !strcmp(fields[0], "year")) {
      uint32_t year;
      if (s->year[0] || strlen(fields[1]) != 4 ||
          hd_album_number(fields[1], 1000, 9999, &year))
        goto done;
      memcpy(s->year, fields[1], 5);
    } else if (count == 2 && !strcmp(fields[0], "cover")) {
      if (cover_path[0] || strlen(fields[1]) >= sizeof(cover_path))
        goto done;
      strcpy(cover_path, fields[1]);
    } else if (count == 6 && !strcmp(fields[0], "track")) {
      if (s->track_count == HD_ALBUM_TRACK_MAX)
        goto done;
      HdAlbumTrack *t = &s->tracks[s->track_count];
      if (hd_album_number(fields[1], 1, 65535, &t->object.disc) ||
          hd_album_number(fields[2], 1, 65535, &t->object.track) ||
          hd_album_number(fields[3], 1, 86400000, &t->object.duration) ||
          hd_album_text(t->object.name, fields[4]) ||
          strlen(fields[5]) >= sizeof(t->path))
        goto done;
      if (strpbrk(t->object.name, "/\\:*?\"<>|"))
        goto done;
      for (size_t i = 0; i < s->track_count; ++i)
        if (!strcasecmp(s->tracks[i].object.name, t->object.name))
          goto done;
      if (s->track_count) {
        HdAlbumObject *prev = &s->tracks[s->track_count - 1].object;
        if (t->object.disc < prev->disc ||
            (t->object.disc == prev->disc && t->object.track <= prev->track))
          goto done;
      }
      for (size_t i = 0; i < s->track_count; ++i)
        if (!strcmp(s->tracks[i].path, fields[5]))
          goto done;
      strcpy(t->path, fields[5]);
      size_t bytes;
      if (hd_album_read_file(t->path, HD_ALBUM_FILE_MAX, &t->data, &bytes) ||
          s->total_bytes + bytes > HD_ALBUM_TOTAL_MAX)
        goto done;
      if (memcmp(t->data, "ID3", 3) &&
          (t->data[0] != 255 || (t->data[1] & 0xe0) != 0xe0))
        goto done;
      t->object.size = (uint32_t)bytes;
      s->total_bytes += bytes;
      ++s->track_count;
    } else
      goto done;
  }
  if (ferror(f) || !s->artist[0] || !s->album[0] || !s->track_count ||
      strpbrk(s->artist, "/\\:*?\"<>|") || strpbrk(s->album, "/\\:*?\"<>|"))
    goto done;
  if (cover_path[0] &&
      (hd_album_read_file(cover_path, 128 * 1024, &s->cover, &s->cover_size) ||
       s->cover[0] != 255 || s->cover[1] != 216 ||
       s->cover[s->cover_size - 2] != 255 ||
       s->cover[s->cover_size - 1] != 217))
    goto done;
  rc = 0;
done:
  fclose(f);
  if (rc)
    hd_album_release(s);
  return rc;
}
static int hd_album_allowed(HdAlbumSync *s) {
  if (!s || s->failed || s->uncertain || !s->dev || !s->out || !s->now_fn)
    return 0;
  s->wire.dev = s->dev;
  s->wire.out = s->out;
  s->wire.stop = s->stop;
  s->wire.now_fn = s->now_fn;
  s->wire.deadline = s->deadline;
  return hd_first_allowed(&s->wire, 0);
}
/* Journal records contain exact object and response identities, no media bytes.
 * Initial record is followed by baseline IDs and complete ordered track plans.
 * Private native ABI, explicitly versioned; never a ZUHDF1 recovery file. */
typedef struct {
  char magic[8], stage[48];
  uint32_t version, record_bytes, index, tracks, baseline_count;
  uint8_t guid[16];
  uint16_t opcode, response;
  int result, parameter_count;
  uint32_t parameters[5];
  HdAlbumObject object;
} HdAlbumRecord;
static int hd_album_record(HdAlbumSync *s, const char *stage, unsigned index,
                           uint16_t op, int result, uint16_t response,
                           const uint32_t params[5], int count) {
  HdAlbumRecord r = {0};
  memcpy(r.magic, "ZUALBM1", 7);
  r.version = 1;
  r.record_bytes = sizeof(r);
  r.index = index;
  r.tracks = (uint32_t)s->track_count;
  r.baseline_count = (uint32_t)s->baseline_count;
  snprintf(r.stage, sizeof(r.stage), "%s", stage);
  memcpy(r.guid, s->guid_wire, 16);
  r.opcode = op;
  r.result = result;
  r.response = response;
  r.parameter_count = count;
  if (params && count > 0)
    memcpy(r.parameters, params, (size_t)(count > 5 ? 5 : count) * 4);
  if (index < HD_ALBUM_OBJECT_MAX)
    r.object = s->objects[index];
  if (s->fd < 0 || hd_first_write_all(s->fd, &r, sizeof(r)) || fsync(s->fd)) {
    s->failed = 1;
    if (s->prepared)
      s->uncertain = 1;
    return -1;
  }
  return 0;
}
static int hd_album_tx(HdAlbumSync *s, unsigned index, uint16_t op,
                       uint32_t *params, int count, uint16_t flags,
                       uint8_t *send, size_t size, uint8_t **data,
                       uint32_t *length, uint32_t reply[5], int mutation) {
  if (data)
    *data = NULL;
  if (length)
    *length = 0;
  if (!hd_album_allowed(s))
    return -1;
  if (mutation && hd_album_record(s, "intent", index, op, 0, 0, params, count))
    return -1;
  if (!hd_album_allowed(s))
    return -1;
  uint16_t response = 0;
  uint32_t values[5] = {0};
  int n = 0;
  int rc = ptp_transaction(&s->dev->ptp, op, params, count, flags, send, size,
                           data, length, &response, values, &n);
  if (mutation &&
      hd_album_record(s, "response", index, op, rc, response, values, n))
    return -1;
  int shape = op == MTP_OC_SendObjectPropList ? n >= 3 && n <= 5
              : op == 0x9108                  ? n >= 0 && n <= 1
                                              : n == 0;
  if (rc || response != PTP_RC_OK || !shape) {
    fprintf(
        s->out,
        "album-response\top=0x%04x response=0x%04x result=%d parameters=%d\n",
        op, response, rc, n);
    s->failed = 1;
    s->uncertain |= hd_cli_transaction_uncertain(rc, response) || !shape ||
                    (mutation && !hd_bind_explicit_refusal(response));
    return -1;
  }
  if (reply)
    memcpy(reply, values, sizeof(values));
  return 0;
}
static int hd_album_get(HdAlbumSync *s, uint32_t id, uint16_t prop,
                        uint8_t **data, uint32_t *len) {
  uint32_t p[5] = {id, prop};
  return hd_album_tx(s, UINT32_MAX, MTP_OC_GetObjectPropValue, p, 2,
                     PTP_DP_GETDATA, NULL, 0, data, len, NULL, 0);
}
static int hd_album_value(HdAlbumSync *s, uint32_t id, uint16_t prop,
                          uint8_t *out, size_t expected) {
  uint8_t *p = NULL;
  uint32_t n = 0;
  int rc = hd_album_get(s, id, prop, &p, &n);
  int good = !rc && p && n == expected;
  if (good)
    memcpy(out, p, n);
  free(p);
  return good ? 0 : -1;
}
static int hd_album_handles(HdAlbumSync *s, uint16_t format, uint32_t parent,
                            uint32_t **ids, size_t *count) {
  *ids = NULL;
  *count = 0;
  uint32_t p[5] = {s->wire.storage, format, parent}, n = 0;
  uint8_t *data = NULL;
  int rc = hd_album_tx(s, UINT32_MAX, PTP_OC_GetObjectHandles, p, 3,
                       PTP_DP_GETDATA, NULL, 0, &data, &n, NULL, 0);
  if (rc || !data || n < 4 || hd_art_u32(data) > HD_FIRST_MAX_HANDLES ||
      n != 4 + 4 * hd_art_u32(data)) {
    free(data);
    return -1;
  }
  *count = hd_art_u32(data);
  *ids = calloc(*count ? *count : 1, 4);
  if (!*ids) {
    free(data);
    return -1;
  }
  for (size_t i = 0; i < *count; ++i) {
    uint32_t id = hd_art_u32(data + 4 + 4 * i);
    if (!id || id == UINT32_MAX) {
      free(*ids);
      *ids = NULL;
      free(data);
      return -1;
    }
    for (size_t j = 0; j < i; ++j)
      if ((*ids)[j] == id) {
        free(*ids);
        *ids = NULL;
        free(data);
        return -1;
      }
    (*ids)[i] = id;
  }
  free(data);
  return 0;
}
static void hd_album_mismatch(HdAlbumSync *s, uint32_t id, uint16_t prop,
                              const uint8_t *expected, size_t want,
                              const uint8_t *actual, size_t got) {
  fprintf(s->out,
          "album-mismatch\thandle=%u property=0x%04x expected-bytes=%zu "
          "actual-bytes=%zu expected-hex=",
          id, prop, want, got);
  for (size_t i = 0; expected && i < want && i < 64; ++i)
    fprintf(s->out, "%02x", expected[i]);
  fprintf(s->out, " actual-hex=");
  for (size_t i = 0; actual && i < got && i < 64; ++i)
    fprintf(s->out, "%02x", actual[i]);
  fputc('\n', s->out);
}
static int hd_album_expect(HdAlbumSync *s, uint32_t id, uint16_t prop,
                           const uint8_t *expected, size_t want) {
  uint8_t *actual = NULL;
  uint32_t got = 0;
  int rc = hd_album_get(s, id, prop, &actual, &got);
  int good = !rc && actual && got == want && !memcmp(actual, expected, want);
  if (!good)
    hd_album_mismatch(s, id, prop, expected, want, actual, got);
  free(actual);
  return good ? 0 : -1;
}
static int hd_album_expect_number(HdAlbumSync *s, uint32_t id, uint16_t prop,
                                  uint32_t value, size_t width) {
  uint8_t expected[4];
  hd_first_u32(expected, value);
  return hd_album_expect(s, id, prop, expected, width);
}
static int hd_album_same_text(HdAlbumSync *s, uint32_t id, uint16_t prop,
                              const char *expected) {
  uint8_t *p = NULL;
  uint32_t n = 0;
  int rc = hd_album_get(s, id, prop, &p, &n);
  int good = !rc && hd_first_name_match(p, n, expected);
  if (!good) {
    uint8_t want[511];
    size_t length = hd_album_string(expected, want);
    hd_album_mismatch(s, id, prop, want, length, p, n);
  }
  free(p);
  return good ? 0 : -1;
}
static int hd_album_verify(HdAlbumSync *s, unsigned index) {
  HdAlbumObject *o = &s->objects[index];
  uint8_t v[16];
  if (!o->handle)
    return -1;
  if (hd_album_expect_number(s, o->handle, MTP_OPC_ObjectFormat, o->format,
                             2) ||
      hd_album_expect_number(s, o->handle, MTP_OPC_StorageID, s->wire.storage,
                             4) ||
      hd_album_expect_number(s, o->handle, MTP_OPC_ParentObject, o->parent, 4))
    return -1;
  uint8_t *text = NULL;
  uint32_t n = 0;
  if (hd_album_get(s, o->handle, MTP_OPC_ObjectFileName, &text, &n)) {
    free(text);
    return -1;
  }
  int match = hd_first_name_match(text, n, o->filename);
  if (!match && index >= 5) {
    char normalized[512];
    int len = snprintf(normalized, sizeof(normalized), "%02u %s.mp3", o->track,
                       o->name);
    if (len > 0 && (size_t)len < sizeof(normalized) &&
        hd_first_name_match(text, n, normalized)) {
      strcpy(o->filename, normalized);
      match = 1;
    }
  }
  if (!match) {
    uint8_t want[511];
    size_t length = hd_album_string(o->filename, want);
    hd_album_mismatch(s, o->handle, MTP_OPC_ObjectFileName, want, length, text,
                      n);
  }
  free(text);
  if (!match || (o->format != MTP_OFC_Association &&
                 hd_album_same_text(s, o->handle, MTP_OPC_Name, o->name)))
    return -1;
  if (hd_album_value(s, o->handle, 0xdc41, v, 16) || !hd_bind_nonzero(v) ||
      (o->proven && memcmp(o->persistent, v, 16)))
    return -1;
  memcpy(o->persistent, v, 16);
  if (index == 3 && hd_album_expect(s, o->handle, 0xda97, s->guid_wire, 16))
    return -1;
  if (index >= 4 &&
      hd_album_expect_number(s, o->handle, 0xdab9, s->objects[3].handle, 4))
    return -1;
  if (index >= 5) {
    if (hd_album_same_text(s, o->handle, MTP_OPC_Artist, s->artist))
      return -1;
    uint8_t *album_text = NULL;
    uint32_t album_n = 0;
    int album_rc =
        hd_album_get(s, o->handle, MTP_OPC_AlbumName, &album_text, &album_n);
    int album_exact =
        !album_rc && hd_first_name_match(album_text, album_n, s->album);
    int provisional = !album_rc && !s->links_verified &&
                      s->dev->device_family == ZUNE_FAMILY_PAVO &&
                      hd_first_name_match(album_text, album_n, "Unknown Album");
    if (!album_exact && !provisional) {
      uint8_t want[511];
      size_t length = hd_album_string(s->album, want);
      hd_album_mismatch(s, o->handle, MTP_OPC_AlbumName, want, length,
                        album_text, album_n);
    }
    free(album_text);
    if (!album_exact && !provisional)
      return -1;
    if (provisional)
      fprintf(s->out,
              "album-provisional\thandle=%u album-name=Unknown Album; final "
              "link proof required\n",
              o->handle);
    if (hd_album_expect_number(s, o->handle, MTP_OPC_Duration, o->duration,
                               4) ||
        hd_album_expect_number(s, o->handle, MTP_OPC_Track, o->track, 2))
      return -1;
    uint8_t *disc = NULL;
    uint32_t disc_n = 0;
    int disc_rc = hd_album_get(s, o->handle, 0xdab8, &disc, &disc_n);
    int disc_exact =
        !disc_rc && disc && disc_n == 4 && hd_art_u32(disc) == o->disc;
    int default_disc = !disc_rc && disc && disc_n == 4 && !hd_art_u32(disc) &&
                       o->disc == 1 &&
                       s->dev->device_family == ZUNE_FAMILY_PAVO;
    if (!disc_exact && !default_disc) {
      uint8_t want[4];
      hd_first_u32(want, o->disc);
      hd_album_mismatch(s, o->handle, 0xdab8, want, 4, disc, disc_n);
    }
    free(disc);
    if (!disc_exact && !default_disc)
      return -1;
    if (default_disc)
      fprintf(s->out,
              "album-normalization\thandle=%u default-disc=1 readback=0\n",
              o->handle);
    uint8_t *size = NULL;
    uint32_t size_n = 0;
    int rc = hd_album_get(s, o->handle, MTP_OPC_ObjectSize, &size, &size_n);
    int good = !rc && size && (size_n == 4 || size_n == 8) &&
               hd_art_u32(size) == o->size &&
               (size_n == 4 || !hd_art_u32(size + 4));
    free(size);
    if (!good)
      return -1;
  }
  o->proven = 1;
  return 0;
}
static int hd_album_payload(HdAlbumSync *s, unsigned index) {
  uint32_t p[5] = {s->objects[index].handle}, n = 0;
  uint8_t *data = NULL;
  int rc = hd_album_tx(s, index, PTP_OC_GetObject, p, 1, PTP_DP_GETDATA, NULL,
                       0, &data, &n, NULL, 0);
  int good = !rc && data && n == s->objects[index].size &&
             !memcmp(data, s->tracks[index - 5].data, n);
  free(data);
  return good ? 0 : -1;
}
static int hd_album_configure(HdAlbumSync *s) {
  const char *names[] = {"Music", s->artist, s->album, s->artist, s->album};
  for (unsigned i = 0; i < 5; ++i) {
    HdAlbumObject *o = &s->objects[i];
    if (hd_album_text(o->name, names[i]))
      return -1;
    o->storage = s->wire.storage;
    o->format = i < 3    ? MTP_OFC_Association
                : i == 3 ? MTP_OFC_AbstractArtist
                         : MTP_OFC_AbstractAudioAlbum;
    int n = snprintf(o->filename, sizeof(o->filename), "%s%s", names[i],
                     i == 3   ? ".art"
                     : i == 4 ? ".alb"
                              : "");
    uint8_t w[511];
    if (n < 0 || (size_t)n >= sizeof(o->filename) ||
        !hd_album_string(o->filename, w))
      return -1;
  }
  for (size_t i = 0; i < s->track_count; ++i) {
    HdAlbumObject *o = &s->objects[5 + i];
    *o = s->tracks[i].object;
    o->storage = s->wire.storage;
    o->format = MTP_OFC_MP3;
    int n = snprintf(o->filename, sizeof(o->filename), "%s.mp3", o->name);
    uint8_t w[511];
    if (n < 0 || (size_t)n >= sizeof(o->filename) ||
        !hd_album_string(o->filename, w))
      return -1;
  }
  return 0;
}
static int hd_album_absent(HdAlbumSync *s, HdAlbumObject *o) {
  uint32_t *ids = NULL;
  size_t count = 0;
  if (hd_album_handles(s, o->format, 0, &ids, &count))
    return -1;
  int good = 1;
  for (size_t i = 0; i < count && good; ++i) {
    uint8_t *text = NULL;
    uint32_t n = 0;
    uint16_t prop = o->format == MTP_OFC_Association ? MTP_OPC_ObjectFileName
                                                     : MTP_OPC_Name;
    if (hd_album_get(s, ids[i], prop, &text, &n)) {
      free(text);
      good = 0;
      break;
    }
    size_t used = 0;
    if (!text || hd_art_string(text, n, &used) || used != n ||
        hd_first_name_match(
            text, n, o->format == MTP_OFC_Association ? o->filename : o->name))
      good = 0;
    free(text);
  }
  free(ids);
  return good ? 0 : -1;
}
static int hd_album_set_guid(HdAlbumSync *s, const char *text) {
  if (!s || !text || strlen(text) != 36)
    return -1;
  uint8_t raw[16];
  unsigned at = 0;
  for (unsigned i = 0; i < 36;) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (text[i++] != '-')
        return -1;
      continue;
    }
    unsigned v = 0;
    for (unsigned j = 0; j < 2; ++j) {
      unsigned char c = (unsigned char)text[i++];
      if (c >= '0' && c <= '9')
        v = v * 16 + c - '0';
      else if (c >= 'a' && c <= 'f')
        v = v * 16 + c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        v = v * 16 + c - 'A' + 10;
      else
        return -1;
    }
    raw[at++] = (uint8_t)v;
  }
  static const unsigned order[16] = {3, 2, 1,  0,  5,  4,  7,  6,
                                     8, 9, 10, 11, 12, 13, 14, 15};
  if (at != 16 || !hd_bind_nonzero(raw))
    return -1;
  for (unsigned i = 0; i < 16; ++i)
    s->guid_wire[i] = raw[order[i]];
  memcpy(s->guid, text, 37);
  return 0;
}
static int hd_album_guid_absent(HdAlbumSync *s) {
  uint32_t *ids = NULL;
  size_t count = 0;
  if (hd_album_handles(s, MTP_OFC_AbstractArtist, 0, &ids, &count))
    return -1;
  int good = 1;
  for (size_t i = 0; i < count; ++i) {
    uint8_t v[16];
    if (hd_album_value(s, ids[i], 0xda97, v, 16) ||
        !memcmp(v, s->guid_wire, 16)) {
      good = 0;
      break;
    }
  }
  free(ids);
  return good ? 0 : -1;
}
static int hd_album_plan_journal(HdAlbumSync *s, const char *journal,
                                 const char *stage) {
  s->fd = open(journal, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (s->fd < 0)
    return -1;
  if (hd_album_record(s, stage, UINT32_MAX, 0, 0, 0, NULL, 0))
    return -1;
  /* Fixed-size, pointer-free plan fields, then baseline IDs and ordered tracks.
   */
  if (hd_first_write_all(s->fd, s->wire.serial, sizeof(s->wire.serial)) ||
      hd_first_write_all(s->fd, s->artist, sizeof(s->artist)) ||
      hd_first_write_all(s->fd, s->album, sizeof(s->album)) ||
      hd_first_write_all(s->fd, s->year, sizeof(s->year)) ||
      hd_first_write_all(s->fd, s->wire.roots, sizeof(s->wire.roots)) ||
      hd_first_write_all(s->fd, &s->music_owned, sizeof(s->music_owned)) ||
      hd_first_write_all(s->fd, s->baseline, 4 * s->baseline_count))
    return -1;
  for (size_t i = 0; i < s->track_count; ++i)
    if (hd_first_write_all(s->fd, &s->tracks[i].object,
                           sizeof(HdAlbumObject)) ||
        hd_first_write_all(s->fd, s->tracks[i].path, sizeof(s->tracks[i].path)))
      return -1;
  if (fsync(s->fd))
    return -1;
  char *dir = strdup(journal);
  if (!dir)
    return -1;
  char *slash = strrchr(dir, '/');
  if (slash) {
    if (slash == dir)
      slash[1] = 0;
    else
      *slash = 0;
  } else {
    free(dir);
    dir = strdup(".");
    if (!dir)
      return -1;
  }
  int parent = open(dir, O_RDONLY | O_DIRECTORY);
  free(dir);
  if (parent < 0)
    return -1;
  int rc = fsync(parent);
  if (close(parent))
    rc = -1;
  if (rc)
    return -1;
  return 0;
}
static int hd_album_prepare(HdAlbumSync *s, const char *journal) {
  if (!s || !hd_bind_nonzero(s->guid_wire) || !hd_bind_ready(s->dev) ||
      !s->track_count || s->track_count > HD_ALBUM_TRACK_MAX)
    return -1;
  strcpy(s->wire.serial, s->dev->serial);
  s->wire.storage = s->dev->storage_id;
  if (!hd_album_allowed(s) || hd_album_configure(s))
    return -1;
  hd_first_configure(&s->wire);
  if (hd_album_handles(s, 0, 0, &s->baseline, &s->baseline_count) ||
      hd_first_roots(&s->wire)) {
    s->uncertain |= s->wire.uncertain;
    return -1;
  }
  if (s->wire.roots[0].handle) {
    HdFirstObject *r = &s->wire.roots[0];
    s->objects[0].handle = r->handle;
    s->objects[0].proven = 1;
    memcpy(s->objects[0].persistent, r->persistent, 16);
    s->phase = 1;
  } else
    s->music_owned = 1;
  for (unsigned i = 1; i < 5; ++i)
    if (hd_album_absent(s, &s->objects[i]))
      return -1;
  if (hd_album_guid_absent(s))
    return -1;
  if (hd_album_plan_journal(s, journal, "baseline-plan"))
    return -1;
  s->prepared = 1;
  fprintf(s->out,
          "album-prepared\ttracks=%zu bytes=%zu; exact artist and album "
          "absent; journal durable\n",
          s->track_count, s->total_bytes);
  return fflush(s->out) || ferror(s->out) ? -1 : 0;
}
static int hd_album_prop_string(uint8_t *buf, size_t *pos, uint16_t prop,
                                const char *text) {
  uint8_t w[511];
  size_t n = hd_album_string(text, w);
  if (!n || *pos + 8 + n > 8192)
    return -1;
  hd_first_prop(buf, pos, prop, PTP_DTC_STR, w, n);
  return 0;
}
static size_t hd_album_props(HdAlbumSync *s, unsigned index,
                             uint8_t buf[8192]) {
  HdAlbumObject *o = &s->objects[index];
  size_t pos = 4;
  uint8_t v[16] = {0};
  if (index < 3) {
    hd_first_u32(buf, 1);
    return hd_album_prop_string(buf, &pos, MTP_OPC_ObjectFileName, o->filename)
               ? 0
               : pos;
  }
  hd_first_u32(buf, index == 3 ? 4 : index == 4 ? (s->year[0] ? 6 : 5) : 13);
  hd_first_prop(buf, &pos, 0xdab0, PTP_DTC_UINT8, v, 1);
  if (hd_album_prop_string(buf, &pos, MTP_OPC_ObjectFileName, o->filename) ||
      hd_album_prop_string(buf, &pos, MTP_OPC_Name, o->name))
    return 0;
  if (index == 3) {
    hd_first_prop(buf, &pos, 0xda97, HD_ART_UINT128, s->guid_wire, 16);
    return pos;
  }
  if (hd_album_prop_string(buf, &pos, MTP_OPC_Artist, s->artist))
    return 0;
  hd_first_u32(v, s->objects[3].handle);
  hd_first_prop(buf, &pos, 0xdab9, PTP_DTC_UINT32, v, 4);
  if (index == 4) {
    if (s->year[0]) {
      char date[20];
      snprintf(date, sizeof(date), "%s0101T000000.0", s->year);
      if (hd_album_prop_string(buf, &pos, 0xdc47, date))
        return 0;
    }
    return pos;
  }
  if (hd_album_prop_string(buf, &pos, MTP_OPC_AlbumName, s->album) ||
      hd_album_prop_string(buf, &pos, 0xdc9b, s->artist))
    return 0;
  hd_first_u16(v, 1);
  hd_first_prop(buf, &pos, 0xdc95, PTP_DTC_UINT16, v, 2);
  hd_first_u16(v, 0);
  hd_first_prop(buf, &pos, 0xdc9d, PTP_DTC_UINT16, v, 2);
  hd_first_prop(buf, &pos, 0xdab2, PTP_DTC_UINT8, v, 1);
  hd_first_u32(v, o->disc);
  hd_first_prop(buf, &pos, 0xdab8, PTP_DTC_UINT32, v, 4);
  hd_first_u32(v, o->duration);
  hd_first_prop(buf, &pos, MTP_OPC_Duration, PTP_DTC_UINT32, v, 4);
  hd_first_u16(v, (uint16_t)o->track);
  hd_first_prop(buf, &pos, MTP_OPC_Track, PTP_DTC_UINT16, v, 2);
  return pos;
}
static int hd_album_new_id(HdAlbumSync *s, uint32_t id) {
  if (!id || id == UINT32_MAX || id == s->replacement_original.handle ||
      id == s->recovery_extras[0].handle || id == s->recovery_extras[1].handle)
    return 0;
  for (size_t i = 0; i < s->baseline_count; ++i)
    if (s->baseline[i] == id)
      return 0;
  for (size_t i = 0; i < 5 + s->track_count; ++i)
    if (s->objects[i].handle == id)
      return 0;
  return 1;
}
static int hd_album_create(HdAlbumSync *s, unsigned index) {
  HdAlbumObject *o = &s->objects[index];
  if (!index) {
    if (hd_album_absent(s, o))
      return -1;
    o->parent = 0;
  } else if (index == 1 || index == 2 || index >= 5) {
    unsigned parent = index == 1 ? 0 : index == 2 ? 1 : 2;
    if (hd_album_verify(s, parent))
      return -1;
    o->parent = s->objects[parent].handle;
  } else {
    unsigned root = index == 3 ? 1 : 2;
    if (!hd_album_allowed(s) ||
        hd_first_verify(&s->wire, &s->wire.roots[root], 0)) {
      s->uncertain |= s->wire.uncertain;
      return -1;
    }
    o->parent = s->wire.roots[root].handle;
  }
  uint8_t props[8192] = {0};
  size_t n = hd_album_props(s, index, props);
  if (!n)
    return -1;
  uint32_t p[5] = {s->wire.storage, o->parent, o->format, 0, o->size},
           reply[5] = {0};
  if (hd_album_tx(s, index, MTP_OC_SendObjectPropList, p, 5, PTP_DP_SENDDATA,
                  props, n, NULL, NULL, reply, 1))
    return -1;
  if (reply[0] != s->wire.storage || reply[1] != o->parent ||
      !hd_album_new_id(s, reply[2])) {
    s->uncertain = 1;
    return -1;
  }
  o->handle = reply[2];
  if (hd_album_record(s, "created-handle", index, 0, 0, 0, NULL, 0))
    return -1;
  if (index >= 3 &&
      hd_album_tx(s, index, PTP_OC_SendObject, NULL, 0, PTP_DP_SENDDATA,
                  index >= 5 ? s->tracks[index - 5].data : NULL, o->size, NULL,
                  NULL, NULL, 1))
    return -1;
  if (hd_album_verify(s, index) || (index >= 5 && hd_album_payload(s, index))) {
    s->uncertain = 1;
    return -1;
  }
  if (hd_album_record(s, "verified-object", index, 0, 0, 0, NULL, 0))
    return -1;
  fprintf(s->out, "album-object\tindex=%u handle=%u verified\n", index,
          o->handle);
  return fflush(s->out) || ferror(s->out) ? -1 : 0;
}
static int hd_album_refs(HdAlbumSync *s, int linked) {
  uint32_t p[5] = {s->objects[4].handle}, n = 0;
  uint8_t *data = NULL;
  int rc = hd_album_tx(s, 4, MTP_OC_GetObjectReferences, p, 1, PTP_DP_GETDATA,
                       NULL, 0, &data, &n, NULL, 0);
  size_t count = linked ? s->track_count : 0;
  int good = !rc && data && n == 4 + 4 * count && hd_art_u32(data) == count;
  for (size_t i = 0; i < count && good; ++i)
    if (hd_art_u32(data + 4 + 4 * i) != s->objects[5 + i].handle)
      good = 0;
  free(data);
  return good ? 0 : -1;
}
static int hd_album_link(HdAlbumSync *s) {
  if (hd_album_verify(s, 4) || hd_album_refs(s, 0))
    return -1;
  for (size_t i = 0; i < s->track_count; ++i)
    if (hd_album_verify(s, (unsigned)i + 5))
      return -1;
  uint8_t data[4 + 4 * HD_ALBUM_TRACK_MAX];
  hd_first_u32(data, (uint32_t)s->track_count);
  for (size_t i = 0; i < s->track_count; ++i)
    hd_first_u32(data + 4 + 4 * i, s->objects[5 + i].handle);
  uint32_t p[5] = {s->objects[4].handle};
  if (hd_album_tx(s, 4, MTP_OC_SetObjectReferences, p, 1, PTP_DP_SENDDATA, data,
                  4 + 4 * s->track_count, NULL, NULL, NULL, 1) ||
      hd_album_refs(s, 1))
    return -1;
  s->links_verified = 1;
  for (size_t i = 0; i < s->track_count; ++i)
    if (hd_album_verify(s, (unsigned)i + 5))
      return -1;
  return hd_album_record(s, "album-links-verified", 4, 0, 0, 0, NULL, 0);
}
static int hd_album_cover(HdAlbumSync *s) {
  if (!s->cover)
    return 0;
  if (hd_album_verify(s, 4))
    return -1;
  size_t length = s->cover_size + 4;
  uint8_t *data = malloc(length);
  if (!data)
    return -1;
  hd_first_u32(data, (uint32_t)s->cover_size);
  memcpy(data + 4, s->cover, s->cover_size);
  uint32_t p[5] = {s->objects[4].handle, MTP_OPC_RepresentativeSampleData};
  int rc = hd_album_tx(s, 4, MTP_OC_SetObjectPropValue, p, 2, PTP_DP_SENDDATA,
                       data, length, NULL, NULL, NULL, 1);
  free(data);
  if (rc)
    return -1;
  uint32_t n = 0;
  data = NULL;
  rc = hd_album_get(s, s->objects[4].handle, MTP_OPC_RepresentativeSampleData,
                    &data, &n);
  int good = !rc && data && n == length && hd_art_u32(data) == s->cover_size &&
             !memcmp(data + 4, s->cover, s->cover_size);
  free(data);
  if (!good)
    return -1;
  s->cover_done = 1;
  return hd_album_record(s, "album-cover-verified", 4, 0, 0, 0, NULL, 0);
}
static int hd_album_id_present(const uint32_t *ids, size_t count, uint32_t id) {
  for (size_t i = 0; i < count; ++i)
    if (ids[i] == id)
      return 1;
  return 0;
}
/* Firmware-derived placeholders are observed, never deleted or renamed. */
static int hd_album_extra_verify(HdAlbumSync *s, unsigned kind) {
  HdAlbumObject *o = &s->recovery_extras[kind];
  uint8_t v[16];
  uint16_t format = kind ? MTP_OFC_AbstractArtist : MTP_OFC_AbstractAudioAlbum;
  if (!o->handle ||
      hd_album_expect_number(s, o->handle, MTP_OPC_ObjectFormat, format, 2) ||
      hd_album_expect_number(s, o->handle, MTP_OPC_StorageID, s->wire.storage,
                             4) ||
      hd_album_expect_number(s, o->handle, MTP_OPC_ParentObject, 0, 4) ||
      hd_album_same_text(s, o->handle, MTP_OPC_Name,
                         kind ? "Unknown Artist" : "Unknown Album"))
    return -1;
  uint8_t *filename = NULL;
  uint32_t length = 0;
  int rc =
      hd_album_get(s, o->handle, MTP_OPC_ObjectFileName, &filename, &length);
  int good =
      !rc && (kind ? hd_first_name_match(filename, length, "Unknown Artist.art")
                   : filename && ((length == 1 && !filename[0]) ||
                                  (length == 3 && filename[0] == 1 &&
                                   !filename[1] && !filename[2])));
  free(filename);
  if (!good)
    return -1;
  if (hd_album_value(s, o->handle, 0xdc41, v, 16) || !hd_bind_nonzero(v) ||
      (o->proven && memcmp(o->persistent, v, 16)))
    return -1;
  memcpy(o->persistent, v, 16);
  o->proven = 1;
  o->format = format;
  o->storage = s->wire.storage;
  memset(v, 0, 16);
  if (hd_album_expect(s, o->handle, 0xda97, v, 16))
    return -1;
  if (kind)
    return 0;
  if (hd_album_expect_number(s, o->handle, 0xdab9, s->recovery_extras[1].handle,
                             4))
    return -1;
  uint32_t p[5] = {o->handle}, n = 0;
  uint8_t *data = NULL;
  rc = hd_album_tx(s, UINT32_MAX, MTP_OC_GetObjectReferences, p, 1,
                   PTP_DP_GETDATA, NULL, 0, &data, &n, NULL, 0);
  good = !rc && data && n >= 4 && hd_art_u32(data) <= s->track_count &&
         n == 4 + 4 * hd_art_u32(data);
  for (unsigned i = 0; good && i < hd_art_u32(data); ++i) {
    uint32_t id = hd_art_u32(data + 4 + 4 * i);
    int found = 0;
    for (size_t j = 0; j < s->track_count; ++j)
      found |= id && id == s->objects[j + 5].handle;
    for (unsigned j = 0; j < i; ++j)
      if (id == hd_art_u32(data + 4 + 4 * j))
        found = 0;
    if (!found)
      good = 0;
  }
  free(data);
  return good ? 0 : -1;
}
static int hd_album_extras_present(HdAlbumSync *s, const uint32_t *ids,
                                   size_t count, size_t *number) {
  *number = 0;
  if (!s->extras_recorded)
    return 0;
  for (unsigned i = 0; i < 2; ++i)
    if (hd_album_id_present(ids, count, s->recovery_extras[i].handle)) {
      if (hd_album_extra_verify(s, i))
        return -1;
      ++*number;
    }
  return 0;
}
static int hd_album_closed_links(HdAlbumSync *s, const uint32_t *ids,
                                 size_t count) {
  for (size_t i = 0; i < count; ++i) {
    uint8_t v[4];
    if (hd_album_value(s, ids[i], MTP_OPC_ObjectFormat, v, 2))
      return -1;
    uint16_t format = hd_art_u16(v);
    if (format == MTP_OFC_AbstractAudioAlbum || format == MTP_OFC_MP3 ||
        format == 0x3008 || (format >= 0xb900 && format <= 0xb904)) {
      if (hd_album_value(s, ids[i], 0xdab9, v, 4))
        return -1;
      uint32_t artist = hd_art_u32(v);
      if (artist == s->recovery_extras[1].handle &&
          ids[i] != s->recovery_extras[0].handle)
        return -1;
    }
    if (format >= 0xba00 && format <= 0xbaff) {
      uint8_t *data = NULL;
      uint32_t n = 0, p[5] = {ids[i]};
      int rc = hd_album_tx(s, UINT32_MAX, MTP_OC_GetObjectReferences, p, 1,
                           PTP_DP_GETDATA, NULL, 0, &data, &n, NULL, 0);
      int good = !rc && data && n >= 4 &&
                 hd_art_u32(data) <= HD_FIRST_MAX_HANDLES &&
                 n == 4 + 4 * hd_art_u32(data);
      if (good && ids[i] != s->recovery_extras[0].handle &&
          ids[i] != s->objects[4].handle)
        for (unsigned j = 0; j < hd_art_u32(data); ++j) {
          uint32_t ref = hd_art_u32(data + 4 + 4 * j);
          for (size_t k = s->music_owned ? 0 : 1; k < 5 + s->track_count; ++k)
            if (ref && ref == s->objects[k].handle)
              good = 0;
          for (unsigned k = 0; k < 2; ++k)
            if (ref == s->recovery_extras[k].handle)
              good = 0;
        }
      free(data);
      if (!good)
        return -1;
    }
  }
  return 0;
}
static int hd_album_prefix_proof(HdAlbumSync *s, size_t through) {
  if (!hd_album_allowed(s) || through < 5 || through >= 5 + s->track_count)
    return -1;
  int bad = 0;
  uint32_t *ids = NULL;
  size_t count = 0;
  if (hd_album_handles(s, 0, 0, &ids, &count))
    return -1;
  int present[HD_ALBUM_OBJECT_MAX] = {0};
  for (size_t i = 0; i <= through; ++i)
    for (size_t j = 0; j < count; ++j)
      present[i] |= s->objects[i].handle == ids[j];
  for (unsigned i = 0; i < 3; ++i)
    if (s->wire.roots[i].handle &&
        hd_first_verify(&s->wire, &s->wire.roots[i], 0)) {
      s->uncertain |= s->wire.uncertain;
      bad = 1;
    }
  for (unsigned i = 0; i <= through && !s->uncertain && !s->failed; ++i) {
    if (!present[i]) {
      if (i == 4 && s->replace_missing_album)
        continue;
      fprintf(s->out,
              "album-inspect-missing\tindex=%u handle=%u; not adopted or "
              "recreated\n",
              i, s->objects[i].handle);
      bad = 1;
      continue;
    }
    if (hd_bind_nonzero(s->objects[i].persistent))
      s->objects[i].proven = 1;
    int identity = hd_album_verify(s, i),
        payload = i >= 5 ? hd_album_payload(s, i) : 0;
    fprintf(s->out,
            "album-inspect\tindex=%u handle=%u identity=%s payload=%s\n", i,
            s->objects[i].handle, identity ? "mismatch" : "verified",
            i < 5     ? "abstract"
            : payload ? "mismatch"
                      : "verified");
    bad |= identity || payload;
  }
  {
    size_t extra_count = 0;
    if (hd_album_extras_present(s, ids, count, &extra_count))
      bad = 1;
    if (s->extras_recorded && hd_album_closed_links(s, ids, count))
      bad = 1;
    size_t expected = s->baseline_count + through + (s->music_owned ? 1 : 0) +
                      extra_count - (s->replace_missing_album ? 1 : 0);
    if (s->replace_missing_album &&
        (present[4] || hd_album_absent(s, &s->objects[4])))
      bad = 1;
    if (count != expected)
      bad = 1;
    for (size_t i = 0; i < s->baseline_count; ++i) {
      int found = 0;
      for (size_t j = 0; j < count; ++j)
        found |= ids[j] == s->baseline[i];
      if (!found)
        bad = 1;
    }
    for (size_t i = 0; i < count; ++i) {
      int found = 0;
      for (size_t j = 0; j < s->baseline_count; ++j)
        found |= ids[i] == s->baseline[j];
      for (size_t j = s->music_owned ? 0 : 1; j <= through; ++j)
        found |= ids[i] == s->objects[j].handle;
      for (unsigned k = 0; s->extras_recorded && k < 2; ++k)
        found |= ids[i] == s->recovery_extras[k].handle;
      if (!found) {
        bad = 1;
        uint8_t format[2] = {0};
        int rc = hd_album_value(s, ids[i], MTP_OPC_ObjectFormat, format, 2);
        fprintf(s->out,
                "album-inspect-extra\thandle=%u format=0x%04x read=%s; not "
                "adopted\n",
                ids[i], hd_art_u16(format), rc ? "failed" : "ok");
      }
    }
    fprintf(s->out,
            "album-inspect-inventory\tbaseline=%zu current=%zu expected=%zu\n",
            s->baseline_count, count, expected);
  }
  free(ids);
  if (present[4] && hd_album_refs(s, 0))
    bad = 1;
  fprintf(s->out, "album-inspect-result\t%s; no mutations sent\n",
          bad ? "mismatch" : "verified-prefix");
  return bad ? -1 : 0;
}
static int hd_album_tick(HdAlbumSync *s, int network_ready) {
  if (!s || !s->prepared || !hd_album_allowed(s))
    return -1;
  if (!network_ready || s->graph_ready)
    return 0;
  if (s->resume_pending) {
    if (hd_album_prefix_proof(s, s->resume_through) ||
        hd_album_record(s, "resume-network-verified",
                        (unsigned)s->resume_through, 0, 0, 0, NULL, 0)) {
      s->failed = 1;
      return -1;
    }
    if (s->replace_missing_album) {
      if (hd_album_record(s, "replace-proven-absent-album", 4, 0, 0, 0, NULL,
                          0)) {
        s->failed = 1;
        return -1;
      }
      s->objects[4].handle = 0;
      s->objects[4].proven = 0;
      memset(s->objects[4].persistent, 0, 16);
      if (hd_album_create(s, 4)) {
        s->failed = 1;
        return -1;
      }
      s->replace_missing_album = 0;
    }
    s->resume_pending = 0;
  }
  int rc;
  if (s->phase < 5 + s->track_count)
    rc = hd_album_create(s, (unsigned)s->phase);
  else if (s->phase == 5 + s->track_count)
    rc = hd_album_link(s);
  else
    rc = hd_album_cover(s);
  if (rc) {
    s->failed = 1;
    hd_album_record(s, "stopped-preserve-journal", (unsigned)s->phase, 0, -1, 0,
                    NULL, 0);
    fprintf(s->out,
            "album-stopped\tphase=%zu; no automatic deletion or retry\n",
            s->phase);
    return -1;
  }
  ++s->phase;
  if (s->phase == 7 + s->track_count)
    s->graph_ready = 1;
  return 0;
}
static int hd_album_inventory(HdAlbumSync *s) {
  uint32_t *ids = NULL;
  size_t count = 0;
  if (hd_album_handles(s, 0, 0, &ids, &count))
    return -1;
  size_t extra_count = 0;
  int good = !hd_album_extras_present(s, ids, count, &extra_count);
  good = good && count == s->baseline_count + 4 + s->track_count +
                              (s->music_owned ? 1 : 0) + extra_count;
  for (size_t i = 0; i < s->baseline_count; ++i) {
    int found = 0;
    for (size_t j = 0; j < count; ++j)
      if (ids[j] == s->baseline[i])
        found = 1;
    if (!found)
      good = 0;
  }
  for (size_t i = 0; i < count; ++i) {
    int found = 0;
    for (size_t j = 0; j < s->baseline_count; ++j)
      if (ids[i] == s->baseline[j])
        found = 1;
    for (size_t j = s->music_owned ? 0 : 1; j < 5 + s->track_count; ++j)
      if (ids[i] == s->objects[j].handle)
        found = 1;
    for (unsigned k = 0; s->extras_recorded && k < 2; ++k)
      found |= ids[i] == s->recovery_extras[k].handle;
    if (!found)
      good = 0;
  }
  free(ids);
  return good ? 0 : -1;
}
static int hd_album_complete(HdAlbumSync *s, int clean, int uncertain,
                             unsigned background, int network_success) {
  if (uncertain)
    s->uncertain = 1;
  if (!s->graph_ready || !clean || !background || !network_success ||
      !hd_album_allowed(s)) {
    fprintf(s->out, "album-retain\tunconfirmed; keep journal; no deletion\n");
    return -1;
  }
  for (size_t i = 0; i < 5 + s->track_count; ++i)
    if (hd_album_verify(s, (unsigned)i) ||
        (i >= 5 && hd_album_payload(s, (unsigned)i)))
      goto failed;
  if (hd_album_refs(s, 1) || hd_album_inventory(s) ||
      (s->cover && !s->cover_done) ||
      hd_album_record(s, "retained-complete-album", UINT32_MAX, 0, 0, 0, NULL,
                      0))
    goto failed;
  s->retained = 1;
  if (hd_album_tx(s, UINT32_MAX, 0x9108, NULL, 0, PTP_DP_NODATA, NULL, 0, NULL,
                  NULL, NULL, 1))
    goto failed;
  if (hd_album_record(s, "retained-handoff-confirmed", UINT32_MAX, 0, 0, 0,
                      NULL, 0))
    goto failed;
  fprintf(s->out,
          "album-retained\ttracks=%zu; exact payloads/references verified; "
          "background acknowledged; handoff confirmed\n",
          s->track_count);
  return fflush(s->out) || ferror(s->out) ? -1 : 0;
failed:
  fprintf(s->out, "album-retain\tfinal proof or handoff failed; all created "
                  "objects preserved; keep journal\n");
  return -1;
}
#endif
