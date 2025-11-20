#pragma once
GCC_SYSTEM

#include "dynNode.h"

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynGroundRegDataLimb
{
	// HeightFixupBone : this is used to determine how close the limb is to the ground
	// EndEffector : the foot/hand/whatever should ultimately have this rotated to face the same direction when done
	const char *pcHeightFixupNode;	AST(POOL_STRING)
	const char *pcEndEffectorNode;	AST(POOL_STRING)

	// toggle to set the limb as upper or lower body
	bool bUpperBody; AST(BOOLFLAG)

	// HyperExtAxis : which axis of rotation is hyper-extension measured in
	Vec3 vHyperExtAxis;
	bool bMinimizeHyperExtension;	NO_AST
}
DynGroundRegDataLimb;
extern ParseTable parse_DynGroundRegDataLimb[];
#define TYPE_parse_DynGroundRegDataLimb DynGroundRegDataLimb

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynGroundRegData
{
	const char *pcName;		AST(KEY POOL_STRING)
	const char *pcFileName;	AST(CURRENTFILE)

	// these are the static registration bones used by the old (original system)
	const char *pcHipsNode;			AST(POOL_STRING)
	const char *pcHeightFixupNode;	AST(POOL_STRING)

	// floor delta's are defined in the base skeleton's space as the distance
	// from a limb's end effector (or height fixup bone) to the floor
	F32 fFloorDeltaNear;	AST(DEFAULT(0.2))
	F32 fFloorDeltaFar;		AST(DEFAULT(1.8))

	// these are dynamic limbs that will help "fit" the skeleton onto the floor
	DynGroundRegDataLimb **eaLimbs; AST(NAME("DynGroundRegDataLimb"))
}
DynGroundRegData;
extern ParseTable parse_DynGroundRegData[];
#define TYPE_parse_DynGroundRegData DynGroundRegData

//runtime version used by skeletons
AUTO_STRUCT;
typedef struct DynGroundRegLimb
{
	const DynGroundRegDataLimb *pStaticLimbData;

	DynNode *pHeightFixupNode;
	DynNode *pEndEffectorNode;

	//these values get modified while computing the ground registration solution
	DynTransform xPerFrame_TargetPosition;
	F32 fPerFrame_HyperExtension;
	F32 fPerFrame_RelWeight;
	F32 fPerFrame_DiffPosition;
	bool bPerFrame_IsSafe;
}
DynGroundRegLimb;
extern ParseTable parse_DynGroundRegLimb[];
#define TYPE_parse_DynGroundRegLimb DynGroundRegLimb

void dynGroundRegDataLoadAll(void);
bool dynGroundRegLimbIsSafe(DynGroundRegLimb *pLimb);