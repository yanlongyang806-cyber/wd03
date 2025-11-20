#if !PLATFORM_CONSOLE

#include "File.h"
#include "GlobalTypes.h"
#include "../../3rdparty/libpng/png.h"
#include "utils.h"
#include "ScratchStack.h"
#include "WritePNG.h"
#include "Color.h"
#include "mathutil.inl"

#ifdef _WIN64
#pragma comment(lib, "libpngX64.lib")
#else
#pragma comment(lib, "libpng.lib")
#endif

__forceinline void dxtnmDecompress(Color* src, Color* dst)
{
	float y = ((float)src->g / 255.0f) * 2.0f - 1.0f;
	float z = ((float)src->a / 255.0f) * 2.0f - 1.0f;
	float x = sqrtf_clamped(1.0f - (y*y + z*z));

	dst->r = src->a;
	dst->g = src->g;
	dst->b = (U8)(x * 255.0);
	dst->a = 255;
}

static void pngWriteData(png_structp pPng, png_bytep data, png_size_t length)
{
	FILE *pFile = (FILE *)pPng->io_ptr;
	fwrite(data, length, 1, pFile);
}

static void pngFlush(png_structp pPng)
{
	FILE *pFile = (FILE *)pPng->io_ptr;
	fflush(pFile);
}

bool writePNG_ErrorCleanup(FILE *pFile, png_structp *pPng, png_infop *pPngInfo)
{
	fclose(pFile);
	png_destroy_write_struct(pPng, pPngInfo);
	return false;
}

bool WritePNG_FileWrapper(void *pImageData, int iXSize, int iYSize, int iRealWidth, int iByteDepth /*3 or 4*/,
					FILE *pFile, bool flipVerticalAxis, bool texIsBGRA, bool doNxDecompression)
{
	int m_bytesPerPixel;
	int colorType, bitDepthPerChannel;

	png_structp pPng;
	png_infop pPngInfo;
	int bytesPerRow;
	int row;
	U8 * swapTempData = NULL;
	U8 * swapTempDataHead = NULL;

	pPng = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,NULL, NULL);
	if(!pPng)
		return false;

	png_set_write_fn(pPng, pFile, pngWriteData, pngFlush);

	pPngInfo = png_create_info_struct(pPng);
	if (!pPngInfo) 
	{
		return writePNG_ErrorCleanup(pFile, &pPng, NULL);
	}

	// it's a goto (in case png lib hits the bucket)
	if (setjmp(png_jmpbuf(pPng))) 
	{
		return writePNG_ErrorCleanup(pFile, &pPng, &pPngInfo);
	}

	png_set_compression_level(pPng, Z_BEST_SPEED); //compresion level 0(none)-9(best compression) 
		// Z_NO_COMPRESSION         0
		// Z_BEST_SPEED             1
		// Z_BEST_COMPRESSION       9

	if (iByteDepth == 3)
	{
		m_bytesPerPixel = 3;
		bitDepthPerChannel = m_bytesPerPixel/3*8;
		colorType = PNG_COLOR_TYPE_RGB;
	}
	else if (iByteDepth == 4)
	{
		m_bytesPerPixel = 4;
		bitDepthPerChannel = m_bytesPerPixel/4*8;
		colorType = PNG_COLOR_TYPE_RGB_ALPHA;
	}
	else 
	{
		return writePNG_ErrorCleanup(pFile, &pPng, &pPngInfo);
	}

	png_set_IHDR(pPng, pPngInfo, (U32)iXSize, (U32)iYSize,
		bitDepthPerChannel, colorType, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_write_info(pPng, pPngInfo);

	if(bitDepthPerChannel == 16) //reverse endian order (PNG is Big Endian)
	{
		png_set_swap(pPng);
	}

	bytesPerRow = iRealWidth*m_bytesPerPixel;

	//write non-interlaced buffer  
	if (texIsBGRA || doNxDecompression)
	{
		swapTempDataHead = swapTempData = (U8*)ScratchAlloc(bytesPerRow);
	}

	assert(!doNxDecompression || m_bytesPerPixel==4);

	for(row=0; row <iYSize; ++row)
	{
		int realRow = flipVerticalAxis ? (iYSize - row - 1) : row;
		U8 * source_data = ((char*)pImageData) + (realRow * bytesPerRow);
		if (texIsBGRA || doNxDecompression)
		{
			int pixel = 0;
			swapTempData = swapTempDataHead;
			for (; pixel < iXSize; ++pixel, source_data += m_bytesPerPixel, swapTempData += m_bytesPerPixel)
			{
				if (texIsBGRA) {
					if (m_bytesPerPixel == 3)
					{
						swapTempData[0] = source_data[2];
						swapTempData[1] = source_data[1];
						swapTempData[2] = source_data[0];
					}
					else
					{
						swapTempData[0] = source_data[2];
						swapTempData[1] = source_data[1];
						swapTempData[2] = source_data[0];
						swapTempData[3] = source_data[3];
					}
				}

				// Do NX decompression if necessary to fix some normal maps.
				if(doNxDecompression) {
					dxtnmDecompress((Color*)source_data,(Color*)swapTempData);
				}
			}
			source_data = swapTempDataHead;
		}
		png_write_row(pPng, source_data);
	}

	if (texIsBGRA || doNxDecompression)
		ScratchFree(swapTempDataHead);

	png_write_end(pPng, NULL);

	png_destroy_write_struct(&pPng, &pPngInfo);

	return true;
	
}

bool WritePNG_FileEx(void *pImageData, int iXSize, int iYSize, int iRealWidth, int iByteDepth /*3 or 4*/,
	const char *pFileName, bool flipVerticalAxis, bool texIsBGRA, bool doNxDecompression)
{
	bool bResult;

	FILE *pFile = fopen(pFileName,"wb");
	if(!pFile)
		return false;

	bResult = WritePNG_FileWrapper(pImageData, iXSize, iYSize, iRealWidth, iByteDepth,
		pFile, flipVerticalAxis, texIsBGRA, doNxDecompression);

	fclose(pFile);

	return bResult;
}

bool WritePNG_StuffBuff(void *pImageData, int iXSize, int iYSize, int iRealWidth, int iByteDepth /*3 or 4*/,
	StuffBuff *psb, bool flipVerticalAxis, bool texIsBGRA, bool doNxDecompression)
{
	bool bResult;

	FILE *pFile = fileOpenStuffBuff(psb);
	if(!pFile)
		return false;

	bResult = WritePNG_FileWrapper(pImageData, iXSize, iYSize, iRealWidth, iByteDepth,
		pFile, flipVerticalAxis, texIsBGRA, doNxDecompression);

	fclose(pFile);

	return bResult;
}

#else

bool WritePNG_FileEx(void *pImageData, int iXSize, int iYSize, int iRealWidth, int iByteDepth /*3 or 4*/, char *pOutFileName, bool flipVerticalAxis, bool texIsBGRA)
{
	return false;
}

bool WritePNG_FileWrapper(void *pImageData, int iXSize, int iYSize, int iRealWidth, int iByteDepth /*3 or 4*/, FILE *pFile, bool flipVerticalAxis, bool texIsBGRA, bool doNxDecompression)
{
	return false;
}

bool WritePNG_StuffBuff(void *pImageData, int iXSize, int iYSize, int iRealWidth, int iByteDepth /*3 or 4*/, StuffBuff *psb, bool flipVerticalAxis, bool texIsBGRA, bool doNxDecompression)
{
	return false;
}

#endif
