#include "GfxAuxDevice.h"
#include "GraphicsLibPrivate.h"
#include "winutil.h"
#include "inputLib.h"
#include "RdrState.h"
#include "inputKeyBind.h"
#include "earray.h"
#include "memlog.h"
#include "XboxThreads.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

void gfxAuxDeviceSaveParams(bool bSave)
{
	static KeyBindQuit mainQuit;
	static RdrDevice *primary_device;
	static GfxCameraController *saved_camera;
	static GfxCameraView *saved_camera_view;
	if (bSave) {
		mainQuit = keybind_GetQuit();
		primary_device = gfxGetActiveDevice();
		saved_camera = gfxGetActiveCameraController();
		saved_camera_view = gfxGetActiveCameraView();
	} else {
		gfxSetActiveDevice(primary_device);
		gfxSetActiveCameraController(saved_camera, false);
		gfxSetActiveCameraView(saved_camera_view, false);
		gfx_state.currentVisFrustum = &gfx_state.currentCameraView->frustum;
		keybind_SetQuit(mainQuit);
	}
}

RdrDevice *gfxAuxDeviceAdd(__in_opt WindowCreateParams *default_params, __in_opt const char *window_name, __in_opt GfxAuxDeviceCloseCallback close_callback, __in_opt GfxAuxDeviceCallback per_frame_callback, __in_opt void *userData)
{
	int i;
	WindowCreateParams params={0};
	RdrDevice *rdr_device;
	InputDevice *inpdev;

	memlog_printf(NULL, "gfxAuxDeviceAdd(%s):start", window_name?window_name:"");

	gfxAuxDeviceSaveParams(true);

	if (default_params) {
		params = *default_params;
	} else {
#if !PLATFORM_CONSOLE
		MONITORINFOEX moninfo;
		params.display.preferred_monitor = -1;
		multiMonGetMonitorInfo(multimonGetPrimaryMonitor() ? 0 : 1, &moninfo);
		params.display.xpos = moninfo.rcWork.left + 5;
		params.display.ypos = moninfo.rcWork.top + 5;
#endif
		params.threaded = 0;
		params.display.fullscreen = 0;
		params.display.width = 400;
		params.display.height = 300;
	}
	if (window_name) {
		rdrGetRememberedParams(window_name, &params.display);
	}

	rdr_device = rdrCreateDevice(&params, winGetHInstance(), THREADINDEX_RENDER);

	if (!rdr_device)
		return NULL;
	inpdev = inpCreateInputDevice(rdr_device,winGetHInstance(),keybind_ExecuteKey, rdr_state.unicodeRendererWindow);
	rdrSetTitle(rdr_device, window_name?window_name:"Cryptic");
	//rdrSetIcon(rdr_device, IDI_CRYPTIC);
	gfxRegisterDevice(rdr_device, inpdev, true);
	for (i=eaSize(&gfx_state.devices)-1; i>=0; i--) {
		if (gfx_state.devices[i] && gfx_state.devices[i]->rdr_device == rdr_device) {
			GfxPerDeviceState *device_state = gfx_state.devices[i];
			device_state->isAuxDevice = 1;
			if (window_name)
				device_state->auxDevice.auxDeviceName = strdup(window_name);
			device_state->auxDevice.auxCloseCallback = close_callback;
			device_state->auxDevice.auxPerFrameCallback = per_frame_callback;
			device_state->auxDevice.auxUserData = userData;
			device_state->primaryCameraController->override_bg_color = 1;
		}
	}

	gfxAuxDeviceSaveParams(false);
	memlog_printf(NULL, "gfxAuxDeviceAdd(%s):end:0x%08p", window_name?window_name:"", rdr_device);
	return rdr_device;
}


void gfxAuxDeviceRemove(RdrDevice *rdr_device)
{
	eaPush(&gfx_state.remove_devices, rdr_device);
}

void gfxAuxDeviceForEach(GfxAuxDeviceCallback callback, void *userData)
{
	int i;
	for (i=eaSize(&gfx_state.devices)-1; i>=0; i--) {
		if (gfx_state.devices[i] && gfx_state.devices[i]->isAuxDevice)
			callback(gfx_state.devices[i]->rdr_device, userData);
	}
}

RdrDevice *gfxNextDevice(RdrDevice *rdr_device)
{
	S32 i;
	bool bUseNext = false;
	for (i = 0; i < eaSize(&gfx_state.devices); i++)
	{
		if (gfx_state.devices[i] && gfx_state.devices[i]->rdr_device == rdr_device)
			bUseNext = true;
		else if (bUseNext && gfx_state.devices[i] && gfx_state.devices[i]->rdr_device)
			return gfx_state.devices[i]->rdr_device;
	}
	for (i = 0; i < eaSize(&gfx_state.devices); i++)
		if (gfx_state.devices[i] && gfx_state.devices[i]->rdr_device)
			return gfx_state.devices[i]->rdr_device;
	return NULL;
}

S32 gfxDeviceCount(void)
{
	return eaSize(&gfx_state.devices);
}

static void gfxAuxDeviceCloseButton(void)
{
	if (gfx_state.currentDevice->auxDevice.auxCloseCallback)
		gfx_state.currentDevice->auxDevice.auxCloseCallback(gfx_state.currentDevice->rdr_device, gfx_state.currentDevice->auxDevice.auxUserData);
}

void gfxAuxDeviceDefaultTop(RdrDevice *rdr_device, int flags, UIUpdateFunc ui_update_func)
{
	inpUpdateEarly(gfxGetActiveInputDevice());
	if (ui_update_func)
		ui_update_func(gfxGetFrameTime(), rdr_device);
	inpUpdateLate(gfxGetActiveInputDevice());
	if (gfx_state.currentDevice->auxDevice.auxDeviceName)
		rdrSetRememberedParams(gfx_state.currentDevice->auxDevice.auxDeviceName,rdr_device);
	gfxActiveCameraControllerSetFOV(45);
	gfxRunActiveCameraController(-1, NULL);

	gfxStartMainFrameAction(false, false, true, false, true);
}

void gfxAuxDeviceDefaultBottom(RdrDevice *rdr_device, int flags)
{
	gfxDrawFrame();
}


void gfxRunAuxDevicesCallback(RdrDevice *rdr_device, void *userData)
{
	gfxSetActiveDevice(rdr_device);
	if (gfx_state.currentDevice->auxDevice.auxPerFrameCallback)
		gfx_state.currentDevice->auxDevice.auxPerFrameCallback(rdr_device, gfx_state.currentDevice->auxDevice.auxUserData);
}

static void gfxAuxDeviceRemoveInternal(RdrDevice *rdr_device)
{
	gfxUnregisterDevice(rdr_device);
	rdrDestroyDevice(rdr_device);
	gfxSetActiveDevice(gfxGetPrimaryDevice());
}

void gfxRunAuxDevices(void)
{
	PERFINFO_AUTO_START("gfxRunAuxDevices", 1);

	gfxAuxDeviceSaveParams(true);
	keybind_SetQuit(gfxAuxDeviceCloseButton);

	gfxAuxDeviceForEach(gfxRunAuxDevicesCallback, NULL);

	gfxAuxDeviceSaveParams(false);

	// Remove all queued devices
	{
		int i;
		for (i=0; i<eaSize(&gfx_state.remove_devices); ++i)
		{
			gfxAuxDeviceRemoveInternal(gfx_state.remove_devices[i]);
		}
		eaDestroy(&gfx_state.remove_devices);
	}

	PERFINFO_AUTO_STOP();
}


