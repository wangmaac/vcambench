#include "attr_dump.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>

#include <cstdio>

#include "logging.h"

namespace vcam {
namespace {

struct KnownKey {
  const GUID* guid;
  const char* name;
};

// The keys worth recognising on sight. Anything else is printed as a raw GUID,
// which is still enough to look up.
const KnownKey kKnownKeys[] = {
    {&MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, "MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME"},
    {&MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, "MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE"},
    {&MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
     "MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK"},
    {&MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_CATEGORY,
     "MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_CATEGORY"},
    {&MF_DEVICESTREAM_STREAM_ID, "MF_DEVICESTREAM_STREAM_ID"},
    {&MF_DEVICESTREAM_STREAM_CATEGORY, "MF_DEVICESTREAM_STREAM_CATEGORY"},
    {&MF_VIRTUALCAMERA_ASSOCIATED_CAMERA_SOURCES, "MF_VIRTUALCAMERA_ASSOCIATED_CAMERA_SOURCES"},
    {&MF_VIRTUALCAMERA_CONFIGURATION_APP_PACKAGE_FAMILY_NAME,
     "MF_VIRTUALCAMERA_CONFIGURATION_APP_PACKAGE_FAMILY_NAME"},
    {&MF_VIRTUALCAMERA_PROVIDE_ASSOCIATED_CAMERA_SOURCES,
     "MF_VIRTUALCAMERA_PROVIDE_ASSOCIATED_CAMERA_SOURCES"},
};

const char* KeyName(const GUID& key) {
  for (const KnownKey& k : kKnownKeys) {
    if (*k.guid == key) return k.name;
  }
  return nullptr;
}

void FormatGuid(const GUID& g, char* out, size_t cap) {
  ::_snprintf_s(out, cap, _TRUNCATE, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                static_cast<unsigned long>(g.Data1), g.Data2, g.Data3, g.Data4[0], g.Data4[1],
                g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
}

void LogOne(IMFAttributes* attrs, UINT32 index) {
  GUID key = GUID_NULL;
  PROPVARIANT value;
  ::PropVariantInit(&value);

  if (FAILED(attrs->GetItemByIndex(index, &key, &value))) {
    Logf("    [%u] <읽기 실패>", index);
    ::PropVariantClear(&value);
    return;
  }

  char keyText[80] = {};
  const char* known = KeyName(key);
  if (!known) FormatGuid(key, keyText, sizeof(keyText));

  switch (value.vt) {
    case VT_LPWSTR:
      Logf("    [%u] %s = \"%ls\"", index, known ? known : keyText,
           value.pwszVal ? value.pwszVal : L"");
      break;
    case VT_UI4:
      Logf("    [%u] %s = %lu", index, known ? known : keyText,
           static_cast<unsigned long>(value.ulVal));
      break;
    case VT_UI8:
      Logf("    [%u] %s = %llu", index, known ? known : keyText,
           static_cast<unsigned long long>(value.uhVal.QuadPart));
      break;
    case VT_CLSID: {
      char guidText[80] = {};
      if (value.puuid) FormatGuid(*value.puuid, guidText, sizeof(guidText));
      Logf("    [%u] %s = %s", index, known ? known : keyText, guidText);
      break;
    }
    case VT_UNKNOWN:
      Logf("    [%u] %s = <IUnknown>", index, known ? known : keyText);
      break;
    default:
      Logf("    [%u] %s = <vt=%u>", index, known ? known : keyText,
           static_cast<unsigned>(value.vt));
      break;
  }
  ::PropVariantClear(&value);
}

}  // namespace

void LogAttributes(const char* what, IMFAttributes* attrs) {
  if (!attrs) {
    Logf("  %s: (없음)", what);
    return;
  }
  UINT32 count = 0;
  if (FAILED(attrs->GetCount(&count))) {
    Logf("  %s: GetCount 실패", what);
    return;
  }
  Logf("  %s: %u개", what, count);
  for (UINT32 i = 0; i < count; ++i) {
    LogOne(attrs, i);
  }
}

}  // namespace vcam
