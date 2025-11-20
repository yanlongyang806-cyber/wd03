#include "AutoSettings.h"
#include "EString.h"
#include "earray.h"
#include "textparser.h"
#include "AutoSEttings_h_ast.h"
#include "cmdParse.h"
#include "StringCache.h"
#include "GlobalTypes.h"
#include "ServerLib.h"
#include "ControllerLink.h"
#include "Alerts.h"
#include "file.h"
#include "NotesServerComm.h"
#include "StashTable.h"
#include "logging.h"

static char *spDumpAutoSettingsAndQuitFileName = NULL;
AUTO_CMD_ESTRING(spDumpAutoSettingsAndQuitFileName, DumpAutoSettingsAndQuitFileName) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;


AUTO_FIXUPFUNC;
TextParserResult fixupAutoSetting_SingleSetting(AutoSetting_SingleSetting* pSetting, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:
	
		{
			char *pTitle = NULL;
			char *pNoteName = NULL;

			SAFE_FREE(pSetting->pNotes);

			estrPrintf(&pNoteName, "AutoSettings.%s.%s", pSetting->pCategory, pSetting->pCmdName);
			
			estrPrintf(&pTitle, "Auto_setting %s (category %s)",
				pSetting->pCmdName, pSetting->pCategory);
			

			pSetting->pNotes = strdup(NotesServer_GetLinkToNoteServerMonPage(pNoteName, pTitle, true, false));

			estrDestroy(&pTitle);
			estrDestroy(&pNoteName);
		}
		
		break;
	}

	return 1;
}


void AutoSetting_Notify(FORMAT_STR const char *pFmt, ...)
{
	char *pFullString = NULL;
	estrGetVarArgs(&pFullString, pFmt);

	TriggerAlertDeferred("AUTO_SETTINGS_ERROR", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, "Auto setting error: %s", pFullString);
	estrDestroy(&pFullString);


}

bool AutoSetting_CommandIsAutoSettingCommand(Cmd *pCmd, char **ppCategory)
{
	if (pCmd->categories && strstri(pCmd->categories, " " AUTO_SETTING_CATEGORY_PREFIX))
	{
		estrCopy2(ppCategory, pCmd->categories);
		estrTrimLeadingAndTrailingWhitespace(ppCategory);
		if (strchr(*ppCategory, ' '))
		{
			assertmsgf(0, "Command %s has a AutoSetting category, and also other categories. This is illegal. Categories: %s", pCmd->name, pCmd->categories);
		}
		estrRemove(ppCategory, 0, 6);
		return true;
	}

	return false;
}



void AutoSetting_GetCmdTypeAndValueString(const Cmd *pCmd, AutoSettingType *pOutType, char **ppOutValString)
{
	assertmsgf(pCmd->iNumLogicalArgs == 1 && pCmd->iNumReadArgs == 1, "A ControllerAutoSetting command must have exactly one arg. %s has %d", pCmd->name, pCmd->iNumLogicalArgs);
	assertmsgf(pCmd->data[0].ptr, "A ControllerAutoSetting command must have a data pointer. %s doesn't have one", pCmd->name);

	switch (pCmd->data[0].type)
	{
	case MULTI_STRING:
		{
			*pOutType = ASTYPE_STRING;

			if (pCmd->data[0].data_size == MAGIC_CMDPARSE_STRING_SIZE_ESTRING)
			{
				estrCopy(ppOutValString, (char**)pCmd->data[0].ptr);
			}
			else
			{
				if (pCmd->data[0].ptr)
				{
					estrCopy2(ppOutValString, (char*)pCmd->data[0].ptr);
				}
				else
				{
					estrDestroy(ppOutValString);
				}
			}
		}
		break;

	case MULTI_INT:
		{
			S64 iVal;
			*pOutType = ASTYPE_INT;
			
			switch (pCmd->data[0].data_size)
			{
			xcase sizeof(U8):
				iVal = *((S8*)pCmd->data[0].ptr);
			xcase sizeof(U16):
				iVal = *((S16*)pCmd->data[0].ptr);
			xcase sizeof(U32):
				iVal = *((S32*)pCmd->data[0].ptr);
			xcase sizeof(U64):
				iVal = *((S64*)pCmd->data[0].ptr);
			xdefault:
				assertmsgf(0, "Command %s has int type but unknown data size %d", pCmd->name, pCmd->data[0].data_size);
			}

			estrPrintf(ppOutValString, "%"FORM_LL"d", iVal);
		}
		break;
	
	case MULTI_FLOAT:
		assertmsgf(pCmd->data[0].data_size == sizeof(F32), "Command %s has a float arg of an known size", pCmd->name);
		*pOutType = ASTYPE_FLOAT;
		estrPrintf(ppOutValString, "%f", (float)(*((float*)pCmd->data[0].ptr)));
		break;

	default:
		assertmsgf(0, "ControllerAutoSetting command %s has unknown type %s. Only int, float, string are supported",
			pCmd->name, MultiValTypeToReadableString(pCmd->data[0].type));
	}
}



AutoSetting_ForDataFile *AutoSetting_SettingForDataFileFromCmd(Cmd *pCmd, char *pCategoryString, bool bEarly)
{
	AutoSetting_ForDataFile *pSetting = StructCreate(parse_AutoSetting_ForDataFile);
	pSetting->pName = allocAddString(pCmd->name);
	pSetting->pCategory = allocAddString(pCategoryString);
	pSetting->pComment = strdup(pCmd->comment);
	pSetting->bEarly = bEarly;
	AutoSetting_GetCmdTypeAndValueString(pCmd, &pSetting->eType, &pSetting->pBuiltInVal);

	return pSetting;
}

bool AutoSetting_CmdGlobalTypesIncludeMe(Cmd *pCmd)
{
	if (ea32Find(&pCmd->pAutoSettingGlobalTypes, GLOBALTYPE_ALL) != -1)
	{
		return true;
	}

	if (ea32Find(&pCmd->pAutoSettingGlobalTypes, GetAppGlobalType()) != -1)
	{
		return true;
	}

	if (IsAppServerBasedType() && ea32Find(&pCmd->pAutoSettingGlobalTypes, GLOBALTYPE_APPSERVER) != -1)
	{
		return true;
	}

	return false;
}

void AutoSetting_GetAllFromCmdListForDumping(AutoSetting_ForDataFile_List *pOutList, CmdList *pCmdList, bool bEarly)
{

	char *pCategoryString = NULL;

	FOR_EACH_IN_STASHTABLE(pCmdList->sCmdsByName, Cmd, pCmd)
	{
		if (AutoSetting_CommandIsAutoSettingCommand(pCmd, &pCategoryString))
		{
			if (AutoSetting_CmdGlobalTypesIncludeMe(pCmd))
			{
				eaPush(&pOutList->ppSettings, AutoSetting_SettingForDataFileFromCmd(pCmd, pCategoryString, bEarly));
			}
		}
	}
	FOR_EACH_END;
}




AUTO_RUN_LATE;
void DumpAutoSettingsAndQuit(void)
{
	AutoSetting_ForDataFile_List *pList = NULL;

	if (!spDumpAutoSettingsAndQuitFileName)
	{
		return;
	}

	mkdirtree_const(spDumpAutoSettingsAndQuitFileName);

	pList = StructCreate(parse_AutoSetting_ForDataFile_List);

	AutoSetting_GetAllFromCmdListForDumping(pList, &gEarlyCmdList, true);
	AutoSetting_GetAllFromCmdListForDumping(pList, &gGlobalCmdList, false);

	ParserWriteTextFile(spDumpAutoSettingsAndQuitFileName, parse_AutoSetting_ForDataFile_List, pList, 0, 0);

	StructDestroy(parse_AutoSetting_ForDataFile_List, pList);

	svrExit(0);

}

void AutoSetting_CommandStringFromController(char *pSuperEscapedCommandString, bool bEarly)
{
	CmdContext cmd_context = {0};
	int iResult;
	char *pFullCommandString = NULL;

	cmd_context.eHowCalled = CMD_CONTEXT_HOWCALLED_CONTROLLER_SETS_AUTO_SETTING;
	cmd_context.access_level = 10;
	cmd_context.flags |= CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED;

	estrSuperUnescapeString(&pFullCommandString, pSuperEscapedCommandString);

	iResult = cmdParseAndExecute(bEarly ? &gEarlyCmdList : &gGlobalCmdList, pFullCommandString, &cmd_context);

	if (!iResult)
	{
		AutoSetting_Notify("Uanble to execute AutoSetting command %s, something is fairly seriously wrong", pFullCommandString);
	}
	estrDestroy(&pFullCommandString);

}

//the name of this MUST be AUTOSETTING_CMDLINECOMMAND_PREFIX AUTOSETTING_CONSTSTRING_EARLY
AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void AutoSettingCmdLineEarly(CmdContext *pContext, char *pSuperEscapedCommandString)
{
	if (pContext->eHowCalled != CMD_CONTEXT_HOWCALLED_EARLY_COMMANDLINE)
	{
		return;
	}

	AutoSetting_CommandStringFromController(pSuperEscapedCommandString, true);
}

//the name of this MUST be AUTOSETTING_CMDLINECOMMAND_PREFIX AUTOSETTING_CONSTSTRING_NORMAL
AUTO_COMMAND ACMD_COMMANDLINE;
void AutoSettingCmdLineNormal(CmdContext *pContext, char *pSuperEscapedCommandString)
{
	if (pContext->eHowCalled != CMD_CONTEXT_HOWCALLED_COMMANDLINE)
	{
		return;
	}

	AutoSetting_CommandStringFromController(pSuperEscapedCommandString, false);
}

//use this to set an auto setting crudely, as if it were just a normal command, use very sparingly and only in weird
//cases like trying to set an auto_setting on a shard-level logparser, etc.
AUTO_COMMAND;
char *EmergencyAutoSettingOverride(CmdContext *pContext, ACMD_SENTENCE pCommandString)
{
	CmdContext cmd_context = {0};
	int iResult;
	char *pFullCommandString = NULL;
	
	log_printf(LOG_MISC, "EmergencyAutoSettingOverride being called via %s(%s) to set %s",
		GetContextHowString(pContext), pContext->pAuthNameAndIP, pCommandString);

	cmd_context.eHowCalled = CMD_CONTEXT_HOWCALLED_EMERGENCY_AUTO_SETTING_OVERRIDE;
	cmd_context.access_level = pContext->access_level;

	iResult = cmdParseAndExecute( &gGlobalCmdList, pCommandString, &cmd_context);

	if (!iResult)
	{
		iResult = cmdParseAndExecute( &gEarlyCmdList, pCommandString, &cmd_context);

		if (!iResult)
		{
			return "Execution failed";
		}
	}

	return "Execution succeeded";
}

AUTO_COMMAND_REMOTE;
void SendAutoSettingFromController(char *pCommandString)
{
	//if we have a link to the controller, we will get auto settings from it, don't want to use both due to 
	//off off chance of synchronization issues
	if (GetControllerLink())
	{
		return;
	}

	if (strStartsWith(pCommandString, AUTOSETTING_CONSTSTRING_EARLY_SPACE))
	{
		AutoSetting_CommandStringFromController(pCommandString + strlen(AUTOSETTING_CONSTSTRING_EARLY_SPACE), true);
	}
	else if (strStartsWith(pCommandString, AUTOSETTING_CONSTSTRING_NORMAL_SPACE))
	{
		AutoSetting_CommandStringFromController(pCommandString + strlen(AUTOSETTING_CONSTSTRING_NORMAL_SPACE), false);
	}
	else
	{
		AutoSetting_Notify("Unable to process AutoSetting string we got from controller: %s", pCommandString);
	}
}

void OVERRIDE_LATELINK_AutoSetting_PacketOfCommandsFromController(Packet *pPack)
{
	while (1)
	{
		char *pCommandString = pktGetStringTemp(pPack);

		if (!pCommandString[0])
		{
			return;
		}

		if (strStartsWith(pCommandString, AUTOSETTING_CONSTSTRING_EARLY_SPACE))
		{
			AutoSetting_CommandStringFromController(pCommandString + strlen(AUTOSETTING_CONSTSTRING_EARLY_SPACE), true);
		}
		else if (strStartsWith(pCommandString, AUTOSETTING_CONSTSTRING_NORMAL_SPACE))
		{
			AutoSetting_CommandStringFromController(pCommandString + strlen(AUTOSETTING_CONSTSTRING_NORMAL_SPACE), false);
		}
		else
		{
			AutoSetting_Notify("Unable to process AutoSetting string we got from controller: %s", pCommandString);
		}
	}
}

void OVERRIDE_LATELINK_AutoSetting_CmdWasCalled(const Cmd *pCmd, enumCmdContextHowCalled eHow, const char *pFullStr)
{
	AutoSetting_Notify("AUTO_SETTING command %s has been called in a nonauthorized fashion (%s), with full string \"%s\". This is being overriden and ignored, but should NOT be done",
		pCmd->name, StaticDefineIntRevLookup(enumCmdContextHowCalledEnum, eHow), pFullStr);
}

void AutoSetting_SetAllCommandFlags(CmdList *pCmdList)
{
	char *pCategoryString = NULL;

	FOR_EACH_IN_STASHTABLE(pCmdList->sCmdsByName, Cmd, pCmd)
	{
		if (AutoSetting_CommandIsAutoSettingCommand(pCmd, &pCategoryString))
		{
			pCmd->flags |= CMDF_AUTOSETTING_NONCONTROLLER;
		}
	}
	FOR_EACH_END;
}



AUTO_RUN_POSTINTERNAL;
void AutoSettings_InitSystem(void)
{
	char isContinuousBuilder[3];
	char systemIsActive[3];

	if (GetAppGlobalType() == GLOBALTYPE_CONTROLLER)
	{
		return;
	}

	ParseCommandOutOfCommandLine("IsContinuousBuilder", isContinuousBuilder);
	if (isContinuousBuilder[0] == '1')
	{
		return;
	}

	ParseCommandOutOfCommandLine("UseAutoSettings", systemIsActive);
  	if (!(isProductionMode() || systemIsActive[0] == '1'))
	{
		return;
	}

	AutoSetting_SetAllCommandFlags(&gEarlyCmdList);
	AutoSetting_SetAllCommandFlags(&gGlobalCmdList);

}
	



#include "AutoSEttings_h_ast.c"
