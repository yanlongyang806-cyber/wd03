#include "MapDescription.h"
#include "gslSendToClient.h"
#include "textparser.h"
#include "ServerLib.h"

#include "AutoGen/MapDescription_h_ast.h"



AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(ServerPatchInfo) ACMD_ACCESSLEVEL(9);
void gslPatchingShowPatchInfo(Entity *pEnt)
{
	char *encoded=NULL;
	ParserWriteText(&encoded, parse_DynamicPatchInfo, ServerLib_GetPatchInfo(), 0, 0, 0);
	gslSendPrintf(pEnt, "Patch info:\n%s\n", encoded);
	estrDestroy(&encoded);
}

