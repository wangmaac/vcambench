#pragma once

#include <cstdint>

// Writes packed NV12 out as a 24-bit BMP. Shared by vcamprobe (which reads the
// media source directly) and camcapture (which reads it back through the
// Windows camera pipeline) so both save frames the same way and their outputs
// can be compared byte for byte.
bool WriteNv12AsBmp(const wchar_t* path, const uint8_t* nv12, int width, int height);
