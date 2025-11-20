#pragma once
GCC_SYSTEM
#if !SPU
#include "dynFxManager.h"
#include "dynRagdollData.h"
//#include "dynRagdollData_h_ast.h"

typedef struct DynParticle DynParticle;
typedef struct DynFx DynFx;
typedef struct DynNode DynNode;
typedef struct DynPhysicsObject DynPhysicsObject;
typedef struct WorldCollActor WorldCollActor;
typedef struct PSDKActor PSDKActor;
typedef struct DynClientSideRagdollBody DynClientSideRagdollBody;
#endif //!SPU

AUTO_ENUM;
typedef enum eDynForceType
{
	eDynForceType_None, ENAMES(None)
	eDynForceType_Out, ENAMES(Out)
	eDynForceType_Swirl, ENAMES(Swirl)
	eDynForceType_Up, ENAMES(Up)
	eDynForceType_Side, ENAMES(Side)
	eDynForceType_Forward, ENAMES(Forward)
	//	eDynForceType_Drag, ENAMES(Drag)
} eDynForceType;

AUTO_ENUM;
typedef enum eDynForceFalloff
{
	eDynForceFallOff_Linear, ENAMES(Linear)
	eDynForceFallOff_None, ENAMES(None)
} eDynForceFalloff;


typedef enum eDynPhysType
{
	edpt_Normal,
	edpt_Large,
	edpt_VeryLarge,
	edpt_Off,
} eDynPhysType;

AUTO_STRUCT;
typedef struct DynContactPair
{
	void* pUID; NO_AST
	bool bTouching;
	F32 fContactForceTotal;
	bool bContactPoint;
	Vec3 vContactPos;
	Vec3 vContactNorm;
} DynContactPair;

AUTO_STRUCT;
typedef struct DynFxPhysicsUpdateInfo
{
	Vec3 vAccel;
	F32 fDrag;
	bool bAccel; AST(NAME(boolAccel))
	bool bNeedsWakeup;
	DynContactPair** eaContactPairs;
} DynFxPhysicsUpdateInfo;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynForce
{
	eDynForceType eForceType; AST(NAME(Type))
		eDynForceFalloff eForceFallOff; AST(NAME(FallOff))
		F32 fForceRadius; AST(NAME(Radius))
		F32 fForcePower; AST(NAME(Power))
		bool bImpulse; AST(BOOLFLAG)
		char*				pcTag; AST(POOL_STRING STRUCTPARAM NAME("Name"))
} DynForce;

typedef struct DynForcePayload
{
	DynForce force;
	Vec3 vForcePos;
	Quat qOrientation;
} DynForcePayload;

typedef enum eJointAxis eJointAxis;

#if !SPU
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynFxPhysicsInfo
{
	F32 fRadius;			AST(DEFAULT(-1))		
	F32 fSFriction;			AST(DEFAULT(-1))
	F32 fDFriction;			AST(DEFAULT(-1))
	F32 fDensity;			AST(DEFAULT(-1))
	F32 fRestitution;		AST(DEFAULT(-1))
	F32 fForceImmunity;
	eDynPhysType eType;		AST(SUBTABLE(ParseDynPhysType) DEFAULT(edpt_Normal))
	const char* pcPhysicalProperty; AST(POOL_STRING)
	bool bDebris;			AST(BOOLFLAG)
	bool bStartAsleep;		AST(BOOLFLAG)
	bool bNoCollide;

	// Joint stuff
	bool bPhysicsAttached;	AST(BOOLFLAG)
	bool bKinematicActor;	AST(BOOLFLAG)
	Vec3 vParentAttachPoint;
	Vec3 vChildAttachPoint;
	eJointAxis jointAxis;
	bool bRevoluteJoint;	AST(BOOLFLAG)

	bool bLimitJoint;		AST(BOOLFLAG)
	float fJointMin;
	float fJointMax;

	float fJointSpringiness;

} DynFxPhysicsInfo;


typedef struct DynContactUpdater
{
	void* pUID; // this is a unique pointer we use to identify this updater. do not deref it!
	U32 uiEventActive;
	DynSoundUpdater			soundUpdater;
	DynNode contactNode;
} DynContactUpdater;

typedef enum DynPhysicsObjectType {
	DPO_FX,
	DPO_SKEL,
	DPO_RAGDOLLBODY,
} DynPhysicsObjectType;

typedef struct DynPhysicsObject {
	DynPhysicsObjectType		dpoType;
	
	WorldCollActor*				wcActor;
	Mat4						mat;
	
	DynFxPhysicsUpdateInfo		updateInfo[2];

	DynContactUpdater**			eaContactUpdaters;

	U32							uiFiredOnce;


	U32							bStartAsleep:1;
	U32							bActive:1;
	U32							bInitialVelocitySet:1;

	Vec3						vInitialVelocity;
	Vec3						vInitialAngularVelocity;
	
	union {
		struct {
			DynFx*				pFx;
		} fx;
		
		struct {
			DynSkeleton*		pSkeleton;
		} skel;

		struct {
			DynClientSideRagdollBody*	pBody;
		} body;
	};
} DynPhysicsObject;


typedef struct DynClientSideRagdollBody
{
	const char *pcBone;
	const char *pcParentBone;
	const char *pcJointTargetBone;
	const char *pcPhysicalProperties;
	DynPhysicsObject *pParentDPO;
	DynJointTuning *pTuning;
	Vec3 vJointAxis;
	eRagdollShape eShape;
	Vec3 vSelfAnchor;
	Vec3 vParentAnchor;
	S32 iParentIndex;

	Mat4 mEntityWS;
	Vec3 vBasePos;
	Vec3 vPosePos;
	Vec3 vWorldSpace;
	Quat qBaseRot;
	Quat qPoseRot;
	Quat qWorldSpace;
	Quat qLocalSpace;
	Quat qBindPose;
	F32 fVolumeMassScale;
	F32 fSkinWidth;
	F32 fDensity;

	Vec3 vAdditionalGravity;
	Vec3 vCenterOfGravity;
	Vec3 vInitVel;
	Vec3 vInitAngVel;

	Vec3 vBoxDimensions;
	Mat4 mBox;

	Vec3 vCapsuleStart;
	Vec3 vCapsuleDir;
	F32 fCapsuleLength;
	F32 fCapsuleRadius;

	Vec3 pyrOffsetToAnimRoot;
	Vec3 posOffsetToAnimRoot;

	F32 fAngDamp;

	U32 uiNumChildren;

	U32 uiTorsoBone : 1;
	U32 uiUseCustomJointAxis : 1;
	U32 uiCollisionTester : 1;
	U32 uiTesterCollided : 1;
	U32 uiSleeping : 1;

} DynClientSideRagdollBody;

void dynFxCreateDPO(DynFx* pFx);
void initDynFxPhysics(void);
bool dynFxPhysicsUpdate(DynParticle* pParticle, F32 fDeltaTime);
void dynFxDestroyPhysics(DynParticle* pParticle);
bool dynFxRaycast(int iPartitionIdx, DynFx* pFx, const DynNode* pOriginNode, F32 fRange, DynNode* pResultNode, bool bOrientToNormal, bool bUseParentRotation, const char** ppcPhysProp, bool bForceRayDown, bool bCopyScale, eDynRaycastFilter eFilter);
void dynFxForceUpdate(const DynNode* pNode, DynFxRegion* pFxRegion, const DynForce* pForce, DynFxTime uiDeltaTime);
void dynFxApplyForce(const DynForce* pForce, const Vec3 vForcePos, const DynNode* pFxNode, DynPhysicsObject* pDPO, F32 fDensity, F32 fDeltaTime, Quat qOrientation);
bool dynFxGetForceEffect(const DynForce* pForce, const Vec3 vForcePos, const Vec3 vSamplePos, F32 fDensity, F32 fDeltaTime, Vec3 vEffectOut, Quat qOrientation);
void dynPhysicsObjectCreate(DynPhysicsObject** dpoOut);
void dynFxPhysicsContactQueue(PSDKActor* pActor, void* pUID, bool bTouching, F32 fContactForceTotal, bool bContactPoint, const Vec3 vContactPos, const Vec3 vContactNorm);
void dynFxPhysicsDrawScene(void);

#endif //!SPU
