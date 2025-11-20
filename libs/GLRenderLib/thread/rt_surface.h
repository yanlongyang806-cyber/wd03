#ifndef _RT_SURFACE_H_
#define _RT_SURFACE_H_

#include "RdrDrawable.h"
#include "surface.h"
#include "device.h"

typedef struct RwglUpdateMatrixData
{
	Mat44 projection;
	Mat4 view_mat, inv_view_mat;
	RdrSurfaceWinGL *surface;
} RwglUpdateMatrixData;

typedef struct RwglSurfaceParams
{
	RdrSurfaceWinGL *surface;
	RdrSurfaceParams params;
} RwglSurfaceParams;


void rwglInitSurfaceDirect(RdrDeviceWinGL *device, RwglSurfaceParams *glparams);
void rwglFreeSurfaceDirect(RdrDeviceWinGL *device, RdrSurfaceWinGL *surface);

void rwglSetSurfaceActiveDirect(RdrDeviceWinGL *device, RdrSurfaceWinGL *surface);
void rwglUnsetSurfaceActiveDirect(RdrDeviceWinGL *device);

void rwglClearActiveSurfaceDirect(RdrDeviceWinGL *device, RdrClearParams *params);
void rwglUpdateSurfaceMatricesDirect(RwglUpdateMatrixData *data);
void rwglSetSurfaceFogDirect(RdrSetFogData *data);

void rwglGetSurfaceDataDirect(RdrSurfaceWinGL *surface, RdrSurfaceData *params);

void rwglBindSurfaceDirect(RdrSurfaceWinGL *surface, int tex_unit, RdrSurfaceBuffer buffer);
void rwglReleaseSurfaceDirect(RdrSurfaceWinGL *surface, int tex_unit, RdrSurfaceBuffer buffer);

#endif //_RT_SURFACE_H_

