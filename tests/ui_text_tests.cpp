#include "ui_text.h"
#include <iostream>
#include <stdexcept>
int main() {
  auto require = [](bool value, const char *message) {
    if (!value)
      throw std::runtime_error(message);
  };
  try {
    const std::wstring pair{static_cast<wchar_t>(0xd83d), static_cast<wchar_t>(0xde80)};
    auto text = L"A" + pair + L"B";
    require(ui_text::previous(text, 3) == 1 && ui_text::next(text, 1) == 3,
            "caret split a UTF-16 pair");
    require(ui_text::previous(text, 0) == 0 && ui_text::next(text, 99) == text.size(),
            "caret escaped bounds");
    require(ui_text::printable(L"A" + pair, 2) == L"A", "input limit retained half a character");
    require(ui_text::printable(L"A" + pair, 3) == L"A" + pair, "valid character pair was lost");
    require(ui_text::printable(L"a\r\nb\tc", 10) == L"abc",
            "single-line input accepted control codes");
    require(ui_text::printable(std::wstring(1, static_cast<wchar_t>(0xdc00)) + L"ok" +
                                   std::wstring(1, static_cast<wchar_t>(0xd800)),
                               10) == L"ok",
            "malformed UTF-16 survived paste");
    require(ui_text::printable(L"abc", 0).empty(), "full field accepted extra input");
    require(ui_text::printable(L"abcdef", 3) == L"abc", "input limit was ignored");
    std::cout << "PASS: Unicode caret boundaries, single-line paste, complete-character limits and "
                 "malformed input\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
