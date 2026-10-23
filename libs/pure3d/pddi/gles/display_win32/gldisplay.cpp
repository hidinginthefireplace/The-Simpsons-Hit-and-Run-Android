//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gles/gl.hpp>
#include <pddi/gles/glcon.hpp>
#include <pddi/gles/gldisplay.hpp>
#include <pddi/base/debug.hpp>
#include <SDL.h>
#if defined(RAD_ANDROID)
#include <SDL_system.h>
#endif

#include<stdio.h>
#include<string.h>
#include<math.h>
#include<stdlib.h>
#include<vector>

#if defined(RAD_ANDROID)
#include <jni.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <sys/stat.h>

bool IsCelShadingEnabled();
bool IsSHARFrontendMenuCelPassActive();
bool IsMotionBlurEnabled();
void SetSHARCharacterMaskWorldEnabled(bool enabled);
void SetSHARCharacterMaskModelPass(bool enabled);
void SetSHARTrackedColorWriteMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void InvalidateSHARTrackedColorWriteMaskCache();
bool GetSHARMotionBlurCameraData(float* output);
bool IsSHARWorldCloudShadowLevelAllowed();
bool IsFrameSharpeningEnabled();
float GetFrameSharpenStrength();


void GetCRTFilterConfiguration(bool* enabled, float* scanlineStrength, float* curvature, float* glowStrength, float* cornerDarkening, float* overscan, float* brightness, float* contrast);

static int gSHARAndroidTargetFPS = 60;
// Tracks the complete iris transition, including the black-screen scene handoff
// between the closing and opening halves.
static bool gSHARIrisTransitionActive = false;

static bool gSHARBloomEnabled = true;
static bool gSHARCloudShadowsEnabled = true;
static bool gSHARGraphicsEffectTogglesLoaded = false;

static bool ReadSHARGraphicsToggleFile(const char* filename, bool defaultValue)
{
    const char* basePath = SDL_AndroidGetApplicationExternalFilesPath();
    if( basePath == NULL || basePath[0] == '\0' )
    {
        return defaultValue;
    }

    char path[512];
    const int length = snprintf(
        path, sizeof(path), "%s/Graphics/%s", basePath, filename );
    if( length < 0 || length >= static_cast<int>( sizeof(path) ) )
    {
        return defaultValue;
    }

    FILE* file = fopen( path, "r" );
    if( file == NULL )
    {
        return defaultValue;
    }

    bool result = defaultValue;
    char line[256];
    while( fgets( line, sizeof(line), file ) != NULL )
    {
        char* value = strchr( line, '=' );
        if( value == NULL )
        {
            continue;
        }
        ++value;
        while( *value == ' ' || *value == '\t' )
        {
            ++value;
        }

        if( strncmp( value, "true", 4 ) == 0 ||
            strncmp( value, "TRUE", 4 ) == 0 ||
            *value == '1' || strncmp( value, "on", 2 ) == 0 ||
            strncmp( value, "ON", 2 ) == 0 ||
            strncmp( value, "yes", 3 ) == 0 )
        {
            result = true;
        }
        else if( strncmp( value, "false", 5 ) == 0 ||
                 strncmp( value, "FALSE", 5 ) == 0 ||
                 *value == '0' || strncmp( value, "off", 3 ) == 0 ||
                 strncmp( value, "OFF", 3 ) == 0 ||
                 strncmp( value, "no", 2 ) == 0 )
        {
            result = false;
        }
        break;
    }

    fclose( file );
    return result;
}

static void WriteSHARGraphicsToggleFile(const char* filename, bool enabled)
{
    const char* basePath = SDL_AndroidGetApplicationExternalFilesPath();
    if( basePath == NULL || basePath[0] == '\0' )
    {
        return;
    }

    char directory[512];
    int length = snprintf(
        directory, sizeof(directory), "%s/Graphics", basePath );
    if( length < 0 || length >= static_cast<int>( sizeof(directory) ) )
    {
        return;
    }

    struct stat directoryInfo;
    if( stat( directory, &directoryInfo ) != 0 )
    {
        mkdir( directory, 0777 );
    }
    if( stat( directory, &directoryInfo ) != 0 || !S_ISDIR( directoryInfo.st_mode ) )
    {
        return;
    }

    char path[512];
    length = snprintf( path, sizeof(path), "%s/%s", directory, filename );
    if( length < 0 || length >= static_cast<int>( sizeof(path) ) )
    {
        return;
    }

    FILE* file = fopen( path, "w" );
    if( file == NULL )
    {
        return;
    }

    fprintf( file,
        "# Simpsons Hit & Run Android graphics effect setting\n"
        "# Saved by the in-game Graphics menu.\n"
        "enabled=%s\n",
        enabled ? "true" : "false" );
    fclose( file );
}

static void EnsureSHARGraphicsEffectTogglesLoaded()
{
    if( gSHARGraphicsEffectTogglesLoaded )
    {
        return;
    }
    gSHARGraphicsEffectTogglesLoaded = true;

    gSHARBloomEnabled =
        ReadSHARGraphicsToggleFile( "Simpsons_bloom.txt", true );
    gSHARCloudShadowsEnabled =
        ReadSHARGraphicsToggleFile( "Simpsons_cloud_shadows.txt", true );
}

bool IsSHARBloomEnabled()
{
    EnsureSHARGraphicsEffectTogglesLoaded();
    return gSHARBloomEnabled;
}

void SetSHARBloomEnabled(bool enabled)
{
    EnsureSHARGraphicsEffectTogglesLoaded();
    if( gSHARBloomEnabled == enabled )
    {
        return;
    }
    gSHARBloomEnabled = enabled;
    WriteSHARGraphicsToggleFile( "Simpsons_bloom.txt", enabled );
}

bool IsSHARCloudShadowsEnabled()
{
    EnsureSHARGraphicsEffectTogglesLoaded();
    return gSHARCloudShadowsEnabled;
}

void SetSHARCloudShadowsEnabled(bool enabled)
{
    EnsureSHARGraphicsEffectTogglesLoaded();
    if( gSHARCloudShadowsEnabled == enabled )
    {
        return;
    }
    gSHARCloudShadowsEnabled = enabled;
    WriteSHARGraphicsToggleFile( "Simpsons_cloud_shadows.txt", enabled );
}

void SetSHARIrisTransitionActive(bool active)
{
    gSHARIrisTransitionActive = active;
}

// The GUI lives in libmain.so and the renderer is built as libpddi.a. Keep
// this setter at global scope so the main library can link to it.
void SetSHARAndroidTargetFPS(int targetFPS)
{
    if (targetFPS == 30 || targetFPS == 60 || targetFPS == 90 ||
        targetFPS == 120 || targetFPS == 144)
    {
        gSHARAndroidTargetFPS = targetFPS;
    }
}

namespace
{

struct SHARMotionBlurCameraState
{
    float position[3];
    float right[3];
    float up[3];
    float forward[3];
    float tanHalfFovX;
    float tanHalfFovY;
    float nearPlane;
    float farPlane;
    bool valid;
};

static SHARMotionBlurCameraState gSHARPreviousMotionBlurCamera = {};
static int gSHARMotionBlurHistoryWidth = 0;
static int gSHARMotionBlurHistoryHeight = 0;
static Uint64 gSHARPreviousMotionBlurSampleTime = 0;

struct SHARMotionBlurSubjectBounds
{
    float position[3];
    float halfWidth;
    float halfHeight;
    float motionIntensity;
    bool valid;
};

static SHARMotionBlurSubjectBounds gSHARMotionBlurCharacterSubject = {};
static SHARMotionBlurSubjectBounds gSHARMotionBlurVehicleSubject = {};

static bool CaptureSHARMotionBlurCamera(SHARMotionBlurCameraState* state)
{
    if (state == NULL)
        return false;

    // Keep camera acquisition inside the PDDI context implementation so the
    // display backend does not depend on Pure3D headers with broader includes.
    float cameraData[16] = {};
    if (!GetSHARMotionBlurCameraData(cameraData))
        return false;

    state->position[0] = cameraData[0];
    state->position[1] = cameraData[1];
    state->position[2] = cameraData[2];
    state->right[0] = cameraData[3];
    state->right[1] = cameraData[4];
    state->right[2] = cameraData[5];
    state->up[0] = cameraData[6];
    state->up[1] = cameraData[7];
    state->up[2] = cameraData[8];
    state->forward[0] = cameraData[9];
    state->forward[1] = cameraData[10];
    state->forward[2] = cameraData[11];
    state->tanHalfFovX = cameraData[12];
    state->tanHalfFovY = cameraData[13];
    state->nearPlane = cameraData[14];
    state->farPlane = cameraData[15];
    state->valid = true;
    return true;
}

static bool IsSHARMotionBlurHistoryUsable(const SHARMotionBlurCameraState& current, int width, int height)
{
    if (!current.valid || !gSHARPreviousMotionBlurCamera.valid ||
        width != gSHARMotionBlurHistoryWidth || height != gSHARMotionBlurHistoryHeight)
        return false;

    const float dx = current.position[0] - gSHARPreviousMotionBlurCamera.position[0];
    const float dy = current.position[1] - gSHARPreviousMotionBlurCamera.position[1];
    const float dz = current.position[2] - gSHARPreviousMotionBlurCamera.position[2];
    if (dx * dx + dy * dy + dz * dz > 400.0f)
        return false; // Avoid streaks across teleports and camera cuts.

    const float forwardDot =
        current.forward[0] * gSHARPreviousMotionBlurCamera.forward[0] +
        current.forward[1] * gSHARPreviousMotionBlurCamera.forward[1] +
        current.forward[2] * gSHARPreviousMotionBlurCamera.forward[2];
    return forwardDot > 0.60f;
}

static void CommitSHARMotionBlurCameraHistory(
    const SHARMotionBlurCameraState& current, bool captured, int width, int height)
{
    if (captured && current.valid && width > 0 && height > 0)
    {
        gSHARPreviousMotionBlurCamera = current;
        gSHARMotionBlurHistoryWidth = width;
        gSHARMotionBlurHistoryHeight = height;
    }
    else
    {
        gSHARPreviousMotionBlurCamera.valid = false;
        gSHARMotionBlurHistoryWidth = 0;
        gSHARMotionBlurHistoryHeight = 0;
    }
}

static float GetSHARMotionBlurTimeScale()
{
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    const Uint64 now = SDL_GetPerformanceCounter();
    float scale = 1.0f;

    if( frequency > 0 && gSHARPreviousMotionBlurSampleTime > 0 &&
        now > gSHARPreviousMotionBlurSampleTime )
    {
        const double elapsedTicks =
            (double)(now - gSHARPreviousMotionBlurSampleTime);
        const double referenceTicks = (double)frequency / 60.0;
        const double ratio = referenceTicks / elapsedTicks;

        // Preserve roughly the same screen-space blur duration as FPS changes,
        // while preventing unusually long frames or timer anomalies from causing streaks.
        if( ratio < 0.25 )
        {
            scale = 0.25f;
        }
        else if( ratio > 2.4 )
        {
            scale = 2.4f;
        }
        else
        {
            scale = (float)ratio;
        }
    }

    gSHARPreviousMotionBlurSampleTime = now;
    return scale;
}

GLuint gCelPostProgram = 0;
GLuint gCelPostTexture = 0;
GLuint gCelPostVbo = 0;
GLuint gCelFallbackProgram = 0;
GLuint gCelFallbackVbo = 0;
GLint gCelFallbackTextureLocation = -1;

// A frame uses one committed world-render-size snapshot. PDDI dimensions are
// restored to that target at frame start and temporarily switch to output size
// after scene composition for GUI rendering.
static int gSHARFrameWidth = 0;
static int gSHARFrameHeight = 0;
static bool gCelWorldInFbo = false;
// Tracks whether full-frame filters have already run before touch controls.
static bool gSHARPostProcessAppliedBeforeTouchHud = false;
// pglDisplay keeps the internal render size for the world. During the GUI phase,
// briefly expose the actual Surface dimensions, then restore the render size at
// the next frame start in CommitPendingResize().
static int* gSHARActiveDisplayWidthForGui = NULL;
static int* gSHARActiveDisplayHeightForGui = NULL;
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
GLint gCelPostOutlineTexelSizeLocation = -1;
GLint gCelPostFineDetailEnabledLocation = -1;
GLint gCelPostBloomLocation = -1;
GLint gCelPostBloomStrengthLocation = -1;
GLint gCelPostDepthLocation = -1;
GLint gCelPostDepthAvailableLocation = -1;
GLint gCelPostDepthEdgeDetectionAvailableLocation = -1;
// Experimental Option B: depth-aware, world-anchored moving cloud shadows.
GLint gCelPostCloudShadowEnabledLocation = -1;
GLint gCelPostCloudShadowTimeLocation = -1;
GLint gCelPostCloudShadowStrengthLocation = -1;
GLint gCelPostCelEffectsLocation = -1;
GLint gCelPostMotionBlurEnabledLocation = -1;
GLint gCelPostMotionBlurValidLocation = -1;
GLint gCelPostMotionBlurScaleLocation = -1;
GLint gCelPostCurrentCameraPositionLocation = -1;
GLint gCelPostCurrentCameraRightLocation = -1;
GLint gCelPostCurrentCameraUpLocation = -1;
GLint gCelPostCurrentCameraForwardLocation = -1;
GLint gCelPostPreviousCameraPositionLocation = -1;
GLint gCelPostPreviousCameraRightLocation = -1;
GLint gCelPostPreviousCameraUpLocation = -1;
GLint gCelPostPreviousCameraForwardLocation = -1;
GLint gCelPostCurrentTanHalfFovXLocation = -1;
GLint gCelPostCurrentTanHalfFovYLocation = -1;
GLint gCelPostPreviousTanHalfFovXLocation = -1;
GLint gCelPostPreviousTanHalfFovYLocation = -1;
GLint gCelPostCurrentNearPlaneLocation = -1;
GLint gCelPostCurrentFarPlaneLocation = -1;
GLint gCelPostPreviousNearPlaneLocation = -1;
GLint gCelPostCharacterMaskEnabledLocation = -1;
GLint gCelPostCharacterMaskPositionLocation = -1;
GLint gCelPostCharacterMaskSizeLocation = -1;
GLint gCelPostVehicleMaskEnabledLocation = -1;
GLint gCelPostVehicleMaskPositionLocation = -1;
GLint gCelPostVehicleMaskSizeLocation = -1;
GLint gCelPostVehicleMotionIntensityLocation = -1;

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
GLint gCRTCrtEnabledLocation = -1;
GLint gCRTSaturationLocation = -1;
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
int gCelPostFailedWidth = 0;
int gCelPostFailedHeight = 0;
int gCelRenderbufferWidth = 0;
int gCelRenderbufferHeight = 0;
int gCelRenderbufferFailedWidth = 0;
int gCelRenderbufferFailedHeight = 0;
int gCelRenderFboFailedWidth = 0;
int gCelRenderFboFailedHeight = 0;

// Successful scene-FBO validation is reusable while its size, attachments and
// resource-budget inputs remain unchanged. ResetAndroidPostProcess() invalidates
// this cache whenever the GL resources are recreated or the context is reset.
static bool gCelRenderTargetValidationCached = false;
static int gCelRenderTargetValidatedWidth = 0;
static int gCelRenderTargetValidatedHeight = 0;
static bool gCelRenderTargetValidatedBloomEnabled = false;
static bool gCelRenderTargetValidatedUsesDepthTexture = false;
static GLuint gCelRenderTargetValidatedFbo = 0;
static GLuint gCelRenderTargetValidatedColourTexture = 0;
static GLuint gCelRenderTargetValidatedDepthObject = 0;

int gFrameSharpenFailedWidth = 0;
int gFrameSharpenFailedHeight = 0;
int gCRTFailedWidth = 0;
int gCRTFailedHeight = 0;

// Budget for auxiliary full-screen post-processing targets. This deliberately
// excludes game assets and the Android default framebuffer, so it is only a
// conservative guard—not a claim about total free GPU memory. At 2160p, the
// estimated targets for cel shading, sharpening, and CRT total about 115 MiB;
// allow a 128 MiB test budget while retaining per-target GL allocation checks.
static const size_t SHAR_POSTPROCESS_BUDGET_BYTES =
    (size_t)128 * 1024 * 1024;
static int gSHARLastRejectedWidth = 0;
static int gSHARLastRejectedHeight = 0;
static int gSHARLastRejectedReason = 0;


#if defined(RAD_ANDROID)
// Temporary, non-blocking performance diagnostics for the cloud-shadow experiment.
// GPU timings use GL_EXT_disjoint_timer_query when available. CPU timings measure
// time spent submitting commands, not GPU completion time; frame intervals include
// scheduling and the configured FPS cap. No GL query result is synchronously waited on.
enum
{
    SHAR_PERF_WORLD = 0,
    SHAR_PERF_COMPOSE,
    SHAR_PERF_SHARPEN,
    SHAR_PERF_CRT,
    SHAR_PERF_SATURATION,
    SHAR_PERF_PASS_COUNT
};
static const int SHAR_PERF_QUERY_SLOTS = 4;
static const GLenum SHAR_GL_TIME_ELAPSED_EXT = 0x88BF;
static const GLenum SHAR_GL_QUERY_RESULT_EXT = 0x8866;
static const GLenum SHAR_GL_QUERY_RESULT_AVAILABLE_EXT = 0x8867;
static const GLenum SHAR_GL_GPU_DISJOINT_EXT = 0x8FBB;

typedef void (*SHARGenQueriesEXTProc)(GLsizei, GLuint*);
typedef void (*SHARDeleteQueriesEXTProc)(GLsizei, const GLuint*);
typedef void (*SHARBeginQueryEXTProc)(GLenum, GLuint);
typedef void (*SHAREndQueryEXTProc)(GLenum);
typedef void (*SHARGetQueryObjectuivEXTProc)(GLuint, GLenum, GLuint*);
typedef void (*SHARGetQueryObjectui64vEXTProc)(GLuint, GLenum, unsigned long long*);

struct SHARGpuQuerySlot
{
    GLuint id;
    bool pending;
    int width;
    int height;
};

struct SHARPerfAccumulator
{
    Uint64 total;
    Uint64 maximum;
    unsigned int samples;
    int width;
    int height;
};

static bool gSHARGpuTimerChecked = false;
static bool gSHARGpuTimerSupported = false;
static SHARGenQueriesEXTProc gSHARGenQueriesEXT = NULL;
static SHARDeleteQueriesEXTProc gSHARDeleteQueriesEXT = NULL;
static SHARBeginQueryEXTProc gSHARBeginQueryEXT = NULL;
static SHAREndQueryEXTProc gSHAREndQueryEXT = NULL;
static SHARGetQueryObjectuivEXTProc gSHARGetQueryObjectuivEXT = NULL;
static SHARGetQueryObjectui64vEXTProc gSHARGetQueryObjectui64vEXT = NULL;
static SHARGpuQuerySlot gSHARGpuQuerySlots[SHAR_PERF_PASS_COUNT][SHAR_PERF_QUERY_SLOTS] = {};
static SHARPerfAccumulator gSHARPerfCpu[SHAR_PERF_PASS_COUNT] = {};
static SHARPerfAccumulator gSHARPerfGpu[SHAR_PERF_PASS_COUNT] = {};
static int gSHARPerfActivePass = -1;
static int gSHARPerfActiveSlot = -1;
static Uint64 gSHARPerfCpuStart = 0;
static int gSHARPerfActiveWidth = 0;
static int gSHARPerfActiveHeight = 0;
static Uint64 gSHARPerfLastSwapCounter = 0;
static Uint64 gSHARPerfSwapTotalTicks = 0;
static Uint64 gSHARPerfSwapMaxTicks = 0;
static unsigned int gSHARPerfSwapSamples = 0;

static const char* SHARPerfPassName(int pass)
{
    switch (pass)
    {
        case SHAR_PERF_WORLD: return "world_render";
        case SHAR_PERF_COMPOSE: return "compose_resolve";
        case SHAR_PERF_SHARPEN: return "sharpen";
        case SHAR_PERF_CRT: return "crt";
        case SHAR_PERF_SATURATION: return "final_saturation";
        default: return "unknown";
    }
}

static bool EnsureSHARGpuTimerSupport()
{
    if (gSHARGpuTimerChecked)
        return gSHARGpuTimerSupported;

    gSHARGpuTimerChecked = true;
    if (SDL_GL_ExtensionSupported("GL_EXT_disjoint_timer_query") != SDL_TRUE)
    {
        SDL_Log("SHAR PERF: GL_EXT_disjoint_timer_query unavailable; CPU and frame-interval diagnostics remain enabled.");
        return false;
    }

    gSHARGenQueriesEXT = reinterpret_cast<SHARGenQueriesEXTProc>(SDL_GL_GetProcAddress("glGenQueriesEXT"));
    gSHARDeleteQueriesEXT = reinterpret_cast<SHARDeleteQueriesEXTProc>(SDL_GL_GetProcAddress("glDeleteQueriesEXT"));
    gSHARBeginQueryEXT = reinterpret_cast<SHARBeginQueryEXTProc>(SDL_GL_GetProcAddress("glBeginQueryEXT"));
    gSHAREndQueryEXT = reinterpret_cast<SHAREndQueryEXTProc>(SDL_GL_GetProcAddress("glEndQueryEXT"));
    gSHARGetQueryObjectuivEXT = reinterpret_cast<SHARGetQueryObjectuivEXTProc>(SDL_GL_GetProcAddress("glGetQueryObjectuivEXT"));
    gSHARGetQueryObjectui64vEXT = reinterpret_cast<SHARGetQueryObjectui64vEXTProc>(SDL_GL_GetProcAddress("glGetQueryObjectui64vEXT"));

    if (gSHARGenQueriesEXT == NULL || gSHARDeleteQueriesEXT == NULL ||
        gSHARBeginQueryEXT == NULL || gSHAREndQueryEXT == NULL ||
        gSHARGetQueryObjectuivEXT == NULL || gSHARGetQueryObjectui64vEXT == NULL)
    {
        SDL_Log("SHAR PERF: timer-query entry points unavailable; CPU and frame-interval diagnostics remain enabled.");
        return false;
    }

    GLuint ids[SHAR_PERF_PASS_COUNT * SHAR_PERF_QUERY_SLOTS] = {};
    gSHARGenQueriesEXT((GLsizei)(SHAR_PERF_PASS_COUNT * SHAR_PERF_QUERY_SLOTS), ids);
    bool allCreated = true;
    for (int i = 0; i < SHAR_PERF_PASS_COUNT * SHAR_PERF_QUERY_SLOTS; ++i)
    {
        if (ids[i] == 0)
            allCreated = false;
    }
    if (!allCreated)
    {
        for (int i = 0; i < SHAR_PERF_PASS_COUNT * SHAR_PERF_QUERY_SLOTS; ++i)
        {
            if (ids[i] != 0)
                gSHARDeleteQueriesEXT(1, &ids[i]);
        }
        SDL_Log("SHAR PERF: could not allocate timer queries; CPU and frame-interval diagnostics remain enabled.");
        return false;
    }

    for (int pass = 0; pass < SHAR_PERF_PASS_COUNT; ++pass)
    {
        for (int slot = 0; slot < SHAR_PERF_QUERY_SLOTS; ++slot)
        {
            gSHARGpuQuerySlots[pass][slot].id = ids[pass * SHAR_PERF_QUERY_SLOTS + slot];
        }
    }
    gSHARGpuTimerSupported = true;
    SDL_Log("SHAR PERF: GPU timer queries enabled. Results are asynchronous and sampled in 120-result batches.");
    return true;
}

static void RecordSHARPerfCpu(int pass, Uint64 ticks, int width, int height)
{
    if (pass < 0 || pass >= SHAR_PERF_PASS_COUNT)
        return;

    SHARPerfAccumulator& stats = gSHARPerfCpu[pass];
    stats.total += ticks;
    if (ticks > stats.maximum)
        stats.maximum = ticks;
    stats.samples++;
    stats.width = width;
    stats.height = height;

    if (stats.samples >= 120)
    {
        const Uint64 frequency = SDL_GetPerformanceFrequency();
        if (frequency > 0)
        {
            const double averageMs = (double)stats.total * 1000.0 /
                ((double)frequency * (double)stats.samples);
            const double maximumMs = (double)stats.maximum * 1000.0 / (double)frequency;
            SDL_Log("SHAR PERF CPU-submit pass=%s avg=%.3fms max=%.3fms samples=%u size=%dx%d",
                SHARPerfPassName(pass), averageMs, maximumMs, stats.samples,
                stats.width, stats.height);
        }
        stats.total = 0;
        stats.maximum = 0;
        stats.samples = 0;
    }
}

static void RecordSHARPerfGpu(int pass, unsigned long long nanoseconds, int width, int height)
{
    if (pass < 0 || pass >= SHAR_PERF_PASS_COUNT)
        return;

    SHARPerfAccumulator& stats = gSHARPerfGpu[pass];
    stats.total += (Uint64)nanoseconds;
    if ((Uint64)nanoseconds > stats.maximum)
        stats.maximum = (Uint64)nanoseconds;
    stats.samples++;
    stats.width = width;
    stats.height = height;

    if (stats.samples >= 120)
    {
        const double averageMs = (double)stats.total / ((double)stats.samples * 1000000.0);
        const double maximumMs = (double)stats.maximum / 1000000.0;
        SDL_Log("SHAR PERF GPU pass=%s avg=%.3fms max=%.3fms samples=%u size=%dx%d",
            SHARPerfPassName(pass), averageMs, maximumMs, stats.samples,
            stats.width, stats.height);
        stats.total = 0;
        stats.maximum = 0;
        stats.samples = 0;
    }
}

static bool BeginSHARPerformancePass(int pass, int width, int height)
{
    if (pass < 0 || pass >= SHAR_PERF_PASS_COUNT || gSHARPerfActivePass >= 0)
        return false;

    gSHARPerfActivePass = pass;
    gSHARPerfActiveSlot = -1;
    gSHARPerfActiveWidth = width;
    gSHARPerfActiveHeight = height;
    gSHARPerfCpuStart = SDL_GetPerformanceCounter();

    if (EnsureSHARGpuTimerSupport())
    {
        for (int slot = 0; slot < SHAR_PERF_QUERY_SLOTS; ++slot)
        {
            SHARGpuQuerySlot& query = gSHARGpuQuerySlots[pass][slot];
            if (query.id != 0 && !query.pending)
            {
                query.width = width;
                query.height = height;
                gSHARBeginQueryEXT(SHAR_GL_TIME_ELAPSED_EXT, query.id);
                gSHARPerfActiveSlot = slot;
                break;
            }
        }
    }
    return true;
}

static void EndSHARPerformancePass()
{
    if (gSHARPerfActivePass < 0)
        return;

    const int pass = gSHARPerfActivePass;
    const Uint64 now = SDL_GetPerformanceCounter();
    if (gSHARPerfActiveSlot >= 0)
    {
        SHARGpuQuerySlot& query = gSHARGpuQuerySlots[pass][gSHARPerfActiveSlot];
        gSHAREndQueryEXT(SHAR_GL_TIME_ELAPSED_EXT);
        query.pending = true;
    }

    if (now >= gSHARPerfCpuStart)
    {
        RecordSHARPerfCpu(pass, now - gSHARPerfCpuStart,
            gSHARPerfActiveWidth, gSHARPerfActiveHeight);
    }

    gSHARPerfActivePass = -1;
    gSHARPerfActiveSlot = -1;
}

class SHARPerfPassScope
{
public:
    SHARPerfPassScope(int pass, int width, int height)
        : mActive(BeginSHARPerformancePass(pass, width, height))
    {
    }
    ~SHARPerfPassScope()
    {
        if (mActive)
            EndSHARPerformancePass();
    }
private:
    bool mActive;
    SHARPerfPassScope(const SHARPerfPassScope&);
    SHARPerfPassScope& operator=(const SHARPerfPassScope&);
};

static void PollSHARGpuTimerQueries()
{
    if (!EnsureSHARGpuTimerSupport())
        return;

    GLint disjoint = 0;
    glGetIntegerv(SHAR_GL_GPU_DISJOINT_EXT, &disjoint);
    if (disjoint != 0)
    {
        for (int pass = 0; pass < SHAR_PERF_PASS_COUNT; ++pass)
        {
            for (int slot = 0; slot < SHAR_PERF_QUERY_SLOTS; ++slot)
                gSHARGpuQuerySlots[pass][slot].pending = false;
        }
        SDL_Log("SHAR PERF GPU timers: discarded pending samples after a disjoint GPU timer interval.");
        return;
    }

    for (int pass = 0; pass < SHAR_PERF_PASS_COUNT; ++pass)
    {
        for (int slot = 0; slot < SHAR_PERF_QUERY_SLOTS; ++slot)
        {
            SHARGpuQuerySlot& query = gSHARGpuQuerySlots[pass][slot];
            if (!query.pending || query.id == 0)
                continue;

            GLuint available = 0;
            gSHARGetQueryObjectuivEXT(query.id, SHAR_GL_QUERY_RESULT_AVAILABLE_EXT, &available);
            if (available == 0)
                continue;

            unsigned long long nanoseconds = 0;
            gSHARGetQueryObjectui64vEXT(query.id, SHAR_GL_QUERY_RESULT_EXT, &nanoseconds);
            query.pending = false;
            RecordSHARPerfGpu(pass, nanoseconds, query.width, query.height);
        }
    }
}

static void RecordSHARSwapFrameInterval()
{
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    const Uint64 now = SDL_GetPerformanceCounter();
    if (frequency == 0)
        return;

    if (gSHARPerfLastSwapCounter != 0 && now >= gSHARPerfLastSwapCounter)
    {
        const Uint64 delta = now - gSHARPerfLastSwapCounter;
        gSHARPerfSwapTotalTicks += delta;
        if (delta > gSHARPerfSwapMaxTicks)
            gSHARPerfSwapMaxTicks = delta;
        gSHARPerfSwapSamples++;

        if (gSHARPerfSwapSamples >= 120)
        {
            const double averageMs = (double)gSHARPerfSwapTotalTicks * 1000.0 /
                ((double)frequency * (double)gSHARPerfSwapSamples);
            const double maximumMs = (double)gSHARPerfSwapMaxTicks * 1000.0 / (double)frequency;
            SDL_Log("SHAR PERF frame_interval avg=%.3fms max=%.3fms samples=%u internal=%dx%d",
                averageMs, maximumMs, gSHARPerfSwapSamples, gSHARFrameWidth, gSHARFrameHeight);
            gSHARPerfSwapTotalTicks = 0;
            gSHARPerfSwapMaxTicks = 0;
            gSHARPerfSwapSamples = 0;
        }
    }
    gSHARPerfLastSwapCounter = now;
}
#endif

static bool QuerySDLDrawableSize(int* width, int* height)
{
    if (width == NULL || height == NULL)
        return false;

    // Query the drawable through SDL rather than calling EGL directly. This
    // keeps the Pure3D static library linkable by Android's small SDL test
    // targets as well as the main game library.
    SDL_Window* currentWindow = SDL_GL_GetCurrentWindow();
    if (currentWindow == NULL)
        return false;

    int drawableWidth = 0;
    int drawableHeight = 0;
#if SDL_MAJOR_VERSION < 3
    SDL_GL_GetDrawableSize(currentWindow, &drawableWidth, &drawableHeight);
#else
    SDL_GetWindowSizeInPixels(currentWindow, &drawableWidth, &drawableHeight);
#endif
    if (drawableWidth <= 0 || drawableHeight <= 0)
        return false;

    *width = drawableWidth;
    *height = drawableHeight;
    return true;
}

static bool QueryEglSurfaceSize(int* width, int* height)
{
    if (width == NULL || height == NULL)
        return false;

    typedef EGLDisplay (*GetCurrentDisplayFn)(void);
    typedef EGLSurface (*GetCurrentSurfaceFn)(EGLint);
    typedef EGLBoolean (*QuerySurfaceFn)(EGLDisplay, EGLSurface, EGLint, EGLint*);

    static void* eglLibrary = dlopen("libEGL.so", RTLD_NOW);
    if (eglLibrary == NULL)
        return false;
    static GetCurrentDisplayFn getCurrentDisplay =
        reinterpret_cast<GetCurrentDisplayFn>(dlsym(eglLibrary, "eglGetCurrentDisplay"));
    static GetCurrentSurfaceFn getCurrentSurface =
        reinterpret_cast<GetCurrentSurfaceFn>(dlsym(eglLibrary, "eglGetCurrentSurface"));
    static QuerySurfaceFn querySurface =
        reinterpret_cast<QuerySurfaceFn>(dlsym(eglLibrary, "eglQuerySurface"));
    if (getCurrentDisplay == NULL || getCurrentSurface == NULL || querySurface == NULL)
        return false;

    EGLDisplay display = getCurrentDisplay();
    EGLSurface surface = getCurrentSurface(EGL_DRAW);
    if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE)
        return false;

    EGLint surfaceWidth = 0;
    EGLint surfaceHeight = 0;
    if (!querySurface(display, surface, EGL_WIDTH, &surfaceWidth) ||
        !querySurface(display, surface, EGL_HEIGHT, &surfaceHeight) ||
        surfaceWidth <= 0 || surfaceHeight <= 0)
    {
        return false;
    }

    *width = (int)surfaceWidth;
    *height = (int)surfaceHeight;
    return true;
}

static bool QueryDefaultFramebufferSize(int* width, int* height)
{
    // Query the actual output Surface independently of the internal FBO size.
    // Prefer EGL's active draw surface, with SDL as a fallback for startup/drivers.
    if (QueryEglSurfaceSize(width, height))
        return true;
    return QuerySDLDrawableSize(width, height);
}

static void LogSHARRenderTargetRejectedOnce(
    int width, int height, int reason, const char* message)
{
    if (gSHARLastRejectedWidth == width &&
        gSHARLastRejectedHeight == height &&
        gSHARLastRejectedReason == reason)
    {
        return;
    }

    gSHARLastRejectedWidth = width;
    gSHARLastRejectedHeight = height;
    gSHARLastRejectedReason = reason;
    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
        "SHAR Android render target %dx%d skipped: %s",
        width, height, message ? message : "unsupported size");
}


static bool CheckSHARRenderTargetSize(
    int width, int height, const char* label,
    int reasonCode, bool needsRenderbuffer, bool checkBudget)
{
    if (width <= 0 || height <= 0)
        return false;

    GLint maxTextureSize = 0;
    GLint maxRenderbufferSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);

    if (maxTextureSize <= 0 ||
        width > maxTextureSize || height > maxTextureSize)
    {
        char message[192];
        snprintf(message, sizeof(message),
            "%s exceeds GL_MAX_TEXTURE_SIZE=%d",
            label ? label : "target", (int)maxTextureSize);
        LogSHARRenderTargetRejectedOnce(
            width, height, reasonCode, message);
        return false;
    }

    if (needsRenderbuffer)
    {
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbufferSize);
        if (maxRenderbufferSize <= 0 ||
            width > maxRenderbufferSize || height > maxRenderbufferSize)
        {
            char message[192];
            snprintf(message, sizeof(message),
                "%s exceeds GL_MAX_RENDERBUFFER_SIZE=%d",
                label ? label : "target", (int)maxRenderbufferSize);
            LogSHARRenderTargetRejectedOnce(
                width, height, reasonCode, message);
            return false;
        }
    }

    if (checkBudget)
    {
        const size_t pixelWidth = (size_t)width;
        const size_t pixelHeight = (size_t)height;
        if (pixelHeight > ((size_t)-1) / pixelWidth)
        {
            LogSHARRenderTargetRejectedOnce(
                width, height, reasonCode,
                "pixel-count calculation overflowed");
            return false;
        }

        const size_t pixels = pixelWidth * pixelHeight;
        if (pixels > ((size_t)-1) / 14)
        {
            LogSHARRenderTargetRejectedOnce(
                width, height, reasonCode,
                "post-process memory estimate overflowed");
            return false;
        }

        size_t estimatedBytes = 0;
        const bool bloomEnabled = IsSHARBloomEnabled();
        const bool sharpeningEnabled = IsFrameSharpeningEnabled();

        // The offscreen scene target (RGBA color + 16-bit depth) is used
        // regardless of Cel Shading; bloom targets are budgeted independently.
        estimatedBytes += pixels * 6;
        if (bloomEnabled)
        {
            const size_t bloomWidth = width > 3 ? (size_t)(width / 4) : 1;
            const size_t bloomHeight = height > 3 ? (size_t)(height / 4) : 1;
            estimatedBytes += bloomWidth * bloomHeight * 8;
        }

        // Count full-screen copies as four bytes per pixel to allow for drivers
        // that pad RGB allocations internally. The global saturation pass shares
        // its texture with CRT, so reserve one copy even when CRT is disabled.
        // If the budget is tight, sharpening is bypassed first.
        if (reasonCode == 5 && sharpeningEnabled)
            estimatedBytes += pixels * 4;

        if (reasonCode == 5 || reasonCode == 6)
            estimatedBytes += pixels * 4;

        if (estimatedBytes > SHAR_POSTPROCESS_BUDGET_BYTES)
        {
            char message[192];
            snprintf(message, sizeof(message),
                "%s would exceed the %u MiB auxiliary post-process budget (estimate=%u MiB)",
                label ? label : "target",
                (unsigned)(SHAR_POSTPROCESS_BUDGET_BYTES / (1024 * 1024)),
                (unsigned)(estimatedBytes / (1024 * 1024)));
            LogSHARRenderTargetRejectedOnce(
                width, height, reasonCode, message);
            return false;
        }
    }

    return true;
}

static bool CheckSHARAllocationError(
    const char* label, int width, int height)
{
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR)
        return true;

    // Drain the remaining error flags after recording the first failure so
    // subsequent GL work can proceed on the direct/default framebuffer.
    GLenum pendingError = GL_NO_ERROR;
    while ((pendingError = glGetError()) != GL_NO_ERROR)
    {
    }

    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
        "SHAR Android allocation failed for %s (%dx%d), GL error=0x%04x; bypassing that effect",
        label ? label : "post-process target",
        width, height, (unsigned)error);
    return false;
}

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

static void InvalidateCelRenderTargetValidationCache()
{
    gCelRenderTargetValidationCached = false;
}

static bool EnsureCelRenderTarget(int width, int height)
{
    // The budget check depends on whether bloom is enabled, so a runtime toggle
    // invalidates this cache entry even if the render dimensions are unchanged.
    const bool bloomEnabled =
        (width > 0 && height > 0) ? IsSHARBloomEnabled() : false;
    const GLuint depthObject =
        gCelDepthTextureActive ? gCelRenderDepthTexture : gCelRenderDepth;
    const bool depthStorageMatches =
        gCelDepthTextureActive
            ? (gCelRenderDepthTexture != 0 &&
               gCelDepthTextureWidth == width &&
               gCelDepthTextureHeight == height)
            : (gCelRenderDepth != 0 &&
               gCelRenderbufferWidth == width &&
               gCelRenderbufferHeight == height);

#if !defined(RAD_DEBUG) && !defined(SHAR_FORCE_CEL_RENDER_TARGET_REVALIDATION)
    // Release fast path: reuse the known-good attachments rather than repeating
    // GL setup, completeness checks, hardware-limit queries and error polling.
    // Define SHAR_FORCE_CEL_RENDER_TARGET_REVALIDATION (or build RAD_DEBUG) to
    // retain full validation every frame while diagnosing a rendering problem.
    if (width > 0 && height > 0 &&
        gCelRenderTargetValidationCached &&
        gCelRenderFboReady &&
        gCelRenderTargetValidatedWidth == width &&
        gCelRenderTargetValidatedHeight == height &&
        gCelPostWidth == width && gCelPostHeight == height &&
        gCelRenderTargetValidatedBloomEnabled == bloomEnabled &&
        gCelRenderTargetValidatedUsesDepthTexture == gCelDepthTextureActive &&
        gCelRenderTargetValidatedFbo == gCelRenderFbo &&
        gCelRenderTargetValidatedColourTexture == gCelPostTexture &&
        gCelRenderTargetValidatedDepthObject == depthObject &&
        depthStorageMatches &&
        !(gCelPostFailedWidth == width && gCelPostFailedHeight == height) &&
        !(gCelRenderbufferFailedWidth == width && gCelRenderbufferFailedHeight == height) &&
        !(gCelRenderFboFailedWidth == width && gCelRenderFboFailedHeight == height))
    {
        return true;
    }
#endif

    // Any non-fast-path call performs the original size, allocation, attachment
    // and framebuffer validation below before it can repopulate the cache.
    InvalidateCelRenderTargetValidationCache();

    if (!CheckSHARRenderTargetSize(
            width, height, "cel render target", 2, true, true))
    {
        gCelRenderFboReady = false;
        return false;
    }

    if ((gCelRenderbufferFailedWidth == width &&
         gCelRenderbufferFailedHeight == height) ||
        (gCelRenderFboFailedWidth == width &&
         gCelRenderFboFailedHeight == height))
    {
        gCelRenderFboReady = false;
        return false;
    }

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

    // Preserve depth-aware outline fading at 2160p by using a compact 16-bit
    // depth texture for targets above four megapixels. This matches the nominal
    // storage of the existing GL_DEPTH_COMPONENT16 fallback renderbuffer.
    const size_t renderPixels = (size_t)width * (size_t)height;
    // Use the depth texture for the normal (cel-shading-off) render path too,
    // when supported. This isolates distant depth-precision artefacts without
    // enabling the cel-shading fullscreen effect itself.
    const bool allowDepthTexture = true;
    const GLenum depthTextureType = renderPixels > (size_t)4 * 1024 * 1024
        ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    gCelDepthTextureActive = false;
    bool depthTextureReady = false;
    if (gCelDepthTextureExtensionAvailable &&
        !gCelDepthTextureFailed && allowDepthTexture)
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

            bool depthStorageReady = true;
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
                    depthTextureType,
                    NULL
                );
                depthStorageReady = CheckSHARAllocationError(
                    "cel depth texture", width, height);
                if (depthStorageReady)
                {
                    gCelDepthTextureWidth = width;
                    gCelDepthTextureHeight = height;
                }
                else
                {
                    gCelDepthTextureFailed = true;
                }
            }

            if (depthStorageReady)
            {
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
        bool depthRenderbufferReady = true;
        glBindRenderbuffer(GL_RENDERBUFFER, gCelRenderDepth);
        if (gCelRenderbufferWidth != width || gCelRenderbufferHeight != height)
        {
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
            depthRenderbufferReady = CheckSHARAllocationError(
                "cel depth renderbuffer", width, height);
            if (depthRenderbufferReady)
            {
                gCelRenderbufferWidth = width;
                gCelRenderbufferHeight = height;
                gCelRenderbufferFailedWidth = 0;
                gCelRenderbufferFailedHeight = 0;
            }
            else
            {
                gCelRenderbufferWidth = 0;
                gCelRenderbufferHeight = 0;
                gCelRenderbufferFailedWidth = width;
                gCelRenderbufferFailedHeight = height;
            }
        }

        if (depthRenderbufferReady)
        {
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
        else
        {
            gCelRenderFboReady = false;
        }
    }

    // Validate the final attachment combination after all size-dependent
    // resources have been attached. Earlier branch-specific checks do not show
    // which objects are attached when scene rendering begins.
    const GLenum finalFramebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    gCelRenderFboReady =
        gCelRenderFboReady &&
        finalFramebufferStatus == GL_FRAMEBUFFER_COMPLETE;

    GLint colorAttachmentType = 0;
    GLint colorAttachmentName = 0;
    GLint depthAttachmentType = 0;
    GLint depthAttachmentName = 0;
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorAttachmentType);
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &colorAttachmentName);
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depthAttachmentType);
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depthAttachmentName);

    GLint maxTextureSize = 0;
    GLint maxRenderbufferSize = 0;
    GLint currentViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbufferSize);
    glGetIntegerv(GL_VIEWPORT, currentViewport);
    int onscreenWidth = 0;
    int onscreenHeight = 0;
    const bool onscreenSizeKnown =
        QueryDefaultFramebufferSize(&onscreenWidth, &onscreenHeight);

    GLenum targetError = glGetError();
    GLenum pendingTargetError = GL_NO_ERROR;
    while ((pendingTargetError = glGetError()) != GL_NO_ERROR)
    {
        if (targetError == GL_NO_ERROR)
            targetError = pendingTargetError;
    }


    if (gCelRenderFboReady)
    {
        gCelRenderFboFailedWidth = 0;
        gCelRenderFboFailedHeight = 0;
    }
    else
    {
        gCelRenderFboFailedWidth = width;
        gCelRenderFboFailedHeight = height;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)previousRenderbuffer);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture2D);

    if (gCelRenderFboReady)
    {
        gCelRenderTargetValidationCached = true;
        gCelRenderTargetValidatedWidth = width;
        gCelRenderTargetValidatedHeight = height;
        gCelRenderTargetValidatedBloomEnabled = bloomEnabled;
        gCelRenderTargetValidatedUsesDepthTexture = gCelDepthTextureActive;
        gCelRenderTargetValidatedFbo = gCelRenderFbo;
        gCelRenderTargetValidatedColourTexture = gCelPostTexture;
        gCelRenderTargetValidatedDepthObject =
            gCelDepthTextureActive ? gCelRenderDepthTexture : gCelRenderDepth;
    }
    else
    {
        InvalidateCelRenderTargetValidationCache();
    }

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

    if (gCelPostFailedWidth == width && gCelPostFailedHeight == height)
        return false;

    if (!CheckSHARRenderTargetSize(
            width, height, "cel color texture", 1, false, true))
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
            "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
            "precision highp float;\n"
            "#else\n"
            "precision mediump float;\n"
            "#endif\n"
            "uniform sampler2D sceneTex;\n"
            "uniform sampler2D bloomTex;\n"
            "uniform float bloomStrength;\n"
            "uniform sampler2D depthTex;\n"
            "uniform float depthTextureAvailable;\n"
            "uniform float depthEdgeDetectionAvailable;\n"
            "uniform float cloudShadowEnabled;\n"
            "uniform float cloudShadowTime;\n"
            "uniform float cloudShadowStrengthMultiplier;\n"
            "uniform vec2 texelSize;\n"
            "uniform vec2 outlineTexelSize;\n"
            "uniform float fineDetailEnabled;\n"
            "uniform float celEffectsEnabled;\n"
            "uniform float motionBlurEnabled;\n"
            "uniform float motionBlurCameraValid;\n"
            "uniform float motionBlurTimeScale;\n"
            "uniform vec3 currentCameraPosition;\n"
            "uniform vec3 currentCameraRight;\n"
            "uniform vec3 currentCameraUp;\n"
            "uniform vec3 currentCameraForward;\n"
            "uniform vec3 previousCameraPosition;\n"
            "uniform vec3 previousCameraRight;\n"
            "uniform vec3 previousCameraUp;\n"
            "uniform vec3 previousCameraForward;\n"
            "uniform float currentTanHalfFovX;\n"
            "uniform float currentTanHalfFovY;\n"
            "uniform float previousTanHalfFovX;\n"
            "uniform float previousTanHalfFovY;\n"
            "uniform float currentNearPlane;\n"
            "uniform float currentFarPlane;\n"
            "uniform float previousNearPlane;\n"
            "uniform float characterMaskEnabled;\n"
            "uniform vec3 characterMaskPosition;\n"
            "uniform vec2 characterMaskSize;\n"
            "uniform float vehicleMaskEnabled;\n"
            "uniform vec3 vehicleMaskPosition;\n"
            "uniform vec2 vehicleMaskSize;\n"
            "uniform float vehicleMotionIntensity;\n"
            "varying vec2 texcoord;\n"
            "\n"
            "float sceneLuma(vec3 colour) {\n"
            "    return dot(colour, vec3(0.299, 0.587, 0.114));\n"
            "}\n"
            "\n"
            "// Convert the nonlinear OpenGL depth-buffer value to positive view-space distance.\n"
            "float linearViewDepth(float depth) {\n"
            "    float nearPlane = currentNearPlane;\n"
            "    float farPlane = currentFarPlane;\n"
            "    float denominator = farPlane - depth * (farPlane - nearPlane);\n"
            "    if (nearPlane <= 0.0 || farPlane <= nearPlane || denominator <= 0.00001) return 0.0;\n"
            "    return (nearPlane * farPlane) / denominator;\n"
            "}\n"
            "\n"
            "// Cheap interpolated value noise; coordinates are reconstructed in world space,\n"
            "// so the shadows move over the level instead of sliding with the camera.\n"
            "float cloudHash(vec2 p) {\n"
            "    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));\n"
            "    p3 += dot(p3, p3.yxz + 33.33);\n"
            "    return fract((p3.x + p3.y) * p3.z);\n"
            "}\n"
            "float cloudNoise(vec2 p) {\n"
            "    vec2 cell = floor(p);\n"
            "    vec2 local = fract(p);\n"
            "    local = local * local * (3.0 - 2.0 * local);\n"
            "    float a = cloudHash(cell);\n"
            "    float b = cloudHash(cell + vec2(1.0, 0.0));\n"
            "    float c = cloudHash(cell + vec2(0.0, 1.0));\n"
            "    float d = cloudHash(cell + vec2(1.0, 1.0));\n"
            "    return mix(mix(a, b, local.x), mix(c, d, local.x), local.y);\n"
            "}\n"
            "vec3 applyWorldCloudShadow(vec2 uv, vec3 baseColour) {\n"
            "    if (cloudShadowEnabled < 0.5 || depthTextureAvailable < 0.5) return baseColour;\n"
            "    float depthValue = texture2D(depthTex, uv).r;\n"
            "    // Reject only clear/background depth. Preserve distant world surfaces so the\n"
            "    // cloud field stays visible well ahead of the player/camera.\n"
            "    if (depthValue >= 0.999999 || currentNearPlane <= 0.0 || currentFarPlane <= currentNearPlane) return baseColour;\n"
            "    float depthNdc = depthValue * 2.0 - 1.0;\n"
            "    float depthDenominator = currentFarPlane + currentNearPlane - depthNdc * (currentFarPlane - currentNearPlane);\n"
            "    float viewZ = (2.0 * currentNearPlane * currentFarPlane) / max(depthDenominator, 0.0001);\n"
            "    vec2 ndc = uv * 2.0 - 1.0;\n"
            "    vec3 viewPosition = vec3(ndc.x * viewZ * currentTanHalfFovX, ndc.y * viewZ * currentTanHalfFovY, viewZ);\n"
            "    vec3 worldPosition = currentCameraPosition + currentCameraRight * viewPosition.x + currentCameraUp * viewPosition.y + currentCameraForward * viewPosition.z;\n"
            "    // Keep most of the level sunny; only the denser, broad cloud regions cast shadows.\n"
            "    // Motion is 15x the initial prototype speed (3x the previous test).\n"
            "    vec2 cloudCoordinates = worldPosition.xz * 0.012 + cloudShadowTime * vec2(0.0750, -0.0555);\n"
            "    float cloudDensity = cloudNoise(cloudCoordinates);\n"
            "    float shadowCoverage = smoothstep(0.62, 0.78, cloudDensity);\n"
            "    // Occasional cloud shadows remain clearly noticeable without making the level overcast.\n"
            "    return baseColour * (1.0 - shadowCoverage * 0.28 * cloudShadowStrengthMultiplier);\n"
            "}\n"
            "\n"
            "float randomNoise(vec2 p) {\n"
            "    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;\n"
            "}\n"
            "\n"
            "float protectMotionBlurSubject(vec2 uv, float sceneViewZ, vec3 subjectPosition, vec2 halfSize, float enabled) {\n"
            "    if (enabled < 0.5) return 0.0;\n"
            "    vec3 fromCamera = subjectPosition - currentCameraPosition;\n"
            "    float subjectZ = dot(fromCamera, currentCameraForward);\n"
            "    if (subjectZ <= currentNearPlane) return 0.0;\n"
            "    // Bound depth protection to the registered subject's projected world-space extent.\n"
            "    // This prevents foreground road/scenery inside the vehicle ellipse from being kept sharp.\n"
            "    float depthExtent = halfSize.x * sqrt(currentCameraForward.x * currentCameraForward.x + currentCameraForward.z * currentCameraForward.z) + halfSize.y * abs(currentCameraForward.y);\n"
            "    float nearDepthLimit = subjectZ - depthExtent - 0.45;\n"
            "    float farDepthLimit = subjectZ + depthExtent + 0.55;\n"
            "    float depthFadeIn = smoothstep(nearDepthLimit - 0.35, nearDepthLimit + 0.35, sceneViewZ);\n"
            "    float depthFadeOut = 1.0 - smoothstep(farDepthLimit - 0.35, farDepthLimit + 0.35, sceneViewZ);\n"
            "    float depthProtection = depthFadeIn * depthFadeOut;\n"
            "    vec2 subjectNdc = vec2(dot(fromCamera, currentCameraRight) / (subjectZ * currentTanHalfFovX), dot(fromCamera, currentCameraUp) / (subjectZ * currentTanHalfFovY));\n"
            "    vec2 subjectUv = subjectNdc * 0.5 + 0.5;\n"
            "    float halfWidthInCameraRight = sqrt(halfSize.x * halfSize.x * (currentCameraRight.x * currentCameraRight.x + currentCameraRight.z * currentCameraRight.z) + halfSize.y * halfSize.y * currentCameraRight.y * currentCameraRight.y);\n"
            "    float halfHeightInCameraUp = sqrt(halfSize.x * halfSize.x * (currentCameraUp.x * currentCameraUp.x + currentCameraUp.z * currentCameraUp.z) + halfSize.y * halfSize.y * currentCameraUp.y * currentCameraUp.y);\n"
            "    vec2 subjectRadius = vec2(halfWidthInCameraRight / (subjectZ * currentTanHalfFovX), halfHeightInCameraUp / (subjectZ * currentTanHalfFovY)) * 0.5;\n"
            "    subjectRadius = max(subjectRadius, texelSize * 3.0);\n"
            "    // Pad the projected bounds slightly so the outline and extremities stay sharp too.\n"
            "    subjectRadius += texelSize * 5.0;\n"
            "    float ellipseDistance = length((uv - subjectUv) / subjectRadius);\n"
            "    float projectedMask = 1.0 - smoothstep(0.90, 1.30, ellipseDistance);\n"
            "    return projectedMask * depthProtection;\n"
            "}\n"
            "\n"
            "float rawMotionBlurSubjectMask(vec2 uv, vec3 subjectPosition, vec2 halfSize, float enabled) {\n"
            "    if (enabled < 0.5) return 0.0;\n"
            "    vec3 fromCamera = subjectPosition - currentCameraPosition;\n"
            "    float subjectZ = dot(fromCamera, currentCameraForward);\n"
            "    if (subjectZ <= currentNearPlane) return 0.0;\n"
            "    vec2 subjectNdc = vec2(dot(fromCamera, currentCameraRight) / (subjectZ * currentTanHalfFovX), dot(fromCamera, currentCameraUp) / (subjectZ * currentTanHalfFovY));\n"
            "    vec2 subjectUv = subjectNdc * 0.5 + 0.5;\n"
            "    float halfWidthInCameraRight = sqrt(halfSize.x * halfSize.x * (currentCameraRight.x * currentCameraRight.x + currentCameraRight.z * currentCameraRight.z) + halfSize.y * halfSize.y * currentCameraRight.y * currentCameraRight.y);\n"
            "    float halfHeightInCameraUp = sqrt(halfSize.x * halfSize.x * (currentCameraUp.x * currentCameraUp.x + currentCameraUp.z * currentCameraUp.z) + halfSize.y * halfSize.y * currentCameraUp.y * currentCameraUp.y);\n"
            "    vec2 subjectRadius = vec2(halfWidthInCameraRight / (subjectZ * currentTanHalfFovX), halfHeightInCameraUp / (subjectZ * currentTanHalfFovY)) * 0.5;\n"
            "    subjectRadius = max(subjectRadius, texelSize * 3.0);\n"
            "    subjectRadius += texelSize * 5.0;\n"
            "    float ellipseDistance = length((uv - subjectUv) / subjectRadius);\n"
            "    return 1.0 - smoothstep(0.90, 1.30, ellipseDistance);\n"
            "}\n"
            "float vehicleRoadTrailMask(vec2 uv) {\n"
            "    if (vehicleMaskEnabled < 0.5 || vehicleMotionIntensity < 0.01) return 0.0;\n"
            "    vec2 vehicleSize = vehicleMaskSize * vec2(1.20, 1.15);\n"
            "    vec3 fromCamera = vehicleMaskPosition - currentCameraPosition;\n"
            "    float vehicleZ = dot(fromCamera, currentCameraForward);\n"
            "    if (vehicleZ <= currentNearPlane) return 0.0;\n"
            "    vec2 vehicleNdc = vec2(dot(fromCamera, currentCameraRight) / (vehicleZ * currentTanHalfFovX), dot(fromCamera, currentCameraUp) / (vehicleZ * currentTanHalfFovY));\n"
            "    vec2 vehicleUv = vehicleNdc * 0.5 + 0.5;\n"
            "    float halfWidthInCameraRight = sqrt(vehicleSize.x * vehicleSize.x * (currentCameraRight.x * currentCameraRight.x + currentCameraRight.z * currentCameraRight.z) + vehicleSize.y * vehicleSize.y * currentCameraRight.y * currentCameraRight.y);\n"
            "    float halfHeightInCameraUp = sqrt(vehicleSize.x * vehicleSize.x * (currentCameraUp.x * currentCameraUp.x + currentCameraUp.z * currentCameraUp.z) + vehicleSize.y * vehicleSize.y * currentCameraUp.y * currentCameraUp.y);\n"
            "    vec2 vehicleRadius = vec2(halfWidthInCameraRight / (vehicleZ * currentTanHalfFovX), halfHeightInCameraUp / (vehicleZ * currentTanHalfFovY)) * 0.5;\n"
            "    vehicleRadius = max(vehicleRadius, texelSize * 3.0) + texelSize * 5.0;\n"
            "    // Trail starts at the rear of the car, widens toward both lower screen edges,\n"
            "    // and softly loses strength across the fan instead of using a flat-edged band.\n"
            "    float distanceBelowCar = vehicleUv.y - uv.y;\n"
            "    float trailStart = vehicleRadius.y * 2.25;\n"
            "    float trailLength = max(vehicleUv.y - trailStart, texelSize.y * 12.0);\n"
            "    float trailProgress = clamp((distanceBelowCar - trailStart) / trailLength, 0.0, 1.0);\n"
            "    float verticalFade = smoothstep(trailStart - texelSize.y * 5.0, trailStart + texelSize.y * 5.0, distanceBelowCar);\n"
            "    float curvedProgress = smoothstep(0.0, 0.95, trailProgress);\n"
            "    float baseSideWidth = max(vehicleRadius.x * 0.72, texelSize.x * 6.0);\n"
            "    float leftWidth = mix(baseSideWidth, max(vehicleUv.x, texelSize.x * 4.0), curvedProgress);\n"
            "    float rightWidth = mix(baseSideWidth, max(1.0 - vehicleUv.x, texelSize.x * 4.0), curvedProgress);\n"
            "    float sideDistance = uv.x < vehicleUv.x ? (vehicleUv.x - uv.x) / leftWidth : (uv.x - vehicleUv.x) / rightWidth;\n"
            "    float sideFalloff = 1.0 - smoothstep(0.0, 1.65, sideDistance);\n"
            "    float lowerScreenFade = 1.0 - 0.12 * smoothstep(0.0, 1.0, trailProgress);\n"
            "    float trailShape = verticalFade * sideFalloff * lowerScreenFade;\n"
            "    float vehicleBodyMask = rawMotionBlurSubjectMask(uv, vehicleMaskPosition, vehicleSize, vehicleMaskEnabled);\n"
            "    return trailShape * (1.0 - vehicleBodyMask);\n"
            "}\n"
            "\n"            "vec3 applyVehicleRoadTrail(vec2 uv, vec3 baseColour) {\n"
            "    if (vehicleMaskEnabled < 0.5 || vehicleMotionIntensity < 0.01) return baseColour;\n"
            "    vec3 fromCamera = vehicleMaskPosition - currentCameraPosition;\n"
            "    float vehicleZ = dot(fromCamera, currentCameraForward);\n"
            "    if (vehicleZ <= currentNearPlane) return baseColour;\n"
            "    vec2 vehicleNdc = vec2(dot(fromCamera, currentCameraRight) / (vehicleZ * currentTanHalfFovX), dot(fromCamera, currentCameraUp) / (vehicleZ * currentTanHalfFovY));\n"
            "    vec2 vehicleUv = vehicleNdc * 0.5 + 0.5;\n"
            "    vec2 towardVehiclePixels = (vehicleUv - uv) / texelSize;\n"
            "    float towardVehicleLength = max(length(towardVehiclePixels), 1.0);\n"
            "    vec2 trailStep = (towardVehiclePixels / towardVehicleLength) * texelSize * mix(10.0, 34.0, vehicleMotionIntensity);\n"
            "    float celAwareTrailStrength = celEffectsEnabled > 0.5 ? 0.90 : 0.60;\n"
            "    float roadTrailWeight = vehicleRoadTrailMask(uv) * vehicleMotionIntensity * celAwareTrailStrength;\n"
            "    if (roadTrailWeight <= 0.001) return baseColour;\n"
            "    // Sample back toward the car along the fan, creating a visible directional road streak.\n"
            "    vec3 roadTrailColour = baseColour * 0.28;\n"
            "    roadTrailColour += texture2D(sceneTex, uv + trailStep * 0.25).rgb * 0.20;\n"
            "    roadTrailColour += texture2D(sceneTex, uv + trailStep * 0.50).rgb * 0.20;\n"
            "    roadTrailColour += texture2D(sceneTex, uv + trailStep * 0.75).rgb * 0.18;\n"
            "    roadTrailColour += texture2D(sceneTex, uv + trailStep).rgb * 0.14;\n"
            "    return mix(baseColour, roadTrailColour, roadTrailWeight);\n"
            "}\n"
            "\n"
            "vec3 applyCameraMotionBlur(vec2 uv, vec3 centreColour) {\n"
            "    if (motionBlurEnabled < 0.5 || depthTextureAvailable < 0.5) return centreColour;\n"
            "    // The vehicle subject is only registered while the player is driving.\n"
            "    if (vehicleMaskEnabled < 0.5) return centreColour;\n"
            "    float depthValue = texture2D(depthTex, uv).r;\n"
            "    if (depthValue >= 0.9995) return centreColour;\n"
            "    if (motionBlurCameraValid < 0.5) return applyVehicleRoadTrail(uv, centreColour);\n"
            "    float depthNdc = depthValue * 2.0 - 1.0;\n"
            "    float depthDenominator = currentFarPlane + currentNearPlane - depthNdc * (currentFarPlane - currentNearPlane);\n"
            "    float viewZ = (2.0 * currentNearPlane * currentFarPlane) / max(depthDenominator, 0.0001);\n"
            "    vec2 ndc = uv * 2.0 - 1.0;\n"
            "    vec3 currentViewPosition = vec3(ndc.x * viewZ * currentTanHalfFovX, ndc.y * viewZ * currentTanHalfFovY, viewZ);\n"
            "    vec3 worldPosition = currentCameraPosition + currentCameraRight * currentViewPosition.x + currentCameraUp * currentViewPosition.y + currentCameraForward * currentViewPosition.z;\n"
            "    vec3 fromPreviousCamera = worldPosition - previousCameraPosition;\n"
            "    float previousZ = dot(fromPreviousCamera, previousCameraForward);\n"
            "    if (previousZ <= previousNearPlane) return applyVehicleRoadTrail(uv, centreColour);\n"
            "    vec2 previousNdc = vec2(dot(fromPreviousCamera, previousCameraRight) / (previousZ * previousTanHalfFovX), dot(fromPreviousCamera, previousCameraUp) / (previousZ * previousTanHalfFovY));\n"
            "    vec2 previousUv = previousNdc * 0.5 + 0.5;\n"
            "    if (previousUv.x < -0.12 || previousUv.x > 1.12 || previousUv.y < -0.12 || previousUv.y > 1.12) return applyVehicleRoadTrail(uv, centreColour);\n"
            "    vec2 motion = uv - previousUv;\n"
            "    float motionPixels = length(motion / texelSize) * motionBlurTimeScale;\n"
            "    float edgeDistance = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));\n"
            "    float edgeProximity = 1.0 - smoothstep(0.05, 0.30, edgeDistance);\n"
            "    float edgeBlurBoost = 1.0 + 0.80 * edgeProximity;\n"
            "    float motionAmount = smoothstep(0.10, 0.90, motionPixels);\n"
            "    if (motionAmount <= 0.001 && vehicleMotionIntensity <= 0.01) return centreColour;\n"
            "    vec2 maxOffset = texelSize * mix(12.0, 20.0, edgeProximity);\n"
            "    vec2 blurOffset = clamp(motion * motionBlurTimeScale * edgeBlurBoost, -maxOffset, maxOffset);\n"
            "    vec3 blurred = centreColour * 0.40;\n"
            "    // Bias the same blur weight toward the previous screen position so passing scenery gets a visible directional trail.\n"
            "    blurred += texture2D(sceneTex, clamp(uv - blurOffset * 0.25, texelSize * 0.5, vec2(1.0) - texelSize * 0.5)).rgb * 0.20;\n"
            "    blurred += texture2D(sceneTex, clamp(uv - blurOffset * 0.50, texelSize * 0.5, vec2(1.0) - texelSize * 0.5)).rgb * 0.20;\n"
            "    blurred += texture2D(sceneTex, clamp(uv - blurOffset * 0.75, texelSize * 0.5, vec2(1.0) - texelSize * 0.5)).rgb * 0.10;\n"
            "    blurred += texture2D(sceneTex, clamp(uv - blurOffset, texelSize * 0.5, vec2(1.0) - texelSize * 0.5)).rgb * 0.10;\n"
            "    // Character materials do not consistently contribute depth, so use the projected player bounds directly.\n"
            "    float characterProtection = rawMotionBlurSubjectMask(uv, characterMaskPosition, characterMaskSize, characterMaskEnabled);\n"
            "    // Add a small horizontal allowance so the tail stays mostly protected without greatly widening the mask.\n"
            "    float vehicleProtection = protectMotionBlurSubject(uv, viewZ, vehicleMaskPosition, vehicleMaskSize * vec2(1.20, 1.15), vehicleMaskEnabled);\n"
            "    float subjectProtection = max(characterProtection, vehicleProtection);\n"
            "    // Fade camera-panning blur out before the screen midpoint; the road-trail fan is separate.\n"
            "    float lowerScreenMotionBlur = 1.0 - smoothstep(0.36, 0.50, uv.y);\n"
            "    float celOffCompensation = celEffectsEnabled < 0.5 ? 0.78 : 1.0;\n"
            "    vec3 baseColour = mix(centreColour, blurred, clamp(motionAmount * celOffCompensation * edgeBlurBoost * lowerScreenMotionBlur, 0.0, 1.0) * (1.0 - subjectProtection));\n"
            "    return applyVehicleRoadTrail(uv, baseColour);\n"
            "}\n"
            "\n"
            "void main() {\n"
            "    vec4 scene = texture2D(sceneTex, texcoord);\n"
            "    scene.rgb = applyCameraMotionBlur(texcoord, scene.rgb);\n"
            "    vec3 finalColour = scene.rgb;\n"
            "    if (celEffectsEnabled > 0.5) {\n"
            "\n"
            "    // Existing screen-space cel outline detection.\n"
            "    float centreLuma = sceneLuma(scene.rgb);\n"
            "    float leftLuma  = sceneLuma(texture2D(sceneTex, texcoord - vec2(outlineTexelSize.x, 0.0)).rgb);\n"
            "    float rightLuma = sceneLuma(texture2D(sceneTex, texcoord + vec2(outlineTexelSize.x, 0.0)).rgb);\n"
            "    float upLuma    = sceneLuma(texture2D(sceneTex, texcoord + vec2(0.0, outlineTexelSize.y)).rgb);\n"
            "    float downLuma  = sceneLuma(texture2D(sceneTex, texcoord - vec2(0.0, outlineTexelSize.y)).rgb);\n"
            "    float edgeStrength = max(max(abs(centreLuma - leftLuma), abs(centreLuma - rightLuma)),\n"
            "                             max(abs(centreLuma - upLuma), abs(centreLuma - downLuma)));\n"
            "    float edge = smoothstep(0.10, 0.22, edgeStrength);\n"
            "    float characterMask = smoothstep(0.0, 0.5, scene.a);\n"
            "    float faceColourDetailEdge = 0.0;\n"
            "    float depthOutlineEdge = 0.0;\n"
            "    float fineDetailEdge = 0.0;\n"
            "    // Reuse the per-pixel character alpha mask for the recovered low-threshold facial-detail signal.\n"
            "    // This avoids the older CPU-side face-bound recognition and adds no texture samples.\n"
            "    if (characterMask > 0.001 && edgeStrength > 0.024) {\n"
            "        faceColourDetailEdge = smoothstep(0.024, 0.082, edgeStrength) * characterMask * 0.60;\n"
            "    }\n"
            "    // Recover tiny colour edges only above 1080p, independently of the broad outline sampling scale.\n"
            "    if (fineDetailEnabled > 0.5) {\n"
            "        float fineCentreLuma = sceneLuma(texture2D(sceneTex, texcoord).rgb);\n"
            "        float fineLeftLuma = sceneLuma(texture2D(sceneTex, texcoord - vec2(texelSize.x, 0.0)).rgb);\n"
            "        float fineRightLuma = sceneLuma(texture2D(sceneTex, texcoord + vec2(texelSize.x, 0.0)).rgb);\n"
            "        float fineUpLuma = sceneLuma(texture2D(sceneTex, texcoord + vec2(0.0, texelSize.y)).rgb);\n"
            "        float fineDownLuma = sceneLuma(texture2D(sceneTex, texcoord - vec2(0.0, texelSize.y)).rgb);\n"
            "        float fineUpLeftLuma = sceneLuma(texture2D(sceneTex, texcoord + vec2(-texelSize.x, texelSize.y)).rgb);\n"
            "        float fineUpRightLuma = sceneLuma(texture2D(sceneTex, texcoord + vec2(texelSize.x, texelSize.y)).rgb);\n"
            "        float fineDownLeftLuma = sceneLuma(texture2D(sceneTex, texcoord + vec2(-texelSize.x, -texelSize.y)).rgb);\n"
            "        float fineDownRightLuma = sceneLuma(texture2D(sceneTex, texcoord + vec2(texelSize.x, -texelSize.y)).rgb);\n"
            "        float fineEdgeStrength = max(max(abs(fineCentreLuma - fineLeftLuma), abs(fineCentreLuma - fineRightLuma)),\n"
            "            max(max(abs(fineCentreLuma - fineUpLuma), abs(fineCentreLuma - fineDownLuma)),\n"
            "            max(max(abs(fineCentreLuma - fineUpLeftLuma), abs(fineCentreLuma - fineUpRightLuma)),\n"
            "                max(abs(fineCentreLuma - fineDownLeftLuma), abs(fineCentreLuma - fineDownRightLuma)))));\n"
            "        fineDetailEdge = smoothstep(0.12, 0.26, fineEdgeStrength);\n"
            "    }\n"
            "    // Fade screen-space outlines by actual view-space distance, not nonlinear depth-buffer values.\n"
            "    float distanceFade = 0.0;\n"
            "    if (depthTextureAvailable > 0.5) {\n"
            "        float sceneDepthValue = texture2D(depthTex, texcoord).r;\n"
            "        float sceneViewDepth = linearViewDepth(sceneDepthValue);\n"
            "        distanceFade = smoothstep(25.0, 80.0, sceneViewDepth);\n"
            "    }\n"
            "    // Use depth discontinuities to recover geometric creases that flat toon colours hide.\n"
            "    // Only mark the nearer side of an edge to reduce double-thick outlines.\n"
            "    if (depthEdgeDetectionAvailable > 0.5 && depthTextureAvailable > 0.5) {\n"
            "        float centreDepth = linearViewDepth(texture2D(depthTex, texcoord).r);\n"
            "        // Depth textures use nearest-neighbour sampling. Always sample whole render pixels here;\n"
            "        // the resolution-scaled outlineTexelSize is intentionally reserved for colour edges.\n"
            "        vec2 depthTexelOffset = texelSize;\n"
            "        float leftDepth = linearViewDepth(texture2D(depthTex, texcoord - vec2(depthTexelOffset.x, 0.0)).r);\n"
            "        float rightDepth = linearViewDepth(texture2D(depthTex, texcoord + vec2(depthTexelOffset.x, 0.0)).r);\n"
            "        float upDepth = linearViewDepth(texture2D(depthTex, texcoord + vec2(0.0, depthTexelOffset.y)).r);\n"
            "        float downDepth = linearViewDepth(texture2D(depthTex, texcoord - vec2(0.0, depthTexelOffset.y)).r);\n"
            "        // A symmetric second difference detects depth steps/creases while rejecting\n"
            "        // the ordinary first-order depth slope across continuous ground and wall surfaces.\n"
            "        float depthCurvatureX = abs(leftDepth + rightDepth - 2.0 * centreDepth);\n"
            "        float depthCurvatureY = abs(upDepth + downDepth - 2.0 * centreDepth);\n"
            "        float relativeDepthCurvature = max(depthCurvatureX, depthCurvatureY) / max(centreDepth, 1.0);\n"
            "        // At lower resolutions, make distant depth edges harder to trigger; keep near thresholds unchanged.\n"
            "        float resolutionRatio = clamp(outlineTexelSize.y / max(texelSize.y, 0.000001), 0.25, 1.0);\n"
            "        float distanceThresholdScale = mix(1.0, 2.0 - resolutionRatio, distanceFade);\n"
                        "        float characterDepthEdge = smoothstep(0.0025 * distanceThresholdScale, 0.018 * distanceThresholdScale, relativeDepthCurvature);\n"
            "        float sceneryDepthEdge = smoothstep(0.012 * distanceThresholdScale, 0.060 * distanceThresholdScale, relativeDepthCurvature);\n"
            "        float depthEdge = mix(sceneryDepthEdge, characterDepthEdge, characterMask);\n"
            "        depthOutlineEdge = depthEdge * mix(0.55, 0.62, characterMask);\n"
            "    }\n"
            "    // Fade the broad colour mask and geometric-depth mask independently.\n"
            "    // Leave the fine-detail mask at its existing distance tuning to protect character contours.\n"
            "    if (depthTextureAvailable > 0.5) {\n"
            "        edge *= mix(1.0, 0.08, distanceFade);\n"
            "        fineDetailEdge *= mix(1.0, 0.30, distanceFade);\n"
            "        faceColourDetailEdge *= mix(1.0, 0.30, distanceFade);\n"
            "        // Almost fully remove distant depth-only terrain artifacts without changing nearby geometry.\n"
            "        depthOutlineEdge *= mix(1.0, 0.02, distanceFade);\n"
            "    }\n"
            "    edge = max(edge, depthOutlineEdge);\n"
            "    edge = max(edge, faceColourDetailEdge);\n"
            "    // Combine independent masks instead of maxing the fine signal into a broad edge.\n"
            "    // This lets narrow eye contours contribute even if the wider sample detects only a weak edge.\n"
            "    edge = 1.0 - (1.0 - edge) * (1.0 - fineDetailEdge * 0.80);\n"
            "\n"
            "    // Lightweight edge smoothing: soften high-contrast edges only,\n"
            "    // avoiding the heavier full-screen FXAA experiment.\n"
            "    vec3 neighbourAverage = (scene.rgb +\n"
            "        texture2D(sceneTex, texcoord - vec2(outlineTexelSize.x, 0.0)).rgb +\n"
            "        texture2D(sceneTex, texcoord + vec2(outlineTexelSize.x, 0.0)).rgb +\n"
            "        texture2D(sceneTex, texcoord - vec2(0.0, outlineTexelSize.y)).rgb +\n"
            "        texture2D(sceneTex, texcoord + vec2(0.0, outlineTexelSize.y)).rgb) * 0.2;\n"
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
            "    float glare = bright * sunRegion * 0.10 * step(0.001, bloomStrength);\n"
            "    toonColour += vec3(glare, glare * 0.95, glare * 0.82);\n"
            "\n"
            "    // Global saturation adds +20%% later; 1.125 * 1.20 keeps Cel Shading at +35%% overall.\n"
            "    float gradedLuma = sceneLuma(toonColour);\n"
            "    toonColour = mix(vec3(gradedLuma), toonColour, 1.125);\n"
            "    toonColour = (toonColour - vec3(0.5)) * 1.02 + vec3(0.5);\n"
            "\n"
            "    // Very small dither to reduce visible gradient/band stepping.\n"
            "    toonColour += vec3(randomNoise(texcoord) / 255.0);\n"
            "\n"
            "\n"
            "    finalColour = toonColour;\n"
            "    }\n"
            "    // Bloom stays available even when the cel outline/colour treatment is disabled.\n"
            "    if (celEffectsEnabled < 0.5 && bloomStrength > 0.0) {\n"
            "        finalColour += texture2D(bloomTex, texcoord).rgb * bloomStrength;\n"
            "    }\n"
            "    // This world pass runs before GUI/touch rendering; skip the effect during iris transitions.\n"
            "    if (cloudShadowEnabled > 0.5) finalColour = applyWorldCloudShadow(texcoord, finalColour);\n"
            "    gl_FragColor = vec4(clamp(finalColour, 0.0, 1.0), 1.0);\n"
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
        gCelPostOutlineTexelSizeLocation = glGetUniformLocation(gCelPostProgram, "outlineTexelSize");
        gCelPostFineDetailEnabledLocation = glGetUniformLocation(gCelPostProgram, "fineDetailEnabled");
        gCelPostBloomLocation = glGetUniformLocation(gCelPostProgram, "bloomTex");
        gCelPostBloomStrengthLocation = glGetUniformLocation(gCelPostProgram, "bloomStrength");
        gCelPostDepthLocation = glGetUniformLocation(gCelPostProgram, "depthTex");
        gCelPostDepthAvailableLocation = glGetUniformLocation(gCelPostProgram, "depthTextureAvailable");
        gCelPostDepthEdgeDetectionAvailableLocation = glGetUniformLocation(gCelPostProgram, "depthEdgeDetectionAvailable");
        gCelPostCloudShadowEnabledLocation = glGetUniformLocation(gCelPostProgram, "cloudShadowEnabled");
        gCelPostCloudShadowTimeLocation = glGetUniformLocation(gCelPostProgram, "cloudShadowTime");
        gCelPostCloudShadowStrengthLocation = glGetUniformLocation(gCelPostProgram, "cloudShadowStrengthMultiplier");
        gCelPostCelEffectsLocation = glGetUniformLocation(gCelPostProgram, "celEffectsEnabled");
        gCelPostMotionBlurEnabledLocation = glGetUniformLocation(gCelPostProgram, "motionBlurEnabled");
        gCelPostMotionBlurValidLocation = glGetUniformLocation(gCelPostProgram, "motionBlurCameraValid");
        gCelPostMotionBlurScaleLocation = glGetUniformLocation(gCelPostProgram, "motionBlurTimeScale");
        gCelPostCurrentCameraPositionLocation = glGetUniformLocation(gCelPostProgram, "currentCameraPosition");
        gCelPostCurrentCameraRightLocation = glGetUniformLocation(gCelPostProgram, "currentCameraRight");
        gCelPostCurrentCameraUpLocation = glGetUniformLocation(gCelPostProgram, "currentCameraUp");
        gCelPostCurrentCameraForwardLocation = glGetUniformLocation(gCelPostProgram, "currentCameraForward");
        gCelPostPreviousCameraPositionLocation = glGetUniformLocation(gCelPostProgram, "previousCameraPosition");
        gCelPostPreviousCameraRightLocation = glGetUniformLocation(gCelPostProgram, "previousCameraRight");
        gCelPostPreviousCameraUpLocation = glGetUniformLocation(gCelPostProgram, "previousCameraUp");
        gCelPostPreviousCameraForwardLocation = glGetUniformLocation(gCelPostProgram, "previousCameraForward");
        gCelPostCurrentTanHalfFovXLocation = glGetUniformLocation(gCelPostProgram, "currentTanHalfFovX");
        gCelPostCurrentTanHalfFovYLocation = glGetUniformLocation(gCelPostProgram, "currentTanHalfFovY");
        gCelPostPreviousTanHalfFovXLocation = glGetUniformLocation(gCelPostProgram, "previousTanHalfFovX");
        gCelPostPreviousTanHalfFovYLocation = glGetUniformLocation(gCelPostProgram, "previousTanHalfFovY");
        gCelPostCurrentNearPlaneLocation = glGetUniformLocation(gCelPostProgram, "currentNearPlane");
        gCelPostCurrentFarPlaneLocation = glGetUniformLocation(gCelPostProgram, "currentFarPlane");
        gCelPostPreviousNearPlaneLocation = glGetUniformLocation(gCelPostProgram, "previousNearPlane");
        gCelPostCharacterMaskEnabledLocation = glGetUniformLocation(gCelPostProgram, "characterMaskEnabled");
        gCelPostCharacterMaskPositionLocation = glGetUniformLocation(gCelPostProgram, "characterMaskPosition");
        gCelPostCharacterMaskSizeLocation = glGetUniformLocation(gCelPostProgram, "characterMaskSize");
        gCelPostVehicleMaskEnabledLocation = glGetUniformLocation(gCelPostProgram, "vehicleMaskEnabled");
        gCelPostVehicleMaskPositionLocation = glGetUniformLocation(gCelPostProgram, "vehicleMaskPosition");
        gCelPostVehicleMaskSizeLocation = glGetUniformLocation(gCelPostProgram, "vehicleMaskSize");
        gCelPostVehicleMotionIntensityLocation = glGetUniformLocation(gCelPostProgram, "vehicleMotionIntensity");

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
        GLint previousTexture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
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

        if (!CheckSHARAllocationError("cel color texture", width, height))
        {
            gCelPostFailedWidth = width;
            gCelPostFailedHeight = height;
            glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
            return false;
        }

        gCelPostWidth = width;
        gCelPostHeight = height;
        gCelPostFailedWidth = 0;
        gCelPostFailedHeight = 0;
        glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    }

    return gCelPostProgram != 0 && gCelPostTexture != 0;
}

static bool EnsureCelBloomResources(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    const int bloomWidth = width > 3 ? width / 4 : 1;
    const int bloomHeight = height > 3 ? height / 4 : 1;

    if (!CheckSHARRenderTargetSize(
            bloomWidth, bloomHeight, "cel bloom textures", 4, false, false))
        return false;

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
        if (!CheckSHARAllocationError("cel bloom texture", bloomWidth, bloomHeight))
        {
            gCelBloomFailedWidth = bloomWidth;
            gCelBloomFailedHeight = bloomHeight;
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
            glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
            glActiveTexture((GLenum)previousActiveTexture);
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, gCelBloomScratchTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bloomWidth, bloomHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        if (!CheckSHARAllocationError("cel bloom scratch texture", bloomWidth, bloomHeight))
        {
            gCelBloomFailedWidth = bloomWidth;
            gCelBloomFailedHeight = bloomHeight;
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
            glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
            glActiveTexture((GLenum)previousActiveTexture);
            return false;
        }

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

static bool EnsureCelFallbackResolveResources()
{
    if (gCelFallbackProgram != 0 && gCelFallbackVbo != 0)
        return true;

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
        "varying vec2 texcoord;\n"
        "void main() { gl_FragColor = vec4(texture2D(sceneTex, texcoord).rgb, 1.0); }\n";

    GLuint vertexShader = CompileCelPostShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragmentShader = CompileCelPostShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader) glDeleteShader(vertexShader);
        if (fragmentShader) glDeleteShader(fragmentShader);
        return false;
    }

    gCelFallbackProgram = glCreateProgram();
    glBindAttribLocation(gCelFallbackProgram, 0, "position");
    glAttachShader(gCelFallbackProgram, vertexShader);
    glAttachShader(gCelFallbackProgram, fragmentShader);
    glLinkProgram(gCelFallbackProgram);

    GLint linked = GL_FALSE;
    glGetProgramiv(gCelFallbackProgram, GL_LINK_STATUS, &linked);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (linked == GL_FALSE)
    {
        GLint length = 0;
        glGetProgramiv(gCelFallbackProgram, GL_INFO_LOG_LENGTH, &length);
        if (length > 0)
        {
            std::vector<char> info((size_t)length + 1, 0);
            glGetProgramInfoLog(gCelFallbackProgram, length, NULL, info.data());
            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                "SHAR cel fallback resolve program link failed: %s", info.data());
        }
        glDeleteProgram(gCelFallbackProgram);
        gCelFallbackProgram = 0;
        return false;
    }

    gCelFallbackTextureLocation = glGetUniformLocation(gCelFallbackProgram, "sceneTex");
    static const GLfloat quad[] =
    {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f
    };
    glGenBuffers(1, &gCelFallbackVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gCelFallbackVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return gCelFallbackVbo != 0;
}

static void ResolveSceneToScreen(int width, int height)
{
    if (!gCelWorldInFbo)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    GLint oldProgram = 0;
    GLint oldActiveTexture = GL_TEXTURE0;
    GLint oldTexture0 = 0;
    GLint oldArrayBuffer = 0;
    GLint oldVao = 0;
    GLint oldViewport[4] = {0, 0, width, height};
    GLint oldAttrib0Enabled = GL_FALSE;
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActiveTexture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldArrayBuffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING_OES, &oldVao);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture0);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &oldAttrib0Enabled);
    glActiveTexture((GLenum)oldActiveTexture);

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean stencilEnabled = glIsEnabled(GL_STENCIL_TEST);

    int outputWidth = width;
    int outputHeight = height;
    QueryDefaultFramebufferSize(&outputWidth, &outputHeight);

    if (gCelPostTexture != 0 && EnsureCelFallbackResolveResources())
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, outputWidth, outputHeight);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glUseProgram(gCelFallbackProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gCelPostTexture);
        if (gCelFallbackTextureLocation >= 0)
            glUniform1i(gCelFallbackTextureLocation, 0);
        // Keep the game-owned VAO untouched while configuring the fallback quad.
        glBindVertexArrayOES(0);
        glBindBuffer(GL_ARRAY_BUFFER, gCelFallbackVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void*)0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    else
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
            "Unable to create direct cel resolve fallback at %dx%d",
            width, height);
    }

    // Restore the original VAO's attribute-enable state, without having
    // changed its vertex pointer or buffer association during fallback drawing.
    glBindVertexArrayOES((GLuint)oldVao);
    if (oldAttrib0Enabled)
        glEnableVertexAttribArray(0);
    else
        glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)oldArrayBuffer);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)oldTexture0);
    glActiveTexture((GLenum)oldActiveTexture);
    glUseProgram((GLuint)oldProgram);
    if (depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (scissorEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (stencilEnabled) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);

    // This fallback must leave the default framebuffer selected for GUI rendering.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, outputWidth, outputHeight);
    gCelWorldInFbo = false;

}

static void RecreateSHARTexture(GLuint& texture)
{
    if (texture == 0)
        return;

    glDeleteTextures(1, &texture);
    texture = 0;
    glGenTextures(1, &texture);
}

static void ResetAndroidPostProcess()
{
    InvalidateSHARTrackedColorWriteMaskCache();
    InvalidateCelRenderTargetValidationCache();
    // Detach the old texture storage from FBOs before replacing texture names.
    if (gCelRenderFbo != 0)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, gCelRenderFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
    }
    if (gCelBloomFbo != 0)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, gCelBloomFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glActiveTexture(GL_TEXTURE0);
    RecreateSHARTexture(gCelPostTexture);
    RecreateSHARTexture(gCelRenderDepthTexture);
    RecreateSHARTexture(gCelBloomTexture);
    RecreateSHARTexture(gCelBloomScratchTexture);
    RecreateSHARTexture(gFrameSharpenTexture);
    RecreateSHARTexture(gCRTTexture);

    if (gCelRenderDepth != 0)
    {
        glDeleteRenderbuffers(1, &gCelRenderDepth);
        gCelRenderDepth = 0;
        glGenRenderbuffers(1, &gCelRenderDepth);
    }

    gCelDepthTextureWidth = gCelDepthTextureHeight = 0;
    gCelDepthTextureActive = false;
    gCelDepthTextureFailed = false;
    gCelPostWidth = gCelPostHeight = 0;
    gCelRenderbufferWidth = gCelRenderbufferHeight = 0;
    gCelBloomWidth = gCelBloomHeight = 0;
    gFrameSharpenWidth = gFrameSharpenHeight = 0;
    gCRTWidth = gCRTHeight = 0;
    gCelRenderFboReady = false;
    gCelBloomReady = false;

    // Allocation failure latches are per-size. A successful new resize commit
    // explicitly allows each target to retry its allocation for the new surface.
    gCelPostFailedWidth = gCelPostFailedHeight = 0;
    gCelRenderbufferFailedWidth = gCelRenderbufferFailedHeight = 0;
    gCelRenderFboFailedWidth = gCelRenderFboFailedHeight = 0;
    gCelBloomFailedWidth = gCelBloomFailedHeight = 0;
    gFrameSharpenFailedWidth = gFrameSharpenFailedHeight = 0;
    gCRTFailedWidth = gCRTFailedHeight = 0;
    gSHARLastRejectedWidth = gSHARLastRejectedHeight = gSHARLastRejectedReason = 0;
    gCelWorldInFbo = false;
    gSHARPreviousMotionBlurCamera.valid = false;
    gSHARMotionBlurHistoryWidth = gSHARMotionBlurHistoryHeight = 0;
    gSHARPreviousMotionBlurSampleTime = 0;
    // Normal frames remain on the offscreen path immediately after a resize.

}

static void ApplyCelPostProcess(int width, int height)
{
    EndSHARPerformancePass();
    // World geometry is finished. Re-enable ordinary alpha writes before the
    // fullscreen post-process and GUI/output rendering passes.
    SetSHARCharacterMaskModelPass(false);
    SetSHARCharacterMaskWorldEnabled(false);

    const float motionBlurTimeScale = GetSHARMotionBlurTimeScale();
    SHARMotionBlurCameraState currentMotionBlurCamera = {};
    const bool haveCurrentMotionBlurCamera = CaptureSHARMotionBlurCamera(&currentMotionBlurCamera);

    if (gSHARFrameWidth > 0 && gSHARFrameHeight > 0)
    {
        width = gSHARFrameWidth;
        height = gSHARFrameHeight;
    }

    SHARPerfPassScope composePerfScope(SHAR_PERF_COMPOSE, width, height);
    const bool celEffectsEnabled = IsCelShadingEnabled();
    const bool motionBlurRequested = IsMotionBlurEnabled();
    const bool bloomRequested = IsSHARBloomEnabled();
    const bool cloudShadowsUserEnabled = IsSHARCloudShadowsEnabled();
    // Experimental Option B: limit cloud shadows to approved exterior gameplay levels.
    // Do not key this effect off the iris-transition flag: the world behind the expanding
    // iris is already the exterior again, and toggling shadows here caused a visible blink.
    const bool cloudShadowLevelAllowed = IsSHARWorldCloudShadowLevelAllowed();
    // Experimental test setting: use the same full strength on Levels 1, 2, and 3.
    const float cloudShadowStrengthMultiplier = 1.0f;
    const bool cloudShadowsActive =
        cloudShadowsUserEnabled && cloudShadowLevelAllowed &&
        gCelDepthTextureActive && haveCurrentMotionBlurCamera;
    // Report only state transitions so the test can distinguish an unavailable depth/camera path
    // from an effect that is active but visually too subtle, without per-frame log spam.
    static int lastCloudShadowState = -1;
    const int cloudShadowState =
        (cloudShadowLevelAllowed ? 1 : 0) |
        (gCelDepthTextureActive ? 2 : 0) |
        (haveCurrentMotionBlurCamera ? 4 : 0) |
        (gSHARIrisTransitionActive ? 8 : 0) |
        (cloudShadowsActive ? 16 : 0) |
        (cloudShadowsUserEnabled ? 32 : 0);
    if (cloudShadowState != lastCloudShadowState)
    {
        SDL_Log("SHAR cloud-shadow test state: allowedLevel=%d userEnabled=%d depthTexture=%d camera=%d irisTransition=%d active=%d",
            cloudShadowLevelAllowed ? 1 : 0,
            cloudShadowsUserEnabled ? 1 : 0,
            gCelDepthTextureActive ? 1 : 0,
            haveCurrentMotionBlurCamera ? 1 : 0,
            gSHARIrisTransitionActive ? 1 : 0,
            cloudShadowsActive ? 1 : 0);
        lastCloudShadowState = cloudShadowState;
    }

    if (!gCelWorldInFbo)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        CommitSHARMotionBlurCameraHistory(currentMotionBlurCamera, false, width, height);
        return;
    }

    const bool motionBlurCameraValid =
        motionBlurRequested && gCelDepthTextureActive && haveCurrentMotionBlurCamera &&
        IsSHARMotionBlurHistoryUsable(currentMotionBlurCamera, width, height);

    if (!celEffectsEnabled && !motionBlurRequested && !cloudShadowsActive && !bloomRequested)
    {
        ResolveSceneToScreen(width, height);
        CommitSHARMotionBlurCameraHistory(currentMotionBlurCamera, haveCurrentMotionBlurCamera, width, height);
        return;
    }

    // Keep the framebuffer validity check used by the normal cel compose route.
    GLint currentFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFramebuffer);

    if ((GLuint)currentFramebuffer != gCelRenderFbo || !gCelRenderFboReady)
    {
        ResolveSceneToScreen(width, height);
        CommitSHARMotionBlurCameraHistory(currentMotionBlurCamera, haveCurrentMotionBlurCamera, width, height);
        return;
    }

    if (!EnsureCelPostProcessResources(width, height))
    {
        ResolveSceneToScreen(width, height);
        CommitSHARMotionBlurCameraHistory(currentMotionBlurCamera, haveCurrentMotionBlurCamera, width, height);
        return;
    }

    const bool bloomReady = bloomRequested ? EnsureCelBloomResources(width, height) : false;

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

    // The scene texture uses the internal render-size snapshot, while the
    // final fullscreen pass covers the actual default framebuffer.
    int outputWidth = width;
    int outputHeight = height;
    QueryDefaultFramebufferSize(&outputWidth, &outputHeight);

    // Final pass: combine scene, bloom and distance-aware screen-space outlines.
    // If depth textures are unavailable, bloom and the rest of post-processing continue normally.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, outputWidth, outputHeight);
    glUseProgram(gCelPostProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gCelPostTexture);
    glUniform1i(gCelPostSceneLocation, 0);
    if (gCelPostTexelLocation >= 0)
        glUniform2f(gCelPostTexelLocation, 1.0f / (float)width, 1.0f / (float)height);

    // Keep 480p/720p at their existing tuning. Use a fixed 1.00x sampling
    // distance at 1080p and above so fine contours are not skipped at high resolution.
    if (gCelPostOutlineTexelSizeLocation >= 0)
    {
        float outlineScale = 1.0f;
        if (height <= 480)
        {
            outlineScale = 0.25f;
        }
        else if (height <= 720)
        {
            outlineScale = 0.50f;
        }
        glUniform2f(gCelPostOutlineTexelSizeLocation,
            outlineScale / (float)width, outlineScale / (float)height);
    }
    if (gCelPostFineDetailEnabledLocation >= 0)
        glUniform1f(gCelPostFineDetailEnabledLocation, height > 1080 ? 1.0f : 0.0f);

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
    if (gCelPostDepthEdgeDetectionAvailableLocation >= 0)
        glUniform1f(gCelPostDepthEdgeDetectionAvailableLocation,
            (gCelDepthTextureActive &&
             haveCurrentMotionBlurCamera &&
             currentMotionBlurCamera.nearPlane > 0.0f &&
             currentMotionBlurCamera.farPlane > currentMotionBlurCamera.nearPlane) ? 1.0f : 0.0f);
    if (gCelPostCloudShadowEnabledLocation >= 0)
        glUniform1f(gCelPostCloudShadowEnabledLocation, cloudShadowsActive ? 1.0f : 0.0f);
    if (gCelPostCloudShadowTimeLocation >= 0)
    {
        static Uint32 cloudShadowStartTicks = SDL_GetTicks();
        const Uint32 cloudShadowElapsedTicks = SDL_GetTicks() - cloudShadowStartTicks;
        glUniform1f(gCelPostCloudShadowTimeLocation,
            (float)cloudShadowElapsedTicks * 0.001f);
    }
    if (gCelPostCloudShadowStrengthLocation >= 0)
        glUniform1f(gCelPostCloudShadowStrengthLocation, cloudShadowStrengthMultiplier);

    if (gCelPostCelEffectsLocation >= 0)
        glUniform1f(gCelPostCelEffectsLocation, celEffectsEnabled ? 1.0f : 0.0f);
    if (gCelPostMotionBlurEnabledLocation >= 0)
        glUniform1f(gCelPostMotionBlurEnabledLocation, motionBlurRequested ? 1.0f : 0.0f);
    if (gCelPostMotionBlurValidLocation >= 0)
        glUniform1f(gCelPostMotionBlurValidLocation, motionBlurCameraValid ? 1.0f : 0.0f);
    if (gCelPostMotionBlurScaleLocation >= 0)
        glUniform1f(gCelPostMotionBlurScaleLocation, motionBlurTimeScale);

    if (haveCurrentMotionBlurCamera)
    {
        if (gCelPostCurrentCameraPositionLocation >= 0)
            glUniform3f(gCelPostCurrentCameraPositionLocation, currentMotionBlurCamera.position[0], currentMotionBlurCamera.position[1], currentMotionBlurCamera.position[2]);
        if (gCelPostCurrentCameraRightLocation >= 0)
            glUniform3f(gCelPostCurrentCameraRightLocation, currentMotionBlurCamera.right[0], currentMotionBlurCamera.right[1], currentMotionBlurCamera.right[2]);
        if (gCelPostCurrentCameraUpLocation >= 0)
            glUniform3f(gCelPostCurrentCameraUpLocation, currentMotionBlurCamera.up[0], currentMotionBlurCamera.up[1], currentMotionBlurCamera.up[2]);
        if (gCelPostCurrentCameraForwardLocation >= 0)
            glUniform3f(gCelPostCurrentCameraForwardLocation, currentMotionBlurCamera.forward[0], currentMotionBlurCamera.forward[1], currentMotionBlurCamera.forward[2]);
        if (gCelPostCurrentTanHalfFovXLocation >= 0)
            glUniform1f(gCelPostCurrentTanHalfFovXLocation, currentMotionBlurCamera.tanHalfFovX);
        if (gCelPostCurrentTanHalfFovYLocation >= 0)
            glUniform1f(gCelPostCurrentTanHalfFovYLocation, currentMotionBlurCamera.tanHalfFovY);
        if (gCelPostCurrentNearPlaneLocation >= 0)
            glUniform1f(gCelPostCurrentNearPlaneLocation, currentMotionBlurCamera.nearPlane);
        if (gCelPostCurrentFarPlaneLocation >= 0)
            glUniform1f(gCelPostCurrentFarPlaneLocation, currentMotionBlurCamera.farPlane);
    }

    if (gCelPostCharacterMaskEnabledLocation >= 0)
        glUniform1f(gCelPostCharacterMaskEnabledLocation, gSHARMotionBlurCharacterSubject.valid ? 1.0f : 0.0f);
    if (gCelPostCharacterMaskPositionLocation >= 0 && gSHARMotionBlurCharacterSubject.valid)
        glUniform3f(gCelPostCharacterMaskPositionLocation, gSHARMotionBlurCharacterSubject.position[0], gSHARMotionBlurCharacterSubject.position[1], gSHARMotionBlurCharacterSubject.position[2]);
    if (gCelPostCharacterMaskSizeLocation >= 0 && gSHARMotionBlurCharacterSubject.valid)
        glUniform2f(gCelPostCharacterMaskSizeLocation, gSHARMotionBlurCharacterSubject.halfWidth, gSHARMotionBlurCharacterSubject.halfHeight);
    if (gCelPostVehicleMaskEnabledLocation >= 0)
        glUniform1f(gCelPostVehicleMaskEnabledLocation, gSHARMotionBlurVehicleSubject.valid ? 1.0f : 0.0f);
    if (gCelPostVehicleMaskPositionLocation >= 0 && gSHARMotionBlurVehicleSubject.valid)
        glUniform3f(gCelPostVehicleMaskPositionLocation, gSHARMotionBlurVehicleSubject.position[0], gSHARMotionBlurVehicleSubject.position[1], gSHARMotionBlurVehicleSubject.position[2]);
    if (gCelPostVehicleMaskSizeLocation >= 0 && gSHARMotionBlurVehicleSubject.valid)
        glUniform2f(gCelPostVehicleMaskSizeLocation, gSHARMotionBlurVehicleSubject.halfWidth, gSHARMotionBlurVehicleSubject.halfHeight);
    if (gCelPostVehicleMotionIntensityLocation >= 0)
        glUniform1f(gCelPostVehicleMotionIntensityLocation,
            gSHARMotionBlurVehicleSubject.valid ? gSHARMotionBlurVehicleSubject.motionIntensity : 0.0f);

    if (motionBlurCameraValid)
    {
        if (gCelPostPreviousCameraPositionLocation >= 0)
            glUniform3f(gCelPostPreviousCameraPositionLocation, gSHARPreviousMotionBlurCamera.position[0], gSHARPreviousMotionBlurCamera.position[1], gSHARPreviousMotionBlurCamera.position[2]);
        if (gCelPostPreviousCameraRightLocation >= 0)
            glUniform3f(gCelPostPreviousCameraRightLocation, gSHARPreviousMotionBlurCamera.right[0], gSHARPreviousMotionBlurCamera.right[1], gSHARPreviousMotionBlurCamera.right[2]);
        if (gCelPostPreviousCameraUpLocation >= 0)
            glUniform3f(gCelPostPreviousCameraUpLocation, gSHARPreviousMotionBlurCamera.up[0], gSHARPreviousMotionBlurCamera.up[1], gSHARPreviousMotionBlurCamera.up[2]);
        if (gCelPostPreviousCameraForwardLocation >= 0)
            glUniform3f(gCelPostPreviousCameraForwardLocation, gSHARPreviousMotionBlurCamera.forward[0], gSHARPreviousMotionBlurCamera.forward[1], gSHARPreviousMotionBlurCamera.forward[2]);
        if (gCelPostPreviousTanHalfFovXLocation >= 0)
            glUniform1f(gCelPostPreviousTanHalfFovXLocation, gSHARPreviousMotionBlurCamera.tanHalfFovX);
        if (gCelPostPreviousTanHalfFovYLocation >= 0)
            glUniform1f(gCelPostPreviousTanHalfFovYLocation, gSHARPreviousMotionBlurCamera.tanHalfFovY);
        if (gCelPostPreviousNearPlaneLocation >= 0)
            glUniform1f(gCelPostPreviousNearPlaneLocation, gSHARPreviousMotionBlurCamera.nearPlane);
    }

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

    // Keep the GUI and all later passes on the onscreen framebuffer.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, outputWidth, outputHeight);
    gCelWorldInFbo = false;
    CommitSHARMotionBlurCameraHistory(currentMotionBlurCamera, haveCurrentMotionBlurCamera, width, height);
}

}

static void ApplyFrameSharpening(int width, int height);
static void ApplyCRTFilter(int width, int height, bool saturationOnly);

void RegisterSHARMotionBlurSubjectBounds(int subjectType, float x, float y, float z, float halfWidth, float halfHeight, float speedKmh)
{
    SHARMotionBlurSubjectBounds* subject = NULL;
    if (subjectType == 0)
        subject = &gSHARMotionBlurCharacterSubject;
    else if (subjectType == 1)
        subject = &gSHARMotionBlurVehicleSubject;
    if (subject == NULL)
        return;

    if (subjectType == 1)
    {
        // Use actual vehicle speed so repeated render passes cannot cancel the trail strength.
        float speed = speedKmh < 0.0f ? -speedKmh : speedKmh;
        float t = (speed - 12.0f) / (50.0f - 12.0f);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        subject->motionIntensity = t * t * (3.0f - 2.0f * t);
    }

    subject->position[0] = x;
    subject->position[1] = y;
    subject->position[2] = z;
    subject->halfWidth = halfWidth > 0.05f ? halfWidth : 0.05f;
    subject->halfHeight = halfHeight > 0.05f ? halfHeight : 0.05f;
    subject->valid = true;
}

bool BeginCelPostProcessFrame(int width, int height)
{
    gSHARMotionBlurCharacterSubject.valid = false;
    gSHARMotionBlurVehicleSubject.valid = false;
    gSHARFrameWidth = width;
    gSHARFrameHeight = height;
    gCelWorldInFbo = false;
    gSHARPostProcessAppliedBeforeTouchHud = false;
    SetSHARCharacterMaskModelPass(false);
    SetSHARCharacterMaskWorldEnabled(false);

    if (!BindCelRenderTarget(width, height))
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    // Clear alpha exactly once at the start of each world frame. Scissor and
    // RGB writes are disabled so the existing colour image is left untouched.
    GLboolean previousColourMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    GLboolean previousScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLfloat previousClearColour[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    glGetBooleanv(GL_COLOR_WRITEMASK, previousColourMask);
    // Synchronize tracking with the queried state before the temporary clear.
    SetSHARTrackedColorWriteMask(
        previousColourMask[0], previousColourMask[1],
        previousColourMask[2], previousColourMask[3]);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColour);
    glDisable(GL_SCISSOR_TEST);
    SetSHARTrackedColorWriteMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(previousClearColour[0], previousClearColour[1],
                 previousClearColour[2], previousClearColour[3]);
    SetSHARTrackedColorWriteMask(
        previousColourMask[0], previousColourMask[1],
        previousColourMask[2], previousColourMask[3]);
    if (previousScissorEnabled)
        glEnable(GL_SCISSOR_TEST);

    // From this point until composition, only CharacterRenderable model draws
    // may write scene alpha. Blob and static shadows remain unmarked.
    SetSHARCharacterMaskWorldEnabled(true);
    gCelWorldInFbo = true;
    BeginSHARPerformancePass(SHAR_PERF_WORLD, width, height);
    return true;
}

#if defined(RAD_ANDROID)
// Front-end camera-set fine contour pass. The regular world post-process samples
// the world FBO before Scrooby draws the menu, so its fine colour-edge detector
// never sees the living-room camera set. Capture only while that 3D front-end
// scene is active, then run a lightweight one-pixel colour-edge pass before touch HUD.
static GLuint gSHARFrontendFineDetailProgram = 0;
static GLuint gSHARFrontendFineDetailTexture = 0;
static GLuint gSHARFrontendFineDetailVbo = 0;
static GLuint gSHARFrontendFineDetailVao = 0;
static GLint gSHARFrontendFineDetailSceneLocation = -1;
static GLint gSHARFrontendFineDetailTexelSizeLocation = -1;
static int gSHARFrontendFineDetailWidth = 0;
static int gSHARFrontendFineDetailHeight = 0;
static int gSHARFrontendFineDetailFailedWidth = 0;
static int gSHARFrontendFineDetailFailedHeight = 0;

static bool EnsureSHARFrontendFineDetailResources(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    if (gSHARFrontendFineDetailFailedWidth == width &&
        gSHARFrontendFineDetailFailedHeight == height)
        return false;

    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture = 0;
    GLint previousArrayBuffer = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);

    if (gSHARFrontendFineDetailProgram == 0)
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
            "uniform vec2 texelSize;\n"
            "varying vec2 texcoord;\n"
            "float sceneLuma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
            "void main() {\n"
            "    vec4 source = texture2D(sceneTex, texcoord);\n"
            "    float centre = sceneLuma(source.rgb);\n"
            "    float left = sceneLuma(texture2D(sceneTex, texcoord - vec2(texelSize.x, 0.0)).rgb);\n"
            "    float right = sceneLuma(texture2D(sceneTex, texcoord + vec2(texelSize.x, 0.0)).rgb);\n"
            "    float up = sceneLuma(texture2D(sceneTex, texcoord + vec2(0.0, texelSize.y)).rgb);\n"
            "    float down = sceneLuma(texture2D(sceneTex, texcoord - vec2(0.0, texelSize.y)).rgb);\n"
            "    float upLeft = sceneLuma(texture2D(sceneTex, texcoord + vec2(-texelSize.x, texelSize.y)).rgb);\n"
            "    float upRight = sceneLuma(texture2D(sceneTex, texcoord + vec2(texelSize.x, texelSize.y)).rgb);\n"
            "    float downLeft = sceneLuma(texture2D(sceneTex, texcoord + vec2(-texelSize.x, -texelSize.y)).rgb);\n"
            "    float downRight = sceneLuma(texture2D(sceneTex, texcoord + vec2(texelSize.x, -texelSize.y)).rgb);\n"
            "    float edgeStrength = max(max(max(abs(centre-left), abs(centre-right)), max(abs(centre-up), abs(centre-down))),\n"
            "        max(max(abs(centre-upLeft), abs(centre-upRight)), max(abs(centre-downLeft), abs(centre-downRight))));\n"
            "    // Lower thresholds recover the very fine ink-like contours that are\n"
            "    // intentionally subtle in the main world post-process.\n"
            "    float detailEdge = smoothstep(0.028, 0.090, edgeStrength);\n"
            "    float inkStrength = detailEdge * 0.42;\n"
            "    vec3 finalColour = mix(source.rgb, vec3(0.012), inkStrength);\n"
            "    gl_FragColor = vec4(finalColour, source.a);\n"
            "}\n";

        gSHARFrontendFineDetailProgram =
            CreateCelPostProgram(vertexSource, fragmentSource, "frontend fine-detail contour");
        if (gSHARFrontendFineDetailProgram == 0)
        {
            gSHARFrontendFineDetailFailedWidth = width;
            gSHARFrontendFineDetailFailedHeight = height;
            glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previousArrayBuffer);
            glActiveTexture((GLenum)previousActiveTexture);
            glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
            return false;
        }

        gSHARFrontendFineDetailSceneLocation =
            glGetUniformLocation(gSHARFrontendFineDetailProgram, "sceneTex");
        gSHARFrontendFineDetailTexelSizeLocation =
            glGetUniformLocation(gSHARFrontendFineDetailProgram, "texelSize");
    }

    if (gSHARFrontendFineDetailVbo == 0)
    {
        const GLfloat vertices[] = {
            -1.0f, -1.0f,
             1.0f, -1.0f,
            -1.0f,  1.0f,
             1.0f,  1.0f
        };
        glGenBuffers(1, &gSHARFrontendFineDetailVbo);
        glBindBuffer(GL_ARRAY_BUFFER, gSHARFrontendFineDetailVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    }

    if (gSHARFrontendFineDetailVao == 0)
        glGenVertexArraysOES(1, &gSHARFrontendFineDetailVao);

    if (gSHARFrontendFineDetailTexture == 0)
        glGenTextures(1, &gSHARFrontendFineDetailTexture);

    bool textureReady = gSHARFrontendFineDetailTexture != 0 &&
                        gSHARFrontendFineDetailVbo != 0 &&
                        gSHARFrontendFineDetailVao != 0;
    if (textureReady &&
        (gSHARFrontendFineDetailWidth != width ||
         gSHARFrontendFineDetailHeight != height))
    {
        if (!CheckSHARRenderTargetSize(
                width, height, "frontend fine-detail capture", 1, false, true))
        {
            textureReady = false;
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, gSHARFrontendFineDetailTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                         GL_RGB, GL_UNSIGNED_BYTE, NULL);

            const GLenum allocationError = glGetError();
            if (allocationError != GL_NO_ERROR)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "SHAR frontend fine-detail texture allocation failed (%dx%d), GL error=0x%04x",
                    width, height, (unsigned)allocationError);
                textureReady = false;
            }
            else
            {
                gSHARFrontendFineDetailWidth = width;
                gSHARFrontendFineDetailHeight = height;
                gSHARFrontendFineDetailFailedWidth = 0;
                gSHARFrontendFineDetailFailedHeight = 0;
            }
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previousArrayBuffer);
    glActiveTexture((GLenum)previousActiveTexture);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    return textureReady;
}

void ApplySHARFrontendMenuFineDetail()
{
    if (!IsSHARFrontendMenuCelPassActive() || !IsCelShadingEnabled())
        return;

    int outputWidth = 0;
    int outputHeight = 0;
    QueryDefaultFramebufferSize(&outputWidth, &outputHeight);
    if (outputWidth <= 0 || outputHeight <= 0)
        return;

    if (!EnsureSHARFrontendFineDetailResources(outputWidth, outputHeight))
        return;

    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {0, 0, outputWidth, outputHeight};
    GLint previousProgram = 0;
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture = 0;
    GLint previousTextureUnit0 = 0;
    GLint previousArrayBuffer = 0;
    GLint previousVao = 0;
    GLboolean previousDepthEnabled = GL_FALSE;
    GLboolean previousBlendEnabled = GL_FALSE;
    GLboolean previousCullEnabled = GL_FALSE;
    GLboolean previousScissorEnabled = GL_FALSE;
    GLboolean previousStencilEnabled = GL_FALSE;
    GLboolean previousColorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureUnit0);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING_OES, &previousVao);
    glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
    previousDepthEnabled = glIsEnabled(GL_DEPTH_TEST);
    previousBlendEnabled = glIsEnabled(GL_BLEND);
    previousCullEnabled = glIsEnabled(GL_CULL_FACE);
    previousScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    previousStencilEnabled = glIsEnabled(GL_STENCIL_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, outputWidth, outputHeight);

    // Capture the completed front-end frame, then sample the copy while drawing
    // back to framebuffer 0. This pass runs after regular post-processing and
    // before touch controls, preventing feedback from sampling the active target.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gSHARFrontendFineDetailTexture);

    // Fail open if this driver's default-framebuffer copy is unsupported: leave
    // the already-rendered menu visible rather than drawing an empty/stale texture.
    while (glGetError() != GL_NO_ERROR) {}
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                        outputWidth, outputHeight);
    const GLenum captureError = glGetError();
    if (captureError == GL_NO_ERROR)
    {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        InvalidateSHARTrackedColorWriteMaskCache();
        SetSHARTrackedColorWriteMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        glUseProgram(gSHARFrontendFineDetailProgram);
        glUniform1i(gSHARFrontendFineDetailSceneLocation, 0);
        if (gSHARFrontendFineDetailTexelSizeLocation >= 0)
            glUniform2f(gSHARFrontendFineDetailTexelSizeLocation,
                        1.0f / (float)outputWidth, 1.0f / (float)outputHeight);

        // Use a dedicated VAO so the renderer's own/default attribute pointer is
        // untouched. Scrooby can reuse that pointer on the next frame.
        glBindVertexArrayOES(gSHARFrontendFineDetailVao);
        glBindBuffer(GL_ARRAY_BUFFER, gSHARFrontendFineDetailVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void*)0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    else
    {
        static bool s_loggedFrontendFineDetailCopyError = false;
        if (!s_loggedFrontendFineDetailCopyError)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                "SHAR frontend fine-detail capture failed (GL error=0x%04x); leaving the frame unchanged",
                (unsigned)captureError);
            s_loggedFrontendFineDetailCopyError = true;
        }
    }

    glBindVertexArrayOES((GLuint)previousVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previousArrayBuffer);

    if (previousDepthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (previousBlendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (previousCullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (previousScissorEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (previousStencilEnabled) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);

    InvalidateSHARTrackedColorWriteMaskCache();
    SetSHARTrackedColorWriteMask(previousColorMask[0], previousColorMask[1],
                                 previousColorMask[2], previousColorMask[3]);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTextureUnit0);
    glActiveTexture((GLenum)previousActiveTexture);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    glUseProgram((GLuint)previousProgram);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
    glViewport(previousViewport[0], previousViewport[1],
               previousViewport[2], previousViewport[3]);
}
#endif

void ApplyCelPostProcessBeforeGui(int width, int height)
{
    /*
     * Use the frame-begin snapshot for the scene target. After compositing, switch
     * the PDDI display dimensions to the physical output size so menus/HUD render
     * at output resolution while remaining on framebuffer 0.
     */
    (void)width;
    (void)height;
    ApplyCelPostProcess(gSHARFrameWidth, gSHARFrameHeight);

#ifdef RAD_ANDROID
    int outputWidth = gSHARFrameWidth;
    int outputHeight = gSHARFrameHeight;
    QueryDefaultFramebufferSize(&outputWidth, &outputHeight);
    if (gSHARActiveDisplayWidthForGui != NULL &&
        gSHARActiveDisplayHeightForGui != NULL &&
        outputWidth > 0 && outputHeight > 0)
    {
        *gSHARActiveDisplayWidthForGui = outputWidth;
        *gSHARActiveDisplayHeightForGui = outputHeight;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, outputWidth, outputHeight);

        // The iris is a 3D GUI object and uses the default framebuffer's depth
        // test. The world is rendered into a separate FBO, so stale/default-FB
        // depth can incorrectly reject parts of the iris and produce thin strips.
        // Clear only during the iris wipe and preserve the caller's GL state.
        if (gSHARIrisTransitionActive)
        {
            GLboolean oldDepthMask = GL_TRUE;
            const GLboolean oldScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
            GLfloat oldDepthClearValue = 1.0f;
            glGetBooleanv(GL_DEPTH_WRITEMASK, &oldDepthMask);
            glGetFloatv(GL_DEPTH_CLEAR_VALUE, &oldDepthClearValue);

            glDisable(GL_SCISSOR_TEST);
            glDepthMask(GL_TRUE);
            glClearDepthf(1.0f);
            glClear(GL_DEPTH_BUFFER_BIT);

            glClearDepthf(oldDepthClearValue);
            glDepthMask(oldDepthMask);
            if (oldScissorEnabled)
                glEnable(GL_SCISSOR_TEST);
        }
    }
#endif
}


#ifdef RAD_ANDROID
// Movies are presentation content, not part of the adaptive-resolution world
// target. Render them directly to the real Android output Surface so the movie
// quad's device-pixel projection and viewport use one consistent coordinate
// system at every selected internal render resolution.
void BeginSHARAndroidFMVOutputRender()
{
    // FMV output bypasses the world FBO, so restore normal alpha writes before
    // drawing directly to the Android Surface.
    SetSHARCharacterMaskModelPass(false);
    SetSHARCharacterMaskWorldEnabled(false);

    // Direct-output FMV frames must each receive the final global colour grade.
    gSHARPostProcessAppliedBeforeTouchHud = false;

    int outputWidth = 0;
    int outputHeight = 0;
    if (!QueryDefaultFramebufferSize(&outputWidth, &outputHeight) ||
        outputWidth <= 0 || outputHeight <= 0)
    {
        return;
    }

    if (gSHARActiveDisplayWidthForGui != NULL)
        *gSHARActiveDisplayWidthForGui = outputWidth;
    if (gSHARActiveDisplayHeightForGui != NULL)
        *gSHARActiveDisplayHeightForGui = outputHeight;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, outputWidth, outputHeight);

    // Do not let scissor or write masks preserve pixels from the previous
    // open-world frame in the letterbox area surrounding the movie.
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    SetSHARTrackedColorWriteMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // The movie is now the frame being presented. The next BeginFrame will
    // restore the internal world target and bind its FBO as usual.
    gCelWorldInFbo = false;

}
#endif

static void ApplyFrameSharpening(int width, int height)
{
    // The pause-menu setter updates the same state queried here, allowing
    // sharpening to be enabled or bypassed immediately without restarting.
    if (!IsFrameSharpeningEnabled())
        return;

    const float strength = GetFrameSharpenStrength();
    if (strength <= 0.001f || width <= 0 || height <= 0)
        return;

    if (gFrameSharpenFailedWidth == width &&
        gFrameSharpenFailedHeight == height)
        return;

    if (!CheckSHARRenderTargetSize(
            width, height, "sharpening texture", 5, false, true))
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
            "vec3 blurred=(a+b+d+f)*0.25;\n"
            "float s=clamp(sharpen,0.0,1.0)*0.9;\n"
            "vec3 result=e+(e-blurred)*s;\n"
            "gl_FragColor=vec4(clamp(result,0.0,1.0),1.0); }\n";
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
    bool sharpenTextureReady = true;
    if (gFrameSharpenWidth!=width || gFrameSharpenHeight!=height)
    {
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);
        sharpenTextureReady = CheckSHARAllocationError(
            "sharpening texture", width, height);
        if (sharpenTextureReady)
        {
            gFrameSharpenWidth=width;
            gFrameSharpenHeight=height;
            gFrameSharpenFailedWidth=0;
            gFrameSharpenFailedHeight=0;
        }
        else
        {
            gFrameSharpenFailedWidth=width;
            gFrameSharpenFailedHeight=height;
        }
    }

    if (sharpenTextureReady)
    {
        SHARPerfPassScope sharpenPerfScope(SHAR_PERF_SHARPEN, width, height);
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
    }

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


static void ApplyCRTFilter(int width, int height, bool saturationOnly)
{
    bool enabled = false;
    float scanlineStrength = 0.35f, curvature = 0.15f, glowStrength = 0.10f;
    float cornerDarkening = 0.05f, overscan = 0.0f, brightness = 1.0f, contrast = 1.0f;
    GetCRTFilterConfiguration(&enabled, &scanlineStrength, &curvature, &glowStrength,
                              &cornerDarkening, &overscan, &brightness, &contrast);
    // The pre-overlay call runs CRT only when enabled. The final call runs global
    // saturation only, after all menu, GUI and touch-overlay rendering is complete.
    if ((!saturationOnly && !enabled) || width <= 0 || height <= 0)
        return;
    const bool crtPassEnabled = enabled && !saturationOnly;

    if (gCRTFailedWidth == width && gCRTFailedHeight == height)
        return;

    if (!CheckSHARRenderTargetSize(
            width, height, "CRT filter texture", 6, false, true))
        return;

    if (gCRTProgram == 0)
    {
        const char* vertexSource =
            "attribute vec2 position; varying vec2 texcoord;\n"
            "void main() { texcoord = position * 0.5 + 0.5; gl_Position = vec4(position, 0.0, 1.0); }\n";
        // Lightweight GLES2 CRT-Geom-inspired pass: scanlines, barrel curvature,
        // restrained highlight glow, corner shading, and user-controlled levels.
        const char* fragmentSource =
            "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
            "precision highp float;\n"
            "#else\n"
            "precision mediump float;\n"
            "#endif\n"
            "uniform sampler2D frameTex; uniform vec2 texelSize;\n"
            "uniform float scanlineStrength; uniform float curvature; uniform float glowStrength;\n"
            "uniform float cornerDarkening; uniform float overscan; uniform float brightness; uniform float contrast;\n"
            "uniform float crtEnabled; uniform float saturation;\n"
            "varying vec2 texcoord;\n"
            "void main() {\n"
            " vec2 uv=texcoord;\n"
            " if (crtEnabled > 0.5) {\n"
            "  uv=(texcoord-0.5)*(1.0+clamp(overscan,0.0,0.05)*2.0)+0.5;\n"
            "  vec2 p=uv*2.0-1.0; float curve=clamp(curvature,0.0,0.5);\n"
            "  uv=(p*(1.0+curve*vec2(p.y*p.y,p.x*p.x)))*0.5+0.5;\n"
            "  if(uv.x<0.0||uv.x>1.0||uv.y<0.0||uv.y>1.0){ gl_FragColor=vec4(0.0,0.0,0.0,1.0); return; }\n"
            " }\n"
            " vec3 color=texture2D(frameTex,uv).rgb;\n"
            " if (crtEnabled > 0.5) {\n"
            "  vec3 glow=(texture2D(frameTex,uv+vec2(texelSize.x*1.5,0.0)).rgb+texture2D(frameTex,uv-vec2(texelSize.x*1.5,0.0)).rgb+texture2D(frameTex,uv+vec2(0.0,texelSize.y*1.5)).rgb+texture2D(frameTex,uv-vec2(0.0,texelSize.y*1.5)).rgb)*0.25;\n"
            "  color+=max(glow-vec3(0.68),vec3(0.0))*clamp(glowStrength,0.0,1.0)*0.35;\n"
            "  float scan=1.0-mod(floor(gl_FragCoord.y),2.0);\n"
            "  color*=1.0-clamp(scanlineStrength,0.0,1.0)*(0.22*(1.0-scan));\n"
            "  float edge=clamp(length((uv*2.0-1.0)*vec2(0.82,1.0)),0.0,1.4);\n"
            "  color*=1.0-clamp(cornerDarkening,0.0,1.0)*smoothstep(0.55,1.15,edge);\n"
            "  color=(color-vec3(0.5))*clamp(contrast,0.5,1.5)+vec3(0.5);\n"
            "  color*=clamp(brightness,0.5,1.5);\n"
            " }\n"
            " float luma=dot(color,vec3(0.299,0.587,0.114));\n"
            " color=mix(vec3(luma),color,clamp(saturation,1.0,1.5));\n"
            " gl_FragColor=vec4(clamp(color,0.0,1.0),1.0);\n"
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
            gCRTCrtEnabledLocation = glGetUniformLocation(gCRTProgram, "crtEnabled");
            gCRTSaturationLocation = glGetUniformLocation(gCRTProgram, "saturation");
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
    bool crtTextureReady = true;
    if (gCRTWidth!=width || gCRTHeight!=height)
    {
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);
        crtTextureReady = CheckSHARAllocationError(
            "CRT filter texture", width, height);
        if (crtTextureReady)
        {
            gCRTWidth=width;
            gCRTHeight=height;
            gCRTFailedWidth=0;
            gCRTFailedHeight=0;
        }
        else
        {
            gCRTFailedWidth=width;
            gCRTFailedHeight=height;
        }
    }

    if (crtTextureReady)
    {
        SHARPerfPassScope crtPerfScope(
            saturationOnly ? SHAR_PERF_SATURATION : SHAR_PERF_CRT, width, height);
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
        if (gCRTCrtEnabledLocation>=0) glUniform1f(gCRTCrtEnabledLocation,crtPassEnabled ? 1.0f : 0.0f);
        if (gCRTSaturationLocation>=0) glUniform1f(gCRTSaturationLocation,saturationOnly ? 1.20f : 1.0f);
        glBindBuffer(GL_ARRAY_BUFFER,gCRTVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,(const GLvoid*)0);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    }

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

#ifdef RAD_ANDROID
// Run full-frame filters after the game/UI scene is composed but before the
// touch HUD is drawn. This keeps CRT curvature and sharpening from moving or
// blurring the on-screen controls. SwapBuffers calls this as a fallback for
// frames that do not render either touch-HUD path.
void ApplySHARPostProcessingBeforeTouchHud()
{
    if (gSHARPostProcessAppliedBeforeTouchHud)
        return;

    int outputWidth = gSHARFrameWidth > 0 ? gSHARFrameWidth : 0;
    int outputHeight = gSHARFrameHeight > 0 ? gSHARFrameHeight : 0;
    QueryDefaultFramebufferSize(&outputWidth, &outputHeight);
    if (outputWidth <= 0 || outputHeight <= 0)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, outputWidth, outputHeight);

    ApplyFrameSharpening(outputWidth, outputHeight);
    ApplyCRTFilter(outputWidth, outputHeight, false);
    gSHARPostProcessAppliedBeforeTouchHud = true;
}
#endif

static SDL_SpinLock gSHARAndroidRenderResolutionLock = 0;
static int gSHARAndroidRenderWidth = 0;
static int gSHARAndroidRenderHeight = 0;
static int gSHARAndroidRenderResizeSerial = 0;

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

    // Java surface callbacks can run on the UI thread while SDL reads this
    // pair on the render/event thread. Keep the dimensions as one consistent
    // snapshot and never call back into Java while holding this short lock.
    SDL_AtomicLock(&gSHARAndroidRenderResolutionLock);
    const bool changed =
        gSHARAndroidRenderWidth != (int)width ||
        gSHARAndroidRenderHeight != (int)height;

    gSHARAndroidRenderWidth = (int)width;
    gSHARAndroidRenderHeight = (int)height;
    if (changed)
        ++gSHARAndroidRenderResizeSerial;
    SDL_AtomicUnlock(&gSHARAndroidRenderResolutionLock);

}

// Request a preferred Android output refresh rate on API 30+ devices.
// The selected FPS cap still functions if Android cannot or chooses not to change modes.
bool RequestSHARAndroidDisplayFrameRate(int targetFPS)
{
#if defined(RAD_ANDROID)
    if( targetFPS != 30 && targetFPS != 60 && targetFPS != 90 &&
        targetFPS != 120 && targetFPS != 144 )
    {
        return false;
    }

    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if( env == NULL )
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "SHAR FPS request deferred: JNI environment unavailable");
        return false;
    }

    jobject activity = (jobject)SDL_AndroidGetActivity();
    if( activity == NULL )
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "SHAR FPS request deferred: Android activity unavailable");
        return false;
    }

    jclass activityClass = env->GetObjectClass(activity);
    jclass sdlActivityClass = activityClass != NULL
        ? env->GetSuperclass(activityClass) : NULL;
    if( env->ExceptionCheck() )
    {
        env->ExceptionClear();
        sdlActivityClass = NULL;
    }

    jmethodID requestMethod = sdlActivityClass != NULL
        ? env->GetStaticMethodID(
            sdlActivityClass, "requestSHARDisplayFrameRate", "(I)Z" )
        : NULL;
    if( env->ExceptionCheck() )
    {
        env->ExceptionClear();
        requestMethod = NULL;
    }

    bool queued = false;
    if( requestMethod != NULL )
    {
        const jboolean result = env->CallStaticBooleanMethod(
            sdlActivityClass, requestMethod, (jint)targetFPS );
        if( env->ExceptionCheck() )
        {
            env->ExceptionClear();
        }
        else
        {
            queued = (result == JNI_TRUE);
        }
    }

    if( sdlActivityClass != NULL ) env->DeleteLocalRef(sdlActivityClass);
    if( activityClass != NULL ) env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
    return queued;
#else
    (void)targetFPS;
    return false;
#endif
}

// Called by the in-game Graphics menu. This only requests a Java/UI-thread
// Surface resize; rendering and GL resource changes continue through SDL's
// regular surface/window resize handling.
bool RequestSHARAndroidRenderResolutionHeight(int targetHeight)
{
#if defined(RAD_ANDROID)
    if (targetHeight <= 0)
        return false;

    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (env == NULL)
    {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "SHAR live resolution request failed: no JNI environment");
        return false;
    }

    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (activity == NULL)
    {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "SHAR live resolution request failed: Android activity unavailable");
        return false;
    }

    jclass activityClass = env->GetObjectClass(activity);
    jclass sdlActivityClass = activityClass != NULL
        ? env->GetSuperclass(activityClass) : NULL;

    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        sdlActivityClass = NULL;
    }

    jmethodID requestMethod = sdlActivityClass != NULL
        ? env->GetStaticMethodID(
            sdlActivityClass,
            "requestSHARRenderResolutionHeight",
            "(I)Z")
        : NULL;

    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        requestMethod = NULL;
    }

    bool queued = false;
    if (requestMethod != NULL)
    {
        const jboolean result = env->CallStaticBooleanMethod(
            sdlActivityClass,
            requestMethod,
            (jint)targetHeight);

        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
        }
        else
        {
            queued = (result == JNI_TRUE);
        }
    }

    if (!queued)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Unable to queue live render resolution request for targetHeight=%d; saved setting will apply at next launch",
            targetHeight);
    }

    if (sdlActivityClass != NULL)
        env->DeleteLocalRef(sdlActivityClass);
    if (activityClass != NULL)
        env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
    return queued;
#else
    (void)targetHeight;
    return false;
#endif
}

static bool
GetSHARAndroidRenderResolution(int* width, int* height)
{
    if (width == NULL || height == NULL)
        return false;

    SDL_AtomicLock(&gSHARAndroidRenderResolutionLock);
    const int renderWidth = gSHARAndroidRenderWidth;
    const int renderHeight = gSHARAndroidRenderHeight;
    SDL_AtomicUnlock(&gSHARAndroidRenderResolutionLock);

    if (renderWidth <= 0 || renderHeight <= 0)
        return false;

    *width = renderWidth;
    *height = renderHeight;

    return true;
}

#endif

#ifdef RAD_ANDROID
void pglDisplay::CommitPendingResize()
{
    static int committedSerial = 0;
    int requestedWidth = 0;
    int requestedHeight = 0;
    int serial = 0;

    SDL_AtomicLock(&gSHARAndroidRenderResolutionLock);
    requestedWidth = gSHARAndroidRenderWidth;
    requestedHeight = gSHARAndroidRenderHeight;
    serial = gSHARAndroidRenderResizeSerial;
    SDL_AtomicUnlock(&gSHARAndroidRenderResolutionLock);

    if (requestedWidth <= 0 || requestedHeight <= 0)
        return;

    if (serial == committedSerial)
    {
        // The previous frame may have temporarily exposed output dimensions to
        // the GUI. Restore the internal world target before the next frame.
        winWidth = requestedWidth;
        winHeight = requestedHeight;
        return;
    }

    winWidth = requestedWidth;
    winHeight = requestedHeight;
    committedSerial = serial;

    // Only the offscreen world target changes here. The Android output Surface
    // remains platform-sized, so there is no EGL size match/wait to perform.
    ResetAndroidPostProcess();

    int outputWidth = winWidth;
    int outputHeight = winHeight;
    QueryDefaultFramebufferSize(&outputWidth, &outputHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, outputWidth, outputHeight);
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
    InvalidateSHARTrackedColorWriteMaskCache();
    InvalidateCelRenderTargetValidationCache();
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
            // Android Surface callbacks run on the UI thread. Do not mutate
            // active PDDI dimensions or call OpenGL here; the GL thread commits
            // the latest confirmed target from BeginFrame().
#else
            SDL_GL_GetDrawableSize(win, &winWidth, &winHeight);
#endif
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
            // Defer active-size mutation and GL calls to CommitPendingResize()
            // on the render thread at the start of a frame.
#else
            SDL_GetWindowSizeInPixels(win, &winWidth, &winHeight);
#endif
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
#ifdef RAD_ANDROID
    gSHARActiveDisplayWidthForGui = &winWidth;
    gSHARActiveDisplayHeightForGui = &winHeight;
#endif
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
     * SDLSurface.java sends the selected internal world-render size through
     * nativeSetSHARRenderResolution(). The Android output Surface remains at its
     * platform size; this target is used for world projection, viewport and scissor.
     * After scene composition, ApplyCelPostProcessBeforeGui temporarily changes the
     * PDDI dimensions to output size for GUI drawing. CommitPendingResize restores
     * the internal render dimensions at the next frame start.
     */
    {
        int renderW = 0;
        int renderH = 0;

        if (GetSHARAndroidRenderResolution(&renderW, &renderH)) {
            winWidth = renderW;
            winHeight = renderH;

        } else {
#if SDL_MAJOR_VERSION < 3
            SDL_GL_GetDrawableSize(win, &winWidth, &winHeight);
#else
            SDL_GetWindowSizeInPixels(win, &winWidth, &winHeight);
#endif
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
    PollSHARGpuTimerQueries();
    RecordSHARSwapFrameInterval();

    // First apply the existing optional filters before the touch HUD if they
    // have not already run during rendering.
    ApplySHARPostProcessingBeforeTouchHud();

    // Then apply global saturation to the final composed frame, including GUI,
    // menus, videos and touch controls. This shares the CRT pass texture.
    int outputWidth = 0;
    int outputHeight = 0;
    if (QueryDefaultFramebufferSize(&outputWidth, &outputHeight) &&
        outputWidth > 0 && outputHeight > 0)
    {
        ApplyCRTFilter(outputWidth, outputHeight, true);
    }
#endif
    SDL_GL_SwapWindow(win);
    reset = false;
#ifdef RAD_ANDROID
    /*
     * Selectable Android gameplay FPS cap.
     *
     * m_only60 is retained as the legacy switch:
     * - true  = apply the selected gameplay FPS cap
     * - false = FMV/cinematics or special states, do not force this cap here
     */
    static Uint64 sCapFreq = 0;
    static Uint64 sLastFrameTime = 0;

    if (sCapFreq == 0)
    {
        sCapFreq = SDL_GetPerformanceFrequency();
    }

    if (m_only60)
    {
        const int configuredFPS = gSHARAndroidTargetFPS;
        const int targetFPS =
            (configuredFPS >= 30 && configuredFPS <= 144) ? configuredFPS : 60;
        const Uint64 targetTicks =
            (Uint64)((double)sCapFreq / (double)targetFPS + 0.5);

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
    if (buffer == NULL || nBytes <= 0 || winWidth <= 0 || winHeight <= 0)
        return 0;

    // Calculate sizes without signed-int overflow. The previous fixed
    // unsigned tmp[2048] row overflowed at 2048-pixel widths (including 1440p
    // and 2160p landscape render targets).
    const size_t rowBytes = (size_t)winWidth * 4;
    if (rowBytes / 4 != (size_t)winWidth ||
        (size_t)winHeight > ((size_t)-1) / rowBytes)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
            "SHAR screenshot refused: image dimensions overflow size_t (%dx%d)",
            winWidth, winHeight);
        return 0;
    }

    const size_t requiredBytes = rowBytes * (size_t)winHeight;
    if (requiredBytes > (size_t)nBytes || requiredBytes > (size_t)~0U)
        return 0;

    unsigned char* temporaryRow =
        (unsigned char*)malloc(rowBytes);
    if (temporaryRow == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
            "SHAR screenshot refused: could not allocate %u-byte row buffer",
            (unsigned)rowBytes);
        return 0;
    }

    glReadPixels(0, 0, winWidth, winHeight,
                 GL_BGRA_EXT, GL_UNSIGNED_BYTE, buffer);
#if defined(RAD_ANDROID)
    if (!CheckSHARAllocationError("screenshot readback", winWidth, winHeight))
    {
        free(temporaryRow);
        return 0;
    }
#endif

    for (int row = 0; row < winHeight / 2; ++row)
    {
        unsigned char* top = (unsigned char*)(buffer + ((size_t)row * (size_t)winWidth));
        unsigned char* bottom = (unsigned char*)(buffer +
            ((size_t)(winHeight - 1 - row) * (size_t)winWidth));

        memcpy(temporaryRow, top, rowBytes);
        memcpy(top, bottom, rowBytes);
        memcpy(bottom, temporaryRow, rowBytes);
    }

    free(temporaryRow);
    return (unsigned)requiredBytes;
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

