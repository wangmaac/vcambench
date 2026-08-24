#include "nv12_bmp.h"

#include <windows.h>

#include <vector>

namespace {

inline uint8_t Clamp8(int v) {
  return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

}  // namespace

bool WriteNv12AsBmp(const wchar_t* path, const uint8_t* nv12, int width, int height) {
  if (!path || !nv12 || width <= 0 || height <= 0) return false;

  const uint8_t* yPlane = nv12;
  const uint8_t* uvPlane = nv12 + static_cast<size_t>(width) * height;

  const int rowBytes = width * 3;
  const int padding = (4 - (rowBytes % 4)) % 4;
  const int stride = rowBytes + padding;
  const DWORD pixelBytes = static_cast<DWORD>(stride) * height;

  BITMAPFILEHEADER fh = {};
  fh.bfType = 0x4D42;  // 'BM'
  fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  fh.bfSize = fh.bfOffBits + pixelBytes;

  BITMAPINFOHEADER ih = {};
  ih.biSize = sizeof(ih);
  ih.biWidth = width;
  ih.biHeight = height;  // positive: rows are written bottom-up
  ih.biPlanes = 1;
  ih.biBitCount = 24;
  ih.biCompression = BI_RGB;
  ih.biSizeImage = pixelBytes;

  HANDLE file = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;

  DWORD written = 0;
  bool ok = ::WriteFile(file, &fh, sizeof(fh), &written, nullptr) != 0;
  ok = ok && ::WriteFile(file, &ih, sizeof(ih), &written, nullptr) != 0;

  std::vector<uint8_t> row(static_cast<size_t>(stride), 0);
  for (int y = height - 1; y >= 0 && ok; --y) {
    for (int x = 0; x < width; ++x) {
      const int Y = yPlane[static_cast<size_t>(y) * width + x];
      const int U = uvPlane[static_cast<size_t>(y / 2) * width + (x / 2) * 2];
      const int V = uvPlane[static_cast<size_t>(y / 2) * width + (x / 2) * 2 + 1];
      const int c = Y - 16;
      const int d = U - 128;
      const int e = V - 128;
      row[static_cast<size_t>(x) * 3 + 0] = Clamp8((298 * c + 516 * d + 128) >> 8);            // B
      row[static_cast<size_t>(x) * 3 + 1] = Clamp8((298 * c - 100 * d - 208 * e + 128) >> 8);  // G
      row[static_cast<size_t>(x) * 3 + 2] = Clamp8((298 * c + 409 * e + 128) >> 8);            // R
    }
    ok = ::WriteFile(file, row.data(), static_cast<DWORD>(stride), &written, nullptr) != 0;
  }
  ::CloseHandle(file);
  return ok;
}
