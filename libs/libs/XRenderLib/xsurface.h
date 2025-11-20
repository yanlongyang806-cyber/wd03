#ifndef _XSURFACE_H_
#define _XSURFACE_H_
#pragma once
GCC_SYSTEM

#include "wininclude.h"
#include "RdrSurface.h"
#include "thread\rt_xstate.h"
#include "xdx.h"

#if _WIN32
	#ifndef D3DVECTOR4_DEFINED
	typedef struct _D3DVECTOR4 {
		float x;
		float y;
		float z;
		float w;
	} D3DVECTOR4;
	typedef struct _D3DVECTOR4 *LPD3DVECTOR4;
	#define D3DVECTOR4_DEFINED
	#endif // D3DVECTOR4_DEFINED
#endif



#if !PLATFORM_CONSOLE
#include "../../3rdparty/NVPerfSDK/nvapi.h"
typedef NVDX_ObjectHandle NVObject;
#endif

typedef struct RdrDeviceDX RdrDeviceDX;
typedef struct EventOwner EventOwner;

typedef enum RxbxSurfaceType
{
	SURF_UNINITED = 0,
	SURF_PRIMARY,
	SURF_SINGLE,
	SURF_DOUBLE,
	SURF_QUADRUPLE,
} RxbxSurfaceType;

typedef struct RdrPerSnapshotState
{
	RdrTexHandle tex_handle;
	RdrTextureObj d3d_texture;
	RdrSurfaceObj snapshot_view;
	RdrTextureBufferObj d3d_buffer;
	const char *name; // for debugging
	U8 bind_count;
} RdrPerSnapshotState;

typedef struct RxbxSurfacePerTargetState {
	RdrSurfaceObj d3d_surface;
	RdrSurfaceObj d3d_surface_readonly;

	RdrPerSnapshotState *textures; // dynArray with entry per snapshot, entry zero is the texture buffer of the surface itself
	int texture_count;
	bool borrowed_texture;
	int size; // Memory usage
#if !PLATFORM_CONSOLE
	NVObject nv_surface;
#endif
} RxbxSurfacePerTargetState;

typedef enum RdrNVIDIACSAAMode
{
	RNVCSAA_NotSupported,
	RNVCSAA_Standard	= 1<<0,
	RNVCSAA_Quality	    = 1<<1,

	RdrNVIDIACSAAMode_AllBits = RNVCSAA_Standard | RNVCSAA_Quality,
} RdrNVIDIACSAAMode;

typedef struct RdrSurfaceDX
{
	// RdrSurface MUST be first in struct!
	RdrSurface surface_base;

	// type of surface
	RxbxSurfaceType type;

	int width_thread, height_thread;
	RdrSurfaceParams params_thread;

	RdrSurfaceStateDX state;

	RxbxSurfacePerTargetState rendertarget[SBUF_MAX];
	RdrSurfaceBufferMaskBits active_buffer_write_mask;
	RdrSurfaceFace active_face;

#if _XBOX
	int tile_count;
	D3DRECT *tile_rects;
#endif

	RdrSurfaceFlags creation_flags;
	RdrSurfaceBufferType buffer_types[SBUF_MAXMRT];

	RdrSurfaceDX *depth_surface;

	NVObject nv_tex_handle;
	NVObject nv_depth_handle;

	int multisample_count;
	DWORD multisample_quality;

	EventOwner	*event_timer;

	U32			d3d_textures_read_index:RDRSURFACE_SET_INDEX_NUMBITS;
	U32			d3d_textures_write_index:RDRSURFACE_SET_INDEX_NUMBITS;
	U32			bind_count;

	RdrSurfaceBufferMaskBits draw_calls_since_resolve_stack;
	RdrSurfaceBufferMaskBits draw_calls_since_resolve;
	RdrSurfaceBufferMaskBits auto_resolve_disable_mask;

	U32			state_inited:1;
	U32			do_zpass:1;
	U32			use_render_to_texture:1;
	U32			supports_post_pixel_ops:1;

} RdrSurfaceDX;


void rxbxInitPrimarySurface(RdrDeviceDX *device);
RdrSurface *rxbxCreateSurface(RdrDevice *device, const RdrSurfaceParams *params);
void rxbxRenameSurface(RdrDevice *device, RdrSurface *surface, const char *name);

__forceinline static U32 rxbxSurfaceFlagsGetMRTCount(U32 flags)
{
	int num_surfaces = 1;
	if (flags & SF_MRT4)
		num_surfaces = 4;
	else
		if (flags & SF_MRT2)
			num_surfaces = 2;
	return num_surfaces;
}

__forceinline static U32 rxbxGetSurfaceMRTCount(const RdrSurfaceDX * surface)
{
	return rxbxSurfaceFlagsGetMRTCount(surface->params_thread.flags);
}

#endif //_XSURFACE_H_
