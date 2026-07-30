#include "core.hpp"

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

void TestTimeline() {
  gooserot::TimelineEngine timeline;
  auto events = timeline.Advance(0.0);
  Expect(events.size() == 1 && events.front().id == gooserot::TimelineEventId::PassiveEntrance,
         "entrance fires at zero");
  events = timeline.Advance(135.0);
  Expect(events.size() == 6, "all crossed events fire exactly once");
  Expect(events.back().id == gooserot::TimelineEventId::Duplicate, "duplication at 2:15");
  Expect(timeline.Advance(135.0).empty(), "event does not fire twice");

  timeline.Reset(270.0);
  events = timeline.Advance(270.0);
  Expect(events.size() == 1 && events.front().id == gooserot::TimelineEventId::Countdown,
         "start-at primes earlier events");
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
  TestCarrierAssignment();
  TestTimeline();
  TestGooseLocomotion();
  TestGooseAnimationState();
  TestWindowClamp();
  if (failures == 0) {
    std::cout << "All GooseRot core tests passed.\n";
    return 0;
  }
  std::cerr << failures << " test(s) failed.\n";
  return 1;
}
