#if !_PS3

#include "GfxNVPerf.h"
#include "GraphicsLibPrivate.h"
#include "GfxDebug.h"
#include "RdrState.h"
#include "inputLib.h"
#include "wininclude.h"
#include "../../3rdparty/NVPerfSDK/NVPerfSDK.h"
#include "../../3rdparty/nvperfsdk/nvapi.h"

#ifndef _XBOX
#pragma comment(lib, "NVPerfSDK.lib")
#endif

static struct {
	// bookkeeping:
	bool inited;
	bool inBottleneckExp;
	int pass;
	int numPasses;

	// sampled values:
	bool have_simexp;
} nv_perf_state;

#ifndef _XBOX

typedef struct Counter {
	char *counterName;
	F32 percent;
	U64 value;
	U64 cycles;
} Counter;

Counter counters[] = {
	{"gpu_idle"},
	{"shader_busy"},
	{"vertex_shader_busy"},
	{"shaded_pixel_count"},
};
enum {
	COUNTER_GPU_IDLE,
	COUNTER_PS_UTIL,
	COUNTER_VS_UTIL,
	COUNTER_PIXEL_COUNT,
};

Counter simexp_counters[] = {
	{"GPU Bottleneck"},
	{"2D Bottleneck"},
	{"2D SOL"},
	{"IDX Bottleneck"},
	{"IDX SOL"},
	{"GEOM Bottleneck"},
	{"GEOM SOL"},
	{"ZCULL Bottleneck"},
	{"ZCULL SOL"},
	{"TEX BOTTLENECK"},
	{"TEX SOL"},
	{"ROP BOTTLENECK"},
	{"ROP SOL"},
	{"SHD Bottleneck"},
	{"SHD SOL"},
	{"FB Bottleneck"},
	{"FB SOL"},
};
enum {
	SIMEXP_COUNTER_GPU_BOTTLENECK, // Result of "expert system"
};
static void addCounters(Counter *pcounters, int size)
{
	int i;
	for (i=0; i<size; i++)
	{
		NVPMAddCounterByName(pcounters[i].counterName);
	}
}

static void sampleCounters(Counter *pcounters, int size)
{
	int i;
	for (i=0; i<size; i++) {
		NVPMGetCounterValueByName(pcounters[i].counterName, 0, &pcounters[i].value, &pcounters[i].cycles);
		if (pcounters[i].cycles)
			pcounters[i].percent = pcounters[i].value / (F64)pcounters[i].cycles * 100.f;
		else
			pcounters[i].percent = 0;
	}
}


int MyEnumFunc(UINT unCounterIndex, char *pcCounterName)
{
	char zString[200], zLine[400];
	unsigned int unLen;
	float fValue;

	unLen = 200;
	if(NVPMGetCounterDescription(unCounterIndex, zString, &unLen) == NVPM_OK) {
		sprintf(zLine, "Counter %d [%s] : ", unCounterIndex, zString);
	
		unLen = 200;
		if(NVPMGetCounterName(unCounterIndex, zString, &unLen) == NVPM_OK)
			strcat(zLine, zString); // Append the short name
		else
			strcat(zLine, "Error retrieving short name");

		NVPMGetCounterClockRate(zString, &fValue);
		sprintf(zString, " %.2f\n", fValue);
		strcat(zLine, zString);

//		OutputDebugStringA(zLine);
	}

	return(NVPM_OK);
}


static void gfxNVPerfInit(void)
{
	NVPMRESULT res;
	if (nv_perf_state.inited)
		return;
	nv_perf_state.inited = true;
	res = NVPMInit();

	// Example of how to perform an enumeration
	res = NVPMEnumCounters(MyEnumFunc);

	addCounters(counters, ARRAY_SIZE(counters));
}

#endif

void gfxNVPerfStartFrame(void)
{
#ifndef _XBOX
	if (!gfx_state.debug.runNVPerf)
		return;
	rdr_state.swapBuffersAtEndOfFrame = 1;
	if (gfx_state.debug.runNVPerf != 3)
		gfxNVPerfInit();
	if (gfx_state.debug.runNVPerf == 3)
		rdrFlushGPU(gfx_state.currentDevice->rdr_device);
	if (gfx_state.debug.runNVPerf == 2 && !nv_perf_state.inBottleneckExp) {
		ignorableAssert(rdr_state.swapBuffersAtEndOfFrame);
		// Do a more complicated experiment
		nv_perf_state.inBottleneckExp = true;
		rdrFlushGPU(gfx_state.currentDevice->rdr_device);
		NVPMRemoveAllCounters();
		addCounters(simexp_counters, 1);
		NVPMBeginExperiment(&nv_perf_state.numPasses);
		nv_perf_state.pass = 0;
	}
	if (nv_perf_state.inBottleneckExp) {
		NVPMBeginPass(nv_perf_state.pass);
	}
#endif
}

void gfxNVPerfEndFrame(void)
{
#ifndef _XBOX
	unsigned int nCount;
	if (nv_perf_state.inBottleneckExp) {
		rdrFlushGPU(gfx_state.currentDevice->rdr_device);
		NVPMEndPass(nv_perf_state.pass);
		nv_perf_state.pass++;
		if (nv_perf_state.pass == nv_perf_state.numPasses)
		{
			NVPMEndExperiment();
			//NVPMSample(NULL, &nCount);

			sampleCounters(simexp_counters, ARRAY_SIZE(simexp_counters));
			nv_perf_state.have_simexp = true;
			nv_perf_state.inBottleneckExp = false;
			gfx_state.debug.runNVPerf = 1;

			NVPMRemoveAllCounters();
			addCounters(counters, ARRAY_SIZE(counters));
		}
	} else if (gfx_state.debug.runNVPerf!=3) {
		// General once per frame sampling
		NVPMSample(NULL, &nCount);
		sampleCounters(counters, ARRAY_SIZE(counters));
	}
#endif
}

F32 gfxNVPerfGetGPUIdle(void)
{
#ifndef _XBOX
	return counters[COUNTER_GPU_IDLE].percent;
#else
	return 0;
#endif
}

int gfxNVPerfDisplay(int y)
{
#ifndef _XBOX
	gfxXYprintf(TEXT_JUSTIFY + 50,y++,"      GPU Idle: %4.1f%%", counters[COUNTER_GPU_IDLE].percent);
	gfxXYprintf(TEXT_JUSTIFY + 50,y++,"PS Utilization: %4.1f%% (%4.1fms)", counters[COUNTER_PS_UTIL].percent, counters[COUNTER_PS_UTIL].percent * gfx_state.debug.last_frame_counts.ms / 100.f);
	gfxXYprintf(TEXT_JUSTIFY + 50,y++,"VS Utilization: %4.1f%% (%4.1fms)", counters[COUNTER_VS_UTIL].percent, counters[COUNTER_VS_UTIL].percent * gfx_state.debug.last_frame_counts.ms / 100.f);
	gfxXYprintf(TEXT_JUSTIFY + 50,y++,"   Pixel Count: %"FORM_LL"d", counters[COUNTER_PIXEL_COUNT].value);
	if (nv_perf_state.have_simexp) {
		char buf[100];
		int i;
		y++;
		NVPMGetGPUBottleneckName(simexp_counters[SIMEXP_COUNTER_GPU_BOTTLENECK].value, buf);
		gfxXYprintf(TEXT_JUSTIFY + 49,y++, "GPU Bottleneck: %s", buf);
		for (i=1; i<ARRAY_SIZE(simexp_counters); i++) {
			sprintf(buf, "%16s: %%2.1f%%%%", simexp_counters[i].counterName);
			gfxXYprintf(TEXT_JUSTIFY + 50,y++, FORMAT_OK(buf), (float)simexp_counters[i].value);
		}
	}
#endif
	return y;
}

bool gfxNVPerfContinue(void)
{
#ifndef _XBOX
	if (gfx_state.debug.runNVPerf == 3) {
		if (GetAsyncKeyState(VK_CONTROL) & 0x8000000 &&
			GetAsyncKeyState(VK_SHIFT) & 0x8000000 &&
			GetAsyncKeyState(VK_MENU) & 0x8000000)
		{
			gfx_state.debug.runNVPerf = 0;
		} else {
			inpClearBuffer(gfxGetActiveInputDevice());
			inpUpdateEarly(gfxGetActiveInputDevice());

			return true;
		}
	}
#endif
	if (nv_perf_state.inBottleneckExp)
		return true;
	return false;
}

U32 gfxNVPerfGetGPUMemUsage(void)
{
	if (nv_api_avail)
	{
		NV_DISPLAY_DRIVER_MEMORY_INFO memory_info;
		memory_info.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER_2;
		NvAPI_GPU_GetMemoryInfo(NVAPI_DEFAULT_HANDLE,&memory_info);
		return memory_info.dedicatedVideoMemory - memory_info.curAvailableDedicatedVideoMemory;
	}
	return 0;
}

extern NV_DISPLAY_DRIVER_MEMORY_INFO_V2 nv_startup_memory_info;

U32 gfxNVPerfGetGPUStartupMemUsage(void)
{
	return nv_startup_memory_info.dedicatedVideoMemory - nv_startup_memory_info.curAvailableDedicatedVideoMemory;
}


#endif
