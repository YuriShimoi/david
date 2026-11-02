#pragma once
#include <cstdint>

namespace david::platform {

// The system accent color as packed RGBA (Windows: the DWM accent). False where there is none (Linux).
bool systemAccent(uint32_t& rgba);
bool hasSystemAccent();

}
