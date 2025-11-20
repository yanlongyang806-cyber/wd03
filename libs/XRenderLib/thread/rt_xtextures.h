#ifndef _RT_XTEXTURES_H_
#define _RT_XTEXTURES_H_
#pragma once
GCC_SYSTEM

#include "rt_xdevice.h"
#include "rt_xstate.h"
#include "RdrTexture.h"

typedef struct BasicTexture BasicTexture;

typedef struct RdrTextureDataDX
{
	RdrTextureObj texture;
	RdrTextureBufferObj d3d11_data;
	RdrTextureObj texture_sysmem;

	RdrTexFormatObj			tex_format;

	U32						tex_hash_value;

	U16						width;
	U16						height;

	U32						depth : 16;
	U32						max_levels : 4;
	U32						levels_inited : 4;
	U32						can_sub_update : 1;
	U32						compressed : 1;
	U32						srgb : 1;
	U32						is_non_managed : 1;
	RdrTexType				tex_type : 3;
	U32						created_while_dev_lost : 1;

	U32						anisotropy : 8;
	U32						explicit_padding : 24;

	int						memory_usage;
	const char *			memmonitor_name;
	const BasicTexture *	debug_backpointer;
} RdrTextureDataDX;

const char * rxbxGetTextureD3DFormatString( D3DFORMAT src_format );

void rxbxSetTextureDataDirect(RdrDeviceDX *device, RdrTexParams *rtex, WTCmdPacket *packet);
void rxbxSetTextureSubDataDirect(RdrDeviceDX *device, RdrSubTexParams *rtex, WTCmdPacket *packet);
void rxbxTexStealSnapshotDirect(RdrDeviceDX *device, RdrTexStealSnapshotParams *steal, WTCmdPacket *packet);

void rxbxSetTexAnisotropyDirect(RdrDeviceDX *device, RdrTextureAnisotropy *params, WTCmdPacket *packet);

RdrTextureDataDX *rxbxMakeTextureForSurface(RdrDeviceDX *device, RdrTexHandle *handle, 
									  RdrTexFormatObj tex_format, U32 width, U32 height, 
									  bool is_srgb, RdrSurfaceBindFlags texture_usage, bool is_cubemap, 
									  RdrTexFlags extra_flags, int multisample_count);
void rxbxFreeSurfaceTexture(RdrDeviceDX *device, RdrTexHandle handle, RdrTextureObj tex);

void rxbxGetTexInfoDirect(RdrDeviceDX *device, RdrGetTexInfo *get, WTCmdPacket *packet);

void rxbxFreeTextureDirect(RdrDeviceDX *device, RdrTexHandle *tex_ptr, WTCmdPacket *packet);
void rxbxFreeAllTexturesDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet);
void rxbxFreeAllNonManagedTexturesDirect(RdrDeviceDX *device);
void rxbxFreeTexData(RdrDeviceDX *device, RdrTexHandle handle);
void rxbxSwapTexHandlesDirect(RdrDeviceDX *device, RdrTexHandle *tex_ptrs, WTCmdPacket *packet);

int rxbxGetTextureSize(int width, int height, int depth, RdrTexFormatObj src_format, int levels, RdrTexType tex_type);
int rxbxGetSurfaceSize(int width, int height, RdrTexFormatObj src_format);

void rxbxValidateTextures(RdrDeviceDX *device);

DXGI_FORMAT rxbxGetGPUFormat11(RdrTexFormatObj dst_format, bool bSRGB);
DXGI_FORMAT rxbxGetGPUSurfaceFormat11(RdrTexFormatObj dst_format, bool bSRGB);
D3DFORMAT rxbxGetGPUFormat9(RdrTexFormatObj dst_format);
void rxbxStereoscopicTexUpdate(RdrDeviceDX *device);

// Enable to log video memory and system memory use after every frame, and device loss cleanup.
#define LOG_RESOURCE_USAGE 0
void rxbxDumpMemInfo(const char * p_pszEvent);

// Enable to emulate resource leaks.
#define D3D_RESOURCE_LEAK_TEST_ENABLE 0
#if D3D_RESOURCE_LEAK_TEST_ENABLE
void rxbxResourceLeakTest(RdrDeviceDX *device);
#endif

#endif // _RT_XTEXTURES_H_


