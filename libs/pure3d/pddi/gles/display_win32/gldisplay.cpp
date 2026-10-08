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

namespace
{
GLuint gCelPostProgram = 0;
GLuint gCelPostTexture = 0;
GLuint gCelPostVbo = 0;
GLuint gCelRenderFbo = 0;
GLuint gCelRenderDepth = 0;
bool gCelRenderFboReady = false;
GLint gCelPostSceneLocation = -1;
GLint gCelPostTexelLocation = -1;
int gCelPostWidth = 0;
int gCelPostHeight = 0;

// GLES 2.0-compatible SMAA 1x resources.
// The SMAA stages use RGBA8 render targets so they remain broadly portable.
GLuint gSmaaFbo = 0;
GLuint gSmaaEdgesTexture = 0;
GLuint gSmaaBlendTexture = 0;
GLuint gSmaaEdgeProgram = 0;
GLuint gSmaaWeightProgram = 0;
GLuint gSmaaResolveProgram = 0;
GLint gSmaaEdgeSceneLocation = -1;
GLint gSmaaEdgeTexelLocation = -1;
GLint gSmaaWeightSceneLocation = -1;
GLint gSmaaWeightEdgesLocation = -1;
GLint gSmaaWeightTexelLocation = -1;
GLint gSmaaResolveSceneLocation = -1;
GLint gSmaaResolveBlendLocation = -1;
GLint gSmaaResolveTexelLocation = -1;
int gSmaaWidth = 0;
int gSmaaHeight = 0;
bool gSmaaResourcesReady = false;

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

static bool EnsureCelPostProcessResources(int width, int height);
static bool EnsureSmaaResources(int width, int height);

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

    /*
     * The cel post-process texture is also the colour attachment for the
     * off-screen game render target. It must exist before the FBO is checked.
     */
    if (!EnsureCelPostProcessResources(width, height))
        return false;

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

    glBindRenderbuffer(GL_RENDERBUFFER, gCelRenderDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, gCelRenderFbo);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        gCelPostTexture,
        0
    );
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
    else
    {
        SDL_Log(
            "SHAR Android cel render FBO ready: %dx%d",
            width,
            height
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
            "precision mediump float;\\n"
            "uniform sampler2D sceneTex;\\n"
            "uniform sampler2D blendTex;\\n"
            "uniform vec2 texelSize;\\n"
            "varying vec2 texcoord;\\n"
            "\\n"
            "float sceneLuma(vec3 colour) {\\n"
            "    return dot(colour, vec3(0.299, 0.587, 0.114));\\n"
            "}\\n"
            "\\n"
            "float randomNoise(vec2 p) {\\n"
            "    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;\\n"
            "}\\n"
            "\\n"
            "void main() {\\n"
            "    vec4 scene = texture2D(sceneTex, texcoord);\\n"
            "    vec4 blend = texture2D(blendTex, texcoord);\\n"
            "\\n"
            "    // SMAA 1x neighborhood resolve. The four blend channels\\n"
            "    // represent left, right, up and down neighborhood weights.\\n"
            "    vec3 aaColour = scene.rgb;\\n"
            "    float weightSum = blend.r + blend.g + blend.b + blend.a;\\n"
            "    if (weightSum > 0.001) {\\n"
            "        vec3 leftColour  = texture2D(sceneTex, texcoord - vec2(texelSize.x, 0.0)).rgb;\\n"
            "        vec3 rightColour = texture2D(sceneTex, texcoord + vec2(texelSize.x, 0.0)).rgb;\\n"
            "        vec3 upColour    = texture2D(sceneTex, texcoord + vec2(0.0, texelSize.y)).rgb;\\n"
            "        vec3 downColour  = texture2D(sceneTex, texcoord - vec2(0.0, texelSize.y)).rgb;\\n"
            "        aaColour = scene.rgb * max(1.0 - weightSum, 0.0) +\\n"
            "                   leftColour * blend.r + rightColour * blend.g +\\n"
            "                   upColour * blend.b + downColour * blend.a;\\n"
            "    }\\n"
            "\\n"
            "    // Existing screen-space cel outline detection, now evaluated\\n"
            "    // on the SMAA-resolved image so high-contrast outlines remain crisp.\\n"
            "    float centreLuma = sceneLuma(aaColour);\\n"
            "    float leftLuma  = sceneLuma(texture2D(sceneTex, texcoord - vec2(texelSize.x, 0.0)).rgb);\\n"
            "    float rightLuma = sceneLuma(texture2D(sceneTex, texcoord + vec2(texelSize.x, 0.0)).rgb);\\n"
            "    float upLuma    = sceneLuma(texture2D(sceneTex, texcoord + vec2(0.0, texelSize.y)).rgb);\\n"
            "    float downLuma  = sceneLuma(texture2D(sceneTex, texcoord - vec2(0.0, texelSize.y)).rgb);\\n"
            "    float edgeStrength = max(max(abs(centreLuma - leftLuma), abs(centreLuma - rightLuma)),\\n"
            "                             max(abs(centreLuma - upLuma), abs(centreLuma - downLuma)));\\n"
            "    float edge = smoothstep(0.10, 0.22, edgeStrength);\\n"
            "\\n"
            "    // Retain the existing full-scene toon edge darkening.\\n"
            "    vec3 toonColour = aaColour;\\n"
            "    toonColour = mix(toonColour, toonColour * 0.35, edge);\\n"
            "\\n"
            "    // Subtle atmospheric fog enhancement. Use the upper part of the\\n"
            "    // completed scene as a cheap sky-colour reference.\\n"
            "    vec3 fogColour = texture2D(sceneTex, vec2(0.5, 0.92)).rgb;\\n"
            "    float fogAmount = 0.04 * smoothstep(0.55, 1.0, texcoord.y);\\n"
            "    toonColour = mix(toonColour, fogColour, fogAmount);\\n"
            "\\n"
            "    // Lightweight screen-space sunlight/glare. No extra bloom target.\\n"
            "    float bright = max(centreLuma - 0.78, 0.0);\\n"
            "    bright = max(bright, max(max(leftLuma, rightLuma), max(upLuma, downLuma)) - 0.78);\\n"
            "    bright = max(bright, 0.0);\\n"
            "    float sunRegion = 1.0 - smoothstep(0.18, 0.70, distance(texcoord, vec2(0.5, 0.82)));\\n"
            "    float glare = bright * sunRegion * 0.08;\\n"
            "    toonColour += vec3(glare, glare * 0.95, glare * 0.82);\\n"
            "\\n"
            "    // Requested colour grade: +5% saturation, +2% contrast, neutral brightness.\\n"
            "    float gradedLuma = sceneLuma(toonColour);\\n"
            "    toonColour = mix(vec3(gradedLuma), toonColour, 1.05);\\n"
            "    toonColour = (toonColour - vec3(0.5)) * 1.02 + vec3(0.5);\\n"
            "\\n"
            "    // Very small dither to reduce visible gradient/band stepping.\\n"
            "    toonColour += vec3(randomNoise(texcoord) / 255.0);\\n"
            "\\n"
            "    // Requested 5% vignette strength at the extreme corners.\\n"
            "    float vignette = 1.0 - 0.05 * smoothstep(0.60, 1.0,\\n"
            "        distance(texcoord, vec2(0.5, 0.5)) * 1.4142);\\n"
            "    toonColour *= vignette;\\n"
            "\\n"
            "    gl_FragColor = vec4(clamp(toonColour, 0.0, 1.0), scene.a);\\n"
            "}\\n";

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


static bool CompileSmaaProgram(
    const char* vertexSource,
    const char* fragmentSource,
    GLuint* outProgram
)
{
    if (vertexSource == NULL || fragmentSource == NULL || outProgram == NULL)
        return false;

    GLuint vs = CompileCelPostShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fs = CompileCelPostShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vs == 0 || fs == 0)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    GLuint program = glCreateProgram();
    if (program == 0)
    {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return false;
    }

    glBindAttribLocation(program, 0, "position");
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);

    glDeleteShader(vs);
    glDeleteShader(fs);

    if (linked == GL_FALSE)
    {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        if (length > 0)
        {
            std::vector<char> log((size_t)length + 1, 0);
            glGetProgramInfoLog(program, length, NULL, log.data());
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "SMAA program link failed: %s",
                log.data()
            );
        }
        glDeleteProgram(program);
        return false;
    }

    *outProgram = program;
    return true;
}

static bool EnsureSmaaResources(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    if (gSmaaResourcesReady && gSmaaWidth == width && gSmaaHeight == height)
        return true;

    if (gSmaaFbo == 0)
        glGenFramebuffers(1, &gSmaaFbo);

    if (gSmaaEdgesTexture == 0)
        glGenTextures(1, &gSmaaEdgesTexture);

    if (gSmaaBlendTexture == 0)
        glGenTextures(1, &gSmaaBlendTexture);

    if (gSmaaFbo == 0 || gSmaaEdgesTexture == 0 || gSmaaBlendTexture == 0)
        return false;

    const char* vertexSource =
        "attribute vec2 position;\n"
        "varying vec2 texcoord;\n"
        "void main() {\n"
        "    texcoord = position * 0.5 + 0.5;\n"
        "    gl_Position = vec4(position, 0.0, 1.0);\n"
        "}\n";

    if (gSmaaEdgeProgram == 0)
    {
        const char* fragmentSource =
            "precision mediump float;\n"
            "uniform sampler2D sceneTex;\n"
            "uniform vec2 texelSize;\n"
            "varying vec2 texcoord;\n"
            "\n"
            "float sceneLuma(vec3 c) {\n"
            "    return dot(c, vec3(0.299, 0.587, 0.114));\n"
            "}\n"
            "\n"
            "void main() {\n"
            "    float c = sceneLuma(texture2D(sceneTex, texcoord).rgb);\n"
            "    float l = sceneLuma(texture2D(sceneTex, texcoord - vec2(texelSize.x, 0.0)).rgb);\n"
            "    float r = sceneLuma(texture2D(sceneTex, texcoord + vec2(texelSize.x, 0.0)).rgb);\n"
            "    float u = sceneLuma(texture2D(sceneTex, texcoord + vec2(0.0, texelSize.y)).rgb);\n"
            "    float d = sceneLuma(texture2D(sceneTex, texcoord - vec2(0.0, texelSize.y)).rgb);\n"
            "\n"
            "    float horizontalDelta = max(abs(c - l), abs(c - r));\n"
            "    float verticalDelta = max(abs(c - u), abs(c - d));\n"
            "    float threshold = 0.10;\n"
            "    float horizontalEdge = step(threshold, horizontalDelta);\n"
            "    float verticalEdge = step(threshold, verticalDelta);\n"
            "\n"
            "    // SMAA 1x luma edge texture: R stores edges crossing X,\n"
            "    // G stores edges crossing Y. BA remain clear for this stage.\n"
            "    gl_FragColor = vec4(horizontalEdge, verticalEdge, 0.0, 1.0);\n"
            "}\n";

        if (!CompileSmaaProgram(vertexSource, fragmentSource, &gSmaaEdgeProgram))
            return false;

        gSmaaEdgeSceneLocation = glGetUniformLocation(gSmaaEdgeProgram, "sceneTex");
        gSmaaEdgeTexelLocation = glGetUniformLocation(gSmaaEdgeProgram, "texelSize");
    }

    if (gSmaaWeightProgram == 0)
    {
        const char* fragmentSource =
            "precision mediump float;\n"
            "uniform sampler2D sceneTex;\n"
            "uniform sampler2D edgesTex;\n"
            "uniform vec2 texelSize;\n"
            "varying vec2 texcoord;\n"
            "\n"
            "float sceneLuma(vec3 c) {\n"
            "    return dot(c, vec3(0.299, 0.587, 0.114));\n"
            "}\n"
            "\n"
            "float edgeAt(vec2 uv, int axis) {\n"
            "    vec4 e = texture2D(edgesTex, uv);\n"
            "    return axis == 0 ? e.r : e.g;\n"
            "}\n"
            "\n"
            "void main() {\n"
            "    vec4 currentEdge = texture2D(edgesTex, texcoord);\n"
            "    vec3 centre = texture2D(sceneTex, texcoord).rgb;\n"
            "    float centreLuma = sceneLuma(centre);\n"
            "\n"
            "    // Search along each detected edge. Six samples keep the pass\n"
            "    // lightweight enough for GLES 2.0 while still following SMAA's\n"
            "    // line-endpoint idea instead of blindly averaging neighbors.\n"
            "    float leftSearch = 0.0;\n"
            "    float rightSearch = 0.0;\n"
            "    float upSearch = 0.0;\n"
            "    float downSearch = 0.0;\n"
            "    for (int i = 1; i <= 6; ++i) {\n"
            "        float fi = float(i);\n"
            "        if (leftSearch >= 0.0 && edgeAt(texcoord - vec2(texelSize.x * fi, 0.0), 0) > 0.5)\n"
            "            leftSearch += 1.0;\n"
            "        else if (leftSearch >= 0.0)\n"
            "            leftSearch = -1.0;\n"
            "\n"
            "        if (rightSearch >= 0.0 && edgeAt(texcoord + vec2(texelSize.x * fi, 0.0), 0) > 0.5)\n"
            "            rightSearch += 1.0;\n"
            "        else if (rightSearch >= 0.0)\n"
            "            rightSearch = -1.0;\n"
            "\n"
            "        if (upSearch >= 0.0 && edgeAt(texcoord + vec2(0.0, texelSize.y * fi), 1) > 0.5)\n"
            "            upSearch += 1.0;\n"
            "        else if (upSearch >= 0.0)\n"
            "            upSearch = -1.0;\n"
            "\n"
            "        if (downSearch >= 0.0 && edgeAt(texcoord - vec2(0.0, texelSize.y * fi), 1) > 0.5)\n"
            "            downSearch += 1.0;\n"
            "        else if (downSearch >= 0.0)\n"
            "            downSearch = -1.0;\n"
            "    }\n"
            "\n"
            "    leftSearch = max(leftSearch, 0.0);\n"
            "    rightSearch = max(rightSearch, 0.0);\n"
            "    upSearch = max(upSearch, 0.0);\n"
            "    downSearch = max(downSearch, 0.0);\n"
            "\n"
            "    float horizontalLineFactor = 0.70 + 0.30 * clamp((leftSearch + rightSearch) / 12.0, 0.0, 1.0);\n"
            "    float verticalLineFactor = 0.70 + 0.30 * clamp((upSearch + downSearch) / 12.0, 0.0, 1.0);\n"
            "\n"
            "    vec4 weights = vec4(0.0);\n"
            "\n"
            "    if (currentEdge.r > 0.5) {\n"
            "        float leftLuma = sceneLuma(texture2D(sceneTex, texcoord - vec2(texelSize.x, 0.0)).rgb);\n"
            "        float rightLuma = sceneLuma(texture2D(sceneTex, texcoord + vec2(texelSize.x, 0.0)).rgb);\n"
            "        float leftContrast = abs(centreLuma - leftLuma);\n"
            "        float rightContrast = abs(centreLuma - rightLuma);\n"
            "        float contrastSum = leftContrast + rightContrast + 0.0001;\n"
            "        float baseWeight = 0.20 * horizontalLineFactor;\n"
            "        weights.r = baseWeight * (0.65 + 0.35 * leftContrast / contrastSum);\n"
            "        weights.g = baseWeight * (0.65 + 0.35 * rightContrast / contrastSum);\n"
            "    }\n"
            "\n"
            "    if (currentEdge.g > 0.5) {\n"
            "        float upLuma = sceneLuma(texture2D(sceneTex, texcoord + vec2(0.0, texelSize.y)).rgb);\n"
            "        float downLuma = sceneLuma(texture2D(sceneTex, texcoord - vec2(0.0, texelSize.y)).rgb);\n"
            "        float upContrast = abs(centreLuma - upLuma);\n"
            "        float downContrast = abs(centreLuma - downLuma);\n"
            "        float contrastSum = upContrast + downContrast + 0.0001;\n"
            "        float baseWeight = 0.20 * verticalLineFactor;\n"
            "        weights.b = baseWeight * (0.65 + 0.35 * upContrast / contrastSum);\n"
            "        weights.a = baseWeight * (0.65 + 0.35 * downContrast / contrastSum);\n"
            "    }\n"
            "\n"
            "    float total = weights.r + weights.g + weights.b + weights.a;\n"
            "    if (total > 0.45) {\n"
            "        weights *= 0.45 / total;\n"
            "    }\n"
            "\n"
            "    gl_FragColor = weights;\n"
            "}\n";

        if (!CompileSmaaProgram(vertexSource, fragmentSource, &gSmaaWeightProgram))
            return false;

        gSmaaWeightSceneLocation = glGetUniformLocation(gSmaaWeightProgram, "sceneTex");
        gSmaaWeightEdgesLocation = glGetUniformLocation(gSmaaWeightProgram, "edgesTex");
        gSmaaWeightTexelLocation = glGetUniformLocation(gSmaaWeightProgram, "texelSize");
    }

    if (gSmaaResolveProgram == 0)
    {
        const char* vertexSource = 
            "attribute vec2 position;\n"
            "varying vec2 texcoord;\n"
            "void main() {\n"
            "    texcoord = position * 0.5 + 0.5;\n"
            "    gl_Position = vec4(position, 0.0, 1.0);\n"
            "}\n";

        const char* fragmentSource = NULL;
        // The final resolve source lives in the existing cel post-process program
        // so that SMAA and the requested Simpsons visual enhancements remain one
        // final fullscreen pass.
        (void)vertexSource;
        (void)fragmentSource;
    }

    // The existing cel post-process program is the final SMAA neighborhood resolve.
    gSmaaResolveProgram = gCelPostProgram;

    gSmaaResolveSceneLocation = gCelPostSceneLocation;
    gSmaaResolveBlendLocation = glGetUniformLocation(gSmaaResolveProgram, "blendTex");
    gSmaaResolveTexelLocation = gCelPostTexelLocation;

    if (gSmaaResolveBlendLocation < 0)
    {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "SMAA resolve shader is missing blendTex uniform"
        );
        return false;
    }

    // Allocate the two temporal SMAA textures.
    glBindTexture(GL_TEXTURE_2D, gSmaaEdgesTexture);
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

    glBindTexture(GL_TEXTURE_2D, gSmaaBlendTexture);
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

    glBindFramebuffer(GL_FRAMEBUFFER, gSmaaFbo);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        gSmaaEdgesTexture,
        0
    );

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "SHAR Android SMAA FBO incomplete: 0x%04x",
            (unsigned)status
        );
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        gSmaaBlendTexture,
        0
    );

    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "SHAR Android SMAA blend FBO incomplete: 0x%04x",
            (unsigned)status
        );
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    gSmaaWidth = width;
    gSmaaHeight = height;
    gSmaaResourcesReady = true;

    SDL_Log(
        "SHAR Android SMAA 1x resources ready: %dx%d",
        width,
        height
    );

    return true;
}

static void ApplyCelPostProcess(int width, int height)
{
    if (!IsCelShadingEnabled())
        return;

    /*
     * The game must have rendered this frame into our off-screen target.
     * During the first frame after the menu toggle, the toggle can happen
     * after BeginFrame(); in that case the frame was rendered to the normal
     * framebuffer and must simply be presented unchanged.
     */
    GLint currentFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFramebuffer);
    if ((GLuint)currentFramebuffer != gCelRenderFbo || !gCelRenderFboReady)
        return;

    if (!EnsureCelPostProcessResources(width, height))
        return;

    if (!EnsureSmaaResources(width, height))
        return;

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean stencilEnabled = glIsEnabled(GL_STENCIL_TEST);

    GLint previousProgram = 0;
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture0 = 0;
    GLint previousTexture1 = 0;
    GLint previousArrayBuffer = 0;
    GLint previousVao = 0;
    GLint previousAttrib0Enabled = GL_FALSE;

    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture0);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture1);
    glActiveTexture(previousActiveTexture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING_OES, &previousVao);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &previousAttrib0Enabled);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);

    glViewport(0, 0, width, height);
    glBindVertexArrayOES(0);
    glBindBuffer(GL_ARRAY_BUFFER, gCelPostVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void*)0);

    // Pass 1: SMAA luma edge detection.
    glBindFramebuffer(GL_FRAMEBUFFER, gSmaaFbo);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        gSmaaEdgesTexture,
        0
    );
    glUseProgram(gSmaaEdgeProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gCelPostTexture);
    if (gSmaaEdgeSceneLocation >= 0)
        glUniform1i(gSmaaEdgeSceneLocation, 0);
    if (gSmaaEdgeTexelLocation >= 0)
        glUniform2f(gSmaaEdgeTexelLocation, 1.0f / (float)width, 1.0f / (float)height);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Pass 2: SMAA directional searches and blend-weight calculation.
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        gSmaaBlendTexture,
        0
    );
    glUseProgram(gSmaaWeightProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gCelPostTexture);
    if (gSmaaWeightSceneLocation >= 0)
        glUniform1i(gSmaaWeightSceneLocation, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gSmaaEdgesTexture);
    if (gSmaaWeightEdgesLocation >= 0)
        glUniform1i(gSmaaWeightEdgesLocation, 1);

    if (gSmaaWeightTexelLocation >= 0)
        glUniform2f(gSmaaWeightTexelLocation, 1.0f / (float)width, 1.0f / (float)height);

    glActiveTexture(GL_TEXTURE0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Pass 3: SMAA neighborhood resolve plus the existing fullscreen
    // cel-shading/visual-enhancement pass.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(gSmaaResolveProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gCelPostTexture);
    if (gSmaaResolveSceneLocation >= 0)
        glUniform1i(gSmaaResolveSceneLocation, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gSmaaBlendTexture);
    if (gSmaaResolveBlendLocation >= 0)
        glUniform1i(gSmaaResolveBlendLocation, 1);

    if (gSmaaResolveTexelLocation >= 0)
        glUniform2f(gSmaaResolveTexelLocation, 1.0f / (float)width, 1.0f / (float)height);

    glActiveTexture(GL_TEXTURE0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Restore the caller's GL state, but intentionally leave framebuffer 0
    // bound so SDL_GL_SwapWindow presents the processed frame.
    if (previousAttrib0Enabled)
        glEnableVertexAttribArray(0);
    else
        glDisableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previousArrayBuffer);
    glBindVertexArrayOES((GLuint)previousVao);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture1);
    glActiveTexture(previousActiveTexture);
    glUseProgram((GLuint)previousProgram);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    if (depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (scissorEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (stencilEnabled) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
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
    if (gSmaaEdgeProgram)
    {
        glDeleteProgram(gSmaaEdgeProgram);
        gSmaaEdgeProgram = 0;
    }
    if (gSmaaWeightProgram)
    {
        glDeleteProgram(gSmaaWeightProgram);
        gSmaaWeightProgram = 0;
    }
    if (gSmaaFbo)
    {
        glDeleteFramebuffers(1, &gSmaaFbo);
        gSmaaFbo = 0;
    }
    if (gSmaaEdgesTexture)
    {
        glDeleteTextures(1, &gSmaaEdgesTexture);
        gSmaaEdgesTexture = 0;
    }
    if (gSmaaBlendTexture)
    {
        glDeleteTextures(1, &gSmaaBlendTexture);
        gSmaaBlendTexture = 0;
    }
    if (gCelPostVbo)
    {
        glDeleteBuffers(1, &gCelPostVbo);
        gCelPostVbo = 0;
    }
    if (gCelPostTexture)
    {
        glDeleteTextures(1, &gCelPostTexture);
        gCelPostTexture = 0;
    }
    if (gCelPostProgram)
    {
        glDeleteProgram(gCelPostProgram);
        gCelPostProgram = 0;
    }

    if (gCelRenderDepth)
    {
        glDeleteRenderbuffers(1, &gCelRenderDepth);
        gCelRenderDepth = 0;
    }
    if (gCelRenderFbo)
    {
        glDeleteFramebuffers(1, &gCelRenderFbo);
        gCelRenderFbo = 0;
    }
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

