#pragma once
GCC_SYSTEM

//WARNING:
//Does not support every tiff format.
//If you want to load any kind of tiff the code would have to be expanded.

typedef enum TiffPredictorType
{
	TIFF_DIFF_NONE,
	TIFF_DIFF_HORIZONTAL,
	TIFF_DIFF_FLOAT,
} TiffPredictorType;

typedef struct
{
	U32	width;
	U32	height;
	U32 bytesPerChannel;
	U32 numChannels;
	bool isFloat;
	bool lzwCompressed;
	TiffPredictorType horizontalDifferencingType;
	U32	rows_per_strip;
	U32	strip_count;
	U32	*strip_offsets;
	U32	*strip_byte_counts;
} TiffIFD;

void *tiffLoad_dbg(FILE *file, int *width, int *height, int *bytesPerChannel, int *numChannels, bool *isFloat MEM_DBG_PARMS);
#define tiffLoad(file, width, height, bytesPerChannel, numChannels, isFloat) tiffLoad_dbg(file, width, height, bytesPerChannel, numChannels, isFloat MEM_DBG_PARMS_INIT)

void *tiffLoadFromFilename_dbg(const char *filename, int *width, int *height, int *bytesPerChannel, int *numChannels, bool *isFloat MEM_DBG_PARMS);
#define tiffLoadFromFilename(filename, width, height, bytesPerChannel, numChannels, isFloat) tiffLoadFromFilename_dbg(filename, width, height, bytesPerChannel, numChannels, isFloat MEM_DBG_PARMS_INIT)

bool tiffSave(FILE *file, const void *data, U32 width, U32 height, U32 bytesPerChannel, U32 numChannels, bool isFloat, bool compress, TiffPredictorType horizontalDifferencingType);
bool tiffSaveToFilename(const char *filename, const void *data, U32 width, U32 height, U32 bytesPerChannel, U32 numChannels, bool isFloat, bool compress, TiffPredictorType horizontalDifferencingType);

void tiffWriteHeader(FILE *file, U32 first_ifd_offset, U32 header_location);
void tiffWriteIFD(FILE *file, const TiffIFD *imageFileDir, U32 imageFileDirStartPos);
U32 tiffWriteStrip(FILE *file, const TiffIFD *imageFileDir, const void *data, U32 num_rows);//returns the compressed size in bytes

bool tiffReadHeader(FILE *file, U32 *first_ifd_offset);

bool tiffReadIFD_dbg(FILE *file, TiffIFD *imageFileDir, U32 imageFileDirStartPos MEM_DBG_PARMS);
#define tiffReadIFD(file, imageFileDir, imageFileDirStartPos) tiffReadIFD_dbg(file, imageFileDir, imageFileDirStartPos MEM_DBG_PARMS_INIT)

bool tiffReadStrip_dbg(FILE *file, const TiffIFD *imageFileDir, U8 *strip, U32 num_lines, U32 strip_idx MEM_DBG_PARMS);
#define tiffReadStrip(file, imageFileDir, strip, num_lines, strip_idx) tiffReadStrip_dbg(file, imageFileDir, strip, num_lines, strip_idx MEM_DBG_PARMS_INIT)

U8 *lzwCompress(const U8 *data, U32 size, U32 *size_out);
bool lzwUncompress(const U8 *data_in, U8 *data_out, U32 size_in, U32 size_out);

