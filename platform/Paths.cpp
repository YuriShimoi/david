#include "platform/Paths.h"
#include "core/io/Png.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <filesystem>

namespace david::paths {

std::string exeDir() {
  const char* base = SDL_GetBasePath();
  return base ? base : "./";
}

static std::string s_configDir;

void overrideConfigDir(const std::string& dir) {
  s_configDir = dir;
  if(!s_configDir.empty() && s_configDir.back() != '/' && s_configDir.back() != '\\') s_configDir += '/';
  std::error_code ec;
  std::filesystem::create_directories(pathFromUtf8(s_configDir), ec);
}

std::string configDir() {
  std::string& dir = s_configDir;
  if(!dir.empty()) return dir;
  std::error_code pec;
  if(std::filesystem::exists(pathFromUtf8(exeDir() + "portable"), pec)) dir = exeDir() + "config/";
  else {
#ifdef _WIN32
    if(char* p = SDL_GetPrefPath(nullptr, "David")) { dir = p; SDL_free(p); }
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if(xdg && *xdg) dir = std::string(xdg) + "/david/";
    else if(home) dir = std::string(home) + "/.config/david/";
#endif
  }
  if(dir.empty()) dir = exeDir();
  std::error_code ec;
  std::filesystem::create_directories(pathFromUtf8(dir), ec);
  return dir;
}

std::string uiFont(bool bold) {
#ifdef _WIN32
  const char* win = std::getenv("WINDIR");
  std::string fonts = std::string(win ? win : "C:\\Windows") + "\\Fonts\\";
  const char* candidates[] = { bold ? "segoeuib.ttf" : "segoeui.ttf", bold ? "arialbd.ttf" : "arial.ttf" };
  for(auto c : candidates)
    if(std::filesystem::exists(pathFromUtf8(fonts + c))) return fonts + c;
#else
  const char* candidates[] = {
    bold ? "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf" : "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    bold ? "/usr/share/fonts/noto/NotoSans-Bold.ttf" : "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    bold ? "/usr/share/fonts/google-noto/NotoSans-Bold.ttf" : "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
    bold ? "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" : "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  };
  for(auto c : candidates)
    if(std::filesystem::exists(c)) return c;
#endif
  return "";
}

}
