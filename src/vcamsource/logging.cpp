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

HANDLE TryOpen(const wchar_t* path) {
  return ::CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void OpenLogFile() {
  wchar_t path[MAX_PATH] = {};

  // Preferred: a shared location the installer grants the Frame Server's
  // service account write access to.
  wchar_t dir[MAX_PATH] = {};
  const DWORD n = ::GetEnvironmentVariableW(L"ProgramData", dir, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    if (::swprintf_s(path, L"%s\\VCamBench", dir) > 0) {
      ::CreateDirectoryW(path, nullptr);  // may already exist, or may fail: fine
    }
    if (::swprintf_s(path, L"%s\\VCamBench\\vcamsource.log", dir) > 0) {
      g_file = TryOpen(path);
      if (g_file != INVALID_HANDLE_VALUE) return;
    }
  }

  // Fallback: next to the DLL. Covers development and portable layouts, and the
  // case where an elevated installer created the ProgramData file with an ACL
  // the service account cannot append to. Logging is diagnostics - it must
  // never take the camera down with it.
  HMODULE self = nullptr;
  if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&OpenLogFile), &self) ||
      self == nullptr) {
    return;
  }

  wchar_t modulePath[MAX_PATH] = {};
  const DWORD len = ::GetModuleFileNameW(self, modulePath, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return;

  wchar_t* slash = ::wcsrchr(modulePath, L'\\');
  if (!slash) return;
  *slash = L'\0';

  if (::swprintf_s(path, L"%s\\vcamsource.log", modulePath) > 0) {
    g_file = TryOpen(path);
  }
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
