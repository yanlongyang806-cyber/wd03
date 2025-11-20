#pragma once

#include "RdrTextureEnums.h"
#include "GfxTextureEnums.h"

// src_format must be RGBA or RGB
// dst_format must be the same as src_format or be RTEX_DXT1 with src=RGBA (what GfxDXT.c supports)
U8 *buildMipMaps(TexOptFlags flags_for_border, const U8 *data, int width, int height, RdrTexFormat src_format, RdrTexFormat dst_format, int *data_size_out);

void alphaBorderBuffer(TexOptFlags flags_for_border, U8 *data, int width, int height, RdrTexFormat src_format);
