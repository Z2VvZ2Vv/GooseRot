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

// The fake Bloc-notes. It belongs to GooseRot, it types on its own, and it
// declines to be closed for a while — `Close()` still destroys it outright, so
// cleanup and the emergency exit are never blocked.
class NotepadWindow {
 public:
  NotepadWindow() = default;
  ~NotepadWindow();

  bool Show(HINSTANCE instance);
  void SetText(const std::wstring& text);
  void Minimize();
  void Close();
  void Tick(double logicalTime);
  bool IsOpen() const { return window_ != nullptr; }
  // True once per refused close, so the goose can gloat about it.
  bool ConsumeRefusal();
  int Refusals() const { return refusals_; }

 private:
  static ATOM Register(HINSTANCE instance);
  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  void RefuseClose();

  HINSTANCE instance_ = nullptr;
  HWND window_ = nullptr;
  HWND edit_ = nullptr;
  HFONT font_ = nullptr;
  int refusals_ = 0;
  int pendingRefusalReports_ = 0;
  bool respawnQueued_ = false;
  bool respawnUsed_ = false;
};

// A bounded swarm of GooseRot popups. Closing one spawns two more until the cap
// is reached; after that they close normally so the desktop can drain. Every
// window is owned by this process and is destroyed by CloseAll().
class PopupSwarm {
 public:
  PopupSwarm() = default;
  ~PopupSwarm();

  PopupSwarm(const PopupSwarm&) = delete;
  PopupSwarm& operator=(const PopupSwarm&) = delete;

  // Adds up to `count` popups, never exceeding the internal cap.
  void Spawn(HINSTANCE instance, std::mt19937& random, int count);
  // Applies queued multiplications, reaps closed windows and jiggles refusals.
  void Tick(HINSTANCE instance, std::mt19937& random, double logicalTime);
  void CloseAll();

  int Count() const { return static_cast<int>(popups_.size()); }
  bool AtCap() const { return Count() >= kMaximumPopups; }
  // True once per close attempt, so the app can answer with a bubble.
  bool ConsumeCloseAttempt();

  static constexpr int kMaximumPopups = 9;

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
};

}  // namespace gooserot

