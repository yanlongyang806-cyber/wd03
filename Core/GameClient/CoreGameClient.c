#include "GameClientLib.h"
#include "GlobalTypes.h"
#include "file.h"
#include "gfxSettings.h"
#include "GraphicsLib.h"

#ifndef _XBOX
#include "resource1.h"
#endif


AUTO_RUN_FIRST;
void InitGlobalConfig(void)
{
	gfxSetFeatures(GFEATURE_POSTPROCESSING|GFEATURE_OUTLINING|GFEATURE_DOF|GFEATURE_SHADOWS|GFEATURE_WATER);
#ifndef _XBOX
	gGCLState.logoResource = IDR_CRYPTIC_LOGO;
#endif
}

