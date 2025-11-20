#include "MemoryPool.h"
#include "memlog.h"
#include "MemRef.h"
#include "ScratchStack.h"
#include "ImageUtil.h"

#include "RenderLib.h"
#include "rt_xtextures.h"
#include "rt_xsurface.h"
#include "nvapi_wrapper.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

#define FORCE_NONTILED 0


#ifdef _FULLDEBUG
#define VALIDATE_TEXTURES rxbxValidateTextures(device)
#else
#define VALIDATE_TEXTURES
#endif

typedef struct D3DFormatDesc
{
	D3DFORMAT format;
	const char * format_string;
} D3DFormatDesc;

#define FORMAT_ENTRY( Format )	{ Format, #Format }
D3DFormatDesc s_formats[] =
{
	FORMAT_ENTRY(D3DFMT_X8R8G8B8),
	FORMAT_ENTRY(D3DFMT_D24S8),
	FORMAT_ENTRY(D3DFMT_A8R8G8B8),
	FORMAT_ENTRY(D3DFMT_A32B32G32R32F),
	FORMAT_ENTRY(D3DFMT_A16B16G16R16F),
	FORMAT_ENTRY(D3DFMT_R32F),
	FORMAT_ENTRY(D3DFMT_G16R16F),
	FORMAT_ENTRY(D3DFMT_G16R16),
	FORMAT_ENTRY(D3DFMT_A16B16G16R16),
	FORMAT_ENTRY(D3DFMT_A8L8),
	FORMAT_ENTRY(D3DFMT_R5G6B5),
	FORMAT_ENTRY(D3DFMT_A1R5G5B5),
	FORMAT_ENTRY(D3DFMT_A8),
#if !PLATFORM_CONSOLE
	FORMAT_ENTRY(D3DFMT_NULL_TEXTURE_FCC),
	FORMAT_ENTRY(D3DFMT_NVIDIA_RAWZ_DEPTH_TEXTURE_FCC),
	FORMAT_ENTRY(D3DFMT_NVIDIA_INTZ_DEPTH_TEXTURE_FCC),
	FORMAT_ENTRY(D3DFMT_ATI_DEPTH_TEXTURE_16_FCC),
	FORMAT_ENTRY(D3DFMT_ATI_DEPTH_TEXTURE_24_FCC),
#endif
	FORMAT_ENTRY(D3DFMT_DXT1),
	FORMAT_ENTRY(D3DFMT_DXT2),
	FORMAT_ENTRY(D3DFMT_DXT3),
	FORMAT_ENTRY(D3DFMT_DXT4),
	FORMAT_ENTRY(D3DFMT_DXT5),
	FORMAT_ENTRY(D3DFMT_A8B8G8R8),
	FORMAT_ENTRY(D3DFMT_X8B8G8R8),
};
#undef FORMAT_ENTRY

const char * rxbxGetTextureD3DFormatString(D3DFORMAT src_format)
{
	int fmt_index;
	for (fmt_index = 0; fmt_index < ARRAY_SIZE(s_formats); ++fmt_index)
	{
		if (s_formats[fmt_index].format == src_format)
			return s_formats[fmt_index].format_string;
	}
	return "Unknown D3DFORMAT";
}

static const char * rxbxGetTextureTypeString(RdrTexType type)
{
	switch (type)
	{
	case RTEX_1D:
		return "1D";

	xcase RTEX_2D:
		 return "2D";

	xcase RTEX_3D:
		return "3D";

	xcase RTEX_CUBEMAP:
		return "cubemap";

	xdefault:
		assert( 0 );
	}
	return "Invalid type";

}

int rtexWidthToBytes(int width, RdrTexFormatObj src_format)
{
	int bytes = 0;
	switch (src_format.format)
	{
		xcase RTEX_BGR_U8:
			bytes = width * 3;

		xcase RTEX_D24S8:
		case RTEX_BGRA_U8:
		case RTEX_RGBA_U8:
		case RTEX_RGBA10:
			bytes = width * 4;
			
		xcase RTEX_RGBA_F32:
			bytes = width * sizeof(F32) * 4;
			
		xcase RTEX_RGBA_F16:
			bytes = width * sizeof(F16) * 4;
			
		xcase RTEX_R_F32:
			bytes = width * sizeof(F32);
			
		xcase RTEX_G16R16F:
			bytes = width * sizeof(F32) * 2;
			
		xcase RTEX_G16R16:
			bytes = width * sizeof(short) * 2;
			
		xcase RTEX_A16B16G16R16:
			bytes = width * sizeof(short) * 4;
			
		xcase RTEX_R5G6B5:
			bytes = width * sizeof(short);
			
		xcase RTEX_DXT1:
			bytes = (( width + 3 ) & ~3)* 2;

		xcase RTEX_DXT3:
		case RTEX_DXT5:
			bytes = (( width + 3 ) & ~3) * 4;
			
		xcase RTEX_BGRA_5551:
			bytes = width * 2;

		xcase RTEX_A_U8:
			bytes = width;
			
		xcase RTEX_NVIDIA_RAWZ:
		case RTEX_NVIDIA_INTZ:
			bytes = width * 4;
			
		xcase RTEX_ATI_DF16:
			bytes = width * 2;
			
		xcase RTEX_ATI_DF24:
			bytes = width * 4;
			
		xdefault:
			bytes = 0;
			assert( 0 );
	}

	return bytes;
}

int HeightToRows(int height, RdrTexFormatObj src_format)
{
	switch (src_format.format)
	{
		case RTEX_BGR_U8:
		case RTEX_BGRA_U8:
		case RTEX_RGBA_U8:
		case RTEX_RGBA_F16:
		case RTEX_RGBA_F32:
		case RTEX_R_F32:
		case RTEX_BGRA_5551:
		case RTEX_A_U8:
		case RTEX_NULL:
		case RTEX_NVIDIA_INTZ:
		case RTEX_NVIDIA_RAWZ:
		case RTEX_ATI_DF16:
		case RTEX_ATI_DF24:
		case RTEX_D24S8:
		case RTEX_G16R16F:
		case RTEX_G16R16:
		case RTEX_A16B16G16R16:
		case RTEX_R5G6B5:
		case RTEX_RGBA10:
			return height;
		case RTEX_DXT1:
		case RTEX_DXT3:
		case RTEX_DXT5:
			return (( height + 3 ) & ~3) / 4;
		xdefault:
			assert( 0 );
			return 0;
	}
}

int rxbxGetTextureSize(int width, int height, int depth, RdrTexFormatObj src_format, int levels, RdrTexType tex_type)
{
	int ret_size = 0;
	if (tex_type == RTEX_CUBEMAP) {
		depth = 6;
		tex_type = RTEX_3D; // faked for the getTextureMemoryUsageEx2 call
	} else if (tex_type == RTEX_3D) {
		assert(depth > 0);
	} else {
		assert(tex_type == RTEX_2D);
		depth = 1;
	}

	if (src_format.format == RTEX_NULL)
		return 0; //these are fake textures to satisfy the API and take no memory

	for (; levels > 0; --levels)
	{
		int src_row_size = rtexWidthToBytes(width, src_format);
		int rows = HeightToRows(height, src_format);
		int level_size = rows * src_row_size;
		ret_size += level_size;

		width = MAX(1, width >> 1);
		height = MAX(1, height >> 1);
	}
	ret_size *= depth;

	return ret_size;
}

int rxbxGetSurfaceSize(int width, int height, RdrTexFormatObj src_format_input)
{
	return rxbxGetTextureSize(width, height, 1, src_format_input, 1, RTEX_2D);
}

MP_DEFINE(RdrTextureDataDX);

#define D3DBaseTexture_Invalidate(texture, tex_type) do { \
	switch(tex_type) {															\
        xcase RTEX_2D:															\
            CHECKX(IDirect3DTexture9_AddDirtyRect((D3DTexture*)texture, NULL));			\
        xcase RTEX_CUBEMAP:														\
            CHECKX(IDirect3DCubeTexture9_AddDirtyRect((D3DCubeTexture*)texture, 0, NULL));	\
            CHECKX(IDirect3DCubeTexture9_AddDirtyRect((D3DCubeTexture*)texture, 1, NULL));	\
            CHECKX(IDirect3DCubeTexture9_AddDirtyRect((D3DCubeTexture*)texture, 2, NULL));	\
            CHECKX(IDirect3DCubeTexture9_AddDirtyRect((D3DCubeTexture*)texture, 3, NULL));	\
            CHECKX(IDirect3DCubeTexture9_AddDirtyRect((D3DCubeTexture*)texture, 4, NULL));	\
            CHECKX(IDirect3DCubeTexture9_AddDirtyRect((D3DCubeTexture*)texture, 5, NULL));	\
		xcase RTEX_3D:															\
            CHECKX(IDirect3DVolumeTexture9_AddDirtyBox((D3DVolumeTexture*)texture, NULL)); \
        xdefault:																\
            assert(0);															\
    } \
} while (0)

#define D3DBaseTexture_LockRect(texture, tex_type, depth, mip_level, pLock, pLockRect, hr)			\
	switch(tex_type) {																				\
		xcase RTEX_2D:																				\
			hr = IDirect3DTexture9_LockRect((D3DTexture*)texture, mip_level, pLock, pLockRect, 0);	\
		xcase RTEX_CUBEMAP:																			\
			hr = IDirect3DCubeTexture9_LockRect((D3DCubeTexture*)texture, depth, mip_level, pLock, pLockRect, 0);	\
		xdefault:																					\
			assert(0);																				\
	}

#define D3DBaseTexture_UnlockRect(texture, tex_type, depth, mip_level, hr)							\
	switch(tex_type) {																			\
		xcase RTEX_2D:																			\
			hr = IDirect3DTexture9_UnlockRect((D3DTexture*)texture, mip_level);						\
		xcase RTEX_CUBEMAP:																		\
			hr = IDirect3DCubeTexture9_UnlockRect((D3DCubeTexture*)texture, depth, mip_level);		\
	}

static int copy3DTexSubImage9(RdrDeviceDX *device, RdrTextureObj dst_texture, RdrTexFormatObj dst_format, int mip_level, void *src_data, RdrTexFormatObj src_format, int x, int y, int z, int width, int height, int depth, int orig_width, int orig_height, int orig_depth)
{
	void *data = src_data;
	D3DLOCKED_BOX lock = { 0 };
	D3DBOX lock_box = { 0 };
	int src_size, src_slice_size, src_row_size;
	HRESULT hrLockOperation = S_OK;

	lock_box.Left = x;
	lock_box.Top = y;
	lock_box.Front = z;
	lock_box.Right = x + width;
	lock_box.Bottom = y + height;
	lock_box.Back = z + depth;

	{
		U8 *dstptr, *srcptr = data;
		int row, rows, slice;
		U8 *sliceptr;

		src_row_size = rtexWidthToBytes(width, src_format);
		rows = HeightToRows(height, src_format);
		src_slice_size = rows * src_row_size;
		src_size = src_slice_size * depth;

		hrLockOperation = IDirect3DVolumeTexture9_LockBox(dst_texture.texture_3d_d3d9, mip_level, &lock, &lock_box, 0);
		if (FAILED(hrLockOperation))
		{
			rxbxFatalHResultErrorf(device, hrLockOperation, "locking a texture", "%s texture size %d x %d x %d (%s)", 
				rxbxGetTextureTypeString(RTEX_3D), width, height, depth, rdrTexFormatName(dst_format));
		}

		sliceptr = lock.pBits;
		assert(src_row_size <= lock.RowPitch); // Otherwise going to overrun the buffer!
		assert(src_slice_size <= lock.SlicePitch);

		for (slice = 0; slice < depth; ++slice)
		{
			dstptr = sliceptr;
			if ( src_format.format == RTEX_BGR_U8 )
			{
				int i, size = width;
				for (row = 0; row < rows; row++)
				{
					U8 *dst = dstptr;
					const U8 *src = srcptr;

					for (i = 0; i < size; i++)
					{
						DWORD dest = ( 0xff << 24 ) |
							( src[ 2 ] << 16 ) |
							( src[ 1 ] << 8 ) |
							( src[ 0 ] );
						*(int*)dst = dest;
						dst += sizeof( DWORD );
						src += sizeof( char ) * 3;
					}

					srcptr += src_row_size;
					dstptr += lock.RowPitch;
				}
			}
			else
			{
				for (row = 0; row < rows; row++)
				{
					memcpy(dstptr, srcptr, src_row_size);
					srcptr += src_row_size;
					dstptr += lock.RowPitch;
				}
			}
			sliceptr += lock.SlicePitch;
		}

		hrLockOperation = IDirect3DVolumeTexture9_UnlockBox(dst_texture.texture_3d_d3d9, mip_level);
		if (FAILED(hrLockOperation))
		{
			rxbxFatalHResultErrorf(device, hrLockOperation, "unlocking a texture", "%s texture size %d x %d x %d (%s)", 
				rxbxGetTextureTypeString(RTEX_3D), width, height, depth, rdrTexFormatName(dst_format));
		}
		if (mip_level && !device->d3d11_device) {
			CHECKX(IDirect3DVolumeTexture9_AddDirtyBox(dst_texture.texture_3d_d3d9, NULL));
		}
	}

	if (data != src_data)
		free(data);

	return src_size;
}

static void texExpandRGB(U8 *srcptr, int width, int height, U8 *dstptr, int src_pitch, int dst_pitch)
{
	int row, i;
	for (row = 0; row < height; row++)
	{
		U8 *dst = dstptr;
		const U8 *src = srcptr;

		for (i = 0; i < width; i++)
		{
			DWORD dest = ( 0xff << 24 ) |
				( src[ 2 ] << 16 ) |
				( src[ 1 ] << 8 ) |
				( src[ 0 ] );
			*(int*)dst = dest;
			dst += sizeof( DWORD );
			src += sizeof( char ) * 3;
		}

		srcptr += src_pitch;
		dstptr += dst_pitch;
	}
}

__forceinline U32 D3D11CalcSubresource( U32 MipSlice, U32 ArraySlice, U32 MipLevels )
{ return MipSlice + ArraySlice * MipLevels; }

static int copy3DTexSubImage11(RdrDeviceDX *device, RdrTextureDataDX *dst_texture, RdrTexFormatObj dst_format, int mip_level, void *src_data, RdrTexFormatObj src_format, int x, int y, int z, int width, int height, int depth, int orig_width, int orig_height, int orig_depth)
{
	U8 *data = src_data;
	D3D11_SHADER_RESOURCE_VIEW_DESC desc = { 0 };
	U32 src_row_size = rtexWidthToBytes(width, src_format);
	U32 dst_row_size = src_row_size;
	int rows = HeightToRows(height, src_format);
	int src_slice_size = rows * src_row_size;
	int dst_slice_size = src_slice_size;
	int src_size = src_slice_size * depth;
	ID3D11ShaderResourceView_GetDesc(dst_texture->texture.texture_view_d3d11, &desc);
	if (src_format.format == RTEX_BGR_U8 && dst_format.format == RTEX_BGRA_U8)
	{
		dst_row_size = rtexWidthToBytes(width, dst_format);
		dst_slice_size = dst_row_size * rows;
		data = ScratchAlloc(dst_row_size * rows * depth);
		texExpandRGB(src_data, width, height*depth, data, src_row_size, dst_row_size);
	}
	if (rdrTexIsCompressedFormat(src_format))
	{
		width = (width + 3) & ~3;
		height = (height + 3) & ~3;
	}
	{
		D3D11_BOX dest_box = {x, y, z, x + width, y + height, z + depth};
		ID3D11DeviceContext_UpdateSubresource(device->d3d11_imm_context, dst_texture->d3d11_data.resource_d3d11, 
			D3D11CalcSubresource(mip_level, 0, desc.Texture3D.MipLevels), &dest_box, data,
			dst_row_size, dst_slice_size);
	}
	if (src_data != data)
		ScratchFree(data);
	return src_size;
}

static int copy2DTexSubImage9(RdrDeviceDX *device, RdrTextureObj dst_texture, RdrTexType tex_type, int face, RdrTexFormatObj dst_format, int mip_level, void *src_data, RdrTexFormatObj src_format, int x, int y, int width, int height, int orig_width, int orig_height)
{
    void *data = src_data;
	D3DLOCKED_RECT lock;
	RECT lock_rect;
	int src_size, src_row_size;
	U8 *dstptr, *srcptr = data;
	int row, rows;
	HRESULT hr;

	lock_rect.left = x;
	lock_rect.top = y;
	lock_rect.right = x + width;
	lock_rect.bottom = y + height;

	src_row_size = rtexWidthToBytes(width, src_format);
	rows = HeightToRows(height, src_format);

	src_size = rows * src_row_size;

	D3DBaseTexture_LockRect(dst_texture.texture_base_d3d9, tex_type, face, mip_level, &lock, &lock_rect, hr);
	if (FAILED(hr))
	{
		rxbxFatalHResultErrorf(device, hr, "locking a texture", "%s texture size %d x %d (%s)", 
			rxbxGetTextureTypeString(tex_type), width, height, rdrTexFormatName(dst_format));
	}

	dstptr = lock.pBits;
	assert(src_row_size <= lock.Pitch); // Otherwise going to overrun the buffer!

	if (src_format.format == RTEX_BGR_U8 && dst_format.format == RTEX_BGRA_U8) // Expanded to 32-bit GPU format
	{
		texExpandRGB(srcptr, width, height, dstptr, src_row_size, lock.Pitch);
	}
	else
	{
		for (row = 0; row < rows; row++)
		{
			memcpy(dstptr, srcptr, src_row_size);
			srcptr += src_row_size;
			dstptr += lock.Pitch;
		}
	}

	D3DBaseTexture_UnlockRect(dst_texture.texture_base_d3d9, tex_type, face, mip_level, hr);
	if (FAILED(hr))
	{
		rxbxFatalHResultErrorf(device, hr, "unlocking a texture", "%s texture size %d x %d (%s)", 
			rxbxGetTextureTypeString(tex_type), width, height, rdrTexFormatName(dst_format));
	}
	if (mip_level && !device->d3d11_device) {
		D3DBaseTexture_Invalidate(dst_texture.texture_base_d3d9, tex_type);
	}

	// Nice for debugging.
#define DUMP_TEXTURES 0
#if DUMP_TEXTURES
	D3DXSaveTextureToFile( "c:\\temp\\tex.png",
		D3DXIFF_PNG,
		dst_texture.texture_base_d3d9,
		NULL );
#endif

	if (data != src_data)
		free(data);

	return src_size;
}

static int copy2DTexSubImage11(RdrDeviceDX *device, RdrTextureDataDX *dst_texture, RdrTexType tex_type, int face, RdrTexFormatObj dst_format, int mip_level, void *src_data, RdrTexFormatObj src_format, int x, int y, int width, int height, int orig_width, int orig_height)
{
	U8 *data = src_data;
	D3D11_SHADER_RESOURCE_VIEW_DESC desc = { 0 };
	U32 src_row_size = rtexWidthToBytes(width, src_format);
	U32 dst_row_size = src_row_size;
	int rows = HeightToRows(height, src_format);
	int src_size = rows * src_row_size;
	ID3D11ShaderResourceView_GetDesc(dst_texture->texture.texture_view_d3d11, &desc);
	// DX11TODO - DJR - ensure D3D11 code handles texture downres, calculates offsets correctly
	if (src_format.format == RTEX_BGR_U8 && dst_format.format == RTEX_BGRA_U8)
	{
		dst_row_size = rtexWidthToBytes(width, dst_format);
		data = ScratchAlloc(dst_row_size * rows);
		texExpandRGB(src_data, width, height, data, src_row_size, dst_row_size);
	}
	if (rdrTexIsCompressedFormat(src_format))
	{
		width = (width + 3) & ~3;
		height = (height + 3) & ~3;
	}
	{
		D3D11_BOX dest_box = {x, y, 0, x + width, y + height, 1};
		ID3D11DeviceContext_UpdateSubresource(device->d3d11_imm_context, dst_texture->d3d11_data.resource_d3d11, 
			D3D11CalcSubresource(mip_level, face, desc.Texture2D.MipLevels), &dest_box, data,
			dst_row_size, 0);
	}
	if (src_data != data)
		ScratchFree(data);
	return src_size;
}

__forceinline static int copyTexSubImage(RdrDeviceDX *device, RdrTextureDataDX *dst_texture, RdrTexType tex_type, int face, RdrTexFormatObj dst_format, int mip_level, void *src_data, RdrTexFormatObj src_format, int x, int y, int z, int width, int height, int depth, int orig_width, int orig_height, int orig_depth)
{
	if (tex_type == RTEX_3D)
	{
		if (device->d3d11_device)
			return copy3DTexSubImage11(device, dst_texture, dst_format, mip_level, src_data, src_format, x, y, z, width, height, depth, orig_width, orig_height, orig_depth);
		else
			return copy3DTexSubImage9(device, dst_texture->texture_sysmem.typeless ? dst_texture->texture_sysmem : dst_texture->texture, dst_format, mip_level, src_data, src_format, x, y, z, width, height, depth, orig_width, orig_height, orig_depth);
	}

	if (device->d3d11_device)
	{
		return copy2DTexSubImage11(device, dst_texture, tex_type, face, dst_format, mip_level, src_data, src_format, x, y, width, height, orig_width, orig_height);
	} else {
		return copy2DTexSubImage9(device, dst_texture->texture_sysmem.typeless ? dst_texture->texture_sysmem : dst_texture->texture, tex_type, face, dst_format, mip_level, src_data, src_format, x, y, width, height, orig_width, orig_height);
	}
}

static int getComponentCount(RdrTexFormatObj format, int *component_bytes, int *is_floating_point)
{
	int bytes = 0;
	int components = 0;
	int float_type = 0;

	switch (format.format)
	{
		xcase RTEX_BGR_U8:
		case RTEX_BGRA_U8:
		case RTEX_RGBA_U8:
		case RTEX_RGBA10:
			components = 4;
			bytes = 1;
		xcase RTEX_A16B16G16R16:
			components = 4;
			bytes = 2;

        xcase RTEX_RGBA_F16:
			components = 4;
			bytes = 2;
			float_type = 1;

		xcase RTEX_RGBA_F32:
			components = 4;
			bytes = 4;
			float_type = 1;

		xcase RTEX_G16R16:
			components = 2;
			bytes = 2;

		xcase RTEX_G16R16F:
			components = 2;
			bytes = 2;
			float_type = 1;

		xcase RTEX_R_F32:
			components = 1;
			bytes = 4;
			float_type = 1;

		xdefault:
			assert(0);
	}

	if (component_bytes)
		*component_bytes = bytes;

	if (is_floating_point)
		*is_floating_point = float_type;

	return components;
}

DXGI_FORMAT rxbxGetGPUFormat11(RdrTexFormatObj dst_format, bool bSRGB)
{
	DXGI_FORMAT format;

	switch (dst_format.format)
	{
		xcase RTEX_BGR_U8:
			format = bSRGB ? DXGI_FORMAT_B8G8R8X8_UNORM_SRGB : DXGI_FORMAT_B8G8R8X8_UNORM;
		xcase RTEX_BGRA_U8:
			format = bSRGB ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
		xcase RTEX_RGBA_U8:
			format = bSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
		xcase RTEX_RGBA_F16:
			format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		xcase RTEX_RGBA_F32:
			format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		xcase RTEX_R_F32:
			format = DXGI_FORMAT_R32_FLOAT;
		xcase RTEX_DXT1:
			format = bSRGB ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
		xcase RTEX_DXT3:
			format = bSRGB ? DXGI_FORMAT_BC2_UNORM_SRGB : DXGI_FORMAT_BC2_UNORM;
		xcase RTEX_DXT5:
			format = bSRGB ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
		xcase RTEX_BGRA_5551:
			assertmsg(0, "Not supported on DX11"); // we should not be using this format, gets converted at load time
			format = DXGI_FORMAT_R16_UNORM; // should be DXGI_FORMAT_B5G5R5A1_UNORM, but that errors, use the other to prevent DX error
			xcase RTEX_A_U8:
			format = DXGI_FORMAT_A8_UNORM;
		xcase RTEX_D24S8:
			format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		xcase RTEX_G16R16F:
			format = DXGI_FORMAT_R16G16_FLOAT;
		xcase RTEX_G16R16:
			format = DXGI_FORMAT_R16G16_UNORM;
		xcase RTEX_A16B16G16R16:
			format = DXGI_FORMAT_R16G16B16A16_UNORM;
		xcase RTEX_R5G6B5:
			// HACK!  Only used for depth-only surfaces that need a color surface, so just use a different light-weight surface type
			//assertmsg(0, "Not supported on DX11");
			format = DXGI_FORMAT_R8_UNORM;
		xcase RTEX_RGBA10:
			format = DXGI_FORMAT_R10G10B10A2_UNORM;

		xcase RTEX_D16:
			format = DXGI_FORMAT_D16_UNORM;

		xcase RTEX_NULL:
		case RTEX_NVIDIA_INTZ:
		case RTEX_NVIDIA_RAWZ:
		case RTEX_ATI_DF24:
		case RTEX_ATI_DF16:
		default:
			assertmsgf(0, "Unknown DXGI destination texture format : 0x%08x.", dst_format.format);
	}

	return format;
}

DXGI_FORMAT rxbxGetGPUSurfaceFormat11(RdrTexFormatObj dst_format, bool bSRGB)
{
	DXGI_FORMAT format;

	switch (dst_format.format)
	{
		xcase RTEX_BGR_U8:
			format = bSRGB ? DXGI_FORMAT_B8G8R8X8_UNORM_SRGB : DXGI_FORMAT_B8G8R8X8_UNORM;
		xcase RTEX_BGRA_U8:
			format = bSRGB ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
		xcase RTEX_RGBA_U8:
			format = bSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
		xcase RTEX_RGBA_F16:
			format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		xcase RTEX_RGBA_F32:
			format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		xcase RTEX_R_F32:
			format = DXGI_FORMAT_R32_FLOAT;
		xcase RTEX_DXT1:
			format = bSRGB ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
		xcase RTEX_DXT3:
			format = bSRGB ? DXGI_FORMAT_BC2_UNORM_SRGB : DXGI_FORMAT_BC2_UNORM;
		xcase RTEX_DXT5:
			format = bSRGB ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
		xcase RTEX_BGRA_5551:
			assertmsg(0, "Not supported on DX11"); // we should not be using this format, gets converted at load time
			format = DXGI_FORMAT_R16_UNORM; // should be DXGI_FORMAT_B5G5R5A1_UNORM, but that errors, use the other to prevent DX error
		xcase RTEX_A_U8:
			format = DXGI_FORMAT_A8_UNORM;
		xcase RTEX_D24S8:
			format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		xcase RTEX_G16R16F:
			format = DXGI_FORMAT_R16G16_FLOAT;
		xcase RTEX_G16R16:
			format = DXGI_FORMAT_R16G16_UNORM;
		xcase RTEX_A16B16G16R16:
			format = DXGI_FORMAT_R16G16B16A16_UNORM;
		xcase RTEX_R5G6B5:
			// HACK!  Only used for depth-only surfaces that need a color surface, so just use a different light-weight surface type
			//assertmsg(0, "Not supported on DX11");
			format = DXGI_FORMAT_R8_UNORM;
		xcase RTEX_RGBA10:
			format = DXGI_FORMAT_R10G10B10A2_UNORM;

		xcase RTEX_D16:
			format = DXGI_FORMAT_D16_UNORM;

		xcase RTEX_NULL:
		case RTEX_NVIDIA_INTZ:
		case RTEX_NVIDIA_RAWZ:
		case RTEX_ATI_DF24:
		case RTEX_ATI_DF16:
		default:
			assertmsgf(0, "Unknown DXGI destination texture format : 0x%08x.", dst_format.format);
	}

	return format;
}

DXGI_FORMAT getGPUTypelessFormat11(RdrTexFormatObj dst_format)
{
	DXGI_FORMAT format;

	switch (dst_format.format)
	{
		xcase RTEX_BGR_U8:
			format = DXGI_FORMAT_B8G8R8X8_TYPELESS;
		xcase RTEX_BGRA_U8:
			format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
		xcase RTEX_RGBA_U8:
			format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
		xcase RTEX_RGBA_F16:
			format = DXGI_FORMAT_R16G16B16A16_TYPELESS;
		xcase RTEX_RGBA_F32:
			format = DXGI_FORMAT_R32G32B32A32_TYPELESS;
		xcase RTEX_R_F32:
			format = DXGI_FORMAT_R32_TYPELESS;
		xcase RTEX_DXT1:
			format = DXGI_FORMAT_BC1_TYPELESS;
		xcase RTEX_DXT3:
			format = DXGI_FORMAT_BC2_TYPELESS;
		xcase RTEX_DXT5:
			format = DXGI_FORMAT_BC3_TYPELESS;
		xcase RTEX_BGRA_5551:
			assertmsg(0, "Not supported on DX11"); // we should not be using this format, gets converted at load time
			format = DXGI_FORMAT_R16_TYPELESS; // should be DXGI_FORMAT_B5G5R5A1_UNORM, but that errors, use the other to prevent DX error
		xcase RTEX_A_U8:
			format = DXGI_FORMAT_R8_TYPELESS;
		xcase RTEX_D24S8:
			format = DXGI_FORMAT_R24G8_TYPELESS;
		xcase RTEX_G16R16F:
			format = DXGI_FORMAT_R16G16_TYPELESS;
		xcase RTEX_G16R16:
			format = DXGI_FORMAT_R16G16_TYPELESS;
		xcase RTEX_A16B16G16R16:
			format = DXGI_FORMAT_R16G16B16A16_TYPELESS;
		xcase RTEX_R5G6B5:
			// HACK!  Only used for depth-only surfaces that need a color surface, so just use a different light-weight surface type
			//assertmsg(0, "Not supported on DX11");
			format = DXGI_FORMAT_R8_TYPELESS;
		xcase RTEX_RGBA10:
			format = DXGI_FORMAT_R10G10B10A2_TYPELESS;

		xcase RTEX_D16:
			format = DXGI_FORMAT_R16_TYPELESS;

		xcase RTEX_NULL:
		case RTEX_NVIDIA_INTZ:
		case RTEX_NVIDIA_RAWZ:
		case RTEX_ATI_DF24:
		case RTEX_ATI_DF16:
		default:
			assertmsgf(0, "Unknown DXGI destination texture format : 0x%08x.", dst_format.format);
	}

	return format;
}

DXGI_FORMAT getDXGITypelessFormat11(DXGI_FORMAT format)
{
	DXGI_FORMAT typeless_format = format;

	switch (format)
	{
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_UINT:
		case DXGI_FORMAT_R32G32B32A32_SINT:
			typeless_format = DXGI_FORMAT_R32G32B32A32_TYPELESS;

		xcase DXGI_FORMAT_R32G32B32_TYPELESS:
		case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R32G32B32_UINT:
		case DXGI_FORMAT_R32G32B32_SINT:
			typeless_format = DXGI_FORMAT_R32G32B32_TYPELESS;
		
		xcase DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SNORM:
		case DXGI_FORMAT_R16G16B16A16_SINT:
			typeless_format = DXGI_FORMAT_R16G16B16A16_TYPELESS;

		xcase DXGI_FORMAT_R32G32_TYPELESS:
		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
		case DXGI_FORMAT_R32G32_SINT:
			typeless_format = DXGI_FORMAT_R32G32_TYPELESS;

		xcase DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			typeless_format = DXGI_FORMAT_R32G8X24_TYPELESS;

		xcase DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UINT:
			typeless_format = DXGI_FORMAT_R10G10B10A2_TYPELESS;

		xcase DXGI_FORMAT_R11G11B10_FLOAT:
			typeless_format = DXGI_FORMAT_R11G11B10_FLOAT;

		xcase DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_SINT:
			typeless_format = DXGI_FORMAT_R8G8B8A8_TYPELESS;

		xcase DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SNORM:
		case DXGI_FORMAT_R16G16_SINT:
			typeless_format = DXGI_FORMAT_R16G16_TYPELESS;

		xcase DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
			typeless_format = DXGI_FORMAT_R32_TYPELESS;

		xcase DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			typeless_format = DXGI_FORMAT_R24G8_TYPELESS;

		xcase DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SNORM:
		case DXGI_FORMAT_R8G8_SINT:
			typeless_format = DXGI_FORMAT_R8G8_TYPELESS;

		xcase DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SNORM:
		case DXGI_FORMAT_R16_SINT:
			typeless_format = DXGI_FORMAT_R16_TYPELESS;

		xcase DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_R8_SINT:
			typeless_format = DXGI_FORMAT_R8_TYPELESS;

		xcase DXGI_FORMAT_A8_UNORM:
			assert(0); // don't know if this has a typeless format; is it DXGI_FORMAT_R8_TYPELESS?
		xcase DXGI_FORMAT_R1_UNORM:
			assert(0); // don't know if this has a typeless format?

		xcase DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
		case DXGI_FORMAT_R8G8_B8G8_UNORM:
		case DXGI_FORMAT_G8R8_G8B8_UNORM:
			typeless_format = format;

		xcase DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
			typeless_format = DXGI_FORMAT_BC1_TYPELESS;

		xcase DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
			typeless_format = DXGI_FORMAT_BC2_TYPELESS;

		xcase DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
			typeless_format = DXGI_FORMAT_BC3_TYPELESS;

		xcase DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			typeless_format = DXGI_FORMAT_BC4_TYPELESS;

		xcase DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
			typeless_format = DXGI_FORMAT_BC5_TYPELESS;

		xcase DXGI_FORMAT_B5G6R5_UNORM:
		case DXGI_FORMAT_B5G5R5A1_UNORM:
			typeless_format = format;

		xcase DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
			typeless_format = format;

		xcase DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			typeless_format = DXGI_FORMAT_B8G8R8A8_TYPELESS;

		xcase DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			typeless_format = DXGI_FORMAT_B8G8R8X8_TYPELESS;

		xcase DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
			typeless_format = DXGI_FORMAT_BC6H_TYPELESS;

		xcase DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			typeless_format = DXGI_FORMAT_BC7_TYPELESS;
	}

	return typeless_format;
}

RdrTexFormat rxbxGetRdrTexFormat11(DXGI_FORMAT dxgiFormat)
{
	RdrTexFormat format;

	switch (dxgiFormat)
	{
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		format = RTEX_BGR_U8;
	xcase DXGI_FORMAT_B8G8R8A8_TYPELESS:
		format = RTEX_BGRA_U8;
	xcase DXGI_FORMAT_R8G8B8A8_TYPELESS:
		format = RTEX_RGBA_U8;
	xcase DXGI_FORMAT_R16G16B16A16_TYPELESS:
		format = RTEX_RGBA_F16;	//RTEX_A16B16G16R16?
	xcase DXGI_FORMAT_R32G32B32A32_TYPELESS:
		format = RTEX_RGBA_F32;
	xcase DXGI_FORMAT_R32G32B32A32_FLOAT:
		format = RTEX_RGBA_F32;
	xcase DXGI_FORMAT_R32_TYPELESS:
		format = RTEX_R_F32;
	xcase DXGI_FORMAT_BC1_TYPELESS:
		format = RTEX_DXT1;
	xcase DXGI_FORMAT_BC2_TYPELESS:
		format = RTEX_DXT3;
	xcase DXGI_FORMAT_BC3_TYPELESS:
		format = RTEX_DXT5;
	xcase DXGI_FORMAT_R8_TYPELESS:
		format = RTEX_A_U8;
	xcase DXGI_FORMAT_R24G8_TYPELESS:
		format = RTEX_D24S8;
	xcase DXGI_FORMAT_R16G16_TYPELESS:
		format = RTEX_G16R16F;
	xcase DXGI_FORMAT_R10G10B10A2_TYPELESS:
		format = RTEX_RGBA10;

	xcase DXGI_FORMAT_R16_TYPELESS:
		format = RTEX_D16;

	default:
		assertmsgf(0, "Unknown DXGI src texture format : 0x%08x.", dxgiFormat);
	}

	return format;
}

D3DFORMAT rxbxGetGPUFormat9(RdrTexFormatObj dst_format)
{
	D3DFORMAT format;

	switch (dst_format.format)
	{
		xcase RTEX_BGR_U8:
			format = D3DFMT_X8R8G8B8;
		xcase RTEX_BGRA_U8:
			format = D3DFMT_A8R8G8B8;
		xcase RTEX_RGBA_U8:
			format = D3DFMT_A8B8G8R8;
		xcase RTEX_RGBA_F16:
			format = D3DFMT_A16B16G16R16F;
		xcase RTEX_RGBA_F32:
			format = D3DFMT_A32B32G32R32F;
		xcase RTEX_R_F32:
			format = D3DFMT_R32F;
		xcase RTEX_DXT1:
			format = D3DFMT_DXT1;
		xcase RTEX_DXT3:
			format = D3DFMT_DXT3;
		xcase RTEX_DXT5:
			format = D3DFMT_DXT5;
		xcase RTEX_BGRA_5551:
			format = D3DFMT_A1R5G5B5;
		xcase RTEX_A_U8:
			format = D3DFMT_A8;
		xcase RTEX_NULL:
			format = D3DFMT_NULL_TEXTURE_FCC;
		xcase RTEX_NVIDIA_INTZ:
			format = D3DFMT_NVIDIA_INTZ_DEPTH_TEXTURE_FCC;
		xcase RTEX_NVIDIA_RAWZ:
			format = D3DFMT_NVIDIA_RAWZ_DEPTH_TEXTURE_FCC;
		xcase RTEX_ATI_DF16:
			format = D3DFMT_ATI_DEPTH_TEXTURE_16_FCC;
		xcase RTEX_ATI_DF24:
			format = D3DFMT_ATI_DEPTH_TEXTURE_24_FCC;
		xcase RTEX_D24S8:
			format = D3DFMT_D24S8;
		xcase RTEX_D16:
			format = D3DFMT_D16;
		xcase RTEX_G16R16F:
			format = D3DFMT_G16R16F;
		xcase RTEX_G16R16:
			format = D3DFMT_G16R16;
		xcase RTEX_A16B16G16R16:
			format = D3DFMT_A16B16G16R16;
		xcase RTEX_R5G6B5:
			format = D3DFMT_R5G6B5;
		xcase RTEX_RGBA10:
			format = D3DFMT_A2B10G10R10;
		xdefault:
            assertmsgf(0, "Unknown destination texture format : 0x%08x.", dst_format.format);
	}

	return format;
}

RdrTexFormat rxbxGetRdrTexFormat9(D3DFORMAT d3dFormat)
{
	RdrTexFormat format;

	switch (d3dFormat)
	{
		xcase D3DFMT_X8R8G8B8:
			format = RTEX_BGR_U8;
		xcase D3DFMT_A8R8G8B8:
			format = RTEX_BGRA_U8;
		xcase D3DFMT_A8B8G8R8:
			format = RTEX_RGBA_U8;
		xcase D3DFMT_A16B16G16R16F:
			format = RTEX_RGBA_F16;
		xcase D3DFMT_A32B32G32R32F:
			format = RTEX_RGBA_F32;
		xcase D3DFMT_R32F:
			format = RTEX_R_F32;
		xcase D3DFMT_DXT1:
			format = RTEX_DXT1;
		xcase D3DFMT_DXT3:
			format = RTEX_DXT3;
		xcase D3DFMT_DXT5:
			format = RTEX_DXT5;
		xcase D3DFMT_A1R5G5B5:
			format = RTEX_BGRA_5551;
		xcase D3DFMT_A8:
			format = RTEX_A_U8;
		xcase D3DFMT_NULL_TEXTURE_FCC:
			format = RTEX_NULL;
		xcase D3DFMT_NVIDIA_INTZ_DEPTH_TEXTURE_FCC:
			format = RTEX_NVIDIA_INTZ;
		xcase D3DFMT_NVIDIA_RAWZ_DEPTH_TEXTURE_FCC:
			format = RTEX_NVIDIA_RAWZ;
		xcase D3DFMT_ATI_DEPTH_TEXTURE_16_FCC:
			format = RTEX_ATI_DF16;
		xcase D3DFMT_ATI_DEPTH_TEXTURE_24_FCC:
			format = RTEX_ATI_DF24;
		xcase D3DFMT_D24S8:
			format = RTEX_D24S8;
		xcase D3DFMT_D16:
			format = RTEX_D16;
		xcase D3DFMT_G16R16F:
			format = RTEX_G16R16F;
		xcase D3DFMT_G16R16:
			format = RTEX_G16R16;
		xcase D3DFMT_A16B16G16R16:
			format = RTEX_A16B16G16R16;
		xcase D3DFMT_R5G6B5:
			format = RTEX_R5G6B5;
		xcase D3DFMT_A2B10G10R10:
			format = RTEX_RGBA10;
		xdefault:
			assertmsgf(0, "Unknown destination texture format : 0x%08x.", d3dFormat);
	}

	return format;
}

__forceinline RdrDXTexFormatObj rxbxGetGPUSurfaceFormat(RdrDeviceDX *device, RdrTexFormatObj format)
{
	RdrDXTexFormatObj gpu_format;
	if (device->d3d11_device)
		gpu_format.format_d3d11 = getGPUTypelessFormat11(format);
	else
		gpu_format.format_d3d9 = rxbxGetGPUFormat9(format);
	return gpu_format;
}

static bool isDXGIFormatRGBA(DXGI_FORMAT format)
{
	return (format == DXGI_FORMAT_B8G8R8X8_TYPELESS || format == DXGI_FORMAT_B8G8R8A8_TYPELESS ||
		format == DXGI_FORMAT_R8G8B8A8_TYPELESS);
}

static bool isCompatibleTexFormat(RdrDeviceDX *device, RdrDXTexFormatObj fmtA, RdrDXTexFormatObj fmtB)
{
	if (fmtA.format_typeless == fmtB.format_typeless)
		return 1;
	if (device->d3d11_device)
	{
		if (isDXGIFormatRGBA(fmtA.format_d3d11) == isDXGIFormatRGBA(fmtB.format_d3d11))
			return 1;
	}
	else
	{
		if (fmtA.format_d3d9 == D3DFMT_X8B8G8R8 && fmtB.format_d3d9 == D3DFMT_A8B8G8R8 ||
			fmtA.format_d3d9 == D3DFMT_A8B8G8R8 && fmtB.format_d3d9 == D3DFMT_X8B8G8R8)
			return 1;
		if (fmtA.format_d3d9 == D3DFMT_X8R8G8B8 && fmtB.format_d3d9 == D3DFMT_A8R8G8B8 ||
			fmtA.format_d3d9 == D3DFMT_A8R8G8B8 && fmtB.format_d3d9 == D3DFMT_X8R8G8B8)
			return 1;
	}
	return 0;
}

static void rxbxTextureDataReleaseSystemMemory(RdrDeviceDX *device, RdrTextureDataDX *tex_data)
{
	if (tex_data->texture_sysmem.texture_base_d3d9)
	{
		IDirect3DBaseTexture9_Release(tex_data->texture_sysmem.texture_base_d3d9);
		tex_data->texture_sysmem.texture_base_d3d9 = NULL;
	}
}

static void releaseTexture(RdrDeviceDX *device, RdrTextureDataDX *tex_data)
{
	if (tex_data->texture.typeless || (device->d3d11_blend_states && tex_data->d3d11_data.typeless)) // May be NULL if it was a non-managed texture, and the device was resized/lost
	{
		int ref_count = 0;
		rxbxNotifyTextureFreed(device, tex_data);

		rxbxTextureDataReleaseSystemMemory(device, tex_data);
		if (device->d3d11_device && tex_data->texture.texture_view_d3d11)
		{
			ID3D11ShaderResourceView_Release(tex_data->texture.texture_view_d3d11);
			tex_data->texture.texture_view_d3d11 = NULL;
		}

		switch (tex_data->tex_type)
		{
			xcase RTEX_2D:
				if (device->d3d11_device)
				{
					ref_count = ID3D11Texture2D_Release(tex_data->d3d11_data.texture_2d_d3d11);
				} else {
					ref_count = IDirect3DTexture9_Release(tex_data->texture.texture_2d_d3d9);
				}
				rxbxLogReleaseTexture(device, tex_data, ref_count);
			xcase RTEX_3D:
				if (device->d3d11_device)
				{
					ref_count = ID3D11Texture3D_Release(tex_data->d3d11_data.texture_3d_d3d11);
				} else {
					ref_count = IDirect3DVolumeTexture9_Release(tex_data->texture.texture_2d_d3d9);
				}
				rxbxLogReleaseVolumeTexture(device, tex_data, ref_count);
			xcase RTEX_CUBEMAP:
				if (device->d3d11_device)
				{
					ref_count = ID3D11Texture2D_Release(tex_data->d3d11_data.texture_2d_d3d11);
				} else {
					ref_count = IDirect3DCubeTexture9_Release(tex_data->texture.texture_cube_d3d9);
				}
				rxbxLogReleaseCubeTexture(device, tex_data, ref_count);
			xdefault:
				assert(0);
		}

		if (ref_count)
		{
			memlog_printf(0, "Texture still referenced after we released it %d refs Dev 0x%p RdrTextureDataDX 0x%p W %d H %d \n",
				ref_count, device, tex_data, tex_data->width, tex_data->height );
		}
		rdrTrackUserMemoryDirect(&device->device_base, tex_data->memmonitor_name?tex_data->memmonitor_name:"rxbx:Textures", 1, -tex_data->memory_usage);
		tex_data->texture.typeless = NULL;
		tex_data->d3d11_data.texture_3d_d3d11 = NULL;
	} else {
		if (device->d3d11_device)
		{
			assert(!tex_data->d3d11_data.typeless); // Otherwise leaking
		}
	}
}

__forceinline static D3D11_BIND_FLAG getGPUTexUsage11(RdrSurfaceBindFlags surface_usage_flags)
{
	D3D11_BIND_FLAG bind_flags = 0;
	if (surface_usage_flags & RSBF_DEPTHSTENCIL)
		bind_flags |= D3D11_BIND_DEPTH_STENCIL;
	if (surface_usage_flags & RSBF_RENDERTARGET)
		bind_flags |= D3D11_BIND_RENDER_TARGET;
	if (surface_usage_flags & RSBF_TEX2D)
		bind_flags |= D3D11_BIND_SHADER_RESOURCE;
	return bind_flags;
}

__forceinline static int rxbxIsGPUDepthFormat9(RdrTexFormatObj tex_format)
{
	return tex_format.format >= RTEX_NVIDIA_INTZ && tex_format.format <= RTEX_D16;
}

__forceinline static DWORD getGPUTexUsage9(RdrSurfaceBindFlags surface_usage_flags, RdrTexFormatObj tex_format)
{
	DWORD tex_usage = 0;
	if ((surface_usage_flags & RSBF_DEPTHSTENCIL) || rxbxIsGPUDepthFormat9(tex_format))
	{
		assert(!(surface_usage_flags & RSBF_RENDERTARGET));
		tex_usage = D3DUSAGE_DEPTHSTENCIL;
	}
	else
	if (surface_usage_flags & RSBF_RENDERTARGET)
		tex_usage = D3DUSAGE_RENDERTARGET;
	return tex_usage;
}

__forceinline D3DPOOL getGPUTexPool9(const RdrDeviceDX *device, RdrSurfaceBindFlags texture_usage, bool managed)
{
	return (texture_usage & RSBF_SYSTEMMEMORY) ? D3DPOOL_SYSTEMMEM : 
		(managed?D3DPOOL_MANAGED:D3DPOOL_DEFAULT);
}

static HRESULT rxbxTextureCreate2D(RdrDeviceDX *device, U32 width, U32 height, U32 num_levels,
						  RdrSurfaceBindFlags texture_usage, RdrTexFormatObj dst_format,
						  bool managed, bool bSRGB, RdrTextureDataDX *tex_data, int multisample_count)
{
	HRESULT ret;
	if (device->d3d11_device)
	{
		D3D11_TEXTURE2D_DESC desc = {0};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = num_levels;
		desc.ArraySize = 1;
		desc.Format = texture_usage == RSBF_TEX2D ? rxbxGetGPUFormat11(dst_format, bSRGB) : getGPUTypelessFormat11(dst_format);
		desc.SampleDesc.Count = multisample_count ? multisample_count : 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT; // Maybe D3D11_USAGE_DYNAMIC if "managed"?
		desc.BindFlags = getGPUTexUsage11(texture_usage);
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		ret = ID3D11Device_CreateTexture2D(device->d3d11_device, &desc, NULL, &tex_data->d3d11_data.texture_2d_d3d11);
		devassert(!FAILED(ret)); // in production this will return and hit a general fatal error, want to debug here in development
		if (!FAILED(ret) && (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE))
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC texture_view;
			texture_view.Format = rxbxGetGPUFormat11(dst_format, bSRGB);
			texture_view.ViewDimension = multisample_count > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
			texture_view.Texture2D.MostDetailedMip = 0;
			texture_view.Texture2D.MipLevels = num_levels;

			ret = ID3D11Device_CreateShaderResourceView(device->d3d11_device, tex_data->d3d11_data.resource_d3d11, &texture_view, &tex_data->texture.texture_view_d3d11);
			devassert(!FAILED(ret)); // in production this will return and hit a general fatal error, want to debug here in development
		}

	} else {
		DWORD surface_tex_usage = getGPUTexUsage9(texture_usage, dst_format);
		D3DPOOL tex_pool = getGPUTexPool9(device, texture_usage, managed);
		CHECKX(ret = IDirect3DDevice9_CreateTexture(device->d3d_device, width, height, num_levels,
			surface_tex_usage, rxbxGetGPUFormat9(dst_format),
			tex_pool, texture_usage & RSBF_SYSTEMMEMORY ?  &tex_data->texture_sysmem.texture_2d_d3d9 : &tex_data->texture.texture_2d_d3d9, NULL));
	}
	return ret;
}

void rxbxDumpMemInfo(const char * p_pszEvent)
{
	MEMORYSTATUSEX memoryStatus;

	ZeroStruct(&memoryStatus);
	memoryStatus.dwLength = sizeof(memoryStatus);

	GlobalMemoryStatusEx(&memoryStatus);

	OutputDebugStringf("%s\nVirt space %"FORM_LL"d K\n", p_pszEvent ? p_pszEvent : "Log",
		(S64)(memoryStatus.ullTotalVirtual - memoryStatus.ullAvailVirtual) / 1024LL);

	if (nv_api_avail)
	{
		NV_DISPLAY_DRIVER_MEMORY_INFO memory_info;

		memory_info.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER_2;
		NvAPI_GPU_GetMemoryInfo(NVAPI_DEFAULT_HANDLE, &memory_info);

		OutputDebugStringf("VideoMemUsedNV %"FORM_LL"d K\n", (S64)(memory_info.dedicatedVideoMemory - memory_info.curAvailableDedicatedVideoMemory));
		OutputDebugStringf("VideoMemFreeNV %"FORM_LL"d K\n", (S64)memory_info.curAvailableDedicatedVideoMemory);
	}
}

#if D3D_RESOURCE_LEAK_TEST_ENABLE
static U32 debug_totalLeakedTex = 0;
static bool debug_d3dFailureMode = 0;
// DJR Uncomment this if you set D3D_RESOURCE_LEAK_TEST_ENABLE to 1
//AUTO_CMD_INT(debug_d3dFailureMode, debug_d3dFailureMode);

#define LEAK_TEX_WIDTH 2048
#define LEAK_TEX_HEIGHT 2048
#define LEAK_PER_ITER 64

void rxbxResourceLeakTest(RdrDeviceDX *device)
{
	int numLeakTex = LEAK_PER_ITER;
	if (!debug_d3dFailureMode)
		return;
	rxbxDumpMemInfo("before leak");
	for (; numLeakTex; --numLeakTex)
	{
		if (debug_d3dFailureMode == 1)
		{
			VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_NOACCESS);
		}
		else
			if (debug_d3dFailureMode > 1)
			{
				int width = LEAK_TEX_WIDTH, height = LEAK_TEX_HEIGHT, texture_usage = RSBF_TEX2D;
				RdrTexFormatObj tex_format = { RTEX_BGRA_U8 };
				RdrTextureDataDX texture_struct = { 0 };
				int multisample_count = 1;
				HRESULT hr;
				DWORD gle = 0;
				SetLastError(0);

				if (debug_d3dFailureMode > 3)
					width = -1;

				hr = rxbxTextureCreate2D(device, width, height, 1, texture_usage, tex_format, 
					true, false, &texture_struct, multisample_count);
				gle = GetLastError();

				OutputDebugStringf("Tex HR %x GLE() = %d\n", hr, gle);

				if (debug_d3dFailureMode > 2)
				{
					if (FAILED(hr))
					{
						int texSize = rxbxGetTextureSize(width, height, 1, tex_format, 1, RTEX_2D);
						void *pMemT, *pMem1, *pMem2, *pMem3, *pMem4, *pMem5;
						// attempt VirtualAllocs!
						rxbxDumpMemInfo("before VirtualAllocs\n");
						pMemT = VirtualAlloc(NULL, texSize, MEM_COMMIT, PAGE_NOACCESS);
						pMem1 = VirtualAlloc(NULL, 64*1024*1024, MEM_COMMIT, PAGE_NOACCESS);
						pMem2 = VirtualAlloc(NULL, 16*1024*1024, MEM_COMMIT, PAGE_NOACCESS);
						pMem3 = VirtualAlloc(NULL, 8*1024*1024, MEM_COMMIT, PAGE_NOACCESS);
						pMem4 = VirtualAlloc(NULL, 16*1024, MEM_COMMIT, PAGE_NOACCESS);
						pMem5 = VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_NOACCESS);
						rxbxDumpMemInfo("after VirtualAllocs\n");
						OutputDebugStringf("TexBuf = 0x%p Mem64MB = 0x%p Mem16MB = 0x%p Mem8MB = 0x%p Mem16K = 0x%p Mem4K = 0x%p\n", pMemT, pMem1, pMem2, pMem3, pMem4, pMem5);
					}
					rxbxFatalHResultErrorf(device, hr, "creating a texture", "%s while creating %s texture size %d x %d (%d mips, %s, usage %d, %d samples)", 
						rxbxGetStringForHResult(hr), rxbxGetTextureTypeString(RTEX_2D), width, height, 1, rdrTexFormatName(tex_format), texture_usage, multisample_count);
				}
			}
			//if (!FAILED(hr))
			//	rxbxLogCreateTexture(device, &texture_struct);
	}
	debug_totalLeakedTex += LEAK_PER_ITER;
	rxbxDumpMemInfo("after leak");
}
#endif

HRESULT rxbxTextureCreateCube(RdrDeviceDX *device, U32 width, U32 num_levels,
							RdrSurfaceBindFlags texture_usage, RdrTexFormatObj dst_format,
							bool managed, bool bSRGB, RdrTextureDataDX *tex_data)
{
	HRESULT ret;
	if (device->d3d11_device)
	{
		D3D11_TEXTURE2D_DESC desc = {0};
		desc.Width = width;
		desc.Height = width;
		desc.MipLevels = num_levels;
		desc.ArraySize = 6;
		desc.Format = getGPUTypelessFormat11(dst_format);
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT; // Maybe D3D11_USAGE_DYNAMIC if "managed"?
		desc.BindFlags = getGPUTexUsage11(texture_usage);
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
		ret = ID3D11Device_CreateTexture2D(device->d3d11_device, &desc, NULL, &tex_data->d3d11_data.texture_2d_d3d11);
		devassert(!FAILED(ret)); // in production this will return and hit a general fatal error, want to debug here in development
		if (!FAILED(ret))
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC texture_view;
			texture_view.Format = rxbxGetGPUFormat11(dst_format, bSRGB);
			texture_view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
			texture_view.TextureCube.MostDetailedMip = 0;
			texture_view.TextureCube.MipLevels = num_levels;

			ret = ID3D11Device_CreateShaderResourceView(device->d3d11_device, tex_data->d3d11_data.resource_d3d11,
				&texture_view, &tex_data->texture.texture_view_d3d11);
			devassert(!FAILED(ret)); // in production this will return and hit a general fatal error, want to debug here in development
		}
	} else {
		DWORD surface_tex_usage = getGPUTexUsage9(texture_usage, dst_format);
		D3DPOOL tex_pool = getGPUTexPool9(device, texture_usage, managed);
		CHECKX(ret = IDirect3DDevice9_CreateCubeTexture(device->d3d_device, width, num_levels,
			surface_tex_usage, rxbxGetGPUFormat9(dst_format),
			tex_pool, texture_usage & RSBF_SYSTEMMEMORY ?  &tex_data->texture_sysmem.texture_cube_d3d9 : &tex_data->texture.texture_cube_d3d9, NULL));
	}
	return ret;
}


HRESULT rxbxTextureCreate3D(RdrDeviceDX *device, U32 width, U32 height, U32 depth, U32 num_levels,
							  RdrSurfaceBindFlags texture_usage, RdrTexFormatObj dst_format,
							  bool managed, bool bSRGB, RdrTextureDataDX *tex_data)
{
	HRESULT ret;
	if (device->d3d11_device)
	{
		D3D11_TEXTURE3D_DESC desc = {0};
		desc.Width = width;
		desc.Height = height;
		desc.Depth = depth;
		desc.MipLevels = num_levels;
		desc.Format = rxbxGetGPUFormat11(dst_format, bSRGB);
		desc.Usage = D3D11_USAGE_DEFAULT; // Maybe D3D11_USAGE_DYNAMIC if "managed"?
		desc.BindFlags = getGPUTexUsage11(texture_usage);
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		ret = ID3D11Device_CreateTexture3D(device->d3d11_device, &desc, NULL, &tex_data->d3d11_data.texture_3d_d3d11);
		if (!FAILED(ret))
			ret = ID3D11Device_CreateShaderResourceView(device->d3d11_device, tex_data->d3d11_data.resource_d3d11, NULL, &tex_data->texture.texture_view_d3d11);
	} else {
		DWORD surface_tex_usage = getGPUTexUsage9(texture_usage, dst_format);
		D3DPOOL tex_pool = getGPUTexPool9(device, texture_usage, managed);
		CHECKX(ret = IDirect3DDevice9_CreateVolumeTexture(device->d3d_device, width, height, depth, num_levels,
			surface_tex_usage, rxbxGetGPUFormat9(dst_format),
			tex_pool, texture_usage & RSBF_SYSTEMMEMORY ?  &tex_data->texture_sysmem.texture_3d_d3d9 : &tex_data->texture.texture_3d_d3d9, NULL));
	}
	return ret;
}

HRESULT rxbxCreateTextureResource(RdrDeviceDX *device, RdrTexType tex_type, U32 width, U32 height, U32 depth, 
	int num_levels, RdrSurfaceBindFlags texture_usage, RdrTexFormatObj dst_format, bool managed, bool is_gamma_space, 
	RdrTextureDataDX *tex_data)
{
	HRESULT hrCreateTex = S_FALSE;
	switch (tex_type)
	{
		xcase RTEX_2D:
		{
			hrCreateTex = rxbxTextureCreate2D(device, width, height, num_levels,
				texture_usage, dst_format,
				managed, is_gamma_space, tex_data, 0);
			if (!FAILED(hrCreateTex))
				rxbxLogCreateTexture(device, tex_data);
		}

		xcase RTEX_3D:
		{
			hrCreateTex = rxbxTextureCreate3D(device, width, height, depth, num_levels,
				texture_usage, dst_format,
				managed, is_gamma_space, tex_data);
			if (!FAILED(hrCreateTex))
				rxbxLogCreateVolumeTexture(device, tex_data);
		}

		xcase RTEX_CUBEMAP:
		{
			assert(width == height);
			hrCreateTex = rxbxTextureCreateCube(device, width, num_levels,
				texture_usage, dst_format,
				managed, is_gamma_space, tex_data);
			if (!FAILED(hrCreateTex))
				rxbxLogCreateCubeTexture(device, tex_data);
		}

		xdefault:
		{
			assert(0);
		}
	}

	if (FAILED(hrCreateTex))
	{
		if (tex_type == RTEX_3D)
			rxbxFatalHResultErrorf(device, hrCreateTex, "creating a texture", "%s while creating %s texture size %d x %d x %d (%d mips, %s, usage %d, Mgd %c, GS %c)", 
				rxbxGetStringForHResult(hrCreateTex), rxbxGetTextureTypeString(tex_type), width, height, depth, num_levels, rdrTexFormatName(dst_format), 
				texture_usage, managed ? 'Y' : 'N', is_gamma_space ? 'Y' : 'N');
		else
			rxbxFatalHResultErrorf(device, hrCreateTex, "creating a texture", "%s while creating %s texture size %d x %d (%d mips, %s, usage %d, Mgd %c, GS %c)", 
				rxbxGetStringForHResult(hrCreateTex), rxbxGetTextureTypeString(tex_type), width, height, num_levels, rdrTexFormatName(dst_format), 
				texture_usage, managed ? 'Y' : 'N', is_gamma_space ? 'Y' : 'N');
	}
	return hrCreateTex;
}

void rxbxSetTextureDataDirect(RdrDeviceDX *device, RdrTexParams *rtex, WTCmdPacket *packet)
{
	U32		width = rtex->width, height = rtex->height, depth = rtex->depth;
	U32 dataSize;
	U8		*bitmap = (U8*)(rtex->data);
	HRESULT hrCreateSysMemTex = S_OK;
	bool bCreateWithUpdateTexture = false;
	bool bKeepSystemMemoryCopy = false;
	RdrTextureDataDX *tex_data = NULL;
	RdrSurfaceBuffer src_buffer_num = 0;
	int src_set_index;
	RdrSurfaceDX *src_surface=NULL;
	RdrTexHandle tex_handle = TexHandleToRdrTexHandle(rtex->tex_handle);
	RdrSurfaceBindFlags texture_usage = RSBF_TEX2D;
	int num_levels = rtex->max_levels;
	RdrTexFormatObj dst_format;
	U32 dst_srgb = rtex->is_srgb;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

    if (rtex->from_surface) {
		RdrDXTexFormatObj src_format;
		RdrTexHandle src_tex_handle = TexHandleToRdrTexHandle(rtex->src_tex_handle);
		assert(rtex->type == RTEX_2D);
		assert(src_tex_handle.is_surface); // Must be a surface
		src_surface = (RdrSurfaceDX*)rdrGetSurfaceForTexHandle(src_tex_handle);
		assert(src_surface);
		src_buffer_num = src_tex_handle.surface.buffer;
		assert(src_buffer_num >= 0 && src_buffer_num < ARRAY_SIZE(src_surface->rendertarget));
		src_set_index = src_tex_handle.surface.set_index;

		//if (src_set_index == RDRSURFACE_SET_INDEX_DEFAULT)
		//	src_set_index = src_surface->d3d_textures_read_index;
#if _XBOX
		assert(device->active_surface != src_surface); // Cannot copy from the currently active surface, it needs to be resolved, etc, first!
#endif

		if (src_set_index < src_surface->rendertarget[src_buffer_num].texture_count && src_set_index >= 0 && src_surface->rendertarget[src_buffer_num].textures[src_set_index].d3d_texture.typeless) {
			if (device->d3d11_device)
			{
				D3D11_TEXTURE2D_DESC src_desc;
				ID3D11Texture2D_GetDesc(src_surface->rendertarget[src_buffer_num].textures[src_set_index].d3d_buffer.texture_2d_d3d11, &src_desc);
				src_format.format_d3d11 = getDXGITypelessFormat11(src_desc.Format);
			}
			else
			{
				D3DSURFACE_DESC src_desc={0};
				CHECKX(IDirect3DTexture9_GetLevelDesc(src_surface->rendertarget[src_buffer_num].textures[src_set_index].d3d_texture.texture_2d_d3d9, 0, &src_desc));
				src_format.format_d3d9 = src_desc.Format;
			}

        } else {
			// Assume backbuffer?
			assert(src_surface == &device->primary_surface);
			if (device->d3d11_device)
				src_format.format_d3d11 = getDXGITypelessFormat11(device->d3d11_swap_desc->BufferDesc.Format);
			else
				src_format.format_d3d9 = device->d3d_present_params->BackBufferFormat;
		}

		if ((src_surface->params_thread.flags & (SF_DEPTH_TEXTURE|SF_DEPTHONLY)) == (SF_DEPTH_TEXTURE|SF_DEPTHONLY))
			texture_usage = RSBF_DEPTHSTENCIL | RSBF_TEX2D;
		else
			texture_usage = RSBF_RENDERTARGET | RSBF_TEX2D;

		dst_format = rtex->src_format = rxbxGetTexFormatForSBT(src_surface->buffer_types[0]);
		dst_srgb = (src_surface->buffer_types[0] & SBT_SRGB) != 0;
		assert(isCompatibleTexFormat(device, src_format, rxbxGetGPUSurfaceFormat(device, dst_format)));
	} else {
 		if (rtex->src_format.format == RTEX_BGR_U8) // DX11 and Xbox do not support 24-bit textures
 			dst_format.format = RTEX_BGRA_U8;
 		else
			dst_format = rtex->src_format;
	}

#if FORCE_NONTILED
	rtex->need_sub_updating = 1;
#endif

	assert(!tex_handle.is_surface);

	stashIntFindPointer(device->texture_data, tex_handle.texture.hash_value, &tex_data);
	if (tex_data && tex_data->texture.typeless && (tex_data->width != width || tex_data->height != height || tex_data->depth != depth ||
		//tex_data->compressed != compressed || // handled by format
		tex_data->tex_format.format != dst_format.format ||
		tex_data->can_sub_update != rtex->need_sub_updating ||
		tex_data->srgb != dst_srgb ||
		tex_data->tex_type != rtex->type ||
		tex_data->max_levels != rtex->max_levels))
	{
		releaseTexture(device, tex_data);
	}
	else if (!tex_data)
	{
		bool bRet;
		MP_CREATE(RdrTextureDataDX, 1024);
		tex_data = MP_ALLOC(RdrTextureDataDX);
		tex_data->tex_hash_value = tex_handle.texture.hash_value;
		bRet = stashIntAddPointer(device->texture_data, tex_handle.texture.hash_value, tex_data, false);
		assert(bRet);
		VALIDATE_TEXTURES;
		tex_data->memmonitor_name = rtex->memmonitor_name;
	}

	if (device->d3d9ex)
	{
		// We must use a system memory copy and UpdateTexture to initialize textures on D3D9Ex due to
		// lack of managed pool support. This will eventually support staged resource creation as well.
		bCreateWithUpdateTexture = !(texture_usage & (RSBF_DEPTHSTENCIL|RSBF_RENDERTARGET));
		// Track if the system will be performing ongoing updates, for texture atlases, 
		// texwords, or updating the high MIP for split-MIP textures.
		bKeepSystemMemoryCopy = rtex->need_sub_updating;
	}

	if (!tex_data->texture.typeless)
	{
		HRESULT hrTemp;
		tex_data->tex_type = rtex->type;
		tex_data->width = width;
		tex_data->height = height;
		tex_data->depth = depth;
		tex_data->levels_inited = 0;
		tex_data->max_levels = num_levels;
		tex_data->compressed = rdrTexIsCompressedFormat(dst_format);
		tex_data->tex_format = dst_format;

		if ( device->isLost )
			tex_data->created_while_dev_lost = 1;

		hrTemp = rxbxCreateTextureResource(device, rtex->type, width, height, depth, 
			num_levels, texture_usage, dst_format, bCreateWithUpdateTexture || rtex->from_surface ? false : true, dst_srgb, 
			tex_data);

		assert(tex_data->texture.typeless || tex_data->d3d11_data.typeless);

		tex_data->is_non_managed = rtex->from_surface;

		// setup texture flags
		tex_data->srgb = dst_srgb;

		tex_data->can_sub_update = rtex->need_sub_updating;

		tex_data->memory_usage = rxbxGetTextureSize(tex_data->width, tex_data->height, tex_data->depth, dst_format, tex_data->max_levels, rtex->type);

		rdrTrackUserMemoryDirect(&device->device_base, tex_data->memmonitor_name?tex_data->memmonitor_name:"rxbx:Textures", 1, tex_data->memory_usage);
	}
	if (bCreateWithUpdateTexture)
	{
		if (!tex_data->texture_sysmem.typeless)
			hrCreateSysMemTex = rxbxCreateTextureResource(device, rtex->type, width, height, depth, 
				num_levels, texture_usage | RSBF_SYSTEMMEMORY, dst_format, false, dst_srgb, 
				tex_data);
	}

	assert(rtex->anisotropy);
	tex_data->anisotropy = rtex->anisotropy;

	width = rtex->width >> rtex->first_level;
	height = rtex->height >> rtex->first_level;
	depth = rtex->type==RTEX_3D?rtex->depth >> rtex->first_level:1;
	dataSize = rxbxGetTextureSize(width, height, depth, rtex->src_format, rtex->level_count, rtex->type);

	// download the bitmap data
	if (rtex->from_surface)
	{
		assert(rtex->type == RTEX_2D);
#if _XBOX
		if (0)
		{
			//HRESULT hrTemp;
			D3DLOCKED_RECT src_lock;
			D3DLOCKED_RECT dst_lock;

			assertmsg(0, "This function is incredibly slow");

			IDirect3DTexture9_LockRect(tex_data->d3d_texture_2d, 0, &dst_lock, NULL, 0);
			IDirect3DTexture9_LockRect(src_surface->rendertarget[src_buffer_num].d3d_textures[src_set_index], 0, &src_lock, NULL, 0);

			//hrTemp = XGCopySurface(dst_lock.pBits, dst_lock.Pitch, width, height, dst_format, NULL, src_lock.pBits, src_lock.Pitch, src_format, NULL, 0, 0);
			//assert(!FAILED(hrTemp));
			assert(dst_lock.Pitch == src_lock.Pitch);
			memcpy_writeCombined(dst_lock.pBits, src_lock.pBits, src_lock.Pitch * height);
			//XMemCpy(dst_lock.pBits, src_lock.pBits, src_lock.Pitch * height);

			IDirect3DTexture9_UnlockRect(src_surface->rendertarget[src_buffer_num].d3d_textures[src_set_index], 0);
			IDirect3DTexture9_UnlockRect(tex_data->d3d_texture_2d, 0);
		}
#else
		rxbxResolveToTexture( device,
			src_surface->rendertarget[src_buffer_num].d3d_surface,
			tex_data,
			src_surface->active_face,
			rtex->width, rtex->height,
			!!(src_surface->creation_flags & SF_CUBEMAP));
#endif
	}
	else
	{
		U32 iMipLevel;
		int iFace;

		assert(rtex->first_level + rtex->level_count <= tex_data->max_levels);
		if (rtex->first_level + rtex->level_count == tex_data->max_levels) {
			// If we're initializing the lowest LODs, assume the entire texture is invalid.
			// This is something of a hack to work around the way our tex handle pooling works.
			tex_data->levels_inited = 0;

			// D3D9 only records the dirty region for the base level. Mark it as dirty if we don't have data for it.
			if (!device->d3d11_device && rtex->first_level != 0) {
                D3DBaseTexture_Invalidate(tex_data->texture.texture_base_d3d9, tex_data->tex_type);
			}
		}

		// if not fully initializing the texture, automatically keep a system memory copy on renderers that manually
		// manage system memory
		if (device->d3d9ex)
			bKeepSystemMemoryCopy = bKeepSystemMemoryCopy || rtex->level_count != tex_data->max_levels;

		// don't allow a gap in the mipmap data
		assert(rtex->first_level + rtex->level_count >= tex_data->max_levels - tex_data->levels_inited);

#if !_XBOX
		if ( device->isLost )
		{
			if (!(device->warnCountBindDevLostTex % 1024))
				memlog_printf(0, "Updating texture while device is lost Dev 0x%p RdrTexParams 0x%p W %d H %d D %d\n", device, rtex, rtex->width, rtex->height, rtex->depth );
			++device->warnCountBindDevLostTex;
		}
#endif
		if (rtex->reversed_mips) {
			bitmap += dataSize;

			for (iMipLevel = 0; iMipLevel < rtex->level_count; ++iMipLevel) {
				int pitch, mipSize;

				pitch = rtexWidthToBytes(width, rtex->src_format);
				mipSize = pitch * depth * HeightToRows(height, rtex->src_format);

				for (iFace=((rtex->type==RTEX_CUBEMAP)?6:1); iFace > 0; iFace--) {
					bitmap -= mipSize;

					copyTexSubImage(device, tex_data, tex_data->tex_type, iFace - 1, dst_format, rtex->first_level + iMipLevel, bitmap, rtex->src_format, 0, 0, 0, width, height, depth, rtex->width, rtex->height, rtex->depth);
				}

				width = MAX(width >> 1, 1);
				height = MAX(height >> 1, 1);
				depth = MAX(depth >> 1, 1);
			}
		} else {
			for (iFace=0; iFace<((rtex->type==RTEX_CUBEMAP)?6:1); iFace++)
			{
				width = rtex->width >> rtex->first_level;
				height = rtex->height >> rtex->first_level;
				depth = rtex->type==RTEX_3D?rtex->depth >> rtex->first_level:1;
				for (iMipLevel = 0; iMipLevel < rtex->level_count; ++iMipLevel)
				{
					int pitch, mipSize;

					pitch = rtexWidthToBytes(width, rtex->src_format);
					mipSize = pitch * depth * HeightToRows(height, rtex->src_format);

					copyTexSubImage(device, tex_data, tex_data->tex_type, iFace, dst_format, rtex->first_level + iMipLevel, bitmap, rtex->src_format, 0, 0, 0, width, height, depth, rtex->width, rtex->height, rtex->depth);

					bitmap += mipSize;
					width = MAX(width >> 1, 1);
					height = MAX(height >> 1, 1);
					depth = MAX(depth >> 1, 1);
				}
			}
		}
	}
	if (rtex->refcount_data) {
		int refCount = memrefDecrement(rtex->data);
		memlog_printf(rdrGetTextureMemLog(), "rxbxSetTextureDataDirect memrefDecrement(%p) = %d, rtex = %p, tex_handle = %"FORM_LL"u", rtex->data, refCount, rtex, rtex->tex_handle);
	} else if (rtex->ringbuffer_data) {
		rdrTextureLoadFree(rtex->data, dataSize);
	}

	if (bCreateWithUpdateTexture)
	{
		HRESULT hrUpdateTex = IDirect3DDevice9_UpdateTexture(device->d3d_device, tex_data->texture_sysmem.texture_base_d3d9, tex_data->texture.texture_base_d3d9);
		if (FAILED(hrUpdateTex))
			rxbxFatalHResultErrorf(device, hrUpdateTex, "initializing texture data", "%s while creating %s texture size %d x %d (%d mips, %s)", 
				rxbxGetStringForHResult(hrUpdateTex), rxbxGetTextureTypeString(tex_data->tex_type), width, height, num_levels, rdrTexFormatName(dst_format));
		if (!bKeepSystemMemoryCopy)
			rxbxTextureDataReleaseSystemMemory(device, tex_data);
	}

	assert(rtex->first_level + tex_data->levels_inited <= tex_data->max_levels);
	tex_data->levels_inited = tex_data->max_levels - rtex->first_level;

	tex_data->debug_backpointer = rtex->debug_texture_backpointer;
}

void rxbxSetTextureSubDataDirect(RdrDeviceDX *device, RdrSubTexParams *rtex, WTCmdPacket *packet)
{
	U32 width = rtex->width, height = rtex->height, depth = rtex->depth, compressed = 0;
	U8 *bitmap = (U8*)(rtex->data);
	RdrTextureDataDX *tex_data = NULL;
	RdrTexHandle tex_handle = TexHandleToRdrTexHandle(rtex->tex_handle);

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	VALIDATE_TEXTURES;

	assert(!tex_handle.is_surface);
	stashIntFindPointer(device->texture_data, tex_handle.texture.hash_value, &tex_data);
	assert(tex_data);
	assert(tex_data->tex_hash_value == tex_handle.texture.hash_value);
	assert(tex_data->can_sub_update);
	assert(tex_data->tex_type == rtex->type);

	if (tex_data->compressed)
	{
		assertmsg(0, "Updating sub image of a compressed format is not currently allowed.");
	}
	else
	{
		HRESULT hrUpdateTex = S_OK;
		copyTexSubImage(device, tex_data, tex_data->tex_type, 0, tex_data->tex_format, 0,
			bitmap, rtex->src_format, rtex->x_offset, rtex->y_offset, rtex->z_offset, width, height, depth, tex_data->width, tex_data->height, tex_data->depth);

		if (device->d3d9ex)
		{
			// The prior copyTexSubImage call will update the system-memory copy. Send the modified parts to the GPU.
			hrUpdateTex = IDirect3DDevice9_UpdateTexture(device->d3d_device, tex_data->texture_sysmem.texture_base_d3d9, tex_data->texture.texture_base_d3d9);
			if (FAILED(hrUpdateTex))
				rxbxFatalHResultErrorf(device, hrUpdateTex, "updating texture data", "%s while updating %s texture size %d x %d (%d mips, %s)", 
					rxbxGetStringForHResult(hrUpdateTex), rxbxGetTextureTypeString(tex_data->tex_type), width, height, tex_data->max_levels, rdrTexFormatName(tex_data->tex_format));
		}
	}

	if (rtex->refcount_data)
	{
		int refCount = memrefDecrement(rtex->data);
		memlog_printf(rdrGetTextureMemLog(), "rxbxSetTextureSubDataDirect memrefDecrement(%p) = %d, rtex = %p, tex_handle = %"FORM_LL"u", rtex->data, refCount, rtex, rtex->tex_handle);
	}
}

static RdrTextureDataDX* rxbxTextureData( RdrDeviceDX* device, RdrTexHandle handle )
{
	if( handle.is_surface ) {
		RdrSurfaceDX* surface = (RdrSurfaceDX*)rdrGetSurfaceForTexHandle( handle );
		return rxbxTextureData( device, surface->rendertarget[ handle.surface.buffer ].textures[ handle.surface.set_index ].tex_handle);
	} else {
		RdrTextureDataDX* tex_data;
		stashIntFindPointer( device->texture_data, handle.texture.hash_value, &tex_data );
		assert(tex_data->tex_hash_value == handle.texture.hash_value);
		return tex_data;
	}
}

// Debugging code for [CO-23282].
extern RdrSurface **texhandle_surfaces;

static void validateTexData(MemoryPool mempool, RdrTextureDataDX *tex_data, RdrDeviceDX *device)
{
	RdrTextureDataDX *found_tex_data;
	assert(stashIntFindPointer(device->texture_data, tex_data->tex_hash_value, &found_tex_data));
	assert(found_tex_data == tex_data);
}

void rxbxValidateTextures( RdrDeviceDX* device )
{
	StashElement elem;
	StashTableIterator iter;
	int i, j, k;

	if (!rdr_state.validateTextures)
		return;

	PERFINFO_AUTO_START_FUNC();

	// no surface should have a surface as its texture
	for (i = 0; i != eaSize(&texhandle_surfaces); ++i)
	{
		RdrSurfaceDX* surf = (RdrSurfaceDX*)texhandle_surfaces[i];

		if (!surf)
			continue;

		for (j = 0; j != SBUF_MAX; ++j)
		{
			RxbxSurfacePerTargetState* buffer = &surf->rendertarget[j];
			for (k = 0; k != buffer->texture_count; ++k)
			{
				RdrTexHandle handle = buffer->textures[k].tex_handle;
				assert(!handle.is_surface);
			}
		}
	}

	// make sure the hash table is not munged
	stashGetIterator(device->texture_data, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		U32 tex_hash_value = stashElementGetIntKey(elem);
		RdrTextureDataDX *tex_data = stashElementGetPointer(elem);
		MemoryPoolAllocState mem_state = mpGetMemoryState(MP_NAME(RdrTextureDataDX), tex_data);
		assert(mem_state == MPAS_ALLOCATED);
		assert(!tex_data || tex_data->tex_hash_value == tex_hash_value);
	}

	// validate that every texture data is in the hash table
	mpForEachAllocation(MP_NAME(RdrTextureDataDX), validateTexData, device);

	PERFINFO_AUTO_STOP();
}

void rxbxTexStealSnapshotDirect(RdrDeviceDX *device, RdrTexStealSnapshotParams *steal, WTCmdPacket *packet)
{
	RdrTexHandle steal_handle = TexHandleToRdrTexHandle(steal->tex_handle);
	RdrSurfaceDX *surface = (RdrSurfaceDX *)steal->surf;
	RdrTextureDataDX *old_data, *surf_tex_data;

	VALIDATE_TEXTURES;

	// remove snapshot texture from surface
	assert(surface->rendertarget[steal->buffer_num].textures[steal->snapshot_idx].d3d_texture.typeless == NULL);
	surface->rendertarget[steal->buffer_num].textures[steal->snapshot_idx].d3d_texture.typeless = NULL;
	if (!stashIntRemovePointer(device->texture_data, surface->rendertarget[steal->buffer_num].textures[steal->snapshot_idx].tex_handle.texture.hash_value, &surf_tex_data))
		return;

	assert(!steal_handle.is_surface);

	// remove old texture data attached to steal handle
	if (stashIntRemovePointer(device->texture_data, steal_handle.texture.hash_value, &old_data))
	{
		releaseTexture(device, old_data);
		MP_FREE(RdrTextureDataDX, old_data);
	}

	// assign snapshot texture to steal handle
	surf_tex_data->tex_hash_value = steal_handle.texture.hash_value;
	assert(stashIntAddPointer(device->texture_data, steal_handle.texture.hash_value, surf_tex_data, false));

	VALIDATE_TEXTURES;

	// These flags are ignored for Surface textures.  But now, this
	// texture is going through a life-altering experience.
	surf_tex_data->debug_backpointer = steal->tex_debug_backpointer;
	surf_tex_data->is_non_managed = true;

	assert( surf_tex_data->memory_usage == 0
		|| surf_tex_data->memory_usage == rxbxGetSurfaceSize(surf_tex_data->width, surf_tex_data->height, surf_tex_data->tex_format));
	surf_tex_data->memory_usage = rxbxGetSurfaceSize(surf_tex_data->width, surf_tex_data->height, surf_tex_data->tex_format);
	rdrTrackUserMemoryDirect( &device->device_base, "VideoMemory:SurfaceTexture", 1, -surf_tex_data->memory_usage );

	surf_tex_data->memmonitor_name = steal->memmonitor_name;
	rdrTrackUserMemoryDirect( &device->device_base, surf_tex_data->memmonitor_name, 1, surf_tex_data->memory_usage );
}

#define U32_SCALE (1.f/U32_MAX)
#define U16_SCALE (1.f/U16_MAX)
#define U8_SCALE  (1.f/U8_MAX)

void rxbxCopyComponent(void *dst, void *src, int dst_bytes, int src_bytes, int dst_float, int src_float)
{
	if (dst_bytes == src_bytes && dst_float == src_float)
	{
		memcpy(dst, src, dst_bytes);
	}
	else
	{
		float f;
		if (src_float)
		{
			if (src_bytes == 4)
				f = *((F32 *)src);
			else if (src_bytes == 2)
				f = F16toF32(*((F16 *)src));
			else
				assertmsg(0, "Unknown floating point format!");
		}
		else
		{
			if (src_bytes == 4)
				f = *((U32 *)src) * U32_SCALE;
			else if (src_bytes == 2)
				f = *((U16 *)src) * U16_SCALE;
			else if (src_bytes == 1)
				f = *((U8 *)src) * U8_SCALE;
			else
				assertmsg(0, "Unknown fixed point format!");
		}

		if (dst_float)
		{
			if (dst_bytes == 4)
				*((F32 *)dst) = f;
			else if (dst_bytes == 2)
				*((F16 *)dst) = F32toF16(f);
			else
				assertmsg(0, "Unknown floating point format!");
		}
		else
		{
			f = CLAMPF32(f, 0, 1);
			if (dst_bytes == 4)
				*((U32 *)dst) = f * U32_MAX;
			else if (dst_bytes == 2)
				*((U16 *)dst) = f * U16_MAX;
			else if (dst_bytes == 2)
				*((U8 *)dst) = f * U8_MAX;
			else
				assertmsg(0, "Unknown fixed point format!");
		}
	}
}

void rxbxGetTexInfoDirect(RdrDeviceDX *device, RdrGetTexInfo *get, WTCmdPacket *packet)
{
	RdrTextureDataDX *texdata = NULL;
	RdrSurfaceDX *surface = NULL;
	RdrTexHandle tex_handle = TexHandleToRdrTexHandle(get->tex_handle);
	int hash_value = 0;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	if (tex_handle.is_surface)
	{
		surface = (RdrSurfaceDX *)rdrGetSurfaceForTexHandle(tex_handle);
		if (surface)
		{
			//if (set_index == RDRSURFACE_SET_INDEX_DEFAULT)
			//	set_index = surface->d3d_textures_read_index;
			if (surface->rendertarget[tex_handle.surface.buffer].texture_count)
				hash_value = surface->rendertarget[tex_handle.surface.buffer].textures[tex_handle.surface.set_index].tex_handle.texture.hash_value;
		}
	}
	else
	{
		hash_value = tex_handle.texture.hash_value;
	}

	if (!hash_value)
		return;

	stashIntFindPointer(device->texture_data, hash_value, &texdata);

	if (!texdata)
		return;

	if (get->widthout)
		*get->widthout = texdata->width;
	if (get->heightout)
		*get->heightout = texdata->height;

	if (get->data)
	{
		IDirect3DSurface9 *temp_d3d_surface = NULL;
		ID3D11Texture2D *pCompatableTexture = NULL;
		RECT rect = {0};

		U32 byte_count = rdrGetImageByteCount(get->type, get->src_format, texdata->width, texdata->height, 1);
		U8 *data_ptr;
		void *pBits;
		int Pitch;

		assert(texdata->tex_type == RTEX_2D);

		if (get->data_size) {
			data_ptr = (U8*)get->data;
			assert(get->data_size >= byte_count);
		} else {
			data_ptr = *get->data = malloc(byte_count);
		}

		rect.left = 0;
		rect.right = texdata->width;
		rect.top = 0;
		rect.bottom = texdata->height;

#if !_XBOX
		assert(!surface); // rxbxGetSurfaceDataDirect could handle this, but I don't think this code path is hit
		if (surface)
		{
			D3DLOCKED_RECT locked_rect = {0};
			D3DFORMAT temp_format = rxbxGetGPUFormat9(rxbxGetTexFormatForSBT(surface->buffer_types[tex_handle.surface.buffer]));
			HRESULT hr;
			assert(!device->d3d11_device); // Don't think this code path is ever hit, if it is, shouldn't be hard to make work like rxbxGetSurfaceDataDirect
			hr = IDirect3DDevice9_CreateOffscreenPlainSurface(device->d3d_device, texdata->width, texdata->height, temp_format,
				D3DPOOL_SYSTEMMEM, &temp_d3d_surface, NULL);
			rxbxFatalHResultErrorf(device, hr, "creating temp surface to copy tex data into", "%s while creating temporary 2D texture size %d x %d (%s)",
								   rxbxGetStringForHResult(hr), texdata->width, texdata->height, rxbxGetTextureD3DFormatString(temp_format));
			//if (!FAILED(hr))
			//	rxbxLogCreateSurface(device, temp_d3d_surface);

			CHECKX(IDirect3DDevice9_GetRenderTargetData(device->d3d_device, surface->rendertarget[tex_handle.surface.buffer].d3d_surface.surface9, temp_d3d_surface));

			CHECKX(IDirect3DSurface9_LockRect(temp_d3d_surface, &locked_rect, &rect, D3DLOCK_READONLY));
			pBits = locked_rect.pBits;
			Pitch = locked_rect.Pitch;
		}
		else if (texdata->is_non_managed)
		{
			// This is the only branch that is hit
			if (device->d3d11_device)
			{
				HRESULT hr;
				D3D11_TEXTURE2D_DESC dsc;
				D3D11_MAPPED_SUBRESOURCE mapped_subresource = {0};

				pCompatableTexture = texdata->d3d11_data.texture_2d_d3d11;
				ID3D11Texture2D_GetDesc(pCompatableTexture, &dsc);

				// Resolve to staging texture
				{
					D3D11_TEXTURE2D_DESC dsc_new = dsc;
					ID3D11Texture2D *resolveTexture;
					D3D11_BOX box = {0, 0, 0, dsc.Width, dsc.Height, 1};
					dsc_new.SampleDesc.Count = 1;
					dsc_new.SampleDesc.Quality = 0;
					dsc_new.Usage = D3D11_USAGE_STAGING;
					dsc_new.BindFlags = 0;
					dsc_new.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					hr = ID3D11Device_CreateTexture2D(device->d3d11_device, &dsc_new, NULL, &resolveTexture);
					assert(!FAILED(hr));
					ID3D11DeviceContext_CopySubresourceRegion(device->d3d11_imm_context, (ID3D11Resource*)resolveTexture, 0, 0, 0, 0, (ID3D11Resource*)pCompatableTexture, 0, &box);
					pCompatableTexture = resolveTexture;
				}

				hr = ID3D11DeviceContext_Map(device->d3d11_imm_context, (ID3D11Resource*)pCompatableTexture, 0, D3D11_MAP_READ, 0, &mapped_subresource);
				pBits = mapped_subresource.pData;
				Pitch = mapped_subresource.RowPitch;

			} else {
				D3DLOCKED_RECT locked_rect = {0};
				D3DFORMAT temp_format = rxbxGetGPUFormat9(texdata->tex_format);
				IDirect3DSurface9* tex_d3d_surface = NULL;
				HRESULT hr;

				hr = IDirect3DTexture9_GetSurfaceLevel(texdata->texture.texture_2d_d3d9, 0, &tex_d3d_surface);
				rxbxFatalHResultErrorf(device, hr, "getting surface level 0 for a render target texture", "%s while getting surface level 0 for a render target texture",
									   rxbxGetStringForHResult(hr));
				
				hr = IDirect3DDevice9_CreateOffscreenPlainSurface(device->d3d_device, texdata->width, texdata->height, temp_format,
																  D3DPOOL_SYSTEMMEM, &temp_d3d_surface, NULL);
				rxbxFatalHResultErrorf(device, hr, "creating temp surface to copy tex data into", "%s while creating temporary 2D texture size %d x %d (%s)",
									   rxbxGetStringForHResult(hr), texdata->width, texdata->height, rxbxGetTextureD3DFormatString(temp_format));
				//if (!FAILED(hr))
				//	rxbxLogCreateSurface(device, temp_d3d_surface);

				CHECKX(IDirect3DDevice9_GetRenderTargetData(device->d3d_device, tex_d3d_surface, temp_d3d_surface));
				CHECKX(IDirect3DSurface9_LockRect(temp_d3d_surface, &locked_rect, &rect, D3DLOCK_READONLY));
				IDirect3DSurface9_Release(tex_d3d_surface);

				pBits = locked_rect.pBits;
				Pitch = locked_rect.Pitch;
			}
		}
		else
#endif
		{
			D3DLOCKED_RECT locked_rect = {0};
			assert(!device->d3d11_device); // Don't think this code path is ever hit, if it is, shouldn't be hard to update
			CHECKX(IDirect3DTexture9_LockRect(texdata->texture.texture_2d_d3d9, 0, &locked_rect, &rect, D3DLOCK_READONLY));
			pBits = locked_rect.pBits;
			Pitch = locked_rect.Pitch;
		}

#if _XBOX
		XGCopySurface(data_ptr, texdata->width, texdata->width, texdata->height, dst_format, NULL, locked_rect.pBits, locked_rect.Pitch, texdata->tex_format, NULL, 0, 0);
#else
		if (pBits)
		{
			U8 *src, *dst;
			int component_bytes_src, component_bytes_dst, float_src, float_dst;
			int components_src = getComponentCount(texdata->tex_format, &component_bytes_src, &float_src);
			int components_dst = getComponentCount(get->src_format, &component_bytes_dst, &float_dst);

			int i;
			U32 x, y;

			src = pBits;
			dst = data_ptr;

			for (y = 0; y < texdata->height; ++y)
			{
				for (x = 0; x < texdata->width; ++x)
				{
					for (i = 0; i < components_dst && i < components_src; ++i)
						rxbxCopyComponent(dst + i * component_bytes_dst, src + x * components_src * component_bytes_src + i * component_bytes_src, component_bytes_dst, component_bytes_src, float_dst, float_src);
					dst += components_dst * component_bytes_dst;
				}
				src += Pitch;
			}
		}
#endif

		if (device->d3d11_device)
		{
			if (pCompatableTexture)
				ID3D11Texture2D_Release(pCompatableTexture);
		} else {
			if (temp_d3d_surface)
			{
				int ref_count;
				CHECKX(IDirect3DSurface9_UnlockRect(temp_d3d_surface));
				ref_count = IDirect3DSurface9_Release(temp_d3d_surface);
				//rxbxLogReleaseSurface(device, temp_d3d_surface, ref_count);
			}
			else
			{
				CHECKX(IDirect3DTexture9_UnlockRect(texdata->texture.texture_2d_d3d9, 0));
			}
		}
	}
}

void rxbxSetTexAnisotropyDirect(RdrDeviceDX *device, RdrTextureAnisotropy *params, WTCmdPacket *packet)
{
	RdrTextureDataDX *tex_data = NULL;
	RdrTexHandle tex_handle = TexHandleToRdrTexHandle(params->tex_handle);

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	assert(!tex_handle.is_surface);
	stashIntFindPointer(device->texture_data, tex_handle.texture.hash_value, &tex_data);
    if (tex_data) {
		tex_data->anisotropy = params->anisotropy;
    }
}

void rxbxFreeTextureDirect(RdrDeviceDX *device, RdrTexHandle *tex_ptr, WTCmdPacket *packet)
{
	RdrTextureDataDX *tex_data = NULL;
	RdrTexHandle tex_handle = *tex_ptr;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	if (tex_handle.is_surface)
		return;

	if (stashIntRemovePointer(device->texture_data, tex_handle.texture.hash_value, &tex_data))
	{
		assert(tex_handle.texture.hash_value == tex_data->tex_hash_value);
		releaseTexture(device, tex_data);
		MP_FREE(RdrTextureDataDX, tex_data);
		VALIDATE_TEXTURES;
	}
}

void rxbxSwapTexHandlesDirect(RdrDeviceDX *device, RdrTexHandle *tex_ptrs, WTCmdPacket *packet)
{
	RdrTextureDataDX *tex_data1 = NULL;
	RdrTextureDataDX *tex_data2 = NULL;
	RdrTexHandle tex_handle1 = tex_ptrs[0];
	RdrTexHandle tex_handle2 = tex_ptrs[1];
	const BasicTexture* debug_backpointer1 = NULL;
	const BasicTexture* debug_backpointer2 = NULL;

	assert(!tex_handle1.is_surface);
	assert(!tex_handle2.is_surface);

	stashIntRemovePointer(device->texture_data, tex_handle1.texture.hash_value, &tex_data1);
	stashIntRemovePointer(device->texture_data, tex_handle2.texture.hash_value, &tex_data2);

	if (tex_data1)
	{
		debug_backpointer1 = tex_data1->debug_backpointer;
	}
	if (tex_data2)
	{
		debug_backpointer2 = tex_data2->debug_backpointer;
	}
	
	if (tex_data1)
	{
		tex_data1->tex_hash_value = tex_handle2.texture.hash_value;
		verify(stashIntAddPointer(device->texture_data, tex_handle2.texture.hash_value, tex_data1, false));
		tex_data1->debug_backpointer = debug_backpointer1;
	}
	if (tex_data2)
	{
		tex_data2->tex_hash_value = tex_handle1.texture.hash_value;
		verify(stashIntAddPointer(device->texture_data, tex_handle1.texture.hash_value, tex_data2, false));
		tex_data2->debug_backpointer = debug_backpointer2;
	}
}


void rxbxTestIfNonManagedTextureActive(RdrDeviceDX *device)
{
	StashElement elem;
	StashTableIterator iter;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);


	stashGetIterator(device->texture_data, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		int tex_hash_value = stashElementGetIntKey(elem);
		RdrTextureDataDX *tex_data = stashElementGetPointer(elem);

		if (tex_data->is_non_managed && tex_data->texture.typeless) {
			int i, j;
			for (j=0; j<TEXTURE_TYPE_COUNT; j++)
			{
				for (i = 0; i < ARRAY_SIZE(device->device_state.textures_driver[j]); ++i)
				{
					RdrTextureObj active_texture = device->device_state.textures_driver[j][i].texture;
					if (tex_data->texture.typeless == active_texture.typeless)
						memlog_printf(0, "Texture being freed while still active as %s texture RdrTextureDataDX 0x%p\n", (j==TEXTURE_PIXELSHADER)?"pixel":"vertex", tex_data);
				}
			}
		}
	}
}

void rxbxFreeAllNonManagedTexturesDirect(RdrDeviceDX *device)
{
	StashElement elem;
	StashTableIterator iter;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	stashGetIterator(device->texture_data, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		int tex_hash_value = stashElementGetIntKey(elem);
		RdrTextureDataDX *tex_data = stashElementGetPointer(elem);

		// DX11TODO: Should only need to do this on DX9?

		if (tex_data->is_non_managed && tex_data->texture.typeless) {
			releaseTexture(device, tex_data);
		}
	}
}

void rxbxFreeAllTexturesDirect(RdrDeviceDX *device, void *unused, WTCmdPacket *packet)
{
	StashElement elem;
	StashTableIterator iter;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	stashGetIterator(device->texture_data, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		int tex_hash_value = stashElementGetIntKey(elem);
		RdrTextureDataDX *tex_data = stashElementGetPointer(elem);

		releaseTexture(device, tex_data);
		MP_FREE(RdrTextureDataDX, tex_data);
	}

	stashTableClear(device->texture_data);
	rxbxDeviceDetachDeadSurfaceTextures( device );
}

__forceinline
bool rdrAllocTexDataDX(RdrDeviceDX *device, RdrTexHandle *handle, RdrTextureDataDX **tex_data)
{
	bool bRet;
	MP_CREATE(RdrTextureDataDX, 1024);
	*tex_data = MP_ALLOC(RdrTextureDataDX);
	(*tex_data)->tex_hash_value = handle->texture.hash_value;
	bRet = stashIntAddPointer(device->texture_data, handle->texture.hash_value, *tex_data, false);
	assert(bRet);
	VALIDATE_TEXTURES;
	return bRet;
}

RdrTextureDataDX *rxbxMakeTextureForSurface(RdrDeviceDX *device, RdrTexHandle *handle, 
	RdrTexFormatObj tex_format, U32 width, U32 height, 
	bool is_srgb, RdrSurfaceBindFlags texture_usage, bool is_cubemap, 
	RdrTexFlags extra_flags, int multisample_count)
{
	HRESULT hr;
	RdrTextureDataDX *tex_data = NULL;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	assert(!handle->is_surface);

	if (!handle->texture.hash_value)
	{
		TexHandle th = rdrGenTexHandle(extra_flags);
		*handle = TexHandleToRdrTexHandle(th);
	}
	else
	{
		rdrChangeTexHandleFlags((TexHandle*)handle, extra_flags);
		stashIntFindPointer(device->texture_data, handle->texture.hash_value, &tex_data);
	}

	if (!tex_data)
	{
		rdrAllocTexDataDX(device, handle, &tex_data);
	}
	else if (tex_data->texture.typeless &&
		(tex_data->width != width ||
		tex_data->height != height ||
		tex_data->depth != 0 ||
		tex_data->tex_format.format != tex_format.format ||
		tex_data->srgb != (U32)is_srgb))
	{
		releaseTexture(device, tex_data);
		rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:SurfaceTexture", 1, -rxbxGetSurfaceSize(tex_data->width, tex_data->height, tex_data->tex_format));
	}

	if (!tex_data->texture.typeless)
	{
		tex_data->width = width;
		tex_data->height = height;
		tex_data->depth = 0;
		tex_data->compressed = 0;
		tex_data->tex_type = RTEX_2D;

#if _XBOX
		if (is_srgb)
			tex_data->tex_format = MAKESRGBFMT(tex_format);
		else
#endif
			tex_data->tex_format = tex_format;

#if !_XBOX
		if ( device->isLost )
		{
			memlog_printf(0, "Creating surface texture while device is lost Dev 0x%p Handle %p W %d H %d \n", device, handle, width, height );
			tex_data->created_while_dev_lost = 1;
		}
#endif

		if (is_cubemap)
		{
			tex_data->tex_type = RTEX_CUBEMAP;
			hr = rxbxTextureCreateCube(device, width, 1, texture_usage, tex_format, false, is_srgb, tex_data);
			if (!FAILED(hr))
				rxbxLogCreateCubeTexture(device, tex_data);
		}
		else
		{
			if (!device->d3d11_device)
			{
				if (rxbxIsGPUDepthFormat9(tex_format))
					texture_usage |= RSBF_DEPTHSTENCIL;
				else
					texture_usage |= RSBF_RENDERTARGET;
			}
			hr = rxbxTextureCreate2D(device, width, height, 1, texture_usage, tex_format, false, is_srgb, tex_data, multisample_count);
			if (!FAILED(hr))
				rxbxLogCreateTexture(device, tex_data);
		}

		rxbxFatalHResultErrorf(device, hr, "while creating a render target texture", "%s while creating %s render target texture size %d x %d (%s, usage %d, %d samples, GS %c)", 
			rxbxGetStringForHResult(hr), is_cubemap ? "cubemap" : "2D", width, height, rdrTexFormatName(tex_format), texture_usage, multisample_count, is_srgb ? 'Y' : 'N');
		// DX11 may have only a texture resource, but no shader texture resource view, for depth-stencil texture surfaces
		assert(tex_data->texture.typeless || tex_data->d3d11_data.typeless);

		rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:SurfaceTexture", 1, rxbxGetSurfaceSize(tex_data->width, tex_data->height, tex_data->tex_format));

		// setup texture flags
		tex_data->max_levels = 1; // no mipmap
		tex_data->levels_inited = 1;
		tex_data->anisotropy = 1;
		tex_data->can_sub_update = 0;
		tex_data->srgb = is_srgb;
	}

	return tex_data;
}

TexHandle rxbxMakeTextureForStereoscopic(RdrDeviceDX *device)
{
	RdrTextureDataDX *tex_data = NULL;
	TexHandle tex_handle = 0;
	RdrTexHandle handle;
	StereoscopicTexParams tex_params;
	RdrTexFormatObj tex_format = {0};
	HRESULT hrTemp;
	bool bCreateWithUpdateTexture = false;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	rxbxNVStereoFillTexParams(&tex_params);
	if (device->d3d11_device)
	{
		tex_format.format = rxbxGetRdrTexFormat11(tex_params.d3d11Format);
	}
	else
	{
		tex_format.format = rxbxGetRdrTexFormat9(tex_params.d3d9Format);
	}

	tex_handle = rdrGenTexHandle(0);
	handle = TexHandleToRdrTexHandle(tex_handle);

	{
		rdrAllocTexDataDX(device, &handle, &tex_data);
	}

	if (device->d3d9ex)
	{
		bCreateWithUpdateTexture = true;
	}

	tex_data->width = tex_params.width;
	tex_data->height = tex_params.height;
	tex_data->depth = 0;
	tex_data->tex_type = RTEX_2D;
	tex_data->levels_inited = 0;
	tex_data->max_levels = 1;
	tex_data->compressed = 0;
	tex_data->memmonitor_name = NULL;

	if ( device->isLost )
		tex_data->created_while_dev_lost = 1;

	hrTemp = rxbxCreateTextureResource(device, RTEX_2D, tex_params.width, tex_params.height, 0, 
		1, RSBF_TEX2D, tex_format, bCreateWithUpdateTexture ? false : true, false, tex_data);

	assert(tex_data->texture.typeless || tex_data->d3d11_data.typeless);

	tex_data->is_non_managed = !bCreateWithUpdateTexture;

	// setup texture flags
	tex_data->srgb = 0;

	tex_data->can_sub_update = 0;

	tex_data->memory_usage = rxbxGetTextureSize(tex_data->width, tex_data->height, tex_data->depth, tex_format, tex_data->max_levels, RTEX_2D);

	rdrTrackUserMemoryDirect(&device->device_base, tex_data->memmonitor_name?tex_data->memmonitor_name:"rxbx:Textures", 1, tex_data->memory_usage);

	return tex_handle;
}

void rxbxStereoscopicTexUpdate(RdrDeviceDX *device)
{
	RdrTextureDataDX *stereo_tex;

	if (!device->stereoscopic_tex)
		device->stereoscopic_tex = rxbxMakeTextureForStereoscopic(device);

	assert(device->stereoscopic_tex);
	stashIntFindPointer(device->texture_data, TexHandleToRdrTexHandle(device->stereoscopic_tex).texture.hash_value, &stereo_tex);

	if (device->d3d11_device)
		rxbxNVTextureUpdateD3D11(device->d3d11_device, device->d3d11_imm_context, stereo_tex->d3d11_data.texture_2d_d3d11);
	else
		rxbxNVTextureUpdateD3D9(device->d3d_device, device->d3d_device_ex, stereo_tex->texture.texture_2d_d3d9, device->isLost != RDR_OPERATING);
}

void rxbxFreeSurfaceTexture(RdrDeviceDX *device, RdrTexHandle handle, RdrTextureObj tex)
{
	RdrTextureDataDX *tex_data = NULL;

	if (handle.is_surface)
		return;

	assert(stashIntRemovePointer(device->texture_data, handle.texture.hash_value, &tex_data));
	assert(tex_data->texture.typeless == tex.typeless);

	releaseTexture(device, tex_data);
	rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:SurfaceTexture", 1, -rxbxGetSurfaceSize(tex_data->width, tex_data->height, tex_data->tex_format));

	MP_FREE(RdrTextureDataDX, tex_data);

	VALIDATE_TEXTURES;
}

void rxbxFreeTexData(RdrDeviceDX *device, RdrTexHandle handle)
{
	RdrTextureDataDX *tex_data = NULL;

	stashIntRemovePointer(device->texture_data, handle.texture.hash_value, &tex_data);

	if(tex_data)
		MP_FREE(RdrTextureDataDX, tex_data);

	VALIDATE_TEXTURES;
}

