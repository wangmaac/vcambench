// camlist - enumerate video capture devices through each Windows API separately.
//
// Windows does not have one camera list. Apps see different lists depending on
// which API they use, so "is my virtual camera visible?" has a different answer
// per API. This tool reports each path on its own.
//
//   --mf      Media Foundation      (Windows Camera app, Teams, Chrome/Edge, modern Zoom)
//   --dshow   DirectShow            (older desktop apps: legacy Skype, some OBS setups)
//   --winrt   WinRT DeviceInformation (UWP / MediaCapture apps)
//   --all     all of the above (default)
//
//   --save <file>   write a snapshot
//   --diff <file>   compare against a snapshot and report added/removed devices

#include <windows.h>

#include <dshow.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#ifdef VCAM_HAVE_CPPWINRT
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/base.h>
#endif

namespace {

struct Device {
  std::string api;   // "MF" | "DSHOW" | "WINRT"
  std::string name;  // friendly name
  std::string id;    // symbolic link / device path / device id
};

std::string ToUtf8(const wchar_t* s, int lenChars = -1) {
  if (!s) return {};
  int n = ::WideCharToMultiByte(CP_UTF8, 0, s, lenChars, nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string out(static_cast<size_t>(n), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, s, lenChars, out.data(), n, nullptr, nullptr);
  if (lenChars == -1 && !out.empty() && out.back() == '\0') out.pop_back();
  return out;
}

// Collapse tabs/newlines so a device can round-trip through the snapshot format.
std::string Sanitize(std::string s) {
  for (char& c : s) {
    if (c == '\t' || c == '\r' || c == '\n') c = ' ';
  }
  return s;
}

void PrintHr(const char* what, HRESULT hr) {
  std::printf("  ! %s failed (hr=0x%08lX)\n", what, static_cast<unsigned long>(hr));
}

// --- Media Foundation ------------------------------------------------------

std::vector<Device> EnumMediaFoundation(bool* ok) {
  std::vector<Device> out;
  *ok = false;

  IMFAttributes* attrs = nullptr;
  HRESULT hr = ::MFCreateAttributes(&attrs, 1);
  if (FAILED(hr)) {
    PrintHr("MFCreateAttributes", hr);
    return out;
  }
  hr = attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (FAILED(hr)) {
    PrintHr("SetGUID(VIDCAP)", hr);
    attrs->Release();
    return out;
  }

  IMFActivate** devices = nullptr;
  UINT32 count = 0;
  hr = ::MFEnumDeviceSources(attrs, &devices, &count);
  attrs->Release();
  if (FAILED(hr)) {
    PrintHr("MFEnumDeviceSources", hr);
    return out;
  }

  for (UINT32 i = 0; i < count; ++i) {
    Device d;
    d.api = "MF";

    WCHAR* buf = nullptr;
    UINT32 len = 0;
    if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &buf, &len))) {
      d.name = ToUtf8(buf, static_cast<int>(len));
      ::CoTaskMemFree(buf);
    }
    buf = nullptr;
    len = 0;
    if (SUCCEEDED(devices[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &buf, &len))) {
      d.id = ToUtf8(buf, static_cast<int>(len));
      ::CoTaskMemFree(buf);
    }
    out.push_back(std::move(d));
    devices[i]->Release();
  }
  ::CoTaskMemFree(devices);

  *ok = true;
  return out;
}

// --- DirectShow ------------------------------------------------------------

std::vector<Device> EnumDirectShow(bool* ok) {
  std::vector<Device> out;
  *ok = false;

  ICreateDevEnum* devEnum = nullptr;
  HRESULT hr = ::CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&devEnum));
  if (FAILED(hr)) {
    PrintHr("CoCreateInstance(SystemDeviceEnum)", hr);
    return out;
  }

  IEnumMoniker* monikers = nullptr;
  hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &monikers, 0);
  devEnum->Release();

  // S_FALSE with a null enumerator means "category is empty" - not an error.
  if (hr == S_FALSE || !monikers) {
    *ok = true;
    return out;
  }
  if (FAILED(hr)) {
    PrintHr("CreateClassEnumerator", hr);
    return out;
  }

  IMoniker* moniker = nullptr;
  while (monikers->Next(1, &moniker, nullptr) == S_OK) {
    Device d;
    d.api = "DSHOW";

    IPropertyBag* bag = nullptr;
    if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&bag)))) {
      VARIANT v;
      ::VariantInit(&v);
      if (SUCCEEDED(bag->Read(L"FriendlyName", &v, nullptr)) && v.vt == VT_BSTR) {
        d.name = ToUtf8(v.bstrVal);
      }
      ::VariantClear(&v);

      ::VariantInit(&v);
      if (SUCCEEDED(bag->Read(L"DevicePath", &v, nullptr)) && v.vt == VT_BSTR) {
        d.id = ToUtf8(v.bstrVal);
      }
      ::VariantClear(&v);
      bag->Release();
    }

    // Software-only filters have no DevicePath; fall back to the display name.
    if (d.id.empty()) {
      LPOLESTR display = nullptr;
      if (SUCCEEDED(moniker->GetDisplayName(nullptr, nullptr, &display)) && display) {
        d.id = ToUtf8(display);
        ::CoTaskMemFree(display);
      }
    }

    out.push_back(std::move(d));
    moniker->Release();
    moniker = nullptr;
  }
  monikers->Release();

  *ok = true;
  return out;
}

// --- WinRT -----------------------------------------------------------------

std::vector<Device> EnumWinRt(bool* ok) {
  std::vector<Device> out;
  *ok = false;
#ifdef VCAM_HAVE_CPPWINRT
  try {
    using namespace winrt::Windows::Devices::Enumeration;
    auto devices = DeviceInformation::FindAllAsync(DeviceClass::VideoCapture).get();
    for (auto const& info : devices) {
      Device d;
      d.api = "WINRT";
      d.name = ToUtf8(info.Name().c_str());
      d.id = ToUtf8(info.Id().c_str());
      out.push_back(std::move(d));
    }
    *ok = true;
  } catch (winrt::hresult_error const& e) {
    PrintHr("DeviceInformation::FindAllAsync", e.code());
  }
#else
  std::printf("  (built without C++/WinRT support)\n");
#endif
  return out;
}

// --- reporting -------------------------------------------------------------

void PrintSection(const char* title, const std::vector<Device>& devices, bool ok) {
  std::printf("\n== %s ==\n", title);
  if (!ok) {
    std::printf("  enumeration FAILED\n");
    return;
  }
  if (devices.empty()) {
    std::printf("  (none)\n");
  }
  for (size_t i = 0; i < devices.size(); ++i) {
    std::printf("  [%zu] %s\n", i, devices[i].name.c_str());
    std::printf("      %s\n", devices[i].id.c_str());
  }
  std::printf("  -> %zu device(s)\n", devices.size());
}

std::vector<std::string> ToSnapshotLines(const std::vector<Device>& devices) {
  std::vector<std::string> lines;
  lines.reserve(devices.size());
  for (const Device& d : devices) {
    lines.push_back(d.api + "\t" + Sanitize(d.name) + "\t" + Sanitize(d.id));
  }
  std::sort(lines.begin(), lines.end());
  return lines;
}

bool SaveSnapshot(const std::string& path, const std::vector<std::string>& lines) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  for (const std::string& l : lines) f << l << "\n";
  return true;
}

bool LoadSnapshot(const std::string& path, std::vector<std::string>* lines) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) lines->push_back(line);
  }
  std::sort(lines->begin(), lines->end());
  return true;
}

void PrintDiff(const std::vector<std::string>& before, const std::vector<std::string>& after) {
  std::vector<std::string> added, removed;
  std::set_difference(after.begin(), after.end(), before.begin(), before.end(),
                      std::back_inserter(added));
  std::set_difference(before.begin(), before.end(), after.begin(), after.end(),
                      std::back_inserter(removed));

  std::printf("\n== diff vs snapshot ==\n");
  if (added.empty() && removed.empty()) {
    std::printf("  no change\n");
  }
  for (const std::string& l : removed) std::printf("  - %s\n", l.c_str());
  for (const std::string& l : added) std::printf("  + %s\n", l.c_str());
  std::printf("  -> +%zu / -%zu\n", added.size(), removed.size());
}

void PrintUsage() {
  std::printf(
      "camlist - list video capture devices per Windows API\n"
      "\n"
      "usage: camlist [--mf] [--dshow] [--winrt] [--all] [--save <file>] [--diff <file>]\n"
      "\n"
      "  --mf      Media Foundation   (Camera app, Teams, Chrome/Edge, modern Zoom)\n"
      "  --dshow   DirectShow         (legacy desktop apps)\n"
      "  --winrt   WinRT              (UWP / MediaCapture apps)\n"
      "  --all     all paths (default)\n"
      "  --save    write a snapshot of the current list\n"
      "  --diff    compare the current list against a snapshot\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  ::SetConsoleOutputCP(CP_UTF8);

  bool wantMf = false, wantDshow = false, wantWinrt = false;
  std::string savePath, diffPath;

  for (int i = 1; i < argc; ++i) {
    std::wstring a = argv[i];
    if (a == L"--mf") {
      wantMf = true;
    } else if (a == L"--dshow") {
      wantDshow = true;
    } else if (a == L"--winrt") {
      wantWinrt = true;
    } else if (a == L"--all") {
      wantMf = wantDshow = wantWinrt = true;
    } else if (a == L"--save" && i + 1 < argc) {
      savePath = ToUtf8(argv[++i]);
    } else if (a == L"--diff" && i + 1 < argc) {
      diffPath = ToUtf8(argv[++i]);
    } else if (a == L"--help" || a == L"-h" || a == L"/?") {
      PrintUsage();
      return 0;
    } else {
      std::printf("unknown argument: %s\n\n", ToUtf8(a.c_str()).c_str());
      PrintUsage();
      return 2;
    }
  }
  if (!wantMf && !wantDshow && !wantWinrt) wantMf = wantDshow = wantWinrt = true;

  // MTA so DirectShow, MF and WinRT enumeration all work from this thread.
  HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr)) {
    PrintHr("CoInitializeEx", hr);
    return 1;
  }
  hr = ::MFStartup(MF_VERSION, MFSTARTUP_LITE);
  if (FAILED(hr)) {
    PrintHr("MFStartup", hr);
    ::CoUninitialize();
    return 1;
  }

  std::vector<std::string> all;

  if (wantMf) {
    bool ok = false;
    auto d = EnumMediaFoundation(&ok);
    PrintSection("Media Foundation", d, ok);
    auto lines = ToSnapshotLines(d);
    all.insert(all.end(), lines.begin(), lines.end());
  }
  if (wantDshow) {
    bool ok = false;
    auto d = EnumDirectShow(&ok);
    PrintSection("DirectShow", d, ok);
    auto lines = ToSnapshotLines(d);
    all.insert(all.end(), lines.begin(), lines.end());
  }
  if (wantWinrt) {
    bool ok = false;
    auto d = EnumWinRt(&ok);
    PrintSection("WinRT DeviceInformation", d, ok);
    auto lines = ToSnapshotLines(d);
    all.insert(all.end(), lines.begin(), lines.end());
  }

  std::sort(all.begin(), all.end());

  int rc = 0;
  if (!diffPath.empty()) {
    std::vector<std::string> before;
    if (!LoadSnapshot(diffPath, &before)) {
      std::printf("\n! cannot read snapshot: %s\n", diffPath.c_str());
      rc = 1;
    } else {
      PrintDiff(before, all);
    }
  }
  if (!savePath.empty()) {
    if (SaveSnapshot(savePath, all)) {
      std::printf("\nsnapshot written: %s (%zu entries)\n", savePath.c_str(), all.size());
    } else {
      std::printf("\n! cannot write snapshot: %s\n", savePath.c_str());
      rc = 1;
    }
  }

  ::MFShutdown();
  ::CoUninitialize();
  return rc;
}
