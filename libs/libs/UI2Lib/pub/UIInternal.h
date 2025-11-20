/***************************************************************************



***************************************************************************/

#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include "UICore.h"

// Functions "internal" to UILib. They're not really private, they're just
// not much use outside of it, and UICore.h should remain small.

void ui_Startup(void);
void ui_OncePerFramePerDevice(F32 frameTime, SA_PARAM_NN_VALID RdrDevice *currentDevice);

// Reset the internal cursor.
void ui_CursorOncePerFrameBeforeTick(void);
void ui_CursorOncePerFrameAfterTick(void);

// Fill the initial texture cache.
void ui_FillTextureCache(void);

void ui_InitKeyBinds(void);

SA_RET_NN_VALID UIDeviceState *ui_StateForDevice(SA_PARAM_OP_VALID RdrDevice *device);

SA_RET_NN_VALID UIWidgetGroup *ui_WidgetGroupForDevice(SA_PARAM_OP_VALID RdrDevice *device);
SA_RET_NN_VALID UIWidgetGroup *ui_TopWidgetGroupForDevice(SA_PARAM_OP_VALID RdrDevice *device);
SA_RET_NN_VALID UIWindowGroup *ui_WindowGroupForDevice(SA_PARAM_OP_VALID RdrDevice *device);
SA_RET_NN_VALID UIWidgetGroup *ui_PaneWidgetGroupForDevice(SA_PARAM_OP_VALID RdrDevice *device);

void ui_DrawBackground(void);
void ui_DrawResolutionOutlines(void);

void ui_CursorLoad(void);

#endif
