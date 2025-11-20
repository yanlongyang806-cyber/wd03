#include "ControllerStartupSupport.h"
#include "ControllerStartupSupport_h_ast.h"
#include "earray.h"
#include "estring.h"
#include "StringUtil.h"
#include "error.h"
#include "GlobalTypes.h"



MachineInfoForShardSetup *FindOrCreateMachineInfo(MachineInfoForShardSetupList* pList, char *pMachineName)
{
	int i;
	MachineInfoForShardSetup *pMachineInfo;

	for (i = 0; i < eaSize(&pList->ppMachines); i++)
	{
		if (stricmp_safe(pList->ppMachines[i]->pMachineName, pMachineName) == 0)
		{
			return pList->ppMachines[i];
		}
	}

	pMachineInfo = StructCreate(parse_MachineInfoForShardSetup);
	estrCopy2(&pMachineInfo->pMachineName, pMachineName);
	eaPush(&pList->ppMachines, pMachineInfo);
	return pMachineInfo;
}

MachineInfoServerLaunchSettings *FindServerSetting(MachineInfoForShardSetup* pMachineInfo, GlobalType eServerType)
{
	int i;

	for (i = 0; i < eaSize(&pMachineInfo->ppSettings); i++)
	{
		if (pMachineInfo->ppSettings[i]->eServerType == eServerType)
		{
			return pMachineInfo->ppSettings[i];
		}
	}

	return NULL;
}


bool AddMachineAndServerForLaunching(MachineInfoForShardSetupList* pList, GlobalType eServerType, char *pMachineName)
{
	MachineInfoServerLaunchSettings *pServerSetting;

	MachineInfoForShardSetup *pMachineInfo = FindOrCreateMachineInfo(pList, pMachineName);
	if (pMachineInfo)
	{
		pServerSetting = FindServerSetting(pMachineInfo, eServerType);
		if (pServerSetting)
		{
			return false;
		}
	}

	pServerSetting = StructCreate(parse_MachineInfoServerLaunchSettings);
	pServerSetting->eServerType = eServerType;
	pServerSetting->eSetting = CAN_LAUNCH_SPECIFIED;
	eaPush(&pMachineInfo->ppSettings, pServerSetting);
	return true;
}


bool ProcessGenericArg(MachineInfoForShardSetupList* pList, ShardSetupGenericArg *pGenericArg)
{
	static char *pLine = NULL;
	static char **ppFirstHalf = NULL;
	static char **ppSecondHalf = NULL;
	int i;
	int j;
	char *pColon;

	estrClear(&pLine);
	eaDestroyEx(&ppFirstHalf, NULL);
	eaDestroyEx(&ppSecondHalf, NULL);

	for (i = 0; i < eaSize(&pGenericArg->ppInternalStrs); i++)
	{
		estrConcatf(&pLine, "%s ", pGenericArg->ppInternalStrs[i]);
	}

	pColon = strchr(pLine, ':');
	if (!pColon)
	{
		ErrorFilenamef(pGenericArg->pCurrentFile, "found no Colon in line %d: \"%s\"", pGenericArg->iLineNum, pLine);
		return false;
	}

	if (strchr(pColon + 1, ':'))
	{
		ErrorFilenamef(pGenericArg->pCurrentFile, "Found two colons in line %d: \"%s\"", pGenericArg->iLineNum, pLine);
		return false;
	}

	*pColon = 0;
	DivideString(pLine, " ,", &ppFirstHalf, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	if (!eaSize(&ppFirstHalf))
	{
		ErrorFilenamef(pGenericArg->pCurrentFile, "Found no server types in line %d: \"%s\"", pGenericArg->iLineNum, pLine);
		return false;
	}

	DivideString(pColon + 1, " ,", &ppSecondHalf, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
	if (!eaSize(&ppSecondHalf))
	{
		ErrorFilenamef(pGenericArg->pCurrentFile, "Found no server types in line %d: \"%s\"", pGenericArg->iLineNum, pLine);
		return false;
	}

	for (i = 0; i < eaSize(&ppFirstHalf); i++)
	{
		if (!NameToGlobalType(ppFirstHalf[i]))
		{
			ErrorFilenamef(pGenericArg->pCurrentFile, "Unrecognized server type %s in line %d: \"%s\"", ppFirstHalf[i], pGenericArg->iLineNum, pLine);
			return false;
		}
	}

	for (i = 0; i < eaSize(&ppFirstHalf); i++)
	{
		for (j = 0; j < eaSize(&ppSecondHalf); j++)
		{
			if (!AddMachineAndServerForLaunching(pList, NameToGlobalType(ppFirstHalf[i]), ppSecondHalf[j]))
			{
				ErrorFilenamef(pGenericArg->pCurrentFile, "Unable to tell %s to launch %s in line %d: \"%s\"", ppSecondHalf[j], ppFirstHalf[i], pGenericArg->iLineNum, pLine);
				return false;
			}
		}
	}

	return true;
}

AUTO_FIXUPFUNC;
TextParserResult fixupMachineInfoForShardSetupList(MachineInfoForShardSetupList* pList, enumTextParserFixupType eType, void *pExtraData)
{
	int i;
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		for (i = 0; i < eaSize(&pList->ppGenericArgs); i++)
		{
			ShardSetupGenericArg *pGenericArg = pList->ppGenericArgs[i];
			if (!ProcessGenericArg(pList, pGenericArg))
			{
				return PARSERESULT_INVALID;
			}
		}
		break;
	}

	return PARSERESULT_SUCCESS;
}


























#include "ControllerStartupSupport_h_ast.c"
