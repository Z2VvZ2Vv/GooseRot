#pragma once

#include <windows.h>
#include <unknwn.h>
#include <objidl.h>
#include <wtypes.h>
#include <gdiplus.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core.hpp"

namespace gooserot {

// A brainrot photo, from the moment a goose is sent to fetch it to the moment
// the user tears it off the desktop.
//
//   Fetching -> the carrier is walking off the edge; nothing is drawn yet
//   Carried  -> the photo is in the beak, crossing the desktop
//   Placed   -> it is pinned down, and its [x] badge becomes clickable
//   Tearing  -> it was closed; a short rip plays before it is removed
enum class PropStage : unsigned char { Fetching, Carried, Placed, Tearing };

struct VisualSprite {
  int resourceId = 0;
  Vec2 center;
  Vec2 targetCenter;
  // Where the carrier leaves the frame to collect it.
  Vec2 fetchPoint;
  float size = 180.0f;
  float angleDegrees = 0.0f;
  double createdAt = 0.0;
  double lifetime = 320.0;
  // Bound on the current stage, so a stuck carrier can never strand a photo.
  double stageDeadline = 0.0;
  double stageChangedAt = 0.0;
  std::size_t carrierIndex = 0;
  PropStage stage = PropStage::Placed;
  // True once the photo has been pinned down at least once. A photo on its first
  // trip is not in the world yet and must not be drawn; one being picked back up
  // off the wall stays visible while its carrier walks over to collect it.
  bool everPlaced = false;

  bool HasCarrier() const {
    return stage == PropStage::Fetching || stage == PropStage::Carried;
  }
  bool IsVisible() const { return stage != PropStage::Fetching || everPlaced; }
  // Only a settled photo may be closed: no clicking props out of a beak.
  bool IsClosable(double logicalTime) const {
    return stage == PropStage::Placed && logicalTime - stageChangedAt >= 0.9;
  }
};

// Fake system notification drawn inside the overlay. Nothing is ever posted to
// the real Windows notification centre.
struct ToastNotice {
  std::wstring title;
  std::wstring body;
  double createdAt = 0.0;
  double lifetime = 6.5;
};

struct RenderState {
  double logicalTime = 0.0;
  double shutdownAge = -1.0;
  RunMode mode = RunMode::Safe;
  PerformanceTier performanceTier = PerformanceTier::Medium;
  float layoutScale = 1.0f;
  std::size_t detailedGooseLimit = 2U;
  std::uint32_t seed = 67;
  const std::vector<GooseEntity>* geese = nullptr;
  const std::vector<VisualSprite>* sprites = nullptr;
  const std::vector<ToastNotice>* toasts = nullptr;
  std::wstring bubbleText;
  Vec2 bubbleAnchor;
  int aura = 0;
  int auraDelta = 0;
  double auraDeltaAt = -1000.0;
  POINT cursor{};
  float emergencyProgress = 0.0f;
  // 0 = clean desktop, 1 = the display is actively falling apart.
  float glitch = 0.0f;
  // 0 = normal pointer, 1 = the flock is violently steering it.
  float cursorChaos = 0.0f;
  // Bounded, real-time-governed pulse and displacement strength.
  float screenFlash = 0.0f;
  float faultRibbon = 0.0f;
  // Narrative effects fade in over several seconds instead of switching on at
  // their phase boundary.
  float screenShakeIntensity = 0.0f;
  float colorFilterIntensity = 0.0f;
  float finalIris = 0.0f;
  // 0 = normal exposure, 1 = the aperture has blown the whole frame to white.
  float finalExposure = 0.0f;
  std::uint32_t effectPattern = 0;
  // 0 = bare wall, 1 = the 67 tag is finished.
  float graffitiProgress = 0.0f;
  int propsClosed = 0;
  // The scorecard only exists once the inspector has started scoring; before
  // that there is no counter on screen at all.
  bool auraVisible = false;
  double auraRevealedAt = -1.0;
  // True during the storm phase, including the windows where the pointer has
  // been handed back, so the HUD can say which of the two is happening.
  bool cursorStormPhase = false;
  bool cursorLatched = false;
  bool clipboardBadge = false;
  bool graffiti = false;
  bool colorFilter = false;
  bool finalMonologue = false;
  bool countdown = false;
  bool resetButton = false;
  bool fakeShutdown = false;
  bool finalBlack = false;
  bool flashesEnabled = true;
  bool reducedMotion = false;
};

class OverlayWindow {
 public:
  OverlayWindow() = default;
  ~OverlayWindow();

  bool Create(HINSTANCE instance, bool preview, bool primaryMonitorOnly,
              std::function<void()> tickHandler, std::function<void()> closeHandler,
              std::wstring& error);
  void Render(const RenderState& state);
  void Close();
  void RequestClose();
  // Stops the WM_TIMER heartbeat so the caller's own frame pump owns cadence.
  void StopRenderTimer();

  HWND Handle() const { return window_; }
  RectF CanvasBounds() const;
  Vec2 ScreenToCanvas(POINT screenPoint) const;
  // Inverse of ScreenToCanvas, so a companion window can be opened exactly
  // where something happened on the canvas.
  POINT CanvasToScreen(Vec2 canvasPoint) const;
  bool IsPreview() const { return preview_; }
  bool IsPrimaryMonitorOnly() const { return primaryMonitorOnly_; }
  // Where the spray can currently is, so the geese can follow their own tag.
  Vec2 GraffitiPaintHead(float progress) const;

 private:
  struct ResourceImage {
    HGLOBAL memory = nullptr;
    IStream* stream = nullptr;
    std::unique_ptr<Gdiplus::Image> image;
    std::unique_ptr<Gdiplus::Bitmap> thumbnail;
    ~ResourceImage();
  };

  struct CachedSprite {
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    std::vector<std::uint32_t> pixels;
    int width = 0;
    int height = 0;
    unsigned lastUsedFrame = 0;
  };

  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  static DWORD WINAPI ForegroundThreadProcedure(void* context);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  bool RecreateSurface(int width, int height);
  bool LoadImages();
  std::unique_ptr<ResourceImage> LoadResourceImage(int resourceId);
  Gdiplus::Image* FindImage(int resourceId) const;
  CachedSprite* FindCachedSprite(int resourceId, int sizePixels, int angleTenths);
  void BlendCachedSprite(const CachedSprite& sprite, int destinationX, int destinationY);

  void DrawPreviewDesktop(Gdiplus::Graphics& graphics);
  void DrawGoose(Gdiplus::Graphics& graphics, const GooseEntity& goose, int index) const;
  void DrawGooseCompact(Gdiplus::Graphics& graphics, const GooseEntity& goose, int index) const;
  void DrawSpeechBubble(Gdiplus::Graphics& graphics, const std::wstring& text, Vec2 anchor) const;
  void DrawSprites(Gdiplus::Graphics& graphics, const RenderState& state);
  void DrawPropCloseBadge(Gdiplus::Graphics& graphics, const VisualSprite& sprite,
                          bool compact) const;
  void DrawHud(Gdiplus::Graphics& graphics, const RenderState& state) const;
  void DrawSceneEffects(Gdiplus::Graphics& graphics, const RenderState& state);
  void DrawClipboardBadge(Gdiplus::Graphics& graphics, const RenderState& state) const;
  void DrawGraffiti(Gdiplus::Graphics& graphics, const RenderState& state);
  void DrawGraffitiUncached(Gdiplus::Graphics& graphics, const RenderState& state) const;
  bool BuildGraffitiCache(const RenderState& state);
  void DrawGlitch(Gdiplus::Graphics& graphics, const RenderState& state) const;
  void DrawChaosFlash(Gdiplus::Graphics& graphics, const RenderState& state) const;
  void ApplyFaultRibbons(const RenderState& state);
  void DrawFinalIris(Gdiplus::Graphics& graphics, const RenderState& state) const;
  void DrawEmergencyExitOverlay(Gdiplus::Graphics& graphics,
                                const RenderState& state) const;
  void DrawToasts(Gdiplus::Graphics& graphics, const RenderState& state) const;
  void DrawCursorLatch(Gdiplus::Graphics& graphics, const RenderState& state) const;
  void DrawFakeShutdown(Gdiplus::Graphics& graphics, const RenderState& state) const;
  void DismissShellSurface();
  bool DismissForegroundShellSurface(HWND candidate);
  void EnsureTopmost();
  bool Present();

  HINSTANCE instance_ = nullptr;
  HWND window_ = nullptr;
  HDC surfaceDc_ = nullptr;
  HBITMAP surfaceBitmap_ = nullptr;
  HGDIOBJ oldBitmap_ = nullptr;
  void* surfacePixels_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  int screenOriginX_ = 0;
  int screenOriginY_ = 0;
  bool preview_ = false;
  // Keep the caller's preference separate from the effective policy. A large
  // virtual desktop may temporarily fall back to the primary monitor and can
  // expand again after a display change.
  bool requestedPrimaryMonitorOnly_ = false;
  bool primaryMonitorOnly_ = false;
  HANDLE foregroundThread_ = nullptr;
  HANDLE foregroundReadyEvent_ = nullptr;
  HANDLE foregroundStopEvent_ = nullptr;
  DWORD foregroundThreadId_ = 0;
  ULONGLONG lastShellDismissAt_ = 0;
  ULONGLONG lastTopmostRefreshAt_ = 0;
  ULONGLONG fpsSampleStartedAt_ = 0;
  unsigned fpsSampleFrames_ = 0;
  double setupSampleMs_ = 0.0;
  double sceneSampleMs_ = 0.0;
  double spriteSampleMs_ = 0.0;
  double gooseSampleMs_ = 0.0;
  double effectsSampleMs_ = 0.0;
  double presentSampleMs_ = 0.0;
  unsigned presentFailureSample_ = 0;
  unsigned topmostFailureSample_ = 0;
  DWORD lastPresentError_ = ERROR_SUCCESS;
  DWORD lastTopmostError_ = ERROR_SUCCESS;
  // Advances once per rendered frame and drives every deterministic wobble.
  unsigned frame_ = 0;
  // Set once per frame: the scene is dense enough that decoration has to give
  // way to throughput.
  bool heavyScene_ = false;
  ULONG_PTR gdiplusToken_ = 0;
  std::function<void()> tickHandler_;
  std::function<void()> closeHandler_;
  std::vector<std::pair<int, std::unique_ptr<ResourceImage>>> images_;
  std::unordered_map<std::uint64_t, std::unique_ptr<CachedSprite>> spriteCache_;
  std::size_t spriteCachePixels_ = 0;
  unsigned spriteCacheBuildsThisFrame_ = 0;
  // Preview scenery is static. Keeping one local ARGB copy avoids rebuilding a
  // full-screen gradient and hills on every benchmark/demo frame.
  std::vector<std::uint32_t> previewBackgroundCache_;
  int previewBackgroundWidth_ = 0;
  int previewBackgroundHeight_ = 0;
  std::unique_ptr<CachedSprite> graffitiCache_;
  int graffitiCacheX_ = 0;
  int graffitiCacheY_ = 0;
  int graffitiCacheCanvasWidth_ = 0;
  int graffitiCacheCanvasHeight_ = 0;
  // A canvas that refuses to cache the tag falls back to drawing it live, but
  // it must not retry the expensive build on every single frame.
  int graffitiCacheFailures_ = 0;
};

}  // namespace gooserot
