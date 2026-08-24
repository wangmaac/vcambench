#pragma once

#include <windows.h>

namespace vcam {

// This DLL runs inside the Windows Frame Server (svchost.exe). There is no
// console and no debugger attached by default, so logging is the only way to
// find out why a camera came up black.
//
// Primary sink is OutputDebugStringA - visible in Sysinternals DebugView with
// no file permissions involved. A copy goes to
// %ProgramData%\VCamTest\vcamsource.log when that path happens to be writable
// by the service account; failures there are ignored on purpose.
void LogInit();
void LogShutdown();

void Logf(const char* format, ...);
void LogHr(const char* what, HRESULT hr);
void LogGuid(const char* what, const GUID& guid);

}  // namespace vcam
