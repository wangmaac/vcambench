#include "media_stream.h"

#include <mfobjects.h>

#include <chrono>

#include "logging.h"
#include "vcam_guids.h"
#include "vcamcore/frame_pattern.h"

using Microsoft::WRL::ComPtr;

namespace vcam {
namespace {

constexpr LONGLONG kFrameDuration100ns =
    10000000LL * vcamcore::kFpsDenominator / vcamcore::kFpsNumerator;

// Placeholder. Every camera currently draws the same label because the media
// source runs inside the Frame Server and has no channel back to the host that
// created it. Giving each camera its own on-screen name is the next task.
constexpr const char* kStreamLabel = VCAM_PRODUCT_NAME_A;

}  // namespace

VCamMediaStream::VCamMediaStream() = default;

VCamMediaStream::~VCamMediaStream() {
  // Shutdown is the normal path; this only covers a source that was dropped
  // without one. Leaving the thread running past destruction would fault
  // inside the Frame Server.
  StopThread();
}

HRESULT VCamMediaStream::RuntimeClassInitialize(IMFMediaSource* parent,
                                                IMFStreamDescriptor* descriptor) {
  if (!parent || !descriptor) return E_POINTER;

  HRESULT hr = ::MFCreateEventQueue(&eventQueue_);
  if (FAILED(hr)) {
    LogHr("MFCreateEventQueue(stream)", hr);
    return hr;
  }
  parent_ = parent;
  descriptor_ = descriptor;
  return S_OK;
}

HRESULT VCamMediaStream::CheckShutdown() const {
  return shutdown_ ? MF_E_SHUTDOWN : S_OK;
}

// --- IMFMediaEventGenerator -------------------------------------------------
// The event queue is captured under the lock and used outside it: these calls
// can block (GetEvent) or re-enter, and holding the stream lock across them
// would deadlock against the generator thread.

IFACEMETHODIMP VCamMediaStream::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) {
  ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    queue = eventQueue_;
  }
  return queue->BeginGetEvent(callback, state);
}

IFACEMETHODIMP VCamMediaStream::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) {
  ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    queue = eventQueue_;
  }
  return queue->EndGetEvent(result, event);
}

IFACEMETHODIMP VCamMediaStream::GetEvent(DWORD flags, IMFMediaEvent** event) {
  ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    queue = eventQueue_;
  }
  return queue->GetEvent(flags, event);
}

IFACEMETHODIMP VCamMediaStream::QueueEvent(MediaEventType type, REFGUID extendedType,
                                           HRESULT status, const PROPVARIANT* value) {
  ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    queue = eventQueue_;
  }
  return queue->QueueEventParamVar(type, extendedType, status, value);
}

// --- IMFMediaStream ---------------------------------------------------------

IFACEMETHODIMP VCamMediaStream::GetMediaSource(IMFMediaSource** source) {
  if (!source) return E_POINTER;
  *source = nullptr;

  std::lock_guard<std::mutex> lock(mutex_);
  HRESULT hr = CheckShutdown();
  if (FAILED(hr)) return hr;
  if (!parent_) return MF_E_SHUTDOWN;

  parent_->AddRef();
  *source = parent_;
  return S_OK;
}

IFACEMETHODIMP VCamMediaStream::GetStreamDescriptor(IMFStreamDescriptor** descriptor) {
  if (!descriptor) return E_POINTER;
  *descriptor = nullptr;

  std::lock_guard<std::mutex> lock(mutex_);
  HRESULT hr = CheckShutdown();
  if (FAILED(hr)) return hr;
  return descriptor_.CopyTo(descriptor);
}

IFACEMETHODIMP VCamMediaStream::RequestSample(IUnknown* token) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    if (state_ != MF_STREAM_STATE_RUNNING) return MF_E_INVALIDREQUEST;
    tokens_.emplace_back(token);
  }
  cv_.notify_all();
  return S_OK;
}

// --- IMFMediaStream2 --------------------------------------------------------

IFACEMETHODIMP VCamMediaStream::SetStreamState(MF_STREAM_STATE state) {
  switch (state) {
    case MF_STREAM_STATE_RUNNING:
      return Start();
    case MF_STREAM_STATE_STOPPED:
    case MF_STREAM_STATE_PAUSED:
      return Stop();
    default:
      return E_INVALIDARG;
  }
}

IFACEMETHODIMP VCamMediaStream::GetStreamState(MF_STREAM_STATE* state) {
  if (!state) return E_POINTER;
  std::lock_guard<std::mutex> lock(mutex_);
  HRESULT hr = CheckShutdown();
  if (FAILED(hr)) return hr;
  *state = state_;
  return S_OK;
}

// --- lifecycle --------------------------------------------------------------

HRESULT VCamMediaStream::Start() {
  ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    if (state_ == MF_STREAM_STATE_RUNNING) return S_OK;

    state_ = MF_STREAM_STATE_RUNNING;
    threadStopRequested_ = false;
    frameIndex_ = 0;
    startTime_ = ::MFGetSystemTime();
    queue = eventQueue_;

    if (!thread_.joinable()) {
      thread_ = std::thread([this] { GeneratorLoop(); });
    }
  }
  Logf("stream started");
  return queue->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
}

HRESULT VCamMediaStream::Stop() {
  ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    if (state_ == MF_STREAM_STATE_STOPPED) return S_OK;

    state_ = MF_STREAM_STATE_STOPPED;
    tokens_.clear();
    queue = eventQueue_;
  }
  cv_.notify_all();
  Logf("stream stopped after %llu frame(s)", static_cast<unsigned long long>(frameIndex_));
  return queue->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
}

HRESULT VCamMediaStream::Shutdown() {
  ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) return S_OK;
    shutdown_ = true;
    state_ = MF_STREAM_STATE_STOPPED;
    tokens_.clear();
    queue = eventQueue_;
    parent_ = nullptr;
  }
  StopThread();

  if (queue) queue->Shutdown();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    eventQueue_.Reset();
    descriptor_.Reset();
  }
  Logf("stream shut down");
  return S_OK;
}

void VCamMediaStream::StopThread() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    threadStopRequested_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

// --- frame production -------------------------------------------------------

void VCamMediaStream::GeneratorLoop() {
  // A fault here happens inside the Frame Server and would take down every
  // camera on the machine, so nothing is allowed to escape this frame.
  try {
    for (;;) {
      ComPtr<IUnknown> token;
      uint64_t index = 0;
      LONGLONG dueTime = 0;

      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] {
          return threadStopRequested_ ||
                 (state_ == MF_STREAM_STATE_RUNNING && !tokens_.empty());
        });
        if (threadStopRequested_) return;

        index = frameIndex_;
        dueTime = startTime_ + static_cast<LONGLONG>(index) * kFrameDuration100ns;

        // Pace to the advertised frame rate. Waiting on the condition variable
        // rather than sleeping means Stop/Shutdown interrupts immediately.
        const LONGLONG now = ::MFGetSystemTime();
        if (dueTime > now) {
          const auto waitFor = std::chrono::microseconds((dueTime - now) / 10);
          cv_.wait_for(lock, waitFor, [this] { return threadStopRequested_; });
          if (threadStopRequested_) return;
        }

        // State may have changed while the lock was released.
        if (state_ != MF_STREAM_STATE_RUNNING || tokens_.empty()) continue;

        token = tokens_.front();
        tokens_.pop_front();
        ++frameIndex_;
      }

      const HRESULT hr = DeliverOneFrame(token.Get(), index, ::MFGetSystemTime());
      if (FAILED(hr)) {
        LogHr("DeliverOneFrame", hr);
      }
    }
  } catch (...) {
    Logf("!! generator thread caught an exception; stopping frame production");
  }
}

HRESULT VCamMediaStream::DeliverOneFrame(IUnknown* token, uint64_t frameIndex,
                                         LONGLONG sampleTime) {
  ComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = ::MFCreate2DMediaBuffer(vcamcore::kDefaultWidth, vcamcore::kDefaultHeight,
                                       static_cast<DWORD>(MFVideoFormat_NV12.Data1),
                                       /*bBottomUp=*/FALSE, &buffer);
  if (FAILED(hr)) return hr;

  ComPtr<IMF2DBuffer2> buffer2d;
  hr = buffer.As(&buffer2d);
  if (FAILED(hr)) return hr;

  BYTE* scanline0 = nullptr;
  LONG pitch = 0;
  BYTE* bufferStart = nullptr;
  DWORD bufferLength = 0;
  hr = buffer2d->Lock2DSize(MF2DBuffer_LockFlags_Write, &scanline0, &pitch, &bufferStart,
                            &bufferLength);
  if (FAILED(hr)) return hr;

  if (pitch > 0 && scanline0) {
    vcamcore::Nv12Target target;
    target.y = scanline0;
    target.yStride = pitch;
    target.uv = scanline0 + static_cast<size_t>(pitch) * vcamcore::kDefaultHeight;
    target.uvStride = pitch;
    target.width = vcamcore::kDefaultWidth;
    target.height = vcamcore::kDefaultHeight;

    const uint64_t elapsedMs =
        static_cast<uint64_t>((sampleTime - startTime_) / 10000);
    vcamcore::RenderFrame(target, frameIndex, elapsedMs, kStreamLabel);
  } else {
    // Bottom-up or unusable layout. Skipping the frame beats writing to a
    // pointer whose geometry we have guessed at.
    Logf("!! unexpected 2D buffer pitch=%ld; frame skipped", pitch);
    buffer2d->Unlock2D();
    return E_UNEXPECTED;
  }

  hr = buffer2d->Unlock2D();
  if (FAILED(hr)) return hr;

  hr = buffer->SetCurrentLength(bufferLength);
  if (FAILED(hr)) return hr;

  ComPtr<IMFSample> sample;
  hr = ::MFCreateSample(&sample);
  if (FAILED(hr)) return hr;

  hr = sample->AddBuffer(buffer.Get());
  if (FAILED(hr)) return hr;

  sample->SetSampleTime(sampleTime);
  sample->SetSampleDuration(kFrameDuration100ns);
  sample->SetUINT32(MFSampleExtension_CleanPoint, 1);
  if (token) sample->SetUnknown(MFSampleExtension_Token, token);

  ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) return MF_E_SHUTDOWN;
    queue = eventQueue_;
  }
  if (!queue) return MF_E_SHUTDOWN;

  if (frameIndex == 0) Logf("first frame delivered");
  return queue->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
}

}  // namespace vcam
