/* Strict read-only reconstruction of an interrupted full-album creation prefix.
 * This does not authorize deletion or guess at a lost transaction response. */
#ifndef ZUNETOOL_HD_ART_ALBUM_RECOVERY_H
#define ZUNETOOL_HD_ART_ALBUM_RECOVERY_H
#include "hd_art_album_sync.h"
#include <gcrypt.h>
static int hd_album_recovery_take(const uint8_t **p, size_t *left, void *out,
                                  size_t n) {
  if (n > *left)
    return -1;
  memcpy(out, *p, n);
  *p += n;
  *left -= n;
  return 0;
}
static int hd_album_recovery_record(const HdAlbumRecord *r,
                                    const HdAlbumSync *s) {
  return memcmp(r->magic, "ZUALBM1\0", 8) || r->version != 1 ||
         r->record_bytes != sizeof(*r) || r->tracks != s->track_count ||
         r->baseline_count != s->baseline_count ||
         memcmp(r->guid, s->guid_wire, 16) ||
         !memchr(r->stage, 0, sizeof(r->stage)) || r->parameter_count < 0 ||
         r->parameter_count > 5;
}
static int hd_album_recovery_object(HdAlbumSync *s, unsigned index,
                                    const HdAlbumObject *o) {
  const HdAlbumObject *expected = &s->objects[index];
  if (!memchr(o->name, 0, sizeof(o->name)) ||
      !memchr(o->filename, 0, sizeof(o->filename)) ||
      strcmp(o->name, expected->name) || o->format != expected->format ||
      o->storage != s->wire.storage || o->size != expected->size ||
      o->duration != expected->duration || o->disc != expected->disc ||
      o->track != expected->track || (o->proven != 0 && o->proven != 1))
    return -1;
  if (strcmp(o->filename, expected->filename)) {
    char normalized[512];
    int n = snprintf(normalized, sizeof(normalized), "%02u %s.mp3", o->track,
                     o->name);
    if (index < 5 || n < 0 || (size_t)n >= sizeof(normalized) ||
        strcmp(o->filename, normalized))
      return -1;
  }
  uint32_t parent = index == 0   ? 0
                    : index == 1 ? s->objects[0].handle
                    : index == 2 ? s->objects[1].handle
                    : index == 3 ? s->wire.roots[1].handle
                    : index == 4 ? s->wire.roots[2].handle
                                 : s->objects[2].handle;
  return o->parent != parent ? -1 : 0;
}
static int hd_album_recovery_load(HdAlbumSync *s, const char *path) {
  if (strlen(path) >= sizeof(s->recovery_source))
    return -1;
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if (fd < 0)
    return -1;
  struct stat before, after;
  int good = !fstat(fd, &before) && S_ISREG(before.st_mode) &&
             before.st_uid == geteuid() && !(before.st_mode & 077) &&
             before.st_size > 0 && before.st_size <= 4 * 1024 * 1024;
  uint8_t *data = good ? malloc((size_t)before.st_size) : NULL;
  if (!data) {
    close(fd);
    return -1;
  }
  size_t got = 0;
  while (got < (size_t)before.st_size) {
    ssize_t n = read(fd, data + got, (size_t)before.st_size - got);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      good = 0;
      break;
    }
    got += (size_t)n;
  }
  good = good && !fstat(fd, &after) && before.st_size == after.st_size &&
         before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
         before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
         before.st_ctim.tv_sec == after.st_ctim.tv_sec &&
         before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
  close(fd);
  int result = -1;
  const uint8_t *p = data;
  size_t left = got;
  HdAlbumRecord r;
  if (!good || hd_album_recovery_take(&p, &left, &r, sizeof(r)) ||
      memcmp(r.magic, "ZUALBM1\0", 8) || r.version != 1 ||
      r.record_bytes != sizeof(r) || r.tracks != s->track_count ||
      !r.baseline_count || r.baseline_count > HD_FIRST_MAX_HANDLES ||
      memcmp(r.stage, "baseline-plan\0", 14) || r.index != UINT32_MAX ||
      r.opcode || r.response || r.result || r.parameter_count ||
      !hd_bind_nonzero(r.guid))
    goto done;
  s->baseline_count = r.baseline_count;
  memcpy(s->guid_wire, r.guid, 16);
  static const unsigned order[16] = {3, 2, 1,  0,  5,  4,  7,  6,
                                     8, 9, 10, 11, 12, 13, 14, 15};
  size_t at = 0;
  for (unsigned i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      s->guid[at++] = '-';
    snprintf(s->guid + at, 3, "%02x", r.guid[order[i]]);
    at += 2;
  }
  char artist[512], album[512], year[5];
  if (hd_album_recovery_take(&p, &left, s->wire.serial,
                             sizeof(s->wire.serial)) ||
      !memchr(s->wire.serial, 0, sizeof(s->wire.serial)) ||
      !s->wire.serial[0] ||
      hd_album_recovery_take(&p, &left, artist, sizeof(artist)) ||
      memcmp(artist, s->artist, sizeof(artist)) ||
      hd_album_recovery_take(&p, &left, album, sizeof(album)) ||
      memcmp(album, s->album, sizeof(album)) ||
      hd_album_recovery_take(&p, &left, year, sizeof(year)) ||
      memcmp(year, s->year, sizeof(year)) ||
      hd_album_recovery_take(&p, &left, s->wire.roots, sizeof(s->wire.roots)) ||
      hd_album_recovery_take(&p, &left, &s->music_owned,
                             sizeof(s->music_owned)) ||
      (s->music_owned != 0 && s->music_owned != 1))
    goto done;
  s->wire.storage = s->wire.roots[1].storage;
  if (!s->wire.storage || s->wire.storage == UINT32_MAX)
    goto done;
  const char *names[] = {"Music", "Artists", "Albums"};
  for (unsigned i = 0; i < 3; ++i) {
    HdFirstObject *root = &s->wire.roots[i];
    if (i == 0 && s->music_owned) {
      if (root->handle)
        goto done;
      continue;
    }
    if (!root->handle || root->handle == UINT32_MAX || root->parent ||
        root->storage != s->wire.storage ||
        root->format != MTP_OFC_Association || root->proven != 1 ||
        !hd_bind_nonzero(root->persistent) ||
        !memchr(root->filename, 0, sizeof(root->filename)) ||
        strcmp(root->filename, names[i]))
      goto done;
  }
  s->baseline = calloc(s->baseline_count, 4);
  if (!s->baseline)
    goto done;
  if (hd_album_recovery_take(&p, &left, s->baseline, s->baseline_count * 4))
    goto done;
  for (size_t i = 0; i < s->baseline_count; ++i) {
    if (!s->baseline[i] || s->baseline[i] == UINT32_MAX)
      goto done;
    for (size_t j = 0; j < i; ++j)
      if (s->baseline[i] == s->baseline[j])
        goto done;
  }
  for (unsigned i = 0; i < 3; ++i)
    if (s->wire.roots[i].handle) {
      int found = 0;
      for (size_t j = 0; j < s->baseline_count; ++j)
        found |= s->baseline[j] == s->wire.roots[i].handle;
      if (!found)
        goto done;
    }
  for (size_t i = 0; i < s->track_count; ++i) {
    HdAlbumObject o;
    char source[4096];
    if (hd_album_recovery_take(&p, &left, &o, sizeof(o)) ||
        memcmp(&o, &s->tracks[i].object, sizeof(o)) ||
        hd_album_recovery_take(&p, &left, source, sizeof(source)) ||
        memcmp(source, s->tracks[i].path, sizeof(source)))
      goto done;
  }
  if (hd_album_configure(s))
    goto done;
  if (!s->music_owned) {
    s->objects[0].handle = s->wire.roots[0].handle;
    s->objects[0].proven = 1;
    memcpy(s->objects[0].persistent, s->wire.roots[0].persistent, 16);
  }
  unsigned index = s->music_owned ? 0 : 1, step = 0;
  uint32_t returned = 0;
  int stopped = 0;
  while (left) {
    if (hd_album_recovery_take(&p, &left, &r, sizeof(r)) ||
        hd_album_recovery_record(&r, s) || index >= 5 + s->track_count ||
        r.index != index || hd_album_recovery_object(s, index, &r.object))
      goto done;
    if (!strcmp(r.stage, "stopped-preserve-journal")) {
      if (step != 5 || index < 5 || left || r.opcode || r.result != -1 ||
          r.response || r.parameter_count || r.object.handle != returned)
        goto done;
      s->objects[index] = r.object;
      stopped = 1;
      break;
    }
    if (step == 0) {
      if (strcmp(r.stage, "intent") || r.opcode != MTP_OC_SendObjectPropList ||
          r.result || r.response || r.parameter_count != 5 || r.object.handle ||
          r.parameters[0] != s->wire.storage ||
          r.parameters[1] != r.object.parent ||
          r.parameters[2] != r.object.format || r.parameters[3] ||
          r.parameters[4] != r.object.size)
        goto done;
      s->objects[index] = r.object;
      step = 1;
    } else if (step == 1) {
      if (strcmp(r.stage, "response") ||
          r.opcode != MTP_OC_SendObjectPropList || r.result ||
          r.response != PTP_RC_OK || r.parameter_count < 3 ||
          r.parameters[0] != s->wire.storage ||
          r.parameters[1] != r.object.parent ||
          !hd_album_new_id(s, r.parameters[2]) || r.object.handle)
        goto done;
      returned = r.parameters[2];
      step = 2;
    } else if (step == 2) {
      if (strcmp(r.stage, "created-handle") || r.opcode || r.result ||
          r.response || r.parameter_count || r.object.handle != returned)
        goto done;
      s->objects[index] = r.object;
      step = index >= 3 ? 3 : 5;
    } else if (step == 3) {
      if (strcmp(r.stage, "intent") || r.opcode != PTP_OC_SendObject ||
          r.result || r.response || r.parameter_count ||
          r.object.handle != returned)
        goto done;
      step = 4;
    } else if (step == 4) {
      if (strcmp(r.stage, "response") || r.opcode != PTP_OC_SendObject ||
          r.result || r.response != PTP_RC_OK || r.parameter_count ||
          r.object.handle != returned)
        goto done;
      step = 5;
    } else {
      if (strcmp(r.stage, "verified-object") || r.opcode || r.result ||
          r.response || r.parameter_count || r.object.handle != returned ||
          r.object.proven != 1 || !hd_bind_nonzero(r.object.persistent))
        goto done;
      s->objects[index] = r.object;
      ++index;
      step = 0;
    }
  }
  if (!stopped)
    goto done;
  s->phase = index;
  s->failed = s->uncertain = s->prepared = 0;
  gcry_md_hash_buffer(GCRY_MD_SHA256, s->recovery_journal_sha256, data, got);
  memcpy(s->recovery_source, path, strlen(path) + 1);
  result = 0;
done:
  free(data);
  return result;
}
/* Recognize only the observed one-track interrupted graph. A missing owned
 * empty album may be replaced; unrelated identities are never substituted. */
static int hd_album_recovery_classify(HdAlbumSync *s) {
  if (s->phase != 5 || !s->objects[4].proven ||
      !hd_bind_nonzero(s->objects[4].persistent))
    return -1;
  uint32_t *ids = NULL;
  size_t count = 0;
  if (hd_album_handles(s, 0, 0, &ids, &count))
    return -1;
  if (hd_album_id_present(ids, count, s->objects[4].handle)) {
    free(ids);
    return 0;
  }
  int good = 1;
  unsigned extras = 0;
  for (size_t i = 0; i < s->baseline_count; ++i)
    if (!hd_album_id_present(ids, count, s->baseline[i]))
      good = 0;
  for (unsigned i = 0; i <= 5; ++i)
    if (i != 4 && !hd_album_id_present(ids, count, s->objects[i].handle))
      good = 0;
  memset(s->recovery_extras, 0, sizeof(s->recovery_extras));
  for (size_t i = 0; i < count && good; ++i) {
    int known = hd_album_id_present(s->baseline, s->baseline_count, ids[i]);
    for (unsigned j = 0; j <= 5; ++j)
      known |= ids[i] == s->objects[j].handle;
    if (known)
      continue;
    uint8_t v[2];
    if (hd_album_value(s, ids[i], MTP_OPC_ObjectFormat, v, 2)) {
      good = 0;
      break;
    }
    uint16_t format = hd_art_u16(v);
    unsigned kind = format == MTP_OFC_AbstractArtist ? 1 : 0;
    if ((format != MTP_OFC_AbstractArtist &&
         format != MTP_OFC_AbstractAudioAlbum) ||
        s->recovery_extras[kind].handle) {
      good = 0;
      break;
    }
    s->recovery_extras[kind].handle = ids[i];
    ++extras;
  }
  if (extras != 2)
    good = 0;
  if (good && (hd_album_extra_verify(s, 1) || hd_album_extra_verify(s, 0) ||
               hd_album_closed_links(s, ids, count)))
    good = 0;
  if (good) {
    uint8_t *data = NULL;
    uint32_t n = 0, p[5] = {s->recovery_extras[0].handle};
    int rc = hd_album_tx(s, UINT32_MAX, MTP_OC_GetObjectReferences, p, 1,
                         PTP_DP_GETDATA, NULL, 0, &data, &n, NULL, 0);
    good = !rc && data && n == 8 && hd_art_u32(data) == 1 &&
           hd_art_u32(data + 4) == s->objects[5].handle;
    free(data);
  }
  free(ids);
  if (!good) {
    memset(s->recovery_extras, 0, sizeof(s->recovery_extras));
    return -1;
  }
  s->replace_missing_album = 1;
  s->extras_recorded = 1;
  s->replacement_original = s->objects[4];
  fprintf(
      s->out,
      "album-recovery-observed\tmissing-owned-album=%u placeholder-album=%u "
      "placeholder-artist=%u; no writes or deletions\n",
      s->objects[4].handle, s->recovery_extras[0].handle,
      s->recovery_extras[1].handle);
  return 0;
}
static int hd_album_recovery_inspect(HdAlbumSync *s) {
  if (!hd_album_allowed(s) ||
      (!s->extras_recorded && hd_album_recovery_classify(s)))
    return -1;
  int rc = hd_album_prefix_proof(s, s->phase);
  s->recovery_verified = !rc;
  return rc;
}
/* Explicit continuation: the original journal remains unchanged. No device
 * mutation until the network-ready callback repeats the entire prefix proof. */
static int hd_album_recovery_prepare(HdAlbumSync *s, const char *journal) {
  if (!s->recovery_verified || !s->recovery_source[0] || s->fd >= 0 ||
      !hd_album_allowed(s) || hd_album_prefix_proof(s, s->phase))
    return -1;
  if (hd_album_plan_journal(s, journal, "resume-baseline-plan"))
    return -1;
  struct {
    char magic[8];
    uint32_t tracks, cover_bytes;
    uint8_t journal[32], cover[32], audio[HD_ALBUM_TRACK_MAX][32];
  } hashes = {0};
  memcpy(hashes.magic, "ZUALSHA1", 8);
  hashes.tracks = (uint32_t)s->track_count;
  hashes.cover_bytes = (uint32_t)s->cover_size;
  memcpy(hashes.journal, s->recovery_journal_sha256, 32);
  gcry_md_hash_buffer(GCRY_MD_SHA256, hashes.cover, s->cover, s->cover_size);
  for (size_t i = 0; i < s->track_count; ++i)
    gcry_md_hash_buffer(GCRY_MD_SHA256, hashes.audio[i], s->tracks[i].data,
                        s->tracks[i].object.size);
  uint32_t through = (uint32_t)s->phase;
  if (hd_first_write_all(s->fd, s->recovery_source,
                         sizeof(s->recovery_source)) ||
      hd_first_write_all(s->fd, &through, sizeof(through)) ||
      hd_first_write_all(s->fd, s->objects, sizeof(s->objects)) ||
      hd_first_write_all(s->fd, &s->replacement_original,
                         sizeof(s->replacement_original)) ||
      hd_first_write_all(s->fd, s->recovery_extras,
                         sizeof(s->recovery_extras)) ||
      hd_first_write_all(s->fd, &hashes, sizeof(hashes)) || fsync(s->fd)) {
    s->failed = 1;
    return -1;
  }
  if (hd_album_record(s, "resume-prefix-verified", through, 0, 0, 0, NULL, 0))
    return -1;
  s->resume_through = s->phase;
  s->phase++;
  s->resume_pending = 1;
  s->prepared = 1;
  fprintf(s->out,
          "album-resume-prepared\tknown-through=%zu next=%zu; original objects "
          "preserved\n",
          s->resume_through, s->phase);
  return fflush(s->out) || ferror(s->out) ? -1 : 0;
}

#endif
