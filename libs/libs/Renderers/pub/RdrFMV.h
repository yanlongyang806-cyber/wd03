#pragma once

#ifndef _M_X64
// Bink disabled for DX11 compatibility until we sign a license and can bug them for DX11 code
#define ENABLE_BINK 1
#endif

typedef struct RdrDevice RdrDevice;
typedef struct RdrFMV RdrFMV;

RdrFMV *rdrFMVOpen(RdrDevice *device, const char *filename);

void rdrFMVGetSize(RdrFMV *fmv, int *w, int *h);

void rdrFMVPlay(RdrFMV *fmv, F32 x, F32 y, F32 x_scale, F32 y_scale, F32 alpha_level);
void rdrFMVSetVolume(RdrFMV *fmv, F32 volume);
void rdrFMVSetDrawParams(RdrFMV *fmv, F32 x, F32 y, F32 x_scale, F32 y_scale, F32 alpha_level);

void rdrFMVClose(RdrFMV **fmv);

bool rdrFMVDone(RdrFMV *fmv);