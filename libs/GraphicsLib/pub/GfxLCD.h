#pragma once

#define GFX_LCD_SKIP_FRAMES 10

S32 gfxLCDIsEnabled(void);

void gfxLCDInit(void);

void gfxLCDAddMeter(const char *name, F32 value, F32 minv, F32 maxv, U32 textcolor, U32 colormin, U32 colormid, U32 colormax);
void gfxLCDAddText(const char *text, U32 color);
bool gfxLCDIsQVGA(void); // use 3 lines if black and white, 8 lines if QVGA

void gfxLCDOncePerFrame(void);
