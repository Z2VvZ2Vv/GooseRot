#include "companion_windows.hpp"

#include <algorithm>
#include <array>

namespace gooserot {
namespace {

constexpr int kPrimaryButtonId = 6701;
constexpr int kSecondaryButtonId = 6702;

void CenterWindow(HWND window) {
  RECT rectangle{};
  GetWindowRect(window, &rectangle);
  const int width = rectangle.right - rectangle.left;
  const int height = rectangle.bottom - rectangle.top;
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &info);
  const int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
  const int y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
  SetWindowPos(window, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

}  // namespace

PromptWindow::~PromptWindow() { Close(); }

ATOM PromptWindow::Register(HINSTANCE instance) {
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  if (GetClassInfoExW(instance, L"GooseRotPrompt", &windowClass)) return 1;
  windowClass.lpfnWndProc = &PromptWindow::WindowProcedure;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hIcon = LoadIconW(nullptr, IDI_WARNING);
  windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  windowClass.lpszClassName = L"GooseRotPrompt";
  return RegisterClassExW(&windowClass);
}

bool PromptWindow::Show(HINSTANCE instance, const wchar_t* title, const wchar_t* message,
                        const wchar_t* primaryLabel, const wchar_t* secondaryLabel,
                        bool evasivePrimary, double logicalTime) {
  Close();
  if (!Register(instance)) return false;
  result_ = PromptResult::None;
  evasivePrimary_ = evasivePrimary;
  shownAt_ = logicalTime;
  nextAutomaticMove_ = logicalTime + 2.0;
  cursorWasOverPrimary_ = false;
  moveIndex_ = 0;

  window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"GooseRotPrompt", title,
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                            CW_USEDEFAULT, CW_USEDEFAULT, 620, 260, nullptr, nullptr, instance, this);
  if (!window_) return false;
  message_ = CreateWindowExW(0, L"STATIC", message, WS_CHILD | WS_VISIBLE | SS_CENTER,
                             25, 26, 555, 92, window_, nullptr, instance, nullptr);
  primaryButton_ = CreateWindowExW(0, L"BUTTON", primaryLabel,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   90, 150, 140, 34, window_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPrimaryButtonId)), instance, nullptr);
  secondaryButton_ = CreateWindowExW(0, L"BUTTON", secondaryLabel,
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     390, 150, 140, 34, window_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSecondaryButtonId)), instance, nullptr);
  font_ = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  for (HWND control : {message_, primaryButton_, secondaryButton_}) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
  }
  CenterWindow(window_);
  ShowWindow(window_, SW_SHOWNORMAL);
  UpdateWindow(window_);
  return true;
}

void PromptWindow::Tick(double logicalTime) {
  if (!window_) return;
  if (logicalTime - shownAt_ >= 8.0) {
    Finish(PromptResult::Secondary);
    return;
  }
  if (!evasivePrimary_) return;

  POINT cursor{};
  RECT primaryRectangle{};
  GetCursorPos(&cursor);
  GetWindowRect(primaryButton_, &primaryRectangle);
  const bool isOver = PtInRect(&primaryRectangle, cursor) != FALSE;
  if (isOver && !cursorWasOverPrimary_) MovePrimaryButton();
  cursorWasOverPrimary_ = isOver;

  if (logicalTime >= nextAutomaticMove_) {
    MovePrimaryButton();
    nextAutomaticMove_ += 2.0;
  }
}

PromptResult PromptWindow::ConsumeResult() {
  const PromptResult current = result_;
  result_ = PromptResult::None;
  return current;
}

void PromptWindow::MovePrimaryButton() {
  if (!primaryButton_ || !window_) return;
  RECT button{};
  RECT client{};
  GetWindowRect(primaryButton_, &button);
  MapWindowPoints(HWND_DESKTOP, window_, reinterpret_cast<POINT*>(&button), 2);
  GetClientRect(window_, &client);
  constexpr std::array<POINT, 2> offsets = {{{100, 0}, {-100, 0}}};
  POINT offset = offsets[static_cast<std::size_t>(moveIndex_++) % offsets.size()];
  int x = button.left + offset.x;
  int y = button.top + offset.y;
  const int width = button.right - button.left;
  const int height = button.bottom - button.top;
  x = std::clamp(x, 12, std::max(12, static_cast<int>(client.right) - width - 12));
  y = std::clamp(y, 118, std::max(118, static_cast<int>(client.bottom) - height - 10));
  SetWindowPos(primaryButton_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
}

void PromptWindow::Finish(PromptResult result) {
  result_ = result;
  if (window_) DestroyWindow(window_);
}

void PromptWindow::Close() {
  if (window_) DestroyWindow(window_);
  if (font_) {
    DeleteObject(font_);
    font_ = nullptr;
  }
  window_ = nullptr;
  message_ = nullptr;
  primaryButton_ = nullptr;
  secondaryButton_ = nullptr;
}

LRESULT CALLBACK PromptWindow::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  PromptWindow* self = reinterpret_cast<PromptWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<PromptWindow*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->window_ = window;
  }
  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    return DefWindowProcW(window, message, wParam, lParam);
  }
  return self ? self->HandleMessage(message, wParam, lParam)
              : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT PromptWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_COMMAND:
      if (LOWORD(wParam) == kPrimaryButtonId) {
        Finish(PromptResult::Primary);
        return 0;
      }
      if (LOWORD(wParam) == kSecondaryButtonId) {
        Finish(PromptResult::Secondary);
        return 0;
      }
      break;
    case WM_CLOSE:
      Finish(PromptResult::Dismissed);
      return 0;
    case WM_DESTROY:
      window_ = nullptr;
      message_ = nullptr;
      primaryButton_ = nullptr;
      secondaryButton_ = nullptr;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wParam, lParam);
}

NotepadWindow::~NotepadWindow() { Close(); }

ATOM NotepadWindow::Register(HINSTANCE instance) {
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  if (GetClassInfoExW(instance, L"GooseRotNotepad", &windowClass)) return 1;
  windowClass.lpfnWndProc = &NotepadWindow::WindowProcedure;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
  windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  windowClass.lpszClassName = L"GooseRotNotepad";
  return RegisterClassExW(&windowClass);
}

bool NotepadWindow::Show(HINSTANCE instance) {
  Close();
  if (!Register(instance)) return false;
  window_ = CreateWindowExW(WS_EX_APPWINDOW, L"GooseRotNotepad", L"Untitled - Grindset",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 650, 420,
                            nullptr, nullptr, instance, this);
  if (!window_) return false;
  edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                          WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
                              ES_AUTOVSCROLL | ES_READONLY,
                          0, 0, 0, 0, window_, nullptr, instance, nullptr);
  font_ = CreateFontW(-19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                      FIXED_PITCH | FF_MODERN, L"Consolas");
  SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
  CenterWindow(window_);
  ShowWindow(window_, SW_SHOWNORMAL);
  UpdateWindow(window_);
  return true;
}

void NotepadWindow::SetText(const std::wstring& text) {
  if (!edit_) return;
  SetWindowTextW(edit_, text.c_str());
  SendMessageW(edit_, EM_SETSEL, static_cast<WPARAM>(text.size()), static_cast<LPARAM>(text.size()));
  SendMessageW(edit_, EM_SCROLLCARET, 0, 0);
}

void NotepadWindow::Minimize() {
  if (window_) ShowWindow(window_, SW_MINIMIZE);
}

void NotepadWindow::Close() {
  if (window_) DestroyWindow(window_);
  if (font_) {
    DeleteObject(font_);
    font_ = nullptr;
  }
  window_ = nullptr;
  edit_ = nullptr;
}

LRESULT CALLBACK NotepadWindow::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  NotepadWindow* self = reinterpret_cast<NotepadWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<NotepadWindow*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->window_ = window;
  }
  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    return DefWindowProcW(window, message, wParam, lParam);
  }
  return self ? self->HandleMessage(message, wParam, lParam)
              : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT NotepadWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_SIZE:
      if (edit_) MoveWindow(edit_, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
      return 0;
    case WM_CLOSE:
      DestroyWindow(window_);
      return 0;
    case WM_DESTROY:
      window_ = nullptr;
      edit_ = nullptr;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace gooserot
