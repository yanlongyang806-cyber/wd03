#include "GfxDebug.h"
#include "GraphicsLibPrivate.h"
#include "RdrState.h"

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("gfxTimeSinceWindowChanged");
F32 gfxTimeSinceWindowChanged(void)
{
	return gfx_state.timeSinceWindowChange;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("gfxShouldShowRestoreButtons");
bool gfxShouldShowRestoreButtons(void)
{
	return gfx_state.shouldShowRestoreButtons && !rdr_state.disable_windowed_fullscreen;
}
