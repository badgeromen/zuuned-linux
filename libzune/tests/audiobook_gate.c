/* Safety checks for the experimental CLI. Linker wrappers prevent USB access.
 */
#include "../tools/audiobook_test.h"
#include "zune_internal.h"
#include <signal.h>
#include <strings.h>

static uint8_t response[32];
static uint32_t response_size;
static int response_error, inventory_error, inventory_count, checks;
static int inventory_calls;
static uint32_t inventory_formats[8];
ZuneDeviceHandle __wrap_zune_breach(void) {
  fputs("Unexpected USB attempt\n", stderr);
  abort();
}
int __wrap_mtp_get_object_prop_value(ptp_session_t *s, uint32_t id,
                                     uint16_t prop, uint8_t **data,
                                     uint32_t *size) {
  (void)s;
  (void)id;
  (void)prop;
  *size = response_size;
  *data = malloc(response_size ? response_size : 1);
  memcpy(*data, response, response_size);
  return response_error;
}
int __wrap_mtp_get_object_handles(ptp_session_t *s, uint32_t storage,
                                  uint32_t format, uint32_t parent,
                                  uint32_t **ids, int *count) {
  (void)s;
  (void)storage;
  if (inventory_calls < 8) inventory_formats[inventory_calls] = format;
  ++inventory_calls;
  (void)parent;
  *ids = NULL;
  *count = inventory_count;
  return inventory_error;
}
static void check(int truth, const char *label) {
  if (!truth) {
    fprintf(stderr, "FAIL %s\n", label);
    exit(1);
  }
  ++checks;
}
int main(void) {
  ZuneDevice dev = {0};
  dev.device_family = ZUNE_FAMILY_KEEL;
  mtp_prop_entry_t p = {.prop_code = MTP_OPC_MetaGenre,
                        .data_type = PTP_DTC_UINT16,
                        .value.u16 = 18};
  response_size = 2;
  response[0] = 18;
  check(book_test_verify(&dev, 0x12000001, &p), "exact classification");
  response[0] = 0;
  check(book_test_verify(&dev, 0x12000001, &p),
        "Keel native book normalization");
  check(!book_test_verify(&dev, 0x10000001, &p), "podcast is not audiobook");
  dev.device_family = ZUNE_FAMILY_PAVO;
  check(book_test_verify(&dev, 0x12000001, &p), "HD native book normalization");
  dev.device_family = ZUNE_FAMILY_SCORPIUS;
  check(!book_test_verify(&dev, 0x12000001, &p),
        "normalization not generalized across models");
  dev.device_family = ZUNE_FAMILY_KEEL;
  response[0] = 64;
  check(!book_test_verify(&dev, 0x12000001, &p),
        "wrong nonzero category refused");
  response_size = 4;
  response[0] = 18;
  check(!book_test_verify(&dev, 0x12000001, &p), "wrong integer width refused");
  response_error = -1;
  response_size = 2;
  check(!book_test_verify(&dev, 0x12000001, &p), "transport failure refused");
  response_error = 0;
  p.prop_code = MTP_OPC_Name;
  p.data_type = PTP_DTC_STR;
  p.value.str = "A";
  response_size = 5;
  response[0] = 2;
  response[1] = 'A';
  response[2] = response[3] = response[4] = 0;
  check(book_test_verify(&dev, 0x12000001, &p), "exact title");
  response_size = 3;
  check(!book_test_verify(&dev, 0x12000001, &p), "truncated string refused");
  response_size = 5;
  response[4] = 1;
  check(!book_test_verify(&dev, 0x12000001, &p), "unterminated string refused");
  uint32_t *ids = NULL;
  int n = 0;
  check(!book_test_inventory(&dev, 0, &ids, &n), "empty inventory succeeds");
  inventory_count = 1;
  check(book_test_inventory(&dev, 0, &ids, &n) < 0,
        "missing inventory storage refused");
  inventory_count = -1;
  check(book_test_inventory(&dev, 0, &ids, &n) < 0,
        "negative inventory refused");
  inventory_count = 0;
  inventory_error = -1;
  check(book_test_inventory(&dev, 0, &ids, &n) < 0,
        "failed inventory not treated as empty");
  char path[] = "/tmp/libzune-book-gate-XXXXXX";
  int fd = mkstemp(path);
  check(fd >= 0, "temporary input");
  check(write(fd, "12345", 5) == 5, "test input written");
  close(fd);
  uint32_t size = 0;
  uint8_t *bytes = book_test_load(path, 4, &size);
  check(!bytes && !size, "oversize input refused");
  bytes = book_test_load(path, 5, &size);
  check(bytes && size == 5 && !memcmp(bytes, "12345", 5),
        "bounded immutable input copy");
  free(bytes);
  unlink(path);
  char *args[] = {"zunetool", "audiobook-test", path, path, path, NULL};
  check(cmd_audiobook_test(5, args) == 2, "invalid files never connect");
  check(cmd_audiobook_test(2, args) == 2, "invalid arguments never connect");
  ZuneAudiobookPartInput part = {
      .book_key =
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      .book_title = "Book",
      .author = "",
      .title = "Part",
      .part_number = 1,
      .duration_ms = 45000};
  uint32_t folder = 99, id = 99;
  check(zune_find_audiobook_part(&dev, &part, &folder, &id) < 0 && !folder &&
            !id,
        "public API requires authentication");
  dev.mtpz_authenticated = 1;
  inventory_error = 0;
  inventory_count = 0;
  inventory_calls = 0;
  check(!zune_find_audiobook_part(&dev, &part, &folder, &id) && !folder && !id,
        "empty live book lookup succeeds without writes");
  check(inventory_calls == 2 && inventory_formats[0] == MTP_OFC_Association &&
            inventory_formats[1] == 0,
        "book lookup uses wildcard inventory because HD MP3 listing omits books");
  inventory_error = -1;
  check(zune_smuggle_audiobook_part(&dev, "/nonexistent", &part, &id) < 0 &&
            !id,
        "send cannot turn failed inventory into creation");
  inventory_error = 0;
  part.book_key = "bad";
  check(zune_find_audiobook_part(&dev, &part, &folder, &id) < 0,
        "malformed book identity refused");
  part.book_key =
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  dev.device_family = ZUNE_FAMILY_SCORPIUS;
  check(zune_find_audiobook_part(&dev, &part, &folder, &id) < 0,
        "untested model gate stays explicit");
  printf("PASS %d audiobook gate checks, no USB\n", checks);
  return 0;
}
