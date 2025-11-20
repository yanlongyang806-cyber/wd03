#ifndef GFXCOMMANDPARSE_H
#define GFXCOMMANDPARSE_H

#include "GraphicsLib.h"
#include "cmdparse.h"

int gfxCommandParse(CMDARGS);

bool gfxDoingPostprocessing(void);
bool gfxDoingOutlining(void);

void gfxSetFrameRatePercentBG(F32 percent);
void gfxSetFrameRatePercent(F32 percent);

void gfxScreenshotName(const char *name);
void gfxSaveScreenshot3dOnly(const char *filename);
void gfxSaveScreenshot3dOnlyDefault(void);
void gfxSaveScreenshotWithUI(const char *filename);
void gfxSaveScreenshotWithUIDefault(void);
void gfxSaveScreenshotDepth(const char *filename, float min, float max);
void gfxSaveScreenshotDepthDefault(void);
    
void gfxSaveJPGScreenshot3dOnly(const char *filename);
void gfxSaveJPGScreenshot3dOnlyDefault(void);
void gfxSaveJPGScreenshot3dOnlyOverride(const char *filename, int jpegQuality);

void gfxSaveJPGScreenshotWithUI(const char *filename);
void gfxSaveJPGScreenshotWithUIDefault(void);
void gfxSaveJPGScreenshotWithUIOverride(const char *filename, int jpegQuality);
void gfxSaveJPGScreenshotWithUIOverrideCallback(const char *filename, int jpegQuality, GfxScreenshotCallBack *callback, void *userdata);

void setMSAA(GfxAntialiasingMode mode, int samples);
void useSM20(int enable);
void useSM2B(int enable);
void useSM30(int enable);

void gfxScreenSetSize(int width, int height);
void gfxScreenSetPosAndSize(int x0, int y0, int width, int height);
void gfxToggleFullscreen(void);

void ShaderTestN(int index, const char *define_name);

#endif
