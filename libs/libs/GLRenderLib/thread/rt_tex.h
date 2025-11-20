#ifndef _RT_TEX_H_
#define _RT_TEX_H_

#include "RdrTexture.h"
#include "device.h"

typedef struct RdrTextureDataWinGL
{
	GLenum			tex_format;
	U32				width;
	U32				height;
	U32				can_sub_update : 1;
	U32				compressed : 1;
} RdrTextureDataWinGL;

void rwglSetTextureDataDirect(RdrDeviceWinGL *device, RdrTexParams *rtex);
void rwglSetTextureSubDataDirect(RdrDeviceWinGL *device, RdrSubTexParams *rtex);
void rwglGetTexInfoDirect(RdrDeviceWinGL *device, RdrGetTexInfo *get);
void rwglSetTexAnisotropyDirect(RdrDeviceWinGL *device, RdrTextureAnisotropy *params);
void rwglFreeTextureDirect(RdrDeviceWinGL *device, TexHandle tex);
void rwglFreeAllTexturesDirect(RdrDeviceWinGL *device);



#endif //_RT_TEX_H_



