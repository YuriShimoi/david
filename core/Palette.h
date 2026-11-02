#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace david {

class Document;

// An ordered list of opaque colors. The order is the ramp the shading ink steps along.
struct Palette {
  std::vector<uint32_t> colors;

  bool empty() const { return colors.empty(); }
  int size() const { return int(colors.size()); }
  // Index of the exact RGB (alpha ignored), -1 when it isn't in the palette
  int find(uint32_t c) const;
  // Index of the closest color in OKLab (or RGB), -1 when empty
  int nearest(uint32_t c, bool rgb = false) const;

  // GIMP .gpl, JASC .pal or .hex (one RRGGBB per line), picked by extension, sniffed when unknown
  bool load(const std::string& path, std::string& error);
  bool save(const std::string& path, std::string& error) const;
  bool parse(const std::string& text, std::string& error);

  void sortByHue();
  void sortByLightness();
  // Drops repeated colors, keeping the first
  void unique();

  static Palette defaults();
  // Up to n colors from the image (or a layer) by median cut in OKLab, weighted by how often each color occurs
  static Palette fromPixels(const std::vector<uint32_t>& px, int n);
  static Palette fromImage(const Document& doc, int n);
};

}
