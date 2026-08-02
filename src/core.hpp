#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
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
  bool muted = false;
  bool flashesEnabled = true;
  bool reducedMotion = false;
  // Enabled only by explicit consent to the full desktop experience. This is
  // session-local: no registry or shell setting is changed.
  bool blockWindowsKey = false;
  bool showHelp = false;
};

bool ParseArguments(int argc, wchar_t** argv, AppConfig& config, std::wstring& error);
bool ParseTimestamp(const std::wstring& value, double& seconds);
const wchar_t* ModeName(RunMode mode);
std::wstring UsageText();

// Every timeline anchor, in logical seconds, in one place.
//
// The run is one story: after a seeded 10-30 second wall-clock preamble, a goose
// turns up to perform an Aura Inspection of the desktop, opens a case file,
// collects evidence, paints its verdict on the wall and finally condemns the
// machine. The logical story lasts seven and a half minutes, with a deliberate
// opening — the goose arrives, introduces itself and walks a full inspection
// round before anything is scored, so the aura counter appears because the
// inspector started writing rather than because the program started.
namespace phase {

constexpr double kEntrance = 0.0;
constexpr double kIntroduction = 20.0;
constexpr double kInspectionRound = 45.0;
// The goose stamps the desktop and the case file opens under its beak.
constexpr double kNotepad = 75.0;
// First entry in the file: this is when the aura counter appears at all.
constexpr double kAuraPrompt = 100.0;
constexpr double kGooseExit = 125.0;
constexpr double kGooseReturn = 148.0;
constexpr double kCursorAndWindows = 165.0;
constexpr double kOwnedApps = 178.0;
constexpr double kSubtitles = 190.0;
constexpr double kClipboard = 215.0;
constexpr double kDuplicate = 235.0;
constexpr double kGraffiti = 265.0;
constexpr double kGraffitiDuration = 18.0;
constexpr double kSigma = 295.0;
constexpr double kScreenShake = 315.0;
constexpr double kColorFilter = 345.0;
constexpr double kFinalMonologue = 375.0;
constexpr double kCountdown = 410.0;
constexpr double kCircleDance = 430.0;
// The last chance is offered just before the aperture starts closing, so the
// screen visibly shuts on it rather than hiding it behind black.
constexpr double kResetAura = 438.0;
constexpr double kEnd = 450.0;

// Third-party windows are only ever nudged inside the hijack phase.
constexpr double kWindowHijackEnd = kDuplicate;
// The occasional cursor hunt stops when the closing choreography takes over.
constexpr double kCursorHuntEnd = kFinalMonologue;
// Nothing new is fed to the scene this late; the finale needs a settled stage.
constexpr double kCompanionCutoff = kEnd - 2.0;
// The inspector closes the file: the aperture starts shutting on the desktop
// while the exposure is cranked, well before the timeline itself runs out.
constexpr double kIrisStart = kEnd - 9.0;
// Compact GooseRot notices accumulate through the middle of the story, then
// disappear individually during the final countdown.
constexpr double kPopupStart = kGooseReturn;
constexpr double kPopupFull = kDuplicate;
constexpr double kPopupCloseStart = kCountdown;
constexpr double kPopupCloseEnd = kIrisStart;

}  // namespace phase

constexpr std::size_t kMaximumPopups = 100U;

// Number of compact notices that should still be present at a timeline
// position. This pure schedule keeps accelerated/resumed runs predictable.
std::size_t DesiredPopupCount(double logicalTime);

// Time-based cues for the non-destructive display corruption. Real time is
// deliberately separate from timeline time so --duration-scale can never
// compress a run of flashes into a photosensitive burst.
struct ChaosVisualCue {
  float flashIntensity = 0.0f;
  float faultRibbonIntensity = 0.0f;
  std::uint32_t pattern = 0;
};

ChaosVisualCue EvaluateChaosVisualCue(double logicalTime, double realTime,
                                      std::uint32_t seed, bool flashesEnabled);

struct FrameAdvance {
  double wallDelta = 0.0;
  double simulationDelta = 0.0;
  double logicalDelta = 0.0;
  double simulationLogicalDelta = 0.0;
};

// Separates real elapsed time from a bounded physical simulation step. This is
// what keeps Esc, process cadence and the story timeline honest even
// when a rendered frame takes substantially longer than 250 ms.
FrameAdvance EvaluateFrameAdvance(double elapsedSeconds, double durationScale);

// A seeded wall-clock preamble before the logical story starts at kEntrance.
// Keeping it outside the timeline makes the wait independent of durationScale.
double InitialEntranceDelaySeconds(std::uint32_t seed);

enum class TimelineEventId {
  PassiveEntrance,
  Introduction,
  InspectionRound,
  NotepadStart,
  AuraPrompt,
  GooseExit,
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

// Types a script into a buffer the way somebody actually types it, instead of
// dumping whole words at a fixed interval: one character at a time, with jitter
// between keystrokes, a beat after punctuation, a longer one after a line
// break, and the occasional typo that gets noticed a moment later and rubbed
// out. Pure logic, so the cadence is unit-testable without a window.
class Typewriter {
 public:
  void Queue(const std::wstring& text);
  // How fast the typist is going, in characters per second.
  void SetSpeed(double charactersPerSecond);
  // Advances to `now`, appending to and correcting `visible`. Returns true when
  // `visible` changed, so the caller only touches the control when it must.
  bool Advance(double now, std::mt19937& random, std::wstring& visible);
  // True when the whole script has been typed and any typo has been repaired.
  bool Idle() const { return pending_.empty() && typoRemaining_ == 0; }
  std::size_t Remaining() const { return pending_.size(); }
  void Reset(double now);

 private:
  std::wstring pending_;
  double nextKeystrokeAt_ = 0.0;
  double charactersPerSecond_ = 11.0;
  int typoRemaining_ = 0;
  bool started_ = false;
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

