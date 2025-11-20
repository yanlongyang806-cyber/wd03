#pragma once
GCC_SYSTEM

/***************************************************************************
*     Copyright (c) 2005-Present, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#if !GAMESERVER && !GAMECLIENT
	#error No Movement code allowed here.
#endif

#include "capsule.h"

typedef struct MovementManager			MovementManager;
typedef struct MovementRequester		MovementRequester;
typedef struct MovementRequesterMsg 	MovementRequesterMsg;
typedef struct Entity					Entity;
typedef struct DynJointTuning			DynJointTuning;
typedef struct MRRagDollPart			MRRagDollPart;
typedef struct Capsule					Capsule;

// Projectile requester.

void	mrProjectileMsgHandler(const MovementRequesterMsg* msg);

S32		mrProjectileApplyFriction(Vec3 velInOut, 
								  U32 onGround);

void	mrProjectileStartWithVelocity(	MovementRequester* mr,
										Entity *e,
										const Vec3 vel,
										U32 spcStart,
										S32 instantFacePlant,
										S32 proneAtEnd,
										F32 timer,
										S32 ignoreTravelTime);

void	mrProjectileStartWithTarget(	MovementRequester* mr,
										Entity *e,
										const Vec3 target,
										U32 spcStart,
										S32 lowAngle,
										S32 targetHasFlight,
										S32 instantFacePlant,
										S32 proneAtEnd,
										F32 timer,
										S32 ignoreTravelTime);

void	mrProjectileSetNearDeath(MovementRequester *mr);

// SimBody requester.

S32		mrSimBodySetEnabled(MovementRequester* mr,
							U32 bodyIndex,
							S32 isEnabled);

// Skeleton resource.

AUTO_STRUCT;
typedef struct MMRSkeletonPart {
	const char*				boneName; AST(POOL_STRING)
	const char*				parentBoneName; AST(POOL_STRING)
} MMRSkeletonPart;

AUTO_STRUCT;
typedef struct MMRSkeletonPartNP {
	Capsule					capsule; AST(NAME(cap))
	Vec3					xyzSizeBox;
	Vec3					posBox;
	Vec3					pyrBox;
	U32						bodyIndex;
	U32						isBox	: 1;
	U32						isBody	: 1;
} MMRSkeletonPartNP;

AUTO_STRUCT;
typedef struct MMRSkeletonPartState {
	Vec3					pos;
	Vec3					pyr;
} MMRSkeletonPartState;

AUTO_STRUCT;
typedef struct MMRSkeletonConstant {
	MMRSkeletonPart**		parts;
} MMRSkeletonConstant;

AUTO_STRUCT;
typedef struct MMRSkeletonConstantNP {
	MMRSkeletonPartNP**		parts;
	Vec3					posOffsetToAnimRoot;
	Vec3					pyrOffsetToAnimRoot;
} MMRSkeletonConstantNP;

AUTO_STRUCT;
typedef struct MMRSkeletonPartStates {
	MMRSkeletonPartState**	states;
} MMRSkeletonPartStates;

S32		mmrSkeletonGetStateFG(	MovementManager* mm,
								const MMRSkeletonConstant** constantOut,
								const MMRSkeletonConstantNP** constantNPOut,
								const MMRSkeletonPartStates** partStatesOut);

// Ragdoll requester.

AUTO_STRUCT;
typedef struct MRRagDollPart {
	S32						parentIndex; AST(NAME(ParentIndex))
	Vec3					parentAnchor; AST(NAME(ParentAnchor))
	Vec3					selfAnchor; AST(NAME(SelfAnchor))
	
	Capsule					capsule; AST(NAME(capsuleUniqueName))

	Vec3					xyzSizeBox;
	Mat4					matBox;

	F32						volumeScale; NO_AST
	F32						skinWidth; NO_AST
	F32						density; AST(NAME(Density))

	const char*				boneName; AST(POOL_STRING)
	const char*				parentBoneName; AST(POOL_STRING)
	Vec3					pos;
	Quat					rot;

	Vec3					pose_pos;
	Quat					pose_rot;

	const DynJointTuning*	tuning; NO_AST
	U32						pose : 1;
	U32						isBox : 1;
} MRRagDollPart;

AUTO_STRUCT;
typedef struct MRRagDollParts {
	MRRagDollPart**			parts;
} MRRagDollParts;

S32		mrRagdollSetup(	MovementRequester* mr,
						Entity* e,
						U32 uiTime);

S32		mrRagDollSetParts(	MovementRequester* mr,
							const MRRagDollParts* parts);

void mrRagdollSetVelocity(	MovementRequester* mr,
							Entity* e,
							const Vec3 vel,
							const Vec3 angVel,
							F32 randomAngVel,
							U32 spcStart);

void	mrRagdollSetDead(	MovementRequester* mr,
							S32 dead );

void	mrRagdollSetDeathDirection(	MovementRequester *mr,
									const char *pcDirection);

S32		mrRagdollEnded( MovementRequester* mr);

