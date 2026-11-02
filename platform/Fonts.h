#pragma once
#include <string>
#include <vector>

namespace david {

struct FontFace {
  std::string style;  // "Regular", "Bold Italic", ...
  std::string path;
  int index = 0;      // face inside a .ttc collection
  int weight = 400;   // CSS weight
  bool italic = false;
};

struct FontFamily {
  std::string name;
  std::vector<FontFace> faces;  // regular first
};

// Installed font families from DirectWrite (Windows) or fontconfig (Linux), sorted by name. Slow on a cold
// fontconfig cache, so callers run it off the UI thread.
std::vector<FontFamily> systemFonts();

// The face closest to the wanted weight and slant
const FontFace* pickFace(const FontFamily& family, bool bold, bool italic);

// Case-insensitive substring match scored for ranking: 0 = no match, higher = better (prefix, word start, length)
int fontMatchScore(const std::string& name, const std::string& query);

}
