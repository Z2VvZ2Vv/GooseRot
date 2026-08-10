#include "core.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cwchar>
#include <limits>
#include <utility>

namespace gooserot {
namespace {

constexpr float kPi = 3.14159265358979323846f;

bool Equals(const std::wstring& left, const wchar_t* right) {
  return _wcsicmp(left.c_str(), right) == 0;
}

std::uint32_t Hash32(std::uint32_t value) {
  value ^= value >> 16U;
  value *= 0x7feb352dU;
  value ^= value >> 15U;
  value *= 0x846ca68bU;
  value ^= value >> 16U;
  return value;
}

float HashUnit(std::uint32_t value) {
  return static_cast<float>(Hash32(value) & 0xFFFFFFU) /
         static_cast<float>(0x1000000U);
}

double SmoothStep(double value) {
  const double clamped = std::clamp(value, 0.0, 1.0);
  return clamped * clamped * (3.0 - 2.0 * clamped);
}

template <std::size_t Size>
double InterpolateKeyframes(
    double logicalTime,
    const std::array<std::pair<double, double>, Size>& keyframes) {
  if (!std::isfinite(logicalTime) || keyframes.empty() ||
      logicalTime < keyframes.front().first) {
    return keyframes.empty() ? 0.0 : keyframes.front().second;
  }
  for (std::size_t index = 1; index < keyframes.size(); ++index) {
    if (logicalTime > keyframes[index].first) continue;
    const auto& previous = keyframes[index - 1U];
    const auto& next = keyframes[index];
    const double span = std::max(0.001, next.first - previous.first);
    const double progress = SmoothStep((logicalTime - previous.first) / span);
    return previous.second + (next.second - previous.second) * progress;
  }
  return keyframes.back().second;
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

PerformanceTier ClassifyPerformance(std::uint64_t physicalMemoryBytes,
                                    unsigned logicalProcessorCount,
                                    std::uint64_t renderPixels) {
  constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
  if (physicalMemoryBytes == 0U || logicalProcessorCount == 0U) {
    return PerformanceTier::Medium;
  }
  // GooseRot composites a full 32-bit software surface. A very large virtual
  // desktop is therefore a real part of the hardware budget, even on a CPU
  // with plenty of threads.
  if (physicalMemoryBytes < 6ULL * kGiB || logicalProcessorCount <= 2U ||
      renderPixels > 12'000'000ULL) {
    return PerformanceTier::Low;
  }
  if (physicalMemoryBytes < 16ULL * kGiB || logicalProcessorCount < 8U ||
      renderPixels > 5'000'000ULL) {
    return PerformanceTier::Medium;
  }
  return PerformanceTier::High;
}

float ResponsiveLayoutScale(RectF canvas) {
  if (!std::isfinite(canvas.Width()) || !std::isfinite(canvas.Height()) ||
      canvas.Width() <= 0.0f || canvas.Height() <= 0.0f) {
    return 1.0f;
  }
  constexpr float kReferenceWidth = 1920.0f;
  constexpr float kReferenceHeight = 1080.0f;
  const float shortEdgeRatio = std::min(canvas.Width() / kReferenceWidth,
                                        canvas.Height() / kReferenceHeight);
  return std::clamp(std::sqrt(std::max(0.0f, shortEdgeRatio)), 0.78f, 1.50f);
}

ExperienceBudget ComputeExperienceBudget(RectF canvas, RunMode mode,
                                         PerformanceTier performance) {
  ExperienceBudget budget;
  budget.layoutScale = ResponsiveLayoutScale(canvas);

  const auto safeDimension = [](float value) {
    return std::isfinite(value) && value > 0.0f ? static_cast<double>(value) : 1.0;
  };
  const double width = safeDimension(canvas.Width());
  const double height = safeDimension(canvas.Height());
  const double area = width * height;
  const double scaleSquared = static_cast<double>(budget.layoutScale) *
                              static_cast<double>(budget.layoutScale);
  const double tierFactor = performance == PerformanceTier::High     ? 1.0
                            : performance == PerformanceTier::Medium ? 0.62
                                                                      : 0.38;

  // About a quarter of the usable wall may be occupied by photos at the peak.
  // Using the scaled average footprint keeps density stable from a small
  // preview to 4K instead of merely multiplying the old fixed ceiling.
  constexpr double kAverageImagePixels = 150.0 * 150.0;
  const double imageSlots = (area * 0.25) / (kAverageImagePixels * scaleSquared);
  const std::size_t narrativeImageCeiling = mode == RunMode::Lab ? 48U : 36U;
  const std::size_t hardwareImageCeiling =
      performance == PerformanceTier::High
          ? narrativeImageCeiling
          : performance == PerformanceTier::Medium
                ? (narrativeImageCeiling * 2U) / 3U
                : (narrativeImageCeiling * 2U) / 5U;
  budget.imageLimit = std::clamp<std::size_t>(
      static_cast<std::size_t>(std::floor(imageSlots * tierFactor)), 2U,
      std::max<std::size_t>(2U, hardwareImageCeiling));

  constexpr double kPopupPixels = 348.0 * 186.0;
  const double popupSlots = area / (kPopupPixels * scaleSquared);
  const double popupDensity = performance == PerformanceTier::High     ? 0.78
                              : performance == PerformanceTier::Medium ? 0.58
                                                                        : 0.40;
  const std::size_t hardwarePopupCeiling =
      performance == PerformanceTier::High     ? kMaximumPopups
      : performance == PerformanceTier::Medium ? 60U
                                                : 32U;
  budget.popupLimit = std::clamp<std::size_t>(
      static_cast<std::size_t>(std::floor(popupSlots * popupDensity)), 1U,
      hardwarePopupCeiling);
  budget.detailedGooseLimit = performance == PerformanceTier::High     ? 3U
                              : performance == PerformanceTier::Medium ? 2U
                                                                        : 1U;
  const double gooseTierFactor = performance == PerformanceTier::High     ? 1.0
                                 : performance == PerformanceTier::Medium ? 0.72
                                                                           : 0.46;
  constexpr double kAverageGoosePixels = 100.0 * 100.0;
  const double gooseSlots = (area * 0.16) / (kAverageGoosePixels * scaleSquared);
  const std::size_t hardwareGooseCeiling =
      performance == PerformanceTier::High     ? 67U
      : performance == PerformanceTier::Medium ? 40U
                                                : 24U;
  budget.gooseLimit = std::clamp<std::size_t>(
      static_cast<std::size_t>(std::floor(gooseSlots * gooseTierFactor)), 3U,
      hardwareGooseCeiling);
  return budget;
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
      RunMode requested = config.mode;
      if (_wcsicmp(value, L"safe") == 0) requested = RunMode::Safe;
      else if (_wcsicmp(value, L"normal") == 0) requested = RunMode::Normal;
      else if (_wcsicmp(value, L"lab") == 0) requested = RunMode::Lab;
      else {
        error = L"Unknown mode. Accepted values: safe, normal, lab.";
        return false;
      }
      if (config.modeLocked && requested != config.mode) {
        error = std::wstring(L"This executable is locked to the ") +
                ModeName(config.mode) + L" profile.";
        return false;
      }
      config.mode = requested;
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
    } else if (Equals(argument, L"--mute")) {
      config.muted = true;
    } else if (Equals(argument, L"--no-flashes")) {
      config.flashesEnabled = false;
    } else if (Equals(argument, L"--reduced-motion")) {
      config.reducedMotion = true;
      config.flashesEnabled = false;
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
  if (config.startAtSeconds > phase::kEnd) {
    error = L"--start-at cannot exceed 07:30.";
    return false;
  }
  if (config.preview) {
    // Preview is safe to launch from a build/test command without surprising
    // the operator with sound or a full-frame flash.
    config.muted = true;
    config.flashesEnabled = false;
    config.reducedMotion = true;
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

bool EmergencyExitEnabled(RunMode mode) {
  return mode != RunMode::Normal;
}

bool ForcedRebootAfterFarewell(RunMode mode, bool preview) {
  return mode == RunMode::Normal && !preview;
}

std::wstring UsageText() {
  return LR"(GooseRot - standalone desktop experience

Usage:
  GooseRot-Safe.exe [options]
  GooseRot-Normal.exe [options]
  GooseRot-Lab.exe [options]

Options:
  --mode safe|normal|lab     Profile selector for shared/development builds
  --start-at MM:SS           Start at a specific timeline position
  --duration-scale N         0.1 = timeline runs ten times faster
  --seed N                   Deterministic seed (67 by default)
  --primary-monitor-only     Limit rendering to the primary monitor
  --fake-reboot              Launch GooseBootPreview after the finale (lab)
  --boot-game                Reserved: unavailable without verified firmware artifacts
  --vm-confirmed             Development-build compatibility flag
  --preview                  Responsive window with no desktop effects
  --no-desktop-effects       Disable cursor and external-window movement
  --mute                     Disable system-style alert sounds
  --no-flashes               Disable full-frame flash pulses
  --reduced-motion           Disable flashes, shake and continuous cursor storm
  --help                     Show this help

The Normal profile does not expose the 2-second Esc emergency exit and requests
an immediate forced Windows reboot after the farewell. Preview never reboots.
BSOD, clipboard hooks and boot effects are simulated.)";
}

ChaosVisualCue EvaluateChaosVisualCue(double logicalTime, double realTime,
                                      std::uint32_t seed, bool flashesEnabled) {
  ChaosVisualCue cue;
  if (!std::isfinite(logicalTime) || !std::isfinite(realTime) ||
      logicalTime < phase::kOwnedApps) {
    return cue;
  }

  const double safeRealTime = std::max(0.0, realTime);
  const float escalation = static_cast<float>(SmoothStep(
      (logicalTime - phase::kOwnedApps) / (phase::kEnd - phase::kOwnedApps)));
  const float ribbonOnset = static_cast<float>(SmoothStep(
      (logicalTime - phase::kOwnedApps) / 20.0));
  // Early damage changes slowly enough to read as one developing fault. The
  // pattern rate accelerates with the story instead of starting at 12 Hz.
  const double patternRate = 2.0 + static_cast<double>(escalation) * 10.0;
  const std::uint32_t visualStep = static_cast<std::uint32_t>(
      std::fmod(std::floor(safeRealTime * patternRate), 4294967295.0));
  cue.pattern = Hash32(seed ^ 0xA67F29C3U ^ visualStep * 0x9E3779B9U);
  cue.faultRibbonIntensity = ribbonOnset * std::clamp(
      (0.05f + 0.95f * std::pow(escalation, 1.08f)) *
          (0.72f + HashUnit(cue.pattern ^ 0xD15EA5EU) * 0.28f),
      0.0f, 1.0f);

  if (!flashesEnabled || logicalTime < phase::kDuplicate) return cue;

  // At most one pulse per 720 ms slot (1.39 Hz), with a short, bounded duty
  // cycle. The activation probability rises toward the finale, but the real
  // time ceiling remains unchanged even when the timeline is accelerated.
  constexpr double kFlashSlotSeconds = 0.72;
  constexpr double kFlashDurationSeconds = 0.11;
  const std::uint32_t slot = static_cast<std::uint32_t>(
      std::fmod(std::floor(safeRealTime / kFlashSlotSeconds), 4294967295.0));
  const std::uint32_t slotPattern = Hash32(seed ^ 0xF1A567U ^ slot * 0x85EBCA6BU);
  const double withinSlot = std::fmod(safeRealTime, kFlashSlotSeconds);
  const double pulseStart = 0.08 + static_cast<double>(HashUnit(slotPattern)) * 0.20;
  const float flashOnset = static_cast<float>(SmoothStep(
      (logicalTime - phase::kDuplicate) / 20.0));
  const float activationThreshold = 0.72f - escalation * 0.62f;
  if (HashUnit(slotPattern ^ 0xBADC0DEU) >= activationThreshold &&
      withinSlot >= pulseStart && withinSlot < pulseStart + kFlashDurationSeconds) {
    const double phase = (withinSlot - pulseStart) / kFlashDurationSeconds;
    const float envelope = static_cast<float>(
        std::sin(phase * 3.14159265358979323846));
    cue.flashIntensity = std::clamp(flashOnset * (0.28f + escalation * 0.72f) * envelope,
                                    0.0f, 1.0f);
    cue.pattern = slotPattern;
  }
  return cue;
}

FrameAdvance EvaluateFrameAdvance(double elapsedSeconds, double durationScale) {
  FrameAdvance advance;
  if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0 ||
      !std::isfinite(durationScale) || durationScale <= 0.0) {
    return advance;
  }
  advance.wallDelta = elapsedSeconds;
  advance.simulationDelta = std::min(elapsedSeconds, 0.25);
  advance.logicalDelta = elapsedSeconds / durationScale;
  advance.simulationLogicalDelta = advance.simulationDelta / durationScale;
  return advance;
}

double InitialEntranceDelaySeconds(std::uint32_t seed) {
  constexpr double kMinimumDelaySeconds = 10.0;
  constexpr double kDelayRangeSeconds = 20.0;
  return kMinimumDelaySeconds +
         kDelayRangeSeconds * static_cast<double>(HashUnit(seed ^ 0x67A11CEU));
}

std::size_t DesiredPopupCount(double logicalTime) {
  if (!std::isfinite(logicalTime) || logicalTime < phase::kPopupStart) return 0U;
  if (logicalTime < phase::kPopupFull) {
    // Story beats, not a single linear flood. The first notice arrives almost
    // alone; density then builds in readable steps until the countdown briefly
    // reaches the administrative cap.
    constexpr std::array<std::pair<double, double>, 9> kGrowth = {{
        {phase::kPopupStart, 0.0},
        {phase::kSubtitles, 1.0},
        {phase::kClipboard, 5.0},
        {phase::kDuplicate, 12.0},
        {phase::kGraffiti, 24.0},
        {phase::kScreenShake, 42.0},
        {phase::kColorFilter, 60.0},
        {phase::kFinalMonologue, 80.0},
        {phase::kPopupFull, static_cast<double>(kMaximumPopups)},
    }};
    return std::min(
        kMaximumPopups,
        static_cast<std::size_t>(std::floor(InterpolateKeyframes(logicalTime, kGrowth))));
  }
  if (logicalTime < phase::kPopupCloseStart) return kMaximumPopups;
  if (logicalTime < phase::kPopupCloseEnd) {
    const double progress = std::clamp(
        (logicalTime - phase::kPopupCloseStart) /
            (phase::kPopupCloseEnd - phase::kPopupCloseStart),
        0.0, 1.0);
    const std::size_t closed = static_cast<std::size_t>(
        std::floor(progress * static_cast<double>(kMaximumPopups)));
    return kMaximumPopups - std::min(closed, kMaximumPopups);
  }
  return 0U;
}

float BaselineGlitchIntensity(double logicalTime) {
  if (!std::isfinite(logicalTime) || logicalTime <= phase::kNotepad) return 0.0f;
  constexpr std::array<std::pair<double, double>, 10> kGlitch = {{
      {phase::kNotepad, 0.00},
      {phase::kAuraPrompt, 0.02},
      {phase::kGooseReturn, 0.04},
      {phase::kSubtitles, 0.07},
      {phase::kDuplicate, 0.13},
      {phase::kGraffiti, 0.22},
      {phase::kScreenShake, 0.36},
      {phase::kColorFilter, 0.52},
      {phase::kFinalMonologue, 0.68},
      {phase::kEnd, 1.00},
  }};
  return static_cast<float>(std::clamp(
      InterpolateKeyframes(logicalTime, kGlitch), 0.0, 1.0));
}

std::size_t DesiredFlockSize(double logicalTime, int bonusGeese) {
  constexpr std::size_t kMaximumFlock = 67U;
  const std::size_t bonus = static_cast<std::size_t>(std::clamp(bonusGeese, 0, 6));
  if (!std::isfinite(logicalTime)) return 1U;
  if (logicalTime < phase::kDuplicate) return std::min(kMaximumFlock, 1U + bonus);
  if (logicalTime < phase::kScreenShake) return std::min(kMaximumFlock, 3U + bonus);
  const double progress = SmoothStep(
      (logicalTime - phase::kScreenShake) / (phase::kResetAura - phase::kScreenShake));
  const std::size_t scripted =
      3U + static_cast<std::size_t>(std::floor(progress * 64.0));
  return std::min(kMaximumFlock, scripted + bonus);
}

TimelineEngine::TimelineEngine()
    : events_({
          {TimelineEventId::PassiveEntrance, phase::kEntrance, L"The Inspector Arrives"},
          {TimelineEventId::Introduction, phase::kIntroduction, L"Credentials"},
          {TimelineEventId::InspectionRound, phase::kInspectionRound, L"Inspection Round"},
          {TimelineEventId::NotepadStart, phase::kNotepad, L"The Case File Opens"},
          {TimelineEventId::AuraPrompt, phase::kAuraPrompt, L"First Deduction"},
          {TimelineEventId::GooseExit, phase::kGooseExit, L"Off To Fetch Evidence"},
          {TimelineEventId::GooseReturn, phase::kGooseReturn, L"Evidence Delivered"},
          {TimelineEventId::CursorAndWindows, phase::kCursorAndWindows, L"Cursor & Window Hijack"},
          {TimelineEventId::MemeSubtitles, phase::kSubtitles, L"Brainrot Subtitles"},
          {TimelineEventId::ClipboardBadge, phase::kClipboard, L"Clipboard Certified"},
          {TimelineEventId::Duplicate, phase::kDuplicate, L"Backup Inspectors"},
          {TimelineEventId::Graffiti, phase::kGraffiti, L"The Score On The Wall"},
          {TimelineEventId::SigmaPrompt, phase::kSigma, L"Right Of Appeal"},
          {TimelineEventId::ScreenShake, phase::kScreenShake, L"Screen Shake"},
          {TimelineEventId::ColorFilter, phase::kColorFilter, L"Color Filter"},
          {TimelineEventId::FinalMonologue, phase::kFinalMonologue, L"The Verdict"},
          {TimelineEventId::Countdown, phase::kCountdown, L"Final Countdown"},
          {TimelineEventId::CircleDance, phase::kCircleDance, L"Circle Dance"},
          {TimelineEventId::ResetAura, phase::kResetAura, L"Final Trigger"},
          {TimelineEventId::Shutdown, phase::kEnd, L"The File Is Closed"},
      }) {
  Reset();
}

void TimelineEngine::Reset(double startAtSeconds) {
  // Negative logical time is the quiet preamble before the first event. CLI
  // start positions remain non-negative, but the app deliberately starts below
  // zero so the entrance event cannot fire before its wall-clock delay.
  currentTime_ = std::isfinite(startAtSeconds) ? startAtSeconds : 0.0;
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

void GooseEntity::SetVisualScale(float scale) {
  visualScale_ = std::clamp(scale, 0.65f, 1.75f);
  parameters_ = GooseParameters{};
  parameters_.walkSpeed *= visualScale_;
  parameters_.runSpeed *= visualScale_;
  parameters_.chargeSpeed *= visualScale_;
  parameters_.accelerationNormal *= visualScale_;
  parameters_.accelerationCharged *= visualScale_;
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

  // An offstage goose is allowed well past the edge so it can leave the frame
  // entirely; a negative margin turns the clamp into an outer safety rail.
  const float margin = offstage_ ? -kOffstageMargin * visualScale_
                                 : kBoundsMargin * visualScale_;
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

float CursorStormEnvelope(double logicalTime) {
  if (!std::isfinite(logicalTime) || logicalTime < phase::kScreenShake) return 0.0f;
  const double since = logicalTime - phase::kScreenShake;
  const float ramp = static_cast<float>(
      std::clamp(since / (phase::kCountdown - phase::kScreenShake), 0.0, 1.0));

  // One cycle of "the flock has it" followed by "you have it back". The seized
  // share grows from about a third to about two thirds of the cycle, so the
  // pointer becomes hard to fight without ever becoming permanently dead.
  constexpr double kCycleSeconds = 7.5;
  const double position = std::fmod(since, kCycleSeconds) / kCycleSeconds;
  const double seizedShare = 0.34 + 0.30 * static_cast<double>(ramp);
  if (position >= seizedShare) return 0.0f;

  const float progress = static_cast<float>(position / seizedShare);
  const float shape = 0.5f - 0.5f * std::cos(progress * 2.0f * kPi);
  return std::clamp(shape * (0.40f + 0.60f * ramp), 0.0f, 1.0f);
}

void Typewriter::Queue(const std::wstring& text) { pending_ += text; }

void Typewriter::SetSpeed(double charactersPerSecond) {
  charactersPerSecond_ = std::clamp(charactersPerSecond, 1.0, 90.0);
}

void Typewriter::Reset(double now) {
  pending_.clear();
  typoRemaining_ = 0;
  nextKeystrokeAt_ = now;
  started_ = true;
}

bool Typewriter::Advance(double now, std::mt19937& random, std::wstring& visible) {
  if (!started_) {
    started_ = true;
    nextKeystrokeAt_ = now;
  }
  // A long stall must not be repaid as one instantaneous paragraph. The budget
  // counts characters actually put on screen, including the ones a typo adds,
  // so a burst of corrections cannot smuggle a whole line through.
  constexpr int kMaximumKeystrokesPerAdvance = 24;
  int budget = kMaximumKeystrokesPerAdvance;
  const double base = 1.0 / charactersPerSecond_;
  std::uniform_real_distribution<double> jitter(0.55, 1.7);
  std::uniform_int_distribution<int> chance(0, 999);

  bool changed = false;
  while (budget > 0 && now >= nextKeystrokeAt_) {
    if (typoRemaining_ > 0) {
      // Noticed. Rubbing out is quicker and steadier than typing.
      if (!visible.empty()) visible.pop_back();
      --typoRemaining_;
      --budget;
      nextKeystrokeAt_ += base * 0.55;
      // A short breath before picking the sentence back up.
      if (typoRemaining_ == 0) nextKeystrokeAt_ += base * 3.0;
      changed = true;
      continue;
    }
    if (pending_.empty()) {
      nextKeystrokeAt_ = now;
      break;
    }

    const wchar_t character = pending_.front();
    // Fumble only mid-word, so the correction reads as a slip rather than as
    // the typist forgetting how sentences start.
    const bool typoCandidate = (character >= L'a' && character <= L'z') && !visible.empty() &&
                               visible.back() != L' ' && visible.back() != L'\n';
    if (typoCandidate && chance(random) < 22) {
      static constexpr wchar_t kNearbyKeys[] = L"qwertyuiopasdfghjklzxcvbnm";
      std::uniform_int_distribution<std::size_t> key(0, std::size(kNearbyKeys) - 2U);
      std::uniform_int_distribution<int> length(1, 3);
      // The per-frame budget remains a hard ceiling even when a
      // multi-character fumble lands on its final available slot.
      typoRemaining_ = std::min(length(random), budget);
      for (int index = 0; index < typoRemaining_; ++index) {
        visible += kNearbyKeys[key(random)];
      }
      budget -= typoRemaining_;
      // Keep going for a moment before the mistake registers.
      nextKeystrokeAt_ += base * (1.0 + static_cast<double>(typoRemaining_) * 0.8);
      changed = true;
      continue;
    }

    visible += character;
    pending_.erase(pending_.begin());
    --budget;
    changed = true;

    double delay = base * jitter(random);
    switch (character) {
      case L'\n': delay += base * 9.0; break;
      case L'.':
      case L'!':
      case L'?': delay += base * 6.0; break;
      case L',':
      case L';':
      case L':': delay += base * 3.0; break;
      case L' ': delay += base * 0.6; break;
      default: break;
    }
    nextKeystrokeAt_ += delay;
  }
  if (nextKeystrokeAt_ < now) nextKeystrokeAt_ = now;
  return changed;
}

float TagScale(RectF canvas) {
  return std::max(70.0f, std::min(canvas.Height() * 0.30f, canvas.Width() * 0.20f));
}

Vec2 TagCenter(RectF canvas) {
  return {canvas.left + canvas.Width() * 0.5f, canvas.top + canvas.Height() * 0.47f};
}

RectF TagZone(RectF canvas) {
  const float scale = TagScale(canvas);
  const Vec2 center = TagCenter(canvas);
  // Covers the widest stroke extents plus the paint band, so nothing lands on
  // the glyph itself.
  const float padding = scale * 0.36f;
  return {center.x - scale * 1.08f - padding, center.y - scale * 0.98f - padding,
          center.x + scale * 1.14f + padding, center.y + scale * 0.98f + padding};
}

RectF PropCloseBox(Vec2 center, float size) {
  const float half = size * 0.5f;
  const float box = std::clamp(size * 0.23f, 24.0f, 36.0f);
  // Tucked just inside the top-right corner, so the badge never floats off the
  // photo it belongs to.
  const Vec2 anchor{center.x + half - box * 0.55f, center.y - half + box * 0.55f};
  return {anchor.x - box * 0.5f, anchor.y - box * 0.5f, anchor.x + box * 0.5f,
          anchor.y + box * 0.5f};
}

bool PropCloseBoxHit(Vec2 center, float size, Vec2 point) {
  const RectF box = PropCloseBox(center, size);
  // A couple of pixels of slack: the pointer is being shoved around by geese.
  constexpr float kSlack = 4.0f;
  return point.x >= box.left - kSlack && point.x <= box.right + kSlack &&
         point.y >= box.top - kSlack && point.y <= box.bottom + kSlack;
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
