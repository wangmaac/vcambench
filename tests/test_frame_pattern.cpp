// Unit tests for vcamcore. No framework: this runs in CI-less local builds and
// the whole point is that it has no dependencies.
//
// The bounds tests matter more than they look. RenderFrame executes inside the
// Windows Frame Server process, where an overrun corrupts a system service and
// takes every camera on the machine down with it.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "vcamcore/font5x7.h"
#include "vcamcore/frame_pattern.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

constexpr uint8_t kGuard = 0xAB;
constexpr size_t kGuardBytes = 64;

// An NV12 buffer wrapped in guard bytes, with a stride wider than the picture
// so stride handling is exercised rather than assumed.
struct GuardedFrame {
  int width;
  int height;
  int yStride;
  int uvStride;
  std::vector<uint8_t> storage;

  GuardedFrame(int w, int h, int strideSlack)
      : width(w), height(h), yStride(w + strideSlack), uvStride(w + strideSlack) {
    const size_t ySize = static_cast<size_t>(yStride) * height;
    const size_t uvSize = static_cast<size_t>(uvStride) * (height / 2);
    storage.assign(kGuardBytes + ySize + kGuardBytes + uvSize + kGuardBytes, kGuard);
  }

  uint8_t* yPlane() { return storage.data() + kGuardBytes; }
  uint8_t* uvPlane() {
    return storage.data() + kGuardBytes + static_cast<size_t>(yStride) * height + kGuardBytes;
  }

  vcamcore::Nv12Target target() {
    vcamcore::Nv12Target t;
    t.y = yPlane();
    t.yStride = yStride;
    t.uv = uvPlane();
    t.uvStride = uvStride;
    t.width = width;
    t.height = height;
    return t;
  }

  bool guardsIntact() const {
    const size_t ySize = static_cast<size_t>(yStride) * height;
    const size_t uvSize = static_cast<size_t>(uvStride) * (height / 2);
    const size_t offsets[3] = {0, kGuardBytes + ySize, kGuardBytes + ySize + kGuardBytes + uvSize};
    for (size_t off : offsets) {
      for (size_t i = 0; i < kGuardBytes; ++i) {
        if (storage[off + i] != kGuard) return false;
      }
    }
    return true;
  }

  // Bytes between the end of the picture and the end of each row must be
  // untouched, otherwise the renderer is ignoring the stride.
  bool paddingIntact() {
    for (int row = 0; row < height; ++row) {
      const uint8_t* p = yPlane() + static_cast<size_t>(row) * yStride;
      for (int x = width; x < yStride; ++x) {
        if (p[x] != kGuard) return false;
      }
    }
    for (int row = 0; row < height / 2; ++row) {
      const uint8_t* p = uvPlane() + static_cast<size_t>(row) * uvStride;
      for (int x = width; x < uvStride; ++x) {
        if (p[x] != kGuard) return false;
      }
    }
    return true;
  }

  std::vector<uint8_t> pictureBytes() {
    std::vector<uint8_t> out;
    for (int row = 0; row < height; ++row) {
      const uint8_t* p = yPlane() + static_cast<size_t>(row) * yStride;
      out.insert(out.end(), p, p + width);
    }
    for (int row = 0; row < height / 2; ++row) {
      const uint8_t* p = uvPlane() + static_cast<size_t>(row) * uvStride;
      out.insert(out.end(), p, p + width);
    }
    return out;
  }
};

void TestIsValid() {
  std::printf("IsValid\n");
  uint8_t dummy[16] = {};

  vcamcore::Nv12Target good;
  good.y = dummy;
  good.uv = dummy;
  good.yStride = 4;
  good.uvStride = 4;
  good.width = 4;
  good.height = 2;
  CHECK(vcamcore::IsValid(good));

  vcamcore::Nv12Target t = good;
  t.y = nullptr;
  CHECK(!vcamcore::IsValid(t));

  t = good;
  t.uv = nullptr;
  CHECK(!vcamcore::IsValid(t));

  t = good;
  t.width = 3;  // odd
  CHECK(!vcamcore::IsValid(t));

  t = good;
  t.height = 3;  // odd
  CHECK(!vcamcore::IsValid(t));

  t = good;
  t.yStride = 2;  // narrower than the picture
  CHECK(!vcamcore::IsValid(t));

  t = good;
  t.width = 0;
  CHECK(!vcamcore::IsValid(t));
}

void TestInvalidTargetIsIgnored() {
  std::printf("invalid target does not write\n");
  GuardedFrame f(64, 48, 16);

  vcamcore::Nv12Target t = f.target();
  t.width = 65;  // odd: invalid
  vcamcore::RenderFrame(t, 0, 0, "X");

  // Nothing at all should have been written.
  bool untouched = true;
  for (uint8_t b : f.storage) {
    if (b != kGuard) {
      untouched = false;
      break;
    }
  }
  CHECK(untouched);

  vcamcore::Nv12Target nullTarget;
  vcamcore::RenderFrame(nullTarget, 0, 0, nullptr);  // must not crash
  CHECK(true);
}

void TestStaysInBounds() {
  std::printf("stays inside the target\n");
  // Several geometries, including sizes far smaller than the real 1280x720, to
  // catch layout maths that assumes there is room for everything it draws.
  const int sizes[][2] = {{1280, 720}, {640, 480}, {320, 240}, {64, 48}, {16, 16}, {2, 2}};
  for (const auto& s : sizes) {
    GuardedFrame f(s[0], s[1], 32);
    vcamcore::Nv12Target t = f.target();
    for (uint64_t i = 0; i < 40; ++i) {
      vcamcore::RenderFrame(t, i * 7, i * 233, "VIRTUAL TEST CAMERA");
    }
    if (!f.guardsIntact()) std::printf("  (guards broken at %dx%d)\n", s[0], s[1]);
    if (!f.paddingIntact()) std::printf("  (stride padding written at %dx%d)\n", s[0], s[1]);
    CHECK(f.guardsIntact());
    CHECK(f.paddingIntact());
  }
}

void TestDeterministic() {
  std::printf("deterministic\n");
  GuardedFrame a(320, 240, 8);
  GuardedFrame b(320, 240, 8);
  vcamcore::Nv12Target ta = a.target();
  vcamcore::Nv12Target tb = b.target();

  vcamcore::RenderFrame(ta, 12345, 67890, "CAM");
  vcamcore::RenderFrame(tb, 12345, 67890, "CAM");

  CHECK(a.pictureBytes() == b.pictureBytes());
}

void TestFrameIndexChangesPixels() {
  std::printf("animation advances\n");
  GuardedFrame a(320, 240, 0);
  GuardedFrame b(320, 240, 0);
  vcamcore::Nv12Target ta = a.target();
  vcamcore::Nv12Target tb = b.target();

  // Consecutive frames must differ, not just distant ones - a dial that only
  // moves every 30 frames would look frozen to a viewer.
  for (uint64_t i = 0; i < 30; ++i) {
    vcamcore::RenderFrame(ta, i, i * 33, "CAM");
    vcamcore::RenderFrame(tb, i + 1, (i + 1) * 33, "CAM");
    if (a.pictureBytes() == b.pictureBytes()) {
      std::printf("  (frames %llu and %llu are identical)\n",
                  static_cast<unsigned long long>(i), static_cast<unsigned long long>(i + 1));
    }
    CHECK(a.pictureBytes() != b.pictureBytes());
  }
}

void TestTimeChangesPixels() {
  std::printf("elapsed time is rendered\n");
  GuardedFrame a(640, 480, 0);
  GuardedFrame b(640, 480, 0);
  vcamcore::Nv12Target ta = a.target();
  vcamcore::Nv12Target tb = b.target();

  vcamcore::RenderFrame(ta, 100, 1000, "CAM");
  vcamcore::RenderFrame(tb, 100, 61000, "CAM");
  CHECK(a.pictureBytes() != b.pictureBytes());
}

void TestColourBarsAreColoured() {
  std::printf("colour bars carry chroma\n");
  GuardedFrame f(1280, 720, 0);
  vcamcore::Nv12Target t = f.target();
  vcamcore::RenderFrame(t, 0, 0, "CAM");

  // Sample the middle of each of the eight bars, a quarter of the way down,
  // and require that they are not all the same neutral grey.
  const int sampleY = 720 * 35 / 100 / 2;
  int distinct = 0;
  int prevU = -1, prevV = -1;
  for (int i = 0; i < 8; ++i) {
    const int x = 1280 * i / 8 + 1280 / 16;
    const uint8_t* uvRow = f.uvPlane() + static_cast<size_t>(sampleY / 2) * f.uvStride;
    const int u = uvRow[(x / 2) * 2];
    const int v = uvRow[(x / 2) * 2 + 1];
    if (u != prevU || v != prevV) ++distinct;
    prevU = u;
    prevV = v;
  }
  CHECK(distinct >= 6);
}

// Regression: a camera whose name ends in a digit must draw that digit.
//
// Cameras are told apart on screen only by this label, so a renderer that
// quietly drops the last glyph makes every camera look identical - which is
// exactly the symptom this was written for.
void TestLabelDrawsEveryGlyph() {
  std::printf("label draws its last glyph\n");

  const int W = 1280;
  const int H = 720;
  GuardedFrame f(W, H, 0);
  vcamcore::Nv12Target t = f.target();
  vcamcore::RenderFrame(t, 0, 0, "VCamBench 1");

  // Layout mirrors RenderFrame: label sits at x = W/20, y = 35% + H/40,
  // drawn at scale (H/120)/2 with a 6px-per-character advance.
  const int scale = (H / 120) / 2;
  const int advance = (vcamcore::kGlyphW + 1) * scale;
  const int originX = W / 20;
  const int originY = H * 35 / 100 + H / 40;

  const auto hasInk = [&](int charIndex) {
    const int x0 = originX + charIndex * advance;
    for (int y = originY; y < originY + vcamcore::kGlyphH * scale; ++y) {
      for (int x = x0; x < x0 + vcamcore::kGlyphW * scale; ++x) {
        if (f.yPlane()[static_cast<size_t>(y) * f.yStride + x] > 200) return true;
      }
    }
    return false;
  };

  // "VCamBench 1": V at 0, H at 8, space at 9, the digit at 10.
  CHECK(hasInk(0));    // first glyph
  CHECK(hasInk(8));    // last letter
  CHECK(!hasInk(9));   // the space really is blank
  if (!hasInk(10)) std::printf("  (the trailing digit was not drawn)\n");
  CHECK(hasInk(10));   // the digit that tells cameras apart
}

void TestNullLabelIsSafe() {
  std::printf("null label is safe\n");
  GuardedFrame f(320, 240, 8);
  vcamcore::Nv12Target t = f.target();
  vcamcore::RenderFrame(t, 5, 5, nullptr);
  CHECK(f.guardsIntact());
  CHECK(f.paddingIntact());
}

}  // namespace

int main() {
  // Unbuffered: if a test faults, the line naming it must already be out.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("vcamcore tests\n\n");

  TestIsValid();
  TestInvalidTargetIsIgnored();
  TestStaysInBounds();
  TestDeterministic();
  TestFrameIndexChangesPixels();
  TestTimeChangesPixels();
  TestColourBarsAreColoured();
  TestLabelDrawsEveryGlyph();
  TestNullLabelIsSafe();

  std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
