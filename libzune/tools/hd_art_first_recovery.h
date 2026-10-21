/* Narrow recovery for a stopped Music-root creation, never general adoption. */
#ifndef ZUNETOOL_HD_ART_FIRST_RECOVERY_H
#define ZUNETOOL_HD_ART_FIRST_RECOVERY_H
#include "hd_art_first_sync.h"
static int hd_first_read_all(int fd, void *out, size_t n) {
  uint8_t *p = out;
  while (n) {
    ssize_t r = read(fd, p, n);
    if (r < 0 && errno == EINTR)
      continue;
    if (r <= 0)
      return -1;
    p += r;
    n -= (size_t)r;
  }
  return 0;
}
static int hd_first_recovery_load(HdFirstSync *s, const char *path) {
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  struct stat a, b;
  HdFirstRecord first, last;
  size_t count = 0;
  uint32_t *ids = NULL;
  int result = -1;
  if (fd < 0)
    return -1;
  if (fstat(fd, &a) || !S_ISREG(a.st_mode) || a.st_uid != geteuid() ||
      (a.st_mode & 0777) != 0600 || a.st_nlink != 1 || a.st_size < 0 ||
      a.st_size >
          (off_t)(2 * sizeof(first) + sizeof(count) + HD_FIRST_MAX_HANDLES * 4))
    goto done;
  if (hd_first_read_all(fd, &first, sizeof(first)) ||
      memcmp(first.magic, "ZUHDF1\0\0", 8) ||
      memcmp(first.stage, "baseline-no-matching-objects", 28) ||
      !memchr(first.name, 0, sizeof(first.name)) ||
      strncmp(first.name, "HDArtworkProbe-", 15) ||
      !memchr(first.serial, 0, sizeof(first.serial)) || !first.serial[0] ||
      memcmp(first.guid, hd_art_test_guid_wire, 16) || !first.storage ||
      first.create_music != 1 || first.music_owned || first.refs_set ||
      first.roots[0].handle || first.roots[0].parent ||
      first.roots[0].format != MTP_OFC_Association ||
      memcmp(first.roots[0].filename, "Music\0", 6) ||
      hd_first_read_all(fd, &count, sizeof(count)) ||
      count > HD_FIRST_MAX_HANDLES)
    goto done;
  if (a.st_size != (off_t)(2 * sizeof(first) + sizeof(count) + 4 * count))
    goto done;
  ids = calloc(count ? count : 1, sizeof(*ids));
  if (!ids)
    goto done;
  if (hd_first_read_all(fd, ids, 4 * count) ||
      hd_first_read_all(fd, &last, sizeof(last)))
    goto done;
  if (memcmp(last.stage, "intent-create-owned-Music-root", 30))
    goto done;
  memcpy(last.stage, first.stage, sizeof(last.stage));
  if (memcmp(&last, &first, sizeof(first)))
    goto done;
  for (unsigned i = 0; i < 5; ++i)
    if (first.objects[i].handle)
      goto done;
  for (size_t i = 0; i < count; ++i) {
    if (!ids[i] || ids[i] == UINT32_MAX)
      goto done;
    for (size_t j = 0; j < i; ++j)
      if (ids[j] == ids[i])
        goto done;
  }
  for (unsigned i = 1; i < 3; ++i) {
    HdFirstObject *r = &first.roots[i];
    int found = 0;
    if (!r->handle || r->parent || r->storage != first.storage ||
        r->format != MTP_OFC_Association || r->proven != 1 || r->deleted ||
        !hd_bind_nonzero(r->persistent) ||
        memcmp(r->filename, i == 1 ? "Artists\0" : "Albums\0", i == 1 ? 8 : 7))
      goto done;
    for (size_t j = 0; j < count; ++j)
      if (ids[j] == r->handle)
        found = 1;
    if (!found)
      goto done;
  }
  if (fstat(fd, &b) || a.st_dev != b.st_dev || a.st_ino != b.st_ino ||
      a.st_size != b.st_size || a.st_mtim.tv_sec != b.st_mtim.tv_sec ||
      a.st_mtim.tv_nsec != b.st_mtim.tv_nsec ||
      a.st_ctim.tv_sec != b.st_ctim.tv_sec ||
      a.st_ctim.tv_nsec != b.st_ctim.tv_nsec)
    goto done;
  memset(s, 0, sizeof(*s));
  s->fd = -1;
  s->storage = first.storage;
  s->mp3_size = first.bytes;
  memcpy(s->name, first.name, sizeof(s->name));
  memcpy(s->serial, first.serial, sizeof(s->serial));
  memcpy(s->roots, first.roots, sizeof(s->roots));
  memcpy(s->objects, first.objects, sizeof(s->objects));
  s->create_music = 1;
  s->baseline = ids;
  s->baseline_count = count;
  ids = NULL;
  result = 0;
done:
  free(ids);
  close(fd);
  return result;
}
static int hd_first_recovery_inspect(HdFirstSync *s) {
  if (!hd_first_allowed(s, 0) || hd_first_verify(s, &s->roots[1], 0) ||
      hd_first_verify(s, &s->roots[2], 0))
    return -1;
  uint32_t *ids = NULL, candidate = 0;
  size_t count = 0;
  if (hd_first_handles(s, 0, &ids, &count, 0))
    return -1;
  int good = count == s->baseline_count + 1;
  for (size_t i = 0; i < s->baseline_count && good; ++i) {
    int found = 0;
    for (size_t j = 0; j < count; ++j)
      if (ids[j] == s->baseline[i])
        found = 1;
    if (!found)
      good = 0;
  }
  for (size_t i = 0; i < count && good; ++i) {
    int found = 0;
    for (size_t j = 0; j < s->baseline_count; ++j)
      if (ids[i] == s->baseline[j])
        found = 1;
    if (!found) {
      if (candidate)
        good = 0;
      candidate = ids[i];
    }
  }
  free(ids);
  if (!good || !candidate)
    return -1;
  HdFirstObject root = s->roots[0];
  if (root.handle && root.handle != candidate)
    return -1;
  root.handle = candidate;
  if (hd_first_verify(s, &root, 0))
    return -1;
  uint8_t *size = NULL;
  uint32_t n = 0;
  if (hd_first_get(s, candidate, MTP_OPC_ObjectSize, &size, &n, 0)) {
    free(size);
    return -1;
  }
  good = size && (n == 4 || n == 8) && !hd_art_u32(size) &&
         (n == 4 || !hd_art_u32(size + 4));
  free(size);
  if (!good)
    return -1;
  if (hd_first_children(s, 0, candidate, &ids, &count, 0))
    return -1;
  free(ids);
  if (count)
    return -1;
  /* A second root named Music would invalidate the original absence premise. */
  if (hd_first_handles(s, MTP_OFC_Association, &ids, &count, 0))
    return -1;
  unsigned matches = 0;
  for (size_t i = 0; i < count; ++i) {
    uint8_t parent[4], *name = NULL;
    uint32_t len = 0;
    size_t used = 0;
    if (hd_first_value(s, ids[i], MTP_OPC_ParentObject, parent, 4, 0) ||
        hd_first_get(s, ids[i], MTP_OPC_ObjectFileName, &name, &len, 0)) {
      free(name);
      free(ids);
      return -1;
    }
    if (!name || hd_art_string(name, len, &used) || used != len) {
      free(name);
      free(ids);
      return -1;
    }
    if (!hd_art_u32(parent) && hd_first_name_match(name, len, "Music")) {
      ++matches;
      if (ids[i] != candidate)
        good = 0;
    }
    free(name);
  }
  free(ids);
  if (!good || matches != 1)
    return -1;
  s->roots[0] = root;
  s->music_owned = 1;
  fprintf(s->out,
          "first-sync-recovery-inspection\thandle=%u exact single inventory "
          "addition; empty Music root; identity verified\n",
          candidate);
  return 0;
}
static int hd_first_recovery_proof(HdFirstSync *s, const char *path) {
  s->fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (s->fd < 0)
    return -1;
  if (hd_first_record(s, "recovery-exact-empty-Music-proof") ||
      hd_first_write_all(s->fd, &s->baseline_count,
                         sizeof(s->baseline_count)) ||
      hd_first_write_all(s->fd, s->baseline, 4 * s->baseline_count) ||
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
  int rc = fsync(fd);
  if (close(fd))
    rc = -1;
  return rc;
}
static int hd_first_recovery_delete(HdFirstSync *s) {
  if (hd_first_recovery_inspect(s) ||
      hd_first_record(s, "recovery-revalidated-before-delete") ||
      !hd_first_allowed(s, 0))
    return -1;
  s->started = 1;
  s->recovery_delete_guard = 1;
  return hd_first_finish(s, 1, 0);
}
#endif
