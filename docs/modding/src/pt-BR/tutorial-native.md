# Tutorial: um tipo de arquivo parecido com QOI (plugin nativo)

Um plugin em C que abre e salva arquivos `.qlike` (sequências de pixels RGBA) e adiciona um pequeno efeito **Swap Red/Blue**.
O mod pronto está em `mods/examples/qoi-like`; a compilação do próprio David também o compila.

## A ABI C

Os plugins nativos conversam com o David por um único header C, `sdk/david_plugin.h`. É C puro, então um plugin pode ser compilado com qualquer compilador (MSVC, GCC, Clang) e qualquer linguagem que consiga exportar uma função C.

O plugin exporta uma função:

```c
DAVID_PLUGIN_EXPORT int david_plugin_init(const david_host* host);
```

`host` tem a versão da API, um alocador, `register_effect`, `register_format` e `log`. Retorne 0 quando tudo foi registrado.

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

`entry` é o nome da biblioteca sem a extensão: o David carrega `qlike.dll` no Windows e `libqlike.so` ou `qlike.so` no Linux.

## 2. O formato

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

Os pixels são `uint32_t`, RGBA direto (não pré-multiplicado) com o vermelho no byte baixo, linhas de cima para baixo. **A memória que você entrega ao David deve vir de `host->alloc`**: o plugin e o David podem usar runtimes C diferentes, então o David a libera com o seu próprio.

## 3. Registrando

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

As structs podem ficar na pilha: o David as copia. Os parâmetros do efeito são um array JSON, com os mesmos campos do Lua (`id`, `label`, `type`, `min`, `max`, `default`, `choices`); `run` recebe os valores como um objeto JSON.

## 4. Compilando

O exemplo tem o próprio `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(qoi_like C)
set(DAVID_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk" CACHE PATH "Folder with david_plugin.h")
add_library(qlike SHARED qoi_like.c)
target_include_directories(qlike PRIVATE ${DAVID_SDK})
set_target_properties(qlike PROPERTIES PREFIX "")
```

No Windows (um prompt de desenvolvedor do Visual Studio) e no Linux é igual:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Copie `qlike.dll` (ou `qlike.so`) para junto do `mod.json`, reinicie o David e ative o mod. **Arquivo > Salvar como** agora oferece `.qlike`, e `david-cli in.png -o out.qlike` também funciona.

## Segurança

Um plugin nativo roda com todos os direitos do David: instale apenas plugins em que você confia. Os mods Lua e de shader rodam em sandbox; os nativos não.
