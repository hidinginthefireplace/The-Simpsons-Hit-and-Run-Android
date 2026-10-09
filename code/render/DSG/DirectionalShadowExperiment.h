// Experimental directional ground-shadow extension for Android.
#ifndef DIRECTIONAL_SHADOW_EXPERIMENT_H
#define DIRECTIONAL_SHADOW_EXPERIMENT_H

#include <contexts/bootupcontext.h>
#include <p3d/matrixstack.hpp>
#include <pddi/pddi.hpp>

// This deliberately adds a soft directional tail to existing simple shadows.
// It is not a shadow map and does not replace the game's original shadow.
namespace DirectionalShadowExperiment
{
    // Provisional world-space cast direction, projected onto each ground plane.
    // The game source does not expose a verified direction for the visible sky sun,
    // so this is a single test direction to be tuned after on-device inspection.
    static const float SHADOW_DIR_X = 0.60f;
    static const float SHADOW_DIR_Z = -0.80f;

    inline void EmitVertex(pddiPrimStream* stream, float x, float y, int alpha)
    {
        stream->Colour(tColour(0, 0, 0, alpha));
        stream->Coord(x, y, 0.0f);
    }

    inline void EmitQuad(
        pddiPrimStream* stream,
        float x00, float x10, int a00, int a10,
        float x01, float x11, int a01, int a11,
        float y0, float y1)
    {
        EmitVertex(stream, x00, y0, a00);
        EmitVertex(stream, x10, y0, a10);
        EmitVertex(stream, x01, y1, a01);

        EmitVertex(stream, x10, y0, a10);
        EmitVertex(stream, x11, y1, a11);
        EmitVertex(stream, x01, y1, a01);
    }

    // halfWidth is the half-width of the tail at its widest point.
    // startDistance lets the tail begin just beyond the existing blob shadow.
    inline void Draw(
        const rmt::Vector& groundPosition,
        const rmt::Vector& groundNormal,
        float length,
        float halfWidth,
        int maximumAlpha,
        float startDistance,
        float opacityScale = 1.0f)
    {
        if (length <= 0.0f || halfWidth <= 0.0f || maximumAlpha <= 0)
        {
            return;
        }

        rmt::Vector castDirection(SHADOW_DIR_X, 0.0f, SHADOW_DIR_Z);
        const float normalPart = castDirection.DotProduct(groundNormal);
        castDirection.ScaleAdd(-normalPart, groundNormal);
        const float directionLength = castDirection.Magnitude();
        if (directionLength < 0.001f)
        {
            return;
        }
        castDirection.Scale(1.0f / directionLength);

        // Lift a tiny amount along the ground normal to reduce z-fighting.
        rmt::Vector raisedPosition(groundPosition);
        raisedPosition.ScaleAdd(0.015f, groundNormal);

        rmt::Matrix shadowTransform;
        shadowTransform.Identity();
        shadowTransform.FillTranslate(raisedPosition);
        shadowTransform.FillHeading(groundNormal, castDirection);

        pddiShader* shadowShader = BootupContext::GetInstance()->GetSharedShader();
        if (shadowShader == NULL)
        {
            return;
        }

        int baseAlpha = int(maximumAlpha * opacityScale);
        if (baseAlpha < 0) baseAlpha = 0;
        if (baseAlpha > 255) baseAlpha = 255;
        if (baseAlpha == 0)
        {
            return;
        }

        const float y[5] = {
            startDistance,
            startDistance + length * 0.24f,
            startDistance + length * 0.50f,
            startDistance + length * 0.76f,
            startDistance + length
        };
        const float innerWidth[5] = {
            halfWidth * 0.48f,
            halfWidth * 0.45f,
            halfWidth * 0.36f,
            halfWidth * 0.22f,
            halfWidth * 0.02f
        };
        const float outerWidth[5] = {
            halfWidth,
            halfWidth * 0.86f,
            halfWidth * 0.66f,
            halfWidth * 0.40f,
            halfWidth * 0.05f
        };
        const int alpha[5] = {
            baseAlpha,
            baseAlpha * 4 / 5,
            baseAlpha * 3 / 5,
            baseAlpha / 3,
            0
        };

        shadowShader->SetInt(PDDI_SP_BLENDMODE, PDDI_BLEND_MODULATE);
        shadowShader->SetInt(PDDI_SP_ISLIT, 0);
        shadowShader->SetInt(PDDI_SP_ALPHATEST, 0);
        shadowShader->SetInt(PDDI_SP_SHADEMODE, PDDI_SHADE_GOURAUD);

        p3d::stack->PushMultiply(shadowTransform);
        // Four longitudinal sections, with a dark centre and transparent side
        // feathers. This keeps the extension soft instead of drawing a hard strip.
        pddiPrimStream* stream = p3d::pddi->BeginPrims(
            shadowShader, PDDI_PRIM_TRIANGLES, PDDI_V_C, 72);

        for (int i = 0; i < 4; ++i)
        {
            // Centre band.
            EmitQuad(stream,
                -innerWidth[i], innerWidth[i], alpha[i], alpha[i],
                -innerWidth[i + 1], innerWidth[i + 1], alpha[i + 1], alpha[i + 1],
                y[i], y[i + 1]);

            // Left soft edge.
            EmitQuad(stream,
                -outerWidth[i], -innerWidth[i], 0, alpha[i],
                -outerWidth[i + 1], -innerWidth[i + 1], 0, alpha[i + 1],
                y[i], y[i + 1]);

            // Right soft edge.
            EmitQuad(stream,
                innerWidth[i], outerWidth[i], alpha[i], 0,
                innerWidth[i + 1], outerWidth[i + 1], alpha[i + 1], 0,
                y[i], y[i + 1]);
        }

        p3d::pddi->EndPrims(stream);
        p3d::stack->Pop();
    }
}

#endif // DIRECTIONAL_SHADOW_EXPERIMENT_H
