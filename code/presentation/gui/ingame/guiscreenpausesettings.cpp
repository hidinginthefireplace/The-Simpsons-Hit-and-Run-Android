//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   CGuiScreenPauseSettings
//
// Description: Implementation of the CGuiScreenPauseSettings class.
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2002/07/04      TChu        Created for SRR2
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================
#include <presentation/gui/ingame/guiscreenpausesettings.h>
#include <presentation/gui/ingame/guimanageringame.h>
#include <presentation/gui/guimenu.h>
#include <presentation/gui/guitextbible.h>
#include <presentation/gui/guisystem.h>

#include <camera/supercammanager.h>
#include <camera/supercamcentral.h>
#include <input/inputmanager.h>
#include <events/eventmanager.h>
#include <memory/srrmemory.h>
#include <mission/charactersheet/charactersheetmanager.h>
#include <presentation/tutorialmanager.h>
#include <worldsim/avatarmanager.h>
#if defined(RAD_ANDROID)
#include <worldsim/character/character.h>
#endif

#include <raddebug.hpp> // Foundation
#include <Group.h>
#include <Screen.h>
#include <Page.h>
#include <Text.h>
#include <strings/unicodeString.h>

#if defined(RAD_ANDROID)
#include "FeGroup.h"
#include "FeText.h"
#include "FeSprite.h"
#endif


#if defined(RAD_ANDROID)
#include <data/config/androidconfigurationmanager.h>
#include <SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Rendering toggles exposed by the GLES renderer.
bool IsCelShadingEnabled();
void SetCelShadingEnabled(bool enabled);
bool IsCRTFilterEnabled();
void SetCRTFilterEnabled(bool enabled);
bool IsFrameSharpeningEnabled();
void SetFrameSharpeningEnabled(bool enabled);
bool IsMotionBlurEnabled();
void SetMotionBlurEnabled(bool enabled);
bool IsSHARBloomEnabled();
void SetSHARBloomEnabled(bool enabled);
bool IsSHARCloudShadowsEnabled();
void SetSHARCloudShadowsEnabled(bool enabled);
bool IsSHARRimLightingEnabled();
void SetSHARRimLightingEnabled(bool enabled);
int GetSHARAnisotropicFilteringSelection();
void SetSHARAnisotropicFilteringSelection(int selection);
int GetFrameSharpeningLevel();
void SetFrameSharpeningLevel(int level);
bool RequestSHARAndroidRenderResolutionHeight(int targetHeight);
int GetSHARAndroidTargetFPS();
void SetSHARAndroidTargetFPS(int targetFPS);
void InitializeSHARAndroidFrameRateSelection();
bool RequestSHARAndroidDisplayFrameRate(int targetFPS);

static bool gAndroidGraphicsSettingsRequested = false;
static bool gAndroidCheatsSettingsRequested = false;

// Live resolution selector. Full-screen effects continue to use GL size,
// budget, and allocation guards; failed targets safely bypass the affected effect.
static const int SHAR_GRAPHICS_RESOLUTION_COUNT = 5;
static int gSHARGraphicsResolutionSelection = 2; // default: 1080p
static bool gSHARGraphicsResolutionNeedsNormalization = false;

// FPS choices are render caps; Android may select a matching display mode when supported.
static const int SHAR_GRAPHICS_FPS_COUNT = 5;
static const int SHAR_GRAPHICS_FPS_VALUES[SHAR_GRAPHICS_FPS_COUNT] =
{
    30, 60, 90, 120, 144
};
static int gSHARGraphicsFPSSelection = 1; // default: 60 FPS

void RequestAndroidGraphicsSettings()
{
    gAndroidGraphicsSettingsRequested = true;
}

void RequestAndroidCheatsSettings()
{
    gAndroidCheatsSettingsRequested = true;
}
#endif


#if defined(RAD_ANDROID)
  #include <android/log.h>
  #define LOG_TAG "SimpsonsHitAndRun"
  #define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
  #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#elif defined(RAD_VITA)
  #include <psp2/kernel/clib.h>
  #define LOGI(...) do { sceClibPrintf(__VA_ARGS__); sceClibPrintf("\n"); } while(0)
  #define LOGE(...) do { sceClibPrintf(__VA_ARGS__); sceClibPrintf("\n"); } while(0)

#else
  #include <cstdio>
  #define LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); std::fflush(stdout); } while(0)
  #define LOGE(...) do { std::printf(__VA_ARGS__); std::printf("\n"); std::fflush(stdout); } while(0)
#endif

#if defined(RAD_ANDROID)

// Resolution choices shown by the Android Graphics submenu. Java reads the
// saved target_height at startup and also accepts a live resize request from
// this menu. Android performs the Surface change on its UI thread.
static const int SHAR_GRAPHICS_RESOLUTION_HEIGHTS[5] =
{
    480, 720, 1080, 1440, 2160
};

static int GetSHARGraphicsResolutionHeight( int selection )
{
    if( selection < 0 || selection >= SHAR_GRAPHICS_RESOLUTION_COUNT )
    {
        selection = 2;
    }
    return SHAR_GRAPHICS_RESOLUTION_HEIGHTS[ selection ];
}

static int GetSHARGraphicsResolutionSelectionForHeight( int height )
{
    if( height <= 0 )
    {
        return 2;
    }

    int bestSelection = 0;
    int bestDistance = abs( height - SHAR_GRAPHICS_RESOLUTION_HEIGHTS[0] );

    for( int i = 1; i < SHAR_GRAPHICS_RESOLUTION_COUNT; ++i )
    {
        const int distance = abs( height - SHAR_GRAPHICS_RESOLUTION_HEIGHTS[i] );
        // Prefer the higher profile when a custom value lies exactly between
        // two menu choices.
        if( distance <= bestDistance )
        {
            bestDistance = distance;
            bestSelection = i;
        }
    }
    return bestSelection;
}

static int LoadSHARGraphicsResolutionSelection()
{
    const char* basePath = SDL_AndroidGetApplicationExternalFilesPath();
    if( basePath == NULL || basePath[0] == '\0' )
    {
        return 2;
    }

    char path[512];
    const int pathLength = snprintf(
        path, sizeof(path), "%s/Graphics/Simpsons_resolution.txt", basePath );
    if( pathLength < 0 || pathLength >= static_cast<int>( sizeof(path) ) )
    {
        LOGE("[SHAR-RESOLUTION] Config path too long; defaulting to 1080p");
        return 2;
    }

    FILE* file = fopen( path, "r" );
    if( file == NULL )
    {
        LOGI("[SHAR-RESOLUTION] Config not present yet; defaulting to 1080p");
        return 2;
    }

    char line[256];
    int selection = 2;
    while( fgets( line, sizeof(line), file ) != NULL )
    {
        char* cursor = line;
        while( *cursor == ' ' || *cursor == '\t' )
        {
            ++cursor;
        }
        if( *cursor == '\0' || *cursor == '#' )
        {
            continue;
        }

        if( strncmp( cursor, "target_height", 13 ) != 0 )
        {
            continue;
        }

        char* equals = strchr( cursor, '=' );
        if( equals == NULL )
        {
            continue;
        }

        char* value = equals + 1;
        while( *value == ' ' || *value == '\t' )
        {
            ++value;
        }

        if( strncmp( value, "MAX", 3 ) == 0 ||
            strncmp( value, "max", 3 ) == 0 )
        {
            selection = SHAR_GRAPHICS_RESOLUTION_COUNT - 1;
            gSHARGraphicsResolutionNeedsNormalization = true;
        }
        else
        {
            const int height = static_cast<int>( strtol( value, NULL, 10 ) );
            if( height > 0 )
            {
                selection = GetSHARGraphicsResolutionSelectionForHeight( height );
                bool supportedProfile = false;
                for( int profile = 0; profile < SHAR_GRAPHICS_RESOLUTION_COUNT; ++profile )
                {
                    if( height == SHAR_GRAPHICS_RESOLUTION_HEIGHTS[profile] )
                    {
                        supportedProfile = true;
                        break;
                    }
                }

                if( !supportedProfile )
                {
                    // Unsupported custom render heights are normalized to the
                    // nearest supported menu profile before a future app launch.
                    gSHARGraphicsResolutionNeedsNormalization = true;
                }
            }
        }
        break;
    }

    fclose( file );
    return selection;
}

static bool SaveSHARGraphicsResolutionSelection( int selection )
{
    const char* basePath = SDL_AndroidGetApplicationExternalFilesPath();
    if( basePath == NULL || basePath[0] == '\0' )
    {
        LOGE("[SHAR-RESOLUTION] Cannot save selection: app files path unavailable");
        return false;
    }

    char directory[512];
    int written = snprintf( directory, sizeof(directory), "%s/Graphics", basePath );
    if( written < 0 || written >= static_cast<int>( sizeof(directory) ) )
    {
        LOGE("[SHAR-RESOLUTION] Cannot save selection: Graphics path too long");
        return false;
    }

    struct stat directoryInfo;
    if( mkdir( directory, 0777 ) != 0 &&
        ( errno != EEXIST || stat( directory, &directoryInfo ) != 0 ||
          !S_ISDIR( directoryInfo.st_mode ) ) )
    {
        LOGE("[SHAR-RESOLUTION] Cannot create config directory %s (errno=%d)",
             directory, errno);
        return false;
    }

    char path[512];
    written = snprintf( path, sizeof(path),
                        "%s/Simpsons_resolution.txt", directory );
    if( written < 0 || written >= static_cast<int>( sizeof(path) ) )
    {
        LOGE("[SHAR-RESOLUTION] Cannot save selection: config path too long");
        return false;
    }

    FILE* file = fopen( path, "w" );
    if( file == NULL )
    {
        LOGE("[SHAR-RESOLUTION] Cannot open config for writing: %s (errno=%d)",
             path, errno);
        return false;
    }

    const int height = GetSHARGraphicsResolutionHeight( selection );
    const int result = fprintf(
        file,
        "# Simpsons Hit & Run Android adaptive render resolution\n"
        "# Selected in the in-game Graphics menu.\n"
        "# Changes are requested immediately and also saved for the next launch.\n"
        "# The render width is calculated automatically from the display aspect ratio.\n"
        "# Supported menu values: 480, 720, 1080, 1440, 2160.\n"
        "target_height=%d\n",
        height );
    const int closeResult = fclose( file );

    if( result < 0 || closeResult != 0 )
    {
        LOGE("[SHAR-RESOLUTION] Failed writing selector value to %s", path);
        return false;
    }

    LOGI("[SHAR-RESOLUTION] Saved target_height=%d to %s",
         height, path);
    return true;
}

static int LoadSHARGraphicsFPSSelection()
{
    const char* basePath = SDL_AndroidGetApplicationExternalFilesPath();
    if( basePath == NULL || basePath[0] == '\0' )
    {
        return 1;
    }

    char path[512];
    const int pathLength = snprintf(
        path, sizeof(path), "%s/Graphics/Simpsons_fps.txt", basePath );
    if( pathLength < 0 || pathLength >= static_cast<int>( sizeof(path) ) )
    {
        LOGE("[SHAR-FPS] Config path too long; defaulting to 60 FPS");
        return 1;
    }

    FILE* file = fopen( path, "r" );
    if( file == NULL )
    {
        return 1;
    }

    int selection = 1;
    char line[256];
    while( fgets( line, sizeof(line), file ) != NULL )
    {
        char* cursor = line;
        while( *cursor == ' ' || *cursor == '\t' )
        {
            ++cursor;
        }
        if( *cursor == '\0' || *cursor == '#' ||
            strncmp( cursor, "target_fps", 10 ) != 0 )
        {
            continue;
        }

        char* equals = strchr( cursor, '=' );
        if( equals == NULL )
        {
            continue;
        }

        const int fps = static_cast<int>( strtol( equals + 1, NULL, 10 ) );
        for( int i = 0; i < SHAR_GRAPHICS_FPS_COUNT; ++i )
        {
            if( fps == SHAR_GRAPHICS_FPS_VALUES[i] )
            {
                selection = i;
                break;
            }
        }
        break;
    }

    fclose( file );
    return selection;
}

static bool SaveSHARGraphicsFPSSelection( int selection )
{
    if( selection < 0 || selection >= SHAR_GRAPHICS_FPS_COUNT )
    {
        selection = 1;
    }

    const char* basePath = SDL_AndroidGetApplicationExternalFilesPath();
    if( basePath == NULL || basePath[0] == '\0' )
    {
        LOGE("[SHAR-FPS] Cannot save selection: app files path unavailable");
        return false;
    }

    char directory[512];
    int written = snprintf( directory, sizeof(directory), "%s/Graphics", basePath );
    if( written < 0 || written >= static_cast<int>( sizeof(directory) ) )
    {
        LOGE("[SHAR-FPS] Graphics path too long");
        return false;
    }

    struct stat directoryInfo;
    if( mkdir( directory, 0777 ) != 0 &&
        ( errno != EEXIST || stat( directory, &directoryInfo ) != 0 ||
          !S_ISDIR( directoryInfo.st_mode ) ) )
    {
        LOGE("[SHAR-FPS] Cannot create config directory %s (errno=%d)",
             directory, errno);
        return false;
    }

    char path[512];
    written = snprintf( path, sizeof(path), "%s/Simpsons_fps.txt", directory );
    if( written < 0 || written >= static_cast<int>( sizeof(path) ) )
    {
        LOGE("[SHAR-FPS] Config path too long");
        return false;
    }

    FILE* file = fopen( path, "w" );
    if( file == NULL )
    {
        LOGE("[SHAR-FPS] Cannot open config file %s (errno=%d)", path, errno);
        return false;
    }

    const int fps = SHAR_GRAPHICS_FPS_VALUES[selection];
    const int result = fprintf(
        file,
        "# Simpsons Hit & Run Android frame-rate cap\n"
        "# Selected from the in-game Graphics menu.\n"
        "# Android may use the closest supported display refresh rate.\n"
        "target_fps=%d\n",
        fps );
    const int closeResult = fclose( file );

    if( result < 0 || closeResult != 0 )
    {
        LOGE("[SHAR-FPS] Failed writing selector value to %s", path);
        return false;
    }

    LOGI("[SHAR-FPS] Saved target_fps=%d to %s", fps, path);
    return true;
}

int GetSHARAndroidTargetFPS()
{
    if( gSHARGraphicsFPSSelection < 0 ||
        gSHARGraphicsFPSSelection >= SHAR_GRAPHICS_FPS_COUNT )
    {
        return 60;
    }
    return SHAR_GRAPHICS_FPS_VALUES[gSHARGraphicsFPSSelection];
}

void InitializeSHARAndroidFrameRateSelection()
{
    gSHARGraphicsFPSSelection = LoadSHARGraphicsFPSSelection();
    SetSHARAndroidTargetFPS( GetSHARAndroidTargetFPS() );
    if( !RequestSHARAndroidDisplayFrameRate( GetSHARAndroidTargetFPS() ) )
    {
        LOGI("[SHAR-FPS] Display refresh-rate request deferred; the FPS cap remains active");
    }
}

#endif


//===========================================================================
// Global Data, Local Data, Local Classes
//===========================================================================




const char* PAUSE_SETTINGS_MENU_ITEMS[] =
{
    "Camera",
    "JumpCamera",
#if defined(RAD_ANDROID)
    "IntersectNavSystem",
    "Radar",
    "InvertCamControl",
    "Vibration",
#else
#if !defined(RAD_PC)
    "InvertCamControl",
#endif
    "IntersectNavSystem",
    "Radar",
#if !defined(RAD_PC)
    "Vibration",
#endif
#endif
    "Tutorial",

    ""
};

#ifdef RAD_PC
SuperCam::Type PC_CAMERAS_FOR_WALKING[] =
{
    SuperCam::WALKER_CAM,
    SuperCam::DEBUG_CAM,
    SuperCam::KULL_CAM
};

const int NUM_PC_CAMERAS_FOR_WALKING = sizeof(PC_CAMERAS_FOR_WALKING)/sizeof(SuperCam::Type);
const int NUM_PC_CAMERAS_FOR_WALKING_WITHOUT_CHEAT = 1;
#endif

#if defined(RAD_ANDROID)

static void CopyAndroidMenuTextAppearance( FeText* destination, FeText* source )
{
    if( destination == NULL || source == NULL )
    {
        return;
    }

    destination->SetTextStyle( source->GetTextStyleResourceID() );

    int width = 0;
    int height = 0;
    source->GetBoundingBoxSize( width, height );
    destination->SetBoundingBoxSize( width, height );

    destination->SetHorizontalJustification( source->GetHorizontalJustification() );
    destination->SetVerticalJustification( source->GetVerticalJustification() );
    destination->SetColour( source->GetColour() );
    destination->SetAlpha( source->GetAlpha() );
    destination->SetTextMode( source->GetTextMode() );
    destination->SetDisplayShadow( source->IsDisplayingShadow() );
    destination->SetShadowOffset( source->GetXShadowOffset(), source->GetYShadowOffset() );
    destination->SetShadowColour( source->GetShadowColour() );
    destination->SetDisplayOutline( source->IsDisplayingOutline() );
    destination->SetOutlineColour( source->GetOutlineColour() );
}

static void CopyAndroidMenuSpriteAppearance( FeSprite* destination, const FeSprite* source )
{
    if( destination == NULL || source == NULL )
    {
        return;
    }

    destination->CopySpriteDataFrom( *source );

    int width = 0;
    int height = 0;
    source->GetBoundingBoxSize( width, height );
    destination->SetBoundingBoxSize( width, height );
    destination->SetHorizontalJustification( source->GetHorizontalJustification() );
    destination->SetVerticalJustification( source->GetVerticalJustification() );
    destination->SetColour( source->GetColour() );
    destination->SetAlpha( source->GetAlpha() );
}

static bool GetAndroidSettingsRowGeometry
(
    Scrooby::Page* pPage,
    int& labelX,
    int& valueX,
    int& leftArrowX,
    int& rightArrowX,
    int& baseY,
    int& rowSpacing
)
{
    if( pPage == NULL )
    {
        return false;
    }

    Scrooby::Group* pNavGroup = pPage->GetGroup( "IntersectNavSystem" );
    Scrooby::Group* pRadarGroup = pPage->GetGroup( "Radar" );

    if( pNavGroup == NULL || pRadarGroup == NULL )
    {
        return false;
    }

    Scrooby::Text* pNavLabel = pNavGroup->GetText( "IntersectNavSystem" );
    Scrooby::Text* pRadarLabel = pRadarGroup->GetText( "Radar" );
    Scrooby::Text* pRadarValue = pRadarGroup->GetText( "Radar_Value" );
    Scrooby::Sprite* pRadarLArrow = pRadarGroup->GetSprite( "Radar_LArrow" );
    Scrooby::Sprite* pRadarRArrow = pRadarGroup->GetSprite( "Radar_RArrow" );

    if( pNavLabel == NULL || pRadarLabel == NULL || pRadarValue == NULL )
    {
        return false;
    }

    int navX = 0;
    int navY = 0;
    int radarLabelX = 0;
    int radarY = 0;
    int radarValueX = 0;
    int radarValueY = 0;
    int radarLArrowX = 0;
    int radarLArrowY = 0;
    int radarRArrowX = 0;
    int radarRArrowY = 0;

    pNavLabel->GetBoundingBoxCenter( navX, navY );
    pRadarLabel->GetBoundingBoxCenter( radarLabelX, radarY );
    pRadarValue->GetBoundingBoxCenter( radarValueX, radarValueY );

    if( pRadarLArrow != NULL )
    {
        pRadarLArrow->GetBoundingBoxCenter( radarLArrowX, radarLArrowY );
    }

    if( pRadarRArrow != NULL )
    {
        pRadarRArrow->GetBoundingBoxCenter( radarRArrowX, radarRArrowY );
    }

    labelX = radarLabelX;
    valueX = radarValueX;
    leftArrowX = radarLArrowX;
    rightArrowX = radarRArrowX;
    baseY = radarY;
    rowSpacing = radarY - navY;

    if( rowSpacing == 0 )
    {
        rowSpacing = 60;
    }

    return true;
}

#endif

//===========================================================================
// Public Member Functions
//===========================================================================

//===========================================================================
// CGuiScreenPauseSettings::CGuiScreenPauseSettings
//===========================================================================
// Description: Constructor.
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
CGuiScreenPauseSettings::CGuiScreenPauseSettings
(
    Scrooby::Screen* pScreen,
    CGuiEntity* pParent
)
:   CGuiScreen( pScreen, pParent, GUI_SCREEN_ID_SETTINGS ),
#if defined(RAD_ANDROID)
    m_graphicsSettingsMode( false ),
    m_cheatsSettingsMode( false ),
#endif
    m_pMenu( NULL ),
    m_currentCameraSelectionMode( CAMERA_SELECTION_NOT_AVAILABLE )
{
MEMTRACK_PUSH_GROUP( "CGuiScreenPauseSettings" );



    // Retrieve the Scrooby drawing elements.
    //
    Scrooby::Page* pPage = m_pScroobyScreen->GetPage( "PauseSettings" );



    rAssert( pPage );

    // Create a menu.
    //
    m_pMenu = new CGuiMenu( this, NUM_PAUSE_SETTINGS_MENU_ITEMS, GUI_TEXT_MENU, MENU_SFX_NONE );

    rAssert( m_pMenu != NULL );

    #if defined(RAD_ANDROID)

    for( int i = 0; i < NUM_PAUSE_SETTINGS_MENU_ITEMS; i++ )
    {
        mMenuItemToGuiMenuIndex[ i ] = -1;
        mGuiMenuIndexToMenuItem[ i ] = -1;
    }
    #endif

    // Add menu items.
    //
    char itemName[ 32 ];

    //
    // Diagnostic only:
    // Original PauseSettings code gets each item group directly from pPage.
    // We only check "Menu" to know whether Android layout differs.
    //
    Scrooby::Group* pMenuGroup = pPage->GetGroup( "Menu" );

#if defined(RAD_ANDROID)

    for( int i = 0; i < NUM_PAUSE_SETTINGS_MENU_ITEMS; i++ )
    {
        const char* item = PAUSE_SETTINGS_MENU_ITEMS[ i ];

        const bool added = AddPauseSettingsMenuItemIfAvailable
        (
            pPage,
            item,
            i
        );

        if( i == MENU_ITEM_INVERT_CAM_CONTROL )
        {
            mHasInvertCamControl = added;
        }
        else if( i == MENU_ITEM_VIBRATION )
        {
            mHasGamepadVibration = added;
        }
    }

#else

    for( int i = 0; i < NUM_PAUSE_SETTINGS_MENU_ITEMS; i++ )
    {
        const char* item = PAUSE_SETTINGS_MENU_ITEMS[ i ];



        //
        // IMPORTANT:
        // This is the original access pattern.
        //
        Scrooby::Group* group = pPage->GetGroup( item );

        //
        // Diagnostic only. Do not use this group yet.
        //


        if( group == NULL )
        {
            rAssert( group != NULL );
            MEMTRACK_POP_GROUP( "CGuiScreenPauseSettings" );
            return;
        }

        Scrooby::Text* pText = group->GetText( item );


        if( pText == NULL )
        {
            rAssert( pText != NULL );
            MEMTRACK_POP_GROUP( "CGuiScreenPauseSettings" );
            return;
        }

        pText->SetTextMode( Scrooby::TEXT_WRAP );

        sprintf( itemName, "%s_Value", item );
        Scrooby::Text* pTextValue = group->GetText( itemName );


        if( pTextValue == NULL )
        {
            rAssert( pTextValue != NULL );
            MEMTRACK_POP_GROUP( "CGuiScreenPauseSettings" );
            return;
        }

        pTextValue->SetTextMode( Scrooby::TEXT_WRAP );

        sprintf( itemName, "%s_LArrow", item );
        Scrooby::Sprite* pLArrow = group->GetSprite( itemName );
        sprintf( itemName, "%s_RArrow", item );
        Scrooby::Sprite* pRArrow = group->GetSprite( itemName );



        m_pMenu->AddMenuItem( pText,
                              pTextValue,
                              NULL,
                              NULL,
                              pLArrow,
                              pRArrow,
                              SELECTION_ENABLED | VALUES_WRAPPED | TEXT_OUTLINE_ENABLED );
    }
#endif

#if defined(RAD_ANDROID)
    // Shift the entire Android Pause Settings menu as one group.
    // Positive values move the menu upward in the Android layout.
    const int ANDROID_SETTINGS_MENU_Y_OFFSET = 20;
    if( pMenuGroup != NULL )
    {
        pMenuGroup->Translate( 0, ANDROID_SETTINGS_MENU_Y_OFFSET );
    }
#endif

    #if defined(RAD_ANDROID)
    //
    // Dejamos de dibujar la opción tutorial en este menu
    //
    Scrooby::Group* pTutorialGroup = pPage->GetGroup( "Tutorial" );

    if( pTutorialGroup == NULL && pMenuGroup != NULL )
    {
        pTutorialGroup = pMenuGroup->GetGroup( "Tutorial" );
    }

    if( pTutorialGroup != NULL )
    {
        Scrooby::Text* pTutorialText = pTutorialGroup->GetText( "Tutorial" );
        if( pTutorialText != NULL )
        {
            pTutorialText->SetVisible( false );
        }

        Scrooby::Text* pTutorialValue = pTutorialGroup->GetText( "Tutorial_Value" );
        if( pTutorialValue != NULL )
        {
            pTutorialValue->SetVisible( false );
        }

        Scrooby::Sprite* pTutorialLArrow = pTutorialGroup->GetSprite( "Tutorial_LArrow" );
        if( pTutorialLArrow != NULL )
        {
            pTutorialLArrow->SetVisible( false );
        }

        Scrooby::Sprite* pTutorialRArrow = pTutorialGroup->GetSprite( "Tutorial_RArrow" );
        if( pTutorialRArrow != NULL )
        {
            pTutorialRArrow->SetVisible( false );
        }
    }
#endif

#ifdef RAD_GAMECUBE
    // change "Vibration" text to "Rumble"
    //
    Scrooby::Text* vibrationText = dynamic_cast<Scrooby::Text*>( m_pMenu->GetMenuItem( MENU_ITEM_VIBRATION )->GetItem() );
    rAssert( vibrationText != NULL );
    vibrationText->SetIndex( 1 );
#endif // RAD_GAMECUBE

    for( int j = 0; j < NUM_CAMERA_SELECTION_MODES; j++ )
    {
        m_cameraSelections[ j ] = NULL;
        m_numCameraSelections[ j ] = 0;
    }


    m_cameraSelections[ CAMERA_SELECTION_FOR_DRIVING ] = CAMERAS_FOR_DRIVING;

#ifdef RAD_PC
    m_cameraSelections[ CAMERA_SELECTION_FOR_WALKING ] = PC_CAMERAS_FOR_WALKING;
#else
    m_cameraSelections[ CAMERA_SELECTION_FOR_WALKING ] = CAMERAS_FOR_WALKING;
#endif



    if( GetCheatInputSystem()->IsCheatEnabled( CHEAT_ID_UNLOCK_CAMERAS ) )
    {
        m_numCameraSelections[ CAMERA_SELECTION_FOR_DRIVING ] = NUM_CAMERAS_FOR_DRIVING;

#ifdef RAD_PC
        m_numCameraSelections[ CAMERA_SELECTION_FOR_WALKING ] = NUM_PC_CAMERAS_FOR_WALKING;
#else
        m_numCameraSelections[ CAMERA_SELECTION_FOR_WALKING ] = NUM_CAMERAS_FOR_WALKING;
#endif
    }
    else
    {
        m_numCameraSelections[ CAMERA_SELECTION_FOR_DRIVING ] = NUM_CAMERAS_FOR_DRIVING_WITHOUT_CHEAT;

#ifdef RAD_PC
        m_numCameraSelections[ CAMERA_SELECTION_FOR_WALKING ] = NUM_PC_CAMERAS_FOR_WALKING_WITHOUT_CHEAT;
#else
        m_numCameraSelections[ CAMERA_SELECTION_FOR_WALKING ] = NUM_CAMERAS_FOR_WALKING_WITHOUT_CHEAT;
#endif
    }

    GetCheatInputSystem()->RegisterCallback( this );

#if defined(RAD_ANDROID)
    // Create a separate five-value Resolution text object for the Graphics
    // submenu. Never expand or overwrite the original Camera value resource.
    {
        Scrooby::Group* pResolutionGroup = pPage->GetGroup( "Camera" );
        FeGroup* pFeResolutionGroup = dynamic_cast<FeGroup*>( pResolutionGroup );
        FeText* pOriginalValue = pResolutionGroup != NULL
            ? dynamic_cast<FeText*>( pResolutionGroup->GetText( "Camera_Value" ) )
            : NULL;

        if( pFeResolutionGroup != NULL && pOriginalValue != NULL )
        {
            m_pOriginalResolutionValue = pOriginalValue;
            FeText* pGraphicsValue =
                pFeResolutionGroup->AddText( "AndroidGraphicsResolutionValue", 0, 0 );

            if( pGraphicsValue != NULL )
            {
                CopyAndroidMenuTextAppearance( pGraphicsValue, pOriginalValue );

                int width = 0;
                int height = 0;
                int centerX = 0;
                int centerY = 0;
                pOriginalValue->GetBoundingBoxSize( width, height );
                pOriginalValue->GetBoundingBoxCenter( centerX, centerY );
                pGraphicsValue->SetBoundingBoxSize( width, height );
                pGraphicsValue->SetPositionOfCenter( centerX, centerY );

                const char* resolutionValues[SHAR_GRAPHICS_RESOLUTION_COUNT] =
                {
                    "480p", "720p", "1080p", "1440p", "2160p"
                };
                for( int valueIndex = 0; valueIndex < SHAR_GRAPHICS_RESOLUTION_COUNT; ++valueIndex )
                {
                    pGraphicsValue->AddHardCodedString( resolutionValues[valueIndex] );
                }

                pGraphicsValue->SetIndex( 3 );
                pGraphicsValue->SetVisible( false );
                m_pGraphicsResolutionValue = pGraphicsValue;
            }
            else
            {
            }
        }
        else
        {
        }
    }

    // Keep the original Jump Cameras ON/OFF text intact; the Graphics page gets
    // a dedicated five-value frame-rate selector in the same row group.
    {
        Scrooby::Group* pFPSGroup = pPage->GetGroup( "JumpCamera" );
        FeGroup* pFeFPSGroup = dynamic_cast<FeGroup*>( pFPSGroup );
        FeText* pOriginalValue = pFPSGroup != NULL
            ? dynamic_cast<FeText*>( pFPSGroup->GetText( "JumpCamera_Value" ) )
            : NULL;

        if( pFeFPSGroup != NULL && pOriginalValue != NULL )
        {
            m_pOriginalFPSValue = pOriginalValue;
            FeText* pGraphicsValue =
                pFeFPSGroup->AddText( "AndroidGraphicsFPSValue", 0, 0 );

            if( pGraphicsValue != NULL )
            {
                CopyAndroidMenuTextAppearance( pGraphicsValue, pOriginalValue );

                int width = 0;
                int height = 0;
                int centerX = 0;
                int centerY = 0;
                pOriginalValue->GetBoundingBoxSize( width, height );
                pOriginalValue->GetBoundingBoxCenter( centerX, centerY );
                pGraphicsValue->SetBoundingBoxSize( width, height );
                pGraphicsValue->SetPositionOfCenter( centerX, centerY );

                const char* fpsValues[SHAR_GRAPHICS_FPS_COUNT] =
                {
                    "30", "60", "90", "120", "144"
                };
                for( int valueIndex = 0; valueIndex < SHAR_GRAPHICS_FPS_COUNT; ++valueIndex )
                {
                    pGraphicsValue->AddHardCodedString( fpsValues[valueIndex] );
                }

                pGraphicsValue->SetIndex( 1 );
                pGraphicsValue->SetVisible( false );
                m_pGraphicsFPSValue = pGraphicsValue;
            }
        }
    }

    // Do not expand the original Scrooby Settings value object to six strings.
    // Build a separate value object in the same row group and swap it in only
    // while the Graphics submenu is displayed. This keeps the resource's
    // original OFF/ON strings intact for the regular Settings menu.
    {
        // Sharpening belongs to the Invert Camera row. On some Android
        // layouts that row is provided by the normal resource group; on
        // others it is generated under the page's "Menu" group because the
        // original InvertCamControl artwork is absent. Create the six-value
        // selector alongside whichever value object the menu actually uses,
        // so it retains the correct row coordinates and group transforms.
        Scrooby::Group* pSharpeningGroup = pPage->GetGroup( "InvertCamControl" );
        FeGroup* pFeSharpeningGroup = dynamic_cast<FeGroup*>( pSharpeningGroup );
        FeText* pOriginalValue = pSharpeningGroup != NULL
            ? dynamic_cast<FeText*>( pSharpeningGroup->GetText( "InvertCamControl_Value" ) )
            : NULL;

        if( pFeSharpeningGroup == NULL || pOriginalValue == NULL )
        {
            Scrooby::Group* pMenuGroup = pPage->GetGroup( "Menu" );
            FeGroup* pFeMenuGroup = dynamic_cast<FeGroup*>( pMenuGroup );
            FeText* pGeneratedValue = pMenuGroup != NULL
                ? dynamic_cast<FeText*>(
                    pMenuGroup->GetText( "AndroidSettings_InvertCamera_Value" ) )
                : NULL;

            if( pFeMenuGroup != NULL && pGeneratedValue != NULL )
            {
                pSharpeningGroup = pMenuGroup;
                pFeSharpeningGroup = pFeMenuGroup;
                pOriginalValue = pGeneratedValue;
            }
        }

        if( pFeSharpeningGroup != NULL && pOriginalValue != NULL )
        {
            m_pOriginalSharpeningValue = pOriginalValue;
            FeText* pGraphicsValue =
                pFeSharpeningGroup->AddText( "AndroidGraphicsSharpeningValue", 0, 0 );

            if( pGraphicsValue != NULL )
            {
                CopyAndroidMenuTextAppearance( pGraphicsValue, pOriginalValue );

                int width = 0;
                int height = 0;
                int centerX = 0;
                int centerY = 0;
                pOriginalValue->GetBoundingBoxSize( width, height );
                pOriginalValue->GetBoundingBoxCenter( centerX, centerY );
                pGraphicsValue->SetBoundingBoxSize( width, height );
                pGraphicsValue->SetPositionOfCenter( centerX, centerY );

                const char* sharpeningValues[6] =
                {
                    "OFF", "20%", "40%", "60%", "80%", "MAX"
                };
                for( int valueIndex = 0; valueIndex < 6; ++valueIndex )
                {
                    pGraphicsValue->AddHardCodedString( sharpeningValues[valueIndex] );
                }

                pGraphicsValue->SetIndex( 0 );
                pGraphicsValue->SetVisible( false );
                m_pGraphicsSharpeningValue = pGraphicsValue;
            }
            else
            {
            }
        }
        else
        {
        }
    }

    // The second Graphics page places Sharpening on the Radar row.
    // Keep its selector separate from the native Settings values.
    {
        Scrooby::Group* pSharpeningGroup = pPage->GetGroup( "Radar" );
        FeGroup* pFeSharpeningGroup = dynamic_cast<FeGroup*>( pSharpeningGroup );
        FeText* pOriginalValue = pSharpeningGroup != NULL
            ? dynamic_cast<FeText*>( pSharpeningGroup->GetText( "Radar_Value" ) )
            : NULL;

        if( pFeSharpeningGroup == NULL || pOriginalValue == NULL )
        {
            Scrooby::Group* pMenuGroup = pPage->GetGroup( "Menu" );
            FeGroup* pFeMenuGroup = dynamic_cast<FeGroup*>( pMenuGroup );
            FeText* pGeneratedValue = pMenuGroup != NULL
                ? dynamic_cast<FeText*>(
                    pMenuGroup->GetText( "AndroidSettings_Radar_Value" ) )
                : NULL;

            if( pFeMenuGroup != NULL && pGeneratedValue != NULL )
            {
                pSharpeningGroup = pMenuGroup;
                pFeSharpeningGroup = pFeMenuGroup;
                pOriginalValue = pGeneratedValue;
            }
        }

        if( pFeSharpeningGroup != NULL && pOriginalValue != NULL )
        {
            m_pOriginalRadarValue = pOriginalValue;
            FeText* pGraphicsValue =
                pFeSharpeningGroup->AddText( "AndroidMoreGraphicsSharpeningValue", 0, 0 );
            if( pGraphicsValue != NULL )
            {
                CopyAndroidMenuTextAppearance( pGraphicsValue, pOriginalValue );
                int width = 0, height = 0, centerX = 0, centerY = 0;
                pOriginalValue->GetBoundingBoxSize( width, height );
                pOriginalValue->GetBoundingBoxCenter( centerX, centerY );
                pGraphicsValue->SetBoundingBoxSize( width, height );
                pGraphicsValue->SetPositionOfCenter( centerX, centerY );

                const char* sharpeningValues[5] =
                {
                    "20%", "40%", "60%", "80%", "MAX"
                };
                for( int valueIndex = 0; valueIndex < 5; ++valueIndex )
                    pGraphicsValue->AddHardCodedString( sharpeningValues[valueIndex] );

                pGraphicsValue->SetIndex( 1 );
                pGraphicsValue->SetVisible( false );
                m_pMoreGraphicsSharpeningValue = pGraphicsValue;
            }
        }
    }

#endif

MEMTRACK_POP_GROUP( "CGuiScreenPauseSettings" );
}
#if defined(RAD_ANDROID)

void CGuiScreenPauseSettings::ConfigureAndroidGraphicsMenu()
{
    m_configuringAndroidSubmenu = true;
    gSHARGraphicsResolutionSelection = LoadSHARGraphicsResolutionSelection();
    gSHARGraphicsFPSSelection = LoadSHARGraphicsFPSSelection();
    SetSHARAndroidTargetFPS( GetSHARAndroidTargetFPS() );
    RequestSHARAndroidDisplayFrameRate( GetSHARAndroidTargetFPS() );
    if( gSHARGraphicsResolutionNeedsNormalization )
    {
        // Normalize legacy MAX/custom settings to an explicit menu profile.
        // 2160p remains available as an opt-in supersampling target on phones.
        if( SaveSHARGraphicsResolutionSelection( gSHARGraphicsResolutionSelection ) )
        {
            gSHARGraphicsResolutionNeedsNormalization = false;
        }
        else
        {
            LOGE("[SHAR-RESOLUTION] Could not normalize unsupported config; retrying next Graphics visit");
        }
    }
    // Reuse four Settings rows without editing Scrooby/game assets.
    // Camera is Resolution, and the existing Back button leaves this submenu.
    if( m_pGraphicsResolutionValue != NULL )
    {
        const char* resolutionValues[SHAR_GRAPHICS_RESOLUTION_COUNT] =
        {
            "480p", "720p", "1080p", "1440p", "2160p"
        };
        for( int valueIndex = 0; valueIndex < SHAR_GRAPHICS_RESOLUTION_COUNT; ++valueIndex )
        {
            m_pGraphicsResolutionValue->SetString( valueIndex, resolutionValues[valueIndex] );
        }
    }

    if( m_pGraphicsFPSValue != NULL )
    {
        const char* fpsValues[SHAR_GRAPHICS_FPS_COUNT] =
        {
            "30", "60", "90", "120", "144"
        };
        for( int valueIndex = 0; valueIndex < SHAR_GRAPHICS_FPS_COUNT; ++valueIndex )
        {
            m_pGraphicsFPSValue->SetString( valueIndex, fpsValues[valueIndex] );
        }
    }

    const int logicalItems[6] =
    {
        MENU_ITEM_CAMERA,
        MENU_ITEM_JUMP_CAMERAS,
        MENU_ITEM_INTERSECT_NAV_SYSTEM,
        MENU_ITEM_RADAR,
        MENU_ITEM_INVERT_CAM_CONTROL,
        MENU_ITEM_VIBRATION
    };

    const char* labels[6] =
    {
        "Resolution",
        "Max FPS",
        "Cel Shading",
        "Bloom",
        "Cloud Shadows",
        "More Options.."
    };

    const bool celShadingEnabled = IsCelShadingEnabled();
    const bool bloomEnabled = IsSHARBloomEnabled();
    const bool cloudShadowsEnabled = IsSHARCloudShadowsEnabled();

    const char* sharpeningValues[6] =
    {
        "OFF", "20%", "40%", "60%", "80%", "MAX"
    };
    const int sharpeningLevel = GetFrameSharpeningLevel();

    if( gSHARGraphicsResolutionSelection < 0 ||
        gSHARGraphicsResolutionSelection >= SHAR_GRAPHICS_RESOLUTION_COUNT )
    {
        gSHARGraphicsResolutionSelection = 2;
    }
    if( gSHARGraphicsFPSSelection < 0 ||
        gSHARGraphicsFPSSelection >= SHAR_GRAPHICS_FPS_COUNT )
    {
        gSHARGraphicsFPSSelection = 1;
    }

    for( int i = 0; i < 6; ++i )
    {
        const int guiIndex = GetGuiMenuIndexFromMenuItem( logicalItems[i] );
        if( guiIndex < 0 )
        {
            continue;
        }

        GuiMenuItem* item = m_pMenu->GetMenuItem( guiIndex );
        if( item == NULL )
        {
            continue;
        }

        if( i == 0 && m_pGraphicsResolutionValue != NULL &&
            m_pOriginalResolutionValue != NULL )
        {
            item->SetItemValue( m_pGraphicsResolutionValue );
            m_pOriginalResolutionValue->SetVisible( false );
            m_pGraphicsResolutionValue->SetVisible( true );
        }
        else if( i == 1 && m_pGraphicsFPSValue != NULL &&
                 m_pOriginalFPSValue != NULL )
        {
            item->SetItemValue( m_pGraphicsFPSValue );
            m_pOriginalFPSValue->SetVisible( false );
            m_pGraphicsFPSValue->SetVisible( true );
        }
        // The last row is an action that opens the second Graphics page.
        item->m_attributes &= ~SELECTABLE;
        item->m_showValueArrows = ( i != 5 );
        item->m_useBackSoundOnSelection = false;
        if( i == 5 )
        {
            item->m_attributes |= SELECTABLE;
        }

        m_pMenu->SetMenuItemEnabled( guiIndex, true, true );

        Scrooby::Text* label = dynamic_cast<Scrooby::Text*>( item->GetItem() );
        Scrooby::Text* value = dynamic_cast<Scrooby::Text*>( item->GetItemValue() );

        if( label != NULL )
        {
            label->SetString( 0, labels[i] );
            label->SetVisible( true );
        }

        if( value != NULL )
        {
            if( i == 0 )
            {
                value->SetVisible( true );
                if( m_pGraphicsResolutionValue != NULL &&
                    value == m_pGraphicsResolutionValue )
                {
                    m_pMenu->SetSelectionValueCount( guiIndex, SHAR_GRAPHICS_RESOLUTION_COUNT );
                    m_pMenu->SetSelectionValue( guiIndex, gSHARGraphicsResolutionSelection );
                }
                else
                {
                    // Defensive fallback if the dedicated five-value text object
                    // cannot be created. Keep the row safe and visibly diagnostic.
                    value->SetString( 0, "1080p" );
                    if( value->GetNumOfStrings() > 1 )
                    {
                        value->SetString( 1, "1080p" );
                    }
                    // If we could not create the dedicated selector text, avoid
                    // exposing an unsupported 2160p fallback; keep the row safe.
                    m_pMenu->SetSelectionValueCount( guiIndex, 1 );
                    m_pMenu->SetSelectionValue( guiIndex, 0 );
                }
            }
            else if( i == 1 )
            {
                value->SetVisible( true );
                if( m_pGraphicsFPSValue != NULL &&
                    value == m_pGraphicsFPSValue )
                {
                    m_pMenu->SetSelectionValueCount( guiIndex, SHAR_GRAPHICS_FPS_COUNT );
                    m_pMenu->SetSelectionValue( guiIndex, gSHARGraphicsFPSSelection );
                }
                else
                {
                    // Defensive fallback if the dedicated FPS text object cannot
                    // be created; retain a visible, safe 60 FPS setting.
                    value->SetString( 0, "60" );
                    if( value->GetNumOfStrings() > 1 )
                    {
                        value->SetString( 1, "60" );
                    }
                    m_pMenu->SetSelectionValueCount( guiIndex, 1 );
                    m_pMenu->SetSelectionValue( guiIndex, 0 );
                }
            }
            else if( i == 2 || i == 3 || i == 4 )
            {
                value->SetVisible( true );
                value->SetString( 0, "OFF" );
                value->SetString( 1, "ON" );
                m_pMenu->SetSelectionValueCount( guiIndex, 2 );
                const bool enabled =
                    ( i == 2 ) ? celShadingEnabled :
                    ( i == 3 ) ? bloomEnabled : cloudShadowsEnabled;
                m_pMenu->SetSelectionValue( guiIndex, enabled ? 1 : 0 );
            }
            else if( i == 5 )
            {
                // More Options is an action, not an ON/OFF value selector.
                value->SetVisible( false );
                m_pMenu->SetSelectionValueCount( guiIndex, 1 );
                m_pMenu->SetSelectionValue( guiIndex, 0 );
            }
            else
            {
                value->SetVisible( false );
            }
        }

        if( item->m_itemValueArrowL != NULL )
        {
            item->m_itemValueArrowL->SetVisible( false );
        }
        if( item->m_itemValueArrowR != NULL )
        {
            item->m_itemValueArrowR->SetVisible( false );
        }
    }

    m_pMenu->Reset( 0 );
    m_configuringAndroidSubmenu = false;
}

void CGuiScreenPauseSettings::ConfigureAndroidMoreGraphicsMenu()
{
    m_configuringAndroidSubmenu = true;

    const int logicalItems[6] =
    {
        MENU_ITEM_CAMERA,
        MENU_ITEM_JUMP_CAMERAS,
        MENU_ITEM_INTERSECT_NAV_SYSTEM,
        MENU_ITEM_RADAR,
        MENU_ITEM_INVERT_CAM_CONTROL,
        MENU_ITEM_VIBRATION
    };

    const char* labels[5] =
    {
        "CRT Filter",
        "Motion Blur",
        "Rim Lighting",
        "Sharpening",
        "Anisotropic Filtering"
    };

    const bool crtEnabled = IsCRTFilterEnabled();
    const bool motionBlurEnabled = IsMotionBlurEnabled();
    const bool rimLightingEnabled = IsSHARRimLightingEnabled();
    const int sharpeningLevel = GetFrameSharpeningLevel();
    const int anisotropicSelection = GetSHARAnisotropicFilteringSelection();

    for( int i = 0; i < 6; ++i )
    {
        const int guiIndex = GetGuiMenuIndexFromMenuItem( logicalItems[i] );
        if( guiIndex < 0 )
            continue;

        GuiMenuItem* item = m_pMenu->GetMenuItem( guiIndex );
        if( item == NULL )
            continue;

        if( i >= 5 )
        {
            item->m_attributes &= ~SELECTABLE;
            item->m_showValueArrows = false;
            Scrooby::Text* hiddenLabel = dynamic_cast<Scrooby::Text*>( item->GetItem() );
            Scrooby::Text* hiddenValue = dynamic_cast<Scrooby::Text*>( item->GetItemValue() );
            if( hiddenLabel != NULL ) hiddenLabel->SetVisible( false );
            if( hiddenValue != NULL ) hiddenValue->SetVisible( false );
            if( item->m_itemValueArrowL != NULL ) item->m_itemValueArrowL->SetVisible( false );
            if( item->m_itemValueArrowR != NULL ) item->m_itemValueArrowR->SetVisible( false );
            m_pMenu->SetMenuItemEnabled( guiIndex, false, true );
            continue;
        }

        item->m_attributes &= ~SELECTABLE;
        item->m_showValueArrows = true;
        item->m_useBackSoundOnSelection = false;
        m_pMenu->SetMenuItemEnabled( guiIndex, true, true );

        // Use native row-value objects for the first three toggles. The
        // Sharpening and Anisotropic Filtering selectors use private value text.
        if( i == 0 && m_pGraphicsResolutionValue != NULL &&
            m_pOriginalResolutionValue != NULL )
        {
            m_pOriginalResolutionValue->SetVisible( false );
            m_pGraphicsResolutionValue->SetVisible( true );
            item->SetItemValue( m_pGraphicsResolutionValue );
        }
        else if( i == 1 && m_pGraphicsFPSValue != NULL &&
                 m_pOriginalFPSValue != NULL )
        {
            m_pOriginalFPSValue->SetVisible( false );
            m_pGraphicsFPSValue->SetVisible( true );
            item->SetItemValue( m_pGraphicsFPSValue );
        }
        else if( i == 3 && m_pMoreGraphicsSharpeningValue != NULL )
        {
            if( m_pOriginalRadarValue != NULL )
                m_pOriginalRadarValue->SetVisible( false );
            m_pMoreGraphicsSharpeningValue->SetVisible( true );
            item->SetItemValue( m_pMoreGraphicsSharpeningValue );
        }
        else if( i == 4 && m_pGraphicsSharpeningValue != NULL )
        {
            if( m_pOriginalSharpeningValue != NULL )
                m_pOriginalSharpeningValue->SetVisible( false );
            m_pGraphicsSharpeningValue->SetVisible( true );
            item->SetItemValue( m_pGraphicsSharpeningValue );
        }

        Scrooby::Text* label = dynamic_cast<Scrooby::Text*>( item->GetItem() );
        Scrooby::Text* value = dynamic_cast<Scrooby::Text*>( item->GetItemValue() );
        if( label != NULL )
        {
            label->SetString( 0, labels[i] );
            label->SetVisible( true );
        }

        if( value != NULL )
        {
            value->SetVisible( true );
            if( i <= 2 )
            {
                value->SetString( 0, "OFF" );
                value->SetString( 1, "ON" );
                m_pMenu->SetSelectionValueCount( guiIndex, 2 );
                const bool enabled = i == 0 ? crtEnabled :
                                     i == 1 ? motionBlurEnabled : rimLightingEnabled;
                m_pMenu->SetSelectionValue( guiIndex, enabled ? 1 : 0 );
            }
            else if( i == 3 && m_pMoreGraphicsSharpeningValue != NULL )
            {
                const char* values[5] = { "20%", "40%", "60%", "80%", "MAX" };
                for( int valueIndex = 0; valueIndex < 5; ++valueIndex )
                    value->SetString( valueIndex, values[valueIndex] );
                m_pMenu->SetSelectionValueCount( guiIndex, 5 );
                // Level 0 is legacy OFF; the five-choice selector represents
                // levels 1..5 as 20%, 40%, 60%, 80%, and MAX.
                int selection = sharpeningLevel > 0 ? sharpeningLevel - 1 : 0;
                if( selection > 4 ) selection = 4;
                m_pMenu->SetSelectionValue( guiIndex, selection );
            }
            else if( i == 4 && m_pGraphicsSharpeningValue != NULL )
            {
                const char* values[5] = { "2x", "4x", "8x", "12x", "16x" };
                for( int valueIndex = 0; valueIndex < 5; ++valueIndex )
                    value->SetString( valueIndex, values[valueIndex] );
                m_pMenu->SetSelectionValueCount( guiIndex, 5 );
                int selection = anisotropicSelection;
                if( selection < 0 ) selection = 0;
                if( selection > 4 ) selection = 4;
                m_pMenu->SetSelectionValue( guiIndex, selection );
            }
        }

        if( item->m_itemValueArrowL != NULL ) item->m_itemValueArrowL->SetVisible( false );
        if( item->m_itemValueArrowR != NULL ) item->m_itemValueArrowR->SetVisible( false );
    }

    m_pMenu->Reset( 0 );
    m_configuringAndroidSubmenu = false;
}

void CGuiScreenPauseSettings::ConfigureAndroidCheatsMenu()
{
    m_configuringAndroidSubmenu = true;

    // Five ordinary ON/OFF cheats and one action to open More Cheats.
    const int logicalItems[6] =
    {
        MENU_ITEM_CAMERA,
        MENU_ITEM_JUMP_CAMERAS,
        MENU_ITEM_INTERSECT_NAV_SYSTEM,
        MENU_ITEM_RADAR,
        MENU_ITEM_INVERT_CAM_CONTROL,
        MENU_ITEM_VIBRATION
    };

    const char* labels[6] =
    {
        "Moon Jump",
        "Faster Acceleration",
        "Vehicle Invincibility",
        "Car Horn Jump",
        "One Hit Tank Cars",
        "More Cheats.."
    };

    CheatInputSystem* cheats = GetCheatInputSystem();
    const bool enabled[5] =
    {
        cheats->IsMoonJumpEnabled(),
        cheats->IsCheatEnabled( CHEAT_ID_HIGH_ACCELERATION ),
        cheats->IsCheatEnabled( CHEAT_ID_INVINCIBLE_CAR ),
        cheats->IsCheatEnabled( CHEAT_ID_CAR_JUMP_ON_HORN ),
        cheats->IsCheatEnabled( CHEAT_ID_ONE_TAP_TRAFFIC_DEATH )
    };

    for( int i = 0; i < 6; ++i )
    {
        const int guiIndex = GetGuiMenuIndexFromMenuItem( logicalItems[i] );
        if( guiIndex < 0 )
        {
            continue;
        }

        GuiMenuItem* item = m_pMenu->GetMenuItem( guiIndex );
        if( item == NULL )
        {
            continue;
        }

        if( i == 0 && m_pGraphicsResolutionValue != NULL &&
            m_pOriginalResolutionValue != NULL )
        {
            item->SetItemValue( m_pGraphicsResolutionValue );
            m_pOriginalResolutionValue->SetVisible( false );
            m_pGraphicsResolutionValue->SetVisible( true );
        }

        item->m_attributes &= ~SELECTABLE;
        item->m_showValueArrows = ( i != 5 );
        item->m_useBackSoundOnSelection = false;
        if( i == 5 )
        {
            // This is a selectable action, not a value toggle.
            item->m_attributes |= SELECTABLE;
        }
        m_pMenu->SetMenuItemEnabled( guiIndex, true, true );

        Scrooby::Text* label = dynamic_cast<Scrooby::Text*>( item->GetItem() );
        Scrooby::Text* value = dynamic_cast<Scrooby::Text*>( item->GetItemValue() );
        if( label != NULL )
        {
            label->SetString( 0, labels[i] );
            label->SetVisible( true );
        }

        if( value != NULL )
        {
            if( i == 5 )
            {
                // More Cheats is an action row, so show no value text beside it.
                value->SetVisible( false );
                m_pMenu->SetSelectionValueCount( guiIndex, 1 );
                m_pMenu->SetSelectionValue( guiIndex, 0 );
            }
            else
            {
                if( value->GetNumOfStrings() > 0 ) value->SetString( 0, "OFF" );
                if( value->GetNumOfStrings() > 1 ) value->SetString( 1, "ON" );
                value->SetVisible( true );
                m_pMenu->SetSelectionValueCount( guiIndex, 2 );
                m_pMenu->SetSelectionValue( guiIndex, enabled[i] ? 1 : 0 );
            }
        }

        if( item->m_itemValueArrowL != NULL )
        {
            item->m_itemValueArrowL->SetVisible( false );
        }
        if( item->m_itemValueArrowR != NULL )
        {
            item->m_itemValueArrowR->SetVisible( false );
        }
    }

    m_pMenu->Reset( 0 );
    m_configuringAndroidSubmenu = false;
}

void CGuiScreenPauseSettings::ConfigureAndroidMoreCheatsMenu()
{
    m_configuringAndroidSubmenu = true;

    // Reuse the five Settings rows for the requested secondary cheat toggles.
    const int logicalItems[5] =
    {
        MENU_ITEM_CAMERA,
        MENU_ITEM_JUMP_CAMERAS,
        MENU_ITEM_INTERSECT_NAV_SYSTEM,
        MENU_ITEM_RADAR,
        MENU_ITEM_INVERT_CAM_CONTROL
    };

    const char* labels[5] =
    {
        "Noclip",
        "Character Swap",
        "Unlock All Cars",
        "Unlimited Speed",
        "Speedometer"
    };

    CheatInputSystem* cheats = GetCheatInputSystem();
    const bool enabled[5] =
    {
        IsSHARNoclipEnabled(),
        cheats->IsCheatEnabled( CHEAT_ID_KICK_TOGGLES_CHARACTER_MODEL ),
        cheats->IsCheatEnabled( CHEAT_ID_UNLOCK_VEHICLES ),
        cheats->IsCheatEnabled( CHEAT_ID_NO_TOP_SPEED ),
        cheats->IsCheatEnabled( CHEAT_ID_SHOW_SPEEDOMETER )
    };

    for( int i = 0; i < 5; ++i )
    {
        const int guiIndex = GetGuiMenuIndexFromMenuItem( logicalItems[i] );
        if( guiIndex < 0 )
        {
            continue;
        }

        GuiMenuItem* item = m_pMenu->GetMenuItem( guiIndex );
        if( item == NULL )
        {
            continue;
        }

        if( i == 0 && m_pGraphicsResolutionValue != NULL &&
            m_pOriginalResolutionValue != NULL )
        {
            item->SetItemValue( m_pGraphicsResolutionValue );
            m_pOriginalResolutionValue->SetVisible( false );
            m_pGraphicsResolutionValue->SetVisible( true );
        }

        item->m_attributes &= ~SELECTABLE;
        item->m_showValueArrows = true;
        item->m_useBackSoundOnSelection = false;
        m_pMenu->SetMenuItemEnabled( guiIndex, true, true );

        Scrooby::Text* label = dynamic_cast<Scrooby::Text*>( item->GetItem() );
        Scrooby::Text* value = dynamic_cast<Scrooby::Text*>( item->GetItemValue() );
        if( label != NULL )
        {
            label->SetString( 0, labels[i] );
            label->SetVisible( true );
        }

        if( value != NULL )
        {
            if( value->GetNumOfStrings() > 0 ) value->SetString( 0, "OFF" );
            if( value->GetNumOfStrings() > 1 ) value->SetString( 1, "ON" );
            value->SetVisible( true );
            m_pMenu->SetSelectionValueCount( guiIndex, 2 );
            m_pMenu->SetSelectionValue( guiIndex, enabled[i] ? 1 : 0 );
        }

        if( item->m_itemValueArrowL != NULL )
        {
            item->m_itemValueArrowL->SetVisible( false );
        }
        if( item->m_itemValueArrowR != NULL )
        {
            item->m_itemValueArrowR->SetVisible( false );
        }
    }

    // Hide the sixth row because Grid Mode is intentionally omitted.
    const int unusedGuiIndex = GetGuiMenuIndexFromMenuItem( MENU_ITEM_VIBRATION );
    if( unusedGuiIndex >= 0 )
    {
        GuiMenuItem* unusedItem = m_pMenu->GetMenuItem( unusedGuiIndex );
        if( unusedItem != NULL )
        {
            Scrooby::Text* unusedLabel =
                dynamic_cast<Scrooby::Text*>( unusedItem->GetItem() );
            Scrooby::Text* unusedValue =
                dynamic_cast<Scrooby::Text*>( unusedItem->GetItemValue() );
            if( unusedLabel != NULL ) unusedLabel->SetVisible( false );
            if( unusedValue != NULL ) unusedValue->SetVisible( false );
            if( unusedItem->m_itemValueArrowL != NULL )
                unusedItem->m_itemValueArrowL->SetVisible( false );
            if( unusedItem->m_itemValueArrowR != NULL )
                unusedItem->m_itemValueArrowR->SetVisible( false );
        }
        m_pMenu->SetMenuItemEnabled( unusedGuiIndex, false, true );
    }

    m_pMenu->Reset( 0 );
    m_configuringAndroidSubmenu = false;
}

void CGuiScreenPauseSettings::RestoreAndroidSettingsMenu()
{
    const int logicalItems[6] =
    {
        MENU_ITEM_CAMERA,
        MENU_ITEM_JUMP_CAMERAS,
        MENU_ITEM_INTERSECT_NAV_SYSTEM,
        MENU_ITEM_RADAR,
        MENU_ITEM_INVERT_CAM_CONTROL,
        MENU_ITEM_VIBRATION
    };

    const char* labels[6] =
    {
        "Camera",
        "Jump Cameras",
        "Navigation System",
        "Radar",
        "Invert Camera",
        "Controller Vibration"
    };

    for( int i = 0; i < 6; ++i )
    {
        const int guiIndex = GetGuiMenuIndexFromMenuItem( logicalItems[i] );
        if( guiIndex < 0 )
        {
            continue;
        }

        GuiMenuItem* item = m_pMenu->GetMenuItem( guiIndex );
        if( item == NULL )
        {
            continue;
        }

        // Restore the original Camera value object after leaving Graphics.
        if( logicalItems[i] == MENU_ITEM_CAMERA &&
            m_pOriginalResolutionValue != NULL &&
            item->GetItemValue() == m_pGraphicsResolutionValue )
        {
            m_pGraphicsResolutionValue->SetVisible( false );
            m_pOriginalResolutionValue->SetVisible( true );
            item->SetItemValue( m_pOriginalResolutionValue );
        }

        // Restore the original Jump Cameras value text after the Graphics FPS selector.
        if( logicalItems[i] == MENU_ITEM_JUMP_CAMERAS &&
            m_pOriginalFPSValue != NULL &&
            item->GetItemValue() == m_pGraphicsFPSValue )
        {
            if( m_pGraphicsFPSValue != NULL ) m_pGraphicsFPSValue->SetVisible( false );
            m_pOriginalFPSValue->SetVisible( true );
            item->SetItemValue( m_pOriginalFPSValue );
        }

        if( logicalItems[i] == MENU_ITEM_RADAR &&
            m_pOriginalRadarValue != NULL &&
            item->GetItemValue() == m_pMoreGraphicsSharpeningValue )
        {
            if( m_pMoreGraphicsSharpeningValue != NULL )
                m_pMoreGraphicsSharpeningValue->SetVisible( false );
            m_pOriginalRadarValue->SetVisible( true );
            item->SetItemValue( m_pOriginalRadarValue );
        }

        // Restore the original Invert Camera value text after the
        // temporary sharpening-percentage selector is removed.
        if( logicalItems[i] == MENU_ITEM_INVERT_CAM_CONTROL &&
            m_pOriginalSharpeningValue != NULL &&
            item->GetItemValue() == m_pGraphicsSharpeningValue )
        {
            m_pGraphicsSharpeningValue->SetVisible( false );
            m_pOriginalSharpeningValue->SetVisible( true );
            item->SetItemValue( m_pOriginalSharpeningValue );
        }

        // Restore the original non-selectable value-row behaviour and normal
        // menu item interaction after leaving the graphics submenu.
        item->m_attributes &= ~SELECTABLE;
        item->m_showValueArrows = true;
        item->m_useBackSoundOnSelection = false;

        Scrooby::Text* label = dynamic_cast<Scrooby::Text*>( item->GetItem() );
        Scrooby::Text* value = dynamic_cast<Scrooby::Text*>( item->GetItemValue() );

        if( label != NULL )
        {
            label->SetString( 0, labels[i] );
            label->SetVisible( true );
        }

        if( value != NULL )
        {
            // The graphics submenu temporarily replaces the first three
            // toggle strings and expands Sharpening to six entries. Restore
            // the normal toggle text and original value count here.
            if( i < 3 )
            {
                if( value->GetNumOfStrings() > 0 ) value->SetString( 0, "OFF" );
                if( value->GetNumOfStrings() > 1 ) value->SetString( 1, "ON" );
            }
            value->SetVisible( true );
            m_pMenu->SetSelectionValueCount( guiIndex, item->m_originalItemValueCount );
        }

        if( item->m_itemValueArrowL != NULL )
        {
            item->m_itemValueArrowL->SetVisible( false );
        }
        if( item->m_itemValueArrowR != NULL )
        {
            item->m_itemValueArrowR->SetVisible( false );
        }

        bool enableItem = true;
        if( logicalItems[i] == MENU_ITEM_INVERT_CAM_CONTROL )
        {
            enableItem = mHasInvertCamControl;
        }
        else if( logicalItems[i] == MENU_ITEM_VIBRATION )
        {
            enableItem = mHasGamepadVibration;
        }

        m_pMenu->SetMenuItemEnabled( guiIndex, enableItem, true );
    }

    const int tutorialGuiIndex = GetGuiMenuIndexFromMenuItem( MENU_ITEM_TUTORIAL );
    if( tutorialGuiIndex >= 0 )
    {
        GuiMenuItem* tutorialItem = m_pMenu->GetMenuItem( tutorialGuiIndex );
        if( tutorialItem != NULL )
        {
            tutorialItem->m_attributes &= ~SELECTABLE;
            tutorialItem->m_showValueArrows = false;
            tutorialItem->m_useBackSoundOnSelection = false;

            Scrooby::Text* label = dynamic_cast<Scrooby::Text*>( tutorialItem->GetItem() );
            Scrooby::Text* value = dynamic_cast<Scrooby::Text*>( tutorialItem->GetItemValue() );
            if( label != NULL )
            {
                label->SetString( 0, "Tutorial" );
                label->SetVisible( false );
            }
            if( value != NULL )
            {
                value->SetVisible( false );
            }
            if( tutorialItem->m_itemValueArrowL != NULL )
            {
                tutorialItem->m_itemValueArrowL->SetVisible( false );
            }
            if( tutorialItem->m_itemValueArrowR != NULL )
            {
                tutorialItem->m_itemValueArrowR->SetVisible( false );
            }
            m_pMenu->SetMenuItemEnabled( tutorialGuiIndex, false, true );
        }
    }

    m_pMenu->Reset( 0 );
}

void CGuiScreenPauseSettings::LeaveAndroidGraphicsMenu( bool backSoundAlreadyPlayed )
{
    // Both Graphics pages leave directly to the parent Options menu, matching Cheats.
    // Keep graphics mode active through the outro so custom values are not applied
    // as camera and gameplay settings.
    m_moreGraphicsSettingsMode = false;
    gAndroidGraphicsSettingsRequested = false;
    m_pParent->HandleMessage( GUI_MSG_BACK_SCREEN );

    // Play the normal back sound for the controller Back-button path.
    if( !backSoundAlreadyPlayed )
    {
        GetEventManager()->TriggerEvent( EVENT_FE_MENU_BACK );
    }
}

void CGuiScreenPauseSettings::LeaveAndroidCheatsMenu( bool backSoundAlreadyPlayed )
{
    // Keep Cheats mode active through the outro so its toggles are not
    // mistaken for ordinary camera, navigation, radar, or controller settings.
    gAndroidCheatsSettingsRequested = false;
    m_pParent->HandleMessage( GUI_MSG_BACK_SCREEN );

    if( !backSoundAlreadyPlayed )
    {
        GetEventManager()->TriggerEvent( EVENT_FE_MENU_BACK );
    }
}

#endif

//===========================================================================
// CGuiScreenPauseSettings::~CGuiScreenPauseSettings
//===========================================================================
// Description: Destructor.
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
CGuiScreenPauseSettings::~CGuiScreenPauseSettings()
{
    GetCheatInputSystem()->UnregisterCallback( this );

    if( m_pMenu != NULL )
    {
        delete m_pMenu;
        m_pMenu = NULL;
    }
}


//===========================================================================
// CGuiScreenPauseSettings::HandleMessage
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenPauseSettings::HandleMessage
(
	eGuiMessage message, 
	unsigned int param1,
	unsigned int param2 
)
{
    if( m_state == GUI_WINDOW_STATE_RUNNING )
    {
        switch( message )
        {
#if defined(RAD_ANDROID)
            case GUI_MSG_CONTROLLER_BACK:
            {
                if( m_graphicsSettingsMode )
                {
                    LeaveAndroidGraphicsMenu( false );
                    return;
                }
                if( m_cheatsSettingsMode )
                {
                    LeaveAndroidCheatsMenu( false );
                    return;
                }
                break;
            }
#endif
            case GUI_MSG_CONTROLLER_START:
            {
                // resume game
                //
                m_pParent->HandleMessage( GUI_MSG_UNPAUSE_INGAME );

                break;
            }

            case GUI_MSG_MENU_SELECTION_MADE:
            {
#if defined(RAD_ANDROID)
                // More Options opens a second Graphics page. Back from that page
                // follows the same direct-to-parent behaviour as the Cheats pages.
                if( m_graphicsSettingsMode && !m_moreGraphicsSettingsMode &&
                    GetMenuItemFromGuiMenuIndex( static_cast<int>( param1 ) ) ==
                        MENU_ITEM_VIBRATION )
                {
                    m_moreGraphicsSettingsMode = true;
                    ConfigureAndroidMoreGraphicsMenu();
                }
                // The action row opens More Cheats in-place.
                if( m_cheatsSettingsMode && !m_moreCheatsSettingsMode &&
                    GetMenuItemFromGuiMenuIndex( static_cast<int>( param1 ) ) ==
                        MENU_ITEM_VIBRATION )
                {
                    m_moreCheatsSettingsMode = true;
                    ConfigureAndroidMoreCheatsMenu();
                }
#endif
                break;
            }

            case GUI_MSG_MENU_SELECTION_VALUE_CHANGED:
            {
            #if defined(RAD_ANDROID)
                const int logicalMenuItem =
                    GetMenuItemFromGuiMenuIndex( static_cast<int>( param1 ) );

                if( m_graphicsSettingsMode || m_cheatsSettingsMode )
                {
                    // Ignore synthetic value-change messages generated while either
                    // custom submenu is being configured.
                    if( m_configuringAndroidSubmenu )
                    {
                        break;
                    }

                    const bool enabled = ( param2 == 1 );
                    if( m_graphicsSettingsMode )
                    {
                        if( m_moreGraphicsSettingsMode )
                        {
                            if( logicalMenuItem == MENU_ITEM_CAMERA )
                            {
                                SetCRTFilterEnabled( enabled );
                            }
                            else if( logicalMenuItem == MENU_ITEM_JUMP_CAMERAS )
                            {
                                SetMotionBlurEnabled( enabled );
                            }
                            else if( logicalMenuItem == MENU_ITEM_INTERSECT_NAV_SYSTEM )
                            {
                                SetSHARRimLightingEnabled( enabled );
                            }
                            else if( logicalMenuItem == MENU_ITEM_RADAR )
                            {
                                if( param2 < 5 )
                                    SetFrameSharpeningLevel( static_cast<int>( param2 ) + 1 );
                            }
                            else if( logicalMenuItem == MENU_ITEM_INVERT_CAM_CONTROL )
                            {
                                SetSHARAnisotropicFilteringSelection( static_cast<int>( param2 ) );
                            }
                        }
                        else if( logicalMenuItem == MENU_ITEM_CAMERA )
                        {
                            if( param2 < SHAR_GRAPHICS_RESOLUTION_COUNT )
                            {
                                const int newSelection = static_cast<int>( param2 );
                                if( newSelection != gSHARGraphicsResolutionSelection )
                                {
                                    gSHARGraphicsResolutionSelection = newSelection;
                                    const int targetHeight =
                                        GetSHARGraphicsResolutionHeight( newSelection );
                                    SaveSHARGraphicsResolutionSelection( newSelection );
                                    if( !RequestSHARAndroidRenderResolutionHeight( targetHeight ) )
                                    {
                                        LOGE("[SHAR-RESOLUTION] Live resize request for %dp could not be queued; saved value will apply on next launch",
                                             targetHeight);
                                    }
                                }
                            }
                        }
                        else if( logicalMenuItem == MENU_ITEM_JUMP_CAMERAS )
                        {
                            if( param2 < SHAR_GRAPHICS_FPS_COUNT )
                            {
                                const int newSelection = static_cast<int>( param2 );
                                if( newSelection != gSHARGraphicsFPSSelection )
                                {
                                    gSHARGraphicsFPSSelection = newSelection;
                                    const int targetFPS = GetSHARAndroidTargetFPS();
                                    SetSHARAndroidTargetFPS( targetFPS );
                                    if( !SaveSHARGraphicsFPSSelection( newSelection ) )
                                    {
                                        LOGE("[SHAR-FPS] Could not save the selected cap; it remains active until app restart");
                                    }
                                    if( !RequestSHARAndroidDisplayFrameRate( targetFPS ) )
                                    {
                                        LOGE("[SHAR-FPS] Display refresh-rate request could not be queued; the software FPS cap remains active");
                                    }
                                }
                            }
                        }
                        else if( logicalMenuItem == MENU_ITEM_INTERSECT_NAV_SYSTEM )
                        {
                            SetCelShadingEnabled( enabled );
                        }
                        else if( logicalMenuItem == MENU_ITEM_RADAR )
                        {
                            SetSHARBloomEnabled( enabled );
                        }
                        else if( logicalMenuItem == MENU_ITEM_INVERT_CAM_CONTROL )
                        {
                            SetSHARCloudShadowsEnabled( enabled );
                        }
                    }
                    else
                    {
                        CheatInputSystem* cheats = GetCheatInputSystem();

                        if( m_moreCheatsSettingsMode )
                        {
                            switch( logicalMenuItem )
                            {
                                case MENU_ITEM_CAMERA:
                                    SetSHARNoclipEnabled( enabled );
                                    break;
                                case MENU_ITEM_JUMP_CAMERAS:
                                    cheats->SetCheatEnabled( CHEAT_ID_KICK_TOGGLES_CHARACTER_MODEL, enabled );
                                    break;
                                case MENU_ITEM_INTERSECT_NAV_SYSTEM:
                                    cheats->SetCheatEnabled( CHEAT_ID_UNLOCK_VEHICLES, enabled );
                                    break;
                                case MENU_ITEM_RADAR:
                                    cheats->SetCheatEnabled( CHEAT_ID_NO_TOP_SPEED, enabled );
                                    break;
                                case MENU_ITEM_INVERT_CAM_CONTROL:
                                    cheats->SetCheatEnabled( CHEAT_ID_SHOW_SPEEDOMETER, enabled );
                                    break;
                                default:
                                    break;
                            }
                        }
                        else
                        {
                            switch( logicalMenuItem )
                            {
                                case MENU_ITEM_CAMERA:
                                    cheats->SetMoonJumpEnabled( enabled );
                                    break;
                                case MENU_ITEM_JUMP_CAMERAS:
                                    cheats->SetCheatEnabled( CHEAT_ID_HIGH_ACCELERATION, enabled );
                                    cheats->SetCheatEnabled( CHEAT_ID_NO_TOP_SPEED, enabled );
                                    break;
                                case MENU_ITEM_INTERSECT_NAV_SYSTEM:
                                    cheats->SetCheatEnabled( CHEAT_ID_INVINCIBLE_CAR, enabled );
                                    break;
                                case MENU_ITEM_RADAR:
                                    cheats->SetCheatEnabled( CHEAT_ID_CAR_JUMP_ON_HORN, enabled );
                                    break;
                                case MENU_ITEM_INVERT_CAM_CONTROL:
                                    cheats->SetCheatEnabled( CHEAT_ID_ONE_TAP_TRAFFIC_DEATH, enabled );
                                    break;
                                default:
                                    break;
                            }
                        }

                        bool actualEnabled = false;
                        if( m_moreCheatsSettingsMode )
                        {
                            switch( logicalMenuItem )
                            {
                                case MENU_ITEM_CAMERA:
                                    actualEnabled = IsSHARNoclipEnabled();
                                    break;
                                case MENU_ITEM_JUMP_CAMERAS:
                                    actualEnabled = cheats->IsCheatEnabled( CHEAT_ID_KICK_TOGGLES_CHARACTER_MODEL );
                                    break;
                                case MENU_ITEM_INTERSECT_NAV_SYSTEM:
                                    actualEnabled = cheats->IsCheatEnabled( CHEAT_ID_UNLOCK_VEHICLES );
                                    break;
                                case MENU_ITEM_RADAR:
                                    actualEnabled = cheats->IsCheatEnabled( CHEAT_ID_NO_TOP_SPEED );
                                    break;
                                case MENU_ITEM_INVERT_CAM_CONTROL:
                                    actualEnabled = cheats->IsCheatEnabled( CHEAT_ID_SHOW_SPEEDOMETER );
                                    break;
                                default:
                                    break;
                            }
                        }
                        else
                        {
                            switch( logicalMenuItem )
                            {
                                case MENU_ITEM_CAMERA:
                                    actualEnabled = cheats->IsMoonJumpEnabled();
                                    break;
                                case MENU_ITEM_JUMP_CAMERAS:
                                    actualEnabled = cheats->IsCheatEnabled( CHEAT_ID_HIGH_ACCELERATION );
                                    break;
                                case MENU_ITEM_INTERSECT_NAV_SYSTEM:
                                    actualEnabled = cheats->IsCheatEnabled( CHEAT_ID_INVINCIBLE_CAR );
                                    break;
                                case MENU_ITEM_RADAR:
                                    actualEnabled = cheats->IsCheatEnabled( CHEAT_ID_CAR_JUMP_ON_HORN );
                                    break;
                                case MENU_ITEM_INVERT_CAM_CONTROL:
                                    actualEnabled = cheats->IsCheatEnabled( CHEAT_ID_ONE_TAP_TRAFFIC_DEATH );
                                    break;
                                default:
                                    break;
                            }
                        }

                        // The game still enforces its existing story-completion rule
                        // for Unlock All Cars, so show the actual runtime state.
                        m_configuringAndroidSubmenu = true;
                        m_pMenu->SetSelectionValue(
                            static_cast<int>( param1 ), actualEnabled ? 1 : 0 );
                        m_configuringAndroidSubmenu = false;
                    }
                    break;
                }
            #else
                const int logicalMenuItem = static_cast<int>( param1 );
            #endif

                if( logicalMenuItem == MENU_ITEM_CAMERA )
                {
                    // set new super cam
                    //
                    SuperCam::Type superCamType =
                        m_cameraSelections[ m_currentCameraSelectionMode ][ param2 ];

                    GetSuperCamManager()->GetSCC( 0 )->SelectSuperCam
                    (
                        superCamType,
                        SuperCamCentral::CUT,
                        0
                    );

                    // and apply it right away
                    //
                    GetSuperCamManager()->GetSCC( 0 )->Update( 0 );
                }
#if !defined(RAD_PC) && !defined(RAD_ANDROID)
                else if( param1 == MENU_ITEM_VIBRATION )
                {
                    if( param2 == 1 ) // vibration turned ON
                    {
                        // send vibration pulse to controller
                        //
                        int controllerID = GetInputManager()->GetControllerIDforPlayer( 0 );

#ifdef RAD_PS2
                        if ( GetInputManager()->IsControllerInPort( Input::USB0 ) )
                        {
                            GetInputManager()->TriggerRumblePulse( Input::USB0 );
                        }
                        else if ( GetInputManager()->IsControllerInPort( Input::USB1 ) )
                        {
                            GetInputManager()->TriggerRumblePulse( Input::USB1 );
                        }
                        else
#endif
                        {
                            GetInputManager()->TriggerRumblePulse( controllerID );
                        }
                    }
                }
#endif

                break;
            }

            default:
            {
                break;
            }
        }

        // relay message to menu
        if( m_pMenu != NULL )
        {
            m_pMenu->HandleMessage( message, param1, param2 );
        }
    }

	// Propogate the message up the hierarchy.
	//
	CGuiScreen::HandleMessage( message, param1, param2 );
}

void
CGuiScreenPauseSettings::OnCheatEntered( eCheatID cheatID, bool isEnabled )
{
    if( cheatID == CHEAT_ID_UNLOCK_CAMERAS && isEnabled )
    {
        m_numCameraSelections[ CAMERA_SELECTION_FOR_DRIVING ] = NUM_CAMERAS_FOR_DRIVING;
        m_numCameraSelections[ CAMERA_SELECTION_FOR_WALKING ] = NUM_CAMERAS_FOR_WALKING;
    }
}

//===========================================================================
// CGuiScreenPauseSettings::InitIntro
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenPauseSettings::InitIntro()
{
#if defined(RAD_ANDROID)
    const bool openGraphicsMenu = gAndroidGraphicsSettingsRequested;
    const bool openCheatsMenu = gAndroidCheatsSettingsRequested;
    // Keep custom mode disabled while normal Settings values are synced.
    // CGuiMenu::SetSelectionValue notifies this screen, so treating these
    // normal values as submenu toggles would change unrelated settings.
    m_graphicsSettingsMode = false;
    m_moreGraphicsSettingsMode = false;
    m_cheatsSettingsMode = false;
    m_moreCheatsSettingsMode = false;
    gAndroidGraphicsSettingsRequested = false;
    gAndroidCheatsSettingsRequested = false;

    // Always restore and initialize the normal settings state first. The
    // Graphics entry can be opened before Settings has ever been visited in
    // the current level; skipping this initialization leaves the reused menu
    // in an incomplete state and can crash when Graphics is opened directly.
    RestoreAndroidSettingsMenu();
#endif

    bool allowCameraToggle = GetSuperCamManager()->GetSCC( 0 )->AllowCameraToggle();

    rAssert( m_pMenu );

    #if defined(RAD_ANDROID)
    const int cameraMenuIndex =
        GetGuiMenuIndexFromMenuItem( MENU_ITEM_CAMERA );

    rAssert( cameraMenuIndex != -1 );

    m_pMenu->SetMenuItemEnabled( cameraMenuIndex, allowCameraToggle );
#else
    m_pMenu->SetMenuItemEnabled( MENU_ITEM_CAMERA, allowCameraToggle );
#endif

    if( allowCameraToggle )
    {
        // update camera selections
        //
        this->UpdateCameraSelections();

    }
    else
    {
        m_currentCameraSelectionMode = CAMERA_SELECTION_NOT_AVAILABLE;
    }

    // update other gameplay settings
    //
    bool isSettingOn = false;

#if !defined(RAD_PC)

#if defined(RAD_ANDROID)
    {
        const int invertCamMenuIndex =
            GetGuiMenuIndexFromMenuItem( MENU_ITEM_INVERT_CAM_CONTROL );

        if( invertCamMenuIndex != -1 )
        {
            const bool realInvertCamera =
            GetAndroidConfigurationManager()->IsInvertCameraEnabled();
                
            m_pMenu->SetSelectionValue
            (
                invertCamMenuIndex,
                realInvertCamera  ? 1 : 0
            );
        }
    }
#else
    isSettingOn =
        GetSuperCamManager()->GetSCC( 0 )->IsInvertedCameraEnabled();

    m_pMenu->SetSelectionValue
    (
        MENU_ITEM_INVERT_CAM_CONTROL,
        isSettingOn ? 1 : 0
    );
#endif

#endif



    isSettingOn = GetSuperCamManager()->GetSCC( 0 )->JumpCamsEnabled();



    #if defined(RAD_ANDROID)
    {
        const int jumpCamerasMenuIndex =
            GetGuiMenuIndexFromMenuItem( MENU_ITEM_JUMP_CAMERAS );

        rAssert( jumpCamerasMenuIndex != -1 );

        m_pMenu->SetSelectionValue
        (
            jumpCamerasMenuIndex,
            isSettingOn ? 1 : 0
        );
    }
#else
    m_pMenu->SetSelectionValue
    (
        MENU_ITEM_JUMP_CAMERAS,
        isSettingOn ? 1 : 0
    );
#endif



    isSettingOn = GetCharacterSheetManager()->QueryNavSystemSetting();



    #if defined(RAD_ANDROID)
    {
        const int intersectNavMenuIndex =
            GetGuiMenuIndexFromMenuItem( MENU_ITEM_INTERSECT_NAV_SYSTEM );

        rAssert( intersectNavMenuIndex != -1 );

        m_pMenu->SetSelectionValue
        (
            intersectNavMenuIndex,
            isSettingOn ? 1 : 0
        );
    }
#else
    m_pMenu->SetSelectionValue
    (
        MENU_ITEM_INTERSECT_NAV_SYSTEM,
        isSettingOn ? 1 : 0
    );
#endif



    isSettingOn = GetGuiSystem()->IsRadarEnabled();



    #if defined(RAD_ANDROID)
    {
        const int radarMenuIndex =
            GetGuiMenuIndexFromMenuItem( MENU_ITEM_RADAR );

        rAssert( radarMenuIndex != -1 );

        m_pMenu->SetSelectionValue
        (
            radarMenuIndex,
            isSettingOn ? 1 : 0
        );
    }
#else
    m_pMenu->SetSelectionValue
    (
        MENU_ITEM_RADAR,
        isSettingOn ? 1 : 0
    );
#endif

#if !defined(RAD_PC)

#if defined(RAD_ANDROID)
    {
        const int vibrationMenuIndex =
            GetGuiMenuIndexFromMenuItem( MENU_ITEM_VIBRATION );

        if( vibrationMenuIndex != -1 )
        {
            isSettingOn =
                GetAndroidConfigurationManager()->IsGamepadVibrationEnabled();

            m_pMenu->SetSelectionValue
            (
                vibrationMenuIndex,
                isSettingOn ? 1 : 0
            );
        }
    }
#else
    isSettingOn = GetInputManager()->IsRumbleEnabled();

    m_pMenu->SetSelectionValue
    (
        MENU_ITEM_VIBRATION,
        isSettingOn ? 1 : 0
    );
#endif

#endif

#if !defined(RAD_ANDROID)
    isSettingOn = GetTutorialManager()->IsTutorialEventsEnabled();
    m_pMenu->SetSelectionValue( MENU_ITEM_TUTORIAL,
                                isSettingOn ? 1 : 0 );

    // show/hide tutorial setting depending on whether or not tutorial mode is enabled
    //
    if( GetTutorialManager()->IsTutorialModeEnabled() )
    {
        CGuiManagerInGame* guiManagerIngame = static_cast<CGuiManagerInGame*>( m_pParent );
        rAssert( guiManagerIngame != NULL );

        bool isTutorialToggleAllowed = (guiManagerIngame->GetResumeGameScreenID() != GUI_SCREEN_ID_TUTORIAL);
        m_pMenu->SetMenuItemEnabled( MENU_ITEM_TUTORIAL, isTutorialToggleAllowed );
    }
    else
    {
        m_pMenu->SetMenuItemEnabled( MENU_ITEM_TUTORIAL, false, true );
    }
#endif

#if defined(RAD_ANDROID)
    // Apply custom labels/values only after ordinary Settings initialization.
    if( openGraphicsMenu )
    {
        m_graphicsSettingsMode = true;
        ConfigureAndroidGraphicsMenu();
    }
    else if( openCheatsMenu )
    {
        m_cheatsSettingsMode = true;
        ConfigureAndroidCheatsMenu();
    }
#endif
}
//===========================================================================
// CGuiScreenPauseSettings::InitRunning
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenPauseSettings::InitRunning()
{
}


//===========================================================================
// CGuiScreenPauseSettings::InitOutro
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenPauseSettings::InitOutro()
{
#if defined(RAD_ANDROID)
    if( m_graphicsSettingsMode )
    {
        // Do not restore the Settings rows while this screen is fading out.
        // InitIntro restores them the next time normal Settings is entered.
        gAndroidGraphicsSettingsRequested = false;
        return;
    }
    if( m_cheatsSettingsMode )
    {
        gAndroidCheatsSettingsRequested = false;
        return;
    }
#endif

    // apply new gameplay settings
    //
    rAssert( m_pMenu != NULL );
    bool isSettingOn = false;

    #if !defined(RAD_PC)

#if defined(RAD_ANDROID)
    {
        const int invertCamMenuIndex =
            GetGuiMenuIndexFromMenuItem( MENU_ITEM_INVERT_CAM_CONTROL );

        if( invertCamMenuIndex != -1 )
        {
            const bool menuInvertCamera =( m_pMenu->GetSelectionValue( invertCamMenuIndex ) == 1 );

            const bool runtimeInvertCamera = !menuInvertCamera;

            GetSuperCamManager()->GetSCC( 0 )->EnableInvertedCamera( runtimeInvertCamera );

            GetAndroidConfigurationManager()->SetInvertCameraEnabled( menuInvertCamera );
         }
    }
#else
    isSettingOn =
        ( m_pMenu->GetSelectionValue( MENU_ITEM_INVERT_CAM_CONTROL ) == 1 );

    GetSuperCamManager()->GetSCC( 0 )->EnableInvertedCamera( isSettingOn );
#endif

#endif



    #if defined(RAD_ANDROID)
    {
        const int jumpCamerasMenuIndex =
            GetGuiMenuIndexFromMenuItem( MENU_ITEM_JUMP_CAMERAS );

        rAssert( jumpCamerasMenuIndex != -1 );

        isSettingOn =
            ( m_pMenu->GetSelectionValue( jumpCamerasMenuIndex ) == 1 );

        GetSuperCamManager()->GetSCC( 0 )->EnableJumpCams( isSettingOn );
    }
#else
    isSettingOn =
        ( m_pMenu->GetSelectionValue( MENU_ITEM_JUMP_CAMERAS ) == 1 );

    GetSuperCamManager()->GetSCC( 0 )->EnableJumpCams( isSettingOn );
#endif



    #if defined(RAD_ANDROID)
    {
        const int intersectNavMenuIndex =
            GetGuiMenuIndexFromMenuItem( MENU_ITEM_INTERSECT_NAV_SYSTEM );

        rAssert( intersectNavMenuIndex != -1 );

        isSettingOn =
            ( m_pMenu->GetSelectionValue( intersectNavMenuIndex ) == 1 );

        GetCharacterSheetManager()->SetNavSystemOn( isSettingOn );
    }
#else
    isSettingOn =
        ( m_pMenu->GetSelectionValue( MENU_ITEM_INTERSECT_NAV_SYSTEM ) == 1 );

    GetCharacterSheetManager()->SetNavSystemOn( isSettingOn );
#endif



    #if defined(RAD_ANDROID)
    {
        const int radarMenuIndex =
            GetGuiMenuIndexFromMenuItem( MENU_ITEM_RADAR );

        rAssert( radarMenuIndex != -1 );

        isSettingOn =
            ( m_pMenu->GetSelectionValue( radarMenuIndex ) == 1 );

        GetGuiSystem()->SetRadarEnabled( isSettingOn );
    }
#else
    isSettingOn =
        ( m_pMenu->GetSelectionValue( MENU_ITEM_RADAR ) == 1 );

    GetGuiSystem()->SetRadarEnabled( isSettingOn );
#endif

#if !defined(RAD_PC)

#if defined(RAD_ANDROID)
    {
        const int vibrationMenuIndex =
            GetGuiMenuIndexFromMenuItem( MENU_ITEM_VIBRATION );

        if( vibrationMenuIndex != -1 )
        {
            isSettingOn =
                ( m_pMenu->GetSelectionValue( vibrationMenuIndex ) == 1 );

            GetAndroidConfigurationManager()->SetGamepadVibrationEnabled( isSettingOn );
            // FIX BUG:
            //muy importante esta linea de abajo para que cuando el juego arranque
            // con Simpsons_configuration.txt en false para vibracion poder activarla in-game 
            // usando el menuSettings extended de la versión de consolas, contenido en el ingame.p3d
             GetInputManager()->SetRumbleEnabled( isSettingOn );
        }
    }
#else
    isSettingOn =
        ( m_pMenu->GetSelectionValue( MENU_ITEM_VIBRATION ) == 1 );

    GetInputManager()->SetRumbleEnabled( isSettingOn );
#endif

#endif

#if !defined(RAD_ANDROID)
    isSettingOn = (m_pMenu->GetSelectionValue( MENU_ITEM_TUTORIAL ) == 1) 
#ifdef RAD_PC
                  && !(GetInputManager()->GetController(0)->IsTutorialDisabled())
#endif                  
                  ;
    GetTutorialManager()->EnableTutorialEvents( isSettingOn );
#endif

#if defined(RAD_ANDROID)
    GetAndroidConfigurationManager()->SaveIfDirty();
#endif

}

//---------------------------------------------------------------------
// Private Functions
//---------------------------------------------------------------------

void
CGuiScreenPauseSettings::UpdateCameraSelections()
{
    rAssert( m_pMenu );

    // set current camera selection mode
    //
    bool isPlayerInCar = GetAvatarManager()->GetAvatarForPlayer( 0 )->IsInCar();



    m_currentCameraSelectionMode = isPlayerInCar ? CAMERA_SELECTION_FOR_DRIVING : CAMERA_SELECTION_FOR_WALKING;



    // get current active camera
    //
    SuperCam* currentSuperCam = GetSuperCamManager()->GetSCC( 0 )->GetActiveSuperCam();



    rAssert( currentSuperCam );

    SuperCam::Type currentSuperCamType = currentSuperCam->GetType();



    int currentCameraIndex = -1;

    // check to see if current camera is in the selection list
    //
    for( int i = 0; i < m_numCameraSelections[ m_currentCameraSelectionMode ]; i++ )
    {


        if( m_cameraSelections[ m_currentCameraSelectionMode ][ i ] == currentSuperCamType )
        {
            currentCameraIndex = i;

            break;
        }
    }

    if( currentCameraIndex == -1 )
    {


        // current camera not found in selection list, disable camera selection
        //
        m_pMenu->SetMenuItemEnabled( MENU_ITEM_CAMERA, false );

        m_currentCameraSelectionMode = CAMERA_SELECTION_NOT_AVAILABLE;

        return;
    }



    m_pMenu->SetMenuItemEnabled( MENU_ITEM_CAMERA, true );



    // set number of camera selections available
    //
    m_pMenu->SetSelectionValueCount( MENU_ITEM_CAMERA,
                                     m_numCameraSelections[ m_currentCameraSelectionMode ] );

    // update camera names
    //
    for( int i = 0; i < m_numCameraSelections[ m_currentCameraSelectionMode ]; i++ )
    {
        GuiMenuItem* menuItemCamera = m_pMenu->GetMenuItem( MENU_ITEM_CAMERA );


        rAssert( menuItemCamera != NULL );

        SuperCam* superCam = GetSuperCamManager()->GetSCC( 0 )->GetSuperCam( m_cameraSelections[ m_currentCameraSelectionMode ][ i ] );

        rAssert( superCam );

        HeapMgr()->PushHeap( GMA_LEVEL_HUD );

        P3D_UNICODE* stringBuffer = GetTextBibleString( superCam->GetName() );

        rAssert( stringBuffer );

        UnicodeString unicodeString;
        unicodeString.ReadUnicode( static_cast<UnicodeChar*>( stringBuffer ) );

        Scrooby::Text* menuItemValue = dynamic_cast<Scrooby::Text*>( menuItemCamera->GetItemValue() );



        rAssert( menuItemValue != NULL );
        menuItemValue->SetString( i, unicodeString );

        HeapMgr()->PopHeap(GMA_LEVEL_HUD);
    }

    // set current camera selection
    //
    m_pMenu->SetSelectionValue( MENU_ITEM_CAMERA, currentCameraIndex );


}

#if defined(RAD_ANDROID)

bool CGuiScreenPauseSettings::AddPauseSettingsMenuItemIfAvailable
(
    Scrooby::Page* pPage,
    const char* item,
    int logicalMenuItem
)
{
    char itemName[ 32 ];

    // The Tutorial row is only a placeholder for the Android Cheats submenu.
    // Always generate it alongside the other optional Android rows so its
    // position follows the Settings menu layout rather than platform-specific
    // Tutorial artwork.
    if( logicalMenuItem == MENU_ITEM_TUTORIAL )
    {
        return AddGeneratedAndroidToggleMenuItem
        (
            pPage->GetGroup( "Menu" ),
            pPage,
            logicalMenuItem
        );
    }

    Scrooby::Group* group = pPage->GetGroup( item );
    if( group == NULL )
    {
        Scrooby::Group* menuGroup = pPage->GetGroup( "Menu" );
        if( menuGroup != NULL )
        {
            group = menuGroup->GetGroup( item );
        }
    }

    if( group == NULL )
    {
        if( IsAndroidOptionalMenuItem( logicalMenuItem ) )
        {
            return AddGeneratedAndroidToggleMenuItem
            (
                pPage->GetGroup( "Menu" ),
                pPage,
                logicalMenuItem
            );
        }

        rAssert( group != NULL );
        return false;
    }

    Scrooby::Text* pText = group->GetText( item );

    if( pText == NULL )
    {
        if( IsAndroidOptionalMenuItem( logicalMenuItem ) )
        {
            return AddGeneratedAndroidToggleMenuItem
            (
                pPage->GetGroup( "Menu" ),
                pPage,
                logicalMenuItem
            );
        }

        rAssert( pText != NULL );
        return false;
    }

    pText->SetTextMode( Scrooby::TEXT_WRAP );

    sprintf( itemName, "%s_Value", item );
    Scrooby::Text* pTextValue = group->GetText( itemName );

    if( pTextValue == NULL )
    {
        if( IsAndroidOptionalMenuItem( logicalMenuItem ) )
        {
            return AddGeneratedAndroidToggleMenuItem
            (
                pPage->GetGroup( "Menu" ),
                pPage,
                logicalMenuItem
            );
        }

        rAssert( pTextValue != NULL );
        return false;
    }

    pTextValue->SetTextMode( Scrooby::TEXT_WRAP );

    sprintf( itemName, "%s_LArrow", item );
    Scrooby::Sprite* pLArrow = group->GetSprite( itemName );

    sprintf( itemName, "%s_RArrow", item );
    Scrooby::Sprite* pRArrow = group->GetSprite( itemName );

    if( IsAndroidOptionalMenuItem( logicalMenuItem ) &&
        ( pLArrow == NULL || pRArrow == NULL ) )
    {
        return AddGeneratedAndroidToggleMenuItem
        (
            pPage->GetGroup( "Menu" ),
            pPage,
            logicalMenuItem
        );
    }

    mMenuItemToGuiMenuIndex[ logicalMenuItem ] = mNumActiveMenuItems;
    mGuiMenuIndexToMenuItem[ mNumActiveMenuItems ] = logicalMenuItem;

    m_pMenu->AddMenuItem
    (
        pText,
        pTextValue,
        NULL,
        NULL,
        pLArrow,
        pRArrow,
        SELECTION_ENABLED | VALUES_WRAPPED | TEXT_OUTLINE_ENABLED
    );

    mNumActiveMenuItems++;

    return true;
}

bool CGuiScreenPauseSettings::AddGeneratedAndroidToggleMenuItem
(
    Scrooby::Group* pMenuGroup,
    Scrooby::Page* pPage,
    int logicalMenuItem
)
{
    FeGroup* menuGroup = dynamic_cast<FeGroup*>( pMenuGroup );

    if( menuGroup == NULL || pPage == NULL )
    {
        return false;
    }

    Scrooby::Group* radarGroup = pPage->GetGroup( "Radar" );

    if( radarGroup == NULL )
    {
        return false;
    }

    FeText* templateLabel =
        dynamic_cast<FeText*>( radarGroup->GetText( "Radar" ) );
    FeText* templateValue =
        dynamic_cast<FeText*>( radarGroup->GetText( "Radar_Value" ) );
    FeSprite* templateLeftArrow =
        dynamic_cast<FeSprite*>( radarGroup->GetSprite( "Radar_LArrow" ) );
    FeSprite* templateRightArrow =
        dynamic_cast<FeSprite*>( radarGroup->GetSprite( "Radar_RArrow" ) );

    if( templateLabel == NULL || templateValue == NULL )
    {
        return false;
    }

    int labelX = 0;
    int valueX = 0;
    int leftArrowX = 0;
    int rightArrowX = 0;
    int radarY = 0;
    int rowSpacing = 0;

    if( !GetAndroidSettingsRowGeometry
        (
            pPage,
            labelX,
            valueX,
            leftArrowX,
            rightArrowX,
            radarY,
            rowSpacing
        ) )
    {
        return false;
    }

    const int rowNumber =
        logicalMenuItem == MENU_ITEM_INVERT_CAM_CONTROL ? 1 :
        logicalMenuItem == MENU_ITEM_VIBRATION ? 2 : 3;
    const int rowY = radarY + rowSpacing * rowNumber;

    const char* labelText =
        logicalMenuItem == MENU_ITEM_INVERT_CAM_CONTROL ? "Invert Camera" :
        logicalMenuItem == MENU_ITEM_VIBRATION ? "Controller Vibration" :
        "Tutorial";
    const char* itemNamePart =
        logicalMenuItem == MENU_ITEM_INVERT_CAM_CONTROL ? "InvertCamera" :
        logicalMenuItem == MENU_ITEM_VIBRATION ? "ControllerVibration" :
        "Tutorial";

    char nameBuffer[ 64 ];

    snprintf
    (
        nameBuffer,
        sizeof( nameBuffer ),
        "AndroidSettings_%s_Label",
        itemNamePart
    );

    FeText* label = menuGroup->AddText( nameBuffer, 0, 0 );

    snprintf
    (
        nameBuffer,
        sizeof( nameBuffer ),
        "AndroidSettings_%s_Value",
        itemNamePart
    );

    FeText* value = menuGroup->AddText( nameBuffer, 0, 0 );

    if( label == NULL || value == NULL )
    {
        return false;
    }

    CopyAndroidMenuTextAppearance( label, templateLabel );
    CopyAndroidMenuTextAppearance( value, templateValue );

    label->AddHardCodedString( labelText );
    value->AddHardCodedString( "OFF" );
    value->AddHardCodedString( "ON" );
    value->SetIndex( 0 );

    label->SetPositionOfCenter( labelX, rowY );
    value->SetPositionOfCenter( valueX, rowY );

    FeSprite* leftArrow = NULL;
    FeSprite* rightArrow = NULL;

    if( templateLeftArrow != NULL && templateRightArrow != NULL )
    {
        char leftName[ 64 ];
        char rightName[ 64 ];

        snprintf
        (
            leftName,
            sizeof( leftName ),
            "AndroidSettings_%s_LArrow",
            logicalMenuItem == MENU_ITEM_INVERT_CAM_CONTROL ?
            "InvertCamera" :
            "ControllerVibration"
        );

        snprintf
        (
            rightName,
            sizeof( rightName ),
            "AndroidSettings_%s_RArrow",
            logicalMenuItem == MENU_ITEM_INVERT_CAM_CONTROL ?
            "InvertCamera" :
            "ControllerVibration"
        );

        leftArrow = menuGroup->AddSprite( leftName, 0, 0 );
        rightArrow = menuGroup->AddSprite( rightName, 0, 0 );

        if( leftArrow != NULL && rightArrow != NULL )
        {
            CopyAndroidMenuSpriteAppearance( leftArrow, templateLeftArrow );
            CopyAndroidMenuSpriteAppearance( rightArrow, templateRightArrow );

            leftArrow->SetPositionOfCenter( leftArrowX, rowY );
            rightArrow->SetPositionOfCenter( rightArrowX, rowY );

            leftArrow->SetVisible( false );
            rightArrow->SetVisible( false );
        }
    }

    const int guiMenuIndex = mNumActiveMenuItems;
    mMenuItemToGuiMenuIndex[ logicalMenuItem ] = guiMenuIndex;
    mGuiMenuIndexToMenuItem[ guiMenuIndex ] = logicalMenuItem;

    m_pMenu->AddMenuItem
    (
        label,
        value,
        NULL,
        NULL,
        leftArrow,
        rightArrow,
        SELECTION_ENABLED | VALUES_WRAPPED | TEXT_OUTLINE_ENABLED
    );

    mNumActiveMenuItems++;

    return true;
}

bool CGuiScreenPauseSettings::IsAndroidOptionalMenuItem( int logicalMenuItem ) const
{
    return logicalMenuItem == MENU_ITEM_INVERT_CAM_CONTROL ||
           logicalMenuItem == MENU_ITEM_VIBRATION ||
           logicalMenuItem == MENU_ITEM_TUTORIAL;
}

bool CGuiScreenPauseSettings::HasMenuItem( int logicalMenuItem ) const
{
    return GetGuiMenuIndexFromMenuItem( logicalMenuItem ) != -1;
}

int CGuiScreenPauseSettings::GetGuiMenuIndexFromMenuItem( int logicalMenuItem ) const
{
    if( logicalMenuItem < 0 || logicalMenuItem >= NUM_PAUSE_SETTINGS_MENU_ITEMS )
    {
        return -1;
    }

    return mMenuItemToGuiMenuIndex[ logicalMenuItem ];
}

int CGuiScreenPauseSettings::GetMenuItemFromGuiMenuIndex( int guiMenuIndex ) const
{
    if( guiMenuIndex < 0 || guiMenuIndex >= mNumActiveMenuItems )
    {
        return -1;
    }

    return mGuiMenuIndexToMenuItem[ guiMenuIndex ];
}

#endif