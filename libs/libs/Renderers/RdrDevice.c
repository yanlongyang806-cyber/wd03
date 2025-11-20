#include "RdrDevice.h"
#include "RdrTexture.h"
#include "RdrState.h"
#include "earray.h"
#include "winutil.h"
#include "MemRef.h"
#include "Prefs.h"
#include "timing.h"
#include "MemoryMonitor.h"
#include "file.h"
#include "EventTimingLog.h"
#include "memlog.h"
#include "MemAlloc.h"
#include "LinearAllocator.h"
#include "RdrDrawListPrivate.h"

#include "RenderLib.h"
#include "rt_winutil.h"
#include "RdrDeviceTrace.h"

#include "AutoGen/RdrDevice_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:RenderThread", BUDGET_Renderer););

#define MEMORYUSE_OWNER_ID 0xD13C

F32 sprite_histogram_sizes[RDR_SPRITE_SIZE_BUCKET_COUNT] = 
{
	4, // RDR_SPRITE_SIZE_TINY 
	10, // RDR_SPRITE_SIZE_SMALL
	100, // RDR_SPRITE_SIZE_MEDIUM_SMALL = 10x10
	1024, // RDR_SPRITE_SIZE_MEDIUM = 32x32
	4096, // RDR_SPRITE_SIZE_MEDIUM_LARGE = 64x64
	16384, // RDR_SPRITE_SIZE_LARGE = 128x128
	FLT_MAX, // RDR_SPRITE_SIZE_HUGE
};

static void rdrUserMemoryDisableThreadedAssert(void);

#undef rdrMemlogPrintf
void rdrMemlogPrintf(RdrDevice *device, MemLog *memlog, FORMAT_STR const char *fmt, ...)
{
	va_list va;
	RdrDeviceMemlogParams *params = wtAllocCmd(device->worker_thread, RDRCMD_MEMLOG, sizeof(RdrDeviceMemlogParams));
	va_start(va, fmt);
	vsprintf(params->buffer, fmt, va);
	va_end(va);
	params->memlog = memlog;
	wtSendCmd(device->worker_thread);
}

static int lock_inited = 0;
static CRITICAL_SECTION lock_critical_section;
static RdrDevice *lock_current_device = 0;

static RdrDeviceInfo **rdr_device_infos;
RdrDeviceEnumFunc *rdr_device_enum_funcs;
void rdrDeviceAddEnumFunc(RdrDeviceEnumFunc enumFunc)
{
	eaPush((cEArrayHandle *)&rdr_device_enum_funcs, (RdrDeviceEnumFunc *)enumFunc);
}

const RdrDeviceInfo * const * rdrEnumerateDevices(void)
{
	if (!eaSize(&rdr_device_infos))
	{
		int i;
		assertmsg(eaSizeUnsafe(&rdr_device_enum_funcs), "No renderers registered device enum funcs");
		for (i=0; i<eaSizeUnsafe(&rdr_device_enum_funcs); i++) 
		{
			rdr_device_enum_funcs[i](&rdr_device_infos);
		}
		assertmsg(eaSize(&rdr_device_infos), "No devices found");
	}
	return rdr_device_infos;
}

int rdrGetDeviceForMonitor(int preferred_monitor, const char * device_type)
{
	const RdrDeviceInfo * const * device_infos = NULL;
	int preferred_adapter;
	device_infos = rdrEnumerateDevices();
	for (preferred_adapter = 0; preferred_adapter < eaSize(&device_infos); ++preferred_adapter)
	{
		const RdrDeviceInfo * device_info = device_infos[preferred_adapter];
		if (stricmp(device_info->type, device_type)==0)
		{
			if (device_info->monitor_index == preferred_monitor)
				return preferred_adapter;
		}
	}

	// must use default of zero
	return 0;
}

RdrDevice *rdrCreateDevice(WindowCreateParams *params, HINSTANCE hInstance, int processor_idx)
{
	const char *type = params->device_type;
	const RdrDeviceInfo * const * device_infos = rdrEnumerateDevices();
	assert(eaSize(&device_infos));
	FOR_EACH_IN_EARRAY(device_infos, const RdrDeviceInfo, device_info)
	{
		if (!type || stricmp(device_info->type, type)==0) {
			return device_info->createFunction(params, hInstance, processor_idx);
		}
	}
	FOR_EACH_END;
	Errorf("Could not find device type \"%s\", using default.", type);
	return device_infos[0]->createFunction(params, hInstance, processor_idx);
}


__forceinline static void rdrInitLock(void)
{
	if (!lock_inited)
	{
		InitializeCriticalSection(&lock_critical_section);
		lock_inited = 1;
	}
}

static void rdrLockDeviceDirect(RdrDevice *device, void *unused, WTCmdPacket *packet)
{
	EnterCriticalSection(&lock_critical_section);
	etlAddEvent(device->event_timer, "Lock", ELT_RESOURCE, ELTT_BEGIN);
	assert(!lock_current_device);
	assert(!device->is_locked_thread);
	lock_current_device = device;
	device->is_locked_thread = 1;
}

static void rdrUnlockDeviceDirect(RdrDevice *device, void *unused, WTCmdPacket *packet)
{
	assert(lock_current_device==device);
	assert(device->is_locked_thread);
	device->is_locked_thread = 0;
	lock_current_device = 0;
	etlAddEvent(device->event_timer, "Lock", ELT_RESOURCE, ELTT_END);
	LeaveCriticalSection(&lock_critical_section);
}

static void rdrAlertMsgDirect(RdrDevice *device, AlertErrorMsg *msg, WTCmdPacket *packet)
{
	if (msg->str)
		msgAlert(rdrGetWindowHandle(device), msg->str);
	SAFE_FREE(msg->str);
	SAFE_FREE(msg->title);
	SAFE_FREE(msg->fault);
}

static void rdrErrorMsgDirect(RdrDevice *device, AlertErrorMsg *msg, WTCmdPacket *packet)
{
	if (msg->str)
		errorDialog(rdrGetWindowHandle(device), msg->str, msg->title, msg->fault, msg->highlight);
	SAFE_FREE(msg->str);
	SAFE_FREE(msg->title);
	SAFE_FREE(msg->fault);
}

static void rdrDisplayParamsDirect(RdrDevice *device, DisplayParams *display_thread, WTCmdPacket *packet)
{
	if (memcmp(display_thread,&device->display_nonthread,sizeof(DisplayParams)))
		rdrDisplayParamsPreChange(device, display_thread);

	device->display_nonthread = *display_thread;

}

static void rdrStatusPrintfMsgHandlerDirect(RdrDevice *device, const char * message, WTCmdPacket *packet)
{
	rdrStatusPrintf("%s", message);
}

static void rdrCmdFenceCompleteDirect(RdrDevice *device, RdrCmdFenceData *fenceData, WTCmdPacket *packet)
{
	// DJR logging these events for initial testing
	memlog_printf(NULL, "Rdr Fence MT callback: Fence=%p Name=%s\n", fenceData, fenceData->name);
	if (fenceData->mainThreadCmdCompleteCB)
		fenceData->mainThreadCmdCompleteCB(device, fenceData);
}

int rdrStatusPrintfFromDeviceThread(RdrDevice *device, const char *format, ...)
{
	int result;
	char outbuf[1024];
	va_list argptr;

	PERFINFO_AUTO_START("rdrDeviceStatusPrintfFromThread", 1);
	va_start(argptr, format);

	result = vsprintf(outbuf, format, argptr);

	va_end(argptr);
	PERFINFO_AUTO_STOP();

	wtQueueMsg(device->worker_thread, RDRMSG_STATUS_PRINTF, outbuf, result + 1);

	return result;
}


static void rdrFreeDataDirect(RdrDevice *device, void *data, WTCmdPacket *packet)
{
	free(*(void **)data);
}

static void rdrFreeMemRefDataDirect(RdrDevice *device, void *data, WTCmdPacket *packet)
{
	memrefDecrement(*(void **)data);
}

static void rdrHandleUserMsg(RdrDevice *device, int msg_type, void *data, WTCmdPacket *packet)
{
	if (device->user_msg_handler)
		device->user_msg_handler(device, device->user_data, msg_type, data, packet);
}

static void rdrHandleCmd(void *user_data, int cmd_type, void *data, WTCmdPacket *packet)
{
    assert(!"invalid command");
}

//////////////////////////////////////////////////////////////////////////

static void rdrUnsupportedFunction(void)
{
	assertmsg(0, "Unsupported function called!");
}

void rdrInitDevice(RdrDevice *device, bool threaded, int processor_idx)
{
	static int renderer_idx;
	char buffer[1024];
	int i;

	rdrStartup();

	ZeroStruct(device);
	assert(device);

	device->frame_done_signal = CreateEvent(0,0,0,0);
	InitializeCriticalSection(&device->frame_check);

	device->device = device;

	sprintf(buffer, "Renderer %d", renderer_idx++);
	device->event_timer = etlCreateEventOwner(buffer, "RdrDevice", "RenderLib");

	// the message queue needs to be big enough to fit the sprite data so there are no per-frame allocations
	device->worker_thread = wtCreate(1<<17, 1<<16, device, "RenderThread");
	device->nonthread_cursor_visible = true;
	device->nonthread_cursor_restrict_to_window = false;
	device->thread_cursor_visible = true;
	device->thread_cursor_restrict_to_window = false;

#if _PS3 && _DEBUG
    wtSetDefaultCmdDispatch(device->worker_thread, rdrHandleCmd);
#endif
	wtSetDefaultMsgDispatch(device->worker_thread, rdrHandleUserMsg);

	wtRegisterCmdDispatch(device->worker_thread, RDRCMD_LOCKACTIVE, rdrLockDeviceDirect);
	wtRegisterCmdDispatch(device->worker_thread, RDRCMD_UNLOCKACTIVE, rdrUnlockDeviceDirect);
	wtRegisterCmdDispatch(device->worker_thread, RDRCMD_FREEDATA, rdrFreeDataDirect);
	wtRegisterCmdDispatch(device->worker_thread, RDRCMD_FREEMEMREFDATA, rdrFreeMemRefDataDirect);
	wtRegisterCmdDispatch(device->worker_thread, RDRCMD_WIN_SETTITLE, rwinSetTitleDirect);
	wtRegisterCmdDispatch(device->worker_thread, RDRCMD_WIN_SETSIZE, rwinSetSizeDirect);
	wtRegisterCmdDispatch(device->worker_thread, RDRCMD_WIN_SETPOSANDSIZE, rwinSetPosAndSizeDirect);
	wtRegisterCmdDispatch(device->worker_thread, RDRCMD_WIN_SETICON, rwinSetIconDirect);
	//wtRegisterCmdDispatch(device->worker_thread, RDRCMD_WIN_SHOW, rwinShowDirect);
	wtRegisterCmdDispatch(device->worker_thread, RDRCMD_SHELL_EXECUTE, rwinShellExecuteDirect);

	wtRegisterMsgDispatch(device->worker_thread, RDRMSG_ALERTMSG, rdrAlertMsgDirect);
	wtRegisterMsgDispatch(device->worker_thread, RDRMSG_ERRORMSG, rdrErrorMsgDirect);
	wtRegisterMsgDispatch(device->worker_thread, RDRMSG_DISPLAYPARAMS, rdrDisplayParamsDirect);
	wtRegisterMsgDispatch(device->worker_thread, RDRMSG_STATUS_PRINTF, rdrStatusPrintfMsgHandlerDirect);
	wtRegisterMsgDispatch(device->worker_thread, RDRMSG_CMDFENCECOMPLETE, rdrCmdFenceCompleteDirect);

	wtSetProcessor(device->worker_thread, processor_idx);
	wtSetThreaded(device->worker_thread, threaded, 0, true);
	wtStartEx(device->worker_thread, 64*1024);

	for (i = 0; i < ARRAY_SIZE(device->device_functions_array); i++) {
		device->device_functions_array[i] = rdrUnsupportedFunction;
	}
}

void rdrSyncDevice(RdrDevice *device)
{
    // presumably you dont have it locked
    assert(!device->is_locked_nonthread);

    wtFlush(device->worker_thread);
    wtMonitorAndSleep(device->worker_thread);
}

void rdrUninitDevice(RdrDevice *device)
{
	wtFlush(device->worker_thread);
	if (device->is_locked_nonthread)
		rdrUnlockActiveDevice(device, true, true, true);
	wtFlush(device->worker_thread);
	wtMonitor(device->worker_thread);
	wtDestroy(device->worker_thread);

#if _PS3
    DestroyEvent(device->frame_done_signal);
#else
	CloseHandle(device->frame_done_signal);
#endif
	DeleteCriticalSection(&device->frame_check);

	etlFreeEventOwner(device->event_timer);

	ZeroStruct(device);

	rdrUserMemoryDisableThreadedAssert();
}

void rdrSetMsgHandler(RdrDevice *device, RdrUserMsgHandler msg_handler, RdrWinMsgHandler winmsg_handler, void *userdata)
{
	wtFlush(device->worker_thread);
	wtMonitor(device->worker_thread);
	device->user_msg_handler = msg_handler;
	device->user_data = userdata;
}

void rdrInitSurface(RdrDevice *device, SA_PRE_NN_FREE SA_POST_NN_VALID RdrSurface *surface)
{
	int i;

	ZeroStruct(surface);

	surface->device = device;
	surface->surface = surface;

	for (i = 0; i < ARRAY_SIZE(surface->surface_functions_array); i++)
		surface->surface_functions_array[i] = rdrUnsupportedFunction;
}

//////////////////////////////////////////////////////////////////////////
// lock and unlock

void rdrLockActiveDevice(RdrDevice *device, bool do_begin_scene)
{
	RdrSurfaceActivateParams activate_params = { 0 };
	RdrDeviceBeginSceneParams begin_scene_params = { 0 };
	int i;
	bool inactive;

	PERFINFO_AUTO_START_FUNC();
	etlAddEvent(device->event_timer, __FUNCTION__, ELT_CODE, ELTT_BEGIN);

	if (rdr_state.max_frames_ahead <= 0)
		rdr_state.max_frames_ahead = 1; // Default value

	if (!rdr_state.swapBuffersAtEndOfFrame)
		// This is done here to match the prior increment behavior in the render thread, when
		// Present is done right before BeginScene (the start of the new frame)
		++device->frame_count_nonthread;
	TRACE_FRAME_MT(device, "MT dev lock start\n");

	assert(!device->is_locked_nonthread);
	device->is_locked_nonthread = 1;
	rdrInitLock();
	wtQueueCmd(device->worker_thread, RDRCMD_LOCKACTIVE, 0, 0);

	
	inactive = rdrIsDeviceInactive(device);
	
	begin_scene_params.do_begin_scene = do_begin_scene;
	begin_scene_params.frame_count_update = device->frame_count_nonthread;
	wtQueueCmd(device->worker_thread, RDRCMD_BEGINSCENE, &begin_scene_params, sizeof(begin_scene_params));

	// Note: this is dangerous because commands may be sent out of order.
	//   I fixed the surface destroy command to get queued as well if it already
	//   has something else queued up, which should prevent this from causing a crash.
	// LDM: this must be done after BEGINSCENE since it can create new surfaces and clear them which on XBOX will mess up the backbuffer we were about to present.
	if (!inactive)
	{
		for (i = 0; i < eaSize(&device->queued_commands); ++i)
		{
			void *data_ptr = wtAllocCmd(device->worker_thread, device->queued_commands[i]->cmd, device->queued_commands[i]->data_size);
			if (device->queued_commands[i]->data_size)
				memcpy(data_ptr, (void *)(device->queued_commands[i] + 1), device->queued_commands[i]->data_size);
			wtSendCmd(device->worker_thread);
		}
		eaClearEx(&device->queued_commands, NULL);
	}

	if (!device->nonthread_active_surface)
		device->nonthread_active_surface = rdrGetPrimarySurface(device);
	activate_params.surface = device->nonthread_active_surface;
	wtQueueCmd(device->worker_thread, RDRCMD_SURFACE_SETACTIVE, &activate_params, sizeof(activate_params));
	wtQueueCmd(device->worker_thread, RDRCMD_SURFACE_RESETACTIVESTATE, 0, 0);
	TRACE_FRAME_MT(device, "MT dev lock done\n");

	etlAddEvent(device->event_timer, __FUNCTION__, ELT_CODE, ELTT_END);
	PERFINFO_AUTO_STOP();
}

bool rdrUnlockActiveDevice(RdrDevice *device, bool do_xlive_callback, bool do_end_scene, bool do_buffer_swap)
{
	bool bDidWait = false;
	RdrSurfaceActivateParams activate_params = { 0 };
	RdrDeviceEndSceneParams end_scene_params = { 0 };

	PERFINFO_AUTO_START_FUNC();

	assert(device->is_locked_nonthread);

	if (do_buffer_swap && wtIsThreaded(device->worker_thread))
	{
		int first=1;

		PERFINFO_AUTO_START("while(frames_buffered)",1);
			etlAddEvent(device->event_timer, "Wait for thread", ELT_CODE, ELTT_BEGIN);
			while (device->frames_buffered >= rdr_state.max_frames_ahead)
			{
				bDidWait = true;
				if (!wtIsDebug(device->worker_thread)) // If running with -renderthreadDebug, the render thread will only process while we're in wtMonitorAndSleep().
					WaitForEvent(device->frame_done_signal, 100);

				if (!first && device->frames_buffered >= rdr_state.max_frames_ahead) {
					wtMonitorAndSleep(device->worker_thread);
				} else {
					wtMonitor(device->worker_thread);
				}
				first = 0;
			}
			etlAddEvent(device->event_timer, "Wait for thread", ELT_CODE, ELTT_END);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("increment frames_buffered",1);
			EnterCriticalSection(&device->frame_check);
			device->frames_buffered++;
			LeaveCriticalSection(&device->frame_check);
		PERFINFO_AUTO_STOP();
	}

	end_scene_params.do_xlive_callback = do_xlive_callback;
	end_scene_params.do_end_scene = do_end_scene;
	end_scene_params.do_buffer_swap = do_buffer_swap;
	end_scene_params.frame_count_update = device->frame_count_nonthread;

	TRACE_FRAME_MT(device, "MT dev unlock start\n");

	if (rdr_state.swapBuffersAtEndOfFrame)
		// This is done here to match the prior increment behavior in the render thread, when
		// Present is done right after Present (the end of the current frame)
		++device->frame_count_nonthread;

	wtQueueCmd(device->worker_thread, RDRCMD_ENDSCENE, &end_scene_params, sizeof(end_scene_params));
	wtQueueCmd(device->worker_thread, RDRCMD_SURFACE_SETACTIVE, &activate_params, sizeof(activate_params));
	device->is_locked_nonthread = 0;
	wtQueueCmd(device->worker_thread, RDRCMD_UNLOCKACTIVE, 0, 0);
	TRACE_FRAME_MT(device, "MT dev unlock done\n");

	PERFINFO_AUTO_STOP();
	return bDidWait;
}

//////////////////////////////////////////////////////////////////////////
// surfaces

void rdrAllocBufferForSurfaceData(RdrSurfaceData *params)
{
	int bpp = 1;
	SAFE_FREE(params->data);
	switch (params->type)
	{
		case SURFDATA_RGB:
			bpp = 3 * sizeof(U8);
			break;
		case SURFDATA_RGBA:
		case SURFDATA_BGRA:
			bpp = 4 * sizeof(U8);
			break;
		case SURFDATA_DEPTH:
			bpp = sizeof(F32);
			break;
		case SURFDATA_STENCIL:
			bpp = sizeof(U32);
			break;
		case SURFDATA_RGB_F32:
			bpp = 3 * sizeof(F32);
			break;
		default:
			devassert(0);
	}
	params->data = malloc(params->width * params->height * bpp);
}

//////////////////////////////////////////////////////////////////////
// error and alert messages

void rdrAlertMsg(RdrDevice *device, const char *str)
{
	AlertErrorMsg msg={0};
	if (!str)
		return;
	msg.str = strdup(str);
	memlog_printf(NULL, "rdrAlertMessage %s", msg.str);
	if (!device)
		device = lock_current_device;
// Queuing this to the main thread causes the win32 error dialog to not pop up because the wrong thread has the window handle, etc
// Just call it directly here instead
// 	if (device)
// 		wtQueueMsg(device->worker_thread, RDRMSG_ALERTMSG, &msg, sizeof(msg));
	rdrAlertMsgDirect(device, &msg, NULL);
}

void rdrErrorMsg(RdrDevice *device, char *str, char* title, char* fault, int highlight)
{
	AlertErrorMsg msg={0};
	msg.str = str?strdup(str):0;
	msg.title = title?strdup(title):0;
	msg.fault = fault?strdup(fault):0;
	msg.highlight = highlight;
	if (!device)
		device = lock_current_device;
	if (device)
		wtQueueMsg(device->worker_thread, RDRMSG_ERRORMSG, &msg, sizeof(msg));
}

void rdrSafeAlertMsg(const char *str)
{
	// Read this variable once because the render thread may have a queued unlock
	RdrDevice *local_copy_of_locked_device = lock_current_device;
	if (local_copy_of_locked_device && wtInWorkerThread(local_copy_of_locked_device->worker_thread))
		rdrAlertMsg(local_copy_of_locked_device, str);
	else
		msgAlert(0, str);
}

void rdrSafeErrorMsg(char *str, char *title, char *fault, int highlight)
{
	// Read this variable once because the render thread may have a queued unlock
	RdrDevice *local_copy_of_locked_device = lock_current_device;
	if (local_copy_of_locked_device && wtInWorkerThread(local_copy_of_locked_device->worker_thread))
		rdrErrorMsg(local_copy_of_locked_device, str, title, fault, highlight);
	else
		errorDialog(0, str, title, fault, highlight);
}

void rdrGetDeviceSize(RdrDevice * device, int * xpos, int * ypos, int * width, int * height, int * refresh_rate, int * fullscreen, int * maximized, int * windowed_fullscreen)
{
	DisplayParams params;
	device->getSize(device, &params);

	if (xpos)
		*xpos = params.xpos;
	if (ypos)
		*ypos = params.ypos;
	if (width)
		*width = params.width;
	if (height)
		*height = params.height;
	if (refresh_rate)
		*refresh_rate = params.refreshRate;
	if (fullscreen)
		*fullscreen = params.fullscreen;
	if (maximized)
		*maximized = params.maximize;
	if (windowed_fullscreen)
		*windowed_fullscreen = params.windowed_fullscreen;
}

void rdrGetDeviceSizeEx(RdrDevice *device, DisplayParams *params)
{
	device->getSize(device, params);
}



void rdrGetRememberedParams(const char *window_name, DisplayParams *params)
{
	char buf[1024];
#define GETPARAM(param)							\
	sprintf(buf, "%s\\" #param, window_name);	\
	params->param = GamePrefGetInt(buf, params->param);
	GETPARAM(fullscreen);
	GETPARAM(maximize);
	GETPARAM(windowed_fullscreen);
	GETPARAM(refreshRate);
	GETPARAM(width);
	GETPARAM(height);
	GETPARAM(xpos);
	GETPARAM(ypos);
#undef GETPARAM
}

void rdrSetRememberedParams(const char *window_name, RdrDevice *device)
{
	static struct {
		const RdrDevice *device;
		DisplayParams params;
	} cache[4] = {0}, temp;
	int i;
	DisplayParams params={0};

	for (i=0; i<ARRAY_SIZE(cache); i++) {
		if (cache[i].device == device) {
			// In cache
			// Move to front
			temp = cache[i];
			memmove(&cache[1], &cache[0], i*sizeof(cache[0]));
			cache[0] = temp;
			break;
		}
	}
	if (i==ARRAY_SIZE(cache)) {
		// Not in cache, put in front
		memmove(&cache[1], &cache[0], (ARRAY_SIZE(cache)-1)*sizeof(cache[0]));
	}
	rdrGetDeviceSizeEx(device, &params);
	if (cache[0].device != device ||
		0!=memcmp(&cache[0].params, &params, sizeof(params)))
	{
		char buf[1024];
		cache[0].device = device;
		cache[0].params = params;
#define SETPARAM(param)											\
		sprintf(buf, "%s\\" #param, window_name);	\
		GamePrefStoreInt(buf, params.param);
		SETPARAM(fullscreen);
		SETPARAM(maximize);
		SETPARAM(windowed_fullscreen);
		SETPARAM(refreshRate);
		SETPARAM(width);
		SETPARAM(height);
		SETPARAM(xpos);
		SETPARAM(ypos);
#undef SETPARAM
	}
}


//////////////////////////////////////////////////////////////////////////
// debugging functions

void rdrCheckThread(void)
{
	assert(lock_current_device);
	wtAssertWorkerThread(lock_current_device->worker_thread);
}

void rdrCheckNotThread(void)
{
	assert(lock_current_device);
	wtAssertNotWorkerThread(lock_current_device->worker_thread);
}

static RdrDevice *single_thread_track_device;
static void rdrUserMemoryDisableThreadedAssert(void)
{
	single_thread_track_device = (RdrDevice*)-1;
}

void rdrTrackUserMemoryDirect(RdrDevice *device, const char *moduleName, int staticModuleName, intptr_t memTrafficDelta)
{
	if (isDevelopmentMode())
	{
		if (!single_thread_track_device)
			single_thread_track_device = device;
		if (device == single_thread_track_device)
		{
			ASSERT_CALLED_IN_SINGLE_THREAD;
		}
	}
	assert(staticModuleName);
	assert(moduleName && moduleName != (char*)0x00000001); // Tracking down crash on close
	if (!memTrafficDelta)
		return;
	memMonitorTrackUserMemory(moduleName, staticModuleName, memTrafficDelta, SIGN(memTrafficDelta));
	FOR_EACH_IN_EARRAY(device->memory_usage, RdrDeviceMemoryUsage, memory_usage)
	{
		assert(memory_usage->staticName && memory_usage->staticName != (char*)0x00000001); // If this goes off, we must have have run-time corruption
		if (memory_usage->staticName == moduleName) {
			if (memTrafficDelta<0)
				memory_usage->count--;
			else
				memory_usage->count++;
			memory_usage->value += memTrafficDelta;
			return;
		}
	}
	FOR_EACH_END;
	{
		int index;
		RdrDeviceMemoryUsage *memory_usage = calloc(sizeof(*memory_usage),1);
		//smallAllocSetOwnerID(memory_usage, MEMORYUSE_OWNER_ID);
		devassert(memTrafficDelta > 0); // New string, but it's a free?
		memory_usage->count = 1;
		memory_usage->value = memTrafficDelta;
		memory_usage->staticName = moduleName;
		index = eaPush(&device->memory_usage, memory_usage);
		memlog_printf(NULL, "rdrTrackUserMemoryDirect(): Added %s (0x%p) at index %d; debug: %p %d %d %s  %s", moduleName, moduleName, index,
			memory_usage, memory_usage->count, memory_usage->value, memory_usage->staticName, device->memory_usage[index]->staticName);
	}
}

void rdrUntrackAllUserMemoryDirect(RdrDevice *device)
{
	memlog_printf(NULL, "rdrUntrackAllUserMemoryDirect()");
	FOR_EACH_IN_EARRAY(device->memory_usage, RdrDeviceMemoryUsage, memory_usage)
	{
		assert(memory_usage->staticName && memory_usage->staticName != (char*)0x00000001); // If this assert goes off, likely corruption while shutting down?
		while (memory_usage->count) {
			memory_usage->count--;
			memMonitorTrackUserMemory(memory_usage->staticName, 1, -(S32)memory_usage->value, MM_FREE);
			memory_usage->value = 0;
		}
		//smallAllocClearOwnerID(memory_usage, MEMORYUSE_OWNER_ID);
		free(memory_usage);
	}
	FOR_EACH_END;
	eaDestroy(&device->memory_usage);
}

void rdrDrawQuad(RdrDevice *device, RdrQuadDrawable *quad_src)
{
	TexHandle *texptr;
	Vec4 *constptr;
	RdrPerDrawableConstantMapping *mapptr;
	RdrQuadDrawable *quad = (RdrQuadDrawable*)wtAllocCmd(device->worker_thread, RDRCMD_DRAWQUAD,
		sizeof(*quad) +
		quad_src->material.tex_count * sizeof(TexHandle) +
		quad_src->material.const_count * sizeof(Vec4) + 
		quad_src->material.drawable_const_count * sizeof(RdrPerDrawableConstantMapping));
	CopyStructs(quad, quad_src, 1);
	texptr = (TexHandle*)(quad + 1);
	constptr = (Vec4*)(((U8*)texptr) + quad_src->material.tex_count * sizeof(TexHandle));
	mapptr = (RdrPerDrawableConstantMapping*)(((U8*)constptr) + quad_src->material.const_count * sizeof(Vec4));
	rdrCopyMaterial(&quad->material, &quad_src->material, texptr, constptr, mapptr);
	wtSendCmd(device->worker_thread);
}

__forceinline static int getLightShaderDataSize(const RdrLightShaderData *shader_data)
{
	return shader_data->const_count * sizeof(Vec4) + shader_data->tex_count * sizeof(TexHandle);
}

__forceinline static U8 *copyLightShaderData(RdrLightShaderData *shader_data_dst, const RdrLightShaderData *shader_data_src, U8 *dataptr)
{
	if (shader_data_src->const_count)
	{
		shader_data_dst->constants = (Vec4*)dataptr;
		dataptr += shader_data_src->const_count * sizeof(Vec4);
		memcpy(shader_data_dst->constants, shader_data_src->constants, shader_data_src->const_count * sizeof(Vec4));
	}

	if (shader_data_src->tex_count)
	{
		shader_data_dst->tex_handles = (TexHandle*)dataptr;
		dataptr += shader_data_src->tex_count * sizeof(TexHandle);
		memcpy(shader_data_dst->tex_handles, shader_data_src->tex_handles, shader_data_src->tex_count * sizeof(TexHandle));
	}

	return dataptr;
}

void rdrPostProcessScreen(RdrDevice *device, RdrScreenPostProcess *ppscreen_src)
{
	Vec2 dest_top_left     = {0, 1};
	Vec2 dest_bottom_right = {1, 0};
	rdrPostProcessScreenPart(device, ppscreen_src, dest_top_left, dest_bottom_right);
}

void rdrPostProcessScreenPart(RdrDevice *device, RdrScreenPostProcess *ppscreen_src, Vec2 dest_top_left, Vec2 dest_bottom_right)
{
	TexHandle *texptr;
	Vec4 *constptr;
	RdrPerDrawableConstantMapping *mapptr;
	RdrScreenPostProcess *ppscreen;
	U8 *dataptr;

	int i, j, size = sizeof(*ppscreen) +
				ppscreen_src->material.tex_count * sizeof(TexHandle) +
				ppscreen_src->material.const_count * sizeof(Vec4) +
				ppscreen_src->material.drawable_const_count * sizeof(RdrPerDrawableConstantMapping); 

	for (i = 0; i < MAX_NUM_OBJECT_LIGHTS; ++i)
	{
		RdrLightData *light_data_src = ppscreen_src->lights[i];
		if (light_data_src)
		{
			size += sizeof(*light_data_src);

			for (j = 0; j < RLCT_COUNT; ++j)
			{
				size += getLightShaderDataSize(&light_data_src->normal[j]);
				size += getLightShaderDataSize(&light_data_src->simple[j]);
			}

			size += getLightShaderDataSize(&light_data_src->shadow_test);
		}
	}
	
	dataptr = wtAllocCmd(device->worker_thread, RDRCMD_POSTPROCESSSCREEN, size);

	ppscreen = (RdrScreenPostProcess*)dataptr;
	dataptr += sizeof(*ppscreen);
	CopyStructs(ppscreen, ppscreen_src, 1);
	copyVec2(dest_top_left,     ppscreen->dest_quad[0]);
	copyVec2(dest_bottom_right, ppscreen->dest_quad[1]);

	texptr = (TexHandle*)dataptr;
	dataptr += ppscreen_src->material.tex_count * sizeof(TexHandle);

	constptr = (Vec4*)dataptr;
	dataptr += ppscreen_src->material.const_count * sizeof(Vec4);

	mapptr = (RdrPerDrawableConstantMapping*)dataptr;
	dataptr += ppscreen_src->material.drawable_const_count * sizeof(RdrPerDrawableConstantMapping);
	rdrCopyMaterial(&ppscreen->material, &ppscreen_src->material, texptr, constptr, mapptr);

	for (i = 0; i < MAX_NUM_OBJECT_LIGHTS; ++i)
	{
		RdrLightData *light_data_src = ppscreen_src->lights[i];
		if (light_data_src)
		{
			RdrLightData *light_data = (RdrLightData*)dataptr;
			dataptr += sizeof(*light_data);
			CopyStructs(light_data, light_data_src, 1);
			ppscreen->lights[i] = light_data;

			for (j = 0; j < RLCT_COUNT; ++j)
			{
				dataptr = copyLightShaderData(&light_data->normal[j], &light_data_src->normal[j], dataptr);
				dataptr = copyLightShaderData(&light_data->simple[j], &light_data_src->simple[j], dataptr);
			}

			dataptr = copyLightShaderData(&light_data->shadow_test, &light_data_src->shadow_test, dataptr);
		}
	}

	wtSendCmd(device->worker_thread);
}

void rdrPostProcessShape(RdrDevice *device, RdrShapePostProcess *ppshape_src)
{
	TexHandle *texptr;
	Vec4 *constptr;
	RdrPerDrawableConstantMapping *mapptr;
	RdrShapePostProcess *ppshape;
	U8 *dataptr;
	int i, j, size = sizeof(*ppshape) +
				ppshape_src->material.tex_count * sizeof(TexHandle) +
				ppshape_src->material.const_count * sizeof(Vec4) +
				ppshape_src->material.drawable_const_count * sizeof(RdrPerDrawableConstantMapping);
	
	for (i = 0; i < MAX_NUM_OBJECT_LIGHTS; ++i)
	{
		RdrLightData *light_data_src = ppshape_src->lights[i];
		if (light_data_src)
		{
			size += sizeof(*light_data_src);

			for (j = 0; j < RLCT_COUNT; ++j)
			{
				size += getLightShaderDataSize(&light_data_src->normal[j]);
				size += getLightShaderDataSize(&light_data_src->simple[j]);
			}

			size += getLightShaderDataSize(&light_data_src->shadow_test);
		}
	}

	dataptr = wtAllocCmd(device->worker_thread, RDRCMD_POSTPROCESSSHAPE, size);

	ppshape = (RdrShapePostProcess*)dataptr;
	dataptr += sizeof(*ppshape);
	CopyStructs(ppshape, ppshape_src, 1);

	texptr = (TexHandle*)dataptr;
	dataptr += ppshape_src->material.tex_count * sizeof(TexHandle);

	constptr = (Vec4*)dataptr;
	dataptr += ppshape_src->material.const_count * sizeof(Vec4);

	mapptr = (RdrPerDrawableConstantMapping*)dataptr;
	dataptr += ppshape_src->material.drawable_const_count * sizeof(RdrPerDrawableConstantMapping);
	rdrCopyMaterial(&ppshape->material, &ppshape_src->material, texptr, constptr, mapptr);

	for (i = 0; i < MAX_NUM_OBJECT_LIGHTS; ++i)
	{
		RdrLightData *light_data_src = ppshape_src->lights[i];
		if (light_data_src)
		{
			RdrLightData *light_data = (RdrLightData*)dataptr;
			dataptr += sizeof(*light_data);
			CopyStructs(light_data, light_data_src, 1);
			ppshape->lights[i] = light_data;

			for (j = 0; j < RLCT_COUNT; ++j)
			{
				dataptr = copyLightShaderData(&light_data->normal[j], &light_data_src->normal[j], dataptr);
				dataptr = copyLightShaderData(&light_data->simple[j], &light_data_src->simple[j], dataptr);
			}

			dataptr = copyLightShaderData(&light_data->shadow_test, &light_data_src->shadow_test, dataptr);
		}
	}

	wtSendCmd(device->worker_thread);
}

//These versions allocate the command at the start but use memory in the command buffer so you have to call them one after the other
RdrSpritesPkg *rdrStartDrawSpritesImmediate(RdrDevice *device, U32 array_size, int screen_width, int screen_height)
{
	// make sure the vertices are aligned, as they will be memcopied later (a better allocator would save us from some sloppy allocating here
	size_t iAllocSize = sizeof(RdrSpritesPkg) + sizeof(RdrSpriteState)*array_size + sizeof(RdrSpriteVertex)*array_size*4 + 0x10;
	RdrSpritesPkg *pkg = wtAllocCmd(device->worker_thread, RDRCMD_DRAWSPRITES, (int)iAllocSize);
	memset(pkg, 0, iAllocSize);  // why?
	pkg->states = (RdrSpriteState*)(pkg+1);
	pkg->vertices = (RdrSpriteVertex*)AlignPointerUpPow2(pkg->states + array_size,16);
	pkg->state_array_size = array_size;
	pkg->sprite_count = 0;
	pkg->indices = 0;
	pkg->indices32 = 0;
	pkg->screen_width = screen_width;
	pkg->screen_height = screen_height;
	pkg->should_free_contiguous_block = false;
	pkg->should_free_states = false;
	pkg->should_free_vertices = false;
	pkg->should_free_indices = false;
	pkg->should_free_pkg_on_send = false;
	return pkg;
}

RdrSpritesPkg * rdrStartDrawSpritesImmediateUP(RdrDevice *device, RdrDrawList *draw_list, U32 array_size, int screen_width, int screen_height, RdrSpriteState *states, RdrSpriteVertex *vertices, U16 *indices, U32* indices32, bool freeStates, bool freeVerts, bool freeIndices, bool isMemRef)
{
	RdrSpritesPkg *pkg = wtAllocCmd(device->worker_thread, RDRCMD_DRAWSPRITES, sizeof(RdrSpritesPkg));

	pkg->alloc_flags_for_test = 0;
	pkg->should_free_states = freeStates;
	pkg->should_free_vertices = freeVerts;
	pkg->should_free_indices = freeIndices;
	pkg->arrays_are_memref = isMemRef;

	if(isMemRef) //if we are using memrefs add a reference here
	{
		if(states)
			memrefIncrement(states);
		if(vertices)
			memrefIncrement(vertices);
		if(indices)
			memrefIncrement(indices);
		if(indices32)
			memrefIncrement(indices32);
	}

	pkg->states = states;
	pkg->vertices = vertices;
	pkg->indices = indices;
	pkg->indices32 = indices32;
	pkg->state_array_size = array_size;
	pkg->sprite_count = 0;
	pkg->screen_width = screen_width;
	pkg->screen_height = screen_height;
	return pkg;
}

void rdrEndDrawSpritesImmediate(RdrDevice *device)
{
	wtSendCmd(device->worker_thread);
}


RdrOcclusionQueryResult *rdrAllocOcclusionQuery(RdrDevice *device)
{
	RdrOcclusionQueryResult *res = calloc(1, sizeof(RdrOcclusionQueryResult));
	res->device = device;
	return res;
}

void rdrFreeOcclusionQuery(RdrOcclusionQueryResult *query)
{
	RdrDevice *device = query->device;
	if (device->is_locked_nonthread)
	{
		wtQueueCmd(device->worker_thread, RDRCMD_FREEQUERY, &query, sizeof(query));
	}
	else
	{
		QueuedRenderCmd *cmd = calloc(1, sizeof(QueuedRenderCmd) + sizeof(query));
		cmd->cmd = RDRCMD_FREEQUERY;
		cmd->data_size = sizeof(query);
		*((RdrOcclusionQueryResult **)(cmd + 1)) = query;
		eaPush(&device->queued_commands, cmd);
	}
}

