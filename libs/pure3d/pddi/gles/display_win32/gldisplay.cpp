//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/glcon.hpp>
#include <pddi/gles/gldisplay.hpp>
#include <pddi/base/debug.hpp>
#include <SDL.h>

#include<stdio.h>
#include<string.h>
#include<math.h>
#include<vector>

#if defined(RAD_ANDROID)
#include <jni.h>

bool IsCelShadingEnabled();
float GetFrameSharpenStrength();
void GetCRTFilterConfiguration(bool* enabled, float* scanlineStrength, float* curvature, float* glowStrength, float* cornerDarkening, float* overscan, float* brightness, float* contrast);

namespace
{
GLuint gCelPostProgram = 0;
GLuint gCelPostTexture = 0;
GLuint gCelPostVbo = 0;
GLuint gCelRenderFbo = 0;
GLuint gCelRenderDepth = 0;
GLuint gCelRenderDepthTexture = 0;
int gCelDepthTextureWidth = 0;
int gCelDepthTextureHeight = 0;
bool gCelDepthTextureActive = false;
bool gCelDepthTextureFailed = false;
bool gCelDepthExtensionChecked = false;
bool gCelDepthTextureExtensionAvailable = false;
bool gCelRenderFboReady = false;
GLint gCelPostSceneLocation = -1;
GLint gCelPostTexelLocation = -1;
GLint gCelPostBloomLocation = -1;
GLint gCelPostBloomStrengthLocation = -1;
GLint gCelPostDepthLocation = -1;
GLint gCelPostDepthAvailableLocation = -1;

GLuint gCelBloomExtractProgram = 0;
GLuint gCelBloomBlurProgram = 0;

GLuint gFrameSharpenProgram = 0;
GLuint gFrameSharpenTexture = 0;
GLuint gFrameSharpenVbo = 0;
GLint gFrameSharpenSceneLocation = -1;
GLint gFrameSharpenTexelLocation = -1;
GLint gFrameSharpenStrengthLocation = -1;
int gFrameSharpenWidth = 0;
int gFrameSharpenHeight = 0;

GLuint gCRTProgram = 0;
GLuint gCRTTexture = 0;
GLuint gCRTVbo = 0;
GLint gCRTSceneLocation = -1;
GLint gCRTTexelLocation = -1;
GLint gCRTScanlineLocation = -1;
GLint gCRTCurvatureLocation = -1;
GLint gCRTGlowLocation = -1;
GLint gCRTCornersLocation = -1;
GLint gCRTOverscanLocation = -1;
GLint gCRTBrightnessLocation = -1;
GLint gCRTContrastLocation = -1;
int gCRTWidth = 0;
int gCRTHeight = 0;

GLuint gCelBloomFbo = 0;
GLuint gCelBloomTexture = 0;
GLuint gCelBloomScratchTexture = 0;
GLint gCelBloomExtractSceneLocation = -1;
GLint gCelBloomExtractTexelLocation = -1;
GLint gCelBloomBlurSourceLocation = -1;
GLint gCelBloomBlurStepLocation = -1;
int gCelBloomWidth = 0;
int gCelBloomHeight = 0;
int gCelBloomFailedWidth = 0;
int gCelBloomFailedHeight = 0;
bool gCelBloomReady = false;

int gCelPostWidth = 0;
int gCelPostHeight = 0;

static GLuint CompileCelPostShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, 0);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE)
    {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        if (length > 0)
        {
            std::vector<char> log((size_t)length + 1, 0);
            glGetShaderInfoLog(shader, length, NULL, log.data());
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cel post-process shader compile failed: %s", log.data());
        }
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static GLuint CreateCelPostProgram(const char* vertexSource, const char* fragmentSource, const char* label)
{
    GLuint vs = CompileCelPostShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fs = CompileCelPostShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vs == 0 || fs == 0)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    GLuint program = glCreateProgram();
    glBindAttribLocation(program, 0, "position");
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE)
    {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        if (length > 0)
        {
            std::vector<char> log((size_t)length + 1, 0);
            glGetProgramInfoLog(program, length, NULL, log.data());
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s shader program link failed: %s",
                label ? label : "Cel post-process", log.data());
        }
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

static bool EnsureCelPostProcessResources(int width, int height);

static bool EnsureCelRenderTarget(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    if (gCelRenderFbo == 0)
        glGenFramebuffers(1, &gCelRenderFbo);
    if (gCelRenderDepth == 0)
        glGenRenderbuffers(1, &gCelRenderDepth);
    if (gCelRenderFbo == 0 || gCelRenderDepth == 0)
        return false;

    if (!EnsureCelPostProcessResources(width, height))
        return false;

    if (!gCelDepthExtensionChecked)
    {
        gCelDepthTextureExtensionAvailable =
            SDL_GL_ExtensionSupported("GL_OES_depth_texture") == SDL_TRUE;
        gCelDepthExtensionChecked = true;
        SDL_Log(
            "SHAR Android depth-aware outlines: GL_OES_depth_texture %s",
            gCelDepthTextureExtensionAvailable ? "available" : "unavailable"
        );
    }

    GLint previousFramebuffer = 0;
    GLint previousTexture2D = 0;
    GLint previousRenderbuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture2D);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);

    glBindTexture(GL_TEXTURE_2D, gCelPostTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, gCelRenderFbo);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        gCelPostTexture,
        0
    );

    // Prefer a sampleable depth texture when the device exposes GLES 2.0's
    // depth-texture extension. If allocation/completeness fails, fall back to
    // the original depth renderbuffer and disable only depth-aware fading.
    gCelDepthTextureActive = false;
    bool depthTextureReady = false;
    if (gCelDepthTextureExtensionAvailable && !gCelDepthTextureFailed)
    {
        if (gCelRenderDepthTexture == 0)
            glGenTextures(1, &gCelRenderDepthTexture);

        if (gCelRenderDepthTexture != 0)
        {
            glBindTexture(GL_TEXTURE_2D, gCelRenderDepthTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            if (gCelDepthTextureWidth != width || gCelDepthTextureHeight != height)
            {
                glTexImage2D(
                    GL_TEXTURE_2D,
                    0,
                    GL_DEPTH_COMPONENT,
                    width,
                    height,
                    0,
                    GL_DEPTH_COMPONENT,
                    GL_UNSIGNED_INT,
                    NULL
                );
                gCelDepthTextureWidth = width;
                gCelDepthTextureHeight = height;
            }

            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_DEPTH_ATTACHMENT,
                GL_TEXTURE_2D,
                gCelRenderDepthTexture,
                0
            );
            GLenum depthTextureStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            depthTextureReady = (depthTextureStatus == GL_FRAMEBUFFER_COMPLETE);
            if (!depthTextureReady)
            {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "SHAR Android depth texture unsupported by framebuffer (0x%04x); using depth renderbuffer",
                    (unsigned)depthTextureStatus
                );
                gCelDepthTextureFailed = true;
            }
        }
        else
        {
            gCelDepthTextureFailed = true;
        }
    }

    if (depthTextureReady)
    {
        gCelDepthTextureActive = true;
        gCelRenderFboReady = true;
    }
    else
    {
        glBindRenderbuffer(GL_RENDERBUFFER, gCelRenderDepth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER,
            GL_DEPTH_ATTACHMENT,
            GL_RENDERBUFFER,
            gCelRenderDepth
        );

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        gCelRenderFboReady = (status == GL_FRAMEBUFFER_COMPLETE);
        if (!gCelRenderFboReady)
        {
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "SHAR Android cel render FBO incomplete: 0x%04x",
                (unsigned)status
            );
        }
    }

    if (gCelRenderFboReady)
    {
        SDL_Log(
            "SHAR Android cel render FBO ready: %dx%d (depth texture %s)",
            width,
            height,
            gCelDepthTextureActive ? "enabled" : "disabled"
        );
    }

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)previousRenderbuffer);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture2D);

    return gCelRenderFboReady;
}

static bool BindCelRenderTarget(int width, int height)
{
    if (!EnsureCelRenderTarget(width, height))
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, gCelRenderFbo);
    glViewport(0, 0, width, height);
    return true;
}

static bool EnsureCelPostProcessResources(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    if (gCelPostProgram == 0)
    {
        const char* vertexSource =
            "attribute vec2 position;\n"
            "varying vec2 texcoord;\n"
            "void main() {\n"
            "    texcoord = position * 0.5 + 0.5;\n"
            "    gl_Position = vec4(position, 0.0, 1.0);\n"
            "}\n";

        const char* fragmentSource =
            "precision mediump float;\n"
            "uniform sampler2D sceneTex;\n"
            "uniform sampler2D bloomTex;\n"
            "uniform float bloomStrength;\n"
            "uniform sampler2D depthTex;\n"
            "uniform float depthTextureAvailable;\n"
            "uniform vec2 texelSize;\n"
            "varying vec2 texcoord;\n"
            "\n"
            "float sceneLuma(vec3 colour) {\n"
            "    return dot(colour, vec3(0.299, 0.587, 0.114));\n"
            "}\n"
            "\n"
            "float randomNoise(vec2 p) {\n"
            "    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;\n"
            "}\n"
            "\n"
            "void main() {\n"
            "    vec4 scene = texture2D(sceneTex, texcoord);\n"
            "\n"
            "    // Existing screen-space cel outline detection.\n"
            "    float centreLuma = sceneLuma(scene.rgb);\n"
            "    float leftLuma  = sceneLuma(texture2D(sceneTex, texcoord - vec2(texelSize.x, 0.0)).rgb);\n"
            "    float rightLuma = sceneLuma(texture2D(sceneTex, texcoord + vec2(texelSize.x, 0.0)).rgb);\n"
            "    float upLuma    = sceneLuma(texture2D(sceneTex, texcoord + vec2(0.0, texelSize.y)).rgb);\n"
            "    float downLuma  = sceneLuma(texture2D(sceneTex, texcoord - vec2(0.0, texelSize.y)).rgb);\n"
            "    float edgeStrength = max(max(abs(centreLuma - leftLuma), abs(centreLuma - rightLuma)),\n"
            "                             max(abs(centreLuma - upLuma), abs(centreLuma - downLuma)));\n"
            "    float edge = smoothstep(0.10, 0.22, edgeStrength);\n"
            "    // Fade only this fullscreen edge-darkening pass with scene depth.\n"
            "    // The separate character/vehicle outline renderer is not modified.\n"
            "    if (depthTextureAvailable > 0.5) {\n"
            "        float sceneDepth = texture2D(depthTex, texcoord).r;\n"
            "        float distanceFade = smoothstep(0.985, 0.998, sceneDepth);\n"
            "        edge *= mix(1.0, 0.30, distanceFade);\n"
            "    }\n"
            "\n"
            "    // Lightweight edge smoothing: soften high-contrast edges only,\n"
            "    // avoiding the heavier full-screen FXAA experiment.\n"
            "    vec3 neighbourAverage = (scene.rgb +\n"
            "        texture2D(sceneTex, texcoord - vec2(texelSize.x, 0.0)).rgb +\n"
            "        texture2D(sceneTex, texcoord + vec2(texelSize.x, 0.0)).rgb +\n"
            "        texture2D(sceneTex, texcoord - vec2(0.0, texelSize.y)).rgb +\n"
            "        texture2D(sceneTex, texcoord + vec2(0.0, texelSize.y)).rgb) * 0.2;\n"
            "    float smoothAmount = 0.12 * smoothstep(0.025, 0.14, edgeStrength);\n"
            "    vec3 toonColour = mix(scene.rgb, neighbourAverage, smoothAmount);\n"
            "\n"
            "    // Retain the existing full-scene toon edge darkening.\n"
            "    toonColour = mix(toonColour, toonColour * 0.35, edge);\n"
            "\n"
            "\n"
            "    // Add the blurred bright-pass texture produced by the multi-pass bloom pipeline.\n"
            "    vec3 bloomGlow = texture2D(bloomTex, texcoord).rgb;\n"
            "    toonColour += bloomGlow * bloomStrength;\n"
            "\n"
            "    // Retain the existing screen-space sunlight/glare on top of the bloom.\n"
            "    float bright = max(centreLuma - 0.78, 0.0);\n"
            "    bright = max(bright, max(max(leftLuma, rightLuma), max(upLuma, downLuma)) - 0.78);\n"
            "    bright = max(bright, 0.0);\n"
            "    float sunRegion = 1.0 - smoothstep(0.18, 0.70, distance(texcoord, vec2(0.5, 0.82)));\n"
            "    float glare = bright * sunRegion * 0.10;\n"
            "    toonColour += vec3(glare, glare * 0.95, glare * 0.82);\n"
            "\n"
            "    // Requested colour grade: +7%% saturation, +2%% contrast, neutral brightness.\n"
            "    float gradedLuma = sceneLuma(toonColour);\n"
            "    toonColour = mix(vec3(gradedLuma), toonColour, 1.07);\n"
            "    toonColour = (toonColour - vec3(0.5)) * 1.02 + vec3(0.5);\n"
            "\n"
            "    // Very small dither to reduce visible gradient/band stepping.\n"
            "    toonColour += vec3(randomNoise(texcoord) / 255.0);\n"
            "\n"
            "\n"
            "    gl_FragColor = vec4(clamp(toonColour, 0.0, 1.0), scene.a);\n"
            "}\n";

        GLuint vs = CompileCelPostShader(GL_VERTEX_SHADER, vertexSource);
        GLuint fs = CompileCelPostShader(GL_FRAGMENT_SHADER, fragmentSource);
        if (vs == 0 || fs == 0)
        {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            return false;
        }

        gCelPostProgram = glCreateProgram();
        glBindAttribLocation(gCelPostProgram, 0, "position");
        glAttachShader(gCelPostProgram, vs);
        glAttachShader(gCelPostProgram, fs);
        glLinkProgram(gCelPostProgram);

        GLint linked = GL_FALSE;
        glGetProgramiv(gCelPostProgram, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE)
        {
            GLint length = 0;
            glGetProgramiv(gCelPostProgram, GL_INFO_LOG_LENGTH, &length);
            if (length > 0)
            {
                std::vector<char> log((size_t)length + 1, 0);
                glGetProgramInfoLog(gCelPostProgram, length, NULL, log.data());
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cel post-process program link failed: %s", log.data());
            }

            glDeleteProgram(gCelPostProgram);
            gCelPostProgram = 0;
            glDeleteShader(vs);
            glDeleteShader(fs);
            return false;
        }

        glDeleteShader(vs);
        glDeleteShader(fs);

        gCelPostSceneLocation = glGetUniformLocation(gCelPostProgram, "sceneTex");
        gCelPostTexelLocation = glGetUniformLocation(gCelPostProgram, "texelSize");
        gCelPostBloomLocation = glGetUniformLocation(gCelPostProgram, "bloomTex");
        gCelPostBloomStrengthLocation = glGetUniformLocation(gCelPostProgram, "bloomStrength");
        gCelPostDepthLocation = glGetUniformLocation(gCelPostProgram, "depthTex");
        gCelPostDepthAvailableLocation = glGetUniformLocation(gCelPostProgram, "depthTextureAvailable");

        glGenTextures(1, &gCelPostTexture);

        static const GLfloat quad[] =
        {
            -1.0f, -1.0f,
             1.0f, -1.0f,
            -1.0f,  1.0f,
             1.0f,  1.0f
        };
        glGenBuffers(1, &gCelPostVbo);
        glBindBuffer(GL_ARRAY_BUFFER, gCelPostVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    if (gCelPostWidth != width || gCelPostHeight != height)
    {
        glBindTexture(GL_TEXTURE_2D, gCelPostTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            width,
            height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            NULL
        );

        gCelPostWidth = width;
        gCelPostHeight = height;
    }

    return gCelPostProgram != 0 && gCelPostTexture != 0;
}

static bool EnsureCelBloomResources(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    const int bloomWidth = width > 3 ? width / 4 : 1;
    const int bloomHeight = height > 3 ? height / 4 : 1;

    if (gCelBloomReady && gCelBloomWidth == bloomWidth && gCelBloomHeight == bloomHeight)
        return true;
    if (gCelBloomFailedWidth == bloomWidth && gCelBloomFailedHeight == bloomHeight)
        return false;

    static const char* vertexSource =
        "attribute vec2 position;\n"
        "varying vec2 texcoord;\n"
        "void main() {\n"
        "    texcoord = position * 0.5 + 0.5;\n"
        "    gl_Position = vec4(position, 0.0, 1.0);\n"
        "}\n";

    if (gCelBloomExtractProgram == 0)
    {
        const char* extractFragment =
            "precision mediump float;\n"
            "uniform sampler2D sceneTex;\n"
            "uniform vec2 sourceTexelSize;\n"
            "varying vec2 texcoord;\n"
            "float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
            "void main() {\n"
            "    vec2 d = sourceTexelSize * 2.0;\n"
            "    vec3 c0 = texture2D(sceneTex, texcoord + vec2(-d.x, -d.y)).rgb;\n"
            "    vec3 c1 = texture2D(sceneTex, texcoord + vec2( d.x, -d.y)).rgb;\n"
            "    vec3 c2 = texture2D(sceneTex, texcoord + vec2(-d.x,  d.y)).rgb;\n"
            "    vec3 c3 = texture2D(sceneTex, texcoord + vec2( d.x,  d.y)).rgb;\n"
            "    vec3 c = (c0 + c1 + c2 + c3) * 0.25;\n"
            "    float brightness = luma(c);\n"
            "    float contribution = clamp((brightness - 0.68) / max(brightness, 0.001), 0.0, 1.0);\n"
            "    gl_FragColor = vec4(c * contribution, 1.0);\n"
            "}\n";

        gCelBloomExtractProgram = CreateCelPostProgram(vertexSource, extractFragment, "Cel bloom bright-pass");
        if (gCelBloomExtractProgram == 0)
            return false;

        gCelBloomExtractSceneLocation = glGetUniformLocation(gCelBloomExtractProgram, "sceneTex");
        gCelBloomExtractTexelLocation = glGetUniformLocation(gCelBloomExtractProgram, "sourceTexelSize");
    }

    if (gCelBloomBlurProgram == 0)
    {
        const char* blurFragment =
            "precision mediump float;\n"
            "uniform sampler2D sourceTex;\n"
            "uniform vec2 blurStep;\n"
            "varying vec2 texcoord;\n"
            "void main() {\n"
            "    vec3 c = texture2D(sourceTex, texcoord).rgb * 0.19648255;\n"
            "    c += texture2D(sourceTex, texcoord + blurStep).rgb * 0.29690696;\n"
            "    c += texture2D(sourceTex, texcoord - blurStep).rgb * 0.29690696;\n"
            "    c += texture2D(sourceTex, texcoord + blurStep * 2.0).rgb * 0.09447040;\n"
            "    c += texture2D(sourceTex, texcoord - blurStep * 2.0).rgb * 0.09447040;\n"
            "    c += texture2D(sourceTex, texcoord + blurStep * 3.0).rgb * 0.01038136;\n"
            "    c += texture2D(sourceTex, texcoord - blurStep * 3.0).rgb * 0.01038136;\n"
            "    gl_FragColor = vec4(c, 1.0);\n"
            "}\n";

        gCelBloomBlurProgram = CreateCelPostProgram(vertexSource, blurFragment, "Cel bloom Gaussian blur");
        if (gCelBloomBlurProgram == 0)
            return false;

        gCelBloomBlurSourceLocation = glGetUniformLocation(gCelBloomBlurProgram, "sourceTex");
        gCelBloomBlurStepLocation = glGetUniformLocation(gCelBloomBlurProgram, "blurStep");
    }

    if (gCelBloomFbo == 0)
        glGenFramebuffers(1, &gCelBloomFbo);
    if (gCelBloomTexture == 0)
        glGenTextures(1, &gCelBloomTexture);
    if (gCelBloomScratchTexture == 0)
        glGenTextures(1, &gCelBloomScratchTexture);

    if (gCelBloomFbo == 0 || gCelBloomTexture == 0 || gCelBloomScratchTexture == 0)
        return false;

    GLint previousFramebuffer = 0;
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

    if (gCelBloomWidth != bloomWidth || gCelBloomHeight != bloomHeight)
    {
        glBindTexture(GL_TEXTURE_2D, gCelBloomTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bloomWidth, bloomHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        glBindTexture(GL_TEXTURE_2D, gCelBloomScratchTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bloomWidth, bloomHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        gCelBloomWidth = bloomWidth;
        gCelBloomHeight = bloomHeight;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, gCelBloomFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gCelBloomTexture, 0);
    GLenum statusA = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gCelBloomScratchTexture, 0);
    GLenum statusB = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    gCelBloomReady = (statusA == GL_FRAMEBUFFER_COMPLETE && statusB == GL_FRAMEBUFFER_COMPLETE);
    if (!gCelBloomReady)
    {
        gCelBloomFailedWidth = bloomWidth;
        gCelBloomFailedHeight = bloomHeight;
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
            "SHAR Android bloom framebuffer incomplete: textureA=0x%04x textureB=0x%04x",
            (unsigned)statusA, (unsigned)statusB);
    }
    else
    {
        gCelBloomFailedWidth = 0;
        gCelBloomFailedHeight = 0;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    glActiveTexture((GLenum)previousActiveTexture);
    return gCelBloomReady;
}

static void DrawCelPostFullscreenQuad()
{
    glBindVertexArrayOES(0);
    glBindBuffer(GL_ARRAY_BUFFER, gCelPostVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void*)0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void ApplyCelPostProcess(int width, int height)
{
    if (!IsCelShadingEnabled())
        return;

    GLint currentFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFramebuffer);
    if ((GLuint)currentFramebuffer != gCelRenderFbo || !gCelRenderFboReady)
        return;

    if (!EnsureCelPostProcessResources(width, height))
        return;

    const bool bloomReady = EnsureCelBloomResources(width, height);

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean stencilEnabled = glIsEnabled(GL_STENCIL_TEST);
    GLint previousProgram = 0;
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture2D = 0;
    GLint previousTextureUnit0 = 0;
    GLint previousTextureUnit1 = 0;
    GLint previousTextureUnit2 = 0;
    GLint previousArrayBuffer = 0;
    GLint previousVao = 0;
    GLint previousAttrib0Enabled = GL_FALSE;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture2D);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureUnit0);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureUnit1);
    glActiveTexture(GL_TEXTURE2);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureUnit2);
    glActiveTexture((GLenum)previousActiveTexture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING_OES, &previousVao);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &previousAttrib0Enabled);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);

    if (bloomReady)
    {
        // Pass 1: downsample the scene and extract only pixels above the bright threshold.
        glBindFramebuffer(GL_FRAMEBUFFER, gCelBloomFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gCelBloomTexture, 0);
        glViewport(0, 0, gCelBloomWidth, gCelBloomHeight);
        glUseProgram(gCelBloomExtractProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gCelPostTexture);
        glUniform1i(gCelBloomExtractSceneLocation, 0);
        if (gCelBloomExtractTexelLocation >= 0)
            glUniform2f(gCelBloomExtractTexelLocation, 1.0f / (float)width, 1.0f / (float)height);
        DrawCelPostFullscreenQuad();

        // Pass 2: horizontal Gaussian blur into a separate reduced-resolution texture.
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gCelBloomScratchTexture, 0);
        glUseProgram(gCelBloomBlurProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gCelBloomTexture);
        glUniform1i(gCelBloomBlurSourceLocation, 0);
        if (gCelBloomBlurStepLocation >= 0)
            glUniform2f(gCelBloomBlurStepLocation, 1.0f / (float)gCelBloomWidth, 0.0f);
        DrawCelPostFullscreenQuad();

        // Pass 3: vertical Gaussian blur back into the bloom texture.
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gCelBloomTexture, 0);
        glUseProgram(gCelBloomBlurProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gCelBloomScratchTexture);
        glUniform1i(gCelBloomBlurSourceLocation, 0);
        if (gCelBloomBlurStepLocation >= 0)
            glUniform2f(gCelBloomBlurStepLocation, 0.0f, 1.0f / (float)gCelBloomHeight);
        DrawCelPostFullscreenQuad();
    }

    // Final pass: combine scene, bloom and distance-aware screen-space outlines.
    // If depth textures are unavailable, bloom and the rest of post-processing continue normally.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glUseProgram(gCelPostProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gCelPostTexture);
    glUniform1i(gCelPostSceneLocation, 0);
    if (gCelPostTexelLocation >= 0)
        glUniform2f(gCelPostTexelLocation, 1.0f / (float)width, 1.0f / (float)height);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomReady ? gCelBloomTexture : 0);
    if (gCelPostBloomLocation >= 0)
        glUniform1i(gCelPostBloomLocation, 1);
    if (gCelPostBloomStrengthLocation >= 0)
        glUniform1f(gCelPostBloomStrengthLocation, bloomReady ? 1.30f : 0.0f);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gCelDepthTextureActive ? gCelRenderDepthTexture : gCelPostTexture);
    if (gCelPostDepthLocation >= 0)
        glUniform1i(gCelPostDepthLocation, 2);
    if (gCelPostDepthAvailableLocation >= 0)
        glUniform1f(gCelPostDepthAvailableLocation, gCelDepthTextureActive ? 1.0f : 0.0f);

    glActiveTexture(GL_TEXTURE0);
    DrawCelPostFullscreenQuad();

    if (previousAttrib0Enabled)
        glEnableVertexAttribArray(0);
    else
        glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previousArrayBuffer);
    glBindVertexArrayOES((GLuint)previousVao);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTextureUnit0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTextureUnit1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTextureUnit2);
    glActiveTexture((GLenum)previousActiveTexture);
    if (previousActiveTexture != GL_TEXTURE0 &&
        previousActiveTexture != GL_TEXTURE1 &&
        previousActiveTexture != GL_TEXTURE2)
    {
        glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture2D);
    }

    glUseProgram((GLuint)previousProgram);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    if (depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (scissorEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (stencilEnabled) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);

    // Leave framebuffer 0 bound so SDL_GL_SwapWindow presents the processed scene.
}

}

bool BeginCelPostProcessFrame(int width, int height)
{
    if (!IsCelShadingEnabled())
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    if (!BindCelRenderTarget(width, height))
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    return true;
}

void ApplyCelPostProcessBeforeGui(int width, int height)
{
    /*
     * The game/world layers have finished rendering at this point, while
     * the dedicated GUI layer has not started yet. Apply the screen-space
     * cel pass here so GUI and touch overlays are drawn afterward without
     * being processed.
     */
    ApplyCelPostProcess(width, height);
}


static void ApplyFrameSharpening(int width, int height)
{
    const float strength = GetFrameSharpenStrength();
    if (strength <= 0.001f || width <= 0 || height <= 0)
        return;

    if (gFrameSharpenProgram == 0)
    {
        const char* vertexSource =
            "attribute vec2 position;\n"
            "varying vec2 texcoord;\n"
            "void main() { texcoord = position * 0.5 + 0.5; gl_Position = vec4(position, 0.0, 1.0); }\n";
        const char* fragmentSource =
            "precision mediump float;\n"
            "uniform sampler2D frameTex; uniform vec2 texelSize; uniform float sharpen;\n"
            "varying vec2 texcoord;\n"
            "void main() {\n"
            "vec3 e=texture2D(frameTex,texcoord).rgb;\n"
            "vec3 a=texture2D(frameTex,texcoord+vec2(0.0,-texelSize.y)).rgb;\n"
            "vec3 b=texture2D(frameTex,texcoord+vec2(-texelSize.x,0.0)).rgb;\n"
            "vec3 d=texture2D(frameTex,texcoord+vec2(texelSize.x,0.0)).rgb;\n"
            "vec3 f=texture2D(frameTex,texcoord+vec2(0.0,texelSize.y)).rgb;\n"
            "vec3 mn=min(min(min(a,b),min(d,f)),e); vec3 mx=max(max(max(a,b),max(d,f)),e);\n"
            "vec3 amp=sqrt(clamp(min(mn,1.0-mx)/max(mx,vec3(0.001)),0.0,1.0));\n"
            "float s=clamp(sharpen,0.0,1.0); vec3 w=amp*(-s/mix(8.0,5.0,s));\n"
            "vec3 result=(w*(a+b+d+f)+e)/(1.0+4.0*w); gl_FragColor=vec4(clamp(result,0.0,1.0),1.0); }\n";
        gFrameSharpenProgram = CreateCelPostProgram(vertexSource, fragmentSource, "Full-frame adaptive sharpening");
        if (gFrameSharpenProgram != 0)
        {
            gFrameSharpenSceneLocation = glGetUniformLocation(gFrameSharpenProgram, "frameTex");
            gFrameSharpenTexelLocation = glGetUniformLocation(gFrameSharpenProgram, "texelSize");
            gFrameSharpenStrengthLocation = glGetUniformLocation(gFrameSharpenProgram, "sharpen");
            const GLfloat quad[] = {-1.0f,-1.0f, 1.0f,-1.0f, -1.0f,1.0f, 1.0f,1.0f};
            glGenBuffers(1, &gFrameSharpenVbo);
            glBindBuffer(GL_ARRAY_BUFFER, gFrameSharpenVbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glGenTextures(1, &gFrameSharpenTexture);
        }
    }
    if (gFrameSharpenProgram == 0 || gFrameSharpenTexture == 0 || gFrameSharpenVbo == 0)
        return;

    GLint oldFbo=0, oldViewport[4]={0,0,width,height}, oldProgram=0, oldActive=GL_TEXTURE0;
    GLint oldTexture=0, oldTexture0=0, oldArray=0, oldVao=0, oldAttrib=GL_FALSE;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&oldFbo);
    glGetIntegerv(GL_VIEWPORT,oldViewport);
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture0);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldArray);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING_OES,&oldVao);
    glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&oldAttrib);
    const GLboolean depth=glIsEnabled(GL_DEPTH_TEST), blend=glIsEnabled(GL_BLEND);
    const GLboolean cull=glIsEnabled(GL_CULL_FACE), scissor=glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean stencil=glIsEnabled(GL_STENCIL_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,gFrameSharpenTexture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    if (gFrameSharpenWidth!=width || gFrameSharpenHeight!=height)
    {
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);
        gFrameSharpenWidth=width;
        gFrameSharpenHeight=height;
    }
    // Snapshot the fully rendered frame before drawing the filtered copy back.
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);
    glViewport(0,0,width,height);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
    glUseProgram(gFrameSharpenProgram);
    glUniform1i(gFrameSharpenSceneLocation,0);
    if (gFrameSharpenTexelLocation>=0) glUniform2f(gFrameSharpenTexelLocation,1.0f/(float)width,1.0f/(float)height);
    if (gFrameSharpenStrengthLocation>=0) glUniform1f(gFrameSharpenStrengthLocation,strength);
    glBindBuffer(GL_ARRAY_BUFFER,gFrameSharpenVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,(const GLvoid*)0);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);

    if (oldAttrib) glEnableVertexAttribArray(0); else glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)oldArray);
    glBindVertexArrayOES((GLuint)oldVao);
    glUseProgram((GLuint)oldProgram);
    glViewport(oldViewport[0],oldViewport[1],oldViewport[2],oldViewport[3]);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,(GLuint)oldTexture0);
    glActiveTexture((GLenum)oldActive);
    if (oldActive!=GL_TEXTURE0) glBindTexture(GL_TEXTURE_2D,(GLuint)oldTexture);
    if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)oldFbo);
}


static void ApplyCRTFilter(int width, int height)
{
    bool enabled = false;
    float scanlineStrength = 0.35f, curvature = 0.15f, glowStrength = 0.10f;
    float cornerDarkening = 0.05f, overscan = 0.0f, brightness = 1.0f, contrast = 1.0f;
    GetCRTFilterConfiguration(&enabled, &scanlineStrength, &curvature, &glowStrength,
                              &cornerDarkening, &overscan, &brightness, &contrast);
    // When disabled, return before allocating resources or issuing any CRT draw calls.
    if (!enabled || width <= 0 || height <= 0)
        return;

    if (gCRTProgram == 0)
    {
        const char* vertexSource =
            "attribute vec2 position; varying vec2 texcoord;\n"
            "void main() { texcoord = position * 0.5 + 0.5; gl_Position = vec4(position, 0.0, 1.0); }\n";
        // Lightweight GLES2 CRT-Geom-inspired pass: scanlines, barrel curvature,
        // restrained highlight glow, corner shading, and user-controlled levels.
        const char* fragmentSource =
            "precision mediump float;\n"
            "uniform sampler2D frameTex; uniform vec2 texelSize;\n"
            "uniform float scanlineStrength; uniform float curvature; uniform float glowStrength;\n"
            "uniform float cornerDarkening; uniform float overscan; uniform float brightness; uniform float contrast;\n"
            "varying vec2 texcoord;\n"
            "void main() {\n"
            " vec2 uv=(texcoord-0.5)*(1.0+clamp(overscan,0.0,0.05)*2.0)+0.5;\n"
            " vec2 p=uv*2.0-1.0; float curve=clamp(curvature,0.0,0.5);\n"
            " uv=(p*(1.0+curve*vec2(p.y*p.y,p.x*p.x)))*0.5+0.5;\n"
            " if(uv.x<0.0||uv.x>1.0||uv.y<0.0||uv.y>1.0){ gl_FragColor=vec4(0.0,0.0,0.0,1.0); return; }\n"
            " vec3 color=texture2D(frameTex,uv).rgb;\n"
            " vec3 glow=(texture2D(frameTex,uv+vec2(texelSize.x*1.5,0.0)).rgb+texture2D(frameTex,uv-vec2(texelSize.x*1.5,0.0)).rgb+texture2D(frameTex,uv+vec2(0.0,texelSize.y*1.5)).rgb+texture2D(frameTex,uv-vec2(0.0,texelSize.y*1.5)).rgb)*0.25;\n"
            " color+=max(glow-vec3(0.68),vec3(0.0))*clamp(glowStrength,0.0,1.0)*0.35;\n"
            " float scan=0.5+0.5*cos((uv.y/texelSize.y)*3.14159265);\n"
            " color*=1.0-clamp(scanlineStrength,0.0,1.0)*(0.22*(1.0-scan));\n"
            " float edge=clamp(length((uv*2.0-1.0)*vec2(0.82,1.0)),0.0,1.4);\n"
            " color*=1.0-clamp(cornerDarkening,0.0,1.0)*smoothstep(0.55,1.15,edge);\n"
            " color=(color-vec3(0.5))*clamp(contrast,0.5,1.5)+vec3(0.5);\n"
            " color*=clamp(brightness,0.5,1.5); gl_FragColor=vec4(clamp(color,0.0,1.0),1.0);\n"
            "}\n";
        gCRTProgram = CreateCelPostProgram(vertexSource, fragmentSource, "CRT-Geom-style fullscreen filter");
        if (gCRTProgram != 0)
        {
            gCRTSceneLocation = glGetUniformLocation(gCRTProgram, "frameTex");
            gCRTTexelLocation = glGetUniformLocation(gCRTProgram, "texelSize");
            gCRTScanlineLocation = glGetUniformLocation(gCRTProgram, "scanlineStrength");
            gCRTCurvatureLocation = glGetUniformLocation(gCRTProgram, "curvature");
            gCRTGlowLocation = glGetUniformLocation(gCRTProgram, "glowStrength");
            gCRTCornersLocation = glGetUniformLocation(gCRTProgram, "cornerDarkening");
            gCRTOverscanLocation = glGetUniformLocation(gCRTProgram, "overscan");
            gCRTBrightnessLocation = glGetUniformLocation(gCRTProgram, "brightness");
            gCRTContrastLocation = glGetUniformLocation(gCRTProgram, "contrast");
            const GLfloat quad[] = {-1.0f,-1.0f, 1.0f,-1.0f, -1.0f,1.0f, 1.0f,1.0f};
            glGenBuffers(1, &gCRTVbo);
            glBindBuffer(GL_ARRAY_BUFFER, gCRTVbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glGenTextures(1, &gCRTTexture);
        }
    }
    if (gCRTProgram == 0 || gCRTTexture == 0 || gCRTVbo == 0)
        return;

    GLint oldFbo=0, oldViewport[4]={0,0,width,height}, oldProgram=0, oldActive=GL_TEXTURE0;
    GLint oldTexture=0, oldTexture0=0, oldArray=0, oldVao=0, oldAttrib=GL_FALSE;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&oldFbo);
    glGetIntegerv(GL_VIEWPORT,oldViewport);
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture0);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldArray);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING_OES,&oldVao);
    glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&oldAttrib);
    const GLboolean depth=glIsEnabled(GL_DEPTH_TEST), blend=glIsEnabled(GL_BLEND);
    const GLboolean cull=glIsEnabled(GL_CULL_FACE), scissor=glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean stencil=glIsEnabled(GL_STENCIL_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,gCRTTexture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    if (gCRTWidth!=width || gCRTHeight!=height)
    {
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);
        gCRTWidth=width;
        gCRTHeight=height;
    }
    // Capture the frame after sharpening, then apply CRT processing to the whole image.
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);
    glViewport(0,0,width,height);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
    glUseProgram(gCRTProgram);
    glUniform1i(gCRTSceneLocation,0);
    if (gCRTTexelLocation>=0) glUniform2f(gCRTTexelLocation,1.0f/(float)width,1.0f/(float)height);
    if (gCRTScanlineLocation>=0) glUniform1f(gCRTScanlineLocation,scanlineStrength);
    if (gCRTCurvatureLocation>=0) glUniform1f(gCRTCurvatureLocation,curvature);
    if (gCRTGlowLocation>=0) glUniform1f(gCRTGlowLocation,glowStrength);
    if (gCRTCornersLocation>=0) glUniform1f(gCRTCornersLocation,cornerDarkening);
    if (gCRTOverscanLocation>=0) glUniform1f(gCRTOverscanLocation,overscan);
    if (gCRTBrightnessLocation>=0) glUniform1f(gCRTBrightnessLocation,brightness);
    if (gCRTContrastLocation>=0) glUniform1f(gCRTContrastLocation,contrast);
    glBindBuffer(GL_ARRAY_BUFFER,gCRTVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,(const GLvoid*)0);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);

    if (oldAttrib) glEnableVertexAttribArray(0); else glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)oldArray);
    glBindVertexArrayOES((GLuint)oldVao);
    glUseProgram((GLuint)oldProgram);
    glViewport(oldViewport[0],oldViewport[1],oldViewport[2],oldViewport[3]);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,(GLuint)oldTexture0);
    glActiveTexture((GLenum)oldActive);
    if (oldActive!=GL_TEXTURE0) glBindTexture(GL_TEXTURE_2D,(GLuint)oldTexture);
    if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)oldFbo);
}


static int gSHARAndroidRenderWidth = 0;
static int gSHARAndroidRenderHeight = 0;

static int gSHARLastLoggedDisplayWidth = 0;
static int gSHARLastLoggedDisplayHeight = 0;

extern "C" JNIEXPORT void JNICALL
Java_org_libsdl_app_SDLSurface_nativeSetSHARRenderResolution(
    JNIEnv* env,
    jclass clazz,
    jint width,
    jint height
)
{
    if (width <= 0 || height <= 0)
        return;

    const bool changed =
        gSHARAndroidRenderWidth != (int)width ||
        gSHARAndroidRenderHeight != (int)height;

    gSHARAndroidRenderWidth = (int)width;
    gSHARAndroidRenderHeight = (int)height;

    if (changed) {
        SDL_Log(
            "SHAR Android Java render resolution received: %dx%d",
            gSHARAndroidRenderWidth,
            gSHARAndroidRenderHeight
        );
    }
}

static bool
GetSHARAndroidRenderResolution(int* width, int* height)
{
    if (width == NULL || height == NULL)
        return false;

    if (gSHARAndroidRenderWidth <= 0 || gSHARAndroidRenderHeight <= 0)
        return false;

    *width = gSHARAndroidRenderWidth;
    *height = gSHARAndroidRenderHeight;

    return true;
}

static void
LogSHARDisplayResolutionIfChanged(const char* reason, int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    if (gSHARLastLoggedDisplayWidth == width &&
        gSHARLastLoggedDisplayHeight == height)
        return;

    gSHARLastLoggedDisplayWidth = width;
    gSHARLastLoggedDisplayHeight = height;

    SDL_Log(
        "SHAR Android PDDI display resolution [%s]: %dx%d",
        reason ? reason : "unknown",
        width,
        height
    );
}
#endif

bool pglDisplay::CheckExtension( const char *extName )
{
    return SDL_GL_ExtensionSupported(extName);
}

pglDisplay ::pglDisplay(pddiDisplayInfo* info)
{
    displayInfo = info;
    mode = PDDI_DISPLAY_WINDOW;
    winWidth = 640;
    winHeight = 480;
    winBitDepth = 32;

    context = NULL;

    win = NULL;
    hRC = NULL;
    prevRC = NULL;

    extBGRA = false;

    gammaR = gammaG = gammaB = 1.0f;

    reset = true;
	m_ForceVSync = false;
}

pglDisplay ::~pglDisplay()
{
#ifdef RAD_ANDROID
    if (gCelRenderDepth)
    {
        glDeleteRenderbuffers(1, &gCelRenderDepth);
        gCelRenderDepth = 0;
    }
    if (gCelRenderDepthTexture)
    {
        glDeleteTextures(1, &gCelRenderDepthTexture);
        gCelRenderDepthTexture = 0;
    }
    if (gCelRenderFbo)
    {
        glDeleteFramebuffers(1, &gCelRenderFbo);
        gCelRenderFbo = 0;
    }
    if (gCelBloomFbo)
    {
        glDeleteFramebuffers(1, &gCelBloomFbo);
        gCelBloomFbo = 0;
    }
    if (gCelBloomTexture)
    {
        glDeleteTextures(1, &gCelBloomTexture);
        gCelBloomTexture = 0;
    }
    if (gCelBloomScratchTexture)
    {
        glDeleteTextures(1, &gCelBloomScratchTexture);
        gCelBloomScratchTexture = 0;
    }
    if (gCelBloomExtractProgram)
    {
        glDeleteProgram(gCelBloomExtractProgram);
        gCelBloomExtractProgram = 0;
    }
    if (gCelBloomBlurProgram)
    {
        glDeleteProgram(gCelBloomBlurProgram);
        gCelBloomBlurProgram = 0;
    }
    gCelBloomReady = false;
#endif

    /* release and free the device context and rendering context */
#if SDL_MAJOR_VERSION < 3
    SDL_GL_DeleteContext(hRC);
    SDL_SetWindowGammaRamp(win, initialGammaRamp[0], initialGammaRamp[1], initialGammaRamp[2]);
#else
    SDL_GL_DestroyContext((SDL_GLContext)hRC);
#endif
}

#define KEYPRESSED(x) (GetKeyState((x)) & (1<<(sizeof(int)*8)-1))

long pglDisplay ::ProcessWindowMessage(SDL_Window* win, const SDL_WindowEvent* event)
{
#if SDL_MAJOR_VERSION < 3
    switch(event->event)
    {
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        {
#if defined(RAD_ANDROID)
            int renderW = 0;
            int renderH = 0;

            /*
             * On Android, Java/SDLSurface is the source of truth for the
             * internal render resolution. Do not let SDL report the physical
             * drawable size as the PDDI render size.
             */
            if (GetSHARAndroidRenderResolution(&renderW, &renderH)) {
                winWidth = renderW;
                winHeight = renderH;

                LogSHARDisplayResolutionIfChanged(
                    "window-size-changed-java",
                    winWidth,
                    winHeight
                );
            } else
#endif
            {
                SDL_GL_GetDrawableSize(win, &winWidth, &winHeight);

#if defined(RAD_ANDROID)
                LogSHARDisplayResolutionIfChanged(
                    "window-size-changed-sdl-fallback",
                    winWidth,
                    winHeight
                );
#endif
            }
            break;
        }

        case SDL_WINDOWEVENT_CLOSE:
            /* release and free the device context and rendering context */
            SDL_GL_DeleteContext(hRC);
            break;

        default:
            return 0;
    }
#else
    switch(event->type)
    {
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        {
#if defined(RAD_ANDROID)
            int renderW = 0;
            int renderH = 0;

            /*
             * On Android, Java/SDLSurface is the source of truth for the
             * internal render resolution. Do not let SDL report the physical
             * pixel size as the PDDI render size.
             */
            if (GetSHARAndroidRenderResolution(&renderW, &renderH)) {
                winWidth = renderW;
                winHeight = renderH;

                LogSHARDisplayResolutionIfChanged(
                    "window-pixel-size-changed-java",
                    winWidth,
                    winHeight
                );
            } else
#endif
            {
                SDL_GetWindowSizeInPixels(win, &winWidth, &winHeight);

#if defined(RAD_ANDROID)
                LogSHARDisplayResolutionIfChanged(
                    "window-pixel-size-changed-sdl-fallback",
                    winWidth,
                    winHeight
                );
#endif
            }
            break;
        }

        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            /* release and free the device context and rendering context */
            SDL_GL_DestroyContext((SDL_GLContext)hRC);
            break;

        default:
            return 0;
    }
#endif

    /* return 1 if handled message, 0 if not */
    return 1;
}

void pglDisplay ::SetWindow(SDL_Window* wnd)
{
#if SDL_MAJOR_VERSION < 3
    SDL_GetWindowGammaRamp(wnd, initialGammaRamp[0], initialGammaRamp[1], initialGammaRamp[2]);
#endif
    win = wnd;
}

bool pglDisplay ::InitDisplay(int x, int y, int bpp)
{
    // check we are not trying to init to the same resolution
    if((x == winWidth) &&  (y == winHeight) && (bpp == winBitDepth))
    {
        return true;
    }

    // fill in the relevent portions of the casced display init structure
    displayInit.xsize = x;
    displayInit.ysize = y;
    displayInit.bpp = bpp;

    // do the full init
    return InitDisplay(&displayInit);
}

#ifdef RAD_DEBUG
void GLAPIENTRY
MessageCallback(GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei length,
    const GLchar* message,
    const void* userParam)
{
    switch(severity)
    {
        case GL_DEBUG_SEVERITY_HIGH_KHR:
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
            break;
        case GL_DEBUG_SEVERITY_MEDIUM_KHR:
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
            break;
        case GL_DEBUG_SEVERITY_LOW_KHR:
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
            break;
        case GL_DEBUG_SEVERITY_NOTIFICATION_KHR:
            SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
            break;
    }
}
#endif

bool pglDisplay ::InitDisplay(const pddiDisplayInit* init)
{
    displayInit = *init;

    int x = init->xsize;
    int y = init->ysize;
    int bpp = init->bpp;
    pddiDisplayMode m = init->displayMode;
    int colourBufferCount = 1;
    unsigned bufMask = init->bufferMask;
    unsigned nSamples = 0;

    reset = true;

    mode = m;
    SDL_DisplayMode displayMode = {}, closestMode = {};
    displayMode.w = x;
    displayMode.h = y;
#if SDL_MAJOR_VERSION < 3
    SDL_DisplayMode* pDisplayMode = SDL_GetClosestDisplayMode(displayInfo->id, &displayMode, &closestMode);
    if (pDisplayMode)
        SDL_SetWindowDisplayMode(win, pDisplayMode);
#else
    if(SDL_GetClosestFullscreenDisplayMode((SDL_DisplayID)displayInfo->id, x, y, 0.0f, false, &closestMode))
        SDL_SetWindowFullscreenMode(win, &closestMode);
#endif

#ifndef __SWITCH__
    SDL_SetWindowFullscreen(win, mode == PDDI_DISPLAY_FULLSCREEN ? SDL_WINDOW_FULLSCREEN : 0);
#endif

#if defined(RAD_ANDROID) || defined(__ANDROID__)
    /*
     * Android adaptive render resolution:
     *
     * SDLSurface.java has already calculated the internal Surface buffer size
     * and sent it through nativeSetSHARRenderResolution().
     *
     * Use that size as PDDI display size so pglContext::SetupHardwareProjection()
     * later uses the adaptive resolution for glViewport/projection/scissor.
     */
    {
        int renderW = 0;
        int renderH = 0;

        if (GetSHARAndroidRenderResolution(&renderW, &renderH)) {
            winWidth = renderW;
            winHeight = renderH;

            LogSHARDisplayResolutionIfChanged(
                "init-display-java",
                winWidth,
                winHeight
            );
        } else {
#if SDL_MAJOR_VERSION < 3
            SDL_GL_GetDrawableSize(win, &winWidth, &winHeight);
#else
            SDL_GetWindowSizeInPixels(win, &winWidth, &winHeight);
#endif
            LogSHARDisplayResolutionIfChanged(
                "init-display-sdl-fallback",
                winWidth,
                winHeight
            );
        }
    }
#else
#if SDL_MAJOR_VERSION < 3
    SDL_GL_GetDrawableSize( win, &winWidth, &winHeight );
#else
    SDL_GetWindowSizeInPixels( win, &winWidth, &winHeight );
#endif
#endif

    winBitDepth = bpp;

    if (hRC)
        return true;

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, bpp == 16 ? 5 : 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, bpp == 16 ? 6 : 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, bpp == 16 ? 5 : 8);
    if (init->bufferMask & PDDI_BUFFER_DEPTH)
#ifdef __SWITCH__
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, init->bufferMask & PDDI_BUFFER_STENCIL ? 24 : 32);
#else
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
#endif
    else
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    if (init->bufferMask & PDDI_BUFFER_STENCIL)
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    else
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
#ifndef RAD_VITA
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, true);
#ifdef RAD_DEBUG
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
#endif

    prevRC = SDL_GL_GetCurrentContext();
    hRC = SDL_GL_CreateContext(win);
    if (!hRC)
        SDL_Log("SDL_GL_CreateContext() error: %s", SDL_GetError());
    PDDIASSERT(hRC);

#ifdef RAD_CG
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
        return false;
#else
    if (!gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress))
        return false;
#endif

    char* glVendor   = (char*)glGetString(GL_VENDOR);
    char* glRenderer = (char*)glGetString(GL_RENDERER);
    char* glVersion  = (char*)glGetString(GL_VERSION);
    char* glExtensions = (char*)glGetString(GL_EXTENSIONS);

    static bool doExtensions = false;

    if(doExtensions)
    {
        doExtensions = false;
        char* buffer = new char[strlen(glExtensions) + 2];
        strcpy(buffer, glExtensions);

        char* walk = buffer;
        char* last = buffer;
        while(*walk)
        {
            if(*walk == ' ')
            {
                *walk = 0;
                SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", last);
                last = walk+1;
            }
            walk++;
        }
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", last);
    }

    extBGRA = CheckExtension("GL_EXT_bgra") || CheckExtension("GL_EXT_texture_format_BGRA8888");

    SDL_Log("OpenGL - Vendor: %s, Renderer: %s, Version: %s",glVendor,glRenderer,glVersion);

#if defined RAD_DEBUG && !defined RAD_VITA
    glEnable(GL_DEBUG_OUTPUT_KHR);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
    glDebugMessageCallback(MessageCallback, NULL);
#endif

    return true;
}

pddiDisplayInfo* pglDisplay ::GetDisplayInfo(void)
{
    return displayInfo;
}

unsigned pglDisplay ::GetFreeTextureMem()
{
    return unsigned(-1);
}

unsigned pglDisplay ::GetBufferMask()
{
    return unsigned(-1);
}

int pglDisplay ::GetHeight()
{
    return winHeight;
}

int pglDisplay ::GetWidth()
{
    return winWidth;
}

int pglDisplay::GetDepth()
{
    return winBitDepth;
}

pddiDisplayMode pglDisplay::GetDisplayMode(void)
{
    return mode;
}

int pglDisplay::GetNumColourBuffer(void)
{
    return 2;
}

void pglDisplay::GetGamma(float* r, float* g, float* b)
{
    *r = gammaR;
    *g = gammaG;
    *b = gammaB;
}

void pglDisplay::SetGamma(float r, float g, float b)
{
    gammaR = r;
    gammaG = g;
    gammaB = b;

    Uint16 gamma[3][256];

    double igr = 1.0 / (double)r;
    double igg = 1.0 / (double)g;
    double igb = 1.0 / (double)b;

    const double n = 1.0 / 65535.0;

    for(int i=0; i < 256; i++)
    {
        double gcr = pow((double)initialGammaRamp[0][i]   * n, igr);
        double gcg = pow((double)initialGammaRamp[1][i] * n, igg);
        double gcb = pow((double)initialGammaRamp[2][i]  * n, igb);

        gamma[0][i] =   (Uint16)(65535.0 * ((1.0 < gcr) ? 1.0 : gcr));
        gamma[1][i] = (Uint16)(65535.0 * ((1.0 < gcg) ? 1.0 : gcg));
        gamma[2][i] =  (Uint16)(65535.0 * ((1.0 < gcb) ? 1.0 : gcb));
    }

#if SDL_MAJOR_VERSION < 3
    SDL_SetWindowGammaRamp(win, gamma[0], gamma[1], gamma[2]);
#endif
}

void pglDisplay::SwapBuffers(void)
{
#ifdef RAD_ANDROID
    ApplyFrameSharpening(winWidth, winHeight);
    ApplyCRTFilter(winWidth, winHeight);
#endif
    SDL_GL_SwapWindow(win);
    reset = false;
    #ifdef RAD_ANDROID
    /*
     * Manual 60 FPS cap for Android.
     *
     * This is intentionally independent from display refresh rate.
     * It prevents 90 Hz / 120 Hz devices from running the game logic too fast.
     *
     * m_only60 must remain the switch:
     * - true  = gameplay cap at 60 FPS
     * - false = FMV/cinematics or special states, do not force 60 here
     */
    static Uint64 sCapFreq = 0;
    static Uint64 sLastFrameTime = 0;

    if (sCapFreq == 0)
    {
        sCapFreq = SDL_GetPerformanceFrequency();
    }

    if (m_only60)
    {
        const Uint64 targetTicks =
            (Uint64)((double)sCapFreq / 60.0 + 0.5);

        Uint64 now = SDL_GetPerformanceCounter();

        if (sLastFrameTime == 0)
        {
            sLastFrameTime = now;
        }
        else
        {
            const Uint64 targetTime = sLastFrameTime + targetTicks;

            if (now < targetTime)
            {
                Uint64 remainingTicks = targetTime - now;

                double remainingMs =
                    (double)remainingTicks * 1000.0 / (double)sCapFreq;

                /*
                 * Coarse wait.
                 * Sleep most of the remaining time, leaving a small margin
                 * because SDL_Delay is not perfectly precise on Android.
                 */
                if (remainingMs > 2.0)
                {
                    SDL_Delay((Uint32)(remainingMs - 1.0));
                }

                /*
                 * Fine wait.
                 * Not a pure busy-wait: SDL_Delay(0) yields to the scheduler.
                 * This usually only runs for a tiny fraction of a millisecond.
                 */
                while (SDL_GetPerformanceCounter() < targetTime)
                {
                    SDL_Delay(0);
                }

                now = SDL_GetPerformanceCounter();
            }

            /*
             * Important:
             * Use the real current time, not targetTime.
             *
             * This avoids "catch-up" frames after a small hitch.
             * For this old game, we prefer never producing a too-fast frame.
             */
            sLastFrameTime = now;
        }
    }
    else
    {
        /*
         * If 60 FPS cap is disabled, reset the cap timer.
         *
         * This is important for FMV/cinematics:
         * when the game returns from a 30 FPS movie to gameplay,
         * we do not want to carry an old timestamp.
         */
        sLastFrameTime = 0;
    }
#endif

// Contador fps para logcat
#if defined(RAD_ANDROID) && defined(RAD_DEBUG)
    static int frames = 0;
    static Uint32 lastTick = 0;

    Uint32 t = SDL_GetTicks();

    if (lastTick == 0)
    {
        lastTick = t;
    }

    frames++;

    Uint32 elapsed = t - lastTick;

    if (elapsed >= 1000)
    {
        double fps = (double)frames * 1000.0 / (double)elapsed;

        SDL_Log("[FPS] %.2f", fps);

        frames = 0;
        lastTick = t;
    }
#endif
}

    
unsigned pglDisplay::Screenshot(pddiColour* buffer, int nBytes)
{
    if(nBytes < (winHeight * winWidth * 4))
        return 0;

    glReadPixels(0, 0,  winWidth, winHeight, GL_BGRA_EXT, GL_UNSIGNED_BYTE, buffer);

    unsigned tmp[2048];
    PDDIASSERT(winWidth < 2048);

    for(int i = 0; i < winHeight / 2; i++)
    {
        pddiColour* a = buffer + (i * winWidth);
        pddiColour* b = buffer + (((winHeight - 1) - i) * winWidth);
        memcpy(tmp, a, winWidth * 4);
        memcpy(a, b, winWidth * 4);
        memcpy(b, tmp, winWidth * 4);
    }

    return winHeight * winWidth * 4;
}

void pglDisplay::BeginTiming()
{
    beginTime = (float)SDL_GetTicks();
}

float pglDisplay::EndTiming()
{
    return (float)SDL_GetTicks() - beginTime;
}

void pglDisplay::BeginContext(void)
{
    prevRC = SDL_GL_GetCurrentContext();
    PDDIASSERT(prevRC != hRC);
    int error = SDL_GL_MakeCurrent(win, (SDL_GLContext)hRC);
    PDDIASSERT(!error);
}

void pglDisplay::EndContext(void)
{
    int error = SDL_GL_MakeCurrent(win, (SDL_GLContext)prevRC);
    PDDIASSERT(!error);
}

