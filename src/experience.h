#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace experience {
struct State {
  bool available, active, backup;
};
enum class Status { Unavailable, Ready, AlreadySet, Applied, Changed };
inline Status status(State s) {
  if (!s.available)
    return Status::Unavailable;
  if (s.active)
    return s.backup ? Status::Applied : Status::AlreadySet;
  return s.backup ? Status::Changed : Status::Ready;
}
inline std::vector<size_t> plan(const std::vector<State> &states) {
  std::vector<size_t> result;
  for (size_t i = 0; i < states.size(); ++i)
    if (states[i].available && !states[i].active)
      result.push_back(i);
  return result;
}
inline std::optional<double> fps(std::wstring value) {
  if (value.empty() || value.size() > 12)
    return {};
  bool dot = false, digit = false;
  double number = 0, divisor = 1;
  for (auto c : value) {
    if (c == L'.' || c == L',') {
      if (dot)
        return {};
      dot = true;
      continue;
    }
    if (c < L'0' || c > L'9')
      return {};
    digit = true;
    if (dot) {
      divisor *= 10;
      number += (c - L'0') / divisor;
    } else
      number = number * 10 + c - L'0';
  }
  if (!digit || number < 1 || number > 10000 || !std::isfinite(number))
    return {};
  return number;
}
inline bool validResults(const std::vector<std::wstring> &values) {
  if (values.size() != 4)
    return false;
  auto a = fps(values[0]), b = fps(values[1]), c = fps(values[2]), d = fps(values[3]);
  return a && b && c && d && *b <= *a && *d <= *c;
}
inline double delta(double before, double after) {
  return (after - before) * 100 / before;
}
inline std::wstring key(const std::wstring &game) {
  uint64_t hash = 14695981039346656037ull;
  // Deliberately stable across builds; callers verify the stored path before loading.
  for (auto c : game) {
    hash ^= static_cast<uint32_t>(c);
    hash *= 1099511628211ull;
  }
  return std::to_wstring(hash);
}
inline std::vector<std::wstring> recent(std::vector<std::wstring> paths, const std::wstring &path) {
  paths.erase(std::remove(paths.begin(), paths.end(), path), paths.end());
  if (!path.empty())
    paths.insert(paths.begin(), path);
  if (paths.size() > 6)
    paths.resize(6);
  return paths;
}
} // namespace experience
