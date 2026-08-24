#pragma once

#include <windows.h>

#include <mfvirtualcamera.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace vcam {

// Owns every virtual camera this process has created.
//
// One process, many cameras. Each camera is an independent IMFVirtualCamera
// registered against the same media source CLSID but with its own friendly
// name, so Windows lists them as separate devices.
//
// Cameras live exactly as long as this process does: they are created with
// MFVirtualCameraLifetime_Session, so a crash or a kill removes them too.
class CameraManager {
 public:
  struct Camera {
    std::wstring name;
    Microsoft::WRL::ComPtr<IMFVirtualCamera> camera;
  };

  CameraManager() = default;
  ~CameraManager();

  CameraManager(const CameraManager&) = delete;
  CameraManager& operator=(const CameraManager&) = delete;

  // True when this Windows build supports software virtual cameras at all.
  // Populates `error` with a human-readable reason when it does not.
  static bool IsSupported(std::wstring* error);

  // Adds one camera. `name` is what the user sees in every camera picker on the
  // machine, so it must be unique - Windows will happily register two cameras
  // with the same name and then nobody can tell them apart.
  //
  // Returns S_OK, or a failure HRESULT with `error` set to an explanation.
  HRESULT Add(const std::wstring& name, std::wstring* error);

  // Removes the camera at `index`. Out-of-range is a caller bug and returns
  // E_INVALIDARG rather than throwing.
  HRESULT RemoveAt(size_t index, std::wstring* error);

  // Removes every camera. Safe to call twice.
  void RemoveAll();

  size_t Count() const { return cameras_.size(); }
  const std::wstring& NameAt(size_t index) const { return cameras_[index].name; }
  bool HasName(const std::wstring& name) const;

  // A name the user has not taken yet, e.g. "VCamBench 3".
  std::wstring SuggestName() const;

 private:
  std::vector<Camera> cameras_;
};

// Turns the HRESULTs this path actually produces into something a user can act
// on. Falls back to FormatMessage for everything else.
std::wstring ExplainHresult(HRESULT hr);

}  // namespace vcam
