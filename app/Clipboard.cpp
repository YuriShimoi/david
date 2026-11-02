#include "app/App.h"
#include "core/Composite.h"
#include "core/Raster.h"
#include "core/Resize.h"
#include "core/Text.h"
#include "core/io/Png.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Widgets.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace david {

#pragma region [ System clipboard ]
// The copied image, offered as PNG (keeps alpha: browsers, GIMP, Krita) and BMP (Paint, Office); SDL asks for
// the bytes only when another program pastes, so they stay here until the next copy
static std::vector<uint8_t> s_png, s_bmp;

static const void* clipboardData(void*, const char* mime, size_t* size) {
  const std::vector<uint8_t>* v = !std::strcmp(mime, "image/png") ? &s_png : !std::strcmp(mime, "image/bmp") ? &s_bmp : nullptr;
  if(!v || v->empty()) { *size = 0; return nullptr; }
  *size = v->size();
  return v->data();
}

static std::vector<uint8_t> encodeBmp(const Lifted& img) {
  std::vector<uint8_t> out;
  int w = img.bounds.width(), h = img.bounds.height();
  SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, const_cast<uint32_t*>(img.px.data()), w * 4);
  SDL_IOStream* io = SDL_IOFromDynamicMem();
  if(s && io && SDL_SaveBMP_IO(s, io, false)) {
    out.resize(size_t(SDL_GetIOSize(io)));
    SDL_SeekIO(io, 0, SDL_IO_SEEK_SET);
    SDL_ReadIO(io, out.data(), out.size());
  }
  if(io) SDL_CloseIO(io);
  if(s) SDL_DestroySurface(s);
  return out;
}

static bool putOnClipboard(const Lifted& img) {
  int w = img.bounds.width();
  s_png = png::encode(w, img.bounds.height(), [&](int y, uint32_t* row) { std::copy_n(img.px.begin() + size_t(y) * w, w, row); }, 1);
  s_bmp = encodeBmp(img);
  const char* mimes[] = { "image/png", "image/bmp" };
  return SDL_SetClipboardData(clipboardData, nullptr, nullptr, mimes, 2);
}

// PNG first (keeps alpha), then BMP; bounds start at 0, 0
static bool readClipboard(Lifted& out) {
  size_t size = 0;
  if(SDL_HasClipboardData("image/png")) {
    if(void* data = SDL_GetClipboardData("image/png", &size)) {
      std::vector<uint8_t> bytes(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
      SDL_free(data);
      std::string err;
      if(auto doc = png::loadMemory(bytes, err)) {
        Layer& l = *doc->layers[0];
        out.bounds = { 0, 0, doc->width() - 1, doc->height() - 1 };
        out.px.resize(size_t(doc->width()) * doc->height());
        for(int y = 0; y < doc->height(); y++)
          for(int x = 0; x < doc->width(); x++) out.px[size_t(y) * doc->width() + x] = l.get(x, y);
        return true;
      }
    }
  }
  if(SDL_HasClipboardData("image/bmp")) {
    if(void* data = SDL_GetClipboardData("image/bmp", &size)) {
      SDL_Surface* s = SDL_LoadBMP_IO(SDL_IOFromConstMem(data, size), true);
      SDL_free(data);
      SDL_Surface* c = s ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32) : nullptr;
      if(s) SDL_DestroySurface(s);
      if(!c) return false;
      out.bounds = { 0, 0, c->w - 1, c->h - 1 };
      out.px.resize(size_t(c->w) * c->h);
      for(int y = 0; y < c->h; y++) std::memcpy(&out.px[size_t(y) * c->w], static_cast<uint8_t*>(c->pixels) + size_t(y) * c->pitch, size_t(c->w) * 4);
      SDL_DestroySurface(c);
      // Screenshots come as 32-bit DIBs with every alpha at 0: those are opaque
      if(std::all_of(out.px.begin(), out.px.end(), [](uint32_t p) { return (p >> 24) == 0; }))
        for(auto& p : out.px) p |= 0xFF000000u;
      return true;
    }
  }
  return false;
}
#pragma endregion

#pragma region [ Copy and paste ]
// The selection's pixels (or the whole image without one) of the active layer, or of the merged image
void App::copyImage(bool merged, bool cut) {
  commitTool();
  Document& doc = *_doc;
  Lifted img;
  if(merged) {
    IRect b = doc.hasSelection() ? doc.selection->bounds() : IRect{ 0, 0, doc.width() - 1, doc.height() - 1 };
    if(b.empty()) return;
    img.bounds = b;
    img.px.assign(size_t(b.width()) * b.height(), 0);
    std::vector<uint32_t> row(doc.width());
    std::vector<uint8_t> cov(b.width(), 255);
    for(int y = b.y0; y <= b.y1; y++) {
      flattenRow(doc, y, row.data());
      if(doc.hasSelection()) doc.selection->readRow(y, b.x0, b.x1, cov.data());
      for(int i = 0; i < b.width(); i++) {
        uint32_t p = row[b.x0 + i];
        uint32_t a = (p >> 24) * cov[i] / 255;
        img.px[size_t(y - b.y0) * b.width() + i] = (p & 0x00FFFFFF) | (a << 24);
      }
    }
  } else {
    Mask all(doc.width(), doc.height());
    all.fillAll();
    img = lift(doc.active(), doc.hasSelection() ? *doc.selection : all);
    if(img.bounds.empty()) return;
  }
  if(!putOnClipboard(img)) { showError(std::string(tr("Couldn't copy to the clipboard:")) + " " + SDL_GetError()); return; }
  if(cut && doc.hasSelection()) selectionOp("erase");
}

void App::pasteImage(EPaste where) {
  commitTool();
  Lifted img;
  if(!readClipboard(img)) { showError(tr("The clipboard has no image.")); return; }
  _pasted = std::move(img);
  int w = _pasted.bounds.width(), h = _pasted.bounds.height();
  if(where == EPaste::NewImage) {
    confirmDiscard([this, w, h] {
      auto doc = Document::blank(w, h, 0);
      Layer& l = *doc->layers[0];
      for(int y = 0; y < h; y++)
        for(int x = 0; x < w; x++) if(uint32_t p = _pasted.px[size_t(y) * w + x]) l.set(x, y, p);
      setDocument(std::move(doc));
      _pasted = {};
    });
    return;
  }
  _pasteWhere = where;
  // Bigger than the canvas: ask whether the canvas grows to fit
  if(w > _doc->width() || h > _doc->height()) _popup = "###pastebig";
  else placePaste(false);
}

// Into the active layer (or a new one) as floating pixels on the Move Selected Pixels tool, at the top-left of the
// visible area; Enter or another tool commits them
void App::placePaste(bool grow) {
  Document& doc = *_doc;
  int w = _pasted.bounds.width(), h = _pasted.bounds.height();
  if(grow) resizeCanvas(doc, std::max(w, doc.width()), std::max(h, doc.height()), 0, 0, 0, ICON_FA_EXPAND);
  if(_pasteWhere == EPaste::NewLayer)
    addLayer(doc, std::make_unique<Layer>(doc.width(), doc.height(), std::string(tr("Layer")) + " " + std::to_string(doc.layers.size() + 1)),
             doc.activeLayer + 1, N_("Add New Layer"), ICON_FA_SQUARE_PLUS);
  int x = std::clamp(int(std::floor(_nav.view.originX)), 0, std::max(0, doc.width() - w));
  int y = std::clamp(int(std::floor(_nav.view.originY)), 0, std::max(0, doc.height() - h));
  _pasted.bounds = { x, y, x + w - 1, y + h - 1 };
  useTool(ETool::MovePixels);
  tool().paste(*this, std::move(_pasted));
  _pasted = {};
}

void App::drawPasteDialog() {
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
  std::string title = std::string(ICON_FA_PASTE "  ") + tr("Paste") + "###pastebig";
  bool open = true;
  if(!ImGui::BeginPopupModal(title.c_str(), &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
  ImGui::Spacing();
  ImGui::TextUnformatted(tr("The image being pasted is larger than the canvas."));
  ImGui::TextDisabled("%s: %d × %d   %s: %d × %d", tr("Image"), _pasted.bounds.width(), _pasted.bounds.height(), tr("Canvas"), _doc->width(), _doc->height());
  ImGui::Spacing();
  ImGui::Spacing();
  float bw = 150 * _dpi;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - bw * 3 - ImGui::GetStyle().ItemSpacing.x * 2));
  bool grow = ui::dialogButton(tr("Expand canvas"), true, bw) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
  ImGui::SameLine();
  bool keep = ui::dialogButton(tr("Keep canvas size"), false, bw);
  ImGui::SameLine();
  bool cancel = ui::dialogButton(tr("Cancel"), false, bw) || ImGui::IsKeyPressed(ImGuiKey_Escape) || !open;
  if(grow || keep || cancel) {
    ImGui::CloseCurrentPopup();
    if(cancel) _pasted = {};
    else placePaste(grow);
  }
  ImGui::EndPopup();
}
#pragma endregion

}
