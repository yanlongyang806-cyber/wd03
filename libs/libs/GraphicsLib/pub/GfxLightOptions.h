/***************************************************************************



***************************************************************************/

#ifndef _GFXLIGHTOPTIONS_H_
#define _GFXLIGHTOPTIONS_H_
#pragma once
GCC_SYSTEM

typedef struct GfxLightingOptions
{
	int max_static_lights_per_object;
	int max_static_lights_per_character;
	int max_shadowed_lights_per_frame;
	bool use_extra_key_lights_as_vertex_lights;
	bool apply_character_light_offsets_globally;
	bool enableDiffuseWarpTex; // Set by calling gfxEnableDiffuseWarp()
	bool enableDOFCameraFade;
	bool bRequireProjectorLights;
	bool bLockSpecularToDiffuseColor;
	bool disableHighBloomQuality;
	bool disableHighBloomIntensity;
} GfxLightingOptions;

extern GfxLightingOptions gfx_lighting_options;

void gfxSetDefaultLightingOptions(void); // call this before setting any lighting options
void gfxEnableDiffuseWarp();
void gfxLightingLockSpecularToDiffuseColor(int bLock);

#endif //_GFXLIGHTOPTIONS_H_
