#include "core/Text.h"
#include "core/Blend.h"
#include "core/Composite.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace david {

static const char* NAMES[] = { N_("Normal"), N_("Multiply"), N_("Additive"), N_("Color Burn"), N_("Color Dodge"), N_("Reflect"), N_("Glow"), N_("Overlay"),
                               N_("Difference"), N_("Negation"), N_("Lighten"), N_("Darken"), N_("Screen"), N_("Xor") };
static const char* IDS[] = { "normal", "multiply", "additive", "color-burn", "color-dodge", "reflect", "glow", "overlay",
                             "difference", "negation", "lighten", "darken", "screen", "xor" };

const char* blendName(EBlend m) { return NAMES[std::min(int(m), int(EBlend::COUNT) - 1)]; }
const char* blendId(EBlend m) { return IDS[std::min(int(m), int(EBlend::COUNT) - 1)]; }

EBlend blendFromId(const char* id) {
  for(int i = 0; i < int(EBlend::COUNT); i++) if(!std::strcmp(id, IDS[i])) return EBlend(i);
  return EBlend::Normal;
}

int blendChannel(EBlend mode, int b, int s) {
  switch(mode) {
    case EBlend::Multiply: return (b * s + 127) / 255;
    case EBlend::Additive: return std::min(255, b + s);
    case EBlend::ColorBurn: return s == 0 ? 0 : std::max(0, 255 - (255 - b) * 255 / s);
    case EBlend::ColorDodge: return s == 255 ? 255 : std::min(255, b * 255 / (255 - s));
    case EBlend::Reflect: return s == 255 ? 255 : std::min(255, b * b / (255 - s));
    case EBlend::Glow: return b == 255 ? 255 : std::min(255, s * s / (255 - b));
    case EBlend::Overlay: return b < 128 ? (2 * b * s + 127) / 255 : 255 - (2 * (255 - b) * (255 - s) + 127) / 255;
    case EBlend::Difference: return std::abs(b - s);
    case EBlend::Negation: return 255 - std::abs(255 - b - s);
    case EBlend::Lighten: return std::max(b, s);
    case EBlend::Darken: return std::min(b, s);
    case EBlend::Screen: return 255 - ((255 - b) * (255 - s) + 127) / 255;
    case EBlend::Xor: return b ^ s;
    default: return s;
  }
}

uint32_t blendPixel(uint32_t dst, uint32_t src, uint8_t opacity, EBlend mode) {
  if(mode == EBlend::Normal) return blendOver(dst, src, opacity);
  uint32_t sa = ((src >> 24) * opacity + 127) / 255;
  if(sa == 0) return dst;
  uint32_t ba = dst >> 24;
  // Weights in 0-255²: source alone, both (blended), backdrop alone
  uint32_t wS = sa * (255 - ba), wF = sa * ba, wB = (255 - sa) * ba;
  uint32_t a255 = wS + wF + wB;  // = outA * 255
  uint32_t out = 0;
  for(int sh = 0; sh < 24; sh += 8) {
    int b = (dst >> sh) & 0xFF, s = (src >> sh) & 0xFF;
    uint32_t c = (s * wS + blendChannel(mode, b, s) * wF + b * wB + a255 / 2) / a255;
    out |= std::min(c, 255u) << sh;
  }
  return out | (((a255 + 127) / 255) << 24);
}

}
