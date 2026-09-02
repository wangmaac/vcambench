#pragma once

// Every string the window shows, in both languages it speaks.
//
// The set is small enough that a table beats a resource DLL: no satellite
// files to ship, no chance of the installer dropping the one the user needs.
// Adding a language means adding a column, and the compiler will point at the
// rows that are missing.
//
// Diagnostics deliberately stay out of here. camlist, camcapture, vcamctl and
// the media source log are English only, because their output ends up pasted
// into bug reports where one language is worth more than a familiar one.

#include <string>

namespace vcam {

enum class Lang {
  English,
  Korean,
};

enum class Str {
  Hint,
  StatusNone,
  StatusOne,
  StatusMany,  // carries %zu
  ButtonAdd,
  ButtonRemove,
  ButtonRemoveAll,
  MenuLanguage,
  MenuEnglish,
  MenuKorean,
  ErrAddFailed,
  ErrRemoveFailed,

  // Reasons CameraManager hands back. Phrased so the user can act on them.
  ErrNotRegistered,
  ErrAccessDenied,
  ErrUnsupportedWindows,
  ErrDuplicateName,
  ErrEmptyName,
  ErrNoSuchCamera,
};

// The language in use. Defaults to the one Windows is running in, unless the
// user has chosen otherwise before - see LoadLanguagePreference().
Lang CurrentLanguage();

// Switches language for the rest of the session and remembers the choice.
void SetLanguage(Lang lang);

// Reads the remembered choice, falling back to the Windows UI language. Call
// once at startup, before anything asks for a string.
void LoadLanguagePreference();

const wchar_t* Text(Str id);

// Text(id) with a single count substituted. Used for the status line, which is
// the only string that varies.
std::wstring TextCount(Str id, size_t count);

}  // namespace vcam
