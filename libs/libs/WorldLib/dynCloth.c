#include "error.h"
#include "Quat.h"

#include "ScratchStack.h"
#include "dynClothInfo.h"
#include "DynCloth.h"
#include "dynClothPrivate.h"
#include "dynClothCollide.h"
#include "dynWind.h"
#include "timing_profiler.h"
#include "dynClothMesh.h"
#include "wlState.h"
#include "wlTime.h"
#include "dynSkeleton.h"
#include "WorldColl.h"
#include "wlCostume.h"
#include "dynRagdollData.h"
#include "dynNodeInline.h"
#include "dynFxManager.h"
#include "winutil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

//============================================================================
// Stuff for debugging dynClothCheckMaxMotion()
//============================================================================

#define CLOTH_DEBUG 0

#if CLOTH_DEBUG
static F32 dynClothDebugMaxDist = 15.0f;
static int dynClothDebugError = 0;
#define CLOTH_DEBUG_DPOS 1
# if CLOTH_DEBUG_DPOS
static F32 dynClothDebugMaxDpos = 3.0f;
# endif
#endif

//============================================================================
// ERROR HANDLING
//============================================================================

S32  dynClothError = 0; // 0 = no error
char dynClothErrorMessage[1024] = "";

#undef dynClothErrorf
int dynClothErrorf(char const *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsprintf(dynClothErrorMessage, fmt, ap);
	va_end(ap);

	Errorf("%s", dynClothErrorMessage);
	
	return -1;
}

//============================================================================
// TUNING PARAMATERS
//============================================================================

// This determines what order the particles are constrained in.
// The particles constrained first have the most 'stretch'
// TOP_FIRST == 1 seems to produce the least collision poke-through
#define CONSTRAIN_TOP_FIRST 1
// ColAmt is used to give weight to particles that collided,
// reducint poke-through artifacts.
#define USE_COLAMT   1

// These limit the motion of the cloth.
// Each #def matches a variable which determines the limit in units/second.
#define USE_MAXPDPOS 1
#define USE_MAXDPOS  1
#define USE_MAXDANG  1
#define USE_MAXACCEL 1

#define cTPF 30.f // per frame -> per second
F32 MAXPDPOS = 2.5f*cTPF;	// Max Particle dpos / sec
F32 MAXDPOS =  0.5f*cTPF;	// Max Body dpos / sec
F32 MAXDANG = (RAD(6.0f))*cTPF;
F32 MAXACCEL = 2.5f*cTPF;	// Max Delta Velocity / sec

// These limits are per-frame, regardless of frame-rate.
// If the motion exceeds these limits, the cloth is not animated, just
// transformed to the new location.
#define MAX_FRAME_DANG_COS 0.707f
//#define MAX_FRAME_DPOS 15.f
#define MAX_FRAME_DPOS 30.f // roughly terminal velocity at 10fps falling off tallest builing in AP

// Misc Constants
#define MAX_DRAG 0.5f	// Maximum drag amount

//============================================================================
// QUATERNION SUPPORT
// These routines are not super-optimized, but are only used per-cloth,
// not per-particle.
// Defined as statics here so that they can be inlined by the compiler
//============================================================================

#define dotVec4(v1,v2)	((v1)[0]*(v2)[0] + (v1)[1]*(v2)[1] + (v1)[2]*(v2)[2] + (v1)[3]*(v2)[3])

//#define EPSILON 0.00001f
F32 EPSILON = 0.0000001f;
#define NEARZERO(x) (fabs(x) < EPSILON)
#define QUAT_TRACE_QZERO_TOLERANCE		0.1

static void NormalizeQuat(Vec4 q)
{
	F32 Nq;
	Nq = q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3];
	if (Nq > EPSILON)
	{
		F32 s = 1.0f / sqrtf(Nq);
		scaleVec4(q, s, q);
	}
	else
	{
		zeroVec3(q);
		q[3] = 1.0f;
	}
	// Keep quaternions "right-side-up"
	if (q[1] < 0.0f)
		scaleVec4(q, -1.0f, q);
}

// q is assumed to be normalized
static void CreateQuatMat(Mat3 mat, Vec4 q)
{
    F32 s;
    F32 xs,	ys,	zs;

	//F32 Nq;
	//Nq = q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3];
	//s = (Nq > 0.0f) ? (2.0f / Nq) : 0.0f;
	s = 2.0f;
	
    xs = q[0]*s;
    ys = q[1]*s;
    zs = q[2]*s;
	
    {
		F32 xx = q[0]*xs;
		F32 yy = q[1]*ys;
		F32 zz = q[2]*zs;
		mat[0][0]= 1.0 - (yy + zz);
		mat[1][1]= 1.0 - (xx + zz);
		mat[2][2]= 1.0 - (xx + yy);
    }
    {
		F32 xy = q[0]*ys;
		F32 wz = q[3]*zs;
		mat[1][0]= (xy + wz);
		mat[0][1]= (xy - wz);
    }
    {
		F32 yz = q[1]*zs;
		F32 wx = q[3]*xs;
		mat[2][1]= (yz + wx);
		mat[1][2]= (yz - wx);
    }
    {
		F32 xz = q[0]*zs;
		F32 wy = q[3]*ys;
		mat[2][0]= xz - wy;
		mat[0][2]= xz + wy;
    }
}

// I tried several different quaternion extraction functions.
// This one is much more reliable than others I tried, which
// is critical since quaternions between potentially arbitrary
// matrices is being performed.
static void ExtractQuat(Mat3 mat, Vec4 q)
{
	F32 tr,s,inv_s;
	F32 X,Y,Z,W;

	tr = mat[0][0] + mat[1][1] + mat[2][2];
	if (tr > 0.0f)
	{
		s = sqrtf(tr + 1.0f);
		W = s * 0.5f;
		inv_s = 0.5f / s;

		X = (mat[2][1] - mat[1][2]) * inv_s;
		Y = (mat[0][2] - mat[2][0]) * inv_s;
		Z = (mat[1][0] - mat[0][1]) * inv_s;
	}
	else
	{
		S32 i, biggest;
		if (mat[2][2] > mat[0][0] && mat[2][2] > mat[1][1])
			biggest = 2;
		else if (mat[0][0] > mat[1][1])
			biggest = 0;
		else
			biggest = 1;

		inv_s = 0.0f; // invalid state
		for (i=0; i<3; i++)
		{
			if (biggest == 0)
			{
				s = sqrtf(mat[0][0] - (mat[1][1] + mat[2][2]) + 1.0f);
				if (s > QUAT_TRACE_QZERO_TOLERANCE)
				{
					X = s * 0.5f;
					inv_s = 0.5f / s;
					W = (mat[2][1] - mat[1][2]) * inv_s;
					Y = (mat[0][1] + mat[1][0]) * inv_s;
					Z = (mat[0][2] + mat[2][0]) * inv_s;
					break;
				}
			}
			else if (biggest == 1)
			{
				s = sqrtf(mat[1][1] - (mat[2][2] + mat[0][0]) + 1.0f);
				if (s > QUAT_TRACE_QZERO_TOLERANCE)
				{
					Y = s * 0.5f;
					inv_s = 0.5f / s;
					W = (mat[0][2] - mat[2][0]) * inv_s;
					Z = (mat[1][2] + mat[2][1]) * inv_s;
					X = (mat[1][0] + mat[0][1]) * inv_s;
					break;
				}
			}
			else
			{
				s = sqrtf(mat[2][2] - (mat[0][0] + mat[1][1]) + 1.0f);
				if (s > QUAT_TRACE_QZERO_TOLERANCE)
				{
					Z = s * 0.5f;
					inv_s = 0.5f / s;
					W = (mat[1][0] - mat[0][1]) * inv_s;
					X = (mat[2][0] + mat[0][2]) * inv_s;
					Y = (mat[2][1] + mat[1][2]) * inv_s;
					break;
				}
			}
			if (++biggest > 2)
				biggest = 0;
		}
#if CLOTH_DEBUG
		assert(inv_s != 0.0f);
#else
		if (inv_s == 0.0f)
			X = Y = Z = 0.0f, W = 1.0f;
#endif
	}
	q[0] = X; q[1] = Y; q[2] = Z; q[3] = W;
	NormalizeQuat(q); // Matrix might be scaled
}



//============================================================================
// CREATION FUNCTIONS
//============================================================================

static void dynClothInitialize(DynCloth *cloth);
static void dynClothFreeData(DynCloth *cloth);

// Create an empty DynCloth structure (i.e. does not allocate particles)
DynCloth *dynClothCreateEmpty(void)
{
	DynCloth *cloth = CLOTH_MALLOC(DynCloth, 1);
	if (cloth)
	{
		dynClothInitialize(cloth);
	}
	else
	{
		dynClothErrorf("Unable to create cloth");
	}
	return cloth;
}

// Handles all memory de-allocation
void dynClothDelete(DynCloth * cloth)
{
	if (cloth)
		dynClothFreeData(cloth);
	CLOTH_FREE(cloth);
}

//////////////////////////////////////////////////////////////////////////////

// See DynCloth.h for descriptions of DynCloth members
static void dynClothInitialize(DynCloth *cloth) 
{
	memset(cloth, 0, sizeof(*cloth));

	dynClothSetNumIterations(cloth, -1, -1); // default
	

	setVec3(cloth->MinPos, 999999999.f, 999999999.f, 999999999.f);
	setVec3(cloth->MaxPos, -999999999.f, -999999999.f, -999999999.f);

	copyMat4(unitmat, cloth->Matrix);
	zeroVec4(cloth->mQuat);
	zeroVec3(cloth->Vel);
	cloth->Gravity = -32.0f;
	cloth->Drag = 0.10f;
	cloth->PointColRad = 1.0f;
	cloth->Time = 0.0f;

	zeroVec3(cloth->WindDir);
	cloth->WindMag = 0.0f;
};

static void dynClothFreeData(DynCloth *cloth)
{
	CLOTH_FREE(cloth->EyeletOrder);
	CLOTH_FREE(cloth->OrigPos);
	CLOTH_FREE(cloth->PosBuffer1);
	CLOTH_FREE(cloth->PosBuffer2);
	CLOTH_FREE(cloth->skinnedPositions);
	CLOTH_FREE(cloth->skinnedWeights);
	CLOTH_FREE(cloth->InvMasses);
	CLOTH_FREE(cloth->UVSplitVertices);
	CLOTH_FREE(cloth->renderData.RenderPos);
	CLOTH_FREE(cloth->renderData.Normals);
#if CLOTH_CALC_BINORMALS
	CLOTH_FREE(cloth->renderData.BiNormals);
#endif
#if CLOTH_CALC_TANGENTS
	CLOTH_FREE(cloth->renderData.Tangents);
#endif
	CLOTH_FREE(cloth->renderData.NormalScale);
	CLOTH_FREE(cloth->commonData.ApproxNormalLens);
	CLOTH_FREE(cloth->renderData.TexCoords);
	CLOTH_FREE(cloth->renderData.hookNormals);
	CLOTH_FREE(cloth->ColAmt);
	CLOTH_FREE(cloth->LengthConstraintsInit);
	cloth->commonData.LengthConstraints = NULL;
	CLOTH_FREE(cloth->commonData.Attachments);

	// Tessellation data.
	CLOTH_FREE(cloth->renderData.edges);
	CLOTH_FREE(cloth->renderData.edgesByTriangle);
}

//////////////////////////////////////////////////////////////////////////////

// Allocate the various buffers
int dynClothCreateParticles(DynCloth *cloth, int npoints)
{
	int rpoints;
	cloth->commonData.NumParticles = npoints;

	if (!(cloth->OrigPos = CLOTH_MALLOC(Vec3, npoints)))
		return 0;
	if (!(cloth->InvMasses = CLOTH_MALLOC(F32, npoints)))
		return 0;
	if (!(cloth->commonData.ApproxNormalLens = CLOTH_MALLOC(F32, npoints)))
		return 0;
	if (!(cloth->ColAmt = CLOTH_MALLOC(F32, npoints)))
		return 0;
	
	if (!(cloth->PosBuffer1 = CLOTH_MALLOC(Vec3, npoints)))
		return 0;
	if (!(cloth->PosBuffer2 = CLOTH_MALLOC(Vec3, npoints)))
		return 0;
	if (!(cloth->skinnedPositions = CLOTH_MALLOC(Vec3, npoints)))
		return 0;
	if (!(cloth->skinnedWeights = CLOTH_MALLOC(F32, npoints)))
		return 0;

	if (!(cloth->UVSplitVertices = (S32 *)CLOTH_MALLOC(S32, npoints)))
		return 0;

	rpoints = dynClothNumRenderedParticles(&cloth->commonData);
	if (!(cloth->renderData.RenderPos = CLOTH_MALLOC(Vec3, rpoints)))
		return 0;

	cloth->CurPos = cloth->PosBuffer1;
	cloth->OldPos = cloth->PosBuffer2;

	if (!(cloth->renderData.Normals = CLOTH_MALLOC(Vec3, rpoints)))
		return 0;
#if CLOTH_CALC_BINORMALS
	if (!(cloth->renderData.BiNormals = CLOTH_MALLOC(Vec3, rpoints)))
		return 0;
#endif
#if CLOTH_CALC_BINORMALS
	if (!(cloth->renderData.Tangents = CLOTH_MALLOC(Vec3, rpoints)))
		return 0;
#endif
	if (!(cloth->renderData.NormalScale = CLOTH_MALLOC(F32, rpoints)))
		return 0;
	if (!(cloth->renderData.TexCoords = CLOTH_MALLOC(Vec2, rpoints)))
		return 0;

	return rpoints;
}

//////////////////////////////////////////////////////////////////////////////
// Interfaces to DynCloth creation + DynCloth Particle allocation

DynCloth *dynClothCreate(int npoints)
{
	DynCloth *cloth = dynClothCreateEmpty();
	if (!cloth)
		return 0;
	if (!dynClothCreateParticles(cloth, npoints))
	{
		dynClothErrorf("Unable to create cloth particles");
		dynClothDelete(cloth);
		cloth = 0;
	}
	return cloth;
}

//////////////////////////////////////////////////////////////////////////////
// Set an individual DynCloth Particle

void dynClothSetParticle(DynCloth *cloth, int pidx, Vec3 pos, F32 mass)
{
	if (pos)
	{
		copyVec3(pos, cloth->OrigPos[pidx]);
		copyVec3(pos, cloth->OldPos[pidx]);
		copyVec3(pos, cloth->CurPos[pidx]);
		MINVEC3(pos, cloth->MinPos, cloth->MinPos);
		MAXVEC3(pos, cloth->MaxPos, cloth->MaxPos);
	}
	if (mass >= 0.0f)
		cloth->InvMasses[pidx] = mass > 0.0f ? 1.0f / mass : 0.0f; // Use 0 for infinite mass (fixed)
}

//////////////////////////////////////////////////////////////////////////////
// Add an 'Eyelet' to the cloth
// An Attachment contains the Particle index of the Eylet, plus two hook
// indices and a fraction (weight) in order to support dynCloths where the
// Eyelets don't line up correctly with the Hooks (e.g. LOD's).

int dynClothAddAttachment(DynCloth *cloth, int pidx, int hook1, int hook2, F32 frac)
{
	int attachidx;
	attachidx = cloth->commonData.NumAttachments;
	cloth->commonData.NumAttachments++;
	cloth->commonData.MaxAttachments = MAX(cloth->commonData.MaxAttachments, hook1+1);
	if (frac != 1.0f)
		cloth->commonData.MaxAttachments = MAX(cloth->commonData.MaxAttachments, hook2+1);
	cloth->commonData.Attachments = CLOTH_REALLOC(cloth->commonData.Attachments, DynClothAttachment, cloth->commonData.NumAttachments);
	cloth->commonData.Attachments[attachidx].PIdx = pidx;
	cloth->commonData.Attachments[attachidx].Hook1 = hook1;
	//cloth->InvMasses[pidx] = 0.0f;
	return attachidx;
}

//============================================================================
// CALCULATE CLOTH INFORMATION
//============================================================================

//////////////////////////////////////////////////////////////////////////////
// Get number of rendered particles

S32 dynClothNumRenderedParticles(DynClothCommonData *commonData) {
	return commonData->NumParticles;
}

//////////////////////////////////////////////////////////////////////////////
// Constraint setup

// Add triangle edge information.
// idx1, idx2 - Vertex indicies for the edge.
// triangle1, triangle2 - Triangle indices for triangles that share this edge. -1 for no triangle (edge of cloth).
static void dynClothAddTessellationEdge(DynCloth *cloth, int idx1, int idx2, int triangle1, int triangle2) {

	DynClothEdge *newEdge;
	int triEdges;
	int i;
	int edgeIndex;

	// First, make sure it's not already in there.
	// FIXME: Slow.
	for(i = 0; i < cloth->renderData.numEdges; i++) {
		if(	(cloth->renderData.edges[i].idx[0] == idx1 && cloth->renderData.edges[i].idx[1] == idx2) || 
			(cloth->renderData.edges[i].idx[1] == idx1 && cloth->renderData.edges[i].idx[0] == idx2)) {

			return;
		}
	}
	
	edgeIndex = cloth->renderData.numEdges;
	cloth->renderData.numEdges++;
	cloth->renderData.edges = CLOTH_REALLOC(cloth->renderData.edges, DynClothEdge, cloth->renderData.numEdges);

	newEdge = &(cloth->renderData.edges[edgeIndex]);
	newEdge->idx[0] = idx1;
	newEdge->idx[1] = idx2;

	// Track edges by triangle.
	if(triangle1 >= 0) {
		assert(cloth->renderData.edgesByTriangle[triangle1].numDefinedEdges < 3);
		triEdges = cloth->renderData.edgesByTriangle[triangle1].numDefinedEdges;
		cloth->renderData.edgesByTriangle[triangle1].edges[triEdges] = edgeIndex;
		cloth->renderData.edgesByTriangle[triangle1].numDefinedEdges++;
	}

	if(triangle2 >= 0) {
		assert(cloth->renderData.edgesByTriangle[triangle2].numDefinedEdges < 3);
		triEdges = cloth->renderData.edgesByTriangle[triangle2].numDefinedEdges;
		cloth->renderData.edgesByTriangle[triangle2].edges[triEdges] = edgeIndex;
		cloth->renderData.edgesByTriangle[triangle2].numDefinedEdges++;
	}

}

// This just goes through and adds in tessellation edges for any edges
// that aren't shared between two triangles.
static void dynClothFillEmptyEdges(DynCloth *cloth, int ntris, const int *tris) {

	int i; // Triangle index
	int j; // Triangle vertex index index
	int k; // Triangle edge index

	for(i = 0; i < ntris; i++) {
		while(cloth->renderData.edgesByTriangle[i].numDefinedEdges < 3) {

			int emptyEdge = cloth->renderData.edgesByTriangle[i].numDefinedEdges;
			const int *idxs = tris + (i * 3);

			// Find an edge that's not shared.
			for(j = 0; j < 3; j++) {

				int id1 = idxs[j];
				int id2 = idxs[(j+1) % 3];
				bool edgeFound = false;

				for(k = 0; k < cloth->renderData.edgesByTriangle[i].numDefinedEdges; k++) {

					if(	(id1 == cloth->renderData.edges[cloth->renderData.edgesByTriangle[i].edges[k]].idx[0] && id2 == cloth->renderData.edges[cloth->renderData.edgesByTriangle[i].edges[k]].idx[1]) || 
						(id1 == cloth->renderData.edges[cloth->renderData.edgesByTriangle[i].edges[k]].idx[1] && id2 == cloth->renderData.edges[cloth->renderData.edgesByTriangle[i].edges[k]].idx[0])) {

						// Already have this edge.
						edgeFound = true;

						break;
					}
				}

				if(!edgeFound) {

					// Couldn't find this edge on the triangle, so add it.
					dynClothAddTessellationEdge(cloth, id1, id2, i, -1);

					break;
				}
			}
			
		}
	}
}

static int addLengthConstraint(S32 **existingConstraints, DynClothLengthConstraint *lengthConstraint, DynCloth *cloth, int idx1, int idx2, float frac) {
	// This only iterates through a linear list of existing connections
	// for each vertex. Not a list of all connections for every vertex.
	
	if(existingConstraints[idx1] || existingConstraints[idx2]) {
		int i;
		int search; // Which list to search through.
		int match;  // The value to match in that list.

		// Pick the list with the lowest number of existing connections for
		// lowest searching time. We'll search for the other vertex in that
		// list.
		if(ea32Size(&existingConstraints[idx1]) < ea32Size(&existingConstraints[idx2])) {
			search = idx1;
			match = idx2;
		} else {
			search = idx2;
			match = idx1;
		}

		// Search through the list.
		for(i = 0; i < ea32Size(&(existingConstraints[search])); i++) {
			if(existingConstraints[search][i] == match) {
				// Connection already exists.
				return 0;
			}
		}
	}

	// Set up the actual constraint.
	dynClothLengthConstraintInitialize(lengthConstraint, cloth, idx1, idx2, frac * 1.1);

	// Keep track of the connection to avoid duplicates.
	ea32Push(&(existingConstraints[idx1]), idx2);
	ea32Push(&(existingConstraints[idx2]), idx1);

	return 1;
}

// ----------------------------------------------------------------------
//  Cached cloth constraint setup stuff.
// ----------------------------------------------------------------------

// DynClothCachedSetupLOD is the cached result of all the expensive
// parts of cloth constraint and tessellation calculation for a single
// LOD level of a single model.
typedef struct {

	// Tessellation info.
	int NumEdges;
	DynClothEdge *edges;
	DynClothTriangleEdgeList *edgesByTriangle;
	int numTriangles;

	// Constraint info.
	DynClothLengthConstraint *lengthConstraintsInit;
	S32 NumLengthConstraints;
	S32 NumConnections;
	S32 iStartOfExtraStiffnessConstraints;

	// Stuff.
	S32 *UVSplitVertices;

	// Time left before we destroy this.
	F32 fTimeSinceAccess;

} DynClothCachedSetupLOD;

// DynClothCachedSetup stores a DynClothCachedSetupLOD for each LOD
// level of the model that's been loaded.
typedef struct {

	DynClothCachedSetupLOD **lods;
	const char *pcModelName;

} DynClothCachedSetup;

// DynClothCachedSetup objects go into this.
static StashTable cachedClothSetup = NULL;

// Time that cloth information sits around in the cache before getting
// pruned.
#define CLOTH_CACHE_TIME 60

// Cloth cache critical section. Cloth data has other critical sections for more general
// things, but the cache stuff can be easily isolated.
static CRITICAL_SECTION dynClothCacheCriticalSection;

AUTO_RUN;
void dynClothCacheInitCriticalSection(void) {
	InitializeCriticalSection(&dynClothCacheCriticalSection);
}

// Get a DynClothCachedSetup based on the name and LOD number, or NULL
// if it doesn't exist. NOT thread safe.
static DynClothCachedSetupLOD *getCachedSetup(const char *pcModelName, int lod) {

	DynClothCachedSetup *setup = NULL;

	if(!cachedClothSetup) {
		cachedClothSetup = stashTableCreate(256, StashDefault, StashKeyTypeStrings, 0);
	}

	stashFindPointer(cachedClothSetup, pcModelName, &setup);

	if(setup) {

		if(lod >= eaSize(&setup->lods)) {
			return NULL;
		}

		if(setup->lods[lod]) {
			// Reset access timer.
			setup->lods[lod]->fTimeSinceAccess = 0;
		}

		// Note: This can return NULL for an empty slot.
		return setup->lods[lod];

	} else {
		return NULL;
	}
}

// Destroy a single cached cloth LOD. NOT thread safe.
static void destroyCachedSetupLOD(DynClothCachedSetupLOD *lod) {

	CLOTH_FREE(lod->edges);
	CLOTH_FREE(lod->edgesByTriangle);
	CLOTH_FREE(lod->lengthConstraintsInit);
	CLOTH_FREE(lod->UVSplitVertices);
	CLOTH_FREE(lod);

}

// Add a cached cloth setup to the main cache. Thread Safe.
static void addCachedSetup(const char *pcModelName, int lod, DynClothCachedSetupLOD *setupLOD) {

	DynClothCachedSetup *setup = NULL;

	EnterCriticalSection(&dynClothCacheCriticalSection);

	if(!cachedClothSetup) {
		cachedClothSetup = stashTableCreate(256, StashDefault, StashKeyTypeStrings, 0);
	}

	stashFindPointer(cachedClothSetup, pcModelName, &setup);

	if(!setup) {
		setup = CLOTH_MALLOC(DynClothCachedSetup, 1);
		setup->pcModelName = pcModelName;
		stashAddPointer(cachedClothSetup, pcModelName, setup, true);
	}

	// Create enough emtpy slots in the DynClothCachedSetup to fit
	// this LOD.
	while(eaSize(&setup->lods) <= lod) {
		eaPush(&setup->lods, NULL);
	}

	// If there's already an LOD here, it's probably an indication that two animation
	// threads have tried to set up the cloth at the same time. Just destroy the old one
	// and replace it with the new one.
	if(setup->lods[lod]) {
		destroyCachedSetupLOD(setup->lods[lod]);
	}

	setup->lods[lod] = setupLOD;

	LeaveCriticalSection(&dynClothCacheCriticalSection);
}

// Destroy a cached cloth setup and all the individual LODs inside of
// it. NOT thread safe.
static void destroyCachedSetup(DynClothCachedSetup *setup) {

	int i;

	for(i = 0; i < eaSize(&setup->lods); i++) {

		if(setup->lods[i]) {

			destroyCachedSetupLOD(setup->lods[i]);
			setup->lods[i] = NULL;

		}

	}

	CLOTH_FREE(setup);

}

// Destroy all of our saved cloth setup information. Thread safe.
void dynClothClearCache(void) {

	EnterCriticalSection(&dynClothCacheCriticalSection);

	if(cachedClothSetup) {

		StashTableIterator iter;
		StashElement elem;

		stashGetIterator(cachedClothSetup, &iter);

		while(stashGetNextElement(&iter, &elem)) {

			DynClothCachedSetup *setup = stashElementGetPointer(elem);

			if(setup) {

				destroyCachedSetup(setup);
			}
		}

		stashTableDestroy(cachedClothSetup);

		cachedClothSetup = NULL;

	}

	LeaveCriticalSection(&dynClothCacheCriticalSection);
}

// Update the cache and prune anything that's old enough that we probably won't be using
// it again anytime soon. THIS IS NOT THREAD SAFE. Make sure it runs AFTER the animation
// threads are done.
void dynClothUpdateCache(float dt) {

	EnterCriticalSection(&dynClothCacheCriticalSection);

	if(cachedClothSetup) {

		StashTableIterator iter;
		StashElement elem;

		DynClothCachedSetup **eaSetupsToDestroy = NULL;

		stashGetIterator(cachedClothSetup, &iter);

		while(stashGetNextElement(&iter, &elem)) {

			DynClothCachedSetup *setup = stashElementGetPointer(elem);

			if(setup) {

				bool allCacheClear = true;
				int i;
				for(i = 0; i < eaSize(&setup->lods); i++) {

					if(setup->lods[i]) {
						setup->lods[i]->fTimeSinceAccess += dt;

						if(setup->lods[i]->fTimeSinceAccess > CLOTH_CACHE_TIME) {

							// Too old. Get rid of it!
							destroyCachedSetupLOD(setup->lods[i]);
							setup->lods[i] = NULL;
						} else {
							allCacheClear = false;
						}
					}
				}

				if(allCacheClear) {
					// Remove this setup.
					eaPush(&eaSetupsToDestroy, setup);
				}
			}
		}

		{
			int i;
			for(i = 0; i < eaSize(&eaSetupsToDestroy); i++) {
				stashRemovePointer(cachedClothSetup, eaSetupsToDestroy[i]->pcModelName, NULL);
				destroyCachedSetup(eaSetupsToDestroy[i]);
			}
		}

		eaDestroy(&eaSetupsToDestroy);
	}

	LeaveCriticalSection(&dynClothCacheCriticalSection);
}

AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dynClothClearCacheManually(void) {
	dynClothClearCache();
}

void dynClothCalcLengthConstraintsAndSetupTessellation(DynCloth *cloth, S32 flags, int nfracscales, F32 *in_fracscales, int ntris, const int *tris, const char *pcModelName, int lodNum)
{

	// FIXME? With this setup, simultaneous attemps to set up the same piece of cloth that
	// isn't in the cache may result in the expensive setup being done redundantly. But I
	// don't think it's a good idea to lock the cloth cache while the setup work is being
	// done.

	DynClothCachedSetupLOD *pCachedSetup = NULL;

	// We need to lock the cloth cache's critical section just to make sure that the
	// return value from getCachedSetup doesn't get destroyed underneath us.
	EnterCriticalSection(&dynClothCacheCriticalSection);

	pCachedSetup = getCachedSetup(pcModelName, lodNum);

	if(pCachedSetup) {

		// We have a copy of the setup information for this cloth piece already
		// in our cache. Just copy that over for this part.

		// Length constraint setup.
		cloth->NumLengthConstraints = pCachedSetup->NumLengthConstraints;
		cloth->NumConnections = pCachedSetup->NumConnections;
		cloth->LengthConstraintsInit = CLOTH_MALLOC(DynClothLengthConstraint, cloth->NumLengthConstraints);
		memcpy(cloth->LengthConstraintsInit, pCachedSetup->lengthConstraintsInit, sizeof(DynClothLengthConstraint) * cloth->NumLengthConstraints);

		// Tessellation setup.
		cloth->renderData.numEdges = pCachedSetup->NumEdges;
		cloth->renderData.numTriangles = pCachedSetup->numTriangles;

		cloth->renderData.edges = CLOTH_MALLOC(DynClothEdge, cloth->renderData.numEdges);
		cloth->renderData.edgesByTriangle = CLOTH_MALLOC(DynClothTriangleEdgeList, cloth->renderData.numTriangles);

		memcpy(cloth->renderData.edges, pCachedSetup->edges, cloth->renderData.numEdges * sizeof(DynClothEdge));
		memcpy(cloth->renderData.edgesByTriangle, pCachedSetup->edgesByTriangle, cloth->renderData.numTriangles * sizeof(DynClothTriangleEdgeList));

		// UV split vertices. (Already allocated earlier. Just copy stuff over.)
		memcpy(cloth->UVSplitVertices, pCachedSetup->UVSplitVertices, cloth->commonData.NumParticles * sizeof(S32));

		cloth->commonData.LengthConstraints = cloth->LengthConstraintsInit;
		cloth->renderData.commonData.LengthConstraints = cloth->LengthConstraintsInit;

		cloth->commonData.iStartOfExtraStiffnessConstraints = pCachedSetup->iStartOfExtraStiffnessConstraints;

	}

	LeaveCriticalSection(&dynClothCacheCriticalSection);

	if(!pCachedSetup) {

		// No setup info in the cache. Do all the really expensive math here.

		int nump = cloth->commonData.NumParticles;
		int i;
		int nconstraints = ntris * 3;

		// Overall length constraint scaling value.
		F32 frac = 1;

		int numOpposites = 0;
		S32 *eaOpposites = NULL;
		S32 **existingConstraints = CLOTH_MALLOC(S32*, cloth->commonData.NumParticles);

		int actualTotalConstraints = 0;

		PERFINFO_AUTO_START("dynClothCalcLengthConstraints", 1);

		PERFINFO_AUTO_START("FindOppositePoints", 1);

		// FIXME: This is potentially very slow with large meshes. It's only
		// done on cloth creation, though.

		cloth->renderData.edgesByTriangle = CLOTH_MALLOC(DynClothTriangleEdgeList, ntris);
		cloth->renderData.numTriangles    = ntris;

		// Iterate through all the triangles. O(n)
		for(i = 0; i < ntris; i++) {
			int j;

			// And all the triangles that it could possibly share an
			// edge with. O(n^2)
			for(j = 0; j < ntris; j++) {
				int k;
				int commonPoints = 0;
				int kUncommonPoint = -1;
				bool jUncommonPoints[3] = { 1, 1, 1 };

				int commonPointsArray[3];

				// And all the points in those triangles.
				// O(n^2*3)
				for(k = 0; k < 3; k++) {
					int l;
					bool kPointIsUncommon = true;

					// And all the points in the other triangle.
					// O(n^2*9)
					for(l = 0; l < 3; l++) {
						if(tris[i*3+k] == tris[j*3+l]) {
							commonPointsArray[commonPoints] = tris[i*3+k];
							commonPoints++;
							kPointIsUncommon = false;

							// Mark this point as shared between the two
							// triangles.
							jUncommonPoints[l] = 0;
						}
					}

					// If we haven't found ANY points in the j/l triangle
					// that match this point (k) on the i/k triangle, then
					// the k point is not shared between these two
					// triangles.
					if(kPointIsUncommon) kUncommonPoint = i*3 + k;
				}

				// Two common points, so these triangles share an edge.
				if(commonPoints == 2) {
					int l;
					int jUncommonPoint;

					// Find the uncommon point on the j/l triangle, because
					// we only marked then as common/uncommon.
					for(l = 0; l < 3; l++) {
						if(jUncommonPoints[l]) jUncommonPoint = j*3 + l;
					}

					// Add the uncommon points to the list of opposite points
					// so we can make real stiffness connections later.
					ea32Push(&eaOpposites, tris[kUncommonPoint]);
					ea32Push(&eaOpposites, tris[jUncommonPoint]);

					numOpposites++;

					// Add a tessellation edge between these two triangles.
					dynClothAddTessellationEdge(cloth, commonPointsArray[0], commonPointsArray[1], j, i);
				}
			}
		}

		// Add any edges that weren't shared between two triangles.
		dynClothFillEmptyEdges(cloth, ntris, tris);

		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("TriangleEdgeConnections", 1);

		// This allocates MORE constraints than we actually will need. But
		// we're not sure how  many we'll need until we've gone through and
		// ignored all the duplicate connections. (Example, A<->B and
		// B<->A.)
		cloth->commonData.LengthConstraints = cloth->LengthConstraintsInit = CLOTH_MALLOC(DynClothLengthConstraint, (nconstraints + numOpposites));

		// Add constraints for each triangle edge.
		for(i = 0; i < ntris; i++) {
			actualTotalConstraints += addLengthConstraint(existingConstraints, &cloth->LengthConstraintsInit[actualTotalConstraints], cloth, tris[i*3  ], tris[i*3+1], frac);
			actualTotalConstraints += addLengthConstraint(existingConstraints, &cloth->LengthConstraintsInit[actualTotalConstraints], cloth, tris[i*3+1], tris[i*3+2], frac);
			actualTotalConstraints += addLengthConstraint(existingConstraints, &cloth->LengthConstraintsInit[actualTotalConstraints], cloth, tris[i*3+2], tris[i*3  ], frac);
		}

		cloth->commonData.iStartOfExtraStiffnessConstraints = actualTotalConstraints;

		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("StiffnessConnections", 1);

		// For high quality LODs, add cross constraints.
		if(lodNum <= 1) {

			if(ea32Size(&eaOpposites)) {

				for(i = 0; i < numOpposites; i++) {

					// Add constraints for each pair of points on opposite
					// sides of an edge for stiffness control.
					//
					// For example, connect points A and D in the following
					// diagram...
					//
					//   B
					//  /|\
					// A | D
					//  \|/
					//   C

					actualTotalConstraints += addLengthConstraint(
							existingConstraints,
							&cloth->LengthConstraintsInit[actualTotalConstraints],
							cloth,
							eaOpposites[i*2],
							eaOpposites[i*2 + 1],
							frac * 1.1);
				}
			}
		}

		// Now we know how many constraints we actually have.
		cloth->NumLengthConstraints = actualTotalConstraints;

		// Reallocate, shrinking down to just what we need.
		cloth->commonData.LengthConstraints = cloth->LengthConstraintsInit = CLOTH_REALLOC(cloth->LengthConstraintsInit, DynClothLengthConstraint, actualTotalConstraints);

		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("FindUVSplits", 1);

		// Find particles that are actually the same point (but maybe with
		// different UVs). These need to be connected, otherwise splits in
		// the UVs will become splits in the cloth.
		for(i = 0; i < nump; i++) {
			int j;
			for(j = 0; j < i; j++) {
				Vec3 subd;
				subVec3(cloth->CurPos[i], cloth->CurPos[j], subd);
				if(lengthVec3(subd) < 0.001) {
					cloth->UVSplitVertices[i] = j + 1;
					break;
				}
			}
		}

		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("Cleanup", 1);

		ea32Destroy(&eaOpposites);

		// Slightly confusing: existingConstraints is actually an array of
		// ea32Arrays. It was manually allocated, but each ea32Array it
		// contains needs to be destroyed in the proper way.
		for(i = 0; i < cloth->commonData.NumParticles; i++) {
			if(existingConstraints[i]) ea32Destroy(&(existingConstraints[i]));
		}
		CLOTH_FREE(existingConstraints);

		PERFINFO_AUTO_STOP();


		// Save a copy of this setup to our cache for later.

		pCachedSetup = CLOTH_MALLOC(DynClothCachedSetupLOD, 1);

		// Length constraint setup.
		pCachedSetup->NumLengthConstraints = cloth->NumLengthConstraints;
		pCachedSetup->NumConnections = cloth->NumConnections;
		pCachedSetup->lengthConstraintsInit = CLOTH_MALLOC(DynClothLengthConstraint, cloth->NumLengthConstraints);
		memcpy(pCachedSetup->lengthConstraintsInit, cloth->LengthConstraintsInit, sizeof(DynClothLengthConstraint) * cloth->NumLengthConstraints);

		// Tessellation setup.
		pCachedSetup->NumEdges = cloth->renderData.numEdges;
		pCachedSetup->numTriangles = cloth->renderData.numTriangles;

		pCachedSetup->edges = CLOTH_MALLOC(DynClothEdge, cloth->renderData.numEdges);
		pCachedSetup->edgesByTriangle = CLOTH_MALLOC(DynClothTriangleEdgeList, cloth->renderData.numTriangles);

		memcpy(pCachedSetup->edges, cloth->renderData.edges, cloth->renderData.numEdges * sizeof(DynClothEdge));
		memcpy(pCachedSetup->edgesByTriangle, cloth->renderData.edgesByTriangle, cloth->renderData.numTriangles * sizeof(DynClothTriangleEdgeList));

		// UV split vertices.
		pCachedSetup->UVSplitVertices = CLOTH_MALLOC(S32, cloth->commonData.NumParticles);
		memcpy(pCachedSetup->UVSplitVertices, cloth->UVSplitVertices, cloth->commonData.NumParticles * sizeof(S32));

		pCachedSetup->iStartOfExtraStiffnessConstraints = cloth->commonData.iStartOfExtraStiffnessConstraints;

		addCachedSetup(pcModelName, lodNum, pCachedSetup);


		PERFINFO_AUTO_STOP();
	}

	// Fix up stiffness/scale values.
	if(nfracscales > 0) {
		int iConstraint;
		for(iConstraint = 0; iConstraint < cloth->NumLengthConstraints; iConstraint++) {
			cloth->LengthConstraintsInit[iConstraint].RestLength *= in_fracscales[0];
		}
	}
}

//============================================================================
// SET CLOTH PARAMATERS
//============================================================================

void dynClothSetNumIterations(DynCloth *cloth, int sublod, int niterations)
{
	cloth->NumIterations = niterations;
}

void dynClothSetColRad(
	DynCloth *cloth,
	F32 fParticleCollisionRadius,
	F32 fParticleCollisionRadiusMax,
	F32 fParticleCollisionMaxSpeed) {

	cloth->PointColRad = fParticleCollisionRadius;
	cloth->fParticleCollisionRadiusMax = fParticleCollisionRadiusMax;
	cloth->fParticleCollisionMaxSpeed = fParticleCollisionMaxSpeed;
}

void dynClothSetMatrix(DynCloth *cloth, Mat4 mat)
{
	ExtractQuat(mat, cloth->mQuat);
	copyMat4(mat, cloth->Matrix);
}

void dynClothSetGravity(DynCloth *cloth, F32 g)
{
	cloth->Gravity = g;
};

void dynClothSetDrag(DynCloth *cloth, F32 drag)
{
	drag = MINMAX(drag, 0.0f, MAX_DRAG); // Limit drag to a reasonable range
	cloth->Drag = drag;
};

void dynClothSetWind(DynCloth *cloth, Vec3 dir, F32 speed, F32 scale)
{
	copyVec3(dir, cloth->WindDir);
	cloth->WindMag = speed;
}

//============================================================================
// COLLISIONS
//============================================================================

// Setup Collisions (see dynClothCollide.c)

int dynClothAddCollidable(DynClothObject *pClothObject)
{
	int idx = pClothObject->NumCollidables;
	pClothObject->NumCollidables++;
	pClothObject->Collidables = CLOTH_REALLOC(pClothObject->Collidables, DynClothCol, pClothObject->NumCollidables);
	pClothObject->OldCollidables = CLOTH_REALLOC(pClothObject->OldCollidables, DynClothCol, pClothObject->NumCollidables);
	dynClothColInitialize(&pClothObject->Collidables[idx]);
	dynClothColInitialize(&pClothObject->OldCollidables[idx]);
	return idx;
}

DynClothCol *dynClothGetCollidable(DynClothObject *pClothObject, int idx)
{
	if (idx >= 0 && idx < pClothObject->NumCollidables)
		return pClothObject->Collidables + idx;
	return 0;
}

DynClothCol *dynClothGetOldCollidable(DynClothObject *pClothObject, int idx)
{
	if (idx >= 0 && idx < pClothObject->NumCollidables)
		return pClothObject->OldCollidables + idx;
	return 0;
}

void dynClothInterpolateCollidable(DynClothObject *pClothObject, int idx, DynClothCol *pCol, float a) {

	DynClothCol *newCol = dynClothGetCollidable(pClothObject, idx);
	DynClothCol *oldCol = dynClothGetOldCollidable(pClothObject, idx);

	if(!newCol) {
		memset(pCol, 0, sizeof(DynClothCol));
		return;
	}

	// Just copy over everything we aren't going to interpolate.
	*pCol = *newCol;

	interpVec3(a, oldCol->Center, newCol->Center, pCol->Center);
	interpVec3(a, oldCol->Norm, newCol->Norm, pCol->Norm);
#if CLOTH_SUPPORT_BOX_COL
	interpVec3(a, oldCol->XVec, newCol->XVec, pCol->XVec);
	interpVec3(a, oldCol->YVec, newCol->YVec, pCol->YVec);
	pCol->YRadius = interpF32(a, oldCol->YRadius, newCol->YRadius);
#endif
	pCol->PlaneD = interpF32(a, oldCol->PlaneD, newCol->PlaneD);
	pCol->HLength = interpF32(a, oldCol->HLength, newCol->HLength);
	pCol->Radius = interpF32(a, oldCol->Radius, newCol->Radius);
}

bool dynClothGetCollidableInsideVolume(DynClothObject *pClothObject, int idx) {

	DynClothCol* pCol;

	if (idx >= 0 && idx < pClothObject->NumCollidables) {
		pCol = pClothObject->Collidables + idx;
	} else {
		return false;
	}

	if (!pCol) {
		return false;
	}

	return pCol->insideVolume;
}

DynClothMesh *dynClothGetCollidableMesh(DynClothObject *pClothObject, int idx, Mat4 mat)
{
	DynClothCol* pCol;
	if (idx >= 0 && idx < pClothObject->NumCollidables)
		pCol = pClothObject->Collidables + idx;
	else
		return NULL;
	if (!pCol)
		return NULL;
	if (pCol->Type & CLOTH_COL_SKIP)
		return NULL;
	if (!pCol->Mesh)
		dynClothColCreateMesh(pCol, 1);
	if (pCol->Mesh)
		dynClothColGetMatrix(pCol, mat);
	return pCol->Mesh;
};

// Collision Interface

void dynClothCollideCollidable(DynCloth *cloth, DynClothCol *clothcol)
{
	int p;
	F32 d;
	
	Vec3 oldPos;
	Vec3 startPos;
	Vec3 delta;
	Vec3 *CurPos = cloth->CurPos;
	F32 *InvMasses = cloth->InvMasses;
	F32 *ColAmt = cloth->ColAmt;
	F32 PointColRad = cloth->PointColRad;
	F32 irad = 1.0f / PointColRad;
	int npoints = cloth->commonData.NumParticles;

	int iNewCollisionTests = 0;

	for (p=0; p<npoints; p++)
	{
		copyVec3(CurPos[p], startPos);

		if (InvMasses[p] != 0.0f)
		{
			bool collided = true;
			int attempts = 5;
			float dtotal = 0;
			F32 fSpeedMod = 0;

			iNewCollisionTests++;

			// Adjust particle collision size based on the speed the particle is moving, if those options are specified
			// in the ClothInfo.
			if(cloth->fParticleCollisionRadiusMax - cloth->PointColRad) {
				Vec3 moveDelta;
				F32 fDeltaLength;
				subVec3(startPos, cloth->OldPos[p], moveDelta);
				fDeltaLength = lengthVec3(moveDelta);
				fDeltaLength = MIN(cloth->fParticleCollisionMaxSpeed, fDeltaLength);
				fSpeedMod = fDeltaLength * (cloth->fParticleCollisionRadiusMax - cloth->PointColRad);
			}

			while(collided && attempts > 0) {

				attempts--;

				copyVec3(CurPos[p], oldPos);

				d = dynClothColCollideSphere(clothcol, CurPos[p], PointColRad + fSpeedMod);

				if(collided &&
				   (dynDebugState.cloth.bForceCollideToSkinnedPosition ||
					clothcol->pushToSkinnedPos) && !dynDebugState.cloth.bForceNoCollideToSkinnedPosition) {

					Vec3 vCurToSkinned;
					Vec3 vDelta;
					F32 fScale;

					subVec3(cloth->pHarness->vHookPositions[p], oldPos, vCurToSkinned);
					subVec3(CurPos[p], oldPos, vDelta);

					fScale = dotVec3(vDelta, vCurToSkinned);
					fScale = fabs(fScale);

					// Clamp so we don't go beyond the skinned position.
					if(fScale > dynDebugState.cloth.fMaxMovementToSkinnedPos)
						fScale = dynDebugState.cloth.fMaxMovementToSkinnedPos;

					scaleAddVec3(vCurToSkinned, fScale, oldPos, CurPos[p]);
				}

				if(d < 0.001 || !clothcol->insideVolume) collided = false;

#if USE_COLAMT

				d *= irad; // (0, PointColRad] -> (0, 1]
				dtotal += d;
				if (dtotal > ColAmt[p])
					ColAmt[p] = MIN(dtotal, 1.0f);
#endif

				if(collided && clothcol->insideVolume) {

					// Inside volumes. Flip around the motion if it goes the wrong way.

					Vec3 diff;
					float dp;
					subVec3(CurPos[p], oldPos, diff);
					dp = dotVec3(diff, cloth->renderData.Normals[p]);
					if(dp > 0) {
						addVec3(diff, oldPos, CurPos[p]);
					}
				}
			}

			if(!dynDebugState.cloth.bDisablePartialCollision && cloth->InvMasses[p]) {
				subVec3(CurPos[p], startPos, delta);
				scaleAddVec3(delta, 1.0 / cloth->InvMasses[p], startPos, CurPos[p]);
			}
		}

	}

	InterlockedExchangeAdd(&g_dynClothCounters.iCollisionTests, iNewCollisionTests);
}

void dynClothUpdateCollisions(
	DynCloth *cloth,
	F32 clothRadius,
	bool collideSkels,
	bool skipOwnVolumes,
	float a)
{
	int i,p;
	int npoints = cloth->commonData.NumParticles;

	for (p=0; p<npoints; p++)
		cloth->ColAmt[p] = 0.0f;

	// Skeletons in the world. (FX cloth.)
	if(collideSkels) {

		// Handle collision with any nearby skeletons. Just treat them
		// as cylinders, with size based on visibility extents.

		Vec3 pos;
		Vec3 boxSize;
		copyVec3(cloth->Matrix[3], pos);
		subVec3(cloth->MaxPos, cloth->MinPos, boxSize);

		for(i = 0; i < eaSize(&eaDrawSkelList); i++) {

			if(eaDrawSkelList[i]->pSkeleton->pLocation) {

				Vec3 skelPos;
				float skelRadius;
				DynClothCol collidable = {0};
				Vec3 skelTop;
				Vec3 up = {0, 0, 0};
				Vec3 vMin;
				Vec3 vMax;
				float checkRadius = 0;
				float realDist = 0;

				dynNodeGetLocalPos(eaDrawSkelList[i]->pSkeleton->pLocation, skelPos);

				dynSkeletonGetVisibilityExtents(eaDrawSkelList[i]->pSkeleton, vMin, vMax, true);

				up[1] = vMax[1] - vMin[1];

				// Just average the X and Z axes for a rough radius.
				skelRadius = ((vMax[0] - vMin[0]) + (vMax[2] - vMin[2])) / 4.0;

				// Overestimate radius for a potential collision check.
				checkRadius = 2 * ((skelRadius > up[1] ? skelRadius : up[1]) + clothRadius);
				realDist = distance3(skelPos, pos);

				if(realDist < checkRadius) {
					addVec3(skelPos, up, skelTop);
					dynClothColSetCylinder(&collidable, skelPos, skelTop, skelRadius, 0, 0, 1);
					dynClothCollideCollidable(cloth, &collidable);
				}
			}
		}

	}

	if(!skipOwnVolumes) {

		if(cloth->pClothObject) {

			// Cloth collision volumes from the clothcol file.
			for(i = 0; i < cloth->pClothObject->NumCollidables; i++) {
				DynClothCol clothCol;
				dynClothInterpolateCollidable(cloth->pClothObject, i, &clothCol, a);
				if ((clothCol.Type != CLOTH_COL_NONE) && !(clothCol.Type & CLOTH_COL_SKIP))
					dynClothCollideCollidable(cloth, &clothCol);
			}
		}
	}
}

//============================================================================
// UPDATE FUNCTIONS
//============================================================================

//////////////////////////////////////////////////////////////////////////////
// dynClothUpdateParticles

// Do inertia, wind ripple, and drag calculations on cloth particles.

void dynClothUpdateParticles(DynCloth *cloth, F32 dt, F32 stepSize, const Mat4 xStiffMat, bool gotoOrigPos)
{
	Vec3 *CurPos = cloth->CurPos;
	Vec3 *OldPos = cloth->OldPos;
	F32 *InvMasses = cloth->InvMasses;	
	int p,npoints;
	F32 drag,dt2;
	Vec3 f, fdt2;
	F32 deltamax = MAXPDPOS*dt;
	F32 windscale = cloth->WindMag * 10;
	Vec3 vNormalizedForceDir;
	Vec3 vScaledMovementSinceLastFrame;
	int iNewPhysicsCalculations = 0;

	PERFINFO_AUTO_START("Particles update", 1);

	dt2 = dt*dt;
	scaleVec3(cloth->WindDir, windscale, f); // No local wind
	
	if(dt) {
		scaleVec3(cloth->vMovementSinceLastFrame, -0.5/dt, vScaledMovementSinceLastFrame);
	} else {
		zeroVec3(vScaledMovementSinceLastFrame);
	}

	// Add in wind resistance from just moving.
	scaleAddVec3(vScaledMovementSinceLastFrame, cloth->fNormalWindFromMovement, f, vNormalizedForceDir);
	windscale = normalVec3(vNormalizedForceDir);

	// Add an optional backwards force to keep things straight.
	scaleAddVec3(vScaledMovementSinceLastFrame, 5.0 * cloth->fFakeWindFromMovement, f, f);

	// Add gravity.
	f[1] += cloth->Gravity * cloth->fGravityScale;

	scaleVec3(f, dt2, fdt2);
	drag = 1.0f - cloth->Drag * stepSize;
	npoints = cloth->commonData.NumParticles;
	
	for (p=0; p<npoints; p++)
	{
		if (gotoOrigPos)
		{
			mulVecMat4(cloth->OrigPos[p], xStiffMat, OldPos[p]);
		}
		else
		{
			if (InvMasses[p] != 0.0f)
			{
				// Verlet Integration
				F32 x,y,z;
				F32 dx,dy,dz;
				x = CurPos[p][0];
				y = CurPos[p][1];
				z = CurPos[p][2];
				dx = (x-OldPos[p][0]);
				dy = (y-OldPos[p][1]);
				dz = (z-OldPos[p][2]);

				iNewPhysicsCalculations++;

				{
					// Add ripples to the cloth.
					float windPt;
					float windAmt;
					Vec3 windShift;
					Vec3 offPos;

					// Get (basically) the particle's position along the
					// axis of the wind direction. (Just for input into
					// the sin function - doesn't have to be exact to
					// anything.)
					subVec3(CurPos[p], cloth->vLastRootPos, offPos);
					windPt = dotVec3(vNormalizedForceDir, offPos);

					// Get the ripple amount for the given point based on
					// a sin wave. Arbitrary scaling amounts in here so
					// that using 1.0 in the cloth info results in a
					// reasonable default.
					windAmt = sin((windPt + cloth->Time * -20.0f * cloth->fWindRippleWaveTimeScale) * cloth->fWindRippleWavePeriodScale) * windscale * dt;

					// Shift the cloth along its normal. (Assuming that
					// it's going to be blown in such a way that the normal
					// is essentially perpendicular to the wind direction.)
					scaleVec3(cloth->renderData.Normals[p], windAmt * 0.05 * cloth->fWindRippleScale, windShift);

					// Adjust the offset (which will be scaled by drag
					// later).
					dx += windShift[0];
					dy += windShift[1];
					dz += windShift[2];

				}

#if 0 && USE_MAXPDPOS
				dx = MINMAX(dx, -deltamax, deltamax);
				dy = MINMAX(dy, -deltamax, deltamax);
				dz = MINMAX(dz, -deltamax, deltamax);
#endif
				// Update OLD position, then swap old and new below
				OldPos[p][0] = x + drag*dx/pow(InvMasses[p], cloth->fClothBoneInfluenceExponent) + fdt2[0];
				OldPos[p][1] = y + drag*dy/pow(InvMasses[p], cloth->fClothBoneInfluenceExponent) + fdt2[1];
				OldPos[p][2] = z + drag*dz/pow(InvMasses[p], cloth->fClothBoneInfluenceExponent) + fdt2[2];


				if(cloth->stiffness > 1) {
				
					F32 fFakeStiffness = (cloth->stiffness - 1.0) / 9.0;
					Vec3 vOrig;

					mulVecMat4(cloth->OrigPos[p], xStiffMat, vOrig);

					interpVec3(fFakeStiffness, OldPos[p], vOrig, OldPos[p]);

				}

			}
		}
	}

	// Swap old and new
	cloth->CurPos = OldPos;
	cloth->OldPos = CurPos;

	InterlockedExchangeAdd(&g_dynClothCounters.iPhysicsCalculations, iNewPhysicsCalculations);

	PERFINFO_AUTO_STOP();
}

//////////////////////////////////////////////////////////////////////////////
// dynClothUpdateAttachments
// Updates the Eyelets using the new Hook positions

// ffrac is used to interpolate between frames. It represents the weight
// between the previous frame and the current frame.
// E.g. if dynClothUpdate() is called 3 times during a frame,
//   dynClothUpdateAttachments() would be called 3 times with the folliwing ffracs':
//   .333 = 1/3 between old hook positions and new hook positions
//   .5   = 1/2 way between 1/3 position and new position = 2/3 between old and new
//   1.0  = Use new positions

void dynClothUpdateAttachments(DynCloth *cloth, Vec3 *hooks, F32 ffrac, int freeze)
{
	Vec3 *CurPos = cloth->CurPos;
	Vec3 *OldPos = cloth->OldPos;
	Vec3 tpos;
	Vec3 dposSum={0};
	Vec3 dpos;
	int i,pidx;

	if(!devassertmsg(
		   cloth->commonData.NumParticles == cloth->commonData.NumAttachments,
		   "Cloth particle count does not equal attachment count!")) {
		return;
	}

	for (i=0; i<cloth->commonData.NumParticles; i++) {
		zeroVec3(cloth->skinnedPositions[i]);
		cloth->skinnedWeights[i] = 0;
	}

	for (i=0; i<cloth->commonData.NumAttachments; i++)
	{
		Vec3 clothParticleLoc = {0,0,0};
		float clothParticleControl = 0;

		DynClothAttachment *attach = cloth->commonData.Attachments+i;
		pidx = attach->PIdx;

		if(!devassertmsg(
			   pidx == i,
			   "Cloth particle and attachment index mismatch!")) {
			return;
		}

		if(cloth->InvMasses[pidx] > 0.0f) {

			// Partial cloth control.
			clothParticleControl = 1.0f / (cloth->InvMasses[pidx]);
			clothParticleControl = 1.0 - pow(1.0 - clothParticleControl, cloth->fClothBoneInfluenceExponent);
			copyVec3(CurPos[pidx], clothParticleLoc);

		}

		if(!devassertmsg(
			   pidx == i,
			   "Cloth particle and hook index mismatch!")) {
			return;
		}

		copyVec3(hooks[attach->Hook1], tpos);

		// Interpolate between previous position and new position
		if (ffrac < 1.0f) {
			scaleVec3(tpos, ffrac, tpos);
			scaleAddVec3(CurPos[pidx], 1.0f-ffrac, tpos, tpos);
		}
		if (freeze) {
			subVec3(tpos, OldPos[pidx], dpos);
			addVec3(dpos, dposSum, dposSum);
		}

		// Save this for later rendering.
		copyVec3(tpos, cloth->skinnedPositions[pidx]);
		cloth->skinnedWeights[pidx] = 1.0 - clothParticleControl;

		if(dynDebugState.cloth.bAllBonesAreFullySkinned) {

			// Force cloth physics off for anything that's skinned at all.
			if(clothParticleControl != 1.0) clothParticleControl = 0;

		} else {

			if(clothParticleControl && dynDebugState.cloth.bDisablePartialControl) {

				// If partial control is turned off, then any cloth influence is considered 100% influence.
				clothParticleControl = 1.0;
			}
		}

		if(clothParticleControl) {
			Vec3 out;
			Vec3 dist;

			subVec3(CurPos[pidx], clothParticleLoc, dist);

			scaleVec3(clothParticleLoc, clothParticleControl, out);
			scaleAddVec3(tpos, 1.0f - clothParticleControl, out, tpos);
		}

		{
			// The old position needs to be moved according to the bone
			// influence amount, but not fully moved to the same
			// position as the current position to keep the velocity.
			Vec3 vOldPosDiff;
			subVec3(tpos, OldPos[pidx], vOldPosDiff);
			scaleAddVec3(vOldPosDiff, (1.0 - clothParticleControl), OldPos[pidx], OldPos[pidx]);
		}

		copyVec3(tpos, CurPos[pidx]);

	}
	if (freeze) { // fix for first frame where hooks might "move" a large distance!
		int nump = cloth->commonData.NumParticles;
		scaleVec3(dposSum, 1.f/cloth->commonData.NumAttachments, dpos);
		// Transform all non-harness points by this distance
		for (i=0; i<nump; i++)
		{
			addVec3(CurPos[i], dpos, CurPos[i]);
			copyVec3(CurPos[i], OldPos[i]);
		}
		// Transform harness points back
		for (i=0; i<cloth->commonData.NumAttachments; i++)
		{
			pidx = cloth->commonData.Attachments[i].PIdx;
			subVec3(CurPos[pidx], dpos, CurPos[pidx]);
			copyVec3(CurPos[pidx], OldPos[pidx]);
		}
	}
}

static void xform_particles(DynCloth *cloth, Mat4 newmat, int zerovel)
{
#if CLOTH_DEBUG_DPOS
	F32 maxdpos2 = dynClothDebugMaxDpos*dynClothDebugMaxDpos;
	Vec3 dpos;
#endif
	Vec3 *CurPos = cloth->CurPos;
	Vec3 *OldPos = cloth->OldPos;
	Mat4 imat,tmat;
	int p,nump;
	transposeMat4Copy(cloth->Matrix, imat);
	mulMat4(newmat, imat, tmat);
	nump = cloth->commonData.NumParticles;
	for (p=0; p<nump; p++)
	{
#if CLOTH_DEBUG_DPOS
		Vec3 prev_curpos;
		copyVec3(CurPos[p], prev_curpos);
#endif
		if (zerovel)
		{
			mulVecMat4(CurPos[p], tmat, OldPos[p]);
			copyVec3(OldPos[p], CurPos[p]); // zero velocity
		}
		else
		{
			Vec3 oldpos;
			copyVec3(OldPos[p], oldpos);
			mulVecMat4(CurPos[p], tmat, OldPos[p]); // xform newpos to oldpos
			mulVecMat4(oldpos, tmat, CurPos[p]);	// xform oldpos to newpos
		}
#if CLOTH_DEBUG_DPOS
		subVec3(OldPos[p], prev_curpos, dpos);
		if (lengthVec3Squared(dpos) > maxdpos2)
			dynClothDebugError++;
#endif
	}
	cloth->CurPos = OldPos;
	cloth->OldPos = CurPos;
}

void dynClothOffsetInternalPosition(DynCloth *cloth, const Vec3 newPositionOffset)
{
	Vec3 delta;
	Vec3 *CurPos = cloth->CurPos;
	Vec3 *OldPos = cloth->OldPos;
	int nump = cloth->commonData.NumParticles, p;
	subVec3(newPositionOffset, cloth->PositionOffset, delta);

	if (!delta[0] && !delta[1] && !delta[2])
		return;

	for (p=0; p<nump; p++)
	{
		subVec3(CurPos[p], delta, CurPos[p]);
		subVec3(OldPos[p], delta, OldPos[p]);
	}
	copyVec3(newPositionOffset, cloth->PositionOffset);
	subVec3(cloth->Matrix[3], delta, cloth->Matrix[3]);
	subVec3(cloth->EntMatrix[3], delta, cloth->EntMatrix[3]);
}


//////////////////////////////////////////////////////////////////////////////
// dynClothCheckMaxMotion()
// Check for too much motion.
// 'inmat' = matrix of the primary attachment node
// If the attachment node has moved a LOT
//   (see MAX_FRAME_DPOS and MAX_FRAME_DANG_COS)
//   then just transform the particles to the new location / orientation
// If the attachment node has moved more than the physics can handle
//   (see MAXACCEL, MAXDANG, MAXDPOS)
//   then partially transform the particles such that the amount of motion
//   can be better handled by the physics (i.e. 'pretend' that there was
//   less motion than there really was)
// The 'freeze' paramater indicates that we know that there is a lot of
//   motion and we want to simply transform the particles.

int dynClothCheckMaxMotion(DynCloth *cloth, F32 dt, Mat4 inmat, int freeze)
{
	int res = 0;
	Vec3 vel;
	Vec4 quat;
	Mat4 newmat;

	// Remove scaling from matrix
	copyMat4(inmat, newmat);
	normalVec3(newmat[0]);
	normalVec3(newmat[1]);
	normalVec3(newmat[2]);

	// Extract Information from the matrix
	ExtractQuat(newmat, quat);
	subVec3(newmat[3], cloth->Matrix[3], vel);

	// Once 'res' is set, further checks are ignored.
	if (freeze)
	{
		xform_particles(cloth, newmat, 1);
		res = 1;
	}
	
#if USE_MAXACCEL
	// Check Velocity
	if (!res)
	{
		Vec3 dvel;
		F32 maxaccel, accelamt2;
		maxaccel = MAXACCEL * dt;
		subVec3(vel, cloth->Vel, dvel);
		accelamt2 = dotVec3(dvel,dvel);
		if (accelamt2 > maxaccel*maxaccel)
		{
  			xform_particles(cloth, newmat, 1);
			res = 1;
		}
	}
#endif // USE_MAXACCEL
	
#if USE_MAXDANG
	// Check Rotation
	if (!res)
	{
		F32 dp;

		dp = dotVec4(quat, cloth->mQuat); // = cos (theta/2)
		dp = fabs(dp); // don't care if rotation is negative or positive
		if (dp < MAX_FRAME_DANG_COS) // .707 = 90 degrees in one frame
		{
			xform_particles(cloth, newmat, 1);
			res = 1;
		}
		else
		{
			F32 maxdang = MAXDANG*dt;
			F32 mincosdang = fcos(maxdang);
			if (dp < mincosdang)
			{
				// .707 < dp < MINCOSDANG
				F32 dang = acosf(dp);
				F32 frac1 = maxdang / dang;
				F32 frac2 = 1.0f - frac1;
				Mat4 tmat;
				Vec4 tquat;
				tquat[0] = quat[0]*frac2 + cloth->mQuat[0]*frac1;
				tquat[1] = quat[1]*frac2 + cloth->mQuat[1]*frac1;
				tquat[2] = quat[2]*frac2 + cloth->mQuat[2]*frac1;
				tquat[3] = quat[3]*frac2 + cloth->mQuat[3]*frac1;
				NormalizeQuat(tquat);
				CreateQuatMat(tmat, tquat);
				copyVec3(cloth->Matrix[3], tmat[3]); // Copy pos
				xform_particles(cloth, tmat, 0);
				res = 0;
			}
		}
	}
#endif // USE_MAXDANG

#if USE_MAXDPOS
	// Check Position
	if (!res)
	{
		Vec3 dpos;
		F32 dist;
		F32 maxdpos = MAXDPOS * dt;

		copyVec3(vel, dpos);
		dist = normalVec3(dpos);
		
		if (dist > MAX_FRAME_DPOS)
		{
			xform_particles(cloth, newmat, 1);
			res = 1;
		}
		else if (dist > maxdpos)
		{
			int p;
			Vec3 *CurPos = cloth->CurPos;
			Vec3 *OldPos = cloth->OldPos;
			int nump = cloth->commonData.NumParticles;
			F32 frac = (dist - maxdpos);
  			scaleVec3(dpos, frac, dpos);
			for (p=0; p<nump; p++)
			{
				addVec3(CurPos[p], dpos, CurPos[p]);
				addVec3(OldPos[p], dpos, OldPos[p]); // maintain velocity
			}
			res = 0;
		}
		else
		{
			res = 0;
		}
	}
#endif // USE_MAXDPOS

	// Store updated values
	copyMat4(newmat, cloth->Matrix);
	copyVec4(quat, cloth->mQuat);
	copyVec3(vel, cloth->Vel);

	cloth->SkipUpdatePhysics = res;
	
	return res;
}

void dynClothInitRotation(DynCloth* pCloth, Quat qRot)
{
	Mat4 mat;
	quatToMat(qRot, mat);
	zeroVec3(mat[3]);
	xform_particles(pCloth, mat, 1);
}

//////////////////////////////////////////////////////////////////////////////
// Convenience function

void dynClothUpdatePosition(DynCloth *cloth, F32 dt, Mat4 newmat, Vec3 *hooks, int freeze, F32 fWorldMoveScale) {

	Vec3 delta;
	int i;

	subVec3(cloth->vLastRootPos, newmat[3], delta);

	// Shift all the vertices to move with the new position.

	for(i = 0; i < cloth->commonData.NumParticles; i++) {
		
		F32 y1 = cloth->CurPos[i][1];
		F32 y2 = cloth->OldPos[i][1];

		scaleAddVec3(delta, -1.0f + fWorldMoveScale, cloth->CurPos[i], cloth->CurPos[i]);
		scaleAddVec3(delta, -1.0f + fWorldMoveScale, cloth->OldPos[i], cloth->OldPos[i]);

		cloth->CurPos[i][1] = y1;
		cloth->OldPos[i][1] = y2;

	}

}

//////////////////////////////////////////////////////////////////////////////
// Update Constraints
// See dynClothConstraint.c

void dynClothUpdateConstraints(DynCloth *cloth, int lod, float dt, Vec3 vScale, F32 *pfMaxConstraintRatio, F32 *pfMinConstraintRatio)
{
	int c;
	int i;
	int *vertModifiedAmounts = ScratchAlloc(sizeof(int) * cloth->commonData.NumParticles);
	F32 fLengthScale = (
		vScale[0] / cloth->vOriginalScale[0] +
		vScale[1] / cloth->vOriginalScale[1] +
		vScale[2] / cloth->vOriginalScale[2]) / 3.0;

	int nconstraints = cloth->NumLengthConstraints;
	int iNewConstraintCalculations = 0;

	*pfMaxConstraintRatio = 0.f;
	*pfMinConstraintRatio = 0.f;

	for (c = 0; c < nconstraints; c++) {
		F32 fConstraintRatio;

		iNewConstraintCalculations++;

		fConstraintRatio = dynClothLengthConstraintUpdate(
								&cloth->commonData.LengthConstraints[c],
								cloth, dt, fLengthScale,
								c >= cloth->commonData.iStartOfExtraStiffnessConstraints);

		MAX1(*pfMaxConstraintRatio, fConstraintRatio);
		MIN1(*pfMinConstraintRatio, fConstraintRatio);
	}

	InterlockedExchangeAdd(&g_dynClothCounters.iConstraintCalculations, iNewConstraintCalculations);

	// Update UV split vertices.

	// We have to do this in two passes. First, we get the averaged
	// position of all the UV split vertices for a given point into the
	// lowest indexed vertex.

	// Note: All of this code assumes that cloth->UVSplitVertices[i] has
	// the index of the lowest-indexed vertex in a group of vertices that
	// make up a given point.

	for(i = 0; i < cloth->commonData.NumParticles; i++) {
		Vec3 averaged;
		if(cloth->UVSplitVertices[i]) {
			
			int origVert = cloth->UVSplitVertices[i] - 1;
			
			vertModifiedAmounts[origVert]++;

			scaleAddVec3(cloth->CurPos[origVert], vertModifiedAmounts[origVert], cloth->CurPos[i], averaged);
			scaleVec3(averaged, 1.0f / (float)(vertModifiedAmounts[origVert] + 1), cloth->CurPos[origVert]);
		}
	}

	// Now move all the UV split vertices to that point.
	for(i = 0; i < cloth->commonData.NumParticles; i++) {
		if(cloth->UVSplitVertices[i]) {

			int origVert = cloth->UVSplitVertices[i] - 1;

			copyVec3(cloth->CurPos[origVert], cloth->CurPos[i]);
		}
	}
	
	ScratchFree(vertModifiedAmounts);
}

float dynClothGetAverageMotion(DynCloth *pCloth) {

	float fTotal = 0;
	int i;

	for(i = 0; i < pCloth->commonData.NumParticles; i++) {
		Vec3 delta;
		subVec3(pCloth->CurPos[i], pCloth->OldPos[i], delta);
		fTotal += lengthVec3(delta);
	}

	return fTotal / (float)pCloth->commonData.NumParticles;
}


//////////////////////////////////////////////////////////////////////////////
// Update Normals
// Calculates the Normals for the Modeled Particles using a cross product
//   from the deltas between neighboring Horizontal and Vertical particles.
// NormalScale is computed by averging the dot products of between the
//   normal and the delta between the Particle and each neighbor.
//   This value is inverted so that a positive value represents a relative
//   maximum and a negative value represents a relative minimum (with the
//   magnitude determing how steep of a peak or valley).
//   This value can be used to scale the normals or in a shader to darken
//   the valleys to simulate shadows.
// The Horizontal component used to calculate the normal is stored as a
//   BiNormal which can be used for bumpmapping.

static F32 calc_dp(Vec3 psegment, Vec3 p0, Vec3 norm, F32 ilength)
{
	Vec3 dv;
	F32 dp;
	subVec3(psegment, p0, dv);
	dp = dotVec3(dv, norm) * ilength;
#if 0
	if (dp >= 0.0f)
		dp = dp*dp;
	else
		dp = -dp*dp;
#endif
	return dp;
}

// Use a 1st order Taylor-expansion to approximate the square root result
void fastNormalVec3(Vec3 vec, F32 approxlen)
{
	F32 x = vec[0];
	F32 y = vec[1];
	F32 z = vec[2];
	F32 dp = x*x + y*y + z*z;
	F32 ilen = approxlen / (.5f*(approxlen*approxlen + dp));
	vec[0] = x * ilen;
	vec[1] = y * ilen;
	vec[2] = z * ilen;
}

void dynClothFastUpdateNormals(DynClothRenderData *renderData, int calcFirstNormals) {

	if(renderData && renderData->currentMesh) {
		int numTris = renderData->currentMesh->Strips[0].NumIndices / 3;
		int numVerts = renderData->currentMesh->NumPoints;
		int i;
		int *numWeightsAlready = ScratchAlloc(numVerts * sizeof(int));
		Vec3 *CurPos = renderData->RenderPos;
		DynCloth *cloth = renderData->clothBackPointer;

		if(renderData->RenderPos) {
			for(i = 0; i < numTris; i++) {
				int j;
				for(j = 0; j < 3; j++) {
					// Get the index if this vert in the triangle.
					int index = renderData->currentMesh->Strips[0].IndicesCCW[i*3+j];
					Vec3 norm;

					// Get the indices of the other verts in the triangle.
					int other1 = j - 1;
					int other2 = j + 1;
					if(other1 < 0) other1 = 2;
					if(other2 > 2) other2 = 0;
					
					other1 = renderData->currentMesh->Strips[0].IndicesCCW[i*3+other1];
					other2 = renderData->currentMesh->Strips[0].IndicesCCW[i*3+other2];

					{
						// Get the normal with a cross product.
						Vec3 vx, vy;
						subVec3(CurPos[other2], CurPos[index], vx);
						subVec3(CurPos[other1], CurPos[index], vy);
						crossVec3(vx, vy, norm);
						normalVec3(norm);
						
						// Add in the normals we've already calculated,
						// scaled by how many there were.
						if(numWeightsAlready[index]) {
							scaleAddVec3(renderData->Normals[index], numWeightsAlready[index], norm, norm);
						}

						// Normalize it.
						normalVec3(norm);

						// Save it.
						copyVec3(norm, renderData->Normals[index]);
					}

					// Calculate tangents and binormals.
					{
						Mat3 basis;
						Vec3 resultBinormal;
						Vec3 resultTangent;

						// Get texture coordinates.
						F32 *t0 = renderData->TexCoords[index];
						F32 *t1 = renderData->TexCoords[other1];
						F32 *t2 = renderData->TexCoords[other2];

						// Get vertex positions.
						F32 *v0 = renderData->RenderPos[index];
						F32 *v1 = renderData->RenderPos[other1];
						F32 *v2 = renderData->RenderPos[other2];

						// Get the current normal.
						F32 *n0 = renderData->Normals[index];

						tangentBasis(basis, v0, v1, v2, t0, t1, t2, n0);

						copyVec3(basis[0], resultTangent);
						copyVec3(basis[1], resultBinormal);
						scaleVec3(resultBinormal, -1, resultBinormal);

						// Average with current running average and save binormal.
						if(numWeightsAlready[index]) {
							scaleAddVec3(renderData->BiNormals[index], numWeightsAlready[index], resultBinormal, resultBinormal);
						}
						fastNormalVec3(resultBinormal, 1);
						copyVec3(resultBinormal, renderData->BiNormals[index]);

						// Average with current running average and save tangent.
						if(numWeightsAlready[index]) {
							scaleAddVec3(renderData->Tangents[index], numWeightsAlready[index], resultTangent, resultTangent);
						}
						fastNormalVec3(resultTangent, 1);
						copyVec3(resultTangent, renderData->Tangents[index]);

					}

					numWeightsAlready[index]++;
				}
			}
		}

		ScratchFree(numWeightsAlready);

		// Cloth vertices that are attached to the
		// character model directly need to use the
		// transformed normal for that point, NOT averaged
		// with anything (otherwise it would not line up
		// with the model's vertex at that point).
		if (renderData && renderData->hookNormals) {
			Vec3 *hookNormals = renderData->hookNormals;
			int k,pidx;
			for (k = 0; k < renderData->commonData.NumAttachments; k++) {

				DynClothAttachment *attach = &renderData->commonData.Attachments[k];
				pidx = attach->PIdx;

				if(lengthVec3(hookNormals[attach->Hook1]) > 0) {

					if(!renderData->clothBackPointer->InvMasses[pidx]) {

						// Fully skinned. Just copy over the hook's normal.
						copyVec3(hookNormals[attach->Hook1], renderData->Normals[pidx]);

					} else {

						// Interpolate between cloth simulated normal and hook normal.
						float mass = 1.0 / renderData->clothBackPointer->InvMasses[pidx];
						lerpVec3(renderData->Normals[pidx], mass, hookNormals[attach->Hook1], renderData->Normals[pidx]);
					}
				}

			}
		}

		// Fixup normals for UV split vertices. These need to be consistent
		// because we'll be doing tessellation based on them.
		{
			int *vertModifiedAmounts = ScratchAlloc(sizeof(int) * cloth->commonData.NumParticles);

			for(i = 0; i < cloth->commonData.NumParticles; i++) {
				Vec3 averaged;
				if(cloth->UVSplitVertices[i]) {

					int origVert = cloth->UVSplitVertices[i] - 1;

					vertModifiedAmounts[origVert]++;

					scaleAddVec3(cloth->renderData.Normals[origVert], vertModifiedAmounts[origVert], cloth->renderData.Normals[i], averaged);
					scaleVec3(averaged, 1.0f / (float)(vertModifiedAmounts[origVert] + 1), cloth->renderData.Normals[origVert]);
					normalVec3(cloth->renderData.Normals[origVert]);
				}
			}

			// Now move all the UV split vertices to that point.
			for(i = 0; i < cloth->commonData.NumParticles; i++) {
				if(cloth->UVSplitVertices[i]) {

					int origVert = cloth->UVSplitVertices[i] - 1;

					copyVec3(cloth->renderData.Normals[origVert], cloth->renderData.Normals[i]);
				}
			}

			ScratchFree(vertModifiedAmounts);
		}

	}
}

void dynClothUpdateNormals(DynClothRenderData *renderData)
{
	dynClothFastUpdateNormals(renderData, 0);
}


int dclothStagePhysics = 0;
AUTO_CMD_INT(dclothStagePhysics, dclothStagePhysics);

int dclothStageCollision = 1;
AUTO_CMD_INT(dclothStageCollision, dclothStageCollision);

int dclothStageConstraints = 2;
AUTO_CMD_INT(dclothStageConstraints, dclothStageConstraints);

//////////////////////////////////////////////////////////////////////////////
// Convenience functions

// skipCollisions causes ALL collisions to be skipped.
// collideSkels enables rough collisions with other skeletons.
void dynClothUpdatePhysics(DynCloth *cloth, F32 dt, F32 clothRadius, bool gotoOrigPos, bool collideSkels, bool skipCollisions, int lod, bool sleepCloth, float a, float stepSize, Vec3 vScale,  const Mat4 xStiffMat, F32 *pfMaxConstraintRatio, F32 *pfMinConstraintRatio)
{
	// Update at fixed time step for consistency.
	F32 fakedt = 0.01 * wlTimeGetStepScale() * cloth->fTimeScale * stepSize;
	int i;

	if(fakedt <= 0.00001) return;

	cloth->Time += fakedt;

	for(i = 0; i < 3; i++) {

		if(!sleepCloth && i == dclothStagePhysics) {
			PERFINFO_AUTO_START("dynClothUpdatePhysics",1);
			if (!cloth->SkipUpdatePhysics) {
				dynClothUpdateParticles(cloth, fakedt, stepSize,  xStiffMat, gotoOrigPos);
			}
			PERFINFO_AUTO_STOP();
		}

		if(!skipCollisions && i == dclothStageCollision) {
			PERFINFO_AUTO_START("dynClothUpdateCollisions",1);
			dynClothUpdateCollisions(cloth, clothRadius, collideSkels, sleepCloth, a);
			PERFINFO_AUTO_STOP();
		}

		if(!sleepCloth && i == dclothStageConstraints) {
			PERFINFO_AUTO_START("dynClothUpdateConstraints",1);
			dynClothUpdateConstraints(cloth, lod, stepSize, vScale, pfMaxConstraintRatio, pfMinConstraintRatio);
			PERFINFO_AUTO_STOP();
		}
	}
}

void dynClothCopyToRenderData(DynCloth *cloth, Vec3 *hookNormals, CCTRWhat what)
{
	if (what & CCTR_COPY_DATA) {

		int i;

		cloth->renderData.commonData = *&cloth->commonData;

		for(i = 0; i < cloth->commonData.NumParticles; i++) {

			if(dynDebugState.cloth.bJustDrawWithSkinnedPositions) {
				copyVec3(cloth->skinnedPositions[i],
						 cloth->renderData.RenderPos[i]);
			} else {

				if(dynDebugState.cloth.bLerpVisualsWithBones) {

					lerpVec3(
						cloth->CurPos[i],
						1.0 - cloth->skinnedWeights[i],
						cloth->skinnedPositions[i],
						cloth->renderData.RenderPos[i]);

				} else {

					copyVec3(cloth->CurPos[i], cloth->renderData.RenderPos[i]);

				}
			}
		}

	}
	if (what & CCTR_COPY_HOOKS) {

		if (!hookNormals && cloth->renderData.hookNormals) {

			CLOTH_FREE(cloth->renderData.hookNormals);
			cloth->renderData.hookNormals = NULL;

		} else if (hookNormals && !cloth->renderData.hookNormals) {

			// TODO: Is this the right number of hook normals?
			cloth->renderData.hookNormals = CLOTH_MALLOC(Vec3, cloth->commonData.MaxAttachments);

		}

		if (hookNormals) {
			memcpy(cloth->renderData.hookNormals, hookNormals, cloth->commonData.MaxAttachments*sizeof(Vec3));
		}
	}
	cloth->renderData.clothBackPointer = cloth;
}


void dynClothUpdateDraw(DynClothRenderData *renderData)
{
	PERFINFO_AUTO_START("dynClothUpdateDraw", 1);
	dynClothUpdateNormals(renderData);
	PERFINFO_AUTO_STOP();
}

// ======================================================================
// Cloth thread syncronization
// ======================================================================

// Cloth gets accessed from the animation thread, so all this thread syncronization stuff
// is needed for handling shared assets.

static CRITICAL_SECTION clothResourceCs;
static CRITICAL_SECTION clothDebugCs;

AUTO_RUN;
void dynClothInitCriticalSections(void) {
	InitializeCriticalSection(&clothResourceCs);
	InitializeCriticalSection(&clothDebugCs);
}

void dynClothLockThreadData_dbg(const char *functionName) {
	EnterCriticalSection(&clothResourceCs);
}

void dynClothUnlockThreadData_dbg(const char *functionName) {
	LeaveCriticalSection(&clothResourceCs);
}

void dynClothLockDebugThreadData_dbg(const char *functionName) {
	EnterCriticalSection(&clothDebugCs);
}

void dynClothUnlockDebugThreadData_dbg(const char *functionName) {
	LeaveCriticalSection(&clothDebugCs);
}

#if CLOTH_TRACK_ALLOCATIONS

// ======================================================================
// Cloth memory allocation tracking
// ======================================================================

static size_t dynClothTotalAllocatedClothBytes = 0;
const char dynClothHeaderVerification[] = "ASDFASDFASDFASD";

typedef struct ClothAllocHeader {
	size_t size;
	const char *funcName;
	char verificationString[16];
} ClothAllocHeader;

static ClothAllocHeader **clothAllocations = NULL;

void *cloth_calloc(size_t n, size_t s, const char *funcName) {

	ClothAllocHeader *header;

	dynClothLockDebugThreadData(); {

		size_t realSize = n * s + sizeof(ClothAllocHeader);
		header = calloc(1, realSize);
		header->size = realSize;
		header->funcName = funcName;
		strcpy(header->verificationString, dynClothHeaderVerification);
		dynClothTotalAllocatedClothBytes += realSize;
		eaPush(&clothAllocations, header);

	} dynClothUnlockDebugThreadData();

	return header + 1;
}


void dynClothPrintAllocations_dbg(void) {

	dynClothLockDebugThreadData(); {

		int i;
		for(i = 0; i < eaSize(&clothAllocations); i++) {
			ClothAllocHeader *header = clothAllocations[i];
			printf("%20u - %s\n", header->size, header->funcName);
		}
		printf("Total bytes allocated with CLOTH_MALLOC: %u\n", dynClothTotalAllocatedClothBytes);

	} dynClothUnlockDebugThreadData();
}

void *cloth_realloc(void *old, size_t newSize, const char *funcName) {

	ClothAllocHeader *header = NULL;

	dynClothLockDebugThreadData(); {

		if(old) {
			header = ((ClothAllocHeader*)old) - 1;
		} else {
			header = ((ClothAllocHeader*)(cloth_calloc(0, 0, funcName)) - 1);
		}

		assert(!strcmp(dynClothHeaderVerification, header->verificationString));
		dynClothTotalAllocatedClothBytes -= header->size;
		dynClothTotalAllocatedClothBytes += newSize;
		header->size = newSize;
		eaFindAndRemoveFast(&clothAllocations, header);
		header = realloc(header, newSize + sizeof(ClothAllocHeader));
		assert(!strcmp(dynClothHeaderVerification, header->verificationString));
		eaPush(&clothAllocations, header);


	} dynClothUnlockDebugThreadData();

	return header + 1;
}

void cloth_free(void *p) {

	dynClothLockDebugThreadData(); {

		ClothAllocHeader *header = ((ClothAllocHeader*)p) - 1;

		assert(!strcmp(dynClothHeaderVerification, header->verificationString));
		eaFindAndRemoveFast(&clothAllocations, header);
		dynClothTotalAllocatedClothBytes -= header->size;
		free(header);

	} dynClothUnlockDebugThreadData();
}

#else

void dynClothPrintAllocations_dbg(void) {
	printf("CLOTH_TRACK_ALLOCATIONS not enabled!\n");
}

#endif

AUTO_COMMAND;
void dynClothPrintAllocations(void) {
	dynClothPrintAllocations_dbg();
}





