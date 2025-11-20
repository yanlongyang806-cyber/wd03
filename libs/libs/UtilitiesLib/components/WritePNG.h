#pragma once

typedef struct
{
	char r;
	char g;
	char b;
	char a;
} PngPixel4;

typedef struct
{
	char r;
	char g;
	char b;
} PngPixel3;

#define WritePNG_File(pImageData, iXSize, iYSize, iRealWidth, iByteDepth, pOutFileName) \
	WritePNG_FileEx(pImageData, iXSize, iYSize, iRealWidth, iByteDepth, pOutFileName, false, false, false)

//realwidth is the width of the source data, iXSize is the number of textures we actually want to see
bool WritePNG_FileEx(void *pImageData, int iXSize, int iYSize, int iRealWidth, int iByteDepth /*3 or 4*/,
					 const char *pOutFileName, bool flipVerticalAxis, bool texIsBGRA, bool doNxDecompression);

bool WritePNG_FileWrapper(void *pImageData, int iXSize, int iYSize, int iRealWidth, int iByteDepth /*3 or 4*/,
	FILE *pFile, bool flipVerticalAxis, bool texIsBGRA, bool doNxDecompression);

bool WritePNG_StuffBuff(void *pImageData, int iXSize, int iYSize, int iRealWidth, int iByteDepth /*3 or 4*/,
	StuffBuff *psb, bool flipVerticalAxis, bool texIsBGRA, bool doNxDecompression);
