//========================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        StaticEntityDSG.cpp
//
// Description: Implementation for StaticEntityDSG class.
//
// History:     Implemented	                         --Devin [5/27/2002]
//========================================================================

//========================================
// System Includes
//========================================

//========================================
// Project Includes
//========================================
#include <render/DSG/StaticEntityDSG.h>
#include <memory/srrmemory.h>
#include <p3d/utility.hpp>
#include <p3d/texture.hpp>
#ifdef RAD_ANDROID
#include <android/log.h>
#include <ctype.h>
#include <string.h>

static bool IsSimpleCircleShadowDiagnosticName(const char* value)
{
   if (value == NULL)
   {
      return false;
   }

   char lowerName[256];
   size_t i = 0;
   for (; value[i] != '\0' && i < sizeof(lowerName) - 1; ++i)
   {
      lowerName[i] = (char)tolower((unsigned char)value[i]);
   }
   lowerName[i] = '\0';
   return strstr(lowerName, "simplecircleshadow") != NULL;
}
#endif

//************************************************************************
//
// Global Data, Local Data, Local Classes
//
//************************************************************************

//************************************************************************
//
// Public Member Functions : StaticEntityDSG Interface
//
//************************************************************************
//========================================================================
// StaticEntityDSG::
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
StaticEntityDSG::StaticEntityDSG()
{
   mpDrawstuff = NULL;
}
//========================================================================
// StaticEntityDSG::
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
StaticEntityDSG::~StaticEntityDSG()
{
BEGIN_PROFILE( "StaticEntityDSG Destroy" );
   if(mpDrawstuff != NULL)
   {
      mpDrawstuff->Release();
   }
END_PROFILE( "StaticEntityDSG Destroy" );
}
//========================================================================
// StaticEntityDSG::SetRank
//========================================================================
//
// Description: Sets rank, defaults to default SetRank for normal geo
//              however, shadows always get drawn first in the translucent pass
//
// Parameters:  rmt::Vector& irRefPosn, rmt::Vector& mViewVector.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void StaticEntityDSG::SetRank(rmt::Vector& irRefPosn, rmt::Vector& mViewVector)
{
    if ( ( mIsGeo & IS_SHADOW ) == false )
    {
        IEntityDSG::SetRank( irRefPosn, mViewVector );
    }
    else
    {
        mRank = FLT_MAX;
    }
}

//========================================================================
// StaticEntityDSG::
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
void StaticEntityDSG::SetGeometry(tGeometry* ipGeo)
{
   if(mpDrawstuff != NULL)
   {
      mpDrawstuff->Release();
   }
   
   mpDrawstuff = ipGeo;

   if(mpDrawstuff != NULL)
   {
      mpDrawstuff->AddRef();
   }
   mIsGeo = GEO;

   if(ipGeo->CastsShadow())
   {
       mIsGeo = mIsGeo | IS_SHADOW;
   }
   
//   mShaderUID = ipGeo->GetShader(0)->GetUID();
   ipGeo->ProcessShaders(*this);

#ifdef RAD_ANDROID
   // Log simple-circle shadow geometry and its actual runtime shader bindings
   // once when the static entity is configured, rather than every frame.
   const char* geometryName = ipGeo->GetNameDangerous();
   if (IsSimpleCircleShadowDiagnosticName(geometryName))
   {
      __android_log_print(ANDROID_LOG_INFO, "SHR-CircleShadow",
         "GEOMETRY name=%s castsShadow=%d shaderCount=%d",
         geometryName, ipGeo->CastsShadow() ? 1 : 0, ipGeo->GetNumShader());

      for (int i = 0; i < ipGeo->GetNumShader(); ++i)
      {
         tShader* shader = ipGeo->GetShader(i);
         if (shader != NULL)
         {
            __android_log_print(ANDROID_LOG_INFO, "SHR-CircleShadow",
               "GEOMETRY_SHADER geometry=%s index=%d material=%s shaderType=%s",
               geometryName, i, shader->GetNameDangerous(), shader->GetType());
         }
      }
   }
#endif

   SetInternalState();
}
//========================================================================
// StaticEntityDSG::
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
tGeometry* StaticEntityDSG::mpGeo()
{
   return (tGeometry*)mpDrawstuff;
}
//========================================================================
// StaticEntityDSG::
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
void StaticEntityDSG::SetDrawable(tDrawable* ipDraw)
{
   if(mpDrawstuff != NULL)
   {
      mpDrawstuff->Release();
   }
   
   mpDrawstuff = ipDraw;

   if(mpDrawstuff != NULL)
   {
      mpDrawstuff->AddRef();
   }

   mIsGeo = NOT_GEO;

   SetInternalState();
}
//========================================================================
// StaticEntityDSG::
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
tDrawable* StaticEntityDSG::mpDraw()
{
    return mpDrawstuff;

}

///////////////////////////////////////////////////////////////////////
// Drawable
///////////////////////////////////////////////////////////////////////
//========================================================================
// StaticEntityDSG::
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
void StaticEntityDSG::Display()
{
#ifdef PROFILER_ENABLED
    char profileName[] = "  StaticEntityDSG Display";
#endif
    if(IS_DRAW_LONG) return;
#ifdef RAD_ANDROID
    UpdateSelectedShadowAlphaForCelState();
#endif
    DSG_BEGIN_PROFILE(profileName)

    if(mIsGeo & IS_SHADOW)
    {
        p3d::pddi->SetZWrite(false);
        mpDrawstuff->Display();
        p3d::pddi->SetZWrite(true);
    }
    else
    {
        mpDrawstuff->Display();
    }
    DSG_END_PROFILE(profileName)
}

#ifndef RAD_RELEASE
//========================================================================
// StaticEntityDSG::
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
void StaticEntityDSG::DisplayBoundingBox(tColour colour)
{
#ifndef RAD_RELEASE
   mpDrawstuff->DisplayBoundingBox(colour);
#endif
}
//========================================================================
// StaticEntityDSG::
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
void StaticEntityDSG::DisplayBoundingSphere(tColour colour)
{
   mpDrawstuff->DisplayBoundingSphere(colour);
}
#endif

//========================================================================
// StaticEntityDSG::
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
void StaticEntityDSG::GetBoundingBox(rmt::Box3D* box)
{
   mpDrawstuff->GetBoundingBox(box);
}
//========================================================================
// StaticEntityDSG::
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
void StaticEntityDSG::GetBoundingSphere(rmt::Sphere* sphere)
{
   mpDrawstuff->GetBoundingSphere(sphere);
}
///////////////////////////////////////////////////////////////////////
// IEntityDSG
///////////////////////////////////////////////////////////////////////
//========================================================================
// StaticEntityDSG::
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
rmt::Vector* StaticEntityDSG::pPosition()
{
   return &mPosn;
}
//========================================================================
// StaticEntityDSG::
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
const rmt::Vector& StaticEntityDSG::rPosition()
{
   return mPosn;
}
//========================================================================
// StaticEntityDSG::
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
void StaticEntityDSG::GetPosition( rmt::Vector* ipPosn )
{
   *ipPosn = mPosn;
}
//========================================================================
// StaticEntityDSG::
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
void StaticEntityDSG::RenderUpdate()
{
   //Do Nothing
}
//************************************************************************
//
// Protected Member Functions : StaticEntityDSG 
//
//************************************************************************
//========================================================================
// StaticEntityDSG::
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
void StaticEntityDSG::SetInternalState()
{
   rmt::Sphere sphere;

   mpDrawstuff->GetBoundingSphere(&sphere);
   mPosn = sphere.centre;
}
//************************************************************************
//
// Private Member Functions : StaticEntityDSG 
//
//************************************************************************


