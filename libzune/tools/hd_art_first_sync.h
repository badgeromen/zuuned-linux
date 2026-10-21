/* Private first-sync experiment. Existing library objects are never reused.
 * Independent C encoding informed by pinned Xune
 * CreateArtistMetadata/CreateTrack. Journal intent is durable before EVERY
 * write; unknown outcomes stop all PTP. */
#ifndef ZUNETOOL_HD_ART_FIRST_SYNC_H
#define ZUNETOOL_HD_ART_FIRST_SYNC_H
#include "hd_art_journal.h"
#include "hd_cli_teardown.h"
#include <signal.h>
#define HD_FIRST_MAX_BYTES (1024u * 1024u)
#define HD_FIRST_MAX_HANDLES 16384u

typedef struct {
  uint32_t handle, storage, size, artist, parent;
  char name[64], filename[160];
  uint16_t format;
  uint8_t persistent[16];
  int proven, deleted;
} HdFirstObject;
typedef struct {
  ZuneDevice *dev;
  FILE *out;
  volatile sig_atomic_t *stop;
  int fd, prepared, started, phase, uncertain, finished;
  int retain_requested, retained;
  uint32_t fixture_duration_ms;
  char name[64], serial[256];
  uint32_t storage;
  uint32_t *baseline;
  size_t baseline_count;
  const uint8_t *mp3;
  size_t mp3_size;
  int64_t deadline;
  int64_t (*now_fn)(void);
  HdFirstObject objects[5], roots[3];
  int refs_set, create_music, music_owned;
  HdFirstObject recovery_extras[2]; /* Observed firmware placeholders; never
                                       deleted here. */
  int recovery_retained_graph;
  int recovery_complete_graph; /* Strict completed/link history only. */
  int recovery_delete_guard; /* Separate recovery obeys stop at final write. */
} HdFirstSync;
static inline void hd_first_u16(uint8_t *p, uint16_t v) {
  p[0] = v;
  p[1] = v >> 8;
}
static inline void hd_first_u32(uint8_t *p, uint32_t v) {
  hd_first_u16(p, v);
  hd_first_u16(p + 2, v >> 16);
}
static int hd_first_write_all(int fd, const void *buf, size_t n) {
  const uint8_t *p = buf;
  while (n) {
    ssize_t wrote = write(fd, p, n);
    if (wrote < 0 && errno == EINTR)
      continue;
    if (wrote <= 0)
      return -1;
    p += wrote;
    n -= (size_t)wrote;
  }
  return 0;
}
typedef struct {
  char magic[8], stage[48], name[64], serial[256];
  uint32_t storage, bytes;
  uint8_t guid[16];
  HdFirstObject objects[5], roots[3];
  int refs_set, create_music, music_owned;
} HdFirstRecord;

typedef struct {
  char magic[8];
  uint16_t opcode, response;
  int result, count;
  uint32_t parameters[5];
} HdFirstTransactionRecord;
/* Fixed native-ABI records are deliberately private, with no serial in stdout.
 * Every record is self-contained and includes the exact expected identities. */
static int hd_first_record(HdFirstSync *s, const char *stage) {
  HdFirstRecord r = {0};
  memcpy(r.magic, "ZUHDF1", 6);
  snprintf(r.stage, sizeof(r.stage), "%s", stage);
  memcpy(r.name, s->name, sizeof(r.name));
  memcpy(r.serial, s->serial, sizeof(r.serial));
  r.storage = s->storage;
  r.bytes = (uint32_t)s->mp3_size;
  memcpy(r.guid, hd_art_test_guid_wire, 16);
  memcpy(r.objects, s->objects, sizeof(r.objects));
  memcpy(r.roots, s->roots, sizeof(r.roots));
  r.refs_set = s->refs_set;
  r.create_music = s->create_music;
  r.music_owned = s->music_owned;
  if (s->fd < 0 || hd_first_write_all(s->fd, &r, sizeof(r)) || fsync(s->fd)) {
    if (s->started)
      s->uncertain = 1;
    fprintf(s->out, "first-sync-journal\tfailed; no further writes\n");
    return -1;
  }
  return 0;
}
static int hd_first_identity(HdFirstSync *s) {
  return s->dev && s->dev->device_family == ZUNE_FAMILY_PAVO &&
         s->dev->mtpz_authenticated && !ZUNE_FLAG_GET(s->dev->disconnecting) &&
         !ZUNE_FLAG_GET(s->dev->cancel_requested) && s->dev->serial &&
         !strcmp(s->serial, s->dev->serial) && s->storage == s->dev->storage_id;
}
static int hd_first_allowed(HdFirstSync *s, int cleanup) {
  if (!hd_first_identity(s) || s->uncertain)
    return 0;
  if (cleanup)
    return 1; /* rollback ignores signal/deadline, never device abort */
  int64_t now = s->now_fn ? s->now_fn() : -1;
  return !(s->stop && *s->stop) && now >= 0 && s->deadline > now;
}
static int hd_first_tx(HdFirstSync *s, uint16_t op, uint32_t *p, int count,
                       uint16_t flags, uint8_t *send, size_t size,
                       uint8_t **data, uint32_t *length, uint32_t *reply,
                       int expected, int mutation, int cleanup) {
  if (!hd_first_allowed(s, cleanup))
    return -1;
  uint16_t rc = 0;
  uint32_t values[5] = {0};
  int n = 0;
  int result = ptp_transaction(&s->dev->ptp, op, p, count, flags, send, size,
                               data, length, &rc, values, &n);
  /* Persist raw create outcome before interpreting it, even malformed success.
   * The initialized five slots are bounded regardless of reported count. */
  if (op == MTP_OC_SendObjectPropList) {
    HdFirstTransactionRecord record = {0};
    memcpy(record.magic, "ZUHDT1", 6);
    record.opcode = op;
    record.response = rc;
    record.result = result;
    record.count = n;
    memcpy(record.parameters, values, sizeof(values));
    if (s->fd < 0 || hd_first_write_all(s->fd, &record, sizeof(record)) ||
        fsync(s->fd)) {
      s->uncertain = 1;
      return -1;
    }
    fprintf(s->out,
            "first-sync-create-response\tresponse=0x%04X result=%d count=%d "
            "parameters=%u,%u,%u,%u,%u\n",
            rc, result, n, values[0], values[1], values[2], values[3],
            values[4]);
  }
  /* Pavo CleanDataStore accepted with one uninterpreted response parameter.
   * Keep all other fixed-shape transactions strict. */
  if (op == 0x9108) {
    fprintf(s->out,
            "first-sync-handoff-response\tresponse=0x%04X result=%d count=%d",
            rc, result, n);
    for (int i = 0; i < n && i < 5; ++i)
      fprintf(s->out, " raw-parameter-%d=0x%08X", i + 1, values[i]);
    fputc('\n', s->out);
  }
  int shape = op == MTP_OC_SendObjectPropList ? n >= 3 && n <= 5
              : op == 0x9108                  ? n >= 0 && n <= 1
                                              : n == expected;
  if (result || rc != PTP_RC_OK || !shape) {
    fprintf(s->out,
            "first-sync-response\topcode=0x%04X response=0x%04X result=%d "
            "parameters=%d\n",
            op, rc, result, n);
    if (hd_cli_transaction_uncertain(result, rc) || n < 0 || n > 5 ||
        (rc == PTP_RC_OK && !shape) ||
        (mutation && !hd_bind_explicit_refusal(rc)))
      s->uncertain = 1;
    return -1;
  }
  if (reply)
    memcpy(reply, values, sizeof(values));
  return 0;
}
static int hd_first_get(HdFirstSync *s, uint32_t id, uint16_t prop,
                        uint8_t **data, uint32_t *n, int cleanup) {
  uint32_t p[2] = {id, prop};
  *data = NULL;
  *n = 0;
  return hd_first_tx(s, MTP_OC_GetObjectPropValue, p, 2, PTP_DP_GETDATA, NULL,
                     0, data, n, NULL, 0, 0, cleanup);
}
static int hd_first_children(HdFirstSync *s, uint16_t format, uint32_t parent,
                             uint32_t **ids, size_t *count, int cleanup) {
  uint8_t *data = NULL;
  uint32_t len = 0, p[3] = {s->storage, format, parent};
  *ids = NULL;
  *count = 0;
  if (hd_first_tx(s, PTP_OC_GetObjectHandles, p, 3, PTP_DP_GETDATA, NULL, 0,
                  &data, &len, NULL, 0, 0, cleanup)) {
    free(data);
    return -1;
  }
  if (!data || len < 4 || hd_art_u32(data) > HD_FIRST_MAX_HANDLES ||
      len != 4u + 4u * hd_art_u32(data)) {
    free(data);
    return -1;
  }
  size_t n = hd_art_u32(data);
  uint32_t *out = calloc(n ? n : 1, sizeof(*out));
  if (!out) {
    free(data);
    return -1;
  }
  for (size_t i = 0; i < n; ++i) {
    out[i] = hd_art_u32(data + 4 + 4 * i);
    if (!out[i] || out[i] == UINT32_MAX) {
      free(out);
      free(data);
      return -1;
    }
    for (size_t j = 0; j < i; ++j)
      if (out[j] == out[i]) {
        free(out);
        free(data);
        return -1;
      }
  }
  free(data);
  *ids = out;
  *count = n;
  return 0;
}
static int hd_first_handles(HdFirstSync *s, uint16_t format, uint32_t **ids,
                            size_t *count, int cleanup) {
  return hd_first_children(s, format, 0, ids, count, cleanup);
}
static int hd_first_name_match(const uint8_t *data, size_t n,
                               const char *text) {
  size_t used = 0;
  return data && !hd_art_string(data, n, &used) && used == n &&
         hd_bind_name_matches(data, (uint16_t)n, text);
}
enum {
  HD_FIRST_ARTIST_FOLDER,
  HD_FIRST_ALBUM_FOLDER,
  HD_FIRST_ARTIST,
  HD_FIRST_ALBUM,
  HD_FIRST_TRACK
};
static int hd_first_value(HdFirstSync *s, uint32_t id, uint16_t prop, void *out,
                          size_t wanted, int cleanup) {
  uint8_t *data = NULL;
  uint32_t n = 0;
  if (hd_first_get(s, id, prop, &data, &n, cleanup)) {
    free(data);
    return -1;
  }
  int good = data && n == wanted;
  if (good)
    memcpy(out, data, n);
  free(data);
  return good ? 0 : -1;
}
static int hd_first_verify(HdFirstSync *s, HdFirstObject *o, int cleanup) {
  HdFirstObject read = *o;
  uint8_t data[16];
  const uint16_t properties[] = {MTP_OPC_ObjectFormat, MTP_OPC_StorageID,
                                 MTP_OPC_ParentObject};
  const uint32_t expected[] = {o->format, s->storage, o->parent};
  for (unsigned i = 0; i < 3; ++i) {
    if (hd_first_value(s, o->handle, properties[i], data, i ? 4 : 2, cleanup)) {
      fprintf(s->out,
              "first-sync-verification\thandle=%u property=%04x read failed\n",
              o->handle, properties[i]);
      return -1;
    }
    uint32_t actual = i ? hd_art_u32(data) : hd_art_u16(data);
    if (actual != expected[i]) {
      fprintf(s->out,
              "first-sync-verification\thandle=%u property=%04x expected=%u "
              "actual=%u\n",
              o->handle, properties[i], expected[i], actual);
      return -1;
    }
  }
  for (unsigned i = 0; i < (o->format == MTP_OFC_Association ? 1u : 2u); ++i) {
    uint8_t *text = NULL;
    uint32_t n = 0;
    if (hd_first_get(s, o->handle, i ? MTP_OPC_Name : MTP_OPC_ObjectFileName,
                     &text, &n, cleanup)) {
      free(text);
      return -1;
    }
    int good = hd_first_name_match(text, n, i ? o->name : o->filename);
    if (!good && !i && o->format == MTP_OFC_MP3) {
      char supplied[160], normalized[160];
      snprintf(supplied, sizeof(supplied), "%.63s.mp3", o->name);
      snprintf(normalized, sizeof(normalized), "01 %.63s.mp3", o->name);
      /* Pavo hardware 2026-10-18: this Track=1 fixture retains exact Name,
       * parent, artist, size and payload but prefixes the device filename. */
      if (!strcmp(o->filename, supplied) &&
          hd_first_name_match(text, n, normalized)) {
        snprintf(read.filename, sizeof(read.filename), "%s", normalized);
        good = 1;
        fprintf(s->out, "first-sync-normalization\thandle=%u filename=%s\n",
                o->handle, normalized);
      }
    }
    if (!good) {
      fprintf(s->out,
              "first-sync-verification\thandle=%u property=%04x expected=%s "
              "actual-wire=",
              o->handle, i ? MTP_OPC_Name : MTP_OPC_ObjectFileName,
              i ? o->name : o->filename);
      for (uint32_t k = 0; text && k < n && k < 512; ++k)
        fprintf(s->out, "%02x", text[k]);
      fputc('\n', s->out);
    }
    free(text);
    if (!good)
      return -1;
  }
  if (hd_first_value(s, o->handle, 0xdc41, read.persistent, 16, cleanup) ||
      !hd_bind_nonzero(read.persistent))
    return -1;
  if (o->proven && memcmp(read.persistent, o->persistent, 16))
    return -1;
  if (o->format == MTP_OFC_AbstractArtist) {
    if (hd_first_value(s, o->handle, 0xda97, data, 16, cleanup) ||
        memcmp(data, hd_art_test_guid_wire, 16))
      return -1;
  } else if (o->format == MTP_OFC_AbstractAudioAlbum ||
             o->format == MTP_OFC_MP3) {
    if (hd_first_value(s, o->handle, 0xdab9, data, 4, cleanup) ||
        hd_art_u32(data) != s->objects[HD_FIRST_ARTIST].handle)
      return -1;
    read.artist = s->objects[HD_FIRST_ARTIST].handle;
  }
  if (o->format == MTP_OFC_MP3) {
    uint8_t *v = NULL;
    uint32_t n = 0;
    if (hd_first_get(s, o->handle, MTP_OPC_ObjectSize, &v, &n, cleanup)) {
      free(v);
      return -1;
    }
    int good = v && (n == 4 || n == 8) && hd_art_u32(v) == s->mp3_size &&
               (n == 4 || !hd_art_u32(v + 4));
    free(v);
    if (!good)
      return -1;
    read.size = (uint32_t)s->mp3_size;
    if (s->fixture_duration_ms &&
        (hd_first_value(s, o->handle, MTP_OPC_Duration, data, 4, cleanup) ||
         hd_art_u32(data) != s->fixture_duration_ms)) {
      fprintf(s->out,
              "first-sync-verification\thandle=%u duration expected=%u "
              "readback mismatch\n",
              o->handle, s->fixture_duration_ms);
      return -1;
    }
  }
  read.proven = 1;
  *o = read;
  return 0;
}
static void hd_first_configure(HdFirstSync *s) {
  static const char *roots[] = {"Music", "Artists", "Albums"};
  for (unsigned i = 0; i < 3; ++i) {
    s->roots[i].format = MTP_OFC_Association;
    s->roots[i].storage = s->storage;
    snprintf(s->roots[i].name, 64, "%s", roots[i]);
    snprintf(s->roots[i].filename, 160, "%s", roots[i]);
  }
  for (unsigned i = 0; i < 5; ++i) {
    HdFirstObject *o = &s->objects[i];
    o->storage = s->storage;
    snprintf(o->name, sizeof(o->name), "%.57s%s", s->name,
             (i == HD_FIRST_ALBUM_FOLDER || i == HD_FIRST_ALBUM) ? " Album"
                                                                 : "");
    if (i < 2) {
      o->format = MTP_OFC_Association;
      snprintf(o->filename, sizeof(o->filename), "%s", o->name);
    }
    if (i == HD_FIRST_ARTIST) {
      o->format = MTP_OFC_AbstractArtist;
      snprintf(o->filename, sizeof(o->filename), "%.63s.art", o->name);
    }
    if (i == HD_FIRST_ALBUM) {
      o->format = MTP_OFC_AbstractAudioAlbum;
      snprintf(o->filename, sizeof(o->filename), "%.63s--%.63s.alb", s->name,
               o->name);
    }
    if (i == HD_FIRST_TRACK) {
      o->format = MTP_OFC_MP3;
      snprintf(o->filename, sizeof(o->filename), "%.63s.mp3", o->name);
      o->size = (uint32_t)s->mp3_size;
    }
  }
}
static int hd_first_roots(HdFirstSync *s) {
  uint32_t *ids = NULL;
  size_t count = 0;
  if (hd_first_handles(s, MTP_OFC_Association, &ids, &count, 0))
    return -1;
  for (size_t i = 0; i < count; ++i) {
    uint8_t *name = NULL, parent[4];
    uint32_t n = 0;
    if (hd_first_get(s, ids[i], MTP_OPC_ObjectFileName, &name, &n, 0) ||
        hd_first_value(s, ids[i], MTP_OPC_ParentObject, parent, 4, 0)) {
      free(name);
      free(ids);
      return -1;
    }
    size_t used = 0;
    if (!name || hd_art_string(name, n, &used) || used != n) {
      free(name);
      free(ids);
      return -1;
    }
    if (hd_art_u32(parent) == 0) {
      fprintf(s->out, "first-sync-root-folder\thandle=%u name=", ids[i]);
      for (size_t c = 1; c + 2 < n; c += 2) {
        uint16_t ch = hd_art_u16(name + c);
        fputc(ch >= 32 && ch < 127 ? (int)ch : '?', s->out);
      }
      fputc('\n', s->out);
    }
    for (unsigned r = 0; r < 3; ++r)
      if (hd_art_u32(parent) == 0 &&
          hd_first_name_match(name, n, s->roots[r].filename)) {
        if (s->roots[r].handle) {
          free(name);
          free(ids);
          fprintf(s->out, "first-sync-roots\tambiguous %s\n",
                  s->roots[r].filename);
          return -1;
        }
        s->roots[r].handle = ids[i];
      }
    free(name);
  }
  free(ids);
  int missing = 0;
  for (unsigned r = 0; r < 3; ++r) {
    fprintf(s->out, "first-sync-root\t%s handle=%u\n", s->roots[r].filename,
            s->roots[r].handle);
    if (!s->roots[r].handle) {
      if (r == 0) {
        s->create_music = 1;
        fprintf(s->out, "first-sync-root-plan\tMusic absent; create tracked "
                        "test-owned root only after network readiness\n");
      } else
        missing = 1;
    } else if (hd_first_verify(s, &s->roots[r], 0))
      return -1;
  }
  return missing ? -1 : 0;
}
static int hd_first_absent_names(HdFirstSync *s, HdFirstObject *o) {
  uint32_t *ids = NULL;
  size_t count = 0;
  if (hd_first_handles(s, o->format, &ids, &count, 0))
    return -1;
  for (size_t i = 0; i < count; ++i)
    for (unsigned field = 0;
         field < (o->format == MTP_OFC_Association ? 1u : 2u); ++field) {
      uint8_t *name = NULL;
      uint32_t n = 0;
      if (hd_first_get(s, ids[i], field ? MTP_OPC_Name : MTP_OPC_ObjectFileName,
                       &name, &n, 0)) {
        free(name);
        free(ids);
        return -1;
      }
      size_t used = 0;
      int bad = !name || hd_art_string(name, n, &used) || used != n;
      int match = hd_first_name_match(name, n, field ? o->name : o->filename);
      free(name);
      if (bad || match) {
        free(ids);
        return -1;
      }
    }
  free(ids);
  return 0;
}
static int hd_first_new_handle(HdFirstSync *s, uint32_t id) {
  if (!id || id == UINT32_MAX)
    return 0;
  for (unsigned r = 0; r < 3; ++r)
    if (s->roots[r].handle == id)
      return 0;
  for (unsigned i = 0; i < 5; ++i)
    if (s->objects[i].handle == id)
      return 0;
  for (size_t i = 0; i < s->baseline_count; ++i)
    if (s->baseline[i] == id)
      return 0;
  return 1;
}
static void hd_first_prop(uint8_t *buf, size_t *pos, uint16_t prop,
                          uint16_t type, const void *data, size_t n) {
  hd_first_u32(buf + *pos, 0);
  hd_first_u16(buf + *pos + 4, prop);
  hd_first_u16(buf + *pos + 6, type);
  *pos += 8;
  memcpy(buf + *pos, data, n);
  *pos += n;
}
static void hd_first_str(uint8_t *buf, size_t *pos, uint16_t prop,
                         const char *text) {
  uint8_t wire[512] = {0};
  size_t n = strlen(text);
  wire[0] = (uint8_t)(n + 1);
  for (size_t i = 0; i < n; ++i)
    hd_first_u16(wire + 1 + 2 * i, (uint8_t)text[i]);
  hd_first_prop(buf, pos, prop, PTP_DTC_STR, wire, 3 + 2 * n);
}
static size_t hd_first_props(HdFirstSync *s, unsigned kind, uint8_t buf[2048]) {
  HdFirstObject *o = &s->objects[kind];
  size_t pos = 4;
  uint8_t v[16] = {0};
  if (kind < 2) {
    hd_first_u32(buf, 1);
    hd_first_str(buf, &pos, MTP_OPC_ObjectFileName, o->filename);
    return pos;
  }
  hd_first_u32(buf, kind == HD_FIRST_ARTIST  ? 4
                    : kind == HD_FIRST_ALBUM ? 6
                                             : 13);
  hd_first_prop(buf, &pos, 0xdab0, PTP_DTC_UINT8, v, 1);
  hd_first_str(buf, &pos, MTP_OPC_ObjectFileName, o->filename);
  hd_first_str(buf, &pos, MTP_OPC_Name, o->name);
  if (kind == HD_FIRST_ARTIST) {
    hd_first_prop(buf, &pos, 0xda97, HD_ART_UINT128, hd_art_test_guid_wire, 16);
    return pos;
  }
  hd_first_str(buf, &pos, MTP_OPC_Artist, s->name);
  hd_first_u32(v, s->objects[HD_FIRST_ARTIST].handle);
  hd_first_prop(buf, &pos, 0xdab9, PTP_DTC_UINT32, v, 4);
  if (kind == HD_FIRST_ALBUM) {
    hd_first_str(buf, &pos, 0xdc47, "20261018T000000.0");
    return pos;
  }
  hd_first_str(buf, &pos, MTP_OPC_AlbumName, s->objects[HD_FIRST_ALBUM].name);
  hd_first_str(buf, &pos, 0xdc9b, s->name);
  hd_first_u16(v, 1);
  hd_first_prop(buf, &pos, 0xdc95, PTP_DTC_UINT16, v, 2);
  hd_first_u16(v, 0);
  hd_first_prop(buf, &pos, 0xdc9d, PTP_DTC_UINT16, v, 2);
  hd_first_prop(buf, &pos, 0xdab2, PTP_DTC_UINT8, v, 1);
  hd_first_u32(v, 1);
  hd_first_prop(buf, &pos, 0xdab8, PTP_DTC_UINT32, v, 4);
  hd_first_u32(v, s->fixture_duration_ms ? s->fixture_duration_ms : 3000);
  hd_first_prop(buf, &pos, MTP_OPC_Duration, PTP_DTC_UINT32, v, 4);
  hd_first_u16(v, 1);
  hd_first_prop(buf, &pos, MTP_OPC_Track, PTP_DTC_UINT16, v, 2);
  return pos;
}
static int hd_first_create(HdFirstSync *s, unsigned kind) {
  HdFirstObject *o = &s->objects[kind];
  if (kind == HD_FIRST_ARTIST_FOLDER) {
    if (hd_first_verify(s, &s->roots[0], 0))
      return -1;
    o->parent = s->roots[0].handle;
  } else if (kind == HD_FIRST_ALBUM_FOLDER) {
    if (hd_first_verify(s, &s->objects[0], 0))
      return -1;
    o->parent = s->objects[0].handle;
  } else if (kind == HD_FIRST_ARTIST) {
    if (hd_first_verify(s, &s->roots[1], 0))
      return -1;
    o->parent = s->roots[1].handle;
  } else if (kind == HD_FIRST_ALBUM) {
    if (hd_first_verify(s, &s->roots[2], 0))
      return -1;
    o->parent = s->roots[2].handle;
  } else {
    if (hd_first_verify(s, &s->objects[1], 0))
      return -1;
    o->parent = s->objects[1].handle;
  }
  uint8_t props[2048] = {0};
  size_t n = hd_first_props(s, kind, props);
  char stage[48];
  snprintf(stage, sizeof(stage), "intent-create-kind-%u", kind);
  if (hd_first_record(s, stage) || !hd_first_allowed(s, 0))
    return -1;
  uint32_t p[5] = {s->storage, o->parent, o->format, 0,
                   kind == HD_FIRST_TRACK ? (uint32_t)s->mp3_size : 0},
           reply[5] = {0};
  s->started = 1;
  if (hd_first_tx(s, MTP_OC_SendObjectPropList, p, 5, PTP_DP_SENDDATA, props, n,
                  NULL, NULL, reply, 3, 1, 0))
    return -1;
  if (reply[0] != s->storage || reply[1] != o->parent ||
      !hd_first_new_handle(s, reply[2])) {
    s->uncertain = 1;
    hd_first_record(s, "unknown-create-response");
    return -1;
  }
  o->handle = reply[2];
  if (hd_first_record(s, "created-handle-returned"))
    return -1;
  if (kind >= HD_FIRST_ARTIST) {
    if (hd_first_record(s, "intent-object-commit") || !hd_first_allowed(s, 0)) {
      s->uncertain = 1;
      return -1;
    }
    if (hd_first_tx(s, PTP_OC_SendObject, NULL, 0, PTP_DP_SENDDATA,
                    kind == HD_FIRST_TRACK ? (uint8_t *)s->mp3 : NULL,
                    kind == HD_FIRST_TRACK ? s->mp3_size : 0, NULL, NULL, NULL,
                    0, 1, 0)) {
      s->uncertain = 1;
      hd_first_record(s, "unknown-object-commit");
      return -1;
    }
  }
  if (hd_first_verify(s, o, 1)) {
    s->uncertain = 1;
    hd_first_record(s, "unverified-created-object");
    return -1;
  }
  if (kind == HD_FIRST_TRACK) {
    uint8_t *data = NULL;
    uint32_t len = 0, id = o->handle;
    if (hd_first_tx(s, PTP_OC_GetObject, &id, 1, PTP_DP_GETDATA, NULL, 0, &data,
                    &len, NULL, 0, 0, 1) ||
        !data || len != s->mp3_size || memcmp(data, s->mp3, len)) {
      free(data);
      s->uncertain = 1;
      hd_first_record(s, "unverified-track-payload");
      return -1;
    }
    free(data);
  }
  if (hd_first_record(s, "created-identity-verified"))
    return -1;
  fprintf(s->out, "first-sync-created\tkind=%u handle=%u verified\n", kind,
          o->handle);
  fflush(s->out);
  return 0;
}
static int hd_first_refs_check(HdFirstSync *s, int expected, int cleanup) {
  uint32_t id = s->objects[HD_FIRST_ALBUM].handle;
  uint8_t *data = NULL;
  uint32_t n = 0;
  if (hd_first_tx(s, MTP_OC_GetObjectReferences, &id, 1, PTP_DP_GETDATA, NULL,
                  0, &data, &n, NULL, 0, 0, cleanup)) {
    free(data);
    return -1;
  }
  int good =
      data && n == (expected ? 8u : 4u) &&
      hd_art_u32(data) == (unsigned)expected &&
      (!expected || hd_art_u32(data + 4) == s->objects[HD_FIRST_TRACK].handle);
  if (!good)
    fprintf(s->out,
            "first-sync-reference-mismatch\thandle=%u expected=%d bytes=%u "
            "actual-count=%u first-reference=%u\n",
            id, expected, n, data && n >= 4 ? hd_art_u32(data) : UINT32_MAX,
            data && n >= 8 ? hd_art_u32(data + 4) : 0);
  free(data);
  return good ? 0 : -1;
}
static int hd_first_refs_set(HdFirstSync *s, int present, int cleanup) {
  if (hd_first_verify(s, &s->objects[HD_FIRST_ALBUM], cleanup) ||
      hd_first_refs_check(s, s->refs_set, cleanup))
    return -1;
  if (hd_first_record(s, present ? "intent-link-album-track"
                                 : "intent-clear-album-track") ||
      !hd_first_allowed(s, cleanup))
    return -1;
  uint32_t id = s->objects[HD_FIRST_ALBUM].handle;
  uint8_t data[8] = {0};
  hd_first_u32(data, present);
  hd_first_u32(data + 4, s->objects[HD_FIRST_TRACK].handle);
  if (hd_first_tx(s, MTP_OC_SetObjectReferences, &id, 1, PTP_DP_SENDDATA, data,
                  present ? 8 : 4, NULL, NULL, NULL, 0, 1, cleanup))
    return -1;
  s->refs_set = present;
  if (hd_first_refs_check(s, present, 1)) {
    s->uncertain = 1;
    return -1;
  }
  return hd_first_record(s, present ? "album-track-link-verified"
                                    : "album-track-clear-verified");
}
static int hd_first_create_music(HdFirstSync *s) {
  /* Re-prove absence immediately before intent. Never adopt a new root that
   * appeared after preflight, and never create the other shared folders. */
  uint32_t *ids = NULL;
  size_t count = 0;
  if (hd_first_handles(s, MTP_OFC_Association, &ids, &count, 0))
    return -1;
  for (size_t i = 0; i < count; ++i) {
    uint8_t parent[4], *name = NULL;
    uint32_t n = 0;
    if (hd_first_value(s, ids[i], MTP_OPC_ParentObject, parent, 4, 0) ||
        hd_first_get(s, ids[i], MTP_OPC_ObjectFileName, &name, &n, 0)) {
      free(name);
      free(ids);
      return -1;
    }
    size_t used = 0;
    int invalid = !name || hd_art_string(name, n, &used) || used != n;
    int collision =
        !hd_art_u32(parent) && hd_first_name_match(name, n, "Music");
    free(name);
    if (invalid || collision) {
      free(ids);
      return -1;
    }
  }
  free(ids);
  uint8_t data[64] = {0};
  size_t size = 4;
  hd_first_u32(data, 1);
  hd_first_str(data, &size, MTP_OPC_ObjectFileName, "Music");
  if (hd_first_record(s, "intent-create-owned-Music-root") ||
      !hd_first_allowed(s, 0))
    return -1;
  uint32_t p[5] = {s->storage, 0, MTP_OFC_Association, 0, 0}, reply[5] = {0};
  s->started = 1;
  if (hd_first_tx(s, MTP_OC_SendObjectPropList, p, 5, PTP_DP_SENDDATA, data,
                  size, NULL, NULL, reply, 3, 1, 0))
    return -1;
  if (reply[0] != s->storage || reply[1] || !hd_first_new_handle(s, reply[2])) {
    s->uncertain = 1;
    hd_first_record(s, "unknown-owned-root-response");
    return -1;
  }
  s->roots[0].handle = reply[2];
  s->music_owned = 1;
  if (hd_first_record(s, "owned-Music-root-handle-returned"))
    return -1;
  if (hd_first_verify(s, &s->roots[0], 1)) {
    s->uncertain = 1;
    hd_first_record(s, "unverified-owned-Music-root");
    return -1;
  }
  if (hd_first_record(s, "owned-Music-root-identity-verified"))
    return -1;
  fprintf(s->out, "first-sync-created\towned-Music-root handle=%u verified\n",
          s->roots[0].handle);
  return 0;
}
static int hd_first_tick(HdFirstSync *s, int ready) {
  if (!s || !ready)
    return 0;
  if (!s->prepared || !hd_first_allowed(s, 0))
    return -1;
  if (s->create_music && !s->music_owned)
    return hd_first_create_music(s);
  if (s->phase >= 6)
    return 0;
  if (s->phase < 5) {
    if (hd_first_create(s, (unsigned)s->phase))
      return -1;
  } else if (hd_first_refs_set(s, 1, 0))
    return -1;
  ++s->phase;
  return 0;
}
static int hd_first_delete(HdFirstSync *s, HdFirstObject *o) {
  if (!o->handle)
    return 0;
  if (!o->proven || o->deleted || hd_first_verify(s, o, 1))
    return -1;
  if (o->format == MTP_OFC_Association) {
    uint32_t *ids = NULL;
    size_t count = 0;
    if (hd_first_children(s, 0, o->handle, &ids, &count, 1)) {
      free(ids);
      return -1;
    }
    free(ids);
    if (count)
      return -1;
  }
  if (o->format == MTP_OFC_AbstractAudioAlbum && hd_first_refs_check(s, 0, 1))
    return -1;
  if (hd_first_record(s, "intent-delete-proven-object") ||
      !hd_first_allowed(s, !s->recovery_delete_guard))
    return -1;
  uint32_t p[2] = {o->handle, 0};
  if (hd_first_tx(s, PTP_OC_DeleteObject, p, 2, PTP_DP_NODATA, NULL, 0, NULL,
                  NULL, NULL, 0, 1, !s->recovery_delete_guard))
    return -1;
  uint32_t *ids = NULL;
  size_t n = 0;
  if (hd_first_handles(s, 0, &ids, &n, 1))
    return -1;
  for (size_t i = 0; i < n; ++i)
    if (ids[i] == o->handle) {
      free(ids);
      return -1;
    }
  free(ids);
  o->deleted = 1;
  return hd_first_record(s, "object-deletion-verified");
}
static int hd_first_finish(HdFirstSync *s, int clean, int uncertain) {
  if (!s)
    return 0;
  if (uncertain)
    s->uncertain = 1;
  int any = s->music_owned && s->roots[0].handle;
  for (unsigned i = 0; i < 5; ++i)
    if (s->objects[i].handle)
      any = 1;
  if (!any)
    return s->uncertain ? -1 : 0;
  if (s->finished || !clean || !hd_first_allowed(s, 1)) {
    fprintf(s->out,
            "first-sync-recovery\tkeep manifest; cleanup not attempted\n");
    return -1;
  }
  s->finished = 1;
  if (s->music_owned &&
      (!s->roots[0].proven || hd_first_verify(s, &s->roots[0], 1)))
    goto failed;
  /* Validate every created identity before the first destructive cleanup. */
  for (unsigned i = 0; i < 5; ++i)
    if (s->objects[i].handle &&
        (!s->objects[i].proven || hd_first_verify(s, &s->objects[i], 1)))
      goto failed;
  if (s->objects[HD_FIRST_ALBUM].handle) {
    if (hd_first_refs_check(s, s->refs_set, 1))
      goto failed;
    if (s->refs_set && hd_first_refs_set(s, 0, 1))
      goto failed;
  }
  for (int i = 4; i >= 0; --i)
    if (hd_first_delete(s, &s->objects[i]))
      goto failed;
  if (s->music_owned && hd_first_delete(s, &s->roots[0]))
    goto failed;
  uint32_t *remaining = NULL;
  size_t count = 0;
  if (hd_first_handles(s, 0, &remaining, &count, 1))
    goto failed;
  int same = count == s->baseline_count;
  for (size_t i = 0; i < count && same; ++i) {
    int found = 0;
    for (size_t j = 0; j < s->baseline_count; ++j)
      if (remaining[i] == s->baseline[j])
        found = 1;
    if (!found)
      same = 0;
  }
  free(remaining);
  if (!same) {
    hd_first_record(s, "created-objects-absent-inventory-changed");
    fprintf(s->out, "first-sync-recovery\tcreated objects absent; other "
                    "inventory changed, no additional deletion\n");
    return -1;
  }
  if (hd_first_record(s, "baseline-inventory-restored"))
    goto failed;
  fprintf(s->out, "first-sync-cleanup\tall created objects absent; "
                  "baseline inventory restored\n");
  fflush(s->out);
  return 0;
failed:
  s->uncertain = 1;
  hd_first_record(s, "cleanup-incomplete-no-retry");
  fprintf(s->out, "first-sync-recovery\tkeep manifest; no more PTP\n");
  return -1;
}
static int hd_first_retain(HdFirstSync *s) {
  if (!s->retain_requested || s->finished || s->phase != 6 ||
      !hd_first_allowed(s, 0)) {
    fprintf(s->out,
            "first-sync-retain-check\tentry denied phase=%d finished=%d "
            "uncertain=%d stop=%d now=%lld deadline=%lld\n",
            s->phase, s->finished, s->uncertain, s->stop ? *s->stop : 0,
            (long long)(s->now_fn ? s->now_fn() : -1), (long long)s->deadline);
    return -1;
  }
  for (unsigned i = 0; i < 3; ++i)
    if (hd_first_verify(s, &s->roots[i], 0)) {
      fprintf(s->out, "first-sync-retain-check\troot %u identity failed\n", i);
      return -1;
    }
  for (unsigned i = 0; i < 5; ++i)
    if (!s->objects[i].handle || !s->objects[i].proven ||
        s->objects[i].deleted || hd_first_verify(s, &s->objects[i], 0))
      return -1;
  if (!s->refs_set || hd_first_refs_check(s, 1, 0)) {
    fprintf(s->out, "first-sync-retain-check\talbum reference proof failed\n");
    return -1;
  }
  uint32_t *ids = NULL;
  size_t count = 0;
  if (hd_first_handles(s, 0, &ids, &count, 0))
    return -1;
  int good = count == s->baseline_count + 5 + (s->music_owned ? 1 : 0);
  for (size_t i = 0; i < s->baseline_count; ++i) {
    int found = 0;
    for (size_t j = 0; j < count; ++j)
      if (ids[j] == s->baseline[i])
        found = 1;
    if (!found)
      good = 0;
  }
  for (size_t i = 0; i < count; ++i) {
    int known = s->music_owned && ids[i] == s->roots[0].handle;
    for (size_t j = 0; j < s->baseline_count; ++j)
      if (ids[i] == s->baseline[j])
        known = 1;
    for (unsigned j = 0; j < 5; ++j)
      if (ids[i] == s->objects[j].handle)
        known = 1;
    if (!known)
      good = 0;
  }
  free(ids);
  if (!good) {
    fprintf(
        s->out,
        "first-sync-retain-check\tinventory expected=%zu actual=%zu mismatch\n",
        s->baseline_count + 5 + (s->music_owned ? 1 : 0), count);
    return -1;
  }
  uint8_t *data = NULL;
  uint32_t len = 0, id = s->objects[4].handle;
  int rc = hd_first_tx(s, PTP_OC_GetObject, &id, 1, PTP_DP_GETDATA, NULL, 0,
                       &data, &len, NULL, 0, 0, 0);
  good = !rc && data && len == s->mp3_size && !memcmp(data, s->mp3, len);
  free(data);
  if (!good || !hd_first_allowed(s, 0) ||
      hd_first_record(s, "retained-for-visual-test")) {
    fprintf(s->out,
            "first-sync-retain-check\tpayload/final guard/journal failed "
            "exact-payload=%d bytes=%u now=%lld deadline=%lld\n",
            good, len, (long long)(s->now_fn ? s->now_fn() : -1),
            (long long)s->deadline);
    return -1;
  }
  s->retained = 1;
  s->finished = 1;
  fprintf(s->out,
          "first-sync-retained\t%s; graph and payload verified; background "
          "acknowledged; preserve manifest for later cleanup\n",
          s->name);
  return 0;
}
static int hd_first_retained_handoff(HdFirstSync *s) {
  if (!s->retained)
    return -1;
  if (!hd_first_allowed(s, 0)) {
    hd_first_record(s, "retained-handoff-skipped");
    fprintf(
        s->out,
        "first-sync-handoff\tretained; reindex request skipped, no cleanup\n");
    return -1;
  }
  if (hd_first_record(s, "intent-retained-handoff"))
    return -1;
  int rc = hd_first_tx(s, 0x9108, NULL, 0, PTP_DP_NODATA, NULL, 0, NULL, NULL,
                       NULL, 0, 1, 0);
  if (rc) {
    hd_first_record(s, s->uncertain ? "retained-handoff-uncertain"
                                    : "retained-handoff-refused");
    fprintf(s->out, "first-sync-handoff\tretained; CleanDataStore not "
                    "confirmed; no retry or cleanup\n");
    return -1;
  }
  if (hd_first_record(s, "retained-handoff-finalized"))
    return -1;
  fprintf(s->out, "first-sync-handoff\tretained; CleanDataStore "
                  "response=0x2001; safe to unplug for visual test\n");
  return 0;
}
static int hd_first_complete(HdFirstSync *s, int clean, int uncertain,
                             unsigned background, int network_success) {
  if (!s)
    return 0;
  if (uncertain)
    s->uncertain = 1;
  if (s->retain_requested && clean && !uncertain && background &&
      network_success && s->phase == 6) {
    if (!hd_first_retain(s))
      return hd_first_retained_handoff(s);
    fprintf(s->out, "first-sync-retain-refused\tfinal proof failed; retain was "
                    "not confirmed\n");
  }
  int result = hd_first_finish(s, clean, uncertain);
  return s->retain_requested ? -1 : result;
}
static int hd_first_probe_name(const char *name) {
  if (!name || strncmp(name, "HDArtworkProbe-", 15))
    return 0;
  const char *p = name + 15;
  size_t n = strlen(p), hex = 24;
  const char *dash = strchr(p, '-');
  if (dash) {
    if (!dash || dash == p || dash - p > 20 || strlen(dash + 1) != 12)
      return 0;
    for (const char *c = p; c < dash; ++c)
      if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z')))
        return 0;
    p = dash + 1;
    hex = 12;
  } else if (n != 24)
    return 0;
  for (size_t i = 0; i < hex; ++i)
    if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f')))
      return 0;
  return 1;
}
static int hd_first_prepare(HdFirstSync *s, const char *path) {
  if (!hd_first_allowed(s, 0))
    return -1;
  hd_first_configure(s);
  if (hd_first_handles(s, 0, &s->baseline, &s->baseline_count, 0) ||
      hd_first_roots(s))
    return -1;
  for (unsigned i = 0; i < 5; ++i)
    if (hd_first_absent_names(s, &s->objects[i]))
      return -1;
  s->fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (s->fd < 0)
    return -1;
  if (hd_first_record(s, "baseline-no-matching-objects") ||
      hd_first_write_all(s->fd, &s->baseline_count,
                         sizeof(s->baseline_count)) ||
      hd_first_write_all(s->fd, s->baseline,
                         s->baseline_count * sizeof(*s->baseline)) ||
      fsync(s->fd))
    return -1;
  char *dir = strdup(path);
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
  int fd = open(dir, O_RDONLY | O_DIRECTORY);
  free(dir);
  if (fd < 0)
    return -1;
  int error = fsync(fd);
  if (close(fd))
    error = -1;
  if (error)
    return -1;
  s->prepared = 1;
  fprintf(s->out,
          "first-sync-ready\t%s; manifest durable; all roots verified and test "
          "identities absent\n",
          s->name);
  return fflush(s->out) || ferror(s->out) ? -1 : 0;
}
static int hd_first_local(HdFirstSync *s, const char *mp3_path) {
  memset(s, 0, sizeof(*s));
  s->fd = -1;
  uint8_t token[12];
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
    return -1;
  ssize_t n = read(fd, token, sizeof(token));
  close(fd);
  if (n != sizeof(token))
    return -1;
  memcpy(s->name, "HDArtworkProbe-", 15);
  for (unsigned i = 0; i < sizeof(token); ++i)
    snprintf(s->name + 15 + 2 * i, 3, "%02x", token[i]);
  FILE *f = fopen(mp3_path, "rb");
  if (!f)
    return -1;
  long size;
  if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 4 ||
      size > HD_FIRST_MAX_BYTES || fseek(f, 0, SEEK_SET)) {
    fclose(f);
    return -1;
  }
  uint8_t *data = malloc((size_t)size);
  if (!data) {
    fclose(f);
    return -1;
  }
  int ok = fread(data, 1, (size_t)size, f) == (size_t)size && fgetc(f) == EOF &&
           !ferror(f) &&
           ((!memcmp(data, "ID3", 3)) ||
            (data[0] == 255 && (data[1] & 0xe0) == 0xe0));
  fclose(f);
  if (!ok) {
    free(data);
    return -1;
  }
  s->mp3 = data;
  s->mp3_size = (size_t)size;
  return 0;
}
static void hd_first_release(HdFirstSync *s) {
  if (s->fd >= 0)
    close(s->fd);
  s->fd = -1;
  free(s->baseline);
  s->baseline = NULL;
  free((void *)s->mp3);
  s->mp3 = NULL;
}
#endif
