#include "desktop_director.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cwchar>
#include <new>
#include <process.h>

namespace gooserot {
namespace {

bool EqualsClass(HWND window, const wchar_t* expected) {
  wchar_t className[128]{};
  GetClassNameW(window, className, static_cast<int>(std::size(className)));
  return _wcsicmp(className, expected) == 0;
}

bool IsExcludedClass(HWND window) {
  constexpr std::array<const wchar_t*, 8> excluded = {
      L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Progman", L"WorkerW",
      L"TaskManagerWindow", L"TaskManagerMainWindow", L"Windows.UI.Core.CoreWindow",
      L"GooseRotOverlay"};
  return std::any_of(excluded.begin(), excluded.end(),
                     [window](const wchar_t* name) { return EqualsClass(window, name); });
}

bool SameRectangle(const RECT& left, const RECT& right) {
  return EqualRect(&left, &right) != FALSE;
}

struct WindowMutationRequest {
  HWND window = nullptr;
  RECT requested{};
  RECT fallback{};
  DWORD ownerProcessId = 0;
  DWORD ownerThreadId = 0;
  HANDLE cancelEvent = nullptr;
};

enum class MutationResult { Failed, Applied, Reverted, Pending };

unsigned __stdcall WindowMutationWorker(void* parameter) {
  auto* request = static_cast<WindowMutationRequest*>(parameter);
  DWORD processId = 0;
  const DWORD threadId = GetWindowThreadProcessId(request->window, &processId);
  if (processId != request->ownerProcessId || threadId != request->ownerThreadId ||
      IsHungAppWindow(request->window)) {
    delete request;
    return 0;
  }

  const BOOL applied = SetWindowPos(
      request->window, nullptr, request->requested.left, request->requested.top,
      request->requested.right - request->requested.left,
      request->requested.bottom - request->requested.top,
      SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
  RECT actual{};
  const bool exact = applied && GetWindowRect(request->window, &actual) &&
                     SameRectangle(actual, request->requested);

  // Keep a short acknowledgement window. If the UI did not observe the
  // result within its bound, this worker restores the original rectangle as
  // soon as the target responds.
  const bool cancelled = WaitForSingleObject(request->cancelEvent, 25) == WAIT_OBJECT_0;
  if (!exact || cancelled) {
    const BOOL reverted = SetWindowPos(
        request->window, nullptr, request->fallback.left, request->fallback.top,
        request->fallback.right - request->fallback.left,
        request->fallback.bottom - request->fallback.top,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
    RECT revertedRectangle{};
    const bool exactFallback = reverted && GetWindowRect(request->window, &revertedRectangle) &&
                               SameRectangle(revertedRectangle, request->fallback);
    delete request;
    return exactFallback ? 2U : 0U;
  }

  delete request;
  return 1U;
}

MutationResult MutateWindowBounded(HWND window, const RECT& requested, const RECT& fallback,
                                   DWORD ownerProcessId, DWORD ownerThreadId,
                                   HANDLE& pendingThread, HANDLE& pendingCancelEvent) {
  pendingThread = nullptr;
  pendingCancelEvent = nullptr;
  if (!window || IsHungAppWindow(window)) return MutationResult::Failed;

  HANDLE cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!cancelEvent) return MutationResult::Failed;
  auto* request = new (std::nothrow)
      WindowMutationRequest{window, requested, fallback, ownerProcessId, ownerThreadId, cancelEvent};
  if (!request) {
    CloseHandle(cancelEvent);
    return MutationResult::Failed;
  }
  const uintptr_t rawThread = _beginthreadex(nullptr, 0, &WindowMutationWorker, request, 0, nullptr);
  if (!rawThread) {
    delete request;
    CloseHandle(cancelEvent);
    return MutationResult::Failed;
  }
  HANDLE thread = reinterpret_cast<HANDLE>(rawThread);
  const DWORD waitResult = WaitForSingleObject(thread, 200);
  if (waitResult == WAIT_OBJECT_0) {
    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    CloseHandle(cancelEvent);
    if (exitCode == 1) return MutationResult::Applied;
    if (exitCode == 2) return MutationResult::Reverted;
    return MutationResult::Failed;
  }
  if (waitResult == WAIT_TIMEOUT) {
    SetEvent(cancelEvent);
    pendingThread = thread;
    pendingCancelEvent = cancelEvent;
    return MutationResult::Pending;
  }
  SetEvent(cancelEvent);
  pendingThread = thread;
  pendingCancelEvent = cancelEvent;
  return MutationResult::Pending;
}

}  // namespace

DesktopDirector::DesktopDirector(bool enabled, bool primaryMonitorOnly, RecoveryWatchdog* watchdog)
    : enabled_(enabled), primaryMonitorOnly_(primaryMonitorOnly), processId_(GetCurrentProcessId()),
      initialForeground_(GetForegroundWindow()), watchdog_(watchdog) {
  GetCursorPos(&initialCursor_);
  initialForegroundThreadId_ =
      GetWindowThreadProcessId(initialForeground_, &initialForegroundProcessId_);
}

DesktopDirector::~DesktopDirector() {
  Restore();
  for (auto& item : originalWindows_) {
    // The process is exiting. Signal fallback, but leave live handles valid for
    // any worker still unwinding; ExitProcess will reclaim them immediately.
    if (item.second.pendingMutationCancelEvent) SetEvent(item.second.pendingMutationCancelEvent);
  }
}

BOOL CALLBACK DesktopDirector::EnumerateCallback(HWND window, LPARAM parameter) {
  auto* pair = reinterpret_cast<std::pair<const DesktopDirector*, std::vector<WindowTarget>*>*>(parameter);
  if (!pair->first->IsEligibleWindow(window)) return TRUE;

  RECT rectangle{};
  if (!GetWindowRect(window, &rectangle)) return TRUE;
  WindowTarget target;
  target.handle = window;
  target.rectangle = rectangle;
  target.ownerThreadId = GetWindowThreadProcessId(window, &target.ownerProcessId);
  target.titleBarPoint = {(rectangle.left + rectangle.right) / 2,
                          rectangle.top + std::min(18L, (rectangle.bottom - rectangle.top) / 2)};
  pair->second->push_back(target);
  return TRUE;
}

bool DesktopDirector::IsEligibleWindow(HWND window) const {
  if (!IsWindowVisible(window) || IsIconic(window) || IsZoomed(window) ||
      IsHungAppWindow(window) || IsExcludedClass(window)) return false;
  if (originalWindows_.find(window) != originalWindows_.end()) return false;
  if (GetWindow(window, GW_OWNER) != nullptr) return false;

  DWORD ownerProcess = 0;
  GetWindowThreadProcessId(window, &ownerProcess);
  if (ownerProcess == 0 || ownerProcess == processId_) return false;

  const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
  const LONG_PTR extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
  if ((style & WS_CAPTION) == 0 || (style & WS_DISABLED) != 0 ||
      (extendedStyle & WS_EX_TOOLWINDOW) != 0) {
    return false;
  }

  RECT rectangle{};
  if (!GetWindowRect(window, &rectangle)) return false;
  if (rectangle.right - rectangle.left < 160 || rectangle.bottom - rectangle.top < 100) return false;
  MONITORINFO monitorInfo{};
  monitorInfo.cbSize = sizeof(monitorInfo);
  const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
  if (primaryMonitorOnly_ && monitor != MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY)) return false;
  if (!GetMonitorInfoW(monitor, &monitorInfo)) return false;
  return rectangle.left >= monitorInfo.rcWork.left && rectangle.top >= monitorInfo.rcWork.top &&
         rectangle.right <= monitorInfo.rcWork.right && rectangle.bottom <= monitorInfo.rcWork.bottom;
}

std::vector<WindowTarget> DesktopDirector::EnumerateWindows() const {
  std::vector<WindowTarget> result;
  std::pair<const DesktopDirector*, std::vector<WindowTarget>*> context{this, &result};
  EnumWindows(&DesktopDirector::EnumerateCallback, reinterpret_cast<LPARAM>(&context));
  return result;
}

std::optional<WindowTarget> DesktopDirector::PickRandomWindow(std::mt19937& random) const {
  if (!enabled_) return std::nullopt;
  auto windows = EnumerateWindows();
  if (windows.empty()) return std::nullopt;
  std::uniform_int_distribution<std::size_t> distribution(0, windows.size() - 1);
  return windows[distribution(random)];
}

bool DesktopDirector::MoveWindowBy67(const WindowTarget& target, int direction) {
  const HWND window = target.handle;
  if (!enabled_ || !RecoveryHealthy() || !window || !IsEligibleWindow(window)) return false;

  DWORD ownerProcessId = 0;
  const DWORD ownerThreadId = GetWindowThreadProcessId(window, &ownerProcessId);
  if (ownerProcessId != target.ownerProcessId || ownerThreadId != target.ownerThreadId) return false;

  RECT current{};
  if (!GetWindowRect(window, &current) || !SameRectangle(current, target.rectangle)) return false;
  constexpr std::array<POINT, 4> offsets = {{{67, 0}, {-67, 0}, {0, 67}, {0, -67}}};
  MONITORINFO monitorInfo{};
  monitorInfo.cbSize = sizeof(monitorInfo);
  const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
  if (!GetMonitorInfoW(monitor, &monitorInfo)) return false;
  const RectF workArea{static_cast<float>(monitorInfo.rcWork.left), static_cast<float>(monitorInfo.rcWork.top),
                       static_cast<float>(monitorInfo.rcWork.right), static_cast<float>(monitorInfo.rcWork.bottom)};
  for (std::size_t attempt = 0; attempt < offsets.size(); ++attempt) {
    const POINT offset = offsets[(static_cast<std::size_t>(direction) + attempt) % offsets.size()];
    const RectF moved{static_cast<float>(current.left + offset.x), static_cast<float>(current.top + offset.y),
                      static_cast<float>(current.right + offset.x), static_cast<float>(current.bottom + offset.y)};
    const RectF clamped = ClampWindowRect(moved, workArea);
    if (clamped.left != moved.left || clamped.top != moved.top ||
        clamped.right != moved.right || clamped.bottom != moved.bottom) {
      continue;
    }
    const OriginalWindowState original{current, ownerProcessId, ownerThreadId, {}, false,
                                       false, nullptr, nullptr};
    const auto inserted = originalWindows_.try_emplace(window, original);
    if (inserted.second && watchdog_) {
      watchdog_->RecordWindow(window, current, ownerProcessId, ownerThreadId);
    }
    RECT expected{static_cast<LONG>(moved.left), static_cast<LONG>(moved.top),
                  static_cast<LONG>(moved.right), static_cast<LONG>(moved.bottom)};
    inserted.first->second.lastAppliedRectangle = expected;
    inserted.first->second.hasAppliedRectangle = true;
    if (watchdog_) watchdog_->PublishWindowCandidate(window, expected);
    if (watchdog_) watchdog_->SetWindowMutationInProgress(window, true);
    HANDLE pendingThread = nullptr;
    HANDLE pendingCancelEvent = nullptr;
    const MutationResult result = MutateWindowBounded(
        window, expected, current, ownerProcessId, ownerThreadId,
        pendingThread, pendingCancelEvent);
    if (result == MutationResult::Pending) {
      inserted.first->second.mutationUnsettled = true;
      inserted.first->second.pendingMutationThread = pendingThread;
      inserted.first->second.pendingMutationCancelEvent = pendingCancelEvent;
      return false;
    }
    RECT actual{};
    if (result == MutationResult::Applied) {
      if (GetWindowRect(window, &actual) && SameRectangle(actual, expected)) {
        if (watchdog_) watchdog_->SetWindowMutationInProgress(window, false);
        return true;
      }
    }
    if (GetWindowRect(window, &actual) && SameRectangle(actual, current)) {
      inserted.first->second.hasAppliedRectangle = false;
      inserted.first->second.mutationUnsettled = false;
      if (watchdog_) watchdog_->SetWindowMutationInProgress(window, false);
    } else {
      inserted.first->second.mutationUnsettled = true;
      if (GetWindowRect(window, &actual)) {
        inserted.first->second.lastAppliedRectangle = actual;
        if (watchdog_) watchdog_->PublishWindowCandidate(window, actual);
      }
    }
    return false;
  }
  return false;
}

bool DesktopDirector::PushCursorRight67() {
  if (!enabled_ || !RecoveryHealthy()) return false;
  POINT cursor{};
  if (!GetCursorPos(&cursor)) return false;
  MONITORINFO monitorInfo{};
  monitorInfo.cbSize = sizeof(monitorInfo);
  const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
  if (!GetMonitorInfoW(monitor, &monitorInfo)) return false;
  int x = cursor.x;
  if (cursor.x + 67 <= monitorInfo.rcMonitor.right - 1) x = cursor.x + 67;
  else if (cursor.x - 67 >= monitorInfo.rcMonitor.left) x = cursor.x - 67;
  else return false;
  const int y = cursor.y;
  if (watchdog_) watchdog_->MarkCursorMoved();
  SetCursorPos(x, y);
  POINT actual{};
  const bool moved = GetCursorPos(&actual) && actual.x == x && actual.y == y &&
                     std::abs(actual.x - cursor.x) == 67;
  cursorMoved_ = cursorMoved_ || moved || actual.x != cursor.x || actual.y != cursor.y;
  if (!moved) SetCursorPos(cursor.x, cursor.y);
  return moved;
}

POINT DesktopDirector::CursorPosition() const {
  POINT cursor{};
  GetCursorPos(&cursor);
  return cursor;
}

void DesktopDirector::PollPendingMutations() {
  for (auto& item : originalWindows_) {
    OriginalWindowState& state = item.second;
    if (state.pendingMutationThread) {
      if (WaitForSingleObject(state.pendingMutationThread, 0) != WAIT_OBJECT_0) continue;
      CloseHandle(state.pendingMutationThread);
      CloseHandle(state.pendingMutationCancelEvent);
      state.pendingMutationThread = nullptr;
      state.pendingMutationCancelEvent = nullptr;
    } else if (!state.mutationUnsettled) {
      continue;
    }

    if (!IsWindow(item.first)) {
      state.mutationUnsettled = false;
      continue;
    }
    DWORD processId = 0;
    const DWORD threadId = GetWindowThreadProcessId(item.first, &processId);
    if (processId != state.ownerProcessId || threadId != state.ownerThreadId) {
      state.mutationUnsettled = false;
      continue;
    }
    RECT current{};
    if (!GetWindowRect(item.first, &current)) continue;
    if (SameRectangle(current, state.rectangle)) {
      state.hasAppliedRectangle = false;
      state.mutationUnsettled = false;
      if (watchdog_) watchdog_->SetWindowMutationInProgress(item.first, false);
      continue;
    }

    // A worker did not manage to restore its fallback. Attribute this
    // immediately observed rectangle to our in-flight mutation and retry.
    state.lastAppliedRectangle = current;
    state.hasAppliedRectangle = true;
    state.mutationUnsettled = true;
    if (watchdog_) watchdog_->PublishWindowCandidate(item.first, current);
    HANDLE pendingThread = nullptr;
    HANDLE pendingCancelEvent = nullptr;
    const MutationResult result = MutateWindowBounded(
        item.first, state.rectangle, state.rectangle, state.ownerProcessId,
        state.ownerThreadId, pendingThread, pendingCancelEvent);
    if (result == MutationResult::Pending) {
      state.pendingMutationThread = pendingThread;
      state.pendingMutationCancelEvent = pendingCancelEvent;
      continue;
    }
    RECT restored{};
    if (GetWindowRect(item.first, &restored) && SameRectangle(restored, state.rectangle)) {
      state.hasAppliedRectangle = false;
      state.mutationUnsettled = false;
      if (watchdog_) watchdog_->SetWindowMutationInProgress(item.first, false);
    }
  }
}

bool DesktopDirector::Restore() {
  if (restored_) return true;
  PollPendingMutations();
  bool allRestored = true;
  if (enabled_ && cursorMoved_) {
    SetCursorPos(initialCursor_.x, initialCursor_.y);
    POINT actual{};
    if (!GetCursorPos(&actual) || actual.x != initialCursor_.x || actual.y != initialCursor_.y) {
      allRestored = false;
    }
  }
  for (auto& item : originalWindows_) {
    if (!IsWindow(item.first)) continue;
    DWORD ownerProcessId = 0;
    const DWORD ownerThreadId = GetWindowThreadProcessId(item.first, &ownerProcessId);
    if (ownerProcessId != item.second.ownerProcessId || ownerThreadId != item.second.ownerThreadId) continue;
    if (item.second.pendingMutationThread) {
      allRestored = false;
      continue;
    }
    RECT current{};
    if (!item.second.hasAppliedRectangle) continue;
    if (!GetWindowRect(item.first, &current)) {
      allRestored = false;
      continue;
    }
    if (!SameRectangle(current, item.second.lastAppliedRectangle)) {
      if (item.second.mutationUnsettled) allRestored = false;
      continue;
    }
    const RECT& rectangle = item.second.rectangle;
    if (watchdog_) watchdog_->SetWindowMutationInProgress(item.first, true);
    HANDLE pendingThread = nullptr;
    HANDLE pendingCancelEvent = nullptr;
    const MutationResult result = MutateWindowBounded(
        item.first, rectangle, rectangle, item.second.ownerProcessId,
        item.second.ownerThreadId, pendingThread, pendingCancelEvent);
    if (result == MutationResult::Pending) {
      item.second.pendingMutationThread = pendingThread;
      item.second.pendingMutationCancelEvent = pendingCancelEvent;
      item.second.mutationUnsettled = true;
      allRestored = false;
    } else {
      RECT restored{};
      if (!GetWindowRect(item.first, &restored) || !SameRectangle(restored, rectangle)) {
        item.second.mutationUnsettled = true;
        allRestored = false;
      } else {
        item.second.hasAppliedRectangle = false;
        item.second.mutationUnsettled = false;
        if (watchdog_) watchdog_->SetWindowMutationInProgress(item.first, false);
      }
    }
  }
  if (enabled_ && initialForeground_ && IsWindow(initialForeground_)) {
    DWORD ownerProcessId = 0;
    const DWORD ownerThreadId = GetWindowThreadProcessId(initialForeground_, &ownerProcessId);
    if (ownerProcessId == initialForegroundProcessId_ && ownerThreadId == initialForegroundThreadId_) {
      SetForegroundWindow(initialForeground_);
    }
  }
  if (allRestored) {
    restored_ = true;
    originalWindows_.clear();
    if (watchdog_) watchdog_->Complete();
  }
  return allRestored;
}

}  // namespace gooserot
