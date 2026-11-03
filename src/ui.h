#pragma once
#include <windows.h>
namespace ui {
constexpr UINT requestFrame=WM_APP+41, pointerMoved=WM_APP+42;
void initialize(HWND root,HFONT font,double scale);
void dispose();
void styleButton(HWND button,bool navigation=false);
void selectedNavigation(int id);
void applied(HWND button,bool active,bool animate=true);
void hoverCard(int index);
void pulseCard(int index);
double cardHover(int index);
double cardPulse(int index);
double navigationY();
void beginTransition();
// Returns true only while a finite animation is still running.
bool tick();
HDC canvas(HDC target,int width,int height);
void present(HDC target,RECT dirty);
void drawButton(const DRAWITEMSTRUCT& item);
void rounded(HDC dc,RECT rect,COLORREF fill,COLORREF stroke,int radius=12);
COLORREF blend(COLORREF a,COLORREF b,double amount);
}
