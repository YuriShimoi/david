#pragma once
#include <cstdint>

namespace david {

class Document;

// Straight-alpha "normal" blend of src over dst, with src scaled by opacity (0-255)
uint32_t blendOver(uint32_t dst, uint32_t src, uint8_t opacity = 255);

// Composites every visible layer for row y into out (width pixels); frame -1 = the active frame
void flattenRow(const Document& doc, int y, uint32_t* out, int frame = -1);

}
