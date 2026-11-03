#include "bloat_policy.h"
#include "power_policy.h"
#include <iostream>
#include <stdexcept>
int main() {
  auto require = [](bool yes, const char *message) {
    if (!yes)
      throw std::runtime_error(message);
  };
  try {
    const std::wstring row = L"Microsoft.BingNews\tMicrosoft.BingNews_1.2.3.4_x64__"
                             L"8wekyb3d8bbwe\tMicrosoft.BingNews_8wekyb3d8bbwe\r\n";
    auto parsed = bloat::parse(row + row);
    require(parsed.size() == 1 && parsed[0].title == L"Microsoft News",
            "inventory identity or deduplication failed");
    auto script = bloat::removalScript(parsed[0]);
    require(script.find(L"-AllUsers") == std::wstring::npos &&
                script.find(L"Remove-AppxProvisionedPackage") == std::wstring::npos,
            "removal escaped current-user scope");
    require(script.find(L"$remaining") != std::wstring::npos,
            "removal completion was not read back");
    require(script.find(L"NonRemovable") != std::wstring::npos &&
                script.find(L"IsFramework") != std::wstring::npos &&
                script.find(L"PackageFullName -eq") != std::wstring::npos,
            "deployment was not revalidated");
    bool rejected = false;
    try {
      bloat::parse(L"Microsoft.WindowsStore\tMicrosoft.WindowsStore_1_x64__"
                   L"8wekyb3d8bbwe\tMicrosoft.WindowsStore_8wekyb3d8bbwe");
    } catch (...) {
      rejected = true;
    }
    require(rejected, "Windows Store was accepted as removable bloat");
    auto app = parsed[0];
    app.fullName += L"';Stop-Process x;#";
    require(!bloat::allowed(app), "command injection accepted");
    app = parsed[0];
    app.family = L"Microsoft.BingNews_otherpublisher";
    require(!bloat::allowed(app), "publisher identity mismatch accepted");
    require(bloat::encoded(L"A") == L"QQA=" && bloat::encoded(L"abc") == L"YQBiAGMA",
            "PowerShell UTF-16 argument encoding failed");
    session_power::State original{1, 4, 5};
    auto changed = session_power::unthrottled(original);
    require(changed.control == 5 && changed.enabled == 4, "EcoQoS update changed unrelated flags");
    require(original.control == 4 && original.enabled == 5, "EcoQoS baseline mutated");
    require(session_power::forcedOff(changed) && !session_power::forcedOff(original),
            "EcoQoS readback predicate failed");
    std::cout << "PASS: optional-app allowlist, identity and injection rejection, current-user "
                 "removal scope, encoding and preserved power flags\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
