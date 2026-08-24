#include "vcamcore/frame_pattern.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "vcamcore/font5x7.h"

namespace vcamcore {
namespace {

struct Yuv {
  uint8_t y;
  uint8_t u;
  uint8_t v;
};

inline uint8_t Clamp8(int v) {
  return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// BT.601 studio swing, matching what the camera pipeline expects for NV12.
Yuv RgbToYuv(int r, int g, int b) {
  const int y = (65481 * r + 128553 * g + 24966 * b) / 255000 + 16;
  const int u = (-37797 * r - 74203 * g + 112000 * b) / 255000 + 128;
  const int v = (112000 * r - 93786 * g - 18214 * b) / 255000 + 128;
  return {Clamp8(y), Clamp8(u), Clamp8(v)};
}

void FillLuma(const Nv12Target& t, int x0, int y0, int w, int h, uint8_t luma) {
  int x1 = x0 + w;
  int y1 = y0 + h;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > t.width) x1 = t.width;
  if (y1 > t.height) y1 = t.height;
  // A rect entirely off the right or bottom edge leaves x1 < x0; without this
  // the width below goes negative and memset runs with a huge size_t length.
  if (x1 <= x0 || y1 <= y0) return;
  for (int y = y0; y < y1; ++y) {
    std::memset(t.y + static_cast<size_t>(y) * t.yStride + x0, luma,
                static_cast<size_t>(x1 - x0));
  }
}

// The rect is given in luma coordinates; NV12 chroma is half resolution in
// both axes, so it is rounded outward to whole chroma samples.
void FillChroma(const Nv12Target& t, int x0, int y0, int w, int h, uint8_t cb, uint8_t cr) {
  const int cw = t.width / 2;
  const int ch = t.height / 2;
  int cx0 = x0 / 2;
  int cy0 = y0 / 2;
  int cx1 = (x0 + w + 1) / 2;
  int cy1 = (y0 + h + 1) / 2;
  if (cx0 < 0) cx0 = 0;
  if (cy0 < 0) cy0 = 0;
  if (cx1 > cw) cx1 = cw;
  if (cy1 > ch) cy1 = ch;
  for (int y = cy0; y < cy1; ++y) {
    uint8_t* row = t.uv + static_cast<size_t>(y) * t.uvStride;
    for (int x = cx0; x < cx1; ++x) {
      row[x * 2] = cb;
      row[x * 2 + 1] = cr;
    }
  }
}

void FillRect(const Nv12Target& t, int x, int y, int w, int h, Yuv c) {
  FillLuma(t, x, y, w, h, c.y);
  FillChroma(t, x, y, w, h, c.u, c.v);
}

// Text is drawn into the luma plane only. Callers put text on a chroma-neutral
// background, so ink lands as clean white without half-resolution color fringes.
void DrawText(const Nv12Target& t, int x, int y, int scale, const char* text, uint8_t ink) {
  if (!text || scale < 1) return;
  int penX = x;
  for (const char* p = text; *p; ++p) {
    const char* const* glyph = FindGlyph(*p);
    if (glyph) {
      for (int row = 0; row < kGlyphH; ++row) {
        for (int col = 0; col < kGlyphW; ++col) {
          if (glyph[row][col] == '#') {
            FillLuma(t, penX + col * scale, y + row * scale, scale, scale, ink);
          }
        }
      }
    }
    penX += (kGlyphW + 1) * scale;
    if (penX >= t.width) break;
  }
}

void FormatElapsed(uint64_t timeMs, char* out, size_t cap) {
  const uint64_t ms = timeMs % 1000;
  const uint64_t totalSec = timeMs / 1000;
  const uint64_t s = totalSec % 60;
  const uint64_t m = (totalSec / 60) % 60;
  const uint64_t h = totalSec / 3600;
  std::snprintf(out, cap, "T %02llu:%02llu:%02llu.%03llu",
                static_cast<unsigned long long>(h), static_cast<unsigned long long>(m),
                static_cast<unsigned long long>(s), static_cast<unsigned long long>(ms));
}

// A dial whose hand sweeps once per second at 30fps - the fastest way to tell
// a live stream from a frozen last frame.
void DrawDial(const Nv12Target& t, int cx, int cy, int radius, uint64_t frameIndex) {
  if (radius < 4) return;

  const Yuv face = RgbToYuv(24, 24, 32);
  const Yuv hand = RgbToYuv(255, 220, 0);
  const Yuv tick = RgbToYuv(120, 120, 130);

  FillRect(t, cx - radius, cy - radius, radius * 2, radius * 2, face);

  const int dot = radius / 12 > 1 ? radius / 12 : 2;
  for (int i = 0; i < 12; ++i) {
    const double a = 3.14159265358979 * 2.0 * i / 12.0;
    const int tx = cx + static_cast<int>(std::cos(a) * (radius - dot * 2));
    const int ty = cy + static_cast<int>(std::sin(a) * (radius - dot * 2));
    FillRect(t, tx - dot / 2, ty - dot / 2, dot, dot, tick);
  }

  const double angle = 3.14159265358979 * 2.0 * (frameIndex % 30) / 30.0 - 3.14159265358979 / 2.0;
  const double dx = std::cos(angle);
  const double dy = std::sin(angle);
  const int steps = radius - dot * 3;
  for (int r = 0; r < steps; ++r) {
    const int px = cx + static_cast<int>(dx * r);
    const int py = cy + static_cast<int>(dy * r);
    FillRect(t, px - dot / 2, py - dot / 2, dot, dot, hand);
  }
  FillRect(t, cx - dot, cy - dot, dot * 2, dot * 2, hand);
}

}  // namespace

bool IsValid(const Nv12Target& t) {
  if (!t.y || !t.uv) return false;
  if (t.width <= 0 || t.height <= 0) return false;
  if (t.width % 2 != 0 || t.height % 2 != 0) return false;
  if (t.yStride < t.width || t.uvStride < t.width) return false;
  return true;
}

void RenderFrame(const Nv12Target& t, uint64_t frameIndex, uint64_t timeMs, const char* label) {
  if (!IsValid(t)) return;

  const int W = t.width;
  const int H = t.height;

  // --- top third: SMPTE-style colour bars ---------------------------------
  // Recognisably a test signal, and wrong chroma handling shows up instantly
  // as wrong bar colours.
  const int barsH = H * 35 / 100;
  static const int kBars[8][3] = {
      {192, 192, 192},  // grey
      {192, 192, 0},    // yellow
      {0, 192, 192},    // cyan
      {0, 192, 0},      // green
      {192, 0, 192},    // magenta
      {192, 0, 0},      // red
      {0, 0, 192},      // blue
      {16, 16, 16},     // near black
  };
  for (int i = 0; i < 8; ++i) {
    const int x0 = W * i / 8;
    const int x1 = W * (i + 1) / 8;
    FillRect(t, x0, 0, x1 - x0, barsH, RgbToYuv(kBars[i][0], kBars[i][1], kBars[i][2]));
  }

  // --- lower area: chroma-neutral backdrop for text -----------------------
  FillRect(t, 0, barsH, W, H - barsH, RgbToYuv(12, 14, 20));

  const int scale = H / 120 > 0 ? H / 120 : 1;
  const int marginX = W / 20;
  const int textTop = barsH + H / 40;
  const uint8_t ink = 235;

  DrawText(t, marginX, textTop, scale / 2 > 0 ? scale / 2 : 1,
           label ? label : "VIRTUAL TEST CAMERA", ink);

  char buf[64];
  std::snprintf(buf, sizeof(buf), "FRAME %llu", static_cast<unsigned long long>(frameIndex));
  DrawText(t, marginX, barsH + H / 12, scale, buf, ink);

  FormatElapsed(timeMs, buf, sizeof(buf));
  DrawText(t, marginX, barsH + H / 4, scale * 2 / 3 > 0 ? scale * 2 / 3 : 1, buf, ink);

  std::snprintf(buf, sizeof(buf), "%dX%d NV12 %dFPS", W, H, kFpsNumerator / kFpsDenominator);
  DrawText(t, marginX, barsH + H / 3, scale / 3 > 0 ? scale / 3 : 1, buf, ink);

  // --- dial ---------------------------------------------------------------
  const int dialR = (W < H ? W : H) / 8;
  DrawDial(t, W * 13 / 16, barsH + (H - barsH) / 2, dialR, frameIndex);

  // --- sweep bar ----------------------------------------------------------
  // Travels the full width and back; catches stalls the counter would hide if
  // the consumer is only sampling occasional frames.
  const int bandY = H - H / 10;
  const int bandH = H / 24 > 2 ? H / 24 : 2;
  FillRect(t, 0, bandY, W, bandH, RgbToYuv(30, 30, 40));

  const int blockW = W / 10 > 4 ? W / 10 : 4;
  const int travel = W - blockW;
  if (travel > 0) {
    const int period = travel * 2;
    const int phase = static_cast<int>(frameIndex * 6 % static_cast<uint64_t>(period));
    const int blockX = phase < travel ? phase : period - phase;
    FillRect(t, blockX, bandY, blockW, bandH, RgbToYuv(0, 220, 120));
  }

  // --- border -------------------------------------------------------------
  // Confirms the consumer is showing the whole frame rather than a crop.
  const int bw = H / 180 > 2 ? H / 180 : 2;
  const Yuv border = RgbToYuv(255, 255, 255);
  FillRect(t, 0, 0, W, bw, border);
  FillRect(t, 0, H - bw, W, bw, border);
  FillRect(t, 0, 0, bw, H, border);
  FillRect(t, W - bw, 0, bw, H, border);
}

}  // namespace vcamcore
