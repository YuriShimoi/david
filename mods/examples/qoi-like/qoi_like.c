/* QOI-like file type: a native David plugin (the modding guide's fourth tutorial).
   A .qlike file is "QLIK", width and height (32-bit little endian), then runs: a count byte (1-255) and one RGBA pixel.
   It also adds a tiny effect, Swap Red/Blue, to show effects from native code. */
#include "david_plugin.h"
#include <string.h>

static const david_host* s_host;

static void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int load(void* user, const uint8_t* bytes, size_t size, david_image* out) {
  (void)user;
  if(size < 12 || memcmp(bytes, "QLIK", 4)) return 1;
  uint32_t w = get32(bytes + 4), h = get32(bytes + 8);
  if(!w || !h || w > 65535 || h > 65535) return 2;
  size_t n = (size_t)w * h, i = 0, p = 12;
  out->pixels = (uint32_t*)s_host->alloc(n * 4);
  if(!out->pixels) return 3;
  out->width = (int)w;
  out->height = (int)h;
  while(i < n && p + 5 <= size) {
    uint8_t run = bytes[p];
    uint32_t c = get32(bytes + p + 1);
    p += 5;
    for(uint8_t k = 0; k < run && i < n; k++) out->pixels[i++] = c;
  }
  while(i < n) out->pixels[i++] = 0;
  return 0;
}

static int save(void* user, const david_image* img, uint8_t** bytes, size_t* size) {
  (void)user;
  size_t n = (size_t)img->width * img->height;
  /* Worst case: one run per pixel */
  uint8_t* o = (uint8_t*)s_host->alloc(12 + n * 5);
  if(!o) return 1;
  memcpy(o, "QLIK", 4);
  put32(o + 4, (uint32_t)img->width);
  put32(o + 8, (uint32_t)img->height);
  size_t p = 12;
  for(size_t i = 0; i < n;) {
    uint32_t c = img->pixels[i];
    uint8_t run = 0;
    while(i < n && img->pixels[i] == c && run < 255) { run++; i++; }
    o[p] = run;
    put32(o + p + 1, c);
    p += 5;
  }
  *bytes = o;
  *size = p;
  return 0;
}

static int swapRedBlue(void* user, const david_image* src, david_image* dst, const char* values) {
  (void)user; (void)values;
  for(int i = 0; i < src->width * src->height; i++) {
    uint32_t c = src->pixels[i];
    dst->pixels[i] = (c & 0xFF00FF00u) | (c & 0xFF) << 16 | (c >> 16 & 0xFF);
  }
  return 0;
}

DAVID_PLUGIN_EXPORT int david_plugin_init(const david_host* host) {
  if(host->api_version < DAVID_API_VERSION) return 1;
  s_host = host;
  david_format format = { "qlike", "QOI-like", load, save, NULL };
  david_effect effect = { "swap-red-blue", "Swap Red/Blue", "Effects/Mods", "[]", swapRedBlue, NULL };
  if(host->register_format(&format)) return 2;
  if(host->register_effect(&effect)) return 3;
  host->log("qoi-like ready");
  return 0;
}
