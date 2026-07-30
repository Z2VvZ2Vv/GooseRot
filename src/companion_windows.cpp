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
  instance_ = instance;
  refusals_ = 0;
  pendingRefusalReports_ = 0;
  respawnQueued_ = false;
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
  respawnQueued_ = false;
}

bool NotepadWindow::ConsumeRefusal() {
  if (pendingRefusalReports_ <= 0) return false;
  --pendingRefusalReports_;
  return true;
}

// The [X] is answered with a new title and a shove instead of a destroyed
// window. After enough insistence the window "closes" and comes straight back.
void NotepadWindow::RefuseClose() {
  ++refusals_;
  ++pendingRefusalReports_;
  if (!window_) return;

  constexpr std::array<const wchar_t*, 4> titles = {
      L"Untitled - Grindset (unsaved)", L"NO - Grindset",
      L"Untitled - Grindset (the close button is decorative)", L"Untitled - Grindset (67)"};
  SetWindowTextW(window_,
                 titles[static_cast<std::size_t>(refusals_ - 1) % titles.size()]);

  RECT rectangle{};
  if (GetWindowRect(window_, &rectangle)) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &info)) {
      const int width = rectangle.right - rectangle.left;
      const int height = rectangle.bottom - rectangle.top;
      const int offset = (refusals_ % 2 == 0) ? 67 : -67;
      const int x = std::clamp(static_cast<int>(rectangle.left) + offset,
                               static_cast<int>(info.rcWork.left),
                               std::max(static_cast<int>(info.rcWork.left),
                                        static_cast<int>(info.rcWork.right) - width));
      const int y = std::clamp(static_cast<int>(rectangle.top) + offset / 3,
                               static_cast<int>(info.rcWork.top),
                               std::max(static_cast<int>(info.rcWork.top),
                                        static_cast<int>(info.rcWork.bottom) - height));
      SetWindowPos(window_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }

  if (refusals_ >= 4) {
    // It finally obeys. The first time it comes straight back; after that the
    // joke is over and the window really does close.
    respawnQueued_ = !respawnUsed_;
    respawnUsed_ = true;
    DestroyWindow(window_);
  }
}

void NotepadWindow::Tick(double logicalTime) {
  (void)logicalTime;
  if (respawnQueued_ && !window_ && instance_) {
    respawnQueued_ = false;
    const int previousRefusals = refusals_;
    if (Show(instance_)) {
      refusals_ = previousRefusals;
      SetWindowTextW(window_, L"Untitled - Grindset (2) - recovered automatically");
    }
    return;
  }
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
      RefuseClose();
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

// ---------------------------------------------------------------------------
// PopupSwarm
// ---------------------------------------------------------------------------

namespace {

constexpr int kPopupCloseButtonId = 6711;

constexpr std::array<const wchar_t*, 12> kPopupTitles = {
    L"Task Manager",
    L"File Explorer",
    L"Untitled - Notepad",
    L"Windows Security",
    L"Control Panel",
    L"Command Prompt",
    L"System Properties",
    L"goose.exe - Application Error",
    L"Aura Report - FINAL",
    L"Ohio - File Explorer",
    L"Rizz Assistant (Beta)",
    L"67 processes - Task Manager"};

constexpr std::array<const wchar_t*, 12> kPopupLines = {
    L"CPU 100%\r\nGooseRot (67 processes)",
    L"This PC > Aura > Ohio\r\n67 items found.",
    L"HONK HONK HONK\r\nThe goose saved this file for you.",
    L"Threat detected: critical brainrot.\r\nRecommended action: listen to the goose.",
    L"Your desktop settings are now managed\r\nby GooseRot Administrator.",
    L"C:\\AURA> goose.exe --take-over\r\nAccess granted. Bad idea.",
    L"System protection: emotionally unavailable.\r\nAura integrity: 0%.",
    L"goose.exe is not responding.\r\nIt is, however, multiplying.",
    L"Clipboard certified: +10,000 aura.\r\nNo refunds. No appeals.",
    L"This folder contains 67 items.\r\nEvery single one is named 67.",
    L"NPC detected at this workstation.\r\nYes, that means you.",
    L"Memory 99%  Disk 100%  Aura 0%\r\nClosing privileges have been revoked."};

constexpr std::array<const wchar_t*, 6> kPopupButtons = {
    L"END TASK", L"CLOSE", L"OK", L"REMOVE", L"CANCEL", L"MAKE IT STOP"};

}  // namespace

PopupSwarm::~PopupSwarm() { CloseAll(); }

ATOM PopupSwarm::Register(HINSTANCE instance) {
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  if (GetClassInfoExW(instance, L"GooseRotPopup", &windowClass)) return 1;
  windowClass.lpfnWndProc = &PopupSwarm::WindowProcedure;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hIcon = LoadIconW(nullptr, IDI_WARNING);
  windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  windowClass.lpszClassName = L"GooseRotPopup";
  return RegisterClassExW(&windowClass);
}

bool PopupSwarm::CreatePopup(HINSTANCE instance, std::mt19937& random) {
  if (AtCap() || !Register(instance)) return false;

  auto popup = std::make_unique<Popup>();
  popup->owner = this;

  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &info)) return false;
  constexpr int kWidth = 348;
  constexpr int kHeight = 186;
  const int spanX = std::max(1, static_cast<int>(info.rcWork.right - info.rcWork.left) - kWidth);
  const int spanY = std::max(1, static_cast<int>(info.rcWork.bottom - info.rcWork.top) - kHeight);
  std::uniform_int_distribution<int> pickX(0, spanX);
  std::uniform_int_distribution<int> pickY(0, spanY);
  const int x = static_cast<int>(info.rcWork.left) + pickX(random);
  const int y = static_cast<int>(info.rcWork.top) + pickY(random);

  const std::size_t variant = static_cast<std::size_t>(spawnCounter_++);
  popup->window = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"GooseRotPopup",
      kPopupTitles[variant % kPopupTitles.size()], WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y,
      kWidth, kHeight, nullptr, nullptr, instance, popup.get());
  if (!popup->window) return false;

  popup->label = CreateWindowExW(0, L"STATIC", kPopupLines[variant % kPopupLines.size()],
                                 WS_CHILD | WS_VISIBLE | SS_CENTER, 16, 20, kWidth - 40, 74,
                                 popup->window, nullptr, instance, nullptr);
  popup->button = CreateWindowExW(
      0, L"BUTTON", kPopupButtons[variant % kPopupButtons.size()],
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
      kWidth / 2 - 70, 104, 140, 32, popup->window,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPopupCloseButtonId)), instance, nullptr);
  popup->font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  for (HWND control : {popup->label, popup->button}) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(popup->font), TRUE);
  }
  ShowWindow(popup->window, SW_SHOWNOACTIVATE);
  UpdateWindow(popup->window);
  popups_.push_back(std::move(popup));
  return true;
}

void PopupSwarm::Spawn(HINSTANCE instance, std::mt19937& random, int count) {
  for (int index = 0; index < count && !AtCap(); ++index) CreatePopup(instance, random);
}

void PopupSwarm::Tick(HINSTANCE instance, std::mt19937& random, double logicalTime) {
  lastTickTime_ = logicalTime;
  popups_.erase(std::remove_if(popups_.begin(), popups_.end(),
                               [](const std::unique_ptr<Popup>& popup) {
                                 if (!popup->dead) return false;
                                 if (popup->font) DeleteObject(popup->font);
                                 return true;
                               }),
                popups_.end());

  while (pendingSpawns_ > 0 && !AtCap()) {
    --pendingSpawns_;
    if (!CreatePopup(instance, random)) break;
  }
  pendingSpawns_ = 0;

  for (const std::unique_ptr<Popup>& popup : popups_) {
    if (!popup->window || logicalTime > popup->jiggleUntil) continue;
    RECT rectangle{};
    if (!GetWindowRect(popup->window, &rectangle)) continue;
    const int shove = (static_cast<int>(logicalTime * 26.0) % 2 == 0) ? 6 : -6;
    SetWindowPos(popup->window, nullptr, rectangle.left + shove, rectangle.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

// Closing a popup is never free. Before the protective cap, one close produces
// two replacements. At the cap, close controls are permanently refused; only
// the emergency cleanup path calls CloseAll().
void PopupSwarm::RequestClose(Popup& popup) {
  ++closeAttempts_;
  ++popup.refusals;
  if (!AtCap()) {
    pendingSpawns_ += 2;
    popup.dead = true;
    if (popup.window) DestroyWindow(popup.window);
    return;
  }
  popup.jiggleUntil = lastTickTime_ + 1.2;
  if (popup.label) {
    SetWindowTextW(popup.label,
                   L"ACCESS DENIED.\r\n67 windows own this desktop now.");
  }
  if (popup.button) SetWindowTextW(popup.button, L"CLOSE DENIED");
}

bool PopupSwarm::ConsumeCloseAttempt() {
  if (closeAttempts_ <= 0) return false;
  --closeAttempts_;
  return true;
}

void PopupSwarm::CloseAll() {
  for (const std::unique_ptr<Popup>& popup : popups_) {
    if (popup->window) DestroyWindow(popup->window);
    if (popup->font) DeleteObject(popup->font);
  }
  popups_.clear();
  pendingSpawns_ = 0;
  closeAttempts_ = 0;
}

LRESULT CALLBACK PopupSwarm::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  Popup* popup = reinterpret_cast<Popup*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    popup = static_cast<Popup*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(popup));
    popup->window = window;
  }
  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    if (popup) {
      popup->window = nullptr;
      popup->label = nullptr;
      popup->button = nullptr;
      popup->dead = true;
    }
    return DefWindowProcW(window, message, wParam, lParam);
  }
  return popup && popup->owner
             ? popup->owner->HandleMessage(*popup, window, message, wParam, lParam)
             : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT PopupSwarm::HandleMessage(Popup& popup, HWND window, UINT message, WPARAM wParam,
                                  LPARAM lParam) {
  switch (message) {
    case WM_COMMAND:
      if (LOWORD(wParam) == kPopupCloseButtonId) {
        RequestClose(popup);
        return 0;
      }
      break;
    case WM_CLOSE:
      RequestClose(popup);
      return 0;
    case WM_DESTROY:
      popup.window = nullptr;
      popup.label = nullptr;
      popup.button = nullptr;
      popup.dead = true;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

// ---------------------------------------------------------------------------
// OwnedWindowsApps
// ---------------------------------------------------------------------------

namespace {

struct OwnedWindowQuery {
  DWORD processId = 0;
  HWND firstWindow = nullptr;
  bool closeWindows = false;
};

BOOL CALLBACK VisitOwnedWindow(HWND window, LPARAM parameter) {
  auto* query = reinterpret_cast<OwnedWindowQuery*>(parameter);
  DWORD processId = 0;
  GetWindowThreadProcessId(window, &processId);
  if (processId != query->processId || GetWindow(window, GW_OWNER) != nullptr) return TRUE;
  if (query->closeWindows) {
    PostMessageW(window, WM_CLOSE, 0, 0);
    return TRUE;
  }
  if (IsWindowVisible(window)) {
    query->firstWindow = window;
    return FALSE;
  }
  return TRUE;
}

HWND FindOwnedWindow(DWORD processId) {
  OwnedWindowQuery query{processId, nullptr, false};
  EnumWindows(&VisitOwnedWindow, reinterpret_cast<LPARAM>(&query));
  return query.firstWindow;
}

void AskOwnedWindowsToClose(DWORD processId) {
  OwnedWindowQuery query{processId, nullptr, true};
  EnumWindows(&VisitOwnedWindow, reinterpret_cast<LPARAM>(&query));
}

}  // namespace

OwnedWindowsApps::~OwnedWindowsApps() { CloseAll(); }

bool OwnedWindowsApps::LaunchRandom(std::mt19937& random, double logicalTime) {
  if (Count() >= kMaximumApps) return false;

  wchar_t systemDirectory[MAX_PATH]{};
  wchar_t windowsDirectory[MAX_PATH]{};
  if (!GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory))) ||
      !GetWindowsDirectoryW(windowsDirectory, static_cast<UINT>(std::size(windowsDirectory)))) {
    return false;
  }

  struct AppSpec {
    std::wstring executable;
    std::wstring arguments;
  };
  const std::wstring system(systemDirectory);
  const std::wstring windows(windowsDirectory);
  const std::array<AppSpec, 4> apps = {{
      {system + L"\\notepad.exe", L""},
      {system + L"\\mspaint.exe", L""},
      {system + L"\\taskmgr.exe", L""},
      {windows + L"\\explorer.exe", L" /separate,\"" + windows + L"\""},
  }};
  std::uniform_int_distribution<std::size_t> pick(0, apps.size() - 1);
  const AppSpec& app = apps[pick(random)];
  if (GetFileAttributesW(app.executable.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

  std::wstring command = L"\"" + app.executable + L"\"" + app.arguments;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(app.executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                      CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &process)) {
    return false;
  }
  CloseHandle(process.hThread);
  processes_.push_back({process.hProcess, process.dwProcessId, logicalTime, false});
  return true;
}

void OwnedWindowsApps::Tick(std::mt19937& random, double logicalTime) {
  processes_.erase(std::remove_if(processes_.begin(), processes_.end(), [](ProcessEntry& entry) {
                     if (!entry.process || WaitForSingleObject(entry.process, 0) != WAIT_OBJECT_0) {
                       return false;
                     }
                     CloseHandle(entry.process);
                     entry.process = nullptr;
                     return true;
                   }),
                   processes_.end());

  RECT workArea{};
  if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) return;
  for (ProcessEntry& entry : processes_) {
    if (entry.positioned || logicalTime - entry.launchedAt < 0.35) continue;
    HWND window = FindOwnedWindow(entry.processId);
    if (!window) continue;
    RECT rectangle{};
    if (!GetWindowRect(window, &rectangle)) continue;
    const int width = std::max(260L, rectangle.right - rectangle.left);
    const int height = std::max(180L, rectangle.bottom - rectangle.top);
    const int maximumX = std::max(workArea.left, workArea.right - width);
    const int maximumY = std::max(workArea.top, workArea.bottom - height);
    std::uniform_int_distribution<int> x(workArea.left, maximumX);
    std::uniform_int_distribution<int> y(workArea.top, maximumY);
    SetWindowPos(window, nullptr, x(random), y(random), 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
    entry.positioned = true;
  }
}

void OwnedWindowsApps::CloseAll() {
  for (ProcessEntry& entry : processes_) {
    if (entry.processId) AskOwnedWindowsToClose(entry.processId);
    if (entry.process) CloseHandle(entry.process);
  }
  processes_.clear();
}

}  // namespace gooserot
