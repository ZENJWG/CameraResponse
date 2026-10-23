#pragma once

#ifdef _WIN32
#define CAMERA_RESPONSE_API extern "C" __declspec(dllexport)
#else
#define CAMERA_RESPONSE_API extern "C"
#endif

// Plugin-sdk-free ABI; offsets use camera-local side/vertical/forward axes.
CAMERA_RESPONSE_API unsigned CameraResponse_GetApiVersion();
CAMERA_RESPONSE_API int CameraResponse_GetOffset(float* side, float* vertical, float* forward);
CAMERA_RESPONSE_API int CameraResponse_GetRotation(float* pitchDeg, float* rollDeg, float* yawDeg);
CAMERA_RESPONSE_API void CameraResponse_SetExternalScale(float scale);
CAMERA_RESPONSE_API void CameraResponse_AddImpulse(
    float side,
    float vertical,
    float forward,
    float priority,
    int frequencyBand
);

CAMERA_RESPONSE_API int CameraResponse_QueueImpulseV2(
    float side,
    float vertical,
    float forward,
    float priority,
    int frequencyBand,
    unsigned sourceTag,
    unsigned ttlMs
);

CAMERA_RESPONSE_API void CameraResponse_ClearExternalQueue();
