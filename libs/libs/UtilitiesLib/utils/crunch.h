#pragma once

#include "stdtypes.h"
#include "ImageTypes.h"

#define CRUNCH_HEADER_SIZE		198

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Materials););

typedef enum CrnTargetFileType {
	CRN_FILE_TYPE_DDS,
	CRN_FILE_TYPE_CRN,
} CrnTargetFileType;

typedef enum CrnTargetFormat {
	CRN_FORMAT_ARGB,
	CRN_FORMAT_DXT1,
	CRN_FORMAT_DXT5,
	CRN_FORMAT_DXT5nm,
} CrnTargetFormat;

typedef enum CrnQualityLevel {
	CRN_QUALITY_NORMAL,
	CRN_QUALITY_LOWEST,
	CRN_QUALITY_HIGHEST,
} CrnQualityLevel;

C_DECLARATIONS_BEGIN

RdrTexFormat texFormatFromCrn(const void *crnHeader);

int crnGetTextureDims(const void *crnHeader, U32 *width, U32 *height, U32 *levels);

// Returns the total *compressed* size for the texture with the given number of levels.
// if numLevels is 0, returns the total size of all levels.
//
size_t crnSizeForLevels(const void *crnHeader, U32 levels);

// Returns the total *uncompressed* size for the texture with the given number of levels.
// if numLevels is 0, returns the total size of all levels.
//
size_t crnDdsSizeForLevels(const void *crnHeader, U32 levels, bool includeDdsHeader);

// Decompresses crunched texture data.
//
// dataOut:				Buffer to receive the uncompressed data. The size of this buffer can be found with crnDdsSizeForLevels().
// crnHeader:			Pointer to the CRN file header
// crnData:				Pointer to the CRN data. If NULL, assumes the data immediately follows crnHeader
// levels:				Number of levels to load. If 0, loads all levels.
// fixupHeader:			(HACK) (TEMP) Work-around an incorrect crn file header.
//
// Returns the size of the uncompressed data.
//
size_t crnDecompress(void *dataOut, const void *crnHeader, const void *crnData, U32 levels, int fixupHeader);

// As above, but adds a DDS file header to the start of the data
size_t crnDecompressToDds(void *dataOut, const void *crnHeader, const void *crnData, U32 levels);

// Compresses a DDS surface.
//
// crnDataOut:	(OUT) pointer to the address of the allocated compressed data. Free with crnFree().
// ddsData:		input dds surface.
// ddsDataSize: size of the input dds data.
// fileType:	specifies the destination file type, either crunch or DDS.
// format:		the target texture format
// quality:		one of the CrnQualityLevel values.
//
// returns the size of the compressed data, or 0 on error.
//
size_t crnCompressDds(void **crnDataOut, const void *ddsData, size_t ddsDataSize, 
					  CrnTargetFileType fileType, CrnTargetFormat format, CrnQualityLevel quality);

// Compresses RGBA texture data.
//
// crnDataOut:	(OUT) pointer to the address of the allocated compressed data. Free with crnFree().
// argbData:	input rgba data.
// width, height: dimensions of the base level
// levels:		number of levels of the input data
// scale:		relative scale to be applied to each level before compression
// buildMips:	if non-zero, generates mipmaps from the base level.
// fileType:	specifies the destination file type, either crunch or DDS.
// format:		the target texture format
// quality:		one of the CrnQualityLevel values.
//
// returns the size of the compressed data, or 0 on error.
//
size_t crnCompressArgb(void **crnDataOut, const void *argbData,
					   U32 width, U32 height, U32 levels, float scale, int buildMips,
					   CrnTargetFileType fileType, CrnTargetFormat format, CrnQualityLevel quality);

// Frees memory allocated by crunchCompressDds.
void crnFree(void *crnData);

// Segments crunched data into two parts based on a level count. Two new crunch files are
// created, one for the levels >= the given threshold, and another for the smaller ones.
// The source data is not modified.
//
// baseFile:		(OUT) the new file for the base levels. Free with free().
// baseSize:		(OUT) size of the file of the base levels
// highFile:		(OUT) the new file for the high levels. Free with free().
// highSize:		(OUT) size of the file of the high levels
// srcHeader:		Header of the source file for the data to split
// srcData:			Crunched texel data. If NULL, assumes the data immediately follows srcHeader
// baseLevels:		The number of levels to place in the base file.
//
void crnSegmentFile(void **baseFile, size_t *baseSize, void **highFile, size_t *highSize,
					const void *srcHeader, const void *srcData, U32 baseLevels);

// TEMP FIX - Remove once textures get reprocessed.
// Returns if a crunch file needs the workaround for the bad headers bug.
//
int crnNeedsWorkaround(const void *srcHeader, U32 fileBytes);

C_DECLARATIONS_END
