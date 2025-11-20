#pragma once

#include "ImageTypes.h"

typedef struct TaskProfile TaskProfile;

// The structure defining the header of a .texture file
typedef struct TextureFileHeader {
	U32 header_size; // Number of bytes in header data of file (not sizeof(struct))
	U32 file_size; // Number of bytes in data chunk of file (everything after the header + mip = .tga file or .dds file)
	U32 width, height;
	TexOptFlags flags;
	F32 unused3; // Non-zero in TX4 and TX5 (still used)
	U8 max_levels;	// total number of levels in the texture
	U8 base_levels;	// levels present in the wtex (rest are in htex)
	U16 unused;		// always zero
	U32 rdr_format; // RdrTexFormat
	U8 alpha; // boolean flag
	U8 verpad[3]; // padding and version info.  =TX3 for v3 (post-CoX) textures, TX4 was processed with NVDXT 8, TX5 has normalized normals
} TextureFileHeader;
// Next comes the mipmap data (header+data) to preload (ignored/not there in v1 .textures)

typedef struct TextureFileMipHeader {
	U32 structsize; // sizeof(TextureFileMipHeader)
	U32 width, height; // of first preload mip level
} TextureFileMipHeader;

#define TEXMIP_NONE 0
#define TEXMIP_DXT1 1
#define TEXMIP_DXT5 2

#define TEXFMT_RAW_DDS -1 // For loading raw data to be uncompressed in software
#define TEXFMT_ARGB_8888 -2 // For loading raw data to be uncompressed in software
#define TEXFMT_ARGB_0888 -3 // For loading raw data to be uncompressed in software

typedef struct TexOpt {
	const char *file_name; // Parser file name of .texopt file
	const char *texture_name; // Texture name auto-assembled from file_name of .texopt file
	TexOptFlags flags;
	//F32 texopt_fade[2];
	F32 alpha_mip_threshold;
	TexOptMipFilterType mip_filter;
	TexOptMipSharpening mip_sharpening;
	TexOptQuality quality;
	TexOptCompressionType compression_type;
	Color border_color;
	__time32_t fileAge;
	int is_folder;
	S16 high_level_size;
	U8 min_level_split;
} TexOpt;

bool texWriteTimestamp(const char *filename, const char *src_tga_path, const TexOpt *src_texopt);
bool texWriteData(const char *filename, const char *data, size_t data_size, const char *opt_header_data,
	int width, int height, bool has_alpha, TexOptFlags texopt_flags, const char *src_tga_path, const TexOpt *src_texopt);
bool texWriteWtex(const char* filedata, int width, int height, float rel_scale, const char* filename, 
	RdrTexFormat destFormat, TexOptFlags tex_flags, TexOptQuality quality, TaskProfile *compressImageProfile);

