#ifndef GFXTEXTUREENUMS_H
#define GFXTEXTUREENUMS_H
GCC_SYSTEM

#include "ImageTypes.h"

typedef struct TexWord TexWord;
typedef struct BasicTexture BasicTexture;

typedef enum TexLoadHow {
	TEX_LOAD_IN_BACKGROUND					= 1,
	TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD,
//	TEX_LOAD_NOW_CALLED_FROM_LOAD_THREAD,
//	TEX_LOAD_DONT_ACTUALLY_LOAD,
//	TEX_LOAD_IN_BACKGROUND_FROM_BACKGROUND,
} TexLoadHow;

typedef enum TexGenMode {
	TEXGEN_NORMAL, // Simple texture
	TEXGEN_VOLATILE_SHARED, // Replicated across renderers, get destroyed when gfxRegisterDevice is called (put your own reset callback in gfxHandleNewDevice)
} TexGenMode;

#endif
