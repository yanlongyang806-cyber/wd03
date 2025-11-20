#include "crunch.h"

#include "../../3rdparty/crunch/inc/crnlib.h"
#include "../../3rdparty/crunch/inc/dds_defs.h"
#include "../../3rdparty/crunch/inc/crn_decomp.h"

C_DECLARATIONS_BEGIN
#include "ImageUtil.h"
C_DECLARATIONS_END

using namespace crnd;

#ifdef _WIN64
#	ifndef NDEBUG
#		pragma comment(lib, "crnlibd_x64_vc10.lib")
#	else
#		pragma comment(lib, "crnlib_x64_vc10.lib")
#	endif
#else
#	ifndef NDEBUG
#		pragma comment(lib, "crnlibd_vc10.lib")
#	else
#		pragma comment(lib, "crnlib_vc10.lib")
#	endif
#endif

// Decompression mempool
#define CRN_MEMPOOL_SIZE	(256 * 1024)
static __ALIGN(8) U8 crnMempool[CRN_MEMPOOL_SIZE];
static size_t crnBytesAlloced;

static void *crnRealloc(void* p, size_t size, size_t* pActual_size, bool /*movable*/, void* /*pUser_data*/)
{
	if (!p || size)
	{
		void *ret;

		size = (size + crnd::CRND_MIN_ALLOC_ALIGNMENT - 1) & ~(crnd::CRND_MIN_ALLOC_ALIGNMENT - 1);
		assert(crnBytesAlloced + size <= CRN_MEMPOOL_SIZE);

		ret = &crnMempool[crnBytesAlloced];
		crnBytesAlloced += size;

		*pActual_size = size;

		return ret;
	}

	return NULL;
}

static bool decompData(void *dataOut, const void *crnHeader, const void *crnData, RdrTexType texType, RdrTexFormat texFormat, U32 numLevels,
    bool fixupHeader = false)
{
	const crn_header *header = static_cast<const crn_header *>(crnHeader);
	char tmpHeader[sizeof(crn_header)];

	U32 texLevels = header->m_levels;
	if (numLevels == 0 || numLevels > header->m_levels) {
		numLevels = header->m_levels;
	}

    U32 firstLevel = texLevels - numLevels;

	if (fixupHeader) {
		memcpy(tmpHeader, header, sizeof(tmpHeader));
		crn_header *tmpCrnHeader = reinterpret_cast<crn_header *>(&tmpHeader[0]);
		
		for (U32 i = 0; i < texLevels; ++i) {
			tmpCrnHeader->m_level_sizes[i] = tmpCrnHeader->m_level_sizes[i] - sizeof(crn_header);
		}

		header = tmpCrnHeader;
	}

	crnBytesAlloced = 0;
	crnd_set_memory_callbacks(crnRealloc, NULL, NULL);

	crnd_unpack_context ctx = crnd_unpack_begin(header, crnData, UINT_MAX);
	if (!ctx) {
		return false;
	}

    size_t faceSize;
	{
		U32 width = MAX(1, header->m_width >> firstLevel);
		U32 height = MAX(1, header->m_height >> firstLevel);
		width = ALIGNUP(width, 4);
		height = ALIGNUP(height, 4);

		faceSize = imgByteCount(RTEX_2D, texFormat, width, height, 1, numLevels);
	}

	size_t mipOfs = 0;
	for (U32 i = 0; i < numLevels; ++i) {
		U32 levelNum = firstLevel + i;

		void *outPtrs[6];
		for (unsigned int j = 0; j < header->m_faces; ++j) {
			outPtrs[j] = static_cast<U8 *>(dataOut) + j * faceSize + mipOfs;
		}

		U32 width = MAX(1, header->m_width >> levelNum);
		U32 height = MAX(1, header->m_height >> levelNum);
		width = ALIGNUP(width, 4);
		height = ALIGNUP(height, 4);
        mipOfs += imgByteCount(RTEX_2D, texFormat, width, height, 1, 1);

		if (!crnd_unpack_level(ctx, outPtrs, UINT_MAX, 0, levelNum)) {
			crnd_unpack_end(ctx);
			return false;
		}
	}

	crnd_unpack_end(ctx);

	return true;
}

static void makeDdsHeader(crnlib::DDSURFACEDESC2 *ddsd, U32 width, U32 height, U32 levels, RdrTexType texType, RdrTexFormat texFormat)
{
	using namespace crnlib;

	memset(ddsd, 0, sizeof(*ddsd));

	ddsd->dwSize = sizeof(ddsd);
	ddsd->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS | DDSD_LINEARSIZE;
	ddsd->ddsCaps.dwCaps = DDSCAPS_TEXTURE;

	ddsd->dwWidth = width;
	ddsd->dwHeight = height;
	ddsd->dwLinearSize = ((ddsd->dwWidth + 3) & ~3) * ((ddsd->dwHeight + 3) & ~3);

	if (levels > 1) {
		ddsd->dwMipMapCount = levels;
		ddsd->dwFlags |= DDSD_MIPMAPCOUNT;
		ddsd->ddsCaps.dwCaps |= (DDSCAPS_MIPMAP | DDSCAPS_COMPLEX);
	}

	if (texType == RTEX_CUBEMAP) {
		ddsd->ddsCaps.dwCaps |= DDSCAPS_COMPLEX;
		ddsd->ddsCaps.dwCaps2 |= DDSCAPS2_CUBEMAP;
		ddsd->ddsCaps.dwCaps2 |= DDSCAPS2_CUBEMAP_POSITIVEX|DDSCAPS2_CUBEMAP_NEGATIVEX|DDSCAPS2_CUBEMAP_POSITIVEY|DDSCAPS2_CUBEMAP_NEGATIVEY|DDSCAPS2_CUBEMAP_POSITIVEZ|DDSCAPS2_CUBEMAP_NEGATIVEZ;
	}

	ddsd->ddpfPixelFormat.dwSize = sizeof(ddsd->ddpfPixelFormat);
	ddsd->ddpfPixelFormat.dwFlags = DDPF_FOURCC;

	switch (texFormat) {
	case RTEX_DXT1:
		ddsd->ddpfPixelFormat.dwFourCC = (crn_uint32)PIXEL_FMT_DXT1;
		ddsd->ddpfPixelFormat.dwRGBBitCount = 0;
		ddsd->dwLinearSize >>= 4;
	xcase RTEX_DXT5:
		ddsd->ddpfPixelFormat.dwFourCC = (crn_uint32)PIXEL_FMT_DXT5;
		ddsd->ddpfPixelFormat.dwRGBBitCount = 0;
		ddsd->dwLinearSize >>= 3;
	}
}

static void setupCrnParams(crn_comp_params *params, int width, int height, int levels, int faces,
						   CrnTargetFileType fileType, CrnTargetFormat format, CrnQualityLevel quality)
{
	params->m_width = width;
	params->m_height = height;
	params->m_levels = levels;
	params->m_faces = faces;
	params->m_flags |= cCRNCompFlagReverseLevels;

	switch (fileType) {
	case CRN_FILE_TYPE_DDS:
		params->m_file_type = cCRNFileTypeDDS;
	xdefault:
		params->m_file_type = cCRNFileTypeCRN;
	}

	switch (quality) {
	case CRN_QUALITY_LOWEST:
		params->m_quality_level = 64;
	xcase CRN_QUALITY_HIGHEST:
		params->m_quality_level = 255;
	xdefault:
		params->m_quality_level = 128;
	}

	switch (format) {
	case CRN_FORMAT_DXT5:
		params->m_format = cCRNFmtDXT5;
	xcase CRN_FORMAT_DXT5nm:
		params->m_format = cCRNFmtDXT5_xGxR;
	xdefault:
		params->m_format = cCRNFmtDXT1;
	}
}

RdrTexFormat texFormatFromCrn(const void *crnHeader)
{
	const crn_header *header = static_cast<const crn_header *>(crnHeader);

	if (header->m_sig != crn_header::cCRNSigValue || header->m_header_size != sizeof(crn_header)) {
		return RTEX_INVALID_FORMAT;
	}

	switch (header->m_format) {
	case cCRNFmtDXT1:
		return RTEX_DXT1;
	case cCRNFmtDXT5:
	case cCRNFmtDXT5_xGxR:
		return RTEX_DXT5;
	}

	return RTEX_INVALID_FORMAT;
}

int crnGetTextureDims(const void *crnHeader, U32 *width, U32 *height, U32 *levels)
{
	const crn_header *header = static_cast<const crn_header *>(crnHeader);

	if (header->m_sig != crn_header::cCRNSigValue || header->m_header_size != sizeof(crn_header)) {
		return 0;
	}

	if (width) {
		*width = header->m_width;
	}
	if (height) {
		*height = header->m_height;
	}
	if (levels) {
		*levels = header->m_levels;
	}

	return 1;
}

int crnNeedsWorkaround(const void *crnHeader, U32 fileBytes)
{
	const crn_header *header = static_cast<const crn_header *>(crnHeader);
	U32 ofs = header->m_level_ofs[0];
	U32 sz = header->m_level_sizes[0];

	return ofs + sz + sizeof(*header) != fileBytes;
}

size_t crnSizeForLevels(const void *crnHeader, U32 levels)
{
	const crn_header *header = static_cast<const crn_header *>(crnHeader);

	if (header->m_sig != crn_header::cCRNSigValue || header->m_header_size != sizeof(crn_header)) {
		return 0;
	}

	U32 maxLevels = header->m_levels;
	ANALYSIS_ASSUME(maxLevels <= cCRNMaxLevels);

	if (levels == 0) {
		levels = maxLevels;
	} else {
        levels = MIN(levels, maxLevels);
	}

	U32 topLevel = maxLevels - levels;

	if (header->m_flags & cCRNHeaderFlagReversedLevels) {
		return header->m_level_ofs[topLevel] + header->m_level_sizes[topLevel];
	}

	return header->m_data_size;
}

size_t crnDdsSizeForLevels(const void *crnHeader, U32 levels, bool includeDdsHeader)
{
	if (!crnHeader) {
		return 0;
	}

	const crn_header *header = static_cast<const crn_header *>(crnHeader);

	// sanity check
	if (header->m_sig != crn_header::cCRNSigValue || header->m_header_size != sizeof(crn_header)) {
		return 0;
	}

	if (levels == 0 || levels > header->m_levels) {
		levels = header->m_levels;
	}
	U32 mipsToSkip = header->m_levels - levels;

	U32 width = MAX(header->m_width >> mipsToSkip, 1);
	U32 height = MAX(header->m_height >> mipsToSkip, 1);

	RdrTexType texType = (header->m_faces == 6 ? RTEX_CUBEMAP : RTEX_2D);
	RdrTexFormat texFormat = texFormatFromCrn(crnHeader);

	size_t ddsSize = imgByteCount(texType, texFormat, width, height, 1, levels);

	if (includeDdsHeader) {
        ddsSize += sizeof(crnlib::DDSURFACEDESC2) + sizeof(U32);
	}

	return ddsSize;
}

size_t crnDecompress(void *dataOut, const void *crnHeader, const void *crnData, U32 levels, int fixupHeader)
{
	if (!dataOut || !crnHeader) {
		return 0;
	}

	const crn_header *header = static_cast<const crn_header *>(crnHeader);

	// sanity check
	if (header->m_sig != crn_header::cCRNSigValue || header->m_header_size != sizeof(crn_header)) {
		return 0;
	}

	if (!crnData) {
		crnData = static_cast<const U8*>(crnHeader) + sizeof(crn_header);
	}

	RdrTexType texType = (header->m_faces == 6 ? RTEX_CUBEMAP : RTEX_2D);
	RdrTexFormat texFormat = texFormatFromCrn(crnHeader);

	size_t ddsSize = crnDdsSizeForLevels(header, levels, false);

	if (!decompData(dataOut, crnHeader, crnData, texType, texFormat, levels, fixupHeader != 0)) {
		return 0;
	}

	return ddsSize;
}

size_t crnDecompressToDds(void *dataOut, const void *crnHeader, const void *crnData, U32 levels)
{
	if (!dataOut || !crnHeader) {
		return 0;
	}

	const crn_header *header = static_cast<const crn_header *>(crnHeader);

	// sanity check
	if (header->m_sig != crn_header::cCRNSigValue || header->m_header_size != sizeof(crn_header)) {
		return 0;
	}

	if (!crnData) {
		crnData = static_cast<const U8*>(crnHeader) + sizeof(crn_header);
	}

	if (levels == 0 || levels > header->m_levels) {
		levels = header->m_levels;
	}
	U32 mipsToSkip = header->m_levels - levels;

	U32 width = MAX(header->m_width >> mipsToSkip, 1);
	U32 height = MAX(header->m_height >> mipsToSkip, 1);

	RdrTexType texType = (header->m_faces == 6 ? RTEX_CUBEMAP : RTEX_2D);
	RdrTexFormat texFormat = texFormatFromCrn(crnHeader);

	size_t ddsSize = crnDdsSizeForLevels(header, levels, true);

	U8 *pixData = static_cast<U8*>(dataOut) + sizeof(crnlib::DDSURFACEDESC2) + sizeof(U32);

	if (!decompData(pixData, crnHeader, crnData, texType, texFormat, levels)) {
		return 0;
	}

	*static_cast<U32*>(dataOut) = crnlib::cDDSFileSignature;
	makeDdsHeader(reinterpret_cast<crnlib::DDSURFACEDESC2 *>(static_cast<U8*>(dataOut) + 4), width, height, levels, texType, texFormat);

	return ddsSize;
}

size_t crnCompressDds(void **crnDataOut, const void *ddsData, size_t ddsDataSize,
					  CrnTargetFileType fileType, CrnTargetFormat format, CrnQualityLevel quality)
{
	if (!crnDataOut || !ddsData || !ddsDataSize) {
		return 0;
	}

	// extract source images from the dds
	crn_texture_desc texDesc;
	crn_uint32 *pImages[cCRNMaxFaces * cCRNMaxLevels]; 
	if (!crn_decompress_dds_to_images(ddsData, (crn_uint32)ddsDataSize, pImages, texDesc)) {
		return 0;
	}

	// set up compression params
	crn_comp_params params;
	setupCrnParams(&params, texDesc.m_width, texDesc.m_height, texDesc.m_levels, texDesc.m_faces,
		fileType, format, quality);

	for (crn_uint32 i = 0; i < texDesc.m_faces; ++i) {
		for (crn_uint32 j = 0; j < texDesc.m_levels; ++j) {
			params.m_pImages[i][j] = pImages[i * texDesc.m_levels + j];
		}
	}

	crn_uint32 crnSize = 0;
	void *crnData = crn_compress(params, crnSize);

	crn_free_all_images(pImages, texDesc);

	if (crnData) {
		*crnDataOut = crnData;
		return crnSize;
	}

	*crnDataOut = 0;
	return 0;
}

size_t crnCompressArgb(void **crnDataOut, const void *argbData, U32 width, U32 height, U32 levels, float scale, int buildMips,
					    CrnTargetFileType fileType, CrnTargetFormat format, CrnQualityLevel quality)
{
	if (!crnDataOut || !argbData) {
		return 0;
	}

	//setup compression params
	crn_comp_params params;
	setupCrnParams(&params, width, height, levels, 1, fileType, format, quality);

	crn_mipmap_params mipParams;
	mipParams.m_mode = buildMips ? cCRNMipModeUseSourceOrGenerateMips : cCRNMipModeUseSourceMips;
	if (scale != 1.0f) {
		mipParams.m_scale_mode = cCRNSMRelative;
		mipParams.m_scale_x = mipParams.m_scale_y = scale;
	}

	const U8 *curMip = static_cast<const U8 *>(argbData);
	for (U32 i = 0; i < levels; ++i) {
		params.m_pImages[0][i] = reinterpret_cast<const crn_uint32 *>(curMip);
		
		curMip += imgByteCount(RTEX_2D, RTEX_BGRA_U8, width, height, 1, 1);
		width = MAX(1, width >> 1);
		height = MAX(1, height >> 1);
	}

	crn_uint32 crnSize = 0;
	void *crnData = crn_compress(params, mipParams, crnSize);

	if (crnData) {
		*crnDataOut = crnData;
		return crnSize;
	}
	
	*crnDataOut = 0;
	return 0;
}

void crnFree(void *crnData)
{
	crn_free_block(crnData);
}

void crnSegmentFile(void **baseFile, size_t *baseSize, void **highFile, size_t *highSize,
                    const void *srcHeader, const void *srcData, U32 baseLevels)
{
	static const size_t header_crc_size = sizeof(crn_header) - offsetof(crn_header, m_data_size);
	const crn_header *srcCrnHeader = static_cast<const crn_header *>(srcHeader);

	if (!srcData) {
		srcData = srcCrnHeader + 1;
	}

	crn_header baseCrnHeader, highCrnHeader;

	memcpy(&baseCrnHeader, srcCrnHeader, sizeof(baseCrnHeader));
	memcpy(&highCrnHeader, srcCrnHeader, sizeof(baseCrnHeader));

	U32 maxLevels = srcCrnHeader->m_levels;
	ANALYSIS_ASSUME(maxLevels <= cCRNMaxLevels);

	baseLevels = MIN(baseLevels, maxLevels);
	U32 highLevels = maxLevels - baseLevels;

	highCrnHeader.m_levels = highLevels;
	baseCrnHeader.m_levels = baseLevels;
	{
		U32 width = MAX(1, srcCrnHeader->m_width >> highLevels);
		U32 height = MAX(1, srcCrnHeader->m_height >> highLevels);
        baseCrnHeader.m_width = ALIGNUP(width, 4);
        baseCrnHeader.m_height = ALIGNUP(height, 4);
	}

	memset(highCrnHeader.m_level_ofs, 0, sizeof(highCrnHeader.m_level_ofs));
	memset(highCrnHeader.m_level_sizes, 0, sizeof(highCrnHeader.m_level_sizes));
	memset(baseCrnHeader.m_level_ofs, 0, sizeof(baseCrnHeader.m_level_ofs));
	memset(baseCrnHeader.m_level_sizes, 0, sizeof(baseCrnHeader.m_level_sizes));

	U32 metadataSize;
	if (srcCrnHeader->m_flags & cCRNHeaderFlagReversedLevels) {
		metadataSize = srcCrnHeader->m_level_ofs[srcCrnHeader->m_levels - 1];
	} else {
		metadataSize = srcCrnHeader->m_level_ofs[0];
	}

	// adjust header offsets
	U32 baseOfs = 0, highOfs = 0;
	if (srcCrnHeader->m_flags & cCRNHeaderFlagReversedLevels) {
		U32 curLevel;

		for (curLevel = highLevels; curLevel-- > 0; ) {
			U32 levelSize = srcCrnHeader->m_level_sizes[curLevel];
			highCrnHeader.m_level_ofs[curLevel] = highOfs + metadataSize;
			highCrnHeader.m_level_sizes[curLevel] = levelSize;
			highOfs += levelSize;
		}

		for (curLevel = baseLevels; curLevel-- > 0; ) {
            U32 levelSize = srcCrnHeader->m_level_sizes[curLevel + highLevels];
			baseCrnHeader.m_level_ofs[curLevel] = baseOfs + metadataSize;
			baseCrnHeader.m_level_sizes[curLevel] = levelSize;
			baseOfs += levelSize;
		}

		baseCrnHeader.m_data_size = srcCrnHeader->m_data_size - highOfs;
		highCrnHeader.m_data_size = srcCrnHeader->m_data_size - baseOfs;
	} else {
		U32 curLevel;

		for (curLevel = 0; curLevel < highLevels; ++curLevel) {
			U32 levelSize = srcCrnHeader->m_level_sizes[curLevel];
			highCrnHeader.m_level_ofs[curLevel] = highOfs + metadataSize;
			highCrnHeader.m_level_sizes[curLevel] = levelSize;
			highOfs += levelSize;
		}

		for (curLevel = 0; curLevel < baseLevels; ++curLevel) {
			U32 levelSize = srcCrnHeader->m_level_sizes[curLevel + highLevels];
			baseCrnHeader.m_level_ofs[curLevel] = baseOfs + metadataSize;
			baseCrnHeader.m_level_sizes[curLevel] = levelSize;
			baseOfs += levelSize;
		}

		baseCrnHeader.m_data_size = srcCrnHeader->m_data_size - highOfs;
		highCrnHeader.m_data_size = srcCrnHeader->m_data_size - baseOfs;
	}

	// create the two new textures
	*baseSize = sizeof(crn_header) + baseCrnHeader.m_data_size;
	*highSize = sizeof(crn_header) + highCrnHeader.m_data_size;

	void *baseCrn = ::malloc(*baseSize);
	void *highCrn = ::malloc(*highSize);

	memcpy((U8 *)baseCrn + sizeof(crn_header), srcData, metadataSize);
	memcpy((U8 *)highCrn + sizeof(crn_header), srcData, metadataSize);

	//copy the level data
	{
		U8 *highData = (U8 *)highCrn + sizeof(crn_header);
		U8 *baseData = (U8 *)baseCrn + sizeof(crn_header);

        for (U32 i = 0; i < highLevels; ++i) {
			memcpy(highData + highCrnHeader.m_level_ofs[i],
                (U8 *)srcData + srcCrnHeader->m_level_ofs[i], highCrnHeader.m_level_sizes[i]);
		}

		for (U32 i = 0; i < baseLevels; ++i) {
			memcpy(baseData + baseCrnHeader.m_level_ofs[i],
                (U8 *)srcData + srcCrnHeader->m_level_ofs[i + highLevels], baseCrnHeader.m_level_sizes[i]);
		}

		//fixup checksums
		highCrnHeader.m_data_crc16 = crnd::crc16(highData, highCrnHeader.m_data_size);
		highCrnHeader.m_header_crc16 = crnd::crc16(&highCrnHeader.m_data_size, header_crc_size);

		baseCrnHeader.m_data_crc16 = crnd::crc16(baseData, baseCrnHeader.m_data_size);
		baseCrnHeader.m_header_crc16 = crnd::crc16(&baseCrnHeader.m_data_size, header_crc_size);
    }

	memcpy(baseCrn, &baseCrnHeader, sizeof(baseCrnHeader));
	memcpy(highCrn, &highCrnHeader, sizeof(highCrnHeader));

	*baseFile = baseCrn;
	*highFile = highCrn;
}
