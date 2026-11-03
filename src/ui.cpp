#include "ui.h"
#include "motion.h"
#include <algorithm>
#include <cmath>
namespace ui {
namespace {
template <class T> void drop(T *&p) {
  if (p)
    p->Release();
  p = nullptr;
}
D2D1_COLOR_F color(unsigned c, float opacity = 1) {
  return D2D1::ColorF(c, opacity);
}
} // namespace
unsigned mix(unsigned a, unsigned b, double t) {
  t = std::clamp(t, 0.0, 1.0);
  unsigned r = 0;
  for (int shift : {0, 8, 16}) {
    int aa = (a >> shift) & 255, bb = (b >> shift) & 255;
    r |= static_cast<unsigned>(aa + (bb - aa) * t) << shift;
  }
  return r;
}
Painter::Painter(ID2D1RenderTarget *value, IDWriteFactory *w,
                 std::map<unsigned long long, IDWriteTextFormat *> &f, const std::wstring &chosen)
    : target(value), write(w), family(chosen), fonts(f) {
  target->CreateSolidColorBrush(color(white), &brush);
}
Painter::~Painter() {
  drop(brush);
}
void Painter::rect(Rect r, unsigned c, float a) {
  brush->SetColor(color(c, a));
  target->FillRectangle(r.native(), brush);
}
void Painter::round(Rect r, unsigned c, float radius, float a) {
  brush->SetColor(color(c, a));
  target->FillRoundedRectangle(D2D1::RoundedRect(r.native(), radius, radius), brush);
}
void Painter::border(Rect r, unsigned c, float radius, float a, float thickness) {
  brush->SetColor(color(c, a));
  target->DrawRoundedRectangle(D2D1::RoundedRect(r.native(), radius, radius), brush, thickness);
}
void Painter::gradient(Rect r, unsigned top, unsigned bottom, float radius) {
  D2D1_GRADIENT_STOP stops[] = {{0, color(top)}, {1, color(bottom)}};
  ID2D1GradientStopCollection *collection = nullptr;
  ID2D1LinearGradientBrush *gradient = nullptr;
  if (SUCCEEDED(target->CreateGradientStopCollection(stops, 2, &collection)) &&
      SUCCEEDED(target->CreateLinearGradientBrush(
          D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.x, r.y),
                                              D2D1::Point2F(r.x + r.w, r.y + r.h)),
          collection, &gradient)))
    target->FillRoundedRectangle(D2D1::RoundedRect(r.native(), radius, radius), gradient);
  else
    round(r, bottom, radius);
  drop(gradient);
  drop(collection);
}
void Painter::text(const std::wstring &value, Rect r, float size, unsigned c, bool bold,
                   DWRITE_TEXT_ALIGNMENT align, float opacity) {
  auto key = static_cast<unsigned long long>(size * 10) * 100 + (bold ? 10 : 0) +
             static_cast<unsigned>(align);
  auto &format = fonts[key];
  if (!format) {
    if (FAILED(write->CreateTextFormat(
            family.c_str(), nullptr,
            bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"da-DK", &format)))
      return;
    format->SetTextAlignment(align);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
  }
  brush->SetColor(color(c, opacity));
  target->DrawText(value.c_str(), static_cast<UINT32>(value.size()), format, r.native(), brush,
                   static_cast<D2D1_DRAW_TEXT_OPTIONS>(D2D1_DRAW_TEXT_OPTIONS_CLIP |
                                                       D2D1_DRAW_TEXT_OPTIONS_NO_SNAP));
}
void Painter::line(float x, float y, float xx, float yy, unsigned c, float thickness,
                   float opacity) {
  brush->SetColor(color(c, opacity));
  target->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(xx, yy), brush, thickness);
}
void Painter::circle(float x, float y, float radius, unsigned c, float opacity) {
  brush->SetColor(color(c, opacity));
  target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius), brush);
}
float Painter::textWidth(const std::wstring &value, float size) {
  auto key = static_cast<unsigned long long>(size * 10) * 100;
  auto &format = fonts[key];
  if (!format && FAILED(write->CreateTextFormat(
                     family.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                     DWRITE_FONT_STRETCH_NORMAL, size, L"da-DK", &format)))
    return 0;
  IDWriteTextLayout *layout = nullptr;
  float result = 0;
  if (SUCCEEDED(write->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), format,
                                        8192, 256, &layout))) {
    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(layout->GetMetrics(&metrics)))
      result = metrics.widthIncludingTrailingWhitespace;
    layout->Release();
  }
  return result;
}
float Painter::textHeight(const std::wstring &value, float width, float size) {
  auto key = static_cast<unsigned long long>(size * 10) * 100;
  auto &format = fonts[key];
  if (!format && FAILED(write->CreateTextFormat(
                     family.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                     DWRITE_FONT_STRETCH_NORMAL, size, L"da-DK", &format)))
    return 0;
  IDWriteTextLayout *layout = nullptr;
  float height = 0;
  if (SUCCEEDED(write->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), format,
                                        width, 8192, &layout))) {
    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(layout->GetMetrics(&metrics)))
      height = metrics.height;
    layout->Release();
  }
  return height;
}
size_t Canvas::caretPosition(const std::wstring &value, float x) {
  if (x <= 0 || value.empty())
    return 0;
  IDWriteTextFormat *format = nullptr;
  IDWriteTextLayout *layout = nullptr;
  size_t position = value.size();
  if (SUCCEEDED(write->CreateTextFormat(fontFamily.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14,
                                        L"da-DK", &format)) &&
      SUCCEEDED(write->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), format,
                                        8192, 128, &layout))) {
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    DWRITE_TEXT_METRICS textMetrics{};
    if (SUCCEEDED(layout->GetMetrics(&textMetrics)) &&
        x >= textMetrics.widthIncludingTrailingWhitespace)
      position = value.size();
    else if (SUCCEEDED(layout->HitTestPoint(x, 8, &trailing, &inside, &metrics)))
      position = metrics.textPosition + (trailing ? metrics.length : 0);
  }
  drop(layout);
  drop(format);
  return std::min(position, value.size());
}
void Painter::opacity(Rect r, float amount) {
  target->PushLayer(D2D1::LayerParameters(r.native(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                          D2D1::IdentityMatrix(), amount),
                    nullptr);
}
void Painter::endOpacity() {
  target->PopLayer();
}
void Painter::clip(Rect r) {
  target->PushAxisAlignedClip(r.native(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}
void Painter::unclip() {
  target->PopAxisAlignedClip();
}
void Canvas::releaseDevice() {
  drop(scene);
  drop(old);
  drop(composed);
  drop(target);
  validFrame = false;
  transition = -10000;
}
Canvas::~Canvas() {
  releaseDevice();
  for (auto &f : fonts)
    drop(f.second);
  drop(write);
  drop(factory);
}
HRESULT Canvas::initialize(HWND w, float factor) {
  window = w;
  scale = factor;
  auto r = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory);
  if (FAILED(r))
    return r;
  r = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                          reinterpret_cast<IUnknown **>(&write));
  if (FAILED(r))
    return r;
  // Validate a real glyph layout. Missing aliases in Wine must not create a textless UI.
  for (const wchar_t *candidate : {L"Segoe UI", L"Arial", L"DejaVu Sans", L"Tahoma"}) {
    IDWriteTextFormat *format = nullptr;
    IDWriteTextLayout *layout = nullptr;
    DWRITE_TEXT_METRICS metrics{};
    bool usable = SUCCEEDED(write->CreateTextFormat(
                      candidate, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                      DWRITE_FONT_STRETCH_NORMAL, 16, L"da-DK", &format)) &&
                  SUCCEEDED(write->CreateTextLayout(L"Maxifying", 9, format, 500, 80, &layout)) &&
                  SUCCEEDED(layout->GetMetrics(&metrics)) && metrics.width > 0;
    drop(layout);
    drop(format);
    if (usable) {
      fontFamily = candidate;
      break;
    }
  }
  return ensure();
}
HRESULT Canvas::ensure() {
  if (target)
    return S_OK;
  RECT client;
  GetClientRect(window, &client);
  auto properties = D2D1::RenderTargetProperties();
  properties.dpiX = properties.dpiY = 96 * scale;
  auto r = factory->CreateHwndRenderTarget(
      properties,
      D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(client.right, client.bottom)), &target);
  if (FAILED(r))
    return r;
  target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  for (auto output : {&scene, &old, &composed}) {
    auto size = D2D1::SizeF(width, height);
    auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    r = target->CreateCompatibleRenderTarget(&size, nullptr, &format,
                                             D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, output);
    if (FAILED(r)) {
      releaseDevice();
      return r;
    }
    (*output)->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    ++surfaces;
  }
  return S_OK;
}
void Canvas::transitionPage(double now) {
  if (validFrame) {
    if (snapshotSerial != serial) {
      std::swap(old, composed);
      snapshotSerial = serial;
    }
    transition = now;
  }
}
HRESULT Canvas::draw(double now, const std::function<void(Painter &)> &content,
                     const std::function<void(Painter &)> &frame) {
  auto r = ensure();
  if (FAILED(r))
    return r;
  scene->BeginDraw();
  scene->Clear(color(background));
  {
    Painter p(scene, write, fonts, fontFamily);
    content(p);
  }
  r = scene->EndDraw();
  if (FAILED(r)) {
    releaseDevice();
    return r;
  }
  ID2D1Bitmap *current = nullptr, *previous = nullptr, *combined = nullptr;
  scene->GetBitmap(&current);
  old->GetBitmap(&previous);
  composed->BeginDraw();
  composed->Clear(color(background));
  double t = motion::ease((now - transition) / 240);
  if (validFrame && t < 1) {
    composed->DrawBitmap(
        previous,
        D2D1::RectF(static_cast<float>(-8 * t), 0, width - static_cast<float>(8 * t), height), 1);
    float shift = static_cast<float>(12 * (1 - t));
    composed->DrawBitmap(current, D2D1::RectF(shift, 0, width + shift, height),
                         static_cast<float>(t));
  } else
    composed->DrawBitmap(current);
  r = composed->EndDraw();
  composed->GetBitmap(&combined);
  if (SUCCEEDED(r)) {
    target->BeginDraw();
    target->Clear(color(background));
    target->DrawBitmap(combined);
    {
      Painter p(target, write, fonts, fontFamily);
      frame(p);
    }
    r = target->EndDraw();
  }
  drop(current);
  drop(previous);
  drop(combined);
  validFrame = SUCCEEDED(r);
  if (validFrame)
    ++serial;
  if (r == static_cast<HRESULT>(D2DERR_RECREATE_TARGET))
    releaseDevice();
  return r;
}
} // namespace ui
