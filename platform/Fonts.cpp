#include "platform/Fonts.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

#ifdef _WIN32
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#pragma comment(lib, "dwrite.lib")
#else
#include <fontconfig/fontconfig.h>
#include <filesystem>
#endif

namespace david {

static std::string lower(std::string s) {
  for(char& c : s) c = char(std::tolower((unsigned char)c));
  return s;
}

#ifdef _WIN32
using Microsoft::WRL::ComPtr;

static std::string narrow(const wchar_t* w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? n - 1 : 0, '\0');
  if(n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

// The English name when there is one, else the first
static std::string localized(IDWriteLocalizedStrings* names) {
  UINT32 index = 0;
  BOOL found = FALSE;
  names->FindLocaleName(L"en-us", &index, &found);
  if(!found) index = 0;
  UINT32 len = 0;
  if(FAILED(names->GetStringLength(index, &len))) return {};
  std::wstring w(len + 1, L'\0');
  if(FAILED(names->GetString(index, w.data(), len + 1))) return {};
  return narrow(w.c_str());
}

std::vector<FontFamily> systemFonts() {
  std::vector<FontFamily> out;
  ComPtr<IDWriteFactory> factory;
  if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) return out;
  ComPtr<IDWriteFontCollection> fonts;
  if(FAILED(factory->GetSystemFontCollection(&fonts, FALSE))) return out;
  for(UINT32 i = 0; i < fonts->GetFontFamilyCount(); i++) {
    ComPtr<IDWriteFontFamily> family;
    ComPtr<IDWriteLocalizedStrings> names;
    if(FAILED(fonts->GetFontFamily(i, &family)) || FAILED(family->GetFamilyNames(&names))) continue;
    FontFamily fam{ localized(names.Get()), {} };
    for(UINT32 j = 0; j < family->GetFontCount(); j++) {
      ComPtr<IDWriteFont> font;
      if(FAILED(family->GetFont(j, &font)) || font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE) continue;
      ComPtr<IDWriteLocalizedStrings> faceNames;
      ComPtr<IDWriteFontFace> face;
      if(FAILED(font->GetFaceNames(&faceNames)) || FAILED(font->CreateFontFace(&face))) continue;
      UINT32 files = 1;
      ComPtr<IDWriteFontFile> file;
      if(FAILED(face->GetFiles(&files, &file)) || !file) continue;
      const void* key = nullptr;
      UINT32 keySize = 0;
      ComPtr<IDWriteFontFileLoader> loader;
      ComPtr<IDWriteLocalFontFileLoader> local;
      if(FAILED(file->GetReferenceKey(&key, &keySize)) || FAILED(file->GetLoader(&loader)) || FAILED(loader.As(&local))) continue;
      UINT32 len = 0;
      if(FAILED(local->GetFilePathLengthFromKey(key, keySize, &len))) continue;
      std::wstring path(len + 1, L'\0');
      if(FAILED(local->GetFilePathFromKey(key, keySize, path.data(), len + 1))) continue;
      fam.faces.push_back({ localized(faceNames.Get()), narrow(path.c_str()), int(face->GetIndex()), int(font->GetWeight()),
                            font->GetStyle() != DWRITE_FONT_STYLE_NORMAL });
    }
    if(!fam.faces.empty() && !fam.name.empty()) out.push_back(std::move(fam));
  }
  std::sort(out.begin(), out.end(), [](const FontFamily& a, const FontFamily& b) { return lower(a.name) < lower(b.name); });
  return out;
}
#else
std::vector<FontFamily> systemFonts() {
  // The static fontconfig would look for its config under the build's prefix; use the system one
  FcConfig* config = nullptr;
  if(std::filesystem::exists("/etc/fonts/fonts.conf")) {
    config = FcConfigCreate();
    if(!FcConfigParseAndLoad(config, reinterpret_cast<const FcChar8*>("/etc/fonts/fonts.conf"), FcTrue) || !FcConfigBuildFonts(config)) {
      FcConfigDestroy(config);
      config = nullptr;
    }
  }
  if(!config) config = FcInitLoadConfigAndFonts();
  std::map<std::string, FontFamily> byName;
  FcPattern* pattern = FcPatternCreate();
  FcObjectSet* set = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_FILE, FC_INDEX, FC_WEIGHT, FC_SLANT, FC_OUTLINE, (char*)nullptr);
  if(FcFontSet* list = FcFontList(config, pattern, set)) {
    for(int i = 0; i < list->nfont; i++) {
      FcPattern* p = list->fonts[i];
      FcChar8 *family = nullptr, *style = nullptr, *file = nullptr;
      int index = 0, weight = FC_WEIGHT_REGULAR, slant = FC_SLANT_ROMAN;
      FcBool outline = FcTrue;
      FcPatternGetBool(p, FC_OUTLINE, 0, &outline);
      if(!outline || FcPatternGetString(p, FC_FAMILY, 0, &family) != FcResultMatch || FcPatternGetString(p, FC_FILE, 0, &file) != FcResultMatch) continue;
      FcPatternGetString(p, FC_STYLE, 0, &style);
      FcPatternGetInteger(p, FC_INDEX, 0, &index);
      FcPatternGetInteger(p, FC_WEIGHT, 0, &weight);
      FcPatternGetInteger(p, FC_SLANT, 0, &slant);
      std::string name = reinterpret_cast<const char*>(family);
      FontFamily& fam = byName[lower(name)];
      if(fam.name.empty()) fam.name = name;
      fam.faces.push_back({ style ? reinterpret_cast<const char*>(style) : "Regular", reinterpret_cast<const char*>(file), index,
                            FcWeightToOpenType(weight), slant != FC_SLANT_ROMAN });
    }
    FcFontSetDestroy(list);
  }
  FcObjectSetDestroy(set);
  FcPatternDestroy(pattern);
  FcConfigDestroy(config);
  std::vector<FontFamily> out;
  for(auto& [key, fam] : byName) out.push_back(std::move(fam));
  return out;
}
#endif

const FontFace* pickFace(const FontFamily& family, bool bold, bool italic) {
  const FontFace* best = nullptr;
  int bestScore = 1 << 30, want = bold ? 700 : 400;
  for(auto& f : family.faces) {
    int score = std::abs(f.weight - want) + (f.italic != italic ? 1000 : 0);
    if(score < bestScore) { best = &f; bestScore = score; }
  }
  return best;
}

int fontMatchScore(const std::string& name, const std::string& query) {
  if(query.empty()) return 1;
  std::string n = lower(name), q = lower(query);
  size_t at = n.find(q);
  if(at == std::string::npos) {
    // Fuzzy: the query's letters in order (skipping spaces), scored low
    size_t k = 0;
    for(char c : n) if(k < q.size() && c == q[k]) k++;
    return k == q.size() ? 10 : 0;
  }
  int score = 100;
  if(at == 0) score += 300;
  else if(n[at - 1] == ' ' || n[at - 1] == '-') score += 150;
  // Shorter names win among equal matches
  return score + std::max(0, 60 - int(n.size()));
}

}
