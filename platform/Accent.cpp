#include "platform/Accent.h"
#ifdef _WIN32
#include <windows.h>
#endif

namespace david::platform {

bool systemAccent(uint32_t& rgba) {
#ifdef _WIN32
  // DWM stores the accent as 0xAABBGGRR, the same byte order David packs colors in
  DWORD value = 0, size = sizeof value;
  if(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
    return false;
  rgba = (value & 0x00FFFFFF) | 0xFF000000;
  return true;
#else
  (void)rgba;
  return false;
#endif
}

bool hasSystemAccent() {
  uint32_t c;
  return systemAccent(c);
}

}
