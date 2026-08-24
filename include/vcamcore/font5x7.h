#pragma once

namespace vcamcore {

inline constexpr int kGlyphW = 5;
inline constexpr int kGlyphH = 7;

// Returns 7 rows of 5 characters ('#' = ink, anything else = background),
// or nullptr for an unsupported character. Lowercase is folded to uppercase.
const char* const* FindGlyph(char c);

}  // namespace vcamcore
