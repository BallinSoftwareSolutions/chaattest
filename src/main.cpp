#include <winsock2.h>
#include <ws2tcpip.h>
#include "engine.h"
#include "ui.h"
#include "motion.h"
#include <commctrl.h>
#include <functional>
#include <commdlg.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <windns.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <future>
#include <sstream>
#include <fstream>
#include <memory>
#include <cwctype>

namespace {
constexpr COLORREF bg=RGB(12,16,23), panel=RGB(20,26,36), border=RGB(40,49,64), gold=RGB(242,190,89), ink=RGB(234,239,247), muted=RGB(143,157,178), green=RGB(118,214,165);
HWND mainWindow,searchBox;
double uiScale=1.0;
int px(int n){return static_cast<int>(n*uiScale+0.5);}
HFONT controlFont;
HFONT normalFont,smallFont,headingFont,titleFont;
HBRUSH background,fieldBrush;
std::wstring page=L"Overblik",query,game,toast=L"Klar. Ingen indstillinger ændres ved opstart.",hardware;
const std::vector<std::wstring> pages={L"Overblik",L"Gaming",L"Input",L"Windows",L"Baggrund",L"Privatliv",L"Spilprofil",L"Netværk",L"System",L"Opstart",L"Gendannelse"};
std::vector<HWND> dynamicControls;
std::vector<size_t> visible;
struct State {bool active=false,backup=false,available=false;std::wstring message;};
std::vector<State> states;
size_t offset=0,backupCount=0;
std::wstring layoutKey;
std::future<std::wstring> actionTask;
std::wstring actionSuccess;
int actionFeature=-1;
bool actionForce=false,animationTimer=false,parentTracking=false;
enum class Feedback {Neutral,Working,Success,Error};
Feedback feedback=Feedback::Neutral;
motion::Tween toastReveal;
void render(bool force=false);
void ensureFrames(){if(mainWindow&&!animationTimer){SetTimer(mainWindow,2,16,nullptr);animationTimer=true;}}
void invalidate(int x,int y,int width,int height){RECT r{px(x),px(y),px(x+width),px(y+height)};InvalidateRect(mainWindow,&r,FALSE);}

bool rebuilding=false;
std::future<std::wstring> pingTask;
std::wstring pingResult=L"Indtast en IPv4-adresse for at måle ICMP round-trip-tid.";
struct Process {HANDLE handle;DWORD pid;std::wstring label;};
struct Priority {HANDLE handle;DWORD original;std::wstring name;};
std::vector<Process> processes;
std::vector<Priority> priorities;
std::vector<tune::StartupEntry> startups;

void fill(HDC dc,RECT r,COLORREF color){auto b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
void line(HDC dc,int x,int y,int width,COLORREF color){fill(dc,{x,y,x+width,y+1},color);}
void text(HDC dc,const std::wstring& value,int x,int y,int width,int height,COLORREF color,HFONT font){
 auto previous=SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);RECT r{x,y,x+width,y+height};
 DrawTextW(dc,value.c_str(),-1,&r,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,previous);
}
HFONT font(int size,int weight){return CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");}
void notify(const std::wstring& message,Feedback kind=Feedback::Neutral){
 toast=message;feedback=kind;toastReveal.snap(0);toastReveal.target(1,static_cast<double>(GetTickCount64()),220);invalidate(220,792,980,66);ensureFrames();
}
HWND control(const wchar_t* cls,const std::wstring& label,int id,int x,int y,int width,int height,DWORD style=0,bool dynamic=true){
 HWND w=CreateWindowExW(0,cls,label.c_str(),WS_CHILD|((dynamic&&rebuilding)?0:WS_VISIBLE)|WS_TABSTOP|style,px(x),px(y),px(width),px(height),mainWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
 SendMessageW(w,WM_SETFONT,reinterpret_cast<WPARAM>(controlFont),TRUE);if(dynamic)dynamicControls.push_back(w);return w;
}
HWND button(const std::wstring& label,int id,int x,int y,int width=136,bool enabled=true,bool dynamic=true){
 auto w=control(L"BUTTON",label,id,x,y,width,32,BS_OWNERDRAW,dynamic);ui::styleButton(w,!dynamic);EnableWindow(w,enabled);return w;
}
std::wstring lower(std::wstring value){std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return std::towlower(c);});return value;}
std::wstring fileName(const std::wstring& path){auto at=path.find_last_of(L"\\/");return at==std::wstring::npos?path:path.substr(at+1);}
void refreshStates(){
 try{backupCount=tune::backupIds().size();}catch(...){}
 states.assign(tune::features().size(),{});
 for(size_t i=0;i<states.size();++i){auto& s=states[i];auto& f=tune::features()[i];
  if((f.kind==tune::Kind::Gpu||f.kind==tune::Kind::Fullscreen)&&game.empty()){s.message=L"Vælg en .exe først";continue;}
  try{s.active=tune::active(f,game);s.backup=tune::backedUp(f,game);s.available=true;s.message=s.active?L"Ønsket indstilling er aktiv":L"Windows-indstilling kan tilpasses";}
  catch(const std::exception& e){s.message=L"Utilgængelig: "+tune::errorText(e);try{s.backup=tune::backedUp(f,game);}catch(...) {}}
 }
}
void beginAction(std::function<std::wstring()> operation,const std::wstring& success,int feature=-1,bool force=false){
 if(actionTask.valid())return;
 actionSuccess=success;actionFeature=feature;actionForce=force;
 actionTask=std::async(std::launch::async,[operation]{try{return operation();}catch(const std::exception& error){return tune::errorText(error);}catch(...){return std::wstring(L"Ukendt fejl. Eventuelle backups bevares.");}});
 notify(L"Arbejder… Du kan fortsat navigere i appen.",Feedback::Working);render();ensureFrames();
}
void finishAction(){
 if(!actionTask.valid()||actionTask.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
 auto error=actionTask.get();auto previousStates=states;int changed=actionFeature;actionFeature=-1;
 refreshStates();render(actionForce);
 for(size_t i=0;i<states.size()&&i<previousStates.size();++i)if(states[i].active!=previousStates[i].active||(error.empty()&&states[i].backup!=previousStates[i].backup))ui::pulseCard(static_cast<int>(i));
 if(error.empty()){if(changed>=0)ui::pulseCard(changed);notify(actionSuccess,Feedback::Success);}
 else{tune::log(L"Fejl: "+error);notify(L"Handlingen blev ikke fuldført. Resterende backups bevares.",Feedback::Error);MessageBoxW(mainWindow,error.c_str(),L"Maxifying · handling ikke gennemført",MB_OK|MB_ICONWARNING);}
}
bool protectedProcess(const std::wstring& name){
 const auto n=lower(name);for(const wchar_t* entry:{L"csrss.exe",L"wininit.exe",L"winlogon.exe",L"services.exe",L"lsass.exe",L"svchost.exe",L"dwm.exe",L"explorer.exe",L"sihost.exe",L"taskhostw.exe",L"conhost.exe",L"smss.exe",L"audiodg.exe",L"spoolsv.exe",L"searchhost.exe",L"startmenuexperiencehost.exe",L"shellexperiencehost.exe",L"runtimebroker.exe",L"securityhealthsystray.exe",L"msmpeng.exe"})if(n==entry)return true;
 return false;
}
void closeProcesses(){for(auto& p:processes)CloseHandle(p.handle);processes.clear();}
void listProcesses(){
 closeProcesses();std::vector<DWORD> windowProcesses;
 EnumWindows([](HWND w,LPARAM data)->BOOL{if(IsWindowVisible(w)&&GetWindow(w,GW_OWNER)==nullptr){DWORD pid=0;GetWindowThreadProcessId(w,&pid);reinterpret_cast<std::vector<DWORD>*>(data)->push_back(pid);}return TRUE;},reinterpret_cast<LPARAM>(&windowProcesses));
 HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
 if(snapshot==INVALID_HANDLE_VALUE)return;
 PROCESSENTRY32W item{};item.dwSize=sizeof item;
 if(Process32FirstW(snapshot,&item)){do{
  if(item.th32ProcessID==GetCurrentProcessId()||protectedProcess(item.szExeFile)||std::find(windowProcesses.begin(),windowProcesses.end(),item.th32ProcessID)==windowProcesses.end())continue;
  HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_SET_INFORMATION|SYNCHRONIZE,FALSE,item.th32ProcessID);
  if(h)processes.push_back({h,item.th32ProcessID,std::wstring(item.szExeFile)+L"  ·  PID "+std::to_wstring(item.th32ProcessID)});
 }while(Process32NextW(snapshot,&item));}CloseHandle(snapshot);
 std::sort(processes.begin(),processes.end(),[](const Process& a,const Process& b){return lower(a.label)<lower(b.label);});
}
void restorePriorities(){
 std::vector<Priority> failed;
 for(auto& p:priorities){
  if(WaitForSingleObject(p.handle,0)==WAIT_OBJECT_0||SetPriorityClass(p.handle,p.original))CloseHandle(p.handle);
  else failed.push_back(p);
 }
 priorities=std::move(failed);
 if(!priorities.empty())throw std::runtime_error("Some process priorities could not be restored. Retry before closing.");
}
void boostProcess(){
 auto combo=GetDlgItem(mainWindow,501);auto selected=SendMessageW(combo,CB_GETCURSEL,0,0);
 if(selected==CB_ERR||static_cast<size_t>(selected)>=processes.size())throw std::runtime_error("Select a running game process first");
 auto& process=processes[static_cast<size_t>(selected)];
 if(WaitForSingleObject(process.handle,0)!=WAIT_TIMEOUT)throw std::runtime_error("The selected process has exited");
 for(auto& p:priorities)if(WaitForSingleObject(p.handle,0)==WAIT_TIMEOUT&&GetProcessId(p.handle)==process.pid){notify(L"Processen har allerede en gemt prioritet.");return;}
 DWORD original=GetPriorityClass(process.handle);
 if(!original)throw std::runtime_error("Cannot read process priority");
 if(original!=NORMAL_PRIORITY_CLASS&&original!=BELOW_NORMAL_PRIORITY_CLASS&&original!=IDLE_PRIORITY_CLASS)throw std::runtime_error("Process already has above-normal or higher priority; no change made");
 HANDLE copy=nullptr;
 if(!DuplicateHandle(GetCurrentProcess(),process.handle,GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_SAME_ACCESS))throw std::runtime_error("Cannot retain process handle");
 if(!SetPriorityClass(copy,ABOVE_NORMAL_PRIORITY_CLASS)){CloseHandle(copy);throw std::runtime_error("Windows rejected priority change");}
 priorities.push_back({copy,original,process.label});tune::log(L"Midlertidig Above Normal-prioritet: "+process.label);
 notify(L"Above Normal anvendt. Gendannes ved normal lukning eller manuelt.",Feedback::Success);
}
void setEnabled(int id,bool enabled){auto w=GetDlgItem(mainWindow,id);if(w&&static_cast<bool>(IsWindowEnabled(w))!=enabled)EnableWindow(w,enabled);}
void setLabel(int id,const std::wstring& label){auto w=GetDlgItem(mainWindow,id);if(!w)return;wchar_t current[256];GetWindowTextW(w,current,256);if(label!=current)SetWindowTextW(w,label.c_str());}
void syncControls(bool initial=false){
 bool busy=actionTask.valid();
 for(auto index:visible){auto& state=states[index];int id=1000+static_cast<int>(index)*3;
  setEnabled(id,state.available&&!state.active&&!busy);setEnabled(id+1,state.backup&&!busy);
  setLabel(id,busy&&actionFeature==static_cast<int>(index)?L"Vent…":state.active?L"Aktiv":L"Anvend");
  ui::applied(GetDlgItem(mainWindow,id),state.active,!initial);
 }
 for(int id:{410,420,421,422,423,502,504,505,512,520,522,543})setEnabled(id,!busy);
 setEnabled(502,!busy&&!processes.empty());setEnabled(505,!busy&&!processes.empty());setEnabled(503,!busy&&!priorities.empty());
 auto selected=SendDlgItemMessageW(mainWindow,540,CB_GETCURSEL,0,0);bool valid=selected!=CB_ERR&&static_cast<size_t>(selected)<startups.size();
 setEnabled(541,!busy&&valid&&startups[static_cast<size_t>(selected)].present);setEnabled(542,!busy&&valid&&startups[static_cast<size_t>(selected)].backup);
}
void requestAppClose(){
 auto selected=SendDlgItemMessageW(mainWindow,501,CB_GETCURSEL,0,0);
 if(selected==CB_ERR||static_cast<size_t>(selected)>=processes.size())throw std::runtime_error("Select a running application first");
 auto& process=processes[static_cast<size_t>(selected)];if(WaitForSingleObject(process.handle,0)!=WAIT_TIMEOUT)throw std::runtime_error("The selected app has already exited");
 if(MessageBoxW(mainWindow,(L"Bed "+process.label+L" om at lukke? Appen kan spørge om at gemme arbejde. Nogle apps bliver i systembakken. Ingen proces bliver tvangsafsluttet.").c_str(),L"Luk valgt app",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
 struct Request{DWORD pid;unsigned sent=0;};Request request{process.pid};
 EnumWindows([](HWND w,LPARAM data)->BOOL{auto& r=*reinterpret_cast<Request*>(data);DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==r.pid&&IsWindowVisible(w)&&GetWindow(w,GW_OWNER)==nullptr&&PostMessageW(w,WM_CLOSE,0,0))++r.sent;return TRUE;},reinterpret_cast<LPARAM>(&request));
 if(!request.sent)throw std::runtime_error("No closable application window was found. Use the app's own tray menu or startup controls.");
 notify(L"Lukkeanmodning sendt. Kontrollér appen — den kan bede om at gemme eller blive i bakken.",Feedback::Neutral);
}
void render(bool force){
 if(rebuilding)return;
 const auto key=page+L"|"+query+L"|"+std::to_wstring(offset);
 if(!force&&key==layoutKey){
  syncControls();if(page==L"Gendannelse"){auto history=GetDlgItem(mainWindow,523);auto value=tune::history();int length=GetWindowTextLengthW(history);std::wstring current(static_cast<size_t>(length)+1,L'\0');GetWindowTextW(history,current.data(),length+1);current.resize(length);if(current!=value)SetWindowTextW(history,value.c_str());}
  invalidate(220,168,980,610);return;
 }
 if(!layoutKey.empty()&&query.empty())ui::beginTransition();
 layoutKey=key;rebuilding=true;SendMessageW(mainWindow,WM_SETREDRAW,FALSE,0);
 struct Batch {~Batch(){rebuilding=false;if(std::uncaught_exceptions())layoutKey.clear();SendMessageW(mainWindow,WM_SETREDRAW,TRUE,0);RedrawWindow(mainWindow,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_NOERASE);}} batch;
 for(auto w:dynamicControls)DestroyWindow(w);
 dynamicControls.clear();visible.clear();
 const bool searching=!query.empty();
 if(searching||page==L"Gaming"||page==L"Input"||page==L"Windows"||page==L"Baggrund"||page==L"Privatliv"||page==L"Spilprofil"){
  std::vector<size_t> matches;
  for(size_t i=0;i<tune::features().size();++i){auto& f=tune::features()[i];
   if(searching?lower(f.title+L" "+f.description+L" "+f.category).find(lower(query))!=std::wstring::npos:f.category==page)matches.push_back(i);
  }
  if(offset>=matches.size())offset=0;
  for(size_t j=offset;j<matches.size()&&visible.size()<6;++j){auto i=matches[j];size_t n=visible.size();visible.push_back(i);int x=224+static_cast<int>(n%3)*328,y=232+static_cast<int>(n/3)*240;
   const auto& s=states[i];button(L"Anvend",1000+static_cast<int>(i)*3,x+16,y+186,116,s.available&&!s.active);
   button(L"Gendan",1001+static_cast<int>(i)*3,x+140,y+186,88,s.backup);
   button(L"Info",1002+static_cast<int>(i)*3,x+236,y+186,60);
  }
  if(matches.size()>6){button(L"Forrige",401,224,734,118,offset>0);button(L"Næste",402,350,734,118,offset+6<matches.size());}
  if(page==L"Spilprofil"&&!searching)button(L"Vælg spillets .exe",410,224,734,220);
 }else if(page==L"Overblik"){
  for(int i=0;i<4;++i){const int ids[]={420,421,423,422};button(L"Anvend profil",ids[i],240+i*246,408,198);}
  button(L"Se alle gamingvalg",430,240,676,276);button(L"Åbn grafikindstillinger",431,568,676,276);button(L"Opstartsapps",432,896,676,276);
 }else if(page==L"Netværk"){
  control(L"EDIT",L"1.1.1.1",510,244,324,260,34,WS_BORDER|ES_AUTOHSCROLL);
  button(L"Mål ping",511,520,324,170,!pingTask.valid());button(L"Ryd DNS-cache",512,244,598,240);button(L"Åbn netværksindstillinger",513,500,598,290);
 }else if(page==L"System"){
  listProcesses();auto combo=control(L"COMBOBOX",L"",501,244,510,890,300,CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL);
  for(auto& p:processes)SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(p.label.c_str()));
  if(!processes.empty())SendMessageW(combo,CB_SETCURSEL,0,0);
  button(L"Above Normal",502,244,566,220);button(L"Gendan prioriteter",503,482,566,220,!priorities.empty());button(L"Opdater procesliste",504,720,566,220);button(L"Luk den valgte app",505,958,566,220);
 }else if(page==L"Opstart"){
  startups=tune::startupEntries();auto combo=control(L"COMBOBOX",L"",540,244,330,890,300,CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL);
  for(auto& entry:startups){auto label=entry.name+(entry.present?L" · Run-post findes":L" · Run-post fjernet")+(entry.backup?L" · backup gemt":L"");SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
  if(!startups.empty())SendMessageW(combo,CB_SETCURSEL,0,0);
  button(L"Deaktivér opstart",541,244,396,240,!startups.empty()&&startups[0].present);button(L"Gendan opstart",542,500,396,240,!startups.empty()&&startups[0].backup);button(L"Opdater liste",543,756,396,240);button(L"Alle opstartsapps i Windows",544,244,620,330);
 }else if(page==L"Gendannelse"){
  button(L"Gendan alle ændringer",520,244,284,300);button(L"Eksportér rapport",521,562,284,230);button(L"Opdater status",522,810,284,230);
  auto log=control(L"EDIT",tune::history(),523,244,408,890,300,ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER);SendMessageW(log,EM_SETSEL,0,0);
 }
 syncControls(true);ui::selectedNavigation(100+static_cast<int>(std::find(pages.begin(),pages.end(),page)-pages.begin()));
 for(auto child:dynamicControls)ShowWindow(child,SW_SHOWNOACTIVATE);
 ensureFrames();
}
void box(HDC dc,int x,int y,int width,int height,int feature=-1){
 double hot=feature>=0?ui::cardHover(feature):0,pulse=feature>=0?ui::cardPulse(feature):0;
 ui::rounded(dc,{x,y+3,x+width,y+height+3},RGB(7,10,16),RGB(7,10,16),16);
 auto color=ui::blend(panel,RGB(28,35,47),hot);color=ui::blend(color,green,pulse*.08);
 auto stroke=ui::blend(border,gold,hot*.48);stroke=ui::blend(stroke,green,pulse*.65);
 ui::rounded(dc,{x,y,x+width,y+height},color,stroke,16);
}
void paint(){
 PAINTSTRUCT ps;HDC target=BeginPaint(mainWindow,&ps);RECT client;GetClientRect(mainWindow,&client);
 if(client.right<=0||client.bottom<=0){EndPaint(mainWindow,&ps);return;}
 HDC dc=ui::canvas(target,client.right,client.bottom);int saved=SaveDC(dc);fill(dc,client,bg);SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,1220,860,nullptr);SetViewportExtEx(dc,client.right,client.bottom,nullptr);client={0,0,1220,860};fill(dc,{0,0,196,client.bottom},RGB(16,21,30));
 ui::rounded(dc,{24,27,60,63},RGB(49,41,29),RGB(85,67,37),12);text(dc,L"M",28,26,36,42,gold,titleFont);text(dc,L"MAXIFYING",70,35,124,28,ink,headingFont);
 text(dc,L"PERFORMANCE STUDIO",24,82,160,22,muted,smallFont);line(dc,24,116,148,border);
 text(dc,L"VERSION 0.3",24,client.bottom-112,150,20,gold,smallFont);text(dc,L"Lokale backups\nManuelle valg · fuld gendannelse",24,client.bottom-80,154,54,muted,smallFont);
 text(dc,L"DIN PC. DINE INDSTILLINGER.",224,28,650,24,gold,smallFont);
 text(dc,query.empty()?page:L"Søgeresultater",224,66,620,48,ink,titleFont);
 text(dc,L"Konkrete ændringer. Aktuel status. Gendan når du vil.",224,125,560,30,muted,normalFont);
 text(dc,L"SØG I ALLE INDSTILLINGER",840,96,348,22,muted,smallFont);
 size_t count=backupCount;
 ui::rounded(dc,{12,static_cast<LONG>(ui::navigationY()+5),16,static_cast<LONG>(ui::navigationY()+27)},gold,gold,4);
 text(dc,std::to_wstring(tune::features().size())+L" valg  ·  "+std::to_wstring(count)+L" gemte backups  ·  "+std::to_wstring(priorities.size())+L" aktive procesændringer",224,176,980,27,green,normalFont);
 if(!query.empty()||page==L"Gaming"||page==L"Input"||page==L"Windows"||page==L"Baggrund"||page==L"Privatliv"||page==L"Spilprofil"){
  if(visible.empty())text(dc,L"Ingen indstillinger matcher din søgning.",244,270,800,80,muted,headingFont);
  for(size_t n=0;n<visible.size();++n){auto index=visible[n];auto& f=tune::features()[index];auto& s=states[index];int x=224+static_cast<int>(n%3)*328,y=232+static_cast<int>(n/3)*240;
   box(dc,x,y,312,224,static_cast<int>(index));text(dc,f.category+L" / "+(s.backup?L"BACKUP GEMT":L"INDIVIDUELT VALG"),x+16,y+13,282,20,gold,smallFont);
   text(dc,f.title,x+16,y+42,282,30,ink,headingFont);text(dc,f.description,x+16,y+82,280,58,muted,normalFont);
   text(dc,f.scope,x+16,y+143,280,18,muted,smallFont);
   COLORREF statusColor=s.available?(s.active?green:muted):RGB(228,165,112);
   ui::rounded(dc,{x+16,y+163,x+296,y+183},ui::blend(panel,statusColor,.06),ui::blend(panel,statusColor,.09),8);
   text(dc,s.message,x+23,y+165,266,18,statusColor,smallFont);
  }
  if(page==L"Spilprofil"&&query.empty())text(dc,game.empty()?L"Intet spil valgt":L"Valgt: "+fileName(game),460,740,720,30,muted,normalFont);
 }else if(page==L"Overblik"){
  const wchar_t* titles[]={L"Gamingprofil",L"Inputprofil",L"Fokusprofil",L"Minimal desktop"};
  const wchar_t* descriptions[]={L"Game Mode til. Baggrundsoptagelse fra. Strømprofil vælges separat.",L"Museacceleration og Shift-/Num Lock-genveje fra. Du bekræfter ændringen.",L"Edge/Chrome-baggrund og Edge Startup Boost fra. Genstart browserne.",L"Færre Windows-animationer og mindre gennemsigtighed. Visuel præference."};
  for(int i=0;i<4;++i){int x=224+i*246;box(dc,x,232,230,224);text(dc,L"ÉT KLIK / LOKAL BACKUP",x+16,250,198,20,gold,smallFont);text(dc,titles[i],x+16,282,198,30,ink,headingFont);text(dc,descriptions[i],x+16,322,198,76,muted,normalFont);}
  box(dc,224,480,968,244);text(dc,L"Din hardware",244,498,500,30,ink,headingFont);text(dc,hardware,244,544,900,110,muted,normalFont);
 }else if(page==L"Netværk"){
  box(dc,224,232,968,280);text(dc,L"Ping & pakketab",244,252,900,32,ink,headingFont);text(dc,L"4 ICMP-pakker til en valgfri IPv4-adresse. Måler netværks-RTT, ikke input-latency.",244,292,900,24,muted,normalFont);
  text(dc,pingResult,244,384,900,100,pingResult.rfind(L"0/4",0)==0?RGB(228,165,112):green,normalFont);
  box(dc,224,536,968,188);text(dc,L"DNS-værktøjer",244,552,800,32,ink,headingFont);text(dc,L"Ryd kun cache ved navneopslagsproblemer. Ændrer ikke DNS-server eller garanterer bedre ping.",244,654,900,50,muted,normalFont);
 }else if(page==L"System"){
  box(dc,224,232,968,210);text(dc,L"Hardwareoversigt",244,252,900,30,ink,headingFont);text(dc,hardware,244,296,900,128,muted,normalFont);
  box(dc,224,466,968,258);text(dc,L"Midlertidig spilprioritet",244,480,900,28,ink,headingFont);text(dc,L"Listen viser apps med synlige hovedvinduer, også minimerede. Prioritet gendannes ved normal applukning.\nLuk-knappen beder appen om at lukke normalt. Den kan spørge om at gemme eller blive i systembakken.\nApps uden hovedvindue håndteres via deres systembakke-menu eller opstartsindstillinger.",244,622,890,86,muted,normalFont);
 }else if(page==L"Opstart"){
  box(dc,224,232,968,492);text(dc,L"Styr opstart enkeltvis",244,252,900,32,ink,headingFont);text(dc,L"Vælg en registreret bruger-opstartspost. Originale data gemmes før deaktivering.",244,294,900,28,muted,normalFont);
  text(dc,L"Viser brugerens Run-poster og gemte backups. Windows' egne startgodkendelser kan også påvirke, om programmet starter. Tjenester, planlagte opgaver og andre brugeres poster ændres ikke.",244,474,890,90,muted,normalFont);
  text(dc,L"Deaktivering afinstallerer ikke programmet. Gendan alle finder også disse backups.",244,576,890,34,green,normalFont);
 }else if(page==L"Gendannelse"){
  box(dc,224,232,968,130);text(dc,L"Bevar kontrollen over dine ændringer",244,248,900,30,ink,headingFont);text(dc,L"Gendanner også gemte profiler for tidligere valgte spil. Luk spil før gendannelse.",244,332,900,24,muted,normalFont);
  text(dc,L"Aktivitetshistorik · gemmes lokalt",244,376,900,28,ink,headingFont);
 }
 line(dc,224,788,968,border);
 double reveal=toastReveal.value(static_cast<double>(GetTickCount64()));COLORREF tone=feedback==Feedback::Success?green:feedback==Feedback::Error?RGB(240,138,127):feedback==Feedback::Working?gold:muted;
 int shift=static_cast<int>(6*(1-reveal));ui::rounded(dc,{224,798+shift,1192,848+shift},ui::blend(bg,tone,.055),ui::blend(bg,tone,.16),12);
 ui::rounded(dc,{238,818+shift,245,825+shift},tone,tone,6);text(dc,toast,258,812+shift,914,32,ui::blend(bg,tone,reveal),normalFont);
 RestoreDC(dc,saved);ui::present(target,ps.rcPaint);EndPaint(mainWindow,&ps);
}
void chooseGame(){
 wchar_t filename[32768]={};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof dialog;dialog.hwndOwner=mainWindow;dialog.lpstrFilter=L"Windows-programmer (*.exe)\0*.exe\0\0";dialog.lpstrFile=filename;dialog.nMaxFile=32768;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
 if(GetOpenFileNameW(&dialog)){std::wstring selected=filename;auto attr=GetFileAttributesW(filename);if(attr==INVALID_FILE_ATTRIBUTES||(attr&FILE_ATTRIBUTE_DIRECTORY)||lower(selected).size()<4||lower(selected).substr(selected.size()-4)!=L".exe")throw std::runtime_error("Choose an existing .exe file");game=selected;tune::rememberGame(game);refreshStates();render();notify(L"Valgt spil: "+fileName(game));}
}
void profile(int id){
 std::vector<std::wstring> keys=id==420?std::vector<std::wstring>{L"GameMode",L"Dvr"}:id==421?std::vector<std::wstring>{L"MouseAcceleration",L"StickyHotkey",L"ToggleHotkey"}:id==423?std::vector<std::wstring>{L"EdgeBackground",L"EdgeStartupBoost",L"ChromeBackground"}:std::vector<std::wstring>{L"Transparency",L"WindowAnimation",L"MenuAnimation",L"TooltipAnimation",L"ClientAnimation"};
 std::wstring summary=L"Følgende ændres, og originale værdier gemmes:\n\n";
 for(auto& key:keys)for(auto& f:tune::features())if(f.id==key)summary+=L"• "+f.title+L"\n";
 if(id==423)summary+=L"\nGenstart browserne. Baggrundsudvidelser og notifikationer kan stoppe. Åbne apps og Windows-tjenester lukkes ikke.\n";
 summary+=L"\nEffekten afhænger af hardware, spil og præferencer. Vil du fortsætte?";
 if(MessageBoxW(mainWindow,summary.c_str(),L"Bekræft profil",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
 auto selectedGame=game;
 beginAction([keys,selectedGame]{std::wstring failed;for(auto& key:keys)for(auto& f:tune::features())if(f.id==key){try{tune::apply(f,selectedGame);}catch(const std::exception& e){failed+=f.title+L": "+tune::errorText(e)+L"\n";}}return failed;},L"Profil anvendt. Originale indstillinger er gemt og kan gendannes.");
}
void settings(const wchar_t* uri){if(reinterpret_cast<INT_PTR>(ShellExecuteW(mainWindow,L"open",uri,nullptr,nullptr,SW_SHOWNORMAL))<=32)throw std::runtime_error("Could not open Windows Settings");}
std::wstring ping(const std::wstring& address){
 IN_ADDR destination{};if(InetPtonW(AF_INET,address.c_str(),&destination)!=1)return L"Ugyldig IPv4-adresse. Eksempel: 1.1.1.1";
 HANDLE handle=IcmpCreateFile();if(handle==INVALID_HANDLE_VALUE)return L"ICMP er ikke tilgængeligt på denne maskine.";
 const char data[]="Maxifying network test";std::vector<BYTE> buffer(sizeof(ICMP_ECHO_REPLY)+sizeof(data)+32);DWORD total=0,minimum=MAXDWORD,maximum=0;unsigned received=0;
 for(int i=0;i<4;++i){DWORD result=IcmpSendEcho(handle,destination.S_un.S_addr,const_cast<char*>(data),sizeof(data),nullptr,buffer.data(),static_cast<DWORD>(buffer.size()),700);
  if(result){auto reply=reinterpret_cast<ICMP_ECHO_REPLY*>(buffer.data());if(reply->Status==IP_SUCCESS){++received;total+=reply->RoundTripTime;minimum=std::min(minimum,reply->RoundTripTime);maximum=std::max(maximum,reply->RoundTripTime);}}
 }
 IcmpCloseHandle(handle);if(!received)return L"0/4 svar. ICMP kan være blokeret; det beviser ikke, at internet eller spilforbindelse er nede.";
 return address+L" · "+std::to_wstring(received)+L"/4 svar · "+std::to_wstring((4-received)*25)+L"% pakketab\r\nMin / gennemsnit / maks: "+std::to_wstring(minimum)+L" / "+std::to_wstring(total/received)+L" / "+std::to_wstring(maximum)+L" ms\r\nKun denne ICMP-test. Spillets server kan have en anden RTT.";
}
void exportReport(){
 wchar_t filename[MAX_PATH]=L"Maxifying-rapport.txt";OPENFILENAMEW dialog{};dialog.lStructSize=sizeof dialog;dialog.hwndOwner=mainWindow;dialog.lpstrFilter=L"Tekstfil\0*.txt\0\0";dialog.lpstrFile=filename;dialog.nMaxFile=MAX_PATH;dialog.lpstrDefExt=L"txt";dialog.Flags=OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR;
 if(!GetSaveFileNameW(&dialog))return;
 std::wstring report=L"Maxifying 0.3\r\n\r\n"+hardware+L"\r\n\r\n";
 for(size_t i=0;i<states.size();++i)report+=tune::features()[i].title+L": "+states[i].message+L"\r\n";
 report+=L"\r\nHistorik:\r\n"+tune::history();
 HANDLE file=CreateFileW(filename,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot save report");
 WORD bom=0xfeff;DWORD written=0;BOOL ok=WriteFile(file,&bom,sizeof bom,&written,nullptr);DWORD size=static_cast<DWORD>(report.size()*sizeof(wchar_t));ok=ok&&WriteFile(file,report.data(),size,&written,nullptr)&&written==size;CloseHandle(file);if(!ok)throw std::runtime_error("Report write failed");notify(L"Rapport gemt. Rapporten indeholder hardware og lokal aktivitetshistorik.");
}
void flushDns(){
 wchar_t directory[MAX_PATH];if(!GetSystemDirectoryW(directory,MAX_PATH))throw std::runtime_error("Cannot locate Windows system directory");
 auto executable=std::wstring(directory)+L"\\ipconfig.exe";auto arguments=L"\""+executable+L"\" /flushdns";
 STARTUPINFOW start{};start.cb=sizeof start;PROCESS_INFORMATION process{};
 if(!CreateProcessW(executable.c_str(),arguments.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process))throw std::runtime_error("Cannot run ipconfig");
 CloseHandle(process.hThread);DWORD result=WaitForSingleObject(process.hProcess,5000),exit=1;
 GetExitCodeProcess(process.hProcess,&exit);CloseHandle(process.hProcess);
 if(result!=WAIT_OBJECT_0||exit!=0)throw std::runtime_error("DNS flush did not complete successfully; Windows may require administrator rights");
}
void updateStartupSelection(){
 auto selection=SendDlgItemMessageW(mainWindow,540,CB_GETCURSEL,0,0);bool valid=selection!=CB_ERR&&static_cast<size_t>(selection)<startups.size();
 EnableWindow(GetDlgItem(mainWindow,541),!actionTask.valid()&&valid&&startups[static_cast<size_t>(selection)].present);EnableWindow(GetDlgItem(mainWindow,542),!actionTask.valid()&&valid&&startups[static_cast<size_t>(selection)].backup);
}
void command(int id){
 if(id>=100&&id<100+static_cast<int>(pages.size())){page=pages[static_cast<size_t>(id-100)];offset=0;query.clear();SetWindowTextW(searchBox,L"");render();return;}
 if(id>=1000&&id<1000+static_cast<int>(tune::features().size())*3){size_t index=static_cast<size_t>((id-1000)/3);auto& f=tune::features()[index];int action=(id-1000)%3;
  if(action==2){MessageBoxW(mainWindow,(f.details+L"\n\nAnvend ændrer denne indstilling. Gendan bruger din oprindelige værdi.").c_str(),f.title.c_str(),MB_OK|MB_ICONINFORMATION);return;}
  if(actionTask.valid())return;
  if(action==0){
   bool confirm=f.kind==tune::Kind::Power||f.kind==tune::Kind::Fullscreen||f.spi==SPI_GETFILTERKEYS||f.spi==SPI_GETSTICKYKEYS||f.spi==SPI_GETTOGGLEKEYS||f.spi==SPI_GETMOUSESPEED;
   if(confirm&&MessageBoxW(mainWindow,(f.details+L"\n\nVil du anvende ændringen?").c_str(),f.title.c_str(),MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
  }
  auto feature=f;auto selectedGame=game;
  beginAction([feature,selectedGame,action]{if(action==0)tune::apply(feature,selectedGame);else tune::restore(feature,selectedGame);return std::wstring{};},(action==0?L"Anvendt: ":L"Gendannet: ")+f.title+L". "+f.scope,static_cast<int>(index));return;
 }
 if(actionTask.valid()){notify(L"Vent et øjeblik — den aktuelle ændring afsluttes.",Feedback::Working);return;}
 switch(id){
 case 401:offset=offset>=6?offset-6:0;render();break;
 case 402:offset+=6;render();break;
 case 410:chooseGame();break;
 case 420:case 421:case 422:case 423:profile(id);break;
 case 430:page=L"Gaming";render();break;
 case 431:settings(L"ms-settings:display-advancedgraphics");break;
 case 432:settings(L"ms-settings:startupapps");break;
 case 502:boostProcess();render();break;
 case 503:restorePriorities();notify(L"Midlertidige procesprioriteter gendannet.",Feedback::Success);render();break;
 case 504:render(true);break;
 case 505:requestAppClose();break;
 case 511:{wchar_t value[64];GetDlgItemTextW(mainWindow,510,value,64);std::wstring address=value;IN_ADDR parsed{};if(InetPtonW(AF_INET,address.c_str(),&parsed)!=1)throw std::runtime_error("Enter a valid IPv4 address");if(!pingTask.valid()){pingResult=L"Måler 4 pakker…";pingTask=std::async(std::launch::async,[address]{return ping(address);});EnableWindow(GetDlgItem(mainWindow,511),FALSE);InvalidateRect(mainWindow,nullptr,FALSE);}break;}
 case 512:if(MessageBoxW(mainWindow,L"Ryd Windows' DNS-cache? Dette er fejlfinding, ikke en garanteret ping-forbedring.",L"DNS-cache",MB_YESNO|MB_ICONQUESTION)==IDYES){beginAction([]{flushDns();tune::log(L"DNS-cache ryddet");return std::wstring{};},L"DNS-cache ryddet.");}break;
 case 513:settings(L"ms-settings:network-status");break;
 case 520:if(MessageBoxW(mainWindow,L"Gendan ALLE gemte indstillinger, inklusive tidligere spilprofiler og aktive procesprioriteter? Luk spil først.",L"Gendan alle",MB_YESNO|MB_ICONQUESTION)==IDYES){std::wstring priorError;try{restorePriorities();}catch(const std::exception& e){priorError=tune::errorText(e);}
  beginAction([priorError]{std::wstring failed=priorError;try{tune::restoreAll();}catch(const std::exception& e){failed+=L"\n"+tune::errorText(e);}return failed;},L"Alle gemte indstillinger er gendannet.",-1,true);}break;
 case 541:case 542:{auto selected=SendDlgItemMessageW(mainWindow,540,CB_GETCURSEL,0,0);if(selected==CB_ERR||static_cast<size_t>(selected)>=startups.size())throw std::runtime_error("Select a startup entry first");auto entry=startups[static_cast<size_t>(selected)];
  if(id==541){if(MessageBoxW(mainWindow,(L"Deaktivér opstart for "+entry.name+L"? Programmet afinstalleres ikke. Originale data gemmes til gendannelse.").c_str(),L"Opstartsprogram",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;}
  beginAction([entry,id]{if(id==541)tune::disableStartup(entry.name);else tune::restoreStartup(entry.name);return std::wstring{};},L"Opstart opdateret: "+entry.name,-1,true);break;}
 case 543:render(true);break;
 case 544:settings(L"ms-settings:startupapps");break;
 case 521:exportReport();break;
 case 522:refreshStates();hardware=tune::hardwareSummary();render();notify(L"Status opdateret fra Windows.");break;
 }
}
void screenshot(const std::wstring& filename){
 UpdateWindow(mainWindow);
 RECT r;GetClientRect(mainWindow,&r);int width=r.right,height=r.bottom;HDC dc=GetDC(mainWindow),copy=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,width,height);auto old=SelectObject(copy,bitmap);BitBlt(copy,0,0,width,height,dc,0,0,SRCCOPY);SelectObject(copy,old);
 BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 std::vector<BYTE> pixels(static_cast<size_t>(width)*height*4);GetDIBits(copy,bitmap,0,height,pixels.data(),&info,DIB_RGB_COLORS);BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof header+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+static_cast<DWORD>(pixels.size());
 HANDLE file=CreateFileW(filename.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);DWORD written;
 if(file!=INVALID_HANDLE_VALUE){WriteFile(file,&header,sizeof header,&written,nullptr);WriteFile(file,&info.bmiHeader,sizeof info.bmiHeader,&written,nullptr);WriteFile(file,pixels.data(),static_cast<DWORD>(pixels.size()),&written,nullptr);CloseHandle(file);}
 DeleteObject(bitmap);DeleteDC(copy);ReleaseDC(mainWindow,dc);
}
bool smoke=false,smokeFailed=false,smokeChecks=false;int smokeStep=0;
void smokeRegression(){
 HWND stable=GetDlgItem(mainWindow,420),focus=GetFocus();SetFocus(stable);DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 for(int i=0;i<30;++i){render();UpdateWindow(mainWindow);if(GetDlgItem(mainWindow,420)!=stable||GetFocus()!=stable)smokeFailed=true;}
 DWORD after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);if(before&&after>before+4)smokeFailed=true;
 std::ofstream report("maxifying-ui-test.txt");report<<(smokeFailed?"FAIL":"PASS")<<": 30 status refreshes preserve HWND and keyboard focus\n";
 if(before)report<<"GDI objects before/after: "<<before<<"/"<<after<<"\n";else report<<"SKIP: GDI resource accounting is not available in this runtime\n";
 if(focus)SetFocus(focus);
 beginAction([]{Sleep(180);return std::wstring{};},L"UI-test afsluttet. Ingen systemindstillinger ændret.");
 command(101);if(page!=L"Gaming"||!actionTask.valid())smokeFailed=true;command(100);
 report<<(smokeFailed?"FAIL":"PASS")<<": page navigation remains responsive while worker is pending\n";
}
LRESULT CALLBACK procedure(HWND w,UINT message,WPARAM a,LPARAM b){
 switch(message){
 case WM_CREATE:mainWindow=w;ui::initialize(w,controlFont,uiScale);toastReveal.snap(1);for(size_t i=0;i<pages.size();++i)button(pages[i],100+static_cast<int>(i),16,142+static_cast<int>(i)*48,164,true,false);
  searchBox=control(L"EDIT",L"",300,840,120,348,34,WS_BORDER|ES_AUTOHSCROLL,false);SendMessageW(searchBox,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Søg efter indstilling…"));
  try{game=tune::selectedGame();}catch(...){}hardware=tune::hardwareSummary();refreshStates();render();SetTimer(w,1,1000,nullptr);return 0;
 case WM_PAINT:paint();return 0;
 case WM_ERASEBKGND:return 1;
 case WM_MEASUREITEM:{auto item=reinterpret_cast<MEASUREITEMSTRUCT*>(b);if(item->CtlType==ODT_COMBOBOX){item->itemHeight=static_cast<UINT>(px(26));return TRUE;}break;}
 case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(b);
  if(item->CtlType==ODT_COMBOBOX){fill(item->hDC,item->rcItem,(item->itemState&ODS_SELECTED)?RGB(65,52,32):panel);std::wstring label=L"Ingen tilgængelige poster";
   if(item->itemID!=static_cast<UINT>(-1)){auto size=SendMessageW(item->hwndItem,CB_GETLBTEXTLEN,item->itemID,0);if(size>=0){label.assign(static_cast<size_t>(size)+1,L'\0');SendMessageW(item->hwndItem,CB_GETLBTEXT,item->itemID,reinterpret_cast<LPARAM>(label.data()));}}
   auto previous=SelectObject(item->hDC,controlFont);SetTextColor(item->hDC,ink);SetBkMode(item->hDC,TRANSPARENT);RECT r=item->rcItem;r.left+=px(8);DrawTextW(item->hDC,label.c_str(),-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);SelectObject(item->hDC,previous);return TRUE;}
  if(item->CtlType!=ODT_BUTTON)return FALSE;
  ui::drawButton(*item);return TRUE;}
 case WM_CTLCOLORLISTBOX:case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:SetTextColor(reinterpret_cast<HDC>(a),ink);SetBkColor(reinterpret_cast<HDC>(a),panel);return reinterpret_cast<LRESULT>(fieldBrush);
 case WM_COMMAND:try{
  if(LOWORD(a)==300&&HIWORD(a)==EN_CHANGE){wchar_t value[256];GetWindowTextW(searchBox,value,256);query=value;offset=0;render();}
  else if(LOWORD(a)==540&&HIWORD(a)==CBN_SELCHANGE)updateStartupSelection();
  else if(HIWORD(a)==BN_CLICKED)command(LOWORD(a));
 }catch(const std::exception& e){tune::log(L"Fejl: "+tune::errorText(e));notify(L"Handlingen kunne ikke gennemføres. Eventuelle backups bevares.");MessageBoxW(w,tune::errorText(e).c_str(),L"Maxifying · handling ikke gennemført",MB_OK|MB_ICONWARNING);refreshStates();render();}return 0;
 case ui::requestFrame:ensureFrames();return 0;
 case WM_MOUSEMOVE:if(!parentTracking){TRACKMOUSEEVENT track{sizeof track,TME_LEAVE,w,0};TrackMouseEvent(&track);parentTracking=true;}[[fallthrough]];
 case ui::pointerMoved:{
  POINT point;GetCursorPos(&point);ScreenToClient(w,&point);double x=point.x/uiScale-224,y=point.y/uiScale-232;int card=-1;
  if(x>=0&&y>=0&&x<984&&y<480&&static_cast<int>(x)%328<312&&static_cast<int>(y)%240<224){size_t at=static_cast<size_t>(y/240)*3+static_cast<size_t>(x/328);if(at<visible.size())card=static_cast<int>(visible[at]);}
  ui::hoverCard(card);return 0;}
 case WM_MOUSELEAVE:parentTracking=false;PostMessageW(w,ui::pointerMoved,0,0);return 0;
 case WM_TIMER:
  if(a==2){finishAction();bool moving=ui::tick();bool toastMoving=toastReveal.moving(static_cast<double>(GetTickCount64()));if(toastMoving||actionTask.valid())invalidate(220,792,980,66);if(!moving&&!toastMoving&&!actionTask.valid()){KillTimer(w,2);animationTimer=false;}return 0;}
  if(pingTask.valid()&&pingTask.wait_for(std::chrono::seconds(0))==std::future_status::ready){try{pingResult=pingTask.get();}catch(...){pingResult=L"Pingtesten kunne ikke gennemføres.";}if(page==L"Netværk")EnableWindow(GetDlgItem(w,511),TRUE);InvalidateRect(w,nullptr,FALSE);}
  if(smoke){
   if(!smokeChecks){smokeChecks=true;smokeRegression();return 0;}
   if(actionTask.valid())return 0;
   if(page==L"Netværk"&&pingTask.valid())return 0;
   screenshot(L"maxifying-page"+std::to_wstring(smokeStep)+L".bmp");++smokeStep;
   if(smokeStep<static_cast<int>(pages.size())){page=pages[static_cast<size_t>(smokeStep)];render();if(page==L"Netværk"){SetDlgItemTextW(w,510,L"127.0.0.1");command(511);}}
   else if(smokeStep==static_cast<int>(pages.size())){SetWindowTextW(searchBox,L"mus");}
   else PostMessageW(w,WM_CLOSE,0,0);
   return 0;
  }

  {auto next=tune::hardwareSummary();if(next!=hardware){hardware=next;if(page==L"Overblik")invalidate(236,536,920,120);else if(page==L"System")invalidate(236,288,920,142);}}return 0;
 case WM_CLOSE:if(actionTask.valid()){notify(L"Den aktuelle ændring afsluttes. Prøv at lukke igen om et øjeblik.",Feedback::Working);return 0;}try{restorePriorities();}catch(...){if(MessageBoxW(w,L"Nogle procesprioriteter kunne ikke gendannes. De varer indtil spillet afsluttes. Vil du lukke alligevel?",L"Aktive procesændringer",MB_YESNO|MB_ICONWARNING)!=IDYES)return 0;}DestroyWindow(w);return 0;
 case WM_DESTROY:KillTimer(w,1);KillTimer(w,2);ui::dispose();closeProcesses();for(auto& p:priorities)CloseHandle(p.handle);priorities.clear();PostQuitMessage(smokeFailed?1:0);return 0;
 }
 return DefWindowProcW(w,message,a,b);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR arguments,int show){
 HANDLE mutex=CreateMutexW(nullptr,TRUE,L"Local\\GameTuneSingleInstance");if(!mutex)return 1;
 if(GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(nullptr,L"Maxifying eller den tidligere GameTune-version er allerede åben.",L"Maxifying",MB_OK);CloseHandle(mutex);return 0;}
 smoke=std::wstring(arguments).find(L"--smoke-ui")!=std::wstring::npos;
 SetProcessDPIAware();RECT desktop{};SystemParametersInfoW(SPI_GETWORKAREA,0,&desktop,0);uiScale=std::min({1.0,(desktop.right-desktop.left-32)/1220.0,(desktop.bottom-desktop.top-64)/860.0});if(uiScale<=0)uiScale=1.0;if(smoke&&std::wstring(arguments).find(L"--small")!=std::wstring::npos)uiScale=0.78;controlFont=font(px(16),FW_NORMAL);normalFont=font(16,FW_NORMAL);smallFont=font(13,FW_NORMAL);headingFont=font(20,FW_SEMIBOLD);titleFont=font(34,FW_BOLD);background=CreateSolidBrush(bg);fieldBrush=CreateSolidBrush(panel);
 WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=procedure;cls.lpszClassName=L"MaxifyingWindow";cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=background;if(!RegisterClassW(&cls))return 1;
 RECT bounds{0,0,px(1220),px(860)};AdjustWindowRect(&bounds,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE);
 HWND w=CreateWindowW(cls.lpszClassName,L"Maxifying 0.3 · Performance Studio",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);if(!w)return 1;
 BOOL dark=TRUE;DwmSetWindowAttribute(w,20,&dark,sizeof dark);DwmSetWindowAttribute(w,34,&gold,sizeof gold);
 WSADATA sockets;WSAStartup(MAKEWORD(2,2),&sockets);ShowWindow(w,show);MSG msg;BOOL result;
 while((result=GetMessageW(&msg,nullptr,0,0))>0){if(!IsDialogMessageW(w,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
 if(pingTask.valid())pingTask.wait();
 WSACleanup();DeleteObject(controlFont);DeleteObject(normalFont);DeleteObject(smallFont);DeleteObject(headingFont);DeleteObject(titleFont);DeleteObject(background);DeleteObject(fieldBrush);ReleaseMutex(mutex);CloseHandle(mutex);return result==-1?1:static_cast<int>(msg.wParam);
}
