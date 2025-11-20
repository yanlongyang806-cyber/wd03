#include "WTex.h"
#include "DirectDrawTypes.h"
#include "file.h"
#include "fileutil.h"
#include "ImageUtil.h"
#include "CrypticDXT.h"
#include "error.h"
#include "WritePNG.h"
#include "TaskProfile.h"
#include "MemRef.h"
#include "crunch.h"

#include "AutoGen/ImageTypes_h_ast.c"
#include "AutoGen/ImageTypes_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Textures_Misc););

int sizeof_DDSURFACEDESC2 = sizeof(DDSURFACEDESC2);

#define EXPECTED_TEXTURE_VERSION 5


static void writeMipMapHeader(FILE *outfile, const char *ddsfile, TextureFileHeader *tfh)
{
	DDSURFACEDESC2 *ddsd;
	TextureFileMipHeader mh;
	int mip_count;
	int i;
	int decrements = 0;
	int depth = 1;

	assert(strncmp(ddsfile, "DDS", 3)==0);

	ddsd = (DDSURFACEDESC2*)(ddsfile + 4);

	if (tfh->flags & TEXOPT_CUBEMAP) {
		depth = 6;
	} else if (tfh->flags & TEXOPT_VOLUMEMAP) {
		depth = ddsd->dwWidth;
		assert(ddsd->dwWidth == ddsd->dwHeight);
	}

	assert(ddsd->dwFlags & DDSD_MIPMAPCOUNT);
	assert((DWORD)imgLevelCount(ddsd->dwWidth, ddsd->dwHeight, 1)==ddsd->dwMipMapCount);

	STATIC_INFUNC_ASSERT(sizeof(mh)%4==0);
	mh.structsize	= sizeof(mh);
	mh.width		= ddsd->dwWidth;
	mh.height		= ddsd->dwHeight;
	mip_count		= ddsd->dwMipMapCount;


	{
		// Make sure the top level of the cached mip is at least 4x4,
		// Decrement to 8x8
		while (mh.width > 4 && mh.height > 4 && (mh.width > 8 || mh.height > 8)) {
			mh.width>>=1;
			mh.height>>=1;
			decrements++;
		}
		// Cubemaps, use 4x4, as 8x8x6x4 is too many bytes
		if (tfh->flags & TEXOPT_CUBEMAP && mh.width == 8)
		{
			mh.width>>=1;
			mh.height>>=1;
			decrements++;
		}

		// but we should never store more than six mips
		if( mip_count - decrements > 6 ) {
			mh.width >>= (mip_count - decrements - 6);
			mh.height >>= (mip_count - decrements - 6);
			decrements = mip_count - 6;
		}

		// and if it's an uncompressed cube, store one less
		if (tfh->flags & TEXOPT_CUBEMAP && (tfh->rdr_format == RTEX_BGR_U8) && decrements)
			decrements++;
	}

	{
		int blockSize = (tfh->rdr_format == RTEX_DXT1) ? 8 : 16;
		int num_to_skip = decrements;
		int total_bytes = (int)imgByteCount(RTEX_2D, tfh->rdr_format, ddsd->dwWidth, ddsd->dwHeight, 1, 0);
		int bytes_to_skip = num_to_skip?(int)imgByteCount(RTEX_2D, tfh->rdr_format, ddsd->dwWidth, ddsd->dwHeight, 1, num_to_skip):0;
		int bytes_to_copy = total_bytes - bytes_to_skip;

		assertmsg(bytes_to_copy < 1024, "Something went wrong!  The cached mipmap header is way too big!");

		// Write MipHeader to file, followed by the bytes of the low mips
		fwrite(&mh, 1, sizeof(mh), outfile);
		tfh->header_size += sizeof(mh);
		for (i=0; i<depth; i++) {
			fwrite(ddsfile + 4 + sizeof(DDSURFACEDESC2) + bytes_to_skip + (total_bytes * i), 1, bytes_to_copy, outfile);
			tfh->header_size += bytes_to_copy;
		}
		if (tfh->header_size%4!=0) {
			int pad=0xAEAEAEAE;
			int pad_size = 4 - (tfh->header_size%4);
			// Pad to word boundary
			fwrite(&pad, 1, pad_size, outfile);
			tfh->header_size+= pad_size;
		}
	}
}

bool texWriteTimestamp(
	const char *filename,
	const char *src_tga_path,
	const TexOpt *src_texopt)
{
	FILE *outf;
	char timestampfile[MAX_PATH];

	changeFileExt(filename, ".timestamp", timestampfile);
	outf = fopen(timestampfile, "wb");
	if (!outf) {
		Errorf("Error opening %s for writing!", timestampfile);
		return false;
	}
	fprintf(outf, "Version1 %d %d",
		src_tga_path?fileLastChanged(src_tga_path):-1,
		src_texopt?fileLastChanged(src_texopt->file_name):-1);
	fclose(outf);
	return true;
}

DDSURFACEDESC2 * texGetDDSHeaderFromRawData(char *data, size_t data_size)
{
	if (data_size < sizeof(DDSURFACEDESC2) + 4)
		return NULL;
	return (DDSURFACEDESC2*)((char*)data + 4);
}

static void ddsSegmentHeader(DDSURFACEDESC2 *baseHeader, DDSURFACEDESC2 *highHeader, const DDSURFACEDESC2 *srcHeader, U32 baseLevels)
{
	U32 highLevels = srcHeader->dwMipMapCount - baseLevels;

	memcpy(baseHeader, srcHeader, sizeof(DDSURFACEDESC2));

	baseHeader->dwWidth >>= highLevels;
	baseHeader->dwHeight >>= highLevels;
	baseHeader->dwLinearSize >>= highLevels;
	baseHeader->dwMipMapCount -= highLevels;

	memcpy(highHeader, srcHeader, sizeof(DDSURFACEDESC2));

	highHeader->dwMipMapCount = highLevels;
}

// returns the number of texture levels that should be placed in the base .wtex based on the texopts
static int getBaseLevelCount(int max_levels, const TexOpt *src_texopt)
{
	int base_levels = max_levels;
	int min_top_levels;

	// Don't split anything 32x32 or smaller.
	if (max_levels <= 6) {
		return max_levels;
	}

	if (src_texopt && src_texopt->high_level_size) {
		base_levels = log2(src_texopt->high_level_size);
		MIN1(base_levels, max_levels);
	}

	min_top_levels = src_texopt ? src_texopt->min_level_split : 0;
	if (max_levels - base_levels < min_top_levels) {
		base_levels = max_levels - min_top_levels;
	}

	return base_levels;
}

bool texWriteData(const char *filename, const char *data, size_t data_size, const char *opt_header_data,
	int width, int height, bool has_alpha, TexOptFlags texopt_flags, const char *src_tga_path, const TexOpt *src_texopt)
{
	FILE *out_wtex, *out_htex = NULL;
	TextureFileHeader tfh = {0};
	RdrTexFormat rdr_format;
	RdrTexType rdr_type = RTEX_2D;
	const DDSURFACEDESC2 *ddsd = (const DDSURFACEDESC2*)(data + 4);
	int comp_flags = (texopt_flags & TEXOPT_COMPRESSION_MASK) >> TEXOPT_COMPRESSION_SHIFT;
	int base_levels, top_levels, max_levels;
	char htex_filename[MAX_PATH];
	
	changeFileExt(filename, ".htex", htex_filename);

	if (texopt_flags & TEXOPT_JPEG) {
		rdr_format = RTEX_BGR_U8;
	} else if (texopt_flags & TEXOPT_CRUNCH) {
		U32 crnWidth = width, crnHeight = height;
		 
		rdr_format = texFormatFromCrn(data);

		// make sure the wtex header matches the actual data
		crnGetTextureDims(data, &crnWidth, &crnHeight, NULL);
		width = (int)crnWidth;
		height = (int)crnHeight;
	} else {
		assert(memcmp(data, "DDS ", 4)==0);

		rdr_format = texFormatFromDDSD(ddsd);
		assert(rdr_format!=(RdrTexFormat)-1 && rdr_format != RTEX_INVALID_FORMAT);

		if (texopt_flags & TEXOPT_CUBEMAP) {
			rdr_type = RTEX_CUBEMAP;
			if ((ddsd->dwFlags & DDSD_MIPMAPCOUNT) == 0)
			{
				printfColor(COLOR_RED|COLOR_BRIGHT, "DDS Cubemap does not have mipmaps, this is not supported.\n");
				return false;
			}
		} else if (texopt_flags & TEXOPT_VOLUMEMAP) {
			rdr_type = RTEX_3D;
		}

		if (comp_flags == COMPRESSION_HALFRES_TRUECOLOR) {
			width = ddsd->dwWidth;
			height = ddsd->dwHeight;
		}
	}

	// figure out how many levels to place in the htex
	if (texopt_flags & TEXOPT_NOMIP) {
		max_levels = base_levels = 1;
	} else {
        max_levels = imgLevelCount(width, height, 1);
        base_levels = getBaseLevelCount(max_levels, src_texopt);
	}
	top_levels = max_levels - base_levels;

	tfh.header_size = sizeof(tfh);
	tfh.alpha = has_alpha;
	tfh.unused3 = 0.0f;
	tfh.unused = 0.0f;
	tfh.max_levels = max_levels;
	tfh.base_levels = base_levels;
	tfh.flags = texopt_flags;
	tfh.width = width;
	tfh.height = height;
	tfh.rdr_format = rdr_format;
	tfh.verpad[0] = 'T';
	tfh.verpad[1] = 'X';
	tfh.verpad[2] = '0' + EXPECTED_TEXTURE_VERSION;  // TX5

	assert(tfh.header_size < 1024); // That's what can fit in the file_pos bitfield

	assert(fileIsAbsolutePath(filename));
	out_wtex = fopen(filename, "wb"); // JE: *not* fileOpen because it does a fileLocateWrite which may redirect us to/from Core!
	if (!out_wtex) {
		Errorf("Error opening %s for writing!", filename);
		return false;
	}

	if (top_levels) { 
		out_htex = fopen(htex_filename, "wb");
		if (!out_htex) {
			Errorf("Error opening %s for writing!", htex_filename);
			fclose(out_wtex);
			return false;
		}
	} else if (fileExists(htex_filename)) {
		fileForceRemove(htex_filename);
	}

	//Write texture header to the .wtex file
	{
		int j;
		U32 save_a_slot = 0xBADF00D;

		fwrite(&tfh.header_size, 1, 4, out_wtex);
		STATIC_INFUNC_ASSERT(sizeof(tfh) % 4 ==0); // should be word aligned!
		for (j=0; j<sizeof(tfh)/4 - 1; j++) {
			fwrite(&save_a_slot, 1, 4, out_wtex);
		}
		assert(ftell(out_wtex)==tfh.header_size);
	}

	// Write pre-cached mip-map levels
	if ((texopt_flags & (TEXOPT_JPEG | TEXOPT_NOMIP)) == 0)
	{
		writeMipMapHeader(out_wtex, opt_header_data ? opt_header_data : data, &tfh);
	}

	if (texopt_flags & (TEXOPT_JPEG | TEXOPT_NOMIP)) {
		// jpeg or no mips, just dump all the data
		assert(base_levels == max_levels);
		fwrite(data,1,data_size,out_wtex);
	} else if (texopt_flags & TEXOPT_CRUNCH) {
		if (!top_levels) {
			fwrite(data,1,data_size,out_wtex);
		} else {
            void *baseCrn, *highCrn;
			size_t baseSize, highSize;

            crnSegmentFile(&baseCrn, &baseSize, &highCrn, &highSize, data, NULL, base_levels);

            fwrite(baseCrn, 1, baseSize, out_wtex);
            fwrite(highCrn, 1, highSize, out_htex);

			free(baseCrn);
			free(highCrn);
		}
	} else {
		int numLevels = tfh.max_levels;
		int numFaces = (rdr_type == RTEX_CUBEMAP) ? 6 : 1;
		int curLevel, curFace;
		int levelWidth = width;
		int levelHeight = height;
		int levelDepth = (rdr_type == RTEX_3D) ? width : 1;
		size_t levelOffsets[16] = { 0 };
		size_t baseOffset = sizeof(U32) + sizeof(DDSURFACEDESC2);
		DDSURFACEDESC2 baseDesc, highDesc;
		size_t faceSize;

		assert(ddsd->dwFlags & DDSD_MIPMAPCOUNT); // if we don't have mips, TEXOPT_NOMIP should be set...

		// write DDS header(s)
		ddsSegmentHeader(&baseDesc, &highDesc, (DDSURFACEDESC2 *)(data + 4), base_levels);

		fwrite(data, 4, 1, out_wtex);
		fwrite(&baseDesc, sizeof(DDSURFACEDESC2), 1, out_wtex);
		if (out_htex) {
			fwrite(data, 4, 1, out_htex);
			fwrite(&highDesc, sizeof(DDSURFACEDESC2), 1, out_htex);
		}

		// find the byte offset of each mip
		for (curLevel = 0; curLevel < numLevels; ++curLevel) {
			levelOffsets[curLevel + 1] = levelOffsets[curLevel] + imgByteCount(rdr_type, rdr_format, levelWidth, levelHeight, levelDepth, 1);

			levelWidth = MAX(levelWidth >> 1, 1);
			levelHeight = MAX(levelHeight >> 1, 1);
			levelDepth = MAX(levelDepth >> 1, 1);
		}

		// now write the texture levels to the appropriate file
		faceSize = levelOffsets[numLevels];

		if (texopt_flags & TEXOPT_REVERSED_MIPS) {
            // write texture levels in reverse order
            for (curLevel = numLevels - 1; curLevel >= 0; --curLevel) {
                size_t levelSize = levelOffsets[curLevel + 1] - levelOffsets[curLevel];
                for (curFace = 0; curFace < numFaces; ++curFace) {
                    fwrite(data + baseOffset + faceSize * curFace + levelOffsets[curLevel], 1, levelSize, curLevel < top_levels ? out_htex : out_wtex);
                }
			}
		} else {
            for (curFace = 0; curFace < numFaces; ++curFace) {
                for (curLevel = 0; curLevel < numLevels; ++curLevel) {
                    size_t levelSize = levelOffsets[curLevel + 1] - levelOffsets[curLevel];
					fwrite(data + baseOffset + faceSize * curFace + levelOffsets[curLevel], 1, levelSize, curLevel < top_levels ? out_htex : out_wtex);
				}
			}
		}
	}

	tfh.file_size = ftell(out_wtex) - tfh.header_size;	//go back and write length of the data to the header
	// also write the modified length of header_size if texAppend wrote anything else
	fseek(out_wtex, 0, SEEK_SET);	//go to header
	fwrite(&tfh,1,sizeof(tfh),out_wtex);

	fclose(out_wtex);
	if (out_htex) {
		fclose(out_htex);
	}

	// write timestamp file
	return texWriteTimestamp(filename, src_tga_path, src_texopt);
}

// Send in an uncompressed buffer and have this function compress and write to a wtex file.
// destFormat supports: RTEX_DXT1, RTEX_DXT5, RTEX_BGRA_U8
bool texWriteWtex(const char* filedata, int width, int height, float rel_scale, const char* filename, 
	RdrTexFormat destFormat, TexOptFlags tex_flags, TexOptQuality quality, TaskProfile *compressImageProfile)
{
	bool retVal = false;
	CrnTargetFileType fileType;
	CrnTargetFormat format;
	CrnQualityLevel crnQuality;
	void *compData = NULL;
	size_t compSize;

	if (compressImageProfile)
		taskStartTimer(compressImageProfile);

	switch (quality) {
	case QUALITY_LOWEST:
		crnQuality = CRN_QUALITY_LOWEST;
		fileType = CRN_FILE_TYPE_CRN;
	xcase QUALITY_MEDIUM:
		crnQuality = CRN_QUALITY_NORMAL;
		fileType = CRN_FILE_TYPE_CRN;
	xdefault:
		fileType = CRN_FILE_TYPE_DDS;
		crnQuality = CRN_QUALITY_HIGHEST;
	}

	switch(destFormat) {
		case RTEX_DXT1:
			format = CRN_FORMAT_DXT1;	
		xcase RTEX_DXT5:
			format = CRN_FORMAT_DXT5;
		xcase RTEX_BGRA_U8:
			// ignore the quality setting for ARGB output
			format = CRN_FORMAT_ARGB;
			fileType = CRN_FILE_TYPE_DDS;
		xdefault:
			assertmsg(0,"Destination format currently unsupported in this function");
	}

	if (fileType == CRN_FILE_TYPE_CRN) {
		tex_flags |= TEXOPT_CRUNCH | TEXOPT_REVERSED_MIPS;
	}

	compSize = crnCompressArgb(&compData, filedata, width, height, 1, rel_scale, (tex_flags & TEXOPT_NOMIP) == 0,
		fileType, format, crnQuality);

	width = (int)(width * rel_scale + 0.5f);
	height = (int)(height * rel_scale + 0.5f);

	if (compSize) {
		void *mipHeader = NULL;

		if (fileType == CRN_FILE_TYPE_CRN && (tex_flags & TEXOPT_NOMIP) == 0) {
			// decompress the low mips to a temp buffer for the cached mip header
			mipHeader = malloc(crnDdsSizeForLevels(compData, 6, 1));
			crnDecompressToDds(mipHeader, compData, NULL, 6);
		}

		retVal = texWriteData(filename, compData, compSize, mipHeader, width, height, true, tex_flags, NULL, NULL);

		free(mipHeader);
		crnFree(compData);
	}

	if (compressImageProfile)
	{
		taskStopTimer(compressImageProfile);
		taskAttributeWriteIO(compressImageProfile, compSize);
	}

	return retVal;
}

