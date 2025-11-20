#pragma once

#include "pub/GfxTexOpts.h"
#include "pub/GfxTextureEnums.h"

typedef void (*TexOptReloadCallback)(const char *path);

void texoptLoad(void);
void texoptLoadPostProcess(void);
void texoptReloadPostProcess(void);
TexOpt *texoptFromTextureName(const char *name, TexOptFlags *texopt_flags);
//void texoptCreateTextures(void); // Creates "textures" that are stubbed in texopts but don't exist as .tgas
void texoptSetReloadCallback(TexOptReloadCallback callback);
TexOptQuality texoptGetQuality(const TexOpt *texopt);
TexOptCompressionType texoptGetCompressionType(const TexOpt *texopt, TexOptFlags texopt_flags);
F32 texoptGetAlphaMipThreshold(const TexOpt *texopt);
TexOptMipSharpening texoptGetMipSharpening(const TexOpt *texopt);
const char *texoptGetMipSharpeningString(const TexOpt *texopt);
bool texoptShouldCrunch(const TexOpt *texopt, TexOptFlags texopt_flags);

extern ParseTable parse_tex_opt[];
extern ParseTable parse_TexOptList[];
