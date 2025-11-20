#pragma once

typedef U16 InpKeyCode;
typedef struct BasicTexture BasicTexture;
typedef struct HTMLViewer HTMLViewer;
typedef struct KeyInput KeyInput;

void hv_Init(void);
void hv_Shutdown(void);
void hv_Update(void);

S32 hv_CreateViewer(HTMLViewer **viewerOut, F32 w, F32 h, const char* url);
S32 hv_Destroy(HTMLViewer **viewerInOut);

// !!! Queries -----------------------------------------------------------------
S32 hv_IsLoading(HTMLViewer *viewer);
S32 hv_GetMainURL(HTMLViewer *viewer, const char ** page);
S32 hv_IsDirty(HTMLViewer *viewer, S32 *dirtyOut);
S32 hv_GetTexture(HTMLViewer *viewer, BasicTexture **texOut);
S32 hv_GetDimensions(HTMLViewer *viewer, Vec2 dimOut);

// !!! Operations --------------------------------------------------------------
S32 hv_SendToScreen(HTMLViewer *viewer);

// NEEDS "http://www."
S32 hv_LoadRemote(HTMLViewer *viewer, const char* page);

S32 hv_LoadLocal(HTMLViewer *viewer, const char* page);
S32 hv_Render(HTMLViewer *viewer, S32 force);
S32 hv_Resize(HTMLViewer *viewer, S32 w, S32 h);
S32 hv_Unfocus(HTMLViewer *viewer);
S32 hv_Focus(HTMLViewer *viewer);

// !!! Input injection ---------------------------------------------------------
S32 hv_InjectMousePos(HTMLViewer *viewer, S32 x, S32 y);
S32 hv_InjectMouseDown(HTMLViewer *viewer, S32 x, S32 y, InpKeyCode code);
S32 hv_InjectMouseUp(HTMLViewer *viewer, S32 x, S32 y, InpKeyCode code);
S32 hv_InjectKey(HTMLViewer *viewer, KeyInput *key);
