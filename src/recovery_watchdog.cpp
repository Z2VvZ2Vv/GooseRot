#include "recovery_watchdog.hpp"

#include <algorithm>
#include <array>
#include <cwchar>
#include <sstream>
#include <vector>

namespace gooserot {
namespace {

LONG ReadAtomic(const volatile LONG* value) {
  return InterlockedCompareExchange(const_cast<volatile LONG*>(value), 0, 0);
}

}  // namespace

RecoveryWatchdog::~RecoveryWatchdog() {
  // An armed state is deliberately left dirty: if ordinary cleanup did not
  // call Complete(), the child retries restoration after this process exits.
  if (state_) UnmapViewOfFile(state_);
  if (mapping_) CloseHandle(mapping_);
  if (childProcess_) CloseHandle(childProcess_);
}

bool RecoveryWatchdog::Start(std::wstring& error) {
  if (state_) return true;

  LARGE_INTEGER counter{};
  QueryPerformanceCounter(&counter);
  std::wostringstream name;
  name << L"Local\\GooseRot.Recovery." << GetCurrentProcessId() << L'.'
       << static_cast<unsigned long long>(counter.QuadPart);
  mappingName_ = name.str();

  mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                static_cast<DWORD>(sizeof(SharedState)), mappingName_.c_str());
  if (!mapping_) {
    error = L"Impossible de créer l'état partagé du watchdog de restauration.";
    return false;
  }
  state_ = static_cast<SharedState*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
  if (!state_) {
    error = L"Impossible d'ouvrir l'état partagé du watchdog de restauration.";
    CloseHandle(mapping_);
    mapping_ = nullptr;
    return false;
  }
  ZeroMemory(state_, sizeof(SharedState));
  state_->magic = kMagic;
  state_->parentProcessId = GetCurrentProcessId();
  state_->initialForeground = GetForegroundWindow();
  state_->initialForegroundThreadId =
      GetWindowThreadProcessId(state_->initialForeground, &state_->initialForegroundProcessId);
  GetCursorPos(&state_->initialCursor);

  const std::wstring readyEventName = mappingName_ + L".Ready";
  HANDLE readyEvent = CreateEventW(nullptr, TRUE, FALSE, readyEventName.c_str());
  if (!readyEvent) {
    error = L"Impossible de créer le signal du watchdog de restauration.";
    Complete();
    return false;
  }

  wchar_t executable[MAX_PATH]{};
  const DWORD executableLength = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
  if (executableLength == 0 || executableLength >= std::size(executable)) {
    error = L"Impossible de localiser GooseRot pour démarrer le watchdog.";
    CloseHandle(readyEvent);
    Complete();
    return false;
  }

  std::wostringstream command;
  command << L'"' << executable << L"\" --recovery-watchdog " << GetCurrentProcessId()
          << L" \"" << mappingName_ << L"\" \"" << readyEventName << L'"';
  std::wstring mutableCommand = command.str();
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  const BOOL created = CreateProcessW(executable, mutableCommand.data(), nullptr, nullptr, FALSE,
                                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
  if (!created) {
    error = L"Impossible de démarrer le watchdog de restauration.";
    CloseHandle(readyEvent);
    Complete();
    return false;
  }
  CloseHandle(process.hThread);
  const HANDLE waits[] = {readyEvent, process.hProcess};
  const DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, 2000);
  const bool childReady = waitResult == WAIT_OBJECT_0;
  CloseHandle(readyEvent);
  if (!childReady) {
    CloseHandle(process.hProcess);
    error = L"Le watchdog de restauration n'a pas confirmé son démarrage.";
    Complete();
    return false;
  }
  childProcess_ = process.hProcess;
  return true;
}

bool RecoveryWatchdog::IsHealthy() const {
  return state_ != nullptr && childProcess_ != nullptr &&
         WaitForSingleObject(childProcess_, 0) == WAIT_TIMEOUT;
}

void RecoveryWatchdog::RecordWindow(HWND window, const RECT& originalRectangle,
                                    DWORD ownerProcessId, DWORD ownerThreadId) {
  if (!state_ || !window) return;
  const LONG count = std::clamp<LONG>(ReadAtomic(&state_->windowCount), 0, kMaximumWindows);
  for (LONG index = 0; index < count; ++index) {
    if (state_->windows[index].window == window) return;
  }
  if (count >= kMaximumWindows) return;
  WindowEntry& entry = state_->windows[count];
  entry.window = window;
  entry.rectangle = originalRectangle;
  entry.ownerProcessId = ownerProcessId;
  entry.ownerThreadId = ownerThreadId;
  entry.appliedRectangles[0] = {};
  entry.appliedRectangles[1] = {};
  entry.appliedRectangleCount = 0;
  entry.mutationInProgress = 0;
  MemoryBarrier();
  InterlockedExchange(&state_->windowCount, count + 1);
}

void RecoveryWatchdog::SetWindowMutationInProgress(HWND window, bool inProgress) {
  if (!state_ || !window) return;
  const LONG count = std::clamp<LONG>(ReadAtomic(&state_->windowCount), 0, kMaximumWindows);
  for (LONG index = 0; index < count; ++index) {
    WindowEntry& entry = state_->windows[index];
    if (entry.window == window) {
      InterlockedExchange(&entry.mutationInProgress, inProgress ? 1 : 0);
      return;
    }
  }
}

void RecoveryWatchdog::PublishWindowCandidate(HWND window, const RECT& appliedRectangle) {
  if (!state_ || !window) return;
  const LONG count = std::clamp<LONG>(ReadAtomic(&state_->windowCount), 0, kMaximumWindows);
  for (LONG index = 0; index < count; ++index) {
    WindowEntry& entry = state_->windows[index];
    if (entry.window != window) continue;
    const LONG candidateCount = std::clamp<LONG>(ReadAtomic(&entry.appliedRectangleCount), 0, 2);
    for (LONG candidate = 0; candidate < candidateCount; ++candidate) {
      if (EqualRect(&entry.appliedRectangles[candidate], &appliedRectangle)) return;
    }
    if (candidateCount >= 2) return;
    entry.appliedRectangles[candidateCount] = appliedRectangle;
    MemoryBarrier();
    InterlockedCompareExchange(&entry.appliedRectangleCount, candidateCount + 1, candidateCount);
    return;
  }
}

void RecoveryWatchdog::MarkCursorMoved() {
  if (state_) InterlockedExchange(&state_->cursorMoved, 1);
}

void RecoveryWatchdog::Complete() {
  if (state_) {
    InterlockedExchange(&state_->cleanExit, 1);
    UnmapViewOfFile(state_);
    state_ = nullptr;
  }
  if (mapping_) {
    CloseHandle(mapping_);
    mapping_ = nullptr;
  }
  if (childProcess_) {
    CloseHandle(childProcess_);
    childProcess_ = nullptr;
  }
  mappingName_.clear();
}

int RecoveryWatchdog::RunChild(DWORD parentProcessId, const wchar_t* mappingName,
                               const wchar_t* readyEventName) {
  if (parentProcessId == 0 || !mappingName || !*mappingName ||
      !readyEventName || !*readyEventName) return 10;
  HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mappingName);
  if (!mapping) return 11;
  auto* state = static_cast<SharedState*>(
      MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
  if (!state || state->magic != kMagic || state->parentProcessId != parentProcessId) {
    if (state) UnmapViewOfFile(state);
    CloseHandle(mapping);
    return 12;
  }
  HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentProcessId);
  if (!parent) {
    UnmapViewOfFile(state);
    CloseHandle(mapping);
    return 13;
  }
  HANDLE readyEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, readyEventName);
  if (!readyEvent) {
    CloseHandle(parent);
    UnmapViewOfFile(state);
    CloseHandle(mapping);
    return 14;
  }
  SetEvent(readyEvent);
  CloseHandle(readyEvent);
  WaitForSingleObject(parent, INFINITE);
  CloseHandle(parent);

  if (ReadAtomic(&state->cleanExit) == 0) {
    const LONG count = std::clamp<LONG>(ReadAtomic(&state->windowCount), 0, kMaximumWindows);
    std::array<int, kMaximumWindows> stableOriginalScans{};
    bool cursorRecovered = ReadAtomic(&state->cursorMoved) == 0;
    bool recoveryComplete = false;
    while (!recoveryComplete) {
      recoveryComplete = true;
      if (!cursorRecovered) {
        const LONG virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const LONG virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const LONG virtualWidth = std::max<LONG>(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
        const LONG virtualHeight = std::max<LONG>(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
        const POINT desired{
            std::clamp(state->initialCursor.x, virtualLeft, virtualLeft + virtualWidth - 1),
            std::clamp(state->initialCursor.y, virtualTop, virtualTop + virtualHeight - 1)};
        SetCursorPos(desired.x, desired.y);
        POINT cursor{};
        cursorRecovered = GetCursorPos(&cursor) && cursor.x == desired.x && cursor.y == desired.y;
        recoveryComplete = recoveryComplete && cursorRecovered;
      }
      for (LONG index = 0; index < count; ++index) {
        WindowEntry& entry = state->windows[index];
        if (!IsWindow(entry.window)) continue;
        DWORD processId = 0;
        const DWORD threadId = GetWindowThreadProcessId(entry.window, &processId);
        if (processId != entry.ownerProcessId || threadId != entry.ownerThreadId) continue;
        RECT current{};
        if (!GetWindowRect(entry.window, &current)) {
          recoveryComplete = false;
          continue;
        }
        if (EqualRect(&current, &entry.rectangle)) {
          if (ReadAtomic(&entry.mutationInProgress) != 0 && stableOriginalScans[index] < 20) {
            ++stableOriginalScans[index];
            recoveryComplete = false;
          }
          continue;
        }
        stableOriginalScans[index] = 0;
        const LONG candidateCount =
            std::clamp<LONG>(ReadAtomic(&entry.appliedRectangleCount), 0, 2);
        bool ownedRectangle = ReadAtomic(&entry.mutationInProgress) != 0;
        for (LONG candidate = 0; candidate < candidateCount; ++candidate) {
          ownedRectangle = ownedRectangle || EqualRect(&current, &entry.appliedRectangles[candidate]);
        }
        if (!ownedRectangle) continue;
        recoveryComplete = false;
        if (IsHungAppWindow(entry.window)) continue;
        SetWindowPos(entry.window, nullptr, entry.rectangle.left, entry.rectangle.top,
                     entry.rectangle.right - entry.rectangle.left,
                     entry.rectangle.bottom - entry.rectangle.top,
                     SWP_ASYNCWINDOWPOS | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
      }
      if (!recoveryComplete) Sleep(100);
    }
    if (state->initialForeground && IsWindow(state->initialForeground)) {
      DWORD processId = 0;
      const DWORD threadId = GetWindowThreadProcessId(state->initialForeground, &processId);
      if (processId == state->initialForegroundProcessId &&
          threadId == state->initialForegroundThreadId) {
        SetForegroundWindow(state->initialForeground);
      }
    }
  }

  UnmapViewOfFile(state);
  CloseHandle(mapping);
  return 0;
}

}  // namespace gooserot
