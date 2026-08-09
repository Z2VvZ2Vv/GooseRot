#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

#include "app.hpp"
#include "core.hpp"
#include "recovery_watchdog.hpp"

#if GOOSEROT_BUILD_PROFILE == 2
#include "lab_mode.hpp"
#endif

namespace {

// Windows 10/11 expose Focus Assist / Do Not Disturb through this local COM
// service.  It is deliberately declared here instead of linking a modern SDK
// import so the same executable still starts on Windows 7, where the class is
// simply absent and CoCreateInstance returns a failure.
struct QuietHoursSettings;

struct QuietHoursSettingsVtable {
  HRESULT(STDMETHODCALLTYPE* queryInterface)(QuietHoursSettings*, REFIID, void**);
  ULONG(STDMETHODCALLTYPE* addRef)(QuietHoursSettings*);
  ULONG(STDMETHODCALLTYPE* release)(QuietHoursSettings*);
  HRESULT(STDMETHODCALLTYPE* getUserSelectedProfile)(QuietHoursSettings*, LPWSTR*);
  HRESULT(STDMETHODCALLTYPE* putUserSelectedProfile)(QuietHoursSettings*, LPCWSTR);
};

struct QuietHoursSettings {
  QuietHoursSettingsVtable* vtable;
};

constexpr CLSID kQuietHoursSettingsClass = {
    0xf53321fa, 0x34f8, 0x4b7f, {0xb9, 0xa3, 0x36, 0x18, 0x77, 0xcb, 0x94, 0xcf}};
constexpr IID kQuietHoursSettingsInterface = {
    0x6bff4732, 0x81ec, 0x4ffb, {0xae, 0x67, 0xb6, 0xc1, 0xbc, 0x29, 0x63, 0x1f}};
constexpr wchar_t kDoNotDisturbProfile[] =
    L"Microsoft.QuietHoursProfile.PriorityOnly";

class ScopedDoNotDisturb {
 public:
  ScopedDoNotDisturb() = default;
  ~ScopedDoNotDisturb() {
    Restore();
    if (ownsComReference_) CoUninitialize();
  }

  ScopedDoNotDisturb(const ScopedDoNotDisturb&) = delete;
  ScopedDoNotDisturb& operator=(const ScopedDoNotDisturb&) = delete;

  bool Changed() const noexcept { return changed_; }

  void Activate() noexcept {
    if (attempted_) return;
    attempted_ = true;

    // Keep our own COM reference alive until restoration.  This remains valid
    // even though wWinMain releases its separate OleInitialize reference first.
    const HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ownsComReference_ = SUCCEEDED(initializeResult);
    if (FAILED(initializeResult)) return;

    QuietHoursSettings* settings = CreateSettings();
    if (!settings) return;

    LPWSTR currentProfile = nullptr;
    const HRESULT queryResult =
        settings->vtable->getUserSelectedProfile(settings, &currentProfile);
    if (SUCCEEDED(queryResult) && currentProfile && *currentProfile &&
        _wcsicmp(currentProfile, kDoNotDisturbProfile) != 0) {
      previousProfile_ = currentProfile;
      changed_ = SUCCEEDED(
          settings->vtable->putUserSelectedProfile(settings, kDoNotDisturbProfile));
    }
    if (currentProfile) CoTaskMemFree(currentProfile);
    settings->vtable->release(settings);
  }

 private:
  static QuietHoursSettings* CreateSettings() noexcept {
    QuietHoursSettings* settings = nullptr;
    const HRESULT result = CoCreateInstance(
        kQuietHoursSettingsClass, nullptr, CLSCTX_LOCAL_SERVER,
        kQuietHoursSettingsInterface, reinterpret_cast<void**>(&settings));
    return SUCCEEDED(result) ? settings : nullptr;
  }

  void Restore() noexcept {
    if (!changed_ || previousProfile_.empty()) return;

    QuietHoursSettings* settings = CreateSettings();
    if (!settings) return;

    // Respect a mode the user deliberately selected during the experience.
    // Restore only while Windows is still using the profile GooseRot applied.
    LPWSTR currentProfile = nullptr;
    if (SUCCEEDED(settings->vtable->getUserSelectedProfile(settings, &currentProfile)) &&
        currentProfile &&
        _wcsicmp(currentProfile, kDoNotDisturbProfile) == 0) {
      settings->vtable->putUserSelectedProfile(settings, previousProfile_.c_str());
    }
    if (currentProfile) CoTaskMemFree(currentProfile);
    settings->vtable->release(settings);
    changed_ = false;
  }

  std::wstring previousProfile_;
  bool attempted_ = false;
  bool changed_ = false;
  bool ownsComReference_ = false;
};

int RunDoNotDisturbGuardian(DWORD parentProcessId,
                            const wchar_t* readyEventName) noexcept {
  if (parentProcessId == 0 || !readyEventName || !*readyEventName) return 20;

  HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentProcessId);
  if (!parent) return 21;
  HANDLE readyEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, readyEventName);
  if (!readyEvent) {
    CloseHandle(parent);
    return 22;
  }

  ScopedDoNotDisturb doNotDisturb;
  doNotDisturb.Activate();
  SetEvent(readyEvent);
  CloseHandle(readyEvent);

  // If GooseRot changed the profile, keep this small process alive so it can
  // restore the previous profile even after an abrupt parent termination.
  if (doNotDisturb.Changed()) WaitForSingleObject(parent, INFINITE);
  CloseHandle(parent);
  return 0;
}

bool StartDoNotDisturbGuardian() noexcept {
  LARGE_INTEGER counter{};
  QueryPerformanceCounter(&counter);
  const std::wstring readyEventName =
      L"Local\\GooseRot.DoNotDisturb." + std::to_wstring(GetCurrentProcessId()) +
      L'.' + std::to_wstring(static_cast<unsigned long long>(counter.QuadPart));
  HANDLE readyEvent = CreateEventW(nullptr, TRUE, FALSE, readyEventName.c_str());
  if (!readyEvent) return false;

  wchar_t executable[MAX_PATH]{};
  const DWORD executableLength =
      GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
  if (executableLength == 0 || executableLength >= std::size(executable)) {
    CloseHandle(readyEvent);
    return false;
  }

  std::wstring command = L"\"" + std::wstring(executable, executableLength) +
                         L"\" --do-not-disturb-guardian " +
                         std::to_wstring(GetCurrentProcessId()) + L" \"" +
                         readyEventName + L"\"";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  const BOOL created = CreateProcessW(
      executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
      nullptr, nullptr, &startup, &process);
  if (!created) {
    CloseHandle(readyEvent);
    return false;
  }
  CloseHandle(process.hThread);

  const HANDLE waits[] = {readyEvent, process.hProcess};
  const DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, 5000);
  const bool ready = waitResult == WAIT_OBJECT_0;
  CloseHandle(process.hProcess);
  CloseHandle(readyEvent);
  return ready;
}

void AttachConsoleIfRequested() {
#if GOOSEROT_BUILD_DEBUG_CONSOLE
  static bool consoleAttached = false;
  if (consoleAttached) return;
  if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
#ifdef _MSC_VER
    FILE* consoleOutput = nullptr;
    FILE* consoleError = nullptr;
    (void)_wfreopen_s(&consoleOutput, L"CONOUT$", L"w", stdout);
    (void)_wfreopen_s(&consoleError, L"CONOUT$", L"w", stderr);
#else
    (void)std::freopen("CONOUT$", "w", stdout);
    (void)std::freopen("CONOUT$", "w", stderr);
#endif
    std::ios::sync_with_stdio();
    std::wcout.clear();
    std::wcerr.clear();
    consoleAttached = true;
    SetConsoleTitleW(L"GooseRot-Lab-Debug");
    std::wcout << L"[debug] Console attached." << std::endl;
  }
#endif
}

void DetachConsoleIfRequested() {
#if GOOSEROT_BUILD_DEBUG_CONSOLE
  if (GetConsoleWindow() != nullptr) {
    std::wcout << L"[debug] Closing console.\n";
  }
#endif
}

[[maybe_unused]] void WriteLogFile(const std::wstring& text) {
  (void)text;
#if GOOSEROT_BUILD_DEBUG_CONSOLE
  wchar_t executable[MAX_PATH]{};
  const DWORD length =
      GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
  std::wstring logPath = L"GooseRot-Lab-Debug.log";
  if (length != 0 && length < std::size(executable)) {
    logPath.assign(executable, length);
    const size_t slash = logPath.find_last_of(L"\\/");
    logPath.resize(slash == std::wstring::npos ? 0 : slash + 1);
    logPath += L"GooseRot-Lab-Debug.log";
  }
  std::wofstream log(logPath.c_str(), std::ios::app);
  if (log) {
    log << text << L"\n";
    log.flush();
  }
#endif
}

[[maybe_unused]] void CaptureLabLogs(const std::wstring& message) {
  (void)message;
#if GOOSEROT_BUILD_DEBUG_CONSOLE
  std::wcout << message << std::endl;
  OutputDebugStringW((message + L"\n").c_str());
  WriteLogFile(message);
#endif
}

#ifndef GOOSEROT_BUILD_PROFILE
#define GOOSEROT_BUILD_PROFILE 0
#endif

static_assert(GOOSEROT_BUILD_PROFILE >= 0 && GOOSEROT_BUILD_PROFILE <= 2,
              "GOOSEROT_BUILD_PROFILE must be safe (0), normal (1) or lab (2)");

constexpr gooserot::RunMode CompiledProfile() {
  if constexpr (GOOSEROT_BUILD_PROFILE == 1) return gooserot::RunMode::Normal;
  if constexpr (GOOSEROT_BUILD_PROFILE == 2) return gooserot::RunMode::Lab;
  return gooserot::RunMode::Safe;
}

constexpr bool IsLabExecutable() {
  return CompiledProfile() == gooserot::RunMode::Lab;
}

void ApplyCompiledProfile(gooserot::AppConfig& config) {
  config.mode = CompiledProfile();
  config.modeLocked = true;
  // The dedicated filename is the operator's explicit VM choice. It replaces
  // the old --vm-confirmed requirement for GooseRot-Lab.exe.
  if (IsLabExecutable()) config.vmConfirmed = true;
}

using TaskDialogIndirectProcedure = HRESULT(WINAPI*)(
    const TASKDIALOGCONFIG*, int*, int*, BOOL*);

TaskDialogIndirectProcedure ResolveTaskDialogIndirect() {
  // TaskDialogIndirect only exists in version 6 of the common-controls DLL.
  // Resolve it at runtime so systems that expose an older compatibility
  // surface can still start GooseRot and use the MessageBox fallback below.
  const HMODULE commonControls = GetModuleHandleW(L"comctl32.dll");
  if (!commonControls) return nullptr;
  const FARPROC address = GetProcAddress(commonControls, "TaskDialogIndirect");
  static_assert(sizeof(address) == sizeof(TaskDialogIndirectProcedure));
  TaskDialogIndirectProcedure procedure = nullptr;
  std::memcpy(&procedure, &address, sizeof(procedure));
  return procedure;
}

bool AskForConsent(gooserot::AppConfig& config) {
  if (config.preview) return true;
  std::wstring message =
      L"After a quiet random wait of 10 to 30 seconds, a goose is going to enter from outside "
      L"the screen and inspect your desktop for seven and a half minutes. It will take the "
      L"place over while it does.\r\n\r\n";
  if (config.desktopEffects) {
    message += L"With your consent, the geese will drag the pointer in waves — each wave hands it "
               L"back before the next one — and may temporarily move selected windows before "
               L"restoring their positions.\r\n"
               L"A small GooseRot window is parked over the Start button and swallows clicks that "
               L"land on it, so the Start menu cannot cover the scene. It is destroyed during "
               L"cleanup and the button then works normally again. In Full mode only, the left "
               L"and right Windows keys are also suppressed for the duration of the experience "
               L"and restored when it ends (or immediately on emergency exit). Ctrl+Shift+Esc, "
               L"Alt+Tab and the Esc exit below are "
               L"never affected.\r\n";
  }
  message += L"The experience includes brief rate-limited full-screen flashes, rapid glitch "
             L"motion and asynchronous Windows-style alert sounds. Choose Reduced / Muted if "
             L"you are photosensitive, motion-sensitive or do not want sound.\r\n";
  message += L"A goose opens one GooseRot text window and writes an inspection report in it. "
              L"That window refuses to close for a while and refuses to minimise into the "
              L"taskbar for as long as the goose is still writing; cleanup destroys it "
              L"outright.\r\n"
              L"GooseRot may also rotate up to 6 genuine built-in Windows utilities at once (Notepad, "
              L"Paint, Task Manager, Character Map, About Windows or a separate File Explorer), "
              L"move only those new windows, and ask them to close during cleanup. If needed, "
              L"GooseRot ends only the exact utility processes it launched. Random typing stays "
              L"inside GooseRot's own Notepad.\r\n"
              L"Geese carry photographic exhibits onto the desktop. Each one can be closed with "
              L"its [x], which costs aura and makes the flock fetch two more.\r\n"
              L"The middle of the inspection progressively opens up to 100 compact GooseRot "
              L"notice windows across the desktop. They close one by one during the final "
              L"countdown, and emergency cleanup closes all remaining boxes immediately.\r\n"
              L"Start or Search is dismissed if it covers "
              L"the experience.\r\n"
              L"No clipboard data, system file, startup setting, real BSOD or real reboot is changed.\r\n\r\n"
              L"Hold Esc for 2 seconds at any time to close everything and restore the desktop.";

  constexpr int kFullExperience = 100;
  constexpr int kReducedExperience = 101;
  const TASKDIALOG_BUTTON buttons[] = {
      {kFullExperience, L"Start full experience"},
      {kReducedExperience, L"Start reduced / muted"},
  };
  TASKDIALOGCONFIG dialog{};
  dialog.cbSize = sizeof(dialog);
  dialog.hwndParent = nullptr;
  dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
  dialog.dwCommonButtons = TDCBF_CANCEL_BUTTON;
  dialog.pszWindowTitle = L"GooseRot - explicit consent";
  dialog.pszMainInstruction = L"Choose how intense the desktop inspection may be.";
  dialog.pszContent = message.c_str();
  dialog.pszMainIcon = config.mode == gooserot::RunMode::Safe
                           ? TD_INFORMATION_ICON
                           : TD_WARNING_ICON;
  dialog.cButtons = static_cast<UINT>(std::size(buttons));
  dialog.pButtons = buttons;
  dialog.nDefaultButton = kReducedExperience;

  int pressed = IDCANCEL;
  const TaskDialogIndirectProcedure showTaskDialog = ResolveTaskDialogIndirect();
  if (showTaskDialog && SUCCEEDED(showTaskDialog(&dialog, &pressed, nullptr, nullptr))) {
    if (pressed == kReducedExperience) {
      config.muted = true;
      config.flashesEnabled = false;
      config.reducedMotion = true;
      config.blockWindowsKey = false;
      return true;
    }
    if (pressed == kFullExperience) {
      config.blockWindowsKey = true;
      return true;
    }
    return false;
  }

  message += L"\r\n\r\nYes = full experience. No = reduced / muted. Cancel = exit.";
  const UINT icon = config.mode == gooserot::RunMode::Safe ? MB_ICONINFORMATION : MB_ICONWARNING;
  const int fallback = MessageBoxW(
      nullptr, message.c_str(), L"GooseRot - explicit consent",
      MB_YESNOCANCEL | MB_DEFBUTTON2 | MB_TOPMOST | icon);
  if (fallback == IDNO) {
    config.muted = true;
    config.flashesEnabled = false;
    config.reducedMotion = true;
    config.blockWindowsKey = false;
    return true;
  }
  if (fallback == IDYES) {
    config.blockWindowsKey = true;
    return true;
  }
  return false;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  SetProcessDPIAware();
  AttachConsoleIfRequested();
  INITCOMMONCONTROLSEX controls{};
  controls.dwSize = sizeof(controls);
  controls.dwICC = ICC_STANDARD_CLASSES;
  InitCommonControlsEx(&controls);
  OleInitialize(nullptr);

  int argumentCount = 0;
  wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
  if (arguments && argumentCount == 5 && _wcsicmp(arguments[1], L"--recovery-watchdog") == 0) {
    errno = 0;
    wchar_t* end = nullptr;
    const unsigned long long parent = std::wcstoull(arguments[2], &end, 10);
    const bool validParent = arguments[2][0] != L'+' && arguments[2][0] != L'-' &&
                             end != arguments[2] && *end == L'\0' && errno != ERANGE &&
                             parent != 0 && parent <= std::numeric_limits<DWORD>::max();
    const int watchdogResult = validParent
                                   ? gooserot::RecoveryWatchdog::RunChild(
                                         static_cast<DWORD>(parent), arguments[3], arguments[4])
                                   : 10;
    LocalFree(arguments);
    OleUninitialize();
    return watchdogResult;
  }
  if (arguments && argumentCount == 4 &&
      _wcsicmp(arguments[1], L"--do-not-disturb-guardian") == 0) {
    errno = 0;
    wchar_t* end = nullptr;
    const unsigned long long parent = std::wcstoull(arguments[2], &end, 10);
    const bool validParent = arguments[2][0] != L'+' && arguments[2][0] != L'-' &&
                             end != arguments[2] && *end == L'\0' && errno != ERANGE &&
                             parent != 0 && parent <= std::numeric_limits<DWORD>::max();
    const int guardianResult =
        validParent ? RunDoNotDisturbGuardian(static_cast<DWORD>(parent), arguments[3]) : 20;
    LocalFree(arguments);
    OleUninitialize();
    return guardianResult;
  }

#if GOOSEROT_BUILD_PROFILE == 2
  // Install diagnostics before anything specific to the Lab profile, including
  // the early Do Not Disturb request.
  gooserot::SetLabLogSink(CaptureLabLogs);
  CaptureLabLogs(L"[debug] Entered Lab wWinMain");
#endif

  gooserot::AppConfig config;
  ApplyCompiledProfile(config);
  // Lab has no consent dialog, so enable Do Not Disturb at the earliest safe
  // point and before every Lab startup hook. Windows 7 safely does nothing.
  if (IsLabExecutable()) {
#if GOOSEROT_BUILD_PROFILE == 2
    CaptureLabLogs(L"[debug] Requesting Windows Do Not Disturb");
#endif
    [[maybe_unused]] const bool doNotDisturbReady = StartDoNotDisturbGuardian();
#if GOOSEROT_BUILD_PROFILE == 2
    CaptureLabLogs(doNotDisturbReady
                       ? L"[debug] Windows Do Not Disturb guardian is ready"
                       : L"[debug] Windows Do Not Disturb unavailable; continuing");
#endif
  }
  std::wstring error;
  if (!arguments || !gooserot::ParseArguments(argumentCount, arguments, config, error)) {
    if (error.empty()) error = L"Unable to read the command line.";
    error += L"\r\n\r\n" + gooserot::UsageText();
    MessageBoxW(nullptr, error.c_str(), L"GooseRot - invalid arguments", MB_OK | MB_ICONERROR);
    if (arguments) LocalFree(arguments);
    OleUninitialize();
    return 2;
  }

  if (config.showHelp) {
    MessageBoxW(nullptr, gooserot::UsageText().c_str(), L"GooseRot - help", MB_OK | MB_ICONINFORMATION);
    LocalFree(arguments);
    OleUninitialize();
    return 0;
  }

  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\GooseRot.SingleInstance.67");
  if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
    if (mutex) CloseHandle(mutex);
    LocalFree(arguments);
    OleUninitialize();
    return 3;
  }

#if GOOSEROT_BUILD_PROFILE == 2
  CaptureLabLogs(L"[debug] Command line accepted; beginning Lab startup");
#endif

  // Safe and Normal remain opt-in. The dedicated Lab build is intended for a
  // disposable VM and starts immediately, without the consent/warning dialog.
  const bool launch = IsLabExecutable() || AskForConsent(config);
  // Safe and Normal must not change the notification mode unless consent was
  // granted. This still runs before GooseRotApp initialization.
  if (launch && !IsLabExecutable()) (void)StartDoNotDisturbGuardian();
  if (IsLabExecutable()) {
    config.blockWindowsKey = config.desktopEffects && !config.preview;
  }

  int result = 0;
  if (launch) {
#if GOOSEROT_BUILD_PROFILE == 2
    gooserot::LabStartupArtifacts labArtifacts;
    const bool labReady = gooserot::RunLabStartup(labArtifacts, error);
    if (!labReady) {
      MessageBoxW(nullptr, error.c_str(), L"GooseRot Lab - startup failed",
                  MB_OK | MB_ICONERROR);
      result = 5;
    }
#else
    constexpr bool labReady = true;
#endif
    if (labReady) {
#if GOOSEROT_BUILD_PROFILE == 2
      // The matching firmware files now exist in %TEMP%\GooseRot-Lab.
      // Put Lab-only code that must run immediately after extraction here;
      // labArtifacts contains the detected firmware kind and extracted paths.
      (void)labArtifacts;
#endif
#if GOOSEROT_BUILD_PROFILE == 2
      gooserot::GooseRotApp app(
          instance, config,
          [&labArtifacts](std::wstring& conclusionError) {
            return gooserot::RunLabConclusion(labArtifacts, conclusionError);
          });
#else
      gooserot::GooseRotApp app(instance, config);
#endif
      if (!app.Initialize(error)) {
        MessageBoxW(nullptr, error.c_str(), L"GooseRot - initialization failed",
                    MB_OK | MB_ICONERROR);
        result = 4;
      } else {
        result = app.Run();
      }
    }
  }

  ReleaseMutex(mutex);
  DetachConsoleIfRequested();
  CloseHandle(mutex);
  LocalFree(arguments);
  OleUninitialize();
  return result;
}
