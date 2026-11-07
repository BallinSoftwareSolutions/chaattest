#pragma once
#include "bloat_policy.h"
#include <atomic>
#include <memory>
namespace bloat {
struct Inventory {
  bool supported = false;
  std::vector<App> apps;
  std::wstring error;
};
Inventory inspect(std::shared_ptr<std::atomic_bool> cancel = {});
std::wstring remove(const App &app);
} // namespace bloat
