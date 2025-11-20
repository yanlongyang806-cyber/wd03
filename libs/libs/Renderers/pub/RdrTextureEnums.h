#pragma once

#include "ImageTypes.h"

typedef enum RdrTexFlags
{
	// texture address modes
	RTF_CLAMP_U			= 1<<0,
	RTF_MIRROR_U		= 1<<1,
	RTF_CLAMP_V			= 1<<2,
	RTF_MIRROR_V		= 1<<3,
	RTF_CLAMP_W			= 1<<4,
	RTF_MIRROR_W		= 1<<5,
	

	// texture filtering, default is linear or anisotropic based on the texture's anisotropy setting
	RTF_MAG_POINT		= 1<<8,
	RTF_MIN_POINT		= 1<<9,

	RTF_MAXMIPLEVEL_1	= 1<<10, // 3-bit number
	RTF_MAXMIPLEVEL_2	= 1<<11,
	RTF_MAXMIPLEVEL_4	= 1<<12,

	RTF_COMPARISON_LESS_EQUAL = 1<<13,

} RdrTexFlags;

#define RdrTexHasMaxMipLevelFromFlags(flags) (((flags) & (RTF_MAXMIPLEVEL_1|RTF_MAXMIPLEVEL_2|RTF_MAXMIPLEVEL_4)))
#define RdrTexMaxMipLevelFromFlags(flags) (((flags) & (RTF_MAXMIPLEVEL_1|RTF_MAXMIPLEVEL_2|RTF_MAXMIPLEVEL_4)) >> 10)

#define RDR_NULL_TEXTURE ((TexHandle)0L)
#define RDR_UNKNOWN_TEXTURE ((TexHandle)1L)
#define RDR_FIRST_SURFACE_FIXUP_TEXTURE ((TexHandle)2L)
#define RDR_LAST_SURFACE_FIXUP_TEXTURE ((TexHandle)31L)
// this define must account for the length of the range [0,RDR_LAST_SURFACE_FIXUP_TEXTURE]
#define MAX_SURFACE_FIXUP_HANDLES 32
#define RDR_FIRST_TEXTURE_GEN ((TexHandle)32L)

