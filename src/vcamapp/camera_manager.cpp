#include "camera_manager.h"

#include <mfapi.h>
#include <mferror.h>

#include <algorithm>

#include "strings.h"
#include "vcamsource/vcam_guids.h"

using Microsoft::WRL::ComPtr;

namespace vcam {
namespace {

std::wstring FormatSystemMessage(HRESULT hr) {
  wchar_t* text = nullptr;
  const DWORD n = ::FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, static_cast<DWORD>(hr), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<wchar_t*>(&text), 0, nullptr);

  std::wstring out;
  if (n && text) {
    out.assign(text, n);
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n')) out.pop_back();
  }
  if (text) ::LocalFree(text);
  return out;
}

}  // namespace

std::wstring ExplainHresult(HRESULT hr) {
  switch (hr) {
    case REGDB_E_CLASSNOTREG:
      return Text(Str::ErrNotRegistered);
    case E_ACCESSDENIED:
      return Text(Str::ErrAccessDenied);
    case MF_E_NOT_AVAILABLE:
    case E_NOTIMPL:
      return Text(Str::ErrUnsupportedWindows);
    case MF_E_INVALIDREQUEST:
      return Text(Str::ErrDuplicateName);
    default:
      break;
  }
  const std::wstring sys = FormatSystemMessage(hr);
  wchar_t code[32] = {};
  ::swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(hr));
  return sys.empty() ? code : (sys + L" (" + code + L")");
}

CameraManager::~CameraManager() {
  RemoveAll();
}

bool CameraManager::IsSupported(std::wstring* error) {
  BOOL supported = FALSE;
  const HRESULT hr =
      ::MFIsVirtualCameraTypeSupported(MFVirtualCameraType_SoftwareCameraSource, &supported);
  if (FAILED(hr)) {
    if (error) *error = ExplainHresult(hr);
    return false;
  }
  if (!supported) {
    if (error) *error = ExplainHresult(E_NOTIMPL);
    return false;
  }
  return true;
}

bool CameraManager::HasName(const std::wstring& name) const {
  return std::any_of(cameras_.begin(), cameras_.end(),
                     [&](const Camera& c) { return c.name == name; });
}

std::wstring CameraManager::SuggestName() const {
  for (int i = 1; i < 1000; ++i) {
    wchar_t buf[128] = {};
    ::swprintf_s(buf, L"%s %d", VCAM_PRODUCT_NAME, i);
    if (!HasName(buf)) return buf;
  }
  return VCAM_PRODUCT_NAME;
}

HRESULT CameraManager::Add(const std::wstring& name, std::wstring* error) {
  if (name.empty()) {
    if (error) *error = Text(Str::ErrEmptyName);
    return E_INVALIDARG;
  }
  // Windows does not reject a duplicate name, it just registers a second camera
  // the user cannot tell apart. Catch it here instead.
  if (HasName(name)) {
    if (error) *error = Text(Str::ErrDuplicateName);
    return MF_E_INVALIDREQUEST;
  }

  ComPtr<IMFVirtualCamera> camera;
  HRESULT hr = ::MFCreateVirtualCamera(
      MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_Session,
      MFVirtualCameraAccess_CurrentUser, name.c_str(), VCAM_CLSID_STRING, nullptr, 0, &camera);
  if (FAILED(hr)) {
    if (error) *error = ExplainHresult(hr);
    return hr;
  }

  hr = camera->Start(nullptr);
  if (FAILED(hr)) {
    // Leaving a created-but-not-started camera behind would show up in the list
    // as a device that produces nothing.
    camera->Remove();
    camera->Shutdown();
    if (error) *error = ExplainHresult(hr);
    return hr;
  }

  cameras_.push_back(Camera{name, std::move(camera)});
  return S_OK;
}

HRESULT CameraManager::RemoveAt(size_t index, std::wstring* error) {
  if (index >= cameras_.size()) {
    if (error) *error = Text(Str::ErrNoSuchCamera);
    return E_INVALIDARG;
  }
  Camera camera = std::move(cameras_[index]);
  cameras_.erase(cameras_.begin() + static_cast<ptrdiff_t>(index));

  if (camera.camera) {
    camera.camera->Stop();
    camera.camera->Remove();
    camera.camera->Shutdown();
  }
  return S_OK;
}

void CameraManager::RemoveAll() {
  while (!cameras_.empty()) {
    RemoveAt(cameras_.size() - 1, nullptr);
  }
}

}  // namespace vcam
