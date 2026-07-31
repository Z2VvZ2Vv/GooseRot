#pragma once

#include <windows.h>
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

struct VisualSprite {
  int resourceId = 0;
  Vec2 center;
  Vec2 targetCenter;
  float size = 180.0f;
  float angleDegrees = 0.0f;
  double createdAt = 0.0;
  double lifetime = 320.0;
  double deliveryDeadline = 0.0;
  std::size_t carrierIndex = 0;
  bool carried = false;
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
  RunMode mode = RunMode::Safe;
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
  // 0 = bare wall, 1 = the 67 tag is finished.
  float graffitiProgress = 0.0f;
  int popupCount = 0;
  bool cursorLatched = false;
  bool clipboardBadge = false;
  bool graffiti = false;
  bool colorFilter = false;
  bool finalMonologue = false;
  bool countdown = false;
  bool resetButton = false;
  bool fakeShutdown = false;
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
  bool IsPreview() const { return preview_; }
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
  };

  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  bool RecreateSurface(int width, int height);
  bool LoadImages();
  std::unique_ptr<ResourceImage> LoadResourceImage(int resourceId);
  Gdiplus::Image* FindImage(int resourceId) const;
  CachedSprite* FindCachedSprite(int resourceId, int sizePixels, int angleTenths);
  void BlendCachedSprite(const CachedSprite& sprite, int destinationX, int destinationY);

  void DrawPreviewDesktop(Gdiplus::Graphics& graphics) const;
  void DrawGoose(Gdiplus::Graphics& graphics, const GooseEntity& goose, int index) const;
  void DrawGooseCompact(Gdiplus::Graphics& graphics, const GooseEntity& goose, int index) const;
  void DrawSpeechBubble(Gdiplus::Graphics& graphics, const std::wstring& text, Vec2 anchor) const;
  void DrawSprites(Gdiplus::Graphics& graphics, const RenderState& state);
  void DrawHud(Gdiplus::Graphics& graphics, const RenderState& state) const;
  void DrawSceneEffects(Gdiplus::Graphics& graphics, const RenderState& state);
  void DrawClipboardBadge(Gdiplus::Graphics& graphics, const RenderState& state) const;
  void DrawGraffiti(Gdiplus::Graphics& graphics, const RenderState& state);
  void DrawGraffitiUncached(Gdiplus::Graphics& graphics, const RenderState& state) const;
  bool BuildGraffitiCache(const RenderState& state);
  void DrawGlitch(Gdiplus::Graphics& graphics, const RenderState& state) const;
  void DrawToasts(Gdiplus::Graphics& graphics, const RenderState& state) const;
  void DrawCursorLatch(Gdiplus::Graphics& graphics, const RenderState& state) const;
  void DrawFakeShutdown(Gdiplus::Graphics& graphics, const RenderState& state) const;
  void DismissShellSurface();
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
  bool primaryMonitorOnly_ = false;
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
  ULONG_PTR gdiplusToken_ = 0;
  std::function<void()> tickHandler_;
  std::function<void()> closeHandler_;
  std::vector<std::pair<int, std::unique_ptr<ResourceImage>>> images_;
  std::unordered_map<std::uint64_t, std::unique_ptr<CachedSprite>> spriteCache_;
  unsigned spriteCacheBuildsThisFrame_ = 0;
  std::unique_ptr<CachedSprite> graffitiCache_;
  int graffitiCacheX_ = 0;
  int graffitiCacheY_ = 0;
  int graffitiCacheCanvasWidth_ = 0;
  int graffitiCacheCanvasHeight_ = 0;
};

}  // namespace gooserot
