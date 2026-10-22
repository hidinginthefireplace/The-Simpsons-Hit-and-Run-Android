/*===========================================================================
    poseanimation.cpp
    05-Jan-00 Created by Neall

    Copyright (c)2000 Radical Entertainment, Inc.
    All rights reserved.
===========================================================================*/

#include <p3d/anim/poseanimation.hpp>
#include <p3d/anim/channel.hpp>
#include <p3d/anim/pose.hpp>
#include <p3d/anim/skeleton.hpp>
#include <p3d/utility.hpp>
#include <radmath/radmath.hpp>

#include <string.h>

using namespace RadicalMathLibrary;

// In Level 1's PTRN_animsigns setup, gas04 is the sign joint associated
// with the faulty moving sign. Keep its translation at the authored position,
// but allow its rotation track and the neighboring gas03 sign to animate.
static bool IsL1Gas04SignJoint(const tAnimation* animation, const tPose::Joint* joint)
{
    return animation != NULL &&
           joint != NULL &&
           animation->GetUID() == tEntity::MakeUID("PTRN_animsigns") &&
           joint->uid == tEntity::MakeUID("gas04");
}

//*********************************************************
// Class tPoseAnimationController
//*********************************************************
tPoseAnimationController::tPoseAnimationController() : 
    tBlendFrameController(),
    pose(NULL)
{
}

//---------------------------------------------------------
tPoseAnimationController::tPoseAnimationController(tPoseAnimationController* c) :
    tBlendFrameController(c)
{
    pose = new tPose(c->GetPose()->GetSkeleton());
    pose->AddRef();
}

//---------------------------------------------------------
tPoseAnimationController::~tPoseAnimationController()
{
    tRefCounted::Release(pose);
}

//---------------------------------------------------------
tFrameController* tPoseAnimationController::Clone(void)
{
    return new tPoseAnimationController(this);
}

//---------------------------------------------------------
void tPoseAnimationController::SetPose(tPose* p)
{
    P3DASSERT(p);
    tRefCounted::Assign(pose, p);
}

//---------------------------------------------------------
bool tPoseAnimationController::ValidateAnimation(tAnimation* anim)
{
    return (anim->GetAnimationType()==Pure3DAnimationTypes::POSE_TRANSFORM_PTRN);
}

//---------------------------------------------------------
void tPoseAnimationController::UpdateNoBlending()
{
    tAnimation* anim = playInfo[0].GetAnimation();

    float frame = playInfo[0].GetCurrentFrame();

    int nJoint = pose->GetNumJoint();
    for(int i=0;i < nJoint; i++)
    {
        tPose::Joint* joint = pose->GetJoint(i);
        P3DASSERT(joint);

        const tAnimationGroup* animGroup = anim->GetGroupByUID(joint->uid);

        const tVectorChannel* translation = NULL;
        const tRotationChannel* rotation = NULL;

        if(animGroup)
        {
            translation = animGroup->GetVectorChannel(Pure3DAnimationChannels::Pose::TRANSLATION_TRAN);
            rotation = animGroup->GetRotationChannel(Pure3DAnimationChannels::Pose::ROTATION_ROT);
        }

        tSkeleton::Joint* skeletonJoint = pose->GetSkeleton()->GetJoint(i);

        if (IsL1Gas04SignJoint(anim, joint))
        {
            // Freeze the complete transform for the faulty sign only. This
            // also prevents its animated rotation from moving it off the pole.
            joint->objectMatrix = skeletonJoint->restPose;
            continue;
        }

        if(skeletonJoint->preserveBoneLengths)
        {
            // Stuff the bonelengths into the pose
            joint->objectMatrix.FillTranslate( skeletonJoint->restPose.Row(3) );
        }
        else if(translation)
        {
            // Animate the translation channel
            translation->GetValue(frame, &joint->objectMatrix.Row(3));
        }
        else
        {
            /************************  Start HC mod **************************/

            // Stuff the bone lengths into the pose.
            joint->objectMatrix.FillTranslate( skeletonJoint->restPose.Row(3) );


            // This new logic allows us to have translation animations on secondary bones.
            // Example run-through for character-specific animation:
            //                1.  core skeleton bones will be driven by the animation if applicable, except for shoulder joints (which have "preserveBoneLengths")
            //                2.  secondary bones will be driven by the animation if applicable.  If there is a translation animation, great!  use it!  If not, just use the skeleton values.
            // Example run-through for core skeleton animation (e.g. grapple receive):
            //                1.  core skeleton bones will be driven by the animation if applicable, except for shoulder joints (which have "preserveBoneLengths")
            //                2.  secondary bones don't have "preserveBoneLengths" and don't have animations channels, ==> use the skeleton's bone lengths.  Easy!


            // Requirements on art:    -  place "preserveBoneLengths" on bones in core skeleton that should never be overridden
            //                            by translation animations.  Example:  ShoulderLeft and ShoulderRight
            //                         -  secondary bones do NOT have "preserveBoneLengths".

            /************************   End  HC mod **************************/
        }
        
        if(rotation)
        {        
            // Animate the rotation channel
            rotation->GetMatrix(frame, &joint->objectMatrix);
        }
        else
        {
            joint->objectMatrix = skeletonJoint->restPose;
        }
    }
    pose->SetPoseReady(false);
}

//---------------------------------------------------------
void tPoseAnimationController::UpdateWithBlending()
{
    int i;
    int nJoint = pose->GetNumJoint();

    // translation
    for(i=0; i < nJoint; i++)
    {
        tPose::Joint* joint = pose->GetJoint(i);
        P3DASSERT(joint);

        // Simpsons Hack
        // Ask the skeleton if it needs to preserve it's bonelength
        // to allow for shared animations across different Hierarchies.
        tSkeleton::Joint* skeletonJoint = pose->GetSkeleton()->GetJoint(i);

        // During blending, initialize gas04 to its full rest transform
        // whenever PTRN_animsigns is active. The rotation pass below also
        // skips this joint so neither part of its transform can be animated.
        bool freezeGas04Transform = false;
        for (int activeIndex = 0; activeIndex < nAnim; ++activeIndex)
        {
            if (IsL1Gas04SignJoint(playInfo[activeIndex].GetAnimation(), joint))
            {
                freezeGas04Transform = true;
                break;
            }
        }
        if (freezeGas04Transform)
        {
            joint->objectMatrix = skeletonJoint->restPose;
            continue;
        }

        if(skeletonJoint->preserveBoneLengths)
        {
            // Stuff the bonelengths into the pose
            joint->objectMatrix.FillTranslate( skeletonJoint->restPose.Row(3) );
        }
        else
        {        
            Vector vBlend(0.0f, 0.0f, 0.0f);  // accumulation buffer
            
            // prime the accumulation buffer by evaluating the first animation in the buffer
            tAnimation *anim = playInfo[0].GetAnimation();
            const tAnimationGroup* animGroup = anim->GetGroupByUID(joint->uid);
            const tVectorChannel* translation = NULL;

            if(animGroup)
            {
                translation = animGroup->GetVectorChannel(Pure3DAnimationChannels::Pose::TRANSLATION_TRAN);
            }

            if(translation)
            {
                translation->GetValue(playInfo[0].GetCurrentFrame(), &vBlend);
            }
            else
            {
                /************************  Start HC mod **************************/
                // Mod by KMC.  See UpdateNoBlending() for comments on what I'm doing and why.

                // Stuff the bone length into the pose
                vBlend = skeletonJoint->restPose.Row( 3 );
                /************************   End  HC mod **************************/
            }

            // blend in remaining animations using the blend weight
            for(int j=1; j < nAnim; j++)
            {
                anim = playInfo[j].GetAnimation();
                animGroup = anim->GetGroupByUID(joint->uid);
                translation = NULL;
                if(animGroup)
                {
                    translation = animGroup->GetVectorChannel(Pure3DAnimationChannels::Pose::TRANSLATION_TRAN);
                }

                if(translation)
                {
                    Vector v;
                    vBlend.Scale(1.0f - playInfo[j].GetWeight());
                    translation->GetValue(playInfo[j].GetCurrentFrame(), &v);
                    vBlend.ScaleAdd(playInfo[j].GetWeight(), v);
                }
            }
             
            // stuff the resulting blended translation vector into the pose
            joint->objectMatrix.Row(3) = vBlend;
        }
    }

    // rotation
    for(i=0; i < nJoint; i++)
    {
        tPose::Joint* joint = pose->GetJoint(i);
        P3DASSERT(joint);

        bool freezeGas04Rotation = false;
        for (int activeIndex = 0; activeIndex < nAnim; ++activeIndex)
        {
            if (IsL1Gas04SignJoint(playInfo[activeIndex].GetAnimation(), joint))
            {
                freezeGas04Rotation = true;
                break;
            }
        }
        if (freezeGas04Rotation)
        {
            // The translation pass already set the full rest transform.
            // Leave it untouched so the sign remains at its authored pose.
            continue;
        }

        Quaternion qBlend(0.0f, 0.0f, 0.0f, 1.0f);

        // Prime the accumulation buffer by evaluating the first animation in the buffer.  
        tAnimation *anim = playInfo[0].GetAnimation();      
        const tAnimationGroup* animGroup = anim->GetGroupByUID(joint->uid);
        const tRotationChannel* rotation = NULL;

        if(animGroup)
        {
            rotation = animGroup->GetRotationChannel(Pure3DAnimationChannels::Pose::ROTATION_ROT);
        }

        if(rotation)
        {
            rotation->GetQuaternion(playInfo[0].GetCurrentFrame(), &qBlend);
        }
        else
        {
            // Make sure to blend to the rest pose if there isn't any animation
            tSkeleton::Joint* skeletonJoint = pose->GetSkeleton()->GetJoint(i);
            qBlend.BuildFromMatrix(skeletonJoint->restPose);          
        }

        // blend in remaining animations using the blend weight
        for(int j=1; j < nAnim; j++)
        {
            anim = playInfo[j].GetAnimation();
            animGroup = anim->GetGroupByUID(joint->uid);
            rotation = NULL;
            if(animGroup)
            {
                rotation = animGroup->GetRotationChannel(Pure3DAnimationChannels::Pose::ROTATION_ROT);
            }

            if(rotation)
            {
                Quaternion q;
                rotation->GetQuaternion(playInfo[j].GetCurrentFrame(), &q);
                qBlend.Slerp(q, playInfo[j].GetWeight());
            }
        }
        // stuff the blended rotation quaternion into the pose
        qBlend.ConvertToMatrix(&(joint->objectMatrix));
    }

    pose->SetPoseReady(false);
}
