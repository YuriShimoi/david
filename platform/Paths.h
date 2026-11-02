#pragma once
#include <string>

namespace david::paths {

// Folder that holds david(.exe), themes/ and assets/, with a trailing slash
std::string exeDir();
// %APPDATA%\David\ on Windows, $XDG_CONFIG_HOME/david/ (or ~/.config/david/) on Linux; created if missing
std::string configDir();
// Points configDir() somewhere else (dev self-tests use a temp folder)
void overrideConfigDir(const std::string& dir);
// The system UI font file (Segoe UI / Noto Sans), or empty when none is found
std::string uiFont(bool bold);

}
