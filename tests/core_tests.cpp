#include "core.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void TestTimestampParsing() {
  double value = 0.0;
  Expect(gooserot::ParseTimestamp(L"04:30", value) && value == 270.0, "MM:SS parsing");
  Expect(gooserot::ParseTimestamp(L"67.5", value) && value == 67.5, "seconds parsing");
  Expect(!gooserot::ParseTimestamp(L"1:99", value), "reject invalid seconds component");
  Expect(!gooserot::ParseTimestamp(L"-1", value), "reject negative timestamp");
}

void TestArgumentParsing() {
  wchar_t program[] = L"GooseRot.exe";
  wchar_t mode[] = L"--mode";
  wchar_t lab[] = L"lab";
  wchar_t confirmed[] = L"--vm-confirmed";
  wchar_t scale[] = L"--duration-scale";
  wchar_t tenth[] = L"0.1";
  wchar_t start[] = L"--start-at";
  wchar_t timestamp[] = L"02:15";
  wchar_t* valid[] = {program, mode, lab, confirmed, scale, tenth, start, timestamp};
  gooserot::AppConfig config;
  std::wstring error;
  Expect(gooserot::ParseArguments(8, valid, config, error), "valid CLI parses");
  Expect(config.mode == gooserot::RunMode::Lab && config.vmConfirmed, "lab confirmation retained");
  Expect(config.durationScale == 0.1 && config.startAtSeconds == 135.0, "CLI numeric options retained");
  Expect(config.desktopEffects && !config.muted && config.flashesEnabled && !config.reducedMotion,
         "lab full experience enables every effect category by default");

  wchar_t safe[] = L"safe";
  wchar_t normal[] = L"normal";
  wchar_t* safeMode[] = {program, mode, safe};
  wchar_t* normalMode[] = {program, mode, normal};
  config = {};
  error.clear();
  Expect(gooserot::ParseArguments(3, safeMode, config, error) && config.desktopEffects &&
             !config.muted && config.flashesEnabled && !config.reducedMotion,
         "safe full experience enables every effect category by default");
  config = {};
  error.clear();
  Expect(gooserot::ParseArguments(3, normalMode, config, error) && config.desktopEffects &&
             !config.muted && config.flashesEnabled && !config.reducedMotion,
         "normal full experience enables every effect category by default");

  wchar_t seed[] = L"--seed";
  wchar_t negativeSeed[] = L"-1";
  wchar_t plusSeed[] = L"+67";
  wchar_t overflowSeed[] = L"4294967296";
  wchar_t* negativeSeedArgs[] = {program, seed, negativeSeed};
  wchar_t* plusSeedArgs[] = {program, seed, plusSeed};
  wchar_t* overflowSeedArgs[] = {program, seed, overflowSeed};
  config = {};
  error.clear();
  Expect(!gooserot::ParseArguments(3, negativeSeedArgs, config, error), "negative seed is rejected");
  config = {};
  error.clear();
  Expect(!gooserot::ParseArguments(3, plusSeedArgs, config, error), "signed seed syntax is rejected");
  config = {};
  error.clear();
  Expect(!gooserot::ParseArguments(3, overflowSeedArgs, config, error), "seed overflow is rejected");

  wchar_t* unsafeLab[] = {program, mode, lab};
  config = {};
  error.clear();
  Expect(!gooserot::ParseArguments(3, unsafeLab, config, error), "lab requires explicit VM confirmation");

  wchar_t preview[] = L"--preview";
  wchar_t* previewLab[] = {program, mode, lab, preview};
  config = {};
  error.clear();
  Expect(gooserot::ParseArguments(4, previewLab, config, error), "safe preview permits lab visuals");
  Expect(!config.desktopEffects, "preview disables desktop effects");
  Expect(config.muted && !config.flashesEnabled && config.reducedMotion,
         "preview defaults to muted reduced effects");

  wchar_t mute[] = L"--mute";
  wchar_t noFlashes[] = L"--no-flashes";
  wchar_t reducedMotion[] = L"--reduced-motion";
  wchar_t* accessible[] = {program, mute, noFlashes, reducedMotion};
  config = {};
  error.clear();
  Expect(gooserot::ParseArguments(4, accessible, config, error),
         "accessibility switches parse");
  Expect(config.muted && !config.flashesEnabled && config.reducedMotion,
         "accessibility switches are retained");

  wchar_t fakeReboot[] = L"--fake-reboot";
  wchar_t* invalidPreviewChain[] = {program, fakeReboot};
  config = {};
  error.clear();
  Expect(!gooserot::ParseArguments(2, invalidPreviewChain, config, error),
         "Preview chaining is lab-only");

  wchar_t bootGame[] = L"--boot-game";
  wchar_t* unsafeHandoff[] = {program, bootGame};
  config = {};
  error.clear();
  Expect(!gooserot::ParseArguments(2, unsafeHandoff, config, error), "boot handoff is unavailable without artifacts");

  wchar_t* unavailableHandoff[] = {program, mode, lab, confirmed, bootGame};
  config = {};
  error.clear();
  Expect(!gooserot::ParseArguments(5, unavailableHandoff, config, error),
         "lab cannot claim an unverified boot handoff");
}

void TestChaosVisualCues() {
  namespace phase = gooserot::phase;
  using gooserot::EvaluateChaosVisualCue;
  const auto before = EvaluateChaosVisualCue(phase::kOwnedApps - 0.1, 12.0, 67U, true);
  Expect(before.flashIntensity == 0.0f && before.faultRibbonIntensity == 0.0f,
         "visual corruption is inactive before its phase");

  const auto mutedFlash = EvaluateChaosVisualCue(phase::kEnd - 1.0, 12.0, 67U, false);
  Expect(mutedFlash.flashIntensity == 0.0f && mutedFlash.faultRibbonIntensity > 0.0f,
         "no-flashes preserves non-flashing glitch ribbons");

  const auto deterministicA = EvaluateChaosVisualCue(phase::kEnd - 1.0, 17.25, 67U, true);
  const auto deterministicB = EvaluateChaosVisualCue(phase::kEnd - 1.0, 17.25, 67U, true);
  Expect(deterministicA.flashIntensity == deterministicB.flashIntensity &&
             deterministicA.faultRibbonIntensity == deterministicB.faultRibbonIntensity &&
             deterministicA.pattern == deterministicB.pattern,
         "visual cues are deterministic for a seed and clock");

  int pulseStarts = 0;
  int currentPulseSamples = 0;
  int longestPulseSamples = 0;
  bool previousFlash = false;
  for (int sample = 0; sample <= 10000; ++sample) {
    const double realTime = static_cast<double>(sample) / 1000.0;
    const auto cue = EvaluateChaosVisualCue(phase::kEnd - 1.0, realTime, 67U, true);
    Expect(cue.flashIntensity >= 0.0f && cue.flashIntensity <= 1.0f,
           "flash intensity remains bounded");
    Expect(cue.faultRibbonIntensity >= 0.0f && cue.faultRibbonIntensity <= 1.0f,
           "fault-ribbon intensity remains bounded");
    const bool flashing = cue.flashIntensity > 0.0f;
    if (flashing && !previousFlash) ++pulseStarts;
    currentPulseSamples = flashing ? currentPulseSamples + 1 : 0;
    longestPulseSamples = std::max(longestPulseSamples, currentPulseSamples);
    previousFlash = flashing;
  }
  Expect(pulseStarts > 0, "the finale produces visible flash pulses");
  Expect(pulseStarts <= 14, "flash cadence stays below two pulses per real second");
  Expect(longestPulseSamples <= 110, "each flash pulse lasts at most 110 milliseconds");
}

void TestFrameAdvanceAtLowFps() {
  const auto oneFps = gooserot::EvaluateFrameAdvance(1.0, 1.0);
  Expect(oneFps.wallDelta == 1.0 && oneFps.logicalDelta == 1.0,
         "a one FPS frame still advances real deadlines and timeline by one second");
  Expect(oneFps.simulationDelta == 0.25 && oneFps.simulationLogicalDelta == 0.25,
         "a one FPS frame keeps only physical simulation bounded");
  const auto accelerated = gooserot::EvaluateFrameAdvance(1.0, 0.1);
  Expect(accelerated.wallDelta == 1.0 && accelerated.logicalDelta == 10.0,
         "duration scale accelerates timeline without compressing real deadlines");
  const auto invalid = gooserot::EvaluateFrameAdvance(-1.0, 0.0);
  Expect(invalid.wallDelta == 0.0 && invalid.logicalDelta == 0.0,
         "invalid frame deltas fail closed");
}

void TestCarrierAssignment() {
  using gooserot::kNoCarrier;
  using gooserot::PickFreeCarrier;
  Expect(PickFreeCarrier({}, false) == kNoCarrier, "an empty flock has no carrier");
  Expect(PickFreeCarrier({0}, false) == 0, "a lone idle goose carries");
  Expect(PickFreeCarrier({0}, true) == kNoCarrier, "a busy lone goose does not carry");
  Expect(PickFreeCarrier({1}, false) == kNoCarrier, "an occupied lone goose is unavailable");
  Expect(PickFreeCarrier({0, 0, 0}, false) == 1, "the first follower carries");
  Expect(PickFreeCarrier({0, 1, 0}, false) == 2, "busy followers are skipped");
  Expect(PickFreeCarrier({0, 1, 1}, false) == kNoCarrier, "the lead remains reserved in a flock");
}

void TestPremultipliedAlphaBlend() {
  using gooserot::BlendPremultipliedArgb;
  Expect(BlendPremultipliedArgb(0x00000000U, 0xFF123456U) == 0xFF123456U,
         "transparent source preserves the destination");
  Expect(BlendPremultipliedArgb(0xFFABCDEFU, 0xFF123456U) == 0xFFABCDEFU,
         "opaque source replaces the destination");
  Expect(BlendPremultipliedArgb(0x80800000U, 0xFF0000FFU) == 0xFF80007FU,
         "half-transparent premultiplied red blends over blue");
  Expect(BlendPremultipliedArgb(0x80402010U, 0x00000000U) == 0x80402010U,
         "premultiplied source is unchanged over transparency");
}

void TestTimeline() {
  namespace phase = gooserot::phase;
  gooserot::TimelineEngine timeline;
  auto events = timeline.Advance(0.0);
  Expect(events.size() == 1 && events.front().id == gooserot::TimelineEventId::PassiveEntrance,
         "entrance fires at zero");
  events = timeline.Advance(phase::kDuplicate);
  Expect(events.size() == 10, "all crossed events fire exactly once");
  Expect(events.back().id == gooserot::TimelineEventId::Duplicate, "duplication ends the crossing");
  Expect(timeline.Advance(phase::kDuplicate).empty(), "event does not fire twice");

  timeline.Reset(phase::kCountdown);
  events = timeline.Advance(phase::kCountdown);
  Expect(events.size() == 1 && events.front().id == gooserot::TimelineEventId::Countdown,
         "start-at primes earlier events");

  // The goose leaves to collect evidence only once the file it is filling in
  // already exists, and stays away long enough for the absence to register.
  Expect(phase::kNotepad < phase::kGooseExit, "the case file is open before the goose leaves");
  Expect(phase::kGooseReturn - phase::kGooseExit >= 20.0, "the absence is long enough to register");

  double previous = -1.0;
  for (const gooserot::TimelineEvent& event : timeline.Events()) {
    Expect(event.atSeconds > previous, "timeline anchors are strictly increasing");
    Expect(event.atSeconds <= phase::kEnd, "no anchor lands past the finale");
    previous = event.atSeconds;
  }
}

void TestCursorStormWaves() {
  namespace phase = gooserot::phase;
  using gooserot::CursorStormEnvelope;
  Expect(CursorStormEnvelope(phase::kScreenShake - 1.0) == 0.0f,
         "the pointer is untouched before the storm");

  // Sample a stretch late in the storm, where it is at its most aggressive: the
  // pointer still has to come back regularly, or it is simply unusable.
  int released = 0;
  int seized = 0;
  float peak = 0.0f;
  for (int step = 0; step < 600; ++step) {
    const double at = phase::kCountdown - 30.0 + step * 0.05;
    const float value = CursorStormEnvelope(at);
    Expect(value >= 0.0f && value <= 1.0f, "the storm envelope stays normalised");
    peak = std::max(peak, value);
    if (value <= 0.0f) ++released;
    else ++seized;
  }
  Expect(released > 0 && seized > 0, "the late storm both seizes and releases the pointer");
  Expect(released * 4 >= seized, "released windows stay a meaningful share of every cycle");
  Expect(peak > 0.7f, "the late storm still gets genuinely violent");

  // The seized share has to grow, otherwise the phase never escalates.
  auto seizedShare = [](double from) {
    int count = 0;
    for (int step = 0; step < 150; ++step) {
      if (CursorStormEnvelope(from + step * 0.05) > 0.0f) ++count;
    }
    return count;
  };
  Expect(seizedShare(phase::kCountdown - 20.0) > seizedShare(phase::kScreenShake + 0.0),
         "the storm seizes the pointer for longer as the run goes on");
}

void TestCaseFileTypist() {
  gooserot::Typewriter typist;
  std::mt19937 random(67);
  std::wstring visible;
  const std::wstring script = L"FINDING 1. Arrived on site. Nobody stopped me.\n";
  typist.SetSpeed(12.0);
  typist.Reset(0.0);
  typist.Queue(script);
  Expect(!typist.Idle(), "a queued finding is not yet written");

  // Nothing may appear before its keystroke is due, and no single advance may
  // repay a long stall as one instantaneous paragraph.
  typist.Advance(0.0, random, visible);
  Expect(visible.size() <= 2U, "typing starts one character at a time");
  const std::size_t afterHugeStall = [&] {
    std::wstring buffer;
    gooserot::Typewriter stalled;
    stalled.SetSpeed(12.0);
    stalled.Reset(0.0);
    stalled.Queue(script);
    stalled.Advance(600.0, random, buffer);
    return buffer.size();
  }();
  Expect(afterHugeStall > 0U && afterHugeStall <= 24U,
         "a long stall advances the file but is not repaid as one instant paragraph");

  double now = 0.0;
  int guard = 0;
  while (!typist.Idle() && guard++ < 20000) {
    now += 1.0 / 60.0;
    typist.Advance(now, random, visible);
  }
  Expect(typist.Idle(), "the finding is eventually fully written");
  Expect(visible == script, "typos are corrected, so the final text is exact");
  Expect(now > script.size() / 30.0, "a human cadence is not instantaneous");
  Expect(now < script.size() * 1.2, "and it does not take all day either");

  // Queueing more while idle simply continues the same document.
  typist.Queue(L"FINDING 2.\n");
  Expect(!typist.Idle(), "a new finding reopens the file");
  guard = 0;
  while (!typist.Idle() && guard++ < 20000) {
    now += 1.0 / 60.0;
    typist.Advance(now, random, visible);
  }
  Expect(visible == script + L"FINDING 2.\n", "findings accumulate in order");

  // The visible buffer must never run backwards past the start, even when a
  // correction lands on the very first characters.
  gooserot::Typewriter edge;
  std::wstring shortBuffer;
  edge.Reset(0.0);
  edge.Queue(L"ab");
  for (int step = 0; step < 400; ++step) edge.Advance(step * 0.05, random, shortBuffer);
  Expect(shortBuffer == L"ab", "a correction never eats past the start of the file");
}

void TestClosingSequence() {
  namespace phase = gooserot::phase;
  // The aperture has to start closing well before the end, so the shutdown is
  // the last step of something already happening rather than a cut.
  Expect(phase::kIrisStart < phase::kEnd - 4.0, "the aperture closes over several seconds");
  Expect(phase::kIrisStart > phase::kCountdown, "and only after the countdown has been announced");

  // The opening must be long enough that the goose arrives, introduces itself
  // and walks a round before anything is scored.
  Expect(phase::kIntroduction > phase::kEntrance + 10.0, "the goose arrives before it speaks");
  Expect(phase::kInspectionRound > phase::kIntroduction, "it introduces itself before inspecting");
  Expect(phase::kNotepad > phase::kInspectionRound + 20.0,
         "the round is walked before the file is opened");
  Expect(phase::kAuraPrompt > phase::kNotepad,
         "the scorecard appears only after the inspector starts writing");
}

void TestTagZoneAndCloseBox() {
  const gooserot::RectF canvas{0.0f, 0.0f, 1024.0f, 768.0f};
  const gooserot::RectF zone = gooserot::TagZone(canvas);
  const gooserot::Vec2 center = gooserot::TagCenter(canvas);
  Expect(zone.left < center.x && zone.right > center.x, "the tag zone brackets the tag centre");
  Expect(zone.top < center.y && zone.bottom > center.y, "the tag zone covers the tag vertically");
  Expect(zone.Width() > gooserot::TagScale(canvas) * 2.0f, "the zone covers both digits");

  // The badge has to sit on the photo it belongs to, and the hit test has to
  // agree with it, or the [x] becomes decorative.
  const gooserot::Vec2 propCenter{400.0f, 300.0f};
  constexpr float propSize = 150.0f;
  const gooserot::RectF box = gooserot::PropCloseBox(propCenter, propSize);
  Expect(box.left > propCenter.x && box.bottom < propCenter.y + propSize * 0.5f,
         "the badge sits in the photo's top-right corner");
  Expect(box.right <= propCenter.x + propSize * 0.5f + 1.0f, "the badge stays on the photo");
  Expect(gooserot::PropCloseBoxHit(propCenter, propSize, box.Center()),
         "the centre of the badge is a hit");
  Expect(!gooserot::PropCloseBoxHit(propCenter, propSize, propCenter),
         "the middle of the photo is not a close target");
  Expect(!gooserot::PropCloseBoxHit(propCenter, propSize, {box.left - 40.0f, box.Center().y}),
         "a click well left of the badge misses");
}

void TestOffstageGoose() {
  const gooserot::RectF bounds{0.0f, 0.0f, 800.0f, 600.0f};
  gooserot::GooseEntity goose({400.0f, 300.0f});
  goose.SetTarget({1200.0f, 300.0f}, gooserot::SpeedTier::Run);
  for (int i = 0; i < 120; ++i) goose.Update(1.0f / 30.0f, bounds);
  Expect(goose.Position().x <= 800.0f - gooserot::GooseEntity::kBoundsMargin + 0.01f,
         "a normal goose is held inside the canvas");

  goose.SetOffstage(true);
  for (int i = 0; i < 200; ++i) goose.Update(1.0f / 30.0f, bounds);
  Expect(goose.IsOffstage(), "the offstage flag persists until cleared");
  Expect(goose.Position().x > 800.0f, "an offstage goose walks right out of the frame");
  Expect(goose.Position().x <= 800.0f + gooserot::GooseEntity::kOffstageMargin + 0.01f,
         "the outer rail still bounds an offstage goose");

  // Walking back in and clearing the flag must not teleport the goose.
  goose.SetTarget({400.0f, 300.0f}, gooserot::SpeedTier::Run);
  for (int i = 0; i < 200; ++i) goose.Update(1.0f / 30.0f, bounds);
  const gooserot::Vec2 before = goose.Position();
  goose.SetOffstage(false);
  goose.Update(1.0f / 30.0f, bounds);
  Expect(gooserot::Distance(before, goose.Position()) < 20.0f,
         "clearing the flag once back inside does not snap the goose");
}

void TestGooseLocomotion() {
  gooserot::GooseEntity goose({100.0f, 100.0f});
  goose.SetTarget({500.0f, 100.0f}, gooserot::SpeedTier::Run);
  const gooserot::RectF bounds{0.0f, 0.0f, 800.0f, 600.0f};
  for (int i = 0; i < 30; ++i) goose.Update(1.0f / 30.0f, bounds);
  Expect(goose.Position().x > 200.0f, "goose advances toward target");
  Expect(std::fabs(goose.Position().y - 100.0f) < 0.01f, "straight target stays straight");
  Expect(gooserot::Length(goose.Velocity()) <= goose.Parameters().runSpeed + 0.01f,
         "run speed remains bounded");

  for (int i = 0; i < 180; ++i) goose.Update(1.0f / 30.0f, bounds);
  Expect(goose.DistanceToTarget() < 1.0f, "goose reaches target without orbiting");

  goose.Update(1.0f / 30.0f, {0.0f, 0.0f, 40.0f, 30.0f});
  Expect(goose.Position().x == 20.0f && goose.Position().y == 15.0f,
         "tiny preview bounds collapse safely to their center");
}

void TestGooseAnimationState() {
  gooserot::GooseEntity goose({400.0f, 300.0f});
  const gooserot::RectF bounds{0.0f, 0.0f, 800.0f, 600.0f};

  goose.Update(1.0f / 30.0f, bounds);
  Expect(goose.BeakOpen() < 0.05f, "an idle goose keeps its beak shut");

  goose.Honk(0.5f);
  Expect(goose.IsHonking(), "honking starts immediately");
  for (int i = 0; i < 6; ++i) goose.Update(1.0f / 30.0f, bounds);
  Expect(goose.BeakOpen() > 0.4f, "honking opens the beak");
  for (int i = 0; i < 30; ++i) goose.Update(1.0f / 30.0f, bounds);
  Expect(!goose.IsHonking() && goose.BeakOpen() < 0.05f, "the honk expires and the beak closes");

  goose.SetLatched(true);
  goose.Honk(1.0f);
  for (int i = 0; i < 30; ++i) goose.Update(1.0f / 30.0f, bounds);
  Expect(goose.NeckExtension() > 0.9f, "latching onto the cursor stretches the neck");
  Expect(goose.BeakOpen() < 0.05f, "a latched beak stays clamped even during a honk");
  const gooserot::Vec2 desiredBeak{700.0f, 250.0f};
  const gooserot::Vec2 bodyTarget = goose.BodyTargetForBeak(desiredBeak);
  const gooserot::Vec2 projectedBeak = bodyTarget + (goose.Rig().beakTip - goose.Position());
  Expect(gooserot::Distance(projectedBeak, desiredBeak) < 0.01f,
         "beak targeting converts the desired tip into a body target");
  goose.SetLatched(false);

  constexpr gooserot::Vec2 beakTargets[] = {
      {36.0f, 300.0f}, {764.0f, 300.0f}, {400.0f, 36.0f}, {400.0f, 564.0f}};
  for (const gooserot::Vec2 beakTarget : beakTargets) {
    gooserot::GooseEntity tracker({400.0f, 300.0f});
    bool reached = false;
    for (int frame = 0; frame < 450 && !reached; ++frame) {
      tracker.SetTarget(tracker.BodyTargetForBeak(beakTarget), gooserot::SpeedTier::Charge, true);
      tracker.Update(1.0f / 30.0f, bounds);
      reached = tracker.BeakDistanceTo(beakTarget) < 14.0f;
    }
    Expect(reached, "the animated beak converges on targets near every screen edge");
  }

  // Facing +X: head and beak lead, tail trails, feet straddle the body.
  goose.SetPosition({400.0f, 300.0f});
  goose.SetTarget({700.0f, 300.0f}, gooserot::SpeedTier::Run);
  for (int i = 0; i < 30; ++i) goose.Update(1.0f / 30.0f, bounds);
  const gooserot::GooseRig& rig = goose.Rig();
  Expect(rig.beakTip.x > rig.headCenter.x && rig.headCenter.x > goose.Position().x,
         "the head and beak lead the body");
  Expect(rig.tailTip.x < goose.Position().x - 30.0f, "the tail trails the body");
  Expect((rig.leftFoot.y < goose.Position().y) != (rig.rightFoot.y < goose.Position().y),
         "the feet straddle the body axis");
}

void TestWindowClamp() {
  const gooserot::RectF work{0.0f, 0.0f, 1920.0f, 1040.0f};
  const auto moved = gooserot::ClampWindowRect({1900.0f, -50.0f, 2300.0f, 250.0f}, work);
  Expect(moved.left == 1520.0f && moved.top == 0.0f, "window remains fully visible");
  Expect(moved.Width() == 400.0f && moved.Height() == 300.0f, "window dimensions preserved");
}

}  // namespace

int main() {
  TestTimestampParsing();
  TestArgumentParsing();
  TestChaosVisualCues();
  TestFrameAdvanceAtLowFps();
  TestCarrierAssignment();
  TestPremultipliedAlphaBlend();
  TestTimeline();
  TestCursorStormWaves();
  TestCaseFileTypist();
  TestClosingSequence();
  TestTagZoneAndCloseBox();
  TestGooseLocomotion();
  TestGooseAnimationState();
  TestOffstageGoose();
  TestWindowClamp();
  if (failures == 0) {
    std::cout << "All GooseRot core tests passed.\n";
    return 0;
  }
  std::cerr << failures << " test(s) failed.\n";
  return 1;
}
