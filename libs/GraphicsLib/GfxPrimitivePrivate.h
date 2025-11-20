#ifndef GFXPRIMITIVEPRIVATE_H
#define GFXPRIMITIVEPRIVATE_H

void gfxDrawQueuedPrims(void);
void gfxQueueSimplePrims(void);
int gfxGetQueuedPrimCount(void);

void gfxPrimitiveDoDataLoading(void);
void gfxPrimitiveCleanupPerFrame();
void gfxPrimitiveClearDrawables(void);

#endif