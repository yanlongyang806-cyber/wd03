#ifndef XDX_H
#define XDX_H
#pragma once
GCC_SYSTEM

// Enable this for extra debug information. Do not check in with D3D_DEBUG_INFO enabled in Debug configurations!
// See the D3D_DEBUG_INFO help for more information, both in the DXSDK help CHM and
// here: http://msdn.microsoft.com/en-us/library/windows/desktop/bb173355(v=vs.85).aspx
#ifdef _FULLDEBUG
#define D3D_DEBUG_INFO
#endif

#if defined(_DEBUG) && !defined(_FULLDEBUG)
#ifdef D3D_DEBUG_INFO
#error "Do not check in xdx.h with D3D_DEBUG_INFO defined! Comment out this line and mark with the do not check in guard symbol, then uncomment the earlier #define and mark it as well."
#endif
#endif

#ifdef _DEBUG
    #define DEBUGUNDEFED
    #undef _DEBUG
#endif
#include "wininclude.h"

// Turn off macros from the Cryptic math utility library

#ifdef powf
	#undef powf
#endif

#ifdef ceilf
	#undef ceilf
#endif

#ifdef floorf
	#undef floorf
#endif

#include <d3d9.h>
#if !_XBOX
#include <d3d9types.h>
#else
#include <xgraphics.h>
#endif

#include <rpcsal.h>
#define COBJMACROS
#include <d3d11.h>

#ifdef DEBUGUNDEFED
    #define _DEBUG 1
    #undef DEBUGUNDEFED
#endif

#if !PLATFORM_CONSOLE
typedef struct IDirect3DDevice9 D3DDevice;
typedef struct IDirect3DTexture9 D3DTexture;
typedef struct IDirect3DBaseTexture9 D3DBaseTexture;
typedef struct IDirect3DVolumeTexture9 D3DVolumeTexture;
typedef struct IDirect3DCubeTexture9 D3DCubeTexture;

__forceinline UINT D3DDrawVertCountToPrimCount( D3DPRIMITIVETYPE PrimitiveType, UINT VertexCount )
{
	switch ( PrimitiveType )
	{
		xcase D3DPT_LINELIST:
			return VertexCount / 2;

		xcase D3DPT_LINESTRIP:
			return VertexCount - 1;

		xcase D3DPT_TRIANGLELIST:
			return VertexCount / 3;

		xcase D3DPT_TRIANGLEFAN:
			return VertexCount - 2;

		xcase D3DPT_TRIANGLESTRIP:
			return VertexCount - 2;

		xdefault:
			assert( 0 );
	}
	return VertexCount;
}

__forceinline HRESULT IDirect3DDevice9_DrawVertices(D3DDevice *pThis, 
	D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT VertexCount)
{ 
	return IDirect3DDevice9_DrawPrimitive( pThis, PrimitiveType, StartVertex,
		D3DDrawVertCountToPrimCount( PrimitiveType, VertexCount ) );
}

__forceinline HRESULT IDirect3DDevice9_DrawIndexedVertices(D3DDevice *pThis, 
	D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT StartIndex, UINT IndexCount)
{
	return IDirect3DDevice9_DrawIndexedPrimitive( pThis, PrimitiveType, BaseVertexIndex, 0, IndexCount, StartIndex, 
		D3DDrawVertCountToPrimCount( PrimitiveType, IndexCount ) );
}

__forceinline HRESULT IDirect3DDevice9_DrawVerticesUP(D3DDevice *pThis, 
	D3DPRIMITIVETYPE PrimitiveType, UINT VertexCount, CONST void *pVertexStreamZeroData,
	UINT VertexStreamZeroStride)
{
	return IDirect3DDevice9_DrawPrimitiveUP( pThis, PrimitiveType, 
		D3DDrawVertCountToPrimCount( PrimitiveType, VertexCount ), pVertexStreamZeroData,
		VertexStreamZeroStride );
}

__forceinline HRESULT IDirect3DDevice9_DrawIndexedVerticesUP(D3DDevice *pThis,
	D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, 
	UINT IndexCount, CONST void *pIndexData, D3DFORMAT IndexDataFormat, 
	CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
	return IDirect3DDevice9_DrawIndexedPrimitiveUP( pThis, PrimitiveType, MinVertexIndex, NumVertices, 
		D3DDrawVertCountToPrimCount( PrimitiveType, IndexCount ), pIndexData, IndexDataFormat, 
		pVertexStreamZeroData, VertexStreamZeroStride );
}
#endif

typedef union RdrVertexShaderObj
{
	IDirect3DVertexShader9 *vertex_shader_d3d9;
	ID3D11VertexShader *vertex_shader_d3d11;
	struct RdrVertexShaderTypeless *typeless;
} RdrVertexShaderObj;

typedef union RdrPixelShaderObj
{
	IDirect3DPixelShader9 *pixel_shader_d3d9;
	ID3D11PixelShader *pixel_shader_d3d11;
	struct RdrPixelShaderTypeless *typeless;
} RdrPixelShaderObj;

typedef union RdrVertexDeclarationObj
{
	IDirect3DVertexDeclaration9 * decl;
	ID3D11InputLayout * layout;
	struct RdrVertexDeclarationTypeless *typeless_decl;
} RdrVertexDeclarationObj;

typedef union RdrQueryObj
{
	IDirect3DQuery9 *query_d3d9;
	ID3D11Query *query_d3d11;
	ID3D11Asynchronous *asynch_d3d11; // Parent type of ID3D11Query
	struct RdrQueryObjTypeless *typeless;
} RdrQueryObj;

typedef union RdrTextureObj {
	// D3D9 types:
	IDirect3DBaseTexture9	*texture_base_d3d9;
	IDirect3DTexture9		*texture_2d_d3d9;
	IDirect3DVolumeTexture9	*texture_3d_d3d9;
	IDirect3DCubeTexture9	*texture_cube_d3d9;
	// D3D11 types:
	ID3D11ShaderResourceView *texture_view_d3d11;

	struct RdrTextureObjTypeless *typeless;
} RdrTextureObj;

// Formats not exposed
typedef enum RdrTexFormatInternal
{
	//												// DX9 type
	RTEX_NULL=RTEX_FIRST_INTERNAL_FORMAT,			// D3DFMT_NULL_TEXTURE_FCC
	RTEX_NVIDIA_INTZ,								// D3DFMT_NVIDIA_INTZ_DEPTH_TEXTURE_FCC
	RTEX_NVIDIA_RAWZ,								// D3DFMT_NVIDIA_RAWZ_DEPTH_TEXTURE_FCC
	RTEX_ATI_DF16,									// D3DFMT_ATI_DEPTH_TEXTURE_16_FCC
	RTEX_ATI_DF24,									// D3DFMT_ATI_DEPTH_TEXTURE_24_FCC
	RTEX_D24S8,										// D3DFMT_D24S8
	RTEX_D16,										// D3DFMT_D16
	RTEX_G16R16F, // SBT_RG_FLOAT					// D3DFMT_G16R16F
	RTEX_G16R16, // SBT_RG_FIXED					// D3DFMT_G16R16
	RTEX_A16B16G16R16, // SBT_RGBA_FIXED			// D3DFMT_A16B16G16R16
	RTEX_R5G6B5, // SBT_RGB16						// D3DFMT_R5G6B5
	RTEX_RGBA10, // SBT_RGBA10						// D3DFMT_A2B10G10R10
} RdrTexFormatInternal;

typedef enum RdrSurfaceBindFlags
{
	RSBF_NONE,

	RSBF_RENDERTARGET		=1<<0,
	RSBF_DEPTHSTENCIL		=1<<1,
	RSBF_TEX2D				=1<<2,
	RSBF_SYSTEMMEMORY		=1<<3,
} RdrSurfaceBindFlags;

typedef union DX11BufferObj
{
	ID3D11Buffer *buffer;
	ID3D11Resource *resource; // parent class
} DX11BufferObj;

typedef struct DX11BufferPoolObj
{
	DX11BufferObj obj;	// MUST BE FIRST!
	bool used;
} DX11BufferPoolObj;

typedef union RdrVertexBufferObj
{
	IDirect3DVertexBuffer9 * vertex_buffer_d3d9;
	ID3D11Buffer * vertex_buffer_d3d11;
	ID3D11Resource * vertex_buffer_resource_d3d11;
	struct RdrVertexBufferTypeless *typeless_vertex_buffer;
} RdrVertexBufferObj;

typedef union RdrIndexBufferObj
{
	IDirect3DIndexBuffer9 * index_buffer_d3d9;
	ID3D11Resource * index_buffer_resource_d3d11;
	ID3D11Buffer * index_buffer_d3d11;
	struct RdrIndexBufferTypeless *typeless_index_buffer;
} RdrIndexBufferObj;

typedef union RdrSurfaceObj
{
	IDirect3DSurface9 * surface9;

	ID3D11RenderTargetView * render_target_view11;
	ID3D11DepthStencilView * depth_stencil_view11;
	ID3D11View * view11;

	struct RdrSurfaceTypeless * typeless_surface;
} RdrSurfaceObj;

typedef union RdrTextureBufferObj
{
	ID3D11Texture2D			*texture_2d_d3d11;
	ID3D11Texture3D			*texture_3d_d3d11;
	ID3D11Resource			*resource_d3d11; // Parent class of the above
	void					*typeless;
} RdrTextureBufferObj;

typedef union RdrDXTexFormatObj
{
	D3DFORMAT format_d3d9;
	DXGI_FORMAT format_d3d11;
	DWORD format_typeless;
} RdrDXTexFormatObj;


// XDX_H
#endif
