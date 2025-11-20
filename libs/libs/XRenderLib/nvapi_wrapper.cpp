
#if !_PS3
#include "utils.h"
#include "nvapi_wrapper.h"
#include "../../3rdparty/nvperfsdk/nvstereo.h"
#include "memlog.h"

nv::stereo::ParamTextureManagerD3D9* gStereoTexMgr9 = NULL;
nv::stereo::ParamTextureManagerD3D11* gStereoTexMgr11 = NULL;

extern "C"
{

NvAPI_Status rxbxGetNVZBufferHandle(IDirect3DDevice9 * device, NVDX_ObjectHandle * handle)
{
	return NvAPI_D3D9_GetCurrentZBufferHandle(device, handle);
}

NvAPI_Status rxbxGetNVTextureHandle(IDirect3DTexture9 * texture, NVDX_ObjectHandle * handle)
{
	return NvAPI_D3D9_GetTextureHandle(texture, handle);
}

NvAPI_Status rxbxNVStretchRect(IDirect3DDevice9 * device,
   NVDX_ObjectHandle hSrcObj,
   CONST RECT * pSourceRect,
   NVDX_ObjectHandle hDstObj,
   CONST RECT * pDestRect,
   D3DTEXTUREFILTERTYPE Filter)
{
	NvAPI_Status status = NvAPI_D3D9_StretchRect(device, hSrcObj, pSourceRect, hDstObj, pDestRect, Filter); 

#ifdef _FULLDEBUG
	if (status != NVAPI_OK)
	{
		NvAPI_ShortString string;
		NvAPI_GetErrorMessage(status, string);
		OutputDebugStringf("NVAPI Error: %s\n", string);
	}
#endif

	return status;
}

U32 rxbxNVDriverVersion(void)
{
	NvAPI_ShortString nvDriverString;
	NvU32 driverVersion;

	NvAPI_SYS_GetDriverAndBranchVersion(&driverVersion, nvDriverString);
	return driverVersion;
}

void rxbxNVInitStereoHandle(IDirect3DDevice9* dev9, IDirect3DDevice9Ex* dev9ex, ID3D11Device* dev11)
{
	NvAPI_Status nvError = NVAPI_OK;
	if (gStereoTexMgr9 || gStereoTexMgr11)
		return;

	if (dev9 || dev9ex) {
		gStereoTexMgr9 = new nv::stereo::ParamTextureManagerD3D9;
		if (dev9)
		{
			nvError = gStereoTexMgr9->Init(dev9);
		}
		else
		{
			nvError = gStereoTexMgr9->Init(dev9ex);
		}
	}
	else if (dev11)
	{
		gStereoTexMgr11 = new nv::stereo::ParamTextureManagerD3D11;
		nvError = gStereoTexMgr11->Init(dev11);
	}
	if (nvError != NVAPI_OK)
	{
		switch(nvError)
		{
		case NVAPI_STEREO_NOT_ENABLED:
			memlog_printf(NULL, "Steroscopic vision is not enabled on this machine.");
			break;
		default:
			memlog_printf(NULL, "Steroscopic vision failed to initialize. Error code: %d", nvError);
		}
	}
}

void rxbxNVDestroyStereoHandle(void)
{
	if (gStereoTexMgr9)
		delete(gStereoTexMgr9);
	else if (gStereoTexMgr11)
		delete(gStereoTexMgr11);
}

bool rxbxNVStereoIsActive(void)
{
	if (gStereoTexMgr9)
		return gStereoTexMgr9->IsStereoActive();
	else if (gStereoTexMgr11)
		return gStereoTexMgr11->IsStereoActive();

	return false;
}

void rxbxNVStereoFillTexParams(StereoscopicTexParams *texParams)
{
	if (gStereoTexMgr9)
	{
		texParams->width = nv::stereo::D3D9Type::StereoTexWidth;
		texParams->height = nv::stereo::D3D9Type::StereoTexHeight;
		texParams->d3d9Format = nv::stereo::D3D9Type::StereoTexFormat;
	} else {
		texParams->width = nv::stereo::D3D11Type::StereoTexWidth;
		texParams->height = nv::stereo::D3D11Type::StereoTexHeight;
		texParams->d3d11Format = nv::stereo::D3D11Type::StereoTexFormat;
	}
}

void rxbxNVTextureUpdateD3D9(IDirect3DDevice9* dev9, IDirect3DDevice9Ex* dev9ex, IDirect3DTexture9 *tex, bool deviceLost)
{
	gStereoTexMgr9->UpdateStereoTexture(dev9 ? dev9 : dev9ex, NULL, tex, deviceLost);
}

void rxbxNVTextureUpdateD3D11(ID3D11Device* dev, ID3D11DeviceContext* devCon, ID3D11Texture2D *tex)
{
	gStereoTexMgr11->UpdateStereoTexture(dev, devCon, tex, false);
}

void rxbxNVSurfaceStereoCreationMode(RdrStereoOption stereo_option)
{
	NVAPI_STEREO_SURFACECREATEMODE surface_creation_mode = NVAPI_STEREO_SURFACECREATEMODE_AUTO;

	switch (stereo_option)
	{
	case SURF_STEREO_AUTO:
		surface_creation_mode = NVAPI_STEREO_SURFACECREATEMODE_AUTO;
			break;
	case SURF_STEREO_FORCE_ON:
		surface_creation_mode = NVAPI_STEREO_SURFACECREATEMODE_FORCESTEREO;
			break;
	case SURF_STEREO_FORCE_OFF:
		surface_creation_mode = NVAPI_STEREO_SURFACECREATEMODE_FORCEMONO;
			break;
	default:
		assertmsg(0,"Invalid option for stereo mode");
	}

	if (gStereoTexMgr9)
		gStereoTexMgr9->SetSurfaceCreationStereoMode(surface_creation_mode);
	else if (gStereoTexMgr11)
		gStereoTexMgr11->SetSurfaceCreationStereoMode(surface_creation_mode);
}

};
#endif
