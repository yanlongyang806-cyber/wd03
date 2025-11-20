
#include "rt_xshader.h"
#include "../RdrShaderPrivate.h"
#include "RenderLib.h"
#include "rt_xShaderServerInterface.h"
#include "rt_xpostprocessing.h"
#include "rt_xdevice.h"
#include "rt_xdrawmode.h"
#include "rt_xsurface.h"
#include "ShaderServerInterface.h"
#include "RdrState.h"
#include "ThreadManager.h"
#include "XboxThreads.h"
#include "RegistryReader.h"
#include "TimedCallback.h"
#include "structInternals.h"
#include "SystemSpecs.h"
#include "crypt.h"
#include "GenericPreProcess.h"
#include "MemAlloc.h"
#include "logging.h"
#include "StringUtil.h"
#include "BlockEarray.h"
#include "D3DCompiler.h"
#include "WorkerThread.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:rtCompileThread", BUDGET_Renderer););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:rxbxBackgroundShaderCompileThread", BUDGET_Renderer););
AUTO_RUN_ANON(memBudgetAddMapping("rxbx:PixelShaders", BUDGET_Materials););
AUTO_RUN_ANON(memBudgetAddMapping("rxbx:VertexShaders", BUDGET_Materials););

#if _XBOX
static const int optimization_flags[] = {0, 0, 0, 0, 0};
#else
static const int optimization_flags[] = {D3D10_SHADER_DEBUG | D3D10_SHADER_SKIP_OPTIMIZATION | D3D10_SHADER_PREFER_FLOW_CONTROL, D3D10_SHADER_OPTIMIZATION_LEVEL0, D3D10_SHADER_OPTIMIZATION_LEVEL1, D3D10_SHADER_OPTIMIZATION_LEVEL2, D3D10_SHADER_OPTIMIZATION_LEVEL3};
#endif
static RxbxVertexShader *loadVertexShader(RdrDeviceDX *device, SA_PARAM_NN_STR const char* filename, char *entrypoint, RxbxVertexShader *existing_shader, ShaderHandle handle, bool forceForegroundCompile, const RxbxPreloadedShaderData *preloaded_data);
static RxbxHullShader *loadHullShader(RdrDeviceDX *device, SA_PARAM_NN_STR const char* filename, char *entrypoint, RxbxHullShader *existing_shader, ShaderHandle handle, bool forceForegroundCompile, const RxbxPreloadedShaderData *preloaded_data);
static RxbxDomainShader *loadDomainShader(RdrDeviceDX *device, SA_PARAM_NN_STR const char* filename, char *entrypoint, RxbxDomainShader *existing_shader, ShaderHandle handle, bool forceForegroundCompile, const RxbxPreloadedShaderData *preloaded_data);
static RxbxPixelShader *loadPixelShader(RdrDeviceDX *device, SA_PARAM_NN_STR const char* filename, char *entrypoint, RxbxPixelShader *existing_shader, ShaderHandle handle, bool forceForegroundCompile, const RxbxPreloadedShaderData *preloaded_data);
static void lpcUpdateCachedCrc(RdrDeviceDX *device, int pixel_shader, int precompiled, ShaderHandle programHandle, U32 crc);
static U32 lpcGetCachedCrc(RdrDeviceDX *device, int pixel_shader, int precompiled, ShaderHandle programHandle);
int rxbxCancelPixelShaderCompile(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxPixelShader* pixelShader);
int rxbxCancelVertexShaderCompile(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxVertexShader* vertexShader);

#define DEBUG_TRACE_SHADER 0

#if DEBUG_TRACE_SHADER
#define SHADER_LOG( FMT, ... ) OutputDebugStringf( FMT, __VA_ARGS__ ) //memlog_printf( NULL, __VA_ARGS__ )
#else
#define SHADER_LOG( FMT, ... )
#endif

#if 0
#define SHADER_SERVER_LOG( ... ) printf(__VA_ARGS__)
#else
#define SHADER_SERVER_LOG( ... )
#endif

typedef struct RdrShaderCompileParams
{
	RdrDeviceDX * device;
	char * programText;
	size_t programTextLen;
	const char * shaderModel;
	const char * entryPointName;
	ShaderHandle shader_handle;
	const char *filename;
	char * defines;
	RdrShaderFinishedData *finishedData;
	volatile int referenceCount;
	int compiler_flags;
	bool hasBeenCompiled; // One of either the background thread or the shader server gave us a result
	RdrShaderType shaderType;
	bool is_vertex_shader;
	bool hide_compiler_errors;
	bool cacheResult; // Should cache the results
	bool do_not_create_device_object;
	const char *cacheFilename;
	FileList *cacheFileList; // Only if caching and non-threaded
} RdrShaderCompileParams;

enum
{
	RT_COMPILE = WT_CMD_USER_START,
};

static CRITICAL_SECTION CriticalSectionCompileShader;
static CRITICAL_SECTION CriticalSectionCancelCompile;
static void rxbxBackgroundShaderCompileAPC(WorkerThread *shaderCompileWT, RdrShaderCompileParams **dwParam, WTCmdPacket *packet);
static void __stdcall rxbxBackgroundCompileShader(void *dwData);
static ManagedThread * rxbx_background_shader_compile_ptr = NULL;
static ShaderHandle *cancelQueue=NULL; // Pixel or Vertex
static WorkerThread *shaderCompileWT = NULL;



// #define LOG_D3DX_CALLS
#ifdef LOG_D3DX_CALLS
void d3dxLogCall(void *data, int data_size, FORMAT_STR const char *fmt, ...)
{
	static FILE *d3dx_log;
	static int d3dx_log_index=0;
	va_list va;
	char buf[4096]={0};
	char fn[MAX_PATH];
	FILE *tempfile;

	va_start(va, fmt);
	vsprintf(buf, fmt, va);
	va_end(va);

#ifdef _XBOX
	if (!d3dx_log)
		d3dx_log = fopen("devkit:\\d3dxlog.c", "w");
	sprintf(fn, "devkit:\\d3dxdata%d.txt", d3dx_log_index++);
#else
	if (!d3dx_log)
		d3dx_log = fopen("C:\\d3dxlog.c", "w");
	sprintf(fn, "C:\\d3dxdata%d.txt", d3dx_log_index++);
#endif
	tempfile = fopen(fn, "wb");
	fwrite(data, 1, data_size, tempfile);
	fclose(tempfile);

	forwardSlashes(fn);
	fprintf(d3dx_log, "data = fileAlloc(\"%s\", &data_len);\n%s\nfree(data);\n", fn, buf);
	fflush(d3dx_log);
	
}
#else
#define d3dxLogCall(...)
#endif

#if PLATFORM_CONSOLE
#define updateNvidiaLowerOptimization(x)
#else
static void updateNvidiaLowerOptimization(RdrDeviceDX *device)
{
	if (system_specs.videoCardVendorID == VENDOR_NV && !device->d3d11_device)
	{
		int nvloSetting = (rdr_state.nvidiaLowerOptimization_commandLineOverride!=-1)?rdr_state.nvidiaLowerOptimization_commandLineOverride:rdr_state.nvidiaLowerOptimization;
		if (nvloSetting != rdr_state.nvidiaLowerOptimization_last) {
			rdr_state.nvidiaLowerOptimization_last = nvloSetting;
			CHECKX(IDirect3DDevice9_SetRenderState(device->d3d_device, D3DRS_ADAPTIVETESS_Y, (nvloSetting==1)?MAKEFOURCC( 'C', 'O', 'P', 'M'):(nvloSetting==2)?MAKEFOURCC( 'C', 'O', 'P', 'L'):MAKEFOURCC( 'C', 'O', 'P', 'D')));
		}
	}
}
#endif

static void rxbxAddSpecifiedIntrinsicDefines(const char *defines)
{
	char *defines2;
	char *context=NULL;
	char *s;
	strdup_alloca(defines2, defines);
	while (s=strtok_s(defines2, " ", &context)) {
		defines2 = NULL;
		genericPreProcAddDefine(s);
	}
}


static void rxbxAddIntrinsicDefines(RdrDeviceDX *device)
{
	rxbxAddSpecifiedIntrinsicDefines(rxbxGetIntrinsicDefines(&device->device_base));
}

void rxbxAddIntrinsicDefinesForXbox(void) // To get at Xbox defines when running on PC
{
	rxbxAddIntrinsicDefines((RdrDeviceDX*)DEVICE_XBOX);
}

void rxbxAddIntrinsicDefinesForPS3(void) // To get at PS3 defines when running on PC
{
	rxbxAddIntrinsicDefines((RdrDeviceDX*)DEVICE_PS3);			// Using RdrDeviceDX for now
}

static int rxbxReleasePixelShader(RdrDeviceDX *device, RdrPixelShaderObj pPixelShader)
{
	int ref_count;
	if (device->d3d11_device)
		ref_count = ID3D11PixelShader_Release(pPixelShader.pixel_shader_d3d11);
	else
		ref_count = IDirect3DPixelShader9_Release(pPixelShader.pixel_shader_d3d9);
	if (ref_count == 0)
	{
		bool bFoundOne=false;
		// Remove from cache
		// If we are ever freeing pixel shaders at run time, we could make this more efficient by having a reverse lookup table
		FOR_EACH_IN_STASHTABLE2(device->stPixelShaderCache, elem)
		{
			if (stashElementGetPointer(elem) == pPixelShader.typeless)
			{
				RdrPixelShaderObj test;
				assert(!bFoundOne);
				bFoundOne = true;
				stashIntRemovePointer(device->stPixelShaderCache, stashElementGetIntKey(elem), &test.typeless);
				assert(test.typeless == pPixelShader.typeless);
			}
		}
		FOR_EACH_END;
		assert(bFoundOne);
	}
	rxbxLogReleasePixelShader(device, pPixelShader, ref_count);
	return ref_count;
}

void rxbxFreePixelShaderInternal(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxPixelShader *pixel_shader, bool bCancelIfNotComplete)
{
	int bCancel=false;
	RxbxPixelShader *old_pixel_shader;

	verify(stashIntRemovePointer(device->pixel_shaders, shader_handle, &old_pixel_shader));
	assert(old_pixel_shader == pixel_shader);
	if (bCancelIfNotComplete)
		bCancel = rxbxCancelPixelShaderCompile(device, shader_handle, pixel_shader);
	if (device->device_state.active_pixel_shader_wrapper == pixel_shader)
		device->device_state.active_pixel_shader_wrapper = NULL;
	if (pixel_shader->shader.typeless && !pixel_shader->is_error_shader)
		rxbxReleasePixelShader(device, pixel_shader->shader);
	if ( !bCancel )
	{
#if _XBOX
		SAFE_FREE(pixel_shader->microcode_text);
		eaDestroyEx(&pixel_shader->microcode_jumps, NULL);
		FOR_EACH_IN_STASHTABLE(pixel_shader->shader_variations, IDirect3DPixelShader9, d3d_pshader)
		{
			rxbxReleasePixelShader(device, d3d_pshader);
		}
		FOR_EACH_END;
		stashTableDestroy(pixel_shader->shader_variations);
		pixel_shader->shader_variations = NULL;
#endif
		free(pixel_shader);
	}
}

void rxbxFreePixelShader(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxPixelShader *pixel_shader)
{
	rxbxFreePixelShaderInternal(device, shader_handle, pixel_shader, true);
}


RxbxPixelShader *rxbxAllocPixelShader(void)
{
	RxbxPixelShader * shader;
	shader = callocStruct(RxbxPixelShader);
	return shader;
}

static int rxbxReleaseVertexShader(RdrDeviceDX *device, RdrVertexShaderObj pVertexShader)
{
	int ref_count;
	if (device->d3d11_device)
		ref_count = ID3D11VertexShader_Release(pVertexShader.vertex_shader_d3d11);
	else
		ref_count = IDirect3DVertexShader9_Release(pVertexShader.vertex_shader_d3d9);
	rxbxLogReleaseVertexShader(device, pVertexShader, ref_count);
	return ref_count;
}

void rxbxFreeVertexShaderInternal(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxVertexShader *vertex_shader, bool bCancelIfNotComplete)
{
	int bCancel=false;
	RxbxVertexShader *stored_vshader;

	assert(vertex_shader);

	verify(stashIntRemovePointer(device->vertex_shaders, shader_handle, &stored_vshader));
	assert(stored_vshader == vertex_shader);
	if (bCancelIfNotComplete)
		bCancel = rxbxCancelVertexShaderCompile(device, shader_handle, vertex_shader);
	if (device->device_state.active_vertex_shader_wrapper == vertex_shader)
		device->device_state.active_vertex_shader_wrapper = NULL;
	if (vertex_shader->shader.typeless && !vertex_shader->is_error_shader)
		rxbxReleaseVertexShader(device, vertex_shader->shader);
	if ( !bCancel )
		free(vertex_shader);
	
}

void rxbxFreeVertexShader(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxVertexShader *vertex_shader)
{
	rxbxFreeVertexShaderInternal(device, shader_handle, vertex_shader, true);
}


RxbxVertexShader *rxbxAllocVertexShader(RdrDeviceDX *device, ShaderHandle shader_handle, const char *debug_name)
{
	RxbxVertexShader * shader;
	CHECKTHREAD;
	if (!device->vertex_shaders)
		device->vertex_shaders = stashTableCreateInt(1024);
	shader = callocStruct(RxbxVertexShader);
	shader->debug_name = debug_name;
	verify(stashIntAddPointer(device->vertex_shaders, shader_handle, shader, false));
	return shader;
}

//--------------------------------------------------------------------------------------
// Hull and Domain Shader

static int rxbxReleaseHullShader(RdrDeviceDX *device, ID3D11HullShader *pHullShader)
{
	int ref_count;
	ref_count = ID3D11HullShader_Release(pHullShader);
	//	TODO: Log the release of the shader.
	return ref_count;
}

int rxbxCancelHullShaderCompile(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxHullShader* hullShader)
{
	return rxbxCancelPixelShaderCompile(device, shader_handle, (RxbxPixelShader*)hullShader);
}

void rxbxFreeHullShaderInternal(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxHullShader *hull_shader, bool bCancelIfNotComplete)
{
	int bCancel=false;
	RxbxHullShader *stored_hshader;

	assert(hull_shader);

	verify(stashIntRemovePointer(device->hull_shaders, shader_handle, &stored_hshader));
	assert(stored_hshader == hull_shader);
	if (bCancelIfNotComplete)
		bCancel = rxbxCancelHullShaderCompile(device, shader_handle, hull_shader);
	if (device->device_state.active_hull_shader_wrapper == hull_shader)
		device->device_state.active_hull_shader_wrapper = NULL;
	if (hull_shader->shader && !hull_shader->is_error_shader)
		rxbxReleaseHullShader(device, hull_shader->shader);
	if ( !bCancel )
		free(hull_shader);

}

void rxbxFreeHullShader(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxHullShader *hull_shader)
{
	rxbxFreeHullShaderInternal(device, shader_handle, hull_shader, true);
}


RxbxHullShader *rxbxAllocHullShader(RdrDeviceDX *device, ShaderHandle shader_handle, const char *debug_name)
{
	RxbxHullShader * shader;
	CHECKTHREAD;
	if (!device->hull_shaders)
		device->hull_shaders = stashTableCreateInt(1024);
	shader = callocStruct(RxbxHullShader);
	shader->debug_name = debug_name;
	verify(stashIntAddPointer(device->hull_shaders, shader_handle, shader, false));
	return shader;
}

ID3D11HullShader* rxbxCreateD3DHullShader(RdrDeviceDX *device, RxbxHullShader *hshader, const char *filename, void **p_compiled_data, int compiled_data_size, ShaderHandle shader_handle, U32 new_crc)
{
	void *compiled_data = *p_compiled_data;
	HRESULT hr;
	ID3D11HullShader* shader = NULL;

	updateNvidiaLowerOptimization(device);

	if (hshader->debug_name != filename)
		hshader->debug_name = allocAddFilename(filename);

	XMemAllocPersistantPhysical(true);
	XMemAllocSetBlamee("rxbx:HullShaders");

	PERFINFO_AUTO_START("ID3D11Device_CreateHullShader", 1);
	if (FAILED(hr = ID3D11Device_CreateHullShader(device->d3d11_device,
		(DWORD*)compiled_data,
		compiled_data_size,
		NULL,
		&shader )))
	{
		PERFINFO_AUTO_STOP();
		rdrStatusPrintf("Hull shader creation FAILED - %s", rxbxGetStringForHResult(hr));
		assert(!shader);
	}
	PERFINFO_AUTO_STOP();

	XMemAllocSetBlamee(NULL);
	XMemAllocPersistantPhysical(false);

	if (!new_crc)
		new_crc = cryptAdler32(compiled_data, compiled_data_size);
	rdrShaderEnterCriticalSection();
	lpcUpdateCachedCrc(device, false, true, shader_handle, new_crc);
	rdrShaderLeaveCriticalSection();

	return shader;
}

int rxbxLoadHullShaders(RdrDeviceDX *device, RxbxProgramDef *defs, ShaderHandle *shaders, int count, const RxbxPreloadedShaderData *preloaded_data, int preloaded_data_count)
{
	int i;
	int loaded_count=0;

	PERFINFO_AUTO_START_FUNC();

	assert(!preloaded_data_count || count<=preloaded_data_count); // All or nothing

	rdrShaderEnterCriticalSection();
	rdrShaderResetCache(true);

	// not with renderthread on! loadstart_printf("Loading hull shaders...");

	PERFINFO_AUTO_START("Load Hull Shaders", 1);

	// Load the hull programs
	for (i = 0; i < count; i++)
	{
		RxbxHullShader *shader = NULL;
		int j = 0;
		const RxbxPreloadedShaderData *const ppd = (i<preloaded_data_count)? &preloaded_data[i]:NULL;

		if (defs[i].skip_me)
			continue;
		if (!shaders[i])
			shaders[i] = rdrGenShaderHandle();

		if(!ppd) {
			genericPreProcReset();
			for (j = 0; defs[i].defines[j] && defs[i].defines[j][0]; j++)
				genericPreProcAddDefine(defs[i].defines[j]);
			rxbxAddIntrinsicDefines(device);
		}

		stashIntFindPointer(device->hull_shaders, shaders[i], &shader);
		shader = loadHullShader(device, defs[i].filename, defs[i].entry_funcname, shader, shaders[i], true, ppd);
		if (strStartsWith(defs[i].entry_funcname, "error"))
		{
			assert(i==0);
			shader->is_error_shader = 1;
			device->error_hull_shader = shader;
		}
		if (shader->shader)// && shader->shader)
			loaded_count++;
	}
	PERFINFO_AUTO_STOP();

	// not with renderthread on! loadend_printf("done (%d shaders).", loaded_count);

	rdrShaderResetCache(false);
	rdrShaderLeaveCriticalSection();

	PERFINFO_AUTO_STOP();

	return loaded_count == count;
}

static int rxbxReleaseDomainShader(RdrDeviceDX *device, ID3D11DomainShader *pDomainShader)
{
	int ref_count;
	ref_count = ID3D11DomainShader_Release(pDomainShader);
	//	TODO: Log the release of the shader.
	return ref_count;
}

int rxbxCancelDomainShaderCompile(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxDomainShader* domainShader)
{
	return rxbxCancelPixelShaderCompile(device, shader_handle, (RxbxPixelShader*)domainShader);
}

void rxbxFreeDomainShaderInternal(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxDomainShader *domain_shader, bool bCancelIfNotComplete)
{
	int bCancel=false;
	RxbxDomainShader *stored_dshader;

	assert(domain_shader);

	verify(stashIntRemovePointer(device->domain_shaders, shader_handle, &stored_dshader));
	assert(stored_dshader == domain_shader);
	if (bCancelIfNotComplete)
		bCancel = rxbxCancelDomainShaderCompile(device, shader_handle, domain_shader);
	if (device->device_state.active_domain_shader_wrapper == domain_shader)
		device->device_state.active_domain_shader_wrapper = NULL;
	if (domain_shader->shader && !domain_shader->is_error_shader)
		rxbxReleaseDomainShader(device, domain_shader->shader);
	if ( !bCancel )
		free(domain_shader);

}

void rxbxFreeDomainShader(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxDomainShader *domain_shader)
{
	rxbxFreeDomainShaderInternal(device, shader_handle, domain_shader, true);
}


RxbxDomainShader *rxbxAllocDomainShader(RdrDeviceDX *device, ShaderHandle shader_handle, const char *debug_name)
{
	RxbxDomainShader * shader;
	CHECKTHREAD;
	if (!device->domain_shaders)
		device->domain_shaders = stashTableCreateInt(1024);
	shader = callocStruct(RxbxDomainShader);
	shader->debug_name = debug_name;
	verify(stashIntAddPointer(device->domain_shaders, shader_handle, shader, false));
	return shader;
}

ID3D11DomainShader* rxbxCreateD3DDomainShader(RdrDeviceDX *device, RxbxDomainShader *dshader, const char *filename, void **p_compiled_data, int compiled_data_size, ShaderHandle shader_handle, U32 new_crc)
{
	void *compiled_data = *p_compiled_data;
	HRESULT hr;
	ID3D11DomainShader* shader = NULL;

	updateNvidiaLowerOptimization(device);

	if (dshader->debug_name != filename)
		dshader->debug_name = allocAddFilename(filename);

	XMemAllocPersistantPhysical(true);
	XMemAllocSetBlamee("rxbx:DomainShaders");

	PERFINFO_AUTO_START("ID3D11Device_CreateDomainShader", 1);
	if (FAILED(hr = ID3D11Device_CreateDomainShader(device->d3d11_device,
		(DWORD*)compiled_data,
		compiled_data_size,
		NULL,
		&shader )))
	{
		PERFINFO_AUTO_STOP();
		rdrStatusPrintf("Domain shader creation FAILED - %s", rxbxGetStringForHResult(hr));
		assert(!shader);
	}
	PERFINFO_AUTO_STOP();

	XMemAllocSetBlamee(NULL);
	XMemAllocPersistantPhysical(false);

	if (!new_crc)
		new_crc = cryptAdler32(compiled_data, compiled_data_size);
	rdrShaderEnterCriticalSection();
	lpcUpdateCachedCrc(device, false, true, shader_handle, new_crc);
	rdrShaderLeaveCriticalSection();

	return shader;
}

int rxbxLoadDomainShaders(RdrDeviceDX *device, RxbxProgramDef *defs, ShaderHandle *shaders, int count, const RxbxPreloadedShaderData *preloaded_data, int preloaded_data_count)
{
	int i;
	int loaded_count=0;

	PERFINFO_AUTO_START_FUNC();

	assert(!preloaded_data_count || count<=preloaded_data_count); // All or nothing

	rdrShaderEnterCriticalSection();
	rdrShaderResetCache(true);

	// not with renderthread on! loadstart_printf("Loading hull shaders...");

	PERFINFO_AUTO_START("Load Domain Shaders", 1);

	// Load the hull programs
	for (i = 0; i < count; i++)
	{
		RxbxDomainShader *shader = NULL;
		int j = 0;
		const RxbxPreloadedShaderData *const ppd = (i<preloaded_data_count)? &preloaded_data[i]:NULL;

		if (defs[i].skip_me)
			continue;
		if (!shaders[i])
			shaders[i] = rdrGenShaderHandle();

		if(!ppd) {
			genericPreProcReset();
			for (j = 0; defs[i].defines[j] && defs[i].defines[j][0]; j++)
				genericPreProcAddDefine(defs[i].defines[j]);
			rxbxAddIntrinsicDefines(device);
		}

		stashIntFindPointer(device->domain_shaders, shaders[i], &shader);
		shader = loadDomainShader(device, defs[i].filename, defs[i].entry_funcname, shader, shaders[i], true, ppd);
		if (strStartsWith(defs[i].entry_funcname, "error"))
		{
			assert(i==0);
			shader->is_error_shader = 1;
			device->error_domain_shader = shader;
		}
		if (shader->shader)// && shader->shader)
			loaded_count++;
	}
	PERFINFO_AUTO_STOP();

	// not with renderthread on! loadend_printf("done (%d shaders).", loaded_count);

	rdrShaderResetCache(false);
	rdrShaderLeaveCriticalSection();

	PERFINFO_AUTO_STOP();

	return loaded_count == count;
}

//-------------------------------------------------------------------------------------------

__forceinline static HRESULT rxbxCreateVertexShader(RdrDeviceDX *device, CONST DWORD *pdata, DWORD data_size, RdrVertexShaderObj *ppVertexShader)
{
	HRESULT hr;
	if (device->d3d11_device)
		hr = ID3D11Device_CreateVertexShader(device->d3d11_device, pdata, data_size, NULL, &ppVertexShader->vertex_shader_d3d11);
	else
		hr = IDirect3DDevice9_CreateVertexShader(device->d3d_device, pdata, &ppVertexShader->vertex_shader_d3d9);
	if (!FAILED(hr))
		rxbxLogCreateVertexShader(device, *ppVertexShader);
	return hr;
}

RdrVertexShaderObj rxbxCreateD3DVertexShader(RdrDeviceDX *device, RxbxVertexShader *vshader, const char *filename, void **p_compiled_data, int compiled_data_size, ShaderHandle shader_handle, U32 new_crc)
{
	void *compiled_data = *p_compiled_data;
	HRESULT hr;
	RdrVertexShaderObj shader = {NULL};

	updateNvidiaLowerOptimization(device);

    if (vshader->debug_name != filename)
		vshader->debug_name = allocAddFilename(filename);

	//OutputDebugStringf("Shader %p %s %p\n", vshader, vshader->debug_name, compiled_data);

	XMemAllocPersistantPhysical(true);
	XMemAllocSetBlamee("rxbx:VertexShaders");

	PERFINFO_AUTO_START("IDirect3DDevice9_CreateVertexShader", 1);
	if (FAILED(hr = rxbxCreateVertexShader(device, (DWORD*)compiled_data, compiled_data_size, &shader)))
	{
		PERFINFO_AUTO_STOP();
		// IMPLEMENT support storing this data
		//OutputDebugStringf("\r\nVertex shader creation failure: %s\r\n", filename);
		//OutputDebugStringf("Defines: %s\r\n", rdrGetDefinesString());
		rdrStatusPrintfFromDeviceThread(&device->device_base, "Vertex shader creation FAILED - %s", rxbxGetStringForHResult(hr));
		assert(!shader.typeless);
	} else {
		if (device->d3d11_device)
		{
			int i;
			ID3D10Blob *input_signature;
			U32 crc;
			int index=-1;
			int sig_size;
			void *sig_data;
			hr = D3DGetInputSignatureBlob(compiled_data, compiled_data_size, &input_signature);
			rxbxFatalHResultErrorf(device, hr, "D3DGetInputSignatureBlob", "");
			sig_size = input_signature->lpVtbl->GetBufferSize(input_signature);
			sig_data = input_signature->lpVtbl->GetBufferPointer(input_signature);
			crc = cryptAdler32(sig_data, sig_size);
			// Start at index 1, 0 is skipped below
			for (i=1; i<beaSize(&device->input_signatures); i++)
			{
				if (crc == device->input_signatures[i].crc)
				{
					// Check for hash collisions
					assert(sig_size == device->input_signatures[i].data_size);
					assert(memcmp(sig_data, device->input_signatures[i].data, sig_size)==0);
					// Use this pooled index
					index = i;
				}
			}
			if (index == -1)
			{
				RxbxInputSignature *sig = beaPushEmpty(&device->input_signatures);
				if (beaSize(&device->input_signatures)==1) // Skip index 0
					sig = beaPushEmpty(&device->input_signatures);
				sig->crc = crc;
				sig->data = memdup(sig_data, sig_size);
				sig->data_size = sig_size;
				index = beaSize(&device->input_signatures) - 1;
			}
			vshader->input_signature_index = index;
			// Free D3D blob
			input_signature->lpVtbl->Release(input_signature);

			/*  Not needed - just hanging onto input signature instead
			// Take ownership of compiled data
			vshader->compiled_data = compiled_data;
			vshader->compiled_data_size = compiled_data_size;
			*p_compiled_data = NULL;
			*/
		}
	}
	PERFINFO_AUTO_STOP();

	XMemAllocSetBlamee(NULL);
	XMemAllocPersistantPhysical(false);

	if (!new_crc)
		new_crc = cryptAdler32(compiled_data, compiled_data_size);
	rdrShaderEnterCriticalSection();
	lpcUpdateCachedCrc(device, false, true, shader_handle, new_crc);
	rdrShaderLeaveCriticalSection();

	return shader;
}

int rxbxLoadVertexShaders(RdrDeviceDX *device, RxbxProgramDef *defs, ShaderHandle *shaders, int count, const RxbxPreloadedShaderData *preloaded_data, int preloaded_data_count)
{
	int i, j;
	int loaded_count=0;

	PERFINFO_AUTO_START_FUNC();

	assert(!preloaded_data_count || count<=preloaded_data_count); // All or nothing

	rdrShaderEnterCriticalSection();
	rdrShaderResetCache(true);

	// not with renderthread on! loadstart_printf("Loading vertex shaders...");

	PERFINFO_AUTO_START("Load Shaders", 1);

	// Load the vertex programs
	for (i = 0; i < count; i++)
	{
		RxbxVertexShader *shader = NULL;
        const RxbxPreloadedShaderData *const ppd = (i<preloaded_data_count)? &preloaded_data[i]:NULL;

		if (defs[i].skip_me)
			continue;
		if (!shaders[i])
			shaders[i] = rdrGenShaderHandle();

        if(!ppd) {
            genericPreProcReset();
		    for (j = 0; defs[i].defines[j] && defs[i].defines[j][0]; j++)
			    genericPreProcAddDefine(defs[i].defines[j]);
		    rxbxAddIntrinsicDefines(device);
        }

        stashIntFindPointer(device->vertex_shaders, shaders[i], &shader);
		shader = loadVertexShader(device, defs[i].filename, defs[i].entry_funcname, shader, shaders[i], true, ppd);
		if (strStartsWith(defs[i].entry_funcname, "error"))
		{
			assert(i==0);
			shader->is_error_shader = 1;
			device->error_vertex_shader = shader;
		}
		if (shader && shader->shader.typeless)
			loaded_count++;
	}
	PERFINFO_AUTO_STOP();

	// not with renderthread on! loadend_printf("done (%d shaders).", loaded_count);

	rdrShaderResetCache(false);
	rdrShaderLeaveCriticalSection();

	PERFINFO_AUTO_STOP();

	return loaded_count == count;
}

void rxbxLoadPixelShaders(RdrDeviceDX *device, RxbxProgramDef *defs, ShaderHandle *shaders, int count, const RxbxPreloadedShaderData *preloaded_data, int preloaded_data_count)
{
	int i, j;

	PERFINFO_AUTO_START("rxbxLoadPixelShaders", 1);

	assert(!preloaded_data || count <= preloaded_data_count); // All or nothing

	if (!device->pixel_shaders)
		device->pixel_shaders = stashTableCreateInt(1024);

	rdrShaderEnterCriticalSection();
	rdrShaderResetCache(true);

	PERFINFO_AUTO_START("rdrGenShaderHandle", 1);

	// Generate the program objects
	for (i = 0; i < count; i++)
	{
		if (!shaders[i])
		{
			if (defs[i].skip_me)
				shaders[i] = 0;
			else if (defs[i].is_nullshader)
				shaders[i] = -1;
			else
				shaders[i] = rdrGenShaderHandle();
		}
	}

	PERFINFO_AUTO_STOP_START("Load Shaders", 1);

	// Load the pixel shaders
	for (i = 0; i < count; i++)
	{
		RxbxPixelShader *pixel_shader=NULL;
		const RxbxPreloadedShaderData *psd = (i < preloaded_data_count && preloaded_data)?&preloaded_data[i]:NULL;
		if (!shaders[i])
			continue;
		if (!psd && defs[i].is_minimal)
			continue; // Don't touch minimal shaders not being loaded from preloaded data
		if (defs[i].dx11_only && !device->d3d11_device)
			continue; // Don't compile DX11 shaders on DX9

        genericPreProcReset();
		for (j = 0; defs[i].defines[j] && defs[i].defines[j][0]; j++)
			genericPreProcAddDefine(defs[i].defines[j]);
		rxbxAddIntrinsicDefines(device);

        stashIntFindPointer(device->pixel_shaders, shaders[i], &pixel_shader);
		pixel_shader = loadPixelShader(device, defs[i].filename, defs[i].entry_funcname, pixel_shader, shaders[i], true, psd);
		if (strStartsWith(defs[i].entry_funcname, "error"))
		{
			errorIsDuringDataLoadingInc(defs[i].filename);
			assert(i==0);
			assert(pixel_shader && pixel_shader->shader.typeless);
			errorIsDuringDataLoadingDec();
			pixel_shader->is_error_shader = 1;
			device->error_pixel_shader = pixel_shader;
		}
	}

	PERFINFO_AUTO_STOP();

	rdrShaderResetCache(false);
	rdrShaderLeaveCriticalSection();

	PERFINFO_AUTO_STOP();
}

static const char *getStringLine(char *text, int linenum)
{
	static char staticline[2048];
	char *line = text, *s;
	int curlinenum = 1;

	while (curlinenum < linenum && (line = strchr(line, '\n')))
	{
		++curlinenum;
		++line;
	}

	if (!line || !line[0])
		return NULL;

	if (s = strchr(line, '\n'))
	{
		*s = 0;
		strcpy(staticline, line);
		*s = '\n';
	}
	else
	{
		strcpy(staticline, line);
	}

	return staticline;
}

static void printErrorMessages(const char *errormsgs, char *text, const char *debug_fn)
{
	if (errormsgs)
	{
		int linenum, i = 1;
		char *errorstr;
		const char *errorline;
		strdup_alloca(errorstr, errormsgs);
		while (errorline = getStringLine(errorstr, i))
		{
			i++;
			linenum = atoi(errorline+1);
			if (!linenum) {
				char *s = strchr(errorline, '(');
				if (s)
				{
					linenum = atoi(s+1);
					if (debug_fn) // prefix output with debug filename so you can double-click it!
					{
						s = strchr(s, ')');
						if (s)
						{
							errorline = s+1;
							OutputDebugStringf("%s(%d)", debug_fn, linenum);
						}
					}
				}
			}
			OutputDebugStringf("%s\n", errorline);
			errorline = getStringLine(text, linenum);
			OutputDebugStringf(" %s\n", errorline);
		}
	}
}


int rxbxCancelPixelShaderCompile(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxPixelShader* pixelShader)
{
	int bCancelQueued = 0;
	EnterCriticalSection(&CriticalSectionCancelCompile);
	SHADER_LOG("rxCPSC Thrd 0x%08x QLen %d Shdr 0x%p ShdrHndl %d\n", GetCurrentThreadId(), 
		eaiSize(&cancelQueue), pixelShader, shader_handle);
	if (!pixelShader->shader.typeless)
	{
		int i;
		bool bHadResult=false;
		// Check if it's in the results queue
		for (i=eaSize(&device->compiledShaderQueue)-1; i>=0; i--) 
		{
			if (device->compiledShaderQueue[i]->shader_handle == shader_handle)
			{
				RxbxCompiledShader *result = eaRemoveFast(&device->compiledShaderQueue, i);
				SHADER_LOG("rxCPSC freeing from result queue\n");
				SAFE_FREE(result->compiledResult);
				SAFE_FREE(result);
				bHadResult = true;
				break;
			}
		}
		if (!bHadResult)
		{
			SHADER_LOG("rxCPSC queueing\n");
			eaiPush(&cancelQueue, shader_handle);
		}
	}
	LeaveCriticalSection(&CriticalSectionCancelCompile);
	return bCancelQueued;
}

int rxbxCancelVertexShaderCompile(RdrDeviceDX *device, ShaderHandle shader_handle, RxbxVertexShader* vertexShader)
{
	return rxbxCancelPixelShaderCompile(device, shader_handle, (RxbxPixelShader*)vertexShader);
}

static int rxbxCheckShaderCompileCancelQueue(ShaderHandle shader_handle)
{
	int bFound = 0;

	EnterCriticalSection(&CriticalSectionCancelCompile);
	SHADER_LOG("rxCSCCQ Thrd 0x%08x QLen %d ShdrHndl %d\n", GetCurrentThreadId(), 
		eaiSize(&cancelQueue), shader_handle );
	bFound = eaiFindAndRemove(&cancelQueue, shader_handle) != -1;
	LeaveCriticalSection(&CriticalSectionCancelCompile);

	return bFound;
}

/*DirectX background shader compile: All I do is sleep, waiting to be given work by QueueUserAPC*/
static DWORD WINAPI rxbxBackgroundShaderCompileThread( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN
		PERFINFO_AUTO_START("rxbxBackgroundShaderCompileThread", 1);
			for(;;)
			{
				SleepEx(INFINITE, TRUE);
			}
		PERFINFO_AUTO_STOP();
	return 0; 
	EXCEPTION_HANDLER_END
}

void rxbxInitBackgroundShaderCompile(void)
{
	if (!shaderCompileWT) {
		InitializeCriticalSection(&CriticalSectionCompileShader);
		InitializeCriticalSection(&CriticalSectionCancelCompile);

		shaderCompileWT = wtCreate(512, 512, NULL, "rtCompileThread");
		wtRegisterCmdDispatch(shaderCompileWT, RT_COMPILE, rxbxBackgroundShaderCompileAPC);
		wtSetProcessor(shaderCompileWT, THREADINDEX_MISC);
		wtSetThreaded(shaderCompileWT, true, 0, false);
		wtStart(shaderCompileWT);

		rxbx_background_shader_compile_ptr = tmCreateThread(rxbxBackgroundShaderCompileThread, NULL);
		assert(rxbx_background_shader_compile_ptr);
		tmSetThreadProcessorIdx(rxbx_background_shader_compile_ptr, THREADINDEX_DATASTREAMING);
	}
}

static void rxbxReturnShaderResultMainThread(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	RdrShaderFinishedData *finishedData = (RdrShaderFinishedData *)userData;
	finishedData->finishedCallback(finishedData);
}

static bool rxbxReturnShaderResultInternal(RdrShaderCompileParams *params, void *compiledResult, int compiledResultSize, void *updb_data, int updb_data_size, const char *updb_filename)
{
	bool bSuccess=false;
	bool bCanceled=false;
	assert(params->device);
	EnterCriticalSection(&CriticalSectionCancelCompile);

	if (params->hasBeenCompiled) {
		bSuccess = false;
		bCanceled = true;
	} else if (compiledResult)
	{
		if (!params->shader_handle || !rxbxCheckShaderCompileCancelQueue(params->shader_handle)) {
			bSuccess = true;
			bCanceled = false;
		} else {
			bSuccess = false;
			bCanceled = true;
		}
	} else {
		bSuccess = false;
		bCanceled = false;
	}
	params->hasBeenCompiled = true;

	if (params->finishedData) {
		if (bSuccess) {
			params->finishedData->shader_data_size = compiledResultSize;
			params->finishedData->shader_data = malloc(params->finishedData->shader_data_size);
			memcpy_s(params->finishedData->shader_data, params->finishedData->shader_data_size, compiledResult, params->finishedData->shader_data_size);
			if (updb_data && updb_filename) {
				params->finishedData->updb_filename = strdup(updb_filename);
				params->finishedData->updb_data_size = updb_data_size;
				params->finishedData->updb_data = malloc(params->finishedData->updb_data_size);
				memcpy_s(params->finishedData->updb_data, params->finishedData->updb_data_size, updb_data, params->finishedData->updb_data_size);
			}
			params->finishedData->compilationFailed = false;
		} else {
			params->finishedData->compilationFailed = true;
		}
		TimedCallback_Run(rxbxReturnShaderResultMainThread, params->finishedData, 0);
		params->finishedData = NULL;
	}

	if ( !bCanceled && params->shader_handle )
	{
		RxbxCompiledShader *result;
		result = callocStruct(RxbxCompiledShader);
		result->is_vertex_shader = params->is_vertex_shader;
		result->shaderType = params->shaderType;
		result->shader_handle = params->shader_handle;
		result->compiledResultSize = compiledResultSize;
		result->do_not_create_device_object = params->do_not_create_device_object;
		if (compiledResultSize)
		{
			result->compiledResult = malloc(compiledResultSize);
			memcpy(result->compiledResult, compiledResult, compiledResultSize);
		}
		eaPush(&params->device->compiledShaderQueue, result);
	}
	if (bSuccess)
	{
		// Also save a cache of the compiled data
		if (params->cacheResult)
		{
			rdrSavePrecompiledShader(params->cacheFilename, params->entryPointName, compiledResult,
				compiledResultSize, params->cacheFileList, rdrGetDeviceIdentifier(&(params->device->device_base)));
		}
	}
	LeaveCriticalSection(&CriticalSectionCancelCompile);
	return bSuccess;
}

static void rxbxReturnShaderResult(RdrShaderCompileParams * params, LPD3D10BLOB compiledResult, LPD3D10BLOB updb_data, const char *updb_filename)
{
	rxbxReturnShaderResultInternal(params,
		compiledResult?compiledResult->lpVtbl->GetBufferPointer(compiledResult):NULL, compiledResult?compiledResult->lpVtbl->GetBufferSize(compiledResult):0,
		updb_data?updb_data->lpVtbl->GetBufferPointer(compiledResult):NULL, updb_data?updb_data->lpVtbl->GetBufferSize(compiledResult):0,
		updb_filename);

	if (compiledResult)
		compiledResult->lpVtbl->Release(compiledResult);
}

static HRESULT debug_hr;

static void rxbxGetShaderStatsSub(DWORD *compiledData, int compiledData_size, const char *filename, int *instruction_count, int *texture_fetch_count, int *temporaries_count,
								  int *d3d_instruction_slots, int *d3d_alu_instruction_slots, char *path_out, int path_out_size)
{
	LPD3D10BLOB buffer = NULL;
	char *assembly;
	char *args[16];
	int count;
	char *next;
	d3dxLogCall(compiledData, compiledData_size, "D3DDisassemble((DWORD*)data, data_size, 0, NULL, &buffer);");
	debug_hr = D3DDisassemble(compiledData, compiledData_size, 0, NULL, &buffer);
	if (!buffer) {
		assertmsg(0, "Shader disassembly failed");
	} else {
		assembly = buffer->lpVtbl->GetBufferPointer(buffer);
		next = assembly;

		if (filename && (rdr_state.writeCompiledShaders || rdr_state.runNvPerfShader))
		{
			FILE *f;

			char path[MAX_PATH]={0};
			if (strStartsWith(filename, "shaders/"))
			{
				sprintf(path, "shaders_processed/%s", filename + strlen("shaders/"));
				fileLocateWrite(path, path);
			} else if (strStartsWith(filename, "shaders_processed")) {
				strcpy(path, filename);
				fileLocateWrite(path, path);
			} else {
				sprintf(path, "shaders_processed/%s", filename);
				fileLocateWrite(path, path);
			}
			if (strEndsWith(filename, ".vhl")) {
				changeFileExt(path, ".asm.vhl", path);
			} else if (strEndsWith(filename, ".phl")) {
				changeFileExt(path, ".asm.phl", path);
			} else {
				strcat(path, ".asm.phl");
			}

			mkdirtree(path);
			f = fopen(path, "w");
			if (!f) {
				printf("Error opening %s for writing.\n", path);
			} else {
				fwrite(assembly, 1, strlen(assembly), f);
				fclose(f);
			}
			if (path_out)
				strcpy_s(SAFESTR2(path_out), path);
		}
	}

	//OutputDebugStringf("%s\n", assembly);
	*instruction_count=0;
	*texture_fetch_count=0;
	*temporaries_count=0;
	do {		
		count = tokenize_line(next, args, &next);
		if (!count)
			continue;

		// detect footer
		if (  count >= 6
			  && stricmp(args[0], "//") == 0
			  && stricmp(args[1], "approximately") == 0
			  && stricmp(args[3], "instruction") == 0
			  && stricmp(args[4], "slots") == 0
			  && stricmp(args[5], "used") == 0) {
			int slots = atoi(args[2]);
			int alu_slots = (count > 8 ? atoi(args[8]) : 0);
			if( alu_slots == 0 ) {
				alu_slots = slots;
			}

			// The slot count reported by D3D seems to be up to 5%
			// off.
			*d3d_instruction_slots = MAX((slots * 105) / 100, slots + 1);
			*d3d_alu_instruction_slots = MAX((alu_slots * 105) / 100, alu_slots + 1);
		}
		
		if (strStartsWith(args[0], "//") || args[0][0]==';' || args[0][0]=='#')
			continue;
		if (strStartsWith(args[0], "ps_") || strStartsWith(args[0], "dcl") ||
			strStartsWith(args[0], "xps_") || strStartsWith(args[0], "def") ||
			strStartsWith(args[0], "config"))
			continue;
		if (args[0][0]=='+') // CD: simultaneous instruction, don't count it towards the instruction count
			continue;
		if (count>=2) {
			if (args[1][0]=='r') {
				int ri=0; sscanf(args[1], "r%d", &ri);
				MAX1(*temporaries_count, ri+1);
			}
		}
		if (strStartsWith(args[0], "tex") && !strStartsWith(args[0], "texkill") ||
			strStartsWith(args[0], "tfetch"))
		{
			(*instruction_count)++;
			(*texture_fetch_count)++;
			continue;
		}
		(*instruction_count)++;
	} while (next);

	buffer->lpVtbl->Release(buffer);
}

static void rxbxGetPShaderStats(DWORD *compiledData, int compiledData_size, RxbxPixelShader * pixel_shader, const char *filename)
{
	assert(filename);
	if (pixel_shader && compiledData ) {
		char path[MAX_PATH] = {0};
		rxbxGetShaderStatsSub(compiledData, compiledData_size, filename, &pixel_shader->instruction_count, &pixel_shader->texture_fetch_count, &pixel_shader->temporaries_count, &pixel_shader->d3d_instruction_slots, &pixel_shader->d3d_alu_instruction_slots, SAFESTR(path));

#if !PLATFORM_CONSOLE
		if (rdr_state.runNvPerfShader && fileExists(path)) {
			char cmdline[512] = { 0 };
			char tempfile[MAX_PATH];
			char *data;
			char *pFullCmdLine = NULL;
			RegReader rr = createRegReader();
			intptr_t ret;
			initRegReader(rr, "HKEY_CURRENT_USER\\SOFTWARE\\NVIDIA Corporation\\NVShaderPerf");
			rrReadString(rr, "InstallPath", SAFESTR(cmdline));
			destroyRegReader(rr);
			strstriReplace(cmdline, "\\Common Files\\", "\\");
			strcat(cmdline, "\\NVShaderPerf.exe");
			sprintf(tempfile, "%s/nvshaderperf.txt", fileTempDir());
			fileForceRemove(tempfile);
			backSlashes(tempfile);
			backSlashes(path);
			

			estrPrintf(&pFullCmdLine, "%s -g NV43-GT -o %s %s", cmdline, tempfile, path);

			ret = system(pFullCmdLine);

			if (ret == -1) {
				ErrorDeferredf("Error running NVShaderPerf, perhaps it's not installed?");
			}

			estrDestroy(&pFullCmdLine);


			data = fileAlloc(tempfile, NULL);
			if (!data) {
				ErrorDeferredf("Error running NVShaderPerf or opening results.");
			} else {
				char *s = strstr(data, "r regs, ");
				if (s) {
					s += strlen("r regs, ");
					pixel_shader->nvps_pps = 0;
					sscanf(s, "%"FORM_LL"d", &pixel_shader->nvps_pps);
				}
				fileFree(data);
			}
		}
#endif
	}
}

// Really just to write out compiled shaders
static void rxbxGetVShaderStats(DWORD *compiledData, int compiledData_size, RxbxVertexShader * vertex_shader, const char *filename)
{
	assert(filename);
	if (!rdr_state.writeCompiledShaders)
		return; // Remove this if we add vshader profiling
	if (vertex_shader && compiledData ) {
		int instruction_count;
		int texture_fetch_count;
		int temporaries_count;
		int d3d_instruction_slots;
		int d3d_alu_instruction_slots;
		rxbxGetShaderStatsSub(compiledData, compiledData_size, filename, &instruction_count, &texture_fetch_count, &temporaries_count, &d3d_instruction_slots, &d3d_alu_instruction_slots, NULL, 0);
	}
}

static void rxbxDealWithCompiledResultDirect(RdrDeviceDX *device, RxbxCompiledShader *result)
{
	// Only called within the CriticalSectionCancelCompile
	RxbxPixelShader *pshader=NULL;
	RxbxVertexShader *vshader=NULL;
	RxbxHullShader *hshader=NULL;
	RxbxDomainShader *dshader=NULL;

	if (result->shader_handle!=0)
	{
		switch(result->shaderType) {
		case RDR_PIXEL_SHADER:
			stashIntFindPointer(device->pixel_shaders, result->shader_handle, &pshader);
			break;
		case RDR_VERTEX_SHADER:
			stashIntFindPointer(device->vertex_shaders, result->shader_handle, &vshader);
			break;
		case RDR_HULL_SHADER:
			stashIntFindPointer(device->hull_shaders, result->shader_handle, &hshader);
			break;
		case RDR_DOMAIN_SHADER:
			stashIntFindPointer(device->domain_shaders, result->shader_handle, &dshader);
			break;
		default:
			assert(0);
		}
		assert(pshader || vshader || hshader || dshader); // Should have been canceled otherwise
	}
	if (!result->compiledResult)
	{
		// Error
		if ( pshader )
		{
			pshader->shader.typeless = SAFE_MEMBER2(device, error_pixel_shader, shader.typeless);
			pshader->is_error_shader = 1;
		}
		else if (vshader)
		{
			vshader->shader.typeless = SAFE_MEMBER2(device, error_vertex_shader, shader.typeless);
			vshader->is_error_shader = 1;
		}
		else if (hshader)
		{
			hshader->shader = NULL;//SAFE_MEMBER2(device, error_hull_shader, shader.typeless);
			hshader->is_error_shader = 1;
		}
		else if (dshader)
		{
			dshader->shader = NULL;//SAFE_MEMBER2(device, error_vertex_shader, shader.typeless);
			dshader->is_error_shader = 1;
		}
	} else {
		// TODO: perhaps run this in the background?
		if (rdr_state.writeCompiledShaders || !rdr_state.disableShaderProfiling)
		{
			if (pshader)
				rxbxGetPShaderStats( (DWORD*)result->compiledResult, result->compiledResultSize, pshader, pshader->debug_name);
			else if (vshader)
				rxbxGetVShaderStats( (DWORD*)result->compiledResult, result->compiledResultSize, vshader, vshader->debug_name);
		}

		// Send to card
		if (pshader && !result->do_not_create_device_object)
		{
			SHADER_LOG("rxCPS Thrd 0x%08x Shdr 0x%p ShdrHndl %d %s\n", GetCurrentThreadId(), pshader, result->shader_handle, pshader->debug_name);
			//assert(!pshader->shader); // This goes off when making quick changes in the MaterialEditor because of things with a canceled compile, including, at least, the error shader being set (and also possibly issues with multiple simultaneous cancels on the same shader handle)
			if (pshader->shader.typeless)
			{
				if (pshader->is_error_shader)
					pshader->is_error_shader = 0;
				else
					rxbxReleasePixelShader(device, pshader->shader);
			}
			pshader->shader = rxbxCreateD3DPixelShader(device, pshader, pshader->debug_name, result->compiledResult, result->compiledResultSize, false, false, result->shader_handle, 0);
		} else if (vshader) {
			SHADER_LOG("rxCVS Thrd 0x%08x Shdr 0x%p ShdrHndl %d %s\n", GetCurrentThreadId(), vshader, result->shader_handle, vshader->debug_name );
			if (vshader->shader.typeless && !vshader->is_error_shader)
			{
				rxbxReleaseVertexShader(device, vshader->shader);
				vshader->shader.typeless = NULL;
			}
			vshader->shader = rxbxCreateD3DVertexShader(device, vshader, vshader->debug_name, &result->compiledResult, result->compiledResultSize, result->shader_handle, 0);
			vshader->is_error_shader = 0;
		} else if (hshader) {
			SHADER_LOG("rxCVS Thrd 0x%08x Shdr 0x%p ShdrHndl %d %s\n", GetCurrentThreadId(), hshader, result->shader_handle, hshader->debug_name );
			if (hshader->shader)// && !hshader->is_error_shader)
			{
				rxbxReleaseHullShader(device, hshader->shader);
				hshader->shader = NULL;
			}
			hshader->shader = rxbxCreateD3DHullShader(device, hshader, hshader->debug_name, &result->compiledResult, result->compiledResultSize, result->shader_handle, 0);
			hshader->is_error_shader = 0;
		} else if (dshader) {
			SHADER_LOG("rxCVS Thrd 0x%08x Shdr 0x%p ShdrHndl %d %s\n", GetCurrentThreadId(), hshader, result->shader_handle, hshader->debug_name );
			if (dshader->shader)// && !hshader->is_error_shader)
			{
				rxbxReleaseDomainShader(device, dshader->shader);
				dshader->shader = NULL;
			}
			dshader->shader = rxbxCreateD3DDomainShader(device, dshader, dshader->debug_name, &result->compiledResult, result->compiledResultSize, result->shader_handle, 0);
			dshader->is_error_shader = 0;
		}

		SAFE_FREE(result->compiledResult);
	}
	SAFE_FREE(result);
}

void rxbxDealWithAllCompiledResultsDirect(RdrDeviceDX *device)
{
	bool bNeedToDoIt=false;
	CHECKTHREAD;

	// Quick test before locking potentially slow critical section
	EnterCriticalSection(&CriticalSectionCancelCompile);
	if (eaSize(&device->compiledShaderQueue))
		bNeedToDoIt = true;
	LeaveCriticalSection(&CriticalSectionCancelCompile);

	if (!bNeedToDoIt)
		return;

	rdrShaderEnterCriticalSection();
	EnterCriticalSection(&CriticalSectionCancelCompile);
	FOR_EACH_IN_EARRAY_FORWARDS(device->compiledShaderQueue, RxbxCompiledShader, result)
	{
		rxbxDealWithCompiledResultDirect(device, result); // Frees all data internally
	}
	FOR_EACH_END;
	eaSetSize(&device->compiledShaderQueue, 0);
	LeaveCriticalSection(&CriticalSectionCancelCompile);
	rdrShaderLeaveCriticalSection();
}

#if _XBOX
HRESULT SaveUPDBFile(LPD3DXSHADER_COMPILE_PARAMETERSA pParameters);
#endif

static void releaseCompileParams(RdrShaderCompileParams *params)
{
	if (0==InterlockedDecrementRelease(&params->referenceCount)) {
		SAFE_FREE( params->programText );
		SAFE_FREE( params->defines );
		SAFE_FREE( params );
	}
}

// Skips c:\temp\Memory(#,#):
static const char *shaderErrorFilter(const char *msg)
{
	const char *s = strchr(msg, ':');
	if (s)
		s = strchr(s+1, ':');
	if (s && (strStartsWith(s, ": error") ||
		strStartsWith(s, ": warn")))
		return s+2;
	return msg;
}

typedef enum ShaderErrorfState
{
	ShaderErrorf_All,
	ShaderErrorf_None,
	ShaderErrorf_NoWarnings,
} ShaderErrorfState;

static void rxbxShaderCompileWarnOrFail(RdrShaderCompileParams *params, const char *errormsgs, bool bWarning, const char *debug_fn, HRESULT hrCompile)
{
	if (!params->hide_compiler_errors)
	{
		static const char *shaderTypes[] = {"Pixel","Vertex","Hull","Domain"};
		char compileStatus[256];
		// Called in multiple threads
		sprintf(compileStatus, "\r\n%s shader compilation %s, (compile error code = %s 0x%x): %s (%s)\r\n", 
			shaderTypes[params->shaderType],
			bWarning?"WARNING":"failure",
			rxbxGetStringForHResult(hrCompile), hrCompile,
			params->filename, params->shaderModel);
		OutputDebugStringf("%s", compileStatus);
		OutputDebugStringf("Defines: %s\r\n", params->defines);
		printErrorMessages(errormsgs, params->programText, debug_fn);
		OutputDebugStringf("\r\n");
		rdrStatusPrintfFromDeviceThread(&params->device->device_base, "%s", compileStatus);
		if (rdr_state.noErrorfOnShaderErrors == ShaderErrorf_All || (rdr_state.noErrorfOnShaderErrors == ShaderErrorf_NoWarnings && !bWarning))
		{
			ErrorDetailsf("%s Shader compile %s\nCompiling %s (%s)\n%s",
				//params->is_vertex_shader?"Vertex":"Pixel",
				shaderTypes[params->shaderType],
				bWarning?"WARNING":"ERROR",
				params->filename,
				params->shaderModel,
				shaderErrorFilter(errormsgs));
			ErrorDeferredf("%s Shader compile %s",
				shaderTypes[params->shaderType],
				bWarning?"WARNING":"ERROR");
		}
	}
}


static void rxbxBackgroundShaderCompileAPC(WorkerThread *shaderCompileWorkerThread, RdrShaderCompileParams **dwParam, WTCmdPacket *packet)//(ULONG_PTR dwParam)
{
	RdrShaderCompileParams *params = *dwParam;
	int timer;

	EnterCriticalSection(&CriticalSectionCompileShader);
	if (!params->hasBeenCompiled)
	{
		LPD3D10BLOB compiledResult = NULL, errormsgs = NULL;

		if (rdr_state.delayShaderCompileTime && tmGetThreadId(rxbx_background_shader_compile_ptr) == GetCurrentThreadId())
			Sleep(rdr_state.delayShaderCompileTime);

		if ( !rxbxCheckShaderCompileCancelQueue( params->shader_handle ) )
		{
			int iAssertReturnsSlotNum=-1; // Guaranteed to be set before used, but the compiler is whining
			char assertReturnsBuf[1024];
			HRESULT hr;
			bool bWriteFailedShader=false;
			bool bAssertReturnsActive=false;

			// generate a failed shader filename in case we need it
			static int index=0;
			char debug_fn[MAX_PATH];
			sprintf(debug_fn, "%s/failed_shader_%d.txt", fileLocalDataDir(), index);

			SHADER_LOG("rxBSCA Thrd 0x%08x QLen %d Shdr 0x%p\n", GetCurrentThreadId(), 
				eaiSize(&cancelQueue), 
				params->pixel_shader ? (void*)params->pixel_shader : (void*)params->vertex_shader );

			//if (!params->is_vertex_shader && rdrShaderPreloadSkip(params->filename))
			if ((params->shaderType == RDR_PIXEL_SHADER) && rdrShaderPreloadSkip(params->filename))
				rdrShaderPreloadLog("rxbxBackgroundShaderCompileAPC(%d, %s, %s)", params->shader_handle, params->filename, params->entryPointName);

			PERFINFO_AUTO_START("D3DCompile", 1);

			if (!rdr_state.disableShaderTimeout) {
				sprintf(assertReturnsBuf, "D3DCompile(%s, %s)", params->filename, params->entryPointName);
				iAssertReturnsSlotNum = AssertCompletes_BeginTiming(60, assertReturnsBuf, NULL, __FILE__, __LINE__);
				bAssertReturnsActive = true;
			}
			
			timer = timerAlloc();
			d3dxLogCall(params->programText, (int)params->programTextLen, "d3dxParams.UPDBTimestamp = %d;\n"
				"d3dxParams.Flags = D3DXSHADEREX_GENERATE_UPDB;\n"
				"d3dxParams.UPDBPath = \"%s\";\n"
				"D3DXCompileShaderEx(data, data_len, NULL, NULL, \"%s\", \"%s\", 0, &compiledResult, &errormsgs, NULL, &d3dxParams);",
				d3dxParams.UPDBTimestamp,
				d3dxParams.UPDBPath,
				params->entryPointName, params->shaderModel);
			if (FAILED(hr=D3DCompile(params->programText, params->programTextLen, NULL, NULL, NULL,
				params->entryPointName, params->shaderModel, params->compiler_flags, 0, &compiledResult, &errormsgs)))
			{
#define EMPTY_COMPILER_ERROR_AND_DIAGNOSIS_TEXT \
	"\"D3DCompile() failed but produced no diagnostic output. May be an internal shader compiler error if error code is E_FAIL. "	\
	"Known causes include placing [unroll] directives not immediately before a for loop."	\
	"Try compiling the shader with fxc.exe from the DirectX SDK.\""

				rxbxShaderCompileWarnOrFail(params, 
					errormsgs ? (const char *)errormsgs->lpVtbl->GetBufferPointer(errormsgs) : EMPTY_COMPILER_ERROR_AND_DIAGNOSIS_TEXT, 
					false, debug_fn, hr);

				if (1 || rdr_state.writeProcessedShaders)
				{
					bWriteFailedShader = true;
				}
			} else {
				float elapsed = timerElapsed(timer);
				if (elapsed > 1.5f) {
					verbose_printf("Slow shader compile (%1.2fs) on %s\n", elapsed, params->filename);
				}
				if (errormsgs)
				{
					bWriteFailedShader = true;
					rxbxShaderCompileWarnOrFail(params, (const char *)errormsgs->lpVtbl->GetBufferPointer(errormsgs), true, debug_fn, hr);
				}
			}

			if (bWriteFailedShader)
			{
				char debug_fn_full[MAX_PATH];
				FILE *f;
				index++;
				fileLocateWrite(debug_fn, debug_fn_full);
				mkdirtree(debug_fn_full);
				f = fileOpen(debug_fn_full, "w");
				if (f)
				{
					fwrite("// Defines: ",1,strlen("// Defines: "),f);
					fwrite(params->defines,1,strlen(params->defines),f);
					fwrite("\n",1,strlen("\n"),f);
					fwrite(params->programText, 1, strlen(params->programText), f);
					fclose(f);
				}
			}

			timerFree(timer);
            if (bAssertReturnsActive)
				AssertCompletes_StopTiming(iAssertReturnsSlotNum);

			PERFINFO_AUTO_STOP();
		}
		//if (!params->is_vertex_shader && !params->cacheResult)
		if ((params->shaderType == RDR_PIXEL_SHADER) && !params->cacheResult)
			SHADER_SERVER_LOG("%d Done compiling pixel shader in process %s\n", timerCpuTicks(), params->filename);
		rxbxReturnShaderResult( params, compiledResult, NULL, NULL );
		compiledResult = NULL;

		if (errormsgs)
			errormsgs->lpVtbl->Release(errormsgs);

		SHADER_LOG("rxBSCA Thrd 0x%08x QLen %d Shdr 0x%p DONE\n", GetCurrentThreadId(), 
			eaiSize(&cancelQueue), 
			params->pixel_shader ? (void*)params->pixel_shader : (void*)params->vertex_shader );
	}

	releaseCompileParams(params);

	LeaveCriticalSection(&CriticalSectionCompileShader);
	InterlockedDecrement(&shader_background_compile_count);
}

static void shaderServerCallback(ShaderServerRequestStatus status, ShaderCompileResponseData *response, RdrShaderCompileParams *params)
{
	// Note: this is called in the ShaderServer's thread, not the render thread or main thread or compile thread
	if (status == SHADER_SERVER_GOT_RESPONSE) {
		// TODO: UPDB needs to get written somewhere on Xbox?
		SHADER_SERVER_LOG("%d ShaderServer: Done compiling px shader %s\n", timerCpuTicks(), params->filename);
		if (!response->compiledResult)
		{
			// Failure
			rxbxShaderCompileWarnOrFail(params, response->errorMessage, false, NULL, S_OK);
		}
		if (!rxbxReturnShaderResultInternal(params, response->compiledResult, response->compiledResultSize, response->updbData, response->updbDataSize, response->updbPath)) {
			// Data not used, discard?  Data is discarded by the caller
		}
	} else {
		// No ShaderServer running, simply release our reference count
	}
	releaseCompileParams(params);
}

static void requestAsyncShaderCompile(RdrShaderCompileParams *shaderCompileParams)
{
	rxbxInitBackgroundShaderCompile();

	SHADER_SERVER_LOG("%d: Requesting %s\n", timerCpuTicks(), shaderCompileParams->filename);
	if (rdr_state.useShaderServer) {
		ShaderCompileRequestData data = {0};
		// Request a shader server compile, but still fire a background compile in case it finishes first
		shaderCompileParams->referenceCount++;
#if _XBOX
		data.target = SHADERTARGET_XBOX_UPDB;
#else
		data.target = SHADERTARGET_PC;
#endif
		data.programText = shaderCompileParams->programText;
		data.entryPointName = shaderCompileParams->entryPointName;
		data.shaderModel = shaderCompileParams->shaderModel;
		data.compilerFlags = shaderCompileParams->compiler_flags;
		data.otherFlags = SHADERTARGETVERSION_D3DCompiler_42;
		shaderServerRequestCompile(&data, shaderServerCallback, shaderCompileParams);
	}
	shaderCompileParams->referenceCount++;

	wtQueueCmd(	shaderCompileWT,
		RT_COMPILE,
		&shaderCompileParams,
		sizeof(RdrShaderCompileParams));
}

static int rxbxDefaultD3DCompileFlags(RdrDeviceDX *device)
{
	int ret = 0;
	if (device->d3d11_device)
		ret |= D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY;
	rdr_state.d3dcOptimization = CLAMP(rdr_state.d3dcOptimization, -1, 3);
	ret |= optimization_flags[rdr_state.d3dcOptimization+1];
	return ret;
}

static void createVertexShader(RdrDeviceDX *device, ShaderHandle shader_handle, const char *filename, const char *debug_fn, const char *text, char *funcname, bool forceForegroundCompile, bool allowCaching, FileList *file_list)
{
    const char *debug_name = allocAddFilename(debug_fn);
	char *shader_model = "vs_2_0";
	RdrShaderCompileParams * shaderCompileParams = NULL;

	if (device->d3d11_device)
		shader_model = "vs_4_0";
	else if (rdrSupportsFeature(&device->device_base, FEATURE_SM30))
		shader_model = device->d3d11_device?"vs_4_0_level_9_3":"vs_3_0";
	else if (rdrSupportsFeature(&device->device_base, FEATURE_SM2B))
		shader_model = "vs_2_0";

	assert(!allowCaching || forceForegroundCompile); // Can only cache foreground compiles currently

	shaderCompileParams = callocStruct(RdrShaderCompileParams);
	shaderCompileParams->device = device;
	shaderCompileParams->programText = strdup( text );
	shaderCompileParams->programTextLen = strlen(text);
	shaderCompileParams->entryPointName = funcname;
	shaderCompileParams->shaderModel = shader_model;
	shaderCompileParams->shader_handle = shader_handle;
	shaderCompileParams->is_vertex_shader = 1;
	shaderCompileParams->shaderType = RDR_VERTEX_SHADER;
	shaderCompileParams->filename = debug_name;
	shaderCompileParams->defines = strdup( genericPreProcGetDefinesString() );
	shaderCompileParams->cacheResult = allowCaching && forceForegroundCompile;
	shaderCompileParams->compiler_flags = rxbxDefaultD3DCompileFlags(device);
	if (allowCaching && forceForegroundCompile)
	{
		shaderCompileParams->cacheFilename = filename;
		shaderCompileParams->cacheFileList = file_list;
	}

	SHADER_LOG("CVS Thrd 0x%08x QLen %d Shdr 0x%p %s\n", GetCurrentThreadId(), 
		eaiSize(&cancelQueue),
		shaderCompileParams->vertex_shader, 
		shaderCompileParams->defines );

	InterlockedIncrement(&shader_background_compile_count);
	if ( rdr_state.backgroundShaderCompile && !forceForegroundCompile )
		requestAsyncShaderCompile(shaderCompileParams);
	else {
		shaderCompileParams->referenceCount++;
		rxbxBackgroundShaderCompileAPC(NULL,&shaderCompileParams,NULL);
	}
}

static void createTessShader(RdrDeviceDX *device, ShaderHandle shader_handle, const char *filename, const char *debug_fn, const char *text, char *funcname, bool forceForegroundCompile, bool allowCaching, FileList *file_list, bool isHullShader)
{
	const char *debug_name = allocAddFilename(debug_fn);
	char *shader_model = (isHullShader?"hs_5_0":"ds_5_0");
	RdrShaderCompileParams * shaderCompileParams = NULL;
	
	assert(!allowCaching || forceForegroundCompile); // Can only cache foreground compiles currently

	shaderCompileParams = callocStruct(RdrShaderCompileParams);
	shaderCompileParams->device = device;
	shaderCompileParams->programText = strdup( text );
	shaderCompileParams->programTextLen = strlen(text);
	shaderCompileParams->entryPointName = funcname;
	shaderCompileParams->shaderModel = shader_model;
	shaderCompileParams->shader_handle = shader_handle;
	shaderCompileParams->is_vertex_shader = 0;
	shaderCompileParams->shaderType = (isHullShader?RDR_HULL_SHADER:RDR_DOMAIN_SHADER);
	shaderCompileParams->filename = debug_name;
	shaderCompileParams->defines = strdup( genericPreProcGetDefinesString() );
	shaderCompileParams->cacheResult = allowCaching && forceForegroundCompile;
	shaderCompileParams->compiler_flags = rxbxDefaultD3DCompileFlags(device);
	if (allowCaching && forceForegroundCompile)
	{
		shaderCompileParams->cacheFilename = filename;
		shaderCompileParams->cacheFileList = file_list;
	}

	SHADER_LOG("CVS Thrd 0x%08x QLen %d Shdr 0x%p %s\n", GetCurrentThreadId(), 
		eaiSize(&cancelQueue), 
		(isHullShader?shaderCompileParams->hull_shader:shaderCompileParams->domain_shader), 
		shaderCompileParams->defines );

	InterlockedIncrement(&shader_background_compile_count);
	if ( rdr_state.backgroundShaderCompile && !forceForegroundCompile )
		requestAsyncShaderCompile(shaderCompileParams);
	else {
		shaderCompileParams->referenceCount++;
		rxbxBackgroundShaderCompileAPC(NULL,&shaderCompileParams,NULL);
	}
}

static RxbxPixelShader *createPixelShader(RdrDeviceDX *device, ShaderHandle shader_handle, const char *filename, const char *debug_fn, char *text, char *funcname, const char *override_shader_model, bool forceForegroundCompile, RdrShaderFinishedData *finishedData, bool allowCaching, FileList *file_list, bool hideCompileErrors)
{
	const char *debug_name = allocAddFilename(debug_fn);
	RxbxPixelShader *pixel_shader;
	const char *shader_model = "ps_2_0";
	RdrShaderCompileParams * shaderCompileParams = NULL;

	PERFINFO_AUTO_START_FUNC();

	shaderCompileParams = callocStruct(RdrShaderCompileParams);

	if (override_shader_model)
	{
		shader_model = override_shader_model;
		if (device->d3d11_device && strcmp(override_shader_model,"ps_4_0"))
		{
			// don't try to submit this to the card.  It won't match.
			shaderCompileParams->do_not_create_device_object = 1;
		}
	}
	else if (device->d3d11_device)
		shader_model = "ps_4_0";
	else if (rdrSupportsFeature(&device->device_base, FEATURE_SM30))
		shader_model = device->d3d11_device?"ps_4_0_level_9_3":"ps_3_0";
	else if (rdrSupportsFeature(&device->device_base, FEATURE_SM2B))
		shader_model = "ps_2_b";

	if (shader_handle!=0)
	{
		pixel_shader = rxbxAllocPixelShader();
		verify(stashIntAddPointer(device->pixel_shaders, shader_handle, pixel_shader, false));
		pixel_shader->debug_name = debug_name;
	} else {
		pixel_shader = NULL;
	}

	shaderCompileParams->device = device;
	shaderCompileParams->programText = text;
	shaderCompileParams->programTextLen = strlen(text);
	shaderCompileParams->entryPointName = funcname;
	shaderCompileParams->shaderModel = shader_model;
	shaderCompileParams->is_vertex_shader = 0;
	shaderCompileParams->shaderType = RDR_PIXEL_SHADER;
	shaderCompileParams->shader_handle = shader_handle;
	shaderCompileParams->filename = debug_name;
	shaderCompileParams->defines = strdup( genericPreProcGetDefinesString() );
	shaderCompileParams->finishedData = finishedData;
	shaderCompileParams->cacheResult = allowCaching && forceForegroundCompile;
	shaderCompileParams->hide_compiler_errors = hideCompileErrors;
	shaderCompileParams->compiler_flags = rxbxDefaultD3DCompileFlags(device);
	if (shaderCompileParams->cacheResult)
	{
		shaderCompileParams->cacheFilename = filename;
		shaderCompileParams->cacheFileList = file_list;
	}

	SHADER_LOG("CPS Thrd 0x%08x QLen %d Shdr 0x%p %s\n", GetCurrentThreadId(), 
		eaiSize(&cancelQueue), 
		shaderCompileParams->pixel_shader,
		shaderCompileParams->defines );

	InterlockedIncrement(&shader_background_compile_count);
	if ( rdr_state.backgroundShaderCompile && !forceForegroundCompile )
		requestAsyncShaderCompile(shaderCompileParams);
	else {
		shaderCompileParams->referenceCount++;
		rxbxBackgroundShaderCompileAPC(NULL,&shaderCompileParams,NULL);
		rxbxDealWithAllCompiledResultsDirect(device);
	}

	PERFINFO_AUTO_STOP();

	return pixel_shader;
}

static void deconstructShader(RdrDeviceDX *device, RxbxPixelShader *pshader, void *compiled_data)
{
#if _XBOX
	LPD3DXBUFFER buffer = NULL;

	assert(!pshader->microcode_text);
	assert(!pshader->microcode_jumps);
	assert(!pshader->shader_variations);

	D3DXDisassembleShader(compiled_data, FALSE, NULL, &buffer);
	if (buffer)
	{
		char *start = NULL;
		bool write_file = false;
		char *assembly;
		char *args[8];
		int count;
		char *next;
		int i;

		assembly = buffer->lpVtbl->GetBufferPointer(buffer);
		pshader->microcode_text = strdup_uncommented(assembly, STRIP_LEADING_SPACES|STRIP_EMPTY_LINES);
		assembly = strdup(pshader->microcode_text); // Gets butchered in the process
		if (write_file)
		{
			char path[MAX_PATH];
			char fn[MAX_PATH];
			FILE *f;
			const char *filename = pshader->debug_name;
			if (!filename)
				filename = "no_name_precompiled";
			sprintf(fn, "shaders_processed/dyn/%s.asm.phl", filename);
			fileLocateWrite(fn, path);
			mkdirtree(path);
			f = fopen(path, "w");
			if (f)
			{
				fwrite(assembly, 1, strlen(assembly), f);
				fclose(f);
			}
		}
		next = assembly;
		do {
			char *this_line = next;
			count = tokenize_line(next, args, &next);
			if (!count)
				continue;
 			if (strStartsWith(args[0], "//") || args[0][0]==';' || args[0][0]=='#')
				continue;

			if (!start)
				start = this_line;

// 			if (strStartsWith(args[0], "ps_") || strStartsWith(args[0], "dcl") ||
// 				strStartsWith(args[0], "xps_") || strStartsWith(args[0], "def") ||
// 				strStartsWith(args[0], "config"))
// 				continue;
			if (stricmp(args[0], "cjmp")==0)
			{
				bool inverted = args[1][0]=='!';
				if (inverted)
					args[1]++;
				if (args[1][0]=='b')
				{
					MicrocodeJump *jump = calloc(1, sizeof(MicrocodeJump));
					sscanf(args[1], "b%d", &jump->bool_constant);
					jump->inverted = inverted;
					jump->src_offset = this_line - assembly;
					jump->skip_offset = next - assembly;
					jump->dst_label = args[2];
					eaPush(&pshader->microcode_jumps, jump);
				}
			}
			else if (stricmp(args[0], "label")==0)
			{
				for (i = 0; i < eaSize(&pshader->microcode_jumps); ++i)
				{
					if (stricmp(args[1], pshader->microcode_jumps[i]->dst_label) == 0)
					{
						assert(!pshader->microcode_jumps[i]->dst_offset);
						pshader->microcode_jumps[i]->dst_offset = this_line - assembly; // leave the label in the microcode in case something else needs it
					}
				}
			}
		} while (next);

		for (i = 0; i < eaSize(&pshader->microcode_jumps); ++i)
		{
			if (!pshader->microcode_jumps[i]->dst_offset)
			{
				free(pshader->microcode_jumps[i]);
				eaRemove(&pshader->microcode_jumps, i);
				--i;
			}
			else
			{
				if (start > assembly)
				{
					pshader->microcode_jumps[i]->src_offset -= start - assembly;
					pshader->microcode_jumps[i]->skip_offset -= start - assembly;
					pshader->microcode_jumps[i]->dst_offset -= start - assembly;
				}

				pshader->microcode_jumps[i]->dst_label = NULL;
			}
		}

		if (eaSize(&pshader->microcode_jumps))
		{
			if (start > assembly)
			{
				char *microcode_text = strdup(pshader->microcode_text + (start - assembly));
				free(pshader->microcode_text);
				pshader->microcode_text = microcode_text;
			}

			pshader->shader_variations = stashTableCreateInt(256);
			pshader->microcode_text_len = strlen(pshader->microcode_text);
		}
		else
		{
			SAFE_FREE(pshader->microcode_text);
		}
		SAFE_FREE(assembly);

		buffer->lpVtbl->Release(buffer);
	}
#endif
}

static HRESULT rxbxCreatePixelShader(RdrDeviceDX *device, CONST DWORD *pdata, DWORD data_size, RdrPixelShaderObj *ppPixelShader)
{
	HRESULT hr;
	U32 crc = cryptAdler32((U8*)pdata, data_size);
	if (stashIntFindPointer(device->stPixelShaderCache, crc, &ppPixelShader->typeless))
	{
		if (device->d3d11_device)
			ID3D11PixelShader_AddRef(ppPixelShader->pixel_shader_d3d11);
		else
			IDirect3DPixelShader9_AddRef(ppPixelShader->pixel_shader_d3d9);
		hr = 0;
	} else {
		if (device->d3d11_device)
			hr = ID3D11Device_CreatePixelShader(device->d3d11_device, pdata, data_size, NULL, &ppPixelShader->pixel_shader_d3d11);
		else
			hr = IDirect3DDevice9_CreatePixelShader(device->d3d_device, pdata, &ppPixelShader->pixel_shader_d3d9);
		if (!FAILED(hr))
		{
			stashIntAddPointer(device->stPixelShaderCache, crc, ppPixelShader->typeless, false);
#if !_XBOX
			rdrTrackUserMemoryDirect(&device->device_base, "rxbx:PixelShaders", 1, (data_size*4+4095)&~4095); // Incredibly rough estimate, still undertracking, probably
#endif
		}
	}
	if (!FAILED(hr))
		rxbxLogCreatePixelShader(device, *ppPixelShader);
	return hr;
}

RdrPixelShaderObj rxbxCreateD3DPixelShader(RdrDeviceDX *device, RxbxPixelShader *pshader, const char *filename, void *compiled_data, int compiled_data_size, bool is_precompiled, bool is_assembled, ShaderHandle shader_handle, U32 new_crc)
{
	HRESULT hr;
	int shader_input_bind;
	RdrPixelShaderObj shader = {NULL};

    updateNvidiaLowerOptimization(device);

    SHADER_LOG("CPSPc Thrd 0x%08x Shdr 0x%p\n", GetCurrentThreadId(), pshader);
	if (!rdrShaderPreloadSkip(filename))
		rdrShaderPreloadLog("IDirect3DDevice9_CreatePixelShader(%p, %s%s)", pshader, is_precompiled?"precompiled, ":"", filename);

	if (pshader->debug_name != filename)
		pshader->debug_name = allocAddFilename(filename);

	XMemAllocPersistantPhysical(true);
	XMemAllocSetBlamee("rxbx:PixelShaders");

	PERFINFO_AUTO_START("rxbxCreatePixelShader", 1);
	if (FAILED(hr = rxbxCreatePixelShader(device, (DWORD *)compiled_data, compiled_data_size, &shader)))
	{
		// IMPLEMENT support storing this data
		//OutputDebugStringf("\r\nPixel shader creation failure: %s\r\n", filename);
		//OutputDebugStringf("Defines: %s\r\n", rdrGetDefinesString());
		rdrStatusPrintfFromDeviceThread(&device->device_base, "Pixel shader creation FAILED - %s", rxbxGetStringForHResult(hr));
		log_printf(LOG_ERRORS, "Pixel shader (%s) creation FAILED - %s", filename, rxbxGetStringForHResult(hr));
		assert(!shader.typeless);
		rxbxDumpDeviceStateOnError(device, hr);
	}
	PERFINFO_AUTO_STOP();

	if (device->d3d11_device)
	{
		bool index_used[MAX_TEXTURE_UNITS_TOTAL] = {0};
		int next_index;

		CHECKX(D3DReflect(compiled_data, compiled_data_size, &IID_ID3D11ShaderReflection, (void**) &pshader->reflection));

		for (shader_input_bind = 0; shader_input_bind < MAX_TEXTURE_UNITS_TOTAL; ++shader_input_bind)
			pshader->texture_resource_slot[shader_input_bind] = 255;
		for (shader_input_bind = 0; ; ++shader_input_bind)
		{
			D3D11_SHADER_INPUT_BIND_DESC resource_bind_desc;
			hr = pshader->reflection->lpVtbl->GetResourceBindingDesc(
				pshader->reflection,
				shader_input_bind,
				&resource_bind_desc);
			if (FAILED(hr))
				break;
			else
			if (resource_bind_desc.Type == D3D10_SIT_CBUFFER && resource_bind_desc.BindPoint < PS_CONSTANT_BUFFER_COUNT)
			{
				ID3D11ShaderReflectionConstantBuffer *buffer = pshader->reflection->lpVtbl->GetConstantBufferByName(pshader->reflection,resource_bind_desc.Name);
				D3D11_SHADER_BUFFER_DESC bufferDesc;
				buffer->lpVtbl->GetDesc(buffer,&bufferDesc);
				pshader->buffer_sizes[resource_bind_desc.BindPoint] = bufferDesc.Size;
			}
			else
			if (resource_bind_desc.Type == D3D10_SIT_TEXTURE)
			{
				// determine sampler for this texture bind by name lookup
				U32 bind_texture_slot = resource_bind_desc.BindPoint;
				char tempName[1024];
				sprintf(tempName, "sam%s", resource_bind_desc.Name);
				hr = pshader->reflection->lpVtbl->GetResourceBindingDescByName(
					pshader->reflection,
					tempName,
					&resource_bind_desc);
				if (!FAILED(hr))
				{
					// Has an explicit sampler
					assert(resource_bind_desc.Type == D3D10_SIT_SAMPLER);
					assert(resource_bind_desc.BindPoint == bind_texture_slot); // All of our explicit samplers are 1:1 mapped for now
					pshader->texture_resource_slot[resource_bind_desc.BindPoint] = bind_texture_slot;
				} else {
					D3D11_SHADER_INPUT_BIND_DESC sampler_resource_bind_desc = { 0 };
					hr = pshader->reflection->lpVtbl->GetResourceBindingDescByName(
						pshader->reflection,
						resource_bind_desc.Name,
						&sampler_resource_bind_desc);
					if (SUCCEEDED(hr))
					{
						assert(sampler_resource_bind_desc.Type == D3D10_SIT_SAMPLER || sampler_resource_bind_desc.Type == D3D10_SIT_TEXTURE);
						pshader->texture_resource_slot[resource_bind_desc.BindPoint] = bind_texture_slot;
					}
				}
				index_used[bind_texture_slot] = 1;
			}
		}
		// Fill in unused ones (in theory we could be slightly more optimal by never binding these textures,
		//  but in practice this rarely happens outside of debugging shaders)
		for (next_index=0, shader_input_bind = 0; shader_input_bind < MAX_TEXTURE_UNITS_TOTAL; ++shader_input_bind)
		{
			if (pshader->texture_resource_slot[shader_input_bind] == 255)
			{
				while (index_used[next_index])
				{
					next_index++;
					assert(next_index < MAX_TEXTURE_UNITS_TOTAL);
				}
				pshader->texture_resource_slot[shader_input_bind] = next_index;
				next_index++;
			}
		}
#if _FULLDEBUG // Verify everything was used exactly once
		while (index_used[next_index] && next_index < MAX_TEXTURE_UNITS_TOTAL)
		{
			next_index++;
		}
		assert(next_index == MAX_TEXTURE_UNITS_TOTAL);
#endif

		pshader->reflection->lpVtbl->Release(pshader->reflection);
		pshader->reflection = NULL;
	}


	XMemAllocSetBlamee(NULL);
	XMemAllocPersistantPhysical(false);

	if (!is_assembled) {
		deconstructShader(device, pshader, compiled_data);

		if (!new_crc)
			new_crc = cryptAdler32(compiled_data, compiled_data_size);
		rdrShaderEnterCriticalSection();
		lpcUpdateCachedCrc(device, true, true, shader_handle, new_crc);
		rdrShaderLeaveCriticalSection();
	}

	return shader;
}

static RxbxPixelShader *createPixelShaderPrecompiled(RdrDeviceDX *device, const char *filename, void *precompiled_data, int precompiled_data_size, ShaderHandle shader_handle, U32 new_crc)
{
	RxbxPixelShader *pshader = rxbxAllocPixelShader();
	verify(stashIntAddPointer(device->pixel_shaders, shader_handle, pshader, false));
	assert(!pshader->shader.typeless);
	pshader->shader = rxbxCreateD3DPixelShader(device, pshader, filename, precompiled_data, precompiled_data_size, true, false, shader_handle, new_crc);
	return pshader;
}

static RxbxVertexShader *createVertexShaderPrecompiled(RdrDeviceDX *device, ShaderHandle shader_handle, const char *filename, char *entrypoint, void **precompiled_data, int precompiled_size, U32 new_crc)
{
	RxbxVertexShader *vshader = rxbxAllocVertexShader(device, shader_handle, filename);

	SHADER_LOG("CVSPc Thrd 0x%08x Shdr 0x%p\n", GetCurrentThreadId(), vshader);

	vshader->shader = rxbxCreateD3DVertexShader(device, vshader, filename, precompiled_data, precompiled_size, shader_handle, new_crc);
	return vshader;
}

__forceinline static int lpcGetVertexShaderKey(ShaderHandle programHandle)
{
	return 0x10000000 | programHandle;
}

__forceinline static int lpcGetPixelShaderKey(ShaderHandle programHandle)
{
	return 0x20000000 | programHandle;
}

static U32 lpcGetCachedCrc(RdrDeviceDX *device, int pixel_shader, int precompiled, ShaderHandle programHandle)
{
	int key = pixel_shader?lpcGetPixelShaderKey(programHandle):lpcGetVertexShaderKey(programHandle);
	U32 cached_crc;

	rdrShaderAssertInCriticalSection();

	if (precompiled)
		key |= 0x80000000;

	if (!device->lpc_crctable)
		device->lpc_crctable = stashTableCreateInt(1024);

	if (stashIntFindInt(device->lpc_crctable, key, &cached_crc))
		return cached_crc;
	return 0;
}

static void lpcUpdateCachedCrc(RdrDeviceDX *device, int pixel_shader, int precompiled, ShaderHandle programHandle, U32 crc)
{
	int key = pixel_shader?lpcGetPixelShaderKey(programHandle):lpcGetVertexShaderKey(programHandle);

	rdrShaderAssertInCriticalSection();

	if (precompiled)
		key |= 0x80000000;

	if (!device->lpc_crctable)
		device->lpc_crctable = stashTableCreateInt(1024);

	stashIntAddInt(device->lpc_crctable, key, crc, true);
}

static RxbxVertexShader *loadVertexShader(RdrDeviceDX *device, const char* filename, 
	char *entrypoint, RxbxVertexShader *existing_shader, ShaderHandle handle, bool forceForegroundCompile, const RxbxPreloadedShaderData *preloaded_data)
{
	void *precompiled_data;
	int precompiled_size;
	RxbxVertexShader *shader = NULL;
	bool bAllowCaching = forceForegroundCompile && filename && !preloaded_data;
	U32 cached_crc=0, new_crc=0;
	bool bNeedToCompile=true;

	PERFINFO_AUTO_START("loadVertexShader", 1);

	// Check pre-compiled cache here, call createVertexShaderFromPrecompiled instead
	// But not if we have a existing version (as then it's either identical or in
	// need of recompiling, either way our cache is unneeded).
	if (bAllowCaching && rdrLoadPrecompiledShaderSync(filename, entrypoint, &precompiled_data, &precompiled_size, rdrGetDeviceIdentifier(&(device->device_base))))
	{
		bNeedToCompile = false;
		new_crc = cryptAdler32(precompiled_data, precompiled_size);
		if (!rdr_state.disableShaderCache)
			cached_crc = lpcGetCachedCrc(device, false, true, handle);

		if (existing_shader && cached_crc == new_crc && !existing_shader->is_error_shader) {
			shader = existing_shader;
		} else {
			if (existing_shader)
			{
				rxbxFreeVertexShader(device, handle, existing_shader);
				existing_shader = NULL;
			}
			shader = createVertexShaderPrecompiled(device, handle, filename, entrypoint, &precompiled_data, precompiled_size, new_crc);
			// Invalidate text-based/non-precompiled cache
			lpcUpdateCachedCrc(device, false, false, handle, 0);

			if (!shader->shader.typeless) // Error - maybe corrupt data or something
			{
				// Free it (without adding it to the cancel queue)
				rxbxFreeVertexShaderInternal(device, handle, shader, false);
				shader = NULL;

				bNeedToCompile = true;
			}
		}
		SAFE_FREE(precompiled_data);
	}
	if (bNeedToCompile) {
		const char*  programText;
		FileList file_list = NULL;
		char debug_fn[1024];
		char debug_header[1024];

		if (preloaded_data)
		{
			char temps[MAX_PATH];
			programText = (char*)preloaded_data->data;
			sprintf(temps, "preloaded_%s", entrypoint);
			rdrShaderGetDebugNameAndHeader(SAFESTR(debug_fn), SAFESTR(debug_header),
				"shaders_processed", "//", temps, ".vhl", true);
		}
		else
		{
			if (!rdr_state.disableShaderCache)
				cached_crc = lpcGetCachedCrc(device, false, false, handle);
			programText = rdrLoadShaderData(filename, "shaders/D3D", "//", cached_crc, &new_crc, bAllowCaching?&file_list:NULL,
				SAFESTR(debug_fn), SAFESTR(debug_header));
		}
		if (programText)
		{
			if (!existing_shader || cached_crc != new_crc)
			{
				if (existing_shader)
				{
					rxbxFreeVertexShader(device, handle, existing_shader);
					existing_shader = NULL;
				}
				shader = rxbxAllocVertexShader(device, handle, allocAddFilename(debug_fn));

				createVertexShader(device, handle, filename, allocAddFilename(debug_fn), programText, entrypoint, forceForegroundCompile, bAllowCaching, &file_list);
				if (forceForegroundCompile)
					rxbxDealWithAllCompiledResultsDirect(device);

				SHADER_LOG("Create Vertex Shader %d 0x%08p %s\n", handle, shader, filename);

				lpcUpdateCachedCrc(device, false, false, handle, new_crc);
			}
			else
			{
				shader = existing_shader;
			}
		}
		FileListDestroy(&file_list);
	}
	// Clean up
	genericPreProcReset();
	rdrFreeShaderData();
	PERFINFO_AUTO_STOP();

	return shader;
}

typedef struct RxbxVertexShaderLoadParams
{
	RxbxProgramDef def;
	ShaderHandle shader_handle;
	RdrDeviceDX *device;
	bool has_existing_shader;
} RxbxVertexShaderLoadParams;

static VOID CALLBACK rxbxLoadVertexShaderAPC(ULONG_PTR dwParam)
{
	int j;
	RxbxVertexShaderLoadParams *params = (RxbxVertexShaderLoadParams*)dwParam;
	RxbxProgramDef *def = &params->def;

	if (rdr_state.delayShaderCompileTime && tmGetThreadId(rxbx_background_shader_compile_ptr) == GetCurrentThreadId())
		Sleep(rdr_state.delayShaderCompileTime);

	rdrShaderEnterCriticalSection();
	rdrShaderResetCache(true);

	genericPreProcReset();
	for (j = 0; def->defines[j] && def->defines[j][0]; j++)
	{
		genericPreProcAddDefine(def->defines[j]);
	}

	{
		void *precompiled_data;
		int precompiled_size;
		U32 cached_crc=0, new_crc=0;
		bool bNeedToCompile=true;

		// Check pre-compiled cache here, call createVertexShaderFromPrecompiled instead
		// But not if we have a existing version (as then it's either identical or in
		// need of recompiling, either way our cache is unneeded).
		if (rdrLoadPrecompiledShaderSync(def->filename, def->entry_funcname, &precompiled_data, &precompiled_size, rdrGetDeviceIdentifier(&(params->device->device_base))))
		{
			bNeedToCompile = false;
			// Move this check to the main thread when binding the return data?
			new_crc = cryptAdler32(precompiled_data, precompiled_size);
			if (!rdr_state.disableShaderCache)
				cached_crc = lpcGetCachedCrc(params->device, false, true, params->shader_handle);

			if (params->has_existing_shader && cached_crc == new_crc) {
				// Nothing changed, just use the existing shader
				// Main thread is already using the old shader, just let it continue
			} else {
				// Return the precompiled data
				RdrShaderCompileParams dummy_params = {0};
				dummy_params.device = params->device;
				dummy_params.shader_handle = params->shader_handle;
				dummy_params.is_vertex_shader = 1;
				dummy_params.shaderType = RDR_VERTEX_SHADER;
				rxbxReturnShaderResultInternal(&dummy_params, precompiled_data, precompiled_size, NULL, 0, NULL);
			}
			SAFE_FREE(precompiled_data);
		}
		if (bNeedToCompile) {
			const char*  programText;
			FileList file_list = NULL;
			char debug_fn[MAX_PATH];
			char debug_header[1024];

			if (!rdr_state.disableShaderCache)
				cached_crc = lpcGetCachedCrc(params->device, false, false, params->shader_handle);
			programText = rdrLoadShaderData(def->filename, "shaders/D3D", "//", cached_crc, &new_crc, &file_list,
				SAFESTR(debug_fn), SAFESTR(debug_header));
			if (programText)
			{
				if (!params->has_existing_shader || cached_crc != new_crc)
				{
					// starts the shader compiling (could queue another APC but instead just compiles here and now for easier caching)
					createVertexShader(params->device, params->shader_handle, def->filename, allocAddFilename(debug_fn), programText, def->entry_funcname, true, true, &file_list);

					lpcUpdateCachedCrc(params->device, false, false, params->shader_handle, new_crc);
				}
				else
				{
					// CRCs are the same, just use the old one
					// Main thread is already using the old shader, just let it continue
				}
			}
			FileListDestroy(&file_list);
		}

		// Clean up
		genericPreProcReset();
		rdrFreeShaderData();
	}

	rdrShaderResetCache(false);
	rdrShaderLeaveCriticalSection();

	SAFE_FREE(params);

	InterlockedDecrement(&shader_background_compile_count);
}

void rxbxLoadVertexShaderAsync(RdrDeviceDX *device, RxbxProgramDef *def, ShaderHandleAndFlags *shader)
{
	RxbxVertexShader *vshader;
	RxbxVertexShaderLoadParams *params;

	if (!shader->handle)
		shader->handle = rdrGenShaderHandle();

	params = callocStruct(RxbxVertexShaderLoadParams);
	params->def = *def;
	params->shader_handle = shader->handle;
	params->device = device;
	params->has_existing_shader = stashIntFindPointer(device->vertex_shaders, shader->handle, NULL);

	// Add intrinsic defines
	{
		const char *defines = rxbxGetIntrinsicDefines(&device->device_base);
		char *defines2;
		char *context=NULL;
		char *s;
		int numDefines=0;
		for (; params->def.defines[numDefines]; numDefines++);
		strdup_alloca(defines2, defines);
		while (s=strtok_s(defines2, " ", &context)) {
			defines2 = NULL;
			assert(numDefines < ARRAY_SIZE(params->def.defines));
			params->def.defines[numDefines++] = allocAddCaseSensitiveString(s);
		}
	}

	// Reuse old vertex shader if it exists
	// Should also free the old one?  I think not, so it can reuse the cached one
	if (!stashIntFindPointer(device->vertex_shaders, shader->handle, NULL))
	{
		int i;
		char debug_fn[MAX_PATH];
		char *s;
		strcpy(debug_fn, def->filename);
		s = strrchr(debug_fn, '.');
		*s='\0';
		for (i=0; params->def.defines[i]; i++)
		{
			strcat(debug_fn, "_");
			strcat(debug_fn, params->def.defines[i]);
		}
		strcat(debug_fn, ".vhl");
		vshader = rxbxAllocVertexShader(device, shader->handle, allocAddFilename(debug_fn));
	} else {
		params->has_existing_shader = true;
	}

	shader->loaded = 1; // Loading, not loaded...

	InterlockedIncrement(&shader_background_compile_count);
	if (rdr_state.backgroundShaderCompile)
	{
		tmQueueUserAPC(rxbxLoadVertexShaderAPC, rxbx_background_shader_compile_ptr, 
			(ULONG_PTR)params);
	} else {
		rxbxLoadVertexShaderAPC((ULONG_PTR)params);
	}
}

static RxbxHullShader *loadHullShader(RdrDeviceDX *device, const char* filename, 
	char *entrypoint, RxbxHullShader *existing_shader, ShaderHandle handle, bool forceForegroundCompile, const RxbxPreloadedShaderData *preloaded_data)
{
	RxbxHullShader *shader = NULL;
	bool bAllowCaching = forceForegroundCompile && filename && !preloaded_data;
	U32 cached_crc=0, new_crc=0;
	bool bNeedToCompile=true;
	const char*  programText;
	FileList file_list = NULL;
	char debug_fn[1024];
	char debug_header[1024];

	PERFINFO_AUTO_START("loadHullShader", 1);

	if (preloaded_data)
	{
		char temps[MAX_PATH];
		programText = (char*)preloaded_data->data;
		sprintf(temps, "preloaded_%s", entrypoint);
		rdrShaderGetDebugNameAndHeader(SAFESTR(debug_fn), SAFESTR(debug_header),
			"shaders_processed", "//", temps, ".hhl", true);
	}
	else
	{
		if (!rdr_state.disableShaderCache)
			cached_crc = lpcGetCachedCrc(device, false, false, handle);
		programText = rdrLoadShaderData(filename, "shaders/D3D", "//", cached_crc, &new_crc, bAllowCaching?&file_list:NULL,
			SAFESTR(debug_fn), SAFESTR(debug_header));
	}
	if (programText)
	{
		if (!existing_shader || cached_crc != new_crc)
		{
			if (existing_shader)
			{
				rxbxFreeHullShader(device, handle, existing_shader);
				existing_shader = NULL;
			}
			shader = rxbxAllocHullShader(device, handle, allocAddFilename(debug_fn));

			createTessShader(device, handle, filename, allocAddFilename(debug_fn), programText, entrypoint, forceForegroundCompile, bAllowCaching, &file_list,true);
			if (forceForegroundCompile)
				rxbxDealWithAllCompiledResultsDirect(device);

			SHADER_LOG("Create Hull Shader %d 0x%08p %s\n", handle, shader, filename);

			lpcUpdateCachedCrc(device, false, false, handle, new_crc);
		}
		else
		{
			shader = existing_shader;
		}
	}
	FileListDestroy(&file_list);
	// Clean up
	genericPreProcReset();
	rdrFreeShaderData();
	PERFINFO_AUTO_STOP();

	return shader;
}

static RxbxDomainShader *loadDomainShader(RdrDeviceDX *device, const char* filename, 
	char *entrypoint, RxbxDomainShader *existing_shader, ShaderHandle handle, bool forceForegroundCompile, const RxbxPreloadedShaderData *preloaded_data)
{
	RxbxDomainShader *shader = NULL;
	bool bAllowCaching = forceForegroundCompile && filename && !preloaded_data;
	U32 cached_crc=0, new_crc=0;
	bool bNeedToCompile=true;
	const char*  programText;
	FileList file_list = NULL;
	char debug_fn[1024];
	char debug_header[1024];

	PERFINFO_AUTO_START("loadDomainShader", 1);

	if (preloaded_data)
	{
		char temps[MAX_PATH];
		programText = (char*)preloaded_data->data;
		sprintf(temps, "preloaded_%s", entrypoint);
		rdrShaderGetDebugNameAndHeader(SAFESTR(debug_fn), SAFESTR(debug_header),
			"shaders_processed", "//", temps, ".dhl", true);
	}
	else
	{
		if (!rdr_state.disableShaderCache)
			cached_crc = lpcGetCachedCrc(device, false, false, handle);
		programText = rdrLoadShaderData(filename, "shaders/D3D", "//", cached_crc, &new_crc, bAllowCaching?&file_list:NULL,
			SAFESTR(debug_fn), SAFESTR(debug_header));
	}
	if (programText)
	{
		if (!existing_shader || cached_crc != new_crc)
		{
			if (existing_shader)
			{
				rxbxFreeDomainShader(device, handle, existing_shader);
				existing_shader = NULL;
			}
			shader = rxbxAllocDomainShader(device, handle, allocAddFilename(debug_fn));

			createTessShader(device, handle, filename, allocAddFilename(debug_fn), programText, entrypoint, forceForegroundCompile, bAllowCaching, &file_list, false);
			if (forceForegroundCompile)
				rxbxDealWithAllCompiledResultsDirect(device);

			SHADER_LOG("Create Domain Shader %d 0x%08p %s\n", handle, shader, filename);

			lpcUpdateCachedCrc(device, false, false, handle, new_crc);
		}
		else
		{
			shader = existing_shader;
		}
	}
	FileListDestroy(&file_list);
	// Clean up
	genericPreProcReset();
	rdrFreeShaderData();
	PERFINFO_AUTO_STOP();

	return shader;
}


static RxbxPixelShader *loadPixelShader(RdrDeviceDX *device, const char* filename, char *entrypoint, RxbxPixelShader *existing_shader, ShaderHandle handle, bool forceForegroundCompile, const RxbxPreloadedShaderData *preloaded_data)
{
	void *precompiled_data;
	int precompiled_size;
	RxbxPixelShader *pixel_shader = NULL;
	bool bAllowCaching = forceForegroundCompile && filename && !preloaded_data;
	U32 cached_crc=0, new_crc=0;
	bool bNeedToCompile=true;

	PERFINFO_AUTO_START("loadPixelShader", 1);

	// Check pre-compiled cache here, call createPixelShaderPrecompiled instead
	// But not if we have a existing version (as then it's either identical or in
	// need of recompiling, either way our cache is unneeded).
	if (bAllowCaching && rdrLoadPrecompiledShaderSync(filename, entrypoint, &precompiled_data, &precompiled_size, rdrGetDeviceIdentifier(&(device->device_base))))
	{
		bNeedToCompile = false;
		new_crc = cryptAdler32(precompiled_data, precompiled_size);
		if (!rdr_state.disableShaderCache)
			cached_crc = lpcGetCachedCrc(device, true, true, handle);

		if (existing_shader && cached_crc == new_crc && !existing_shader->is_error_shader) {
			pixel_shader = existing_shader;
		} else {
			if (existing_shader)
			{
				rxbxFreePixelShader(device, handle, existing_shader);
				existing_shader = NULL;
			}
			pixel_shader = createPixelShaderPrecompiled(device, filename, precompiled_data, precompiled_size, handle, new_crc);
			// Invalidate text-based/non-precompiled cache
			lpcUpdateCachedCrc(device, true, false, handle, 0);

			if (!pixel_shader->shader.typeless) // Error - maybe corrupt data or something
			{
				// Free it (without adding it to the cancel queue)
				rxbxFreePixelShaderInternal(device, handle, pixel_shader, false);
				pixel_shader = NULL;

				bNeedToCompile = true;
			}
		}
		SAFE_FREE(precompiled_data);
	}

	if (bNeedToCompile) {
		const char*  programText;
		FileList file_list = NULL;
		char debug_fn[MAX_PATH];
		char debug_header[1024];

		if (!rdr_state.disableShaderCache)
			cached_crc = lpcGetCachedCrc(device, true, false, handle);

		if (preloaded_data)
		{
			programText = preloaded_data->data;
			new_crc = 0x23234545; // Arbitrary number, but unique so find in files finds it if someone searches for it...
			debug_header[0] = 0;
			rdrShaderGetDebugNameAndHeader(SAFESTR(debug_fn), NULL, 0, "shaders_processed", "//", filename, strrchr(filename, '.'), true);
		}
		else
		{
			programText = rdrLoadShaderData(filename, "shaders/D3D", "//", cached_crc, &new_crc, bAllowCaching?&file_list:NULL,
				SAFESTR(debug_fn), SAFESTR(debug_header));
		}
		if (programText)
		{
			if (!existing_shader || cached_crc != new_crc)
			{
				if (existing_shader)
				{
					rxbxFreePixelShader(device, handle, existing_shader);
					existing_shader = NULL;
				}
				pixel_shader = createPixelShader(device, handle, filename, debug_fn, strdup( programText ), entrypoint, NULL, forceForegroundCompile, NULL, bAllowCaching, &file_list, false);

				SHADER_LOG("Create Pixel Shader %d 0x%08p %s\n", handle, pixel_shader, filename);

				lpcUpdateCachedCrc(device, true, false, handle, new_crc);
			}
			else
			{
				pixel_shader = existing_shader;
			}
		}
		FileListDestroy(&file_list);
	}

	// Clean up
	genericPreProcReset();
	rdrFreeShaderData();
	PERFINFO_AUTO_STOP();

	return pixel_shader;
}

void rxbxSetPixelShaderDataDirect(RdrDeviceDX *device, RdrShaderParams *params, WTCmdPacket *packet)
{
	ShaderHandle handle = params->shader_handle;
	RxbxPixelShader *pixel_shader=NULL;
	U32 cached_crc=0, new_crc = 0;

	PERFINFO_AUTO_START_FUNC();

	assert(params->shader_type==SPT_FRAGMENT);

	if (!device->pixel_shaders)
		device->pixel_shaders = stashTableCreateInt(1024);

	updateNvidiaLowerOptimization(device);

	rdrShaderEnterCriticalSection();
	rdrShaderResetCache(true);

	if (params->isPrecompiled)
	{
		void *dataptr = params + 1;
		int dataptr_size = params->shader_data_size;
		assert(dataptr_size);

		new_crc = cryptAdler32(dataptr, dataptr_size);
		if (!rdr_state.disableShaderCache)
			cached_crc = lpcGetCachedCrc(device, true, true, handle);

		stashIntFindPointer(device->pixel_shaders, handle, &pixel_shader);

		if (pixel_shader && new_crc == cached_crc && !pixel_shader->is_error_shader) {
			// It's good!
		} else {
			if (pixel_shader)
				rxbxFreePixelShader(device, handle, pixel_shader);

			pixel_shader = createPixelShaderPrecompiled(device, params->shader_debug_name, dataptr, dataptr_size, handle, new_crc);

			if (!pixel_shader->shader.typeless) {
				const char *error_filename = params->shader_error_filename;
				if (!error_filename) {
					error_filename = "shaders/D3D/error.phl";
				}
				pixel_shader = loadPixelShader(device, error_filename, "error_pixelshader", pixel_shader, handle, true, NULL);
			}
			if (!rdr_state.disableShaderProfiling || rdr_state.writeCompiledShaders)
			{
				char filename[MAX_PATH];
				sprintf( filename, "shaders_processed/dyn/precompiled/%s.asm.phl", params->shader_debug_name );
				rxbxGetPShaderStats( dataptr, dataptr_size, pixel_shader, filename );
			}
			// Invalidate text-based/non-precompiled cache
			lpcUpdateCachedCrc(device, true, false, handle, 0);
		}

	} else {
		char debug_fn[MAX_PATH];
		char debug_header[1000];
		int i;
		int special_defines_size = sizeof(char*)*params->num_defines;
		char **special_defines = (char**)(params+1);
		char *programText = strdup((char*)(params +1) + special_defines_size);

		// Preprocess
        genericPreProcReset();
		for (i=0; i<params->num_defines; i++)
			genericPreProcAddDefine(special_defines[i]);
		if (params->intrinsic_defines)
			rxbxAddSpecifiedIntrinsicDefines(params->intrinsic_defines);
		else
			rxbxAddIntrinsicDefines(device);
		if (!rdr_state.disableShaderCache && handle)
			cached_crc = lpcGetCachedCrc(device, true, false, handle);
		rdrPreProcessShader(&programText, "shaders/D3D", params->shader_debug_name, ".phl", "//", cached_crc, &new_crc, params->finishedCallbackData?&params->finishedCallbackData->file_list:NULL, debug_fn, debug_header);

		stashIntFindPointer(device->pixel_shaders, handle, &pixel_shader);
		if (!pixel_shader || cached_crc != new_crc)
		{
			if (pixel_shader)
				rxbxFreePixelShader(device, handle, pixel_shader);
			pixel_shader = createPixelShader(device, handle, params->shader_debug_name, debug_fn, programText, "main_output", params->override_shader_model, params->noBackgroundCompile, params->finishedCallbackData, false, NULL, params->hideCompileErrors);

			SHADER_LOG("Create Pixel Shader %d 0x%08p %s\n", handle, pixel_shader, params->shader_debug_name);

			if (handle)
				lpcUpdateCachedCrc(device, true, false, handle, new_crc);
		} else {
			// Nothing changed
			if (params->finishedCallbackData)
			{
				params->finishedCallbackData->compilationFailed = true; // Didn't actually fail, but pretend it did to release resources
				TimedCallback_Run(rxbxReturnShaderResultMainThread, params->finishedCallbackData, 0);
			}
			SAFE_FREE(programText);
		}

		if (!pixel_shader && handle) {
			const char *error_filename = params->shader_error_filename;
			if (!error_filename) {
				error_filename = "shaders/D3D/error.phl";
			}
			pixel_shader = loadPixelShader(device, error_filename, "error_pixelshader", NULL, handle, true, NULL);
		}

        //SAFE_FREE(programText);
	}

	rdrShaderResetCache(false);
	rdrShaderLeaveCriticalSection();

	if (!rdrShaderPreloadSkip(params->shader_debug_name))
		rdrShaderPreloadLog("rxbxSetPixelShaderDataDirect(%p, %s)", pixel_shader, params->shader_debug_name);

	PERFINFO_AUTO_STOP();
}

void rxbxQueryShaderPerfDirect(RdrDeviceDX *device, RdrShaderPerformanceValues **params_ptr, WTCmdPacket *packet)
{
	RdrShaderPerformanceValues *params = *params_ptr;
	RxbxPixelShader *pixel_shader = NULL;

	assert(params->shader_type==SPT_FRAGMENT);

	stashIntFindPointer(device->pixel_shaders, params->shader_handle, &pixel_shader);

	if (pixel_shader) {
		params->instruction_count = pixel_shader->instruction_count;
		params->texture_fetch_count = pixel_shader->texture_fetch_count;
		params->temporaries_count = pixel_shader->temporaries_count;
		params->d3d_instruction_slots = pixel_shader->d3d_instruction_slots;
		params->d3d_alu_instruction_slots = pixel_shader->d3d_alu_instruction_slots;
		params->nvps_pps = pixel_shader->nvps_pps;
		if (device->frame_count_xdevice - pixel_shader->pixel_count_frame < 5) {
			params->pixel_count = pixel_shader->pixel_count_last;
		} else {
			params->pixel_count = 0;
		}
		if (pixel_shader->is_error_shader)
			params->instruction_count = -1;
	} else {
		params->instruction_count = -2;
	}
}

void rxbxQueryPerfTimesDirect(RdrDeviceDX *device, RdrDevicePerfTimes **params_ptr, WTCmdPacket *packet)
{
	RdrDevicePerfTimes *params = *params_ptr;
	int timer = timerAlloc();
	RdrScreenPostProcess ppscreen = {0};
	RdrClearParams clear_params = {0};
	TexHandle textures[2];
	Vec4 constants[2];
	float scale=1; // If they happen to be lower than 256x256 resolution

	ppscreen.shader_handle = rxbxGetPerfTestShader(device);
	textures[0] = device->white_tex_handle;
	textures[1] = device->black_tex_handle;
	setVec4(constants[0], 0, 1, 2, 3);
	setVec4(constants[1], 4, 5, 6, 7);
	ppscreen.material.textures = textures;
	ppscreen.material.constants = constants;
	ppscreen.material.drawable_constants = NULL;
	ppscreen.material.tex_count = 2;
	ppscreen.material.const_count = 2;
	ppscreen.material.flags = RMATERIAL_ADDITIVE | RMATERIAL_NOZTEST;
	ppscreen.tex_width = 256;
	ppscreen.tex_height = 256;
	ppscreen.blend_type = RPPBLEND_ADD;
	ppscreen.depth_test_mode = RPPDEPTHTEST_OFF;
	ppscreen.write_depth = 1;
	ppscreen.rdr_internal = 1;

	rxbxResetViewport(device, 256, 256);
	// Not clearing was making it go faster because clearing was resetting the viewport!
	clear_params.bits = CLEAR_ALL;
	rxbxClearActiveSurfaceDirect(device, &clear_params, NULL);
	rxbxResetViewport(device, 256, 256);
	scale = (MIN(device->active_surface->width_thread,256)*MIN(device->active_surface->height_thread,256)) / (256.f*256.f);

	// Run test once, and flush, making sure any stalls are taken care of
	rxbxPostProcessScreenDirect(device, &ppscreen, NULL);
	rxbxFlushGPUDirectEx(device, false);

	// Repeat the test with larger samples until it takes around 20ms, so on
	// AMD CPUs with imprecise clocks we still get a precise result
	{
		int i=-1; // Guaranteed to be set before used, but the compiler is whining
		static int lastN=5;
		int N = lastN;
		int maxN=17000;
		F32 desired_time = 0.020; // 20 ms for 5% precision
		float elapsed=0;

		while (elapsed < desired_time && N<maxN)
		{
			timerStart(timer);
			// Run test more times to get actual timing
			for (i=0; i<N; i++) 
			{
				rxbxPostProcessScreenDirect(device, &ppscreen, NULL);
				if (timerElapsed(timer) > 1.f)
					break;
			}
			rxbxFlushGPUDirectEx(device, false);
			elapsed = timerElapsed(timer);
			lastN = N;
			N*=5;
		}
		if (elapsed == 0)
			elapsed = 0.0001;
		params->pixelShaderFillValue = i / elapsed * scale;
	}

	// TODO: Test object draw/vertex speed
	// TODO: Test depth read speed?

	params->bFilledIn = true;
	timerFree(timer);
}

#if _XBOX
HRESULT SaveUPDBFile(LPD3DXSHADER_COMPILE_PARAMETERSA pParameters)
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
	HRESULT hr = E_FAIL;
    LPD3DXBUFFER pUPDBBuffer = pParameters->pUPDBBuffer;
    DWORD bytesWritten, bytesToWrite;

	// Chack arg.
	if(pParameters == NULL) {
		goto Exit;
	}

	if(pUPDBBuffer == NULL)
	{
		if (!rdr_state.noGenerateUPDBs)
			OutputDebugStringA("ERROR: No UPDB data was generated. UPDB file not saved.\n");
		goto Exit;
	}

	bytesToWrite = pUPDBBuffer->lpVtbl->GetBufferSize( pUPDBBuffer );
	if( bytesToWrite == 0) {
		OutputDebugStringA("ERROR: UPDB data was 0 bytes. UPDB file not saved.\n");
		goto Exit;
	}

	// Create the UPDB file.
	{
		char temppath[MAX_PATH];
		strcpy(temppath, pParameters->UPDBPath);
		// for some reason writing to ShaderDumpxe:\\ sometimes fails when devkit:\\ succeeds.
		strstriReplace(temppath, "ShaderDumpxe:\\", "devkit:\\");
		strstriReplace(temppath, "xe:\\", "devkit:\\");
		makeDirectoriesForFile(temppath);
		hFile = CreateFile(temppath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL, NULL);
	}

    if(INVALID_HANDLE_VALUE == hFile)
    {
		OutputDebugStringA("ERROR: Couldn't create UPDB file.\n");
		goto Exit;
    }

	bytesWritten = 0;
	if(!WriteFile(hFile, pUPDBBuffer->lpVtbl->GetBufferPointer( pUPDBBuffer ), bytesToWrite,
			&bytesWritten, NULL) || bytesWritten != bytesToWrite)
	{
		OutputDebugStringA("ERROR: Couldn't write UPDB file.\n");
		goto Exit;
	}

	// If we made it this far, we need to return S_OK.
	hr = S_OK;

Exit:
	if(hFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hFile);
	}

    return hr;
}
#endif

