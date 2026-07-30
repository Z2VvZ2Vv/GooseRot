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
  if (logicalTime >= 135.0) nextPopupAt_ = logicalTime + 2.0;
  nextWindowAction_ = std::max(60.0, logicalTime + 2.0);
  nextCursorAction_ = std::max(70.0, logicalTime + 4.0);
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
    UpdatePopups();
    UpdateToasts();
    double movementRemaining = logicalDelta;
    while (movementRemaining > 0.0) {
      const float step = static_cast<float>(std::min(0.05, movementRemaining));
      UpdateGooseTargets(step);
      movementRemaining -= step;
    }
    UpdateDesktopActions();
    UpdateCursorGrab(logicalDelta);
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
      PushToast(L"Sécurité Windows", L"Menace détectée : negative rizz.\nAucune action requise. Ni possible.");
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
      nextCursorAction_ = logicalTime_ + 8.0;
      leftMouseWasDown_ = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
      break;
    case TimelineEventId::MemeSubtitles:
      SpawnSprite();
      SetBubble(L"Brainrot subtitles: certified.", 4.0);
      KickGlitch(0.2f);
      break;
    case TimelineEventId::ClipboardBadge:
      AddAura(9999);
      SetBubble(L"+9999 AURA\nClipboard certified (visual simulation).", 6.0);
      PushToast(L"Presse-papiers", L"Collage certifié. +9999 AURA.\nRien n'a été lu ni copié.");
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
      PushToast(L"Windows Update", L"Installation de 67 mises à jour de l'aura…\nNe redémarrez rien, c'est faux.");
      break;
    case TimelineEventId::SigmaPrompt:
      sigmaPrompt_.Show(instance_, L"The Sigma Trap", L"Are you a Sigma Chad or a NPC?",
                        L"SIGMA CHAD", L"NPC", true, logicalTime_);
      break;
    case TimelineEventId::ScreenShake:
      SetBubble(L"Visual instability detected.", 4.0);
      KickGlitch(0.5f);
      PushToast(L"Explorateur de fichiers", L"explorer.exe ne répond plus.\n(mensonge, il va très bien)");
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
      SetBubble(L"Thirty seconds until aura reset.", 5.0);
      PushToast(L"Système", L"Réinitialisation de l'aura dans 00:30.\nWindows n'est pas concerné.");
      break;
    case TimelineEventId::CircleDance:
      for (GooseEntity& goose : geese_) goose.Honk(1.0f);
      SetBubble(L"THE CIRCLE OF 67.", 5.0);
      break;
    case TimelineEventId::ResetAura:
      AddAura(-aura_);
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
    popups_.Spawn(instance_, random_, 1);
    std::uniform_real_distribution<double> delay(config_.mode == RunMode::Lab ? 7.0 : 12.0,
                                                 config_.mode == RunMode::Lab ? 13.0 : 22.0);
    nextPopupAt_ = logicalTime_ + delay(random_);
  }
  popups_.Tick(instance_, random_, logicalTime_);

  if (popups_.ConsumeCloseAttempt()) {
    constexpr std::array<const wchar_t*, 5> lines = {
        L"Une fenêtre fermée = deux fenêtres.\nC'est mathématique.",
        L"Le bouton FERMER est décoratif.",
        L"Tu peux pas fermer le grindset.",
        L"J'ai dupliqué ta décision.",
        L"Chaque clic finance mon aura."};
    std::uniform_int_distribution<std::size_t> pick(0, lines.size() - 1);
    SetBubble(popups_.AtCap() ? L"Ok ok. Celle-là je te la laisse." : lines[pick(random_)], 4.5);
    if (!geese_.empty()) geese_.front().Honk(0.45f);
    KickGlitch(0.18f);
  }
}

void GooseRotApp::UpdateNotepad() {
  // Consume first: a refused close can queue a respawn, and respawning resets
  // the window's counters.
  if (notepad_.ConsumeRefusal()) {
    constexpr std::array<const wchar_t*, 4> lines = {
        L"Non. Je n'ai pas fini d'écrire.",
        L"Le bouton [X] a été taxé.",
        L"Encore un clic et je duplique tout.",
        L"Bon. Elle revient dans une seconde."};
    const std::size_t index =
        static_cast<std::size_t>(std::max(0, notepad_.Refusals() - 1)) % lines.size();
    SetBubble(lines[index], 4.0);
    if (!geese_.empty()) geese_.front().Honk(0.4f);
    KickGlitch(0.22f);
  }
  notepad_.Tick(logicalTime_);
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
    geese_.front().SetTarget(overlay_.ScreenToCanvas(desktop_->CursorPosition()), SpeedTier::Charge, true);
  }
  // The beak has to actually reach the pointer; 34 px is roughly the distance
  // between the body centre and the tip of the beak.
  if (pendingAction_.kind != PendingActionKind::None && geese_.front().DistanceToTarget() < 34.0f) {
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
      static_cast<double>(Distance(geese_.front().Position(), target)) / 400.0 + 2.5, 4.5, 15.0);
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
    SetBubble(L"Le curseur était coincé au bord.\nJ'ai fait semblant.", 4.0);
    return;
  }
  AddAura(-67);
  constexpr std::array<const wchar_t*, 4> lines = {
      L"Curseur déplacé de 67 pixels. Exactement.",
      L"Ton aim avait une aura négative.",
      L"NO CLICK. ONLY 67.",
      L"Repose-le où tu veux, je recommencerai."};
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
  geese_.front().SetTarget(overlay_.ScreenToCanvas(desktop_->CursorPosition()), SpeedTier::Charge, true);

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
  // Whatever the swarm was refusing, it goes away here: cleanup and the
  // emergency exit always win.
  popups_.CloseAll();
  nextPopupAt_ = 1e9;
  cursorLatched_ = false;
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
