#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "dynDraw.h"


typedef struct GeoMeshTempData GeoMeshTempData;
typedef struct ClothCreationData ClothCreationData;
typedef struct DynClothCollisionSet DynClothCollisionSet;
typedef struct DynClothInfo DynClothInfo;
typedef struct DynDrawParticle DynDrawParticle;

#define CLOTH_FLAGS_CONNECTIONS(x) 		((x<=0)?1:(x>3)?3:(x))
#define CLOTH_FLAGS_CONNECTIONS_MAX 	   3
#define CLOTH_FLAGS_CONNECTIONS_MASK 	0x03
#define CLOTH_FLAGS_DIAGONAL1 			0x04
#define CLOTH_FLAGS_DIAGONAL2 			0x08

//============================================================================
// DEFINITIONS
//============================================================================

#define CLOTH_SUB_LOD_NUM 4

enum {
	CLOTH_FLAG_DEBUG_COLLISION = 1,
	CLOTH_FLAG_DEBUG_POINTS = 2,
	CLOTH_FLAG_DEBUG_IM_POINTS = 4,
	CLOTH_FLAG_DEBUG_TANGENTS = 8,
	CLOTH_FLAG_DEBUG_AND_RENDER = 16,
	CLOTH_FLAG_DEBUG_NORMALS = 32,
	CLOTH_FLAG_DEBUG_HARNESS = 64,
	CLOTH_FLAG_DEBUG = 0xff,
	CLOTH_FLAG_RIPPLE_VERTICAL = 0x100,	// Produces nice ripples for flags.
	CLOTH_FLAG_MASK = 0xffff
};

//============================================================================
// RENDER SUPPORT DEFINITIONS
//============================================================================

#define CLOTH_NORMALIZE_NORMALS 1
#define CLOTH_CALC_BINORMALS 1
#define CLOTH_NORMALIZE_BINORMALS 1
#define CLOTH_CALC_TANGENTS 1
#define CLOTH_NORMALIZE_TANGENTS 1

#define CLOTH_SCALE_NORMALS 0

//============================================================================
// PROTOTYPES
//============================================================================

// Defined in DynCloth.h
typedef struct DynCloth DynCloth;
typedef struct DynClothAttachment DynClothAttachment;
typedef struct DynClothLengthConstraint DynClothLengthConstraint;
typedef struct DynClothLOD DynClothLOD;
typedef struct DynClothObject DynClothObject;
typedef struct DynClothCollisionInfo DynClothCollisionInfo;
// Defined in DynClothCol.h
typedef struct DynClothCol DynClothCol;
// Defined in DynClothMesh.h
typedef struct DynClothMesh DynClothMesh;
typedef struct DynClothStrip DynClothStrip;

typedef struct DynClothNodeData DynClothNodeData;

//============================================================================
// CLOTH
//============================================================================

typedef struct DynClothCommonData
{
	// Data shared between main/threaded data
	//   Pointers should be to things that don't change after init
	//   Other values are copied per frame

	S32 NumParticles;
	const DynClothLengthConstraint *LengthConstraints; // Const pointer to make sure no one changes this after init

	S32 iStartOfExtraStiffnessConstraints;
	
	// ----------------------------------------
	// Attachment (Hook / Eyelet) Data
	S32 NumAttachments;
	S32 MaxAttachments; // Largest index into hooks that is used
	DynClothAttachment *Attachments;

	F32 *ApproxNormalLens;	// Used for fast normal calculation

} DynClothCommonData;

typedef struct DynClothEdge {

	int idx[2];

} DynClothEdge;

typedef struct DynClothTriangleEdgeList {
	int numDefinedEdges;
	int edges[3]; // Indexes into the edges array on DynClothRenderData.
} DynClothTriangleEdgeList;

typedef struct DynClothRenderData
{
	DynClothCommonData commonData;

	// Data used only by rendering

	// One element per Rendered Particle
	Vec3 *RenderPos;		// Buffer for positions to be used by the renderer (copied from CurPos)
	Vec2 *TexCoords; 		// Array of texture coords

	// One element per rendered particle
	Vec3 *Normals;	// Array of normals
#if CLOTH_CALC_BINORMALS
	Vec3 *BiNormals;	// Array of binormals (for bumpmapping)
#endif
#if CLOTH_CALC_TANGENTS
	Vec3 *Tangents;		// Array of tangents (for bumpmapping)
#endif
	F32 *NormalScale; // - = relative min, + = relative max, 1 / particle

	Vec3 *hookNormals; // Filled in per-frame and sent to renderer

	DynClothMesh *currentMesh;

	DynCloth *clothBackPointer;

	int numEdges;
	DynClothEdge *edges;

	DynClothTriangleEdgeList *edgesByTriangle;
	int numTriangles;

} DynClothRenderData;

typedef struct DynClothAttachmentHarness {
	U32 uiNumHooks;

	DynSoftwareSkinData *skinHooks;
	Vec3 *vHookPositions;
	Vec3 *vHookNormals;

	const DynDrawSkeleton* pDrawSkel;
	Vec3 vBasePos;
	const DynNode* pAttachmentNode;

	Mat4 xAvgMat;
	U32 uiBiggestMatIndex;
	U32 uiClothBoneIndex;

} DynClothAttachmentHarness;

struct DynCloth
{
	S32 Flags;			// Mostly debug, also RIPPLE_VERTICAL

	// ----------------------------------------
	// Constants (set on creation)
	DynClothCommonData commonData;
	F32 PointColRad;	// Collision radius of Modeled Particles
	F32 fParticleCollisionRadiusMax; // How much the speed of a particle will affect the collision radius.
	F32 fParticleCollisionMaxSpeed;
	S32 NumIterations;	// How many times to iterate

	// One element per Modeled Particle
	Vec3 *OrigPos; 			// Origional Positions
	F32 *InvMasses; 		// Array of 1/mass
	Vec3 *PosBuffer1; 		// CurPos or OldPos points to this
	Vec3 *PosBuffer2; 		// OldPos or CurPos points to this

	DynClothAttachmentHarness *pHarness;

	DynClothRenderData renderData;

	// ----------------------------------------
	// Calculated Each Frame
	Vec3 MaxPos, MinPos;
	Vec3 CenterPos, DeltaPos;

	// One element per Modeled Particle
	F32 *ColAmt; 			// Collision amount each frame

	// ----------------------------------------
	// Pointers
	Vec3 *CurPos; // Points to PosBuffer1 or PosBuffer2
	Vec3 *OldPos; // Points to PosBuffer2 or PosBuffer1

	Vec3 *skinnedPositions;
	F32 *skinnedWeights;

	// ----------------------------------------
	// Constraint Data
	S32 NumLengthConstraints;
	S32 NumConnections;
	DynClothLengthConstraint *LengthConstraintsInit; // Version only written two during initialization
	S32 *UVSplitVertices;
	// in commonData: const DynClothLengthConstraint *LengthConstraints; // Const pointer to make sure no one changes this after init

	// ----------------------------------------
	// Private data
	// in commonData: S32 SubLOD;
	S32 SkipUpdatePhysics; // set when movement during a frame is too severe
	
	// ----------------------------------------
	// Physics
	Mat4 Matrix;
	Vec4 mQuat; // From matrix
	Mat4 EntMatrix;
	Vec3 Vel;
	F32 Gravity;
	F32 Drag;
	F32 Time;
	float stiffness;
	F32 fClothBoneInfluenceExponent;

	// Wind
	Vec3 WindDir;			// [-1, 1]
	F32 WindMag;			// [ 0, 2] (normally [0, 1])

	// Used for attachment harness calculations
	S32* EyeletOrder;
	S32 NumEyelets;

	Vec3 PositionOffset;	// Offset to all verts from actual world coordinates, add this to a Vec3 to get to world space, subtract it from a Vec3 to get to DynCloth space

	F32 fWindRippleScale;
	F32 fWindRippleWaveTimeScale;
	F32 fWindRippleWavePeriodScale;

	F32 fFakeWindFromMovement;
	F32 fNormalWindFromMovement;

	F32 fGravityScale;
	F32 fTimeScale;

	Vec3 vLastRootPos;
	Vec3 vMovementSinceLastFrame;
	Vec3 vOriginalScale;	// Scale from when the cloth mesh was built.

	F32 fUvDensity;

	// Backpointer to the object that owns this, so we can pull collision volumes off of
	// it.
	DynClothObject *pClothObject;

};

// Create
extern DynCloth *dynClothCreate(int npoints);
extern DynCloth *dynClothCreateEmpty(void);
extern void dynClothCalcLengthConstraintsAndSetupTessellation(DynCloth *cloth, S32 flags, int nfracscales, F32 *in_fracscales, int ntris, const int *tris, const char *pcModelName, int lodNum);
extern void dynClothSetParticle(DynCloth *cloth, int pidx, Vec3 pos, F32 mass);
extern int  dynClothAddAttachment(DynCloth *cloth, int pidx, int hook1, int hook2, F32 frac);
extern void dynClothDelete(DynCloth * cloth);

// Build (dynClothBuild.c)
extern void dynClothBuildGrid(DynCloth *cloth, int width, int height, const Vec3 dxyz, const Vec3 origin, F32 xscale, int arc, F32 stiffness);
extern int dynClothCreateParticles(DynCloth *cloth, int npoints);
extern void dynClothSetPointsMasses(DynCloth *cloth, Vec3 *points, F32 *masses);
extern int dynClothBuildGridFromTriList(DynCloth *cloth, int npoints, const Vec3 *points, const Vec2 *texcoords, const F32 *masses, F32 mass_y_scale, F32 stiffness, int ntris, const int *tris, const Vec3 scale, const char *pcModelName, int lodNum);
extern int dynClothBuildLOD(DynCloth *newcloth, DynCloth *incloth, int lod, F32 point_y_scale, F32 stiffness);
extern int dynClothBuildAttachHarness(DynCloth *cloth, int hooknum, Vec3 *hooks);

// Physics
extern void dynClothSetGravity(DynCloth *cloth, F32 g);
extern void dynClothSetDrag(DynCloth *cloth, F32 drag);
extern void dynClothSetWind(DynCloth *cloth, Vec3 dir, F32 speed, F32 scale);
extern void dynClothSetWindRipple(DynCloth *cloth, F32 amt, F32 repeat, F32 period);

// Collisions
extern void dynClothSetColRad(
	DynCloth *cloth,
	F32 fParticleCollisionRadius,
	F32 fParticleCollisionRadiusMax,
	F32 fParticleCollisionMaxSpeed);

extern int  dynClothAddCollidable(DynClothObject *cloth);
extern DynClothCol *dynClothGetCollidable(DynClothObject *cloth, int idx);
extern DynClothCol *dynClothGetOldCollidable(DynClothObject *cloth, int idx);
extern void dynClothInterpolateCollidable(DynClothObject *cloth, int idx, DynClothCol *pCol, float a);
DynClothMesh *dynClothGetCollidableMesh(DynClothObject *cloth, int idx, Mat4 mat);
bool dynClothGetCollidableInsideVolume(DynClothObject *cloth, int idx);

// Update
extern int dynClothCheckMaxMotion(DynCloth *cloth, F32 dt, Mat4 newmat, int freeze);
extern void dynClothUpdateAttachments(DynCloth *cloth, Vec3 *hooks, F32 ffrac, int freeze);
extern void dynClothUpdatePosition(DynCloth *cloth, F32 dt, Mat4 newmat, Vec3 *hooks, int freeze, F32 fWorldMoveScale);
extern void dynClothUpdatePhysics(DynCloth *cloth, F32 dt, F32 clothRadius, bool gotoOrigPos, bool collideSkels, bool skipCollisions, int lod, bool sleepCloth, float a, float stepSize, Vec3 vScale,  const Mat4 xStiffMat, F32 *pfMaxConstraintRatio, F32 *pfMinConstraintRatio);
extern void dynClothUpdateDraw(DynClothRenderData *cloth);
extern void dynClothOffsetInternalPosition(DynCloth *cloth, const Vec3 newPositionOffset);
extern float dynClothGetAverageMotion(DynCloth *pCloth);

// Cached setup information
void dynClothClearCache(void);
void dynClothUpdateCache(float dt);

// Error Handling
extern S32  dynClothError; // 0 = no error
extern char dynClothErrorMessage[];

// Debug
DynClothObject ***dynClothGetAllClothObjects(void);

//============================================================================
// CLOTH ATTACHMENT (EYELET)
//============================================================================

struct DynClothAttachment
{
	S32 PIdx;	// Particle Idx
	S32 Hook1;	// 1st Index into Hooks
};

//============================================================================
// CLOTH LENGTH CONSTRAINT
//============================================================================

struct DynClothLengthConstraint
{
	S16 P1;
	S16 P2;
	F32 RestLength; // if < 0, -RestLength = minimum distance
	F32 InvLength; // 1/dist(P1,P2)
	F32 MassTot2; // 2 / (P1->InvMass + P2->InvMass)
};

//============================================================================
// CLOTH LOD
//============================================================================
// A DynClothLOD consists of a DynCloth structure and a mesh.

struct DynClothLOD {
	DynCloth *pCloth;
	DynClothMesh *pMesh;
};

//============================================================================
// CLOTH OBJECT
//============================================================================
// A cloth object consists of one or more DynClothLOD's and some state info

struct DynClothObject {

	REF_TO(DynClothInfo) hInfo;
	REF_TO(DynClothCollisionInfo) hColInfo;

	// LODs
	S32 iCurrentLOD;
	S32 iLastLOD;
	DynClothLOD **eaLODs;

	S32 NumCollidables;
	DynClothCol *Collidables;
	DynClothCol *OldCollidables;
	DynClothCollisionSet *pCollisionSet;

	DynDrawModel *pGeo;			// This will only point to something if the cloth is on a skeleton.
	DynDrawParticle *pParticle;	// This is for FX cloth.

	Model *pModel;

	S32 fAvgdtFactor;
	F32 fAvgdt;
	bool bTessellate;

	// This will keep track of how long it's been since the cloth last moved,
	// and stop simulating if it's been long enough.
	F32 fSleepTimer;

	//keeps track of differences in length between a constraint model and the runtime results
	F32 fMaxConstraintRatio;
	F32 fMinConstraintRatio;

	//keeps track of when the dynamic model was dropped for the static version
	bool bQueueModelReset;

	int iNumIterations;
	F32 fWorldMovementScale;
	F32 fWindSpeedScale;
};

extern DynClothObject *dynClothObjectCreate();
extern void dynClothObjectDelete(DynClothObject *obj);

extern int dynClothObjectAddLOD(DynClothObject *obj, int minsublod, int maxsublod);
extern int dynClothObjectRemoveLastLOD(DynClothObject *obj);
extern DynCloth *dynClothObjGetLODCloth(DynClothObject *obj, int lod);
extern DynClothMesh *dynClothObjGetLODMesh(DynClothObject *obj, int lod, int sublod);
extern int dynClothObjectSetLOD(DynClothObject *obj, int lod);
extern DynClothMesh * dynClothObjectGetLODMesh(DynClothObject *obj, int lod, int sublod);

extern int dynClothObjectCreateMeshes(DynClothObject *obj, Model *model);

extern void dynClothObjectSetColRad(
	DynClothObject *obj,
	F32 fParticleCollisionRadius,
	F32 fParticleCollisionRadiusMax,
	F32 fParticleCollisionMaxSpeed);

extern void dynClothObjectSetGravity(DynClothObject *obj, F32 g);
extern void dynClothObjectSetDrag(DynClothObject *obj, F32 drag);
extern void dynClothObjectSetWind(DynClothObject *obj, Vec3 dir, F32 speed, F32 maxspeed);
extern void dynClothObjectSetWindRipple(DynClothObject *obj, F32 amt, F32 repeat, F32 period);

extern int  dynClothCheckMaxMotion(DynCloth *cloth, F32 dt, Mat4 newmat, int freeze);
extern void dynClothObjectUpdatePosition(DynClothObject *clothobj, F32 dt, Mat4 newmat, Vec3 *hooks, int freeze);
extern void dynClothObjectUpdatePhysics(DynClothObject *clothobj, F32 dt, F32 clothRadius, bool gotoOrigPos, bool skipCollisions, bool collideSkels, Vec3 vScaleOverride);
extern void dynClothObjectUpdateDraw(DynClothObject *clothobj);
extern void dynClothObjectUpdate(DynClothObject *clothobj, F32 dt, F32 fForwardSpeed, Vec3 vForwardVec, Vec3 vScale, bool moving, bool mounted, bool gotoOrigPos, bool skipCollisions, bool collideSkels, Vec3 *pvWindOverride);
F32 dynClothAttachmentHarnessUpdate(DynCloth *pCloth, Vec3 vScale);
void dynClothInitRotation(DynCloth* pCloth, Quat qRot);


//////////////////////////////////////////////////////////////////////////////

// Creation helpers
DynClothAttachmentHarness* dynClothCreateAttachmentHarness(const GeoMeshTempData* pMeshTempData, const DynDrawSkeleton* pDrawSkel, const DynDrawModel* pGeo, U32 uiNumHooks, S32* puiHookIndices);
void createClothFromGeoData( ClothCreationData* pData, const GeoMeshTempData* pMeshTempData );
DynClothObject* dynClothObjectSetup(const char *pcClothInfo, const char *pcClothColInfo, Model *pModel, DynDrawModel* pGeo, DynDrawSkeleton* pDrawSkel, const DynNode *pAttachNode, Vec3 vScale, DynFx *pFx);
DynClothObject* dynClothObjectSetupFromDynDrawModel(DynDrawModel* pGeo, DynDrawSkeleton* pDrawSkel);
DynClothObject* dynClothObjectCopy(DynClothObject *pClothObject);

void dynClothInfoLoadAll(void);
void dynClothObjectResetAll(void);

//////////////////////////////////////////////////////////////////////////////

// Cloth state save/load. This lets us reset the cloth, but keep all the
// position and velocity information so it doesn't freak out every time we do
// something with it.

typedef struct DynClothObjectSavedStateLOD {

	int iNumPoints;
	Vec3 *pvPoints;
	Vec3 *pvPointsOld;

} DynClothObjectSavedStateLOD;

typedef struct DynClothObjectSavedState {

	const char *pcClothGeoName;
	int iNumLODs;
	DynClothObjectSavedStateLOD **pLODs;

} DynClothObjectSavedState;

DynClothObjectSavedState *dynClothObjectSaveState(DynClothObject *pClothObject);
void dynClothObjectRestoreState(DynClothObject *pClothObject, DynClothObjectSavedState *pState);
void dynClothObjectDestroySavedState(DynClothObjectSavedState *pState);
void dynClothObjectSaveAllStates(DynDrawSkeleton *pSkel, DynClothObjectSavedState ***peaStates);
void dynClothObjectApplyAllStates(DynDrawSkeleton *pSkel, DynClothObjectSavedState ***peaStates);

//////////////////////////////////////////////////////////////////////////////

// Global counter of all cloth operations per frame.
typedef struct {
	volatile int iCollisionTests;
	volatile int iConstraintCalculations;
	volatile int iPhysicsCalculations;
} DynClothCounters;

extern DynClothCounters g_dynClothCounters;

void dynClothResetCounters(void);

//////////////////////////////////////////////////////////////////////////////

void dynClothLockThreadData_dbg(const char *functionName);
void dynClothUnlockThreadData_dbg(const char *functionName);
#define dynClothUnlockThreadData() dynClothUnlockThreadData_dbg(__FUNCTION__)
#define dynClothLockThreadData() dynClothLockThreadData_dbg(__FUNCTION__)

void dynClothLockDebugThreadData_dbg(const char *functionName);
void dynClothUnlockDebugThreadData_dbg(const char *functionName);
#define dynClothUnlockDebugThreadData() dynClothUnlockDebugThreadData_dbg(__FUNCTION__)
#define dynClothLockDebugThreadData() dynClothLockDebugThreadData_dbg(__FUNCTION__)


