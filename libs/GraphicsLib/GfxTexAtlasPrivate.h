#pragma once
GCC_SYSTEM

// Private to GraphicsLib

typedef struct AtlasTex AtlasTex;
typedef U64 TexHandle;

// this needs to be called at the beginning of each frame
void atlasDoFrame(void);
void atlasDisplayStats(void);

void atlasMakeWhite(void);
void resetTextures(void);

bool isDoingAtlasTex(void);
