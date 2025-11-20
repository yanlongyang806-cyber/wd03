#include "GfxDXT.h"
#include "CrypticDXT.h"
#include "GfxTextures.h"
#include "RdrTexture.h"
#include "MemRef.h"
#include "Color.h"
#include "ImageUtil.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Textures_Misc););

static __forceinline int RoundUpPixelsToNextBlockSize( int pixels )
{
	return (pixels+3) & ~3;
}

const static U8 rgb_5_to_8[] = {
	0,8,16,25,33,41,49,58,
	66,74,82,90,99,107,115,123,
	132,140,148,156,165,173,181,189,
	197,206,214,222,230,239,247,255,
};

bool uncompressRawTexInfo(TexReadInfo *rawInfo, bool reversed_mips) // Uncompresses to RTEX_BGRA_U8, does not do mips
{
	int w=0, h=0, bytedepth, total_size;
	U8 *buffer;
	bool bRet=true;
	if (!rawInfo->depth)
		rawInfo->depth = 1;
	if (rawInfo->tex_format == TEXFMT_RAW_DDS || rawInfo->tex_format == RTEX_DXT1 || rawInfo->tex_format == RTEX_DXT5)
	{
		PERFINFO_AUTO_START("dxtDecompress()", 1);
		if (rawInfo->tex_format == TEXFMT_RAW_DDS)
		{
			assert(rawInfo->depth == 1);
			buffer = dxtDecompressMemRef(&w, &h, &bytedepth, &total_size, rawInfo->texture_data );
			rawInfo->width = w;
			rawInfo->height = h;
		} else {
			int i;
			int image_size_out;
			int image_size_in;
			int bpp = (rawInfo->tex_format == RTEX_DXT1) ? 4 : 8;
			assert(rawInfo->depth == 1 || rawInfo->level_count == 1);
			image_size_out = RoundUpPixelsToNextBlockSize(rawInfo->width) * RoundUpPixelsToNextBlockSize(rawInfo->height) * 4;
			image_size_in = (RoundUpPixelsToNextBlockSize(rawInfo->width) * RoundUpPixelsToNextBlockSize(rawInfo->height) * bpp) >> 3;
			total_size = image_size_out * rawInfo->depth;
			buffer = memrefAlloc(total_size);
			if (reversed_mips) {
				const U8 *src_ptr = rawInfo->texture_data + rawInfo->size;
				U8 *dst_ptr = buffer + image_size_out * rawInfo->depth;
				for (i=0; i<rawInfo->depth; i++) {
					src_ptr -= image_size_in;
					dst_ptr -= image_size_out;
					dxtDecompressDirect(src_ptr, dst_ptr, rawInfo->width, rawInfo->height, rawInfo->tex_format);
				}
			} else {
				const U8 *src_ptr = rawInfo->texture_data;
				U8 *dst_ptr = buffer;
				for (i=0; i<rawInfo->depth; i++) {
					dxtDecompressDirect(src_ptr, dst_ptr, rawInfo->width, rawInfo->height, rawInfo->tex_format);
					src_ptr += image_size_in;
					dst_ptr += image_size_out;
				}
			}
			bytedepth = 4;
			rawInfo->level_count = 1;
		}
		texReadInfoAssignMemRefAlloc(rawInfo, buffer, NULL);

		rawInfo->size = total_size;
		if (bytedepth == 4) {
			rawInfo->tex_format = TEXFMT_ARGB_8888;
		} else if (bytedepth == 3) {
			rawInfo->tex_format = TEXFMT_ARGB_0888;
		} else if (bytedepth == 1) {
			rawInfo->tex_format = RTEX_A_U8;
		} else {
			assert(!"bad bit depth");
		}
		PERFINFO_AUTO_STOP();
	}

	if (rawInfo->tex_format == RTEX_BGRA_U8) {
		// already uncompressed
	} else {
		int i, size;
		size = rawInfo->width*rawInfo->height*rawInfo->depth*4;
		// Uncompress
		switch (rawInfo->tex_format) {
			case TEXFMT_ARGB_8888:
				break;
			case TEXFMT_ARGB_0888:
				PERFINFO_AUTO_START("TEXFMT_ARGB_0888 resample", 1);
				buffer = memrefAlloc(size);
				rawInfo->size = size;
				size = rawInfo->width*rawInfo->height*rawInfo->depth;
				for (i=0; i<size; i++) {
					((U8*)buffer)[i*4] = rawInfo->texture_data[i*3];
					((U8*)buffer)[i*4+1] = rawInfo->texture_data[i*3+1];
					((U8*)buffer)[i*4+2] = rawInfo->texture_data[i*3+2];
					((U8*)buffer)[i*4+3] = 0xff;
				}
				texReadInfoAssignMemRefAlloc(rawInfo, buffer, NULL);
				PERFINFO_AUTO_STOP();
				break;
			case RTEX_BGR_U8:
				PERFINFO_AUTO_START("RTEX_BGR_U8 resample", 1);
				buffer = memrefAlloc(size);
				rawInfo->size = size;
				size = rawInfo->width*rawInfo->height*rawInfo->depth;
				for (i=0; i<size; i++) {
//					((int*)buffer)[i] = *((int*)&rawInfo->data[i*3])|0xff000000;
					((U8*)buffer)[i*4] = rawInfo->texture_data[i*3];
					((U8*)buffer)[i*4+1] = rawInfo->texture_data[i*3+1];
					((U8*)buffer)[i*4+2] = rawInfo->texture_data[i*3+2];
					((U8*)buffer)[i*4+3] = 0xff;
				}
				texReadInfoAssignMemRefAlloc(rawInfo, buffer, NULL);
				PERFINFO_AUTO_STOP();
				break;
			case RTEX_A_U8:
				PERFINFO_AUTO_START("RTEX_A_U8 resample", 1);
				buffer = memrefAlloc(size);
				rawInfo->size = size;
				size = rawInfo->width*rawInfo->height*rawInfo->depth;
				for (i=0; i<size; i++) {
					((U8*)buffer)[i*4] = 0x0;
					((U8*)buffer)[i*4+1] = 0x0;
					((U8*)buffer)[i*4+2] = 0x0;
					((U8*)buffer)[i*4+3] = rawInfo->texture_data[i];
				}
				texReadInfoAssignMemRefAlloc(rawInfo, buffer, NULL);
				PERFINFO_AUTO_STOP();
				break;
			case RTEX_BGRA_5551:
				PERFINFO_AUTO_START("RTEX_BGRA_5551 resample", 1);
				buffer = memrefAlloc(size);
				rawInfo->size = size;
				size = rawInfo->width*rawInfo->height*rawInfo->depth;
				{
					Color5551 *walk = (Color5551*)rawInfo->texture_data;
					for (i=0; i<size; i++, walk++) {
						((U8*)buffer)[i*4] = rgb_5_to_8[walk->b];
						((U8*)buffer)[i*4+1] = rgb_5_to_8[walk->g];
						((U8*)buffer)[i*4+2] = rgb_5_to_8[walk->r];
						((U8*)buffer)[i*4+3] = walk->a*255;
					}
				}
				texReadInfoAssignMemRefAlloc(rawInfo, buffer, NULL);
				PERFINFO_AUTO_STOP();
				break;
			default:
				bRet = false;
				break;
		}
		rawInfo->tex_format = RTEX_BGRA_U8;
	}
	return bRet;
}
