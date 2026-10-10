//========================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        StaticPhysDSG.cpp
//
// Description: Implementation for StaticPhysDSG class.
//
// History:     Implemented	                         --Devin [5/27/2002]
//========================================================================

//========================================
// System Includes
//========================================
#include <p3d/camera.hpp>
#include <p3d/matrixstack.hpp>
#include <p3d/utility.hpp>
#include <p3d/view.hpp>
#include <contexts/bootupcontext.h>
#include <pddi/pddi.hpp>
#include <string.h>
#ifdef RAD_ANDROID
#include <android/log.h>
#endif

#ifdef RAD_ANDROID
bool IsCelShadingEnabled();
#endif
#include <simcollision/collisionobject.hpp>
#include <simcollision/collisionvolume.hpp>

//========================================
// Project Includes
//========================================
#include <render/DSG/StaticPhysDSG.h>
#include <render/Particles/particlemanager.h>
#include <render/breakables/breakablesmanager.h>
#include <render/IntersectManager/IntersectManager.h>

//************************************************************************
//
// Global Data, Local Data, Local Classes
//
//************************************************************************

// Bias that determines how much force is required to emit particles during a 
// collision
const float STAT_PHYS_MASS_IMPULSE_PARTICLE_BIAS = 10.0f;

static const float SMALL_TREE_TOON_SHADOW_FALLBACK_RADIUS_X = 1.15f;
static const float SMALL_TREE_TOON_SHADOW_FALLBACK_RADIUS_Z = 1.15f;

#ifdef RAD_ANDROID
namespace
{
    const int SMALL_TREE_TOON_SHADOW_SLICES = 32;

    bool IsSmallTreeShadowAsset(const char* shadowName)
    {
        // Diagnostic mode deliberately matches the shared small-tree shadow
        // drawable regardless of the owning object's name. Cypress trees may
        // also be highlighted because they share this same asset.
        return shadowName != NULL &&
               strcmp(shadowName, "treeshadowsmall") == 0;
    }

    bool DrawSmallTreeToonShadow(
        float centerX,
        float centerZ,
        float radiusX,
        float radiusZ)
    {
        BootupContext* bootupContext = BootupContext::GetInstance();
        if (bootupContext == NULL || p3d::pddi == NULL)
        {
            return false;
        }

        pddiShader* shadowShader = bootupContext->GetSharedShader();
        if (shadowShader == NULL)
        {
            return false;
        }

        // Diagnostic pass: replace the authored shadow with a bright magenta marker
        // so it's obvious whether this code path is reached. This is temporary.
        // No blending: the magenta marker should be clearly visible over the ground.
        shadowShader->SetInt(PDDI_SP_BLENDMODE, PDDI_BLEND_NONE);
        shadowShader->SetInt(PDDI_SP_ISLIT, 0);
        shadowShader->SetInt(PDDI_SP_ALPHATEST, 0);
        shadowShader->SetInt(PDDI_SP_SHADEMODE, PDDI_SHADE_GOURAUD);

        pddiPrimStream* stream = p3d::pddi->BeginPrims(
            shadowShader,
            PDDI_PRIM_TRIANGLES,
            PDDI_V_C,
            SMALL_TREE_TOON_SHADOW_SLICES * 3);
        if (stream == NULL)
        {
            return false;
        }

        // Deliberately unmistakable diagnostic marker; this is not the final shadow tint.
        const tColour shadowColour(255, 0, 255, 255);
        const float angleStep = rmt::PI_2 / float(SMALL_TREE_TOON_SHADOW_SLICES);

        for (int i = 0; i < SMALL_TREE_TOON_SHADOW_SLICES; ++i)
        {
            float sin0, cos0, sin1, cos1;
            rmt::SinCos(angleStep * float(i), &sin0, &cos0);
            rmt::SinCos(angleStep * float(i + 1), &sin1, &cos1);

            stream->Colour(shadowColour);
            stream->Coord(centerX, 0.0f, centerZ);

            stream->Colour(shadowColour);
            stream->Coord(centerX + radiusX * cos0, 0.0f, centerZ + radiusZ * sin0);

            stream->Colour(shadowColour);
            stream->Coord(centerX + radiusX * cos1, 0.0f, centerZ + radiusZ * sin1);
        }

        p3d::pddi->EndPrims(stream);
        return true;
    }
}
#endif

//************************************************************************
//
// Public Member Functions : StaticPhysDSG Interface
//
//************************************************************************

//========================================================================
// StaticPhysDSG::
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
StaticPhysDSG::StaticPhysDSG() : 
mpShadow( NULL ),
mpShadowMatrix( NULL ),
mUseToonSmallTreeShadow( false ),
mToonSmallTreeShadowCenterX( 0.0f ),
mToonSmallTreeShadowCenterZ( 0.0f ),
mToonSmallTreeShadowRadiusX( SMALL_TREE_TOON_SHADOW_FALLBACK_RADIUS_X ),
mToonSmallTreeShadowRadiusZ( SMALL_TREE_TOON_SHADOW_FALLBACK_RADIUS_Z )
{
   mpSimStateObj = NULL;
}

//========================================================================
// StaticPhysDSG::
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
StaticPhysDSG::~StaticPhysDSG()
{
BEGIN_PROFILE( "StaticPhysDSG Destroy" );
   if(mpSimStateObj != NULL)
   {
      mpSimStateObj->Release();
   }
    if (mpShadow)
    {
        mpShadow->Release();
        mpShadow = 0;
    }
    if (mpShadowMatrix != NULL )
    {
        delete mpShadowMatrix;
        mpShadowMatrix = NULL;
    }
END_PROFILE( "StaticPhysDSG Destroy" );
}
//========================================================================
// StaticPhysDSG::
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
void StaticPhysDSG::OnSetSimState( sim::SimState* ipSimState )
{
    tRefCounted::Assign( mpSimStateObj, ipSimState );
    
    //mpSimStateObj->mAIRefIndex = StaticPhysDSG::GetAIRef();
    mpSimStateObj->mAIRefIndex = this->GetAIRef();

    SetInternalState();
}
//========================================================================
// StaticPhysDSG::
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
sim::SimState* StaticPhysDSG::GetSimState() const
{
   return mpSimStateObj;
}
///////////////////////////////////////////////////////////////////////
// Drawable
///////////////////////////////////////////////////////////////////////
//========================================================================
// StaticPhysDSG::
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
void StaticPhysDSG::Display()
{
    if(IS_DRAW_LONG) return;
#ifdef PROFILER_ENABLED
    char profileName[] = "  StaticPhysDSG Display";
#endif
   DSG_BEGIN_PROFILE(profileName)
   //Currently unsupported. Contact Devin.
   //rAssert(false);
   //Do nothing, but allow inst stat phys to render their pGeo's
   DSG_END_PROFILE(profileName)
}
//========================================================================
// StaticPhysDSG::
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
void StaticPhysDSG::DisplayBoundingBox(tColour colour)
{
#ifndef RAD_RELEASE
   //Currently unsupported. Contact Devin.
   //rAssert(false);
   pddiPrimStream* stream = p3d::pddi->BeginPrims(NULL, PDDI_PRIM_LINESTRIP, PDDI_V_C, 5);
   stream->Colour(colour);
   stream->Coord(mBBox.low.x, mBBox.low.y, mBBox.low.z);
   stream->Colour(colour);
   stream->Coord(mBBox.high.x, mBBox.low.y, mBBox.low.z);
   stream->Colour(colour);
   stream->Coord(mBBox.high.x, mBBox.high.y, mBBox.low.z);
   stream->Colour(colour);
   stream->Coord(mBBox.low.x, mBBox.high.y, mBBox.low.z);
   stream->Colour(colour);
   stream->Coord(mBBox.low.x, mBBox.low.y, mBBox.low.z);
   p3d::pddi->EndPrims(stream);

   stream = p3d::pddi->BeginPrims(NULL, PDDI_PRIM_LINESTRIP, PDDI_V_C, 5);
   stream->Colour(colour);
   stream->Coord(mBBox.high.x, mBBox.high.y, mBBox.high.z);
   stream->Colour(colour);
   stream->Coord(mBBox.low.x, mBBox.high.y, mBBox.high.z);
   stream->Colour(colour);
   stream->Coord(mBBox.low.x, mBBox.low.y, mBBox.high.z);
   stream->Colour(colour);
   stream->Coord(mBBox.high.x, mBBox.low.y, mBBox.high.z);
   stream->Colour(colour);
   stream->Coord(mBBox.high.x, mBBox.high.y, mBBox.high.z);
   p3d::pddi->EndPrims(stream);

   stream = p3d::pddi->BeginPrims(NULL, PDDI_PRIM_LINESTRIP, PDDI_V_C, 5);
   stream->Colour(colour);
   stream->Coord(mBBox.high.x, mBBox.high.y, mBBox.high.z);
   stream->Colour(colour);
   stream->Coord(mBBox.high.x, mBBox.low.y, mBBox.high.z);
   stream->Colour(colour);
   stream->Coord(mBBox.high.x, mBBox.low.y, mBBox.low.z);
   stream->Colour(colour);
   stream->Coord(mBBox.high.x, mBBox.high.y, mBBox.low.z);
   stream->Colour(colour);
   stream->Coord(mBBox.high.x, mBBox.high.y, mBBox.high.z);
   p3d::pddi->EndPrims(stream);

   stream = p3d::pddi->BeginPrims(NULL, PDDI_PRIM_LINESTRIP, PDDI_V_C, 5);
   stream->Colour(colour);
   stream->Coord(mBBox.low.x, mBBox.high.y, mBBox.high.z);
   stream->Colour(colour);
   stream->Coord(mBBox.low.x, mBBox.low.y, mBBox.high.z);
   stream->Colour(colour);
   stream->Coord(mBBox.low.x, mBBox.low.y, mBBox.low.z);
   stream->Colour(colour);
   stream->Coord(mBBox.low.x, mBBox.high.y, mBBox.low.z);
   stream->Colour(colour);
   stream->Coord(mBBox.low.x, mBBox.high.y, mBBox.high.z);
   p3d::pddi->EndPrims(stream);
#endif
}
//========================================================================
// StaticPhysDSG::
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
void StaticPhysDSG::DisplayBoundingSphere(tColour colour)
{
   //Currently unsupported. Contact Devin.
   rAssert(false);
}
//========================================================================
// StaticPhysDSG::
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
void StaticPhysDSG::GetBoundingBox(rmt::Box3D* box)
{
   (*box) = mBBox;
}
//========================================================================
// StaticPhysDSG::
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
void StaticPhysDSG::GetBoundingSphere(rmt::Sphere* sphere)
{
   (*sphere) = mSphere;
}

///////////////////////////////////////////////////////////////////////
// IEntityDSG
///////////////////////////////////////////////////////////////////////
//========================================================================
// StaticPhysDSG::
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
rmt::Vector* StaticPhysDSG::pPosition()
{
   return &mPosn;
}
//========================================================================
// StaticPhysDSG::
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
const rmt::Vector& StaticPhysDSG::rPosition()
{
   return mPosn;
}
//========================================================================
// StaticPhysDSG::
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
void StaticPhysDSG::GetPosition( rmt::Vector* ipPosn )
{
   *ipPosn = mPosn;
}
//========================================================================
// StaticPhysDSG::
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



void StaticPhysDSG::RenderUpdate()
{
   //do Nothing
}


//************************************************************************
//
// Protected Member Functions : StaticPhysDSG 
//
//************************************************************************
void StaticPhysDSG::SetInternalState()
{
   mPosn = mpSimStateObj->GetCollisionObject()->GetCollisionVolume()->mPosition;
   
   mBBox.low   = mPosn;
   mBBox.high  = mPosn;
   mBBox.high += mpSimStateObj->GetCollisionObject()->GetCollisionVolume()->mBoxSize;
   mBBox.low  -= mpSimStateObj->GetCollisionObject()->GetCollisionVolume()->mBoxSize;

   mSphere.centre.Sub(mBBox.high,mBBox.low);
   mSphere.centre *= 0.5f;
   mSphere.centre.Add(mBBox.low);
   mSphere.radius = mpSimStateObj->GetCollisionObject()->GetCollisionVolume()->mSphereRadius;
}





//=============================================================================
// StaticPhysDSG::PreReactToCollision
//=============================================================================
// Description: Comment
//
// Parameters:  ( sim::SimState* pCollidedObj, sim::Collision& inCollision )
//
// Return:      sim
//
//=============================================================================
sim::Solving_Answer StaticPhysDSG::PreReactToCollision( sim::SimState* pCollidedObj, sim::Collision& inCollision )
{
    return sim::Solving_Continue;
}


//=============================================================================
// StaticPhysDSG::PostReactToCollision
//=============================================================================
// Description: Comment
//
// Parameters:  ( sim::SimState* pCollidedObj, sim::Collision& inCollision )
//
// Return:      sim
//
//=============================================================================
sim::Solving_Answer StaticPhysDSG::PostReactToCollision(rmt::Vector& impulse, sim::Collision& inCollision)
{
    
    // subclass-specific shit here

    // If it is a breakable object and has an assicated particle animation with it (it should)
    // play the associated particle effect, if the impulse magnitude is greater than a certain threshold

    if ( mpCollisionAttributes != NULL )
    {
        float threshold = STAT_PHYS_MASS_IMPULSE_PARTICLE_BIAS * mpCollisionAttributes->GetMass();
        if( impulse.MagnitudeSqr() > (threshold*threshold) )
        {
            if (mpCollisionAttributes->GetParticle() != ParticleEnum::eNull )
            {
                ParticleAttributes attr;
                attr.mType = mpCollisionAttributes->GetParticle();
                GetParticleManager()->Add( attr, inCollision.GetPositionA() );          
            }
        }
    }
    return CollisionEntityDSG::PostReactToCollision(impulse, inCollision);
}



void StaticPhysDSG::SetShadow( tDrawable* ipShadow )
{
#ifdef RAD_ANDROID
    // Runtime diagnostic: cap logs to avoid flooding logcat during level streaming. The tag is SHR-ShadowTrace.
    static int shadowAssignmentLogCount = 0;
    if (shadowAssignmentLogCount < 180)
    {
        const rmt::Vector& pos = rPosition();
        __android_log_print(ANDROID_LOG_INFO, "SHR-ShadowTrace",
            "SET_SHADOW[%d] drawable=%s objectPos=(%.2f,%.2f,%.2f)",
            shadowAssignmentLogCount,
            (ipShadow != NULL && ipShadow->GetName() != NULL) ? ipShadow->GetName() : "<null>",
            pos.x, pos.y, pos.z);
        ++shadowAssignmentLogCount;
    }
#endif
    tRefCounted::Assign( mpShadow, ipShadow );

    mUseToonSmallTreeShadow = false;
    mToonSmallTreeShadowCenterX = 0.0f;
    mToonSmallTreeShadowCenterZ = 0.0f;
    mToonSmallTreeShadowRadiusX = SMALL_TREE_TOON_SHADOW_FALLBACK_RADIUS_X;
    mToonSmallTreeShadowRadiusZ = SMALL_TREE_TOON_SHADOW_FALLBACK_RADIUS_Z;

    if ( ipShadow != NULL )
    {
        // Hang onto the shadow drawable
        rAssert( mpShadowMatrix == NULL );
        mpShadowMatrix = CreateShadowMatrix( rPosition() );

#ifdef RAD_ANDROID
        if ( IsSmallTreeShadowAsset( ipShadow->GetName() ) )
        {
            // Use the asset's own local bounds to preserve the artist-authored
            // footprint as closely as possible, but render it as a crisp oval.
            rmt::Box3D shadowBounds;
            ipShadow->GetBoundingBox( &shadowBounds );

            const float radiusX = ( shadowBounds.high.x - shadowBounds.low.x ) * 0.5f;
            const float localHeight = shadowBounds.high.y - shadowBounds.low.y;
            const float radiusZ = ( shadowBounds.high.z - shadowBounds.low.z ) * 0.5f;

            // tDrawable's base implementation returns a generic 0..1 box.
            // A usable shadow asset should be essentially planar in local Y.
            if ( localHeight >= 0.0f && localHeight < 0.25f &&
                 radiusX > 0.1f && radiusX < 20.0f &&
                 radiusZ > 0.1f && radiusZ < 20.0f )
            {
                mToonSmallTreeShadowCenterX = ( shadowBounds.high.x + shadowBounds.low.x ) * 0.5f;
                mToonSmallTreeShadowCenterZ = ( shadowBounds.high.z + shadowBounds.low.z ) * 0.5f;
                mToonSmallTreeShadowRadiusX = radiusX;
                mToonSmallTreeShadowRadiusZ = radiusZ;
            }

            mUseToonSmallTreeShadow = true;
#ifdef RAD_ANDROID
            __android_log_print(ANDROID_LOG_INFO, "SHR-ShadowTrace",
                "TREE_SHADOW_MATCH drawable=%s center=(%.2f,%.2f) radius=(%.2f,%.2f)",
                ipShadow->GetName(),
                mToonSmallTreeShadowCenterX, mToonSmallTreeShadowCenterZ,
                mToonSmallTreeShadowRadiusX, mToonSmallTreeShadowRadiusZ);
#endif
        }
#endif
    }
}

rmt::Matrix* StaticPhysDSG::CreateShadowMatrix( const rmt::Vector& objectPosition )
{
    rmt::Matrix* pResult;
    rmt::Matrix shadowMat;
    if ( ComputeShadowMatrix( objectPosition, &shadowMat ) )
    {
        HeapMgr()->PushHeap( GMA_LEVEL_OTHER );
        pResult = new rmt::Matrix();
        HeapMgr()->PopHeap( GMA_LEVEL_OTHER);
        *pResult = shadowMat;

    }
    else
    {
        pResult = NULL ;
    }
    return pResult;
}
    
void StaticPhysDSG::RecomputeShadowPosition( float height_radius_bias )
{
    if ( mpShadowMatrix )
    {
        rmt::Vector position;
        GetPosition( &position );
        ComputeShadowMatrix( position, mpShadowMatrix );
    }
}

void StaticPhysDSG::RecomputeShadowPositionNoIntersect( float height, const rmt::Vector& normal, float height_radius_bias, float scale )
{
    if ( mpShadowMatrix )
    {
        rmt::Vector position;
        GetPosition( &position );
        rmt::Vector shadowPosition( position.x, height, position.z );


	    mpShadowMatrix->Identity();
	    mpShadowMatrix->FillTranslate( shadowPosition );
        rmt::Vector worldRight( 1,0,0 );
        rmt::Vector forward;
        forward.CrossProduct( worldRight, normal );
	    mpShadowMatrix->FillHeading( forward, normal );
/*
        if ( height_radius_bias != 1.0f )
        {
            // scale = (objheight - groundY) * bias
            float matrixScale = 1.0f - ( position.y - shadowPosition.y ) * height_radius_bias;
            matrixScale *= scale;
            if ( matrixScale < 0.0f )
            {
                matrixScale = 0.0f;
            }
            mpShadowMatrix->FillScale( matrixScale );
        }*/
    }
}

void StaticPhysDSG::DisplaySimpleShadow()
{
#ifdef RAD_ANDROID
    static int shadowDisplayLogCount = 0;
    if (shadowDisplayLogCount < 180)
    {
        __android_log_print(ANDROID_LOG_INFO, "SHR-ShadowTrace",
            "DISPLAY_SHADOW[%d] drawable=%s toonTreeMatch=%d cel=%d matrix=%d",
            shadowDisplayLogCount,
            (mpShadow != NULL && mpShadow->GetName() != NULL) ? mpShadow->GetName() : "<null>",
            mUseToonSmallTreeShadow ? 1 : 0,
            IsCelShadingEnabled() ? 1 : 0,
            mpShadowMatrix != NULL ? 1 : 0);
        ++shadowDisplayLogCount;
    }
#endif
    p3d::pddi->SetZWrite(false);
    BEGIN_PROFILE("DisplaySimpleShadow")
	rAssert( mpShadow != NULL );

	// Translate the shadow towards the camera slightly, instead of moving it off the
	//ground in the direction of the the ground normal. Hopefully this will cause less distortion of the shadow.
	
    if ( mpShadow != NULL && mpShadowMatrix != NULL )
    {

	// Create a camera that pushes the shadow a meter towards
	// the camera
	rmt::Vector camPos;
	p3d::context->GetView()->GetCamera()->GetWorldPosition( &camPos );
	camPos.Sub( mpShadowMatrix->Row(3) );
	camPos.Normalize();
    // Distance to raise the object 
    const float Z_FIGHTING_OFFSET = 1.0f;
    camPos.Scale( Z_FIGHTING_OFFSET );
	
	// Final shadow transform = position/orientation * tocamera translation
	rmt::Matrix shadowTransform( *mpShadowMatrix );
	shadowTransform.Row( 3 ).Add( camPos );

    // Display. In this diagnostic build, the bright marker substitutes the
    // original drawable for any object using the shared small-tree shadow asset
    // while cel shading is enabled.
    p3d::stack->PushMultiply( shadowTransform );
    bool displayedToonSmallTreeShadow = false;
#ifdef RAD_ANDROID
    if ( mUseToonSmallTreeShadow && IsCelShadingEnabled() )
    {
        displayedToonSmallTreeShadow = DrawSmallTreeToonShadow(
            mToonSmallTreeShadowCenterX,
            mToonSmallTreeShadowCenterZ,
            mToonSmallTreeShadowRadiusX * 1.8f,
            mToonSmallTreeShadowRadiusZ * 1.8f );
    }
#endif
    if ( !displayedToonSmallTreeShadow )
    {
        mpShadow->Display();
    }
    p3d::stack->Pop();
    }
    else
    {
        mpShadowMatrix = CreateShadowMatrix( rPosition() );
    }
    END_PROFILE("DisplaySimpleShadow")
    p3d::pddi->SetZWrite(true);
}


//************************************************************************
//
// Private Member Functions : StaticPhysDSG 
//
//************************************************************************


bool 
StaticPhysDSG::ComputeShadowMatrix( const rmt::Vector& in_position, rmt::Matrix* out_pMatrix )
{
   	// Determine where our shadow casting object intersects the ground plane
	rmt::Vector groundNormal(0,1,0);
	rmt::Vector groundPlaneIntersectionPoint;

	const float INTERSECT_TEST_RADIUS = 10.0f;
	bool foundPlane;
	rmt::Vector deepestIntersectPos, deepestIntersectNormal;

    // Get rid of the fact that FindIntersection doesn't want a const value
	// and I'm above casting away constness

    rmt::Vector searchPosition = in_position;
    searchPosition.y += 10.0f;

	GetIntersectManager()->FindIntersection( searchPosition, 
											foundPlane,
											groundNormal,
											groundPlaneIntersectionPoint );

    if ( foundPlane )
    {
	    out_pMatrix->Identity();
	    out_pMatrix->FillTranslate( groundPlaneIntersectionPoint );
        rmt::Vector worldRight( 1,0,0 );
        rmt::Vector forward;
        forward.CrossProduct( worldRight, groundNormal );
	    out_pMatrix->FillHeading( forward, groundNormal );

    }
    return foundPlane;
    
}

