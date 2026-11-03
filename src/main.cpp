#include <winsock2.h>
#include <ws2tcpip.h>
#include "engine.h"
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
size_t offset=0;
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
void notify(const std::wstring& message){toast=message;InvalidateRect(mainWindow,nullptr,FALSE);}
HWND control(const wchar_t* cls,const std::wstring& label,int id,int x,int y,int width,int height,DWORD style=0,bool dynamic=true){
 HWND w=CreateWindowExW(0,cls,label.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,px(x),px(y),px(width),px(height),mainWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
 SendMessageW(w,WM_SETFONT,reinterpret_cast<WPARAM>(controlFont),TRUE);if(dynamic)dynamicControls.push_back(w);return w;
}
HWND button(const std::wstring& label,int id,int x,int y,int width=136,bool enabled=true,bool dynamic=true){
 auto w=control(L"BUTTON",label,id,x,y,width,32,BS_OWNERDRAW,dynamic);EnableWindow(w,enabled);return w;
}
std::wstring lower(std::wstring value){std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return std::towlower(c);});return value;}
std::wstring fileName(const std::wstring& path){auto at=path.find_last_of(L"\\/");return at==std::wstring::npos?path:path.substr(at+1);}
void refreshStates(){
 states.assign(tune::features().size(),{});
 for(size_t i=0;i<states.size();++i){auto& s=states[i];auto& f=tune::features()[i];
  if((f.kind==tune::Kind::Gpu||f.kind==tune::Kind::Fullscreen)&&game.empty()){s.message=L"Vælg en .exe først";continue;}
  try{s.active=tune::active(f,game);s.backup=tune::backedUp(f,game);s.available=true;s.message=s.active?L"Ønsket indstilling er aktiv":L"Windows-indstilling kan tilpasses";}
  catch(const std::exception& e){s.message=L"Utilgængelig: "+tune::errorText(e);try{s.backup=tune::backedUp(f,game);}catch(...) {}}
 }
}
void closeProcesses(){for(auto& p:processes)CloseHandle(p.handle);processes.clear();}
void listProcesses(){
 closeProcesses();HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
 if(snapshot==INVALID_HANDLE_VALUE)return;
 PROCESSENTRY32W item{};item.dwSize=sizeof item;
 if(Process32FirstW(snapshot,&item)){do{
  if(item.th32ProcessID==GetCurrentProcessId())continue;
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
 notify(L"Above Normal anvendt. Gendannes ved normal lukning eller manuelt.");
}
void render(){
 if(rebuilding)return;
 rebuilding=true;
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
   const auto& s=states[i];button(L"Anvend",1000+static_cast<int>(i)*3,x+16,y+186,88,s.available&&!s.active);
   button(L"Gendan",1001+static_cast<int>(i)*3,x+110,y+186,88,s.backup);
   button(L"Info",1002+static_cast<int>(i)*3,x+204,y+186,88);
  }
  if(matches.size()>6){button(L"Forrige",401,224,734,118,offset>0);button(L"Næste",402,350,734,118,offset+6<matches.size());}
  if(page==L"Spilprofil"&&!searching)button(L"Vælg spillets .exe",410,224,734,220);
 }else if(page==L"Overblik"){
  button(L"Anvend gamingprofil",420,240,408,276);button(L"Anvend inputprofil",421,568,408,276);button(L"Anvend skrivebordsprofil",422,896,408,276);
  button(L"Se alle gamingvalg",430,240,676,276);button(L"Åbn grafikindstillinger",431,568,676,276);button(L"Opstartsapps",432,896,676,276);
 }else if(page==L"Netværk"){
  control(L"EDIT",L"1.1.1.1",510,244,324,260,34,WS_BORDER|ES_AUTOHSCROLL);
  button(L"Mål ping",511,520,324,170,!pingTask.valid());button(L"Ryd DNS-cache",512,244,598,240);button(L"Åbn netværksindstillinger",513,500,598,290);
 }else if(page==L"System"){
  listProcesses();auto combo=control(L"COMBOBOX",L"",501,244,510,890,300,CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL);
  for(auto& p:processes)SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(p.label.c_str()));
  if(!processes.empty())SendMessageW(combo,CB_SETCURSEL,0,0);
  button(L"Above Normal",502,244,566,220);button(L"Gendan prioriteter",503,482,566,220,!priorities.empty());button(L"Opdater procesliste",504,720,566,220);
 }else if(page==L"Opstart"){
  startups=tune::startupEntries();auto combo=control(L"COMBOBOX",L"",540,244,330,890,300,CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL);
  for(auto& entry:startups){auto label=entry.name+(entry.present?L" · Run-post findes":L" · Run-post fjernet")+(entry.backup?L" · backup gemt":L"");SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
  if(!startups.empty())SendMessageW(combo,CB_SETCURSEL,0,0);
  button(L"Deaktivér opstart",541,244,396,240,!startups.empty()&&startups[0].present);button(L"Gendan opstart",542,500,396,240,!startups.empty()&&startups[0].backup);button(L"Opdater liste",543,756,396,240);button(L"Alle opstartsapps i Windows",544,244,620,330);
 }else if(page==L"Gendannelse"){
  button(L"Gendan alle ændringer",520,244,284,300);button(L"Eksportér rapport",521,562,284,230);button(L"Opdater status",522,810,284,230);
  auto log=control(L"EDIT",tune::history(),523,244,408,890,300,ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER);SendMessageW(log,EM_SETSEL,0,0);
 }
 rebuilding=false;InvalidateRect(mainWindow,nullptr,TRUE);EnumChildWindows(mainWindow,[](HWND child,LPARAM)->BOOL{InvalidateRect(child,nullptr,TRUE);return TRUE;},0);
}
void box(HDC dc,int x,int y,int width,int height){fill(dc,{x,y,x+width,y+height},panel);line(dc,x,y,width,border);}
void paint(){
 PAINTSTRUCT ps;HDC dc=BeginPaint(mainWindow,&ps);RECT client;GetClientRect(mainWindow,&client);fill(dc,client,bg);SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,1220,860,nullptr);SetViewportExtEx(dc,client.right,client.bottom,nullptr);client={0,0,1220,860};fill(dc,{0,0,196,client.bottom},RGB(16,21,30));
 text(dc,L"G",24,26,48,42,gold,titleFont);text(dc,L"GAMETUNE",68,32,124,30,ink,headingFont);
 text(dc,L"PERFORMANCE STUDIO",24,82,160,22,muted,smallFont);line(dc,24,116,148,border);
 text(dc,L"VERSION 0.2",24,client.bottom-112,150,20,gold,smallFont);text(dc,L"Lokale backups\nManuelle valg · fuld gendannelse",24,client.bottom-80,154,54,muted,smallFont);
 text(dc,L"DIN PC. DINE INDSTILLINGER.",224,28,650,24,gold,smallFont);
 text(dc,query.empty()?page:L"Søgeresultater",224,66,620,48,ink,titleFont);
 text(dc,L"Konkrete ændringer. Aktuel status. Gendan når du vil.",224,125,560,30,muted,normalFont);
 text(dc,L"SØG I ALLE INDSTILLINGER",840,96,348,22,muted,smallFont);
 size_t count=0;try{count=tune::backupIds().size();}catch(...){}
 text(dc,std::to_wstring(tune::features().size())+L" valg  ·  "+std::to_wstring(count)+L" gemte backups  ·  "+std::to_wstring(priorities.size())+L" aktive procesændringer",224,176,980,27,green,normalFont);
 if(!query.empty()||page==L"Gaming"||page==L"Input"||page==L"Windows"||page==L"Baggrund"||page==L"Privatliv"||page==L"Spilprofil"){
  if(visible.empty())text(dc,L"Ingen indstillinger matcher din søgning.",244,270,800,80,muted,headingFont);
  for(size_t n=0;n<visible.size();++n){auto index=visible[n];auto& f=tune::features()[index];auto& s=states[index];int x=224+static_cast<int>(n%3)*328,y=232+static_cast<int>(n/3)*240;
   box(dc,x,y,312,224);text(dc,f.category+L" / "+(s.backup?L"BACKUP GEMT":L"INDIVIDUELT VALG"),x+16,y+13,282,20,gold,smallFont);
   text(dc,f.title,x+16,y+42,282,30,ink,headingFont);text(dc,f.description,x+16,y+82,280,58,muted,normalFont);
   text(dc,f.scope,x+16,y+143,280,18,muted,smallFont);text(dc,s.message,x+16,y+165,282,18,s.available?(s.active?green:muted):RGB(228,165,112),smallFont);
  }
  if(page==L"Spilprofil"&&query.empty())text(dc,game.empty()?L"Intet spil valgt":L"Valgt: "+fileName(game),460,740,720,30,muted,normalFont);
 }else if(page==L"Overblik"){
  const wchar_t* titles[]={L"Gamingprofil",L"Inputprofil",L"Minimal desktop"};
  const wchar_t* descriptions[]={L"Game Mode til. Baggrundsoptagelse fra. Strømprofil og eksperimenter vælges separat.",L"Museacceleration fra. Sticky Keys- og Toggle Keys-genveje fra. Kræver bekræftelse.",L"Reducer Windows-animationer og gennemsigtighed. En skrivebordspræference, ikke et FPS-løfte."};
  for(int i=0;i<3;++i){int x=224+i*328;box(dc,x,232,312,224);text(dc,L"GENNEMSIGTIG PROFIL",x+16,250,280,20,gold,smallFont);text(dc,titles[i],x+16,282,280,30,ink,headingFont);text(dc,descriptions[i],x+16,324,280,70,muted,normalFont);}
  box(dc,224,480,968,244);text(dc,L"Din hardware",244,498,500,30,ink,headingFont);text(dc,hardware,244,544,900,110,muted,normalFont);
 }else if(page==L"Netværk"){
  box(dc,224,232,968,280);text(dc,L"Ping & pakketab",244,252,900,32,ink,headingFont);text(dc,L"4 ICMP-pakker til en valgfri IPv4-adresse. Måler netværks-RTT, ikke input-latency.",244,292,900,24,muted,normalFont);
  text(dc,pingResult,244,384,900,100,pingResult.rfind(L"0/4",0)==0?RGB(228,165,112):green,normalFont);
  box(dc,224,536,968,188);text(dc,L"DNS-værktøjer",244,552,800,32,ink,headingFont);text(dc,L"Ryd kun cache ved navneopslagsproblemer. Ændrer ikke DNS-server eller garanterer bedre ping.",244,654,900,50,muted,normalFont);
 }else if(page==L"System"){
  box(dc,224,232,968,210);text(dc,L"Hardwareoversigt",244,252,900,30,ink,headingFont);text(dc,hardware,244,296,900,128,muted,normalFont);
  box(dc,224,466,968,258);text(dc,L"Midlertidig spilprioritet",244,480,900,28,ink,headingFont);text(dc,L"Kun til den valgte, kørende proces. Vælg spillets rigtige proces, ikke launcheren. Beskyttede processer vises ikke.\nGendannes ved normal applukning; ved crash varer ændringen indtil spillet afsluttes. Kan påvirke andre apps.",244,622,890,86,muted,normalFont);
 }else if(page==L"Opstart"){
  box(dc,224,232,968,492);text(dc,L"Styr opstart enkeltvis",244,252,900,32,ink,headingFont);text(dc,L"Vælg en registreret bruger-opstartspost. Originale data gemmes før deaktivering.",244,294,900,28,muted,normalFont);
  text(dc,L"Viser brugerens Run-poster og gemte backups. Windows' egne startgodkendelser kan også påvirke, om programmet starter. Tjenester, planlagte opgaver og andre brugeres poster ændres ikke.",244,474,890,90,muted,normalFont);
  text(dc,L"Deaktivering afinstallerer ikke programmet. Gendan alle finder også disse backups.",244,576,890,34,green,normalFont);
 }else if(page==L"Gendannelse"){
  box(dc,224,232,968,130);text(dc,L"Bevar kontrollen over dine ændringer",244,248,900,30,ink,headingFont);text(dc,L"Gendanner også gemte profiler for tidligere valgte spil. Luk spil før gendannelse.",244,332,900,24,muted,normalFont);
  text(dc,L"Aktivitetshistorik · gemmes lokalt",244,376,900,28,ink,headingFont);
 }
 line(dc,224,788,968,border);text(dc,toast,224,802,968,44,muted,normalFont);EndPaint(mainWindow,&ps);
}
void chooseGame(){
 wchar_t filename[32768]={};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof dialog;dialog.hwndOwner=mainWindow;dialog.lpstrFilter=L"Windows-programmer (*.exe)\0*.exe\0\0";dialog.lpstrFile=filename;dialog.nMaxFile=32768;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
 if(GetOpenFileNameW(&dialog)){std::wstring selected=filename;auto attr=GetFileAttributesW(filename);if(attr==INVALID_FILE_ATTRIBUTES||(attr&FILE_ATTRIBUTE_DIRECTORY)||lower(selected).size()<4||lower(selected).substr(selected.size()-4)!=L".exe")throw std::runtime_error("Choose an existing .exe file");game=selected;tune::rememberGame(game);refreshStates();render();notify(L"Valgt spil: "+fileName(game));}
}
void profile(int id){
 std::vector<std::wstring> keys=id==420?std::vector<std::wstring>{L"GameMode",L"Dvr"}:id==421?std::vector<std::wstring>{L"MouseAcceleration",L"StickyHotkey",L"ToggleHotkey"}:std::vector<std::wstring>{L"Transparency",L"WindowAnimation",L"MenuAnimation",L"TooltipAnimation",L"ClientAnimation"};
 std::wstring summary=L"Følgende ændres, og originale værdier gemmes:\n\n";
 for(auto& key:keys)for(auto& f:tune::features())if(f.id==key)summary+=L"• "+f.title+L"\n";
 summary+=L"\nEffekten afhænger af hardware, spil og præferencer. Vil du fortsætte?";
 if(MessageBoxW(mainWindow,summary.c_str(),L"Bekræft profil",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
 size_t applied=0;std::wstring failed;
 for(auto& key:keys)for(auto& f:tune::features())if(f.id==key){try{tune::apply(f,game);++applied;}catch(const std::exception& e){failed+=f.title+L": "+tune::errorText(e)+L"\n";}}
 refreshStates();render();notify(std::to_wstring(applied)+L" indstillinger anvendt. Genstart spillet, hvis relevant.");
 if(!failed.empty())MessageBoxW(mainWindow,(L"Nogle ændringer blev ikke gennemført. Backups bevares:\n"+failed).c_str(),L"Profil delvist anvendt",MB_OK|MB_ICONWARNING);
}
void settings(const wchar_t* uri){if(reinterpret_cast<INT_PTR>(ShellExecuteW(mainWindow,L"open",uri,nullptr,nullptr,SW_SHOWNORMAL))<=32)throw std::runtime_error("Could not open Windows Settings");}
std::wstring ping(const std::wstring& address){
 IN_ADDR destination{};if(InetPtonW(AF_INET,address.c_str(),&destination)!=1)return L"Ugyldig IPv4-adresse. Eksempel: 1.1.1.1";
 HANDLE handle=IcmpCreateFile();if(handle==INVALID_HANDLE_VALUE)return L"ICMP er ikke tilgængeligt på denne maskine.";
 const char data[]="GameTune network test";std::vector<BYTE> buffer(sizeof(ICMP_ECHO_REPLY)+sizeof(data)+32);DWORD total=0,minimum=MAXDWORD,maximum=0;unsigned received=0;
 for(int i=0;i<4;++i){DWORD result=IcmpSendEcho(handle,destination.S_un.S_addr,const_cast<char*>(data),sizeof(data),nullptr,buffer.data(),static_cast<DWORD>(buffer.size()),700);
  if(result){auto reply=reinterpret_cast<ICMP_ECHO_REPLY*>(buffer.data());if(reply->Status==IP_SUCCESS){++received;total+=reply->RoundTripTime;minimum=std::min(minimum,reply->RoundTripTime);maximum=std::max(maximum,reply->RoundTripTime);}}
 }
 IcmpCloseHandle(handle);if(!received)return L"0/4 svar. ICMP kan være blokeret; det beviser ikke, at internet eller spilforbindelse er nede.";
 return address+L" · "+std::to_wstring(received)+L"/4 svar · "+std::to_wstring((4-received)*25)+L"% pakketab\r\nMin / gennemsnit / maks: "+std::to_wstring(minimum)+L" / "+std::to_wstring(total/received)+L" / "+std::to_wstring(maximum)+L" ms\r\nKun denne ICMP-test. Spillets server kan have en anden RTT.";
}
void exportReport(){
 wchar_t filename[MAX_PATH]=L"GameTune-rapport.txt";OPENFILENAMEW dialog{};dialog.lStructSize=sizeof dialog;dialog.hwndOwner=mainWindow;dialog.lpstrFilter=L"Tekstfil\0*.txt\0\0";dialog.lpstrFile=filename;dialog.nMaxFile=MAX_PATH;dialog.lpstrDefExt=L"txt";dialog.Flags=OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR;
 if(!GetSaveFileNameW(&dialog))return;
 std::wstring report=L"GameTune 0.2\r\n\r\n"+hardware+L"\r\n\r\n";
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
 EnableWindow(GetDlgItem(mainWindow,541),valid&&startups[static_cast<size_t>(selection)].present);EnableWindow(GetDlgItem(mainWindow,542),valid&&startups[static_cast<size_t>(selection)].backup);
}
void command(int id){
 if(id>=100&&id<100+static_cast<int>(pages.size())){page=pages[static_cast<size_t>(id-100)];offset=0;query.clear();SetWindowTextW(searchBox,L"");render();return;}
 if(id>=1000&&id<1000+static_cast<int>(tune::features().size())*3){size_t index=static_cast<size_t>((id-1000)/3);auto& f=tune::features()[index];int action=(id-1000)%3;
  if(action==2){MessageBoxW(mainWindow,(f.details+L"\n\nAnvend ændrer denne indstilling. Gendan bruger din oprindelige værdi.").c_str(),f.title.c_str(),MB_OK|MB_ICONINFORMATION);return;}
  if(action==0){if(MessageBoxW(mainWindow,(f.details+L"\n\nVil du anvende ændringen?").c_str(),f.title.c_str(),MB_YESNO|MB_ICONQUESTION)!=IDYES)return;tune::apply(f,game);notify(L"Anvendt: "+f.title+L". "+f.scope);}
  else{tune::restore(f,game);notify(L"Gendannet: "+f.title);}
  refreshStates();render();return;
 }
 switch(id){
 case 401:offset=offset>=6?offset-6:0;render();break;
 case 402:offset+=6;render();break;
 case 410:chooseGame();break;
 case 420:case 421:case 422:profile(id);break;
 case 430:page=L"Gaming";render();break;
 case 431:settings(L"ms-settings:display-advancedgraphics");break;
 case 432:settings(L"ms-settings:startupapps");break;
 case 502:boostProcess();render();break;
 case 503:restorePriorities();notify(L"Midlertidige procesprioriteter gendannet.");render();break;
 case 504:render();break;
 case 511:{wchar_t value[64];GetDlgItemTextW(mainWindow,510,value,64);std::wstring address=value;IN_ADDR parsed{};if(InetPtonW(AF_INET,address.c_str(),&parsed)!=1)throw std::runtime_error("Enter a valid IPv4 address");if(!pingTask.valid()){pingResult=L"Måler 4 pakker…";pingTask=std::async(std::launch::async,[address]{return ping(address);});EnableWindow(GetDlgItem(mainWindow,511),FALSE);InvalidateRect(mainWindow,nullptr,FALSE);}break;}
 case 512:if(MessageBoxW(mainWindow,L"Ryd Windows' DNS-cache? Dette er fejlfinding, ikke en garanteret ping-forbedring.",L"DNS-cache",MB_YESNO|MB_ICONQUESTION)==IDYES){flushDns();notify(L"DNS-cache ryddet.");tune::log(L"DNS-cache ryddet");}break;
 case 513:settings(L"ms-settings:network-status");break;
 case 520:if(MessageBoxW(mainWindow,L"Gendan ALLE gemte indstillinger, inklusive tidligere spilprofiler og aktive procesprioriteter? Luk spil først.",L"Gendan alle",MB_YESNO|MB_ICONQUESTION)==IDYES){bool failed=false;try{restorePriorities();}catch(...){failed=true;}try{tune::restoreAll();}catch(...){failed=true;}refreshStates();render();if(failed)throw std::runtime_error("Some changes could not be restored. Remaining backups are retained. Retry Restore All.");notify(L"Alle gemte indstillinger er gendannet.");}break;
 case 541:case 542:{auto selected=SendDlgItemMessageW(mainWindow,540,CB_GETCURSEL,0,0);if(selected==CB_ERR||static_cast<size_t>(selected)>=startups.size())throw std::runtime_error("Select a startup entry first");auto entry=startups[static_cast<size_t>(selected)];
  if(id==541){if(MessageBoxW(mainWindow,(L"Deaktivér opstart for "+entry.name+L"? Programmet afinstalleres ikke. Originale data gemmes til gendannelse.").c_str(),L"Opstartsprogram",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;tune::disableStartup(entry.name);}
  else tune::restoreStartup(entry.name);
  notify(L"Opstart opdateret: "+entry.name);render();break;}
 case 543:render();break;
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
bool smoke=false;int smokeStep=0;
LRESULT CALLBACK procedure(HWND w,UINT message,WPARAM a,LPARAM b){
 switch(message){
 case WM_CREATE:mainWindow=w;for(size_t i=0;i<pages.size();++i)button(pages[i],100+static_cast<int>(i),16,142+static_cast<int>(i)*48,164,true,false);
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
  bool nav=item->CtlID>=100&&item->CtlID<100+pages.size();bool selected=nav&&pages[item->CtlID-100]==page;bool disabled=item->itemState&ODS_DISABLED;
  fill(item->hDC,item->rcItem,disabled?RGB(26,31,40):selected?RGB(65,52,32):(item->itemState&ODS_SELECTED)?RGB(86,65,32):nav?RGB(16,21,30):RGB(50,43,32));
  wchar_t label[160];GetWindowTextW(item->hwndItem,label,160);auto previous=SelectObject(item->hDC,controlFont);SetTextColor(item->hDC,disabled?RGB(84,95,111):nav&&!selected?muted:gold);SetBkMode(item->hDC,TRANSPARENT);RECT rect=item->rcItem;if(nav)rect.left+=16;
  DrawTextW(item->hDC,label,-1,&rect,DT_VCENTER|DT_SINGLELINE|(nav?DT_LEFT:DT_CENTER));if(item->itemState&ODS_FOCUS){InflateRect(&rect,-3,-3);DrawFocusRect(item->hDC,&rect);}SelectObject(item->hDC,previous);return TRUE;}
 case WM_CTLCOLORLISTBOX:case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:SetTextColor(reinterpret_cast<HDC>(a),ink);SetBkColor(reinterpret_cast<HDC>(a),panel);return reinterpret_cast<LRESULT>(fieldBrush);
 case WM_COMMAND:try{
  if(LOWORD(a)==300&&HIWORD(a)==EN_CHANGE){wchar_t value[256];GetWindowTextW(searchBox,value,256);query=value;offset=0;render();}
  else if(LOWORD(a)==540&&HIWORD(a)==CBN_SELCHANGE)updateStartupSelection();
  else if(HIWORD(a)==BN_CLICKED)command(LOWORD(a));
 }catch(const std::exception& e){tune::log(L"Fejl: "+tune::errorText(e));notify(L"Handlingen kunne ikke gennemføres. Eventuelle backups bevares.");MessageBoxW(w,tune::errorText(e).c_str(),L"GameTune · handling ikke gennemført",MB_OK|MB_ICONWARNING);refreshStates();render();}return 0;
 case WM_TIMER:
  if(pingTask.valid()&&pingTask.wait_for(std::chrono::seconds(0))==std::future_status::ready){try{pingResult=pingTask.get();}catch(...){pingResult=L"Pingtesten kunne ikke gennemføres.";}if(page==L"Netværk")EnableWindow(GetDlgItem(w,511),TRUE);InvalidateRect(w,nullptr,FALSE);}
  if(smoke){
   if(page==L"Netværk"&&pingTask.valid())return 0;
   screenshot(L"gametune-page"+std::to_wstring(smokeStep)+L".bmp");++smokeStep;
   if(smokeStep<static_cast<int>(pages.size())){page=pages[static_cast<size_t>(smokeStep)];render();if(page==L"Netværk"){SetDlgItemTextW(w,510,L"127.0.0.1");command(511);}}
   else if(smokeStep==static_cast<int>(pages.size())){SetWindowTextW(searchBox,L"mus");}
   else PostMessageW(w,WM_CLOSE,0,0);
   return 0;
  }

  hardware=tune::hardwareSummary();if(page==L"Overblik"||page==L"System")InvalidateRect(w,nullptr,FALSE);return 0;
 case WM_CLOSE:try{restorePriorities();}catch(...){if(MessageBoxW(w,L"Nogle procesprioriteter kunne ikke gendannes. De varer indtil spillet afsluttes. Vil du lukke alligevel?",L"Aktive procesændringer",MB_YESNO|MB_ICONWARNING)!=IDYES)return 0;}DestroyWindow(w);return 0;
 case WM_DESTROY:KillTimer(w,1);closeProcesses();for(auto& p:priorities)CloseHandle(p.handle);priorities.clear();PostQuitMessage(0);return 0;
 }
 return DefWindowProcW(w,message,a,b);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR arguments,int show){
 HANDLE mutex=CreateMutexW(nullptr,TRUE,L"Local\\GameTuneSingleInstance");if(!mutex)return 1;
 if(GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(nullptr,L"GameTune er allerede åben.",L"GameTune",MB_OK);CloseHandle(mutex);return 0;}
 smoke=std::wstring(arguments).find(L"--smoke-ui")!=std::wstring::npos;
 SetProcessDPIAware();RECT desktop{};SystemParametersInfoW(SPI_GETWORKAREA,0,&desktop,0);uiScale=std::min({1.0,(desktop.right-desktop.left-32)/1220.0,(desktop.bottom-desktop.top-64)/860.0});if(uiScale<=0)uiScale=1.0;if(smoke&&std::wstring(arguments).find(L"--small")!=std::wstring::npos)uiScale=0.78;controlFont=font(px(16),FW_NORMAL);normalFont=font(16,FW_NORMAL);smallFont=font(13,FW_NORMAL);headingFont=font(20,FW_SEMIBOLD);titleFont=font(34,FW_BOLD);background=CreateSolidBrush(bg);fieldBrush=CreateSolidBrush(panel);
 WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=procedure;cls.lpszClassName=L"GameTuneWindow";cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=background;if(!RegisterClassW(&cls))return 1;
 RECT bounds{0,0,px(1220),px(860)};AdjustWindowRect(&bounds,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE);
 HWND w=CreateWindowW(cls.lpszClassName,L"GameTune 0.2 · Performance Studio",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);if(!w)return 1;
 BOOL dark=TRUE;DwmSetWindowAttribute(w,20,&dark,sizeof dark);DwmSetWindowAttribute(w,34,&gold,sizeof gold);
 WSADATA sockets;WSAStartup(MAKEWORD(2,2),&sockets);ShowWindow(w,show);MSG msg;BOOL result;
 while((result=GetMessageW(&msg,nullptr,0,0))>0){if(!IsDialogMessageW(w,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
 if(pingTask.valid())pingTask.wait();
 WSACleanup();DeleteObject(controlFont);DeleteObject(normalFont);DeleteObject(smallFont);DeleteObject(headingFont);DeleteObject(titleFont);DeleteObject(background);DeleteObject(fieldBrush);ReleaseMutex(mutex);CloseHandle(mutex);return result==-1?1:static_cast<int>(msg.wParam);
}
