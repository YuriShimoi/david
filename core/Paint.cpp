#include "core/Paint.h"
#include "core/Composite.h"
#include "core/Document.h"
#include <algorithm>
#include <atomic>
#include <thread>

namespace david {

uint32_t mixPixel(uint32_t a, uint32_t b, int k) {
  if(k >= 255) return b;
  if(k <= 0) return a;
  uint32_t wa = (a >> 24) * (255 - k), wb = (b >> 24) * k, sum = wa + wb;
  if(!sum) return 0;
  uint32_t out = 0;
  for(int s = 0; s < 24; s += 8) out |= ((((a >> s) & 0xFF) * wa + ((b >> s) & 0xFF) * wb + sum / 2) / sum) << s;
  return out | (((sum + 127) / 255) << 24);
}

void paintRegion(Document& doc, TileRecorder& rec, IRect r, const RowPainter& fn) {
  Layer& layer = rec.layer();
  r.x0 = std::max(r.x0, 0); r.y0 = std::max(r.y0, 0);
  r.x1 = std::min(r.x1, layer.width() - 1); r.y1 = std::min(r.y1, layer.height() - 1);
  const Mask* sel = doc.hasSelection() ? doc.selection.get() : nullptr;
  if(sel) {
    IRect b = sel->bounds();
    r.x0 = std::max(r.x0, b.x0); r.y0 = std::max(r.y0, b.y0); r.x1 = std::min(r.x1, b.x1); r.y1 = std::min(r.y1, b.y1);
  }
  if(r.empty()) return;
  rec.touchRect(r.x0, r.y0, r.x1, r.y1);

  // Tiles are allocated up front; the workers then only write inside their own tile
  struct Job { int tx, ty; Tile* tile; const Tile* before; };
  std::vector<Job> jobs;
  for(int ty = r.y0 / Tile::SIZE; ty <= r.y1 / Tile::SIZE; ty++)
    for(int tx = r.x0 / Tile::SIZE; tx <= r.x1 / Tile::SIZE; tx++)
      jobs.push_back({ tx, ty, &layer.ensureTile(tx, ty), rec.beforeTile(tx, ty) });

  std::atomic<size_t> next{ 0 };
  auto work = [&] {
    std::vector<uint32_t> zeros(Tile::SIZE, 0), before(Tile::SIZE);
    std::vector<uint8_t> cov(Tile::SIZE);
    for(size_t j; (j = next++) < jobs.size();) {
      const Job& job = jobs[j];
      int x0 = std::max(r.x0, job.tx * Tile::SIZE), x1 = std::min(r.x1, job.tx * Tile::SIZE + Tile::SIZE - 1);
      int y0 = std::max(r.y0, job.ty * Tile::SIZE), y1 = std::min(r.y1, job.ty * Tile::SIZE + Tile::SIZE - 1);
      int n = x1 - x0 + 1, rx = x0 - job.tx * Tile::SIZE;
      for(int y = y0; y <= y1; y++) {
        int ry = y - job.ty * Tile::SIZE;
        const uint32_t* b = job.before ? job.before->data() + ry * Tile::SIZE + rx : zeros.data();
        uint32_t* out = job.tile->data() + ry * Tile::SIZE + rx;
        if(sel) std::copy_n(b, n, before.data());
        fn(x0, y, n, b, out);
        if(sel) {
          sel->readRow(y, x0, x1, cov.data());
          for(int i = 0; i < n; i++) out[i] = mixPixel(before[i], out[i], cov[i]);
        }
      }
      job.tile->touch();
    }
  };
  int threads = std::clamp(int(std::thread::hardware_concurrency()), 1, std::min(int(jobs.size()), 16));
  std::vector<std::thread> pool;
  for(int i = 1; i < threads; i++) pool.emplace_back(work);
  work();
  for(auto& t : pool) t.join();

  // A tile that was missing and stayed empty goes back to missing
  for(auto& job : jobs)
    if(!job.before && job.tile->isEmpty()) layer.putTile(job.tx, job.ty, nullptr);
}

void paintCoverage(Document& doc, TileRecorder& rec, std::initializer_list<CoveragePass> passes) {
  IRect r;
  bool any = false;
  for(auto& p : passes) {
    IRect b = p.mask->bounds();
    if(b.empty()) continue;
    if(!any) r = b;
    else { r.x0 = std::min(r.x0, b.x0); r.y0 = std::min(r.y0, b.y0); r.x1 = std::max(r.x1, b.x1); r.y1 = std::max(r.y1, b.y1); }
    any = true;
  }
  if(!any) return;
  std::vector<CoveragePass> list(passes);
  paintRegion(doc, rec, r, [&](int x0, int y, int n, const uint32_t* before, uint32_t* out) {
    thread_local std::vector<uint8_t> cov;
    cov.resize(n);
    std::copy_n(before, n, out);
    for(auto& p : list) {
      p.mask->readRow(y, x0, x0 + n - 1, cov.data());
      for(int i = 0; i < n; i++) if(cov[i]) out[i] = blendOver(out[i], p.color, cov[i]);
    }
  });
}

TileRecorder& LiveEdit::redraw(Document& doc) {
  if(_rec && _layerId != doc.active().id()) cancel();
  if(_rec) _rec->restore();
  else {
    _rec = std::make_unique<TileRecorder>(doc.active());
    _layerId = doc.active().id();
  }
  return *_rec;
}

void LiveEdit::commit(Document& doc, std::string name, const char* icon) {
  if(!_rec) return;
  if(auto edit = _rec->finish(std::move(name), icon)) doc.history.push(doc, std::move(edit));
  _rec.reset();
}

std::unique_ptr<TileEdit> LiveEdit::take(std::string name, const char* icon) {
  if(!_rec) return nullptr;
  auto edit = _rec->finish(std::move(name), icon);
  _rec.reset();
  return edit;
}

void LiveEdit::cancel() {
  if(_rec) _rec->restore();
  _rec.reset();
}

}
