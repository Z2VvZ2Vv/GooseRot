#include "app.hpp"

#include <timeapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <sstream>

#include "resource.h"

namespace gooserot {
namespace {

constexpr double kTimelineEnd = 300.0;
constexpr double kShutdownVisualDuration = 7.5;

double CounterSeconds(const LARGE_INTEGER& value, const LARGE_INTEGER& frequency) {
  return static_cast<double>(value.QuadPart) / static_cast<double>(frequency.QuadPart);
}

}  // namespace

GooseRotApp::GooseRotApp(HINSTANCE instance, AppConfig config)
    : instance_(instance), config_(config), random_(config.seed) {}

GooseRotApp::~GooseRotApp() { Cleanup(); }

bool GooseRotApp::Initialize(std::wstring& error) {
  if (!QueryPerformanceFrequency(&performanceFrequency_) || performanceFrequency_.QuadPart <= 0 ||
      !QueryPerformanceCounter(&lastCounter_)) {
    error = L"The high-resolution monotonic clock is unavailable.";
    return false;
  }
  logicalTime_ = config_.startAtSeconds;
  timeline_.Reset(logicalTime_);

  if (!overlay_.Create(instance_, config_.preview, config_.primaryMonitorOnly,
                       [this]() { Tick(); }, [this]() { exiting_ = true; Cleanup(); }, error)) {
    return false;
  }
  const bool desktopEffects = config_.desktopEffects && !config_.preview;
  if (desktopEffects && !watchdog_.Start(error)) return false;
  desktop_ = std::make_unique<DesktopDirector>(desktopEffects, config_.primaryMonitorOnly,
                                               desktopEffects ? &watchdog_ : nullptr);

  const RectF bounds = overlay_.CanvasBounds();
  if (logicalTime_ < 5.0) {
    std::uniform_int_distribution<int> side(0, 1);
    std::uniform_real_distribution<float> entranceY(bounds.top + bounds.Height() * 0.28f,
                                                     bounds.top + bounds.Height() * 0.72f);
    const bool fromLeft = side(random_) == 0;
    const Vec2 entrance{fromLeft ? bounds.left - 46.0f : bounds.right + 46.0f,
                        entranceY(random_)};
    geese_.emplace_back(entrance);
    geese_.front().SetTarget({bounds.left + bounds.Width() * (fromLeft ? 0.32f : 0.68f),
                              bounds.top + bounds.Height() * 0.58f},
                             SpeedTier::Walk, true);
  } else {
    geese_.emplace_back(bounds.Center());
  }
  auraReferenceCursor_ = desktop_->CursorPosition();
  ApplyBaseline(logicalTime_);
  for (const TimelineEvent& event : timeline_.Advance(logicalTime_)) HandleEvent(event);
  overlay_.Render(BuildRenderState());
  return true;
}

int GooseRotApp::Run() {
  // Drive frames from a QPC-paced pump instead of WM_TIMER. WM_TIMER coalesces
  // and, without a raised scheduler resolution, only lands on ~15.6 ms
  // boundaries, so at a light CPU load the cadence wanders and the motion looks
  // erratic. A 1 ms scheduler period plus a steady 60 Hz QPC schedule keeps the
  // animation smooth while still idling the CPU between frames.
  const bool raisedTimer = timeBeginPeriod(1) == TIMERR_NOERROR;
  overlay_.StopRenderTimer();

  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  const LONGLONG framePeriod = frequency.QuadPart / 60;  // ticks per 60 Hz frame
  LARGE_INTEGER nextFrame{};
  QueryPerformanceCounter(&nextFrame);

  MSG message{};
  bool running = true;
  while (running) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        running = false;
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (!running) break;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const LONGLONG remaining = nextFrame.QuadPart - now.QuadPart;
    if (remaining > frequency.QuadPart / 2000) {  // more than ~0.5 ms early
      const DWORD waitMs = static_cast<DWORD>((remaining * 1000) / frequency.QuadPart);
      MsgWaitForMultipleObjectsEx(0, nullptr, waitMs == 0 ? 1 : waitMs, QS_ALLINPUT,
                                  MWMO_INPUTAVAILABLE);
      continue;
    }

    Tick();
    if (exiting_) {
      // Let the queued WM_CLOSE/WM_QUIT drain, then leave.
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) break;
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      break;
    }

    nextFrame.QuadPart += framePeriod;
    QueryPerformanceCounter(&now);
    if (nextFrame.QuadPart < now.QuadPart) nextFrame = now;  // dropped frames: don't spiral
  }

  if (raisedTimer) timeEndPeriod(1);
  Cleanup();
  if (bootPreviewLaunchFailed_) {
    MessageBoxW(nullptr,
                L"GooseBootPreview.exe was not found. Build GooseBootPreview in the same "
                L"output directory as GooseRot.exe.",
                L"GooseRot — Preview unavailable", MB_OK | MB_ICONWARNING | MB_TOPMOST);
  }
  return exitCode_;
}

void GooseRotApp::ApplyBaseline(double logicalTime) {
  if (logicalTime >= 15.0) aura_ = -10000;
  if (logicalTime > 120.0) aura_ += 10000;
  if (logicalTime >= 203.0) aura_ -= 999998;
  EnsureGooseCount();

  if (logicalTime > 0.0 && logicalTime < 7.0) {
    SetBubble(L"Mewing in progress... DO NOT DISTURB.", 7.0 - logicalTime);
  }
  if (logicalTime >= 40.0 && logicalTime < 298.0) {
    notepad_.Show(instance_);
    lastTypedAt_ = logicalTime;
  }
  if (logicalTime >= 15.0 && logicalTime < 23.0) {
    auraPromptPending_ = true;
    auraPromptArmedAt_ = std::min(logicalTime, 15.0);
    auraReferenceCursor_ = desktop_->CursorPosition();
  }
  if (logicalTime >= 195.0 && logicalTime < 203.0) {
    sigmaPrompt_.Show(instance_, L"The Sigma Trap", L"Are you a Sigma Chad or a NPC?",
                      L"SIGMA CHAD", L"NPC", true, logicalTime);
  }
  if (logicalTime >= 135.0) {
    const int baselinePopups = std::clamp(
        1 + static_cast<int>((logicalTime - 135.0) / 2.5), 1, PopupSwarm::kMaximumPopups);
    popups_.Spawn(instance_, random_, baselinePopups);
    nextPopupAt_ = logicalTime + 0.8;
  }
  if (logicalTime >= 45.0) {
    const std::size_t baselineSprites = std::min<std::size_t>(
        config_.mode == RunMode::Lab ? 48U : 36U,
        1U + static_cast<std::size_t>((logicalTime - 45.0) / 7.0));
    while (sprites_.size() < baselineSprites) SpawnSprite();
  }
  if (config_.desktopEffects && !config_.preview && logicalTime >= 75.0 && logicalTime < 298.0) {
    const int baselineApps = std::clamp(1 + static_cast<int>((logicalTime - 75.0) / 42.0),
                                        1, OwnedWindowsApps::kMaximumApps);
    while (ownedWindowsApps_.Count() < baselineApps &&
           ownedWindowsApps_.LaunchRandom(random_, logicalTime)) {
    }
  }
  nextWindowAction_ = std::max(60.0, logicalTime + 2.0);
  nextCursorAction_ = std::max(70.0, logicalTime + 4.0);
  nextSpriteAt_ = std::max(45.0, logicalTime + 1.0);
}

void GooseRotApp::Tick() {
  if (exiting_) return;
  LARGE_INTEGER counter{};
  if (!QueryPerformanceCounter(&counter)) {
    exitCode_ = 4;
    exiting_ = true;
    Cleanup();
    overlay_.RequestClose();
    return;
  }
  const double previous = CounterSeconds(lastCounter_, performanceFrequency_);
  const double current = CounterSeconds(counter, performanceFrequency_);
  lastCounter_ = counter;
  const double realDelta = std::clamp(current - previous, 0.0, 0.25);
  const double logicalDelta = realDelta / config_.durationScale;
  logicalTime_ += logicalDelta;

  if (desktop_) desktop_->PollPendingMutations();
  if (UpdateEmergencyExit(realDelta) || !overlay_.Handle()) return;
  if (recoveryFailure_ || (desktop_ && !desktop_->RecoveryHealthy())) {
    recoveryFailure_ = true;
    exitCode_ = 4;
    if (Cleanup()) {
      exiting_ = true;
      overlay_.RequestClose();
    } else {
      SetBubble(L"Recovery watchdog interrupted. Restoring safely...", 1.0);
      overlay_.Render(BuildRenderState());
    }
    return;
  }

  for (const TimelineEvent& event : timeline_.Advance(logicalTime_)) HandleEvent(event);
  if (!shutdownStarted_) {
    EnsureGooseCount();
    UpdatePrompts();
    UpdateNotepad();
    UpdatePopups();
    UpdateOwnedWindowsApps();
    UpdateToasts();
    double movementRemaining = logicalDelta;
    while (movementRemaining > 0.0) {
      const float step = static_cast<float>(std::min(0.05, movementRemaining));
      UpdateGooseTargets(step);
      movementRemaining -= step;
    }
    UpdateDesktopActions();
    UpdateCursorGrab(logicalDelta);
    UpdateCursorChaos();
    UpdateSprites();
    UpdateGlitch(logicalDelta);
  } else if (!cleanupDone_) {
    Cleanup();
  }

  if (shutdownStarted_ && logicalTime_ >= kTimelineEnd + kShutdownVisualDuration &&
      !conclusionHandled_) {
    conclusionHandled_ = true;
    const bool restored = Cleanup();
    if (config_.fakeReboot && completedTimeline_ && restored) {
      bootPreviewLaunchFailed_ = !LaunchBootPreview();
      if (bootPreviewLaunchFailed_) exitCode_ = 4;
    } else if (config_.fakeReboot && !restored) {
      // Fail closed: the watchdog gets sole ownership of recovery after exit.
      exitCode_ = 5;
    }
    exiting_ = true;
    overlay_.RequestClose();
    return;
  }

  overlay_.Render(BuildRenderState());
}

void GooseRotApp::HandleEvent(const TimelineEvent& event) {
  switch (event.id) {
    case TimelineEventId::PassiveEntrance:
      SetBubble(L"Mewing in progress... DO NOT DISTURB.", 8.0);
      break;
    case TimelineEventId::AuraPrompt:
      aura_ = -10000;
      auraDelta_ = -10000;
      auraDeltaAt_ = logicalTime_;
      auraPromptPending_ = true;
      auraPromptArmedAt_ = logicalTime_;
      auraReferenceCursor_ = desktop_->CursorPosition();
      geese_.front().Honk(0.5f);
      SetBubble(L"I can feel your aura moving...", 5.0);
      PushToast(L"Windows Security", L"Threat detected: negative rizz.\nNo action is available. Or useful.");
      break;
    case TimelineEventId::NotepadStart:
      notepadText_.clear();
      typedWordCount_ = 0;
      lastTypedAt_ = logicalTime_;
      notepad_.Show(instance_);
      geese_.front().SetTarget({overlay_.CanvasBounds().right * 0.58f,
                                overlay_.CanvasBounds().bottom * 0.56f}, SpeedTier::Run, true);
      SetBubble(L"Jawline protocol activated.", 5.0);
      break;
    case TimelineEventId::CursorAndWindows:
      if (notepad_.IsOpen()) {
        notepadText_ += L"\r\nKEYBOARD CONTROL: REVOKED. HONK INPUT ENABLED.\r\n";
        notepad_.SetText(notepadText_);
      }
      SetBubble(L"NO CLICK. ONLY 67.\nCursor privileges under review.", 6.0);
      nextWindowAction_ = logicalTime_ + 2.0;
      nextCursorAction_ = logicalTime_ + 8.0;
      leftMouseWasDown_ = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
      break;
    case TimelineEventId::MemeSubtitles:
      SpawnSprite();
      SetBubble(L"Brainrot subtitles: certified.", 4.0);
      KickGlitch(0.2f);
      break;
    case TimelineEventId::ClipboardBadge:
      AddAura(10000);
      SetBubble(L"+10,000 AURA\nClipboard certified. Nothing was copied.", 6.0);
      PushToast(L"Clipboard", L"Paste certified. +10,000 AURA.\nNo clipboard data was read.");
      break;
    case TimelineEventId::Duplicate: {
      const Vec2 center = overlay_.CanvasBounds().Center();
      while (geese_.size() < 3) geese_.emplace_back(center);
      for (GooseEntity& goose : geese_) goose.Honk(0.9f);
      SetBubble(L"ONE GOOSE WAS NOT ENOUGH.", 6.0);
      KickGlitch(0.55f);
      // The flock starts opening windows the user cannot simply dismiss.
      nextPopupAt_ = logicalTime_ + 1.5;
      break;
    }
    case TimelineEventId::Graffiti:
      SetBubble(L"67 PIXELS. PERFECTLY CALCULATED.", 6.0);
      PushToast(L"Windows Update", L"Installing 67 aura updates...\nDo not restart. This is extremely fake.");
      break;
    case TimelineEventId::SigmaPrompt:
      sigmaPrompt_.Show(instance_, L"The Sigma Trap", L"Are you a Sigma Chad or a NPC?",
                        L"SIGMA CHAD", L"NPC", true, logicalTime_);
      break;
    case TimelineEventId::ScreenShake:
      SetBubble(L"Visual instability detected.", 4.0);
      KickGlitch(0.5f);
      PushToast(L"File Explorer", L"explorer.exe is not responding.\n(It is fine. The geese are lying.)");
      break;
    case TimelineEventId::ColorFilter:
      SetBubble(L"Matrix Green vs Neon Pink.", 4.0);
      KickGlitch(0.4f);
      break;
    case TimelineEventId::FinalMonologue:
      for (GooseEntity& goose : geese_) goose.Honk(1.4f);
      SetBubble(L"CRITICAL ERROR:\nMAXIMUM BRAINROT REACHED.", 8.0);
      KickGlitch(0.7f);
      break;
    case TimelineEventId::Countdown:
      SetBubble(L"THIRTY SECONDS UNTIL TOTAL COLLAPSE.", 5.0);
      PushToast(L"System", L"Desktop integrity expires in 00:30.\nThe geese are in control now.");
      break;
    case TimelineEventId::CircleDance:
      for (GooseEntity& goose : geese_) goose.Honk(1.0f);
      SetBubble(L"THE CIRCLE OF 67.", 5.0);
      break;
    case TimelineEventId::ResetAura:
      geese_.front().SetTarget({overlay_.CanvasBounds().Center().x,
                                overlay_.CanvasBounds().bottom - 98.0f}, SpeedTier::Charge, true);
      SetBubble(L"LAST CHANCE. DO NOT PRESS IT.", 1.2);
      KickGlitch(1.0f);
      break;
    case TimelineEventId::Shutdown:
      BeginShutdown();
      break;
  }
}

bool GooseRotApp::UpdateEmergencyExit(double realDeltaSeconds) {
  const bool escapeHeld = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
  if (escapeHeld) emergencyHeldSeconds_ += realDeltaSeconds;
  else emergencyHeldSeconds_ = 0.0;
  if (emergencyHeldSeconds_ >= 2.0) {
    exitCode_ = 0;
    const bool restored = Cleanup();
    const bool recoveryAvailable = desktop_ && desktop_->RecoveryHealthy();
    if (restored || recoveryAvailable) {
      exiting_ = true;
      overlay_.RequestClose();
    } else {
      recoveryFailure_ = true;
      SetBubble(L"Esc received. Waiting for safe desktop restoration...", 1.0);
      overlay_.Render(BuildRenderState());
    }
    return true;
  }
  return false;
}

void GooseRotApp::UpdatePrompts() {
  if (auraPromptPending_) {
    const POINT cursor = desktop_->CursorPosition();
    const double dx = static_cast<double>(cursor.x - auraReferenceCursor_.x);
    const double dy = static_cast<double>(cursor.y - auraReferenceCursor_.y);
    if (std::sqrt(dx * dx + dy * dy) > 50.0 || logicalTime_ - auraPromptArmedAt_ >= 5.0) {
      const bool fallback = logicalTime_ - auraPromptArmedAt_ >= 5.0;
      auraPrompt_.Show(instance_, L"Aura Points Deducted",
                       L"You broke the streak. -10,000 Aura.\r\nApologize immediately.",
                       L"Sorry", L"Forgive Me", false, logicalTime_);
      if (fallback) {
        const Vec2 cursorCanvas = overlay_.ScreenToCanvas(cursor);
        geese_.front().SetTarget(cursorCanvas, SpeedTier::Charge, true);
        SetBubble(L"I felt your aura move. Nice try.", 5.0);
      }
      auraPromptPending_ = false;
    }
  }

  auraPrompt_.Tick(logicalTime_);
  const PromptResult auraResult = auraPrompt_.ConsumeResult();
  if (auraResult != PromptResult::None) SetBubble(L"Apology barely accepted.", 5.0);

  sigmaPrompt_.Tick(logicalTime_);
  const PromptResult sigmaResult = sigmaPrompt_.ConsumeResult();
  if (sigmaResult != PromptResult::None) {
    AddAura(-1000000);
    SetBubble(sigmaResult == PromptResult::Primary
                  ? L"Button caught. Suspiciously sigma."
                  : L"AFK detected. NPC confirmed.\nHonesty bonus: +2 Aura.",
              7.0);
    if (sigmaResult != PromptResult::Primary) AddAura(2);
    KickGlitch(0.45f);
  }
}

void GooseRotApp::AddAura(int delta) {
  if (delta == 0) return;
  aura_ += delta;
  auraDelta_ = delta;
  auraDeltaAt_ = logicalTime_;
}

void GooseRotApp::KickGlitch(float amount) {
  glitchBoost_ = std::min(1.0f, glitchBoost_ + amount);
}

void GooseRotApp::PushToast(std::wstring title, std::wstring body, double lifetime) {
  constexpr std::size_t kMaximumToasts = 4;
  if (toasts_.size() >= kMaximumToasts) toasts_.erase(toasts_.begin());
  toasts_.push_back({std::move(title), std::move(body), logicalTime_, lifetime});
}

void GooseRotApp::UpdateToasts() {
  toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(),
                               [this](const ToastNotice& toast) {
                                 return logicalTime_ - toast.createdAt > toast.lifetime;
                               }),
                toasts_.end());
}

// The display degrades along the timeline; events add a decaying spike on top.
void GooseRotApp::UpdateGlitch(double logicalDelta) {
  float baseline = 0.0f;
  if (logicalTime_ >= 90.0) baseline = 0.06f;
  if (logicalTime_ >= 135.0) baseline = 0.16f;
  if (logicalTime_ >= 165.0) baseline = 0.24f;
  if (logicalTime_ >= 210.0) baseline = 0.40f;
  if (logicalTime_ >= 240.0) baseline = 0.58f;
  if (logicalTime_ >= 270.0) baseline = 0.74f;
  if (logicalTime_ >= 292.0) baseline = 0.92f;
  glitchBoost_ = std::max(0.0f, glitchBoost_ - static_cast<float>(logicalDelta) * 0.55f);
  glitch_ = std::min(1.0f, baseline + glitchBoost_);
}

float GooseRotApp::GraffitiProgress() const {
  constexpr double kTagStart = 165.0;
  constexpr double kTagDuration = 15.0;
  if (logicalTime_ < kTagStart) return 0.0f;
  return static_cast<float>(std::clamp((logicalTime_ - kTagStart) / kTagDuration, 0.0, 1.0));
}

void GooseRotApp::UpdatePopups() {
  if (nextPopupAt_ < 1e8 && logicalTime_ >= nextPopupAt_ && !popups_.AtCap()) {
    const int burst = logicalTime_ >= 270.0 ? 4 : logicalTime_ >= 210.0 ? 3 : logicalTime_ >= 165.0 ? 2 : 1;
    popups_.Spawn(instance_, random_, burst);
    std::uniform_real_distribution<double> delay(config_.mode == RunMode::Lab ? 0.8 : 1.4,
                                                 config_.mode == RunMode::Lab ? 1.8 : 3.0);
    nextPopupAt_ = logicalTime_ + delay(random_);
  }
  popups_.Tick(instance_, random_, logicalTime_);

  if (popups_.ConsumeCloseAttempt()) {
    constexpr std::array<const wchar_t*, 5> lines = {
        L"ONE WINDOW CLOSED. TWO WINDOWS SPAWNED.\nGOOSE MATHEMATICS.",
        L"THE CLOSE BUTTON IS PURELY DECORATIVE.",
        L"YOU CANNOT CLOSE THE GRINDSET.",
        L"I DUPLICATED YOUR DECISION.",
        L"EVERY CLICK FUNDS ANOTHER GOOSE."};
    std::uniform_int_distribution<std::size_t> pick(0, lines.size() - 1);
    SetBubble(popups_.AtCap() ? L"67 WINDOWS. CLOSE PRIVILEGES REVOKED." : lines[pick(random_)], 4.5);
    if (!geese_.empty()) geese_.front().Honk(0.45f);
    KickGlitch(0.18f);
  }
}

void GooseRotApp::UpdateNotepad() {
  // Consume first: a refused close can queue a respawn, and respawning resets
  // the window's counters.
  if (notepad_.ConsumeRefusal()) {
    constexpr std::array<const wchar_t*, 4> lines = {
        L"NO. I AM NOT DONE TYPING.",
        L"THE [X] BUTTON HAS BEEN TAXED.",
        L"CLICK AGAIN. I DARE YOU.",
        L"FINE. IT WILL BE BACK IMMEDIATELY."};
    const std::size_t index =
        static_cast<std::size_t>(std::max(0, notepad_.Refusals() - 1)) % lines.size();
    SetBubble(lines[index], 4.0);
    if (!geese_.empty()) geese_.front().Honk(0.4f);
    KickGlitch(0.22f);
  }
  notepad_.Tick(logicalTime_);
  if (logicalTime_ < 40.0 || logicalTime_ >= 298.0) return;
  if (!notepad_.IsOpen()) {
    notepad_.Show(instance_);
    lastTypedAt_ = logicalTime_;
  }
  if (!notepad_.IsOpen()) return;
  constexpr std::array<const wchar_t*, 34> words = {
      L"skibidi", L"rizzler", L"alpha", L"grindset", L"no-cap", L"fr-fr",
      L"ohio", L"sigma", L"mewing", L"streak", L"aura", L"farming",
      L"level-67", L"fanum-tax", L"jawline", L"protocol", L"activated",
      L"brainrot", L"goose", L"honk", L"NPC", L"certified", L"+10000", L"tralalero",
      L"q", L"x", L"AAAA", L"hjkl", L"goose.exe", L"NO_ESCAPE", L"67-67-67",
      L"typing...", L"wrong-window", L"HONK_INPUT"};
  std::uniform_int_distribution<std::size_t> word(0, words.size() - 1);
  const double interval = logicalTime_ >= 210.0 ? 0.20 : logicalTime_ >= 60.0 ? 0.72 : 0.18;
  int additions = 0;
  while (lastTypedAt_ + interval <= logicalTime_ && additions < 80) {
    lastTypedAt_ += interval;
    if (typedWordCount_ > 0) notepadText_ += (typedWordCount_ % 11 == 0) ? L"...\r\n" : L" ";
    notepadText_ += words[word(random_)];
    ++typedWordCount_;
    ++additions;
  }
  if (additions > 0) {
    if (notepadText_.size() > 7000U) {
      const std::size_t newline = notepadText_.find(L'\n', 1800U);
      notepadText_.erase(0, newline == std::wstring::npos ? 1800U : newline + 1U);
    }
    notepad_.SetText(notepadText_);
  }
}

void GooseRotApp::UpdateOwnedWindowsApps() {
  ownedWindowsApps_.Tick(random_, logicalTime_);
  if (!config_.desktopEffects || config_.preview || logicalTime_ < 75.0 ||
      logicalTime_ >= 298.0 || logicalTime_ < nextOwnedAppAt_ ||
      ownedWindowsApps_.Count() >= OwnedWindowsApps::kMaximumApps) {
    return;
  }
  if (ownedWindowsApps_.LaunchRandom(random_, logicalTime_)) {
    SetBubble(L"WINDOWS BROUGHT REINFORCEMENTS.", 3.5);
  }
  std::uniform_real_distribution<double> delay(24.0, 39.0);
  nextOwnedAppAt_ = logicalTime_ + delay(random_);
}

void GooseRotApp::UpdateDesktopActions() {
  // Window moves stay inside the 1:00-2:15 phase; the cursor hunt keeps running
  // until the closing choreography takes the flock over.
  const bool windowPhase = logicalTime_ >= 60.0 && logicalTime_ < 135.0;
  const bool cursorPhase = logicalTime_ >= 60.0 && logicalTime_ < 255.0;
  if (shutdownStarted_ || !cursorPhase) {
    if (cursorLatched_) EndCursorGrab(false);
    pendingAction_ = {};
    return;
  }
  if (cursorLatched_) return;

  const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  if (leftDown && !leftMouseWasDown_ && pendingAction_.kind == PendingActionKind::None) {
    ScheduleCursorAction(true);
  }
  leftMouseWasDown_ = leftDown;

  if (windowPhase && pendingAction_.kind == PendingActionKind::None &&
      logicalTime_ >= nextWindowAction_) {
    ScheduleWindowAction();
    std::uniform_real_distribution<double> delay(6.0, 10.0);
    nextWindowAction_ = logicalTime_ + delay(random_);
  }
  if (pendingAction_.kind == PendingActionKind::None && logicalTime_ >= nextCursorAction_) {
    ScheduleCursorAction(false);
    nextCursorAction_ = logicalTime_ + 15.0;
  }
  if (pendingAction_.kind == PendingActionKind::Cursor) {
    AimLeadGooseBeakAt(overlay_.ScreenToCanvas(desktop_->CursorPosition()), SpeedTier::Charge);
  } else if (pendingAction_.kind == PendingActionKind::Window) {
    AimLeadGooseBeakAt(overlay_.ScreenToCanvas(pendingAction_.windowTarget.titleBarPoint),
                       SpeedTier::Charge);
  }
  bool reached = false;
  if (pendingAction_.kind == PendingActionKind::Cursor) {
    reached = geese_.front().BeakDistanceTo(
                  overlay_.ScreenToCanvas(desktop_->CursorPosition())) < 14.0f;
  } else if (pendingAction_.kind == PendingActionKind::Window) {
    reached = geese_.front().BeakDistanceTo(
                  overlay_.ScreenToCanvas(pendingAction_.windowTarget.titleBarPoint)) < 14.0f;
  } else if (pendingAction_.kind == PendingActionKind::FauxPanel) {
    reached = geese_.front().DistanceToTarget() < 34.0f;
  }
  if (pendingAction_.kind != PendingActionKind::None && reached) {
    ExecutePendingAction();
  } else if (pendingAction_.kind != PendingActionKind::None && logicalTime_ >= pendingAction_.executeAt) {
    SetBubble(pendingAction_.kind == PendingActionKind::Window
                  ? L"Window escaped. Faux panel moved instead."
                  : L"Target too far. The 67-pixel move stayed simulated.",
              4.0);
    pendingAction_ = {};
  }
}

void GooseRotApp::ScheduleCursorAction(bool userTriggered) {
  const POINT cursor = desktop_->CursorPosition();
  const Vec2 target = overlay_.ScreenToCanvas(cursor);
  const Vec2 bodyTarget = geese_.front().BodyTargetForBeak(target);
  geese_.front().SetTarget(bodyTarget, SpeedTier::Charge, true);
  const double travel = std::clamp(
      static_cast<double>(Distance(geese_.front().Position(), bodyTarget)) / 400.0 + 2.5,
      4.5, 15.0);
  pendingAction_ = {PendingActionKind::Cursor, logicalTime_ + travel, {}, 0};
  SetBubble(userTriggered ? L"NO CLICK. ONLY 67." : L"AFK IS NOT A DEFENSE.", 3.0);
}

// The goose bites down on the pointer and starts hauling. The drag is spread
// over roughly a second of small steps, each one relative to wherever the
// pointer is at that instant, so wrestling with the mouse cannot cancel it.
void GooseRotApp::BeginCursorGrab() {
  if (geese_.empty() || cursorLatched_) return;
  cursorLatched_ = true;
  grabRemainingPixels_ = 67;
  grabDirection_ = 1;
  grabFlipped_ = false;
  grabStartedAt_ = logicalTime_;
  grabDeadline_ = logicalTime_ + 4.0;
  pendingAction_ = {};
  geese_.front().SetLatched(true);
  geese_.front().Honk(0.55f);
  SetBubble(L"GOTCHA.\nCursor privileges revoked.", 3.0);
  KickGlitch(0.25f);
}

void GooseRotApp::EndCursorGrab(bool succeeded) {
  if (!cursorLatched_) return;
  cursorLatched_ = false;
  grabRemainingPixels_ = 0;
  if (!geese_.empty()) geese_.front().SetLatched(false);
  if (!succeeded) {
    SetBubble(L"THE CURSOR HIT THE EDGE.\nI WILL BREAK IT AGAIN LATER.", 4.0);
    return;
  }
  AddAura(-67);
  constexpr std::array<const wchar_t*, 4> lines = {
      L"CURSOR MOVED BY EXACTLY 67 PIXELS.",
      L"YOUR AIM HAD NEGATIVE AURA.",
      L"NO CLICK. ONLY 67.",
      L"PUT IT BACK. I WILL STEAL IT AGAIN."};
  std::uniform_int_distribution<std::size_t> pick(0, lines.size() - 1);
  SetBubble(lines[pick(random_)], 4.5);
}

void GooseRotApp::UpdateCursorGrab(double logicalDelta) {
  if (!cursorLatched_) return;
  if (geese_.empty() || !desktop_) {
    EndCursorGrab(false);
    return;
  }

  // Stay glued to the pointer for as long as the beak holds it.
  AimLeadGooseBeakAt(overlay_.ScreenToCanvas(desktop_->CursorPosition()), SpeedTier::Charge);

  constexpr double kDragSeconds = 0.9;
  const int step = std::max(1, static_cast<int>(std::ceil(67.0 * logicalDelta / kDragSeconds)));
  const int request = grabDirection_ * std::min(step, grabRemainingPixels_);
  const int applied = desktop_->DragCursorBy(request, 0);
  grabRemainingPixels_ -= std::abs(applied);

  if (applied == 0) {
    // Blocked by a screen edge: try the other way once, then give up cleanly.
    if (!grabFlipped_) {
      grabDirection_ = -grabDirection_;
      grabFlipped_ = true;
    } else if (logicalTime_ - grabStartedAt_ > 1.5) {
      EndCursorGrab(false);
      return;
    }
  }
  if (grabRemainingPixels_ <= 0) {
    EndCursorGrab(true);
    return;
  }
  if (logicalTime_ >= grabDeadline_) EndCursorGrab(grabRemainingPixels_ < 34);
}

void GooseRotApp::UpdateCursorChaos() {
  if (!desktop_ || shutdownStarted_ || logicalTime_ < 210.0 || cursorLatched_) {
    cursorChaos_ = 0.0f;
    return;
  }

  cursorChaos_ = static_cast<float>(std::clamp((logicalTime_ - 210.0) / 75.0, 0.0, 1.0));
  const RectF bounds = overlay_.CanvasBounds();
  const Vec2 center = bounds.Center();
  const float phase = static_cast<float>(logicalTime_);
  const Vec2 attractor{
      center.x + std::sin(phase * (2.1f + cursorChaos_ * 3.7f)) * bounds.Width() * 0.38f,
      center.y + std::cos(phase * (2.7f + cursorChaos_ * 4.9f)) * bounds.Height() * 0.34f};
  const Vec2 cursor = overlay_.ScreenToCanvas(desktop_->CursorPosition());
  const Vec2 pull = (attractor - cursor) * (0.08f + cursorChaos_ * 0.58f);
  const float jitterAmplitude = 3.0f + cursorChaos_ * 38.0f;
  const Vec2 jitter{
      std::sin(phase * 41.0f) * jitterAmplitude + std::cos(phase * 67.0f) * jitterAmplitude * 0.55f,
      std::cos(phase * 47.0f) * jitterAmplitude + std::sin(phase * 71.0f) * jitterAmplitude * 0.55f};
  const float maximumStep = 8.0f + cursorChaos_ * 62.0f;
  const Vec2 step = ClampMagnitude(pull + jitter, maximumStep);
  desktop_->DragCursorBy(static_cast<int>(std::round(step.x)),
                         static_cast<int>(std::round(step.y)));
}

void GooseRotApp::ScheduleWindowAction() {
  const auto target = desktop_->PickRandomWindow(random_);
  if (!target) {
    geese_.front().SetTarget(RandomCanvasPoint(90.0f), SpeedTier::Run, true);
    pendingAction_ = {PendingActionKind::FauxPanel, logicalTime_ + 3.5, {}, 0};
    return;
  }
  const Vec2 canvasTarget = overlay_.ScreenToCanvas(target->titleBarPoint);
  const Vec2 bodyTarget = geese_.front().BodyTargetForBeak(canvasTarget);
  geese_.front().SetTarget(bodyTarget, SpeedTier::Charge, true);
  std::uniform_int_distribution<int> direction(0, 3);
  const double travel = std::clamp(
      static_cast<double>(Distance(geese_.front().Position(), bodyTarget)) / 400.0 + 1.5,
      3.5, 15.0);
  pendingAction_ = {PendingActionKind::Window, logicalTime_ + travel, *target, direction(random_)};
}

void GooseRotApp::ExecutePendingAction() {
  switch (pendingAction_.kind) {
    case PendingActionKind::Cursor:
      BeginCursorGrab();
      return;
    case PendingActionKind::Window:
      if (desktop_->MoveWindowBy67(pendingAction_.windowTarget, pendingAction_.direction)) {
        AddAura(-67);
        geese_.front().Honk(0.4f);
        constexpr std::array<const wchar_t*, 4> lines = {
            L"67 PIXELS. PERFECTLY CALCULATED.", L"Your window had negative aura.",
            L"Interior design by Goose.", L"I put it there. Don't question the grindset."};
        std::uniform_int_distribution<std::size_t> pick(0, lines.size() - 1);
        SetBubble(lines[pick(random_)], 5.0);
      } else {
        SetBubble(L"Window escaped. Faux panel moved instead.", 4.0);
      }
      break;
    case PendingActionKind::FauxPanel:
      SetBubble(L"No eligible window. Interior design simulated.", 4.0);
      break;
    case PendingActionKind::None:
      break;
  }
  pendingAction_ = {};
}

void GooseRotApp::UpdateGooseTargets(float deltaSeconds) {
  if (geese_.empty()) return;
  const RectF bounds = overlay_.CanvasBounds();
  const Vec2 center = bounds.Center();
  const POINT cursorScreen = desktop_->CursorPosition();
  const Vec2 cursor = overlay_.ScreenToCanvas(cursorScreen);

  // While the lead goose is hauling the pointer or stalking a title bar, the
  // choreography leaves it alone.
  const bool leadBusy = cursorLatched_ || pendingAction_.kind != PendingActionKind::None;

  if (!shutdownStarted_) {
    if (logicalTime_ >= 285.0 && geese_.size() >= 3) {
      const float aspect = std::max(0.5f, bounds.Width() / std::max(1.0f, bounds.Height()));
      const int columns = std::max(1, static_cast<int>(std::ceil(
          std::sqrt(static_cast<float>(geese_.size()) * aspect))));
      const int rows = std::max(1, static_cast<int>((geese_.size() + columns - 1) / columns));
      const float cellWidth = bounds.Width() / columns;
      const float cellHeight = bounds.Height() / rows;
      for (std::size_t index = 0; index < geese_.size(); ++index) {
        const int column = static_cast<int>(index) % columns;
        const int row = static_cast<int>(index) / columns;
        const float wobbleX = std::sin(static_cast<float>(logicalTime_ * 3.0 + index * 1.7)) *
                              std::min(20.0f, cellWidth * 0.16f);
        const float wobbleY = std::cos(static_cast<float>(logicalTime_ * 3.7 + index * 2.1)) *
                              std::min(16.0f, cellHeight * 0.14f);
        geese_[index].SetTarget(
            {bounds.left + (column + 0.5f) * cellWidth + wobbleX,
             bounds.top + (row + 0.5f) * cellHeight + wobbleY},
            SpeedTier::Charge, true);
      }
      if (logicalTime_ >= 299.0) {
        geese_.front().SetTarget({center.x, bounds.bottom - 98.0f}, SpeedTier::Charge, true);
      }
    } else if (logicalTime_ >= 255.0 && geese_.size() >= 3) {
      constexpr float kGoldenAngle = 2.39996323f;
      const float maximumRadius = std::min(bounds.Width(), bounds.Height()) * 0.44f;
      for (std::size_t index = 0; index < geese_.size(); ++index) {
        const float angle = static_cast<float>(logicalTime_ * 1.8) + index * kGoldenAngle;
        const float radius = std::min(maximumRadius, 58.0f + std::sqrt(static_cast<float>(index)) * 43.0f);
        geese_[index].SetTarget(cursor + Vec2{std::cos(angle) * radius, std::sin(angle) * radius},
                                SpeedTier::Charge, true);
      }
    } else if (logicalTime_ >= 165.0 && logicalTime_ < 210.0 && geese_.size() >= 3) {
      const float progress = GraffitiProgress();
      if (!leadBusy) {
        if (progress < 1.0f) {
          // Goose 1 walks the tag while it is being sprayed: the paint follows
          // the beak instead of appearing out of nowhere.
          const Vec2 nozzle = overlay_.GraffitiPaintHead(progress);
          geese_[0].SetTarget(nozzle - Vec2{26.0f, 0.0f}, SpeedTier::Run, true);
        } else {
          geese_[0].SetTarget(center + Vec2{-155.0f, 35.0f}, SpeedTier::Run, true);
        }
      }
      for (std::size_t index = 1; index < geese_.size(); ++index) {
        const float angle = static_cast<float>(logicalTime_ * 2.0 + index * 3.14159265);
        geese_[index].SetTarget(center + Vec2{std::cos(angle) * 175.0f, std::sin(angle) * 95.0f},
                                SpeedTier::Run, false);
      }
    } else if (logicalTime_ >= 135.0 && logicalTime_ < 165.0 && geese_.size() >= 3) {
      if (!leadBusy) geese_[0].SetTarget(center + Vec2{-120.0f, 0.0f}, SpeedTier::Run);
      geese_[1].SetTarget(center + Vec2{0.0f, -85.0f}, SpeedTier::Run);
      geese_[2].SetTarget(center + Vec2{120.0f, 20.0f}, SpeedTier::Run);
    } else if (!leadBusy) {
      for (GooseEntity& goose : geese_) {
        if (goose.DistanceToTarget() < 18.0f) goose.SetTarget(RandomCanvasPoint(65.0f), SpeedTier::Walk);
      }
    }

    // A carried brainrot image temporarily overrides that goose's choreography
    // so the prop visibly travels across the desktop before being abandoned.
    // Only the first prop per carrier may steer it; without this guard several
    // in-flight props reassign the same goose's target every frame and it jitters.
    std::vector<bool> carrierSteered(geese_.size(), false);
    for (const VisualSprite& sprite : sprites_) {
      if (!sprite.carried || sprite.carrierIndex >= geese_.size()) continue;
      if (carrierSteered[sprite.carrierIndex]) continue;
      if (sprite.carrierIndex == 0 && leadBusy) continue;
      carrierSteered[sprite.carrierIndex] = true;
      GooseEntity& carrier = geese_[sprite.carrierIndex];
      carrier.SetTarget(carrier.BodyTargetForBeak(sprite.targetCenter), SpeedTier::Run, true);
    }
  }

  for (GooseEntity& goose : geese_) goose.Update(deltaSeconds, bounds);
}

void GooseRotApp::UpdateSprites() {
  for (VisualSprite& sprite : sprites_) {
    if (!sprite.carried || sprite.carrierIndex >= geese_.size()) continue;
    const GooseEntity& carrier = geese_[sprite.carrierIndex];
    sprite.center = carrier.Rig().beakTip;
    if (carrier.BeakDistanceTo(sprite.targetCenter) < 28.0f ||
        logicalTime_ >= sprite.deliveryDeadline) {
      sprite.carried = false;
      sprite.center = sprite.targetCenter;
    }
  }

  if (logicalTime_ >= 45.0 && logicalTime_ < 298.0 && logicalTime_ >= nextSpriteAt_) {
    const std::size_t limit = config_.mode == RunMode::Lab ? 48U : 36U;
    if (sprites_.size() < limit) SpawnSprite();
    const bool late = logicalTime_ >= 210.0;
    std::uniform_real_distribution<double> delay(
        late ? (config_.mode == RunMode::Lab ? 0.7 : 1.2) : 3.0,
        late ? (config_.mode == RunMode::Lab ? 1.5 : 2.6) : 6.0);
    nextSpriteAt_ = logicalTime_ + delay(random_);
  }
}

void GooseRotApp::SpawnSprite() {
  constexpr std::array<int, 7> resources = {
      IDR_BRAINROT_TRALALERO, IDR_BRAINROT_BALLERINA, IDR_BRAINROT_BOMBARDIRO,
      IDR_CAT_SHOCKED, IDR_CAT_JUDGMENTAL, IDR_CAT_VACANT, IDR_CAT_SUSPICIOUS};
  std::uniform_int_distribution<std::size_t> resource(0, resources.size() - 1);
  std::uniform_real_distribution<float> size(108.0f, 176.0f);
  std::uniform_real_distribution<float> angle(-12.0f, 12.0f);
  VisualSprite sprite;
  sprite.resourceId = resources[resource(random_)];
  sprite.size = size(random_);
  sprite.angleDegrees = angle(random_);
  sprite.createdAt = logicalTime_;
  sprite.lifetime = std::max(12.0, 310.0 - logicalTime_);
  sprite.targetCenter = FindSpriteLandingPoint(sprite.size);

  // Hand the prop to a goose that is not already ferrying one. Capping carried
  // props to one per goose is what keeps the carriers from thrashing; any prop
  // that can't get a free carrier simply appears already resting at its spot.
  std::vector<unsigned char> carrierBusy(geese_.size(), 0U);
  for (const VisualSprite& existing : sprites_) {
    if (existing.carried && existing.carrierIndex < carrierBusy.size()) {
      carrierBusy[existing.carrierIndex] = 1U;
    }
  }
  const bool leadBusy = cursorLatched_ || pendingAction_.kind != PendingActionKind::None;
  const std::size_t carrier = PickFreeCarrier(carrierBusy, leadBusy);
  sprite.carried = carrier != kNoCarrier;
  sprite.carrierIndex = sprite.carried ? carrier : 0U;
  sprite.deliveryDeadline = logicalTime_ + 5.0;
  sprite.center = sprite.carried ? geese_[carrier].Rig().beakTip : sprite.targetCenter;
  sprites_.push_back(sprite);
}

Vec2 GooseRotApp::FindSpriteLandingPoint(float size) {
  const RectF bounds = overlay_.CanvasBounds();
  const float margin = std::max(58.0f, size * 0.55f);
  Vec2 fallback = RandomCanvasPoint(margin);
  for (int attempt = 0; attempt < 48; ++attempt) {
    const Vec2 candidate = RandomCanvasPoint(margin);
    fallback = candidate;
    const bool auraHud = candidate.x > bounds.right - 340.0f && candidate.y < bounds.top + 160.0f;
    const bool clipboardZone = candidate.x < bounds.left + 480.0f &&
                               candidate.y > bounds.bottom - 230.0f;
    const bool graffitiCore = candidate.x > bounds.left + bounds.Width() * 0.28f &&
                               candidate.x < bounds.left + bounds.Width() * 0.72f &&
                               candidate.y > bounds.top + bounds.Height() * 0.17f &&
                               candidate.y < bounds.top + bounds.Height() * 0.78f;
    if (auraHud || clipboardZone || (logicalTime_ >= 135.0 && graffitiCore)) continue;

    bool overlaps = false;
    for (const VisualSprite& existing : sprites_) {
      const float minimum = (size + existing.size) * 0.34f;
      if (Distance(candidate, existing.targetCenter) < minimum) {
        overlaps = true;
        break;
      }
    }
    if (!overlaps) return candidate;
  }
  return fallback;
}

std::size_t GooseRotApp::DesiredGooseCount() const {
  if (logicalTime_ < 135.0) return 1;
  if (logicalTime_ < 210.0) return 3;
  const double progress = std::clamp((logicalTime_ - 210.0) / 89.0, 0.0, 1.0);
  return static_cast<std::size_t>(3 + std::floor(std::pow(progress, 0.72) * 64.0));
}

void GooseRotApp::EnsureGooseCount() {
  const std::size_t desired = DesiredGooseCount();
  while (geese_.size() < desired) {
    GooseEntity goose(RandomCanvasPoint(58.0f));
    goose.Honk(0.35f);
    geese_.push_back(goose);
  }
}

Vec2 GooseRotApp::RandomCanvasPoint(float margin) {
  const RectF bounds = overlay_.CanvasBounds();
  const float horizontalMargin = std::min(margin, bounds.Width() * 0.5f);
  const float verticalMargin = std::min(margin, bounds.Height() * 0.5f);
  std::uniform_real_distribution<float> x(bounds.left + horizontalMargin, bounds.right - horizontalMargin);
  std::uniform_real_distribution<float> y(bounds.top + verticalMargin, bounds.bottom - verticalMargin);
  return {x(random_), y(random_)};
}

void GooseRotApp::SetBubble(std::wstring text, double durationSeconds) {
  bubbleText_ = std::move(text);
  bubbleUntil_ = logicalTime_ + durationSeconds;
}

void GooseRotApp::AimLeadGooseBeakAt(Vec2 target, SpeedTier tier) {
  if (geese_.empty()) return;
  geese_.front().SetTarget(geese_.front().BodyTargetForBeak(target), tier, true);
}

void GooseRotApp::BeginShutdown() {
  if (shutdownStarted_) return;
  shutdownStarted_ = true;
  completedTimeline_ = true;
  Cleanup();
}

bool GooseRotApp::Cleanup() {
  if (cleanupDone_) return true;
  auraPrompt_.Close();
  sigmaPrompt_.Close();
  notepad_.Close();
  // Whatever the swarm was refusing, it goes away here: cleanup and the
  // emergency exit always win.
  popups_.CloseAll();
  ownedWindowsApps_.CloseAll();
  nextPopupAt_ = 1e9;
  cursorLatched_ = false;
  cursorChaos_ = 0.0f;
  if (!geese_.empty()) geese_.front().SetLatched(false);
  pendingAction_ = {};
  if (desktop_ && !desktop_->Restore()) return false;
  cleanupDone_ = true;
  return true;
}

bool GooseRotApp::LaunchBootPreview() {
  wchar_t executablePath[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, executablePath,
                                          static_cast<DWORD>(std::size(executablePath)));
  if (length == 0 || length >= std::size(executablePath)) return false;
  std::wstring directory(executablePath, length);
  const std::size_t separator = directory.find_last_of(L"\\/");
  if (separator == std::wstring::npos) return false;
  directory.resize(separator);

  const std::wstring candidates[] = {
      directory + L"\\GooseBootPreview.exe",
  };
  for (const std::wstring& candidate : candidates) {
    if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
    std::wstring command = L"\"" + candidate + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(candidate.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP, nullptr, directory.c_str(), &startup, &process)) {
      continue;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
  }
  return false;
}

RenderState GooseRotApp::BuildRenderState() const {
  RenderState state;
  state.logicalTime = logicalTime_;
  state.mode = config_.mode;
  state.geese = &geese_;
  state.sprites = &sprites_;
  state.toasts = &toasts_;
  state.aura = aura_;
  state.auraDelta = auraDelta_;
  state.auraDeltaAt = auraDeltaAt_;
  state.cursor = desktop_ ? desktop_->CursorPosition() : POINT{};
  state.emergencyProgress = static_cast<float>(emergencyHeldSeconds_ / 2.0);
  state.glitch = glitch_;
  state.cursorChaos = cursorChaos_;
  state.graffitiProgress = GraffitiProgress();
  state.popupCount = popups_.Count();
  state.cursorLatched = cursorLatched_;
  state.clipboardBadge = logicalTime_ >= 120.0 && logicalTime_ < 165.0;
  state.graffiti = logicalTime_ >= 165.0 && logicalTime_ < 299.0;
  state.colorFilter = logicalTime_ >= 240.0 && logicalTime_ < 300.0;
  state.finalMonologue = logicalTime_ >= 255.0 && logicalTime_ < 300.0;
  state.countdown = logicalTime_ >= 270.0 && logicalTime_ < 300.0;
  state.resetButton = logicalTime_ >= 298.4 && logicalTime_ < 300.0;
  state.fakeShutdown = shutdownStarted_ || logicalTime_ >= 300.0;
  if (logicalTime_ <= bubbleUntil_ && !geese_.empty()) {
    state.bubbleText = bubbleText_;
    state.bubbleAnchor = geese_.front().Position();
  }
  return state;
}

}  // namespace gooserot
