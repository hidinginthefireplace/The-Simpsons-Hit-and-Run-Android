//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   CGuiScreenPlayMovie
//
// Description: Implementation of the CGuiScreenPlayMovie class.
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
#include <presentation/gui/frontend/guiscreenplaymovie.h>
#include <presentation/gui/guimanager.h>
#include <presentation/gui/guitextbible.h>
#include <presentation/gui/utility/specialfx.h>

#include <presentation/presentation.h>
#include <presentation/fmvplayer/fmvplayer.h>
#include <presentation/fmvplayer/fmvuserinputhandler.h>

#include <memory/srrmemory.h>
#include <render/RenderManager/RenderManager.h>
#include <render/RenderManager/RenderLayer.h>

#include <Screen.h>
#include <Page.h>
#include <Text.h>
#include <Layer.h>

#include <p3d/view.hpp>

#include <raddebug.hpp> // Foundation

#if defined(RAD_ANDROID)
#include <android/log.h>
#define TVFRAME_DIAG(...) __android_log_print(ANDROID_LOG_INFO, "SHR-TVFrameDiag", __VA_ARGS__)
#else
#define TVFRAME_DIAG(...) rDebugPrintf(__VA_ARGS__)
#endif
#include <radmovie2.hpp>

#include <sound/soundmanager.h>

#include <input/inputmanager.h>


//===========================================================================
// Global Data, Local Data, Local Classes
//===========================================================================

//===========================================================================
// Public Member Functions
//===========================================================================

//===========================================================================
// CGuiScreenPlayMovie::CGuiScreenPlayMovie
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
CGuiScreenPlayMovie::CGuiScreenPlayMovie
(
    Scrooby::Screen* pScreen,
    CGuiEntity* pParent,
    eGuiWindowID windowID
)
:   CGuiScreen( pScreen, pParent, windowID ),
    m_demoText( NULL ),
    m_tvFrame( NULL ),
    m_elapsedTime( 0 ),
	m_IsSkippable(true)
{
    // The normal movie screen and the demo screen do not have the same
    // page set. Retrieve Demo text only when that page actually exists.
    //
    Scrooby::Page* pDemoPage = m_pScroobyScreen->GetPage( "Demo" );
    TVFRAME_DIAG("TVFrameDiag: ctor windowId=%d screen=%p DemoPage=%p",
                 (int)windowID, (void*)m_pScroobyScreen, (void*)pDemoPage);
    if( pDemoPage != NULL )
    {
        m_demoText = pDemoPage->GetText( "Demo" );
        TVFRAME_DIAG("TVFrameDiag: Demo text=%p", (void*)m_demoText);
    }

    // The Android frontend assets contain a dedicated TVFrame page. Prefer
    // its layer; fall back to legacy layer nesting for older asset layouts.
    //
    Scrooby::Page* pTVFramePage = m_pScroobyScreen->GetPage( "TVFrame" );
    TVFRAME_DIAG("TVFrameDiag: TVFrame page=%p", (void*)pTVFramePage);
    if( pTVFramePage != NULL )
    {
        m_tvFrame = pTVFramePage->GetLayer( "TVFrame" );
        if( m_tvFrame == NULL && pTVFramePage->GetNumberOfLayers() == 1 )
        {
            m_tvFrame = pTVFramePage->GetLayerByIndex( 0 );
        }
        TVFRAME_DIAG("TVFrameDiag: dedicated TVFrame page layers=%d selectedLayer=%p",
                     pTVFramePage->GetNumberOfLayers(), (void*)m_tvFrame);
    }

    if( m_tvFrame == NULL )
    {
        Scrooby::Page* pPlayMoviePage = m_pScroobyScreen->GetPage( "PlayMovie" );
        if( pPlayMoviePage != NULL )
        {
            m_tvFrame = pPlayMoviePage->GetLayer( "TVFrame" );
            TVFRAME_DIAG("TVFrameDiag: PlayMovie page=%p TVFrame layer=%p",
                         (void*)pPlayMoviePage, (void*)m_tvFrame);
        }
    }

    if( m_tvFrame == NULL && pDemoPage != NULL )
    {
        m_tvFrame = pDemoPage->GetLayer( "TVFrame" );
        TVFRAME_DIAG("TVFrameDiag: Demo fallback TVFrame layer=%p", (void*)m_tvFrame);
    }

    if( m_tvFrame != NULL )
    {
        int xMin = 0, yMin = 0, xMax = 0, yMax = 0;
        m_tvFrame->GetBoundingBox( xMin, yMin, xMax, yMax );
        m_tvFrame->SetVisible( true );
        TVFRAME_DIAG("TVFrameDiag: selected frame=%p bbox=(%d,%d)-(%d,%d) visible=%d",
                     (void*)m_tvFrame, xMin, yMin, xMax, yMax,
                     m_tvFrame->IsVisible() ? 1 : 0);
    }
    else
    {
        TVFRAME_DIAG("TVFrameDiag: no TVFrame layer found for windowId=%d", (int)windowID);
    }

    if( windowID == GUI_SCREEN_ID_PLAY_MOVIE )
    {
        // invert fading; ie. fade out during transition in, and vice versa
        //
        m_inverseFading = true;
    }
}


//===========================================================================
// CGuiScreenPlayMovie::~CGuiScreenPlayMovie
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
CGuiScreenPlayMovie::~CGuiScreenPlayMovie()
{
}


//===========================================================================
// CGuiScreenPlayMovie::HandleMessage
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
void CGuiScreenPlayMovie::HandleMessage
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
            case GUI_MSG_UPDATE:
            {
                GetPresentationManager()->Update( param1 );

                if( m_demoText != NULL )
                {
                    m_elapsedTime += param1;

                    const unsigned int BLINK_PERIOD = 250;
                    bool blinked = GuiSFX::Blink( m_demoText,
                                                (float)m_elapsedTime,
                                                (float)BLINK_PERIOD );
                    if( blinked )
                    {
                        m_elapsedTime %= BLINK_PERIOD;
                    }
                }
                // The dedicated TV frame remains visible throughout movie playback.

                break;
            }

            case GUI_MSG_CONTROLLER_BACK:
            {
                // don't allow user to back out of screen, must be done
                // by skipping movie
                //
                return;

                break;
            }

            default:
            {
                break;
            }
        }
    }

	// Propogate the message up the hierarchy.
	//
	CGuiScreen::HandleMessage( message, param1, param2 );
}


void
CGuiScreenPlayMovie::SetMovieToPlay( const char* movieName, bool IsSkippable, bool KillMusic, bool IsLocalized )
{
    rAssert( movieName != NULL );

    FMVEvent* pEvent = NULL;
    GetPresentationManager()->QueueFMV( &pEvent, this );

    strcpy( pEvent->fileName, movieName );
    pEvent->SetRenderLayer( RenderEnums::PresentationSlot );
    pEvent->SetAutoPlay( false );
    pEvent->SetClearWhenDone( true );
	pEvent->SetSkippable(IsSkippable);

#ifdef PAL
    if( IsLocalized )
    {
        switch( CGuiTextBible::GetCurrentLanguage() )
        {
            case Scrooby::XL_FRENCH:
            {
                pEvent->SetAudioIndex( FMVEvent::AUDIO_INDEX_FRENCH );

                break;
            }
            case Scrooby::XL_GERMAN:
            {
                pEvent->SetAudioIndex( FMVEvent::AUDIO_INDEX_GERMAN );

                break;
            }
            case Scrooby::XL_SPANISH:
            {
                pEvent->SetAudioIndex( FMVEvent::AUDIO_INDEX_SPANISH );

                break;
            }
            default:
            {
                rAssert( CGuiTextBible::GetCurrentLanguage() == Scrooby::XL_ENGLISH );
                pEvent->SetAudioIndex( FMVEvent::AUDIO_INDEX_ENGLISH );

                break;
            }
        }
    }
    else
#endif // PAL
    {
        pEvent->SetAudioIndex( FMVEvent::AUDIO_INDEX_ENGLISH );
    }

    if( KillMusic )
    {
        pEvent->KillMusic();
    }
}


//=============================================================================
// CGuiScreenPlayMovie::OnPresentationEventBegin
//=============================================================================
// Description: Comment
//
// Parameters:  ( PresentationEvent* pEvent )
//
// Return:      void 
//
//=============================================================================
void CGuiScreenPlayMovie::OnPresentationEventBegin( PresentationEvent* pEvent )
{
    GetSoundManager()->StopForMovie();
}


//=============================================================================
// CGuiScreenPlayMovie::OnPresentationEventLoadComplete
//=============================================================================
// Description: Comment
//
// Parameters:  ( PresentationEvent* pEvent )
//
// Return:      void 
//
//=============================================================================
void CGuiScreenPlayMovie::OnPresentationEventLoadComplete( PresentationEvent* pEvent )
{
}


//=============================================================================
// CGuiScreenPlayMovie::OnPresentationEventEnd
//=============================================================================
// Description: Comment
//
// Parameters:  ( PresentationEvent* pEvent )
//
// Return:      void 
//
//=============================================================================
void CGuiScreenPlayMovie::OnPresentationEventEnd( PresentationEvent* pEvent )
{
    if( GetPresentationManager()->IsQueueEmpty() )
    {
        GetSoundManager()->ResumeAfterMovie();

        if( m_ID == GUI_SCREEN_ID_PLAY_MOVIE_INTRO )
        {
            // finished playing intro movie, now go to main menu
            //
            m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN,
                                      GUI_SCREEN_ID_INTRO_TRANSITION,
                                      CLEAR_WINDOW_HISTORY );
        }
        else if( m_ID == GUI_SCREEN_ID_PLAY_MOVIE_NEW_GAME )
        {
            // finished playing new game movie, now quit the FE
            //
            m_pParent->HandleMessage( GUI_MSG_QUIT_FRONTEND, 1 ); // 1 = one player
        }
        else
        {
            rAssert( m_guiManager );
            eGuiWindowID previousScreen = m_guiManager->GetPreviousScreen();
            if( previousScreen == GUI_SCREEN_ID_VIEW_MOVIES )
            {
                this->StartTransitionAnimation( 1005, 1035 );
            }
            else if( previousScreen == GUI_SCREEN_ID_OPTIONS )
            {
                this->StartTransitionAnimation( 630, 660 );
            }

            m_pParent->HandleMessage( GUI_MSG_BACK_SCREEN );
        }
    }
}


//===========================================================================
// CGuiScreenPlayMovie::InitIntro
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
void CGuiScreenPlayMovie::InitIntro()
{
    Scrooby::Page* diagPage = m_pScroobyScreen->GetPage( "Demo" );
    Scrooby::Page* diagTVFramePage = m_pScroobyScreen->GetPage( "TVFrame" );
    TVFRAME_DIAG("TVFrameDiag: InitIntro windowId=%d DemoPage=%p TVFramePage=%p selectedFrame=%p visible=%d",
                 (int)m_ID, (void*)diagPage, (void*)diagTVFramePage,
                 (void*)m_tvFrame, m_tvFrame != NULL && m_tvFrame->IsVisible() ? 1 : 0);

    rAssertMsg( !GetPresentationManager()->IsQueueEmpty(),
                "ERROR: *** No movies were queued for playing!" );

    if( m_demoText != NULL )
    {
        m_demoText->SetVisible( false );
        m_elapsedTime = 0;
    }

#ifdef RAD_PC
    GetInputManager()->GetFEMouse()->SetInGameMode( true );
#endif

    // Keep the TV border visible throughout movie playback. The original
    // code hid this layer at intro, relying on a separate transition frame.
    //
    if( m_tvFrame != NULL )
    {
        m_tvFrame->SetVisible( true );
    }
}


//===========================================================================
// CGuiScreenPlayMovie::InitRunning
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
void CGuiScreenPlayMovie::InitRunning()
{
    Scrooby::Page* diagPage = m_pScroobyScreen->GetPage( "Demo" );
    Scrooby::Page* diagTVFramePage = m_pScroobyScreen->GetPage( "TVFrame" );
    TVFRAME_DIAG("TVFrameDiag: InitRunning windowId=%d DemoPage=%p TVFramePage=%p selectedFrame=%p visible=%d",
                 (int)m_ID, (void*)diagPage, (void*)diagTVFramePage,
                 (void*)m_tvFrame, m_tvFrame != NULL && m_tvFrame->IsVisible() ? 1 : 0);

    if( m_ID == GUI_SCREEN_ID_PLAY_MOVIE_DEMO ||
        m_ID == GUI_SCREEN_ID_PLAY_MOVIE )
    {
        // Keep the GUI layer active so the TV frame can render over the FMV.
        // Only depth and stencil are cleared, matching the demo-movie path.
        //
        GetRenderManager()->mpLayer( RenderEnums::GUI )->pView( 0 )->SetClearMask( PDDI_BUFFER_DEPTH | PDDI_BUFFER_STENCIL );
    }
    else
    {
        GetRenderManager()->mpLayer( RenderEnums::GUI )->Chill();
    }

    GetPresentationManager()->GetFMVPlayer()->Play();

#ifdef FINAL
    if( m_ID == GUI_SCREEN_ID_PLAY_MOVIE_NEW_GAME )
    {
        // disable skipping of new game movie
        //
        FMVUserInputHandler* userInputHandler = GetPresentationManager()->GetFMVPlayer()->GetUserInputHandler();
        rAssert( userInputHandler != NULL );
        userInputHandler->SetEnabled( false );
    }
#endif
}


//===========================================================================
// CGuiScreenPlayMovie::InitOutro
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
void CGuiScreenPlayMovie::InitOutro()
{
    Scrooby::Page* diagPage = m_pScroobyScreen->GetPage( "Demo" );
    Scrooby::Page* diagTVFramePage = m_pScroobyScreen->GetPage( "TVFrame" );
    TVFRAME_DIAG("TVFrameDiag: InitOutro windowId=%d DemoPage=%p TVFramePage=%p selectedFrame=%p visible=%d",
                 (int)m_ID, (void*)diagPage, (void*)diagTVFramePage,
                 (void*)m_tvFrame, m_tvFrame != NULL && m_tvFrame->IsVisible() ? 1 : 0);

    if( m_ID == GUI_SCREEN_ID_PLAY_MOVIE_DEMO ||
        m_ID == GUI_SCREEN_ID_PLAY_MOVIE )
    {
        // Restore normal GUI clear behavior now that FMV playback is ending.
        //
        GetRenderManager()->mpLayer( RenderEnums::GUI )->pView( 0 )->SetClearMask( PDDI_BUFFER_ALL );
    }
    else
    {
        GetRenderManager()->mpLayer( RenderEnums::GUI )->Warm();
    }

#ifdef RAD_PC
    GetInputManager()->GetFEMouse()->SetInGameMode( false );
#endif

#ifdef FINAL
    if( m_ID == GUI_SCREEN_ID_PLAY_MOVIE_NEW_GAME )
    {
        // re-enable FMV user inputs
        //
        FMVUserInputHandler* userInputHandler = GetPresentationManager()->GetFMVPlayer()->GetUserInputHandler();
        rAssert( userInputHandler != NULL );
        userInputHandler->SetEnabled( true );
    }
#endif
}


//---------------------------------------------------------------------
// Private Functions
//---------------------------------------------------------------------

