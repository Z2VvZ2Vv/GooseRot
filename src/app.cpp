#include "app.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <sstream>

#include "resource.h"

namespace gooserot {
namespace {

constexpr double kTimelineEnd = 300.0;
constexpr double kShutdownVisualDuration = 4.5;

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
    error = L"L'horloge monotone haute résolution est indisponible.";
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
  geese_.emplace_back(bounds.Center());
  auraReferenceCursor_ = desktop_->CursorPosition();
  ApplyBaseline(logicalTime_);
  for (const TimelineEvent& event : timeline_.Advance(logicalTime_)) HandleEvent(event);
  overlay_.Render(BuildRenderState());
  return true;
}

int GooseRotApp::Run() {
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  Cleanup();
  if (bootPreviewLaunchFailed_) {
    MessageBoxW(nullptr,
                L"GooseBootPreview.exe est introuvable. Construisez la cible GooseBootPreview "
                L"dans le même dossier de sortie que GooseRot.exe.",
                L"GooseRot — Preview indisponible", MB_OK | MB_ICONWARNING | MB_TOPMOST);
  }
  return exitCode_;
}

void GooseRotApp::ApplyBaseline(double logicalTime) {
  if (logicalTime >= 15.0) aura_ = -10000;
  if (logicalTime > 120.0) aura_ += 9999;
  if (logicalTime >= 203.0) aura_ -= 999998;
  if (logicalTime >= 299.0) aura_ = 0;

  if (logicalTime >= 135.0) {
    const Vec2 center = overlay_.CanvasBounds().Center();
    while (geese_.size() < 3) {
      const float offset = static_cast<float>(geese_.size()) * 35.0f;
      geese_.emplace_back(center + Vec2{offset - 35.0f, 28.0f});
    }
  }

  if (logicalTime > 0.0 && logicalTime < 7.0) {
    SetBubble(L"Mewing in progress... DO NOT DISTURB.", 7.0 - logicalTime);
  }
  if (logicalTime >= 40.0 && logicalTime < 60.0) {
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
  nextWindowAction_ = std::max(60.0, logicalTime + 2.0);
  nextCursorAction_ = std::max(75.0, logicalTime + 5.0);
  nextSpriteAt_ = std::max(90.0, logicalTime + 1.0);
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
    UpdatePrompts();
    UpdateNotepad();
    double movementRemaining = logicalDelta;
    while (movementRemaining > 0.0) {
      const float step = static_cast<float>(std::min(0.05, movementRemaining));
      UpdateGooseTargets(step);
      movementRemaining -= step;
    }
    UpdateDesktopActions();
    UpdateSprites();
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
      auraPromptPending_ = true;
      auraPromptArmedAt_ = logicalTime_;
      auraReferenceCursor_ = desktop_->CursorPosition();
      SetBubble(L"I can feel your aura moving...", 5.0);
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
        notepadText_ += L"\r\nSESSION SAVED. +67 AURA.";
        notepad_.SetText(notepadText_);
        notepad_.Minimize();
      }
      SetBubble(L"NO CLICK. ONLY 67.\nCursor privileges under review.", 6.0);
      nextWindowAction_ = logicalTime_ + 2.0;
      nextCursorAction_ = logicalTime_ + 15.0;
      leftMouseWasDown_ = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
      break;
    case TimelineEventId::MemeSubtitles:
      SpawnSprite();
      SetBubble(L"Brainrot subtitles: certified.", 4.0);
      break;
    case TimelineEventId::ClipboardBadge:
      aura_ += 9999;
      SetBubble(L"+9999 AURA\nClipboard certified (visual simulation).", 6.0);
      break;
    case TimelineEventId::Duplicate: {
      const Vec2 center = overlay_.CanvasBounds().Center();
      while (geese_.size() < 3) geese_.emplace_back(center);
      SetBubble(L"ONE GOOSE WAS NOT ENOUGH.", 6.0);
      break;
    }
    case TimelineEventId::Graffiti:
      SetBubble(L"67 PIXELS. PERFECTLY CALCULATED.", 6.0);
      break;
    case TimelineEventId::SigmaPrompt:
      sigmaPrompt_.Show(instance_, L"The Sigma Trap", L"Are you a Sigma Chad or a NPC?",
                        L"SIGMA CHAD", L"NPC", true, logicalTime_);
      break;
    case TimelineEventId::ScreenShake:
      SetBubble(L"Visual instability detected.", 4.0);
      break;
    case TimelineEventId::ColorFilter:
      SetBubble(L"Matrix Green vs Neon Pink.", 4.0);
      break;
    case TimelineEventId::FinalMonologue:
      SetBubble(L"CRITICAL ERROR:\nMAXIMUM BRAINROT REACHED.", 8.0);
      break;
    case TimelineEventId::Countdown:
      SetBubble(L"Thirty seconds until aura reset.", 5.0);
      break;
    case TimelineEventId::CircleDance:
      SetBubble(L"THE CIRCLE OF 67.", 5.0);
      break;
    case TimelineEventId::ResetAura:
      aura_ = 0;
      geese_.front().SetTarget({overlay_.CanvasBounds().Center().x,
                                overlay_.CanvasBounds().bottom - 98.0f}, SpeedTier::Charge, true);
      SetBubble(L"RESETTING AURA.", 1.2);
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
    aura_ -= 1000000;
    SetBubble(sigmaResult == PromptResult::Primary
                  ? L"Button caught. Suspiciously sigma."
                  : L"AFK detected. NPC confirmed.\nHonesty bonus: +2 Aura.",
              7.0);
    if (sigmaResult != PromptResult::Primary) aura_ += 2;
  }
}

void GooseRotApp::UpdateNotepad() {
  if (!notepad_.IsOpen() || logicalTime_ < 40.0 || logicalTime_ >= 60.0) return;
  constexpr std::array<const wchar_t*, 24> words = {
      L"skibidi", L"rizzler", L"alpha", L"grindset", L"no-cap", L"fr-fr",
      L"ohio", L"sigma", L"mewing", L"streak", L"aura", L"farming",
      L"level-67", L"fanum-tax", L"jawline", L"protocol", L"activated",
      L"brainrot", L"goose", L"honk", L"NPC", L"certified", L"+9999", L"tralalero"};
  std::uniform_int_distribution<std::size_t> word(0, words.size() - 1);
  int additions = 0;
  while (lastTypedAt_ + 0.18 <= logicalTime_ && additions < 80) {
    lastTypedAt_ += 0.18;
    if (typedWordCount_ > 0) notepadText_ += (typedWordCount_ % 11 == 0) ? L"...\r\n" : L" ";
    notepadText_ += words[word(random_)];
    ++typedWordCount_;
    ++additions;
  }
  if (additions > 0) notepad_.SetText(notepadText_);
}

void GooseRotApp::UpdateDesktopActions() {
  if (logicalTime_ >= 135.0 || shutdownStarted_) {
    pendingAction_ = {};
    return;
  }
  if (logicalTime_ < 60.0) return;
  const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  if (leftDown && !leftMouseWasDown_ && pendingAction_.kind == PendingActionKind::None) {
    ScheduleCursorAction(true);
  }
  leftMouseWasDown_ = leftDown;

  if (pendingAction_.kind == PendingActionKind::None && logicalTime_ >= nextWindowAction_) {
    ScheduleWindowAction();
    std::uniform_real_distribution<double> delay(6.0, 10.0);
    nextWindowAction_ = logicalTime_ + delay(random_);
  }
  if (pendingAction_.kind == PendingActionKind::None && logicalTime_ >= nextCursorAction_) {
    ScheduleCursorAction(false);
    nextCursorAction_ = logicalTime_ + 15.0;
  }
  if (pendingAction_.kind == PendingActionKind::Cursor) {
    geese_.front().SetTarget(overlay_.ScreenToCanvas(desktop_->CursorPosition()), SpeedTier::Charge, true);
  }
  if (pendingAction_.kind != PendingActionKind::None && geese_.front().DistanceToTarget() < 24.0f) {
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
  geese_.front().SetTarget(target, SpeedTier::Charge, true);
  const double travel = std::clamp(
      static_cast<double>(Distance(geese_.front().Position(), target)) / 400.0 + 1.5, 3.5, 15.0);
  pendingAction_ = {PendingActionKind::Cursor, logicalTime_ + travel, {}, 0};
  SetBubble(userTriggered ? L"NO CLICK. ONLY 67." : L"AFK IS NOT A DEFENSE.", 3.0);
}

void GooseRotApp::ScheduleWindowAction() {
  const auto target = desktop_->PickRandomWindow(random_);
  if (!target) {
    geese_.front().SetTarget(RandomCanvasPoint(90.0f), SpeedTier::Run, true);
    pendingAction_ = {PendingActionKind::FauxPanel, logicalTime_ + 3.5, {}, 0};
    return;
  }
  const Vec2 canvasTarget = overlay_.ScreenToCanvas(target->titleBarPoint);
  geese_.front().SetTarget(canvasTarget, SpeedTier::Charge, true);
  std::uniform_int_distribution<int> direction(0, 3);
  const double travel = std::clamp(
      static_cast<double>(Distance(geese_.front().Position(), canvasTarget)) / 400.0 + 1.5, 3.5, 15.0);
  pendingAction_ = {PendingActionKind::Window, logicalTime_ + travel, *target, direction(random_)};
}

void GooseRotApp::ExecutePendingAction() {
  switch (pendingAction_.kind) {
    case PendingActionKind::Cursor:
      if (desktop_->PushCursorRight67()) SetBubble(L"Cursor shifted by exactly 67 pixels.", 4.0);
      else SetBubble(L"Cursor privileges revoked (simulation).", 4.0);
      break;
    case PendingActionKind::Window:
      if (desktop_->MoveWindowBy67(pendingAction_.windowTarget, pendingAction_.direction)) {
        aura_ -= 67;
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

  if (!shutdownStarted_) {
    if (logicalTime_ >= 299.0) {
      geese_.front().SetTarget({center.x, bounds.bottom - 98.0f}, SpeedTier::Charge, true);
    } else if (logicalTime_ >= 255.0 && geese_.size() >= 3) {
      const float speed = logicalTime_ >= 285.0 ? 4.2f : 1.1f;
      const float radius = logicalTime_ >= 285.0 ? 92.0f : 120.0f;
      for (std::size_t index = 0; index < geese_.size(); ++index) {
        const float angle = static_cast<float>(logicalTime_ * speed + index * 2.0943951);
        geese_[index].SetTarget(cursor + Vec2{std::cos(angle) * radius, std::sin(angle) * radius},
                                SpeedTier::Charge, true);
      }
    } else if (logicalTime_ >= 165.0 && logicalTime_ < 210.0 && geese_.size() >= 3) {
      geese_[0].SetTarget(center + Vec2{-155.0f, 35.0f}, SpeedTier::Run, true);
      for (std::size_t index = 1; index < geese_.size(); ++index) {
        const float angle = static_cast<float>(logicalTime_ * 2.0 + index * 3.14159265);
        geese_[index].SetTarget(center + Vec2{std::cos(angle) * 135.0f, std::sin(angle) * 70.0f},
                                SpeedTier::Run, false);
      }
    } else if (logicalTime_ >= 135.0 && logicalTime_ < 165.0 && geese_.size() >= 3) {
      geese_[0].SetTarget(center + Vec2{-120.0f, 0.0f}, SpeedTier::Run);
      geese_[1].SetTarget(center + Vec2{0.0f, -85.0f}, SpeedTier::Run);
      geese_[2].SetTarget(center + Vec2{120.0f, 20.0f}, SpeedTier::Run);
    } else if (pendingAction_.kind == PendingActionKind::None) {
      for (GooseEntity& goose : geese_) {
        if (goose.DistanceToTarget() < 18.0f) goose.SetTarget(RandomCanvasPoint(65.0f), SpeedTier::Walk);
      }
    }
  }

  for (GooseEntity& goose : geese_) goose.Update(deltaSeconds, bounds);
}

void GooseRotApp::UpdateSprites() {
  sprites_.erase(std::remove_if(sprites_.begin(), sprites_.end(), [this](const VisualSprite& sprite) {
                   return logicalTime_ - sprite.createdAt > sprite.lifetime;
                 }), sprites_.end());
  if (logicalTime_ >= 90.0 && logicalTime_ < 210.0 && logicalTime_ >= nextSpriteAt_) {
    const std::size_t limit = config_.mode == RunMode::Lab ? 24U : 12U;
    if (sprites_.size() < limit) SpawnSprite();
    std::uniform_real_distribution<double> delay(config_.mode == RunMode::Lab ? 1.4 : 3.0,
                                                  config_.mode == RunMode::Lab ? 3.5 : 7.0);
    nextSpriteAt_ = logicalTime_ + delay(random_);
  }
}

void GooseRotApp::SpawnSprite() {
  constexpr std::array<int, 7> resources = {
      IDR_BRAINROT_TRALALERO, IDR_BRAINROT_BALLERINA, IDR_BRAINROT_BOMBARDIRO,
      IDR_CAT_SHOCKED, IDR_CAT_JUDGMENTAL, IDR_CAT_VACANT, IDR_CAT_SUSPICIOUS};
  std::uniform_int_distribution<std::size_t> resource(0, resources.size() - 1);
  std::uniform_real_distribution<float> size(130.0f, 235.0f);
  std::uniform_real_distribution<float> angle(-12.0f, 12.0f);
  std::uniform_real_distribution<double> life(6.5, 10.5);
  sprites_.push_back({resources[resource(random_)], RandomCanvasPoint(125.0f), size(random_), angle(random_),
                      logicalTime_, life(random_)});
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

void GooseRotApp::BeginShutdown() {
  if (shutdownStarted_) return;
  shutdownStarted_ = true;
  completedTimeline_ = true;
  aura_ = 0;
  Cleanup();
}

bool GooseRotApp::Cleanup() {
  if (cleanupDone_) return true;
  auraPrompt_.Close();
  sigmaPrompt_.Close();
  notepad_.Close();
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
  state.aura = aura_;
  state.cursor = desktop_ ? desktop_->CursorPosition() : POINT{};
  state.emergencyProgress = static_cast<float>(emergencyHeldSeconds_ / 2.0);
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
