#include <winsock2.h>
#include <ws2tcpip.h>
#include "engine.h"
#include "motion.h"
#include "localization.h"
#include "ui.h"
#include "ui_text.h"
#include "bloat.h"
#include "power_policy.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <cwctype>
#include <dwmapi.h>
#include <fstream>
#include <functional>
#include <future>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <psapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <sstream>
#include <tlhelp32.h>
#include <windowsx.h>
#include <mmsystem.h>

namespace {
std::wstring tr(const std::wstring &source) {
  return l10n::text(source);
}
HWND window = nullptr;
ui::Canvas canvas;
float scale = 1;
double clockNow() {
  static const double frequency = [] {
    LARGE_INTEGER value;
    QueryPerformanceFrequency(&value);
    return static_cast<double>(value.QuadPart);
  }();
  LARGE_INTEGER value;
  QueryPerformanceCounter(&value);
  return static_cast<double>(value.QuadPart) * 1000.0 / frequency;
}
int pixel(float n) {
  return static_cast<int>(n * scale + .5f);
}
const std::vector<std::wstring> pages = {L"Overblik", L"Indstillinger", L"Fokus", L"Netværk",
                                         L"Gendannelse"};
const std::vector<std::wstring> categories = {L"Alle",     L"Gaming",    L"Input",     L"Windows",
                                              L"Baggrund", L"Privatliv", L"Spilprofil"};
int page = 0, category = 0, focusTab = 0, selectedFeature = 0, hovered = 0, pressed = 0,
    keyboardFocus = 0, inputFocus = 0;
size_t featureOffset = 0, processOffset = 0, startupOffset = 0;
std::wstring search, address = L"1.1.1.1", game, hardware, activity,
                     status = L"Klar til din næste session.", pingResult = L"Ingen måling endnu.";
size_t cursor = 0, anchor = 0;
float editScroll = 0;
wchar_t pendingHigh = 0;
bool keyboardMode = false;
size_t backups = 0;
unsigned processCount = 0, threadCount = 0, ramLoad = 0;
unsigned long long totalRam = 0;
bool timerRunning = false, closeRequested = false;
bool preciseTimer = false;
std::vector<size_t> filtered;
struct State {
  bool active = false, backup = false, available = false;
  std::wstring error;
};
std::vector<State> states;
struct Hit {
  int id;
  ui::Rect rect;
  bool enabled = true;
  bool field = false;
};
std::vector<Hit> hits;
std::map<int, motion::Tween> hovers, toggles;
motion::Tween nav, inspector, notice;
motion::Scroll listPosition, focusPosition;
bool modelReady = false, hitClipping = false;
ui::Rect clipHits{};
int statusTone = 0;
std::future<std::wstring> job, pingJob;
std::wstring jobSuccess;
int jobFeature = -1;
bool refreshFocusAfterJob = false;
struct Dialog {
  bool open = false, information = false, danger = false, languagePicker = false;
  std::wstring title, body, primary = L"Anvend", eyebrow = L"DU HAR KONTROLLEN";
  std::function<void()> accept;
  motion::Tween opacity;
  motion::Scroll scroll;
  float maximum = 0;
  int previousFocus = 0, previousInput = 0;
  size_t previousCursor = 0, previousAnchor = 0;
} dialog;
struct Process {
  HANDLE handle;
  DWORD pid;
  std::wstring name;
};
struct Priority {
  HANDLE handle;
  DWORD original;
};
std::vector<Process> processes;
std::vector<Priority> priorities;
struct PowerOverride {
  HANDLE handle;
  PROCESS_POWER_THROTTLING_STATE original;
};
std::vector<PowerOverride> powerOverrides;
bool ecoAvailable = false, ecoForced = false;
std::wstring ecoStatus = L"Vælg en app";
bloat::Inventory inventory;
std::future<bloat::Inventory> inventoryJob;
std::shared_ptr<std::atomic_bool> inventoryCancel;
std::vector<bloat::App> removedApps;
std::optional<bloat::App> pendingRemoval;
int bloatSelection = -1;
size_t bloatOffset = 0;
bool inventoryLoaded = false, reloadInventoryAfterJob = false;
void updateEco();
void startInventory(bool force = false);
std::vector<tune::StartupEntry> startups;
int processSelection = -1, startupSelection = -1;
bool smoke = false, smokeFailed = false, smokeChecked = false;
int smokeStep = 0;

void invalidate() {
  InvalidateRect(window, nullptr, FALSE);
}
UINT animationInterval() {
  MONITORINFOEXW monitor{};
  monitor.cbSize = sizeof monitor;
  DEVMODEW mode{};
  mode.dmSize = sizeof mode;
  if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) &&
      EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
      mode.dmDisplayFrequency >= 30 && mode.dmDisplayFrequency <= 360)
    return std::max<UINT>(4, 1000 / mode.dmDisplayFrequency);
  return 8;
}
void animate() {
  if (window && !timerRunning) {
    preciseTimer = timeBeginPeriod(1) == TIMERR_NOERROR;
    timerRunning = SetTimer(window, 2, animationInterval(), nullptr) != 0;
    if (!timerRunning && preciseTimer) {
      timeEndPeriod(1);
      preciseTimer = false;
    }
  }
}
std::wstring lower(std::wstring value) {
  if (!value.empty())
    CharLowerBuffW(value.data(), static_cast<DWORD>(value.size()));
  return value;
}
std::wstring filename(const std::wstring &value) {
  auto at = value.find_last_of(L"\\/");
  return at == std::wstring::npos ? value : value.substr(at + 1);
}
void setStatus(const std::wstring &value, int tone = 0) {
  status = value;
  statusTone = tone;
  notice.snap(0);
  notice.target(1, clockNow(), 220);
  invalidate();
  animate();
}
void refreshStates() {
  states.assign(tune::features().size(), {});
  for (size_t i = 0; i < states.size(); ++i) {
    auto &s = states[i];
    const auto &f = tune::features()[i];
    if ((f.kind == tune::Kind::Gpu || f.kind == tune::Kind::Fullscreen) && game.empty()) {
      s.error = tr(L"Vælg spillets .exe først");
      continue;
    }
    try {
      s.active = tune::active(f, game);
      s.backup = tune::backedUp(f, game);
      s.available = true;
    } catch (const std::exception &e) {
      s.error = tune::errorText(e);
      try {
        s.backup = tune::backedUp(f, game);
      } catch (...) {
      }
    }
    if (modelReady)
      toggles[static_cast<int>(i)].target(s.active ? 1 : 0, clockNow(), 220);
    else
      toggles[static_cast<int>(i)].snap(s.active ? 1 : 0);
  }
  modelReady = true;
  try {
    backups = tune::backupIds().size();
    activity = tune::history();
  } catch (...) {
  }
}
void refreshHardware(bool inspectHardware = true) {
  if (inspectHardware)
    hardware = tune::hardwareSummary();
  PERFORMANCE_INFORMATION info{};
  info.cb = sizeof info;
  if (GetPerformanceInfo(&info, sizeof info)) {
    processCount = info.ProcessCount;
    threadCount = info.ThreadCount;
  }
  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof memory;
  if (GlobalMemoryStatusEx(&memory)) {
    ramLoad = memory.dwMemoryLoad;
    totalRam = memory.ullTotalPhys / (1024ull * 1024 * 1024);
  }
}
void filterFeatures() {
  filtered.clear();
  for (size_t i = 0; i < tune::features().size(); ++i) {
    const auto &f = tune::features()[i];
    if (category && f.category != categories[static_cast<size_t>(category)])
      continue;
    if (!search.empty() &&
        lower(tr(f.title) + L" " + tr(f.description) + L" " + tr(f.category)).find(lower(search)) ==
            std::wstring::npos)
      continue;
    filtered.push_back(i);
  }
  if (featureOffset >= filtered.size())
    featureOffset = 0;
  if (std::find(filtered.begin(), filtered.end(), static_cast<size_t>(selectedFeature)) ==
          filtered.end() &&
      !filtered.empty())
    selectedFeature = static_cast<int>(filtered.front());
}
std::wstring &editValue();
void showDialog(const std::wstring &title, const std::wstring &body, const std::wstring &primary,
                std::function<void()> accept = {}, bool danger = false, bool information = false) {
  dialog.languagePicker = false;
  if (!dialog.open) {
    dialog.previousFocus = keyboardFocus;
    dialog.previousInput = inputFocus;
    dialog.previousCursor = cursor;
    dialog.previousAnchor = anchor;
  }
  pendingHigh = 0;
  dialog.title = title;
  dialog.body = body;
  dialog.primary = primary;
  dialog.accept = std::move(accept);
  dialog.danger = danger;
  dialog.information = information;
  dialog.open = true;
  dialog.scroll.snap(0);
  dialog.opacity.target(1, clockNow(), 180);
  keyboardFocus = danger && !information ? 9002 : 9001;
  inputFocus = 0;
  invalidate();
  animate();
}
void dismissDialog(bool accept) {
  auto callback = accept ? dialog.accept : std::function<void()>{};
  dialog.accept = {};
  dialog.open = false;
  dialog.opacity.target(0, clockNow(), 150);
  keyboardFocus = dialog.previousFocus;
  inputFocus =
      ((dialog.previousInput == 300 && page == 1) || (dialog.previousInput == 510 && page == 3))
          ? dialog.previousInput
          : 0;
  if (inputFocus) {
    cursor = std::min(dialog.previousCursor, editValue().size());
    anchor = std::min(dialog.previousAnchor, editValue().size());
  }
  invalidate();
  animate();
  if (callback)
    callback();
}
void errorDialog(const std::wstring &message) {
  showDialog(tr(L"Ændringen blev ikke gennemført"),
             message + tr(L"\n\nEventuelle originale backups er stadig gemt. Du kan se status "
                          L"under Gendannelse."),
             tr(L"Forstået"), {}, true, true);
}
void closeProcessHandles() {
  for (auto &p : processes)
    CloseHandle(p.handle);
  processes.clear();
}
bool protectedProcess(const std::wstring &name) {
  const auto n = lower(name);
  for (const wchar_t *entry : {L"csrss.exe",
                               L"wininit.exe",
                               L"winlogon.exe",
                               L"services.exe",
                               L"lsass.exe",
                               L"svchost.exe",
                               L"dwm.exe",
                               L"explorer.exe",
                               L"sihost.exe",
                               L"taskhostw.exe",
                               L"conhost.exe",
                               L"smss.exe",
                               L"audiodg.exe",
                               L"spoolsv.exe",
                               L"searchhost.exe",
                               L"startmenuexperiencehost.exe",
                               L"shellexperiencehost.exe",
                               L"runtimebroker.exe",
                               L"securityhealthsystray.exe",
                               L"msmpeng.exe"})
    if (n == entry)
      return true;
  return false;
}
void loadProcesses() {
  DWORD previous = processSelection >= 0 && static_cast<size_t>(processSelection) < processes.size()
                       ? processes[static_cast<size_t>(processSelection)].pid
                       : 0;
  closeProcessHandles();
  std::vector<DWORD> visible;
  EnumWindows(
      [](HWND w, LPARAM data) -> BOOL {
        if (IsWindowVisible(w) && GetWindow(w, GW_OWNER) == nullptr) {
          DWORD pid = 0;
          GetWindowThreadProcessId(w, &pid);
          reinterpret_cast<std::vector<DWORD> *>(data)->push_back(pid);
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&visible));
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    return;
  PROCESSENTRY32W item{};
  item.dwSize = sizeof item;
  if (Process32FirstW(snapshot, &item)) {
    do {
      if (item.th32ProcessID == GetCurrentProcessId() || protectedProcess(item.szExeFile) ||
          std::find(visible.begin(), visible.end(), item.th32ProcessID) == visible.end())
        continue;
      HANDLE h =
          OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION | SYNCHRONIZE,
                      FALSE, item.th32ProcessID);
      if (h)
        processes.push_back({h, item.th32ProcessID, item.szExeFile});
    } while (Process32NextW(snapshot, &item));
  }
  CloseHandle(snapshot);
  std::sort(processes.begin(), processes.end(),
            [](const Process &a, const Process &b) { return lower(a.name) < lower(b.name); });
  processSelection = processes.empty() ? -1 : 0;
  for (size_t i = 0; i < processes.size(); ++i)
    if (processes[i].pid == previous)
      processSelection = static_cast<int>(i);
  processOffset = 0;
  updateEco();
}
void loadStartups() {
  auto name = startupSelection >= 0 && static_cast<size_t>(startupSelection) < startups.size()
                  ? startups[static_cast<size_t>(startupSelection)].name
                  : L"";
  startups = tune::startupEntries();
  startupSelection = startups.empty() ? -1 : 0;
  for (size_t i = 0; i < startups.size(); ++i)
    if (startups[i].name == name)
      startupSelection = static_cast<int>(i);
  startupOffset = 0;
}
void updateEco() {
  ecoAvailable = false;
  ecoForced = false;
  ecoStatus = tr(L"Vælg en app");
  if (processSelection < 0 || static_cast<size_t>(processSelection) >= processes.size())
    return;
  if (GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")) {
    ecoStatus = tr(L"EcoQoS kræver afprøvning på Windows 11");
    return;
  }
  auto &selected = processes[static_cast<size_t>(processSelection)];
  PROCESS_POWER_THROTTLING_STATE current{};
  current.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  if (WaitForSingleObject(selected.handle, 0) != WAIT_TIMEOUT ||
      !GetProcessInformation(selected.handle, ProcessPowerThrottling, &current, sizeof current)) {
    ecoStatus = tr(L"Windows tillader ikke EcoQoS på denne proces");
    return;
  }
  ecoAvailable = true;
  ecoForced = session_power::forcedOff({current.Version, current.ControlMask, current.StateMask});
  ecoStatus = ecoForced ? tr(L"EcoQoS: execution-speed throttling er tvunget fra")
              : (current.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED)
                  ? tr(L"EcoQoS: execution-speed throttling er aktiv")
                  : tr(L"EcoQoS: Windows-styret / ikke aktiv lige nu");
}
void restorePriorities() {
  std::vector<Priority> failed;
  for (auto &p : priorities) {
    if (WaitForSingleObject(p.handle, 0) == WAIT_OBJECT_0 || SetPriorityClass(p.handle, p.original))
      CloseHandle(p.handle);
    else
      failed.push_back(p);
  }
  priorities = std::move(failed);
  std::vector<PowerOverride> powerFailed;
  for (auto &p : powerOverrides) {
    if (WaitForSingleObject(p.handle, 0) == WAIT_OBJECT_0 ||
        SetProcessInformation(p.handle, ProcessPowerThrottling, &p.original, sizeof p.original))
      CloseHandle(p.handle);
    else
      powerFailed.push_back(p);
  }
  powerOverrides = std::move(powerFailed);
  updateEco();
  if (!priorities.empty() || !powerOverrides.empty())
    throw std::runtime_error("Some session choices could not be restored. Retry before closing.");
}
void disableEco() {
  if (!ecoAvailable || processSelection < 0 ||
      static_cast<size_t>(processSelection) >= processes.size())
    throw std::runtime_error("EcoQoS is unavailable for the selected process");
  auto &selected = processes[static_cast<size_t>(processSelection)];
  PROCESS_POWER_THROTTLING_STATE original{};
  original.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  if (WaitForSingleObject(selected.handle, 0) != WAIT_TIMEOUT ||
      !GetProcessInformation(selected.handle, ProcessPowerThrottling, &original, sizeof original))
    throw std::runtime_error("Cannot read the process power state");
  auto changed =
      session_power::unthrottled({original.Version, original.ControlMask, original.StateMask});
  PROCESS_POWER_THROTTLING_STATE desired{changed.version, changed.control, changed.enabled};
  auto existing =
      std::find_if(powerOverrides.begin(), powerOverrides.end(), [&](const PowerOverride &item) {
        return WaitForSingleObject(item.handle, 0) == WAIT_TIMEOUT &&
               GetProcessId(item.handle) == selected.pid;
      });
  HANDLE retained = nullptr;
  if (existing == powerOverrides.end() &&
      !DuplicateHandle(GetCurrentProcess(), selected.handle, GetCurrentProcess(), &retained, 0,
                       FALSE, DUPLICATE_SAME_ACCESS))
    throw std::runtime_error("Cannot retain a session baseline");
  if (!SetProcessInformation(selected.handle, ProcessPowerThrottling, &desired, sizeof desired)) {
    if (retained)
      CloseHandle(retained);
    throw std::runtime_error("Windows rejected the process power change");
  }
  if (retained)
    powerOverrides.push_back({retained, original});
  updateEco();
  if (!ecoForced)
    throw std::runtime_error("Windows did not confirm the power change; session baseline retained");
  tune::log(tr(L"EcoQoS execution-speed throttling fra: ") + selected.name);
  setStatus(
      tr(L"Proces-strømbegrænsning slået fra. Originaltilstanden gendannes ved normal applukning."),
      2);
}
void startInventory(bool force) {
  if (inventoryJob.valid() || (!force && inventoryLoaded))
    return;
  inventoryCancel = std::make_shared<std::atomic_bool>(false);
  auto cancel = inventoryCancel;
  inventoryJob = std::async(std::launch::async, [cancel] { return bloat::inspect(cancel); });
  invalidate();
}
void finishInventory() {
  if (!inventoryJob.valid() ||
      inventoryJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    return;
  inventory = inventoryJob.get();
  inventoryLoaded = true;
  if (inventory.error.empty())
    for (auto app : removedApps)
      if (std::find_if(inventory.apps.begin(), inventory.apps.end(),
                       [&](const bloat::App &candidate) { return candidate.name == app.name; }) ==
          inventory.apps.end()) {
        app.installed = false;
        inventory.apps.push_back(std::move(app));
      }
  bloatSelection = inventory.apps.empty() ? -1 : 0;
  bloatOffset = 0;
  invalidate();
}
void beginJob(std::function<std::wstring()> work, const std::wstring &success, int feature = -1,
              bool focusRefresh = false) {
  if (job.valid())
    return;
  jobSuccess = success;
  jobFeature = feature;
  refreshFocusAfterJob = focusRefresh;
  job = std::async(std::launch::async, [work] {
    try {
      return work();
    } catch (const std::exception &e) {
      return tune::errorText(e);
    } catch (...) {
      return std::wstring(tr(L"Ukendt fejl. Backups bevares."));
    }
  });
  setStatus(tr(L"Arbejder. Du kan fortsat navigere i appen."), 1);
  animate();
}
void finishJob() {
  if (!job.valid() || job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    return;
  auto error = job.get();
  refreshStates();
  if (refreshFocusAfterJob)
    loadStartups();
  jobFeature = -1;
  if (reloadInventoryAfterJob) {
    reloadInventoryAfterJob = false;
    if (error.empty() && pendingRemoval)
      removedApps.push_back(*pendingRemoval);
    pendingRemoval.reset();
    startInventory(true);
  }
  if (error.empty()) {
    setStatus(jobSuccess, 2);
    if (closeRequested)
      PostMessageW(window, WM_CLOSE, 0, 0);
  } else {
    closeRequested = false;
    tune::log(tr(L"Fejl: ") + error);
    setStatus(tr(L"Ændringen blev ikke gennemført. Se oplysningerne."), 3);
    errorDialog(error);
  }
}
void navigate(int next) {
  if (next == page)
    return;
  canvas.transitionPage(clockNow());
  page = next;
  inputFocus = 0;
  nav.target(330 + next * 126, clockNow(), 240);
  if (page == 2) {
    if (focusTab == 1)
      loadProcesses();
    if (focusTab == 2)
      loadStartups();
    if (focusTab == 3)
      startInventory();
  }
  invalidate();
  animate();
}
void switchFocusTab(int tab) {
  if (tab == focusTab)
    return;
  canvas.transitionPage(clockNow());
  focusTab = tab;
  focusPosition.snap(0);
  if (tab == 1)
    loadProcesses();
  if (tab == 2)
    loadStartups();
  if (tab == 3)
    startInventory();
  invalidate();
  animate();
}
void startProfile(int kind) {
  const std::vector<std::vector<std::wstring>> keys = {
      {L"GameMode", L"Dvr"},
      {L"MouseAcceleration", L"StickyHotkey", L"ToggleHotkey"},
      {L"EdgeBackground", L"EdgeStartupBoost", L"ChromeBackground"},
      {L"Transparency", L"WindowAnimation", L"MenuAnimation", L"TooltipAnimation",
       L"ClientAnimation"}};
  const std::vector<std::wstring> names = {tr(L"Gamingprofil"), tr(L"Inputprofil"),
                                           tr(L"Fokusprofil"), tr(L"Minimal desktop")};
  auto selected = keys[static_cast<size_t>(kind)];
  std::wstring body = tr(L"Disse indstillinger tilpasses, og deres originale værdier gemmes:\n\n");
  for (const auto &id : selected)
    for (const auto &f : tune::features())
      if (f.id == id)
        body += L"• " + tr(f.title) + L"\n";
  body += kind == 2 ? tr(L"\nGenstart browserne. Baggrundsudvidelser og notifikationer kan stoppe. "
                         L"Åbne apps og Windows-tjenester lukkes ikke.")
          : kind == 1 ? tr(L"\nÆndrer Windows-museadfærd og tilgængelighedsgenveje. Raw-input-spil "
                           L"kan ignorere museindstillingerne.")
                      : tr(L"\nEffekten afhænger af spil, hardware og dine præferencer. Ingen "
                           L"garanteret FPS-forbedring.");
  showDialog(names[static_cast<size_t>(kind)], body, tr(L"Anvend profil"), [selected] {
    auto chosen = game;
    beginJob(
        [selected, chosen] {
          std::wstring errors;
          for (auto &id : selected)
            for (auto &f : tune::features())
              if (f.id == id) {
                try {
                  tune::apply(f, chosen);
                } catch (const std::exception &e) {
                  errors += tr(f.title) + L": " + tune::errorText(e) + L"\n";
                }
              }
          return errors;
        },
        tr(L"Profil anvendt. Originale indstillinger kan gendannes."));
  });
}
void changeFeature(size_t index, bool restoring) {
  if (job.valid() || index >= states.size())
    return;
  auto feature = tune::features()[index];
  auto chosen = game;
  auto action = [feature, chosen, index, restoring] {
    beginJob(
        [feature, chosen, restoring] {
          if (restoring)
            tune::restore(feature, chosen);
          else
            tune::apply(feature, chosen);
          return std::wstring{};
        },
        (restoring ? tr(L"Gendannet: ") : tr(L"Anvendt: ")) + tr(feature.title) + L". " +
            tr(feature.scope),
        static_cast<int>(index));
  };
  bool confirm =
      !restoring && (feature.kind == tune::Kind::Power || feature.kind == tune::Kind::Fullscreen ||
                     feature.spi == SPI_GETFILTERKEYS || feature.spi == SPI_GETSTICKYKEYS ||
                     feature.spi == SPI_GETTOGGLEKEYS || feature.spi == SPI_GETMOUSESPEED);
  if (confirm)
    showDialog(tr(feature.title),
               tr(feature.details) + tr(L"\n\nOriginalværdien gemmes før ændringen."),
               tr(L"Anvend ændring"), action, feature.kind == tune::Kind::Fullscreen);
  else
    action();
}
void hit(int id, ui::Rect rect, bool enabled = true, bool field = false) {
  if (hitClipping) {
    float x = std::max(rect.x, clipHits.x), y = std::max(rect.y, clipHits.y),
          right = std::min(rect.x + rect.w, clipHits.x + clipHits.w),
          bottom = std::min(rect.y + rect.h, clipHits.y + clipHits.h);
    if (right <= x || bottom <= y)
      return;
    rect = {x, y, right - x, bottom - y};
  }
  hits.push_back({id, rect, enabled, field});
}
float hover(int id) {
  return static_cast<float>(hovers[id].value(clockNow()));
}
void button(ui::Painter &p, int id, ui::Rect r, const std::wstring &label, bool primary = false,
            bool enabled = true, bool danger = false) {
  hit(id, r, enabled);
  float h = enabled ? hover(id) : 0;
  float down = pressed == id ? 1.f : 0.f;
  ui::Rect surface = r;
  surface.y += down - h * 1.5f;
  unsigned color = primary ? (danger ? ui::red : ui::lime) : ui::raised;
  p.round({surface.x, surface.y + 3, surface.w, surface.h}, 0x050609, 10, .4f);
  p.round(surface, ui::mix(color, 0xffffff, h * .07), 10, enabled ? 1 : .38f);
  if (!primary)
    p.border(surface, ui::mix(ui::line, ui::muted, h * .22), 10, enabled ? 1 : .45f);
  float labelSize = 14;
  while (labelSize > 10 && p.textWidth(label, labelSize) * 1.1f > surface.w - 24)
    labelSize -= .5f;
  p.text(label, {surface.x + 12, surface.y + (surface.h - 20) / 2, surface.w - 24, 24}, labelSize,
         primary   ? ui::background
         : enabled ? ui::white
                   : ui::muted,
         true, DWRITE_TEXT_ALIGNMENT_CENTER, enabled ? 1 : .52f);
  if (keyboardFocus == id)
    p.border({surface.x - 3, surface.y - 3, surface.w + 6, surface.h + 6}, ui::purple, 12, .8f,
             1.5f);
}
void pill(ui::Painter &p, const std::wstring &label, ui::Rect r, unsigned color = ui::muted) {
  p.round(r, color, 6, .08f);
  p.border(r, color, 6, .13f);
  p.text(label, {r.x + 9, r.y + 4, r.w - 18, r.h - 4}, 11, color, true);
}
void panel(ui::Painter &p, ui::Rect r, unsigned color = ui::panel) {
  p.round({r.x, r.y + 4, r.w, r.h}, 0x040508, 18, .35f);
  p.round(r, color, 18);
  p.border(r, ui::line, 18, .7f);
}
void input(ui::Painter &p, int id, ui::Rect r, const std::wstring &value,
           const std::wstring &placeholder) {
  hit(id, r, true, true);
  bool active = inputFocus == id;
  p.round(r, ui::background, 10);
  p.border(r, active ? ui::purple : ui::line, 10, 1, active ? 1.5f : 1);
  p.clip({r.x + 12, r.y + 6, r.w - 24, r.h - 12});
  // Edit strings are deliberately short; caret scrolling uses the same DirectWrite metrics as
  // rendering.
  float caretX = p.textWidth(value.substr(0, std::min(cursor, value.size())));
  float scroll = active ? std::max(0.f, caretX - r.w + 42) : 0;
  if (active)
    editScroll = scroll;
  if (active && cursor != anchor) {
    float a = p.textWidth(value.substr(0, std::min(cursor, anchor))),
          b = p.textWidth(value.substr(0, std::max(cursor, anchor)));
    p.round({r.x + 14 + a - scroll, r.y + 9, b - a, 22}, ui::purple, 3, .28f);
  }
  p.text(value.empty() ? placeholder : value,
         {r.x + 14 - scroll, r.y + (r.h - 21) / 2, r.w + scroll - 26, 25}, 14,
         value.empty() ? ui::muted : ui::white);
  if (active && static_cast<ULONGLONG>(clockNow() / 500) % 2 == 0)
    p.line(r.x + 14 + caretX - scroll, r.y + 11, r.x + 14 + caretX - scroll, r.y + r.h - 11,
           ui::lime, 1.2f);
  p.unclip();
}
void sectionTitle(ui::Painter &p, const std::wstring &eyebrow, const std::wstring &title,
                  const std::wstring &subtitle) {
  p.text(eyebrow, {44, 119, 1100, 20}, 11, ui::lime, true);
  p.text(title, {42, 145, 1100, 52}, 34, ui::white, true);
  p.text(subtitle, {44, 199, 1100, 32}, 14, ui::muted);
}
void switchControl(ui::Painter &p, int index, ui::Rect r) {
  auto &s = states[static_cast<size_t>(index)];
  bool enabled = !job.valid() && (s.active ? s.backup : s.available);
  hit(1000 + index, r, enabled);
  float value = static_cast<float>(toggles[index].value(clockNow())),
        h = enabled ? hover(1000 + index) : 0;
  unsigned track = ui::mix(0x343b47, ui::lime, value);
  p.round(r, ui::mix(track, ui::white, h * .09f), r.h / 2, enabled || s.active ? 1 : .42f);
  float x = r.x + 5 + (r.w - r.h) * value;
  p.circle(x + (r.h - 10) / 2, r.y + r.h / 2, (r.h - 10) / 2,
           ui::mix(0x929aa8, ui::background, value));
  if (keyboardFocus == 1000 + index)
    p.border({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, ui::purple, r.h / 2 + 4, 1, 1.5f);
}
void featureRow(ui::Painter &p, size_t index, float y, float width = 754) {
  const auto &f = tune::features()[index];
  auto &s = states[index];
  bool selected = static_cast<int>(index) == selectedFeature;
  ui::Rect r{44, y, width, 68};
  hit(2000 + static_cast<int>(index), r);
  float hot = hover(2000 + static_cast<int>(index));
  p.round(r, ui::mix(selected ? 0x20252d : ui::panel, 0x252c36, hot * .5), 12);
  p.border(r, selected ? ui::purple : ui::line, 12, selected ? .65f : .5f);
  if (selected)
    p.round({44, y + 18, 3, 32}, ui::purple, 2);
  p.circle(72, y + 33, 13, s.active ? ui::lime : ui::muted, .08f);
  p.circle(72, y + 33, 3, s.active ? ui::lime : ui::muted);
  p.text(tr(f.title), {96, y + 12, width - 192, 23}, 16, ui::white, true);
  p.text(tr(f.scope), {96, y + 39, width - 192, 18}, 11, ui::muted);
  switchControl(p, static_cast<int>(index), {44 + width - 76, y + 21, 48, 26});
}
void drawDashboard(ui::Painter &p) {
  p.text(tr(L"D I T   P E R F O R M A N C E - W O R K S P A C E"), {44, 134, 710, 22}, 11, ui::lime,
         true);
  p.text(tr(L"Mindre støj.\nMere spil."), {40, 177, 718, 143}, 56, ui::white, true);
  p.text(tr(L"Tilpas Windows til din session. Vælg præcis, hvad der ændres — og behold en vej "
            L"tilbage."),
         {44, 324, 630, 55}, 17, ui::muted);
  button(p, 423, {44, 395, 212, 46}, tr(L"Klargør fokus"), true, !job.valid());
  p.text(tr(L"3 baggrundsvalg · kan gendannes"), {276, 410, 450, 28}, 12, ui::muted);
  panel(p, {794, 132, 378, 302});
  p.text(tr(L"LIGE NU PÅ DIN PC"), {820, 157, 240, 20}, 11, ui::muted, true);
  p.circle(1139, 166, 3, ui::lime);
  p.text(processCount ? std::to_wstring(processCount) : L"—", {817, 192, 310, 105}, 78, ui::lime,
         true);
  p.text(tr(L"kørende processer"), {823, 295, 300, 25}, 15, ui::white);
  p.line(821, 340, 1145, 340, ui::line);
  p.text(std::to_wstring(ramLoad) + L"%", {822, 358, 150, 36}, 25, ui::white, true);
  p.text(std::to_wstring(threadCount), {998, 358, 148, 36}, 25, ui::white, true);
  p.text(tr(L"RAM i brug / ") + std::to_wstring(totalRam) + L" GB", {823, 398, 160, 20}, 11,
         ui::muted);
  p.text(tr(L"aktive tråde"), {1000, 398, 148, 20}, 11, ui::muted);
  panel(p, {44, 480, 716, 266});
  p.text(tr(L"Vælg din retning"), {66, 500, 640, 32}, 19, ui::white, true);
  const std::wstring names[] = {tr(L"Gaming"), tr(L"Input"), tr(L"Fokus"), tr(L"Minimal desktop")};
  const std::wstring descriptions[] = {tr(L"Game Mode og optagelse"), tr(L"Mus og tastaturgenveje"),
                                       tr(L"Browser-baggrundsaktivitet"),
                                       tr(L"Mindre bevægelse på skrivebordet")};
  const int ids[] = {420, 421, 423, 422};
  for (int i = 0; i < 4; ++i) {
    float y = 544 + i * 47.f;
    hit(ids[i], {60, y, 684, 43}, !job.valid());
    float h = hover(ids[i]);
    p.round({60, y, 684, 43}, ui::white, 8, h * .035f);
    p.text(L"0" + std::to_wstring(i + 1), {70, y + 10, 32, 24}, 12, ui::lime, true);
    p.text(names[i], {112, y + 7, 190, 26}, 16, ui::white, true);
    p.text(descriptions[i], {304, y + 11, 372, 22}, 12, ui::muted);
    p.text(L"↗", {699, y + 7, 32, 28}, 20, ui::lime);
    if (i < 3)
      p.line(112, y + 46, 724, y + 46, ui::line, 1, .55f);
  }
  panel(p, {794, 480, 378, 266});
  p.text(tr(L"Din maskine"), {820, 502, 324, 32}, 19, ui::white, true);
  p.text(hardware, {820, 550, 326, 154}, 13, ui::muted);
  pill(p, tr(L"LOKALT / INGEN KONTO"), {820, 710, 212, 22}, ui::purple);
}
void drawSettings(ui::Painter &p) {
  sectionTitle(p, tr(L"INDIVIDUEL KONTROL"), tr(L"Finjustér din PC."),
               tr(L"Vælg en indstilling for at se effekt, begrænsninger og gendannelse."));
  float x = 44;
  for (size_t i = 0; i < categories.size(); ++i) {
    float w = std::max(61.f, p.textWidth(tr(categories[i]), 12) * 1.08f + 22);
    ui::Rect r{x, 244, w, 30};
    hit(400 + static_cast<int>(i), r);
    p.round(r, category == static_cast<int>(i) ? ui::white : ui::panel, 8,
            category == static_cast<int>(i) ? .095f : 1);
    p.text(tr(categories[i]), {x + 9, 251, w - 18, 21}, 12,
           category == static_cast<int>(i) ? ui::white : ui::muted, true);
    x += w + 6;
  }
  input(p, 300, {824, 242, 348, 36}, search, tr(L"Søg efter en indstilling…"));
  if (filtered.empty())
    p.text(tr(L"Ingen match. Prøv et andet søgeord."), {68, 342, 680, 90}, 20, ui::muted);
  float position = static_cast<float>(listPosition.value(clockNow()));
  size_t first = static_cast<size_t>(std::max(0.f, std::floor(position / 76)));
  p.clip({44, 298, 754, 448});
  hitClipping = true;
  clipHits = {44, 298, 754, 448};
  for (size_t i = first; i < filtered.size() && i < first + 7; ++i)
    featureRow(p, filtered[i], 298 + static_cast<float>(i) * 76 - position);
  hitClipping = false;
  p.unclip();
  if (!filtered.empty())
    p.text(std::to_wstring(featureOffset + 1) + L"–" +
               std::to_wstring(std::min(filtered.size(), featureOffset + 6)) + tr(L" af ") +
               std::to_wstring(filtered.size()) + tr(L" indstillinger"),
           {44, 761, 470, 22}, 11, ui::muted);
  button(p, 601, {590, 752, 116, 31}, tr(L"Forrige"), false, featureOffset > 0);
  button(p, 602, {714, 752, 84, 31}, tr(L"Næste"), false, featureOffset + 6 < filtered.size());
  panel(p, {824, 298, 348, 440});
  if (filtered.empty())
    return;
  auto index = static_cast<size_t>(selectedFeature);
  auto &f = tune::features()[index];
  auto &s = states[index];
  float a = static_cast<float>(inspector.value(clockNow()));
  p.text(tr(f.category), {848, 321, 300, 22}, 11, ui::purple, true);
  p.text(tr(f.title), {848, 353, 300, 66}, 25, ui::white, true, DWRITE_TEXT_ALIGNMENT_LEADING, a);
  p.text(tr(f.description), {848, 429, 300, 92}, 15, ui::muted, false,
         DWRITE_TEXT_ALIGNMENT_LEADING, a);
  pill(p,
       s.available ? (s.active ? tr(L"ØNSKET VÆRDI ER VALGT") : tr(L"KLAR TIL TILPASNING"))
                   : tr(L"IKKE TILGÆNGELIG"),
       {848, 532, 300, 25}, s.available ? (s.active ? ui::lime : ui::muted) : ui::red);
  p.text(s.backup   ? tr(L"Din originalværdi er gemt.")
         : s.active ? tr(L"Værdien var allerede valgt i Windows.")
                    : tr(L"Originalværdien gemmes før ændring."),
         {848, 570, 300, 48}, 12, ui::muted);
  button(p, 3000 + selectedFeature * 3, {848, 635, 142, 38},
         s.active ? tr(L"Allerede valgt") : tr(L"Anvend"), true,
         !job.valid() && s.available && !s.active);
  button(p, 3001 + selectedFeature * 3, {998, 635, 150, 38}, tr(L"Gendan"), false,
         !job.valid() && s.backup);
  button(p, 3002 + selectedFeature * 3, {848, 688, 300, 31}, tr(L"Læs hele forklaringen"));
}
void focusTabs(ui::Painter &p) {
  const std::wstring names[] = {tr(L"Spilprofil"), tr(L"Kørende apps"), tr(L"Opstart"),
                                tr(L"Windows-apps")};
  for (int i = 0; i < 4; ++i) {
    ui::Rect r{44 + i * 156.f, 244, 148, 34};
    hit(450 + i, r);
    p.round(r, focusTab == i ? ui::raised : ui::panel, 9);
    p.text(names[i], {r.x + 12, r.y + 8, r.w - 24, 22}, 13, focusTab == i ? ui::lime : ui::muted,
           true);
  }
}
void drawBloat(ui::Painter &p) {
  panel(p, {44, 304, 754, 434});
  panel(p, {824, 304, 348, 434});
  p.text(tr(L"Valgfrie Windows-apps"), {68, 327, 530, 30}, 21, ui::white, true);
  button(p, 550, {654, 323, 120, 34}, inventoryJob.valid() ? tr(L"Henter…") : tr(L"Opdater"), false,
         !job.valid() && !inventoryJob.valid());
  if (inventoryJob.valid())
    p.text(tr(L"Windows læser listen. Ingen apps ændres ved scanning."), {68, 400, 680, 100}, 18,
           ui::muted);
  else if (!inventory.error.empty())
    p.text(tr(inventory.error), {68, 400, 680, 200}, 15, ui::muted);
  else if (inventory.apps.empty())
    p.text(
        tr(L"Ingen af de ni kendte valgfrie apps blev fundet. Dette er ikke en komplet liste over "
           L"alle installerede programmer."),
        {68, 400, 680, 145}, 17, ui::muted);
  else {
    float position = static_cast<float>(focusPosition.value(clockNow()));
    size_t first = static_cast<size_t>(std::max(0.f, std::floor(position / 59)));
    p.clip({60, 386, 722, 295});
    hitClipping = true;
    clipHits = {60, 386, 722, 295};
    for (size_t i = first; i < inventory.apps.size() && i < first + 6; ++i) {
      const auto &app = inventory.apps[i];
      float y = 386 + static_cast<float>(i) * 59 - position;
      bool chosen = static_cast<int>(i) == bloatSelection;
      hit(60000 + static_cast<int>(i), {60, y, 722, 53});
      p.round({60, y, 722, 53}, chosen ? 0x272d35 : ui::panel, 10);
      if (chosen)
        p.border({60, y, 722, 53}, ui::purple, 10, .45f);
      p.text(tr(app.title), {82, y + 6, 666, 24}, 15, ui::white, true);
      p.text(app.installed ? tr(L"Installeret for din bruger") : tr(L"Fjernet · kan søges i Store"),
             {82, y + 30, 666, 18}, 11, app.installed ? ui::muted : ui::lime);
    }
    hitClipping = false;
    p.unclip();
    if (inventory.apps.size() > 5) {
      button(p, 701, {552, 691, 104, 31}, tr(L"Forrige"), false, bloatOffset > 0);
      button(p, 702, {664, 691, 110, 31}, tr(L"Næste"), false,
             bloatOffset + 5 < inventory.apps.size());
    }
  }
  bool selected = bloatSelection >= 0 &&
                  static_cast<size_t>(bloatSelection) < inventory.apps.size() &&
                  inventory.error.empty();
  p.text(tr(L"Oprydning med omtanke"), {848, 329, 300, 70}, 24, ui::white, true);
  p.text(selected ? tr(inventory.apps[static_cast<size_t>(bloatSelection)].impact)
                  : tr(L"Kun udvalgte, valgfrie apps vises. Store, sikkerhed, frameworks og "
                       L"gaming-tjenester fjernes ikke."),
         {848, 414, 300, 96}, 14, ui::muted);
  p.text(tr(L"Afinstallation kan slette appdata. Gendan alle kan ikke genskabe apps. Mindre "
            L"diskforbrug er ikke i sig selv lavere latency."),
         {848, 520, 300, 96}, 13, ui::red);
  button(p, 551, {848, 634, 300, 40}, tr(L"Afinstaller den valgte app"), false,
         selected && inventory.apps[static_cast<size_t>(std::max(0, bloatSelection))].installed &&
             !inventoryJob.valid() && !job.valid());
  button(p, 552, {848, 687, 300, 30}, tr(L"Find den valgte app i Store"), false, selected);
}
void drawFocus(ui::Painter &p) {
  sectionTitle(
      p, tr(L"DIN NÆSTE SESSION"), tr(L"Fokus, på dine præmisser."),
      tr(L"Et spil ad gangen. Et program ad gangen. Ingen blind lukning af Windows-processer."));
  focusTabs(p);
  if (focusTab == 3) {
    drawBloat(p);
    return;
  }
  if (focusTab == 0) {
    panel(p, {44, 304, 754, 434});
    p.text(tr(L"Vælg spillets rigtige programfil"), {68, 330, 706, 32}, 21, ui::white, true);
    p.text(game.empty() ? tr(L"Intet spil valgt endnu") : filename(game), {68, 376, 706, 44}, 15,
           game.empty() ? ui::muted : ui::lime);
    button(p, 410, {68, 434, 244, 42},
           game.empty() ? tr(L"Vælg en .exe") : tr(L"Vælg et andet spil"), true, !job.valid());
    p.text(tr(L"Ændringerne gælder kun denne fil. Genstart spillet bagefter."), {68, 499, 706, 44},
           14, ui::muted);
    int row = 0;
    for (size_t i = 0; i < tune::features().size(); ++i) {
      auto &f = tune::features()[i];
      if (f.kind != tune::Kind::Gpu && f.kind != tune::Kind::Fullscreen)
        continue;
      float y = 565 + row * 75.f;
      p.line(68, y - 12, 774, y - 12, ui::line);
      p.text(tr(f.title), {68, y, 550, 24}, 16, ui::white, true);
      p.text(tr(f.scope), {68, y + 28, 550, 22}, 12, ui::muted);
      switchControl(p, static_cast<int>(i), {704, y + 8, 48, 26});
      hit(3002 + static_cast<int>(i) * 3, {68, y, 600, 55});
      ++row;
    }
    panel(p, {824, 304, 348, 434});
    pill(p, tr(L"MÅL FØR / EFTER"), {848, 329, 298, 25}, ui::purple);
    p.text(tr(L"Præference.\nIkke et FPS-løfte."), {848, 380, 300, 92}, 29, ui::white, true);
    p.text(tr(L"GPU-valget er et ønske til Windows og driveren. Fullscreen-valget er en valgfri "
              L"A/B-test og kan forringe ydelse, VRR eller Alt-Tab."),
           {848, 497, 300, 132}, 15, ui::muted);
    button(p, 431, {848, 663, 300, 42}, tr(L"Åbn Windows-grafikindstillinger"));
    return;
  }
  panel(p, {44, 304, 754, 434});
  panel(p, {824, 304, 348, 434});
  bool apps = focusTab == 1;
  p.text(apps ? tr(L"Apps med hovedvinduer") : tr(L"Din brugers opstartsposter"),
         {68, 327, 530, 30}, 21, ui::white, true);
  button(p, apps ? 504 : 543, {654, 323, 120, 34}, tr(L"Opdater"));
  size_t count = apps ? processes.size() : startups.size(),
         offset = apps ? processOffset : startupOffset;
  float position = static_cast<float>(focusPosition.value(clockNow()));
  size_t first = static_cast<size_t>(std::max(0.f, std::floor(position / 59)));
  if (!count)
    p.text(apps ? tr(L"Ingen andre tilgængelige apps med hovedvinduer.")
                : tr(L"Ingen registrerede opstartsposter."),
           {68, 400, 680, 96}, 18, ui::muted);
  p.clip({60, 386, 722, 295});
  hitClipping = true;
  clipHits = {60, 386, 722, 295};
  for (size_t i = first; i < count && i < first + 6; ++i) {
    float y = 386 + static_cast<float>(i) * 59 - position;
    bool selected = static_cast<int>(i) == (apps ? processSelection : startupSelection);
    int id = (apps ? 20000 : 40000) + static_cast<int>(i);
    hit(id, {60, y, 722, 53});
    p.round({60, y, 722, 53}, selected ? 0x272d35 : ui::panel, 10);
    if (selected)
      p.border({60, y, 722, 53}, ui::purple, 10, .45f);
    auto label = apps ? processes[i].name : startups[i].name;
    auto detail = apps                  ? L"PID " + std::to_wstring(processes[i].pid)
                  : startups[i].present ? tr(L"Registreret Run-post")
                                        : tr(L"Fjernet · original gemt");
    p.circle(82, y + 26, 3, selected ? ui::lime : ui::muted);
    p.text(label, {100, y + 6, 650, 24}, 15, ui::white, true);
    p.text(detail, {100, y + 30, 650, 18}, 11, ui::muted);
  }
  hitClipping = false;
  p.unclip();
  if (count > 5) {
    button(p, 701, {552, 691, 104, 31}, tr(L"Forrige"), false, offset > 0);
    button(p, 702, {664, 691, 110, 31}, tr(L"Næste"), false, offset + 5 < count);
  }
  if (apps) {
    bool selected =
        processSelection >= 0 && static_cast<size_t>(processSelection) < processes.size();
    p.text(tr(L"Sessionkontrol"), {848, 329, 300, 30}, 21, ui::white, true);
    p.text(selected ? processes[static_cast<size_t>(processSelection)].name : tr(L"Vælg en app"),
           {848, 381, 300, 55}, 22, selected ? ui::lime : ui::muted, true);
    p.text(
        tr(L"Prioritet og EcoQoS gælder kun denne proces og gendannes ved normal applukning. Ingen "
           L"globale timer- eller driverændringer."),
        {848, 449, 300, 82}, 14, ui::muted);
    p.text(ecoStatus, {848, 531, 300, 36}, 11, ecoAvailable ? ui::purple : ui::muted);
    button(p, 502, {848, 575, 300, 36}, tr(L"Brug Above Normal"), true, selected && !job.valid());
    button(p, 506, {848, 621, 300, 36},
           ecoForced ? tr(L"Strømbegrænsning er fjernet") : tr(L"Fjern proces-strømbegrænsning"),
           false, selected && ecoAvailable && !ecoForced && !job.valid());
    button(p, 505, {848, 667, 144, 36}, tr(L"Bed appen lukke"), false, selected && !job.valid());
    button(p, 503, {1000, 667, 148, 36}, tr(L"Gendan session"), false,
           (!priorities.empty() || !powerOverrides.empty()) && !job.valid());

  } else {
    bool selected =
        startupSelection >= 0 && static_cast<size_t>(startupSelection) < startups.size();
    p.text(tr(L"Start mindre automatisk"), {848, 329, 300, 66}, 24, ui::white, true);
    p.text(
        tr(L"Fjerner kun den valgte Run-post. Programmet afinstalleres ikke. Windows-godkendelser, "
           L"tjenester og planlagte opgaver er uden for denne liste."),
        {848, 417, 300, 135}, 14, ui::muted);
    button(p, 541, {848, 575, 300, 40}, tr(L"Deaktivér opstart"), true,
           selected && startups[static_cast<size_t>(std::max(0, startupSelection))].present &&
               !job.valid());
    button(p, 542, {848, 626, 300, 40}, tr(L"Gendan opstart"), false,
           selected && startups[static_cast<size_t>(std::max(0, startupSelection))].backup &&
               !job.valid());
    button(p, 432, {848, 687, 300, 30}, tr(L"Alle opstartsapps i Windows"));
  }
}
void drawNetwork(ui::Painter &p) {
  sectionTitle(
      p, tr(L"NETVÆRK / FEJLFINDING"), tr(L"Se forbindelsen, som den er."),
      tr(L"Ping måler ICMP round-trip-tid. Det er ikke en måling af input-latency i dit spil."));
  panel(p, {44, 258, 720, 480});
  pill(p, tr(L"4 PAKKER / IPv4"), {70, 284, 166, 25}, ui::purple);
  p.text(tr(L"Hvor vil du måle hen?"), {70, 340, 650, 44}, 28, ui::white, true);
  input(p, 510, {70, 410, 420, 46}, address, tr(L"Fx 1.1.1.1"));
  button(p, 511, {506, 410, 232, 46}, pingJob.valid() ? tr(L"Måler…") : tr(L"Mål forbindelsen"),
         true, !pingJob.valid());
  p.line(70, 490, 738, 490, ui::line);
  p.text(tr(pingResult), {70, 522, 668, 170}, 16,
         pingResult.rfind(L"0/4", 0) == 0 ? ui::red : ui::muted);
  panel(p, {790, 258, 382, 480});
  p.text(tr(L"Et rent DNS-opslag"), {816, 291, 330, 76}, 29, ui::white, true);
  p.text(
      tr(L"Ryd cache ved navneopslagsproblemer. Det ændrer ikke din DNS-server og garanterer ikke "
         L"bedre ping."),
      {816, 392, 330, 100}, 16, ui::muted);
  p.text(tr(L"Cacheposter kan ikke gendannes. Windows opbygger automatisk en ny cache ved "
            L"efterfølgende opslag."),
         {816, 518, 330, 80}, 13, ui::muted);
  button(p, 512, {816, 628, 330, 42}, tr(L"Ryd DNS-cache"), false, !job.valid());
  button(p, 513, {816, 684, 330, 30}, tr(L"Åbn Windows-netværksindstillinger"));
}
void drawRestore(ui::Painter &p) {
  sectionTitle(p, tr(L"HISTORIK / GENDANNELSE"), tr(L"Du har altid overblikket."),
               tr(L"Originale indstillinger gemmes lokalt — også for tidligere valgte spil og "
                  L"opstartsposter."));
  panel(p, {44, 258, 724, 480});
  p.text(tr(L"Seneste aktivitet"), {70, 285, 668, 32}, 22, ui::white, true);
  p.line(70, 337, 742, 337, ui::line);
  p.clip({70, 359, 668, 350});
  p.text(activity.empty() ? tr(L"Din historik begynder, når du foretager en ændring.") : activity,
         {70, 359, 668, 350}, 13, ui::muted);
  p.unclip();
  panel(p, {794, 258, 378, 480});
  p.text(std::to_wstring(backups), {818, 291, 328, 90}, 68, ui::purple, true);
  p.text(tr(L"gemte originale tilstande"), {822, 390, 324, 30}, 16, ui::white, true);
  p.text(tr(L"Gendanner også gamle GameTune-backups og midlertidige sessionsvalg. Luk dine spil "
            L"først."),
         {822, 454, 324, 104}, 15, ui::muted);
  button(p, 520, {820, 580, 326, 44}, tr(L"Gendan alle ændringer"), true,
         !job.valid() && (backups > 0 || !priorities.empty() || !powerOverrides.empty()));
  button(p, 521, {820, 637, 326, 40}, tr(L"Eksportér rapport"));
  button(p, 522, {820, 692, 326, 28}, tr(L"Opdater status"), false, !job.valid());
}
void drawContent(ui::Painter &p) {
  switch (page) {
  case 0:
    drawDashboard(p);
    break;
  case 1:
    drawSettings(p);
    break;
  case 2:
    drawFocus(p);
    break;
  case 3:
    drawNetwork(p);
    break;
  case 4:
    drawRestore(p);
    break;
  }
}
void drawDialog(ui::Painter &p) {
  double amount = dialog.opacity.value(clockNow());
  if (amount <= .001 && !dialog.open)
    return;
  p.rect({0, 0, ui::width, ui::height}, 0x030407, static_cast<float>(amount * .78));
  float y = 174 + static_cast<float>(12 * (1 - amount));
  p.opacity({310, y - 15, 600, 510}, static_cast<float>(amount));
  ui::Rect r{328, y, 560, 472};
  p.round({r.x - 10, r.y + 6, r.w + 20, r.h + 18}, 0x000000, 25, static_cast<float>(amount * .25));
  p.gradient(r, 0x1d222b, 0x12161d, 22);
  p.border(r, 0x434b5a, 22, static_cast<float>(amount * .7));
  unsigned accent = dialog.danger ? ui::red : ui::lime;
  p.circle(366, y + 39, 12, accent, .10f);
  p.circle(366, y + 39, 3, accent);
  p.text(dialog.information ? tr(L"DETALJER / MAXIFYING") : tr(L"BEKRÆFT ÆNDRING"),
         {390, y + 31, 450, 20}, 11, accent, true);
  p.text(dialog.title, {360, y + 79, 496, 80}, 29, ui::white, true);
  p.clip({360, y + 174, 496, 196});
  p.text(dialog.body,
         {360, y + 174 - static_cast<float>(dialog.scroll.value(clockNow())), 496, 8000}, 15,
         ui::muted);
  p.unclip();
  dialog.maximum = std::max(0.f, p.textHeight(dialog.body, 496, 15) - 196);
  if (dialog.maximum > 0)
    p.text(tr(L"Rul for at læse resten"), {360, y + 376, 496, 20}, 11, ui::purple);
  if (dialog.languagePicker) {
    const std::wstring names[] = {L"Dansk", L"English", L"Svenska"};
    for (int i = 0; i < 3; ++i)
      button(p, 9100 + i, {360, y + 250 + i * 47.f, 496, 38}, names[i],
             static_cast<int>(l10n::selected.load()) == i, dialog.open);
    button(p, 9002, {360, y + 412, 496, 38}, tr(L"Annuller"), false, dialog.open);
  } else if (dialog.information)
    button(p, 9001, {360, y + 412, 496, 38}, dialog.primary, true, dialog.open, dialog.danger);
  else {
    button(p, 9002, {360, y + 412, 160, 38}, tr(L"Annuller"), false, dialog.open);
    button(p, 9001, {532, y + 412, 324, 38}, dialog.primary, true, dialog.open, dialog.danger);
  }
  p.endOpacity();
}
void drawFrame(ui::Painter &p) {
  p.rect({0, 0, ui::width, 98}, ui::background);
  p.line(44, 96, 1172, 96, ui::line, 1, .7f);
  p.icon({42, 25, 36, 36});
  p.text(L"MAXIFYING", {87, 30, 210, 27}, 20, ui::white, true);
  float x = static_cast<float>(nav.value(clockNow()));
  p.round({x, 28, 118, 35}, ui::white, 10, .055f);
  for (size_t i = 0; i < pages.size(); ++i) {
    ui::Rect r{330 + static_cast<float>(i) * 126, 28, 118, 35};
    hit(100 + static_cast<int>(i), r);
    p.text(tr(pages[i]), {r.x + 10, r.y + 8, r.w - 20, 22}, 13,
           page == static_cast<int>(i) ? ui::white : ui::muted, true, DWRITE_TEXT_ALIGNMENT_CENTER);
  }
  button(p, 310, {986, 29, 92, 32}, L"Ctrl K");
  hit(1, {1100, 26, 32, 35});
  hit(2, {1142, 26, 32, 35});
  p.round({1100, 26, 32, 35}, ui::white, 8, hover(1) * .06f);
  p.round({1142, 26, 32, 35}, ui::red, 8, hover(2) * .1f);
  p.line(1110, 44, 1122, 44, ui::muted, 1.3f);
  p.line(1153, 38, 1164, 49, hovered == 2 ? ui::red : ui::muted, 1.3f);
  p.line(1164, 38, 1153, 49, hovered == 2 ? ui::red : ui::muted, 1.3f);
  p.rect({0, 788, ui::width, 44}, ui::background);
  p.line(44, 788, 1172, 788, ui::line, 1, .55f);
  unsigned tone = statusTone == 2   ? ui::lime
                  : statusTone == 3 ? ui::red
                  : statusTone == 1 ? ui::purple
                                    : ui::muted;
  float revealed = static_cast<float>(notice.value(clockNow()));
  p.circle(48, 810, 3, tone, revealed);
  p.text(tr(status), {62, 801 + 3 * (1 - revealed), 790, 24}, 12, tone, false,
         DWRITE_TEXT_ALIGNMENT_LEADING, revealed);
  button(p, 311, {870, 798, 126, 27},
         l10n::selected == l10n::Language::Danish    ? L"Sprog · DA"
         : l10n::selected == l10n::Language::Swedish ? L"Språk · SV"
                                                     : L"Language");
  p.text(L"MAXIFYING / 0.6", {1020, 802, 152, 22}, 11, ui::muted, true,
         DWRITE_TEXT_ALIGNMENT_TRAILING);
  if (keyboardMode && !dialog.open) {
    auto focus = std::find_if(hits.begin(), hits.end(),
                              [](const Hit &h) { return h.id == keyboardFocus && h.enabled; });
    bool custom = (keyboardFocus >= 100 && keyboardFocus < 105) ||
                  (keyboardFocus >= 400 && keyboardFocus < 407) ||
                  (keyboardFocus >= 450 && keyboardFocus < 454) ||
                  (keyboardFocus >= 2000 && keyboardFocus < 2023) || keyboardFocus >= 20000;
    if (focus != hits.end() && custom) {
      auto r = focus->rect;
      p.border({r.x - 2, r.y - 2, r.w + 4, r.h + 4}, ui::purple, 11, .8f, 1.5f);
    }
  }
  drawDialog(p);
}
std::wstring selectFile(bool save) {
  IFileDialog *dialogFile = nullptr;
  HRESULT result = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialogFile));
  if (FAILED(result))
    throw std::runtime_error("Windows file picker is unavailable");
  const std::wstring filterLabel = save ? tr(L"Tekstfil") : tr(L"Windows-program");
  const COMDLG_FILTERSPEC filter{filterLabel.c_str(), save ? L"*.txt" : L"*.exe"};
  dialogFile->SetFileTypes(1, &filter);
  const auto title = save ? tr(L"Eksportér Maxifying-rapport") : tr(L"Vælg spillets rigtige .exe");
  dialogFile->SetTitle(title.c_str());
  DWORD options = 0;
  dialogFile->GetOptions(&options);
  dialogFile->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR |
                         (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
  if (save) {
    dialogFile->SetDefaultExtension(L"txt");
    dialogFile->SetFileName(L"Maxifying-rapport.txt");
  }
  result = dialogFile->Show(window);
  std::wstring path;
  if (SUCCEEDED(result)) {
    IShellItem *item = nullptr;
    if (SUCCEEDED(dialogFile->GetResult(&item))) {
      PWSTR value = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) {
        path = value;
        CoTaskMemFree(value);
      }
      item->Release();
    }
  }
  dialogFile->Release();
  if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED))
    throw std::runtime_error("Windows file picker failed");
  return path;
}
void chooseGame() {
  auto selected = selectFile(false);
  if (selected.empty())
    return;
  auto attributes = GetFileAttributesW(selected.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) ||
      selected.size() < 4 || _wcsicmp(selected.c_str() + selected.size() - 4, L".exe"))
    throw std::runtime_error("Choose an existing .exe file");
  game = selected;
  tune::rememberGame(game);
  refreshStates();
  setStatus(tr(L"Valgt spil: ") + filename(game), 2);
}
void exportReport() {
  auto path = selectFile(true);
  if (path.empty())
    return;
  std::wstring report = L"Maxifying 0.6\r\n\r\n" + hardware + L"\r\n\r\n";
  for (size_t i = 0; i < states.size(); ++i)
    report += tr(tune::features()[i].title) + L": " +
              (states[i].available
                   ? (states[i].active ? tr(L"Ønsket værdi valgt") : tr(L"Ikke tilpasset"))
                   : states[i].error) +
              L"\r\n";
  report += L"\r\nAktivitet:\r\n" + activity;
  int bytes = WideCharToMultiByte(CP_UTF8, 0, report.data(), static_cast<int>(report.size()),
                                  nullptr, 0, nullptr, nullptr);
  std::string utf8(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, report.data(), static_cast<int>(report.size()), utf8.data(),
                      bytes, nullptr, nullptr);
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Cannot save report");
  DWORD written = 0;
  bool ok = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) &&
            written == utf8.size();
  CloseHandle(file);
  if (!ok)
    throw std::runtime_error("Report write failed");
  setStatus(tr(L"Rapport gemt. Den indeholder hardwareoplysninger og lokal aktivitetshistorik."),
            2);
}
void openSettings(const wchar_t *uri) {
  if (reinterpret_cast<INT_PTR>(
          ShellExecuteW(window, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
    throw std::runtime_error("Windows Settings could not be opened");
}
void flushDns() {
  wchar_t directory[MAX_PATH];
  if (!GetSystemDirectoryW(directory, MAX_PATH))
    throw std::runtime_error("Cannot locate Windows system directory");
  auto executable = std::wstring(directory) + L"\\ipconfig.exe",
       arguments = L"\"" + executable + L"\" /flushdns";
  STARTUPINFOW start{};
  start.cb = sizeof start;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable.c_str(), arguments.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &start, &process))
    throw std::runtime_error("Cannot run ipconfig");
  CloseHandle(process.hThread);
  DWORD wait = WaitForSingleObject(process.hProcess, 5000), code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hProcess);
  if (wait != WAIT_OBJECT_0 || code)
    throw std::runtime_error(
        "DNS flush did not complete; Windows may require administrator rights");
  tune::log(tr(L"DNS-cache ryddet"));
}
std::wstring ping(const std::wstring &ip) {
  IN_ADDR destination{};
  if (InetPtonW(AF_INET, ip.c_str(), &destination) != 1)
    return tr(L"Ugyldig IPv4-adresse.");
  HANDLE handle = IcmpCreateFile();
  if (handle == INVALID_HANDLE_VALUE)
    return tr(L"ICMP er ikke tilgængeligt på denne maskine.");
  const char data[] = "Maxifying connection test";
  std::vector<BYTE> buffer(sizeof(ICMP_ECHO_REPLY) + sizeof data + 32);
  unsigned received = 0;
  DWORD total = 0, minimum = MAXDWORD, maximum = 0;
  for (int i = 0; i < 4; ++i) {
    DWORD result =
        IcmpSendEcho(handle, destination.S_un.S_addr, const_cast<char *>(data), sizeof data,
                     nullptr, buffer.data(), static_cast<DWORD>(buffer.size()), 700);
    if (result) {
      auto reply = reinterpret_cast<ICMP_ECHO_REPLY *>(buffer.data());
      if (reply->Status == IP_SUCCESS) {
        ++received;
        total += reply->RoundTripTime;
        minimum = std::min(minimum, reply->RoundTripTime);
        maximum = std::max(maximum, reply->RoundTripTime);
      }
    }
  }
  IcmpCloseHandle(handle);
  if (!received)
    return tr(L"0/4 svar. ICMP kan være blokeret; det beviser ikke, at internet eller "
              L"spilforbindelsen er nede.");
  return ip + L" · " + std::to_wstring(received) + tr(L"/4 svar · ") +
         std::to_wstring((4 - received) * 25) + tr(L"% pakketab\n\nMin / gennemsnit / maks\n") +
         std::to_wstring(minimum) + L" / " + std::to_wstring(total / received) + L" / " +
         std::to_wstring(maximum) + tr(L" ms\n\nSpillets egen server kan have en anden RTT.");
}
void boostProcess() {
  if (processSelection < 0 || static_cast<size_t>(processSelection) >= processes.size())
    throw std::runtime_error("Select a running application first");
  auto &selected = processes[static_cast<size_t>(processSelection)];
  if (WaitForSingleObject(selected.handle, 0) != WAIT_TIMEOUT)
    throw std::runtime_error("The selected application has exited");
  for (auto &p : priorities)
    if (WaitForSingleObject(p.handle, 0) == WAIT_TIMEOUT &&
        GetProcessId(p.handle) == selected.pid) {
      setStatus(tr(L"Der er allerede gemt en prioritet for denne app."));
      return;
    }
  DWORD original = GetPriorityClass(selected.handle);
  if (!original)
    throw std::runtime_error("Cannot read process priority");
  if (original != NORMAL_PRIORITY_CLASS && original != BELOW_NORMAL_PRIORITY_CLASS &&
      original != IDLE_PRIORITY_CLASS)
    throw std::runtime_error("The process already has Above Normal or higher priority");
  HANDLE duplicate = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), selected.handle, GetCurrentProcess(), &duplicate, 0,
                       FALSE, DUPLICATE_SAME_ACCESS))
    throw std::runtime_error("Cannot retain process handle");
  if (!SetPriorityClass(duplicate, ABOVE_NORMAL_PRIORITY_CLASS)) {
    CloseHandle(duplicate);
    throw std::runtime_error("Windows rejected the priority change");
  }
  priorities.push_back({duplicate, original});
  tune::log(tr(L"Midlertidig Above Normal: ") + selected.name);
  setStatus(tr(L"Above Normal anvendt. Gendannes ved normal applukning eller manuelt."), 2);
}
void requestAppClose() {
  if (processSelection < 0 || static_cast<size_t>(processSelection) >= processes.size())
    throw std::runtime_error("Select an application first");
  auto selected = processes[static_cast<size_t>(processSelection)];
  showDialog(
      tr(L"Luk ") + selected.name + L"?",
      tr(L"Appen bliver bedt om at lukke normalt. Den kan spørge om at gemme arbejde eller blive i "
         L"systembakken.\n\nIngen proces bliver tvangsafsluttet."),
      tr(L"Send lukkeanmodning"),
      [selected] {
        if (WaitForSingleObject(selected.handle, 0) != WAIT_TIMEOUT)
          throw std::runtime_error("The selected app has already exited");
        struct Request {
          DWORD pid;
          HANDLE handle;
          unsigned sent = 0;
        } request{selected.pid, selected.handle};
        EnumWindows(
            [](HWND w, LPARAM data) -> BOOL {
              auto &r = *reinterpret_cast<Request *>(data);
              DWORD pid = 0;
              GetWindowThreadProcessId(w, &pid);
              if (pid == r.pid && WaitForSingleObject(r.handle, 0) == WAIT_TIMEOUT &&
                  IsWindowVisible(w) && GetWindow(w, GW_OWNER) == nullptr &&
                  PostMessageW(w, WM_CLOSE, 0, 0))
                ++r.sent;
              return TRUE;
            },
            reinterpret_cast<LPARAM>(&request));
        if (!request.sent)
          throw std::runtime_error(
              "No closable main window was found. Use the app's tray menu or startup controls.");
        setStatus(tr(L"Lukkeanmodning sendt. Appen kan bede om at gemme eller blive i bakken."));
      },
      true);
}
void languageMenu() {
  showDialog(tr(L"Vælg sprog"), tr(L"Skift sprog med det samme. Dit valg gemmes på denne PC."),
             tr(L"Forstået"), {}, false, true);
  dialog.languagePicker = true;
  keyboardFocus = 9100 + static_cast<int>(l10n::selected.load());
}
void chooseLanguage(l10n::Language language) {
  const DWORD value = static_cast<DWORD>(language);
  if (RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Maxifying\\UI", L"Language", REG_DWORD, &value,
                      sizeof value) != ERROR_SUCCESS) {
    errorDialog(tr(L"Kunne ikke gemme sprogvalget. Prøv igen."));
    return;
  }
  l10n::selected = language;
  updateEco();
  refreshHardware();
  dismissDialog(false);
  filterFeatures();
  featureOffset = 0;
  listPosition.snap(0);
  setStatus(tr(L"Klar til din næste session."));
  invalidate();
}
void activate(int id) {
  if (id == 311) {
    languageMenu();
    return;
  }
  if (id >= 9100 && id <= 9102) {
    chooseLanguage(static_cast<l10n::Language>(id - 9100));
    return;
  }
  if (id == 9001) {
    if (dialog.open)
      dismissDialog(true);
    return;
  }
  if (id == 9002) {
    if (dialog.open)
      dismissDialog(false);
    return;
  }
  if (id == 1) {
    ShowWindow(window, SW_MINIMIZE);
    return;
  }
  if (id == 2) {
    PostMessageW(window, WM_CLOSE, 0, 0);
    return;
  }
  if (id >= 100 && id < 105) {
    navigate(id - 100);
    return;
  }
  if (id == 310) {
    navigate(1);
    inputFocus = 300;
    keyboardFocus = 300;
    cursor = search.size();
    anchor = 0;
    invalidate();
    return;
  }
  if (id >= 400 && id < 407) {
    category = id - 400;
    featureOffset = 0;
    listPosition.snap(0);
    filterFeatures();
    inspector.snap(0);
    inspector.target(1, clockNow(), 170);
    invalidate();
    animate();
    return;
  }
  if (id >= 450 && id < 454) {
    switchFocusTab(id - 450);
    return;
  }
  if (id >= 2000 && id < 2000 + static_cast<int>(states.size())) {
    selectedFeature = id - 2000;
    inspector.snap(0);
    inspector.target(1, clockNow(), 180);
    invalidate();
    animate();
    return;
  }
  if (id >= 3000 && id < 3000 + static_cast<int>(states.size()) * 3) {
    size_t index = static_cast<size_t>((id - 3000) / 3);
    int action = (id - 3000) % 3;
    if (action == 2) {
      showDialog(tr(tune::features()[index].title), tr(tune::features()[index].details),
                 tr(L"Forstået"), {}, false, true);
      return;
    }
    if (!job.valid())
      changeFeature(index, action == 1);
    return;
  }
  if (id >= 1000 && id < 1000 + static_cast<int>(states.size())) {
    size_t index = static_cast<size_t>(id - 1000);
    changeFeature(index, states[index].active);
    return;
  }
  if (id >= 20000 && id < 20000 + static_cast<int>(processes.size())) {
    processSelection = id - 20000;
    updateEco();
    invalidate();
    return;
  }
  if (id >= 40000 && id < 40000 + static_cast<int>(startups.size())) {
    startupSelection = id - 40000;
    invalidate();
    return;
  }
  if (id >= 60000 && id < 60000 + static_cast<int>(inventory.apps.size())) {
    bloatSelection = id - 60000;
    invalidate();
    return;
  }
  if (id == 601) {
    featureOffset = motion::pageOffset(featureOffset, false, filtered.size(), 6);
    listPosition.target(static_cast<double>(featureOffset) * 76, clockNow(), 220);
    invalidate();
    animate();
    return;
  }
  if (id == 602) {
    featureOffset = motion::pageOffset(featureOffset, true, filtered.size(), 6);
    listPosition.target(static_cast<double>(featureOffset) * 76, clockNow(), 220);
    invalidate();
    animate();
    return;
  }
  if (id == 701 || id == 702) {
    auto &offset = focusTab == 1 ? processOffset : focusTab == 2 ? startupOffset : bloatOffset;
    const size_t count = focusTab == 1   ? processes.size()
                         : focusTab == 2 ? startups.size()
                                         : inventory.apps.size();
    offset = motion::pageOffset(offset, id == 702, count, 5);
    focusPosition.target(static_cast<double>(offset) * 59, clockNow(), 220);
    invalidate();
    animate();
    return;
  }
  if (id == 431) {
    openSettings(L"ms-settings:display-advancedgraphics");
    return;
  }
  if (id == 432) {
    openSettings(L"ms-settings:startupapps");
    return;
  }
  if (id == 513) {
    openSettings(L"ms-settings:network-status");
    return;
  }
  if (id == 521) {
    exportReport();
    return;
  }
  if (id == 511) {
    if (pingJob.valid())
      return;
    IN_ADDR parsed{};
    if (InetPtonW(AF_INET, address.c_str(), &parsed) != 1) {
      showDialog(tr(L"Indtast en IPv4-adresse"),
                 tr(L"Brug fire tal adskilt af punktummer. For eksempel 1.1.1.1 eller 8.8.8.8."),
                 tr(L"Forstået"), {}, false, true);
      return;
    }
    auto ip = address;
    pingResult = tr(L"Sender fire pakker. Målingen vises her, når den er klar.");
    pingJob = std::async(std::launch::async, [ip] { return ping(ip); });
    invalidate();
    animate();
    return;
  }
  if (job.valid()) {
    setStatus(tr(L"Den aktuelle ændring afsluttes først."), 1);
    return;
  }
  switch (id) {
  case 420:
    startProfile(0);
    break;
  case 421:
    startProfile(1);
    break;
  case 423:
    startProfile(2);
    break;
  case 422:
    startProfile(3);
    break;
  case 410:
    chooseGame();
    break;
  case 502:
    boostProcess();
    break;
  case 503:
    restorePriorities();
    setStatus(tr(L"Sessionsvalg er gendannet."), 2);
    break;
  case 504:
    loadProcesses();
    setStatus(tr(L"Listen over apps er opdateret."));
    break;
  case 505:
    requestAppClose();
    break;
  case 506:
    showDialog(tr(L"Fjern strømbegrænsning fra denne proces?"),
               tr(L"Slår execution-speed throttling (EcoQoS) fra for netop den valgte app. Andre "
                  L"strømflag "
                  L"bevares. Det kan øge strømforbruget.\n\nDet ændrer ikke globale timere, HPET, "
                  L"CPU-affinity eller drivere. En lavere latency er ikke garanteret. Tilstanden "
                  L"gendannes "
                  L"ved normal applukning; ved crash varer den indtil spillet afsluttes."),
               tr(L"Anvend for denne session"), [] { disableEco(); });
    break;
  case 550:
    startInventory(true);
    break;
  case 551: {
    if (bloatSelection < 0 || static_cast<size_t>(bloatSelection) >= inventory.apps.size())
      return;
    auto selected = inventory.apps[static_cast<size_t>(bloatSelection)];
    if (!selected.installed)
      return;
    showDialog(
        tr(L"Afinstaller ") + selected.title + L"?",
        tr(L"Dette kan ikke fortrydes af Maxifying. Gendan alle geninstallerer ikke apps. Appdata "
           L"kan "
           L"blive slettet.\n\n") +
            tr(selected.impact) + tr(L"\n\nPakke: ") + selected.fullName +
            tr(L"\n\nKun denne brugers pakke fjernes. Windows Store, frameworks, Windows-sikkerhed "
               L"og "
               L"gaming-tjenester er ikke med i listen. Du kan normalt finde appen i Store igen, "
               L"men "
               L"lokale data genskabes ikke automatisk."),
        tr(L"Afinstaller kun for mig"),
        [selected] {
          pendingRemoval = selected;
          reloadInventoryAfterJob = true;
          beginJob(
              [selected] {
                auto error = bloat::remove(selected);
                if (error.empty())
                  tune::log(tr(L"Afinstalleret valgfri Windows-app: ") + selected.name +
                            tr(L". Geninstallation kræver Store."));
                return error;
              },
              tr(L"Windows-app afinstalleret for din bruger. Gendan alle geninstallerer den "
                 L"ikke."));
        },
        true);
    break;
  }
  case 552: {
    if (bloatSelection < 0 || static_cast<size_t>(bloatSelection) >= inventory.apps.size())
      return;
    const auto &app = inventory.apps[static_cast<size_t>(bloatSelection)];
    auto uri = L"ms-windows-store://pdp/?PFN=" + app.family;
    if (reinterpret_cast<INT_PTR>(
            ShellExecuteW(window, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
      throw std::runtime_error("Microsoft Store could not be opened");
    break;
  }
  case 512:
    showDialog(
        tr(L"Ryd DNS-cache?"),
        tr(L"Brug dette ved navneopslagsproblemer. Det ændrer ikke DNS-serveren og er ikke en "
           L"garanteret ping-forbedring.\n\nCacheposter kan ikke gendannes. Windows opbygger "
           L"automatisk en ny cache ved nye opslag."),
        tr(L"Ryd cache"), [] {
          beginJob(
              [] {
                flushDns();
                return std::wstring{};
              },
              tr(L"DNS-cache er ryddet."));
        });
    break;
  case 520:
    showDialog(
        tr(L"Gendan dine originale indstillinger"),
        tr(L"Gendanner alle gemte indstillinger, inklusive tidligere spilprofiler, opstartsposter "
           L"og "
           L"aktive procesprioriteter.\n\nLuk dine spil først. Ændringer i Windows kan kræve "
           L"genstart "
           L"af spil eller en ny login-session."),
        tr(L"Gendan alle"),
        [] {
          std::wstring prior;
          try {
            restorePriorities();
          } catch (const std::exception &e) {
            prior = tune::errorText(e);
          }
          beginJob(
              [prior] {
                auto error = prior;
                try {
                  tune::restoreAll();
                } catch (const std::exception &e) {
                  error += L"\n" + tune::errorText(e);
                }
                return error;
              },
              tr(L"Alle gemte indstillinger er gendannet."), -1, true);
        },
        true);
    break;
  case 522:
    refreshStates();
    refreshHardware();
    setStatus(tr(L"Status er læst fra Windows igen."));
    break;
  case 541:
  case 542: {
    if (startupSelection < 0 || static_cast<size_t>(startupSelection) >= startups.size())
      return;
    auto selected = startups[static_cast<size_t>(startupSelection)];
    auto action = [selected, id] {
      beginJob(
          [selected, id] {
            if (id == 541)
              tune::disableStartup(selected.name);
            else
              tune::restoreStartup(selected.name);
            return std::wstring{};
          },
          tr(L"Opstart opdateret: ") + selected.name, -1, true);
    };
    if (id == 541)
      showDialog(tr(L"Deaktivér ") + selected.name + tr(L" ved opstart?"),
                 tr(L"Programmet bliver ikke afinstalleret. Den registrerede Run-post fjernes, og "
                    L"dens originale data gemmes, så du kan gendanne den."),
                 tr(L"Deaktivér opstart"), action);
    else
      action();
    break;
  }
  case 543:
    loadStartups();
    setStatus(tr(L"Opstartslisten er opdateret."));
    break;
  }
  invalidate();
}
const Hit *hitAt(float x, float y) {
  bool modal = dialog.open || dialog.opacity.value(clockNow()) > .01;
  for (auto i = hits.rbegin(); i != hits.rend(); ++i) {
    if (!i->enabled || !i->rect.contains(x, y))
      continue;
    if (modal && i->id != 9001 && i->id != 9002 && !(i->id >= 9100 && i->id <= 9102))
      continue;
    if (!modal && !canvas.interactive(clockNow()) && i->rect.y >= 100 && i->rect.y < 788)
      continue;
    return &*i;
  }
  return nullptr;
}
void moveHover(int id) {
  if (id == hovered)
    return;
  double now = clockNow();
  if (hovered)
    hovers[hovered].target(0, now, 160);
  hovered = id;
  if (id)
    hovers[id].target(1, now, 120);
  invalidate();
  animate();
}
std::wstring &editValue() {
  return inputFocus == 510 ? address : search;
}
size_t previousPoint(const std::wstring &value, size_t at) {
  return ui_text::previous(value, at);
}
size_t nextPoint(const std::wstring &value, size_t at) {
  return ui_text::next(value, at);
}
void edited() {
  if (inputFocus == 300) {
    featureOffset = 0;
    listPosition.snap(0);
    filterFeatures();
  }
  invalidate();
}
void insertText(std::wstring text) {
  auto &value = editValue();
  size_t begin = std::min(std::min(cursor, anchor), value.size()),
         end = std::min(std::max(cursor, anchor), value.size());
  value.erase(begin, end - begin);
  text = ui_text::printable(text, 256 - value.size());
  value.insert(begin, text);
  cursor = anchor = begin + text.size();
  edited();
}
void copySelection(bool cut) {
  auto &value = editValue();
  size_t begin = std::min(cursor, anchor), end = std::max(cursor, anchor);
  if (begin == end || !OpenClipboard(window))
    return;
  auto text = value.substr(begin, end - begin);
  bool copied = false;
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
  if (memory) {
    auto destination = GlobalLock(memory);
    if (destination) {
      CopyMemory(destination, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
      GlobalUnlock(memory);
      EmptyClipboard();
      if (SetClipboardData(CF_UNICODETEXT, memory))
        copied = true;
      else
        GlobalFree(memory);
    } else
      GlobalFree(memory);
  }
  CloseClipboard();
  if (cut && copied)
    insertText(L"");
}
void paste() {
  if (!OpenClipboard(window))
    return;
  HANDLE memory = GetClipboardData(CF_UNICODETEXT);
  std::wstring text;
  if (memory) {
    auto data = static_cast<const wchar_t *>(GlobalLock(memory));
    if (data) {
      size_t limit = std::min(static_cast<size_t>(GlobalSize(memory) / sizeof(wchar_t)),
                              static_cast<size_t>(256));
      for (size_t i = 0; i < limit && data[i]; ++i)
        text += data[i];
      GlobalUnlock(memory);
    }
  }
  CloseClipboard();
  text = ui_text::printable(text, 256);
  if (!text.empty())
    insertText(text);
}
void tabFocus(bool reverse) {
  keyboardMode = true;
  pendingHigh = 0;
  std::vector<const Hit *> order;
  bool modal = dialog.open || dialog.opacity.value(clockNow()) > .01;
  for (auto &h : hits)
    if (h.enabled && h.id != 1 && h.id != 2 &&
        (!modal || (h.id == 9001 || h.id == 9002 || (h.id >= 9100 && h.id <= 9102))))
      order.push_back(&h);
  std::stable_sort(order.begin(), order.end(), [](const Hit *a, const Hit *b) {
    return a->rect.y == b->rect.y ? a->rect.x < b->rect.x : a->rect.y < b->rect.y;
  });
  if (order.empty())
    return;
  auto found =
      std::find_if(order.begin(), order.end(), [](const Hit *h) { return h->id == keyboardFocus; });
  int at = found == order.end() ? (reverse ? 0 : -1) : static_cast<int>(found - order.begin());
  at = (at + (reverse ? -1 : 1) + static_cast<int>(order.size())) % static_cast<int>(order.size());
  auto h = order[static_cast<size_t>(at)];
  keyboardFocus = h->id;
  inputFocus = h->field ? h->id : 0;
  if (inputFocus)
    cursor = anchor = editValue().size();
  invalidate();
}
void safeActivate(int id) {
  try {
    activate(id);
  } catch (const std::exception &error) {
    tune::log(tr(L"Fejl: ") + tune::errorText(error));
    setStatus(tr(L"Handlingen blev ikke gennemført."), 3);
    errorDialog(tune::errorText(error));
  }
}
void keyDown(WPARAM key) {
  keyboardMode = true;
  bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0,
       shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  if (key == VK_ESCAPE) {
    if (dialog.open)
      dismissDialog(false);
    else {
      inputFocus = 0;
      keyboardFocus = 0;
      invalidate();
    }
    return;
  }
  if (dialog.open) {
    if (key == VK_TAB)
      tabFocus(shift);
    else if (key == VK_RETURN || key == VK_SPACE)
      safeActivate(dialog.languagePicker ? keyboardFocus : (keyboardFocus == 9002 ? 9002 : 9001));
    return;
  }
  if (control && key == 'K') {
    safeActivate(310);
    return;
  }
  if (key == VK_TAB) {
    tabFocus(shift);
    return;
  }
  if (inputFocus) {
    auto &value = editValue();
    if (control && key == 'A') {
      anchor = 0;
      cursor = value.size();
      invalidate();
      return;
    }
    if (control && key == 'C') {
      copySelection(false);
      return;
    }
    if (control && key == 'X') {
      copySelection(true);
      return;
    }
    if (control && key == 'V') {
      paste();
      return;
    }
    if (key == VK_LEFT || key == VK_RIGHT || key == VK_HOME || key == VK_END) {
      if (key == VK_HOME)
        cursor = 0;
      else if (key == VK_END)
        cursor = value.size();
      else if (!shift && cursor != anchor)
        cursor = key == VK_LEFT ? std::min(cursor, anchor) : std::max(cursor, anchor);
      else
        cursor = key == VK_LEFT ? previousPoint(value, cursor) : nextPoint(value, cursor);
      if (!shift)
        anchor = cursor;
      invalidate();
      return;
    }
    if (key == VK_DELETE || key == VK_BACK) {
      if (cursor != anchor)
        insertText(L"");
      else if (key == VK_BACK && cursor) {
        size_t before = previousPoint(value, cursor);
        value.erase(before, cursor - before);
        cursor = anchor = before;
        edited();
      } else if (key == VK_DELETE && cursor < value.size()) {
        value.erase(cursor, nextPoint(value, cursor) - cursor);
        edited();
      }
      return;
    }
    if (key == VK_RETURN) {
      if (inputFocus == 510)
        safeActivate(511);
      else {
        inputFocus = 0;
        invalidate();
      }
      return;
    }
    return;
  }
  if (key == VK_RETURN || key == VK_SPACE) {
    auto found = std::find_if(hits.begin(), hits.end(),
                              [](const Hit &h) { return h.id == keyboardFocus && h.enabled; });
    if (found != hits.end())
      safeActivate(keyboardFocus);
  }
}
void paint() {
  PAINTSTRUCT info;
  BeginPaint(window, &info);
  hits.clear();
  auto result = canvas.draw(clockNow(), drawContent, drawFrame);
  EndPaint(window, &info);
  if (FAILED(result)) {
    std::ofstream log("maxifying-render-error.txt");
    log << "Direct2D HRESULT " << static_cast<unsigned long>(result) << "\n";
    invalidate();
    animate();
  }
}
bool moving() {
  double now = clockNow();
  if (canvas.moving(now) || nav.moving(now) || inspector.moving(now) || notice.moving(now) ||
      dialog.opacity.moving(now) || dialog.scroll.moving(now) || listPosition.moving(now) ||
      focusPosition.moving(now) || job.valid() || pingJob.valid())
    return true;
  for (auto &h : hovers)
    if (h.second.moving(now))
      return true;
  for (auto &t : toggles)
    if (t.second.moving(now))
      return true;
  return false;
}
void finishPing() {
  if (pingJob.valid() && pingJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    try {
      pingResult = pingJob.get();
    } catch (...) {
      pingResult = tr(L"Målingen kunne ikke gennemføres.");
    }
    setStatus(tr(L"Pingtesten er afsluttet."));
    invalidate();
  }
}
void requestClose() {
  if (dialog.open) {
    dismissDialog(false);
    return;
  }
  if (job.valid()) {
    closeRequested = true;
    setStatus(tr(L"Afslutter den aktuelle ændring og lukker derefter."), 1);
    return;
  }
  if (inventoryCancel)
    inventoryCancel->store(true);
  try {
    restorePriorities();
  } catch (const std::exception &error) {
    showDialog(
        tr(L"Nogle sessionsvalg kunne ikke gendannes"),
        tune::errorText(error) + tr(L"\n\nDe resterende ændringer varer indtil spilprocesserne "
                                    L"afsluttes. Vil du lukke alligevel?"),
        tr(L"Luk alligevel"), [] { DestroyWindow(window); }, true);
    return;
  }
  DestroyWindow(window);
}
void screenshot(const std::wstring &name) {
  UpdateWindow(window);
  RECT r;
  GetClientRect(window, &r);
  HDC screen = GetDC(window), memory = CreateCompatibleDC(screen);
  HBITMAP bitmap = CreateCompatibleBitmap(screen, r.right, r.bottom);
  auto previous = SelectObject(memory, bitmap);
  BitBlt(memory, 0, 0, r.right, r.bottom, screen, 0, 0, SRCCOPY);
  SelectObject(memory, previous);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = r.right;
  info.bmiHeader.biHeight = -r.bottom;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  std::vector<BYTE> data(static_cast<size_t>(r.right) * r.bottom * 4);
  GetDIBits(memory, bitmap, 0, r.bottom, data.data(), &info, DIB_RGB_COLORS);
  BITMAPFILEHEADER header{};
  header.bfType = 0x4d42;
  header.bfOffBits = sizeof header + sizeof(BITMAPINFOHEADER);
  header.bfSize = header.bfOffBits + static_cast<DWORD>(data.size());
  HANDLE file = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  DWORD written;
  if (file != INVALID_HANDLE_VALUE) {
    WriteFile(file, &header, sizeof header, &written, nullptr);
    WriteFile(file, &info.bmiHeader, sizeof info.bmiHeader, &written, nullptr);
    WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(file);
  }
  DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(window, screen);
}
void smokeTick() {
  if (job.valid() || pingJob.valid() || inventoryJob.valid() || canvas.moving(clockNow()))
    return;
  if (!smokeChecked) {
    smokeChecked = true;
    if (canvas.caretPosition(L"Maxifying", 0) != 0 ||
        canvas.caretPosition(L"Maxifying", 10000) != 9)
      smokeFailed = true;
    if (lower(L"ÆØÅ") != L"æøå")
      smokeFailed = true;
    int children = 0;
    EnumChildWindows(
        window,
        [](HWND, LPARAM p) -> BOOL {
          ++*reinterpret_cast<int *>(p);
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&children));
    if (children)
      smokeFailed = true;
    // Focus and selection return to the original text field when a dialog is cancelled.
    auto savedSearch = search;
    int savedInput = inputFocus, savedFocus = keyboardFocus;
    size_t savedCursor = cursor, savedAnchor = anchor;
    navigate(1);
    search = L"muse";
    inputFocus = keyboardFocus = 300;
    cursor = 2;
    anchor = 1;
    showDialog(L"UI-test", L"Ingen systemændring", tr(L"Forstået"), {}, false, true);
    dismissDialog(false);
    bool focusReturned = inputFocus == 300 && keyboardFocus == 300 && cursor == 2 && anchor == 1;
    if (!focusReturned)
      smokeFailed = true;
    search = savedSearch;
    inputFocus = savedInput;
    keyboardFocus = savedFocus;
    cursor = savedCursor;
    anchor = savedAnchor;
    filterFeatures();
    dialog.opacity.snap(0);
    paint();
    keyboardFocus = 0;
    tabFocus(false);
    bool safeTab = keyboardFocus == 100;
    if (!safeTab)
      smokeFailed = true;
    keyboardFocus = savedFocus;
    inputFocus = savedInput;
    keyboardMode = false;
    auto allocations = canvas.surfaceAllocations();
    for (int i = 0; i < 30; ++i) {
      navigate(i % 5);
      paint();
    }
    navigate(0);
    paint();
    if (canvas.surfaceAllocations() != allocations)
      smokeFailed = true;
    const auto originalLanguage = l10n::selected.load();
    const auto originalPreference = tune::read(L"Software\\Maxifying\\UI", L"Language");
    bool languagesWork = true;
    for (int language = 0; language < 3; ++language) {
      languageMenu();
      paint();
      keyboardFocus = 9100 + language;
      SendMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
      DWORD stored = 99, bytes = sizeof stored;
      languagesWork = languagesWork && static_cast<int>(l10n::selected.load()) == language &&
                      RegGetValueW(HKEY_CURRENT_USER, L"Software\\Maxifying\\UI", L"Language",
                                   RRF_RT_REG_DWORD, nullptr, &stored, &bytes) == ERROR_SUCCESS &&
                      stored == static_cast<DWORD>(language);
    }
    tune::write(L"Software\\Maxifying\\UI", L"Language", originalPreference);
    l10n::selected = originalLanguage;
    refreshHardware();
    status = tr(L"Klar til din næste session.");
    dialog.opacity.snap(0);
    navigate(1);
    search.clear();
    category = 0;
    filterFeatures();
    listPosition.snap(0);
    Sleep(260); // Only the smoke test: let the page transition finish before measuring scrolling.
    paint();
    const unsigned warmCompositions = canvas.compositionPasses();
    const unsigned warmLayouts = canvas.textLayoutBuilds();
    LARGE_INTEGER perfStart{}, perfEnd{}, perfFrequency{};
    QueryPerformanceCounter(&perfStart);
    for (int frame = 0; frame < 12; ++frame) {
      listPosition.snap(frame * .4);
      paint();
    }
    QueryPerformanceCounter(&perfEnd);
    QueryPerformanceFrequency(&perfFrequency);
    const double frameMilliseconds =
        (perfEnd.QuadPart - perfStart.QuadPart) * 1000.0 / perfFrequency.QuadPart / 12;
    const unsigned newLayouts = canvas.textLayoutBuilds() - warmLayouts;
    const bool cacheWorks = newLayouts == 0 && canvas.cachedLayouts() <= 512 &&
                            canvas.compositionPasses() == warmCompositions;
    if (!languagesWork || !cacheWorks)
      smokeFailed = true;
    listPosition.snap(0);
    // Exercise downward scrolling through the actual window-message route.
    navigate(1);
    search.clear();
    category = 0;
    filterFeatures();
    SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
    const double wheelStart = clockNow();
    const double scrollTarget = listPosition.to;
    Sleep(24);
    paint();
    const double duringScroll = listPosition.value(clockNow());
    bool wheelWorks = scrollTarget == 76 && duringScroll > 0 && duringScroll < scrollTarget;
    SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA / 4)), 0);
    wheelWorks = wheelWorks && listPosition.to == 95 && clockNow() > wheelStart;
    if (!wheelWorks)
      smokeFailed = true;
    navigate(0);
    std::ofstream report("maxifying-ui-test.txt");
    report << (languagesWork ? "PASS" : "FAIL")
           << ": language picker keyboard selection persists all three preferences; original "
              "restored\n";
    report << (cacheWorks ? "PASS" : "FAIL") << ": scroll frames build " << newLayouts
           << " text layouts after warmup; no transition composition; cached="
           << canvas.cachedLayouts() << "; mean paint=" << frameMilliseconds << "ms (Wine)\n";
    report << (wheelWorks ? "PASS" : "FAIL")
           << ": downward WM_MOUSEWHEEL moves rendered list; fractional burst retained\n";
    report << (focusReturned && safeTab ? "PASS" : "FAIL")
           << ": modal cancellation restores text selection; Tab skips window chrome\n";
    report << "Children=" << children << " surfaces=" << allocations << " -> "
           << canvas.surfaceAllocations() << " caret=" << canvas.caretPosition(L"Maxifying", 0)
           << "/" << canvas.caretPosition(L"Maxifying", 10000)
           << " unicode=" << (lower(L"ÆØÅ") == L"æøå") << "\n";
    report
        << (smokeFailed ? "FAIL" : "PASS")
        << ": zero native child controls; 30 rapid page switches reuse the same render surfaces\n";
    beginJob(
        [] {
          Sleep(120);
          return std::wstring{};
        },
        L"UI-test: ingen systemindstillinger ændret.");
    navigate(1);
    if (!job.valid() || page != 1)
      smokeFailed = true;
    navigate(0);
    report << (smokeFailed ? "FAIL" : "PASS")
           << ": navigation remains responsive during pending work\n";
    return;
  }
  screenshot(L"maxifying-page" + std::to_wstring(smokeStep) + L".bmp");
  ++smokeStep;
  switch (smokeStep) {
  case 1:
    navigate(1);
    break;
  case 2:
    navigate(2);
    focusTab = 0;
    break;
  case 3:
    switchFocusTab(1);
    break;
  case 4:
    switchFocusTab(2);
    break;
  case 5:
    navigate(3);
    address = L"127.0.0.1";
    safeActivate(511);
    break;
  case 6:
    navigate(4);
    break;
  case 7:
    startProfile(2);
    break;
  case 8:
    dismissDialog(false);
    navigate(1);
    category = 2;
    filterFeatures();
    break;
  case 9:
    showDialog(
        tr(L"Windows afviste ændringen"),
        tr(L"Dette er et visuelt testvindue. Ingen systemændring blev udført.\n\nDin originale "
           L"indstilling og dine backups bevares."),
        tr(L"Forstået"), {}, true, true);
    break;
  case 10:
    dismissDialog(false);
    inputFocus = 300;
    search = l10n::selected == l10n::Language::English ? L"mouse" : L"mus";
    cursor = anchor = search.size();
    filterFeatures();
    break;
  case 11:
    inputFocus = 0;
    search.clear();
    category = 4;
    featureOffset = 0;
    listPosition.snap(0);
    filterFeatures();
    break;
  case 12:
    category = 1;
    filterFeatures();
    break;
  case 13:
    navigate(2);
    switchFocusTab(3);
    break;
  case 14:
    languageMenu();
    break;
  default:
    PostMessageW(window, WM_CLOSE, 0, 0);
    break;
  }
  invalidate();
  animate();
}
LRESULT CALLBACK procedure(HWND w, UINT message, WPARAM a, LPARAM b) {
  switch (message) {
  case WM_NCCALCSIZE:
    if (a)
      return 0;
    break;
  case WM_CREATE: {
    window = w;
    auto result = canvas.initialize(w, scale);
    if (FAILED(result))
      return -1;
    nav.snap(330);
    inspector.snap(1);
    notice.snap(1);
    try {
      game = tune::selectedGame();
    } catch (...) {
    }
    refreshStates();
    refreshHardware();
    filterFeatures();
    SetTimer(w, 1, 500, nullptr);
    return 0;
  }
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT:
    paint();
    return 0;
  case WM_NCHITTEST: {
    POINT point{GET_X_LPARAM(b), GET_Y_LPARAM(b)};
    ScreenToClient(w, &point);
    if (point.y / scale < 96 && !dialog.open && !hitAt(point.x / scale, point.y / scale))
      return HTCAPTION;
    return HTCLIENT;
  }
  case WM_GETMINMAXINFO: {
    auto info = reinterpret_cast<MINMAXINFO *>(b);
    info->ptMinTrackSize = {pixel(ui::width), pixel(ui::height)};
    info->ptMaxTrackSize = info->ptMinTrackSize;
    return 0;
  }
  case WM_MOUSEMOVE: {
    if ((pressed == 300 || pressed == 510) && inputFocus == pressed) {
      auto f =
          std::find_if(hits.begin(), hits.end(), [](const Hit &h) { return h.id == inputFocus; });
      if (f != hits.end()) {
        cursor = canvas.caretPosition(editValue(),
                                      GET_X_LPARAM(b) / scale - f->rect.x - 14 + editScroll);
        invalidate();
      }
    }
    float x = GET_X_LPARAM(b) / scale, y = GET_Y_LPARAM(b) / scale;
    auto h = hitAt(x, y);
    moveHover(h ? h->id : 0);
    TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, w, 0};
    TrackMouseEvent(&track);
    return 0;
  }
  case WM_MOUSELEAVE:
    moveHover(0);
    return 0;
  case WM_SETCURSOR:
    if (LOWORD(b) == HTCLIENT) {
      POINT point;
      GetCursorPos(&point);
      ScreenToClient(w, &point);
      auto h = hitAt(point.x / scale, point.y / scale);
      SetCursor(LoadCursorW(nullptr, h ? (h->field ? IDC_IBEAM : IDC_HAND) : IDC_ARROW));
      return TRUE;
    }
    break;
  case WM_LBUTTONDOWN: {
    keyboardMode = false;
    pendingHigh = 0;
    SetFocus(w);
    auto h = hitAt(GET_X_LPARAM(b) / scale, GET_Y_LPARAM(b) / scale);
    if (h) {
      pressed = h->id;
      keyboardFocus = h->id;
      int previousInput = inputFocus;
      inputFocus = h->field ? h->id : 0;
      if (inputFocus) {
        cursor =
            canvas.caretPosition(editValue(), GET_X_LPARAM(b) / scale - h->rect.x - 14 +
                                                  (previousInput == inputFocus ? editScroll : 0));
        if (previousInput != inputFocus || !(GetKeyState(VK_SHIFT) & 0x8000))
          anchor = cursor;
      }
      SetCapture(w);
    } else {
      inputFocus = 0;
      keyboardFocus = 0;
    }
    invalidate();
    return 0;
  }
  case WM_LBUTTONUP: {
    int id = pressed;
    pressed = 0;
    ReleaseCapture();
    auto h = hitAt(GET_X_LPARAM(b) / scale, GET_Y_LPARAM(b) / scale);
    if (h && h->id == id && !h->field)
      safeActivate(id);
    invalidate();
    animate();
    return 0;
  }
  case WM_CAPTURECHANGED:
    pressed = 0;
    invalidate();
    return 0;
  case WM_MOUSEWHEEL: {
    const double steps = -static_cast<double>(GET_WHEEL_DELTA_WPARAM(a)) / WHEEL_DELTA;
    const double now = clockNow();
    if (dialog.open) {
      dialog.scroll.target(
          std::clamp(dialog.scroll.to + steps * 40, 0.0, static_cast<double>(dialog.maximum)), now);
      animate();
      invalidate();
      return 0;
    }
    if (page == 1 && filtered.size() > 6) {
      double next = std::clamp(listPosition.to + steps * 76, 0.0,
                               static_cast<double>(filtered.size() - 6) * 76);
      featureOffset = static_cast<size_t>(std::ceil(next / 76));
      listPosition.target(next, now);
      animate();
    }
    if (page == 2 && focusTab) {
      size_t count = focusTab == 1   ? processes.size()
                     : focusTab == 2 ? startups.size()
                                     : inventory.apps.size();
      auto &offset = focusTab == 1 ? processOffset : focusTab == 2 ? startupOffset : bloatOffset;
      if (count > 5) {
        double next =
            std::clamp(focusPosition.to + steps * 59, 0.0, static_cast<double>(count - 5) * 59);
        offset = static_cast<size_t>(std::ceil(next / 59));
        focusPosition.target(next, now);
        animate();
      }
    }
    invalidate();
    return 0;
  }
  case WM_KEYDOWN:
    try {
      keyDown(a);
    } catch (const std::exception &e) {
      errorDialog(tune::errorText(e));
    }
    return 0;
  case WM_CHAR:
    if (inputFocus && a >= 32 && !(GetKeyState(VK_CONTROL) & 0x8000)) {
      wchar_t c = static_cast<wchar_t>(a);
      if (ui_text::high(c)) {
        pendingHigh = c;
        return 0;
      }
      if (ui_text::low(c)) {
        if (pendingHigh)
          insertText(std::wstring{pendingHigh, c});
        pendingHigh = 0;
      } else {
        pendingHigh = 0;
        insertText(std::wstring(1, c));
      }
    } else
      pendingHigh = 0;
    return 0;
  case WM_TIMER:
    if (a == 2) {
      finishJob();
      finishPing();
      invalidate();
      if (!moving()) {
        KillTimer(w, 2);
        timerRunning = false;
        if (preciseTimer) {
          timeEndPeriod(1);
          preciseTimer = false;
        }
      }
      return 0;
    }
    if (a == 1) {
      finishInventory();
      static unsigned ticks = 0;
      if (inputFocus)
        invalidate();
      if (++ticks % 4 == 0 && !moving()) {
        auto before = hardware;
        refreshHardware(false);
        if (before != hardware)
          invalidate();
      }
      if (smoke)
        smokeTick();
      return 0;
    }
    break;
  case WM_CLOSE:
    requestClose();
    return 0;
  case WM_DESTROY:
    KillTimer(w, 1);
    KillTimer(w, 2);
    if (preciseTimer) {
      timeEndPeriod(1);
      preciseTimer = false;
    }
    closeProcessHandles();
    for (auto &p : priorities)
      CloseHandle(p.handle);
    priorities.clear();
    for (auto &record : powerOverrides)
      CloseHandle(record.handle);
    powerOverrides.clear();
    PostQuitMessage(smokeFailed ? 1 : 0);
    return 0;
  }
  return DefWindowProcW(w, message, a, b);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR arguments, int show) {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\GameTuneSingleInstance");
  if (!mutex)
    return 1;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND existing = FindWindowW(L"MaxifyingWindow", nullptr);
    if (existing) {
      ShowWindow(existing, SW_RESTORE);
      SetForegroundWindow(existing);
    }
    CloseHandle(mutex);
    return 0;
  }
  DWORD language = 0, languageBytes = sizeof language;
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Maxifying\\UI", L"Language", RRF_RT_REG_DWORD,
                   nullptr, &language, &languageBytes) == ERROR_SUCCESS &&
      language <= 2)
    l10n::selected = static_cast<l10n::Language>(language);
  smoke = std::wstring(arguments).find(L"--smoke-ui") != std::wstring::npos;
  const std::wstring commandLine(arguments);
  if (smoke && commandLine.find(L"--lang=en") != std::wstring::npos)
    l10n::selected = l10n::Language::English;
  if (smoke && commandLine.find(L"--lang=sv") != std::wstring::npos)
    l10n::selected = l10n::Language::Swedish;
  SetProcessDPIAware();
  RECT desktop{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &desktop, 0);
  scale = std::min({1.f, (desktop.right - desktop.left - 40) / ui::width,
                    (desktop.bottom - desktop.top - 40) / ui::height});
  if (scale <= 0)
    scale = 1;
  if (smoke && std::wstring(arguments).find(L"--small") != std::wstring::npos)
    scale = .78f;
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  WSADATA sockets;
  WSAStartup(MAKEWORD(2, 2), &sockets);
  WNDCLASSW cls{};
  cls.hInstance = instance;
  cls.lpfnWndProc = procedure;
  cls.lpszClassName = L"MaxifyingWindow";
  cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
  cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  if (!RegisterClassW(&cls))
    return 1;
  HWND w = CreateWindowW(cls.lpszClassName, L"Maxifying 0.6",
                         WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                         CW_USEDEFAULT, pixel(ui::width), pixel(ui::height), nullptr, nullptr,
                         instance, nullptr);
  if (!w)
    return 1;
  BOOL dark = TRUE;
  DwmSetWindowAttribute(w, 20, &dark, sizeof dark);
  DWORD corner = 2, borderColor = 0x00110d0b;
  DwmSetWindowAttribute(w, 33, &corner, sizeof corner);
  DwmSetWindowAttribute(w, 34, &borderColor, sizeof borderColor);
  MARGINS margins{1, 1, 1, 1};
  DwmExtendFrameIntoClientArea(w, &margins);
  ShowWindow(w, show);
  MSG message;
  BOOL result;
  while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  if (pingJob.valid())
    pingJob.wait();
  if (inventoryJob.valid())
    inventoryJob.wait();
  WSACleanup();
  CoUninitialize();
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return result == -1 ? 1 : static_cast<int>(message.wParam);
}
