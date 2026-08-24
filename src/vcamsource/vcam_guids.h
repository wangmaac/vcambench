#pragma once

#include <guiddef.h>

namespace vcam {

// The CLSID registered by DllRegisterServer and handed to MFCreateVirtualCamera.
// It identifies this media source to the Windows Frame Server, so it must never
// change once anything has been registered with it.
//
// Distinct from the vcamtest prototype's CLSID on purpose: both can be
// installed on the same machine without colliding.
//
// {351A1EA5-CE9E-4A6D-8806-8950D9AF4973}
inline constexpr GUID kClsidVCamMediaSource = {
    0x351a1ea5, 0xce9e, 0x4a6d, {0x88, 0x06, 0x89, 0x50, 0xd9, 0xaf, 0x49, 0x73}};

// PINNAME_VIDEO_CAPTURE. Spelled out rather than pulled from ksmedia.h, which
// only materialises the constant when INITGUID is defined.
inline constexpr GUID kPinNameVideoCapture = {
    0xfb6c4281, 0x0353, 0x11d1, {0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba}};

}  // namespace vcam

#define VCAM_CLSID_STRING L"{351A1EA5-CE9E-4A6D-8806-8950D9AF4973}"

// Product name is provisional - it appears in the camera list, the installer and
// the registry, so settle it before the first public release.
#define VCAM_PRODUCT_NAME L"VCamBench"
#define VCAM_PRODUCT_NAME_A "VCamBench"
