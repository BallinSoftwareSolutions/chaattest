#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
namespace motion {
inline size_t pageOffset(size_t current, bool next, size_t count, size_t visible) {
  const size_t maximum = count > visible ? count - visible : 0;
  current = std::min(current, maximum);
  return next ? current + std::min(visible, maximum - current)
              : current - std::min(visible, current);
}
// Absolute-time interpolation remains smooth even when the message loop misses a frame.
inline double ease(double t) {
  t = std::clamp(t, 0.0, 1.0);
  return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
}
struct Tween {
  double from = 0, to = 0, started = 0, duration = 160;
  double value(double now) const {
    return from + (to - from) * ease((now - started) / duration);
  }
  bool moving(double now) const {
    return from != to && now < started + duration;
  }
  void target(double next, double now, double milliseconds = 160) {
    if (next == to)
      return;
    from = value(now);
    to = next;
    started = now;
    duration = std::max(1.0, milliseconds);
  }
  void snap(double next) {
    from = to = next;
  }
};
// Critically damped scrolling retains velocity when wheel events arrive together.
struct Scroll {
  double from = 0, to = 0, started = 0, velocity = 0;
  static constexpr double rate = .032;
  double value(double now) const {
    double t = std::max(0.0, now - started);
    if (t >= 500)
      return to;
    double d = from - to;
    return to + (d + (velocity + rate * d) * t) * std::exp(-rate * t);
  }
  double speed(double now) const {
    double t = std::max(0.0, now - started), d = from - to;
    return t >= 500 ? 0 : (velocity - rate * (velocity + rate * d) * t) * std::exp(-rate * t);
  }
  bool moving(double now) const {
    return from != to && now < started + 500;
  }
  void target(double next, double now, double = 0) {
    if (next == to)
      return;
    double current = value(now), v = speed(now), distance = next - current;
    // Reversals react immediately; the bound prevents crossing the destination.
    velocity = distance * v > 0
                   ? std::copysign(std::min(std::abs(v), rate * std::abs(distance)), distance)
                   : rate * distance * .6;
    from = current;
    to = next;
    started = now;
  }
  void snap(double next) {
    from = to = next;
    velocity = 0;
  }
};

} // namespace motion
