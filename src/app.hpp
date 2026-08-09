#pragma once

#include <windows.h>

#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "companion_windows.hpp"
#include "core.hpp"
#include "desktop_director.hpp"
#include "overlay_window.hpp"
#include "recovery_watchdog.hpp"

namespace gooserot {

class GooseRotApp {
 public:
  using ConclusionHook = std::function<bool(std::wstring&)>;

  GooseRotApp(HINSTANCE instance, AppConfig config,
              ConclusionHook conclusionHook = {});
  ~GooseRotApp();

  bool Initialize(std::wstring& error);
  int Run();

 private:
  enum class PendingActionKind { None, Cursor, Window, FauxPanel };
  struct PendingAction {
    PendingActionKind kind = PendingActionKind::None;
    double executeAt = 0.0;
    WindowTarget windowTarget{};
    int direction = 0;
  };

  void Tick();
  void HandleEvent(const TimelineEvent& event);
  void ApplyBaseline(double logicalTime);
  bool UpdateEmergencyExit(double realDeltaSeconds);
  void PollMouseButton();
  void UpdatePrompts();
  void UpdateNotepad();
  void UpdateErrorSounds();
  void UpdateOwnedWindowsApps();
  void UpdatePopups();
  void UpdateTaskbarGuard();
  void FileFinding(const wchar_t* text);
  void UpdateDesktopActions();
  void UpdateGooseTargets(float deltaSeconds);
  void UpdateSprites();
  void UpdatePropInteractions();
  void UpdateCursorGrab(double logicalDelta);
  void UpdateCursorChaos();
  void UpdateToasts();
  void UpdateGlitch(double logicalDelta);
  void EnsureGooseCount();
  void ScheduleCursorAction(bool userTriggered);
  void ScheduleWindowAction();
  void ExecutePendingAction();
  void BeginCursorGrab();
  void EndCursorGrab(bool succeeded);
  void SendFlockOffstage();
  void BringFlockBack();
  bool SpawnSprite(std::size_t forcedCarrier = kNoCarrier);
  void ClosePropAt(std::size_t index);
  void ClearPropsFromTagZone();
  Vec2 FindSpriteLandingPoint(float size);
  Vec2 OffstagePointNear(Vec2 origin) const;
  Vec2 NextPatrolPoint(std::size_t index);
  bool IsGooseBusy(std::size_t index) const;
  void PushToast(std::wstring title, std::wstring body, double lifetime = 6.5);
  void AddAura(int delta);
  void KickGlitch(float amount);
  void SetBubble(std::wstring text, double durationSeconds);
  void AimLeadGooseBeakAt(Vec2 target, SpeedTier tier);
  void BeginShutdown();
  void StartShutdownVisuals();
  bool Cleanup();
  bool LaunchBootPreview();
  RenderState BuildRenderState() const;
  Vec2 RandomCanvasPoint(float margin);
  float GraffitiProgress() const;
  std::size_t DesiredGooseCount() const;

  HINSTANCE instance_ = nullptr;
  AppConfig config_;
  ConclusionHook conclusionHook_;
  OverlayWindow overlay_;
  RecoveryWatchdog watchdog_;
  std::unique_ptr<DesktopDirector> desktop_;
  TimelineEngine timeline_;
  std::vector<GooseEntity> geese_;
  std::vector<VisualSprite> sprites_;
  std::vector<ToastNotice> toasts_;
  PromptWindow auraPrompt_;
  PromptWindow sigmaPrompt_;
  NotepadWindow notepad_;
  Typewriter typist_;
  OwnedWindowsApps ownedWindowsApps_;
  PopupSwarm popups_;
  TaskbarGuard taskbarGuard_;
  WindowsKeyGuard windowsKeyGuard_;
  std::mt19937 random_;
  std::mt19937 audioRandom_;
  std::mt19937 keyboardRandom_;

  LARGE_INTEGER performanceFrequency_{};
  LARGE_INTEGER lastCounter_{};
  double logicalTime_ = 0.0;
  double realTime_ = 0.0;
  double nextErrorSoundAt_ = -1.0;
  double nextWindowAction_ = phase::kCursorAndWindows;
  double nextCursorAction_ = phase::kCursorAndWindows + 15.0;
  double nextSpriteAt_ = phase::kGooseReturn;
  double nextGooseArrivalAt_ = 0.0;
  double nextOwnedAppAtReal_ = 0.0;
  double bubbleUntil_ = 0.0;
  double emergencyHeldSeconds_ = 0.0;
  double auraPromptArmedAt_ = -1.0;
  double auraDeltaAt_ = -1000.0;
  double grabStartedAt_ = -1.0;
  double grabDeadline_ = -1.0;
  double lastTaskbarGuardPokeAt_ = -1000.0;
  // When the scorecard was pinned up. Before that it does not exist at all.
  double auraRevealedAt_ = -1.0;
  double shutdownStartedRealTime_ = -1.0;
  int auraDelta_ = 0;
  int grabRemainingPixels_ = 0;
  int grabDirection_ = 1;
  bool grabFlipped_ = false;
  bool cursorLatched_ = false;
  float glitch_ = 0.0f;
  float glitchBoost_ = 0.0f;
  float cursorChaos_ = 0.0f;
  bool auraPromptPending_ = false;
  bool leftMouseWasDown_ = false;
  // Set once per fresh press by PollMouseButton and consumed by whichever gag
  // gets to it first, so a single click never triggers two reactions.
  bool leftMousePressed_ = false;
  bool flockOffstage_ = false;
  bool shutdownRequested_ = false;
  bool shutdownStarted_ = false;
  bool cleanupDone_ = false;
  bool completedTimeline_ = false;
  bool exiting_ = false;
  bool conclusionHandled_ = false;
  bool finalBlack_ = false;
  bool bootPreviewLaunchFailed_ = false;
  bool recoveryFailure_ = false;
  bool errorSoundActive_ = false;
  bool windowsKeyGuardAttempted_ = false;
  bool tagZoneCleared_ = false;
  bool auraVisible_ = false;
  bool inspectionRound_ = false;
  bool initialEntrancePending_ = false;
  int aura_ = 0;
  int exitCode_ = 0;
  // Photos the user tore off the desktop, and the replacements still owed.
  int propsClosed_ = 0;
  int pendingPropOrders_ = 0;
  // Extra geese earned by closing photos, on top of the timeline's own count.
  int extraGeese_ = 0;
  unsigned patrolStep_ = 0;
  // -1 until the inspector reaches its first station on the opening round.
  int inspectionStation_ = -1;
  Vec2 patrolFocus_;
  Vec2 initialEntranceTarget_;
  POINT auraReferenceCursor_{};
  PendingAction pendingAction_;
  std::wstring bubbleText_;
  std::wstring notepadText_;
  std::wstring conclusionError_;
};

}  // namespace gooserot
