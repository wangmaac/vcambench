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
#include "vcamsource/vcam_guids.h"

#pragma comment(lib, "comctl32.lib")

namespace {

enum ControlId : int {
  kIdList = 1001,
  kIdName = 1002,
  kIdAdd = 1003,
  kIdRemove = 1004,
  kIdRemoveAll = 1005,
  kIdStatus = 1006,
  kIdHint = 1007,
};

constexpr wchar_t kWindowClass[] = L"VCamBenchMainWindow";

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
    status = L"카메라 없음 — 추가하면 이 PC의 카메라 목록에 나타납니다.";
  } else {
    status = L"카메라 " + std::to_wstring(count) + L"대 실행 중 — 이 창을 닫으면 사라집니다.";
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
    ShowError(hwnd, L"카메라를 만들지 못했습니다.", error);
  }
  RefreshList(hwnd);
}

void OnRemove(HWND hwnd) {
  HWND list = Child(hwnd, kIdList);
  const int index = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
  if (index < 0) return;

  std::wstring error;
  if (FAILED(g_manager->RemoveAt(static_cast<size_t>(index), &error))) {
    ShowError(hwnd, L"카메라를 제거하지 못했습니다.", error);
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

void CreateControls(HWND hwnd) {
  const HINSTANCE instance =
      reinterpret_cast<HINSTANCE>(::GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));

  const auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
    ::CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
  };

  make(L"STATIC", L"만든 카메라는 이 PC의 모든 앱에서 일반 카메라처럼 보입니다.", 0, kIdHint);
  make(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY, kIdList);
  make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, kIdName);
  make(L"BUTTON", L"추가", BS_DEFPUSHBUTTON, kIdAdd);
  make(L"BUTTON", L"선택 제거", BS_PUSHBUTTON, kIdRemove);
  make(L"BUTTON", L"모두 제거", BS_PUSHBUTTON, kIdRemoveAll);
  make(L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, kIdStatus);

  g_font = CreateUiFont(::GetDpiForWindow(hwnd));
  ApplyFont(hwnd);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      CreateControls(hwnd);
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
