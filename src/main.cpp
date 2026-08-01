#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <cerrno>
#include <limits>
#include <string>

#include "app.hpp"
#include "core.hpp"
#include "recovery_watchdog.hpp"

namespace {

bool AskForConsent(const gooserot::AppConfig& config) {
  if (config.preview) return true;
  std::wstring message = L"GooseRot will take over the desktop visually for six minutes.\r\n\r\n";
  if (config.desktopEffects) {
    message += L"With your consent, the geese will drag the pointer in waves — each wave hands it "
               L"back before the next one — and may temporarily move selected windows before "
               L"restoring their positions.\r\n"
               L"A small GooseRot window is parked over the Start button and swallows clicks that "
               L"land on it, so the Start menu cannot cover the scene. It is destroyed during "
               L"cleanup and the button then works normally again. Ctrl+Shift+Esc, Alt+Tab and the "
               L"Esc exit below are never affected.\r\n";
  }
  message += L"GooseRot may create up to 67 fake Task Manager, File Explorer, Notepad and system "
             L"windows. They may multiply and refuse normal close requests. Every one belongs to "
             L"GooseRot and is destroyed during cleanup.\r\n"
             L"Its own Notepad refuses to minimise into the taskbar for as long as it is typing.\r\n"
             L"It may also launch up to 6 genuine built-in Windows utilities (Notepad, Paint, "
             L"Task Manager or a separate File Explorer), move only those new windows, and ask "
             L"them to close during cleanup. Random typing stays inside GooseRot's own Notepad.\r\n"
             L"Geese carry brainrot photos onto the desktop. Each one can be closed with its [x], "
             L"which costs aura and makes the flock fetch two more.\r\n"
             L"The Start or Search surface is dismissed if it covers the experience.\r\n"
             L"No clipboard data, system file, startup setting, real BSOD or real reboot is changed.\r\n\r\n"
             L"Hold Esc for 2 seconds at any time to close everything and restore the desktop.\r\n\r\n"
             L"Start the experience?";
  const UINT icon = config.mode == gooserot::RunMode::Safe ? MB_ICONINFORMATION : MB_ICONWARNING;
  return MessageBoxW(nullptr, message.c_str(), L"GooseRot - explicit consent",
                     MB_OKCANCEL | MB_DEFBUTTON2 | MB_TOPMOST | icon) == IDOK;
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
