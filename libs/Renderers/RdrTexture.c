#include "RdrTexture.h"
#include "RdrState.h"
#include "RenderLib.h"
#include "UnitSpec.h"
#include "MemRef.h"
#include "MemLog.h"
#include "ImageUtil.h"
#include "MirrorRingBuffer.h"


//////////////////////////////////////////////////////////////////////////
// Textures

static MemLog rdrTextureMemLog;
static MirrorRingBuffer rdrTextureLoadBuffer;

MemLog * rdrGetTextureMemLog()
{
	return &rdrTextureMemLog;
}

bool rdrTextureLoadInit(size_t size)
{
	return mirrorRingCreate(&rdrTextureLoadBuffer, size);
}

void *rdrTextureLoadAlloc(size_t size)
{
	void *ret;

	// Rarely, the client can get into a situation where it is waiting on the file load thread, but
	// not pumping the texture queues. Whether this is a bug or a feature is up for debate.
	// In any case, if space in the ring buffer does not become available soon enough, we will
	// give up and hope the calling code has a fallback path.
	int retry_count = 50;

	if (!rdrTextureLoadBuffer.base || size > mirrorRingMaxSize(&rdrTextureLoadBuffer)) {
		return NULL;
	}

	while ((ret = mirrorRingAlloc(&rdrTextureLoadBuffer, size)) == NULL && retry_count-- > 0) {
		Sleep(1);
	}

	return ret;
}

void rdrTextureLoadFree(void *buf, size_t size)
{
	const void *get =  (U8 *)rdrTextureLoadBuffer.base + rdrTextureLoadBuffer.get;
	assert(get == buf);
	mirrorRingFree(&rdrTextureLoadBuffer, size);
}

int rdrGetImageByteCount(RdrTexType tex_type, RdrTexFormatObj tex_format, U32 width, U32 height, U32 depth)
{
	return (int)imgByteCount(tex_type, tex_format.format, width, height, depth, 1);
}


int texIsCompressedFormat(RdrTexFormat format)
{
	switch (format)
	{
	case RTEX_DXT1:
	case RTEX_DXT3:
	case RTEX_DXT5:
		return 1;
	}
	return 0;
}

int rdrTexIsCompressedFormat(RdrTexFormatObj format)
{
	return texIsCompressedFormat(format.format);
}


int rdrTexBitsPerPixelFromFormat(RdrTexFormatObj tex_format)
{
	switch (tex_format.format)
	{
		xcase RTEX_BGR_U8:
			return 4*8; // We grow to ARGB

		xcase RTEX_BGRA_U8:
			return 4*8;

		xcase RTEX_RGBA_F16:
			return 8*8;

		xcase RTEX_RGBA_F32:
			return 16*8;

		xcase RTEX_R_F32:
			return 4*8;

		xcase RTEX_BGRA_5551:
			return 16;

		xcase RTEX_A_U8:
			return 8;

		xcase RTEX_RGBA_U8:
			return 4*8;

		xcase RTEX_DXT1:
			return 4;

		xcase RTEX_DXT3:
		case RTEX_DXT5:
			return 8;
	}
	assert(0);
}

static int sizeForLevel(int bpp, int compressed, int width, int height, bool tiledNotLinear, int *sizeIfLastMip)
{
	int src_width_blocks = width, src_height_blocks = height;
	int src_height_blocks_orig;
	int pitch;
	int level_size;
	int bytes_per_block;

	bytes_per_block = (bpp * (compressed?16:1)) >> 3;

	if (compressed)
	{
		src_width_blocks = (width + 3) / 4;
		src_height_blocks = (height + 3) / 4;
	}
	// width and height round up to tile size (32x32 blocks (128 texels if compressed!))
	src_width_blocks = NEXTMULTIPLE(src_width_blocks, 32);
	pitch = src_width_blocks * bytes_per_block;
	if (!tiledNotLinear) {
		pitch = NEXTMULTIPLE(pitch, 256);
	}

	src_height_blocks_orig = src_height_blocks;
	// Padded size (aligned so the next mipmap level can follow)
	src_height_blocks = NEXTMULTIPLE(src_height_blocks, 32);
	level_size = pitch * src_height_blocks;
	level_size = NEXTMULTIPLE(level_size, 4096); // Require 4K memory alignment

	if (sizeIfLastMip) {
		// We don't need the tile size padding at the end, just the block stride * the height in blocks
		*sizeIfLastMip = pitch * src_height_blocks_orig;
		*sizeIfLastMip = NEXTMULTIPLE(*sizeIfLastMip, 4096); // Require 4K memory alignment
	}

	return level_size;
}

int getTextureMemoryUsageEx2(RdrTexType tex_type, int bpp, int compressed, U32 width, U32 height, U32 depth,
						   bool mipmap, bool tiledNotLinear, bool packedMipChain)
{
	int ret_size = 0;
	int size_if_last_mip;

	if (tex_type == RTEX_CUBEMAP) {
		depth = 6;
	} else if (tex_type == RTEX_3D) {
		assert(depth > 0);
	} else {
		assert(tex_type == RTEX_2D);
		depth = 1;
	}

	// Calculate the size of the top level
	ret_size = sizeForLevel(bpp, compressed, width, height, tiledNotLinear, &size_if_last_mip);

	if (mipmap && (!packedMipChain || (width > 16 && height > 16)))
	{
		// NP2: top level rounded up, 1st MIP of a 513x513 texture stores 256x256 texels in 512x512 :(
		width = pow2(width);
		height = pow2(height);

		width >>=1;
		height >>=1;
		assert(width || height);
		if (!width)
			width = 1;
		if (!height)
			height = 1;

		while (true) 
		{
			int level_size;
			level_size = sizeForLevel(bpp, compressed, width, height, tiledNotLinear, &size_if_last_mip);

			if ((width <= 16 || height <=16) && packedMipChain) {
				// All mipmaps below this fit into this level, because it's packed!
				ret_size += level_size;
				break;
			}


			width >>=1;
			height >>=1;
			if (!width && !height) {
				ret_size += size_if_last_mip;
				break;
			}
			ret_size += level_size;
			if (!width)
				width = 1;
			if (!height)
				height = 1;
		}
	} else {
		ret_size = size_if_last_mip;
	}
	return ret_size * depth;
}

int getTextureMemoryUsageEx(RdrTexType tex_type, RdrTexFormat tex_format, U32 width, U32 height, U32 depth,
							bool mipmap, bool tiledNotLinear, bool packedMipChain)
{
#ifdef _XBOX
	return getTextureMemoryUsageEx2(tex_type, rdrTexBitsPerPixelFromFormat(MakeRdrTexFormatObj(tex_format)), 
		texIsCompressedFormat(tex_format), width, height, depth, mipmap, tiledNotLinear, packedMipChain);
#else
	return (int)imgByteCount(tex_type, tex_format, width, height, depth, mipmap ? 0 : 1);
#endif
}

// Prints out a chart of texture memory usage
AUTO_COMMAND ACMD_CMDLINE;
void rdrTextureChart(void)
{
	int size;
	int i;
	struct {
		RdrTexFormatObj format;
		char *name;
		int mipmapped;
	} formats[] = {
		{{RTEX_BGR_U8}, "Truecolor", 0},
		{{RTEX_BGR_U8}, "Truecolor", 1},
		{{RTEX_BGRA_U8}, "Truecolor w/Alpha", 0},
		{{RTEX_BGRA_U8}, "Truecolor w/Alpha", 1},
		{{RTEX_DXT1}, "DXT1", 0},
		{{RTEX_DXT1}, "DXT1 (default for Opaque)", 1},
		{{RTEX_DXT5}, "DXT5", 0},
		{{RTEX_DXT5}, "DXT5 (default for alpha)", 1},
	};
	printf("|| || ");
	for (i=0; i<ARRAY_SIZE(formats); i++) {
		printf("%s %s(PC) || ", formats[i].name, formats[i].mipmapped?"(w/ Mips) ":"");
		printf("%s %s(Xbox360) || ", formats[i].name, formats[i].mipmapped?"(w/ Mips) ":"");
	}
	printf("\n");
	for (size=4; size<=2048; size<<=1) 
	{
		printf("|| %4dx%4d |", size, size);
		for (i=0; i<ARRAY_SIZE(formats); i++) {
			printf(" %s |", friendlyBytes(imgByteCount(RTEX_2D, formats[i].format.format, size, size, 1, (formats[i].mipmapped?0:1))));
			printf(FORMAT_OK(strstri(formats[i].name, "default")?" {color:red}%s{color} |":" %s |"),
				friendlyBytes(getTextureMemoryUsageEx2(RTEX_2D, rdrTexBitsPerPixelFromFormat(formats[i].format), 
                    texIsCompressedFormat(formats[i].format.format), size, size, 1, formats[i].mipmapped, true, true)));
		}
		printf("|\n");
	}
}


RdrTexParams *rdrStartUpdateTextureEx(RdrDevice *device, RdrTexParams *base_params, TexHandle handle, RdrTexType type, RdrTexFormat tex_format, U32 width, U32 height, U32 depth, U32 mip_count, U32 *image_bytes_count, const char *memmonitor_name, void *texture_data, int ringbuffer_data)
{
	RdrTexParams *params;

	*image_bytes_count = (U32)imgByteCount(type, tex_format, width, height, depth, mip_count);

	params = wtAllocCmd(device->worker_thread, RDRCMD_UPDATETEXTURE, sizeof(*params) + (texture_data?0:*image_bytes_count));
	if (base_params)
		CopyStructs(params, base_params, 1);
	else
		ZeroStructForce(params);
	params->type = type;
	params->anisotropy = 1;
	params->width = width;
	params->height = height;
	params->depth = depth;
	params->src_format.format = tex_format;
	params->tex_handle = handle;
	assert(0 < mip_count && mip_count < 16);
	params->max_levels = mip_count;
	params->first_level = 0;
	params->level_count = mip_count;
	params->memmonitor_name = memmonitor_name;
	if (texture_data)
	{
		params->data = texture_data;
		if (ringbuffer_data) {
            params->ringbuffer_data = 1;
			params->refcount_data = 0;
		} else {
            params->refcount_data = 1;
			params->ringbuffer_data = 0;
            memrefIncrement(texture_data);
		}
	}
	else
	{
		params->refcount_data = 0;
		params->ringbuffer_data = 0;
		params->data = params+1;
	}
    params->debug_texture_backpointer = 0;

	return params;
}

RdrTexParams *rdrStartUpdateTextureFromSurface(RdrDevice *device, TexHandle handle, RdrTexFormat tex_format, RdrSurface *src_surface, RdrSurfaceBuffer buffer_num, int width, int height, U32 *image_bytes_count)
{
	RdrTexParams *params;


	if( width <= 0 ) {
		width = src_surface->width_nonthread;
	}
	if( height <= 0 ) {
		height = src_surface->height_nonthread;
	}

	//assert(device->nonthread_active_surface != src_surface); This might have problems on the Xbox...

	*image_bytes_count = (U32)imgByteCount(RTEX_2D, tex_format, width, height, 1, 1);

	params = wtAllocCmd(device->worker_thread, RDRCMD_UPDATETEXTURE, sizeof(*params));
	ZeroStructForce(params);
	params->type = RTEX_2D;
	params->anisotropy = 1;
	params->x = 0;
	params->y = 0;
	params->z = 0;
	params->width = width;
	params->height = height;
	params->src_format.format = tex_format;
	params->tex_handle = handle;
	params->src_tex_handle = rdrSurfaceToTexHandle(src_surface, buffer_num);
	params->from_surface = 1;
	params->memmonitor_name = "Textures:Misc";
	params->first_level = 0;
	params->level_count = 1;
	params->max_levels = 1;

	return params;
}

RdrSubTexParams *rdrStartUpdateSubTexture(RdrDevice *device, TexHandle handle, RdrTexType type, RdrTexFormat tex_format, U32 x, U32 y, U32 z, U32 width, U32 height, U32 depth, U32 *image_bytes_count, void *refcounted_data)
{
	RdrSubTexParams *params;

	assert(type == RTEX_1D || type == RTEX_2D);

	*image_bytes_count = (U32)imgByteCount(type, tex_format, width, height, depth, 1);

	params = wtAllocCmd(device->worker_thread, RDRCMD_UPDATESUBTEXTURE, sizeof(*params) + (refcounted_data?0:*image_bytes_count));
	ZeroStructForce(params);
	params->type = type;
	params->x_offset = x;
	params->y_offset = y;
	params->z_offset = z;
	params->width = width;
	params->height = height;
	params->depth = depth;
	params->src_format.format = tex_format;
	params->tex_handle = handle;
	if (refcounted_data)
	{
		params->refcount_data = 1;
		params->data = refcounted_data;
		memrefIncrement(refcounted_data);
	}
	else
	{
		params->refcount_data = 0;
		params->data = params+1;
	}

	return params;
}

void rdrTexStealSnapshot(SA_PARAM_NN_VALID RdrDevice *device, TexHandle handle, RdrSurface* surf, RdrSurfaceBuffer buffer_num, int snapshot_idx, const char* memmonitor_name, BasicTexture* debug_backpointer)
{
	RdrTexStealSnapshotParams* params;

	params = wtAllocCmd( device->worker_thread, RDRCMD_TEXSTEALSNAPSHOT, sizeof( *params ));
	params->tex_handle = handle;
	params->surf = surf;
	params->buffer_num = buffer_num;
	params->snapshot_idx = snapshot_idx;
	params->memmonitor_name = memmonitor_name;
	params->tex_debug_backpointer = debug_backpointer;
	wtSendCmd( device->worker_thread );
}
