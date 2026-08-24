#pragma once

#include <windows.h>

#include <mfobjects.h>

namespace vcam {

// Diagnostic: writes every attribute in `attrs` to the log, resolving the keys
// we recognise to readable names.
//
// The media source runs inside the Frame Server with no channel back to the app
// that created the camera, so the only way to find out whether Windows tells us
// *which* camera we are is to look at what it hands us. This is how we look.
void LogAttributes(const char* what, IMFAttributes* attrs);

}  // namespace vcam
