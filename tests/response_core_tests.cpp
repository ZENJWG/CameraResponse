#include "../source/ResponseCore.h"
#include "../source/IniValue.h"
#include "../source/ConfigReload.h"
#include "../source/ScanProfiler.h"
#include <string>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <cstring>

struct TestSpring {
    float position = 0.0f;
    float velocity = 0.0f;
    void AddImpulse(float v) { velocity += v; }
    void Update(float dt, float k, float damping) {
        float remaining = dt > 0.05f ? 0.05f : dt;
        while (remaining > 0.00001f) {
            const float step = remaining > 0.008f ? 0.008f : remaining;
            const float a = -k * position - damping * velocity;
            velocity += a * step;
            position += velocity * step;
            remaining -= step;
        }
    }
};


static float PeakAtHz(float hz) {
    TestSpring spring;
    spring.AddImpulse(0.8f);
    const float dt=1.0f/hz;
    float peak=0.0f;
    for (int i=0;i<static_cast<int>(hz*2.0f);++i) {
        spring.Update(dt,26.0f,9.0f);
        peak=std::max(peak,std::fabs(spring.position));
    }
    return peak;
}

static float SettleAtHz(float hz) {
    TestSpring spring;
    spring.AddImpulse(0.8f);
    const float dt=1.0f/hz;
    float t=0.0f;
    for (int i=0;i<static_cast<int>(hz*8.0f);++i) {
        spring.Update(dt,26.0f,9.0f);
        t+=dt;
        if (t>0.25f &&
            std::fabs(spring.position)<0.001f &&
            std::fabs(spring.velocity)<0.001f)
            return t;
    }
    return t;
}

static void TestFrameRateInvariance() {
    const float rates[]={30.0f,60.0f,120.0f,144.0f};
    const float refPeak=PeakAtHz(60.0f);
    const float refSettle=SettleAtHz(60.0f);
    for (float hz:rates) {
        const float peak=PeakAtHz(hz);
        const float settle=SettleAtHz(hz);
        const float peakError=std::fabs(peak-refPeak)/std::max(refPeak,0.0001f);
        const float settleError=std::fabs(settle-refSettle)/std::max(refSettle,0.0001f);
        assert(peakError<0.16f);
        assert(settleError<0.18f);
    }
}

static void TestIniParsing() {
    char preset[] = " Default ; preset selection";
    cr::StripIniComment(preset);
    assert(std::strcmp(preset, "Default") == 0);

    char spaced[] = "  0.420 ; noise blend";
    assert(std::fabs(cr::ParseIniFloat(spaced, 0.0f) - 0.42f) < 0.00001f);

    char negative[] = "-0.018 ; FOV adjustment";
    assert(std::fabs(cr::ParseIniFloat(negative, 0.0f) + 0.018f) < 0.00001f);

    char integer[] = "  250 ; milliseconds";
    assert(cr::ParseIniInt(integer, 1) == 250);

    char invalid[] = "12garbage";
    assert(cr::ParseIniInt(invalid, 77) == 77);
    char overflow[] = "21474836480";
    assert(cr::ParseIniInt(overflow, 77) == 77);
    char nanValue[] = "nan";
    assert(cr::ParseIniFloat(nanValue, 3.5f) == 3.5f);
    char invalidFloat[] = "12garbage";
    assert(cr::ParseIniFloat(invalidFloat, 3.5f) == 3.5f);

    char literalSemi[] = "Default;literal";
    cr::StripIniComment(literalSemi);
    assert(std::strcmp(literalSemi, "Default;literal") == 0);
}

static void TestMixerOverflowAndTelemetry() {
    cr::FrameResponseMixer mixer;
    mixer.BeginFrame();
    cr::CameraResponseEvent event{};
    event.impulse.side = 1.0f;
    for (unsigned i = 0; i < cr::FrameResponseMixer::kCapacity; ++i)
        mixer.Publish(event);
    assert(mixer.Count() == cr::FrameResponseMixer::kCapacity);
    assert(mixer.Telemetry().accepted == cr::FrameResponseMixer::kCapacity);

    event.impulse.side = 0.0f;
    mixer.Publish(event);
    assert(mixer.Telemetry().dropped == 1);
    assert(mixer.Telemetry().accepted == cr::FrameResponseMixer::kCapacity);

    event.impulse.side = 3.0f;
    mixer.Publish(event);
    assert(mixer.Telemetry().dropped == 2);
    assert(mixer.Telemetry().accepted == cr::FrameResponseMixer::kCapacity + 1);
    assert(mixer.Count() == cr::FrameResponseMixer::kCapacity);

    event.impulse.side = std::numeric_limits<float>::quiet_NaN();
    mixer.Publish(event);
    assert(mixer.Telemetry().dropped == 3);
    assert(mixer.Count() == cr::FrameResponseMixer::kCapacity);

    const cr::Vec3 once = mixer.Resolve(1, 1, 1, 1000);
    const float total = mixer.Telemetry().source[static_cast<unsigned>(cr::ResponseSource::Environment)].side;
    const cr::Vec3 twice = mixer.Resolve(1, 1, 1, 1000);
    assert(std::fabs(once.side - twice.side) < 0.00001f);
    assert(std::fabs(total -
        mixer.Telemetry().source[static_cast<unsigned>(cr::ResponseSource::Environment)].side) < 0.00001f);
}

struct MockIniConfig {
    std::string dir;
    std::string iniPath;
    int strength = 100;
    bool rotation = true;
    static int selectedStrength;
    static bool selectedRotation;
    static bool hasStrength;
    static bool hasRotation;

    void Load() {
        if (dir.empty()) { iniPath.clear(); return; }
        iniPath = dir + "CameraResponse.ini";
        if (hasStrength) strength = selectedStrength;
        if (hasRotation) rotation = selectedRotation;
    }
};
int MockIniConfig::selectedStrength = 200;
bool MockIniConfig::selectedRotation = false;
bool MockIniConfig::hasStrength = true;
bool MockIniConfig::hasRotation = true;

static void TestConfigReloadRestoresDefaults() {
    MockIniConfig live;
    live.dir = "game/";
    assert(cr::ReloadConfigFresh(live));
    assert(live.strength == 200 && !live.rotation);

    // Removing overrides should return them to constructor defaults.
    MockIniConfig::hasStrength = false;
    MockIniConfig::hasRotation = false;
    assert(cr::ReloadConfigFresh(live));
    assert(live.strength == 100 && live.rotation);
    assert(live.iniPath == "game/CameraResponse.ini");

    // Failure must leave the active config untouched.
    live.dir.clear();
    assert(!cr::ReloadConfigFresh(live));
    assert(live.strength == 100 && live.rotation);
}

static float WatchdogMagnitudeAfterTwoSeconds(unsigned fps) {
    cr::SourceWatchdogBank bank;
    const auto origin = cr::ResponseOrigin::Explosion;
    bank.Touch(origin, 100u, 1.0f, 10000u);
    for (unsigned frame = 1; frame <= fps * 2; ++frame) {
        const unsigned t = 100u + static_cast<unsigned>(
            (static_cast<std::uint64_t>(frame) * 2000u) / (fps * 2u));
        bank.Update(t);
    }
    const float beforeSameTick = bank.Magnitude(origin);
    bank.Update(2100u); // Duplicate update must not decay twice.
    assert(std::fabs(bank.Magnitude(origin) - beforeSameTick) < 0.00001f);
    return bank.Magnitude(origin);
}

static void TestWatchdogFrameRateInvariance() {
    const float reference = WatchdogMagnitudeAfterTwoSeconds(60u);
    for (unsigned fps : {30u, 60u, 120u, 144u}) {
        const float value = WatchdogMagnitudeAfterTwoSeconds(fps);
        assert(std::fabs(value - reference) < 0.0001f);
    }
    cr::SourceWatchdogBank bank;
    bank.Touch(cr::ResponseOrigin::Damage, 0u, 2.0f, 1000u);
    bank.Update(0u);
    assert(bank.Magnitude(cr::ResponseOrigin::Damage) == 2.0f);
    bank.Reset();
    bank.Touch(cr::ResponseOrigin::Damage, 0u, 1.0f, 1000u);
    bank.Update(20u);
    assert(bank.Magnitude(cr::ResponseOrigin::Damage) < 1.0f);
}

static float RepeatedWatchdogTouches(unsigned fps) {
    cr::SourceWatchdogBank bank;
    const auto origin = cr::ResponseOrigin::Explosion;
    bank.Touch(origin, 100u, 1.0f, 10000u);
    for (unsigned frame = 1; frame <= fps * 2; ++frame) {
        const unsigned t = 100u + static_cast<unsigned>(
            (static_cast<std::uint64_t>(frame) * 2000u) / (fps * 2u));
        bank.Touch(origin, t, 0.1f, 10000u);
        bank.Update(t);
    }
    return bank.Magnitude(origin);
}

static void TestRepeatedTouchFrameRateInvariance() {
    const float reference = RepeatedWatchdogTouches(60u);
    for (unsigned fps : {30u, 60u, 120u, 144u})
        // Touches are quantized to frame times; the final sample may be
        // slightly below the latest 0.1 evidence depending on FPS.
        assert(std::fabs(RepeatedWatchdogTouches(fps) - reference) < 0.003f);
}

static void TestOptionalProfiler() {
    cr::ScanProfiler profiler;
    const auto kind = cr::ScanKind::NearbyVehicles;
    profiler.Record(kind, 20u);
    profiler.Record(kind, 100u);
    assert(profiler.Get(kind).calls == 2u);
    assert(profiler.Get(kind).totalUs == 120u);
    assert(profiler.Get(kind).peakUs == 100u);
    {
        cr::ScopedScanProfile disabled(profiler, kind, false);
    }
    assert(profiler.Get(kind).calls == 2u);
    {
        cr::ScopedScanProfile enabled(profiler, kind, true);
    }
    assert(profiler.Get(kind).calls == 3u);
    profiler.Reset();
    assert(profiler.Get(kind).calls == 0u);
    assert(profiler.Get(kind).peakUs == 0u);
}

int main() {
    TestConfigReloadRestoresDefaults();
    TestWatchdogFrameRateInvariance();
    TestRepeatedTouchFrameRateInvariance();
    TestOptionalProfiler();
    TestFrameRateInvariance();
    TestIniParsing();
    TestMixerOverflowAndTelemetry();

    cr::FrameResponseMixer mixer;
    mixer.BeginFrame();
    cr::CameraResponseEvent event{};
    event.source = cr::ResponseSource::Combat;
    event.band = cr::ResponseBand::Mid;
    event.impulse.forward = 0.25f;
    mixer.Publish(event);
    assert(mixer.Count() == 1);
    mixer.BeginFrame();
    assert(mixer.Count() == 0);

    cr::SourceWatchdogBank watchdog;
    watchdog.Touch(cr::ResponseOrigin::Explosion, 1000u, 1.0f, 600u);
    watchdog.Update(1500u);
    assert(watchdog.Active(cr::ResponseOrigin::Explosion));
    watchdog.Update(1701u);
    assert(!watchdog.Active(cr::ResponseOrigin::Explosion));

    watchdog.Reset();
    assert(watchdog.Dominant() == cr::ResponseOrigin::Count);
    watchdog.Touch(cr::ResponseOrigin::Damage, 0u, 0.3f, 100u);
    assert(watchdog.LastEvidenceAge(cr::ResponseOrigin::Damage, 10u) == 10u);
    watchdog.Update(101u);
    assert(!watchdog.Active(cr::ResponseOrigin::Damage));
    watchdog.Reset();
    for (unsigned t = 0; t <= 4000; t += 100) {
        watchdog.Touch(cr::ResponseOrigin::NativeShake, t, 0.4f, 300u, 900u);
        watchdog.Update(t);
    }
    assert(watchdog.Suppressed(cr::ResponseOrigin::NativeShake));

    TestSpring spring;
    spring.AddImpulse(0.8f);
    for (int i = 0; i < 600; ++i)
        spring.Update(1.0f / 60.0f, 26.0f, 9.0f);
    assert(std::fabs(spring.position) < 0.001f);
    assert(std::fabs(spring.velocity) < 0.001f);

    cr::BandLimitedNoise noise(0x12345678u);
    for (int i = 0; i < 5000; ++i) {
        const float v = noise.Update(1.0f / 60.0f, 20.0f, 8.0f);
        assert(std::isfinite(v));
        assert(v >= -1.05f && v <= 1.05f);
    }

    cr::TransientEventBuffer transients;
    transients.BeginFrame();
    cr::TransientResponseEvent transient{};
    transient.origin=cr::ResponseOrigin::Explosion;
    transient.translation.forward=-0.2f;
    transient.ambientDuck=0.8f;
    assert(transients.Publish(transient));
    assert(transients.Count()==1);
    transients.BeginFrame();
    assert(transients.Count()==0);

    // Normal/critical hits should displace ambient motion in a full frame.
    cr::TransientResponseEvent ambient{};
    ambient.priorityClass = cr::TransientPriority::Ambient;
    for (unsigned i = 0; i < cr::TransientEventBuffer::kCapacity; ++i)
        assert(transients.Publish(ambient));
    cr::TransientResponseEvent critical{};
    critical.priorityClass = cr::TransientPriority::Critical;
    critical.origin = cr::ResponseOrigin::Damage;
    assert(transients.Publish(critical));
    assert(transients.Count() == cr::TransientEventBuffer::kCapacity);
    assert(transients.Dropped() == 1);
    bool foundCritical = false;
    for (unsigned i = 0; i < transients.Count(); ++i)
        foundCritical |= transients[i].origin == cr::ResponseOrigin::Damage;
    assert(foundCritical);

    std::puts("response_core_tests: PASS");
    return 0;
}
