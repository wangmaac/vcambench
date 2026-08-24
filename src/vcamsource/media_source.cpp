#include "media_source.h"

#include <mfobjects.h>

#include "logging.h"
#include "module.h"
#include "vcam_guids.h"
#include "vcamcore/frame_pattern.h"

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::MakeAndInitialize;

namespace vcam {
namespace {

HRESULT CreateVideoMediaType(IMFMediaType** out) {
  ComPtr<IMFMediaType> type;
  HRESULT hr = ::MFCreateMediaType(&type);
  if (FAILED(hr)) return hr;

  hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (FAILED(hr)) return hr;
  hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
  if (FAILED(hr)) return hr;
  hr = ::MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, vcamcore::kDefaultWidth,
                            vcamcore::kDefaultHeight);
  if (FAILED(hr)) return hr;
  hr = ::MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, vcamcore::kFpsNumerator,
                             vcamcore::kFpsDenominator);
  if (FAILED(hr)) return hr;
  hr = ::MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  if (FAILED(hr)) return hr;
  hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (FAILED(hr)) return hr;
  hr = type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
  if (FAILED(hr)) return hr;
  hr = type->SetUINT32(MF_MT_DEFAULT_STRIDE, vcamcore::kDefaultWidth);
  if (FAILED(hr)) return hr;
  hr = type->SetUINT32(MF_MT_SAMPLE_SIZE,
                       vcamcore::kDefaultWidth * vcamcore::kDefaultHeight * 3 / 2);
  if (FAILED(hr)) return hr;

  *out = type.Detach();
  return S_OK;
}

}  // namespace

// ===========================================================================
// VCamMediaSource
// ===========================================================================

VCamMediaSource::VCamMediaSource() {
  ModuleAddRef();
}

VCamMediaSource::~VCamMediaSource() {
  ModuleRelease();
}

HRESULT VCamMediaSource::RuntimeClassInitialize() {
  HRESULT hr = ::MFCreateEventQueue(&eventQueue_);
  if (FAILED(hr)) {
    LogHr("MFCreateEventQueue(source)", hr);
    return hr;
  }
  hr = ::MFCreateAttributes(&attributes_, 4);
  if (FAILED(hr)) {
    LogHr("MFCreateAttributes(source)", hr);
    return hr;
  }
  hr = BuildTopology();
  if (FAILED(hr)) {
    LogHr("BuildTopology", hr);
    return hr;
  }
  Logf("media source created (%dx%d NV12 %d fps)", vcamcore::kDefaultWidth,
       vcamcore::kDefaultHeight, vcamcore::kFpsNumerator / vcamcore::kFpsDenominator);
  return S_OK;
}

HRESULT VCamMediaSource::BuildTopology() {
  ComPtr<IMFMediaType> mediaType;
  HRESULT hr = CreateVideoMediaType(&mediaType);
  if (FAILED(hr)) return hr;

  IMFMediaType* types[] = {mediaType.Get()};
  hr = ::MFCreateStreamDescriptor(/*streamIdentifier=*/0, ARRAYSIZE(types), types,
                                  &streamDescriptor_);
  if (FAILED(hr)) return hr;

  ComPtr<IMFMediaTypeHandler> handler;
  hr = streamDescriptor_->GetMediaTypeHandler(&handler);
  if (FAILED(hr)) return hr;
  hr = handler->SetCurrentMediaType(mediaType.Get());
  if (FAILED(hr)) return hr;

  // Without these the Frame Server does not recognise the stream as a colour
  // capture pin, and the camera shows up but never produces a preview.
  hr = streamDescriptor_->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
  if (FAILED(hr)) return hr;
  hr = streamDescriptor_->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, kPinNameVideoCapture);
  if (FAILED(hr)) return hr;
  hr = streamDescriptor_->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES,
                                    MFFrameSourceTypes_Color);
  if (FAILED(hr)) return hr;

  IMFStreamDescriptor* descriptors[] = {streamDescriptor_.Get()};
  hr = ::MFCreatePresentationDescriptor(ARRAYSIZE(descriptors), descriptors,
                                        &presentationDescriptor_);
  if (FAILED(hr)) return hr;
  hr = presentationDescriptor_->SelectStream(0);
  if (FAILED(hr)) return hr;

  return MakeAndInitialize<VCamMediaStream>(&stream_, this, streamDescriptor_.Get());
}

HRESULT VCamMediaSource::CheckShutdown() const {
  return shutdown_ ? MF_E_SHUTDOWN : S_OK;
}

// --- IMFMediaEventGenerator -------------------------------------------------

IFACEMETHODIMP VCamMediaSource::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) {
  ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    queue = eventQueue_;
  }
  return queue->BeginGetEvent(callback, state);
}

IFACEMETHODIMP VCamMediaSource::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) {
  ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    queue = eventQueue_;
  }
  return queue->EndGetEvent(result, event);
}

IFACEMETHODIMP VCamMediaSource::GetEvent(DWORD flags, IMFMediaEvent** event) {
  ComPtr<IMFMediaEventQueue> queue;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    queue = eventQueue_;
  }
  return queue->GetEvent(flags, event);
}

IFACEMETHODIMP VCamMediaSource::QueueEvent(MediaEventType type, REFGUID extendedType,
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

// --- IMFMediaSource ---------------------------------------------------------

IFACEMETHODIMP VCamMediaSource::GetCharacteristics(DWORD* characteristics) {
  if (!characteristics) return E_POINTER;
  std::lock_guard<std::mutex> lock(mutex_);
  HRESULT hr = CheckShutdown();
  if (FAILED(hr)) return hr;
  *characteristics = MFMEDIASOURCE_IS_LIVE;
  return S_OK;
}

IFACEMETHODIMP VCamMediaSource::CreatePresentationDescriptor(
    IMFPresentationDescriptor** descriptor) {
  if (!descriptor) return E_POINTER;
  *descriptor = nullptr;

  std::lock_guard<std::mutex> lock(mutex_);
  HRESULT hr = CheckShutdown();
  if (FAILED(hr)) return hr;
  if (!presentationDescriptor_) return MF_E_NOT_INITIALIZED;
  return presentationDescriptor_->Clone(descriptor);
}

IFACEMETHODIMP VCamMediaSource::Start(IMFPresentationDescriptor* descriptor,
                                      const GUID* timeFormat,
                                      const PROPVARIANT* startPosition) {
  if (!descriptor) return E_INVALIDARG;
  if (timeFormat && *timeFormat != GUID_NULL) return MF_E_UNSUPPORTED_TIME_FORMAT;

  ComPtr<IMFMediaEventQueue> queue;
  ComPtr<VCamMediaStream> stream;
  bool firstStart = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;

    DWORD count = 0;
    hr = descriptor->GetStreamDescriptorCount(&count);
    if (FAILED(hr)) return hr;
    if (count != 1) return MF_E_UNSUPPORTED_REPRESENTATION;

    queue = eventQueue_;
    stream = stream_;
    firstStart = !started_;
    started_ = true;
  }
  if (!queue || !stream) return MF_E_SHUTDOWN;

  // Order matters to the pipeline: the stream must be announced before the
  // source reports that it started.
  PROPVARIANT streamVar;
  ::PropVariantInit(&streamVar);
  streamVar.vt = VT_UNKNOWN;
  streamVar.punkVal = static_cast<IMFMediaStream*>(stream.Get());
  streamVar.punkVal->AddRef();
  HRESULT hr = queue->QueueEventParamVar(firstStart ? MENewStream : MEUpdatedStream, GUID_NULL,
                                         S_OK, &streamVar);
  ::PropVariantClear(&streamVar);
  if (FAILED(hr)) {
    LogHr("QueueEvent(MENewStream)", hr);
    return hr;
  }

  hr = queue->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, startPosition);
  if (FAILED(hr)) {
    LogHr("QueueEvent(MESourceStarted)", hr);
    return hr;
  }

  Logf("source started (first=%d)", firstStart ? 1 : 0);
  return stream->Start();
}

IFACEMETHODIMP VCamMediaSource::Stop() {
  ComPtr<IMFMediaEventQueue> queue;
  ComPtr<VCamMediaStream> stream;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    HRESULT hr = CheckShutdown();
    if (FAILED(hr)) return hr;
    queue = eventQueue_;
    stream = stream_;
  }
  if (stream) stream->Stop();
  Logf("source stopped");
  return queue ? queue->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr)
               : MF_E_SHUTDOWN;
}

IFACEMETHODIMP VCamMediaSource::Pause() {
  // A live source has nothing to pause into.
  return MF_E_INVALID_STATE_TRANSITION;
}

IFACEMETHODIMP VCamMediaSource::Shutdown() {
  ComPtr<IMFMediaEventQueue> queue;
  ComPtr<VCamMediaStream> stream;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) return S_OK;
    shutdown_ = true;
    queue = eventQueue_;
    stream = stream_;
  }

  if (stream) stream->Shutdown();
  if (queue) queue->Shutdown();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    eventQueue_.Reset();
    stream_.Reset();
    presentationDescriptor_.Reset();
    streamDescriptor_.Reset();
    attributes_.Reset();
  }
  Logf("source shut down");
  return S_OK;
}

// --- IMFMediaSourceEx -------------------------------------------------------

IFACEMETHODIMP VCamMediaSource::GetSourceAttributes(IMFAttributes** attributes) {
  if (!attributes) return E_POINTER;
  *attributes = nullptr;

  std::lock_guard<std::mutex> lock(mutex_);
  HRESULT hr = CheckShutdown();
  if (FAILED(hr)) return hr;
  return attributes_.CopyTo(attributes);
}

IFACEMETHODIMP VCamMediaSource::GetStreamAttributes(DWORD streamId, IMFAttributes** attributes) {
  if (!attributes) return E_POINTER;
  *attributes = nullptr;
  if (streamId != 0) return MF_E_INVALIDSTREAMNUMBER;

  std::lock_guard<std::mutex> lock(mutex_);
  HRESULT hr = CheckShutdown();
  if (FAILED(hr)) return hr;
  if (!streamDescriptor_) return MF_E_NOT_INITIALIZED;
  // IMFStreamDescriptor derives from IMFAttributes, so the descriptor itself is
  // the stream's attribute store.
  return streamDescriptor_->QueryInterface(IID_PPV_ARGS(attributes));
}

IFACEMETHODIMP VCamMediaSource::SetD3DManager(IUnknown* /*manager*/) {
  // Frames are produced on the CPU. Declining makes the pipeline hand us plain
  // system-memory buffers, which is what the renderer expects.
  return E_NOTIMPL;
}

// --- IMFGetService ----------------------------------------------------------

IFACEMETHODIMP VCamMediaSource::GetService(REFGUID service, REFIID riid, LPVOID* object) {
  if (!object) return E_POINTER;
  *object = nullptr;
  if (service == MF_MEDIASOURCE_SERVICE) {
    return QueryInterface(riid, object);
  }
  return MF_E_UNSUPPORTED_SERVICE;
}

// --- IKsControl -------------------------------------------------------------

IFACEMETHODIMP VCamMediaSource::KsProperty(PKSPROPERTY property, ULONG /*propertyLength*/,
                                           void* /*propertyData*/, ULONG /*dataLength*/,
                                           ULONG* bytesReturned) {
  if (bytesReturned) *bytesReturned = 0;
  if (property) LogGuid("KsProperty set", property->Set);
  return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP VCamMediaSource::KsMethod(PKSMETHOD method, ULONG /*methodLength*/,
                                         void* /*methodData*/, ULONG /*dataLength*/,
                                         ULONG* bytesReturned) {
  if (bytesReturned) *bytesReturned = 0;
  if (method) LogGuid("KsMethod set", method->Set);
  return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP VCamMediaSource::KsEvent(PKSEVENT event, ULONG /*eventLength*/,
                                        void* /*eventData*/, ULONG /*dataLength*/,
                                        ULONG* bytesReturned) {
  if (bytesReturned) *bytesReturned = 0;
  if (event) LogGuid("KsEvent set", event->Set);
  return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

// ===========================================================================
// VCamActivate
// ===========================================================================

VCamActivate::VCamActivate() {
  ModuleAddRef();
}

VCamActivate::~VCamActivate() {
  ModuleRelease();
}

HRESULT VCamActivate::RuntimeClassInitialize() {
  return ::MFCreateAttributes(&attributes_, 1);
}

IFACEMETHODIMP VCamActivate::ActivateObject(REFIID riid, void** object) {
  if (!object) return E_POINTER;
  *object = nullptr;

  std::lock_guard<std::mutex> lock(mutex_);
  if (!source_) {
    HRESULT hr = MakeAndInitialize<VCamMediaSource>(&source_);
    if (FAILED(hr)) {
      LogHr("MakeAndInitialize<VCamMediaSource>", hr);
      return hr;
    }
  }
  return source_.CopyTo(riid, object);
}

IFACEMETHODIMP VCamActivate::DetachObject() {
  std::lock_guard<std::mutex> lock(mutex_);
  source_.Reset();
  return S_OK;
}

IFACEMETHODIMP VCamActivate::ShutdownObject() {
  ComPtr<VCamMediaSource> source;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    source = source_;
    source_.Reset();
  }
  if (source) source->Shutdown();
  return S_OK;
}

// IMFAttributes: a straight pass-through to the backing store. Verbose, but
// IMFActivate derives from IMFAttributes and the Frame Server does read it.
#define VCAM_FORWARD(call) return attributes_ ? attributes_->call : MF_E_NOT_INITIALIZED

IFACEMETHODIMP VCamActivate::GetItem(REFGUID key, PROPVARIANT* value) {
  VCAM_FORWARD(GetItem(key, value));
}
IFACEMETHODIMP VCamActivate::GetItemType(REFGUID key, MF_ATTRIBUTE_TYPE* type) {
  VCAM_FORWARD(GetItemType(key, type));
}
IFACEMETHODIMP VCamActivate::CompareItem(REFGUID key, REFPROPVARIANT value, BOOL* result) {
  VCAM_FORWARD(CompareItem(key, value, result));
}
IFACEMETHODIMP VCamActivate::Compare(IMFAttributes* other, MF_ATTRIBUTES_MATCH_TYPE matchType,
                                     BOOL* result) {
  VCAM_FORWARD(Compare(other, matchType, result));
}
IFACEMETHODIMP VCamActivate::GetUINT32(REFGUID key, UINT32* value) {
  VCAM_FORWARD(GetUINT32(key, value));
}
IFACEMETHODIMP VCamActivate::GetUINT64(REFGUID key, UINT64* value) {
  VCAM_FORWARD(GetUINT64(key, value));
}
IFACEMETHODIMP VCamActivate::GetDouble(REFGUID key, double* value) {
  VCAM_FORWARD(GetDouble(key, value));
}
IFACEMETHODIMP VCamActivate::GetGUID(REFGUID key, GUID* value) {
  VCAM_FORWARD(GetGUID(key, value));
}
IFACEMETHODIMP VCamActivate::GetStringLength(REFGUID key, UINT32* length) {
  VCAM_FORWARD(GetStringLength(key, length));
}
IFACEMETHODIMP VCamActivate::GetString(REFGUID key, LPWSTR value, UINT32 size, UINT32* length) {
  VCAM_FORWARD(GetString(key, value, size, length));
}
IFACEMETHODIMP VCamActivate::GetAllocatedString(REFGUID key, LPWSTR* value, UINT32* length) {
  VCAM_FORWARD(GetAllocatedString(key, value, length));
}
IFACEMETHODIMP VCamActivate::GetBlobSize(REFGUID key, UINT32* size) {
  VCAM_FORWARD(GetBlobSize(key, size));
}
IFACEMETHODIMP VCamActivate::GetBlob(REFGUID key, UINT8* buffer, UINT32 bufferSize,
                                     UINT32* blobSize) {
  VCAM_FORWARD(GetBlob(key, buffer, bufferSize, blobSize));
}
IFACEMETHODIMP VCamActivate::GetAllocatedBlob(REFGUID key, UINT8** buffer, UINT32* size) {
  VCAM_FORWARD(GetAllocatedBlob(key, buffer, size));
}
IFACEMETHODIMP VCamActivate::GetUnknown(REFGUID key, REFIID riid, LPVOID* object) {
  VCAM_FORWARD(GetUnknown(key, riid, object));
}
IFACEMETHODIMP VCamActivate::SetItem(REFGUID key, REFPROPVARIANT value) {
  VCAM_FORWARD(SetItem(key, value));
}
IFACEMETHODIMP VCamActivate::DeleteItem(REFGUID key) {
  VCAM_FORWARD(DeleteItem(key));
}
IFACEMETHODIMP VCamActivate::DeleteAllItems() {
  VCAM_FORWARD(DeleteAllItems());
}
IFACEMETHODIMP VCamActivate::SetUINT32(REFGUID key, UINT32 value) {
  VCAM_FORWARD(SetUINT32(key, value));
}
IFACEMETHODIMP VCamActivate::SetUINT64(REFGUID key, UINT64 value) {
  VCAM_FORWARD(SetUINT64(key, value));
}
IFACEMETHODIMP VCamActivate::SetDouble(REFGUID key, double value) {
  VCAM_FORWARD(SetDouble(key, value));
}
IFACEMETHODIMP VCamActivate::SetGUID(REFGUID key, REFGUID value) {
  VCAM_FORWARD(SetGUID(key, value));
}
IFACEMETHODIMP VCamActivate::SetString(REFGUID key, LPCWSTR value) {
  VCAM_FORWARD(SetString(key, value));
}
IFACEMETHODIMP VCamActivate::SetBlob(REFGUID key, const UINT8* buffer, UINT32 size) {
  VCAM_FORWARD(SetBlob(key, buffer, size));
}
IFACEMETHODIMP VCamActivate::SetUnknown(REFGUID key, IUnknown* unknown) {
  VCAM_FORWARD(SetUnknown(key, unknown));
}
IFACEMETHODIMP VCamActivate::LockStore() {
  VCAM_FORWARD(LockStore());
}
IFACEMETHODIMP VCamActivate::UnlockStore() {
  VCAM_FORWARD(UnlockStore());
}
IFACEMETHODIMP VCamActivate::GetCount(UINT32* count) {
  VCAM_FORWARD(GetCount(count));
}
IFACEMETHODIMP VCamActivate::GetItemByIndex(UINT32 index, GUID* key, PROPVARIANT* value) {
  VCAM_FORWARD(GetItemByIndex(index, key, value));
}
IFACEMETHODIMP VCamActivate::CopyAllItems(IMFAttributes* dest) {
  VCAM_FORWARD(CopyAllItems(dest));
}

#undef VCAM_FORWARD

}  // namespace vcam
