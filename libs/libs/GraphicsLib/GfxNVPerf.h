#pragma once

#if _PS3

static inline void gfxNVPerfStartFrame(void) {}
static inline void gfxNVPerfEndFrame(void) {}
static inline F32 gfxNVPerfGetGPUIdle(void) { return 0.f; }
static inline bool gfxNVPerfContinue(void) { return 0; }
static inline int gfxNVPerfDisplay(int y) { return y; }

#else

void gfxNVPerfStartFrame(void);
void gfxNVPerfEndFrame(void);
F32 gfxNVPerfGetGPUIdle(void);
bool gfxNVPerfContinue(void);
int gfxNVPerfDisplay(int y);
U32 gfxNVPerfGetGPUMemUsage(void);
U32 gfxNVPerfGetGPUStartupMemUsage(void);


#endif

