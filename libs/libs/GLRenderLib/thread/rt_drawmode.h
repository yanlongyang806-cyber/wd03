#ifndef _RT_DRAWMODE_H_
#define _RT_DRAWMODE_H_

#include "device.h"

typedef enum
{
	DRAWBIT_SKINNED = 1<<0,
	DRAWBIT_SHADOWRENDER = 1<<1,
} DrawModeBits;

void rwglSetupPrimitiveDrawMode(RdrDeviceWinGL *device);
void rwglSetupPostProcessDrawMode(RdrDeviceWinGL *device, int is_shape);
void rwglSetupSpriteDrawMode(RdrDeviceWinGL *device);
void rwglSetupParticleDrawMode(RdrDeviceWinGL *device);
void rwglSetupTerrainDrawMode(RdrDeviceWinGL *device, int shadowmap_render);
void rwglSetupNormalDrawMode(RdrDeviceWinGL *device, DrawModeBits bits);

void rwglReloadDefaultShadersDirect(RdrDeviceWinGL *device);

void rwglSetupSkinning(RdrDeviceWinGL *device, int bone_count, SkinningMat4 bone_infos[]);
void rwglSetDefaultBlendMode(RdrDeviceWinGL *device);
void rwglBindBlendModeTextures(RdrDeviceWinGL *device, TexHandle *textures, U32 tex_count);

void rwglBindMaterial(RdrDeviceWinGL *device, RdrMaterial *rdr_material, ShaderHandle handle);


#endif //_RT_DRAWMODE_H_

