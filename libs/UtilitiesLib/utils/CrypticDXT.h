#pragma once
GCC_SYSTEM

#include "ImageTypes.h"

typedef struct DDSURFACEDESC2 DDSURFACEDESC2;

RdrTexFormat texFormatFromDDSD(const DDSURFACEDESC2 *ddsd);

U8 *dxtCompress(const U8 *data, U8 *dataOut, int width, int height, RdrTexFormat src_format, RdrTexFormat dst_format);
U8* dxtDecompressMemRef(int *w, int *h, int *depth, int *size, const U8* data);
void dxtDecompressDirect(const U8 *src_data, U8 *dst_data, int w, int h, RdrTexFormat tex_format);
void dxtuncompress_block(RdrTexFormat tex_format, const U8 *src, U8 *dest, int dest_stride);

// Compresses a bitfield into DXT1, with 1 = transparent and 0 = specified color (truncated to 16bits)
void dxtCompressBitfield(const U32 *data, int width, int height, Color zero_color,
						 U8 *data_out, int data_out_size);

// Structures for casting in the debugger:
typedef struct DXT5Data
{
	U64 a0:8;
	U64 a1:8;
	U64 a00:3;
	U64 a01:3;
	U64 a02:3;
	U64 a03:3;
	U64 a10:3;
	U64 a11:3;
	U64 a12:3;
	U64 a13:3;
	U64 a20:3;
	U64 a21:3;
	U64 a22:3;
	U64 a23:3;
	U64 a30:3;
	U64 a31:3;
	U64 a32:3;
	U64 a33:3;

	U64 c0_r:5;
	U64 c0_g:6;
	U64 c0_b:5;
	U64 c1_r:5;
	U64 c1_g:6;
	U64 c1_b:5;
	U64 c00:2;
	U64 c01:2;
	U64 c02:2;
	U64 c03:2;
	U64 c10:2;
	U64 c11:2;
	U64 c12:2;
	U64 c13:2;
	U64 c20:2;
	U64 c21:2;
	U64 c22:2;
	U64 c23:2;
	U64 c30:2;
	U64 c31:2;
	U64 c32:2;
	U64 c33:2;
} DXT5Data;
