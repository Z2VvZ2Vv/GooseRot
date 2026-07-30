#include "core.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cwchar>
#include <limits>

namespace gooserot {
namespace {

constexpr float kPi = 3.14159265358979323846f;

bool Equals(const std::wstring& left, const wchar_t* right) {
  return _wcsicmp(left.c_str(), right) == 0;
}

bool ParseDouble(const std::wstring& value, double& result) {
  if (value.empty()) return false;
  errno = 0;
  wchar_t* end = nullptr;
  const double parsed = std::wcstod(value.c_str(), &end);
  if (end == value.c_str() || *end != L'\0' || errno == ERANGE || !std::isfinite(parsed)) return false;
  result = parsed;
  return true;
}

float MoveTowards(float current, float target, float maximumDelta) {
  const float difference = target - current;
  if (std::fabs(difference) <= maximumDelta) return target;
  return current + (difference > 0.0f ? maximumDelta : -maximumDelta);
}

}  // namespace

float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

float Length(Vec2 value) { return std::sqrt(Dot(value, value)); }

float Distance(Vec2 a, Vec2 b) { return Length(a - b); }

Vec2 Normalize(Vec2 value) {
  const float length = Length(value);
  return length <= std::numeric_limits<float>::epsilon() ? Vec2{} : value / length;
}

Vec2 ClampMagnitude(Vec2 value, float maximum) {
  const float length = Length(value);
  if (length <= maximum || length <= std::numeric_limits<float>::epsilon()) return value;
  return value * (maximum / length);
}

Vec2 Lerp(Vec2 from, Vec2 to, float amount) {
  const float clamped = std::clamp(amount, 0.0f, 1.0f);
  return from * (1.0f - clamped) + to * clamped;
}

bool ParseTimestamp(const std::wstring& value, double& seconds) {
  const auto separator = value.find(L':');
  if (separator == std::wstring::npos) {
    if (!ParseDouble(value, seconds)) return false;
    return seconds >= 0.0;
  }

  double minutes = 0.0;
  double remainder = 0.0;
  if (!ParseDouble(value.substr(0, separator), minutes) ||
      !ParseDouble(value.substr(separator + 1), remainder)) {
    return false;
  }
  if (minutes < 0.0 || remainder < 0.0 || remainder >= 60.0) return false;
  seconds = minutes * 60.0 + remainder;
  return true;
}

bool ParseArguments(int argc, wchar_t** argv, AppConfig& config, std::wstring& error) {
  auto requireValue = [&](int& index, const wchar_t* option) -> const wchar_t* {
    if (index + 1 >= argc) {
      error = std::wstring(L"Missing value after ") + option;
      return nullptr;
    }
    return argv[++index];
  };

  for (int index = 1; index < argc; ++index) {
    const std::wstring argument(argv[index]);
    if (Equals(argument, L"--help") || Equals(argument, L"-h") || Equals(argument, L"/?")) {
      config.showHelp = true;
    } else if (Equals(argument, L"--mode")) {
      const wchar_t* value = requireValue(index, L"--mode");
      if (!value) return false;
      if (_wcsicmp(value, L"safe") == 0) config.mode = RunMode::Safe;
      else if (_wcsicmp(value, L"normal") == 0) config.mode = RunMode::Normal;
      else if (_wcsicmp(value, L"lab") == 0) config.mode = RunMode::Lab;
      else {
        error = L"Unknown mode. Accepted values: safe, normal, lab.";
        return false;
      }
    } else if (Equals(argument, L"--start-at")) {
      const wchar_t* value = requireValue(index, L"--start-at");
      if (!value || !ParseTimestamp(value, config.startAtSeconds)) {
        if (error.empty()) error = L"--start-at expects MM:SS or a positive number of seconds.";
        return false;
      }
    } else if (Equals(argument, L"--duration-scale")) {
      const wchar_t* value = requireValue(index, L"--duration-scale");
      if (!value || !ParseDouble(value, config.durationScale) || config.durationScale < 0.01 ||
          config.durationScale > 100.0) {
        if (error.empty()) error = L"--duration-scale must be between 0.01 and 100.";
        return false;
      }
    } else if (Equals(argument, L"--seed")) {
      const wchar_t* value = requireValue(index, L"--seed");
      if (!value) return false;
      errno = 0;
      wchar_t* end = nullptr;
      const unsigned long long parsed = std::wcstoull(value, &end, 10);
      if (value[0] == L'+' || value[0] == L'-' || end == value || *end != L'\0' ||
          errno == ERANGE || parsed > std::numeric_limits<std::uint32_t>::max()) {
        error = L"--seed expects an unsigned integer.";
        return false;
      }
      config.seed = static_cast<std::uint32_t>(parsed);
    } else if (Equals(argument, L"--primary-monitor-only")) {
      config.primaryMonitorOnly = true;
    } else if (Equals(argument, L"--fake-reboot")) {
      config.fakeReboot = true;
    } else if (Equals(argument, L"--boot-game")) {
      config.bootGame = true;
    } else if (Equals(argument, L"--vm-confirmed")) {
      config.vmConfirmed = true;
    } else if (Equals(argument, L"--preview")) {
      config.preview = true;
      config.desktopEffects = false;
    } else if (Equals(argument, L"--no-desktop-effects")) {
      config.desktopEffects = false;
    } else {
      error = std::wstring(L"Unknown option: ") + argument;
      return false;
    }
  }

  if (config.mode == RunMode::Lab && !config.vmConfirmed && !config.preview) {
    error = L"Lab mode requires --vm-confirmed (or --preview for rendering without desktop effects).";
    return false;
  }
  if (config.fakeReboot && config.mode != RunMode::Lab) {
    error = L"--fake-reboot launches the AURA 67 Preview and requires --mode lab.";
    return false;
  }
  if (config.bootGame) {
    error = L"--boot-game is reserved for a future release with signed, verifiable UEFI/BIOS "
            L"artifacts. Use --mode lab --fake-reboot for the safe Preview.";
    return false;
  }
  if (config.startAtSeconds > 300.0) {
    error = L"--start-at cannot exceed 05:00.";
    return false;
  }
  return true;
}

const wchar_t* ModeName(RunMode mode) {
  switch (mode) {
    case RunMode::Safe: return L"safe";
    case RunMode::Normal: return L"normal";
    case RunMode::Lab: return L"lab";
  }
  return L"safe";
}

std::wstring UsageText() {
  return LR"(GooseRot - standalone desktop experience

Usage:
  GooseRot.exe [options]

Options:
  --mode safe|normal|lab     Visual profile (safe by default)
  --start-at MM:SS           Start at a specific timeline position
  --duration-scale N         0.1 = timeline runs ten times faster
  --seed N                   Deterministic seed (67 by default)
  --primary-monitor-only     Limit rendering to the primary monitor
  --fake-reboot              Launch GooseBootPreview after the finale (lab)
  --boot-game                Reserved: unavailable without verified firmware artifacts
  --vm-confirmed             Required confirmation for lab mode
  --preview                  960x540 window with no desktop effects
  --no-desktop-effects       Disable cursor and external-window movement
  --help                     Show this help

Every profile keeps the 2-second Esc emergency exit.
BSOD, reboot, clipboard hooks and boot effects are simulated.)";
}

TimelineEngine::TimelineEngine()
    : events_({
          {TimelineEventId::PassiveEntrance, 0.0, L"Passive Entrance"},
          {TimelineEventId::AuraPrompt, 15.0, L"Aura Deduction"},
          {TimelineEventId::NotepadStart, 40.0, L"Auto-Typing"},
          {TimelineEventId::CursorAndWindows, 60.0, L"Cursor & Window Hijack"},
          {TimelineEventId::MemeSubtitles, 90.0, L"Brainrot Subtitles"},
          {TimelineEventId::ClipboardBadge, 120.0, L"Clipboard Certified"},
          {TimelineEventId::Duplicate, 135.0, L"Duplication"},
          {TimelineEventId::Graffiti, 165.0, L"Graffiti & Vibe"},
          {TimelineEventId::SigmaPrompt, 195.0, L"Sigma Trap"},
          {TimelineEventId::ScreenShake, 210.0, L"Screen Shake"},
          {TimelineEventId::ColorFilter, 240.0, L"Color Filter"},
          {TimelineEventId::FinalMonologue, 255.0, L"Final Monologue"},
          {TimelineEventId::Countdown, 270.0, L"Final Countdown"},
          {TimelineEventId::CircleDance, 285.0, L"Circle Dance"},
          {TimelineEventId::ResetAura, 299.0, L"Final Trigger"},
          {TimelineEventId::Shutdown, 300.0, L"Aura Core Explosion"},
      }) {
  Reset();
}

void TimelineEngine::Reset(double startAtSeconds) {
  currentTime_ = std::max(0.0, startAtSeconds);
  fired_.assign(events_.size(), false);
  for (std::size_t index = 0; index < events_.size(); ++index) {
    fired_[index] = events_[index].atSeconds < currentTime_;
  }
}

std::vector<TimelineEvent> TimelineEngine::Advance(double logicalSeconds) {
  currentTime_ = std::max(currentTime_, logicalSeconds);
  std::vector<TimelineEvent> result;
  for (std::size_t index = 0; index < events_.size(); ++index) {
    if (!fired_[index] && events_[index].atSeconds <= currentTime_) {
      fired_[index] = true;
      result.push_back(events_[index]);
    }
  }
  return result;
}

GooseEntity::GooseEntity(Vec2 start) : position_(start), target_(start) { UpdateRig(0.0f); }

void GooseEntity::SetTarget(Vec2 target, SpeedTier tier, bool extendNeck) {
  target_ = target;
  tier_ = tier;
  extendNeck_ = extendNeck;
}

void GooseEntity::SetPosition(Vec2 position) {
  position_ = position;
  target_ = position;
  velocity_ = {};
  UpdateRig(0.0f);
}

float GooseEntity::MaximumSpeed() const {
  switch (tier_) {
    case SpeedTier::Walk: return parameters_.walkSpeed;
    case SpeedTier::Run: return parameters_.runSpeed;
    case SpeedTier::Charge: return parameters_.chargeSpeed;
  }
  return parameters_.walkSpeed;
}

float GooseEntity::Acceleration() const {
  return tier_ == SpeedTier::Charge ? parameters_.accelerationCharged
                                    : parameters_.accelerationNormal;
}

void GooseEntity::Honk(float seconds) { honkTimer_ = std::max(honkTimer_, seconds); }

void GooseEntity::Update(float deltaSeconds, RectF bounds) {
  const float dt = std::clamp(deltaSeconds, 0.0f, 0.1f);
  const Vec2 toTarget = target_ - position_;
  const float distance = Length(toTarget);
  const float maxSpeed = MaximumSpeed();
  const float brakingSpeed = std::sqrt(std::max(0.0f, 2.0f * Acceleration() * distance));
  const float desiredSpeed = std::min(maxSpeed, brakingSpeed);
  const Vec2 desiredVelocity = distance > 1.0f ? Normalize(toTarget) * desiredSpeed : Vec2{};
  const Vec2 velocityDelta = ClampMagnitude(desiredVelocity - velocity_, Acceleration() * dt);
  velocity_ += velocityDelta;
  if (distance < 0.75f && Length(velocity_) < 8.0f) {
    position_ = target_;
    velocity_ = {};
  } else {
    position_ += velocity_ * dt;
  }

  constexpr float margin = kBoundsMargin;
  if (bounds.Width() <= margin * 2.0f) position_.x = bounds.Center().x;
  else position_.x = std::clamp(position_.x, bounds.left + margin, bounds.right - margin);
  if (bounds.Height() <= margin * 2.0f) position_.y = bounds.Center().y;
  else position_.y = std::clamp(position_.y, bounds.top + margin, bounds.bottom - margin);
  if (Length(velocity_) > 2.0f) directionRadians_ = std::atan2(velocity_.y, velocity_.x);
  UpdateRig(dt);
}

void GooseEntity::UpdateRig(float deltaSeconds) {
  const Vec2 forward{std::cos(directionRadians_), std::sin(directionRadians_)};
  const Vec2 side{-forward.y, forward.x};
  const float desiredExtension =
      (extendNeck_ || latched_ || tier_ != SpeedTier::Walk) ? 1.0f : 0.0f;
  neckExtension_ = MoveTowards(neckExtension_, desiredExtension, deltaSeconds * 5.5f);

  honkTimer_ = std::max(0.0f, honkTimer_ - deltaSeconds);
  const float desiredBeak = latched_          ? 0.0f
                            : honkTimer_ > 0.0f ? 1.0f
                            : tier_ == SpeedTier::Charge ? 0.35f
                                                         : 0.0f;
  beakOpen_ = MoveTowards(beakOpen_, desiredBeak, deltaSeconds * 9.0f);

  const float flapAmplitude = tier_ == SpeedTier::Charge ? 0.55f
                              : tier_ == SpeedTier::Run  ? 0.26f
                                                         : 0.06f;
  flapClock_ += deltaSeconds * (tier_ == SpeedTier::Charge ? 26.0f : 12.0f);
  wingFlap_ = std::sin(flapClock_) * flapAmplitude;

  const float interval = tier_ == SpeedTier::Charge ? parameters_.stepTimeCharged
                                                     : parameters_.stepTimeNormal;
  stepClock_ += deltaSeconds * (2.0f * kPi / std::max(0.05f, interval * 2.0f));

  // Top-down layout: the body spans roughly [-31, +30] along `forward` and
  // ±21 along `side`, so the neck, tail and feet all clear that silhouette.
  rig_.underbodyCenter = position_ - forward * 10.0f;
  rig_.bodyCenter = position_ - forward * 2.0f;
  rig_.tailTip = position_ - forward * 44.0f;
  rig_.neckBase = position_ + forward * 16.0f;
  const float sway = std::sin(stepClock_ * 0.5f) * 3.0f * (1.0f - neckExtension_ * 0.6f);
  rig_.neckCenter = rig_.neckBase + forward * (12.0f + 15.0f * neckExtension_) + side * sway;
  rig_.headCenter = rig_.neckCenter + forward * (15.0f + 7.0f * neckExtension_);
  rig_.beakTip = rig_.headCenter + forward * 24.0f;
  rig_.leftEye = rig_.headCenter + forward * 2.5f - side * 6.5f;
  rig_.rightEye = rig_.headCenter + forward * 2.5f + side * 6.5f;

  const float stride = std::min(9.0f, Length(velocity_) * 0.04f);
  rig_.leftFoot = position_ - forward * 8.0f - side * 22.0f + forward * (std::sin(stepClock_) * stride);
  rig_.rightFoot = position_ - forward * 8.0f + side * 22.0f + forward * (std::sin(stepClock_ + kPi) * stride);
}

std::size_t PickFreeCarrier(const std::vector<unsigned char>& carrierBusy, bool leadBusy) {
  if (carrierBusy.empty()) return kNoCarrier;
  for (std::size_t index = 1; index < carrierBusy.size(); ++index) {
    if (!carrierBusy[index]) return index;
  }
  if (carrierBusy.size() == 1 && !carrierBusy[0] && !leadBusy) return 0;
  return kNoCarrier;
}

RectF ClampWindowRect(RectF window, RectF workArea) {
  const float width = std::min(window.Width(), workArea.Width());
  const float height = std::min(window.Height(), workArea.Height());
  const float maximumLeft = workArea.right - width;
  const float maximumTop = workArea.bottom - height;
  const float left = std::clamp(window.left, workArea.left, maximumLeft);
  const float top = std::clamp(window.top, workArea.top, maximumTop);
  return {left, top, left + width, top + height};
}

}  // namespace gooserot
