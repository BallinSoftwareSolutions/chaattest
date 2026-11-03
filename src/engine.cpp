#include "engine.h"
#include <powrprof.h>
#include <objbase.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <iomanip>
namespace tune {
namespace {
const std::wstring root=L"Software\\GameTune\\Backup";
const GUID highPower={0x8c5e7fda,0xe8bf,0x4a96,{0x9a,0x85,0xa6,0xe2,0x3a,0x8c,0x63,0x5c}};
struct Key { HKEY h=nullptr; ~Key(){if(h)RegCloseKey(h);} };
void check(LSTATUS result){if(result!=ERROR_SUCCESS)throw std::runtime_error("Windows error "+std::to_string(result));}
void spiCheck(BOOL result){if(!result)throw std::runtime_error("Windows rejected setting ("+std::to_string(GetLastError())+")");}
std::wstring slot(const std::wstring& id){return root+L"\\"+id;}
std::wstring asString(const Value& v){
 if(!v.exists)return L"";
 if(v.type!=REG_SZ||v.bytes.size()%sizeof(wchar_t)!=0)throw std::runtime_error("Unexpected registry data type");
 std::wstring result(v.bytes.size()/sizeof(wchar_t),L'\0');
 if(!v.bytes.empty())std::memcpy(result.data(),v.bytes.data(),v.bytes.size());
 while(!result.empty()&&result.back()==L'\0')result.pop_back();
 return result;
}
DWORD asNumber(const Value& v){
 if(v.type!=REG_DWORD||v.bytes.size()!=4)throw std::runtime_error("Invalid stored value");
 DWORD result;std::memcpy(&result,v.bytes.data(),4);return result;
}
std::wstring gameId(const Feature& f,const std::wstring& game){
 if(game.empty())throw std::runtime_error("Select a game executable first");
 std::wstring lower=game;CharLowerBuffW(lower.data(),static_cast<DWORD>(lower.size()));
 unsigned long long hash=14695981039346656037ull;
 for(wchar_t c:lower){hash^=static_cast<unsigned>(c);hash*=1099511628211ull;}
 return f.id+L"_"+std::to_wstring(hash);
}
std::pair<std::wstring,std::wstring> gameTarget(const Feature& f,const std::wstring& game){
 if(game.empty())throw std::runtime_error("Select a game executable first");
 return {f.kind==Kind::Gpu?L"Software\\Microsoft\\DirectX\\UserGpuPreferences":L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",game};
}
Value spiValue(UINT action){
 Value v;v.exists=true;v.type=REG_BINARY;size_t size=sizeof(BOOL);UINT parameter=0;
 if(action==SPI_GETMOUSE)size=sizeof(int)*3;
 if(action==SPI_GETSTICKYKEYS){size=sizeof(STICKYKEYS);parameter=static_cast<UINT>(size);}
 if(action==SPI_GETTOGGLEKEYS){size=sizeof(TOGGLEKEYS);parameter=static_cast<UINT>(size);}
 if(action==SPI_GETFILTERKEYS){size=sizeof(FILTERKEYS);parameter=static_cast<UINT>(size);}
 if(action==SPI_GETANIMATION){size=sizeof(ANIMATIONINFO);parameter=static_cast<UINT>(size);}
 v.bytes.assign(size,0);
 if(parameter)std::memcpy(v.bytes.data(),&parameter,sizeof parameter);
 spiCheck(SystemParametersInfoW(action,parameter,v.bytes.data(),0));
 if(action==SPI_GETSTICKYKEYS||action==SPI_GETTOGGLEKEYS||action==SPI_GETFILTERKEYS){DWORD flags=0;std::memcpy(&flags,v.bytes.data()+sizeof(UINT),sizeof flags);if(!(flags&2))throw std::runtime_error("Windows reports this accessibility feature unavailable");}
 return v;
}
Value desired(UINT action,Value current){
 if(action==SPI_GETMOUSE){int n[3]={0,0,0};std::memcpy(current.bytes.data(),n,sizeof n);}
 else if(action==SPI_GETMOUSESPEED){DWORD n=10;std::memcpy(current.bytes.data(),&n,4);}
 else if(action==SPI_GETSTICKYKEYS){STICKYKEYS s;std::memcpy(&s,current.bytes.data(),sizeof s);s.dwFlags&=~(SKF_HOTKEYACTIVE|SKF_CONFIRMHOTKEY);std::memcpy(current.bytes.data(),&s,sizeof s);}
 else if(action==SPI_GETTOGGLEKEYS){TOGGLEKEYS s;std::memcpy(&s,current.bytes.data(),sizeof s);s.dwFlags&=~(TKF_HOTKEYACTIVE|TKF_CONFIRMHOTKEY);std::memcpy(current.bytes.data(),&s,sizeof s);}
 else if(action==SPI_GETFILTERKEYS){FILTERKEYS s;std::memcpy(&s,current.bytes.data(),sizeof s);s.dwFlags&=~FKF_FILTERKEYSON;std::memcpy(current.bytes.data(),&s,sizeof s);}
 else if(action==SPI_GETANIMATION){ANIMATIONINFO s{sizeof(ANIMATIONINFO),0};std::memcpy(current.bytes.data(),&s,sizeof s);}
 else std::fill(current.bytes.begin(),current.bytes.end(),0);
 return current;
}
void setSpi(UINT action,const Value& value){
 auto expected=spiValue(action);
 if(value.type!=REG_BINARY||value.bytes.size()!=expected.bytes.size())throw std::runtime_error("Invalid system-setting backup");
 auto data=value.bytes;UINT set=0,parameter=0;void* pointer=data.data();DWORD n=0;
 switch(action){
 case SPI_GETMOUSE:set=SPI_SETMOUSE;break;
 case SPI_GETMOUSESPEED:set=SPI_SETMOUSESPEED;std::memcpy(&n,data.data(),4);pointer=reinterpret_cast<void*>(static_cast<UINT_PTR>(n));
 // Windows ignores uiParam here; Wine 10 incorrectly reads it instead of pvParam.
 if(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"wine_get_version"))parameter=n;
 break;
 case SPI_GETMOUSETRAILS:set=SPI_SETMOUSETRAILS;std::memcpy(&parameter,data.data(),4);pointer=nullptr;break;
 case SPI_GETSTICKYKEYS:set=SPI_SETSTICKYKEYS;parameter=sizeof(STICKYKEYS);break;
 case SPI_GETTOGGLEKEYS:set=SPI_SETTOGGLEKEYS;parameter=sizeof(TOGGLEKEYS);break;
 case SPI_GETFILTERKEYS:set=SPI_SETFILTERKEYS;parameter=sizeof(FILTERKEYS);break;
 case SPI_GETANIMATION:set=SPI_SETANIMATION;parameter=sizeof(ANIMATIONINFO);break;
 case SPI_GETMENUANIMATION:set=SPI_SETMENUANIMATION;break;
 case SPI_GETTOOLTIPANIMATION:set=SPI_SETTOOLTIPANIMATION;break;
 case SPI_GETCLIENTAREAANIMATION:set=SPI_SETCLIENTAREAANIMATION;break;
 case SPI_GETCURSORSHADOW:set=SPI_SETCURSORSHADOW;break;
 default:throw std::runtime_error("Unsupported system setting");
 }
 if(action==SPI_GETMENUANIMATION||action==SPI_GETTOOLTIPANIMATION||action==SPI_GETCLIENTAREAANIMATION||action==SPI_GETCURSORSHADOW){std::memcpy(&n,data.data(),4);pointer=reinterpret_cast<void*>(static_cast<UINT_PTR>(n));}
 spiCheck(SystemParametersInfoW(set,parameter,pointer,SPIF_UPDATEINIFILE|SPIF_SENDCHANGE));
 if(spiValue(action).bytes!=value.bytes)throw std::runtime_error("Windows did not confirm the requested setting. Original backup retained.");
}
void saveSpi(const Feature& f,const Value& original){
 auto path=slot(f.id);if(read(path,L"Saved").exists)return;
 write(path,L"Value",original);write(path,L"Spi",number(f.spi));write(path,L"Saved",number(1));
}
bool same(const Value& a,const Value& b){return a.exists==b.exists&&(!a.exists||(a.type==b.type&&a.bytes==b.bytes));}
void restorePower(){
 auto v=read(root,L"Power");if(!v.exists)return;auto text=asString(v);GUID id;
 if(FAILED(CLSIDFromString(text.c_str(),&id)))throw std::runtime_error("Invalid power backup");
 check(PowerSetActiveScheme(nullptr,&id));write(root,L"Power",{});
}
}
const std::vector<Feature>& features(){
 static const std::vector<Feature> list={
 {L"GameMode",L"Windows Game Mode",L"Gaming",L"Lad Windows prioritere spillet, når det kører.",L"Aktiverer Windows Game Mode for din bruger. Effekten afhænger af spil, baggrundsbelastning og Windows-version. Genstart spillet efter ændringen.",L"Genstart spil",Kind::Registry,0,{{L"Software\\Microsoft\\GameBar",L"AutoGameModeEnabled",1}}},
 {L"Dvr",L"Baggrundsoptagelse",L"Gaming",L"Slå Game DVR-optagelser fra, hvis du ikke bruger klip.",L"Deaktiverer brugerens Game DVR- og App Capture-indstillinger. Påvirker Xbox Game Bar-klip; genstart spil eller log ind igen. Det lukker ikke alle tredjeparts-overlays.",L"Genstart spil",Kind::Registry,0,{{L"System\\GameConfigStore",L"GameDVR_Enabled",0},{L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",L"AppCaptureEnabled",0}}},
 {L"PowerPlan",L"Høj ydeevne",L"Gaming",L"Vælg Windows' høj ydeevne-strømprofil.",L"Kan øge strømforbrug, varme og blæserstøj. Bedre FPS er ikke garanteret. Profilen findes ofte ikke på Modern Standby-laptops. Appen opretter eller ændrer ikke andre profiler.",L"Mere strøm / varme",Kind::Power,0,{}},
 {L"MouseAcceleration",L"Museacceleration",L"Input",L"Slå Enhance pointer precision fra for ensartet markørbevægelse.",L"Slår Windows' museacceleration og tærskler fra. Påvirker markøren og spil, der bruger Windows-input. Spil med raw input kan ignorere dette. Gendannelse genskaber alle tre originale museværdier.",L"Straks · Windows-input",Kind::Spi,SPI_GETMOUSE,{}},
 {L"MouseTrails",L"Markørspor",L"Input",L"Fjern visuelle spor efter musemarkøren.",L"Slår Windows' markørspor fra. Dette er en visuel præference; appen lover ikke lavere input-latency. Originalt antal spor gemmes.",L"Visuel præference",Kind::Spi,SPI_GETMOUSETRAILS,{}},
 {L"MouseSpeed",L"Neutral musehastighed",L"Input",L"Sæt Windows' markørhastighed til 6/11 (værdi 10).",L"Indstiller Windows' pointer speed til midterpositionen. Ændrer den følte markørhastighed, ikke musens DPI eller polling rate. Raw-input-spil kan ignorere den. Din originale hastighed kan gendannes.",L"Ændrer følsomhed",Kind::Spi,SPI_GETMOUSESPEED,{}},
 {L"StickyHotkey",L"Sticky Keys-genvej",L"Input",L"Undgå aktiveringsdialogen ved gentagne tryk på Shift.",L"Deaktiverer Sticky Keys-genvejen og dens bekræftelse. Den slår ikke selve Sticky Keys-funktionen fra, hvis den allerede er aktiv. Kan påvirke din foretrukne tilgængelighedsgenvej.",L"Tilgængelighedsgenvej",Kind::Spi,SPI_GETSTICKYKEYS,{}},
 {L"ToggleHotkey",L"Toggle Keys-genvej",L"Input",L"Undgå aktivering ved at holde Num Lock nede.",L"Deaktiverer Toggle Keys-genvejen og bekræftelsesdialogen. Bevarer funktionens øvrige flag. Ændrer ikke FPS; relevant hvis genvejen afbryder dit spil.",L"Tilgængelighedsgenvej",Kind::Spi,SPI_GETTOGGLEKEYS,{}},
 {L"FilterKeys",L"Filter Keys",L"Input",L"Slå filtrering af korte og gentagne tastetryk fra.",L"Slår Filter Keys fra, men bevarer tids- og genvejsindstillinger. Brug ikke dette, hvis du har behov for tastaturfiltrering som tilgængelighedsfunktion. Tilføjes ikke automatisk af nogen profil.",L"Tilgængelighed · manuelt",Kind::Spi,SPI_GETFILTERKEYS,{}},
 {L"Transparency",L"Gennemsigtighed",L"Windows",L"Brug uigennemsigtige Windows-overflader.",L"Slår brugerens Windows-transparens fra. Påvirker skrivebordets udseende; ingen dokumenteret universel FPS-gevinst. Kan kræve ny login-session før alle overflader opdateres.",L"Skrivebord · evt. log ind igen",Kind::Registry,0,{{L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"EnableTransparency",0}}},
 {L"WindowAnimation",L"Vinduesanimationer",L"Windows",L"Fjern animation ved minimering og maksimering.",L"Slår Windows' minimize/maximize-animation fra gennem den understøttede system-API. En skrivebordspræference, ikke en FPS-optimering inde i spil.",L"Skrivebordseffekt",Kind::Spi,SPI_GETANIMATION,{}},
 {L"MenuAnimation",L"Menuanimationer",L"Windows",L"Vis Windows-menuer uden animation.",L"Deaktiverer systemets menuanimation. Apps med egne menuer kan ignorere dette. Handler om skrivebordets følelse; ingen garanteret FPS- eller latencygevinst i spil.",L"Skrivebordseffekt",Kind::Spi,SPI_GETMENUANIMATION,{}},
 {L"TooltipAnimation",L"Tooltip-animationer",L"Windows",L"Vis system-tooltips uden overgangsanimation.",L"Slår animation af systemets tooltips fra. Programmer kan have egne tooltips. Den oprindelige værdi gemmes og kan gendannes.",L"Skrivebordseffekt",Kind::Spi,SPI_GETTOOLTIPANIMATION,{}},
 {L"ClientAnimation",L"Indholdsanimationer",L"Windows",L"Reducer animation i Windows' klientområder.",L"Slår klientområde-animationer fra i programmer, der respekterer Windows-indstillingen. Reducerer bevægelse i brugerfladen; påstår ikke højere FPS i spil.",L"Skrivebordseffekt",Kind::Spi,SPI_GETCLIENTAREAANIMATION,{}},
 {L"CursorShadow",L"Markørskygge",L"Windows",L"Fjern skyggen under Windows' musemarkør.",L"Slår markørskygge fra. En visuel præference, som kan ignoreres af spil med egen markør. Ingen påstået FPS-forbedring.",L"Visuel præference",Kind::Spi,SPI_GETCURSORSHADOW,{}},
 {L"EdgeBackground",L"Edge i baggrunden",L"Baggrund",L"Stop Edge-baggrundsapps, når browseren er lukket.",L"Anvender Microsoft Edges BackgroundModeEnabled-politik for din bruger. Genstart browseren. Kan stoppe baggrundsudvidelser og notifikationer; ændrer ikke åbne faner. Eksisterende organisationspolitik kan have forrang.",L"Genstart Edge",Kind::Registry,0,{{L"Software\\Policies\\Microsoft\\Edge",L"BackgroundModeEnabled",0}}},
 {L"EdgeStartupBoost",L"Edge Startup Boost",L"Baggrund",L"Undgå at Edge forudstarter browserprocesser.",L"Slår Edge Startup Boost fra via brugerpolitik. Kan reducere forhåndsstartet baggrundsaktivitet, men gøre browserens første opstart langsommere. Kræver browsergenstart. Ingen garanteret FPS-gevinst.",L"Langsommere browserstart",Kind::Registry,0,{{L"Software\\Policies\\Microsoft\\Edge",L"StartupBoostEnabled",0}}},
 {L"ChromeBackground",L"Chrome i baggrunden",L"Baggrund",L"Stop Chrome-baggrundsapps, når browseren er lukket.",L"Anvender Chromes BackgroundModeEnabled-politik for din bruger. Genstart Chrome. Baggrundsudvidelser og notifikationer kan stoppe. Organisationsstyring kan overstyre politikken. Åbne faner afsluttes ikke.",L"Genstart Chrome",Kind::Registry,0,{{L"Software\\Policies\\Google\\Chrome",L"BackgroundModeEnabled",0}}},
 {L"SearchSuggestions",L"Søgeforslag fra nettet",L"Windows",L"Fravælg webforslag i understøttet Windows-søgning.",L"Sætter DisableSearchBoxSuggestions-politikken for din bruger. Virkningen varierer med Windows-version, udgave og systempolitik. Log ind igen. Påvirker søgeoplevelsen; ikke en FPS-optimering.",L"Versionsafhængig · log ind igen",Kind::Registry,0,{{L"Software\\Policies\\Microsoft\\Windows\\Explorer",L"DisableSearchBoxSuggestions",1}}},
 {L"AdvertisingId",L"Windows reklame-id",L"Privatliv",L"Slå brug af Windows-reklame-id fra for din bruger.",L"Deaktiverer brugerens reklame-id-indstilling. Det slår ikke alle reklamer eller Windows-telemetri fra. Kan påvirke personlig tilpasning af annoncer; ikke en FPS-optimering.",L"Privatliv · brugerindstilling",Kind::Registry,0,{{L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo",L"Enabled",0}}},
 {L"TailoredExperiences",L"Personlige anbefalinger",L"Privatliv",L"Fravælg tilpassede oplevelser baseret på diagnosedata.",L"Slår brugerens tailored experiences-indstilling fra. Reducerer denne form for personlig tilpasning, men deaktiverer ikke al diagnostik eller telemetri. Ingen påstået FPS-forbedring.",L"Privatliv · brugerindstilling",Kind::Registry,0,{{L"Software\\Microsoft\\Windows\\CurrentVersion\\Privacy",L"TailoredExperiencesWithDiagnosticDataEnabled",0}}},
 {L"GameGpu",L"GPU-præference",L"Spilprofil",L"Bed Windows om højtydende GPU til det valgte spil.",L"Gemmer GpuPreference=2 for den valgte .exe og bevarer andre GPU-præferencer. Nyttigst på systemer med flere GPU'er. Driver, spil og systempolitik kan overstyre ønsket. Genstart spillet. Kræver at du vælger spillets rigtige .exe.",L"Pr. .exe · genstart spil",Kind::Gpu,0,{}},
 {L"GameFullscreen",L"Fullscreen A/B-test",L"Spilprofil",L"Test spillet med fullscreen-optimeringer slået fra.",L"Tilføjer DISABLEDXMAXIMIZEDWINDOWEDMODE kun til den valgte .exe, mens eksisterende kompatibilitetsflag bevares. Kan forringe ydelse, VRR eller Alt-Tab. Anvend kun som målt A/B-test; medtages ikke i en profil. Genstart spillet.",L"Eksperiment · kan forringe",Kind::Fullscreen,0,{}}
 };return list;
}
Value read(const std::wstring& path,const std::wstring& name){
 Key key;auto result=RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_QUERY_VALUE,&key.h);
 if(result==ERROR_FILE_NOT_FOUND)return {};
 check(result);
 Value value;DWORD size=0;result=RegQueryValueExW(key.h,name.c_str(),nullptr,&value.type,nullptr,&size);
 if(result==ERROR_FILE_NOT_FOUND)return {};
 check(result);value.bytes.resize(size);
 check(RegQueryValueExW(key.h,name.c_str(),nullptr,&value.type,value.bytes.data(),&size));value.bytes.resize(size);value.exists=true;return value;
}
void write(const std::wstring& path,const std::wstring& name,const Value& value){
 Key key;check(RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key.h,nullptr));
 if(value.exists)check(RegSetValueExW(key.h,name.c_str(),0,value.type,value.bytes.data(),static_cast<DWORD>(value.bytes.size())));
 else {auto result=RegDeleteValueW(key.h,name.c_str());if(result!=ERROR_FILE_NOT_FOUND)check(result);}
}
Value number(DWORD n){Value v;v.type=REG_DWORD;v.exists=true;v.bytes.resize(4);std::memcpy(v.bytes.data(),&n,4);return v;}
Value stringValue(const std::wstring& text){Value v;v.type=REG_SZ;v.exists=true;auto bytes=reinterpret_cast<const BYTE*>(text.c_str());v.bytes.assign(bytes,bytes+(text.size()+1)*sizeof(wchar_t));return v;}
void saveRegistry(const std::wstring& id,const std::wstring& path,const std::wstring& name){
 auto key=slot(id);
 if(read(key,L"Saved").exists){
  auto previous=asString(read(key,L"Path"));
  if(!previous.empty()&&(previous!=path||asString(read(key,L"Name"))!=name))throw std::runtime_error("Backup collision; operation cancelled");
  return;
 }
 auto old=read(path,name);write(key,L"Value",old);write(key,L"Exists",number(old.exists));
 write(key,L"Path",stringValue(path));write(key,L"Name",stringValue(name));write(key,L"Saved",number(1));
}
void restoreRegistry(const std::wstring& id){
 auto key=slot(id);if(!read(key,L"Saved").exists)return;
 auto path=asString(read(key,L"Path")),name=asString(read(key,L"Name"));
 // Support backups made by the original public build.
 if(path.empty()){
  if(id==L"GameMode"){path=L"Software\\Microsoft\\GameBar";name=L"AutoGameModeEnabled";}
  else if(id==L"Capture"){path=L"System\\GameConfigStore";name=L"GameDVR_Enabled";}
  else if(id==L"Capture2"){path=L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR";name=L"AppCaptureEnabled";}
  else throw std::runtime_error("Incomplete registry backup");
 }
 auto exists=asNumber(read(key,L"Exists"));auto original=read(key,L"Value");
 if(exists&&!original.exists)throw std::runtime_error("Incomplete original value");
 original.exists=exists!=0;write(path,name,original);check(RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str()));
}
std::wstring gpuPreference(const std::wstring& original){
 std::wstring output;std::wstringstream stream(original);std::wstring token;
 while(std::getline(stream,token,L';')){if(!token.empty()&&token.rfind(L"GpuPreference=",0)!=0)output+=token+L";";}
 return output+L"GpuPreference=2;";
}
std::wstring fullscreenFlags(const std::wstring& original){
 std::wstringstream stream(original);std::wstring token;
 while(stream>>token)if(token==L"DISABLEDXMAXIMIZEDWINDOWEDMODE")return original;
 return (original.empty()?L"~":original)+L" DISABLEDXMAXIMIZEDWINDOWEDMODE";
}
void apply(const Feature& f,const std::wstring& game){
 if(f.kind==Kind::Gpu||f.kind==Kind::Fullscreen){auto attributes=GetFileAttributesW(game.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY)||game.size()<4||_wcsicmp(game.c_str()+game.size()-4,L".exe")!=0)throw std::runtime_error("Select an existing game .exe before applying settings");}

 if(f.kind==Kind::Registry){
  for(size_t i=0;i<f.targets.size();++i){auto& t=f.targets[i];auto id=f.id==L"Dvr"?(i?L"Capture2":L"Capture"):f.id;saveRegistry(id,t.path,t.name);}
  for(auto& t:f.targets)write(t.path,t.name,number(t.value));
 }else if(f.kind==Kind::Spi){auto old=spiValue(f.spi);saveSpi(f,old);setSpi(f.spi,desired(f.spi,old));}
 else if(f.kind==Kind::Power){
  GUID* active=nullptr;check(PowerGetActiveScheme(nullptr,&active));wchar_t text[40];StringFromGUID2(*active,text,40);LocalFree(active);
  if(!read(root,L"Power").exists)write(root,L"Power",stringValue(text));
  check(PowerSetActiveScheme(nullptr,&highPower));
 }else{
  auto target=gameTarget(f,game);auto original=asString(read(target.first,target.second));
  saveRegistry(gameId(f,game),target.first,target.second);
  write(target.first,target.second,stringValue(f.kind==Kind::Gpu?gpuPreference(original):fullscreenFlags(original)));
 }
 if(!active(f,game))throw std::runtime_error("The requested setting was not confirmed by Windows. Backup retained.");
 log(L"Anvendt: "+f.title);
}
void restore(const Feature& f,const std::wstring& game){
 if(f.kind==Kind::Registry){for(size_t i=0;i<f.targets.size();++i)restoreRegistry(f.id==L"Dvr"?(i?L"Capture2":L"Capture"):f.id);}
 else if(f.kind==Kind::Spi){auto key=slot(f.id);if(read(key,L"Saved").exists){setSpi(f.spi,read(key,L"Value"));check(RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str()));}}
 else if(f.kind==Kind::Power)restorePower();
 else restoreRegistry(gameId(f,game));
 log(L"Gendannet: "+f.title);
}
bool active(const Feature& f,const std::wstring& game){
 if(f.kind==Kind::Registry){for(auto& t:f.targets)if(!same(read(t.path,t.name),number(t.value)))return false;return true;}
 if(f.kind==Kind::Spi){auto current=spiValue(f.spi);return same(current,desired(f.spi,current));}
 if(f.kind==Kind::Power){GUID* p=nullptr;check(PowerGetActiveScheme(nullptr,&p));bool result=IsEqualGUID(*p,highPower);LocalFree(p);return result;}
 auto target=gameTarget(f,game);auto value=asString(read(target.first,target.second));
 if(f.kind==Kind::Gpu){std::wstringstream stream(value);std::wstring token;bool found=false;while(std::getline(stream,token,L';')){if(token.rfind(L"GpuPreference=",0)==0){if(token!=L"GpuPreference=2")return false;found=true;}}return found;}
 return fullscreenFlags(value)==value;
}
bool backedUp(const Feature& f,const std::wstring& game){
 if(f.kind==Kind::Power)return read(root,L"Power").exists;
 if(f.id==L"Dvr")return read(slot(L"Capture"),L"Saved").exists||read(slot(L"Capture2"),L"Saved").exists;
 return read(slot(f.kind==Kind::Gpu||f.kind==Kind::Fullscreen?gameId(f,game):f.id),L"Saved").exists;
}
std::vector<std::wstring> backupIds(){
 std::vector<std::wstring> result;Key key;auto error=RegOpenKeyExW(HKEY_CURRENT_USER,root.c_str(),0,KEY_ENUMERATE_SUB_KEYS,&key.h);
 if(error==ERROR_FILE_NOT_FOUND)return result;
 check(error);
 for(DWORD i=0;;++i){wchar_t name[256];DWORD size=256;auto e=RegEnumKeyExW(key.h,i,name,&size,nullptr,nullptr,nullptr,nullptr);if(e==ERROR_NO_MORE_ITEMS)break;check(e);if(read(slot(name),L"Saved").exists)result.emplace_back(name);}
 if(read(root,L"Power").exists)result.push_back(L"Power");
 return result;
}
void restoreAll(){
 auto ids=backupIds();std::wstring failures;
 for(auto& id:ids){try{
  if(id==L"Power")restorePower();
  else if(read(slot(id),L"Spi").exists){auto action=asNumber(read(slot(id),L"Spi"));setSpi(action,read(slot(id),L"Value"));check(RegDeleteTreeW(HKEY_CURRENT_USER,slot(id).c_str()));}
  else restoreRegistry(id);
 }catch(...){failures+=id+L" ";}}
 if(!failures.empty()){log(L"Delvis gendannelse; resterende: "+failures);throw std::runtime_error("Some backups could not be restored. Remaining backups are retained; retry Restore All.");}
 log(L"Alle gemte indstillinger gendannet");
}
namespace {
const std::wstring runPath=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
std::wstring startupId(const std::wstring& name){Feature f{};f.id=L"Startup";return gameId(f,name);}
}
std::vector<StartupEntry> startupEntries(){
 std::vector<StartupEntry> result;Key key;auto error=RegOpenKeyExW(HKEY_CURRENT_USER,runPath.c_str(),0,KEY_QUERY_VALUE,&key.h);
 if(error!=ERROR_FILE_NOT_FOUND){check(error);DWORD maximum=0;check(RegQueryInfoKeyW(key.h,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&maximum,nullptr,nullptr,nullptr));
  for(DWORD i=0;;++i){std::vector<wchar_t> buffer(static_cast<size_t>(maximum)+2);DWORD size=static_cast<DWORD>(buffer.size()),type=0;
   auto e=RegEnumValueW(key.h,i,buffer.data(),&size,nullptr,&type,nullptr,nullptr);if(e==ERROR_NO_MORE_ITEMS)break;check(e);
   if(size&&(type==REG_SZ||type==REG_EXPAND_SZ)){std::wstring name(buffer.data(),size);result.push_back({name,true,read(slot(startupId(name)),L"Saved").exists});}
  }
 }
 for(auto& id:backupIds())if(id.rfind(L"Startup_",0)==0&&asString(read(slot(id),L"Path"))==runPath){auto name=asString(read(slot(id),L"Name"));auto found=std::find_if(result.begin(),result.end(),[&](const StartupEntry& e){return _wcsicmp(e.name.c_str(),name.c_str())==0;});if(found==result.end())result.push_back({name,false,true});}
 std::sort(result.begin(),result.end(),[](const StartupEntry& a,const StartupEntry& b){return _wcsicmp(a.name.c_str(),b.name.c_str())<0;});return result;
}
void disableStartup(const std::wstring& name){
 auto value=read(runPath,name);if(!value.exists)throw std::runtime_error("Startup entry no longer exists; refresh the list");
 saveRegistry(startupId(name),runPath,name);write(runPath,name,{});if(read(runPath,name).exists)throw std::runtime_error("Startup entry removal was not confirmed");log(L"Opstart fjernet: "+name);
}
void restoreStartup(const std::wstring& name){restoreRegistry(startupId(name));log(L"Opstart gendannet: "+name);}
void rememberGame(const std::wstring& game){write(L"Software\\GameTune",L"SelectedGame",stringValue(game));}
std::wstring selectedGame(){return asString(read(L"Software\\GameTune",L"SelectedGame"));}
std::wstring hardwareSummary(){
 wchar_t cpu[256]=L"CPU ukendt";DWORD size=sizeof cpu;
 RegGetValueW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",L"ProcessorNameString",RRF_RT_REG_SZ,nullptr,cpu,&size);
 SYSTEM_INFO sys;GetNativeSystemInfo(&sys);MEMORYSTATUSEX mem{};mem.dwLength=sizeof mem;GlobalMemoryStatusEx(&mem);
 DISPLAY_DEVICEW display{};display.cb=sizeof display;std::wstring gpu=L"GPU ukendt";
 for(DWORD i=0;EnumDisplayDevicesW(nullptr,i,&display,0);++i){if(display.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE){gpu=display.DeviceString;break;}display.cb=sizeof display;}
 DEVMODEW mode{};mode.dmSize=sizeof mode;EnumDisplaySettingsW(nullptr,ENUM_CURRENT_SETTINGS,&mode);
 std::wostringstream out;out<<cpu<<L"\r\n"<<sys.dwNumberOfProcessors<<L" logiske processorer  |  "<<(mem.ullTotalPhys/(1024ull*1024*1024))<<L" GB RAM ("<<mem.dwMemoryLoad<<L"% i brug)\r\n"<<gpu<<L"\r\n"<<mode.dmPelsWidth<<L" × "<<mode.dmPelsHeight<<L"  ·  "<<mode.dmDisplayFrequency<<L" Hz";return out.str();
}
void log(const std::wstring& message){
 SYSTEMTIME time;GetLocalTime(&time);wchar_t stamp[40];swprintf(stamp,40,L"%04d-%02d-%02d %02d:%02d:%02d",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
 try{auto old=history();auto next=std::wstring(stamp)+L"  "+message+L"\r\n"+old;
 if(next.size()>16000)next.resize(16000);
 write(L"Software\\GameTune",L"History",stringValue(next));}catch(...){/* Logging must not turn a successful setting change into an error. */}
}
std::wstring history(){try{return asString(read(L"Software\\GameTune",L"History"));}catch(...){return L"Historik kunne ikke læses.\r\n";}}
std::wstring errorText(const std::exception& error){std::string s=error.what();return std::wstring(s.begin(),s.end());}
}
