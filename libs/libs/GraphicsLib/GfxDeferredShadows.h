#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef _GFXDEFERREDSHADOWS_H_
#define _GFXDEFERREDSHADOWS_H_

typedef struct GfxLight GfxLight;
typedef struct RdrDevice RdrDevice;
typedef struct RdrSurface RdrSurface;
typedef struct RdrLightDefinition RdrLightDefinition;
typedef struct BlendedSkyInfo BlendedSkyInfo;

void gfxRenderShadowBuffer(GfxLight **lights, int light_count, 
	const RdrLightDefinition **light_defs, const BlendedSkyInfo *sky_info, int scattering_stage);
void gfxSetShadowBufferTexture(RdrDevice *rdr_device, RdrSurface *shadow_buffer);

#define NUM_SSAO_SAMPLES 16
#define NUM_SSAO_CONSTANTS 5


#endif //_GFXDEFERREDSHADOWS_H_
