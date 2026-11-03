#include <windows.h>
#include <powrprof.h>
#include <shellapi.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <stdexcept>

namespace {
constexpr wchar_t backupRoot[] = L"Software\\GameTune\\Backup";
constexpr COLORREF gold = RGB(239,185,83), muted = RGB(149,161,180), white = RGB(236,240,248);
const GUID highPerformance = {0x8c5e7fda,0xe8bf,0x4a96,{0x9a,0x85,0xa6,0xe2,0x3a,0x8c,0x63,0x5c}};
struct Setting { const wchar_t* path; const wchar_t* name; const wchar_t* id; };
const Setting game{L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", L"GameMode"};
const Setting capture{L"System\\GameConfigStore", L"GameDVR_Enabled", L"Capture"};
const Setting appCapture{L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled", L"Capture2"};
struct Key { HKEY h=nullptr; ~Key(){if(h) RegCloseKey(h);} };
struct Value { DWORD type=REG_NONE; std::vector<BYTE> bytes; bool exists=false; };
HWND window;
HBRUSH background;
HFONT normalFont, titleFont, headingFont;
std::wstring gameStatus, captureStatus, powerStatus;

void check(LSTATUS code) {
    if(code!=ERROR_SUCCESS) throw std::runtime_error("Windows operation failed. Error: "+std::to_string(code));
}
Value read(const wchar_t* path, const wchar_t* name) {
    Key key;
    auto result=RegOpenKeyExW(HKEY_CURRENT_USER,path,0,KEY_QUERY_VALUE,&key.h);
    if(result==ERROR_FILE_NOT_FOUND) return {};
    check(result);
    Value value; DWORD size=0;
    result=RegQueryValueExW(key.h,name,nullptr,&value.type,nullptr,&size);
    if(result==ERROR_FILE_NOT_FOUND) return {};
    check(result);
    value.bytes.resize(size);
    check(RegQueryValueExW(key.h,name,nullptr,&value.type,value.bytes.data(),&size));
    value.bytes.resize(size); value.exists=true;
    return value;
}
void write(const wchar_t* path, const wchar_t* name, const Value& value) {
    Key key;
    check(RegCreateKeyExW(HKEY_CURRENT_USER,path,0,nullptr,0,KEY_SET_VALUE,nullptr,&key.h,nullptr));
    if(value.exists) {
        check(RegSetValueExW(key.h,name,0,value.type,value.bytes.data(),static_cast<DWORD>(value.bytes.size())));
    } else {
        auto result=RegDeleteValueW(key.h,name);
        if(result!=ERROR_FILE_NOT_FOUND) check(result);
    }
}
Value number(DWORD n) {
    Value value; value.type=REG_DWORD; value.exists=true; value.bytes.resize(sizeof n);
    CopyMemory(value.bytes.data(),&n,sizeof n); return value;
}
std::wstring slot(const Setting& setting) { return std::wstring(backupRoot)+L"\\"+setting.id; }
void save(const Setting& setting) {
    auto path=slot(setting);
    if(read(path.c_str(),L"Saved").exists) return;
    auto original=read(setting.path,setting.name);
    write(path.c_str(),L"Value",original);
    write(path.c_str(),L"Exists",number(original.exists));
    write(path.c_str(),L"Saved",number(1));
}
void change(const Setting& setting, DWORD n) { save(setting); write(setting.path,setting.name,number(n)); }
void restore(const Setting& setting) {
    auto path=slot(setting);
    if(!read(path.c_str(),L"Saved").exists) return;
    auto flag=read(path.c_str(),L"Exists");
    if(flag.type!=REG_DWORD || flag.bytes.size()!=4) throw std::runtime_error("Invalid backup; no restoration performed.");
    DWORD exists; CopyMemory(&exists,flag.bytes.data(),4);
    auto original=read(path.c_str(),L"Value");
    if(exists && !original.exists) throw std::runtime_error("Incomplete backup; no restoration performed.");
    original.exists=exists!=0;
    write(setting.path,setting.name,original);
    check(RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str()));
}
std::wstring state(const Setting& setting) {
    auto value=read(setting.path,setting.name);
    if(!value.exists) return L"Windows-standard";
    if(value.type!=REG_DWORD || value.bytes.size()!=4) return L"Ukendt værdi";
    DWORD n; CopyMemory(&n,value.bytes.data(),4);
    return n ? L"Til" : L"Fra";
}
void setPower() {
    GUID* current=nullptr; check(PowerGetActiveScheme(nullptr,&current));
    wchar_t text[40]; StringFromGUID2(*current,text,40); LocalFree(current);
    if(!read(backupRoot,L"Power").exists) {
        Value value; value.exists=true; value.type=REG_SZ;
        const auto* bytes=reinterpret_cast<const BYTE*>(text);
        value.bytes.assign(bytes,bytes+(wcslen(text)+1)*sizeof(wchar_t));
        write(backupRoot,L"Power",value);
    }
    check(PowerSetActiveScheme(nullptr,&highPerformance));
}
void restorePower() {
    auto original=read(backupRoot,L"Power");
    if(!original.exists) return;
    if(original.type!=REG_SZ || original.bytes.size()!=78) throw std::runtime_error("Invalid power backup.");
    wchar_t text[40]={}; CopyMemory(text,original.bytes.data(),original.bytes.size());
    GUID id;
    if(FAILED(CLSIDFromString(text,&id))) throw std::runtime_error("Invalid power scheme ID.");
    check(PowerSetActiveScheme(nullptr,&id));
    write(backupRoot,L"Power",{});
}
void refresh() {
    gameStatus=L"Status: "+state(game);
    captureStatus=L"DVR: "+state(capture)+L" / App: "+state(appCapture);
    GUID* active=nullptr;
    if(PowerGetActiveScheme(nullptr,&active)==ERROR_SUCCESS) {
        powerStatus=IsEqualGUID(*active,highPerformance) ? L"Status: Høj ydeevne" : L"Status: Anden strømprofil";
        LocalFree(active);
    } else powerStatus=L"Status: Utilgængelig";
    InvalidateRect(window,nullptr,TRUE);
}
void fill(HDC dc, RECT rect, COLORREF color) {
    auto brush=CreateSolidBrush(color); FillRect(dc,&rect,brush); DeleteObject(brush);
}
void text(HDC dc, const std::wstring& value, int x, int y, int width, int height, COLORREF color, HFONT font) {
    auto previous=SelectObject(dc,font); SetTextColor(dc,color); SetBkMode(dc,TRANSPARENT);
    RECT rect{x,y,x+width,y+height};
    DrawTextW(dc,value.c_str(),-1,&rect,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
    SelectObject(dc,previous);
}
void card(HDC dc,int x,int y,const wchar_t* tag,const wchar_t* title,const wchar_t* description,const std::wstring& status) {
    fill(dc,{x,y,x+308,y+206},RGB(25,32,44));
    fill(dc,{x,y,x+308,y+2},RGB(58,66,79));
    text(dc,tag,x+18,y+16,275,18,gold,normalFont);
    text(dc,title,x+18,y+44,275,30,white,headingFont);
    text(dc,description,x+18,y+80,275,57,muted,normalFont);
    text(dc,status,x+18,y+143,275,24,RGB(154,212,173),normalFont);
}
void paint(HWND w) {
    PAINTSTRUCT ps; HDC dc=BeginPaint(w,&ps); RECT client; GetClientRect(w,&client);
    fill(dc,client,RGB(13,18,27)); fill(dc,{0,0,client.right,3},gold);
    fill(dc,{0,3,68,client.bottom},RGB(18,24,34));
    text(dc,L"G",21,23,40,40,gold,titleFont);
    fill(dc,{14,106,54,146},RGB(54,47,34)); text(dc,L"01",23,116,40,24,gold,normalFont);
    text(dc,L"WIN\n11",22,client.bottom-68,40,48,muted,normalFont);
    text(dc,L"GAMETUNE",98,26,330,38,white,titleFont);
    text(dc,L"MERE KONTROL. KLAR TIL DIT NÆSTE SPIL.",99,70,680,24,gold,normalFont);
    text(dc,L"Gaming-overblik",98,126,660,34,white,titleFont);
    text(dc,L"Tilpas Windows. Bevar kontrollen. Gendan når du vil.",99,168,850,26,muted,normalFont);
    text(dc,L"ANBEFALET / 3 INDSTILLINGER / LOKAL BACKUP",99,207,900,22,gold,normalFont);
    card(dc,98,248,L"GAMING",L"Windows Game Mode",L"Lad Windows prioritere spillet. Effekten afhænger af dit spil og din hardware.",gameStatus);
    card(dc,422,248,L"BAGGRUNDSAKTIVITET",L"Baggrundsoptagelse",L"Slå Game DVR fra, hvis du ikke bruger klip. Påvirker Xbox Game Bar-optagelser.",captureStatus);
    card(dc,746,248,L"STRØM & YDEEVNE",L"Strømprofil",L"Høj ydeevne kan øge varme og strømforbrug. Understøttes ikke på alle pc'er.",powerStatus);
    card(dc,98,474,L"GPU & SKÆRM",L"Grafikindstillinger",L"Vælg GPU pr. spil. Find HAGS og VRR i Windows, hvis din hardware understøtter det.",L"Hardwareafhængige muligheder");
    card(dc,422,474,L"DU HAR KONTROLLEN",L"Gendan ændringer",L"Genskab værdierne fra før appens første ændringer. Backups bevares efter lukning.",L"Ingen ændringer ved appstart");
    card(dc,746,474,L"MÅL RESULTATET",L"FPS & latency",L"Sammenlign samme scene og indstillinger. Se FPS, 1% lows og frametimes.",L"Ingen garanteret FPS-forbedring");
    text(dc,L"WINDOWS 11 / Ingen database / Genstart spil efter ændringer",99,702,950,24,muted,normalFont);
    EndPaint(w,&ps);
}
void button(HWND w,const wchar_t* label,int id,int x,int y,int width=128) {
    CreateWindowW(L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,x,y,width,30,w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
}
LRESULT CALLBACK procedure(HWND w,UINT message,WPARAM a,LPARAM b) {
    if(message==WM_CREATE) {
        window=w;
        button(w,L"Gamingprofil",9,864,42,190);
        button(w,L"Slå til",1,116,416); button(w,L"Slå fra",2,260,416);
        button(w,L"Slå fra",3,440,416); button(w,L"Slå til",4,584,416);
        button(w,L"Høj ydeevne",5,764,416); button(w,L"Gendan",6,908,416);
        button(w,L"Åbn indstillinger",7,116,642,272);
        button(w,L"Gendan alle ændringer",8,440,642,272);
        button(w,L"Sådan måler du",10,764,642,272);
        try {refresh();} catch(...) {gameStatus=captureStatus=powerStatus=L"Kunne ikke læse indstillinger";}
        return 0;
    }
    if(message==WM_PAINT) {paint(w);return 0;}
    if(message==WM_DRAWITEM) {
        auto item=reinterpret_cast<DRAWITEMSTRUCT*>(b);
        if(item->CtlType!=ODT_BUTTON) return FALSE;
        fill(item->hDC,item->rcItem,(item->itemState&ODS_SELECTED)?RGB(102,77,35):RGB(63,51,32));
        wchar_t label[100]; GetWindowTextW(item->hwndItem,label,100);
        auto previous=SelectObject(item->hDC,normalFont); SetBkMode(item->hDC,TRANSPARENT); SetTextColor(item->hDC,gold);
        RECT rect=item->rcItem; DrawTextW(item->hDC,label,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(item->itemState&ODS_FOCUS) {InflateRect(&rect,-3,-3);DrawFocusRect(item->hDC,&rect);}
        SelectObject(item->hDC,previous); return TRUE;
    }
    if(message==WM_COMMAND) {
        try {
            switch(LOWORD(a)) {
                case 1: change(game,1);break;
                case 2: change(game,0);break;
                case 3: save(capture);save(appCapture);change(capture,0);change(appCapture,0);break;
                case 4: save(capture);save(appCapture);change(capture,1);change(appCapture,1);break;
                case 5: setPower();break;
                case 6: restorePower();break;
                case 7:
                    if(reinterpret_cast<INT_PTR>(ShellExecuteW(w,L"open",L"ms-settings:display-advancedgraphics",nullptr,nullptr,SW_SHOWNORMAL))<=32)
                        throw std::runtime_error("Could not open Windows Settings.");
                    return 0;
                case 8:
                    if(MessageBoxW(w,L"Gendan værdierne fra før appens første ændringer?",L"Gendan",MB_YESNO|MB_ICONQUESTION)!=IDYES) return 0;
                    restore(game);restore(capture);restore(appCapture);restorePower();break;
                case 9:
                    if(MessageBoxW(w,L"Gamingprofil slår Game Mode til og Game DVR-optagelse fra. Strømprofil ændres ikke. Fortsæt?",L"Gamingprofil",MB_YESNO|MB_ICONQUESTION)!=IDYES) return 0;
                    save(game);save(capture);save(appCapture);change(game,1);change(capture,0);change(appCapture,0);break;
                case 10:
                    MessageBoxW(w,L"1. Mål samme spil, scene og grafikindstillinger.\n2. Notér FPS, 1% lows og frametimes med spillets benchmark eller et måleværktøj.\n3. Skift én indstilling, genstart spillet og gentag.\n\nFrametime er ikke det samme som input-latency. Appen måler ikke FPS eller latency selv.",L"Mål før og efter",MB_OK|MB_ICONINFORMATION);
                    return 0;
                default: return 0;
            }
            refresh();
            MessageBoxW(w,L"Handlingen er gennemført. Genstart spillet efter registerændringer.",L"GameTune",MB_OK|MB_ICONINFORMATION);
        } catch(const std::exception& error) {
            MessageBoxA(w,error.what(),"GameTune - action incomplete; backups retained",MB_OK|MB_ICONERROR);
            try {refresh();}catch(...){}
        }
        return 0;
    }
    if(message==WM_DESTROY) {PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,message,a,b);
}
HFONT makeFont(int size,int weight) {
    return CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
}
}
#ifndef GAMETUNE_TEST
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    HANDLE mutex=CreateMutexW(nullptr,TRUE,L"Local\\GameTuneSingleInstance");
    if(!mutex) return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS) {MessageBoxW(nullptr,L"GameTune er allerede åben.",L"GameTune",MB_OK);CloseHandle(mutex);return 0;}
    SetProcessDPIAware();background=CreateSolidBrush(RGB(13,18,27));
    normalFont=makeFont(16,FW_NORMAL);titleFont=makeFont(27,FW_BOLD);headingFont=makeFont(20,FW_SEMIBOLD);
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=procedure;cls.lpszClassName=L"GameTuneWindow";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=background;
    if(!RegisterClassW(&cls)) return 1;
    HWND w=CreateWindowW(cls.lpszClassName,L"GameTune",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,1100,790,nullptr,nullptr,instance,nullptr);
    if(!w) return 1;
    ShowWindow(w,show); MSG msg; BOOL result;
    while((result=GetMessageW(&msg,nullptr,0,0))>0) {
        if(!IsDialogMessageW(w,&msg)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    DeleteObject(normalFont);DeleteObject(titleFont);DeleteObject(headingFont);DeleteObject(background);
    ReleaseMutex(mutex);CloseHandle(mutex);
    return result==-1?1:static_cast<int>(msg.wParam);
}
#else
int main() {
    auto unique=std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());
    auto path=std::wstring(L"Software\\GameTune\\Tests\\")+unique;
    auto id=L"Test_"+unique;
    Setting setting{path.c_str(),L"Synthetic",id.c_str()};
    auto require=[](bool result){if(!result) throw std::runtime_error("Backup regression");};
    try {
        require(!read(setting.path,setting.name).exists);
        change(setting,1);change(setting,0);restore(setting);
        require(!read(setting.path,setting.name).exists);
        require(!read(slot(setting).c_str(),L"Saved").exists);
        Value original;original.exists=true;original.type=REG_BINARY;original.bytes={0,13,255,0,42};
        write(setting.path,setting.name,original);
        change(setting,1);change(setting,0);restore(setting);restore(setting);
        auto actual=read(setting.path,setting.name);
        require(actual.exists&&actual.type==original.type&&actual.bytes==original.bytes);
        check(RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str()));return 0;
    } catch(...) {
        RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());RegDeleteTreeW(HKEY_CURRENT_USER,slot(setting).c_str());return 1;
    }
}
#endif
