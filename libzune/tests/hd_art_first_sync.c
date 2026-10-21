#include "../tools/hd_art_graph_recovery.h"
#include <assert.h>
static HdFirstSync state;
static ZuneDevice dev;
static volatile sig_atomic_t stop;
static int64_t now;
static unsigned checks, transactions, mutations, deletions, commits, creates,
    link_writes;
static int mode, no_more, missing_root, duplicate_root, duplicate_artist,
    foreign_child, wrong_ref, changed_identity;
static off_t journal_size;
static char directory[128], manifest[160];
static uint8_t audio[] = {0xff, 0xfb, 0x90, 0x64, 1, 2, 3, 4};
typedef struct {
  HdFirstObject object;
  int live, committed;
  uint8_t guid[16];
} Object;
static Object objects[16];
static uint32_t refs[2];
static unsigned ref_count;
static int pending;
static int64_t mock_now(void) { return now; }
static void check(int value, const char *message) {
  if (!value) {
    fprintf(stderr, "FAIL %s\n", message);
    exit(1);
  }
  ++checks;
}
static Object *lookup(uint32_t id) {
  for (unsigned i = 0; i < 16; ++i)
    if (objects[i].live && objects[i].object.handle == id)
      return &objects[i];
  return NULL;
}
static void response_data(uint8_t **out, uint32_t *size, const void *data,
                          size_t n) {
  *out = malloc(n ? n : 1);
  assert(*out);
  memcpy(*out, data, n);
  *size = (uint32_t)n;
}
static size_t wire_string(uint8_t *data, const char *str) {
  size_t n = strlen(str);
  data[0] = (uint8_t)(n + 1);
  for (size_t i = 0; i < n; ++i)
    hd_first_u16(data + 1 + 2 * i, (uint8_t)str[i]);
  hd_first_u16(data + 1 + 2 * n, 0);
  return 3 + 2 * n;
}
static void journal_before_write(void) {
  struct stat st;
  assert(state.fd >= 0 && !fstat(state.fd, &st) && S_ISREG(st.st_mode) &&
         (st.st_mode & 0777) == 0600 && st.st_size > journal_size);
  journal_size = st.st_size;
  ++mutations;
}
static void parse_creation(uint8_t *data, size_t size, unsigned kind,
                           Object *target) {
  size_t pos = 4;
  uint32_t count = hd_art_u32(data);
  unsigned seen_guid = 0, seen_ref = 0, seen_file = 0, seen_name = 0,
           seen_album = 0;
  assert(count == (kind < 2                  ? 1
                   : kind == HD_FIRST_ARTIST ? 4
                   : kind == HD_FIRST_ALBUM  ? 6
                                             : 13));
  for (uint32_t i = 0; i < count; ++i) {
    assert(size - pos >= 8 && !hd_art_u32(data + pos));
    uint16_t prop = hd_art_u16(data + pos + 4),
             type = hd_art_u16(data + pos + 6);
    pos += 8;
    size_t n = 0;
    if (type == PTP_DTC_STR) {
      assert(!hd_art_string(data + pos, size - pos, &n));
    } else
      n = type == PTP_DTC_UINT8    ? 1
          : type == PTP_DTC_UINT16 ? 2
          : type == PTP_DTC_UINT32 ? 4
          : type == HD_ART_UINT128 ? 16
                                   : 0;
    assert(n && size - pos >= n);
    if (prop == 0xda97) {
      assert(kind == HD_FIRST_ARTIST && type == HD_ART_UINT128 && n == 16 &&
             !memcmp(data + pos, hd_art_test_guid_wire, 16));
      memcpy(target->guid, data + pos, 16);
      ++seen_guid;
    }
    if (prop == 0xdab9) {
      assert((kind == HD_FIRST_ALBUM || kind == HD_FIRST_TRACK) &&
             type == PTP_DTC_UINT32 &&
             hd_art_u32(data + pos) == state.objects[HD_FIRST_ARTIST].handle);
      ++seen_ref;
    }
    if (prop == MTP_OPC_ObjectFileName) {
      assert(hd_first_name_match(data + pos, n, state.objects[kind].filename));
      ++seen_file;
    }
    if (prop == MTP_OPC_Name) {
      assert(hd_first_name_match(data + pos, n, state.objects[kind].name));
      ++seen_name;
    }
    if (prop == MTP_OPC_AlbumName) {
      assert(kind == HD_FIRST_TRACK &&
             hd_first_name_match(data + pos, n,
                                 state.objects[HD_FIRST_ALBUM].name));
      ++seen_album;
    }
    if (prop == MTP_OPC_Duration)
      assert(
          type == PTP_DTC_UINT32 &&
          hd_art_u32(data + pos) ==
              (state.fixture_duration_ms ? state.fixture_duration_ms : 3000));
    pos += n;
  }
  assert(pos == size && seen_file == 1 && seen_name == (kind < 2 ? 0u : 1u) &&
         seen_guid == (kind == HD_FIRST_ARTIST) &&
         seen_ref == (kind >= HD_FIRST_ALBUM) &&
         seen_album == (kind == HD_FIRST_TRACK));
}
int ptp_transaction(ptp_session_t *session, uint16_t op, uint32_t *params,
                    int count, uint16_t flags, uint8_t *send,
                    uint64_t send_size, uint8_t **data, uint32_t *length,
                    uint16_t *rc, uint32_t *reply, int *reply_count) {
  assert(session == &dev.ptp && !no_more);
  ++transactions;
  *rc = session->last_response = PTP_RC_OK;
  *reply_count = 0;
  if (data)
    *data = NULL;
  if (length)
    *length = 0;
  if (op == PTP_OC_GetObjectHandles) {
    assert(flags == PTP_DP_GETDATA && count == 3 && params[0] == state.storage);
    uint8_t buffer[128] = {0};
    unsigned n = 0;
    for (unsigned i = 0; i < 16; ++i)
      if (objects[i].live &&
          (!params[1] || objects[i].object.format == params[1]) &&
          (!params[2] || objects[i].object.parent == params[2]))
        hd_first_u32(buffer + 4 + 4 * n++, objects[i].object.handle);
    if ((foreign_child == 1 &&
         params[2] == state.objects[HD_FIRST_ALBUM_FOLDER].handle) ||
        (foreign_child == 2 && params[2] == state.roots[0].handle))
      hd_first_u32(buffer + 4 + 4 * n++, 999);
    if (state.recovery_delete_guard && params[2] == 200) {
      if (mode == 12)
        stop = 1;
      if (mode == 13)
        now = state.deadline;
    }
    hd_first_u32(buffer, n);
    response_data(data, length, buffer, 4 + 4 * n);
    return 0;
  }
  if (op == MTP_OC_GetObjectPropValue) {
    assert(flags == PTP_DP_GETDATA && count == 2);
    Object *obj = lookup(params[0]);
    if (!obj) {
      *rc = session->last_response = 0x2009;
      return -1;
    }
    HdFirstObject *o = &obj->object;
    uint8_t buffer[512] = {0};
    size_t n = 0;
    switch (params[1]) {
    case MTP_OPC_ObjectFormat:
      hd_first_u16(buffer, o->format);
      n = 2;
      break;
    case MTP_OPC_StorageID:
      hd_first_u32(buffer, o->storage);
      n = 4;
      break;
    case MTP_OPC_ParentObject:
      hd_first_u32(buffer, o->parent);
      n = 4;
      break;
    case MTP_OPC_Name:
      n = wire_string(buffer, o->name);
      break;
    case MTP_OPC_ObjectFileName:
      if (mode == 15 && o->format == MTP_OFC_MP3) {
        n = wire_string(buffer, "wrong.mp3");
        break;
      }
      if (mode == 14 && o->format == MTP_OFC_MP3) {
        char normalized[160];
        snprintf(normalized, sizeof(normalized), "01 %.63s.mp3", o->name);
        n = wire_string(buffer, normalized);
        break;
      }
      n = wire_string(buffer, o->filename);
      break;
    case 0xdc41:
      memcpy(buffer, o->persistent, 16);
      if (changed_identity && params[0] == state.objects[HD_FIRST_TRACK].handle)
        buffer[0] ^= 0x40;
      n = 16;
      break;
    case 0xda97:
      memcpy(buffer, obj->guid, 16);
      n = 16;
      break;
    case 0xdab9:
      hd_first_u32(buffer, wrong_ref ? 999 : o->artist);
      n = 4;
      break;
    case MTP_OPC_Duration:
      hd_first_u32(buffer, mode == 19 ? 999
                                      : (state.fixture_duration_ms
                                             ? state.fixture_duration_ms
                                             : 3000));
      n = 4;
      break;
    case MTP_OPC_ObjectSize:
      hd_first_u32(buffer, o->size);
      n = 8;
      break;
    default:
      assert(!"unexpected property read");
    }
    response_data(data, length, buffer, n);
    return 0;
  }
  if (op == MTP_OC_SendObjectPropList) {
    assert(flags == PTP_DP_SENDDATA && count == 5 && !params[3]);
    journal_before_write();
    unsigned kind = state.create_music && !state.music_owned ? 99 : creates++;
    assert(kind < 5 || kind == 99);
    if (mode == 1) {
      *rc = session->last_response = PTP_ERROR_IO;
      no_more = 1;
      return -1;
    }
    if (mode == 2) {
      *rc = session->last_response = 0x2005;
      return -1;
    }
    if (kind == 99) {
      assert(params[0] == state.storage && !params[1] &&
             params[2] == MTP_OFC_Association && !params[4]);
      assert(send_size >= 15 && hd_art_u32(send) == 1 &&
             !hd_art_u32(send + 4) &&
             hd_art_u16(send + 8) == MTP_OPC_ObjectFileName &&
             hd_art_u16(send + 10) == PTP_DTC_STR &&
             hd_first_name_match(send + 12, send_size - 12, "Music"));
      Object *root = &objects[8];
      root->object = state.roots[0];
      root->object.handle = 200;
      root->object.persistent[0] = 200;
      root->live = root->committed = 1;
      reply[0] = state.storage;
      reply[1] = 0;
      reply[2] = mode == 3 ? 2 : 200;
      *reply_count = mode == 9 ? 4 : mode == 10 ? 5 : mode == 11 ? 2 : 3;
      reply[3] = 123;
      reply[4] = 456;
      if (mode == 3)
        no_more = 1;
      if (mode == 5) {
        close(state.fd);
        state.fd = -1;
        no_more = 1;
      }
      return 0;
    }
    Object *obj = &objects[3 + kind];
    obj->object = state.objects[kind];
    obj->object.handle = 100 + kind;
    obj->object.parent = params[1];
    obj->object.artist =
        kind >= HD_FIRST_ALBUM ? state.objects[HD_FIRST_ARTIST].handle : 0;
    obj->object.persistent[0] = (uint8_t)(100 + kind);
    obj->live = 1;
    obj->committed = kind < 2;
    assert(params[0] == state.storage &&
           params[1] == state.objects[kind].parent &&
           params[2] == obj->object.format &&
           params[4] == (kind == HD_FIRST_TRACK ? sizeof(audio) : 0));
    parse_creation(send, send_size, kind, obj);
    reply[0] = params[0];
    reply[1] = params[1];
    reply[2] = mode == 3 ? 1 : obj->object.handle;
    *reply_count = mode == 9 ? 4 : mode == 10 ? 5 : mode == 11 ? 2 : 3;
    reply[3] = 123;
    reply[4] = 456;
    pending = (int)kind;
    if (mode == 3)
      no_more = 1;
    if (mode == 4 && kind == HD_FIRST_ARTIST) {
      stop = 1;
      no_more = 1;
    }
    if (mode == 5) {
      close(state.fd);
      state.fd = -1;
      no_more = 1;
    }
    return 0;
  }
  if (op == PTP_OC_SendObject) {
    assert(flags == PTP_DP_SENDDATA && !count && pending >= HD_FIRST_ARTIST &&
           pending <= HD_FIRST_TRACK);
    journal_before_write();
    ++commits;
    assert(send_size == (pending == HD_FIRST_TRACK ? sizeof(audio) : 0));
    if (send_size)
      assert(!memcmp(send, audio, sizeof(audio)));
    else
      assert(!send);
    if (mode == 6) {
      *rc = session->last_response = 0x2005;
      no_more = 1;
      return -1;
    }
    objects[3 + pending].committed = 1;
    return 0;
  }
  if (op == PTP_OC_GetObject) {
    assert(flags == PTP_DP_GETDATA && count == 1 && params[0] == 104);
    response_data(data, length, audio, sizeof(audio));
    if (mode == 20)
      stop = 1;
    if (mode == 21) {
      close(state.fd);
      state.fd = -1;
      no_more = 1;
    }
    if (mode == 7) {
      (*data)[4] ^= 1;
      no_more = 1;
    }
    return 0;
  }
  if (op == MTP_OC_GetObjectReferences) {
    if (params[0] == 300 || params[0] == 10) {
      uint8_t b[8] = {0};
      int has = params[0] == 300 || mode == 16;
      hd_first_u32(b, has);
      hd_first_u32(b + 4, 104);
      response_data(data, length, b, has ? 8 : 4);
      return 0;
    }
    assert(flags == PTP_DP_GETDATA && count == 1 && params[0] == 103);
    uint8_t buffer[12] = {0};
    hd_first_u32(buffer, ref_count);
    for (unsigned i = 0; i < ref_count; ++i)
      hd_first_u32(buffer + 4 + 4 * i, refs[i]);
    response_data(data, length, buffer, 4 + 4 * ref_count);
    return 0;
  }
  if (op == MTP_OC_SetObjectReferences) {
    assert(flags == PTP_DP_SENDDATA && count == 1 && params[0] == 103);
    journal_before_write();
    ++link_writes;
    if ((mode == 17 || mode == 18) && send_size == 4)
      return 0; /* Firmware restores tagged album membership. */
    assert(send_size == 4 || send_size == 8);
    ref_count = hd_art_u32(send);
    assert(ref_count <= 1 && send_size == 4 + 4 * ref_count);
    if (ref_count) {
      refs[0] = hd_art_u32(send + 4);
      assert(refs[0] == 104);
    }
    return 0;
  }
  if (op == 0x9108) {
    assert(flags == PTP_DP_NODATA && !count);
    journal_before_write();
    if (mode == 25 || mode == 26) {
      *reply_count = mode == 25 ? 1 : 2;
      reply[0] = 0x12345678;
      reply[1] = 9;
    }
    if (mode == 22) {
      *rc = session->last_response = 0x2005;
      return -1;
    }
    if (mode == 23) {
      *rc = session->last_response = PTP_ERROR_IO;
      no_more = 1;
      return -1;
    }
    return 0;
  }
  if (op == PTP_OC_DeleteObject) {
    assert(flags == PTP_DP_NODATA && count == 2 && !params[1]);
    journal_before_write();
    Object *obj = lookup(params[0]);
    assert(obj && ((params[0] >= 100 && params[0] <= 104) || params[0] == 200));
    ++deletions;
    if (mode == 8) {
      *rc = session->last_response = PTP_ERROR_IO;
      no_more = 1;
      return -1;
    }
    uint32_t next = 200;
    for (int k = 4; k >= 0; --k)
      if (lookup(100 + k)) {
        next = 100 + k;
        break;
      }
    assert(params[0] == next || creates < 5);
    obj->live = 0;
    if ((mode == 17 || mode == 18) && params[0] == 104) {
      ref_count = 0;
      if (mode == 18) {
        objects[6].live = 0;
        objects[5].live = 0;
      }
    }
    return 0;
  }
  assert(!"unexpected PTP opcode");
  return -1;
}
static void setup(FILE *out) {
  memset(&state, 0, sizeof(state));
  memset(&dev, 0, sizeof(dev));
  memset(objects, 0, sizeof(objects));
  memset(refs, 0, sizeof(refs));
  state.fd = -1;
  state.dev = &dev;
  state.out = out;
  state.stop = &stop;
  state.storage = 0x10001;
  state.mp3 = audio;
  state.mp3_size = sizeof(audio);
  state.now_fn = mock_now;
  state.deadline = 100000;
  strcpy(state.name, "HDArtworkProbe-test");
  strcpy(state.serial, "PRIVATE-TEST-SERIAL");
  dev.serial = state.serial;
  dev.storage_id = state.storage;
  dev.device_family = ZUNE_FAMILY_PAVO;
  dev.mtpz_authenticated = 1;
  now = 1;
  stop = 0;
  checks += 0;
  transactions = mutations = deletions = commits = creates = link_writes = 0;
  mode = no_more = missing_root = duplicate_root = duplicate_artist =
      foreign_child = wrong_ref = changed_identity = 0;
  pending = -1;
  journal_size = 0;
  ref_count = 0;
  hd_first_configure(&state);
  for (unsigned i = 0; i < 3; ++i) {
    objects[i].object = state.roots[i];
    objects[i].object.handle = i + 1;
    objects[i].object.persistent[0] = (uint8_t)(i + 1);
    objects[i].live = objects[i].committed = 1;
  }
  strcpy(directory, "/tmp/hd-first-sync-test.XXXXXX");
  assert(mkdtemp(directory));
  snprintf(manifest, sizeof(manifest), "%s/recovery.bin", directory);
}
static void teardown(void) {
  if (state.fd >= 0)
    close(state.fd);
  free(state.baseline);
  unlink(manifest);
  rmdir(directory);
}
static void prepare(void) {
  check(!hd_first_prepare(&state, manifest) && state.prepared && !mutations,
        "read-only preflight, verified roots, unique identities, durable "
        "private journal");
}
static void complete(void) {
  for (unsigned i = 0; i < 6; ++i)
    check(!hd_first_tick(&state, 1), "serialized graph phase succeeds");
  check(creates == 5 && commits == 3 && link_writes == 1 && state.refs_set,
        "five objects, artist/album zero commits, track commit, exact album "
        "reference");
}
int main(void) {
  FILE *out = tmpfile();
  assert(out);
  check(hd_first_probe_name("HDArtworkProbe-BillieEilish-0123456789ab") &&
            hd_first_probe_name("HDArtworkProbe-0123456789abcdef01234567"),
        "controlled readable and original probe names accepted");
  check(!hd_first_probe_name("Billie Eilish") &&
            !hd_first_probe_name("HDArtworkProbe-Billie Eilish-0123456789ab") &&
            !hd_first_probe_name("HDArtworkProbe-BillieEilish-0123456789AB"),
        "plain real artist, spaces and malformed suffix rejected");
  for (int retained_music = 0; retained_music < 2; ++retained_music) {
    setup(out);
    if (retained_music)
      objects[0].live = 0;
    state.fixture_duration_ms = 30000;
    prepare();
    if (retained_music)
      check(!hd_first_tick(&state, 1), "retained test owned Music created");
    complete();
    state.retain_requested = 1;
    unsigned writes = mutations;
    check(!hd_first_complete(&state, 1, 0, 1, 1) && state.retained &&
              state.finished && !deletions && mutations == writes + 1,
          "retain requires proof, no deletion, and one journaled reindex "
          "handoff");
    close(state.fd);
    state.fd = -1;
    HdFirstSync retained;
    check(!hd_first_graph_recovery_load(&retained, manifest) &&
              retained.recovery_retained_graph,
          "durable retained manifest permits guarded later cleanup");
    free(state.baseline);
    state = retained;
    state.dev = &dev;
    dev.serial = state.serial;
    state.out = out;
    state.stop = &stop;
    state.now_fn = mock_now;
    state.deadline = 100000;
    state.mp3 = audio;
    mode = 17;
    check(!hd_first_graph_recovery_inspect(&state),
          "retained graph revalidated after later connection");
    char proof[200];
    snprintf(proof, sizeof(proof), "%s/retained-proof", directory);
    check(!hd_first_recovery_proof(&state, proof),
          "retained cleanup proof durable");
    journal_size = 0;
    check(!hd_first_graph_recovery_delete(&state) &&
              deletions == (unsigned)(retained_music ? 6 : 5),
          "retained test cleanup preserves reused Music or removes only owned "
          "Music");
    unlink(proof);
    teardown();
  }
  for (int gate = 0; gate < 8; ++gate) {
    setup(out);
    prepare();
    complete();
    state.retain_requested = gate != 0;
    if (gate == 4)
      state.phase = 5;
    if (gate == 6)
      mode = 20;
    if (gate == 7)
      mode = 21;
    int clean = gate != 2, uncertain = gate == 3, background = gate != 1,
        network_ok = gate != 5;
    int retain_result =
        hd_first_complete(&state, clean, uncertain, background, network_ok);
    check(!state.retained && (!state.retain_requested || retain_result),
          "no retain without opt-in/full graph/clean "
          "transport/background/overall success/durable final proof");
    teardown();
  }
  for (int response_mode = 0; response_mode < 3; ++response_mode) {
    setup(out);
    prepare();
    complete();
    state.retain_requested = 1;
    mode = response_mode ? 24 + response_mode : 0;
    int rc = hd_first_complete(&state, 1, 0, 1, 1);
    check(response_mode == 2 ? rc && state.uncertain : !rc && !state.uncertain,
          "CleanDataStore accepts observed zero or one parameter and rejects "
          "more");
    check(state.retained && !deletions,
          "response shape handling never retries or deletes retained graph");
    teardown();
  }
  for (int failure = 22; failure <= 24; ++failure) {
    setup(out);
    prepare();
    complete();
    state.retain_requested = 1;
    mode = failure;
    int rc;
    if (failure == 24) {
      check(!hd_first_retain(&state),
            "direct retain proof for cancellation handoff gate");
      stop = 1;
      rc = hd_first_retained_handoff(&state);
    } else
      rc = hd_first_complete(&state, 1, 0, 1, 1);
    check(rc && state.retained && !deletions,
          "handoff refusal uncertainty or cancellation preserves retained "
          "objects and returns failure");
    close(state.fd);
    state.fd = -1;
    HdFirstSync recover;
    check(!hd_first_graph_recovery_load(&recover, manifest) &&
              recover.recovery_retained_graph,
          "retained handoff failure preserves exact later cleanup proof");
    hd_first_release(&recover);
    teardown();
  }
  setup(out);
  state.fixture_duration_ms = 30000;
  prepare();
  mode = 19;
  int duration_rc = 0;
  for (unsigned i = 0; i < 6 && !duration_rc; ++i)
    duration_rc = hd_first_tick(&state, 1);
  check(duration_rc && state.uncertain,
        "declared fixture duration must read back exactly");
  teardown();
  for (int countmode = 9; countmode <= 11; ++countmode) {
    setup(out);
    objects[0].live = 0;
    prepare();
    mode = countmode;
    int rc = hd_first_tick(&state, 1);
    check(
        countmode == 11 ? rc && state.uncertain : !rc && state.music_owned,
        "3..5 create response parameters accepted; malformed success stopped");
    int fd = open(manifest, O_RDONLY);
    assert(fd >= 0);
    off_t offset =
        2 * sizeof(HdFirstRecord) + sizeof(size_t) + 4 * state.baseline_count;
    assert(lseek(fd, offset, SEEK_SET) == offset);
    HdFirstTransactionRecord raw;
    assert(!hd_first_read_all(fd, &raw, sizeof(raw)));
    close(fd);
    check(
        raw.count == (countmode == 9    ? 4
                      : countmode == 10 ? 5
                                        : 2) &&
            raw.parameters[3] == 123 && raw.parameters[4] == 456,
        "raw count and all five response slots durable before interpretation");
    teardown();
  }
  for (int recovery_case = 0; recovery_case < 9; ++recovery_case) {
    setup(out);
    objects[0].live = 0;
    prepare();
    check(!hd_first_tick(&state, 1), "recovery fixture creates only Music");
    /* Recreate the precise old manifest: no recorded outcome after intent. */
    off_t old_size =
        2 * sizeof(HdFirstRecord) + sizeof(size_t) + 4 * state.baseline_count;
    assert(!ftruncate(state.fd, old_size));
    close(state.fd);
    state.fd = -1;
    HdFirstSync recovered;
    check(!hd_first_recovery_load(&recovered, manifest),
          "exact original stopped-root manifest accepted");
    if (!recovery_case) {
      int edit = open(manifest, O_RDWR);
      assert(edit >= 0);
      HdFirstRecord original, bad;
      assert(pread(edit, &original, sizeof(original), 0) == sizeof(original));
      bad = original;
      memset(bad.roots[0].filename, 'X', sizeof(bad.roots[0].filename));
      assert(pwrite(edit, &bad, sizeof(bad), 0) == sizeof(bad));
      HdFirstSync rejected;
      check(hd_first_recovery_load(&rejected, manifest) != 0,
            "unterminated Music filename rejected within bounds");
      assert(pwrite(edit, &original, sizeof(original), 0) == sizeof(original));
      assert(!fchmod(edit, 0644));
      check(hd_first_recovery_load(&rejected, manifest) != 0,
            "public-mode recovery file rejected");
      assert(!fchmod(edit, 0600));
      assert(!ftruncate(edit, old_size - 1));
      check(hd_first_recovery_load(&rejected, manifest) != 0,
            "truncated journal rejected");
      assert(!ftruncate(edit, old_size));
      HdFirstRecord intent = original;
      snprintf(intent.stage, sizeof(intent.stage),
               "intent-create-owned-Music-root");
      assert(pwrite(edit, &intent, sizeof(intent), old_size - sizeof(intent)) ==
             sizeof(intent));
      close(edit);
      char link[200];
      snprintf(link, sizeof(link), "%s/link", directory);
      assert(!symlink(manifest, link));
      check(hd_first_recovery_load(&rejected, link) != 0,
            "symlink recovery source rejected");
      unlink(link);
    }
    free(state.baseline);
    state = recovered;
    state.dev = &dev;
    state.out = out;
    state.stop = &stop;
    state.now_fn = mock_now;
    state.deadline = 100000;
    dev.serial = state.serial;
    if (recovery_case == 1)
      dev.storage_id++;
    if (recovery_case == 2)
      objects[9] = objects[8], objects[9].object.handle = 201;
    if (recovery_case == 3)
      objects[1].live = 0;
    if (recovery_case == 4)
      objects[9] = objects[8], objects[9].object.handle = 201,
      objects[9].object.parent = 200;
    if (recovery_case == 5)
      objects[8].object.size = 1;
    if (recovery_case == 6)
      strcpy(objects[8].object.filename, "Foreign");
    int rc = hd_first_recovery_inspect(&state);
    check((recovery_case > 0 && recovery_case < 7) ? rc != 0 : rc == 0,
          "recovery requires same device and exact sole empty Music addition");
    check(mutations == 1 && !deletions, "inspection never writes device");
    if (!recovery_case || recovery_case >= 7) {
      char proof[200];
      snprintf(proof, sizeof(proof), "%s/proof", directory);
      check(!hd_first_recovery_proof(&state, proof),
            "recovery proof durable before deletion");
      journal_size = 0;
      if (recovery_case >= 7) {
        mode = recovery_case == 7 ? 12 : 13;
        check(hd_first_recovery_delete(&state) && !deletions && lookup(200),
              "stop or deadline during final recovery emptiness read prevents "
              "deletion");
      } else {
        check(!hd_first_recovery_delete(&state) && deletions == 1 &&
                  !lookup(200),
              "exact recovered empty root removed and original baseline "
              "restored");
      }
      unlink(proof);
    }
    teardown();
  }
  for (int complete_mode = 17; complete_mode <= 18; ++complete_mode) {
    setup(out);
    objects[0].live = 0;
    prepare();
    check(!hd_first_tick(&state, 1), "complete recovery Music created");
    complete();
    mode = complete_mode;
    check(
        hd_first_finish(&state, 1, 0) && state.uncertain && ref_count == 1 &&
            !deletions,
        "successful clear with firmware-restored membership refuses automatic "
        "cleanup");
    close(state.fd);
    state.fd = -1;
    HdFirstSync complete_recovery;
    check(!hd_first_graph_recovery_load(&complete_recovery, manifest) &&
              complete_recovery.recovery_complete_graph,
          "exact completed graph and failed clear history accepted");
    free(state.baseline);
    state = complete_recovery;
    state.dev = &dev;
    dev.serial = state.serial;
    state.out = out;
    state.stop = &stop;
    state.now_fn = mock_now;
    state.deadline = 100000;
    state.mp3 = audio;
    check(!hd_first_graph_recovery_inspect(&state) && state.refs_set == 1,
          "complete six graph with exact sole own-track membership proven");
    char complete_proof[200];
    snprintf(complete_proof, sizeof(complete_proof), "%s/full-proof",
             directory);
    check(!hd_first_recovery_proof(&state, complete_proof),
          "complete graph proof durable");
    journal_size = 0;
    unsigned before_handoff = mutations;
    ref_count = 0;
    check(hd_first_graph_recovery_retain(&state) && !state.retained &&
              mutations == before_handoff,
          "recovery retention requires actual sole track album membership");
    ref_count = 1;
    check(!hd_first_graph_recovery_retain(&state) && state.retained &&
              !deletions && mutations == before_handoff + 1,
          "explicit recovery retain preserves existing graph and only requests "
          "one reindex");
    check(!hd_first_graph_recovery_delete(&state) &&
              deletions == (complete_mode == 17 ? 6u : 4u) && link_writes == 2,
          "complete recovery deletes track first without repeating failed "
          "reference clearing and restores baseline");
    unlink(complete_proof);
    teardown();
  }
  for (int graph_case = 0; graph_case < 3; ++graph_case) {
    setup(out);
    objects[0].live = 0;
    objects[11].live = objects[11].committed = 1;
    objects[11].object.handle = 10;
    objects[11].object.format = 0xba05;
    prepare();
    check(!hd_first_tick(&state, 1), "graph fixture Music created");
    mode = 15;
    int rc = 0;
    for (unsigned i = 0; i < 5 && !rc; ++i)
      rc = hd_first_tick(&state, 1);
    check(rc && state.uncertain && state.objects[4].handle == 104,
          "exact failed-track terminal journal produced");
    close(state.fd);
    state.fd = -1;
    HdFirstSync recovered;
    check(!hd_first_graph_recovery_load(&recovered, manifest),
          "strict six-create journal replay accepts observed failure only");
    free(state.baseline);
    state = recovered;
    state.dev = &dev;
    state.out = out;
    state.stop = &stop;
    state.now_fn = mock_now;
    state.deadline = 100000;
    state.mp3 = audio;
    dev.serial = state.serial;
    mode = 0;
    objects[6].live = 0;
    objects[9].live = objects[10].live = 1;
    objects[9].object = (HdFirstObject){.handle = 300,
                                        .storage = state.storage,
                                        .format = MTP_OFC_AbstractAudioAlbum,
                                        .artist = 301};
    strcpy(objects[9].object.name, "Unknown Album");
    objects[9].object.persistent[0] = 30;
    objects[10].object = (HdFirstObject){.handle = 301,
                                         .storage = state.storage,
                                         .format = MTP_OFC_AbstractArtist};
    strcpy(objects[10].object.name, "Unknown Artist");
    strcpy(objects[10].object.filename, "Unknown Artist.art");
    objects[10].object.persistent[0] = 31;
    if (graph_case == 1)
      mode = 16;
    if (graph_case == 2)
      objects[12] = objects[10], objects[12].object.handle = 302;
    rc = hd_first_graph_recovery_inspect(&state);
    check(graph_case ? rc != 0 : rc == 0,
          "closed-graph inspection refuses foreign playlist links and "
          "unexplained additions");
    if (!graph_case) {
      char proof[200];
      snprintf(proof, sizeof(proof), "%s/graph-proof", directory);
      check(!hd_first_recovery_proof(&state, proof) &&
                !hd_first_graph_extra_record(&state),
            "observed graph and placeholders durably recorded");
      journal_size = 0;
      check(hd_first_graph_recovery_delete(&state) == 1 && deletions == 5 &&
                !lookup(104) && lookup(300) && lookup(301),
            "only five known created objects deleted; generated placeholders "
            "truthfully retained");
      unlink(proof);
    }
    teardown();
  }
  setup(out);
  prepare();
  mode = 14;
  complete();
  check(!strncmp(state.objects[4].filename, "01 ", 3) &&
            !hd_first_finish(&state, 1, 0),
        "observed Track1 filename normalization retained only with exact "
        "identity and payload");
  teardown();
  setup(out);
  prepare();
  check(!hd_first_tick(&state, 0) && !mutations, "not-ready does not mutate");
  complete();
  unsigned before = mutations;
  check(!hd_first_tick(&state, 1) && mutations == before,
        "ready reentry does not recreate");
  stop = 1;
  check(!hd_first_finish(&state, 1, 0) && deletions == 5 && ref_count == 0,
        "signal after creation still cleans exact graph and restores baseline");
  check(hd_first_finish(&state, 1, 0) && deletions == 5,
        "cleanup never retries");
  teardown();
  setup(out);
  objects[2].live = 0;
  check(hd_first_prepare(&state, manifest) && !mutations && state.fd < 0,
        "missing shared root refuses without creating anything");
  teardown();
  setup(out);
  objects[8] = objects[1];
  objects[8].object.handle = 88;
  check(hd_first_prepare(&state, manifest) && !mutations,
        "ambiguous shared root refused");
  teardown();
  setup(out);
  objects[8].object = state.objects[HD_FIRST_ARTIST];
  objects[8].object.handle = 88;
  objects[8].live = 1;
  check(hd_first_prepare(&state, manifest) && !mutations,
        "exact existing artist refuses instead of reuse");
  teardown();
  setup(out);
  prepare();
  now = state.deadline;
  check(hd_first_tick(&state, 1) && !mutations,
        "expired observation never creates");
  teardown();
  setup(out);
  prepare();
  stop = 1;
  check(hd_first_tick(&state, 1) && !mutations,
        "signal before readiness never creates");
  teardown();
  for (int failure = 1; failure <= 7; ++failure) {
    setup(out);
    prepare();
    mode = failure;
    int rc = 0;
    for (unsigned i = 0; i < 6 && !rc; ++i)
      rc = hd_first_tick(&state, 1);
    check(rc != 0, "injected creation/commit/payload failure stops phases");
    before = transactions;
    check(hd_first_finish(&state, 1, state.uncertain) != 0 || failure == 2,
          "unknown outcome retains recovery instead of blind cleanup");
    check(transactions == before && !deletions,
          "unknown or refused first create sends no followup destructive "
          "operation");
    struct stat st;
    check(!stat(manifest, &st) && st.st_size > 0,
          "recovery manifest survives failure");
    teardown();
  }
  setup(out);
  prepare();
  complete();
  changed_identity = 1;
  before = mutations;
  check(hd_first_finish(&state, 1, 0) && !deletions && mutations == before,
        "changed persistent identity blocks all cleanup writes");
  teardown();
  setup(out);
  prepare();
  complete();
  refs[1] = 999;
  ref_count = 2;
  before = mutations;
  check(hd_first_finish(&state, 1, 0) && !deletions && mutations == before,
        "foreign album reference blocks cleanup without changing it");
  teardown();
  setup(out);
  prepare();
  complete();
  foreign_child = 1;
  check(hd_first_finish(&state, 1, 0) && deletions == 3,
        "nonempty test folder is not recursively deleted");
  teardown();
  setup(out);
  prepare();
  complete();
  mode = 8;
  check(hd_first_finish(&state, 1, 0) && state.uncertain && deletions == 1,
        "uncertain delete stops immediately without retry or other deletes");
  teardown();
  setup(out);
  prepare();
  complete();
  before = transactions;
  check(hd_first_finish(&state, 0, 1) && transactions == before && !deletions,
        "wounded network skips all cleanup PTP");
  teardown();
  setup(out);
  objects[0].live = 0;
  prepare();
  check(state.create_music && !state.music_owned,
        "missing Music is a planned owned root, never a shared-root reuse");
  check(!hd_first_tick(&state, 1) && state.music_owned &&
            state.roots[0].proven && creates == 0 && commits == 0,
        "owned Music created once without SendObject, exact identity proven");
  complete();
  check(!hd_first_finish(&state, 1, 0) && deletions == 6,
        "owned Music removed only after full graph and verified empty, "
        "baseline restored");
  teardown();
  setup(out);
  objects[0].live = 0;
  prepare();
  objects[8] = objects[0];
  objects[8].live = 1;
  objects[8].object.handle = 88;
  check(hd_first_tick(&state, 1) && !mutations && !state.music_owned,
        "Music appearing after preflight refuses creation and adoption");
  teardown();
  for (int failure = 1; failure <= 5; ++failure) {
    if (failure == 4)
      continue;
    setup(out);
    objects[0].live = 0;
    prepare();
    mode = failure;
    check(hd_first_tick(&state, 1) != 0,
          "owned-root lost response/refusal/collision/journal failure stops");
    before = transactions;
    check(hd_first_finish(&state, 1, state.uncertain) != 0 || failure == 2,
          "owned-root unknown outcome retains recovery");
    check(transactions == before && !deletions,
          "unknown owned root never blindly deleted");
    teardown();
  }
  setup(out);
  objects[0].live = 0;
  prepare();
  check(!hd_first_tick(&state, 1), "owned root creation succeeds");
  complete();
  foreign_child = 2;
  check(
      hd_first_finish(&state, 1, 0) && deletions == 5 && lookup(200),
      "owned Music with foreign child is preserved without recursive deletion");
  teardown();
  fflush(out);
  rewind(out);
  char buffer[65536];
  size_t n = fread(buffer, 1, sizeof(buffer) - 1, out);
  buffer[n] = 0;
  check(!strstr(buffer, "PRIVATE-TEST-SERIAL"),
        "public report excludes private serial");
  fclose(out);
  printf("HD first sync: %u checks passed; mocked PTP only\n", checks);
  return 0;
}
