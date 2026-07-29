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

enum class TimelineEventId {
  PassiveEntrance,
  AuraPrompt,
  NotepadStart,
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

  Vec2 Position() const { return position_; }
  Vec2 Velocity() const { return velocity_; }
  Vec2 Target() const { return target_; }
  float DirectionRadians() const { return directionRadians_; }
  float DistanceToTarget() const { return Distance(position_, target_); }
  SpeedTier Tier() const { return tier_; }
  const GooseRig& Rig() const { return rig_; }
  const GooseParameters& Parameters() const { return parameters_; }

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
  SpeedTier tier_ = SpeedTier::Walk;
  bool extendNeck_ = false;
  GooseRig rig_;
};

RectF ClampWindowRect(RectF window, RectF workArea);

}  // namespace gooserot

