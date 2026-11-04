#include "localization.h"
#include <iostream>
#include <set>
#include <stdexcept>
int main() {
  try {
    std::set<std::wstring> keys;
    for (const auto &entry : l10n::catalog) {
      if (!keys.insert(entry.da).second || !*entry.en || !*entry.sv)
        throw std::runtime_error("duplicate or empty translation");
      l10n::selected = l10n::Language::Danish;
      if (l10n::text(entry.da) != entry.da)
        throw std::runtime_error("Danish source changed");
      l10n::selected = l10n::Language::English;
      if (l10n::text(entry.da) != entry.en)
        throw std::runtime_error("English lookup failed");
      l10n::selected = l10n::Language::Swedish;
      if (l10n::text(entry.da) != entry.sv)
        throw std::runtime_error("Swedish lookup failed");
    }
    for (auto language :
         {l10n::Language::Danish, l10n::Language::English, l10n::Language::Swedish}) {
      l10n::selected = language;
      if (l10n::text(L"C:\\Games\\Game.exe") != L"C:\\Games\\Game.exe")
        throw std::runtime_error("translation changed a user path");
    }
    std::cout << "PASS: " << keys.size()
              << " complete unique translations; all languages and identifier fallback\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
