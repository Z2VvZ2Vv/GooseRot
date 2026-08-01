#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cwchar>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "companion_windows.hpp"
#include "desktop_director.hpp"
#include "overlay_window.hpp"
#include "recovery_watchdog.hpp"
#include "resource.h"
#include "shell_surface.hpp"

namespace {

constexpr wchar_t kVictimClass[] = L"GooseRotWin32TestVictim";
constexpr wchar_t kFakeShellClass[] = L"DV2ControlHost";
bool gSlowPositionChanges = false;
int gFailures = 0;

void Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++gFailures;
  }
}

bool SameRectangle(const RECT& left, const RECT& right) {
  return EqualRect(&left, &right) != FALSE;
}

LRESULT CALLBACK VictimProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_WINDOWPOSCHANGING && gSlowPositionChanges) Sleep(450);
  switch (message) {
    case WM_CLOSE:
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

void DrainPendingMessages() {
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    if (message.message == WM_QUIT) continue;
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

LRESULT CALLBACK FakeShellProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  return DefWindowProcW(window, message, wParam, lParam);
}

int RunWindowHelper(const wchar_t* readyEventName, const wchar_t* title, bool slow) {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = &VictimProcedure;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  windowClass.lpszClassName = kVictimClass;
  if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 20;

  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor)) return 21;
  const int x = monitor.rcWork.left + 120;
  const int y = monitor.rcWork.top + 120;
  HWND window = CreateWindowExW(WS_EX_NOACTIVATE, kVictimClass, title,
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                x, y, 420, 260, nullptr, nullptr, instance, nullptr);
  if (!window) return 22;
  ShowWindow(window, SW_SHOWNOACTIVATE);
  UpdateWindow(window);
  gSlowPositionChanges = slow;

  HANDLE readyEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, readyEventName);
  if (!readyEvent) return 23;
  SetEvent(readyEvent);
  CloseHandle(readyEvent);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return 0;
}

std::wstring ExecutablePath() {
  wchar_t path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
  return length > 0 && length < std::size(path) ? std::wstring(path, length) : std::wstring{};
}

std::wstring UniqueName(const wchar_t* suffix) {
  LARGE_INTEGER counter{};
  QueryPerformanceCounter(&counter);
  std::wostringstream value;
  value << L"Local\\GooseRot.Test." << GetCurrentProcessId() << L'.'
        << static_cast<unsigned long long>(counter.QuadPart) << L'.' << suffix;
  return value.str();
}

struct VictimProcess {
  HANDLE process = nullptr;
  HWND window = nullptr;
  std::wstring title;
};

VictimProcess LaunchVictim(bool slow) {
  VictimProcess result;
  const std::wstring executable = ExecutablePath();
  const std::wstring readyName = UniqueName(L"Ready");
  result.title = UniqueName(L"Window");
  HANDLE readyEvent = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str());
  if (!readyEvent || executable.empty()) {
    if (readyEvent) CloseHandle(readyEvent);
    return result;
  }

  std::wostringstream command;
  command << L'"' << executable << L"\" --window-helper \"" << readyName
          << L"\" \"" << result.title << L"\" " << (slow ? L"slow" : L"responsive");
  std::wstring mutableCommand = command.str();
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
    CloseHandle(readyEvent);
    return result;
  }
  CloseHandle(process.hThread);
  const HANDLE waits[] = {readyEvent, process.hProcess};
  const DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, 3000);
  CloseHandle(readyEvent);
  if (waitResult == WAIT_OBJECT_0) {
    result.process = process.hProcess;
    result.window = FindWindowW(kVictimClass, result.title.c_str());
  } else {
    CloseHandle(process.hProcess);
  }
  return result;
}

void CloseVictim(VictimProcess& victim) {
  if (victim.window) PostMessageW(victim.window, WM_CLOSE, 0, 0);
  if (victim.process) {
    if (WaitForSingleObject(victim.process, 3000) == WAIT_TIMEOUT) {
      TerminateProcess(victim.process, 90);
      WaitForSingleObject(victim.process, 1000);
    }
    CloseHandle(victim.process);
  }
  victim = {};
}

gooserot::WindowTarget MakeTarget(HWND window) {
  gooserot::WindowTarget target;
  target.handle = window;
  GetWindowRect(window, &target.rectangle);
  target.ownerThreadId = GetWindowThreadProcessId(window, &target.ownerProcessId);
  target.titleBarPoint = {(target.rectangle.left + target.rectangle.right) / 2,
                          target.rectangle.top + 18};
  return target;
}

bool WaitForOriginal(HWND window, const RECT& original, DWORD timeoutMilliseconds) {
  const DWORD started = GetTickCount();
  while (GetTickCount() - started < timeoutMilliseconds) {
    RECT current{};
    if (GetWindowRect(window, &current) && SameRectangle(current, original)) return true;
    Sleep(25);
  }
  return false;
}

void TestResponsiveWindow() {
  VictimProcess victim = LaunchVictim(false);
  Expect(victim.process && victim.window, "responsive victim starts");
  if (!victim.process || !victim.window) {
    CloseVictim(victim);
    return;
  }
  const gooserot::WindowTarget target = MakeTarget(victim.window);
  std::wstring error;
  gooserot::RecoveryWatchdog watchdog;
  Expect(watchdog.Start(error), "watchdog starts for responsive test");
  {
    gooserot::DesktopDirector director(true, false, &watchdog);
    Expect(director.MoveWindowBy67(target, 0), "responsive window moves exactly");
    RECT moved{};
    GetWindowRect(victim.window, &moved);
    const LONG dx = moved.left - target.rectangle.left;
    const LONG dy = moved.top - target.rectangle.top;
    Expect((std::abs(dx) == 67 && dy == 0) || (std::abs(dy) == 67 && dx == 0),
           "window offset is cardinal 67 pixels");
    Expect(director.Restore(), "responsive window restore confirms");
    Expect(WaitForOriginal(victim.window, target.rectangle, 500), "responsive rectangle is original");
  }
  CloseVictim(victim);
}

void TestSlowWindowFallback() {
  VictimProcess victim = LaunchVictim(true);
  Expect(victim.process && victim.window, "slow victim starts");
  if (!victim.process || !victim.window) {
    CloseVictim(victim);
    return;
  }
  const gooserot::WindowTarget target = MakeTarget(victim.window);
  std::wstring error;
  gooserot::RecoveryWatchdog watchdog;
  Expect(watchdog.Start(error), "watchdog starts for slow test");
  {
    gooserot::DesktopDirector director(true, false, &watchdog);
    const auto started = std::chrono::steady_clock::now();
    const bool moved = director.MoveWindowBy67(target, 0);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    Expect(!moved, "slow mutation degrades to fallback");
    Expect(elapsed.count() < 600, "slow target never blocks the UI path");

    bool restored = false;
    const DWORD deadline = GetTickCount() + 4000;
    do {
      director.PollPendingMutations();
      restored = director.Restore();
      if (!restored) Sleep(50);
    } while (!restored && static_cast<LONG>(GetTickCount() - deadline) < 0);
    Expect(restored, "slow worker eventually confirms restoration");
    Expect(WaitForOriginal(victim.window, target.rectangle, 500), "slow rectangle is original");
  }
  CloseVictim(victim);
}

void TestShellSurfaceAllowList() {
  using gooserot::IsKnownShellSurfaceIdentityForWindowsDirectory;
  constexpr wchar_t kWindowsDirectory[] = L"X:\\Windows";
  Expect(IsKnownShellSurfaceIdentityForWindowsDirectory(
             L"Windows.UI.Core.CoreWindow",
             L"X:\\Windows\\SystemApps\\Microsoft.Windows.StartMenuExperienceHost_67\\"
             L"StartMenuExperienceHost.exe",
             kWindowsDirectory),
         "modern Start CoreWindow is accepted");
  Expect(IsKnownShellSurfaceIdentityForWindowsDirectory(
             L"windows.ui.core.corewindow",
             L"x:/windows/systemapps/search_package/SEARCHHOST.EXE",
             kWindowsDirectory),
         "modern Search identity is case and separator insensitive");
  Expect(IsKnownShellSurfaceIdentityForWindowsDirectory(
             L"DV2ControlHost", L"X:\\Windows\\explorer.exe", kWindowsDirectory),
         "legacy Explorer Start host is accepted");
  Expect(!IsKnownShellSurfaceIdentityForWindowsDirectory(
             L"Windows.UI.Core.CoreWindow",
             L"C:\\Temp\\StartMenuExperienceHost.exe", kWindowsDirectory),
         "spoofed Start basename outside Windows is rejected");
  Expect(!IsKnownShellSurfaceIdentityForWindowsDirectory(
             L"DV2ControlHost", L"C:\\Temp\\explorer.exe", kWindowsDirectory),
         "spoofed Explorer basename is rejected");
  Expect(!IsKnownShellSurfaceIdentityForWindowsDirectory(
             L"Windows.UI.Core.CoreWindow",
             L"X:\\Windows\\SystemAppsEvil\\StartMenuExperienceHost.exe",
             kWindowsDirectory),
         "lookalike SystemApps directory is rejected");
  Expect(!IsKnownShellSurfaceIdentityForWindowsDirectory(
             L"Windows.UI.Core.CoreWindow",
             L"X:\\Windows\\SystemApps\\..\\Temp\\StartMenuExperienceHost.exe",
             kWindowsDirectory),
         "non-canonical shell path is rejected");
  Expect(!IsKnownShellSurfaceIdentityForWindowsDirectory(
             L"Windows.UI.Core.CoreWindow", L"C:\\Program Files\\OrdinaryApp.exe",
             kWindowsDirectory),
         "ordinary CoreWindow process is rejected");
  Expect(!IsKnownShellSurfaceIdentityForWindowsDirectory(
             L"DV2ControlHost", L"C:\\Program Files\\OrdinaryApp.exe",
             kWindowsDirectory),
         "ordinary DV2ControlHost process is rejected");
  Expect(!IsKnownShellSurfaceIdentityForWindowsDirectory(
             L"ApplicationFrameWindow", L"X:\\Windows\\explorer.exe",
             kWindowsDirectory),
         "unlisted shell class is rejected");
  Expect(!IsKnownShellSurfaceIdentityForWindowsDirectory(
             nullptr, L"X:\\Windows\\explorer.exe", kWindowsDirectory),
         "missing shell identity is rejected");

  Expect(gooserot::ShouldDismissShellSurfaceState(
             true, true, false, true, true, false),
         "foreground shell bypasses cross-band z-order comparison");
  Expect(gooserot::ShouldDismissShellSurfaceState(
             false, true, false, true, true, true),
         "background shell still requires proof that it is above the overlay");
  Expect(!gooserot::ShouldDismissShellSurfaceState(
             true, true, false, false, true, false),
         "foreground shell outside the overlay is preserved");
  Expect(!gooserot::ShouldDismissShellSurfaceState(
             true, true, false, true, false, false),
         "ordinary foreground window is preserved");

  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = &FakeShellProcedure;
  windowClass.hInstance = instance;
  windowClass.lpszClassName = kFakeShellClass;
  const ATOM atom = RegisterClassExW(&windowClass);
  Expect(atom != 0, "fake DV2ControlHost class registers");
  if (!atom) return;

  constexpr DWORD kTestWindowStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
  HWND fakeAbove = CreateWindowExW(kTestWindowStyle, kFakeShellClass,
                                   L"GooseRot fake shell surface",
                                   WS_POPUP, -32000, -32000, 1, 1,
                                   nullptr, nullptr, instance, nullptr);
  HWND reference = CreateWindowExW(kTestWindowStyle, kFakeShellClass,
                                   L"GooseRot z-order reference",
                                   WS_POPUP, -32000, -32000, 1, 1,
                                   nullptr, nullptr, instance, nullptr);
  Expect(fakeAbove != nullptr && reference != nullptr, "fake shell test windows start");
  if (fakeAbove && reference) {
    Expect(!gooserot::IsKnownShellSurfaceWindow(fakeAbove),
           "a third-party DV2ControlHost remains outside the shell allow-list");
    ShowWindow(reference, SW_SHOWNOACTIVATE);
    ShowWindow(fakeAbove, SW_SHOWNOACTIVATE);
    Expect(SetWindowPos(reference, HWND_TOP, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE,
           "z-order reference moves to the top");
    Expect(SetWindowPos(fakeAbove, HWND_TOP, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE,
           "z-order candidate moves above the reference");
    Expect(gooserot::IsWindowAboveInZOrder(fakeAbove, reference),
           "GW_HWNDNEXT recognizes a window above the reference");
    Expect(!gooserot::IsWindowAboveInZOrder(reference, fakeAbove),
           "GW_HWNDNEXT rejects a window below the reference");
  }
  if (fakeAbove) DestroyWindow(fakeAbove);
  if (reference) DestroyWindow(reference);
  UnregisterClassW(kFakeShellClass, instance);
}

int RunMutationParent(HWND victimWindow, const wchar_t* movedEventName) {
  std::wstring error;
  gooserot::RecoveryWatchdog watchdog;
  if (!watchdog.Start(error)) return 30;
  gooserot::DesktopDirector director(true, false, &watchdog);
  const gooserot::WindowTarget target = MakeTarget(victimWindow);
  if (!director.MoveWindowBy67(target, 0)) return 31;
  HANDLE movedEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, movedEventName);
  if (!movedEvent) return 32;
  SetEvent(movedEvent);
  CloseHandle(movedEvent);
  ExitProcess(99);
  return 99;
}

void TestCrashRecovery() {
  VictimProcess victim = LaunchVictim(false);
  Expect(victim.process && victim.window, "crash victim starts");
  if (!victim.process || !victim.window) {
    CloseVictim(victim);
    return;
  }
  const RECT original = MakeTarget(victim.window).rectangle;
  const std::wstring movedName = UniqueName(L"Moved");
  HANDLE movedEvent = CreateEventW(nullptr, TRUE, FALSE, movedName.c_str());
  const std::wstring executable = ExecutablePath();
  std::wostringstream command;
  command << L'"' << executable << L"\" --mutation-parent "
          << static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(victim.window))
          << L" \"" << movedName << L'"';
  std::wstring mutableCommand = command.str();
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  const BOOL created = CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
  Expect(created != FALSE, "mutation parent starts");
  if (created) {
    CloseHandle(process.hThread);
    const HANDLE waits[] = {movedEvent, process.hProcess};
    Expect(WaitForMultipleObjects(2, waits, FALSE, 4000) == WAIT_OBJECT_0,
           "mutation completes before simulated crash");
    WaitForSingleObject(process.hProcess, 3000);
    Expect(WaitForOriginal(victim.window, original, 5000),
           "watchdog restores after abrupt parent exit");
    CloseHandle(process.hProcess);
  }
  CloseHandle(movedEvent);
  CloseVictim(victim);
}

HWND FindPopupWindow() {
  return FindWindowExW(nullptr, nullptr, L"GooseRotPopup", nullptr);
}

void TestPopupSwarmCapRefusalAndEmergencyCleanup() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  std::mt19937 random(67);

  {
    gooserot::PopupSwarm swarm;
    swarm.Spawn(instance, random, 1);
    swarm.Tick(instance, random, 0.0);
    Expect(swarm.Count() == 1, "popup swarm creates its first window");
    HWND popup = FindPopupWindow();
    Expect(popup != nullptr, "popup window is discoverable");
    if (popup) SendMessageW(popup, WM_CLOSE, 0, 0);
    swarm.Tick(instance, random, 1.0);
    Expect(swarm.Count() == 2, "closing below the cap replaces one popup with two");
    swarm.CloseAll();
  }

  {
    gooserot::PopupSwarm swarm;
    swarm.Spawn(instance, random, gooserot::PopupSwarm::kMaximumPopups);
    for (int tick = 0; tick < 34; ++tick) {
      swarm.Tick(instance, random, static_cast<double>(tick) / 60.0);
    }
    Expect(swarm.AtCap(), "popup swarm reaches but never exceeds its cap");
    Expect(swarm.NativeCount() == gooserot::PopupSwarm::kMaximumNativePopups,
           "the 267-window wall materialises exactly 67 native HWND popups");

    HWND popup = FindPopupWindow();
    Expect(popup != nullptr, "a tracked popup has a live window");
    if (popup) {
      SendMessageW(popup, WM_CLOSE, 0, 0);
      SendMessageW(popup, WM_CLOSE, 0, 0);
      swarm.Tick(instance, random, 2.0);
      Expect(IsWindow(popup) != FALSE, "at the cap every normal close is refused");
      Expect(swarm.Count() == gooserot::PopupSwarm::kMaximumPopups,
             "refused closes keep the capped swarm full");
    }
    swarm.CloseAll();
    Expect(swarm.Count() == 0 && swarm.NativeCount() == 0,
           "emergency cleanup destroys the native and virtual capped swarm");
  }
}

void TestPopupSwarmCeilingAndDissolve() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  std::mt19937 random(67);
  gooserot::PopupSwarm swarm;

  swarm.SetCeiling(4);
  swarm.Spawn(instance, random, 20);
  swarm.Tick(instance, random, 0.0);
  Expect(swarm.Count() == 4, "the live ceiling bounds spawning below the maximum");
  Expect(swarm.AtCap(), "the swarm reports being at its live ceiling");

  HWND popup = FindPopupWindow();
  Expect(popup != nullptr, "a capped swarm still has live windows");
  if (popup) {
    SendMessageW(popup, WM_CLOSE, 0, 0);
    swarm.Tick(instance, random, 1.0);
    Expect(IsWindow(popup) != FALSE, "closes are refused at the live ceiling");
    Expect(swarm.Count() == 4, "a refused close leaves the count untouched");
  }

  // The finale's path: windows are destroyed outright, refusal does not apply.
  Expect(swarm.Dissolve(3) == 3, "dissolving reports what it destroyed");
  swarm.Tick(instance, random, 2.0);
  Expect(swarm.Count() == 1, "dissolved windows are reaped");
  Expect(swarm.Dissolve(9) == 1, "dissolving never claims more than it had");
  swarm.Tick(instance, random, 3.0);
  Expect(swarm.Count() == 0, "the finale can empty the swarm completely");

  swarm.SetCeiling(gooserot::PopupSwarm::kMaximumPopups);
  swarm.Spawn(instance, random, 1);
  Expect(swarm.Count() == 1, "raising the ceiling lets the swarm grow again");
  swarm.CloseAll();
}

void TestNotepadRefusesTheTaskbar() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  gooserot::NotepadWindow notepad;
  Expect(notepad.Show(instance), "the fake Notepad opens");
  HWND window = FindWindowW(L"GooseRotNotepad", nullptr);
  Expect(window != nullptr, "the fake Notepad is discoverable");
  if (!window) {
    notepad.Close();
    return;
  }

  Expect((GetWindowLongPtrW(window, GWL_STYLE) & WS_MINIMIZEBOX) == 0,
         "the fake Notepad has no minimise box");
  SendMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
  Expect(IsIconic(window) == FALSE, "SC_MINIMIZE is refused outright");
  Expect(notepad.ConsumeMinimiseRefusal(), "the refused minimise is reported once");
  Expect(!notepad.ConsumeMinimiseRefusal(), "the report is consumed exactly once");

  // Show Desktop and the taskbar button iconify from outside; the next tick
  // has to undo that too.
  ShowWindow(window, SW_MINIMIZE);
  notepad.Tick(1.0);
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
  Expect(IsIconic(window) == FALSE, "an external minimise is undone");
  Expect(notepad.ConsumeMinimiseRefusal(), "the external minimise is reported too");

  notepad.Close();
  Expect(IsWindow(window) == FALSE, "cleanup still destroys the fake Notepad outright");
}

void TestWindowsKeySuppressionPolicy() {
  using gooserot::ShouldSuppressWindowsKey;
  Expect(ShouldSuppressWindowsKey(WM_KEYDOWN, VK_LWIN),
         "left Windows key-down is suppressed in full mode");
  Expect(ShouldSuppressWindowsKey(WM_SYSKEYUP, VK_RWIN),
         "right Windows key-up is suppressed in full mode");
  Expect(!ShouldSuppressWindowsKey(WM_KEYDOWN, VK_ESCAPE),
         "the Esc emergency exit is never suppressed");
  Expect(!ShouldSuppressWindowsKey(WM_SYSKEYDOWN, VK_TAB),
         "Alt+Tab is never suppressed");
  Expect(!ShouldSuppressWindowsKey(WM_KEYDOWN, VK_CONTROL),
         "Ctrl+Shift+Esc remains available");
  Expect(!ShouldSuppressWindowsKey(WM_CHAR, VK_LWIN),
         "non-keyboard hook messages are ignored");
}

void TestEmbeddedChaosAssets() {
  constexpr std::array<int, 14> resourceIds = {
      IDR_BRAINROT_TRALALERO, IDR_BRAINROT_BALLERINA, IDR_BRAINROT_BOMBARDIRO,
      IDR_CAT_SHOCKED, IDR_CAT_JUDGMENTAL, IDR_CAT_VACANT, IDR_CAT_SUSPICIOUS,
      IDR_USER_GOOSE_ICE_CREAM, IDR_USER_GOOSE_WORKER, IDR_USER_GOOSE_CALL,
      IDR_USER_GOOSE_STORE, IDR_USER_GOOSE_PUNCHY, IDR_USER_GOOSE_GANGSTER,
      IDR_USER_GOOSE_JET};
  HMODULE module = GetModuleHandleW(nullptr);
  bool allPresent = true;
  bool allNonEmpty = true;
  for (const int resourceId : resourceIds) {
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    allPresent = allPresent && resource != nullptr;
    allNonEmpty = allNonEmpty && resource != nullptr && SizeofResource(module, resource) > 0;
  }
  Expect(allPresent, "all fourteen image resources are embedded in the executable");
  Expect(allNonEmpty, "every embedded image resource contains data");
}

void TestDenseRenderBudget() {
  DrainPendingMessages();
  const HRESULT ole = OleInitialize(nullptr);
  // Warm up window-class, GDI+ and image-codec process caches before taking the
  // resource baseline. Those one-time allocations are not per-overlay leaks.
  {
    gooserot::OverlayWindow warmup;
    std::wstring warmupError;
    if (warmup.Create(GetModuleHandleW(nullptr), true, true, []() {}, []() {},
                      warmupError)) {
      warmup.StopRenderTimer();
      gooserot::RenderState warmupState;
      warmup.Render(warmupState);
      warmup.Close();
    }
  }
  DrainPendingMessages();
  Sleep(80);
  DrainPendingMessages();
  const DWORD userBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
  const DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
  gooserot::OverlayWindow overlay;
  std::wstring error;
  const bool created = overlay.Create(GetModuleHandleW(nullptr), true, true, []() {}, []() {}, error);
  Expect(created, "dense render benchmark creates its offscreen preview");
  if (!created) {
    if (SUCCEEDED(ole)) OleUninitialize();
    return;
  }
  overlay.StopRenderTimer();
  RECT outer{0, 0, 1920, 1080};
  AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0);
  SetWindowPos(overlay.Handle(), nullptr, 0, 0, outer.right - outer.left,
               outer.bottom - outer.top,
               SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
  const gooserot::RectF bounds = overlay.CanvasBounds();
  // The desktop test runner clamps top-level windows to its work area. Accept
  // 80% of a compact runner's display while requesting and capping at 1.5 MP.
  const float availablePixels = static_cast<float>(GetSystemMetrics(SM_CXSCREEN)) *
                                static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
  const float requiredPixels = std::min(1500000.0f, availablePixels * 0.8f);
  Expect(bounds.Width() * bounds.Height() >= requiredPixels,
          "dense render benchmark uses a representative display-sized surface");

  std::vector<gooserot::GooseEntity> geese;
  geese.reserve(67);
  for (int index = 0; index < 67; ++index) {
    const int column = index % 11;
    const int row = index / 11;
    geese.emplace_back(gooserot::Vec2{90.0f + column * 170.0f, 110.0f + row * 145.0f});
  }

  constexpr std::array<int, 14> resourceIds = {
      IDR_BRAINROT_TRALALERO, IDR_BRAINROT_BALLERINA, IDR_BRAINROT_BOMBARDIRO,
      IDR_CAT_SHOCKED, IDR_CAT_JUDGMENTAL, IDR_CAT_VACANT, IDR_CAT_SUSPICIOUS,
      IDR_USER_GOOSE_ICE_CREAM, IDR_USER_GOOSE_WORKER, IDR_USER_GOOSE_CALL,
      IDR_USER_GOOSE_STORE, IDR_USER_GOOSE_PUNCHY, IDR_USER_GOOSE_GANGSTER,
      IDR_USER_GOOSE_JET};
  std::vector<gooserot::VisualSprite> sprites;
  sprites.reserve(48);
  for (int index = 0; index < 48; ++index) {
    gooserot::VisualSprite sprite;
    sprite.resourceId = resourceIds[static_cast<std::size_t>(index) % resourceIds.size()];
    sprite.center = {130.0f + static_cast<float>(index % 12) * 145.0f,
                     105.0f + static_cast<float>(index / 12) * 245.0f};
    sprite.targetCenter = sprite.center;
    sprite.size = 118.0f + static_cast<float>(index % 4) * 12.0f;
    sprite.angleDegrees = static_cast<float>((index % 5) - 2) * 2.5f;
    sprite.createdAt = 0.0;
    sprite.lifetime = 320.0;
    sprites.push_back(sprite);
  }

  std::vector<gooserot::ToastNotice> toasts;
  gooserot::RenderState state;
  state.logicalTime = 292.0;
  state.seed = 67;
  state.geese = &geese;
  state.sprites = &sprites;
  state.toasts = &toasts;
  state.bubbleText = L"CRITICAL ERROR: MAXIMUM BRAINROT REACHED.";
  state.bubbleAnchor = bounds.Center();
  state.aura = -999998;
  state.cursor = {960, 540};
  state.glitch = 0.92f;
  state.cursorChaos = 0.95f;
  state.faultRibbon = 0.92f;
  state.effectPattern = 67U;
  state.graffitiProgress = 1.0f;
  state.popupCount = 267;
  state.nativePopupCount = 0;
  state.graffiti = true;
  state.colorFilter = true;
  state.finalMonologue = true;
  state.countdown = true;
  state.flashesEnabled = false;

  for (int frame = 0; frame < 12; ++frame) overlay.Render(state);
  std::vector<double> rampSamples;
  rampSamples.reserve(30);
  state.nativePopupCount = 67;
  for (int frame = 0; frame < 30; ++frame) {
    state.popupCount = 68 + (199 * frame) / 29;
    const auto started = std::chrono::steady_clock::now();
    overlay.Render(state);
    const auto finished = std::chrono::steady_clock::now();
    rampSamples.push_back(
        std::chrono::duration<double, std::milli>(finished - started).count());
  }
  std::sort(rampSamples.begin(), rampSamples.end());
  const double rampP95 =
      rampSamples[static_cast<std::size_t>(rampSamples.size() * 0.95)];
  Expect(rampP95 < 100.0,
         "incremental 68-to-267 popup growth keeps the 10 FPS p95 budget");

  std::vector<double> samples;
  samples.reserve(30);
  state.popupCount = 267;
  state.nativePopupCount = 0;
  for (int frame = 0; frame < 30; ++frame) {
    const auto started = std::chrono::steady_clock::now();
    state.effectPattern += 17U;
    overlay.Render(state);
    const auto finished = std::chrono::steady_clock::now();
    samples.push_back(std::chrono::duration<double, std::milli>(finished - started).count());
  }
  std::sort(samples.begin(), samples.end());
  const double p95 = samples[static_cast<std::size_t>(samples.size() * 0.95)];
  const double average = std::accumulate(samples.begin(), samples.end(), 0.0) /
                         static_cast<double>(samples.size());
  std::cout << "Dense render (" << static_cast<int>(bounds.Width()) << 'x'
            << static_cast<int>(bounds.Height()) << "): average " << average
            << " ms, p95 " << p95 << " ms, ramp p95 " << rampP95 << " ms\n";
  wchar_t profileTitle[512]{};
  GetWindowTextW(overlay.Handle(), profileTitle, static_cast<int>(std::size(profileTitle)));
  std::wcout << L"Dense render profile: " << profileTitle << L'\n';
  Expect(average < 100.0, "dense render keeps at least 10 FPS on average");
  Expect(p95 < 130.0, "dense render avoids long-tail slideshow stalls");

  overlay.Close();
  DrainPendingMessages();
  Sleep(80);
  DrainPendingMessages();
  if (SUCCEEDED(ole)) OleUninitialize();
  const DWORD userAfter = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
  const DWORD gdiAfter = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
  std::cout << "Dense render resources: USER " << userBefore << " -> " << userAfter
            << ", GDI " << gdiBefore << " -> " << gdiAfter << '\n';
  Expect(userAfter <= userBefore + 8, "dense render releases USER objects");
  Expect(gdiAfter <= gdiBefore + 16, "dense render releases GDI objects");
}

bool ParseUnsigned(const wchar_t* text, unsigned long long& value) {
  if (!text || !*text || *text == L'+' || *text == L'-') return false;
  wchar_t* end = nullptr;
  value = std::wcstoull(text, &end, 10);
  return end != text && *end == L'\0';
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc == 5 && _wcsicmp(argv[1], L"--recovery-watchdog") == 0) {
    unsigned long long parent = 0;
    if (!ParseUnsigned(argv[2], parent) || parent == 0 || parent > MAXDWORD) return 10;
    return gooserot::RecoveryWatchdog::RunChild(
        static_cast<DWORD>(parent), argv[3], argv[4]);
  }
  if (argc == 5 && _wcsicmp(argv[1], L"--window-helper") == 0) {
    return RunWindowHelper(argv[2], argv[3], _wcsicmp(argv[4], L"slow") == 0);
  }
  if (argc == 4 && _wcsicmp(argv[1], L"--mutation-parent") == 0) {
    unsigned long long windowValue = 0;
    if (!ParseUnsigned(argv[2], windowValue)) return 33;
    return RunMutationParent(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(windowValue)), argv[3]);
  }

  TestResponsiveWindow();
  TestSlowWindowFallback();
  TestShellSurfaceAllowList();
  TestCrashRecovery();
  TestPopupSwarmCapRefusalAndEmergencyCleanup();
  TestPopupSwarmCeilingAndDissolve();
  TestNotepadRefusesTheTaskbar();
  TestWindowsKeySuppressionPolicy();
  TestEmbeddedChaosAssets();
  TestDenseRenderBudget();
  if (gFailures == 0) {
    std::cout << "All GooseRot Win32 integration tests passed.\n";
    return 0;
  }
  std::cerr << gFailures << " integration test(s) failed.\n";
  return 1;
}
