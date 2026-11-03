#include "ui.h"
#include "motion.h"
#include <commctrl.h>
#include <windowsx.h>
#include <unordered_map>
#include <cmath>
#include <string>
namespace ui {
namespace {
HWND root;
HFONT face;
double scale=1;
int selected=100;
constexpr COLORREF panel=RGB(20,26,36),navBase=RGB(16,21,30),accent=RGB(242,190,89),green=RGB(118,214,165),white=RGB(234,239,247),muted=RGB(143,157,178);
int px(int n){return static_cast<int>(n*scale+.5);}
double now(){return static_cast<double>(GetTickCount64());}
void frames(){if(root)PostMessageW(root,requestFrame,0,0);}
struct Button {
 bool navigation=false,tracking=false,active=false;
 motion::Tween hover,pressed,selection,toggle;
 double ripple=-10000;
 POINT origin{};
};
std::unordered_map<HWND,Button> buttons;
std::unordered_map<int,motion::Tween> hover;
std::unordered_map<int,double> pulses;
motion::Tween rail;
HDC buffer=nullptr,previous=nullptr;
HBITMAP bitmap=nullptr,previousBitmap=nullptr;
HGDIOBJ oldBitmap=nullptr,oldPrevious=nullptr;
int width=0,height=0;
double transition=-10000;
bool transitionsEnabled=true;
int hovered=-1;
bool hadMotion=false;
void destroyBuffers(){
 if(buffer){SelectObject(buffer,oldBitmap);DeleteObject(bitmap);DeleteDC(buffer);}
 if(previous){SelectObject(previous,oldPrevious);DeleteObject(previousBitmap);DeleteDC(previous);}
 buffer=previous=nullptr;bitmap=previousBitmap=nullptr;width=height=0;
}
void fill(HDC dc,RECT r,COLORREF color){auto brush=CreateSolidBrush(color);FillRect(dc,&r,brush);DeleteObject(brush);}
LRESULT CALLBACK subclass(HWND w,UINT message,WPARAM a,LPARAM b,UINT_PTR,DWORD_PTR){
 auto it=buttons.find(w);
 if(it!=buttons.end()){
  auto& state=it->second;
  switch(message){
  case WM_MOUSEMOVE:
   if(!state.tracking){TRACKMOUSEEVENT track{sizeof track,TME_LEAVE,w,0};TrackMouseEvent(&track);state.tracking=true;state.hover.target(1,now(),140);frames();}
   PostMessageW(root,pointerMoved,0,0);break;
  case WM_MOUSELEAVE:state.tracking=false;state.hover.target(0,now(),180);PostMessageW(root,pointerMoved,0,0);frames();break;
  case WM_LBUTTONDOWN:state.origin={GET_X_LPARAM(b),GET_Y_LPARAM(b)};state.ripple=now();state.pressed.target(1,now(),65);frames();break;
  case WM_KEYDOWN:if(a==VK_SPACE){RECT r;GetClientRect(w,&r);state.origin={r.right/2,r.bottom/2};state.ripple=now();state.pressed.target(1,now(),65);frames();}break;
  case WM_LBUTTONUP:case WM_CAPTURECHANGED:case WM_CANCELMODE:state.pressed.target(0,now(),170);frames();break;
  case WM_KEYUP:if(a==VK_SPACE){state.pressed.target(0,now(),170);frames();}break;
  case WM_ENABLE:if(!a){state.hover.target(0,now());state.pressed.target(0,now());state.tracking=false;TRACKMOUSEEVENT track{sizeof track,TME_CANCEL|TME_LEAVE,w,0};TrackMouseEvent(&track);frames();}break;
  case WM_ERASEBKGND:return 1;
  case WM_NCDESTROY:RemoveWindowSubclass(w,subclass,1);buttons.erase(it);return DefSubclassProc(w,message,a,b);
  }
 }
 return DefSubclassProc(w,message,a,b);
}
}
COLORREF blend(COLORREF a,COLORREF b,double t){
 t=std::clamp(t,0.0,1.0);return RGB(static_cast<int>(GetRValue(a)+(GetRValue(b)-GetRValue(a))*t),static_cast<int>(GetGValue(a)+(GetGValue(b)-GetGValue(a))*t),static_cast<int>(GetBValue(a)+(GetBValue(b)-GetBValue(a))*t));
}
void rounded(HDC dc,RECT r,COLORREF color,COLORREF stroke,int radius){
 auto brush=CreateSolidBrush(color);auto pen=CreatePen(PS_SOLID,1,stroke);auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);
 RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(pen);
}
void initialize(HWND w,HFONT font,double factor){
 root=w;face=font;scale=factor;rail.snap(142);
 BOOL enabled=TRUE;if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0))transitionsEnabled=enabled!=FALSE;
}
void dispose(){destroyBuffers();buttons.clear();hover.clear();pulses.clear();root=nullptr;}
void styleButton(HWND button,bool navigation){
 auto& state=buttons[button];state.navigation=navigation;state.selection.snap(navigation&&GetDlgCtrlID(button)==selected?1:0);
 SetWindowSubclass(button,subclass,1,0);
}
void selectedNavigation(int id){
 selected=id;double t=now();rail.target(142+(id-100)*48,t,210);
 for(auto& entry:buttons)if(entry.second.navigation)entry.second.selection.target(GetDlgCtrlID(entry.first)==id?1:0,t,180);
 frames();
}
void applied(HWND button,bool active,bool animate){
 auto it=buttons.find(button);if(it==buttons.end())return;auto& state=it->second;
 state.active=active;if(animate)state.toggle.target(active?1:0,now(),220);else state.toggle.snap(active?1:0);
 InvalidateRect(button,nullptr,FALSE);if(animate)frames();
}
void hoverCard(int index){
 if(index==hovered)return;
 hovered=index;
 double t=now();for(auto& entry:hover)entry.second.target(entry.first==index?1:0,t,200);
 if(index>=0)hover[index].target(1,t,150);
 frames();
}
void pulseCard(int index){pulses[index]=now();frames();}
double cardHover(int index){auto found=hover.find(index);return found==hover.end()?0:found->second.value(now());}
double cardPulse(int index){auto found=pulses.find(index);return found==pulses.end()?0:std::max(0.0,1.0-(now()-found->second)/650.0);}
double navigationY(){return rail.value(now());}
HDC canvas(HDC target,int w,int h){
 if(w!=width||h!=height){destroyBuffers();width=w;height=h;buffer=CreateCompatibleDC(target);previous=CreateCompatibleDC(target);bitmap=CreateCompatibleBitmap(target,w,h);previousBitmap=CreateCompatibleBitmap(target,w,h);oldBitmap=SelectObject(buffer,bitmap);oldPrevious=SelectObject(previous,previousBitmap);transition=-10000;}
 return buffer;
}
void beginTransition(){
 if(!buffer||!transitionsEnabled)return;
 HDC dc=GetDC(root);BitBlt(previous,0,0,width,height,dc,0,0,SRCCOPY);ReleaseDC(root,dc);transition=now();frames();
}
void present(HDC target,RECT dirty){
 int w=dirty.right-dirty.left,h=dirty.bottom-dirty.top;
 BitBlt(target,dirty.left,dirty.top,w,h,buffer,dirty.left,dirty.top,SRCCOPY);
 double t=(now()-transition)/190.0;
 if(t>=0&&t<1){RECT content{px(208),px(58),width,px(782)},area{};
  if(IntersectRect(&area,&content,&dirty)){w=area.right-area.left;h=area.bottom-area.top;BitBlt(target,area.left,area.top,w,h,previous,area.left,area.top,SRCCOPY);
   BLENDFUNCTION alpha{AC_SRC_OVER,0,static_cast<BYTE>(255*motion::ease(t)),0};AlphaBlend(target,area.left,area.top,w,h,buffer,area.left,area.top,w,h,alpha);
  }
 }
}
bool tick(){
 double t=now();bool active=false;
 for(auto& entry:buttons){auto& s=entry.second;int id=GetDlgCtrlID(entry.first);bool cardMoving=false;
  if(id>=1000){int index=(id-1000)/3;auto found=hover.find(index);cardMoving=(found!=hover.end()&&found->second.moving(t))||cardPulse(index)>0;}
  bool moving=cardMoving||s.hover.moving(t)||s.pressed.moving(t)||s.selection.moving(t)||s.toggle.moving(t)||(t-s.ripple<360);
  if(moving){InvalidateRect(entry.first,nullptr,FALSE);active=true;}
 }
 bool content=t-transition<190;
 for(auto& entry:hover)if(entry.second.moving(t)){active=true;content=true;}
 for(auto it=pulses.begin();it!=pulses.end();){if(t-it->second>=650)it=pulses.erase(it);else{active=true;content=true;++it;}}
 if(content){RECT r{px(208),px(58),width,px(782)};InvalidateRect(root,&r,FALSE);active=true;}
 if(rail.moving(t)){RECT r{px(10),px(132),px(22),px(710)};InvalidateRect(root,&r,FALSE);active=true;}
 // Paint the exact settled frame as well, avoiding a permanently half-moved switch.
 if(!active&&hadMotion){for(auto& entry:buttons)InvalidateRect(entry.first,nullptr,FALSE);RECT r{0,0,width,height};InvalidateRect(root,&r,FALSE);}
 hadMotion=active;
 return active;
}
void drawButton(const DRAWITEMSTRUCT& item){
 auto it=buttons.find(item.hwndItem);if(it==buttons.end())return;auto& state=it->second;double t=now();
 int w=item.rcItem.right-item.rcItem.left,h=item.rcItem.bottom-item.rcItem.top;
 HDC dc=CreateCompatibleDC(item.hDC);HBITMAP image=CreateCompatibleBitmap(item.hDC,w,h);auto old=SelectObject(dc,image);
 RECT bounds{0,0,w,h};bool disabled=(item.itemState&ODS_DISABLED)!=0,nav=state.navigation;
 bool switchButton=!nav&&item.CtlID>=1000&&(item.CtlID-1000)%3==0;
 COLORREF under=nav?navBase:panel;
 if(!nav&&item.CtlID>=1000){int index=static_cast<int>((item.CtlID-1000)/3);under=blend(panel,RGB(28,35,47),cardHover(index));under=blend(under,green,cardPulse(index)*.08);}
 fill(dc,bounds,under);
 double hot=disabled?0:state.hover.value(t),pressed=state.pressed.value(t),selection=state.selection.value(t),on=state.toggle.value(t);
 COLORREF color=nav?blend(navBase,RGB(57,45,29),selection):disabled?RGB(27,33,43):RGB(40,38,34);
 color=blend(color,nav?RGB(48,43,33):RGB(67,57,38),hot*.7);color=blend(color,RGB(79,61,32),pressed*.7);
 if(switchButton)color=blend(color,RGB(23,55,45),on*.85);
 RECT body{px(1),px(1),w-px(1),h-px(1)};rounded(dc,body,color,blend(color,accent,hot*.32),px(nav?10:12));
 double ripple=(t-state.ripple)/360.0;
 if(!disabled&&ripple>=0&&ripple<1){auto clip=CreateRoundRectRgn(body.left,body.top,body.right,body.bottom,px(12),px(12));SelectClipRgn(dc,clip);
  int radius=static_cast<int>(std::sqrt(static_cast<double>(w*w+h*h))*motion::ease(ripple));auto brush=CreateSolidBrush(blend(color,accent,(1-ripple)*.12));auto pen=CreatePen(PS_NULL,0,0);auto ob=SelectObject(dc,brush),op=SelectObject(dc,pen);
  Ellipse(dc,state.origin.x-radius,state.origin.y-radius,state.origin.x+radius,state.origin.y+radius);SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(brush);DeleteObject(pen);SelectClipRgn(dc,nullptr);DeleteObject(clip);
 }
 wchar_t label[180];GetWindowTextW(item.hwndItem,label,180);auto font=SelectObject(dc,face);SetBkMode(dc,TRANSPARENT);
 COLORREF text=disabled?RGB(92,105,121):nav?blend(muted,accent,selection):accent;if(switchButton&&on>.5)text=green;
 SetTextColor(dc,text);RECT area{px(nav?16:5),static_cast<LONG>(px(1)*pressed),w-px(5),h};
 if(switchButton){RECT track{px(10),h/2-px(7),px(38),h/2+px(7)};COLORREF trackColor=blend(RGB(67,75,88),RGB(62,157,111),on);rounded(dc,track,trackColor,trackColor,px(14));
  int x=px(13)+static_cast<int>(px(13)*on);RECT thumb{x,h/2-px(4),x+px(8),h/2+px(4)};rounded(dc,thumb,white,white,px(8));area.left=px(43);
 }
 DrawTextW(dc,label,-1,&area,DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|(nav?DT_LEFT:DT_CENTER));
 if(item.itemState&ODS_FOCUS){RECT focus{px(3),px(3),w-px(3),h-px(3)};auto pen=CreatePen(PS_DOT,1,accent);auto op=SelectObject(dc,pen),ob=SelectObject(dc,GetStockObject(NULL_BRUSH));RoundRect(dc,focus.left,focus.top,focus.right,focus.bottom,px(9),px(9));SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(pen);}
 SelectObject(dc,font);BitBlt(item.hDC,0,0,w,h,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(image);DeleteDC(dc);
}
}
