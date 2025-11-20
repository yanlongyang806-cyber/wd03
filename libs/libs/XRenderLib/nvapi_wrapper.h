#pragma once

#if !_PS3
#include <d3d9.h>
#include <d3d11.h>
#include <d3d10.h>
#include "../../3rdparty/nvperfsdk/nvapi.h"
#include "RdrEnums.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct StereoscopicTexParams
{
	U32 height;
	U32 width;
	union
	{
		D3DFORMAT d3d9Format;
		DXGI_FORMAT d3d11Format;
	};
} StereoscopicTexParams;

NvAPI_Status rxbxGetNVZBufferHandle(IDirect3DDevice9 * device, NVDX_ObjectHandle * handle);
NvAPI_Status rxbxGetNVTextureHandle(IDirect3DTexture9 * texture, NVDX_ObjectHandle * handle);
NvAPI_Status rxbxNVStretchRect(IDirect3DDevice9 * device,
							   NVDX_ObjectHandle hSrcObj,
							   CONST RECT * pSourceRect,
							   NVDX_ObjectHandle hDstObj,
							   CONST RECT * pDestRect,
							   D3DTEXTUREFILTERTYPE Filter);

U32 rxbxNVDriverVersion(void);
void rxbxNVInitStereoHandle(IDirect3DDevice9* dev9, IDirect3DDevice9Ex* dev9ex, ID3D11Device* dev11);
void rxbxNVDestroyStereoHandle(void);
bool rxbxNVStereoIsActive(void);
void rxbxNVStereoFillTexParams(StereoscopicTexParams *texParams);
void rxbxNVTextureUpdateD3D9(IDirect3DDevice9* dev9, IDirect3DDevice9Ex* dev9ex, IDirect3DTexture9 *tex, bool deviceLost);
void rxbxNVTextureUpdateD3D11(ID3D11Device* dev, ID3D11DeviceContext* devCon, ID3D11Texture2D *tex);
void rxbxNVSurfaceStereoCreationMode(RdrStereoOption stereo_option);

#ifdef __cplusplus
};
#endif
#endif
