#include "../tools/hd_image_server.h"
#include <libxml/parser.h>
#include <stdlib.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    tests++;                                                                   \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                          \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static unsigned tests;
static uint8_t output[128][1100];
static size_t sizes[128], count;
static int emit(void *o, const uint8_t *p, size_t n) {
  (void)o;
  CHECK(count < 128);
  CHECK(n <= 1100);
  memcpy(output[count], p, n);
  sizes[count++] = n;
  return 0;
}
static uint8_t jpeg[2504];
static const char *guid = "01234567-89ab-cdef-0123-456789abcdef";
static void init(HdImageServer *s) {
  memset(jpeg, 0x45, sizeof(jpeg));
  jpeg[0] = 255;
  jpeg[1] = 216;
  jpeg[2502] = 255;
  jpeg[2503] = 217;
  count = 0;
  CHECK(!hd_image_server_init(s, 0xc0a83764, guid, jpeg, sizeof(jpeg), emit,
                              NULL));
}
static size_t packet(HdImageServer *s, uint8_t *p, uint16_t port, uint32_t seq,
                     uint32_t ack, uint8_t flags, const void *data, size_t n) {
  memset(p, 0, 1500);
  p[0] = 0x45;
  p[8] = 64;
  p[9] = 6;
  hdis_w16(p + 2, (uint16_t)(40 + n));
  hdis_w32(p + 12, 0xc0a83765);
  hdis_w32(p + 16, s->server_ip);
  hdis_w16(p + 10, hdis_checksum(p, 20));
  hdis_w16(p + 20, 30000);
  hdis_w16(p + 22, port);
  hdis_w32(p + 24, seq);
  hdis_w32(p + 28, ack);
  p[32] = 0x50;
  p[33] = flags;
  hdis_w16(p + 34, 4000);
  if (n)
    memcpy(p + 40, data, n);
  hdis_w16(p + 36, hdis_transport_sum(p, p + 20, n + 20, 6));
  return n + 40;
}
static void input(HdImageServer *s, uint16_t port, uint32_t seq, uint32_t ack,
                  uint8_t flags, const void *data, size_t n) {
  uint8_t p[1500];
  size_t z = packet(s, p, port, seq, ack, flags, data, n);
  CHECK(!hd_image_server_packet(s, p, z));
}
static void handshake(HdImageServer *s, uint16_t port) {
  input(s, port, 100, 0, 2, NULL, 0);
  CHECK(count == 1);
  CHECK(output[0][33] == 0x12);
  CHECK(!hdis_checksum(output[0], 20));
  CHECK(!hdis_transport_sum(output[0], output[0] + 20, sizes[0] - 20, 6));
  input(s, port, 101, s->flow[0].send_next, 0x10, NULL, 0);
  CHECK(s->flow[0].established);
}
static void image_test(void) {
  HdImageServer s;
  char req[256];
  uint8_t received[4000];
  size_t rn = 0, seen = 0, i;
  init(&s);
  handshake(&s, 80);
  snprintf(req, sizeof(req),
           "GET /v3.0/en-US/music/artist/%s/deviceBackgroundImage?width=480 "
           "HTTP/1.1\r\nHost: catalog.zune.net\r\n\r\n",
           guid);
  input(&s, 80, 101, s.flow[0].send_next, 0x18, req, 20);
  CHECK(!s.requests);
  input(&s, 80, 121, s.flow[0].send_next, 0x18, req + 20, strlen(req) - 20);
  CHECK(s.requests == 1);
  CHECK(s.flow[0].image);
  CHECK(s.flow[0].pending);
  while (s.flow[0].active) {
    for (i = seen; i < count; i++) {
      CHECK(!hdis_checksum(output[i], 20));
      CHECK(!hdis_transport_sum(output[i], output[i] + 20, sizes[i] - 20, 6));
      if (sizes[i] > 40) {
        CHECK(rn + sizes[i] - 40 < sizeof(received));
        memcpy(received + rn, output[i] + 40, sizes[i] - 40);
        rn += sizes[i] - 40;
      }
    }
    seen = count;
    input(&s, 80, s.flow[0].receive_next, s.flow[0].send_next, 0x10, NULL, 0);
  }
  CHECK(s.image_completed == 1);
  CHECK(rn == s.flow[0].header_n + sizeof(jpeg));
  CHECK(!memcmp(received + s.flow[0].header_n, jpeg, sizeof(jpeg)));
  CHECK(!memcmp(received, "HTTP/1.1 200 OK", 15));
}
static void bad_and_retransmit(void) {
  HdImageServer s;
  uint8_t p[1500], saved[1100];
  size_t n;
  init(&s);
  n = packet(&s, p, 80, 100, 0, 2, NULL, 0);
  p[10] ^= 1;
  CHECK(!hd_image_server_packet(&s, p, n));
  CHECK(!count);
  p[10] ^= 1;
  p[36] ^= 1;
  CHECK(!hd_image_server_packet(&s, p, n));
  CHECK(!count);
  p[36] ^= 1;
  CHECK(!hd_image_server_packet(&s, p, n));
  CHECK(count == 1);
  memcpy(saved, output[0], sizes[0]);
  input(&s, 80, 101, s.flow[0].send_next + 1, 0x10, NULL, 0);
  CHECK(!s.flow[0].established);
  CHECK(count == 1);
  CHECK(!hd_image_server_tick(&s, 999));
  CHECK(count == 1);
  CHECK(!hd_image_server_tick(&s, 1000));
  CHECK(count == 2);
  CHECK(!memcmp(saved, output[1], sizes[1]));
  for (unsigned i = 2; i <= 6; i++)
    CHECK(!hd_image_server_tick(&s, i * 1000));
  CHECK(s.timed_out == 1);
  CHECK(!s.flow[0].active);
}
static size_t dns_query(uint8_t *p, const char *name) {
  size_t n = 12;
  const char *at = name;
  memset(p, 0, 512);
  hdis_w16(p, 0x1234);
  hdis_w16(p + 2, 0x100);
  hdis_w16(p + 4, 1);
  while (*at) {
    const char *end = strchr(at, '.');
    size_t len = end ? (size_t)(end - at) : strlen(at);
    p[n++] = (uint8_t)len;
    memcpy(p + n, at, len);
    n += len;
    at = end ? end + 1 : at + len;
  }
  p[n++] = 0;
  hdis_w16(p + n, 1);
  hdis_w16(p + n + 2, 1);
  return n + 4;
}
static void dns_test(void) {
  HdImageServer s;
  uint8_t q[512], r[600], framed[600];
  size_t n, z;
  init(&s);
  n = dns_query(q, "catalog.zune.net");
  z = hdis_dns(&s, q, n, r, sizeof(r));
  CHECK(z == n + 16);
  CHECK(hdis_u16(r + 6) == 1);
  CHECK(hdis_u32(r + z - 4) == s.server_ip);
  n = dns_query(q, "stats.zune.net");
  z = hdis_dns(&s, q, n, r, sizeof(r));
  CHECK(z == n);
  CHECK((r[3] & 15) == 3);
  q[12] = 0xc0;
  CHECK(!hdis_dns(&s, q, n, r, sizeof(r)));
  handshake(&s, 49949);
  n = dns_query(framed + 8, "catalog.zune.net");
  hdis_w16(framed, 49333);
  hdis_w16(framed + 2, 53);
  hdis_w16(framed + 4, (uint16_t)(n + 8));
  hdis_w16(framed + 6, 0);
  input(&s, 49949, 101, s.flow[0].send_next, 0x18, framed, n + 8);
  CHECK(s.flow[0].response == 2);
  CHECK(hdis_u16(s.flow[0].header) == 53);
  CHECK(hdis_u16(s.flow[0].header + 2) == 49333);
  input(&s, 49949, s.flow[0].receive_next, s.flow[0].send_next, 0x18, framed,
        n + 8);
  CHECK(s.dns_queries == 4);
  CHECK(s.flow[0].active);
  CHECK(s.flow[0].response == 2);
}
static void route_test(void) {
  HdImageServer s;
  const char *bad = "GET /other HTTP/1.1\r\n\r\n";
  init(&s);
  handshake(&s, 80);
  input(&s, 80, 101, s.flow[0].send_next, 0x18, bad, strlen(bad));
  CHECK(!s.flow[0].image);
  CHECK(!s.flow[0].body_n);
  CHECK(!memcmp(s.flow[0].header, "HTTP/1.1 404", 12));
}
static int failed_emit(void *opaque, const uint8_t *p, size_t n) {
  (void)opaque;
  (void)p;
  (void)n;
  return -1;
}
static void rejection_test(void) {
  HdImageServer s;
  uint8_t p[1500], data[1400], q[512], reply[600];
  size_t n, before;
  init(&s);
  CHECK(hd_image_server_init(&s, 1, "not-a-guid", jpeg, sizeof(jpeg), emit,
                             NULL) == -1);
  CHECK(hd_image_server_init(&s, 1, guid, jpeg, HDIS_JPEG_MAX + 1, emit,
                             NULL) == -1);
  handshake(&s, 80);
  before = count;
  input(&s, 80, 102, s.flow[0].send_next, 0x18, "GET ", 4);
  CHECK(s.flow[0].request_n == 0);
  CHECK(count ==
        before + 1); /* ACK expected sequence without accepting future data. */
  input(&s, 80, 101, s.flow[0].send_next, 0x18, "GET ", 4);
  CHECK(s.flow[0].request_n == 4);
  input(&s, 80, 101, s.flow[0].send_next, 0x18, "GET ", 4);
  CHECK(s.flow[0].request_n == 4); /* Retransmit not appended twice. */
  memset(data, 'x', sizeof(data));
  input(&s, 80, s.flow[0].receive_next, s.flow[0].send_next, 0x18, data, 1400);
  input(&s, 80, s.flow[0].receive_next, s.flow[0].send_next, 0x18, data, 1400);
  input(&s, 80, s.flow[0].receive_next, s.flow[0].send_next, 0x18, data, 1400);
  CHECK(!s.flow[0].active);
  CHECK(s.rejected == 1);
  init(&s);
  n = dns_query(q, "catalog.zune.net");
  CHECK(!hdis_dns(&s, q, n - 1, reply, sizeof(reply)));
  CHECK(!hdis_dns(&s, q, n, reply, 10));
  q[5] = 2;
  CHECK(!hdis_dns(&s, q, n, reply, sizeof(reply)));
  q[5] = 1;
  q[n - 1] = 2;
  CHECK(!hdis_dns(&s, q, n, reply, sizeof(reply)));
  n = packet(&s, p, 80, 100, 0, 2, NULL, 0);
  s.emit = failed_emit;
  CHECK(hd_image_server_packet(&s, p, n) == -1);
  CHECK(hd_image_server_tick(&s, 1000) == -1);
  CHECK(hd_image_server_tick(&s, 999) == -1);
}
static void mss_and_fragment_test(void) {
  HdImageServer s;
  uint8_t p[1500];
  size_t n;
  init(&s);
  n = packet(&s, p, 80, 100, 0, 2, NULL, 0);
  hdis_w16(p + 6, 0x2000);
  hdis_w16(p + 10, 0);
  hdis_w16(p + 10, hdis_checksum(p, 20));
  CHECK(!hd_image_server_packet(&s, p, n));
  CHECK(!count);
  n = packet(&s, p, 80, 100, 0, 2, NULL, 0);
  n += 4;
  p[32] = 0x60;
  p[40] = 2;
  p[41] = 4;
  hdis_w16(p + 42, 256);
  hdis_w16(p + 2, (uint16_t)n);
  hdis_w16(p + 10, 0);
  hdis_w16(p + 10, hdis_checksum(p, 20));
  hdis_w16(p + 36, 0);
  hdis_w16(p + 36, hdis_transport_sum(p, p + 20, n - 20, 6));
  CHECK(!hd_image_server_packet(&s, p, n));
  CHECK(s.flow[0].mss == 256);
  input(&s, 80, 101, s.flow[0].send_next, 0x10, NULL, 0);
  {
    char req[256];
    snprintf(req, sizeof(req),
             "GET /v3.0/en-US/music/artist/%s/deviceBackgroundImage "
             "HTTP/1.1\r\n\r\n",
             guid);
    input(&s, 80, 101, s.flow[0].send_next, 0x18, req, strlen(req));
  }
  CHECK(sizes[count - 1] == 296);
}
static void udp_test(void) {
  HdImageServer s;
  uint8_t p[600];
  size_t n;
  init(&s);
  memset(p, 0, sizeof(p));
  n = dns_query(p + 28, "catalog.zune.net") + 28;
  p[0] = 0x45;
  p[8] = 64;
  p[9] = 17;
  hdis_w16(p + 2, (uint16_t)n);
  hdis_w32(p + 12, 0xc0a83765);
  hdis_w32(p + 16, s.server_ip);
  hdis_w16(p + 10, hdis_checksum(p, 20));
  hdis_w16(p + 20, 30000);
  hdis_w16(p + 22, 53);
  hdis_w16(p + 24, (uint16_t)(n - 20));
  hdis_w16(p + 26, hdis_transport_sum(p, p + 20, n - 20, 17));
  CHECK(!hd_image_server_packet(&s, p, n));
  CHECK(count == 1);
  CHECK(output[0][9] == 17);
  CHECK(!hdis_checksum(output[0], 20));
  CHECK(!hdis_transport_sum(output[0], output[0] + 20, sizes[0] - 20, 17));
  p[26] ^= 1;
  CHECK(!hd_image_server_packet(&s, p, n));
  CHECK(count == 1);
}
static void fwlink_diagnostic_test(void) {
  HdImageServer s;
  uint8_t q[512], r[600];
  size_t n, z;
  init(&s);
  n = dns_query(q, "go.microsoft.com");
  z = hdis_dns(&s, q, n, r, sizeof(r));
  CHECK(z == n + 16);
  CHECK(hdis_u16(r + 6) == 1);
  CHECK(hdis_u32(r + z - 4) == s.server_ip);
  const char *paths[] = {"/fwlink",
                         "/fwlink/",
                         "/fwlink?LinkID=123",
                         "/fwlink/?LinkId=25817",
                         "/fwlinkevil",
                         "/fwlink/evil"};
  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    char req[256];
    init(&s);
    handshake(&s, 49949);
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\nHost: go.microsoft.com\r\n\r\n", paths[i]);
    input(&s, 49949, 101, s.flow[0].send_next, 0x18, req, strlen(req));
    CHECK(!s.flow[0].image);
    if (i < 4) {
      CHECK(!memcmp(s.flow[0].header, "HTTP/1.1 200", 12));
      CHECK(s.flow[0].body_n == 28);
      CHECK(s.fwlink_diagnostic_responses == 1);
      CHECK(!memcmp(s.flow[0].body, "<html><body>OK</body></html>", 28));
    } else {
      CHECK(!memcmp(s.flow[0].header, "HTTP/1.1 404", 12));
      CHECK(!s.flow[0].body_n);
      CHECK(!s.fwlink_diagnostic_responses);
    }
    while (s.flow[0].active)
      input(&s, 49949, s.flow[0].receive_next, s.flow[0].send_next, 0x10, NULL,
            0);
    CHECK(!s.image_completed);
  }
}
static xmlNode *element(xmlNode *parent, const char *local, const char *ns) {
  for (xmlNode *n = parent->children; n; n = n->next)
    if (n->type == XML_ELEMENT_NODE &&
        !xmlStrcmp(n->name, (const xmlChar *)local) && n->ns &&
        !xmlStrcmp(n->ns->href, (const xmlChar *)ns))
      return n;
  return NULL;
}
static size_t collect_response(HdImageServer *s, const char *target,
                               const char *host, uint8_t response[8192]) {
  char request[1024];
  size_t seen = 0, total = 0;
  handshake(s, 80);
  if (host)
    snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n",
             target, host);
  else
    snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\n\r\n", target);
  input(s, 80, 101, s->flow[0].send_next, 0x18, request, strlen(request));
  while (s->flow[0].active) {
    for (size_t i = seen; i < count; i++)
      if (sizes[i] > 40) {
        size_t n = sizes[i] - 40;
        CHECK(total + n < 8192);
        CHECK(!hdis_transport_sum(output[i], output[i] + 20, sizes[i] - 20, 6));
        memcpy(response + total, output[i] + 40, n);
        total += n;
      }
    seen = count;
    input(s, 80, s->flow[0].receive_next, s->flow[0].send_next, 0x10, NULL, 0);
  }
  response[total] = 0;
  CHECK(total > 0);
  char *body = strstr((char *)response, "\r\n\r\n"),
       *length = strstr((char *)response, "Content-Length: ");
  CHECK(body && length);
  CHECK(strtoul(length + 16, NULL, 10) ==
        total - (size_t)(body + 4 - (char *)response));
  return total;
}
static void catalog_test(void) {
  const char *atom = "http://www.w3.org/2005/Atom",
             *catalog = "http://schemas.zune.net/catalog/music/2007/10";
  const char *name = "Björk & <Orson> \"Test\" 'Band'";
  HdImageServer s;
  uint8_t response[8192];
  char target[256], image_guid[37];
  for (unsigned kind = 0; kind < 3; kind++) {
    init(&s);
    CHECK(!hd_image_server_set_artist(&s, name));
    memcpy(image_guid, s.image_guid, 37);
    CHECK(strcmp(s.guid, s.image_guid));
    const char *suffix = kind == 0 ? "" : kind == 1 ? "/images" : "/biography";
    snprintf(target, sizeof(target),
             "http://catalog.zune.net/v3.0/en-US/music/artist/%s%s?client=Zune",
             guid, suffix);
    size_t n = collect_response(&s, target, "Catalog.Zune.Net:80", response);
    CHECK(!memcmp(response, "HTTP/1.1 200", 12));
    CHECK(strstr((char *)response,
                 "Content-Type: application/atom+xml; charset=utf-8\r\n"));
    CHECK(!s.image_completed);
    CHECK(!s.background_completed);
    uint8_t *body = (uint8_t *)strstr((char *)response, "\r\n\r\n") + 4;
    size_t body_n = n - (size_t)(body - response);
    xmlDoc *doc = xmlReadMemory((const char *)body, (int)body_n, "local.xml",
                                NULL, XML_PARSE_NONET);
    CHECK(doc);
    xmlNode *root = xmlDocGetRootElement(doc);
    CHECK(root && root->ns);
    CHECK(!xmlStrcmp(root->ns->href, (const xmlChar *)atom));
    xmlNode *title = element(root, "title", atom);
    CHECK(title);
    xmlChar *text = xmlNodeGetContent(title);
    CHECK(!strcmp((const char *)text, name));
    xmlFree(text);
    if (kind == 0) {
      xmlNode *img = element(root, "image", catalog);
      CHECK(img);
      xmlNode *id = element(img, "id", catalog);
      CHECK(id);
      text = xmlNodeGetContent(id);
      CHECK(!strcmp((const char *)text + 9, s.image_guid));
      xmlFree(text);
    }
    if (kind == 1) {
      xmlNode *entry = element(root, "entry", atom);
      CHECK(entry);
      xmlNode *instances = element(entry, "instances", catalog);
      CHECK(instances);
      xmlNode *instance = element(instances, "imageInstance", catalog);
      CHECK(instance);
      xmlNode *url = element(instance, "url", catalog);
      CHECK(url);
      text = xmlNodeGetContent(url);
      char expected[160];
      snprintf(expected, sizeof(expected),
               "http://art.zune.net/1/%s/504/image.jpg", s.image_guid);
      CHECK(!strcmp((const char *)text, expected));
      xmlFree(text);
    }
    if (kind == 2) {
      xmlNode *content = element(root, "content", atom);
      CHECK(content);
      text = xmlNodeGetContent(content);
      CHECK(text && !*text);
      xmlFree(text);
    }
    xmlFreeDoc(doc);
  }
  const char *formats[] = {"/v3.0/en-US/music/artist/%s/deviceBackgroundImage",
                           "/v3.0/en-US/music/artist/%s/primaryImage",
                           "/v3.0/en-US/image/%s", "/v3.0/en-US/image/%s/",
                           "/1/%s/504/image.jpg"};
  for (size_t i = 0; i < 5; i++) {
    init(&s);
    CHECK(!hd_image_server_set_artist(&s, name));
    const char *format = formats[i];
    CHECK(format != NULL);
    snprintf(target, sizeof(target), format, i < 2 ? guid : image_guid);
    size_t n = collect_response(&s, target,
                                i < 2   ? "catalog.zune.net"
                                : i < 4 ? "image.catalog.zune.net"
                                        : "art.zune.net",
                                response);
    uint8_t *body = (uint8_t *)strstr((char *)response, "\r\n\r\n") + 4;
    CHECK(!memcmp(response, "HTTP/1.1 200", 12));
    CHECK(n - (size_t)(body - response) == sizeof(jpeg));
    CHECK(!memcmp(body, jpeg, sizeof(jpeg)));
    CHECK(s.image_completed == 1);
    CHECK(s.background_completed == (i == 0));
  }
  const char *bad[] = {
      "/v3.0/en-US/music/artist/00000000-0000-0000-0000-000000000000",
      "/v3.0/en-US/image/00000000-0000-0000-0000-000000000000",
      "http://evil.test/v3.0/en-US/music/artist/"
      "01234567-89ab-cdef-0123-456789abcdef",
      "http://catalog.zune.net.evil.test/fwlink",
      "http://catalog.zune.net@evil.test/fwlink",
      "https://catalog.zune.net/fwlink",
      "/v3.0/en-US/music/artist/01234567-89ab-cdef-0123-456789abcdef/../"
      "images"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    init(&s);
    collect_response(&s, bad[i], NULL, response);
    CHECK(!memcmp(response, "HTTP/1.1 404", 12));
    CHECK(!s.image_completed);
  }
  init(&s);
  snprintf(target, sizeof(target), "/v3.0/en-US/music/artist/%s", guid);
  collect_response(&s, target, "evil.test", response);
  CHECK(!memcmp(response, "HTTP/1.1 404", 12));
  init(&s);
  snprintf(target, sizeof(target),
           "http://catalog.zune.net/v3.0/en-US/music/artist/%s", guid);
  collect_response(&s, target, "art.zune.net", response);
  CHECK(!memcmp(response, "HTTP/1.1 404", 12));
}
static void catalog_configuration_test(void) {
  HdImageServer s;
  char before[4096], large[258];
  uint8_t q[512], r[600];
  init(&s);
  CHECK(!hd_image_server_set_artist(&s, "safe"));
  memcpy(before, s.overview, sizeof(before));
  const char *invalid[] = {"bad\x01",          "\xc0\xaf",     "\xed\xa0\x80",
                           "\xf4\x90\x80\x80", "\xef\xbf\xbe", "\xe2\x82"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    CHECK(hd_image_server_set_artist(&s, invalid[i]) == -1);
    CHECK(!memcmp(before, s.overview, sizeof(before)));
  }
  memset(large, 'x', 257);
  large[257] = 0;
  CHECK(hd_image_server_set_artist(&s, large) == -1);
  large[256] = 0;
  CHECK(!hd_image_server_set_artist(&s, large));
  memset(large, '"', 256);
  CHECK(!hd_image_server_set_artist(&s, large));
  xmlDoc *maximum_name_doc =
      xmlReadMemory(s.overview, (int)strlen(s.overview), "bounded.xml", NULL,
                    XML_PARSE_NONET);
  CHECK(maximum_name_doc);
  xmlFreeDoc(maximum_name_doc);
  const char *hosts[] = {"catalog.zune.net", "image.catalog.zune.net",
                         "art.zune.net", "mix.zune.net", "go.microsoft.com"};
  for (size_t i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++) {
    size_t n = dns_query(q, hosts[i]);
    CHECK(hdis_dns(&s, q, n, r, sizeof(r)) == n + 16);
    CHECK(hdis_u32(r + n + 12) == s.server_ip);
  }
  size_t n = dns_query(q, "catalog.zune.net.evil.test");
  CHECK(hdis_dns(&s, q, n, r, sizeof(r)) == n);
  CHECK((r[3] & 15) == 3);
  handshake(&s, 80);
  CHECK(hd_image_server_set_artist(&s, "changed") == -1);
  {
    unsigned w, h;
    const uint8_t sof[] = {255, 216, 255, 192, 0,    11, 8,   1,  224,
                           1,   224, 1,   1,   0x11, 0,  255, 217};
    hdis_jpeg_dimensions(sof, sizeof(sof), &w, &h);
    CHECK(w == 480 && h == 480);
    HdImageServer dimensions_server;
    CHECK(!hd_image_server_init(&dimensions_server, 1, guid, sof, sizeof(sof),
                                emit, NULL));
    CHECK(strstr(dimensions_server.images,
                 "<width>480</width><height>480</height>"));
    hdis_jpeg_dimensions(sof, 9, &w, &h);
    CHECK(!w && !h);
  }
}
/* Structural JPEG fixtures exercise the packet responder, not a JPEG decoder.
 * CLI preflight is separately responsible for complete decode before USB. */
static uint8_t multi_jpeg[HDIS_ASSETS][29];
static void multi_init(HdImageServer *s, HdImageAsset assets[HDIS_ASSETS]) {
  init(s);
  memset(assets, 0, sizeof(HdImageAsset) * HDIS_ASSETS);
  for (size_t i = 0; i < HDIS_ASSETS; i++) {
    uint8_t source[] = {255, 216, 255, 192,  0,   11,  8,   0,   0,  0,
                        0,   1,   1,   0x11, 0,   255, 254, 0,   10, 't',
                        'e', 's', 't', 'd',  'a', 't', 'a', 255, 217};
    memcpy(multi_jpeg[i], source, sizeof(source));
    hdis_w16(multi_jpeg[i] + 7, (uint16_t)(240 + 40 * i));
    hdis_w16(multi_jpeg[i] + 9, 480);
    multi_jpeg[i][26] = (uint8_t)('a' + i);
    snprintf(assets[i].guid, 37, "10000000-0000-0000-0000-%012u",
             (unsigned)i + 1);
    assets[i].jpeg = multi_jpeg[i];
    assets[i].jpeg_n = sizeof(source);
    assets[i].width = 123;
    assets[i].height = 456; /* Do not trust supplied metadata. */
  }
  CHECK(!hd_image_server_set_assets(s, assets, 3, 0, 1));
}
static void multi_asset_test(void) {
  HdImageServer s;
  HdImageAsset assets[HDIS_ASSETS];
  uint8_t response[8192];
  char path[256];
  multi_init(&s, assets);
  CHECK(s.asset_count == 3 && s.background_index == 0 && s.primary_index == 1);
  CHECK(!strcmp(s.image_guid, assets[1].guid));
  CHECK(strstr(s.overview, assets[1].guid));
  const char *atom = "http://www.w3.org/2005/Atom";
  const char *catalog = "http://schemas.zune.net/catalog/music/2007/10";
  snprintf(path, sizeof(path),
           "/v3.0/en-US/music/artist/%s/images?chunkSize=10", guid);
  size_t rn = collect_response(&s, path, "catalog.zune.net", response);
  char *body = strstr((char *)response, "\r\n\r\n") + 4;
  xmlDoc *doc =
      xmlReadMemory(body, (int)(rn - (size_t)(body - (char *)response)),
                    "multi.xml", NULL, XML_PARSE_NONET);
  CHECK(doc);
  xmlNode *root = xmlDocGetRootElement(doc);
  size_t entry_count = 0;
  for (xmlNode *n = root->children; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE ||
        xmlStrcmp(n->name, (const xmlChar *)"entry"))
      continue;
    CHECK(entry_count < 3);
    xmlChar *id = xmlNodeGetContent(element(n, "id", atom));
    CHECK(!strcmp((const char *)id + 9, assets[entry_count].guid));
    xmlFree(id);
    xmlNode *instance =
        element(element(n, "instances", catalog), "imageInstance", catalog);
    CHECK(instance);
    xmlChar *width = xmlNodeGetContent(element(instance, "width", catalog));
    xmlChar *height = xmlNodeGetContent(element(instance, "height", catalog));
    CHECK(atoi((const char *)width) == 480);
    CHECK(atoi((const char *)height) == 240 + (int)entry_count * 40);
    xmlFree(width);
    xmlFree(height);
    entry_count++;
  }
  CHECK(entry_count == 3);
  xmlFreeDoc(doc);
  CHECK(!s.image_completed && !s.distinct_assets_completed);
  for (size_t i = 0; i < 7; i++) {
    size_t selected;
    count = 0;
    if (i == 0) {
      selected = 0;
      snprintf(path, sizeof(path),
               "/v3.0/en-US/music/artist/%s/deviceBackgroundImage", guid);
    } else if (i == 1) {
      selected = 1;
      snprintf(path, sizeof(path), "/v3.0/en-US/music/artist/%s/primaryImage",
               guid);
    } else {
      selected = (i - 2) % 3;
      snprintf(path, sizeof(path),
               i < 5 ? "http://image.catalog.zune.net/v3.0/en-US/image/%s/"
                     : "http://art.zune.net/1/%s/504/image.jpg",
               assets[selected].guid);
    }
    rn = collect_response(&s, path, NULL, response);
    body = strstr((char *)response, "\r\n\r\n") + 4;
    CHECK(!memcmp(response, "HTTP/1.1 200", 12));
    CHECK(rn - (size_t)(body - (char *)response) == assets[selected].jpeg_n);
    CHECK(!memcmp(body, assets[selected].jpeg, assets[selected].jpeg_n));
  }
  CHECK(s.image_completed == 7 && s.background_completed == 1 &&
        s.primary_completed == 1);
  CHECK(s.distinct_assets_completed == 3 && s.distinct_contents_completed == 3);
  CHECK(s.asset_completed[0] == 3 && s.asset_completed[1] == 3 &&
        s.asset_completed[2] == 1);
  CHECK(hd_image_server_set_assets(&s, assets, 3, 0, 1) ==
        -1); /* Frozen even after all FINs. */
  count = 0;
  collect_response(&s, "/1/10000000-0000-0000-0000-000000000099/504/image.jpg",
                   NULL, response);
  CHECK(!memcmp(response, "HTTP/1.1 404", 12));
  CHECK(s.image_completed == 7);
  const char *queries[] = {"?width=480&resize=true&contenttype=image/jpeg",
                           "?width=100",
                           "?width=480&width=480",
                           "?resize=false",
                           "?filename=/tmp/anything",
                           "?",
                           "?width=480&",
                           "?contenttype=image/png",
                           "?width=9999999999999999999999"};
  for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) {
    count = 0;
    snprintf(path, sizeof(path), "/v3.0/en-US/image/%.36s%s", assets[0].guid,
             queries[i]);
    collect_response(&s, path, NULL, response);
    CHECK(!memcmp(response, i == 0 ? "HTTP/1.1 200" : "HTTP/1.1 404", 12));
  }
  CHECK(s.image_completed == 8 && s.distinct_assets_completed == 3);
  /* Distinct IDs can deliberately share content; report that truthfully. */
  multi_init(&s, assets);
  assets[2].jpeg = assets[0].jpeg;
  assets[2].jpeg_n = assets[0].jpeg_n;
  CHECK(!hd_image_server_set_assets(&s, assets, 3, 0, 1));
  for (size_t i = 0; i < 3; i++) {
    count = 0;
    snprintf(path, sizeof(path), "/v3.0/en-US/image/%.36s", assets[i].guid);
    collect_response(&s, path, NULL, response);
  }
  CHECK(s.distinct_assets_completed == 3 && s.distinct_contents_completed == 2);
}
static void multi_validation_test(void) {
  HdImageServer s;
  HdImageAsset assets[HDIS_ASSETS], bad[HDIS_ASSETS];
  char before[HDIS_IMAGES_XML], name[257];
  multi_init(&s, assets);
  memcpy(before, s.images, sizeof(before));
  CHECK(hd_image_server_set_assets(&s, assets, 0, 0, 0) == -1);
  CHECK(hd_image_server_set_assets(&s, assets, 7, 0, 0) == -1);
  CHECK(hd_image_server_set_assets(&s, assets, 3, 3, 0) == -1);
  CHECK(hd_image_server_set_assets(&s, assets, 3, 0, 3) == -1);
  for (unsigned i = 0; i < 9; i++) {
    memcpy(bad, assets, sizeof(bad));
    if (i == 0)
      memcpy(bad[1].guid, bad[0].guid, 37);
    if (i == 1)
      memcpy(bad[1].guid, guid, 37);
    if (i == 2)
      bad[0].guid[36] = 'x';
    if (i == 3)
      bad[0].guid[10] = '!';
    if (i == 4)
      bad[0].jpeg = NULL;
    if (i == 5)
      bad[0].jpeg_n = HDIS_ASSET_MAX + 1;
    if (i == 6)
      bad[0].jpeg_n = 3;
    if (i == 7) {
      bad[0].jpeg = jpeg;
      bad[0].jpeg_n = sizeof(jpeg);
    } /* No dimension segment. */
    if (i == 8)
      bad[0].jpeg_n--;
    CHECK(hd_image_server_set_assets(&s, bad, 3, 0, 1) == -1);
    CHECK(!memcmp(before, s.images, sizeof(before)));
    CHECK(s.asset_count == 3 && s.assets[1].jpeg == assets[1].jpeg);
  }
  hdis_w16(multi_jpeg[0] + 9, 5000);
  CHECK(hd_image_server_set_assets(&s, assets, 3, 0, 1) == -1);
  hdis_w16(multi_jpeg[0] + 9, 480);
  hdis_w16(multi_jpeg[0] + 7, 0);
  CHECK(hd_image_server_set_assets(&s, assets, 3, 0, 1) == -1);
  hdis_w16(multi_jpeg[0] + 7, 240);
  memset(name, '"', 256);
  name[256] = 0;
  CHECK(!hd_image_server_set_artist(&s, name));
  CHECK(!hd_image_server_set_assets(&s, assets, HDIS_LEGACY_ASSETS, 0, 1));
  xmlDoc *doc = xmlReadMemory(s.images, (int)strlen(s.images), "max.xml", NULL,
                              XML_PARSE_NONET);
  CHECK(doc);
  xmlFreeDoc(doc);
  CHECK(strlen(s.images) < HDIS_IMAGES_XML);
}
static void separate_roles_test(void) {
  HdImageServer s, before;
  HdImageAsset assets[HDIS_ASSETS], bad[HDIS_ASSETS];
  size_t members[] = {6,5,4,3,2,1};
  uint8_t response[8192]; char path[256];
  multi_init(&s, assets);
  hdis_w16(multi_jpeg[7]+9,160); hdis_w16(multi_jpeg[7]+7,120);
  CHECK(!hd_image_server_set_asset_roles(&s,assets,8,0,7,members,6));
  CHECK(s.separate_roles && s.gallery_count==6 && s.asset_count==8);
  CHECK(s.assets[7].width==160 && s.assets[7].height==120);
  CHECK(strstr(s.overview,assets[7].guid) && !strstr(s.images,assets[7].guid));
  CHECK(!strstr(s.images,assets[0].guid));
  before=s;
  size_t invalid[][6]={{1,2,3,4,5,5},{0,2,3,4,5,6},{7,2,3,4,5,6},{8,2,3,4,5,6}};
  for(size_t i=0;i<4;i++) {
    CHECK(hd_image_server_set_asset_roles(&s,assets,8,0,7,invalid[i],6)==-1);
    CHECK(!memcmp(&s,&before,sizeof(s)));
  }
  CHECK(hd_image_server_set_asset_roles(&s,assets,8,0,7,members,0)==-1);
  CHECK(hd_image_server_set_asset_roles(&s,assets,8,0,7,members,7)==-1);
  CHECK(hd_image_server_set_asset_roles(&s,assets,8,0,0,members,6)==-1);
  CHECK(hd_image_server_set_asset_roles(&s,assets,7,0,6,members,6)==-1);
  CHECK(hd_image_server_set_asset_roles(&s,assets,9,0,7,members,6)==-1);
  memcpy(bad,assets,sizeof(bad));memcpy(bad[7].guid,bad[0].guid,37);
  CHECK(hd_image_server_set_asset_roles(&s,bad,8,0,7,members,6)==-1);
  CHECK(!memcmp(&s,&before,sizeof(s)));
  snprintf(path,sizeof(path),"/v3.0/en-US/music/artist/%s/images",guid);
  size_t rn=collect_response(&s,path,NULL,response);
  char *body=strstr((char *)response,"\r\n\r\n")+4;
  xmlDoc *doc=xmlReadMemory(body,(int)(rn-(size_t)(body-(char *)response)),"roles.xml",NULL,XML_PARSE_NONET);
  CHECK(doc);size_t entries=0;
  for(xmlNode *n=xmlDocGetRootElement(doc)->children;n;n=n->next) {
    if(n->type!=XML_ELEMENT_NODE||xmlStrcmp(n->name,(const xmlChar *)"entry"))continue;
    CHECK(entries<6);
    xmlChar *id=xmlNodeGetContent(element(n,"id","http://www.w3.org/2005/Atom"));
    CHECK(!strcmp((char *)id+9,assets[members[entries]].guid));xmlFree(id);entries++;
  }
  CHECK(entries==6);xmlFreeDoc(doc);
  CHECK(hd_image_server_set_asset_roles(&s,assets,8,0,7,members,6)==-1);
  /* Background and thumbnail never count as collection acknowledgements. */
  const char *queries[]={"?width=160&resize=true","?width=480&resize=true",
                        "?width=320&resize=true","?width=480","?width=480&resize=false"};
  for(size_t i=0;i<5;i++) {
    count=0;snprintf(path,sizeof(path),"/v3.0/en-US/image/%s%s",assets[7].guid,queries[i]);
    rn=collect_response(&s,path,NULL,response);
    CHECK(!memcmp(response,i<2?"HTTP/1.1 200":"HTTP/1.1 404",12));
    if(i<2){body=strstr((char *)response,"\r\n\r\n")+4;
      CHECK(rn-(size_t)(body-(char *)response)==assets[7].jpeg_n);
      CHECK(!memcmp(body,assets[7].jpeg,assets[7].jpeg_n));}
    CHECK(!s.gallery_completed && !s.distinct_gallery_completed);
  }
  count=0;snprintf(path,sizeof(path),"/v3.0/en-US/music/artist/%s/deviceBackgroundImage?width=480",guid);
  collect_response(&s,path,NULL,response);
  CHECK(s.background_completed==1 && !s.gallery_completed);
  for(size_t i=0;i<6;i++) {
    count=0;snprintf(path,sizeof(path),"/1/%s/504/image.jpg?width=480&resize=true",assets[members[i]].guid);
    rn=collect_response(&s,path,NULL,response);body=strstr((char *)response,"\r\n\r\n")+4;
    CHECK(!memcmp(response,"HTTP/1.1 200",12));
    CHECK(rn-(size_t)(body-(char *)response)==assets[members[i]].jpeg_n);
    CHECK(!memcmp(body,assets[members[i]].jpeg,assets[members[i]].jpeg_n));
    CHECK(s.distinct_gallery_completed==i+1);
  }
  CHECK(s.gallery_completed==6 && s.distinct_assets_completed==8 && s.distinct_contents_completed==8);
  count=0;collect_response(&s,path,NULL,response);
  CHECK(s.gallery_completed==7 && s.distinct_gallery_completed==6 && s.distinct_assets_completed==8);
  multi_init(&s,assets);hdis_w16(multi_jpeg[1]+9,160);hdis_w16(multi_jpeg[7]+9,160);
  CHECK(!hd_image_server_set_asset_roles(&s,assets,8,0,7,members,6));
  snprintf(path,sizeof(path),"/v3.0/en-US/image/%s?width=480&resize=true",assets[1].guid);
  collect_response(&s,path,NULL,response);CHECK(!memcmp(response,"HTTP/1.1 404",12));
  CHECK(!s.image_completed);
}
static void multi_interleaved_test(void) {
  HdImageServer s;
  HdImageAsset assets[HDIS_ASSETS];
  char req[256];
  multi_init(&s, assets);
  handshake(&s, 80);
  input(&s, 49949, 100, 0, 2, NULL, 0);
  input(&s, 49949, 101, s.flow[1].send_next, 0x10, NULL, 0);
  CHECK(s.flow[1].established);
  for (size_t i = 0; i < 2; i++) {
    snprintf(req, sizeof(req), "GET /v3.0/en-US/image/%.36s HTTP/1.0\r\n\r\n",
             assets[i].guid);
    input(&s, i ? 49949 : 80, 101, s.flow[i].send_next, 0x18, req, strlen(req));
    CHECK(s.flow[i].body == assets[i].jpeg);
  }
  CHECK(!s.image_completed && s.asset_requests[0] == 1 &&
        s.asset_requests[1] == 1);
  CHECK(hd_image_server_set_assets(&s, assets, 3, 2, 2) == -1);
  /* Retransmit, partial and future ACKs never count completion. */
  CHECK(!hd_image_server_tick(&s, 1000));
  input(&s, 80, s.flow[0].receive_next, s.flow[0].send_next - 1, 0x10, NULL, 0);
  input(&s, 80, s.flow[0].receive_next, s.flow[0].send_next + 1, 0x10, NULL, 0);
  CHECK(!s.image_completed);
  /* Complete second flow first; reset the first before its FIN ACK. */
  while (s.flow[1].active)
    input(&s, 49949, s.flow[1].receive_next, s.flow[1].send_next, 0x10, NULL,
          0);
  CHECK(s.asset_completed[1] == 1 && !s.asset_completed[0]);
  input(&s, 80, s.flow[0].receive_next, s.flow[0].send_next, 0x14, NULL, 0);
  CHECK(!s.flow[0].active);
  CHECK(s.image_completed == 1 && s.distinct_assets_completed == 1);
  /* A new requested asset that times out must not count. */
  count = 0;
  handshake(&s, 80);
  snprintf(req, sizeof(req), "GET /v3.0/en-US/image/%.36s HTTP/1.0\r\n\r\n",
           assets[2].guid);
  input(&s, 80, 101, s.flow[0].send_next, 0x18, req, strlen(req));
  for (unsigned i = 2; i <= 7; i++)
    CHECK(!hd_image_server_tick(&s, i * 1000));
  CHECK(!s.flow[0].active && s.timed_out == 1);
  CHECK(s.image_completed == 1 && !s.asset_completed[2]);
}

static void biography_test(void) {
  HdImageServer s,before;char target[256];uint8_t response[8192];
  const char *text="Café & <script>bad</script> \"quote\" 'single'\nSecond line.";
  init(&s);CHECK(!hd_image_server_set_artist(&s,"A & B"));
  CHECK(!hd_image_server_set_biography(&s,text,strlen(text)));
  CHECK(!strcmp(s.biography_text,text));before=s;
  const char *bad[]={"\xc0\xaf","\xed\xa0\x80","\xf4\x90\x80\x80","\xef\xbf\xbe","\xe2\x82","x\x01","x\0y"};
  const size_t lengths[]={2,3,4,3,2,2,3};
  for(size_t i=0;i<sizeof(lengths)/sizeof(lengths[0]);i++) {
    CHECK(hd_image_server_set_biography(&s,bad[i],lengths[i])==-1);
    CHECK(!memcmp(&before,&s,sizeof(s)));
  }
  char maximum[HDIS_BIO_MAX+2];memset(maximum,'\'',sizeof(maximum));
  CHECK(hd_image_server_set_biography(&s,maximum,HDIS_BIO_MAX+1)==-1);
  CHECK(!memcmp(&before,&s,sizeof(s)));
  CHECK(!hd_image_server_set_biography(&s,maximum,HDIS_BIO_MAX));
  xmlDoc *doc=xmlReadMemory(s.biography,(int)strlen(s.biography),"max-bio.xml",NULL,XML_PARSE_NONET);CHECK(doc);xmlFreeDoc(doc);
  memset(maximum,'\n',HDIS_BIO_MAX);
  CHECK(!hd_image_server_set_biography(&s,maximum,HDIS_BIO_MAX));
  doc=xmlReadMemory(s.biography,(int)strlen(s.biography),"newlines.xml",NULL,XML_PARSE_NONET);CHECK(doc);xmlFreeDoc(doc);
  CHECK(!hd_image_server_set_biography(&s,text,strlen(text)));
  CHECK(!hd_image_server_set_artist(&s,"New & Name"));
  CHECK(!strcmp(s.biography_text,text) && strstr(s.biography,"New &amp; Name"));
  snprintf(target,sizeof(target),"/v3.0/en-US/music/artist/%s/biography",guid);
  size_t rn=collect_response(&s,target,"catalog.zune.net",response);
  CHECK(!memcmp(response,"HTTP/1.1 200",12));
  char *body=strstr((char *)response,"\r\n\r\n")+4;
  CHECK(rn-(size_t)(body-(char *)response)==strlen(s.biography));
  CHECK(!strcmp(body,s.biography));
  doc=xmlReadMemory(body,(int)strlen(body),"bio.xml",NULL,XML_PARSE_NONET);CHECK(doc);
  xmlNode *content=element(xmlDocGetRootElement(doc),"content","http://www.w3.org/2005/Atom");CHECK(content);
  xmlChar *kind=xmlGetProp(content,(const xmlChar *)"type");CHECK(kind&&!strcmp((char *)kind,"html"));xmlFree(kind);
  xmlChar *html=xmlNodeGetContent(content);CHECK(html);
  CHECK(!strcmp((char *)html,"<p>Café &amp; &lt;script&gt;bad&lt;/script&gt; &quot;quote&quot; &apos;single&apos;<br/>Second line.</p>"));
  CHECK(!strstr((char *)html,"<script>"));xmlFree(html);xmlFreeDoc(doc);
  CHECK(s.biography_requests==1 && s.biography_completed==1 && !s.image_completed);
  before=s;CHECK(hd_image_server_set_biography(&s,"changed",7)==-1);CHECK(!memcmp(&before,&s,sizeof(s)));
  count=0;snprintf(target,sizeof(target),"/v3.0/en-US/music/artist/00000000-0000-0000-0000-000000000000/biography");
  collect_response(&s,target,NULL,response);CHECK(!memcmp(response,"HTTP/1.1 404",12));
  CHECK(s.biography_requests==1 && s.biography_completed==1);
  init(&s);CHECK(!hd_image_server_set_biography(&s,text,strlen(text)));handshake(&s,80);
  char request[256];snprintf(request,sizeof(request),"GET /v3.0/en-US/music/artist/%s/biography HTTP/1.0\r\n\r\n",guid);
  input(&s,80,101,s.flow[0].send_next,0x18,request,strlen(request));
  CHECK(s.biography_requests==1 && !s.biography_completed);
  while(!s.flow[0].fin_sent)input(&s,80,s.flow[0].receive_next,s.flow[0].send_next,0x10,NULL,0);
  CHECK(!s.biography_completed);
  input(&s,80,s.flow[0].receive_next,s.flow[0].send_next-1,0x10,NULL,0);
  input(&s,80,s.flow[0].receive_next,s.flow[0].send_next+1,0x10,NULL,0);
  CHECK(!s.biography_completed);
  input(&s,80,s.flow[0].receive_next,s.flow[0].send_next,0x10,NULL,0);
  CHECK(s.biography_completed==1 && !s.image_completed && !s.background_completed);
}
/* Live Pavo opened an idle proxy plus a burst of catalog/image TCP connections.
 * Four slots silently refused retrying SYNs. Exercise bounded simultaneous
 * admission, exact per-flow responses, and full-table refusal without eviction. */
static void port_input(HdImageServer *s,uint16_t source,uint16_t destination,
    uint32_t seq,uint32_t ack,uint8_t flags,const void *data,size_t n) {
  uint8_t p[1500];size_t z=packet(s,p,destination,seq,ack,flags,data,n);
  hdis_w16(p+20,source);hdis_w16(p+36,0);
  hdis_w16(p+36,hdis_transport_sum(p,p+20,z-20,6));
  CHECK(!hd_image_server_packet(s,p,z));
}
static void concurrent_capacity_test(void) {
  HdImageServer s;HdImageAsset assets[HDIS_ASSETS];
  const size_t members[]={1,2,3,4,5,6};
  HdImageFlow snapshot[HDIS_FLOWS];
  uint8_t received[8][1024];size_t received_n[8]={0};
  multi_init(&s,assets);
  CHECK(!hd_image_server_set_asset_roles(&s,assets,8,0,7,members,6));
  for(size_t i=0;i<HDIS_FLOWS;i++) {
    count=0;
    port_input(&s,(uint16_t)(49164+i),i?80:49949,100,0,2,NULL,0);
    CHECK(count==1 && output[0][33]==0x12);
    CHECK(hdis_u16(output[0]+22)==49164+i);
    CHECK(!hdis_checksum(output[0],20));
    CHECK(!hdis_transport_sum(output[0],output[0]+20,sizes[0]-20,6));
    port_input(&s,(uint16_t)(49164+i),i?80:49949,101,s.flow[i].send_next,0x10,NULL,0);
    CHECK(s.flow[i].active && s.flow[i].established);
  }
  CHECK(s.peak_flows==HDIS_FLOWS && !s.capacity_refused);
  memcpy(snapshot,s.flow,sizeof(snapshot));count=0;
  for(unsigned retry=0;retry<3;retry++)
    port_input(&s,49200,80,100,0,2,NULL,0);
  CHECK(count==0 && s.capacity_refused==3);
  CHECK(!memcmp(snapshot,s.flow,sizeof(snapshot)));
  CHECK(!s.image_completed && !s.gallery_completed);
  count=0;
  for(size_t i=0;i<8;i++) {
    char request[256];
    if(i==0)snprintf(request,sizeof(request),"GET /v3.0/en-US/music/artist/%s/deviceBackgroundImage HTTP/1.0\r\n\r\n",guid);
    else snprintf(request,sizeof(request),"GET /v3.0/en-US/image/%.36s HTTP/1.0\r\n\r\n",assets[i].guid);
    HdImageFlow *f=&s.flow[i+1];
    port_input(&s,f->peer_port,80,f->receive_next,f->send_next,0x18,request,strlen(request));
    CHECK(f->image && f->pending);
  }
  /* Every asset stays in-flight together, while the idle proxy stays alive. */
  CHECK(s.flow[0].active && !s.flow[0].response && s.requests==8);
  CHECK(!s.image_completed);
  for(unsigned round=0;round<10;round++) {
    for(size_t j=0;j<count;j++) {
      CHECK(!hdis_checksum(output[j],20));
      CHECK(!hdis_transport_sum(output[j],output[j]+20,sizes[j]-20,6));
      if(sizes[j]<=40)continue;
      unsigned index=(unsigned)hdis_u16(output[j]+22)-49165;
      CHECK(index<8 && received_n[index]+sizes[j]-40<sizeof(received[index]));
      memcpy(received[index]+received_n[index],output[j]+40,sizes[j]-40);
      received_n[index]+=sizes[j]-40;
    }
    count=0;int active=0;
    for(size_t i=0;i<8;i++) {
      HdImageFlow *f=&s.flow[i+1];if(!f->active)continue;
      active=1;port_input(&s,f->peer_port,80,f->receive_next,f->send_next,0x10,NULL,0);
    }
    if(!active)break;
  }
  for(size_t i=0;i<8;i++) {
    CHECK(received_n[i]<sizeof(received[i]));received[i][received_n[i]]=0;
    char *body=strstr((char *)received[i],"\r\n\r\n");CHECK(body);body+=4;
    CHECK(!memcmp(received[i],"HTTP/1.1 200",12));
    CHECK(received_n[i]-(size_t)(body-(char *)received[i])==assets[i].jpeg_n);
    CHECK(!memcmp(body,assets[i].jpeg,assets[i].jpeg_n));
    CHECK(s.asset_completed[i]==1);
  }
  CHECK(s.background_completed==1 && s.distinct_gallery_completed==6 && s.distinct_assets_completed==8);
  CHECK(s.flow[0].active && s.flow[0].peer_port==49164);
  count=0;port_input(&s,49200,80,100,0,2,NULL,0);
  CHECK(count==1 && output[0][33]==0x12 && s.flow[1].peer_port==49200);
  CHECK(s.capacity_refused==3 && s.distinct_assets_completed==8);
}

int main(void) {
  biography_test();
  concurrent_capacity_test();
  multi_asset_test();
  multi_validation_test();
  separate_roles_test();
  multi_interleaved_test();
  image_test();
  catalog_test();
  catalog_configuration_test();
  fwlink_diagnostic_test();
  rejection_test();
  mss_and_fragment_test();
  udp_test();
  bad_and_retransmit();
  dns_test();
  route_test();
  printf("HD image server: %u checks passed\n", tests);
  return 0;
}
