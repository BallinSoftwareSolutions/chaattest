#include "engine.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cstring>
int main(int argc,char** argv){
 auto unique=std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());
 auto path=L"Software\\GameTune\\Tests\\"+unique;auto id=L"Test_"+unique;
 auto require=[](bool condition,const char* message){if(!condition)throw std::runtime_error(message);};
 try{
  require(!tune::read(path,L"Synthetic").exists,"test key unexpectedly exists");
  tune::saveRegistry(id,path,L"Synthetic");tune::write(path,L"Synthetic",tune::number(1));
  tune::saveRegistry(id,path,L"Synthetic");tune::write(path,L"Synthetic",tune::number(0));
  tune::restoreRegistry(id);require(!tune::read(path,L"Synthetic").exists,"absent value not restored");
  tune::Value original;original.type=REG_BINARY;original.exists=true;original.bytes={0,42,255,13,0};
  tune::write(path,L"Synthetic",original);tune::saveRegistry(id,path,L"Synthetic");tune::write(path,L"Synthetic",tune::number(2));
  bool rejected=false;try{tune::saveRegistry(id,path,L"OtherValue");}catch(...){rejected=true;}
  require(rejected,"backup collision was not rejected");
  tune::restoreRegistry(id);tune::restoreRegistry(id);auto restored=tune::read(path,L"Synthetic");
  require(restored.exists&&restored.type==original.type&&restored.bytes==original.bytes,"typed binary backup not restored");
  require(tune::gpuPreference(L"GpuPreference=1;Other=3;")==L"Other=3;GpuPreference=2;","GPU preference overwrote other values");
  require(tune::gpuPreference(tune::gpuPreference(L"Other=3;"))==tune::gpuPreference(L"Other=3;"),"GPU merge is not idempotent");
  require(tune::fullscreenFlags(L"~ RUNASADMIN")==L"~ RUNASADMIN DISABLEDXMAXIMIZEDWINDOWEDMODE","compatibility flags not preserved");
  require(tune::fullscreenFlags(tune::fullscreenFlags(L"~ RUNASADMIN"))==tune::fullscreenFlags(L"~ RUNASADMIN"),"fullscreen merge is not idempotent");
  auto ids=tune::backupIds();require(std::find(ids.begin(),ids.end(),id)==ids.end(),"restored backup marker remains");
  for(const auto& f:tune::features())if(f.kind==tune::Kind::Gpu){bool invalidRejected=false;auto before=tune::backupIds();try{tune::apply(f,L"Z:\\nonexistent-gametune-fixture.exe");}catch(...){invalidRejected=true;}require(invalidRejected,"missing executable accepted");require(before==tune::backupIds(),"missing executable created a backup");}
  const auto& features=tune::features();require(features.size()==23,"feature count changed unexpectedly");
  for(size_t i=0;i<features.size();++i){require(!features[i].details.empty(),"feature has no explanation");for(size_t j=i+1;j<features.size();++j)require(features[i].id!=features[j].id,"duplicate feature ID");}
  if(argc>1&&std::string(argv[1])=="--system-settings"){
   unsigned tested=0,skipped=0;
   for(const auto& f:features){if(f.kind!=tune::Kind::Spi)continue;
    if(f.spi==SPI_GETSTICKYKEYS||f.spi==SPI_GETTOGGLEKEYS||f.spi==SPI_GETFILTERKEYS){
     FILTERKEYS availability{};availability.cbSize=f.spi==SPI_GETFILTERKEYS?sizeof(FILTERKEYS):sizeof(STICKYKEYS);
     if(SystemParametersInfoW(f.spi,availability.cbSize,&availability,0)&&!(availability.dwFlags&2)){std::cout<<"SKIP: "<<std::string(f.id.begin(),f.id.end())<<" not reported available by platform\n";++skipped;continue;}
    }
    // Exercise real SPI getters/setters and verify persisted backup restoration.
    require(!tune::backedUp(f,L""),"isolated test requires no preexisting SPI backups");
    std::cout<<"Testing "<<std::string(f.id.begin(),f.id.end())<<"\n";
    try{tune::apply(f,L"");require(tune::active(f,L""),"SPI setting did not take effect");require(tune::backedUp(f,L""),"SPI baseline not saved");auto original=tune::read(L"Software\\GameTune\\Backup\\"+f.id,L"Value");
     tune::restore(f,L"");require(!tune::backedUp(f,L""),"SPI backup was not cleared");
     auto actual=original.bytes;std::fill(actual.begin(),actual.end(),0);UINT size=0;
     if(f.spi==SPI_GETSTICKYKEYS||f.spi==SPI_GETTOGGLEKEYS||f.spi==SPI_GETFILTERKEYS||f.spi==SPI_GETANIMATION){size=static_cast<UINT>(actual.size());std::memcpy(actual.data(),&size,4);}
     require(SystemParametersInfoW(f.spi,size,actual.data(),0)!=FALSE,"SPI restoration could not be read");
     require(actual==original.bytes,"SPI baseline was not restored exactly");++tested;}
    catch(...){try{tune::restore(f,L"");}catch(...){}throw;}
   }
   std::cout<<"PASS: "<<tested<<" real system-setting apply/restore cycles; "<<skipped<<" unavailable settings skipped\n";
  }
  RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());
  std::cout<<"PASS: absent restore, typed restore, repeated apply/restore, backup collision, GPU merge, fullscreen merge, catalog integrity\n";return 0;
 }catch(const std::exception& error){
  RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());RegDeleteTreeW(HKEY_CURRENT_USER,(L"Software\\GameTune\\Backup\\"+id).c_str());
  std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;
 }
}
