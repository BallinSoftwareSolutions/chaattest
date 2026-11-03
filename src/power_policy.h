#pragma once
#include <cstdint>
namespace session_power {
struct State {
  std::uint32_t version, control, enabled;
};
constexpr std::uint32_t executionSpeed = 1;
inline State unthrottled(State original) {
  original.version = 1;
  original.control |= executionSpeed;
  original.enabled &= ~executionSpeed;
  return original;
}
inline bool forcedOff(State state) {
  return (state.control & executionSpeed) && !(state.enabled & executionSpeed);
}
} // namespace session_power
