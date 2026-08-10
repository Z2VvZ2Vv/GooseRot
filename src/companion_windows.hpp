#pragma once

#include <windows.h>

#include <memory>
#include <random>
#include <string>
#include <vector>

#include "core.hpp"

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
  void LayoutControls();
  void RefreshFont();

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
  float layoutScale_ = 1.0f;
};

// The inspector's case file: a text window that belongs to GooseRot, opens
// where the goose stamped the desktop, is typed into by the goose itself, and
// declines to be closed for a while. It cannot be parked in the taskbar either:
// there is no minimise box, SC_MINIMIZE is swallowed and anything that
// iconifies it from outside (Show Desktop, the taskbar button) is undone on the
// next tick. `Close()` still destroys it outright, so cleanup and the emergency
// exit are never blocked.
class NotepadWindow {
 public:
  NotepadWindow() = default;
  ~NotepadWindow();

  // `anchor` is where the goose's beak was, in screen coordinates: the file
  // opens under it so the window visibly comes from the goose. A null anchor
  // centres the window as before.
  bool Show(HINSTANCE instance, const POINT* anchor = nullptr);
  void SetText(const std::wstring& text);
  void Close();
  void Tick(double logicalTime);
  bool IsOpen() const { return window_ != nullptr; }
  // True once per refused close, so the goose can gloat about it.
  bool ConsumeRefusal();
  int Refusals() const { return refusals_; }
  // True once per refused minimise, for the same reason.
  bool ConsumeMinimiseRefusal();
  // Screen point where paperwork leaves the case file. False while closed.
  bool TryGetIssuePoint(POINT& point) const;

 private:
  static ATOM Register(HINSTANCE instance);
  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  void RefuseClose();
  void RefuseMinimise();
  void RefreshFont();

  HINSTANCE instance_ = nullptr;
  HWND window_ = nullptr;
  HWND edit_ = nullptr;
  HFONT font_ = nullptr;
  int refusals_ = 0;
  int pendingRefusalReports_ = 0;
  int pendingMinimiseReports_ = 0;
  float layoutScale_ = 1.0f;
  bool respawnQueued_ = false;
  bool respawnUsed_ = false;
};

// A small set of genuine Windows utilities launched by GooseRot. Only process
// IDs created here are tracked, moved or asked to close; pre-existing user
// windows are never adopted.
class OwnedWindowsApps {
 public:
  OwnedWindowsApps();
  ~OwnedWindowsApps();

  OwnedWindowsApps(const OwnedWindowsApps&) = delete;
  OwnedWindowsApps& operator=(const OwnedWindowsApps&) = delete;

  bool LaunchRandom(std::mt19937& random, double clockSeconds,
                    bool stableProcessOnly = false);
  void Tick(std::mt19937& random, double clockSeconds);
  void RequestCloseOlderThan(double clockSeconds, double maximumAgeSeconds);
  void CloseAll();
  int Count() const { return static_cast<int>(processes_.size()); }

  static constexpr int kMaximumApps = 6;

 private:
  struct ProcessEntry {
    HANDLE process = nullptr;
    DWORD processId = 0;
    double launchedAt = 0.0;
    bool positioned = false;
    double nextCloseAttemptAt = 0.0;
  };

  std::vector<ProcessEntry> processes_;
  HANDLE job_ = nullptr;
  double nextWindowEnumerationAt_ = 0.0;
};

// A bounded swarm of compact GooseRot notices. They deliberately use small
// modeless Win32 windows instead of system MessageBoxW dialogs, so a hundred of
// them can fill the desktop without blocking the timeline or creating a hundred
// worker threads. Closing one before the finale spawns two replacements; the
// final countdown closes them directly, one by one.
//
// Placement is immediate and deterministic. The first cell is near the case
// file, then each next cell is chosen as far as possible from those already
// selected, so early notices are spread across the desktop without random
// jitter. A cadence limiter still allows only one new window at a time.
class PopupSwarm {
 public:
  PopupSwarm() = default;
  ~PopupSwarm();

  PopupSwarm(const PopupSwarm&) = delete;
  PopupSwarm& operator=(const PopupSwarm&) = delete;

  void SetBounds(RECT bounds);
  void SetCapacity(std::size_t capacity);
  void SetOrigin(POINT origin) { origin_ = origin; }
  void Tick(HINSTANCE instance, std::mt19937& random, double logicalTime);
  void CloseAll();
  int Count() const { return static_cast<int>(popups_.size()); }
  bool AtCap() const { return Count() >= static_cast<int>(capacity_); }
  std::size_t Capacity() const { return capacity_; }
  bool ConsumeCloseAttempt();

 private:
  struct Popup {
    PopupSwarm* owner = nullptr;
    HWND window = nullptr;
    HWND label = nullptr;
    HWND button = nullptr;
    HFONT font = nullptr;
    POINT to{};
    int slot = -1;
    int fontHeight = 0;
    int width = 348;
    int height = 186;
    UINT dpi = 96U;
    float layoutScale = 1.0f;
    bool dead = false;
  };

  static ATOM Register(HINSTANCE instance);
  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(Popup& popup, HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  void RequestClose(Popup& popup);
  bool CreatePopup(HINSTANCE instance);
  void ReapClosed();
  void CloseNewest();
  void ReleaseSlot(int slot);
  void RebuildLayout();
  void LayoutPopup(Popup& popup);
  void EnsureSlotOrder();
  int AcquireSlot();
  POINT SlotPosition(int slot) const;
  double SpawnInterval(double logicalTime) const;

  std::vector<std::unique_ptr<Popup>> popups_;
  std::vector<int> slotOrder_;
  std::vector<unsigned char> slotTaken_;
  RECT bounds_{};
  POINT origin_{};
  POINT slotOrderOrigin_{};
  int columns_ = 1;
  int rows_ = 1;
  double lastTickTime_ = 0.0;
  double nextSpawnAt_ = -1.0;
  double nextCloseAt_ = -1.0;
  int pendingSpawns_ = 0;
  int closeAttempts_ = 0;
  int permanentDismissals_ = 0;
  int spawnCounter_ = 0;
  std::size_t capacity_ = kMaximumPopups;
  float layoutScale_ = 1.0f;
  int popupWidth_ = 348;
  int popupHeight_ = 186;
  bool closing_ = false;
};

// An invisible input shield placed over the Start button.
//
// One small always-on-top window is parked exactly over the Start button and
// swallows the clicks that land on it, because a Start menu opening mid-scene
// covers the whole experience. The shield is visually imperceptible and keeps
// the normal arrow cursor. No shell window is subclassed or moved: the guard is
// a window of our own that is destroyed during cleanup, after which the button
// behaves normally again.
//
// Pure geometry fallback shared with the integration tests. Real Start HWND or
// UI Automation bounds take priority; these candidates cover Windows 7/10 and
// Windows 11 with either left or centred taskbar alignment.
bool SelectTaskbarStartButtonRect(const RECT& taskbar, bool centred,
                                  const std::vector<RECT>& buttonCandidates,
                                  RECT& rectangle);

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
  ULONGLONG nextPlacementCheckAt_ = 0;
  int pendingAttempts_ = 0;
  int totalAttempts_ = 0;
};

// Pure policy used by both the hook and integration tests. The guard suppresses
// only the two Windows keys and only for keyboard messages; it never records or
// synthesises input.
bool ShouldSuppressWindowsKey(WPARAM message, DWORD virtualKey);

// Session-scoped guard used only after the user selects the full experience.
// Alt+Tab, Ctrl+Shift+Esc, Esc and every non-Windows key continue normally.
class WindowsKeyGuard {
 public:
  WindowsKeyGuard() = default;
  ~WindowsKeyGuard();

  WindowsKeyGuard(const WindowsKeyGuard&) = delete;
  WindowsKeyGuard& operator=(const WindowsKeyGuard&) = delete;

  bool Install(HINSTANCE instance);
  void Close();
  bool IsInstalled() const { return hook_ != nullptr; }

 private:
  static LRESULT CALLBACK HookProcedure(int code, WPARAM wParam, LPARAM lParam);

  HHOOK hook_ = nullptr;
};

}  // namespace gooserot

