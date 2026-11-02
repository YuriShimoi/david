#pragma once
#include "core/Effect.h"

namespace david {

// Full 12 × 4 blob autotile sheet (48 tiles) from a minimal source: two grid × grid tiles side by side,
// the border tile and the floor tile. A C++ port of F-220 (panda's TilesetConversor), pixel-identical.
// border: pixels the floor is cut back on bordered sides; corner: pixels cut from the floor at corners.
Image autotile(const Image& src, int grid, int border, int corner);

// Neighbor mask (N 1, NE 2, E 4, SE 8, S 16, SW 32, W 64, NW 128) of each of the 48 tiles; -1 = the void tile
extern const int AUTOTILE_MASKS[48];

}
