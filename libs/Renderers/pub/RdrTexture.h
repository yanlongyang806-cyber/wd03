#ifndef _RDRTEXTURE_H_
#define _RDRTEXTURE_H_
GCC_SYSTEM

#include "RdrDevice.h"
#include "RdrTextureEnums.h"

typedef U64 TexHandle;

typedef struct BasicTexture BasicTexture;

typedef struct RdrTexParams
{
	U16				width, height, depth, x, y, z;
	RdrTexFlags		flags;
	U32				max_levels : 4;		// maximum number of levels in the texture
	U32				first_level : 4;	// first level present in the initialization data
	U32				level_count : 4;	// number of levels present in the initialization data
	U32				reversed_mips : 1;
	U32				from_surface : 1;
	U32				need_sub_updating : 1;
	U32				is_srgb : 1;
	U32				refcount_data : 1;
	U32				ringbuffer_data : 1;
	TexHandle		tex_handle;
	TexHandle		src_tex_handle; // if from_surface==1
	RdrTexType		type;
	RdrTexFormatObj	src_format;
	int				anisotropy;

	void			*data;

	const char *	memmonitor_name;

	const BasicTexture * debug_texture_backpointer;
} RdrTexParams;

typedef struct RdrSubTexParams
{
	U16				x_offset, y_offset, z_offset;
	U16				width, height;
	U16				depth; // For CUBEMAP and 3D textures
	TexHandle		tex_handle;
	RdrTexType		type;
	RdrTexFormatObj	src_format;
	bool			refcount_data;
	void			*data;
} RdrSubTexParams;

typedef struct RdrTexStealSnapshotParams
{
	TexHandle tex_handle;
	RdrSurface* surf;
	RdrSurfaceBuffer buffer_num;
	int snapshot_idx;
	const char* memmonitor_name;
	
	BasicTexture* tex_debug_backpointer;
} RdrTexStealSnapshotParams;

typedef struct RdrGetTexInfo
{
	TexHandle		tex_handle;
	int				*widthout, *heightout;
	RdrTexType		type;
	RdrTexFormatObj	src_format;
	U8				**data;
	U32				data_size;
} RdrGetTexInfo;

typedef struct RdrTextureAnisotropy
{
	TexHandle tex_handle;
	int anisotropy;
} RdrTextureAnisotropy;

MemLog * rdrGetTextureMemLog();

// XRenderLib should *only* call the interfaces which take a RdrTexFormatObj for type safety
int rdrGetImageByteCount(RdrTexType tex_type, RdrTexFormatObj tex_format, U32 width, U32 height, U32 depth);
int texIsCompressedFormat(RdrTexFormat format);

int getTextureMemoryUsageEx(RdrTexType tex_type, RdrTexFormat tex_format, U32 width, U32 height, U32 depth,
								   bool mipmap, bool tiledNotLinear, bool packedMipChain);
int getTextureMemoryUsageEx2(RdrTexType tex_type, int bpp, int compressed, U32 width, U32 height, U32 depth,
							 bool mipmap, bool tiledNotLinear, bool packedMipChain);

bool rdrTextureLoadInit(size_t size);
void *rdrTextureLoadAlloc(size_t size);
void rdrTextureLoadFree(void *buf, size_t size);

RdrTexParams *rdrStartUpdateTextureEx(SA_PARAM_NN_VALID RdrDevice *device, SA_PARAM_OP_VALID RdrTexParams *base_params, TexHandle handle, RdrTexType type, RdrTexFormat tex_format, U32 width, U32 height, U32 depth, U32 mip_count, SA_PRE_NN_FREE SA_POST_NN_VALID U32 *image_bytes_count, SA_PARAM_OP_STR const char *memmonitor_name, SA_PARAM_OP_VALID void *refcounted_data, int ringbuffer_data);
__forceinline static RdrTexParams *rdrStartUpdateTexture(SA_PARAM_NN_VALID RdrDevice *device, TexHandle handle, RdrTexType type, RdrTexFormat tex_format, U32 width, U32 height, U32 depth, U32 mip_count, SA_PRE_NN_FREE SA_POST_NN_VALID U32 *image_bytes_count, SA_PARAM_OP_STR const char *memmonitor_name, SA_PARAM_OP_VALID void *refcounted_data)
{
	return rdrStartUpdateTextureEx(device, NULL, handle, type, tex_format, width, height, depth, mip_count, image_bytes_count, memmonitor_name, refcounted_data, 0);
}

RdrTexParams *rdrStartUpdateTextureFromSurface(SA_PARAM_NN_VALID RdrDevice *device, TexHandle handle, RdrTexFormat tex_format, SA_PARAM_NN_VALID RdrSurface *src_surface, RdrSurfaceBuffer buffer_num, int width, int height, SA_PRE_NN_FREE SA_POST_NN_VALID U32 *image_bytes_count);
RdrSubTexParams *rdrStartUpdateSubTexture(SA_PARAM_NN_VALID RdrDevice *device, TexHandle handle, RdrTexType type, RdrTexFormat tex_format, U32 x, U32 y, U32 z, U32 width, U32 height, U32 depth, SA_PRE_NN_FREE SA_POST_NN_VALID U32 *image_bytes_count, SA_PARAM_OP_VALID void *refcounted_data);
__forceinline static void rdrEndUpdateTexture(SA_PARAM_NN_VALID RdrDevice *device) { wtSendCmd(device->worker_thread); }
void rdrTexStealSnapshot(SA_PARAM_NN_VALID RdrDevice *device, TexHandle handle, RdrSurface* surf, RdrSurfaceBuffer buffer_num, int snapshot_idx, const char* memmonitor_name, BasicTexture* debug_backpointer);

__forceinline static void rdrFreeTexture(SA_PARAM_NN_VALID RdrDevice *device, TexHandle handle) { wtQueueCmd(device->worker_thread, RDRCMD_FREETEXTURE, &handle, sizeof(handle)); }
__forceinline static void rdrFreeAllTextures(SA_PARAM_NN_VALID RdrDevice *device) { wtQueueCmd(device->worker_thread, RDRCMD_FREEALLTEXTURES, 0, 0); }

__forceinline static void rdrSwapTexHandles(SA_PARAM_NN_VALID RdrDevice *device, TexHandle handle1, TexHandle handle2) {
	TexHandle handles[2] = {handle1, handle2};
	wtQueueCmd(device->worker_thread, RDRCMD_SWAPTEXHANDLES, &handles[0], sizeof(handles));
}


__forceinline static void rdrSetTextureAnisotropy(SA_PARAM_NN_VALID RdrDevice *device, TexHandle handle, int anisotropy)
{
	RdrTextureAnisotropy params;
	params.tex_handle = handle;
	params.anisotropy = anisotropy;
	wtQueueCmd(device->worker_thread, RDRCMD_SETTEXTUREANISOTROPY, &params, sizeof(params));
}

__forceinline static U8 *rdrGetTextureData(SA_PARAM_NN_VALID RdrDevice *device, TexHandle tex_handle, RdrTexType tex_type, RdrTexFormat tex_format, SA_PRE_OP_FREE SA_POST_OP_VALID int *width, SA_PRE_OP_FREE SA_POST_OP_VALID int *height)
{
	RdrGetTexInfo params={(TexHandle)0};
	U8 *data = 0;
	params.tex_handle = tex_handle;
	params.src_format.format = tex_format;
	params.type = tex_type;
	params.widthout = width;
	params.heightout = height;
	params.data = &data;
	wtQueueCmd(device->worker_thread, RDRCMD_GETTEXINFO, &params, sizeof(params));
	wtFlush(device->worker_thread);
	return data;
}

__forceinline static void rdrGetTextureDataAsync(SA_PARAM_NN_VALID RdrDevice *device, TexHandle tex_handle, RdrTexType tex_type, RdrTexFormat tex_format, SA_PARAM_OP_VALID int *width, SA_PARAM_OP_VALID int *height, SA_PRE_OP_BYTES_VAR(data_size) SA_POST_OP_VALID U8 *data, U32 data_size)
{
	RdrGetTexInfo params={(TexHandle)0};
	params.tex_handle = tex_handle;
	params.src_format.format = tex_format;
	params.type = tex_type;
	params.widthout = width;
	params.heightout = height;
	params.data = (U8**)data;
	params.data_size = data_size;
	wtQueueCmd(device->worker_thread, RDRCMD_GETTEXINFO, &params, sizeof(params));
}


const char *rdrTexFormatName(RdrTexFormatObj rdr_format);
int rdrTexIsCompressedFormat(RdrTexFormatObj format);
int rdrTexBitsPerPixelFromFormat(RdrTexFormatObj tex_format);


#endif //_RDRTEXTURE_H_

