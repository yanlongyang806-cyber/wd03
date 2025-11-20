#pragma once

#include "ImageTypes.h"

C_DECLARATIONS_BEGIN

#if !PLATFORM_CONSOLE
int nvdxtCompress(U8 *dataIn, U8 *dataOut, int width, int height, RdrTexFormat fmt, int quality, int max_extent);
#else
__forceinline static int nvdxtCompress(U8 *data_in, U8 *data_out, int width, int height, int fmt, int quality, int max_extent) { return 0; }
#endif

C_DECLARATIONS_END
