#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

#include "core.hpp"
#include "recovery_watchdog.hpp"

namespace gooserot {

struct WindowTarget {
  HWND handle = nullptr;
  RECT rectangle{};
  POINT titleBarPoint{};
  DWORD ownerProcessId = 0;
  DWORD ownerThreadId = 0;
};

class DesktopDirector {
 public:
  DesktopDirector(bool enabled, bool primaryMonitorOnly, RecoveryWatchdog* watchdog);
  ~DesktopDirector();

  DesktopDirector(const DesktopDirector&) = delete;
  DesktopDirector& operator=(const DesktopDirector&) = delete;

  std::optional<WindowTarget> PickRandomWindow(std::mt19937& random) const;
  bool MoveWindowBy67(const WindowTarget& target, int direction);
  // Drags the pointer one step from wherever it currently is and reports how
  // far it actually travelled. Called every frame while the goose has the
  // cursor in its beak, so a user fighting back simply gets dragged from the
  // new position instead of cancelling the whole gag.
  int DragCursorBy(int deltaX, int deltaY);
  POINT CursorPosition() const;
  void PollPendingMutations();
  bool Restore();
  bool Enabled() const { return enabled_; }
  void SetPrimaryMonitorOnly(bool primaryMonitorOnly) {
    primaryMonitorOnly_ = primaryMonitorOnly;
  }
  bool RecoveryHealthy() const { return !enabled_ || restored_ || (watchdog_ && watchdog_->IsHealthy()); }

 private:
  struct OriginalWindowState {
    RECT rectangle{};
    DWORD ownerProcessId = 0;
    DWORD ownerThreadId = 0;
    RECT lastAppliedRectangle{};
    bool hasAppliedRectangle = false;
    bool mutationUnsettled = false;
    HANDLE pendingMutationThread = nullptr;
    HANDLE pendingMutationCancelEvent = nullptr;
  };

  static BOOL CALLBACK EnumerateCallback(HWND window, LPARAM parameter);
  bool IsEligibleWindow(HWND window) const;
  std::vector<WindowTarget> EnumerateWindows() const;

  bool enabled_ = false;
  bool restored_ = false;
  bool cursorMoved_ = false;
  bool primaryMonitorOnly_ = false;
  DWORD processId_ = 0;
  POINT initialCursor_{};
  HWND initialForeground_ = nullptr;
  DWORD initialForegroundProcessId_ = 0;
  DWORD initialForegroundThreadId_ = 0;
  std::unordered_map<HWND, OriginalWindowState> originalWindows_;
  RecoveryWatchdog* watchdog_ = nullptr;
};

}  // namespace gooserot
