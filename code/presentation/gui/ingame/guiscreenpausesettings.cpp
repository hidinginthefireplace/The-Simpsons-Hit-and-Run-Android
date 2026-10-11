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
int GetFrameSharpeningLevel();
void SetFrameSharpeningLevel(int level);

static bool gAndroidGraphicsSettingsRequested = false;

// Restart-apply selector stage: 480p through 1440p are enabled.
// 2160p remains deferred until GL allocation safeguards are added.
static const int SHAR_GRAPHICS_RESOLUTION_COUNT = 4;
static int gSHARGraphicsResolutionSelection = 3; // default: 1440p
static bool gSHARGraphicsResolutionNeedsNormalization = false

void RequestAndroidGraphicsSettings()
{
    gAndroidGraphicsSettingsRequested = true;
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

// Resolution choices shown by the Android Graphics submenu. The existing Java
// startup code consumes target_height from /files/Graphics/Simpsons_resolution.txt.
// No Surface/GL resize is requested here; the new value is applied at next launch.
static const int SHAR_GRAPHICS_RESOLUTION_HEIGHTS[4] =
{
    480, 720, 1080, 1440
};

static int GetSHARGraphicsResolutionHeight( int selection )
{
    if( selection < 0 || selection >= SHAR_GRAPHICS_RESOLUTION_COUNT )
    {
        selection = 3;
    }
    return SHAR_GRAPHICS_RESOLUTION_HEIGHTS[ selection ];
}

static int GetSHARGraphicsResolutionSelectionForHeight( int height )
{
    if( height <= 0 )
    {
        return 3;
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
        return 3;
    }

    char path[512];
    const int pathLength = snprintf(
        path, sizeof(path), "%s/Graphics/Simpsons_resolution.txt", basePath );
    if( pathLength < 0 || pathLength >= static_cast<int>( sizeof(path) ) )
    {
        LOGE("[SHAR-RESOLUTION] Config path too long; defaulting to 1440p");
        return 3;
    }

    FILE* file = fopen( path, "r" );
    if( file == NULL )
    {
        LOGI("[SHAR-RESOLUTION] Config not present yet; defaulting to 1440p");
        return 3;
    }

    char line[256];
    int selection = 3;
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
                    // Unsupported custom/MAX/2160p values are normalized to the
                    // nearest supported menu profile before a future app launch.
                    gSHARGraphicsResolutionNeedsNormalization = true;
                }
            }
        }
        break;
    }

    fclose( file );
    LOGI("[SHAR-RESOLUTION] Loaded selector value %dp from %s",
         GetSHARGraphicsResolutionHeight(selection), path);
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
        "# Changes take effect after fully restarting the game.\n"
        "# The render width is calculated automatically from the display aspect ratio.\n"
        "# Supported menu values in this build: 480, 720, 1080, 1440.\n"
        "target_height=%d\n",
        height );
    const int closeResult = fclose( file );

    if( result < 0 || closeResult != 0 )
    {
        LOGE("[SHAR-RESOLUTION] Failed writing selector value to %s", path);
        return false;
    }

    LOGI("[SHAR-RESOLUTION] Saved target_height=%d to %s (applies next launch)",
         height, path);
    return true;
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
#if !defined(RAD_ANDROID)
    "Tutorial",
#endif
#endif

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
                    "480p", "720p", "1080p", "1440p"
                };
                for( int valueIndex = 0; valueIndex < SHAR_GRAPHICS_RESOLUTION_COUNT; ++valueIndex )
                {
                    pGraphicsValue->AddHardCodedString( resolutionValues[valueIndex] );
                }

                pGraphicsValue->SetIndex( 3 );
                pGraphicsValue->SetVisible( false );
                m_pGraphicsResolutionValue = pGraphicsValue;
                LOGI("[SHAR-RESOLUTION-MENU] Created dedicated five-value Resolution text");
            }
            else
            {
                LOGE("[SHAR-RESOLUTION-MENU] Failed to create dedicated Resolution value text");
            }
        }
        else
        {
            LOGE("[SHAR-RESOLUTION-MENU] Could not find Camera row group/value text");
        }
    }

    // Do not expand the original Scrooby Settings value object to six strings.
    // Build a separate value object in the same row group and swap it in only
    // while the Graphics submenu is displayed. This keeps the resource's
    // original OFF/ON strings intact for the regular Settings menu.
    {
        Scrooby::Group* pSharpeningGroup = pPage->GetGroup( "Radar" );
        FeGroup* pFeSharpeningGroup = dynamic_cast<FeGroup*>( pSharpeningGroup );
        FeText* pOriginalValue = pSharpeningGroup != NULL
            ? dynamic_cast<FeText*>( pSharpeningGroup->GetText( "Radar_Value" ) )
            : NULL;

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
                LOGI("[SHAR-GRAPHICS-DIAG] Created dedicated six-value sharpening text");
            }
            else
            {
                LOGE("[SHAR-GRAPHICS-DIAG] Failed to create dedicated sharpening text");
            }
        }
        else
        {
            LOGE("[SHAR-GRAPHICS-DIAG] Could not find sharpening row group/value text");
        }
    }
#endif

MEMTRACK_POP_GROUP( "CGuiScreenPauseSettings" );
}
#if defined(RAD_ANDROID)

void CGuiScreenPauseSettings::ConfigureAndroidGraphicsMenu()
{
    m_configuringAndroidGraphicsMenu = true;
    gSHARGraphicsResolutionSelection = LoadSHARGraphicsResolutionSelection();
    if( gSHARGraphicsResolutionNeedsNormalization )
    {
        // Migrate unsupported MAX/2160p selections to the safe 1440p ceiling
        // so the next full launch cannot unexpectedly use the unguarded 4K path.
        if( SaveSHARGraphicsResolutionSelection( gSHARGraphicsResolutionSelection ) )
        {
            gSHARGraphicsResolutionNeedsNormalization = false;
        }
        else
        {
            LOGE("[SHAR-RESOLUTION] Could not normalize unsupported config; retrying next Graphics visit");
        }
    }
    LOGI("[SHAR-RESOLUTION-MENU] ConfigureAndroidGraphicsMenu begin; selected=%dp",
         GetSHARGraphicsResolutionHeight(gSHARGraphicsResolutionSelection));

    // Reuse five existing Settings rows without editing any Scrooby/game assets.
    // Camera is used as the temporary Resolution row, and Invert Camera is
    // the temporary Back row. These values are UI-only in this diagnostic build.
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
        "Resolution",
        "Cel Shading",
        "CRT Filter",
        "Sharpening",
        "Back"
    };

    const bool enabled[2] =
    {
        IsCelShadingEnabled(),
        IsCRTFilterEnabled()
    };

    const char* sharpeningValues[6] =
    {
        "OFF", "20%", "40%", "60%", "80%", "MAX"
    };
    const int sharpeningLevel = GetFrameSharpeningLevel();

    if( gSHARGraphicsResolutionSelection < 0 ||
        gSHARGraphicsResolutionSelection >= SHAR_GRAPHICS_RESOLUTION_COUNT )
    {
        gSHARGraphicsResolutionSelection = 3;
    }

    for( int i = 0; i < 5; ++i )
    {
        const int guiIndex = GetGuiMenuIndexFromMenuItem( logicalItems[i] );
        if( guiIndex < 0 )
        {
            LOGE("[SHAR-RESOLUTION-MENU] Missing GUI row for option %d", i);
            continue;
        }

        GuiMenuItem* item = m_pMenu->GetMenuItem( guiIndex );
        if( item == NULL )
        {
            LOGE("[SHAR-RESOLUTION-MENU] Null GUI row for option %d", i);
            continue;
        }

        if( i == 0 && m_pGraphicsResolutionValue != NULL )
        {
            item->SetItemValue( m_pGraphicsResolutionValue );
            m_pOriginalResolutionValue->SetVisible( false );
            m_pGraphicsResolutionValue->SetVisible( true );
        }
        else if( i == 3 && m_pGraphicsSharpeningValue != NULL )
        {
            item->SetItemValue( m_pGraphicsSharpeningValue );
            m_pOriginalSharpeningValue->SetVisible( false );
            m_pGraphicsSharpeningValue->SetVisible( true );
        }

        // Back is the only action row. All other entries are left/right values.
        if( i == 4 )
        {
            item->m_attributes |= SELECTABLE;
        }
        else
        {
            item->m_attributes &= ~SELECTABLE;
        }
        item->m_showValueArrows = ( i < 4 );
        item->m_useBackSoundOnSelection = ( i == 4 );

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
                    value->SetString( 0, "1440p" );
                    if( value->GetNumOfStrings() > 1 )
                    {
                        value->SetString( 1, "2160p" );
                    }
                    m_pMenu->SetSelectionValueCount( guiIndex, 2 );
                    m_pMenu->SetSelectionValue( guiIndex,
                        gSHARGraphicsResolutionSelection >= 4 ? 1 : 0 );
                }
            }
            else if( i == 1 || i == 2 )
            {
                value->SetVisible( true );
                value->SetString( 0, "OFF" );
                value->SetString( 1, "ON" );
                m_pMenu->SetSelectionValueCount( guiIndex, 2 );
                m_pMenu->SetSelectionValue( guiIndex, enabled[i - 1] ? 1 : 0 );
            }
            else if( i == 3 )
            {
                value->SetVisible( true );
                if( m_pGraphicsSharpeningValue != NULL &&
                    value == m_pGraphicsSharpeningValue )
                {
                    m_pMenu->SetSelectionValueCount( guiIndex, 6 );
                    m_pMenu->SetSelectionValue( guiIndex, sharpeningLevel );
                }
                else
                {
                    value->SetString( 0, "OFF" );
                    value->SetString( 1, "ON" );
                    m_pMenu->SetSelectionValueCount( guiIndex, 2 );
                    m_pMenu->SetSelectionValue( guiIndex, 0 );
                }
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

    // The unused Android-only gameplay row stays hidden while Graphics is open.
    const int unusedItem = MENU_ITEM_VIBRATION;
    const int unusedGuiIndex = GetGuiMenuIndexFromMenuItem( unusedItem );
    if( unusedGuiIndex >= 0 )
    {
        m_pMenu->SetMenuItemEnabled( unusedGuiIndex, false, true );
    }

    m_pMenu->Reset( 0 );
    m_configuringAndroidGraphicsMenu = false;
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
            LOGI("[SHAR-RESOLUTION-MENU] Restored original Settings Camera value text");
        }

        // Restore the original sharpening value text. Its OFF/ON resource
        // string table was never modified by the Graphics submenu.
        if( logicalItems[i] == MENU_ITEM_RADAR &&
            m_pOriginalSharpeningValue != NULL &&
            item->GetItemValue() == m_pGraphicsSharpeningValue )
        {
            m_pGraphicsSharpeningValue->SetVisible( false );
            m_pOriginalSharpeningValue->SetVisible( true );
            item->SetItemValue( m_pOriginalSharpeningValue );
            LOGI("[SHAR-GRAPHICS-DIAG] Restored original Settings sharpening value text");
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

    m_pMenu->Reset( 0 );
}

void CGuiScreenPauseSettings::LeaveAndroidGraphicsMenu( bool backSoundAlreadyPlayed )
{
    // Keep graphics mode active through the outro so the custom labels/values
    // are not applied as camera and gameplay settings.
    gAndroidGraphicsSettingsRequested = false;
    m_pParent->HandleMessage( GUI_MSG_BACK_SCREEN );

    // The Back row plays this sound through CGuiMenu::MakeSelection. The B
    // button bypasses that path, so play the normal back sound here instead.
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
                if( m_graphicsSettingsMode &&
                    GetMenuItemFromGuiMenuIndex( static_cast<int>( param1 ) ) == MENU_ITEM_INVERT_CAM_CONTROL )
                {
                    LeaveAndroidGraphicsMenu( true );
                    return;
                }
#endif
                break;
            }

            case GUI_MSG_MENU_SELECTION_VALUE_CHANGED:
            {
            #if defined(RAD_ANDROID)
                const int logicalMenuItem =
                    GetMenuItemFromGuiMenuIndex( static_cast<int>( param1 ) );

                if( m_graphicsSettingsMode )
                {
                    // SetSelectionValue is used while the submenu is being
                    // configured. Ignore those synthetic notifications so they
                    // cannot overwrite user settings or the resolution config.
                    if( m_configuringAndroidGraphicsMenu )
                    {
                        break;
                    }

                    const bool enabled = ( param2 == 1 );
                    if( logicalMenuItem == MENU_ITEM_CAMERA )
                    {
                        if( param2 < SHAR_GRAPHICS_RESOLUTION_COUNT )
                        {
                            const int newSelection = static_cast<int>( param2 );
                            if( newSelection != gSHARGraphicsResolutionSelection )
                            {
                                gSHARGraphicsResolutionSelection = newSelection;
                                // Persist to the canonical app-internal config.
                                // SDLSurface reads this at full app startup, so
                                // the active renderer is never resized mid-frame.
                                SaveSHARGraphicsResolutionSelection( newSelection );
                            }
                        }
                    }
                    else if( logicalMenuItem == MENU_ITEM_JUMP_CAMERAS )
                    {
                        SetCelShadingEnabled( enabled );
                    }
                    else if( logicalMenuItem == MENU_ITEM_INTERSECT_NAV_SYSTEM )
                    {
                        SetCRTFilterEnabled( enabled );
                    }
                    else if( logicalMenuItem == MENU_ITEM_RADAR )
                    {
                        // The sharpening row has six values: OFF, 20%, 40%,
                        // 60%, 80%, and MAX.
                        SetFrameSharpeningLevel( static_cast<int>( param2 ) );
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
    // Keep graphics mode disabled while normal Settings values are synced.
    // CGuiMenu::SetSelectionValue notifies this screen, so treating these
    // normal values as graphics toggles would write the wrong settings.
    m_graphicsSettingsMode = false;
    gAndroidGraphicsSettingsRequested = false;

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
    // Apply the graphics labels/values only after the shared Settings screen
    // has completed its normal state synchronization. This preserves direct
    // Graphics entry after a fresh map load without restoring Settings rows
    // during the Graphics outro transition.
    if( openGraphicsMenu )
    {
        LOGI("[SHAR-GRAPHICS-DIAG] Normal Settings init finished; entering Graphics layout");
        m_graphicsSettingsMode = true;
        ConfigureAndroidGraphicsMenu();
        LOGI("[SHAR-GRAPHICS-DIAG] ConfigureAndroidGraphicsMenu finished");
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
        // Do not restore the Settings rows while this screen is fading out:
        // that caused a brief flash of the normal menu behind the transition.
        // InitIntro restores them the next time normal Settings is entered.
        gAndroidGraphicsSettingsRequested = false;
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

    Scrooby::Group* group = pPage->GetGroup( item );

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
        logicalMenuItem == MENU_ITEM_INVERT_CAM_CONTROL ? 1 : 2;
    const int rowY = radarY + rowSpacing * rowNumber;

    const char* labelText =
        logicalMenuItem == MENU_ITEM_INVERT_CAM_CONTROL ?
        "Invert Camera" :
        "Controller Vibration";

    char nameBuffer[ 64 ];

    snprintf
    (
        nameBuffer,
        sizeof( nameBuffer ),
        "AndroidSettings_%s_Label",
        logicalMenuItem == MENU_ITEM_INVERT_CAM_CONTROL ?
        "InvertCamera" :
        "ControllerVibration"
    );

    FeText* label = menuGroup->AddText( nameBuffer, 0, 0 );

    snprintf
    (
        nameBuffer,
        sizeof( nameBuffer ),
        "AndroidSettings_%s_Value",
        logicalMenuItem == MENU_ITEM_INVERT_CAM_CONTROL ?
        "InvertCamera" :
        "ControllerVibration"
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
           logicalMenuItem == MENU_ITEM_VIBRATION;
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