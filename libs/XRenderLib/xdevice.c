
#include "EventTimingLog.h"
#include "StringUtil.h"
#include "memlog.h"
#include "BlockEarray.h"
#include "winutil.h"

#include "RenderLib.h"
#include "../RdrDrawListPrivate.h"
#include "xdevice.h"
#include "rt_xcursor.h"
#include "rt_xdevice.h"
#include "rt_xsurface.h"
#include "rt_xshader.h"
#include "rt_xdrawmode.h"
#include "rt_xprimitive.h"
#include "rt_xtextures.h"
#include "rt_xgeo.h"
#include "rt_xsprite.h"
#include "rt_xpostprocessing.h"
#include "rt_xdrawlist.h"
#include "rt_xFMV.h"
#include "systemspecs.h"
#include "WTCmdPacket.h"
#include "DXVersionCheck.h"
#include "file.h"
#include "RdrDeviceTrace.h"
#include "nvapi_wrapper.h"
#include "sysutil.h"
#include "UTF8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

#if _PS3
#elif _XBOX
	//Note: some libraries also referenced in file.c so no-graphics apps can link
	#pragma comment(lib, "dxerr9.lib")
	#pragma comment(lib, "xboxkrnl.lib")

	#ifdef FULLDEBUG
		#pragma comment(lib, "xgraphicsd.lib")
	#else
		#pragma comment(lib, "xgraphics.lib")
	#endif

	#ifdef PROFILE
		//#pragma comment(lib, "d3d9i.lib") // in file.c because XAPILIB depends on it
		#pragma comment(lib, "d3dx9i.lib")
		#pragma comment(lib, "xapilibi.lib")
		//#pragma comment(lib, "libcMT.lib")
		#pragma comment(lib, "libcpMT.lib")
	#else
		//#pragma comment(lib, "d3d9.lib") // in file.c because XAPILIB depends on it
		#pragma comment(lib, "d3dx9.lib")
		#pragma comment(lib, "libcMTd.lib")
		#pragma comment(lib, "xapilibd.lib")
	#endif
#else
	#ifdef _M_X64
		#pragma comment(lib, "../../3rdparty/directx/lib/x64/dxguid.lib")
		#pragma comment(lib, "../../3rdparty/directx/lib/x64/d3d9.lib")
		#pragma comment(lib, "../../3rdparty/directx/lib/x64/d3dcompiler.lib")
		#pragma comment(lib, "../../3rdparty/directx/lib/x64/d3d11.lib")
		#pragma comment(lib, "../../3rdparty/directx/lib/x64/dxgi.lib")
		#pragma comment(lib, "nvapi64.lib")
	#else
		#pragma comment(lib, "../../3rdparty/directx/lib/dxguid.lib")
		#pragma comment(lib, "../../3rdparty/directx/lib/d3d9.lib")
		#pragma comment(lib, "../../3rdparty/directx/lib/d3dcompiler.lib")
		#pragma comment(lib, "../../3rdparty/directx/lib/d3d11.lib")
		#pragma comment(lib, "../../3rdparty/directx/lib/dxgi.lib")
		#pragma comment(lib, "nvapi.lib")
	#endif
#endif

static void rxbxEnumD3D9Adapters(bool bDirect3D9Ex, RdrDeviceInfo ***device_infos);

static void rxbxFreeAllShadersDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet)
{
	// Wait for all shader compiles to finish - we could instead cancel these?
	while (shader_background_compile_count)
		Sleep(1);
	stashTableDestroy(device->lpc_crctable);
	device->lpc_crctable = 0;
	SAFE_FREE(device->special_vertex_shaders);
	SAFE_FREE(device->default_pixel_shaders);
	stashTableDestroy(device->standard_vertex_shaders_table);
	eaDestroyEx(&device->standard_vertex_shaders_mem, NULL);
	device->standard_vertex_shaders_mem_left = 0;
	if (device->error_vertex_shader)
		device->error_vertex_shader->is_error_shader = 0;
	if (device->error_pixel_shader)
		device->error_pixel_shader->is_error_shader = 0;
	FOR_EACH_IN_STASHTABLE2(device->vertex_shaders, elem)
		RxbxVertexShader *vshader = stashElementGetPointer(elem);
		rxbxFreeVertexShader(device, stashElementGetIntKey(elem), vshader);
	FOR_EACH_END;
	stashTableDestroy(device->vertex_shaders);
	device->vertex_shaders = 0;
	FOR_EACH_IN_STASHTABLE2(device->pixel_shaders, elem)
		RxbxPixelShader *pshader = stashElementGetPointer(elem);
		rxbxFreePixelShader(device, stashElementGetIntKey(elem), pshader);
	FOR_EACH_END;
	stashTableDestroy(device->pixel_shaders);
	device->pixel_shaders = 0;
}

static void rxbxDestroyDevice(RdrDevice *device);
static void rxbxFreeAllCursors(RdrDevice *device);

static void rxbxSetSurfaceActivePtrDirect(RdrDeviceDX *device, RdrSurfaceActivateParams *params, WTCmdPacket *packet)
{
	rxbxSetSurfaceActiveDirect(device, params);
}

static void rxbxGetActiveSurfaceDirect(RdrDeviceDX *device, void *data, WTCmdPacket *packet)
{
	rxbxGetSurfaceDataDirect(device->active_surface, data);
}

//////////////////////////////////////////////////////////////////////////
// process messages and swap

static void rxbxProcessMessages(RdrDevice *device)
{
	PERFINFO_AUTO_START_FUNC();
	etlAddEvent(device->event_timer, "Process messages", ELT_CODE, ELTT_BEGIN);
#if !PLATFORM_CONSOLE
	PERFINFO_AUTO_START("lock/queue/unlock", 1);
	rdrLockActiveDevice(device, false);
	wtQueueCmd(device->worker_thread, RXBXCMD_PROCESSWINMSGS, 0, 0);
	rdrUnlockActiveDevice(device, false, false, false);
	PERFINFO_AUTO_STOP();
#endif
	PERFINFO_AUTO_START("wtMonitor", 1);
	wtMonitor(device->worker_thread);
	PERFINFO_AUTO_STOP();
	etlAddEvent(device->event_timer, "Process messages", ELT_CODE, ELTT_END);
	PERFINFO_AUTO_STOP();
}

//////////////////////////////////////////////////////////////////////////
// surfaces

static RdrSurface *rxbxGetPrimarySurface(RdrDevice *device)
{
	RdrDeviceDX *xdevice = (RdrDeviceDX *)device;
	return &xdevice->primary_surface.surface_base;
}

static void rxbxSetPrimarySurfaceActive(RdrDevice *device)
{
	RdrDeviceDX *xdevice = (RdrDeviceDX *)device;
	rdrSurfaceSetActive(&xdevice->primary_surface.surface_base, 0, 0);
}

static void *rxbxGetHWND(RdrDevice *device)
{
    RdrDeviceDX *xdevice = (RdrDeviceDX *)device;
	if (!xdevice->d3d_device && !xdevice->d3d11_device)
		return NULL; // Have not created a device yet, return NULL so that pop-up windows always show up!
	return xdevice->hWindow;
}

static void *rxbxGetHINSTANCE(RdrDevice *device)
{
    RdrDeviceDX *xdevice = (RdrDeviceDX *)device;
#if PLATFORM_CONSOLE
	return NULL;
#else
	return xdevice->windowClass.hInstance;
#endif
}

//////////////////////////////////////////////////////////////////////////
// capability query
static int rxbxGetMaxTexAnisotropy(RdrDevice *device)
{
#if PLATFORM_CONSOLE
	return 4;
#else
	RdrDeviceDX *xdevice = (RdrDeviceDX *)device;
	assert(xdevice->caps_filled);
	return xdevice->rdr_caps_new.max_anisotropy;
#endif
}

int rxbxSupportsFeature(RdrDevice *device, RdrFeature feature)
{
	RdrDeviceDX *xdevice = (RdrDeviceDX *)device;

	if (feature & rdr_state.features_disabled)
		return 0;

#if _PS3
	// PS3: just return this
	return rdrSupportsFeaturePS3(device, feature);
#endif

#if !PLATFORM_CONSOLE
	// PC: special cages for querying XBOX/PS3 options
	if (device == DEVICE_PS3)
	{
		return rdrSupportsFeaturePS3(device, feature);
	}
	else if (device == DEVICE_XBOX)
#endif
	{
		// Xbox or PC special case
		switch (feature)
		{
			xcase FEATURE_ANISOTROPY:
				return 1;
			xcase FEATURE_NONPOW2TEXTURES:
				return 1;
			xcase FEATURE_NONSQUARETEXTURES:
				return 1;
			xcase FEATURE_MRT2:
				return 1;
			xcase FEATURE_MRT4:
				return 1;
			xcase FEATURE_SM20:
				return 1;
			xcase FEATURE_SM2B:
				return 0;
			xcase FEATURE_SM30:
				return 1;
			xcase FEATURE_SRGB:
				return 1;
			xcase FEATURE_DX10_LEVEL_CARD:
				return 0;
			xcase FEATURE_INSTANCING:
#if ENABLE_XBOX_HW_INSTANCING
				return !rdr_state.disableHWInstancing;
#else
				return 0;
#endif
			xcase FEATURE_VFETCH:
				return 1;
			xcase FEATURE_DEPTH_TEXTURE:
				return 1;
			xcase FEATURE_24BIT_DEPTH_TEXTURE:
				return 1;
			xcase FEATURE_STENCIL_DEPTH_TEXTURE:
				return 1;
			xcase FEATURE_DECL_F16_2:
				return 1;
			xcase FEATURE_SBUF_FLOAT_FORMATS:
				return 1;
			xcase FEATURE_DXT_VOLUME_TEXTURE:
				return 1;
		}
	}

#if !PLATFORM_CONSOLE
	// PC general case
    else 
    {
		assert(device);
		assert(xdevice->caps_filled);
		switch (feature)
		{
			xcase FEATURE_INSTANCING:
				return	(xdevice->rdr_caps_new.features_supported & FEATURE_INSTANCING)
						 && rdr_state.hwInstancingMode;
			xcase FEATURE_DEPTH_TEXTURE:
				return (xdevice->rdr_caps_new.features_supported & FEATURE_DEPTH_TEXTURE) &&
					!rdr_state.disableDepthTextures;
			xcase FEATURE_24BIT_DEPTH_TEXTURE:
				return (xdevice->rdr_caps_new.features_supported & FEATURE_24BIT_DEPTH_TEXTURE) &&
					!rdr_state.disableF24DepthTexture;
			xcase FEATURE_STENCIL_DEPTH_TEXTURE:
				return (xdevice->rdr_caps_new.features_supported & FEATURE_STENCIL_DEPTH_TEXTURE) &&
					!rdr_state.disableStencilDepthTexture;
			xcase FEATURE_DECL_F16_2:
				return (xdevice->rdr_caps_new.features_supported & FEATURE_DECL_F16_2) &&
					!rdr_state.disableF16s;
			xcase FEATURE_DEPTH_TEXTURE_MSAA:
				return (xdevice->rdr_caps_new.features_supported & FEATURE_DEPTH_TEXTURE_MSAA) &&
					!rdr_state.disableMSAADepthResolve;
			xdefault:
				return !!(xdevice->rdr_caps_new.features_supported & feature);
		}
	}
#endif
	return 0;
}

int rxbxGetMaxPixelShaderConstants(RdrDevice *device)
{
	if (rxbxSupportsFeature(device, FEATURE_SM30))
		return 224;
	return 32;
}

void rxbxUpdateMaterialFeatures(RdrDevice *device)
{
	RdrDeviceDX *xdevice = (RdrDeviceDX *)device;

	if (!system_specs.material_hardware_override)
	{
		ShaderGraphFeatures accum;
		accum = 0;

		if( rdrSupportsFeature( device, FEATURE_SM30 ))
		{
			accum |= SGFEAT_SM30;
#if !PLATFORM_CONSOLE
			if (xdevice->rdr_caps_new.supports_sm30_plus)
				accum |= SGFEAT_SM30_PLUS;
#else
			accum |= SGFEAT_SM30_PLUS;
#endif
		}
		if( rdrSupportsFeature( device, FEATURE_SM2B ))
		{
			accum |= SGFEAT_SM20_PLUS | SGFEAT_SM20;
		}
		else if( rdrSupportsFeature( device, FEATURE_SM20 ))
		{
			accum |= SGFEAT_SM20;
		}

		system_specs.material_hardware_supported_features = accum;
	}
}

// returns 0 if not supported, otherwise it returns the maximum multisample level of the surface type
static int rxbxSupportsSurfaceType(RdrDevice *device, RdrSurfaceBufferType surface_type)
{
	RdrDeviceDX *xdevice = (RdrDeviceDX *)device;
#if PLATFORM_CONSOLE
	return 1;
#else
	assert(xdevice->caps_filled);
	return xdevice->surface_types_multisample_supported[surface_type & SBT_TYPE_MASK];
#endif
}

static void rxbxGetDeviceSize(RdrDevice *device, DisplayParams * size_info)
{
	RdrDeviceDX *xdevice = (RdrDeviceDX *)device;
	*size_info = xdevice->display_thread;
}

static const char *rxbxGetIdentifier(RdrDevice *device)
{
	return "D3D";
}

static const char *rxbxGetProfileName(RdrDevice *device)
{
	RdrDeviceDX *xdevice = (RdrDeviceDX *)device;
	return xdevice->d3d11_device?"D3D11":"D3D9";
}


const char *rxbxGetIntrinsicDefines(RdrDevice *device)
{
	int i;
	char buf[1024] = {0};

#define ADD_DEFINE(s) do { if (buf[0]) strcat(buf, " "); strcat(buf, s); } while(0)

	// PLATFORM DEFINES WARNING: These must match the order and contents exactly of what's defined in gfxShaderCompileForAllPlatforms()
	if (rxbxSupportsFeature(device, FEATURE_MRT4) && rdr_state.allowMRTshaders)
		ADD_DEFINE("MRT4");

	if (rxbxSupportsFeature(device, FEATURE_MRT2) && rdr_state.allowMRTshaders)
		ADD_DEFINE("MRT2");

	if (rxbxSupportsFeature(device, FEATURE_SM30))
		ADD_DEFINE("SM30");
	else if (rxbxSupportsFeature(device, FEATURE_SM2B))
		ADD_DEFINE("SM2B");
	else
		ADD_DEFINE("SM20");
	
	if (rxbxSupportsFeature(device, FEATURE_VFETCH))
		ADD_DEFINE("VFETCH");

	if (rxbxSupportsFeature(device, FEATURE_DEPTH_TEXTURE))
		ADD_DEFINE("DEPTH_TEXTURE");

#if !PLATFORM_CONSOLE
	if (device != DEVICE_XBOX && device != DEVICE_PS3)
	{
		RdrDeviceDX* xdevice = (RdrDeviceDX*)device;

		// DEPTH_FORMAT This must match the order of checking in rxbxGetDepthFormat()
		if(xdevice->nvidia_intz_supported)
		{
			//ADD_DEFINE("INTZ_DEPTH"); // Not actually used
		} else if(xdevice->nvidia_rawz_supported) {
			ADD_DEFINE("RAWZ_DEPTH");
		} else if(xdevice->ati_df24_supported) {
			//ADD_DEFINE("DF24_DEPTH"); // Not actually used
		} else if(xdevice->ati_df16_supported) {
			//ADD_DEFINE("DF16_DEPTH"); // Not actually used
		}

		if (xdevice->d3d11_device)
			ADD_DEFINE("D3D11");
	}
#endif

	if(getIsTransgaming()) {
		ADD_DEFINE("TRANSGAMING");
	}

	for (i=0; i<rdrShaderGetTestDefineCount(); i++) {
		const char *str = rdrShaderGetTestDefine(i);
		if (str) {
			if (str[0] && str[0]!='0') {
				ADD_DEFINE(str);
			}
		}
	}
	for (i=0; i<rdrShaderGetGlobalDefineCount(); i++) {
		const char *str = rdrShaderGetGlobalDefine(i);
		if (str) {
			if (str[0] && str[0]!='0') {
				ADD_DEFINE(str);
			}
		}
	}

	// PLATFORM DEFINES WARNING: These must match the order and contents exactly of what's defined in gfxShaderCompileForAllPlatforms()
	// Platform identifying define *must* be last (gets pulled out and used as part of the cache name)
#if _PS3
    ADD_DEFINE("_PS3");
#elif _XBOX
    ADD_DEFINE("_XBOX");
#else
	if (device == DEVICE_PS3)
	{
		ADD_DEFINE("_PS3");
    }
    else if (device == DEVICE_XBOX)
	{
		ADD_DEFINE("_XBOX");
	} else {
		// Platform identifying define *must* be last (gets pulled out and used as part of the cache name)
		switch (system_specs.videoCardVendorID)
		{
		case VENDOR_ATI:
			ADD_DEFINE("ATI");

		xcase VENDOR_NV:
			ADD_DEFINE("NVIDIA");

		xcase VENDOR_INTEL:
			ADD_DEFINE("INTEL");

		xcase VENDOR_S3G:
			ADD_DEFINE("S3G");

		xcase VENDOR_XBOX360:
			assert(0); // Should get hit above

		xcase VENDOR_WINE:
			ADD_DEFINE("WINE");

		xdefault:
			ADD_DEFINE("UNKNOWN_VENDOR");
		}
	}

	// Platform identifying define *must* be last (gets pulled out and used as part of the cache name)
	// PLATFORM DEFINES WARNING: These must match the order and contents exactly of what's defined in gfxShaderCompileForAllPlatforms()
#endif

	return allocAddCaseSensitiveString(buf);
#undef ADD_DEFINE
}

//////////////////////////////////////////////////////////////////////////
// cursor

static void rxbxInitCursor(RdrDeviceDX *device)
{
	device->cursor_cache = stashTableCreateWithStringKeys(1024, StashDeepCopyKeys_NeverRelease);
}

static void rxbxCheckClearCursorCache(RdrDevice *device)
{
	RdrDeviceDX *xdevice = (RdrDeviceDX *)device;
	if (xdevice->reset_all_cursors)
	{
		xdevice->reset_all_cursors = 0;
		rxbxFreeAllCursors(device);
	}
}

static int rxbxSetCursorFromCache(RdrDevice *device, const char *cursor_name)
{
	RdrDeviceDX *xdevice = (RdrDeviceDX *)device;
	RxbxCursor *cursor;

	rxbxCheckClearCursorCache(device);

	if (stashFindPointer(xdevice->cursor_cache, cursor_name, &cursor))
	{
		// set cursor
		if (!rdrIsDeviceInactive(device))
		{
			assert(cursor->cache_it);
			wtQueueCmd(device->worker_thread, RXBXCMD_SETCURSOR, &cursor, sizeof(cursor));
			return 1;
		}
	}
	return 0; // not cached
}

static void rxbxSetCursorFromData(RdrDevice *device, RdrCursorData *data)
{
	RxbxCursorData cursor_data = {0};
	RdrDeviceDX *xdevice = (RdrDeviceDX *)device;
	RxbxCursor *cursor = malloc(sizeof(*cursor));

	assert(device->is_locked_nonthread);

	cursor->cache_it = 0;
	if (data->cursor_name)
	{
		RxbxCursor *old_cursor;

		rxbxCheckClearCursorCache(device);

		// cache it
		if (stashFindPointer(xdevice->cursor_cache, data->cursor_name, &old_cursor))
		{
			// destroy old one
			wtQueueCmd(device->worker_thread, RXBXCMD_DESTROYCURSOR, &old_cursor, sizeof(old_cursor));
		}
		stashAddPointer(xdevice->cursor_cache, data->cursor_name, cursor, true);
		cursor->cache_it = 1;
	}

	cursor->hotspot_x = data->hotspot_x;
	cursor->hotspot_y = data->hotspot_y;
	cursor->handle = 0;
	cursor_data.cursor = cursor;
	cursor_data.size_x = data->size_x;
	cursor_data.size_y = data->size_y;
	cursor_data.data = data->data;
	data->data = NULL;
	wtQueueCmd(device->worker_thread, RXBXCMD_SETCURSOR_FROM_DATA, &cursor_data, sizeof(cursor_data));
}

static int rxbxFreeCursorFromCache(void *param, StashElement element)
{
	RdrDevice *device = (RdrDevice *)param;
	RxbxCursor *cursor = stashElementGetPointer(element);
	wtQueueCmd(device->worker_thread, RXBXCMD_DESTROYCURSOR, &cursor, sizeof(cursor));
	return 1;
}

static void rxbxFreeAllCursors(RdrDevice *device)
{
	RdrDeviceDX *xdevice = (RdrDeviceDX *)device;
	wtQueueCmd(device->worker_thread, RXBXCMD_DESTROYACTIVECURSOR, 0, 0);
	stashForEachElementEx(xdevice->cursor_cache, rxbxFreeCursorFromCache, device);
	stashTableClear(xdevice->cursor_cache);
}

static U32 reactivateSequenceNumber = 0;

//////////////////////////////////////////////////////////////////////////
// Create and destroy
int rxbxIsInactive(RdrDevice *device, bool attemptReactivate)
{
	RdrDeviceDX *xdevice = (RdrDeviceDX*)device;
	int isDeviceLost = xdevice->isLost;
#if !PLATFORM_CONSOLE
	if (attemptReactivate && isDeviceLost) // This value is not up to date, but last frame's results
	{
		bool need_lock = !xdevice->device_base.is_locked_nonthread;
		if (need_lock)
			rdrLockActiveDevice(device, false);
		TRACE_FRAME_MT(device, "MT sending reactivate %u\n", reactivateSequenceNumber);
		++reactivateSequenceNumber;
		wtQueueCmd(device->worker_thread, RXBXCMD_HANDLEDEVICELOSS, &reactivateSequenceNumber, sizeof(reactivateSequenceNumber));
		if (need_lock)
			rdrUnlockActiveDevice(device, false, false, false);
	}
#endif
	TRACE_FRAME_MT(device, "MT dev %s(%d)\n", isDeviceLost ? "lost" : "oper", isDeviceLost);
	return isDeviceLost; // This value is not up to date, but last frame's results
}

int rxbxIssueProfilingQuery(RdrDeviceDX *device, int iTimerIdx);

void rxbxBeginNamedEvent(RdrDevice *device, void* data, WTCmdPacket *packet)
{
#if _PS3
    RdrDeviceDX *xdevice = (RdrDeviceDX*)device;
    char *eventName;
	wtCmdRead(packet, &eventName, sizeof(char*));
    cellGcmSetPerfMonPushMarker(xdevice->d3d_device, eventName);

#elif defined(PROFILE) && _XBOX

	char *eventName;
	wtCmdRead(packet, &eventName, sizeof(char*));
	PIXBeginNamedEvent(0, eventName);

#elif !_XBOX

	static StashTable wideEventNames;
	short *wideEventName = NULL;
	char *eventName = NULL;

	if (!wideEventNames)
		wideEventNames = stashTableCreateWithStringKeys(1024, StashDeepCopyKeys_NeverRelease);

	wtCmdRead(packet, &eventName, sizeof(char*));
	if (!stashFindPointer(wideEventNames, eventName, &wideEventName))
	{
		short wideEventNameBuffer[1024];
		int length = 1 + UTF8ToWideStrConvert(eventName, wideEventNameBuffer, ARRAY_SIZE(wideEventNameBuffer));
		stashAddPointer(wideEventNames, eventName, memcpy(malloc(length * sizeof(short)), wideEventNameBuffer, length * sizeof(short)), false);
		assert(stashFindPointer(wideEventNames, eventName, &wideEventName));
	}

	D3DPERF_BeginEvent(0, wideEventName);

#endif
}

void rxbxGPUMarker(RdrDevice *device, void* data, WTCmdPacket *packet)
{
	int iTimerIdx;
	wtCmdRead(packet, &iTimerIdx, sizeof(iTimerIdx));
	rxbxIssueProfilingQuery((RdrDeviceDX*)device, iTimerIdx);
}

void rxbxEndNamedEvent(RdrDevice *device, void* data, WTCmdPacket *packet)
{
#if _PS3
    RdrDeviceDX *xdevice = (RdrDeviceDX*)device;

    cellGcmSetPerfMonPopMarker(xdevice->d3d_device);

#elif defined(PROFILE) && _XBOX
	PIXEndNamedEvent();
#elif !_XBOX
	D3DPERF_EndEvent();
#endif
}

void rxbxPerfMarkerColor(RdrDeviceDX *device, const char * marker_string, U32 color)
{
#if _DEBUG && !_XBOX
    if (!device->d3d11_device)
	{
        short wideEventNameBuffer[256];
        UTF8ToWideStrConvert(marker_string, wideEventNameBuffer, ARRAY_SIZE(wideEventNameBuffer));
        D3DPERF_SetMarker(color, wideEventNameBuffer);
    }
#endif
}

void rxbxMemlogDirect(RdrDevice *device, RdrDeviceMemlogParams *params, WTCmdPacket *packet)
{
	memlog_printf(params->memlog, "%s", params->buffer);
}

void rxbxCmdFenceDirect(RdrDevice *device, RdrCmdFenceData *fenceData, WTCmdPacket *packet)
{
	// DJR logging these events for initial testing
	memlog_printf(NULL, "Rdr Fence RT callback: Fence=%p Name=%s\n", fenceData, fenceData->name);
	if (fenceData->rdrThreadCmdCompleteCB)
		fenceData->rdrThreadCmdCompleteCB(device, fenceData);
	wtQueueMsg(device->worker_thread, RDRMSG_CMDFENCECOMPLETE, fenceData, sizeof(*fenceData)); 
}

RdrDeviceDX xbox_static_device_for_debug = { 0 };
int xbox_static_device_for_debug_allocated = 0;

static float NULL_SHADOW_MAP_FAR_DEPTH = 1.0f;

RdrDevice *rdrCreateDeviceXBoxCommon(WindowCreateParams *params, HINSTANCE hInstance, int processor_idx, int api_level, int feature_level)
{
	RdrDeviceDX *device;
	WorkerThread *wt;

	if (xbox_static_device_for_debug_allocated)
		device = aligned_calloc(1, sizeof(RdrDeviceDX), VEC_ALIGNMENT);
	else
	{
		xbox_static_device_for_debug_allocated = 1;
		device = &xbox_static_device_for_debug;
	}

#if _FULLDEBUG
	assertVecAligned(device->device_state.vs_constants_driver[0]);
	assertVecAligned(device->device_state.vs_constants_desired[0]);
	assertVecAligned(device->device_state.ps_constants_driver[0]);
	assertVecAligned(device->device_state.ps_constants_desired[0]);
#endif

	systemSpecsInit();

	//////////////////////////////////////////////////////////////////////////
	// create worker thread
	rdrInitDevice((RdrDevice *)device, params->threaded, processor_idx);

	device->vertex_declarations = stashTableCreateInt(64);
	device->device_base.display_nonthread = params->display;
	device->display_thread = params->display;


	wt = device->device_base.worker_thread;
	wtSetEventOwner(wt, device->device_base.event_timer);

	// create and destroy
	wtRegisterCmdDispatch(wt, RXBXCMD_CREATE, api_level > 9 ? rxbxCreateDirect11 : rxbxCreateDirect);
	wtRegisterCmdDispatch(wt, RXBXCMD_DESTROY, rxbxDestroyDirect);

	// window
#if !PLATFORM_CONSOLE
	wtRegisterCmdDispatch(wt, RXBXCMD_HANDLEDEVICELOSS, rxbxIsInactiveDirect);
	wtRegisterCmdDispatch(wt, RXBXCMD_PROCESSWINMSGS, rxbxProcessWindowsMessagesDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_WIN_SHOW, rxbxShowDirect);
#endif
	wtRegisterCmdDispatch(wt, RDRCMD_BEGINSCENE, rxbxBeginSceneDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_ENDSCENE, rxbxEndSceneDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_FREEQUERY, rxbxFreeQueryDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_FLUSHQUERIES, rxbxFlushQueriesDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_WIN_SETVSYNC, rxbxSetVsyncDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_WIN_SETGAMMA, rxbxSetGammaDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_WIN_GAMMANEEDRESET, rxbxNeedGammaResetDirect);

	// surfaces
	wtRegisterCmdDispatch(wt, RXBXCMD_SURFACE_INIT, rxbxInitSurfaceDirect);
	wtRegisterCmdDispatch(wt, RXBXCMD_SURFACE_FREE, rxbxFreeSurfaceDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_RESETACTIVESTATE, rxbxResetStateDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_SETACTIVE, rxbxSetSurfaceActivePtrDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_OVERRIDE_DEPTH_SURFACE, rxbxOverrideDepthSurfaceDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_CLEAR, rxbxClearActiveSurfaceDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_GETDATA, rxbxGetActiveSurfaceDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_SNAPSHOT, rxbxSurfaceSnapshotDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_UPDATEMATRICES, rxbxUpdateSurfaceMatricesDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_SETFOG, rxbxSetSurfaceFogDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_SETDEPTHSURFACE, rxbxSurfaceSetDepthSurfaceDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_SET_AUTO_RESOLVE_DISABLE_MASK, rxbxSurfaceSetAutoResolveDisableMaskDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_PUSH_AUTO_RESOLVE_MASK, rxbxSurfacePushAutoResolveMaskDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_POP_AUTO_RESOLVE_MASK, rxbxSurfacePopAutoResolveMaskDirect);
#if _XBOX
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_RESTORE, rxbxSurfaceRestoreAfterSetActiveDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_RESOLVE, rxbxSurfaceResolve);
#endif
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_SWAP_SNAPSHOTS, rxbxSurfaceSwapSnapshotsDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SURFACE_QUERY_ALL, rxbxQueryAllSurfacesDirect);

	// shaders
	wtRegisterCmdDispatch(wt, RDRCMD_UPDATESHADER, rxbxSetPixelShaderDataDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_RELOADSHADERS, rxbxReloadDefaultShadersDirect);
#if !_PS3
	wtRegisterCmdDispatch(wt, RDRCMD_QUERYSHADERPERF, rxbxQueryShaderPerfDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_QUERYPERFTIMES, rxbxQueryPerfTimesDirect);
#endif
	wtRegisterCmdDispatch(wt, RDRCMD_FREEALLSHADERS, rxbxFreeAllShadersDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_PRELOADVERTEXSHADERS, rxbxPreloadVertexShaders);
	wtRegisterCmdDispatch(wt, RDRCMD_PRELOADHULLSHADERS, rxbxPreloadHullShaders);
	wtRegisterCmdDispatch(wt, RDRCMD_PRELOADDOMAINSHADERS, rxbxPreloadDomainShaders);

	// textures
	wtRegisterCmdDispatch(wt, RDRCMD_UPDATETEXTURE, rxbxSetTextureDataDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_UPDATESUBTEXTURE, rxbxSetTextureSubDataDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_TEXSTEALSNAPSHOT, rxbxTexStealSnapshotDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SETTEXTUREANISOTROPY, rxbxSetTexAnisotropyDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_GETTEXINFO, rxbxGetTexInfoDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_FREETEXTURE, rxbxFreeTextureDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_FREEALLTEXTURES, rxbxFreeAllTexturesDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_SWAPTEXHANDLES, rxbxSwapTexHandlesDirect);
	if (rdr_state.validateTextures)
	{
		wtSetPostCmdCallback(wt, rxbxValidateTextures);
	}

	// geometry
	wtRegisterCmdDispatch(wt, RDRCMD_UPDATEGEOMETRY, rxbxSetGeometryDataDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_FREEGEOMETRY, rxbxFreeGeometryDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_FREEALLGEOMETRY, rxbxFreeAllGeometryDirect);

	// drawing
	wtRegisterCmdDispatch(wt, RDRCMD_SETEXPOSURE, rxbxSetExposureTransform);
	wtRegisterCmdDispatch(wt, RDRCMD_SETSHADOWBUFFER, rxbxSetShadowBufferTexture);
	wtRegisterCmdDispatch(wt, RDRCMD_SETCUBEMAPLOOKUP, rxbxSetCubemapLookupTexture);
	wtRegisterCmdDispatch(wt, RDRCMD_SETSOFTPARTICLEDEPTH, rxbxSetSoftParticleDepthTexture);
	wtRegisterCmdDispatch(wt, RDRCMD_SET_SSAO_DIRECT_ILLUM_FACTOR, rxbxSetSSAODirectIllumFactor);
	wtRegisterCmdDispatch(wt, RDRCMD_DRAWSPRITES, rxbxDrawSpritesDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_DRAWQUAD, rxbxDrawQuadDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_POSTPROCESSSCREEN, rxbxPostProcessScreenDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_POSTPROCESSSHAPE, rxbxPostProcessShapeDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_DRAWLIST_SORT, rxbxSortDrawObjectsDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_DRAWLIST_DRAW, rxbxDrawObjectsDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_DRAWLIST_RELEASE, rdrReleaseDrawListDataDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_DEBUG_BUFFER, rxbxSetDebugBuffer);

	// cursor
	wtRegisterCmdDispatch(wt, RDRCMD_SETCURSORSTATE, rxbxSetCursorStateDirect);
	wtRegisterCmdDispatch(wt, RXBXCMD_SETCURSOR, rxbxSetCursorDirect);
	wtRegisterCmdDispatch(wt, RXBXCMD_SETCURSOR_FROM_DATA, rxbxSetCursorFromDataDirect);
	wtRegisterCmdDispatch(wt, RXBXCMD_DESTROYCURSOR, rxbxDestroyCursorDirect);
	wtRegisterCmdDispatch(wt, RXBXCMD_DESTROYACTIVECURSOR, rxbxDestroyActiveCursorDirect);

    wtRegisterCmdDispatch(wt, RDRCMD_BEGINNAMEDEVENT, rxbxBeginNamedEvent);
	wtRegisterCmdDispatch(wt, RDRCMD_ENDNAMEDEVENT, rxbxEndNamedEvent);
	wtRegisterCmdDispatch(wt, RDRCMD_FLUSHGPU, rxbxFlushGPUDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_MEMLOG, rxbxMemlogDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_CMDFENCE, rxbxCmdFenceDirect);

    wtRegisterCmdDispatch(wt, RDRCMD_GPU_MARKER, rxbxGPUMarker);

	wtRegisterCmdDispatch(wt, RDRCMD_STENCILFUNC, rxbxStencilFuncHandler);
	wtRegisterCmdDispatch(wt, RDRCMD_STENCILOP, rxbxStencilOpHandler);

	wtRegisterCmdDispatch(wt, RDRCMD_FMV_INIT, rxbxFMVInitDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_FMV_PLAY, rxbxFMVPlayDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_FMV_SETPARAMS, rxbxFMVSetParamsDirect);
	wtRegisterCmdDispatch(wt, RDRCMD_FMV_CLOSE, rxbxFMVCloseDirect);

	device->texture_data = stashTableCreateInt(4096);
	device->geometry_cache = stashTableCreateInt(4096);

	//////////////////////////////////////////////////////////////////////////
	// setup function pointers
	device->device_base.processMessages = rxbxProcessMessages;

	device->device_base.destroy = rxbxDestroyDevice;
	device->device_base.isInactive = rxbxIsInactive;
	device->device_base.reactivate = NULL;
						
	device->device_base.createSurface = rxbxCreateSurface;
	device->device_base.renameSurface = rxbxRenameSurface;
	device->device_base.getPrimarySurface = rxbxGetPrimarySurface;
	device->device_base.setPrimarySurfaceActive = rxbxSetPrimarySurfaceActive;
						
	device->device_base.getWindowHandle = rxbxGetHWND;
	device->device_base.getInstanceHandle = rxbxGetHINSTANCE;
	device->device_base.appCrashed = rxbxAppCrashed;

	device->device_base.getMaxTexAnisotropy = rxbxGetMaxTexAnisotropy;
	device->device_base.supportsFeature = rxbxSupportsFeature;
	device->device_base.updateMaterialFeatures = rxbxUpdateMaterialFeatures;
	device->device_base.supportsSurfaceType = rxbxSupportsSurfaceType;
	device->device_base.getMaxPixelShaderConstants = rxbxGetMaxPixelShaderConstants;
						
	device->device_base.setCursorFromData = rxbxSetCursorFromData;
	device->device_base.setCursorFromCache = rxbxSetCursorFromCache;
						
	device->device_base.getSize = rxbxGetDeviceSize;
	device->device_base.getIdentifier = rxbxGetIdentifier;
	device->device_base.getProfileName = rxbxGetProfileName;
	device->device_base.getIntrinsicDefines = rxbxGetIntrinsicDefines;
						
	device->device_base.resizeDeviceDirect = rxbxResizeDeviceDirect;

	device->device_base.fmvCreate = rxbxFMVCreate;

	//////////////////////////////////////////////////////////////////////////
	// setup primary surface
	rxbxInitPrimarySurface(device);


	//////////////////////////////////////////////////////////////////////////
	// create window
	rdrLockActiveDevice((RdrDevice *)device, false);
	wtQueueCmd(device->device_base.worker_thread, RXBXCMD_CREATE, params, sizeof(*params));
	wtFlush(device->device_base.worker_thread);

	if (device->d3d_device || device->d3d11_device)
	{
		// create default textures
		U32 byte_count;
		RdrTexParams *texParams;
		device->white_tex_handle = rdrGenTexHandle(0);
		texParams = rdrStartUpdateTexture((RdrDevice *)device, device->white_tex_handle, RTEX_2D, RTEX_BGRA_U8, 1, 1, 1, 1, &byte_count, "Textures:Misc", NULL);
		memset(texParams->data, 0xffffffff, byte_count);
		rdrEndUpdateTexture((RdrDevice *)device);

		// Make a white depth texture for a standin when we need a comparison sampler instead of a normal sampler for a
		// blank texture.
		if (device->d3d_device)
			device->white_depth_tex_handle = RDR_NULL_TEXTURE;
		else
		{
			device->white_depth_tex_handle = rdrGenTexHandle(0);
			texParams = rdrStartUpdateTexture(
				(RdrDevice *)device, device->white_depth_tex_handle,
				RTEX_2D,
				RTEX_R_F32, 1, 1, 1, 1,
				&byte_count, "Textures:Misc", NULL);
			memset(texParams->data, 0, byte_count);
			((F32*)texParams->data)[ 0 ] = NULL_SHADOW_MAP_FAR_DEPTH;
			rdrEndUpdateTexture((RdrDevice *)device);
			rdrAddRemoveTexHandleFlags(
				&(device->white_depth_tex_handle),
				RTF_COMPARISON_LESS_EQUAL,
				0);
		}

		device->black_tex_handle = rdrGenTexHandle(0);
		texParams = rdrStartUpdateTexture((RdrDevice *)device, device->black_tex_handle, RTEX_2D, RTEX_BGRA_U8, 1, 1, 1, 1, &byte_count, "Textures:Misc", NULL);
		memset(texParams->data, 0, byte_count);
		rdrEndUpdateTexture((RdrDevice *)device);

		wtFlush(device->device_base.worker_thread);

		// cursor
		rxbxInitCursor(device);
	}
	rdrUnlockActiveDevice((RdrDevice *)device, true, false, false);

	if (!device->d3d_device && !device->d3d11_device)
	{
		rxbxDestroyDevice((RdrDevice *)device);
		device = 0;
	}

	return (RdrDevice *)device;
}

RdrDevice *rdrCreateDeviceXBox(WindowCreateParams *params, HINSTANCE hInstance, int processor_idx)
{
	return rdrCreateDeviceXBoxCommon(params, hInstance, processor_idx, 9, 0);
}

RdrDevice *rdrCreateDeviceXBox11(WindowCreateParams *params, HINSTANCE hInstance, int processor_idx)
{
	return rdrCreateDeviceXBoxCommon(params, hInstance, processor_idx, 11, 0);
}

int rxbxEnumDXGIOutputs(RdrHandleDXGIOutputDX handler, void * pUserData)
{
	int terminated_early = 0;
	IDXGIAdapter * pAdapter; 
	IDXGIFactory* pFactory = NULL; 
	UINT Adapter;

	// Create a DXGIFactory object.
	if(FAILED(CreateDXGIFactory(&IID_IDXGIFactory ,(void**)&pFactory)))
		return 0;

	for ( Adapter = 0;
		!terminated_early && IDXGIFactory_EnumAdapters(pFactory, Adapter, &pAdapter) != DXGI_ERROR_NOT_FOUND;
		++Adapter )
	{
		int i=0;
		DXGI_ADAPTER_DESC desc;
		IDXGIOutput *pOutput;
		IDXGIAdapter_GetDesc(pAdapter, &desc);
		while(!terminated_early && IDXGIAdapter_EnumOutputs(pAdapter, i, &pOutput) != DXGI_ERROR_NOT_FOUND)
		{
			DXGI_OUTPUT_DESC output_desc;
			DXGIEnumResults enumHandlerResult = DXGIEnum_ContinueEnum;
			IDXGIOutput_GetDesc(pOutput, &output_desc);

			enumHandlerResult = handler(pAdapter, Adapter, &desc, pOutput, &output_desc, i, pUserData);
			IDXGIOutput_Release(pOutput);

			if (enumHandlerResult == DXGIEnum_Fail)
				terminated_early = 1;
			else
			if (enumHandlerResult == DXGIEnum_RetryOutput)
				// retry enumerating modes for this output
				continue;

			++i;
		}
		IDXGIAdapter_Release(pAdapter);
	}

	if(pFactory)
		IDXGIFactory_Release(pFactory);

	return 1;
}

static const char * getScanOutString(DXGI_MODE_SCANLINE_ORDER scanline_order)
{
	switch (scanline_order)
	{
	xcase DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE:
		return "Progressive";
	xcase DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST:
	case DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST:
		return "Interlace";
	xdefault:
		return "Auto";
	}
}

static const char * getDisplayScalingModeString(DXGI_MODE_SCALING scaling)
{
	switch (scaling)
	{
	xcase DXGI_MODE_SCALING_CENTERED:
		return "Center";
	xcase DXGI_MODE_SCALING_STRETCHED:
		return "Stretch";
	xdefault:
		return "Auto";
	}
}

static int EnumerateDXGIModes(IDXGIOutput *pOutput, const char * adapterName, 
	const char * outputName, DXGI_FORMAT displayFormat, UINT *pNumModes, DXGI_MODE_DESC *pDisplayModes, bool bIssueErrorFOnFail)
{
	char strResults[256];
	HRESULT resultGetModeList = IDXGIOutput_GetDisplayModeList(pOutput, displayFormat, 
		DXGI_ENUM_MODES_SCALING, pNumModes, pDisplayModes);
	if (FAILED(resultGetModeList))
	{
		sprintf(strResults, "Enumerate display modes for Direct3D 11 (DXGI) adapter failed; "
			"adapter \"%s\" output \"%s\" format %d, code = 0x%x%s", adapterName, outputName, displayFormat, resultGetModeList, 
			resultGetModeList == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE ? ". Possibly failed due to running under Remote Desktop." : "");
		memlog_printf(NULL, "%s", strResults);
		if (bIssueErrorFOnFail)
		{
			ErrorDetailsf("%s", strResults);
			Errorf("Enumerate display modes for Direct3D 11 (DXGI) adapter failed.");
		}
	}
	else
		memlog_printf(NULL, "Enumerate display modes for Direct3D 11 (DXGI) adapter succeeded; "
			"adapter \"%s\" output \"%s\" format %d, %d modes", adapterName, outputName, displayFormat, *pNumModes);
	return resultGetModeList;
}

#define DXGI_NUM_STANDARD_FORMATS 2
#define DXGI_NUM_TOTAL_FORMATS 7

static const DXGI_FORMAT dxgiDisplayFormats[DXGI_NUM_TOTAL_FORMATS] = 
{
	DXGI_FORMAT_B8G8R8A8_UNORM,
	DXGI_FORMAT_R8G8B8A8_UNORM,
	DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
	DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
	DXGI_FORMAT_R10G10B10A2_UNORM,
	DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM,
	DXGI_FORMAT_R16G16B16A16_FLOAT,
};

int rxbxDXGIToCrypticDisplayFormat(DXGI_FORMAT Format)
{
	int formatIndex = 0;
	for (; formatIndex < DXGI_NUM_TOTAL_FORMATS; ++formatIndex)
		if (dxgiDisplayFormats[formatIndex] == Format)
			return formatIndex;
	assert(!"Not a valid display format");
	return 0;
}

DXGIEnumResults rxbxEnumDevicesDXGIOutputHandler(IDXGIAdapter * pAdapter, UINT adapterNum, const DXGI_ADAPTER_DESC * pDesc, IDXGIOutput * pOutput, 
	const DXGI_OUTPUT_DESC * pOutputDesc, UINT outputNum, void * pUserData)
{
	if (pOutputDesc->AttachedToDesktop)
	{
		RdrDeviceInfo *** device_infos = (RdrDeviceInfo ***)pUserData;
		RdrDeviceInfo *device_info;
		char buf[256];
		char bufOutputName[64];
		UINT mode_index;
		UINT numModes = 0, totalModes = 0;
		HRESULT resultGetModeList;
		DXGI_MODE_DESC * pDisplayModes = NULL;
		DXGI_FORMAT modeFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		UINT numSupportedFormatModes[DXGI_NUM_TOTAL_FORMATS] = { 0 };
		DISPLAY_DEVICE monitor_dev_info;
		U32 formatIndex = 0;
		U32 numFormats = DXGI_NUM_STANDARD_FORMATS;
		if (rdr_state.supportHighPrecisionDisplays)
			numFormats = DXGI_NUM_TOTAL_FORMATS;

		WideToUTF8StrConvert(pDesc->Description, SAFESTR(buf));
		WideToUTF8StrConvert(pOutputDesc->DeviceName, SAFESTR(bufOutputName));

		memlog_printf(NULL, "DX11 Adapter %s Output %s", buf, bufOutputName);
#define MEGABYTE (1024*1024)
		memlog_printf(NULL, "VID 0x%x DID 0x%x SubSys 0x%x Rev 0x%x "
			"VRAM %u MB Dedicated RAM %u MB Shared RAM %u MB"
			"Adapter LUID 0x%08x%08x",
			pDesc->VendorId, pDesc->DeviceId, pDesc->SubSysId, pDesc->Revision, 
			pDesc->DedicatedVideoMemory / MEGABYTE, pDesc->DedicatedSystemMemory / MEGABYTE, 
			pDesc->SharedSystemMemory / MEGABYTE, pDesc->AdapterLuid.HighPart, pDesc->AdapterLuid.LowPart);

		monitor_dev_info.cb = sizeof(monitor_dev_info);
		if (!EnumDisplayDevices_UTF8(bufOutputName, 0, &monitor_dev_info, 0))
		{
			memlog_printf(NULL, "EnumDisplayDevices failed, GetLastError() = %u.", GetLastError());
			memset(&monitor_dev_info, 0, sizeof(monitor_dev_info));
		}

		for (formatIndex = 0; formatIndex < numFormats; ++formatIndex)
		{
			modeFormat = dxgiDisplayFormats[formatIndex];
			resultGetModeList = EnumerateDXGIModes(pOutput, buf, bufOutputName, modeFormat, &numModes, NULL, true);
			if (SUCCEEDED(resultGetModeList))
			{
				numSupportedFormatModes[formatIndex] = numModes;
				totalModes += numModes;
			}
		}

		{
			// list the counts of modes the adapter's output provides for all display formats
			char strMessage[256];
			sprintf(strMessage, "Adapter has the following display modes: %d BGRA8, %d RGBA8, %d BGRA8 SRGB, %d RGBA8 SRGB, %d 2101010, %d XR 2101010, %d FP16\n",
				numSupportedFormatModes[0], numSupportedFormatModes[1], numSupportedFormatModes[2], 
				numSupportedFormatModes[3], numSupportedFormatModes[4], numSupportedFormatModes[5],
				numSupportedFormatModes[6]);
			memlog_printf(NULL, "%s", strMessage);
			if (!totalModes)
			{
				ErrorDetailsf("%s", strMessage);
				ErrorDeferredf("DX11 Adapter has no fullscreen modes");
			}
		}

		pDisplayModes = malloc(sizeof(*pDisplayModes) * totalModes);
		numModes = 0;
		totalModes = 0;
		for (formatIndex = 0; formatIndex < numFormats; ++formatIndex)
		{
			modeFormat = dxgiDisplayFormats[formatIndex];
			numModes = numSupportedFormatModes[formatIndex];
			resultGetModeList = EnumerateDXGIModes(pOutput, buf, bufOutputName, modeFormat, &numModes, &pDisplayModes[totalModes], true);
			if (SUCCEEDED(resultGetModeList))
				totalModes += numModes;
			else
			if (resultGetModeList == DXGI_ERROR_MORE_DATA)
			{
				// Retry the entire operation, per MSDN documentation on GetDisplayModeList, as the
				// user may have attached another output
				SAFE_FREE(pDisplayModes);
				return DXGIEnum_RetryOutput;
			}
		}

		if (!totalModes)
		{
			// No fullscreen support in this case, but fail silently
			memlog_printf(NULL, "DXGI was not able to retrieve any full screen display modes. See generic memlog.\n");
			SAFE_FREE(pDisplayModes);
			return DXGIEnum_ContinueEnum;
		}

		device_info = callocStruct(RdrDeviceInfo);
		device_info->monitor_handle = pOutputDesc->Monitor;
		device_info->monitor_index = multimonFindMonitorHMonitor(pOutputDesc->Monitor);
		device_info->adapter_index = adapterNum;
		device_info->output_index = outputNum;
		device_info->name = allocAddCaseSensitiveString(buf);
		device_info->type = "Direct3D11";
		device_info->createFunction = rdrCreateDeviceXBox11;
		eaPush(device_infos, device_info);

		eaSetCapacity(&device_info->display_modes, numModes);

		for (mode_index = 0; mode_index < totalModes; ++mode_index)
		{
			const DXGI_MODE_DESC * pDXGIMode = pDisplayModes + mode_index;
			RdrDeviceModeDX * pRdrMode;

			/*
			// DJR - make mode list shorter for my debug convenience
			if (pDXGIMode->ScanlineOrdering != DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE)
				continue;
			if (eaSize(&device_info->display_modes))
			{
				const RdrDeviceMode * last_mode = device_info->display_modes[eaSize(&device_info->display_modes) - 1];
				if (last_mode->width == (int)pDXGIMode->Width && last_mode->height == (int)pDXGIMode->Height)
					continue;
			}
			*/

			pRdrMode = (RdrDeviceModeDX*)malloc(sizeof(RdrDeviceModeDX));
			ZeroStruct(pRdrMode);
			eaPush(&device_info->display_modes, &pRdrMode->base);
			pRdrMode->dxgi_mode = *pDXGIMode;
			pRdrMode->base.width = pDXGIMode->Width;
			pRdrMode->base.height = pDXGIMode->Height;
			pRdrMode->base.display_format = rxbxDXGIToCrypticDisplayFormat(pDXGIMode->Format);
			if (pDXGIMode->RefreshRate.Denominator)
				pRdrMode->base.refresh_rate = (pDXGIMode->RefreshRate.Numerator + pDXGIMode->RefreshRate.Denominator / 2) / pDXGIMode->RefreshRate.Denominator;
			else
				pRdrMode->base.refresh_rate = pDXGIMode->RefreshRate.Numerator;
			estrPrintf(&pRdrMode->base.name, "%d x %d @ %d Hz %s %s Mon %d %d", pDXGIMode->Width, pDXGIMode->Height, pRdrMode->base.refresh_rate,
				getScanOutString(pDXGIMode->ScanlineOrdering), getDisplayScalingModeString(pDXGIMode->Scaling), device_info->monitor_index + 1, 
				pRdrMode->base.display_format);
			if (rdr_state.bLogAllAdapterModes)
				printf("Mode %d %s\n", eaSize(&device_info->display_modes), pRdrMode->base.name);
		}
		SAFE_FREE(pDisplayModes);
	}
	return DXGIEnum_ContinueEnum;
}

#if !PLATFORM_CONSOLE
static int alwaysAllowNVPerfHUD = 0;
AUTO_CMD_INT(alwaysAllowNVPerfHUD, alwaysAllowNVPerfHUD) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
#endif

#define TEST_ADAPTER_FAIL 0

#define MAX_D3D9_DISPLAY_FORMATS 6
static const D3DFORMAT displayFormatsD3D9[MAX_D3D9_DISPLAY_FORMATS] = { D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, 
	D3DFMT_A1R5G5B5, D3DFMT_X1R5G5B5, D3DFMT_R5G6B5, D3DFMT_A2R10G10B10 };
static const int D3D9_X8R8G8B8_FORMAT_INDEX = 0;
static const int D3D9_A8R8G8B8_FORMAT_INDEX = 1;

HRESULT rxbxCreateD3D9Ex(IDirect3D9Ex ** ppD3D9Ex)
{
	HRESULT hr = E_FAIL;
	HMODULE libHandle = NULL;

	// Retrieve the d3d9.dll library handle - this library should already be loaded
	// because GameClient statically links it.
	libHandle = LoadLibrary(L"d3d9.dll");

	if (libHandle != NULL)
	{
		// Define a function pointer to the Direct3DCreate9Ex function.
		typedef HRESULT (WINAPI *LPDIRECT3DCREATE9EX)( UINT, void **);

		// Obtain the address of the Direct3DCreate9Ex function. 
		LPDIRECT3DCREATE9EX Direct3DCreate9ExPtr = NULL;

		Direct3DCreate9ExPtr = (LPDIRECT3DCREATE9EX)GetProcAddress( libHandle, "Direct3DCreate9Ex" );

		if (Direct3DCreate9ExPtr != NULL)
			hr = Direct3DCreate9ExPtr(D3D_SDK_VERSION, ppD3D9Ex);
		else
			// Direct3DCreate9Ex is not supported on this
			// operating system.
			hr = ERROR_NOT_SUPPORTED;

		// Release the extra reference to the library.
		FreeLibrary( libHandle );
	}
	return hr;
}

static void rxbxDeviceEnumFunc(RdrDeviceInfo ***device_infos)
{
#if _PS3
	{
		RdrDeviceInfo *device_info;
		device_info = callocStruct(RdrDeviceInfo);
		device_info->monitor_index = -1;
		device_info->name = "RSX_PS3";
		device_info->type = "GCM_PPU_PS3";
		device_info->createFunction = rdrCreateDeviceXBox;
		eaPush(device_infos, device_info);
	}
#elif _XBOX
	{
		RdrDeviceInfo *device_info;
		device_info = callocStruct(RdrDeviceInfo);
		device_info->monitor_index = -1;
		device_info->name = "Xbox360";
		device_info->type = "Direct3D9";
		device_info->createFunction = rdrCreateDeviceXBox;
		eaPush(device_infos, device_info);
	}
#else

	// This indicates Remote Desktop, unknown if it indicates other remote session types.
	memlog_printf(NULL, "Remote Desktop (RDP): %s", GetSystemMetrics(SM_REMOTESESSION) ? "Yes" : "No");

	if (system_specs.isDx11Enabled)
		system_specs.supportedDXVersion = GetSupportedDX11Version();

	if (system_specs.isDx11Enabled && system_specs.supportedDXVersion >= 10.1f)
	{
		rxbxEnumDXGIOutputs(rxbxEnumDevicesDXGIOutputHandler, device_infos);
	}

	if (system_specs.isVista && system_specs.isDx9ExEnabled)
		rxbxEnumD3D9Adapters(true, device_infos);
	if (system_specs.isDx11Enabled && system_specs.supportedDXVersion == 10.0f)
	{
		rxbxEnumDXGIOutputs(rxbxEnumDevicesDXGIOutputHandler, device_infos);
	}

	rxbxEnumD3D9Adapters(false, device_infos);
#endif
}

static void rxbxEnumD3D9Adapters(bool bDirect3D9Ex, RdrDeviceInfo ***device_infos)
{
	LPDIRECT3D9EX d3dex = NULL;
	LPDIRECT3D9 d3d = NULL;
	UINT Adapter = 0;

	// Prevent "inheriting" prior error codes.
	SetLastError(ERROR_SUCCESS);
	if (bDirect3D9Ex)
	{
		HRESULT hrCreateEx = rxbxCreateD3D9Ex(&d3dex);
		if (FAILED(hrCreateEx))
		{
			char *pErrorMessageText = NULL;
			char strFullErrorMessageText[256];
			DWORD nGLE = GetLastError();

			if (!FormatMessage_UTF8(FORMAT_MESSAGE_FROM_SYSTEM, NULL, nGLE, 0, &pErrorMessageText, NULL))
			{
				estrCopy2(&pErrorMessageText, "Unknown or lookup failed.");
			}
			sprintf(strFullErrorMessageText, "Direct3DCreate9Ex(D3D_SDK_VERSION=0x%x) failed. GetLastError() = %x \"%s\"", D3D_SDK_VERSION, nGLE, pErrorMessageText);

			memlog_printf(NULL, "%s", strFullErrorMessageText);
			ErrorDetailsf("%s", strFullErrorMessageText);
			estrDestroy(&pErrorMessageText);
		}
		else
			d3d = (LPDIRECT3D9)d3dex;
	}
	else
	{
		d3d = Direct3DCreate9(D3D_SDK_VERSION);
		if (!d3d)
		{
			char *pErrorMessageText = NULL;
			DWORD nGLE = GetLastError();
			if (!FormatMessage_UTF8(FORMAT_MESSAGE_FROM_SYSTEM, NULL, nGLE, 0, &pErrorMessageText, NULL))
			{
				estrCopy2(&pErrorMessageText, "Unknown or lookup failed.");
			}

			ErrorDetailsf("Direct3DCreate9(D3D_SDK_VERSION=0x%x) failed. GetLastError() = %x \"%s\"", D3D_SDK_VERSION, nGLE, pErrorMessageText);
			FatalErrorf("Unable to initialize Direct3D.  This application requires Direct3D 9.0c.  This failure may indicate the installed Direct3D version does not meet that requirement.  Rebooting, reinstalling DirectX, updating video drivers, or contacting customer service may help resolve this issue.");
			estrDestroy(&pErrorMessageText);
		}
	}

	if (d3d)
	{
		UINT NumAdapters = IDirect3D9_GetAdapterCount(d3d);
		UINT perfHUDState = (isDevelopmentMode() || alwaysAllowNVPerfHUD) ? 1 : 0;
		for (Adapter=0; Adapter < NumAdapters; Adapter++)
		{
			D3DADAPTER_IDENTIFIER9 Identifier;
			HRESULT Res;
			RdrDeviceInfo *device_info;
			UINT mode_index, numModes, numSupportedFormatModes[MAX_D3D9_DISPLAY_FORMATS];
			D3DFORMAT fullscreen_format = displayFormatsD3D9[D3D9_X8R8G8B8_FORMAT_INDEX];
			D3DDISPLAYMODE currentAdapterMode = { 0 };
			D3DDISPLAYMODEEX currentAdapterModeEx = { sizeof(currentAdapterModeEx), 0 };
			D3DDISPLAYMODE adapterMode9 = { 0 };
			D3DDISPLAYMODEFILTER adapterModeFilter9 = { sizeof(adapterModeFilter9), D3DFMT_UNKNOWN, D3DSCANLINEORDERING_PROGRESSIVE };

			Res = IDirect3D9_GetAdapterIdentifier(d3d, Adapter, 0, &Identifier);
			assert(SUCCEEDED(Res));

			if (perfHUDState)
			{
				// Look for 'NVIDIA NVPerfHUD' adapter
				// If it is present, override default settings
				if (strstr(Identifier.Description,"PerfHUD") != 0)
				{
					// clear current device info list so PerfHUD device will be lone entry
					eaDestroy(device_infos);
					rdr_state.usingNVPerfHUD = 1;
					perfHUDState = 2;

					// terminate the loop after enumerating the PerfHUD device
					NumAdapters = Adapter;
				}
			}

			device_info = callocStruct(RdrDeviceInfo);
			device_info->monitor_handle = IDirect3D9_GetAdapterMonitor(d3d, Adapter);
			device_info->monitor_index = multimonFindMonitorHMonitor(device_info->monitor_handle);
			device_info->adapter_index = Adapter;
			device_info->output_index = Adapter;
			device_info->name = allocAddCaseSensitiveString(Identifier.Description);
			device_info->type = bDirect3D9Ex ? DEVICETYPE_D3D9EX : DEVICETYPE_D3D9;
			device_info->createFunction = rdrCreateDeviceXBox;

			for (numModes = 0; numModes < MAX_D3D9_DISPLAY_FORMATS; ++numModes)
			{
				if (d3dex)
				{
					adapterModeFilter9.Format = displayFormatsD3D9[numModes];
					numSupportedFormatModes[numModes] = IDirect3D9Ex_GetAdapterModeCountEx(d3dex, Adapter, &adapterModeFilter9);
				}
				else
					numSupportedFormatModes[numModes] = IDirect3D9_GetAdapterModeCount(d3d, Adapter, displayFormatsD3D9[numModes]);
			}
			numModes = numSupportedFormatModes[D3D9_X8R8G8B8_FORMAT_INDEX];
			if (!numModes)
			{
				fullscreen_format = displayFormatsD3D9[D3D9_A8R8G8B8_FORMAT_INDEX];
				numModes = numSupportedFormatModes[D3D9_A8R8G8B8_FORMAT_INDEX];
			}
#if TEST_ADAPTER_FAIL
			numModes = 0;
#endif
			eaSetCapacity(&device_info->display_modes, numModes);

			memlog_printf(NULL, "DX9 Adapter %s Output %d %s %s", device_info->name, device_info->monitor_index + 1, Identifier.Driver, Identifier.DeviceName);
			memlog_printf(NULL, "Driver ver %u.%u VID 0x%x DID 0x%x SubSys 0x%x Rev 0x%x "
				"DevIdent {%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x} WHQL %u", 
				Identifier.DriverVersion.HighPart, Identifier.DriverVersion.LowPart, 
				Identifier.VendorId, Identifier.DeviceId, Identifier.SubSysId, Identifier.Revision, 
				Identifier.DeviceIdentifier.Data1, Identifier.DeviceIdentifier.Data2, 
				Identifier.DeviceIdentifier.Data3, Identifier.DeviceIdentifier.Data4[0], 
				Identifier.DeviceIdentifier.Data4[1], Identifier.DeviceIdentifier.Data4[2],
				Identifier.DeviceIdentifier.Data4[3], Identifier.DeviceIdentifier.Data4[4], 
				Identifier.DeviceIdentifier.Data4[5], Identifier.DeviceIdentifier.Data4[6],
				Identifier.DeviceIdentifier.Data4[7], Identifier.WHQLLevel);
			if (d3dex)
			{
				D3DDISPLAYROTATION displayRotation = D3DDISPLAYROTATION_IDENTITY;
				Res = IDirect3D9Ex_GetAdapterDisplayModeEx(d3dex, Adapter, &currentAdapterModeEx, &displayRotation);
			}
			else
			{
				Res = IDirect3D9_GetAdapterDisplayMode(d3d, Adapter, &currentAdapterMode);
				currentAdapterModeEx.Width = currentAdapterMode.Width;
				currentAdapterModeEx.Height = currentAdapterMode.Height;
				currentAdapterModeEx.RefreshRate = currentAdapterMode.RefreshRate;
				currentAdapterModeEx.Format = currentAdapterMode.Format;
				currentAdapterModeEx.ScanLineOrdering = D3DSCANLINEORDERING_UNKNOWN;
			}
			if (FAILED(Res))
				memlog_printf(NULL, "GetAdapterDisplayMode%s failed with hr = 0x%x, GetLastError() = %u", d3dex ? "Ex" : "",
					Res, GetLastError());
			else
				memlog_printf(NULL, "Adapter current mode %u x %u @ %u Hz %s %u", 
					currentAdapterModeEx.Width, currentAdapterModeEx.Height, 
					currentAdapterModeEx.RefreshRate, 
					rxbxGetTextureFormatString(currentAdapterModeEx.Format),
					currentAdapterModeEx.ScanLineOrdering);

			for (mode_index = 0; mode_index < numModes; ++mode_index)
			{
				RdrDeviceModeDX * pRdrMode = (RdrDeviceModeDX*)malloc(sizeof(RdrDeviceModeDX));
				
				ZeroStruct(pRdrMode);
				eaPush(&device_info->display_modes, &pRdrMode->base);
				if (d3dex)
				{
					adapterModeFilter9.Format = fullscreen_format;
					pRdrMode->d3d9_mode.Size = sizeof(pRdrMode->d3d9_mode);
					Res = IDirect3D9Ex_EnumAdapterModesEx(d3dex, Adapter, &adapterModeFilter9, mode_index, &pRdrMode->d3d9_mode);
					assert(SUCCEEDED(Res));
				}
				else
				{
					Res = IDirect3D9_EnumAdapterModes(d3d, Adapter, fullscreen_format, mode_index, &adapterMode9);
					assert(SUCCEEDED(Res));
					pRdrMode->d3d9_mode.Size = 0;	// This indicates it's a D3D9 mode, not D3D9Ex.
					pRdrMode->d3d9_mode.Width = adapterMode9.Width;
					pRdrMode->d3d9_mode.Height = adapterMode9.Height;
					pRdrMode->d3d9_mode.RefreshRate = adapterMode9.RefreshRate;
					pRdrMode->d3d9_mode.Format = adapterMode9.Format;
					pRdrMode->d3d9_mode.ScanLineOrdering = D3DSCANLINEORDERING_UNKNOWN;
				}

				estrPrintf(&pRdrMode->base.name, "%d x %d @ %d Hz Mon %d", pRdrMode->d3d9_mode.Width, pRdrMode->d3d9_mode.Height, pRdrMode->d3d9_mode.RefreshRate, device_info->monitor_index + 1);
				if (rdr_state.bLogAllAdapterModes)
					printf("Mode %d %s\n", eaSize(&device_info->display_modes), pRdrMode->base.name);
				pRdrMode->base.width = pRdrMode->d3d9_mode.Width;
				pRdrMode->base.height = pRdrMode->d3d9_mode.Height;
				pRdrMode->base.refresh_rate = pRdrMode->d3d9_mode.RefreshRate;
			}

			{
				// list the counts of modes the adapter provides for all display formats
				char strMessage[256];
				sprintf(strMessage, "Adapter has the following display modes: %d RGBX8, %d RGBA8, %d 1555 ARGB, %d X555, %d 565, and %d 2101010\n",
					numSupportedFormatModes[0], numSupportedFormatModes[1], numSupportedFormatModes[2], 
					numSupportedFormatModes[3], numSupportedFormatModes[4], numSupportedFormatModes[5]);
				memlog_printf(NULL, "%s", strMessage);
				if (!numModes)
				{
					ErrorDetailsf("%s", strMessage);
					ErrorDeferredf("DX9 Adapter has no RGBA8 or RGBX8 modes");
				}
			}

			eaPush(device_infos, device_info);
		}
		IDirect3D9_Release(d3d);
	}
}

AUTO_RUN;
void rxbxRegisterDeviceEnumFunc(void)
{
	rdrDeviceAddEnumFunc(rxbxDeviceEnumFunc);
}

static void rxbxDestroyDevice(RdrDevice *device)
{
	int i;
	RdrDeviceDX *xdevice = (RdrDeviceDX *)device;

	if (!xdevice)
		return;

	rxbxNVDestroyStereoHandle();
	rdrLockActiveDevice(device, false);
	rxbxFreeAllCursors(device); // JE: This was previously commented out, perhaps because of bugs I just fixed?
	rdrFreeAllGeometry(device);
	rdrFreeAllTextures(device);
	rdrFreeAllShaders(device);
	wtFlush(device->worker_thread);
	wtQueueCmd(device->worker_thread, RXBXCMD_DESTROY, 0, 0);
	rdrUnlockActiveDevice(device, true, false, false);

	rdrUninitDevice(device);

	stashTableDestroy(xdevice->cursor_cache);

	eaDestroy(&xdevice->surfaces);

	stashTableDestroy(xdevice->texture_data);
	stashTableDestroy(xdevice->geometry_cache);
	for (i=0; i<beaSize(&xdevice->input_signatures); i++)
		SAFE_FREE(xdevice->input_signatures[i].data);
	beaDestroy(&xdevice->input_signatures);

	if (xdevice->d3d11_device)
		rxbxDestroyConstantBuffers(xdevice);

	ZeroStruct(xdevice);
	if (xdevice != &xbox_static_device_for_debug)
		free(xdevice);
	else
		xbox_static_device_for_debug_allocated = 0;

}

#if 0

void simpleXBoxTest(void)
{
	DWORD creationflags = 0;
	HRESULT hr;
	LPDIRECT3D9 d3d;
	D3DDevice *d3d_device;
	D3DPRESENT_PARAMETERS d3d_present_params;
	XVIDEO_MODE VideoMode;
	XGetVideoMode(&VideoMode);

	ZeroStruct(&d3d_present_params);
	d3d_present_params.BackBufferWidth = MIN(VideoMode.dwDisplayWidth, 1280);
	d3d_present_params.BackBufferHeight = MIN(VideoMode.dwDisplayHeight, 720);
	d3d_present_params.BackBufferCount = 1;
	d3d_present_params.BackBufferFormat = D3DFMT_A8R8G8B8;
	d3d_present_params.MultiSampleType = D3DMULTISAMPLE_NONE;
	d3d_present_params.EnableAutoDepthStencil = TRUE;
	d3d_present_params.Flags = 0;
	d3d_present_params.FullScreen_RefreshRateInHz = 0;
	d3d_present_params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	d3d_present_params.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3d_present_params.AutoDepthStencilFormat = D3DFMT_D24S8;

	d3d = Direct3DCreate9(D3D_SDK_VERSION);
	if (FAILED(hr = Direct3D_CreateDevice(0, D3DDEVTYPE_HAL, NULL, creationflags, &d3d_present_params, &d3d_device)))
	{
		Direct3D_Release();
		return;
	}
	Direct3D_Release();


	while (1)
	{
		IDirect3DDevice9_Present(d3d_device);
		Sleep(20);
	}
}

#endif

extern D3DVERTEXELEMENT9 vertex_components9[] =
{
	{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
	{ 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
	{ 0, 0, PACKED_NORMAL_DECLTYPE, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
	{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
	{ 0, 0, D3DDECLTYPE_FLOAT16_2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
	{ 0, 0, D3DDECLTYPE_FLOAT16_4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
	{ 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
	{ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
	{ 0, 0, D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
	{ 0, 0, D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 1 },
	{ 0, 0, D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
	{ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
	{ 0, 0, PACKED_NORMAL_DECLTYPE, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
	{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
	{ 0, 0, PACKED_NORMAL_DECLTYPE, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2 },
	{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2 },
// You can not use this generic float with VBONEWEIGHT, since they occupy the same register
	{ 0, 0, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5 },
	{ 0, 0, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 6 },
	{ 0, 0, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 7 },
#if _XBOX
	{ 0, 0, D3DDECLTYPE_USHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4 },
#else
	{ 0, 0, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4 },
#endif
	{ 0, 0, D3DDECLTYPE_SHORT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4 },
	{ 0, 0, D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5 },
	{ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5 },
	{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 6 },
	{ 0, 0, PACKED_NORMAL_DECLTYPE, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 7 },
	{ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3 },
	{ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4 },
	{ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5 },
	{ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 6 },
	{ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 7 },

#if _XBOX
	{ 0, 0, D3DDECLTYPE_UINT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1 },
#else
	{ 0, 0, D3DDECLTYPE_UDEC3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1 },
#endif

	D3DDECL_END(),
};

STATIC_ASSERT(ARRAY_SIZE(vertex_components9) == VERTEX_COMPONENT_MAX);

extern D3D11_INPUT_ELEMENT_DESC vertex_components11[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VPOS
	{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VPOSSPRITE
	{ "NORMAL", 0, DXGI_FORMAT_R16G16B16A16_SNORM, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VNORMAL_PACKED
	{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VNORMAL32
	{ "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VTEXCOORD16
	{ "TEXCOORD", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // V2TEXCOORD16
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VTEXCOORD32
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // V2TEXCOORD32
	{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VLIGHT
	{ "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VCOLORU8
	{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VCOLORU8_IDX0
	{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VCOLOR
	// TANGENT
	{ "TEXCOORD", 1, DXGI_FORMAT_R16G16B16A16_SNORM, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VTANGENT_PACKED
	{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VTANGENT32
	// BINORMAL
	{ "TEXCOORD", 2, DXGI_FORMAT_R16G16B16A16_SNORM, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VBINORMAL_PACKED
	{ "TEXCOORD", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VBINORMAL32
// You can not use this generic float with VBONEWEIGHT, since they occupy the same register
	{ "TEXCOORD", 5, DXGI_FORMAT_R32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VFLOAT
	{ "TEXCOORD", 6, DXGI_FORMAT_R32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VTIGHTENUP
	{ "TEXCOORD", 7, DXGI_FORMAT_R32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VALPHA
	// BLENDINDICES
	{ "TEXCOORD", 4, DXGI_FORMAT_R16G16B16A16_SINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VBONEIDX
	{ "TEXCOORD", 4, DXGI_FORMAT_R16G16_SINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VSMALLIDX
	{ "TEXCOORD", 5, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VBONEWEIGHT
	{ "TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VBONEWEIGHT32
	{ "TEXCOORD", 6, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VPOS2
	{ "TEXCOORD", 7, DXGI_FORMAT_R16G16B16A16_SNORM, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VNORMAL2_PACKED
	{ "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VINST_MATX
	{ "TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VINST_MATY
	{ "TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VINST_MATZ
	{ "TEXCOORD", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VINST_COLOR
	{ "TEXCOORD", 7, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VINST_PARAM

	{ "TEXCOORD", 1, DXGI_FORMAT_R32_SINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // VINDEX

	{ 0 },
};

STATIC_ASSERT(ARRAY_SIZE(vertex_components11) == VERTEX_COMPONENT_MAX);

U32 rxbxVertexComponentSize(int component_type)
{
	U32 bytes = 0;
	switch (component_type)
	{
	xcase VPOS:
		bytes = sizeof(F32) * 3;
	xcase VPOSSPRITE:
		bytes = sizeof(F32) * 2;
	xcase VNORMAL_PACKED:
		bytes = sizeof(U32);
	xcase VNORMAL32:
		bytes = sizeof(F32) * 3;
	xcase VTEXCOORD16:
		bytes = sizeof(F16) * 2;
	xcase V2TEXCOORD16:
		bytes = sizeof(F16) * 4;
	xcase VTEXCOORD32:
		bytes = sizeof(F32) * 2;
	xcase V2TEXCOORD32:
		bytes = sizeof(F32) * 4;
	xcase VLIGHT:
		bytes = sizeof(F32) * 2;
	xcase VCOLORU8:
		bytes = sizeof(U32);
	xcase VCOLORU8_IDX0:
		bytes = sizeof(U32);
	xcase VCOLOR:
		bytes = sizeof(U32);
	xcase VTANGENT_PACKED:
		bytes = sizeof(S16) * 4;
	xcase VTANGENT32:
		bytes = sizeof(F32) * 3;
	xcase VBINORMAL_PACKED:
		bytes = sizeof(S16) * 4;
	xcase VBINORMAL32:
		bytes = sizeof(F32) * 3;
	// You can not use this generic float with xcase VBONEWEIGHT: since they occupy the same register
	xcase VFLOAT:
		bytes = sizeof(F32);
	xcase VTIGHTENUP:
		bytes = sizeof(F32);
	xcase VALPHA:
		bytes = sizeof(F32);
	xcase VBONEIDX:
		bytes = sizeof(U16) * 4;

	xcase VSMALLIDX:
		bytes = sizeof(U16) * 2;
	xcase VBONEWEIGHT:
		bytes = sizeof(U32);
	xcase VBONEWEIGHT32:
		bytes = sizeof(F32) * 4;
	xcase VPOS2:
		bytes = sizeof(F32) * 2;
	xcase VNORMAL2_PACKED:
		bytes = sizeof(S16) * 4;

	xcase VINST_MATX:
	case VINST_MATY:
	case VINST_MATZ:
	case VINST_COLOR:
	case VINST_PARAM:
		bytes = sizeof(F32) * 4;

	xcase VINDEX:
		bytes = sizeof(S32);

	xcase VTERMINATE:
		bytes = 0;

	xdefault:
		assert(0);
	}

	return bytes;
}

#define MAX_ELEMENTS 20
void rxbxVertexDeclGenerateWithOffsets(VertexDecl * decl, const VertexComponentInfo * components, int num_components, U32 stream)
{
	int element;
	if (!num_components)
		num_components = MAX_ELEMENTS;

	for (element = 0; element < num_components; ++element, ++components)
	{
		if (components->component_type == VTERMINATE)
			break;
		assert(stream != 1); // This would probably be the instanced stream, but I don't think that code should go through here
		rxbxVertexDeclAddElement(decl, components->component_type, components->offset, stream, 0);
	}
	rxbxVertexDeclEnd(decl);
}

HRESULT rxbxCreateVertexDeclaration(RdrDeviceDX *device, const VertexDecl *decl, const RxbxVertexShader *shader, RdrVertexDeclarationObj *vertex_declaration)
{
	HRESULT hr;
	if (device->d3d11_device)
	{
		assert(shader->input_signature_index>0);	// Check to see if a vertex shader failed to compile.
		CHECKX(hr = ID3D11Device_CreateInputLayout(device->d3d11_device, decl->elements11, decl->num_elements,
			device->input_signatures[shader->input_signature_index].data, device->input_signatures[shader->input_signature_index].data_size, &vertex_declaration->layout));
	} else {
		hr = IDirect3DDevice9_CreateVertexDeclaration(device->d3d_device, decl->elements9, &vertex_declaration->decl);
	}
	if (!FAILED(hr))
		rxbxLogCreateVertexDeclaration(device, *vertex_declaration);
	return hr;
}


HRESULT rxbxCreateVertexDeclarationFromDecl(RdrDeviceDX *device, const VertexDecl * decl, RxbxVertexShader * shader, RdrVertexDeclarationObj *vertex_declaration)
{
	return rxbxCreateVertexDeclaration(device, decl, shader, vertex_declaration);
}

HRESULT rxbxCreateVertexDeclarationFromComponents(RdrDeviceDX *device, const VertexComponentInfo * components, int num_components, RxbxVertexShader * shader, RdrVertexDeclarationObj *vertex_declaration)
{
	VertexDecl decl = { device->d3d11_device != NULL, 0 };
	rxbxVertexDeclGenerateWithOffsets(&decl, components, num_components, 0);
	return rxbxCreateVertexDeclaration(device, &decl, shader, vertex_declaration);
}

// Moved here to reference XRenderLib format types as well. Could implement device callbacks if we go back to supporting multiple devices
const char *rdrTexFormatName(RdrTexFormatObj rdr_format)
{
	int i;
	static struct {
		RdrTexFormat rdr_format;
		char *name;
	} formats[] = {
		{RTEX_BGR_U8, "RTEX_BGR_U8"},
		{RTEX_BGRA_U8, "RTEX_BGRA_U8"},
		{RTEX_RGBA_F16, "RTEX_RGBA_F16"},
		{RTEX_RGBA_F32, "RTEX_RGBA_F32"},
		{RTEX_DXT1, "RTEX_DXT1"},
		{RTEX_DXT3, "RTEX_DXT3"},
		{RTEX_DXT5, "RTEX_DXT5"},
		{RTEX_R_F32, "RTEX_R_F32"},
		{RTEX_BGRA_5551, "RTEX_BGRA_5551"},
		{RTEX_A_U8, "RTEX_A_U8"},
		{RTEX_INVALID_FORMAT, "INVALID"},

		// Internal XRenderLib formats
		{RTEX_NULL, "RTEX_NULL"},
		{RTEX_NVIDIA_INTZ, "RTEX_NVIDIA_INTZ"},
		{RTEX_NVIDIA_RAWZ, "RTEX_NVIDIA_RAWZ"},
		{RTEX_ATI_DF16, "RTEX_ATI_DF16"},
		{RTEX_ATI_DF24, "RTEX_ATI_DF24"},
		{RTEX_D24S8, "RTEX_D24S8"},
		{RTEX_G16R16F, "RTEX_G16R16F"},
		{RTEX_G16R16, "RTEX_G16R16"},
		{RTEX_A16B16G16R16, "RTEX_A16B16G16R16"},
		{RTEX_R5G6B5, "RTEX_R5G6B5"},
		{RTEX_RGBA10, "RTEX_RGBA10"},
	};
	for (i=0; i<ARRAY_SIZE(formats); i++) 
		if (rdr_format.format == formats[i].rdr_format)
			return formats[i].name;
	return "UNKNOWN";
}

void LOG_REF_COUNT(const char *type, void *object, int refCount)
{
#if 1
	if (refCount != 0)
	{
		memlog_printf(NULL, "%s: 0x%08p %d refs\n", type, object, refCount);
	}
#endif
}
