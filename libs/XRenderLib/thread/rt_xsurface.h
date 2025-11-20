#ifndef _RT_XSURFACE_H_
#define _RT_XSURFACE_H_

#include "RdrDrawable.h"
#include "xsurface.h"
#include "xdevice.h"

#if !PLATFORM_CONSOLE
#define D3DFMT_NVIDIA_RAWZ_DEPTH_TEXTURE_FCC ((D3DFORMAT)MAKEFOURCC('R','A','W','Z'))
#define D3DFMT_NVIDIA_INTZ_DEPTH_TEXTURE_FCC ((D3DFORMAT)MAKEFOURCC('I','N','T','Z'))
#define D3DFMT_NULL_TEXTURE_FCC ((D3DFORMAT)MAKEFOURCC('N','U','L','L'))
#define D3DFMT_ATI_DEPTH_TEXTURE_16_FCC ((D3DFORMAT)MAKEFOURCC('D','F','1','6'))
#define D3DFMT_ATI_DEPTH_TEXTURE_24_FCC ((D3DFORMAT)MAKEFOURCC('D','F','2','4'))
#define D3DFMT_ATI_FOURCC_RESZ ((D3DFORMAT)(MAKEFOURCC('R','E','S','Z')))
#define D3DFMT_ATI_INSTANCING_FCC ((D3DFORMAT)MAKEFOURCC('I','N','S','T'))
#define ATI_RESZ_CODE 0x7fa05000
#define D3DFMT_NVIDIA_ATOC ((D3DFORMAT)MAKEFOURCC('A','T','O','C'))
#define ATI_ATOC_ENABLE (MAKEFOURCC('A','2','M','1'))
#define ATI_ATOC_DISABLE (MAKEFOURCC('A','2','M','0'))
#endif

typedef struct RxbxSurfaceParams
{
	RdrSurfaceDX *surface;
	RdrSurfaceParams params;
} RxbxSurfaceParams;


void rxbxInitSurfaceDirect(RdrDeviceDX *device, RxbxSurfaceParams *glparams, WTCmdPacket *packet);
void rxbxFreeSurfaceDirect(RdrDeviceDX *device, RdrSurfaceDX **surface_ptr, WTCmdPacket *packet);
void rxbxSurfaceCleanupDirect(RdrDeviceDX *device,RdrSurfaceDX *surface, WTCmdPacket *packet);

void rxbxSetSurfaceActiveDirect(RdrDeviceDX *device, RdrSurfaceActivateParams *params);
void rxbxOverrideDepthSurfaceDirect(RdrDeviceDX *device, RdrSurface **override_depth_surface, WTCmdPacket *packet);
void rxbxSurfaceRestoreAfterSetActiveDirect(RdrDeviceDX *device, RdrSurfaceBufferMaskBits *pmask, WTCmdPacket *packet);

__forceinline static bool rxbxSurfaceIsInited(const RdrSurfaceDX *surface)
{
	return surface->type != SURF_UNINITED;
}
__forceinline static void rxbxSetSurfaceActiveDirectSimple(RdrDeviceDX *device, RdrSurfaceDX *surface)
{
	RdrSurfaceActivateParams params = { &surface->surface_base, 0 };
	rxbxSetSurfaceActiveDirect(device, &params);
}

void rxbxUnsetSurfaceActiveDirect(RdrDeviceDX *device);

void rxbxClearActiveSurfaceDirect(RdrDeviceDX *device, RdrClearParams *params, WTCmdPacket *packet);
void rxbxSetSurfaceAutoClearDirect(RdrDeviceDX *device, RdrSurfaceDX *surface, RdrClearParams *params);
void rxbxUpdateSurfaceMatricesDirect(RdrDeviceDX *device, RdrSurfaceUpdateMatrixData *data, WTCmdPacket *packet);
void rxbxSetSurfaceFogDirect(RdrDeviceDX *device, RdrSetFogData *data, WTCmdPacket *packet);
void rxbxSurfaceSetDepthSurfaceDirect(RdrDeviceDX *device, RdrSurfaceDX **surfaces, WTCmdPacket *packet);
void rxbxSurfaceSwapSnapshotsDirect(RdrDeviceDX *device, RdrSwapSnapshotData *params, WTCmdPacket *packet);

void rxbxSetActiveSurfaceBufferWriteMask(RdrDeviceDX *device, RdrSurfaceBufferMaskBits buffer_write_mask, RdrSurfaceFace face);

void rxbxGetSurfaceDataDirect(RdrSurfaceDX *surface, RdrSurfaceData *params);

void rxbxSurfaceSnapshotDirect(RdrDeviceDX *device, RdrSurfaceSnapshotData *snapshot_data, WTCmdPacket *packet);

void rxbxBindSurfaceDirect(RdrSurfaceDX *surface, bool is_vertex_texture, int tex_unit, RdrSurfaceBuffer buffer, int set_index, RdrTexFlags sampler_flags);
void rxbxReleaseSurfaceDirect(RdrSurfaceDX *surface, bool is_vertex_texture, int tex_unit, RdrSurfaceBuffer buffer, int set_index);

const char * rdrGetSurfaceBufferTypeNameString(RdrSurfaceBufferType type);

RdrTexFormatObj rxbxGetTexFormatForSBT(RdrSurfaceBufferType sbt);
DXGI_FORMAT rxbxGetDXGIFormatForSBT(RdrSurfaceBufferType sbt);
bool rxbxGetDepthFormat(RdrDeviceDX *device, RdrSurfaceFlags surface_creation_flags, RdrTexFormatObj *depth_format);

int rxbxCalcSurfaceMemUsage(int width, int height, int multisample_type, RdrTexFormatObj surface_format);

#if !_XBOX
void rxbxResolve(RdrDeviceDX * device, RdrSurfaceObj pSrcSurface, RdrPerSnapshotState * snapshot, RdrSurfaceFace face, int width, int height, bool bIsCubemapTexture);
void rxbxResolveToTexture(RdrDeviceDX * device, RdrSurfaceObj pSrcSurface, RdrTextureDataDX * pDstTexture, RdrSurfaceFace face, int width, int height, bool bIsCubemapTexture);
#endif

void rxbxSurfaceResolve(RdrDeviceDX* device, RdrSurfaceResolveData* resolve_data, WTCmdPacket* packet);
void rxbxUpdateMSAAResolveOrSurfaceCopyTexture(RdrDeviceDX *device, RdrSurfaceDX* surface, RdrSurfaceBufferMaskBits buffers, int dest_set_index);

void rxbxSurfaceSetAutoResolveDisableMaskDirect(RdrDeviceDX *device, RdrSurfaceSetAutoResolveDisableMaskData *data, WTCmdPacket *packet);
void rxbxSurfacePushAutoResolveMaskDirect(RdrDeviceDX *device, RdrSurfaceSetAutoResolveDisableMaskData *data, WTCmdPacket *packet);
void rxbxSurfacePopAutoResolveMaskDirect(RdrDeviceDX *device, RdrSurfaceSetAutoResolveDisableMaskData *data, WTCmdPacket *packet);


RdrNVIDIACSAAMode rxbxSurfaceGetMinCSAALevel(const RdrSurfaceDX *surface);
int rxbxSurfaceGetMultiSampleCount(const RdrSurfaceDX *surface, DWORD *outQuality);

void rxbxSurfaceGrowSnapshotTextures(RdrDeviceDX *device, RdrSurfaceDX *surface, int buffer_index, int snapshot_index, const char *name);

#endif //_RT_XSURFACE_H_


