#include "../tools/hd_art_album_recovery.h"
#include <assert.h>
#define OBJECTS 80
#define PROPS 24
typedef struct {
  uint16_t code;
  size_t n;
  uint8_t data[1024];
} Prop;
typedef struct {
  uint32_t id, parent, format, size;
  Prop props[PROPS];
  unsigned count;
  uint8_t *body;
  uint32_t refs[64];
  size_t ref_count;
} Obj;
static Obj objects[OBJECTS];
static unsigned object_count, mutations, calls, checks;
static int fail_mode;
static uint32_t hidden[3];
static int64_t clock_ms = 1;
static volatile sig_atomic_t stop;
static const char *guid = "95a0ca20-55d9-4d67-bb32-31096398fc5a";
static ZuneDevice dev;
static HdAlbumSync state;
static FILE *output;
static char tempdir[128], plan[256], journal[256], audio1[256], audio2[256],
    cover[256];
static int64_t now(void) { return clock_ms; }
static void check(int ok, const char *label) {
  if (!ok) {
    fprintf(stderr, "FAIL %s\n", label);
    exit(1);
  }
  printf("PASS %s\n", label);
  ++checks;
}
static Obj *find(uint32_t id) {
  for (unsigned i = 0; i < object_count; ++i)
    if (objects[i].id == id)
      return &objects[i];
  return NULL;
}
static Prop *prop(Obj *o, uint16_t code) {
  for (unsigned i = 0; i < o->count; ++i)
    if (o->props[i].code == code)
      return &o->props[i];
  assert(o->count < PROPS);
  Prop *p = &o->props[o->count++];
  p->code = code;
  return p;
}
static void set(Obj *o, uint16_t code, const uint8_t *v, size_t n) {
  Prop *p = prop(o, code);
  assert(n <= sizeof(p->data));
  memcpy(p->data, v, n);
  p->n = n;
}
static void textprop(Obj *o, uint16_t code, const char *value) {
  uint8_t w[511];
  size_t n = hd_album_string(value, w);
  assert(n);
  set(o, code, w, n);
}
static void copyout(uint8_t **data, uint32_t *len, const void *p, size_t n) {
  *data = malloc(n ? n : 1);
  assert(*data);
  memcpy(*data, p, n);
  *len = (uint32_t)n;
}
int ptp_transaction(ptp_session_t *ptp, uint16_t op, uint32_t *p, int pc,
                    uint16_t flags, uint8_t *send, uint64_t size,
                    uint8_t **data, uint32_t *len, uint16_t *rc,
                    uint32_t *reply, int *nreply) {
  ++calls;
  *rc = ptp->last_response = PTP_RC_OK;
  *nreply = 0;
  if (data)
    *data = NULL;
  if (len)
    *len = 0;
  assert(op != PTP_OC_DeleteObject);
  assert(op == PTP_OC_GetObjectHandles || op == MTP_OC_GetObjectPropValue ||
         op == MTP_OC_SendObjectPropList || op == PTP_OC_SendObject ||
         op == PTP_OC_GetObject || op == MTP_OC_GetObjectReferences ||
         op == MTP_OC_SetObjectReferences || op == MTP_OC_SetObjectPropValue ||
         op == 0x9108);
  if (fail_mode == 1 && op == PTP_OC_GetObjectHandles) {
    *rc = ptp->last_response = PTP_ERROR_TIMEOUT;
    return -1;
  }
  if (op == PTP_OC_GetObjectHandles) {
    assert(pc == 3 && flags == PTP_DP_GETDATA);
    uint8_t b[4 + 4 * OBJECTS];
    unsigned count = 0;
    for (unsigned i = 0; i < object_count; ++i)
      if (objects[i].id != hidden[0] && objects[i].id != hidden[1] &&
          objects[i].id != hidden[2] && (!p[1] || objects[i].format == p[1]) &&
          (!p[2] || objects[i].parent == p[2]))
        hd_first_u32(b + 4 + 4 * count++, objects[i].id);
    hd_first_u32(b, count);
    copyout(data, len, b, 4 + 4 * count);
  } else if (op == MTP_OC_GetObjectPropValue) {
    assert(pc == 2 && flags == PTP_DP_GETDATA);
    Obj *o = find(p[0]);
    assert(o);
    uint8_t b[16] = {0};
    size_t n = 4;
    if (p[1] == MTP_OPC_ObjectFormat) {
      hd_first_u16(b, (uint16_t)o->format);
      n = 2;
    } else if (p[1] == MTP_OPC_StorageID)
      hd_first_u32(b, dev.storage_id);
    else if (p[1] == MTP_OPC_ParentObject)
      hd_first_u32(b, o->parent);
    else if (p[1] == MTP_OPC_ObjectSize) {
      hd_first_u32(b, o->size);
      n = 8;
    } else if (p[1] == 0xdc41) {
      hd_first_u32(b, o->id);
      b[15] = 0x71;
      n = 16;
    } else {
      Prop *v = prop(o, (uint16_t)p[1]);
      assert(v->n);
      copyout(data, len, v->data, v->n);
      return 0;
    }
    copyout(data, len, b, n);
  } else if (op == MTP_OC_SendObjectPropList) {
    assert(pc == 5 && flags == PTP_DP_SENDDATA);
    ++mutations;
    assert(state.fd >= 0);
    struct stat st;
    assert(!fstat(state.fd, &st) && st.st_size > 0);
    if (fail_mode == 2) {
      *nreply = 3;
      reply[0] = dev.storage_id;
      reply[1] = p[1];
      reply[2] = 1;
      return 0;
    }
    assert(object_count < OBJECTS);
    Obj *o = &objects[object_count++];
    o->id = 100 + object_count;
    o->parent = p[1];
    o->format = p[2];
    o->size = p[4];
    size_t at = 4;
    uint32_t count = hd_art_u32(send);
    for (uint32_t i = 0; i < count; ++i) {
      assert(at + 8 <= size && hd_art_u32(send + at) == 0);
      uint16_t code = hd_art_u16(send + at + 4),
               type = hd_art_u16(send + at + 6);
      at += 8;
      size_t n = type == PTP_DTC_STR      ? 1 + 2 * send[at]
                 : type == PTP_DTC_UINT8  ? 1
                 : type == PTP_DTC_UINT16 ? 2
                 : type == PTP_DTC_UINT32 ? 4
                 : type == HD_ART_UINT128 ? 16
                                          : 0;
      assert(n && at + n <= size);
      set(o, code, send + at, n);
      at += n;
    }
    assert(at == size);
    *nreply = fail_mode == 4 ? 2 : 4;
    reply[0] = dev.storage_id;
    reply[1] = p[1];
    reply[2] = o->id;
    reply[3] = 0;
  } else if (op == PTP_OC_SendObject) {
    assert(!pc && flags == PTP_DP_SENDDATA);
    ++mutations;
    Obj *o = &objects[object_count - 1];
    assert(size == o->size);
    if (size) {
      o->body = malloc(size);
      assert(o->body);
      memcpy(o->body, send, size);
      if (fail_mode == 3)
        o->body[0] ^= 1;
      if (fail_mode == 6 || fail_mode == 7) {
        uint8_t zero[4] = {0};
        set(o, 0xdab8, zero, 4);
        textprop(o, MTP_OPC_AlbumName, "Unknown Album");
      }
    }
  } else if (op == PTP_OC_GetObject) {
    assert(pc == 1 && flags == PTP_DP_GETDATA);
    Obj *o = find(p[0]);
    assert(o && o->body);
    copyout(data, len, o->body, o->size);
  } else if (op == MTP_OC_GetObjectReferences) {
    assert(pc == 1 && flags == PTP_DP_GETDATA);
    Obj *o = find(p[0]);
    assert(o);
    uint8_t b[260];
    hd_first_u32(b, (uint32_t)o->ref_count);
    for (size_t i = 0; i < o->ref_count; ++i)
      hd_first_u32(b + 4 + 4 * i, o->refs[i]);
    copyout(data, len, b, 4 + 4 * o->ref_count);
  } else if (op == MTP_OC_SetObjectReferences) {
    assert(pc == 1 && flags == PTP_DP_SENDDATA);
    ++mutations;
    Obj *o = find(p[0]);
    assert(o && size >= 4);
    o->ref_count = hd_art_u32(send);
    assert(size == 4 + 4 * o->ref_count && o->ref_count <= 64);
    for (size_t i = 0; i < o->ref_count; ++i)
      o->refs[i] = hd_art_u32(send + 4 + 4 * i);
    if (fail_mode != 7)
      for (size_t i = 0; i < o->ref_count; ++i)
        textprop(find(o->refs[i]), MTP_OPC_AlbumName, state.album);
  } else if (op == MTP_OC_SetObjectPropValue) {
    assert(pc == 2 && flags == PTP_DP_SENDDATA &&
           p[1] == MTP_OPC_RepresentativeSampleData);
    ++mutations;
    set(find(p[0]), (uint16_t)p[1], send, size);
  } else {
    assert(!pc && flags == PTP_DP_NODATA);
    ++mutations;
    *nreply = 1;
    reply[0] = 0;
  }
  return 0;
}
static void write_plan(const char *suffix) {
  FILE *f = fopen(plan, "wb");
  assert(f);
  fprintf(f,
          "ZUUNED-HD-ALBUM\t1\nartist\tAlbum Artist\nalbum\tFull "
          "Album\nyear\t2020\ncover\t%s\ntrack\t1\t1\t30000\tFirst "
          "Song\t%s%s\ntrack\t1\t2\t42000\tSecond Song\t%s\n",
          cover, audio1, suffix, audio2);
  assert(!fclose(f));
}
static void setup(void) {
  for (unsigned i = 0; i < object_count; ++i)
    free(objects[i].body);
  memset(objects, 0, sizeof(objects));
  object_count = 3;
  mutations = calls = 0;
  fail_mode = 0;
  memset(hidden, 0, sizeof(hidden));
  stop = 0;
  clock_ms = 1;
  memset(&dev, 0, sizeof(dev));
  dev.device_family = ZUNE_FAMILY_PAVO;
  dev.mtpz_authenticated = 1;
  dev.serial = "mock-album-HD";
  dev.storage_id = 65537;
  const char *roots[] = {"Music", "Artists", "Albums"};
  for (unsigned i = 0; i < 3; ++i) {
    objects[i].id = i + 1;
    objects[i].format = MTP_OFC_Association;
    textprop(&objects[i], MTP_OPC_ObjectFileName, roots[i]);
  }
  unlink(journal);
  write_plan("");
  assert(!hd_album_local(&state, plan));
  assert(!hd_album_set_guid(&state, guid));
  state.dev = &dev;
  state.out = output;
  state.stop = &stop;
  state.now_fn = now;
  state.deadline = 100000;
}
static void teardown(void) { hd_album_release(&state); }
static int graph(void) {
  int rc = 0;
  for (unsigned i = 0; i < 10 && !state.graph_ready && !rc; ++i)
    rc = hd_album_tick(&state, 1);
  return rc;
}
int main(int argc, char **argv) {
  if (argc == 3) {
    int rc = hd_album_local(&state, argv[1]) ||
             hd_album_recovery_load(&state, argv[2]);
    if (!rc)
      printf("album-journal-loaded tracks=%zu phase=%zu handle=%u\n",
             state.track_count, state.phase, state.objects[state.phase].handle);
    hd_album_release(&state);
    return rc ? 1 : 0;
  }

  snprintf(tempdir, sizeof(tempdir), "/tmp/hd-album-test-XXXXXX");
  assert(mkdtemp(tempdir));
  snprintf(plan, sizeof(plan), "%s/plan.tsv", tempdir);
  snprintf(journal, sizeof(journal), "%s/journal", tempdir);
  snprintf(audio1, sizeof(audio1), "%s/one.mp3", tempdir);
  snprintf(audio2, sizeof(audio2), "%s/two.mp3", tempdir);
  snprintf(cover, sizeof(cover), "%s/cover.jpg", tempdir);
  const char *files[] = {audio1, audio2};
  for (unsigned i = 0; i < 2; ++i) {
    FILE *f = fopen(files[i], "wb");
    assert(f);
    assert(fwrite("ID3fixture-audio", 1, 16, f) == 16);
    assert(!fclose(f));
  }
  FILE *f = fopen(cover, "wb");
  assert(f);
  uint8_t jpeg[] = {255, 216, 0, 0, 255, 217};
  assert(fwrite(jpeg, 1, sizeof(jpeg), f) == sizeof(jpeg));
  fclose(f);
  output = tmpfile();
  assert(output);
  uint8_t wire[511];
  check(
      hd_album_string("Björk \xf0\x9f\x8c\x99", wire) &&
          hd_first_name_match(wire, 1 + 2 * wire[0], "Björk \xf0\x9f\x8c\x99"),
      "strict UTF8 and supplementary character roundtrip");
  check(!hd_album_string("bad\xc0\x80", wire) &&
            !hd_album_string("bad\xed\xa0\x80", wire),
        "overlong UTF8 and surrogate encoding rejected");
  setup();
  check(state.track_count == 2 && state.total_bytes == 32 &&
            !strcmp(state.artist, "Album Artist") &&
            state.tracks[1].object.duration == 42000,
        "ordered full-album plan preserves actual metadata and bytes");
  check(!hd_album_prepare(&state, journal) && !mutations,
        "album preflight creates durable journal with no USB mutation");
  check(!hd_album_tick(&state, 0) && !mutations,
        "album mutation waits for bidirectional network readiness");
  check(!graph() && state.graph_ready && state.cover_done && object_count == 9,
        "whole graph with two independent tracks and distinct album cover "
        "verified");
  Obj *album = find(state.objects[4].handle);
  check(album->ref_count == 2 && album->refs[0] == state.objects[5].handle &&
            album->refs[1] == state.objects[6].handle,
        "album references preserve full ordered track list");
  unsigned before = mutations;
  check(!hd_album_complete(&state, 1, 0, 1, 1) && state.retained &&
            mutations == before + 1,
        "success retains all tracks and sends exactly one reindex handoff");
  teardown();
  for (unsigned bad = 0; bad < 4; ++bad) {
    setup();
    if (bad == 0)
      dev.device_family = ZUNE_FAMILY_KEEL;
    else if (bad == 1)
      dev.mtpz_authenticated = 0;
    else if (bad == 2)
      stop = 1;
    else
      clock_ms = state.deadline;
    check(hd_album_prepare(&state, journal) && !calls && !mutations,
          "non-HD unauthenticated cancelled or expired preflight makes no "
          "transaction");
    teardown();
  }
  for (unsigned same_guid = 0; same_guid < 2; ++same_guid) {
    setup();
    Obj *existing = &objects[object_count++];
    existing->id = 44;
    existing->format = MTP_OFC_AbstractArtist;
    textprop(existing, MTP_OPC_Name,
             same_guid ? "Different Artist" : state.artist);
    set(existing, 0xda97, state.guid_wire, 16);
    check(hd_album_prepare(&state, journal) && !mutations && state.fd < 0,
          "existing artist name or catalog GUID refuses before journal and "
          "writes");
    teardown();
  }
  setup();
  fail_mode = 1;
  check(hd_album_prepare(&state, journal) && state.uncertain && !mutations,
        "baseline timeout preserves uncertainty with no write");
  teardown();
  for (int mode = 2; mode <= 4; ++mode) {
    setup();
    assert(!hd_album_prepare(&state, journal));
    fail_mode = mode;
    check(graph() && state.failed && state.uncertain,
          "collision malformed reply or changed payload stops graph with "
          "recovery state");
    before = mutations;
    check(hd_album_tick(&state, 1) && mutations == before,
          "failed graph never retries or deletes");
    teardown();
    if (mode == 4) {
      assert(!hd_album_local(&state, plan));
      check(hd_album_recovery_load(&state, journal),
            "malformed successful creation response cannot be resumed");
      teardown();
    }
  }
  setup();
  assert(!hd_album_prepare(&state, journal));
  stop = 1;
  check(hd_album_tick(&state, 1) && !mutations,
        "cancellation before first mutation leaves baseline untouched");
  teardown();
  setup();
  assert(!hd_album_prepare(&state, journal));
  close(state.fd);
  state.fd = -1;
  check(hd_album_tick(&state, 1) && !mutations,
        "journal failure prevents next device mutation");
  teardown();
  setup();
  assert(!hd_album_prepare(&state, journal));
  assert(!graph());
  before = mutations;
  check(hd_album_complete(&state, 1, 0, 0, 1) && !state.retained &&
            mutations == before,
        "missing background ACK preserves graph without false successful "
        "retention");
  teardown();
  setup();
  assert(!hd_album_prepare(&state, journal));
  assert(!graph());
  find(state.objects[4].handle)->refs[0] = 1;
  before = mutations;
  check(hd_album_complete(&state, 1, 0, 1, 1) && !state.retained &&
            mutations == before,
        "changed reference refuses retention and never deletes");
  teardown();
  setup();
  assert(!hd_album_prepare(&state, journal));
  fail_mode = 7;
  check(graph() && !state.graph_ready && state.failed,
        "Unknown Album is refused after explicit album linking");
  teardown();
  setup();
  assert(!hd_album_prepare(&state, journal));
  fail_mode = 3;
  assert(graph());
  unsigned failed_track = state.objects[state.phase].handle;
  teardown();
  assert(!hd_album_local(&state, plan));
  check(!hd_album_recovery_load(&state, journal) && state.phase == 5 &&
            state.objects[5].handle == failed_track,
        "strict interrupted journal reconstructs only successful returned "
        "handles");
  state.dev = &dev;
  state.out = output;
  state.stop = &stop;
  state.now_fn = now;
  state.deadline = 100000;
  before = mutations;
  check(hd_album_recovery_inspect(&state) && mutations == before,
        "read-only inspection detects changed payload without writes");
  find(failed_track)->body[0] ^= 1;
  check(!hd_album_recovery_inspect(&state) && mutations == before,
        "exact interrupted prefix identity payload inventory and empty refs "
        "verified read-only");
  state.tracks[0].data[15] ^= 1;
  check(hd_album_recovery_inspect(&state) && mutations == before,
        "same-size changed source bytes refuse existing-track recovery proof");
  state.tracks[0].data[15] ^= 1;
  uint8_t zero_disc[4] = {0};
  set(find(failed_track), 0xdab8, zero_disc, 4);
  textprop(find(failed_track), MTP_OPC_AlbumName, "Unknown Album");
  check(!hd_album_recovery_inspect(&state) && mutations == before,
        "Pavo provisional album and default disc normalization preserve exact "
        "source metadata");
  state.objects[5].disc = 2;
  check(hd_album_recovery_inspect(&state) && mutations == before,
        "nondefault disc mismatch remains strict");
  state.objects[5].disc = 1;
  assert(!hd_album_recovery_inspect(&state));
  char continuation[256];
  snprintf(continuation, sizeof(continuation), "%s/resume", tempdir);
  check(!hd_album_recovery_prepare(&state, continuation) && state.phase == 6 &&
            mutations == before,
        "resume journals complete known graph without changing device objects");
  unsigned objects_before = object_count;
  fail_mode = 6;
  check(!graph() && state.graph_ready && object_count == objects_before + 1 &&
            state.objects[5].handle == failed_track,
        "resume creates only remaining track and links complete ordered album");
  check(!hd_album_complete(&state, 1, 0, 1, 1) && state.retained,
        "resumed full album requires exact final computed album metadata");
  unlink(continuation);
  teardown();
  assert(!hd_album_local(&state, plan));
  assert(!chmod(journal, 0644));
  check(hd_album_recovery_load(&state, journal),
        "public journal permissions rejected");
  teardown();
  assert(!chmod(journal, 0600));
  assert(!hd_album_local(&state, plan));
  f = fopen(journal, "ab");
  assert(f);
  fputc(0, f);
  fclose(f);
  check(hd_album_recovery_load(&state, journal),
        "trailing journal bytes rejected");
  teardown();
  for (unsigned prune = 0; prune < 4; ++prune) {
    setup();
    assert(!hd_album_prepare(&state, journal));
    fail_mode = 3;
    assert(graph());
    uint32_t track = state.objects[5].handle,
             old_album = state.objects[4].handle;
    find(track)->body[0] ^= 1;
    hidden[0] = old_album;
    for (unsigned k = 0; k < 2; ++k) {
      Obj *extra = &objects[object_count++];
      extra->id = 300 + k;
      extra->format = k ? MTP_OFC_AbstractArtist : MTP_OFC_AbstractAudioAlbum;
      textprop(extra, MTP_OPC_Name, k ? "Unknown Artist" : "Unknown Album");
      if (k)
        textprop(extra, MTP_OPC_ObjectFileName, "Unknown Artist.art");
      else {
        uint8_t empty = 0;
        set(extra, MTP_OPC_ObjectFileName, &empty, 1);
      }
      uint8_t zero[16] = {0};
      set(extra, 0xda97, zero, 16);
      if (!k) {
        hd_first_u32(zero, 301);
        set(extra, 0xdab9, zero, 4);
        extra->refs[0] = track;
        extra->ref_count = 1;
      }
    }
    teardown();
    assert(!hd_album_local(&state, plan));
    assert(!hd_album_recovery_load(&state, journal));
    state.dev = &dev;
    state.out = output;
    state.stop = &stop;
    state.now_fn = now;
    state.deadline = 100000;
    before = mutations;
    check(!hd_album_recovery_inspect(&state) && state.replace_missing_album &&
              state.extras_recorded && mutations == before,
          "exact pruned owned album plus closed firmware placeholder graph "
          "recognized read-only");
    find(300)->refs[0] = 1;
    check(hd_album_recovery_inspect(&state) && mutations == before,
          "foreign placeholder reference refuses replacement");
    find(300)->refs[0] = track;
    assert(!hd_album_recovery_inspect(&state));
    snprintf(continuation, sizeof(continuation), "%s/resume-placeholder",
             tempdir);
    unlink(continuation);
    assert(!hd_album_recovery_prepare(&state, continuation));
    if (prune >= 2) {
      if (prune == 2)
        find(300)->refs[0] = 1;
      else
        hidden[0] = 0;
      before = mutations;
      check(hd_album_tick(&state, 1) && mutations == before,
            "changed placeholder link or returned old album blocks first "
            "network-ready write");
      teardown();
      unlink(continuation);
      continue;
    }
    fail_mode = 6;
    check(!graph() && state.graph_ready &&
              state.objects[4].handle != old_album &&
              state.objects[5].handle == track &&
              state.objects[4].handle != 300 && state.objects[4].handle != 301,
          "resume replaces only proven missing own album and preserves known "
          "track plus placeholders");
    if (prune) {
      hidden[1] = 300;
      hidden[2] = 301;
    }
    check(!hd_album_complete(&state, 1, 0, 1, 1),
          "final inventory permits exact identified placeholders retained or "
          "firmware-pruned");
    teardown();
    unlink(continuation);
  }
  for (unsigned i = 0; i < 2; ++i) {
    setup();
    teardown();
    write_plan(i ? "\t" : "\textra");
    check(hd_album_local(&state, plan) != 0,
          "seventh field or trailing tab rejected before device access");
  }
  for (unsigned i = 0; i < object_count; ++i)
    free(objects[i].body);
  fclose(output);
  unlink(audio1);
  unlink(audio2);
  unlink(cover);
  unlink(plan);
  unlink(journal);
  rmdir(tempdir);
  printf("HD full album: %u checks passed; no USB\n", checks);
  return 0;
}
