#pragma once
GCC_SYSTEM

AUTO_ENUM;
typedef enum eJointType
{
	eJointType_None, ENAMES(None)
	eJointType_D6, ENAMES(D6)
	eJointType_Spherical, ENAMES(Spherical)
	eJointType_Revolute, ENAMES(Revolute)
} eJointType;

extern StaticDefineInt eJointAxisEnum[];
AUTO_ENUM;
typedef enum eJointAxis
{
	eJointAxis_X, ENAMES(X)
	eJointAxis_Y, ENAMES(Y)
	eJointAxis_Z, ENAMES(Z)
} eJointAxis;

AUTO_STRUCT;
typedef struct DynJointLimit
{
	F32 value;
	F32 hardness;
	F32 restitution;
} DynJointLimit;

AUTO_STRUCT;
typedef struct DynJointSpring
{
	F32 spring;
	F32 damper;
} DynJointSpring;

AUTO_STRUCT;
typedef struct DynJointTuning {
	eJointType jointType;
	eJointAxis axis;

	DynJointSpring spring;
	DynJointLimit limitLow;
	DynJointLimit limitHigh;

	DynJointSpring swingSpring;
	DynJointLimit swingLimit;

	U32 springEnabled : 1;
	U32 limitEnabled : 1;

	U32 swingSpringEnabled : 1;
	U32 swingLimitEnabled : 1;
} DynJointTuning;

extern StaticDefineInt eRagdollShapeEnum[];
AUTO_ENUM;
typedef enum eRagdollShape
{
	eRagdollShape_Capsule, ENAMES(Capsule) 
	eRagdollShape_Box, ENAMES(Box)
} eRagdollShape;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynRagdollShape
{
	const char* pcBone; AST(STRUCTPARAM POOL_STRING)
	const char* pcParentBone; AST(NAME(Parent) POOL_STRING)
	const char* pcJointTargetBone; AST(NAME(JointTarget) POOL_STRING)
	eRagdollShape eShape; AST(NAME(Shape))
	Vec3 vOffset;
	Quat qRotation; AST(NAME(Rotation))
	Vec3 vMin;
	Vec3 vMax;
	F32 fHeightMin;
	F32 fHeightMax;
	F32 fRadius;
	F32 fDensity;
	bool bUseCustomJointAxis; AST(BOOLFLAG)

	// Joint info
	DynJointTuning tuning; AST(EMBEDDED_FLAT)

	bool bTorsoBone;  NO_AST
	int iParentIndex; NO_AST
	int iNumChildren; NO_AST

} DynRagdollShape;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynRagdollData
{
	const char*		pcName;					AST(KEY POOL_STRING)
	const char*		pcFileName;				AST(CURRENTFILE)
	const char*		pcPhysicalProperties;
	DynRagdollShape** eaShapes;				AST(NAME(Bone))
	char*			pcPoseAnimTrack;		AST(POOL_STRING)
	F32*			eaPoseFrames;
} DynRagdollData;

void dynRagdollDataLoadAll(void);