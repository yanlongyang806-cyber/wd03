#ifndef _GFXSURFACE_H_
#define _GFXSURFACE_H_
GCC_SYSTEM

typedef struct GfxTempSurface GfxTempSurface;
typedef struct RdrSurfaceParams RdrSurfaceParams;
typedef struct GfxPerDeviceState GfxPerDeviceState;


GfxTempSurface *gfxGetTempSurface(const RdrSurfaceParams *params);
#define gfxReleaseTempSurface(surface_ptr) { gfxReleaseTempSurfaceEx((surface_ptr)); (surface_ptr) = NULL; }
void gfxReleaseTempSurfaceEx(GfxTempSurface *surface);
void gfxMarkTempSurfaceUsed(GfxTempSurface *surface);
void gfxTempSurfaceOncePerFramePerDevice(void);
void gfxFreeTempSurfaces(GfxPerDeviceState *device_state);
void gfxFreeSpecificSizedTempSurfaces(GfxPerDeviceState *device_state, U32 size[2], bool freeMultiples);

void gfxOnSurfaceDestroy(const RdrSurface *surface);
void gfxSurfaceDestroy(RdrSurface *surface);


#endif //_GFXSURFACE_H_
