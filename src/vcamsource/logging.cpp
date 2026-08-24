#include "logging.h"

#include <shlwapi.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace vcam {
namespace {

CRITICAL_SECTION g_lock;
bool g_lockReady = false;
bool g_fileTried = false;
HANDLE g_file = INVALID_HANDLE_VALUE;

void OpenLogFile() {
  wchar_t dir[MAX_PATH] = {};
  DWORD n = ::GetEnvironmentVariableW(L"ProgramData", dir, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return;

  wchar_t path[MAX_PATH] = {};
  if (::swprintf_s(path, L"%s\\VCamBench", dir) < 0) return;
  ::CreateDirectoryW(path, nullptr);  // may already exist, or may fail: fine

  if (::swprintf_s(path, L"%s\\VCamBench\\vcamsource.log", dir) < 0) return;

  // Best effort. Under the Frame Server's service account this often fails,
  // which is exactly why OutputDebugString is the primary sink.
  g_file = ::CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void WriteLine(const char* text) {
  char stamped[1200];
  SYSTEMTIME st;
  ::GetLocalTime(&st);
  const int len = ::_snprintf_s(stamped, sizeof(stamped), _TRUNCATE,
                                "[vcamsource %02u:%02u:%02u.%03u pid=%lu tid=%lu] %s\r\n",
                                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                                ::GetCurrentProcessId(), ::GetCurrentThreadId(), text);
  if (len <= 0) return;

  ::OutputDebugStringA(stamped);

  if (g_lockReady) {
    ::EnterCriticalSection(&g_lock);
    // Opened lazily: DllMain runs under the loader lock, which is no place to
    // be creating files.
    if (!g_fileTried) {
      g_fileTried = true;
      OpenLogFile();
    }
    if (g_file != INVALID_HANDLE_VALUE) {
      DWORD written = 0;
      ::WriteFile(g_file, stamped, static_cast<DWORD>(len), &written, nullptr);
    }
    ::LeaveCriticalSection(&g_lock);
  }
}

}  // namespace

void LogInit() {
  if (!g_lockReady) {
    ::InitializeCriticalSection(&g_lock);
    g_lockReady = true;
  }
}

void LogShutdown() {
  if (g_lockReady) {
    ::EnterCriticalSection(&g_lock);
    if (g_file != INVALID_HANDLE_VALUE) {
      ::CloseHandle(g_file);
      g_file = INVALID_HANDLE_VALUE;
    }
    ::LeaveCriticalSection(&g_lock);
    ::DeleteCriticalSection(&g_lock);
    g_lockReady = false;
  }
}

void Logf(const char* format, ...) {
  char text[1024];
  va_list args;
  va_start(args, format);
  ::_vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
  va_end(args);
  WriteLine(text);
}

void LogHr(const char* what, HRESULT hr) {
  Logf("%s -> hr=0x%08lX", what, static_cast<unsigned long>(hr));
}

void LogGuid(const char* what, const GUID& guid) {
  Logf("%s -> {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", what,
       static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3, guid.Data4[0],
       guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6],
       guid.Data4[7]);
}

}  // namespace vcam
