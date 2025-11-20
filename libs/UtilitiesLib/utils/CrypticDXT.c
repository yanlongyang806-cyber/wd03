#include "CrypticDXT.h"
#include "timing.h"

#include "wininclude.h"
#include "DirectDrawTypes.h"
#include "endian.h"
#include "Color.h"
#include "MemRef.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Textures_Misc););

DXT5Data dxt5_symbol; // So the struct tpye gets in the PDB

#define EmitU8(dest, value) *(U8*)(dest) = (value); (dest)+=1
#define EmitU16(dest, value) *(U16*)(dest) = endianSwapIfBig(U16, (value)); (dest)+=2
#define EmitU32(dest, value) *(U32*)(dest) = endianSwapIfBig(U32, (value)); (dest)+=4

static __forceinline int RoundUpPixelsToNextBlockSize( int pixels )
{
	return (pixels+3) & ~3;
}
static __forceinline int GetBlocksFromPixels( int pixels )
{
	return (pixels+3)/4;
}

void dxtuncompress_block(RdrTexFormat tex_format, const U8 *src, U8 *dest, int dest_stride)
{
	// Assumes data is in little endian format
	// Does the union work right cross-endianness?
	int i, j;
	Color colors[4];
	Color565 color0, color1;
	U32 idx=0;
	static U8 dxt3_alpha_table[16] = {0*255/15, 1*255/15, 2*255/15, 3*255/15, 4*255/15, 5*255/15, 6*255/15, 7*255/15, 8*255/15, 9*255/15, 10*255/15, 11*255/15, 12*255/15, 13*255/15, 14*255/15, 15*255/15};
	U8 *orig_dest = dest;
	bool alpha_seperate = tex_format != RTEX_DXT1;

	// Funny stuff since the ++s happen after the entire line is evaluated
#define GETU8 (src++[0])
#define GETU16 (GETU8) | ((src++[1]) << 8)    // *((U16*)src)++; <- Not endian safe
#define GETU24 (GETU16) | ((src++[2]) << 16)
#define GETU32 (GETU24) | ((src++[3]) << 24) // *((U32*)src)++; <- Not endian safe

	// Alpha block
	dest = orig_dest;
	if (tex_format == RTEX_DXT3) {
		for (j=0; j<4; j++) {
			if (j==0 || j==2) {
				idx = GETU32;
			}
			for (i=0; i<4; i++) {
				U8 index = idx & 15;
				idx >>= 4;
				dest+=3;
				*dest++ = dxt3_alpha_table[index];
			}
			dest += dest_stride - 4*4;
		}
	} else if (tex_format == RTEX_DXT5) {
		U8 alpha[8];
		alpha[0] = GETU8;
		alpha[1] = GETU8;
		// 8-alpha or 6-alpha block?    
		if (alpha[0] > alpha[1]) {    
			// 8-alpha block:  derive the other six alphas.    
			// Bit code 000 = alpha[0], 001 = alpha[1], others are interpolated.
			alpha[2] = (6 * alpha[0] + 1 * alpha[1] + 3) / 7;    // bit code 010
			alpha[3] = (5 * alpha[0] + 2 * alpha[1] + 3) / 7;    // bit code 011
			alpha[4] = (4 * alpha[0] + 3 * alpha[1] + 3) / 7;    // bit code 100
			alpha[5] = (3 * alpha[0] + 4 * alpha[1] + 3) / 7;    // bit code 101
			alpha[6] = (2 * alpha[0] + 5 * alpha[1] + 3) / 7;    // bit code 110
			alpha[7] = (1 * alpha[0] + 6 * alpha[1] + 3) / 7;    // bit code 111  
		}    
		else {  
			// 6-alpha block.    
			// Bit code 000 = alpha[0], 001 = alpha[1], others are interpolated.
			alpha[2] = (4 * alpha[0] + 1 * alpha[1] + 2) / 5;    // Bit code 010
			alpha[3] = (3 * alpha[0] + 2 * alpha[1] + 2) / 5;    // Bit code 011
			alpha[4] = (2 * alpha[0] + 3 * alpha[1] + 2) / 5;    // Bit code 100
			alpha[5] = (1 * alpha[0] + 4 * alpha[1] + 2) / 5;    // Bit code 101
			alpha[6] = 0;                                      // Bit code 110
			alpha[7] = 255;                                    // Bit code 111
		}
		for (j=0; j<4; j++) {
			if (j==0 || j==2) {
				idx = GETU24;
			}
			for (i=0; i<4; i++) {
				U8 index = idx & 7;
				idx >>= 3;
				dest+=3;
				*dest++ = alpha[index];
			}
			dest += dest_stride - 4*4;
		}
	}

	// Color block 
	dest = orig_dest;
	color0.integer = GETU16;
	color1.integer = GETU16;
	if (isBigEndian()) {
		// the integer is right, but the bitfield doesn't work right, so swap them so that .r means the high bits of the integer
		U8 temp = color0.r;
		color0.r = color0.b;
		color0.b = temp;
		temp = color1.r;
		color1.r = color1.b;
		color1.b = temp;
	}
	colors[0].r = color0.r * 255 / 31;
	colors[0].g = color0.g * 255 / 63;
	colors[0].b = color0.b * 255 / 31;
	colors[1].r = color1.r * 255 / 31;
	colors[1].g = color1.g * 255 / 63;
	colors[1].b = color1.b * 255 / 31;
	colors[0].a = 255;
	colors[1].a = 255;
	if (color0.integer > color1.integer || tex_format != RTEX_DXT1) {
		// 4 color
		for (i=0; i<4; i++) {
			colors[2].rgba[i] = (2 * colors[0].rgba[i] + colors[1].rgba[i] + 1) / 3;
			colors[3].rgba[i] = (colors[0].rgba[i] + 2 * colors[1].rgba[i] + 1) / 3;
		}
	} else {
		for (i=0; i<4; i++) {
			colors[2].rgba[i] = (colors[0].rgba[i] + colors[1].rgba[i]) / 2;
		}
		colors[3].r = colors[3].g = colors[3].b = colors[3].a = 0;
	}
	idx = GETU32;
	for (j=0; j<4; j++) {
		for (i=0; i<4; i++) {
			U8 index = idx & 3;
			idx >>= 2;
			if (alpha_seperate) {
				*dest++ = colors[index].b;
				*dest++ = colors[index].g;
				*dest++ = colors[index].r;
				dest++;
			} else {
#if 0 // RGBA
				*(U32*)dest = colors[index].integer;
				((U32*)dest)++;
#else // BGRA
				*dest++ = colors[index].b;
				*dest++ = colors[index].g;
				*dest++ = colors[index].r;
				*dest++ = colors[index].a;
#endif
			}
		}
		dest += dest_stride - 4*4;
	}
}

RdrTexFormat texFormatFromDDSD( const DDSURFACEDESC2 *ddsd )
{
	switch(ddsd->ddpfPixelFormat.dwFourCC)
	{
	case FOURCC_DXT1:
		return RTEX_DXT1;
	case FOURCC_DXT3:
		return RTEX_DXT3;
	case FOURCC_DXT5:
		return RTEX_DXT5;
	default:
		if (ddsd->ddpfPixelFormat.dwFlags == DDS_RGBA && ddsd->ddpfPixelFormat.dwRGBBitCount == 32 && ddsd->ddpfPixelFormat.dwRGBAlphaBitMask == 0xff000000)
			return RTEX_BGRA_U8;
		else if (ddsd->ddpfPixelFormat.dwFlags == DDS_RGB  && ddsd->ddpfPixelFormat.dwRGBBitCount == 24)
			return RTEX_BGR_U8;
		else if (ddsd->ddpfPixelFormat.dwFlags == DDS_RGBA && ddsd->ddpfPixelFormat.dwRGBBitCount == 16 && ddsd->ddpfPixelFormat.dwRGBAlphaBitMask == 0x00008000) 
			return RTEX_BGRA_5551;
		else if (ddsd->ddpfPixelFormat.dwFlags == DDS_ALPHA && ddsd->ddpfPixelFormat.dwRGBBitCount == 8 && ddsd->ddpfPixelFormat.dwRGBAlphaBitMask == 0x000000ff)
			return RTEX_A_U8;
		else
		{
			assertmsg(0, "Cannot determine texture compression type");
			return (RdrTexFormat)-1;
		}
	}
}

void dxtDecompressDirect(const U8 *src_data, U8 *dst_data, int width, int height, RdrTexFormat tex_format)
{
	int blockSize = (tex_format == RTEX_DXT1) ? 8 : 16;
	int source_size;
	int num_blocks;
	int x, y, block_num;

	source_size = GetBlocksFromPixels(width)*GetBlocksFromPixels(height)*blockSize;

	num_blocks = source_size / blockSize;
	// Walk through all blocks and decode them
	for (x=0, y=0, block_num=0; block_num < num_blocks; block_num++, src_data += blockSize)
	{
		dxtuncompress_block(tex_format, src_data, &dst_data[(y*width+x)*4], width*4);
		x+=4;
		if (x>=width) {
			x=0;
			y+=4;
		}
	}
}

U8* dxtDecompressMemRef(int *w, int *h, int *depth, int *size, const U8* data)
{
	RdrTexFormat tex_format;
	DDSURFACEDESC2	ddsd;

	if (strncmp(data, "DDS ", 4)!=0) {
		assertmsg(0, "Bad header data on DXT chunk!");
		return NULL;
	}
	data+=4;
	memcpy(&ddsd, data, sizeof(ddsd));
	data+=sizeof(ddsd);

	tex_format = texFormatFromDDSD(&ddsd);
	if (tex_format == (RdrTexFormat)-1)
		return 0;

	if (tex_format != RTEX_DXT1 && tex_format != RTEX_DXT3 && tex_format != RTEX_DXT5)
	{
		// Uncompressed, simply copy and return it!
		U8 *dest;
		int bpp = 4;
		assert(tex_format != RTEX_BGRA_5551); // Not supported here
		*w = ddsd.dwWidth;
		*h = ddsd.dwHeight;
		*depth = 4;
		if (tex_format == RTEX_BGR_U8)
			*depth = bpp = 3;
		if (tex_format == RTEX_A_U8)
			*depth = bpp = 1;
		*size = ddsd.dwWidth * ddsd.dwHeight * bpp;
		dest = memrefAlloc(*size);
		memcpy(dest, data, *size);
		return dest;
	}

	{
		int width = RoundUpPixelsToNextBlockSize(ddsd.dwWidth);
		int height = RoundUpPixelsToNextBlockSize(ddsd.dwHeight);
		int dest_size = width * height * 4;
		U8 *dest = memrefAlloc(dest_size);
		*w = width;
		*h = height;
		*depth = 4;
		*size = dest_size;
		dxtDecompressDirect(data, dest, width, height, tex_format);
		return dest;
	}
}

#if PLATFORM_CONSOLE // Big Endian
typedef Color ColorType;
#else
typedef ColorBGRA ColorType;
#endif
typedef ColorType ColorBlock[4][4];

#define ALPHA_CUTOFF 0x7F

// Algorithm from Intel at http://www.intel.com/cd/ids/developer/asmo-na/eng/324337.htm
// See paper for optimized assembly code

int ColorDistance( ColorType c1, ColorType c2 ) {
	return ( ( c1.r - c2.r ) * ( c1.r - c2.r ) ) +
		( ( c1.g - c2.g ) * ( c1.g - c2.g ) ) +
		( ( c1.b - c2.b ) * ( c1.b - c2.b ) );
}
void GetMinMaxColorsEuclidean( const ColorType *colorBlock, ColorType *minColor, ColorType *maxColor ) {
	int maxDistance = -1;
	int i, j;
	for ( i = 0; i < 16; i++ ) {
		if (colorBlock[i].a >= ALPHA_CUTOFF) {
			for ( j = i; j < 16; j++ ) { // Not i+1 to catch the case with a single non-alpha'd pixel
				if (colorBlock[j].a >= ALPHA_CUTOFF) {
					int distance = ColorDistance( colorBlock[i], colorBlock[j] );
					if ( distance > maxDistance ) {
						maxDistance = distance;
						*minColor = colorBlock[i];
						*maxColor = colorBlock[j];
					}
				}
			}
		}
	}
	assert(maxDistance != -1); // Should be caught before this!
}

void GetMinMaxColorsEuclideanAlpha( const ColorType *colorBlock, ColorType *minColor, ColorType *maxColor ) {
	int maxDistance = -1;
	int i, j;
	int minAlpha=255, maxAlpha=0;
	// JE: This is assuming the color and alpha channels are independnt, and doing
	//   a poor job when you've got a color next to invisible white, with something
	//   dark in between (chooses white + dark, no color)
	// If we don't care about losing color on the low-alpha pixels, we can do what
	//   we do for DXT1.
	for ( i = 0; i < 16; i++ ) {
		for ( j = i; j < 16; j++ ) { // Not i+1 to catch the case with a single non-alpha'd pixel
			int distance = ColorDistance( colorBlock[i], colorBlock[j] );
			if ( distance > maxDistance ) {
				maxDistance = distance;
				*minColor = colorBlock[i];
				*maxColor = colorBlock[j];
			}
		}
		MIN1(minAlpha, colorBlock[i].a);
		MAX1(maxAlpha, colorBlock[i].a);
	}
	assert(maxDistance != -1); // Should be caught before this!
	if (minAlpha == 255 || maxAlpha == 0) {
		minColor->a = 0;
		maxColor->a = 255;
	} else {
		minColor->a = minAlpha;
		maxColor->a = (maxAlpha==minAlpha)?(minAlpha+1):maxAlpha;
	}
}

#define INSET_SHIFT 4 // inset the bounding box with ( range >> shift )
void GetMinMaxColorsBB( const U8 *colorBlock, U8 *minColor, U8 *maxColor ) {
	int i;
	U8 inset[3];
	minColor[0] = minColor[1] = minColor[2] = 255;
	maxColor[0] = maxColor[1] = maxColor[2] = 0;
	for ( i = 0; i < 16; i++ ) {
		if ( colorBlock[i*4+0] < minColor[0] ) { minColor[0] = colorBlock[i*4+0]; }
		if ( colorBlock[i*4+1] < minColor[1] ) { minColor[1] = colorBlock[i*4+1]; }
		if ( colorBlock[i*4+2] < minColor[2] ) { minColor[2] = colorBlock[i*4+2]; }
		if ( colorBlock[i*4+0] > maxColor[0] ) { maxColor[0] = colorBlock[i*4+0]; }
		if ( colorBlock[i*4+1] > maxColor[1] ) { maxColor[1] = colorBlock[i*4+1]; }
		if ( colorBlock[i*4+2] > maxColor[2] ) { maxColor[2] = colorBlock[i*4+2]; }
	}
	inset[0] = ( maxColor[0] - minColor[0] ) >> INSET_SHIFT;
	inset[1] = ( maxColor[1] - minColor[1] ) >> INSET_SHIFT;
	inset[2] = ( maxColor[2] - minColor[2] ) >> INSET_SHIFT;

	minColor[0] = ( minColor[0] + inset[0] <= 255 ) ? minColor[0] + inset[0] : 255;
	minColor[1] = ( minColor[1] + inset[1] <= 255 ) ? minColor[1] + inset[1] : 255;
	minColor[2] = ( minColor[2] + inset[2] <= 255 ) ? minColor[2] + inset[2] : 255;
	maxColor[0] = ( maxColor[0] >= inset[0] ) ? maxColor[0] - inset[0] : 0;
	maxColor[1] = ( maxColor[1] >= inset[1] ) ? maxColor[1] - inset[1] : 0;
	maxColor[2] = ( maxColor[2] >= inset[2] ) ? maxColor[2] - inset[2] : 0;
}

#define C565_5_MASK 0xF8 // 0xFF minus last three bits
#define C565_6_MASK 0xFC // 0xFF minus last two bits
U32 EmitColorIndices( const U8 *colorBlock, const U8 *minColor, const U8 *maxColor )
{
	U16 colors[4][4];
	U32 result = 0;
	int i;
	colors[0][0] = ( maxColor[0] & C565_5_MASK ) | ( maxColor[0] >> 5 );
	colors[0][1] = ( maxColor[1] & C565_6_MASK ) | ( maxColor[1] >> 6 );
	colors[0][2] = ( maxColor[2] & C565_5_MASK ) | ( maxColor[2] >> 5 );
	colors[1][0] = ( minColor[0] & C565_5_MASK ) | ( minColor[0] >> 5 );
	colors[1][1] = ( minColor[1] & C565_6_MASK ) | ( minColor[1] >> 6 );
	colors[1][2] = ( minColor[2] & C565_5_MASK ) | ( minColor[2] >> 5 );
	colors[2][0] = ( 2 * colors[0][0] + 1 * colors[1][0] ) / 3;
	colors[2][1] = ( 2 * colors[0][1] + 1 * colors[1][1] ) / 3;
	colors[2][2] = ( 2 * colors[0][2] + 1 * colors[1][2] ) / 3;
	colors[3][0] = ( 1 * colors[0][0] + 2 * colors[1][0] ) / 3;
	colors[3][1] = ( 1 * colors[0][1] + 2 * colors[1][1] ) / 3;
	colors[3][2] = ( 1 * colors[0][2] + 2 * colors[1][2] ) / 3;
	for (i = 15; i >= 0; i--)
	{
		int c0 = colorBlock[i*4+0];
		int c1 = colorBlock[i*4+1];
		int c2 = colorBlock[i*4+2];
		int d0 = abs( colors[0][0] - c0 ) + abs( colors[0][1] - c1 ) + abs( colors[0][2] - c2 );
		int d1 = abs( colors[1][0] - c0 ) + abs( colors[1][1] - c1 ) + abs( colors[1][2] - c2 );
		int d2 = abs( colors[2][0] - c0 ) + abs( colors[2][1] - c1 ) + abs( colors[2][2] - c2 );
		int d3 = abs( colors[3][0] - c0 ) + abs( colors[3][1] - c1 ) + abs( colors[3][2] - c2 );
		int b0 = d0 > d3;
		int b1 = d1 > d2;
		int b2 = d0 > d2;
		int b3 = d1 > d3;
		int b4 = d2 > d3;
		int x0 = b1 & b2;
		int x1 = b0 & b3;
		int x2 = b0 & b4;
		result |= ( x2 | ( ( x0 | x1 ) << 1 ) ) << ( i << 1 );
	}
	return result;
}

U32 EmitColorIndicesAlpha( const U8 *colorBlock, const U8 *minColor, const U8 *maxColor )
{
	U16 colors[3][4];
	U32 result = 0;
	int i;
	colors[0][0] = ( maxColor[0] & C565_5_MASK ) | ( maxColor[0] >> 5 );
	colors[0][1] = ( maxColor[1] & C565_6_MASK ) | ( maxColor[1] >> 6 );
	colors[0][2] = ( maxColor[2] & C565_5_MASK ) | ( maxColor[2] >> 5 );
	colors[1][0] = ( minColor[0] & C565_5_MASK ) | ( minColor[0] >> 5 );
	colors[1][1] = ( minColor[1] & C565_6_MASK ) | ( minColor[1] >> 6 );
	colors[1][2] = ( minColor[2] & C565_5_MASK ) | ( minColor[2] >> 5 );
	colors[2][0] = ( colors[0][0] + colors[1][0] ) / 2;
	colors[2][1] = ( colors[0][1] + colors[1][1] ) / 2;
	colors[2][2] = ( colors[0][2] + colors[1][2] ) / 2;
// 	colors[3][0] = 0;
// 	colors[3][1] = 0;
// 	colors[3][2] = 0;
// 	colors[3][3] = 0;
	for (i = 15; i >= 0; i--)
	{
		if (colorBlock[i*4+3] < ALPHA_CUTOFF) {
			result |= 3 << (i << 1);
		} else {
			int c0 = colorBlock[i*4+0];
			int c1 = colorBlock[i*4+1];
			int c2 = colorBlock[i*4+2];
			int d0 = abs( colors[0][0] - c0 ) + abs( colors[0][1] - c1 ) + abs( colors[0][2] - c2 );
			int d1 = abs( colors[1][0] - c0 ) + abs( colors[1][1] - c1 ) + abs( colors[1][2] - c2 );
			int d2 = abs( colors[2][0] - c0 ) + abs( colors[2][1] - c1 ) + abs( colors[2][2] - c2 );
			int b0 = d0 > d1;
			int b1 = d0 > d2;
			int b2 = d1 > d2;
			int x0 = !b2 && b0;
			int x1 = b1 & b2;
			result |= ( x0 | ( x1 << 1 ) ) << ( i << 1 );
		}
	}
	return result;
}

char *EmitAlphaIndices( char *dest, const U8 *colorBlock, const U8 minAlpha, const U8 maxAlpha )
{
	int i;
	U8 indices[16];
	U8 mid = ( maxAlpha - minAlpha ) / ( 2 * 7 );
	U8 ab1 = minAlpha + mid;
	U8 ab2 = ( 6 * maxAlpha + 1 * minAlpha ) / 7 + mid;
	U8 ab3 = ( 5 * maxAlpha + 2 * minAlpha ) / 7 + mid;
	U8 ab4 = ( 4 * maxAlpha + 3 * minAlpha ) / 7 + mid;
	U8 ab5 = ( 3 * maxAlpha + 4 * minAlpha ) / 7 + mid;
	U8 ab6 = ( 2 * maxAlpha + 5 * minAlpha ) / 7 + mid;
	U8 ab7 = ( 1 * maxAlpha + 6 * minAlpha ) / 7 + mid;
	U64 ret=0;
	assert( maxAlpha > minAlpha );
	colorBlock += 3;
	for (i = 0; i < 16; i++ ) {
		U8 a = colorBlock[i*4];
		int b1 = ( a <= ab1 );
		int b2 = ( a <= ab2 );
		int b3 = ( a <= ab3 );
		int b4 = ( a <= ab4 );
		int b5 = ( a <= ab5 );
		int b6 = ( a <= ab6 );
		int b7 = ( a <= ab7 );
		int index = ( b1 + b2 + b3 + b4 + b5 + b6 + b7 + 1 ) & 7;
		indices[i] = index ^ ( 2 > index );
	}
 	EmitU8(dest, (indices[ 0] >> 0) | (indices[ 1] << 3) | (indices[ 2] << 6) );
 	EmitU8(dest, (indices[ 2] >> 2) | (indices[ 3] << 1) | (indices[ 4] << 4) | (indices[ 5] << 7) );
 	EmitU8(dest, (indices[ 5] >> 1) | (indices[ 6] << 2) | (indices[ 7] << 5) );
 	EmitU8(dest, (indices[ 8] >> 0) | (indices[ 9] << 3) | (indices[10] << 6) );
 	EmitU8(dest, (indices[10] >> 2) | (indices[11] << 1) | (indices[12] << 4) | (indices[13] << 7) );
 	EmitU8(dest, (indices[13] >> 1) | (indices[14] << 2) | (indices[15] << 5) );
	return dest;
}

static __forceinline void swizzleRGBAToBGRA(const U8* src, U8* dst)
{
	assert(src!=dst);
	dst[0] = src[2];
	dst[1] = src[1];
	dst[2] = src[0];
	dst[3] = src[3];
}

static U8 * dxtcompress_block_dxt1_intel(const U8 *_src, U8 *_dest, int src_stride, int blockw, int blockh, RdrTexFormat src_format)
{
	int cx, cy;
	const U8 *src=_src;
	U8 *dest=_dest;
	Color565 color0, color1;
	ColorType min;
	ColorType max;
	ColorBlock colorBlock;
	bool bHasNonAlpha=false;
	bool bHasAlpha=false;
	U32 idx=0;
	PERFINFO_AUTO_START_FUNC();

	// Extract the colors
	src_stride-=(blockw-1)*4;
	for (cy=0; cy<4; cy++) {
		for (cx=0; cx<4; cx++) {
			ColorType c;
			if (cx < blockw-1)
				src+=4;

			if (src_format == RTEX_RGBA_U8) {
				swizzleRGBAToBGRA(src,c.bgra);
			} else {
				c = *(ColorType*)src;
			}

			colorBlock[cy][cx] = c;
			if (c.a < ALPHA_CUTOFF) {
				bHasAlpha=true;
				continue;
			}
			bHasNonAlpha = true;
		}
		if (cy < blockh-1)
			src += src_stride;
		else
			src -= (blockw-1)*4;
	}
	src_stride+=(blockw-1)*4;

	if (!bHasNonAlpha) {
		// All fields were unimportant (alpha < ALPHA_CUTOFF)
		color0.integer = 0;
		color1.integer = 0xffff;
		EmitU16(dest, 0);
		EmitU16(dest, 0xffff);
		EmitU32(dest, 0xffffffff); // 4 sets of Color 3 - invisible
		assert(dest == _dest + (64/8));
		PERFINFO_AUTO_STOP();
		return dest;
	}

	// Euclidean is slow, but bounding-box looks really bad for red on green, etc
	//GetMinMaxColorsBB((U8*)colorBlock, (U8*)&min, (U8*)&max);
	GetMinMaxColorsEuclidean((ColorType*)colorBlock, &min, &max);

	src = _src;
	color0.r = max.r >> 3;
	color0.g = max.g >> 2;
	color0.b = max.b >> 3;
	color1.r = min.r >> 3;
	color1.g = min.g >> 2;
	color1.b = min.b >> 3;
	if (bHasAlpha)
	{
		// encode numerically smaller endpoint first (c0 <= c1 implies alpha)
		if (color1.integer < color0.integer)
		{
			ColorType tc;
			Color565 t = color0;
			color0 = color1;
			color1 = t;
			tc = min;
			min = max;
			max = tc;
		}
		EmitU16(dest, color0.integer);
		EmitU16(dest, color1.integer);
		idx = EmitColorIndicesAlpha((U8*)colorBlock, (U8*)&min, (U8*)&max);
		EmitU32(dest, idx);
	}
	else // opaque
	{
		// encode numerically larger endpoint first (c0 > c1 implies no alpha)
		if (color0.integer < color1.integer)
		{
			Color565 t = color0;
			ColorType tc;
			color0 = color1;
			color1 = t;
			tc = min;
			min = max;
			max = tc;
		}
		EmitU16(dest, color0.integer);
		EmitU16(dest, color1.integer);
		idx = EmitColorIndices((U8*)colorBlock, (U8*)&min, (U8*)&max);
		EmitU32(dest, idx);
	}
	assert(dest == _dest + (64/8));
	PERFINFO_AUTO_STOP();
	return dest;
}

static U8 * dxtcompress_block_dxt5_intel(const U8 *_src, U8 *_dest, int src_stride, int blockw, int blockh, RdrTexFormat src_format)
{
	int cx, cy;
	const U8 *src=_src;
	U8 *dest=_dest;
	Color565 color0, color1;
	ColorType min;
	ColorType max;
	ColorBlock colorBlock;
	U32 color_idx=0, alpha_idx=0;
	PERFINFO_AUTO_START_FUNC();

	// Extract the colors
	src_stride-=(blockw-1)*4;
	for (cy=0; cy<4; cy++) {
		for (cx=0; cx<4; cx++) {
			ColorType c;
			if (src_format == RTEX_RGBA_U8) {
				swizzleRGBAToBGRA(src,c.bgra);
			} else {
				c = *(ColorType*)src;
			}
			if (cx < blockw-1)
				src+=4;

			colorBlock[cy][cx] = c;
		}
		if (cy < blockh-1)
			src += src_stride;
		else
			src -= (blockw-1)*4;
	}
	src_stride+=(blockw-1)*4;

	GetMinMaxColorsEuclideanAlpha((ColorType*)colorBlock, &min, &max);

	src = _src;
	color0.r = max.r >> 3;
	color0.g = max.g >> 2;
	color0.b = max.b >> 3;
	color1.r = min.r >> 3;
	color1.g = min.g >> 2;
	color1.b = min.b >> 3;

	EmitU8(dest, max.a);
	EmitU8(dest, min.a);
	dest = EmitAlphaIndices(dest, (U8*)colorBlock, min.a, max.a);

	// encode numerically larger endpoint first (c0 > c1 implies no alpha)
	// Does this matter for DXT5?
	if (color0.integer < color1.integer)
	{
		Color565 t = color0;
		ColorType tc;
		color0 = color1;
		color1 = t;
		tc = min;
		min = max;
		max = tc;
	}
	EmitU16(dest, color0.integer);
	EmitU16(dest, color1.integer);
	color_idx = EmitColorIndices((U8*)colorBlock, (U8*)&min, (U8*)&max);
	EmitU32(dest, color_idx);

	assert(dest == _dest + (128/8));
	PERFINFO_AUTO_STOP();
	return dest;
}

void dxtCompressBitfield(const U32 *data, int width, int height, Color zero_color,
						 U8 *data_out, int data_out_size)
{
	int i, j;
	int jj;
	U8 *dest;
	U32 idx;
	Color565 color1 = {0};
	U32 colorblock;
	// From 4-bit pattern to 8-bit encoded indices
	static U8 lookup[] = {
		(1<<6) | (1<<4) | (1<<2) | (1<<0), // 11
		(1<<6) | (1<<4) | (1<<2) | (3<<0), // 10
		(1<<6) | (1<<4) | (3<<2) | (1<<0), // 01
		(1<<6) | (1<<4) | (3<<2) | (3<<0), // 00
		(1<<6) | (3<<4) | (1<<2) | (1<<0), // 11
		(1<<6) | (3<<4) | (1<<2) | (3<<0), // 10
		(1<<6) | (3<<4) | (3<<2) | (1<<0), // 01
		(1<<6) | (3<<4) | (3<<2) | (3<<0), // 00
		(3<<6) | (1<<4) | (1<<2) | (1<<0), // 11
		(3<<6) | (1<<4) | (1<<2) | (3<<0), // 10
		(3<<6) | (1<<4) | (3<<2) | (1<<0), // 01
		(3<<6) | (1<<4) | (3<<2) | (3<<0), // 00
		(3<<6) | (3<<4) | (1<<2) | (1<<0), // 11
		(3<<6) | (3<<4) | (1<<2) | (3<<0), // 10
		(3<<6) | (3<<4) | (3<<2) | (1<<0), // 01
		(3<<6) | (3<<4) | (3<<2) | (3<<0), // 00
	};
// Other (slower) ways of calculating the above list
#define SHIFTMASK(x) (~(((x&1)<<1) | ((x&2)<<2) | ((x&4)<<3) | ((x&8)<<4)) & 0xff)
#define SQUARES(x) (~(((x&(1|4)) * (x&(1|4)) & (1|16) | (x&(2|8)) * (x&(2|8)) & (4|64))<<1) & 0xff)

	assert(width % 4 == 0);
	assert(height % 4 == 0);
	assert(data_out_size == width*height/2);

	color1.r = zero_color.r >> 3;
	color1.g = zero_color.g >> 2;
	color1.b = zero_color.b >> 3;

	// Make color block
	dest = (U8*)&colorblock;
	EmitU16(dest, 0);
	EmitU16(dest, color1.integer);

	// Output indices
	dest = data_out;
	for (j=0; j<height; j+=4)
	{
		for (i=0; i<width; i+=4)
		{
			*(U32*)dest = colorblock; dest+=4;

			idx = 0;
			for (jj=0; jj<4; jj++)
			{
				int bitindex2 = (j+jj)*width + i;
				U8 fourbit = 0xf & (data[bitindex2>>5] >> (bitindex2&31));
				idx |= lookup[fourbit] << (jj*8);
			}
			EmitU32(dest, idx);
		}
	}
	assert(dest == data_out + data_out_size);
}

static U8* dxtcompressDXT1(const U8 *_src, U8* dest, int w, int h, RdrTexFormat src_format)
{
	int i, j;
	int src_stride = 4*w;
	const U8 * src;
	for (j=0; j<h; j+=4) {
		int h2 = MIN(h - j, 4);
		src = _src + j*src_stride;
		for (i=0; i<w; i+=4) {
			int w2 = MIN(w - i, 4);
			dxtcompress_block_dxt1_intel(src, dest, src_stride, w2, h2, src_format);
			src += 4*4;
			dest+= 8;
		}
	}
	return dest;
}

static U8* dxtcompressDXT5(const U8 *_src, U8* dest, int w, int h, RdrTexFormat src_format)
{
	int i, j;
	int src_stride = 4*w;
	const U8 * src;
	for (j=0; j<h; j+=4) {
		int h2 = MIN(h - j, 4);
		src = _src + j*src_stride;
		for (i=0; i<w; i+=4) {
			int w2 = MIN(w - i, 4);
			dxtcompress_block_dxt5_intel(src, dest, src_stride, w2, h2, src_format);
			src += 4*4;
			dest+= 16;
		}
	}
	return dest;
}


U8 *dxtCompress(const U8 *data, U8 *dataOut, int width, int height, RdrTexFormat src_format, RdrTexFormat dst_format)
{
	if ((src_format == RTEX_BGRA_U8 || src_format == RTEX_RGBA_U8) && dst_format == RTEX_DXT1) {
		return dxtcompressDXT1(data, dataOut, width, height, src_format);
	} else if ((src_format == RTEX_BGRA_U8 || src_format == RTEX_RGBA_U8) && dst_format == RTEX_DXT5) {
		return dxtcompressDXT5(data, dataOut, width, height, src_format);
	} else {
		assert(!"Unsupported DXT compression format");
	}
	return NULL;
}
