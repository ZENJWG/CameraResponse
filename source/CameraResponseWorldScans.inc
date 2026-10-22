// Internal single-translation-unit fragment: methods remain inside CameraResponse class for ABI stability.
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
        cr::ScopedScanProfile scanScope(scanProfiler_, cr::ScanKind::LargeVehicles, cfg_.scanProfilingEnabled);

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
        cr::ScopedScanProfile scanScope(scanProfiler_, cr::ScanKind::WorldObjects, cfg_.scanProfilingEnabled);

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

            // GTA updates m_nLastCollisionTime while contact persists. A changing
            // timestamp is therefore not, by itself, a new impact.
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
        cr::ScopedScanProfile scanScope(scanProfiler_, cr::ScanKind::PedWorld, cfg_.scanProfilingEnabled);

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

            // Nearby sprinting/running pedestrians produce a restrained
            // low-frequency footfall response. This intentionally avoids bone
            // sampling for NPCs to keep the feature cheap.
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

                // Counter the abrupt stock inward push, then let the spring
                // settle naturally. Translation only; GTA still owns camera LOS.
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

