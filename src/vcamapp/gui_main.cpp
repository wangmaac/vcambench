// VCamBench - the window the user actually runs.
//
// Deliberately one screen with no modes: the list of cameras that exist right
// now, and the three things you can do to it. Cameras live only while this
// process does, so the window closing is the same event as the cameras going
// away - the status line says so rather than leaving it to be discovered.

#include <windows.h>

#include <commctrl.h>
#include <mfapi.h>

#include <string>

#include "camera_manager.h"
#include "strings.h"
#include "vcamsource/vcam_guids.h"

#pragma comment(lib, "comctl32.lib")

namespace {

using vcam::Str;
using vcam::Text;

enum ControlId : int {
  kIdList = 1001,
  kIdName = 1002,
  kIdAdd = 1003,
  kIdRemove = 1004,
  kIdRemoveAll = 1005,
  kIdStatus = 1006,
  kIdHint = 1007,
};

enum MenuId : int {
  kIdLangEnglish = 2001,
  kIdLangKorean = 2002,
};

constexpr wchar_t kWindowClass[] = L"VCamBenchMainWindow";

// Two instances would each suggest "VCamBench 1" and happily create two cameras
// with the same name that nobody can tell apart - the duplicate check only sees
// this process's own list. The installer uses the same name to notice a running
// copy before it replaces files.
constexpr wchar_t kSingleInstanceMutex[] = L"Local\\VCamBenchSingleInstance";

vcam::CameraManager* g_manager = nullptr;
HFONT g_font = nullptr;

// --- small helpers ----------------------------------------------------------

int Scaled(HWND hwnd, int value) {
  const UINT dpi = ::GetDpiForWindow(hwnd);
  return ::MulDiv(value, static_cast<int>(dpi == 0 ? 96 : dpi), 96);
}

HFONT CreateUiFont(UINT dpi) {
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  if (::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi)) {
    return ::CreateFontIndirectW(&metrics.lfMessageFont);
  }
  return static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
}

void ApplyFont(HWND parent) {
  ::EnumChildWindows(
      parent,
      [](HWND child, LPARAM font) -> BOOL {
        ::SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(g_font));
}

HWND Child(HWND parent, int id) {
  return ::GetDlgItem(parent, id);
}

void SetText(HWND parent, int id, const std::wstring& text) {
  ::SetWindowTextW(Child(parent, id), text.c_str());
}

// --- view -------------------------------------------------------------------

void RefreshList(HWND hwnd) {
  HWND list = Child(hwnd, kIdList);
  const int previous = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
  ::SendMessageW(list, LB_RESETCONTENT, 0, 0);

  for (size_t i = 0; i < g_manager->Count(); ++i) {
    ::SendMessageW(list, LB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(g_manager->NameAt(i).c_str()));
  }

  const int count = static_cast<int>(g_manager->Count());
  if (count > 0) {
    // Keep the selection where the user left it; after a removal that means the
    // row that slid up into the same position.
    const int select = (previous >= 0 && previous < count) ? previous : count - 1;
    ::SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
  }

  const bool any = count > 0;
  ::EnableWindow(Child(hwnd, kIdRemove), any);
  ::EnableWindow(Child(hwnd, kIdRemoveAll), any);

  std::wstring status;
  if (count == 0) {
    status = Text(Str::StatusNone);
  } else if (count == 1) {
    status = Text(Str::StatusOne);
  } else {
    status = vcam::TextCount(Str::StatusMany, static_cast<size_t>(count));
  }
  SetText(hwnd, kIdStatus, status);

  // Always offer the next free name so Add is a single click.
  SetText(hwnd, kIdName, g_manager->SuggestName());
}

std::wstring ReadNameBox(HWND hwnd) {
  wchar_t buffer[128] = {};
  ::GetWindowTextW(Child(hwnd, kIdName), buffer, ARRAYSIZE(buffer));
  std::wstring name(buffer);
  while (!name.empty() && name.front() == L' ') name.erase(name.begin());
  while (!name.empty() && name.back() == L' ') name.pop_back();
  return name;
}

void ShowError(HWND hwnd, const std::wstring& what, const std::wstring& detail) {
  const std::wstring text = what + L"\n\n" + detail;
  ::MessageBoxW(hwnd, text.c_str(), VCAM_PRODUCT_NAME, MB_OK | MB_ICONWARNING);
}

// --- actions ----------------------------------------------------------------

void OnAdd(HWND hwnd) {
  std::wstring name = ReadNameBox(hwnd);
  if (name.empty()) name = g_manager->SuggestName();

  std::wstring error;
  if (FAILED(g_manager->Add(name, &error))) {
    ShowError(hwnd, Text(Str::ErrAddFailed), error);
  }
  RefreshList(hwnd);
}

void OnRemove(HWND hwnd) {
  HWND list = Child(hwnd, kIdList);
  const int index = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
  if (index < 0) return;

  std::wstring error;
  if (FAILED(g_manager->RemoveAt(static_cast<size_t>(index), &error))) {
    ShowError(hwnd, Text(Str::ErrRemoveFailed), error);
  }
  RefreshList(hwnd);
}

void OnRemoveAll(HWND hwnd) {
  g_manager->RemoveAll();
  RefreshList(hwnd);
}

// --- layout -----------------------------------------------------------------

// Anchored from the bottom: the status line, the buttons and the name row have
// fixed heights and must always be fully on screen, so they are placed first
// and the list takes whatever is left. Stacking downwards from the top instead
// pushes the last row off the edge whenever the arithmetic is off by a few
// pixels - which is exactly what it did.
void Layout(HWND hwnd) {
  RECT rc = {};
  ::GetClientRect(hwnd, &rc);

  const int pad = Scaled(hwnd, 12);
  const int gap = Scaled(hwnd, 8);
  const int rowH = Scaled(hwnd, 28);
  const int hintH = Scaled(hwnd, 20);
  const int statusH = Scaled(hwnd, 20);
  const int width = rc.right - pad * 2;
  if (width <= 0) return;

  int bottom = rc.bottom - pad;

  ::MoveWindow(Child(hwnd, kIdStatus), pad, bottom - statusH, width, statusH, TRUE);
  bottom -= statusH + gap;

  const int halfW = (width - gap) / 2;
  ::MoveWindow(Child(hwnd, kIdRemove), pad, bottom - rowH, halfW, rowH, TRUE);
  ::MoveWindow(Child(hwnd, kIdRemoveAll), pad + halfW + gap, bottom - rowH, width - halfW - gap,
               rowH, TRUE);
  bottom -= rowH + gap;

  const int addW = Scaled(hwnd, 96);
  ::MoveWindow(Child(hwnd, kIdName), pad, bottom - rowH, width - addW - gap, rowH, TRUE);
  ::MoveWindow(Child(hwnd, kIdAdd), pad + width - addW, bottom - rowH, addW, rowH, TRUE);
  bottom -= rowH + gap;

  const int top = pad;
  ::MoveWindow(Child(hwnd, kIdHint), pad, top, width, hintH, TRUE);

  const int listTop = top + hintH + gap;
  const int listH = bottom - listTop;
  ::MoveWindow(Child(hwnd, kIdList), pad, listTop, width, listH > rowH ? listH : rowH, TRUE);
}

// --- language ---------------------------------------------------------------

// Rebuilt rather than relabelled on every switch: the menu is three items, and
// SetMenuItemInfo for each of them is more code than throwing it away.
void InstallMenu(HWND hwnd) {
  HMENU previous = ::GetMenu(hwnd);

  HMENU languages = ::CreatePopupMenu();
  ::AppendMenuW(languages, MF_STRING, kIdLangEnglish, Text(Str::MenuEnglish));
  ::AppendMenuW(languages, MF_STRING, kIdLangKorean, Text(Str::MenuKorean));

  HMENU bar = ::CreateMenu();
  ::AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(languages), Text(Str::MenuLanguage));

  ::SetMenu(hwnd, bar);
  if (previous) ::DestroyMenu(previous);

  ::CheckMenuRadioItem(bar, kIdLangEnglish, kIdLangKorean,
                       vcam::CurrentLanguage() == vcam::Lang::Korean ? kIdLangKorean
                                                                     : kIdLangEnglish,
                       MF_BYCOMMAND);
  ::DrawMenuBar(hwnd);
}

void ApplyLanguage(HWND hwnd, vcam::Lang lang) {
  if (lang == vcam::CurrentLanguage()) return;
  vcam::SetLanguage(lang);

  InstallMenu(hwnd);
  SetText(hwnd, kIdHint, Text(Str::Hint));
  SetText(hwnd, kIdAdd, Text(Str::ButtonAdd));
  SetText(hwnd, kIdRemove, Text(Str::ButtonRemove));
  SetText(hwnd, kIdRemoveAll, Text(Str::ButtonRemoveAll));

  // The status line and the suggested name both come from here, and the menu
  // bar may have changed height, so re-lay out rather than only repainting.
  RefreshList(hwnd);
  Layout(hwnd);
  ::InvalidateRect(hwnd, nullptr, TRUE);
}

void CreateControls(HWND hwnd) {
  const HINSTANCE instance =
      reinterpret_cast<HINSTANCE>(::GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));

  const auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
    ::CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
  };

  make(L"STATIC", Text(Str::Hint), 0, kIdHint);
  make(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY, kIdList);
  make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, kIdName);
  make(L"BUTTON", Text(Str::ButtonAdd), BS_DEFPUSHBUTTON, kIdAdd);
  make(L"BUTTON", Text(Str::ButtonRemove), BS_PUSHBUTTON, kIdRemove);
  make(L"BUTTON", Text(Str::ButtonRemoveAll), BS_PUSHBUTTON, kIdRemoveAll);
  make(L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, kIdStatus);

  g_font = CreateUiFont(::GetDpiForWindow(hwnd));
  ApplyFont(hwnd);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      CreateControls(hwnd);
      InstallMenu(hwnd);
      RefreshList(hwnd);
      return 0;

    case WM_SIZE:
      Layout(hwnd);
      return 0;

    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
      info->ptMinTrackSize.x = 380;
      info->ptMinTrackSize.y = 320;
      return 0;
    }

    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        case kIdAdd:
          OnAdd(hwnd);
          return 0;
        case kIdRemove:
          OnRemove(hwnd);
          return 0;
        case kIdRemoveAll:
          OnRemoveAll(hwnd);
          return 0;
        case kIdLangEnglish:
          ApplyLanguage(hwnd, vcam::Lang::English);
          return 0;
        case kIdLangKorean:
          ApplyLanguage(hwnd, vcam::Lang::Korean);
          return 0;
        default:
          break;
      }
      return 0;

    case WM_CLOSE:
      // Removing here rather than in WM_DESTROY keeps the window on screen
      // until Windows has actually dropped the devices.
      g_manager->RemoveAll();
      ::DestroyWindow(hwnd);
      return 0;

    case WM_DESTROY:
      ::PostQuitMessage(0);
      return 0;

    default:
      break;
  }
  return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand) {
  ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  // Before any string is read: picks up a previous choice, else follows Windows.
  vcam::LoadLanguagePreference();

  // Held for the life of the process; released by Windows when it exits.
  HANDLE single = ::CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
  if (single && ::GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND existing = ::FindWindowW(kWindowClass, nullptr);
    if (existing) {
      if (::IsIconic(existing)) ::ShowWindow(existing, SW_RESTORE);
      ::SetForegroundWindow(existing);
    }
    ::CloseHandle(single);
    return 0;
  }

  HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr)) {
    ::MessageBoxW(nullptr, vcam::ExplainHresult(hr).c_str(), VCAM_PRODUCT_NAME,
                  MB_OK | MB_ICONERROR);
    return 1;
  }
  hr = ::MFStartup(MF_VERSION, MFSTARTUP_LITE);
  if (FAILED(hr)) {
    ::MessageBoxW(nullptr, vcam::ExplainHresult(hr).c_str(), VCAM_PRODUCT_NAME,
                  MB_OK | MB_ICONERROR);
    ::CoUninitialize();
    return 1;
  }

  int exitCode = 0;
  {
    std::wstring error;
    if (!vcam::CameraManager::IsSupported(&error)) {
      ::MessageBoxW(nullptr, error.c_str(), VCAM_PRODUCT_NAME, MB_OK | MB_ICONERROR);
      ::MFShutdown();
      ::CoUninitialize();
      return 1;
    }

    vcam::CameraManager manager;
    g_manager = &manager;

    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES};
    ::InitCommonControlsEx(&controls);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWindowClass;
    if (!::RegisterClassExW(&wc)) {
      ::MFShutdown();
      ::CoUninitialize();
      return 1;
    }

    HWND hwnd = ::CreateWindowExW(0, kWindowClass, VCAM_PRODUCT_NAME,
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 460, 420,
                                  nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
      ::MFShutdown();
      ::CoUninitialize();
      return 1;
    }

    ::ShowWindow(hwnd, showCommand);
    ::UpdateWindow(hwnd);

    MSG message = {};
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
      if (!::IsDialogMessageW(hwnd, &message)) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
      }
    }
    exitCode = static_cast<int>(message.wParam);

    g_manager = nullptr;
  }

  if (g_font) ::DeleteObject(g_font);
  ::MFShutdown();
  ::CoUninitialize();
  return exitCode;
}
