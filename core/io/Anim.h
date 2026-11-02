#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace david {

class Document;

// One flattened frame of an animation (every visible layer), straight RGBA, and how long it shows
struct AnimFrame {
  std::vector<uint32_t> px;
  int ms = 100;
};

// The frames a file gets: in the play order of tag (-1 = every frame; a ping-pong tag goes there and back),
// each scaled up by an integer factor (nearest neighbor)
std::vector<AnimFrame> animFrames(const Document& doc, int tag, int scale);
// A one-layer document with one frame per image (imports); the most common frame length becomes the
// frame rate and the others keep their own duration
std::unique_ptr<Document> documentFromFrames(int w, int h, const std::vector<AnimFrame>& frames);

namespace gif {
  // GIF89a: one global palette (exact up to 255 colors, median cut past that), alpha under 128 transparent,
  // loops 0 = forever. The frame delay is kept in 1/100 s.
  std::vector<uint8_t> encode(int w, int h, const std::vector<AnimFrame>& frames, int loops);
  std::unique_ptr<Document> load(const std::vector<uint8_t>& bytes, std::string& error);
}

namespace apng {
  bool animated(const std::vector<uint8_t>& bytes);
  // Every frame full size (dispose none, blend source), so any APNG reader shows it the same way
  std::vector<uint8_t> encode(int w, int h, const std::vector<AnimFrame>& frames, int loops);
  std::unique_ptr<Document> load(const std::vector<uint8_t>& bytes, std::string& error);
}

namespace webpanim {
  bool animated(const std::vector<uint8_t>& bytes);
  // Lossless at quality 100, else lossy at that quality
  std::vector<uint8_t> encode(int w, int h, const std::vector<AnimFrame>& frames, int loops, int quality, std::string& error);
  std::unique_ptr<Document> load(const std::vector<uint8_t>& bytes, std::string& error);
}

#pragma region [ Sprite sheets ]
struct SheetOptions {
  int columns = 0;  // 0 = as square as the frames allow; a big number = one row
  int padding = 0;  // empty pixels between frames (and around the sheet)
  int scale = 1;
  int tag = -1;     // only that tag's frames
};

// The frames side by side in one image, plus a JSON description in Aseprite's format (frames with their
// rect and duration, the tags); image is the file name the JSON points to
std::unique_ptr<Document> sheetFromDocument(const Document& doc, const SheetOptions& o, const std::string& image, std::string& json);

struct SliceOptions {
  int frameW = 32, frameH = 32;
  int padding = 0;            // between frames
  int offsetX = 0, offsetY = 0;
  int count = 0;              // 0 = every whole cell
  bool skipEmpty = true;      // trailing empty cells are not frames
};

// A sheet cut into a grid of frames: one layer, one frame per cell, in reading order
std::unique_ptr<Document> documentFromSheet(const Document& sheet, const SliceOptions& o, std::string& error);
#pragma endregion

}
