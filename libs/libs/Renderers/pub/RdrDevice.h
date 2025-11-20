#ifndef _RDRDEVICE_H_
#define _RDRDEVICE_H_
GCC_SYSTEM

#include "mathutil.h"
#include "StringCache.h"

#include "WorkerThread.h"
#include "RdrSurface.h"
#include "RdrDrawable.h"
#include "windefinclude.h"

#define RDR_CURSOR_SIZE 48

#ifdef _M_X64
#define RDR_NVIDIA_TXAA_SUPPORT 0
#else
#define RDR_NVIDIA_TXAA_SUPPORT 1
#endif

//////////////////////////////////////////////////////////////////////////

typedef struct RdrDevice RdrDevice;
typedef struct RdrDrawList RdrDrawList;
typedef struct RdrShaderPerformanceValues RdrShaderPerformanceValues;
typedef struct RdrDeviceDX RdrDeviceDX;
typedef struct RdrDeviceWinGL RdrDeviceWinGL;
typedef struct EventOwner EventOwner;
typedef struct WindowCreateParams WindowCreateParams;
typedef struct MemLog MemLog;
typedef struct RdrLightDefinitionData RdrLightDefinitionData;
typedef struct RdrFMV RdrFMV;
typedef struct RdrCmdFenceData RdrCmdFenceData;

// Passing this to some query functions will give you information about our Xbox
//  renderer
#define DEVICE_XBOX ((RdrDevice*)-2)
#define DEVICE_PS3 ((RdrDevice*)-3)

//////////////////////////////////////////////////////////////////////////
// Device enumeration for user options

typedef struct RdrDeviceMode
{
	char *name; // e.g. Unique name for the mode
	int width;
	int height;
	int refresh_rate;
	int display_format;
} RdrDeviceMode;

typedef RdrDevice *(*RdrDeviceCreateFunction)(WindowCreateParams *params, HINSTANCE hInstance, int processor_idx);
typedef struct RdrDeviceInfo
{
	int monitor_index;
	HMONITOR monitor_handle;
	// The index or identifier of the device or adapter enumerated by a given API. This may be distinct GPUs, or 
	// different monitors on a single GPU.
	U32 adapter_index;			
	// The index or identifier of an output (i.e. a monitor) on an adapter enumerated by a given API. 
	// Disambiguates adapters and outputs for APIs the support multiple outputs from a single adapter (e.g. DXGI/DX11).
	U32 output_index;
	const char *name; // e.g. 
	const char *type; // e.g. Direct3D9
	RdrDeviceCreateFunction createFunction;
	RdrDeviceMode ** display_modes;
} RdrDeviceInfo;
typedef void (*RdrDeviceEnumFunc)(RdrDeviceInfo ***device_infos);
void rdrDeviceAddEnumFunc(RdrDeviceEnumFunc enumFunc);

// Gives an earray of available devices
const RdrDeviceInfo * const * rdrEnumerateDevices(void);
int rdrGetDeviceForMonitor(int preferred_monitor, const char * device_type);
RdrDevice *rdrCreateDevice(WindowCreateParams *params, HINSTANCE hInstance, int processor_idx);

//////////////////////////////////////////////////////////////////////////
// Device functions (the inlined functions are at the end of the file):

void rdrLockActiveDevice(RdrDevice *device, bool do_begin_scene);
bool rdrUnlockActiveDevice(RdrDevice *device, bool do_xlive_callback, bool do_end_scene, bool do_buffer_swap); // Returns true if we waited for the render thread
void rdrSyncDevice(RdrDevice *device);

#define rdrDestroyDevice(device) (device)->destroy(device)
#define rdrAppCrashed(device) (device)->appCrashed(device)

#define rdrProcessMessages(device) (device)->processMessages(device)

int rdrStatusPrintfFromDeviceThread(RdrDevice *device, const char *format, ...);

void rdrGetDeviceSize(RdrDevice * device, int * xpos, int * ypos, int * width, int * height, int * refresh_rate, int * fullscreen, int * maximized, int * windowed_fullscreen);
#define rdrIsDeviceInactive(device) (device)->isInactive(device, true)
#define rdrTestDeviceInactiveAndAttemptReactivate(device) (device)->isInactive(device, true)
#define rdrTestDeviceInactiveDontReactivate(device) (device)->isInactive(device, false)
#define rdrReactivate(device) (device)->reactivate(device)
#define rdrGetDeviceIdentifier(device) (device)->getIdentifier(device)
#define rdrGetDeviceProfileName(device) (device)->getProfileName(device)

#define rdrGetPrimarySurface(device) (device)->getPrimarySurface(device)
#define rdrCreateSurface(device, params) (device)->createSurface(device, params)
#define rdrRenameSurface(device, surface, name) (device)->renameSurface(device, surface, name)
#define rdrSetPrimarySurfaceActive(device) (device)->setPrimarySurfaceActive(device)

#define rdrSetCursorFromData(device, cursor_data) (device)->setCursorFromData(device, cursor_data)
#define rdrSetCursorFromCache(device, cursor_name) (device)->setCursorFromCache(device, cursor_name)

#define rdrSupportsFeaturePS3(device, feature) ((feature)&(0 \
			|FEATURE_ANISOTROPY \
			|FEATURE_NONPOW2TEXTURES \
			|FEATURE_NONSQUARETEXTURES \
			|FEATURE_MRT2 \
			|FEATURE_MRT4 \
			|FEATURE_SM20 \
			/*|FEATURE_SM2B*/ \
			|FEATURE_SM30 \
			|FEATURE_SRGB \
			/*|FEATURE_GF8*/ \
			/*|FEATURE_INSTANCING*/ \
			|FEATURE_VFETCH \
			|FEATURE_DEPTH_TEXTURE \
			|FEATURE_24BIT_DEPTH_TEXTURE \
			|FEATURE_STENCIL_DEPTH_TEXTURE \
			|FEATURE_DECL_F16_2 \
			|FEATURE_SBUF_FLOAT_FORMATS \
			|FEATURE_DXT_VOLUME_TEXTURE \
))

// returns 0 or 1
#define rdrSupportsFeature(device, feature) (device)->supportsFeature(device, feature)

#define rdrGetMaxPixelShaderConstants(device) (device)->getMaxPixelShaderConstants(device)

// returns 0 if not supported, otherwise it returns the maximum multisample level of the surface type
#define rdrSupportsSurfaceType(device, surface_type) (device)->supportsSurfaceType(device, surface_type)

#define rdrUpdateMaterialFeatures(device) (device)->updateMaterialFeatures(device)
#define rdrGetIntrinsicDefines(device) (device)->getIntrinsicDefines(device)

//////////////////////////////////////////////////////////////////////////

typedef struct DisplayParams
{
	int width, height, xpos, ypos;
	int preferred_monitor;		// Which monitor the device should be optimized for.  -1 autodetects based on position
	int preferred_adapter;
	int preferred_fullscreen_mode;
	int refreshRate;			// Output only; selected fullscreen display mode controls.
	U32 fullscreen : 1;
	U32 minimize : 1;
	U32 maximize : 1;
	U32 windowed_fullscreen : 1;
	U32 allow_windowed_fullscreen : 1; // Allow automatically entering windowed_fullscreen mode
	U32 vsync : 1;
	U32 srgbBackBuffer : 1;
	U32 hide : 1;
	U32 allow_any_size : 1;     //< Win32 enforces a minimum and
								//< maximum size on the window by
								//< default.  Setting this allows you
								//< to override those limits.
	U32 deviceOperating:1;		// Output only; non-zero indicates the rendering device is operating, zero means unavailable, e.g. due to Direct3D device loss.
	U32 stereoscopicActive:1;
} DisplayParams;

typedef struct WindowCreateParams
{
	const char *device_type;
	int icon;
	const char *window_title;
	DisplayParams display;
	U32 threaded : 1;
	U32 bNotifyUserOnCreateFail : 1;
	U32 bStereoEnable : 1;
} WindowCreateParams;


typedef struct RdrDeviceBeginSceneParams
{
	bool do_begin_scene;
	U32 frame_count_update;
} RdrDeviceBeginSceneParams;

typedef struct RdrDeviceEndSceneParams
{
	bool do_xlive_callback;
	bool do_end_scene;
	bool do_buffer_swap;
	U32 frame_count_update;
} RdrDeviceEndSceneParams;

typedef struct RdrDeviceMemlogParams
{
	MemLog *memlog;
	char buffer[512];
} RdrDeviceMemlogParams;


typedef void (*ShellExecuteCallback)(int iReturnValue, HWND hwnd, const char* lpOperation, const char* lpFile, const char* lpParameters, const char* lpDirectory, int nShowCmd );
typedef struct RdrShellExecuteCommands
{
	HWND hwnd;
	const char* lpOperation;
	const char* lpFile;
	const char* lpParameters;
	const char* lpDirectory;
	int nShowCmd;
	ShellExecuteCallback callback;
} RdrShellExecuteCommands;


//////////////////////////////////////////////////////////////////////////

typedef struct WinMsg
{
	UINT uMsg;
	LONG timeStamp;
	WPARAM wParam;
	LPARAM lParam;
} WinMsg;

typedef struct AlertErrorMsg
{
	int highlight;
	char *str, *title, *fault;
} AlertErrorMsg;

typedef struct RdrDeviceMemoryUsage
{
	const char *staticName;
	int count;
	U32 value;
} RdrDeviceMemoryUsage;

typedef struct RdrLightMappings
{
// 	int lightdir_vs_index;
// 	int diffuse_index;
// 	int secondary_diffuse_index;
// 	int specular_index;
	RdrLightDefinitionData *defs[RDRLIGHT_TYPE_MAX];
} RdrLightMappings;

AST_PREFIX( DEF(-1) )
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(" ");
typedef struct RdrDeviceStateChangePerfValues
{
	int vertex_shader;
	int vertex_texture;
	int vertex_texture_sampler_state;
	int vertex_declaration;
	int vertex_stream;
	int vertex_stream_frequency;
	int vertex_stream_comb;
	int vs_constants;

	int hull_shader;
	int domain_shader;

	int index_buffer;
	int indices_comb;

	int pixel_shader;
	int texture;
	int texture_sampler_state;
	int ps_constants;
	int ps_bool_constants;

	int hull_texture_sampler_state;
	int domain_texture_sampler_state;

	int viewport;
	int scissor_rect;

	// state blocks
	int rasterizer;
	int blend;
	int depth_stencil;

} RdrDeviceStateChangePerfValues;

AST_PREFIX( DEF(-1) )
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(" ");
typedef struct RdrDeviceConstantStateChangePerfValues
{
	int min_seq;
	int max_seq;
	int avg_seq;
	int num_seq;
} RdrDeviceConstantStateChangePerfValues;

enum
{
	RDR_SPRITE_SIZE_TINY,
	RDR_SPRITE_SIZE_SMALL,
	RDR_SPRITE_SIZE_MEDIUM_SMALL,
	RDR_SPRITE_SIZE_MEDIUM,
	RDR_SPRITE_SIZE_MEDIUM_LARGE,
	RDR_SPRITE_SIZE_LARGE,
	RDR_SPRITE_SIZE_HUGE,

	RDR_SPRITE_SIZE_BUCKET_COUNT,
};

extern F32 sprite_histogram_sizes[RDR_SPRITE_SIZE_BUCKET_COUNT];


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(" ");
typedef struct RdrDeviceOperationPerfValues
{
	int draw_call_count; // all types
	int triangle_count; // all types
	int sprite_draw_call_count;
	int sprite_triangle_count;
	int sprite_pixel_count;
	F32 sprite_draw_time;
	int sprite_count_histogram[RDR_SPRITE_SIZE_BUCKET_COUNT];		NO_AST
	int resolve_count;
	int resolve_pixel_count;
	int clear_count;
	int clear_pixel_count;
	int postprocess_count;
	int postprocess_pixel_count;
	int surface_active_count;
} RdrDeviceOperationPerfValues;
AST_PREFIX()

// Performance values for a given frame
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(" ");
typedef struct RdrDevicePerformanceValues
{
	RdrDeviceStateChangePerfValues state_changes;
	RdrDeviceOperationPerfValues operations;
	RdrDeviceConstantStateChangePerfValues vs_constant_changes;
} RdrDevicePerformanceValues;
extern ParseTable parse_RdrDevicePerformanceValues[];
#define TYPE_parse_RdrDevicePerformanceValues RdrDevicePerformanceValues

// Measured performance characteristics of the video card
typedef struct RdrDevicePerfTimes
{
	F32 pixelShaderFillValue;
	F32 msaaPerformanceValue;
	bool bFilledIn; // If getting filled in asynchronously, this gets set to true when it's done
} RdrDevicePerfTimes;

// Stencil operations
// Note: this enum must match the D3D definitions of stencil operations, or
// rxbxStencilOpHandler must translate into D3DSTENCILOP values.
typedef enum RdrStencilOp
{
#if _PS3
	RDRSTENCILOP_KEEP = 0,
	RDRSTENCILOP_ZERO           ,
	RDRSTENCILOP_REPLACE        ,
	RDRSTENCILOP_INCRSAT        ,
	RDRSTENCILOP_DECRSAT        ,
	RDRSTENCILOP_INVERT         ,
	RDRSTENCILOP_INCR           ,
	RDRSTENCILOP_DECR           ,
    	RDRSTENCILOP_DEFAULT = RDRSTENCILOP_KEEP,
#elif _XBOX
	RDRSTENCILOP_DEFAULT		= 0,
	RDRSTENCILOP_KEEP			= 0,
	RDRSTENCILOP_ZERO           = 1,
	RDRSTENCILOP_REPLACE        = 2,
	RDRSTENCILOP_INCRSAT        = 3,
	RDRSTENCILOP_DECRSAT        = 4,
	RDRSTENCILOP_INVERT         = 5,
	RDRSTENCILOP_INCR           = 6,
	RDRSTENCILOP_DECR           = 7,
#else
	RDRSTENCILOP_DEFAULT		= 0,
	RDRSTENCILOP_KEEP			= 1,
	RDRSTENCILOP_ZERO           = 2,
	RDRSTENCILOP_REPLACE        = 3,
	RDRSTENCILOP_INCRSAT        = 4,
	RDRSTENCILOP_DECRSAT        = 5,
	RDRSTENCILOP_INVERT         = 6,
	RDRSTENCILOP_INCR           = 7,
	RDRSTENCILOP_DECR           = 8,
#endif
} RdrStencilOp;

typedef struct RdrStencilFuncParams
{
	U32 enable:1;
	U8	ref;
	RdrPPDepthTestMode func;
	U32	mask;
} RdrStencilFuncParams;

typedef struct RdrStencilOpParams
{
	RdrStencilOp fail;
	RdrStencilOp zfail;
	RdrStencilOp pass;
} RdrStencilOpParams;

#define RDRSTENCIL_NO_MASK (-1)
#define RDRSTENCIL_ENABLE 1
#define RDRSTENCIL_DISABLE 0

typedef void (*RdrCmdFenceCallback)(RdrDevice *device, RdrCmdFenceData *cmdFenceData);

// See rdrCmdFenceEx and RDRCMD_CMDFENCE.
typedef struct RdrCmdFenceData
{
	const char *name; // Fence event name
	RdrCmdFenceCallback rdrThreadCmdCompleteCB;
	RdrCmdFenceCallback mainThreadCmdCompleteCB;
	void * user_data;
	bool bWaitForGPU;
} RdrCmdFenceData;

typedef enum RdrResizeFlags
{
	RDRRESIZE_DEFAULT,
	RDRRESIZE_PRECOMMIT_START,
	RDRRESIZE_PRECOMMIT_END,
	RDRRESIZE_PRECOMMIT_SKIPONE,

} RdrResizeFlags;


typedef enum
{
	RDRMSG_WINMSG = WT_CMD_USER_START,		// a windows message was received by the device, not guaranteed on all devices
	RDRMSG_ALERTMSG,						// the device is requesting an alert message box
	RDRMSG_ERRORMSG,						// the device is requesting an error message box
	RDRMSG_DESTROY,
	RDRMSG_SIZE,
	RDRMSG_DISPLAYPARAMS,
	RDRMSG_STATUS_PRINTF,
	RDRMSG_CMDFENCECOMPLETE,

} RdrMsgType;

typedef enum
{
	// lock and unlock
	RDRCMD_LOCKACTIVE = WT_CMD_USER_START,
	RDRCMD_UNLOCKACTIVE,

	// begin and end scene
	RDRCMD_BEGINSCENE,
	RDRCMD_ENDSCENE,

	// occlusion queries
	RDRCMD_FREEQUERY,
	RDRCMD_FLUSHQUERIES,

	// device window settings
	RDRCMD_WIN_SETTITLE,
	RDRCMD_WIN_SETSIZE,
	RDRCMD_WIN_SETPOSANDSIZE,
	RDRCMD_WIN_SETICON,
	RDRCMD_WIN_SETVSYNC,
	RDRCMD_WIN_SETGAMMA,
	RDRCMD_WIN_GAMMANEEDRESET,
	RDRCMD_WIN_SHOW,
	RDRCMD_SHELL_EXECUTE,

	// surfaces
	RDRCMD_SURFACE_RESETACTIVESTATE,
	RDRCMD_SURFACE_SETACTIVE,
	RDRCMD_OVERRIDE_DEPTH_SURFACE,
	RDRCMD_SURFACE_GETDATA,
	RDRCMD_SURFACE_SNAPSHOT,
	RDRCMD_SURFACE_SETDEPTHSURFACE,
	RDRCMD_SURFACE_SETFOG,
	RDRCMD_SURFACE_RESTORE,
	RDRCMD_SURFACE_RESOLVE,
	RDRCMD_SURFACE_UPDATEMATRICES,
	RDRCMD_SURFACE_SET_AUTO_RESOLVE_DISABLE_MASK,
	RDRCMD_SURFACE_PUSH_AUTO_RESOLVE_MASK,
	RDRCMD_SURFACE_POP_AUTO_RESOLVE_MASK,
	RDRCMD_SURFACE_SWAP_SNAPSHOTS,
	RDRCMD_SURFACE_QUERY_ALL,

	// shaders
	RDRCMD_UPDATESHADER,
	RDRCMD_RELOADSHADERS,
	RDRCMD_QUERYSHADERPERF,
	RDRCMD_QUERYPERFTIMES,
	RDRCMD_FREEALLSHADERS,
	RDRCMD_PRELOADVERTEXSHADERS,
	RDRCMD_PRELOADHULLSHADERS,
	RDRCMD_PRELOADDOMAINSHADERS,

	// textures
	RDRCMD_UPDATETEXTURE,
	RDRCMD_UPDATESUBTEXTURE,
	RDRCMD_TEXSTEALSNAPSHOT,
	RDRCMD_SETTEXTUREANISOTROPY,
	RDRCMD_GETTEXINFO,
	RDRCMD_FREETEXTURE,
	RDRCMD_FREEALLTEXTURES,
	RDRCMD_SWAPTEXHANDLES,

	// geometry
	RDRCMD_UPDATEGEOMETRY,
	RDRCMD_FREEGEOMETRY,
	RDRCMD_FREEALLGEOMETRY,

	// drawing
	RDRCMD_SURFACE_CLEAR,
	RDRCMD_SETEXPOSURE,
	RDRCMD_SETSHADOWBUFFER,
	RDRCMD_SETCUBEMAPLOOKUP,
	RDRCMD_SETSOFTPARTICLEDEPTH,
	RDRCMD_SET_SSAO_DIRECT_ILLUM_FACTOR,
	RDRCMD_DRAWQUAD,
	RDRCMD_DRAWSPRITES,
	RDRCMD_POSTPROCESSSCREEN,
	RDRCMD_POSTPROCESSSHAPE,
	RDRCMD_DRAWLIST_SORT,
	RDRCMD_DRAWLIST_DRAW,
	RDRCMD_DRAWLIST_RELEASE,
	RDRCMD_DEBUG_BUFFER,

	RDRCMD_SETCURSORSTATE,

	RDRCMD_FREEDATA,
	RDRCMD_FREEMEMREFDATA,

	// Events
	RDRCMD_BEGINNAMEDEVENT,
	RDRCMD_ENDNAMEDEVENT,
	RDRCMD_FLUSHGPU,
	RDRCMD_MEMLOG,
	RDRCMD_CMDFENCE,

	// GPU profiling
	RDRCMD_GPU_MARKER,

	// Stencil
	RDRCMD_STENCILFUNC,
	RDRCMD_STENCILOP,

	// FMV
	RDRCMD_FMV_INIT,
	RDRCMD_FMV_PLAY,
	RDRCMD_FMV_SETPARAMS,
	RDRCMD_FMV_CLOSE,

	// last one
	RDRCMD_MAX,

} RdrCmdType;


typedef void (*RdrUserMsgHandler)(RdrDevice *device, void *userdata, RdrMsgType type, void *data, WTCmdPacket *packet);
typedef void (*RdrWinMsgHandler)(RdrDevice *device, void *userdata, WinMsg **msgs);

//////////////////////////////////////////////////////////////////////////

void rdrSetMsgHandler(RdrDevice *device, RdrUserMsgHandler msg_handler, RdrWinMsgHandler winmsg_handler, void *userdata);
#define rdrGetWindowHandle(device) ((device)?(device)->getWindowHandle(device):0)
#define rdrGetInstanceHandle(device) ((device)?(device)->getInstanceHandle(device):0)

bool rwinSetFocusAwayFromClient();

//////////////////////////////////////////////////////////////////////////

typedef struct QueuedRenderCmd
{
	RdrCmdType cmd;
	int data_size;
} QueuedRenderCmd;

typedef struct RdrCursorData
{
	const char *cursor_name; // Only needed for function call scope
	int hotspot_x;
	int hotspot_y;
	int size_x;
	int size_y;
	U8 *data; // RGBA_U8, freed by renderer
} RdrCursorData;


typedef struct RdrDevice
{
	union {
		RdrDevice *device;
		RdrDeviceDX *device_xbox;
		RdrDeviceWinGL *device_gl;
	};
	union {
		struct RdrDeviceFunctions
		{
			/* destructor*/
			void (*destroy)(RdrDevice *device);

			/* messages*/
			void (*processMessages)(RdrDevice *device);

			/* device query*/
			void (*getSize)(RdrDevice *device, DisplayParams * size_info);
			int (*isInactive)(RdrDevice *device, bool attemptReactivate);
			void (*reactivate)(RdrDevice *device);
			const char *(*getIdentifier)(RdrDevice *device);
			const char *(*getProfileName)(RdrDevice *device);
			const char *(*getIntrinsicDefines)(RdrDevice *device);

			/* surfaces*/
			RdrSurface *(*getPrimarySurface)(RdrDevice *device);
			RdrSurface *(*createSurface)(RdrDevice *device, const RdrSurfaceParams *params);
			void (*renameSurface)(RdrDevice *device, RdrSurface *surface, const char *name);
			void (*setPrimarySurfaceActive)(RdrDevice *device);

			/* cursor*/
			void (*setCursorFromData)(RdrDevice *device, RdrCursorData *data);
			int (*setCursorFromCache)(RdrDevice *device, const char *cursor_name);

			/* capability query*/
			int (*getMaxTexAnisotropy)(RdrDevice *device);
			int (*supportsFeature)(RdrDevice *device, RdrFeature feature);
			void (*updateMaterialFeatures)(RdrDevice *device);
			int (*supportsSurfaceType)(RdrDevice *device, RdrSurfaceBufferType surface_type);
			int (*getMaxPixelShaderConstants)(RdrDevice *device);

			/* window handle accessor (for input library)*/
			void *(*getWindowHandle)(RdrDevice *device);
			/* Instance handle (for WinGL)*/
			void *(*getInstanceHandle)(RdrDevice *device);

			/* Call this when the app has crashed to hide the (full-screen exclusive) window so you can see the crash */
			void (*appCrashed)(RdrDevice *device);

			/* Resize handler called by internal rwinSetSizeDirect() after resizing window/changing resolutions*/
			/* See rwinSetSizeDirect() for a description of the flags*/
			void (*resizeDeviceDirect)(RdrDevice * device, DisplayParams *dimensions, RdrResizeFlags flags);

			/* FMVs */
			RdrFMV *(*fmvCreate)(RdrDevice *device);
		};
		void *device_functions_array[sizeof(struct RdrDeviceFunctions)/sizeof(void*)];
	};

	//////////////////////////////////////////////////////////////////////////
	// private data
	WorkerThread *worker_thread;
	RdrUserMsgHandler user_msg_handler;
	void *user_data;

	QueuedRenderCmd **queued_commands;

	// frame update synchronization
	volatile int frames_buffered;
	CRITICAL_SECTION frame_check;
	HANDLE frame_done_signal;

	U32 frame_count;
	U32 frame_count_nonthread;

	EventOwner *event_timer;

	DisplayParams display_nonthread;
	U32 pad1;

	U32 nSendTextInputEnableCount;

	RdrSurface *nonthread_active_surface;
	RdrSurfaceBufferMaskBits nonthread_active_surface_write_mask;
	RdrSurfaceFace nonthread_active_surface_face;

	U8 is_locked_thread;
	U8 is_locked_nonthread;

	int primary_monitor; // What monitor this device was created for

	// General performance
	RdrDevicePerformanceValues perf_values;
	RdrDevicePerformanceValues perf_values_last_frame;

	char current_title[256];
	U32 title_need_update : 1;
	U32 thread_cursor_needs_refresh;
	U32 bManualWindowManagement : 1;
	U32 thread_cursor_visible : 1;
	U32 thread_cursor_restrict_to_window : 1;
	U32 thread_reservedAvailableBits : 28;
	
	U32 nonthread_cursor_visible : 1;
	U32 nonthread_cursor_restrict_to_window : 1;
	U32 nonthread_reservedAvailableBits : 30;

	RdrDeviceMemoryUsage **memory_usage;

	void (*gfx_lib_device_lost_cb)( RdrDevice* device );
	void (*gfx_lib_display_params_change_cb)( RdrDevice* device );
} RdrDevice;



//////////////////////////////////////////////////////////////////////////
// inlined commands
//////////////////////////////////////////////////////////////////////////

__forceinline static void rdrStartTextInput(RdrDevice *device)
{
	++device->nSendTextInputEnableCount;
}

__forceinline static void rdrStopTextInput(RdrDevice *device)
{
	assert(device->nSendTextInputEnableCount > 0);
	--device->nSendTextInputEnableCount;
}

__forceinline static void rdrSetTitle(RdrDevice *device, const char *title)
{
	if (!title)
		title = "";
	if (strcmp(device->current_title, title) != 0)
	{
		strcpy(device->current_title, title);
		wtQueueCmd(device->worker_thread, RDRCMD_WIN_SETTITLE, (void *)title, (int)strlen(title)+1);
	}
}

__forceinline static void rdrSetSize(RdrDevice *device, DisplayParams *dimensions)
{
	DisplayParams *params;
	bool didLock=false;
	if (!device->is_locked_nonthread) {
		// Lock the device, because showing the window can cause WM_ACTIVATEAPP to happen, which calls rxbxIsInactiveDirect()
		didLock = true;
		rdrLockActiveDevice(device, false);
	}
	device->display_nonthread = *dimensions;
	params = (DisplayParams*)wtAllocCmd(device->worker_thread, RDRCMD_WIN_SETSIZE, sizeof(*dimensions));
	*params = *dimensions;
	wtSendCmd(device->worker_thread);
	if (didLock) {
		rdrUnlockActiveDevice(device, false, false, false);
		rdrProcessMessages(device);
	}
}


__forceinline static void rdrSetPosAndSize(RdrDevice *device, DisplayParams * dimensions)
{
	DisplayParams *params;
	bool didLock=false;
	if (!device->is_locked_nonthread) {
		// Lock the device, because showing the window can cause WM_ACTIVATEAPP to happen, which calls rxbxIsInactiveDirect()
		didLock = true;
		rdrLockActiveDevice(device, false);
	}
	device->display_nonthread = *dimensions;
	params = (DisplayParams*)wtAllocCmd(device->worker_thread, RDRCMD_WIN_SETPOSANDSIZE, sizeof(*dimensions));
	*params = *dimensions;
	wtSendCmd(device->worker_thread);
	if (didLock) {
		rdrUnlockActiveDevice(device, false, false, false);
		rdrProcessMessages(device);
	}
}


#define SW_HIDE             0
#define SW_SHOW             5
__forceinline static void rdrShowWindow(RdrDevice *device, int nCmdShow)
{
	bool didLock=false;
	if (!device->is_locked_nonthread) {
		// Lock the device, because showing the window can cause WM_ACTIVATEAPP to happen, which calls rxbxIsInactiveDirect()
		didLock = true;
		rdrLockActiveDevice(device, false);
	}
	wtQueueCmd(device->worker_thread, RDRCMD_WIN_SHOW, &nCmdShow, sizeof(int));
	if (didLock) {
		rdrUnlockActiveDevice(device, false, false, false);
	}
}

__forceinline static void rdrShellExecute(RdrDevice *device, HWND hwnd, const char* lpOperation, const char* lpFile, const char* lpParameters, const char* lpDirectory, int nShowCmd, ShellExecuteCallback callback)
{
	RdrShellExecuteCommands* pCommands;
	bool didLock=false;
	if (!device->is_locked_nonthread) {
		// Lock the device, because showing the window can cause WM_ACTIVATEAPP to happen, which calls rxbxIsInactiveDirect()
		didLock = true;
		rdrLockActiveDevice(device, false);
	}

	pCommands = wtAllocCmd(device->worker_thread, RDRCMD_SHELL_EXECUTE, sizeof(*pCommands));
	pCommands->hwnd = hwnd;
	pCommands->lpOperation = allocAddCaseSensitiveString(lpOperation);
	pCommands->lpFile = allocAddCaseSensitiveString(lpFile);
	pCommands->lpParameters = allocAddCaseSensitiveString(lpParameters);
	pCommands->lpDirectory = allocAddCaseSensitiveString(lpDirectory);
	pCommands->nShowCmd = nShowCmd;
	pCommands->callback = callback;

	wtSendCmd(device->worker_thread);
	if (didLock) {
		rdrUnlockActiveDevice(device, false, false, false);
	}
}

__forceinline static void rdrSetIcon(RdrDevice *device, int resource_id) { wtQueueCmd(device->worker_thread, RDRCMD_WIN_SETICON, &resource_id, sizeof(resource_id)); }
__forceinline static void rdrSetVsync(RdrDevice *device, bool enable) { int set_enable = !!enable; wtQueueCmd(device->worker_thread, RDRCMD_WIN_SETVSYNC, &set_enable, sizeof(set_enable)); }
__forceinline static void rdrSetGamma(RdrDevice *device, F32 gamma) { wtQueueCmd(device->worker_thread, RDRCMD_WIN_SETGAMMA, &gamma, sizeof(gamma)); }
__forceinline static void rdrGammaNeedReset(RdrDevice *device) { wtQueueCmd(device->worker_thread, RDRCMD_WIN_GAMMANEEDRESET, 0, 0); }

__forceinline static void rdrSurfaceSetDepthSurface(RdrSurface *surface, RdrSurface *depth_surface)
{
	RdrSurface *surfaces[2];
	surfaces[0] = surface;
	surfaces[1] = depth_surface;
	wtQueueCmd(surface->device->worker_thread, RDRCMD_SURFACE_SETDEPTHSURFACE, surfaces, 2*sizeof(RdrSurface *));
}

__forceinline static void rdrSurfaceSetActive(RdrSurface *surface, RdrSurfaceBufferMaskBits write_mask, RdrSurfaceFace face)
{
	RdrSurfaceActivateParams activate_params;
	RdrDevice *device = surface->device;
	assert(!surface->destroyed_nonthread);
	activate_params.surface = surface;
	activate_params.write_mask = write_mask;
	activate_params.face = face;
	device->nonthread_active_surface = surface;
	device->nonthread_active_surface_face = face;
	device->nonthread_active_surface_write_mask = write_mask;
	if (device->is_locked_nonthread)
		wtQueueCmd(device->worker_thread, RDRCMD_SURFACE_SETACTIVE, &activate_params, sizeof(activate_params));
}

__forceinline static void rdrOverrideDepthSurface(RdrDevice *device, RdrSurface *override_depth_surface)
{
	if (device->is_locked_nonthread)
		wtQueueCmd(device->worker_thread, RDRCMD_OVERRIDE_DEPTH_SURFACE, &override_depth_surface, sizeof(override_depth_surface));
}

__forceinline static void rdrClearActiveSurface(RdrDevice *device, RdrClearBits clear_bits, const Vec4 clear_color, F32 clear_depth)
{
	RdrClearParams *params = (RdrClearParams *)wtAllocCmd(device->worker_thread, RDRCMD_SURFACE_CLEAR, sizeof(*params));
	params->bits = clear_bits;
	copyVec4(clear_color, params->clear_color);
	params->clear_depth = clear_depth;
	wtSendCmd(device->worker_thread);
}

__forceinline static void rdrSurfaceSetFog(RdrSurface *surface, F32 low_near_dist, F32 low_far_dist, F32 low_max_fog,
										   F32 high_near_dist, F32 high_far_dist, F32 high_max_fog,
										   F32 low_height, F32 high_height,
										   const Vec3 low_fog_color, const Vec3 high_fog_color,
										   int bVolumeFog)
{
	RdrSetFogData *params = (RdrSetFogData *)wtAllocCmd(surface->device->worker_thread, RDRCMD_SURFACE_SETFOG, sizeof(*params));
	params->low_near_dist = low_near_dist;
	params->low_far_dist = low_far_dist;
	params->low_max_fog = saturate(low_max_fog);
	params->high_near_dist = high_near_dist;
	params->high_far_dist = high_far_dist;
	params->high_max_fog = saturate(high_max_fog);
	params->low_height = low_height;
	params->high_height = high_height;
	copyVec3(low_fog_color, params->low_fog_color);
	copyVec3(high_fog_color, params->high_fog_color);
	params->surface = surface;
	params->bVolumeFog = bVolumeFog;
	wtSendCmd(surface->device->worker_thread);
}

// Note: if swapping snapshots between surfaces of difference sizes, be careful to restore them before
//  doing things like taking a new snapshot!  You may need to disable auto-resolve on these surfaces.
__forceinline static void rdrSurfaceSwapSnapshots(RdrSurface *surface0, RdrSurface *surface1, RdrSurfaceBuffer buffer0, RdrSurfaceBuffer buffer1, U32 index0, U32 index1)
{
	RdrSwapSnapshotData *params = wtAllocCmd(surface0->device->worker_thread, RDRCMD_SURFACE_SWAP_SNAPSHOTS, sizeof(*params));
	assert(surface0->device == surface1->device);
	params->surface0 = surface0;
	params->surface1 = surface1;
	params->index0 = index0;
	params->index1 = index1;
	params->buffer0 = buffer0;
	params->buffer1 = buffer1;
	wtSendCmd(surface0->device->worker_thread);
}


typedef struct RdrSurfaceQueryData
{
	const char *name;
	int w;
	int h;
	int mrt_count;
	int msaa;
	RdrSurfaceBufferType buffer_types[SBUF_MAXMRT];
	int texture_count[SBUF_MAX];
	int texture_max[SBUF_MAX];
	const char *snapshot_names[SBUF_MAX][16];
	int total_mem_size;
	RdrSurface *rdr_surface; // NOT thread-safe, may have been destroyed, use with care
} RdrSurfaceQueryData;

typedef struct RdrSurfaceQueryAllData
{
	int nsurfaces;
	RdrSurfaceQueryData details[128];
} RdrSurfaceQueryAllData;


__forceinline static void rdrQueryAllSurfaces(RdrDevice *device, RdrSurfaceQueryAllData *data)
{
	wtQueueCmd(device->worker_thread, RDRCMD_SURFACE_QUERY_ALL, &data, sizeof(data));
}

__forceinline static void rdrGetActiveSurfaceDataAsync(RdrDevice *device, RdrSurfaceData *params)
{
    wtQueueCmd(device->worker_thread, RDRCMD_SURFACE_GETDATA, params, sizeof(*params));
}

__forceinline static U8 *rdrGetActiveSurfaceData(RdrDevice *device, RdrSurfaceDataType type, U32 width, U32 height)
{
	RdrSurfaceData params={(RdrSurfaceDataType)0};
	params.type = type;
	params.width = width;
	params.height = height;
	params.data = NULL;
	rdrAllocBufferForSurfaceData(&params);
	rdrGetActiveSurfaceDataAsync(device, &params);
	wtFlush(device->worker_thread);
	return params.data;
}

__forceinline static void rdrSurfaceSnapshotEx(RdrSurface *surface, const char *name, int dest_set_index, int buffer_mask, int continue_tiling)
{
	RdrSurfaceSnapshotData *params = (RdrSurfaceSnapshotData *)wtAllocCmd(surface->device->worker_thread, RDRCMD_SURFACE_SNAPSHOT, sizeof(*params));
	params->surface = surface;
	params->name = name;
	params->dest_set_index = dest_set_index;
	params->txaa_prev_set_index = 0;
	params->buffer_mask = buffer_mask;
	params->continue_tiling = continue_tiling;
	params->force_srgb = 0;

	wtSendCmd(surface->device->worker_thread);
}

__forceinline static void rdrSurfaceSnapshotExTXAA(RdrSurface *surface, const char *name, int dest_set_index, int txaa_prev_set_index, int buffer_mask, int debug_mode)
{
	RdrSurfaceSnapshotData *params = (RdrSurfaceSnapshotData *)wtAllocCmd(surface->device->worker_thread, RDRCMD_SURFACE_SNAPSHOT, sizeof(*params));
	params->surface = surface;
	params->name = name;
	params->dest_set_index = dest_set_index;
	params->txaa_prev_set_index = txaa_prev_set_index;
	params->txaa_debug_mode = debug_mode;
	params->buffer_mask = buffer_mask;
	params->continue_tiling = 0;
	params->force_srgb = true; // TXAA requires this

	wtSendCmd(surface->device->worker_thread);
}

__forceinline static void rdrSurfaceSnapshot(RdrSurface *surface, const char *name, int dest_set_index)
{
	rdrSurfaceSnapshotEx(surface, name, dest_set_index, MASK_SBUF_ALL_COLOR, 0);
}

__forceinline static void rdrSurfaceRestoreAfterSetActive(RdrSurface *surface, RdrSurfaceBufferMaskBits mask)
{
#if _XBOX
	wtQueueCmd(surface->device->worker_thread, RDRCMD_SURFACE_RESTORE, &mask, sizeof(mask));
#endif
}

__forceinline static void rdrSurfaceSetAutoResolveDisableMask(RdrSurface *surface, RdrSurfaceBufferMaskBits mask)
{
	RdrSurfaceSetAutoResolveDisableMaskData data;
	data.surface = surface;
	data.buffer_mask = mask;
	wtQueueCmd(surface->device->worker_thread, RDRCMD_SURFACE_SET_AUTO_RESOLVE_DISABLE_MASK, &data, sizeof(RdrSurfaceSetAutoResolveDisableMaskData));
}

__forceinline static void rdrSurfacePushAutoResolveMask(RdrSurface *surface, RdrSurfaceBufferMaskBits mask)
{
	RdrSurfaceSetAutoResolveDisableMaskData data;
	data.surface = surface;
	data.buffer_mask = mask;
	wtQueueCmd(surface->device->worker_thread, RDRCMD_SURFACE_PUSH_AUTO_RESOLVE_MASK, &data, sizeof(RdrSurfaceSetAutoResolveDisableMaskData));
}

__forceinline static void rdrSurfacePopAutoResolveMask(RdrSurface *surface)
{
	RdrSurfaceSetAutoResolveDisableMaskData data;
	data.surface = surface;
	data.buffer_mask = 0;
	wtQueueCmd(surface->device->worker_thread, RDRCMD_SURFACE_POP_AUTO_RESOLVE_MASK, &data, sizeof(RdrSurfaceSetAutoResolveDisableMaskData));
}

__forceinline static void rdrCopyMaterial(RdrMaterial *dest, RdrMaterial *src, TexHandle *texptr, Vec4 *constptr, RdrPerDrawableConstantMapping *mapptr)
{
	*dest = *src;
	dest->textures = texptr;
	CopyStructs(dest->textures, src->textures, dest->tex_count);
	dest->constants = constptr;
	CopyStructs(dest->constants, src->constants, dest->const_count);
	dest->drawable_constants = mapptr;
	CopyStructs(dest->drawable_constants, src->drawable_constants, dest->drawable_const_count);
}

__forceinline static void rdrSetExposureTransform(RdrDevice *device, const Vec4 exposure_transform) { wtQueueCmd(device->worker_thread, RDRCMD_SETEXPOSURE, exposure_transform, sizeof(Vec4)); }
__forceinline static void rdrSetSSAODirectIllumFactor(RdrDevice *device, F32 factor) { wtQueueCmd(device->worker_thread, RDRCMD_SET_SSAO_DIRECT_ILLUM_FACTOR, &factor, sizeof(F32)); }
__forceinline static void rdrSetShadowBufferTexture(RdrDevice *device, TexHandle tex) { wtQueueCmd(device->worker_thread, RDRCMD_SETSHADOWBUFFER, &tex, sizeof(tex)); }
__forceinline static void rdrSetCubemapLookupTexture(RdrDevice *device, TexHandle tex) { wtQueueCmd(device->worker_thread, RDRCMD_SETCUBEMAPLOOKUP, &tex, sizeof(tex)); }
__forceinline static void rdrSetSoftParticleDepthTexture(RdrDevice *device, TexHandle tex) { wtQueueCmd(device->worker_thread, RDRCMD_SETSOFTPARTICLEDEPTH, &tex, sizeof(tex)); }

void rdrDrawQuad(RdrDevice *device, RdrQuadDrawable *quad_src);

void rdrPostProcessScreen(RdrDevice *device, RdrScreenPostProcess *ppscreen_src);
void rdrPostProcessScreenPart(RdrDevice *device, RdrScreenPostProcess *ppscreen_src, Vec2 dest_top_left, Vec2 dest_bottom_right);
void rdrPostProcessShape(RdrDevice *device, RdrShapePostProcess *ppshape_src);

RdrSpritesPkg *rdrStartDrawSpritesImmediate(RdrDevice *device, U32 array_size, int screen_width, int screen_height);
//specify either indices or indices32 but not both
RdrSpritesPkg *rdrStartDrawSpritesImmediateUP(RdrDevice *device, RdrDrawList *draw_list, U32 array_size, int screen_width, int screen_height,
									 RdrSpriteState  *states, RdrSpriteVertex *vertices, U16 *indices, U32* indices32, bool freeStates, bool freeVerts, bool freeIndices, bool isMemRef);
void rdrEndDrawSpritesImmediate(RdrDevice *device);


__forceinline static void rdrEndDrawFastParticleSet(RdrDevice *device) { wtSendCmd(device->worker_thread); }

//////////////////////////////////////////////////////////////////////////
// Cursor state controls visibility and mouse clip
typedef struct RdrCursorState
{
	bool visible;
	bool restrict_to_window;
} RdrCursorState;

// MJF July/22/2013 -- You should not call rdrSetCursorState directly,
// because then your logic will not work with the software cursor or
// mouseLock.  Instead add logic to UICursor.c to control the cursor
// visibility state.
__forceinline static void rdrSetCursorState(RdrDevice *device, bool visible, bool restrict_to_window) {
	if( visible != !!device->nonthread_cursor_visible || restrict_to_window != !!device->nonthread_cursor_restrict_to_window ) {
		RdrCursorState params = { 0 };
		params.visible = visible;
		params.restrict_to_window = restrict_to_window;		
		wtQueueCmd(device->worker_thread, RDRCMD_SETCURSORSTATE, &params, sizeof( params ));
		device->nonthread_cursor_visible = visible;
		device->nonthread_cursor_restrict_to_window = restrict_to_window;
	}
}
#define rdrSetCursorState(...) DO_NOT_CALL_rdrSetCursorState_DIRECTLY__SEE_COMMENT_FOR_INSTRUCTIONS

__forceinline static void rdrFlush(RdrDevice *device, bool dispatch_messages) { wtFlushEx(device->worker_thread, dispatch_messages); }

// The fence is a render thread command that issues a callback at two points: first in 
// the render thread where the command is processed (and so the render thread has processed 
// all prior commands), and second when a result message queued back to the main thread 
// at the first point actually executes in the main thread. A third event location is still 
// unimplemented: after the GPU completes all side-effects of the commands. This feature 
// is meant to provide efficient notification for the processing of command and retrieval 
// of results from the render thread. See RDRCMD_CMDFENCE and rdrCmdFenceEx.
__forceinline static void rdrCmdFenceEx(RdrDevice *device, const char *fenceName, 
	RdrCmdFenceCallback rdrThreadCmdCompleteCB, RdrCmdFenceCallback mainThreadCmdCompleteCB,
	void * user_data, bool bWaitForGPU)
{
	RdrCmdFenceData fenceData = { fenceName, 
		rdrThreadCmdCompleteCB, mainThreadCmdCompleteCB,
		user_data, bWaitForGPU };

	wtQueueCmd(device->worker_thread, RDRCMD_CMDFENCE, &fenceData, sizeof(fenceData)); 
}
// This is a simplified interface to rdrCmdFenceEx.
__forceinline static void rdrCmdFence(RdrDevice *device, const char *fenceName, 
	RdrCmdFenceCallback mainThreadCmdCompleteCB, void * user_data)
{
	rdrCmdFenceEx(device, fenceName, NULL, mainThreadCmdCompleteCB, user_data, false);
}

__forceinline static void rdrReloadShaders(RdrDevice *device)
{
    wtQueueCmd(device->worker_thread, RDRCMD_RELOADSHADERS, NULL, 0);
}
__forceinline static void rdrQueryShaderPerf(RdrDevice *device, RdrShaderPerformanceValues *perf_values) { wtQueueCmd(device->worker_thread, RDRCMD_QUERYSHADERPERF, &perf_values, sizeof(perf_values)); }
__forceinline static void rdrQueryPerfTimes(RdrDevice *device, RdrDevicePerfTimes *perf_times) { wtQueueCmd(device->worker_thread, RDRCMD_QUERYPERFTIMES, &perf_times, sizeof(perf_times)); }


__forceinline static void rdrPreloadVertexShaders(RdrDevice *device) { wtQueueCmd(device->worker_thread, RDRCMD_PRELOADVERTEXSHADERS, NULL, 0); }
__forceinline static void rdrPreloadHullShaders(RdrDevice *device) { wtQueueCmd(device->worker_thread, RDRCMD_PRELOADHULLSHADERS, NULL, 0); }
__forceinline static void rdrPreloadDomainShaders(RdrDevice *device) { wtQueueCmd(device->worker_thread, RDRCMD_PRELOADDOMAINSHADERS, NULL, 0); }

__forceinline static void rdrFreeData(RdrDevice *device, void *data) { wtQueueCmd(device->worker_thread, RDRCMD_FREEDATA, &data, sizeof(data)); }
__forceinline static void rdrFreeReferencedData(RdrDevice *device, void *data) { wtQueueCmd(device->worker_thread, RDRCMD_FREEMEMREFDATA, &data, sizeof(data)); }

void rdrGetRememberedParams(const char *window_name, DisplayParams *params);
void rdrSetRememberedParams(const char *window_name, RdrDevice *device);
void rdrGetDeviceSizeEx(RdrDevice *device, DisplayParams *params);

//////////////////////////////////////////////////////////////////////////

void rdrTrackUserMemoryDirect(RdrDevice *device, const char *moduleName, int staticModuleName, intptr_t memTrafficDelta);
void rdrUntrackAllUserMemoryDirect(RdrDevice *device);

__forceinline static void rdrBeginNamedEvent(RdrDevice *device, const char *name) { wtQueueCmd(device->worker_thread, RDRCMD_BEGINNAMEDEVENT, &name, sizeof(name)); }
__forceinline static void rdrEndNamedEvent(RdrDevice *device) { wtQueueCmd(device->worker_thread, RDRCMD_ENDNAMEDEVENT, NULL, 0); }
__forceinline static void rdrFlushGPU(RdrDevice *device) { wtQueueCmd(device->worker_thread, RDRCMD_FLUSHGPU, NULL, 0); }
void rdrMemlogPrintf(RdrDevice *device, MemLog *memlog, FORMAT_STR const char *fmt, ...);
#define rdrMemlogPrintf(device, memlog, fmt, ...) rdrMemlogPrintf(device, memlog, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

RdrOcclusionQueryResult *rdrAllocOcclusionQuery(RdrDevice *device);
void rdrFreeOcclusionQuery(RdrOcclusionQueryResult *query);
__forceinline static void rdrFlushQueries(RdrDevice *device) { wtQueueCmd(device->worker_thread, RDRCMD_FLUSHQUERIES, NULL, 0); wtFlush(device->worker_thread); }

__forceinline static void rdrStencilFunc(RdrDevice *device, int enable, RdrPPDepthTestMode func, S32 ref, U32 mask)
{
	RdrStencilFuncParams params = {0};
	params.enable = enable;
	params.func = func;
	params.ref = ref;
	params.mask = mask;
	wtQueueCmd(device->worker_thread, RDRCMD_STENCILFUNC, &params, sizeof(params));
}

__forceinline static void rdrStencilOp(RdrDevice *device, RdrStencilOp fail, RdrStencilOp zfail, RdrStencilOp zpass)
{
	RdrStencilOpParams params = {0};
	params.fail = fail;
	params.zfail = zfail;
	params.pass = zpass;
	wtQueueCmd(device->worker_thread, RDRCMD_STENCILOP, &params, sizeof(params));
}

__forceinline static void rdrSurfaceResolve(RdrDevice* device, RdrSurface* dest, const IVec4 sourceRect, const IVec2 destPoint)
{
#if _XBOX
	RdrSurfaceResolveData resolve_data = {0};
	resolve_data.dest = dest;
	if (sourceRect)
	{
		resolve_data.hasSourceRect = true;
		copyVec4(sourceRect, resolve_data.sourceRect);
	}
	if (destPoint)
	{
		resolve_data.hasDestPoint = true;
		copyVec2(destPoint, resolve_data.destPoint);
	}
	wtQueueCmd(device->worker_thread, RDRCMD_SURFACE_RESOLVE, &resolve_data, sizeof(resolve_data));
#endif
}


__forceinline static void rdrSurfaceUpdateMatrices(RdrSurface *surface, const Mat44 projmat, const Mat44 fardepth_projmat, const Mat44 sky_projmat,
												   const Mat4 viewmat, const Mat4 inv_viewmat, 
												   const Mat4 fogmat, F32 znear, F32 zfar, F32 far_znear, 
												   F32 viewport_x, F32 viewport_width, F32 viewport_y, F32 viewport_height, 
												   const Vec3 camera_pos)
{
	RdrSurfaceUpdateMatrixData *data;

	// send updated matrices to thread
	data = wtAllocCmd(surface->device->worker_thread, RDRCMD_SURFACE_UPDATEMATRICES, sizeof(RdrSurfaceUpdateMatrixData));
	data->surface = surface;
	copyMat44(projmat, data->projection);
	copyMat44(fardepth_projmat, data->fardepth_projection);
	copyMat44(sky_projmat, data->sky_projection);
	copyMat4(viewmat, data->view_mat);
	copyMat4(inv_viewmat, data->inv_view_mat);
	copyVec3(camera_pos, data->camera_pos);
	copyMat4(fogmat, data->fog_mat);
	data->znear = znear;
	data->zfar = zfar;
	data->far_znear = far_znear;
	data->viewport_x = viewport_x;
	data->viewport_width = viewport_width;
	data->viewport_y = viewport_y;
	data->viewport_height = viewport_height;
	wtSendCmd(surface->device->worker_thread);
}

#define rdrSurfaceUpdateMatricesFromFrustum(surface, projmat, fogmat, frustum, viewport_x, viewport_width, viewport_y, viewport_height, camera_pos) rdrSurfaceUpdateMatrices(surface, projmat, projmat, projmat, (frustum)->viewmat, (frustum)->inv_viewmat, fogmat, (frustum)->znear, (frustum)->zfar, (frustum)->zfar * 0.95f, viewport_x, viewport_width, viewport_y, viewport_height, camera_pos)
#define rdrSurfaceUpdateMatricesFromFrustumEX(surface, projmat, far_projmat, sky_projmat, fogmat, frustum, viewport_x, viewport_width, viewport_y, viewport_height, camera_pos) rdrSurfaceUpdateMatrices(surface, projmat, far_projmat, sky_projmat, (frustum)->viewmat, (frustum)->inv_viewmat, fogmat, (frustum)->znear, (frustum)->zfar, (frustum)->zfar * 0.95f, viewport_x, viewport_width, viewport_y, viewport_height, camera_pos)

__forceinline bool rdrIsDeviceTypeAuto(const char *device_type)
{
	return !device_type || !strcmp(device_type, "");
}

#endif //_RDRDEVICE_H_


