#include "AppServerLib.h"

#include "objSchema.h"
#include "error.h"
#include "serverlib.h"
#include "autostartupsupport.h"
#include "resourceManager.h"
#include "objTransactions.h"
#include "winInclude.h"
#include "globalTypes.h"

#include "aslResourceDBPub.h"
#include "ResourceDBUtils.h"
#include "ResourceDBUtils_h_ast.h"
#include "aslResourceDB.h"
#include "StructDefines.h"
#include "autogen/AppServerLib_autogen_slowfuncs.h"
#include "floatAverager.h"
#include "EventCountingHeatMap.h"

AUTO_COMMAND_REMOTE ACMD_IFDEF(RESOURCE_DB_SUPPORT);
void RequestResource(GlobalType eServerType, ContainerID iServerID, eResourceDBResourceType eResourceType, char *pResourceName)
{
	EventCounter_ItHappened(pRequestCounter, timeSecondsSince2000());

	verbose_printf("Someone requesting resource %s of type %s\n", pResourceName, StaticDefineIntRevLookup(eResourceDBResourceTypeEnum, eResourceType));
	RequestResource_Internal(eServerType, iServerID, eResourceType, pResourceName, false);
}

AUTO_COMMAND_REMOTE_SLOW(NameSpaceAllResourceList*);
void RequestResourceNames(NameSpaceNamesRequest *pReq, SlowRemoteCommandID iCmdID)
{
	ResourceDBNameSpace *pNameSpace = FindOrCreateNameSpace(pReq->pNameSpaceName);
	NameSpaceAllResourceList emptyList = {0};

	emptyList.pNameSpaceName = pReq->pNameSpaceName;

	if (!pNameSpace)
	{
		Errorf("Someone requested resource names from %s. This is not a valid namespace name",
			pReq->pNameSpaceName);
		SlowRemoteCommandReturn_RequestResourceNames(iCmdID, &emptyList);
		return;
	}

	if (pNameSpace->bLoaded)
	{
		NameSpaceAllResourceList *pRetVal = StructCreate(parse_NameSpaceAllResourceList);
		FillResourceList(pRetVal, pNameSpace, pReq);
		SlowRemoteCommandReturn_RequestResourceNames(iCmdID, pRetVal);
		StructDestroy(parse_NameSpaceAllResourceList, pRetVal);
	}
	else
	{
		pReq->iCmdID = iCmdID;
		eaPush(&pNameSpace->ppNameSpaceReqs, StructClone(parse_NameSpaceNamesRequest, pReq));
	}
}