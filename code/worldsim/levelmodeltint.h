#ifndef SHAR_LEVEL_MODEL_LIGHTING_TINT_H
#define SHAR_LEVEL_MODEL_LIGHTING_TINT_H

// Temporarily reduce the lighting applied to world characters and vehicles in
// the darker later levels. All original lighting state is restored at scope exit,
// so scenery, GUI, menus, and subsequent renderables keep their original colours.

#ifdef RAD_ANDROID

#include <mission/gameplaymanager.h>
#include <p3d/utility.hpp>
#include <pddi/pddi.hpp>

class SHARLevelModelLightingTint
{
public:
    SHARLevelModelLightingTint()
        : mContext(NULL),
          mLightCount(0),
          mActive(false)
    {
        GameplayManager* gameplayManager = ::GetGameplayManager();
        if (gameplayManager == NULL ||
            gameplayManager->GetCurrentMission() == NULL ||
            p3d::pddi == NULL)
        {
            return;
        }

        const RenderEnums::LevelEnum level = gameplayManager->GetCurrentLevelIndex();
        float multiplier = 1.0f;

        if (level == RenderEnums::L3)
        {
            multiplier = 0.85f;
        }
        else if (level >= RenderEnums::L4 && level <= RenderEnums::L7)
        {
            multiplier = 0.80f;
        }
        else
        {
            // Leave Levels 1 and 2, mini-games, and non-level contexts unchanged.
            return;
        }

        mContext = p3d::pddi;
        mAmbientColour = mContext->GetAmbientLight();
        mContext->SetAmbientLight(ScaleColour(mAmbientColour, multiplier));

        int availableLights = mContext->GetMaxLights();
        if (availableLights < 0)
        {
            availableLights = 0;
        }
        if (availableLights > MAX_TRACKED_LIGHTS)
        {
            availableLights = MAX_TRACKED_LIGHTS;
        }

        mLightCount = availableLights;
        for (int i = 0; i < mLightCount; ++i)
        {
            mLightColours[i] = mContext->GetLightColour(i);
            mContext->SetLightColour(i, ScaleColour(mLightColours[i], multiplier));
        }

        mActive = true;
    }

    ~SHARLevelModelLightingTint()
    {
        if (!mActive || mContext == NULL)
        {
            return;
        }

        for (int i = 0; i < mLightCount; ++i)
        {
            mContext->SetLightColour(i, mLightColours[i]);
        }
        mContext->SetAmbientLight(mAmbientColour);
    }

private:
    enum { MAX_TRACKED_LIGHTS = 16 };

    static pddiColour ScaleColour(const pddiColour& colour, float multiplier)
    {
        return pddiColour(
            static_cast<int>(colour.Red() * multiplier + 0.5f),
            static_cast<int>(colour.Green() * multiplier + 0.5f),
            static_cast<int>(colour.Blue() * multiplier + 0.5f),
            colour.Alpha());
    }

    pddiRenderContext* mContext;
    pddiColour mAmbientColour;
    pddiColour mLightColours[MAX_TRACKED_LIGHTS];
    int mLightCount;
    bool mActive;
};

#else

// Keep non-Android platform builds source-compatible.
class SHARLevelModelLightingTint
{
public:
    SHARLevelModelLightingTint() {}
};

#endif // RAD_ANDROID

#endif // SHAR_LEVEL_MODEL_LIGHTING_TINT_H
