// Shared macros and prototypes for cloth library
#pragma once

// Creation
extern void dynClothCalcConstants(DynCloth *cloth, int calcTexCoords);
extern void dynClothSetNumIterations(DynCloth *cloth, int sublod, int niterations);

// Utility
extern DynCloth *dynClothCopyCloth(DynCloth *cloth, DynCloth *pcloth);
extern DynCloth *dynClothBuildCopy(DynCloth *cloth, DynCloth *pcloth, F32 point_h_scale);

// Collisions
extern void dynClothCollideCollidable(DynCloth *cloth, DynClothCol *clothcol);
	
// Queries
extern S32 dynClothNumRenderedParticles(DynClothCommonData *commonData);

// SubLOD
extern void dynClothSetMinSubLOD(DynCloth *cloth, int minsublod);
extern S32  dynClothSetSubLOD(DynCloth *cloth, int sublod);
extern S32  dynClothGetSubLOD(DynCloth *cloth);
	
// Physics
extern void dynClothSetMatrix(DynCloth *cloth, Mat4Ptr mat);

// Update
extern void dynClothUpdateCollisions(DynCloth *cloth, F32 clothRadius, bool collideSkels, bool skipOwnVolumes, float a);
extern void dynClothUpdateConstraints(DynCloth *cloth, int lod, float dt, Vec3 vScale, F32 *pfMaxConstraintRatio, F32 *pfMinConstraintRatio);
typedef enum CCTRWhat {
	CCTR_COPY_DATA		= 1<<0,
	CCTR_COPY_HOOKS		= 1<<1,

	CCTR_COPY_ALL		= CCTR_COPY_HOOKS|CCTR_COPY_DATA,
} CCTRWhat;
extern void dynClothCopyToRenderData(DynCloth *cloth, Vec3 *hookNormals, CCTRWhat what);

extern void dynClothFastUpdateNormals(DynClothRenderData *renderData, int calcFirstNormals);
extern void dynClothUpdateNormals(DynClothRenderData *renderData);
extern void dynClothUpdateIntermediatePoints(DynClothRenderData *renderData);
extern void dynClothDarkenConcavePoints(DynClothRenderData *renderData);

// Render
extern DynClothMesh *dynClothCreateMeshIndices(DynCloth *cloth, Model *model, int lod);

// Errors (returns -1)
extern int dynClothErrorf(FORMAT_STR char const *fmt, ...);
#define dynClothErrorf(fmt, ...) dynClothErrorf(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// DynClothLengthConstraint
extern void dynClothLengthConstraintInitialize(DynClothLengthConstraint *constraint, DynCloth *cloth, S32 p1, S32 p2, F32 frac);

extern F32 dynClothLengthConstraintUpdate(
	const DynClothLengthConstraint *constraint,
	DynCloth *cloth,
	float dt,
	float fLengthScale,
	bool bCrossConstraint);

extern void dynClothLengthConstraintFastUpdate(const DynClothLengthConstraint *constraint, DynCloth *cloth);

// DynClothMesh
extern void dynClothMeshAllocate(DynClothMesh *mesh, int npoints);
extern void dynClothMeshSetPoints(DynClothMesh *mesh, int npoints, Vec3 *pts, Vec3 *norms, Vec2 *tcoords, Vec3 *binorms, Vec3 *tangents);
extern void dynClothMeshCreateStrips(DynClothMesh *mesh, int num, int type);
extern void dynClothMeshCalcMinMax(DynClothMesh *mesh);
extern void dynClothMeshSetColorAlpha(DynClothMesh *mesh, Vec3 c, F32 a);

extern void dynClothStripCreateIndices(DynClothStrip *strip, int num, int type);

// DynClothCol
extern void dynClothColInitialize(DynClothCol *clothcol);
extern void dynClothColDelete(DynClothCol *clothcol);

// DynClothAttachmentHarness
extern void dynClothAttachmentHarnessDestroy(DynClothAttachmentHarness* pHarness);

// MACROS

#define CLOTH_TRACK_ALLOCATIONS 0

#if CLOTH_TRACK_ALLOCATIONS

void *cloth_calloc(size_t n, size_t s, const char *funcName);
void *cloth_realloc(void *old, size_t newSize, const char *funcName);
void cloth_free(void *p);

#define CLOTH_MALLOC(type, n) ((type *)cloth_calloc((n), sizeof(type), __FUNCTION__))
#define CLOTH_REALLOC(old, type, n) ((type *)cloth_realloc(old, (n) * sizeof(type), __FUNCTION__))
#define CLOTH_FREE(p)       { if(p) { cloth_free(p);     (p)=0; } }

#else

#define CLOTH_MALLOC(type, n) ((type *)calloc((n), sizeof(type)))
#define CLOTH_REALLOC(old, type, n) ((type *)realloc(old, (n) * sizeof(type)))
#define CLOTH_FREE(p)       { if(p) { free(p);     (p)=0; } }

#endif



