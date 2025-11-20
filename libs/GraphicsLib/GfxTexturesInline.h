#ifndef _GFXTEXTURESINLINE_H
#define _GFXTEXTURESINLINE_H

#include "utils.h"
#include "GfxTextures.h"
#include "GraphicsLibPrivate.h"
#include "GenericMesh.h"

// For fixed/hardcoded textures
#define texDemandLoadFixed(texbind) texDemandLoadInlineNoDetailCalc(texbind, white_tex)
#define texDemandLoadFixedInvis(texbind) texDemandLoadInlineNoDetailCalc(texbind, invisible_tex)

__forceinline static TexHandle texDemandLoadInline(BasicTexture *texbind, F32 dist, F32 uv_density, BasicTexture *errortex)
{
	return texDemandLoad(texbind, dist, uv_density, errortex);
}

__forceinline static TexHandle texDemandLoadInlineNoDetailCalc(BasicTexture *texbind, BasicTexture *errortex)
{
	return texDemandLoad(texbind, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY, errortex);
}

__forceinline static bool texIsFullyLoadedInline(const BasicTexture *texbind)
{
	const BasicTexture * actual_tex = texbind->actualTexture ? texbind->actualTexture : texbind;
	return actual_tex && actual_tex->loaded_data && !actual_tex->loaded_data->mip_bound_on && 
		!actual_tex->loaded_data->loading && (actual_tex->tex_is_loaded & gfx_state.currentRendererFlag);
}

#endif

