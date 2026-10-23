// Camera Response v1.0 | Zeneric | Inspired by MachineGames
// GTA San Andreas Classic US 1.0 (x86); plugin-sdk baseline a1df3a6.
// Camera hook: CALL 0x52C98B -> CopyCameraMatrixToRWCam; restore GTA's matrix after copying.
// Translation by default; optional validated rotation modifies RenderWare only.
// Startup log beside the ASI (or in %TEMP% if the ASI folder is read-only).

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
#include "CMenuManager.h"
#include "CWorld.h"
#include "CWeather.h"
#include "CColPoint.h"
#include "CFont.h"
#include "CRGBA.h"
#include "common.h"
#include "injector.hpp"
#include "ResponseCore.h"
#include "UpdateDispatch.h"
#include "OverlayLayout.h"
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

    // Re-orthonormalize to prevent rotation-induced scale/shear drift.
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
        // Bounded semi-implicit substeps stabilize springs across frame-time spikes.
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

    void Init(const std::string& dir, bool verbose) {
        Close();
        enabled_ = verbose && !dir.empty();
        path_ = dir.empty() ? std::string() : dir + "CameraResponse.log";

        if (path_.empty())
            return;

        // A minimal startup log is always written, even when verbose logs are off.
        Open("w");
        if (!file_) {
            char tempDir[MAX_PATH + 1] = {};
            const DWORD length = GetTempPathA(MAX_PATH, tempDir);
            if (length > 0 && length < MAX_PATH) {
                path_ = std::string(tempDir) + "CameraResponse.log";
                Open("w");
            }
        }

        if (!file_) {
            OutputDebugStringA("CameraResponse: unable to open startup log in ASI folder or TEMP.\n");
            return;
        }

        std::fprintf(
            file_,
            "GTASA: Camera Response | startup diagnostic\r\n"
            "------------------------------------------------------------\r\n"
        );
        std::fflush(file_);
        lastFlushTick_ = GetTickCount();
    }

    bool Enabled() const {
        return enabled_;
    }

    const std::string& Path() const {
        return path_;
    }

    void Flush() {
        if (file_)
            std::fflush(file_);
    }

    // Normal logging remains opt-in to avoid per-frame log overhead.
    void Write(const char* fmt, ...) {
        if (!enabled_)
            return;
        va_list ap;
        va_start(ap, fmt);
        WriteV(fmt, ap);
        va_end(ap);
    }

    // Small startup/failure records are written without requiring Debug.Enabled=1.
    void WriteImportant(const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        WriteV(fmt, ap);
        va_end(ap);
        Flush();
    }

private:
    void WriteV(const char* fmt, va_list ap) {
        if (!file_)
            Open("a");
        if (!file_)
            return;

        std::fprintf(file_, "[%10lu] ", static_cast<unsigned long>(GetTickCount()));
        std::vfprintf(file_, fmt, ap);
        std::fprintf(file_, "\r\n");

        const DWORD now = GetTickCount();
        if (now - lastFlushTick_ >= 1000u) {
            std::fflush(file_);
            lastFlushTick_ = now;
        }
    }

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


// Use an address inside this ASI to locate its path, independent of modloader/GTA paths.
static void CameraResponseModuleAnchor() {
}

struct Config {
    bool enabled = true;
    float strength = 1.0f;
    float presetScale = 1.0f;

    float masterOverallScale = 1.0f;
    float masterMovementScale = 1.0f;
    float masterCombatScale = 1.0f;
    float masterVehicleScale = 1.0f;
    float masterWorldScale = 1.0f;
    float masterFovScale = 1.0f;

    bool contextAttenuationEnabled = true;
    float contextIndoorScale = 0.82f;
    float contextAimScale = 0.78f;
    float contextCrouchScale = 0.88f;
    float contextLowHealthScale = 0.82f;
    float contextLowHealthThreshold = 35.0f;
    float contextScriptedScale = 0.0f;
    unsigned contextIndoorScanMs = 250;
    float contextIndoorCeilingHeight = 8.0f;

    bool directionalResponseEnabled = true;
    float directionalSourceSide = 1.00f;
    float directionalSourceForward = 1.00f;
    float directionalSourceVertical = 0.30f;
    float directionalDoorScale = 1.00f;
    float directionalWaterScale = 0.80f;
    float directionalVehicleTransitionScale = 0.70f;
    float directionalSwimScale = 0.55f;
    float directionalParachuteScale = 0.80f;
    float directionalLargeVehicleScale = 0.90f;

    bool responseBudgetEnabled = true;
    float responseBudgetNormalized = 1.12f;
    float responseBudgetCriticalReserve = 0.22f;
    float responseBudgetSoftness = 0.65f;
    bool ambientDuckingEnabled = true;
    float ambientDuckAttack = 18.0f;
    float ambientDuckRelease = 5.5f;
    float ambientDuckMinimum = 0.28f;
    unsigned ambientDuckHoldMs = 260;

    bool directionalRotationEnabled = true;
    float directionalRotationRollDeg = 0.75f;
    float directionalRotationPitchDeg = 0.55f;
    float directionalRotationYawDeg = 0.18f;
    float directionalRotationSpring = 38.0f;
    float directionalRotationDamping = 12.5f;

    bool jerkResponseEnabled = true;
    float playerJerkStrength = 0.0065f;
    float vehicleJerkStrength = 0.0100f;
    float jerkDeadzone = 4.5f;
    float jerkMax = 22.0f;

    float nearbyApproachInfluence = 0.70f;
    float nearbyPassInfluence = 0.55f;
    float rotorWashDistance = 24.0f;
    float rotorWashHeight = 18.0f;
    float rotorWashStrength = 0.040f;

    float explosionPressureIndoorScale = 0.72f;
    float explosionPressureBelowBoost = 0.45f;
    float explosionPressureAboveScale = 0.72f;
    float explosionPressureLateralRoll = 0.65f;

    float footstepSurfaceBandStrength = 0.024f;

    bool sourceWatchdogEnabled = true;
    unsigned sourceReleaseMs = 650;
    unsigned sourceContinuousMaxMs = 3500;
    unsigned globalTailMs = 3200;
    float stuckSpringEpsilon = 0.003f;
    char presetName[32] = "Default";
    bool comfortPreset = false;

    // Presets multiply user gains rather than overriding per-feature tuning.
    float presetMovement = 1.0f;
    float presetCombat = 1.0f;
    float presetVehicle = 1.0f;
    float presetWorld = 1.0f;
    float presetFov = 1.0f;
    float presetAmbient = 1.0f;
    float presetImpact = 1.0f;
    float presetRecoil = 1.0f;
    float presetMelee = 1.0f;
    float presetLanding = 1.0f;
    float presetRotation = 1.0f;
    float presetRecovery = 1.0f;
    float idleScale = 1.0f;
    float walkScale = 1.0f;
    float runScale = 1.15f;
    float sprintScale = 1.30f;
    float aimScale = 0.38f;
    float crouchScale = 0.72f;
    float airborneScale = 0.30f;

    // Ignore near-zero velocity noise while visually stationary.
    float movementDeadzone = 0.012f;

    bool locomotionEnabled = true;
    bool footstepsEnabled = true;
    bool inertiaEnabled = true;
    bool jumpLandEnabled = true;
    bool damageEnabled = true;
    bool explosionsEnabled = true;

    // NPC shots produce distance-scaled impulses, separate from player recoil.
    bool nearbyGunfireEnabled = true;
    bool nearbyGunfireInVehicle = true;
    float nearbyGunfireStrength = 0.22f;
    float nearbyGunfireMinDistance = 2.0f;
    float nearbyGunfireMaxDistance = 45.0f;
    float nearbyGunfireFalloffPower = 1.35f;
    float nearbyGunfireSide = 0.55f;
    float nearbyGunfireVertical = 1.0f;
    float nearbyGunfireForward = 0.35f;
    unsigned nearbyGunfireMaxResponseShotsPerFrame = 4;
    unsigned nearbyGunfireScanMs = 12;

    // Nearby vehicles add subtle motion; exclude the player's own vehicle.
    bool nearbyVehiclesEnabled = true;
    bool nearbyVehiclesOnFoot = true;
    bool nearbyVehiclesInVehicle = true;
    float nearbyVehiclesStrength = 0.004f;
    float nearbyVehiclesMinDistance = 2.5f;
    float nearbyVehiclesMaxDistance = 38.0f;
    float nearbyVehiclesFalloffPower = 1.45f;
    float nearbyVehiclesIdleScale = 0.30f;
    float nearbyVehiclesLowSpeedScale = 0.55f;
    float nearbyVehiclesHighSpeedScale = 1.00f;
    float nearbyVehiclesAccelerationScale = 0.45f;
    float nearbyVehiclesVertical = 1.00f;
    float nearbyVehiclesSide = 0.42f;
    float nearbyVehiclesForward = 0.28f;
    float nearbyVehiclesMaxCombined = 0.030f;
    unsigned nearbyVehiclesMaxContributors = 12;

    bool doorResponseEnabled = true;
    bool vehicleDoorResponseEnabled = true;
    bool buildingDoorResponseEnabled = true;
    bool doorResponseInVehicle = true;
    float doorResponseStrength = 0.145f;
    float vehicleDoorStrength = 1.00f;
    float buildingDoorStrength = 0.85f;
    float doorResponseMinDistance = 0.35f;
    float doorResponseMaxDistance = 22.0f;
    float doorResponseFalloffPower = 1.25f;
    float doorResponseSide = 0.42f;
    float doorResponseVertical = 1.00f;
    float doorResponseForward = 0.62f;
    float doorVehicleOwnScale = 1.20f;
    float vehicleDoorCloseThreshold = 0.020f;
    float vehicleDoorWasOpenThreshold = 0.120f;
    unsigned doorScanMs = 25;
    float doorBonnetScale = 1.35f;
    float doorBootScale = 1.45f;

    bool vehicleMechanicalEnabled = true;
    float vehicleEnterStrength = 0.34f;
    float vehicleExitStrength = 0.26f;
    bool vehicleBailOutEnabled = true;
    float vehicleBailOutMinSpeed = 0.20f;
    float vehicleBailOutStrength = 0.62f;
    unsigned vehicleBailAirborneWindowMs = 500;
    float vehicleBailLandingScale = 1.45f;
    float vehicleBailFovKick = -0.018f;
    float engineStartStrength = 0.32f;
    float engineStopStrength = 0.18f;
    float gearShiftStrength = 0.20f;
    float engineRpmVibration = 0.010f;
    float engineRpmMinHz = 5.5f;
    float engineRpmMaxHz = 15.5f;
    float engineLoadInfluence = 0.55f;
    float bikeEngineScale = 1.45f;
    float gearShiftCarScale = 1.00f;
    float gearShiftBikeScale = 1.35f;
    float gearShiftPulseRecovery = 0.70f;
    float tyreBlowoutStrength = 0.72f;
    float axleLandingStrength = 0.82f;

    bool waterResponseEnabled = true;
    float waterEntryStrength = 0.48f;

    bool largeVehicleResponseEnabled = true;
    float trainResponseStrength = 0.022f;
    float trainResponseDistance = 48.0f;
    float aircraftResponseStrength = 0.016f;
    float aircraftResponseDistance = 42.0f;
    unsigned aircraftMaxContinuousMs = 2600;
    unsigned aircraftRearmMs = 1200;
    unsigned largeVehicleScanMs = 80;

    bool worldObjectResponseEnabled = true;
    float worldObjectImpactStrength = 0.22f;
    float worldObjectImpactDistance = 28.0f;
    unsigned worldObjectScanMs = 67;
    unsigned worldObjectContactReleaseMs = 320;
    unsigned worldObjectImpactCooldownMs = 360;
    float worldObjectReimpactDeltaV = 0.080f;
    float worldObjectReimpactDamageRise = 1.50f;

    bool pedWorldResponseEnabled = true;
    float pedImpactStrength = 0.18f;
    float heavyFootstepStrength = 0.030f;
    float heavyFootstepDistance = 8.0f;
    unsigned pedWorldScanMs = 50;

    bool weatherResponseEnabled = true;
    float windResponseStrength = 0.010f;
    float rainResponseStrength = 0.006f;
    float sandstormResponseStrength = 0.016f;
    float earthquakeResponseStrength = 0.095f;

    float weatherSmoothing = 2.80f;
    float windGustStrength = 0.42f;
    float windGustRate = 0.85f;
    float rainTextureRate = 13.0f;
    float rainVerticalBias = 0.72f;
    float sandstormLowFrequency = 0.70f;
    float earthquakeMaxOffset = 0.070f;
    float weatherMaxAmbient = 0.075f;

    bool obstructionResponseEnabled = true;
    float obstructionThreshold = 0.055f;
    float obstructionStrength = 0.70f;
    float obstructionSpring = 34.0f;
    float obstructionDamping = 11.0f;

    bool environmentPriorityMixEnabled = true;
    float environmentMixMaxImpulse = 1.25f;

    bool vehiclePersonalityEnabled = true;
    float collisionMaterialInfluence = 0.45f;

    bool advancedResponseEnabled = true;
    float reloadResponseStrength = 0.18f;
    float equipResponseStrength = 0.13f;
    bool nearMissEnabled = true;
    float nearMissStrength = 0.16f;
    float nearMissDistance = 2.25f;
    float nearMissMaxShooterDistance = 55.0f;

    float swimResponseStrength = 0.060f;
    float climbResponseStrength = 0.22f;
    float parachuteDeployStrength = 0.55f;
    float parachuteAirStrength = 0.020f;
    float parachuteLandStrength = 0.42f;
    float jetpackStrength = 0.036f;
    float jetpackTransitionStrength = 0.24f;
    float jetpackRecoilScale = 2.20f;
    bool mountedWeaponResponseEnabled = true;
    float mountedGunStrength = 0.045f;
    float mountedHeavyStrength = 0.100f;
    float mountedWaterStrength = 0.022f;

    bool jetpackDirectionalEnabled = true;
    float jetpackDirectionalStrength = 0.160f;
    float jetpackForwardScale = 1.00f;
    float jetpackBackwardScale = 0.82f;
    float jetpackSideScale = 0.92f;
    float jetpackVerticalScale = 0.72f;
    float jetpackAccelInfluence = 1.00f;
    float jetpackVelocityInfluence = 0.34f;
    float jetpackHoverVibration = 0.0070f;
    float jetpackHoverHz = 8.5f;
    float jetpackSmoothing = 8.5f;
    float jetpackMaxSide = 0.075f;
    float jetpackMaxVertical = 0.085f;
    float jetpackMaxForward = 0.085f;

    float bikeLeanStrength = 0.070f;
    float bikePitchStrength = 0.060f;
    float bicyclePedalStrength = 0.026f;
    float boatWaveStrength = 0.085f;

    bool boatWavePhysicalEnabled = true;
    float boatWaveHeaveScale = 0.055f;
    float boatWavePitchScale = 0.070f;
    float boatWaveRollScale = 0.060f;
    float boatWaveOrientationHold = 0.018f;
    float boatWaveSpeedInfluence = 0.55f;
    float boatWaveSmoothing = 7.5f;
    float boatWaveSlamThreshold = 2.20f;
    float boatWaveSlamStrength = 0.105f;
    unsigned boatWaveSlamCooldownMs = 180;
    float boatWaveRotationPitchDeg = 0.85f;
    float boatWaveRotationRollDeg = 1.05f;
    float boatWaveMaxSide = 0.070f;
    float boatWaveMaxVertical = 0.100f;
    float boatWaveMaxForward = 0.085f;

    float aircraftTurbulenceStrength = 0.024f;
    float aircraftLandingStrength = 0.60f;

    bool aircraftPhysicalEnabled = true;
    float aircraftAccelSideScale = 0.020f;
    float aircraftAccelForwardScale = 0.018f;
    float aircraftAccelVerticalScale = 0.015f;
    float aircraftAttitudeHold = 0.030f;
    float aircraftSmoothing = 7.5f;
    float aircraftWeatherInfluence = 0.90f;
    float aircraftPlaneTurbulenceScale = 0.90f;
    float aircraftHeliTurbulenceScale = 1.15f;
    float aircraftHeliRotorVibration = 0.0065f;
    float aircraftPlaneAirframeVibration = 0.0040f;
    float aircraftRotationPitchDeg = 1.10f;
    float aircraftRotationRollDeg = 1.35f;
    float aircraftTouchdownThreshold = 0.035f;
    float aircraftTouchdownForwardScale = 0.28f;
    float aircraftMaxSide = 0.090f;
    float aircraftMaxVertical = 0.095f;
    float aircraftMaxForward = 0.090f;
    float trainJointStrength = 0.018f;
    float skidStrength = 0.040f;
    float engineVibrationStrength = 0.012f;
    float roadTextureStrength = 0.014f;

    float doorOpenStrength = 0.32f;
    float machineryStrength = 0.016f;
    float machineryDistance = 24.0f;
    float nativeShakeBlend = 0.0f;
    unsigned nativeShakeMaxContinuousMs = 900;
    unsigned nativeShakeRearmMs = 250;
    float nativeShakeChangeEpsilon = 0.002f;

    bool explosionShockDelayEnabled = true;
    float explosionShockSpeed = 343.0f;
    float explosionShockSecondaryScale = 0.38f;
    bool explosionOcclusionEnabled = true;
    float explosionOccludedScale = 0.55f;

    bool impactSequencingEnabled = true;
    unsigned impactSequenceResetMs = 420;
    float impactSecondaryScale = 0.82f;
    float impactTertiaryScale = 0.68f;

    // HighFrequency=0 suppresses vibration without removing larger body motion.
    float locomotionResponseScale = 1.0f;
    float combatResponseScale = 1.0f;
    float vehicleResponseScale = 1.0f;
    float environmentResponseScale = 1.0f;
    float highFrequencyScale = 1.0f;

    bool frequencyBandsEnabled = true;
    float lowFrequencyScale = 1.0f;
    float midFrequencyScale = 1.0f;
    float highFrequencyBandScale = 1.0f;
    bool proceduralNoiseEnabled = true;
    float proceduralNoiseMix = 0.42f;

    bool rotationEnabled = false;
    float rotationSafetyScale = 0.65f;
    float explosionPitchDeg = 1.25f;
    float explosionRollDeg = 1.60f;
    float explosionYawDeg = 0.30f;
    float explosionRotationSpring = 34.0f;
    float explosionRotationDamping = 11.5f;

    bool fovEnabled = true;
    bool fovPreferWidescreenFix = true;
    bool fovNativeFallback = true;
    float fovMinMultiplier = 0.90f;
    float fovMaxMultiplier = 1.12f;
    float fovResponse = 9.0f;
    float fovReturnResponse = 6.0f;
    float fovKickDecay = 7.5f;
    float fovWalk = 0.002f;
    float fovRun = 0.008f;
    float fovSprint = 0.020f;
    float fovCrouch = -0.005f;
    float fovAim = -0.018f;
    float fovAirborne = 0.009f;
    float fovVehicleSpeed = 0.055f;
    float fovVehicleAccel = 0.014f;
    float fovVehicleBrake = -0.010f;
    float fovVehicleCorner = 0.009f;
    float fovRecoilKick = 0.006f;
    float fovMeleeKick = 0.008f;
    float fovDamageKick = -0.018f;
    float fovExplosionKick = -0.030f;
    float fovLandingKick = -0.016f;
    float fovVehicleImpactKick = -0.022f;
    float fovChainsawAttack = 0.006f;
    float fovChainsawSaw = 0.012f;
    float fovContinuousTool = 0.004f;

    bool directionalDamageEnabled = true;
    float directionalDamageWeight = 0.85f;
    float incomingMeleeDamageScale = 1.35f;
    float incomingBulletDamageScale = 1.15f;
    float incomingBulletMidPunch = 0.085f;
    float incomingBulletSnap = 0.095f;
    float incomingBulletRecovery = 0.72f;
    float incomingExplosiveDamageScale = 1.55f;
    float incomingOtherDamageScale = 1.00f;
    float incomingDamageMinSeverity = 0.24f;
    float incomingDamageHighSnap = 0.055f;
    bool npcBulletHitFallbackEnabled = false;
    float npcBulletHitRadius = 0.42f;
    float npcBulletHitFallbackScale = 1.00f;
    unsigned npcBulletHitFallbackCooldownMs = 150;

    bool cameraOffsetCollisionEnabled = true;
    float cameraOffsetCollisionPadding = 0.035f;

    bool detailedMeleeWeapons = true;
    float batSwingScale = 1.00f;
    float knifeSwingScale = 0.58f;
    float meleeWeaponScale[16] = {};
    float meleeWeaponSideWeight[16] = {};
    float meleeWeaponVerticalWeight[16] = {};
    float meleeWeaponReleaseForward[16] = {};
    float meleeWeaponReleaseSide[16] = {};
    float meleeWeaponContactScale[16] = {};
    float meleeWeaponLockOnScale[16] = {};
    unsigned meleeWeaponCooldownMs[16] = {};

    float chainsawVibration = 0.028f;
    float chainsawIdleVibration = 0.006f;
    float chainsawAttackVibration = 0.020f;
    float chainsawSawVibration = 0.040f;
    float chainsawIdleHz = 11.0f;
    float chainsawAttackHz = 22.0f;
    float chainsawSawHz = 34.0f;
    float chainsawForwardLoad = 0.030f;
    float chainsawContactHoldMs = 150.0f;
    float continuousToolVibration = 0.010f;
    float heavyMeleeScale = 1.20f;

    bool contextualRecoilEnabled = true;
    float recoilCrouchScale = 0.84f;
    float recoilAimScale = 0.90f;
    float recoilMovingScale = 1.10f;
    float recoilDualWieldScale = 1.08f;

    bool reloadAnimationResponse = true;
    float reloadInsertScale = 0.65f;
    float reloadActionScale = 1.00f;

    bool wheelResponseEnabled = true;
    float wheelCompressionStrength = 0.40f;
    float tractionLossStrength = 0.045f;
    float tractionRegainStrength = 0.16f;

    bool runtimeReloadEnabled = false;
    int runtimeReloadKey = VK_F10;
    unsigned runtimeReloadDebounceMs = 350;
    bool detailedDebugHud = true;

    bool transitionsEnabled = true;
    bool meleeEnabled = true;
    bool breathingEnabled = true;
    bool surfaceStepsEnabled = true;

    // Idle drift in camera-local units.
    float idleSide = 0.0450f;
    float idleVertical = 0.0550f;
    float idleForward = 0.0150f;

    // Reduce procedural idle motion after sustained stillness.
    float idleSettleDelay = 0.80f;
    float idleSettledScale = 0.0f;
    float aimBreathingSide = 0.010f;
    float aimBreathingVertical = 0.016f;
    float aimBreathingForward = 0.006f;
    float aimBreathingRate = 0.82f;

    float gaitSide = 0.0900f;
    float gaitVertical = 0.1200f;
    float gaitForward = 0.0300f;
    float fallbackGait = 0.18f;
    float stepImpulse = 0.48f;
    float surfaceRoad = 1.00f;
    float surfaceConcrete = 1.04f;
    float surfaceGravel = 1.12f;
    float surfaceGrass = 0.78f;
    float surfaceDirt = 0.86f;
    float surfaceSand = 0.72f;
    float surfaceWood = 1.02f;
    float surfaceMetal = 1.16f;
    float surfaceRock = 1.08f;
    float surfaceMisc = 0.95f;

    float lookYaw = 1.0f;
    float lookPitch = 1.0f;
    float turnInput = 0.75f;
    float accelInertia = 0.80f;
    float directionalInertia = 1.0f;
    float strafeSway = 0.060f;
    float backwardSway = 0.035f;
    float jumpImpulse = 0.42f;
    float landingImpulse = 0.78f;
    float hardLandingScale = 1.35f;
    float landingSurfaceInfluence = 0.65f;
    float landingSoftMinScale = 0.72f;
    float landingHardMaxScale = 1.30f;
    float damageImpulse = 0.68f;
    float damageSideImpulse = 0.30f;
    float explosionImpulse = 1.05f;
    float explosionRadius = 28.0f;
    float maxImpactOffset = 0.34f;
    float maxFinalSide = 0.32f;
    float maxFinalVertical = 0.42f;
    float maxFinalForward = 0.22f;
    float meleeImpact = 0.42f;
    float meleeSide = 0.12f;

    // Use live fight-task animation phase, not raw attack input.
    bool detailedPlayerMelee = true;
    float punchWindup = 0.120f;
    float punchRelease = 0.520f;
    float punchContact = 0.300f;
    float punchSide = 0.110f;
    float punchVertical = 0.080f;
    float punchReleasePhase = 0.34f;
    float kickWindup = 0.190f;
    float kickRelease = 0.780f;
    float kickContact = 0.460f;
    float kickSide = 0.145f;
    float kickVertical = 0.210f;
    float kickReleasePhase = 0.40f;
    float meleeComboStep = 0.060f;
    float meleeComboMaxScale = 1.22f;

    float idlePitchDeg = 0.22f;
    float idleRollDeg = 0.20f;
    float gaitPitchDeg = 0.52f;
    float gaitRollDeg = 0.62f;
    float turnRollDeg = 0.85f;
    float lookYawDeg = 0.38f;
    float lookPitchDeg = 0.42f;
    float maxPitchDeg = 1.75f;
    float maxRollDeg = 2.00f;
    float maxYawDeg = 1.10f;

    // Vehicle response uses independent, translation-only motion.
    bool vehicleEnabled = true;
    float vehicleStrength = 1.0f;
    float vehicleAccelForward = 1.05f;
    float vehicleBrakeForward = 1.40f;
    float vehicleCornerSide = 0.92f;
    float vehicleVerticalAccel = 0.72f;
    float vehicleSpeedVibration = 0.022f;
    float vehicleSpeedVibrationRate = 5.6f;

    // Read actual horn playback (m_nHornCounter), including scripted horns.
    bool vehicleHornEnabled = true;
    float vehicleHornVibration = 0.012f;
    float vehicleHornVibrationRate = 11.5f;
    float vehicleHornAttack = 18.0f;
    float vehicleHornRelease = 10.0f;

    float vehicleImpactThreshold = 7.5f;
    float vehicleImpactImpulse = 0.72f;

    bool vehicleContactResponseEnabled = true;
    float vehicleContactStrength = 1.00f;
    float vehicleContactMinSpeed = 0.015f;
    float vehicleContactDeltaVelocityThreshold = 0.0075f;
    float vehicleContactStrongDeltaVelocity = 0.030f;
    float vehicleContactDamageReference = 45.0f;
    float vehicleContactSpring = 38.0f;
    float vehicleContactDamping = 10.5f;
    float vehicleContactDirectKick = 0.115f;
    float vehicleContactKickDecay = 18.0f;
    unsigned vehicleContactCooldownMs = 140;

    // Reinforce a physics collision only with matching health loss; exclude fire/bullets.
    bool vehicleCrashDamageEnabled = true;
    float vehicleCrashDamageThreshold = 2.0f;
    float vehicleCrashDamageReference = 120.0f;
    float vehicleCrashDamageStrength = 0.90f;
    float vehicleCrashFrontMultiplier = 1.15f;
    float vehicleCrashRearMultiplier = 0.85f;
    float vehicleCrashSideMultiplier = 1.00f;
    float vehicleCrashVerticalMultiplier = 0.75f;
    float vehicleCrashCatastrophicThreshold = 180.0f;
    float vehicleCrashCatastrophicMultiplier = 1.40f;
    unsigned vehicleCrashPhysicsWindowMs = 180;

    // Suspension: 0=compressed, 1=relaxed; fast compression adds curb/landing response.
    bool vehicleSuspensionEnabled = true;
    float vehicleSuspensionThreshold = 0.16f;
    float vehicleSuspensionStrength = 0.35f;
    bool vehicleSuspensionAdaptiveHandling = true;
    float vehicleSuspensionHandlingInfluence = 1.00f;

    float vehicleSpring = 20.0f;
    float vehicleDamping = 7.4f;
    float vehicleMaxSide = 0.26f;
    float vehicleMaxVertical = 0.32f;
    float vehicleMaxForward = 0.24f;
    float vehicleAutomobileScale = 1.00f;
    float vehicleBikeScale = 1.20f;
    float vehicleBoatScale = 0.82f;
    float vehicleHeliScale = 0.72f;
    float vehiclePlaneScale = 0.62f;
    float vehicleOtherScale = 0.85f;

    bool recoilEnabled = true;
    float recoilStrength = 1.00f;

    // Recoil bypasses aim damping; impulses use camera-local units.
    float recoilVerticalKick = 0.145f;
    float recoilForwardKick = 0.060f;
    float recoilSideKick = 0.020f;

    // Recoil displacement hits immediately; velocity creates the damped follow-through.
    float recoilPositionVertical = 0.055f;
    float recoilPositionForward = 0.024f;
    float recoilPositionSide = 0.008f;
    float recoilVelocityVertical = 0.85f;
    float recoilVelocityForward = 0.32f;
    float recoilVelocitySide = 0.10f;
    unsigned recoilMinIntervalMs = 20;
    float recoilPistol = 0.82f;
    float recoilDeagle = 1.18f;
    float recoilSmg = 0.62f;
    float recoilRifle = 0.82f;
    float recoilShotgun = 1.28f;
    float recoilSniper = 1.05f;
    float recoilLauncher = 1.55f;
    float recoilFlamethrower = 0.30f;
    float recoilMinigun = 0.42f;
    float firstShotMultiplier = 1.18f;
    float sustainedShotMultiplier = 0.90f;
    unsigned sustainedResetMs = 240;
    float weaponScale[47] = {};
    float weaponVertical[47] = {};
    float weaponForward[47] = {};
    float weaponSide[47] = {};
    float weaponRecovery[47] = {};

    float spring = 26.0f;
    float damping = 9.0f;
    float lookSpring = 48.0f;
    float lookDamping = 13.5f;
    float stateResponse = 4.8f;
    float footPhaseResponse = 11.0f;

    // Use faster recoil recovery than for heavy landing/explosion springs.
    float recoilSpringMultiplier = 1.75f;
    float recoilDampingMultiplier = 1.75f;
    float idleDampingMultiplier = 1.65f;

    bool debug = false;
    unsigned logEveryMs = 700;
    bool logSteps = false;
    bool logShots = false;
    bool overlayEnabled = false;
    float overlayScale = 1.0f;

    bool auditEnabled = false;
    bool auditConfigValues = true;
    bool auditFunctionCalls = true;
    unsigned auditEveryMs = 5000;

    unsigned nearbyVehicleScanMs = 33;
    unsigned surfaceScanMs = 80;

    std::string dir;
    std::string iniPath;

    static std::string ModuleDirectory() {
        HMODULE module = nullptr;

        // Resolve paths against this ASI, never the game directory.
        if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&CameraResponseModuleAnchor),
                &module) ||
            !module) {
            return {};
        }

        // Support paths longer than MAX_PATH.
        std::vector<char> path(512, '\0');

        for (;;) {
            const DWORD length =
                GetModuleFileNameA(
                    module,
                    path.data(),
                    static_cast<DWORD>(path.size())
                );

            if (length == 0)
                return {};

            if (length < path.size() - 1) {
                std::string result(path.data(), length);

                const auto slash =
                    result.find_last_of("\\/");

                if (slash == std::string::npos)
                    return {};

                result.resize(slash + 1);
                return result;
            }

            if (path.size() >= 32768)
                return {};

            path.resize(path.size() * 2, '\0');
        }
    }

    static float ReadFloat(
        const std::string& path,
        const char* section,
        const char* key,
        float fallback
    ) {
        char def[64] = {};
        char value[64] = {};

        std::snprintf(def, sizeof(def), "%.6f", fallback);

        GetPrivateProfileStringA(
            section,
            key,
            def,
            value,
            sizeof(value),
            path.c_str()
        );

        char* end = nullptr;
        const float result = std::strtof(value, &end);

        // Reject NaN/Inf before Clamp: unordered comparisons cannot sanitize them.
        return end && end != value && std::isfinite(result)
            ? result
            : fallback;
    }

    static int ReadInt(
        const std::string& path,
        const char* section,
        const char* key,
        int fallback
    ) {
        return GetPrivateProfileIntA(
            section,
            key,
            fallback,
            path.c_str()
        );
    }

    static bool ReadBool(
        const std::string& path,
        const char* section,
        const char* key,
        bool fallback
    ) {
        return ReadInt(
            path,
            section,
            key,
            fallback ? 1 : 0
        ) != 0;
    }

    static void ReadString(
        const std::string& path,
        const char* section,
        const char* key,
        const char* fallback,
        char* out,
        unsigned outSize
    ) {
        GetPrivateProfileStringA(section, key, fallback, out, outSize, path.c_str());
    }

    void Load() {
        if (dir.empty())
            dir = ModuleDirectory();

        // On module lookup failure, do not fall back to the working directory.
        if (dir.empty()) {
            iniPath.clear();
            return;
        }

        iniPath = dir + "CameraResponse.ini";

        enabled = ReadBool(iniPath, "General", "Enabled", enabled);
        strength = ReadFloat(iniPath, "General", "Strength", strength);

        masterMovementScale = ReadFloat(iniPath, "MasterTuning", "Movement", masterMovementScale);
        masterCombatScale = ReadFloat(iniPath, "MasterTuning", "Combat", masterCombatScale);
        masterVehicleScale = ReadFloat(iniPath, "MasterTuning", "Vehicle", masterVehicleScale);
        masterWorldScale = ReadFloat(iniPath, "MasterTuning", "World", masterWorldScale);
        masterFovScale = ReadFloat(iniPath, "MasterTuning", "FOV", masterFovScale);

        contextAttenuationEnabled = ReadBool(iniPath, "ContextAttenuation", "Enabled", contextAttenuationEnabled);
        contextIndoorScale = ReadFloat(iniPath, "ContextAttenuation", "Indoors", contextIndoorScale);
        contextAimScale = ReadFloat(iniPath, "ContextAttenuation", "Aiming", contextAimScale);
        contextCrouchScale = ReadFloat(iniPath, "ContextAttenuation", "Crouching", contextCrouchScale);
        contextLowHealthScale = ReadFloat(iniPath, "ContextAttenuation", "LowHealth", contextLowHealthScale);

        directionalResponseEnabled = ReadBool(iniPath, "DirectionalResponse", "Enabled", directionalResponseEnabled);


        directionalRotationEnabled = ReadBool(iniPath, "DirectionalRotation", "Enabled", directionalRotationEnabled);

        jerkResponseEnabled = ReadBool(iniPath, "JerkResponse", "Enabled", jerkResponseEnabled);
        playerJerkStrength = ReadFloat(iniPath, "JerkResponse", "PlayerStrength", playerJerkStrength);
        vehicleJerkStrength = ReadFloat(iniPath, "JerkResponse", "VehicleStrength", vehicleJerkStrength);




        ReadString(iniPath, "General", "Preset", presetName, presetName, sizeof(presetName));
        comfortPreset = _stricmp(presetName, "Comfort") == 0;

        presetScale = 1.0f;
        presetMovement = 1.0f;
        presetCombat = 1.0f;
        presetVehicle = 1.0f;
        presetWorld = 1.0f;
        presetFov = 1.0f;
        presetAmbient = 1.0f;
        presetImpact = 1.0f;
        presetRecoil = 1.0f;
        presetMelee = 1.0f;
        presetLanding = 1.0f;
        presetRotation = 1.0f;
        presetRecovery = 1.0f;

        if (_stricmp(presetName, "Subtle") == 0) {
            // Comfort: retain impacts, reduce ambient movement and FOV/rotation.
            presetScale = 0.82f;
            presetMovement = 0.78f;
            presetCombat = 0.88f;
            presetVehicle = 0.84f;
            presetWorld = 0.66f;
            presetFov = 0.58f;
            presetAmbient = 0.52f;
            presetImpact = 0.90f;
            presetRecoil = 0.82f;
            presetMelee = 0.86f;
            presetLanding = 0.90f;
            presetRotation = 0.62f;
            presetRecovery = 1.18f;
        } else if (_stricmp(presetName, "Heavy") == 0) {
            // Heavy: stronger body movement and impacts.
            presetScale = 1.18f;
            presetMovement = 1.16f;
            presetCombat = 1.22f;
            presetVehicle = 1.26f;
            presetWorld = 1.12f;
            presetFov = 1.08f;
            presetAmbient = 1.08f;
            presetImpact = 1.30f;
            presetRecoil = 1.24f;
            presetMelee = 1.24f;
            presetLanding = 1.30f;
            presetRotation = 1.10f;
            presetRecovery = 0.88f;
        } else if (_stricmp(presetName, "MachineGames") == 0) {
            // MachineGames: punchy transients, brisk recovery, restrained ambience.
            presetScale = 1.08f;
            presetMovement = 1.02f;
            presetCombat = 1.30f;
            presetVehicle = 1.12f;
            presetWorld = 0.86f;
            presetFov = 1.18f;
            presetAmbient = 0.72f;
            presetImpact = 1.34f;
            presetRecoil = 1.36f;
            presetMelee = 1.26f;
            presetLanding = 1.18f;
            presetRotation = 1.24f;
            presetRecovery = 1.20f;
        } else if (comfortPreset) {
            presetScale = 0.92f;
            presetMovement = 0.84f;
            presetCombat = 0.94f;
            presetVehicle = 0.88f;
            presetWorld = 0.72f;
            presetFov = 0.45f;
            presetAmbient = 0.0f;
            presetImpact = 0.94f;
            presetRecoil = 0.92f;
            presetMelee = 0.92f;
            presetLanding = 0.94f;
            presetRotation = 0.42f;
            presetRecovery = 1.28f;
        } else {
            // Default preset: balanced baseline.
            presetScale = 1.0f;
            presetMovement = 1.0f;
            presetCombat = 1.0f;
            presetVehicle = 1.0f;
            presetWorld = 0.92f;
            presetFov = 0.92f;
            presetAmbient = 0.86f;
            presetImpact = 1.0f;
            presetRecoil = 1.0f;
            presetMelee = 1.0f;
            presetLanding = 1.0f;
            presetRotation = 0.92f;
            presetRecovery = 1.0f;
        }

        walkScale = ReadFloat(iniPath, "Movement", "Walk", walkScale);
        runScale = ReadFloat(iniPath, "Movement", "Run", runScale);
        sprintScale = ReadFloat(iniPath, "Movement", "Sprint", sprintScale);
        aimScale = ReadFloat(iniPath, "Movement", "Aim", aimScale);
        crouchScale = ReadFloat(iniPath, "Movement", "Crouch", crouchScale);
        airborneScale = ReadFloat(iniPath, "Movement", "Airborne", airborneScale);
        locomotionEnabled = ReadBool(iniPath, "General", "LocomotionEnabled", locomotionEnabled);
        footstepsEnabled = ReadBool(iniPath, "General", "FootstepsEnabled", footstepsEnabled);
        inertiaEnabled = ReadBool(iniPath, "General", "InertiaEnabled", inertiaEnabled);
        jumpLandEnabled = ReadBool(iniPath, "General", "JumpLandEnabled", jumpLandEnabled);
        damageEnabled = ReadBool(iniPath, "General", "DamageEnabled", damageEnabled);
        explosionsEnabled = ReadBool(iniPath, "General", "ExplosionsEnabled", explosionsEnabled);
        nearbyGunfireEnabled = ReadBool(iniPath, "NearbyGunfire", "Enabled", nearbyGunfireEnabled);
        nearbyGunfireStrength = ReadFloat(iniPath, "NearbyGunfire", "Strength", nearbyGunfireStrength);
        nearbyGunfireMaxDistance = ReadFloat(iniPath, "NearbyGunfire", "MaxDistance", nearbyGunfireMaxDistance);

        nearbyVehiclesEnabled = ReadBool(iniPath, "NearbyVehicles", "Enabled", nearbyVehiclesEnabled);
        nearbyVehiclesStrength = ReadFloat(iniPath, "NearbyVehicles", "Strength", nearbyVehiclesStrength);

        doorResponseEnabled = ReadBool(iniPath, "DoorResponse", "Enabled", doorResponseEnabled);
        doorResponseStrength = ReadFloat(iniPath, "DoorResponse", "Strength", doorResponseStrength);

        vehicleMechanicalEnabled = ReadBool(iniPath, "ExtendedVehicle", "Enabled", vehicleMechanicalEnabled);
        vehicleBailOutEnabled = ReadBool(iniPath, "ExtendedVehicle", "BailOutEnabled", vehicleBailOutEnabled);
        engineRpmVibration = ReadFloat(iniPath, "ExtendedVehicle", "EngineRPMVibration", engineRpmVibration);

        waterResponseEnabled = ReadBool(iniPath, "WorldResponse", "WaterResponse", waterResponseEnabled);
        largeVehicleResponseEnabled = ReadBool(iniPath, "WorldResponse", "LargeVehicles", largeVehicleResponseEnabled);

        worldObjectResponseEnabled = ReadBool(iniPath, "WorldResponse", "ObjectImpacts", worldObjectResponseEnabled);

        pedWorldResponseEnabled = ReadBool(iniPath, "WorldResponse", "PedImpacts", pedWorldResponseEnabled);

        weatherResponseEnabled = ReadBool(iniPath, "EnvironmentResponse", "Enabled", weatherResponseEnabled);
        windResponseStrength = ReadFloat(iniPath, "EnvironmentResponse", "WindStrength", windResponseStrength);
        rainResponseStrength = ReadFloat(iniPath, "EnvironmentResponse", "RainStrength", rainResponseStrength);
        sandstormResponseStrength = ReadFloat(iniPath, "EnvironmentResponse", "SandstormStrength", sandstormResponseStrength);
        earthquakeResponseStrength = ReadFloat(iniPath, "EnvironmentResponse", "EarthquakeStrength", earthquakeResponseStrength);

        obstructionResponseEnabled = ReadBool(iniPath, "CameraObstruction", "Enabled", obstructionResponseEnabled);



        advancedResponseEnabled = ReadBool(iniPath, "AdvancedResponse", "Enabled", advancedResponseEnabled);
        nearMissEnabled = ReadBool(iniPath, "AdvancedResponse", "NearMiss", nearMissEnabled);
        nearMissStrength = ReadFloat(iniPath, "AdvancedResponse", "NearMissStrength", nearMissStrength);

        jetpackStrength = ReadFloat(iniPath, "AdvancedResponse", "JetpackStrength", jetpackStrength);
        jetpackRecoilScale = Clamp(ReadFloat(iniPath, "JetpackResponse", "RecoilScale", jetpackRecoilScale), 0.0f, 4.0f);
        mountedWeaponResponseEnabled = ReadBool(iniPath, "MountedWeapons", "Enabled", mountedWeaponResponseEnabled);

        jetpackDirectionalEnabled = ReadBool(iniPath, "JetpackResponse", "Enabled", jetpackDirectionalEnabled);

        boatWaveStrength = ReadFloat(iniPath, "AdvancedResponse", "BoatWaveStrength", boatWaveStrength);

        boatWavePhysicalEnabled = ReadBool(iniPath, "BoatResponse", "Enabled", boatWavePhysicalEnabled);
        boatWaveHeaveScale = ReadFloat(iniPath, "BoatResponse", "HeaveScale", boatWaveHeaveScale);
        boatWaveSlamStrength = ReadFloat(iniPath, "BoatResponse", "SlamStrength", boatWaveSlamStrength);


        aircraftPhysicalEnabled = ReadBool(iniPath, "AircraftResponse", "Enabled", aircraftPhysicalEnabled);
        aircraftWeatherInfluence = ReadFloat(iniPath, "AircraftResponse", "WeatherInfluence", aircraftWeatherInfluence);





        rotationEnabled = ReadBool(iniPath, "Rotation", "Enabled", rotationEnabled);

        fovEnabled = ReadBool(iniPath, "FOV", "Enabled", fovEnabled);
        fovWalk = ReadFloat(iniPath, "FOV", "Walk", fovWalk);
        fovRun = ReadFloat(iniPath, "FOV", "Run", fovRun);
        fovSprint = ReadFloat(iniPath, "FOV", "Sprint", fovSprint);
        fovCrouch = ReadFloat(iniPath, "FOV", "Crouch", fovCrouch);
        fovAim = ReadFloat(iniPath, "FOV", "Aim", fovAim);
        fovVehicleSpeed = ReadFloat(iniPath, "FOV", "VehicleSpeed", fovVehicleSpeed);
        fovRecoilKick = ReadFloat(iniPath, "FOV", "RecoilKick", fovRecoilKick);
        fovMeleeKick = ReadFloat(iniPath, "FOV", "MeleeKick", fovMeleeKick);
        fovDamageKick = ReadFloat(iniPath, "FOV", "DamageKick", fovDamageKick);
        fovExplosionKick = ReadFloat(iniPath, "FOV", "ExplosionKick", fovExplosionKick);
        fovLandingKick = ReadFloat(iniPath, "FOV", "LandingKick", fovLandingKick);
        fovVehicleImpactKick = ReadFloat(iniPath, "FOV", "VehicleImpactKick", fovVehicleImpactKick);

        incomingMeleeDamageScale = ReadFloat(iniPath, "DamageResponse", "MeleeScale", incomingMeleeDamageScale);
        incomingBulletDamageScale = ReadFloat(iniPath, "DamageResponse", "BulletScale", incomingBulletDamageScale);
        incomingExplosiveDamageScale = ReadFloat(iniPath, "DamageResponse", "ExplosiveScale", incomingExplosiveDamageScale);


        detailedMeleeWeapons = ReadBool(iniPath, "MeleeWeapons", "Enabled", detailedMeleeWeapons);
        batSwingScale = ReadFloat(iniPath, "MeleeWeapons", "BatClubScale", batSwingScale);
        knifeSwingScale = ReadFloat(iniPath, "MeleeWeapons", "KnifeScale", knifeSwingScale);
        chainsawVibration = ReadFloat(iniPath, "MeleeWeapons", "ChainsawVibration", chainsawVibration);
        heavyMeleeScale = ReadFloat(iniPath, "MeleeWeapons", "HeavyScale", heavyMeleeScale);

        contextualRecoilEnabled = ReadBool(iniPath, "RecoilContext", "Enabled", contextualRecoilEnabled);

        reloadAnimationResponse = ReadBool(iniPath, "ReloadResponse", "Enabled", reloadAnimationResponse);


        runtimeReloadEnabled = ReadBool(iniPath, "Developer", "RuntimeReload", runtimeReloadEnabled);
        detailedDebugHud = ReadBool(iniPath, "Developer", "DetailedHUD", detailedDebugHud);

        transitionsEnabled = ReadBool(iniPath, "General", "TransitionsEnabled", transitionsEnabled);
        meleeEnabled = ReadBool(iniPath, "General", "MeleeEnabled", meleeEnabled);
        breathingEnabled = ReadBool(iniPath, "General", "BreathingEnabled", breathingEnabled);
        surfaceStepsEnabled = ReadBool(iniPath, "General", "SurfaceStepsEnabled", surfaceStepsEnabled);

        vehicleEnabled = ReadBool(iniPath, "Vehicle", "Enabled", vehicleEnabled);
        vehicleStrength = ReadFloat(iniPath, "Vehicle", "Strength", vehicleStrength);
        vehicleAccelForward = ReadFloat(iniPath, "Vehicle", "AccelerationWeight", vehicleAccelForward);
        vehicleBrakeForward = ReadFloat(iniPath, "Vehicle", "BrakingWeight", vehicleBrakeForward);
        vehicleCornerSide = ReadFloat(iniPath, "Vehicle", "CorneringWeight", vehicleCornerSide);
        vehicleSpeedVibration = ReadFloat(iniPath, "Vehicle", "SpeedVibration", vehicleSpeedVibration);
        vehicleImpactImpulse = ReadFloat(iniPath, "Vehicle", "ImpactImpulse", vehicleImpactImpulse);
        vehicleContactResponseEnabled = ReadBool(iniPath, "VehicleCrash", "ContactResponse", vehicleContactResponseEnabled);
        vehicleCrashDamageEnabled = ReadBool(iniPath, "VehicleCrash", "DamageResponse", vehicleCrashDamageEnabled);
        vehicleCrashDamageStrength = ReadFloat(iniPath, "VehicleCrash", "DamageStrength", vehicleCrashDamageStrength);
        vehicleSuspensionEnabled = ReadBool(iniPath, "VehicleCrash", "SuspensionResponse", vehicleSuspensionEnabled);
        vehicleSuspensionHandlingInfluence = Clamp(vehicleSuspensionHandlingInfluence, 0.0f, 1.5f);
        vehicleAutomobileScale = ReadFloat(iniPath, "VehicleProfiles", "Automobile", vehicleAutomobileScale);
        vehicleBikeScale = ReadFloat(iniPath, "VehicleProfiles", "Bike", vehicleBikeScale);
        vehicleBoatScale = ReadFloat(iniPath, "VehicleProfiles", "Boat", vehicleBoatScale);
        vehicleHeliScale = ReadFloat(iniPath, "VehicleProfiles", "Helicopter", vehicleHeliScale);
        vehiclePlaneScale = ReadFloat(iniPath, "VehicleProfiles", "Plane", vehiclePlaneScale);
        vehicleOtherScale = ReadFloat(iniPath, "VehicleProfiles", "Other", vehicleOtherScale);



        jumpImpulse = ReadFloat(iniPath, "Impact", "JumpImpulse", jumpImpulse);
        landingImpulse = ReadFloat(iniPath, "Impact", "LandingImpulse", landingImpulse);
        damageImpulse = ReadFloat(iniPath, "Impact", "DamageImpulse", damageImpulse);
        explosionImpulse = ReadFloat(iniPath, "Impact", "ExplosionImpulse", explosionImpulse);

        detailedPlayerMelee = ReadBool(iniPath, "Melee", "DetailedPlayerResponse", detailedPlayerMelee);


        recoilEnabled = ReadBool(
            iniPath, "Recoil", "Enabled", recoilEnabled
        );
        recoilStrength = ReadFloat(
            iniPath, "Recoil", "Strength", recoilStrength
        );
        recoilPistol = ReadFloat(iniPath, "RecoilProfiles", "Pistol", recoilPistol);
        recoilDeagle = ReadFloat(iniPath, "RecoilProfiles", "DesertEagle", recoilDeagle);
        recoilSmg = ReadFloat(iniPath, "RecoilProfiles", "SMG", recoilSmg);
        recoilRifle = ReadFloat(iniPath, "RecoilProfiles", "Rifle", recoilRifle);
        recoilShotgun = ReadFloat(iniPath, "RecoilProfiles", "Shotgun", recoilShotgun);
        recoilSniper = ReadFloat(iniPath, "RecoilProfiles", "Sniper", recoilSniper);
        recoilLauncher = ReadFloat(iniPath, "RecoilProfiles", "Launcher", recoilLauncher);
        recoilFlamethrower = ReadFloat(iniPath, "RecoilProfiles", "Flamethrower", recoilFlamethrower);
        recoilMinigun = ReadFloat(iniPath, "RecoilProfiles", "Minigun", recoilMinigun);

        for (int id = 0; id < 47; ++id) {
            weaponScale[id] = -1.0f;
            weaponVertical[id] = 1.0f;
            weaponForward[id] = 1.0f;
            weaponSide[id] = 1.0f;
            weaponRecovery[id] = 1.0f;
        }
        // Per-weapon constants stay internal; user gains remain configurable.
        static constexpr float recoilTuning[17][5] = {
            {-1.000f, 1.000f, 1.000f, 1.000f, 1.000f}, // Colt45
            {-1.000f, 1.000f, 1.000f, 1.000f, 1.000f}, // Silenced
            {-1.000f, 1.100f, 1.100f, 1.050f, 0.950f}, // DesertEagle
            {-1.000f, 1.180f, 1.220f, 1.080f, 0.880f}, // Shotgun
            {-1.000f, 1.120f, 1.150f, 1.100f, 0.900f}, // Sawnoff
            {-1.000f, 1.250f, 1.280f, 1.120f, 0.850f}, // Spas12
            {-1.000f, 0.920f, 0.900f, 1.050f, 1.150f}, // MicroUzi
            {-1.000f, 0.900f, 0.880f, 1.050f, 1.150f}, // MP5
            {-1.000f, 1.000f, 1.000f, 1.000f, 1.050f}, // AK47
            {-1.000f, 0.960f, 0.960f, 1.020f, 1.080f}, // M4
            {-1.000f, 0.900f, 0.880f, 1.080f, 1.160f}, // Tec9
            {-1.000f, 1.080f, 1.080f, 0.900f, 0.920f}, // CountryRifle
            {-1.000f, 1.120f, 1.100f, 0.880f, 0.900f}, // SniperRifle
            {-1.000f, 1.200f, 1.250f, 1.120f, 0.820f}, // RocketLauncher
            {-1.000f, 1.160f, 1.200f, 1.080f, 0.840f}, // HeatSeeker
            {-1.000f, 0.550f, 0.450f, 0.650f, 1.100f}, // Flamethrower
            {-1.000f, 0.620f, 0.580f, 1.150f, 1.280f}, // Minigun
        };
        for (int id = 22; id <= 38; ++id) {
            const float* p = recoilTuning[id - 22];
            weaponScale[id] = p[0];
            weaponVertical[id] = p[1];
            weaponForward[id] = p[2];
            weaponSide[id] = p[3];
            weaponRecovery[id] = p[4];
        }

        static const float dScale[16] = {1.00f,1.08f,1.05f,0.82f,0.82f,1.00f,1.22f,0.70f,0.94f,0.78f,0.72f,0.76f,0.72f,0.76f,0.58f,0.68f};
        static const float dSideW[16] = {1.00f,1.05f,1.40f,1.05f,0.45f,1.32f,1.05f,1.12f,1.30f,0.55f,0.85f,0.88f,0.82f,0.86f,0.72f,0.95f};
        static const float dVertW[16] = {1.00f,1.05f,0.18f,0.14f,0.08f,0.22f,0.34f,0.12f,0.16f,0.12f,0.13f,0.14f,0.12f,0.13f,0.10f,0.14f};
        static const float dForward[16] = {1.00f,1.05f,0.27f,0.23f,0.17f,0.31f,0.36f,0.22f,0.29f,0.13f,0.20f,0.21f,0.19f,0.20f,0.16f,0.22f};
        static const float dSide[16] = {1.00f,1.05f,0.24f,0.15f,0.08f,0.22f,0.20f,0.16f,0.25f,0.07f,0.12f,0.13f,0.11f,0.12f,0.09f,0.15f};
        static const float dContact[16] = {1.00f,1.10f,1.08f,0.92f,0.90f,1.10f,1.22f,0.88f,1.18f,1.30f,0.80f,0.84f,0.78f,0.82f,0.62f,0.86f};
        static const float dLock[16] = {1.00f,1.00f,0.96f,1.00f,0.92f,0.96f,0.94f,0.95f,0.94f,1.08f,0.92f,0.92f,0.90f,0.90f,0.88f,0.94f};
        static const unsigned dCooldown[16] = {170u,175u,250u,180u,170u,235u,275u,200u,205u,115u,190u,190u,185u,185u,175u,205u};

        for (int id = 0; id < 16; ++id) {
            meleeWeaponScale[id]=dScale[id];
            meleeWeaponSideWeight[id]=dSideW[id];
            meleeWeaponVerticalWeight[id]=dVertW[id];
            meleeWeaponReleaseForward[id]=dForward[id];
            meleeWeaponReleaseSide[id]=dSide[id];
            meleeWeaponContactScale[id]=dContact[id];
            meleeWeaponLockOnScale[id]=dLock[id];
            meleeWeaponCooldownMs[id]=dCooldown[id];
        }


        spring = ReadFloat(iniPath, "Smoothing", "Spring", spring);
        damping = ReadFloat(iniPath, "Smoothing", "Damping", damping);

        debug = ReadBool(iniPath, "Debug", "Enabled", debug);
logSteps = ReadBool(
            iniPath, "Debug", "LogStepEvents", logSteps
        );
        logShots = ReadBool(
            iniPath, "Debug", "LogShotEvents", logShots
        );
        overlayEnabled = ReadBool(iniPath, "Debug", "OverlayEnabled", overlayEnabled);
        overlayScale = Clamp(ReadFloat(iniPath, "Debug", "OverlayScale", overlayScale), 0.65f, 2.5f);
        auditEnabled = ReadBool(iniPath, "DebugAudit", "Enabled", auditEnabled);

        strength = Clamp(strength, 0.0f, 4.0f);
        masterOverallScale = Clamp(masterOverallScale, 0.0f, 3.0f);
        masterMovementScale = Clamp(masterMovementScale, 0.0f, 3.0f);
        masterCombatScale = Clamp(masterCombatScale, 0.0f, 3.0f);
        masterVehicleScale = Clamp(masterVehicleScale, 0.0f, 3.0f);
        masterWorldScale = Clamp(masterWorldScale, 0.0f, 3.0f);
        masterFovScale = Clamp(masterFovScale, 0.0f, 3.0f);
        recoilStrength = Clamp(recoilStrength, 0.0f, 4.0f);
        recoilVerticalKick = Clamp(recoilVerticalKick, 0.0f, 0.50f);
        recoilForwardKick = Clamp(recoilForwardKick, 0.0f, 0.30f);
        recoilSideKick = Clamp(recoilSideKick, 0.0f, 0.20f);
        recoilPositionVertical = Clamp(recoilPositionVertical, 0.0f, 0.20f);
        recoilPositionForward = Clamp(recoilPositionForward, 0.0f, 0.10f);
        recoilPositionSide = Clamp(recoilPositionSide, 0.0f, 0.05f);
        recoilVelocityVertical = Clamp(recoilVelocityVertical, 0.0f, 4.0f);
        recoilVelocityForward = Clamp(recoilVelocityForward, 0.0f, 2.0f);
        recoilVelocitySide = Clamp(recoilVelocitySide, 0.0f, 1.0f);
        fallbackGait = Clamp(fallbackGait, 0.0f, 1.0f);
        crouchScale = Clamp(crouchScale, 0.0f, 2.0f);
        airborneScale = Clamp(airborneScale, 0.0f, 1.0f);
        movementDeadzone = Clamp(movementDeadzone, 0.0f, 0.05f);
        idleSettleDelay = Clamp(idleSettleDelay, 0.0f, 3.0f);
        idleSettledScale = Clamp(idleSettledScale, 0.0f, 1.0f);
        directionalInertia = Clamp(directionalInertia, 0.0f, 3.0f);
        strafeSway = Clamp(strafeSway, 0.0f, 0.20f);
        backwardSway = Clamp(backwardSway, 0.0f, 0.15f);
        jumpImpulse = Clamp(jumpImpulse, 0.0f, 2.0f);
        landingImpulse = Clamp(landingImpulse, 0.0f, 2.5f);
        hardLandingScale = Clamp(hardLandingScale, 0.5f, 3.0f);
        damageImpulse = Clamp(damageImpulse, 0.0f, 2.0f);
        damageSideImpulse = Clamp(damageSideImpulse, 0.0f, 1.0f);
        explosionImpulse = Clamp(explosionImpulse, 0.0f, 3.0f);
        explosionRadius = Clamp(explosionRadius, 3.0f, 80.0f);
        maxImpactOffset = Clamp(maxImpactOffset, 0.10f, 0.80f);
        maxFinalSide = Clamp(maxFinalSide, 0.05f, 1.50f);
        maxFinalVertical = Clamp(maxFinalVertical, 0.05f, 2.00f);
        maxFinalForward = Clamp(maxFinalForward, 0.05f, 1.00f);
        meleeImpact = Clamp(meleeImpact, 0.0f, 2.0f);
        meleeSide = Clamp(meleeSide, 0.0f, 1.0f);
        punchWindup = Clamp(punchWindup, 0.0f, 1.0f);
        punchRelease = Clamp(punchRelease, 0.0f, 2.0f);
        punchContact = Clamp(punchContact, 0.0f, 2.0f);
        punchSide = Clamp(punchSide, 0.0f, 1.0f);
        punchVertical = Clamp(punchVertical, 0.0f, 1.0f);
        punchReleasePhase = Clamp(punchReleasePhase, 0.05f, 0.90f);
        kickWindup = Clamp(kickWindup, 0.0f, 1.5f);
        kickRelease = Clamp(kickRelease, 0.0f, 2.5f);
        kickContact = Clamp(kickContact, 0.0f, 2.5f);
        kickSide = Clamp(kickSide, 0.0f, 1.0f);
        kickVertical = Clamp(kickVertical, 0.0f, 1.5f);
        kickReleasePhase = Clamp(kickReleasePhase, 0.05f, 0.90f);
        meleeComboStep = Clamp(meleeComboStep, 0.0f, 0.30f);
        meleeComboMaxScale = Clamp(meleeComboMaxScale, 1.0f, 2.0f);
        aimBreathingRate = Clamp(aimBreathingRate, 0.1f, 3.0f);
        firstShotMultiplier = Clamp(firstShotMultiplier, 0.1f, 3.0f);
        sustainedShotMultiplier = Clamp(sustainedShotMultiplier, 0.1f, 3.0f);
        for (int id = 22; id <= 38; ++id) {
            weaponScale[id] = Clamp(weaponScale[id], -1.0f, 4.0f);
            weaponVertical[id] = Clamp(weaponVertical[id], 0.0f, 3.0f);
            weaponForward[id] = Clamp(weaponForward[id], 0.0f, 3.0f);
            weaponSide[id] = Clamp(weaponSide[id], 0.0f, 3.0f);
            weaponRecovery[id] = Clamp(weaponRecovery[id], 0.4f, 3.0f);
        }
        spring = Clamp(spring, 1.0f, 160.0f);
        damping = Clamp(damping, 0.1f, 60.0f);
        lookSpring = Clamp(lookSpring, 1.0f, 180.0f);
        lookDamping = Clamp(lookDamping, 0.1f, 70.0f);
        stateResponse = Clamp(stateResponse, 0.1f, 40.0f);
        footPhaseResponse = Clamp(footPhaseResponse, 2.0f, 30.0f);
        recoilSpringMultiplier = Clamp(recoilSpringMultiplier, 0.50f, 4.0f);
        recoilDampingMultiplier = Clamp(recoilDampingMultiplier, 0.50f, 4.0f);
        idleDampingMultiplier = Clamp(idleDampingMultiplier, 1.0f, 4.0f);
        idlePitchDeg = Clamp(idlePitchDeg, 0.0f, 3.0f);
        idleRollDeg = Clamp(idleRollDeg, 0.0f, 3.0f);
        gaitPitchDeg = Clamp(gaitPitchDeg, 0.0f, 5.0f);
        gaitRollDeg = Clamp(gaitRollDeg, 0.0f, 5.0f);
        turnRollDeg = Clamp(turnRollDeg, 0.0f, 4.0f);
        lookYawDeg = Clamp(lookYawDeg, 0.0f, 3.0f);
        lookPitchDeg = Clamp(lookPitchDeg, 0.0f, 3.0f);
        maxPitchDeg = Clamp(maxPitchDeg, 0.0f, 6.0f);
        maxRollDeg = Clamp(maxRollDeg, 0.0f, 6.0f);
        maxYawDeg = Clamp(maxYawDeg, 0.0f, 4.0f);
        rotationSafetyScale = Clamp(rotationSafetyScale, 0.0f, 1.0f);
        explosionPitchDeg = Clamp(explosionPitchDeg, 0.0f, 8.0f);
        explosionRollDeg = Clamp(explosionRollDeg, 0.0f, 10.0f);
        explosionYawDeg = Clamp(explosionYawDeg, 0.0f, 4.0f);
        explosionRotationSpring = Clamp(explosionRotationSpring, 5.0f, 120.0f);
        explosionRotationDamping = Clamp(explosionRotationDamping, 1.0f, 40.0f);
        fovMinMultiplier = Clamp(fovMinMultiplier, 0.70f, 1.0f);
        fovMaxMultiplier = Clamp(fovMaxMultiplier, 1.0f, 1.35f);
        if (fovMinMultiplier > fovMaxMultiplier) std::swap(fovMinMultiplier, fovMaxMultiplier);
        fovResponse = Clamp(fovResponse, 0.5f, 30.0f);
        fovReturnResponse = Clamp(fovReturnResponse, 0.5f, 30.0f);
        fovKickDecay = Clamp(fovKickDecay, 0.5f, 30.0f);
        fovWalk = Clamp(fovWalk, -0.10f, 0.10f);
        fovRun = Clamp(fovRun, -0.10f, 0.12f);
        fovSprint = Clamp(fovSprint, -0.10f, 0.15f);
        fovCrouch = Clamp(fovCrouch, -0.10f, 0.10f);
        fovAim = Clamp(fovAim, -0.15f, 0.10f);
        fovAirborne = Clamp(fovAirborne, -0.10f, 0.12f);
        fovVehicleSpeed = Clamp(fovVehicleSpeed, -0.10f, 0.20f);
        fovVehicleAccel = Clamp(fovVehicleAccel, -0.10f, 0.12f);
        fovVehicleBrake = Clamp(fovVehicleBrake, -0.12f, 0.10f);
        fovVehicleCorner = Clamp(fovVehicleCorner, -0.10f, 0.12f);
        fovRecoilKick = Clamp(fovRecoilKick, -0.08f, 0.08f);
        fovMeleeKick = Clamp(fovMeleeKick, -0.08f, 0.08f);
        fovDamageKick = Clamp(fovDamageKick, -0.12f, 0.08f);
        fovExplosionKick = Clamp(fovExplosionKick, -0.15f, 0.10f);
        fovLandingKick = Clamp(fovLandingKick, -0.12f, 0.08f);
        fovVehicleImpactKick = Clamp(fovVehicleImpactKick, -0.15f, 0.10f);
        fovChainsawAttack = Clamp(fovChainsawAttack, -0.05f, 0.08f);
        fovChainsawSaw = Clamp(fovChainsawSaw, -0.05f, 0.10f);
        fovContinuousTool = Clamp(fovContinuousTool, -0.05f, 0.08f);
        lowFrequencyScale = Clamp(lowFrequencyScale, 0.0f, 2.0f);
        midFrequencyScale = Clamp(midFrequencyScale, 0.0f, 2.0f);
        highFrequencyBandScale = Clamp(highFrequencyBandScale, 0.0f, 2.0f);
        proceduralNoiseMix = Clamp(proceduralNoiseMix, 0.0f, 1.0f);
        directionalDamageWeight = Clamp(directionalDamageWeight, 0.0f, 2.0f);
        incomingMeleeDamageScale = Clamp(incomingMeleeDamageScale, 0.0f, 3.0f);
        incomingBulletDamageScale = Clamp(incomingBulletDamageScale, 0.0f, 3.0f);
        incomingBulletMidPunch = Clamp(incomingBulletMidPunch, 0.0f, 0.30f);
        incomingBulletSnap = Clamp(incomingBulletSnap, 0.0f, 0.30f);
        incomingBulletRecovery = Clamp(incomingBulletRecovery, 0.10f, 2.0f);
        incomingExplosiveDamageScale = Clamp(incomingExplosiveDamageScale, 0.0f, 4.0f);
        incomingOtherDamageScale = Clamp(incomingOtherDamageScale, 0.0f, 3.0f);
        incomingDamageMinSeverity = Clamp(incomingDamageMinSeverity, 0.0f, 1.0f);
        incomingDamageHighSnap = Clamp(incomingDamageHighSnap, 0.0f, 0.30f);
        npcBulletHitRadius = Clamp(npcBulletHitRadius, 0.08f, 1.0f);
        npcBulletHitFallbackScale = Clamp(npcBulletHitFallbackScale, 0.0f, 3.0f);
        npcBulletHitFallbackCooldownMs =
            static_cast<unsigned>(
                Clamp<int>(
                    static_cast<int>(npcBulletHitFallbackCooldownMs),
                    40,
                    1000
                )
            );
        explosionOccludedScale = Clamp(explosionOccludedScale, 0.05f, 1.0f);
        cameraOffsetCollisionPadding = Clamp(cameraOffsetCollisionPadding, 0.0f, 0.20f);
        batSwingScale = Clamp(batSwingScale, 0.0f, 2.5f);
        knifeSwingScale = Clamp(knifeSwingScale, 0.0f, 2.5f);
        chainsawVibration = Clamp(chainsawVibration, 0.0f, 0.12f);
        chainsawIdleVibration = Clamp(chainsawIdleVibration, 0.0f, 0.06f);
        chainsawAttackVibration = Clamp(chainsawAttackVibration, 0.0f, 0.10f);
        chainsawSawVibration = Clamp(chainsawSawVibration, 0.0f, 0.16f);
        chainsawIdleHz = Clamp(chainsawIdleHz, 2.0f, 30.0f);
        chainsawAttackHz = Clamp(chainsawAttackHz, 5.0f, 45.0f);
        chainsawSawHz = Clamp(chainsawSawHz, 8.0f, 60.0f);
        chainsawForwardLoad = Clamp(chainsawForwardLoad, 0.0f, 0.12f);
        chainsawContactHoldMs = Clamp(chainsawContactHoldMs, 40.0f, 500.0f);
        continuousToolVibration = Clamp(continuousToolVibration, 0.0f, 0.08f);
        heavyMeleeScale = Clamp(heavyMeleeScale, 0.0f, 3.0f);
        recoilCrouchScale = Clamp(recoilCrouchScale, 0.2f, 2.0f);
        recoilAimScale = Clamp(recoilAimScale, 0.2f, 2.0f);
        recoilMovingScale = Clamp(recoilMovingScale, 0.2f, 2.5f);
        recoilDualWieldScale = Clamp(recoilDualWieldScale, 0.2f, 2.5f);
        reloadInsertScale = Clamp(reloadInsertScale, 0.0f, 2.0f);
        reloadActionScale = Clamp(reloadActionScale, 0.0f, 2.0f);
        wheelCompressionStrength = Clamp(wheelCompressionStrength, 0.0f, 2.0f);
        tractionLossStrength = Clamp(tractionLossStrength, 0.0f, 0.30f);
        tractionRegainStrength = Clamp(tractionRegainStrength, 0.0f, 1.0f);

        for (int id=0; id<16; ++id) {
            meleeWeaponScale[id]=Clamp(meleeWeaponScale[id],0.0f,3.0f);
            meleeWeaponSideWeight[id]=Clamp(meleeWeaponSideWeight[id],0.0f,3.0f);
            meleeWeaponVerticalWeight[id]=Clamp(meleeWeaponVerticalWeight[id],0.0f,3.0f);
            meleeWeaponReleaseForward[id]=Clamp(meleeWeaponReleaseForward[id],0.0f,1.0f);
            meleeWeaponReleaseSide[id]=Clamp(meleeWeaponReleaseSide[id],0.0f,1.0f);
            meleeWeaponContactScale[id]=Clamp(meleeWeaponContactScale[id],0.0f,3.0f);
            meleeWeaponLockOnScale[id]=Clamp(meleeWeaponLockOnScale[id],0.2f,2.0f);
        }
        contextIndoorScale=Clamp(contextIndoorScale,0.0f,1.5f);
        contextAimScale=Clamp(contextAimScale,0.0f,1.5f);
        contextCrouchScale=Clamp(contextCrouchScale,0.0f,1.5f);
        contextLowHealthScale=Clamp(contextLowHealthScale,0.0f,1.5f);
        contextLowHealthThreshold=Clamp(contextLowHealthThreshold,1.0f,100.0f);
        contextScriptedScale=Clamp(contextScriptedScale,0.0f,1.0f);
        contextIndoorCeilingHeight=Clamp(contextIndoorCeilingHeight,2.0f,30.0f);
        landingSurfaceInfluence=Clamp(landingSurfaceInfluence,0.0f,1.5f);
        landingSoftMinScale=Clamp(landingSoftMinScale,0.3f,1.0f);
        landingHardMaxScale=Clamp(landingHardMaxScale,1.0f,2.0f);
        engineRpmVibration=Clamp(engineRpmVibration,0.0f,0.08f);
        engineRpmMinHz=Clamp(engineRpmMinHz,1.0f,30.0f);
        engineRpmMaxHz=Clamp(engineRpmMaxHz,engineRpmMinHz,45.0f);
        engineLoadInfluence=Clamp(engineLoadInfluence,0.0f,2.0f);
        bikeEngineScale=Clamp(bikeEngineScale,0.2f,3.0f);
        gearShiftCarScale=Clamp(gearShiftCarScale,0.2f,3.0f);
        gearShiftBikeScale=Clamp(gearShiftBikeScale,0.2f,3.0f);
        gearShiftPulseRecovery=Clamp(gearShiftPulseRecovery,0.1f,2.0f);

        jetpackDirectionalStrength = Clamp(jetpackDirectionalStrength, 0.0f, 0.40f);
        jetpackForwardScale = Clamp(jetpackForwardScale, 0.0f, 2.5f);
        jetpackBackwardScale = Clamp(jetpackBackwardScale, 0.0f, 2.5f);
        jetpackSideScale = Clamp(jetpackSideScale, 0.0f, 2.5f);
        jetpackVerticalScale = Clamp(jetpackVerticalScale, 0.0f, 2.5f);
        jetpackAccelInfluence = Clamp(jetpackAccelInfluence, 0.0f, 3.0f);
        jetpackVelocityInfluence = Clamp(jetpackVelocityInfluence, 0.0f, 2.0f);
        jetpackHoverVibration = Clamp(jetpackHoverVibration, 0.0f, 0.030f);
        jetpackHoverHz = Clamp(jetpackHoverHz, 1.0f, 24.0f);
        jetpackSmoothing = Clamp(jetpackSmoothing, 1.0f, 30.0f);
        jetpackMaxSide = Clamp(jetpackMaxSide, 0.0f, 0.20f);
        jetpackMaxVertical = Clamp(jetpackMaxVertical, 0.0f, 0.20f);
        jetpackMaxForward = Clamp(jetpackMaxForward, 0.0f, 0.20f);

        boatWaveHeaveScale = Clamp(boatWaveHeaveScale, 0.0f, 0.25f);
        boatWavePitchScale = Clamp(boatWavePitchScale, 0.0f, 0.25f);
        boatWaveRollScale = Clamp(boatWaveRollScale, 0.0f, 0.25f);
        boatWaveOrientationHold = Clamp(boatWaveOrientationHold, 0.0f, 0.10f);
        boatWaveSpeedInfluence = Clamp(boatWaveSpeedInfluence, 0.0f, 2.0f);
        boatWaveSmoothing = Clamp(boatWaveSmoothing, 1.0f, 30.0f);
        boatWaveSlamThreshold = Clamp(boatWaveSlamThreshold, 0.25f, 12.0f);
        boatWaveSlamStrength = Clamp(boatWaveSlamStrength, 0.0f, 0.40f);
        boatWaveRotationPitchDeg = Clamp(boatWaveRotationPitchDeg, 0.0f, 4.0f);
        boatWaveRotationRollDeg = Clamp(boatWaveRotationRollDeg, 0.0f, 5.0f);
        boatWaveMaxSide = Clamp(boatWaveMaxSide, 0.0f, 0.20f);
        boatWaveMaxVertical = Clamp(boatWaveMaxVertical, 0.0f, 0.25f);
        boatWaveMaxForward = Clamp(boatWaveMaxForward, 0.0f, 0.20f);

        weatherSmoothing = Clamp(weatherSmoothing, 0.5f, 12.0f);
        windGustStrength = Clamp(windGustStrength, 0.0f, 1.5f);
        windGustRate = Clamp(windGustRate, 0.1f, 4.0f);
        rainTextureRate = Clamp(rainTextureRate, 2.0f, 30.0f);
        rainVerticalBias = Clamp(rainVerticalBias, 0.0f, 2.0f);
        sandstormLowFrequency = Clamp(sandstormLowFrequency, 0.1f, 4.0f);
        earthquakeMaxOffset = Clamp(earthquakeMaxOffset, 0.0f, 0.20f);
        weatherMaxAmbient = Clamp(weatherMaxAmbient, 0.0f, 0.20f);

        aircraftAccelSideScale = Clamp(aircraftAccelSideScale, 0.0f, 0.10f);
        aircraftAccelForwardScale = Clamp(aircraftAccelForwardScale, 0.0f, 0.10f);
        aircraftAccelVerticalScale = Clamp(aircraftAccelVerticalScale, 0.0f, 0.10f);
        aircraftAttitudeHold = Clamp(aircraftAttitudeHold, 0.0f, 0.15f);
        aircraftSmoothing = Clamp(aircraftSmoothing, 1.0f, 30.0f);
        aircraftWeatherInfluence = Clamp(aircraftWeatherInfluence, 0.0f, 2.0f);
        aircraftPlaneTurbulenceScale = Clamp(aircraftPlaneTurbulenceScale, 0.0f, 3.0f);
        aircraftHeliTurbulenceScale = Clamp(aircraftHeliTurbulenceScale, 0.0f, 3.0f);
        aircraftHeliRotorVibration = Clamp(aircraftHeliRotorVibration, 0.0f, 0.03f);
        aircraftPlaneAirframeVibration = Clamp(aircraftPlaneAirframeVibration, 0.0f, 0.03f);
        aircraftRotationPitchDeg = Clamp(aircraftRotationPitchDeg, 0.0f, 5.0f);
        aircraftRotationRollDeg = Clamp(aircraftRotationRollDeg, 0.0f, 6.0f);
        aircraftTouchdownThreshold = Clamp(aircraftTouchdownThreshold, 0.005f, 0.20f);
        aircraftTouchdownForwardScale = Clamp(aircraftTouchdownForwardScale, 0.0f, 1.0f);
        aircraftMaxSide = Clamp(aircraftMaxSide, 0.0f, 0.20f);
        aircraftMaxVertical = Clamp(aircraftMaxVertical, 0.0f, 0.20f);
        aircraftMaxForward = Clamp(aircraftMaxForward, 0.0f, 0.20f);

        directionalSourceSide = Clamp(directionalSourceSide, 0.0f, 2.0f);
        directionalSourceForward = Clamp(directionalSourceForward, 0.0f, 2.0f);
        directionalSourceVertical = Clamp(directionalSourceVertical, 0.0f, 1.0f);
        directionalDoorScale = Clamp(directionalDoorScale, 0.0f, 2.0f);
        directionalWaterScale = Clamp(directionalWaterScale, 0.0f, 2.0f);
        directionalVehicleTransitionScale = Clamp(directionalVehicleTransitionScale, 0.0f, 2.0f);
        directionalSwimScale = Clamp(directionalSwimScale, 0.0f, 2.0f);
        directionalParachuteScale = Clamp(directionalParachuteScale, 0.0f, 2.0f);
        directionalLargeVehicleScale = Clamp(directionalLargeVehicleScale, 0.0f, 2.0f);

        responseBudgetNormalized = Clamp(responseBudgetNormalized, 0.55f, 2.0f);
        responseBudgetCriticalReserve = Clamp(responseBudgetCriticalReserve, 0.0f, 0.75f);
        responseBudgetSoftness = Clamp(responseBudgetSoftness, 0.05f, 1.0f);
        ambientDuckAttack = Clamp(ambientDuckAttack, 1.0f, 60.0f);
        ambientDuckRelease = Clamp(ambientDuckRelease, 0.5f, 30.0f);
        ambientDuckMinimum = Clamp(ambientDuckMinimum, 0.0f, 1.0f);
        directionalRotationRollDeg = Clamp(directionalRotationRollDeg, 0.0f, 4.0f);
        directionalRotationPitchDeg = Clamp(directionalRotationPitchDeg, 0.0f, 4.0f);
        directionalRotationYawDeg = Clamp(directionalRotationYawDeg, 0.0f, 2.0f);
        directionalRotationSpring = Clamp(directionalRotationSpring, 5.0f, 120.0f);
        directionalRotationDamping = Clamp(directionalRotationDamping, 1.0f, 40.0f);
        playerJerkStrength = Clamp(playerJerkStrength, 0.0f, 0.05f);
        vehicleJerkStrength = Clamp(vehicleJerkStrength, 0.0f, 0.08f);
        jerkDeadzone = Clamp(jerkDeadzone, 0.0f, 30.0f);
        jerkMax = Clamp(jerkMax, 1.0f, 80.0f);
        nearbyApproachInfluence = Clamp(nearbyApproachInfluence, 0.0f, 2.0f);
        nearbyPassInfluence = Clamp(nearbyPassInfluence, 0.0f, 2.0f);
        rotorWashDistance = Clamp(rotorWashDistance, 2.0f, 80.0f);
        rotorWashHeight = Clamp(rotorWashHeight, 2.0f, 60.0f);
        rotorWashStrength = Clamp(rotorWashStrength, 0.0f, 0.20f);
        explosionPressureIndoorScale = Clamp(explosionPressureIndoorScale, 0.0f, 1.5f);
        explosionPressureBelowBoost = Clamp(explosionPressureBelowBoost, 0.0f, 2.0f);
        explosionPressureAboveScale = Clamp(explosionPressureAboveScale, 0.0f, 1.5f);
        explosionPressureLateralRoll = Clamp(explosionPressureLateralRoll, 0.0f, 2.0f);
        footstepSurfaceBandStrength = Clamp(footstepSurfaceBandStrength, 0.0f, 0.10f);

        vehicleStrength = Clamp(vehicleStrength, 0.0f, 3.0f);
        vehicleAccelForward = Clamp(vehicleAccelForward, 0.0f, 3.0f);
        vehicleBrakeForward = Clamp(vehicleBrakeForward, 0.0f, 3.0f);
        vehicleCornerSide = Clamp(vehicleCornerSide, 0.0f, 3.0f);
        vehicleVerticalAccel = Clamp(vehicleVerticalAccel, 0.0f, 3.0f);
        vehicleSpeedVibration = Clamp(vehicleSpeedVibration, 0.0f, 0.15f);
        vehicleSpeedVibrationRate = Clamp(vehicleSpeedVibrationRate, 0.1f, 20.0f);
        vehicleHornVibration = Clamp(vehicleHornVibration, 0.0f, 0.08f);
        vehicleHornVibrationRate = Clamp(vehicleHornVibrationRate, 1.0f, 30.0f);
        vehicleHornAttack = Clamp(vehicleHornAttack, 1.0f, 60.0f);
        vehicleHornRelease = Clamp(vehicleHornRelease, 1.0f, 60.0f);
        vehicleImpactThreshold = Clamp(vehicleImpactThreshold, 1.0f, 40.0f);
        vehicleImpactImpulse = Clamp(vehicleImpactImpulse, 0.0f, 3.0f);
        vehicleSpring = Clamp(vehicleSpring, 1.0f, 120.0f);
        vehicleDamping = Clamp(vehicleDamping, 0.1f, 50.0f);
        vehicleMaxSide = Clamp(vehicleMaxSide, 0.02f, 1.0f);
        vehicleMaxVertical = Clamp(vehicleMaxVertical, 0.02f, 1.0f);
        vehicleMaxForward = Clamp(vehicleMaxForward, 0.02f, 1.0f);
        vehicleAutomobileScale = Clamp(vehicleAutomobileScale, 0.0f, 3.0f);
        vehicleBikeScale = Clamp(vehicleBikeScale, 0.0f, 3.0f);
        vehicleBoatScale = Clamp(vehicleBoatScale, 0.0f, 3.0f);
        vehicleHeliScale = Clamp(vehicleHeliScale, 0.0f, 3.0f);
        vehiclePlaneScale = Clamp(vehiclePlaneScale, 0.0f, 3.0f);
        vehicleOtherScale = Clamp(vehicleOtherScale, 0.0f, 3.0f);

        if (comfortPreset) {
            idleScale=0.0f;
            aimBreathingSide=aimBreathingVertical=aimBreathingForward=0.0f;
            nearbyVehiclesStrength=0.0f;
            windResponseStrength=rainResponseStrength=sandstormResponseStrength=0.0f;
            machineryStrength=0.0f;
            nativeShakeBlend=0.0f;
            trainResponseStrength=aircraftResponseStrength=0.0f;
            engineVibrationStrength=roadTextureStrength=0.0f;
            vehicleSpeedVibration=vehicleHornVibration=0.0f;
            boatWaveStrength=aircraftTurbulenceStrength=0.0f;
            parachuteAirStrength=jetpackStrength=0.0f;
            continuousToolVibration=0.0f;
        }
    }

    void DumpEffectiveValues(Logger& log) const {
        if (!auditEnabled || !auditConfigValues)
            return;
        log.Write("CONFIG AUDIT BEGIN effective-values");
        log.Write("CONFIG EFFECTIVE [General] Enabled=%d", enabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [General] Strength=%.6f", strength);
        log.Write("CONFIG EFFECTIVE [Movement] Idle=%.6f", idleScale);
        log.Write("CONFIG EFFECTIVE [Movement] Walk=%.6f", walkScale);
        log.Write("CONFIG EFFECTIVE [Movement] Run=%.6f", runScale);
        log.Write("CONFIG EFFECTIVE [Movement] Sprint=%.6f", sprintScale);
        log.Write("CONFIG EFFECTIVE [Movement] Aim=%.6f", aimScale);
        log.Write("CONFIG EFFECTIVE [Movement] Crouch=%.6f", crouchScale);
        log.Write("CONFIG EFFECTIVE [Movement] Airborne=%.6f", airborneScale);
        log.Write("CONFIG EFFECTIVE [Movement] Deadzone=%.6f", movementDeadzone);
        log.Write("CONFIG EFFECTIVE [General] LocomotionEnabled=%d", locomotionEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [General] FootstepsEnabled=%d", footstepsEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [General] InertiaEnabled=%d", inertiaEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [General] JumpLandEnabled=%d", jumpLandEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [General] DamageEnabled=%d", damageEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [General] ExplosionsEnabled=%d", explosionsEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [NearbyGunfire] Enabled=%d", nearbyGunfireEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [NearbyGunfire] AffectInVehicle=%d", nearbyGunfireInVehicle ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [NearbyGunfire] Strength=%.6f", nearbyGunfireStrength);
        log.Write("CONFIG EFFECTIVE [NearbyGunfire] MinDistance=%.6f", nearbyGunfireMinDistance);
        log.Write("CONFIG EFFECTIVE [NearbyGunfire] MaxDistance=%.6f", nearbyGunfireMaxDistance);
        log.Write("CONFIG EFFECTIVE [NearbyGunfire] FalloffPower=%.6f", nearbyGunfireFalloffPower);
        log.Write("CONFIG EFFECTIVE [NearbyGunfire] Side=%.6f", nearbyGunfireSide);
        log.Write("CONFIG EFFECTIVE [NearbyGunfire] Vertical=%.6f", nearbyGunfireVertical);
        log.Write("CONFIG EFFECTIVE [NearbyGunfire] Forward=%.6f", nearbyGunfireForward);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] Enabled=%d", nearbyVehiclesEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] AffectOnFoot=%d", nearbyVehiclesOnFoot ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] AffectInVehicle=%d", nearbyVehiclesInVehicle ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] Strength=%.6f", nearbyVehiclesStrength);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] MinDistance=%.6f", nearbyVehiclesMinDistance);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] MaxDistance=%.6f", nearbyVehiclesMaxDistance);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] FalloffPower=%.6f", nearbyVehiclesFalloffPower);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] IdleScale=%.6f", nearbyVehiclesIdleScale);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] LowSpeedScale=%.6f", nearbyVehiclesLowSpeedScale);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] HighSpeedScale=%.6f", nearbyVehiclesHighSpeedScale);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] AccelerationScale=%.6f", nearbyVehiclesAccelerationScale);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] Vertical=%.6f", nearbyVehiclesVertical);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] Side=%.6f", nearbyVehiclesSide);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] Forward=%.6f", nearbyVehiclesForward);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] MaxCombined=%.6f", nearbyVehiclesMaxCombined);
        log.Write("CONFIG EFFECTIVE [NearbyVehicles] MaxContributors=%lu", static_cast<unsigned long>(nearbyVehiclesMaxContributors));
        log.Write("CONFIG EFFECTIVE [DoorResponse] Enabled=%d", doorResponseEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [DoorResponse] VehicleDoors=%d", vehicleDoorResponseEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [DoorResponse] BuildingDoors=%d", buildingDoorResponseEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [DoorResponse] AffectInVehicle=%d", doorResponseInVehicle ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [DoorResponse] Strength=%.6f", doorResponseStrength);
        log.Write("CONFIG EFFECTIVE [DoorResponse] VehicleStrength=%.6f", vehicleDoorStrength);
        log.Write("CONFIG EFFECTIVE [DoorResponse] BuildingStrength=%.6f", buildingDoorStrength);
        log.Write("CONFIG EFFECTIVE [DoorResponse] MinDistance=%.6f", doorResponseMinDistance);
        log.Write("CONFIG EFFECTIVE [DoorResponse] MaxDistance=%.6f", doorResponseMaxDistance);
        log.Write("CONFIG EFFECTIVE [DoorResponse] FalloffPower=%.6f", doorResponseFalloffPower);
        log.Write("CONFIG EFFECTIVE [DoorResponse] Side=%.6f", doorResponseSide);
        log.Write("CONFIG EFFECTIVE [DoorResponse] Vertical=%.6f", doorResponseVertical);
        log.Write("CONFIG EFFECTIVE [DoorResponse] Forward=%.6f", doorResponseForward);
        log.Write("CONFIG EFFECTIVE [DoorResponse] OwnVehicleScale=%.6f", doorVehicleOwnScale);
        log.Write("CONFIG EFFECTIVE [DoorResponse] VehicleCloseThreshold=%.6f", vehicleDoorCloseThreshold);
        log.Write("CONFIG EFFECTIVE [DoorResponse] VehicleWasOpenThreshold=%.6f", vehicleDoorWasOpenThreshold);
        log.Write("CONFIG EFFECTIVE [DoorResponse] ScanMs=%lu", static_cast<unsigned long>(doorScanMs));
        log.Write("CONFIG EFFECTIVE [DoorResponse] BonnetScale=%.6f", doorBonnetScale);
        log.Write("CONFIG EFFECTIVE [DoorResponse] BootScale=%.6f", doorBootScale);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] Enabled=%d", vehicleMechanicalEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] EnterStrength=%.6f", vehicleEnterStrength);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] ExitStrength=%.6f", vehicleExitStrength);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] BailOutEnabled=%d", vehicleBailOutEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] BailOutMinSpeed=%.6f", vehicleBailOutMinSpeed);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] BailOutStrength=%.6f", vehicleBailOutStrength);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] BailAirborneWindowMs=%lu", static_cast<unsigned long>(vehicleBailAirborneWindowMs));
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] BailLandingScale=%.6f", vehicleBailLandingScale);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] BailFOVKick=%.6f", vehicleBailFovKick);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] EngineStartStrength=%.6f", engineStartStrength);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] EngineStopStrength=%.6f", engineStopStrength);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] GearShiftStrength=%.6f", gearShiftStrength);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] TyreBlowoutStrength=%.6f", tyreBlowoutStrength);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] AxleLandingStrength=%.6f", axleLandingStrength);
        log.Write("CONFIG EFFECTIVE [WorldResponse] WaterResponse=%d", waterResponseEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [WorldResponse] WaterEntryStrength=%.6f", waterEntryStrength);
        log.Write("CONFIG EFFECTIVE [WorldResponse] LargeVehicles=%d", largeVehicleResponseEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [WorldResponse] TrainStrength=%.6f", trainResponseStrength);
        log.Write("CONFIG EFFECTIVE [WorldResponse] TrainDistance=%.6f", trainResponseDistance);
        log.Write("CONFIG EFFECTIVE [WorldResponse] AircraftStrength=%.6f", aircraftResponseStrength);
        log.Write("CONFIG EFFECTIVE [WorldResponse] AircraftDistance=%.6f", aircraftResponseDistance);
        log.Write("CONFIG EFFECTIVE [WorldResponse] LargeVehicleScanMs=%lu", static_cast<unsigned long>(largeVehicleScanMs));
        log.Write("CONFIG EFFECTIVE [WorldResponse] ObjectImpacts=%d", worldObjectResponseEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [WorldResponse] ObjectImpactStrength=%.6f", worldObjectImpactStrength);
        log.Write("CONFIG EFFECTIVE [WorldResponse] ObjectImpactDistance=%.6f", worldObjectImpactDistance);
        log.Write("CONFIG EFFECTIVE [WorldResponse] ObjectScanMs=%lu", static_cast<unsigned long>(worldObjectScanMs));
        log.Write("CONFIG EFFECTIVE [WorldResponse] PedImpacts=%d", pedWorldResponseEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [WorldResponse] PedImpactStrength=%.6f", pedImpactStrength);
        log.Write("CONFIG EFFECTIVE [WorldResponse] HeavyFootstepStrength=%.6f", heavyFootstepStrength);
        log.Write("CONFIG EFFECTIVE [WorldResponse] HeavyFootstepDistance=%.6f", heavyFootstepDistance);
        log.Write("CONFIG EFFECTIVE [WorldResponse] PedScanMs=%lu", static_cast<unsigned long>(pedWorldScanMs));
        log.Write("CONFIG EFFECTIVE [EnvironmentResponse] Enabled=%d", weatherResponseEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [EnvironmentResponse] WindStrength=%.6f", windResponseStrength);
        log.Write("CONFIG EFFECTIVE [EnvironmentResponse] RainStrength=%.6f", rainResponseStrength);
        log.Write("CONFIG EFFECTIVE [EnvironmentResponse] SandstormStrength=%.6f", sandstormResponseStrength);
        log.Write("CONFIG EFFECTIVE [EnvironmentResponse] EarthquakeStrength=%.6f", earthquakeResponseStrength);
        log.Write("CONFIG EFFECTIVE [CameraObstruction] Enabled=%d", obstructionResponseEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [CameraObstruction] Threshold=%.6f", obstructionThreshold);
        log.Write("CONFIG EFFECTIVE [CameraObstruction] Strength=%.6f", obstructionStrength);
        log.Write("CONFIG EFFECTIVE [CameraObstruction] Spring=%.6f", obstructionSpring);
        log.Write("CONFIG EFFECTIVE [CameraObstruction] Damping=%.6f", obstructionDamping);
        log.Write("CONFIG EFFECTIVE [Performance] EnvironmentPriorityMix=%d", environmentPriorityMixEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [Performance] EnvironmentMixMaxImpulse=%.6f", environmentMixMaxImpulse);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] HandlingPersonality=%d", vehiclePersonalityEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [ExtendedVehicle] CollisionMaterialInfluence=%.6f", collisionMaterialInfluence);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] Enabled=%d", advancedResponseEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] ReloadStrength=%.6f", reloadResponseStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] EquipStrength=%.6f", equipResponseStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] NearMiss=%d", nearMissEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] NearMissStrength=%.6f", nearMissStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] NearMissDistance=%.6f", nearMissDistance);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] NearMissMaxShooterDistance=%.6f", nearMissMaxShooterDistance);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] SwimStrength=%.6f", swimResponseStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] ClimbStrength=%.6f", climbResponseStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] ParachuteDeployStrength=%.6f", parachuteDeployStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] ParachuteAirStrength=%.6f", parachuteAirStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] ParachuteLandStrength=%.6f", parachuteLandStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] JetpackStrength=%.6f", jetpackStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] JetpackTransitionStrength=%.6f", jetpackTransitionStrength);
        log.Write("CONFIG EFFECTIVE [JetpackResponse] RecoilScale=%.6f", jetpackRecoilScale);
        log.Write("CONFIG EFFECTIVE [MountedWeapons] Enabled=%d", mountedWeaponResponseEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [MountedWeapons] GunStrength=%.6f", mountedGunStrength);
        log.Write("CONFIG EFFECTIVE [MountedWeapons] HeavyStrength=%.6f", mountedHeavyStrength);
        log.Write("CONFIG EFFECTIVE [MountedWeapons] WaterStrength=%.6f", mountedWaterStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] BikeLeanStrength=%.6f", bikeLeanStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] BikePitchStrength=%.6f", bikePitchStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] BicyclePedalStrength=%.6f", bicyclePedalStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] BoatWaveStrength=%.6f", boatWaveStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] AircraftTurbulenceStrength=%.6f", aircraftTurbulenceStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] AircraftLandingStrength=%.6f", aircraftLandingStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] TrainJointStrength=%.6f", trainJointStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] SkidStrength=%.6f", skidStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] EngineVibrationStrength=%.6f", engineVibrationStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] RoadTextureStrength=%.6f", roadTextureStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] DoorOpenStrength=%.6f", doorOpenStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] MachineryStrength=%.6f", machineryStrength);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] MachineryDistance=%.6f", machineryDistance);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] NativeShakeBlend=%.6f", nativeShakeBlend);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] ExplosionShockDelay=%d", explosionShockDelayEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] ExplosionShockSpeed=%.6f", explosionShockSpeed);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] ExplosionShockSecondaryScale=%.6f", explosionShockSecondaryScale);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] ExplosionOcclusion=%d", explosionOcclusionEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] ExplosionOccludedScale=%.6f", explosionOccludedScale);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] ImpactSequencing=%d", impactSequencingEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] ImpactSequenceResetMs=%lu", static_cast<unsigned long>(impactSequenceResetMs));
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] ImpactSecondaryScale=%.6f", impactSecondaryScale);
        log.Write("CONFIG EFFECTIVE [AdvancedResponse] ImpactTertiaryScale=%.6f", impactTertiaryScale);
        log.Write("CONFIG EFFECTIVE [ResponseScales] Locomotion=%.6f", locomotionResponseScale);
        log.Write("CONFIG EFFECTIVE [ResponseScales] Combat=%.6f", combatResponseScale);
        log.Write("CONFIG EFFECTIVE [ResponseScales] Vehicle=%.6f", vehicleResponseScale);
        log.Write("CONFIG EFFECTIVE [ResponseScales] Environment=%.6f", environmentResponseScale);
        log.Write("CONFIG EFFECTIVE [ResponseScales] HighFrequency=%.6f", highFrequencyScale);
        log.Write("CONFIG EFFECTIVE [FrequencyBands] Enabled=%d", frequencyBandsEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [FrequencyBands] Low=%.6f", lowFrequencyScale);
        log.Write("CONFIG EFFECTIVE [FrequencyBands] Mid=%.6f", midFrequencyScale);
        log.Write("CONFIG EFFECTIVE [FrequencyBands] High=%.6f", highFrequencyBandScale);
        log.Write("CONFIG EFFECTIVE [ProceduralNoise] Enabled=%d", proceduralNoiseEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [ProceduralNoise] Mix=%.6f", proceduralNoiseMix);
        log.Write("CONFIG EFFECTIVE [Rotation] Enabled=%d", rotationEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [Rotation] SafetyScale=%.6f", rotationSafetyScale);
        log.Write("CONFIG EFFECTIVE [DamageResponse] Directional=%d", directionalDamageEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [DamageResponse] DirectionalWeight=%.6f", directionalDamageWeight);
        log.Write("CONFIG EFFECTIVE [DamageResponse] MeleeScale=%.6f", incomingMeleeDamageScale);
        log.Write("CONFIG EFFECTIVE [DamageResponse] BulletScale=%.6f", incomingBulletDamageScale);
        log.Write("CONFIG EFFECTIVE [DamageResponse] BulletMidPunch=%.6f", incomingBulletMidPunch);
        log.Write("CONFIG EFFECTIVE [DamageResponse] BulletSnap=%.6f", incomingBulletSnap);
        log.Write("CONFIG EFFECTIVE [DamageResponse] BulletRecovery=%.6f", incomingBulletRecovery);
        log.Write("CONFIG EFFECTIVE [DamageResponse] ExplosiveScale=%.6f", incomingExplosiveDamageScale);
        log.Write("CONFIG EFFECTIVE [DamageResponse] OtherScale=%.6f", incomingOtherDamageScale);
        log.Write("CONFIG EFFECTIVE [DamageResponse] MinSeverity=%.6f", incomingDamageMinSeverity);
        log.Write("CONFIG EFFECTIVE [DamageResponse] HighSnap=%.6f", incomingDamageHighSnap);
        log.Write("CONFIG EFFECTIVE [DamageResponse] NpcBulletHitFallback=%d", npcBulletHitFallbackEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [DamageResponse] NpcBulletHitRadius=%.6f", npcBulletHitRadius);
        log.Write("CONFIG EFFECTIVE [DamageResponse] NpcBulletHitFallbackScale=%.6f", npcBulletHitFallbackScale);
        log.Write("CONFIG EFFECTIVE [DamageResponse] NpcBulletHitCooldownMs=%lu", static_cast<unsigned long>(npcBulletHitFallbackCooldownMs));
        log.Write("CONFIG EFFECTIVE [CameraObstruction] OffsetCollision=%d", cameraOffsetCollisionEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [CameraObstruction] OffsetCollisionPadding=%.6f", cameraOffsetCollisionPadding);
        log.Write("CONFIG EFFECTIVE [MeleeWeapons] Enabled=%d", detailedMeleeWeapons ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [MeleeWeapons] BatClubScale=%.6f", batSwingScale);
        log.Write("CONFIG EFFECTIVE [MeleeWeapons] KnifeScale=%.6f", knifeSwingScale);
        log.Write("CONFIG EFFECTIVE [MeleeWeapons] ChainsawVibration=%.6f", chainsawVibration);
        log.Write("CONFIG EFFECTIVE [MeleeWeapons] HeavyScale=%.6f", heavyMeleeScale);
        log.Write("CONFIG EFFECTIVE [RecoilContext] Enabled=%d", contextualRecoilEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [RecoilContext] CrouchScale=%.6f", recoilCrouchScale);
        log.Write("CONFIG EFFECTIVE [RecoilContext] AimScale=%.6f", recoilAimScale);
        log.Write("CONFIG EFFECTIVE [RecoilContext] MovingScale=%.6f", recoilMovingScale);
        log.Write("CONFIG EFFECTIVE [RecoilContext] DualWieldScale=%.6f", recoilDualWieldScale);
        log.Write("CONFIG EFFECTIVE [ReloadResponse] Enabled=%d", reloadAnimationResponse ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [ReloadResponse] InsertScale=%.6f", reloadInsertScale);
        log.Write("CONFIG EFFECTIVE [ReloadResponse] ActionScale=%.6f", reloadActionScale);
        log.Write("CONFIG EFFECTIVE [VehicleDynamics] WheelResponse=%d", wheelResponseEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [VehicleDynamics] WheelCompressionStrength=%.6f", wheelCompressionStrength);
        log.Write("CONFIG EFFECTIVE [VehicleDynamics] TractionLossStrength=%.6f", tractionLossStrength);
        log.Write("CONFIG EFFECTIVE [VehicleDynamics] TractionRegainStrength=%.6f", tractionRegainStrength);
        log.Write("CONFIG EFFECTIVE [Developer] RuntimeReload=%d", runtimeReloadEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [Developer] ReloadKey=%ld", static_cast<long>(runtimeReloadKey));
        log.Write("CONFIG EFFECTIVE [Developer] ReloadDebounceMs=%lu", static_cast<unsigned long>(runtimeReloadDebounceMs));
        log.Write("CONFIG EFFECTIVE [Developer] DetailedHUD=%d", detailedDebugHud ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [General] TransitionsEnabled=%d", transitionsEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [General] MeleeEnabled=%d", meleeEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [General] BreathingEnabled=%d", breathingEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [General] SurfaceStepsEnabled=%d", surfaceStepsEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [Vehicle] Enabled=%d", vehicleEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [Vehicle] Strength=%.6f", vehicleStrength);
        log.Write("CONFIG EFFECTIVE [Vehicle] AccelerationWeight=%.6f", vehicleAccelForward);
        log.Write("CONFIG EFFECTIVE [Vehicle] BrakingWeight=%.6f", vehicleBrakeForward);
        log.Write("CONFIG EFFECTIVE [Vehicle] CorneringWeight=%.6f", vehicleCornerSide);
        log.Write("CONFIG EFFECTIVE [Vehicle] VerticalWeight=%.6f", vehicleVerticalAccel);
        log.Write("CONFIG EFFECTIVE [Vehicle] SpeedVibration=%.6f", vehicleSpeedVibration);
        log.Write("CONFIG EFFECTIVE [Vehicle] SpeedVibrationRate=%.6f", vehicleSpeedVibrationRate);
        log.Write("CONFIG EFFECTIVE [VehicleHorn] Enabled=%d", vehicleHornEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [VehicleHorn] Vibration=%.6f", vehicleHornVibration);
        log.Write("CONFIG EFFECTIVE [VehicleHorn] Rate=%.6f", vehicleHornVibrationRate);
        log.Write("CONFIG EFFECTIVE [VehicleHorn] Attack=%.6f", vehicleHornAttack);
        log.Write("CONFIG EFFECTIVE [VehicleHorn] Release=%.6f", vehicleHornRelease);
        log.Write("CONFIG EFFECTIVE [Vehicle] ImpactThreshold=%.6f", vehicleImpactThreshold);
        log.Write("CONFIG EFFECTIVE [Vehicle] ImpactImpulse=%.6f", vehicleImpactImpulse);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] ContactResponse=%d", vehicleContactResponseEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] ContactStrength=%.6f", vehicleContactStrength);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] ContactMinSpeed=%.6f", vehicleContactMinSpeed);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] ContactDeltaVelocityThreshold=%.6f", vehicleContactDeltaVelocityThreshold);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] ContactStrongDeltaVelocity=%.6f", vehicleContactStrongDeltaVelocity);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] ContactDamageReference=%.6f", vehicleContactDamageReference);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] ContactSpring=%.6f", vehicleContactSpring);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] ContactDamping=%.6f", vehicleContactDamping);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] ContactDirectKick=%.6f", vehicleContactDirectKick);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] ContactKickDecay=%.6f", vehicleContactKickDecay);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] ContactCooldownMs=%lu", static_cast<unsigned long>(vehicleContactCooldownMs));
        log.Write("CONFIG EFFECTIVE [VehicleCrash] DamageResponse=%d", vehicleCrashDamageEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] DamageThreshold=%.6f", vehicleCrashDamageThreshold);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] DamageReference=%.6f", vehicleCrashDamageReference);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] DamageStrength=%.6f", vehicleCrashDamageStrength);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] FrontMultiplier=%.6f", vehicleCrashFrontMultiplier);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] RearMultiplier=%.6f", vehicleCrashRearMultiplier);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] SideMultiplier=%.6f", vehicleCrashSideMultiplier);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] VerticalMultiplier=%.6f", vehicleCrashVerticalMultiplier);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] CatastrophicThreshold=%.6f", vehicleCrashCatastrophicThreshold);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] CatastrophicMultiplier=%.6f", vehicleCrashCatastrophicMultiplier);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] PhysicsWindowMs=%lu", static_cast<unsigned long>(vehicleCrashPhysicsWindowMs));
        log.Write("CONFIG EFFECTIVE [VehicleCrash] SuspensionResponse=%d", vehicleSuspensionEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] SuspensionThreshold=%.6f", vehicleSuspensionThreshold);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] SuspensionStrength=%.6f", vehicleSuspensionStrength);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] AdaptiveHandling=%d", vehicleSuspensionAdaptiveHandling ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [VehicleCrash] HandlingInfluence=%.6f", vehicleSuspensionHandlingInfluence);
        log.Write("CONFIG EFFECTIVE [Vehicle] Spring=%.6f", vehicleSpring);
        log.Write("CONFIG EFFECTIVE [Vehicle] Damping=%.6f", vehicleDamping);
        log.Write("CONFIG EFFECTIVE [Vehicle] MaxSide=%.6f", vehicleMaxSide);
        log.Write("CONFIG EFFECTIVE [Vehicle] MaxVertical=%.6f", vehicleMaxVertical);
        log.Write("CONFIG EFFECTIVE [Vehicle] MaxForward=%.6f", vehicleMaxForward);
        log.Write("CONFIG EFFECTIVE [VehicleProfiles] Automobile=%.6f", vehicleAutomobileScale);
        log.Write("CONFIG EFFECTIVE [VehicleProfiles] Bike=%.6f", vehicleBikeScale);
        log.Write("CONFIG EFFECTIVE [VehicleProfiles] Boat=%.6f", vehicleBoatScale);
        log.Write("CONFIG EFFECTIVE [VehicleProfiles] Helicopter=%.6f", vehicleHeliScale);
        log.Write("CONFIG EFFECTIVE [VehicleProfiles] Plane=%.6f", vehiclePlaneScale);
        log.Write("CONFIG EFFECTIVE [VehicleProfiles] Other=%.6f", vehicleOtherScale);
        log.Write("CONFIG EFFECTIVE [Alive] IdleSide=%.6f", idleSide);
        log.Write("CONFIG EFFECTIVE [Alive] IdleVertical=%.6f", idleVertical);
        log.Write("CONFIG EFFECTIVE [Alive] IdleForward=%.6f", idleForward);
        log.Write("CONFIG EFFECTIVE [Alive] SettleDelay=%.6f", idleSettleDelay);
        log.Write("CONFIG EFFECTIVE [Alive] SettledScale=%.6f", idleSettledScale);
        log.Write("CONFIG EFFECTIVE [Alive] AimBreathingSide=%.6f", aimBreathingSide);
        log.Write("CONFIG EFFECTIVE [Alive] AimBreathingVertical=%.6f", aimBreathingVertical);
        log.Write("CONFIG EFFECTIVE [Alive] AimBreathingForward=%.6f", aimBreathingForward);
        log.Write("CONFIG EFFECTIVE [Alive] AimBreathingRate=%.6f", aimBreathingRate);
        log.Write("CONFIG EFFECTIVE [Gait] Side=%.6f", gaitSide);
        log.Write("CONFIG EFFECTIVE [Gait] Vertical=%.6f", gaitVertical);
        log.Write("CONFIG EFFECTIVE [Gait] Forward=%.6f", gaitForward);
        log.Write("CONFIG EFFECTIVE [Gait] Fallback=%.6f", fallbackGait);
        log.Write("CONFIG EFFECTIVE [Gait] StepImpulse=%.6f", stepImpulse);
        log.Write("CONFIG EFFECTIVE [SurfaceSteps] Road=%.6f", surfaceRoad);
        log.Write("CONFIG EFFECTIVE [SurfaceSteps] Concrete=%.6f", surfaceConcrete);
        log.Write("CONFIG EFFECTIVE [SurfaceSteps] Gravel=%.6f", surfaceGravel);
        log.Write("CONFIG EFFECTIVE [SurfaceSteps] Grass=%.6f", surfaceGrass);
        log.Write("CONFIG EFFECTIVE [SurfaceSteps] Dirt=%.6f", surfaceDirt);
        log.Write("CONFIG EFFECTIVE [SurfaceSteps] Sand=%.6f", surfaceSand);
        log.Write("CONFIG EFFECTIVE [SurfaceSteps] Wood=%.6f", surfaceWood);
        log.Write("CONFIG EFFECTIVE [SurfaceSteps] Metal=%.6f", surfaceMetal);
        log.Write("CONFIG EFFECTIVE [SurfaceSteps] Rock=%.6f", surfaceRock);
        log.Write("CONFIG EFFECTIVE [SurfaceSteps] Misc=%.6f", surfaceMisc);
        log.Write("CONFIG EFFECTIVE [Look] YawInertia=%.6f", lookYaw);
        log.Write("CONFIG EFFECTIVE [Look] PitchInertia=%.6f", lookPitch);
        log.Write("CONFIG EFFECTIVE [Look] TurnInput=%.6f", turnInput);
        log.Write("CONFIG EFFECTIVE [Look] AccelerationInertia=%.6f", accelInertia);
        log.Write("CONFIG EFFECTIVE [Movement] DirectionalInertia=%.6f", directionalInertia);
        log.Write("CONFIG EFFECTIVE [Movement] StrafeSway=%.6f", strafeSway);
        log.Write("CONFIG EFFECTIVE [Movement] BackwardSway=%.6f", backwardSway);
        log.Write("CONFIG EFFECTIVE [Impact] JumpImpulse=%.6f", jumpImpulse);
        log.Write("CONFIG EFFECTIVE [Impact] LandingImpulse=%.6f", landingImpulse);
        log.Write("CONFIG EFFECTIVE [Impact] HardLandingScale=%.6f", hardLandingScale);
        log.Write("CONFIG EFFECTIVE [Impact] DamageImpulse=%.6f", damageImpulse);
        log.Write("CONFIG EFFECTIVE [Impact] DamageSideImpulse=%.6f", damageSideImpulse);
        log.Write("CONFIG EFFECTIVE [Impact] ExplosionImpulse=%.6f", explosionImpulse);
        log.Write("CONFIG EFFECTIVE [Impact] ExplosionRadius=%.6f", explosionRadius);
        log.Write("CONFIG EFFECTIVE [Impact] MaxImpactOffset=%.6f", maxImpactOffset);
        log.Write("CONFIG EFFECTIVE [Impact] MaxFinalSide=%.6f", maxFinalSide);
        log.Write("CONFIG EFFECTIVE [Impact] MaxFinalVertical=%.6f", maxFinalVertical);
        log.Write("CONFIG EFFECTIVE [Impact] MaxFinalForward=%.6f", maxFinalForward);
        log.Write("CONFIG EFFECTIVE [Impact] MeleeImpact=%.6f", meleeImpact);
        log.Write("CONFIG EFFECTIVE [Impact] MeleeSide=%.6f", meleeSide);
        log.Write("CONFIG EFFECTIVE [Melee] DetailedPlayerResponse=%d", detailedPlayerMelee ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [Melee] PunchWindup=%.6f", punchWindup);
        log.Write("CONFIG EFFECTIVE [Melee] PunchRelease=%.6f", punchRelease);
        log.Write("CONFIG EFFECTIVE [Melee] PunchContact=%.6f", punchContact);
        log.Write("CONFIG EFFECTIVE [Melee] PunchSide=%.6f", punchSide);
        log.Write("CONFIG EFFECTIVE [Melee] PunchVertical=%.6f", punchVertical);
        log.Write("CONFIG EFFECTIVE [Melee] PunchReleasePhase=%.6f", punchReleasePhase);
        log.Write("CONFIG EFFECTIVE [Melee] KickWindup=%.6f", kickWindup);
        log.Write("CONFIG EFFECTIVE [Melee] KickRelease=%.6f", kickRelease);
        log.Write("CONFIG EFFECTIVE [Melee] KickContact=%.6f", kickContact);
        log.Write("CONFIG EFFECTIVE [Melee] KickSide=%.6f", kickSide);
        log.Write("CONFIG EFFECTIVE [Melee] KickVertical=%.6f", kickVertical);
        log.Write("CONFIG EFFECTIVE [Melee] KickReleasePhase=%.6f", kickReleasePhase);
        log.Write("CONFIG EFFECTIVE [Melee] ComboStep=%.6f", meleeComboStep);
        log.Write("CONFIG EFFECTIVE [Melee] ComboMaxScale=%.6f", meleeComboMaxScale);
        log.Write("CONFIG EFFECTIVE [Rotation] IdlePitchDeg=%.6f", idlePitchDeg);
        log.Write("CONFIG EFFECTIVE [Rotation] IdleRollDeg=%.6f", idleRollDeg);
        log.Write("CONFIG EFFECTIVE [Rotation] GaitPitchDeg=%.6f", gaitPitchDeg);
        log.Write("CONFIG EFFECTIVE [Rotation] GaitRollDeg=%.6f", gaitRollDeg);
        log.Write("CONFIG EFFECTIVE [Rotation] TurnRollDeg=%.6f", turnRollDeg);
        log.Write("CONFIG EFFECTIVE [Rotation] LookYawDeg=%.6f", lookYawDeg);
        log.Write("CONFIG EFFECTIVE [Rotation] LookPitchDeg=%.6f", lookPitchDeg);
        log.Write("CONFIG EFFECTIVE [Rotation] MaxPitchDeg=%.6f", maxPitchDeg);
        log.Write("CONFIG EFFECTIVE [Rotation] MaxRollDeg=%.6f", maxRollDeg);
        log.Write("CONFIG EFFECTIVE [Rotation] MaxYawDeg=%.6f", maxYawDeg);
        log.Write("CONFIG EFFECTIVE [Recoil] Enabled=%d", recoilEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [Recoil] Strength=%.6f", recoilStrength);
        log.Write("CONFIG EFFECTIVE [Recoil] VerticalKick=%.6f", recoilVerticalKick);
        log.Write("CONFIG EFFECTIVE [Recoil] ForwardKick=%.6f", recoilForwardKick);
        log.Write("CONFIG EFFECTIVE [Recoil] SideKick=%.6f", recoilSideKick);
        log.Write("CONFIG EFFECTIVE [Recoil] PositionVertical=%.6f", recoilPositionVertical);
        log.Write("CONFIG EFFECTIVE [Recoil] PositionForward=%.6f", recoilPositionForward);
        log.Write("CONFIG EFFECTIVE [Recoil] PositionSide=%.6f", recoilPositionSide);
        log.Write("CONFIG EFFECTIVE [Recoil] VelocityVertical=%.6f", recoilVelocityVertical);
        log.Write("CONFIG EFFECTIVE [Recoil] VelocityForward=%.6f", recoilVelocityForward);
        log.Write("CONFIG EFFECTIVE [Recoil] VelocitySide=%.6f", recoilVelocitySide);
        log.Write("CONFIG EFFECTIVE [RecoilProfiles] Pistol=%.6f", recoilPistol);
        log.Write("CONFIG EFFECTIVE [RecoilProfiles] DesertEagle=%.6f", recoilDeagle);
        log.Write("CONFIG EFFECTIVE [RecoilProfiles] SMG=%.6f", recoilSmg);
        log.Write("CONFIG EFFECTIVE [RecoilProfiles] Rifle=%.6f", recoilRifle);
        log.Write("CONFIG EFFECTIVE [RecoilProfiles] Shotgun=%.6f", recoilShotgun);
        log.Write("CONFIG EFFECTIVE [RecoilProfiles] Sniper=%.6f", recoilSniper);
        log.Write("CONFIG EFFECTIVE [RecoilProfiles] Launcher=%.6f", recoilLauncher);
        log.Write("CONFIG EFFECTIVE [RecoilProfiles] Flamethrower=%.6f", recoilFlamethrower);
        log.Write("CONFIG EFFECTIVE [RecoilProfiles] Minigun=%.6f", recoilMinigun);
        log.Write("CONFIG EFFECTIVE [Recoil] FirstShotMultiplier=%.6f", firstShotMultiplier);
        log.Write("CONFIG EFFECTIVE [Recoil] SustainedShotMultiplier=%.6f", sustainedShotMultiplier);
        log.Write("CONFIG EFFECTIVE [Recoil] SustainedResetMs=%lu", static_cast<unsigned long>(sustainedResetMs));
        log.Write("CONFIG EFFECTIVE [Recoil] MinIntervalMs=%lu", static_cast<unsigned long>(recoilMinIntervalMs));
        log.Write("CONFIG EFFECTIVE [Smoothing] Spring=%.6f", spring);
        log.Write("CONFIG EFFECTIVE [Smoothing] Damping=%.6f", damping);
        log.Write("CONFIG EFFECTIVE [Smoothing] LookSpring=%.6f", lookSpring);
        log.Write("CONFIG EFFECTIVE [Smoothing] LookDamping=%.6f", lookDamping);
        log.Write("CONFIG EFFECTIVE [Smoothing] StateResponse=%.6f", stateResponse);
        log.Write("CONFIG EFFECTIVE [Smoothing] FootPhaseResponse=%.6f", footPhaseResponse);
        log.Write("CONFIG EFFECTIVE [Smoothing] RecoilSpringMultiplier=%.6f", recoilSpringMultiplier);
        log.Write("CONFIG EFFECTIVE [Smoothing] RecoilDampingMultiplier=%.6f", recoilDampingMultiplier);
        log.Write("CONFIG EFFECTIVE [Smoothing] IdleDampingMultiplier=%.6f", idleDampingMultiplier);
        log.Write("CONFIG EFFECTIVE [Debug] Enabled=%d", debug ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [Debug] LogEveryMs=%lu", static_cast<unsigned long>(logEveryMs));
        log.Write("CONFIG EFFECTIVE [Debug] LogStepEvents=%d", logSteps ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [Debug] LogShotEvents=%d", logShots ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [Debug] OverlayEnabled=%d", overlayEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [Debug] OverlayScale=%.3f", overlayScale);
        log.Write("CONFIG EFFECTIVE [DebugAudit] Enabled=%d", auditEnabled ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [DebugAudit] LogConfigValues=%d", auditConfigValues ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [DebugAudit] LogFunctionCalls=%d", auditFunctionCalls ? 1 : 0);
        log.Write("CONFIG EFFECTIVE [DebugAudit] SummaryEveryMs=%lu", static_cast<unsigned long>(auditEveryMs));
        log.Write("CONFIG EFFECTIVE [Performance] NearbyVehicleScanMs=%lu", static_cast<unsigned long>(nearbyVehicleScanMs));
        log.Write("CONFIG EFFECTIVE [Performance] SurfaceScanMs=%lu", static_cast<unsigned long>(surfaceScanMs));
        log.Write("CONFIG EFFECTIVE [General] Preset=%s", presetName);
        log.Write(
            "PRESET CHARACTER move=%.2f combat=%.2f vehicle=%.2f world=%.2f "
            "fov=%.2f ambient=%.2f impact=%.2f recoil=%.2f melee=%.2f "
            "landing=%.2f rotation=%.2f recovery=%.2f",
            presetMovement,
            presetCombat,
            presetVehicle,
            presetWorld,
            presetFov,
            presetAmbient,
            presetImpact,
            presetRecoil,
            presetMelee,
            presetLanding,
            presetRotation,
            presetRecovery
        );
        for (int id = 22; id <= 38; ++id) {
            log.Write("CONFIG EFFECTIVE [WeaponRecoil] Weapon%dScale=%.6f", id, weaponScale[id]);
            log.Write("CONFIG EFFECTIVE [WeaponRecoil] Weapon%dVertical=%.6f", id, weaponVertical[id]);
            log.Write("CONFIG EFFECTIVE [WeaponRecoil] Weapon%dForward=%.6f", id, weaponForward[id]);
            log.Write("CONFIG EFFECTIVE [WeaponRecoil] Weapon%dSide=%.6f", id, weaponSide[id]);
            log.Write("CONFIG EFFECTIVE [WeaponRecoil] Weapon%dRecovery=%.6f", id, weaponRecovery[id]);
        }
        log.Write("CONFIG AUDIT END effective-values");
    }

};

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

// GetDpiForWindow is optional on older Windows; query it dynamically.
static unsigned GetOverlayWindowDpi() {
    static unsigned cachedDpi = 96;
    static DWORD lastProbe = 0;
    const DWORD now = GetTickCount();
    if (lastProbe != 0 && now - lastProbe < 1000u)
        return cachedDpi;
    lastProbe = now;

    HWND hwnd = RsGlobal.ps ? reinterpret_cast<HWND>(RsGlobal.ps->window) : nullptr;
    if (!hwnd || !IsWindow(hwnd))
        return cachedDpi;

    using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
    const HMODULE user32 = GetModuleHandleA("user32.dll");
    const auto getDpi = user32
        ? reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"))
        : nullptr;
    UINT dpi = getDpi ? getDpi(hwnd) : 0;
    if (dpi == 0) {
        HDC dc = GetDC(hwnd);
        if (dc) {
            dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSY));
            ReleaseDC(hwnd, dc);
        }
    }
    cachedDpi = (dpi >= 72 && dpi <= 480) ? dpi : 96u;
    return cachedDpi;
}

class CameraResponse {
public:
    void InitializeRuntime(const char* source) {
        if (runtimeInitialized_ || runtimeInitAttempted_)
            return;

        runtimeInitAttempted_ = true;

        cfg_.dir = Config::ModuleDirectory();
        cfg_.Load();

        // Keep the config path tied to the loaded ASI, not GTA's working directory.
        if (cfg_.dir.empty() || cfg_.iniPath.empty()) {
            OutputDebugStringA("CameraResponse: cannot resolve loaded ASI directory.\n");
            runtimeInitAttempted_ = false;
            return;
        }

        log_.Init(cfg_.dir, cfg_.debug || cfg_.auditEnabled);
        const DWORD iniAttributes = GetFileAttributesA(cfg_.iniPath.c_str());
        const bool iniPresent = iniAttributes != INVALID_FILE_ATTRIBUTES &&
            !(iniAttributes & FILE_ATTRIBUTE_DIRECTORY);

        log_.WriteImportant(
            "START event=%s asiDir=%s ini=%s iniPresent=%d log=%s",
            source, cfg_.dir.c_str(), cfg_.iniPath.c_str(),
            iniPresent ? 1 : 0, log_.Path().c_str()
        );
        log_.WriteImportant(
            "CONFIG enabled=%d debug=%d audit=%d preset=%s",
            cfg_.enabled ? 1 : 0, cfg_.debug ? 1 : 0,
            cfg_.auditEnabled ? 1 : 0, cfg_.presetName
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

        // Hook only after a game callback; unsupported executables must not be patched.
        InstallCameraCommitHook();
        if (cameraCommitHookInstalled_)
            InstallWaterCannonHook();

        runtimeInitialized_ = true;

        log_.WriteImportant(
            "READY cameraHook=%d enabled=%d (0 means no camera effects; check game version or hook conflicts)",
            cameraCommitHookInstalled_ ? 1 : 0, cfg_.enabled ? 1 : 0
        );
    }

    CameraResponse() {
        instance_ = this;
        OutputDebugStringA("CameraResponse: ASI loaded; waiting for game callbacks.\n");

        // Register callbacks during static construction; defer I/O, validation and patching beyond
        // loader lock.
        Events::gameProcessEvent += [] {
            if (instance_)
                instance_->OnGameProcessEvent();
        };

        Events::drawingEvent += [] {
            if (instance_)
                instance_->OnDrawingEvent();
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

    void OnGameProcessEvent() {
        if (!runtimeInitialized_)
            InitializeRuntime("gameProcessEvent");
        if (!runtimeInitialized_)
            return;

        ++primaryCallbackCount_;
        if (primaryCallbackCount_ == 1)
            log_.WriteImportant("GAME PROCESS callback active frame=%u", CTimer::m_FrameCounter);
        const cr::UpdateDispatch::Source before = updateDispatch_.ActiveSource();
        if (!updateDispatch_.OnGameProcess(CTimer::m_FrameCounter))
            return;

        if (before == cr::UpdateDispatch::Source::DrawingFallback)
            log_.WriteImportant("UPDATE SOURCE recovered=gameProcessEvent frame=%u", CTimer::m_FrameCounter);
        GameUpdate();
    }

    void OnDrawingEvent() {
        if (!runtimeInitialized_)
            InitializeRuntime("drawingEvent");
        if (!runtimeInitialized_)
            return;

        ++drawingCallbackCount_;
        // Do not invoke game simulation while the main menu, pause menu,
        // or character/world are unavailable.
        const bool gameplayReady =
            !CTimer::m_UserPause && !CTimer::m_CodePause &&
            !FrontEndMenuManager.m_bMenuActive &&
            CTimer::ms_fTimeStep > 0.0f &&
            FindPlayerPed() != nullptr &&
            static_cast<unsigned>(TheCamera.m_nActiveCam) <
                sizeof(TheCamera.m_aCams) / sizeof(TheCamera.m_aCams[0]);

        const cr::UpdateDispatch::Source previousSource = updateDispatch_.ActiveSource();
        const bool fallbackTick =
            updateDispatch_.OnDrawing(CTimer::m_FrameCounter, gameplayReady);
        if (fallbackTick) {
            if (previousSource != cr::UpdateDispatch::Source::DrawingFallback) {
                log_.WriteImportant(
                    "UPDATE SOURCE drawingEvent fallback enabled frame=%u primaryCallbacks=%llu",
                    CTimer::m_FrameCounter,
                    static_cast<unsigned long long>(primaryCallbackCount_)
                );
            }
            ++fallbackUpdateCount_;
            GameUpdate();
        }

        const DWORD now = GetTickCount();
        if (lastDrawingHeartbeatMs_ == 0 || now - lastDrawingHeartbeatMs_ >= 5000u) {
            lastDrawingHeartbeatMs_ = now;
            log_.WriteImportant(
                "DRAW HEARTBEAT frame=%u draws=%llu processCalls=%llu updates=%llu "
                "fallbackUpdates=%llu gameplayReady=%d source=%s "
                "prepared=%llu commit=%llu applied=%llu gate=%s",
                CTimer::m_FrameCounter,
                static_cast<unsigned long long>(drawingCallbackCount_),
                static_cast<unsigned long long>(primaryCallbackCount_),
                static_cast<unsigned long long>(updates_),
                static_cast<unsigned long long>(fallbackUpdateCount_),
                gameplayReady ? 1 : 0,
                UpdateSourceName(),
                static_cast<unsigned long long>(prepared_),
                static_cast<unsigned long long>(commitCalls_),
                static_cast<unsigned long long>(commitApplied_),
                lastGate_.empty() ? "-" : lastGate_.c_str()
            );
        }
        RenderDebugOverlay();
    }

    const char* UpdateSourceName() const {
        switch (updateDispatch_.ActiveSource()) {
        case cr::UpdateDispatch::Source::GameProcess: return "GAME";
        case cr::UpdateDispatch::Source::DrawingFallback: return "DRAW-FALLBACK";
        default: return "WAIT";
        }
    }

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
        if (!transientEvents_.Publish(event))
            ++transientEventsDropped_;
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

    // Camera copy signature shared by hook and original-function pointer.
    using CopyCameraFn = void(__thiscall*)(CCamera*, bool);


    MotionState motion_ = MotionState::Disabled;

    FootState leftFoot_;
    FootState rightFoot_;

    MotionSpring gaitSpring_;
    MotionSpring lookSpring_;
    MotionSpring recoilSpring_;
    MotionSpring impactSpring_;

    // Common event mixer aggregates legacy/new layers and diagnostic telemetry.
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

    bool pendingAllowed_ = false;
    float pendingSide_ = 0.0f;
    float pendingVertical_ = 0.0f;
    float pendingForward_ = 0.0f;

    float pendingPitchRad_ = 0.0f;
    float pendingRollRad_ = 0.0f;
    float pendingYawRad_ = 0.0f;

    // US 1.0 camera commit: CCamera::Process -> CopyCameraMatrixToRWCam (CALL 0x52C98B).
    static CopyCameraFn originalCopyCamera_;

    bool cameraCommitHookInstalled_ = false;
    uintptr_t previousCopyTarget_ = 0;
    unsigned lastCameraHookCheckMs_ = 0;
    unsigned cameraHookRestoreCount_ = 0;

    bool runtimeInitialized_ = false;
    bool runtimeInitAttempted_ = false;
    unsigned lastStatusMs_ = 0;
    std::string lastGate_;

    cr::UpdateDispatch updateDispatch_;
    uint64_t primaryCallbackCount_ = 0;
    uint64_t drawingCallbackCount_ = 0;
    uint64_t fallbackUpdateCount_ = 0;
    DWORD lastDrawingHeartbeatMs_ = 0;

    int overlayWidth_ = 0;
    int overlayHeight_ = 0;
    unsigned overlayDpi_ = 0;
    float overlayScaleLogged_ = 0.0f;
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

        // Vehicle basis: right=+X, forward=+Y; convert planar motion, heave separately.
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

        // Use reconciled targeting state for mapped controller lock-on and aiming tasks.
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
        // Unknown or modded surface material IDs use neutral handling.
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
        // Hard surfaces transmit stronger vibration; soft surfaces absorb it.
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

        // Stock firearm/launcher IDs 22..38; ignore melee/tools/thrown weapons.
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

                // Infer dual wield via weapon animation metadata, not private skill fields.
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

        // Bound multishot gain so large ammo deltas cannot produce excessive recoil.
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

        // Direct displacement gives instant recoil; velocity creates damped recovery without rotating
        // GTA's matrix.
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

        // Per-shot Kick values add explicit impulses alongside weapon profiles.
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

        // Clamp per-shot state even when modded weapon data is malformed.
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

        // Reject duplicate touchdown minima from the same planted foot or frame.
        unsigned minGlobalIntervalMs = 105u;
        if (motion_ == MotionState::Run)
            minGlobalIntervalMs = 82u;
        else if (motion_ == MotionState::Sprint)
            minGlobalIntervalMs = 68u;

        if (lastAnyStepMs_ != 0 &&
            now - lastAnyStepMs_ < minGlobalIntervalMs) {
            return;
        }

        // Require alternating feet within a stride; allow same-foot recovery after a long gap.
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

        // Latch confirmed flight until ground contact; air flags can lag at touchdown/apex.
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
            // Skip jump impulse on bail-out; the ejection response already covers it.
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

            // Typical logged jump impact speed ~0.17 (GTA units); avoid under-scaling.
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

                // Blend fall speed with horizontal momentum to capture fast tumbles.
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

        // Expire stale bail state if scripts leave airborne flags inconsistent.
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

            // Use numeric GTA SA weapon families for plugin-sdk header compatibility.
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

            float sideDir = RandomSigned() * 0.35f;
            float forwardDir = -1.0f;
            float verticalDir = meleeHit ? -0.34f : -0.20f;
            bool directional = false;

            // Damage impulse must be camera-local; player heading differs from an orbited camera.
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

                            // Push away from the source in the horizontal plane; vertical response is
                            // separate.
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

            // Melee: body shove; bullets: sharp snap; explosives: heavy displacement.
            float bodyImpulse = cfg_.damageImpulse * severity;
            float sideImpulse = cfg_.damageSideImpulse * severity;

            if (meleeHit) {
                bodyImpulse *= 1.08f;
                sideImpulse *= 1.18f;
                verticalDir -= 0.08f;
            } else if (bulletHit) {
                // Keep gunshot base spring small; mid/high-band impulses provide the snap.
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

            // Add immediate snap so hits remain visible atop existing spring motion.
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

        // Track every NPC shot; final displacement clamps enforce safety.
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

            // Prefer consumed clip ammo to count shots.
            unsigned rounds = oldAmmo > newAmmo ? oldAmmo - newAmmo : 0u;

            // Fallback to advancing cooldown or firing edge when ammo stays unchanged.
            const bool nextShotAdvanced =
                nextShot != 0 &&
                nextShot != oldNextShot &&
                nextShot > oldNextShot;

            const bool firingNow = state == WEAPONSTATE_FIRING;
            const bool firingEdge =
                firingNow && oldState != WEAPONSTATE_FIRING;

            if (rounds == 0 && (nextShotAdvanced || firingEdge) && firingNow)
                rounds = 1;

            // Clamp shot count from corrupt/modded ammo data.
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

                            // Task target is aiming intent, not actual shot trajectory; aiming at CJ
                            // proves neither hit nor miss.
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
                        // Use scripted-fire fallback only when no gun-task target exists.
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

                        // When damage callbacks miss a shot, infer a hit-like event from CJ's aim
                        // corridor; avoid double impulses.
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

                            // Convert attacker direction to camera-local space, independent of CJ's
                            // heading.
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

        // Door slams also emit a short mid-band pulse to remain perceptible.
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

                        // Prefer door angular velocity; use angle delta for scripted doors with zeroed
                        // physics velocity.
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

                            // Use door location rather than vehicle center for left/right direction.
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

                    // Latch door classification; bIsDoorMoving can clear before closure.
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

                // Retain the faster velocity source across the bInVehicle transition.
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

                // If already airborne when bInVehicle clears, confirm bailout immediately.
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

            // Stroke gives vertical cadence; actual swim velocity gives direction.
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

            // Sustained flight keeps a directional load cue.
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

            // Include steady climb/descent thrust; acceleration alone vanishes at constant speed.
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


            // Use minimal hover texture, not constant jetpack shake.
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

        // Ignore stale native shake force; it must not drive perpetual oscillation.
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

        // A nearly unchanged nonzero native shake force is also stale.
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

        // Scale subtle rain texture with GTA's actual rain blend.
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

        // Sandstorm texture follows wind direction at a slower cadence than rain.
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

    void UpdateLargeVehicleProximity(CPlayerPed* player) {
        AuditHit(AUD_UpdateLargeVehicleProximity);
        if (!cfg_.largeVehicleResponseEnabled || !player) {
            largeVehicleSide_ = 0.0f;
            largeVehicleVertical_ = 0.0f;
            largeVehicleForward_ = 0.0f;
            return;
        }

        const unsigned now = CTimer::m_snTimeInMilliseconds;
        if (lastLargeVehicleScanMs_ != 0 &&
            now - lastLargeVehicleScanMs_ < cfg_.largeVehicleScanMs) {
            return;
        }
        lastLargeVehicleScanMs_ = now;

        largeVehicleSide_ = 0.0f;
        largeVehicleVertical_ = 0.0f;
        largeVehicleForward_ = 0.0f;

        auto* pool = CPools::ms_pVehiclePool;
        if (!pool)
            return;

        const CVector& pp = player->GetPosition();
        const float t = static_cast<float>(now) * 0.001f;
        bool aircraftSeenThisScan = false;

        for (int i = 0; i < pool->m_nSize; ++i) {
            CVehicle* v = pool->GetAt(i);
            if (!v || v->m_fHealth <= 0.0f)
                continue;

            const int model = v->m_nModelIndex;
            const bool train = CModelInfo::IsTrainModel(model);
            const bool heli = CModelInfo::IsHeliModel(model);
            const bool plane = CModelInfo::IsPlaneModel(model);

            if (!train && !heli && !plane)
                continue;

            if ((heli || plane) && !v->bEngineOn)
                continue;

            const CVector& vp = v->GetPosition();
            const float dx = vp.x - pp.x;
            const float dy = vp.y - pp.y;
            const float dz = vp.z - pp.z;
            const float distSq = dx * dx + dy * dy + dz * dz;

            const float maxD =
                train ? cfg_.trainResponseDistance : cfg_.aircraftResponseDistance;

            if (distSq > maxD * maxD)
                continue;

            if (heli || plane) {
                aircraftSeenThisScan = true;
                aircraftLastSeenMs_ = now;

                if (aircraftContinuousSinceMs_ == 0)
                    aircraftContinuousSinceMs_ = now;

                if (cfg_.aircraftMaxContinuousMs > 0 &&
                    now - aircraftContinuousSinceMs_ >= cfg_.aircraftMaxContinuousMs) {
                    if (!aircraftResponseSuppressed_ && cfg_.debug) {
                        log_.Write(
                            "AIRCRAFT RESPONSE SUPPRESSED continuousMs=%u model=%d heli=%d",
                            now - aircraftContinuousSinceMs_,
                            model,
                            heli ? 1 : 0
                        );
                    }
                    aircraftResponseSuppressed_ = true;
                }

                if (aircraftResponseSuppressed_)
                    continue;
            }

            const float dist = std::sqrt(distSq);
            const float falloff =
                1.0f - Clamp(dist / std::max(maxD, 1.0f), 0.0f, 1.0f);

            const float speed =
                std::sqrt(
                    v->m_vecMoveSpeed.x * v->m_vecMoveSpeed.x +
                    v->m_vecMoveSpeed.y * v->m_vecMoveSpeed.y +
                    v->m_vecMoveSpeed.z * v->m_vecMoveSpeed.z
                );

            const float distSafe=std::max(dist,0.001f);
            const float radialVelocity=
                (v->m_vecMoveSpeed.x*dx+
                 v->m_vecMoveSpeed.y*dy+
                 v->m_vecMoveSpeed.z*dz)/distSafe;
            const float approach=
                Clamp(-radialVelocity*3.0f,0.0f,1.0f);
            const float tangentialSpeed=
                std::sqrt(
                    std::max(
                        speed*speed-
                        radialVelocity*radialVelocity,
                        0.0f
                    )
                );
            const float passActivity=
                Clamp(tangentialSpeed*3.0f,0.0f,1.0f);
            const float passageBoost=
                1.0f+
                approach*cfg_.nearbyApproachInfluence+
                passActivity*cfg_.nearbyPassInfluence*0.55f;

            float localSide = 0.0f;
            float localForward = 0.0f;
            WorldXYToCameraLocal(
                dx,
                dy,
                localSide,
                localForward
            );
            const float dirLen = std::max(
                std::sqrt(
                    localSide * localSide +
                    localForward * localForward +
                    dz * dz
                ),
                0.001f
            );

            const float sideBias =
                cfg_.directionalResponseEnabled
                    ? Clamp(-localSide / dirLen, -1.0f, 1.0f) *
                      cfg_.directionalLargeVehicleScale
                    : 0.0f;

            const float forwardBias =
                cfg_.directionalResponseEnabled
                    ? Clamp(-localForward / dirLen, -1.0f, 1.0f) *
                      cfg_.directionalLargeVehicleScale
                    : 0.0f;

            const float verticalBias =
                cfg_.directionalResponseEnabled
                    ? Clamp(-dz / dirLen, -1.0f, 1.0f) *
                      cfg_.directionalLargeVehicleScale
                    : 0.0f;

            if (train) {
                const float amp =
                    (cfg_.trainResponseStrength * cfg_.presetAmbient) *
                    falloff *
                    Clamp(0.45f + speed * 4.0f, 0.45f, 1.35f) *
                    passageBoost *
                    cfg_.strength *
                    cfg_.presetScale;

                const float phase =
                    t * (5.5f + speed * 6.0f) * 2.0f * kPi +
                    static_cast<float>(i) * 0.41f;

                largeVehicleSide_ +=
                    (std::sin(phase * 0.73f) * 0.38f + sideBias * 0.62f) *
                    amp;
                largeVehicleVertical_ +=
                    (std::sin(phase) * 0.82f + verticalBias * 0.18f) *
                    amp;
                largeVehicleForward_ +=
                    (std::sin(phase * 0.57f + 1.0f) * 0.38f +
                     forwardBias * 0.62f) *
                    amp * 0.72f;
            } else {
                const float altitudeScale =
                    Clamp(1.0f - std::fabs(dz) / std::max(maxD, 1.0f), 0.2f, 1.0f);
                const float typeScale = heli ? 1.25f : 0.80f;
                const float amp =
                    (cfg_.aircraftResponseStrength * cfg_.presetAmbient) *
                    typeScale *
                    falloff *
                    altitudeScale *
                    passageBoost *
                    cfg_.strength *
                    cfg_.presetScale;

                const float phase =
                    t * (heli ? 11.5f : 7.2f) * 2.0f * kPi +
                    static_cast<float>(i) * 0.73f;

                largeVehicleSide_ +=
                    (std::sin(phase * 0.81f) * 0.44f + sideBias * 0.56f) *
                    amp;
                largeVehicleVertical_ +=
                    (std::sin(phase) * 0.56f +
                     std::sin(phase * 1.87f) * 0.22f +
                     verticalBias * 0.22f) *
                    amp;
                largeVehicleForward_ +=
                    (std::sin(phase * 0.63f + 0.6f) * 0.42f +
                     forwardBias * 0.58f) *
                    amp * 0.68f;

                if (heli && dz>0.0f && dz<=cfg_.rotorWashHeight) {
                    const float horizontalDistance=std::sqrt(dx*dx+dy*dy);
                    if (horizontalDistance<=cfg_.rotorWashDistance) {
                        const float hFalloff=1.0f-Clamp(
                            horizontalDistance/std::max(cfg_.rotorWashDistance,0.1f),
                            0.0f,1.0f
                        );
                        const float zFalloff=1.0f-Clamp(
                            dz/std::max(cfg_.rotorWashHeight,0.1f),
                            0.0f,1.0f
                        );
                        const float wash=
                            cfg_.rotorWashStrength*hFalloff*zFalloff*
                            (0.55f+Clamp(speed*2.5f,0.0f,0.75f))*
                            cfg_.strength*cfg_.presetScale;

                        largeVehicleVertical_-=wash;
                        largeVehicleSide_+=sideBias*wash*0.42f;
                        largeVehicleForward_+=forwardBias*wash*0.28f;
                    }
                }
            }
        }

        if (!aircraftSeenThisScan &&
            aircraftContinuousSinceMs_ != 0 &&
            aircraftLastSeenMs_ != 0 &&
            now - aircraftLastSeenMs_ >= cfg_.aircraftRearmMs) {
            if (aircraftResponseSuppressed_ && cfg_.debug)
                log_.Write("AIRCRAFT RESPONSE REARMED");

            aircraftContinuousSinceMs_ = 0;
            aircraftLastSeenMs_ = 0;
            aircraftResponseSuppressed_ = false;
        }

        const float largeVehicleEvidence =
            std::fabs(largeVehicleSide_) +
            std::fabs(largeVehicleVertical_) +
            std::fabs(largeVehicleForward_);

        if (largeVehicleEvidence > 0.00001f) {
            TouchResponseSource(
                cr::ResponseOrigin::Aircraft,
                largeVehicleEvidence,
                cfg_.aircraftRearmMs + 100u,
                cfg_.aircraftMaxContinuousMs
            );
        }

        const float cap = 0.085f;
        largeVehicleSide_ = Clamp(largeVehicleSide_, -cap, cap);
        largeVehicleVertical_ = Clamp(largeVehicleVertical_, -cap, cap);
        largeVehicleForward_ = Clamp(largeVehicleForward_, -cap, cap);
    }

    void UpdateWorldObjectImpacts(CPlayerPed* player) {
        AuditHit(AUD_UpdateWorldObjectImpacts);
        if (!cfg_.worldObjectResponseEnabled || !player)
            return;

        const unsigned now = CTimer::m_snTimeInMilliseconds;
        if (lastWorldObjectScanMs_ != 0 &&
            now - lastWorldObjectScanMs_ < cfg_.worldObjectScanMs) {
            return;
        }
        lastWorldObjectScanMs_ = now;

        auto* pool = CPools::ms_pObjectPool;
        if (!pool)
            return;

        if (worldObjectTrack_.size() != static_cast<size_t>(pool->m_nSize))
            worldObjectTrack_.assign(static_cast<size_t>(pool->m_nSize), WorldObjectTrack{});

        const CVector& pp = player->GetPosition();
        const float maxD = std::max(cfg_.worldObjectImpactDistance, 1.0f);
        const float maxDSq = maxD * maxD;

        unsigned machineryContributors = 0;

        for (int i = 0; i < pool->m_nSize; ++i) {
            CObject* object = pool->GetAt(i);
            WorldObjectTrack& tr =
                worldObjectTrack_[static_cast<size_t>(i)];

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
                tr.object = object;
                tr.collisionTime = object->m_nLastCollisionTime;
                tr.velocity = object->m_vecMoveSpeed;
                tr.damageIntensity = object->m_fDamageIntensity;
                tr.initialized = true;
                continue;
            }

            if (!tr.initialized || tr.object != object) {
                tr = {};
                tr.object = object;
                tr.collisionTime = object->m_nLastCollisionTime;
                tr.velocity = object->m_vecMoveSpeed;
                tr.damageIntensity = object->m_fDamageIntensity;
                tr.initialized = true;
                continue;
            }

            const bool collisionEdge =
                object->m_nLastCollisionTime != 0 &&
                object->m_nLastCollisionTime != tr.collisionTime;

            const float speed =
                std::sqrt(
                    object->m_vecMoveSpeed.x * object->m_vecMoveSpeed.x +
                    object->m_vecMoveSpeed.y * object->m_vecMoveSpeed.y +
                    object->m_vecMoveSpeed.z * object->m_vecMoveSpeed.z
                );

            const float dvx = object->m_vecMoveSpeed.x - tr.velocity.x;
            const float dvy = object->m_vecMoveSpeed.y - tr.velocity.y;
            const float dvz = object->m_vecMoveSpeed.z - tr.velocity.z;
            const float deltaV = std::sqrt(dvx * dvx + dvy * dvy + dvz * dvz);

            const float damageRise =
                std::max(0.0f, object->m_fDamageIntensity - tr.damageIntensity);

            // Collision timestamp changes during sustained contact; not every update is a new hit.
            if (tr.contactActive &&
                !collisionEdge &&
                tr.lastCollisionObservedMs != 0 &&
                now - tr.lastCollisionObservedMs >= cfg_.worldObjectContactReleaseMs) {
                tr.contactActive = false;
            }

            const bool newContact =
                collisionEdge &&
                (!tr.contactActive ||
                 tr.lastCollisionObservedMs == 0 ||
                 now - tr.lastCollisionObservedMs >= cfg_.worldObjectContactReleaseMs);

            if (collisionEdge) {
                tr.lastCollisionObservedMs = now;
                tr.contactActive = true;
            }

            const bool eventReady =
                tr.lastImpactEventMs == 0 ||
                now - tr.lastImpactEventMs >= cfg_.worldObjectImpactCooldownMs;

            const bool strongReimpact =
                collisionEdge &&
                eventReady &&
                (deltaV >= cfg_.worldObjectReimpactDeltaV ||
                 damageRise >= cfg_.worldObjectReimpactDamageRise);

            const bool meaningfulInitialContact =
                newContact &&
                (
                    speed > 0.045f ||
                    deltaV > 0.028f
                );

            if (eventReady &&
                (meaningfulInitialContact || strongReimpact)) {
                const float massScale =
                    Clamp(
                        std::sqrt(std::max(object->m_fMass, 1.0f) / 80.0f),
                        0.65f,
                        2.0f
                    );

                const float impactActivity =
                    std::max(speed, deltaV * 1.35f);

                const float strength =
                    cfg_.worldObjectImpactStrength *
                    massScale *
                    Clamp(0.55f + impactActivity * 5.0f, 0.55f, 1.60f);

                QueueSourceImpulse(
                    player,
                    op,
                    strength,
                    maxD,
                    0.58f,
                    1.0f,
                    0.54f,
                    1.20f
                );

                tr.lastImpactEventMs = now;

                if (cfg_.debug) {
                    log_.Write(
                        "WORLD OBJECT IMPACT dist=%.2f speed=%.3f dv=%.3f "
                        "mass=%.1f damage=%.2f damageRise=%.2f newContact=%d reimpact=%d",
                        std::sqrt(distSq),
                        speed,
                        deltaV,
                        object->m_fMass,
                        object->m_fDamageIntensity,
                        damageRise,
                        meaningfulInitialContact ? 1 : 0,
                        strongReimpact ? 1 : 0
                    );
                }
            }

            if (cfg_.advancedResponseEnabled &&
                machineryContributors < 4 &&
                !tr.contactActive &&
                distSq <= cfg_.machineryDistance * cfg_.machineryDistance &&
                object->m_fMass >= 180.0f &&
                speed > 0.012f) {

                const float machineryScale =
                    (cfg_.machineryStrength * cfg_.presetAmbient) *
                    Clamp(std::sqrt(object->m_fMass / 250.0f), 0.7f, 2.0f) *
                    Clamp(speed * 5.0f, 0.15f, 1.25f) *
                    cfg_.highFrequencyScale;

                QueueSourceImpulse(
                    player,
                    op,
                    machineryScale,
                    cfg_.machineryDistance,
                    0.34f,
                    0.72f,
                    0.42f,
                    0.32f
                );

                ++machineryContributors;
            }

            tr.object = object;
            tr.collisionTime = object->m_nLastCollisionTime;
            tr.velocity = object->m_vecMoveSpeed;
            tr.damageIntensity = object->m_fDamageIntensity;
        }
    }

    void UpdatePedWorldResponses(CPlayerPed* player) {
        AuditHit(AUD_UpdatePedWorldResponses);
        if (!cfg_.pedWorldResponseEnabled || !player)
            return;

        const unsigned now = CTimer::m_snTimeInMilliseconds;
        if (lastPedWorldScanMs_ != 0 &&
            now - lastPedWorldScanMs_ < cfg_.pedWorldScanMs) {
            return;
        }
        lastPedWorldScanMs_ = now;

        auto* pool = CPools::ms_pPedPool;
        if (!pool)
            return;

        if (pedWorldTrack_.size() != static_cast<size_t>(pool->m_nSize))
            pedWorldTrack_.assign(static_cast<size_t>(pool->m_nSize), PedWorldTrack{});

        const CVector& pp = player->GetPosition();
        const float stepMaxD = std::max(cfg_.heavyFootstepDistance, 1.0f);
        const float impactMaxD = 12.0f;

        for (int i = 0; i < pool->m_nSize; ++i) {
            CPed* ped = pool->GetAt(i);
            PedWorldTrack& tr =
                pedWorldTrack_[static_cast<size_t>(i)];

            if (!ped || ped == player || ped->m_fHealth <= 0.0f) {
                tr = {};
                continue;
            }

            const CVector& pos = ped->GetPosition();
            const float dx = pos.x - pp.x;
            const float dy = pos.y - pp.y;
            const float dz = pos.z - pp.z;
            const float distSq = dx * dx + dy * dy + dz * dz;

            if (!tr.initialized || tr.ped != ped) {
                tr.ped = ped;
                tr.collisionTime = ped->m_nLastCollisionTime;
                tr.nextFootstepMs = now + 150u + (static_cast<unsigned>(i) * 37u % 220u);
                tr.initialized = true;
                continue;
            }

            const float speed =
                std::sqrt(
                    ped->m_vecMoveSpeed.x * ped->m_vecMoveSpeed.x +
                    ped->m_vecMoveSpeed.y * ped->m_vecMoveSpeed.y
                );

            const bool collisionEdge =
                ped->m_nLastCollisionTime != 0 &&
                ped->m_nLastCollisionTime != tr.collisionTime;

            if (collisionEdge &&
                distSq <= impactMaxD * impactMaxD &&
                (speed > 0.055f || ped->m_fDamageIntensity > 0.02f)) {

                const float massScale =
                    Clamp(std::sqrt(std::max(ped->m_fMass, 1.0f) / 70.0f), 0.8f, 1.35f);

                QueueSourceImpulse(
                    player,
                    pos,
                    cfg_.pedImpactStrength *
                    massScale *
                    Clamp(0.70f + speed * 3.0f, 0.70f, 1.45f),
                    impactMaxD,
                    0.60f,
                    0.85f,
                    0.55f,
                    1.05f
                );
            }

            // Approximate nearby runner footfalls without expensive NPC bone sampling.
            if (distSq <= stepMaxD * stepMaxD &&
                speed > 0.115f &&
                !ped->bIsInTheAir &&
                now >= tr.nextFootstepMs) {

                const float dist = std::sqrt(distSq);
                const float falloff =
                    1.0f - Clamp(dist / stepMaxD, 0.0f, 1.0f);
                const float massScale =
                    Clamp(std::sqrt(std::max(ped->m_fMass, 1.0f) / 70.0f), 0.85f, 1.35f);
                const float impulse =
                    cfg_.heavyFootstepStrength *
                    falloff *
                    massScale *
                    Clamp(speed / 0.20f, 0.65f, 1.35f);

                QueueSourceImpulse(
                    player,
                    pos,
                    impulse,
                    stepMaxD,
                    0.28f,
                    1.0f,
                    0.18f,
                    0.45f
                );

                const unsigned interval =
                    static_cast<unsigned>(
                        Clamp(430.0f - speed * 900.0f, 230.0f, 430.0f)
                    );
                tr.nextFootstepMs = now + interval;
            }

            tr.ped = ped;
            tr.collisionTime = ped->m_nLastCollisionTime;
        }
    }

    void UpdateCameraObstructionResponse(
        CPlayerPed* player,
        CVehicle* vehicle,
        float dt
    ) {
        AuditHit(AUD_UpdateCameraObstructionResponse);
        if (!cfg_.obstructionResponseEnabled || !player) {
            obstructionSpring_.Reset();
            obstructionInit_ = false;
            return;
        }

        CVector target =
            vehicle ? vehicle->GetPosition() : player->GetPosition();
        const CVector camera =
            TheCamera.m_mCameraMatrix.pos;

        const float dx = camera.x - target.x;
        const float dy = camera.y - target.y;
        const float dz = camera.z - target.z;
        const float distance =
            std::sqrt(dx * dx + dy * dy + dz * dz);

        if (!obstructionInit_) {
            previousCameraTargetDistance_ = distance;
            obstructionInit_ = true;
        } else {
            const float delta =
                distance - previousCameraTargetDistance_;

            if (delta < -cfg_.obstructionThreshold) {
                const float impulse =
                    Clamp(
                        (-delta - cfg_.obstructionThreshold) *
                        cfg_.obstructionStrength,
                        0.0f,
                        0.65f
                    );

                // Counter abrupt stock obstruction motion; GTA retains line-of-sight control.
                TouchResponseSource(
                    cr::ResponseOrigin::Obstruction,
                    impulse,
                    700
                );
                obstructionSpring_.forward.AddImpulse(-impulse);
            }

            previousCameraTargetDistance_ = distance;
        }

        obstructionSpring_.Update(
            dt,
            cfg_.obstructionSpring,
            (cfg_.obstructionDamping * cfg_.presetRecovery)
        );

        obstructionSpring_.forward.position =
            Clamp(obstructionSpring_.forward.position, -0.12f, 0.08f);
    }

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

            // Ambient speed mapping: ~0.02 is near idle, ~0.55+ is fast traffic.
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

            // Per-pool-slot phase desynchronizes traffic; speed and acceleration shape vibration.
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

        // GTA heading: forward=(sin h, cos h), right=(cos h, -sin h).
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

            // Fade stride phase on stopping to avoid a camera snap.
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

        // Foot separation drives animation phase.
        rawFootPhase_ =
            Clamp(footDiff_ * 2.7f, -1.0f, 1.0f);

        // Filter gait phase between animation samples to prevent stepped offsets.
        const float phaseAlpha =
            1.0f - std::exp(-cfg_.footPhaseResponse * dt);

        smoothedFootPhase_ +=
            (rawFootPhase_ - smoothedFootPhase_) *
            phaseAlpha;

        const float phase =
            Clamp(smoothedFootPhase_, -1.0f, 1.0f);

        // phase^2 creates two smooth vertical pulses per stride without an abs() cusp.
        const float stride =
            phase * phase;

        // Center the vertical bob around zero so it does not become a static downward offset.
        const float verticalWave =
            0.42f - stride;

        const float forwardWave =
            0.30f - stride;

        // Speed shapes intensity; strengthNow_ selects the movement state.
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

        // Small fallback gait grows only when bone touchdowns are absent.
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

        // Accept GTA special/finisher fight slots beyond the normal 0..2 combo.
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
            // Accept special unarmed moves with live fight task+animation; preserve weapon fallback.
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

        // Profile combo stages: quick opener, weighted follow-up, strong finisher.
        static const float kMoveRelease[3] = { 0.86f, 1.03f, 1.18f };
        static const float kMoveSide[3]    = { 0.72f, 1.08f, 1.20f };
        static const float kMoveVertical[3]= { 0.82f, 1.00f, 1.18f };
        static const float kMovePhase[3]   = {-0.035f, 0.000f, 0.025f };

        releaseScale = kMoveRelease[move];
        sideScale = kMoveSide[move];
        verticalScale = kMoveVertical[move];
        phaseOffset = kMovePhase[move];

        if (style == STYLE_BOXING) {
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

            // Anticipation briefly lags opposite the strike.
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

            // Emit release even on misses to follow body motion.
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

            // Layer a contact jolt over release; kicks bias vertical, punches stay compact.
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

                // Chainsaw contact is unreliable; require a live cutting animation for higher move
                // slots.
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

            // Chainsaw forward load must be bounded and frame-local, never integrated into the shared
            // spring.
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

        // Clear all chainsaw blends and forward bias on weapon exit.
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

        // Continuous-effect weapons use sustained texture while firing, not discrete gun recoil.
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

        // Chainsaw animation loops are not new swings; its continuous texture handles cutting.
        if (id == 9) {
            // Controller lock-on can hide fight-task edges; emit one start pulse on fire edge.
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

        // Melee fallback uses weapon cooldown when fight animation is unavailable (e.g. nightstick).
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

            // Fallback swing includes anticipation/release even without registered contact.
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

        // Clip loss signals shots even if gameProcessEvent samples after FIRING has ended.
        const unsigned clipSpent =
            prevAmmo_ > clip ?
            prevAmmo_ - clip :
            0u;
        // Clip and total ammo may reflect the same shot; use max, not sum.
        const unsigned totalSpent = jetpackTask && prevAmmoTotal_ > total
            ? prevAmmoTotal_ - total : 0u;
        const unsigned roundsSpent = std::max(clipSpent, totalSpent);

        const bool ammoShot =
            roundsSpent > 0;

        // Fallback to m_nTimeForNextShot changes for infinite-ammo/unchanged-clip weapons.
        const bool nextShotAdvanced =
            nextShot != 0 &&
            nextShot != prevNextShotMs_ &&
            nextShot > prevNextShotMs_;

        const bool firingState =
            state == WEAPONSTATE_FIRING;

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

        // Vehicle appearance IDs: auto=1, bike=2, heli=3, boat=4, plane=5.
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
        // US 1.0 FireTruckControl submits the water-spray segment; SWAT uses it too.
        constexpr uintptr_t site = 0x72A260;
        if (!IsReadableCodeSpan(site, 5) ||
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
                // SDK labels offset 0x94C vertical; EXE uses it as turret yaw.
                const float yaw = reinterpret_cast<CAutomobile*>(v)->m_fDoomVerticalRotation;
                side = std::sin(yaw);
                forward = -std::cos(yaw);
            }
            float cameraSide = 0.0f, cameraForward = 0.0f;
            VehicleLocalXYToCameraLocal(v, side, forward, cameraSide, cameraForward);
            const float kick = (heavy ? cfg_.mountedHeavyStrength : cfg_.mountedGunStrength) * gain;
            // Immediate displacement stays visible at high FPS and when rotation is off.
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
                // Mix bounded camera-local pressure separately from ambient ducking.
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
        // Reset frame mix before RPM, gearshift, or other producers add to it.
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

        // Filter physics spikes from steady camera targets; impacts have separate impulses.
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

        // Normalize live tHandlingData to support stock and custom vehicle suspension.
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

            // Clamp ratios around typical road-car handling values for mod safety.
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

        // Acceleration pulls back, braking surges forward, cornering leans outward.
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

        // Inertial targets affect the camera only, never vehicle physics or handling.
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

        // m_vecLastCollisionImpactVelocity encodes collision normal/distance, not impact severity.
        const bool contactMetadata =
            newEntityContact ||
            physicalDamageIntensity > 0.01f ||
            v->m_pDamageEntity != nullptr ||
            v->bHasHitWall;

        const float dvThreshold =
            std::max(cfg_.vehicleContactDeltaVelocityThreshold, 0.001f);
        const float strongDv =
            std::max(cfg_.vehicleContactStrongDeltaVelocity, dvThreshold * 1.5f);

        // Use velocity discontinuity; persistent road contact can repeatedly update collision time.
        const bool deltaVelocityImpact =
            deltaVelMag >= dvThreshold;

        // Require collision metadata/timestamp plus delta-V to reject steering/braking false hits.
        const bool collisionEvidence =
            contactMetadata ||
            freshCollisionTimestamp;

        const bool strongTimestampImpact =
            freshCollisionTimestamp &&
            deltaVelMag >= strongDv * 1.35f;

        // Collision hysteresis merges one crash's multiple contacts; retrigger only for stronger
        // impacts.
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

            // Separate collision spring prevents normal driving motion from masking crashes.
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

            // Independent immediate collision kick remains visible during saturated cornering offsets.
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

            // Separate first-frame collision displacement from normal cornering springs.
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

        // Reinforce health loss only within a real collision window; exclude fire/weapon damage.
        const float currentHealth = v->m_fHealth;
        if (!vehicleHealthInit_) {
            vehicleHealthInit_ = true;
            prevVehicleHealth_ = currentHealth;
        }

        const float healthLoss = std::max(0.0f, prevVehicleHealth_ - currentHealth);
        lastVehicleHealthLoss_ = healthLoss;

        const bool recentPhysicsImpact =
            (now - lastVehicleImpactMs_) <= cfg_.vehicleCrashPhysicsWindowMs;
        // Frame delta-V catches crashes below smoothed acceleration thresholds.
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

            // Project collision delta-V into vehicle space; frontal deceleration surges the camera
            // forward.
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

            // Positive forwardDir indicates a frontal/deceleration hit.
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

            // Strong crashes add short downward chassis compression.
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

        // Weight wheel compression by live handling and feed a dedicated suspension spring.
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

                // GTA eWheels order: FL, RL, FR, RR.
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

            // Reduce compression trigger for stiff/short-travel setups; preserve soft-travel behavior.
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

                // Fore/aft curb response works even with camera rotation disabled.
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
                    // Derive BMX cadence from actual crank motion rather than speed/time.
                    CBmx* bicycle =
                        reinterpret_cast<CBmx*>(v);

                    const float leftAngle =
                        bicycle->m_fPedalAngleL;
                    const float rightAngle =
                        bicycle->m_fPedalAngleR;

                    // Combine left pedal and inverted right pedal vectors to stabilize crank phase.
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

                        // No crank movement means no pedaling bob, even while coasting.
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

                            // Two load pulses per crank revolution, one per downstroke.
                            const float downstroke =
                                -std::cos(
                                    pedalPhase * 2.0f
                                );

                            // Lateral weight shift follows the crank once per revolution.
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

                // Use physical hull wave motion for heave/roll/pitch, not synthetic oscillation.
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
                    // BoatWaveStrength=0.085 is nominal; axis coefficients already define small
                    // offsets.
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

                // Optional orientation follows hull attitude/rate for smooth wave roll and pitch.
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

                // Water re-entry creates upward hull acceleration and downward camera kick.
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

                // Pitch/roll rate must also affect translation when orientation is disabled.
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

        // Keep road/engine texture small; bikes gain more through their profile.
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

        // Horn vibration works at idle; ease attack/release to avoid discontinuities.
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

        // Convert own-vehicle motion from vehicle-local to camera-local before mixing world effects.
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

        // Keep player recoil in third-person vehicle view.
        side += recoilSpring_.side.position * cfg_.strength * cfg_.presetScale;
        vertical += recoilSpring_.vertical.position * cfg_.strength * cfg_.presetScale;
        forward += recoilSpring_.forward.position * cfg_.strength * cfg_.presetScale;

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

        // Zero invisible residuals to avoid denormal spring tails.
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

        // Fade idle wave after sustained rest; reserve it for subtle breathing/weight.
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

        if (fullySettledIdle) {
            side = 0.0f;
            vertical = 0.0f;
            forward = 0.0f;
        }

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

        // Apply recoil after aim damping so aiming does not hide shots.
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

        // Chainsaw pressure is a bounded frame-local layer; clear on weapon switch.
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

        if (cfg_.advancedResponseEnabled && cfg_.jetpackDirectionalEnabled && previousJetpack_) {
            const float jetpackScale = cfg_.strength * cfg_.presetScale;
            side += jetpackResponseSide_ * jetpackScale;
            vertical += jetpackResponseVertical_ * jetpackScale;
            forward += jetpackResponseForward_ * jetpackScale;
        }

        const float finalScale = cfg_.strength;

        // CMatrix basis: right=camera right, up=camera forward, at=camera vertical.
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

        // Rotation is opt-in on RenderWare's frame only; GTA's internal CCamera basis stays untouched.
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
                cfg_.debug || cfg_.auditEnabled;
            cfg_.Load();
            lastRuntimeReloadMs_ = now;

            const bool wantsLogging =
                cfg_.debug || cfg_.auditEnabled;
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
            "Camera Response | %s | %s | hook=%d | %s tick=%llu",
            cfg_.presetName, vehicleInit_ ? "VEHICLE" : MotionName(motion_),
            cameraCommitHookInstalled_ ? 1 : 0, UpdateSourceName(),
            static_cast<unsigned long long>(updates_));
        std::snprintf(line2, sizeof(line2),
            "offset S/V/F %.3f / %.3f / %.3f | surface=%u x%.2f | nearVeh=%u",
            pendingSide_, pendingVertical_, pendingForward_, currentSurfaceId_,
            currentSurfaceMultiplier_, nearbyVehicleContributors_);
        if (vehicleInit_) {
            std::snprintf(line3, sizeof(line3),
                "veh speed=%.2f accel F/S/Z %.2f/%.2f/%.2f | hit=%u dmg=%u susp=%u horn=%d gate=%s",
                vehicleSpeed_, vehicleForwardAccel_, vehicleSideAccel_,
                vehicleVerticalAccelNow_, vehicleImpacts_, vehicleDamageCrashes_,
                vehicleSuspensionImpacts_, vehicleHornActive_ ? 1 : 0,
                lastGate_.empty() ? "?" : lastGate_.c_str());
        } else {
            std::snprintf(line3, sizeof(line3),
                "steps=%u/%u | shots=%u streak=%u melee=%u | gate=%s",
                leftSteps_, rightSteps_, shots_, shotStreak_, meleeContacts_,
                lastGate_.empty() ? "?" : lastGate_.c_str());
        }

        if (cfg_.detailedDebugHud) {
            const cr::MixTelemetry& telemetry = responseMixer_.Telemetry();
            std::snprintf(line4, sizeof(line4),
                "mix=%u drop=%u | bands L/M/H %.3f/%.3f/%.3f | rotation P/R/Y %.2f/%.2f/%.2f",
                telemetry.accepted, telemetry.dropped,
                std::fabs(telemetry.band[(unsigned)cr::ResponseBand::Low].vertical),
                std::fabs(telemetry.band[(unsigned)cr::ResponseBand::Mid].vertical),
                std::fabs(telemetry.band[(unsigned)cr::ResponseBand::High].vertical),
                lastPitchDeg_, lastRollDeg_, lastYawDeg_);
        }

        const int width = RsGlobal.maximumWidth;
        const int height = RsGlobal.maximumHeight;
        if (width < 320 || height < 200)
            return;

        // Query the game window's DPI without changing the host process's awareness.
        const unsigned dpi = GetOverlayWindowDpi();
        const cr::OverlayLayout layout =
            cr::ComputeOverlayLayout(width, height, dpi, cfg_.overlayScale);

        if (width != overlayWidth_ || height != overlayHeight_ ||
            dpi != overlayDpi_ || cfg_.overlayScale != overlayScaleLogged_) {
            overlayWidth_ = width;
            overlayHeight_ = height;
            overlayDpi_ = dpi;
            overlayScaleLogged_ = cfg_.overlayScale;
            log_.WriteImportant("OVERLAY resolution=%dx%d dpi=%u userScale=%.2f font=%.2f/%.2f",
                width, height, dpi, cfg_.overlayScale, layout.fontX, layout.fontY);
        }

        CFont::SetOrientation(ALIGN_LEFT);
        CFont::SetFontStyle(FONT_SUBTITLES);
        CFont::SetProportional(true);
        CFont::SetColor(CRGBA(255, 255, 255, 240));
        CFont::SetDropShadowPosition(2);
        CFont::SetDropColor(CRGBA(0, 0, 0, 255));
        CFont::SetBackground(false, false);
        CFont::SetWrapx(layout.wrapX);
        CFont::SetScale(layout.fontX, layout.fontY);

        // Avoid wrapped telemetry lines overlapping the following row.
        const char* textLines[] = {line1, line2, line3, line4};
        const int count = cfg_.detailedDebugHud ? 4 : 3;
        float widest = 0.0f;
        for (int i = 0; i < count; ++i) {
            widest = std::max(widest, CFont::GetStringWidth(textLines[i], true, false));
        }
        const float availableWidth = layout.wrapX - layout.x - 8.0f;
        const float fit = widest > availableWidth && widest > 1.0f
            ? Clamp(availableWidth / widest, 0.60f, 1.0f)
            : 1.0f;
        if (fit < 1.0f)
            CFont::SetScale(layout.fontX * fit, layout.fontY * fit);

        for (int i = 0; i < count; ++i)
            CFont::PrintString(layout.x, layout.firstY + i * layout.rowHeight * fit, textLines[i]);
    }

    void GameUpdate() {
        AuditHit(AUD_GameUpdate);
        ++updates_;
        if (updates_ == 1)
            log_.WriteImportant("GAME UPDATE started source=%s frame=%u",
                UpdateSourceName(), CTimer::m_FrameCounter);
        VerifyCameraCommitHook();
        const float dt = Dt();
        UpdateRuntimeReload();
        AuditTick();

        CPlayerPed* p = FindPlayerPed();
        if (!p) {
            pendingAllowed_ = false;
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

            // Reset on-foot springs on vehicle entry/exit to prevent inherited impulses.
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

            // Keep player weapon/melee updates active in both vehicle and on-foot modes.
            UpdateWeapon(p, pad);
            CommitTransientEvents();
            UpdateAmbientDucking(dt);

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

            impactSpring_.Update(
                dt,
                cfg_.spring * 0.82f,
                cfg_.damping * 0.82f * cfg_.presetRecovery
            );
            UpdateVehicle(vehicle, dt);
            UpdateMountedWeapons(p, vehicle, dt);

            // Consume vehicle-published jerk/crash events before final composition.
            CommitTransientEvents();
            UpdateAmbientDucking(dt);

            directionalRotationSpring_.Update(
                dt,
                cfg_.directionalRotationSpring,
                cfg_.directionalRotationDamping*cfg_.presetRecovery
            );

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
            // Clear gated camera output and diagnostics to avoid stale API/log values.
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

    static void __fastcall CameraCopyThunk(
        CCamera* camera,
        void*,
        bool updateMatrix
    ) {
        if (instance_) {
            instance_->OnCameraCommit(camera, updateMatrix);
            return;
        }

        if (originalCopyCamera_)
            originalCopyCamera_(camera, updateMatrix);
    }






    static std::string ModulePathForAddress(uintptr_t address) {
        if (!address)
            return {};

        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(
                reinterpret_cast<LPCVOID>(address),
                &mbi,
                sizeof(mbi))) {
            return {};
        }

        HMODULE module =
            reinterpret_cast<HMODULE>(mbi.AllocationBase);

        char path[1024] = {};
        const DWORD length =
            GetModuleFileNameA(
                module,
                path,
                static_cast<DWORD>(sizeof(path))
            );

        return length > 0 ?
            std::string(path, length) :
            std::string();
    }

    static bool IsReadableCodeSpan(uintptr_t address, size_t length) {
        if (!address || !length || address + length < address)
            return false;

        const uintptr_t end = address + length;
        for (uintptr_t cursor = address; cursor < end;) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi)))
                return false;

            const DWORD access = mbi.Protect & 0xFFu;
            if (mbi.State != MEM_COMMIT ||
                (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
                (access != PAGE_READONLY && access != PAGE_READWRITE &&
                 access != PAGE_WRITECOPY && access != PAGE_EXECUTE_READ &&
                 access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY))
                return false;

            const uintptr_t regionEnd =
                reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (regionEnd <= cursor)
                return false;
            cursor = std::min(end, regionEnd);
        }
        return true;
    }

    static uintptr_t DecodeRelativeCallTarget(uintptr_t callsite) {
        const uint8_t* call =
            reinterpret_cast<const uint8_t*>(callsite);

        if (call[0] != 0xE8)
            return 0;

        const int32_t rel =
            *reinterpret_cast<const int32_t*>(callsite + 1);

        return callsite + 5 + rel;
    }

    bool ValidateCameraCommitCallsite() const {
        constexpr uintptr_t kProcessCopyCallsite = 0x52C98B;

        if (!IsReadableCodeSpan(kProcessCopyCallsite - 3, 3 + 5 + 12))
            return false;

        const uint8_t* prefix =
            reinterpret_cast<const uint8_t*>(kProcessCopyCallsite - 3);
        const uint8_t* call =
            reinterpret_cast<const uint8_t*>(kProcessCopyCallsite);
        const uint8_t* suffix =
            reinterpret_cast<const uint8_t*>(kProcessCopyCallsite + 5);

        const bool prefixOk =
            prefix[0] == 0x55 &&
            prefix[1] == 0x8B &&
            prefix[2] == 0xCE &&
            call[0] == 0xE8;

        const uint8_t expectedSuffix[] = {
            0x8B, 0x8E, 0xA8, 0x09, 0x00, 0x00,
            0x8B, 0x86, 0xA4, 0x09, 0x00, 0x00
        };

        bool suffixOk = true;
        for (unsigned i = 0; i < sizeof(expectedSuffix); ++i) {
            if (suffix[i] != expectedSuffix[i]) {
                suffixOk = false;
                break;
            }
        }

        return prefixOk && suffixOk;
    }

    void InstallCameraCommitHook() {
        AuditHit(AUD_InstallCameraCommitHook);
        constexpr uintptr_t kProcessCopyCallsite = 0x52C98B;
        constexpr uintptr_t kExpectedCopyFunction = 0x50AFA0;

        if (!ValidateCameraCommitCallsite()) {
            log_.WriteImportant(
                "ERROR: unsupported camera callsite signature at 0x%08X; hook NOT installed. "
                "Requires Classic US 1.0 and compatible camera-hook mods.",
                static_cast<unsigned>(kProcessCopyCallsite)
            );
            cameraCommitHookInstalled_ = false;
            log_.Flush();
            return;
        }

        const uintptr_t decodedTarget =
            DecodeRelativeCallTarget(kProcessCopyCallsite);

        const uintptr_t ourTarget =
            reinterpret_cast<uintptr_t>(&CameraCopyThunk);

        if (decodedTarget == ourTarget) {
            cameraCommitHookInstalled_ = true;

            if (!originalCopyCamera_) {
                originalCopyCamera_ =
                    reinterpret_cast<CopyCameraFn>(kExpectedCopyFunction);
                previousCopyTarget_ = kExpectedCopyFunction;
            }

            log_.Write(
                "CAMERA COMMIT HOOK already active callsite=0x%08X target=%p original=%p",
                static_cast<unsigned>(kProcessCopyCallsite),
                reinterpret_cast<void*>(decodedTarget),
                reinterpret_cast<void*>(previousCopyTarget_)
            );
            log_.Flush();
            return;
        }

        auto previous = injector::MakeCALL(
            kProcessCopyCallsite,
            injector::raw_ptr(&CameraCopyThunk),
            true
        );

        const uintptr_t previousTarget =
            reinterpret_cast<uintptr_t>(previous.get<void>());

        // Chain existing CALL redirections; yield if another plugin later replaces our hook.
        originalCopyCamera_ =
            reinterpret_cast<CopyCameraFn>(
                previousTarget != 0 ?
                previousTarget :
                kExpectedCopyFunction
            );
        previousCopyTarget_ =
            previousTarget != 0 ?
                previousTarget :
                kExpectedCopyFunction;

        cameraCommitHookInstalled_ =
            DecodeRelativeCallTarget(kProcessCopyCallsite) == ourTarget &&
            originalCopyCamera_ != nullptr;

        log_.Write(
            "CAMERA COMMIT HOOK callsite=0x%08X decodedBefore=%p previous=%p originalNow=%p "
            "expected=0x%08X installed=%d restoreCount=%u",
            static_cast<unsigned>(kProcessCopyCallsite),
            reinterpret_cast<void*>(decodedTarget),
            reinterpret_cast<void*>(previousTarget),
            reinterpret_cast<void*>(previousCopyTarget_),
            static_cast<unsigned>(kExpectedCopyFunction),
            cameraCommitHookInstalled_ ? 1 : 0,
            cameraHookRestoreCount_
        );

        if (decodedTarget != kExpectedCopyFunction &&
            decodedTarget != ourTarget) {
            log_.Write(
                "NOTICE: camera CALL was redirected to %p before CameraResponse hook",
                reinterpret_cast<void*>(decodedTarget)
            );
        }

        log_.Flush();
    }

    void VerifyCameraCommitHook() {
        AuditHit(AUD_VerifyCameraCommitHook);
        constexpr uintptr_t kProcessCopyCallsite = 0x52C98B;

        const unsigned now =
            CTimer::m_snTimeInMilliseconds;

        if (lastCameraHookCheckMs_ != 0 &&
            now - lastCameraHookCheckMs_ < 500u) {
            return;
        }

        lastCameraHookCheckMs_ = now;

        if (!ValidateCameraCommitCallsite()) {
            if (cameraCommitHookInstalled_) {
                cameraCommitHookInstalled_ = false;
                log_.Write(
                    "ERROR: camera hook callsite signature changed; CameraResponse disabled commit hook"
                );
                log_.Flush();
            }
            return;
        }

        const uintptr_t currentTarget =
            DecodeRelativeCallTarget(kProcessCopyCallsite);

        const uintptr_t ourTarget =
            reinterpret_cast<uintptr_t>(&CameraCopyThunk);

        if (currentTarget == ourTarget) {
            cameraCommitHookInstalled_ = true;
            return;
        }

        ++cameraHookRestoreCount_;
        cameraCommitHookInstalled_ = false;

        const std::string owner =
            ModulePathForAddress(currentTarget);

        // Never watchdog-repatch the CALL; disable output if another mod takes ownership.
        log_.Write(
            "CAMERA HOOK LOST current=%p owner=%s expectedSelf=%p; "
            "CameraResponse yielding (no automatic repatch) count=%u",
            reinterpret_cast<void*>(currentTarget),
            owner.empty() ? "<unknown>" : owner.c_str(),
            reinterpret_cast<void*>(ourTarget),
            cameraHookRestoreCount_
        );
        log_.Flush();
    }



    void ClipCameraOffsetAgainstWorld(
        CPlayerPed* player,
        const CVector& origin,
        CVector& worldOffset
    ) {
        AuditHit(AUD_ClipCameraOffsetAgainstWorld);
        if (!cfg_.cameraOffsetCollisionEnabled)
            return;

        const float length =
            std::sqrt(
                worldOffset.x * worldOffset.x +
                worldOffset.y * worldOffset.y +
                worldOffset.z * worldOffset.z
            );

        if (length <= 0.0005f)
            return;

        CVector target{
            origin.x + worldOffset.x,
            origin.y + worldOffset.y,
            origin.z + worldOffset.z
        };

        CColPoint hit{};
        CEntity* hitEntity = nullptr;
        CEntity* oldIgnore = CWorld::pIgnoreEntity;
        CWorld::pIgnoreEntity = player;

        const bool blocked =
            CWorld::ProcessLineOfSight(
                origin,
                target,
                hit,
                hitEntity,
                true,   // buildings
                false,  // vehicles
                false,  // peds
                true,   // objects
                true,   // dummies
                false,  // see-through
                true,   // camera-ignore
                false   // shoot-through
            );

        CWorld::pIgnoreEntity = oldIgnore;

        if (!blocked)
            return;

        const float hx = hit.m_vecPoint.x - origin.x;
        const float hy = hit.m_vecPoint.y - origin.y;
        const float hz = hit.m_vecPoint.z - origin.z;
        const float hitDistance =
            std::sqrt(hx * hx + hy * hy + hz * hz);

        const float allowed =
            std::max(
                0.0f,
                hitDistance - cfg_.cameraOffsetCollisionPadding
            );

        if (allowed >= length)
            return;

        const float scale = allowed / length;
        worldOffset.x *= scale;
        worldOffset.y *= scale;
        worldOffset.z *= scale;
    }


    void OnCameraCommit(
        CCamera* camera,
        bool updateMatrix
    ) {
        AuditHit(AUD_OnCameraCommit);
        ++commitCalls_;

        if (!originalCopyCamera_)
            return;

        if (!camera || !pendingAllowed_) {
            originalCopyCamera_(camera, updateMatrix);
            return;
        }

        CPlayerPed* p = FindPlayerPed();

        if (!p) {
            originalCopyCamera_(camera, updateMatrix);
            return;
        }

        CCam& cam =
            camera->m_aCams[camera->m_nActiveCam];

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

        const char* commitGate =
            Gate(
                p,
                cam,
                cameraContext_
            );

        if (std::strcmp(commitGate, "OK") != 0 &&
            std::strcmp(commitGate, "OK-VEHICLE") != 0) {
            originalCopyCamera_(camera, updateMatrix);
            return;
        }

        CVector forward = cam.m_vecFront;
        CVector up = cam.m_vecUp;

        const float fLen = std::sqrt(
            forward.x * forward.x +
            forward.y * forward.y +
            forward.z * forward.z
        );

        const float uLen = std::sqrt(
            up.x * up.x +
            up.y * up.y +
            up.z * up.z
        );

        if (fLen <= 0.00001f || uLen <= 0.00001f) {
            originalCopyCamera_(camera, updateMatrix);
            return;
        }

        forward.x /= fLen;
        forward.y /= fLen;
        forward.z /= fLen;

        up.x /= uLen;
        up.y /= uLen;
        up.z /= uLen;

        CVector right;
        right.x = forward.y * up.z - forward.z * up.y;
        right.y = forward.z * up.x - forward.x * up.z;
        right.z = forward.x * up.y - forward.y * up.x;

        const float rLen = std::sqrt(
            right.x * right.x +
            right.y * right.y +
            right.z * right.z
        );

        if (rLen <= 0.00001f) {
            originalCopyCamera_(camera, updateMatrix);
            return;
        }

        right.x /= rLen;
        right.y /= rLen;
        right.z /= rLen;

        CVector worldOffset;
        worldOffset.x =
            right.x * pendingSide_ +
            up.x * pendingVertical_ +
            forward.x * pendingForward_;

        worldOffset.y =
            right.y * pendingSide_ +
            up.y * pendingVertical_ +
            forward.y * pendingForward_;

        worldOffset.z =
            right.z * pendingSide_ +
            up.z * pendingVertical_ +
            forward.z * pendingForward_;

        if (!std::isfinite(worldOffset.x) ||
            !std::isfinite(worldOffset.y) ||
            !std::isfinite(worldOffset.z)) {

            pendingAllowed_ = false;
            originalCopyCamera_(camera, updateMatrix);
            log_.Write("SAFETY: rejected non-finite camera offset");
            return;
        }

        ClipCameraOffsetAgainstWorld(
            p,
            camera->m_mCameraMatrix.pos,
            worldOffset
        );

        worldOffset.x = Clamp(worldOffset.x, -1.0f, 1.0f);
        worldOffset.y = Clamp(worldOffset.y, -1.0f, 1.0f);
        worldOffset.z = Clamp(worldOffset.z, -1.0f, 1.0f);

        // Temporarily translate GTA's camera matrix for its stock copy; preserve its orientation basis.
        const CVector savedPos =
            camera->m_mCameraMatrix.pos;

        lastMatrixBase_ = savedPos;

        camera->m_mCameraMatrix.pos.x += worldOffset.x;
        camera->m_mCameraMatrix.pos.y += worldOffset.y;
        camera->m_mCameraMatrix.pos.z += worldOffset.z;

        lastMatrixForced_ =
            camera->m_mCameraMatrix.pos;

        originalCopyCamera_(camera, updateMatrix);

        // If the RW frame missed the translated copy, synchronize its position and update objects.
        if (camera->m_pRwCamera) {
            RwFrame* frame =
                RwCameraGetFrame(camera->m_pRwCamera);

            if (frame) {
                RwMatrix* frameMatrix =
                    RwFrameGetMatrix(frame);

                if (frameMatrix) {
                    const float dx =
                        frameMatrix->pos.x - lastMatrixForced_.x;
                    const float dy =
                        frameMatrix->pos.y - lastMatrixForced_.y;
                    const float dz =
                        frameMatrix->pos.z - lastMatrixForced_.z;
                    const float distSq =
                        dx * dx + dy * dy + dz * dz;

                    if (distSq > 0.0025f) {
                        frameMatrix->pos.x =
                            lastMatrixForced_.x;
                        frameMatrix->pos.y =
                            lastMatrixForced_.y;
                        frameMatrix->pos.z =
                            lastMatrixForced_.z;

                        RwFrameUpdateObjects(frame);
                        ++rwFrameFallbacks_;
                    }

                    if (cfg_.rotationEnabled &&
                        (std::fabs(pendingPitchRad_) > 0.000001f ||
                         std::fabs(pendingRollRad_) > 0.000001f ||
                         std::fabs(pendingYawRad_) > 0.000001f)) {
                        if (ApplyLocalRwRotation(
                                frameMatrix,
                                pendingPitchRad_,
                                pendingRollRad_,
                                pendingYawRad_)) {
                            RwFrameUpdateObjects(frame);
                        } else {
                            lastPitchDeg_ = 0.0f;
                            lastRollDeg_ = 0.0f;
                            lastYawDeg_ = 0.0f;
                        }
                    }

                    lastRwFramePos_.x =
                        frameMatrix->pos.x;
                    lastRwFramePos_.y =
                        frameMatrix->pos.y;
                    lastRwFramePos_.z =
                        frameMatrix->pos.z;
                }
            }
        }

        // Restore GTA's original matrix; RenderWare retains the rendered transform.
        camera->m_mCameraMatrix.pos =
            savedPos;

        ++commitApplied_;

        if (commitApplied_ <= 8) {
            log_.Write(
                "COMMIT APPLY #%llu update=%d pending=(%.4f %.4f %.4f) rotDeg=(%.3f %.3f %.3f) "
                "world=(%.4f %.4f %.4f) "
                "matrix=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) "
                "rw=(%.3f %.3f %.3f) fallback=%llu",
                static_cast<unsigned long long>(commitApplied_),
                updateMatrix ? 1 : 0,
                pendingSide_,
                pendingVertical_,
                pendingForward_,
                lastPitchDeg_,
                lastRollDeg_,
                lastYawDeg_,
                worldOffset.x,
                worldOffset.y,
                worldOffset.z,
                lastMatrixBase_.x,
                lastMatrixBase_.y,
                lastMatrixBase_.z,
                lastMatrixForced_.x,
                lastMatrixForced_.y,
                lastMatrixForced_.z,
                lastRwFramePos_.x,
                lastRwFramePos_.y,
                lastRwFramePos_.z,
                static_cast<unsigned long long>(rwFrameFallbacks_)
            );
        }
    }

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
