// vcamprobe - exercise vcamsource.dll without registering it and without the
// Windows Frame Server.
//
// The DLL is loaded with LoadLibrary, the class object is fetched straight from
// DllGetClassObject, and the media source is driven by hand. That covers nearly
// all of the media source implementation while needing no administrator rights,
// leaving exactly one question for the on-device test: will the Frame Server
// load an unsigned, regsvr32-registered source?

#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "common/nv12_bmp.h"

using Microsoft::WRL::ComPtr;

namespace {

// {351A1EA5-CE9E-4A6D-8806-8950D9AF4973} - must match vcam_guids.h.
constexpr GUID kClsidVCamMediaSource = {
    0x351a1ea5, 0xce9e, 0x4a6d, {0x88, 0x06, 0x89, 0x50, 0xd9, 0xaf, 0x49, 0x73}};

using DllGetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, LPVOID*);

int g_failures = 0;

void Fail(const char* what, HRESULT hr) {
  std::printf("FAIL  %s (hr=0x%08lX)\n", what, static_cast<unsigned long>(hr));
  ++g_failures;
}

void Pass(const char* what) {
  std::printf("ok    %s\n", what);
}

std::wstring DirectoryOfExecutable() {
  wchar_t path[MAX_PATH] = {};
  const DWORD n = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return L".";
  std::wstring s(path, n);
  const size_t slash = s.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

// Pumps the source's event queue until the stream shows up and the source
// reports that it started.
HRESULT WaitForStream(IMFMediaSource* source, ComPtr<IMFMediaStream>* stream) {
  for (int i = 0; i < 16; ++i) {
    ComPtr<IMFMediaEvent> event;
    HRESULT hr = source->GetEvent(0, &event);  // blocking
    if (FAILED(hr)) return hr;

    MediaEventType type = MEUnknown;
    hr = event->GetType(&type);
    if (FAILED(hr)) return hr;

    if (type == MENewStream || type == MEUpdatedStream) {
      PROPVARIANT var;
      ::PropVariantInit(&var);
      hr = event->GetValue(&var);
      if (SUCCEEDED(hr) && var.vt == VT_UNKNOWN && var.punkVal) {
        hr = var.punkVal->QueryInterface(IID_PPV_ARGS(stream->ReleaseAndGetAddressOf()));
      } else if (SUCCEEDED(hr)) {
        hr = E_UNEXPECTED;
      }
      ::PropVariantClear(&var);
      if (FAILED(hr)) return hr;
    } else if (type == MESourceStarted) {
      return stream->Get() ? S_OK : E_UNEXPECTED;
    } else if (type == MEError) {
      HRESULT status = S_OK;
      event->GetStatus(&status);
      return FAILED(status) ? status : E_FAIL;
    }
  }
  return E_FAIL;
}

HRESULT PullSample(IMFMediaStream* stream, ComPtr<IMFSample>* sample) {
  HRESULT hr = stream->RequestSample(nullptr);
  if (FAILED(hr)) return hr;

  for (int i = 0; i < 16; ++i) {
    ComPtr<IMFMediaEvent> event;
    hr = stream->GetEvent(0, &event);  // blocking
    if (FAILED(hr)) return hr;

    MediaEventType type = MEUnknown;
    hr = event->GetType(&type);
    if (FAILED(hr)) return hr;

    if (type == MEMediaSample) {
      PROPVARIANT var;
      ::PropVariantInit(&var);
      hr = event->GetValue(&var);
      if (SUCCEEDED(hr) && var.vt == VT_UNKNOWN && var.punkVal) {
        hr = var.punkVal->QueryInterface(IID_PPV_ARGS(sample->ReleaseAndGetAddressOf()));
      } else if (SUCCEEDED(hr)) {
        hr = E_UNEXPECTED;
      }
      ::PropVariantClear(&var);
      return hr;
    }
    if (type == MEError) {
      HRESULT status = S_OK;
      event->GetStatus(&status);
      return FAILED(status) ? status : E_FAIL;
    }
    // MEStreamStarted and friends: keep waiting.
  }
  return E_FAIL;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  ::SetConsoleOutputCP(CP_UTF8);
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  int frameCount = 10;
  std::wstring cameraName;
  std::wstring dllPath = DirectoryOfExecutable() + L"\\vcamsource.dll";
  std::wstring outDir = DirectoryOfExecutable() + L"\\probe-frames";

  for (int i = 1; i < argc; ++i) {
    const std::wstring a = argv[i];
    if (a == L"--dll" && i + 1 < argc) {
      dllPath = argv[++i];
    } else if (a == L"--out" && i + 1 < argc) {
      outDir = argv[++i];
    } else if (a == L"--frames" && i + 1 < argc) {
      frameCount = _wtoi(argv[++i]);
      if (frameCount < 1) frameCount = 1;
    } else if (a == L"--name" && i + 1 < argc) {
      cameraName = argv[++i];
    } else {
      std::printf(
          "usage: vcamprobe [--dll <path>] [--out <dir>] [--frames <n>]\n"
          "  Loads the media source DLL directly and saves frames as BMP.\n");
      return 2;
    }
  }

  std::printf("vcamprobe\n  dll : %ls\n  out : %ls\n\n", dllPath.c_str(), outDir.c_str());

  HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr)) {
    Fail("CoInitializeEx", hr);
    return 1;
  }
  hr = ::MFStartup(MF_VERSION, MFSTARTUP_LITE);
  if (FAILED(hr)) {
    Fail("MFStartup", hr);
    ::CoUninitialize();
    return 1;
  }

  HMODULE dll = ::LoadLibraryW(dllPath.c_str());
  if (!dll) {
    Fail("LoadLibrary(vcamsource.dll)", HRESULT_FROM_WIN32(::GetLastError()));
    ::MFShutdown();
    ::CoUninitialize();
    return 1;
  }
  Pass("DLL loaded");

  auto getClassObject =
      reinterpret_cast<DllGetClassObjectFn>(::GetProcAddress(dll, "DllGetClassObject"));
  if (!getClassObject) {
    Fail("GetProcAddress(DllGetClassObject)", HRESULT_FROM_WIN32(::GetLastError()));
    ::MFShutdown();
    ::CoUninitialize();
    return 1;
  }
  Pass("DllGetClassObject exported");

  ComPtr<IClassFactory> factory;
  hr = getClassObject(kClsidVCamMediaSource, IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    Fail("DllGetClassObject", hr);
    ::MFShutdown();
    ::CoUninitialize();
    return 1;
  }
  Pass("class factory obtained");

  ComPtr<IMFActivate> activate;
  hr = factory->CreateInstance(nullptr, IID_PPV_ARGS(&activate));
  if (FAILED(hr)) {
    Fail("CreateInstance(IMFActivate)", hr);
    ::MFShutdown();
    ::CoUninitialize();
    return 1;
  }
  Pass("IMFActivate created");

  // The Frame Server identifies a camera by putting its friendly name on the
  // activate object before activating it. Do the same so the probe renders what
  // the real pipeline would.
  if (!cameraName.empty()) {
    const HRESULT nameHr =
        activate->SetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, cameraName.c_str());
    if (FAILED(nameHr)) Fail("SetString(FRIENDLY_NAME)", nameHr);
    Pass("friendly name set on the activate object");
  } else {
    Pass("activating with no name (fallback path)");
  }

  ComPtr<IMFMediaSource> source;
  hr = activate->ActivateObject(IID_PPV_ARGS(&source));
  if (FAILED(hr)) {
    Fail("ActivateObject(IMFMediaSource)", hr);
    ::MFShutdown();
    ::CoUninitialize();
    return 1;
  }
  Pass("media source activated");

  DWORD characteristics = 0;
  hr = source->GetCharacteristics(&characteristics);
  if (FAILED(hr) || (characteristics & MFMEDIASOURCE_IS_LIVE) == 0) {
    Fail("GetCharacteristics reports a live source", hr);
  } else {
    Pass("source is live");
  }

  ComPtr<IMFPresentationDescriptor> pd;
  hr = source->CreatePresentationDescriptor(&pd);
  if (FAILED(hr)) {
    Fail("CreatePresentationDescriptor", hr);
    ::MFShutdown();
    ::CoUninitialize();
    return 1;
  }
  Pass("presentation descriptor created");

  // Report the advertised format so a mismatch is obvious in the log.
  {
    BOOL selected = FALSE;
    ComPtr<IMFStreamDescriptor> sd;
    if (SUCCEEDED(pd->GetStreamDescriptorByIndex(0, &selected, &sd))) {
      ComPtr<IMFMediaTypeHandler> handler;
      ComPtr<IMFMediaType> type;
      if (SUCCEEDED(sd->GetMediaTypeHandler(&handler)) &&
          SUCCEEDED(handler->GetCurrentMediaType(&type))) {
        UINT32 w = 0, h = 0, num = 0, den = 0;
        ::MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
        ::MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &num, &den);
        std::printf("      advertised: %ux%u @ %u/%u, selected=%d\n", w, h, num, den,
                    selected ? 1 : 0);
      }
    }
  }

  PROPVARIANT start;
  ::PropVariantInit(&start);
  start.vt = VT_EMPTY;
  hr = source->Start(pd.Get(), nullptr, &start);
  ::PropVariantClear(&start);
  if (FAILED(hr)) {
    Fail("Start", hr);
    source->Shutdown();
    ::MFShutdown();
    ::CoUninitialize();
    return 1;
  }
  Pass("source started");

  ComPtr<IMFMediaStream> stream;
  hr = WaitForStream(source.Get(), &stream);
  if (FAILED(hr) || !stream) {
    Fail("MENewStream / MESourceStarted", hr);
    source->Shutdown();
    ::MFShutdown();
    ::CoUninitialize();
    return 1;
  }
  Pass("stream announced");

  ::CreateDirectoryW(outDir.c_str(), nullptr);

  LONGLONG previousTime = -1;
  std::vector<uint8_t> previousPixels;
  int savedFrames = 0;
  int changedFrames = 0;

  for (int i = 0; i < frameCount; ++i) {
    ComPtr<IMFSample> sample;
    hr = PullSample(stream.Get(), &sample);
    if (FAILED(hr)) {
      Fail("PullSample", hr);
      break;
    }

    LONGLONG sampleTime = 0;
    sample->GetSampleTime(&sampleTime);
    if (previousTime >= 0 && sampleTime <= previousTime) {
      std::printf("FAIL  sample time did not advance (%lld -> %lld)\n", previousTime, sampleTime);
      ++g_failures;
    }
    previousTime = sampleTime;

    ComPtr<IMFMediaBuffer> buffer;
    hr = sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr)) {
      Fail("ConvertToContiguousBuffer", hr);
      break;
    }

    BYTE* data = nullptr;
    DWORD maxLength = 0, currentLength = 0;
    hr = buffer->Lock(&data, &maxLength, &currentLength);
    if (FAILED(hr)) {
      Fail("Lock", hr);
      break;
    }

    const DWORD expected = 1280u * 720u * 3u / 2u;
    if (currentLength < expected) {
      std::printf("FAIL  frame %d is %lu bytes, expected at least %lu\n", i, currentLength,
                  expected);
      ++g_failures;
    } else {
      std::vector<uint8_t> pixels(data, data + expected);
      if (!previousPixels.empty() && pixels != previousPixels) ++changedFrames;
      previousPixels = pixels;

      wchar_t path[MAX_PATH] = {};
      ::swprintf_s(path, L"%s\\frame_%03d.bmp", outDir.c_str(), i);
      if (WriteNv12AsBmp(path, pixels.data(), 1280, 720)) ++savedFrames;
    }
    buffer->Unlock();
  }

  std::printf("\n      %d frame(s) saved, %d consecutive pair(s) differed\n", savedFrames,
              changedFrames);
  if (savedFrames == frameCount) {
    Pass("all requested frames delivered");
  } else {
    std::printf("FAIL  only %d of %d frames delivered\n", savedFrames, frameCount);
    ++g_failures;
  }
  if (frameCount > 1 && changedFrames == 0) {
    std::printf("FAIL  the picture never changed - the stream looks frozen\n");
    ++g_failures;
  } else if (frameCount > 1) {
    Pass("picture changes between frames");
  }

  source->Stop();
  source->Shutdown();
  activate->ShutdownObject();
  stream.Reset();
  source.Reset();
  activate.Reset();
  factory.Reset();

  ::MFShutdown();
  ::CoUninitialize();

  std::printf("\n%s\n", g_failures == 0 ? "PROBE PASSED" : "PROBE FAILED");
  return g_failures == 0 ? 0 : 1;
}
