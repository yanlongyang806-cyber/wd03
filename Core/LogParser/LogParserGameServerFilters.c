#include "LogParser.h"
#include "StringUtil.h"
#include "LogParser_h_ast.h"
#include "StringCache.h"
#include "../../libs/worldlib/pub/WorldGrid.h"
#include "MapDescription_h_ast.h"
#include "GlobalEnums_h_ast.h"

LogParserGameServerFilter *FindGameServerFilter(char *pFilterName)
{
	int iIndex = eaIndexedFindUsingString(&gStandAloneOptions.ppGameServerFilterCategories, pFilterName);

	if (iIndex == -1)
	{
		return NULL;
	}

	return gStandAloneOptions.ppGameServerFilterCategories[iIndex];

}

LogParserGameServerFilter *FindGameServerExclusionFilter(char *pFilterName)
{
	int iIndex = eaIndexedFindUsingString(&gStandAloneOptions.ppExclusionGameServerFilters, pFilterName);

	if (iIndex == -1)
	{
		return NULL;
	}

	return gStandAloneOptions.ppExclusionGameServerFilters[iIndex];

}


LogParserGameServerFilter *CreateGameServerFilter(char *pFilterName)
{
	LogParserGameServerFilter *pFilter;

	if (StringIsAllWhiteSpace(pFilterName))
	{
		return NULL;
	}

	pFilter = StructCreate(parse_LogParserGameServerFilter);
	pFilter->pFilterName = allocAddString(pFilterName);
	eaPush(&gStandAloneOptions.ppGameServerFilterCategories, pFilter);

	return pFilter;
}



AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char *AddGameServerFilter(char *pFilterName)
{
	LogParserGameServerFilter *pFilter = FindGameServerFilter(pFilterName);

	if (pFilter)
	{
		return "Already existing filter name";
	}

	if (strStartsWith(pFilterName, EXCFILTER_NAME_PREFIX))
	{
		return "Invalid filter name";
	}		

	pFilter = CreateGameServerFilter(pFilterName);

	SaveStandAloneOptions(NULL);
	return "New filter added";
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void AddGameServerExclusionFilter(void)
{
	int i;
	int iID = 0;
	LogParserGameServerFilter * pFilter = StructCreate(parse_LogParserGameServerFilter);
	char tempName[64];


	for (i=0; i < eaSize(&gStandAloneOptions.ppExclusionGameServerFilters); i++)
	{
		int iThatID = atoi(gStandAloneOptions.ppExclusionGameServerFilters[i]->pFilterName + strlen(EXCFILTER_NAME_PREFIX));
		if (iThatID >= iID)
		{
			iID = iThatID + 1;
		}
	}

	sprintf(tempName, "%s%d", EXCFILTER_NAME_PREFIX, iID);
	pFilter->pFilterName = allocAddString(tempName);

	eaPush(&gStandAloneOptions.ppExclusionGameServerFilters, pFilter);

	SaveStandAloneOptions(NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void RemoveGameServerFilter(char *pFilterName)
{
	int iIndex = eaIndexedFindUsingString(&gStandAloneOptions.ppGameServerFilterCategories, pFilterName);

	if (iIndex == -1)
	{
		iIndex = eaIndexedFindUsingString(&gStandAloneOptions.ppExclusionGameServerFilters, pFilterName);

		if (iIndex == -1)
		{
			return;
		}
		StructDestroy(parse_LogParserGameServerFilter, gStandAloneOptions.ppExclusionGameServerFilters[iIndex]);
		eaRemove(&gStandAloneOptions.ppExclusionGameServerFilters, iIndex);

	}
	else
	{
		StructDestroy(parse_LogParserGameServerFilter, gStandAloneOptions.ppGameServerFilterCategories[iIndex]);
		eaRemove(&gStandAloneOptions.ppGameServerFilterCategories, iIndex);
	}
	SaveStandAloneOptions(NULL);
}
AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void GameServerLog_AddMapNameRestriction(char *pFilterName, char *pMapName)
{
	LogParserGameServerFilter *pFilter = FindGameServerFilter(pFilterName);
	LogParserGameServerFilterMapName *pMapNameFilter;
	

	if (!pFilter)
	{
		pFilter = FindGameServerExclusionFilter(pFilterName);
	}

	if (!pFilter)
	{
		return;
	}

	pMapNameFilter = StructCreate(parse_LogParserGameServerFilterMapName);
	if (stricmp(pMapName, "ALL") == 0)
	{
		pMapNameFilter->pMapName = strdup(pMapName);
	}
	else
	{
		pMapNameFilter->pMapName = strdup(worldGetZoneMapFilenameByPublicName(pMapName));
	}	
	pMapNameFilter->pParentName = pFilter->pFilterName;

	eaPush(&pFilter->ppMapNames, pMapNameFilter);

	SaveStandAloneOptions(NULL);

}


AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void GameServerLog_AddMachineNameRestriction(char *pFilterName, char *pMachineName)
{
	LogParserGameServerFilter *pFilter = FindGameServerFilter(pFilterName);
	LogParserGameServerFilterMachineName *pMachineNameFilter;
	

	if (!pFilter)
	{
		pFilter = FindGameServerExclusionFilter(pFilterName);
	}

	if (!pFilter)
	{
		return;
	}

	pMachineNameFilter = StructCreate(parse_LogParserGameServerFilterMachineName);
	
	pMachineNameFilter->pMachineName = strdup(pMachineName);
	
	pMachineNameFilter->pParentName = pFilter->pFilterName;

	eaPush(&pFilter->ppMachineNames, pMachineNameFilter);

	SaveStandAloneOptions(NULL);

}


AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void GameServerLog_RemoveMapNameRestriction(char *pFilterName, char *pMapName)
{
	LogParserGameServerFilter *pFilter = FindGameServerFilter(pFilterName);

	int i;

	if (!pFilter)
	{
		pFilter = FindGameServerExclusionFilter(pFilterName);
	}

	if (!pFilter)
	{
		return;
	}

	for (i=0; i < eaSize(&pFilter->ppMapNames); i++)
	{
		if (stricmp(pFilter->ppMapNames[i]->pMapName, pMapName) == 0)
		{
			StructDestroy(parse_LogParserGameServerFilterMapName, pFilter->ppMapNames[i]);
			eaRemove(&pFilter->ppMapNames, i);
			return;
		}
	}

	SaveStandAloneOptions(NULL);
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void GameServerLog_RemoveMachineNameRestriction(char *pFilterName, char *pMachineName)
{
	LogParserGameServerFilter *pFilter = FindGameServerFilter(pFilterName);

	int i;

	if (!pFilter)
	{
		pFilter = FindGameServerExclusionFilter(pFilterName);
	}

	if (!pFilter)
	{
		return;
	}

	for (i=0; i < eaSize(&pFilter->ppMachineNames); i++)
	{
		if (stricmp(pFilter->ppMachineNames[i]->pMachineName, pMachineName) == 0)
		{
			StructDestroy(parse_LogParserGameServerFilterMachineName, pFilter->ppMachineNames[i]);
			eaRemove(&pFilter->ppMachineNames, i);
			return;
		}
	}

	SaveStandAloneOptions(NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void GameServerLog_SetMapTypeRestriction(char *pFilterName, char *pTypeName)
{
	LogParserGameServerFilter *pFilter = FindGameServerFilter(pFilterName);

	if (!pFilter)
	{
		pFilter = FindGameServerExclusionFilter(pFilterName);
	}

	if (!pFilter)
	{
		return;
	}

	pFilter->eMapType = StaticDefineIntGetInt(ZoneMapTypeEnum, pTypeName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void GameServerLog_SetPlayerNumRestriction(char *pFilterName, int iMinPlayers, int iMaxPlayers)
{
	LogParserGameServerFilter *pFilter = FindGameServerFilter(pFilterName);

	if (!pFilter)
	{
		pFilter = FindGameServerExclusionFilter(pFilterName);
	}

	if (!pFilter)
	{
		return;
	}

	pFilter->iMinPlayers = iMinPlayers;
	pFilter->iMaxPlayers = iMaxPlayers;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void GameServerLog_SetEntityNumRestriction(char *pFilterName, int iMinEntities, int iMaxEntities)
{
	LogParserGameServerFilter *pFilter = FindGameServerFilter(pFilterName);

	if (!pFilter)
	{
		pFilter = FindGameServerExclusionFilter(pFilterName);
	}

	if (!pFilter)
	{
		return;
	}

	pFilter->iMinEntities = iMinEntities;
	pFilter->iMaxEntities = iMaxEntities;
}

