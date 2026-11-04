#pragma once
#include <atomic>
#include <map>
#include <string>
namespace l10n {
enum class Language { Danish, English, Swedish };
inline std::atomic<Language> selected{Language::Danish};
struct Entry {
  const wchar_t *da, *en, *sv;
};
inline const Entry catalog[] = {
#include "translations.inc"
};
inline const wchar_t *locale() {
  switch (selected.load()) {
  case Language::English:
    return L"en-US";
  case Language::Swedish:
    return L"sv-SE";
  default:
    return L"da-DK";
  }
}
inline std::wstring text(const std::wstring &source) {
  const auto language = selected.load();
  if (language == Language::Danish)
    return source;
  static const auto index = [] {
    std::map<std::wstring, const Entry *> values;
    for (const auto &entry : catalog)
      values.emplace(entry.da, &entry);
    return values;
  }();
  auto found = index.find(source);
  if (found == index.end())
    return source; // OS errors, names and identifiers stay intact.
  return language == Language::English ? found->second->en : found->second->sv;
}
} // namespace l10n
