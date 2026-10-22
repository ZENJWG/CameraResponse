// Internal single-translation-unit fragment: relies on GTA SDK types and anonymous namespace helpers.
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

    // Presets shape category balance and response character on top of the
    // detailed INI values. They are intentionally multipliers rather than
    // replacements so advanced users can still tune individual systems.
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
    // export, UltimFPC_IsFirstPerson(), which Camera Response resolves at runtime.
    float idleScale = 1.0f;
    float walkScale = 1.0f;
    float runScale = 1.15f;
    float sprintScale = 1.30f;
    float aimScale = 0.38f;
    float crouchScale = 0.72f;
    float airborneScale = 0.30f;

    // Movement below this horizontal speed is treated as stationary. This
    // prevents tiny physics/animation velocity noise from continuously
    // feeding the locomotion layers while CJ is visually idle.
    float movementDeadzone = 0.012f;

    bool locomotionEnabled = true;
    bool footstepsEnabled = true;
    bool inertiaEnabled = true;
    bool jumpLandEnabled = true;
    bool damageEnabled = true;
    bool explosionsEnabled = true;

    // Optional nearby NPC gunfire camera reaction. This is separate from
    // player recoil: non-player shots create a distance-scaled pressure jolt.
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

    // Optional ambient response to nearby running vehicles. The player's own
    // vehicle is excluded because it already has a dedicated vehicle camera
    // layer. Nearby vehicles contribute a subtle distance/speed/acceleration
    // dependent engine/chassis vibration on both on-foot and vehicle cameras.
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

    // Extended physical response layers.
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

    // Advanced response pass.
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

    // Category/accessibility scales. HighFrequency can be set to zero to keep
    // large physical motion while removing buzz/texture layers.
    float locomotionResponseScale = 1.0f;
    float combatResponseScale = 1.0f;
    float vehicleResponseScale = 1.0f;
    float environmentResponseScale = 1.0f;
    float highFrequencyScale = 1.0f;

    // v1.0 response shaping and developer features.
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

    // Always-alive floating layer, in camera-local world units.
    float idleSide = 0.0450f;
    float idleVertical = 0.0550f;
    float idleForward = 0.0150f;

    // After CJ has been genuinely stationary for a short time, retain only a
    // restrained "alive" motion instead of letting the camera visibly orbit.
    float idleSettleDelay = 0.55f;
    float idleSettledScale = 0.0f;
    float aimBreathingSide = 0.010f;
    float aimBreathingVertical = 0.016f;
    float aimBreathingForward = 0.006f;
    float aimBreathingRate = 0.82f;

    // Foot-bone gait layer.
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

    // Camera-look and movement inertia.
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
    // Generic melee-weapon contact fallback.
    float meleeImpact = 0.42f;
    float meleeSide = 0.12f;

    // Player fist / fighting-style response.  These use the live
    // CTaskSimpleFight state and active animation phase rather than raw input.
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

    // Small orientation changes are much more perceptible than translation
    // alone at GTA SA's normal third-person camera distance.
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

    // Vehicle camera overhaul. All values are translation-only and are
    // evaluated independently from on-foot locomotion.
    bool vehicleEnabled = true;
    float vehicleStrength = 1.0f;
    float vehicleAccelForward = 1.05f;
    float vehicleBrakeForward = 1.40f;
    float vehicleCornerSide = 0.92f;
    float vehicleVerticalAccel = 0.72f;
    float vehicleSpeedVibration = 0.022f;
    float vehicleSpeedVibrationRate = 5.6f;

    // Subtle body vibration while the vehicle horn is actually sounding.
    // Uses CVehicle::m_nHornCounter rather than input state so controller,
    // keyboard and scripted horn playback all follow the vehicle itself.
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

    // Crash damage confirmation. A physics spike gives the immediate jolt;
    // actual health loss reinforces it only when a collision happened at the
    // same time, preventing bullets/fire from being treated as a crash.
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

    // CAutomobile suspension compression: 0=completely compressed,
    // 1=completely relaxed. Rapid compression changes reinforce curb,
    // pothole and hard-landing impacts.
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

    // Recoil is deliberately independent from the normal aim dampener.
    // Translation values are camera-local world-unit impulses.
    float recoilVerticalKick = 0.145f;
    float recoilForwardKick = 0.060f;
    float recoilSideKick = 0.020f;

    // Weapon recoil is translation-only for camera-matrix safety.
    // Position kick guarantees an immediate visible response; velocity kick
    // gives the motion a short springy continuation.
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

    // Separate response rates stop short weapon impulses from inheriting the
    // slow/heavy decay intended for landings and explosions.
    float recoilSpringMultiplier = 1.75f;
    float recoilDampingMultiplier = 1.75f;
    float idleDampingMultiplier = 1.65f;

    bool debug = false;
    unsigned logEveryMs = 700;
    bool logSteps = false;
    bool logShots = false;
    bool overlayEnabled = false;

    bool auditEnabled = false;
    bool auditConfigValues = true;
    bool auditFunctionCalls = true;
    unsigned auditEveryMs = 5000;

    unsigned nearbyVehicleScanMs = 33;
    unsigned surfaceScanMs = 80;
    bool scanProfilingEnabled = false;
    unsigned scanProfileEveryMs = 5000;

    std::string dir;
    std::string iniPath;

    static std::string ModuleDirectory() {
        HMODULE module = nullptr;

        // Resolve the module from executable code that lives in this ASI.
        // Do not use the EXE directory, current working directory, or a
        // modloader-specific path.
        if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&CameraResponseModuleAnchor),
                &module) ||
            !module) {
            return {};
        }

        // Grow the buffer if necessary instead of assuming MAX_PATH.
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

            // Windows returns size when the buffer was too small.
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
        char value[128] = {};
        std::snprintf(def, sizeof(def), "%.6f", fallback);
        GetPrivateProfileStringA(
            section, key, def, value, sizeof(value), path.c_str()
        );
        return cr::ParseIniFloat(value, fallback);
    }

    static int ReadInt(
        const std::string& path,
        const char* section,
        const char* key,
        int fallback
    ) {
        char def[32] = {};
        char value[128] = {};
        std::snprintf(def, sizeof(def), "%d", fallback);
        GetPrivateProfileStringA(
            section, key, def, value, sizeof(value), path.c_str()
        );
        return cr::ParseIniInt(value, fallback);
    }

    static bool ReadBool(
        const std::string& path,
        const char* section,
        const char* key,
        bool fallback
    ) {
        return ReadInt(
            path, section, key, fallback ? 1 : 0
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
        if (!out || outSize == 0) return;
        GetPrivateProfileStringA(section, key, fallback, out, outSize, path.c_str());
        cr::StripIniComment(out);
    }

    void Load() {
        if (dir.empty())
            dir = ModuleDirectory();

        // If module resolution ever fails, leave iniPath empty rather than
        // accidentally reading CameraResponse.ini from GTA's working directory.
        if (dir.empty()) {
            iniPath.clear();
            return;
        }

        iniPath = dir + "CameraResponse.ini";

        enabled = ReadBool(iniPath, "General", "Enabled", enabled);
        strength = ReadFloat(iniPath, "General", "Strength", strength);

        masterOverallScale = ReadFloat(iniPath, "MasterTuning", "Overall", masterOverallScale);
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
        contextLowHealthThreshold = ReadFloat(iniPath, "ContextAttenuation", "LowHealthThreshold", contextLowHealthThreshold);
        contextScriptedScale = ReadFloat(iniPath, "ContextAttenuation", "ScriptedCamera", contextScriptedScale);
        contextIndoorScanMs = static_cast<unsigned>(Clamp(
            ReadInt(iniPath, "ContextAttenuation", "IndoorScanMs", static_cast<int>(contextIndoorScanMs)),
            50, 2000));
        contextIndoorCeilingHeight = ReadFloat(iniPath, "ContextAttenuation", "IndoorCeilingHeight", contextIndoorCeilingHeight);

        directionalResponseEnabled = ReadBool(iniPath, "DirectionalResponse", "Enabled", directionalResponseEnabled);
        directionalSourceSide = ReadFloat(iniPath, "DirectionalResponse", "SourceSide", directionalSourceSide);
        directionalSourceForward = ReadFloat(iniPath, "DirectionalResponse", "SourceForward", directionalSourceForward);
        directionalSourceVertical = ReadFloat(iniPath, "DirectionalResponse", "SourceVertical", directionalSourceVertical);
        directionalDoorScale = ReadFloat(iniPath, "DirectionalResponse", "DoorScale", directionalDoorScale);
        directionalWaterScale = ReadFloat(iniPath, "DirectionalResponse", "WaterEntryScale", directionalWaterScale);
        directionalVehicleTransitionScale = ReadFloat(iniPath, "DirectionalResponse", "VehicleTransitionScale", directionalVehicleTransitionScale);
        directionalSwimScale = ReadFloat(iniPath, "DirectionalResponse", "SwimmingScale", directionalSwimScale);
        directionalParachuteScale = ReadFloat(iniPath, "DirectionalResponse", "ParachuteScale", directionalParachuteScale);
        directionalLargeVehicleScale = ReadFloat(iniPath, "DirectionalResponse", "LargeVehicleScale", directionalLargeVehicleScale);

        responseBudgetEnabled = ReadBool(iniPath, "ResponseBudget", "Enabled", responseBudgetEnabled);
        responseBudgetNormalized = ReadFloat(iniPath, "ResponseBudget", "NormalizedLimit", responseBudgetNormalized);
        responseBudgetCriticalReserve = ReadFloat(iniPath, "ResponseBudget", "CriticalReserve", responseBudgetCriticalReserve);
        responseBudgetSoftness = ReadFloat(iniPath, "ResponseBudget", "Softness", responseBudgetSoftness);
        ambientDuckingEnabled = ReadBool(iniPath, "ResponseBudget", "AmbientDucking", ambientDuckingEnabled);
        ambientDuckAttack = ReadFloat(iniPath, "ResponseBudget", "DuckAttack", ambientDuckAttack);
        ambientDuckRelease = ReadFloat(iniPath, "ResponseBudget", "DuckRelease", ambientDuckRelease);
        ambientDuckMinimum = ReadFloat(iniPath, "ResponseBudget", "DuckMinimum", ambientDuckMinimum);
        ambientDuckHoldMs = static_cast<unsigned>(Clamp(ReadInt(iniPath, "ResponseBudget", "DuckHoldMs", static_cast<int>(ambientDuckHoldMs)), 0, 2000));

        directionalRotationEnabled = ReadBool(iniPath, "DirectionalRotation", "Enabled", directionalRotationEnabled);
        directionalRotationRollDeg = ReadFloat(iniPath, "DirectionalRotation", "RollDeg", directionalRotationRollDeg);
        directionalRotationPitchDeg = ReadFloat(iniPath, "DirectionalRotation", "PitchDeg", directionalRotationPitchDeg);
        directionalRotationYawDeg = ReadFloat(iniPath, "DirectionalRotation", "YawDeg", directionalRotationYawDeg);
        directionalRotationSpring = ReadFloat(iniPath, "DirectionalRotation", "Spring", directionalRotationSpring);
        directionalRotationDamping = ReadFloat(iniPath, "DirectionalRotation", "Damping", directionalRotationDamping);

        jerkResponseEnabled = ReadBool(iniPath, "JerkResponse", "Enabled", jerkResponseEnabled);
        playerJerkStrength = ReadFloat(iniPath, "JerkResponse", "PlayerStrength", playerJerkStrength);
        vehicleJerkStrength = ReadFloat(iniPath, "JerkResponse", "VehicleStrength", vehicleJerkStrength);
        jerkDeadzone = ReadFloat(iniPath, "JerkResponse", "Deadzone", jerkDeadzone);
        jerkMax = ReadFloat(iniPath, "JerkResponse", "Max", jerkMax);

        nearbyApproachInfluence = ReadFloat(iniPath, "NearbyPassageResponse", "ApproachInfluence", nearbyApproachInfluence);
        nearbyPassInfluence = ReadFloat(iniPath, "NearbyPassageResponse", "PassInfluence", nearbyPassInfluence);
        rotorWashDistance = ReadFloat(iniPath, "NearbyPassageResponse", "RotorWashDistance", rotorWashDistance);
        rotorWashHeight = ReadFloat(iniPath, "NearbyPassageResponse", "RotorWashHeight", rotorWashHeight);
        rotorWashStrength = ReadFloat(iniPath, "NearbyPassageResponse", "RotorWashStrength", rotorWashStrength);

        explosionPressureIndoorScale = ReadFloat(iniPath, "ExplosionPressure", "IndoorScale", explosionPressureIndoorScale);
        explosionPressureBelowBoost = ReadFloat(iniPath, "ExplosionPressure", "BelowBoost", explosionPressureBelowBoost);
        explosionPressureAboveScale = ReadFloat(iniPath, "ExplosionPressure", "AboveScale", explosionPressureAboveScale);
        explosionPressureLateralRoll = ReadFloat(iniPath, "ExplosionPressure", "LateralRoll", explosionPressureLateralRoll);

        footstepSurfaceBandStrength = ReadFloat(iniPath, "SurfaceSteps", "FootstepBandStrength", footstepSurfaceBandStrength);

        sourceWatchdogEnabled = ReadBool(iniPath, "Safety", "SourceWatchdog", sourceWatchdogEnabled);
        sourceReleaseMs = static_cast<unsigned>(Clamp(
            ReadInt(iniPath, "Safety", "SourceReleaseMs", static_cast<int>(sourceReleaseMs)),
            50, 10000));
        sourceContinuousMaxMs = static_cast<unsigned>(Clamp(
            ReadInt(iniPath, "Safety", "ContinuousMaxMs", static_cast<int>(sourceContinuousMaxMs)),
            250, 60000));
        globalTailMs = static_cast<unsigned>(Clamp(
            ReadInt(iniPath, "Safety", "GlobalTailMs", static_cast<int>(globalTailMs)),
            250, 15000));
        stuckSpringEpsilon = Clamp(
            ReadFloat(iniPath, "Safety", "SpringEpsilon", stuckSpringEpsilon),
            0.0001f, 0.0500f);
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
            // Low-fatigue cinematic response: keep informative impacts while
            // reducing constant motion and aggressive FOV/rotation changes.
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
            // Weighty physical response with stronger displacement and impacts.
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
            // Fast, punchy response: restrained ambient motion, strong attack
            // transients, brisk recovery, noticeable but controlled FOV/roll.
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
            // Comfort is handled by explicit ambient-source shutdown below.
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
            // Default: balanced, readable response without excessive ambient
            // drift. This is the baseline all detailed values are authored for.
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

        idleScale = ReadFloat(iniPath, "Movement", "Idle", idleScale);
        walkScale = ReadFloat(iniPath, "Movement", "Walk", walkScale);
        runScale = ReadFloat(iniPath, "Movement", "Run", runScale);
        sprintScale = ReadFloat(iniPath, "Movement", "Sprint", sprintScale);
        aimScale = ReadFloat(iniPath, "Movement", "Aim", aimScale);
        crouchScale = ReadFloat(iniPath, "Movement", "Crouch", crouchScale);
        airborneScale = ReadFloat(iniPath, "Movement", "Airborne", airborneScale);
        movementDeadzone = ReadFloat(iniPath, "Movement", "Deadzone", movementDeadzone);
        locomotionEnabled = ReadBool(iniPath, "General", "LocomotionEnabled", locomotionEnabled);
        footstepsEnabled = ReadBool(iniPath, "General", "FootstepsEnabled", footstepsEnabled);
        inertiaEnabled = ReadBool(iniPath, "General", "InertiaEnabled", inertiaEnabled);
        jumpLandEnabled = ReadBool(iniPath, "General", "JumpLandEnabled", jumpLandEnabled);
        damageEnabled = ReadBool(iniPath, "General", "DamageEnabled", damageEnabled);
        explosionsEnabled = ReadBool(iniPath, "General", "ExplosionsEnabled", explosionsEnabled);
        nearbyGunfireEnabled = ReadBool(iniPath, "NearbyGunfire", "Enabled", nearbyGunfireEnabled);
        nearbyGunfireInVehicle = ReadBool(iniPath, "NearbyGunfire", "AffectInVehicle", nearbyGunfireInVehicle);
        nearbyGunfireStrength = ReadFloat(iniPath, "NearbyGunfire", "Strength", nearbyGunfireStrength);
        nearbyGunfireMinDistance = ReadFloat(iniPath, "NearbyGunfire", "MinDistance", nearbyGunfireMinDistance);
        nearbyGunfireMaxDistance = ReadFloat(iniPath, "NearbyGunfire", "MaxDistance", nearbyGunfireMaxDistance);
        nearbyGunfireFalloffPower = ReadFloat(iniPath, "NearbyGunfire", "FalloffPower", nearbyGunfireFalloffPower);
        nearbyGunfireSide = ReadFloat(iniPath, "NearbyGunfire", "Side", nearbyGunfireSide);
        nearbyGunfireVertical = ReadFloat(iniPath, "NearbyGunfire", "Vertical", nearbyGunfireVertical);
        nearbyGunfireForward = ReadFloat(iniPath, "NearbyGunfire", "Forward", nearbyGunfireForward);
        nearbyGunfireMaxResponseShotsPerFrame = static_cast<unsigned>(Clamp(
            ReadInt(iniPath, "NearbyGunfire", "MaxResponseShotsPerFrame",
                static_cast<int>(nearbyGunfireMaxResponseShotsPerFrame)),
            1,
            16
        ));
        nearbyGunfireScanMs = static_cast<unsigned>(Clamp(
            ReadInt(iniPath, "NearbyGunfire", "ScanMs",
                static_cast<int>(nearbyGunfireScanMs)),
            0,
            100
        ));

        nearbyVehiclesEnabled = ReadBool(iniPath, "NearbyVehicles", "Enabled", nearbyVehiclesEnabled);
        nearbyVehiclesOnFoot = ReadBool(iniPath, "NearbyVehicles", "AffectOnFoot", nearbyVehiclesOnFoot);
        nearbyVehiclesInVehicle = ReadBool(iniPath, "NearbyVehicles", "AffectInVehicle", nearbyVehiclesInVehicle);
        nearbyVehiclesStrength = ReadFloat(iniPath, "NearbyVehicles", "Strength", nearbyVehiclesStrength);
        nearbyVehiclesMinDistance = ReadFloat(iniPath, "NearbyVehicles", "MinDistance", nearbyVehiclesMinDistance);
        nearbyVehiclesMaxDistance = ReadFloat(iniPath, "NearbyVehicles", "MaxDistance", nearbyVehiclesMaxDistance);
        nearbyVehiclesFalloffPower = ReadFloat(iniPath, "NearbyVehicles", "FalloffPower", nearbyVehiclesFalloffPower);
        nearbyVehiclesIdleScale = ReadFloat(iniPath, "NearbyVehicles", "IdleScale", nearbyVehiclesIdleScale);
        nearbyVehiclesLowSpeedScale = ReadFloat(iniPath, "NearbyVehicles", "LowSpeedScale", nearbyVehiclesLowSpeedScale);
        nearbyVehiclesHighSpeedScale = ReadFloat(iniPath, "NearbyVehicles", "HighSpeedScale", nearbyVehiclesHighSpeedScale);
        nearbyVehiclesAccelerationScale = ReadFloat(iniPath, "NearbyVehicles", "AccelerationScale", nearbyVehiclesAccelerationScale);
        nearbyVehiclesVertical = ReadFloat(iniPath, "NearbyVehicles", "Vertical", nearbyVehiclesVertical);
        nearbyVehiclesSide = ReadFloat(iniPath, "NearbyVehicles", "Side", nearbyVehiclesSide);
        nearbyVehiclesForward = ReadFloat(iniPath, "NearbyVehicles", "Forward", nearbyVehiclesForward);
        nearbyVehiclesMaxCombined = ReadFloat(iniPath, "NearbyVehicles", "MaxCombined", nearbyVehiclesMaxCombined);
        nearbyVehiclesMaxContributors = static_cast<unsigned>(std::max(1, ReadInt(iniPath, "NearbyVehicles", "MaxContributors", static_cast<int>(nearbyVehiclesMaxContributors))));

        doorResponseEnabled = ReadBool(iniPath, "DoorResponse", "Enabled", doorResponseEnabled);
        vehicleDoorResponseEnabled = ReadBool(iniPath, "DoorResponse", "VehicleDoors", vehicleDoorResponseEnabled);
        buildingDoorResponseEnabled = ReadBool(iniPath, "DoorResponse", "BuildingDoors", buildingDoorResponseEnabled);
        doorResponseInVehicle = ReadBool(iniPath, "DoorResponse", "AffectInVehicle", doorResponseInVehicle);
        doorResponseStrength = ReadFloat(iniPath, "DoorResponse", "Strength", doorResponseStrength);
        vehicleDoorStrength = ReadFloat(iniPath, "DoorResponse", "VehicleStrength", vehicleDoorStrength);
        buildingDoorStrength = ReadFloat(iniPath, "DoorResponse", "BuildingStrength", buildingDoorStrength);
        doorResponseMinDistance = ReadFloat(iniPath, "DoorResponse", "MinDistance", doorResponseMinDistance);
        doorResponseMaxDistance = ReadFloat(iniPath, "DoorResponse", "MaxDistance", doorResponseMaxDistance);
        doorResponseFalloffPower = ReadFloat(iniPath, "DoorResponse", "FalloffPower", doorResponseFalloffPower);
        doorResponseSide = ReadFloat(iniPath, "DoorResponse", "Side", doorResponseSide);
        doorResponseVertical = ReadFloat(iniPath, "DoorResponse", "Vertical", doorResponseVertical);
        doorResponseForward = ReadFloat(iniPath, "DoorResponse", "Forward", doorResponseForward);
        doorVehicleOwnScale = ReadFloat(iniPath, "DoorResponse", "OwnVehicleScale", doorVehicleOwnScale);
        vehicleDoorCloseThreshold = ReadFloat(iniPath, "DoorResponse", "VehicleCloseThreshold", vehicleDoorCloseThreshold);
        vehicleDoorWasOpenThreshold = ReadFloat(iniPath, "DoorResponse", "VehicleWasOpenThreshold", vehicleDoorWasOpenThreshold);
        doorScanMs = static_cast<unsigned>(Clamp(ReadInt(iniPath, "DoorResponse", "ScanMs", static_cast<int>(doorScanMs)), 20, 200));
        doorBonnetScale = ReadFloat(iniPath, "DoorResponse", "BonnetScale", doorBonnetScale);
        doorBootScale = ReadFloat(iniPath, "DoorResponse", "BootScale", doorBootScale);

        vehicleMechanicalEnabled = ReadBool(iniPath, "ExtendedVehicle", "Enabled", vehicleMechanicalEnabled);
        vehicleEnterStrength = ReadFloat(iniPath, "ExtendedVehicle", "EnterStrength", vehicleEnterStrength);
        vehicleExitStrength = ReadFloat(iniPath, "ExtendedVehicle", "ExitStrength", vehicleExitStrength);
        vehicleBailOutEnabled = ReadBool(iniPath, "ExtendedVehicle", "BailOutEnabled", vehicleBailOutEnabled);
        vehicleBailOutMinSpeed = Clamp(
            ReadFloat(iniPath, "ExtendedVehicle", "BailOutMinSpeed", vehicleBailOutMinSpeed),
            0.05f, 1.50f);
        vehicleBailOutStrength = Clamp(
            ReadFloat(iniPath, "ExtendedVehicle", "BailOutStrength", vehicleBailOutStrength),
            0.0f, 2.0f);
        vehicleBailAirborneWindowMs = static_cast<unsigned>(Clamp(
            ReadInt(iniPath, "ExtendedVehicle", "BailAirborneWindowMs", static_cast<int>(vehicleBailAirborneWindowMs)),
            100, 1500));
        vehicleBailLandingScale = Clamp(
            ReadFloat(iniPath, "ExtendedVehicle", "BailLandingScale", vehicleBailLandingScale),
            0.5f, 3.0f);
        vehicleBailFovKick = Clamp(
            ReadFloat(iniPath, "ExtendedVehicle", "BailFOVKick", vehicleBailFovKick),
            -0.10f, 0.08f);
        engineStartStrength = ReadFloat(iniPath, "ExtendedVehicle", "EngineStartStrength", engineStartStrength);
        engineStopStrength = ReadFloat(iniPath, "ExtendedVehicle", "EngineStopStrength", engineStopStrength);
        gearShiftStrength = ReadFloat(iniPath, "ExtendedVehicle", "GearShiftStrength", gearShiftStrength);
        engineRpmVibration = ReadFloat(iniPath, "ExtendedVehicle", "EngineRPMVibration", engineRpmVibration);
        engineRpmMinHz = ReadFloat(iniPath, "ExtendedVehicle", "EngineRPMMinHz", engineRpmMinHz);
        engineRpmMaxHz = ReadFloat(iniPath, "ExtendedVehicle", "EngineRPMMaxHz", engineRpmMaxHz);
        engineLoadInfluence = ReadFloat(iniPath, "ExtendedVehicle", "EngineLoadInfluence", engineLoadInfluence);
        bikeEngineScale = ReadFloat(iniPath, "ExtendedVehicle", "BikeEngineScale", bikeEngineScale);
        gearShiftCarScale = ReadFloat(iniPath, "ExtendedVehicle", "GearShiftCarScale", gearShiftCarScale);
        gearShiftBikeScale = ReadFloat(iniPath, "ExtendedVehicle", "GearShiftBikeScale", gearShiftBikeScale);
        gearShiftPulseRecovery = ReadFloat(iniPath, "ExtendedVehicle", "GearShiftPulseRecovery", gearShiftPulseRecovery);
        tyreBlowoutStrength = ReadFloat(iniPath, "ExtendedVehicle", "TyreBlowoutStrength", tyreBlowoutStrength);
        axleLandingStrength = ReadFloat(iniPath, "ExtendedVehicle", "AxleLandingStrength", axleLandingStrength);

        waterResponseEnabled = ReadBool(iniPath, "WorldResponse", "WaterResponse", waterResponseEnabled);
        waterEntryStrength = ReadFloat(iniPath, "WorldResponse", "WaterEntryStrength", waterEntryStrength);
        largeVehicleResponseEnabled = ReadBool(iniPath, "WorldResponse", "LargeVehicles", largeVehicleResponseEnabled);
        trainResponseStrength = ReadFloat(iniPath, "WorldResponse", "TrainStrength", trainResponseStrength);
        trainResponseDistance = ReadFloat(iniPath, "WorldResponse", "TrainDistance", trainResponseDistance);
        aircraftResponseStrength = ReadFloat(iniPath, "WorldResponse", "AircraftStrength", aircraftResponseStrength);
        aircraftResponseDistance = ReadFloat(iniPath, "WorldResponse", "AircraftDistance", aircraftResponseDistance);
        aircraftMaxContinuousMs = static_cast<unsigned>(Clamp(ReadInt(iniPath, "WorldResponse", "AircraftMaxContinuousMs", static_cast<int>(aircraftMaxContinuousMs)), 0, 30000));
        aircraftRearmMs = static_cast<unsigned>(Clamp(ReadInt(iniPath, "WorldResponse", "AircraftRearmMs", static_cast<int>(aircraftRearmMs)), 0, 10000));
        largeVehicleScanMs = static_cast<unsigned>(Clamp(ReadInt(iniPath, "WorldResponse", "LargeVehicleScanMs", static_cast<int>(largeVehicleScanMs)), 33, 250));

        worldObjectResponseEnabled = ReadBool(iniPath, "WorldResponse", "ObjectImpacts", worldObjectResponseEnabled);
        worldObjectImpactStrength = ReadFloat(iniPath, "WorldResponse", "ObjectImpactStrength", worldObjectImpactStrength);
        worldObjectImpactDistance = ReadFloat(iniPath, "WorldResponse", "ObjectImpactDistance", worldObjectImpactDistance);
        worldObjectScanMs = static_cast<unsigned>(Clamp(ReadInt(iniPath, "WorldResponse", "ObjectScanMs", static_cast<int>(worldObjectScanMs)), 33, 250));
        worldObjectContactReleaseMs = static_cast<unsigned>(Clamp(ReadInt(iniPath, "WorldResponse", "ObjectContactReleaseMs", static_cast<int>(worldObjectContactReleaseMs)), 80, 1000));
        worldObjectImpactCooldownMs = static_cast<unsigned>(Clamp(ReadInt(iniPath, "WorldResponse", "ObjectImpactCooldownMs", static_cast<int>(worldObjectImpactCooldownMs)), 80, 1500));
        worldObjectReimpactDeltaV = Clamp(ReadFloat(iniPath, "WorldResponse", "ObjectReimpactDeltaV", worldObjectReimpactDeltaV), 0.01f, 1.0f);
        worldObjectReimpactDamageRise = Clamp(ReadFloat(iniPath, "WorldResponse", "ObjectReimpactDamageRise", worldObjectReimpactDamageRise), 0.05f, 20.0f);

        pedWorldResponseEnabled = ReadBool(iniPath, "WorldResponse", "PedImpacts", pedWorldResponseEnabled);
        pedImpactStrength = ReadFloat(iniPath, "WorldResponse", "PedImpactStrength", pedImpactStrength);
        heavyFootstepStrength = ReadFloat(iniPath, "WorldResponse", "HeavyFootstepStrength", heavyFootstepStrength);
        heavyFootstepDistance = ReadFloat(iniPath, "WorldResponse", "HeavyFootstepDistance", heavyFootstepDistance);
        pedWorldScanMs = static_cast<unsigned>(Clamp(ReadInt(iniPath, "WorldResponse", "PedScanMs", static_cast<int>(pedWorldScanMs)), 33, 200));

        weatherResponseEnabled = ReadBool(iniPath, "EnvironmentResponse", "Enabled", weatherResponseEnabled);
        windResponseStrength = ReadFloat(iniPath, "EnvironmentResponse", "WindStrength", windResponseStrength);
        rainResponseStrength = ReadFloat(iniPath, "EnvironmentResponse", "RainStrength", rainResponseStrength);
        sandstormResponseStrength = ReadFloat(iniPath, "EnvironmentResponse", "SandstormStrength", sandstormResponseStrength);
        earthquakeResponseStrength = ReadFloat(iniPath, "EnvironmentResponse", "EarthquakeStrength", earthquakeResponseStrength);
        weatherSmoothing = ReadFloat(iniPath, "EnvironmentResponse", "Smoothing", weatherSmoothing);
        windGustStrength = ReadFloat(iniPath, "EnvironmentResponse", "WindGustStrength", windGustStrength);
        windGustRate = ReadFloat(iniPath, "EnvironmentResponse", "WindGustRate", windGustRate);
        rainTextureRate = ReadFloat(iniPath, "EnvironmentResponse", "RainTextureRate", rainTextureRate);
        rainVerticalBias = ReadFloat(iniPath, "EnvironmentResponse", "RainVerticalBias", rainVerticalBias);
        sandstormLowFrequency = ReadFloat(iniPath, "EnvironmentResponse", "SandstormLowFrequency", sandstormLowFrequency);
        earthquakeMaxOffset = ReadFloat(iniPath, "EnvironmentResponse", "EarthquakeMaxOffset", earthquakeMaxOffset);
        weatherMaxAmbient = ReadFloat(iniPath, "EnvironmentResponse", "MaxAmbient", weatherMaxAmbient);

        obstructionResponseEnabled = ReadBool(iniPath, "CameraObstruction", "Enabled", obstructionResponseEnabled);
        obstructionThreshold = ReadFloat(iniPath, "CameraObstruction", "Threshold", obstructionThreshold);
        obstructionStrength = ReadFloat(iniPath, "CameraObstruction", "Strength", obstructionStrength);
        obstructionSpring = ReadFloat(iniPath, "CameraObstruction", "Spring", obstructionSpring);
        obstructionDamping = ReadFloat(iniPath, "CameraObstruction", "Damping", obstructionDamping);

        environmentPriorityMixEnabled = ReadBool(iniPath, "Performance", "EnvironmentPriorityMix", environmentPriorityMixEnabled);
        environmentMixMaxImpulse = ReadFloat(iniPath, "Performance", "EnvironmentMixMaxImpulse", environmentMixMaxImpulse);

        vehiclePersonalityEnabled = ReadBool(iniPath, "ExtendedVehicle", "HandlingPersonality", vehiclePersonalityEnabled);
        collisionMaterialInfluence = ReadFloat(iniPath, "ExtendedVehicle", "CollisionMaterialInfluence", collisionMaterialInfluence);

        advancedResponseEnabled = ReadBool(iniPath, "AdvancedResponse", "Enabled", advancedResponseEnabled);
        reloadResponseStrength = ReadFloat(iniPath, "AdvancedResponse", "ReloadStrength", reloadResponseStrength);
        equipResponseStrength = ReadFloat(iniPath, "AdvancedResponse", "EquipStrength", equipResponseStrength);
        nearMissEnabled = ReadBool(iniPath, "AdvancedResponse", "NearMiss", nearMissEnabled);
        nearMissStrength = ReadFloat(iniPath, "AdvancedResponse", "NearMissStrength", nearMissStrength);
        nearMissDistance = ReadFloat(iniPath, "AdvancedResponse", "NearMissDistance", nearMissDistance);
        nearMissMaxShooterDistance = ReadFloat(iniPath, "AdvancedResponse", "NearMissMaxShooterDistance", nearMissMaxShooterDistance);

        swimResponseStrength = ReadFloat(iniPath, "AdvancedResponse", "SwimStrength", swimResponseStrength);
        climbResponseStrength = ReadFloat(iniPath, "AdvancedResponse", "ClimbStrength", climbResponseStrength);
        parachuteDeployStrength = ReadFloat(iniPath, "AdvancedResponse", "ParachuteDeployStrength", parachuteDeployStrength);
        parachuteAirStrength = ReadFloat(iniPath, "AdvancedResponse", "ParachuteAirStrength", parachuteAirStrength);
        parachuteLandStrength = ReadFloat(iniPath, "AdvancedResponse", "ParachuteLandStrength", parachuteLandStrength);
        jetpackStrength = ReadFloat(iniPath, "AdvancedResponse", "JetpackStrength", jetpackStrength);
        jetpackTransitionStrength = ReadFloat(iniPath, "AdvancedResponse", "JetpackTransitionStrength", jetpackTransitionStrength);
        jetpackRecoilScale = Clamp(ReadFloat(iniPath, "JetpackResponse", "RecoilScale", jetpackRecoilScale), 0.0f, 4.0f);
        mountedWeaponResponseEnabled = ReadBool(iniPath, "MountedWeapons", "Enabled", mountedWeaponResponseEnabled);
        mountedGunStrength = Clamp(ReadFloat(iniPath, "MountedWeapons", "GunStrength", mountedGunStrength), 0.0f, 0.15f);
        mountedHeavyStrength = Clamp(ReadFloat(iniPath, "MountedWeapons", "HeavyStrength", mountedHeavyStrength), 0.0f, 0.25f);
        mountedWaterStrength = Clamp(ReadFloat(iniPath, "MountedWeapons", "WaterStrength", mountedWaterStrength), 0.0f, 0.10f);

        jetpackDirectionalEnabled = ReadBool(iniPath, "JetpackResponse", "Enabled", jetpackDirectionalEnabled);
        jetpackDirectionalStrength = ReadFloat(iniPath, "JetpackResponse", "DirectionalStrength", jetpackDirectionalStrength);
        jetpackForwardScale = ReadFloat(iniPath, "JetpackResponse", "ForwardScale", jetpackForwardScale);
        jetpackBackwardScale = ReadFloat(iniPath, "JetpackResponse", "BackwardScale", jetpackBackwardScale);
        jetpackSideScale = ReadFloat(iniPath, "JetpackResponse", "SideScale", jetpackSideScale);
        jetpackVerticalScale = ReadFloat(iniPath, "JetpackResponse", "VerticalScale", jetpackVerticalScale);
        jetpackAccelInfluence = ReadFloat(iniPath, "JetpackResponse", "AccelerationInfluence", jetpackAccelInfluence);
        jetpackVelocityInfluence = ReadFloat(iniPath, "JetpackResponse", "VelocityInfluence", jetpackVelocityInfluence);
        jetpackHoverVibration = ReadFloat(iniPath, "JetpackResponse", "HoverVibration", jetpackHoverVibration);
        jetpackHoverHz = ReadFloat(iniPath, "JetpackResponse", "HoverHz", jetpackHoverHz);
        jetpackSmoothing = ReadFloat(iniPath, "JetpackResponse", "Smoothing", jetpackSmoothing);
        jetpackMaxSide = ReadFloat(iniPath, "JetpackResponse", "MaxSide", jetpackMaxSide);
        jetpackMaxVertical = ReadFloat(iniPath, "JetpackResponse", "MaxVertical", jetpackMaxVertical);
        jetpackMaxForward = ReadFloat(iniPath, "JetpackResponse", "MaxForward", jetpackMaxForward);

        bikeLeanStrength = ReadFloat(iniPath, "AdvancedResponse", "BikeLeanStrength", bikeLeanStrength);
        bikePitchStrength = ReadFloat(iniPath, "AdvancedResponse", "BikePitchStrength", bikePitchStrength);
        bicyclePedalStrength = ReadFloat(iniPath, "AdvancedResponse", "BicyclePedalStrength", bicyclePedalStrength);
        boatWaveStrength = ReadFloat(iniPath, "AdvancedResponse", "BoatWaveStrength", boatWaveStrength);

        boatWavePhysicalEnabled = ReadBool(iniPath, "BoatResponse", "Enabled", boatWavePhysicalEnabled);
        boatWaveHeaveScale = ReadFloat(iniPath, "BoatResponse", "HeaveScale", boatWaveHeaveScale);
        boatWavePitchScale = ReadFloat(iniPath, "BoatResponse", "PitchScale", boatWavePitchScale);
        boatWaveRollScale = ReadFloat(iniPath, "BoatResponse", "RollScale", boatWaveRollScale);
        boatWaveOrientationHold = ReadFloat(iniPath, "BoatResponse", "OrientationHold", boatWaveOrientationHold);
        boatWaveSpeedInfluence = ReadFloat(iniPath, "BoatResponse", "SpeedInfluence", boatWaveSpeedInfluence);
        boatWaveSmoothing = ReadFloat(iniPath, "BoatResponse", "Smoothing", boatWaveSmoothing);
        boatWaveSlamThreshold = ReadFloat(iniPath, "BoatResponse", "SlamThreshold", boatWaveSlamThreshold);
        boatWaveSlamStrength = ReadFloat(iniPath, "BoatResponse", "SlamStrength", boatWaveSlamStrength);
        boatWaveSlamCooldownMs = static_cast<unsigned>(Clamp(
            ReadInt(iniPath, "BoatResponse", "SlamCooldownMs", static_cast<int>(boatWaveSlamCooldownMs)),
            80, 1000));
        boatWaveRotationPitchDeg = ReadFloat(iniPath, "BoatResponse", "RotationPitchDeg", boatWaveRotationPitchDeg);
        boatWaveRotationRollDeg = ReadFloat(iniPath, "BoatResponse", "RotationRollDeg", boatWaveRotationRollDeg);
        boatWaveMaxSide = ReadFloat(iniPath, "BoatResponse", "MaxSide", boatWaveMaxSide);
        boatWaveMaxVertical = ReadFloat(iniPath, "BoatResponse", "MaxVertical", boatWaveMaxVertical);
        boatWaveMaxForward = ReadFloat(iniPath, "BoatResponse", "MaxForward", boatWaveMaxForward);

        aircraftTurbulenceStrength = ReadFloat(iniPath, "AdvancedResponse", "AircraftTurbulenceStrength", aircraftTurbulenceStrength);
        aircraftLandingStrength = ReadFloat(iniPath, "AdvancedResponse", "AircraftLandingStrength", aircraftLandingStrength);

        aircraftPhysicalEnabled = ReadBool(iniPath, "AircraftResponse", "Enabled", aircraftPhysicalEnabled);
        aircraftAccelSideScale = ReadFloat(iniPath, "AircraftResponse", "AccelSideScale", aircraftAccelSideScale);
        aircraftAccelForwardScale = ReadFloat(iniPath, "AircraftResponse", "AccelForwardScale", aircraftAccelForwardScale);
        aircraftAccelVerticalScale = ReadFloat(iniPath, "AircraftResponse", "AccelVerticalScale", aircraftAccelVerticalScale);
        aircraftAttitudeHold = ReadFloat(iniPath, "AircraftResponse", "AttitudeHold", aircraftAttitudeHold);
        aircraftSmoothing = ReadFloat(iniPath, "AircraftResponse", "Smoothing", aircraftSmoothing);
        aircraftWeatherInfluence = ReadFloat(iniPath, "AircraftResponse", "WeatherInfluence", aircraftWeatherInfluence);
        aircraftPlaneTurbulenceScale = ReadFloat(iniPath, "AircraftResponse", "PlaneTurbulenceScale", aircraftPlaneTurbulenceScale);
        aircraftHeliTurbulenceScale = ReadFloat(iniPath, "AircraftResponse", "HeliTurbulenceScale", aircraftHeliTurbulenceScale);
        aircraftHeliRotorVibration = ReadFloat(iniPath, "AircraftResponse", "HeliRotorVibration", aircraftHeliRotorVibration);
        aircraftPlaneAirframeVibration = ReadFloat(iniPath, "AircraftResponse", "PlaneAirframeVibration", aircraftPlaneAirframeVibration);
        aircraftRotationPitchDeg = ReadFloat(iniPath, "AircraftResponse", "RotationPitchDeg", aircraftRotationPitchDeg);
        aircraftRotationRollDeg = ReadFloat(iniPath, "AircraftResponse", "RotationRollDeg", aircraftRotationRollDeg);
        aircraftTouchdownThreshold = ReadFloat(iniPath, "AircraftResponse", "TouchdownThreshold", aircraftTouchdownThreshold);
        aircraftTouchdownForwardScale = ReadFloat(iniPath, "AircraftResponse", "TouchdownForwardScale", aircraftTouchdownForwardScale);
        aircraftMaxSide = ReadFloat(iniPath, "AircraftResponse", "MaxSide", aircraftMaxSide);
        aircraftMaxVertical = ReadFloat(iniPath, "AircraftResponse", "MaxVertical", aircraftMaxVertical);
        aircraftMaxForward = ReadFloat(iniPath, "AircraftResponse", "MaxForward", aircraftMaxForward);
        trainJointStrength = ReadFloat(iniPath, "AdvancedResponse", "TrainJointStrength", trainJointStrength);
        skidStrength = ReadFloat(iniPath, "AdvancedResponse", "SkidStrength", skidStrength);
        engineVibrationStrength = ReadFloat(iniPath, "AdvancedResponse", "EngineVibrationStrength", engineVibrationStrength);
        roadTextureStrength = ReadFloat(iniPath, "AdvancedResponse", "RoadTextureStrength", roadTextureStrength);

        doorOpenStrength = ReadFloat(iniPath, "AdvancedResponse", "DoorOpenStrength", doorOpenStrength);
        machineryStrength = ReadFloat(iniPath, "AdvancedResponse", "MachineryStrength", machineryStrength);
        machineryDistance = ReadFloat(iniPath, "AdvancedResponse", "MachineryDistance", machineryDistance);
        nativeShakeBlend = Clamp(
            ReadFloat(iniPath, "AdvancedResponse", "NativeShakeBlend", nativeShakeBlend),
            0.0f, 1.0f);
        nativeShakeMaxContinuousMs = static_cast<unsigned>(Clamp(
            ReadInt(iniPath, "AdvancedResponse", "NativeShakeMaxContinuousMs", static_cast<int>(nativeShakeMaxContinuousMs)),
            0, 10000));
        nativeShakeRearmMs = static_cast<unsigned>(Clamp(
            ReadInt(iniPath, "AdvancedResponse", "NativeShakeRearmMs", static_cast<int>(nativeShakeRearmMs)),
            0, 5000));
        nativeShakeChangeEpsilon = Clamp(
            ReadFloat(iniPath, "AdvancedResponse", "NativeShakeChangeEpsilon", nativeShakeChangeEpsilon),
            0.0001f, 0.1000f);

        explosionShockDelayEnabled = ReadBool(iniPath, "AdvancedResponse", "ExplosionShockDelay", explosionShockDelayEnabled);
        explosionShockSpeed = ReadFloat(iniPath, "AdvancedResponse", "ExplosionShockSpeed", explosionShockSpeed);
        explosionShockSecondaryScale = ReadFloat(iniPath, "AdvancedResponse", "ExplosionShockSecondaryScale", explosionShockSecondaryScale);
        explosionOcclusionEnabled = ReadBool(iniPath, "AdvancedResponse", "ExplosionOcclusion", explosionOcclusionEnabled);
        explosionOccludedScale = ReadFloat(iniPath, "AdvancedResponse", "ExplosionOccludedScale", explosionOccludedScale);

        impactSequencingEnabled = ReadBool(iniPath, "AdvancedResponse", "ImpactSequencing", impactSequencingEnabled);
        impactSequenceResetMs = static_cast<unsigned>(Clamp(ReadInt(iniPath, "AdvancedResponse", "ImpactSequenceResetMs", static_cast<int>(impactSequenceResetMs)), 100, 2000));
        impactSecondaryScale = ReadFloat(iniPath, "AdvancedResponse", "ImpactSecondaryScale", impactSecondaryScale);
        impactTertiaryScale = ReadFloat(iniPath, "AdvancedResponse", "ImpactTertiaryScale", impactTertiaryScale);

        locomotionResponseScale = ReadFloat(iniPath, "ResponseScales", "Locomotion", locomotionResponseScale);
        combatResponseScale = ReadFloat(iniPath, "ResponseScales", "Combat", combatResponseScale);
        vehicleResponseScale = ReadFloat(iniPath, "ResponseScales", "Vehicle", vehicleResponseScale);
        environmentResponseScale = ReadFloat(iniPath, "ResponseScales", "Environment", environmentResponseScale);
        highFrequencyScale = ReadFloat(iniPath, "ResponseScales", "HighFrequency", highFrequencyScale);
        frequencyBandsEnabled = ReadBool(iniPath, "FrequencyBands", "Enabled", frequencyBandsEnabled);
        lowFrequencyScale = ReadFloat(iniPath, "FrequencyBands", "Low", lowFrequencyScale);
        midFrequencyScale = ReadFloat(iniPath, "FrequencyBands", "Mid", midFrequencyScale);
        highFrequencyBandScale = ReadFloat(iniPath, "FrequencyBands", "High", highFrequencyBandScale);
        proceduralNoiseEnabled = ReadBool(iniPath, "ProceduralNoise", "Enabled", proceduralNoiseEnabled);
        proceduralNoiseMix = ReadFloat(iniPath, "ProceduralNoise", "Mix", proceduralNoiseMix);

        rotationEnabled = ReadBool(iniPath, "Rotation", "Enabled", rotationEnabled);
        rotationSafetyScale = ReadFloat(iniPath, "Rotation", "SafetyScale", rotationSafetyScale);
        explosionPitchDeg = ReadFloat(iniPath, "Rotation", "ExplosionPitchDeg", explosionPitchDeg);
        explosionRollDeg = ReadFloat(iniPath, "Rotation", "ExplosionRollDeg", explosionRollDeg);
        explosionYawDeg = ReadFloat(iniPath, "Rotation", "ExplosionYawDeg", explosionYawDeg);
        explosionRotationSpring = ReadFloat(iniPath, "Rotation", "ExplosionSpring", explosionRotationSpring);
        explosionRotationDamping = ReadFloat(iniPath, "Rotation", "ExplosionDamping", explosionRotationDamping);

        fovEnabled = ReadBool(iniPath, "FOV", "Enabled", fovEnabled);
        fovPreferWidescreenFix = ReadBool(iniPath, "FOV", "PreferWidescreenFix", fovPreferWidescreenFix);
        fovNativeFallback = ReadBool(iniPath, "FOV", "NativeFallback", fovNativeFallback);
        fovMinMultiplier = ReadFloat(iniPath, "FOV", "MinMultiplier", fovMinMultiplier);
        fovMaxMultiplier = ReadFloat(iniPath, "FOV", "MaxMultiplier", fovMaxMultiplier);
        fovResponse = ReadFloat(iniPath, "FOV", "Response", fovResponse);
        fovReturnResponse = ReadFloat(iniPath, "FOV", "ReturnResponse", fovReturnResponse);
        fovKickDecay = ReadFloat(iniPath, "FOV", "KickDecay", fovKickDecay);
        fovWalk = ReadFloat(iniPath, "FOV", "Walk", fovWalk);
        fovRun = ReadFloat(iniPath, "FOV", "Run", fovRun);
        fovSprint = ReadFloat(iniPath, "FOV", "Sprint", fovSprint);
        fovCrouch = ReadFloat(iniPath, "FOV", "Crouch", fovCrouch);
        fovAim = ReadFloat(iniPath, "FOV", "Aim", fovAim);
        fovAirborne = ReadFloat(iniPath, "FOV", "Airborne", fovAirborne);
        fovVehicleSpeed = ReadFloat(iniPath, "FOV", "VehicleSpeed", fovVehicleSpeed);
        fovVehicleAccel = ReadFloat(iniPath, "FOV", "VehicleAccel", fovVehicleAccel);
        fovVehicleBrake = ReadFloat(iniPath, "FOV", "VehicleBrake", fovVehicleBrake);
        fovVehicleCorner = ReadFloat(iniPath, "FOV", "VehicleCorner", fovVehicleCorner);
        fovRecoilKick = ReadFloat(iniPath, "FOV", "RecoilKick", fovRecoilKick);
        fovMeleeKick = ReadFloat(iniPath, "FOV", "MeleeKick", fovMeleeKick);
        fovDamageKick = ReadFloat(iniPath, "FOV", "DamageKick", fovDamageKick);
        fovExplosionKick = ReadFloat(iniPath, "FOV", "ExplosionKick", fovExplosionKick);
        fovLandingKick = ReadFloat(iniPath, "FOV", "LandingKick", fovLandingKick);
        fovVehicleImpactKick = ReadFloat(iniPath, "FOV", "VehicleImpactKick", fovVehicleImpactKick);
        fovChainsawAttack = ReadFloat(iniPath, "FOV", "ChainsawAttack", fovChainsawAttack);
        fovChainsawSaw = ReadFloat(iniPath, "FOV", "ChainsawSaw", fovChainsawSaw);
        fovContinuousTool = ReadFloat(iniPath, "FOV", "ContinuousTool", fovContinuousTool);

        directionalDamageEnabled = ReadBool(iniPath, "DamageResponse", "Directional", directionalDamageEnabled);
        directionalDamageWeight = ReadFloat(iniPath, "DamageResponse", "DirectionalWeight", directionalDamageWeight);
        incomingMeleeDamageScale = ReadFloat(iniPath, "DamageResponse", "MeleeScale", incomingMeleeDamageScale);
        incomingBulletDamageScale = ReadFloat(iniPath, "DamageResponse", "BulletScale", incomingBulletDamageScale);
        incomingBulletMidPunch = ReadFloat(iniPath, "DamageResponse", "BulletMidPunch", incomingBulletMidPunch);
        incomingBulletSnap = ReadFloat(iniPath, "DamageResponse", "BulletSnap", incomingBulletSnap);
        incomingBulletRecovery = ReadFloat(iniPath, "DamageResponse", "BulletRecovery", incomingBulletRecovery);
        incomingExplosiveDamageScale = ReadFloat(iniPath, "DamageResponse", "ExplosiveScale", incomingExplosiveDamageScale);
        incomingOtherDamageScale = ReadFloat(iniPath, "DamageResponse", "OtherScale", incomingOtherDamageScale);
        incomingDamageMinSeverity = ReadFloat(iniPath, "DamageResponse", "MinSeverity", incomingDamageMinSeverity);
        incomingDamageHighSnap = ReadFloat(iniPath, "DamageResponse", "HighSnap", incomingDamageHighSnap);
        npcBulletHitFallbackEnabled = ReadBool(iniPath, "DamageResponse", "NpcBulletHitFallback", npcBulletHitFallbackEnabled);
        npcBulletHitRadius = ReadFloat(iniPath, "DamageResponse", "NpcBulletHitRadius", npcBulletHitRadius);
        npcBulletHitFallbackScale = ReadFloat(iniPath, "DamageResponse", "NpcBulletHitFallbackScale", npcBulletHitFallbackScale);
        npcBulletHitFallbackCooldownMs = static_cast<unsigned>(
            std::max(0, ReadInt(iniPath, "DamageResponse", "NpcBulletHitCooldownMs", static_cast<int>(npcBulletHitFallbackCooldownMs)))
        );

        cameraOffsetCollisionEnabled = ReadBool(iniPath, "CameraObstruction", "OffsetCollision", cameraOffsetCollisionEnabled);
        cameraOffsetCollisionPadding = ReadFloat(iniPath, "CameraObstruction", "OffsetCollisionPadding", cameraOffsetCollisionPadding);

        detailedMeleeWeapons = ReadBool(iniPath, "MeleeWeapons", "Enabled", detailedMeleeWeapons);
        batSwingScale = ReadFloat(iniPath, "MeleeWeapons", "BatClubScale", batSwingScale);
        knifeSwingScale = ReadFloat(iniPath, "MeleeWeapons", "KnifeScale", knifeSwingScale);
        chainsawVibration = ReadFloat(iniPath, "MeleeWeapons", "ChainsawVibration", chainsawVibration);
        chainsawIdleVibration = ReadFloat(iniPath, "MeleeWeapons", "ChainsawIdleVibration", chainsawIdleVibration);
        chainsawAttackVibration = ReadFloat(iniPath, "MeleeWeapons", "ChainsawAttackVibration", chainsawAttackVibration);
        chainsawSawVibration = ReadFloat(iniPath, "MeleeWeapons", "ChainsawSawVibration", chainsawSawVibration);
        chainsawIdleHz = ReadFloat(iniPath, "MeleeWeapons", "ChainsawIdleHz", chainsawIdleHz);
        chainsawAttackHz = ReadFloat(iniPath, "MeleeWeapons", "ChainsawAttackHz", chainsawAttackHz);
        chainsawSawHz = ReadFloat(iniPath, "MeleeWeapons", "ChainsawSawHz", chainsawSawHz);
        chainsawForwardLoad = ReadFloat(iniPath, "MeleeWeapons", "ChainsawForwardLoad", chainsawForwardLoad);
        chainsawContactHoldMs = ReadFloat(iniPath, "MeleeWeapons", "ChainsawContactHoldMs", chainsawContactHoldMs);
        continuousToolVibration = ReadFloat(iniPath, "MeleeWeapons", "ContinuousToolVibration", continuousToolVibration);
        heavyMeleeScale = ReadFloat(iniPath, "MeleeWeapons", "HeavyScale", heavyMeleeScale);

        contextualRecoilEnabled = ReadBool(iniPath, "RecoilContext", "Enabled", contextualRecoilEnabled);
        recoilCrouchScale = ReadFloat(iniPath, "RecoilContext", "CrouchScale", recoilCrouchScale);
        recoilAimScale = ReadFloat(iniPath, "RecoilContext", "AimScale", recoilAimScale);
        recoilMovingScale = ReadFloat(iniPath, "RecoilContext", "MovingScale", recoilMovingScale);
        recoilDualWieldScale = ReadFloat(iniPath, "RecoilContext", "DualWieldScale", recoilDualWieldScale);

        reloadAnimationResponse = ReadBool(iniPath, "ReloadResponse", "Enabled", reloadAnimationResponse);
        reloadInsertScale = ReadFloat(iniPath, "ReloadResponse", "InsertScale", reloadInsertScale);
        reloadActionScale = ReadFloat(iniPath, "ReloadResponse", "ActionScale", reloadActionScale);

        wheelResponseEnabled = ReadBool(iniPath, "VehicleDynamics", "WheelResponse", wheelResponseEnabled);
        wheelCompressionStrength = ReadFloat(iniPath, "VehicleDynamics", "WheelCompressionStrength", wheelCompressionStrength);
        tractionLossStrength = ReadFloat(iniPath, "VehicleDynamics", "TractionLossStrength", tractionLossStrength);
        tractionRegainStrength = ReadFloat(iniPath, "VehicleDynamics", "TractionRegainStrength", tractionRegainStrength);

        runtimeReloadEnabled = ReadBool(iniPath, "Developer", "RuntimeReload", runtimeReloadEnabled);
        runtimeReloadKey = Clamp(ReadInt(iniPath, "Developer", "ReloadKey", runtimeReloadKey), 1, 255);
        runtimeReloadDebounceMs = static_cast<unsigned>(Clamp(ReadInt(iniPath, "Developer", "ReloadDebounceMs", static_cast<int>(runtimeReloadDebounceMs)), 100, 2000));
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
        vehicleVerticalAccel = ReadFloat(iniPath, "Vehicle", "VerticalWeight", vehicleVerticalAccel);
        vehicleSpeedVibration = ReadFloat(iniPath, "Vehicle", "SpeedVibration", vehicleSpeedVibration);
        vehicleSpeedVibrationRate = ReadFloat(iniPath, "Vehicle", "SpeedVibrationRate", vehicleSpeedVibrationRate);
        vehicleHornEnabled = ReadBool(iniPath, "VehicleHorn", "Enabled", vehicleHornEnabled);
        vehicleHornVibration = ReadFloat(iniPath, "VehicleHorn", "Vibration", vehicleHornVibration);
        vehicleHornVibrationRate = ReadFloat(iniPath, "VehicleHorn", "Rate", vehicleHornVibrationRate);
        vehicleHornAttack = ReadFloat(iniPath, "VehicleHorn", "Attack", vehicleHornAttack);
        vehicleHornRelease = ReadFloat(iniPath, "VehicleHorn", "Release", vehicleHornRelease);
        vehicleImpactThreshold = ReadFloat(iniPath, "Vehicle", "ImpactThreshold", vehicleImpactThreshold);
        vehicleImpactImpulse = ReadFloat(iniPath, "Vehicle", "ImpactImpulse", vehicleImpactImpulse);
        vehicleContactResponseEnabled = ReadBool(iniPath, "VehicleCrash", "ContactResponse", vehicleContactResponseEnabled);
        vehicleContactStrength = ReadFloat(iniPath, "VehicleCrash", "ContactStrength", vehicleContactStrength);
        vehicleContactMinSpeed = ReadFloat(iniPath, "VehicleCrash", "ContactMinSpeed", vehicleContactMinSpeed);
        vehicleContactDeltaVelocityThreshold = ReadFloat(iniPath, "VehicleCrash", "ContactDeltaVelocityThreshold", vehicleContactDeltaVelocityThreshold);
        vehicleContactStrongDeltaVelocity = ReadFloat(iniPath, "VehicleCrash", "ContactStrongDeltaVelocity", vehicleContactStrongDeltaVelocity);
        vehicleContactDamageReference = ReadFloat(iniPath, "VehicleCrash", "ContactDamageReference", vehicleContactDamageReference);
        vehicleContactSpring = ReadFloat(iniPath, "VehicleCrash", "ContactSpring", vehicleContactSpring);
        vehicleContactDamping = ReadFloat(iniPath, "VehicleCrash", "ContactDamping", vehicleContactDamping);
        vehicleContactDirectKick = ReadFloat(iniPath, "VehicleCrash", "ContactDirectKick", vehicleContactDirectKick);
        vehicleContactKickDecay = ReadFloat(iniPath, "VehicleCrash", "ContactKickDecay", vehicleContactKickDecay);
        vehicleContactCooldownMs = static_cast<unsigned>(std::max(25, ReadInt(iniPath, "VehicleCrash", "ContactCooldownMs", static_cast<int>(vehicleContactCooldownMs))));
        vehicleCrashDamageEnabled = ReadBool(iniPath, "VehicleCrash", "DamageResponse", vehicleCrashDamageEnabled);
        vehicleCrashDamageThreshold = ReadFloat(iniPath, "VehicleCrash", "DamageThreshold", vehicleCrashDamageThreshold);
        vehicleCrashDamageReference = ReadFloat(iniPath, "VehicleCrash", "DamageReference", vehicleCrashDamageReference);
        vehicleCrashDamageStrength = ReadFloat(iniPath, "VehicleCrash", "DamageStrength", vehicleCrashDamageStrength);
        vehicleCrashFrontMultiplier = ReadFloat(iniPath, "VehicleCrash", "FrontMultiplier", vehicleCrashFrontMultiplier);
        vehicleCrashRearMultiplier = ReadFloat(iniPath, "VehicleCrash", "RearMultiplier", vehicleCrashRearMultiplier);
        vehicleCrashSideMultiplier = ReadFloat(iniPath, "VehicleCrash", "SideMultiplier", vehicleCrashSideMultiplier);
        vehicleCrashVerticalMultiplier = ReadFloat(iniPath, "VehicleCrash", "VerticalMultiplier", vehicleCrashVerticalMultiplier);
        vehicleCrashCatastrophicThreshold = ReadFloat(iniPath, "VehicleCrash", "CatastrophicThreshold", vehicleCrashCatastrophicThreshold);
        vehicleCrashCatastrophicMultiplier = ReadFloat(iniPath, "VehicleCrash", "CatastrophicMultiplier", vehicleCrashCatastrophicMultiplier);
        vehicleCrashPhysicsWindowMs = static_cast<unsigned>(std::max(0, ReadInt(iniPath, "VehicleCrash", "PhysicsWindowMs", static_cast<int>(vehicleCrashPhysicsWindowMs))));
        vehicleSuspensionEnabled = ReadBool(iniPath, "VehicleCrash", "SuspensionResponse", vehicleSuspensionEnabled);
        vehicleSuspensionThreshold = ReadFloat(iniPath, "VehicleCrash", "SuspensionThreshold", vehicleSuspensionThreshold);
        vehicleSuspensionStrength = ReadFloat(iniPath, "VehicleCrash", "SuspensionStrength", vehicleSuspensionStrength);
        vehicleSuspensionAdaptiveHandling = ReadBool(iniPath, "VehicleCrash", "AdaptiveHandling", vehicleSuspensionAdaptiveHandling);
        vehicleSuspensionHandlingInfluence = ReadFloat(iniPath, "VehicleCrash", "HandlingInfluence", vehicleSuspensionHandlingInfluence);
        vehicleSuspensionHandlingInfluence = Clamp(vehicleSuspensionHandlingInfluence, 0.0f, 1.5f);
        vehicleSpring = ReadFloat(iniPath, "Vehicle", "Spring", vehicleSpring);
        vehicleDamping = ReadFloat(iniPath, "Vehicle", "Damping", vehicleDamping);
        vehicleMaxSide = ReadFloat(iniPath, "Vehicle", "MaxSide", vehicleMaxSide);
        vehicleMaxVertical = ReadFloat(iniPath, "Vehicle", "MaxVertical", vehicleMaxVertical);
        vehicleMaxForward = ReadFloat(iniPath, "Vehicle", "MaxForward", vehicleMaxForward);
        vehicleAutomobileScale = ReadFloat(iniPath, "VehicleProfiles", "Automobile", vehicleAutomobileScale);
        vehicleBikeScale = ReadFloat(iniPath, "VehicleProfiles", "Bike", vehicleBikeScale);
        vehicleBoatScale = ReadFloat(iniPath, "VehicleProfiles", "Boat", vehicleBoatScale);
        vehicleHeliScale = ReadFloat(iniPath, "VehicleProfiles", "Helicopter", vehicleHeliScale);
        vehiclePlaneScale = ReadFloat(iniPath, "VehicleProfiles", "Plane", vehiclePlaneScale);
        vehicleOtherScale = ReadFloat(iniPath, "VehicleProfiles", "Other", vehicleOtherScale);

        idleSide = ReadFloat(iniPath, "Alive", "IdleSide", idleSide);
        idleVertical = ReadFloat(iniPath, "Alive", "IdleVertical", idleVertical);
        idleForward = ReadFloat(iniPath, "Alive", "IdleForward", idleForward);
        idleSettleDelay = ReadFloat(iniPath, "Alive", "SettleDelay", idleSettleDelay);
        idleSettledScale = ReadFloat(iniPath, "Alive", "SettledScale", idleSettledScale);
        aimBreathingSide = ReadFloat(iniPath, "Alive", "AimBreathingSide", aimBreathingSide);
        aimBreathingVertical = ReadFloat(iniPath, "Alive", "AimBreathingVertical", aimBreathingVertical);
        aimBreathingForward = ReadFloat(iniPath, "Alive", "AimBreathingForward", aimBreathingForward);
        aimBreathingRate = ReadFloat(iniPath, "Alive", "AimBreathingRate", aimBreathingRate);

        gaitSide = ReadFloat(iniPath, "Gait", "Side", gaitSide);
        gaitVertical = ReadFloat(iniPath, "Gait", "Vertical", gaitVertical);
        gaitForward = ReadFloat(iniPath, "Gait", "Forward", gaitForward);
        fallbackGait = ReadFloat(iniPath, "Gait", "Fallback", fallbackGait);
        stepImpulse = ReadFloat(iniPath, "Gait", "StepImpulse", stepImpulse);
        surfaceRoad = ReadFloat(iniPath, "SurfaceSteps", "Road", surfaceRoad);
        surfaceConcrete = ReadFloat(iniPath, "SurfaceSteps", "Concrete", surfaceConcrete);
        surfaceGravel = ReadFloat(iniPath, "SurfaceSteps", "Gravel", surfaceGravel);
        surfaceGrass = ReadFloat(iniPath, "SurfaceSteps", "Grass", surfaceGrass);
        surfaceDirt = ReadFloat(iniPath, "SurfaceSteps", "Dirt", surfaceDirt);
        surfaceSand = ReadFloat(iniPath, "SurfaceSteps", "Sand", surfaceSand);
        surfaceWood = ReadFloat(iniPath, "SurfaceSteps", "Wood", surfaceWood);
        surfaceMetal = ReadFloat(iniPath, "SurfaceSteps", "Metal", surfaceMetal);
        surfaceRock = ReadFloat(iniPath, "SurfaceSteps", "Rock", surfaceRock);
        surfaceMisc = ReadFloat(iniPath, "SurfaceSteps", "Misc", surfaceMisc);

        lookYaw = ReadFloat(iniPath, "Look", "YawInertia", lookYaw);
        lookPitch = ReadFloat(iniPath, "Look", "PitchInertia", lookPitch);
        turnInput = ReadFloat(iniPath, "Look", "TurnInput", turnInput);
        accelInertia = ReadFloat(
            iniPath, "Look", "AccelerationInertia", accelInertia
        );
        directionalInertia = ReadFloat(iniPath, "Movement", "DirectionalInertia", directionalInertia);
        strafeSway = ReadFloat(iniPath, "Movement", "StrafeSway", strafeSway);
        backwardSway = ReadFloat(iniPath, "Movement", "BackwardSway", backwardSway);
        jumpImpulse = ReadFloat(iniPath, "Impact", "JumpImpulse", jumpImpulse);
        landingImpulse = ReadFloat(iniPath, "Impact", "LandingImpulse", landingImpulse);
        hardLandingScale = ReadFloat(iniPath, "Impact", "HardLandingScale", hardLandingScale);
        landingSurfaceInfluence = ReadFloat(iniPath, "Impact", "LandingSurfaceInfluence", landingSurfaceInfluence);
        landingSoftMinScale = ReadFloat(iniPath, "Impact", "LandingSoftMinScale", landingSoftMinScale);
        landingHardMaxScale = ReadFloat(iniPath, "Impact", "LandingHardMaxScale", landingHardMaxScale);
        damageImpulse = ReadFloat(iniPath, "Impact", "DamageImpulse", damageImpulse);
        damageSideImpulse = ReadFloat(iniPath, "Impact", "DamageSideImpulse", damageSideImpulse);
        explosionImpulse = ReadFloat(iniPath, "Impact", "ExplosionImpulse", explosionImpulse);
        explosionRadius = ReadFloat(iniPath, "Impact", "ExplosionRadius", explosionRadius);
        maxImpactOffset = ReadFloat(iniPath, "Impact", "MaxImpactOffset", maxImpactOffset);
        maxFinalSide = ReadFloat(iniPath, "Impact", "MaxFinalSide", maxFinalSide);
        maxFinalVertical = ReadFloat(iniPath, "Impact", "MaxFinalVertical", maxFinalVertical);
        maxFinalForward = ReadFloat(iniPath, "Impact", "MaxFinalForward", maxFinalForward);
        meleeImpact = ReadFloat(iniPath, "Impact", "MeleeImpact", meleeImpact);
        meleeSide = ReadFloat(iniPath, "Impact", "MeleeSide", meleeSide);

        detailedPlayerMelee = ReadBool(iniPath, "Melee", "DetailedPlayerResponse", detailedPlayerMelee);
        punchWindup = ReadFloat(iniPath, "Melee", "PunchWindup", punchWindup);
        punchRelease = ReadFloat(iniPath, "Melee", "PunchRelease", punchRelease);
        punchContact = ReadFloat(iniPath, "Melee", "PunchContact", punchContact);
        punchSide = ReadFloat(iniPath, "Melee", "PunchSide", punchSide);
        punchVertical = ReadFloat(iniPath, "Melee", "PunchVertical", punchVertical);
        punchReleasePhase = ReadFloat(iniPath, "Melee", "PunchReleasePhase", punchReleasePhase);
        kickWindup = ReadFloat(iniPath, "Melee", "KickWindup", kickWindup);
        kickRelease = ReadFloat(iniPath, "Melee", "KickRelease", kickRelease);
        kickContact = ReadFloat(iniPath, "Melee", "KickContact", kickContact);
        kickSide = ReadFloat(iniPath, "Melee", "KickSide", kickSide);
        kickVertical = ReadFloat(iniPath, "Melee", "KickVertical", kickVertical);
        kickReleasePhase = ReadFloat(iniPath, "Melee", "KickReleasePhase", kickReleasePhase);
        meleeComboStep = ReadFloat(iniPath, "Melee", "ComboStep", meleeComboStep);
        meleeComboMaxScale = ReadFloat(iniPath, "Melee", "ComboMaxScale", meleeComboMaxScale);

        idlePitchDeg = ReadFloat(
            iniPath, "Rotation", "IdlePitchDeg", idlePitchDeg
        );
        idleRollDeg = ReadFloat(
            iniPath, "Rotation", "IdleRollDeg", idleRollDeg
        );
        gaitPitchDeg = ReadFloat(
            iniPath, "Rotation", "GaitPitchDeg", gaitPitchDeg
        );
        gaitRollDeg = ReadFloat(
            iniPath, "Rotation", "GaitRollDeg", gaitRollDeg
        );
        turnRollDeg = ReadFloat(
            iniPath, "Rotation", "TurnRollDeg", turnRollDeg
        );
        lookYawDeg = ReadFloat(
            iniPath, "Rotation", "LookYawDeg", lookYawDeg
        );
        lookPitchDeg = ReadFloat(
            iniPath, "Rotation", "LookPitchDeg", lookPitchDeg
        );
        maxPitchDeg = ReadFloat(
            iniPath, "Rotation", "MaxPitchDeg", maxPitchDeg
        );
        maxRollDeg = ReadFloat(
            iniPath, "Rotation", "MaxRollDeg", maxRollDeg
        );
        maxYawDeg = ReadFloat(
            iniPath, "Rotation", "MaxYawDeg", maxYawDeg
        );

        recoilEnabled = ReadBool(
            iniPath, "Recoil", "Enabled", recoilEnabled
        );
        recoilStrength = ReadFloat(
            iniPath, "Recoil", "Strength", recoilStrength
        );
        recoilVerticalKick = ReadFloat(
            iniPath, "Recoil", "VerticalKick", recoilVerticalKick
        );
        recoilForwardKick = ReadFloat(
            iniPath, "Recoil", "ForwardKick", recoilForwardKick
        );
        recoilSideKick = ReadFloat(
            iniPath, "Recoil", "SideKick", recoilSideKick
        );
        recoilPositionVertical = ReadFloat(
            iniPath, "Recoil", "PositionVertical", recoilPositionVertical
        );
        recoilPositionForward = ReadFloat(
            iniPath, "Recoil", "PositionForward", recoilPositionForward
        );
        recoilPositionSide = ReadFloat(
            iniPath, "Recoil", "PositionSide", recoilPositionSide
        );
        recoilVelocityVertical = ReadFloat(
            iniPath, "Recoil", "VelocityVertical", recoilVelocityVertical
        );
        recoilVelocityForward = ReadFloat(
            iniPath, "Recoil", "VelocityForward", recoilVelocityForward
        );
        recoilVelocitySide = ReadFloat(
            iniPath, "Recoil", "VelocitySide", recoilVelocitySide
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
        firstShotMultiplier = ReadFloat(iniPath, "Recoil", "FirstShotMultiplier", firstShotMultiplier);
        sustainedShotMultiplier = ReadFloat(iniPath, "Recoil", "SustainedShotMultiplier", sustainedShotMultiplier);
        sustainedResetMs = static_cast<unsigned>(Clamp(
            ReadInt(iniPath, "Recoil", "SustainedResetMs", static_cast<int>(sustainedResetMs)), 50, 1500));

        for (int id = 0; id < 47; ++id) {
            weaponScale[id] = -1.0f;
            weaponVertical[id] = 1.0f;
            weaponForward[id] = 1.0f;
            weaponSide[id] = 1.0f;
            weaponRecovery[id] = 1.0f;
        }
        static const char* recoilNames[17] = {
            "Colt45", "Silenced", "DesertEagle", "Shotgun", "Sawnoff",
            "Spas12", "MicroUzi", "MP5", "AK47", "M4", "Tec9",
            "CountryRifle", "SniperRifle", "RocketLauncher", "HeatSeeker",
            "Flamethrower", "Minigun"
        };
        for (int id = 22; id <= 38; ++id) {
            const char* name = recoilNames[id - 22];
            char key[64] = {};
            std::snprintf(key, sizeof(key), "%s.Scale", name);
            weaponScale[id] = ReadFloat(iniPath, "WeaponRecoilProfiles", key, weaponScale[id]);
            std::snprintf(key, sizeof(key), "%s.Vertical", name);
            weaponVertical[id] = ReadFloat(iniPath, "WeaponRecoilProfiles", key, 1.0f);
            std::snprintf(key, sizeof(key), "%s.Forward", name);
            weaponForward[id] = ReadFloat(iniPath, "WeaponRecoilProfiles", key, 1.0f);
            std::snprintf(key, sizeof(key), "%s.Side", name);
            weaponSide[id] = ReadFloat(iniPath, "WeaponRecoilProfiles", key, 1.0f);
            std::snprintf(key, sizeof(key), "%s.Recovery", name);
            weaponRecovery[id] = ReadFloat(iniPath, "WeaponRecoilProfiles", key, 1.0f);
        }

        static const char* meleeNames[16] = {
            "Unarmed","BrassKnuckle","GolfClub","Nightstick","Knife","BaseballBat",
            "Shovel","PoolCue","Katana","Chainsaw","Dildo1","Dildo2","Vibrator1",
            "Vibrator2","Flowers","Cane"
        };
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

            char key[80]={};
            const char* name=meleeNames[id];
            std::snprintf(key,sizeof(key),"%s.Scale",name);
            meleeWeaponScale[id]=ReadFloat(iniPath,"MeleeWeaponProfiles",key,meleeWeaponScale[id]);
            std::snprintf(key,sizeof(key),"%s.SideWeight",name);
            meleeWeaponSideWeight[id]=ReadFloat(iniPath,"MeleeWeaponProfiles",key,meleeWeaponSideWeight[id]);
            std::snprintf(key,sizeof(key),"%s.VerticalWeight",name);
            meleeWeaponVerticalWeight[id]=ReadFloat(iniPath,"MeleeWeaponProfiles",key,meleeWeaponVerticalWeight[id]);
            std::snprintf(key,sizeof(key),"%s.ReleaseForward",name);
            meleeWeaponReleaseForward[id]=ReadFloat(iniPath,"MeleeWeaponProfiles",key,meleeWeaponReleaseForward[id]);
            std::snprintf(key,sizeof(key),"%s.ReleaseSide",name);
            meleeWeaponReleaseSide[id]=ReadFloat(iniPath,"MeleeWeaponProfiles",key,meleeWeaponReleaseSide[id]);
            std::snprintf(key,sizeof(key),"%s.ContactScale",name);
            meleeWeaponContactScale[id]=ReadFloat(iniPath,"MeleeWeaponProfiles",key,meleeWeaponContactScale[id]);
            std::snprintf(key,sizeof(key),"%s.LockOnScale",name);
            meleeWeaponLockOnScale[id]=ReadFloat(iniPath,"MeleeWeaponProfiles",key,meleeWeaponLockOnScale[id]);
            std::snprintf(key,sizeof(key),"%s.CooldownMs",name);
            meleeWeaponCooldownMs[id]=static_cast<unsigned>(Clamp(ReadInt(iniPath,"MeleeWeaponProfiles",key,static_cast<int>(meleeWeaponCooldownMs[id])),60,600));
        }

        recoilMinIntervalMs = static_cast<unsigned>(Clamp(
            ReadInt(
                iniPath,
                "Recoil",
                "MinIntervalMs",
                static_cast<int>(recoilMinIntervalMs)
            ),
            0,
            250
        ));

        spring = ReadFloat(iniPath, "Smoothing", "Spring", spring);
        damping = ReadFloat(iniPath, "Smoothing", "Damping", damping);
        lookSpring = ReadFloat(
            iniPath, "Smoothing", "LookSpring", lookSpring
        );
        lookDamping = ReadFloat(
            iniPath, "Smoothing", "LookDamping", lookDamping
        );
        stateResponse = ReadFloat(
            iniPath, "Smoothing", "StateResponse", stateResponse
        );
        footPhaseResponse = ReadFloat(
            iniPath, "Smoothing", "FootPhaseResponse", footPhaseResponse
        );
        recoilSpringMultiplier = ReadFloat(
            iniPath, "Smoothing", "RecoilSpringMultiplier", recoilSpringMultiplier
        );
        recoilDampingMultiplier = ReadFloat(
            iniPath, "Smoothing", "RecoilDampingMultiplier", recoilDampingMultiplier
        );
        idleDampingMultiplier = ReadFloat(
            iniPath, "Smoothing", "IdleDampingMultiplier", idleDampingMultiplier
        );

        debug = ReadBool(iniPath, "Debug", "Enabled", debug);
        logEveryMs = static_cast<unsigned>(Clamp(
            ReadInt(iniPath, "Debug", "LogEveryMs", int(logEveryMs)),
            100,
            10000
        ));
logSteps = ReadBool(
            iniPath, "Debug", "LogStepEvents", logSteps
        );
        logShots = ReadBool(
            iniPath, "Debug", "LogShotEvents", logShots
        );
        overlayEnabled = ReadBool(iniPath, "Debug", "OverlayEnabled", overlayEnabled);
        auditEnabled = ReadBool(iniPath, "DebugAudit", "Enabled", auditEnabled);
        auditConfigValues = ReadBool(iniPath, "DebugAudit", "LogConfigValues", auditConfigValues);
        auditFunctionCalls = ReadBool(iniPath, "DebugAudit", "LogFunctionCalls", auditFunctionCalls);
        auditEveryMs = static_cast<unsigned>(Clamp(
            ReadInt(iniPath, "DebugAudit", "SummaryEveryMs", static_cast<int>(auditEveryMs)),
            1000,
            60000
        ));
        nearbyVehicleScanMs = static_cast<unsigned>(Clamp(ReadInt(iniPath, "Performance", "NearbyVehicleScanMs", static_cast<int>(nearbyVehicleScanMs)), 16, 250));
        surfaceScanMs = static_cast<unsigned>(Clamp(ReadInt(iniPath, "Performance", "SurfaceScanMs", static_cast<int>(surfaceScanMs)), 25, 500));
        scanProfilingEnabled = ReadBool(iniPath, "Performance", "ScanProfiling", scanProfilingEnabled);
        scanProfileEveryMs = static_cast<unsigned>(Clamp(
            ReadInt(iniPath, "Performance", "ProfileEveryMs", static_cast<int>(scanProfileEveryMs)),
            1000, 60000));

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
        log.Write("INI AUDIT BEGIN effective-values");
        log.Write("INI EFFECTIVE [General] Enabled=%d", enabled ? 1 : 0);
        log.Write("INI EFFECTIVE [General] Strength=%.6f", strength);
        log.Write("INI EFFECTIVE [Movement] Idle=%.6f", idleScale);
        log.Write("INI EFFECTIVE [Movement] Walk=%.6f", walkScale);
        log.Write("INI EFFECTIVE [Movement] Run=%.6f", runScale);
        log.Write("INI EFFECTIVE [Movement] Sprint=%.6f", sprintScale);
        log.Write("INI EFFECTIVE [Movement] Aim=%.6f", aimScale);
        log.Write("INI EFFECTIVE [Movement] Crouch=%.6f", crouchScale);
        log.Write("INI EFFECTIVE [Movement] Airborne=%.6f", airborneScale);
        log.Write("INI EFFECTIVE [Movement] Deadzone=%.6f", movementDeadzone);
        log.Write("INI EFFECTIVE [General] LocomotionEnabled=%d", locomotionEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [General] FootstepsEnabled=%d", footstepsEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [General] InertiaEnabled=%d", inertiaEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [General] JumpLandEnabled=%d", jumpLandEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [General] DamageEnabled=%d", damageEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [General] ExplosionsEnabled=%d", explosionsEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [NearbyGunfire] Enabled=%d", nearbyGunfireEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [NearbyGunfire] AffectInVehicle=%d", nearbyGunfireInVehicle ? 1 : 0);
        log.Write("INI EFFECTIVE [NearbyGunfire] Strength=%.6f", nearbyGunfireStrength);
        log.Write("INI EFFECTIVE [NearbyGunfire] MinDistance=%.6f", nearbyGunfireMinDistance);
        log.Write("INI EFFECTIVE [NearbyGunfire] MaxDistance=%.6f", nearbyGunfireMaxDistance);
        log.Write("INI EFFECTIVE [NearbyGunfire] FalloffPower=%.6f", nearbyGunfireFalloffPower);
        log.Write("INI EFFECTIVE [NearbyGunfire] Side=%.6f", nearbyGunfireSide);
        log.Write("INI EFFECTIVE [NearbyGunfire] Vertical=%.6f", nearbyGunfireVertical);
        log.Write("INI EFFECTIVE [NearbyGunfire] Forward=%.6f", nearbyGunfireForward);
        log.Write("INI EFFECTIVE [NearbyVehicles] Enabled=%d", nearbyVehiclesEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [NearbyVehicles] AffectOnFoot=%d", nearbyVehiclesOnFoot ? 1 : 0);
        log.Write("INI EFFECTIVE [NearbyVehicles] AffectInVehicle=%d", nearbyVehiclesInVehicle ? 1 : 0);
        log.Write("INI EFFECTIVE [NearbyVehicles] Strength=%.6f", nearbyVehiclesStrength);
        log.Write("INI EFFECTIVE [NearbyVehicles] MinDistance=%.6f", nearbyVehiclesMinDistance);
        log.Write("INI EFFECTIVE [NearbyVehicles] MaxDistance=%.6f", nearbyVehiclesMaxDistance);
        log.Write("INI EFFECTIVE [NearbyVehicles] FalloffPower=%.6f", nearbyVehiclesFalloffPower);
        log.Write("INI EFFECTIVE [NearbyVehicles] IdleScale=%.6f", nearbyVehiclesIdleScale);
        log.Write("INI EFFECTIVE [NearbyVehicles] LowSpeedScale=%.6f", nearbyVehiclesLowSpeedScale);
        log.Write("INI EFFECTIVE [NearbyVehicles] HighSpeedScale=%.6f", nearbyVehiclesHighSpeedScale);
        log.Write("INI EFFECTIVE [NearbyVehicles] AccelerationScale=%.6f", nearbyVehiclesAccelerationScale);
        log.Write("INI EFFECTIVE [NearbyVehicles] Vertical=%.6f", nearbyVehiclesVertical);
        log.Write("INI EFFECTIVE [NearbyVehicles] Side=%.6f", nearbyVehiclesSide);
        log.Write("INI EFFECTIVE [NearbyVehicles] Forward=%.6f", nearbyVehiclesForward);
        log.Write("INI EFFECTIVE [NearbyVehicles] MaxCombined=%.6f", nearbyVehiclesMaxCombined);
        log.Write("INI EFFECTIVE [NearbyVehicles] MaxContributors=%lu", static_cast<unsigned long>(nearbyVehiclesMaxContributors));
        log.Write("INI EFFECTIVE [DoorResponse] Enabled=%d", doorResponseEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [DoorResponse] VehicleDoors=%d", vehicleDoorResponseEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [DoorResponse] BuildingDoors=%d", buildingDoorResponseEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [DoorResponse] AffectInVehicle=%d", doorResponseInVehicle ? 1 : 0);
        log.Write("INI EFFECTIVE [DoorResponse] Strength=%.6f", doorResponseStrength);
        log.Write("INI EFFECTIVE [DoorResponse] VehicleStrength=%.6f", vehicleDoorStrength);
        log.Write("INI EFFECTIVE [DoorResponse] BuildingStrength=%.6f", buildingDoorStrength);
        log.Write("INI EFFECTIVE [DoorResponse] MinDistance=%.6f", doorResponseMinDistance);
        log.Write("INI EFFECTIVE [DoorResponse] MaxDistance=%.6f", doorResponseMaxDistance);
        log.Write("INI EFFECTIVE [DoorResponse] FalloffPower=%.6f", doorResponseFalloffPower);
        log.Write("INI EFFECTIVE [DoorResponse] Side=%.6f", doorResponseSide);
        log.Write("INI EFFECTIVE [DoorResponse] Vertical=%.6f", doorResponseVertical);
        log.Write("INI EFFECTIVE [DoorResponse] Forward=%.6f", doorResponseForward);
        log.Write("INI EFFECTIVE [DoorResponse] OwnVehicleScale=%.6f", doorVehicleOwnScale);
        log.Write("INI EFFECTIVE [DoorResponse] VehicleCloseThreshold=%.6f", vehicleDoorCloseThreshold);
        log.Write("INI EFFECTIVE [DoorResponse] VehicleWasOpenThreshold=%.6f", vehicleDoorWasOpenThreshold);
        log.Write("INI EFFECTIVE [DoorResponse] ScanMs=%lu", static_cast<unsigned long>(doorScanMs));
        log.Write("INI EFFECTIVE [DoorResponse] BonnetScale=%.6f", doorBonnetScale);
        log.Write("INI EFFECTIVE [DoorResponse] BootScale=%.6f", doorBootScale);
        log.Write("INI EFFECTIVE [ExtendedVehicle] Enabled=%d", vehicleMechanicalEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [ExtendedVehicle] EnterStrength=%.6f", vehicleEnterStrength);
        log.Write("INI EFFECTIVE [ExtendedVehicle] ExitStrength=%.6f", vehicleExitStrength);
        log.Write("INI EFFECTIVE [ExtendedVehicle] BailOutEnabled=%d", vehicleBailOutEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [ExtendedVehicle] BailOutMinSpeed=%.6f", vehicleBailOutMinSpeed);
        log.Write("INI EFFECTIVE [ExtendedVehicle] BailOutStrength=%.6f", vehicleBailOutStrength);
        log.Write("INI EFFECTIVE [ExtendedVehicle] BailAirborneWindowMs=%lu", static_cast<unsigned long>(vehicleBailAirborneWindowMs));
        log.Write("INI EFFECTIVE [ExtendedVehicle] BailLandingScale=%.6f", vehicleBailLandingScale);
        log.Write("INI EFFECTIVE [ExtendedVehicle] BailFOVKick=%.6f", vehicleBailFovKick);
        log.Write("INI EFFECTIVE [ExtendedVehicle] EngineStartStrength=%.6f", engineStartStrength);
        log.Write("INI EFFECTIVE [ExtendedVehicle] EngineStopStrength=%.6f", engineStopStrength);
        log.Write("INI EFFECTIVE [ExtendedVehicle] GearShiftStrength=%.6f", gearShiftStrength);
        log.Write("INI EFFECTIVE [ExtendedVehicle] TyreBlowoutStrength=%.6f", tyreBlowoutStrength);
        log.Write("INI EFFECTIVE [ExtendedVehicle] AxleLandingStrength=%.6f", axleLandingStrength);
        log.Write("INI EFFECTIVE [WorldResponse] WaterResponse=%d", waterResponseEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [WorldResponse] WaterEntryStrength=%.6f", waterEntryStrength);
        log.Write("INI EFFECTIVE [WorldResponse] LargeVehicles=%d", largeVehicleResponseEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [WorldResponse] TrainStrength=%.6f", trainResponseStrength);
        log.Write("INI EFFECTIVE [WorldResponse] TrainDistance=%.6f", trainResponseDistance);
        log.Write("INI EFFECTIVE [WorldResponse] AircraftStrength=%.6f", aircraftResponseStrength);
        log.Write("INI EFFECTIVE [WorldResponse] AircraftDistance=%.6f", aircraftResponseDistance);
        log.Write("INI EFFECTIVE [WorldResponse] LargeVehicleScanMs=%lu", static_cast<unsigned long>(largeVehicleScanMs));
        log.Write("INI EFFECTIVE [WorldResponse] ObjectImpacts=%d", worldObjectResponseEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [WorldResponse] ObjectImpactStrength=%.6f", worldObjectImpactStrength);
        log.Write("INI EFFECTIVE [WorldResponse] ObjectImpactDistance=%.6f", worldObjectImpactDistance);
        log.Write("INI EFFECTIVE [WorldResponse] ObjectScanMs=%lu", static_cast<unsigned long>(worldObjectScanMs));
        log.Write("INI EFFECTIVE [WorldResponse] PedImpacts=%d", pedWorldResponseEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [WorldResponse] PedImpactStrength=%.6f", pedImpactStrength);
        log.Write("INI EFFECTIVE [WorldResponse] HeavyFootstepStrength=%.6f", heavyFootstepStrength);
        log.Write("INI EFFECTIVE [WorldResponse] HeavyFootstepDistance=%.6f", heavyFootstepDistance);
        log.Write("INI EFFECTIVE [WorldResponse] PedScanMs=%lu", static_cast<unsigned long>(pedWorldScanMs));
        log.Write("INI EFFECTIVE [EnvironmentResponse] Enabled=%d", weatherResponseEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [EnvironmentResponse] WindStrength=%.6f", windResponseStrength);
        log.Write("INI EFFECTIVE [EnvironmentResponse] RainStrength=%.6f", rainResponseStrength);
        log.Write("INI EFFECTIVE [EnvironmentResponse] SandstormStrength=%.6f", sandstormResponseStrength);
        log.Write("INI EFFECTIVE [EnvironmentResponse] EarthquakeStrength=%.6f", earthquakeResponseStrength);
        log.Write("INI EFFECTIVE [CameraObstruction] Enabled=%d", obstructionResponseEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [CameraObstruction] Threshold=%.6f", obstructionThreshold);
        log.Write("INI EFFECTIVE [CameraObstruction] Strength=%.6f", obstructionStrength);
        log.Write("INI EFFECTIVE [CameraObstruction] Spring=%.6f", obstructionSpring);
        log.Write("INI EFFECTIVE [CameraObstruction] Damping=%.6f", obstructionDamping);
        log.Write("INI EFFECTIVE [Performance] EnvironmentPriorityMix=%d", environmentPriorityMixEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [Performance] EnvironmentMixMaxImpulse=%.6f", environmentMixMaxImpulse);
        log.Write("INI EFFECTIVE [ExtendedVehicle] HandlingPersonality=%d", vehiclePersonalityEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [ExtendedVehicle] CollisionMaterialInfluence=%.6f", collisionMaterialInfluence);
        log.Write("INI EFFECTIVE [AdvancedResponse] Enabled=%d", advancedResponseEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [AdvancedResponse] ReloadStrength=%.6f", reloadResponseStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] EquipStrength=%.6f", equipResponseStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] NearMiss=%d", nearMissEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [AdvancedResponse] NearMissStrength=%.6f", nearMissStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] NearMissDistance=%.6f", nearMissDistance);
        log.Write("INI EFFECTIVE [AdvancedResponse] NearMissMaxShooterDistance=%.6f", nearMissMaxShooterDistance);
        log.Write("INI EFFECTIVE [AdvancedResponse] SwimStrength=%.6f", swimResponseStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] ClimbStrength=%.6f", climbResponseStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] ParachuteDeployStrength=%.6f", parachuteDeployStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] ParachuteAirStrength=%.6f", parachuteAirStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] ParachuteLandStrength=%.6f", parachuteLandStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] JetpackStrength=%.6f", jetpackStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] JetpackTransitionStrength=%.6f", jetpackTransitionStrength);
        log.Write("INI EFFECTIVE [JetpackResponse] RecoilScale=%.6f", jetpackRecoilScale);
        log.Write("INI EFFECTIVE [MountedWeapons] Enabled=%d", mountedWeaponResponseEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [MountedWeapons] GunStrength=%.6f", mountedGunStrength);
        log.Write("INI EFFECTIVE [MountedWeapons] HeavyStrength=%.6f", mountedHeavyStrength);
        log.Write("INI EFFECTIVE [MountedWeapons] WaterStrength=%.6f", mountedWaterStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] BikeLeanStrength=%.6f", bikeLeanStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] BikePitchStrength=%.6f", bikePitchStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] BicyclePedalStrength=%.6f", bicyclePedalStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] BoatWaveStrength=%.6f", boatWaveStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] AircraftTurbulenceStrength=%.6f", aircraftTurbulenceStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] AircraftLandingStrength=%.6f", aircraftLandingStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] TrainJointStrength=%.6f", trainJointStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] SkidStrength=%.6f", skidStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] EngineVibrationStrength=%.6f", engineVibrationStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] RoadTextureStrength=%.6f", roadTextureStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] DoorOpenStrength=%.6f", doorOpenStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] MachineryStrength=%.6f", machineryStrength);
        log.Write("INI EFFECTIVE [AdvancedResponse] MachineryDistance=%.6f", machineryDistance);
        log.Write("INI EFFECTIVE [AdvancedResponse] NativeShakeBlend=%.6f", nativeShakeBlend);
        log.Write("INI EFFECTIVE [AdvancedResponse] ExplosionShockDelay=%d", explosionShockDelayEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [AdvancedResponse] ExplosionShockSpeed=%.6f", explosionShockSpeed);
        log.Write("INI EFFECTIVE [AdvancedResponse] ExplosionShockSecondaryScale=%.6f", explosionShockSecondaryScale);
        log.Write("INI EFFECTIVE [AdvancedResponse] ExplosionOcclusion=%d", explosionOcclusionEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [AdvancedResponse] ExplosionOccludedScale=%.6f", explosionOccludedScale);
        log.Write("INI EFFECTIVE [AdvancedResponse] ImpactSequencing=%d", impactSequencingEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [AdvancedResponse] ImpactSequenceResetMs=%lu", static_cast<unsigned long>(impactSequenceResetMs));
        log.Write("INI EFFECTIVE [AdvancedResponse] ImpactSecondaryScale=%.6f", impactSecondaryScale);
        log.Write("INI EFFECTIVE [AdvancedResponse] ImpactTertiaryScale=%.6f", impactTertiaryScale);
        log.Write("INI EFFECTIVE [ResponseScales] Locomotion=%.6f", locomotionResponseScale);
        log.Write("INI EFFECTIVE [ResponseScales] Combat=%.6f", combatResponseScale);
        log.Write("INI EFFECTIVE [ResponseScales] Vehicle=%.6f", vehicleResponseScale);
        log.Write("INI EFFECTIVE [ResponseScales] Environment=%.6f", environmentResponseScale);
        log.Write("INI EFFECTIVE [ResponseScales] HighFrequency=%.6f", highFrequencyScale);
        log.Write("INI EFFECTIVE [FrequencyBands] Enabled=%d", frequencyBandsEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [FrequencyBands] Low=%.6f", lowFrequencyScale);
        log.Write("INI EFFECTIVE [FrequencyBands] Mid=%.6f", midFrequencyScale);
        log.Write("INI EFFECTIVE [FrequencyBands] High=%.6f", highFrequencyBandScale);
        log.Write("INI EFFECTIVE [ProceduralNoise] Enabled=%d", proceduralNoiseEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [ProceduralNoise] Mix=%.6f", proceduralNoiseMix);
        log.Write("INI EFFECTIVE [Rotation] Enabled=%d", rotationEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [Rotation] SafetyScale=%.6f", rotationSafetyScale);
        log.Write("INI EFFECTIVE [DamageResponse] Directional=%d", directionalDamageEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [DamageResponse] DirectionalWeight=%.6f", directionalDamageWeight);
        log.Write("INI EFFECTIVE [DamageResponse] MeleeScale=%.6f", incomingMeleeDamageScale);
        log.Write("INI EFFECTIVE [DamageResponse] BulletScale=%.6f", incomingBulletDamageScale);
        log.Write("INI EFFECTIVE [DamageResponse] BulletMidPunch=%.6f", incomingBulletMidPunch);
        log.Write("INI EFFECTIVE [DamageResponse] BulletSnap=%.6f", incomingBulletSnap);
        log.Write("INI EFFECTIVE [DamageResponse] BulletRecovery=%.6f", incomingBulletRecovery);
        log.Write("INI EFFECTIVE [DamageResponse] ExplosiveScale=%.6f", incomingExplosiveDamageScale);
        log.Write("INI EFFECTIVE [DamageResponse] OtherScale=%.6f", incomingOtherDamageScale);
        log.Write("INI EFFECTIVE [DamageResponse] MinSeverity=%.6f", incomingDamageMinSeverity);
        log.Write("INI EFFECTIVE [DamageResponse] HighSnap=%.6f", incomingDamageHighSnap);
        log.Write("INI EFFECTIVE [DamageResponse] NpcBulletHitFallback=%d", npcBulletHitFallbackEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [DamageResponse] NpcBulletHitRadius=%.6f", npcBulletHitRadius);
        log.Write("INI EFFECTIVE [DamageResponse] NpcBulletHitFallbackScale=%.6f", npcBulletHitFallbackScale);
        log.Write("INI EFFECTIVE [DamageResponse] NpcBulletHitCooldownMs=%lu", static_cast<unsigned long>(npcBulletHitFallbackCooldownMs));
        log.Write("INI EFFECTIVE [CameraObstruction] OffsetCollision=%d", cameraOffsetCollisionEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [CameraObstruction] OffsetCollisionPadding=%.6f", cameraOffsetCollisionPadding);
        log.Write("INI EFFECTIVE [MeleeWeapons] Enabled=%d", detailedMeleeWeapons ? 1 : 0);
        log.Write("INI EFFECTIVE [MeleeWeapons] BatClubScale=%.6f", batSwingScale);
        log.Write("INI EFFECTIVE [MeleeWeapons] KnifeScale=%.6f", knifeSwingScale);
        log.Write("INI EFFECTIVE [MeleeWeapons] ChainsawVibration=%.6f", chainsawVibration);
        log.Write("INI EFFECTIVE [MeleeWeapons] HeavyScale=%.6f", heavyMeleeScale);
        log.Write("INI EFFECTIVE [RecoilContext] Enabled=%d", contextualRecoilEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [RecoilContext] CrouchScale=%.6f", recoilCrouchScale);
        log.Write("INI EFFECTIVE [RecoilContext] AimScale=%.6f", recoilAimScale);
        log.Write("INI EFFECTIVE [RecoilContext] MovingScale=%.6f", recoilMovingScale);
        log.Write("INI EFFECTIVE [RecoilContext] DualWieldScale=%.6f", recoilDualWieldScale);
        log.Write("INI EFFECTIVE [ReloadResponse] Enabled=%d", reloadAnimationResponse ? 1 : 0);
        log.Write("INI EFFECTIVE [ReloadResponse] InsertScale=%.6f", reloadInsertScale);
        log.Write("INI EFFECTIVE [ReloadResponse] ActionScale=%.6f", reloadActionScale);
        log.Write("INI EFFECTIVE [VehicleDynamics] WheelResponse=%d", wheelResponseEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [VehicleDynamics] WheelCompressionStrength=%.6f", wheelCompressionStrength);
        log.Write("INI EFFECTIVE [VehicleDynamics] TractionLossStrength=%.6f", tractionLossStrength);
        log.Write("INI EFFECTIVE [VehicleDynamics] TractionRegainStrength=%.6f", tractionRegainStrength);
        log.Write("INI EFFECTIVE [Developer] RuntimeReload=%d", runtimeReloadEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [Developer] ReloadKey=%ld", static_cast<long>(runtimeReloadKey));
        log.Write("INI EFFECTIVE [Developer] ReloadDebounceMs=%lu", static_cast<unsigned long>(runtimeReloadDebounceMs));
        log.Write("INI EFFECTIVE [Developer] DetailedHUD=%d", detailedDebugHud ? 1 : 0);
        log.Write("INI EFFECTIVE [General] TransitionsEnabled=%d", transitionsEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [General] MeleeEnabled=%d", meleeEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [General] BreathingEnabled=%d", breathingEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [General] SurfaceStepsEnabled=%d", surfaceStepsEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [Vehicle] Enabled=%d", vehicleEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [Vehicle] Strength=%.6f", vehicleStrength);
        log.Write("INI EFFECTIVE [Vehicle] AccelerationWeight=%.6f", vehicleAccelForward);
        log.Write("INI EFFECTIVE [Vehicle] BrakingWeight=%.6f", vehicleBrakeForward);
        log.Write("INI EFFECTIVE [Vehicle] CorneringWeight=%.6f", vehicleCornerSide);
        log.Write("INI EFFECTIVE [Vehicle] VerticalWeight=%.6f", vehicleVerticalAccel);
        log.Write("INI EFFECTIVE [Vehicle] SpeedVibration=%.6f", vehicleSpeedVibration);
        log.Write("INI EFFECTIVE [Vehicle] SpeedVibrationRate=%.6f", vehicleSpeedVibrationRate);
        log.Write("INI EFFECTIVE [VehicleHorn] Enabled=%d", vehicleHornEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [VehicleHorn] Vibration=%.6f", vehicleHornVibration);
        log.Write("INI EFFECTIVE [VehicleHorn] Rate=%.6f", vehicleHornVibrationRate);
        log.Write("INI EFFECTIVE [VehicleHorn] Attack=%.6f", vehicleHornAttack);
        log.Write("INI EFFECTIVE [VehicleHorn] Release=%.6f", vehicleHornRelease);
        log.Write("INI EFFECTIVE [Vehicle] ImpactThreshold=%.6f", vehicleImpactThreshold);
        log.Write("INI EFFECTIVE [Vehicle] ImpactImpulse=%.6f", vehicleImpactImpulse);
        log.Write("INI EFFECTIVE [VehicleCrash] ContactResponse=%d", vehicleContactResponseEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [VehicleCrash] ContactStrength=%.6f", vehicleContactStrength);
        log.Write("INI EFFECTIVE [VehicleCrash] ContactMinSpeed=%.6f", vehicleContactMinSpeed);
        log.Write("INI EFFECTIVE [VehicleCrash] ContactDeltaVelocityThreshold=%.6f", vehicleContactDeltaVelocityThreshold);
        log.Write("INI EFFECTIVE [VehicleCrash] ContactStrongDeltaVelocity=%.6f", vehicleContactStrongDeltaVelocity);
        log.Write("INI EFFECTIVE [VehicleCrash] ContactDamageReference=%.6f", vehicleContactDamageReference);
        log.Write("INI EFFECTIVE [VehicleCrash] ContactSpring=%.6f", vehicleContactSpring);
        log.Write("INI EFFECTIVE [VehicleCrash] ContactDamping=%.6f", vehicleContactDamping);
        log.Write("INI EFFECTIVE [VehicleCrash] ContactDirectKick=%.6f", vehicleContactDirectKick);
        log.Write("INI EFFECTIVE [VehicleCrash] ContactKickDecay=%.6f", vehicleContactKickDecay);
        log.Write("INI EFFECTIVE [VehicleCrash] ContactCooldownMs=%lu", static_cast<unsigned long>(vehicleContactCooldownMs));
        log.Write("INI EFFECTIVE [VehicleCrash] DamageResponse=%d", vehicleCrashDamageEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [VehicleCrash] DamageThreshold=%.6f", vehicleCrashDamageThreshold);
        log.Write("INI EFFECTIVE [VehicleCrash] DamageReference=%.6f", vehicleCrashDamageReference);
        log.Write("INI EFFECTIVE [VehicleCrash] DamageStrength=%.6f", vehicleCrashDamageStrength);
        log.Write("INI EFFECTIVE [VehicleCrash] FrontMultiplier=%.6f", vehicleCrashFrontMultiplier);
        log.Write("INI EFFECTIVE [VehicleCrash] RearMultiplier=%.6f", vehicleCrashRearMultiplier);
        log.Write("INI EFFECTIVE [VehicleCrash] SideMultiplier=%.6f", vehicleCrashSideMultiplier);
        log.Write("INI EFFECTIVE [VehicleCrash] VerticalMultiplier=%.6f", vehicleCrashVerticalMultiplier);
        log.Write("INI EFFECTIVE [VehicleCrash] CatastrophicThreshold=%.6f", vehicleCrashCatastrophicThreshold);
        log.Write("INI EFFECTIVE [VehicleCrash] CatastrophicMultiplier=%.6f", vehicleCrashCatastrophicMultiplier);
        log.Write("INI EFFECTIVE [VehicleCrash] PhysicsWindowMs=%lu", static_cast<unsigned long>(vehicleCrashPhysicsWindowMs));
        log.Write("INI EFFECTIVE [VehicleCrash] SuspensionResponse=%d", vehicleSuspensionEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [VehicleCrash] SuspensionThreshold=%.6f", vehicleSuspensionThreshold);
        log.Write("INI EFFECTIVE [VehicleCrash] SuspensionStrength=%.6f", vehicleSuspensionStrength);
        log.Write("INI EFFECTIVE [VehicleCrash] AdaptiveHandling=%d", vehicleSuspensionAdaptiveHandling ? 1 : 0);
        log.Write("INI EFFECTIVE [VehicleCrash] HandlingInfluence=%.6f", vehicleSuspensionHandlingInfluence);
        log.Write("INI EFFECTIVE [Vehicle] Spring=%.6f", vehicleSpring);
        log.Write("INI EFFECTIVE [Vehicle] Damping=%.6f", vehicleDamping);
        log.Write("INI EFFECTIVE [Vehicle] MaxSide=%.6f", vehicleMaxSide);
        log.Write("INI EFFECTIVE [Vehicle] MaxVertical=%.6f", vehicleMaxVertical);
        log.Write("INI EFFECTIVE [Vehicle] MaxForward=%.6f", vehicleMaxForward);
        log.Write("INI EFFECTIVE [VehicleProfiles] Automobile=%.6f", vehicleAutomobileScale);
        log.Write("INI EFFECTIVE [VehicleProfiles] Bike=%.6f", vehicleBikeScale);
        log.Write("INI EFFECTIVE [VehicleProfiles] Boat=%.6f", vehicleBoatScale);
        log.Write("INI EFFECTIVE [VehicleProfiles] Helicopter=%.6f", vehicleHeliScale);
        log.Write("INI EFFECTIVE [VehicleProfiles] Plane=%.6f", vehiclePlaneScale);
        log.Write("INI EFFECTIVE [VehicleProfiles] Other=%.6f", vehicleOtherScale);
        log.Write("INI EFFECTIVE [Alive] IdleSide=%.6f", idleSide);
        log.Write("INI EFFECTIVE [Alive] IdleVertical=%.6f", idleVertical);
        log.Write("INI EFFECTIVE [Alive] IdleForward=%.6f", idleForward);
        log.Write("INI EFFECTIVE [Alive] SettleDelay=%.6f", idleSettleDelay);
        log.Write("INI EFFECTIVE [Alive] SettledScale=%.6f", idleSettledScale);
        log.Write("INI EFFECTIVE [Alive] AimBreathingSide=%.6f", aimBreathingSide);
        log.Write("INI EFFECTIVE [Alive] AimBreathingVertical=%.6f", aimBreathingVertical);
        log.Write("INI EFFECTIVE [Alive] AimBreathingForward=%.6f", aimBreathingForward);
        log.Write("INI EFFECTIVE [Alive] AimBreathingRate=%.6f", aimBreathingRate);
        log.Write("INI EFFECTIVE [Gait] Side=%.6f", gaitSide);
        log.Write("INI EFFECTIVE [Gait] Vertical=%.6f", gaitVertical);
        log.Write("INI EFFECTIVE [Gait] Forward=%.6f", gaitForward);
        log.Write("INI EFFECTIVE [Gait] Fallback=%.6f", fallbackGait);
        log.Write("INI EFFECTIVE [Gait] StepImpulse=%.6f", stepImpulse);
        log.Write("INI EFFECTIVE [SurfaceSteps] Road=%.6f", surfaceRoad);
        log.Write("INI EFFECTIVE [SurfaceSteps] Concrete=%.6f", surfaceConcrete);
        log.Write("INI EFFECTIVE [SurfaceSteps] Gravel=%.6f", surfaceGravel);
        log.Write("INI EFFECTIVE [SurfaceSteps] Grass=%.6f", surfaceGrass);
        log.Write("INI EFFECTIVE [SurfaceSteps] Dirt=%.6f", surfaceDirt);
        log.Write("INI EFFECTIVE [SurfaceSteps] Sand=%.6f", surfaceSand);
        log.Write("INI EFFECTIVE [SurfaceSteps] Wood=%.6f", surfaceWood);
        log.Write("INI EFFECTIVE [SurfaceSteps] Metal=%.6f", surfaceMetal);
        log.Write("INI EFFECTIVE [SurfaceSteps] Rock=%.6f", surfaceRock);
        log.Write("INI EFFECTIVE [SurfaceSteps] Misc=%.6f", surfaceMisc);
        log.Write("INI EFFECTIVE [Look] YawInertia=%.6f", lookYaw);
        log.Write("INI EFFECTIVE [Look] PitchInertia=%.6f", lookPitch);
        log.Write("INI EFFECTIVE [Look] TurnInput=%.6f", turnInput);
        log.Write("INI EFFECTIVE [Look] AccelerationInertia=%.6f", accelInertia);
        log.Write("INI EFFECTIVE [Movement] DirectionalInertia=%.6f", directionalInertia);
        log.Write("INI EFFECTIVE [Movement] StrafeSway=%.6f", strafeSway);
        log.Write("INI EFFECTIVE [Movement] BackwardSway=%.6f", backwardSway);
        log.Write("INI EFFECTIVE [Impact] JumpImpulse=%.6f", jumpImpulse);
        log.Write("INI EFFECTIVE [Impact] LandingImpulse=%.6f", landingImpulse);
        log.Write("INI EFFECTIVE [Impact] HardLandingScale=%.6f", hardLandingScale);
        log.Write("INI EFFECTIVE [Impact] DamageImpulse=%.6f", damageImpulse);
        log.Write("INI EFFECTIVE [Impact] DamageSideImpulse=%.6f", damageSideImpulse);
        log.Write("INI EFFECTIVE [Impact] ExplosionImpulse=%.6f", explosionImpulse);
        log.Write("INI EFFECTIVE [Impact] ExplosionRadius=%.6f", explosionRadius);
        log.Write("INI EFFECTIVE [Impact] MaxImpactOffset=%.6f", maxImpactOffset);
        log.Write("INI EFFECTIVE [Impact] MaxFinalSide=%.6f", maxFinalSide);
        log.Write("INI EFFECTIVE [Impact] MaxFinalVertical=%.6f", maxFinalVertical);
        log.Write("INI EFFECTIVE [Impact] MaxFinalForward=%.6f", maxFinalForward);
        log.Write("INI EFFECTIVE [Impact] MeleeImpact=%.6f", meleeImpact);
        log.Write("INI EFFECTIVE [Impact] MeleeSide=%.6f", meleeSide);
        log.Write("INI EFFECTIVE [Melee] DetailedPlayerResponse=%d", detailedPlayerMelee ? 1 : 0);
        log.Write("INI EFFECTIVE [Melee] PunchWindup=%.6f", punchWindup);
        log.Write("INI EFFECTIVE [Melee] PunchRelease=%.6f", punchRelease);
        log.Write("INI EFFECTIVE [Melee] PunchContact=%.6f", punchContact);
        log.Write("INI EFFECTIVE [Melee] PunchSide=%.6f", punchSide);
        log.Write("INI EFFECTIVE [Melee] PunchVertical=%.6f", punchVertical);
        log.Write("INI EFFECTIVE [Melee] PunchReleasePhase=%.6f", punchReleasePhase);
        log.Write("INI EFFECTIVE [Melee] KickWindup=%.6f", kickWindup);
        log.Write("INI EFFECTIVE [Melee] KickRelease=%.6f", kickRelease);
        log.Write("INI EFFECTIVE [Melee] KickContact=%.6f", kickContact);
        log.Write("INI EFFECTIVE [Melee] KickSide=%.6f", kickSide);
        log.Write("INI EFFECTIVE [Melee] KickVertical=%.6f", kickVertical);
        log.Write("INI EFFECTIVE [Melee] KickReleasePhase=%.6f", kickReleasePhase);
        log.Write("INI EFFECTIVE [Melee] ComboStep=%.6f", meleeComboStep);
        log.Write("INI EFFECTIVE [Melee] ComboMaxScale=%.6f", meleeComboMaxScale);
        log.Write("INI EFFECTIVE [Rotation] IdlePitchDeg=%.6f", idlePitchDeg);
        log.Write("INI EFFECTIVE [Rotation] IdleRollDeg=%.6f", idleRollDeg);
        log.Write("INI EFFECTIVE [Rotation] GaitPitchDeg=%.6f", gaitPitchDeg);
        log.Write("INI EFFECTIVE [Rotation] GaitRollDeg=%.6f", gaitRollDeg);
        log.Write("INI EFFECTIVE [Rotation] TurnRollDeg=%.6f", turnRollDeg);
        log.Write("INI EFFECTIVE [Rotation] LookYawDeg=%.6f", lookYawDeg);
        log.Write("INI EFFECTIVE [Rotation] LookPitchDeg=%.6f", lookPitchDeg);
        log.Write("INI EFFECTIVE [Rotation] MaxPitchDeg=%.6f", maxPitchDeg);
        log.Write("INI EFFECTIVE [Rotation] MaxRollDeg=%.6f", maxRollDeg);
        log.Write("INI EFFECTIVE [Rotation] MaxYawDeg=%.6f", maxYawDeg);
        log.Write("INI EFFECTIVE [Recoil] Enabled=%d", recoilEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [Recoil] Strength=%.6f", recoilStrength);
        log.Write("INI EFFECTIVE [Recoil] VerticalKick=%.6f", recoilVerticalKick);
        log.Write("INI EFFECTIVE [Recoil] ForwardKick=%.6f", recoilForwardKick);
        log.Write("INI EFFECTIVE [Recoil] SideKick=%.6f", recoilSideKick);
        log.Write("INI EFFECTIVE [Recoil] PositionVertical=%.6f", recoilPositionVertical);
        log.Write("INI EFFECTIVE [Recoil] PositionForward=%.6f", recoilPositionForward);
        log.Write("INI EFFECTIVE [Recoil] PositionSide=%.6f", recoilPositionSide);
        log.Write("INI EFFECTIVE [Recoil] VelocityVertical=%.6f", recoilVelocityVertical);
        log.Write("INI EFFECTIVE [Recoil] VelocityForward=%.6f", recoilVelocityForward);
        log.Write("INI EFFECTIVE [Recoil] VelocitySide=%.6f", recoilVelocitySide);
        log.Write("INI EFFECTIVE [RecoilProfiles] Pistol=%.6f", recoilPistol);
        log.Write("INI EFFECTIVE [RecoilProfiles] DesertEagle=%.6f", recoilDeagle);
        log.Write("INI EFFECTIVE [RecoilProfiles] SMG=%.6f", recoilSmg);
        log.Write("INI EFFECTIVE [RecoilProfiles] Rifle=%.6f", recoilRifle);
        log.Write("INI EFFECTIVE [RecoilProfiles] Shotgun=%.6f", recoilShotgun);
        log.Write("INI EFFECTIVE [RecoilProfiles] Sniper=%.6f", recoilSniper);
        log.Write("INI EFFECTIVE [RecoilProfiles] Launcher=%.6f", recoilLauncher);
        log.Write("INI EFFECTIVE [RecoilProfiles] Flamethrower=%.6f", recoilFlamethrower);
        log.Write("INI EFFECTIVE [RecoilProfiles] Minigun=%.6f", recoilMinigun);
        log.Write("INI EFFECTIVE [Recoil] FirstShotMultiplier=%.6f", firstShotMultiplier);
        log.Write("INI EFFECTIVE [Recoil] SustainedShotMultiplier=%.6f", sustainedShotMultiplier);
        log.Write("INI EFFECTIVE [Recoil] SustainedResetMs=%lu", static_cast<unsigned long>(sustainedResetMs));
        log.Write("INI EFFECTIVE [Recoil] MinIntervalMs=%lu", static_cast<unsigned long>(recoilMinIntervalMs));
        log.Write("INI EFFECTIVE [Smoothing] Spring=%.6f", spring);
        log.Write("INI EFFECTIVE [Smoothing] Damping=%.6f", damping);
        log.Write("INI EFFECTIVE [Smoothing] LookSpring=%.6f", lookSpring);
        log.Write("INI EFFECTIVE [Smoothing] LookDamping=%.6f", lookDamping);
        log.Write("INI EFFECTIVE [Smoothing] StateResponse=%.6f", stateResponse);
        log.Write("INI EFFECTIVE [Smoothing] FootPhaseResponse=%.6f", footPhaseResponse);
        log.Write("INI EFFECTIVE [Smoothing] RecoilSpringMultiplier=%.6f", recoilSpringMultiplier);
        log.Write("INI EFFECTIVE [Smoothing] RecoilDampingMultiplier=%.6f", recoilDampingMultiplier);
        log.Write("INI EFFECTIVE [Smoothing] IdleDampingMultiplier=%.6f", idleDampingMultiplier);
        log.Write("INI EFFECTIVE [Debug] Enabled=%d", debug ? 1 : 0);
        log.Write("INI EFFECTIVE [Debug] LogEveryMs=%lu", static_cast<unsigned long>(logEveryMs));
        log.Write("INI EFFECTIVE [Debug] LogStepEvents=%d", logSteps ? 1 : 0);
        log.Write("INI EFFECTIVE [Debug] LogShotEvents=%d", logShots ? 1 : 0);
        log.Write("INI EFFECTIVE [Debug] OverlayEnabled=%d", overlayEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [DebugAudit] Enabled=%d", auditEnabled ? 1 : 0);
        log.Write("INI EFFECTIVE [DebugAudit] LogConfigValues=%d", auditConfigValues ? 1 : 0);
        log.Write("INI EFFECTIVE [DebugAudit] LogFunctionCalls=%d", auditFunctionCalls ? 1 : 0);
        log.Write("INI EFFECTIVE [DebugAudit] SummaryEveryMs=%lu", static_cast<unsigned long>(auditEveryMs));
        log.Write("INI EFFECTIVE [Performance] NearbyVehicleScanMs=%lu", static_cast<unsigned long>(nearbyVehicleScanMs));
        log.Write("INI EFFECTIVE [Performance] SurfaceScanMs=%lu", static_cast<unsigned long>(surfaceScanMs));
        log.Write("INI EFFECTIVE [General] Preset=%s", presetName);
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
            log.Write("INI EFFECTIVE [WeaponRecoil] Weapon%dScale=%.6f", id, weaponScale[id]);
            log.Write("INI EFFECTIVE [WeaponRecoil] Weapon%dVertical=%.6f", id, weaponVertical[id]);
            log.Write("INI EFFECTIVE [WeaponRecoil] Weapon%dForward=%.6f", id, weaponForward[id]);
            log.Write("INI EFFECTIVE [WeaponRecoil] Weapon%dSide=%.6f", id, weaponSide[id]);
            log.Write("INI EFFECTIVE [WeaponRecoil] Weapon%dRecovery=%.6f", id, weaponRecovery[id]);
        }
        log.Write("INI AUDIT END effective-values");
    }

};

