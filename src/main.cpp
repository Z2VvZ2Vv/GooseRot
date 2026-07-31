#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <cerrno>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>

#include "app.hpp"
#include "core.hpp"
#include "recovery_watchdog.hpp"

namespace {

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
  std::wstring message = L"GooseRot will take over the desktop visually for five minutes.\r\n\r\n";
  if (config.desktopEffects) {
    message += L"With your consent, the geese will move and violently shake the pointer, and may "
               L"temporarily move selected windows before restoring their positions.\r\n";
  }
  message += L"The experience includes brief rate-limited full-screen flashes, rapid glitch "
             L"motion and asynchronous Windows-style alert sounds. Choose Reduced / Muted if "
             L"you are photosensitive, motion-sensitive or do not want sound.\r\n";
  message += L"GooseRot may create up to 67 fake Task Manager, File Explorer, Notepad and system "
              L"windows. They may multiply and refuse normal close requests. Every one belongs to "
              L"GooseRot and is destroyed during cleanup.\r\n"
              L"It may also launch up to 6 genuine built-in Windows utilities (Notepad, Paint, "
              L"Task Manager, Character Map, Command Prompt or a separate File Explorer), move only those new windows, and ask "
              L"them to close during cleanup. Random typing stays inside GooseRot's own Notepad.\r\n"
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
  dialog.pszMainInstruction = L"Choose how intense the five-minute takeover may be.";
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
      return true;
    }
    return pressed == kFullExperience;
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
    return true;
  }
  return fallback == IDYES;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  SetProcessDPIAware();
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
  gooserot::AppConfig config;
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

  int result = 0;
  if (AskForConsent(config)) {
    gooserot::GooseRotApp app(instance, config);
    if (!app.Initialize(error)) {
      MessageBoxW(nullptr, error.c_str(), L"GooseRot - initialization failed", MB_OK | MB_ICONERROR);
      result = 4;
    } else {
      result = app.Run();
    }
  }

  ReleaseMutex(mutex);
  CloseHandle(mutex);
  LocalFree(arguments);
  OleUninitialize();
  return result;
}
