#include "GfxUnload.h"
#include "GfxModelUnload.h"
#include "texUnload.h"

void gfxUnloadCheck(void)
{
	texUnloadTexturesToFitMemory();
	gfxGeoUnloadCheck();
}

void gfxUnloadAllNotUsedThisFrame(void)
{
	// Unloads all data which was not used last frame.
	// This is expected to be called when the user suddenly moves to a new location,
	//   and we start drawing one frame (ideally in all 6 directions), and we
	//   want to immediately unload everything not used in the nearby scene.

	texUnloadAllNotUsedThisFrame();
	gfxGeoUnloadAllNotUsedThisFrame();

	// TODO: Terrain
	// TODO: Anything else?
}

