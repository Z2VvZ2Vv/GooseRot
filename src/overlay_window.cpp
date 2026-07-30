#include "overlay_window.hpp"

#include <objidl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <sstream>

#include "resource.h"

namespace gooserot {
namespace {

using namespace Gdiplus;

constexpr float kPi = 3.14159265358979323846f;

const Color kNeonPink(255, 255, 45, 170);
const Color kMatrixGreen(255, 57, 255, 20);
const Color kCriticalRed(255, 255, 36, 56);
const Color kBubbleWhite(255, 255, 251, 234);
const Color kInk(255, 28, 25, 34);
const Color kGooseWhite(255, 255, 253, 246);
const Color kGooseShade(255, 220, 216, 226);
const Color kBeakOrange(255, 255, 171, 36);
const Color kBeakOrangeDark(255, 178, 98, 8);

// Deterministic value noise. Every wobble, drip and glitch block derives from
// this, so a given seed always redraws the same frame.
std::uint32_t Hash32(std::uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352dU;
  value ^= value >> 15;
  value *= 0x846ca68bU;
  value ^= value >> 16;
  return value;
}

float Noise01(std::uint32_t seed) {
  return static_cast<float>(Hash32(seed) & 0xFFFFFFU) / static_cast<float>(0x1000000U);
}

float NoiseSigned(std::uint32_t seed) { return Noise01(seed) * 2.0f - 1.0f; }

bool IsShellSurfaceProcess(const wchar_t* path) {
  if (!path || !*path) return false;
  const wchar_t* name = std::wcsrchr(path, L'\\');
  name = name ? name + 1 : path;
  constexpr std::array<const wchar_t*, 4> processes = {
      L"StartMenuExperienceHost.exe", L"SearchHost.exe",
      L"SearchApp.exe", L"ShellExperienceHost.exe"};
  return std::any_of(processes.begin(), processes.end(), [name](const wchar_t* candidate) {
    return _wcsicmp(name, candidate) == 0;
  });
}

PointF ToPointF(Vec2 value) { return PointF(value.x, value.y); }

Color WithAlpha(const Color& color, float alpha) {
  const int clamped = std::clamp(static_cast<int>(alpha * 255.0f), 0, 255);
  return Color(static_cast<BYTE>(clamped), color.GetR(), color.GetG(), color.GetB());
}

const wchar_t* PickFamily(std::initializer_list<const wchar_t*> candidates) {
  for (const wchar_t* name : candidates) {
    FontFamily family(name);
    if (family.IsAvailable()) return name;
  }
  return L"Arial";
}

// Resolved once: a heavy poster face, a chunky UI face and a terminal face.
const wchar_t* PosterFamily() {
  static const wchar_t* name = PickFamily({L"Impact", L"Haettenschweiler", L"Arial Black", L"Arial"});
  return name;
}
const wchar_t* UiFamily() {
  static const wchar_t* name = PickFamily({L"Segoe UI", L"Tahoma", L"Arial"});
  return name;
}
const wchar_t* MonoFamily() {
  static const wchar_t* name = PickFamily({L"Consolas", L"Lucida Console", L"Courier New"});
  return name;
}

// A rectangle traced by hand: every edge wanders, one corner is cut off and the
// whole outline boils slowly. This is what keeps the HUD from looking like a
// stack of identical rounded boxes.
void AddWobblyRectangle(GraphicsPath& path, float x, float y, float width, float height,
                        float jitter, std::uint32_t seed) {
  constexpr int kPerEdge = 5;
  std::vector<PointF> points;
  points.reserve(kPerEdge * 4 + 2);
  const float cut = std::min(width, height) * 0.28f;

  auto push = [&](float px, float py, std::uint32_t index) {
    points.push_back(PointF(px + NoiseSigned(seed * 977U + index * 31U) * jitter,
                            py + NoiseSigned(seed * 977U + index * 31U + 7U) * jitter));
  };

  std::uint32_t index = 0;
  for (int step = 0; step < kPerEdge; ++step) {
    const float t = static_cast<float>(step) / kPerEdge;
    push(x + cut + (width - cut) * t, y, index++);
  }
  for (int step = 0; step < kPerEdge; ++step) {
    const float t = static_cast<float>(step) / kPerEdge;
    push(x + width, y + height * t, index++);
  }
  for (int step = 0; step < kPerEdge; ++step) {
    const float t = static_cast<float>(step) / kPerEdge;
    push(x + width - width * t, y + height, index++);
  }
  for (int step = 0; step < kPerEdge; ++step) {
    const float t = static_cast<float>(step) / kPerEdge;
    push(x, y + height - (height - cut) * t, index++);
  }
  points.push_back(PointF(x + cut, y));
  path.AddPolygon(points.data(), static_cast<INT>(points.size()));
}

void FillCircle(Graphics& graphics, Brush& brush, Vec2 center, float radius) {
  graphics.FillEllipse(&brush, center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);
}

void StrokeCircle(Graphics& graphics, Pen& pen, Vec2 center, float radius) {
  graphics.DrawEllipse(&pen, center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);
}

void DrawCenteredText(Graphics& graphics, const std::wstring& text, const Font& font,
                      const RectF& rectangle, Brush& brush) {
  StringFormat format;
  format.SetAlignment(StringAlignmentCenter);
  format.SetLineAlignment(StringAlignmentCenter);
  format.SetTrimming(StringTrimmingEllipsisWord);
  graphics.DrawString(text.c_str(), -1, &font,
                      Gdiplus::RectF(rectangle.left, rectangle.top, rectangle.Width(), rectangle.Height()),
                      &format, &brush);
}

// Same text three times with a red/cyan offset: the cheapest convincing
// chromatic aberration, and it costs nothing per pixel.
void DrawSplitText(Graphics& graphics, const std::wstring& text, const Font& font,
                   const RectF& rectangle, const Color& core, float split) {
  if (split > 0.05f) {
    SolidBrush red(Color(150, 255, 40, 60));
    SolidBrush cyan(Color(150, 40, 230, 255));
    RectF left = rectangle;
    left.left -= split;
    left.right -= split;
    RectF right = rectangle;
    right.left += split;
    right.right += split;
    DrawCenteredText(graphics, text, font, left, red);
    DrawCenteredText(graphics, text, font, right, cyan);
  }
  SolidBrush brush(core);
  DrawCenteredText(graphics, text, font, rectangle, brush);
}

Vec2 QuadraticPoint(Vec2 a, Vec2 b, Vec2 c, float t) {
  const float inverse = 1.0f - t;
  return a * (inverse * inverse) + b * (2.0f * inverse * t) + c * (t * t);
}

struct SprayStroke {
  std::vector<Vec2> points;
  float length = 0.0f;
};

// The "67" as hand-drawn spray strokes in a unit box, so it can be revealed one
// centimetre of paint at a time instead of appearing as a finished glyph.
std::vector<SprayStroke> BuildTagStrokes(Vec2 center, float scale) {
  auto unit = [&](float x, float y) { return Vec2{center.x + x * scale, center.y + y * scale}; };
  std::vector<SprayStroke> strokes;

  // "6": a long cane sweeping down-left, then a fat closed belly loop.
  SprayStroke six;
  for (int step = 0; step <= 22; ++step) {
    const float t = static_cast<float>(step) / 22.0f;
    six.points.push_back(QuadraticPoint(unit(-0.24f, -0.95f), unit(-1.02f, -0.62f),
                                        unit(-1.02f, 0.10f), t));
  }
  for (int step = 1; step <= 30; ++step) {
    const float t = static_cast<float>(step) / 30.0f;
    const float angle = kPi + t * 2.0f * kPi;
    six.points.push_back(unit(-0.62f + std::cos(angle) * 0.40f, 0.46f + std::sin(angle) * 0.44f));
  }
  strokes.push_back(std::move(six));

  // "7": top bar, then the long diagonal, then the crossed leg.
  SprayStroke seven;
  seven.points.push_back(unit(0.12f, -0.86f));
  seven.points.push_back(unit(1.08f, -0.92f));
  for (int step = 1; step <= 16; ++step) {
    const float t = static_cast<float>(step) / 16.0f;
    seven.points.push_back(QuadraticPoint(unit(1.08f, -0.92f), unit(0.86f, -0.05f),
                                          unit(0.44f, 0.92f), t));
  }
  strokes.push_back(std::move(seven));

  SprayStroke cross;
  cross.points.push_back(unit(0.30f, 0.06f));
  cross.points.push_back(unit(1.00f, -0.02f));
  strokes.push_back(std::move(cross));

  for (SprayStroke& stroke : strokes) {
    for (std::size_t index = 1; index < stroke.points.size(); ++index) {
      stroke.length += Distance(stroke.points[index - 1], stroke.points[index]);
    }
  }
  return strokes;
}

// Walks the strokes and returns where the nozzle sits at `progress`.
Vec2 StrokeHead(const std::vector<SprayStroke>& strokes, float progress) {
  float total = 0.0f;
  for (const SprayStroke& stroke : strokes) total += stroke.length;
  if (total <= 0.0f || strokes.empty()) return {};
  float remaining = std::clamp(progress, 0.0f, 1.0f) * total;
  for (const SprayStroke& stroke : strokes) {
    if (remaining > stroke.length) {
      remaining -= stroke.length;
      continue;
    }
    for (std::size_t index = 1; index < stroke.points.size(); ++index) {
      const float segment = Distance(stroke.points[index - 1], stroke.points[index]);
      if (remaining <= segment || index + 1 == stroke.points.size()) {
        const float t = segment <= 0.0001f ? 0.0f : std::clamp(remaining / segment, 0.0f, 1.0f);
        return Lerp(stroke.points[index - 1], stroke.points[index], t);
      }
      remaining -= segment;
    }
  }
  return strokes.back().points.back();
}

// The classic Windows arrow, used for the fake cursors that pile up on screen.
void DrawArrowCursor(Graphics& graphics, Vec2 position, float scale, const Color& fill,
                     const Color& outline) {
  constexpr std::array<std::pair<float, float>, 7> shape = {
      {{0.0f, 0.0f}, {0.0f, 16.5f}, {4.1f, 12.6f}, {6.9f, 18.4f}, {9.6f, 17.1f}, {6.7f, 11.5f},
       {11.6f, 11.5f}}};
  std::array<PointF, 7> points{};
  for (std::size_t index = 0; index < shape.size(); ++index) {
    points[index] =
        PointF(position.x + shape[index].first * scale, position.y + shape[index].second * scale);
  }
  SolidBrush brush(fill);
  Pen pen(outline, 1.6f * scale);
  pen.SetLineJoin(LineJoinRound);
  graphics.FillPolygon(&brush, points.data(), static_cast<INT>(points.size()));
  graphics.DrawPolygon(&pen, points.data(), static_cast<INT>(points.size()));
}

}  // namespace

OverlayWindow::ResourceImage::~ResourceImage() {
  image.reset();
  if (stream) stream->Release();
}

OverlayWindow::~OverlayWindow() { Close(); }

bool OverlayWindow::Create(HINSTANCE instance, bool preview, bool primaryMonitorOnly,
                           std::function<void()> tickHandler, std::function<void()> closeHandler,
                           std::wstring& error) {
  instance_ = instance;
  preview_ = preview;
  primaryMonitorOnly_ = primaryMonitorOnly;
  tickHandler_ = std::move(tickHandler);
  closeHandler_ = std::move(closeHandler);

  GdiplusStartupInput input;
  if (GdiplusStartup(&gdiplusToken_, &input, nullptr) != Ok) {
    error = L"Unable to initialize GDI+.";
    return false;
  }

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = &OverlayWindow::WindowProcedure;
  windowClass.hInstance = instance_;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  windowClass.lpszClassName = L"GooseRotOverlay";
  if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    error = L"Unable to register the GooseRot window.";
    return false;
  }

  DWORD style = 0;
  DWORD extendedStyle = 0;
  int x = 0;
  int y = 0;
  int width = 960;
  int height = 540;
  if (preview_) {
    style = WS_OVERLAPPEDWINDOW;
    extendedStyle = WS_EX_APPWINDOW;
    RECT desired{0, 0, width, height};
    AdjustWindowRectEx(&desired, style, FALSE, extendedStyle);
    width = desired.right - desired.left;
    height = desired.bottom - desired.top;
    x = CW_USEDEFAULT;
    y = CW_USEDEFAULT;
  } else {
    style = WS_POPUP;
    extendedStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
    if (primaryMonitorOnly) {
      screenOriginX_ = 0;
      screenOriginY_ = 0;
      width = GetSystemMetrics(SM_CXSCREEN);
      height = GetSystemMetrics(SM_CYSCREEN);
    } else {
      screenOriginX_ = GetSystemMetrics(SM_XVIRTUALSCREEN);
      screenOriginY_ = GetSystemMetrics(SM_YVIRTUALSCREEN);
      width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
      height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }
    x = screenOriginX_;
    y = screenOriginY_;
  }

  window_ = CreateWindowExW(extendedStyle, L"GooseRotOverlay", L"GooseRot — safe desktop demo",
                            style, x, y, width, height, nullptr, nullptr, instance_, this);
  if (!window_) {
    error = L"Unable to create the GooseRot overlay.";
    return false;
  }

  RECT client{};
  GetClientRect(window_, &client);
  if (!RecreateSurface(client.right - client.left, client.bottom - client.top)) {
    error = L"Unable to create the 32-bit rendering surface.";
    return false;
  }
  LoadImages();
  ShowWindow(window_, preview_ ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE);
  UpdateWindow(window_);
  // Request 60 Hz. Dense scenes switch to compact primitives below, allowing
  // the renderer to use a full core instead of collapsing to slideshow speed.
  if (SetTimer(window_, 67, 16, nullptr) == 0) {
    error = L"Unable to start the rendering clock.";
    return false;
  }
  return true;
}

bool OverlayWindow::RecreateSurface(int width, int height) {
  if (width <= 0 || height <= 0) return false;
  constexpr std::uint64_t kMaximumSurfacePixels = 30'000'000ULL;
  const std::uint64_t pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  if (pixels > kMaximumSurfacePixels) return false;

  HDC newDc = CreateCompatibleDC(nullptr);
  if (!newDc) return false;
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* newPixels = nullptr;
  HBITMAP newBitmap = CreateDIBSection(newDc, &info, DIB_RGB_COLORS, &newPixels, nullptr, 0);
  if (!newBitmap || !newPixels) {
    if (newBitmap) DeleteObject(newBitmap);
    DeleteDC(newDc);
    return false;
  }
  HGDIOBJ newOldBitmap = SelectObject(newDc, newBitmap);
  if (!newOldBitmap || newOldBitmap == HGDI_ERROR) {
    DeleteObject(newBitmap);
    DeleteDC(newDc);
    return false;
  }

  if (surfaceDc_) {
    if (oldBitmap_) SelectObject(surfaceDc_, oldBitmap_);
    if (surfaceBitmap_) DeleteObject(surfaceBitmap_);
    DeleteDC(surfaceDc_);
  }
  surfaceDc_ = newDc;
  surfaceBitmap_ = newBitmap;
  oldBitmap_ = newOldBitmap;
  surfacePixels_ = newPixels;
  width_ = width;
  height_ = height;
  return true;
}

bool OverlayWindow::LoadImages() {
  constexpr std::array<int, 7> ids = {IDR_BRAINROT_TRALALERO, IDR_BRAINROT_BALLERINA,
                                      IDR_BRAINROT_BOMBARDIRO, IDR_CAT_SHOCKED,
                                      IDR_CAT_JUDGMENTAL, IDR_CAT_VACANT, IDR_CAT_SUSPICIOUS};
  bool loadedAny = false;
  for (const int id : ids) {
    auto image = LoadResourceImage(id);
    if (image && image->image && image->image->GetLastStatus() == Ok) {
      images_.push_back({id, std::move(image)});
      loadedAny = true;
    }
  }
  return loadedAny;
}

std::unique_ptr<OverlayWindow::ResourceImage> OverlayWindow::LoadResourceImage(int resourceId) {
  HRSRC resource = FindResourceW(instance_, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
  if (!resource) return nullptr;
  const DWORD size = SizeofResource(instance_, resource);
  HGLOBAL loaded = LoadResource(instance_, resource);
  const void* source = LockResource(loaded);
  if (!source || size == 0) return nullptr;

  auto result = std::make_unique<ResourceImage>();
  result->memory = GlobalAlloc(GMEM_MOVEABLE, size);
  if (!result->memory) return nullptr;
  void* destination = GlobalLock(result->memory);
  if (!destination) {
    GlobalFree(result->memory);
    result->memory = nullptr;
    return nullptr;
  }
  std::memcpy(destination, source, size);
  GlobalUnlock(result->memory);
  if (CreateStreamOnHGlobal(result->memory, TRUE, &result->stream) != S_OK) {
    GlobalFree(result->memory);
    result->memory = nullptr;
    return nullptr;
  }
  result->image.reset(Image::FromStream(result->stream, FALSE));
  return result;
}

Image* OverlayWindow::FindImage(int resourceId) const {
  for (const auto& item : images_) {
    if (item.first == resourceId) return item.second->image.get();
  }
  return nullptr;
}

RectF OverlayWindow::CanvasBounds() const {
  return {0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)};
}

Vec2 OverlayWindow::ScreenToCanvas(POINT screenPoint) const {
  if (preview_ && window_) {
    ScreenToClient(window_, &screenPoint);
    const float x = width_ <= 72 ? width_ * 0.5f
                                 : std::clamp(static_cast<float>(screenPoint.x), 36.0f,
                                              static_cast<float>(width_ - 36));
    const float y = height_ <= 72 ? height_ * 0.5f
                                  : std::clamp(static_cast<float>(screenPoint.y), 36.0f,
                                               static_cast<float>(height_ - 36));
    return {x, y};
  }
  const float canvasX = static_cast<float>(screenPoint.x - screenOriginX_);
  const float canvasY = static_cast<float>(screenPoint.y - screenOriginY_);
  const float x = width_ <= 72 ? width_ * 0.5f
                               : std::clamp(canvasX, 36.0f, static_cast<float>(width_ - 36));
  const float y = height_ <= 72 ? height_ * 0.5f
                                : std::clamp(canvasY, 36.0f, static_cast<float>(height_ - 36));
  return {x, y};
}

Vec2 OverlayWindow::GraffitiPaintHead(float progress) const {
  const float scale = std::max(70.0f, std::min(height_ * 0.30f, width_ * 0.20f));
  const Vec2 center{width_ * 0.5f, height_ * 0.47f};
  return StrokeHead(BuildTagStrokes(center, scale), progress);
}

void OverlayWindow::Render(const RenderState& state) {
  if (!surfacePixels_ || width_ <= 0 || height_ <= 0) return;
  ++frame_;
  ++fpsSampleFrames_;
  const ULONGLONG now = GetTickCount64();
  if (fpsSampleStartedAt_ == 0) fpsSampleStartedAt_ = now;
  if (preview_ && now - fpsSampleStartedAt_ >= 1000) {
    const double fps = static_cast<double>(fpsSampleFrames_) * 1000.0 /
                       static_cast<double>(now - fpsSampleStartedAt_);
    wchar_t title[96]{};
    swprintf(title, std::size(title), L"GooseRot Preview - %.1f FPS", fps);
    SetWindowTextW(window_, title);
    fpsSampleStartedAt_ = now;
    fpsSampleFrames_ = 0;
  }
  std::memset(surfacePixels_, 0, static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4U);
  Bitmap surface(width_, height_, width_ * 4, PixelFormat32bppPARGB,
                 static_cast<BYTE*>(surfacePixels_));
  Graphics graphics(&surface);
  const bool heavyScene = (state.geese && state.geese->size() > 12U) || state.popupCount > 24;
  graphics.SetSmoothingMode(heavyScene ? SmoothingModeHighSpeed : SmoothingModeAntiAlias);
  graphics.SetInterpolationMode(heavyScene ? InterpolationModeLowQuality
                                           : InterpolationModeHighQualityBicubic);
  graphics.SetCompositingQuality(heavyScene ? CompositingQualityHighSpeed
                                            : CompositingQualityDefault);
  graphics.SetPixelOffsetMode(heavyScene ? PixelOffsetModeHighSpeed : PixelOffsetModeDefault);
  graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

  if (preview_) DrawPreviewDesktop(graphics);
  if (state.fakeShutdown) {
    DrawFakeShutdown(graphics, state);
  } else {
    const GraphicsState saved = graphics.Save();
    // Shake: a steady beat from 3:30, plus an extra kick the more the display
    // is falling apart.
    if (state.logicalTime >= 210.0) {
      const int pulse = static_cast<int>((state.logicalTime - 210.0) / 2.0);
      if (std::fmod(state.logicalTime - 210.0, 2.0) < 0.18) {
        graphics.TranslateTransform((pulse % 2 == 0) ? 4.0f : -4.0f,
                                    (pulse % 3 == 0) ? -3.0f : 3.0f);
      }
    }
    if (state.glitch > 0.35f) {
      const float kick = (state.glitch - 0.35f) * 11.0f;
      graphics.TranslateTransform(NoiseSigned(frame_ * 13U) * kick,
                                  NoiseSigned(frame_ * 13U + 5U) * kick * 0.6f);
    }
    DrawSceneEffects(graphics, state);
    DrawSprites(graphics, state);
    DrawClipboardBadge(graphics, state);
    if (state.geese) {
      for (std::size_t index = 0; index < state.geese->size(); ++index) {
        if (heavyScene && index >= 3U) {
          DrawGooseCompact(graphics, (*state.geese)[index], static_cast<int>(index));
        } else {
          DrawGoose(graphics, (*state.geese)[index], static_cast<int>(index));
        }
      }
    }
    DrawCursorLatch(graphics, state);
    if (!state.bubbleText.empty()) DrawSpeechBubble(graphics, state.bubbleText, state.bubbleAnchor);
    DrawHud(graphics, state);
    DrawToasts(graphics, state);
    DrawGlitch(graphics, state);
    graphics.Restore(saved);
  }
  Present();
}

void OverlayWindow::DrawPreviewDesktop(Graphics& graphics) const {
  LinearGradientBrush background(Point(0, 0), Point(width_, height_),
                                  Color(255, 18, 77, 126), Color(255, 54, 155, 161));
  graphics.FillRectangle(&background, 0, 0, width_, height_);
  SolidBrush hill(Color(150, 32, 100, 86));
  graphics.FillEllipse(&hill, -120, height_ / 2, width_ * 3 / 4, height_ / 2);
  graphics.FillEllipse(&hill, width_ / 3, height_ / 2 - 40, width_, height_ / 2 + 80);
  SolidBrush taskbar(Color(235, 24, 29, 38));
  graphics.FillRectangle(&taskbar, 0, height_ - 42, width_, 42);
  SolidBrush start(Color(255, 58, 170, 225));
  graphics.FillEllipse(&start, 10, height_ - 34, 26, 26);

  SolidBrush windowBrush(Color(235, 245, 247, 250));
  SolidBrush titleBrush(Color(245, 37, 92, 145));
  graphics.FillRectangle(&windowBrush, 54, 62, 350, 220);
  graphics.FillRectangle(&titleBrush, 54, 62, 350, 30);
  Font font(UiFamily(), 12.0f, FontStyleRegular, UnitPixel);
  SolidBrush titleText(Color::White);
  graphics.DrawString(L"Preview desktop — no system effects", -1, &font, PointF(65.0f, 69.0f), &titleText);
}

void OverlayWindow::DrawGoose(Graphics& graphics, const GooseEntity& goose, int index) const {
  const Vec2 position = goose.Position();
  const GooseRig& rig = goose.Rig();
  const float angle = goose.DirectionRadians() * 180.0f / kPi;
  const Vec2 forward{std::cos(goose.DirectionRadians()), std::sin(goose.DirectionRadians())};
  const Vec2 side{-forward.y, forward.x};

  const Color bodyTint = index == 0   ? kGooseWhite
                         : index == 1 ? Color(255, 255, 241, 250)
                                      : Color(255, 240, 255, 240);
  const Color wingTint = index == 0   ? Color(255, 243, 239, 227)
                         : index == 1 ? Color(255, 255, 220, 243)
                                      : Color(255, 220, 247, 215);

  SolidBrush inkBrush(kInk);
  SolidBrush bodyBrush(bodyTint);
  SolidBrush orangeBrush(kBeakOrange);
  Pen inkPen(kInk, 3.2f);
  inkPen.SetLineJoin(LineJoinRound);

  // Ground shadow, always screen-aligned so the flock reads as standing on the
  // desktop rather than floating over it.
  SolidBrush shadow(Color(52, 0, 0, 0));
  graphics.FillEllipse(&shadow, position.x - 32.0f, position.y + 15.0f, 64.0f, 18.0f);

  // Legs and webbed feet, under the body.
  const Vec2 hipLeft = position - forward * 8.0f - side * 12.0f;
  const Vec2 hipRight = position - forward * 8.0f + side * 12.0f;
  Pen legDark(kBeakOrangeDark, 6.0f);
  legDark.SetStartCap(LineCapRound);
  legDark.SetEndCap(LineCapRound);
  Pen legLight(kBeakOrange, 3.4f);
  legLight.SetStartCap(LineCapRound);
  legLight.SetEndCap(LineCapRound);
  for (const auto& leg : {std::pair<Vec2, Vec2>{hipLeft, rig.leftFoot},
                          std::pair<Vec2, Vec2>{hipRight, rig.rightFoot}}) {
    graphics.DrawLine(&legDark, leg.first.x, leg.first.y, leg.second.x, leg.second.y);
    graphics.DrawLine(&legLight, leg.first.x, leg.first.y, leg.second.x, leg.second.y);
  }
  Pen footPen(kBeakOrangeDark, 1.8f);
  footPen.SetLineJoin(LineJoinRound);
  Pen toePen(Color(190, 150, 80, 8), 1.1f);
  toePen.SetStartCap(LineCapRound);
  toePen.SetEndCap(LineCapRound);
  for (int foot = 0; foot < 2; ++foot) {
    const Vec2 anchor = foot == 0 ? rig.leftFoot : rig.rightFoot;
    const float sign = foot == 0 ? -1.0f : 1.0f;
    std::array<PointF, 4> shape{};
    shape[0] = ToPointF(anchor);
    const std::array<float, 3> angles = {0.30f, 0.95f, 1.60f};
    const std::array<float, 3> lengths = {10.5f, 11.5f, 10.5f};
    for (std::size_t toe = 0; toe < angles.size(); ++toe) {
      const float toeAngle = sign * angles[toe];
      const Vec2 tip = anchor - forward * (std::cos(toeAngle) * lengths[toe]) +
                       side * (std::sin(toeAngle) * lengths[toe]);
      shape[toe + 1] = ToPointF(tip);
    }
    graphics.FillPolygon(&orangeBrush, shape.data(), static_cast<INT>(shape.size()));
    graphics.DrawPolygon(&footPen, shape.data(), static_cast<INT>(shape.size()));
    for (std::size_t toe = 1; toe < shape.size(); ++toe) {
      graphics.DrawLine(&toePen, shape[0], shape[toe]);
    }
  }

  // Neck: a tapered ribbon swept along a quadratic spine, drawn before the body
  // so the shoulder swallows its root.
  {
    const Vec2 start = rig.neckBase - forward * 9.0f;
    constexpr int kSamples = 14;
    std::array<Vec2, kSamples + 1> spine{};
    for (int step = 0; step <= kSamples; ++step) {
      spine[step] = QuadraticPoint(start, rig.neckCenter, rig.headCenter,
                                   static_cast<float>(step) / kSamples);
    }
    std::vector<PointF> ribbon;
    ribbon.reserve((kSamples + 1) * 2);
    std::vector<PointF> back;
    back.reserve(kSamples + 1);
    for (int step = 0; step <= kSamples; ++step) {
      const float t = static_cast<float>(step) / kSamples;
      const float halfWidth = 10.5f - 4.5f * t * t;
      const Vec2 previous = spine[std::max(0, step - 1)];
      const Vec2 next = spine[std::min(kSamples, step + 1)];
      const Vec2 normal = Normalize(Vec2{-(next.y - previous.y), next.x - previous.x});
      ribbon.push_back(ToPointF(spine[step] + normal * halfWidth));
      back.push_back(ToPointF(spine[step] - normal * halfWidth));
    }
    for (auto reverse = back.rbegin(); reverse != back.rend(); ++reverse) ribbon.push_back(*reverse);
    Pen neckPen(kInk, 2.8f);
    neckPen.SetLineJoin(LineJoinRound);
    graphics.FillPolygon(&bodyBrush, ribbon.data(), static_cast<INT>(ribbon.size()));
    graphics.DrawPolygon(&neckPen, ribbon.data(), static_cast<INT>(ribbon.size()));
  }

  // Body block: tail fan, torso, then a folded wing on each flank.
  {
    const GraphicsState bodyState = graphics.Save();
    graphics.TranslateTransform(position.x, position.y);
    graphics.RotateTransform(angle);

    GraphicsPath tail;
    tail.AddBezier(-22.0f, -17.0f, -32.0f, -19.0f, -46.0f, -15.0f, -50.0f, -9.0f);
    tail.AddBezier(-50.0f, -9.0f, -44.0f, -6.5f, -44.0f, -2.0f, -47.0f, 0.0f);
    tail.AddBezier(-47.0f, 0.0f, -44.0f, 2.0f, -44.0f, 6.5f, -50.0f, 9.0f);
    tail.AddBezier(-50.0f, 9.0f, -46.0f, 15.0f, -32.0f, 19.0f, -22.0f, 17.0f);
    tail.CloseFigure();
    Pen tailPen(kInk, 2.6f);
    tailPen.SetLineJoin(LineJoinRound);
    graphics.FillPath(&bodyBrush, &tail);
    graphics.DrawPath(&tailPen, &tail);
    Pen featherPen(Color(90, 28, 25, 34), 1.5f);
    featherPen.SetStartCap(LineCapRound);
    featherPen.SetEndCap(LineCapRound);
    graphics.DrawLine(&featherPen, -26.0f, -9.0f, -43.0f, -8.0f);
    graphics.DrawLine(&featherPen, -26.0f, 9.0f, -43.0f, 8.0f);

    GraphicsPath body;
    body.AddBezier(30.0f, 0.0f, 29.0f, -12.0f, 16.0f, -21.0f, -4.0f, -21.0f);
    body.AddBezier(-4.0f, -21.0f, -22.0f, -21.0f, -31.0f, -12.0f, -31.0f, 0.0f);
    body.AddBezier(-31.0f, 0.0f, -31.0f, 12.0f, -22.0f, 21.0f, -4.0f, 21.0f);
    body.AddBezier(-4.0f, 21.0f, 16.0f, 21.0f, 29.0f, 12.0f, 30.0f, 0.0f);
    body.CloseFigure();
    graphics.FillPath(&bodyBrush, &body);
    graphics.DrawPath(&inkPen, &body);
    const GraphicsState clipState = graphics.Save();
    graphics.SetClip(&body, CombineModeReplace);
    SolidBrush rumpShade(Color(22, 28, 25, 34));
    graphics.FillEllipse(&rumpShade, -42.0f, -24.0f, 32.0f, 48.0f);
    graphics.Restore(clipState);

    SolidBrush wingBrush(wingTint);
    Pen wingPen(kInk, 2.4f);
    wingPen.SetLineJoin(LineJoinRound);
    Pen wingDetail(Color(115, 28, 25, 34), 1.6f);
    wingDetail.SetStartCap(LineCapRound);
    wingDetail.SetEndCap(LineCapRound);
    for (int wing = 0; wing < 2; ++wing) {
      const float sign = wing == 0 ? -1.0f : 1.0f;
      const GraphicsState wingState = graphics.Save();
      graphics.TranslateTransform(-3.0f, sign * 12.0f);
      graphics.RotateTransform(sign * goose.WingFlap() * 17.0f);
      GraphicsPath shape;
      shape.AddBezier(17.0f, sign * -4.0f, 6.0f, sign * -9.0f, -13.0f, sign * -8.0f, -22.0f, sign * 1.0f);
      shape.AddBezier(-22.0f, sign * 1.0f, -16.0f, sign * 9.0f, -3.0f, sign * 11.0f, 11.0f, sign * 6.0f);
      shape.CloseFigure();
      graphics.FillPath(&wingBrush, &shape);
      graphics.DrawPath(&wingPen, &shape);
      GraphicsPath quills;
      quills.AddBezier(10.0f, sign * -1.5f, 2.0f, sign * -4.0f, -8.0f, sign * -4.0f, -16.0f, sign * 0.5f);
      quills.StartFigure();
      quills.AddBezier(7.0f, sign * 2.5f, 0.0f, sign * 0.0f, -8.0f, sign * 1.0f, -15.0f, sign * 4.0f);
      graphics.DrawPath(&wingDetail, &quills);
      graphics.Restore(wingState);
    }
    graphics.Restore(bodyState);
  }

  // Head and beak.
  {
    const GraphicsState headState = graphics.Save();
    graphics.TranslateTransform(rig.headCenter.x, rig.headCenter.y);
    graphics.RotateTransform(angle);
    Pen headPen(kInk, 2.9f);
    graphics.FillEllipse(&bodyBrush, -12.0f, -12.0f, 27.0f, 24.0f);
    graphics.DrawEllipse(&headPen, -12.0f, -12.0f, 27.0f, 24.0f);

    const float open = goose.BeakOpen();
    Pen beakPen(kBeakOrangeDark, 2.3f);
    beakPen.SetLineJoin(LineJoinRound);
    SolidBrush lowerBrush(Color(255, 226, 134, 15));
    const GraphicsState upperState = graphics.Save();
    graphics.RotateTransform(-open * 19.0f);
    std::array<PointF, 4> upper = {PointF(6.0f, -8.0f), PointF(25.0f, -3.5f), PointF(25.0f, 0.5f),
                                   PointF(6.0f, 1.5f)};
    graphics.FillPolygon(&orangeBrush, upper.data(), static_cast<INT>(upper.size()));
    graphics.DrawPolygon(&beakPen, upper.data(), static_cast<INT>(upper.size()));
    graphics.Restore(upperState);
    const GraphicsState lowerState = graphics.Save();
    graphics.RotateTransform(open * 26.0f);
    std::array<PointF, 4> lower = {PointF(6.0f, 8.0f), PointF(23.0f, 3.0f), PointF(23.0f, 0.5f),
                                   PointF(6.0f, -0.5f)};
    graphics.FillPolygon(&lowerBrush, lower.data(), static_cast<INT>(lower.size()));
    graphics.DrawPolygon(&beakPen, lower.data(), static_cast<INT>(lower.size()));
    graphics.Restore(lowerState);
    SolidBrush nostril(Color(205, 105, 50, 0));
    graphics.FillEllipse(&nostril, 10.4f, -5.1f, 3.2f, 3.2f);
    graphics.Restore(headState);
  }

  // Eyes: sclera, pupil looking forward, catchlight, and a brow when annoyed.
  {
    SolidBrush sclera(kGooseWhite);
    SolidBrush pupil(kInk);
    SolidBrush glint(Color(255, 255, 255, 255));
    Pen rim(kInk, 1.7f);
    for (const Vec2 eye : {rig.leftEye, rig.rightEye}) {
      FillCircle(graphics, sclera, eye, 4.3f);
      StrokeCircle(graphics, rim, eye, 4.3f);
      const Vec2 look = eye + forward * 1.5f;
      FillCircle(graphics, pupil, look, 2.4f);
      FillCircle(graphics, glint, look + Vec2{-0.9f, -1.1f}, 0.9f);
    }
    if (goose.IsAngry()) {
      Pen brow(kInk, 2.8f);
      brow.SetStartCap(LineCapRound);
      brow.SetEndCap(LineCapRound);
      for (int eye = 0; eye < 2; ++eye) {
        const Vec2 anchor = eye == 0 ? rig.leftEye : rig.rightEye;
        const float sign = eye == 0 ? -1.0f : 1.0f;
        const Vec2 outer = anchor - forward * 2.0f - side * (sign * 6.5f);
        const Vec2 inner = anchor + forward * 6.5f - side * (sign * 2.5f);
        graphics.DrawLine(&brow, outer.x, outer.y, inner.x, inner.y);
      }
    }
  }

  if (goose.IsHonking()) {
    Pen honkPen(Color(210, 255, 255, 255), 3.0f);
    honkPen.SetStartCap(LineCapRound);
    honkPen.SetEndCap(LineCapRound);
    for (int ring = 0; ring < 3; ++ring) {
      const float radius = 26.0f + ring * 12.0f;
      graphics.DrawArc(&honkPen, rig.beakTip.x - radius, rig.beakTip.y - radius, radius * 2.0f,
                       radius * 2.0f, angle - 29.0f, 58.0f);
    }
  }

  if (index > 0) {
    SolidBrush badge(index == 1 ? kNeonPink : kMatrixGreen);
    Pen badgeRim(kInk, 1.6f);
    const Vec2 spot = position - side * 19.0f - forward * 8.0f;
    FillCircle(graphics, badge, spot, 5.0f);
    StrokeCircle(graphics, badgeRim, spot, 5.0f);
  }
}

void OverlayWindow::DrawGooseCompact(Graphics& graphics, const GooseEntity& goose, int index) const {
  const Vec2 position = goose.Position();
  const GooseRig& rig = goose.Rig();
  const Vec2 forward{std::cos(goose.DirectionRadians()), std::sin(goose.DirectionRadians())};
  const Vec2 side{-forward.y, forward.x};
  const Color bodyColor = index % 3 == 0 ? kGooseWhite
                          : index % 3 == 1 ? Color(255, 255, 235, 248)
                                           : Color(255, 232, 255, 226);
  SolidBrush shadow(Color(46, 0, 0, 0));
  SolidBrush body(bodyColor);
  SolidBrush wing(Color(255, 222, 218, 226));
  SolidBrush beak(kBeakOrange);
  SolidBrush ink(kInk);
  Pen outline(kInk, 2.0f);
  Pen neck(bodyColor, 13.0f);
  neck.SetStartCap(LineCapRound);
  neck.SetEndCap(LineCapRound);

  graphics.FillEllipse(&shadow, position.x - 30.0f, position.y + 12.0f, 60.0f, 15.0f);
  graphics.DrawLine(&neck, position.x + forward.x * 14.0f, position.y + forward.y * 14.0f,
                    rig.headCenter.x, rig.headCenter.y);
  graphics.FillEllipse(&body, position.x - 29.0f, position.y - 19.0f, 58.0f, 38.0f);
  graphics.DrawEllipse(&outline, position.x - 29.0f, position.y - 19.0f, 58.0f, 38.0f);
  const Vec2 wingCenter = position - forward * 5.0f + side * 4.0f;
  graphics.FillEllipse(&wing, wingCenter.x - 17.0f, wingCenter.y - 10.0f, 34.0f, 20.0f);
  graphics.FillEllipse(&body, rig.headCenter.x - 11.0f, rig.headCenter.y - 11.0f, 22.0f, 22.0f);
  graphics.DrawEllipse(&outline, rig.headCenter.x - 11.0f, rig.headCenter.y - 11.0f, 22.0f, 22.0f);
  std::array<PointF, 3> bill = {
      ToPointF(rig.headCenter + forward * 7.0f - side * 6.0f),
      ToPointF(rig.beakTip),
      ToPointF(rig.headCenter + forward * 7.0f + side * 6.0f)};
  graphics.FillPolygon(&beak, bill.data(), static_cast<INT>(bill.size()));
  graphics.DrawPolygon(&outline, bill.data(), static_cast<INT>(bill.size()));
  FillCircle(graphics, ink, rig.leftEye, 2.2f);
  FillCircle(graphics, ink, rig.rightEye, 2.2f);
}

void OverlayWindow::DrawSpeechBubble(Graphics& graphics, const std::wstring& text, Vec2 anchor) const {
  int lines = 1;
  for (const wchar_t character : text) if (character == L'\n') ++lines;
  const float width = std::clamp(190.0f + static_cast<float>(text.size()) * 3.2f, 230.0f, 470.0f);
  const float height = 34.0f + lines * 24.0f + (text.size() > 55 ? 22.0f : 0.0f);
  float x = anchor.x - width * 0.5f;
  float y = anchor.y - height - 86.0f;
  x = std::clamp(x, 12.0f, std::max(12.0f, static_cast<float>(width_) - width - 12.0f));
  y = std::clamp(y, 12.0f, std::max(12.0f, static_cast<float>(height_) - height - 12.0f));

  // Marker-drawn balloon: a lumpy outline that boils, not a rounded rectangle.
  const std::uint32_t seed = 4211U + frame_ / 5U % 3U;
  GraphicsPath path;
  constexpr int kSteps = 26;
  std::vector<PointF> outline;
  outline.reserve(kSteps);
  for (int step = 0; step < kSteps; ++step) {
    const float angle = static_cast<float>(step) / kSteps * 2.0f * kPi;
    const float wobble = 1.0f + NoiseSigned(seed * 31U + static_cast<std::uint32_t>(step)) * 0.045f;
    outline.push_back(PointF(x + width * 0.5f + std::cos(angle) * width * 0.53f * wobble,
                             y + height * 0.5f + std::sin(angle) * height * 0.62f * wobble));
  }
  path.AddClosedCurve(outline.data(), static_cast<INT>(outline.size()), 0.35f);

  const float tailBase = std::clamp(anchor.x, x + 26.0f, x + width - 26.0f);
  std::array<PointF, 3> tail = {PointF(tailBase - 16.0f, y + height * 0.86f),
                                PointF(anchor.x, std::min(anchor.y - 40.0f, y + height + 46.0f)),
                                PointF(tailBase + 12.0f, y + height * 0.9f)};

  SolidBrush shadow(Color(70, 0, 0, 0));
  const GraphicsState shadowState = graphics.Save();
  graphics.TranslateTransform(4.0f, 5.0f);
  graphics.FillPath(&shadow, &path);
  graphics.Restore(shadowState);

  SolidBrush fill(kBubbleWhite);
  Pen outlinePen(kInk, 3.4f);
  outlinePen.SetLineJoin(LineJoinRound);
  graphics.FillPolygon(&fill, tail.data(), static_cast<INT>(tail.size()));
  graphics.FillPath(&fill, &path);
  graphics.DrawPath(&outlinePen, &path);
  graphics.DrawLines(&outlinePen, tail.data(), static_cast<INT>(tail.size()));

  Font font(UiFamily(), 19.0f, FontStyleBold, UnitPixel);
  SolidBrush ink(Color(255, 28, 28, 34));
  RectF textRectangle{x + 22.0f, y + 10.0f, x + width - 22.0f, y + height - 10.0f};
  DrawCenteredText(graphics, text, font, textRectangle, ink);
}

void OverlayWindow::DrawSprites(Graphics& graphics, const RenderState& state) const {
  if (!state.sprites) return;
  for (const VisualSprite& sprite : *state.sprites) {
    Image* image = FindImage(sprite.resourceId);
    if (!image) continue;
    const double age = state.logicalTime - sprite.createdAt;
    if (age < 0.0 || age > sprite.lifetime) continue;
    const float fadeIn = static_cast<float>(std::clamp(age / 0.6, 0.0, 1.0));
    const float fadeOut = static_cast<float>(std::clamp((sprite.lifetime - age) / 1.0, 0.0, 1.0));
    const float alpha = std::min(fadeIn, fadeOut);
    // Stickers slap down with a short overshoot instead of fading in politely.
    const float pop = age < 0.35 ? 1.0f + static_cast<float>(0.35 - age) * 0.75f : 1.0f;
    const GraphicsState saved = graphics.Save();
    graphics.TranslateTransform(sprite.center.x, sprite.center.y);
    graphics.RotateTransform(sprite.angleDegrees);
    const float carriedScale = sprite.carried ? 0.68f : 1.0f;
    const float size = sprite.size * pop * carriedScale;
    const Rect destination(static_cast<INT>(-size * 0.5f), static_cast<INT>(-size * 0.5f),
                           static_cast<INT>(size), static_cast<INT>(size));
    if (alpha >= 0.995f) {
      graphics.DrawImage(image, destination);
    } else {
      ColorMatrix matrix = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0},
                             {0, 0, 0, alpha, 0}, {0, 0, 0, 0, 1}}};
      ImageAttributes attributes;
      attributes.SetColorMatrix(&matrix);
      graphics.DrawImage(image, destination, 0, 0, image->GetWidth(), image->GetHeight(),
                         UnitPixel, &attributes);
    }
    graphics.Restore(saved);
  }
}

void OverlayWindow::DrawSceneEffects(Graphics& graphics, const RenderState& state) const {
  if (state.colorFilter) {
    const bool matrix = static_cast<int>((state.logicalTime - 240.0) / 2.0) % 2 == 0;
    SolidBrush filter(matrix ? Color(38, 57, 255, 20) : Color(38, 255, 45, 170));
    graphics.FillRectangle(&filter, 0, 0, width_, height_);
  }

  if (state.graffiti) DrawGraffiti(graphics, state);
}

void OverlayWindow::DrawClipboardBadge(Graphics& graphics, const RenderState& state) const {
  if (!state.clipboardBadge) return;
  const float age = static_cast<float>(state.logicalTime - 120.0);
  const float travel = std::clamp(age / 5.0f, 0.0f, 1.0f);
  // Settle in a protected lower-left lane, away from the aura counter and the
  // toast stack. Drawing after sprites keeps the certification legible.
  const float x = (1.0f - travel) * width_ * 0.5f + travel * 220.0f;
  const float y = (1.0f - travel) * height_ * 0.5f + travel * (height_ - 148.0f);
  const GraphicsState saved = graphics.Save();
  graphics.TranslateTransform(x, y);
  graphics.RotateTransform(-5.0f + std::sin(static_cast<float>(state.logicalTime) * 2.4f) * 1.5f);
  GraphicsPath badge;
  AddWobblyRectangle(badge, -196.0f, -46.0f, 392.0f, 92.0f, 3.0f,
                      907U + frame_ / 6U % 3U);
  SolidBrush fill(Color(244, 255, 251, 234));
  Pen edge(kMatrixGreen, 6.0f);
  edge.SetLineJoin(LineJoinRound);
  graphics.FillPath(&fill, &badge);
  graphics.DrawPath(&edge, &badge);
  Font font(PosterFamily(), 26.0f, FontStyleBold, UnitPixel);
  SolidBrush ink(Color(255, 18, 80, 10));
  DrawCenteredText(graphics, L"CLIPBOARD CERTIFIED\n+10,000 AURA", font,
                   {-186.0f, -40.0f, 186.0f, 40.0f}, ink);
  graphics.Restore(saved);
}

void OverlayWindow::DrawGraffiti(Graphics& graphics, const RenderState& state) const {
  // The tag is sprayed live: strokes are revealed by arc length, paint sags into
  // drips behind the nozzle and overspray keeps accumulating.
  const float progress = std::clamp(state.graffitiProgress, 0.0f, 1.0f);
  if (progress <= 0.0f) return;
  const float scale = std::max(70.0f, std::min(height_ * 0.30f, width_ * 0.20f));
  const Vec2 center{width_ * 0.5f, height_ * 0.47f};
  const std::vector<SprayStroke> strokes = BuildTagStrokes(center, scale);

  float total = 0.0f;
  for (const SprayStroke& stroke : strokes) total += stroke.length;
  if (total <= 0.0f) return;
  float painted = progress * total;

  const float band = scale * 0.30f;
  Pen halo(Color(70, 255, 45, 170), band * 1.85f);
  halo.SetStartCap(LineCapRound);
  halo.SetEndCap(LineCapRound);
  halo.SetLineJoin(LineJoinRound);
  // The dark pass is laid down first and slightly wider, so the pink core reads
  // as paint with an edge rather than a line with a stripe through it.
  Pen edge(Color(255, 55, 5, 40), band * 1.16f);
  edge.SetStartCap(LineCapRound);
  edge.SetEndCap(LineCapRound);
  edge.SetLineJoin(LineJoinRound);
  Pen core(kNeonPink, band * 0.92f);
  core.SetStartCap(LineCapRound);
  core.SetEndCap(LineCapRound);
  core.SetLineJoin(LineJoinRound);

  SolidBrush oversprayBrush(Color(90, 255, 45, 170));
  SolidBrush dripBrush(kNeonPink);
  std::uint32_t overspraySeed = 17U;

  for (const SprayStroke& stroke : strokes) {
    if (painted <= 0.0f) break;
    std::vector<PointF> visible;
    visible.reserve(stroke.points.size());
    visible.push_back(ToPointF(stroke.points.front()));
    float used = 0.0f;
    for (std::size_t index = 1; index < stroke.points.size() && used < painted; ++index) {
      const float segment = Distance(stroke.points[index - 1], stroke.points[index]);
      if (used + segment <= painted) {
        visible.push_back(ToPointF(stroke.points[index]));
        used += segment;
      } else {
        const float t = segment <= 0.0001f ? 0.0f : (painted - used) / segment;
        const Vec2 partial = Lerp(stroke.points[index - 1], stroke.points[index], t);
        visible.push_back(ToPointF(partial));
        used = painted;
      }
    }
    if (visible.size() >= 2) {
      graphics.DrawLines(&halo, visible.data(), static_cast<INT>(visible.size()));
      graphics.DrawLines(&edge, visible.data(), static_cast<INT>(visible.size()));
      graphics.DrawLines(&core, visible.data(), static_cast<INT>(visible.size()));

      // Overspray speckle around the painted line.
      for (std::size_t index = 1; index < visible.size(); index += 2) {
        for (int dot = 0; dot < 3; ++dot) {
          const std::uint32_t seed = overspraySeed++ * 41U + static_cast<std::uint32_t>(dot);
          const float radius = band * (0.55f + Noise01(seed) * 0.75f);
          const float angle = Noise01(seed + 3U) * 2.0f * kPi;
          const float size = 1.4f + Noise01(seed + 9U) * 3.6f;
          graphics.FillEllipse(&oversprayBrush, visible[index].X + std::cos(angle) * radius,
                               visible[index].Y + std::sin(angle) * radius, size, size);
        }
      }

      // Drips: paint that kept running after the nozzle moved on.
      for (std::size_t index = 2; index + 1 < visible.size(); index += 5) {
        const std::uint32_t seed = static_cast<std::uint32_t>(index) * 733U + overspraySeed;
        if (Noise01(seed) < 0.45f) continue;
        const float travelled = painted - used * static_cast<float>(index) / visible.size();
        const float run = std::clamp(travelled * 0.16f, 0.0f, band * (1.6f + Noise01(seed + 1U) * 3.2f));
        if (run <= 2.0f) continue;
        const float thickness = band * (0.16f + Noise01(seed + 2U) * 0.16f);
        graphics.FillRectangle(&dripBrush, visible[index].X - thickness * 0.5f, visible[index].Y,
                               thickness, run);
        graphics.FillEllipse(&dripBrush, visible[index].X - thickness * 0.8f,
                             visible[index].Y + run - thickness * 0.8f, thickness * 1.6f,
                             thickness * 1.6f);
      }
    }
    painted -= stroke.length;
  }

  if (progress < 1.0f) {
    // The can itself: a bright burst of atomised paint at the nozzle.
    const Vec2 head = StrokeHead(strokes, progress);
    SolidBrush mist(Color(120, 255, 255, 255));
    for (int dot = 0; dot < 16; ++dot) {
      const std::uint32_t seed = frame_ * 97U + static_cast<std::uint32_t>(dot);
      const float radius = band * (0.4f + Noise01(seed) * 1.5f);
      const float angle = Noise01(seed + 5U) * 2.0f * kPi;
      const float size = 1.5f + Noise01(seed + 11U) * 4.0f;
      graphics.FillEllipse(&mist, head.x + std::cos(angle) * radius,
                           head.y + std::sin(angle) * radius, size, size);
    }
    SolidBrush flash(Color(150, 255, 45, 170));
    graphics.FillEllipse(&flash, head.x - band * 0.5f, head.y - band * 0.5f, band, band);
  }
}

void OverlayWindow::DrawCursorLatch(Graphics& graphics, const RenderState& state) const {
  if (!state.cursorLatched || !state.geese || state.geese->empty()) return;
  const Vec2 beak = state.geese->front().Rig().beakTip;
  const Vec2 cursor = ScreenToCanvas(state.cursor);

  // A visible clamp between beak and pointer: the drag is happening, and it is
  // obvious who is doing it.
  Pen grip(Color(220, 255, 45, 170), 5.0f);
  grip.SetStartCap(LineCapRound);
  grip.SetEndCap(LineCapRound);
  graphics.DrawLine(&grip, beak.x, beak.y, cursor.x, cursor.y);
  Pen ring(Color(230, 255, 45, 170), 3.5f);
  const float pulse = 12.0f + std::sin(static_cast<float>(state.logicalTime) * 22.0f) * 3.0f;
  StrokeCircle(graphics, ring, cursor, pulse);

  Font font(PosterFamily(), 22.0f, FontStyleBold, UnitPixel);
  DrawSplitText(graphics, L"GRABBED", font,
                {cursor.x - 90.0f, cursor.y + 27.0f, cursor.x + 90.0f, cursor.y + 57.0f},
                kNeonPink, 2.0f);
}

void OverlayWindow::DrawToasts(Graphics& graphics, const RenderState& state) const {
  if (!state.toasts) return;
  int slot = 0;
  for (const ToastNotice& toast : *state.toasts) {
    const double age = state.logicalTime - toast.createdAt;
    if (age < 0.0 || age > toast.lifetime) continue;
    const float slide = static_cast<float>(std::clamp(age / 0.35, 0.0, 1.0));
    const float fade = static_cast<float>(std::clamp((toast.lifetime - age) / 0.6, 0.0, 1.0));
    const float width = 372.0f;
    const float height = 104.0f;
    const float x = width_ - width - 22.0f + (1.0f - slide) * (width + 40.0f);
    const float y = height_ - 70.0f - (slot + 1) * (height + 12.0f);
    if (y < 10.0f) break;
    ++slot;

    const GraphicsState saved = graphics.Save();
    graphics.TranslateTransform(x, y);
    graphics.RotateTransform(NoiseSigned(static_cast<std::uint32_t>(toast.createdAt * 13.0) + 3U) * 1.6f);
    GraphicsPath frame;
    AddWobblyRectangle(frame, 0.0f, 0.0f, width, height, 2.4f,
                       static_cast<std::uint32_t>(toast.createdAt * 7.0) + frame_ / 7U % 3U);
    SolidBrush fill(WithAlpha(Color(255, 24, 24, 31), fade * 0.94f));
    Pen edge(WithAlpha(kMatrixGreen, fade), 2.6f);
    edge.SetLineJoin(LineJoinRound);
    graphics.FillPath(&fill, &frame);
    graphics.DrawPath(&edge, &frame);

    SolidBrush shieldBrush(WithAlpha(kMatrixGreen, fade));
    std::array<PointF, 5> shield = {PointF(30.0f, 20.0f), PointF(52.0f, 28.0f), PointF(48.0f, 62.0f),
                                    PointF(30.0f, 74.0f), PointF(14.0f, 58.0f)};
    graphics.FillPolygon(&shieldBrush, shield.data(), static_cast<INT>(shield.size()));

    Font title(UiFamily(), 17.0f, FontStyleBold, UnitPixel);
    Font body(UiFamily(), 15.0f, FontStyleRegular, UnitPixel);
    SolidBrush titleInk(WithAlpha(Color(255, 255, 255, 255), fade));
    SolidBrush bodyInk(WithAlpha(Color(255, 214, 218, 226), fade));
    StringFormat left;
    left.SetAlignment(StringAlignmentNear);
    left.SetLineAlignment(StringAlignmentCenter);
    graphics.DrawString(toast.title.c_str(), -1, &title,
                        Gdiplus::RectF(68.0f, 14.0f, width - 82.0f, 32.0f), &left, &titleInk);
    graphics.DrawString(toast.body.c_str(), -1, &body,
                        Gdiplus::RectF(68.0f, 44.0f, width - 82.0f, 50.0f), &left, &bodyInk);
    graphics.Restore(saved);
  }
}

void OverlayWindow::DrawGlitch(Graphics& graphics, const RenderState& state) const {
  const float intensity = std::clamp(state.glitch, 0.0f, 1.0f);
  if (intensity <= 0.01f) return;
  const float canvasWidth = static_cast<float>(width_);
  const float canvasHeight = static_cast<float>(height_);

  // Torn scan bands: slices of the display that slipped sideways.
  const int bands = static_cast<int>(1.0f + intensity * 7.0f);
  for (int band = 0; band < bands; ++band) {
    const std::uint32_t seed = frame_ / 3U * 71U + static_cast<std::uint32_t>(band) * 911U;
    if (Noise01(seed) > 0.25f + intensity * 0.6f) continue;
    const float y = Noise01(seed + 1U) * canvasHeight;
    const float bandHeight = 6.0f + Noise01(seed + 2U) * 46.0f * intensity;
    const float shift = NoiseSigned(seed + 3U) * 60.0f * intensity;
    SolidBrush tear(Color(static_cast<BYTE>(28 + 46 * intensity), 255, 45, 170));
    SolidBrush tearCyan(Color(static_cast<BYTE>(24 + 40 * intensity), 40, 230, 255));
    graphics.FillRectangle(&tear, shift, y, canvasWidth, bandHeight);
    graphics.FillRectangle(&tearCyan, -shift * 0.6f, y + bandHeight * 0.35f, canvasWidth,
                           bandHeight * 0.45f);
  }

  // Datamosh blocks: chunks of "corrupted framebuffer".
  const int blocks = static_cast<int>(intensity * 16.0f);
  for (int block = 0; block < blocks; ++block) {
    const std::uint32_t seed = frame_ / 2U * 313U + static_cast<std::uint32_t>(block) * 577U;
    const float x = Noise01(seed) * canvasWidth;
    const float y = Noise01(seed + 1U) * canvasHeight;
    const float w = 24.0f + Noise01(seed + 2U) * 190.0f;
    const float h = 8.0f + Noise01(seed + 3U) * 46.0f;
    const bool green = Noise01(seed + 4U) > 0.5f;
    SolidBrush brush(green ? Color(70, 57, 255, 20) : Color(70, 255, 45, 170));
    graphics.FillRectangle(&brush, x, y, w, h);
  }

  // CRT scanlines across the whole desktop.
  if (intensity > 0.25f) {
    // Hundreds of full-width lines per frame: antialiasing them would be the
    // most expensive thing on screen and would change nothing visually.
    const SmoothingMode previous = graphics.GetSmoothingMode();
    graphics.SetSmoothingMode(SmoothingModeNone);
    SolidBrush scan(Color(static_cast<BYTE>(16 + 26 * intensity), 0, 0, 0));
    const bool heavyScene = state.geese && state.geese->size() > 12U;
    const int step = heavyScene ? 10 : 4;
    const int offset = static_cast<int>(frame_ % static_cast<unsigned>(step));
    for (int y = offset; y < height_; y += step) {
      graphics.FillRectangle(&scan, 0.0f, static_cast<float>(y), canvasWidth, 1.0f);
    }
    graphics.SetSmoothingMode(previous);
  }

  // Ghost cursors: the pointer appears to have multiplied.
  const float cursorChaos = std::clamp(state.cursorChaos, 0.0f, 1.0f);
  const int ghosts = static_cast<int>(intensity * 9.0f + cursorChaos * 28.0f);
  const Vec2 realCursor = ScreenToCanvas(state.cursor);
  for (int ghost = 0; ghost < ghosts; ++ghost) {
    const std::uint32_t seed = frame_ / 4U * 149U + static_cast<std::uint32_t>(ghost) * 383U;
    const float spread = intensity + cursorChaos * 1.8f;
    const Vec2 spot{realCursor.x + NoiseSigned(seed) * 260.0f * spread,
                    realCursor.y + NoiseSigned(seed + 1U) * 210.0f * spread};
    DrawArrowCursor(graphics, spot, 1.15f, Color(150, 255, 255, 255), Color(190, 28, 25, 34));
  }

  if (cursorChaos > 0.58f) {
    Font warning(PosterFamily(), 31.0f, FontStyleBold, UnitPixel);
    DrawSplitText(graphics, L"CURSOR CONTROL LOST", warning,
                  {realCursor.x - 240.0f, realCursor.y + 42.0f,
                   realCursor.x + 240.0f, realCursor.y + 88.0f},
                  kCriticalRed, 2.0f + cursorChaos * 8.0f);
  }

  // A window that stopped responding, drawn as a hollow ghost frame.
  if (intensity > 0.45f) {
    const std::uint32_t seed = frame_ / 40U * 617U;
    const float w = 300.0f + Noise01(seed + 2U) * 240.0f;
    const float h = 180.0f + Noise01(seed + 3U) * 150.0f;
    const float x = Noise01(seed) * std::max(1.0f, canvasWidth - w);
    const float y = Noise01(seed + 1U) * std::max(1.0f, canvasHeight - h);
    const float drift = NoiseSigned(frame_ / 3U * 29U) * 6.0f * intensity;
    SolidBrush body(Color(70, 236, 240, 246));
    SolidBrush bar(Color(120, 40, 46, 60));
    Pen frameEdge(Color(150, 255, 45, 170), 2.0f);
    graphics.FillRectangle(&body, x + drift, y, w, h);
    graphics.FillRectangle(&bar, x + drift, y, w, 30.0f);
    graphics.DrawRectangle(&frameEdge, x + drift, y, w, h);
    Font font(UiFamily(), 15.0f, FontStyleRegular, UnitPixel);
    SolidBrush ink(Color(210, 255, 255, 255));
    StringFormat left;
    left.SetLineAlignment(StringAlignmentCenter);
    graphics.DrawString(L"explorer.exe - Not Responding", -1, &font,
                        Gdiplus::RectF(x + drift + 10.0f, y, w - 20.0f, 30.0f), &left, &ink);
  }

  // Full-frame white/blue kicks, only ever painted inside our own overlay.
  if (intensity > 0.6f && Noise01(frame_ * 613U) > 0.955f) {
    SolidBrush flash(Noise01(frame_ * 977U) > 0.5f ? Color(46, 255, 255, 255) : Color(56, 18, 92, 171));
    graphics.FillRectangle(&flash, 0, 0, width_, height_);
  }
}

void OverlayWindow::DrawHud(Graphics& graphics, const RenderState& state) const {
  const std::uint32_t boil = frame_ / 6U % 3U;
  SolidBrush dark(Color(226, 18, 18, 24));
  SolidBrush light(Color(245, 255, 255, 255));

  // Aura counter: shakes and flashes whenever the number moves.
  {
    const double sinceDelta = state.logicalTime - state.auraDeltaAt;
    const float punch = static_cast<float>(std::clamp(1.0 - sinceDelta / 0.9, 0.0, 1.0));
    const GraphicsState saved = graphics.Save();
    graphics.TranslateTransform(static_cast<float>(width_ - 286) + NoiseSigned(frame_ * 7U) * punch * 7.0f,
                                14.0f + NoiseSigned(frame_ * 7U + 3U) * punch * 6.0f);
    graphics.RotateTransform(1.6f + punch * NoiseSigned(frame_ * 11U) * 3.0f);
    GraphicsPath auraBox;
    AddWobblyRectangle(auraBox, 0.0f, 0.0f, 268.0f, 48.0f, 2.6f, 733U + boil);
    SolidBrush auraFill(Color(232, 22, 22, 30));
    Pen auraPen(state.aura >= 0 ? kMatrixGreen : kNeonPink, 3.2f + punch * 2.0f);
    auraPen.SetLineJoin(LineJoinRound);
    graphics.FillPath(&auraFill, &auraBox);
    graphics.DrawPath(&auraPen, &auraBox);
    std::wostringstream aura;
    aura << L"AURA  " << state.aura;
    Font auraFont(PosterFamily(), 24.0f, FontStyleBold, UnitPixel);
    DrawSplitText(graphics, aura.str(), auraFont, {6.0f, 4.0f, 262.0f, 44.0f},
                  Color(250, 255, 255, 255), punch * 3.5f + state.glitch * 2.0f);
    graphics.Restore(saved);

    if (punch > 0.0f && state.auraDelta != 0) {
      std::wostringstream delta;
      delta << (state.auraDelta > 0 ? L"+" : L"") << state.auraDelta;
      Font deltaFont(PosterFamily(), 30.0f, FontStyleBold, UnitPixel);
      SolidBrush deltaBrush(WithAlpha(state.auraDelta > 0 ? kMatrixGreen : kCriticalRed, punch));
      const float rise = (1.0f - punch) * 46.0f;
      DrawCenteredText(graphics, delta.str(), deltaFont,
                       {static_cast<float>(width_ - 286), 70.0f - rise,
                        static_cast<float>(width_ - 18), 108.0f - rise},
                       deltaBrush);
    }
  }

  if (state.popupCount > 0) {
    std::wostringstream counter;
    counter << L"WINDOWS OPEN: " << state.popupCount;
    Font font(MonoFamily(), 15.0f, FontStyleBold, UnitPixel);
    SolidBrush ink(Color(200, 255, 45, 170));
    DrawCenteredText(graphics, counter.str(), font,
                     {static_cast<float>(width_ - 286), 112.0f, static_cast<float>(width_ - 18), 136.0f},
                     ink);
  }

  if (state.logicalTime >= 90.0 && state.logicalTime < 135.0) {
    constexpr std::array<const wchar_t*, 3> subtitles = {
        L"Tralala la la la...", L"Tutti frutti cappuccina", L"Bombardino crocodilo"};
    const auto index = static_cast<std::size_t>(state.logicalTime / 4.0) % subtitles.size();
    Font subtitleFont(UiFamily(), 25.0f, FontStyleBold, UnitPixel);
    const GraphicsState saved = graphics.Save();
    graphics.TranslateTransform(width_ * 0.5f - 214.0f, height_ - 104.0f);
    graphics.RotateTransform(-0.9f);
    GraphicsPath box;
    AddWobblyRectangle(box, 0.0f, 0.0f, 428.0f, 50.0f, 2.4f, 1201U + boil);
    graphics.FillPath(&dark, &box);
    Pen edge(Color(180, 255, 251, 234), 2.0f);
    graphics.DrawPath(&edge, &box);
    DrawCenteredText(graphics, subtitles[index], subtitleFont, {6.0f, 4.0f, 422.0f, 46.0f}, light);
    graphics.Restore(saved);
  }

  if (state.finalMonologue) {
    Font criticalFont(PosterFamily(), 46.0f, FontStyleBold, UnitPixel);
    DrawSplitText(graphics, L"CRITICAL ERROR: MAXIMUM BRAINROT REACHED.", criticalFont,
                  {20.0f, height_ * 0.16f, static_cast<float>(width_ - 20), height_ * 0.28f},
                  kCriticalRed, 3.0f + state.glitch * 6.0f);
  }

  if (state.countdown) {
    const int remaining = std::clamp(30 - static_cast<int>(state.logicalTime - 270.0), 0, 30);
    wchar_t countdown[16]{};
    swprintf(countdown, std::size(countdown), L"00:%02d", remaining);
    Font countdownFont(MonoFamily(), 72.0f, FontStyleBold, UnitPixel);
    const float beat = static_cast<float>(std::fmod(state.logicalTime, 1.0));
    const float kick = beat < 0.12f ? 1.0f - beat / 0.12f : 0.0f;
    DrawSplitText(graphics, countdown, countdownFont,
                  {width_ * 0.5f - 200.0f, 18.0f - kick * 4.0f, width_ * 0.5f + 200.0f,
                   112.0f - kick * 4.0f},
                  kCriticalRed, 2.0f + kick * 6.0f);
  }

  if (state.resetButton) {
    const float buttonWidth = 268.0f;
    const float x = width_ * 0.5f - buttonWidth * 0.5f;
    const float y = height_ - 136.0f;
    const GraphicsState saved = graphics.Save();
    graphics.TranslateTransform(x, y);
    graphics.RotateTransform(-1.2f);
    GraphicsPath button;
    AddWobblyRectangle(button, 0.0f, 0.0f, buttonWidth, 68.0f, 3.0f, 1607U + boil);
    SolidBrush fill(kNeonPink);
    Pen edge(Color(255, 255, 255, 255), 4.0f);
    edge.SetLineJoin(LineJoinRound);
    graphics.FillPath(&fill, &button);
    graphics.DrawPath(&edge, &button);
    Font font(PosterFamily(), 30.0f, FontStyleBold, UnitPixel);
    DrawCenteredText(graphics, L"DO NOT PRESS", font, {0.0f, 2.0f, buttonWidth, 66.0f}, light);
    graphics.Restore(saved);
  }

  if (state.emergencyProgress > 0.0f) {
    const float width = std::min(560.0f, width_ - 40.0f);
    const float x = (width_ - width) * 0.5f;
    const float y = height_ - 38.0f;
    SolidBrush track(Color(230, 20, 20, 26));
    SolidBrush progress(kMatrixGreen);
    GraphicsPath frame;
    AddWobblyRectangle(frame, x, y, width, 24.0f, 1.8f, 2311U + boil);
    graphics.FillPath(&track, &frame);
    graphics.FillRectangle(&progress, x + 3.0f, y + 3.0f,
                           (width - 6.0f) * std::clamp(state.emergencyProgress, 0.0f, 1.0f), 18.0f);
    Font font(UiFamily(), 14.0f, FontStyleBold, UnitPixel);
    DrawCenteredText(graphics, L"Hold Esc to close everything and restore the desktop", font,
                     {x, y - 26.0f, x + width, y - 2.0f}, light);
  }
}

void OverlayWindow::DrawFakeShutdown(Graphics& graphics, const RenderState& state) const {
  const double age = state.logicalTime - 300.0;
  SolidBrush background(Color(255, 0, 0, 0));
  graphics.FillRectangle(&background, 0, 0, width_, height_);

  // A short, screen-filling aura-core detonation before the hard cut to black.
  if (age < 1.15) {
    const float blast = static_cast<float>(std::clamp(age / 1.15, 0.0, 1.0));
    const Vec2 center{width_ * 0.5f, height_ * 0.48f};
    if (age < 0.16) {
      const BYTE alpha = static_cast<BYTE>(255.0 * (1.0 - age / 0.16));
      SolidBrush flash(Color(alpha, 255, 255, 255));
      graphics.FillRectangle(&flash, 0, 0, width_, height_);
    }
    for (int fragment = 0; fragment < 67; ++fragment) {
      const std::uint32_t seed = static_cast<std::uint32_t>(fragment) * 977U + 67U;
      const float angle = Noise01(seed) * 2.0f * kPi;
      const float length = (70.0f + Noise01(seed + 1U) * width_ * 0.72f) * blast;
      const float thickness = 2.0f + Noise01(seed + 2U) * 13.0f * (1.0f - blast * 0.45f);
      const Color color = fragment % 3 == 0 ? kMatrixGreen
                          : fragment % 3 == 1 ? kNeonPink
                                              : Color(255, 255, 171, 36);
      Pen ray(WithAlpha(color, 1.0f - blast * 0.72f), thickness);
      ray.SetStartCap(LineCapRound);
      ray.SetEndCap(LineCapRound);
      graphics.DrawLine(&ray, center.x, center.y,
                        center.x + std::cos(angle) * length,
                        center.y + std::sin(angle) * length);
    }
    SolidBrush core(WithAlpha(Color(255, 255, 255, 255), 1.0f - blast));
    const float radius = 36.0f + blast * std::min(width_, height_) * 0.46f;
    FillCircle(graphics, core, center, radius);
    Font detonation(PosterFamily(), 48.0f, FontStyleBold, UnitPixel);
    DrawSplitText(graphics, L"AURA CORE DETONATED", detonation,
                  {20.0f, height_ * 0.12f, static_cast<float>(width_ - 20), height_ * 0.28f},
                  kCriticalRed, 4.0f + blast * 14.0f);
    return;
  }

  const float entrance = static_cast<float>(std::clamp((age - 1.15) / 1.35, 0.0, 1.0));
  const float eased = 1.0f - std::pow(1.0f - entrance, 3.0f);
  const Vec2 goosePosition{-90.0f + eased * (std::min(width_ * 0.27f, 330.0f) + 90.0f),
                           height_ * 0.72f};
  GooseEntity farewell(goosePosition);
  farewell.Honk(8.0f);
  DrawGoose(graphics, farewell, 0);
  if (entrance > 0.55f) {
    DrawSpeechBubble(graphics, L"GOODBYE, DUDE.\nYOU SHOULD'VE LISTENED.",
                     farewell.Rig().beakTip);
  }
}

void OverlayWindow::Present() {
  if (!window_ || !surfaceDc_) return;
  if (preview_) {
    InvalidateRect(window_, nullptr, FALSE);
    UpdateWindow(window_);
    return;
  }
  DismissShellSurface();
  HDC screen = GetDC(nullptr);
  if (!screen) return;
  POINT destination{screenOriginX_, screenOriginY_};
  POINT source{0, 0};
  SIZE size{width_, height_};
  BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  UpdateLayeredWindow(window_, screen, &destination, &size, surfaceDc_, &source, 0, &blend, ULW_ALPHA);
  ReleaseDC(nullptr, screen);
  SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void OverlayWindow::DismissShellSurface() {
  if (preview_) return;
  const ULONGLONG now = GetTickCount64();
  if (now - lastShellDismissAt_ < 200) return;
  lastShellDismissAt_ = now;
  HWND foreground = GetForegroundWindow();
  if (!foreground || foreground == window_) return;

  wchar_t className[96]{};
  GetClassNameW(foreground, className, static_cast<int>(std::size(className)));
  bool shellSurface = _wcsicmp(className, L"DV2ControlHost") == 0;
  DWORD processId = 0;
  GetWindowThreadProcessId(foreground, &processId);
  HANDLE process = processId ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId)
                             : nullptr;
  if (process) {
    wchar_t path[1024]{};
    DWORD length = static_cast<DWORD>(std::size(path));
    if (QueryFullProcessImageNameW(process, 0, path, &length)) {
      shellSurface = shellSurface || IsShellSurfaceProcess(path);
    }
    CloseHandle(process);
  }
  if (!shellSurface) return;

  // Start and Search use a higher shell z-band than normal TOPMOST windows.
  // Targeting Escape and hiding that one surface avoids global input and lets
  // the shell show it normally again the next time the user presses Start.
  PostMessageW(foreground, WM_KEYDOWN, VK_ESCAPE, 0);
  PostMessageW(foreground, WM_KEYUP, VK_ESCAPE, 0);
  ShowWindowAsync(foreground, SW_HIDE);
}

void OverlayWindow::RequestClose() {
  if (window_) PostMessageW(window_, WM_CLOSE, 0, 0);
}

void OverlayWindow::Close() {
  if (window_) {
    KillTimer(window_, 67);
    DestroyWindow(window_);
    window_ = nullptr;
  }
  images_.clear();
  if (surfaceDc_) {
    if (oldBitmap_) SelectObject(surfaceDc_, oldBitmap_);
    if (surfaceBitmap_) DeleteObject(surfaceBitmap_);
    DeleteDC(surfaceDc_);
  }
  surfaceDc_ = nullptr;
  surfaceBitmap_ = nullptr;
  oldBitmap_ = nullptr;
  surfacePixels_ = nullptr;
  if (gdiplusToken_) {
    GdiplusShutdown(gdiplusToken_);
    gdiplusToken_ = 0;
  }
}

LRESULT CALLBACK OverlayWindow::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  OverlayWindow* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<OverlayWindow*>(create->lpCreateParams);
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

LRESULT OverlayWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_GETMINMAXINFO:
      if (preview_) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = 640;
        limits->ptMinTrackSize.y = 400;
        return 0;
      }
      break;
    case WM_TIMER:
      if (wParam == 67 && tickHandler_) tickHandler_();
      return 0;
    case WM_SIZE:
      if (preview_ && wParam != SIZE_MINIMIZED && LOWORD(lParam) > 0 && HIWORD(lParam) > 0 && surfaceDc_) {
        RecreateSurface(LOWORD(lParam), HIWORD(lParam));
      }
      return 0;
    case WM_DISPLAYCHANGE:
      if (!preview_) {
        const int newOriginX = primaryMonitorOnly_ ? 0 : GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int newOriginY = primaryMonitorOnly_ ? 0 : GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int newWidth = GetSystemMetrics(primaryMonitorOnly_ ? SM_CXSCREEN : SM_CXVIRTUALSCREEN);
        const int newHeight = GetSystemMetrics(primaryMonitorOnly_ ? SM_CYSCREEN : SM_CYVIRTUALSCREEN);
        if (RecreateSurface(newWidth, newHeight)) {
          screenOriginX_ = newOriginX;
          screenOriginY_ = newOriginY;
          SetWindowPos(window_, HWND_TOPMOST, newOriginX, newOriginY, newWidth, newHeight,
                       SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
      }
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC target = BeginPaint(window_, &paint);
      if (preview_ && surfaceDc_) BitBlt(target, 0, 0, width_, height_, surfaceDc_, 0, 0, SRCCOPY);
      EndPaint(window_, &paint);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_QUERYENDSESSION:
      return TRUE;
    case WM_ENDSESSION:
      if (wParam && closeHandler_) closeHandler_();
      return 0;
    case WM_CLOSE:
      if (closeHandler_) closeHandler_();
      DestroyWindow(window_);
      return 0;
    case WM_DESTROY:
      window_ = nullptr;
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace gooserot
