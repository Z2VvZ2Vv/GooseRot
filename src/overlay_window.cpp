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
    if (image && ((image->thumbnail && image->thumbnail->GetLastStatus() == Ok) ||
                  (image->image && image->image->GetLastStatus() == Ok))) {
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
  if (result->image && result->image->GetLastStatus() == Ok) {
    constexpr INT kCachedSpritePixels = 256;
    auto thumbnail = std::make_unique<Bitmap>(kCachedSpritePixels, kCachedSpritePixels,
                                               PixelFormat32bppPARGB);
    if (thumbnail->GetLastStatus() == Ok) {
      Graphics cacheGraphics(thumbnail.get());
      cacheGraphics.Clear(Color(0, 0, 0, 0));
      cacheGraphics.SetCompositingMode(CompositingModeSourceCopy);
      cacheGraphics.SetCompositingQuality(CompositingQualityHighQuality);
      cacheGraphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
      cacheGraphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
      if (cacheGraphics.DrawImage(result->image.get(), Rect(0, 0, kCachedSpritePixels,
                                                            kCachedSpritePixels)) == Ok) {
        result->thumbnail = std::move(thumbnail);
      }
    }
  }
  if (result->thumbnail) {
    result->image.reset();
    result->stream->Release();
    result->stream = nullptr;
    result->memory = nullptr;
  }
  return result;
}

Image* OverlayWindow::FindImage(int resourceId) const {
  for (const auto& item : images_) {
    if (item.first == resourceId) {
      return item.second->thumbnail ? item.second->thumbnail.get() : item.second->image.get();
    }
  }
  return nullptr;
}

OverlayWindow::CachedSprite* OverlayWindow::FindCachedSprite(int resourceId, int sizePixels,
                                                             int angleTenths) {
  const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<unsigned>(resourceId)) << 32U) |
                            (static_cast<std::uint64_t>(static_cast<unsigned>(sizePixels) & 0xFFFFU)
                             << 16U) |
                            static_cast<std::uint16_t>(angleTenths + 32768);
  const auto existing = spriteCache_.find(key);
  if (existing != spriteCache_.end()) return existing->second.get();
  constexpr unsigned kMaximumSpriteCacheBuildsPerFrame = 4;
  if (spriteCacheBuildsThisFrame_ >= kMaximumSpriteCacheBuildsPerFrame) return nullptr;
  ++spriteCacheBuildsThisFrame_;

  Image* source = FindImage(resourceId);
  if (!source || sizePixels <= 0) return nullptr;
  const float angle = static_cast<float>(angleTenths) / 10.0f;
  const float radians = angle * kPi / 180.0f;
  const int extent = std::max(1, static_cast<int>(std::ceil(
                                     sizePixels * (std::abs(std::cos(radians)) +
                                                   std::abs(std::sin(radians))))) + 4);
  auto bitmap = std::make_unique<Bitmap>(extent, extent, PixelFormat32bppPARGB);
  if (bitmap->GetLastStatus() != Ok) return nullptr;

  Graphics cacheGraphics(bitmap.get());
  cacheGraphics.Clear(Color(0, 0, 0, 0));
  cacheGraphics.SetCompositingMode(CompositingModeSourceCopy);
  cacheGraphics.SetCompositingQuality(CompositingQualityHighQuality);
  cacheGraphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
  cacheGraphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
  cacheGraphics.TranslateTransform(extent * 0.5f, extent * 0.5f);
  cacheGraphics.RotateTransform(angle);
  const Rect destination(-sizePixels / 2, -sizePixels / 2, sizePixels, sizePixels);
  if (cacheGraphics.DrawImage(source, destination) != Ok) return nullptr;
  cacheGraphics.Flush(FlushIntentionSync);

  auto cached = std::make_unique<CachedSprite>();
  cached->width = extent;
  cached->height = extent;
  cached->pixels.resize(static_cast<std::size_t>(extent) * static_cast<std::size_t>(extent));
  BitmapData data{};
  const Rect bounds(0, 0, extent, extent);
  if (bitmap->LockBits(&bounds, ImageLockModeRead, PixelFormat32bppPARGB, &data) != Ok) {
    return nullptr;
  }
  const auto* firstRow = static_cast<const BYTE*>(data.Scan0);
  for (int y = 0; y < extent; ++y) {
    const auto* sourceRow = firstRow + static_cast<std::ptrdiff_t>(y) * data.Stride;
    std::memcpy(cached->pixels.data() + static_cast<std::size_t>(y) * extent, sourceRow,
                static_cast<std::size_t>(extent) * sizeof(std::uint32_t));
  }
  bitmap->UnlockBits(&data);
  cached->bitmap = std::move(bitmap);

  CachedSprite* result = cached.get();
  spriteCache_.emplace(key, std::move(cached));
  return result;
}

void OverlayWindow::BlendCachedSprite(const CachedSprite& sprite, int destinationX,
                                      int destinationY) {
  if (!surfacePixels_ || sprite.pixels.empty()) return;
  const int sourceLeft = std::max(0, -destinationX);
  const int sourceTop = std::max(0, -destinationY);
  const int sourceRight = std::min(sprite.width, width_ - destinationX);
  const int sourceBottom = std::min(sprite.height, height_ - destinationY);
  if (sourceLeft >= sourceRight || sourceTop >= sourceBottom) return;

  auto* destinationPixels = static_cast<std::uint32_t*>(surfacePixels_);
  for (int y = sourceTop; y < sourceBottom; ++y) {
    const std::uint32_t* source = sprite.pixels.data() +
                                  static_cast<std::size_t>(y) * sprite.width + sourceLeft;
    std::uint32_t* destination = destinationPixels +
                                 static_cast<std::size_t>(destinationY + y) * width_ +
                                 destinationX + sourceLeft;
    for (int x = sourceLeft; x < sourceRight; ++x, ++source, ++destination) {
      const std::uint32_t sourcePixel = *source;
      const unsigned sourceAlpha = sourcePixel >> 24U;
      if (sourceAlpha == 0U) continue;
      if (sourceAlpha == 255U) {
        *destination = sourcePixel;
        continue;
      }
      *destination = BlendPremultipliedArgb(sourcePixel, *destination);
    }
  }
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
  const float scale = TagScale(CanvasBounds());
  const Vec2 center = TagCenter(CanvasBounds());
  return StrokeHead(BuildTagStrokes(center, scale), progress);
}

void OverlayWindow::Render(const RenderState& state) {
  if (!surfacePixels_ || width_ <= 0 || height_ <= 0) return;
  ++frame_;
  spriteCacheBuildsThisFrame_ = 0;
  if (fpsSampleStartedAt_ == 0) fpsSampleStartedAt_ = GetTickCount64();
  LARGE_INTEGER profileFrequency{};
  LARGE_INTEGER profileStamp{};
  QueryPerformanceFrequency(&profileFrequency);
  QueryPerformanceCounter(&profileStamp);
  const auto markProfile = [&]() {
    LARGE_INTEGER current{};
    QueryPerformanceCounter(&current);
    const double milliseconds = static_cast<double>(current.QuadPart - profileStamp.QuadPart) *
                                1000.0 / static_cast<double>(profileFrequency.QuadPart);
    profileStamp = current;
    return milliseconds;
  };
  if (preview_ || (!state.colorFilter && !state.fakeShutdown)) {
    std::memset(surfacePixels_, 0,
                static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4U);
  }
  Bitmap surface(width_, height_, width_ * 4, PixelFormat32bppPARGB,
                 static_cast<BYTE*>(surfacePixels_));
  Graphics graphics(&surface);
  const std::size_t spriteCount = state.sprites ? state.sprites->size() : 0U;
  const std::uint64_t surfacePixels = static_cast<std::uint64_t>(width_) *
                                      static_cast<std::uint64_t>(height_);
  heavyScene_ = (state.geese && state.geese->size() > 12U) || state.popupCount > 24 ||
                spriteCount > 8U || surfacePixels > 3'000'000ULL;
  const bool heavyScene = heavyScene_;
  graphics.SetSmoothingMode(heavyScene ? SmoothingModeHighSpeed : SmoothingModeAntiAlias);
  graphics.SetInterpolationMode(heavyScene ? InterpolationModeLowQuality
                                           : InterpolationModeHighQualityBicubic);
  graphics.SetCompositingQuality(heavyScene ? CompositingQualityHighSpeed
                                            : CompositingQualityDefault);
  graphics.SetPixelOffsetMode(heavyScene ? PixelOffsetModeHighSpeed : PixelOffsetModeDefault);
  graphics.SetTextRenderingHint(heavyScene ? TextRenderingHintSingleBitPerPixelGridFit
                                           : TextRenderingHintAntiAliasGridFit);
  setupSampleMs_ += markProfile();

  if (preview_) DrawPreviewDesktop(graphics);
  sceneSampleMs_ += markProfile();
  if (state.fakeShutdown) {
    DrawFakeShutdown(graphics, state);
    effectsSampleMs_ += markProfile();
  } else {
    const GraphicsState saved = graphics.Save();
    // Shake: a steady beat from the screen-shake beat onwards, plus an extra
    // kick the more the display is falling apart.
    if (state.logicalTime >= phase::kScreenShake) {
      const int pulse = static_cast<int>((state.logicalTime - phase::kScreenShake) / 2.0);
      if (std::fmod(state.logicalTime - phase::kScreenShake, 2.0) < 0.18) {
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
    sceneSampleMs_ += markProfile();
    DrawSprites(graphics, state);
    spriteSampleMs_ += markProfile();
    // The tag goes on after the photos. Props keep clear of its footprint, but
    // drawing it last is what guarantees a small screen can never end up with
    // the 67 buried under a stack of brainrot images.
    if (state.graffiti) DrawGraffiti(graphics, state);
    DrawClipboardBadge(graphics, state);
    sceneSampleMs_ += markProfile();
    if (state.geese) {
      for (std::size_t index = 0; index < state.geese->size(); ++index) {
        if (heavyScene && index >= 3U) {
          DrawGooseCompact(graphics, (*state.geese)[index], static_cast<int>(index));
        } else {
          DrawGoose(graphics, (*state.geese)[index], static_cast<int>(index));
        }
      }
    }
    gooseSampleMs_ += markProfile();
    DrawCursorLatch(graphics, state);
    if (!state.bubbleText.empty()) DrawSpeechBubble(graphics, state.bubbleText, state.bubbleAnchor);
    DrawHud(graphics, state);
    DrawToasts(graphics, state);
    DrawGlitch(graphics, state);
    graphics.Restore(saved);
    effectsSampleMs_ += markProfile();
  }
  if (!Present()) ++presentFailureSample_;
  presentSampleMs_ += markProfile();
  ++fpsSampleFrames_;
  const ULONGLONG completedAt = GetTickCount64();
  if (completedAt - fpsSampleStartedAt_ >= 1000) {
    const double frames = static_cast<double>(fpsSampleFrames_);
    const double fps = frames * 1000.0 /
                       static_cast<double>(completedAt - fpsSampleStartedAt_);
    wchar_t title[384]{};
    swprintf(title, std::size(title),
             L"GooseRot%ls - %.1f FPS | %dx%d | setup %.1f scene %.1f images %.1f geese %.1f fx %.1f present %.1f ms | fail %u/%u err %lu/%lu",
             preview_ ? L" Preview" : L"", fps, width_, height_, setupSampleMs_ / frames,
             sceneSampleMs_ / frames, spriteSampleMs_ / frames, gooseSampleMs_ / frames,
             effectsSampleMs_ / frames, presentSampleMs_ / frames, presentFailureSample_,
             topmostFailureSample_, static_cast<unsigned long>(lastPresentError_),
             static_cast<unsigned long>(lastTopmostError_));
    SetWindowTextW(window_, title);
    fpsSampleStartedAt_ = completedAt;
    fpsSampleFrames_ = 0;
    setupSampleMs_ = 0.0;
    sceneSampleMs_ = 0.0;
    spriteSampleMs_ = 0.0;
    gooseSampleMs_ = 0.0;
    effectsSampleMs_ = 0.0;
    presentSampleMs_ = 0.0;
    presentFailureSample_ = 0;
    topmostFailureSample_ = 0;
  }
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

void OverlayWindow::DrawSprites(Graphics& graphics, const RenderState& state) {
  if (!state.sprites) return;
  Matrix sceneTransform;
  REAL transform[6]{};
  graphics.GetTransform(&sceneTransform);
  sceneTransform.GetElements(transform);
  const int sceneOffsetX = static_cast<int>(std::lround(transform[4]));
  const int sceneOffsetY = static_cast<int>(std::lround(transform[5]));
  bool gdiDrawingSinceFlush = true;
  for (const VisualSprite& sprite : *state.sprites) {
    // A photo on its first trip has not entered the world yet.
    if (!sprite.IsVisible()) continue;
    Image* image = FindImage(sprite.resourceId);
    if (!image) continue;
    const double age = state.logicalTime - sprite.createdAt;
    if (age < 0.0 || age > sprite.lifetime) continue;
    const double sinceStage = state.logicalTime - sprite.stageChangedAt;
    const float fadeIn = static_cast<float>(std::clamp(age / 0.6, 0.0, 1.0));
    const float fadeOut = static_cast<float>(std::clamp((sprite.lifetime - age) / 1.0, 0.0, 1.0));
    float alpha = std::min(fadeIn, fadeOut);
    // Torn off the desktop: it snaps outward and burns off in a few frames.
    float tear = 0.0f;
    if (sprite.stage == PropStage::Tearing) {
      tear = static_cast<float>(std::clamp(sinceStage / 0.45, 0.0, 1.0));
      alpha = std::min(alpha, 1.0f - tear);
    }
    // Stickers slap down with a short overshoot instead of fading in politely.
    const double sincePlaced = sprite.stage == PropStage::Placed ? sinceStage : 1.0;
    const float pop = sincePlaced < 0.35 ? 1.0f + static_cast<float>(0.35 - sincePlaced) * 0.75f
                                         : 1.0f;
    const float carriedScale = sprite.stage == PropStage::Carried ? 0.68f : 1.0f;
    const float size = sprite.size * pop * carriedScale * (1.0f + tear * 0.35f);
    const bool stableGeometry = sprite.stage == PropStage::Placed && sincePlaced >= 0.35;
    CachedSprite* cached = stableGeometry
                               ? FindCachedSprite(sprite.resourceId,
                                                  std::max(1, static_cast<int>(std::lround(size))),
                                                  static_cast<int>(std::lround(
                                                      sprite.angleDegrees * 10.0f)))
                               : nullptr;
    if (cached && alpha >= 0.995f) {
      if (gdiDrawingSinceFlush) {
        graphics.Flush(FlushIntentionSync);
        gdiDrawingSinceFlush = false;
      }
      BlendCachedSprite(*cached,
                        sceneOffsetX + static_cast<int>(
                                           std::lround(sprite.center.x - cached->width * 0.5f)),
                        sceneOffsetY + static_cast<int>(
                                           std::lround(sprite.center.y - cached->height * 0.5f)));
      continue;
    }
    const GraphicsState saved = graphics.Save();
    Rect destination{};
    if (cached) {
      destination = Rect(static_cast<INT>(std::lround(sprite.center.x - cached->width * 0.5f)),
                         static_cast<INT>(std::lround(sprite.center.y - cached->height * 0.5f)),
                         cached->width, cached->height);
      image = cached->bitmap.get();
    } else {
      graphics.TranslateTransform(sprite.center.x, sprite.center.y);
      graphics.RotateTransform(sprite.angleDegrees);
      destination = Rect(static_cast<INT>(-size * 0.5f), static_cast<INT>(-size * 0.5f),
                         static_cast<INT>(size), static_cast<INT>(size));
    }
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
    if (tear > 0.0f) {
      // A pink slash across whatever is left, so removing a photo reads as
      // damage rather than as a polite fade.
      Pen rip(WithAlpha(kNeonPink, 1.0f - tear), 4.0f + tear * 10.0f);
      rip.SetStartCap(LineCapRound);
      rip.SetEndCap(LineCapRound);
      const float reach = size * (0.35f + tear * 0.5f);
      graphics.DrawLine(&rip, sprite.center.x - reach, sprite.center.y - reach * 0.7f,
                        sprite.center.x + reach, sprite.center.y + reach * 0.7f);
      graphics.DrawLine(&rip, sprite.center.x + reach, sprite.center.y - reach * 0.7f,
                        sprite.center.x - reach, sprite.center.y + reach * 0.7f);
    }
    gdiDrawingSinceFlush = true;
  }

  // Badges go in one pass after every photo, for two reasons: a badge is never
  // hidden by a photo delivered later, and the cached photos keep their single
  // batched flush instead of one per sprite.
  for (const VisualSprite& sprite : *state.sprites) {
    if (sprite.IsClosable(state.logicalTime)) DrawPropCloseBadge(graphics, sprite, heavyScene_);
  }
}

// The [x] the flock leaves on a delivered photo. It is a real target: the app
// hit-tests the same box, and closing it costs aura and buys two more photos.
// Dense scenes get a plain frame instead of a hand-drawn one — dozens of boiling
// outlines per frame is not where the budget belongs.
void OverlayWindow::DrawPropCloseBadge(Graphics& graphics, const VisualSprite& sprite,
                                       bool compact) const {
  const RectF box = PropCloseBox(sprite.center, sprite.size);
  const float width = box.Width();
  const float height = box.Height();
  SolidBrush fill(Color(226, 24, 22, 30));
  Pen edge(kNeonPink, 2.4f);
  edge.SetLineJoin(LineJoinRound);
  if (compact) {
    graphics.FillRectangle(&fill, box.left, box.top, width, height);
    graphics.DrawRectangle(&edge, box.left, box.top, width, height);
  } else {
    GraphicsPath frame;
    AddWobblyRectangle(frame, box.left, box.top, width, height, 1.6f,
                       static_cast<std::uint32_t>(sprite.resourceId) * 131U +
                           static_cast<std::uint32_t>(std::max(0.0f, sprite.center.x)) +
                           frame_ / 8U % 3U);
    graphics.FillPath(&fill, &frame);
    graphics.DrawPath(&edge, &frame);
  }

  Pen cross(Color(240, 255, 251, 234), 2.6f);
  cross.SetStartCap(LineCapRound);
  cross.SetEndCap(LineCapRound);
  const float inset = width * 0.29f;
  graphics.DrawLine(&cross, box.left + inset, box.top + inset, box.right - inset,
                    box.bottom - inset);
  graphics.DrawLine(&cross, box.right - inset, box.top + inset, box.left + inset,
                    box.bottom - inset);
}

void OverlayWindow::DrawSceneEffects(Graphics& graphics, const RenderState& state) {
  if (state.colorFilter) {
    const bool matrix = static_cast<int>((state.logicalTime - phase::kColorFilter) / 2.0) % 2 == 0;
    const Color filterColor = matrix ? Color(38, 57, 255, 20) : Color(38, 255, 45, 170);
    if (!preview_ && surfacePixels_) {
      graphics.Flush(FlushIntentionSync);
      const unsigned alpha = filterColor.GetA();
      const auto premultiply = [alpha](unsigned channel) {
        return (channel * alpha + 127U) / 255U;
      };
      const std::uint32_t pixel = (alpha << 24U) |
                                  (premultiply(filterColor.GetR()) << 16U) |
                                  (premultiply(filterColor.GetG()) << 8U) |
                                  premultiply(filterColor.GetB());
      std::fill_n(static_cast<std::uint32_t*>(surfacePixels_),
                  static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), pixel);
    } else {
      SolidBrush filter(filterColor);
      graphics.FillRectangle(&filter, 0, 0, width_, height_);
    }
  }
}

void OverlayWindow::DrawClipboardBadge(Graphics& graphics, const RenderState& state) const {
  if (!state.clipboardBadge) return;
  const float age = static_cast<float>(state.logicalTime - phase::kClipboard);
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

void OverlayWindow::DrawGraffiti(Graphics& graphics, const RenderState& state) {
  const float progress = std::clamp(state.graffitiProgress, 0.0f, 1.0f);
  if (progress < 1.0f) {
    DrawGraffitiUncached(graphics, state);
    return;
  }

  if (graffitiCacheCanvasWidth_ != width_ || graffitiCacheCanvasHeight_ != height_) {
    graffitiCache_.reset();
    graffitiCacheCanvasWidth_ = width_;
    graffitiCacheCanvasHeight_ = height_;
    graffitiCacheFailures_ = 0;
  }
  constexpr int kMaximumCacheAttempts = 3;
  if (!graffitiCache_ && graffitiCacheFailures_ < kMaximumCacheAttempts) {
    if (!BuildGraffitiCache(state)) ++graffitiCacheFailures_;
  }
  if (!graffitiCache_) {
    // A canvas that will not give us an offscreen bitmap still gets its tag;
    // it just costs a little more per frame.
    DrawGraffitiUncached(graphics, state);
    return;
  }
  graphics.Flush(FlushIntentionSync);
  Matrix sceneTransform;
  REAL transform[6]{};
  graphics.GetTransform(&sceneTransform);
  sceneTransform.GetElements(transform);
  BlendCachedSprite(*graffitiCache_,
                    graffitiCacheX_ + static_cast<int>(std::lround(transform[4])),
                    graffitiCacheY_ + static_cast<int>(std::lround(transform[5])));
}

bool OverlayWindow::BuildGraffitiCache(const RenderState& state) {
  const float scale = TagScale(CanvasBounds());
  const Vec2 center = TagCenter(CanvasBounds());
  const std::vector<SprayStroke> strokes = BuildTagStrokes(center, scale);
  float minimumX = static_cast<float>(width_);
  float minimumY = static_cast<float>(height_);
  float maximumX = 0.0f;
  float maximumY = 0.0f;
  for (const SprayStroke& stroke : strokes) {
    for (const Vec2 point : stroke.points) {
      minimumX = std::min(minimumX, point.x);
      minimumY = std::min(minimumY, point.y);
      maximumX = std::max(maximumX, point.x);
      maximumY = std::max(maximumY, point.y);
    }
  }
  // Covers the widest possible overspray radius, halo and downward drip.
  const float margin = scale * 0.30f * 5.0f + 12.0f;
  const int cacheLeft = std::max(0, static_cast<int>(std::floor(minimumX - margin)));
  const int cacheTop = std::max(0, static_cast<int>(std::floor(minimumY - margin)));
  const int cacheRight = std::min(width_, static_cast<int>(std::ceil(maximumX + margin)) + 1);
  const int cacheBottom = std::min(height_, static_cast<int>(std::ceil(maximumY + margin)) + 1);
  const int cacheWidth = cacheRight - cacheLeft;
  const int cacheHeight = cacheBottom - cacheTop;
  if (cacheWidth <= 0 || cacheHeight <= 0) return false;

  auto canvas = std::make_unique<Bitmap>(cacheWidth, cacheHeight, PixelFormat32bppPARGB);
  if (canvas->GetLastStatus() != Ok) return false;
  Graphics cacheGraphics(canvas.get());
  cacheGraphics.Clear(Color(0, 0, 0, 0));
  cacheGraphics.SetSmoothingMode(SmoothingModeHighSpeed);
  cacheGraphics.SetCompositingQuality(CompositingQualityHighSpeed);
  cacheGraphics.SetInterpolationMode(InterpolationModeLowQuality);
  cacheGraphics.SetPixelOffsetMode(PixelOffsetModeHighSpeed);
  cacheGraphics.TranslateTransform(static_cast<float>(-cacheLeft),
                                   static_cast<float>(-cacheTop));
  DrawGraffitiUncached(cacheGraphics, state);
  cacheGraphics.Flush(FlushIntentionSync);

  BitmapData data{};
  const Rect bounds(0, 0, cacheWidth, cacheHeight);
  if (canvas->LockBits(&bounds, ImageLockModeRead, PixelFormat32bppPARGB, &data) != Ok) {
    return false;
  }
  const auto* firstRow = static_cast<const BYTE*>(data.Scan0);
  int left = cacheWidth;
  int top = cacheHeight;
  int right = -1;
  int bottom = -1;
  for (int y = 0; y < cacheHeight; ++y) {
    const auto* row = reinterpret_cast<const std::uint32_t*>(
        firstRow + static_cast<std::ptrdiff_t>(y) * data.Stride);
    for (int x = 0; x < cacheWidth; ++x) {
      if ((row[x] >> 24U) == 0U) continue;
      left = std::min(left, x);
      top = std::min(top, y);
      right = std::max(right, x);
      bottom = std::max(bottom, y);
    }
  }
  if (right < left || bottom < top) {
    canvas->UnlockBits(&data);
    return false;
  }

  auto cached = std::make_unique<CachedSprite>();
  cached->width = right - left + 1;
  cached->height = bottom - top + 1;
  cached->pixels.resize(static_cast<std::size_t>(cached->width) * cached->height);
  for (int y = 0; y < cached->height; ++y) {
    const auto* sourceRow = reinterpret_cast<const std::uint32_t*>(
        firstRow + static_cast<std::ptrdiff_t>(top + y) * data.Stride) + left;
    std::memcpy(cached->pixels.data() + static_cast<std::size_t>(y) * cached->width,
                sourceRow, static_cast<std::size_t>(cached->width) * sizeof(std::uint32_t));
  }
  canvas->UnlockBits(&data);
  graffitiCacheX_ = cacheLeft + left;
  graffitiCacheY_ = cacheTop + top;
  graffitiCache_ = std::move(cached);
  return true;
}

void OverlayWindow::DrawGraffitiUncached(Graphics& graphics, const RenderState& state) const {
  // The tag is sprayed live: strokes are revealed by arc length, paint sags into
  // drips behind the nozzle and overspray keeps accumulating.
  const float progress = std::clamp(state.graffitiProgress, 0.0f, 1.0f);
  if (progress <= 0.0f) return;
  const float scale = TagScale(CanvasBounds());
  const Vec2 center = TagCenter(CanvasBounds());
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
  // A near-opaque ink pass under everything else. Whatever the desktop, the
  // colour filter or a stray photo is doing behind the tag, the glyph keeps a
  // hard silhouette instead of dissolving into the background.
  Pen backing(Color(235, 24, 6, 26), band * 1.40f);
  backing.SetStartCap(LineCapRound);
  backing.SetEndCap(LineCapRound);
  backing.SetLineJoin(LineJoinRound);
  // The dark pass is laid down next and slightly wider, so the pink core reads
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
      graphics.DrawLines(&backing, visible.data(), static_cast<INT>(visible.size()));
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

  // Rolling signal loss: whole bands of the display drop to black and shear
  // sideways. This is what the finale spends its budget on now that the window
  // swarm is being taken apart instead of grown.
  if (intensity > 0.62f) {
    const float severity = (intensity - 0.62f) / 0.38f;
    const float rollHeight = canvasHeight * (0.06f + severity * 0.16f);
    const float roll = std::fmod(static_cast<float>(frame_) * (5.0f + severity * 16.0f),
                                 canvasHeight + rollHeight) -
                       rollHeight;
    SolidBrush blackout(Color(static_cast<BYTE>(70 + 130 * severity), 0, 0, 0));
    graphics.FillRectangle(&blackout, 0.0f, roll, canvasWidth, rollHeight);
    Pen seam(Color(static_cast<BYTE>(120 + 100 * severity), 255, 45, 170), 2.0f + severity * 3.0f);
    graphics.DrawLine(&seam, 0.0f, roll, canvasWidth, roll + NoiseSigned(frame_ * 53U) * 14.0f);
    graphics.DrawLine(&seam, 0.0f, roll + rollHeight, canvasWidth,
                      roll + rollHeight + NoiseSigned(frame_ * 59U) * 14.0f);
  }

  // Full-frame white/blue kicks, only ever painted inside our own overlay. They
  // get noticeably more frequent as the display gives up.
  const float flashOdds = 0.955f - std::max(0.0f, intensity - 0.6f) * 0.14f;
  if (intensity > 0.6f && Noise01(frame_ * 613U) > flashOdds) {
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

  float hudRow = 112.0f;
  if (state.popupCount > 0) {
    std::wostringstream counter;
    counter << L"WINDOWS OPEN: " << state.popupCount;
    Font font(MonoFamily(), 15.0f, FontStyleBold, UnitPixel);
    SolidBrush ink(Color(200, 255, 45, 170));
    DrawCenteredText(graphics, counter.str(), font,
                     {static_cast<float>(width_ - 286), hudRow,
                      static_cast<float>(width_ - 18), hudRow + 24.0f},
                     ink);
    hudRow += 24.0f;
  }

  if (state.propsClosed > 0) {
    std::wostringstream counter;
    counter << L"PHOTOS TORN: " << state.propsClosed;
    Font font(MonoFamily(), 15.0f, FontStyleBold, UnitPixel);
    SolidBrush ink(Color(200, 255, 36, 56));
    DrawCenteredText(graphics, counter.str(), font,
                     {static_cast<float>(width_ - 286), hudRow,
                      static_cast<float>(width_ - 18), hudRow + 24.0f},
                     ink);
    hudRow += 24.0f;
  }

  // During the storm the pointer alternates between seized and released. Saying
  // which of the two is happening is what turns it into a game instead of a
  // stretch of dead input.
  if (state.cursorStormPhase && !state.cursorLatched) {
    const bool seized = state.cursorChaos > 0.02f;
    Font font(MonoFamily(), 15.0f, FontStyleBold, UnitPixel);
    SolidBrush ink(seized ? Color(220, 255, 36, 56) : Color(210, 57, 255, 20));
    DrawCenteredText(graphics, seized ? L"CURSOR: SEIZED" : L"CURSOR: YOURS", font,
                     {static_cast<float>(width_ - 286), hudRow,
                      static_cast<float>(width_ - 18), hudRow + 24.0f},
                     ink);
  }

  if (state.logicalTime >= phase::kSubtitles && state.logicalTime < phase::kDuplicate) {
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
    const int span = static_cast<int>(phase::kEnd - phase::kCountdown);
    const int remaining =
        std::clamp(static_cast<int>(std::ceil(phase::kEnd - state.logicalTime)), 0, span);
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
  const double age = state.logicalTime - phase::kEnd;
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

bool OverlayWindow::Present() {
  if (!window_ || !surfaceDc_) return false;
  if (preview_) {
    InvalidateRect(window_, nullptr, FALSE);
    UpdateWindow(window_);
    return true;
  }
  DismissShellSurface();
  HDC screen = GetDC(nullptr);
  if (!screen) return false;
  POINT destination{screenOriginX_, screenOriginY_};
  POINT source{0, 0};
  SIZE size{width_, height_};
  BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  const BOOL presented = UpdateLayeredWindow(window_, screen, &destination, &size, surfaceDc_,
                                             &source, 0, &blend, ULW_ALPHA);
  lastPresentError_ = presented ? ERROR_SUCCESS : GetLastError();
  ReleaseDC(nullptr, screen);
  const ULONGLONG now = GetTickCount64();
  if (now - lastTopmostRefreshAt_ >= 500) {
    lastTopmostRefreshAt_ = now;
    if (!SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {
      ++topmostFailureSample_;
      lastTopmostError_ = GetLastError();
    } else {
      lastTopmostError_ = ERROR_SUCCESS;
    }
  }
  return presented != FALSE;
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

void OverlayWindow::StopRenderTimer() {
  if (window_) KillTimer(window_, 67);
}

void OverlayWindow::Close() {
  if (window_) {
    KillTimer(window_, 67);
    DestroyWindow(window_);
    window_ = nullptr;
  }
  images_.clear();
  spriteCache_.clear();
  graffitiCache_.reset();
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
