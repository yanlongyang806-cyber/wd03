#include "gclAIDebugUI.h"
#include "aiDebugShared.h"

void aiDebugViewInternal(int bShow);

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(7);
void aiDebugView(int bShow)
{
	aiDebugViewInternal(bShow);
}

AUTO_COMMAND ACMD_CLIENTCMD;
void aiAnimListDebugSetlistClient(StringListStruct *list)
{
	aiAnimListDebugSetList(list);
}