#ifndef ZUNETOOL_HD_ART_GRAPH_RECOVERY_H
#define ZUNETOOL_HD_ART_GRAPH_RECOVERY_H
#include "hd_art_first_recovery.h"
/* Only reads manifest data here. Mutation recovery performs separate proof. */
static int hd_first_graph_load(HdFirstSync *s, const char *path) {
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  struct stat a, b;
  HdFirstRecord first, r;
  uint32_t *ids = NULL;
  size_t count = 0;
  int rc = -1;
  if (fd < 0)
    return -1;
  if (fstat(fd, &a) || !S_ISREG(a.st_mode) || a.st_uid != geteuid() ||
      (a.st_mode & 0777) != 0600 || a.st_nlink != 1 || a.st_size < 0 ||
      a.st_size > 1024 * 1024 || hd_first_read_all(fd, &first, sizeof(first)) ||
      memcmp(first.magic, "ZUHDF1\0\0", 8) ||
      memcmp(first.stage, "baseline-no-matching-objects", 28) ||
      !memchr(first.name, 0, 64) ||
      strncmp(first.name, "HDArtworkProbe-", 15) ||
      !memchr(first.serial, 0, 256) || !first.serial[0] ||
      memcmp(first.guid, hd_art_test_guid_wire, 16) ||
      hd_first_read_all(fd, &count, sizeof(count)) ||
      count > HD_FIRST_MAX_HANDLES)
    goto done;
  ids = calloc(count ? count : 1, 4);
  if (!ids || hd_first_read_all(fd, ids, 4 * count))
    goto done;
  for (size_t i = 0; i < count; ++i) {
    if (!ids[i] || ids[i] == UINT32_MAX)
      goto done;
    for (size_t j = 0; j < i; ++j)
      if (ids[i] == ids[j])
        goto done;
  }
  r = first;
  unsigned records = 0;
  while (lseek(fd, 0, SEEK_CUR) < a.st_size) {
    char magic[8];
    if (++records > 128 || hd_first_read_all(fd, magic, 8) ||
        lseek(fd, -8, SEEK_CUR) < 0)
      goto done;
    if (!memcmp(magic, "ZUHDT1\0\0", 8)) {
      HdFirstTransactionRecord t;
      if (hd_first_read_all(fd, &t, sizeof(t)) ||
          t.opcode != MTP_OC_SendObjectPropList || t.response != PTP_RC_OK ||
          t.result || t.count < 3 || t.count > 5)
        goto done;
    } else {
      if (hd_first_read_all(fd, &r, sizeof(r)) ||
          memcmp(r.magic, first.magic, 8) || memcmp(r.name, first.name, 64) ||
          memcmp(r.serial, first.serial, 256) || r.storage != first.storage ||
          r.bytes != first.bytes || memcmp(r.guid, first.guid, 16) ||
          !memchr(r.stage, 0, 48))
        goto done;
      for (unsigned i = 0; i < 5; ++i)
        if (!memchr(r.objects[i].name, 0, 64) ||
            !memchr(r.objects[i].filename, 0, 160))
          goto done;
      for (unsigned i = 0; i < 3; ++i)
        if (!memchr(r.roots[i].name, 0, 64) ||
            !memchr(r.roots[i].filename, 0, 160))
          goto done;
    }
  }
  if (fstat(fd, &b) || a.st_size != b.st_size ||
      a.st_mtim.tv_sec != b.st_mtim.tv_sec ||
      a.st_mtim.tv_nsec != b.st_mtim.tv_nsec ||
      a.st_ctim.tv_sec != b.st_ctim.tv_sec ||
      a.st_ctim.tv_nsec != b.st_ctim.tv_nsec)
    goto done;
  memset(s, 0, sizeof(*s));
  s->fd = -1;
  s->storage = r.storage;
  s->mp3_size = r.bytes;
  memcpy(s->name, r.name, 64);
  memcpy(s->serial, r.serial, 256);
  memcpy(s->objects, r.objects, sizeof(s->objects));
  memcpy(s->roots, r.roots, sizeof(s->roots));
  s->refs_set = r.refs_set;
  s->recovery_retained_graph = !strcmp(r.stage, "retained-for-visual-test") ||
                               !strcmp(r.stage, "intent-retained-handoff") ||
                               !strcmp(r.stage, "retained-handoff-finalized") ||
                               !strcmp(r.stage, "retained-handoff-refused") ||
                               !strcmp(r.stage, "retained-handoff-uncertain") ||
                               !strcmp(r.stage, "retained-handoff-skipped");
  s->create_music = r.create_music;
  s->music_owned = r.music_owned;
  s->baseline = ids;
  s->baseline_count = count;
  ids = NULL;
  rc = 0;
done:
  free(ids);
  close(fd);
  return rc;
}
static void hd_first_print_text(FILE *out, const uint8_t *data, size_t n) {
  size_t used = 0;
  if (!data || hd_art_string(data, n, &used) || used != n) {
    fprintf(out, "<malformed>");
    return;
  }
  for (size_t i = 1; i + 2 < n; i += 2) {
    uint16_t c = hd_art_u16(data + i);
    fputc(c >= 32 && c < 127 ? (int)c : '?', out);
  }
}
static int hd_first_graph_inventory(HdFirstSync *s) {
  uint32_t *ids = NULL;
  size_t count = 0;
  if (hd_first_handles(s, 0, &ids, &count, 0))
    return -1;
  fprintf(s->out, "first-sync-inventory\tbaseline=%zu current=%zu\n",
          s->baseline_count, count);
  for (size_t i = 0; i < s->baseline_count; ++i) {
    int found = 0;
    for (size_t j = 0; j < count; ++j)
      if (ids[j] == s->baseline[i])
        found = 1;
    if (!found)
      fprintf(s->out, "first-sync-baseline-missing\thandle=%u\n",
              s->baseline[i]);
  }
  for (size_t i = 0; i < count; ++i) {
    int found = 0;
    for (size_t j = 0; j < s->baseline_count; ++j)
      if (ids[i] == s->baseline[j])
        found = 1;
    if (found)
      continue;
    fprintf(s->out, "first-sync-inventory-new\thandle=%u\n", ids[i]);
    uint16_t props[] = {MTP_OPC_ObjectFormat, MTP_OPC_ParentObject,
                        MTP_OPC_Name, MTP_OPC_ObjectFileName, 0xdab9};
    uint16_t format = 0;
    for (unsigned k = 0; k < 5; ++k) {
      if (k == 4 && format != MTP_OFC_MP3 &&
          format != MTP_OFC_AbstractAudioAlbum)
        continue;
      uint8_t *data = NULL;
      uint32_t n = 0;
      int rc = hd_first_get(s, ids[i], props[k], &data, &n, 0);
      fprintf(s->out,
              "first-sync-inventory-property\thandle=%u prop=%04x result=%d "
              "bytes=%u value=",
              ids[i], props[k], rc, n);
      if (!rc && data) {
        if (k == 0 && n == 2)
          format = hd_art_u16(data);
        if (k == 2 || k == 3)
          hd_first_print_text(s->out, data, n);
        else if (n <= 16)
          for (uint32_t j = 0; j < n; ++j)
            fprintf(s->out, "%02x", data[j]);
      }
      fputc('\n', s->out);
      free(data);
      if (s->uncertain) {
        free(ids);
        return -1;
      }
    }
    if (format >= 0xba00 && format <= 0xbaff) {
      uint8_t *data = NULL;
      uint32_t n = 0;
      int rc = hd_first_tx(s, MTP_OC_GetObjectReferences, &ids[i], 1,
                           PTP_DP_GETDATA, NULL, 0, &data, &n, NULL, 0, 0, 0);
      fprintf(
          s->out,
          "first-sync-inventory-album-refs\thandle=%u result=%d bytes=%u refs=",
          ids[i], rc, n);
      if (!rc && data && n >= 4 && hd_art_u32(data) <= HD_FIRST_MAX_HANDLES &&
          n == 4 + 4 * hd_art_u32(data))
        for (uint32_t k = 0; k < hd_art_u32(data); ++k)
          fprintf(s->out, "%u,", hd_art_u32(data + 4 + 4 * k));
      fputc('\n', s->out);
      free(data);
      if (s->uncertain) {
        free(ids);
        return -1;
      }
    }
  }
  free(ids);
  return 0;
}
static int hd_first_graph_diagnose(HdFirstSync *s) {
  if (!hd_first_allowed(s, 0) || hd_first_graph_inventory(s))
    return -1;
  static const uint16_t props[] = {MTP_OPC_ObjectFormat,
                                   MTP_OPC_StorageID,
                                   MTP_OPC_ParentObject,
                                   MTP_OPC_ObjectFileName,
                                   MTP_OPC_Name,
                                   0xdc41,
                                   0xda97,
                                   0xdab9,
                                   MTP_OPC_ObjectSize};
  for (unsigned i = 0; i < 6; ++i) {
    HdFirstObject *o = i ? &s->objects[i - 1] : &s->roots[0];
    if (!o->handle)
      continue;
    fprintf(s->out,
            "first-sync-diagnostic\tkind=%d handle=%u expected-format=%04x "
            "expected-parent=%u expected-filename=%s expected-name=%s\n",
            (int)i - 1, o->handle, o->format, o->parent, o->filename, o->name);
    for (unsigned j = 0; j < sizeof(props) / sizeof(props[0]); ++j) {
      uint16_t prop = props[j];
      if (prop == 0xda97 && o->format != MTP_OFC_AbstractArtist)
        continue;
      if (prop == 0xdab9 && o->format != MTP_OFC_MP3 &&
          o->format != MTP_OFC_AbstractAudioAlbum)
        continue;
      uint8_t *data = NULL;
      uint32_t n = 0;
      int result = hd_first_get(s, o->handle, prop, &data, &n, 0);
      fprintf(
          s->out,
          "first-sync-property\thandle=%u prop=%04x bytes=%u result=%d value=",
          o->handle, prop, n, result);
      if (!result && data) {
        if (prop == MTP_OPC_Name || prop == MTP_OPC_ObjectFileName)
          hd_first_print_text(s->out, data, n);
        else if (n <= 16)
          for (uint32_t k = 0; k < n; ++k)
            fprintf(s->out, "%02x", data[k]);
        else
          fprintf(s->out, "<unexpected-size>");
      }
      fputc('\n', s->out);
      free(data);
      if (s->uncertain)
        return -1;
    }
  }
  if (s->objects[4].handle && s->mp3) {
    uint8_t *data = NULL;
    uint32_t n = 0, id = s->objects[4].handle;
    int rc = hd_first_tx(s, PTP_OC_GetObject, &id, 1, PTP_DP_GETDATA, NULL, 0,
                         &data, &n, NULL, 0, 0, 0);
    int equal = !rc && data && n == s->mp3_size && !memcmp(data, s->mp3, n);
    free(data);
    fprintf(s->out,
            "first-sync-payload\thandle=%u expected-bytes=%zu actual-bytes=%u "
            "exact=%d\n",
            id, s->mp3_size, n, equal);
    if (rc || !equal)
      return -1;
  }
  return 0;
}
static int hd_first_graph_record_stage(int fd, const char *stage) {
  HdFirstRecord r;
  return hd_first_read_all(fd, &r, sizeof(r)) ||
                 memcmp(r.magic, "ZUHDF1\0\0", 8) || !memchr(r.stage, 0, 48) ||
                 strcmp(r.stage, stage)
             ? -1
             : 0;
}
static int hd_first_graph_returned(int fd, HdFirstSync *s, HdFirstObject *o) {
  HdFirstTransactionRecord r;
  if (hd_first_read_all(fd, &r, sizeof(r)) ||
      memcmp(r.magic, "ZUHDT1\0\0", 8) ||
      r.opcode != MTP_OC_SendObjectPropList || r.response != PTP_RC_OK ||
      r.result || r.count < 3 || r.count > 5 || r.parameters[0] != s->storage ||
      r.parameters[1] != o->parent || r.parameters[2] != o->handle ||
      !o->handle || o->handle == UINT32_MAX)
    return -1;
  for (size_t i = 0; i < s->baseline_count; ++i)
    if (s->baseline[i] == o->handle)
      return -1;
  return 0;
}
/* Accept only explicit terminal histories: failed track identity verification,
 * fully verified creation/link with unsuccessful empty-reference readback, or
 * the durable retained-for-visual-test terminal record. No previous deletes. */
static int hd_first_graph_recovery_load(HdFirstSync *s, const char *path) {
  struct stat source_before, source_after;
  if (lstat(path, &source_before) || !S_ISREG(source_before.st_mode))
    return -1;
  if (hd_first_graph_load(s, path))
    return -1;
  int fd = open(path, O_RDONLY | O_NOFOLLOW), rc = -1;
  HdFirstRecord first;
  size_t count = 0;
  uint32_t *ids = NULL;
  if (fd < 0)
    goto done;
  if (fstat(fd, &source_after) || source_before.st_dev != source_after.st_dev ||
      source_before.st_ino != source_after.st_ino ||
      source_before.st_size != source_after.st_size ||
      source_before.st_mtim.tv_sec != source_after.st_mtim.tv_sec ||
      source_before.st_mtim.tv_nsec != source_after.st_mtim.tv_nsec ||
      source_before.st_ctim.tv_sec != source_after.st_ctim.tv_sec ||
      source_before.st_ctim.tv_nsec != source_after.st_ctim.tv_nsec)
    goto done;
  if (hd_first_read_all(fd, &first, sizeof(first)) ||
      memcmp(first.name, s->name, 64) || memcmp(first.serial, s->serial, 256) ||
      first.storage != s->storage ||
      memcmp(first.guid, hd_art_test_guid_wire, 16) || first.music_owned ||
      first.refs_set ||
      (s->music_owned && (!first.create_music || first.roots[0].handle)) ||
      hd_first_read_all(fd, &count, sizeof(count)) ||
      count != s->baseline_count)
    goto done;
  ids = calloc(count ? count : 1, 4);
  if (!ids || hd_first_read_all(fd, ids, 4 * count) ||
      memcmp(ids, s->baseline, 4 * count))
    goto done;
  for (unsigned i = 0; i < 5; ++i)
    if (first.objects[i].handle)
      goto done;
  if ((!s->music_owned && !s->recovery_retained_graph) ||
      s->refs_set != (s->recovery_retained_graph ? 1 : 0) || s->roots[0].parent)
    goto done;
  if (s->music_owned &&
      (!s->create_music ||
       hd_first_graph_record_stage(fd, "intent-create-owned-Music-root") ||
       hd_first_graph_returned(fd, s, &s->roots[0]) ||
       hd_first_graph_record_stage(fd, "owned-Music-root-handle-returned") ||
       hd_first_graph_record_stage(fd, "owned-Music-root-identity-verified")))
    goto done;
  HdFirstSync expected = {0};
  expected.storage = s->storage;
  expected.mp3_size = s->mp3_size;
  memcpy(expected.name, s->name, 64);
  hd_first_configure(&expected);
  for (unsigned i = 0; i < 3; ++i) {
    HdFirstObject *o = &s->roots[i];
    if (o->format != MTP_OFC_Association || o->parent ||
        o->storage != s->storage ||
        strcmp(o->filename, expected.roots[i].filename) || !o->proven ||
        o->deleted || !hd_bind_nonzero(o->persistent))
      goto done;
    if ((i || !s->music_owned) && memcmp(o, &first.roots[i], sizeof(*o)))
      goto done;
  }
  s->recovery_complete_graph = s->objects[4].proven == 1;
  if (s->recovery_retained_graph && !s->recovery_complete_graph)
    goto done;
  for (unsigned i = 0; i < 5; ++i) {
    HdFirstObject *o = &s->objects[i], *e = &expected.objects[i];
    char stage[48];
    uint32_t parent = i == 0   ? s->roots[0].handle
                      : i == 1 ? s->objects[0].handle
                      : i == 2 ? s->roots[1].handle
                      : i == 3 ? s->roots[2].handle
                               : s->objects[1].handle;
    char normalized[160];
    snprintf(normalized, sizeof(normalized), "01 %.63s.mp3", e->name);
    int valid_filename = !strcmp(o->filename, e->filename) ||
                         (i == 4 && s->recovery_complete_graph &&
                          !strcmp(o->filename, normalized));
    if (o->format != e->format || o->storage != s->storage ||
        o->parent != parent || strcmp(o->name, e->name) || !valid_filename ||
        o->deleted ||
        ((i < 4 || s->recovery_complete_graph) &&
         (!o->proven || !hd_bind_nonzero(o->persistent))) ||
        (i == 4 && !s->recovery_complete_graph && o->proven))
      goto done;
    for (unsigned j = 0; j < 3; ++j)
      if (o->handle == s->roots[j].handle)
        goto done;
    for (unsigned j = 0; j < i; ++j)
      if (o->handle == s->objects[j].handle)
        goto done;
    snprintf(stage, sizeof(stage), "intent-create-kind-%u", i);
    if (hd_first_graph_record_stage(fd, stage) ||
        hd_first_graph_returned(fd, s, o) ||
        hd_first_graph_record_stage(fd, "created-handle-returned"))
      goto done;
    if (i >= 2 && hd_first_graph_record_stage(fd, "intent-object-commit"))
      goto done;
    if (hd_first_graph_record_stage(fd, i == 4 && !s->recovery_complete_graph
                                            ? "unverified-created-object"
                                            : "created-identity-verified"))
      goto done;
  }
  if (s->recovery_complete_graph) {
    if (hd_first_graph_record_stage(fd, "intent-link-album-track") ||
        hd_first_graph_record_stage(fd, "album-track-link-verified"))
      goto done;
    if (s->recovery_retained_graph) {
      if (hd_first_graph_record_stage(fd, "retained-for-visual-test"))
        goto done;
      if (lseek(fd, 0, SEEK_CUR) < source_before.st_size) {
        HdFirstRecord handoff;
        if (hd_first_read_all(fd, &handoff, sizeof(handoff)) ||
            !memchr(handoff.stage, 0, 48))
          goto done;
        if (!strcmp(handoff.stage, "retained-handoff-skipped")) {
        } else {
          if (strcmp(handoff.stage, "intent-retained-handoff"))
            goto done;
          if (lseek(fd, 0, SEEK_CUR) < source_before.st_size) {
            if (hd_first_read_all(fd, &handoff, sizeof(handoff)) ||
                !memchr(handoff.stage, 0, 48) ||
                (strcmp(handoff.stage, "retained-handoff-finalized") &&
                 strcmp(handoff.stage, "retained-handoff-refused") &&
                 strcmp(handoff.stage, "retained-handoff-uncertain")))
              goto done;
          }
        }
      }
    } else if (hd_first_graph_record_stage(fd, "intent-clear-album-track") ||
               hd_first_graph_record_stage(fd, "cleanup-incomplete-no-retry"))
      goto done;
  }
  char extra;
  if (read(fd, &extra, 1) != 0)
    goto done;
  if (fstat(fd, &source_after) ||
      source_before.st_size != source_after.st_size ||
      source_before.st_mtim.tv_sec != source_after.st_mtim.tv_sec ||
      source_before.st_mtim.tv_nsec != source_after.st_mtim.tv_nsec ||
      source_before.st_ctim.tv_sec != source_after.st_ctim.tv_sec ||
      source_before.st_ctim.tv_nsec != source_after.st_ctim.tv_nsec)
    goto done;
  rc = 0;
done:
  if (fd >= 0)
    close(fd);
  free(ids);
  if (rc)
    hd_first_release(s);
  return rc;
}
static int hd_first_graph_extra_verify(HdFirstSync *s, HdFirstObject *o,
                                       unsigned kind) {
  uint8_t data[16];
  uint16_t format = kind ? MTP_OFC_AbstractArtist : MTP_OFC_AbstractAudioAlbum;
  if (hd_first_value(s, o->handle, MTP_OPC_ObjectFormat, data, 2, 0) ||
      hd_art_u16(data) != format ||
      hd_first_value(s, o->handle, MTP_OPC_StorageID, data, 4, 0) ||
      hd_art_u32(data) != s->storage ||
      hd_first_value(s, o->handle, MTP_OPC_ParentObject, data, 4, 0) ||
      hd_art_u32(data))
    return -1;
  for (unsigned i = 0; i < 2; ++i) {
    uint8_t *text = NULL;
    uint32_t n = 0;
    if (hd_first_get(s, o->handle, i ? MTP_OPC_Name : MTP_OPC_ObjectFileName,
                     &text, &n, 0)) {
      free(text);
      return -1;
    }
    int good =
        hd_first_name_match(text, n,
                            i ? (kind ? "Unknown Artist" : "Unknown Album")
                              : (kind ? "Unknown Artist.art" : ""));
    if (!kind && !i)
      good = text && ((n == 1 && text[0] == 0) ||
                      (n == 3 && text[0] == 1 && text[1] == 0 && text[2] == 0));
    free(text);
    if (!good)
      return -1;
  }
  if (hd_first_value(s, o->handle, 0xdc41, data, 16, 0) ||
      !hd_bind_nonzero(data) || (o->proven && memcmp(o->persistent, data, 16)))
    return -1;
  memcpy(o->persistent, data, 16);
  o->format = format;
  o->storage = s->storage;
  o->proven = 1;
  snprintf(o->name, 64, "%s", kind ? "Unknown Artist" : "Unknown Album");
  snprintf(o->filename, 160, "%s", kind ? "Unknown Artist.art" : "");
  if (!kind && (hd_first_value(s, o->handle, 0xdab9, data, 4, 0) ||
                hd_art_u32(data) != s->recovery_extras[1].handle))
    return -1;
  return 0;
}
static int hd_first_graph_inbound(HdFirstSync *s, uint32_t *ids, size_t count) {
  unsigned own_album_refs = 0;
  for (size_t i = 0; i < count; ++i) {
    uint8_t scalar[4];
    if (hd_first_value(s, ids[i], MTP_OPC_ObjectFormat, scalar, 2, 0))
      return -1;
    uint16_t format = hd_art_u16(scalar);
    if (format == MTP_OFC_AbstractAudioAlbum ||
        (format == 0x3008 || format == MTP_OFC_MP3) ||
        (format >= 0xb900 && format <= 0xb904)) {
      if (hd_first_value(s, ids[i], 0xdab9, scalar, 4, 0))
        return -1;
      uint32_t artist = hd_art_u32(scalar);
      if (s->recovery_extras[1].handle &&
          artist == s->recovery_extras[1].handle &&
          ids[i] != s->recovery_extras[0].handle)
        return -1;
      if (artist == s->objects[2].handle && ids[i] != s->objects[3].handle &&
          ids[i] != s->objects[4].handle)
        return -1;
    }
    if (format >= 0xba00 && format <= 0xbaff) {
      uint8_t *data = NULL;
      uint32_t n = 0;
      if (hd_first_tx(s, MTP_OC_GetObjectReferences, &ids[i], 1, PTP_DP_GETDATA,
                      NULL, 0, &data, &n, NULL, 0, 0, 0)) {
        free(data);
        return -1;
      }
      if (!data || n < 4 || hd_art_u32(data) > HD_FIRST_MAX_HANDLES ||
          n != 4 + 4 * hd_art_u32(data)) {
        free(data);
        return -1;
      }
      unsigned refs = hd_art_u32(data);
      if (ids[i] == s->recovery_extras[0].handle) {
        if (refs != 1 || hd_art_u32(data + 4) != s->objects[4].handle) {
          free(data);
          return -1;
        }
        ++own_album_refs;
      } else if (ids[i] == s->objects[3].handle) {
        if (refs > 1 ||
            (refs == 1 && hd_art_u32(data + 4) != s->objects[4].handle)) {
          free(data);
          return -1;
        }
      } else
        for (unsigned j = 0; j < refs; ++j) {
          uint32_t ref = hd_art_u32(data + 4 + 4 * j);
          for (unsigned k = 0; k < 5; ++k)
            if (ref == s->objects[k].handle) {
              free(data);
              return -1;
            }
          for (unsigned k = 0; k < 2; ++k)
            if (ref == s->recovery_extras[k].handle) {
              free(data);
              return -1;
            }
        }
      free(data);
    }
  }
  return own_album_refs == (s->recovery_extras[0].handle ? 1u : 0u) ? 0 : -1;
}
static int hd_first_graph_extra_record(HdFirstSync *s) {
  struct {
    char magic[8];
    HdFirstObject objects[2];
  } r = {0};
  memcpy(r.magic, "ZUHDX1", 6);
  memcpy(r.objects, s->recovery_extras, sizeof(r.objects));
  return s->fd < 0 || hd_first_write_all(s->fd, &r, sizeof(r)) || fsync(s->fd)
             ? -1
             : 0;
}
static int hd_first_graph_recovery_inspect(HdFirstSync *s) {
  if (!hd_first_allowed(s, 0))
    return -1;
  uint32_t *ids = NULL;
  size_t count = 0;
  if (hd_first_handles(s, 0, &ids, &count, 0))
    return -1;
  int album_present = 0, good = 1;
  unsigned added = 0, extras = 0;
  uint32_t extra_ids[2] = {0};
  for (size_t i = 0; i < s->baseline_count; ++i) {
    int found = 0;
    for (size_t j = 0; j < count; ++j)
      if (ids[j] == s->baseline[i])
        found = 1;
    if (!found)
      good = 0;
  }
  for (size_t i = 0; i < count; ++i) {
    int old = 0, own = ids[i] == s->roots[0].handle;
    for (size_t j = 0; j < s->baseline_count; ++j)
      if (ids[i] == s->baseline[j])
        old = 1;
    if (old)
      continue;
    for (unsigned j = 0; j < 5; ++j)
      if (ids[i] == s->objects[j].handle)
        own = 1;
    if (ids[i] == s->objects[3].handle)
      album_present = 1;
    if (!own) {
      if (extras < 2)
        extra_ids[extras] = ids[i];
      ++extras;
    }
    ++added;
  }
  if (!good || extras > 2 || extras == 1 ||
      (s->recovery_complete_graph &&
       (!album_present || extras || added != (unsigned)(5 + s->music_owned))) ||
      added != (unsigned)((album_present ? 5 : 4) + s->music_owned) + extras) {
    free(ids);
    return -1;
  }
  if (extras) {
    for (unsigned i = 0; i < 2; ++i) {
      uint8_t f[2];
      if (hd_first_value(s, extra_ids[i], MTP_OPC_ObjectFormat, f, 2, 0)) {
        free(ids);
        return -1;
      }
      unsigned kind = hd_art_u16(f) == MTP_OFC_AbstractAudioAlbum ? 0
                      : hd_art_u16(f) == MTP_OFC_AbstractArtist   ? 1
                                                                  : 2;
      if (kind > 1 || (s->recovery_extras[kind].handle &&
                       s->recovery_extras[kind].handle != extra_ids[i])) {
        free(ids);
        return -1;
      }
      s->recovery_extras[kind].handle = extra_ids[i];
    }
    if (!s->recovery_extras[0].handle || !s->recovery_extras[1].handle ||
        hd_first_graph_extra_verify(s, &s->recovery_extras[0], 0) ||
        hd_first_graph_extra_verify(s, &s->recovery_extras[1], 1)) {
      free(ids);
      return -1;
    }
  }
  if (hd_first_graph_inbound(s, ids, count)) {
    free(ids);
    return -1;
  }
  free(ids);
  if (s->objects[3].deleted && album_present)
    return -1;
  for (unsigned i = 0; i < 3; ++i)
    if (hd_first_verify(s, &s->roots[i], 0))
      return -1;
  for (unsigned i = 0; i < 5; ++i) {
    if (i == 3 && !album_present)
      continue;
    if (hd_first_verify(s, &s->objects[i], 0))
      return -1;
  }
  /* Every folder contains exactly our next child, never foreign content. */
  for (unsigned i = 0; i < 3; ++i) {
    if (!i && !s->music_owned)
      continue;
    uint32_t parent = i ? s->objects[i - 1].handle : s->roots[0].handle;
    uint32_t child = i == 0   ? s->objects[0].handle
                     : i == 1 ? s->objects[1].handle
                              : s->objects[4].handle;
    if (hd_first_children(s, 0, parent, &ids, &count, 0))
      return -1;
    good = count == 1 && ids[0] == child;
    free(ids);
    if (!good)
      return -1;
  }
  if (album_present) {
    uint8_t *refs = NULL;
    uint32_t n = 0, id = s->objects[3].handle;
    if (hd_first_tx(s, MTP_OC_GetObjectReferences, &id, 1, PTP_DP_GETDATA, NULL,
                    0, &refs, &n, NULL, 0, 0, 0)) {
      free(refs);
      return -1;
    }
    int valid = refs && ((n == 4 && !hd_art_u32(refs)) ||
                         (s->recovery_complete_graph && n == 8 &&
                          hd_art_u32(refs) == 1 &&
                          hd_art_u32(refs + 4) == s->objects[4].handle));
    if (valid)
      s->refs_set = (int)hd_art_u32(refs);
    free(refs);
    if (!valid)
      return -1;
  }
  uint32_t id = s->objects[4].handle, len = 0;
  uint8_t *data = NULL;
  int result = hd_first_tx(s, PTP_OC_GetObject, &id, 1, PTP_DP_GETDATA, NULL, 0,
                           &data, &len, NULL, 0, 0, 0);
  good = !result && data && s->mp3 && len == s->mp3_size &&
         !memcmp(data, s->mp3, len);
  free(data);
  if (!good)
    return -1;
  s->objects[3].deleted = !album_present;
  fprintf(s->out,
          "first-sync-graph-recovery-proof\tbaseline unchanged; %u exact "
          "created IDs; album=%s; payload exact\n",
          added,
          album_present ? "present" : "already absent in complete inventory");
  return 0;
}
/* After deleting our track, firmware may prune its empty album/artist.
 * Only a complete exact inventory can establish this, never a failed getter. */
static int hd_first_graph_already_absent(HdFirstSync *s, HdFirstObject *o) {
  uint32_t *ids = NULL;
  size_t count = 0;
  if (hd_first_handles(s, 0, &ids, &count, 0))
    return -1;
  int good = 1, present = 0;
  for (size_t i = 0; i < s->baseline_count; ++i) {
    int found = 0;
    for (size_t j = 0; j < count; ++j)
      if (ids[j] == s->baseline[i])
        found = 1;
    if (!found)
      good = 0;
  }
  for (size_t i = 0; i < count; ++i) {
    int known = 0;
    if (ids[i] == o->handle)
      present = 1;
    for (size_t j = 0; j < s->baseline_count; ++j)
      if (ids[i] == s->baseline[j])
        known = 1;
    for (unsigned j = 0; j < 5; ++j)
      if (ids[i] == s->objects[j].handle && !s->objects[j].deleted)
        known = 1;
    if (ids[i] == s->roots[0].handle && !s->roots[0].deleted)
      known = 1;
    for (unsigned j = 0; j < 2; ++j)
      if (ids[i] == s->recovery_extras[j].handle)
        known = 1;
    if (!known)
      good = 0;
  }
  free(ids);
  if (!good)
    return -1;
  if (present)
    return 0;
  o->deleted = 1;
  if (hd_first_record(s, "graph-proven-object-already-absent"))
    return -1;
  return 1;
}
/* Explicit operator-authorized continuation of a verified completed graph.
 * The original manifest remains cleanup authority; this does not claim another
 * image delivery or attempt to recreate/resync anything. */
static int hd_first_graph_recovery_retain(HdFirstSync *s) {
  if (!s->recovery_complete_graph || hd_first_graph_recovery_inspect(s) ||
      s->refs_set != 1 || !hd_first_allowed(s, 0) ||
      hd_first_record(s, "recovery-retained-for-visual-test"))
    return -1;
  s->retained = 1;
  s->finished = 1;
  s->started = 1;
  fprintf(s->out, "first-sync-recovery-retained\texisting graph and payload "
                  "verified; no new image delivery claimed; preserve original "
                  "manifest for later cleanup\n");
  return hd_first_retained_handoff(s);
}
static int hd_first_graph_recovery_delete(HdFirstSync *s) {
  if (hd_first_graph_recovery_inspect(s) ||
      hd_first_record(s, "graph-recovery-revalidated") ||
      hd_first_graph_extra_record(s) || !hd_first_allowed(s, 0))
    return -1;
  s->recovery_delete_guard = 1;
  s->started = 1;
  for (int i = 4; i >= 0; --i) {
    if (s->objects[i].deleted)
      continue;
    if (i == 3 || i == 2) {
      int absent = hd_first_graph_already_absent(s, &s->objects[i]);
      if (absent < 0)
        return -1;
      if (absent)
        continue;
    }
    if (hd_first_delete(s, &s->objects[i]))
      return -1;
  }
  if (s->music_owned && hd_first_delete(s, &s->roots[0]))
    return -1;
  uint32_t *ids = NULL;
  size_t count = 0;
  if (hd_first_handles(s, 0, &ids, &count, 0))
    return -1;
  int good = 1;
  unsigned remaining_extras = 0;
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
    if (!found) {
      if (ids[i] == s->recovery_extras[0].handle ||
          ids[i] == s->recovery_extras[1].handle)
        ++remaining_extras;
      else
        good = 0;
    }
  }
  free(ids);
  if (!good)
    return -1;
  if (remaining_extras) {
    hd_first_record(s, "known-test-objects-removed-extras-remain");
    fprintf(s->out,
            "first-sync-graph-recovery\tall explicit test objects absent; %u "
            "proven firmware placeholders retained pending reindex\n",
            remaining_extras);
    return 1;
  }
  if (hd_first_record(s, "graph-recovery-baseline-restored"))
    return -1;
  fprintf(s->out, "first-sync-graph-recovery\tbaseline inventory restored\n");
  return 0;
}

#endif
