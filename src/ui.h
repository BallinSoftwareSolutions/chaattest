#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <windows.h>
namespace ui {
constexpr float width = 1216, height = 832;
constexpr unsigned background = 0x0b0d11, panel = 0x14171d, raised = 0x1b1f27, line = 0x2b303b,
                   white = 0xf4f4ed, muted = 0x929aa8, lime = 0xc0f579, purple = 0xa99bff,
                   red = 0xffa199;
struct Rect {
  float x, y, w, h;
  bool contains(float px, float py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
  D2D1_RECT_F native() const {
    return D2D1::RectF(x, y, x + w, y + h);
  }
};
using LayoutKey = std::tuple<std::wstring, float, float, unsigned long long>;
using LayoutCache = std::map<LayoutKey, IDWriteTextLayout *>;
class Painter {
  ID2D1RenderTarget *target;
  ID2D1SolidColorBrush *brush = nullptr;
  ID2D1Bitmap *brand;
  IDWriteFactory *write;
  const std::wstring &family;
  std::map<unsigned long long, IDWriteTextFormat *> &fonts;
  LayoutCache &layouts;
  unsigned &layoutBuilds;
  IDWriteTextLayout *layout(const std::wstring &, float, float, float, bool = false,
                            DWRITE_TEXT_ALIGNMENT = DWRITE_TEXT_ALIGNMENT_LEADING);

public:
  Painter(ID2D1RenderTarget *target, IDWriteFactory *write,
          std::map<unsigned long long, IDWriteTextFormat *> &fonts, const std::wstring &family,
          LayoutCache &layouts, unsigned &layoutBuilds, ID2D1Bitmap *brand);
  ~Painter();
  void icon(Rect r);
  void rect(Rect r, unsigned color, float opacity = 1);
  void round(Rect r, unsigned color, float radius = 12, float opacity = 1);
  void border(Rect r, unsigned color, float radius = 12, float opacity = 1, float thickness = 1);
  void gradient(Rect r, unsigned top, unsigned bottom, float radius = 16);
  void text(const std::wstring &value, Rect r, float size = 15, unsigned color = white,
            bool bold = false, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
            float opacity = 1);
  void line(float x1, float y1, float x2, float y2, unsigned color, float thickness = 1,
            float opacity = 1);
  void circle(float x, float y, float radius, unsigned color, float opacity = 1);
  float textWidth(const std::wstring &value, float size = 14);
  float textHeight(const std::wstring &value, float width, float size = 15);
  void opacity(Rect r, float amount);
  void endOpacity();
  void clip(Rect r);
  void unclip();
};
class Canvas {
  ID2D1Factory *factory = nullptr;
  IDWriteFactory *write = nullptr;
  ID2D1HwndRenderTarget *target = nullptr;
  ID2D1BitmapRenderTarget *scene = nullptr, *old = nullptr, *composed = nullptr;
  std::map<unsigned long long, IDWriteTextFormat *> fonts;
  ID2D1Bitmap *brand = nullptr;
  LayoutCache layouts;
  unsigned layoutBuilds = 0, compositions = 0;
  std::wstring fontFamily = L"Segoe UI";
  HWND window = nullptr;
  float scale = 1;
  double transition = -10000;
  bool validFrame = false, composedFrame = false;
  unsigned surfaces = 0;
  unsigned long long serial = 0, snapshotSerial = ~0ull;
  HRESULT ensure();
  void releaseDevice();

public:
  ~Canvas();
  HRESULT initialize(HWND window, float scale);
  void transitionPage(double now);
  bool moving(double now) const {
    return now - transition < 240;
  }
  bool interactive(double now) const {
    return now - transition >= 120;
  }
  size_t caretPosition(const std::wstring &value, float x);
  size_t cachedLayouts() const {
    return layouts.size();
  }
  unsigned compositionPasses() const {
    return compositions;
  }
  unsigned textLayoutBuilds() const {
    return layoutBuilds;
  }
  unsigned surfaceAllocations() const {
    return surfaces;
  }
  HRESULT draw(double now, const std::function<void(Painter &)> &content,
               const std::function<void(Painter &)> &frame);
};
unsigned mix(unsigned a, unsigned b, double amount);
} // namespace ui
