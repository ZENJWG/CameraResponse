#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cr {

enum class ResponseSource : unsigned char {
    Locomotion = 0,
    Combat,
    Vehicle,
    Environment,
    World,
    Obstruction,
    Native,
    External,
    Count
};

enum class ResponseBand : unsigned char {
    Low = 0,
    Mid,
    High,
    Count
};

struct Vec3 {
    float side = 0.0f;
    float vertical = 0.0f;
    float forward = 0.0f;

    Vec3& operator+=(const Vec3& rhs) {
        side += rhs.side;
        vertical += rhs.vertical;
        forward += rhs.forward;
        return *this;
    }
};

struct CameraResponseEvent {
    ResponseSource source = ResponseSource::Environment;
    ResponseBand band = ResponseBand::Mid;
    Vec3 impulse{};
    float magnitude = 1.0f;
    float priority = 1.0f;
    unsigned tag = 0;
};


enum class ResponseOrigin : unsigned char {
    Locomotion = 0,
    Footstep,
    Look,
    JumpLanding,
    Damage,
    Explosion,
    Gunfire,
    Recoil,
    Melee,
    Vehicle,
    NearbyVehicle,
    Aircraft,
    Door,
    Water,
    Weather,
    WorldObject,
    PedWorld,
    Obstruction,
    NativeShake,
    External,
    Count
};

inline const char* ResponseOriginName(ResponseOrigin origin) {
    static const char* names[] = {
        "LOCOMOTION", "FOOTSTEP", "LOOK", "JUMP_LANDING", "DAMAGE",
        "EXPLOSION", "GUNFIRE", "RECOIL", "MELEE", "VEHICLE",
        "NEARBY_VEHICLE", "AIRCRAFT", "DOOR", "WATER", "WEATHER",
        "WORLD_OBJECT", "PED_WORLD", "OBSTRUCTION", "NATIVE_SHAKE",
        "EXTERNAL"
    };
    const unsigned i = static_cast<unsigned>(origin);
    return i < static_cast<unsigned>(ResponseOrigin::Count)
        ? names[i] : "UNKNOWN";
}


enum class TransientPriority : unsigned char { Ambient = 0, Normal, Critical };

struct TransientResponseEvent {
    ResponseOrigin origin = ResponseOrigin::WorldObject;
    Vec3 translation{};
    Vec3 rotationDeg{};
    float fovKick = 0.0f;
    float priority = 1.0f;
    float ambientDuck = 0.0f;
    TransientPriority priorityClass = TransientPriority::Normal;
    unsigned tag = 0;
};

class TransientEventBuffer {
public:
    static constexpr unsigned kCapacity = 48;
    void BeginFrame() { count_ = 0; dropped_ = 0; }
    bool Publish(const TransientResponseEvent& event) {
        if (count_ >= kCapacity) { ++dropped_; return false; }
        events_[count_++] = event; return true;
    }
    unsigned Count() const { return count_; }
    unsigned Dropped() const { return dropped_; }
    const TransientResponseEvent& operator[](unsigned i) const { return events_[i]; }
private:
    TransientResponseEvent events_[kCapacity]{};
    unsigned count_ = 0;
    unsigned dropped_ = 0;
};

struct SourceWatchdogState {
    unsigned activeSinceMs = 0;
    unsigned lastEvidenceMs = 0;
    unsigned releaseMs = 0;
    unsigned maxContinuousMs = 0;
    float magnitude = 0.0f;
    unsigned events = 0;
    bool active = false;
    bool suppressed = false;
};

class SourceWatchdogBank {
public:
    void Reset() {
        for (auto& state : states_)
            state = {};
    }

    void Touch(
        ResponseOrigin origin,
        unsigned nowMs,
        float magnitude,
        unsigned releaseMs,
        unsigned maxContinuousMs = 0
    ) {
        const unsigned i = static_cast<unsigned>(origin);
        if (i >= static_cast<unsigned>(ResponseOrigin::Count))
            return;

        SourceWatchdogState& state = states_[i];
        if (!state.active) {
            state.active = true;
            state.activeSinceMs = nowMs;
            state.suppressed = false;
        }

        state.lastEvidenceMs = nowMs;
        state.releaseMs = releaseMs;
        state.maxContinuousMs = maxContinuousMs;
        state.magnitude = magnitude > state.magnitude
            ? magnitude
            : state.magnitude * 0.92f;
        ++state.events;

        if (maxContinuousMs > 0 &&
            nowMs - state.activeSinceMs >= maxContinuousMs) {
            state.suppressed = true;
        }
    }

    void Update(unsigned nowMs) {
        for (auto& state : states_) {
            if (!state.active)
                continue;

            if (state.releaseMs > 0 &&
                nowMs - state.lastEvidenceMs >= state.releaseMs) {
                state = {};
                continue;
            }

            if (state.maxContinuousMs > 0 &&
                nowMs - state.activeSinceMs >= state.maxContinuousMs) {
                state.suppressed = true;
            }

            state.magnitude *= 0.985f;
            if (state.magnitude < 0.000001f)
                state.magnitude = 0.0f;
        }
    }

    bool Suppressed(ResponseOrigin origin) const {
        const unsigned i = static_cast<unsigned>(origin);
        return i < static_cast<unsigned>(ResponseOrigin::Count) &&
               states_[i].suppressed;
    }

    bool Active(ResponseOrigin origin) const {
        const unsigned i = static_cast<unsigned>(origin);
        return i < static_cast<unsigned>(ResponseOrigin::Count) &&
               states_[i].active;
    }

    unsigned ActiveCount() const {
        unsigned count = 0;
        for (const auto& state : states_) {
            if (state.active)
                ++count;
        }
        return count;
    }

    ResponseOrigin Dominant() const {
        unsigned best = 0;
        float bestMagnitude = -1.0f;
        for (unsigned i = 0;
             i < static_cast<unsigned>(ResponseOrigin::Count);
             ++i) {
            const auto& state = states_[i];
            if (!state.active || state.suppressed)
                continue;
            if (state.magnitude > bestMagnitude) {
                bestMagnitude = state.magnitude;
                best = i;
            }
        }
        return static_cast<ResponseOrigin>(best);
    }

    float Magnitude(ResponseOrigin origin) const {
        const unsigned i = static_cast<unsigned>(origin);
        return i < static_cast<unsigned>(ResponseOrigin::Count)
            ? states_[i].magnitude : 0.0f;
    }

    unsigned LastEvidenceAge(ResponseOrigin origin, unsigned nowMs) const {
        const unsigned i = static_cast<unsigned>(origin);
        if (i >= static_cast<unsigned>(ResponseOrigin::Count) ||
            states_[i].lastEvidenceMs == 0) {
            return 0;
        }
        return nowMs - states_[i].lastEvidenceMs;
    }

private:
    SourceWatchdogState states_[
        static_cast<unsigned>(ResponseOrigin::Count)
    ]{};
};

struct MixTelemetry {
    Vec3 source[(unsigned)ResponseSource::Count]{};
    Vec3 band[(unsigned)ResponseBand::Count]{};
    unsigned accepted = 0;
    unsigned dropped = 0;

    void Reset() {
        *this = {};
    }
};

class FrameResponseMixer {
public:
    static const unsigned kCapacity = 48;

    void BeginFrame() {
        count_ = 0;
        telemetry_.Reset();
    }

    void Publish(const CameraResponseEvent& e) {
        CameraResponseEvent normalized = e;
        normalized.magnitude = Clamp(normalized.magnitude, 0.0f, 8.0f);
        normalized.priority = Clamp(normalized.priority, 0.0f, 8.0f);

        if (count_ < kCapacity) {
            events_[count_++] = normalized;
            ++telemetry_.accepted;
            return;
        }

        // Keep the most significant events when a busy scene overflows.
        unsigned weakest = 0;
        float weakestScore = Score(events_[0]);
        for (unsigned i = 1; i < count_; ++i) {
            const float s = Score(events_[i]);
            if (s < weakestScore) {
                weakestScore = s;
                weakest = i;
            }
        }

        if (Score(normalized) > weakestScore)
            events_[weakest] = normalized;
        else
            ++telemetry_.dropped;
    }

    Vec3 Resolve(float lowScale, float midScale, float highScale, float maxAxis) {
        Vec3 out{};
        const float bandScale[3] = { lowScale, midScale, highScale };

        for (unsigned i = 0; i < count_; ++i) {
            const CameraResponseEvent& e = events_[i];
            const unsigned band = (unsigned)e.band;
            const unsigned source = (unsigned)e.source;
            const float scale =
                e.magnitude * e.priority *
                bandScale[band < 3 ? band : 1];

            Vec3 v{
                e.impulse.side * scale,
                e.impulse.vertical * scale,
                e.impulse.forward * scale
            };

            out += v;
            if (source < (unsigned)ResponseSource::Count)
                telemetry_.source[source] += v;
            if (band < (unsigned)ResponseBand::Count)
                telemetry_.band[band] += v;
        }

        out.side = Clamp(out.side, -maxAxis, maxAxis);
        out.vertical = Clamp(out.vertical, -maxAxis, maxAxis);
        out.forward = Clamp(out.forward, -maxAxis, maxAxis);
        return out;
    }

    const MixTelemetry& Telemetry() const { return telemetry_; }
    unsigned Count() const { return count_; }

private:
    CameraResponseEvent events_[kCapacity]{};
    unsigned count_ = 0;
    MixTelemetry telemetry_{};

    static float Clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    static float Score(const CameraResponseEvent& e) {
        const float length =
            std::sqrt(
                e.impulse.side * e.impulse.side +
                e.impulse.vertical * e.impulse.vertical +
                e.impulse.forward * e.impulse.forward
            );
        return length * e.magnitude * e.priority;
    }
};

// Deterministic, allocation-free smooth noise avoids looping sine artifacts.
class BandLimitedNoise {
public:
    explicit BandLimitedNoise(uint32_t seed = 0xA9137C21u)
        : state_(seed ? seed : 0xA9137C21u) {}

    void Reset(uint32_t seed = 0xA9137C21u) {
        state_ = seed ? seed : 0xA9137C21u;
        target_ = current_ = previous_ = 0.0f;
        timer_ = 0.0f;
    }

    float Update(float dt, float rateHz, float smoothing) {
        rateHz = Clamp(rateHz, 0.05f, 80.0f);
        dt = Clamp(dt, 0.0f, 0.050f);
        timer_ -= dt;

        const float interval = 1.0f / rateHz;
        while (timer_ <= 0.0f) {
            previous_ = target_;
            target_ = RandomSigned();
            timer_ += interval;
        }

        const float a =
            1.0f - std::exp(-Clamp(smoothing, 0.1f, 80.0f) * dt);
        current_ += (target_ - current_) * a;
        return current_;
    }

private:
    uint32_t state_;
    float target_ = 0.0f;
    float current_ = 0.0f;
    float previous_ = 0.0f;
    float timer_ = 0.0f;

    static float Clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    float RandomSigned() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return
            (static_cast<float>(state_ & 0x00FFFFFFu) / 8388607.5f) -
            1.0f;
    }
};

} // namespace cr
