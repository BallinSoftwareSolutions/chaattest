#include "bloat.h"
#include <windows.h>
#include <stdexcept>
namespace bloat {
namespace {
struct OperationError : std::exception {
  std::wstring message;
  explicit OperationError(std::wstring text) : message(std::move(text)) {}
  const char *what() const noexcept override {
    return "Windows app operation failed";
  }
};
struct Handle {
  HANDLE value = nullptr;
  ~Handle() {
    if (value && value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
  }
};
std::wstring shell() {
  wchar_t directory[MAX_PATH];
  if (!GetSystemDirectoryW(directory, MAX_PATH))
    return L"";
  auto path = std::wstring(directory) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
  return GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES ? L"" : path;
}
std::wstring utf8(const std::string &bytes) {
  if (bytes.empty())
    return L"";
  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                                 static_cast<int>(bytes.size()), nullptr, 0);
  if (size <= 0)
    throw std::runtime_error("Windows returned invalid UTF-8 output");
  std::wstring text(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()),
                      text.data(), size);
  return text;
}
std::wstring run(const std::wstring &body, bool readOnly,
                 std::shared_ptr<std::atomic_bool> cancel = {}) {
  auto executable = shell();
  if (executable.empty())
    throw std::runtime_error("Windows PowerShell is unavailable");
  auto script = L"[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false); "
                L"$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; try { " +
                body + L" } catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }";
  auto command = L"\"" + executable + L"\" -NoLogo -NoProfile -NonInteractive -EncodedCommand " +
                 encoded(script);
  SECURITY_ATTRIBUTES security{sizeof security, nullptr, TRUE};
  Handle read, write, input, process, thread;
  if (!CreatePipe(&read.value, &write.value, &security, 0) ||
      !SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0))
    throw std::runtime_error("Cannot create app-inventory pipe");
  input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (input.value == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Cannot open Windows input stream");
  STARTUPINFOW start{};
  start.cb = sizeof start;
  start.dwFlags = STARTF_USESTDHANDLES;
  start.hStdInput = input.value;
  start.hStdOutput = write.value;
  start.hStdError = write.value;
  PROCESS_INFORMATION created{};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                      nullptr, nullptr, &start, &created))
    throw std::runtime_error("Cannot start Windows app inventory");
  process.value = created.hProcess;
  thread.value = created.hThread;
  CloseHandle(write.value);
  write.value = nullptr;
  const auto begun = GetTickCount64();
  std::string output;
  bool truncated = false;
  for (;;) {
    DWORD available = 0;
    if (PeekNamedPipe(read.value, nullptr, 0, nullptr, &available, nullptr) && available) {
      char buffer[4096];
      DWORD count = 0;
      if (ReadFile(read.value, buffer, std::min<DWORD>(available, sizeof buffer), &count,
                   nullptr) &&
          count) {
        if (output.size() + count <= 1024 * 1024)
          output.append(buffer, count);
        else
          truncated = true;
        continue;
      }
    }
    if (WaitForSingleObject(process.value, 40) == WAIT_OBJECT_0) {
      DWORD remaining = 0;
      if (!PeekNamedPipe(read.value, nullptr, 0, nullptr, &remaining, nullptr) || !remaining)
        break;
    }
    // Inventory is read-only and may safely time out. Never terminate a package deployment.
    if (readOnly && ((cancel && cancel->load()) || GetTickCount64() - begun > 30000)) {
      TerminateProcess(process.value, 1);
      WaitForSingleObject(process.value, 5000);
      throw std::runtime_error("Windows app inventory timed out");
    }
  }
  DWORD exit = 1;
  GetExitCodeProcess(process.value, &exit);
  if (truncated)
    throw std::runtime_error("Windows app inventory output was too large");
  auto text = utf8(output);
  if (exit)
    throw OperationError(text);
  return text;
}
} // namespace
Inventory inspect(std::shared_ptr<std::atomic_bool> cancel) {
  Inventory result;
  if (shell().empty()) {
    result.error = L"Windows PowerShell/Appx er ikke tilgængeligt på denne maskine. Funktionen "
                   L"kræver en understøttet Windows 11-installation.";
    return result;
  }
  result.supported = true;
  try {
    result.apps = parse(run(inventoryScript(), true, std::move(cancel)));
  } catch (const OperationError &e) {
    result.error = e.message;
  } catch (const std::exception &e) {
    std::string message = e.what();
    result.error.assign(message.begin(), message.end());
  }
  return result;
}
std::wstring remove(const App &app) {
  try {
    (void)run(removalScript(app), false);
    return L"";
  } catch (const OperationError &e) {
    return e.message;
  } catch (const std::exception &e) {
    std::string text = e.what();
    return std::wstring(text.begin(), text.end());
  }
}
} // namespace bloat
