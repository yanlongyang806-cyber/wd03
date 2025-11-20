#pragma once
GCC_SYSTEM

typedef struct DynSkeleton DynSkeleton;
typedef struct DynBaseSkeleton DynBaseSkeleton;

typedef struct DynBouncerInfo DynBouncerInfo;
typedef struct DynNode DynNode;
typedef struct DynTransform DynTransform;
typedef struct DynScaleCollection DynScaleCollection;
typedef struct DynFxManager DynFxManager;
typedef struct DynClientSideRagdollBody DynClientSideRagdollBody;

typedef struct DynBouncerRotationalState
{
	Vec2 vPos;
	Vec2 vVel;
} DynBouncerRotationalState;

/*
typedef struct DynBouncerHingeState
{
	F32 fYaw;
	F32 fYawVel;
} DynBouncerHingeState;
*/

typedef struct DynBouncerUpdater
{
	DynBouncerInfo* pInfo;
	F32 fLODMissedTime;
	Vec3 vMassPosWS;
	Vec3 vRootPosWS;
	union {
		//DynBouncerHingeState hinge;
		DynBouncerRotationalState rotational;
	} state;
	bool bNeedsInit;
	bool bReprocessedSkeleton;
} DynBouncerUpdater;

void dynAnimPhysicsCreateObject(DynSkeleton* pSkeleton);
bool dynAnimPhysicsIsFullSimulation(DynSkeleton *pSkeleton);
void dynAnimPhysicsFreeObject(DynSkeleton* pSkeleton);

void dynAnimPhysicsCreateRagdollBody(DynSkeleton *pSkeleton, DynClientSideRagdollBody **pRagdollBody);
void dynAnimPhysicsFreeRagdollBodies(DynSkeleton *pSkeleton);

DynBouncerUpdater* dynBouncerUpdaterCreate(DynBouncerInfo* pInfo);
void dynBouncerUpdaterDestroy(DynBouncerUpdater* pUpdater);
void dynBouncerUpdaterUpdateBone(DynBouncerUpdater* pUpdater, DynNode* pBone, F32 fDeltaTime);
void dynBouncerUpdaterUpdateBoneNew(DynBouncerUpdater* pUpdater, DynNode* pBone, F32 fDeltaTime);

const char* dynCalculateHitReactDirectionBit(const Mat3 mFaceSpace, const Vec3 vDirection);

void dynAnimFindTargetTransform(
	const DynSkeleton *pSkeleton, const DynScaleCollection* pScaleCollection, const DynBaseSkeleton* pRegSkeleton, const DynNode* pHand,
	const DynNode *pIKTargetA, const DynNode *pIKTargetB, U32 uiNumIKTargets, DynTransform* pxTarget, F32 fBlend,
	bool bIKBothHands, bool bLeftHand,
	bool bIKMeleeMode, bool bEnableIKSliding
	);
bool dynAnimFixupArm(
	DynNode* pWep, const DynTransform* pxTarget, F32 fWepRegisterBlend,
	bool bIKGroundRegMode, bool bIKBothHandsMode, bool bIKMeleeMode,
	bool bDisableIKLeftWrist
	);

bool dynAnimPrintArmFixupDebugInfo(DynNode* pHand, const DynTransform* pxTarget);

bool dynAnimPhysicsRaycastToGround(int iPartitionIdx, const Vec3 vStart, F32 fRange, Vec3 vImpactPos, Vec3 vImpactNorm);