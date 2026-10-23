#include "../source/ResponseCore.h"
#include <cassert>
#include <cmath>
#include <cstdio>

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

int main() {
    TestFrameRateInvariance();

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

    std::puts("response_core_tests: PASS");
    return 0;
}
