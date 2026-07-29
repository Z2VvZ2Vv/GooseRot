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
  std::wstring message = L"GooseRot va afficher un overlay pendant cinq minutes.\r\n\r\n";
  if (config.desktopEffects) {
    message += L"Avec votre accord, l'oie pourra déplacer ponctuellement le curseur et certaines fenêtres, "
               L"puis restaurera leur position.\r\n";
  }
  message += L"Aucun presse-papiers, fichier système, démarrage, BSOD ou redémarrage réel ne sera modifié.\r\n\r\n"
             L"Maintenez Esc pendant 2 secondes pour fermer à tout moment.\r\n\r\n"
             L"Démarrer la démonstration ?";
  const UINT icon = config.mode == gooserot::RunMode::Safe ? MB_ICONINFORMATION : MB_ICONWARNING;
  return MessageBoxW(nullptr, message.c_str(), L"GooseRot — consentement explicite",
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
    if (error.empty()) error = L"Impossible de lire la ligne de commande.";
    error += L"\r\n\r\n" + gooserot::UsageText();
    MessageBoxW(nullptr, error.c_str(), L"GooseRot — arguments invalides", MB_OK | MB_ICONERROR);
    if (arguments) LocalFree(arguments);
    OleUninitialize();
    return 2;
  }

  if (config.showHelp) {
    MessageBoxW(nullptr, gooserot::UsageText().c_str(), L"GooseRot — aide", MB_OK | MB_ICONINFORMATION);
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
      MessageBoxW(nullptr, error.c_str(), L"GooseRot — initialisation impossible", MB_OK | MB_ICONERROR);
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
