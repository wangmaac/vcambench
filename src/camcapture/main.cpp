// camcapture - open a camera the way an ordinary app does and save what comes
// out.
//
// This is the counterpart to vcamprobe. Where the probe talks to the media
// source directly, this goes through the whole Windows camera pipeline:
// MFEnumDeviceSources picks the device, and the Frame Server hands frames back
// through a source reader. If the picture arrives here, it will arrive in the
// Camera app and in Chrome too.

#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <vector>

#include "common/nv12_bmp.h"

using Microsoft::WRL::ComPtr;

namespace {

std::string ToUtf8(const wchar_t* s) {
  if (!s) return {};
  const int n = ::WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return {};
  std::string out(static_cast<size_t>(n - 1), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
  return out;
}

bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle) {
  if (needle.empty()) return true;
  const auto lower = [](std::wstring v) {
    for (wchar_t& c : v) c = static_cast<wchar_t>(::towlower(c));
    return v;
  };
  return lower(haystack).find(lower(needle)) != std::wstring::npos;
}

void PrintHr(const char* what, HRESULT hr) {
  std::printf("FAIL  %s (hr=0x%08lX)\n", what, static_cast<unsigned long>(hr));
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  ::SetConsoleOutputCP(CP_UTF8);
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  std::wstring wanted = L"Virtual Test Camera";
  std::wstring outDir = L".\\capture-frames";
  int frameCount = 5;

  for (int i = 1; i < argc; ++i) {
    const std::wstring a = argv[i];
    if (a == L"--name" && i + 1 < argc) {
      wanted = argv[++i];
    } else if (a == L"--out" && i + 1 < argc) {
      outDir = argv[++i];
    } else if (a == L"--frames" && i + 1 < argc) {
      frameCount = _wtoi(argv[++i]);
      if (frameCount < 1) frameCount = 1;
    } else {
      std::printf(
          "usage: camcapture [--name <substring>] [--out <dir>] [--frames <n>]\n"
          "  opens a camera the way an ordinary app does and saves frames as BMP.\n");
      return 2;
    }
  }

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

  int rc = 1;
  IMFActivate** devices = nullptr;
  UINT32 deviceCount = 0;
  {
    ComPtr<IMFAttributes> attrs;
    hr = ::MFCreateAttributes(&attrs, 1);
    if (SUCCEEDED(hr)) {
      hr = attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                          MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    }
    if (SUCCEEDED(hr)) hr = ::MFEnumDeviceSources(attrs.Get(), &devices, &deviceCount);
    if (FAILED(hr)) {
      PrintHr("MFEnumDeviceSources", hr);
      ::MFShutdown();
      ::CoUninitialize();
      return 1;
    }
  }

  ComPtr<IMFMediaSource> source;
  std::wstring chosenName;
  for (UINT32 i = 0; i < deviceCount; ++i) {
    WCHAR* name = nullptr;
    UINT32 len = 0;
    if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name,
                                                 &len))) {
      const std::wstring friendly(name, len);
      if (!source && ContainsNoCase(friendly, wanted)) {
        chosenName = friendly;
        const HRESULT ahr = devices[i]->ActivateObject(IID_PPV_ARGS(&source));
        if (FAILED(ahr)) PrintHr("ActivateObject", ahr);
      }
      ::CoTaskMemFree(name);
    }
  }
  for (UINT32 i = 0; i < deviceCount; ++i) devices[i]->Release();
  ::CoTaskMemFree(devices);

  if (!source) {
    std::printf("FAIL  no camera matching \"%s\" (%u enumerated)\n",
                ToUtf8(wanted.c_str()).c_str(), deviceCount);
    ::MFShutdown();
    ::CoUninitialize();
    return 1;
  }
  std::printf("ok    opened: %s\n", ToUtf8(chosenName.c_str()).c_str());

  ComPtr<IMFSourceReader> reader;
  {
    ComPtr<IMFAttributes> attrs;
    hr = ::MFCreateAttributes(&attrs, 1);
    // Lets the reader insert a converter if the device does not offer NV12
    // natively, so this tool works against real webcams too.
    if (SUCCEEDED(hr)) hr = attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (SUCCEEDED(hr)) {
      hr = ::MFCreateSourceReaderFromMediaSource(source.Get(), attrs.Get(), &reader);
    }
    if (FAILED(hr)) {
      PrintHr("MFCreateSourceReaderFromMediaSource", hr);
      source->Shutdown();
      ::MFShutdown();
      ::CoUninitialize();
      return 1;
    }
  }

  ComPtr<IMFMediaType> wantType;
  hr = ::MFCreateMediaType(&wantType);
  if (SUCCEEDED(hr)) hr = wantType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (SUCCEEDED(hr)) hr = wantType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
  if (SUCCEEDED(hr)) {
    hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr,
                                     wantType.Get());
  }
  if (FAILED(hr)) {
    PrintHr("SetCurrentMediaType(NV12)", hr);
    source->Shutdown();
    ::MFShutdown();
    ::CoUninitialize();
    return 1;
  }

  UINT32 width = 0, height = 0;
  {
    ComPtr<IMFMediaType> actual;
    if (SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &actual))) {
      ::MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &width, &height);
    }
  }
  if (width == 0 || height == 0) {
    std::printf("FAIL  cannot read the negotiated frame size\n");
    source->Shutdown();
    ::MFShutdown();
    ::CoUninitialize();
    return 1;
  }
  std::printf("ok    negotiated: %ux%u NV12\n", width, height);

  ::CreateDirectoryW(outDir.c_str(), nullptr);

  const size_t expected = static_cast<size_t>(width) * height * 3 / 2;
  int saved = 0;
  int changed = 0;
  int failures = 0;
  LONGLONG previousTime = -1;
  std::vector<uint8_t> previous;

  for (int i = 0; i < frameCount;) {
    DWORD streamIndex = 0, flags = 0;
    LONGLONG timestamp = 0;
    ComPtr<IMFSample> sample;
    hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &streamIndex, &flags,
                            &timestamp, &sample);
    if (FAILED(hr)) {
      PrintHr("ReadSample", hr);
      ++failures;
      break;
    }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
      std::printf("FAIL  stream ended earlier than expected\n");
      ++failures;
      break;
    }
    if (!sample) continue;  // a gap or a format change: ask again

    if (previousTime >= 0 && timestamp < previousTime) {
      std::printf("FAIL  timestamp went backwards (%lld -> %lld)\n", previousTime, timestamp);
      ++failures;
    }
    previousTime = timestamp;

    ComPtr<IMFMediaBuffer> buffer;
    hr = sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr)) {
      PrintHr("ConvertToContiguousBuffer", hr);
      ++failures;
      break;
    }

    BYTE* data = nullptr;
    DWORD maxLength = 0, currentLength = 0;
    hr = buffer->Lock(&data, &maxLength, &currentLength);
    if (FAILED(hr)) {
      PrintHr("Lock", hr);
      ++failures;
      break;
    }
    if (currentLength >= expected) {
      std::vector<uint8_t> pixels(data, data + expected);
      if (!previous.empty() && pixels != previous) ++changed;
      previous = pixels;

      wchar_t path[MAX_PATH] = {};
      ::swprintf_s(path, L"%s\\capture_%03d.bmp", outDir.c_str(), i);
      if (WriteNv12AsBmp(path, pixels.data(), static_cast<int>(width),
                         static_cast<int>(height))) {
        ++saved;
      }
    } else {
      std::printf("FAIL  frame %d is %lu bytes, expected %zu\n", i, currentLength, expected);
      ++failures;
    }
    buffer->Unlock();
    ++i;
  }

  std::printf("\n      %d frame(s) saved, %d consecutive pair(s) differ\n", saved, changed);
  if (saved != frameCount) {
    std::printf("FAIL  only received %d of %d frames\n", saved, frameCount);
    ++failures;
  }
  if (frameCount > 1 && changed == 0) {
    std::printf("FAIL  the picture never changed - it looks frozen\n");
    ++failures;
  }

  reader.Reset();
  source->Shutdown();
  source.Reset();
  ::MFShutdown();
  ::CoUninitialize();

  rc = failures == 0 ? 0 : 1;
  std::printf("\n%s\n", rc == 0 ? "CAPTURE PASSED" : "CAPTURE FAILED");
  return rc;
}
