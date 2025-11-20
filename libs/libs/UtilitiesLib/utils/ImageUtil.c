#pragma once

#include "ImageUtil.h"

static const TexBlockInfo gBlockInfos[] = {
	{ 1, 1, 0, 0, 3 },	// RTEX_BGR_U8
	{ 1, 1, 0, 0, 4 },	// RTEX_BGRA_U8
	{ 1, 1, 0, 0, 8 },	// RTEX_RGBA_F16
	{ 1, 1, 0, 0, 16 },	// RTEX_RGBA_F32
	{ 1, 1, 0, 0, 1 },	// RTEX_unused_was_LA8
	{ 4, 4, 2, 2, 8 },	// RTEX_DXT1
	{ 4, 4, 2, 2, 16 },	// RTEX_DXT3
	{ 4, 4, 2, 2, 16 },	// RTEX_DXT3
	{ 1, 1, 0, 0, 4 },	// RTEX_unused_was_depth
	{ 1, 1, 0, 0, 4 },	// RTEX_R_F32
	{ 1, 1, 0, 0, 2 },	// RTEX_BGRA_5551
	{ 1, 1, 0, 0, 1 },	// RTEX_A_U8
	{ 1, 1, 0, 0, 4 },	// RTEX_RGBA_U8
	{ 1, 1, 0, 0, 0 },	// RTEX_INVALID_FORMAT
};

const TexBlockInfo *imgBlockInfo(RdrTexFormat tex_format)
{
	tex_format = CLAMP(tex_format, 0, RTEX_INVALID_FORMAT);
	return &gBlockInfos[tex_format];
}

int imgBitsPerPixel(RdrTexFormat tex_format)
{
	switch (tex_format)
	{
		xcase RTEX_BGR_U8:
			return 3*8;

		xcase RTEX_BGRA_U8:
			return 4*8;

		xcase RTEX_RGBA_F16:
			return 8*8;

		xcase RTEX_RGBA_F32:
			return 16*8;

		xcase RTEX_R_F32:
			return 4*8;

		xcase RTEX_BGRA_5551:
			return 2*8;

		xcase RTEX_A_U8:
			return 8;

		xcase RTEX_DXT1:
			return 4;

		xcase RTEX_DXT3:
		case RTEX_DXT5:
			return 8;
	}
	assert(0);
	return 0;
}

U32 imgLevelCount(U32 width, U32 height, U32 depth)
{
	return 1 + log2(MAX(MAX(width, height), depth));
}

U32 imgMinPitch(RdrTexFormat tex_format, U32 width)
{
	const TexBlockInfo *blockInfo = imgBlockInfo(tex_format);
	U32 blocksW = (width + blockInfo->width - 1) >> blockInfo->wshift;

	return blocksW * blockInfo->size;
}

U32 imgBlockCount(RdrTexFormat tex_format, U32 width, U32 height)
{
	const TexBlockInfo *blockInfo = imgBlockInfo(tex_format);

	U32 blocksW = (width + blockInfo->width - 1) >> blockInfo->wshift;
	U32 blocksH = (height + blockInfo->height - 1) >> blockInfo->hshift;

	return blocksW * blocksH;
}

size_t imgByteCount(RdrTexType tex_type, RdrTexFormat tex_format, U32 width, U32 height, U32 depth, U32 levels)
{
	size_t ret=0;
	const TexBlockInfo *blockInfo = imgBlockInfo(tex_format);
	U32 maxLevels = imgLevelCount(width, height, depth);
	U32 faces = 1;

	switch (tex_type)
	{
		xcase RTEX_1D:
            height = depth = 1;

        xcase RTEX_2D:
            depth = 1;

        xcase RTEX_CUBEMAP:
            faces = 6;
            depth = 1;
	}

	if (levels < 1 || levels > maxLevels) {
		levels = maxLevels;
	}

	for (; levels; --levels) {
		U32 blocksW = (width + blockInfo->width - 1) >> blockInfo->wshift;
		U32 blocksH = (height + blockInfo->height - 1) >> blockInfo->hshift;

        ret += blocksW * blocksH * blockInfo->size * depth;

		width = MAX(width >> 1, 1);
		height = MAX(height >> 1, 1);
		depth = MAX(depth >> 1, 1);
	}

	return ret * faces;
}
