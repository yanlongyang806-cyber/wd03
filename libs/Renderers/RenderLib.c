#include "RenderLib.h"

#include "earray.h"
#include "winutil.h"
#include "MemoryMonitor.h"
#include "file.h"
#include "piglib.h"

#include "RdrState.h"
#include "RdrCommandParse.h"
#include "RdrShader.h"
#include "RdrDevicePrivate.h"
#include "RdrLightAssembly.h"
#include "RdrFMV.h"

#include "systemspecs.h"
#include "AutoGen/RdrEnums_h_ast.c"

#if _PS3
#include <sysutil/sysutil_sysparam.h>
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

//////////////////////////////////////////////////////////////////////////

RdrVertexDeclaration rdr_vertex_declarations[RUSE_NUM_COMBINATIONS];

void rdrInitVertexDeclarations(void)
{
	int i;
	for (i = 0; i < RUSE_NUM_COMBINATIONS; i++)
	{
		int texcoordsize = (i&RUSE_TEXCOORDS_HI_FLAG)?sizeof(F32):sizeof(F16);
		int boneweightsize = (i&RUSE_BONEWEIGHTS_HI_FLAG)?sizeof(F32):sizeof(U8);
		RdrVertexDeclaration *decl = &rdr_vertex_declarations[i];
		ZeroStruct(decl);
		if (i & RUSE_POSITIONS)
		{
			decl->position_offset = decl->stride;
			decl->stride += sizeof(Vec3);
		}
		if (i & RUSE_NORMALS)
		{
			decl->normal_offset = decl->stride;
			decl->stride += sizeof(Vec3_Packed);
		}
		if (i & RUSE_TANGENTS)
		{
			decl->tangent_offset = decl->stride;
			decl->stride += sizeof(Vec3_Packed);
		}
		if (i & RUSE_BINORMALS)
		{
			decl->binormal_offset = decl->stride;
			decl->stride += sizeof(Vec3_Packed);
		}
		if ((i & RUSE_TEXCOORDS) || (i & RUSE_TEXCOORD2S))
		{
			decl->texcoord_offset = decl->stride;
			decl->stride += texcoordsize*2;
		}
		if (i & RUSE_TEXCOORD2S)
		{
			decl->texcoord2_offset = decl->stride;
			decl->stride += texcoordsize*2;
		}
		if (i & RUSE_BONEWEIGHTS)
		{
			decl->boneweight_offset = decl->stride;
			decl->stride += boneweightsize * 4;
		}
		if (i & RUSE_BONEIDS)
		{
			decl->boneid_offset = decl->stride;
			decl->stride += sizeof(U16) * 4;
		}
		if (i & RUSE_COLORS)
		{
			decl->color_offset = decl->stride;
			decl->stride += sizeof(U32);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

StaticDefineInt RdrLightTypeEnum[] =
{
	DEFINE_INT
	{ "NONE", RDRLIGHT_NONE},
	{ "DIRECTIONAL", RDRLIGHT_DIRECTIONAL},
	{ "POINT", RDRLIGHT_POINT},
	{ "SPOT", RDRLIGHT_SPOT},
	{ "PROJECTOR", RDRLIGHT_PROJECTOR},
	{ "SHADOWED", RDRLIGHT_SHADOWED},
	DEFINE_END
};


static volatile int last_tex_handle = RDR_FIRST_TEXTURE_GEN, last_geo_handle = 0, last_shader_handle = 0;
static CRITICAL_SECTION surface_texhandle_section;

// Static has been removed for debugging [CO-23282].
RdrSurface **texhandle_surfaces=0;

static void rdrSurfaceTexHandleCSInit()
{
	InitializeCriticalSection(&surface_texhandle_section);
}

void rdrChangeTexHandleFlags(TexHandle *handle, RdrTexFlags flags)
{
	if (handle)
	{
		RdrTexHandle *rth = (RdrTexHandle *)handle;
		rth->sampler_flags = flags;
	}
}

RdrTexFlags rdrGetTexHandleFlags(TexHandle *handle)
{
	if (handle)
	{
		RdrTexHandle *rth = (RdrTexHandle *)handle;
		return rth->sampler_flags;
	}
	return 0;
}

U32 rdrGetTexHandleKey(TexHandle *handle)
{
	if (handle)
	{
		RdrTexHandle *rth = (RdrTexHandle *)handle;
		return rth->texture.hash_value;
	}
	return 0;
}

void rdrAddRemoveTexHandleFlags(TexHandle *handle, RdrTexFlags flags_to_add, RdrTexFlags flags_to_remove)
{
	if (handle)
	{
		RdrTexHandle *rth = (RdrTexHandle *)handle;
		// JE: Removes *must* happen before adds because of what is done in setupTextureReflection()
		rth->sampler_flags &= ~flags_to_remove;
		rth->sampler_flags |= flags_to_add;
	}
}

TexHandle rdrGenTexHandle(RdrTexFlags flags)
{
	RdrTexHandle ret = {0};
	ret.texture.hash_value = InterlockedIncrement(&last_tex_handle);
	rdrChangeTexHandleFlags((TexHandle*)&ret, flags);
	return RdrTexHandleToTexHandle(ret);
}

GeoHandle rdrGenGeoHandle(void)
{
	GeoHandle ret = (GeoHandle)InterlockedIncrement(&last_geo_handle);
	return ret;
}

ShaderHandle rdrGenShaderHandle(void)
{
	ShaderHandle ret = (ShaderHandle)InterlockedIncrement(&last_shader_handle);
	return ret;
}

#define INDEX_MAX	(1 << RDRSURFACE_INDEX_NUMBITS)
#define SET_MAX		(1 << RDRSURFACE_SET_INDEX_NUMBITS)
#define BUFFER_MAX	(1 << RDRSURFACE_BUFFER_NUMBITS)

TexHandle rdrSurfaceToTexHandleEx(RdrSurface *surface, RdrSurfaceBuffer buffer, int set_index, int force_flags, bool unresolved_is_ok)
{
	RdrTexHandle tex_handle = {0};
	int idx;
	int first_free=-1;

	if (!surface)
		return 0;

	EnterCriticalSection(&surface_texhandle_section);

	for (idx=eaSize(&texhandle_surfaces)-1; idx >= 0; idx--) {
		if (texhandle_surfaces[idx] == surface)
			break;
		if (!texhandle_surfaces[idx])
			first_free = idx;
	}
	if (-1 == idx) {
		if (first_free == -1)
			idx = eaPush(&texhandle_surfaces, surface);
		else {
			idx = first_free;
			texhandle_surfaces[idx] = surface;
		}
	}

 	assert(buffer >= 0 && buffer < BUFFER_MAX);
	assert(buffer < SBUF_MAX);
 	assert(set_index >= 0 && set_index < SET_MAX);
 	assert(idx >= 0 && idx < INDEX_MAX);

	tex_handle.is_surface = 1;
	tex_handle.surface.index = idx;
	tex_handle.surface.buffer = buffer;
	tex_handle.surface.no_autoresolve = unresolved_is_ok;
	tex_handle.surface.set_index = set_index;
	

	if (buffer < SBUF_MAXMRT)
		rdrChangeTexHandleFlags((TexHandle*)&tex_handle, surface->default_tex_flags[buffer]|force_flags);
	else // depth buffer
		rdrChangeTexHandleFlags((TexHandle*)&tex_handle, RTF_MIN_POINT|RTF_MAG_POINT|RTF_CLAMP_U|RTF_CLAMP_V|force_flags);

	LeaveCriticalSection(&surface_texhandle_section);

	return RdrTexHandleToTexHandle(tex_handle);
}

void rdrRemoveSurfaceTexHandle(RdrSurface *surface)
{
	int idx;

	EnterCriticalSection(&surface_texhandle_section);
	if ((idx = eaFind(&texhandle_surfaces, surface)) >= 0)
		texhandle_surfaces[idx] = 0;
	LeaveCriticalSection(&surface_texhandle_section);
}

RdrSurface *rdrGetSurfaceForTexHandle(RdrTexHandle tex_handle)
{
	RdrSurface *surface = NULL;
	
	if (!tex_handle.is_surface)
		return NULL;

	EnterCriticalSection(&surface_texhandle_section);
	if (texhandle_surfaces)
	{
		assert((int)tex_handle.surface.index < eaSize(&texhandle_surfaces));
		ANALYSIS_ASSUME(texhandle_surfaces);
		surface = texhandle_surfaces[tex_handle.surface.index];
	}
	LeaveCriticalSection(&surface_texhandle_section);

	return surface;
}

//////////////////////////////////////////////////////////////////////////




//////////////////////////////////////////////////////////////////////////

#define MAX_MONS 10

typedef struct MonitorResolutionCache{
	GfxResolution **supported_resolutions;
	GfxResolution desktop_resolution;
} MonitorResolutionCache;

static MonitorResolutionCache monitor_resolution_cache[MAX_MONS] = {0};

SA_ORET_NN_VALID GfxResolution **rdrGetSupportedResolutions(GfxResolution **desktop_res, int monitor_index, const char * device_type)
{
	GfxResolution *gr;
	MonitorResolutionCache * mon_cache = &monitor_resolution_cache[monitor_index];

	// always returning complete list for all monitors
	int numMonitors = multiMonGetNumMonitors();
	if (monitor_index == -1)
		monitor_index = multimonGetPrimaryMonitor();
	MIN1(monitor_index, MIN(numMonitors-1, MAX_MONS-1));
	MAX1(monitor_index, 0);

	if (mon_cache->supported_resolutions)
	{
		*desktop_res = &mon_cache->desktop_resolution;
		return mon_cache->supported_resolutions;
	}

#if _XBOX
	{
		XVIDEO_MODE VideoMode;
		XGetVideoMode(&VideoMode);

		gr = calloc(1,sizeof(GfxResolution));
		eaPush(&supported_resolutions, gr);
		desktop_resolution.width = gr->width = MIN(VideoMode.dwDisplayWidth, 1280);
		desktop_resolution.height = gr->height = MIN(VideoMode.dwDisplayHeight, 720);
		desktop_resolution.depth = gr->depth = 32;
        eaiPush(&gr->refreshRates, round(VideoMode.RefreshRate));
		// VideoMode.fIsWideScreen;
	}
#else
	{
		int success, modeNum, modeMax;
		DEVMODE dm = {0};
		// Find the Device Name for the currently selected monitor
		MONITORINFOEX moninfo;
		const WCHAR *deviceName;
		const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
		int adapter_for_monitor = rdrGetDeviceForMonitor(monitor_index, device_type);
		const RdrDeviceInfo * device = device_infos[adapter_for_monitor];
		

		multiMonGetMonitorInfo(monitor_index, &moninfo);
		deviceName = moninfo.szDevice;

		dm.dmSize = sizeof(dm);
		success = EnumDisplaySettingsEx(deviceName, ENUM_REGISTRY_SETTINGS,	&dm, 0 );

		// EnumDisplaySettingsEx returns success when running over RDP even though it fails.
		if (success && dm.dmPelsHeight > 0)
		{
			mon_cache->desktop_resolution.width = dm.dmPelsWidth;
			mon_cache->desktop_resolution.height = dm.dmPelsHeight;
			mon_cache->desktop_resolution.depth = dm.dmBitsPerPel;
		}
		else
		{
			mon_cache->desktop_resolution.width = GetSystemMetrics(SM_CXSCREEN);
			mon_cache->desktop_resolution.height = GetSystemMetrics(SM_CYSCREEN);
			mon_cache->desktop_resolution.depth = 32;
		}
		mon_cache->desktop_resolution.adapter = adapter_for_monitor;

		if (device_type)
		{
			for (modeNum = 0, modeMax = eaSize(&device->display_modes); modeNum < modeMax; ++modeNum)
			{
				const RdrDeviceMode * displayMode = device->display_modes[modeNum];
				if(displayMode->width >= 780 && displayMode->height >= 580 || displayMode->width >= 580 && displayMode->height >= 780)
				{
					gr = calloc(1,sizeof(GfxResolution));
					eaPush(&monitor_resolution_cache[monitor_index].supported_resolutions, gr);

					gr->width = displayMode->width;
					gr->height = displayMode->height;
					// DX11 TODO enumerate the bit-depth in the RdrDeviceMode struct; but currently
					// those modes only list 32-bit modes
					gr->depth = 32;
					gr->adapter = adapter_for_monitor;
					gr->displayMode = modeNum;
					eaiPushUnique(&gr->refreshRates, displayMode->refresh_rate);
				}
			}
		}
	}
#endif

	*desktop_res = &monitor_resolution_cache[monitor_index].desktop_resolution;
	return monitor_resolution_cache[monitor_index].supported_resolutions;
}

void rdrGetClosestResolution(int *width, int *height, int *refreshRate, int fullscreen, int monitor_index)
{
	GfxResolution *desktop_resolution, **supported_resolutions;
	supported_resolutions = rdrGetSupportedResolutions(&desktop_resolution, monitor_index, NULL);

	if (fullscreen)
	{
		GfxResolution *closest = NULL;
		F32 closest_dist;
		int i;

		// find closest matching resolution
		for (i = 0; i < eaSize(&supported_resolutions); ++i)
		{
			GfxResolution *res = supported_resolutions[i];
			F32 res_dist;

			if (res->depth != 32)
				continue;

			res_dist = sqrtf(SQR(res->width - *width) + SQR(res->height - *height));
			if (!closest || res_dist < closest_dist)
			{
				closest = res;
				closest_dist = res_dist;
			}
		}

		if (closest)
		{
			int closest_refresh = closest->refreshRates?closest->refreshRates[0]:0;
			closest_dist = fabsf(closest_refresh - *refreshRate);

			for (i = 1; i < eaiSize(&closest->refreshRates); ++i)
			{
				F32 ref_dist = fabsf(*refreshRate - closest->refreshRates[i]);
				if (ref_dist < closest_dist)
				{
					closest_refresh = closest->refreshRates[i];
					closest_dist = ref_dist;
				}
			}

			*width = closest->width;
			*height = closest->height;
			*refreshRate = closest_refresh;
		}
	}
	else
	{
		// fit window on desktop
		MIN1(*width, desktop_resolution->width);
		MIN1(*height, desktop_resolution->height);
	}
}

//////////////////////////////////////////////////////////////////////////

// equivalent to glFrustum( l, r, b, t, znear, zfar ):
void rdrSetupFrustumGL(Mat44 projection_matrix, F32 l, F32 r, F32 b, F32 t, F32 znear, F32 zfar)
{
	setVec4(projection_matrix[0], 2*znear/(r-l), 0, 0, 0);
	setVec4(projection_matrix[1], 0, 2*znear/(t-b), 0, 0);
	setVec4(projection_matrix[2], (r+l)/(r-l), (t+b)/(t-b), -(zfar+znear)/(zfar-znear), -1);
	setVec4(projection_matrix[3], 0, 0, -2*zfar*znear/(zfar-znear), 0);
}

void rdrSetupFrustumDX(Mat44 projection_matrix, F32 l, F32 r, F32 b, F32 t, F32 znear, F32 zfar)
{
	setVec4(projection_matrix[0], 2*znear/(r-l), 0, 0, 0);
	setVec4(projection_matrix[1], 0, 2*znear/(t-b), 0, 0);
	setVec4(projection_matrix[2], (r+l)/(r-l), (t+b)/(t-b), zfar/(znear-zfar), -1);
	setVec4(projection_matrix[3], 0, 0, zfar*znear/(znear-zfar), 0);
}

// equivalent to glOrtho( l, r, b, t, znear, zfar ):
void rdrSetupOrthoGL(Mat44 projection_matrix, F32 l, F32 r, F32 b, F32 t, F32 znear, F32 zfar)
{
	setVec4(projection_matrix[0], 2/(r-l), 0, 0, 0);
	setVec4(projection_matrix[1], 0, 2/(t-b), 0, 0);
	setVec4(projection_matrix[2], 0, 0, -2/(zfar-znear), 0);
	setVec4(projection_matrix[3], -(r+l)/(r-l), -(t+b)/(t-b), -(zfar+znear)/(zfar-znear), 1);
}

void rdrSetupOrthoDX(Mat44 projection_matrix, F32 l, F32 r, F32 b, F32 t, F32 znear, F32 zfar)
{
	setVec4(projection_matrix[0], 2/(r-l), 0, 0, 0);
	setVec4(projection_matrix[1], 0, 2/(t-b), 0, 0);
	setVec4(projection_matrix[2], 0, 0, 1/(znear-zfar), 0);
	setVec4(projection_matrix[3], (l+r)/(l-r), (t+b)/(b-t), znear/(znear-zfar), 1);
}

// equivalent to gluPerspective( fovy, aspect, znear, zfar ):
void rdrSetupPerspectiveProjection(Mat44 projection_matrix, F32 fovy, F32 aspect, F32 znear, F32 zfar)
{
	F32 l, r, b, t;

	t = znear * (F32)tan(fovy * PI / 360.0);
	b = -t;
	l = b * aspect;
	r = t * aspect;

	rdrSetupFrustumDX(projection_matrix, l, r, b, t, znear, zfar);
}

// equivalent to glOrtho( ((float)width/height)*-ortho_zoom, ((float)width/height)*ortho_zoom, -ortho_zoom, ortho_zoom, 10, 50000):
void rdrSetupOrthographicProjection(Mat44 projection_matrix, F32 aspect, F32 ortho_zoom)
{
	F32 l = aspect*-ortho_zoom;
	F32 r = aspect*ortho_zoom;
	F32 b = -ortho_zoom;
	F32 t = ortho_zoom;
	F32 n = -2000;
	F32 f = 30000;

	rdrSetupOrthoDX(projection_matrix, l, r, b, t, n, f);
}

PrintfFunc rdrStatusPrintf = printf_timed;
void rdrSetStatusPrintf(PrintfFunc printf_func)
{
	rdrStatusPrintf = printf_func;
}

static int rdrStartExec = 0;
void rdrStartup(void)
{
	if ( rdrStartExec )
		return;
	rdrStartExec = 1;

	rdrTextureLoadInit(rdr_state.texLoadPoolSize);

	rdrInitUberLightParams();

	rdrInitVertexDeclarations();
	// Moved to after we display the logo in the GameClient, auto-loaded elsewhere
	if (PigSetInited())
		rdrShaderLibInit();
	rdrSurfaceTexHandleCSInit();

	rdr_state.lowResAlphaMaxDist = 300;
	rdr_state.lowResAlphaMinDist = -300;

	systemSpecsInit();
	rdr_state.gpu_count = MAX(MAX(system_specs.nvidiaSLIGPUCount, system_specs.atiCrossfireGPUCount), 1);

#if !PLATFORM_CONSOLE
	if (rdr_state.max_gpu_frames_ahead==-1)
		rdr_state.max_gpu_frames_ahead = rdr_state.gpu_count;
#endif

#if _PS3
    //YVS

    rdr_state.disableLowResAlpha = 1; // until we fix it

#elif _XBOX
	if (isDevelopmentMode()) {
		DM_SYSTEM_INFO si;
		si.SizeOfStruct = sizeof(si);
		DmGetSystemInfo(&si);
		if (si.BaseKernelVersion.Build < XDK_FLASH_DESIRED_VERSION) {
			Errorf("Your Xbox Flash version is too old (yours: %d, required: " LINE_STR_INTERNAL(DESIRED_VERSION) "), please update by running " XDK_FLASH_DESIRED_VERSION_LOCATION, si.BaseKernelVersion.Build);
		}
	}
#endif

}

int rdrMaxSupportedObjectLights( void )
{
	if( systemSpecsMaterialSupportedFeatures() & SGFEAT_SM30_PLUS ) {
		return MAX_NUM_OBJECT_LIGHTS;
	} else {
		return 2;
	}
}

// Update any cached values here
void rdrUpdateGlobalsOnDeviceLock(RdrDevice* device)
{
	// This is queried many times in the main rendering loop, and I
	// don't want to do an indirect function call per object. -- MJF
	rdr_state.lowResAlphaUnsupported = (!rdrSupportsFeature(device, FEATURE_DEPTH_TEXTURE)
										|| !rdrSupportsFeature(device, FEATURE_SM30)
										//|| !rdrSupportsFeature(device, FEATURE_SM2B) - we probably want this instead, but haven't had a chance to test on a SM2B (ATI X###) card
										|| (rdr_state.msaaQuality > 1 && !rdrSupportsFeature(device, FEATURE_DEPTH_TEXTURE_MSAA)));
	rdr_state.lowResAlphaHighResNeedsManualDepthTest = !rdrSupportsFeature(device, FEATURE_STENCIL_DEPTH_TEXTURE) && rdrLowResAlphaEnabled();

	rdr_state.nvidiaCSAASupported = rdrSupportsFeature(device, FEATURE_NV_CSAA_SURFACE);
	rdr_state.dx11Renderer |= rdrSupportsFeature(device, FEATURE_DX11_RENDERER);
}

void rdrDisableLowResAlpha( bool low_res_alpha_disabled )
{
	rdr_state.disableLowResAlpha = low_res_alpha_disabled;
}

void rdrSetDisableToneCurve10PercentBoost(bool disable)
{
	rdr_state.disableToneCurve10PercentBoost = disable;
}

// Remember, anything called here is prior to the parameters being called, so most likely the real work will need to be deferred to a later point.
// This function is primarily used for notifications that the DisplayParams will change.
void rdrDisplayParamsPreChange(RdrDevice *device, DisplayParams *new_params)
{
	if ((new_params->stereoscopicActive != device->display_nonthread.stereoscopicActive) && device->gfx_lib_display_params_change_cb)
	{
		device->gfx_lib_display_params_change_cb(device);
	}
}
