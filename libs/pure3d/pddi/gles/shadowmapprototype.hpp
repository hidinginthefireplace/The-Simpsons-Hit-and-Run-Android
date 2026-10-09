// Experimental Android shadow-map prototype interface.
// This is isolated to the android11-shield-shadowmap-prototype branch.
#ifndef SHAR_SHADOWMAP_PROTOTYPE_HPP
#define SHAR_SHADOWMAP_PROTOTYPE_HPP

#if defined(RAD_ANDROID)
void SHAR_ResetShadowMapPrototypeSourceLight();
void SHAR_SetShadowMapPrototypeSourceLight(
    const char* name,
    float directionX,
    float directionY,
    float directionZ,
    bool enabled,
    bool shadowCaster,
    int illuminationType);
bool SHAR_GetShadowMapPrototypeDirection(float* x, float* y, float* z);
bool SHAR_IsShadowMapPrototypeEnabled();
bool SHAR_BeginShadowMapPrototype(int width, int height);
void SHAR_BindShadowMapPrototypeViewport();
void SHAR_EndShadowMapPrototype();
void SHAR_SetShadowMapPrototypeLightViewProjection(const float* matrix16);
void SHAR_SetShadowMapPrototypeInverseCameraViewProjection(const float* matrix16);
#endif

#endif
