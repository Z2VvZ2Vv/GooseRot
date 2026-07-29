#pragma once

#include <windows.h>

#include <random>
#include <string>

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

class NotepadWindow {
 public:
  NotepadWindow() = default;
  ~NotepadWindow();

  bool Show(HINSTANCE instance);
  void SetText(const std::wstring& text);
  void Minimize();
  void Close();
  bool IsOpen() const { return window_ != nullptr; }

 private:
  static ATOM Register(HINSTANCE instance);
  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

  HWND window_ = nullptr;
  HWND edit_ = nullptr;
  HFONT font_ = nullptr;
};

}  // namespace gooserot

