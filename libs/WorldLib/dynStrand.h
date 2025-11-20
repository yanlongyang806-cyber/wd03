#pragma once
GCC_SYSTEM

#include "dynNode.h"

AUTO_STRUCT;
typedef	struct DynStrandPoint
{	
	DynNode *pFromNode;
	DynNode *pToNode;
	U32 uiNumJoints;

	F32 fSpringK;
	F32 fDamperC;

	Vec3 vFromPos;
	Vec3 vToPos;

	Vec3 vProcPos;
	Vec3 vProcVel;

	Vec3 vPos;
	Vec3 vVel; AST(NAME(vel))
	Vec3 vAcc;
	Vec3 vForce;

	F32 fMassInv;
	F32 fRestLength;
}
DynStrandPoint;
extern ParseTable parse_DynStrandPoint[];
#define TYPE_parse_DynStrandPoint DynStrandPoint

AUTO_STRUCT;
typedef struct DynStrand
{
	DynNode *pRootNode;
	DynNode *pEndNode;
	DynNode **eaJoints;
	U32 uiNumJoints;

	Vec3 vAxis; AST(NAME(axis))
	Vec3 vAxisDuringSim;

	DynStrandPoint strongPoint;
	DynStrandPoint weakPoint;

	Vec3 vRootPos;
	Vec3 vEndPos;

	F32 fSelfSpringK;
	F32 fSelfDamperC;

	F32 fStrength;
	F32 fMaxJointAngle;
	F32 fWindResistance;
	F32 fGravity;
	F32 fTorsionRatio;

	U32 bAxisIsInWorldSpace:1;
	U32 bPrealignToProceduralAxis:1;
	U32 bHasWeakPoint:1;
	U32 bFullGroundReg:1;
	U32 bPartialGroundReg:1;
	U32 bUseEulerIntegration:1;
}
DynStrand;
extern ParseTable parse_DynStrand[];
#define TYPE_parse_DynStrand DynStrand

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynStrandData
{
	const char* pcStartNodeName;
	const char* pcBreakNodeName;
	const char* pcEndNodeName;

	Vec3 vAxis; AST(NAME(axis))

	F32 fAnimSpringK;	AST(NAME(StablizeSpringK))
	F32 fAnimDamperC;	AST(NAME(StablizeDamperC))
	F32 fSelfSpringK;	AST(NAME(DynModelSpringK))
	F32 fSelfDamperC;	AST(NAME(DynModelDamperC))

	F32 fBreakPointMass;
	F32 fEndPointMass;	

	F32 fApplicationStrength;
	F32 fMaxJointAngle;
	F32 fWindResistance;
	F32 fGravity;
	F32 fTorsionRatio;

	bool bAxisIsInWorldSpace;		AST(BOOLFLAG)
	bool bPrealignToProceduralAxis;	AST(BOOLFLAG NAME("PrealignStrandToProceduralAxis"))
	bool bFullGroundReg;			AST(BOOLFLAG)
	bool bPartialGroundReg;			AST(BOOLFLAG)
	bool bUseEulerIntegration;		AST(BOOLFLAG)
}
DynStrandData;
extern ParseTable parse_DynStrandData[];
#define TYPE_parse_DynStrandData DynStrandData

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynStrandDataSet
{
	const char*	pcName;		AST(KEY POOL_STRING)
	const char*	pcFileName;	AST(CURRENTFILE)
	DynStrandData **eaDynStrandData;
}
DynStrandDataSet;
extern ParseTable parse_DynStrandDataSet[];
#define TYPE_parse_DynStrandDataSet DynStrandDataSet


void dynStrandDataSetLoadAll(void);

void dynStrandInitStrandPoint(DynStrand *pStrand, DynStrandPoint *pStrandPoint, DynTransform *pxRoot, F32 fAdditionalLength);

void dynStrandPrealignToProceduralAxis(DynStrand *pStrand);
void dynStrandDeform(DynStrand *pStrand, F32 fDeltaTime);

void dynStrandDebugRenderStrandPoint(DynStrandPoint *pStrandPoint, Vec3 vParentPos, Vec3 vRootPos);
void dynStrandDebugRenderStrand(DynStrand *pStrand, U32 uiColor);

void dynStrandGroundRegStrandFull(DynStrand *pStrand, Vec3 vBasePos);
void dynStrandGroundRegStrandQuick(DynStrand *pStrand, Vec3 vBasePos);