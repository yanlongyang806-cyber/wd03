#pragma once
GCC_SYSTEM

#include "referencesystem.h"

typedef struct DynNode DynNode;
typedef struct DynCloth DynCloth;
typedef struct DynClothObject DynClothObject;
typedef struct DynSkeleton DynSkeleton;

AUTO_ENUM;
typedef enum CLOTH_COLTYPE
{
	CLOTH_COL_NONE, 
	CLOTH_COL_SPHERE, ENAMES(Sphere)
	CLOTH_COL_PLANE, ENAMES(Plane)
	CLOTH_COL_CYLINDER, ENAMES(Cylinder)
	CLOTH_COL_BALLOON, ENAMES(Baloon)
	CLOTH_COL_BOX, ENAMES(Box)
	CLOTH_COL_SKIP = 0x1000
} CLOTH_COLTYPE;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynClothCollisionShape
{
	CLOTH_COLTYPE type; AST(STRUCTPARAM)
	const char* pcBone;
	Vec3 vOffset;
	Vec3 vDirection;
	Vec3 vPoint1;
	Vec3 vPoint2;
	F32 fRadius;
	F32 fExten1;
	F32 fExten2;
	bool bMoving; AST(BOOLFLAG)
	bool bMovingBackwards; AST(BOOLFLAG)
	bool bMountedOnly; AST(BOOLFLAG)
	bool bWalkingOnly; AST(BOOLFLAG)
	bool bInsideVolume; AST(BOOLFLAG)
	bool bPushToSkinnedPos; AST(BOOLFLAG)
} DynClothCollisionShape;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynClothCollisionInfo
{
	const char* pcInfoName; AST(KEY POOL_STRING)
	const char*		pcFileName;				AST(CURRENTFILE)
	DynClothCollisionShape** eaShape;
} DynClothCollisionInfo;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynClothInfo
{
	const char* pcInfoName; AST(KEY POOL_STRING)
	const char*		pcFileName;				AST(CURRENTFILE)
	F32 fStiffness; AST(DEF(0.9f))
	F32 fDrag; AST(DEF(0.1f))
	F32 fMinWeight; AST(DEF(0.1f))
	F32 fMaxWeight; AST(DEF(0.9f))
	bool bTessellate;
	bool bAllowExtraStiffness;

	F32 fWindRippleScale; AST(DEF(1.0f))
	F32 fWindRippleWaveTimeScale; AST(DEF(1.0f))
	F32 fWindRippleWavePeriodScale; AST(DEF(1.0f))
	F32 fWindSpeedScale; AST(DEF(1.0f))

	F32 fFakeWindFromMovement;   AST(DEF(0))
	F32 fNormalWindFromMovement; AST(DEF(0))

	F32 fGravityScale; AST(DEF(1.0f))
	F32 fTimeScale; AST(DEF(1.0f))

	int iNumIterations; AST(DEF(1))
	F32 fWorldMovementScale; AST(DEF(1.0f))

	F32 fClothBoneInfluenceExponent; AST(DEF(2.0))

	F32 fParticleCollisionRadius; AST(DEF(0.2))
	F32 fParticleCollisionRadiusMax; AST(DEF(0.2))
	F32 fParticleCollisionMaxSpeed; AST(DEF(0.2))

} DynClothInfo;






typedef struct DynClothCollisionPiece
{
	DynClothCollisionShape* pShape;
	DynNode *pNode;
	S32 iIndex;
} DynClothCollisionPiece;

typedef struct DynClothCollisionSet
{
	DynClothCollisionPiece** eaPieces;
} DynClothCollisionSet;

typedef struct DynFx DynFx;

void dynClothCollisionInfoLoadAll(void);
void dynClothCollisionSetUpdate(DynClothCollisionSet* pSet, DynClothObject* pClothObject, bool bMoving, bool bMovingBackwards, bool bMounted);
void dynClothCollisionSetDestroy( DynClothCollisionSet* pSet );
DynClothCollisionSet* dynClothCollisionSetCreate( DynClothCollisionInfo* pColInfo, DynClothObject* pClothObject, DynSkeleton* pSkeleton, const DynNode *pBoneOverride, DynFx *pFx);
void dynClothCollisionSetAppend(DynClothCollisionSet *pSet, DynClothCollisionInfo *pColInfo, DynClothObject *pClothObject, DynSkeleton *pMountSkeleton);


