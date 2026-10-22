// Internal single-translation-unit fragment: keeps fixed-address hooks and RenderWare integration isolated.
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

    static uintptr_t DecodeRelativeCallTarget(uintptr_t callsite) {
        if (!cr::ClassicSaImageLayout() ||
            !cr::ReadableImageCode(callsite, 5))
            return 0;
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
        if (!cr::ClassicSaImageLayout() ||
            !cr::ReadableImageCode(kProcessCopyCallsite - 3, 20))
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
            log_.Write(
                "ERROR: unsupported camera callsite signature at 0x%08X; hook NOT installed",
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

        // The one-time installation may safely chain an already redirected
        // CALL. If a later-loaded plugin replaces our hook, VerifyCameraCommitHook
        // yields rather than rewriting the callsite again.
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

        // Do not repeatedly rewrite executable memory. If a later-loaded mod
        // replaces this CALL, yield cleanly and keep the plugin running with
        // camera output disabled. This is safer for mod interoperability and
        // avoids the loader-like "watchdog repatching" pattern that heuristic
        // scanners may dislike.
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

        // Convert camera-local motion to GTA world space.
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

        // This is the matrix CopyCameraMatrixToRWCam is about to commit.
        // Modify position AND orientation only for the duration of GTA's copy.
        // Translation-only commit. Never touch right/up/at basis vectors.
        // This preserves GTA/RenderWare's exact camera orientation.
        const CVector savedPos =
            camera->m_mCameraMatrix.pos;

        lastMatrixBase_ = savedPos;

        camera->m_mCameraMatrix.pos.x += worldOffset.x;
        camera->m_mCameraMatrix.pos.y += worldOffset.y;
        camera->m_mCameraMatrix.pos.z += worldOffset.z;

        lastMatrixForced_ =
            camera->m_mCameraMatrix.pos;

        // Let GTA perform its normal RenderWare camera copy.
        originalCopyCamera_(camera, updateMatrix);

        // Verify the actual RenderWare camera frame. Normally GTA's copy above
        // already puts the forced matrix there. If another mod/pipeline does
        // not, force the frame position and update attached RW objects.
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
                            // Do not risk carrying a malformed basis.
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

        // Restore GTA's internal matrix. RenderWare keeps the copied
        // translated/rotated camera until the next stock camera commit.
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

