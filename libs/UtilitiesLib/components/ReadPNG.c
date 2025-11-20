#include "File.h"
#include "GlobalTypes.h"
#include "../../3rdparty/libpng/png.h"
#include "utils.h"
#include "ReadPNG.h"

#ifdef _WIN64
#pragma comment(lib, "libpngX64.lib")
#else
#pragma comment(lib, "libpng.lib")
#endif

#define PNG_RGBA_CHANNELS 4

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

char* ReadPNG_FileEx(const char *pFileName, int *width, int *height, bool flipVerticalAxis)
{
	int colorType;

	FILE *pFile = NULL;	
	png_structp pPng;
	png_infop pPngInfo;

	char *imgData;
	int i, local_height, local_width;

	//---------------------------------------------
	int bits;
	png_bytep *rows;
	//---------------------------------------------


	pFile = fopen(pFileName,"!rb");

	if(!pFile) {
		printf("Attempting to open %s which does not exist.\n", pFileName);
		return false;
	}

	pPng = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!pPng)
	{
		fclose(pFile);
		return false;   
	}

	pPngInfo = png_create_info_struct(pPng);
	if (!pPngInfo) 
	{
		fclose(pFile);
		png_destroy_read_struct(&pPng, NULL, NULL);
		return false;   
	}

	// it's a goto (in case png lib hits the bucket)
	if (setjmp(png_jmpbuf(pPng))) 
	{
		fclose(pFile);
		png_destroy_read_struct(&pPng, &pPngInfo, NULL);
		return false;
	}

	png_init_io(pPng, fileRealPointer(pFile));
	png_read_info(pPng, pPngInfo);

	*width     = local_width = (int)png_get_image_width(pPng, pPngInfo);
	*height    = local_height = (int)png_get_image_height(pPng, pPngInfo);
	bits      = png_get_bit_depth(pPng, pPngInfo);
	colorType = png_get_color_type(pPng, pPngInfo);

	imgData = malloc((*width) * (*height) * PNG_RGBA_CHANNELS * sizeof(char));

	// De-palettize.
	if (colorType == PNG_COLOR_TYPE_PALETTE) {
		png_set_palette_to_rgb(pPng);
	}

	// Convert grayscale to RGB.
	if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
		png_set_gray_to_rgb(pPng);
	}

	// 8-bits per channel for our internal format.
	if (bits == 16) {
		png_set_strip_16(pPng);
		bits = 8;
	}

	// Convert transparency to alpha.
	if(png_get_valid(pPng, pPngInfo, PNG_INFO_tRNS)) {
		png_set_tRNS_to_alpha(pPng);
	}

	if (colorType == PNG_COLOR_TYPE_RGB)
		png_set_filler(pPng, 0xFF, PNG_FILLER_AFTER);

	rows = malloc(local_height * sizeof(png_bytep));
	for(i = 0; i < local_height; i++) {
		int actual_i = flipVerticalAxis ? local_height - 1 - i : i;
		rows[i] = imgData + actual_i * local_width * PNG_RGBA_CHANNELS;
	}

	png_read_image(pPng, rows);

	free(rows);
	png_destroy_read_struct(&pPng, &pPngInfo, NULL);
	fclose(pFile);

	return imgData;
	
}