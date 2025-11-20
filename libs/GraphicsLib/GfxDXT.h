#pragma once
GCC_SYSTEM

#include "ImageTypes.h"

typedef struct TexReadInfo TexReadInfo;

#define textureMipsReversed(basicTex) ((basicTex->flags&(TEX_REVERSED_MIPS|TEX_CRUNCH))==TEX_REVERSED_MIPS)
bool uncompressRawTexInfo(TexReadInfo *rawInfo, bool reversed_mips); // Uncompresses to RTEX_RGBA_U8
