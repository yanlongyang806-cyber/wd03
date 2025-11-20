#ifndef _RDRDEVICEPRIVATE_H_
#define _RDRDEVICEPRIVATE_H_

#include "RdrDevice.h"
#include "RdrSurface.h"
#include "RdrTexture.h"

#define XDK_FLASH_DESIRED_VERSION 8507
#define XDK_FLASH_DESIRED_VERSION_LOCATION "N:\\Software\\xdk\\Jul 2009\\XDKRecoveryXenon9328.8.exe"

#if _XBOX
	#if _XDK_VER < 9328
		#pragma message("XDK version 9328 (July 2009) required, please update by running N:\\Software\\xdk\\Jul 2009\\XDKSetupXenon9328.8.exe")
		YouHaveA InvalidXDKVersion
	#endif
#endif


// MUST be 64 bits or less!
typedef struct RdrTexHandle
{
	union
	{
		struct 
		{
			U32 index:RDRSURFACE_INDEX_NUMBITS; // index into texhandle_surfaces earray
			U32 buffer:RDRSURFACE_BUFFER_NUMBITS; // RdrSurfaceBuffer
			U32 no_autoresolve:RDRSURFACE_NOAUTORESOLVE_BIT; // bind texture without auto-resolving
			U32 set_index:RDRSURFACE_SET_INDEX_NUMBITS; // which screen grab texture to use, 0 for current surface contents
		} surface;

		struct 
		{
			U32 hash_value;
		} texture;
	};

	U32 is_surface:1;
	U32 sampler_flags:31;

} RdrTexHandle;

STATIC_ASSERT(sizeof(RdrTexHandle) == sizeof(TexHandle));

#define TexHandleToRdrTexHandle(tex_handle) (*((RdrTexHandle *)(&(tex_handle))))
#define RdrTexHandleToTexHandle(rdr_tex_handle) (*((TexHandle *)(&(rdr_tex_handle))))

void rdrInitDevice(SA_PARAM_NN_VALID RdrDevice *device, bool threaded, int processor_idx);
void rdrUninitDevice(RdrDevice *device);

void rdrInitSurface(RdrDevice *device, RdrSurface *surface);

void rdrAlertMsg(RdrDevice *device, const char *str);
void rdrErrorMsg(RdrDevice *device, char *str, char* title, char* fault, int highlight);

void rdrCheckThread(void);
void rdrCheckNotThread(void);

void rdrRemoveSurfaceTexHandle(RdrSurface *surface);
RdrSurface *rdrGetSurfaceForTexHandle(RdrTexHandle tex_handle);

#endif //_RDRDEVICEPRIVATE_H_


