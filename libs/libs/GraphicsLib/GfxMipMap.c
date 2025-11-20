#include "GfxMipMap.h"
#include "GfxTextureTools.h"
#include "CrypticDXT.h"
#include "RdrTexture.h"
#include "ImageUtil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Textures_Misc););

void alphaBorderBuffer(TexOptFlags flags_for_border, U8 *data, int w, int h, RdrTexFormat src_format)
{
	int i;
	assert(src_format == RTEX_BGRA_U8); // Should only get here from TexWords which only uses that format.  Alphs alpha bordering something without alpha makes no sense
	if (flags_for_border & TEXOPT_ALPHABORDER_TB)
	{
		for (i=0; i<w; i++)
		{
			data[i*4 + 3] = 0;
			data[(i + (h-1)*w)*4 + 3] = 0;
		}
	}
	if (flags_for_border & TEXOPT_ALPHABORDER_LR)
	{
		for (i=0; i<h; i++)
		{
			data[i*w*4 + 3] = 0;
			data[(i*w + w-1)*4 + 3] = 0;
		}
	}
}


// Returns pointer off the end of this mip level
static U32* buildMipRGBA(const U32* datain, U32* dataout, int w, int h)
{
	int		i,x,y,y_in[2];
	U32		pixel,rgba[4],*pix;
	int neww = w>>1;
	int newh = h>>1;
	int xmask=1;
	int y2off=1;

	if (neww == 0) {
		neww = 1;
		xmask = 0;
	}
	if (newh == 0) {
		newh = 1;
		y2off = 0;
	}

	for(pix=dataout,y=0;y<newh;y++)
	{
		y_in[0] = (y*2+0) * w;
		y_in[1] = (y*2+y2off) * w;
		for(x=0;x<neww;x++)
		{
			ZeroStruct(&rgba);
			for(i=0;i<4;i++)
			{
				pixel = datain[y_in[i>>1] + (x<<1) + (i&xmask)];
				rgba[0] += (pixel >> (8 * 0)) & 255;
				rgba[1] += (pixel >> (8 * 1)) & 255;
				rgba[2] += (pixel >> (8 * 2)) & 255;
				rgba[3] += (pixel >> (8 * 3)) & 255;
			}
			*pix++ = (rgba[0] >> 2) << (8 * 0)
				| (rgba[1] >> 2) << (8 * 1)
				| (rgba[2] >> 2) << (8 * 2)
				| (rgba[3] >> 2) << (8 * 3);
		}
	}
	return pix;
}
static U8* buildMipRGB(const U8* datain, U8* dataout, int w, int h)
{
	int		i,x,y,y_in[2];
	U32		rgba[3];
	const U8 *pixin;
	U8 *pixout;
	int neww = w>>1;
	int newh = h>>1;
	int xmask = 1;
	int y2off = 1;

	// JE: This is untested, but probably works ;)

	if (neww == 0) {
		neww = 1;
		xmask = 0;
	}

	if (newh == 0) {
		newh = 1;
		y2off = 0;
	}

	for(pixout=dataout,y=0;y<newh;y++)
	{
		y_in[0] = (y*2+0) * w;
		y_in[1] = (y*2+y2off) * w;
		for(x=0;x<neww;x++)
		{
			ZeroStruct(&rgba);
			for(i=0;i<4;i++)
			{
				pixin = &datain[(y_in[i>>1] + (x<<1) + (i&xmask)) * 3];
				rgba[0] += pixin[0];
				rgba[1] += pixin[1];
				rgba[2] += pixin[2];
			}
			*pixout++ = (rgba[0] >> 2);
			*pixout++ = (rgba[1] >> 2);
			*pixout++ = (rgba[2] >> 2);
		}
	}
	return pixout;
}

U8 *buildMipMaps(TexOptFlags flags_for_border, const U8 *data, int width, int height, RdrTexFormat src_format, RdrTexFormat dst_format, int *data_size_out)
{
	int w = width;
	int h = height;
	int mip_count;
	U8 *mipdata;
	int mipdata_size;
	U8 *dxtmipdata=NULL, *dxtmipdata_level=NULL;
	U8 *level;
	const U8 *lastlevel;

	if (!isPower2(width) || !isPower2(height))
		return NULL;
	if (src_format != RTEX_BGRA_U8 && src_format != RTEX_BGR_U8)
		return NULL;
	if (!(src_format == dst_format || (src_format == RTEX_BGRA_U8 && (dst_format == RTEX_DXT1 || dst_format == RTEX_DXT5))))
		return NULL;
	mip_count = imgLevelCount(w>>1, h>>1, 1);
	mipdata_size = imgByteCount(RTEX_2D, src_format, w>>1, h>>1, 0, mip_count);
	if (dst_format == RTEX_DXT1 || dst_format == RTEX_DXT5) {
		int dxtmipdata_size = imgByteCount(RTEX_2D, dst_format, w>>1, h>>1, 0, mip_count);
		dxtmipdata_level = dxtmipdata = malloc(dxtmipdata_size);
		if (data_size_out)
			*data_size_out = dxtmipdata_size;
	} else {
		if (data_size_out)
			*data_size_out = mipdata_size;
	}
	mipdata = malloc(mipdata_size);

	lastlevel=data;
	level = mipdata;
	while (mip_count) {
		U8 *saved = level;
		switch(src_format) {
		xcase RTEX_BGRA_U8:
			level = (U8*)buildMipRGBA((const U32*)lastlevel, (U32*)level, w, h);
		xcase RTEX_BGR_U8:
			level = buildMipRGB(lastlevel, level, w, h);
		}
		lastlevel = saved;
		mip_count--;
		w >>=1;
		if (w==0)
			w = 1;
		h >>=1;
		if (h==0)
			h = 1;
		if (flags_for_border & TEXOPT_ALPHABORDER)
			alphaBorderBuffer(flags_for_border, saved, w, h, src_format);
		if (dst_format == RTEX_DXT1 || dst_format == RTEX_DXT5) {
			dxtmipdata_level = dxtCompress(lastlevel, dxtmipdata_level, w, h, src_format, dst_format);
			assert(dxtmipdata_level);
		}
	}
	if (dxtmipdata) {
		SAFE_FREE(mipdata);
		return dxtmipdata;
	}
	return mipdata;
}
