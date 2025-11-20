#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "Frustum.h"
#include "MemTrack.h"

#if _PS3
#define ZO_SPU_DRAW 1
#else
#define ZO_SPU_DRAW 0
#endif

#define ZO_DRAW_INTERLEAVED 0		//  turn on if you need to pass values other than 0 and 1

#define ZO_SAFE				0

#define ZO_NUM_JITTER_FRAMES 3
#define ZO_TEST_BITS 7

// Use 24.8 fixed-point for triangle screen coordinates for scan-conversion
#define ZB_SCAN_RES 256
#define ZB_HALF_SCAN_RES 128
#define ZB_OO_SCAN_RES ( 1.0f / ZB_SCAN_RES )

#if 0

#define ZB_HALF_DIM 64
#define ZB_DIM 128
#define ZB_DIM_MINUS_ONE 127
#define ZB_ONE_OVER_DIM 0.0078125f
// use 8x8 - 32x32
#define ZO_START_HOFFSET 3
#define ZO_DEFAULT_END_HOFFSET 2

#define ZO_USE_JITTER 1
#define ZO_NUM_JITTER_FRAMES 3

#elif 1

#define ZB_HALF_DIM 128
#define ZB_DIM 256
#define ZB_DIM_MINUS_ONE 255
#define ZB_ONE_OVER_DIM 0.00390625f
// use 8x8 - 64x64
#define ZO_START_HOFFSET 3
#define ZO_DEFAULT_END_HOFFSET 1

#define ZO_USE_JITTER 1

#else

#define ZB_HALF_DIM 256
#define ZB_DIM 512
#define ZB_DIM_MINUS_ONE 511
#define ZB_ONE_OVER_DIM 0.001953125f
// use 8x8 - 32x32
#define ZO_START_HOFFSET 3
#define ZO_DEFAULT_END_HOFFSET 4

#define ZO_USE_JITTER 0
#define ZO_NUM_JITTER_FRAMES 3

#endif



#define ZMAX 1.0f

#define ZINIT ZMAX
#define ZTEST(newZ, existingZ) ((newZ) <= (existingZ))

#if PLATFORM_CONSOLE
// This test must match the ZTEST defined above
#define ZTEST_XBOX(newZ, existingZ) (FloatBranchFLE(newZ, existingZ, 1, 0))
#define ZTEST_XBOX_TF(newZ, existingZ, T, F) (FloatBranchFLE(newZ, existingZ, (T), (F)))
#endif

#define ZTOR(val)		(val>=0.9f?2550*val-2295:0)
#define ZTOG(val)		(0)
#define ZTOB(val)		(0)
#define ZTOALPHA(val)	(val < ZMAX ? 255 : 0)

#define MAX_OCCLUDERS 200
#define MAX_OCCLUDER_TRIS 2000
#define MAX_OCCLUDER_VERTS 8000
#define MIN_OCCLUDER_SCREEN_SPACE 0.001f		// weighted screen space
#define MIN_OCCLUDER_REAL_SCREEN_SPACE 0.0025f  // real screen space
#define MAX_REJECTED_OCCLUDERS 20

// the smaller the number, the more rejections will happen
// the larger the number, the fewer false rejections will happen
// JE: was 1e-5 prior to SSE/VMX optimizations, which give results just over 1e-4 off from previous code
#define TEST_Z_OFFSET 1.8E-4f

////////////////////////////////////////////////////
////////////////////////////////////////////////////

typedef float ZType;
typedef struct ModelLOD ModelLOD;
typedef struct BasicTexture BasicTexture;
typedef struct ManagedThread ManagedThread;
typedef void * HANDLE;
typedef struct EventOwner EventOwner;

typedef struct ZBufferPoint
{
	int clipcode;
	int x,y;     // integer coordinates in the zbuffer
	ZType z;
	Vec4 hClipPos;
} ZBufferPoint, *ZBufferPointPtr;

typedef struct ZBufferPointCached
{
	int cached;
	ZBufferPoint p;
} ZBufferPointCached;

typedef struct Occluder
{
	float screenSpace;
	float fZWeightedScreenSpace; // for debugging
	ModelLOD *m;
	Vec3 minBounds, maxBounds;
	int faceBits;
	Mat4 modelToWorld;
	U32 ok_materials;
	bool double_sided;
} Occluder, *OccluderPtr;

typedef struct RejectedOccluder
{
	float fZWeightedScreenSpace;
	char const * pchName;
	int iTris;
	int iVerts;
} RejectedOccluder;

typedef struct HierarchicalZBuffer
{
	int dim;
	ZType *minZ;
	ZType *maxZ;
} HierarchicalZBuffer;

typedef struct GfxOcclusionBuffer
{
	Mat44 projection_mat;
	Frustum frustum;

	EventOwner *event_timer;
	U32 uInterlockedReady;

	struct 
	{
		ManagedThread *occlusion_thread;
		HANDLE wait_for_zo_done;
		volatile bool needs_wait;
		volatile bool in_createzhierarchy;
		volatile bool needs_occluder_wait;
		volatile bool in_drawoccluders;

#if !_PS3
		int two_zo_draw_threads;
		ManagedThread *occlusion_thread2;
		HANDLE wait_for_zo_done2;
#endif
	} zthread;

	struct
	{
		ZType *data;
		GuardedAlignedMalloc data_alloc;

		int size;
		float width_mul, height_mul;
	} zbuffer;

	struct
	{
		int array_count;
		HierarchicalZBuffer *arrays;
		ZType minz, maxz;
	} zhierarchy;

	struct
	{
		int occluder_count, draw_occluder_count, last_occluder_count;
		OccluderPtr *occluders;
		OccluderPtr *draw_occluders;
		F32 min_occluder_screenspace;
		F32 last_screen_space, last_z;
		OccluderPtr occluders1[MAX_OCCLUDERS];
		OccluderPtr occluders2[MAX_OCCLUDERS];
	} occluders;

	struct 
	{
		int tint_occluders;
		int occluder_tris_drawn;
		int num_tests, num_exact_tests, num_test_calls, num_culls;
		int last_num_tests, last_num_exact_tests, last_num_test_calls, last_num_culls;
		int iNumRejectedOccluders;
		RejectedOccluder aRejectedOccluders[MAX_REJECTED_OCCLUDERS];
		BasicTexture *zbuf_tex;
	} debug;

#if ZO_USE_JITTER
	struct 
	{
		U32 frame_num, draw_frame_num;
		float amount;
	} jitter;

	bool use_jitter;
#endif

} GfxOcclusionBuffer;


typedef struct GfxZBuffData
{
	Mat44 projection_mat;

    F32 projection_far, one_over_projection_far;
	F32 ydim, yoffset;

	bool use_jitter;
	F32 amount;

	F32 * data;
	int width;
	int occluder_tris_drawn;

	GfxOcclusionBuffer * zo;
	ZBufferPointCached * point_cache_debug;
} GfxZBuffData;

