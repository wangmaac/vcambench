// COM in-proc server entry points for the virtual camera media source.

#include <windows.h>

#include <wrl/client.h>
#include <wrl/implements.h>

#include <atomic>
#include <cstdio>
#include <cwchar>

#include "logging.h"
#include "media_source.h"
#include "module.h"
#include "vcam_guids.h"

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::MakeAndInitialize;

namespace vcam {
namespace {

std::atomic<long> g_objectCount{0};
HMODULE g_module = nullptr;

class VCamClassFactory
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IClassFactory> {
 public:
  VCamClassFactory() { ModuleAddRef(); }
  ~VCamClassFactory() override { ModuleRelease(); }

  IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** object) override {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;

    ComPtr<VCamActivate> activate;
    HRESULT hr = MakeAndInitialize<VCamActivate>(&activate);
    if (FAILED(hr)) {
      LogHr("MakeAndInitialize<VCamActivate>", hr);
      return hr;
    }
    return activate.CopyTo(riid, object);
  }

  IFACEMETHODIMP LockServer(BOOL lock) override {
    if (lock) {
      ModuleAddRef();
    } else {
      ModuleRelease();
    }
    return S_OK;
  }
};

HRESULT ModulePath(wchar_t* buffer, DWORD count) {
  const DWORD n = ::GetModuleFileNameW(g_module, buffer, count);
  if (n == 0 || n >= count) return HRESULT_FROM_WIN32(::GetLastError());
  return S_OK;
}

HRESULT SetRegString(HKEY key, const wchar_t* name, const wchar_t* value) {
  const DWORD bytes = static_cast<DWORD>((::wcslen(value) + 1) * sizeof(wchar_t));
  const LSTATUS s = ::RegSetValueExW(key, name, 0, REG_SZ,
                                     reinterpret_cast<const BYTE*>(value), bytes);
  return s == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(s);
}

}  // namespace

void ModuleAddRef() {
  g_objectCount.fetch_add(1, std::memory_order_relaxed);
}

void ModuleRelease() {
  g_objectCount.fetch_sub(1, std::memory_order_relaxed);
}

long ModuleObjectCount() {
  return g_objectCount.load(std::memory_order_relaxed);
}

}  // namespace vcam

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
  switch (reason) {
    case DLL_PROCESS_ATTACH:
      vcam::g_module = module;
      ::DisableThreadLibraryCalls(module);
      vcam::LogInit();
      vcam::Logf("DLL loaded into pid=%lu", ::GetCurrentProcessId());
      break;
    case DLL_PROCESS_DETACH:
      vcam::Logf("DLL unloading (objects=%ld)", vcam::ModuleObjectCount());
      vcam::LogShutdown();
      break;
    default:
      break;
  }
  return TRUE;
}

_Check_return_ STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid,
                                        _Outptr_ LPVOID* object) {
  if (!object) return E_POINTER;
  *object = nullptr;

  if (rclsid != vcam::kClsidVCamMediaSource) {
    vcam::LogGuid("DllGetClassObject for unknown CLSID", rclsid);
    return CLASS_E_CLASSNOTAVAILABLE;
  }

  ComPtr<vcam::VCamClassFactory> factory = Microsoft::WRL::Make<vcam::VCamClassFactory>();
  if (!factory) return E_OUTOFMEMORY;
  return factory.CopyTo(riid, object);
}

__control_entrypoint(DllExport) STDAPI DllCanUnloadNow() {
  return vcam::ModuleObjectCount() == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
  wchar_t path[MAX_PATH] = {};
  HRESULT hr = vcam::ModulePath(path, ARRAYSIZE(path));
  if (FAILED(hr)) return hr;

  wchar_t clsidKey[128] = {};
  ::swprintf_s(clsidKey, L"CLSID\\%s", VCAM_CLSID_STRING);

  HKEY key = nullptr;
  LSTATUS s = ::RegCreateKeyExW(HKEY_CLASSES_ROOT, clsidKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                KEY_WRITE, nullptr, &key, nullptr);
  if (s != ERROR_SUCCESS) return HRESULT_FROM_WIN32(s);

  hr = vcam::SetRegString(key, nullptr, VCAM_PRODUCT_NAME L" Media Source");

  HKEY inproc = nullptr;
  if (SUCCEEDED(hr)) {
    s = ::RegCreateKeyExW(key, L"InprocServer32", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE,
                          nullptr, &inproc, nullptr);
    hr = (s == ERROR_SUCCESS) ? S_OK : HRESULT_FROM_WIN32(s);
  }
  if (SUCCEEDED(hr)) hr = vcam::SetRegString(inproc, nullptr, path);
  // "Both" lets the Frame Server create the source on whichever apartment it
  // is already running, avoiding a marshalling proxy on the frame path.
  if (SUCCEEDED(hr)) hr = vcam::SetRegString(inproc, L"ThreadingModel", L"Both");

  if (inproc) ::RegCloseKey(inproc);
  ::RegCloseKey(key);
  return hr;
}

STDAPI DllUnregisterServer() {
  wchar_t clsidKey[128] = {};
  ::swprintf_s(clsidKey, L"CLSID\\%s", VCAM_CLSID_STRING);

  const LSTATUS s = ::RegDeleteTreeW(HKEY_CLASSES_ROOT, clsidKey);
  if (s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND) return S_OK;
  return HRESULT_FROM_WIN32(s);
}
