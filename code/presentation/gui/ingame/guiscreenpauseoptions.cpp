//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   CGuiScreenPauseOptions
//
// Description: Implementation of the CGuiScreenPauseOptions class.
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
#include <presentation/gui/ingame/guiscreenpauseoptions.h>
#ifdef RAD_ANDROID
// Graphics and Cheats both reuse the existing Pause Settings screen.
void RequestAndroidGraphicsSettings();
void RequestAndroidCheatsSettings();
#endif
#include <presentation/gui/guimenu.h>

#include <cheats/cheatinputsystem.h>
#include <memory/srrmemory.h>

#include <raddebug.hpp> // Foundation
#include <Group.h>
#include <Screen.h>
#include <Page.h>
#include <Text.h>

#ifdef RAD_ANDROID
#include <input/touch/touchhudsystem.h>
#endif

//===========================================================================
// Global Data, Local Data, Local Classes
//===========================================================================

enum ePauseMenuItem
{
#ifdef RAD_PC
    MENU_ITEM_DISPLAY,
    MENU_ITEM_CONTROLLER,
    MENU_ITEM_SOUND,
    MENU_ITEM_SETTINGS,
#elif defined(RAD_ANDROID)
    MENU_ITEM_SOUND,
    MENU_ITEM_SETTINGS,
    MENU_ITEM_GRAPHICS,
    MENU_ITEM_CONTROLLER,
#else
    MENU_ITEM_CONTROLLER,
    MENU_ITEM_SOUND,
    MENU_ITEM_SETTINGS,
#endif

    NUM_PAUSE_MENU_ITEMS
};


static const char* PAUSE_MENU_ITEMS[] =
{
#ifdef RAD_PC
    "Display",
    "Controller",
    "Sound",
    "Settings",
#elif defined(RAD_ANDROID)
    "Sound",
    "Settings",
    "Graphics",
    "Controller",
#else
    "Controller",
    "Sound",
    "Settings",
#endif

    ""
};

//===========================================================================
// Public Member Functions
//===========================================================================

//===========================================================================
// CGuiScreenPauseOptions::CGuiScreenPauseOptions
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
CGuiScreenPauseOptions::CGuiScreenPauseOptions
(
    Scrooby::Screen* pScreen,
    CGuiEntity* pParent
)
:   CGuiScreen( pScreen, pParent, GUI_SCREEN_ID_OPTIONS ),
    m_pMenu( NULL )
{
MEMTRACK_PUSH_GROUP( "CGUIScreenPauseOptions" );
    // Retrieve the Scrooby drawing elements.
    //
    Scrooby::Page* pPage;
	pPage = m_pScroobyScreen->GetPage( "PauseOptions" );
	rAssert( pPage );

    // Create a menu.
    //
    m_pMenu = new(GMA_LEVEL_HUD) CGuiMenu( this, NUM_PAUSE_MENU_ITEMS );
    rAssert( m_pMenu != NULL );

    // Add menu items
    //
    Scrooby::Group* menu = pPage->GetGroup( "Menu" );
    rAssert( menu != NULL );
    char itemName[ 32 ];
    for( int i = 0; i < NUM_PAUSE_MENU_ITEMS; i++ )
    {
        sprintf( itemName, "%s_Value", PAUSE_MENU_ITEMS[ i ] );
        Scrooby::Text* pTextValue = pPage->GetText( itemName );
        Scrooby::Text* pLabel = menu->GetText( PAUSE_MENU_ITEMS[ i ] );
#ifdef RAD_ANDROID
        if( i == MENU_ITEM_GRAPHICS )
        {
            pLabel = menu->GetText( "Display" );
            pTextValue = pPage->GetText( "Display_Value" );
            if( pLabel )
            {
                pLabel->SetString( 0, "GRAPHICS" );
                pLabel->SetVisible( true );
            }
            if( pTextValue )
            {
                pTextValue->SetString( 0, "OPEN" );
                pTextValue->SetVisible( true );
            }
        }
#endif

        sprintf( itemName, "%s_LArrow", PAUSE_MENU_ITEMS[ i ] );
        Scrooby::Sprite* pLArrow = pPage->GetSprite( itemName );

        sprintf( itemName, "%s_RArrow", PAUSE_MENU_ITEMS[ i ] );
        Scrooby::Sprite* pRArrow = pPage->GetSprite( itemName );

        m_pMenu->AddMenuItem( pLabel,
                              pTextValue,
                              NULL,
                              NULL,
                              pLArrow,
                              pRArrow );
    }

#ifdef RAD_ANDROID
    // Reuse the Android Display artwork for Graphics and Controller for Cheats.
    // Use a fixed vertical translation for this menu-position test.
    Scrooby::Text* pSoundLabel = menu->GetText( "Sound" );
    Scrooby::Text* pSettingsLabel = menu->GetText( "Settings" );
    Scrooby::Text* pGraphicsLabel = menu->GetText( "Display" );
    Scrooby::Text* pCheatsLabel = menu->GetText( "Controller" );
    Scrooby::Text* pGraphicsValue = pPage->GetText( "Display_Value" );
    Scrooby::Text* pCheatsValue = pPage->GetText( "Controller_Value" );
    Scrooby::Sprite* pGraphicsLArrow = pPage->GetSprite( "Display_LArrow" );
    Scrooby::Sprite* pGraphicsRArrow = pPage->GetSprite( "Display_RArrow" );
    Scrooby::Sprite* pCheatsLArrow = pPage->GetSprite( "Controller_LArrow" );
    Scrooby::Sprite* pCheatsRArrow = pPage->GetSprite( "Controller_RArrow" );

    int soundX = 0, soundY = 0;
    int settingsX = 0, settingsY = 0;
    int graphicsX = 0, graphicsY = 0;
    int displayX = 0, displayY = 0;
    int cheatsX = 0, cheatsY = 0;
    int controllerX = 0, controllerY = 0;
    int valueX = 0, valueY = 0;
    int lArrowX = 0, lArrowY = 0;
    int rArrowX = 0, rArrowY = 0;

    if( pSoundLabel && pSettingsLabel && pGraphicsLabel )
    {
        pSoundLabel->GetBoundingBoxCenter( soundX, soundY );
        pSettingsLabel->GetBoundingBoxCenter( settingsX, settingsY );
        pGraphicsLabel->GetBoundingBoxCenter( displayX, displayY );

        // Keep existing vertical spacing and use one shared horizontal center.
        int centeredX = ( soundX + settingsX ) / 2;
        const int rowSpacing = settingsY - soundY;
                pSoundLabel->SetPositionOfCenter( centeredX, soundY );
        pSettingsLabel->SetPositionOfCenter( centeredX, settingsY );

        graphicsX = centeredX;
        graphicsY = settingsY + rowSpacing;
        pGraphicsLabel->SetPositionOfCenter( graphicsX, graphicsY );

        if( pCheatsLabel )
        {
            pCheatsLabel->GetBoundingBoxCenter( controllerX, controllerY );
            cheatsX = centeredX;
            cheatsY = graphicsY + rowSpacing;
            pCheatsLabel->SetPositionOfCenter( cheatsX, cheatsY );
            pCheatsLabel->SetString( 0, "CHEATS" );

            if( pCheatsValue )
            {
                pCheatsValue->GetBoundingBoxCenter( valueX, valueY );
                pCheatsValue->SetPositionOfCenter(
                    cheatsX + ( valueX - controllerX ),
                    cheatsY + ( valueY - controllerY ) );
                pCheatsValue->SetString( 0, "OPEN" );
            }
            if( pCheatsLArrow )
            {
                pCheatsLArrow->GetBoundingBoxCenter( lArrowX, lArrowY );
                pCheatsLArrow->SetPositionOfCenter(
                    cheatsX + ( lArrowX - controllerX ),
                    cheatsY + ( lArrowY - controllerY ) );
            }
            if( pCheatsRArrow )
            {
                pCheatsRArrow->GetBoundingBoxCenter( rArrowX, rArrowY );
                pCheatsRArrow->SetPositionOfCenter(
                    cheatsX + ( rArrowX - controllerX ),
                    cheatsY + ( rArrowY - controllerY ) );
            }
        }

        // Preserve the reused Display value/arrows' offsets from its label.
        if( pGraphicsValue )
        {
            pGraphicsValue->GetBoundingBoxCenter( valueX, valueY );
            pGraphicsValue->SetPositionOfCenter(
                graphicsX + ( valueX - displayX ),
                graphicsY + ( valueY - displayY ) );
        }

        if( pGraphicsLArrow )
        {
            pGraphicsLArrow->GetBoundingBoxCenter( lArrowX, lArrowY );
            pGraphicsLArrow->SetPositionOfCenter(
                graphicsX + ( lArrowX - displayX ),
                graphicsY + ( lArrowY - displayY ) );
        }

        if( pGraphicsRArrow )
        {
            pGraphicsRArrow->GetBoundingBoxCenter( rArrowX, rArrowY );
            pGraphicsRArrow->SetPositionOfCenter(
                graphicsX + ( rArrowX - displayX ),
                graphicsY + ( rArrowY - displayY ) );
        }
    }

    if( pGraphicsValue )
    {
        pGraphicsValue->SetString( 0, "OPEN" );
    }

    // Give the Graphics row the normal left/right value arrows.
    if( pGraphicsLArrow && pGraphicsRArrow )
    {
        pGraphicsLArrow->SetVisible( false );
        pGraphicsRArrow->SetVisible( false );
    }
#endif

#ifndef RAD_PC
    Scrooby::Text* pText = menu->GetText( "Display" );
    if( pText )
    {
#ifndef RAD_ANDROID
        pText->SetVisible( false );
#endif
    }

    // re-center menu items
    //
    menu->ResetTransformation();
#ifdef RAD_ANDROID
    menu->Translate( 0, 100 );
#else
    menu->Translate( 0, 70 );
#endif
#endif

    // TC: [TEMP] disable controller screen for now to free up some memory for HUD map
    //
#ifndef RAD_PC
#ifdef RAD_ANDROID
    m_pMenu->SetMenuItemEnabled( MENU_ITEM_CONTROLLER, true, true );
#else
    m_pMenu->SetMenuItemEnabled( MENU_ITEM_CONTROLLER, false, true );
#endif
#endif

#ifdef RAD_E3
    // disable pause menu settings for E3 build
    //
    m_pMenu->SetMenuItemEnabled( MENU_ITEM_SETTINGS, false );
#endif
MEMTRACK_POP_GROUP("CGUIScreenPauseOptions");
}


//===========================================================================
// CGuiScreenPauseOptions::~CGuiScreenPauseOptions
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
CGuiScreenPauseOptions::~CGuiScreenPauseOptions()
{
    #ifdef RAD_ANDROID
    TouchHudSystem::GetInstance().SetTouchControlsEditorEntryAllowed( false );
    #endif

    if( m_pMenu != NULL )
    {
        delete m_pMenu;
        m_pMenu = NULL;
    }
}


//===========================================================================
// CGuiScreenPauseOptions::HandleMessage
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
void CGuiScreenPauseOptions::HandleMessage
(
	eGuiMessage message, 
	unsigned int param1,
	unsigned int param2 
)
{
    if( this->IsControllerMessage( message ) &&
        GetCheatInputSystem()->IsActivated( param1 ) )
    {
        // ignore all controller inputs when cheat input system is activated
        return;
    }

    if( m_state == GUI_WINDOW_STATE_RUNNING )
    {
        switch( message )
        {
            case GUI_MSG_CONTROLLER_START:
            {
                if( !m_pMenu->HasSelectionBeenMade() )
                {
                    // resume game
                    m_pParent->HandleMessage( GUI_MSG_UNPAUSE_INGAME );
                }

                break;
            }

            case GUI_MSG_MENU_SELECTION_MADE:
            {
                if( param1 == MENU_ITEM_CONTROLLER )
                {
#ifdef RAD_ANDROID
                    RequestAndroidCheatsSettings();
                    m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_SETTINGS );
#else
                    m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_CONTROLLER );
#endif
                }
                else if( param1 == MENU_ITEM_SOUND )
                {
                    m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_SOUND );
                }
                
                else if( param1 == MENU_ITEM_SETTINGS )
                {
                     m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_SETTINGS );
                }
#ifdef RAD_ANDROID
                else if( param1 == MENU_ITEM_GRAPHICS )
                {
                    RequestAndroidGraphicsSettings();
                    m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_SETTINGS );
                }
#endif
            
#ifdef RAD_PC
                else if( param1 == MENU_ITEM_DISPLAY )
                {
                    m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_DISPLAY );
                }
#endif
                else
                {
                    rAssertMsg( false, "Invalid menu selection!" );
                }

                break;
            }
/*
            case GUI_MSG_MENU_SELECTION_CHANGED:
            {
                this->SetButtonVisible( BUTTON_ICON_ACCEPT, (param1 != MENU_ITEM_CAMERA) );

                break;
            }
*/
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


//===========================================================================
// CGuiScreenPauseOptions::InitIntro
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
void CGuiScreenPauseOptions::InitIntro()
{
//    this->SetButtonVisible( BUTTON_ICON_ACCEPT, (m_pMenu->GetSelection() != MENU_ITEM_CAMERA) );

#ifndef RAD_E3
    GetCheatInputSystem()->SetEnabled( true );
#endif

#ifdef RAD_ANDROID
    TouchHudSystem::GetInstance().SetTouchControlsEditorEntryAllowed( true );
#endif
}


//===========================================================================
// CGuiScreenPauseOptions::InitRunning
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
void CGuiScreenPauseOptions::InitRunning()
{
}


//===========================================================================
// CGuiScreenPauseOptions::InitOutro
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
void CGuiScreenPauseOptions::InitOutro()
{
#ifndef RAD_E3
    GetCheatInputSystem()->SetEnabled( false );
#endif

#ifdef RAD_ANDROID
    TouchHudSystem::GetInstance().SetTouchControlsEditorEntryAllowed( false );
#endif
}


//---------------------------------------------------------------------
// Private Functions
//---------------------------------------------------------------------

