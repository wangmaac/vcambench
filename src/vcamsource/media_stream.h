#pragma once

#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <string>
#include <mutex>
#include <thread>

namespace vcam {

// The single video stream exposed by the virtual camera.
//
// Sample delivery is pull-driven: the Frame Server calls RequestSample, which
// only queues a token and returns. A dedicated thread paces frame production at
// the advertised frame rate. No requests means no frames and no CPU spent, so
// the camera costs nothing while nobody is looking at it.
class VCamMediaStream
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          Microsoft::WRL::ChainInterfaces<IMFMediaStream2, IMFMediaStream,
                                          IMFMediaEventGenerator>> {
 public:
  VCamMediaStream();
  ~VCamMediaStream() override;

  // `parent` is held as a raw pointer on purpose: the source owns this stream,
  // and a strong reference back would be an unbreakable cycle.
  // `label` is drawn on every frame so a viewer can tell which camera this is.
  HRESULT RuntimeClassInitialize(IMFMediaSource* parent, IMFStreamDescriptor* descriptor,
                                 std::string label);

  // IMFMediaEventGenerator
  IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
  IFACEMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
  IFACEMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
  IFACEMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status,
                            const PROPVARIANT* value) override;

  // IMFMediaStream
  IFACEMETHODIMP GetMediaSource(IMFMediaSource** source) override;
  IFACEMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** descriptor) override;
  IFACEMETHODIMP RequestSample(IUnknown* token) override;

  // IMFMediaStream2
  IFACEMETHODIMP SetStreamState(MF_STREAM_STATE state) override;
  IFACEMETHODIMP GetStreamState(MF_STREAM_STATE* state) override;

  // Called by the owning media source.
  HRESULT Start();
  HRESULT Stop();
  HRESULT Shutdown();

 private:
  HRESULT CheckShutdown() const;
  void GeneratorLoop();
  HRESULT DeliverOneFrame(IUnknown* token, uint64_t frameIndex, LONGLONG sampleTime);
  void StopThread();

  mutable std::mutex mutex_;
  std::condition_variable cv_;

  Microsoft::WRL::ComPtr<IMFMediaEventQueue> eventQueue_;
  Microsoft::WRL::ComPtr<IMFStreamDescriptor> descriptor_;
  IMFMediaSource* parent_ = nullptr;  // weak, cleared on Shutdown

  std::deque<Microsoft::WRL::ComPtr<IUnknown>> tokens_;
  std::thread thread_;

  bool shutdown_ = false;
  bool threadStopRequested_ = false;
  MF_STREAM_STATE state_ = MF_STREAM_STATE_STOPPED;

  std::string label_;
  uint64_t frameIndex_ = 0;
  LONGLONG startTime_ = 0;  // MFGetSystemTime units (100ns)
};

}  // namespace vcam
