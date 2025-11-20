#ifndef _RENDERLIB_H_
#define _RENDERLIB_H_
#pragma once
GCC_SYSTEM

#include "RdrDevice.h"
#include "RdrTextureEnums.h"
#include "RdrGeometry.h"
#include "RdrDrawList.h"

//////////////////////////////////////////////////////////////////////////
// Handles

void rdrChangeTexHandleFlags(TexHandle *handle, RdrTexFlags flags);
RdrTexFlags rdrGetTexHandleFlags(TexHandle *handle);
U32 rdrGetTexHandleKey(TexHandle *handle); // just for hashing in GraphicsLib
void rdrAddRemoveTexHandleFlags(TexHandle *handle, RdrTexFlags flags_to_add, RdrTexFlags flags_to_remove);
TexHandle rdrGenTexHandle(RdrTexFlags flags);
GeoHandle rdrGenGeoHandle(void);
ShaderHandle rdrGenShaderHandle(void);
TexHandle rdrSurfaceToTexHandleEx(RdrSurface *surface, RdrSurfaceBuffer buffer, int set_index, int force_flags, bool unresolved_is_ok);
#define rdrSurfaceToTexHandle(surface, buffer) rdrSurfaceToTexHandleEx(surface, buffer, RDRSURFACE_SET_INDEX_DEFAULT, 0, false)


//////////////////////////////////////////////////////////////////////////
// Resolution query

typedef struct GfxResolution
{	
	int width, height, depth;
	int *refreshRates;
	int adapter, displayMode;
} GfxResolution;

SA_ORET_NN_VALID GfxResolution **rdrGetSupportedResolutions(GfxResolution **desktop_res, int monitor_index, const char * device_type);
void rdrGetClosestResolution(SA_PARAM_NN_VALID int *width, SA_PARAM_NN_VALID int *height, SA_PARAM_NN_VALID int *refreshRate, int fullscreen, int monitor_index);

//////////////////////////////////////////////////////////////////////////
// error and alert functions able to be called from anywhere (in render thread or not)

void rdrSafeAlertMsg(const char *str);
void rdrSafeErrorMsg(char *str, char *title, char *fault, int highlight);


typedef int (*PrintfFunc)(FORMAT_STR char const *fmt, ...);
void rdrSetStatusPrintf(PrintfFunc printf_func);
extern PrintfFunc rdrStatusPrintf;

void rdrStartup(void);

void rdrUpdateGlobalsOnDeviceLock(RdrDevice* device);

void rdrDisableLowResAlpha( bool low_res_alpha_disabled );

void rdrSetDisableToneCurve10PercentBoost(bool disable);

void rdrDisplayParamsPreChange(RdrDevice *device, DisplayParams *new_params);

//////////////////////////////////////////////////////////////////////////

void rdrSetupOrthoGL(Mat44 projection_matrix, F32 l, F32 r, F32 b, F32 t, F32 znear, F32 zfar);
void rdrSetupOrthoDX(Mat44 projection_matrix, F32 l, F32 r, F32 b, F32 t, F32 znear, F32 zfar);
void rdrSetupOrthographicProjection(Mat44 projection_matrix, F32 aspect, F32 ortho_zoom);

void rdrSetupFrustumGL(Mat44 projection_matrix, F32 l, F32 r, F32 b, F32 t, F32 znear, F32 zfar);
void rdrSetupFrustumDX(Mat44 projection_matrix, F32 l, F32 r, F32 b, F32 t, F32 znear, F32 zfar);
void rdrSetupPerspectiveProjection(Mat44 projection_matrix, F32 fovy, F32 aspect, F32 znear, F32 zfar);

//////////////////////////////////////////////////////////////////////////

#endif //_RENDERLIB_H_

