#pragma once

#include <windows.h>

#include <memory>
#include <random>
#include <string>
#include <vector>

namespace gooserot {

enum class PromptResult { None, Primary, Secondary, Dismissed };

class PromptWindow {
 public:
  PromptWindow() = default;
  ~PromptWindow();

  bool Show(HINSTANCE instance, const wchar_t* title, const wchar_t* message,
            const wchar_t* primaryLabel, const wchar_t* secondaryLabel,
            bool evasivePrimary, double logicalTime);
  void Tick(double logicalTime);
  PromptResult ConsumeResult();
  void Close();
  bool IsOpen() const { return window_ != nullptr; }

 private:
  static ATOM Register(HINSTANCE instance);
  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  void Finish(PromptResult result);
  void MovePrimaryButton();

  HWND window_ = nullptr;
  HWND message_ = nullptr;
  HWND primaryButton_ = nullptr;
  HWND secondaryButton_ = nullptr;
  HFONT font_ = nullptr;
  PromptResult result_ = PromptResult::None;
  bool evasivePrimary_ = false;
  bool cursorWasOverPrimary_ = false;
  double shownAt_ = 0.0;
  double nextAutomaticMove_ = 0.0;
  int moveIndex_ = 0;
};

// The fake Notepad. It belongs to GooseRot, it types on its own, it declines to
// be closed for a while, and it cannot be parked in the taskbar: there is no
// minimise box, SC_MINIMIZE is swallowed and anything that iconifies it from
// outside (Show Desktop, the taskbar button) is undone on the next tick.
// `Close()` still destroys it outright, so cleanup and the emergency exit are
// never blocked.
class NotepadWindow {
 public:
  NotepadWindow() = default;
  ~NotepadWindow();

  bool Show(HINSTANCE instance);
  void SetText(const std::wstring& text);
  void Close();
  void Tick(double logicalTime);
  bool IsOpen() const { return window_ != nullptr; }
  // True once per refused close, so the goose can gloat about it.
  bool ConsumeRefusal();
  int Refusals() const { return refusals_; }
  // True once per refused minimise, for the same reason.
  bool ConsumeMinimiseRefusal();

 private:
  static ATOM Register(HINSTANCE instance);
  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  void RefuseClose();
  void RefuseMinimise();

  HINSTANCE instance_ = nullptr;
  HWND window_ = nullptr;
  HWND edit_ = nullptr;
  HFONT font_ = nullptr;
  int refusals_ = 0;
  int pendingRefusalReports_ = 0;
  int pendingMinimiseReports_ = 0;
  bool respawnQueued_ = false;
  bool respawnUsed_ = false;
};

// A bounded swarm of GooseRot popups. Closing one spawns two more until the
// protective ceiling is reached, where normal close requests are refused.
// Emergency cleanup still destroys every window directly.
class PopupSwarm {
 public:
  PopupSwarm() = default;
  ~PopupSwarm();

  PopupSwarm(const PopupSwarm&) = delete;
  PopupSwarm& operator=(const PopupSwarm&) = delete;

  // Adds up to `count` popups, never exceeding the current ceiling.
  void Spawn(HINSTANCE instance, std::mt19937& random, int count);
  // Applies queued multiplications, reaps closed windows and jiggles refusals.
  void Tick(HINSTANCE instance, std::mt19937& random, double logicalTime);
  // Destroys `count` popups outright and reports how many actually went away.
  // This is the finale's path: the glitch eats the swarm, which is not the same
  // thing as the user being allowed to close one.
  int Dissolve(int count);
  void CloseAll();
  // Lowers the live ceiling below the protective maximum. The finale uses it to
  // stop the swarm refilling itself while it is being consumed.
  void SetCeiling(int ceiling);

  int Count() const { return static_cast<int>(popups_.size()); }
  int Ceiling() const { return ceiling_; }
  bool AtCap() const { return Count() >= ceiling_; }
  // True once per close attempt, so the app can answer with a bubble.
  bool ConsumeCloseAttempt();

  static constexpr int kMaximumPopups = 67;

 private:
  struct Popup {
    PopupSwarm* owner = nullptr;
    HWND window = nullptr;
    HWND label = nullptr;
    HWND button = nullptr;
    HFONT font = nullptr;
    double jiggleUntil = -1.0;
    int refusals = 0;
    bool dead = false;
  };

  static ATOM Register(HINSTANCE instance);
  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(Popup& popup, HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  void RequestClose(Popup& popup);
  bool CreatePopup(HINSTANCE instance, std::mt19937& random);

  std::vector<std::unique_ptr<Popup>> popups_;
  double lastTickTime_ = 0.0;
  int pendingSpawns_ = 0;
  int closeAttempts_ = 0;
  int spawnCounter_ = 0;
  int ceiling_ = kMaximumPopups;
};

// A small set of genuine Windows utilities launched by GooseRot. Only process
// IDs created here are tracked, moved or asked to close; pre-existing user
// windows are never adopted.
class OwnedWindowsApps {
 public:
  OwnedWindowsApps() = default;
  ~OwnedWindowsApps();

  OwnedWindowsApps(const OwnedWindowsApps&) = delete;
  OwnedWindowsApps& operator=(const OwnedWindowsApps&) = delete;

  bool LaunchRandom(std::mt19937& random, double logicalTime);
  void Tick(std::mt19937& random, double logicalTime);
  void CloseAll();
  int Count() const { return static_cast<int>(processes_.size()); }

  static constexpr int kMaximumApps = 6;

 private:
  struct ProcessEntry {
    HANDLE process = nullptr;
    DWORD processId = 0;
    double launchedAt = 0.0;
    bool positioned = false;
  };

  std::vector<ProcessEntry> processes_;
};

// A goose foot planted on the Start button.
//
// One small always-on-top window is parked exactly over the Start button and
// swallows the clicks that land on it, because a Start menu opening mid-scene
// covers the whole experience. Nothing is hooked, no input is synthesised and
// no shell window is subclassed or moved: the guard is a window of our own that
// is destroyed during cleanup, after which the button behaves normally again.
// Ctrl+Shift+Esc, Alt+Tab and the Esc emergency exit are untouched.
class TaskbarGuard {
 public:
  TaskbarGuard() = default;
  ~TaskbarGuard();

  TaskbarGuard(const TaskbarGuard&) = delete;
  TaskbarGuard& operator=(const TaskbarGuard&) = delete;

  bool Show(HINSTANCE instance);
  // Re-locates the Start button and keeps the guard above the taskbar. Cheap
  // enough to call once a second; it does nothing if the geometry is unchanged.
  void Tick();
  void Close();
  bool IsOpen() const { return window_ != nullptr; }
  // True once per swallowed click, so the flock can take credit for it.
  bool ConsumePressAttempt();
  int PressAttempts() const { return totalAttempts_; }

 private:
  static ATOM Register(HINSTANCE instance);
  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  static bool FindStartButtonRect(RECT& rectangle);

  HWND window_ = nullptr;
  RECT placement_{};
  int pendingAttempts_ = 0;
  int totalAttempts_ = 0;
};

}  // namespace gooserot

