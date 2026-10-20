#!/usr/bin/env python3
"""Apply a focused player-only nose-contour pass for this experimental APK build.

The main eye contour, face-bound size, depth edge settings, and motion-blur source
are deliberately left unchanged. This script is run by the branch's build workflow.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = ROOT / "libs/pure3d/pddi/gles/display_win32/gldisplay.cpp"


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise RuntimeError(f"Expected exactly one {label} anchor, found {count}; refusing partial patch.")
    return source.replace(old, new, 1)


source = SOURCE_PATH.read_text(encoding="utf-8")
if "float faceNoseDetailEdge = 0.0;" in source:
    raise RuntimeError("The nose-contour patch already appears in the renderer source.")

declaration = '            "    float faceColourDetailEdge = 0.0;\\n"\n'
source = replace_once(
    source, declaration,
    declaration + '            "    float faceNoseDetailEdge = 0.0;\\n"\n',
    "facial-edge declaration",
)

nose_lines = [
    "    // Native-pixel contour sampling is restricted to a compact lower-central player-face area.",
    "    vec4 noseBounds = characterOutlineBounds[0];",
    "    if (noseBounds.w > 0.0) {",
    "        vec3 noseFromCamera = noseBounds.xyz - currentCameraPosition;",
    "        float noseCentreDepth = dot(noseFromCamera, currentCameraForward);",
    "        if (noseCentreDepth > currentNearPlane) {",
    "            vec2 noseCentreNdc = vec2(dot(noseFromCamera, currentCameraRight) / (noseCentreDepth * currentTanHalfFovX), dot(noseFromCamera, currentCameraUp) / (noseCentreDepth * currentTanHalfFovY));",
    "            vec2 noseCentreUv = noseCentreNdc * 0.5 + 0.5;",
    "            vec2 noseHalfSize = vec2(noseBounds.w, noseBounds.w * 0.82);",
    "            float noseHalfWidth = sqrt(noseHalfSize.x * noseHalfSize.x * (currentCameraRight.x * currentCameraRight.x + currentCameraRight.z * currentCameraRight.z) + noseHalfSize.y * noseHalfSize.y * currentCameraRight.y * currentCameraRight.y);",
    "            float noseHalfHeight = sqrt(noseHalfSize.x * noseHalfSize.x * (currentCameraUp.x * currentCameraUp.x + currentCameraUp.z * currentCameraUp.z) + noseHalfSize.y * noseHalfSize.y * currentCameraUp.y * currentCameraUp.y);",
    "            vec2 noseRadius = vec2(noseHalfWidth / (noseCentreDepth * currentTanHalfFovX), noseHalfHeight / (noseCentreDepth * currentTanHalfFovY)) * 0.5;",
    "            noseRadius = max(noseRadius, texelSize * 1.5);",
    "            vec2 noseLocal = (texcoord - noseCentreUv) / noseRadius;",
    "            float noseArea = (1.0 - smoothstep(0.34, 0.44, abs(noseLocal.x))) * smoothstep(-0.44, -0.32, noseLocal.y) * (1.0 - smoothstep(0.08, 0.20, noseLocal.y));",
    "            if (noseArea > 0.05) {",
    "                float n = sceneLuma(texture2D(sceneTex, texcoord).rgb);",
    "                float nl = sceneLuma(texture2D(sceneTex, texcoord - vec2(texelSize.x, 0.0)).rgb);",
    "                float nr = sceneLuma(texture2D(sceneTex, texcoord + vec2(texelSize.x, 0.0)).rgb);",
    "                float nu = sceneLuma(texture2D(sceneTex, texcoord + vec2(0.0, texelSize.y)).rgb);",
    "                float nd = sceneLuma(texture2D(sceneTex, texcoord - vec2(0.0, texelSize.y)).rgb);",
    "                float nul = sceneLuma(texture2D(sceneTex, texcoord + vec2(-texelSize.x, texelSize.y)).rgb);",
    "                float nur = sceneLuma(texture2D(sceneTex, texcoord + vec2(texelSize.x, texelSize.y)).rgb);",
    "                float ndl = sceneLuma(texture2D(sceneTex, texcoord + vec2(-texelSize.x, -texelSize.y)).rgb);",
    "                float ndr = sceneLuma(texture2D(sceneTex, texcoord + vec2(texelSize.x, -texelSize.y)).rgb);",
    "                float noseEdgeStrength = max(max(abs(n - nl), abs(n - nr)), max(max(abs(n - nu), abs(n - nd)), max(max(abs(n - nul), abs(n - nur)), max(abs(n - ndl), abs(n - ndr)))));",
    "                faceNoseDetailEdge = smoothstep(0.008, 0.035, noseEdgeStrength) * noseArea * 0.60;",
    "            }",
    "        }",
    "    }",
]
nose_block = "\n".join(f'            "{line}\\n"' for line in nose_lines) + "\n"
fine_anchor = '            "    // Recover tiny colour edges only above 1080p, independently of the broad outline sampling scale.\\n"'
source = replace_once(source, fine_anchor, nose_block + fine_anchor, "fine-detail insertion")

fade = '            "        faceColourDetailEdge *= mix(1.0, 0.30, distanceFade);\\n"\n'
source = replace_once(
    source, fade,
    fade + '            "        faceNoseDetailEdge *= mix(1.0, 0.30, distanceFade);\\n"\n',
    "distance fade insertion",
)

combine = '            "    edge = max(edge, faceColourDetailEdge);\\n"\n'
source = replace_once(
    source, combine,
    combine + '            "    edge = max(edge, faceNoseDetailEdge);\\n"\n',
    "nose edge combine",
)

required = [
    "smoothstep(0.008, 0.035, noseEdgeStrength)",
    "edge = max(edge, faceNoseDetailEdge);",
    "smoothstep(0.024, 0.082, edgeStrength) * faceMask * 0.60",
    "SHAR_MAX_CHARACTER_OUTLINE_BOUNDS = 1",
    "RegisterSHARMotionBlurSubjectBounds",
]
for marker in required:
    if marker not in source:
        raise RuntimeError(f"Post-patch validation failed for {marker!r}.")

SOURCE_PATH.write_text(source, encoding="utf-8")
print("Applied focused player nose-contour shader patch; eye contours and motion-blur code retained.")
