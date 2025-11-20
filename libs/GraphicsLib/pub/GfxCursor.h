#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

typedef struct RdrDevice RdrDevice;
typedef struct AtlasTex AtlasTex;
typedef union Color Color;

void gfxCursorClear(void);
void gfxCursorBlit(AtlasTex *atlas, int x, int y, F32 scale, Color clr);
void gfxCursorSet(RdrDevice *device, const char *name, int iHotX, int iHotY); // Device must be locked.  name* can be a temp/stack buffer
