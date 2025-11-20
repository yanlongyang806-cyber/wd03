#pragma once
GCC_SYSTEM
#ifndef GFXLIBTEST
#pragma message("A project other than GraphicsLibTest is includeing GfxTesting.h.  BAD!\r\n")
#endif

#include "GfxTextureEnums.h"
#include "MaterialEnums.h"

typedef struct Material Material;
typedef struct MaterialNamedTexture MaterialNamedTexture;
typedef struct ShaderGraph ShaderGraph;
typedef struct RdrMaterial RdrMaterial;
typedef struct BasicTexture BasicTexture;
typedef struct RdrSubobject RdrSubobject;
typedef struct RdrLight RdrLight;
typedef struct MaterialNamedTexture MaterialNamedTexture;
typedef int GeoHandle;

// Public header file with private functions which are currently being tested in a LibTester project.
// Should not include anything called by actual game projects!


