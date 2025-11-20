#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "BitArray.h"
#include "RdrTextureEnums.h"
#include "RdrEnums.h"
#include "SystemSpecs.h"


//////////////////////////////////////////////////////////////////////////
// convenience macros
#define rdrSurfaceDestroy(surface) (surface)->destroy(surface)

#define rdrSurfaceChangeParams(surface, params) (surface)->changeParams(surface, params)

#define rdrSurfaceSetWriteMask(surface, mask) (surface)->setWriteMask(surface, mask)


//////////////////////////////////////////////////////////////////////////
// types and enums

typedef struct RdrSurface RdrSurface;
typedef struct RdrDevice RdrDevice;
typedef struct RdrSurfaceDX RdrSurfaceDX;
typedef struct RdrSurfaceWinGL RdrSurfaceWinGL;
typedef int ShaderHandle;

#define RDRSURFACE_SET_INDEX_DEFAULT 0

#define RDRSURFACE_SET_INDEX_NUMBITS 16
#define RDRSURFACE_BUFFER_NUMBITS 3
#define RDRSURFACE_INDEX_NUMBITS 12
#define RDRSURFACE_NOAUTORESOLVE_BIT 1

typedef enum
{
	WRITE_RED	=1<<0,
	WRITE_GREEN	=1<<1,
	WRITE_BLUE	=1<<2,
	WRITE_ALPHA	=1<<3,
	WRITE_DEPTH	=1<<4,

	WRITE_ALL = WRITE_RED|WRITE_GREEN|WRITE_BLUE|WRITE_ALPHA|WRITE_DEPTH,

} RdrWriteMask;

typedef enum RdrSurfaceFlags
{
	SF_MRT2				= 1<<0,		// creates a 2 target MRT
	SF_MRT4				= 1<<1,		// creates a 4 target MRT
	SF_DEPTHONLY		= 1<<2,		// creates a depth-only target
	SF_DEPTH_TEXTURE	= 1<<3,		// creates a texture to receive the depth-buffer for general depth read
	SF_SHADOW_MAP		= 1<<4,		// creates a texture to receive the depth-buffer for shadow map
	SF_CUBEMAP			= 1<<5,		// creates a cubemap render texture for the color buffers
} RdrSurfaceFlags;

AUTO_ENUM;
typedef enum RdrSurfaceBufferType
{
	SBT_RGBA,					// 8,8,8,8
	SBT_RGBA10,					// 10f,10f,10f,2
	SBT_FLOAT,					// 32f
	SBT_RG_FIXED,				// 16,16
	SBT_RG_FLOAT,				// 16f,16f
	SBT_RGBA_FIXED,				// 16,16,16,16
	SBT_RGBA_FLOAT,				// 16f,16f,16f,16f
	SBT_RGBA_FLOAT32,			// 32f,32f,32f,32f
	SBT_RGB16,					// 5,6,5 - often actually NULL surface
	SBT_BGRA,					// 8,8,8,8

	SBT_NUM_TYPES,

	SBT_TYPE_MASK			= (1<<16)-1,
	
	SBT_SRGB			= (1<<18),

} RdrSurfaceBufferType;
extern StaticDefineInt RdrSurfaceBufferTypeEnum[];

STATIC_ASSERT(SBUF_DEPTH < (1 << RDRSURFACE_BUFFER_NUMBITS))

typedef enum RdrSurfaceDataType
{
	SURFDATA_RGB,
	SURFDATA_RGBA,
	SURFDATA_DEPTH,
	SURFDATA_STENCIL,
	SURFDATA_RGB_F32,
	SURFDATA_BGRA,
} RdrSurfaceDataType;

typedef struct RdrSurfaceParams
{
	RdrSurfaceFlags	flags;
	RdrSurfaceFlags ignoreFlags;
	RdrSurfaceBufferType buffer_types[SBUF_MAXMRT];
	RdrTexFlags buffer_default_flags[SBUF_MAXMRT];
	U32		width,height;
	U8		desired_multisample_level;
	U8		required_multisample_level;
	U8		stencil_bits;
	U8		depth_bits;
	const char *name;
	U8		stereo_option:3;
} RdrSurfaceParams;

typedef struct RdrSurfaceActivateParams
{
	RdrSurface *surface;
	RdrSurfaceBufferMaskBits write_mask;
	RdrSurfaceFace face;
} RdrSurfaceActivateParams;

typedef struct RdrSurfaceData
{
	RdrSurfaceDataType type;
	U8 *data;
	U32 width, height;
} RdrSurfaceData;

typedef struct RdrSetFogData
{
	RdrSurface *surface;
	F32 low_near_dist, low_far_dist, low_max_fog;
	F32 high_near_dist, high_far_dist, high_max_fog;
	F32 low_height, high_height;
	Vec3 low_fog_color, high_fog_color;
	int bVolumeFog;
} RdrSetFogData;

typedef struct RdrSwapSnapshotData
{
	RdrSurface *surface0;
	RdrSurface *surface1;
	U32 index0;
	U32 index1;
	RdrSurfaceBuffer buffer0;
	RdrSurfaceBuffer buffer1;
} RdrSwapSnapshotData;

typedef struct RdrSurfaceSnapshotData
{
	RdrSurface *surface;
	const char *name;
	int dest_set_index;
	int txaa_prev_set_index;
	U16 txaa_debug_mode;
	U16 buffer_mask;
	U32 continue_tiling : 1;
	U32 force_srgb : 1;
} RdrSurfaceSnapshotData;

typedef struct RdrSurfaceResolveData
{
	RdrSurface* dest;
	IVec4 sourceRect;
	IVec2 destPoint;
	bool hasSourceRect;
	bool hasDestPoint;
} RdrSurfaceResolveData;

typedef struct RdrSurfaceUpdateMatrixData
{
	Mat44 projection, fardepth_projection, sky_projection;
	Mat4 view_mat, inv_view_mat;
	Vec3 camera_pos;
	Mat4 fog_mat;
	RdrSurface *surface;
	F32 znear, zfar, far_znear;
	F32 viewport_x, viewport_width, viewport_y, viewport_height;
} RdrSurfaceUpdateMatrixData;

typedef struct RdrSurfaceSetAutoResolveDisableMaskData
{
	RdrSurface *surface;
	RdrSurfaceBufferMaskBits buffer_mask;
} RdrSurfaceSetAutoResolveDisableMaskData;


void rdrAllocBufferForSurfaceData(RdrSurfaceData *params);

#define RdrSurfaceFunctions \
\
			/* destructor*/\
			void (*destroy)(RdrSurface *surface);\
\
			/* properties*/\
			int (*changeParams)(RdrSurface *surface, const RdrSurfaceParams *params);\
\
			void (*setWriteMask)(RdrSurface *surface, RdrWriteMask mask);\
\

struct RdrSurfaceFunctionsArray { RdrSurfaceFunctions };

typedef struct RdrSurface
{
	union {
		RdrSurface *surface;
		RdrSurfaceDX *surface_xbox;
		RdrSurfaceWinGL *surface_gl;
	};
	union {
        struct {
            RdrSurfaceFunctions
        };
		void *surface_functions_array[sizeof(struct RdrSurfaceFunctionsArray)/sizeof(void*)];
	};

	// device pointer
	RdrDevice *device;

	BitArray used_snapshot_indices;

	int width_nonthread, height_nonthread;
	int vwidth_nonthread, vheight_nonthread;

	RdrSurfaceParams params_nonthread;
	bool destroyed_nonthread;

	RdrTexFlags default_tex_flags[SBUF_MAXMRT];

} RdrSurface;

// Use to set surface dimensions to calculated values where they may be degenerate (zero) due to 
// the calculations. The function implicitly fixes degenerate dimensions to one.
__forceinline void rdrSurfaceParamSetSizeSafe(RdrSurfaceParams *params, U32 width, U32 height)
{
	params->width = MAX(width, 1);
	params->height = MAX(height, 1);
}

U32 rdrGetAvailableSurfaceSnapshotIndex(RdrSurface *surface);
void rdrReleaseSurfaceSnapshotIndex(RdrSurface *surface, U32 snapshot_index);

RdrTexFlags rdrGetDefaultTexFlagsForSurfaceBufferType(RdrSurfaceBufferType type);
void rdrSetDefaultTexFlagsForSurfaceParams(RdrSurfaceParams *params);

const char *rdrGetSurfaceBufferTypeNameString(RdrSurfaceBufferType type);
