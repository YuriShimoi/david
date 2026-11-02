# Tutorial: a QOI-like file type (native plugin)

A plugin in C that opens and saves `.qlike` files (runs of RGBA pixels), and adds a small **Swap Red/Blue** effect.
The finished mod is `mods/examples/qoi-like`; David's own build compiles it too.

## The C ABI

Native plugins talk to David through one C header, `sdk/david_plugin.h`. It is plain C, so a plugin can be built with any compiler (MSVC, GCC, Clang) and any language that can export a C function.

The plugin exports one function:

```c
DAVID_PLUGIN_EXPORT int david_plugin_init(const david_host* host);
```

`host` has the API version, an allocator, `register_effect`, `register_format` and `log`. Return 0 when everything registered.

## 1. mod.json

```json
{
  "name": "QOI-like File Type",
  "version": "1.0.0",
  "api": 1,
  "kind": "native",
  "entry": "qlike"
}
```

`entry` is the library name without its extension: David loads `qlike.dll` on Windows and `libqlike.so` or `qlike.so` on Linux.

## 2. The format

```c
static const david_host* s_host;

static int load(void* user, const uint8_t* bytes, size_t size, david_image* out) {
  if(size < 12 || memcmp(bytes, "QLIK", 4)) return 1;
  /* read width and height, then: */
  out->pixels = (uint32_t*)s_host->alloc((size_t)w * h * 4);
  /* fill out->pixels, set out->width and out->height */
  return 0;
}

static int save(void* user, const david_image* img, uint8_t** bytes, size_t* size) {
  uint8_t* o = (uint8_t*)s_host->alloc(12 + (size_t)img->width * img->height * 5);
  /* write the file into o */
  *bytes = o;
  *size = written;
  return 0;
}
```

Pixels are `uint32_t`, straight RGBA with red in the low byte, rows top first. **Memory you hand to David must come from `host->alloc`**: the plugin and David may use different C runtimes, so David frees it with its own.

## 3. Registering

```c
DAVID_PLUGIN_EXPORT int david_plugin_init(const david_host* host) {
  if(host->api_version < DAVID_API_VERSION) return 1;
  s_host = host;
  david_format format = { "qlike", "QOI-like", load, save, NULL };
  david_effect effect = { "swap-red-blue", "Swap Red/Blue", "Effects/Mods", "[]", swapRedBlue, NULL };
  if(host->register_format(&format)) return 2;
  if(host->register_effect(&effect)) return 3;
  return 0;
}
```

The structs can live on the stack: David copies them. Effect params are a JSON array, the same fields as Lua's (`id`, `label`, `type`, `min`, `max`, `default`, `choices`); `run` gets the values as a JSON object.

## 4. Building

The example has its own `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(qoi_like C)
set(DAVID_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk" CACHE PATH "Folder with david_plugin.h")
add_library(qlike SHARED qoi_like.c)
target_include_directories(qlike PRIVATE ${DAVID_SDK})
set_target_properties(qlike PROPERTIES PREFIX "")
```

Windows (a Visual Studio developer prompt) and Linux are the same:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Copy `qlike.dll` (or `qlike.so`) next to `mod.json`, restart David and turn the mod on. **File > Save As** now offers `.qlike`, and `david-cli in.png -o out.qlike` works too.

## Safety

A native plugin runs with David's full rights: only install plugins you trust. Lua and shader mods are sandboxed; native ones can't be.
