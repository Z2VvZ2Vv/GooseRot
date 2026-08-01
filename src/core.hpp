#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gooserot {

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;

  Vec2 operator+(const Vec2& rhs) const { return {x + rhs.x, y + rhs.y}; }
  Vec2 operator-(const Vec2& rhs) const { return {x - rhs.x, y - rhs.y}; }
  Vec2 operator*(float scalar) const { return {x * scalar, y * scalar}; }
  Vec2 operator/(float scalar) const { return {x / scalar, y / scalar}; }
  Vec2& operator+=(const Vec2& rhs) {
    x += rhs.x;
    y += rhs.y;
    return *this;
  }
};

float Dot(Vec2 a, Vec2 b);
float Length(Vec2 value);
float Distance(Vec2 a, Vec2 b);
Vec2 Normalize(Vec2 value);
Vec2 ClampMagnitude(Vec2 value, float maximum);
Vec2 Lerp(Vec2 from, Vec2 to, float amount);

// Composites a premultiplied ARGB source pixel over a premultiplied destination.
// Kept in the core so the renderer's fast software path remains unit-testable.
inline std::uint32_t BlendPremultipliedArgb(std::uint32_t source, std::uint32_t destination) {
  const unsigned sourceAlpha = source >> 24U;
  if (sourceAlpha == 0U) return destination;
  if (sourceAlpha == 255U) return source;
  const unsigned inverseAlpha = 255U - sourceAlpha;
  const unsigned blue = (source & 0xFFU) +
                        (((destination & 0xFFU) * inverseAlpha + 127U) / 255U);
  const unsigned green = ((source >> 8U) & 0xFFU) +
                         ((((destination >> 8U) & 0xFFU) * inverseAlpha + 127U) / 255U);
  const unsigned red = ((source >> 16U) & 0xFFU) +
                       ((((destination >> 16U) & 0xFFU) * inverseAlpha + 127U) / 255U);
  const unsigned alpha = sourceAlpha +
                         ((((destination >> 24U) & 0xFFU) * inverseAlpha + 127U) / 255U);
  return (blue & 0xFFU) | ((green & 0xFFU) << 8U) | ((red & 0xFFU) << 16U) |
         ((alpha & 0xFFU) << 24U);
}

struct RectF {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;

  float Width() const { return right - left; }
  float Height() const { return bottom - top; }
  Vec2 Center() const { return {(left + right) * 0.5f, (top + bottom) * 0.5f}; }
};

enum class RunMode { Safe, Normal, Lab };

struct AppConfig {
  RunMode mode = RunMode::Safe;
  double startAtSeconds = 0.0;
  double durationScale = 1.0;
  std::uint32_t seed = 67;
  bool primaryMonitorOnly = false;
  bool fakeReboot = false;
  bool bootGame = false;
  bool vmConfirmed = false;
  bool preview = false;
  bool desktopEffects = true;
  bool showHelp = false;
};

bool ParseArguments(int argc, wchar_t** argv, AppConfig& config, std::wstring& error);
bool ParseTimestamp(const std::wstring& value, double& seconds);
const wchar_t* ModeName(RunMode mode);
std::wstring UsageText();

// Every timeline anchor, in logical seconds, in one place. The experience runs
// six minutes: the flock walks off the screen once near the start and comes
// back carrying the first photo, and that beat needs room of its own instead of
// squeezing the later phases.
namespace phase {

constexpr double kEntrance = 0.0;
constexpr double kAuraPrompt = 15.0;
constexpr double kGooseExit = 35.0;
constexpr double kNotepad = 45.0;
constexpr double kGooseReturn = 65.0;
constexpr double kCursorAndWindows = 80.0;
constexpr double kOwnedApps = 95.0;
constexpr double kSubtitles = 110.0;
constexpr double kClipboard = 140.0;
constexpr double kDuplicate = 160.0;
constexpr double kGraffiti = 190.0;
constexpr double kGraffitiDuration = 18.0;
constexpr double kSigma = 216.0;
constexpr double kScreenShake = 240.0;
constexpr double kColorFilter = 270.0;
constexpr double kFinalMonologue = 300.0;
constexpr double kCountdown = 330.0;
constexpr double kCircleDance = 345.0;
constexpr double kResetAura = 358.4;
constexpr double kEnd = 360.0;

// Third-party windows are only ever nudged inside the hijack phase.
constexpr double kWindowHijackEnd = kDuplicate;
// The occasional cursor hunt stops when the closing choreography takes over.
constexpr double kCursorHuntEnd = kFinalMonologue;
// Nothing new is fed to the scene this late; the finale needs a settled stage.
constexpr double kCompanionCutoff = kEnd - 2.0;

}  // namespace phase

enum class TimelineEventId {
  PassiveEntrance,
  AuraPrompt,
  GooseExit,
  NotepadStart,
  GooseReturn,
  CursorAndWindows,
  MemeSubtitles,
  ClipboardBadge,
  Duplicate,
  Graffiti,
  SigmaPrompt,
  ScreenShake,
  ColorFilter,
  FinalMonologue,
  Countdown,
  CircleDance,
  ResetAura,
  Shutdown
};

struct TimelineEvent {
  TimelineEventId id;
  double atSeconds;
  const wchar_t* name;
};

class TimelineEngine {
 public:
  TimelineEngine();

  void Reset(double startAtSeconds = 0.0);
  std::vector<TimelineEvent> Advance(double logicalSeconds);
  const std::vector<TimelineEvent>& Events() const { return events_; }
  double CurrentTime() const { return currentTime_; }

 private:
  std::vector<TimelineEvent> events_;
  std::vector<bool> fired_;
  double currentTime_ = 0.0;
};

enum class SpeedTier { Walk, Run, Charge };

struct GooseRig {
  Vec2 underbodyCenter;
  Vec2 bodyCenter;
  Vec2 tailTip;
  Vec2 neckBase;
  Vec2 neckCenter;
  Vec2 headCenter;
  Vec2 beakTip;
  Vec2 leftEye;
  Vec2 rightEye;
  Vec2 leftFoot;
  Vec2 rightFoot;
};

struct GooseParameters {
  float walkSpeed = 80.0f;
  float runSpeed = 200.0f;
  float chargeSpeed = 400.0f;
  float accelerationNormal = 1300.0f;
  float accelerationCharged = 2300.0f;
  float stepTimeNormal = 0.2f;
  float stepTimeCharged = 0.1f;
};

class GooseEntity {
 public:
  explicit GooseEntity(Vec2 start = {300.0f, 300.0f});

  void SetTarget(Vec2 target, SpeedTier tier = SpeedTier::Walk, bool extendNeck = false);
  void SetPosition(Vec2 position);
  void Update(float deltaSeconds, RectF bounds);

  // Opens the beak and throws honk rings for the requested duration.
  void Honk(float seconds);
  // Latching on the cursor keeps the neck out and the beak clamped shut.
  void SetLatched(bool latched) { latched_ = latched; }
  // Lifts the edge clamp so the goose can walk right out of the frame and come
  // back later. Clearing it while the goose is still outside would teleport it,
  // so callers only clear it once the body is back on the canvas.
  void SetOffstage(bool offstage) { offstage_ = offstage; }

  Vec2 Position() const { return position_; }
  Vec2 Velocity() const { return velocity_; }
  Vec2 Target() const { return target_; }
  float DirectionRadians() const { return directionRadians_; }
  float DistanceToTarget() const { return Distance(position_, target_); }
  SpeedTier Tier() const { return tier_; }
  const GooseRig& Rig() const { return rig_; }
  const GooseParameters& Parameters() const { return parameters_; }

  float NeckExtension() const { return neckExtension_; }
  float BeakOpen() const { return beakOpen_; }
  float WingFlap() const { return wingFlap_; }
  float StepPhase() const { return stepClock_; }
  bool IsHonking() const { return honkTimer_ > 0.0f; }
  bool IsLatched() const { return latched_; }
  bool IsOffstage() const { return offstage_; }
  bool IsAngry() const { return tier_ == SpeedTier::Charge || honkTimer_ > 0.0f; }

  // Converts a desired beak-tip position into the body target that keeps the
  // current procedural rig aligned with it.
  Vec2 BodyTargetForBeak(Vec2 desiredBeakTip) const {
    return desiredBeakTip - (rig_.beakTip - position_);
  }
  float BeakDistanceTo(Vec2 target) const { return Distance(rig_.beakTip, target); }

  static constexpr float kBoundsMargin = 48.0f;
  // How far past the canvas an offstage goose may walk before it is clamped.
  static constexpr float kOffstageMargin = 340.0f;

 private:
  void UpdateRig(float deltaSeconds);
  float MaximumSpeed() const;
  float Acceleration() const;

  GooseParameters parameters_;
  Vec2 position_;
  Vec2 velocity_;
  Vec2 target_;
  float directionRadians_ = 0.0f;
  float neckExtension_ = 0.0f;
  float stepClock_ = 0.0f;
  float flapClock_ = 0.0f;
  float beakOpen_ = 0.0f;
  float wingFlap_ = 0.0f;
  float honkTimer_ = 0.0f;
  SpeedTier tier_ = SpeedTier::Walk;
  bool extendNeck_ = false;
  bool latched_ = false;
  bool offstage_ = false;
  GooseRig rig_;
};

RectF ClampWindowRect(RectF window, RectF workArea);

// Sentinel returned when no goose is available to carry a new prop.
constexpr std::size_t kNoCarrier = static_cast<std::size_t>(-1);

// Prefer an idle follower so the lead goose remains available for cursor and
// window choreography. A lone lead goose may carry only while otherwise idle.
std::size_t PickFreeCarrier(const std::vector<unsigned char>& carrierBusy, bool leadBusy);

// How hard the flock is steering the pointer right now, in [0, 1].
//
// The storm deliberately breathes: every cycle hands the pointer back for a
// stretch, so there is a middle ground between "mine" and "unusable". The
// seized share and the peak both grow with the timeline, but the released
// window never disappears entirely.
float CursorStormEnvelope(double logicalTime);

// How many popups the swarm is allowed to keep alive at `logicalTime`. It grows
// until the final monologue, then the finale trades windows for glitch and eats
// the swarm back down to nothing.
int DesiredPopupCount(double logicalTime, int maximum);

// Where and how big the sprayed 67 is on a given canvas. The renderer draws
// from these, and prop placement keeps clear of them, so the tag can never be
// buried under a pile of brainrot photos.
float TagScale(RectF canvas);
Vec2 TagCenter(RectF canvas);
RectF TagZone(RectF canvas);

// The [x] badge the flock leaves on a delivered photo. Shared so the hit test
// and the drawn badge can never drift apart.
RectF PropCloseBox(Vec2 center, float size);
bool PropCloseBoxHit(Vec2 center, float size, Vec2 point);

}  // namespace gooserot

