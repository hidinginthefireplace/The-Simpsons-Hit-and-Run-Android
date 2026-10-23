//========================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        FrontEndRenderLayer.cpp
//
// Description: Implementation for FrontEndRenderLayer class.
//
// History:     + Initial Implementation -- Tony [6/05/2002]
//
//========================================================================

//========================================
// System Includes
//========================================
#include <raddebug.hpp>
#include <p3d/view.hpp>
#include <p3d/billboardobject.hpp>

//========================================
// Project Includes
//========================================
#include <render/RenderManager/FrontEndRenderLayer.h>
#include <debug/profiler.h>
#include <presentation/gui/guisystem.h>
#include <presentation/presentation.h>
#include <gameflow/gameflow.h>
#include <contexts/contextenum.h>
#include <contexts/gameplay/gameplaycontext.h>
#include <App.h>
#include <Project.h>
#include <Screen.h>
#include <Page.h>
#include <worldsim/coins/coinmanager.h>
#include <worldsim/coins/sparkle.h>
#ifdef RAD_WIN32
#include <input/inputmanager.h>
#endif
#ifdef RAD_ANDROID
#include <presentation/fmvplayer/fmvplayer.h>
#include <input/touch/touchhudrenderer.h>
void ApplySHARPostProcessingBeforeTouchHud();
void ApplySHARFrontendMenuFineDetail();
bool IsSHARFrontendMenuCelPassActive();
#endif


#if defined(RAD_ANDROID)
  #include <android/log.h>
#include "RenderManager.h"

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
//************************************************************************
//
// Global Data, Local Data, Local Classes
//
//************************************************************************

//************************************************************************
//
// Public Member Functions : Context Interface
//
//************************************************************************

//========================================================================
// FrontEndFrontEndRenderLayer::FrontEndRenderLayer
//========================================================================
//
// Description: Inits state and variables to represent Dead State
//
// Parameters:	 None
//
// Return:      None.
//
// Constraints: 
//
//========================================================================
FrontEndRenderLayer::FrontEndRenderLayer()
:   RenderLayer(),
    mpScroobyApp( NULL )
{
#ifdef DEBUGWATCH
    radDbgWatchAddUnsignedInt(&mDebugRenderTime, "Render Time", "Front End Render Layer" );
#endif
}

//========================================================================
// FrontEndFrontEndRenderLayer::~FrontEndRenderLayer
//========================================================================
//
// Description: Cleans state and variables to represent Dead State
//
// Parameters:	 None
//
// Return:      None.
//
// Constraints: 
//
//========================================================================
FrontEndRenderLayer::~FrontEndRenderLayer()
{
#ifdef DEBUGWATCH
    radDbgWatchDelete(&mDebugRenderTime);
#endif
}

void FrontEndRenderLayer::DrawCoinObject()
{
    // Render HUD coin effects.
    if((GetGameFlow()->GetCurrentContext() == CONTEXT_GAMEPLAY ||
        GetGameFlow()->GetCurrentContext() == CONTEXT_PAUSE) &&
	   !GetPresentationManager()->IsBusy())
	{
        GetCoinManager()->HUDRender();
        GetSparkleManager()->HUDRender();
        //??? GetHitnRunManager()->HUDRender();
    }
	else
	{
		GetCoinManager()->ClearHUDCoins();
	}

}
//************************************************************************
// Render Interface
//************************************************************************
//========================================================================
// FrontEndFrontEndRenderLayer::Render
//========================================================================
//
// Description: Renders all (TODO:visible/DSG) drawables
//
// Parameters:	 None
//
// Return:      None.
//
// Constraints: 
//
//========================================================================

// FUNCION ORIGINAL DA PROBLEMAS EN ANDROID 64 bits por un problema de orden de render de capas, la capa gui se pinta por encima tapando el video 
/*
void FrontEndRenderLayer::Render()
{
    BEGIN_PROFILE( "FE Render" );

#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds();
#endif

    for( unsigned int view = 0; view < mNumViews; view++ )
    {
        mpView[ view ]->BeginRender();

        rAssert(!IsDead());

        HeapMgr()->PushHeap( GMA_TEMP );

        if (!GetCoinManager()->DrawAfterGui())
            DrawCoinObject();
        // display Scrooby screen (and updates all Pure3d objects)
        //
        mpScroobyApp->DrawFrame( static_cast<float>( g_scroobySimulationTime ) );
#ifdef RAD_PC
        // Update the frontend cursor.
        GetInputManager()->GetFEMouse()->Update();
#endif

        if (GetCoinManager()->DrawAfterGui())
            DrawCoinObject();

        HeapMgr()->PopHeap ( GMA_TEMP );

//        GetBillboardQuadManager()->DisplayAll();

        mpView[ view ]->EndRender();
    }

#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds()-mDebugRenderTime;
#endif

    END_PROFILE( "FE Render" );
}

*/




// ESTA FUNCION ES LA DEFINITIVA(6/03/2026 19:00)  (Esta es la que actualmente funciona las cinematicas in game con parpadeo )

/*
void FrontEndRenderLayer::Render()
{
    BEGIN_PROFILE( "FE Render" );

#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds();
#endif

    bool fmvPlaying = false;
    PresentationManager* pm = GetPresentationManager();
    if( pm && pm->GetFMVPlayer() )
    {
        fmvPlaying = pm->GetFMVPlayer()->IsPlaying();
    }

    for( unsigned int view = 0; view < mNumViews; view++ )
    {
        rAssert(!IsDead());

        // nuevos cambios

        if( mpView[ view ] == NULL )
        {
            continue;
        }

        mpView[ view ]->BeginRender();

        if( !fmvPlaying )
        {
            HeapMgr()->PushHeap( GMA_TEMP );

            if( !GetCoinManager()->DrawAfterGui() )
                DrawCoinObject();

            if( mpScroobyApp != NULL )
            {
                mpScroobyApp->DrawFrame( static_cast<float>( g_scroobySimulationTime ) );
            }

#ifdef RAD_PC
            GetInputManager()->GetFEMouse()->Update();
#endif

            if( GetCoinManager()->DrawAfterGui() )
                DrawCoinObject();

            HeapMgr()->PopHeap( GMA_TEMP );
        }

        mpView[ view ]->EndRender();

        //Fin nuevos cambios
    }

#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds() - mDebugRenderTime;
#endif

    END_PROFILE( "FE Render" );
}
*/


// PRUEBO ESTA NUEVA VERSION 

void FrontEndRenderLayer::Render()
{
    BEGIN_PROFILE( "FE Render" );

#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds();
#endif

    bool fmvPlaying = false;
    PresentationManager* pm = GetPresentationManager();
    if( pm && pm->GetFMVPlayer() )
    {
        fmvPlaying = pm->GetFMVPlayer()->IsPlaying();
    }

    static bool s_loggedFmvForeground = false;
    if( fmvPlaying )
    {
#ifdef RAD_ANDROID
        // Do not call DrawFrame() during an FMV: that would render the full
        // movie screen UI and can cover the video. Render only the foreground
        // pages, which is where the standalone TVFrame page belongs.
        for( unsigned int view = 0; view < mNumViews; view++ )
        {
            if( mpView[ view ] == NULL )
            {
                continue;
            }

            mpView[ view ]->BeginRender();

            if( mpScroobyApp != NULL )
            {
                if( !s_loggedFmvForeground )
                {
                    Scrooby::Project* project = mpScroobyApp->GetProject();
                    Scrooby::Screen* screen = project != NULL ? project->GetCurrentScreen() : NULL;
                    Scrooby::Page* divider = screen != NULL ? screen->GetPage( "FeDivide" ) : NULL;
                    Scrooby::Page* tvFramePage = screen != NULL ? screen->GetPage( "TVFrame" ) : NULL;
                    LOGI( "FMVForeground: screen=%p pages=%d divider=%p tvFramePage=%p; drawing foreground pages only",
                          (void*)screen,
                          screen != NULL ? screen->GetNumberOfPages() : -1,
                          (void*)divider, (void*)tvFramePage );
                    s_loggedFmvForeground = true;
                }

                mpScroobyApp->DrawForeground( static_cast<float>( g_scroobySimulationTime ) );
            }

            mpView[ view ]->EndRender();
        }
#else
        // Preserve the original non-Android behavior during FMV playback.
#endif
#ifdef DEBUGWATCH
        mDebugRenderTime = radTimeGetMicroseconds() - mDebugRenderTime;
#endif
        END_PROFILE( "FE Render" );
        return;
    }
    s_loggedFmvForeground = false;

    for( unsigned int view = 0; view < mNumViews; view++ )
    {
        rAssert( !IsDead() );

        if( mpView[ view ] == NULL )
        {
            continue;
        }

        mpView[ view ]->BeginRender();

        HeapMgr()->PushHeap( GMA_TEMP );

        if( !GetCoinManager()->DrawAfterGui() )
            DrawCoinObject();

        if( mpScroobyApp != NULL )
        {
#ifdef RAD_ANDROID
            // The front-end fine-contour pass must only see the 3D living-room
            // scene. Scrooby's FeDivide split lets us process the background
            // before the 2D foreground menu, so text, sprites and loading UI
            // are never sampled by the contour shader.
            Scrooby::Project* project = mpScroobyApp->GetProject();
            Scrooby::Screen* screen = project != NULL ? project->GetCurrentScreen() : NULL;
            const bool hasMenuCameraSet =
                IsSHARFrontendMenuCelPassActive() &&
                screen != NULL &&
                screen->GetPage( "3dFE" ) != NULL &&
                screen->GetPage( "FeDivide" ) != NULL;

            if( hasMenuCameraSet )
            {
                const float frameDelta = static_cast<float>( g_scroobySimulationTime );
                mpScroobyApp->DrawBackground( frameDelta );
                ApplySHARFrontendMenuFineDetail();
                mpScroobyApp->DrawForeground( frameDelta );
            }
            else
            {
                mpScroobyApp->DrawFrame( static_cast<float>( g_scroobySimulationTime ) );
            }
#else
            mpScroobyApp->DrawFrame( static_cast<float>( g_scroobySimulationTime ) );
#endif
        }

#ifdef RAD_PC
        GetInputManager()->GetFEMouse()->Update();
#endif

        if( GetCoinManager()->DrawAfterGui() )
            DrawCoinObject();
        
        
        #ifdef RAD_ANDROID
        ApplySHARPostProcessingBeforeTouchHud();
        TouchHudRenderer::GetInstance().Render(); // Render touch controls after full-frame filters.
        #endif
        HeapMgr()->PopHeap( GMA_TEMP );

        mpView[ view ]->EndRender();

        
    }

#ifdef DEBUGWATCH
    mDebugRenderTime = radTimeGetMicroseconds() - mDebugRenderTime;
#endif

    END_PROFILE( "FE Render" );
}



//************************************************************************
// Resource Interface
//************************************************************************
//////////////////////////////////////////////////////////////////////////
// Guts; Renderable Type Things
//////////////////////////////////////////////////////////////////////////
//========================================================================
// FrontEndRenderLayer::AddGuts
//========================================================================
//
// Description: Add a tDrawable
//
// Parameters:	 tDrawable to add
//
// Return:      None.
//
// Constraints: 
//
//========================================================================
void FrontEndRenderLayer::AddGuts( tDrawable* ipDrawable )
{
   //The Basic FrontEndRenderLayer does not support this type
   rAssert(false);
}

//========================================================================
// FrontEndRenderLayer::AddGuts
//========================================================================
//
// Description: Add a tGeometry
//
// Parameters:	 tGeometry to add
//
// Return:      None.
//
// Constraints: 
//
//========================================================================
void FrontEndRenderLayer::AddGuts( tGeometry* ipGeometry )
{
   //The Basic FrontEndRenderLayer does not support this type
   rAssert(false);
}

//========================================================================
// FrontEndRenderLayer::AddGuts
//========================================================================
//
// Description: Add an IntersectDSG
//
// Parameters:  IntersectDSG to add
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void FrontEndRenderLayer::AddGuts( IntersectDSG* ipIntersectDSG )
{
   //The Basic FrontEndRenderLayer does not support this type
   rAssert(false);
}

//========================================================================
// FrontEndRenderLayer::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void FrontEndRenderLayer::AddGuts( StaticEntityDSG* ipStaticEntityDSG )
{
   //The Basic FrontEndRenderLayer does not support this type
   rAssert(false);
}

//========================================================================
// FrontEndRenderLayer::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void FrontEndRenderLayer::AddGuts( StaticPhysDSG* ipStaticPhysDSG )
{
   //The Basic FrontEndRenderLayer does not support this type
   rAssert(false);
}

//========================================================================
// FrontEndRenderLayer::AddGuts
//========================================================================
//
// Description: Add the Scrooby App reference
//
// Parameters:  Scrooby App to add
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void FrontEndRenderLayer::AddGuts( Scrooby::App* ipScroobyApp )
{
    rAssert( mpScroobyApp == NULL );

    mpScroobyApp = ipScroobyApp;
}

//========================================================================
// FrontEndRenderLayer::SetUpGuts
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void FrontEndRenderLayer::SetUpGuts()
{
    // do nothing
}

//========================================================================
// FrontEndRenderLayer::NullifyGuts
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void FrontEndRenderLayer::NullifyGuts()
{
    mpScroobyApp = NULL;
}
