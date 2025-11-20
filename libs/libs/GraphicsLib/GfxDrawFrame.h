#ifndef _GFXDRAWFRAME_H_
#define _GFXDRAWFRAME_H_
GCC_SYSTEM

#include "GfxEnums.h"

typedef struct GfxRenderAction GfxRenderAction;
typedef struct GfxPerDeviceState GfxPerDeviceState;

bool gfxIsActionFull(GfxActionType action_type);

void gfxActionReleaseSurfaces(GfxRenderAction *action);
void gfxFreeActions(GfxPerDeviceState *device_state);

void gfxGetRenderSizeFromScreenSize(U32 renderSize[2]);

void gfxSetCurrentRegion(int region_idx);

#endif //_GFXDRAWFRAME_H_
