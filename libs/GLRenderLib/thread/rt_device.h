#ifndef _RT_DEVICE_H_
#define _RT_DEVICE_H_

#include "device.h"

void rwglCreateDirect(RdrDeviceWinGL *device, WindowCreateParams *params);
void rwglSwapBufferDirect(RdrDeviceWinGL *device);
void rwglProcessWindowsMessagesDirect(RdrDeviceWinGL *device);
void rwglDestroyDirect(RdrDeviceWinGL *device);

int rwglIsInactive(RdrDevice *device);
void rwglReactivate(RdrDevice *device);

void rwglSetVsyncDirect(RdrDeviceWinGL *device, int enable);
void rwglSetGammaDirect(RdrDeviceWinGL *device, F32 gamma);
void rwglNeedGammaResetDirect(RdrDeviceWinGL *device);

void rwglDestroyAllSecondarySurfacesDirect(RdrDeviceWinGL *device);

#endif //_RT_DEVICE_H_

