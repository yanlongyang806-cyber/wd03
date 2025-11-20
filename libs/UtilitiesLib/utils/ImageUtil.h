#pragma once

#include "ImageTypes.h"

// description of a block-compression texture format
typedef struct TexBlockInfo {
	U16 width;		// texels per block in the u direction
	U16 height;		// texels per block in the v direction
	U16 wshift;		// log2 of width
	U16 hshift;		// log2 of height
	U32 size;		// bytes per block
} TexBlockInfo;

int imgBitsPerPixel(RdrTexFormat tex_format);

// Returns the total size of a texture with the given type, format, dimensions, and level count.
// If levels 0, assumes a full mip chain.
size_t imgByteCount(RdrTexType tex_type, RdrTexFormat tex_format, U32 width, U32 height, U32 depth, U32 levels);

// Returns the number of levels a texture of the given dimensions would have.
U32 imgLevelCount(U32 width, U32 height, U32 depth);

// Returns the smallest pitch possible for a texture of the given format and width.
U32 imgMinPitch(RdrTexFormat tex_format, U32 width);

// Returns the number of texel blocks in a texture with the given format and dimensions.
U32 imgBlockCount(RdrTexFormat tex_format, U32 width, U32 height);

// Returns a pointer to a TexBlockInfo struct for the given format.
const TexBlockInfo *imgBlockInfo(RdrTexFormat tex_format);

#include "AutoGen/ImageTypes_h_ast.h"
