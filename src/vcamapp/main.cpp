// vcamctl - drives CameraManager from the console.
//
// This is the harness for the piece that had to be proven first: one process
// owning several virtual cameras at once. The GUI replaces the front end later;
// the manager underneath stays.

#include <windows.h>

#include <mfapi.h>

#include <cstdio>
#include <string>
#include <vector>

#include "camera_manager.h"

namespace {

HANDLE g_quit = nullptr;

BOOL WINAPI ConsoleHandler(DWORD type) {
  switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      if (g_quit) ::SetEvent(g_quit);
      return TRUE;
    default:
      return FALSE;
  }
}

void PrintList(const vcam::CameraManager& mgr) {
  if (mgr.Count() == 0) {
    std::printf("  (없음)\n");
    return;
  }
  for (size_t i = 0; i < mgr.Count(); ++i) {
    std::printf("  [%zu] %ls\n", i, mgr.NameAt(i).c_str());
  }
}

void PrintUsage() {
  std::printf(
      "vcamctl - 가상 카메라 관리\n"
      "\n"
      "  vcamctl                      대화형 모드\n"
      "  vcamctl --count N            카메라 N개를 만들고 Enter 까지 유지\n"
      "  vcamctl --count N --seconds S  카메라 N개를 만들고 S초 뒤 종료\n"
      "\n"
      "대화형 명령: add [이름] / rm <번호> / ls / quit\n");
}

int RunInteractive(vcam::CameraManager& mgr) {
  std::printf("명령: add [이름] / rm <번호> / ls / quit\n\n");
  for (;;) {
    std::printf("> ");
    wchar_t line[256] = {};
    if (!::_getws_s(line, 255)) break;

    std::wstring input(line);
    const size_t space = input.find(L' ');
    const std::wstring cmd = input.substr(0, space);
    std::wstring arg = (space == std::wstring::npos) ? L"" : input.substr(space + 1);
    while (!arg.empty() && arg.front() == L' ') arg.erase(arg.begin());
    while (!arg.empty() && arg.back() == L' ') arg.pop_back();

    if (cmd == L"quit" || cmd == L"q" || cmd == L"exit") break;

    if (cmd == L"ls" || cmd.empty()) {
      PrintList(mgr);
    } else if (cmd == L"add") {
      const std::wstring name = arg.empty() ? mgr.SuggestName() : arg;
      std::wstring error;
      if (SUCCEEDED(mgr.Add(name, &error))) {
        std::printf("  + %ls  (총 %zu대)\n", name.c_str(), mgr.Count());
      } else {
        std::printf("  실패: %ls\n", error.c_str());
      }
    } else if (cmd == L"rm") {
      const int index = arg.empty() ? -1 : ::_wtoi(arg.c_str());
      std::wstring error;
      if (index >= 0 && SUCCEEDED(mgr.RemoveAt(static_cast<size_t>(index), &error))) {
        std::printf("  - 제거됨 (총 %zu대)\n", mgr.Count());
      } else {
        std::printf("  실패: %ls\n", error.empty() ? L"번호를 확인하세요." : error.c_str());
      }
    } else {
      std::printf("  모르는 명령입니다. add / rm / ls / quit\n");
    }
  }
  return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  ::SetConsoleOutputCP(CP_UTF8);
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  int count = 0;
  int seconds = 0;
  for (int i = 1; i < argc; ++i) {
    const std::wstring a = argv[i];
    if (a == L"--count" && i + 1 < argc) {
      count = ::_wtoi(argv[++i]);
    } else if (a == L"--seconds" && i + 1 < argc) {
      seconds = ::_wtoi(argv[++i]);
    } else {
      PrintUsage();
      return 2;
    }
  }

  HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr)) {
    std::printf("CoInitializeEx 실패: %ls\n", vcam::ExplainHresult(hr).c_str());
    return 1;
  }
  hr = ::MFStartup(MF_VERSION, MFSTARTUP_LITE);
  if (FAILED(hr)) {
    std::printf("MFStartup 실패: %ls\n", vcam::ExplainHresult(hr).c_str());
    ::CoUninitialize();
    return 1;
  }

  int rc = 0;
  {
    std::wstring error;
    if (!vcam::CameraManager::IsSupported(&error)) {
      std::printf("지원되지 않음: %ls\n", error.c_str());
      ::MFShutdown();
      ::CoUninitialize();
      return 1;
    }

    vcam::CameraManager mgr;

    if (count > 0) {
      for (int i = 0; i < count; ++i) {
        const std::wstring name = mgr.SuggestName();
        std::wstring addError;
        if (SUCCEEDED(mgr.Add(name, &addError))) {
          std::printf("  + %ls\n", name.c_str());
        } else {
          std::printf("  실패 (%d번째): %ls\n", i + 1, addError.c_str());
          rc = 1;
          break;
        }
      }
      std::printf("\n현재 %zu대:\n", mgr.Count());
      PrintList(mgr);

      if (seconds > 0) {
        std::printf("\n%d초 유지 후 종료합니다.\n", seconds);
        ::Sleep(static_cast<DWORD>(seconds) * 1000);
      } else {
        g_quit = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ::SetConsoleCtrlHandler(ConsoleHandler, TRUE);
        std::printf("\n종료하려면 Ctrl+C 를 누르세요.\n");
        ::WaitForSingleObject(g_quit, INFINITE);
      }
    } else {
      rc = RunInteractive(mgr);
    }

    std::printf("\n정리 중...\n");
    mgr.RemoveAll();
  }

  ::MFShutdown();
  ::CoUninitialize();
  std::printf("완료.\n");
  return rc;
}
