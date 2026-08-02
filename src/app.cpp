#include "app.hpp"

#include <mmsystem.h>
#include <timeapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <limits>
#include <sstream>

#include "resource.h"

namespace gooserot {
namespace {

// Crash, silence, the walk back in and the closing line all have to finish
// before the process leaves.
constexpr double kShutdownVisualDuration = 9.5;
// The scenario's own ceiling: sixty-seven geese, never one more.
constexpr std::size_t kMaximumGeese = 67U;

// Photos the scene keeps on the desktop at once.
std::size_t PropLimit(RunMode mode) { return mode == RunMode::Lab ? 48U : 36U; }

// How long a goose is given to walk a leg of a delivery before the photo is
// released on its own. Derived from the actual distance so a long walk across a
// wide desktop is never cut short, and bounded so nothing can hang.
double DeliveryDeadline(Vec2 from, Vec2 to) {
  const double travel = static_cast<double>(Distance(from, to)) / 190.0;
  return std::clamp(travel + 3.5, 5.0, 18.0);
}

double CounterSeconds(const LARGE_INTEGER& value, const LARGE_INTEGER& frequency) {
  return static_cast<double>(value.QuadPart) / static_cast<double>(frequency.QuadPart);
}

}  // namespace

GooseRotApp::GooseRotApp(HINSTANCE instance, AppConfig config)
    : instance_(instance),
      config_(config),
      random_(config.seed),
      audioRandom_(config.seed ^ 0xA71067U),
      keyboardRandom_(config.seed ^ 0xBADC067U) {}

GooseRotApp::~GooseRotApp() { Cleanup(); }

bool GooseRotApp::Initialize(std::wstring& error) {
  if (!QueryPerformanceFrequency(&performanceFrequency_) || performanceFrequency_.QuadPart <= 0 ||
      !QueryPerformanceCounter(&lastCounter_)) {
    error = L"The high-resolution monotonic clock is unavailable.";
    return false;
  }
  initialEntrancePending_ = config_.startAtSeconds <= phase::kEntrance;
  logicalTime_ = initialEntrancePending_
                     ? -InitialEntranceDelaySeconds(config_.seed) / config_.durationScale
                     : config_.startAtSeconds;
  timeline_.Reset(logicalTime_);

  if (!overlay_.Create(instance_, config_.preview, config_.primaryMonitorOnly,
                       [this]() { Tick(); }, [this]() { exiting_ = true; Cleanup(); }, error)) {
    return false;
  }
  const bool desktopEffects = config_.desktopEffects && !config_.preview;
  if (desktopEffects && config_.blockWindowsKey && !windowsKeyGuard_.Install(instance_)) {
    error = L"The temporary Windows-key guard could not be installed.";
    return false;
  }
  if (desktopEffects && !watchdog_.Start(error)) return false;
  desktop_ = std::make_unique<DesktopDirector>(desktopEffects, config_.primaryMonitorOnly,
                                               desktopEffects ? &watchdog_ : nullptr);

  const RectF bounds = overlay_.CanvasBounds();
  const POINT popupTopLeft = overlay_.CanvasToScreen({bounds.left, bounds.top});
  const POINT popupBottomRight = overlay_.CanvasToScreen({bounds.right, bounds.bottom});
  popups_.SetBounds({std::min(popupTopLeft.x, popupBottomRight.x),
                     std::min(popupTopLeft.y, popupBottomRight.y),
                     std::max(popupTopLeft.x, popupBottomRight.x),
                     std::max(popupTopLeft.y, popupBottomRight.y)});
  patrolFocus_ = bounds.Center();
  if (initialEntrancePending_) {
    std::uniform_int_distribution<int> edge(0, 3);
    std::uniform_real_distribution<float> along(0.28f, 0.72f);
    const float clearance = GooseEntity::kBoundsMargin + 28.0f;
    Vec2 entrance;
    switch (edge(random_)) {
      case 0:
        entrance = {bounds.left - clearance, bounds.top + bounds.Height() * along(random_)};
        initialEntranceTarget_ = {bounds.left + bounds.Width() * 0.30f, entrance.y};
        break;
      case 1:
        entrance = {bounds.right + clearance, bounds.top + bounds.Height() * along(random_)};
        initialEntranceTarget_ = {bounds.left + bounds.Width() * 0.70f, entrance.y};
        break;
      case 2:
        entrance = {bounds.left + bounds.Width() * along(random_), bounds.top - clearance};
        initialEntranceTarget_ = {entrance.x, bounds.top + bounds.Height() * 0.34f};
        break;
      default:
        entrance = {bounds.left + bounds.Width() * along(random_), bounds.bottom + clearance};
        initialEntranceTarget_ = {entrance.x, bounds.top + bounds.Height() * 0.66f};
        break;
    }
    geese_.emplace_back(entrance);
    geese_.front().SetOffstage(true);
    geese_.front().SetTarget(entrance);
  } else {
    geese_.emplace_back(bounds.Center());
  }
  auraReferenceCursor_ = desktop_->CursorPosition();
  leftMouseWasDown_ = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  ApplyBaseline(logicalTime_);
  for (const TimelineEvent& event : timeline_.Advance(logicalTime_)) HandleEvent(event);
  overlay_.Render(BuildRenderState());
  // Loading resources, creating the watchdog and the initial render are setup,
  // not part of the user's story clock or the Esc hold duration.
  if (!QueryPerformanceCounter(&lastCounter_)) {
    error = L"The high-resolution monotonic clock stopped during initialization.";
    return false;
  }
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
    LARGE_INTEGER pumpStarted{};
    QueryPerformanceCounter(&pumpStarted);
    constexpr unsigned kMaximumMessagesPerPump = 64;
    for (unsigned processed = 0; processed < kMaximumMessagesPerPump; ++processed) {
      if (!PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) break;
      if (message.message == WM_QUIT) {
        running = false;
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
      LARGE_INTEGER pumpNow{};
      QueryPerformanceCounter(&pumpNow);
      if (pumpNow.QuadPart >= nextFrame.QuadPart ||
          pumpNow.QuadPart - pumpStarted.QuadPart >= frequency.QuadPart / 500) {
        break;
      }
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
      // Give the queued WM_CLOSE/WM_QUIT a bounded chance to drain. Cleanup and
      // OverlayWindow::Close remain the fallback if the queue is still busy.
      for (unsigned processed = 0; processed < kMaximumMessagesPerPump; ++processed) {
        if (!PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) break;
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
  // In the completed run the desktop is restored before the farewell visuals,
  // but the Windows keys remain guarded until those visuals actually end.
  windowsKeyGuard_.Close();
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
  // The scorecard only exists once the inspector has started scoring, so a
  // resume before that point must not show one.
  if (logicalTime >= phase::kAuraPrompt) {
    auraVisible_ = true;
    auraRevealedAt_ = logicalTime;
    aura_ = -10000;
  }
  if (logicalTime > phase::kClipboard) aura_ += 10000;
  if (logicalTime >= phase::kSigma + 8.0) aura_ -= 999998;
  if (logicalTime >= phase::kInspectionRound) inspectionRound_ = true;
  EnsureGooseCount();

  if (logicalTime > 0.0 && logicalTime < 9.0) {
    SetBubble(L"Do not mind me.\nI am just having a look around.", 9.0 - logicalTime);
  }
  // Resuming inside the walk-out window has to leave the stage empty, or the
  // beat where the desktop is alone with its own Notepad never happens.
  if (logicalTime >= phase::kGooseExit && logicalTime < phase::kGooseReturn) {
    SendFlockOffstage();
    // Resuming here means the walk-out already happened: start them outside.
    for (GooseEntity& goose : geese_) {
      goose.SetOffstage(true);
      goose.SetPosition(goose.Target());
    }
  }
  if (logicalTime >= phase::kNotepad && logicalTime < phase::kCompanionCutoff) {
    notepad_.Show(instance_);
    // A resumed run joins the file mid-sentence rather than replaying every
    // finding the inspector already wrote.
    typist_.Reset(logicalTime);
    notepadText_ =
        L"AURA INSPECTION -- CASE 67\r\n"
        L"Site: this desktop. Inspector: a goose.\r\n\r\n"
        L"[earlier findings recorded]\r\n\r\n";
    notepad_.SetText(notepadText_);
  }
  if (logicalTime >= phase::kAuraPrompt && logicalTime < phase::kAuraPrompt + 8.0) {
    auraPromptPending_ = true;
    auraPromptArmedAt_ = std::min(logicalTime, phase::kAuraPrompt);
    auraReferenceCursor_ = desktop_->CursorPosition();
  }
  if (logicalTime >= phase::kSigma && logicalTime < phase::kSigma + 8.0) {
    sigmaPrompt_.Show(instance_, L"AURA INSPECTION - Right of Appeal",
                      L"Case 67. Do you wish to contest the inspector's findings?",
                      L"I CONTEST", L"I ACCEPT", true, logicalTime);
  }
  if (logicalTime >= phase::kGooseReturn) {
    const std::size_t baselineProps = std::min<std::size_t>(
        PropLimit(config_.mode),
        1U + static_cast<std::size_t>((logicalTime - phase::kGooseReturn) / 7.0));
    // Nothing was carried in before the resume point: these are already pinned,
    // and the goose that would have fetched them never left.
    while (sprites_.size() < baselineProps) {
      if (!SpawnSprite()) break;
      VisualSprite& placed = sprites_.back();
      placed.stage = PropStage::Placed;
      placed.stageChangedAt = logicalTime;
      placed.center = placed.targetCenter;
      placed.everPlaced = true;
      if (placed.carrierIndex < geese_.size()) geese_[placed.carrierIndex].SetOffstage(false);
    }
  }
  if (config_.desktopEffects && !config_.preview && logicalTime >= phase::kOwnedApps &&
      logicalTime < phase::kFinalMonologue) {
    const int baselineApps = std::clamp(
        1 + static_cast<int>((logicalTime - phase::kOwnedApps) / 45.0), 1,
        OwnedWindowsApps::kMaximumApps);
    while (ownedWindowsApps_.Count() < baselineApps &&
           ownedWindowsApps_.LaunchRandom(random_, realTime_, false)) {
    }
  }
  nextWindowAction_ = std::max(phase::kCursorAndWindows, logicalTime + 2.0);
  nextCursorAction_ = std::max(phase::kCursorAndWindows + 10.0, logicalTime + 4.0);
  nextSpriteAt_ = std::max(phase::kGooseReturn, logicalTime + 1.0);
  nextOwnedAppAtReal_ = realTime_ + 6.0;
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
  // Wall-clock deadlines (Esc, utility rotation and the conclusion) must not
  // slow down when rendering drops frames. Only physical simulation steps are
  // capped; elapsed QPC time and the story timeline are not.
  const FrameAdvance advance = EvaluateFrameAdvance(current - previous,
                                                     config_.durationScale);
  realTime_ += advance.wallDelta;
  logicalTime_ += advance.logicalDelta;

  if (desktop_) desktop_->PollPendingMutations();
  if (UpdateEmergencyExit(advance.wallDelta) || !overlay_.Handle()) return;
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
  if (!shutdownRequested_) {
    PollMouseButton();
    EnsureGooseCount();
    UpdatePrompts();
    UpdateNotepad();
    UpdateErrorSounds();
    UpdateOwnedWindowsApps();
    UpdatePopups();
    UpdateTaskbarGuard();
    UpdateToasts();
    double movementRemaining = advance.simulationLogicalDelta;
    while (movementRemaining > 0.0) {
      const float step = static_cast<float>(std::min(0.05, movementRemaining));
      UpdateGooseTargets(step);
      movementRemaining -= step;
    }
    // A click on a photo's [x] is answered first; whatever is left of the press
    // then feeds the cursor hunt.
    UpdatePropInteractions();
    UpdateDesktopActions();
    UpdateCursorGrab(advance.simulationLogicalDelta);
    UpdateCursorChaos();
    UpdateSprites();
    UpdateGlitch(advance.simulationLogicalDelta);
  } else if (!shutdownStarted_ && Cleanup()) {
    StartShutdownVisuals();
  }

  if (shutdownStarted_ && shutdownStartedRealTime_ >= 0.0 &&
      realTime_ - shutdownStartedRealTime_ >= kShutdownVisualDuration &&
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

// The case file the inspector is writing. Each finding is queued at its beat
// and then typed out one character at a time, so the window fills with sentences
// somebody is composing rather than with blocks of pasted words.
void GooseRotApp::FileFinding(const wchar_t* text) {
  // Queued unconditionally: if the occupant just destroyed the window, the
  // finding waits in the typist and is written out when it reopens.
  typist_.Queue(text);
}

void GooseRotApp::HandleEvent(const TimelineEvent& event) {
  switch (event.id) {
    case TimelineEventId::PassiveEntrance:
      if (initialEntrancePending_ && !geese_.empty()) {
        initialEntrancePending_ = false;
        geese_.front().SetTarget(initialEntranceTarget_, SpeedTier::Walk, true);
      }
      SetBubble(L"Do not mind me.\nI am just having a look around.", 9.0);
      break;
    case TimelineEventId::Introduction:
      geese_.front().Honk(0.9f);
      SetBubble(L"AURA INSPECTION.\nI am the inspector. Stay where you are.", 9.0);
      PushToast(L"AURA INSPECTION", L"Case 67 opened for this desktop.\nAn inspector is already on site.");
      break;
    case TimelineEventId::InspectionRound:
      SetBubble(L"Walking the site now.\nThis will take a moment.", 7.0);
      // The goose starts its circuit; the patrol choreography does the rest.
      inspectionRound_ = true;
      break;
    case TimelineEventId::NotepadStart: {
      // The file does not appear from nowhere: the goose stops, stamps the
      // desktop, and the window opens under its beak.
      const Vec2 beak = geese_.empty() ? overlay_.CanvasBounds().Center()
                                       : geese_.front().Rig().beakTip;
      const POINT anchor = overlay_.CanvasToScreen(beak);
      notepadText_.clear();
      typist_.Reset(logicalTime_);
      notepad_.Show(instance_, &anchor);
      if (!geese_.empty()) geese_.front().Honk(0.7f);
      SetBubble(L"Opening the file.\nEverything from here is written down.", 7.0);
      KickGlitch(0.12f);
      FileFinding(
          L"AURA INSPECTION -- CASE 67\r\n"
          L"Site: this desktop. Inspector: a goose.\r\n"
          L"Authorisation: not required. I am already inside.\r\n\r\n"
          L"FINDING 1. Arrived on site. Nobody stopped me.\r\n"
          L"That is, in itself, the first finding.\r\n\r\n");
      break;
    }
    case TimelineEventId::AuraPrompt:
      // The counter exists because the inspector just started scoring, not
      // because the program started.
      auraVisible_ = true;
      auraRevealedAt_ = logicalTime_;
      aura_ = -10000;
      auraDelta_ = -10000;
      auraDeltaAt_ = logicalTime_;
      auraPromptPending_ = true;
      auraPromptArmedAt_ = logicalTime_;
      auraReferenceCursor_ = desktop_->CursorPosition();
      geese_.front().Honk(0.5f);
      SetBubble(L"Baseline aura measured.\nI am going to need a bigger form.", 7.0);
      PushToast(L"AURA INSPECTION", L"Scorecard attached to case 67.\nYou may now watch it get worse.");
      FileFinding(
          L"FINDING 2. Baseline aura measured at -10,000.\r\n"
          L"Scorecard attached to the top right of the site so the\r\n"
          L"occupant can follow along. They will not enjoy it.\r\n\r\n");
      break;
    case TimelineEventId::GooseExit:
      SetBubble(L"I need evidence.\nDo not touch anything while I am out.", 7.0);
      geese_.front().Honk(0.8f);
      SendFlockOffstage();
      PushToast(L"AURA INSPECTION", L"Inspector off site collecting exhibits.\nThe file remains open.");
      FileFinding(
          L"FINDING 3. Verbal claims are not evidence.\r\n"
          L"Leaving site to collect exhibits. Back shortly.\r\n"
          L"The file stays open while I am gone. Obviously.\r\n\r\n");
      break;
    case TimelineEventId::GooseReturn:
      BringFlockBack();
      FileFinding(
          L"FINDING 4. Exhibit A recovered and pinned to the site.\r\n"
          L"Do not move it. Do not close it. There is a form for that\r\n"
          L"and I have not written it yet.\r\n\r\n");
      break;
    case TimelineEventId::CursorAndWindows:
      SetBubble(L"Pointer control: sloppy.\nConfiscating it for testing.", 7.0);
      nextWindowAction_ = logicalTime_ + 2.0;
      nextCursorAction_ = logicalTime_ + 8.0;
      leftMouseWasDown_ = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
      leftMousePressed_ = false;
      FileFinding(
          L"FINDING 5. Pointer handling assessed. It is sloppy.\r\n"
          L"Confiscating the cursor at intervals for measurement.\r\n"
          L"Window alignment was also out by 67 pixels. Corrected.\r\n"
          L"You are welcome.\r\n\r\n");
      break;
    case TimelineEventId::MemeSubtitles:
      SpawnSprite();
      SetBubble(L"Ambient brainrot detected.\nLogging it. Loudly.", 6.0);
      KickGlitch(0.2f);
      FileFinding(
          L"FINDING 6. Site is saturated with unregulated brainrot.\r\n"
          L"Measured level: yes.\r\n\r\n");
      break;
    case TimelineEventId::ClipboardBadge:
      AddAura(10000);
      SetBubble(L"Clipboard inspected.\nNothing was read. It passed anyway.", 7.0);
      PushToast(L"AURA INSPECTION", L"Clipboard certified without inspection.\nNo clipboard data was read.");
      FileFinding(
          L"FINDING 7. Clipboard certified. I did not read it.\r\n"
          L"I did not need to. +10,000 aura, non-refundable.\r\n\r\n");
      break;
    case TimelineEventId::Duplicate: {
      const Vec2 center = overlay_.CanvasBounds().Center();
      while (geese_.size() < 3) geese_.emplace_back(center);
      for (GooseEntity& goose : geese_) goose.Honk(0.9f);
      SetBubble(L"This site needs more inspectors.\nThey were already outside.", 7.0);
      KickGlitch(0.55f);
      FileFinding(
          L"FINDING 8. Workload exceeds one goose.\r\n"
          L"Backup inspectors requested. Request granted by me.\r\n\r\n");
      break;
    }
    case TimelineEventId::Graffiti:
      SetBubble(L"Score computed.\nStand back, I am writing it on the wall.", 7.0);
      PushToast(L"AURA INSPECTION", L"Final score being applied to the site.\nThe medium is permanent. The paint is not.");
      // The wall has to be bare before the score goes up, so anything hanging
      // on it is picked up and carried out of the way first.
      ClearPropsFromTagZone();
      FileFinding(
          L"FINDING 9. Final score computed: 67.\r\n"
          L"Out of what, the form does not say. Applying it to the\r\n"
          L"wall in a permanent medium, as regulations require.\r\n\r\n");
      break;
    case TimelineEventId::SigmaPrompt:
      sigmaPrompt_.Show(instance_, L"AURA INSPECTION - Right of Appeal",
                        L"Case 67. Do you wish to contest the inspector's findings?",
                        L"I CONTEST", L"I ACCEPT", true, logicalTime_);
      FileFinding(
          L"FINDING 10. Right of appeal offered, as required.\r\n"
          L"The appeal button was moved during the appeal. Also as\r\n"
          L"required.\r\n\r\n");
      break;
    case TimelineEventId::ScreenShake:
      SetBubble(L"Display integrity: deteriorating.\nNot my department.", 6.0);
      KickGlitch(0.5f);
      PushToast(L"AURA INSPECTION", L"Site display flagged as unstable.\nThe inspection continues regardless.");
      FileFinding(
          L"FINDING 11. Display integrity is deteriorating.\r\n"
          L"I have noted it. I am not going to fix it.\r\n\r\n");
      break;
    case TimelineEventId::ColorFilter:
      SetBubble(L"Colour calibration: also wrong.", 5.0);
      KickGlitch(0.4f);
      FileFinding(
          L"FINDING 12. Colour calibration is wrong in two directions\r\n"
          L"at once. Impressive, in a way.\r\n\r\n");
      break;
    case TimelineEventId::FinalMonologue:
      for (GooseEntity& goose : geese_) goose.Honk(1.4f);
      SetBubble(L"VERDICT: NON-COMPLIANT.\nThis desktop cannot be certified.", 9.0);
      KickGlitch(0.7f);
      PushToast(L"AURA INSPECTION", L"Case 67 verdict recorded.\nThe site is scheduled for closure.");
      FileFinding(
          L"VERDICT. This desktop is non-compliant on all twelve\r\n"
          L"findings. It cannot be certified. It cannot be appealed.\r\n"
          L"It can only be closed.\r\n\r\n");
      break;
    case TimelineEventId::Countdown:
      SetBubble(L"The file closes in forty seconds.\nClear every notice.", 6.0);
      PushToast(L"AURA INSPECTION", L"Case 67 closes in 00:40.\nThank you for your cooperation.");
      FileFinding(
          L"CLOSING. The file will be closed in forty seconds.\r\n"
          L"Thank you for your cooperation. You did not give any.\r\n\r\n");
      break;
    case TimelineEventId::CircleDance:
      for (GooseEntity& goose : geese_) goose.Honk(1.0f);
      SetBubble(L"Inspectors, close the site.", 6.0);
      break;
    case TimelineEventId::ResetAura:
      geese_.front().SetTarget({overlay_.CanvasBounds().Center().x,
                                overlay_.CanvasBounds().bottom - 98.0f}, SpeedTier::Charge, true);
      SetBubble(L"Signature required.\nDo not press it.", 1.2);
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

void GooseRotApp::PollMouseButton() {
  const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  leftMousePressed_ = down && !leftMouseWasDown_;
  leftMouseWasDown_ = down;
}

// A point just outside the canvas, past whichever edge is actually nearest, so
// leaving looks like walking out rather than fading away — and so a wide desktop
// does not turn every errand into a hike across the whole screen.
Vec2 GooseRotApp::OffstagePointNear(Vec2 origin) const {
  const RectF bounds = overlay_.CanvasBounds();
  constexpr float kClearance = 150.0f;
  const float toLeft = origin.x - bounds.left;
  const float toRight = bounds.right - origin.x;
  const float toTop = origin.y - bounds.top;
  const float toBottom = bounds.bottom - origin.y;
  const float nearest = std::min({toLeft, toRight, toTop, toBottom});
  if (nearest == toLeft) return {bounds.left - kClearance, origin.y};
  if (nearest == toRight) return {bounds.right + kClearance, origin.y};
  if (nearest == toTop) return {origin.x, bounds.top - kClearance};
  return {origin.x, bounds.bottom + kClearance};
}

// The staged walk-out, as opposed to a prop errand: the flock always leaves
// sideways, matching the way it entered, and far enough out to be gone.
void GooseRotApp::SendFlockOffstage() {
  const RectF bounds = overlay_.CanvasBounds();
  flockOffstage_ = true;
  for (GooseEntity& goose : geese_) {
    const bool exitLeft = goose.Position().x < bounds.Center().x;
    const float x = exitLeft ? bounds.left - 220.0f : bounds.right + 220.0f;
    goose.SetOffstage(true);
    goose.SetTarget({x, goose.Position().y}, SpeedTier::Run, false);
  }
}

// The comeback: the flock re-enters from the far side, and the lead goose is
// carrying the first brainrot photo of the run in its beak.
void GooseRotApp::BringFlockBack() {
  const RectF bounds = overlay_.CanvasBounds();
  flockOffstage_ = false;
  for (std::size_t index = 0; index < geese_.size(); ++index) {
    GooseEntity& goose = geese_[index];
    // Re-enter from the opposite side of wherever the walk-out ended. The
    // reposition happens entirely off screen, so nothing visibly teleports.
    const float rank = static_cast<float>(index);
    const bool fromLeft = goose.Position().x > bounds.Center().x;
    const float entranceX = fromLeft ? bounds.left - 90.0f - rank * 70.0f
                                     : bounds.right + 90.0f + rank * 70.0f;
    goose.SetPosition({entranceX, bounds.top + bounds.Height() * (0.42f + 0.1f * rank)});
    goose.SetTarget({bounds.left + bounds.Width() * (fromLeft ? 0.34f : 0.66f),
                     bounds.top + bounds.Height() * 0.56f},
                    SpeedTier::Run, true);
    goose.Honk(0.7f);
  }
  SetBubble(L"I WENT TO GET SUPPLIES.\nLOOK WHAT I FOUND.", 6.0);
  SpawnSprite(0U);
  nextSpriteAt_ = logicalTime_ + 6.0;
  KickGlitch(0.2f);
}

void GooseRotApp::UpdatePrompts() {
  if (auraPromptPending_) {
    const POINT cursor = desktop_->CursorPosition();
    const double dx = static_cast<double>(cursor.x - auraReferenceCursor_.x);
    const double dy = static_cast<double>(cursor.y - auraReferenceCursor_.y);
    if (std::sqrt(dx * dx + dy * dy) > 50.0 || logicalTime_ - auraPromptArmedAt_ >= 5.0) {
      const bool fallback = logicalTime_ - auraPromptArmedAt_ >= 5.0;
      auraPrompt_.Show(instance_, L"AURA INSPECTION - Notice of Deduction",
                       L"Case 67. Baseline aura recorded at -10,000.\r\n"
                       L"Please acknowledge the finding.",
                       L"I ACKNOWLEDGE", L"I AM SORRY", false, logicalTime_);
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
  if (auraResult != PromptResult::None) SetBubble(L"Acknowledgement recorded.\nIt changes nothing.", 5.0);

  sigmaPrompt_.Tick(logicalTime_);
  const PromptResult sigmaResult = sigmaPrompt_.ConsumeResult();
  if (sigmaResult != PromptResult::None) {
    AddAura(-1000000);
    SetBubble(sigmaResult == PromptResult::Primary
                  ? L"Appeal lodged. Appeal denied.\nThat was quick, even for me."
                  : L"No appeal lodged. Findings stand.\nHonesty bonus: +2 aura.",
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
// The finale leans on this instead of on more windows: past the monologue the
// baseline climbs hard while the swarm is being taken apart.
void GooseRotApp::UpdateGlitch(double logicalDelta) {
  float baseline = 0.0f;
  if (logicalTime_ >= phase::kNotepad) baseline = 0.04f;
  if (logicalTime_ >= phase::kSubtitles) baseline = 0.08f;
  if (logicalTime_ >= phase::kDuplicate) baseline = 0.18f;
  if (logicalTime_ >= phase::kGraffiti) baseline = 0.26f;
  if (logicalTime_ >= phase::kScreenShake) baseline = 0.42f;
  if (logicalTime_ >= phase::kColorFilter) baseline = 0.58f;
  if (logicalTime_ >= phase::kFinalMonologue) {
    // From the monologue to the end the damage climbs continuously rather than
    // in steps, so the last minute reads as one long collapse.
    const double progress = std::clamp(
        (logicalTime_ - phase::kFinalMonologue) / (phase::kEnd - phase::kFinalMonologue), 0.0, 1.0);
    baseline = 0.70f + 0.30f * static_cast<float>(progress);
  }
  glitchBoost_ = std::max(0.0f, glitchBoost_ - static_cast<float>(logicalDelta) * 0.55f);
  glitch_ = std::min(1.0f, baseline + glitchBoost_);
}

float GooseRotApp::GraffitiProgress() const {
  if (logicalTime_ < phase::kGraffiti) return 0.0f;
  return static_cast<float>(
      std::clamp((logicalTime_ - phase::kGraffiti) / phase::kGraffitiDuration, 0.0, 1.0));
}

void GooseRotApp::UpdateNotepad() {
  // Consume first: a refused close can queue a respawn, and respawning resets
  // the window's counters.
  if (notepad_.ConsumeRefusal()) {
    constexpr std::array<const wchar_t*, 4> lines = {
        L"The file stays open. I am still writing.",
        L"Closing an open case file is a finding.",
        L"Try that again and it becomes two findings.",
        L"Fine. It will reopen itself."};
    const std::size_t index =
        static_cast<std::size_t>(std::max(0, notepad_.Refusals() - 1)) % lines.size();
    SetBubble(lines[index], 4.0);
    if (!geese_.empty()) geese_.front().Honk(0.4f);
    KickGlitch(0.22f);
  }
  if (notepad_.ConsumeMinimiseRefusal()) {
    constexpr std::array<const wchar_t*, 4> lines = {
        L"The case file does not go in the taskbar.",
        L"You are required to be able to see it.",
        L"Minimising evidence is also a finding.",
        L"It comes straight back. Every time."};
    std::uniform_int_distribution<std::size_t> pick(0, lines.size() - 1);
    SetBubble(lines[pick(random_)], 4.0);
    if (!geese_.empty()) geese_.front().Honk(0.35f);
    KickGlitch(0.16f);
  }
  notepad_.Tick(logicalTime_);
  if (logicalTime_ < phase::kNotepad || logicalTime_ >= phase::kCompanionCutoff) return;
  if (!notepad_.IsOpen()) {
    // It was destroyed after too many refusals: the inspector reopens it and
    // picks the file back up exactly where the typing stopped.
    const Vec2 beak = geese_.empty() ? overlay_.CanvasBounds().Center()
                                     : geese_.front().Rig().beakTip;
    const POINT anchor = overlay_.CanvasToScreen(beak);
    notepad_.Show(instance_, &anchor);
    if (notepad_.IsOpen()) notepad_.SetText(notepadText_);
  }
  if (!notepad_.IsOpen()) return;

  // A tired inspector writes faster as the site falls apart, and the backlog of
  // findings has to keep up with the timeline rather than lag a phase behind.
  double speed = logicalTime_ >= phase::kFinalMonologue  ? 26.0
                 : logicalTime_ >= phase::kScreenShake   ? 19.0
                                                         : 13.0;
  if (typist_.Remaining() > 320U) speed *= 1.8;
  typist_.SetSpeed(speed);
  if (typist_.Advance(logicalTime_, keyboardRandom_, notepadText_)) {
    if (notepadText_.size() > 7000U) {
      const std::size_t newline = notepadText_.find(L'\n', 1800U);
      notepadText_.erase(0, newline == std::wstring::npos ? 1800U : newline + 1U);
    }
    notepad_.SetText(notepadText_);
  }
}

void GooseRotApp::UpdateErrorSounds() {
  const bool enabled = !config_.muted && !config_.preview && !shutdownStarted_ &&
                       logicalTime_ >= phase::kSubtitles &&
                       logicalTime_ < phase::kResetAura;
  if (!enabled) {
    if (errorSoundActive_) PlaySoundW(nullptr, nullptr, 0);
    errorSoundActive_ = false;
    nextErrorSoundAt_ = -1.0;
    return;
  }

  if (nextErrorSoundAt_ < 0.0) nextErrorSoundAt_ = realTime_ + 0.35;
  if (realTime_ < nextErrorSoundAt_) return;

  constexpr std::array<const wchar_t*, 4> aliases = {
      L"SystemHand", L"SystemQuestion", L"SystemExclamation", L"SystemAsterisk"};
  std::uniform_int_distribution<std::size_t> pick(0, aliases.size() - 1U);
  if (PlaySoundW(aliases[pick(audioRandom_)], nullptr,
                 SND_ALIAS | SND_ASYNC | SND_NODEFAULT) != FALSE) {
    errorSoundActive_ = true;
  }

  const double escalation = std::clamp(
      (logicalTime_ - phase::kSubtitles) / (phase::kResetAura - phase::kSubtitles),
      0.0, 1.0);
  const double minimum = 1.75 - escalation * 1.32;
  const double maximum = 3.70 - escalation * 2.75;
  std::uniform_real_distribution<double> delay(minimum, maximum);
  nextErrorSoundAt_ = realTime_ + delay(audioRandom_);
}

void GooseRotApp::UpdateOwnedWindowsApps() {
  ownedWindowsApps_.Tick(random_, realTime_);
  // Real utilities belong to the middle of the run. The finale is glitch, not
  // more windows, so nothing new is launched once the monologue lands.
  ownedWindowsApps_.RequestCloseOlderThan(
      realTime_, logicalTime_ >= phase::kFinalMonologue ? 0.0 : 60.0);
  if (!config_.desktopEffects || config_.preview || logicalTime_ < phase::kOwnedApps ||
      logicalTime_ >= phase::kFinalMonologue || realTime_ < nextOwnedAppAtReal_ ||
      ownedWindowsApps_.Count() >= OwnedWindowsApps::kMaximumApps) {
    return;
  }
  const bool launched = ownedWindowsApps_.LaunchRandom(random_, realTime_, false);
  if (launched) {
    SetBubble(L"Opening a site application for inspection.", 3.5);
  }
  if (!launched) {
    // Missing optional utilities or a transient CreateProcess failure should
    // not consume an entire cadence slot.
    nextOwnedAppAtReal_ = realTime_ + 0.5;
    return;
  }
  std::uniform_real_distribution<double> delay(26.0, 42.0);
  nextOwnedAppAtReal_ = realTime_ + delay(random_);
}

void GooseRotApp::UpdatePopups() {
  if (!config_.desktopEffects || config_.preview) return;
  // Notices are pages coming off the inspector's desk, so they are issued from
  // the case file if it is open, and from the goose that would be writing it
  // otherwise. Without this the wall dealt itself across the desktop with no
  // visible source.
  POINT issuePoint{};
  if (!notepad_.TryGetIssuePoint(issuePoint)) {
    const RectF bounds = overlay_.CanvasBounds();
    const Vec2 anchor = geese_.empty() ? bounds.Center() : geese_.front().Rig().beakTip;
    issuePoint = overlay_.CanvasToScreen(anchor);
  }
  popups_.SetOrigin(issuePoint);
  popups_.Tick(instance_, random_, logicalTime_);
  if (popups_.ConsumeCloseAttempt()) {
    constexpr std::array<const wchar_t*, 4> lines = {
        L"ONE NOTICE CLOSED. TWO MORE FILED.",
        L"THE CLOSE BUTTON HAS BEEN NOTED.",
        L"YOU CANNOT OUT-CLICK THE PAPERWORK.",
        L"EVERY DISMISSAL REQUIRES TWO FOLLOW-UPS."};
    std::uniform_int_distribution<std::size_t> pick(0, lines.size() - 1U);
    SetBubble(popups_.AtCap() ? L"100 NOTICES. ADMINISTRATIVE CAP REACHED."
                              : lines[pick(random_)],
              4.5);
    if (!geese_.empty()) geese_.front().Honk(0.45f);
    KickGlitch(0.18f);
  }
}

// The Start button is covered from the first gag onwards: a Start menu opening
// mid-scene hides everything the run is building.
void GooseRotApp::UpdateTaskbarGuard() {
  if (!config_.desktopEffects || config_.preview) return;
  if (logicalTime_ < phase::kAuraPrompt || logicalTime_ >= phase::kCompanionCutoff) return;
  if (!taskbarGuard_.IsOpen() && !taskbarGuard_.Show(instance_)) return;
  taskbarGuard_.Tick();

  if (taskbarGuard_.ConsumePressAttempt() && logicalTime_ - lastTaskbarGuardPokeAt_ > 2.5) {
    lastTaskbarGuardPokeAt_ = logicalTime_;
    constexpr std::array<const wchar_t*, 4> lines = {
        L"Start menu sealed for the inspection.\nThere is a goose standing on it.",
        L"That button is part of the site. Do not touch it.",
        L"Access to the menu is suspended.",
        L"I am standing on the Start button.\nIt was load-bearing."};
    std::uniform_int_distribution<std::size_t> pick(0, lines.size() - 1);
    SetBubble(lines[pick(random_)], 4.5);
    AddAura(-67);
    if (!geese_.empty()) geese_.front().Honk(0.4f);
    KickGlitch(0.2f);
  }
}

void GooseRotApp::UpdateDesktopActions() {
  // Window moves stay inside the hijack phase; the cursor hunt keeps running
  // until the closing choreography takes the flock over.
  const bool windowPhase = logicalTime_ >= phase::kCursorAndWindows &&
                           logicalTime_ < phase::kWindowHijackEnd;
  const bool cursorPhase = logicalTime_ >= phase::kCursorAndWindows &&
                           logicalTime_ < phase::kCursorHuntEnd;
  if (shutdownStarted_ || !cursorPhase || flockOffstage_) {
    if (cursorLatched_) EndCursorGrab(false);
    pendingAction_ = {};
    return;
  }
  if (cursorLatched_) return;

  if (leftMousePressed_ && pendingAction_.kind == PendingActionKind::None) {
    leftMousePressed_ = false;
    ScheduleCursorAction(true);
  }

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
  SetBubble(userTriggered ? L"Clicking during an inspection. Noted."
                          : L"Taking a pointer sample. Hold still.",
            3.0);
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
  SetBubble(L"Pointer seized for measurement.\nHold still.", 3.0);
  KickGlitch(0.25f);
}

void GooseRotApp::EndCursorGrab(bool succeeded) {
  if (!cursorLatched_) return;
  cursorLatched_ = false;
  grabRemainingPixels_ = 0;
  if (!geese_.empty()) geese_.front().SetLatched(false);
  if (!succeeded) {
    SetBubble(L"Measurement aborted at the site boundary.\nI will retake it later.", 4.0);
    return;
  }
  AddAura(-67);
  constexpr std::array<const wchar_t*, 4> lines = {
      L"Pointer displaced by exactly 67 pixels.\nWithin tolerance. Barely.",
      L"Measurement complete. Your aim is a finding.",
      L"Sample taken. You may have it back.",
      L"Put it where you like. I will measure it again."};
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

// The storm comes in waves. Inside a wave the pointer is dragged hard; between
// waves the flock lets go completely and the pointer is the user's again, so
// there is a usable middle ground instead of one long unusable stretch.
void GooseRotApp::UpdateCursorChaos() {
  if (!desktop_ || config_.reducedMotion || shutdownStarted_ ||
      cursorLatched_ || flockOffstage_) {
    cursorChaos_ = 0.0f;
    return;
  }

  cursorChaos_ = CursorStormEnvelope(logicalTime_);
  if (cursorChaos_ <= 0.02f) {
    // Released: not a single pixel is taken from the pointer this frame.
    cursorChaos_ = 0.0f;
    return;
  }

  const RectF bounds = overlay_.CanvasBounds();
  const Vec2 center = bounds.Center();
  const float beat = static_cast<float>(logicalTime_);
  const Vec2 attractor{
      center.x + std::sin(beat * (2.1f + cursorChaos_ * 3.7f)) * bounds.Width() * 0.38f,
      center.y + std::cos(beat * (2.7f + cursorChaos_ * 4.9f)) * bounds.Height() * 0.34f};
  const Vec2 cursor = overlay_.ScreenToCanvas(desktop_->CursorPosition());
  const Vec2 pull = (attractor - cursor) * (cursorChaos_ * 0.46f);
  const float jitterAmplitude = cursorChaos_ * 26.0f;
  const Vec2 jitter{
      std::sin(beat * 41.0f) * jitterAmplitude + std::cos(beat * 67.0f) * jitterAmplitude * 0.55f,
      std::cos(beat * 47.0f) * jitterAmplitude + std::sin(beat * 71.0f) * jitterAmplitude * 0.55f};
  const float maximumStep = 4.0f + cursorChaos_ * 44.0f;
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
            L"Window realigned to 67 pixels.\nRegulation spacing.",
            L"That window was out of compliance.",
            L"Corrected. Do not thank me in writing.",
            L"Noted, moved, logged. Next."};
        std::uniform_int_distribution<std::size_t> pick(0, lines.size() - 1);
        SetBubble(lines[pick(random_)], 5.0);
      } else {
        SetBubble(L"Window withdrew from inspection.\nRecorded as non-cooperation.", 4.0);
      }
      break;
    case PendingActionKind::FauxPanel:
      SetBubble(L"No eligible window on site.\nMeasurement simulated instead.", 4.0);
      break;
    case PendingActionKind::None:
      break;
  }
  pendingAction_ = {};
}

bool GooseRotApp::IsGooseBusy(std::size_t index) const {
  if (index >= geese_.size()) return true;
  if (geese_[index].IsOffstage()) return true;
  if (index == 0 && (cursorLatched_ || pendingAction_.kind != PendingActionKind::None)) return true;
  for (const VisualSprite& sprite : sprites_) {
    if (sprite.HasCarrier() && sprite.carrierIndex == index) return true;
  }
  return false;
}

// Idle geese circle whatever the scene currently cares about — the pointer —
// instead of teleporting their target to a random pixel. The walk then reads as
// a patrol closing in rather than as noise.
Vec2 GooseRotApp::NextPatrolPoint(std::size_t index) {
  const RectF bounds = overlay_.CanvasBounds();
  const float radius = std::min(bounds.Width(), bounds.Height()) * 0.30f;
  ++patrolStep_;
  const float angle = static_cast<float>(patrolStep_) * 0.9f + static_cast<float>(index) * 2.1f;
  const Vec2 point{patrolFocus_.x + std::cos(angle) * radius,
                   patrolFocus_.y + std::sin(angle) * radius * 0.62f};
  const float horizontal = std::min(70.0f, bounds.Width() * 0.5f);
  const float vertical = std::min(70.0f, bounds.Height() * 0.5f);
  return {std::clamp(point.x, bounds.left + horizontal, bounds.right - horizontal),
          std::clamp(point.y, bounds.top + vertical, bounds.bottom - vertical)};
}

void GooseRotApp::UpdateGooseTargets(float deltaSeconds) {
  if (geese_.empty()) return;
  // The goose exists beyond the canvas during the quiet preamble, but must not
  // start walking until the seeded wall-clock delay has elapsed.
  if (initialEntrancePending_) return;
  const RectF bounds = overlay_.CanvasBounds();
  const Vec2 center = bounds.Center();
  const POINT cursorScreen = desktop_->CursorPosition();
  const Vec2 cursor = overlay_.ScreenToCanvas(cursorScreen);
  // The patrol focus trails the pointer instead of snapping to it, so the flock
  // drifts toward the user rather than tracking every twitch.
  patrolFocus_ = Lerp(patrolFocus_, cursor, std::min(1.0f, deltaSeconds * 0.55f));

  // While the lead goose is hauling the pointer or stalking a title bar, the
  // choreography leaves it alone.
  const bool leadBusy = cursorLatched_ || pendingAction_.kind != PendingActionKind::None;

  // A goose that walked out keeps its exit target; one that has made it back on
  // to the canvas rejoins the choreography. A goose still on its way out to
  // collect a photo keeps the flag, or it would be clamped back inside before it
  // ever reaches the edge and the photo would appear out of nowhere.
  std::vector<bool> leaving(geese_.size(), false);
  for (const VisualSprite& sprite : sprites_) {
    if (sprite.stage == PropStage::Fetching && sprite.carrierIndex < leaving.size()) {
      leaving[sprite.carrierIndex] = true;
    }
  }
  for (std::size_t index = 0; index < geese_.size(); ++index) {
    GooseEntity& goose = geese_[index];
    if (!goose.IsOffstage() || leaving[index] || flockOffstage_) continue;
    const Vec2 position = goose.Position();
    const bool inside = position.x > bounds.left + GooseEntity::kBoundsMargin &&
                        position.x < bounds.right - GooseEntity::kBoundsMargin &&
                        position.y > bounds.top + GooseEntity::kBoundsMargin &&
                        position.y < bounds.bottom - GooseEntity::kBoundsMargin;
    if (inside) goose.SetOffstage(false);
  }

  if (!shutdownStarted_ && !flockOffstage_) {
    if (logicalTime_ >= phase::kCircleDance && geese_.size() >= 3) {
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
      if (logicalTime_ >= phase::kResetAura) {
        geese_.front().SetTarget({center.x, bounds.bottom - 98.0f}, SpeedTier::Charge, true);
      }
    } else if (logicalTime_ >= phase::kFinalMonologue && geese_.size() >= 3) {
      constexpr float kGoldenAngle = 2.39996323f;
      const float maximumRadius = std::min(bounds.Width(), bounds.Height()) * 0.44f;
      for (std::size_t index = 0; index < geese_.size(); ++index) {
        const float angle = static_cast<float>(logicalTime_ * 1.8) + index * kGoldenAngle;
        const float radius = std::min(maximumRadius, 58.0f + std::sqrt(static_cast<float>(index)) * 43.0f);
        geese_[index].SetTarget(cursor + Vec2{std::cos(angle) * radius, std::sin(angle) * radius},
                                SpeedTier::Charge, true);
      }
    } else if (logicalTime_ >= phase::kGraffiti && logicalTime_ < phase::kScreenShake &&
               geese_.size() >= 3) {
      const float progress = GraffitiProgress();
      const RectF tag = TagZone(bounds);
      if (!leadBusy) {
        if (progress < 1.0f) {
          // Goose 1 walks the tag while it is being sprayed: the paint follows
          // the beak instead of appearing out of nowhere.
          const Vec2 nozzle = overlay_.GraffitiPaintHead(progress);
          geese_[0].SetTarget(nozzle - Vec2{26.0f, 0.0f}, SpeedTier::Run, true);
        } else {
          geese_[0].SetTarget({tag.left - 46.0f, tag.bottom - 30.0f}, SpeedTier::Run, true);
        }
      }
      // The rest of the flock lines up either side of the wall and bobs at it,
      // rather than orbiting through the middle of the tag being painted.
      for (std::size_t index = 1; index < geese_.size(); ++index) {
        if (IsGooseBusy(index)) continue;
        const float sway = std::sin(static_cast<float>(logicalTime_ * 2.4) +
                                    static_cast<float>(index) * 1.9f) * 30.0f;
        const bool leftSide = index % 2 == 1;
        const float lane = 52.0f + static_cast<float>(index / 2) * 48.0f;
        const float x = leftSide ? tag.left - lane : tag.right + lane;
        geese_[index].SetTarget(
            {std::clamp(x, bounds.left + 60.0f, bounds.right - 60.0f),
             std::clamp(tag.Center().y + sway, bounds.top + 60.0f, bounds.bottom - 60.0f)},
            SpeedTier::Run, false);
      }
    } else if (logicalTime_ >= phase::kDuplicate && logicalTime_ < phase::kGraffiti &&
               geese_.size() >= 3) {
      // A wedge behind the leader: the trio moves as one unit toward the
      // pointer instead of standing on three fixed marks.
      const Vec2 head = Lerp(center, patrolFocus_, 0.55f);
      if (!leadBusy) geese_[0].SetTarget(head, SpeedTier::Run, true);
      for (std::size_t index = 1; index < geese_.size(); ++index) {
        if (IsGooseBusy(index)) continue;
        const float sign = index % 2 == 1 ? -1.0f : 1.0f;
        const float rank = static_cast<float>((index + 1) / 2);
        geese_[index].SetTarget(head + Vec2{sign * 86.0f * rank, 62.0f * rank}, SpeedTier::Run);
      }
    } else if (inspectionRound_ && logicalTime_ < phase::kNotepad && !IsGooseBusy(0)) {
      // The opening round is a route, not a wander: the inspector visits the
      // four corners of the site and the middle, in order, and says what it
      // found at each one before moving on.
      constexpr std::array<std::pair<float, float>, 5> stations = {
          {{0.16f, 0.24f}, {0.84f, 0.26f}, {0.82f, 0.78f}, {0.18f, 0.76f}, {0.50f, 0.52f}}};
      constexpr std::array<const wchar_t*, 5> remarks = {
          L"Top left. Dust. Noted.",
          L"Top right. This is where the scorecard goes.",
          L"Bottom right. Something lives here.",
          L"Bottom left. No comment. Written down anyway.",
          L"Centre of the site. This will do."};
      GooseEntity& inspector = geese_.front();
      if (inspector.DistanceToTarget() < 22.0f || inspectionStation_ < 0) {
        inspectionStation_ = std::min<int>(inspectionStation_ + 1,
                                           static_cast<int>(stations.size()) - 1);
        const auto& station = stations[static_cast<std::size_t>(inspectionStation_)];
        // Five stations in thirty seconds: brisk, which is also how somebody
        // with a clipboard and a schedule actually moves.
        inspector.SetTarget({bounds.left + bounds.Width() * station.first,
                             bounds.top + bounds.Height() * station.second},
                            SpeedTier::Run, true);
        SetBubble(remarks[static_cast<std::size_t>(inspectionStation_)], 6.0);
      }
    } else {
      for (std::size_t index = 0; index < geese_.size(); ++index) {
        if (IsGooseBusy(index)) continue;
        if (geese_[index].DistanceToTarget() < 18.0f) {
          geese_[index].SetTarget(NextPatrolPoint(index), SpeedTier::Walk);
        }
      }
    }

    // A goose fetching or carrying a photo overrides that goose's choreography
    // so the prop visibly travels across the desktop before being put down.
    // Only the first prop per carrier may steer it; without this guard several
    // in-flight props reassign the same goose's target every frame and it jitters.
    std::vector<bool> carrierSteered(geese_.size(), false);
    for (const VisualSprite& sprite : sprites_) {
      if (!sprite.HasCarrier() || sprite.carrierIndex >= geese_.size()) continue;
      if (carrierSteered[sprite.carrierIndex]) continue;
      if (sprite.carrierIndex == 0 && leadBusy) continue;
      carrierSteered[sprite.carrierIndex] = true;
      GooseEntity& carrier = geese_[sprite.carrierIndex];
      // Hurrying out to collect it, then walking it back in so the photo in the
      // beak is actually readable on the way.
      const bool fetching = sprite.stage == PropStage::Fetching;
      const Vec2 destination = fetching ? sprite.fetchPoint : sprite.targetCenter;
      carrier.SetTarget(carrier.BodyTargetForBeak(destination),
                        fetching ? SpeedTier::Charge : SpeedTier::Run, true);
    }
  }

  for (GooseEntity& goose : geese_) goose.Update(deltaSeconds, bounds);
}

void GooseRotApp::UpdateSprites() {
  const RectF bounds = overlay_.CanvasBounds();
  for (VisualSprite& sprite : sprites_) {
    if (sprite.HasCarrier() && sprite.carrierIndex >= geese_.size()) {
      // The carrier disappeared under us: put the photo down where it stands.
      sprite.stage = PropStage::Placed;
      sprite.stageChangedAt = logicalTime_;
      sprite.center = sprite.targetCenter;
      sprite.everPlaced = true;
      continue;
    }
    switch (sprite.stage) {
      case PropStage::Fetching: {
        GooseEntity& carrier = geese_[sprite.carrierIndex];
        const Vec2 position = carrier.Position();
        // One rule covers both errands: the beak has arrived at whatever it was
        // sent to pick up. For a new photo that point is outside the canvas, so
        // being off screen counts too and the photo can appear unseen.
        const bool offCanvas = position.x < bounds.left || position.x > bounds.right ||
                               position.y < bounds.top || position.y > bounds.bottom;
        const bool reached = carrier.BeakDistanceTo(sprite.fetchPoint) < 34.0f;
        if (reached || (offCanvas && !sprite.everPlaced) ||
            logicalTime_ >= sprite.stageDeadline) {
          sprite.stage = PropStage::Carried;
          sprite.stageChangedAt = logicalTime_;
          sprite.stageDeadline =
              logicalTime_ + DeliveryDeadline(carrier.Position(), sprite.targetCenter);
          sprite.center = carrier.Rig().beakTip;
          carrier.Honk(0.3f);
        }
        break;
      }
      case PropStage::Carried: {
        const GooseEntity& carrier = geese_[sprite.carrierIndex];
        sprite.center = carrier.Rig().beakTip;
        if (carrier.BeakDistanceTo(sprite.targetCenter) < 28.0f ||
            logicalTime_ >= sprite.stageDeadline) {
          sprite.stage = PropStage::Placed;
          sprite.stageChangedAt = logicalTime_;
          sprite.center = sprite.targetCenter;
          sprite.everPlaced = true;
        }
        break;
      }
      case PropStage::Placed:
      case PropStage::Tearing:
        break;
    }
  }

  // A torn photo plays its rip and then leaves for good.
  sprites_.erase(std::remove_if(sprites_.begin(), sprites_.end(),
                                [this](const VisualSprite& sprite) {
                                  return sprite.stage == PropStage::Tearing &&
                                         logicalTime_ - sprite.stageChangedAt > 0.45;
                                }),
                 sprites_.end());
  // Erasing shifts every later index, and carrier indices point into `geese_`,
  // not into `sprites_`, so nothing needs remapping here.

  if (logicalTime_ < phase::kGooseReturn || logicalTime_ >= phase::kCompanionCutoff) return;
  const bool owed = pendingPropOrders_ > 0;
  if (!owed && logicalTime_ < nextSpriteAt_) return;
  if (sprites_.size() >= PropLimit(config_.mode)) {
    // The wall is already full, so a replacement owed for a torn photo has
    // nowhere to go; forgetting it here is what stops the debt growing forever.
    pendingPropOrders_ = 0;
    nextSpriteAt_ = logicalTime_ + 2.0;
    return;
  }
  if (SpawnSprite() && owed) --pendingPropOrders_;
  const bool late = logicalTime_ >= phase::kScreenShake;
  std::uniform_real_distribution<double> delay(
      late ? (config_.mode == RunMode::Lab ? 0.8 : 1.3) : 3.0,
      late ? (config_.mode == RunMode::Lab ? 1.6 : 2.8) : 6.0);
  nextSpriteAt_ = logicalTime_ + (owed ? 0.6 : delay(random_));
}

// Every photo arrives in a beak. A free goose walks off the nearest edge, comes
// back with it and pins it down; if no goose is free the spawn is simply
// deferred, because a picture that materialises on its own has no author.
bool GooseRotApp::SpawnSprite(std::size_t forcedCarrier) {
  // Hand the prop to a goose that is not already ferrying one. Capping carried
  // props to one per goose is what keeps the carriers from thrashing.
  std::vector<unsigned char> carrierBusy(geese_.size(), 0U);
  for (const VisualSprite& existing : sprites_) {
    if (existing.HasCarrier() && existing.carrierIndex < carrierBusy.size()) {
      carrierBusy[existing.carrierIndex] = 1U;
    }
  }
  const bool leadBusy = cursorLatched_ || pendingAction_.kind != PendingActionKind::None;
  std::size_t carrier = forcedCarrier;
  if (carrier == kNoCarrier || carrier >= geese_.size() || carrierBusy[carrier]) {
    carrier = PickFreeCarrier(carrierBusy, leadBusy);
  }
  if (carrier == kNoCarrier) return false;

  constexpr std::array<int, 14> resources = {
      IDR_BRAINROT_TRALALERO, IDR_BRAINROT_BALLERINA, IDR_BRAINROT_BOMBARDIRO,
      IDR_CAT_SHOCKED, IDR_CAT_JUDGMENTAL, IDR_CAT_VACANT, IDR_CAT_SUSPICIOUS,
      IDR_USER_GOOSE_ICE_CREAM, IDR_USER_GOOSE_WORKER, IDR_USER_GOOSE_CALL,
      IDR_USER_GOOSE_STORE, IDR_USER_GOOSE_PUNCHY, IDR_USER_GOOSE_GANGSTER,
      IDR_USER_GOOSE_JET};
  std::uniform_int_distribution<std::size_t> resource(0, resources.size() - 1);
  std::uniform_real_distribution<float> size(108.0f, 176.0f);
  std::uniform_real_distribution<float> angle(-12.0f, 12.0f);
  VisualSprite sprite;
  sprite.resourceId = resources[resource(random_)];
  sprite.size = size(random_);
  sprite.angleDegrees = angle(random_);
  sprite.createdAt = logicalTime_;
  sprite.lifetime = std::max(12.0, phase::kEnd + 10.0 - logicalTime_);
  sprite.targetCenter = FindSpriteLandingPoint(sprite.size);
  sprite.carrierIndex = carrier;
  sprite.stageChangedAt = logicalTime_;

  GooseEntity& goose = geese_[carrier];
  if (goose.IsOffstage()) {
    // Already outside the frame — it walks straight back in holding the photo.
    sprite.stage = PropStage::Carried;
    sprite.stageDeadline =
        logicalTime_ + DeliveryDeadline(goose.Position(), sprite.targetCenter);
    sprite.center = goose.Rig().beakTip;
  } else {
    sprite.stage = PropStage::Fetching;
    sprite.fetchPoint = OffstagePointNear(goose.Position());
    sprite.stageDeadline = logicalTime_ + DeliveryDeadline(goose.Position(), sprite.fetchPoint);
    sprite.center = sprite.fetchPoint;
    goose.SetOffstage(true);
  }
  sprites_.push_back(sprite);
  return true;
}

// Tearing a photo off the desktop works, and it is always answered: the aura
// drops, the display tears, two replacements are ordered and the flock grows.
void GooseRotApp::ClosePropAt(std::size_t index) {
  if (index >= sprites_.size()) return;
  VisualSprite& sprite = sprites_[index];
  sprite.stage = PropStage::Tearing;
  sprite.stageChangedAt = logicalTime_;
  ++propsClosed_;

  AddAura(-6700);
  KickGlitch(0.24f + 0.04f * static_cast<float>(std::min(6, propsClosed_)));
  pendingPropOrders_ += 2;
  nextSpriteAt_ = std::min(nextSpriteAt_, logicalTime_ + 0.4);

  constexpr std::array<const wchar_t*, 5> lines = {
      L"You destroyed an exhibit.\nI am fetching two more.",
      L"Tampering with evidence. -6,700 aura.",
      L"That was case material. It is now a finding.",
      L"I left the site to collect that.",
      L"Every removed exhibit is replaced. Twice."};
  std::uniform_int_distribution<std::size_t> pick(0, lines.size() - 1);
  SetBubble(lines[pick(random_)], 5.0);
  if (!geese_.empty()) geese_.front().Honk(0.55f);
  PushToast(L"AURA INSPECTION", L"Exhibit destroyed by the occupant.\nTwo replacements dispatched.");

  // Escalation: from five removed exhibits the inspector calls in more help.
  if (propsClosed_ >= 5 && extraGeese_ < 6) ++extraGeese_;
}

// The tag needs a bare wall. Anything already hanging where the 67 is about to
// go is picked back up and re-hung somewhere else instead of being painted over.
void GooseRotApp::ClearPropsFromTagZone() {
  if (tagZoneCleared_) return;
  tagZoneCleared_ = true;
  const RectF zone = TagZone(overlay_.CanvasBounds());
  std::vector<unsigned char> carrierBusy(geese_.size(), 0U);
  for (const VisualSprite& existing : sprites_) {
    if (existing.HasCarrier() && existing.carrierIndex < carrierBusy.size()) {
      carrierBusy[existing.carrierIndex] = 1U;
    }
  }

  for (VisualSprite& sprite : sprites_) {
    if (sprite.stage != PropStage::Placed) continue;
    const float reach = sprite.size * 0.5f;
    if (sprite.center.x + reach < zone.left || sprite.center.x - reach > zone.right ||
        sprite.center.y + reach < zone.top || sprite.center.y - reach > zone.bottom) {
      continue;
    }
    // Whichever goose is still free takes it. Without one the photo has nobody
    // to move it, so it stays where it is and the tag is simply painted over it.
    const std::size_t carrier = PickFreeCarrier(carrierBusy, true);
    if (carrier == kNoCarrier) continue;
    carrierBusy[carrier] = 1U;
    sprite.carrierIndex = carrier;
    sprite.targetCenter = FindSpriteLandingPoint(sprite.size);
    // The goose walks over to the photo and takes it off the wall; it stays
    // hanging where it is until a beak actually reaches it.
    sprite.fetchPoint = sprite.center;
    sprite.stage = PropStage::Fetching;
    sprite.stageChangedAt = logicalTime_;
    sprite.stageDeadline =
        logicalTime_ + DeliveryDeadline(geese_[carrier].Position(), sprite.fetchPoint);
  }
}

// The overlay is click-through, so photo clicks are read from the pointer
// itself: no hook is installed and no input is captured or synthesised.
void GooseRotApp::UpdatePropInteractions() {
  if (!leftMousePressed_ || sprites_.empty() || !desktop_) return;
  const Vec2 cursor = overlay_.ScreenToCanvas(desktop_->CursorPosition());
  // Topmost first: the badges are drawn in spawn order, so the last match wins.
  for (std::size_t index = sprites_.size(); index-- > 0;) {
    const VisualSprite& sprite = sprites_[index];
    if (!sprite.IsClosable(logicalTime_)) continue;
    if (!PropCloseBoxHit(sprite.center, sprite.size, cursor)) continue;
    leftMousePressed_ = false;
    ClosePropAt(index);
    return;
  }
}

Vec2 GooseRotApp::FindSpriteLandingPoint(float size) {
  const RectF bounds = overlay_.CanvasBounds();
  const RectF tag = TagZone(bounds);
  const float margin = std::max(58.0f, size * 0.55f);
  const float reach = size * 0.5f;
  Vec2 fallback = RandomCanvasPoint(margin);
  float bestClearance = -1.0f;
  for (int attempt = 0; attempt < 48; ++attempt) {
    const Vec2 candidate = RandomCanvasPoint(margin);
    const bool auraHud = candidate.x > bounds.right - 340.0f && candidate.y < bounds.top + 160.0f;
    const bool clipboardZone = candidate.x < bounds.left + 480.0f &&
                               candidate.y > bounds.bottom - 230.0f;
    // The tag's footprint is off limits for the whole run, not only once the
    // paint starts: a photo dropped there early would still be covering the 67
    // an hour later, and on a small screen that hides the tag completely.
    const bool tagZone = candidate.x + reach > tag.left && candidate.x - reach < tag.right &&
                         candidate.y + reach > tag.top && candidate.y - reach < tag.bottom;
    if (auraHud || clipboardZone || tagZone) continue;

    // Keep the least crowded spot seen so far, so a busy desktop still spreads
    // its photos instead of stacking them on the last candidate drawn.
    float clearance = std::numeric_limits<float>::max();
    for (const VisualSprite& existing : sprites_) {
      clearance = std::min(clearance, Distance(candidate, existing.targetCenter) -
                                          (size + existing.size) * 0.34f);
    }
    if (clearance >= 0.0f) return candidate;
    if (clearance > bestClearance) {
      bestClearance = clearance;
      fallback = candidate;
    }
  }
  return fallback;
}

std::size_t GooseRotApp::DesiredGooseCount() const {
  const std::size_t bonus = static_cast<std::size_t>(std::clamp(extraGeese_, 0, 6));
  if (logicalTime_ < phase::kDuplicate) return 1;
  if (logicalTime_ < phase::kScreenShake) return 3 + bonus;
  const double progress = std::clamp(
      (logicalTime_ - phase::kScreenShake) / (phase::kResetAura - phase::kScreenShake), 0.0, 1.0);
  const std::size_t flock =
      static_cast<std::size_t>(3 + std::floor(std::pow(progress, 0.72) * 64.0)) + bonus;
  return std::min(flock, kMaximumGeese);
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
  if (shutdownRequested_) return;
  shutdownRequested_ = true;
  completedTimeline_ = true;
  if (Cleanup()) StartShutdownVisuals();
}

void GooseRotApp::StartShutdownVisuals() {
  if (shutdownStarted_) return;
  // Cleanup may spend up to a little over one second waiting for child apps.
  // Restart the frame clock here so that wait is not charged against the 7.5
  // seconds of explosion/farewell visuals that begin only after restoration.
  LARGE_INTEGER afterCleanup{};
  if (QueryPerformanceCounter(&afterCleanup)) lastCounter_ = afterCleanup;
  shutdownStarted_ = true;
  shutdownStartedRealTime_ = realTime_;
}

bool GooseRotApp::Cleanup() {
  if (cleanupDone_) return true;
  if (errorSoundActive_) {
    PlaySoundW(nullptr, nullptr, 0);
    errorSoundActive_ = false;
  }
  auraPrompt_.Close();
  sigmaPrompt_.Close();
  notepad_.Close();
  popups_.CloseAll();
  // Whatever the swarm was refusing, it goes away here: cleanup and the
  // Emergency/error exits hand the Windows keys back immediately. A completed
  // run keeps them guarded through its short farewell visual; Run() releases
  // them the instant that final frame loop ends.
  if (!shutdownRequested_ || !completedTimeline_) windowsKeyGuard_.Close();
  taskbarGuard_.Close();
  ownedWindowsApps_.CloseAll();
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
  state.shutdownAge = shutdownStartedRealTime_ >= 0.0
                          ? std::max(0.0, realTime_ - shutdownStartedRealTime_)
                          : -1.0;
  state.mode = config_.mode;
  state.seed = config_.seed;
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
  ChaosVisualCue cue = EvaluateChaosVisualCue(
      logicalTime_, realTime_, config_.seed,
      config_.flashesEnabled && !config_.reducedMotion);
  if (config_.reducedMotion) cue.faultRibbonIntensity *= 0.24f;
  state.screenFlash = cue.flashIntensity;
  state.faultRibbon = cue.faultRibbonIntensity;
  // The aperture starts closing nine seconds before the end and the exposure
  // is cranked over the same stretch, so the desktop is already a blown-out
  // porthole by the time the file is actually closed.
  const float irisProgress = static_cast<float>(
      std::clamp((logicalTime_ - phase::kIrisStart) / (phase::kEnd - phase::kIrisStart),
                 0.0, 1.0));
  state.finalIris = irisProgress * irisProgress * (3.0f - 2.0f * irisProgress);
  state.finalExposure = static_cast<float>(std::pow(
      std::clamp((logicalTime_ - phase::kIrisStart - 2.0) / (phase::kEnd - phase::kIrisStart - 2.0),
                 0.0, 1.0),
      1.8));
  state.effectPattern = cue.pattern;
  state.reducedMotion = config_.reducedMotion;
  state.graffitiProgress = GraffitiProgress();
  state.propsClosed = propsClosed_;
  state.auraVisible = auraVisible_;
  state.auraRevealedAt = auraRevealedAt_;
  state.cursorStormPhase = logicalTime_ >= phase::kScreenShake &&
                           logicalTime_ < phase::kEnd && !shutdownStarted_;
  state.cursorLatched = cursorLatched_;
  state.clipboardBadge = logicalTime_ >= phase::kClipboard && logicalTime_ < phase::kGraffiti;
  state.graffiti = logicalTime_ >= phase::kGraffiti && logicalTime_ < phase::kEnd;
  state.colorFilter = logicalTime_ >= phase::kColorFilter && logicalTime_ < phase::kEnd;
  state.finalMonologue = logicalTime_ >= phase::kFinalMonologue && logicalTime_ < phase::kEnd;
  state.countdown = logicalTime_ >= phase::kCountdown && logicalTime_ < phase::kEnd;
  state.resetButton = logicalTime_ >= phase::kResetAura && logicalTime_ < phase::kEnd;
  // BeginShutdown performs cleanup before this branch becomes visible.  In
  // particular, --start-at 360 must not flash the explosion one frame before
  // the first tick has restored the desktop.
  state.fakeShutdown = shutdownStarted_;
  state.flashesEnabled = config_.flashesEnabled && !config_.reducedMotion;
  if (logicalTime_ <= bubbleUntil_ && !geese_.empty()) {
    // A goose that is off screen has nothing to attach a balloon to; those
    // moments are carried by the fake notifications instead.
    const Vec2 anchor = geese_.front().Position();
    const RectF bounds = overlay_.CanvasBounds();
    if (anchor.x >= bounds.left && anchor.x <= bounds.right && anchor.y >= bounds.top &&
        anchor.y <= bounds.bottom) {
      state.bubbleText = bubbleText_;
      state.bubbleAnchor = anchor;
    }
  }
  return state;
}

}  // namespace gooserot
