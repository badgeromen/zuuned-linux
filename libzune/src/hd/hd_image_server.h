#ifndef ZUNE_HD_IMAGE_SERVER_H
#define ZUNE_HD_IMAGE_SERVER_H
/* Private research responder. No sockets, filesystem or USB. Caller owns JPEGs.
 * Protocol leads: XuneSyncLibrary DNSHandler.cpp (8-byte TCP DNS framing),
 * HTTPParser.h (deviceBackgroundImage route), HttpClient.cpp (generic fwlink
 * HTML reply). Independently implemented C. IMPORTANT: the fwlink reply is a
 * diagnostic approximation only. The observed LinkId=25817 URL is associated
 * with MS-DRMCD secure-clock provisioning, whose challenge response requires
 * transaction correlation and a valid signature. HTML 200 cannot satisfy that
 * contract and does not prove connectivity, secure clock success, or that the
 * device will proceed to request artwork.
 * https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-drmcd/57809984-3dc2-4c1b-911e-46d3fa4600c9
 * Sixteen bounded flows, one outstanding segment per flow, no IP fragmentation/reassembly,
 * no TCP out-of-order buffering, HTTP pipelining, TLS or arbitrary proxying.
 * Clock supplied by caller must be monotonic milliseconds. */
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define HDIS_FLOWS 16
#define HDIS_MSS 1000
#define HDIS_REQUEST 4096
#define HDIS_JPEG_MAX (4u * 1024u * 1024u)
#define HDIS_ASSETS 8
#define HDIS_GALLERY_MAX 6
#define HDIS_LEGACY_ASSETS 6
#define HDIS_ASSET_MAX (128u * 1024u)
#define HDIS_ASSET_TOTAL_MAX (HDIS_ASSETS * HDIS_ASSET_MAX)
#define HDIS_IMAGES_XML 8192
#define HDIS_BIO_MAX 4096
#define HDIS_BIO_XML (12 * HDIS_BIO_MAX + 4096)
typedef struct {
  char guid[37];
  const uint8_t *jpeg;
  size_t jpeg_n;
  unsigned width, height; /* Parsed from JPEG, never trusted from caller. */
} HdImageAsset;
typedef int (*HdImageEmit)(void *, const uint8_t *, size_t);
typedef struct {
  int active, established, pending, fin_sent, response, image, background,
      primary, biography;
  size_t asset_index;
  uint32_t peer_ip, receive_next, send_next, pending_end;
  uint16_t peer_port, local_port, mss, window;
  uint8_t request[HDIS_REQUEST], header[512], pending_packet[HDIS_MSS + 40];
  size_t request_n, header_n, body_n, sent, pending_n;
  const uint8_t *body;
  uint64_t last_send;
  unsigned retries;
} HdImageFlow;
typedef struct {
  uint32_t server_ip, seed;
  uint16_t packet_id, proxy_port;
  char guid[37], image_guid[37], dns_hostname[128], last_request[256],
      last_dns[256];
  char overview[4096], images[HDIS_IMAGES_XML], biography[HDIS_BIO_XML];
  char biography_text[HDIS_BIO_MAX + 1];
  unsigned biography_requests, biography_completed;
  char artist_name[257];
  HdImageAsset assets[HDIS_ASSETS];
  size_t asset_count, background_index, primary_index;
  size_t gallery_count, gallery_indices[HDIS_GALLERY_MAX];
  unsigned gallery_completed, distinct_gallery_completed;
  int separate_roles;
  unsigned asset_completed[HDIS_ASSETS], asset_requests[HDIS_ASSETS];
  unsigned distinct_assets_completed, distinct_contents_completed,
      primary_completed;
  int frozen, assets_configured;
  const uint8_t *jpeg;
  size_t jpeg_n;
  HdImageEmit emit;
  void *opaque;
  uint64_t now;
  unsigned image_completed, requests, dns_queries, rejected, timed_out;
  unsigned fwlink_diagnostic_responses, background_completed;
  unsigned capacity_refused, peak_flows;
  HdImageFlow flow[HDIS_FLOWS];
} HdImageServer;
static uint16_t hdis_u16(const uint8_t *p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}
static uint32_t hdis_u32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}
static void hdis_w16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}
static void hdis_w32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}
static uint32_t hdis_sum(const uint8_t *p, size_t n, uint32_t sum) {
  while (n >= 2) {
    sum += hdis_u16(p);
    p += 2;
    n -= 2;
  }
  if (n)
    sum += (uint32_t)p[0] << 8;
  return sum;
}
static uint16_t hdis_fold(uint32_t n) {
  while (n >> 16)
    n = (n & 65535) + (n >> 16);
  return (uint16_t)~n;
}
static uint16_t hdis_checksum(const uint8_t *p, size_t n) {
  return hdis_fold(hdis_sum(p, n, 0));
}
static uint16_t hdis_transport_sum(const uint8_t *ip, const uint8_t *p,
                                   size_t n, uint8_t protocol) {
  return hdis_fold(
      hdis_sum(p, n, hdis_sum(ip + 12, 8, protocol + (uint32_t)n)));
}
static void hdis_ip(HdImageServer *s, uint8_t *p, size_t n, uint32_t peer,
                    uint8_t protocol) {
  memset(p, 0, 20);
  p[0] = 0x45;
  hdis_w16(p + 2, (uint16_t)n);
  hdis_w16(p + 4, ++s->packet_id);
  p[8] = 64;
  p[9] = protocol;
  hdis_w32(p + 12, s->server_ip);
  hdis_w32(p + 16, peer);
  hdis_w16(p + 10, hdis_checksum(p, 20));
}
/* Catalog schema leads: artist server 393c13c813ef32953376a3efc383a8e2b5eee586
 * main.py and Zune.Net catalog ff8e36374b049d000b9b9227fe5e69e1c4f70cf4.
 * These are independently serialized, local-only responses for ONE artist.
 * No provider identities are derived or written by this responder. */
static int hdis_art_host(const char *host) {
  return !strcmp(host, "catalog.zune.net") ||
         !strcmp(host, "image.catalog.zune.net") ||
         !strcmp(host, "art.zune.net") || !strcmp(host, "mix.zune.net");
}
static int hdis_host(const uint8_t *p, size_t n, char result[128]) {
  size_t i;
  if (!n || n >= 128)
    return 0;
  if (n > 3 && !memcmp(p + n - 3, ":80", 3))
    n -= 3;
  if (!n)
    return 0;
  for (i = 0; i < n; i++) {
    unsigned char c = p[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '-'))
      return 0;
    result[i] = (char)tolower(c);
  }
  result[n] = 0;
  return hdis_art_host(result) || !strcmp(result, "go.microsoft.com");
}
/* Reject malformed UTF-8 and XML 1.0 controls, then escape all markup chars. */
static int hdis_xml_text(const char *name, size_t n, char *escaped, size_t capacity) {
  size_t i = 0, out = 0;
  if (!name || !escaped || !capacity) return -1;
  while (i < n) {
    unsigned char c = (unsigned char)name[i];
    uint32_t cp;
    size_t bytes;
    const char *entity = NULL;
    if (c < 0x80) {
      cp = c;
      bytes = 1;
    } else if (c >= 0xc2 && c <= 0xdf) {
      cp = c & 31;
      bytes = 2;
    } else if (c >= 0xe0 && c <= 0xef) {
      cp = c & 15;
      bytes = 3;
    } else if (c >= 0xf0 && c <= 0xf4) {
      cp = c & 7;
      bytes = 4;
    } else
      return -1;
    if (i + bytes > n)
      return -1;
    for (size_t j = 1; j < bytes; j++) {
      unsigned char b = (unsigned char)name[i + j];
      if ((b & 0xc0) != 0x80)
        return -1;
      cp = (cp << 6) | (b & 63);
    }
    if ((bytes == 2 && cp < 0x80) || (bytes == 3 && cp < 0x800) ||
        (bytes == 4 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff) || cp == 0xfffe || cp == 0xffff ||
        (cp < 32 && cp != 9 && cp != 10 && cp != 13))
      return -1;
    if (c == '&')
      entity = "&amp;";
    else if (c == '<')
      entity = "&lt;";
    else if (c == '>')
      entity = "&gt;";
    else if (c == '\"')
      entity = "&quot;";
    else if (c == '\'')
      entity = "&apos;";
    if (entity) {
      size_t e = strlen(entity);
      if (e >= capacity - out) return -1;
      memcpy(escaped + out, entity, e);
      out += e;
    } else {
      if (bytes >= capacity - out) return -1;
      memcpy(escaped + out, name + i, bytes);
      out += bytes;
    }
    i += bytes;
  }
  escaped[out] = 0;
  return 0;
}
static int hdis_xml_name(const char *name, char escaped[1537]) {
  size_t n = 0;
  if (!name) return -1;
  while (n <= 256 && name[n]) n++;
  return n > 256 ? -1 : hdis_xml_text(name, n, escaped, 1537);
}
/* Input is plain text, never trusted HTML. Escape once for a generated HTML
 * paragraph, then again for Atom's type=html XML text node. Newlines become
 * generated <br/> tags; source markup always remains inert text. */
static int hdis_biography_xml(const HdImageServer *s, const char *name,
                              const char *text, size_t n, char *xml) {
  char plain[6 * HDIS_BIO_MAX + 1], html[6 * HDIS_BIO_MAX + 32];
  char escaped[12 * HDIS_BIO_MAX + 64];
  size_t used=0;
  if (n > HDIS_BIO_MAX || hdis_xml_text(text,n,plain,sizeof(plain))) return -1;
  if (n) {
    memcpy(html,"<p>",3);used=3;
    for(size_t i=0;plain[i];i++) {
      if(plain[i]=='\r')continue;
      if(plain[i]=='\n') { memcpy(html+used,"<br/>",5);used+=5; }
      else html[used++]=plain[i];
    }
    memcpy(html+used,"</p>",4);used+=4;
  }
  html[used]=0;
  if(hdis_xml_text(html,used,escaped,sizeof(escaped)))return -1;
  int wrote=snprintf(xml,HDIS_BIO_XML,
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<a:entry xmlns:a=\"http://www.w3.org/2005/Atom\" "
      "xmlns=\"http://schemas.zune.net/catalog/music/2007/10\">"
      "<a:id>tag:catalog.zune.net,1900-01-01:/music/artist/%s/biography</a:id>"
      "<a:title type=\"text\">%s</a:title><a:updated>1900-01-01T00:00:00Z</a:updated>"
      "<a:link rel=\"self\" type=\"application/atom+xml\" "
      "href=\"/v3.0/en-US/music/artist/%s/biography\"/>"
      "<a:content type=\"html\">%s</a:content>"
      "<a:author><a:name>Zuuned local artwork</a:name></a:author></a:entry>",
      s->guid,name,s->guid,escaped);
  return wrote < 0 || wrote >= HDIS_BIO_XML ? -1 : wrote;
}
/* Length is explicit so embedded NULs cannot silently truncate file contents. */
static int hd_image_server_set_biography(HdImageServer *s,const char *text,size_t n) {
  char name[1537],xml[HDIS_BIO_XML];
  if(!s||s->frozen||!text||n>HDIS_BIO_MAX)return -1;
  for(size_t i=0;i<HDIS_FLOWS;i++)if(s->flow[i].active)return -1;
  if(hdis_xml_name(s->artist_name,name))return -1;
  int wrote=hdis_biography_xml(s,name,text,n,xml);
  if(wrote<0)return -1;
  memcpy(s->biography,xml,(size_t)wrote+1);
  memmove(s->biography_text,text,n);s->biography_text[n]=0;
  return 0;
}
static void hdis_jpeg_dimensions(const uint8_t *p, size_t n, unsigned *width,
                                 unsigned *height) {
  size_t at = 2;
  *width = 0;
  *height = 0;
  while (at < n) {
    unsigned marker;
    size_t length;
    if (p[at++] != 255)
      return;
    while (at < n && p[at] == 255)
      at++;
    if (at >= n)
      return;
    marker = p[at++];
    if (marker == 0xda || marker == 0xd9)
      return;
    if (marker == 1 || (marker >= 0xd0 && marker <= 0xd7))
      continue;
    if (at + 2 > n)
      return;
    length = hdis_u16(p + at);
    if (length < 2 || length > n - at)
      return;
    if ((marker >= 0xc0 && marker <= 0xc3) ||
        (marker >= 0xc5 && marker <= 0xc7) ||
        (marker >= 0xc9 && marker <= 0xcb) ||
        (marker >= 0xcd && marker <= 0xcf)) {
      if (length < 8)
        return;
      unsigned h = hdis_u16(p + at + 3), w = hdis_u16(p + at + 5);
      if (!w || !h)
        return;
      *width = w;
      *height = h;
      return;
    }
    at += length;
  }
}
/* Call before traffic. Name <=256 UTF-8 bytes; NULL means no name.
 * Overview uses the primary asset ID; the image feed advertises only collection members.
 * Reconfiguration after traffic is refused so body pointers remain stable. */
static int hd_image_server_set_artist(HdImageServer *s, const char *name) {
  char escaped[1537], image_id[37];
  int a, b, c;
  char overview[4096], images[HDIS_IMAGES_XML], bio[HDIS_BIO_XML];
  if (!s || s->frozen)
    return -1;
  for (size_t i = 0; i < HDIS_FLOWS; i++)
    if (s->flow[i].active)
      return -1;
  if (hdis_xml_name(name ? name : "", escaped) < 0)
    return -1;
  memcpy(image_id, s->assets[s->primary_index].guid, 37);
  a = snprintf(
      overview, sizeof(overview),
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<a:entry xmlns:a=\"http://www.w3.org/2005/Atom\" "
      "xmlns=\"http://schemas.zune.net/catalog/music/2007/10\">"
      "<a:id>urn:uuid:%s</a:id><a:title "
      "type=\"text\">%s</a:title><a:updated>1900-01-01T00:00:00Z</a:updated>"
      "<a:link rel=\"self\" type=\"application/atom+xml\" "
      "href=\"/v3.0/en-US/music/artist/%s\"/>"
      "<a:link rel=\"zune://artist/biography\" type=\"application/atom+xml\" "
      "href=\"/v3.0/en-US/music/artist/%s/biography\"/>"
      "<a:link rel=\"zune://artist/images\" type=\"application/atom+xml\" "
      "href=\"/v3.0/en-US/music/artist/%s/images\"/>"
      "<sortName>%s</sortName><isDisabled>False</"
      "isDisabled><image><id>urn:uuid:%s</id></image>"
      "<a:author><a:name>Zuuned local artwork</a:name></a:author></a:entry>",
      s->guid, escaped, s->guid, s->guid, s->guid, escaped, image_id);
  b = snprintf(
      images, sizeof(images),
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<a:feed xmlns:a=\"http://www.w3.org/2005/Atom\" "
      "xmlns=\"http://schemas.zune.net/catalog/music/2007/10\">"
      "<a:id>tag:catalog.zune.net,1900-01-01:/music/artist/%s/images</a:id>"
      "<a:title "
      "type=\"text\">%s</a:title><a:updated>1900-01-01T00:00:00Z</a:updated>"
      "<a:link rel=\"self\" type=\"application/atom+xml\" "
      "href=\"/v3.0/en-US/music/artist/%s/images\"/>",
      s->guid, escaped, s->guid);
  if (b < 0 || (size_t)b >= sizeof(images))
    return -1;
  for (size_t i = 0; i < s->gallery_count; i++) {
    const HdImageAsset *asset = &s->assets[s->gallery_indices[i]];
    char dimensions[100] = "";
    if (asset->width && asset->height)
      (void)snprintf(dimensions, sizeof(dimensions),
                     "<width>%u</width><height>%u</height>", asset->width,
                     asset->height);
    int wrote = snprintf(
        images + b, sizeof(images) - (size_t)b,
        "<a:entry><a:id>urn:uuid:%s</a:id><a:title type=\"text\">Local artist "
        "image</a:title>"
        "<a:updated>1900-01-01T00:00:00Z</a:updated><instances><imageInstance>"
        "<id>urn:uuid:%s</id><url>http://art.zune.net/1/%s/504/image.jpg</url>"
        "<format>jpg</format>%s</imageInstance></instances></a:entry>",
        asset->guid, asset->guid, asset->guid, dimensions);
    if (wrote < 0 || (size_t)wrote >= sizeof(images) - (size_t)b)
      return -1;
    b += wrote;
  }
  {
    int wrote = snprintf(
        images + b, sizeof(images) - (size_t)b,
        "<a:author><a:name>Zuuned local artwork</a:name></a:author></a:feed>");
    if (wrote < 0 || (size_t)wrote >= sizeof(images) - (size_t)b)
      return -1;
    b += wrote;
  }
  c = hdis_biography_xml(s,escaped,s->biography_text,strlen(s->biography_text),bio);
  if (a < 0 || b < 0 || c < 0 || (size_t)a >= sizeof(overview) ||
      (size_t)b >= sizeof(images) || (size_t)c >= sizeof(bio))
    return -1;
  memcpy(s->overview, overview, (size_t)a + 1);
  memcpy(s->images, images, (size_t)b + 1);
  memcpy(s->biography, bio, (size_t)c + 1);
  memcpy(s->image_guid, image_id, 37);
  if (name != s->artist_name) {
    size_t name_n = name ? strlen(name) : 0;
    if (name_n)
      memcpy(s->artist_name, name, name_n);
    s->artist_name[name_n] = 0;
  }
  return 0;
}
/* Multi-image configuration is atomic and frozen on the first input packet.
 * The caller retains immutable byte buffers for the entire server lifetime and
 * MUST fully decode JPEGs before opening USB. This layer validates bounded
 * framing/dimensions, not compressed entropy. No memory allocation or codec. */
static int hdis_set_assets(HdImageServer *s,
                                      const HdImageAsset *assets, size_t count,
                                      size_t background, size_t primary,
                                      const size_t *members, size_t member_count, int separate) {
  HdImageAsset parsed[HDIS_ASSETS] = {0}, old[HDIS_ASSETS];
  size_t total = 0;
  if (!s || s->frozen || !assets || !count || count > HDIS_ASSETS ||
      background >= count || primary >= count || !members || (!member_count && !separate) ||
      member_count > HDIS_GALLERY_MAX ||
      (separate ? (count != member_count + 2 || background == primary)
                : count > HDIS_LEGACY_ASSETS))
    return -1;
  for (size_t i = 0; i < member_count; i++) {
    if (members[i] >= count || (separate &&
        (members[i] == background || members[i] == primary))) return -1;
    for (size_t j = 0; j < i; j++)
      if (members[i] == members[j]) return -1;
  }
  for (size_t i = 0; i < count; i++) {
    parsed[i] = assets[i];
    if (assets[i].guid[36] != 0 || !assets[i].jpeg || assets[i].jpeg_n < 4 ||
        assets[i].jpeg_n > HDIS_ASSET_MAX)
      return -1;
    for (size_t j = 0; j < 36; j++) {
      unsigned char c = (unsigned char)assets[i].guid[j];
      if ((j == 8 || j == 13 || j == 18 || j == 23) ? c != '-' : !isxdigit(c))
        return -1;
      parsed[i].guid[j] = (char)tolower(c);
    }
    if (!strcmp(parsed[i].guid, s->guid))
      return -1;
    for (size_t j = 0; j < i; j++)
      if (!strcmp(parsed[j].guid, parsed[i].guid))
        return -1;
    const uint8_t *p = parsed[i].jpeg;
    size_t n = parsed[i].jpeg_n;
    if (p[0] != 255 || p[1] != 216 || p[n - 2] != 255 || p[n - 1] != 217)
      return -1;
    hdis_jpeg_dimensions(p, n, &parsed[i].width, &parsed[i].height);
    if (!parsed[i].width || !parsed[i].height || parsed[i].width > 4096 ||
        parsed[i].height > 4096 ||
        (uint64_t)parsed[i].width * parsed[i].height > 8000000)
      return -1;
    if (n > HDIS_ASSET_TOTAL_MAX - total)
      return -1;
    total += n;
  }
  memcpy(old, s->assets, sizeof(old));
  size_t old_count = s->asset_count, old_background = s->background_index,
         old_primary = s->primary_index, old_gallery_count = s->gallery_count;
  size_t old_members[HDIS_GALLERY_MAX];
  memcpy(old_members, s->gallery_indices, sizeof(old_members));
  memcpy(s->assets, parsed, count * sizeof(*parsed));
  s->asset_count = count;
  s->background_index = background;
  s->primary_index = primary;
  s->gallery_count = member_count;
  memcpy(s->gallery_indices, members, member_count * sizeof(*members));
  if (hd_image_server_set_artist(s, s->artist_name) < 0) {
    memcpy(s->assets, old, sizeof(old));
    s->asset_count = old_count;
    s->background_index = old_background;
    s->primary_index = old_primary;
    s->gallery_count = old_gallery_count;
    memcpy(s->gallery_indices, old_members, sizeof(old_members));
    return -1;
  }
  s->assets_configured = 1;
  s->separate_roles = separate;
  s->jpeg = s->assets[background].jpeg;
  s->jpeg_n = s->assets[background].jpeg_n;
  return 0;
}
/* Legacy mode includes every asset in the feed, including background/primary. */
static int hd_image_server_set_assets(HdImageServer *s, const HdImageAsset *assets,
                                      size_t count, size_t background, size_t primary) {
  const size_t members[HDIS_GALLERY_MAX] = {0,1,2,3,4,5};
  if (count > HDIS_LEGACY_ASSETS) return -1;
  return hdis_set_assets(s, assets, count, background, primary, members, count, 0);
}
/* Explicit mode assigns every asset exactly one role; the ordered collection
 * excludes both background and thumbnail. Configuration is immutable in use. */
static int hd_image_server_set_asset_roles(HdImageServer *s, const HdImageAsset *assets,
      size_t count, size_t background, size_t thumbnail,
      const size_t *members, size_t member_count) {
  return hdis_set_assets(s, assets, count, background, thumbnail, members, member_count, 1);
}
/* No dynamic resize: explicit multiasset callers preconvert JPEGs. Supported
 * image query parameters must describe the bytes actually served. Legacy init
 * retains its historical ignored-query behavior for compatibility. */
static int hdis_image_query(const HdImageServer *s, size_t index,
                            const uint8_t *query, size_t n) {
  unsigned seen = 0;
  int thumbnail_width_compat = 0;
  if (!s->assets_configured || !n)
    return 1;
  if (*query++ != '?' || --n == 0)
    return 0;
  while (n) {
    size_t end = 0;
    unsigned bit = 0;
    while (end < n && query[end] != '&')
      end++;
    if (end > 6 && !memcmp(query, "width=", 6)) {
      unsigned width = 0;
      for (size_t i = 6; i < end; i++) {
        if (query[i] < '0' || query[i] > '9' || width > 4096)
          return 0;
        width = width * 10 + (unsigned)(query[i] - '0');
      }
      if (width != s->assets[index].width) {
        /* The static HD thumbnail template serves its actual 160px JPEG for
         * a width=480&resize=true request. Do not claim or perform upscaling. */
        if (!s->separate_roles || index != s->primary_index ||
            s->assets[index].width != 160 || width != 480) return 0;
        thumbnail_width_compat = 1;
      }
      bit = 1;
    } else if (end == 11 && !memcmp(query, "resize=true", 11))
      bit = 2;
    else if (end == 22 && !memcmp(query, "contenttype=image/jpeg", 22))
      bit = 4;
    else
      return 0;
    if (seen & bit)
      return 0;
    seen |= bit;
    query += end;
    n -= end;
    if (n) {
      query++;
      n--;
      if (!n)
        return 0;
    }
  }
  return !thumbnail_width_compat || (seen & 2);
}
/* Validates an HTTP GET target plus any supplied Host. Hostless origin-form
 * remains supported for old HTTP/1.0 peers. Absolute-form requires exact host.
 * Returns route enum or zero (404), never a file/network location. */
static int hdis_catalog_route(HdImageServer *s, const uint8_t *request,
                              size_t line_end, size_t total,
                              size_t *asset_index) {
  size_t end = 4, start = 4, path_end, at;
  char host[128] = "", absolute_host[128] = "", path[256], expected[160];
  if (line_end <= 4 || memcmp(request, "GET ", 4))
    return 0;
  while (end < line_end && request[end] != ' ')
    end++;
  if (end + 9 != line_end || (memcmp(request + end, " HTTP/1.1", 9) &&
                              memcmp(request + end, " HTTP/1.0", 9)))
    return 0;
  if (end - start >= 7 && !memcmp(request + start, "http://", 7)) {
    start += 7;
    size_t authority = start;
    while (start < end && request[start] != '/')
      start++;
    if (start == end ||
        !hdis_host(request + authority, start - authority, absolute_host))
      return 0;
  }
  if (start >= end || request[start] != '/')
    return 0;
  path_end = start;
  while (path_end < end && request[path_end] != '?')
    path_end++;
  if (path_end - start >= sizeof(path))
    return 0;
  memcpy(path, request + start, path_end - start);
  path[path_end - start] = 0;
  if (path_end > start + 1 && path[path_end - start - 1] == '/')
    path[path_end - start - 1] = 0;
  at = line_end + 2;
  while (at + 2 < total) {
    size_t e = at, colon = at, value, last;
    while (e + 1 < total && (request[e] != '\r' || request[e + 1] != '\n'))
      e++;
    if (e == at)
      break;
    if (e + 1 >= total || request[at] == ' ' || request[at] == '\t')
      return 0;
    while (colon < e && request[colon] != ':')
      colon++;
    if (colon == e)
      return 0;
    if (colon - at == 4 && tolower(request[at]) == 'h' &&
        tolower(request[at + 1]) == 'o' && tolower(request[at + 2]) == 's' &&
        tolower(request[at + 3]) == 't') {
      if (host[0])
        return 0;
      value = colon + 1;
      while (value < e && (request[value] == ' ' || request[value] == '\t'))
        value++;
      last = e;
      while (last > value &&
             (request[last - 1] == ' ' || request[last - 1] == '\t'))
        last--;
      if (!hdis_host(request + value, last - value, host))
        return 0;
    }
    at = e + 2;
  }
  if (host[0] && absolute_host[0] && strcmp(host, absolute_host))
    return 0;
  if (!host[0] && absolute_host[0])
    memcpy(host, absolute_host, sizeof(host));
  if (!strcmp(path, "/fwlink"))
    return !host[0] || !strcmp(host, "go.microsoft.com") ? 7 : 0;
  if (host[0] && !hdis_art_host(host))
    return 0;
  (void)snprintf(expected, sizeof(expected), "/v3.0/en-US/music/artist/%s",
                 s->guid);
  size_t prefix = strlen(expected);
  if (!strncmp(path, expected, prefix)) {
    const char *tail = path + prefix;
    if (!*tail)
      return 4;
    if (!strcmp(tail, "/images"))
      return 5;
    if (!strcmp(tail, "/biography"))
      return 6;
    if (!strcmp(tail, "/deviceBackgroundImage")) {
      *asset_index = s->background_index;
      return hdis_image_query(s, *asset_index, request + path_end,
                              end - path_end)
                 ? 1
                 : 0;
    }
    if (!strcmp(tail, "/primaryImage")) {
      *asset_index = s->primary_index;
      return hdis_image_query(s, *asset_index, request + path_end,
                              end - path_end)
                 ? 2
                 : 0;
    }
  }
  for (size_t i = 0; i < s->asset_count; i++) {
    (void)snprintf(expected, sizeof(expected), "/v3.0/en-US/image/%s",
                   s->assets[i].guid);
    if (!strcmp(path, expected)) {
      *asset_index = i;
      return hdis_image_query(s, i, request + path_end, end - path_end) ? 3 : 0;
    }
    (void)snprintf(expected, sizeof(expected), "/1/%s/504/image.jpg",
                   s->assets[i].guid);
    if (!strcmp(path, expected)) {
      *asset_index = i;
      return hdis_image_query(s, i, request + path_end, end - path_end) ? 3 : 0;
    }
  }
  return 0;
}
static int hdis_tcp(HdImageServer *s, HdImageFlow *f, uint8_t flags,
                    const uint8_t *data, size_t n, int track) {
  uint8_t p[HDIS_MSS + 40];
  size_t total = 40 + n;
  if (n > HDIS_MSS)
    return -1;
  hdis_ip(s, p, total, f->peer_ip, 6);
  memset(p + 20, 0, 20);
  hdis_w16(p + 20, f->local_port);
  hdis_w16(p + 22, f->peer_port);
  hdis_w32(p + 24, f->send_next);
  hdis_w32(p + 28, f->receive_next);
  p[32] = 0x50;
  p[33] = flags;
  hdis_w16(p + 34, HDIS_REQUEST);
  if (n)
    memcpy(p + 40, data, n);
  hdis_w16(p + 36, hdis_transport_sum(p, p + 20, total - 20, 6));
  if (track) {
    memcpy(f->pending_packet, p, total);
    f->pending_n = total;
    f->pending = 1;
    f->send_next += (uint32_t)n + !!(flags & 2) + !!(flags & 1);
    f->pending_end = f->send_next;
    f->last_send = s->now;
    f->retries = 0;
  }
  return s->emit(s->opaque, p, total) < 0 ? -1 : 0;
}
/* One uncompressed DNS question, bounded labels, IN A. No forwarding. */
static size_t hdis_dns(HdImageServer *s, const uint8_t *q, size_t n, uint8_t *r,
                       size_t cap) {
  char name[256];
  size_t at = 12, k = 0, end;
  int match;
  if (n < 17 || n > 512 || (hdis_u16(q + 2) & 0xf800) || hdis_u16(q + 4) != 1 ||
      hdis_u16(q + 6) || hdis_u16(q + 8) || hdis_u16(q + 10))
    return 0;
  while (at < n && q[at]) {
    size_t len = q[at++];
    if (len > 63 || at + len > n || k + len + 1 >= sizeof(name))
      return 0;
    if (k)
      name[k++] = '.';
    while (len--) {
      unsigned char c = q[at++];
      if (!(isalnum(c) || c == '-' || c == '_'))
        return 0;
      name[k++] = (char)tolower(c);
    }
  }
  if (at >= n || at + 5 != n)
    return 0;
  name[k] = 0;
  end = at + 5;
  if (hdis_u16(q + at + 1) != 1 || hdis_u16(q + at + 3) != 1)
    return 0;
  memcpy(s->last_dns, name, k + 1);
  match = !strcmp(name, s->dns_hostname) || hdis_art_host(name) ||
          !strcmp(name, "go.microsoft.com");
  if (end + (match ? 16 : 0) > cap)
    return 0;
  memcpy(r, q, end);
  hdis_w16(r + 2,
           (uint16_t)(0x8080 | (hdis_u16(q + 2) & 0x0100) | (match ? 0 : 3)));
  hdis_w16(r + 6, (uint16_t)match);
  s->dns_queries++;
  if (match) {
    uint8_t *a = r + end;
    hdis_w16(a, 0xc00c);
    hdis_w16(a + 2, 1);
    hdis_w16(a + 4, 1);
    hdis_w32(a + 6, 60);
    hdis_w16(a + 10, 4);
    hdis_w32(a + 12, s->server_ip);
    end += 16;
  }
  return end;
}
static int hdis_send_response(HdImageServer *s, HdImageFlow *f) {
  uint8_t payload[HDIS_MSS];
  size_t n, total = f->header_n + f->body_n, i;
  if (!f->response || f->pending)
    return 0;
  if (f->sent < total) {
    if (!f->window)
      return 0;
    n = total - f->sent;
    if (n > f->mss)
      n = f->mss;
    if (n > f->window)
      n = f->window;
    for (i = 0; i < n; i++) {
      size_t at = f->sent + i;
      payload[i] = at < f->header_n ? f->header[at] : f->body[at - f->header_n];
    }
    f->sent += n;
    return hdis_tcp(s, f, 0x18, payload, n, 1);
  }
  if (f->response == 2) { /* DNS remains open for another framed query. */
    f->response = 0;
    f->request_n = 0;
    f->sent = 0;
    return 0;
  }
  if (!f->fin_sent) {
    f->fin_sent = 1;
    return hdis_tcp(s, f, 0x11, NULL, 0, 1);
  }
  return 0;
}
static int hdis_request(HdImageServer *s, HdImageFlow *f) {
  size_t i, line_end = 0, head_end = 0;
  int route = 0, matched = 0, fwlink_diagnostic = 0;
  const char *type = "text/plain";
  static const uint8_t fwlink_diagnostic_body[] =
      "<html><body>OK</body></html>";
  if (f->response)
    return 0;
  if (f->request_n >= 8 && hdis_u16(f->request + 2) == 53 &&
      hdis_u16(f->request + 6) == 0) {
    size_t total = hdis_u16(f->request + 4), response;
    if (total < 20 || total > 520)
      return -2;
    if (f->request_n < total)
      return 0;
    if (f->request_n != total)
      return -2;
    response = hdis_dns(s, f->request + 8, total - 8, f->header + 8,
                        sizeof(f->header) - 8);
    if (!response)
      return -2;
    hdis_w16(f->header, 53);
    hdis_w16(f->header + 2, hdis_u16(f->request));
    hdis_w16(f->header + 4, (uint16_t)(response + 8));
    hdis_w16(f->header + 6, 0);
    f->header_n = response + 8;
    f->body_n = 0;
    f->sent = 0;
    f->response = 2;
    return hdis_send_response(s, f);
  }
  for (i = 0; i + 1 < f->request_n; i++)
    if (f->request[i] == '\r' && f->request[i + 1] == '\n') {
      line_end = i;
      break;
    }
  for (i = 0; i + 3 < f->request_n; i++)
    if (!memcmp(f->request + i, "\r\n\r\n", 4)) {
      head_end = i + 4;
      break;
    }
  if (!head_end)
    return f->request_n == HDIS_REQUEST ? -2 : 0;
  if (!line_end || head_end != f->request_n)
    return -2;
  for (i = 0; i < head_end; i++)
    if (f->request[i] == 0 || f->request[i] > 127)
      return -2;
  route =
      hdis_catalog_route(s, f->request, line_end, head_end, &f->asset_index);
  matched = route >= 1 && route <= 3;
  fwlink_diagnostic = route == 7;
  s->requests++;
  if (fwlink_diagnostic)
    s->fwlink_diagnostic_responses++;
  {
    size_t copy = line_end < sizeof(s->last_request) - 1
                      ? line_end
                      : sizeof(s->last_request) - 1;
    for (size_t j = 0; j < copy; j++)
      s->last_request[j] = (f->request[j] >= 32 && f->request[j] <= 126)
                               ? (char)f->request[j]
                               : '?';
    s->last_request[copy] = 0;
  }
  f->image = matched;
  f->background = route == 1;
  f->primary = route == 2;
  f->biography = route == 6;
  if (f->biography) s->biography_requests++;
  f->body = NULL;
  f->body_n = 0;
  if (matched) {
    f->body = s->assets[f->asset_index].jpeg;
    f->body_n = s->assets[f->asset_index].jpeg_n;
    s->asset_requests[f->asset_index]++;
    type = "image/jpeg";
  } else if (fwlink_diagnostic) {
    f->body = fwlink_diagnostic_body;
    f->body_n = sizeof(fwlink_diagnostic_body) - 1;
    type = "text/html";
  } else if (route >= 4 && route <= 6) {
    f->body = (const uint8_t *)(route == 4   ? s->overview
                                : route == 5 ? s->images
                                             : s->biography);
    f->body_n = strlen((const char *)f->body);
    type = "application/atom+xml; charset=utf-8";
  }
  f->header_n =
      (size_t)snprintf((char *)f->header, sizeof(f->header),
                       "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: "
                       "%zu\r\nConnection: close\r\n\r\n",
                       route ? "200 OK" : "404 Not Found", type, f->body_n);
  f->response = 1;
  return hdis_send_response(s, f);
}
static int hd_image_server_init(HdImageServer *s, uint32_t ip, const char *guid,
                                const uint8_t *jpeg, size_t n, HdImageEmit emit,
                                void *opaque) {
  size_t i;
  if (!s || !guid || strlen(guid) != 36 || !jpeg || n < 4 ||
      n > HDIS_JPEG_MAX || !emit || jpeg[0] != 255 || jpeg[1] != 216 ||
      jpeg[n - 2] != 255 || jpeg[n - 1] != 217)
    return -1;
  for (i = 0; i < 36; i++)
    if ((i == 8 || i == 13 || i == 18 || i == 23)
            ? guid[i] != '-'
            : !isxdigit((unsigned char)guid[i]))
      return -1;
  memset(s, 0, sizeof(*s));
  s->server_ip = ip;
  s->seed = 0x51a00000;
  s->proxy_port = 49949;
  memcpy(s->guid, guid, 37);
  for (i = 0; i < 36; i++)
    s->guid[i] = (char)tolower((unsigned char)s->guid[i]);
  strcpy(s->dns_hostname, "catalog.zune.net");
  s->jpeg = jpeg;
  s->jpeg_n = n;
  s->asset_count = 1;
  s->gallery_count = 1;
  s->assets[0].jpeg = jpeg;
  s->assets[0].jpeg_n = n;
  memcpy(s->assets[0].guid, s->guid, 37);
  {
    unsigned char c = (unsigned char)s->guid[35];
    unsigned nibble = c <= '9' ? c - '0' : c - 'a' + 10;
    s->assets[0].guid[35] = "0123456789abcdef"[nibble ^ 1];
  }
  hdis_jpeg_dimensions(jpeg, n, &s->assets[0].width, &s->assets[0].height);
  s->emit = emit;
  s->opaque = opaque;
  return hd_image_server_set_artist(s, NULL);
}
/* Returns 0 for accepted/ignored input, -1 only for output failure. */
static int hd_image_server_packet(HdImageServer *s, const uint8_t *p,
                                  size_t n) {
  s->frozen = 1;
  size_t ih, th, len, i;
  const uint8_t *t;
  uint32_t source, seq, ack;
  uint16_t sport, dport;
  uint8_t flags;
  HdImageFlow *f = NULL;
  if (!s || !p || n < 20 || (p[0] >> 4) != 4)
    return 0;
  ih = (p[0] & 15) * 4u;
  if (ih < 20 || ih > n || hdis_u16(p + 2) != n || (hdis_u16(p + 6) & 0xbfff) ||
      hdis_checksum(p, ih) || hdis_u32(p + 16) != s->server_ip) {
    s->rejected++;
    return 0;
  }
  source = hdis_u32(p + 12);
  t = p + ih;
  len = n - ih;
  if (p[9] == 17) {
    uint8_t reply[560];
    size_t rn;
    if (len < 8 || hdis_u16(t + 2) != 53 || hdis_u16(t + 4) != len ||
        (hdis_u16(t + 6) && hdis_transport_sum(p, t, len, 17)))
      return 0;
    rn = hdis_dns(s, t + 8, len - 8, reply + 28, sizeof(reply) - 28);
    if (!rn)
      return 0;
    hdis_ip(s, reply, rn + 28, source, 17);
    hdis_w16(reply + 20, 53);
    hdis_w16(reply + 22, hdis_u16(t));
    hdis_w16(reply + 24, (uint16_t)(rn + 8));
    hdis_w16(reply + 26, 0);
    {
      uint16_t c = hdis_transport_sum(reply, reply + 20, rn + 8, 17);
      hdis_w16(reply + 26, c ? c : 65535);
    }
    return s->emit(s->opaque, reply, rn + 28) < 0 ? -1 : 0;
  }
  if (p[9] != 6 || len < 20 || hdis_transport_sum(p, t, len, 6)) {
    s->rejected++;
    return 0;
  }
  th = (t[12] >> 4) * 4u;
  if (th < 20 || th > len)
    return 0;
  sport = hdis_u16(t);
  dport = hdis_u16(t + 2);
  seq = hdis_u32(t + 4);
  ack = hdis_u32(t + 8);
  flags = t[13];
  if (dport != 80 && dport != 49950 && dport != 49234 && dport != s->proxy_port)
    return 0;
  for (i = 0; i < HDIS_FLOWS; i++)
    if (s->flow[i].active && s->flow[i].peer_ip == source &&
        s->flow[i].peer_port == sport && s->flow[i].local_port == dport) {
      f = &s->flow[i];
      break;
    }
  if (flags == 2 && len == th) {
    if (f) {
      if (seq + 1 == f->receive_next && !f->established && f->pending)
        return s->emit(s->opaque, f->pending_packet, f->pending_n) < 0 ? -1 : 0;
      return 0;
    }
    for (i = 0; i < HDIS_FLOWS; i++)
      if (!s->flow[i].active) {
        f = &s->flow[i];
        break;
      }
    if (!f) {
      /* Keep existing transfers intact. A repeated SYN may retry once a slot
       * becomes available; record every refusal instead of silently hiding it. */
      s->capacity_refused++;
      return 0;
    }
    memset(f, 0, sizeof(*f));
    f->active = 1;
    {
      unsigned active = 0;
      for (size_t j = 0; j < HDIS_FLOWS; j++) active += !!s->flow[j].active;
      if (active > s->peak_flows) s->peak_flows = active;
    }
    f->peer_ip = source;
    f->peer_port = sport;
    f->local_port = dport;
    f->receive_next = seq + 1;
    f->send_next = (s->seed += 0x10000);
    f->mss = HDIS_MSS;
    f->window = hdis_u16(t + 14);
    for (i = 20; i < th;) {
      uint8_t opt = t[i], sz;
      if (!opt)
        break;
      if (opt == 1) {
        i++;
        continue;
      }
      if (i + 2 > th || (sz = t[i + 1]) < 2 || i + sz > th) {
        f->active = 0;
        return 0;
      }
      if (opt == 2 && sz == 4) {
        uint16_t m = hdis_u16(t + i + 2);
        if (!m) {
          f->active = 0;
          return 0;
        }
        if (m < f->mss)
          f->mss = m;
      }
      i += sz;
    }
    return hdis_tcp(s, f, 0x12, NULL, 0, 1);
  }
  if (!f)
    return 0;
  if (flags & 4) {
    if (seq == f->receive_next)
      f->active = 0;
    return 0;
  }
  if (!(flags & 16) || (flags & (2 | 32 | 64 | 128)))
    return 0;
  /* Reject future/partial ACKs. Exact previous ACK is a valid duplicate. */
  if (ack != f->send_next &&
      (!f->pending || ack != hdis_u32(f->pending_packet + 24)))
    return 0;
  if (seq != f->receive_next)
    return hdis_tcp(s, f, 0x10, NULL, 0, 0);
  f->window = hdis_u16(t + 14);
  if (f->pending && ack == f->pending_end) {
    f->pending = 0;
    f->established = 1;
    if (f->fin_sent) {
      if (f->image) {
        size_t index = f->asset_index;
        s->image_completed++;
        for (size_t j = 0; j < s->gallery_count; j++)
          if (s->gallery_indices[j] == index) {
            s->gallery_completed++;
            if (!s->asset_completed[index]) s->distinct_gallery_completed++;
          }
        if (!s->asset_completed[index]) {
          int seen = 0;
          s->distinct_assets_completed++;
          for (size_t j = 0; j < s->asset_count; j++)
            if (s->asset_completed[j] &&
                s->assets[j].jpeg_n == s->assets[index].jpeg_n &&
                !memcmp(s->assets[j].jpeg, s->assets[index].jpeg,
                        s->assets[index].jpeg_n))
              seen = 1;
          if (!seen)
            s->distinct_contents_completed++;
        }
        s->asset_completed[index]++;
      }
      if (f->primary)
        s->primary_completed++;
      if (f->biography)
        s->biography_completed++;
      if (f->background)
        s->background_completed++;
      f->active = 0;
      return 0;
    }
  }
  if (!f->established)
    return 0;
  if (f->response == 2 && !f->pending && f->sent == f->header_n) {
    f->response = 0;
    f->request_n = 0;
    f->sent = 0;
  }
  if (len > th) {
    size_t payload = len - th;
    int result;
    if (f->response || payload > sizeof(f->request) - f->request_n) {
      f->active = 0;
      s->rejected++;
      return 0;
    }
    memcpy(f->request + f->request_n, t + th, payload);
    f->request_n += payload;
    f->receive_next += (uint32_t)payload;
    if (hdis_tcp(s, f, 0x10, NULL, 0, 0) < 0)
      return -1;
    result = hdis_request(s, f);
    if (result == -2) {
      f->active = 0;
      s->rejected++;
      return 0;
    }
    if (result < 0)
      return -1;
  }
  if (flags & 1) {
    f->receive_next++;
    if (hdis_tcp(s, f, 0x10, NULL, 0, 0) < 0)
      return -1;
    if (!f->response && !f->fin_sent) {
      f->fin_sent = 1;
      return hdis_tcp(s, f, 0x11, NULL, 0, 1);
    }
  }
  return hdis_send_response(s, f);
}
static int hd_image_server_tick(HdImageServer *s, uint64_t now) {
  size_t i;
  if (now < s->now)
    return -1;
  s->now = now;
  for (i = 0; i < HDIS_FLOWS; i++) {
    HdImageFlow *f = &s->flow[i];
    if (f->active && f->pending && now - f->last_send >= 1000) {
      if (f->retries >= 5) {
        f->active = 0;
        s->timed_out++;
        continue;
      }
      f->retries++;
      f->last_send = now;
      if (s->emit(s->opaque, f->pending_packet, f->pending_n) < 0)
        return -1;
    }
  }
  return 0;
}
#endif
