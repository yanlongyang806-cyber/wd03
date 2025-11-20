#include "aiDebug.h"
#include "aiDebugShared.h"
#include "AnimList_Common.h"

#include "ResourceInfo.h"
#include "StringUtil.h"

#include "GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_COMMAND;
void aiDebugSendAnimLists(Entity *e)
{
	int i;
	StringListStruct sls = {0};
	DictionaryEArrayStruct *deas = resDictGetEArrayStruct("AIAnimList");
	
	for(i=0; i<eaSize(&deas->ppReferents); i++)
	{
		AIAnimList *al = (AIAnimList*)deas->ppReferents[i];
		eaPush(&sls.list, al->name);
	}
	ClientCmd_aiAnimListDebugSetlistClient(e, &sls);
	eaDestroy(&sls.list);
}
