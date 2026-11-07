#pragma once
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace bloat {
struct App {
  std::wstring name, fullName, family, title, impact;
  bool installed = true;
};
inline const std::vector<App> &catalog() {
  static const std::vector<App> items = {
      {L"Microsoft.BingNews", L"", L"", L"Microsoft News",
       L"Nyhedsappen. Fjernelse stopper ikke nødvendigvis Widgets."},
      {L"Microsoft.BingWeather", L"", L"", L"Microsoft Weather",
       L"Vejrappen. Fjernelse ændrer ikke Windows' netværksfunktioner."},
      {L"Microsoft.MicrosoftSolitaireCollection", L"", L"", L"Solitaire Collection",
       L"Kortspil. Xbox- og gaming-tjenester ændres ikke."},
      {L"Microsoft.MicrosoftOfficeHub", L"", L"", L"Microsoft 365-startapp",
       L"Startappen til Microsoft 365. De klassiske Word-/Excel-programmer afinstalleres ikke."},
      {L"Microsoft.YourPhone", L"", L"", L"Phone Link",
       L"Telefonintegration og tilhørende notifikationer kan ophøre."},
      {L"Microsoft.GetHelp", L"", L"", L"Get Help",
       L"Windows' hjælpeapp. Fejlfinding gennem denne app bliver utilgængelig."},
      {L"Microsoft.Getstarted", L"", L"", L"Windows Tips",
       L"Appen med introduktion og tips til Windows."},
      {L"Microsoft.WindowsFeedbackHub", L"", L"", L"Feedback Hub",
       L"Feedback-appen. Dette deaktiverer ikke al Windows-telemetri."},
      {L"Clipchamp.Clipchamp", L"", L"", L"Clipchamp",
       L"Videoeditor. Gem og eksportér projekter før fjernelse."}};
  return items;
}
inline bool identifier(const std::wstring &value) {
  return !value.empty() && value.size() <= 512 &&
         std::all_of(value.begin(), value.end(), [](wchar_t c) {
           return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                  (c >= L'0' && c <= L'9') || c == L'.' || c == L'_' || c == L'-';
         });
}
inline bool allowed(const App &app) {
  auto item = std::find_if(catalog().begin(), catalog().end(),
                           [&](const App &c) { return c.name == app.name; });
  if (item == catalog().end() || !identifier(app.fullName) || !identifier(app.family))
    return false;
  auto prefix = app.name + L"_";
  if (app.fullName.rfind(prefix, 0) != 0 || app.family.rfind(prefix, 0) != 0)
    return false;
  auto publisher = app.family.substr(prefix.size());
  return !publisher.empty() && publisher.find(L'_') == std::wstring::npos &&
         app.fullName.size() > publisher.size() &&
         app.fullName.compare(app.fullName.size() - publisher.size() - 1, publisher.size() + 1,
                              L"_" + publisher) == 0;
}
inline std::vector<App> parse(const std::wstring &output) {
  std::vector<App> result;
  std::wstringstream stream(output);
  std::wstring row;
  while (std::getline(stream, row)) {
    if (!row.empty() && row.back() == L'\r')
      row.pop_back();
    if (!row.empty() && row.front() == 0xfeff)
      row.erase(row.begin());
    if (row.empty())
      continue;
    auto first = row.find(L'\t'),
         second = first == std::wstring::npos ? first : row.find(L'\t', first + 1);
    if (first == std::wstring::npos || second == std::wstring::npos ||
        row.find(L'\t', second + 1) != std::wstring::npos)
      throw std::runtime_error("Unexpected Windows app inventory output");
    App app;
    app.name = row.substr(0, first);
    app.fullName = row.substr(first + 1, second - first - 1);
    app.family = row.substr(second + 1);
    if (!allowed(app))
      throw std::runtime_error("App inventory identity was not accepted");
    auto found = std::find_if(catalog().begin(), catalog().end(),
                              [&](const App &c) { return c.name == app.name; });
    app.title = found->title;
    app.impact = found->impact;
    if (std::find_if(result.begin(), result.end(),
                     [&](const App &a) { return a.fullName == app.fullName; }) == result.end())
      result.push_back(app);
  }
  return result;
}
inline std::wstring inventoryScript() {
  std::wstring names;
  for (const auto &app : catalog()) {
    if (!names.empty())
      names += L",";
    names += L"'" + app.name + L"'";
  }
  return L"$names=@(" + names +
         L"); Get-AppxPackage | Where-Object { $names -contains $_.Name -and -not $_.IsFramework "
         L"-and -not $_.IsResourcePackage -and -not $_.NonRemovable } | ForEach-Object { $_.Name + "
         L"[char]9 + $_.PackageFullName + [char]9 + $_.PackageFamilyName }";
}
inline std::wstring removalScript(const App &app) {
  if (!allowed(app))
    throw std::runtime_error("This package is not an allowed optional Windows app");
  return L"$p=@(Get-AppxPackage -Name '" + app.name +
         L"' | Where-Object { $_.PackageFullName -eq '" + app.fullName +
         L"' -and -not $_.IsFramework -and -not $_.IsResourcePackage -and -not $_.NonRemovable }); "
         L"if($p.Count -ne 1){ throw 'Pakken er ændret eller kan ikke fjernes. Opdater listen.' }; "
         L"Remove-AppxPackage -Package $p[0].PackageFullName -ErrorAction Stop; "
         L"$remaining=@(Get-AppxPackage -Name '" +
         app.name + L"' | Where-Object { $_.PackageFullName -eq '" + app.fullName +
         L"' }); if($remaining.Count -ne 0){ throw 'Windows viser stadig den valgte pakke. Opdater "
         L"listen.' }";
}
inline std::wstring encoded(const std::wstring &script) {
  static const wchar_t table[] =
      L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<unsigned char> bytes;
  for (wchar_t c : script) {
    bytes.push_back(static_cast<unsigned char>(c & 255));
    bytes.push_back(static_cast<unsigned char>((c >> 8) & 255));
  }
  std::wstring out;
  for (size_t i = 0; i < bytes.size(); i += 3) {
    unsigned n = static_cast<unsigned>(bytes[i]) << 16;
    if (i + 1 < bytes.size())
      n |= static_cast<unsigned>(bytes[i + 1]) << 8;
    if (i + 2 < bytes.size())
      n |= bytes[i + 2];
    out += table[(n >> 18) & 63];
    out += table[(n >> 12) & 63];
    out += i + 1 < bytes.size() ? table[(n >> 6) & 63] : L'=';
    out += i + 2 < bytes.size() ? table[n & 63] : L'=';
  }
  return out;
}
} // namespace bloat
