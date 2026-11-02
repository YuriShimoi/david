/* David native plugin API (C ABI), version 1.
   A plugin is a DLL (Windows) or .so (Linux) in mods/<name>/ whose mod.json says "kind": "native" and names the
   library without its extension in "entry". It exports david_plugin_init, which registers effects and file formats.
   Pixels are straight (not premultiplied) RGBA8, one uint32_t each, red in the low byte, rows top first.
   Memory a plugin hands back to David (loaded images, saved bytes) must come from host->alloc. */
#ifndef DAVID_PLUGIN_H
#define DAVID_PLUGIN_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define DAVID_PLUGIN_EXPORT __declspec(dllexport)
#else
#define DAVID_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

//@api c DAVID_API_VERSION
//@sig #define DAVID_API_VERSION 1
//@doc The API version this header describes. david_plugin_init gets the host's version; return 0 to refuse an older host.
#define DAVID_API_VERSION 1

//@api c david_image
//@sig typedef struct { int width, height; uint32_t* pixels; } david_image
//@doc An RGBA8 image: width * height pixels, straight alpha, red in the low byte, rows top first.
//@ex uint32_t c = img->pixels[y * img->width + x]; uint8_t alpha = c >> 24;
typedef struct david_image {
  int width, height;
  uint32_t* pixels;
} david_image;

//@api c david_effect
//@sig typedef struct { id, name, category, params_json, run, user } david_effect
//@doc An effect. params_json is a JSON array of params, each {"id", "label", "type" (float, int, bool, choice, color), "min", "max", "default", "choices"}; David builds the dialog from it, and the CLI and Lua use the same ids. run gets the selection's bounds as src (dst starts as a copy) and the values as a JSON object; it returns 0 on success.
//@ex static int invert(void* user, const david_image* src, david_image* dst, const char* values) { for(int i = 0; i < src->width * src->height; i++) dst->pixels[i] = src->pixels[i] ^ 0x00FFFFFFu; return 0; }
typedef struct david_effect {
  const char* id;           /* "my-blur": stable, used by the CLI (--effect my-blur:radius=3) */
  const char* name;         /* menu label */
  const char* category;     /* "Effects/Blurs", "Adjustments", ... */
  const char* params_json;  /* may be NULL or "[]" */
  int (*run)(void* user, const david_image* src, david_image* dst, const char* values_json);
  void* user;
} david_effect;

//@api c david_format
//@sig typedef struct { ext, name, load, save, user } david_format
//@doc A file format by extension. load decodes bytes into out (out->pixels allocated with host->alloc); save encodes img into *bytes (allocated with host->alloc). Either may be NULL. Both return 0 on success.
//@ex static int load(void* user, const uint8_t* bytes, size_t size, david_image* out) { out->width = 1; out->height = 1; out->pixels = host->alloc(4); out->pixels[0] = 0xFF0000FFu; return 0; }
typedef struct david_format {
  const char* ext;   /* lower case, no dot */
  const char* name;  /* shown in dialogs */
  int (*load)(void* user, const uint8_t* bytes, size_t size, david_image* out);
  int (*save)(void* user, const david_image* img, uint8_t** bytes, size_t* size);
  void* user;
} david_format;

//@api c david_host
//@sig typedef struct { api_version, alloc, free, register_effect, register_format, log } david_host
//@doc What David gives the plugin. The register functions copy what they need, so the structs passed to them may live on the stack; the function pointers and user data must stay valid while the plugin is loaded.
typedef struct david_host {
  int api_version;
  //@api c david_host.alloc
  //@sig void* alloc(size_t bytes)
  //@doc Memory David frees later: loaded pixels and saved bytes.
  void* (*alloc)(size_t bytes);
  //@api c david_host.free
  //@sig void free(void* p)
  //@doc Frees memory from alloc that the plugin didn't hand back.
  void (*free)(void* p);
  //@api c david_host.register_effect
  //@sig int register_effect(const david_effect* effect)
  //@doc Adds an effect to the menus, the CLI and Lua. Returns 0 on success.
  //@ex host->register_effect(&my_effect);
  int (*register_effect)(const david_effect* effect);
  //@api c david_host.register_format
  //@sig int register_format(const david_format* format)
  //@doc Adds a file format to Open, Save As and the CLI. Returns 0 on success.
  //@ex host->register_format(&my_format);
  int (*register_format)(const david_format* format);
  //@api c david_host.log
  //@sig void log(const char* message)
  //@doc Writes a line to the mod's log (Settings > Mods shows the last lines).
  void (*log)(const char* message);
} david_host;

//@api c david_plugin_init
//@sig DAVID_PLUGIN_EXPORT int david_plugin_init(const david_host* host)
//@doc The one function a plugin exports. Register everything here; return 0 on success, anything else to fail loading (the error shows in Settings > Mods).
//@ex DAVID_PLUGIN_EXPORT int david_plugin_init(const david_host* host) { return host->register_effect(&my_effect); }
typedef int (*david_plugin_init_fn)(const david_host* host);

#ifdef __cplusplus
}
#endif
#endif
