#pragma once
#include <windows.h>
#include <exception>
#include <string>
#include <vector>
namespace tune {
struct Value { DWORD type=REG_NONE; std::vector<BYTE> bytes; bool exists=false; };
struct RegTarget { std::wstring path,name; DWORD value; };
enum class Kind { Registry, Spi, Power, Gpu, Fullscreen };
struct Feature {
 std::wstring id,title,category,description,details,scope;
 Kind kind; UINT spi=0; std::vector<RegTarget> targets;
};
struct StartupEntry {std::wstring name;bool present=false,backup=false;};
std::vector<StartupEntry> startupEntries();
void disableStartup(const std::wstring& name);
void restoreStartup(const std::wstring& name);
const std::vector<Feature>& features();
Value read(const std::wstring& path,const std::wstring& name);
void write(const std::wstring& path,const std::wstring& name,const Value& value);
Value number(DWORD value);
Value stringValue(const std::wstring& value);
void saveRegistry(const std::wstring& id,const std::wstring& path,const std::wstring& name);
void restoreRegistry(const std::wstring& id);
void apply(const Feature& feature,const std::wstring& game);
void restore(const Feature& feature,const std::wstring& game);
bool active(const Feature& feature,const std::wstring& game);
bool backedUp(const Feature& feature,const std::wstring& game);
std::vector<std::wstring> backupIds();
void restoreAll();
void rememberGame(const std::wstring& game);
std::wstring selectedGame();
std::wstring hardwareSummary();
std::wstring gpuPreference(const std::wstring& existing);
std::wstring fullscreenFlags(const std::wstring& existing);
void log(const std::wstring& message);
std::wstring history();
std::wstring errorText(const std::exception& error);
}
