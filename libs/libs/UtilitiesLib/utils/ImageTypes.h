#pragma once

typedef enum RdrTexFormat
{
	// Note: The order/value of these enums cannot be changed (used in
	//  on-disk texture format)
	//								// DX9 types
	RTEX_BGR_U8,					// D3DFMT_X8R8G8B8
	RTEX_BGRA_U8,					// D3DFMT_A8R8G8B8
	RTEX_RGBA_F16, // dst only		// D3DFMT_A16B16G16R16F
	RTEX_RGBA_F32, // src only		// D3DFMT_A32B32G32R32F
	RTEX_unused_was_LA8,			// not used on disk, we could add a new one to this slot safely
	RTEX_DXT1,						// D3DFMT_DXT1:
	RTEX_DXT3,						// D3DFMT_DXT3:
	RTEX_DXT5,						// D3DFMT_DXT5:
	RTEX_unused_was_depth,			// not used on disk, we could add a new one to this slot safely
	RTEX_R_F32,						// D3DFMT_R32F:
	RTEX_BGRA_5551,					// D3DFMT_A1R5G5B5:
	RTEX_A_U8,						// D3DFMT_A8:
	RTEX_RGBA_U8,					// D3DFMT_A8B8G8R8

	// Not on-disk formats, can be changed
	RTEX_INVALID_FORMAT,
	RTEX_MAX,
	RTEX_FIRST_INTERNAL_FORMAT=RTEX_MAX,
} RdrTexFormat;
#define RdrTexFormat_NumBits 4 // Note: enum bitfield must be either a U32 or NumBits+1 because of signed enum issues
STATIC_ASSERT(RTEX_MAX <= (1<<4));

// Process-time, send-to-card-time flags, artist specified
typedef enum TexOptFlags
{
	TEXOPT_EXCLUDE		= 1 << 0,
	TEXOPT_SRGB			= 1 << 1,	// Texture is in sRGB space and should be converted to linear on read
	TEXOPT_MAGFILTER_POINT	= 1 << 2,
	TEXOPT_CUBEMAP		= 1 << 3,
	TEXOPT_FIX_ALPHA_MIPS = 1 << 4,
	TEXOPT_SPLIT		= 1 << 5, // Split the texture into lots of smaller textures
	TEXOPT_CLAMPS		= 1 << 6,
	TEXOPT_CLAMPT		= 1 << 7,
	TEXOPT_MIRRORS		= 1 << 9,
	TEXOPT_MIRRORT		= 1 << 10,
	TEXOPT_NORMALMAP	= 1 << 11, // Already saved as a normal map, but may need different mip generation
	TEXOPT_BUMPMAP		= 1 << 12, // Needs to be treated as a heightfield when processed
	TEXOPT_ALPHABORDER_LR	= 1 << 13, // Border left and right edges of the texture and mips with alpha = 0
	TEXOPT_ALPHABORDER_TB	= 1 << 14, // Border top and bottom ediges of the texture and mips with alpha = 0
	TEXOPT_ALPHABORDER	= (TEXOPT_ALPHABORDER_LR|TEXOPT_ALPHABORDER_TB), // Border texture and mips with alpha = 0
	TEXOPT_COLORBORDER	= 1 << 15, // Border texture and mips with custom color
	TEXOPT_NOMIP		= 1 << 16,
	TEXOPT_JPEG			= 1 << 17, // Texture *should be* processed as a jpeg when running GetTex
	TEXOPT_VOLUMEMAP	= 1 << 18, // 3D texture
	TEXOPT_RGBE			= 1 << 19, // HDR texture in RGBE format
	TEXOPT_FOR_FALLBACK	= 1 << 20, // Makes this texture not effected by global texture reduce
	TEXOPT_COLORBORDER_LEGACY	= 1 << 21, // Allows less perfect color bordering but in some cases allows non-565 color values
	TEXOPT_NO_ANISO		= 1 << 22, // Do not apply anisotropic filtering to this texture
	TEXOPT_COMPRESSION_MASK = (1 << 23) | (1<<24) | (1<<25) | (1<<26),
	TEXOPT_LIGHTMAP		= 1 << 27, // Combined lightmap
	TEXOPT_REVERSED_MIPS = 1<< 28,	// Texture levels are stored in reverse order (smallest to largest)
	TEXOPT_CRUNCH		= 1 << 29,	// DXT texture preprocessed with crunch lossy compression
} TexOptFlags;
#define TEXOPT_DO_NOT_SAVE_OR_READ_FLAGS (TEXOPT_COMPRESSION_MASK|TEXOPT_NORMALMAP)
#define TEXOPT_COMPRESSION_SHIFT 23

#define TEXOPT_FLAGS_TO_COPY_TO_TEXWORD (TEXOPT_MAGFILTER_POINT|TEXOPT_CLAMPS|TEXOPT_CLAMPT|TEXOPT_MIRRORS|TEXOPT_MIRRORT|TEXOPT_ALPHABORDER|TEXOPT_COLORBORDER|TEXOPT_FOR_FALLBACK|TEXOPT_NO_ANISO)

AUTO_ENUM;
typedef enum TexOptMipFilterType
{
	MIP_KAISER,	ENAMES(Kaiser)
	MIP_BOX,	ENAMES(Box)
	MIP_CUBIC,	ENAMES(Cubic)
	MIP_MITCHELL, ENAMES(Mitchell)
} TexOptMipFilterType;

AUTO_ENUM;
typedef enum TexOptMipSharpening
{
	SHARPEN_NONE,			ENAMES(None)
	SHARPEN_NEGATIVE,		ENAMES(Negative)
	SHARPEN_LIGHTER,		ENAMES(Lighter)
	SHARPEN_DARKER,			ENAMES(Darker)
	SHARPEN_CONTRASTMORE,	ENAMES(ContrastMore)
	SHARPEN_CONTRASTLESS,	ENAMES(ContrastLess)
	SHARPEN_SMOOTHEN,		ENAMES(Smoothen)
	SHARPEN_SHARPENSOFT,	ENAMES(SharpenSoft)
	SHARPEN_SHARPENMEDIUM,	ENAMES(SharpenMedium)
	SHARPEN_SHARPENSTRONG,	ENAMES(SharpenStrong)
	SHARPEN_FINDEDGES,		ENAMES(FindEdges)
	SHARPEN_CONTOUR,		ENAMES(Contour)
	SHARPEN_EDGEDETECT,		ENAMES(EdgeDetect)
	SHARPEN_EDGEDETECTSOFT,	ENAMES(EdgeDetectSoft)
	SHARPEN_EMBOSS,			ENAMES(Emboss)
	SHARPEN_MEANREMOVAL,	ENAMES(MeanRemoval)
} TexOptMipSharpening;

AUTO_ENUM;
typedef enum TexOptQuality
{
	QUALITY_PRODUCTION,	ENAMES(Production)
	QUALITY_LOWEST,		ENAMES(Lowest)
	QUALITY_MEDIUM,		ENAMES(Medium)
	QUALITY_HIGHEST,	ENAMES(Highest)
} TexOptQuality;

AUTO_ENUM;
typedef enum TexOptCompressionType
{
	COMPRESSION_AUTO,	ENAMES(Auto)
	COMPRESSION_DXT1,	ENAMES(DXT1)
	COMPRESSION_DXT5,	ENAMES(DXT5)
	COMPRESSION_HALFRES_TRUECOLOR,	ENAMES(HalfResTrueColor)
	COMPRESSION_1555,	ENAMES(Uncompressed16bpp)
	COMPRESSION_DXT_IF_LARGE, ENAMES(DXTifLarge)
	COMPRESSION_U8,		ENAMES(U8)
	COMPRESSION_DXT5NM,	ENAMES(DXT5nm)
	COMPRESSION_TRUECOLOR,	ENAMES(TrueColor)
} TexOptCompressionType;


// Load-time flags, not changed at runtime, comes from data format
typedef enum TexFlags
{
	TEX_ALPHA		= 1 << 0,
	TEX_CRUNCH		= 1 << 1, // Texture was crunched
	TEX_JPEG		= 1 << 2, // Texture *was* processed as a jpeg
	TEX_VOLATILE_TEXGEN = 1 << 3, // Used by TexGen system
	TEX_TEXGEN		= 1 << 4, // This texture is owned by the TexGen system
	TEX_DYNAMIC_TEXWORD = 1 << 5, // This texture is a unique instance of dynamic TexWord
	TEX_SCRATCH = 1 << 6,		  // This texture is a scratch
	TEX_REVERSED_MIPS = 1 << 7,		// Textures mipmaps are stored in reverse order
	// texture, like the ones the
	// headshots use.
	TexFlags_MAX
} TexFlags;
#define TexFlags_NumBits 8
STATIC_ASSERT(TexFlags_MAX == (1<<(TexFlags_NumBits-1))+1);

AUTO_ENUM AEN_NO_PREFIX_STRIPPING;
typedef enum RdrTexType
{
	RTEX_1D,
	RTEX_2D,
	RTEX_3D,
	RTEX_CUBEMAP,
	// If this gets anything added, RdrTextureDataDX::tex_type may need another bit
} RdrTexType;
extern StaticDefineInt RdrTexTypeEnum[];

// Union used for type-safety
typedef union RdrTexFormatObj
{
	RdrTexFormat format;
} RdrTexFormatObj;

__forceinline RdrTexFormatObj MakeRdrTexFormatObj(RdrTexFormat fmt)
{
	RdrTexFormatObj ret = {fmt};
	return ret;
}

