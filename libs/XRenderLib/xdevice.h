#ifndef _XDEVICE_H
#define _XDEVICE_H
#pragma once
GCC_SYSTEM

#include "wininclude.h"
#include "BitArray.h"
#include "StashTable.h"
#include "RdrDevice.h"
#include "RdrGeometry.h"
#include "RdrState.h"
#include "xdx.h"
#include "xsurface.h"
#include "thread\rt_xstate.h"
#include "earray.h"
#include "memlog.h"
#include "timing.h"

#if !PLATFORM_CONSOLE
#include <winuser.h>
#endif

#include "../../../3rdparty/NvidiaTXAA/Txaa.h"

#if MAKE_INTELLISENSE_HAPPY
#define _XBOX
#endif

#define DEBUG_DRAW_CALLS 0

// Names of supported DirectX message types. These are used in generating message keys,
// so must conform to key name requirements.
#define DEVICETYPE_D3D9 "Direct3D9"
#define DEVICETYPE_D3D9EX "Direct3D9Ex"
#define DEVICETYPE_D3D11 "Direct3D11"

typedef enum DrawCallDebugMode
{
	DCDM_Off,

	DCDM_LessEqual,
	DCDM_Equal,
	DCDM_GreaterEqual,
	DCDM_NotEqual,
};

typedef enum RdrDeviceLossState
{
	RDR_OPERATING,

	RDR_LOST_FOCUS,
	RDR_FORCE_RESET_SETTINGS_CHANGED,
	RDR_LOST_DRIVER_INTERNAL_ERROR,
} RdrDeviceLossState;

#define MAX_VDECL_IDX 8

#define VALIDATE_D3D_DEVICE 0
#if VALIDATE_D3D_DEVICE
	#define VALIDATE_DEVICE_DEBUG()															\
		{																					\
			DWORD nD3DPasses = 0;															\
			IDirect3DDevice9_ValidateDevice(device->d3d_device, &nD3DPasses);				\
		}
#else
	#define VALIDATE_DEVICE_DEBUG()
#endif

typedef struct RdrGeometryDataDX RdrGeometryDataDX;
typedef struct RdrTextureDataDX RdrTextureDataDX;
typedef struct RxbxOcclusionQuery RxbxOcclusionQuery;
typedef struct RxbxCursor RxbxCursor;
typedef struct RdrDeviceDX RdrDeviceDX;
typedef struct RxbxFMV RxbxFMV;
typedef struct WTCmdPacket WTCmdPacket;
typedef struct LinearAllocator LinearAllocator;

typedef enum
{
	// create and destroy
	RXBXCMD_CREATE = RDRCMD_MAX,
	RXBXCMD_DESTROY,

	// process messages
	RXBXCMD_PROCESSWINMSGS,

	// swap buffers
	RXBXCMD_PRESENT,

	// surfaces
	RXBXCMD_SURFACE_INIT,
	RXBXCMD_SURFACE_FREE,

	// cursor
	RXBXCMD_SETCURSOR,
	RXBXCMD_SETCURSOR_FROM_DATA,
	RXBXCMD_DESTROYCURSOR,
	RXBXCMD_DESTROYACTIVECURSOR,

	RXBXCMD_HANDLEDEVICELOSS
} RxbxCmdType;

typedef struct RdrDeviceModeDX 
{
	RdrDeviceMode base;
	DXGI_MODE_DESC dxgi_mode;
	UINT dx9_adapter;
	D3DDISPLAYMODEEX d3d9_mode;
} RdrDeviceModeDX;

typedef struct RdrVBOMemoryDX
{
	RdrDeviceDX *device;
	RdrVertexBufferObj vbo;
	int size_bytes;
	int used_bytes;
	U32 last_frame_used;
} RdrVBOMemoryDX;

typedef struct RdrIBOMemoryDX
{
	RdrDeviceDX *device;
	RdrIndexBufferObj ibo;
	int size_bytes;
	int used_bytes;
	U32 last_frame_used;
} RdrIBOMemoryDX;

typedef enum RdrSurfaceBufferSupportXbox
{
	RSBS_NotSupported,
	RSBS_AsSurfaceWithPostPixOps	= 1<<0,
	RSBS_AsRTTexturePostPixOps		= 1<<1,
	RSBS_AsSurface					= 1<<2,
};

typedef enum RdrShaderType
{
	RDR_PIXEL_SHADER = 0,
	RDR_VERTEX_SHADER,
	RDR_HULL_SHADER,
	RDR_DOMAIN_SHADER,
} RdrShaderType;

typedef struct RxbxCompiledShader
{
	void *compiledResult;
	U32 compiledResultSize;
	ShaderHandle shader_handle;
	RdrShaderType shaderType;
	U32 is_vertex_shader : 1; // else pixel
	U32 do_not_create_device_object : 1; // for when we want to compile a shader, but don't actually want to create the device object
} RxbxCompiledShader;

typedef struct RxbxOcclusionQuery {
	RxbxOcclusionQuery *next;
	RdrQueryObj query;
	RxbxPixelShader *pshader;
	RdrOcclusionQueryResult *rdr_query;
	U32 frame;
} RxbxOcclusionQuery;

typedef struct RxbxInputSignature
{
	U32 crc;
	void *data;
	int data_size;
} RxbxInputSignature;

typedef struct RxbxSyncQuery
{
	// Code has set the query to the issued state, by marking the GPU command point being monitored by the query.
	U32 bIssued : 1;
	// Set when a sync query overflows an iteration count,
	U32 bSyncStuck : 1;
	// Explicitly-declared, available bits in the bitfield.
	U32 reservedAvailable : 30;
	// The render thread frame upon which the sync query reached the maximum wait loop iterations, for tracing
	// the timeline of the query.
	U32 frameStuck;
	RdrQueryObj query;
} RxbxSyncQuery;

typedef struct RxbxProfilingQuery
{
	bool bIssued;
	RdrQueryObj query;
	UINT64 uTimeStamp;
	int iTimerIdx;
} RxbxProfilingQuery;

typedef struct ResolveSurfaceInfo
{
	D3D11_TEXTURE2D_DESC tex_desc;
	D3D11_TEXTURE2D_DESC src_tex_desc;
	int width, height;
} ResolveSurfaceInfo;

// An interval of the operating state, beginning on particular frame, and ending
// at the device's current frame, or the next entry in the set of intervals.
typedef struct RdrDeviceOperatingInterval
{
	U32 beginFrameIndex;					// Initial device frame at transition to this state interval.
	RdrDeviceLossState newDeviceState;		// The device state for this interval.
} RdrDeviceOperatingInterval;

#define RDR_DEVICE_OPER_STATE_HIST_COUNT 8

// A ring buffer of device operating state history.
// May be viewed as a graph of device->isLost (RdrDeviceLossState) over time in frames,
// we have a history of last N intervals.
// State =        |
//  D.I.E      3  |
//  Changing   2  |
//  Lost       1  |
//  Operating (0) +------------...---------+---->  
//                012...   Time(frames)    device->device_base.frame_count (current)
typedef struct RdrDeviceOperatingHistory
{
	// Current interval index of the state history. 
	U32 currentIndex;														
	// Prior N intervals.
	RdrDeviceOperatingInterval history[RDR_DEVICE_OPER_STATE_HIST_COUNT]; 
} RdrDeviceOperatingHistory;

typedef struct RdrDeviceDX
{
	// RdrDevice MUST be first in the struct!
	RdrDevice device_base;

#ifndef _M_X64
	U32 pad[1]; // RdrDeviceStateDX must be aligned
#endif
	RdrDeviceStateDX device_state;
	TxaaCtxDX txaa_context;
	bool disable_texture_and_target_apply;
	U32 material_disable_flags;

	IDirect3D9 *d3d9;
	IDirect3D9Ex *d3d9ex;
	D3DDevice *d3d_device;
	IDirect3DDevice9Ex *d3d_device_ex;
	ID3D11Device *d3d11_device;
	IDXGISwapChain * d3d11_swapchain;
	ID3D11DeviceContext * d3d11_imm_context;
	D3DPRESENT_PARAMETERS *d3d_present_params;
	DXGI_SWAP_CHAIN_DESC *d3d11_swap_desc;
	int d3d11_swap_interval;
	HWND hWindow;
	DisplayParams display_thread;
	int notify_settings_frame_count;
	U32 device_info_index;
	U32 thread_id;


	int isLost; // See RdrDeviceLossState
	int warnCountBindDevLostTex;
	RdrDeviceOperatingHistory operatingHistory;

	// surfaces
	RdrSurfaceDX primary_surface;
	U32 primary_surface_mem_usage[2];
	RdrSurfaceDX *active_surface;
	RdrSurfaceDX *override_depth_surface;
	RdrSurfaceDX **surfaces;

	// cursor
	RxbxCursor *cursor;
	StashTable cursor_cache;
	HCURSOR last_set_cursor;
	U32 can_set_cursor:1;
	U32 reset_all_cursors:1;

#if !PLATFORM_CONSOLE
	// gamma
	U32 gamma_ramp_has_been_preserved:1;
	WORD preserved_ramp[256*3];
	F32 current_gamma;
	HDC hDCforGamma;
	bool everTouchedGamma;
#endif

	// cached data
	StashTable texture_data;
	StashTable geometry_cache;

	ShaderHandle *minimal_vertex_shaders;
	ShaderHandle *special_vertex_shaders;
	ShaderHandle *default_pixel_shaders;
	ShaderHandle *default_hull_shaders;
	ShaderHandle *default_domain_shaders;
	RxbxVertexShader *error_vertex_shader;
	RxbxHullShader *error_hull_shader;
	RxbxDomainShader *error_domain_shader;
	RxbxPixelShader *error_pixel_shader;

	StashTable standard_vertex_shaders_table;
	ShaderHandleAndFlags **standard_vertex_shaders_mem; // chunks of memory used to store the shader handles+flags
	int standard_vertex_shaders_mem_left; // index into most recent chunk

	RxbxCompiledShader **compiledShaderQueue; // Results of background compiling

	StashTable vertex_shaders;
	StashTable pixel_shaders;
	StashTable hull_shaders;
	StashTable domain_shaders;
	StashTable vertex_declarations;

	StashTable lpc_crctable;

	StashTable d3d11_rasterizer_states;
	StashTable d3d11_blend_states;
	StashTable d3d11_depth_stencil_states;
	StashTable d3d11_sampler_states;
	LinearAllocator *d3d11_state_keys;

	RdrVertexDeclarationObj primitive_vertex_declaration;
	RdrVertexDeclarationObj primitive_vertex_textured_declaration;
	RdrVertexDeclarationObj postprocess_screen_vertex_declaration, postprocess_screen_vertex_sprite_declaration, postprocess_shape_vertex_declaration;
    RdrVertexDeclarationObj postprocess_screen_ex_vertex_declaration;
	RdrVertexDeclarationObj sprite_vertex_declaration;
	RdrVertexDeclarationObj quad_vertex_declaration;
	RdrVertexDeclarationObj particle_vertex_declaration;
	RdrVertexDeclarationObj fast_particle_vertex_declaration;
	RdrVertexDeclarationObj fast_particle_streak_vertex_declaration;
	RdrVertexDeclarationObj cylinder_trail_vertex_declaration;
	RdrVertexDeclarationObj cloth_mesh_vertex_declaration;
	RdrVertexDeclarationObj cloth_mesh_vertex_declaration_no_normalmap;

	// earrays used for logging, they do not own their objects
	U32	count_surfaces;
	RdrSurfaceObj *eaSurfaces;

	U32	count_vertex_declarations;
	RdrVertexDeclarationObj *eaVertexDeclarations;

	U32	count_textures;
	RdrTextureDataDX **eaTextures;
	U32	count_cubetextures;
	RdrTextureDataDX **eaCubeTextures;
	U32	count_volumetextures;
	RdrTextureDataDX **eaVolumeTextures;

	U32	count_vertexbuffers;
	RdrVertexBufferObj *eaVertexBuffers;
	U32	count_indexbuffers;
	RdrIndexBufferObj *eaIndexBuffers;

	U32	count_queries;
	RdrQueryObj *eaQueries;

	U32	count_vertexshaders;
	RdrVertexShaderObj *eaVertexShaders;
	U32	count_pixelshaders;
	RdrPixelShaderObj *eaPixelShaders;

	U32 quad_index_list_count;
	RdrIndexBufferObj quad_index_list_ibo;
	//

	RdrVBOMemoryDX **vbo_memory;
	RdrIBOMemoryDX **ibo_memory;
	RdrIBOMemoryDX **ibo32_memory;

	struct 
	{
		RdrIndexBufferObj index_buffer;
		U32 index_buffer_size;
		bool is_32_bit;
	} sprite_index_buffer_info[2];
	U32 current_sprite_index_buffer;

	// misc
	TexHandle white_tex_handle, white_depth_tex_handle, black_tex_handle;

	// soft particles
	TexHandle soft_particle_depthbuffer;

	//ssao
	F32	ssao_direct_illum_factor;

	// For per-shader profiling via occlusion queries
	U32 frame_count_xdevice;
#if !_PS3
	RxbxOcclusionQuery *last_pshader_query;
#endif
	RxbxOcclusionQuery *last_rdr_query;
	RxbxOcclusionQuery *outstanding_queries; // Linked list
	RxbxOcclusionQuery *outstanding_queries_tail; // tail of the linked list
	RxbxOcclusionQuery *free_queries; // Linked list

	// Specialized Textures
	TexHandle stereoscopic_tex;

#if !PLATFORM_CONSOLE
	WNDCLASSEXA windowClass;
	WNDCLASSEXW windowClassW;
// Ensure consistency with registerWindowClass for class name length requirements.
#define MAX_WINDOW_CLASS_LENGTH 32
	char windowClassName[MAX_WINDOW_CLASS_LENGTH];
	wchar_t windowClassNameW[MAX_WINDOW_CLASS_LENGTH];

	int screen_x_pos, screen_y_pos;
	int screen_width_restored, screen_height_restored;
	int screen_x_restored, screen_y_restored;
	int device_width, device_height;
	int refresh_rate;

	U8 surface_types_multisample_supported[SBT_NUM_TYPES];
	RdrNVIDIACSAAMode surface_types_nvidia_csaa_supported[SBT_NUM_TYPES];
#endif

	U8 surface_types_post_pixel_supported[SBT_NUM_TYPES];

	RxbxInputSignature *input_signatures; // AST(BLOCK_EARRAY)

	RxbxFMV **active_fmvs;
	U32 fmv_inited:1;

	RdrQueryObj flush_query;

	// queries to keep us from getting too far ahead of the GPU
	RxbxSyncQuery sync_query[16];
	int sync_query_index;

	// queries for GPU profiling
	bool bProfilingQueries;
	RdrQueryObj disjoint_query[3];
	int iNextFreeDisjointQuery;
	int iFirstWaitingDisjointQuery;
	int iNumWaitingQueries;
	RxbxProfilingQuery profiling_query[128];
	int profiling_query_index;

	int frame_sync_query_frame_indices[8];
	int sprite_draw_sync_query_frame_indices[8];
	int cur_frame_index;

	WPARAM last_size_wParam;
	LPARAM last_size_lParam;

#if !_PS3
	StashTable stPixelShaderCache; // Cache by CRC to prevent creating duplicate shaders
#endif

#if _PS3
#elif _XBOX
	U32 owns_constants:1; // Whether we have declared that we own constants 20-23
#else
	D3DCAPS9 rdr_caps_d3d9_debug; // Not actually referenced at runtime, just for debug
	D3D_FEATURE_LEVEL d3d11_feature_level; // Should not reference this after init, just for debugging
	struct {
		RdrFeature features_supported;
		U32 supports_sm30_plus:1;
		U32 supports_independent_write_masks:1;
		int max_anisotropy;
	} rdr_caps_new;

	RdrTexFlags unsupported_tex_flags;

	U32 caps_filled : 1;
	U32 allow_windowed_fullscreen : 1;
	U32 inactive_display : 1;
	U32 inactive_app : 1;
	U32 in_scene : 1;
	U32 after_scene_before_present : 1;
	U32 in_present : 1;
    U32 allow_any_size : 1;
	U32 vfetch_r32f_supported : 1;
	U32 nvidia_rawz_supported : 1;
	U32 nvidia_intz_supported : 1;
	U32 null_supported : 1;
	U32 ati_df16_supported : 1;
	U32 ati_df24_supported : 1;
	U32 ati_instancing_supported : 1;
	U32 ati_resolve_msaa_z_supported : 1;
	U32 dxt_volume_tex_supported : 1;
	U32 alpha_to_coverage_supported_nv : 1; // also Intel
	U32 alpha_to_coverage_supported_ati : 1;
	U32 doing_fullscreen_toggle:1;
	U32 is_always_on_top:1;
	U32 txaa_supported : 1;
#endif

	U32 loaded_minimal_pixel_shaders:1;
	U32 loaded_default_pixel_shaders:1;

	U32 skip_next_present : 1;
	U32 needs_present : 1;
	U32 interactive_resizing:1;
	U32 interactive_resizing_got_size:1;

	U32 bind_texture_recursive_call : 1;
#if !PLATFORM_CONSOLE
	U32 debug_in_reset : 1;
	U32 nv_api_support : 1;
	U32 nvidia_csaa_supported : 1;
	U32 bIsDuringCreateDevice : 1;
	U32 bResolveSubresourceValidationErrorIssued : 1;
	U32 bCopySubresourceRegionValidationErrorIssued : 1;
	U32 bCopyResourceValidationErrorIssued : 1;
	U32 bEnableNVStereoscopicAPI : 1;
	ResolveSurfaceInfo lastResolveSubresourceParams;
	ResolveSurfaceInfo lastCopySubresourceRegionParams;
	ResolveSurfaceInfo lastCopyResourceParams;

	U32 reset_fail_count;
#endif

#if DEBUG_DRAW_CALLS
	int drawCallNumber;
	int drawCallNumberIsolate;
	int drawCallDebugMode;
#endif

	DXGI_SWAP_CHAIN_DESC d3d11_swap_desc_for_debug;
	D3DPRESENT_PARAMETERS d3d_present_params_for_debug;

} RdrDeviceDX;

#if _XBOX 
    #define PACKED_NORMAL_DECLTYPE D3DDECLTYPE_HEND3N
#else
    #define PACKED_NORMAL_DECLTYPE D3DDECLTYPE_SHORT4N
#endif

typedef enum VertexComponents
{
	VPOS,
	VPOSSPRITE,
	VNORMAL_PACKED,
	VNORMAL32,
	VTEXCOORD16,
	V2TEXCOORD16,
	VTEXCOORD32,
	V2TEXCOORD32,
	VLIGHT,
	VCOLORU8,
	VCOLORU8_IDX0,
	VCOLOR,
	VTANGENT_PACKED,
	VTANGENT32,
	VBINORMAL_PACKED,
	VBINORMAL32,
	// You can not use this generic float with VBONEWEIGHT, since they occupy the same register
	VFLOAT,
	VTIGHTENUP,
	VALPHA,
	VBONEIDX,

	VSMALLIDX,
	VBONEWEIGHT,
	VBONEWEIGHT32,
	VPOS2,
	VNORMAL2_PACKED,

	VINST_MATX,
	VINST_MATY,
	VINST_MATZ,
	VINST_COLOR,
	VINST_PARAM,

	VINDEX,

	VTERMINATE,

	VERTEX_COMPONENT_MAX
} VertexComponents;

extern D3DVERTEXELEMENT9 vertex_components9[];
extern D3D11_INPUT_ELEMENT_DESC vertex_components11[];

#define MAX_VERTEX_ELEMENTS 20

typedef struct VertexDecl
{
	int bDX11;
	union 
	{
		D3DVERTEXELEMENT9 elements9[ MAX_VERTEX_ELEMENTS ];
		D3D11_INPUT_ELEMENT_DESC elements11[ MAX_VERTEX_ELEMENTS ];
	};
	int num_elements;
} VertexDecl;

typedef struct VertexComponentInfo
{
	U32 offset;
	VertexComponents component_type;
} VertexComponentInfo;

__forceinline int rxbxVertexDeclAddElement(VertexDecl * decl, int component_type, U32 offset, U32 stream, int instanced)
{
	int num_elements = decl->num_elements;
	assert(component_type < VERTEX_COMPONENT_MAX);
	if (!decl->bDX11)
	{
		decl->elements9[num_elements] = vertex_components9[component_type];
		decl->elements9[num_elements].Stream = stream;
		decl->elements9[num_elements].Offset = offset;
	}
	else
	{
		decl->elements11[num_elements] = vertex_components11[component_type];
		decl->elements11[num_elements].InputSlot = stream;
		decl->elements11[num_elements].AlignedByteOffset = offset;
		if (!instanced)
		{
			decl->elements11[num_elements].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
			decl->elements11[num_elements].InstanceDataStepRate = 0;
		} else {
			decl->elements11[num_elements].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
			decl->elements11[num_elements].InstanceDataStepRate = 1;
		}
	}

	decl->num_elements = ++num_elements;
	return num_elements;
}

__forceinline int rxbxVertexDeclEnd(VertexDecl * decl)
{
	int num_elements = decl->num_elements;
	if (!decl->bDX11)
		decl->elements9[num_elements] = vertex_components9[VTERMINATE];

	return num_elements;
}

HRESULT rxbxCreateD3D9Ex(IDirect3D9Ex ** ppD3D9Ex);

HRESULT rxbxCreateVertexDeclarationFromDecl(RdrDeviceDX *device, const VertexDecl * decl, RxbxVertexShader * shader, RdrVertexDeclarationObj *vertex_declaration);
HRESULT rxbxCreateVertexDeclarationFromComponents(RdrDeviceDX *device, const VertexComponentInfo * components, int num_components, RxbxVertexShader * shader, RdrVertexDeclarationObj *vertex_declaration);

int rxbxSupportsFeature(RdrDevice *device, RdrFeature feature);
void rxbxUpdateMaterialFeatures(RdrDevice *device);
const char *rxbxGetIntrinsicDefines(RdrDevice *device);

void rxbxBeginNamedEvent(RdrDevice *device, void* data, WTCmdPacket *packet);
void rxbxEndNamedEvent(RdrDevice *device, void* data, WTCmdPacket *packet);
void rxbxPerfMarkerColor(RdrDeviceDX *device, const char * marker_string, U32 color);
__forceinline void rxbxPerfMarkerRGB(RdrDeviceDX *device, const char * marker_string, U8 r, U8 g, U8 b)
{
	rxbxPerfMarkerColor(device, marker_string, D3DCOLOR_ARGB(0xff,r,g,b));
}

__forceinline void rxbxPerfMarker(RdrDeviceDX *device, const char * marker_string)
{
	rxbxPerfMarkerColor(device, marker_string, D3DCOLOR_ARGB(0xff,0xff,0xff,0xff));
}

#if _PS3
#else
// For verifying object counts to check leaks
#define ENABLE_COUNT_D3D_OBJECTS	1
// For identifying usage, leaking objects
#define ENABLE_LOG_D3D_OBJECTS		1
// For printing a trace of object lifetime to the debugger

// Enable to log the create/release lifetime of all D3D objects.
#define ENABLE_TRACE_D3D_OBJECTS	0

__forceinline int IUnknown_GetRefCount(IUnknown * object)
{
	if (!object)
		return 0;
	object->lpVtbl->AddRef(object);
	return object->lpVtbl->Release(object);
}
#endif

#if ENABLE_TRACE_D3D_OBJECTS
#include "utils.h"
#define LOG_TRACE_CREATE_D3D_OBJECT(object, d3dobj)		{ OutputDebugStringf("Create %s: 0x%08p %d\n", #object, (d3dobj), IUnknown_GetRefCount((IUnknown*)d3dobj) ); }
#define LOG_TRACE_RELEASE_D3D_OBJECT(object, refcnt)	{ OutputDebugStringf("Release %s: 0x%08p %d\n", #object, (object), refcnt ); }
#else
#define LOG_TRACE_CREATE_D3D_OBJECT(object, d3dobj)
#define LOG_TRACE_RELEASE_D3D_OBJECT(object, refcnt)
#endif

#define COUNT_CREATE_D3D_OBJECT(counter)	++device->counter
#define COUNT_RELEASE_D3D_OBJECT(counter)	--device->counter
#define LOG_CREATE_D3D_OBJECT(ea, object, d3dobj)	{ eaPush((cEArrayHandle*)&device->ea, *(void**)&object); LOG_TRACE_CREATE_D3D_OBJECT(object, d3dobj); }
#define LOG_RELEASE_D3D_OBJECT(ea, object, refcnt)	\
	{ int index = eaFindAndRemoveFast((cEArrayHandle*)&device->ea, *(void**)&object); LOG_TRACE_RELEASE_D3D_OBJECT(object, refcnt); devassert(index >= 0); }

#if _PS3
#define rxbxGetD3DTexture(x) 0
#else
__forceinline D3DBaseTexture * rxbxGetD3DTexture(RdrTextureDataDX * texture)
{
	return ((D3DBaseTexture**)texture)[0];
}
#endif

#if ENABLE_COUNT_D3D_OBJECTS
	#if ENABLE_LOG_D3D_OBJECTS
		#define LOG_CREATE(counter, ea, object, d3dobj)		\
			COUNT_CREATE_D3D_OBJECT(counter);				\
			LOG_CREATE_D3D_OBJECT(ea, object, d3dobj)
		#define LOG_RELEASE(counter, ea, object, refcnt)	\
			COUNT_RELEASE_D3D_OBJECT(counter);				\
			LOG_RELEASE_D3D_OBJECT(ea, object, refcnt)
	#else
		#define LOG_CREATE(counter, ea, object, d3dobj)		\
			COUNT_CREATE_D3D_OBJECT(counter)
		#define LOG_RELEASE(counter, ea, object, refcnt)	\
			COUNT_RELEASE_D3D_OBJECT(counter)
	#endif
#else
	#if ENABLE_LOG_D3D_OBJECTS
		#define LOG_CREATE(counter, ea, object, d3dobj)		\
			LOG_CREATE_D3D_OBJECT(ea, object, d3dobj)
		#define LOG_RELEASE(counter, ea, object, refcnt)	\
			LOG_RELEASE_D3D_OBJECT(ea, object, refcnt)
	#else
		#define LOG_CREATE(counter, ea, object, d3dobj)
		#define LOG_RELEASE(counter, ea, object, refcnt)
	#endif
#endif

#define LOG_SUSPICIOUS_D3D_REF_COUNTS ( 1 && !PLATFORM_CONSOLE )
#if !LOG_SUSPICIOUS_D3D_REF_COUNTS
#define LOG_REF_COUNT(type, object, refCount)
#else
void LOG_REF_COUNT(const char *type, void *object, int refCount);
#endif

__forceinline static void rxbxLogCreateSurface(RdrDeviceDX *device, RdrSurfaceObj surface)
{
	LOG_CREATE(count_surfaces, eaSurfaces, surface, surface.typeless_surface);
}

__forceinline static void rxbxLogReleaseSurface(RdrDeviceDX *device, RdrSurfaceObj surface, int refCount)
{
	LOG_RELEASE(count_surfaces, eaSurfaces, surface.typeless_surface, refCount);
	LOG_REF_COUNT("D3DSurface", surface.typeless_surface, refCount);
}
__forceinline static void rxbxLogCreateVertexDeclaration(RdrDeviceDX *device, RdrVertexDeclarationObj vertex_declaration)
{
	LOG_CREATE(count_vertex_declarations, eaVertexDeclarations, vertex_declaration, vertex_declaration.typeless_decl);
}

__forceinline static void rxbxLogReleaseVertexDeclaration(RdrDeviceDX *device, RdrVertexDeclarationObj vertex_declaration, int refCount)
{
	LOG_RELEASE(count_vertex_declarations, eaVertexDeclarations, vertex_declaration.typeless_decl, refCount);
	LOG_REF_COUNT("VertexDeclaration", vertex_declaration.typeless_decl, refCount);
}

__forceinline static void rxbxLogCreateTexture(RdrDeviceDX *device, RdrTextureDataDX *texture)
{
	LOG_CREATE(count_textures, eaTextures, texture, rxbxGetD3DTexture(texture));
}

__forceinline static void rxbxLogReleaseTexture(RdrDeviceDX *device, RdrTextureDataDX *texture, int refCount)
{
	LOG_RELEASE(count_textures, eaTextures, texture, refCount);
	LOG_REF_COUNT("D3DTexture", texture, refCount);
}

__forceinline static void rxbxLogCreateCubeTexture(RdrDeviceDX *device, RdrTextureDataDX *cubetexture)
{
	LOG_CREATE(count_cubetextures, eaCubeTextures, cubetexture, rxbxGetD3DTexture(cubetexture));
}

__forceinline static void rxbxLogReleaseCubeTexture(RdrDeviceDX *device, RdrTextureDataDX *cubetexture, int refCount)
{
	LOG_RELEASE(count_cubetextures, eaCubeTextures, cubetexture, refCount);
	LOG_REF_COUNT("D3DCubeTexture", cubetexture, refCount);
}

__forceinline static void rxbxLogCreateVolumeTexture(RdrDeviceDX *device, RdrTextureDataDX *volumetexture)
{
	LOG_CREATE(count_volumetextures, eaVolumeTextures, volumetexture, rxbxGetD3DTexture(volumetexture));
}

__forceinline static void rxbxLogReleaseVolumeTexture(RdrDeviceDX *device, RdrTextureDataDX *volumetexture, int refCount)
{
	LOG_RELEASE(count_volumetextures, eaVolumeTextures, volumetexture, refCount);
	LOG_REF_COUNT("D3DVolumeTexture", volumetexture, refCount);
}

__forceinline static void rxbxLogCreateVertexBuffer(RdrDeviceDX *device, RdrVertexBufferObj vertexbuffer)
{
	LOG_CREATE(count_vertexbuffers, eaVertexBuffers, vertexbuffer, vertexbuffer.typeless_vertex_buffer);
}

__forceinline static void rxbxLogReleaseVertexBuffer(RdrDeviceDX *device, RdrVertexBufferObj vertexbuffer, int refCount)
{
	LOG_RELEASE(count_vertexbuffers, eaVertexBuffers, vertexbuffer.typeless_vertex_buffer, refCount);
	LOG_REF_COUNT("RdrVertexBufferObj", vertexbuffer.typeless_vertex_buffer, refCount);
}

__forceinline static void rxbxLogCreateIndexBuffer(RdrDeviceDX *device, RdrIndexBufferObj indexbuffer)
{
	LOG_CREATE(count_indexbuffers, eaIndexBuffers, indexbuffer, indexbuffer.typeless_index_buffer);
}

__forceinline static void rxbxLogReleaseIndexBuffer(RdrDeviceDX *device, RdrIndexBufferObj indexbuffer, int refCount)
{
	LOG_RELEASE(count_indexbuffers, eaIndexBuffers, indexbuffer.typeless_index_buffer, refCount);
	LOG_REF_COUNT("RdrIndexBufferObj", indexbuffer.typeless_index_buffer, refCount);
}

__forceinline static void rxbxLogCreateQuery(RdrDeviceDX *device, RdrQueryObj query)
{
	LOG_CREATE(count_queries, eaQueries, query, query.typeless);
}

__forceinline static void rxbxLogReleaseQuery(RdrDeviceDX *device, RdrQueryObj query, int refCount)
{
	LOG_RELEASE(count_queries, eaQueries, query.typeless, refCount);
	LOG_REF_COUNT("D3DQuery", query.typeless, refCount);
}

__forceinline static void rxbxLogCreateVertexShader(RdrDeviceDX *device, RdrVertexShaderObj vertex_shader)
{
	LOG_CREATE(count_vertexshaders, eaVertexShaders, vertex_shader, vertex_shader.typeless);
}

__forceinline static void rxbxLogReleaseVertexShader(RdrDeviceDX *device, RdrVertexShaderObj vertex_shader, int refCount)
{
	LOG_RELEASE(count_vertexshaders, eaVertexShaders, vertex_shader.typeless, refCount);
	LOG_REF_COUNT("D3DVertexShader", vertex_shader.typeless, refCount);
}

__forceinline static void rxbxLogCreatePixelShader(RdrDeviceDX *device, RdrPixelShaderObj pixelshader)
{
	LOG_CREATE(count_pixelshaders, eaPixelShaders, pixelshader, pixelshader.typeless);
}

__forceinline static void rxbxLogReleasePixelShader(RdrDeviceDX *device, RdrPixelShaderObj pixelshader, int refCount)
{
	LOG_RELEASE(count_pixelshaders, eaPixelShaders, pixelshader.typeless, refCount);
	LOG_REF_COUNT("D3DPixelShader", pixelshader.typeless, refCount);
}

__forceinline static int rxbxReleaseVertexDeclaration(RdrDeviceDX *device, RdrVertexDeclarationObj vertex_declaration)
{
	int ref_count;
	if (device->d3d11_device)
		ref_count = ID3D11InputLayout_Release(vertex_declaration.layout);
	else
		ref_count = IDirect3DVertexDeclaration9_Release(vertex_declaration.decl);
	rxbxLogReleaseVertexDeclaration(device, vertex_declaration, ref_count);
	return ref_count;
}

typedef enum RdrBufferUsageFlags
{
	BUF_DEFAULT = 0, // Immutable/stored on device, 16-bit indices (for index buffers)
	BUF_DYNAMIC = 1<<0, // Updated at run-time
	BUF_MANAGED = 1<<1, // Preserved on device loss (cannot be managed and dynamic)
	BUF_32BIT_INDEX = 1<<2, // For index buffers, 32-bit indices, otherwise assume 16-bit
} RdrBufferUsageFlags;

__forceinline static HRESULT rxbxCreateVertexBuffer(RdrDeviceDX *device, UINT Length, RdrBufferUsageFlags usage, RdrVertexBufferObj *pVertexBuffer)
{
	HRESULT hr;
	if (device->d3d11_device)
	{
		D3D11_BUFFER_DESC desc;

		desc.ByteWidth = Length;
		if (usage&BUF_DYNAMIC)
		{
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		}
		else
		{
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.CPUAccessFlags = 0;
		}
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;

		hr = ID3D11Device_CreateBuffer(device->d3d11_device, &desc, NULL, &pVertexBuffer->vertex_buffer_d3d11);
	} else {
		hr = IDirect3DDevice9_CreateVertexBuffer(device->d3d_device, Length, D3DUSAGE_WRITEONLY|((usage&BUF_DYNAMIC)?D3DUSAGE_DYNAMIC:0), 0,
			(!device->d3d9ex && (usage & BUF_MANAGED))?D3DPOOL_MANAGED:D3DPOOL_DEFAULT, &pVertexBuffer->vertex_buffer_d3d9, NULL);
	}
	if (!FAILED(hr))
		rxbxLogCreateVertexBuffer(device, *pVertexBuffer);
	return hr;
}

__forceinline static int rxbxReleaseVertexBuffer(RdrDeviceDX *device, RdrVertexBufferObj vertexbuffer)
{
	int ref_count;
	if (device->d3d11_device)
		ref_count = ID3D11Buffer_Release(vertexbuffer.vertex_buffer_d3d11);
	else
		ref_count = IDirect3DVertexBuffer9_Release(vertexbuffer.vertex_buffer_d3d9);
	rxbxLogReleaseVertexBuffer(device, vertexbuffer, ref_count);
	return ref_count;
}

__forceinline static HRESULT rxbxCreateIndexBuffer(RdrDeviceDX *device, UINT Length,
										   RdrBufferUsageFlags usage, RdrIndexBufferObj *pIndexBuffer)
{
	HRESULT hr;
	if (device->d3d11_device)
	{
		D3D11_BUFFER_DESC desc;

		desc.ByteWidth = Length;
		if (usage&BUF_DYNAMIC)
		{
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		}
		else
		{
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.CPUAccessFlags = 0;
		}
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;
		hr = ID3D11Device_CreateBuffer(device->d3d11_device, &desc, NULL, &pIndexBuffer->index_buffer_d3d11);
	} else {
		hr = IDirect3DDevice9_CreateIndexBuffer(device->d3d_device, Length,
			D3DUSAGE_WRITEONLY| ((usage & BUF_DYNAMIC)?D3DUSAGE_DYNAMIC:0), (usage & BUF_32BIT_INDEX)?D3DFMT_INDEX32:D3DFMT_INDEX16,
			(!device->d3d9ex && (usage & BUF_MANAGED))?D3DPOOL_MANAGED:D3DPOOL_DEFAULT, &pIndexBuffer->index_buffer_d3d9, NULL);
	}
	if (!FAILED(hr))
		rxbxLogCreateIndexBuffer(device, *pIndexBuffer);
	return hr;
}

__forceinline static int rxbxReleaseIndexBuffer(RdrDeviceDX *device, RdrIndexBufferObj indexbuffer)
{
	int ref_count;
	if (device->d3d11_device)
		ref_count = ID3D11Buffer_Release(indexbuffer.index_buffer_d3d11);
	else
		ref_count = IDirect3DIndexBuffer9_Release(indexbuffer.index_buffer_d3d9);
	rxbxLogReleaseIndexBuffer(device, indexbuffer, ref_count);
	return ref_count;
}

__forceinline static int rxbxReleaseQuery(RdrDeviceDX *device, RdrQueryObj query)
{
	int ref_count;
	if (device->d3d11_device)
	{
		ref_count = ID3D11Query_Release(query.query_d3d11);
	} else {
		ref_count = IDirect3DQuery9_Release(query.query_d3d9);
	}
	rxbxLogReleaseQuery(device, query, ref_count);
	return ref_count;
}

__forceinline HRESULT rxbxQueryGetData(RdrDeviceDX *device, RdrQueryObj query, void *data, UINT data_size, bool flush)
{
	HRESULT hr;
	PERFINFO_AUTO_START_FUNC();
	if (device->d3d11_device)
	{
		hr = ID3D11DeviceContext_GetData(device->d3d11_imm_context, query.asynch_d3d11, data, data_size, flush?0:D3D11_ASYNC_GETDATA_DONOTFLUSH);
	} else {
		hr = IDirect3DQuery9_GetData(query.query_d3d9, data, data_size, flush?D3DGETDATA_FLUSH:0);
	}
	PERFINFO_AUTO_STOP();
	return hr;
}

__forceinline HRESULT rxbxQueryGetOcclusionData(RdrDeviceDX *device, RdrQueryObj query, LARGE_INTEGER *data, bool flush)
{
	HRESULT hr;
	PERFINFO_AUTO_START_FUNC();
	data->HighPart = data->LowPart = 0;
	if (device->d3d11_device)
	{
		hr = ID3D11DeviceContext_GetData(device->d3d11_imm_context, query.asynch_d3d11, data, sizeof( *data ), flush?0:D3D11_ASYNC_GETDATA_DONOTFLUSH);
	} else {
		hr = IDirect3DQuery9_GetData(query.query_d3d9, data, sizeof( data->LowPart ), flush?D3DGETDATA_FLUSH:0);
	}
	PERFINFO_AUTO_STOP();
	return hr;
}

__forceinline void rxbxQueryEnd(RdrDeviceDX *device, RdrQueryObj query)
{
	PERFINFO_AUTO_START_FUNC();
	if (device->d3d11_device)
	{
		ID3D11DeviceContext_End(device->d3d11_imm_context, query.asynch_d3d11);
	} else {
		IDirect3DQuery9_Issue(query.query_d3d9, D3DISSUE_END);
	}
	PERFINFO_AUTO_STOP();
}

typedef enum RdrLockMode
{
	RDRLOCK_WRITEONCE,
	RDRLOCK_DISCARD,
	RDRLOCK_NOOVERWRITE,

	RDRLOCK_MAX
} RdrLockMode;

static int lock_flags_dx11[RDRLOCK_MAX] = {D3D11_MAP_WRITE_NO_OVERWRITE, D3D11_MAP_WRITE_DISCARD, D3D11_MAP_WRITE_NO_OVERWRITE};
static int lock_flags_dx9[RDRLOCK_MAX] = {0, D3DLOCK_DISCARD, D3DLOCK_NOOVERWRITE};

// offset=bytes=0 means map the whole thing
__forceinline void *rxbxIndexBufferLockWrite(RdrDeviceDX *device, RdrIndexBufferObj index_buffer, RdrLockMode lock_mode, int offset, int bytes, HRESULT *hr_out)
{
	HRESULT hr;
	void *ret;
	if (device->d3d11_device)
	{
		D3D11_MAPPED_SUBRESOURCE map_info;
		hr = ID3D11DeviceContext_Map(device->d3d11_imm_context, index_buffer.index_buffer_resource_d3d11,
			0, lock_flags_dx11[lock_mode], 0, &map_info);
		ret = (U8*)map_info.pData + offset;
	} else {
		hr = IDirect3DIndexBuffer9_Lock(index_buffer.index_buffer_d3d9, offset, bytes, &ret, lock_flags_dx9[lock_mode]);
	}
	if (FAILED(hr))
		ret = NULL;
	if (hr_out)
		*hr_out = hr;
	else
		rxbxFatalHResultErrorf(device, hr, "mapping index buffer", "");
	return ret;
}

__forceinline void rxbxIndexBufferUnlock(RdrDeviceDX *device, RdrIndexBufferObj index_buffer)
{
	if (device->d3d11_device)
	{
		ID3D11DeviceContext_Unmap(device->d3d11_imm_context, index_buffer.index_buffer_resource_d3d11, 0);
	} else {
		IDirect3DIndexBuffer9_Unlock(index_buffer.index_buffer_d3d9);
	}
}

__forceinline void *rxbxVertexBufferLockWrite(RdrDeviceDX *device, RdrVertexBufferObj vertex_buffer, RdrLockMode lock_mode)
{
	HRESULT hr;
	void *ret;
	if (device->d3d11_device)
	{
		D3D11_MAPPED_SUBRESOURCE map_info;
		hr = ID3D11DeviceContext_Map(device->d3d11_imm_context, vertex_buffer.vertex_buffer_resource_d3d11,
			0, lock_flags_dx11[lock_mode], 0, &map_info);
		ret = map_info.pData;
	} else {
		hr = IDirect3DVertexBuffer9_Lock(vertex_buffer.vertex_buffer_d3d9, 0, 0, &ret, lock_flags_dx9[lock_mode]);
	}
	rxbxFatalHResultErrorf(device, hr, "mapping vertex buffer", "");
	return ret;
}

__forceinline void rxbxVertexBufferUnlock(RdrDeviceDX *device, RdrVertexBufferObj vertex_buffer)
{
	if (device->d3d11_device)
	{
		ID3D11DeviceContext_Unmap(device->d3d11_imm_context, vertex_buffer.vertex_buffer_resource_d3d11, 0);
	} else {
		IDirect3DVertexBuffer9_Unlock(vertex_buffer.vertex_buffer_d3d9);
	}
}

__forceinline void rxbxCountQueryDrawCall(RdrDeviceDX *device)
{
	if (device->last_rdr_query)
		++device->last_rdr_query->rdr_query->draw_calls;
}

typedef enum DXGIEnumResults
{
	DXGIEnum_Complete,			// Enumeration handler is done with enumeration, and it should stop after the current output. Used to terminate a successful search, for example.
	DXGIEnum_RetryOutput,		// Enumeration handler wants to retry enumerating a device since DXGI indicates an output may have been added or removed changed (monitor added/removed) during enumeration.
	DXGIEnum_ContinueEnum,		// Enumeration handler indicates continue the enumeration, for enumerating all outputs and modes, for example.
	DXGIEnum_Fail,				// Enumeration handler indicates a fatal failure.
} DXGIEnumResults;

typedef DXGIEnumResults (*RdrHandleDXGIOutputDX)(IDXGIAdapter * pAdapter, UINT adapterNum, const DXGI_ADAPTER_DESC * pDesc, 
	IDXGIOutput * pOutput, const DXGI_OUTPUT_DESC * pOutputDesc, UINT monitorNum, void * pUserData);

int rxbxEnumDXGIOutputs(RdrHandleDXGIOutputDX handler, void * pUserData);

#endif //_XDEVICE_H

