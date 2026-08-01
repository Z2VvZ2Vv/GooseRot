#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cwchar>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#include "companion_windows.hpp"
#include "desktop_director.hpp"
#include "recovery_watchdog.hpp"

namespace {

constexpr wchar_t kVictimClass[] = L"GooseRotWin32TestVictim";
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
    Expect(swarm.AtCap(), "popup swarm reaches but never exceeds its cap");

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
    Expect(swarm.Count() == 0, "emergency cleanup destroys the entire capped swarm");
  }
}

void TestPopupSwarmCeilingAndDissolve() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  std::mt19937 random(67);
  gooserot::PopupSwarm swarm;

  swarm.SetCeiling(4);
  swarm.Spawn(instance, random, 20);
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
  TestCrashRecovery();
  TestPopupSwarmCapRefusalAndEmergencyCleanup();
  TestPopupSwarmCeilingAndDissolve();
  TestNotepadRefusesTheTaskbar();
  if (gFailures == 0) {
    std::cout << "All GooseRot Win32 integration tests passed.\n";
    return 0;
  }
  std::cerr << gFailures << " integration test(s) failed.\n";
  return 1;
}
