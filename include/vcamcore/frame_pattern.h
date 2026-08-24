#pragma once

#include <cstdint>

namespace vcamcore {

// The one format the virtual camera advertises. NV12 is what the Windows
// camera pipeline consumes natively, so consumers get it without conversion.
inline constexpr int kDefaultWidth = 1280;
inline constexpr int kDefaultHeight = 720;
inline constexpr int kFpsNumerator = 30;
inline constexpr int kFpsDenominator = 1;

// One NV12 destination. `y` and `uv` may live in the same allocation (the usual
// contiguous layout) or in separate ones; only the strides are assumed.
//
//   y  : height rows of at least width bytes, spaced yStride apart
//   uv : height/2 rows of at least width bytes (width/2 interleaved U,V pairs),
//        spaced uvStride apart
struct Nv12Target {
  uint8_t* y = nullptr;
  int yStride = 0;
  uint8_t* uv = nullptr;
  int uvStride = 0;
  int width = 0;
  int height = 0;
};

// Width and height must be positive and even, planes non-null, strides at least
// as wide as the picture.
bool IsValid(const Nv12Target& t);

// Draws one frame of the test pattern.
//
// Deterministic: identical arguments produce identical pixels. Allocates
// nothing, touches no globals, and never writes outside the target - this runs
// inside the Windows Frame Server process, where a fault takes down every
// camera on the machine.
//
// `frameIndex` drives the animation, `timeMs` is the elapsed time shown on
// screen, and `label` (may be null) names the camera.
void RenderFrame(const Nv12Target& t, uint64_t frameIndex, uint64_t timeMs, const char* label);

}  // namespace vcamcore
