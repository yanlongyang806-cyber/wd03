#include "LogParser.h"
#include "StringUtil.h"
#include "LogParser_h_ast.h"
#include "StringCache.h"
#include "../../libs/worldlib/pub/WorldGrid.h"

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void RemovePlayerFilter(char *pFilterName)
{
	int iIndex = eaIndexedFindUsingString(&gStandAloneOptions.ppPlayerFilterCategories, pFilterName);

	if (iIndex == -1)
	{
		iIndex = eaIndexedFindUsingString(&gStandAloneOptions.ppExclusionPlayerFilters, pFilterName);

		if (iIndex == -1)
		{
			return;
		}
		StructDestroy(parse_LogParserPlayerLogFilter, gStandAloneOptions.ppExclusionPlayerFilters[iIndex]);
		eaRemove(&gStandAloneOptions.ppExclusionPlayerFilters, iIndex);

	}
	else
	{
		StructDestroy(parse_LogParserPlayerLogFilter, gStandAloneOptions.ppPlayerFilterCategories[iIndex]);
		eaRemove(&gStandAloneOptions.ppPlayerFilterCategories, iIndex);
	}
	SaveStandAloneOptions(NULL);
}




LogParserPlayerLogFilter *FindPlayerFilter(char *pFilterName)
{
	int iIndex = eaIndexedFindUsingString(&gStandAloneOptions.ppPlayerFilterCategories, pFilterName);

	if (iIndex == -1)
	{
		return NULL;
	}

	return gStandAloneOptions.ppPlayerFilterCategories[iIndex];

}

LogParserPlayerLogFilter *FindPlayerExclusionFilter(char *pFilterName)
{
	int iIndex = eaIndexedFindUsingString(&gStandAloneOptions.ppExclusionPlayerFilters, pFilterName);

	if (iIndex == -1)
	{
		return NULL;
	}

	return gStandAloneOptions.ppExclusionPlayerFilters[iIndex];

}


LogParserPlayerLogFilter *CreatePlayerFilter(char *pFilterName)
{
	LogParserPlayerLogFilter *pFilter;

	if (StringIsAllWhiteSpace(pFilterName))
	{
		return NULL;
	}

	pFilter = StructCreate(parse_LogParserPlayerLogFilter);
	pFilter->pFilterName = allocAddString(pFilterName);
	eaPush(&gStandAloneOptions.ppPlayerFilterCategories, pFilter);

	return pFilter;
}




/*too many args to actually split them up, so put all the ints into a big string*/
AUTO_COMMAND ACMD_ACCESSLEVEL(0);
char *AddPlayerFilter(char *pFilterName, char *pIntArgs)
{
	LogParserPlayerLogFilter *pFilter = FindPlayerFilter(pFilterName);
	int iMinLevel, iMaxLevel, iUseAccessLevel, iMinAccessLevel, iMaxAccessLevel, iBalanced, iOffense, iDefense, iSupport, iMinTeamSize, iMaxTeamSize;


	if (pFilter)
	{
		return "Already existing filter name";
	}

	if (strStartsWith(pFilterName, EXCFILTER_NAME_PREFIX))
	{
		return "Invalid filter name";
	}

	pFilter = CreatePlayerFilter(pFilterName);

	if (!pFilter)
	{
		return "Invalid filter name";
	}

	sscanf(pIntArgs, "%d %d %d %d %d %d %d %d %d %d %d", &iMinLevel, &iMaxLevel, &iUseAccessLevel, &iMinAccessLevel, 
		&iMaxAccessLevel, &iBalanced, &iOffense, &iDefense, &iSupport, &iMinTeamSize, &iMaxTeamSize);

	pFilter->iMinLevel = iMinLevel;
	pFilter->iMaxLevel = iMaxLevel;

	pFilter->iMinTeamSize = iMinTeamSize;
	pFilter->iMaxTeamSize = iMaxTeamSize;

	if (iUseAccessLevel)
	{
		pFilter->iMinAccessLevel = iMinAccessLevel;
		pFilter->iMaxAccessLevel = iMaxAccessLevel;
	}
	else
	{
		pFilter->iMinAccessLevel = 0;
		pFilter->iMaxAccessLevel = ACCESS_INTERNAL;
	}

	eaDestroy(&pFilter->ppRoleNames);

	if (iBalanced)
	{
		eaPush(&pFilter->ppRoleNames, (char*)allocAddCaseSensitiveString("Balanced"));
	}

	if (iOffense)
	{
		eaPush(&pFilter->ppRoleNames, (char*)allocAddCaseSensitiveString("Offense"));
	}

	if (iDefense)
	{
		eaPush(&pFilter->ppRoleNames, (char*)allocAddCaseSensitiveString("Defense"));
	}

	if (iSupport)
	{
		eaPush(&pFilter->ppRoleNames, (char*)allocAddCaseSensitiveString("Support"));
	}

	SaveStandAloneOptions(NULL);
	return "New filter added";
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void AddPlayerExclusionFilter(int iMinLevel, int iMaxLevel, bool bUseAccessLevel, int iMinAccessLevel, int iMaxAccessLevel, int bBalanced, int bOffense, int bDefense, int bSupport)
{
	int i;
	int iID = 0;
	LogParserPlayerLogFilter * pFilter = StructCreate(parse_LogParserPlayerLogFilter);
	char tempName[64];


	for (i=0; i < eaSize(&gStandAloneOptions.ppExclusionPlayerFilters); i++)
	{
		int iThatID = atoi(gStandAloneOptions.ppExclusionPlayerFilters[i]->pFilterName + strlen(EXCFILTER_NAME_PREFIX));
		if (iThatID >= iID)
		{
			iID = iThatID + 1;
		}
	}

	sprintf(tempName, "%s%d", EXCFILTER_NAME_PREFIX, iID);
	pFilter->pFilterName = allocAddString(tempName);


	pFilter->iMinLevel = iMinLevel;
	pFilter->iMaxLevel = iMaxLevel;

	if (bUseAccessLevel)
	{
		pFilter->iMinAccessLevel = iMinAccessLevel;
		pFilter->iMaxAccessLevel = iMaxAccessLevel;
	}
	else
	{
		pFilter->iMinAccessLevel = 0;
		pFilter->iMaxAccessLevel = 11;
	}

	eaDestroy(&pFilter->ppRoleNames);

	if (bBalanced)
	{
		eaPush(&pFilter->ppRoleNames, (char*)allocAddCaseSensitiveString("Balanced"));
	}

	if (bOffense)
	{
		eaPush(&pFilter->ppRoleNames, (char*)allocAddCaseSensitiveString("Offense"));
	}

	if (bDefense)
	{
		eaPush(&pFilter->ppRoleNames, (char*)allocAddCaseSensitiveString("Defense"));
	}

	if (bSupport)
	{
		eaPush(&pFilter->ppRoleNames, (char*)allocAddCaseSensitiveString("Support"));
	}

	eaPush(&gStandAloneOptions.ppExclusionPlayerFilters, pFilter);

	SaveStandAloneOptions(NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void PlayerLog_AddPlayerRestriction(char *pFilterName, char *pPlayerName)
{
	LogParserPlayerLogFilter *pFilter = FindPlayerFilter(pFilterName);
	LogParserPlayerLogFilterPlayerToRequire *pPlayerFilter;
	

	if (!pFilter)
	{
		pFilter = FindPlayerExclusionFilter(pFilterName);
	}

	if (!pFilter)
	{
		return;
	}

	pPlayerFilter = StructCreate(parse_LogParserPlayerLogFilterPlayerToRequire);
	pPlayerFilter->pPlayerName = strdup(pPlayerName);
	pPlayerFilter->pParentName = pFilter->pFilterName;

	eaPush(&pFilter->ppPlayersToRequire, pPlayerFilter);

	SaveStandAloneOptions(NULL);

}


AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void PlayerLog_AddMapNameRestriction(char *pFilterName, char *pMapName)
{
	LogParserPlayerLogFilter *pFilter = FindPlayerFilter(pFilterName);
	LogParserPlayerLogFilterMapNameToRequire *pMapNameFilter;
	

	if (!pFilter)
	{
		pFilter = FindPlayerExclusionFilter(pFilterName);
	}

	if (!pFilter)
	{
		return;
	}

	pMapNameFilter = StructCreate(parse_LogParserPlayerLogFilterMapNameToRequire);
	if (stricmp(pMapName, "ALL") == 0)
	{
		pMapNameFilter->pMapName = strdup(pMapName);
	}
	else
	{
		pMapNameFilter->pMapName = strdup(worldGetZoneMapFilenameByPublicName(pMapName));
	}
	pMapNameFilter->pParentName = pFilter->pFilterName;

	eaPush(&pFilter->ppMapNamesToRequire, pMapNameFilter);

	SaveStandAloneOptions(NULL);

}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void PlayerLog_AddPowerRestriction(char *pFilterName, char *pPowerName)
{
	LogParserPlayerLogFilter *pFilter = FindPlayerFilter(pFilterName);
	LogParserPlayerLogFilterPowerToRequire *pPowerFilter;
	

	if (!pFilter)
	{
		pFilter = FindPlayerExclusionFilter(pFilterName);
	}

	if (!pFilter)
	{
		return;
	}
	
	pPowerFilter = StructCreate(parse_LogParserPlayerLogFilterPowerToRequire);
	pPowerFilter->pPowerName = strdup(pPowerName);
	pPowerFilter->pParentName = pFilter->pFilterName;

	eaPush(&pFilter->ppPowersToRequire, pPowerFilter);

	SaveStandAloneOptions(NULL);

}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void PlayerLog_RemovePowerRestriction(char *pFilterName, char *pPowerName)
{
	LogParserPlayerLogFilter *pFilter = FindPlayerFilter(pFilterName);

	int i;

	if (!pFilter)
	{
		pFilter = FindPlayerExclusionFilter(pFilterName);
	}

	if (!pFilter)
	{
		return;
	}

	for (i=0; i < eaSize(&pFilter->ppPowersToRequire); i++)
	{
		if (stricmp(pFilter->ppPowersToRequire[i]->pPowerName, pPowerName) == 0)
		{
			StructDestroy(parse_LogParserPlayerLogFilterPowerToRequire, pFilter->ppPowersToRequire[i]);
			eaRemove(&pFilter->ppPowersToRequire, i);
			return;
		}
	}

	SaveStandAloneOptions(NULL);
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void PlayerLog_RemovePlayerRestriction(char *pFilterName, char *pPlayerName)
{
	LogParserPlayerLogFilter *pFilter = FindPlayerFilter(pFilterName);

	int i;

	if (!pFilter)
	{
		pFilter = FindPlayerExclusionFilter(pFilterName);
	}

	if (!pFilter)
	{
		return;
	}

	for (i=0; i < eaSize(&pFilter->ppPlayersToRequire); i++)
	{
		if (stricmp(pFilter->ppPlayersToRequire[i]->pPlayerName, pPlayerName) == 0)
		{
			StructDestroy(parse_LogParserPlayerLogFilterPlayerToRequire, pFilter->ppPlayersToRequire[i]);
			eaRemove(&pFilter->ppPlayersToRequire, i);
			return;
		}
	}

	SaveStandAloneOptions(NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void PlayerLog_RemoveMapNameRestriction(char *pFilterName, char *pMapName)
{
	LogParserPlayerLogFilter *pFilter = FindPlayerFilter(pFilterName);

	int i;

	if (!pFilter)
	{
		pFilter = FindPlayerExclusionFilter(pFilterName);
	}

	if (!pFilter)
	{
		return;
	}

	for (i=0; i < eaSize(&pFilter->ppMapNamesToRequire); i++)
	{
		if (stricmp(pFilter->ppMapNamesToRequire[i]->pMapName, pMapName) == 0)
		{
			StructDestroy(parse_LogParserPlayerLogFilterMapNameToRequire, pFilter->ppMapNamesToRequire[i]);
			eaRemove(&pFilter->ppMapNamesToRequire, i);
			return;
		}
	}

	SaveStandAloneOptions(NULL);
}



