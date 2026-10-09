//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include <p3d/directionallight.hpp>
#include <p3d/context.hpp>
#include <p3d/utility.hpp>

#if defined(RAD_ANDROID)
#include <android/log.h>

namespace
{
struct DirectionalLightDiagnosticEntry
{
    const tDirectionalLight* light;
    float x;
    float y;
    float z;
    unsigned slot;
    bool enabled;
    bool shadowCaster;
    bool used;
};

static void LogSubmittedDirectionalLight(
    const tDirectionalLight* light,
    unsigned slot,
    const rmt::Vector& direction,
    bool enabled,
    bool shadowCaster)
{
    // Keep the diagnostic useful without flooding logcat for lights whose
    // direction is re-submitted every frame by an animation controller.
    static DirectionalLightDiagnosticEntry entries[128] = {};
    static bool cacheFullWarningLogged = false;
    int entryIndex = -1;
    int freeIndex = -1;

    for (int i = 0; i < 128; ++i)
    {
        if (entries[i].used && entries[i].light == light)
        {
            entryIndex = i;
            break;
        }
        if (!entries[i].used && freeIndex < 0)
            freeIndex = i;
    }

    if (entryIndex < 0)
    {
        if (freeIndex < 0)
        {
            if (!cacheFullWarningLogged)
            {
                cacheFullWarningLogged = true;
                __android_log_print(
                    ANDROID_LOG_WARN,
                    "SHAR-LightDiag",
                    "Directional-light diagnostic cache is full; additional light directions are not being tracked");
            }
            return;
        }
        entryIndex = freeIndex;
    }

    DirectionalLightDiagnosticEntry& entry = entries[entryIndex];
    const float dx = direction.x - entry.x;
    const float dy = direction.y - entry.y;
    const float dz = direction.z - entry.z;
    const bool newLight = !entry.used;
    const bool directionChanged =
        (dx * dx + dy * dy + dz * dz) >= 0.0025f; // roughly a 0.05 unit-vector change
    const bool stateChanged =
        entry.slot != slot ||
        entry.enabled != enabled ||
        entry.shadowCaster != shadowCaster;

    if (newLight || directionChanged || stateChanged)
    {
        __android_log_print(
            ANDROID_LOG_INFO,
            "SHAR-LightDiag",
            "Submitted directional light: ptr=%p slot=%u direction=(%.4f, %.4f, %.4f) enabled=%d shadow_caster=%d",
            (const void*)light,
            slot,
            direction.x,
            direction.y,
            direction.z,
            enabled ? 1 : 0,
            shadowCaster ? 1 : 0);

        entry.light = light;
        entry.x = direction.x;
        entry.y = direction.y;
        entry.z = direction.z;
        entry.slot = slot;
        entry.enabled = enabled;
        entry.shadowCaster = shadowCaster;
        entry.used = true;
    }
}
}
#endif

tDirectionalLight::tDirectionalLight() :
    direction(0.0f, 0.0f, 1.0f)
{
}

tDirectionalLight::tDirectionalLight(tDirectionalLight *dirLight) :
    tLight(dirLight),
    direction(dirLight->direction)
{
}

void tDirectionalLight::SetDirection(float x, float y, float z)
{
    direction.x = x;
    direction.y = y;
    direction.z = z;

    direction.Normalize();

    Update();
}

void tDirectionalLight::GetDirection(float* x, float* y, float* z)
{
    *x = direction.x;
    *y = direction.y;
    *z = direction.z;
}

void tDirectionalLight::Update()
{
    if(!active)
        return;

    pddiLightDesc desc(enabled);
    desc.SetDirectionalLight(colour, (pddiVector*)&direction);
    p3d::pddi->SetLight(slot, &desc);
#ifdef RAD_ANDROID
    LogSubmittedDirectionalLight(this, slot, direction, enabled, isShadowCaster);
#endif
}
    

