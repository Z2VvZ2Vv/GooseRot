#pragma once

#include <windows.h>

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
  GooseRotApp(HINSTANCE instance, AppConfig config);
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
  void UpdatePrompts();
  void UpdateNotepad();
  void UpdateDesktopActions();
  void UpdateGooseTargets(float deltaSeconds);
  void UpdateSprites();
  void ScheduleCursorAction(bool userTriggered);
  void ScheduleWindowAction();
  void ExecutePendingAction();
  void SpawnSprite();
  void SetBubble(std::wstring text, double durationSeconds);
  void BeginShutdown();
  bool Cleanup();
  bool LaunchBootPreview();
  RenderState BuildRenderState() const;
  Vec2 RandomCanvasPoint(float margin);

  HINSTANCE instance_ = nullptr;
  AppConfig config_;
  OverlayWindow overlay_;
  RecoveryWatchdog watchdog_;
  std::unique_ptr<DesktopDirector> desktop_;
  TimelineEngine timeline_;
  std::vector<GooseEntity> geese_;
  std::vector<VisualSprite> sprites_;
  PromptWindow auraPrompt_;
  PromptWindow sigmaPrompt_;
  NotepadWindow notepad_;
  std::mt19937 random_;

  LARGE_INTEGER performanceFrequency_{};
  LARGE_INTEGER lastCounter_{};
  double logicalTime_ = 0.0;
  double lastTypedAt_ = 0.0;
  double nextWindowAction_ = 60.0;
  double nextCursorAction_ = 75.0;
  double nextSpriteAt_ = 90.0;
  double bubbleUntil_ = 0.0;
  double emergencyHeldSeconds_ = 0.0;
  double auraPromptArmedAt_ = -1.0;
  bool auraPromptPending_ = false;
  bool leftMouseWasDown_ = false;
  bool shutdownStarted_ = false;
  bool cleanupDone_ = false;
  bool completedTimeline_ = false;
  bool exiting_ = false;
  bool conclusionHandled_ = false;
  bool bootPreviewLaunchFailed_ = false;
  bool recoveryFailure_ = false;
  int aura_ = 0;
  int exitCode_ = 0;
  int typedWordCount_ = 0;
  POINT auraReferenceCursor_{};
  PendingAction pendingAction_;
  std::wstring bubbleText_;
  std::wstring notepadText_;
};

}  // namespace gooserot
