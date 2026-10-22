// GTASA: Camera Response v0.9 | Created by Zeneric | Inspired by MachineGames
// Made by Zeneric
// Public version: 1.0
// Safety revision: translation-first camera commit; optional validated local orientation is INI-gated.
// GTA San Andreas Classic PC US 1.0, x86
// DK22Pac/plugin-sdk tested baseline: a1df3a6 (Win32 / GTA SA Classic 1.0 US)
//
// v1.0 extends the layered camera feature set with a dedicated vehicle camera system: locomotion, directional
// inertia, footsteps/surfaces, jump/landing, damage, explosions, melee,
// per-weapon recoil, presets and optional debug overlay.
// It patches the US 1.0 call at 0x52C98B to CopyCameraMatrixToRWCam.
// The plugin offsets CCamera::m_mCameraMatrix only for that copy, then
// restores it immediately so GTA's internal camera smoothing stays stock.
//
// Log = CameraResponse.log beside CameraResponse.asi

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include "plugin.h"
#include "CCamera.h"
#include "CCam.h"
#include "CPlayerPed.h"
#include "CAnimBlendHierarchy.h"
#include "CWeapon.h"
#include "CWeaponInfo.h"
#include "CVehicle.h"
#include "CAutomobile.h"
#include "CBmx.h"
#include "CBoat.h"
#include "CTaskSimpleJetpack.h"
#include "CObject.h"
#include "CModelInfo.h"
#include "CExplosion.h"
#include "CPools.h"
#include "CPed.h"
#include "CPedIntelligence.h"
#include "CPad.h"
#include "CTimer.h"
#include "CWorld.h"
#include "CWeather.h"
#include "CColPoint.h"
#include "CFont.h"
#include "CRGBA.h"
#include "common.h"
#include "injector.hpp"
#include "ResponseCore.h"
#include "IniValue.h"
#include "ConfigReload.h"
#include "ScanProfiler.h"
#include "CameraHookGuard.h"
#include "CameraResponseAPI.h"

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>

using namespace plugin;

namespace {

constexpr unsigned kBonePelvis    = 2;
constexpr unsigned kBoneLeftFoot  = 44;
constexpr unsigned kBoneRightFoot = 54;
constexpr float kPi = 3.14159265358979323846f;

template <class T>
T Clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float WrapAngle(float a) {
    while (a > kPi)  a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}

static RwV3d CrossRw(const RwV3d& a, const RwV3d& b) {
    RwV3d r{};
    r.x = a.y * b.z - a.z * b.y;
    r.y = a.z * b.x - a.x * b.z;
    r.z = a.x * b.y - a.y * b.x;
    return r;
}

static float DotRw(const RwV3d& a, const RwV3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static bool NormalizeRw(RwV3d& v) {
    const float lenSq = DotRw(v, v);
    if (!std::isfinite(lenSq) || lenSq <= 0.0000001f)
        return false;
    const float inv = 1.0f / std::sqrt(lenSq);
    v.x *= inv;
    v.y *= inv;
    v.z *= inv;
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

static RwV3d RotateRwAroundAxis(const RwV3d& v, const RwV3d& axis, float angle) {
    const float c = std::cos(angle);
    const float sn = std::sin(angle);
    const float d = DotRw(axis, v);
    const RwV3d cross = CrossRw(axis, v);

    RwV3d out{};
    out.x = v.x * c + cross.x * sn + axis.x * d * (1.0f - c);
    out.y = v.y * c + cross.y * sn + axis.y * d * (1.0f - c);
    out.z = v.z * c + cross.z * sn + axis.z * d * (1.0f - c);
    return out;
}

static bool ApplyLocalRwRotation(
    RwMatrix* matrix,
    float pitch,
    float roll,
    float yaw
) {
    if (!matrix)
        return false;

    RwV3d right = matrix->right;
    RwV3d up = matrix->up;
    RwV3d at = matrix->at;
    if (!NormalizeRw(right) || !NormalizeRw(up) || !NormalizeRw(at))
        return false;

    if (std::fabs(pitch) > 0.000001f) {
        up = RotateRwAroundAxis(up, right, pitch);
        at = RotateRwAroundAxis(at, right, pitch);
    }
    if (std::fabs(yaw) > 0.000001f) {
        right = RotateRwAroundAxis(right, up, yaw);
        at = RotateRwAroundAxis(at, up, yaw);
    }
    if (std::fabs(roll) > 0.000001f) {
        right = RotateRwAroundAxis(right, at, roll);
        up = RotateRwAroundAxis(up, at, roll);
    }

    // Rebuild an orthonormal basis to avoid cumulative scale/shear drift.
    if (!NormalizeRw(at) || !NormalizeRw(up))
        return false;
    right = CrossRw(up, at);
    if (!NormalizeRw(right))
        return false;
    up = CrossRw(at, right);
    if (!NormalizeRw(up))
        return false;

    matrix->right = right;
    matrix->up = up;
    matrix->at = at;
    return true;
}

float HorizontalSpeed(const CPlayerPed* p) {
    if (!p)
        return 0.0f;

    return std::sqrt(
        p->m_vecMoveSpeed.x * p->m_vecMoveSpeed.x +
        p->m_vecMoveSpeed.y * p->m_vecMoveSpeed.y
    );
}

struct Spring1D {
    float position = 0.0f;
    float velocity = 0.0f;

    void AddImpulse(float v) {
        velocity += v;
    }

    void Update(float dt, float k, float damping, float target = 0.0f) {
        // Semi-implicit Euler with bounded substeps keeps spring response
        // consistent when GTA's timestep spikes at low/variable frame rates.
        float remaining = Clamp(dt, 0.0f, 0.050f);
        while (remaining > 0.000001f) {
            const float h = std::min(remaining, 0.010f);
            const float a =
                k * (target - position) -
                damping * velocity;
            velocity += a * h;
            position += velocity * h;
            remaining -= h;
        }
    }

    void Reset() {
        position = 0.0f;
        velocity = 0.0f;
    }
};

struct MotionSpring {
    Spring1D side;
    Spring1D vertical;
    Spring1D forward;

    void Update(
        float dt,
        float k,
        float damping,
        float targetSide = 0.0f,
        float targetVertical = 0.0f,
        float targetForward = 0.0f
    ) {
        side.Update(dt, k, damping, targetSide);
        vertical.Update(dt, k, damping, targetVertical);
        forward.Update(dt, k, damping, targetForward);
    }

    void Reset() {
        side.Reset();
        vertical.Reset();
        forward.Reset();
    }
};

struct FootState {
    float prevHeight = 0.0f;
    float prevVelocity = 0.0f;
    unsigned lastStepMs = 0;
    bool initialized = false;

    void Reset() {
        *this = {};
    }
};

class Logger {
public:
    ~Logger() {
        Close();
    }

    void Init(const std::string& dir, bool enabled) {
        Close();
        enabled_ = enabled && !dir.empty();
        path_ = dir.empty() ? std::string() : dir + "CameraResponse.log";

        if (!enabled_)
            return;

        Open("w");
        if (!file_)
            return;

        std::fprintf(
            file_,
            "GTASA: Camera Response v0.9 | Created by Zeneric | Inspired by MachineGames debug log\r\n"
            "------------------------------------------------------------\r\n"
        );
        std::fflush(file_);
        lastFlushTick_ = GetTickCount();
    }

    bool Enabled() const {
        return enabled_;
    }

    void Flush() {
        if (file_)
            std::fflush(file_);
    }

    void Write(const char* fmt, ...) {
        if (!enabled_)
            return;

        if (!file_)
            Open("a");
        if (!file_)
            return;

        std::fprintf(file_, "[%10lu] ", static_cast<unsigned long>(GetTickCount()));

        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(file_, fmt, ap);
        va_end(ap);

        std::fprintf(file_, "\r\n");

        const DWORD now = GetTickCount();
        if (now - lastFlushTick_ >= 1000u) {
            std::fflush(file_);
            lastFlushTick_ = now;
        }
    }

private:
    void Open(const char* mode) {
        if (file_ || path_.empty())
            return;

        fopen_s(&file_, path_.c_str(), mode);
        if (file_) {
            setvbuf(file_, nullptr, _IOFBF, 16 * 1024);
            lastFlushTick_ = GetTickCount();
        }
    }

    void Close() {
        if (!file_)
            return;

        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
    }

    bool enabled_ = false;
    std::string path_;
    FILE* file_ = nullptr;
    DWORD lastFlushTick_ = 0;
};

// Runtime path anchor. Taking the address of this non-inline function gives
// GetModuleHandleEx a code address that is guaranteed to belong to
// CameraResponse.asi, independent of the GTA directory or modloader layout.
static void CameraResponseModuleAnchor() {
}

#include "CameraResponseConfig.inc"

enum class MotionState {
    Disabled,
    Idle,
    Walk,
    Run,
    Sprint,
    Crouch,
    CrouchWalk,
    Airborne,
    Aim
};

const char* MotionName(MotionState s) {
    switch (s) {
    case MotionState::Idle:   return "IDLE";
    case MotionState::Walk:   return "WALK";
    case MotionState::Run:    return "RUN";
    case MotionState::Sprint: return "SPRINT";
    case MotionState::Crouch: return "CROUCH";
    case MotionState::CrouchWalk: return "CROUCH-WALK";
    case MotionState::Airborne: return "AIRBORNE";
    case MotionState::Aim:    return "AIM";
    default:                  return "DISABLED";
    }
}

class CameraResponse {
public:
    void InitializeRuntime() {
        if (runtimeInitialized_ || runtimeInitAttempted_)
            return;

        runtimeInitAttempted_ = true;

        cfg_.dir = Config::ModuleDirectory();
        cr::ReloadConfigFresh(cfg_);

        // If the ASI path could not be resolved, allow a later retry instead of
        // silently binding to GTA's working directory.
        if (cfg_.dir.empty() || cfg_.iniPath.empty()) {
            runtimeInitAttempted_ = false;
            return;
        }

        log_.Init(cfg_.dir, cfg_.debug || cfg_.auditEnabled || cfg_.scanProfilingEnabled);
        log_.Write(
            "PLUGIN INITIALIZING (plugin-sdk game event). asiDir=%s ini=%s",
            cfg_.dir.c_str(),
            cfg_.iniPath.c_str()
        );
        log_.Write(
            "AUDIT MODE enabled=%d configValues=%d functionCalls=%d summaryEveryMs=%u",
            cfg_.auditEnabled ? 1 : 0,
            cfg_.auditConfigValues ? 1 : 0,
            cfg_.auditFunctionCalls ? 1 : 0,
            cfg_.auditEveryMs
        );
        cfg_.DumpEffectiveValues(log_);
        log_.Flush();
        // Install the stock third-person camera commit redirection plus the
        // event after ASI load-order hooks have already been established.
        InstallCameraCommitHook();
        InstallWaterCannonHook();

        runtimeInitialized_ = true;

        log_.Write(
            "runtime initialized from %s; camera commit hook=%d "
            "camera integration=stock",
            cfg_.dir.c_str(),
            cameraCommitHookInstalled_ ? 1 : 0
        );
        log_.Flush();
    }

    CameraResponse() {
        instance_ = this;

        // Follow plugin-sdk's normal plugin lifetime: the static constructor
        // only registers callbacks. File I/O, game-memory validation, and the
        // one required camera CALL redirection happen later from
        // gameProcessEvent, outside the Windows loader lock.
        Events::gameProcessEvent += [] {
            if (!instance_)
                return;

            if (!instance_->runtimeInitialized_)
                instance_->InitializeRuntime();

            if (instance_->runtimeInitialized_)
                instance_->GameUpdate();
        };

        Events::drawingEvent += [] {
            if (instance_ && instance_->runtimeInitialized_)
                instance_->RenderDebugOverlay();
        };

        Events::restartGameEvent += [] {
            if (instance_ && instance_->runtimeInitialized_)
                instance_->Reset("restart");
        };

        Events::reInitGameEvent += [] {
            if (instance_ && instance_->runtimeInitialized_)
                instance_->Reset("reinit");
        };
    }

    ~CameraResponse() = default;

    bool GetCurrentOffset(float* side, float* vertical, float* forward) const {
        if (side) *side = pendingSide_;
        if (vertical) *vertical = pendingVertical_;
        if (forward) *forward = pendingForward_;
        return pendingAllowed_;
    }

    bool GetCurrentRotation(float* pitchDeg, float* rollDeg, float* yawDeg) const {
        if (pitchDeg) *pitchDeg = lastPitchDeg_;
        if (rollDeg) *rollDeg = lastRollDeg_;
        if (yawDeg) *yawDeg = lastYawDeg_;
        return pendingAllowed_ && cfg_.rotationEnabled;
    }

    void SetExternalScale(float scale) {
        if (std::isfinite(scale))
            externalScale_ = Clamp(scale, 0.0f, 2.0f);
    }

    bool QueueExternalImpulse(
        float side,
        float vertical,
        float forward,
        float priority,
        int frequencyBand,
        unsigned sourceTag,
        unsigned ttlMs
    ) {
        // External callers may send NaN/Inf. Reject them before Clamp and
        // spring integration; comparisons alone do not sanitize NaN.
        if (!std::isfinite(side) || !std::isfinite(vertical) ||
            !std::isfinite(forward) || !std::isfinite(priority))
            return false;

        ExternalImpulseV2 impulse{};
        impulse.side = Clamp(side, -1.0f, 1.0f);
        impulse.vertical = Clamp(vertical, -1.0f, 1.0f);
        impulse.forward = Clamp(forward, -1.0f, 1.0f);
        impulse.priority = Clamp(priority, 0.0f, 4.0f);
        impulse.frequencyBand = Clamp(frequencyBand, 0, 2);
        impulse.sourceTag = sourceTag;
        impulse.ttlMs = Clamp<unsigned>(ttlMs, 50u, 5000u);

        while (externalQueueLock_.test_and_set(std::memory_order_acquire)) {}

        bool accepted = false;
        if (externalQueueCount_ < kExternalQueueCapacity) {
            externalQueue_[externalQueueTail_] = impulse;
            externalQueueTail_ =
                (externalQueueTail_ + 1u) % kExternalQueueCapacity;
            ++externalQueueCount_;
            accepted = true;
        } else {
            ++externalQueueDropped_;
        }

        externalQueueLock_.clear(std::memory_order_release);
        return accepted;
    }

    void ClearExternalQueue() {
        while (externalQueueLock_.test_and_set(std::memory_order_acquire)) {}
        externalQueueHead_ = 0;
        externalQueueTail_ = 0;
        externalQueueCount_ = 0;
        externalQueueLock_.clear(std::memory_order_release);
    }

    void DrainExternalImpulseQueue() {
        ExternalImpulseV2 local[kExternalQueueCapacity]{};
        unsigned count = 0;

        while (externalQueueLock_.test_and_set(std::memory_order_acquire)) {}

        while (externalQueueCount_ > 0 &&
               count < kExternalQueueCapacity) {
            local[count++] = externalQueue_[externalQueueHead_];
            externalQueueHead_ =
                (externalQueueHead_ + 1u) % kExternalQueueCapacity;
            --externalQueueCount_;
        }

        externalQueueLock_.clear(std::memory_order_release);

        for (unsigned i = 0; i < count; ++i) {
            const ExternalImpulseV2& impulse = local[i];

            float bandScale = cfg_.midFrequencyScale;
            cr::ResponseBand band = cr::ResponseBand::Mid;
            if (impulse.frequencyBand <= 0) {
                bandScale = cfg_.lowFrequencyScale;
                band = cr::ResponseBand::Low;
            } else if (impulse.frequencyBand >= 2) {
                bandScale =
                    cfg_.highFrequencyBandScale *
                    cfg_.highFrequencyScale;
                band = cr::ResponseBand::High;
            }

            TouchResponseSource(
                cr::ResponseOrigin::External,
                std::fabs(impulse.side) +
                std::fabs(impulse.vertical) +
                std::fabs(impulse.forward),
                impulse.ttlMs
            );

            if (cfg_.frequencyBandsEnabled) {
                cr::CameraResponseEvent event{};
                event.source = cr::ResponseSource::External;
                event.band = band;
                event.impulse.side = impulse.side * bandScale;
                event.impulse.vertical = impulse.vertical * bandScale;
                event.impulse.forward = impulse.forward * bandScale;
                event.priority = impulse.priority;
                event.tag = impulse.sourceTag;
                responseMixer_.Publish(event);
            } else {
                impactSpring_.side.AddImpulse(
                    impulse.side * impulse.priority * bandScale
                );
                impactSpring_.vertical.AddImpulse(
                    impulse.vertical * impulse.priority * bandScale
                );
                impactSpring_.forward.AddImpulse(
                    impulse.forward * impulse.priority * bandScale
                );
            }
        }
    }


private:
    static float SpringMagnitude(const MotionSpring& spring) {
        return
            std::fabs(spring.side.position) +
            std::fabs(spring.vertical.position) +
            std::fabs(spring.forward.position) +
            0.15f * (
                std::fabs(spring.side.velocity) +
                std::fabs(spring.vertical.velocity) +
                std::fabs(spring.forward.velocity)
            );
    }

    void TouchResponseSource(
        cr::ResponseOrigin origin,
        float magnitude,
        unsigned releaseMs = 0,
        unsigned maxContinuousMs = 0
    ) {
        if (!cfg_.sourceWatchdogEnabled)
            return;

        const unsigned now = CTimer::m_snTimeInMilliseconds;
        sourceWatchdog_.Touch(
            origin,
            now,
            std::fabs(magnitude),
            releaseMs > 0 ? releaseMs : cfg_.sourceReleaseMs,
            maxContinuousMs
        );
        lastAnyResponseEvidenceMs_ = now;
    }

    bool ResponseSourceSuppressed(cr::ResponseOrigin origin) const {
        return cfg_.sourceWatchdogEnabled &&
               sourceWatchdog_.Suppressed(origin);
    }

    void BeginTransientFrame() {
        transientEvents_.BeginFrame();
        ambientDuckRequested_ = 1.0f;
    }

    void SubmitTransientEvent(
        cr::ResponseOrigin origin,
        const cr::Vec3& translation,
        const cr::Vec3& rotationDeg,
        float fovKick,
        float priority,
        float ambientDuck,
        cr::TransientPriority priorityClass,
        unsigned tag
    ) {
        cr::TransientResponseEvent event{};
        event.origin=origin;
        event.translation=translation;
        event.rotationDeg=rotationDeg;
        event.fovKick=fovKick;
        event.priority=Clamp(priority,0.0f,4.0f);
        event.ambientDuck=Clamp(ambientDuck,0.0f,1.0f);
        event.priorityClass=priorityClass;
        event.tag=tag;
        ++transientEventsSubmitted_;
        const unsigned droppedBefore = transientEvents_.Dropped();
        transientEvents_.Publish(event);
        transientEventsDropped_ += transientEvents_.Dropped() - droppedBefore;
    }

    void CommitTransientEvents() {
        const unsigned now=CTimer::m_snTimeInMilliseconds;
        for (unsigned i=0;i<transientEvents_.Count();++i) {
            const auto& event=transientEvents_[i];
            const float classScale=
                event.priorityClass==cr::TransientPriority::Critical
                    ? 1.0f+cfg_.responseBudgetCriticalReserve
                    : 1.0f;
            const float p=event.priority*classScale;

            impactSpring_.side.AddImpulse(event.translation.side*p);
            impactSpring_.vertical.AddImpulse(event.translation.vertical*p);
            impactSpring_.forward.AddImpulse(event.translation.forward*p);

            if (cfg_.directionalRotationEnabled && cfg_.rotationEnabled) {
                MotionSpring& rotationSpring =
                    event.origin == cr::ResponseOrigin::Explosion
                        ? explosionRotationSpring_
                        : directionalRotationSpring_;
                rotationSpring.side.AddImpulse(event.rotationDeg.side * p);
                rotationSpring.vertical.AddImpulse(event.rotationDeg.vertical * p);
                rotationSpring.forward.AddImpulse(event.rotationDeg.forward * p);
            }

            if (std::fabs(event.fovKick)>0.000001f)
                AddFovKick(event.fovKick*p);

            ambientDuckRequested_=std::min(
                ambientDuckRequested_,
                1.0f-event.ambientDuck
            );
            if (event.ambientDuck>0.001f)
                ambientDuckHoldUntilMs_=std::max(
                    ambientDuckHoldUntilMs_,
                    now+cfg_.ambientDuckHoldMs
                );
        }

        transientEvents_.BeginFrame();
    }

    void UpdateAmbientDucking(float dt) {
        if (!cfg_.ambientDuckingEnabled) {
            ambientDuck_=1.0f;
            return;
        }

        const unsigned now=CTimer::m_snTimeInMilliseconds;
        float target=Clamp(
            ambientDuckRequested_,
            cfg_.ambientDuckMinimum,
            1.0f
        );

        if (now<ambientDuckHoldUntilMs_)
            target=std::min(target,ambientDuck_);

        const float response=
            target<ambientDuck_
                ? cfg_.ambientDuckAttack
                : cfg_.ambientDuckRelease;

        const float alpha=
            1.0f-std::exp(
                -response*Clamp(dt,0.0f,0.05f)
            );

        ambientDuck_ += (target-ambientDuck_)*alpha;
        ambientDuck_=Clamp(
            ambientDuck_,
            cfg_.ambientDuckMinimum,
            1.0f
        );
    }

    void ApplyResponseBudget(
        float& side,
        float& vertical,
        float& forward,
        float maxSide,
        float maxVertical,
        float maxForward
    ) {
        if (!cfg_.responseBudgetEnabled)
            return;

        const float ns=side/std::max(maxSide,0.001f);
        const float nv=vertical/std::max(maxVertical,0.001f);
        const float nf=forward/std::max(maxForward,0.001f);
        const float energy=std::sqrt(ns*ns+nv*nv+nf*nf);

        if (energy<=cfg_.responseBudgetNormalized)
            return;

        ++responseBudgetSaturations_;
        const float excess=energy-cfg_.responseBudgetNormalized;
        const float softened=
            cfg_.responseBudgetNormalized+
            excess*cfg_.responseBudgetSoftness;
        const float scale=softened/std::max(energy,0.001f);
        side*=scale;
        vertical*=scale;
        forward*=scale;
    }

    void ResetPersistentResponseState(const char* reason) {
        gaitSpring_.Reset();
        lookSpring_.Reset();
        recoilSpring_.Reset();
        impactSpring_.Reset();
        obstructionSpring_.Reset();
        vehicleCollisionSpring_.Reset();

        gaitTargetSide_ = 0.0f;
        gaitTargetVertical_ = 0.0f;
        gaitTargetForward_ = 0.0f;
        lookTargetSide_ = 0.0f;
        lookTargetVertical_ = 0.0f;
        lookTargetForward_ = 0.0f;

        chainsawActivityBlend_ = 0.0f;
        chainsawContactBlend_ = 0.0f;
        chainsawForwardBias_ = 0.0f;
        chainsawContactUntilMs_ = 0;

        nativeShakeSide_ = 0.0f;
        nativeShakeVertical_ = 0.0f;
        nativeShakeForward_ = 0.0f;
        largeVehicleSide_ = 0.0f;
        largeVehicleVertical_ = 0.0f;
        largeVehicleForward_ = 0.0f;

        mixedEventOffset_ = {};
        environmentEventCount_ = 0;
        responseMixer_.BeginFrame();

        for (auto& shock : pendingShocks_)
            shock.active = false;

        pendingAllowed_ = false;
        pendingSide_ = 0.0f;
        pendingVertical_ = 0.0f;
        pendingForward_ = 0.0f;
        pendingPitchRad_ = 0.0f;
        pendingRollRad_ = 0.0f;
        pendingYawRad_ = 0.0f;

        sourceWatchdog_.Reset();
        lastAnyResponseEvidenceMs_ = 0;
        ClearExternalQueue();

        if (cfg_.debug && reason)
            log_.Write("PERSISTENT RESPONSE RESET reason=%s", reason);
    }

    void UpdateResponseWatchdog() {
        if (!cfg_.sourceWatchdogEnabled)
            return;

        const unsigned now = CTimer::m_snTimeInMilliseconds;
        sourceWatchdog_.Update(now);

        if (lastAnyResponseEvidenceMs_ == 0 ||
            now - lastAnyResponseEvidenceMs_ < cfg_.globalTailMs) {
            return;
        }

        const float residual =
            SpringMagnitude(impactSpring_) +
            SpringMagnitude(recoilSpring_) +
            SpringMagnitude(obstructionSpring_) +
            SpringMagnitude(vehicleCollisionSpring_);

        if (residual <= cfg_.stuckSpringEpsilon)
            return;

        if (lastStuckResetMs_ != 0 &&
            now - lastStuckResetMs_ < cfg_.globalTailMs) {
            return;
        }

        lastStuckResetMs_ = now;
        ++stuckResetCount_;

        log_.Write(
            "STUCK RESPONSE WATCHDOG reset=%u residual=%.6f noEvidenceMs=%u "
            "dominant=%s activeSources=%u",
            stuckResetCount_,
            residual,
            now - lastAnyResponseEvidenceMs_,
            cr::ResponseOriginName(sourceWatchdog_.Dominant()),
            sourceWatchdog_.ActiveCount()
        );

        ResetPersistentResponseState("stuck-watchdog");
    }

    struct CameraContext {
        bool aiming = false;
        bool meleeLockOn = false;
        bool inVehicle = false;
        bool firstPerson = false;
        bool cutscene = false;
        bool missionScriptCamera = false;
        bool indoors = false;
        bool crouching = false;
        bool lowHealth = false;
        bool supportedGameplayMode = false;
        int cameraMode = -1;
        float attenuation = 1.0f;
        bool Scripted() const { return cutscene || missionScriptCamera; }
    };

    CameraContext BuildCameraContext(
        CPlayerPed* p,
        CCam& cam,
        bool aiming,
        bool meleeLockOn
    ) const {
        CameraContext c{};
        c.aiming = aiming;
        c.meleeLockOn = meleeLockOn;
        c.inVehicle = p && p->bInVehicle && p->m_pVehicle;
        c.firstPerson =
            TheCamera.Using1stPersonWeaponMode() ||
            TheCamera.m_bFirstPersonBeingUsed;
        c.cutscene = TheCamera.m_bWideScreenOn;
        c.cameraMode = static_cast<int>(cam.m_nMode);

        if (c.inVehicle) {
            c.supportedGameplayMode =
                c.cameraMode == 3 ||
                c.cameraMode == 18 ||
                c.cameraMode == 22;
        } else {
            c.supportedGameplayMode =
                c.cameraMode == 4 ||
                c.cameraMode == 5 ||
                c.cameraMode == 20 ||
                c.cameraMode == 53 ||
                meleeLockOn;
        }

        bool targetOwned = true;
        if (p && cam.m_pCamTargetEntity) {
            targetOwned =
                cam.m_pCamTargetEntity == p ||
                (c.inVehicle &&
                 cam.m_pCamTargetEntity == p->m_pVehicle);
        }

        c.missionScriptCamera =
            !c.cutscene &&
            !c.firstPerson &&
            (!c.supportedGameplayMode ||
             (!aiming && !targetOwned));

        return c;
    }


    static CameraResponse* instance_;

    Config cfg_;
    Logger log_;

    enum AuditFunctionId {
        AUD_UpdateSurface,
        AUD_TriggerWeaponRecoil,
        AUD_UpdateAirAndLanding,
        AUD_UpdateDamage,
        AUD_UpdateNearbyGunfire,
        AUD_BeginEnvironmentFrame,
        AUD_CommitEnvironmentFrame,
        AUD_ApplyDoorCloseImpulse,
        AUD_UpdateDoorResponses,
        AUD_UpdatePlayerVehicleTransition,
        AUD_UpdateAdvancedPlayerActions,
        AUD_UpdateNativeCameraShake,
        AUD_ProcessPendingExplosionShocks,
        AUD_UpdateWaterResponse,
        AUD_UpdateWeatherResponse,
        AUD_UpdateLargeVehicleProximity,
        AUD_UpdateWorldObjectImpacts,
        AUD_UpdatePedWorldResponses,
        AUD_UpdateCameraObstructionResponse,
        AUD_UpdateNearbyVehicles,
        AUD_UpdateExplosions,
        AUD_ApplyDirectionalMovement,
        AUD_UpdateFeet,
        AUD_UpdateLook,
        AUD_UpdateDetailedPlayerMelee,
        AUD_UpdateDetailedMeleeWeapon,
        AUD_UpdateWeapon,
        AUD_ResetVehicleState,
        AUD_UpdateVehicle,
        AUD_PrepareVehicleOffset,
        AUD_UpdateStrength,
        AUD_UpdateSprings,
        AUD_PrepareOffset,
        AUD_UpdateRuntimeReload,
        AUD_RenderDebugOverlay,
        AUD_GameUpdate,
        AUD_InstallCameraCommitHook,
        AUD_VerifyCameraCommitHook,
        AUD_ClipCameraOffsetAgainstWorld,
        AUD_OnCameraCommit,
        AUD_FUNCTION_COUNT
    };
    static const char* AuditFunctionName(AuditFunctionId id) {
        static const char* names[AUD_FUNCTION_COUNT] = {
            "UpdateSurface",
            "TriggerWeaponRecoil",
            "UpdateAirAndLanding",
            "UpdateDamage",
            "UpdateNearbyGunfire",
            "BeginEnvironmentFrame",
            "CommitEnvironmentFrame",
            "ApplyDoorCloseImpulse",
            "UpdateDoorResponses",
            "UpdatePlayerVehicleTransition",
            "UpdateAdvancedPlayerActions",
            "UpdateNativeCameraShake",
            "ProcessPendingExplosionShocks",
            "UpdateWaterResponse",
            "UpdateWeatherResponse",
            "UpdateLargeVehicleProximity",
            "UpdateWorldObjectImpacts",
            "UpdatePedWorldResponses",
            "UpdateCameraObstructionResponse",
            "UpdateNearbyVehicles",
            "UpdateExplosions",
            "ApplyDirectionalMovement",
            "UpdateFeet",
            "UpdateLook",
            "UpdateDetailedPlayerMelee",
            "UpdateDetailedMeleeWeapon",
            "UpdateWeapon",
            "ResetVehicleState",
            "UpdateVehicle",
            "PrepareVehicleOffset",
            "UpdateStrength",
            "UpdateSprings",
            "PrepareOffset",
            "UpdateRuntimeReload",
            "RenderDebugOverlay",
            "GameUpdate",
            "InstallCameraCommitHook",
            "VerifyCameraCommitHook",
            "ClipCameraOffsetAgainstWorld",
            "OnCameraCommit",
        };
        return names[static_cast<unsigned>(id)];
    }

    void AuditHit(AuditFunctionId id) {
        if (!cfg_.auditEnabled || !cfg_.auditFunctionCalls)
            return;
        const unsigned index = static_cast<unsigned>(id);
        if (index < AUD_FUNCTION_COUNT)
            ++auditCalls_[index];
    }

    void AuditTick() {
        if (!cfg_.auditEnabled || !cfg_.auditFunctionCalls)
            return;

        const unsigned now = CTimer::m_snTimeInMilliseconds;
        if (lastAuditSummaryMs_ != 0 &&
            now - lastAuditSummaryMs_ < cfg_.auditEveryMs)
            return;

        lastAuditSummaryMs_ = now;

        unsigned activeFunctions = 0;
        unsigned long long totalDelta = 0;
        unsigned long long busiestDelta = 0;
        AuditFunctionId busiest =
            AUD_UpdateSurface;

        for (unsigned i = 0; i < AUD_FUNCTION_COUNT; ++i) {
            const unsigned long long total = auditCalls_[i];
            const unsigned long long delta =
                total - auditPrevious_[i];

            if (delta > 0) {
                ++activeFunctions;
                totalDelta += delta;

                if (delta > busiestDelta) {
                    busiestDelta = delta;
                    busiest =
                        static_cast<AuditFunctionId>(i);
                }
            }

            auditPrevious_[i] = total;
        }

        log_.Write(
            "FUNCTION AUDIT intervalMs=%u active=%u/%u callsDelta=%llu "
            "busiest=%s:%llu",
            cfg_.auditEveryMs,
            activeFunctions,
            static_cast<unsigned>(AUD_FUNCTION_COUNT),
            totalDelta,
            AuditFunctionName(busiest),
            busiestDelta
        );
    }

    unsigned long long auditCalls_[AUD_FUNCTION_COUNT] = {};
    unsigned long long auditPrevious_[AUD_FUNCTION_COUNT] = {};
    unsigned lastAuditSummaryMs_ = 0;
    cr::ScanProfiler scanProfiler_{};
    unsigned lastScanProfileMs_ = 0;

    void ReportScanProfiles() {
        if (!cfg_.scanProfilingEnabled)
            return;

        const unsigned now = CTimer::m_snTimeInMilliseconds;
        if (lastScanProfileMs_ != 0 &&
            now - lastScanProfileMs_ < cfg_.scanProfileEveryMs)
            return;

        lastScanProfileMs_ = now;
        for (unsigned i = 0; i < cr::ScanProfiler::kCount; ++i) {
            const auto kind = static_cast<cr::ScanKind>(i);
            const cr::ScanProfile& sample = scanProfiler_.Get(kind);
            if (!sample.calls)
                continue;
            log_.Write("SCAN PROFILE name=%s calls=%llu totalUs=%llu avgUs=%llu peakUs=%llu windowMs=%u",
                cr::ScanKindName(kind),
                static_cast<unsigned long long>(sample.calls),
                static_cast<unsigned long long>(sample.totalUs),
                static_cast<unsigned long long>(sample.totalUs / sample.calls),
                static_cast<unsigned long long>(sample.peakUs),
                cfg_.scanProfileEveryMs);
        }
        scanProfiler_.Reset();
    }

    using WsfGetCurrentFovFn = void (__cdecl*)(float*);
    using WsfSetFovMultiplierFn = void (__cdecl*)(void*, float);
    using WsfRemoveFovMultiplierFn = void (__cdecl*)(void*);

    HMODULE fovWsfModule_ = nullptr;
    WsfGetCurrentFovFn wsfGetCurrentFov_ = nullptr;
    WsfSetFovMultiplierFn wsfSetFovMultiplier_ = nullptr;
    WsfRemoveFovMultiplierFn wsfRemoveFovMultiplier_ = nullptr;
    unsigned lastFovBackendProbeMs_ = 0;
    float fovMultiplier_ = 1.0f;
    float fovKick_ = 0.0f;
    float fovLastReportedBase_ = 0.0f;
    unsigned lastFovDebugMs_ = 0;
    bool fovWsfApplied_ = false;
    CCam* nativeFovCam_ = nullptr;
    float nativeFovBase_ = 70.0f;
    float nativeFovApplied_ = 70.0f;
    bool nativeFovOwned_ = false;

    // Shared GTA camera-copy signature. Keep this declaration before both
    using CopyCameraFn = void(__thiscall*)(CCamera*, bool);


    MotionState motion_ = MotionState::Disabled;

    FootState leftFoot_;
    FootState rightFoot_;

    MotionSpring gaitSpring_;
    MotionSpring lookSpring_;
    MotionSpring recoilSpring_;
    MotionSpring impactSpring_;

    // Unified v1.0 event/mixing layer. Legacy detectors still feed their
    // dedicated springs, while cross-cutting/new responses publish here so
    // source/frequency telemetry and future extensions share one contract.
    cr::FrameResponseMixer responseMixer_;
    cr::BandLimitedNoise idleNoiseSide_{0x13579BDFu};
    cr::BandLimitedNoise idleNoiseVertical_{0x2468ACE1u};
    cr::BandLimitedNoise idleNoiseForward_{0xA51C39E7u};
    cr::BandLimitedNoise vehicleNoise_{0x7F4A7C15u};
    cr::BandLimitedNoise weatherWindNoise_{0xBC31D9A3u};
    cr::BandLimitedNoise weatherRainNoise_{0x7A11C4E5u};
    cr::BandLimitedNoise weatherSandNoise_{0xD24F831Bu};
    cr::BandLimitedNoise weatherQuakeNoise_{0x8E17A62Du};
    cr::BandLimitedNoise aircraftTurbulenceNoise_{0x4A17C9D3u};
    cr::BandLimitedNoise aircraftEngineNoise_{0x671E2B95u};
    cr::Vec3 mixedEventOffset_{};
    cr::TransientEventBuffer transientEvents_{};
    cr::SourceWatchdogBank sourceWatchdog_{};
    MotionSpring directionalRotationSpring_;
    float ambientDuck_ = 1.0f;
    float ambientDuckRequested_ = 1.0f;
    unsigned ambientDuckHoldUntilMs_ = 0;
    unsigned transientEventsSubmitted_ = 0;
    unsigned transientEventsDropped_ = 0;
    unsigned responseBudgetSaturations_ = 0;
    unsigned lastAnyResponseEvidenceMs_ = 0;
    unsigned lastStuckResetMs_ = 0;
    unsigned stuckResetCount_ = 0;

    struct ExternalImpulseV2 {
        float side = 0.0f;
        float vertical = 0.0f;
        float forward = 0.0f;
        float priority = 1.0f;
        int frequencyBand = 1;
        unsigned sourceTag = 0;
        unsigned ttlMs = 600;
    };

    static constexpr unsigned kExternalQueueCapacity = 32;
    ExternalImpulseV2 externalQueue_[kExternalQueueCapacity]{};
    unsigned externalQueueHead_ = 0;
    unsigned externalQueueTail_ = 0;
    unsigned externalQueueCount_ = 0;
    unsigned externalQueueDropped_ = 0;
    std::atomic_flag externalQueueLock_ = ATOMIC_FLAG_INIT;

    float externalScale_ = 1.0f;
    unsigned lastRuntimeReloadMs_ = 0;
    bool reloadKeyWasDown_ = false;
    bool previousTractionLoss_ = false;
    float previousWheelCompression_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool wheelCompressionInit_ = false;
    unsigned reloadPhaseSerial_ = 0;
    eWeaponState lastReloadState_ = WEAPONSTATE_READY;
    MotionSpring vehicleSpring_;
    MotionSpring vehicleSuspensionSpring_;
    MotionSpring vehicleCollisionSpring_;
    bool vehicleInit_ = false;
    CVehicle* lastVehicle_ = nullptr;
    CVector prevVehicleVelocity_{};
    float prevVehicleHeading_ = 0.0f;
    float vehicleSpeed_ = 0.0f;
    float vehicleForwardAccel_ = 0.0f;
    float vehicleSideAccel_ = 0.0f;
    float vehicleVerticalAccelNow_ = 0.0f;
    float vehicleTargetSide_ = 0.0f;
    float vehicleTargetVertical_ = 0.0f;
    float vehicleTargetForward_ = 0.0f;
    float vehicleVibrationSide_ = 0.0f;
    float vehicleVibrationVertical_ = 0.0f;
    float vehicleVibrationForward_ = 0.0f;
    float vehicleHornBlend_ = 0.0f;
    float vehicleHornSide_ = 0.0f;
    float vehicleHornVertical_ = 0.0f;
    float vehicleHornForward_ = 0.0f;
    bool vehicleHornActive_ = false;
    unsigned vehicleHornFrames_ = 0;
    unsigned lastVehicleFrameDebugMs_ = 0;
    unsigned lastVehicleImpactMs_ = 0;
    unsigned vehicleImpacts_ = 0;
    CEntity* prevVehicleContacts_[6] = {};
    unsigned char prevVehicleContactCount_ = 0;
    unsigned prevVehicleCollisionTime_ = 0;
    unsigned lastVehicleContactResponseMs_ = 0;
    unsigned vehicleContactImpacts_ = 0;
    float lastVehicleContactIntensity_ = 0.0f;
    float lastVehicleContactImpulse_ = 0.0f;
    float prevVehicleDeltaVelMag_ = 0.0f;
    float vehicleCollisionKickSide_ = 0.0f;
    float vehicleCollisionKickVertical_ = 0.0f;
    float vehicleCollisionKickForward_ = 0.0f;
    bool vehicleHealthInit_ = false;
    float prevVehicleHealth_ = 1000.0f;
    float lastVehicleHealthLoss_ = 0.0f;
    unsigned lastVehicleDamageCrashMs_ = 0;
    unsigned vehicleDamageCrashes_ = 0;
    float vehicleSuspensionJolt_ = 0.0f;
    unsigned vehicleSuspensionImpacts_ = 0;
    float vehicleSuspensionHandlingStiffness_ = 1.0f;
    float vehicleSuspensionHandlingDamping_ = 1.0f;
    float vehicleSuspensionHandlingTravel_ = 1.0f;
    float vehicleSuspensionHandlingMass_ = 1.0f;
    float vehicleSuspensionHandlingBias_ = 0.5f;
    float vehicleSuspensionHandlingAntiDive_ = 0.0f;
    float vehicleSuspensionHandlingImpulseScale_ = 1.0f;
    float vehicleSuspensionHandlingSpring_ = 20.0f;
    float vehicleSuspensionHandlingSpringDamping_ = 7.4f;


    MotionState prevMotion_ = MotionState::Disabled;
    bool airborneInit_ = false;
    bool wasAirborne_ = false;
    unsigned airborneStartedMs_ = 0;
    float mostNegativeAirVz_ = 0.0f;
    bool vitalsInit_ = false;
    float prevHealth_ = 0.0f;
    float prevArmour_ = 0.0f;
    unsigned lastConfirmedPlayerDamageMs_ = 0;
    CVector prevWorldVelocity_{};
    bool playerDerivativeInit_ = false;
    CVector previousPlayerAcceleration_{};
    float previousVehicleForwardAccelForJerk_ = 0.0f;
    float previousVehicleSideAccelForJerk_ = 0.0f;
    float previousVehicleVerticalAccelForJerk_ = 0.0f;
    int lastExplosionCreated_[16] = {};
    struct NpcShotTrack {
        CPed* ped = nullptr;
        eWeaponType weapon = static_cast<eWeaponType>(0);
        unsigned ammo = 0;
        unsigned nextShotMs = 0;
        unsigned lastBodyHitMs = 0;
        eWeaponState state = WEAPONSTATE_READY;
        bool initialized = false;
    };
    std::vector<NpcShotTrack> npcShotTrack_;
    unsigned nearbyGunfireShots_ = 0;
    unsigned lastNearbyGunfireScanMs_ = 0;
    float lastNearbyGunfireDistance_ = 0.0f;
    float lastNearbyGunfireStrength_ = 0.0f;

    struct NearbyVehicleTrack {
        CVehicle* vehicle = nullptr;
        CVector velocity{};
        bool initialized = false;
    };
    std::vector<NearbyVehicleTrack> nearbyVehicleTrack_;
    float nearbyVehicleSide_ = 0.0f;
    float nearbyVehicleVertical_ = 0.0f;
    float nearbyVehicleForward_ = 0.0f;
    unsigned nearbyVehicleContributors_ = 0;
    float nearbyVehicleNearestDistance_ = 0.0f;
    float nearbyVehicleStrongestActivity_ = 0.0f;
    unsigned lastNearbyVehicleScanMs_ = 0;

    struct VehicleDoorTrack {
        CVehicle* vehicle = nullptr;
        float ratio[6] = {};
        bool latchedOpen[6] = {};
        bool wasClosed[6] = {};
        bool wasMoving[6] = {};
        unsigned lastEventMs[6] = {};
        bool initialized = false;
    };
    std::vector<VehicleDoorTrack> vehicleDoorTrack_;

    struct ObjectDoorTrack {
        CObject* object = nullptr;
        bool moving = false;
        bool open = false;
        bool doorCandidate = false;
        bool motionActive = false;
        float motion = 0.0f;
        float peakMotion = 0.0f;
        unsigned lastEventMs = 0;
        bool initialized = false;
    };
    std::vector<ObjectDoorTrack> objectDoorTrack_;

    unsigned lastDoorScanMs_ = 0;
    unsigned vehicleDoorSlams_ = 0;
    unsigned buildingDoorSlams_ = 0;
    float lastDoorDistance_ = 0.0f;
    float lastDoorStrength_ = 0.0f;

    struct EnvironmentEvent {
        float side = 0.0f;
        float vertical = 0.0f;
        float forward = 0.0f;
        float score = 0.0f;
    };
    EnvironmentEvent environmentEvents_[8] = {};
    unsigned environmentEventCount_ = 0;

    struct WorldObjectTrack {
        CObject* object = nullptr;
        unsigned collisionTime = 0;
        unsigned lastCollisionObservedMs = 0;
        unsigned lastImpactEventMs = 0;
        CVector velocity{};
        float damageIntensity = 0.0f;
        bool contactActive = false;
        bool initialized = false;
    };
    std::vector<WorldObjectTrack> worldObjectTrack_;

    struct PedWorldTrack {
        CPed* ped = nullptr;
        unsigned collisionTime = 0;
        unsigned nextFootstepMs = 0;
        bool initialized = false;
    };
    std::vector<PedWorldTrack> pedWorldTrack_;

    unsigned lastWorldObjectScanMs_ = 0;
    unsigned lastPedWorldScanMs_ = 0;
    unsigned lastLargeVehicleScanMs_ = 0;

    float largeVehicleSide_ = 0.0f;
    float largeVehicleVertical_ = 0.0f;
    float largeVehicleForward_ = 0.0f;
    unsigned aircraftContinuousSinceMs_ = 0;
    unsigned aircraftLastSeenMs_ = 0;
    bool aircraftResponseSuppressed_ = false;

    float weatherSide_ = 0.0f;
    float weatherVertical_ = 0.0f;
    float weatherForward_ = 0.0f;
    float weatherWindBlend_ = 0.0f;
    float weatherRainBlend_ = 0.0f;
    float weatherSandBlend_ = 0.0f;
    float weatherQuakeBlend_ = 0.0f;

    MotionSpring obstructionSpring_;
    MotionSpring explosionRotationSpring_;
    bool obstructionInit_ = false;
    float previousCameraTargetDistance_ = 0.0f;
    float contextScale_ = 1.0f;
    CameraContext cameraContext_{};
    bool contextIndoors_ = false;
    unsigned lastContextIndoorScanMs_ = 0;

    CVehicle* observedPlayerVehicle_ = nullptr;
    bool observedPlayerInVehicle_ = false;

    bool vehicleBailCandidate_ = false;
    bool vehicleBailActive_ = false;
    unsigned vehicleBailCandidateUntilMs_ = 0;
    unsigned vehicleBailStartedMs_ = 0;
    float vehicleBailExitSpeed_ = 0.0f;
    CVector vehicleBailExitVelocity_{};
    float vehicleBailLandingWorstVz_ = 0.0f;

    bool previousPlayerTouchingWater_ = false;

    bool previousVehicleEngineOn_ = false;
    bool previousVehicleChangingGear_ = false;
    float vehicleEnginePhase_ = 0.0f;
    float vehicleEngineRpmBlend_ = 0.0f;
    float vehicleGearPulse_ = 0.0f;
    unsigned char previousWheelDamage_[4] = {};
    unsigned char previousVehicleWheelsOnGround_ = 0;
    bool previousVehicleTouchingWater_ = false;

    float vehiclePersonalityResponse_ = 1.0f;
    float vehiclePersonalityVibration_ = 1.0f;

    float advancedPlayerSide_ = 0.0f;
    float advancedPlayerVertical_ = 0.0f;
    float advancedPlayerForward_ = 0.0f;
    bool previousSwimming_ = false;
    bool previousClimbing_ = false;
    bool previousParachute_ = false;
    bool previousJetpack_ = false;
    bool jetpackMotionInit_ = false;
    CVector previousJetpackVelocity_{};
    float jetpackResponseSide_ = 0.0f;
    float jetpackResponseVertical_ = 0.0f;
    float jetpackResponseForward_ = 0.0f;

    float advancedVehicleSide_ = 0.0f;
    float advancedVehicleVertical_ = 0.0f;
    float advancedVehicleForward_ = 0.0f;

    bool boatWaveInit_ = false;
    float previousBoatRoll_ = 0.0f;
    float previousBoatPitch_ = 0.0f;
    float boatWaveSide_ = 0.0f;
    float boatWaveVertical_ = 0.0f;
    float boatWaveForward_ = 0.0f;
    float boatWavePitchDeg_ = 0.0f;
    float boatWaveRollDeg_ = 0.0f;
    unsigned lastBoatWaveSlamMs_ = 0;

    bool aircraftMotionInit_ = false;
    float previousAircraftRoll_ = 0.0f;
    float previousAircraftPitch_ = 0.0f;
    float aircraftResponseSide_ = 0.0f;
    float aircraftResponseVertical_ = 0.0f;
    float aircraftResponseForward_ = 0.0f;
    float aircraftPitchDeg_ = 0.0f;
    float aircraftRollDeg_ = 0.0f;

    bool bmxPedalPhaseInit_ = false;
    float previousBmxPedalAngle_ = 0.0f;
    float bmxPedalRateSmoothed_ = 0.0f;

    int previousVehicleContactWheels_ = 0;
    unsigned vehicleImpactSequence_ = 0;
    unsigned lastVehicleSequenceMs_ = 0;

    float nativeShakeSide_ = 0.0f;
    float nativeShakeVertical_ = 0.0f;
    float nativeShakeForward_ = 0.0f;
    float nativeShakeLastForce_ = 0.0f;
    unsigned nativeShakeContinuousSinceMs_ = 0;
    unsigned nativeShakeLastChangedMs_ = 0;
    unsigned nativeShakeBelowThresholdSinceMs_ = 0;
    bool nativeShakeSuppressed_ = false;

    struct PendingShock {
        bool active = false;
        unsigned fireAtMs = 0;
        float side = 0.0f;
        float vertical = 0.0f;
        float forward = 0.0f;
        float rotationPitchDeg = 0.0f;
        float rotationRollDeg = 0.0f;
        float ambientDuck = 0.0f;
    };
    PendingShock pendingShocks_[16] = {};

    float gaitTargetSide_ = 0.0f;
    float gaitTargetVertical_ = 0.0f;
    float gaitTargetForward_ = 0.0f;

    float lookTargetSide_ = 0.0f;
    float lookTargetVertical_ = 0.0f;
    float lookTargetForward_ = 0.0f;

    float strengthNow_ = 0.0f;
    float idleTime_ = 0.0f;
    float stationaryTime_ = 0.0f;
    float gaitPhase_ = 0.0f;

    bool camAngleInit_ = false;
    float prevYaw_ = 0.0f;
    float prevPitch_ = 0.0f;
    float yawRate_ = 0.0f;
    float pitchRate_ = 0.0f;

    bool speedInit_ = false;
    float prevSpeed_ = 0.0f;

    float leftHeight_ = 0.0f;
    float rightHeight_ = 0.0f;
    float footDiff_ = 0.0f;
    float smoothedFootPhase_ = 0.0f;
    float rawFootPhase_ = 0.0f;
    unsigned lastAnyStepMs_ = 0;
    bool lastAcceptedStepLeft_ = false;
    bool acceptedStepSequenceInit_ = false;
    unsigned leftSteps_ = 0;
    unsigned rightSteps_ = 0;

    bool weaponInit_ = false;
    eWeaponType prevWeapon_ = static_cast<eWeaponType>(0);
    unsigned prevAmmo_ = 0;
    unsigned prevAmmoTotal_ = 0;
    unsigned prevNextShotMs_ = 0;
    eWeaponState prevWeaponState_ = WEAPONSTATE_READY;
    bool previousMeleeFireInput_ = false;
    unsigned lastRecoilMs_ = 0;
    unsigned lastWeaponShotMs_ = 0;
    unsigned shotStreak_ = 0;
    unsigned meleeContacts_ = 0;

    CTaskSimpleFight* lastFightTask_ = nullptr;
    CAnimBlendAssociation* lastFightAnim_ = nullptr;
    int lastFightMove_ = -1;
    float lastFightPhase_ = 0.0f;
    bool fightReleaseTriggered_ = false;
    unsigned meleeAttackSerial_ = 0;

    CAnimBlendAssociation* lastWeaponFightAnim_ = nullptr;
    int lastWeaponFightMove_ = -1;
    float lastWeaponFightPhase_ = 0.0f;
    bool weaponFightReleaseTriggered_ = false;
    unsigned weaponMeleeSerial_ = 0;
    unsigned lastMeleeWeaponSwingMs_ = 0;
    unsigned lastMeleeWeaponNextShot_ = 0;
    eWeaponState lastMeleeWeaponState_ = WEAPONSTATE_READY;
    float chainsawActivityBlend_ = 0.0f;
    float chainsawContactBlend_ = 0.0f;
    float chainsawForwardBias_ = 0.0f;
    unsigned chainsawContactUntilMs_ = 0;
    int lastChainsawMode_ = -1;
    cr::BandLimitedNoise chainsawNoise_{0x51A7B22Du};
    float continuousToolBlend_ = 0.0f;
    int lastContinuousToolId_ = -1;
    cr::BandLimitedNoise continuousToolNoise_{0xC0717A11u};

    float currentRecoilRecovery_ = 1.0f;
    unsigned shots_ = 0;
    unsigned currentSurfaceId_ = 0;
    float currentSurfaceMultiplier_ = 1.0f;
    unsigned lastSurfaceScanMs_ = 0;

    uint32_t rng_ = 0xA9137C21u;

    // Prepared camera-local translation.
    bool pendingAllowed_ = false;
    float pendingSide_ = 0.0f;
    float pendingVertical_ = 0.0f;
    float pendingForward_ = 0.0f;

    float pendingPitchRad_ = 0.0f;
    float pendingRollRad_ = 0.0f;
    float pendingYawRad_ = 0.0f;

    // Stock third-person camera commit:
    // CCamera::Process -> call CopyCameraMatrixToRWCam at 0x52C98B (US 1.0).
    static CopyCameraFn originalCopyCamera_;

    bool cameraCommitHookInstalled_ = false;
    uintptr_t previousCopyTarget_ = 0;
    unsigned lastCameraHookCheckMs_ = 0;
    unsigned cameraHookRestoreCount_ = 0;

    bool runtimeInitialized_ = false;
    bool runtimeInitAttempted_ = false;
    unsigned lastStatusMs_ = 0;
    std::string lastGate_;

    uint64_t updates_ = 0;
    uint64_t prepared_ = 0;
    uint64_t commitCalls_ = 0;
    uint64_t commitApplied_ = 0;
    uint64_t rwFrameFallbacks_ = 0;

    CVector lastMatrixBase_{};
    CVector lastMatrixForced_{};
    CVector lastRwFramePos_{};
    float lastPitchDeg_ = 0.0f;
    float lastRollDeg_ = 0.0f;
    float lastYawDeg_ = 0.0f;

    float cameraBasisForwardX_ = 0.0f;
    float cameraBasisForwardY_ = 1.0f;
    float cameraBasisRightX_ = 1.0f;
    float cameraBasisRightY_ = 0.0f;
    bool cameraBasisValid_ = false;





    static float Dt() {
        const float dt = CTimer::ms_fTimeStep * (1.0f / 50.0f);
        return Clamp(dt, 0.001f, 0.050f);
    }

    void UpdateCameraBasisCache(const CCam& cam) {
        float fx = cam.m_vecFront.x;
        float fy = cam.m_vecFront.y;
        const float len = std::sqrt(fx * fx + fy * fy);

        if (!std::isfinite(len) || len <= 0.0001f) {
            cameraBasisForwardX_ = 0.0f;
            cameraBasisForwardY_ = 1.0f;
            cameraBasisRightX_ = 1.0f;
            cameraBasisRightY_ = 0.0f;
            cameraBasisValid_ = false;
            return;
        }

        fx /= len;
        fy /= len;
        cameraBasisForwardX_ = fx;
        cameraBasisForwardY_ = fy;
        cameraBasisRightX_ = fy;
        cameraBasisRightY_ = -fx;
        cameraBasisValid_ = true;
    }

    bool WorldXYToCameraLocal(
        float worldX,
        float worldY,
        float& outSide,
        float& outForward
    ) const {
        if (!cameraBasisValid_) {
            outSide = 0.0f;
            outForward = 0.0f;
            return false;
        }

        outSide =
            worldX * cameraBasisRightX_ +
            worldY * cameraBasisRightY_;
        outForward =
            worldX * cameraBasisForwardX_ +
            worldY * cameraBasisForwardY_;

        return std::isfinite(outSide) && std::isfinite(outForward);
    }

    bool VehicleLocalXYToCameraLocal(
        CVehicle* vehicle,
        float vehicleSide,
        float vehicleForward,
        float& outSide,
        float& outForward
    ) const {
        if (!vehicle) {
            outSide = vehicleSide;
            outForward = vehicleForward;
            return false;
        }

        CMatrix& vm = vehicle->GetMatrix();

        // GTA SA vehicle basis:
        //   right = local +X
        //   up    = local +Y / vehicle forward
        // Convert only the horizontal component. Vertical/heave is handled
        // separately so camera pitch cannot feed back into longitudinal motion.
        const float worldX =
            vm.right.x * vehicleSide +
            vm.up.x * vehicleForward;

        const float worldY =
            vm.right.y * vehicleSide +
            vm.up.y * vehicleForward;

        if (!WorldXYToCameraLocal(
                worldX,
                worldY,
                outSide,
                outForward)) {
            outSide = vehicleSide;
            outForward = vehicleForward;
            return false;
        }

        return true;
    }


    static bool IsMeleeWeaponType(eWeaponType type) {
        const int id = static_cast<int>(type);
        return id >= 0 && id <= 15;
    }

    static bool IsControllerMeleeLockOn(
        CPlayerPed* p,
        CPad* pad,
        bool aiming
    ) {
        if (!p || !aiming)
            return false;

        CWeapon* weapon = p->GetWeapon();
        if (!weapon || !IsMeleeWeaponType(weapon->m_eWeaponType))
            return false;

        // GTA's reconciled target state covers controller lock-on after input
        // mapping. bIsAimingGun can also remain set by targeting tasks.
        return (pad && pad->GetTarget()) || p->bIsAimingGun;
    }

    static bool Moving(MotionState s) {
        return s == MotionState::Walk ||
               s == MotionState::Run ||
               s == MotionState::Sprint ||
               s == MotionState::CrouchWalk;
    }

    MotionState DetectMotion(CPlayerPed* p, bool aiming) const {
        if (!p)
            return MotionState::Disabled;

        if (p->bIsInTheAir)
            return MotionState::Airborne;

        if (aiming || p->bIsAimingGun)
            return MotionState::Aim;

        if (p->bIsDucking) {
            return HorizontalSpeed(p) > cfg_.movementDeadzone
                ? MotionState::CrouchWalk
                : MotionState::Crouch;
        }

        switch (static_cast<int>(p->m_nMoveState)) {
        case 4: return MotionState::Walk;
        case 5:
        case 6: return MotionState::Run;
        case 7: return MotionState::Sprint;
        default: return MotionState::Idle;
        }
    }

    float StateStrength(MotionState s) const {
        switch (s) {
        case MotionState::Idle:   return (cfg_.idleScale * cfg_.presetAmbient);
        case MotionState::Walk:   return cfg_.walkScale;
        case MotionState::Run:    return cfg_.runScale;
        case MotionState::Sprint: return cfg_.sprintScale;
        case MotionState::Crouch: return cfg_.crouchScale * 0.55f;
        case MotionState::CrouchWalk: return cfg_.crouchScale;
        case MotionState::Airborne: return cfg_.airborneScale;
        case MotionState::Aim:    return cfg_.aimScale;
        default:                  return 0.0f;
        }
    }











    void AddFovKick(float delta) {
        if (cfg_.fovEnabled && std::isfinite(delta))
            fovKick_ = Clamp(fovKick_ + delta, -0.10f, 0.10f);
    }

    void RestoreNativeFov() {
        if (nativeFovOwned_ && nativeFovCam_ &&
            std::isfinite(nativeFovCam_->m_fFOV) &&
            std::fabs(nativeFovCam_->m_fFOV - nativeFovApplied_) < 0.05f)
            nativeFovCam_->m_fFOV = nativeFovBase_;
        nativeFovOwned_ = false;
        nativeFovCam_ = nullptr;
    }

    void RemoveFovOutput() {
        if (fovWsfApplied_ && wsfRemoveFovMultiplier_) {
            wsfRemoveFovMultiplier_(this);
            fovWsfApplied_ = false;
        }
        RestoreNativeFov();
    }

    bool ResolveFovBackend(bool force = false) {
        const unsigned now = CTimer::m_snTimeInMilliseconds;
        if (!force && lastFovBackendProbeMs_ &&
            now - lastFovBackendProbeMs_ < 1000u)
            return wsfSetFovMultiplier_ && wsfRemoveFovMultiplier_;

        lastFovBackendProbeMs_ = now;
        HMODULE module = cfg_.fovPreferWidescreenFix
            ? GetModuleHandleA("GTASA.WidescreenFix.asi")
            : nullptr;

        if (module != fovWsfModule_) {
            if (fovWsfApplied_ && wsfRemoveFovMultiplier_)
                wsfRemoveFovMultiplier_(this);

            fovWsfApplied_ = false;
            fovWsfModule_ = module;
            wsfGetCurrentFov_ = nullptr;
            wsfSetFovMultiplier_ = nullptr;
            wsfRemoveFovMultiplier_ = nullptr;

            if (module) {
                wsfGetCurrentFov_ = reinterpret_cast<WsfGetCurrentFovFn>(
                    GetProcAddress(module, "GetCurrentFOV"));
                wsfSetFovMultiplier_ = reinterpret_cast<WsfSetFovMultiplierFn>(
                    GetProcAddress(module, "SetFOVMultiplier"));
                wsfRemoveFovMultiplier_ = reinterpret_cast<WsfRemoveFovMultiplierFn>(
                    GetProcAddress(module, "RemoveFOVMultiplier"));
            }

            log_.Write(
                "FOV BACKEND widescreenFix=%d api=%d nativeFallback=%d module=%p",
                module ? 1 : 0,
                (wsfSetFovMultiplier_ && wsfRemoveFovMultiplier_) ? 1 : 0,
                cfg_.fovNativeFallback ? 1 : 0,
                module);
        }

        return wsfSetFovMultiplier_ && wsfRemoveFovMultiplier_;
    }

    void ApplyNativeFovMultiplier(CCam& cam, float multiplier) {
        if (!cfg_.fovNativeFallback)
            return;

        const float observed = cam.m_fFOV;
        if (!std::isfinite(observed) || observed < 10.0f || observed > 170.0f)
            return;

        if (nativeFovOwned_ && nativeFovCam_ != &cam)
            RestoreNativeFov();

        float base = observed;
        if (nativeFovOwned_ && nativeFovCam_ == &cam &&
            std::fabs(observed - nativeFovApplied_) < 0.05f)
            base = nativeFovBase_;

        nativeFovBase_ = base;
        nativeFovCam_ = &cam;
        nativeFovApplied_ = Clamp(base * multiplier, 15.0f, 150.0f);
        cam.m_fFOV = nativeFovApplied_;
        nativeFovOwned_ = true;
        fovLastReportedBase_ = base;
    }

    void UpdateFovResponse(
        CPlayerPed* player, CCam& cam, bool aiming,
        bool vehicleMode, bool allowed, float dt
    ) {
        if (!cfg_.fovEnabled || !player || !allowed) {
            fovKick_ *= std::exp(-cfg_.fovKickDecay * dt);
            fovMultiplier_ += (1.0f - fovMultiplier_) *
                (1.0f - std::exp(-cfg_.fovReturnResponse * dt));
            if (std::fabs(fovMultiplier_ - 1.0f) < 0.0005f) {
                fovMultiplier_ = 1.0f;
                RemoveFovOutput();
            }
            return;
        }

        float target = 1.0f;

        if (vehicleMode) {
            target += cfg_.fovVehicleSpeed *
                Clamp(vehicleSpeed_ / 0.95f, 0.0f, 1.25f);

            const float a = Clamp(vehicleForwardAccel_ / 7.0f, -1.0f, 1.0f);
            target += a >= 0.0f
                ? cfg_.fovVehicleAccel * a
                : cfg_.fovVehicleBrake * (-a);

            target += cfg_.fovVehicleCorner *
                Clamp(std::fabs(vehicleSideAccel_) / 6.0f, 0.0f, 1.0f);
        } else {
            switch (motion_) {
            case MotionState::Walk: target += cfg_.fovWalk; break;
            case MotionState::Run: target += cfg_.fovRun; break;
            case MotionState::Sprint: target += cfg_.fovSprint; break;
            case MotionState::Crouch:
            case MotionState::CrouchWalk: target += cfg_.fovCrouch; break;
            case MotionState::Airborne: target += cfg_.fovAirborne; break;
            case MotionState::Aim: target += cfg_.fovAim; break;
            default: break;
            }
            if (aiming && motion_ != MotionState::Aim)
                target += cfg_.fovAim;
        }

        CWeapon* weapon = player->GetWeapon();
        if (weapon) {
            const int id = static_cast<int>(weapon->m_eWeaponType);
            if (id == 9) {
                target += cfg_.fovChainsawAttack *
                    chainsawActivityBlend_ * (1.0f - chainsawContactBlend_);
                target += cfg_.fovChainsawSaw * chainsawContactBlend_;
            } else if ((id == 37 || id == 41 || id == 42) &&
                       weapon->m_nState == WEAPONSTATE_FIRING) {
                target += cfg_.fovContinuousTool * continuousToolBlend_;
            }
        }

        fovKick_ *= std::exp(-cfg_.fovKickDecay * dt);
        target += fovKick_;

        target =
            1.0f +
            (target - 1.0f) *
            cfg_.masterFovScale *
            cfg_.masterOverallScale *
            cfg_.presetFov *
            contextScale_;

        target = Clamp(
            target,
            cfg_.fovMinMultiplier,
            cfg_.fovMaxMultiplier
        );

        const float response =
            std::fabs(target - 1.0f) > std::fabs(fovMultiplier_ - 1.0f)
                ? cfg_.fovResponse : cfg_.fovReturnResponse;

        fovMultiplier_ += (target - fovMultiplier_) *
            (1.0f - std::exp(-response * dt));
        fovMultiplier_ = Clamp(fovMultiplier_,
                               cfg_.fovMinMultiplier, cfg_.fovMaxMultiplier);

        const bool usingWsf = ResolveFovBackend();
        if (usingWsf) {
            RestoreNativeFov();
            wsfSetFovMultiplier_(this, fovMultiplier_);
            fovWsfApplied_ = true;

            if (wsfGetCurrentFov_) {
                float currentFov = 0.0f;
                wsfGetCurrentFov_(&currentFov);
                if (std::isfinite(currentFov))
                    fovLastReportedBase_ = currentFov;
            }
        } else {
            ApplyNativeFovMultiplier(cam, fovMultiplier_);
        }

        const unsigned now = CTimer::m_snTimeInMilliseconds;
        if (cfg_.debug &&
            (!lastFovDebugMs_ || now - lastFovDebugMs_ >= cfg_.logEveryMs)) {
            lastFovDebugMs_ = now;
            log_.Write(
                "FOV RESPONSE backend=%s multiplier=%.4f target=%.4f kick=%.4f "
                "base=%.2f camFov=%.2f vehicle=%d motion=%s",
                usingWsf ? "WidescreenFix" :
                    (cfg_.fovNativeFallback ? "native" : "none"),
                fovMultiplier_, target, fovKick_, fovLastReportedBase_,
                cam.m_fFOV, vehicleMode ? 1 : 0, MotionName(motion_));
        }
    }




    const char* Gate(
        CPlayerPed* p,
        CCam& cam,
        const CameraContext& context
    ) const {
        if (!cfg_.enabled)
            return "disabled";

        if (!p)
            return "no-player";

        if (p->m_fHealth <= 0.0f)
            return "dead";

        if (context.cutscene)
            return "cutscene";

        if (context.missionScriptCamera)
            return "mission-script-camera";

        const bool actuallyInVehicle =
            context.inVehicle;

        // First-person camera modes are intentionally excluded.
        if (context.firstPerson)
            return "first-person";

        if (actuallyInVehicle) {
            if (!cfg_.vehicleEnabled)
                return "vehicle-disabled";

            const int vehicleMode =
                static_cast<int>(cam.m_nMode);

            if (vehicleMode != 3 &&
                vehicleMode != 18 &&
                vehicleMode != 22) {
                return "unsupported-vehicle-camera";
            }

            return "OK-VEHICLE";
        }

        const int mode =
            static_cast<int>(cam.m_nMode);

        if (mode != 4 &&
            mode != 5 &&
            mode != 20 &&
            mode != 53 &&
            !context.meleeLockOn) {
            return "unsupported-camera-mode";
        }

        if (!context.aiming &&
            cam.m_pCamTargetEntity &&
            cam.m_pCamTargetEntity != p) {
            return "other-camera-target";
        }

        return "OK";
    }

    float QueryLandingSurfaceScale(CPlayerPed* p, unsigned* outSurfaceId=nullptr) const {
        if (!p) return 1.0f;
        CVector origin=p->GetPosition();
        origin.z += 0.35f;
        CColPoint col{};
        CEntity* hit=nullptr;
        unsigned surfaceId=currentSurfaceId_;
        if (CWorld::ProcessVerticalLine(origin,-1.8f,col,hit,true,false,false,true,false,false,nullptr))
            surfaceId=static_cast<unsigned>(col.m_nSurfaceTypeB);
        if (outSurfaceId) *outSurfaceId=surfaceId;
        const float raw=Clamp(SurfaceMultiplier(surfaceId),cfg_.landingSoftMinScale,cfg_.landingHardMaxScale);
        return 1.0f + (raw-1.0f)*cfg_.landingSurfaceInfluence;
    }

    void UpdateContextAttenuation(
        CPlayerPed* p,
        CameraContext& context
    ) {
        if (!cfg_.contextAttenuationEnabled || !p) {
            contextScale_=1.0f;
            contextIndoors_=false;
            context.attenuation=1.0f;
            return;
        }

        const unsigned now=CTimer::m_snTimeInMilliseconds;
        if (!lastContextIndoorScanMs_ ||
            now-lastContextIndoorScanMs_>=cfg_.contextIndoorScanMs) {
            lastContextIndoorScanMs_=now;
            cr::ScopedScanProfile scanScope(scanProfiler_, cr::ScanKind::IndoorRay, cfg_.scanProfilingEnabled);
            CVector from=p->GetPosition(); from.z+=0.8f;
            CVector to=from; to.z+=cfg_.contextIndoorCeilingHeight;
            CColPoint hit{}; CEntity* blocker=nullptr;
            CEntity* oldIgnore=CWorld::pIgnoreEntity;
            CWorld::pIgnoreEntity=p;
            contextIndoors_=CWorld::ProcessLineOfSight(
                from,to,hit,blocker,
                true,false,false,true,true,false,true,false
            );
            CWorld::pIgnoreEntity=oldIgnore;
        }

        context.indoors=contextIndoors_;
        context.crouching=p->bIsDucking;
        context.lowHealth=p->m_fHealth<cfg_.contextLowHealthThreshold;

        float scale=1.0f;
        if (context.indoors) scale*=cfg_.contextIndoorScale;
        if (context.aiming) scale*=cfg_.contextAimScale;
        if (context.crouching) scale*=cfg_.contextCrouchScale;
        if (context.lowHealth) scale*=cfg_.contextLowHealthScale;
        if (context.Scripted()) scale*=cfg_.contextScriptedScale;

        contextScale_=Clamp(scale,0.0f,1.5f);
        context.attenuation=contextScale_;
    }

    float RandomSigned() {
        rng_ = rng_ * 1664525u + 1013904223u;
        const float x =
            float((rng_ >> 8) & 0xFFFFFFu) /
            16777215.0f;
        return x * 2.0f - 1.0f;
    }

    float SurfaceMultiplier(unsigned id) const {
        // GTA SA collision material families. Unknown/modded IDs stay neutral.
        if (id <= 3) return cfg_.surfaceRoad;
        if ((id >= 4 && id <= 5) || (id >= 7 && id <= 8) || id == 34 || id == 89 ||
            (id >= 127 && id <= 139) || id == 144) return cfg_.surfaceConcrete;
        if (id == 6 || id == 85 || id == 134 || id == 140) return cfg_.surfaceGravel;
        if ((id >= 9 && id <= 17) || (id >= 80 && id <= 82) || id == 122 ||
            (id >= 146 && id <= 153)) return cfg_.surfaceGrass;
        if ((id >= 19 && id <= 27) || (id >= 83 && id <= 84) || (id >= 87 && id <= 88) ||
            (id >= 123 && id <= 126) || (id >= 128 && id <= 133) || (id >= 141 && id <= 142) ||
            id == 145 || (id >= 155 && id <= 156)) return cfg_.surfaceDirt;
        if ((id >= 28 && id <= 33) || (id >= 74 && id <= 79) || id == 86 ||
            id == 131 || id == 143 || id == 157) return cfg_.surfaceSand;
        if (id >= 42 && id <= 49) return cfg_.surfaceWood;
        if (id >= 50 && id <= 57) return cfg_.surfaceMetal;
        if ((id >= 18 && id <= 18) || (id >= 35 && id <= 37) || id == 154) return cfg_.surfaceRock;
        return cfg_.surfaceMisc;
    }

    void SurfaceBandWeights(
        unsigned id,
        float& low,
        float& mid,
        float& high
    ) const {
        low=0.34f; mid=0.48f; high=0.18f;
        if (id>=50 && id<=57) {
            low=0.16f; mid=0.44f; high=0.72f;
        } else if (id>=42 && id<=49) {
            low=0.28f; mid=0.70f; high=0.24f;
        } else if ((id>=28 && id<=33) || (id>=74 && id<=79) ||
                   id==86 || id==131 || id==143 || id==157) {
            low=0.62f; mid=0.24f; high=0.05f;
        } else if ((id>=9 && id<=17) || (id>=80 && id<=88) ||
                   (id>=122 && id<=133) || (id>=145 && id<=156)) {
            low=0.52f; mid=0.34f; high=0.08f;
        } else if ((id>=35 && id<=37) || id==18 || id==154) {
            low=0.25f; mid=0.62f; high=0.38f;
        }
    }

    float CollisionMaterialMultiplier(unsigned id) const {
        // Sharper rigid materials transfer more camera energy; soft/loose
        // surfaces absorb some of it. Influence is blended in UpdateVehicle.
        if (id >= 50 && id <= 57) return 1.18f; // metal
        if ((id <= 8) || id == 34 || id == 89 ||
            (id >= 127 && id <= 144)) return 1.12f; // road/concrete
        if ((id >= 18 && id <= 18) || (id >= 35 && id <= 37) || id == 154)
            return 1.15f; // rock
        if (id >= 42 && id <= 49) return 0.94f; // wood
        if ((id >= 9 && id <= 17) || (id >= 80 && id <= 88) ||
            (id >= 122 && id <= 133) || (id >= 145 && id <= 156))
            return 0.82f; // grass/dirt
        if ((id >= 28 && id <= 33) || (id >= 74 && id <= 79) ||
            id == 86 || id == 131 || id == 143 || id == 157)
            return 0.74f; // sand
        return 1.0f;
    }


    void UpdateSurface(CPlayerPed* p) {
        AuditHit(AUD_UpdateSurface);
        if (!cfg_.surfaceStepsEnabled || !p || p->bIsInTheAir)
            return;

        if (!Moving(motion_))
            return;

        const unsigned now = CTimer::m_snTimeInMilliseconds;
        if (lastSurfaceScanMs_ != 0 &&
            now - lastSurfaceScanMs_ < cfg_.surfaceScanMs) {
            return;
        }

        lastSurfaceScanMs_ = now;
        cr::ScopedScanProfile scanScope(scanProfiler_, cr::ScanKind::GroundSurface, cfg_.scanProfilingEnabled);
        currentSurfaceId_ = 0;
        currentSurfaceMultiplier_ = 1.0f;

        CVector origin = p->GetPosition();
        origin.z += 0.35f;

        CColPoint col{};
        CEntity* hit = nullptr;
        if (CWorld::ProcessVerticalLine(
                origin, -1.6f, col, hit,
                true, false, false, true, false, false, nullptr)) {
            currentSurfaceId_ =
                static_cast<unsigned>(col.m_nSurfaceTypeB);
            currentSurfaceMultiplier_ =
                SurfaceMultiplier(currentSurfaceId_);
        }
    }

    static bool WeaponSupportsCameraRecoil(eWeaponType type) {
        const int id = static_cast<int>(type);

        // GTA SA firearm / launcher range:
        // 22 pistols ... 38 minigun/flamethrower group.
        // Melee, thrown items, spraycan/camera/etc. are intentionally ignored.
        return id >= 22 && id <= 38 && id != 37;
    }

    float RecoilScale(eWeaponType type) const {
        const int id = static_cast<int>(type);
        if (id >= 0 && id < 47 && cfg_.weaponScale[id] >= 0.0f)
            return cfg_.weaponScale[id];

        switch (id) {
        case 22:
        case 23: return cfg_.recoilPistol;
        case 24: return cfg_.recoilDeagle;
        case 28:
        case 29:
        case 32: return cfg_.recoilSmg;
        case 30:
        case 31: return cfg_.recoilRifle;
        case 25:
        case 26:
        case 27: return cfg_.recoilShotgun;
        case 33:
        case 34: return cfg_.recoilSniper;
        case 35:
        case 36: return cfg_.recoilLauncher;
        case 37: return cfg_.recoilFlamethrower;
        case 38: return cfg_.recoilMinigun;
        default: return 0.75f;
        }
    }

    void TriggerWeaponRecoil(
        eWeaponType type,
        unsigned roundsSpent,
        const char* signal
    ) {
        AuditHit(AUD_TriggerWeaponRecoil);
        if (!cfg_.recoilEnabled ||
            !WeaponSupportsCameraRecoil(type)) {
            return;
        }

        const unsigned now =
            CTimer::m_snTimeInMilliseconds;

        if (lastRecoilMs_ != 0 &&
            now - lastRecoilMs_ < cfg_.recoilMinIntervalMs) {
            return;
        }

        lastRecoilMs_ = now;

        const int weaponId = static_cast<int>(type);
        const bool firstShot =
            lastWeaponShotMs_ == 0 || now - lastWeaponShotMs_ > cfg_.sustainedResetMs;
        if (firstShot)
            shotStreak_ = 0;
        ++shotStreak_;
        lastWeaponShotMs_ = now;

        const float cadenceScale =
            firstShot ? cfg_.firstShotMultiplier : cfg_.sustainedShotMultiplier;

        float contextScale = 1.0f;
        if (cfg_.contextualRecoilEnabled) {
            CPlayerPed* player = FindPlayerPed();
            if (player) {
                CPad* playerPad = player->GetPadFromPlayer();
                const bool aiming =
                    (playerPad && playerPad->GetTarget()) ||
                    player->bIsAimingGun;
                if (player->bIsDucking)
                    contextScale *= cfg_.recoilCrouchScale;
                if (aiming)
                    contextScale *= cfg_.recoilAimScale;
                if (!aiming &&
                    HorizontalSpeed(player) > cfg_.movementDeadzone * 2.0f) {
                    contextScale *= cfg_.recoilMovingScale;
                }

                // Dual-wield weapon skills in SA predominantly affect pistols
                // and compact SMGs. We cannot safely depend on private skill
                // fields here, so use the active weapon model's second-gun
                // animation flag when available through the stock weapon info.
                const unsigned char weaponSkill =
                    static_cast<unsigned char>(
                        player->GetWeaponSkill(type)
                    );
                CWeaponInfo* info =
                    CWeaponInfo::GetWeaponInfo(type, weaponSkill);
                if (info && info->m_nFlags.bTwinPistol)
                    contextScale *= cfg_.recoilDualWieldScale;
            }
        }

        CPlayerPed* recoilPlayer = FindPlayerPed();
        const bool jetpackShot = recoilPlayer && recoilPlayer->m_pIntelligence &&
            recoilPlayer->m_pIntelligence->GetTaskJetPack();

        const float scale =
            RecoilScale(type) *
            cfg_.recoilStrength *
            cadenceScale *
            (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat) *
            contextScale *
            (jetpackShot ? cfg_.jetpackRecoilScale : 1.0f);

        AddFovKick(cfg_.fovRecoilKick * Clamp(scale, 0.0f, 2.0f));

        const float verticalMul =
            (weaponId >= 0 && weaponId < 47) ? cfg_.weaponVertical[weaponId] : 1.0f;
        const float forwardMul =
            (weaponId >= 0 && weaponId < 47) ? cfg_.weaponForward[weaponId] : 1.0f;
        const float sideMul =
            (weaponId >= 0 && weaponId < 47) ? cfg_.weaponSide[weaponId] : 1.0f;
        currentRecoilRecovery_ =
            (weaponId >= 0 && weaponId < 47) ? cfg_.weaponRecovery[weaponId] : 1.0f;

        // If a weapon consumes more than one ammo unit in a single game
        // update, increase the response slightly, but do not make automatics
        // explode the camera.
        const float burst =
            1.0f +
            0.18f *
            static_cast<float>(
                Clamp<unsigned>(roundsSpent > 0 ? roundsSpent - 1 : 0, 0u, 3u)
            );

        const float s =
            scale *
            burst *
            cfg_.presetRecoil;
        TouchResponseSource(cr::ResponseOrigin::Recoil, s, 700);

        // Direct spring displacement gives an immediate visible shot response.
        // Velocity then carries it into a short damped return. This never
        // changes the camera orientation matrix, avoiding visual corruption.
        const float sideSign = RandomSigned();

        {
            cr::Vec3 tr{};
            cr::Vec3 rot{};
            rot.side=sideSign*cfg_.directionalRotationRollDeg*0.12f*s;
            rot.vertical=cfg_.directionalRotationPitchDeg*0.20f*s;
            SubmitTransientEvent(
                cr::ResponseOrigin::Recoil,
                tr,rot,0.0f,1.0f,0.42f,
                cr::TransientPriority::Normal,
                0x5245434Fu
            );
        }

        recoilSpring_.vertical.position +=
            cfg_.recoilPositionVertical * verticalMul * s;

        recoilSpring_.forward.position -=
            cfg_.recoilPositionForward * forwardMul * s;

        recoilSpring_.side.position +=
            sideSign *
            cfg_.recoilPositionSide *
            sideMul *
            s;

        recoilSpring_.vertical.velocity +=
            cfg_.recoilVelocityVertical * verticalMul * s;

        recoilSpring_.forward.velocity -=
            cfg_.recoilVelocityForward * forwardMul * s;

        recoilSpring_.side.velocity +=
            sideSign *
            cfg_.recoilVelocitySide *
            sideMul *
            s;

        // High-level Kick controls are an additional explicit per-shot impulse
        // layer. They were previously read from the INI but never consumed,
        // making VerticalKick/ForwardKick/SideKick dead settings.
        recoilSpring_.vertical.AddImpulse(
            cfg_.recoilVerticalKick *
            verticalMul *
            s
        );
        recoilSpring_.forward.AddImpulse(
            -cfg_.recoilForwardKick *
            forwardMul *
            s
        );
        recoilSpring_.side.AddImpulse(
            sideSign *
            cfg_.recoilSideKick *
            sideMul *
            s
        );

        // Hard per-shot state guards. Even a bad modded weapon definition
        // cannot accumulate enough recoil to break the camera.
        recoilSpring_.vertical.position =
            Clamp(recoilSpring_.vertical.position, -0.08f, 0.18f);

        recoilSpring_.forward.position =
            Clamp(recoilSpring_.forward.position, -0.10f, 0.05f);

        recoilSpring_.side.position =
            Clamp(recoilSpring_.side.position, -0.06f, 0.06f);

        recoilSpring_.vertical.velocity =
            Clamp(recoilSpring_.vertical.velocity, -2.0f, 4.0f);

        recoilSpring_.forward.velocity =
            Clamp(recoilSpring_.forward.velocity, -2.5f, 1.0f);

        recoilSpring_.side.velocity =
            Clamp(recoilSpring_.side.velocity, -1.5f, 1.5f);

        ++shots_;

        if (cfg_.logShots) {
            log_.Write(
                "SHOT weapon=%d signal=%s spent=%u state=%u "
                "clip=%u next=%u scale=%.3f shots=%u jetpack=%d recoilV/F=(%.4f %.4f)",
                static_cast<int>(type),
                signal ? signal : "?",
                roundsSpent,
                static_cast<unsigned>(prevWeaponState_),
                prevAmmo_,
                prevNextShotMs_,
                s,
                shots_,
                jetpackShot ? 1 : 0,
                recoilSpring_.vertical.position,
                recoilSpring_.forward.position
            );
        }
    }

    bool Touchdown(
        FootState& foot,
        float h,
        float otherH,
        float dt,
        unsigned now
    ) {
        if (!foot.initialized) {
            foot.prevHeight = h;
            foot.prevVelocity = 0.0f;
            foot.initialized = true;
            return false;
        }

        const float velocity = (h - foot.prevHeight) / dt;

        const bool descending = foot.prevVelocity < -0.010f;
        const bool slowingDown = velocity > foot.prevVelocity + 0.010f;
        const bool low = h <= otherH + 0.080f;
        const bool cooldown = now - foot.lastStepMs >= 105u;

        const bool hit =
            descending &&
            slowingDown &&
            low &&
            cooldown;

        foot.prevHeight = h;
        foot.prevVelocity = velocity;

        if (hit)
            foot.lastStepMs = now;

        return hit;
    }

    void Step(bool left) {
        if (!cfg_.footstepsEnabled)
            return;

        const unsigned now = CTimer::m_snTimeInMilliseconds;

        // Bone vertical velocity can contain multiple local minima during one
        // planted-foot animation. The old code accepted each minimum as a new
        // touchdown, producing LEFT/LEFT or RIGHT/RIGHT bursts ~100 ms apart
        // and even two accepted feet on the same game timestamp.
        unsigned minGlobalIntervalMs = 105u;
        if (motion_ == MotionState::Run)
            minGlobalIntervalMs = 82u;
        else if (motion_ == MotionState::Sprint)
            minGlobalIntervalMs = 68u;

        if (lastAnyStepMs_ != 0 &&
            now - lastAnyStepMs_ < minGlobalIntervalMs) {
            return;
        }

        // Continuous biped gait alternates feet. Bone sampling can report a
        // second minimum from the same planted foot 250-350 ms later, which was
        // still being accepted in the runtime log. Suppress same-foot repeats
        // for a full stride window; after a longer gap we allow recovery so
        // turns, animation changes, or a genuinely skipped touchdown cannot
        // permanently desynchronise the detector.
        if (acceptedStepSequenceInit_ &&
            lastAcceptedStepLeft_ == left &&
            lastAnyStepMs_ != 0 &&
            now - lastAnyStepMs_ < 520u) {
            return;
        }

        const float sign = left ? 1.0f : -1.0f;
        const float s = strengthNow_ * cfg_.stepImpulse * currentSurfaceMultiplier_;

        gaitSpring_.vertical.AddImpulse(-0.085f * s);
        gaitSpring_.side.AddImpulse(sign * 0.030f * s);
        gaitSpring_.forward.AddImpulse(-0.010f * s);

        if (cfg_.frequencyBandsEnabled &&
            cfg_.footstepSurfaceBandStrength>0.0001f) {
            float lowW=0.0f, midW=0.0f, highW=0.0f;
            SurfaceBandWeights(currentSurfaceId_,lowW,midW,highW);
            const float b=cfg_.footstepSurfaceBandStrength*s;

            cr::CameraResponseEvent low{};
            low.source=cr::ResponseSource::Locomotion;
            low.band=cr::ResponseBand::Low;
            low.impulse.vertical=-b*lowW;
            low.impulse.forward=-b*lowW*0.18f;
            low.priority=0.52f;
            low.tag=0x53544C4Fu;
            responseMixer_.Publish(low);

            cr::CameraResponseEvent mid{};
            mid.source=cr::ResponseSource::Locomotion;
            mid.band=cr::ResponseBand::Mid;
            mid.impulse.side=sign*b*midW*0.24f;
            mid.impulse.vertical=-b*midW;
            mid.priority=0.62f;
            mid.tag=0x53544D49u;
            responseMixer_.Publish(mid);

            cr::CameraResponseEvent high{};
            high.source=cr::ResponseSource::Locomotion;
            high.band=cr::ResponseBand::High;
            high.impulse.side=sign*b*highW*0.18f;
            high.impulse.vertical=-b*highW*0.55f;
            high.priority=0.48f;
            high.tag=0x53544849u;
            responseMixer_.Publish(high);
        }

        lastAnyStepMs_ = now;
        lastAcceptedStepLeft_ = left;
        acceptedStepSequenceInit_ = true;

        if (left)
            ++leftSteps_;
        else
            ++rightSteps_;

        if (cfg_.logSteps) {
            log_.Write(
                "STEP %s L=%.4f R=%.4f diff=%.4f strength=%.3f",
                left ? "LEFT" : "RIGHT",
                leftHeight_,
                rightHeight_,
                footDiff_,
                strengthNow_
            );
        }
    }

    void TriggerTransition(MotionState from, MotionState to) {
        if (!cfg_.transitionsEnabled || from == MotionState::Disabled || from == to)
            return;

        if (to == MotionState::Sprint && from != MotionState::Sprint) {
            impactSpring_.forward.AddImpulse(-0.22f * cfg_.directionalInertia);
            impactSpring_.vertical.AddImpulse(-0.05f);
        } else if (from == MotionState::Sprint && (to == MotionState::Idle || to == MotionState::Aim)) {
            impactSpring_.forward.AddImpulse(0.28f * cfg_.directionalInertia);
            impactSpring_.vertical.AddImpulse(-0.07f);
        }
    }

    void UpdatePlayerJerk(CPlayerPed* p, float dt) {
        if (!cfg_.jerkResponseEnabled || !p)
            return;

        const float safeDt=std::max(dt,0.001f);
        const CVector velocity=p->m_vecMoveSpeed;

        if (!playerDerivativeInit_) {
            prevWorldVelocity_=velocity;
            previousPlayerAcceleration_={};
            playerDerivativeInit_=true;
            return;
        }

        CVector acceleration{};
        acceleration.x=(velocity.x-prevWorldVelocity_.x)/safeDt;
        acceleration.y=(velocity.y-prevWorldVelocity_.y)/safeDt;
        acceleration.z=(velocity.z-prevWorldVelocity_.z)/safeDt;

        CVector jerk{};
        jerk.x=(acceleration.x-previousPlayerAcceleration_.x)/safeDt;
        jerk.y=(acceleration.y-previousPlayerAcceleration_.y)/safeDt;
        jerk.z=(acceleration.z-previousPlayerAcceleration_.z)/safeDt;

        prevWorldVelocity_=velocity;
        previousPlayerAcceleration_=acceleration;

        float localSide=0.0f;
        float localForward=0.0f;
        WorldXYToCameraLocal(jerk.x,jerk.y,localSide,localForward);

        const float mag=std::sqrt(
            localSide*localSide+
            localForward*localForward+
            jerk.z*jerk.z
        );

        if (mag<=cfg_.jerkDeadzone)
            return;

        const float n=Clamp(
            (mag-cfg_.jerkDeadzone)/
            std::max(cfg_.jerkMax-cfg_.jerkDeadzone,0.1f),
            0.0f,1.0f
        );
        const float denom=std::max(mag,0.001f);

        cr::Vec3 tr{};
        tr.side=-localSide/denom*cfg_.playerJerkStrength*n;
        tr.vertical=-jerk.z/denom*cfg_.playerJerkStrength*n*0.55f;
        tr.forward=-localForward/denom*cfg_.playerJerkStrength*n;

        cr::Vec3 rot{};
        rot.side=-localSide/denom*cfg_.directionalRotationRollDeg*n*0.18f;
        rot.vertical=-localForward/denom*cfg_.directionalRotationPitchDeg*n*0.18f;

        SubmitTransientEvent(
            cr::ResponseOrigin::Locomotion,
            tr,rot,0.0f,0.55f,0.10f,
            cr::TransientPriority::Ambient,
            0x4A524B50u
        );
    }

    void UpdateAirAndLanding(CPlayerPed* p) {
        AuditHit(AUD_UpdateAirAndLanding);
        if (!cfg_.jumpLandEnabled || !p) {
            airborneInit_ = false;
            return;
        }

        CPedIntelligence* intel = p->m_pIntelligence;
        // Swimming and climbing end flights without a ground impact.
        if (intel && (intel->GetTaskSwim() || intel->GetTaskClimb())) {
            airborneInit_ = false;
            wasAirborne_ = false;
            mostNegativeAirVz_ = 0.0f;
            vehicleBailCandidate_ = false;
            vehicleBailActive_ = false;
            vehicleBailLandingWorstVz_ = 0.0f;
            return;
        }

        const float vz =
            p->m_vecMoveSpeed.z;

        // Standing is collision evidence. The generic air flag alone can lag
        // touchdown or miss ledge falls. Keep a confirmed flight latched until
        // contact, including the zero-velocity frame at a jump's apex.
        const bool grounded = p->bIsStanding ||
            (p->bIsLanding && std::fabs(vz) < 0.015f);
        const bool airborne = !grounded &&
            (p->bIsInTheAir || (intel && intel->GetTaskInAir()) ||
             std::fabs(vz) > 0.025f || (airborneInit_ && wasAirborne_));

        const unsigned now =
            CTimer::m_snTimeInMilliseconds;

        if (vehicleBailCandidate_) {
            if (airborne &&
                now <= vehicleBailCandidateUntilMs_) {
                vehicleBailCandidate_ = false;
                vehicleBailActive_ = true;
                vehicleBailStartedMs_ = now;
                vehicleBailLandingWorstVz_ =
                    std::min(0.0f, vz);

                float localSide = 0.0f;
                float localForward = 0.0f;

                WorldXYToCameraLocal(
                    vehicleBailExitVelocity_.x,
                    vehicleBailExitVelocity_.y,
                    localSide,
                    localForward
                );

                const float speedScale =
                    Clamp(
                        vehicleBailExitSpeed_ /
                        std::max(
                            cfg_.vehicleBailOutMinSpeed,
                            0.05f
                        ),
                        1.0f,
                        3.0f
                    );

                const float strength =
                    cfg_.vehicleBailOutStrength *
                    std::sqrt(speedScale);

                impactSpring_.side.AddImpulse(
                    Clamp(
                        localSide * 0.90f,
                        -0.30f,
                        0.30f
                    ) *
                    strength
                );

                impactSpring_.vertical.AddImpulse(
                    0.16f *
                    strength
                );

                impactSpring_.forward.AddImpulse(
                    Clamp(
                        -localForward * 0.55f,
                        -0.34f,
                        0.34f
                    ) *
                    strength
                );

                AddFovKick(
                    cfg_.vehicleBailFovKick *
                    Clamp(speedScale, 1.0f, 2.0f)
                );

                if (cfg_.debug) {
                    log_.Write(
                        "VEHICLE BAIL START source=airborne-confirm speed=%.3f "
                        "vel=(%.3f %.3f %.3f) strength=%.3f",
                        vehicleBailExitSpeed_,
                        vehicleBailExitVelocity_.x,
                        vehicleBailExitVelocity_.y,
                        vehicleBailExitVelocity_.z,
                        strength
                    );
                }
            } else if (now > vehicleBailCandidateUntilMs_) {
                vehicleBailCandidate_ = false;

                if (cfg_.debug) {
                    log_.Write(
                        "VEHICLE BAIL CANCEL reason=no-airborne speed=%.3f",
                        vehicleBailExitSpeed_
                    );
                }
            }
        }

        if (vehicleBailActive_ && airborne) {
            vehicleBailLandingWorstVz_ =
                std::min(
                    vehicleBailLandingWorstVz_,
                    vz
                );
        }

        if (!airborneInit_) {
            airborneInit_ = true;
            wasAirborne_ = airborne;
            airborneStartedMs_ = now;
            mostNegativeAirVz_ =
                std::min(0.0f, vz);

            if (vehicleBailActive_)
                vehicleBailLandingWorstVz_ =
                    std::min(
                        vehicleBailLandingWorstVz_,
                        vz
                    );

            return;
        }

        if (airborne && !wasAirborne_) {
            airborneStartedMs_ = now;
            // Do not apply the ordinary "jump takeoff" impulse when the
            // airborne transition is a vehicle bail. The dedicated bail-start
            // response already represents the ejection.
            if (!vehicleBailActive_ && vz > 0.025f &&
                !(intel && (intel->GetTaskJetPack() || intel->GetUsingParachute()))) {
                impactSpring_.vertical.AddImpulse(
                    -cfg_.jumpImpulse *
                    0.35f
                );

                impactSpring_.forward.AddImpulse(
                    -cfg_.jumpImpulse *
                    0.14f
                );

                AddFovKick(
                    cfg_.fovAirborne *
                    0.35f
                );
            }

            mostNegativeAirVz_ =
                std::min(0.0f, vz);
        }

        if (airborne)
            mostNegativeAirVz_ =
                std::min(
                    mostNegativeAirVz_,
                    vz
                );

        if (!airborne && wasAirborne_) {
            const float fall =
                Clamp(
                    -mostNegativeAirVz_,
                    0.0f,
                    1.4f
                );

            // GTA velocity units: ordinary jumps in the supplied log land at
            // about 0.17. The old curve reduced them to only 0.15 severity.
            const float severity = Clamp(
                std::sqrt(std::max(0.0f, (fall - 0.025f) / 0.32f)),
                0.0f, 1.8f);

            if (vehicleBailActive_) {
                const float bailFall =
                    Clamp(
                        -vehicleBailLandingWorstVz_,
                        0.0f,
                        1.8f
                    );

                const float bailFallSeverity =
                    Clamp(
                        (bailFall - 0.04f) /
                        0.58f,
                        0.0f,
                        2.0f
                    );

                const float speedScale =
                    Clamp(
                        vehicleBailExitSpeed_ /
                        std::max(
                            cfg_.vehicleBailOutMinSpeed,
                            0.05f
                        ),
                        1.0f,
                        3.0f
                    );

                // A low vertical fall can still be a hard tumbling landing at
                // high forward speed, so blend fall and exit-speed evidence.
                const float bailSeverity =
                    Clamp(
                        std::max(
                            bailFallSeverity,
                            (speedScale - 1.0f) *
                            0.42f
                        ),
                        0.12f,
                        2.0f
                    );

                unsigned bailSurfaceId=0;
                const float bailSurfaceScale=QueryLandingSurfaceScale(p,&bailSurfaceId);
                const float scaled =
                    bailSeverity *
                    cfg_.vehicleBailLandingScale *
                    bailSurfaceScale *
                    cfg_.presetLanding;

                float localSide = 0.0f;
                float localForward = 0.0f;

                WorldXYToCameraLocal(
                    vehicleBailExitVelocity_.x,
                    vehicleBailExitVelocity_.y,
                    localSide,
                    localForward
                );

                impactSpring_.vertical.AddImpulse(
                    -cfg_.landingImpulse *
                    scaled *
                    1.10f
                );

                impactSpring_.forward.AddImpulse(
                    -cfg_.landingImpulse *
                    0.24f *
                    scaled
                );

                impactSpring_.side.AddImpulse(
                    Clamp(
                        localSide,
                        -0.22f,
                        0.22f
                    ) *
                    0.28f *
                    scaled
                );

                AddFovKick(
                    cfg_.fovLandingKick *
                    Clamp(
                        scaled *
                        1.12f,
                        0.0f,
                        2.2f
                    )
                );

                if (cfg_.debug) {
                    log_.Write(
                        "VEHICLE BAIL LAND speed=%.3f fall=%.3f "
                        "severity=%.3f scaled=%.3f airborneMs=%u",
                        vehicleBailExitSpeed_,
                        bailFall,
                        bailSeverity,
                        scaled,
                        now - vehicleBailStartedMs_
                    );
                }

                vehicleBailActive_ = false;
                vehicleBailLandingWorstVz_ = 0.0f;
            } else if (severity > 0.01f && now - airborneStartedMs_ >= 80u) {
                const float hard=fall>0.40f?cfg_.hardLandingScale:1.0f;
                unsigned landingSurfaceId=0;
                const float surfaceScale=QueryLandingSurfaceScale(p,&landingSurfaceId);
                const float landingSeverity=
                    severity *
                    hard *
                    surfaceScale *
                    cfg_.presetLanding;
                TouchResponseSource(cr::ResponseOrigin::JumpLanding,landingSeverity,850);

                {
                    cr::Vec3 tr{};
                    cr::Vec3 rot{};
                    rot.vertical=cfg_.directionalRotationPitchDeg*
                        Clamp(landingSeverity,0.0f,1.5f)*0.45f;
                    SubmitTransientEvent(
                        cr::ResponseOrigin::JumpLanding,
                        tr,rot,0.0f,1.0f,0.48f,
                        landingSeverity>1.0f?cr::TransientPriority::Critical:cr::TransientPriority::Normal,
                        0x4C414E44u
                    );
                }
                AddFovKick(cfg_.fovLandingKick*Clamp(landingSeverity,0.0f,2.0f));
                impactSpring_.vertical.AddImpulse(-cfg_.landingImpulse*landingSeverity);
                impactSpring_.forward.AddImpulse(-cfg_.landingImpulse*0.18f*landingSeverity);
                if (cfg_.debug)
                    log_.Write("LANDING fall=%.3f severity=%.3f surface=%u surfaceScale=%.3f final=%.3f airMs=%u impulse=%.3f",
                        fall,severity,landingSurfaceId,surfaceScale,landingSeverity,
                        now-airborneStartedMs_,cfg_.landingImpulse*landingSeverity);
            }

            mostNegativeAirVz_ = 0.0f;
        }

        // Fail-safe: if a mod/script leaves bIsInTheAir inconsistent for a
        // very long time, do not keep a stale bail classification forever.
        if (vehicleBailActive_ &&
            now - vehicleBailStartedMs_ > 8000u) {
            vehicleBailActive_ = false;
            vehicleBailLandingWorstVz_ = 0.0f;

            if (cfg_.debug)
                log_.Write("VEHICLE BAIL CANCEL reason=timeout");
        }

        wasAirborne_ = airborne;
    }

    void UpdateDamage(CPlayerPed* p) {
        AuditHit(AUD_UpdateDamage);
        if (!cfg_.damageEnabled || !p)
            return;

        if (!vitalsInit_) {
            prevHealth_ = p->m_fHealth;
            prevArmour_ = p->m_fArmour;
            vitalsInit_ = true;
            return;
        }

        const float healthLoss = std::max(0.0f, prevHealth_ - p->m_fHealth);
        const float armourLoss = std::max(0.0f, prevArmour_ - p->m_fArmour);
        const float loss = healthLoss + armourLoss;

        if (loss > 0.01f) {
            lastConfirmedPlayerDamageMs_ = CTimer::m_snTimeInMilliseconds;
            const int weaponId = static_cast<int>(p->m_nLastWeaponDamage);

            // GTA SA weapon families. Keep this numeric so the code remains
            // compatible across plugin-sdk enum-header reorganisations.
            const bool meleeHit =
                weaponId >= 0 && weaponId <= 15;
            const bool explosiveHit =
                weaponId == 16 || weaponId == 17 ||
                weaponId == 18 || weaponId == 19 ||
                weaponId == 35 || weaponId == 36 ||
                weaponId == 39;
            const bool bulletHit =
                (weaponId >= 22 && weaponId <= 34) ||
                weaponId == 38;

            float familyScale = cfg_.incomingOtherDamageScale;
            const char* family = "other";
            if (meleeHit) {
                familyScale = cfg_.incomingMeleeDamageScale;
                family = "melee";
            } else if (bulletHit) {
                familyScale = cfg_.incomingBulletDamageScale;
                family = "bullet";
            } else if (explosiveHit) {
                familyScale = cfg_.incomingExplosiveDamageScale;
                family = "explosive";
            }

            const float severity =
                Clamp(
                    std::max(
                        loss / 30.0f,
                        cfg_.incomingDamageMinSeverity
                    ),
                    cfg_.incomingDamageMinSeverity,
                    2.2f
                ) *
                familyScale;
            AddFovKick(cfg_.fovDamageKick *
                Clamp(severity, 0.0f, 1.75f));

            // Default fallback is a rearward/downward body reaction.
            float sideDir = RandomSigned() * 0.35f;
            float forwardDir = -1.0f;
            float verticalDir = meleeHit ? -0.34f : -0.20f;
            bool directional = false;

            // Camera Response offsets are camera-local at commit time, so
            // damage direction must also be computed in camera space. The old
            // implementation used CJ's heading, which is wrong whenever the
            // third-person camera is orbited around him.
            if (cfg_.directionalDamageEnabled && p->m_pLastEntityDamage) {
                const CVector& pp = p->GetPosition();
                const CVector& dp = p->m_pLastEntityDamage->GetPosition();

                float dx = dp.x - pp.x;
                float dy = dp.y - pp.y;
                float dz = dp.z - pp.z;
                const float planar = std::sqrt(dx * dx + dy * dy);

                if (planar > 0.001f) {
                    dx /= planar;
                    dy /= planar;

                    CCam& cam =
                        TheCamera.m_aCams[TheCamera.m_nActiveCam];

                    CVector front = cam.m_vecFront;
                    CVector up = cam.m_vecUp;

                    const float fLen =
                        std::sqrt(front.x * front.x +
                                  front.y * front.y +
                                  front.z * front.z);
                    const float uLen =
                        std::sqrt(up.x * up.x +
                                  up.y * up.y +
                                  up.z * up.z);

                    if (fLen > 0.0001f && uLen > 0.0001f) {
                        front.x /= fLen;
                        front.y /= fLen;
                        front.z /= fLen;
                        up.x /= uLen;
                        up.y /= uLen;
                        up.z /= uLen;

                        CVector right;
                        right.x = front.y * up.z - front.z * up.y;
                        right.y = front.z * up.x - front.x * up.z;
                        right.z = front.x * up.y - front.y * up.x;

                        const float rLen =
                            std::sqrt(right.x * right.x +
                                      right.y * right.y +
                                      right.z * right.z);

                        if (rLen > 0.0001f) {
                            right.x /= rLen;
                            right.y /= rLen;
                            right.z /= rLen;

                            // Push the camera away from the attacker/projectile
                            // source. Only use horizontal source direction here;
                            // vertical shaping is handled independently below.
                            sideDir =
                                -Clamp(
                                    dx * right.x + dy * right.y,
                                    -1.0f,
                                    1.0f
                                ) *
                                cfg_.directionalDamageWeight;

                            forwardDir =
                                -Clamp(
                                    dx * front.x + dy * front.y,
                                    -1.0f,
                                    1.0f
                                ) *
                                cfg_.directionalDamageWeight;

                            directional = true;
                        }
                    }
                }

                verticalDir -=
                    Clamp(dz / 4.0f, -0.25f, 0.25f) *
                    cfg_.directionalDamageWeight;
            }

            // Family-specific physical shape:
            // melee = heavier body displacement,
            // bullets = sharper but shorter snap,
            // explosives = strongest whole-body displacement.
            float bodyImpulse = cfg_.damageImpulse * severity;
            float sideImpulse = cfg_.damageSideImpulse * severity;

            if (meleeHit) {
                bodyImpulse *= 1.08f;
                sideImpulse *= 1.18f;
                verticalDir -= 0.08f;
            } else if (bulletHit) {
                // Gunshots should read as an immediate sharp impact rather
                // than a slow body shove. Keep the base spring smaller and
                // add dedicated mid/high-band impulses below.
                bodyImpulse *= cfg_.incomingBulletRecovery;
                sideImpulse *= cfg_.incomingBulletRecovery;
                verticalDir = std::min(verticalDir, -0.12f);
            } else if (explosiveHit) {
                bodyImpulse *= 1.15f;
                sideImpulse *= 1.10f;
                verticalDir -= 0.16f;
            }

            impactSpring_.vertical.AddImpulse(
                bodyImpulse * verticalDir
            );
            impactSpring_.forward.AddImpulse(
                bodyImpulse * forwardDir
            );
            impactSpring_.side.AddImpulse(
                sideImpulse * sideDir
            );

            {
                cr::Vec3 tr{};
                cr::Vec3 rot{};
                rot.side=-sideDir*cfg_.directionalRotationRollDeg*severity*(bulletHit?0.42f:0.68f);
                rot.vertical=-forwardDir*cfg_.directionalRotationPitchDeg*severity*0.52f;
                rot.forward=sideDir*cfg_.directionalRotationYawDeg*severity*0.25f;
                SubmitTransientEvent(
                    cr::ResponseOrigin::Damage,
                    tr,rot,0.0f,
                    bulletHit?1.20f:1.05f,
                    bulletHit?0.50f:0.62f,
                    explosiveHit?cr::TransientPriority::Critical:cr::TransientPriority::Normal,
                    0x444D4752u
                );
            }

            if (bulletHit && cfg_.frequencyBandsEnabled) {
                // Mid-band punch gives a gunshot a short shoulder/chest hit
                // before the slower body spring has time to move.
                cr::CameraResponseEvent mid{};
                mid.source = cr::ResponseSource::Combat;
                mid.band = cr::ResponseBand::Mid;
                mid.impulse.side =
                    sideDir * cfg_.incomingBulletMidPunch;
                mid.impulse.vertical =
                    verticalDir * cfg_.incomingBulletMidPunch * 0.42f;
                mid.impulse.forward =
                    forwardDir * cfg_.incomingBulletMidPunch;
                mid.magnitude =
                    Clamp(severity, 0.45f, 1.80f);
                mid.priority = 1.45f;
                mid.tag = 0x424D4944u; // BMID
                responseMixer_.Publish(mid);
            }

            // Add a very short camera-local snap so small bullet/melee hits are
            // perceptible immediately even while larger springs are already
            // moving from gunfire, footsteps, or combat.
            if (cfg_.frequencyBandsEnabled &&
                cfg_.incomingDamageHighSnap > 0.0001f) {
                const float snapAmount =
                    bulletHit ?
                        cfg_.incomingBulletSnap :
                        cfg_.incomingDamageHighSnap;

                cr::CameraResponseEvent snap{};
                snap.source = cr::ResponseSource::Combat;
                snap.band = cr::ResponseBand::High;
                snap.impulse.side =
                    sideDir * snapAmount;
                snap.impulse.vertical =
                    verticalDir * snapAmount * 0.55f;
                snap.impulse.forward =
                    forwardDir * snapAmount;
                snap.magnitude =
                    Clamp(
                        severity * (bulletHit ? 1.20f : 1.0f),
                        0.35f,
                        bulletHit ? 1.95f : 1.65f
                    );
                snap.priority =
                    bulletHit ? 1.60f : 1.35f;
                snap.tag =
                    bulletHit ?
                        0x42534E50u : // BSNP
                        0x444D4701u;  // DMG
                responseMixer_.Publish(snap);
            }

            log_.Write(
                "PLAYER DAMAGE family=%s weapon=%d healthLoss=%.2f armourLoss=%.2f "
                "hp=%.2f armour=%.2f severity=%.3f source=%p directional=%d "
                "dir=(%.3f %.3f %.3f) bulletMid=%.3f bulletSnap=%.3f bulletRecovery=%.3f",
                family,
                weaponId,
                healthLoss,
                armourLoss,
                p->m_fHealth,
                p->m_fArmour,
                severity,
                p->m_pLastEntityDamage,
                directional ? 1 : 0,
                sideDir,
                verticalDir,
                forwardDir,
                cfg_.incomingBulletMidPunch,
                cfg_.incomingBulletSnap,
                cfg_.incomingBulletRecovery
            );
        }

        prevHealth_ = p->m_fHealth;
        prevArmour_ = p->m_fArmour;
    }

    void UpdateNearbyGunfire(CPlayerPed* player, bool inVehicle) {
        AuditHit(AUD_UpdateNearbyGunfire);
        if (!cfg_.nearbyGunfireEnabled || !player)
            return;
        if (inVehicle && !cfg_.nearbyGunfireInVehicle)
            return;

        const unsigned scanNow =
            CTimer::m_snTimeInMilliseconds;
        if (cfg_.nearbyGunfireScanMs > 0 &&
            lastNearbyGunfireScanMs_ != 0 &&
            scanNow - lastNearbyGunfireScanMs_ <
                cfg_.nearbyGunfireScanMs) {
            return;
        }
        lastNearbyGunfireScanMs_ = scanNow;
        cr::ScopedScanProfile scanScope(scanProfiler_, cr::ScanKind::NearbyGunfire, cfg_.scanProfilingEnabled);

        auto* pool = CPools::ms_pPedPool;
        if (!pool)
            return;

        const int poolSize = pool->m_nSize;
        if (poolSize <= 0)
            return;

        if (npcShotTrack_.size() != static_cast<size_t>(poolSize))
            npcShotTrack_.assign(static_cast<size_t>(poolSize), NpcShotTrack{});

        const CVector& pp = player->GetPosition();
        const float minD = std::max(0.0f, cfg_.nearbyGunfireMinDistance);
        const float maxD = std::max(minD + 0.01f, cfg_.nearbyGunfireMaxDistance);
        const float maxDSq = maxD * maxD;
        unsigned frameShots = 0;
        unsigned responseShots = 0;

        // Do not stop after an arbitrary number of NPC shots. Every detected
        // firing event in the ped pool is processed. Final camera safety is
        // still provided by the existing impact/final offset clamps.
        for (int i = 0; i < poolSize; ++i) {
            CPed* ped = pool->GetAt(i);
            NpcShotTrack& tr = npcShotTrack_[static_cast<size_t>(i)];

            if (!ped || ped == player || ped->m_fHealth <= 0.0f) {
                tr = {};
                continue;
            }

            CWeapon& weapon = ped->m_aWeapons[ped->m_nSelectedWepSlot];
            const eWeaponType type = weapon.m_eWeaponType;
            const unsigned newAmmo = weapon.m_nAmmoInClip;
            const unsigned nextShot = weapon.m_nTimeForNextShot;
            const eWeaponState state = weapon.m_nState;

            if (!WeaponSupportsCameraRecoil(type)) {
                tr.ped = ped;
                tr.weapon = type;
                tr.ammo = newAmmo;
                tr.nextShotMs = nextShot;
                tr.state = state;
                tr.initialized = true;
                continue;
            }

            if (!tr.initialized || tr.ped != ped || tr.weapon != type) {
                tr.ped = ped;
                tr.weapon = type;
                tr.ammo = newAmmo;
                tr.nextShotMs = nextShot;
                tr.state = state;
                tr.initialized = true;
                continue;
            }

            const unsigned oldAmmo = tr.ammo;
            const unsigned oldNextShot = tr.nextShotMs;
            const eWeaponState oldState = tr.state;

            tr.ammo = newAmmo;
            tr.nextShotMs = nextShot;
            tr.state = state;

            // Primary signal: clip consumption tells us exactly how many ammo
            // units were fired since the previous sample.
            unsigned rounds = oldAmmo > newAmmo ? oldAmmo - newAmmo : 0u;

            // Full-coverage fallback: some weapon/AI paths can keep clip ammo
            // unchanged (or use unusual ammo semantics). A newly advanced shot
            // cooldown while FIRING is still a real firing event. The state edge
            // fallback catches short firing pulses even if the cooldown value is
            // unchanged by the time gameProcessEvent samples it.
            const bool nextShotAdvanced =
                nextShot != 0 &&
                nextShot != oldNextShot &&
                nextShot > oldNextShot;

            const bool firingNow = state == WEAPONSTATE_FIRING;
            const bool firingEdge =
                firingNow && oldState != WEAPONSTATE_FIRING;

            if (rounds == 0 && (nextShotAdvanced || firingEdge) && firingNow)
                rounds = 1;

            // Corrupt/modded ammo values must not generate hundreds of
            // impulses in one gameProcessEvent sample.
            rounds = std::min(rounds, 8u);

            if (rounds == 0)
                continue;

            const CVector& sp = ped->GetPosition();

            if (cfg_.advancedResponseEnabled &&
                cfg_.nearMissEnabled) {

                const float toPlayerX = pp.x - sp.x;
                const float toPlayerY = pp.y - sp.y;
                const float toPlayerZ = pp.z - sp.z;
                const float shooterDistance =
                    std::sqrt(
                        toPlayerX * toPlayerX +
                        toPlayerY * toPlayerY +
                        toPlayerZ * toPlayerZ
                    );

                if (shooterDistance <= cfg_.nearMissMaxShooterDistance) {
                    CVector shotOrigin = sp;
                    shotOrigin.z += 0.70f;

                    CVector aimTarget{};
                    bool haveAimTarget = false;
                    bool taskBlockedLos = false;
                    bool taskTargetsPlayer = false;

                    CTaskSimpleUseGun* useGun =
                        ped->m_pIntelligence
                            ? ped->m_pIntelligence->GetTaskUseGun()
                            : nullptr;

                    if (useGun) {
                        taskBlockedLos = useGun->m_bBlockedLOS;

                        if (useGun->m_pTarget) {
                            taskTargetsPlayer =
                                useGun->m_pTarget == player;

                            aimTarget =
                                useGun->m_pTarget->GetPosition();

                            // The task target is an aim intention, not the
                            // actual shot trajectory/spread. If the target is
                            // CJ, this vector points exactly through CJ and
                            // therefore cannot be used to prove a hit or a
                            // near-miss (it produced lateral=0.000 for every
                            // aimed shot in the runtime log).
                            if (taskTargetsPlayer)
                                aimTarget.z += 0.70f;

                            haveAimTarget = true;
                        } else if (
                            std::fabs(useGun->m_vecTarget.x) > 0.001f ||
                            std::fabs(useGun->m_vecTarget.y) > 0.001f
                        ) {
                            aimTarget = useGun->m_vecTarget;
                            haveAimTarget = true;
                        }
                    }

                    if (!haveAimTarget) {
                        // Fallback only when no gun task target is available.
                        // This preserves support for unusual scripted firing,
                        // but normal AI fire now uses GTA's actual aim target.
                        const float sf =
                            std::sin(ped->m_fHeadingCurrent);
                        const float cf =
                            std::cos(ped->m_fHeadingCurrent);

                        aimTarget = shotOrigin;
                        aimTarget.x += sf * 100.0f;
                        aimTarget.y += cf * 100.0f;
                    }

                    float aimX = aimTarget.x - shotOrigin.x;
                    float aimY = aimTarget.y - shotOrigin.y;
                    float aimZ = aimTarget.z - shotOrigin.z;
                    const float aimLen =
                        std::sqrt(
                            aimX * aimX +
                            aimY * aimY +
                            aimZ * aimZ
                        );

                    if (aimLen > 0.001f) {
                        aimX /= aimLen;
                        aimY /= aimLen;
                        aimZ /= aimLen;
                    }

                    CVector playerAimPoint = pp;
                    playerAimPoint.z += 0.70f;

                    const float rayToPlayerX =
                        playerAimPoint.x - shotOrigin.x;
                    const float rayToPlayerY =
                        playerAimPoint.y - shotOrigin.y;
                    const float rayToPlayerZ =
                        playerAimPoint.z - shotOrigin.z;

                    const float along =
                        rayToPlayerX * aimX +
                        rayToPlayerY * aimY +
                        rayToPlayerZ * aimZ;

                    const float missX =
                        rayToPlayerX - aimX * along;
                    const float missY =
                        rayToPlayerY - aimY * along;
                    const float missZ =
                        rayToPlayerZ - aimZ * along;

                    const float lateral =
                        std::sqrt(
                            missX * missX +
                            missY * missY +
                            missZ * missZ
                        );

                    if (!taskBlockedLos &&
                        !taskTargetsPlayer &&
                        aimLen > 0.001f &&
                        along > 1.0f &&
                        lateral <= cfg_.nearMissDistance) {

                        const unsigned nowMs =
                            CTimer::m_snTimeInMilliseconds;

                        // GTA's health/armour sampling does not always expose
                        // an NPC gunshot as a same-frame PLAYER DAMAGE event in
                        // this plugin. The shot tracker already has enough
                        // information to identify a round whose aim ray passes
                        // through CJ's body corridor. Treat that as a hit-like
                        // fallback, but suppress it when a real damage event was
                        // just observed so confirmed damage never double-kicks.
                        const bool bulletWeapon =
                            (static_cast<int>(type) >= 22 &&
                             static_cast<int>(type) <= 34) ||
                            static_cast<int>(type) == 38;

                        const bool realDamageJustSeen =
                            lastConfirmedPlayerDamageMs_ != 0 &&
                            nowMs - lastConfirmedPlayerDamageMs_ < 120u;

                        bool clearWorldPath = true;
                        const float bodyRadius =
                            std::max(
                                0.08f,
                                cfg_.npcBulletHitRadius
                            );

                        const bool candidateBodyIntersection =
                            cfg_.npcBulletHitFallbackEnabled &&
                            bulletWeapon &&
                            lateral <= bodyRadius;

                        if (candidateBodyIntersection) {
                            CEntity* oldIgnore =
                                CWorld::pIgnoreEntity;

                            CWorld::pIgnoreEntity =
                                ped->m_pVehicle
                                    ? static_cast<CEntity*>(ped->m_pVehicle)
                                    : static_cast<CEntity*>(ped);

                            clearWorldPath =
                                CWorld::GetIsLineOfSightClear(
                                    shotOrigin,
                                    playerAimPoint,
                                    true,   // buildings
                                    true,   // vehicles
                                    false,  // peds
                                    true,   // objects
                                    true,   // dummies
                                    false,  // see-through
                                    false   // camera-ignore
                                );

                            CWorld::pIgnoreEntity = oldIgnore;
                        }

                        const bool bodyIntersection =
                            candidateBodyIntersection &&
                            clearWorldPath;

                        const bool fallbackReady =
                            tr.lastBodyHitMs == 0 ||
                            nowMs - tr.lastBodyHitMs >=
                                cfg_.npcBulletHitFallbackCooldownMs;

                        if (bodyIntersection &&
                            !realDamageJustSeen &&
                            fallbackReady) {

                            tr.lastBodyHitMs = nowMs;

                            // Shooter direction converted into camera-local
                            // space so a hit from screen-right pushes left,
                            // regardless of CJ's current world heading.
                            CCam& activeCam =
                                TheCamera.m_aCams[
                                    TheCamera.m_nActiveCam
                                ];

                            CVector front = activeCam.m_vecFront;
                            CVector up = activeCam.m_vecUp;

                            const float fLen =
                                std::sqrt(
                                    front.x * front.x +
                                    front.y * front.y +
                                    front.z * front.z
                                );
                            const float uLen =
                                std::sqrt(
                                    up.x * up.x +
                                    up.y * up.y +
                                    up.z * up.z
                                );

                            float hitSide = 0.0f;
                            float hitForward = -1.0f;

                            if (fLen > 0.0001f &&
                                uLen > 0.0001f) {
                                front.x /= fLen;
                                front.y /= fLen;
                                front.z /= fLen;

                                up.x /= uLen;
                                up.y /= uLen;
                                up.z /= uLen;

                                CVector right;
                                right.x =
                                    front.y * up.z -
                                    front.z * up.y;
                                right.y =
                                    front.z * up.x -
                                    front.x * up.z;
                                right.z =
                                    front.x * up.y -
                                    front.y * up.x;

                                const float rLen =
                                    std::sqrt(
                                        right.x * right.x +
                                        right.y * right.y +
                                        right.z * right.z
                                    );

                                if (rLen > 0.0001f) {
                                    right.x /= rLen;
                                    right.y /= rLen;
                                    right.z /= rLen;

                                    float sx = sp.x - pp.x;
                                    float sy = sp.y - pp.y;
                                    const float sLen =
                                        std::sqrt(
                                            sx * sx + sy * sy
                                        );

                                    if (sLen > 0.001f) {
                                        sx /= sLen;
                                        sy /= sLen;

                                        hitSide =
                                            -Clamp(
                                                sx * right.x +
                                                sy * right.y,
                                                -1.0f,
                                                1.0f
                                            );

                                        hitForward =
                                            -Clamp(
                                                sx * front.x +
                                                sy * front.y,
                                                -1.0f,
                                                1.0f
                                            );
                                    }
                                }
                            }

                            const float weaponScale =
                                Clamp(
                                    RecoilScale(type),
                                    0.55f,
                                    1.65f
                                );

                            const float hitImpulse =
                                cfg_.damageImpulse *
                                cfg_.incomingBulletDamageScale *
                                cfg_.npcBulletHitFallbackScale *
                                weaponScale *
                                (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat);

                            impactSpring_.side.AddImpulse(
                                hitSide *
                                cfg_.damageSideImpulse *
                                cfg_.incomingBulletDamageScale *
                                cfg_.npcBulletHitFallbackScale
                            );
                            impactSpring_.vertical.AddImpulse(
                                -hitImpulse * 0.24f
                            );
                            impactSpring_.forward.AddImpulse(
                                hitForward * hitImpulse
                            );

                            if (cfg_.frequencyBandsEnabled &&
                                cfg_.incomingDamageHighSnap >
                                    0.0001f) {
                                cr::CameraResponseEvent snap{};
                                snap.source =
                                    cr::ResponseSource::Combat;
                                snap.band =
                                    cr::ResponseBand::High;
                                snap.impulse.side =
                                    hitSide *
                                    cfg_.incomingDamageHighSnap;
                                snap.impulse.vertical =
                                    -cfg_.incomingDamageHighSnap *
                                    0.45f;
                                snap.impulse.forward =
                                    hitForward *
                                    cfg_.incomingDamageHighSnap;
                                snap.magnitude =
                                    1.15f * weaponScale;
                                snap.priority = 1.45f;
                                snap.tag = 0x42484954u; // BHIT
                                responseMixer_.Publish(snap);
                            }

                            if (cfg_.debug &&
                                cfg_.logShots) {
                                log_.Write(
                                    "BULLET HIT-FALLBACK weapon=%d shooterDist=%.2f lateral=%.3f "
                                    "radius=%.3f impulse=%.3f dir=(%.3f %.3f)",
                                    static_cast<int>(type),
                                    shooterDistance,
                                    lateral,
                                    cfg_.npcBulletHitRadius,
                                    hitImpulse,
                                    hitSide,
                                    hitForward
                                );
                            }
                        }

                        const float nearFactor =
                            1.0f -
                            Clamp(
                                lateral /
                                std::max(cfg_.nearMissDistance, 0.05f),
                                0.0f,
                                1.0f
                            );

                        const float impulse =
                            cfg_.nearMissStrength *
                            (0.35f + nearFactor * 0.65f) *
                            (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat);

                        float missLocalSide = 0.0f;
                        float missLocalForward = 0.0f;
                        WorldXYToCameraLocal(
                            missX,
                            missY,
                            missLocalSide,
                            missLocalForward
                        );

                        const float missSide =
                            std::fabs(missLocalSide) > 0.0001f
                                ? (missLocalSide > 0.0f ? 1.0f : -1.0f)
                                : RandomSigned();

                        for (unsigned n = 0; n < rounds; ++n) {
                            impactSpring_.side.AddImpulse(
                                missSide * impulse * 0.72f
                            );
                            impactSpring_.vertical.AddImpulse(
                                -impulse * 0.26f
                            );
                            impactSpring_.forward.AddImpulse(
                                -impulse * 0.18f
                            );
                        }

                        if (cfg_.debug && cfg_.logShots) {
                            log_.Write(
                                "BULLET NEAR-MISS weapon=%d shooterDist=%.2f lateral=%.2f rounds=%u impulse=%.3f",
                                static_cast<int>(type),
                                shooterDistance,
                                lateral,
                                rounds,
                                impulse
                            );
                        }
                    }
                }
            }
            const float dx = sp.x - pp.x;
            const float dy = sp.y - pp.y;
            const float dz = sp.z - pp.z;
            const float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq > maxDSq)
                continue;

            const float dist = std::sqrt(distSq);

            float t = 1.0f - Clamp((dist - minD) / (maxD - minD), 0.0f, 1.0f);
            t = std::pow(t, std::max(0.05f, cfg_.nearbyGunfireFalloffPower));

            const float weaponScale = Clamp(RecoilScale(type), 0.35f, 1.75f);
            const float impulse = cfg_.nearbyGunfireStrength * t * weaponScale * (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat);

            float localSide = 0.0f;
            float localForward = 0.0f;
            WorldXYToCameraLocal(
                dx,
                dy,
                localSide,
                localForward
            );
            const float sideSign =
                std::fabs(localSide) > 0.05f ?
                (localSide > 0.0f ? -1.0f : 1.0f) :
                RandomSigned();
            const float forwardSign =
                std::fabs(localForward) > 0.05f ?
                (localForward > 0.0f ? -1.0f : 1.0f) :
                RandomSigned();

            for (unsigned n = 0; n < rounds; ++n) {
                ++frameShots;
                ++nearbyGunfireShots_;

                if (responseShots >=
                    cfg_.nearbyGunfireMaxResponseShotsPerFrame) {
                    continue;
                }

                const float jitter =
                    0.88f + 0.24f * (0.5f + 0.5f * RandomSigned());

                TouchResponseSource(
                    cr::ResponseOrigin::Gunfire,
                    impulse,
                    550
                );

                impactSpring_.side.AddImpulse(
                    sideSign * impulse * cfg_.nearbyGunfireSide * jitter
                );
                impactSpring_.vertical.AddImpulse(
                    -impulse * cfg_.nearbyGunfireVertical * jitter
                );
                impactSpring_.forward.AddImpulse(
                    forwardSign * impulse * cfg_.nearbyGunfireForward * jitter
                );

                ++responseShots;
            }

            lastNearbyGunfireDistance_ = dist;
            lastNearbyGunfireStrength_ = impulse;

            if (cfg_.debug && cfg_.logShots) {
                log_.Write(
                    "NPC GUNFIRE weapon=%d dist=%.2f falloff=%.3f impulse=%.3f rounds=%u full=1",
                    static_cast<int>(type),
                    dist,
                    t,
                    impulse,
                    rounds
                );
            }
        }

        if (cfg_.debug && cfg_.logShots && frameShots > 1) {
            log_.Write(
                "NPC GUNFIRE FRAME totalShots=%u responseShots=%u cap=%u allTracked=1",
                frameShots,
                responseShots,
                cfg_.nearbyGunfireMaxResponseShotsPerFrame
            );
        }
    }



    void BeginEnvironmentFrame() {
        AuditHit(AUD_BeginEnvironmentFrame);
        environmentEventCount_ = 0;
        responseMixer_.BeginFrame();
        mixedEventOffset_ = {};
    }

    void PublishResponseEvent(
        cr::ResponseSource source,
        cr::ResponseBand band,
        float side,
        float vertical,
        float forward,
        float magnitude = 1.0f,
        float priority = 1.0f,
        unsigned tag = 0
    ) {
        cr::CameraResponseEvent event{};
        event.source = source;
        event.band = band;
        event.impulse.side = side;
        event.impulse.vertical = vertical;
        event.impulse.forward = forward;
        event.magnitude = magnitude;
        event.priority = priority;
        event.tag = tag;
        responseMixer_.Publish(event);
    }

    void QueueEnvironmentImpulse(
        float side,
        float vertical,
        float forward,
        float priority = 1.0f
    ) {
        side *= (cfg_.environmentResponseScale * cfg_.masterWorldScale * cfg_.presetWorld);
        vertical *= (cfg_.environmentResponseScale * cfg_.masterWorldScale * cfg_.presetWorld);
        forward *= (cfg_.environmentResponseScale * cfg_.masterWorldScale * cfg_.presetWorld);

        const float magnitude =
            std::sqrt(side * side + vertical * vertical + forward * forward);

        if (magnitude <= 0.00001f)
            return;

        if (cfg_.frequencyBandsEnabled) {
            PublishResponseEvent(
                cr::ResponseSource::Environment,
                cr::ResponseBand::Mid,
                side,
                vertical,
                forward,
                1.0f,
                std::max(priority, 0.01f)
            );
            return;
        }

        if (!cfg_.environmentPriorityMixEnabled) {
            impactSpring_.side.AddImpulse(side);
            impactSpring_.vertical.AddImpulse(vertical);
            impactSpring_.forward.AddImpulse(forward);
            return;
        }

        EnvironmentEvent event{};
        event.side = side;
        event.vertical = vertical;
        event.forward = forward;
        event.score = magnitude * std::max(priority, 0.01f);

        if (environmentEventCount_ < 8) {
            environmentEvents_[environmentEventCount_++] = event;
            return;
        }

        unsigned weakest = 0;
        for (unsigned i = 1; i < 8; ++i) {
            if (environmentEvents_[i].score < environmentEvents_[weakest].score)
                weakest = i;
        }

        if (event.score > environmentEvents_[weakest].score)
            environmentEvents_[weakest] = event;
    }

    void CommitEnvironmentFrame() {
        AuditHit(AUD_CommitEnvironmentFrame);
        if (cfg_.frequencyBandsEnabled) {
            mixedEventOffset_ = responseMixer_.Resolve(
                cfg_.lowFrequencyScale,
                cfg_.midFrequencyScale,
                cfg_.highFrequencyBandScale * cfg_.highFrequencyScale,
                std::max(cfg_.environmentMixMaxImpulse, 0.05f)
            );

            const float mixedMagnitude =
                std::fabs(mixedEventOffset_.side) +
                std::fabs(mixedEventOffset_.vertical) +
                std::fabs(mixedEventOffset_.forward);

            if (mixedMagnitude > 0.00001f) {
                TouchResponseSource(
                    cr::ResponseOrigin::WorldObject,
                    mixedMagnitude,
                    500
                );
            }

            impactSpring_.side.AddImpulse(mixedEventOffset_.side);
            impactSpring_.vertical.AddImpulse(mixedEventOffset_.vertical);
            impactSpring_.forward.AddImpulse(mixedEventOffset_.forward);
            return;
        }

        if (!cfg_.environmentPriorityMixEnabled || environmentEventCount_ == 0)
            return;

        float side = 0.0f;
        float vertical = 0.0f;
        float forward = 0.0f;

        for (unsigned i = 0; i < environmentEventCount_; ++i) {
            side += environmentEvents_[i].side;
            vertical += environmentEvents_[i].vertical;
            forward += environmentEvents_[i].forward;
        }

        const float magnitude =
            std::sqrt(side * side + vertical * vertical + forward * forward);
        const float maxImpulse =
            std::max(cfg_.environmentMixMaxImpulse, 0.05f);

        if (magnitude > maxImpulse) {
            const float scale = maxImpulse / magnitude;
            side *= scale;
            vertical *= scale;
            forward *= scale;
        }

        impactSpring_.side.AddImpulse(side);
        impactSpring_.vertical.AddImpulse(vertical);
        impactSpring_.forward.AddImpulse(forward);
    }

    void SourceDirectionToCameraLocal(
        CPlayerPed* player,
        const CVector& source,
        float& awaySide,
        float& awayVertical,
        float& awayForward
    ) const {
        awaySide = 0.0f;
        awayVertical = 0.0f;
        awayForward = -1.0f;

        if (!player || !cfg_.directionalResponseEnabled)
            return;

        const CVector& pp = player->GetPosition();
        const float dx = source.x - pp.x;
        const float dy = source.y - pp.y;
        const float dz = source.z - pp.z;

        float localSide = 0.0f;
        float localForward = 0.0f;
        WorldXYToCameraLocal(dx, dy, localSide, localForward);

        const float len = std::sqrt(
            localSide * localSide +
            localForward * localForward +
            dz * dz
        );

        if (len <= 0.001f)
            return;

        awaySide = Clamp(-localSide / len, -1.0f, 1.0f);
        awayForward = Clamp(-localForward / len, -1.0f, 1.0f);
        awayVertical = Clamp(-dz / len, -1.0f, 1.0f);
    }

    void VelocityToCameraLocal(
        const CVector& velocity,
        float& side,
        float& vertical,
        float& forward
    ) const {
        WorldXYToCameraLocal(
            velocity.x,
            velocity.y,
            side,
            forward
        );
        vertical = velocity.z;
    }

    void QueueSourceImpulse(
        CPlayerPed* player,
        const CVector& source,
        float strength,
        float maxDistance,
        float sideScale,
        float verticalScale,
        float forwardScale,
        float priority
    ) {
        if (!player || strength <= 0.0f)
            return;

        const CVector& pp = player->GetPosition();
        const float dx = source.x - pp.x;
        const float dy = source.y - pp.y;
        const float dz = source.z - pp.z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        const float maxD = std::max(maxDistance, 0.1f);

        if (distSq > maxD * maxD)
            return;

        const float dist = std::sqrt(distSq);
        const float falloff =
            1.0f - Clamp(dist / maxD, 0.0f, 1.0f);

        float awaySide = 0.0f;
        float awayVertical = 0.0f;
        float awayForward = -1.0f;

        SourceDirectionToCameraLocal(
            player,
            source,
            awaySide,
            awayVertical,
            awayForward
        );

        const float impulse =
            strength *
            falloff *
            cfg_.strength *
            cfg_.presetScale;

        QueueEnvironmentImpulse(
            awaySide *
                impulse *
                sideScale *
                cfg_.directionalSourceSide,
            -impulse * verticalScale +
                awayVertical *
                impulse *
                verticalScale *
                cfg_.directionalSourceVertical,
            awayForward *
                impulse *
                forwardScale *
                cfg_.directionalSourceForward,
            priority
        );
    }

    void ApplyDoorCloseImpulse(
        CPlayerPed* player,
        const CVector& source,
        float sourceScale,
        float closeSpeed,
        bool vehicleDoor,
        bool ownVehicle
    ) {
        AuditHit(AUD_ApplyDoorCloseImpulse);
        if (!player)
            return;

        const CVector& pp = player->GetPosition();
        const float dx = source.x - pp.x;
        const float dy = source.y - pp.y;
        const float dz = source.z - pp.z;
        const float distSq = dx * dx + dy * dy + dz * dz;

        const float minD = std::max(0.0f, cfg_.doorResponseMinDistance);
        const float maxD = std::max(minD + 0.01f, cfg_.doorResponseMaxDistance);
        if (!ownVehicle && distSq > maxD * maxD)
            return;

        const float dist = ownVehicle ? 0.0f : std::sqrt(distSq);
        float falloff = ownVehicle
            ? 1.0f
            : 1.0f - Clamp((dist - minD) / (maxD - minD), 0.0f, 1.0f);

        falloff = std::pow(
            falloff,
            std::max(0.05f, cfg_.doorResponseFalloffPower)
        );

        if (falloff <= 0.0f)
            return;

        const float speedScale =
            Clamp(0.65f + closeSpeed * 0.22f, 0.65f, 1.35f);

        float impulse =
            cfg_.doorResponseStrength *
            sourceScale *
            falloff *
            speedScale *
            cfg_.strength *
            cfg_.presetScale;

        if (ownVehicle)
            impulse *= cfg_.doorVehicleOwnScale;

        float localSide = 0.0f;
        float localForward = 0.0f;
        WorldXYToCameraLocal(
            dx,
            dy,
            localSide,
            localForward
        );

        float awaySide = 0.0f;
        float awayVertical = 0.0f;
        float awayForward = -1.0f;
        SourceDirectionToCameraLocal(
            player,
            source,
            awaySide,
            awayVertical,
            awayForward
        );

        if (ownVehicle && std::fabs(awaySide) < 0.08f) {
            awaySide = localSide >= 0.0f ? -0.32f : 0.32f;
        }

        const float directional =
            cfg_.directionalResponseEnabled
                ? cfg_.directionalDoorScale
                : 1.0f;

        TouchResponseSource(
            cr::ResponseOrigin::Door,
            impulse,
            600
        );

        QueueEnvironmentImpulse(
            awaySide * impulse * cfg_.doorResponseSide * directional,
            -impulse * cfg_.doorResponseVertical +
                awayVertical * impulse * cfg_.doorResponseVertical *
                0.18f * directional,
            awayForward * impulse * cfg_.doorResponseForward * directional,
            ownVehicle ? 1.35f : 1.05f
        );

        // A door slam is a short structural thump. Publish a compact mid-band
        // layer as well so it cannot disappear behind other environmental
        // contributors in a busy frame.
        if (cfg_.frequencyBandsEnabled) {
            cr::CameraResponseEvent doorEvent{};
            doorEvent.source = cr::ResponseSource::Environment;
            doorEvent.band = cr::ResponseBand::Mid;
            doorEvent.impulse.side =
                awaySide * impulse * cfg_.doorResponseSide * directional * 0.40f;
            doorEvent.impulse.vertical =
                -impulse * cfg_.doorResponseVertical * 0.32f;
            doorEvent.impulse.forward =
                awayForward * impulse * cfg_.doorResponseForward * directional * 0.48f;
            doorEvent.magnitude =
                Clamp(impulse * 2.6f, 0.18f, 1.30f);
            doorEvent.priority =
                ownVehicle ? 1.55f : 1.20f;
            doorEvent.tag =
                vehicleDoor ? 0x56444F52u : 0x42444F52u; // VDOR / BDOR
            responseMixer_.Publish(doorEvent);
        }

        lastDoorDistance_ = dist;
        lastDoorStrength_ = impulse;

        if (vehicleDoor)
            ++vehicleDoorSlams_;
        else
            ++buildingDoorSlams_;

        if (cfg_.debug) {
            log_.Write(
                "DOOR IMPULSE type=%s ownVehicle=%d dist=%.2f speed=%.2f "
                "falloff=%.3f impulse=%.3f",
                vehicleDoor ? "vehicle" : "building",
                ownVehicle ? 1 : 0,
                dist,
                closeSpeed,
                falloff,
                impulse
            );
        }
    }

    void UpdateDoorResponses(
        CPlayerPed* player,
        bool inVehicle,
        CVehicle* ownVehicle
    ) {
        AuditHit(AUD_UpdateDoorResponses);
        if (!cfg_.doorResponseEnabled || !player)
            return;

        if (inVehicle && !cfg_.doorResponseInVehicle)
            return;

        const unsigned now = CTimer::m_snTimeInMilliseconds;
        if (lastDoorScanMs_ != 0 &&
            now - lastDoorScanMs_ < cfg_.doorScanMs) {
            return;
        }

        const float scanDt =
            lastDoorScanMs_ != 0
                ? std::max(float(now - lastDoorScanMs_) * 0.001f, 0.001f)
                : std::max(float(cfg_.doorScanMs) * 0.001f, 0.001f);

        lastDoorScanMs_ = now;
        cr::ScopedScanProfile scanScope(scanProfiler_, cr::ScanKind::Doors, cfg_.scanProfilingEnabled);
        const CVector& pp = player->GetPosition();
        const float maxD = std::max(
            cfg_.doorResponseMinDistance + 0.01f,
            cfg_.doorResponseMaxDistance
        );
        const float maxDSq = maxD * maxD;

        if (cfg_.vehicleDoorResponseEnabled) {
            auto* pool = CPools::ms_pVehiclePool;
            if (pool && pool->m_nSize > 0) {
                const int poolSize = pool->m_nSize;

                if (vehicleDoorTrack_.size() != static_cast<size_t>(poolSize)) {
                    vehicleDoorTrack_.assign(
                        static_cast<size_t>(poolSize),
                        VehicleDoorTrack{}
                    );
                }

                for (int i = 0; i < poolSize; ++i) {
                    CVehicle* v = pool->GetAt(i);
                    VehicleDoorTrack& tr =
                        vehicleDoorTrack_[static_cast<size_t>(i)];

                    if (!v ||
                        v->m_fHealth <= 0.0f ||
                        v->GetVehicleAppearance() != VEHICLE_APPEARANCE_AUTOMOBILE) {
                        tr = {};
                        continue;
                    }

                    const bool isOwn = v == ownVehicle;
                    const CVector& vp = v->GetPosition();
                    const float dx = vp.x - pp.x;
                    const float dy = vp.y - pp.y;
                    const float dz = vp.z - pp.z;
                    const float distSq = dx * dx + dy * dy + dz * dz;

                    if (!isOwn && distSq > maxDSq) {
                        tr = {};
                        continue;
                    }

                    CAutomobile* automobile =
                        static_cast<CAutomobile*>(v);

                    float current[6] = {};
                    bool closed[6] = {};
                    bool movingNow[6] = {};
                    float angularSpeed[6] = {};

                    for (unsigned door = 0; door < 6; ++door) {
                        CDoor& gtaDoor = automobile->m_doors[door];

                        current[door] = Clamp(
                            gtaDoor.GetAngleOpenRatio(),
                            0.0f,
                            1.5f
                        );

                        closed[door] =
                            gtaDoor.IsClosed() ||
                            current[door] <=
                                cfg_.vehicleDoorCloseThreshold;

                        angularSpeed[door] =
                            std::fabs(gtaDoor.m_fAngVel);

                        movingNow[door] =
                            angularSpeed[door] > 0.0025f ||
                            gtaDoor.m_nDoorState == DOOR_POP_OPEN ||
                            gtaDoor.m_nDoorState == DOOR_SLAM_SHUT;
                    }

                    if (!tr.initialized || tr.vehicle != v) {
                        tr = {};
                        tr.vehicle = v;

                        for (unsigned door = 0; door < 6; ++door) {
                            tr.ratio[door] = current[door];
                            tr.latchedOpen[door] =
                                current[door] >=
                                    cfg_.vehicleDoorWasOpenThreshold;
                            tr.wasClosed[door] = closed[door];
                            tr.wasMoving[door] = movingNow[door];
                        }

                        tr.initialized = true;
                        continue;
                    }

                    for (unsigned door = 0; door < 6; ++door) {
                        const float previous = tr.ratio[door];
                        const float currentRatio = current[door];

                        if (currentRatio >=
                                cfg_.vehicleDoorWasOpenThreshold ||
                            automobile->m_doors[door].m_nDoorState ==
                                DOOR_POP_OPEN) {
                            tr.latchedOpen[door] = true;
                        }

                        const float closingDelta =
                            std::max(
                                0.0f,
                                previous - currentRatio
                            );

                        const float openingDelta =
                            std::max(
                                0.0f,
                                currentRatio - previous
                            );

                        // CDoor::m_fAngVel is a better speed signal than a
                        // 40 ms ratio delta, but retain the delta fallback for
                        // scripted doors whose physics velocity is zeroed.
                        const float closeSpeed =
                            std::max(
                                angularSpeed[door],
                                closingDelta / scanDt
                            );

                        const float openSpeed =
                            std::max(
                                angularSpeed[door],
                                openingDelta / scanDt
                            );

                        const bool closeTransition =
                            tr.latchedOpen[door] &&
                            currentRatio <=
                                cfg_.vehicleDoorCloseThreshold;

                        const bool slamState =
                            automobile->m_doors[door].m_nDoorState ==
                                DOOR_SLAM_SHUT &&
                            tr.latchedOpen[door];

                        const bool movementFinishedClosed =
                            tr.latchedOpen[door] &&
                            tr.wasMoving[door] &&
                            !movingNow[door] &&
                            currentRatio <=
                                cfg_.vehicleDoorCloseThreshold;

                        const bool eventReady =
                            tr.lastEventMs[door] == 0 ||
                            now - tr.lastEventMs[door] >= 350u;

                        if (eventReady &&
                            (closeTransition ||
                             slamState ||
                             movementFinishedClosed)) {

                            float componentScale =
                                cfg_.vehicleDoorStrength;

                            if (door == BONNET)
                                componentScale *=
                                    cfg_.doorBonnetScale;
                            else if (door == BOOT)
                                componentScale *=
                                    cfg_.doorBootScale;

                            // Use the actual door/component location rather
                            // than vehicle centre so left/right door reactions
                            // remain directional.
                            static const int kDoorNodes[6] = {
                                CAR_BONNET,
                                CAR_BOOT,
                                CAR_DOOR_LF,
                                CAR_DOOR_RF,
                                CAR_DOOR_LR,
                                CAR_DOOR_RR
                            };

                            CVector source = vp;
                            if (v->IsComponentPresent(
                                    kDoorNodes[door])) {
                                v->GetComponentWorldPosition(
                                    kDoorNodes[door],
                                    source
                                );
                            }

                            ApplyDoorCloseImpulse(
                                player,
                                source,
                                componentScale,
                                std::max(closeSpeed, 0.35f),
                                true,
                                isOwn
                            );

                            tr.lastEventMs[door] = now;
                            tr.latchedOpen[door] = false;

                            if (cfg_.debug) {
                                log_.Write(
                                    "VEHICLE DOOR EVENT action=close door=%u ratio=%.3f "
                                    "prev=%.3f angVel=%.4f state=%u closed=%d",
                                    door,
                                    currentRatio,
                                    previous,
                                    automobile->m_doors[door].m_fAngVel,
                                    static_cast<unsigned>(
                                        automobile->m_doors[door].m_nDoorState
                                    ),
                                    closed[door] ? 1 : 0
                                );
                            }
                        }

                        const bool openingStarted =
                            !tr.latchedOpen[door] &&
                            (
                                currentRatio >=
                                    cfg_.vehicleDoorWasOpenThreshold ||
                                automobile->m_doors[door].m_nDoorState ==
                                    DOOR_POP_OPEN
                            ) &&
                            (
                                openingDelta > 0.005f ||
                                movingNow[door]
                            );

                        if (cfg_.advancedResponseEnabled &&
                            eventReady &&
                            openingStarted) {

                            static const int kDoorNodes[6] = {
                                CAR_BONNET,
                                CAR_BOOT,
                                CAR_DOOR_LF,
                                CAR_DOOR_RF,
                                CAR_DOOR_LR,
                                CAR_DOOR_RR
                            };

                            CVector source = vp;
                            if (v->IsComponentPresent(
                                    kDoorNodes[door])) {
                                v->GetComponentWorldPosition(
                                    kDoorNodes[door],
                                    source
                                );
                            }

                            float componentScale =
                                cfg_.vehicleDoorStrength *
                                cfg_.doorOpenStrength;

                            if (door == BONNET)
                                componentScale *=
                                    cfg_.doorBonnetScale;
                            else if (door == BOOT)
                                componentScale *=
                                    cfg_.doorBootScale;

                            ApplyDoorCloseImpulse(
                                player,
                                source,
                                componentScale,
                                std::max(openSpeed, 0.25f),
                                true,
                                isOwn
                            );

                            tr.lastEventMs[door] = now;
                            tr.latchedOpen[door] = true;

                            if (cfg_.debug) {
                                log_.Write(
                                    "VEHICLE DOOR EVENT action=open door=%u ratio=%.3f "
                                    "prev=%.3f angVel=%.4f state=%u",
                                    door,
                                    currentRatio,
                                    previous,
                                    automobile->m_doors[door].m_fAngVel,
                                    static_cast<unsigned>(
                                        automobile->m_doors[door].m_nDoorState
                                    )
                                );
                            }
                        }

                        tr.ratio[door] = currentRatio;
                        tr.wasClosed[door] = closed[door];
                        tr.wasMoving[door] = movingNow[door];
                    }
                }
            }
        }

        if (cfg_.buildingDoorResponseEnabled) {
            auto* pool = CPools::ms_pObjectPool;
            if (pool && pool->m_nSize > 0) {
                const int poolSize = pool->m_nSize;

                if (objectDoorTrack_.size() != static_cast<size_t>(poolSize)) {
                    objectDoorTrack_.assign(
                        static_cast<size_t>(poolSize),
                        ObjectDoorTrack{}
                    );
                }

                for (int i = 0; i < poolSize; ++i) {
                    CObject* object = pool->GetAt(i);
                    ObjectDoorTrack& tr =
                        objectDoorTrack_[static_cast<size_t>(i)];

                    if (!object) {
                        tr = {};
                        continue;
                    }

                    const CVector& op = object->GetPosition();
                    const float dx = op.x - pp.x;
                    const float dy = op.y - pp.y;
                    const float dz = op.z - pp.z;
                    const float distSq = dx * dx + dy * dy + dz * dz;

                    if (distSq > maxDSq) {
                        tr = {};
                        continue;
                    }

                    const bool movingFlag =
                        object->m_nObjectFlags.bIsDoorMoving;
                    const bool open =
                        object->m_nObjectFlags.bbIsDoorOpen;

                    const float motion =
                        std::sqrt(
                            object->m_vecMoveSpeed.x * object->m_vecMoveSpeed.x +
                            object->m_vecMoveSpeed.y * object->m_vecMoveSpeed.y +
                            object->m_vecMoveSpeed.z * object->m_vecMoveSpeed.z +
                            object->m_vecTurnSpeed.x * object->m_vecTurnSpeed.x +
                            object->m_vecTurnSpeed.y * object->m_vecTurnSpeed.y +
                            object->m_vecTurnSpeed.z * object->m_vecTurnSpeed.z
                        );

                    const bool motionActive =
                        motion > 0.0015f;

                    if (!tr.initialized || tr.object != object) {
                        tr = {};
                        tr.object = object;
                        tr.moving = movingFlag;
                        tr.open = open;
                        tr.doorCandidate =
                            movingFlag || open;
                        tr.motionActive = motionActive;
                        tr.motion = motion;
                        tr.peakMotion = motion;
                        tr.initialized = true;
                        continue;
                    }

                    // Once GTA identifies an object as a door, keep that
                    // classification latched. bIsDoorMoving is transient and
                    // is commonly cleared before the final closed sample.
                    if (movingFlag || open)
                        tr.doorCandidate = true;

                    tr.peakMotion =
                        motionActive
                            ? std::max(tr.peakMotion, motion)
                            : tr.peakMotion;

                    const bool explicitClosed =
                        tr.open && !open;

                    const bool movementFinished =
                        tr.doorCandidate &&
                        tr.motionActive &&
                        !motionActive;

                    const bool finishedClosing =
                        movementFinished &&
                        !open;

                    const bool eventReady =
                        tr.lastEventMs == 0 ||
                        now - tr.lastEventMs >= 150u;

                    if (eventReady &&
                        (explicitClosed || finishedClosing)) {
                        const float closeSpeed =
                            Clamp(
                                std::max(
                                    tr.peakMotion,
                                    tr.motion
                                ) * 8.0f,
                                0.30f,
                                2.5f
                            );

                        ApplyDoorCloseImpulse(
                            player,
                            op,
                            cfg_.buildingDoorStrength,
                            closeSpeed,
                            false,
                            false
                        );

                        tr.lastEventMs = now;
                        tr.peakMotion = 0.0f;

                        if (cfg_.debug) {
                            log_.Write(
                                "BUILDING DOOR EVENT action=close model=%d movingFlag=%d "
                                "open=%d motion=%.4f peak=%.4f",
                                object->m_nModelIndex,
                                movingFlag ? 1 : 0,
                                open ? 1 : 0,
                                motion,
                                tr.peakMotion
                            );
                        }
                    }

                    const bool openingFlag =
                        !tr.open && open;

                    const bool movementStarted =
                        tr.doorCandidate &&
                        !tr.motionActive &&
                        motionActive &&
                        open;

                    if (cfg_.advancedResponseEnabled &&
                        eventReady &&
                        (openingFlag || movementStarted)) {
                        ApplyDoorCloseImpulse(
                            player,
                            op,
                            cfg_.buildingDoorStrength *
                            cfg_.doorOpenStrength,
                            Clamp(
                                std::max(motion, 0.04f) * 5.0f,
                                0.25f,
                                1.5f
                            ),
                            false,
                            false
                        );

                        tr.lastEventMs = now;

                        if (cfg_.debug) {
                            log_.Write(
                                "BUILDING DOOR EVENT action=open model=%d movingFlag=%d "
                                "open=%d motion=%.4f",
                                object->m_nModelIndex,
                                movingFlag ? 1 : 0,
                                open ? 1 : 0,
                                motion
                            );
                        }
                    }

                    tr.object = object;
                    tr.moving = movingFlag;
                    tr.open = open;
                    tr.motionActive = motionActive;
                    tr.motion = motion;
                    if (!motionActive && !open)
                        tr.peakMotion = 0.0f;
                    tr.initialized = true;
                }
            }
        }
    }


    void UpdatePlayerVehicleTransition(
        CPlayerPed* player,
        bool inVehicle,
        CVehicle* vehicle
    ) {
        AuditHit(AUD_UpdatePlayerVehicleTransition);
        if (!player)
            return;

        const unsigned now =
            CTimer::m_snTimeInMilliseconds;

        if (!observedPlayerInVehicle_ && inVehicle) {
            vehicleBailCandidate_ = false;
            vehicleBailActive_ = false;
            vehicleBailCandidateUntilMs_ = 0;
            vehicleBailLandingWorstVz_ = 0.0f;

            if (cfg_.vehicleMechanicalEnabled) {
                float enterSide = 0.0f;
                float enterVertical = 0.0f;
                float enterForward = 0.0f;
                if (vehicle) {
                    VelocityToCameraLocal(
                        vehicle->m_vecMoveSpeed,
                        enterSide,
                        enterVertical,
                        enterForward
                    );
                }

                const float enterHorizontal = std::sqrt(
                    enterSide * enterSide +
                    enterForward * enterForward
                );

                const float directional =
                    cfg_.directionalResponseEnabled
                        ? cfg_.directionalVehicleTransitionScale
                        : 0.0f;

                QueueEnvironmentImpulse(
                    enterHorizontal > 0.001f
                        ? (-enterSide / enterHorizontal) *
                          cfg_.vehicleEnterStrength * 0.14f * directional
                        : 0.0f,
                    -cfg_.vehicleEnterStrength * 0.42f,
                    cfg_.vehicleEnterStrength * 0.48f +
                        (enterHorizontal > 0.001f
                            ? (-enterForward / enterHorizontal) *
                              cfg_.vehicleEnterStrength * 0.14f * directional
                            : 0.0f),
                    1.25f
                );

                if (cfg_.debug) {
                    log_.Write(
                        "PLAYER VEHICLE ENTER response=%.3f",
                        cfg_.vehicleEnterStrength
                    );
                }
            }
        } else if (observedPlayerInVehicle_ && !inVehicle) {
            CVehicle* exitedVehicle =
                observedPlayerVehicle_;

            float vehicleSpeed = 0.0f;
            CVector exitVelocity =
                player->m_vecMoveSpeed;

            if (exitedVehicle) {
                const CVector vv =
                    exitedVehicle->m_vecMoveSpeed;

                vehicleSpeed =
                    std::sqrt(
                        vv.x * vv.x +
                        vv.y * vv.y +
                        vv.z * vv.z
                    );

                // Prefer the faster retained motion source. GTA can update the
                // player velocity one frame before/after clearing bInVehicle.
                const float playerSpeed =
                    std::sqrt(
                        exitVelocity.x * exitVelocity.x +
                        exitVelocity.y * exitVelocity.y +
                        exitVelocity.z * exitVelocity.z
                    );

                if (vehicleSpeed > playerSpeed)
                    exitVelocity = vv;
            } else {
                vehicleSpeed =
                    std::sqrt(
                        exitVelocity.x * exitVelocity.x +
                        exitVelocity.y * exitVelocity.y +
                        exitVelocity.z * exitVelocity.z
                    );
            }

            if (cfg_.vehicleMechanicalEnabled) {
                float exitSide = 0.0f;
                float exitVertical = 0.0f;
                float exitForward = 0.0f;
                VelocityToCameraLocal(
                    exitVelocity,
                    exitSide,
                    exitVertical,
                    exitForward
                );

                const float exitHorizontal = std::sqrt(
                    exitSide * exitSide +
                    exitForward * exitForward
                );

                const float directional =
                    cfg_.directionalResponseEnabled
                        ? cfg_.directionalVehicleTransitionScale
                        : 0.0f;

                QueueEnvironmentImpulse(
                    exitHorizontal > 0.001f
                        ? (-exitSide / exitHorizontal) *
                          cfg_.vehicleExitStrength * 0.18f * directional
                        : 0.0f,
                    cfg_.vehicleExitStrength * 0.30f,
                    -cfg_.vehicleExitStrength * 0.42f +
                        (exitHorizontal > 0.001f
                            ? (-exitForward / exitHorizontal) *
                              cfg_.vehicleExitStrength * 0.18f * directional
                            : 0.0f),
                    1.15f
                );

                if (cfg_.debug) {
                    log_.Write(
                        "PLAYER VEHICLE EXIT response=%.3f speed=%.3f airborne=%d",
                        cfg_.vehicleExitStrength,
                        vehicleSpeed,
                        player->bIsInTheAir ? 1 : 0
                    );
                }
            }

            if (cfg_.vehicleBailOutEnabled &&
                vehicleSpeed >=
                    cfg_.vehicleBailOutMinSpeed) {
                vehicleBailCandidate_ = true;
                vehicleBailActive_ = false;
                vehicleBailCandidateUntilMs_ =
                    now +
                    cfg_.vehicleBailAirborneWindowMs;
                vehicleBailExitSpeed_ =
                    vehicleSpeed;
                vehicleBailExitVelocity_ =
                    exitVelocity;
                vehicleBailLandingWorstVz_ =
                    std::min(
                        0.0f,
                        player->m_vecMoveSpeed.z
                    );

                // Many bail-outs are already airborne on the same frame GTA
                // clears bInVehicle. Confirm immediately in that case.
                if (player->bIsInTheAir) {
                    vehicleBailActive_ = true;
                    vehicleBailCandidate_ = false;
                    vehicleBailStartedMs_ = now;

                    float localSide = 0.0f;
                    float localForward = 0.0f;

                    WorldXYToCameraLocal(
                        exitVelocity.x,
                        exitVelocity.y,
                        localSide,
                        localForward
                    );

                    const float speedScale =
                        Clamp(
                            vehicleSpeed /
                            std::max(
                                cfg_.vehicleBailOutMinSpeed,
                                0.05f
                            ),
                            1.0f,
                            3.0f
                        );

                    const float strength =
                        cfg_.vehicleBailOutStrength *
                        std::sqrt(speedScale);

                    impactSpring_.side.AddImpulse(
                        Clamp(
                            localSide * 0.90f,
                            -0.30f,
                            0.30f
                        ) *
                        strength
                    );

                    impactSpring_.vertical.AddImpulse(
                        0.16f *
                        strength
                    );

                    impactSpring_.forward.AddImpulse(
                        Clamp(
                            -localForward * 0.55f,
                            -0.34f,
                            0.34f
                        ) *
                        strength
                    );

                    AddFovKick(
                        cfg_.vehicleBailFovKick *
                        Clamp(speedScale, 1.0f, 2.0f)
                    );

                    if (cfg_.debug) {
                        log_.Write(
                            "VEHICLE BAIL START source=exit-frame speed=%.3f "
                            "vel=(%.3f %.3f %.3f) strength=%.3f",
                            vehicleSpeed,
                            exitVelocity.x,
                            exitVelocity.y,
                            exitVelocity.z,
                            strength
                        );
                    }
                } else if (cfg_.debug) {
                    log_.Write(
                        "VEHICLE BAIL CANDIDATE speed=%.3f windowMs=%u",
                        vehicleSpeed,
                        cfg_.vehicleBailAirborneWindowMs
                    );
                }
            }
        }

        observedPlayerInVehicle_ = inVehicle;
        observedPlayerVehicle_ = vehicle;
    }

    void UpdateAdvancedPlayerActions(CPlayerPed* player, float dt) {
        AuditHit(AUD_UpdateAdvancedPlayerActions);
        advancedPlayerSide_ = 0.0f;
        advancedPlayerVertical_ = 0.0f;
        advancedPlayerForward_ = 0.0f;

        if (!cfg_.advancedResponseEnabled || !player || !player->m_pIntelligence)
            return;

        CPedIntelligence* intel = player->m_pIntelligence;
        const bool swimming = intel->GetTaskSwim() != nullptr;
        const bool climbing = intel->GetTaskClimb() != nullptr;
        const bool parachute = intel->GetUsingParachute();
        const bool jetpack = intel->GetTaskJetPack() != nullptr;

        const float t =
            static_cast<float>(CTimer::m_snTimeInMilliseconds) * 0.001f;

        if (swimming) {
            const float speed = HorizontalSpeed(player);
            const float amp =
                cfg_.swimResponseStrength *
                Clamp(0.45f + speed * 4.0f, 0.45f, 1.25f) *
                (cfg_.locomotionResponseScale *
                 cfg_.masterMovementScale *
                 cfg_.presetMovement);

            float swimSide = 0.0f;
            float swimVertical = 0.0f;
            float swimForward = 0.0f;
            VelocityToCameraLocal(
                player->m_vecMoveSpeed,
                swimSide,
                swimVertical,
                swimForward
            );

            const float horizontal = std::sqrt(
                swimSide * swimSide +
                swimForward * swimForward
            );

            const float directional =
                cfg_.directionalResponseEnabled
                    ? cfg_.directionalSwimScale
                    : 0.0f;

            if (horizontal > 0.001f) {
                advancedPlayerSide_ +=
                    (-swimSide / horizontal) *
                    amp * 0.22f * directional;
                advancedPlayerForward_ +=
                    (-swimForward / horizontal) *
                    amp * 0.30f * directional;
            }

            // Stroke cadence remains mostly vertical; travel direction now
            // comes from the actual swim velocity above.
            advancedPlayerSide_ +=
                std::sin(t * 4.4f * 2.0f * kPi) *
                amp * 0.16f;
            advancedPlayerVertical_ +=
                std::sin(t * 2.2f * 2.0f * kPi + 0.8f) *
                amp * 0.60f;
            advancedPlayerForward_ +=
                std::sin(t * 2.2f * 2.0f * kPi + 2.0f) *
                amp * 0.18f;
        }

        if (swimming && !previousSwimming_) {
            impactSpring_.vertical.AddImpulse(
                -cfg_.swimResponseStrength * 2.1f * (cfg_.locomotionResponseScale * cfg_.masterMovementScale * cfg_.presetMovement)
            );
        }

        if (climbing) {
            const float vz =
                Clamp(player->m_vecMoveSpeed.z, -0.20f, 0.20f);
            const float amp =
                cfg_.climbResponseStrength *
                (cfg_.locomotionResponseScale * cfg_.masterMovementScale * cfg_.presetMovement);

            advancedPlayerVertical_ +=
                -vz * amp * 2.2f;
            advancedPlayerForward_ +=
                -std::fabs(vz) * amp * 0.35f;
        }

        if (climbing && !previousClimbing_) {
            impactSpring_.forward.AddImpulse(
                -cfg_.climbResponseStrength *
                0.72f *
                (cfg_.locomotionResponseScale * cfg_.masterMovementScale * cfg_.presetMovement)
            );
            impactSpring_.vertical.AddImpulse(
                cfg_.climbResponseStrength *
                0.34f *
                (cfg_.locomotionResponseScale * cfg_.masterMovementScale * cfg_.presetMovement)
            );
        } else if (!climbing && previousClimbing_) {
            impactSpring_.vertical.AddImpulse(
                -cfg_.climbResponseStrength *
                0.40f *
                (cfg_.locomotionResponseScale * cfg_.masterMovementScale * cfg_.presetMovement)
            );
        }

        if (parachute) {
            const CVector velocity = player->m_vecMoveSpeed;
            const float speed = std::sqrt(
                velocity.x * velocity.x +
                velocity.y * velocity.y +
                velocity.z * velocity.z
            );

            const float amp =
                (cfg_.parachuteAirStrength * cfg_.presetAmbient) *
                Clamp(0.65f + speed * 2.5f, 0.65f, 1.45f) *
                cfg_.highFrequencyScale *
                (cfg_.environmentResponseScale *
                 cfg_.masterWorldScale *
                 cfg_.presetWorld);

            float airSide = 0.0f;
            float airVertical = 0.0f;
            float airForward = 0.0f;
            VelocityToCameraLocal(
                velocity,
                airSide,
                airVertical,
                airForward
            );

            const float horizontal = std::sqrt(
                airSide * airSide +
                airForward * airForward
            );

            const float directional =
                cfg_.directionalResponseEnabled
                    ? cfg_.directionalParachuteScale
                    : 0.0f;

            if (horizontal > 0.001f) {
                advancedPlayerSide_ +=
                    (-airSide / horizontal) *
                    amp * 0.72f * directional;
                advancedPlayerForward_ +=
                    (-airForward / horizontal) *
                    amp * 0.78f * directional;
            }

            advancedPlayerVertical_ +=
                Clamp(-airVertical * 2.5f, -1.0f, 1.0f) *
                amp * 0.48f * directional;

            float windSide = 0.0f;
            float windForward = 0.0f;
            WorldXYToCameraLocal(
                CWeather::WindDir.x,
                CWeather::WindDir.y,
                windSide,
                windForward
            );

            const float windAmount =
                Clamp(CWeather::Wind, 0.0f, 2.0f) *
                (1.0f - Clamp(CWeather::InTunnelness, 0.0f, 1.0f));

            advancedPlayerSide_ +=
                -windSide * windAmount * amp * 0.32f * directional;
            advancedPlayerForward_ +=
                -windForward * windAmount * amp * 0.22f * directional;

            advancedPlayerVertical_ +=
                std::sin(t * 3.7f + 1.1f) *
                amp * 0.20f;
        }

        if (parachute && !previousParachute_) {
            const float impulse =
                cfg_.parachuteDeployStrength *
                (cfg_.environmentResponseScale * cfg_.masterWorldScale * cfg_.presetWorld);

            float deploySide = 0.0f;
            float deployVertical = 0.0f;
            float deployForward = 0.0f;
            VelocityToCameraLocal(
                player->m_vecMoveSpeed,
                deploySide,
                deployVertical,
                deployForward
            );

            const float horizontal = std::max(
                std::sqrt(
                    deploySide * deploySide +
                    deployForward * deployForward
                ),
                0.001f
            );

            const float directional =
                cfg_.directionalResponseEnabled
                    ? cfg_.directionalParachuteScale
                    : 0.0f;

            impactSpring_.vertical.AddImpulse(impulse * 0.52f);
            impactSpring_.forward.AddImpulse(
                -impulse +
                (-deployForward / horizontal) *
                impulse * 0.22f * directional
            );
            impactSpring_.side.AddImpulse(
                (-deploySide / horizontal) *
                impulse * 0.22f * directional
            );
        } else if (!parachute && previousParachute_ && !player->bIsInTheAir) {
            const float impulse =
                cfg_.parachuteLandStrength *
                (cfg_.environmentResponseScale * cfg_.masterWorldScale * cfg_.presetWorld);

            impactSpring_.vertical.AddImpulse(-impulse);
            impactSpring_.forward.AddImpulse(-impulse * 0.28f);
        }

        if (jetpack && cfg_.jetpackDirectionalEnabled) {
            const CVector velocity =
                player->m_vecMoveSpeed;

            if (!jetpackMotionInit_) {
                previousJetpackVelocity_ = velocity;
                jetpackMotionInit_ = true;
            }

            const float safeDt =
                std::max(dt, 0.001f);

            const float accelX =
                (velocity.x - previousJetpackVelocity_.x) /
                safeDt;
            const float accelY =
                (velocity.y - previousJetpackVelocity_.y) /
                safeDt;
            const float accelZ =
                (velocity.z - previousJetpackVelocity_.z) /
                safeDt;

            previousJetpackVelocity_ = velocity;

            float localVelocitySide = 0.0f;
            float localVelocityForward = 0.0f;
            WorldXYToCameraLocal(
                velocity.x,
                velocity.y,
                localVelocitySide,
                localVelocityForward
            );

            float localAccelSide = 0.0f;
            float localAccelForward = 0.0f;
            WorldXYToCameraLocal(
                accelX,
                accelY,
                localAccelSide,
                localAccelForward
            );

            const float movementScale =
                cfg_.locomotionResponseScale *
                cfg_.masterMovementScale *
                cfg_.presetMovement;

            const float directionalBase =
                cfg_.jetpackDirectionalStrength *
                (Clamp(cfg_.jetpackStrength, 0.0f, 0.40f) / 0.036f) *
                movementScale;

            float targetSide =
                -localAccelSide *
                cfg_.jetpackAccelInfluence *
                cfg_.jetpackSideScale *
                directionalBase;

            float targetForward =
                -localAccelForward *
                cfg_.jetpackAccelInfluence *
                (localAccelForward >= 0.0f
                    ? cfg_.jetpackForwardScale : cfg_.jetpackBackwardScale) *
                directionalBase;

            float targetVertical =
                -accelZ *
                cfg_.jetpackAccelInfluence *
                cfg_.jetpackVerticalScale *
                directionalBase;

            // Sustained flight retains a small directional body/load cue
            // instead of reverting to anonymous vibration.
            targetSide +=
                -localVelocitySide * 4.0f *
                cfg_.jetpackVelocityInfluence *
                cfg_.jetpackSideScale *
                directionalBase;

            const float forwardDirectionScale =
                localVelocityForward >= 0.0f
                    ? cfg_.jetpackForwardScale
                    : cfg_.jetpackBackwardScale;

            targetForward +=
                -localVelocityForward * 4.0f *
                cfg_.jetpackVelocityInfluence *
                forwardDirectionScale *
                directionalBase;

            // Retain thrust/load while climbing or descending at constant
            // speed; acceleration alone goes to zero in sustained flight.
            targetVertical += -velocity.z * 4.0f *
                cfg_.jetpackVelocityInfluence * cfg_.jetpackVerticalScale *
                directionalBase;

            targetSide =
                Clamp(
                    targetSide,
                    -cfg_.jetpackMaxSide,
                    cfg_.jetpackMaxSide
                );
            targetVertical =
                Clamp(
                    targetVertical,
                    -cfg_.jetpackMaxVertical,
                    cfg_.jetpackMaxVertical
                );
            targetForward =
                Clamp(
                    targetForward,
                    -cfg_.jetpackMaxForward,
                    cfg_.jetpackMaxForward
                );

            const float smooth =
                1.0f -
                std::exp(
                    -cfg_.jetpackSmoothing *
                    Clamp(dt, 0.0f, 0.05f)
                );

            jetpackResponseSide_ +=
                (targetSide - jetpackResponseSide_) *
                smooth;
            jetpackResponseVertical_ +=
                (targetVertical - jetpackResponseVertical_) *
                smooth;
            jetpackResponseForward_ +=
                (targetForward - jetpackResponseForward_) *
                smooth;

            // Physical thrust is mixed separately from ambient effects.

            // Tiny hover texture only. The previous full-time jetpack shake
            // has intentionally been removed.
            const float speed =
                std::sqrt(
                    velocity.x * velocity.x +
                    velocity.y * velocity.y +
                    velocity.z * velocity.z
                );

            const float hoverActivity =
                Clamp(
                    0.30f +
                    speed * 2.0f,
                    0.30f,
                    1.0f
                );

            const float hoverAmp =
                cfg_.jetpackHoverVibration *
                (Clamp(cfg_.jetpackStrength, 0.0f, 0.40f) / 0.036f) *
                cfg_.presetAmbient *
                hoverActivity *
                cfg_.highFrequencyScale *
                (cfg_.environmentResponseScale *
                 cfg_.masterWorldScale *
                 cfg_.presetWorld);

            advancedPlayerSide_ +=
                std::sin(
                    t *
                    cfg_.jetpackHoverHz *
                    2.0f *
                    kPi
                ) *
                hoverAmp *
                0.32f;

            advancedPlayerVertical_ +=
                std::sin(
                    t *
                    cfg_.jetpackHoverHz *
                    1.37f *
                    2.0f *
                    kPi +
                    0.7f
                ) *
                hoverAmp *
                0.48f;

            TouchResponseSource(
                cr::ResponseOrigin::Locomotion,
                std::fabs(jetpackResponseSide_) +
                std::fabs(jetpackResponseVertical_) +
                std::fabs(jetpackResponseForward_),
                250
            );
        } else {
            jetpackMotionInit_ = false;
            previousJetpackVelocity_ = {};

            const float release =
                1.0f -
                std::exp(
                    -cfg_.jetpackSmoothing *
                    1.35f *
                    Clamp(dt, 0.0f, 0.05f)
                );

            jetpackResponseSide_ +=
                (0.0f - jetpackResponseSide_) *
                release;
            jetpackResponseVertical_ +=
                (0.0f - jetpackResponseVertical_) *
                release;
            jetpackResponseForward_ +=
                (0.0f - jetpackResponseForward_) *
                release;
        }

        if (jetpack != previousJetpack_) {
            const float impulse =
                cfg_.jetpackTransitionStrength *
                (cfg_.locomotionResponseScale *
                 cfg_.masterMovementScale *
                 cfg_.presetMovement);

            impactSpring_.vertical.AddImpulse(
                jetpack
                    ? -impulse
                    : impulse * 0.30f
            );

            impactSpring_.forward.AddImpulse(
                jetpack
                    ? -impulse * 0.48f
                    : impulse * 0.16f
            );

            AddFovKick(
                jetpack
                    ? cfg_.fovContinuousTool * 0.55f
                    : -cfg_.fovContinuousTool * 0.20f
            );

            if (cfg_.debug) {
                log_.Write(
                    "JETPACK TRANSITION active=%d impulse=%.4f",
                    jetpack ? 1 : 0,
                    impulse
                );
            }
        }

        previousSwimming_ = swimming;
        previousClimbing_ = climbing;
        previousParachute_ = parachute;
        previousJetpack_ = jetpack;
    }

    void UpdateNativeCameraShake() {
        AuditHit(AUD_UpdateNativeCameraShake);

        nativeShakeSide_ = 0.0f;
        nativeShakeVertical_ = 0.0f;
        nativeShakeForward_ = 0.0f;

        if (!cfg_.advancedResponseEnabled ||
            cfg_.nativeShakeBlend <= 0.0001f) {
            nativeShakeContinuousSinceMs_ = 0;
            nativeShakeLastChangedMs_ = 0;
            nativeShakeBelowThresholdSinceMs_ = 0;
            nativeShakeSuppressed_ = false;
            nativeShakeLastForce_ = 0.0f;
            return;
        }

        const unsigned now =
            CTimer::m_snTimeInMilliseconds;

        const float shake =
            Clamp(
                std::fabs(TheCamera.m_fCamShakeForce),
                0.0f,
                2.5f
            );

        // A real native shake should decay/change. A stale nonzero
        // m_fCamShakeForce must not become an infinite procedural oscillator.
        if (shake <= 0.001f) {
            if (nativeShakeBelowThresholdSinceMs_ == 0)
                nativeShakeBelowThresholdSinceMs_ = now;

            if (now - nativeShakeBelowThresholdSinceMs_ >=
                cfg_.nativeShakeRearmMs) {
                if (nativeShakeSuppressed_ && cfg_.debug)
                    log_.Write("NATIVE SHAKE REARMED");

                nativeShakeContinuousSinceMs_ = 0;
                nativeShakeLastChangedMs_ = 0;
                nativeShakeSuppressed_ = false;
            }

            nativeShakeLastForce_ = shake;
            return;
        }

        nativeShakeBelowThresholdSinceMs_ = 0;

        const bool changed =
            std::fabs(shake - nativeShakeLastForce_) >=
            cfg_.nativeShakeChangeEpsilon;

        if (nativeShakeContinuousSinceMs_ == 0) {
            nativeShakeContinuousSinceMs_ = now;
            nativeShakeLastChangedMs_ = now;
        } else if (changed) {
            nativeShakeLastChangedMs_ = now;
        }

        nativeShakeLastForce_ = shake;

        const bool continuousExpired =
            cfg_.nativeShakeMaxContinuousMs > 0 &&
            now - nativeShakeContinuousSinceMs_ >=
                cfg_.nativeShakeMaxContinuousMs;

        // Also treat a basically unchanged nonzero force as stale. This is
        // the failure mode observed after explosion/combat camera shake.
        const bool staleForce =
            cfg_.nativeShakeMaxContinuousMs > 0 &&
            nativeShakeLastChangedMs_ != 0 &&
            now - nativeShakeLastChangedMs_ >=
                cfg_.nativeShakeMaxContinuousMs;

        if (continuousExpired || staleForce) {
            if (!nativeShakeSuppressed_ && cfg_.debug) {
                log_.Write(
                    "NATIVE SHAKE SUPPRESSED force=%.5f continuousMs=%u staleMs=%u",
                    shake,
                    now - nativeShakeContinuousSinceMs_,
                    nativeShakeLastChangedMs_ != 0
                        ? now - nativeShakeLastChangedMs_
                        : 0u
                );
            }

            nativeShakeSuppressed_ = true;
        }

        if (nativeShakeSuppressed_)
            return;

        const float t =
            static_cast<float>(now) * 0.001f;

        const float amp =
            shake *
            cfg_.nativeShakeBlend *
            (cfg_.environmentResponseScale * cfg_.masterWorldScale * cfg_.presetWorld) *
            cfg_.highFrequencyScale;

        TouchResponseSource(
            cr::ResponseOrigin::NativeShake,
            amp,
            cfg_.nativeShakeRearmMs + 100u,
            cfg_.nativeShakeMaxContinuousMs
        );

        if (ResponseSourceSuppressed(cr::ResponseOrigin::NativeShake))
            return;

        nativeShakeSide_ =
            std::sin(t * 17.0f + 0.7f) *
            amp *
            0.44f;

        nativeShakeVertical_ =
            std::sin(t * 23.0f + 1.1f) *
            amp *
            0.62f;

        nativeShakeForward_ =
            std::sin(t * 13.0f + 2.0f) *
            amp *
            0.30f;
    }

    void ProcessPendingExplosionShocks() {
        AuditHit(AUD_ProcessPendingExplosionShocks);
        const unsigned now =
            CTimer::m_snTimeInMilliseconds;

        for (auto& shock : pendingShocks_) {
            if (!shock.active || now < shock.fireAtMs)
                continue;

            TouchResponseSource(
                cr::ResponseOrigin::Explosion,
                std::fabs(shock.side) +
                std::fabs(shock.vertical) +
                std::fabs(shock.forward),
                900
            );

            impactSpring_.side.AddImpulse(shock.side);
            impactSpring_.vertical.AddImpulse(shock.vertical);
            impactSpring_.forward.AddImpulse(shock.forward);

            {
                cr::Vec3 tr{};
                cr::Vec3 rot{};
                rot.vertical=shock.rotationPitchDeg;
                rot.side=shock.rotationRollDeg;
                SubmitTransientEvent(
                    cr::ResponseOrigin::Explosion,
                    tr,rot,0.0f,1.0f,shock.ambientDuck,
                    cr::TransientPriority::Critical,
                    0x50525645u
                );
            }

            if (cfg_.debug) {
                log_.Write(
                    "EXPLOSION SHOCK FIRE side=%.4f vertical=%.4f forward=%.4f",
                    shock.side,
                    shock.vertical,
                    shock.forward
                );
            }

            shock.active = false;
        }
    }

    void UpdateWaterResponse(
        CPlayerPed* player,
        CVehicle* vehicle,
        bool inVehicle
    ) {
        AuditHit(AUD_UpdateWaterResponse);
        if (!cfg_.waterResponseEnabled || !player)
            return;

        CPhysical* physical =
            inVehicle && vehicle
                ? static_cast<CPhysical*>(vehicle)
                : static_cast<CPhysical*>(player);

        const bool touching =
            physical->bTouchingWater || physical->bSubmergedInWater;

        bool& previous =
            inVehicle ? previousVehicleTouchingWater_ : previousPlayerTouchingWater_;

        if (!previous && touching) {
            const float downSpeed =
                std::max(0.0f, -physical->m_vecMoveSpeed.z);
            const float speedScale =
                Clamp(0.75f + downSpeed * 7.0f, 0.75f, 1.75f);
            const float impulse =
                cfg_.waterEntryStrength * speedScale;

            float velocitySide = 0.0f;
            float velocityVertical = 0.0f;
            float velocityForward = 0.0f;
            VelocityToCameraLocal(
                physical->m_vecMoveSpeed,
                velocitySide,
                velocityVertical,
                velocityForward
            );

            const float horizontal = std::sqrt(
                velocitySide * velocitySide +
                velocityForward * velocityForward
            );

            const float directional =
                cfg_.directionalResponseEnabled
                    ? cfg_.directionalWaterScale
                    : 0.0f;

            const float travelSide =
                horizontal > 0.001f
                    ? -velocitySide / horizontal
                    : 0.0f;

            const float travelForward =
                horizontal > 0.001f
                    ? -velocityForward / horizontal
                    : 0.0f;

            QueueEnvironmentImpulse(
                travelSide * impulse * 0.18f * directional,
                -impulse,
                -impulse * 0.20f +
                    travelForward * impulse * 0.24f * directional,
                1.30f
            );

            if (cfg_.debug)
                log_.Write(
                    "WATER ENTRY vehicle=%d downSpeed=%.4f impulse=%.3f",
                    inVehicle ? 1 : 0,
                    downSpeed,
                    impulse
                );
        }

        previous = touching;
    }

    void UpdateWeatherResponse(CPlayerPed* player, float dt) {
        AuditHit(AUD_UpdateWeatherResponse);

        if (!cfg_.weatherResponseEnabled || !player) {
            weatherSide_ = 0.0f;
            weatherVertical_ = 0.0f;
            weatherForward_ = 0.0f;
            weatherWindBlend_ = 0.0f;
            weatherRainBlend_ = 0.0f;
            weatherSandBlend_ = 0.0f;
            weatherQuakeBlend_ = 0.0f;
            return;
        }

        const float outside =
            1.0f -
            Clamp(
                CWeather::InTunnelness,
                0.0f,
                1.0f
            );

        const float windTarget =
            Clamp(CWeather::Wind, 0.0f, 2.0f) *
            outside;

        const float rainTarget =
            Clamp(CWeather::Rain, 0.0f, 1.5f) *
            outside;

        const float sandTarget =
            Clamp(CWeather::Sandstorm, 0.0f, 1.5f) *
            outside;

        const float quakeTarget =
            Clamp(
                std::fabs(CWeather::Earthquake),
                0.0f,
                2.0f
            );

        const float alpha =
            1.0f -
            std::exp(
                -cfg_.weatherSmoothing *
                Clamp(dt, 0.0f, 0.05f)
            );

        weatherWindBlend_ +=
            (windTarget - weatherWindBlend_) *
            alpha;

        weatherRainBlend_ +=
            (rainTarget - weatherRainBlend_) *
            alpha;

        weatherSandBlend_ +=
            (sandTarget - weatherSandBlend_) *
            alpha;

        weatherQuakeBlend_ +=
            (quakeTarget - weatherQuakeBlend_) *
            alpha;

        float localWindSide = 0.0f;
        float localWindForward = 0.0f;

        WorldXYToCameraLocal(
            CWeather::WindDir.x,
            CWeather::WindDir.y,
            localWindSide,
            localWindForward
        );

        const float gust =
            weatherWindBlend_ <= 0.0005f ? 0.0f :
            cfg_.proceduralNoiseEnabled
                ? weatherWindNoise_.Update(
                    dt,
                    cfg_.windGustRate,
                    3.5f
                )
                : std::sin(
                    static_cast<float>(
                        CTimer::m_snTimeInMilliseconds
                    ) *
                    0.001f *
                    cfg_.windGustRate *
                    2.0f *
                    kPi
                );

        const float windStrength =
            weatherWindBlend_ *
            (cfg_.windResponseStrength * cfg_.presetAmbient) *
            (cfg_.environmentResponseScale *
             cfg_.masterWorldScale *
             cfg_.presetWorld);

        const float gustScale =
            1.0f +
            gust *
            cfg_.windGustStrength *
            Clamp(weatherWindBlend_, 0.0f, 1.0f);

        weatherSide_ =
            localWindSide *
            windStrength *
            gustScale;

        weatherForward_ =
            localWindForward *
            windStrength *
            (0.70f + 0.30f * gustScale);

        // Rain is deliberately subtle. It reads as fine body/camera texture
        // and scales continuously with GTA's real rain blend.
        const float rainTexture =
            weatherRainBlend_ <= 0.0005f ? 0.0f :
            cfg_.proceduralNoiseEnabled
                ? weatherRainNoise_.Update(
                    dt,
                    cfg_.rainTextureRate,
                    18.0f
                )
                : std::sin(
                    static_cast<float>(
                        CTimer::m_snTimeInMilliseconds
                    ) *
                    0.001f *
                    cfg_.rainTextureRate *
                    2.0f *
                    kPi
                );

        const float rainAmp =
            weatherRainBlend_ *
            (cfg_.rainResponseStrength * cfg_.presetAmbient) *
            cfg_.highFrequencyScale *
            (cfg_.environmentResponseScale *
             cfg_.masterWorldScale *
             cfg_.presetWorld);

        weatherVertical_ +=
            rainTexture *
            rainAmp *
            cfg_.rainVerticalBias;

        weatherSide_ +=
            rainTexture *
            rainAmp *
            0.12f;

        // Sandstorms are slower and heavier than rain and follow wind
        // direction rather than producing unrelated camera oscillation.
        const float sandTexture =
            weatherSandBlend_ <= 0.0005f ? 0.0f :
            cfg_.proceduralNoiseEnabled
                ? weatherSandNoise_.Update(
                    dt,
                    cfg_.sandstormLowFrequency,
                    2.5f
                )
                : std::sin(
                    static_cast<float>(
                        CTimer::m_snTimeInMilliseconds
                    ) *
                    0.001f *
                    cfg_.sandstormLowFrequency *
                    2.0f *
                    kPi +
                    0.8f
                );

        const float sandAmp =
            weatherSandBlend_ *
            (cfg_.sandstormResponseStrength * cfg_.presetAmbient) *
            (cfg_.environmentResponseScale *
             cfg_.masterWorldScale *
             cfg_.presetWorld);

        weatherSide_ +=
            localWindSide *
            sandAmp *
            (0.75f + sandTexture * 0.25f);

        weatherForward_ +=
            localWindForward *
            sandAmp *
            (0.60f + sandTexture * 0.22f);

        weatherVertical_ +=
            sandTexture *
            sandAmp *
            0.28f;

        if (weatherQuakeBlend_ > 0.001f) {
            const float quakeTexture =
                cfg_.proceduralNoiseEnabled
                    ? weatherQuakeNoise_.Update(
                        dt,
                        18.0f,
                        24.0f
                    )
                    : std::sin(
                        static_cast<float>(
                            CTimer::m_snTimeInMilliseconds
                        ) *
                        0.001f *
                        18.0f *
                        2.0f *
                        kPi
                    );

            const float q =
                weatherQuakeBlend_ *
                cfg_.earthquakeResponseStrength *
                (cfg_.environmentResponseScale *
                 cfg_.masterWorldScale *
                 cfg_.presetWorld);

            weatherSide_ +=
                quakeTexture *
                q;

            weatherVertical_ +=
                quakeTexture *
                q *
                0.72f;

            weatherForward_ +=
                quakeTexture *
                q *
                0.48f;

            weatherSide_ =
                Clamp(
                    weatherSide_,
                    -cfg_.earthquakeMaxOffset,
                    cfg_.earthquakeMaxOffset
                );

            weatherVertical_ =
                Clamp(
                    weatherVertical_,
                    -cfg_.earthquakeMaxOffset,
                    cfg_.earthquakeMaxOffset
                );

            weatherForward_ =
                Clamp(
                    weatherForward_,
                    -cfg_.earthquakeMaxOffset,
                    cfg_.earthquakeMaxOffset
                );
        }

        weatherSide_ =
            Clamp(
                weatherSide_,
                -cfg_.weatherMaxAmbient,
                cfg_.weatherMaxAmbient
            );

        weatherVertical_ =
            Clamp(
                weatherVertical_,
                -cfg_.weatherMaxAmbient,
                cfg_.weatherMaxAmbient
            );

        weatherForward_ =
            Clamp(
                weatherForward_,
                -cfg_.weatherMaxAmbient,
                cfg_.weatherMaxAmbient
            );

        const float weatherEvidence =
            std::fabs(weatherSide_) +
            std::fabs(weatherVertical_) +
            std::fabs(weatherForward_);

        if (weatherEvidence > 0.00001f) {
            TouchResponseSource(
                cr::ResponseOrigin::Weather,
                weatherEvidence,
                350
            );
        }
    }

#include "CameraResponseWorldScans.inc"

    void UpdateNearbyVehicles(CPlayerPed* player, bool inVehicle, CVehicle* ownVehicle, float dt) {
        AuditHit(AUD_UpdateNearbyVehicles);
        const bool allowed =
            cfg_.nearbyVehiclesEnabled &&
            player &&
            (!inVehicle || cfg_.nearbyVehiclesInVehicle) &&
            (inVehicle || cfg_.nearbyVehiclesOnFoot);

        if (!allowed) {
            nearbyVehicleSide_ = 0.0f;
            nearbyVehicleVertical_ = 0.0f;
            nearbyVehicleForward_ = 0.0f;
            nearbyVehicleContributors_ = 0;
            nearbyVehicleNearestDistance_ = 0.0f;
            nearbyVehicleStrongestActivity_ = 0.0f;
            lastNearbyVehicleScanMs_ = 0;
            return;
        }

        const unsigned nowMs = CTimer::m_snTimeInMilliseconds;
        if (lastNearbyVehicleScanMs_ != 0 &&
            nowMs - lastNearbyVehicleScanMs_ < cfg_.nearbyVehicleScanMs) {
            return;
        }

        const float scanDt =
            lastNearbyVehicleScanMs_ != 0
                ? std::max(float(nowMs - lastNearbyVehicleScanMs_) * 0.001f, 0.001f)
                : std::max(dt, 0.001f);

        lastNearbyVehicleScanMs_ = nowMs;
        cr::ScopedScanProfile scanScope(scanProfiler_, cr::ScanKind::NearbyVehicles, cfg_.scanProfilingEnabled);

        nearbyVehicleSide_ = 0.0f;
        nearbyVehicleVertical_ = 0.0f;
        nearbyVehicleForward_ = 0.0f;
        nearbyVehicleContributors_ = 0;
        nearbyVehicleNearestDistance_ = 0.0f;
        nearbyVehicleStrongestActivity_ = 0.0f;

        auto* pool = CPools::ms_pVehiclePool;
        if (!pool)
            return;

        const int poolSize = pool->m_nSize;
        if (poolSize <= 0)
            return;

        if (nearbyVehicleTrack_.size() != static_cast<size_t>(poolSize))
            nearbyVehicleTrack_.assign(static_cast<size_t>(poolSize), NearbyVehicleTrack{});

        const CVector& pp = player->GetPosition();
        const float minD = std::max(0.0f, cfg_.nearbyVehiclesMinDistance);
        const float maxD = std::max(minD + 0.01f, cfg_.nearbyVehiclesMaxDistance);
        const float safeDt = scanDt;
        const float tNow = static_cast<float>(nowMs) * 0.001f;

        float sideSum = 0.0f;
        float verticalSum = 0.0f;
        float forwardSum = 0.0f;

        for (int i = 0; i < poolSize; ++i) {
            CVehicle* v = pool->GetAt(i);
            NearbyVehicleTrack& tr = nearbyVehicleTrack_[static_cast<size_t>(i)];

            if (!v || v == ownVehicle || !v->bEngineOn || v->m_fHealth <= 0.0f) {
                tr = {};
                continue;
            }

            const CVector& vp = v->GetPosition();
            const float dx = vp.x - pp.x;
            const float dy = vp.y - pp.y;
            const float dz = vp.z - pp.z;
            const float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq > maxD * maxD) {
                tr.vehicle = v;
                tr.velocity = v->m_vecMoveSpeed;
                tr.initialized = true;
                continue;
            }

            const float dist = std::sqrt(distSq);
            const CVector vel = v->m_vecMoveSpeed;
            CVector accel{};
            if (tr.initialized && tr.vehicle == v) {
                accel.x = (vel.x - tr.velocity.x) / safeDt;
                accel.y = (vel.y - tr.velocity.y) / safeDt;
                accel.z = (vel.z - tr.velocity.z) / safeDt;
            }
            tr.vehicle = v;
            tr.velocity = vel;
            tr.initialized = true;

            const float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
            const float accelMag = std::sqrt(
                accel.x * accel.x + accel.y * accel.y + accel.z * accel.z);

            if (nearbyVehicleContributors_ >= cfg_.nearbyVehiclesMaxContributors)
                continue;

            float distanceFalloff =
                1.0f - Clamp((dist - minD) / (maxD - minD), 0.0f, 1.0f);
            distanceFalloff =
                std::pow(distanceFalloff, std::max(0.05f, cfg_.nearbyVehiclesFalloffPower));

            // GTA move-speed values are small. Treat ~0.02 as crawling/idle
            // motion and ~0.55+ as fast road speed for this ambient layer.
            const float lowSpeedNorm = Clamp(speed / 0.16f, 0.0f, 1.0f);
            const float highSpeedNorm = Clamp((speed - 0.16f) / 0.44f, 0.0f, 1.0f);
            float speedActivity =
                cfg_.nearbyVehiclesIdleScale +
                lowSpeedNorm * (cfg_.nearbyVehiclesLowSpeedScale - cfg_.nearbyVehiclesIdleScale) +
                highSpeedNorm * (cfg_.nearbyVehiclesHighSpeedScale - cfg_.nearbyVehiclesLowSpeedScale);

            const float accelActivity =
                Clamp(accelMag / 5.0f, 0.0f, 1.0f) *
                cfg_.nearbyVehiclesAccelerationScale;

            const float activity = Clamp(speedActivity + accelActivity, 0.0f, 1.5f);
            const float amp =
                (cfg_.nearbyVehiclesStrength * cfg_.presetAmbient) *
                cfg_.strength *
                cfg_.presetScale *
                distanceFalloff *
                activity;

            // A deterministic per-pool-slot phase avoids nearby cars pulsing in
            // sync. Faster vehicles vibrate a little faster; acceleration adds
            // intensity, not an unbounded frequency jump.
            const float rate = 5.5f + Clamp(speed * 8.0f, 0.0f, 6.0f);
            const float phase =
                tNow * rate * 2.0f * kPi +
                static_cast<float>(i) * 1.6180339f;

            float localSide = 0.0f;
            float localForward = 0.0f;
            WorldXYToCameraLocal(
                dx,
                dy,
                localSide,
                localForward
            );
            const float sideBias =
                std::fabs(localSide) > 0.05f ? (localSide > 0.0f ? -1.0f : 1.0f) : 0.0f;
            const float forwardBias =
                std::fabs(localForward) > 0.05f ? (localForward > 0.0f ? -1.0f : 1.0f) : 0.0f;

            sideSum +=
                (std::sin(phase * 0.83f + 0.4f) * 0.75f + sideBias * 0.25f) *
                amp * cfg_.nearbyVehiclesSide;
            verticalSum +=
                (std::sin(phase) * 0.72f +
                 std::sin(phase * 1.79f + 1.2f) * 0.28f) *
                amp * cfg_.nearbyVehiclesVertical;
            forwardSum +=
                (std::sin(phase * 1.13f + 2.0f) * 0.70f + forwardBias * 0.30f) *
                amp * cfg_.nearbyVehiclesForward;

            ++nearbyVehicleContributors_;
            if (nearbyVehicleNearestDistance_ <= 0.0f || dist < nearbyVehicleNearestDistance_)
                nearbyVehicleNearestDistance_ = dist;
            nearbyVehicleStrongestActivity_ =
                std::max(nearbyVehicleStrongestActivity_, activity);

        }

        const float maxCombined = std::max(0.0f, cfg_.nearbyVehiclesMaxCombined);
        nearbyVehicleSide_ = Clamp(sideSum, -maxCombined, maxCombined);
        nearbyVehicleVertical_ = Clamp(verticalSum, -maxCombined, maxCombined);
        nearbyVehicleForward_ = Clamp(forwardSum, -maxCombined, maxCombined);

    }

    void UpdateExplosions(CPlayerPed* p) {
        AuditHit(AUD_UpdateExplosions);
        if (!cfg_.explosionsEnabled || !aExplosions)
            return;

        const CVector& pp = p->GetPosition();

        for (unsigned i = 0; i < 16; ++i) {
            CExplosion& ex = aExplosions[i];

            if (!ex.m_bIsActive) {
                lastExplosionCreated_[i] = ex.m_nCreatedTime;
                continue;
            }

            if (ex.m_nCreatedTime == lastExplosionCreated_[i])
                continue;

            lastExplosionCreated_[i] = ex.m_nCreatedTime;

            const float dx = pp.x - ex.m_vecPosition.x;
            const float dy = pp.y - ex.m_vecPosition.y;
            const float dz = pp.z - ex.m_vecPosition.z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            if (distance >= cfg_.explosionRadius)
                continue;

            const float proximity = 1.0f - Clamp(distance / cfg_.explosionRadius, 0.0f, 1.0f);

            float propagationScale = 1.0f;
            if (cfg_.advancedResponseEnabled &&
                cfg_.explosionOcclusionEnabled &&
                distance > 1.0f) {
                CColPoint hit{};
                CEntity* blocker = nullptr;
                CEntity* oldIgnore = CWorld::pIgnoreEntity;
                CWorld::pIgnoreEntity = p;
                const bool occluded =
                    CWorld::ProcessLineOfSight(
                        ex.m_vecPosition,
                        pp,
                        hit,
                        blocker,
                        true,   // buildings
                        false,  // vehicles
                        false,  // peds
                        true,   // objects
                        true,   // dummies
                        false,
                        true,
                        false
                    );
                CWorld::pIgnoreEntity = oldIgnore;

                if (occluded)
                    propagationScale = cfg_.explosionOccludedScale;
            }

            const float impulse =
                cfg_.explosionImpulse *
                (0.18f + 0.82f * proximity * proximity) *
                propagationScale;
            AddFovKick(cfg_.fovExplosionKick *
                Clamp(proximity * propagationScale, 0.0f, 1.25f));

            const float len2d = std::sqrt(dx * dx + dy * dy);

            float nx = 0.0f;
            float ny = 0.0f;
            if (len2d > 0.001f) {
                nx = dx / len2d;
                ny = dy / len2d;
            }

            float localSide = 0.0f;
            float localForward = 0.0f;
            WorldXYToCameraLocal(
                nx,
                ny,
                localSide,
                localForward
            );

            const float combatImpulse =
                impulse *
                cfg_.presetImpact *
                (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat);

            TouchResponseSource(
                cr::ResponseOrigin::Explosion,
                combatImpulse,
                1100
            );

            impactSpring_.side.AddImpulse(localSide * combatImpulse * 0.42f);
            impactSpring_.forward.AddImpulse(localForward * combatImpulse * 0.62f);
            impactSpring_.vertical.AddImpulse((0.32f + proximity * 0.28f) * combatImpulse);

            {
                const float rs=Clamp(
                    proximity*propagationScale*(0.65f+combatImpulse*0.35f),
                    0.0f,1.75f
                );
                cr::Vec3 tr{};
                cr::Vec3 rot{};
                rot.vertical=-localForward*cfg_.explosionPitchDeg*rs;
                rot.side=-localSide*cfg_.explosionRollDeg*rs;
                rot.forward=localSide*cfg_.explosionYawDeg*rs;
                SubmitTransientEvent(
                    cr::ResponseOrigin::Explosion,
                    tr,rot,0.0f,1.20f,0.78f,
                    cr::TransientPriority::Critical,
                    0x4558504Cu
                );
            }

            if (cfg_.advancedResponseEnabled &&
                cfg_.explosionShockDelayEnabled &&
                distance > 4.0f) {

                PendingShock* slot = nullptr;
                for (auto& pending : pendingShocks_) {
                    if (!pending.active) {
                        slot = &pending;
                        break;
                    }
                }

                if (slot) {
                    const float speed =
                        std::max(cfg_.explosionShockSpeed, 10.0f);
                    const unsigned delayMs =
                        static_cast<unsigned>(
                            Clamp(distance / speed * 1000.0f, 8.0f, 220.0f)
                        );

                    const float secondary =
                        combatImpulse *
                        cfg_.explosionShockSecondaryScale *
                        (propagationScale < 0.999f ? propagationScale : 1.0f);

                    slot->active = true;
                    slot->fireAtMs =
                        CTimer::m_snTimeInMilliseconds + delayMs;
                    const float indoorScale=
                        contextIndoors_?cfg_.explosionPressureIndoorScale:1.0f;
                    const float verticalRelation=
                        distance>0.001f?dz/distance:0.0f;
                    const float verticalScale=
                        verticalRelation<0.0f
                            ? 1.0f+(-verticalRelation)*cfg_.explosionPressureBelowBoost
                            : 1.0f-verticalRelation*(1.0f-cfg_.explosionPressureAboveScale);

                    slot->side=localSide*secondary*0.50f*indoorScale;
                    slot->forward=localForward*secondary*0.78f*indoorScale;
                    slot->vertical=secondary*0.42f*verticalScale*indoorScale;
                    slot->rotationPitchDeg=-localForward*cfg_.explosionPitchDeg*secondary*0.55f;
                    slot->rotationRollDeg=-localSide*cfg_.explosionRollDeg*secondary*cfg_.explosionPressureLateralRoll;
                    slot->ambientDuck=Clamp(secondary*1.8f,0.20f,0.82f);
                }
            }

            if (cfg_.debug) {
                log_.Write(
                    "EXPLOSION slot=%u type=%u dist=%.2f proximity=%.2f impulse=%.3f",
                    i, ex.m_nType, distance, proximity, impulse
                );
            }
        }
    }

    void ApplyDirectionalMovement(CPlayerPed* p) {
        AuditHit(AUD_ApplyDirectionalMovement);
        if (!cfg_.locomotionEnabled || !cfg_.inertiaEnabled)
            return;

        if (HorizontalSpeed(p) <= cfg_.movementDeadzone)
            return;

        const float vx = p->m_vecMoveSpeed.x;
        const float vy = p->m_vecMoveSpeed.y;
        const float sh = std::sin(p->m_fHeadingCurrent);
        const float ch = std::cos(p->m_fHeadingCurrent);

        // GTA heading basis: forward=(sin h, cos h), right=(cos h,-sin h).
        const float localForward = vx * sh + vy * ch;
        const float localSide = vx * ch - vy * sh;

        const float sideNorm = Clamp(localSide / 0.09f, -1.0f, 1.0f);
        const float backNorm = Clamp(-localForward / 0.08f, 0.0f, 1.0f);

        gaitTargetSide_ += sideNorm * cfg_.strafeSway * strengthNow_;
        gaitTargetForward_ += backNorm * cfg_.backwardSway * strengthNow_;
        gaitTargetVertical_ -= std::fabs(sideNorm) * cfg_.strafeSway * 0.10f * strengthNow_;
    }

    void UpdateFeet(CPlayerPed* p, float dt) {
        AuditHit(AUD_UpdateFeet);
        gaitTargetSide_ = 0.0f;
        gaitTargetVertical_ = 0.0f;
        gaitTargetForward_ = 0.0f;

        const bool physicallyMoving =
            HorizontalSpeed(p) > cfg_.movementDeadzone;

        if (!Moving(motion_) || !physicallyMoving) {
            leftFoot_.Reset();
            rightFoot_.Reset();

            // Fade phase smoothly rather than instantly zeroing it, so
            // run -> stop settles instead of snapping.
            const float alpha =
                1.0f - std::exp(-cfg_.footPhaseResponse * dt);

            smoothedFootPhase_ +=
                (0.0f - smoothedFootPhase_) * alpha;

            rawFootPhase_ = 0.0f;
            return;
        }

        RwV3d pelvis{};
        RwV3d left{};
        RwV3d right{};

        p->GetBonePosition(pelvis, kBonePelvis, true);
        p->GetBonePosition(left, kBoneLeftFoot, true);
        p->GetBonePosition(right, kBoneRightFoot, true);

        leftHeight_ = left.z - pelvis.z;
        rightHeight_ = right.z - pelvis.z;
        footDiff_ = leftHeight_ - rightHeight_;

        // Left/right foot separation is our real animation phase source.
        rawFootPhase_ =
            Clamp(footDiff_ * 2.7f, -1.0f, 1.0f);

        // Important: the old translation-only build used the raw phase
        // directly. Filter it every frame so locomotion glides between
        // animation samples instead of looking like discrete offsets.
        const float phaseAlpha =
            1.0f - std::exp(-cfg_.footPhaseResponse * dt);

        smoothedFootPhase_ +=
            (rawFootPhase_ - smoothedFootPhase_) *
            phaseAlpha;

        const float phase =
            Clamp(smoothedFootPhase_, -1.0f, 1.0f);

        // phase^2 gives two smooth vertical pulses per full left/right gait
        // cycle and, unlike abs(phase), has no cusp at the center crossing.
        const float stride =
            phase * phase;

        // Center the vertical waveform around zero. The previous build spent
        // most of locomotion at a nearly constant negative vertical offset,
        // which technically moved the camera but did not read as "bob".
        const float verticalWave =
            0.42f - stride;

        const float forwardWave =
            0.30f - stride;

        // Actual horizontal speed adds continuous intensity while the move
        // state selects walk/run/sprint scaling through strengthNow_.
        const float speed =
            HorizontalSpeed(p);

        const float speedFactor =
            Clamp(
                0.48f + speed / 0.15f,
                0.55f,
                1.45f
            );

        const float gaitStrength =
            strengthNow_ *
            speedFactor;

        gaitTargetSide_ =
            phase *
            cfg_.gaitSide *
            gaitStrength;

        gaitTargetVertical_ =
            verticalWave *
            cfg_.gaitVertical *
            gaitStrength;

        gaitTargetForward_ =
            forwardWave *
            cfg_.gaitForward *
            gaitStrength;

        const unsigned now =
            CTimer::m_snTimeInMilliseconds;

        if (Touchdown(
                leftFoot_,
                leftHeight_,
                rightHeight_,
                dt,
                now)) {
            Step(true);
        }

        if (Touchdown(
                rightFoot_,
                rightHeight_,
                leftHeight_,
                dt,
                now)) {
            Step(false);
        }

        // Low-level fallback motion remains underneath the real bone gait.
        // It is deliberately small and only becomes stronger if touchdown
        // detection has been quiet for a while.
        float freq = 6.4f;
        if (motion_ == MotionState::Run)
            freq = 9.3f;
        else if (motion_ == MotionState::Sprint)
            freq = 11.5f;

        gaitPhase_ +=
            freq * dt;

        if (gaitPhase_ > 2.0f * kPi)
            gaitPhase_ -= 2.0f * kPi;

        float fallback =
            cfg_.fallbackGait * 0.18f;

        if (lastAnyStepMs_ == 0 ||
            now - lastAnyStepMs_ > 500u) {
            fallback =
                cfg_.fallbackGait;
        }

        const float phaseFallback =
            std::sin(gaitPhase_);

        const float bounceFallback =
            phaseFallback * phaseFallback;

        gaitTargetSide_ +=
            phaseFallback *
            cfg_.gaitSide *
            0.34f *
            gaitStrength *
            fallback;

        gaitTargetVertical_ +=
            (0.40f - bounceFallback) *
            cfg_.gaitVertical *
            0.34f *
            gaitStrength *
            fallback;
    }

    void UpdateLook(
        CPlayerPed* p,
        CPad* pad,
        CCam& cam,
        float dt
    ) {
        AuditHit(AUD_UpdateLook);
        lookTargetSide_ = 0.0f;
        lookTargetVertical_ = 0.0f;
        lookTargetForward_ = 0.0f;

        const float yaw = cam.m_fHorizontalAngle;
        const float pitch = cam.m_fVerticalAngle;

        if (!camAngleInit_) {
            prevYaw_ = yaw;
            prevPitch_ = pitch;
            camAngleInit_ = true;
        }

        float dy = WrapAngle(yaw - prevYaw_);
        float dp = WrapAngle(pitch - prevPitch_);

        prevYaw_ = yaw;
        prevPitch_ = pitch;

        if (std::fabs(dy) > 0.65f)
            dy = 0.0f;
        if (std::fabs(dp) > 0.50f)
            dp = 0.0f;

        yawRate_ = Clamp(dy / dt, -9.0f, 9.0f);
        pitchRate_ = Clamp(dp / dt, -7.0f, 7.0f);

        if (!cfg_.inertiaEnabled)
            return;

        // Camera-local inertial lag.
        lookTargetSide_ += Clamp(
            -yawRate_ * 0.0052f * cfg_.lookYaw,
            -0.040f,
            0.040f
        );

        lookTargetVertical_ += Clamp(
            -pitchRate_ * 0.0042f * cfg_.lookPitch,
            -0.032f,
            0.032f
        );

        lookTargetForward_ += Clamp(
            -std::fabs(yawRate_) * 0.0010f,
            -0.010f,
            0.0f
        );

        if (pad) {
            const float lr = Clamp(
                float(pad->GetPedWalkLeftRight()) / 128.0f,
                -1.0f,
                1.0f
            );

            lookTargetSide_ +=
                -lr *
                0.0080f *
                cfg_.turnInput *
                (motion_ == MotionState::Idle ? 1.0f : 0.55f);
        }

        const float speed = HorizontalSpeed(p);

        if (!speedInit_) {
            prevSpeed_ = speed;
            speedInit_ = true;
        }

        const float accel =
            Clamp((speed - prevSpeed_) / dt, -3.5f, 3.5f);

        prevSpeed_ = speed;

        lookTargetForward_ += Clamp(
            -accel * 0.0045f * cfg_.accelInertia,
            -0.014f,
            0.014f
        );

        lookTargetVertical_ += Clamp(
            -std::fabs(accel) *
            0.0012f *
            cfg_.accelInertia,
            -0.005f,
            0.0f
        );
    }


    enum class PlayerStrikeKind {
        None,
        Punch,
        Kick
    };

    static PlayerStrikeKind ClassifyPlayerStrike(
        const CTaskSimpleFight* fight,
        eWeaponType weaponType
    ) {
        if (!fight)
            return PlayerStrikeKind::None;

        const int move = static_cast<int>(fight->m_nCurrentMove);
        const int style = static_cast<unsigned char>(fight->m_nComboSet);
        const int weaponId = static_cast<int>(weaponType);
        const bool unarmed = weaponId <= 1; // fists / brass knuckles

        // Normal combo slots are 0..2. GTA also uses additional fight slots
        // for special/finisher attacks such as kicking a downed ped. The old
        // classifier rejected every move outside 0..2, so those attacks never
        // reached Camera Response at all.
        const bool specialGroundLikeMove = move < 0 || move > 2;

        switch (style) {
        case STYLE_KUNG_FU:
        case STYLE_KNEE_HEAD:
            return unarmed ? PlayerStrikeKind::Kick : PlayerStrikeKind::None;

        case STYLE_GRAB_KICK:
            if (!unarmed)
                return PlayerStrikeKind::None;
            return move == 0 ?
                PlayerStrikeKind::Punch :
                PlayerStrikeKind::Kick;

        case STYLE_STANDARD:
        case STYLE_BOXING:
            if (!unarmed)
                return PlayerStrikeKind::None;
            return specialGroundLikeMove ?
                PlayerStrikeKind::Kick :
                PlayerStrikeKind::Punch;

        default:
            // Some unarmed special attacks use a transient/non-style combo
            // selector. Keep them responsive when a real fight task + anim is
            // active, while weapon fights still fall through to their own path.
            if (unarmed && fight->m_pAnim)
                return specialGroundLikeMove ?
                    PlayerStrikeKind::Kick :
                    PlayerStrikeKind::Punch;
            return PlayerStrikeKind::None;
        }
    }

    static void PlayerStrikeProfile(
        const CTaskSimpleFight* fight,
        float& releaseScale,
        float& sideScale,
        float& verticalScale,
        float& phaseOffset
    ) {
        releaseScale = 1.0f;
        sideScale = 1.0f;
        verticalScale = 1.0f;
        phaseOffset = 0.0f;
        if (!fight)
            return;

        const int style =
            static_cast<unsigned char>(fight->m_nComboSet);
        const int move =
            Clamp(static_cast<int>(fight->m_nCurrentMove), 0, 2);

        // GTA's fight task exposes three active move slots. Rather than giving
        // every slot the same camera envelope, profile the stock combo steps:
        // quick opener -> lateral/heavier follow-up -> strongest finisher.
        static const float kMoveRelease[3] = { 0.86f, 1.03f, 1.18f };
        static const float kMoveSide[3]    = { 0.72f, 1.08f, 1.20f };
        static const float kMoveVertical[3]= { 0.82f, 1.00f, 1.18f };
        static const float kMovePhase[3]   = {-0.035f, 0.000f, 0.025f };

        releaseScale = kMoveRelease[move];
        sideScale = kMoveSide[move];
        verticalScale = kMoveVertical[move];
        phaseOffset = kMovePhase[move];

        if (style == STYLE_BOXING) {
            // Boxing stays compact except for the final heavy/uppercut slot.
            sideScale *= move == 2 ? 0.92f : 0.78f;
            verticalScale *= move == 2 ? 1.28f : 0.72f;
        } else if (style == STYLE_KUNG_FU) {
            sideScale *= move == 2 ? 1.25f : 1.05f;
            verticalScale *= 1.15f;
            releaseScale *= 1.06f;
        } else if (style == STYLE_KNEE_HEAD) {
            sideScale *= 0.82f;
            verticalScale *= 1.32f;
            releaseScale *= 1.08f;
        } else if (style == STYLE_GRAB_KICK && move > 0) {
            verticalScale *= 1.18f;
            releaseScale *= 1.10f;
        }
    }

    float FightAnimationPhase(const CTaskSimpleFight* fight) const {
        if (!fight || !fight->m_pAnim || !fight->m_pAnim->m_pHierarchy)
            return 0.0f;

        const float total = fight->m_pAnim->m_pHierarchy->m_fTotalTime;
        if (total <= 0.0001f)
            return 0.0f;

        return Clamp(fight->m_pAnim->m_fCurrentTime / total, 0.0f, 1.0f);
    }

    void ResetPlayerFightTracking() {
        lastFightTask_ = nullptr;
        lastFightAnim_ = nullptr;
        lastFightMove_ = -1;
        lastFightPhase_ = 0.0f;
        fightReleaseTriggered_ = false;
    }

    bool UpdateDetailedPlayerMelee(CPlayerPed* p, eWeaponType weaponType, bool madeContact) {
        AuditHit(AUD_UpdateDetailedPlayerMelee);
        if (!cfg_.meleeEnabled || !cfg_.detailedPlayerMelee ||
            !p || !p->m_pIntelligence) {
            ResetPlayerFightTracking();
            return false;
        }

        CTaskSimpleFight* fight =
            p->m_pIntelligence->GetTaskFighting();

        const PlayerStrikeKind kind =
            ClassifyPlayerStrike(fight, weaponType);

        if (!fight || kind == PlayerStrikeKind::None || !fight->m_pAnim) {
            ResetPlayerFightTracking();
            return false;
        }

        const int move = static_cast<int>(fight->m_nCurrentMove);

        const int meleeProfileId =
            Clamp(static_cast<int>(weaponType), 0, 15);

        CPad* meleePad =
            p->GetPadFromPlayer();

        const bool meleeLockOn =
            (meleePad && meleePad->GetTarget()) ||
            p->bIsAimingGun;

        const float weaponProfileScale =
            cfg_.meleeWeaponScale[meleeProfileId] *
            cfg_.presetMelee *
            (meleeLockOn
                ? cfg_.meleeWeaponLockOnScale[meleeProfileId]
                : 1.0f);

        const float weaponSideScale =
            cfg_.meleeWeaponSideWeight[meleeProfileId];

        const float weaponVerticalScale =
            cfg_.meleeWeaponVerticalWeight[meleeProfileId];

        const bool specialGroundKick =
            kind == PlayerStrikeKind::Kick && (move < 0 || move > 2);
        const float phase = FightAnimationPhase(fight);

        float moveReleaseScale = 1.0f;
        float moveSideScale = 1.0f;
        float moveVerticalScale = 1.0f;
        float movePhaseOffset = 0.0f;
        PlayerStrikeProfile(
            fight,
            moveReleaseScale,
            moveSideScale,
            moveVerticalScale,
            movePhaseOffset
        );

        const bool attackChanged =
            fight != lastFightTask_ ||
            fight->m_pAnim != lastFightAnim_ ||
            move != lastFightMove_ ||
            phase + 0.20f < lastFightPhase_;

        if (attackChanged) {
            ++meleeAttackSerial_;
            fightReleaseTriggered_ = false;

            const float sign =
                ((meleeAttackSerial_ + static_cast<unsigned>(move)) & 1u) ?
                1.0f : -1.0f;

            const float windup =
                kind == PlayerStrikeKind::Kick ?
                cfg_.kickWindup :
                cfg_.punchWindup;

            const float side =
                kind == PlayerStrikeKind::Kick ?
                cfg_.kickSide :
                cfg_.punchSide;

            // Anticipation: the camera lags very slightly opposite the body
            // drive before the strike is released.
            impactSpring_.forward.AddImpulse(
                windup * 0.42f * weaponProfileScale * (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
            );
            impactSpring_.vertical.AddImpulse(
                -windup *
                (kind == PlayerStrikeKind::Kick ? 0.28f : 0.16f) *
                weaponVerticalScale *
                weaponProfileScale *
                (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
            );
            impactSpring_.side.AddImpulse(
                -sign * side * moveSideScale * weaponSideScale * 0.32f * weaponProfileScale * (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
            );

            if (cfg_.debug)
                log_.Write(
                    "MELEE start kind=%s style=%d move=%d anim=%d phase=%.3f serial=%u",
                    specialGroundKick ? "ground-kick" : (kind == PlayerStrikeKind::Kick ? "kick" : "punch"),
                    static_cast<int>(static_cast<unsigned char>(fight->m_nComboSet)),
                    move,
                    fight->m_pAnim ? static_cast<int>(fight->m_pAnim->m_nAnimId) : -1,
                    phase,
                    meleeAttackSerial_
                );
        }

        const float releasePhase =
            Clamp(
                (kind == PlayerStrikeKind::Kick ?
                    cfg_.kickReleasePhase :
                    cfg_.punchReleasePhase) +
                movePhaseOffset,
                0.10f,
                0.85f
            );

        if (!fightReleaseTriggered_ &&
            ((lastFightPhase_ < releasePhase && phase >= releasePhase) ||
             (attackChanged && phase >= releasePhase))) {
            fightReleaseTriggered_ = true;

            const float comboScale =
                std::min(
                    cfg_.meleeComboMaxScale,
                    1.0f +
                    cfg_.meleeComboStep *
                    static_cast<float>(
                        std::max(
                            0,
                            std::min(
                                4,
                                static_cast<int>(fight->m_nChainCounter)
                            )
                        )
                    )
                );

            const float specialScale = specialGroundKick ? 1.45f : 1.0f;
            const float release =
                (kind == PlayerStrikeKind::Kick ?
                    cfg_.kickRelease :
                    cfg_.punchRelease) *
                moveReleaseScale *
                comboScale *
                specialScale *
                weaponProfileScale *
                (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat);

            const float side =
                kind == PlayerStrikeKind::Kick ?
                cfg_.kickSide :
                cfg_.punchSide;

            const float vertical =
                kind == PlayerStrikeKind::Kick ?
                cfg_.kickVertical :
                cfg_.punchVertical;

            const float sign =
                ((meleeAttackSerial_ + static_cast<unsigned>(move)) & 1u) ?
                1.0f : -1.0f;

            // Release is intentionally present even on a miss so the camera
            // follows CJ's body mechanics, not just hit registration.
            impactSpring_.forward.AddImpulse(-release);
            impactSpring_.vertical.AddImpulse(vertical * moveVerticalScale * weaponVerticalScale * comboScale * (specialGroundKick ? 1.35f : 1.0f) * weaponProfileScale * (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat));
            impactSpring_.side.AddImpulse(sign * side * moveSideScale * weaponSideScale * 0.72f * comboScale * weaponProfileScale * (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat));

            if (cfg_.debug)
                log_.Write(
                    "MELEE release kind=%s style=%d move=%d phase=%.3f combo=%.3f",
                    specialGroundKick ? "ground-kick" : (kind == PlayerStrikeKind::Kick ? "kick" : "punch"),
                    static_cast<int>(static_cast<unsigned char>(fight->m_nComboSet)),
                    move,
                    phase,
                    comboScale
                );
        }

        if (madeContact) {
            const float contact =
                (kind == PlayerStrikeKind::Kick ?
                    cfg_.kickContact :
                    cfg_.punchContact) *
                cfg_.meleeWeaponContactScale[meleeProfileId] *
                weaponProfileScale *
                (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat);

            const float side =
                kind == PlayerStrikeKind::Kick ?
                cfg_.kickSide :
                cfg_.punchSide;

            const float sign =
                ((meleeAttackSerial_ + static_cast<unsigned>(move)) & 1u) ?
                1.0f : -1.0f;

            // Contact is a short secondary jolt layered on top of the body's
            // release.  Kicks get more vertical weight; punches stay tighter.
            impactSpring_.forward.AddImpulse(-contact * 0.72f);
            impactSpring_.vertical.AddImpulse(
                contact *
                (kind == PlayerStrikeKind::Kick ? 0.42f : 0.18f) *
                moveVerticalScale
            );
            impactSpring_.side.AddImpulse(sign * side * moveSideScale * 0.38f * (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat));

            ++meleeContacts_;

            if (cfg_.debug)
                log_.Write(
                    "MELEE contact kind=%s style=%d move=%d phase=%.3f contacts=%u",
                    specialGroundKick ? "ground-kick" : (kind == PlayerStrikeKind::Kick ? "kick" : "punch"),
                    static_cast<int>(static_cast<unsigned char>(fight->m_nComboSet)),
                    move,
                    phase,
                    meleeContacts_
                );
        }

        lastFightTask_ = fight;
        lastFightAnim_ = fight->m_pAnim;
        lastFightMove_ = move;
        lastFightPhase_ = phase;
        return true;
    }


    void UpdateContinuousWeaponTexture(
        CPlayerPed* p,
        CWeapon* weapon,
        CTaskSimpleFight* fight,
        bool madeContact,
        float dt
    ) {
        if (!p || !weapon) {
            chainsawActivityBlend_ = 0.0f;
            chainsawContactBlend_ = 0.0f;
            chainsawForwardBias_ = 0.0f;
            chainsawContactUntilMs_ = 0;
            lastChainsawMode_ = -1;
            continuousToolBlend_ = 0.0f;
            lastContinuousToolId_ = -1;
            return;
        }

        const int id = static_cast<int>(weapon->m_eWeaponType);
        const unsigned now = CTimer::m_snTimeInMilliseconds;

        if (id == 9) {
            const bool haveFightAnim =
                fight && fight->m_pAnim;

            const bool activeAttack =
                haveFightAnim ||
                weapon->m_nState == WEAPONSTATE_FIRING ||
                weapon->m_nState == WEAPONSTATE_MELEE_MADECONTACT;

            if (madeContact ||
                weapon->m_nState == WEAPONSTATE_MELEE_MADECONTACT) {
                chainsawContactUntilMs_ =
                    now + static_cast<unsigned>(
                        cfg_.chainsawContactHoldMs
                    );
            }

            bool sustainedCutHint = false;
            int move = -1;
            if (haveFightAnim) {
                move = static_cast<int>(
                    fight->m_nCurrentMove
                );

                // GTA does not consistently expose MELEE_MADECONTACT for the
                // chainsaw. The higher fight move slots observed during the
                // sustained cutting sequence are used only while a live fight
                // animation is present.
                sustainedCutHint = move >= 3;
            }

            const bool contactLike =
                now < chainsawContactUntilMs_ ||
                (activeAttack && sustainedCutHint);

            const float activityTarget =
                activeAttack ? 1.0f : 0.0f;
            const float contactTarget =
                contactLike ? 1.0f : 0.0f;

            chainsawActivityBlend_ +=
                (activityTarget - chainsawActivityBlend_) *
                (1.0f - std::exp(-10.0f * dt));

            chainsawContactBlend_ +=
                (contactTarget - chainsawContactBlend_) *
                (1.0f - std::exp(-14.0f * dt));

            const float idleW =
                1.0f - chainsawActivityBlend_;
            const float attackW =
                chainsawActivityBlend_ *
                (1.0f - chainsawContactBlend_);
            const float sawW =
                chainsawContactBlend_;

            const float amplitude =
                cfg_.chainsawIdleVibration * idleW +
                cfg_.chainsawAttackVibration * attackW +
                cfg_.chainsawSawVibration * sawW;

            const float hz =
                cfg_.chainsawIdleHz * idleW +
                cfg_.chainsawAttackHz * attackW +
                cfg_.chainsawSawHz * sawW;

            const float n =
                chainsawNoise_.Update(
                    dt,
                    hz,
                    12.0f + hz * 0.45f
                );

            impactSpring_.side.position +=
                n *
                amplitude *
                (0.22f +
                 0.18f * chainsawActivityBlend_);

            impactSpring_.vertical.position +=
                (
                    n * 0.36f +
                    std::fabs(n) * 0.38f
                ) *
                amplitude;

            // Do not integrate a one-way forward load into the shared impact
            // spring every frame. That used to build a large negative spring
            // displacement (often clamped at MaxFinalForward) and could remain
            // visible long after the saw stopped. Keep it as a bounded,
            // frame-local bias instead.
            chainsawForwardBias_ =
                -cfg_.chainsawForwardLoad *
                (
                    0.18f * chainsawActivityBlend_ +
                    0.82f * chainsawContactBlend_
                );

            if (cfg_.frequencyBandsEnabled &&
                amplitude > 0.001f) {
                cr::CameraResponseEvent tex{};
                tex.source =
                    cr::ResponseSource::Combat;
                tex.band =
                    contactLike
                        ? cr::ResponseBand::High
                        : cr::ResponseBand::Mid;
                tex.impulse.side =
                    n * amplitude * 0.20f;
                tex.impulse.vertical =
                    n * amplitude * 0.30f;
                tex.impulse.forward =
                    -cfg_.chainsawForwardLoad *
                    chainsawContactBlend_ *
                    0.20f;
                tex.magnitude =
                    contactLike ? 1.10f :
                    (activeAttack ? 0.75f : 0.30f);
                tex.priority =
                    contactLike ? 1.30f : 0.75f;
                tex.tag = 0x43485357u;
                responseMixer_.Publish(tex);
            }

            const int mode =
                contactLike ? 2 :
                (activeAttack ? 1 : 0);

            if (cfg_.debug &&
                mode != lastChainsawMode_) {
                log_.Write(
                    "CHAINSAW MODE mode=%s attackBlend=%.2f "
                    "contactBlend=%.2f state=%d move=%d",
                    mode == 2 ? "sawing" :
                    (mode == 1 ? "attack" : "idle"),
                    chainsawActivityBlend_,
                    chainsawContactBlend_,
                    static_cast<int>(weapon->m_nState),
                    move
                );
                lastChainsawMode_ = mode;
            }

            return;
        }

        // Leaving the chainsaw must immediately release all chainsaw-only
        // state. In particular, never carry contact/activity blends or the
        // frame-local forward bias into another weapon.
        if (chainsawActivityBlend_ > 0.0f ||
            chainsawContactBlend_ > 0.0f ||
            chainsawForwardBias_ != 0.0f ||
            chainsawContactUntilMs_ != 0 ||
            lastChainsawMode_ != -1) {
            if (cfg_.debug && lastChainsawMode_ != -1) {
                log_.Write(
                    "CHAINSAW RESET reason=weapon-change newWeapon=%d "
                    "attackBlend=%.2f contactBlend=%.2f forwardBias=%.4f",
                    id,
                    chainsawActivityBlend_,
                    chainsawContactBlend_,
                    chainsawForwardBias_
                );
            }

            chainsawActivityBlend_ = 0.0f;
            chainsawContactBlend_ = 0.0f;
            chainsawForwardBias_ = 0.0f;
            chainsawContactUntilMs_ = 0;
            lastChainsawMode_ = -1;
            chainsawNoise_.Reset(0x51A7B22Du);
        }

        // GTA's CWeapon owns an FxSystem specifically for continuous
        // flamethrower/spraycan/extinguisher effects. Represent those as a
        // sustained texture while WEAPONSTATE_FIRING instead of discrete
        // firearm recoil.
        if (id == 37 || id == 41 || id == 42) {
            const bool active =
                weapon->m_nState == WEAPONSTATE_FIRING;

            if (lastContinuousToolId_ != id) {
                continuousToolBlend_ = 0.0f;
                continuousToolNoise_.Reset(
                    0xC0717A11u +
                    static_cast<unsigned>(id)
                );
                lastContinuousToolId_ = id;
            }

            continuousToolBlend_ +=
                ((active ? 1.0f : 0.0f) -
                 continuousToolBlend_) *
                (
                    1.0f -
                    std::exp(
                        -(active ? 12.0f : 7.0f) *
                        dt
                    )
                );

            if (continuousToolBlend_ > 0.002f) {
                float scale = 1.0f;
                float hz = 18.0f;

                if (id == 37) {
                    scale = 1.35f;
                    hz = 20.0f;
                } else if (id == 41) {
                    scale = 0.65f;
                    hz = 15.0f;
                } else if (id == 42) {
                    scale = 0.85f;
                    hz = 17.0f;
                }

                const float amp =
                    (cfg_.continuousToolVibration * cfg_.presetAmbient) *
                    scale *
                    continuousToolBlend_;

                const float n =
                    continuousToolNoise_.Update(
                        dt,
                        hz,
                        14.0f
                    );

                impactSpring_.side.position +=
                    n * amp * 0.24f;

                impactSpring_.vertical.position +=
                    n * amp * 0.34f;

                impactSpring_.forward.position -=
                    amp * 0.18f;
            }
        } else {
            continuousToolBlend_ *=
                std::exp(-8.0f * dt);

            if (continuousToolBlend_ < 0.001f)
                lastContinuousToolId_ = -1;
        }
    }

    bool UpdateDetailedMeleeWeapon(
        CPlayerPed* p,
        eWeaponType type,
        bool madeContact,
        bool fireInputEdge,
        float dt
    ) {
        AuditHit(AUD_UpdateDetailedMeleeWeapon);

        if (!cfg_.meleeEnabled ||
            !cfg_.detailedMeleeWeapons ||
            !p) {
            return false;
        }

        const int id = static_cast<int>(type);
        if (id < 2 || id > 15)
            return false;

        CWeapon* weapon = p->GetWeapon();
        if (!weapon)
            return false;

        const unsigned now =
            CTimer::m_snTimeInMilliseconds;

        const eWeaponState weaponState =
            weapon->m_nState;

        // Chainsaw is stateful/continuous. UpdateContinuousWeaponTexture()
        // already handles idle/attack/cutting/contact. Treating its looping
        // fight animation phase resets as discrete melee swings generated a
        // stream of false START/RELEASE impulses and could sustain shake.
        if (id == 9) {
            // Chainsaw remains continuous/stateful, but controller lock-on can
            // suppress the fight-task transition GTA normally exposes. Give
            // the initial fire edge a compact start jolt; sustained cutting
            // remains owned by UpdateContinuousWeaponTexture().
            if (fireInputEdge) {
                CPad* chainsawPad=p->GetPadFromPlayer();
                const bool chainsawLockOn=(chainsawPad && chainsawPad->GetTarget()) || p->bIsAimingGun;
                const float chainsawProfile=
                    cfg_.meleeWeaponScale[id] *
                    cfg_.presetMelee *
                    (chainsawLockOn ? cfg_.meleeWeaponLockOnScale[id] : 1.0f);
                impactSpring_.forward.AddImpulse(
                    -0.085f * chainsawProfile *
                    (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
                );
                impactSpring_.vertical.AddImpulse(
                    0.018f * cfg_.meleeWeaponVerticalWeight[id] * chainsawProfile *
                    (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
                );
                impactSpring_.side.AddImpulse(
                    RandomSigned() *
                    0.025f * cfg_.meleeWeaponSideWeight[id] * chainsawProfile *
                    (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
                );

                AddFovKick(
                    cfg_.fovMeleeKick *
                    0.55f
                );

                if (cfg_.debug) {
                    log_.Write(
                        "CHAINSAW ATTACK START source=controller-fire-edge"
                    );
                }
            }

            lastMeleeWeaponState_ = weaponState;
            lastMeleeWeaponNextShot_ = weapon->m_nTimeForNextShot;
            lastWeaponFightAnim_ = nullptr;
            lastWeaponFightMove_ = -1;
            lastWeaponFightPhase_ = 0.0f;
            weaponFightReleaseTriggered_ = false;
            return true;
        }

        const unsigned nextShot =
            weapon->m_nTimeForNextShot;

        CPad* meleePad=p->GetPadFromPlayer();
        const bool lockOn=(meleePad && meleePad->GetTarget()) || p->bIsAimingGun;
        float weight=
            cfg_.meleeWeaponScale[id] *
            cfg_.presetMelee *
            (lockOn ? cfg_.meleeWeaponLockOnScale[id] : 1.0f);
        const float sideWeight=cfg_.meleeWeaponSideWeight[id];
        const float verticalWeight=cfg_.meleeWeaponVerticalWeight[id];
        const float releaseForward=cfg_.meleeWeaponReleaseForward[id];
        const float releaseSide=cfg_.meleeWeaponReleaseSide[id];
        const float contactScale=cfg_.meleeWeaponContactScale[id];
        const unsigned fallbackCooldownMs=cfg_.meleeWeaponCooldownMs[id];

        CTaskSimpleFight* fight =
            p->m_pIntelligence
                ? p->m_pIntelligence->GetTaskFighting()
                : nullptr;

        const bool haveFightAnim =
            fight && fight->m_pAnim;

        bool handledSwing = false;

        // Preferred path: use GTA's live fight animation phase when present.
        if (haveFightAnim) {
            const float phase =
                FightAnimationPhase(fight);

            const int move =
                static_cast<int>(fight->m_nCurrentMove);

            const bool changed =
                fight->m_pAnim != lastWeaponFightAnim_ ||
                move != lastWeaponFightMove_ ||
                phase + 0.20f < lastWeaponFightPhase_;

            if (changed) {
                ++weaponMeleeSerial_;
                weaponFightReleaseTriggered_ = false;
                lastMeleeWeaponSwingMs_ = now;

                const float sign =
                    (weaponMeleeSerial_ & 1u)
                        ? 1.0f
                        : -1.0f;

                impactSpring_.forward.AddImpulse(
                    0.08f *
                    weight *
                    (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
                );

                impactSpring_.side.AddImpulse(
                    -sign *
                    0.06f *
                    sideWeight *
                    weight *
                    (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
                );

                handledSwing = true;

                if (cfg_.debug) {
                    log_.Write(
                        "MELEE WEAPON START weapon=%d source=fight-task "
                        "move=%d phase=%.3f",
                        id,
                        move,
                        phase
                    );
                }
            }

            const float releasePhase =
                id == 4
                    ? 0.28f
                    : (id == 9 ? 0.18f : 0.36f);

            if (!weaponFightReleaseTriggered_ &&
                phase >= releasePhase) {
                weaponFightReleaseTriggered_ = true;
                lastMeleeWeaponSwingMs_ = now;

                const float sign =
                    (weaponMeleeSerial_ & 1u)
                        ? 1.0f
                        : -1.0f;

                impactSpring_.forward.AddImpulse(
                    -releaseForward *
                    weight *
                    (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
                );

                impactSpring_.side.AddImpulse(
                    sign *
                    releaseSide *
                    sideWeight *
                    weight *
                    (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
                );

                impactSpring_.vertical.AddImpulse(
                    verticalWeight *
                    weight *
                    (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
                );

                handledSwing = true;
                AddFovKick(cfg_.fovMeleeKick * Clamp(weight, 0.5f, 1.5f));

                if (cfg_.debug) {
                    log_.Write(
                        "MELEE WEAPON RELEASE weapon=%d source=fight-task "
                        "move=%d phase=%.3f",
                        id,
                        move,
                        phase
                    );
                }
            }

            lastWeaponFightAnim_ = fight->m_pAnim;
            lastWeaponFightMove_ = move;
            lastWeaponFightPhase_ = phase;
        }

        // Fallback path:
        // Some GTA melee weapons, especially the police nightstick/baton, can
        // advance weapon firing/cooldown state without exposing a usable
        // CTaskSimpleFight animation through GetTaskFighting(). Detect the
        // actual weapon cycle instead of silently returning false.
        const bool firingEdge =
            weaponState == WEAPONSTATE_FIRING &&
            lastMeleeWeaponState_ != WEAPONSTATE_FIRING;

        const bool nextShotAdvanced =
            nextShot != 0 &&
            nextShot != lastMeleeWeaponNextShot_ &&
            nextShot > lastMeleeWeaponNextShot_;

        const bool fallbackReady =
            lastMeleeWeaponSwingMs_ == 0 ||
            now - lastMeleeWeaponSwingMs_ >=
                fallbackCooldownMs;

        if (!handledSwing &&
            fallbackReady &&
            (firingEdge ||
             nextShotAdvanced ||
             fireInputEdge)) {
            ++weaponMeleeSerial_;
            lastMeleeWeaponSwingMs_ = now;
            weaponFightReleaseTriggered_ = true;

            const float sign =
                (weaponMeleeSerial_ & 1u)
                    ? 1.0f
                    : -1.0f;

            // A compact anticipation + release combined response. This path
            // intentionally does not wait for contact, so missed baton/bat
            // swings still move the camera naturally.
            impactSpring_.forward.AddImpulse(
                -releaseForward *
                0.92f *
                weight *
                (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
            );

            impactSpring_.side.AddImpulse(
                sign *
                releaseSide *
                sideWeight *
                weight *
                (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
            );

            impactSpring_.vertical.AddImpulse(
                verticalWeight *
                0.88f *
                weight *
                (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
            );

            if (cfg_.frequencyBandsEnabled) {
                cr::CameraResponseEvent swing{};
                swing.source =
                    cr::ResponseSource::Combat;
                swing.band =
                    cr::ResponseBand::Mid;
                swing.impulse.side =
                    sign *
                    releaseSide *
                    sideWeight *
                    0.35f;
                swing.impulse.vertical =
                    verticalWeight *
                    0.20f;
                swing.impulse.forward =
                    -releaseForward *
                    0.42f;
                swing.magnitude =
                    Clamp(weight, 0.45f, 1.50f);
                swing.priority = 1.25f;
                swing.tag = 0x4D57504Eu; // MWPN
                responseMixer_.Publish(swing);
            }

            handledSwing = true;
            AddFovKick(cfg_.fovMeleeKick * Clamp(weight, 0.5f, 1.5f));

            if (cfg_.debug) {
                log_.Write(
                    "MELEE WEAPON RELEASE weapon=%d source=weapon-cycle "
                    "state=%d nextShot=%u firingEdge=%d timingEdge=%d inputEdge=%d",
                    id,
                    static_cast<int>(weaponState),
                    nextShot,
                    firingEdge ? 1 : 0,
                    nextShotAdvanced ? 1 : 0,
                    fireInputEdge ? 1 : 0
                );
            }
        }

        if (madeContact) {
            impactSpring_.forward.AddImpulse(
                -cfg_.meleeImpact *
                0.82f *
                contactScale *
                weight *
                (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
            );

            impactSpring_.vertical.AddImpulse(
                cfg_.meleeImpact *
                verticalWeight *
                weight *
                (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
            );

            impactSpring_.side.AddImpulse(
                RandomSigned() *
                cfg_.meleeSide *
                sideWeight *
                weight *
                (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat)
            );

            ++meleeContacts_;
            handledSwing = true;
            AddFovKick(cfg_.fovMeleeKick * 0.55f *
                Clamp(weight, 0.5f, 1.5f));

            if (cfg_.debug) {
                log_.Write(
                    "MELEE WEAPON CONTACT weapon=%d",
                    id
                );
            }
        }

        lastMeleeWeaponState_ = weaponState;
        lastMeleeWeaponNextShot_ = nextShot;

        return handledSwing || haveFightAnim;
    }

    void UpdateWeapon(CPlayerPed* p, CPad* pad) {
        AuditHit(AUD_UpdateWeapon);
        CWeapon* w = p->GetWeapon();

        if (!w) {
            weaponInit_ = false;
            previousMeleeFireInput_ = false;
            return;
        }

        const eWeaponType type =
            w->m_eWeaponType;

        const unsigned clip =
            w->m_nAmmoInClip;

        const unsigned total =
            w->m_nAmmoTotal;

        const unsigned nextShot =
            w->m_nTimeForNextShot;

        const eWeaponState state =
            w->m_nState;

        const CTaskSimpleJetPack* jetpackTask = p->m_pIntelligence
            ? p->m_pIntelligence->GetTaskJetPack() : nullptr;
        const bool fireInput =
            (pad && pad->NewState.ButtonCircle != 0) ||
            (jetpackTask && jetpackTask->m_bAttackButtonPressed);

        const bool meleeFireEdge =
            fireInput &&
            !previousMeleeFireInput_;

        const bool meleeContact =
            cfg_.meleeEnabled &&
            state == WEAPONSTATE_MELEE_MADECONTACT &&
            prevWeaponState_ != WEAPONSTATE_MELEE_MADECONTACT;

        CTaskSimpleFight* activeFight =
            p->m_pIntelligence
                ? p->m_pIntelligence->GetTaskFighting()
                : nullptr;

        UpdateContinuousWeaponTexture(
            p,
            w,
            activeFight,
            meleeContact,
            Dt()
        );

        const bool detailedPlayerMelee =
            UpdateDetailedPlayerMelee(p, type, meleeContact);
        const bool detailedWeaponMelee =
            !detailedPlayerMelee &&
            UpdateDetailedMeleeWeapon(
                p,
                type,
                meleeContact,
                meleeFireEdge,
                Dt()
            );

        if (!weaponInit_) {
            prevWeapon_ = type;
            prevAmmo_ = clip;
            prevAmmoTotal_ = total;
            prevNextShotMs_ = nextShot;
            prevWeaponState_ = state;
            previousMeleeFireInput_ = fireInput;
            weaponInit_ = true;
            return;
        }

        if (type != prevWeapon_) {
            if (cfg_.advancedResponseEnabled) {
                const int id = static_cast<int>(type);
                float weight = 0.75f;
                if (id >= 25 && id <= 27) weight = 1.20f;
                else if (id == 34 || id == 35 || id == 36 || id == 38) weight = 1.35f;
                else if (id >= 30 && id <= 33) weight = 1.05f;

                const float impulse =
                    cfg_.equipResponseStrength *
                    weight *
                    (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat);

                impactSpring_.vertical.AddImpulse(-impulse * 0.32f);
                impactSpring_.forward.AddImpulse(-impulse * 0.70f);
                impactSpring_.side.AddImpulse(RandomSigned() * impulse * 0.18f);

                if (cfg_.debug)
                    log_.Write("WEAPON EQUIP weapon=%d impulse=%.3f", id, impulse);
            }

            prevWeapon_ = type;
            prevAmmo_ = clip;
            prevAmmoTotal_ = total;
            prevNextShotMs_ = nextShot;
            prevWeaponState_ = state;
            previousMeleeFireInput_ = fireInput;
            return;
        }

        // PRIMARY signal:
        // A clip decrease is a real ammo-consuming shot. Do NOT require the
        // weapon to still be WEAPONSTATE_FIRING here: gameProcessEvent runs
        // after gameplay processing and short-lived firing states can already
        // have changed by the time this sampler sees the weapon.
        const unsigned clipSpent =
            prevAmmo_ > clip ?
            prevAmmo_ - clip :
            0u;
        // Some flight/weapon paths consume reserve ammo while restoring the
        // clip. Clip and total represent the same shots, so use max, not sum.
        const unsigned totalSpent = jetpackTask && prevAmmoTotal_ > total
            ? prevAmmoTotal_ - total : 0u;
        const unsigned roundsSpent = std::max(clipSpent, totalSpent);

        const bool ammoShot =
            roundsSpent > 0;

        // FALLBACK signal:
        // m_nTimeForNextShot is a timestamp in current plugin-sdk's CWeapon.
        // It advances when a new shot schedules its cooldown. This catches
        // infinite-ammo cheats and weapon paths where the clip is unchanged.
        const bool nextShotAdvanced =
            nextShot != 0 &&
            nextShot != prevNextShotMs_ &&
            nextShot > prevNextShotMs_;

        const bool firingState =
            state == WEAPONSTATE_FIRING;

        // CPad's reconciled ButtonCircle is the normal ped fire action,
        // including keyboard/mouse/controller input after GTA input mapping.
        const bool timingShot =
            !ammoShot &&
            nextShotAdvanced &&
            state != WEAPONSTATE_RELOADING &&
            prevWeaponState_ != WEAPONSTATE_RELOADING &&
            clip <= prevAmmo_ &&
            (firingState || fireInput);

        if (cfg_.advancedResponseEnabled) {
            const bool reloadStart =
                state == WEAPONSTATE_RELOADING &&
                prevWeaponState_ != WEAPONSTATE_RELOADING;

            const bool reloadCycle =
                state == WEAPONSTATE_RELOADING &&
                clip > prevAmmo_;

            const bool reloadEnd =
                cfg_.reloadAnimationResponse &&
                state != WEAPONSTATE_RELOADING &&
                prevWeaponState_ == WEAPONSTATE_RELOADING;

            if (reloadStart || reloadCycle || reloadEnd) {
                const int id = static_cast<int>(type);
                float weight = 1.0f;
                if (id >= 25 && id <= 27) weight = 1.25f;
                else if (id == 35 || id == 36) weight = 1.35f;
                else if (id >= 30 && id <= 34) weight = 1.12f;

                float impulse =
                    cfg_.reloadResponseStrength *
                    weight *
                    (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat);

                if (reloadCycle && !reloadStart)
                    impulse *= cfg_.reloadInsertScale;
                if (reloadEnd)
                    impulse *= cfg_.reloadActionScale * 0.72f;

                impactSpring_.vertical.AddImpulse(
                    impulse * (reloadEnd ? 0.24f : -0.40f)
                );
                impactSpring_.forward.AddImpulse(
                    impulse * (reloadEnd ? -0.52f : 0.58f)
                );
                impactSpring_.side.AddImpulse(RandomSigned() * impulse * 0.12f);

                if (cfg_.debug)
                    log_.Write(
                        "WEAPON RELOAD weapon=%d start=%d cycle=%d impulse=%.3f",
                        id,
                        reloadStart ? 1 : 0,
                        reloadCycle ? 1 : 0,
                        impulse
                    );
            }
        }

        if (meleeContact && !detailedPlayerMelee && !detailedWeaponMelee) {
            const float side = RandomSigned();
            impactSpring_.forward.AddImpulse(-cfg_.meleeImpact * (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat));
            impactSpring_.vertical.AddImpulse(cfg_.meleeImpact * 0.18f * (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat));
            impactSpring_.side.AddImpulse(side * cfg_.meleeSide * (cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat));
            ++meleeContacts_;
            if (cfg_.debug)
                log_.Write("MELEE generic-contact weapon=%d contacts=%u", static_cast<int>(type), meleeContacts_);
        }

        if (ammoShot) {
            TriggerWeaponRecoil(
                type,
                roundsSpent,
                clipSpent ? "ammo" : "jetpack-total-ammo"
            );
        }
        else if (timingShot) {
            TriggerWeaponRecoil(
                type,
                1,
                "next-shot"
            );
        }

        prevAmmo_ = clip;
        prevAmmoTotal_ = total;
        prevNextShotMs_ = nextShot;
        prevWeaponState_ = state;
        prevWeapon_ = type;
        previousMeleeFireInput_ = fireInput;
    }

    float VehicleProfileScale(CVehicle* v) const {
        if (!v)
            return cfg_.vehicleOtherScale;

        // CVehicle::GetVehicleAppearance() is part of SA's stock vehicle API:
        // 1 automobile, 2 bike, 3 heli, 4 boat, 5 plane.
        switch (v->GetVehicleAppearance()) {
        case 1: return cfg_.vehicleAutomobileScale;
        case 2: return cfg_.vehicleBikeScale;
        case 3: return cfg_.vehicleHeliScale;
        case 4: return cfg_.vehicleBoatScale;
        case 5: return cfg_.vehiclePlaneScale;
        default: return cfg_.vehicleOtherScale;
        }
    }

    void ResetVehicleState() {
        AuditHit(AUD_ResetVehicleState);
        mountedWeaponVehicle_ = nullptr;
        waterEmitter_ = nullptr;
        waterResponseBlend_ = 0.0f;
        waterResponseActive_ = false;
        mountedWaterSide_ = mountedWaterVertical_ = mountedWaterForward_ = 0.0f;
        vehicleSpring_.Reset();
        vehicleSuspensionSpring_.Reset();
        vehicleCollisionSpring_.Reset();
        vehicleInit_ = false;
        lastVehicle_ = nullptr;
        prevVehicleVelocity_ = CVector{};
        prevVehicleHeading_ = 0.0f;
        vehicleSpeed_ = 0.0f;
        vehicleForwardAccel_ = 0.0f;
        vehicleSideAccel_ = 0.0f;
        vehicleVerticalAccelNow_ = 0.0f;
        previousVehicleForwardAccelForJerk_ = 0.0f;
        previousVehicleSideAccelForJerk_ = 0.0f;
        previousVehicleVerticalAccelForJerk_ = 0.0f;
        vehicleTargetSide_ = 0.0f;
        vehicleTargetVertical_ = 0.0f;
        vehicleTargetForward_ = 0.0f;
        vehicleVibrationSide_ = 0.0f;
        vehicleVibrationVertical_ = 0.0f;
        vehicleVibrationForward_ = 0.0f;
        vehicleHornBlend_ = 0.0f;
        vehicleHornSide_ = 0.0f;
        vehicleHornVertical_ = 0.0f;
        vehicleHornForward_ = 0.0f;
        vehicleHornActive_ = false;
        vehicleHornFrames_ = 0;
        vehicleHealthInit_ = false;
        prevVehicleHealth_ = 1000.0f;
        lastVehicleHealthLoss_ = 0.0f;
        vehicleSuspensionJolt_ = 0.0f;
        std::memset(prevVehicleContacts_, 0, sizeof(prevVehicleContacts_));
        prevVehicleContactCount_ = 0;
        prevVehicleCollisionTime_ = 0;
        lastVehicleContactResponseMs_ = 0;
        lastVehicleContactIntensity_ = 0.0f;
        lastVehicleContactImpulse_ = 0.0f;
        prevVehicleDeltaVelMag_ = 0.0f;
        vehicleCollisionKickSide_ = 0.0f;
        vehicleCollisionKickVertical_ = 0.0f;
        vehicleCollisionKickForward_ = 0.0f;
        vehicleSuspensionHandlingStiffness_ = 1.0f;
        vehicleSuspensionHandlingDamping_ = 1.0f;
        vehicleSuspensionHandlingTravel_ = 1.0f;
        vehicleSuspensionHandlingMass_ = 1.0f;
        vehicleSuspensionHandlingBias_ = 0.5f;
        vehicleSuspensionHandlingAntiDive_ = 0.0f;
        vehicleSuspensionHandlingImpulseScale_ = 1.0f;
        vehicleSuspensionHandlingSpring_ = cfg_.vehicleSpring;
        vehicleSuspensionHandlingSpringDamping_ = cfg_.vehicleDamping;
        previousVehicleEngineOn_ = false;
        previousVehicleChangingGear_ = false;
        vehicleEnginePhase_=0.0f;
        vehicleEngineRpmBlend_=0.0f;
        vehicleGearPulse_=0.0f;
        std::memset(previousWheelDamage_, 0, sizeof(previousWheelDamage_));
        previousVehicleWheelsOnGround_ = 0;
        previousVehicleTouchingWater_ = false;
        vehiclePersonalityResponse_ = 1.0f;
        vehiclePersonalityVibration_ = 1.0f;
        advancedVehicleSide_ = 0.0f;
        advancedVehicleVertical_ = 0.0f;
        advancedVehicleForward_ = 0.0f;
        boatWaveInit_ = false;
        previousBoatRoll_ = 0.0f;
        previousBoatPitch_ = 0.0f;
        boatWaveSide_ = 0.0f;
        boatWaveVertical_ = 0.0f;
        boatWaveForward_ = 0.0f;
        boatWavePitchDeg_ = 0.0f;
        boatWaveRollDeg_ = 0.0f;
        lastBoatWaveSlamMs_ = 0;

        aircraftMotionInit_ = false;
        previousAircraftRoll_ = 0.0f;
        previousAircraftPitch_ = 0.0f;
        aircraftResponseSide_ = 0.0f;
        aircraftResponseVertical_ = 0.0f;
        aircraftResponseForward_ = 0.0f;
        aircraftPitchDeg_ = 0.0f;
        aircraftRollDeg_ = 0.0f;

        bmxPedalPhaseInit_ = false;
        previousBmxPedalAngle_ = 0.0f;
        bmxPedalRateSmoothed_ = 0.0f;
        previousVehicleContactWheels_ = 0;
        vehicleImpactSequence_ = 0;
        lastVehicleSequenceMs_ = 0;
    }

    using WaterCannonUpdate = void (__cdecl*)(CVehicle*, CVector*, CVector*);
    inline static WaterCannonUpdate originalWaterCannonUpdate_ = nullptr;
    CVehicle* waterEmitter_ = nullptr;
    unsigned waterEmissionMs_ = 0;
    CVector waterEmissionVelocity_{};
    CVehicle* mountedWeaponVehicle_ = nullptr;
    unsigned mountedGunTime_ = 0, mountedMinigunTime_ = 0;
    unsigned mountedMissileTime_ = 0, mountedAdditionalMissileTime_ = 0;
    float waterResponseBlend_ = 0.0f;
    bool waterResponseActive_ = false;

    static void __cdecl WaterCannonThunk(CVehicle* owner, CVector* origin, CVector* velocity) {
        originalWaterCannonUpdate_(owner, origin, velocity);
        CPlayerPed* p = FindPlayerPed();
        if (instance_ && p && p->m_pVehicle == owner && owner &&
            owner->m_pDriver == p && velocity) {
            instance_->waterEmitter_ = owner;
            instance_->waterEmissionMs_ = CTimer::m_snTimeInMilliseconds;
            instance_->waterEmissionVelocity_ = *velocity;
        }
    }

    void InstallWaterCannonHook() {
        // Verified in the supplied US 1.0 EXE: FireTruckControl submits an
        // actual spray segment here (also used by the SWAT water cannon).
        constexpr uintptr_t site = 0x72A260;
        if (!cr::ClassicSaImageLayout() ||
            !cr::ReadableImageCode(0x728CB0, 5) ||
            DecodeRelativeCallTarget(site) != 0x728CB0) {
            log_.Write("WATER CANNON hook skipped: emission callsite differs from stock");
            return;
        }
        originalWaterCannonUpdate_ = reinterpret_cast<WaterCannonUpdate>(0x728CB0);
        injector::MakeCALL(site, &WaterCannonThunk, true);
        log_.Write("WATER CANNON emission hook installed=%d",
            DecodeRelativeCallTarget(site) == reinterpret_cast<uintptr_t>(&WaterCannonThunk) ? 1 : 0);
    }

    void UpdateMountedWeapons(CPlayerPed* p, CVehicle* v, float dt) {
        mountedWaterSide_ = mountedWaterVertical_ = mountedWaterForward_ = 0.0f;
        if (!p || !v || v->m_pDriver != p || !cfg_.mountedWeaponResponseEnabled) {
            mountedWeaponVehicle_ = nullptr;
            waterResponseBlend_ = 0.0f;
            waterResponseActive_ = false;
            return;
        }
        const bool first = mountedWeaponVehicle_ != v;
        const bool gun = !first && v->m_nGunFiringTime != 0 &&
            v->m_nGunFiringTime != mountedGunTime_;
        const bool minigun = !first && v->m_nTimeForMinigunFiring != 0 &&
            v->m_nTimeForMinigunFiring != mountedMinigunTime_;
        const bool missile = !first &&
            ((v->m_nProjectileWeaponFiringTime != 0 && v->m_nProjectileWeaponFiringTime != mountedMissileTime_) ||
             (v->m_nAdditionalProjectileWeaponFiringTime != 0 && v->m_nAdditionalProjectileWeaponFiringTime != mountedAdditionalMissileTime_));
        mountedWeaponVehicle_ = v;
        mountedGunTime_ = v->m_nGunFiringTime;
        mountedMinigunTime_ = v->m_nTimeForMinigunFiring;
        mountedMissileTime_ = v->m_nProjectileWeaponFiringTime;
        mountedAdditionalMissileTime_ = v->m_nAdditionalProjectileWeaponFiringTime;
        if (first) waterResponseBlend_ = 0.0f;

        const float gain = cfg_.strength * cfg_.presetScale * cfg_.vehicleStrength *
            cfg_.combatResponseScale * cfg_.masterCombatScale * cfg_.presetCombat;
        const bool tank = v->m_nModelIndex == 432 && v->m_nVehicleSubClass == VEHICLE_AUTOMOBILE;
        const bool heavy = missile || (tank && gun);
        if (gun || minigun || missile) {
            float side = 0.0f, forward = -1.0f;
            if (tank) {
                // SDK names this vertical, but offset 0x94C is turret yaw in
                // the executable. Preserve recoil opposite the actual barrel.
                const float yaw = reinterpret_cast<CAutomobile*>(v)->m_fDoomVerticalRotation;
                side = std::sin(yaw);
                forward = -std::cos(yaw);
            }
            float cameraSide = 0.0f, cameraForward = 0.0f;
            VehicleLocalXYToCameraLocal(v, side, forward, cameraSide, cameraForward);
            const float kick = (heavy ? cfg_.mountedHeavyStrength : cfg_.mountedGunStrength) * gain;
            // Immediate displacement remains visible at high FPS and with
            // optional rotation off; existing impact bounds limit the tail.
            impactSpring_.side.position = Clamp(impactSpring_.side.position + cameraSide*kick, -cfg_.maxImpactOffset, cfg_.maxImpactOffset);
            impactSpring_.forward.position = Clamp(impactSpring_.forward.position + cameraForward*kick, -cfg_.maxImpactOffset, cfg_.maxImpactOffset);
            impactSpring_.vertical.position = Clamp(impactSpring_.vertical.position + kick*0.45f, -cfg_.maxImpactOffset, cfg_.maxImpactOffset);
            TouchResponseSource(cr::ResponseOrigin::Recoil, kick, 450);
            if (cfg_.debug) log_.Write("MOUNTED FIRE model=%d gun=%d minigun=%d missile=%d heavy=%d kick=%.4f",
                int(v->m_nModelIndex), gun?1:0, minigun?1:0, missile?1:0, heavy?1:0, kick);
        }
        const unsigned now = CTimer::m_snTimeInMilliseconds;
        const bool spraying = waterEmitter_ == v && now - waterEmissionMs_ <= 80u;
        waterResponseBlend_ += ((spraying ? 1.0f : 0.0f) - waterResponseBlend_) *
            (1.0f - std::exp(-(spraying ? 18.0f : 12.0f)*Clamp(dt,0.0f,0.05f)));
        if (spraying != waterResponseActive_ && cfg_.debug)
            log_.Write("MOUNTED WATER model=%d active=%d", int(v->m_nModelIndex), spraying?1:0);
        waterResponseActive_ = spraying;
        if (waterResponseBlend_ > 0.001f) {
            float side = 0.0f, forward = 0.0f;
            WorldXYToCameraLocal(waterEmissionVelocity_.x, waterEmissionVelocity_.y, side, forward);
            const float length = std::sqrt(side*side+forward*forward+waterEmissionVelocity_.z*waterEmissionVelocity_.z);
            const float amp = cfg_.mountedWaterStrength * gain * waterResponseBlend_;
            const float pulse = 1.0f + 0.22f*std::sin(float(now)*0.055f);
            if (length > 0.001f) {
                // Feed a bounded camera-local pressure offset to the common
                // spring target-free mix, independently of ambient ducking.
                mountedWaterSide_ = -side/length*amp*pulse;
                mountedWaterForward_ = -forward/length*amp*pulse;
                mountedWaterVertical_ = -waterEmissionVelocity_.z/length*amp*pulse + amp*0.15f*std::sin(float(now)*0.081f);
            }
            TouchResponseSource(cr::ResponseOrigin::Water, amp, 200);
        }
    }

    float mountedWaterSide_ = 0.0f, mountedWaterVertical_ = 0.0f, mountedWaterForward_ = 0.0f;

    void UpdateVehicle(CVehicle* v, float dt) {
        AuditHit(AUD_UpdateVehicle);
        // Clear the frame mix before any producer writes to it (including
        // RPM vibration and the gear pulse below).
        advancedVehicleSide_ = 0.0f;
        advancedVehicleVertical_ = 0.0f;
        advancedVehicleForward_ = 0.0f;
        if (!v) {
            ResetVehicleState();
            return;
        }

        const CVector vel = v->m_vecMoveSpeed;
        const float horizontalSpeed =
            std::sqrt(vel.x * vel.x + vel.y * vel.y);
        vehicleSpeed_ = horizontalSpeed;

        CMatrix& vm = v->GetMatrix();
        CVector right = vm.right;
        CVector forward = vm.up;

        const float rLen = std::sqrt(right.x * right.x + right.y * right.y);
        const float fLen = std::sqrt(forward.x * forward.x + forward.y * forward.y);
        if (rLen > 0.0001f) {
            right.x /= rLen;
            right.y /= rLen;
        }
        if (fLen > 0.0001f) {
            forward.x /= fLen;
            forward.y /= fLen;
        }

        const float heading = std::atan2(forward.x, forward.y);

        if (!vehicleInit_ || lastVehicle_ != v) {
            ResetVehicleState();
            vehicleInit_ = true;
            lastVehicle_ = v;
            vehicleSpeed_ = horizontalSpeed;
            prevVehicleVelocity_ = vel;
            prevVehicleHeading_ = heading;
            vehicleSpring_.Reset();
            vehicleSuspensionSpring_.Reset();
            vehicleForwardAccel_ = 0.0f;
            vehicleSideAccel_ = 0.0f;
            vehicleVerticalAccelNow_ = 0.0f;
            vehicleTargetSide_ = 0.0f;
            vehicleTargetVertical_ = 0.0f;
            vehicleTargetForward_ = 0.0f;
            vehicleVibrationSide_ = 0.0f;
            vehicleVibrationVertical_ = 0.0f;
            vehicleVibrationForward_ = 0.0f;
            vehicleHealthInit_ = true;
            prevVehicleHealth_ = v->m_fHealth;
            lastVehicleHealthLoss_ = 0.0f;
            vehicleSuspensionJolt_ = 0.0f;
            prevVehicleContactCount_ =
                static_cast<unsigned char>(std::min<int>(v->m_nNumEntitiesCollided, 6));
            for (unsigned i = 0; i < 6; ++i) {
                prevVehicleContacts_[i] =
                    i < prevVehicleContactCount_ ? v->m_apCollidedEntities[i] : nullptr;
            }
            prevVehicleCollisionTime_ = v->m_nLastCollisionTime;
            previousVehicleEngineOn_ = v->bEngineOn;
            previousVehicleChangingGear_ = v->bAudioChangingGear;
            previousVehicleTouchingWater_ =
                v->bTouchingWater || v->bSubmergedInWater;

            previousVehicleContactWheels_ =
                v->GetNumContactWheels();

            if (v->GetVehicleAppearance() == VEHICLE_APPEARANCE_AUTOMOBILE) {
                CAutomobile* car = reinterpret_cast<CAutomobile*>(v);
                for (unsigned wheel = 0; wheel < 4; ++wheel)
                    previousWheelDamage_[wheel] =
                        car->m_damageManager.m_anWheelsStatus[wheel];
                previousVehicleWheelsOnGround_ = car->m_nWheelsOnGround;
            }
            return;
        }

        const float safeDt = std::max(dt, 0.001f);
        CVector deltaVelocity;
        deltaVelocity.x = vel.x - prevVehicleVelocity_.x;
        deltaVelocity.y = vel.y - prevVehicleVelocity_.y;
        deltaVelocity.z = vel.z - prevVehicleVelocity_.z;

        CVector accel;
        accel.x = deltaVelocity.x / safeDt;
        accel.y = deltaVelocity.y / safeDt;
        accel.z = deltaVelocity.z / safeDt;

        const float localDeltaSide =
            deltaVelocity.x * right.x + deltaVelocity.y * right.y;
        const float localDeltaForward =
            deltaVelocity.x * forward.x + deltaVelocity.y * forward.y;
        const float localDeltaVertical = deltaVelocity.z;

        const float localSide =
            accel.x * right.x + accel.y * right.y;
        const float localForward =
            accel.x * forward.x + accel.y * forward.y;

        // Filter physics spikes before they become camera targets. True hard
        // impacts are handled separately below as impulses.
        vehicleSideAccel_ = Clamp(localSide, -5.0f, 5.0f);
        vehicleForwardAccel_ = Clamp(localForward, -6.0f, 6.0f);
        vehicleVerticalAccelNow_ = Clamp(accel.z, -7.0f, 7.0f);

        if (cfg_.jerkResponseEnabled) {
            const float js=(vehicleSideAccel_-previousVehicleSideAccelForJerk_)/safeDt;
            const float jf=(vehicleForwardAccel_-previousVehicleForwardAccelForJerk_)/safeDt;
            const float jv=(vehicleVerticalAccelNow_-previousVehicleVerticalAccelForJerk_)/safeDt;

            previousVehicleSideAccelForJerk_=vehicleSideAccel_;
            previousVehicleForwardAccelForJerk_=vehicleForwardAccel_;
            previousVehicleVerticalAccelForJerk_=vehicleVerticalAccelNow_;

            const float jm=std::sqrt(js*js+jf*jf+jv*jv);
            if (jm>cfg_.jerkDeadzone) {
                const float n=Clamp(
                    (jm-cfg_.jerkDeadzone)/
                    std::max(cfg_.jerkMax-cfg_.jerkDeadzone,0.1f),
                    0.0f,1.0f
                );
                const float d=std::max(jm,0.001f);

                cr::Vec3 tr{};
                tr.side=-js/d*cfg_.vehicleJerkStrength*n;
                tr.vertical=-jv/d*cfg_.vehicleJerkStrength*n*0.50f;
                tr.forward=-jf/d*cfg_.vehicleJerkStrength*n;

                cr::Vec3 rot{};
                rot.side=-js/d*cfg_.directionalRotationRollDeg*n*0.30f;
                rot.vertical=-jf/d*cfg_.directionalRotationPitchDeg*n*0.30f;

                SubmitTransientEvent(
                    cr::ResponseOrigin::Vehicle,
                    tr,rot,0.0f,0.72f,0.16f,
                    cr::TransientPriority::Normal,
                    0x4A524B56u
                );
            }
        }

        const float yawDelta = WrapAngle(heading - prevVehicleHeading_);
        const float yawRate = Clamp(yawDelta / safeDt, -5.0f, 5.0f);

        vehiclePersonalityResponse_ = 1.0f;
        vehiclePersonalityVibration_ = 1.0f;

        if (cfg_.vehiclePersonalityEnabled) {
            const float massScale =
                Clamp(std::sqrt(std::max(v->m_fMass, 1.0f) / 1400.0f), 0.72f, 1.45f);
            const float turnRatio =
                Clamp(
                    std::sqrt(
                        std::max(v->m_fTurnMass, 1.0f) /
                        std::max(v->m_fMass * 2.2f, 1.0f)
                    ),
                    0.70f, 1.35f
                );
            const float comScale =
                Clamp(1.0f + std::fabs(v->m_vecCentreOfMass.z) * 0.22f, 0.90f, 1.22f);

            vehiclePersonalityResponse_ =
                Clamp(
                    0.62f +
                    massScale * 0.24f +
                    turnRatio * 0.08f +
                    comScale * 0.10f,
                    0.82f, 1.28f
                );

            vehiclePersonalityVibration_ =
                Clamp(
                    1.18f - (massScale - 0.85f) * 0.23f,
                    0.78f, 1.18f
                );
        }

        const float profile = VehicleProfileScale(v);
        const float master =
            cfg_.vehicleStrength *
            cfg_.strength *
            cfg_.presetScale *
            profile *
            vehiclePersonalityResponse_ *
            (cfg_.vehicleResponseScale * cfg_.masterVehicleScale * cfg_.presetVehicle);

        const bool bikeAppearance=v->GetVehicleAppearance()==2;
        const float engineLoad=Clamp(std::fabs(vehicleForwardAccel_)*cfg_.engineLoadInfluence*0.08f,0.0f,1.0f);
        const float rpmTarget=Clamp(vehicleSpeed_*1.55f+engineLoad*0.45f,0.0f,1.0f);
        vehicleEngineRpmBlend_ += (rpmTarget-vehicleEngineRpmBlend_) *
            (1.0f-std::exp(-8.0f*Clamp(dt,0.0f,0.05f)));
        const float engineHz=cfg_.engineRpmMinHz+(cfg_.engineRpmMaxHz-cfg_.engineRpmMinHz)*vehicleEngineRpmBlend_;
        vehicleEnginePhase_ += dt*engineHz*2.0f*kPi;
        if (vehicleEnginePhase_>2.0f*kPi)
            vehicleEnginePhase_=std::fmod(vehicleEnginePhase_,2.0f*kPi);
        if (v->bEngineOn && (cfg_.engineRpmVibration * cfg_.presetAmbient)>0.00001f && !cfg_.comfortPreset) {
            const float typeScale=bikeAppearance?cfg_.bikeEngineScale:1.0f;
            const float rpmAmp=(cfg_.engineRpmVibration * cfg_.presetAmbient)*(0.25f+vehicleEngineRpmBlend_*0.75f)*
                (0.75f+engineLoad*0.25f)*typeScale*master;
            advancedVehicleVertical_ += std::sin(vehicleEnginePhase_)*rpmAmp;
            advancedVehicleSide_ += std::sin(vehicleEnginePhase_*0.51f+0.8f)*rpmAmp*(bikeAppearance?0.55f:0.25f);
            advancedVehicleForward_ += std::sin(vehicleEnginePhase_*0.74f+1.3f)*rpmAmp*0.35f;
        }
        vehicleGearPulse_ *= std::exp(-cfg_.gearShiftPulseRecovery*12.0f*Clamp(dt,0.0f,0.05f));
        advancedVehicleForward_ += vehicleGearPulse_;
        advancedVehicleVertical_ -= std::fabs(vehicleGearPulse_)*0.18f;

        // Per-vehicle suspension personality comes from GTA SA's live
        // tHandlingData. Values are normalized and clamped so vanilla and
        // custom handling.cfg setups can influence the camera safely.
        vehicleSuspensionHandlingStiffness_ = 1.0f;
        vehicleSuspensionHandlingDamping_ = 1.0f;
        vehicleSuspensionHandlingTravel_ = 1.0f;
        vehicleSuspensionHandlingMass_ = 1.0f;
        vehicleSuspensionHandlingBias_ = 0.5f;
        vehicleSuspensionHandlingAntiDive_ = 0.0f;
        vehicleSuspensionHandlingImpulseScale_ = 1.0f;
        vehicleSuspensionHandlingSpring_ = cfg_.vehicleSpring;
        vehicleSuspensionHandlingSpringDamping_ = cfg_.vehicleDamping;

        if (cfg_.vehicleSuspensionAdaptiveHandling && v->m_pHandlingData) {
            const tHandlingData* h = v->m_pHandlingData;

            const float rawMass = std::max(h->m_fMass, 1.0f);
            const float rawForce = std::max(h->m_fSuspensionForceLevel, 0.01f);
            const float rawDamping = std::max(h->m_fSuspensionDampingLevel, 0.001f);
            const float rawTravel = std::max(
                std::fabs(h->m_fSuspensionUpperLimit - h->m_fSuspensionLowerLimit),
                0.02f
            );
            const float rawBias = Clamp(h->m_fSuspensionBiasBetweenFrontAndRear, 0.0f, 1.0f);
            const float rawAntiDive = Clamp(h->m_fSuspensionAntiDiveMultiplier, 0.0f, 2.0f);

            // Ratios are centered around common SA road-car values, then
            // clamped to keep unusual/modded handling data camera-safe.
            vehicleSuspensionHandlingMass_ =
                Clamp(std::sqrt(rawMass / 1400.0f), 0.70f, 1.45f);
            vehicleSuspensionHandlingStiffness_ =
                Clamp(rawForce / 1.20f, 0.55f, 1.60f);
            vehicleSuspensionHandlingDamping_ =
                Clamp(rawDamping / 0.12f, 0.55f, 1.75f);
            vehicleSuspensionHandlingTravel_ =
                Clamp(rawTravel / 0.30f, 0.55f, 1.70f);
            vehicleSuspensionHandlingBias_ = rawBias;
            vehicleSuspensionHandlingAntiDive_ = rawAntiDive;

            const float speedBlend = Clamp(vehicleSpeed_ / 0.55f, 0.0f, 1.0f);
            const float highSpeedDamping =
                1.0f + Clamp(h->m_fSuspensionHighSpdComDamp * 0.015f, 0.0f, 0.65f) * speedBlend;

            const float adaptiveSpring =
                Clamp(
                    vehicleSuspensionHandlingStiffness_ /
                    std::sqrt(vehicleSuspensionHandlingTravel_ *
                              vehicleSuspensionHandlingMass_),
                    0.65f, 1.55f
                );

            const float adaptiveDamping =
                Clamp(
                    vehicleSuspensionHandlingDamping_ *
                    std::sqrt(vehicleSuspensionHandlingMass_) *
                    highSpeedDamping,
                    0.65f, 1.80f
                );

            const float adaptiveImpulse =
                Clamp(
                    (0.72f +
                     vehicleSuspensionHandlingStiffness_ * 0.22f +
                     vehicleSuspensionHandlingTravel_ * 0.30f +
                     vehicleSuspensionHandlingMass_ * 0.16f) /
                    (0.88f + vehicleSuspensionHandlingDamping_ * 0.14f),
                    0.70f, 1.50f
                );

            const float influence = cfg_.vehicleSuspensionHandlingInfluence;
            vehicleSuspensionHandlingSpring_ =
                cfg_.vehicleSpring *
                (1.0f + (adaptiveSpring - 1.0f) * influence);
            vehicleSuspensionHandlingSpringDamping_ =
                cfg_.vehicleDamping *
                (1.0f + (adaptiveDamping - 1.0f) * influence);
            vehicleSuspensionHandlingImpulseScale_ =
                1.0f + (adaptiveImpulse - 1.0f) * influence;
        }

        // Acceleration pulls the camera rearward; braking lets it surge
        // forward. Cornering leans it opposite lateral acceleration/turn.
        float forwardWeight =
            vehicleForwardAccel_ >= 0.0f
                ? cfg_.vehicleAccelForward
                : cfg_.vehicleBrakeForward;

        if (vehicleForwardAccel_ < 0.0f && cfg_.vehicleSuspensionAdaptiveHandling) {
            const float antiDiveReduction =
                Clamp(vehicleSuspensionHandlingAntiDive_ * 0.18f *
                      cfg_.vehicleSuspensionHandlingInfluence, 0.0f, 0.30f);
            forwardWeight *= (1.0f - antiDiveReduction);
        }

        // Camera-only inertial lag. These targets never write vehicle physics
        // or handling state; they model what the occupant/camera feels during
        // throttle, braking, cornering and heave.
        vehicleTargetForward_ =
            Clamp(-vehicleForwardAccel_ * 0.040f * forwardWeight * master,
                  -cfg_.vehicleMaxForward, cfg_.vehicleMaxForward);

        vehicleTargetSide_ =
            Clamp((-vehicleSideAccel_ * 0.034f -
                   yawRate * 0.024f) *
                      cfg_.vehicleCornerSide * master,
                  -cfg_.vehicleMaxSide, cfg_.vehicleMaxSide);

        vehicleTargetVertical_ =
            Clamp(-vehicleVerticalAccelNow_ * 0.026f *
                      cfg_.vehicleVerticalAccel * master,
                  -cfg_.vehicleMaxVertical, cfg_.vehicleMaxVertical);

        vehicleSpring_.Update(
            dt,
            cfg_.vehicleSpring,
            cfg_.vehicleDamping,
            vehicleTargetSide_,
            vehicleTargetVertical_,
            vehicleTargetForward_
        );

        vehicleSuspensionSpring_.Update(
            dt,
            vehicleSuspensionHandlingSpring_,
            vehicleSuspensionHandlingSpringDamping_
        );
        vehicleCollisionSpring_.Update(
            dt,
            cfg_.vehicleContactSpring,
            cfg_.vehicleContactDamping
        );
        const float kickDecay =
            std::exp(-std::max(cfg_.vehicleContactKickDecay, 1.0f) *
                     Clamp(dt, 0.0f, 0.050f));
        vehicleCollisionKickSide_ *= kickDecay;
        vehicleCollisionKickVertical_ *= kickDecay;
        vehicleCollisionKickForward_ *= kickDecay;

        vehicleCollisionSpring_.side.position =
            Clamp(vehicleCollisionSpring_.side.position, -0.18f, 0.18f);
        vehicleCollisionSpring_.vertical.position =
            Clamp(vehicleCollisionSpring_.vertical.position, -0.14f, 0.14f);
        vehicleCollisionSpring_.forward.position =
            Clamp(vehicleCollisionSpring_.forward.position, -0.20f, 0.20f);

        const float accelMag = std::sqrt(
            accel.x * accel.x +
            accel.y * accel.y +
            accel.z * accel.z
        );

        const float deltaVelMag = std::sqrt(
            deltaVelocity.x * deltaVelocity.x +
            deltaVelocity.y * deltaVelocity.y +
            deltaVelocity.z * deltaVelocity.z
        );

        const unsigned now = CTimer::m_snTimeInMilliseconds;
        const unsigned currentContactCount =
            static_cast<unsigned>(std::min<int>(v->m_nNumEntitiesCollided, 6));

        bool newEntityContact = false;
        CEntity* newContactEntity = nullptr;

        for (unsigned i = 0; i < currentContactCount; ++i) {
            CEntity* entity = v->m_apCollidedEntities[i];
            if (!entity)
                continue;

            bool existedLastFrame = false;
            for (unsigned j = 0; j < prevVehicleContactCount_; ++j) {
                if (prevVehicleContacts_[j] == entity) {
                    existedLastFrame = true;
                    break;
                }
            }

            if (!existedLastFrame) {
                newEntityContact = true;
                newContactEntity = entity;
                break;
            }
        }

        const bool freshCollisionTimestamp =
            v->m_nLastCollisionTime != 0 &&
            v->m_nLastCollisionTime != prevVehicleCollisionTime_ &&
            now - v->m_nLastCollisionTime <= 100u;

        const float physicalDamageIntensity =
            std::max(0.0f, v->m_fDamageIntensity);

        // m_vecLastCollisionImpactVelocity is not an impact-strength vector.
        // GTA fills it from the collision normal scaled by a distance factor,
        // so its magnitude is often near 1.0 even during ordinary contacts.
        // It must therefore NOT be used to scale camera severity.
        const bool contactMetadata =
            newEntityContact ||
            physicalDamageIntensity > 0.01f ||
            v->m_pDamageEntity != nullptr ||
            v->bHasHitWall;

        const float dvThreshold =
            std::max(cfg_.vehicleContactDeltaVelocityThreshold, 0.001f);
        const float strongDv =
            std::max(cfg_.vehicleContactStrongDeltaVelocity, dvThreshold * 1.5f);

        // A real collision produces a discontinuity in vehicle velocity.
        // Normal road/ground contact can update GTA's collision timestamp every
        // frame, but it does not normally produce a sufficiently large delta-V.
        const bool deltaVelocityImpact =
            deltaVelMag >= dvThreshold;

        // Delta-V by itself is not reliable collision evidence. Steering,
        // braking and rough-road suspension produced several false CONTACT-HIT
        // events in the runtime log with no entity, no damage entity, no wall
        // flag and zero damage intensity. Require GTA collision metadata or a
        // fresh collision timestamp before a velocity discontinuity can become
        // a camera impact.
        const bool collisionEvidence =
            contactMetadata ||
            freshCollisionTimestamp;

        const bool strongTimestampImpact =
            freshCollisionTimestamp &&
            deltaVelMag >= strongDv * 1.35f;

        // Hysteresis: trigger on the rising edge, or allow a distinctly stronger
        // second impact after the cooldown, but only with collision evidence.
        // The cooldown is intentionally longer than a single physics-contact
        // burst so one crash touching multiple entities does not become two or
        // three separate camera kicks.
        const bool risingImpact =
            deltaVelocityImpact &&
            prevVehicleDeltaVelMag_ < dvThreshold * 0.70f;

        const bool collisionEvent =
            collisionEvidence &&
            ((contactMetadata &&
              (risingImpact || deltaVelMag >= dvThreshold * 1.35f)) ||
             strongTimestampImpact);

        if (cfg_.vehicleContactResponseEnabled &&
            collisionEvent &&
            vehicleSpeed_ >= cfg_.vehicleContactMinSpeed &&
            now - lastVehicleContactResponseMs_ >= cfg_.vehicleContactCooldownMs) {

            const float dvSeverity =
                Clamp(
                    (deltaVelMag - dvThreshold) /
                    std::max(strongDv - dvThreshold, 0.001f),
                    0.0f, 2.0f
                );

            const float damageSeverity =
                Clamp(
                    physicalDamageIntensity /
                    std::max(cfg_.vehicleContactDamageReference, 1.0f),
                    0.0f, 1.5f
                );

            const float severity =
                Clamp(
                    std::max(dvSeverity, damageSeverity),
                    0.08f, 2.0f
                );

            // Direction comes from actual velocity discontinuity, which is what
            // the player's body/camera physically reacts to.
            const float dirDenom =
                std::max(
                    std::fabs(localDeltaSide) +
                    std::fabs(localDeltaForward) +
                    std::fabs(localDeltaVertical),
                    0.001f
                );

            const float sideDir =
                Clamp(-localDeltaSide / dirDenom, -1.0f, 1.0f);
            const float forwardDir =
                Clamp(-localDeltaForward / dirDenom, -1.0f, 1.0f);
            const float verticalDir =
                Clamp(-localDeltaVertical / dirDenom, -1.0f, 1.0f);

            const float materialRaw =
                CollisionMaterialMultiplier(v->m_nContactSurface);
            const float materialScale =
                1.0f +
                (materialRaw - 1.0f) *
                Clamp(cfg_.collisionMaterialInfluence, 0.0f, 1.5f);

            float sequenceScale = 1.0f;
            if (cfg_.advancedResponseEnabled &&
                cfg_.impactSequencingEnabled) {

                if (lastVehicleSequenceMs_ == 0 ||
                    now - lastVehicleSequenceMs_ > cfg_.impactSequenceResetMs) {
                    vehicleImpactSequence_ = 0;
                } else {
                    ++vehicleImpactSequence_;
                }

                if (vehicleImpactSequence_ == 1)
                    sequenceScale = cfg_.impactSecondaryScale;
                else if (vehicleImpactSequence_ >= 2)
                    sequenceScale = cfg_.impactTertiaryScale;

                lastVehicleSequenceMs_ = now;
            }

            const float contactImpulse =
                cfg_.vehicleContactStrength *
                (0.45f + severity * 1.10f) *
                materialScale *
                sequenceScale *
                master;

            // Dedicated transient spring: collision response cannot be hidden by
            // sustained cornering/acceleration saturating the normal vehicle spring.
            vehicleCollisionSpring_.side.AddImpulse(
                sideDir *
                contactImpulse *
                cfg_.vehicleCrashSideMultiplier
            );

            const float longitudinalMultiplier =
                forwardDir >= 0.0f
                    ? cfg_.vehicleCrashFrontMultiplier
                    : cfg_.vehicleCrashRearMultiplier;

            vehicleCollisionSpring_.forward.AddImpulse(
                forwardDir *
                contactImpulse *
                longitudinalMultiplier
            );

            vehicleCollisionSpring_.vertical.AddImpulse(
                (verticalDir - 0.10f) *
                contactImpulse *
                cfg_.vehicleCrashVerticalMultiplier
            );

            // Direct kick is deliberately independent from the normal vehicle
            // spring. This makes the first 50-150 ms of a real collision visible
            // even if acceleration/cornering offsets are already near their clamp.
            const float kickMagnitude =
                Clamp(
                    cfg_.vehicleContactDirectKick *
                    cfg_.vehicleContactStrength *
                    (0.55f + severity * 0.55f),
                    0.020f, 0.180f
                );

            vehicleCollisionKickSide_ +=
                sideDir * kickMagnitude * 0.85f;
            vehicleCollisionKickForward_ +=
                forwardDir * kickMagnitude;
            vehicleCollisionKickVertical_ +=
                (verticalDir - 0.10f) * kickMagnitude * 0.70f;

            vehicleCollisionKickSide_ =
                Clamp(vehicleCollisionKickSide_, -0.16f, 0.16f);
            vehicleCollisionKickVertical_ =
                Clamp(vehicleCollisionKickVertical_, -0.14f, 0.14f);
            vehicleCollisionKickForward_ =
                Clamp(vehicleCollisionKickForward_, -0.18f, 0.18f);

            // Immediate displacement gives the hit a readable first-frame punch.
            // Unlike the previous implementation this uses its own spring, so it
            // is not clipped by normal cornering weight transfer.
            const float immediate =
                Clamp(
                    0.018f + severity * 0.032f,
                    0.018f, 0.080f
                ) * Clamp(cfg_.vehicleContactStrength, 0.1f, 3.0f);

            vehicleCollisionSpring_.side.position +=
                sideDir * immediate * 0.80f;
            vehicleCollisionSpring_.forward.position +=
                forwardDir * immediate;
            vehicleCollisionSpring_.vertical.position +=
                (verticalDir - 0.10f) * immediate * 0.65f;

            lastVehicleContactResponseMs_ = now;
            lastVehicleContactIntensity_ = physicalDamageIntensity;
            lastVehicleContactImpulse_ = contactImpulse;
            ++vehicleContactImpacts_;
            AddFovKick(cfg_.fovVehicleImpactKick *
                Clamp(severity, 0.0f, 2.0f));

            // Existing damage-crash reinforcement can now use the same real
            // collision window.
            lastVehicleImpactMs_ = now;

            if (cfg_.debug) {
                log_.Write(
                    "VEHICLE CONTACT-HIT entity=%p new=%d count=%u wall=%d damageEntity=%p "
                    "damageIntensity=%.3f dv=%.4f prevDv=%.4f threshold=%.4f "
                    "dir=(%.2f %.2f %.2f) severity=%.2f impulse=%.3f kick=(%.3f %.3f %.3f)",
                    newContactEntity,
                    newEntityContact ? 1 : 0,
                    currentContactCount,
                    v->bHasHitWall ? 1 : 0,
                    v->m_pDamageEntity,
                    physicalDamageIntensity,
                    deltaVelMag,
                    prevVehicleDeltaVelMag_,
                    dvThreshold,
                    sideDir,
                    forwardDir,
                    verticalDir,
                    severity,
                    contactImpulse,
                    vehicleCollisionKickSide_,
                    vehicleCollisionKickVertical_,
                    vehicleCollisionKickForward_
                );
            }
        }

        prevVehicleContactCount_ =
            static_cast<unsigned char>(currentContactCount);
        for (unsigned i = 0; i < 6; ++i) {
            prevVehicleContacts_[i] =
                i < currentContactCount ? v->m_apCollidedEntities[i] : nullptr;
        }
        prevVehicleCollisionTime_ = v->m_nLastCollisionTime;
        prevVehicleDeltaVelMag_ = deltaVelMag;

        if (accelMag >= cfg_.vehicleImpactThreshold &&
            now - lastVehicleImpactMs_ > 140) {

            const float severity =
                Clamp((accelMag - cfg_.vehicleImpactThreshold) /
                      std::max(cfg_.vehicleImpactThreshold, 0.1f),
                      0.0f, 2.0f);

            const float impulse =
                cfg_.vehicleImpactImpulse * (0.45f + severity) * master;

            vehicleCollisionSpring_.forward.AddImpulse(
                Clamp(-vehicleForwardAccel_ * 0.030f, -1.0f, 1.0f) * impulse
            );
            vehicleCollisionSpring_.side.AddImpulse(
                Clamp(-vehicleSideAccel_ * 0.025f, -1.0f, 1.0f) * impulse
            );
            vehicleCollisionSpring_.vertical.AddImpulse(
                Clamp(-vehicleVerticalAccelNow_ * 0.025f, -1.0f, 1.0f) * impulse
            );

            lastVehicleImpactMs_ = now;
            ++vehicleImpacts_;
            AddFovKick(cfg_.fovVehicleImpactKick *
                Clamp(severity, 0.0f, 2.0f));

            if (cfg_.debug)
                log_.Write(
                    "VEHICLE impact appearance=%d accel=%.2f impulse=%.3f speed=%.3f",
                    v->GetVehicleAppearance(), accelMag, impulse, vehicleSpeed_
                );
        }

        // Reinforce a real collision when GTA actually subtracts vehicle
        // health. Health loss alone is intentionally insufficient: the
        // velocity-change/physics window must also indicate a crash, so weapon
        // or fire damage does not shake the camera like a wall impact.
        const float currentHealth = v->m_fHealth;
        if (!vehicleHealthInit_) {
            vehicleHealthInit_ = true;
            prevVehicleHealth_ = currentHealth;
        }

        const float healthLoss = std::max(0.0f, prevVehicleHealth_ - currentHealth);
        lastVehicleHealthLoss_ = healthLoss;

        const bool recentPhysicsImpact =
            (now - lastVehicleImpactMs_) <= cfg_.vehicleCrashPhysicsWindowMs;
        // A per-frame velocity delta catches damaging crashes that land just
        // below the acceleration threshold at low/variable frame rates.
        const bool collisionMotion =
            recentPhysicsImpact ||
            deltaVelMag >= 0.055f ||
            accelMag >= cfg_.vehicleImpactThreshold * 0.70f;

        if (cfg_.vehicleCrashDamageEnabled &&
            healthLoss >= cfg_.vehicleCrashDamageThreshold &&
            collisionMotion &&
            now - lastVehicleDamageCrashMs_ > 100) {

            const float damageSeverity =
                Clamp(healthLoss /
                      std::max(cfg_.vehicleCrashDamageReference, 1.0f),
                      0.0f, 2.0f);

            const bool catastrophic =
                healthLoss >= cfg_.vehicleCrashCatastrophicThreshold;
            const float catastropheScale =
                catastrophic ? cfg_.vehicleCrashCatastrophicMultiplier : 1.0f;

            // Convert the collision velocity delta to a stable vehicle-local
            // direction. A frontal collision normally causes negative local
            // forward delta (rapid deceleration), producing a positive
            // forward camera surge.
            const float dirDenom =
                std::max(std::fabs(localDeltaSide) +
                         std::fabs(localDeltaForward) +
                         std::fabs(localDeltaVertical), 0.001f);

            const float sideDir =
                Clamp(-localDeltaSide / dirDenom, -1.0f, 1.0f);
            const float forwardDir =
                Clamp(-localDeltaForward / dirDenom, -1.0f, 1.0f);
            const float verticalDir =
                Clamp(-localDeltaVertical / dirDenom, -1.0f, 1.0f);

            // Front vs rear weighting is based on collision response:
            // positive forwardDir = strong deceleration/front hit.
            const float longitudinalMultiplier =
                forwardDir >= 0.0f
                    ? cfg_.vehicleCrashFrontMultiplier
                    : cfg_.vehicleCrashRearMultiplier;

            const float damageImpulse =
                cfg_.vehicleCrashDamageStrength *
                (0.35f + damageSeverity) *
                catastropheScale *
                master;

            vehicleCollisionSpring_.side.AddImpulse(
                sideDir * damageImpulse * cfg_.vehicleCrashSideMultiplier
            );
            vehicleCollisionSpring_.forward.AddImpulse(
                forwardDir * damageImpulse * longitudinalMultiplier
            );

            // Give severe crashes a short downward chassis/body compression
            // even when the collision itself is nearly horizontal.
            const float verticalKick =
                verticalDir * damageImpulse * cfg_.vehicleCrashVerticalMultiplier -
                damageImpulse * (0.08f + 0.10f * Clamp(damageSeverity, 0.0f, 1.0f));
            vehicleCollisionSpring_.vertical.AddImpulse(verticalKick);

            {
                cr::Vec3 tr{};
                cr::Vec3 rot{};
                rot.side=-sideDir*cfg_.directionalRotationRollDeg*Clamp(damageSeverity,0.0f,1.5f);
                rot.vertical=-forwardDir*cfg_.directionalRotationPitchDeg*Clamp(damageSeverity,0.0f,1.5f);
                rot.forward=sideDir*cfg_.directionalRotationYawDeg*Clamp(damageSeverity,0.0f,1.5f)*0.35f;
                SubmitTransientEvent(
                    cr::ResponseOrigin::Vehicle,
                    tr,rot,0.0f,1.15f,0.72f,
                    catastrophic?cr::TransientPriority::Critical:cr::TransientPriority::Normal,
                    0x56435253u
                );
            }

            lastVehicleDamageCrashMs_ = now;
            ++vehicleDamageCrashes_;

            if (cfg_.debug) {
                log_.Write(
                    "VEHICLE DAMAGE-CRASH healthLoss=%.2f health=%.2f severity=%.2f catastrophic=%d "
                    "dv=(side %.3f forward %.3f z %.3f) impulse=%.3f",
                    healthLoss, currentHealth, damageSeverity, catastrophic ? 1 : 0,
                    localDeltaSide, localDeltaForward, localDeltaVertical, damageImpulse
                );
            }
        }

        // Automobile suspension reinforcement. Per-wheel compression is
        // weighted by the handling front/rear suspension bias, then translated
        // through a dedicated spring whose stiffness/damping/impulse character
        // is derived from this vehicle's live handling data.
        vehicleSuspensionJolt_ = 0.0f;
        if (cfg_.vehicleSuspensionEnabled &&
            v->GetVehicleAppearance() == VEHICLE_APPEARANCE_AUTOMOBILE) {

            CAutomobile* car = reinterpret_cast<CAutomobile*>(v);
            float weightedCompression = 0.0f;
            float weightTotal = 0.0f;
            float strongestCompression = 0.0f;

            const float frontWeight =
                Clamp(vehicleSuspensionHandlingBias_ * 2.0f, 0.50f, 1.50f);
            const float rearWeight =
                Clamp((1.0f - vehicleSuspensionHandlingBias_) * 2.0f, 0.50f, 1.50f);

            for (int wheel = 0; wheel < 4; ++wheel) {
                const float current =
                    Clamp(car->m_fWheelsSuspensionCompression[wheel], 0.0f, 1.5f);
                const float previous =
                    Clamp(car->m_fWheelsSuspensionCompressionPrev[wheel], 0.0f, 1.5f);
                const float d = previous - current; // positive = rapid compression

                // GTA SA eWheels order is FL, RL, FR, RR.
                const bool frontWheel = (wheel == 0 || wheel == 2);
                const float axleWeight = frontWheel ? frontWeight : rearWeight;

                weightedCompression += d * axleWeight;
                weightTotal += axleWeight;
                strongestCompression =
                    std::max(strongestCompression, d * axleWeight);
            }

            const float compressionDelta =
                weightTotal > 0.001f ? weightedCompression / weightTotal : 0.0f;

            vehicleSuspensionJolt_ =
                std::max(compressionDelta, strongestCompression * 0.50f);

            // Stiff/short-travel setups generally move less physically, so
            // lower the trigger slightly; long/soft suspension naturally
            // produces larger compression deltas and can keep the base gate.
            float adaptiveThreshold = cfg_.vehicleSuspensionThreshold;
            if (cfg_.vehicleSuspensionAdaptiveHandling) {
                const float thresholdScale =
                    Clamp(
                        1.05f -
                        (vehicleSuspensionHandlingStiffness_ - 1.0f) * 0.16f +
                        (vehicleSuspensionHandlingTravel_ - 1.0f) * 0.08f,
                        0.72f, 1.28f
                    );
                adaptiveThreshold *=
                    1.0f + (thresholdScale - 1.0f) *
                    cfg_.vehicleSuspensionHandlingInfluence;
            }

            if (vehicleSuspensionJolt_ >= adaptiveThreshold &&
                now - lastVehicleImpactMs_ > 85) {

                const float suspensionSeverity =
                    Clamp((vehicleSuspensionJolt_ - adaptiveThreshold) /
                          std::max(0.10f, 1.0f - adaptiveThreshold),
                          0.0f, 1.5f);

                const float suspensionImpulse =
                    cfg_.vehicleSuspensionStrength *
                    (0.30f + suspensionSeverity) *
                    vehicleSuspensionHandlingImpulseScale_ *
                    master;

                vehicleSuspensionSpring_.vertical.AddImpulse(-suspensionImpulse);

                // A small fore/aft component helps front/rear curb strikes
                // read as chassis movement without requiring camera rotation.
                vehicleSuspensionSpring_.forward.AddImpulse(
                    Clamp(-localDeltaForward * 0.40f, -0.18f, 0.18f) *
                    suspensionImpulse
                );

                ++vehicleSuspensionImpacts_;

                if (cfg_.debug) {
                    log_.Write(
                        "VEHICLE SUSPENSION jolt=%.3f severity=%.2f impulse=%.3f "
                        "handling(stiff=%.2f damp=%.2f travel=%.2f mass=%.2f bias=%.2f antiDive=%.2f spring=%.1f sdamp=%.1f)",
                        vehicleSuspensionJolt_, suspensionSeverity, suspensionImpulse,
                        vehicleSuspensionHandlingStiffness_,
                        vehicleSuspensionHandlingDamping_,
                        vehicleSuspensionHandlingTravel_,
                        vehicleSuspensionHandlingMass_,
                        vehicleSuspensionHandlingBias_,
                        vehicleSuspensionHandlingAntiDive_,
                        vehicleSuspensionHandlingSpring_,
                        vehicleSuspensionHandlingSpringDamping_
                    );
                }
            }
        }

        prevVehicleHealth_ = currentHealth;

        vehicleSpring_.side.position =
            Clamp(vehicleSpring_.side.position, -cfg_.vehicleMaxSide, cfg_.vehicleMaxSide);
        vehicleSpring_.vertical.position =
            Clamp(vehicleSpring_.vertical.position, -cfg_.vehicleMaxVertical, cfg_.vehicleMaxVertical);
        vehicleSpring_.forward.position =
            Clamp(vehicleSpring_.forward.position, -cfg_.vehicleMaxForward, cfg_.vehicleMaxForward);

        vehicleSuspensionSpring_.side.position =
            Clamp(vehicleSuspensionSpring_.side.position, -cfg_.vehicleMaxSide * 0.45f, cfg_.vehicleMaxSide * 0.45f);
        vehicleSuspensionSpring_.vertical.position =
            Clamp(vehicleSuspensionSpring_.vertical.position, -cfg_.vehicleMaxVertical * 0.70f, cfg_.vehicleMaxVertical * 0.70f);
        vehicleSuspensionSpring_.forward.position =
            Clamp(vehicleSuspensionSpring_.forward.position, -cfg_.vehicleMaxForward * 0.50f, cfg_.vehicleMaxForward * 0.50f);

        if (cfg_.vehicleMechanicalEnabled) {
            if (!previousVehicleEngineOn_ && v->bEngineOn) {
                vehicleCollisionSpring_.vertical.AddImpulse(
                    -cfg_.engineStartStrength * 0.55f * master
                );
                vehicleCollisionSpring_.forward.AddImpulse(
                    -cfg_.engineStartStrength * 0.72f * master
                );
                vehicleCollisionSpring_.side.AddImpulse(
                    RandomSigned() * cfg_.engineStartStrength * 0.16f * master
                );

                if (cfg_.debug)
                    log_.Write("VEHICLE ENGINE START impulse=%.3f", cfg_.engineStartStrength * master);
            } else if (previousVehicleEngineOn_ && !v->bEngineOn) {
                vehicleCollisionSpring_.vertical.AddImpulse(
                    cfg_.engineStopStrength * 0.25f * master
                );
                vehicleCollisionSpring_.forward.AddImpulse(
                    cfg_.engineStopStrength * 0.42f * master
                );

                if (cfg_.debug)
                    log_.Write("VEHICLE ENGINE STOP impulse=%.3f", cfg_.engineStopStrength * master);
            }

            const bool changingGear = v->bAudioChangingGear;
            if (!previousVehicleChangingGear_ && changingGear && vehicleSpeed_ > 0.05f) {
                const bool shiftBike=v->GetVehicleAppearance()==2;
                const float shiftScale=Clamp(0.60f+vehicleSpeed_*0.70f+engineLoad*0.25f,0.60f,1.40f)*
                    (shiftBike?cfg_.gearShiftBikeScale:cfg_.gearShiftCarScale);
                const float shiftImpulse=cfg_.gearShiftStrength*shiftScale*master;
                vehicleCollisionSpring_.forward.AddImpulse(-shiftImpulse);
                vehicleCollisionSpring_.vertical.AddImpulse(-shiftImpulse*(shiftBike?0.25f:0.18f));
                vehicleGearPulse_=Clamp(vehicleGearPulse_-0.018f*shiftScale*(shiftBike?1.35f:1.0f),-0.08f,0.04f);
                TouchResponseSource(cr::ResponseOrigin::Vehicle,shiftImpulse,500);
                if (cfg_.debug)
                    log_.Write("VEHICLE GEAR SHIFT speed=%.3f load=%.2f bike=%d rpmBlend=%.2f impulse=%.3f",
                        vehicleSpeed_,engineLoad,shiftBike?1:0,vehicleEngineRpmBlend_,shiftImpulse);
            }
            previousVehicleChangingGear_ = changingGear;

            if (v->GetVehicleAppearance() == VEHICLE_APPEARANCE_AUTOMOBILE) {
                CAutomobile* car = reinterpret_cast<CAutomobile*>(v);

                for (unsigned wheel = 0; wheel < 4; ++wheel) {
                    const unsigned char status =
                        car->m_damageManager.m_anWheelsStatus[wheel];

                    if (previousWheelDamage_[wheel] == 0 && status != 0) {
                        const bool left =
                            wheel == WHEEL_FRONT_LEFT ||
                            wheel == WHEEL_REAR_LEFT;
                        const bool front =
                            wheel == WHEEL_FRONT_LEFT ||
                            wheel == WHEEL_FRONT_RIGHT;

                        const float sideDir = left ? 1.0f : -1.0f;
                        const float frontDir = front ? -0.45f : 0.28f;
                        const float impulse =
                            cfg_.tyreBlowoutStrength *
                            Clamp(0.70f + vehicleSpeed_ * 0.75f, 0.70f, 1.35f) *
                            master;

                        vehicleCollisionSpring_.side.AddImpulse(
                            sideDir * impulse
                        );
                        vehicleCollisionSpring_.vertical.AddImpulse(
                            -impulse * 0.42f
                        );
                        vehicleCollisionSpring_.forward.AddImpulse(
                            frontDir * impulse
                        );

                        if (cfg_.debug)
                            log_.Write(
                                "VEHICLE TYRE DAMAGE wheel=%u status=%u impulse=%.3f",
                                wheel,
                                unsigned(status),
                                impulse
                            );
                    }

                    previousWheelDamage_[wheel] = status;
                }

                const unsigned char wheelsOnGround =
                    car->m_nWheelsOnGround;

                if (previousVehicleWheelsOnGround_ == 0 &&
                    wheelsOnGround > 0 &&
                    prevVehicleVelocity_.z < -0.025f) {

                    unsigned frontContacts = 0;
                    unsigned rearContacts = 0;
                    unsigned leftContacts = 0;
                    unsigned rightContacts = 0;

                    for (unsigned wheel = 0; wheel < 4; ++wheel) {
                        const bool grounded =
                            car->m_fWheelsSuspensionCompression[wheel] < 0.985f;

                        if (!grounded)
                            continue;

                        if (wheel == WHEEL_FRONT_LEFT ||
                            wheel == WHEEL_FRONT_RIGHT)
                            ++frontContacts;
                        else
                            ++rearContacts;

                        if (wheel == WHEEL_FRONT_LEFT ||
                            wheel == WHEEL_REAR_LEFT)
                            ++leftContacts;
                        else
                            ++rightContacts;
                    }

                    const float landingSeverity =
                        Clamp((-prevVehicleVelocity_.z - 0.025f) / 0.18f, 0.20f, 1.65f);
                    const float impulse =
                        cfg_.axleLandingStrength *
                        landingSeverity *
                        master;

                    const float axleBias =
                        float(int(frontContacts) - int(rearContacts)) * 0.28f;
                    const float sideBias =
                        float(int(leftContacts) - int(rightContacts)) * 0.22f;

                    vehicleSuspensionSpring_.vertical.AddImpulse(-impulse);
                    vehicleSuspensionSpring_.forward.AddImpulse(
                        -axleBias * impulse
                    );
                    vehicleSuspensionSpring_.side.AddImpulse(
                        sideBias * impulse
                    );

                    if (cfg_.debug)
                        log_.Write(
                            "VEHICLE AXLE LAND front=%u rear=%u left=%u right=%u vz=%.3f impulse=%.3f",
                            frontContacts,
                            rearContacts,
                            leftContacts,
                            rightContacts,
                            prevVehicleVelocity_.z,
                            impulse
                        );
                }

                previousVehicleWheelsOnGround_ = wheelsOnGround;
            }

            previousVehicleEngineOn_ = v->bEngineOn;
        }

        if (cfg_.advancedResponseEnabled) {
            const float t =
                static_cast<float>(CTimer::m_snTimeInMilliseconds) * 0.001f;
            const int appearance = v->GetVehicleAppearance();
            const bool bike =
                v->m_nVehicleSubClass == VEHICLE_BIKE;
            const bool bmx =
                v->m_nVehicleSubClass == VEHICLE_BMX;
            const bool train =
                v->m_nVehicleSubClass == VEHICLE_TRAIN;
            const bool boat =
                appearance == VEHICLE_APPEARANCE_BOAT;
            const bool aircraft =
                appearance == VEHICLE_APPEARANCE_HELI ||
                appearance == VEHICLE_APPEARANCE_PLANE;

            if (v->bEngineOn) {
                const float engineLoad =
                    Clamp(
                        0.35f +
                        std::fabs(v->m_fGasPedal) * 0.65f +
                        vehicleSpeed_ * 0.55f,
                        0.35f,
                        1.35f
                    );
                const float amp =
                    (cfg_.engineVibrationStrength * cfg_.presetAmbient) *
                    engineLoad *
                    cfg_.highFrequencyScale *
                    master;

                if (cfg_.proceduralNoiseEnabled) {
                    const float texture =
                        vehicleNoise_.Update(
                            dt,
                            9.5f + vehicleSpeed_ * 5.0f,
                            15.0f
                        );
                    advancedVehicleVertical_ += texture * amp;
                    advancedVehicleSide_ +=
                        vehicleNoise_.Update(
                            dt,
                            7.3f + vehicleSpeed_ * 3.0f,
                            12.0f
                        ) * amp * 0.38f;
                } else {
                    advancedVehicleVertical_ +=
                        std::sin(t * (9.5f + vehicleSpeed_ * 5.0f)) * amp;
                    advancedVehicleSide_ +=
                        std::sin(t * (7.3f + vehicleSpeed_ * 3.0f) + 0.8f) *
                        amp * 0.38f;
                }
            }

            if (bike || bmx) {
                advancedVehicleSide_ +=
                    Clamp(vm.right.z, -0.8f, 0.8f) *
                    cfg_.bikeLeanStrength *
                    master;

                advancedVehicleForward_ +=
                    Clamp(forward.z, -0.75f, 0.75f) *
                    cfg_.bikePitchStrength *
                    master;

                if (bmx) {
                    // Phase-lock the camera to GTA's actual BMX crank instead
                    // of synthesizing cadence from speed and elapsed time.
                    CBmx* bicycle =
                        reinterpret_cast<CBmx*>(v);

                    const float leftAngle =
                        bicycle->m_fPedalAngleL;
                    const float rightAngle =
                        bicycle->m_fPedalAngleR;

                    // Left and right pedals are nominally opposite. Combining
                    // left with the inverted right vector gives a stable crank
                    // phase while averaging out small per-pedal differences.
                    const float phaseX =
                        std::cos(leftAngle) -
                        std::cos(rightAngle);
                    const float phaseY =
                        std::sin(leftAngle) -
                        std::sin(rightAngle);

                    float pedalPhase =
                        std::atan2(phaseY, phaseX);

                    if (!std::isfinite(pedalPhase))
                        pedalPhase = leftAngle;

                    if (!bmxPedalPhaseInit_) {
                        previousBmxPedalAngle_ = pedalPhase;
                        bmxPedalRateSmoothed_ = 0.0f;
                        bmxPedalPhaseInit_ = true;
                    } else {
                        const float pedalDelta =
                            WrapAngle(
                                pedalPhase -
                                previousBmxPedalAngle_
                            );

                        const float pedalRate =
                            std::fabs(pedalDelta) /
                            std::max(dt, 0.001f);

                        const float rateAlpha =
                            1.0f -
                            std::exp(
                                -18.0f *
                                Clamp(dt, 0.0f, 0.050f)
                            );

                        bmxPedalRateSmoothed_ +=
                            (pedalRate - bmxPedalRateSmoothed_) *
                            rateAlpha;

                        previousBmxPedalAngle_ = pedalPhase;

                        // No crank motion means no pedal camera motion, even
                        // if the BMX is still moving while CJ coasts.
                        const float cadence =
                            Clamp(
                                bmxPedalRateSmoothed_ / 10.0f,
                                0.0f,
                                1.35f
                            );

                        if (cadence > 0.015f) {
                            const float amp =
                                cfg_.bicyclePedalStrength *
                                cadence *
                                cfg_.highFrequencyScale *
                                master;

                            // Two load pulses per crank revolution: one for
                            // each left/right downstroke.
                            const float downstroke =
                                -std::cos(
                                    pedalPhase * 2.0f
                                );

                            // Lateral weight shift follows the true crank once
                            // per revolution.
                            const float bodyShift =
                                std::sin(pedalPhase);

                            advancedVehicleVertical_ +=
                                downstroke * amp;

                            advancedVehicleSide_ +=
                                bodyShift *
                                amp *
                                0.42f;

                            advancedVehicleForward_ +=
                                -std::fabs(downstroke) *
                                amp *
                                0.18f;
                        }
                    }
                } else {
                    bmxPedalPhaseInit_ = false;
                    bmxPedalRateSmoothed_ = 0.0f;
                }
            }

            if (boat && cfg_.boatWavePhysicalEnabled) {
                const bool touchingWater =
                    v->bTouchingWater ||
                    v->bSubmergedInWater ||
                    (v->m_nVehicleSubClass == VEHICLE_BOAT &&
                     reinterpret_cast<CBoat*>(v)->m_nBoatFlags.bOnWater);

                // GTA's boat physics already contains the real water-wave
                // solution. Read hull heave/roll/pitch from that physical
                // motion instead of synthesizing an unrelated camera sine.
                const float boatRoll =
                    Clamp(vm.right.z, -0.95f, 0.95f);

                const float boatPitch =
                    Clamp(forward.z, -0.95f, 0.95f);

                if (!boatWaveInit_) {
                    previousBoatRoll_ = boatRoll;
                    previousBoatPitch_ = boatPitch;
                    boatWaveInit_ = true;
                }

                const float safeBoatDt =
                    std::max(dt, 0.001f);

                const float rollRate =
                    (boatRoll - previousBoatRoll_) /
                    safeBoatDt;

                const float pitchRate =
                    (boatPitch - previousBoatPitch_) /
                    safeBoatDt;

                previousBoatRoll_ = boatRoll;
                previousBoatPitch_ = boatPitch;

                const float waterScale =
                    touchingWater ? 1.0f : 0.0f;

                const float speedScale =
                    1.0f +
                    Clamp(
                        vehicleSpeed_ *
                        cfg_.boatWaveSpeedInfluence,
                        0.0f,
                        0.85f
                    );

                const float waveMaster =
                    // BoatWaveStrength historically defaults to 0.085. Treat
                    // that as the nominal gain: the per-axis scales already
                    // specify small camera displacements.
                    (cfg_.boatWaveStrength / 0.085f) *
                    cfg_.presetAmbient *
                    master *
                    waterScale;

                float targetVertical =
                    -vehicleVerticalAccelNow_ *
                    cfg_.boatWaveHeaveScale *
                    waveMaster *
                    speedScale;

                float targetSide =
                    (-rollRate *
                     cfg_.boatWaveRollScale -
                     boatRoll *
                     cfg_.boatWaveOrientationHold) *
                    waveMaster *
                    speedScale;

                float targetForward =
                    (-pitchRate *
                     cfg_.boatWavePitchScale -
                     boatPitch *
                     cfg_.boatWaveOrientationHold) *
                    waveMaster *
                    speedScale;

                targetSide =
                    Clamp(
                        targetSide,
                        -cfg_.boatWaveMaxSide,
                        cfg_.boatWaveMaxSide
                    );

                targetVertical =
                    Clamp(
                        targetVertical,
                        -cfg_.boatWaveMaxVertical,
                        cfg_.boatWaveMaxVertical
                    );

                targetForward =
                    Clamp(
                        targetForward,
                        -cfg_.boatWaveMaxForward,
                        cfg_.boatWaveMaxForward
                    );

                const float waveAlpha =
                    1.0f -
                    std::exp(
                        -cfg_.boatWaveSmoothing *
                        Clamp(dt, 0.0f, 0.05f)
                    );

                boatWaveSide_ +=
                    (targetSide - boatWaveSide_) *
                    waveAlpha;

                boatWaveVertical_ +=
                    (targetVertical - boatWaveVertical_) *
                    waveAlpha;

                boatWaveForward_ +=
                    (targetForward - boatWaveForward_) *
                    waveAlpha;

                advancedVehicleSide_ +=
                    boatWaveSide_;

                advancedVehicleVertical_ +=
                    boatWaveVertical_;

                advancedVehicleForward_ +=
                    boatWaveForward_;

                // Optional orientation response uses actual hull attitude and
                // rate, producing slow wave-following pitch/roll rather than
                // noisy camera rotation.
                boatWavePitchDeg_ =
                    Clamp(
                        (-boatPitch * 0.58f -
                         pitchRate * 0.018f) *
                        cfg_.boatWaveRotationPitchDeg *
                        waveMaster,
                        -cfg_.boatWaveRotationPitchDeg,
                        cfg_.boatWaveRotationPitchDeg
                    );

                boatWaveRollDeg_ =
                    Clamp(
                        (-boatRoll * 0.62f -
                         rollRate * 0.018f) *
                        cfg_.boatWaveRotationRollDeg *
                        waveMaster,
                        -cfg_.boatWaveRotationRollDeg,
                        cfg_.boatWaveRotationRollDeg
                    );

                // Re-entry into water arrests downward motion: hull
                // acceleration is upward, while the camera kick is downward.
                const float slamAccel =
                    std::max(
                        0.0f,
                        vehicleVerticalAccelNow_
                    );

                const unsigned now =
                    CTimer::m_snTimeInMilliseconds;

                if (touchingWater &&
                    slamAccel >= cfg_.boatWaveSlamThreshold &&
                    (lastBoatWaveSlamMs_ == 0 ||
                     now - lastBoatWaveSlamMs_ >=
                        cfg_.boatWaveSlamCooldownMs)) {
                    lastBoatWaveSlamMs_ = now;

                    const float slamSeverity =
                        Clamp(
                            (slamAccel -
                             cfg_.boatWaveSlamThreshold) /
                            std::max(
                                cfg_.boatWaveSlamThreshold,
                                0.25f
                            ),
                            0.0f,
                            1.35f
                        );

                    const float slamImpulse =
                        cfg_.boatWaveSlamStrength *
                        (0.35f +
                         slamSeverity * 0.65f) *
                        master;

                    vehicleCollisionSpring_.vertical.AddImpulse(
                        -slamImpulse
                    );

                    vehicleCollisionSpring_.forward.AddImpulse(
                        -slamImpulse *
                        Clamp(
                            0.16f +
                            std::fabs(boatPitch) * 0.55f,
                            0.16f,
                            0.55f
                        )
                    );

                    vehicleCollisionSpring_.side.AddImpulse(
                        -boatRoll *
                        slamImpulse *
                        0.32f
                    );

                    TouchResponseSource(
                        cr::ResponseOrigin::Vehicle,
                        slamImpulse,
                        650
                    );

                    if (cfg_.debug) {
                        log_.Write(
                            "BOAT WAVE SLAM accel=%.3f severity=%.3f "
                            "pitch=%.3f roll=%.3f impulse=%.4f",
                            slamAccel,
                            slamSeverity,
                            boatPitch,
                            boatRoll,
                            slamImpulse
                        );
                    }
                }

                const float waveEvidence =
                    std::fabs(boatWaveSide_) +
                    std::fabs(boatWaveVertical_) +
                    std::fabs(boatWaveForward_);

                if (waveEvidence > 0.00001f) {
                    TouchResponseSource(
                        cr::ResponseOrigin::Vehicle,
                        waveEvidence,
                        300
                    );
                }
            } else {
                boatWaveInit_ = false;
                previousBoatRoll_ = 0.0f;
                previousBoatPitch_ = 0.0f;

                const float release =
                    1.0f -
                    std::exp(
                        -cfg_.boatWaveSmoothing *
                        1.35f *
                        Clamp(dt, 0.0f, 0.05f)
                    );

                boatWaveSide_ +=
                    (0.0f - boatWaveSide_) *
                    release;
                boatWaveVertical_ +=
                    (0.0f - boatWaveVertical_) *
                    release;
                boatWaveForward_ +=
                    (0.0f - boatWaveForward_) *
                    release;
                boatWavePitchDeg_ +=
                    (0.0f - boatWavePitchDeg_) *
                    release;
                boatWaveRollDeg_ +=
                    (0.0f - boatWaveRollDeg_) *
                    release;
            }

            if (aircraft && cfg_.aircraftPhysicalEnabled) {
                const bool helicopter =
                    appearance ==
                    VEHICLE_APPEARANCE_HELI;

                const float aircraftRoll =
                    Clamp(
                        vm.right.z,
                        -0.95f,
                        0.95f
                    );

                const float aircraftPitch =
                    Clamp(
                        forward.z,
                        -0.95f,
                        0.95f
                    );

                if (!aircraftMotionInit_) {
                    previousAircraftRoll_ =
                        aircraftRoll;
                    previousAircraftPitch_ =
                        aircraftPitch;
                    aircraftMotionInit_ = true;
                }

                const float safeAircraftDt =
                    std::max(dt, 0.001f);

                const float rollRate =
                    (aircraftRoll -
                     previousAircraftRoll_) /
                    safeAircraftDt;

                const float pitchRate =
                    (aircraftPitch -
                     previousAircraftPitch_) /
                    safeAircraftDt;

                previousAircraftRoll_ =
                    aircraftRoll;
                previousAircraftPitch_ =
                    aircraftPitch;

                float targetSide =
                    -vehicleSideAccel_ *
                    cfg_.aircraftAccelSideScale;

                float targetForward =
                    -vehicleForwardAccel_ *
                    cfg_.aircraftAccelForwardScale;

                float targetVertical =
                    -vehicleVerticalAccelNow_ *
                    cfg_.aircraftAccelVerticalScale;

                targetSide +=
                    (-aircraftRoll - Clamp(rollRate, -3.0f, 3.0f) * 0.45f) *
                    cfg_.aircraftAttitudeHold;

                targetForward +=
                    (-aircraftPitch - Clamp(pitchRate, -3.0f, 3.0f) * 0.45f) *
                    cfg_.aircraftAttitudeHold;

                // Pitch/roll rates must also reach translation when optional
                // camera rotation is off. Scale physical response once, just
                // like the turbulence and engine contributions below.
                targetSide *= master;
                targetVertical *= master;
                targetForward *= master;

                const float weatherIntensity =
                    Clamp(
                        CWeather::Wind * 0.65f +
                        CWeather::Rain * 0.18f +
                        CWeather::Sandstorm * 0.30f,
                        0.0f,
                        2.0f
                    );

                const float turbulenceScale =
                    helicopter
                        ? cfg_.aircraftHeliTurbulenceScale
                        : cfg_.aircraftPlaneTurbulenceScale;

                const float turbulenceAmp =
                    cfg_.aircraftTurbulenceStrength *
                    cfg_.presetAmbient *
                    turbulenceScale *
                    (0.35f +
                     vehicleSpeed_ * 0.65f) *
                    (1.0f +
                     weatherIntensity *
                     cfg_.aircraftWeatherInfluence) *
                    cfg_.highFrequencyScale *
                    master;

                const float turbulence =
                    cfg_.proceduralNoiseEnabled
                        ? aircraftTurbulenceNoise_.Update(
                            dt,
                            helicopter
                                ? 6.5f
                                : 4.2f,
                            helicopter
                                ? 11.0f
                                : 7.5f
                        )
                        : std::sin(
                            t *
                            (helicopter
                                ? 6.5f
                                : 4.2f) *
                            2.0f *
                            kPi
                        );

                targetSide +=
                    turbulence *
                    turbulenceAmp;

                targetVertical +=
                    turbulence *
                    turbulenceAmp *
                    (helicopter ? 0.82f : 0.62f);

                targetForward +=
                    turbulence *
                    turbulenceAmp *
                    (helicopter ? 0.38f : 0.48f);

                // Rotor/airframe texture is intentionally tiny and only
                // supplements physical motion.
                const float engineTexture =
                    helicopter
                        ? cfg_.aircraftHeliRotorVibration
                        : cfg_.aircraftPlaneAirframeVibration;

                if (v->bEngineOn &&
                    engineTexture > 0.00001f &&
                    !cfg_.comfortPreset) {
                    const float engineActivity =
                        Clamp(
                            0.30f +
                            std::fabs(v->m_fGasPedal) *
                                0.55f +
                            vehicleSpeed_ *
                                0.30f,
                            0.30f,
                            1.20f
                        );

                    const float texture =
                        cfg_.proceduralNoiseEnabled
                            ? aircraftEngineNoise_.Update(
                                dt,
                                helicopter
                                    ? 14.0f
                                    : 10.0f,
                                helicopter
                                    ? 20.0f
                                    : 15.0f
                            )
                            : std::sin(
                                t *
                                (helicopter
                                    ? 14.0f
                                    : 10.0f) *
                                2.0f *
                                kPi
                            );

                    targetVertical +=
                        texture *
                        engineTexture *
                        engineActivity *
                        master *
                        cfg_.presetAmbient;

                    targetSide +=
                        texture *
                        engineTexture *
                        engineActivity *
                        master *
                        cfg_.presetAmbient *
                        (helicopter ? 0.45f : 0.22f);
                }

                targetSide =
                    Clamp(
                        targetSide,
                        -cfg_.aircraftMaxSide,
                        cfg_.aircraftMaxSide
                    );

                targetVertical =
                    Clamp(
                        targetVertical,
                        -cfg_.aircraftMaxVertical,
                        cfg_.aircraftMaxVertical
                    );

                targetForward =
                    Clamp(
                        targetForward,
                        -cfg_.aircraftMaxForward,
                        cfg_.aircraftMaxForward
                    );

                const float aircraftAlpha =
                    1.0f -
                    std::exp(
                        -cfg_.aircraftSmoothing *
                        Clamp(dt, 0.0f, 0.05f)
                    );

                aircraftResponseSide_ +=
                    (targetSide -
                     aircraftResponseSide_) *
                    aircraftAlpha;

                aircraftResponseVertical_ +=
                    (targetVertical -
                     aircraftResponseVertical_) *
                    aircraftAlpha;

                aircraftResponseForward_ +=
                    (targetForward -
                     aircraftResponseForward_) *
                    aircraftAlpha;

                advancedVehicleSide_ +=
                    aircraftResponseSide_;

                advancedVehicleVertical_ +=
                    aircraftResponseVertical_;

                advancedVehicleForward_ +=
                    aircraftResponseForward_;

                aircraftPitchDeg_ =
                    Clamp(
                        (-aircraftPitch * 0.60f -
                         pitchRate * 0.012f) *
                        cfg_.aircraftRotationPitchDeg,
                        -cfg_.aircraftRotationPitchDeg,
                        cfg_.aircraftRotationPitchDeg
                    );

                aircraftRollDeg_ =
                    Clamp(
                        (-aircraftRoll * 0.65f -
                         rollRate * 0.012f) *
                        cfg_.aircraftRotationRollDeg,
                        -cfg_.aircraftRotationRollDeg,
                        cfg_.aircraftRotationRollDeg
                    );

                const float aircraftEvidence =
                    std::fabs(aircraftResponseSide_) +
                    std::fabs(aircraftResponseVertical_) +
                    std::fabs(aircraftResponseForward_);

                if (aircraftEvidence > 0.00001f) {
                    TouchResponseSource(
                        cr::ResponseOrigin::Vehicle,
                        aircraftEvidence,
                        300
                    );
                }
            } else {
                aircraftMotionInit_ = false;
                previousAircraftRoll_ = 0.0f;
                previousAircraftPitch_ = 0.0f;

                const float release =
                    1.0f -
                    std::exp(
                        -cfg_.aircraftSmoothing *
                        1.35f *
                        Clamp(dt, 0.0f, 0.05f)
                    );

                aircraftResponseSide_ +=
                    (0.0f -
                     aircraftResponseSide_) *
                    release;

                aircraftResponseVertical_ +=
                    (0.0f -
                     aircraftResponseVertical_) *
                    release;

                aircraftResponseForward_ +=
                    (0.0f -
                     aircraftResponseForward_) *
                    release;

                aircraftPitchDeg_ +=
                    (0.0f -
                     aircraftPitchDeg_) *
                    release;

                aircraftRollDeg_ +=
                    (0.0f -
                     aircraftRollDeg_) *
                    release;
            }

            if (train && vehicleSpeed_ > 0.01f) {
                const float joints =
                    cfg_.trainJointStrength *
                    Clamp(vehicleSpeed_ / 0.40f, 0.25f, 1.35f) *
                    cfg_.highFrequencyScale *
                    master;

                const float phase =
                    t *
                    (3.0f + vehicleSpeed_ * 12.0f) *
                    2.0f * kPi;

                advancedVehicleVertical_ +=
                    std::sin(phase) * joints;
                advancedVehicleForward_ +=
                    std::sin(phase + 0.8f) * joints * 0.55f;
            }

            if (appearance == VEHICLE_APPEARANCE_AUTOMOBILE) {
                CAutomobile* car =
                    reinterpret_cast<CAutomobile*>(v);

                if (cfg_.wheelResponseEnabled) {
                    if (!wheelCompressionInit_) {
                        for (unsigned wheel = 0; wheel < 4; ++wheel)
                            previousWheelCompression_[wheel] =
                                car->m_fWheelsSuspensionCompression[wheel];
                        wheelCompressionInit_ = true;
                    } else {
                        for (unsigned wheel = 0; wheel < 4; ++wheel) {
                            const float current =
                                car->m_fWheelsSuspensionCompression[wheel];
                            const float delta =
                                previousWheelCompression_[wheel] - current;
                            previousWheelCompression_[wheel] = current;

                            if (std::fabs(delta) < 0.008f)
                                continue;

                            const bool left =
                                wheel == WHEEL_FRONT_LEFT ||
                                wheel == WHEEL_REAR_LEFT;
                            const bool front =
                                wheel == WHEEL_FRONT_LEFT ||
                                wheel == WHEEL_FRONT_RIGHT;

                            const float wheelImpulse =
                                Clamp(delta * 5.0f, -0.75f, 0.75f) *
                                cfg_.wheelCompressionStrength *
                                master;

                            vehicleSuspensionSpring_.vertical.AddImpulse(
                                -wheelImpulse
                            );
                            vehicleSuspensionSpring_.side.AddImpulse(
                                (left ? 1.0f : -1.0f) *
                                wheelImpulse * 0.50f
                            );
                            vehicleSuspensionSpring_.forward.AddImpulse(
                                (front ? -1.0f : 1.0f) *
                                wheelImpulse * 0.34f
                            );
                        }
                    }
                } else {
                    wheelCompressionInit_ = false;
                }

                unsigned skiddingWheels = 0;
                for (unsigned wheel = 0; wheel < 4; ++wheel) {
                    if (car->m_wheelState[wheel] == WHEEL_STATE_SKIDDING ||
                        car->m_wheelState[wheel] == WHEEL_STATE_SPINNING) {
                        ++skiddingWheels;
                    }
                }

                const bool tractionLoss =
                    skiddingWheels > 0 || v->bIsHandbrakeOn;
                if (cfg_.wheelResponseEnabled && tractionLoss != previousTractionLoss_) {
                    if (tractionLoss) {
                        advancedVehicleSide_ +=
                            Clamp(vehicleSideAccel_ * -0.018f, -0.08f, 0.08f) *
                            cfg_.tractionLossStrength * 10.0f;
                    } else {
                        vehicleCollisionSpring_.side.AddImpulse(
                            Clamp(-vehicleSideAccel_ * 0.08f, -1.0f, 1.0f) *
                            cfg_.tractionRegainStrength *
                            master
                        );
                    }
                    previousTractionLoss_ = tractionLoss;
                }

                if (tractionLoss) {
                    const float skidAmount =
                        Clamp(
                            float(skiddingWheels) / 4.0f +
                            (v->bIsHandbrakeOn ? 0.25f : 0.0f),
                            0.0f,
                            1.0f
                        );
                    const float amp =
                        cfg_.skidStrength *
                        skidAmount *
                        Clamp(vehicleSpeed_ / 0.30f, 0.30f, 1.25f) *
                        cfg_.highFrequencyScale *
                        master;

                    advancedVehicleSide_ +=
                        std::sin(t * 16.0f) * amp;
                    advancedVehicleVertical_ +=
                        std::sin(t * 21.0f + 0.6f) * amp * 0.40f;
                }

                const float surfaceRaw =
                    CollisionMaterialMultiplier(v->m_nContactSurface);
                const float roughness =
                    Clamp(std::fabs(surfaceRaw - 1.0f) * 2.0f, 0.0f, 1.0f);
                const float roadAmp =
                    (cfg_.roadTextureStrength * cfg_.presetAmbient) *
                    roughness *
                    Clamp(vehicleSpeed_ / 0.35f, 0.0f, 1.25f) *
                    cfg_.highFrequencyScale *
                    master;

                advancedVehicleVertical_ +=
                    std::sin(t * 24.0f + float(v->m_nContactSurface) * 0.17f) *
                    roadAmp;
                advancedVehicleSide_ +=
                    std::sin(t * 18.0f + 1.1f) *
                    roadAmp * 0.34f;
            }

            const int contactWheels =
                v->GetNumContactWheels();

            if (aircraft && cfg_.aircraftPhysicalEnabled &&
                previousVehicleContactWheels_ == 0 &&
                contactWheels > 0 &&
                prevVehicleVelocity_.z <
                    -cfg_.aircraftTouchdownThreshold) {

                const bool helicopter =
                    appearance ==
                    VEHICLE_APPEARANCE_HELI;

                const float landing =
                    cfg_.aircraftLandingStrength *
                    Clamp(
                        (-prevVehicleVelocity_.z -
                         cfg_.aircraftTouchdownThreshold) /
                        0.20f,
                        0.20f,
                        1.60f
                    ) *
                    master;

                vehicleCollisionSpring_.vertical.AddImpulse(
                    -landing
                );

                vehicleCollisionSpring_.forward.AddImpulse(
                    -landing *
                    cfg_.aircraftTouchdownForwardScale *
                    (helicopter ? 0.65f : 1.0f)
                );

                vehicleCollisionSpring_.side.AddImpulse(
                    Clamp(
                        -vehicleSideAccel_ *
                        0.012f,
                        -landing * 0.20f,
                        landing * 0.20f
                    )
                );

                TouchResponseSource(
                    cr::ResponseOrigin::Vehicle,
                    landing,
                    900
                );

                AddFovKick(
                    cfg_.fovVehicleImpactKick *
                    Clamp(
                        landing * 0.55f,
                        0.25f,
                        1.20f
                    )
                );

                if (cfg_.debug) {
                    log_.Write(
                        "AIRCRAFT TOUCHDOWN heli=%d vz=%.4f "
                        "contacts=%d impulse=%.4f",
                        helicopter ? 1 : 0,
                        prevVehicleVelocity_.z,
                        contactWheels,
                        landing
                    );
                }
            }

            previousVehicleContactWheels_ = contactWheels;
        }

        prevVehicleVelocity_ = vel;
        prevVehicleHeading_ = heading;
    }

    void PrepareVehicleOffset(CVehicle* v, float dt) {
        AuditHit(AUD_PrepareVehicleOffset);
        if (!v) {
            pendingAllowed_ = false;
            return;
        }

        idleTime_ += dt;

        const float profile = VehicleProfileScale(v);
        const float master =
            cfg_.vehicleStrength *
            cfg_.strength *
            cfg_.presetScale *
            profile *
            vehiclePersonalityResponse_ *
            (cfg_.vehicleResponseScale * cfg_.masterVehicleScale * cfg_.presetVehicle);

        // Speed texture is intentionally tiny: it should read as road/engine
        // energy rather than a sine-wave bob. Bikes receive more via profile.
        const float speedNorm = Clamp(vehicleSpeed_ / 0.75f, 0.0f, 1.0f);
        const float vibration =
            (cfg_.vehicleSpeedVibration * cfg_.presetAmbient) *
            speedNorm *
            master *
            vehiclePersonalityVibration_ *
            cfg_.highFrequencyScale;

        if (vibration > 0.000001f) {
            vehicleVibrationSide_ =
                std::sin(idleTime_ * cfg_.vehicleSpeedVibrationRate * 1.73f) *
                vibration * 0.55f;
            vehicleVibrationVertical_ =
                std::sin(idleTime_ * cfg_.vehicleSpeedVibrationRate * 2.21f + 0.8f) *
                vibration;
            vehicleVibrationForward_ =
                std::sin(idleTime_ * cfg_.vehicleSpeedVibrationRate * 1.11f + 2.0f) *
                vibration * 0.30f;
        } else {
            vehicleVibrationSide_ = 0.0f;
            vehicleVibrationVertical_ = 0.0f;
            vehicleVibrationForward_ = 0.0f;
        }

        // Horn vibration is deliberately independent from speed so a parked
        // car still gives a faint body/chassis buzz while the horn is sounding.
        // Smooth attack/release prevents a hard camera discontinuity when the
        // horn key is pressed or released.
        vehicleHornActive_ =
            cfg_.vehicleHornEnabled && v->m_nHornCounter != 0;

        const float hornTarget = vehicleHornActive_ ? 1.0f : 0.0f;
        const float hornResponse =
            vehicleHornActive_ ? cfg_.vehicleHornAttack : cfg_.vehicleHornRelease;
        const float hornAlpha =
            1.0f - std::exp(-hornResponse * Clamp(dt, 0.0f, 0.050f));
        vehicleHornBlend_ += (hornTarget - vehicleHornBlend_) * hornAlpha;
        vehicleHornBlend_ = Clamp(vehicleHornBlend_, 0.0f, 1.0f);

        if (vehicleHornActive_)
            ++vehicleHornFrames_;

        const float hornAmp =
            cfg_.vehicleHornVibration * vehicleHornBlend_ * master;
        const float hornPhase =
            idleTime_ * cfg_.vehicleHornVibrationRate * 2.0f * kPi;

        if (hornAmp > 0.000001f) {
            vehicleHornSide_ =
                std::sin(hornPhase * 0.87f + 0.55f) * hornAmp * 0.30f;
            vehicleHornVertical_ =
                (std::sin(hornPhase) * 0.72f +
                 std::sin(hornPhase * 1.91f + 1.10f) * 0.28f) *
                hornAmp;
            vehicleHornForward_ =
                std::sin(hornPhase * 1.13f + 1.85f) * hornAmp * 0.24f;
        } else {
            vehicleHornSide_ = 0.0f;
            vehicleHornVertical_ = 0.0f;
            vehicleHornForward_ = 0.0f;
        }

        // Own-vehicle response is generated in VEHICLE-local space
        // (right/forward). It must be converted to CAMERA-local space before
        // being mixed with nearby/world/weather/camera effects. The previous
        // code mixed these spaces directly, which made orbiting the camera
        // visually drag/pitch the vehicle body even though no vehicle physics
        // state was actually modified.
        const float ownVehicleSide =
            vehicleSpring_.side.position +
            vehicleSuspensionSpring_.side.position +
            vehicleCollisionSpring_.side.position +
            vehicleCollisionKickSide_ +
            advancedVehicleSide_;

        const float ownVehicleForward =
            vehicleSpring_.forward.position +
            vehicleSuspensionSpring_.forward.position +
            vehicleCollisionSpring_.forward.position +
            vehicleCollisionKickForward_ +
            advancedVehicleForward_;

        float ownCameraSide = ownVehicleSide;
        float ownCameraForward = ownVehicleForward;

        VehicleLocalXYToCameraLocal(
            v,
            ownVehicleSide,
            ownVehicleForward,
            ownCameraSide,
            ownCameraForward
        );

        const float ambientScale=ambientDuck_;

        float side =
            ownCameraSide +
            (nearbyVehicleSide_+largeVehicleSide_+weatherSide_+
             nativeShakeSide_+vehicleVibrationSide_+vehicleHornSide_)*
                ambientScale +
            obstructionSpring_.side.position;

        float vertical =
            vehicleSpring_.vertical.position +
            vehicleSuspensionSpring_.vertical.position +
            vehicleCollisionSpring_.vertical.position +
            vehicleCollisionKickVertical_ +
            advancedVehicleVertical_ +
            (vehicleVibrationVertical_+vehicleHornVertical_+
             nearbyVehicleVertical_+largeVehicleVertical_+weatherVertical_+
             nativeShakeVertical_)*ambientScale +
            obstructionSpring_.vertical.position;

        float forward =
            ownCameraForward +
            (nearbyVehicleForward_+largeVehicleForward_+weatherForward_+
             nativeShakeForward_+vehicleVibrationForward_+vehicleHornForward_)*
                ambientScale +
            obstructionSpring_.forward.position;

        side += mountedWaterSide_;
        vertical += mountedWaterVertical_;
        forward += mountedWaterForward_;

        // Player weapon recoil remains visible in supported third-person
        side += recoilSpring_.side.position * cfg_.strength * cfg_.presetScale;
        vertical += recoilSpring_.vertical.position * cfg_.strength * cfg_.presetScale;
        forward += recoilSpring_.forward.position * cfg_.strength * cfg_.presetScale;

        // Nearby blasts/damage/NPC gunfire/melee use the common impact spring.
        side += impactSpring_.side.position * cfg_.strength * cfg_.presetScale;
        vertical += impactSpring_.vertical.position * cfg_.strength * cfg_.presetScale;
        forward += impactSpring_.forward.position * cfg_.strength * cfg_.presetScale;

        if (cfg_.debug && cfg_.logEveryMs > 0 &&
            CTimer::m_snTimeInMilliseconds - lastVehicleFrameDebugMs_ >= cfg_.logEveryMs) {
            lastVehicleFrameDebugMs_ = CTimer::m_snTimeInMilliseconds;
            log_.Write(
                "VEHICLE CAMERA BASIS local=(%.4f %.4f) camera=(%.4f %.4f) "
                "accelLocal=(%.3f %.3f)",
                ownVehicleSide,
                ownVehicleForward,
                ownCameraSide,
                ownCameraForward,
                vehicleSideAccel_,
                vehicleForwardAccel_
            );
        }

        const float vehicleEvidence =
            std::fabs(side) + std::fabs(vertical) + std::fabs(forward);
        if (vehicleEvidence > 0.00001f) {
            TouchResponseSource(
                cr::ResponseOrigin::Vehicle,
                vehicleEvidence,
                350
            );
        }

        side*=externalScale_*cfg_.masterOverallScale*contextScale_;
        vertical*=externalScale_*cfg_.masterOverallScale*contextScale_;
        forward*=externalScale_*cfg_.masterOverallScale*contextScale_;

        ApplyResponseBudget(
            side,vertical,forward,
            cfg_.vehicleMaxSide,
            cfg_.vehicleMaxVertical,
            cfg_.vehicleMaxForward
        );

        pendingSide_=Clamp(side,-cfg_.vehicleMaxSide,cfg_.vehicleMaxSide);
        pendingVertical_=Clamp(vertical,-cfg_.vehicleMaxVertical,cfg_.vehicleMaxVertical);
        pendingForward_=Clamp(forward,-cfg_.vehicleMaxForward,cfg_.vehicleMaxForward);

        if (cfg_.rotationEnabled) {
            float cameraAccelSide = vehicleSideAccel_;
            float cameraAccelForward = vehicleForwardAccel_;

            VehicleLocalXYToCameraLocal(
                v,
                vehicleSideAccel_,
                vehicleForwardAccel_,
                cameraAccelSide,
                cameraAccelForward
            );

            float advancedCameraSide = advancedVehicleSide_;
            float advancedCameraForward = advancedVehicleForward_;

            VehicleLocalXYToCameraLocal(
                v,
                advancedVehicleSide_,
                advancedVehicleForward_,
                advancedCameraSide,
                advancedCameraForward
            );

            const bool boatAppearance =
                v->GetVehicleAppearance() ==
                VEHICLE_APPEARANCE_BOAT;

            const float boatPitchContribution =
                boatAppearance
                    ? boatWavePitchDeg_
                    : 0.0f;

            const float boatRollContribution =
                boatAppearance
                    ? boatWaveRollDeg_
                    : 0.0f;

            const bool aircraftAppearance =
                v->GetVehicleAppearance() ==
                    VEHICLE_APPEARANCE_HELI ||
                v->GetVehicleAppearance() ==
                    VEHICLE_APPEARANCE_PLANE;

            const float aircraftPitchContribution =
                aircraftAppearance
                    ? aircraftPitchDeg_
                    : 0.0f;

            const float aircraftRollContribution =
                aircraftAppearance
                    ? aircraftRollDeg_
                    : 0.0f;

            lastPitchDeg_ =
                Clamp(
                    -cameraAccelForward * 0.16f +
                    boatPitchContribution +
                    aircraftPitchContribution +
                    directionalRotationSpring_.vertical.position,
                    -cfg_.maxPitchDeg,
                    cfg_.maxPitchDeg
                ) *
                cfg_.rotationSafetyScale *
                cfg_.presetRotation *
                contextScale_;

            lastRollDeg_ =
                Clamp(
                    cameraAccelSide * 0.18f +
                    advancedCameraSide * 5.0f +
                    boatRollContribution +
                    aircraftRollContribution +
                    directionalRotationSpring_.side.position,
                    -cfg_.maxRollDeg,
                    cfg_.maxRollDeg
                ) *
                cfg_.rotationSafetyScale *
                cfg_.presetRotation *
                contextScale_;
            lastYawDeg_ = 0.0f;
            pendingPitchRad_ = lastPitchDeg_ * (kPi / 180.0f);
            pendingRollRad_ = lastRollDeg_ * (kPi / 180.0f);
            pendingYawRad_ = 0.0f;
        } else {
            pendingPitchRad_ = 0.0f;
            pendingRollRad_ = 0.0f;
            pendingYawRad_ = 0.0f;
            lastPitchDeg_ = 0.0f;
            lastRollDeg_ = 0.0f;
            lastYawDeg_ = 0.0f;
        }
        pendingAllowed_ = true;
        ++prepared_;
    }

    void UpdateStrength(float dt, float target) {
        AuditHit(AUD_UpdateStrength);
        const float alpha =
            1.0f - std::exp(-cfg_.stateResponse * dt);

        strengthNow_ +=
            (target - strengthNow_) * alpha;

        strengthNow_ =
            Clamp(strengthNow_, 0.0f, 2.0f);
    }

    void UpdateSprings(float dt) {
        AuditHit(AUD_UpdateSprings);
        const bool settledIdle =
            stationaryTime_ >= cfg_.idleSettleDelay &&
            (motion_ == MotionState::Idle ||
             motion_ == MotionState::Crouch ||
             motion_ == MotionState::Aim);

        const float idleDamping =
            settledIdle ? cfg_.idleDampingMultiplier : 1.0f;

        gaitSpring_.Update(
            dt,
            cfg_.spring,
            cfg_.damping * idleDamping,
            gaitTargetSide_,
            gaitTargetVertical_,
            gaitTargetForward_
        );

        lookSpring_.Update(
            dt,
            cfg_.lookSpring,
            cfg_.lookDamping * idleDamping,
            lookTargetSide_,
            lookTargetVertical_,
            lookTargetForward_
        );

        recoilSpring_.Update(
            dt,
            cfg_.spring *
                cfg_.recoilSpringMultiplier *
                currentRecoilRecovery_,
            cfg_.damping *
                cfg_.recoilDampingMultiplier *
                cfg_.presetRecovery *
                currentRecoilRecovery_
        );

        impactSpring_.Update(
            dt,
            cfg_.spring * 0.82f,
            cfg_.damping * 0.82f * cfg_.presetRecovery
        );
        explosionRotationSpring_.Update(dt,cfg_.explosionRotationSpring,(cfg_.explosionRotationDamping * cfg_.presetRecovery));
        explosionRotationSpring_.side.position=Clamp(explosionRotationSpring_.side.position,-cfg_.maxRollDeg,cfg_.maxRollDeg);
        explosionRotationSpring_.vertical.position=Clamp(explosionRotationSpring_.vertical.position,-cfg_.maxPitchDeg,cfg_.maxPitchDeg);
        explosionRotationSpring_.forward.position=Clamp(explosionRotationSpring_.forward.position,-cfg_.maxYawDeg,cfg_.maxYawDeg);

        directionalRotationSpring_.Update(
            dt,
            cfg_.directionalRotationSpring,
            cfg_.directionalRotationDamping*cfg_.presetRecovery
        );
        directionalRotationSpring_.side.position=Clamp(
            directionalRotationSpring_.side.position,-cfg_.maxRollDeg,cfg_.maxRollDeg
        );
        directionalRotationSpring_.vertical.position=Clamp(
            directionalRotationSpring_.vertical.position,-cfg_.maxPitchDeg,cfg_.maxPitchDeg
        );
        directionalRotationSpring_.forward.position=Clamp(
            directionalRotationSpring_.forward.position,-cfg_.maxYawDeg,cfg_.maxYawDeg
        );

        impactSpring_.side.position = Clamp(impactSpring_.side.position, -cfg_.maxImpactOffset, cfg_.maxImpactOffset);
        impactSpring_.vertical.position = Clamp(impactSpring_.vertical.position, -cfg_.maxImpactOffset, cfg_.maxImpactOffset);
        impactSpring_.forward.position = Clamp(impactSpring_.forward.position, -cfg_.maxImpactOffset, cfg_.maxImpactOffset);

        recoilSpring_.vertical.position =
            Clamp(recoilSpring_.vertical.position, -0.08f, 0.18f);
        recoilSpring_.forward.position =
            Clamp(recoilSpring_.forward.position, -0.10f, 0.05f);
        recoilSpring_.side.position =
            Clamp(recoilSpring_.side.position, -0.06f, 0.06f);

        recoilSpring_.vertical.velocity =
            Clamp(recoilSpring_.vertical.velocity, -2.0f, 4.0f);
        recoilSpring_.forward.velocity =
            Clamp(recoilSpring_.forward.velocity, -2.5f, 1.0f);
        recoilSpring_.side.velocity =
            Clamp(recoilSpring_.side.velocity, -1.5f, 1.5f);

    }

    void StabilizeTransientState() {
        auto sane = [](float v, float limit) {
            return std::isfinite(v) && std::fabs(v) <= limit;
        };

        bool resetImpact = false;
        const Spring1D* axes[] = {
            &impactSpring_.side,
            &impactSpring_.vertical,
            &impactSpring_.forward
        };

        for (const Spring1D* axis : axes) {
            if (!sane(axis->position, 4.0f) ||
                !sane(axis->velocity, 40.0f)) {
                resetImpact = true;
                break;
            }
        }

        if (resetImpact) {
            impactSpring_.Reset();
            directionalRotationSpring_.Reset();
            transientEvents_.BeginFrame();
            responseMixer_.BeginFrame();
            mixedEventOffset_ = {};
            log_.Write("STABILITY WATCHDOG reset impact/mixer due to non-finite or runaway transient state");
        }

        // Snap microscopic residuals to zero. This is below visible motion and
        // prevents denormal-sized spring tails from surviving indefinitely.
        auto settle = [](Spring1D& axis) {
            if (std::isfinite(axis.position) &&
                std::isfinite(axis.velocity) &&
                std::fabs(axis.position) < 0.00001f &&
                std::fabs(axis.velocity) < 0.00005f) {
                axis.position = 0.0f;
                axis.velocity = 0.0f;
            }
        };

        settle(impactSpring_.side);
        settle(impactSpring_.vertical);
        settle(impactSpring_.forward);
    }

    void PrepareOffset(bool aiming, float dt) {
        AuditHit(AUD_PrepareOffset);
        idleTime_ += dt;
        const float t = idleTime_;

        const bool fullySettledIdle =
            stationaryTime_ >= cfg_.idleSettleDelay + 0.75f &&
            cfg_.idleSettledScale <= 0.001f;

        float idleSideWave = 0.0f;
        float idleVerticalWave = 0.0f;
        float idleForwardWave = 0.0f;

        if (!fullySettledIdle) {
            idleSideWave =
                0.70f * std::sin(t * 0.71f + 0.7f) +
                0.29f * std::sin(t * 1.17f + 2.1f) +
                0.11f * std::sin(t * 1.91f + 0.2f);

            idleVerticalWave =
                0.73f * std::sin(t * 0.55f + 1.3f) +
                0.27f * std::sin(t * 0.97f + 3.9f) +
                0.10f * std::sin(t * 1.69f + 0.4f);

            idleForwardWave =
                0.72f * std::sin(t * 0.43f + 3.0f) +
                0.28f * std::sin(t * 0.83f + 1.0f);
        }

        float idleSideWaveMutable = idleSideWave;
        float idleVerticalWaveMutable = idleVerticalWave;
        if (cfg_.proceduralNoiseEnabled && !fullySettledIdle) {
            const float mix = cfg_.proceduralNoiseMix;
            idleSideWaveMutable =
                idleSideWave * (1.0f - mix) +
                idleNoiseSide_.Update(dt, 1.35f, 3.4f) * mix;
            idleVerticalWaveMutable =
                idleVerticalWave * (1.0f - mix) +
                idleNoiseVertical_.Update(dt, 1.05f, 3.0f) * mix;
            idleForwardWave =
                idleForwardWave * (1.0f - mix) +
                idleNoiseForward_.Update(dt, 0.90f, 2.6f) * mix;
        }

        float side =
            cfg_.idleSide * idleSideWaveMutable;

        float vertical =
            cfg_.idleVertical * idleVerticalWaveMutable;

        float forward =
            cfg_.idleForward * idleForwardWave;

        if (aiming && cfg_.breathingEnabled) {
            const float breath = std::sin(t * cfg_.aimBreathingRate * 2.0f * kPi);
            const float breath2 = std::sin(t * cfg_.aimBreathingRate * kPi + 1.2f);
            side += (cfg_.aimBreathingSide * cfg_.presetAmbient) * breath2;
            vertical += (cfg_.aimBreathingVertical * cfg_.presetAmbient) * breath;
            forward += (cfg_.aimBreathingForward * cfg_.presetAmbient) * breath;
        }

        float idleLayer = 1.0f;
        if (Moving(motion_))
            idleLayer = 0.68f;
        if (aiming)
            idleLayer = 0.38f;

        // The procedural idle wave is an "alive" layer, not locomotion.
        // Fade it to a much smaller amplitude after genuine sustained idle so
        // it reads as breathing/weight instead of continuous camera drift.
        if (stationaryTime_ >= cfg_.idleSettleDelay) {
            const float settleAge =
                stationaryTime_ - cfg_.idleSettleDelay;
            const float settleBlend =
                1.0f - std::exp(-5.0f * settleAge);
            const float settled =
                1.0f + (cfg_.idleSettledScale - 1.0f) * settleBlend;
            idleLayer *= settled;
        }

        side *= idleLayer * (cfg_.idleScale * cfg_.presetAmbient) * ambientDuck_;
        vertical *= idleLayer * (cfg_.idleScale * cfg_.presetAmbient) * ambientDuck_;
        forward *= idleLayer * (cfg_.idleScale * cfg_.presetAmbient) * ambientDuck_;

        // Fully settle the always-on "alive" layer at genuine rest.
        if (fullySettledIdle) {
            side = 0.0f;
            vertical = 0.0f;
            forward = 0.0f;
        }

        // Body/locomotion layer.
        side +=
            gaitSpring_.side.position +
            lookSpring_.side.position;

        vertical +=
            gaitSpring_.vertical.position +
            lookSpring_.vertical.position;

        forward +=
            gaitSpring_.forward.position +
            lookSpring_.forward.position;

        float bodyScale = cfg_.strength * cfg_.presetScale;

        if (aiming)
            bodyScale *= Clamp(strengthNow_, 0.30f, 0.62f);

        side *= bodyScale;
        vertical *= bodyScale;
        forward *= bodyScale;

        // Recoil is applied AFTER aim damping. Firing while aiming should
        // remain visible even though ordinary bob is intentionally calmer.
        side +=
            recoilSpring_.side.position *
            cfg_.strength * cfg_.presetScale;

        vertical +=
            recoilSpring_.vertical.position *
            cfg_.strength * cfg_.presetScale;

        forward +=
            recoilSpring_.forward.position *
            cfg_.strength * cfg_.presetScale;

        side += impactSpring_.side.position * cfg_.strength * cfg_.presetScale;
        vertical += impactSpring_.vertical.position * cfg_.strength * cfg_.presetScale;
        forward += impactSpring_.forward.position * cfg_.strength * cfg_.presetScale;

        // Chainsaw engine/cutting pressure is a direct bounded layer, not
        // persistent spring state. It vanishes immediately when weapon changes.
        forward +=
            chainsawForwardBias_ *
            cfg_.strength *
            cfg_.presetScale;

        const float ambientScale=ambientDuck_;
        side +=
            (advancedPlayerSide_+nearbyVehicleSide_+largeVehicleSide_+
             weatherSide_+nativeShakeSide_)*ambientScale+
            obstructionSpring_.side.position;
        vertical +=
            (advancedPlayerVertical_+nearbyVehicleVertical_+largeVehicleVertical_+
             weatherVertical_+nativeShakeVertical_)*ambientScale+
            obstructionSpring_.vertical.position;
        forward +=
            (advancedPlayerForward_+nearbyVehicleForward_+largeVehicleForward_+
             weatherForward_+nativeShakeForward_)*ambientScale+
            obstructionSpring_.forward.position;

        // Thrust/acceleration is player motion, not ambient world vibration.
        if (cfg_.advancedResponseEnabled && cfg_.jetpackDirectionalEnabled && previousJetpack_) {
            const float jetpackScale = cfg_.strength * cfg_.presetScale;
            side += jetpackResponseSide_ * jetpackScale;
            vertical += jetpackResponseVertical_ * jetpackScale;
            forward += jetpackResponseForward_ * jetpackScale;
        }

        const float finalScale = cfg_.strength;

        // Orientation layer. CMatrix uses:
        //   right = camera right
        //   up    = camera forward
        //   at    = camera vertical/up.
        const float footPhase =
            Moving(motion_) ?
            Clamp(footDiff_ * 2.7f, -1.0f, 1.0f) :
            0.0f;

        float pitchDeg =
            idleVerticalWaveMutable *
            cfg_.idlePitchDeg *
            idleLayer;

        float rollDeg =
            idleSideWaveMutable *
            cfg_.idleRollDeg *
            idleLayer;

        float yawDeg = 0.0f;

        if (Moving(motion_)) {
            const float stride =
                std::fabs(footPhase);

            pitchDeg +=
                (-stride + 0.25f) *
                cfg_.gaitPitchDeg *
                strengthNow_;

            rollDeg +=
                footPhase *
                cfg_.gaitRollDeg *
                strengthNow_;

            // Touchdown spring adds a short pitch response.
            const float gaitVerticalNorm =
                cfg_.gaitVertical > 0.001f ?
                Clamp(
                    gaitSpring_.vertical.position /
                    cfg_.gaitVertical,
                    -1.5f,
                    1.5f
                ) :
                0.0f;

            pitchDeg +=
                gaitVerticalNorm *
                cfg_.gaitPitchDeg *
                0.55f;
        }

        const float yawRateNorm =
            Clamp(yawRate_ / 8.0f, -1.0f, 1.0f);

        const float pitchRateNorm =
            Clamp(pitchRate_ / 6.0f, -1.0f, 1.0f);

        // Lean slightly opposite a camera turn, plus a tiny yaw lag.
        rollDeg +=
            -yawRateNorm *
            cfg_.turnRollDeg;

        yawDeg +=
            -yawRateNorm *
            cfg_.lookYawDeg;

        pitchDeg +=
            -pitchRateNorm *
            cfg_.lookPitchDeg;

        if (aiming) {
            pitchDeg *= 0.42f;
            rollDeg *= 0.38f;
            yawDeg *= 0.38f;
        }

        pitchDeg *= finalScale * cfg_.masterOverallScale;
        rollDeg *= finalScale * cfg_.masterOverallScale;
        yawDeg *= finalScale * cfg_.masterOverallScale;


        pitchDeg =
            Clamp(
                pitchDeg,
                -cfg_.maxPitchDeg,
                cfg_.maxPitchDeg
            );

        rollDeg =
            Clamp(
                rollDeg,
                -cfg_.maxRollDeg,
                cfg_.maxRollDeg
            );

        yawDeg =
            Clamp(
                yawDeg,
                -cfg_.maxYawDeg,
                cfg_.maxYawDeg
            );
        const float locomotionEvidence =
            std::fabs(gaitSpring_.side.position) +
            std::fabs(gaitSpring_.vertical.position) +
            std::fabs(gaitSpring_.forward.position);
        if (locomotionEvidence > 0.00001f) {
            TouchResponseSource(
                cr::ResponseOrigin::Locomotion,
                locomotionEvidence,
                350
            );
        }

        const float lookEvidence =
            std::fabs(lookSpring_.side.position) +
            std::fabs(lookSpring_.vertical.position) +
            std::fabs(lookSpring_.forward.position);
        if (lookEvidence > 0.00001f) {
            TouchResponseSource(
                cr::ResponseOrigin::Look,
                lookEvidence,
                350
            );
        }

        if (chainsawActivityBlend_ > 0.01f ||
            chainsawContactBlend_ > 0.01f) {
            TouchResponseSource(
                cr::ResponseOrigin::Melee,
                chainsawActivityBlend_ + chainsawContactBlend_,
                300
            );
        }

        side*=externalScale_*cfg_.masterOverallScale*contextScale_;
        vertical*=externalScale_*cfg_.masterOverallScale*contextScale_;
        forward*=externalScale_*cfg_.masterOverallScale*contextScale_;

        ApplyResponseBudget(
            side,vertical,forward,
            cfg_.maxFinalSide,
            cfg_.maxFinalVertical,
            cfg_.maxFinalForward
        );

        pendingSide_=Clamp(side,-cfg_.maxFinalSide,cfg_.maxFinalSide);
        pendingVertical_=Clamp(vertical,-cfg_.maxFinalVertical,cfg_.maxFinalVertical);
        pendingForward_=Clamp(forward,-cfg_.maxFinalForward,cfg_.maxFinalForward);

        if (cfg_.rotationEnabled) {
            pitchDeg += explosionRotationSpring_.vertical.position;
            rollDeg += explosionRotationSpring_.side.position;
            yawDeg += explosionRotationSpring_.forward.position;
            pitchDeg += directionalRotationSpring_.vertical.position;
            rollDeg += directionalRotationSpring_.side.position;
            yawDeg += directionalRotationSpring_.forward.position;
        }

        // Orientation is opt-in. It is applied only to the RenderWare frame
        // after GTA has copied its stock camera matrix, never to GTA's internal
        // CCamera basis. Translation-only remains the default safe path.
        if (cfg_.rotationEnabled) {
            pitchDeg *= cfg_.rotationSafetyScale * cfg_.presetRotation * contextScale_;
            rollDeg *= cfg_.rotationSafetyScale * cfg_.presetRotation * contextScale_;
            yawDeg *= cfg_.rotationSafetyScale * cfg_.presetRotation * contextScale_;

            pendingPitchRad_ = pitchDeg * (kPi / 180.0f);
            pendingRollRad_ = rollDeg * (kPi / 180.0f);
            pendingYawRad_ = yawDeg * (kPi / 180.0f);

            lastPitchDeg_ = pitchDeg;
            lastRollDeg_ = rollDeg;
            lastYawDeg_ = yawDeg;
        } else {
            pendingPitchRad_ = 0.0f;
            pendingRollRad_ = 0.0f;
            pendingYawRad_ = 0.0f;
            lastPitchDeg_ = 0.0f;
            lastRollDeg_ = 0.0f;
            lastYawDeg_ = 0.0f;
        }

        pendingAllowed_ = true;
        ++prepared_;
    }

    void LogStatus(
        CPlayerPed* p,
        CCam& cam,
        const char* gate,
        bool aiming
    ) {
        if (!log_.Enabled())
            return;

        const unsigned now = CTimer::m_snTimeInMilliseconds;

        if (lastGate_ != gate) {
            log_.Write(
                "GATE %s -> %s camMode=%d player=%p target=%p",
                lastGate_.empty() ? "(startup)" : lastGate_.c_str(),
                gate,
                int(cam.m_nMode),
                p,
                cam.m_pCamTargetEntity
            );
            lastGate_ = gate;
        }

        if (now - lastStatusMs_ < cfg_.logEveryMs)
            return;

        lastStatusMs_ = now;

        const cr::ResponseOrigin dominantSource =
            sourceWatchdog_.Dominant();
        const char* dominantSourceName =
            sourceWatchdog_.ActiveCount() > 0
                ? cr::ResponseOriginName(dominantSource)
                : "NONE";
        const float dominantSourceMagnitude =
            sourceWatchdog_.ActiveCount() > 0
                ? sourceWatchdog_.Magnitude(dominantSource)
                : 0.0f;
        const unsigned dominantSourceAge =
            sourceWatchdog_.ActiveCount() > 0
                ? sourceWatchdog_.LastEvidenceAge(dominantSource, now)
                : 0u;

        if (std::strcmp(gate, "OK-VEHICLE") == 0) {
            const float vehicleHealth =
                lastVehicle_ ? lastVehicle_->m_fHealth : 0.0f;
            const int appearance =
                lastVehicle_ ? lastVehicle_->GetVehicleAppearance() : -1;

            log_.Write(
                "VEHICLE STATUS updates=%llu prepared=%llu commitCalls=%llu "
                "commitApplied=%llu hook=%d gate=%s mode=%d appearance=%d "
                "speed=%.4f accelF/S/Z=(%.3f %.3f %.3f) "
                "targetS/V/F=(%.4f %.4f %.4f) springS/V/F=(%.4f %.4f %.4f) "
                "crashS/V/F=(%.4f %.4f %.4f) kickS/V/F=(%.4f %.4f %.4f) vibS/V/F=(%.4f %.4f %.4f) "
                "horn=%d blend=%.2f hornS/V/F=(%.4f %.4f %.4f) "
                "nearVeh=%u nearDist=%.2f nearAct=%.2f nearS/V/F=(%.4f %.4f %.4f) "
                "impactS/V/F=(%.4f %.4f %.4f) pendingS/V/F=(%.4f %.4f %.4f) "
                "phys=%u contact=%u dmgCrash=%u susp=%u hornFrames=%u health=%.1f loss=%.1f contactI=%.2f contactImp=%.3f suspJolt=%.3f "
                "suspHandling(stiff=%.2f damp=%.2f travel=%.2f mass=%.2f bias=%.2f k=%.1f d=%.1f)",
                static_cast<unsigned long long>(updates_),
                static_cast<unsigned long long>(prepared_),
                static_cast<unsigned long long>(commitCalls_),
                static_cast<unsigned long long>(commitApplied_),
                cameraCommitHookInstalled_ ? 1 : 0,
                gate,
                int(cam.m_nMode),
                appearance,
                vehicleSpeed_,
                vehicleForwardAccel_,
                vehicleSideAccel_,
                vehicleVerticalAccelNow_,
                vehicleTargetSide_,
                vehicleTargetVertical_,
                vehicleTargetForward_,
                vehicleSpring_.side.position,
                vehicleSpring_.vertical.position,
                vehicleSpring_.forward.position,
                vehicleCollisionSpring_.side.position,
                vehicleCollisionSpring_.vertical.position,
                vehicleCollisionSpring_.forward.position,
                vehicleCollisionKickSide_,
                vehicleCollisionKickVertical_,
                vehicleCollisionKickForward_,
                vehicleVibrationSide_,
                vehicleVibrationVertical_,
                vehicleVibrationForward_,
                vehicleHornActive_ ? 1 : 0,
                vehicleHornBlend_,
                vehicleHornSide_,
                vehicleHornVertical_,
                vehicleHornForward_,
                nearbyVehicleContributors_,
                nearbyVehicleNearestDistance_,
                nearbyVehicleStrongestActivity_,
                nearbyVehicleSide_,
                nearbyVehicleVertical_,
                nearbyVehicleForward_,
                impactSpring_.side.position,
                impactSpring_.vertical.position,
                impactSpring_.forward.position,
                pendingSide_,
                pendingVertical_,
                pendingForward_,
                vehicleImpacts_,
                vehicleContactImpacts_,
                vehicleDamageCrashes_,
                vehicleSuspensionImpacts_,
                vehicleHornFrames_,
                vehicleHealth,
                lastVehicleHealthLoss_,
                lastVehicleContactIntensity_,
                lastVehicleContactImpulse_,
                vehicleSuspensionJolt_,
                vehicleSuspensionHandlingStiffness_,
                vehicleSuspensionHandlingDamping_,
                vehicleSuspensionHandlingTravel_,
                vehicleSuspensionHandlingMass_,
                vehicleSuspensionHandlingBias_,
                vehicleSuspensionHandlingSpring_,
                vehicleSuspensionHandlingSpringDamping_
            );

            log_.Write(
                "VEHICLE SPECIAL advanced=%d boatEnabled=%d aircraftEnabled=%d "
                "water=%d boatS/V/F=(%.5f %.5f %.5f) "
                "aircraftS/V/F=(%.5f %.5f %.5f) advancedS/V/F=(%.5f %.5f %.5f)",
                cfg_.advancedResponseEnabled ? 1 : 0,
                cfg_.boatWavePhysicalEnabled ? 1 : 0,
                cfg_.aircraftPhysicalEnabled ? 1 : 0,
                lastVehicle_ && (lastVehicle_->bTouchingWater ||
                    lastVehicle_->bSubmergedInWater ||
                    (lastVehicle_->m_nVehicleSubClass == VEHICLE_BOAT &&
                     reinterpret_cast<CBoat*>(lastVehicle_)->m_nBoatFlags.bOnWater)) ? 1 : 0,
                boatWaveSide_, boatWaveVertical_, boatWaveForward_,
                aircraftResponseSide_, aircraftResponseVertical_, aircraftResponseForward_,
                advancedVehicleSide_, advancedVehicleVertical_, advancedVehicleForward_
            );

            log_.Write(
                "SOURCE STATUS dominant=%s magnitude=%.5f ageMs=%u active=%u "
                "stuckResets=%u externalDropped=%u duck=%.2f budgetSat=%u "
                "transientDrop=%u scripted=%d cutscene=%d",
                dominantSourceName,
                dominantSourceMagnitude,
                dominantSourceAge,
                sourceWatchdog_.ActiveCount(),
                stuckResetCount_,
                externalQueueDropped_,
                ambientDuck_,
                responseBudgetSaturations_,
                transientEventsDropped_,
                cameraContext_.missionScriptCamera ? 1 : 0,
                cameraContext_.cutscene ? 1 : 0
            );
            return;
        }

        log_.Write(
            "STATUS updates=%llu prepared=%llu commitCalls=%llu "
            "commitApplied=%llu rwFallbacks=%llu hook=%d "
            "gate=%s mode=%d state=%s move=%d aim=%d speed=%.4f "
            "yawRate=%.3f pitchRate=%.3f feet=(%.4f %.4f %.4f) "
            "steps=%u/%u shots=%u npcShots=%u nearVeh=%u nearDist=%.2f nearS/V/F=(%.4f %.4f %.4f) "
            "pending=(%.4f %.4f %.4f) saw=(%.2f %.2f %.4f) rotDeg=(%.3f %.3f %.3f) "
            "matrixBase=(%.3f %.3f %.3f) matrixForced=(%.3f %.3f %.3f) "
            "rwFrame=(%.3f %.3f %.3f)",
            static_cast<unsigned long long>(updates_),
            static_cast<unsigned long long>(prepared_),
            static_cast<unsigned long long>(commitCalls_),
            static_cast<unsigned long long>(commitApplied_),
            static_cast<unsigned long long>(rwFrameFallbacks_),
            cameraCommitHookInstalled_ ? 1 : 0,
            gate,
            int(cam.m_nMode),
            MotionName(motion_),
            p ? int(p->m_nMoveState) : -1,
            aiming ? 1 : 0,
            HorizontalSpeed(p),
            yawRate_,
            pitchRate_,
            leftHeight_,
            rightHeight_,
            footDiff_,
            leftSteps_,
            rightSteps_,
            shots_,
            nearbyGunfireShots_,
            nearbyVehicleContributors_,
            nearbyVehicleNearestDistance_,
            nearbyVehicleSide_,
            nearbyVehicleVertical_,
            nearbyVehicleForward_,
            pendingSide_,
            pendingVertical_,
            pendingForward_,
            chainsawActivityBlend_,
            chainsawContactBlend_,
            chainsawForwardBias_,
            lastPitchDeg_,
            lastRollDeg_,
            lastYawDeg_,
            lastMatrixBase_.x,
            lastMatrixBase_.y,
            lastMatrixBase_.z,
            lastMatrixForced_.x,
            lastMatrixForced_.y,
            lastMatrixForced_.z,
            lastRwFramePos_.x,
            lastRwFramePos_.y,
            lastRwFramePos_.z
        );

        log_.Write(
            "PLAYER AIR standing=%d airFlag=%d landing=%d trackedAir=%d worstVz=%.4f "
            "jetpack=%d enabled=%d jetpackS/V/F=(%.5f %.5f %.5f) impactV=%.5f",
            p && p->bIsStanding ? 1 : 0,
            p && p->bIsInTheAir ? 1 : 0,
            p && p->bIsLanding ? 1 : 0,
            airborneInit_ && wasAirborne_ ? 1 : 0,
            mostNegativeAirVz_, previousJetpack_ ? 1 : 0,
            cfg_.jetpackDirectionalEnabled ? 1 : 0,
            jetpackResponseSide_, jetpackResponseVertical_, jetpackResponseForward_,
            impactSpring_.vertical.position
        );

        log_.Write(
            "SOURCE STATUS dominant=%s magnitude=%.5f ageMs=%u active=%u "
            "stuckResets=%u externalDropped=%u duck=%.2f budgetSat=%u "
            "transientDrop=%u scripted=%d cutscene=%d",
            dominantSourceName,
            dominantSourceMagnitude,
            dominantSourceAge,
            sourceWatchdog_.ActiveCount(),
            stuckResetCount_,
            externalQueueDropped_,
            ambientDuck_,
            responseBudgetSaturations_,
            transientEventsDropped_,
            cameraContext_.missionScriptCamera ? 1 : 0,
            cameraContext_.cutscene ? 1 : 0
        );
    }

    void UpdateRuntimeReload() {
        AuditHit(AUD_UpdateRuntimeReload);
        if (!cfg_.runtimeReloadEnabled) {
            reloadKeyWasDown_ = false;
            return;
        }

        const SHORT state = GetAsyncKeyState(cfg_.runtimeReloadKey);
        const bool down = (state & 0x8000) != 0;
        const unsigned now = CTimer::m_snTimeInMilliseconds;

        if (down && !reloadKeyWasDown_ &&
            (lastRuntimeReloadMs_ == 0 ||
             now - lastRuntimeReloadMs_ >= cfg_.runtimeReloadDebounceMs)) {
            const bool wasLogging =
                cfg_.debug || cfg_.auditEnabled || cfg_.scanProfilingEnabled;
            if (!cr::ReloadConfigFresh(cfg_)) {
                log_.Write("CONFIG RELOAD SKIPPED: failed to load a valid path");
                reloadKeyWasDown_ = down;
                return;
            }
            scanProfiler_.Reset();
            lastScanProfileMs_ = now;
            lastRuntimeReloadMs_ = now;

            const bool wantsLogging =
                cfg_.debug || cfg_.auditEnabled || cfg_.scanProfilingEnabled;
            if (wasLogging != wantsLogging)
                log_.Init(cfg_.dir, wantsLogging);

            cfg_.DumpEffectiveValues(log_);
            RemoveFovOutput();
            fovWsfModule_ = nullptr;
            wsfGetCurrentFov_ = nullptr;
            wsfSetFovMultiplier_ = nullptr;
            wsfRemoveFovMultiplier_ = nullptr;
            lastFovBackendProbeMs_ = 0;

            log_.Write(
                "CONFIG RELOADED key=%d preset=%s strength=%.3f rotation=%d bands=%d",
                cfg_.runtimeReloadKey,
                cfg_.presetName,
                cfg_.strength,
                cfg_.rotationEnabled ? 1 : 0,
                cfg_.frequencyBandsEnabled ? 1 : 0
            );
            log_.Flush();
        }

        reloadKeyWasDown_ = down;
    }

    void RenderDebugOverlay() {
        AuditHit(AUD_RenderDebugOverlay);
        if (!cfg_.overlayEnabled)
            return;

        CPlayerPed* p = FindPlayerPed();
        if (!p)
            return;

        char line1[256] = {};
        char line2[256] = {};
        char line3[256] = {};
        char line4[256] = {};

        std::snprintf(line1, sizeof(line1),
            "GTASA: Camera Response v0.9 | Created by Zeneric | Inspired by MachineGames  preset=%s  state=%s  hook=%d",
            cfg_.presetName, vehicleInit_ ? "VEHICLE" : MotionName(motion_),
            cameraCommitHookInstalled_ ? 1 : 0);
        std::snprintf(line2, sizeof(line2),
            "offset S/V/F %.3f %.3f %.3f  surface=%u x%.2f nearVeh=%u",
            pendingSide_, pendingVertical_, pendingForward_, currentSurfaceId_, currentSurfaceMultiplier_,
            nearbyVehicleContributors_);
        if (vehicleInit_) {
            std::snprintf(line3, sizeof(line3),
                "veh spd=%.3f aF/S/Z %.2f %.2f %.2f tgt S/V/F %.3f %.3f %.3f hit=%u dmg=%u susp=%u horn=%d gate=%s",
                vehicleSpeed_, vehicleForwardAccel_, vehicleSideAccel_,
                vehicleVerticalAccelNow_, vehicleTargetSide_, vehicleTargetVertical_,
                vehicleTargetForward_, vehicleImpacts_, vehicleDamageCrashes_,
                vehicleSuspensionImpacts_, vehicleHornActive_ ? 1 : 0,
                lastGate_.empty() ? "?" : lastGate_.c_str());
        } else {
            std::snprintf(line3, sizeof(line3),
                "steps %u/%u  shots=%u streak=%u melee=%u  gate=%s",
                leftSteps_, rightSteps_, shots_, shotStreak_, meleeContacts_,
                lastGate_.empty() ? "?" : lastGate_.c_str());
        }

        if (cfg_.detailedDebugHud) {
            const cr::MixTelemetry& telemetry = responseMixer_.Telemetry();
            std::snprintf(
                line4,
                sizeof(line4),
                "mix events=%u drop=%u band L/M/H %.3f/%.3f/%.3f rot P/R/Y %.2f/%.2f/%.2f",
                telemetry.accepted,
                telemetry.dropped,
                std::fabs(telemetry.band[(unsigned)cr::ResponseBand::Low].vertical),
                std::fabs(telemetry.band[(unsigned)cr::ResponseBand::Mid].vertical),
                std::fabs(telemetry.band[(unsigned)cr::ResponseBand::High].vertical),
                lastPitchDeg_,
                lastRollDeg_,
                lastYawDeg_
            );
        }

        CFont::SetOrientation(ALIGN_LEFT);
        CFont::SetColor(CRGBA(255, 255, 255, 220));
        CFont::SetDropShadowPosition(1);
        CFont::SetBackground(false, false);
        CFont::SetWrapx(620.0f);
        CFont::SetScale(0.32f, 0.65f);
        CFont::SetFontStyle(FONT_SUBTITLES);
        CFont::SetProportional(true);
        CFont::PrintString(18.0f, 24.0f, line1);
        CFont::PrintString(18.0f, 40.0f, line2);
        CFont::PrintString(18.0f, 56.0f, line3);
        if (cfg_.detailedDebugHud)
            CFont::PrintString(18.0f, 72.0f, line4);
    }

    void GameUpdate() {
        AuditHit(AUD_GameUpdate);
        ++updates_;
        VerifyCameraCommitHook();
        const float dt = Dt();
        UpdateRuntimeReload();
        AuditTick();
        ReportScanProfiles();

        CPlayerPed* p = FindPlayerPed();
        if (!p) {
            pendingAllowed_ = false;
            pendingSide_ = pendingVertical_ = pendingForward_ = 0.0f;
            pendingPitchRad_ = pendingRollRad_ = pendingYawRad_ = 0.0f;
            lastPitchDeg_ = lastRollDeg_ = lastYawDeg_ = 0.0f;
            fovMultiplier_ = 1.0f;
            fovKick_ = 0.0f;
            RemoveFovOutput();
            return;
        }

        CCam& cam =
            TheCamera.m_aCams[TheCamera.m_nActiveCam];

        UpdateCameraBasisCache(cam);

        CPad* pad = p->GetPadFromPlayer();
        const bool aiming =
            (pad && pad->GetTarget()) ||
            p->bIsAimingGun;

        const bool meleeLockOn =
            IsControllerMeleeLockOn(
                p,
                pad,
                aiming
            );

        cameraContext_=BuildCameraContext(
            p,cam,aiming,meleeLockOn
        );
        UpdateContextAttenuation(p,cameraContext_);
        BeginTransientFrame();

        const char* gate=
            Gate(p,cam,cameraContext_);
        const bool vehicleGate =
            std::strcmp(gate, "OK-VEHICLE") == 0;

        const bool onFootGate =
            std::strcmp(gate, "OK") == 0;

        if (vehicleGate) {
            CVehicle* vehicle = p->m_pVehicle;
            if (!vehicle)
                vehicle = FindPlayerVehicle(-1, false);

            // Vehicle cameras own their own spring state. Clear pedestrian
            // movement targets so entering/exiting a vehicle cannot inherit a
            // stale footstep or look impulse.
            motion_ = MotionState::Disabled;
            gaitTargetSide_ = gaitTargetVertical_ = gaitTargetForward_ = 0.0f;
            lookTargetSide_ = lookTargetVertical_ = lookTargetForward_ = 0.0f;
            stationaryTime_ = 0.0f;
            leftFoot_.Reset();
            rightFoot_.Reset();
            camAngleInit_ = false;
            speedInit_ = false;
            airborneInit_ = false;

            if (!vehicleInit_ || lastVehicle_ != vehicle) {
                weaponInit_ = false;
                gaitSpring_.Reset();
                lookSpring_.Reset();
                recoilSpring_.Reset();
                currentRecoilRecovery_ = 1.0f;
            }

            BeginEnvironmentFrame();
            DrainExternalImpulseQueue();
            UpdatePlayerVehicleTransition(p, true, vehicle);
            UpdateDamage(p);
            UpdateExplosions(p);
            UpdateNearbyGunfire(p, true);
            UpdateNearbyVehicles(p, true, vehicle, dt);
            UpdateDoorResponses(p, true, vehicle);
            UpdateWaterResponse(p, vehicle, true);
            UpdateWeatherResponse(p, dt);
            UpdateNativeCameraShake();
            ProcessPendingExplosionShocks();
            UpdateLargeVehicleProximity(p);
            UpdateWorldObjectImpacts(p);
            UpdatePedWorldResponses(p);
            UpdateCameraObstructionResponse(p, vehicle, dt);
            CommitEnvironmentFrame();

            // Player weapon/melee response remains active in both stock
            UpdateWeapon(p, pad);
            CommitTransientEvents();

            recoilSpring_.Update(
            dt,
            cfg_.spring *
                cfg_.recoilSpringMultiplier *
                currentRecoilRecovery_,
            cfg_.damping *
                cfg_.recoilDampingMultiplier *
                cfg_.presetRecovery *
                currentRecoilRecovery_
        );

            recoilSpring_.vertical.position =
                Clamp(recoilSpring_.vertical.position, -0.08f, 0.18f);
            recoilSpring_.forward.position =
                Clamp(recoilSpring_.forward.position, -0.10f, 0.05f);
            recoilSpring_.side.position =
                Clamp(recoilSpring_.side.position, -0.06f, 0.06f);

            // Keep common blast/damage/NPC-gunfire/melee reactions alive while driving.
            impactSpring_.Update(
                dt,
                cfg_.spring * 0.82f,
                cfg_.damping * 0.82f * cfg_.presetRecovery
            );
            UpdateVehicle(vehicle, dt);
            UpdateMountedWeapons(p, vehicle, dt);

            // UpdateVehicle can publish jerk/crash transients, so consume the
            // second batch before final camera composition.
            CommitTransientEvents();
            UpdateAmbientDucking(dt);

            directionalRotationSpring_.Update(
                dt,
                cfg_.directionalRotationSpring,
                cfg_.directionalRotationDamping*cfg_.presetRecovery
            );

            // Match the on-foot path: release expired sources and stuck tails.
            UpdateResponseWatchdog();
            PrepareVehicleOffset(vehicle, dt);
            UpdateFovResponse(p, cam, aiming, true, true, dt);
            LogStatus(p, cam, gate, false);
            return;
        }

        if (!onFootGate) {
            motion_ = MotionState::Disabled;
            UpdateStrength(dt, 0.0f);

            gaitTargetSide_ = 0.0f;
            gaitTargetVertical_ = 0.0f;
            gaitTargetForward_ = 0.0f;
            lookTargetSide_ = 0.0f;
            lookTargetVertical_ = 0.0f;
            lookTargetForward_ = 0.0f;

            UpdateSprings(dt);
            StabilizeTransientState();

            pendingAllowed_ = false;
            // Never leave the last valid camera transform in diagnostics/state
            // while output is gated. It was not being applied, but the stale
            // values made the log look as though a disabled response was still
            // active and could leak through external API readers.
            pendingSide_ = 0.0f;
            pendingVertical_ = 0.0f;
            pendingForward_ = 0.0f;
            pendingPitchRad_ = 0.0f;
            pendingRollRad_ = 0.0f;
            pendingYawRad_ = 0.0f;
            obstructionSpring_.Reset();
            obstructionInit_ = false;
            camAngleInit_ = false;
            speedInit_ = false;
            weaponInit_ = false;
            airborneInit_ = false;
            vitalsInit_ = false;
            stationaryTime_ = 0.0f;
            playerDerivativeInit_=false;
            previousPlayerAcceleration_={};
            prevWorldVelocity_={};
            transientEvents_.BeginFrame();
            ambientDuckRequested_=1.0f;
            prevMotion_ = MotionState::Disabled;
            leftFoot_.Reset();
            rightFoot_.Reset();
            if (vehicleInit_)
                ResetVehicleState();

            UpdateFovResponse(p, cam, aiming, false, false, dt);
            UpdateResponseWatchdog();
            LogStatus(p, cam, gate, aiming);
            return;
        }

        if (vehicleInit_)
            ResetVehicleState();

        motion_ = DetectMotion(p, aiming);
        if (motion_ != prevMotion_)
            TriggerTransition(prevMotion_, motion_);
        prevMotion_ = motion_;

        const float horizontalSpeed = HorizontalSpeed(p);
        const bool stationary =
            horizontalSpeed <= cfg_.movementDeadzone &&
            motion_ != MotionState::Airborne;

        stationaryTime_ =
            stationary ? (stationaryTime_ + dt) : 0.0f;

        UpdateStrength(dt, StateStrength(motion_) * (cfg_.locomotionResponseScale * cfg_.masterMovementScale * cfg_.presetMovement));

        UpdatePlayerJerk(p,dt);
        UpdateAirAndLanding(p);
        BeginEnvironmentFrame();
        DrainExternalImpulseQueue();
        UpdatePlayerVehicleTransition(p, false, nullptr);
        UpdateDamage(p);
        UpdateExplosions(p);
        UpdateNearbyGunfire(p, false);
        UpdateNearbyVehicles(p, false, nullptr, dt);
        UpdateDoorResponses(p, false, nullptr);
        UpdateWaterResponse(p, nullptr, false);
        UpdateAdvancedPlayerActions(p, dt);
        UpdateWeatherResponse(p, dt);
        UpdateNativeCameraShake();
        ProcessPendingExplosionShocks();
        UpdateLargeVehicleProximity(p);
        UpdateWorldObjectImpacts(p);
        UpdatePedWorldResponses(p);
        UpdateCameraObstructionResponse(p, nullptr, dt);
        CommitEnvironmentFrame();
        UpdateSurface(p);
        UpdateFeet(p, dt);
        ApplyDirectionalMovement(p);
        UpdateLook(p, pad, cam, dt);
        UpdateWeapon(p, pad);
        CommitTransientEvents();
        UpdateAmbientDucking(dt);
        UpdateSprings(dt);
        PrepareOffset(aiming, dt);
        UpdateFovResponse(p, cam, aiming, false, true, dt);
        UpdateResponseWatchdog();

        LogStatus(p, cam, gate, aiming);
    }

#include "CameraResponseCameraIntegration.inc"

    void Reset(const char* why) {
        log_.Write("RESET %s", why ? why : "?");

        ResetPersistentResponseState(why ? why : "reset");

        motion_ = MotionState::Disabled;

        leftFoot_.Reset();
        rightFoot_.Reset();

        gaitSpring_.Reset();
        lookSpring_.Reset();
        recoilSpring_.Reset();
        impactSpring_.Reset();
        ResetVehicleState();
        lastVehicleImpactMs_ = 0;
        vehicleImpacts_ = 0;
        RemoveFovOutput();
        fovMultiplier_ = 1.0f;
        fovKick_ = 0.0f;
        fovLastReportedBase_ = 0.0f;
        lastFovDebugMs_ = 0;

        gaitTargetSide_ = 0.0f;
        gaitTargetVertical_ = 0.0f;
        gaitTargetForward_ = 0.0f;
        lookTargetSide_ = 0.0f;
        lookTargetVertical_ = 0.0f;
        lookTargetForward_ = 0.0f;

        strengthNow_ = 0.0f;
        idleTime_ = 0.0f;
        stationaryTime_ = 0.0f;
        gaitPhase_ = 0.0f;
        rawFootPhase_ = 0.0f;
        smoothedFootPhase_ = 0.0f;

        camAngleInit_ = false;
        speedInit_ = false;
        airborneInit_ = false;
        wasAirborne_ = false;
        airborneStartedMs_ = 0;
        mostNegativeAirVz_ = 0.0f;
        vitalsInit_ = false;
        prevHealth_ = 0.0f;
        prevArmour_ = 0.0f;
        lastConfirmedPlayerDamageMs_ = 0;
        prevMotion_ = MotionState::Disabled;
        std::memset(lastExplosionCreated_, 0, sizeof(lastExplosionCreated_));
        npcShotTrack_.clear();
        nearbyGunfireShots_ = 0;
        lastNearbyGunfireScanMs_ = 0;
        lastNearbyGunfireDistance_ = 0.0f;
        lastNearbyGunfireStrength_ = 0.0f;
        weaponInit_ = false;
        prevWeapon_ = static_cast<eWeaponType>(0);
        prevAmmo_ = 0;
        prevAmmoTotal_ = 0;
        prevNextShotMs_ = 0;
        prevWeaponState_ = WEAPONSTATE_READY;
        previousMeleeFireInput_ = false;
        lastRecoilMs_ = 0;
        lastWeaponShotMs_ = 0;
        shotStreak_ = 0;
        meleeContacts_ = 0;
        currentRecoilRecovery_ = 1.0f;
        currentSurfaceId_ = 0;
        currentSurfaceMultiplier_ = 1.0f;
        lastSurfaceScanMs_ = 0;
        lastNearbyVehicleScanMs_ = 0;
        lastDoorScanMs_ = 0;
        vehicleDoorTrack_.clear();
        objectDoorTrack_.clear();
        worldObjectTrack_.clear();
        pedWorldTrack_.clear();
        lastWorldObjectScanMs_ = 0;
        lastPedWorldScanMs_ = 0;
        lastLargeVehicleScanMs_ = 0;
        largeVehicleSide_ = largeVehicleVertical_ = largeVehicleForward_ = 0.0f;
        weatherSide_ = weatherVertical_ = weatherForward_ = 0.0f;
        obstructionSpring_.Reset();
        explosionRotationSpring_.Reset();
        directionalRotationSpring_.Reset();
        transientEvents_.BeginFrame();
        ambientDuck_=1.0f;
        ambientDuckRequested_=1.0f;
        ambientDuckHoldUntilMs_=0;
        obstructionInit_ = false;
        contextScale_=1.0f;
        contextIndoors_=false;
        lastContextIndoorScanMs_=0;
        previousCameraTargetDistance_ = 0.0f;
        observedPlayerVehicle_ = nullptr;
        observedPlayerInVehicle_ = false;
        vehicleBailCandidate_ = false;
        vehicleBailActive_ = false;
        vehicleBailCandidateUntilMs_ = 0;
        vehicleBailStartedMs_ = 0;
        vehicleBailExitSpeed_ = 0.0f;
        vehicleBailExitVelocity_ = {};
        vehicleBailLandingWorstVz_ = 0.0f;
        previousPlayerTouchingWater_ = false;
        advancedPlayerSide_ = advancedPlayerVertical_ = advancedPlayerForward_ = 0.0f;
        nativeShakeSide_ = nativeShakeVertical_ = nativeShakeForward_ = 0.0f;
        previousSwimming_ = false;
        previousClimbing_ = false;
        previousParachute_ = false;
        previousJetpack_ = false;
        jetpackMotionInit_ = false;
        previousJetpackVelocity_ = {};
        jetpackResponseSide_ = 0.0f;
        jetpackResponseVertical_ = 0.0f;
        jetpackResponseForward_ = 0.0f;
        for (auto& shock : pendingShocks_)
            shock.active = false;
        environmentEventCount_ = 0;
        responseMixer_.BeginFrame();
        mixedEventOffset_ = {};
        idleNoiseSide_.Reset(0x13579BDFu);
        idleNoiseVertical_.Reset(0x2468ACE1u);
        idleNoiseForward_.Reset(0xA51C39E7u);
        vehicleNoise_.Reset(0x7F4A7C15u);
        weatherWindNoise_.Reset(0xBC31D9A3u);
        weatherRainNoise_.Reset(0x7A11C4E5u);
        weatherSandNoise_.Reset(0xD24F831Bu);
        weatherQuakeNoise_.Reset(0x8E17A62Du);
        aircraftTurbulenceNoise_.Reset(0x4A17C9D3u);
        aircraftEngineNoise_.Reset(0x671E2B95u);
        weatherWindBlend_ = 0.0f;
        weatherRainBlend_ = 0.0f;
        weatherSandBlend_ = 0.0f;
        weatherQuakeBlend_ = 0.0f;
        previousTractionLoss_ = false;
        wheelCompressionInit_ = false;
        for (unsigned i = 0; i < 4; ++i)
            previousWheelCompression_[i] = 1.0f;
        lastWeaponFightAnim_ = nullptr;
        lastWeaponFightMove_ = -1;
        lastWeaponFightPhase_ = 0.0f;
        weaponFightReleaseTriggered_ = false;
        lastMeleeWeaponSwingMs_ = 0;
        lastMeleeWeaponNextShot_ = 0;
        lastMeleeWeaponState_ = WEAPONSTATE_READY;
        chainsawActivityBlend_ = 0.0f;
        chainsawContactBlend_ = 0.0f;
        chainsawForwardBias_ = 0.0f;
        chainsawContactUntilMs_ = 0;
        lastChainsawMode_ = -1;
        chainsawNoise_.Reset(0x51A7B22Du);
        continuousToolBlend_ = 0.0f;
        lastContinuousToolId_ = -1;
        continuousToolNoise_.Reset(0xC0717A11u);

        pendingAllowed_ = false;
        pendingSide_ = 0.0f;
        pendingVertical_ = 0.0f;
        pendingForward_ = 0.0f;
        pendingPitchRad_ = 0.0f;
        pendingRollRad_ = 0.0f;
        pendingYawRad_ = 0.0f;
        lastPitchDeg_ = 0.0f;
        lastRollDeg_ = 0.0f;
        lastYawDeg_ = 0.0f;

        lastCameraHookCheckMs_ = 0;
        lastGate_.clear();
    }
};

CameraResponse* CameraResponse::instance_ = nullptr;
CameraResponse::CopyCameraFn CameraResponse::originalCopyCamera_ = nullptr;
static CameraResponse gCameraResponseInstance;
static CameraResponse* gCameraResponse = &gCameraResponseInstance;

} // namespace

CAMERA_RESPONSE_API unsigned CameraResponse_GetApiVersion() {
    return 2u;
}

CAMERA_RESPONSE_API int CameraResponse_GetOffset(
    float* side,
    float* vertical,
    float* forward
) {
    return gCameraResponse &&
           gCameraResponse->GetCurrentOffset(side, vertical, forward) ? 1 : 0;
}

CAMERA_RESPONSE_API int CameraResponse_GetRotation(
    float* pitchDeg,
    float* rollDeg,
    float* yawDeg
) {
    return gCameraResponse &&
           gCameraResponse->GetCurrentRotation(pitchDeg, rollDeg, yawDeg) ? 1 : 0;
}

CAMERA_RESPONSE_API void CameraResponse_SetExternalScale(float scale) {
    if (gCameraResponse)
        gCameraResponse->SetExternalScale(scale);
}

CAMERA_RESPONSE_API void CameraResponse_AddImpulse(
    float side,
    float vertical,
    float forward,
    float priority,
    int frequencyBand
) {
    if (gCameraResponse) {
        gCameraResponse->QueueExternalImpulse(
            side,
            vertical,
            forward,
            priority,
            frequencyBand,
            0u,
            600u
        );
    }
}

CAMERA_RESPONSE_API int CameraResponse_QueueImpulseV2(
    float side,
    float vertical,
    float forward,
    float priority,
    int frequencyBand,
    unsigned sourceTag,
    unsigned ttlMs
) {
    return gCameraResponse &&
           gCameraResponse->QueueExternalImpulse(
               side,
               vertical,
               forward,
               priority,
               frequencyBand,
               sourceTag,
               ttlMs
           ) ? 1 : 0;
}

CAMERA_RESPONSE_API void CameraResponse_ClearExternalQueue() {
    if (gCameraResponse)
        gCameraResponse->ClearExternalQueue();
}
