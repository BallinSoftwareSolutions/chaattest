#pragma once
#include <algorithm>
#include <string>
namespace ui_text {
inline bool high(wchar_t c) {
  return c >= 0xd800 && c <= 0xdbff;
}
inline bool low(wchar_t c) {
  return c >= 0xdc00 && c <= 0xdfff;
}
inline size_t previous(const std::wstring &value, size_t at) {
  at = std::min(at, value.size());
  if (!at)
    return 0;
  --at;
  if (at && low(value[at]) && high(value[at - 1]))
    --at;
  return at;
}
inline size_t next(const std::wstring &value, size_t at) {
  if (at >= value.size())
    return value.size();
  return at + (high(value[at]) && at + 1 < value.size() && low(value[at + 1]) ? 2 : 1);
}
// A single-line field must not retain control codes or half a UTF-16 character at its limit.
inline std::wstring printable(const std::wstring &text, size_t capacity) {
  std::wstring result;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] < L' ' || low(text[i]))
      continue;
    if (high(text[i])) {
      if (i + 1 >= text.size() || !low(text[i + 1]))
        continue;
      if (result.size() + 2 > capacity)
        break;
      result += text[i];
      result += text[++i];
    } else {
      if (result.size() >= capacity)
        break;
      result += text[i];
    }
  }
  return result;
}
} // namespace ui_text
