#include "overlay_window.hpp"

#include <objidl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

#include "resource.h"

namespace gooserot {
namespace {

using namespace Gdiplus;

const Color kNeonPink(255, 255, 45, 170);
const Color kMatrixGreen(255, 57, 255, 20);
const Color kCriticalRed(255, 255, 36, 56);
const Color kBubbleWhite(255, 255, 251, 234);

void AddRoundedRectangle(GraphicsPath& path, float x, float y, float width, float height, float radius) {
  const float diameter = radius * 2.0f;
  path.AddArc(x, y, diameter, diameter, 180.0f, 90.0f);
  path.AddArc(x + width - diameter, y, diameter, diameter, 270.0f, 90.0f);
  path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0.0f, 90.0f);
  path.AddArc(x, y + height - diameter, diameter, diameter, 90.0f, 90.0f);
  path.CloseFigure();
}

void FillCircle(Graphics& graphics, Brush& brush, Vec2 center, float radius) {
  graphics.FillEllipse(&brush, center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);
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
    error = L"Impossible d'initialiser GDI+.";
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
    error = L"Impossible d'enregistrer la fenêtre GooseRot.";
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
    error = L"Impossible de créer l'overlay GooseRot.";
    return false;
  }

  RECT client{};
  GetClientRect(window_, &client);
  if (!RecreateSurface(client.right - client.left, client.bottom - client.top)) {
    error = L"Impossible de créer la surface de rendu 32 bits.";
    return false;
  }
  LoadImages();
  ShowWindow(window_, preview_ ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE);
  UpdateWindow(window_);
  if (SetTimer(window_, 67, 33, nullptr) == 0) {
    error = L"Impossible de démarrer l'horloge de rendu.";
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

void OverlayWindow::Render(const RenderState& state) {
  if (!surfacePixels_ || width_ <= 0 || height_ <= 0) return;
  std::memset(surfacePixels_, 0, static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4U);
  Bitmap surface(width_, height_, width_ * 4, PixelFormat32bppPARGB,
                 static_cast<BYTE*>(surfacePixels_));
  Graphics graphics(&surface);
  graphics.SetSmoothingMode(SmoothingModeAntiAlias);
  graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
  graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

  if (preview_) DrawPreviewDesktop(graphics);
  if (state.fakeShutdown) {
    DrawFakeShutdown(graphics, state);
  } else {
    const GraphicsState saved = graphics.Save();
    if (state.logicalTime >= 210.0) {
      const int pulse = static_cast<int>((state.logicalTime - 210.0) / 2.0);
      if (std::fmod(state.logicalTime - 210.0, 2.0) < 0.18) {
        graphics.TranslateTransform((pulse % 2 == 0) ? 4.0f : -4.0f,
                                    (pulse % 3 == 0) ? -3.0f : 3.0f);
      }
    }
    DrawSceneEffects(graphics, state);
    DrawSprites(graphics, state);
    if (state.geese) {
      for (std::size_t index = 0; index < state.geese->size(); ++index) {
        DrawGoose(graphics, (*state.geese)[index], static_cast<int>(index));
      }
    }
    if (!state.bubbleText.empty()) DrawSpeechBubble(graphics, state.bubbleText, state.bubbleAnchor);
    DrawHud(graphics, state);
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
  Font font(L"Segoe UI", 12.0f, FontStyleRegular, UnitPixel);
  SolidBrush titleText(Color::White);
  graphics.DrawString(L"Preview desktop — no system effects", -1, &font, PointF(65.0f, 69.0f), &titleText);
}

void OverlayWindow::DrawGoose(Graphics& graphics, const GooseEntity& goose, int index) const {
  const Vec2 position = goose.Position();
  const GooseRig& rig = goose.Rig();
  const float angle = goose.DirectionRadians() * 180.0f / 3.14159265358979323846f;
  const Vec2 forward{std::cos(goose.DirectionRadians()), std::sin(goose.DirectionRadians())};
  const Vec2 side{-forward.y, forward.x};

  SolidBrush orange(Color(255, 255, 165, 35));
  Pen orangePen(Color(255, 255, 145, 20), 5.0f);
  orangePen.SetStartCap(LineCapRound);
  orangePen.SetEndCap(LineCapRound);
  graphics.DrawLine(&orangePen, position.x - side.x * 4.0f, position.y - side.y * 4.0f,
                    rig.leftFoot.x, rig.leftFoot.y);
  graphics.DrawLine(&orangePen, position.x + side.x * 4.0f, position.y + side.y * 4.0f,
                    rig.rightFoot.x, rig.rightFoot.y);
  FillCircle(graphics, orange, rig.leftFoot, 4.0f);
  FillCircle(graphics, orange, rig.rightFoot, 4.0f);

  const GraphicsState transformed = graphics.Save();
  graphics.TranslateTransform(position.x, position.y);
  graphics.RotateTransform(angle);
  SolidBrush shadow(Color(55, 0, 0, 0));
  graphics.FillEllipse(&shadow, -30.0f, -18.0f, 58.0f, 38.0f);
  SolidBrush outline(Color(255, 172, 174, 176));
  graphics.FillEllipse(&outline, -29.0f, -23.0f, 54.0f, 46.0f);
  SolidBrush white(index == 0 ? Color(255, 255, 255, 250) : Color(255, 246, 250, 255));
  graphics.FillEllipse(&white, -27.0f, -21.0f, 50.0f, 42.0f);
  Pen wingPen(Color(135, 150, 153, 158), 2.0f);
  graphics.DrawArc(&wingPen, -17.0f, -13.0f, 27.0f, 25.0f, 205.0f, 120.0f);
  graphics.Restore(transformed);

  Pen neckOutline(Color(255, 172, 174, 176), 29.0f);
  neckOutline.SetStartCap(LineCapRound);
  neckOutline.SetEndCap(LineCapRound);
  graphics.DrawLine(&neckOutline, rig.neckBase.x, rig.neckBase.y, rig.headCenter.x, rig.headCenter.y);
  Pen neckWhite(Color(255, 255, 255, 250), 25.0f);
  neckWhite.SetStartCap(LineCapRound);
  neckWhite.SetEndCap(LineCapRound);
  graphics.DrawLine(&neckWhite, rig.neckBase.x, rig.neckBase.y, rig.headCenter.x, rig.headCenter.y);

  SolidBrush headOutline(Color(255, 172, 174, 176));
  FillCircle(graphics, headOutline, rig.headCenter, 16.0f);
  SolidBrush headWhite(Color(255, 255, 255, 250));
  FillCircle(graphics, headWhite, rig.headCenter, 14.0f);

  PointF beak[4] = {
      PointF(rig.headCenter.x + side.x * 8.0f + forward.x * 6.0f,
             rig.headCenter.y + side.y * 8.0f + forward.y * 6.0f),
      PointF(rig.beakTip.x, rig.beakTip.y),
      PointF(rig.headCenter.x - side.x * 8.0f + forward.x * 6.0f,
             rig.headCenter.y - side.y * 8.0f + forward.y * 6.0f),
      PointF(rig.headCenter.x + forward.x * 3.0f, rig.headCenter.y + forward.y * 3.0f)};
  graphics.FillPolygon(&orange, beak, 4);
  Pen beakOutline(Color(210, 190, 105, 18), 1.5f);
  graphics.DrawPolygon(&beakOutline, beak, 4);

  SolidBrush black(Color(255, 20, 20, 24));
  FillCircle(graphics, black, rig.leftEye, 2.2f);
  FillCircle(graphics, black, rig.rightEye, 2.2f);

  if (index > 0) {
    SolidBrush badge(index == 1 ? kNeonPink : kMatrixGreen);
    FillCircle(graphics, badge, position - side * 17.0f, 3.0f);
  }
}

void OverlayWindow::DrawSpeechBubble(Graphics& graphics, const std::wstring& text, Vec2 anchor) const {
  int lines = 1;
  for (const wchar_t character : text) if (character == L'\n') ++lines;
  const float width = std::clamp(190.0f + static_cast<float>(text.size()) * 3.2f, 230.0f, 470.0f);
  const float height = 34.0f + lines * 24.0f + (text.size() > 55 ? 22.0f : 0.0f);
  float x = anchor.x - width * 0.5f;
  float y = anchor.y - height - 72.0f;
  x = std::clamp(x, 12.0f, std::max(12.0f, static_cast<float>(width_) - width - 12.0f));
  y = std::clamp(y, 12.0f, std::max(12.0f, static_cast<float>(height_) - height - 12.0f));

  GraphicsPath path;
  AddRoundedRectangle(path, x, y, width, height, 18.0f);
  PointF tail[3] = {PointF(anchor.x - 12.0f, y + height - 2.0f),
                    PointF(anchor.x, std::min(anchor.y - 18.0f, y + height + 25.0f)),
                    PointF(anchor.x + 12.0f, y + height - 2.0f)};
  SolidBrush shadow(Color(70, 0, 0, 0));
  const GraphicsState shadowState = graphics.Save();
  graphics.TranslateTransform(3.0f, 4.0f);
  graphics.FillPath(&shadow, &path);
  graphics.Restore(shadowState);
  SolidBrush fill(kBubbleWhite);
  Pen outline(Color(255, 35, 35, 42), 3.0f);
  graphics.FillPolygon(&fill, tail, 3);
  graphics.FillPath(&fill, &path);
  graphics.DrawPath(&outline, &path);
  graphics.DrawLines(&outline, tail, 3);

  Font font(L"Segoe UI", 18.0f, FontStyleBold, UnitPixel);
  SolidBrush ink(Color(255, 28, 28, 34));
  RectF textRectangle{x + 14.0f, y + 8.0f, x + width - 14.0f, y + height - 8.0f};
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
    ColorMatrix matrix = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0},
                           {0, 0, 0, alpha, 0}, {0, 0, 0, 0, 1}}};
    ImageAttributes attributes;
    attributes.SetColorMatrix(&matrix);
    const GraphicsState saved = graphics.Save();
    graphics.TranslateTransform(sprite.center.x, sprite.center.y);
    graphics.RotateTransform(sprite.angleDegrees);
    const Rect destination(static_cast<INT>(-sprite.size * 0.5f), static_cast<INT>(-sprite.size * 0.5f),
                           static_cast<INT>(sprite.size), static_cast<INT>(sprite.size));
    graphics.DrawImage(image, destination, 0, 0, image->GetWidth(), image->GetHeight(), UnitPixel, &attributes);
    graphics.Restore(saved);
  }
}

void OverlayWindow::DrawSceneEffects(Graphics& graphics, const RenderState& state) const {
  if (state.colorFilter) {
    const bool matrix = static_cast<int>((state.logicalTime - 240.0) / 2.0) % 2 == 0;
    SolidBrush filter(matrix ? Color(38, 57, 255, 20) : Color(38, 255, 45, 170));
    graphics.FillRectangle(&filter, 0, 0, width_, height_);
  }

  if (state.graffiti) {
    Font graffitiFont(L"Impact", 150.0f, FontStyleBold | FontStyleItalic, UnitPixel);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    GraphicsPath textPath;
    FontFamily family(L"Impact");
    textPath.AddString(L"67", -1, &family, FontStyleBold | FontStyleItalic, 150.0f,
                       PointF(width_ * 0.5f - 150.0f, height_ * 0.43f - 100.0f), &format);
    Pen glow(Color(95, 255, 45, 170), 16.0f);
    Pen edge(Color(255, 55, 5, 40), 5.0f);
    SolidBrush fill(kNeonPink);
    graphics.DrawPath(&glow, &textPath);
    graphics.FillPath(&fill, &textPath);
    graphics.DrawPath(&edge, &textPath);
    (void)graffitiFont;
  }

  if (state.clipboardBadge) {
    const float age = static_cast<float>(state.logicalTime - 120.0);
    const float travel = std::clamp(age / 5.0f, 0.0f, 1.0f);
    const float x = (1.0f - travel) * width_ * 0.5f + travel * (width_ - 230.0f);
    const float y = (1.0f - travel) * height_ * 0.5f + travel * 100.0f;
    const GraphicsState saved = graphics.Save();
    graphics.TranslateTransform(x, y);
    graphics.RotateTransform(-7.0f);
    GraphicsPath badge;
    AddRoundedRectangle(badge, -205.0f, -48.0f, 410.0f, 96.0f, 14.0f);
    SolidBrush fill(Color(235, 255, 251, 234));
    Pen edge(kMatrixGreen, 6.0f);
    graphics.FillPath(&fill, &badge);
    graphics.DrawPath(&edge, &badge);
    Font font(L"Arial", 23.0f, FontStyleBold, UnitPixel);
    SolidBrush ink(Color(255, 18, 80, 10));
    DrawCenteredText(graphics, L"CLIPBOARD CERTIFIED\n+9999 AURA", font,
                     {-195.0f, -42.0f, 195.0f, 42.0f}, ink);
    graphics.Restore(saved);
  }
}

void OverlayWindow::DrawHud(Graphics& graphics, const RenderState& state) const {
  Font small(L"Segoe UI", 16.0f, FontStyleBold, UnitPixel);
  SolidBrush dark(Color(220, 18, 18, 24));
  SolidBrush light(Color(245, 255, 255, 255));
  GraphicsPath profile;
  AddRoundedRectangle(profile, 12.0f, 12.0f, 178.0f, 34.0f, 10.0f);
  graphics.FillPath(&dark, &profile);
  std::wstring profileText = std::wstring(L"GooseRot · ") + ModeName(state.mode);
  DrawCenteredText(graphics, profileText, small, {12.0f, 12.0f, 190.0f, 46.0f}, light);

  std::wostringstream aura;
  aura << L"AURA  " << state.aura;
  GraphicsPath auraBox;
  AddRoundedRectangle(auraBox, static_cast<float>(width_ - 235), 12.0f, 220.0f, 44.0f, 12.0f);
  SolidBrush auraFill(Color(230, 22, 22, 30));
  Pen auraPen(state.aura >= 0 ? kMatrixGreen : kNeonPink, 3.0f);
  graphics.FillPath(&auraFill, &auraBox);
  graphics.DrawPath(&auraPen, &auraBox);
  Font auraFont(L"Segoe UI", 20.0f, FontStyleBold, UnitPixel);
  DrawCenteredText(graphics, aura.str(), auraFont,
                   {static_cast<float>(width_ - 230), 15.0f, static_cast<float>(width_ - 20), 52.0f}, light);

  if (state.logicalTime >= 90.0 && state.logicalTime < 135.0) {
    constexpr std::array<const wchar_t*, 3> subtitles = {
        L"Tralala la la la...", L"Tutti frutti cappuccina", L"Bombardino crocodilo"};
    const auto index = static_cast<std::size_t>(state.logicalTime / 4.0) % subtitles.size();
    Font subtitleFont(L"Segoe UI", 24.0f, FontStyleBold, UnitPixel);
    GraphicsPath box;
    AddRoundedRectangle(box, width_ * 0.5f - 210.0f, height_ - 102.0f, 420.0f, 48.0f, 12.0f);
    graphics.FillPath(&dark, &box);
    DrawCenteredText(graphics, subtitles[index], subtitleFont,
                     {width_ * 0.5f - 205.0f, height_ - 99.0f,
                      width_ * 0.5f + 205.0f, height_ - 57.0f}, light);
  }

  if (state.finalMonologue) {
    Font criticalFont(L"Impact", 42.0f, FontStyleBold, UnitPixel);
    SolidBrush critical(kCriticalRed);
    DrawCenteredText(graphics, L"CRITICAL ERROR: MAXIMUM BRAINROT REACHED.", criticalFont,
                     {20.0f, height_ * 0.16f, static_cast<float>(width_ - 20), height_ * 0.28f}, critical);
  }

  if (state.countdown) {
    const int remaining = std::clamp(30 - static_cast<int>(state.logicalTime - 270.0), 0, 30);
    wchar_t countdown[16]{};
    swprintf(countdown, std::size(countdown), L"00:%02d", remaining);
    Font countdownFont(L"Consolas", 64.0f, FontStyleBold, UnitPixel);
    SolidBrush critical(kCriticalRed);
    DrawCenteredText(graphics, countdown, countdownFont,
                     {width_ * 0.5f - 180.0f, 20.0f, width_ * 0.5f + 180.0f, 100.0f}, critical);
  }

  if (state.resetButton) {
    const float buttonWidth = 250.0f;
    const float x = width_ * 0.5f - buttonWidth * 0.5f;
    const float y = height_ - 130.0f;
    GraphicsPath button;
    AddRoundedRectangle(button, x, y, buttonWidth, 64.0f, 18.0f);
    SolidBrush fill(kNeonPink);
    Pen edge(Color(255, 255, 255, 255), 4.0f);
    graphics.FillPath(&fill, &button);
    graphics.DrawPath(&edge, &button);
    Font font(L"Segoe UI", 25.0f, FontStyleBold, UnitPixel);
    DrawCenteredText(graphics, L"RESET AURA", font, {x, y, x + buttonWidth, y + 64.0f}, light);
  }

  if (state.emergencyProgress > 0.0f) {
    const float width = std::min(560.0f, width_ - 40.0f);
    const float x = (width_ - width) * 0.5f;
    const float y = height_ - 38.0f;
    SolidBrush track(Color(225, 20, 20, 26));
    SolidBrush progress(kNeonPink);
    graphics.FillRectangle(&track, x, y, width, 22.0f);
    graphics.FillRectangle(&progress, x, y, width * std::clamp(state.emergencyProgress, 0.0f, 1.0f), 22.0f);
    Font font(L"Segoe UI", 14.0f, FontStyleBold, UnitPixel);
    DrawCenteredText(graphics, L"Maintenez Esc pour fermer et restaurer le bureau", font,
                     {x, y - 26.0f, x + width, y - 2.0f}, light);
  }
}

void OverlayWindow::DrawFakeShutdown(Graphics& graphics, const RenderState& state) const {
  const double age = state.logicalTime - 300.0;
  if (state.mode == RunMode::Lab && age < 2.0) {
    SolidBrush blueScreen(Color(255, 18, 92, 171));
    graphics.FillRectangle(&blueScreen, 0, 0, width_, height_);
    Font face(L"Segoe UI", 76.0f, FontStyleRegular, UnitPixel);
    Font title(L"Segoe UI", 28.0f, FontStyleRegular, UnitPixel);
    Font detail(L"Segoe UI", 18.0f, FontStyleRegular, UnitPixel);
    SolidBrush white(Color::White);
    DrawCenteredText(graphics, L":(", face,
                     {20.0f, height_ * 0.22f, static_cast<float>(width_ - 20), height_ * 0.40f}, white);
    DrawCenteredText(graphics, L"MAXIMUM BRAINROT reached a critical aura state.", title,
                     {20.0f, height_ * 0.43f, static_cast<float>(width_ - 20), height_ * 0.56f}, white);
    DrawCenteredText(graphics, L"SIMULATION — aucun crash système n'a été déclenché.", detail,
                     {20.0f, height_ * 0.59f, static_cast<float>(width_ - 20), height_ * 0.68f}, white);
    return;
  }
  SolidBrush background(Color(255, 5, 10, 18));
  graphics.FillRectangle(&background, 0, 0, width_, height_);
  Pen spinner(kNeonPink, 8.0f);
  graphics.DrawArc(&spinner, width_ * 0.5f - 38.0f, height_ * 0.42f - 38.0f, 76.0f, 76.0f,
                   static_cast<float>(age * 220.0), 250.0f);
  Font title(L"Segoe UI", 34.0f, FontStyleRegular, UnitPixel);
  Font detail(L"Segoe UI", 18.0f, FontStyleRegular, UnitPixel);
  SolidBrush white(Color::White);
  DrawCenteredText(graphics, L"Resetting aura...", title,
                   {20.0f, height_ * 0.53f, static_cast<float>(width_ - 20), height_ * 0.63f}, white);
  DrawCenteredText(graphics, L"Simulation sûre — aucune donnée système n'est modifiée", detail,
                   {20.0f, height_ * 0.64f, static_cast<float>(width_ - 20), height_ * 0.71f}, white);
}

void OverlayWindow::Present() {
  if (!window_ || !surfaceDc_) return;
  if (preview_) {
    InvalidateRect(window_, nullptr, FALSE);
    UpdateWindow(window_);
    return;
  }
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
