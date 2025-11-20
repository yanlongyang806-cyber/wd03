#pragma once
GCC_SYSTEM


#include "dynNode.h"
#include "dynRagdollData.h"

#define RAGDOLL_GRAVITY 80.0f

typedef struct GfxSplat GfxSplat;

typedef struct DynRagdollPartState
{
	const char* pcBoneName;
	const char* pcParentBoneName;
	Quat qBindPose;
	Quat qWorldSpace;
	Quat qLocalSpace;
	Vec3 vWorldSpace;
	eRagdollShape eShape;
	Vec3 vCenterOfGravity;
	Vec3 vBoxDimensions;
	Vec3 vCapsuleDir;
	F32 fCapsuleLength;
	F32 fCapsuleRadius;
	GfxSplat *pSplat;
} DynRagdollPartState;

typedef struct DynRagdollState
{
	DynRagdollPartState* aParts;
	bool bRagdollOn;
	U32 uiNumParts;
	Quat qRootOffset;
	Vec3 vRootOffset;
	Vec3 vHipsWorldSpace;
	F32 fBlend;
} DynRagdollState;