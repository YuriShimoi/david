#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace david {

// A tileable threshold matrix for ordered dithering; thresholds are in (0, 1)
struct Pattern {
  std::string id, name;
  int w = 0, h = 0;
  std::vector<float> th;
  float at(int x, int y) const { return th[size_t(((y % h) + h) % h) * w + ((x % w) + w) % w]; }
};

// Built-in (bayer2, bayer4, bayer8, bluenoise) plus every loaded pattern
const std::vector<Pattern>& patterns();
const Pattern* findPattern(const std::string& id);

// Pattern from integer values: threshold = (v + 0.5) / (max + 1); max < 0 takes the largest value
Pattern patternFromValues(const std::string& id, int w, int h, const std::vector<int>& values, int max = -1);
// Pattern from an image's brightness: distinct levels are ranked, darkest first (works for any gray range)
Pattern patternFromPixels(const std::string& id, int w, int h, const std::vector<uint32_t>& rgba);

// One file: an Aseprite extension (.aseprite-extension: package.json with contributes.ditheringMatrices),
// a grayscale .png, or a .json / .csv matrix ([[0, 8, 2, 10], ...] or {"matrix": [...], "max": 15})
bool loadPatternFile(const std::string& path, std::vector<Pattern>& out, std::string& error);
// Loads every pattern file in the folders into the list (ids already there are replaced); errors per file
void loadPatternDirs(const std::vector<std::string>& dirs, std::vector<std::string>& errors);
// Adds or replaces one pattern in the list
void addPattern(Pattern p);

}
