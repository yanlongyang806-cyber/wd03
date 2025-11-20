#include "GraphicsLib.h"
#include "UIInternal.h"


static void elAuxDevicePerFrameCallback(RdrDevice *rdr_device, void *userData)
{
	gfxAuxDeviceDefaultTop(rdr_device, 0, ui_OncePerFramePerDevice);
	gfxAuxDeviceDefaultBottom(rdr_device, 0);
}

AUTO_COMMAND;
void elAuxDeviceAdd(void)
{
	static int count=0;
	char buf[1024];
	count++;
	sprintf(buf, "Aux Window %d", count);
	gfxAuxDeviceAdd(NULL, buf, NULL, elAuxDevicePerFrameCallback, NULL);

}
