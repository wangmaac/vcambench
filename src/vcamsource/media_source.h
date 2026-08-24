#pragma once

#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <mutex>

#include "media_stream.h"

// Deliberately last. ks.h redefines GUID_NULL as __uuidof(struct GUID_NULL),
// which turns cguid.h's `extern const IID GUID_NULL;` into a syntax error if
// that header is processed afterwards. Nothing here includes COM headers, so
// keeping these at the bottom contains the damage.
#include <ks.h>
#include <ksproxy.h>

namespace vcam {

// The media source the Windows Frame Server instantiates inside its own
// process. Advertises exactly one stream: NV12 1280x720 30fps.
class VCamMediaSource
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          Microsoft::WRL::ChainInterfaces<IMFMediaSourceEx, IMFMediaSource,
                                          IMFMediaEventGenerator>,
          IMFGetService, IKsControl> {
 public:
  VCamMediaSource();
  ~VCamMediaSource() override;

  HRESULT RuntimeClassInitialize();

  // IMFMediaEventGenerator
  IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
  IFACEMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
  IFACEMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
  IFACEMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status,
                            const PROPVARIANT* value) override;

  // IMFMediaSource
  IFACEMETHODIMP GetCharacteristics(DWORD* characteristics) override;
  IFACEMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) override;
  IFACEMETHODIMP Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat,
                       const PROPVARIANT* startPosition) override;
  IFACEMETHODIMP Stop() override;
  IFACEMETHODIMP Pause() override;
  IFACEMETHODIMP Shutdown() override;

  // IMFMediaSourceEx
  IFACEMETHODIMP GetSourceAttributes(IMFAttributes** attributes) override;
  IFACEMETHODIMP GetStreamAttributes(DWORD streamId, IMFAttributes** attributes) override;
  IFACEMETHODIMP SetD3DManager(IUnknown* manager) override;

  // IMFGetService
  IFACEMETHODIMP GetService(REFGUID service, REFIID riid, LPVOID* object) override;

  // IKsControl - the Frame Server probes camera controls through this. Nothing
  // is supported, but the interface has to exist and answer politely.
  IFACEMETHODIMP KsProperty(PKSPROPERTY property, ULONG propertyLength, void* propertyData,
                            ULONG dataLength, ULONG* bytesReturned) override;
  IFACEMETHODIMP KsMethod(PKSMETHOD method, ULONG methodLength, void* methodData,
                          ULONG dataLength, ULONG* bytesReturned) override;
  IFACEMETHODIMP KsEvent(PKSEVENT event, ULONG eventLength, void* eventData, ULONG dataLength,
                         ULONG* bytesReturned) override;

 private:
  HRESULT CheckShutdown() const;
  HRESULT BuildTopology();

  mutable std::mutex mutex_;
  bool shutdown_ = false;
  bool started_ = false;

  Microsoft::WRL::ComPtr<IMFMediaEventQueue> eventQueue_;
  Microsoft::WRL::ComPtr<IMFAttributes> attributes_;
  Microsoft::WRL::ComPtr<IMFStreamDescriptor> streamDescriptor_;
  Microsoft::WRL::ComPtr<IMFPresentationDescriptor> presentationDescriptor_;
  Microsoft::WRL::ComPtr<VCamMediaStream> stream_;
};

// The class actually registered under our CLSID. The Frame Server co-creates
// this, then calls ActivateObject to get the media source.
class VCamActivate
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          Microsoft::WRL::ChainInterfaces<IMFActivate, IMFAttributes>> {
 public:
  VCamActivate();
  ~VCamActivate() override;

  HRESULT RuntimeClassInitialize();

  // IMFActivate
  IFACEMETHODIMP ActivateObject(REFIID riid, void** object) override;
  IFACEMETHODIMP DetachObject() override;
  IFACEMETHODIMP ShutdownObject() override;

  // IMFAttributes - forwarded to a plain attribute store.
  IFACEMETHODIMP GetItem(REFGUID key, PROPVARIANT* value) override;
  IFACEMETHODIMP GetItemType(REFGUID key, MF_ATTRIBUTE_TYPE* type) override;
  IFACEMETHODIMP CompareItem(REFGUID key, REFPROPVARIANT value, BOOL* result) override;
  IFACEMETHODIMP Compare(IMFAttributes* other, MF_ATTRIBUTES_MATCH_TYPE matchType,
                         BOOL* result) override;
  IFACEMETHODIMP GetUINT32(REFGUID key, UINT32* value) override;
  IFACEMETHODIMP GetUINT64(REFGUID key, UINT64* value) override;
  IFACEMETHODIMP GetDouble(REFGUID key, double* value) override;
  IFACEMETHODIMP GetGUID(REFGUID key, GUID* value) override;
  IFACEMETHODIMP GetStringLength(REFGUID key, UINT32* length) override;
  IFACEMETHODIMP GetString(REFGUID key, LPWSTR value, UINT32 size, UINT32* length) override;
  IFACEMETHODIMP GetAllocatedString(REFGUID key, LPWSTR* value, UINT32* length) override;
  IFACEMETHODIMP GetBlobSize(REFGUID key, UINT32* size) override;
  IFACEMETHODIMP GetBlob(REFGUID key, UINT8* buffer, UINT32 bufferSize, UINT32* blobSize) override;
  IFACEMETHODIMP GetAllocatedBlob(REFGUID key, UINT8** buffer, UINT32* size) override;
  IFACEMETHODIMP GetUnknown(REFGUID key, REFIID riid, LPVOID* object) override;
  IFACEMETHODIMP SetItem(REFGUID key, REFPROPVARIANT value) override;
  IFACEMETHODIMP DeleteItem(REFGUID key) override;
  IFACEMETHODIMP DeleteAllItems() override;
  IFACEMETHODIMP SetUINT32(REFGUID key, UINT32 value) override;
  IFACEMETHODIMP SetUINT64(REFGUID key, UINT64 value) override;
  IFACEMETHODIMP SetDouble(REFGUID key, double value) override;
  IFACEMETHODIMP SetGUID(REFGUID key, REFGUID value) override;
  IFACEMETHODIMP SetString(REFGUID key, LPCWSTR value) override;
  IFACEMETHODIMP SetBlob(REFGUID key, const UINT8* buffer, UINT32 size) override;
  IFACEMETHODIMP SetUnknown(REFGUID key, IUnknown* unknown) override;
  IFACEMETHODIMP LockStore() override;
  IFACEMETHODIMP UnlockStore() override;
  IFACEMETHODIMP GetCount(UINT32* count) override;
  IFACEMETHODIMP GetItemByIndex(UINT32 index, GUID* key, PROPVARIANT* value) override;
  IFACEMETHODIMP CopyAllItems(IMFAttributes* dest) override;

 private:
  std::mutex mutex_;
  Microsoft::WRL::ComPtr<IMFAttributes> attributes_;
  Microsoft::WRL::ComPtr<VCamMediaSource> source_;
};

}  // namespace vcam
