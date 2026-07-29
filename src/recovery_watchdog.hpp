#pragma once

#include <windows.h>

#include <string>

namespace gooserot {

class RecoveryWatchdog {
 public:
  RecoveryWatchdog() = default;
  ~RecoveryWatchdog();

  RecoveryWatchdog(const RecoveryWatchdog&) = delete;
  RecoveryWatchdog& operator=(const RecoveryWatchdog&) = delete;

  bool Start(std::wstring& error);
  void RecordWindow(HWND window, const RECT& originalRectangle,
                    DWORD ownerProcessId, DWORD ownerThreadId);
  void PublishWindowCandidate(HWND window, const RECT& appliedRectangle);
  void SetWindowMutationInProgress(HWND window, bool inProgress);
  void MarkCursorMoved();
  void Complete();
  bool IsRunning() const { return state_ != nullptr; }
  bool IsHealthy() const;

  static int RunChild(DWORD parentProcessId, const wchar_t* mappingName,
                      const wchar_t* readyEventName);

 private:
  static constexpr LONG kMagic = 0x47523637;
  static constexpr LONG kMaximumWindows = 64;

  struct WindowEntry {
    HWND window;
    RECT rectangle;
    DWORD ownerProcessId;
    DWORD ownerThreadId;
    RECT appliedRectangles[2];
    volatile LONG appliedRectangleCount;
    volatile LONG mutationInProgress;
  };

  struct SharedState {
    LONG magic;
    volatile LONG cleanExit;
    volatile LONG cursorMoved;
    volatile LONG windowCount;
    DWORD parentProcessId;
    POINT initialCursor;
    HWND initialForeground;
    DWORD initialForegroundProcessId;
    DWORD initialForegroundThreadId;
    WindowEntry windows[kMaximumWindows];
  };

  HANDLE mapping_ = nullptr;
  HANDLE childProcess_ = nullptr;
  SharedState* state_ = nullptr;
  std::wstring mappingName_;
};

}  // namespace gooserot
