#include "strings.h"

#include <windows.h>

#include <array>

namespace vcam {
namespace {

// Rows are indexed by Str, so the order here must match the enum. The
// static_assert below catches a row going missing; it cannot catch two rows
// being swapped, so keep them in enum order.
struct Row {
  const wchar_t* en;
  const wchar_t* ko;
};

constexpr std::array<Row, 18> kTable = {{
    // Hint
    {L"Cameras you add look like ordinary cameras to every app on this PC.",
     L"만든 카메라는 이 PC의 모든 앱에서 일반 카메라처럼 보입니다."},
    // StatusNone
    {L"No cameras - add one and it appears in this PC's camera list.",
     L"카메라 없음 — 추가하면 이 PC의 카메라 목록에 나타납니다."},
    // StatusOne
    {L"1 camera running - closing this window removes it.",
     L"카메라 1대 실행 중 — 이 창을 닫으면 사라집니다."},
    // StatusMany
    {L"%zu cameras running - closing this window removes them.",
     L"카메라 %zu대 실행 중 — 이 창을 닫으면 사라집니다."},
    // ButtonAdd
    {L"Add", L"추가"},
    // ButtonRemove
    {L"Remove selected", L"선택 제거"},
    // ButtonRemoveAll
    {L"Remove all", L"모두 제거"},
    // MenuLanguage
    {L"&Language", L"언어(&L)"},
    // MenuEnglish
    {L"&English", L"&English"},
    // MenuKorean
    {L"&Korean (한국어)", L"한국어(&K)"},
    // ErrAddFailed
    {L"Could not create the camera.", L"카메라를 만들지 못했습니다."},
    // ErrRemoveFailed
    {L"Could not remove the camera.", L"카메라를 제거하지 못했습니다."},

    // ErrNotRegistered
    {L"The camera component is not registered. Finish installing first.",
     L"카메라 구성요소가 등록되어 있지 않습니다. 설치를 먼저 완료하세요."},
    // ErrAccessDenied
    {L"Access denied.", L"권한이 부족합니다."},
    // ErrUnsupportedWindows
    {L"This version of Windows does not support virtual cameras. Windows 11 is required.",
     L"이 Windows 버전은 가상 카메라를 지원하지 않습니다. Windows 11이 필요합니다."},
    // ErrDuplicateName
    {L"A camera with that name already exists.", L"같은 이름의 카메라가 이미 있습니다."},
    // ErrEmptyName
    {L"The camera name is empty.", L"카메라 이름이 비어 있습니다."},
    // ErrNoSuchCamera
    {L"Tried to remove a camera that does not exist.", L"없는 카메라를 제거하려 했습니다."},
}};

static_assert(kTable.size() == static_cast<size_t>(Str::ErrNoSuchCamera) + 1,
              "kTable and Str have drifted apart");

Lang g_lang = Lang::English;

constexpr wchar_t kRegKey[] = L"Software\\VCamBench";
constexpr wchar_t kRegValue[] = L"Language";

// What Windows itself is running in. Only the primary language matters: every
// Korean locale gets Korean, everything else gets English.
Lang SystemLanguage() {
  const LANGID id = ::GetUserDefaultUILanguage();
  return PRIMARYLANGID(id) == LANG_KOREAN ? Lang::Korean : Lang::English;
}

}  // namespace

Lang CurrentLanguage() {
  return g_lang;
}

void SetLanguage(Lang lang) {
  g_lang = lang;

  // Per user, not per machine: the app runs unelevated and two people on one PC
  // can disagree. Failing to persist is not worth telling the user about - the
  // language still changed for this session.
  HKEY key = nullptr;
  if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) == ERROR_SUCCESS) {
    const wchar_t* value = (lang == Lang::Korean) ? L"ko" : L"en";
    ::RegSetValueExW(key, kRegValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(value),
                     static_cast<DWORD>((::wcslen(value) + 1) * sizeof(wchar_t)));
    ::RegCloseKey(key);
  }
}

void LoadLanguagePreference() {
  g_lang = SystemLanguage();

  wchar_t buffer[8] = {};
  DWORD bytes = sizeof(buffer);
  DWORD type = 0;
  if (::RegGetValueW(HKEY_CURRENT_USER, kRegKey, kRegValue, RRF_RT_REG_SZ, &type, buffer,
                     &bytes) != ERROR_SUCCESS) {
    return;
  }
  if (::wcscmp(buffer, L"ko") == 0) {
    g_lang = Lang::Korean;
  } else if (::wcscmp(buffer, L"en") == 0) {
    g_lang = Lang::English;
  }
  // Anything else is a value we did not write; the system default stands.
}

const wchar_t* Text(Str id) {
  const Row& row = kTable[static_cast<size_t>(id)];
  return (g_lang == Lang::Korean) ? row.ko : row.en;
}

std::wstring TextCount(Str id, size_t count) {
  const wchar_t* format = Text(id);
  wchar_t buffer[256] = {};
  const int written = ::swprintf(buffer, ARRAYSIZE(buffer), format, count);
  return written > 0 ? std::wstring(buffer, static_cast<size_t>(written)) : std::wstring(format);
}

}  // namespace vcam
