#include "Controller_AutoSettings.h"
#include "Controller_AutoSettings_c_ast.h"
#include "../common/autogen/ServerLib_autogen_RemoteFuncs.h"

#include "earray.h"
#include "stashtable.h"
#include "estring.h"
#include "Stringcache.h"
#include "resourceInfo.h"
#include "file.h"
#include "textparser.h"
#include "sysUtil.h"
#include "StringUtil.h"
#include "statusReporting.h"
#include "TimedCallback.h"
#include "Controller.h"
#include "ServerLib.h"
#include "ControllerPub_h_ASt.h"
#include "Controller_Utils.h"


#include "AutoSettings_h_ast.h"

static bool sSystemActive[3] = "";
static AutoSetting_SingleSetting **sppSettings = NULL;
StashTable sSettingsByName = NULL;
static char sMainControllerAutoSettingFileName[CRYPTIC_MAX_PATH];
static bool sbNeedToWriteFile = false;

static bool sbNoMoreBufferedWarnings = false;
static char **sppBufferedWarnings = NULL;

static const char *spAllocedFutureName = NULL;
#define FUTURE_SETTING_DEFAULT "(Restoring a future setting to default removes it)"

#define LEV_DISTANCE_FOR_POTENTIAL_MISSPELLING 4

StashTable gControllerAutoSettingCategories = NULL; //because category names are pool_strings, this is all by address

void ControllerAutoSetting_DumpAllToString(char **ppOutString);
void Controller_LoadInOtherServerAutoSettingsAndProcess(void);
char *GetCommandStringForApplyingSettingToOtherServer(AutoSetting_SingleSetting *pSetting);
void ControllerAutoSettings_ClearCommandStringsForServerType(GlobalType eServerType);
bool CreateAndAddFutureSetting(char *pSettingName, char *pSettingValue, char **ppOutError);


static int siNumControllerAutoSettingsErrors = 0;

static char *spAllAutoSettingsErrors = NULL;

AUTO_COMMAND;
int NumAutoSettingErrors(void)
{
	return siNumControllerAutoSettingsErrors;
}

//dummy
AUTO_COMMAND;
void UseAutoSettings(int iUse)
{
}

bool ControllerAutoSetting_SystemIsActive(void)
{
	if (sSystemActive[0] == '0')
	{
		return false;
	}

	return isProductionMode() || sSystemActive[0] == '1';
}

void ControllerAutoSetting_Notify(bool bFatal, FORMAT_STR const char *pFmt, ...)
{
	char *pTempString1 = NULL;
	char *pTempString2 = NULL;
	char *pEscapedString = NULL;

	siNumControllerAutoSettingsErrors++;

	estrGetVarArgs(&pTempString1, pFmt);

	estrCopyWithHTMLEscaping(&pEscapedString, pTempString1, false);
	estrConcatf(&spAllAutoSettingsErrors, "<li>%s</li>\n", pEscapedString);
	estrDestroy(&pEscapedString);

	printfColor(bFatal ? COLOR_BRIGHT | COLOR_RED : COLOR_BRIGHT | COLOR_GREEN | COLOR_RED, "%s%s",
		bFatal ? "FATAL ERROR. Execution halting: " : "", pTempString1);

	estrReplaceOccurrences(&pTempString1, "\"", "\\q");
	
	if (bFatal)
	{
		Controller_MessageBoxError("FATAL_SETTINGS_ERROR", "FATAL ERROR. Execution halting: %s",
			pTempString1);
	}
	else
	{
		Controller_MessageBoxError("SETTINGS_ERROR", "%s",
			pTempString1);
	}

	if (bFatal)
	{
		exit(-1);
	}


	if (StatusReporting_GetState() == STATUSREPORTING_CONNECTED || sbNoMoreBufferedWarnings)
	{
		WARNING_NETOPS_ALERT("SETTINGS_WARNING", "%s", pTempString1);
	}
	else
	{
		eaPush(&sppBufferedWarnings, strdup(pTempString1));
	}

	estrDestroy(&pTempString1);
	estrDestroy(&pTempString2);
}

char *ControllerAutoSetting_GetHTMLCommentCB(ResourceDictionary *pDictionary)
{
	if (estrLength(&spAllAutoSettingsErrors))
	{
		static char *pFullRetString = NULL;
		estrClear(&pFullRetString);
		estrInsertf(&pFullRetString, 0, "<h2>AUTO_SETTING errors:</h2><ul>%s</ul>", spAllAutoSettingsErrors);

		return pFullRetString;
	}
	else
	{
		return "No AUTO_SETTING errors have been encountered. Hurray!";
	}
}

void ControllerAutoSetting_LazyInit(void)
{
	if (!sSettingsByName)
	{
		ControllerAutoSetting_Category *pFutureCategory;

		spAllocedFutureName = allocAddString("__Future");

		sSettingsByName = stashTableCreateWithStringKeys(16, StashDefault);
		resRegisterDictionaryForStashTable("ControllerAutoSettings", RESCATEGORY_SYSTEM, 0, sSettingsByName, parse_AutoSetting_SingleSetting);

		gControllerAutoSettingCategories = stashTableCreateAddress(16);
		resRegisterDictionaryForStashTable("ControllerAutoSetting_Categories", RESCATEGORY_SYSTEM, 0, gControllerAutoSettingCategories, parse_ControllerAutoSetting_Category);
		resDictSetHTMLCommentCallback("ControllerAutoSetting_Categories", ControllerAutoSetting_GetHTMLCommentCB);
		resDictSetHTMLExtraLink("ControllerAutoSetting_Categories", "<a href=\"/viewxpath?xpath=Controller[0].globObj.Controllerautosettings\">All Auto Settings</a>");
		resDictSetHTMLExtraLink("ControllerAutoSettings", "<a href=\"/viewxpath?xpath=Controller[0].globObj.ControllerAutoSetting_Categories\">By Category</a>");



		pFutureCategory = StructCreate(parse_ControllerAutoSetting_Category);
		pFutureCategory->pName = spAllocedFutureName;
		estrCopy2(&pFutureCategory->pComment, "Settings that do not yet exist, but presumably will at some point in the future, being set now to get their value later");
		estrCopy2(&pFutureCategory->pAddSetting, "AddFutureSetting $STRING(Name) $STRING(Value)");
		stashAddPointer(gControllerAutoSettingCategories, pFutureCategory->pName, pFutureCategory, false);
	}
}




AutoSetting_SingleSetting *ControllerAutoSetting_SettingFromCmd(Cmd *pCmd, char *pCategory)
{
	AutoSetting_SingleSetting *pSetting = StructCreate(parse_AutoSetting_SingleSetting);
	pSetting->pCmd = pCmd;
	pSetting->pCmdName = pCmd->name;
	pSetting->pCategory = allocAddString(pCategory);
	pSetting->eOrigin = ASORIGIN_DEFAULT;
	pSetting->pComment = pCmd->comment;
	AutoSetting_GetCmdTypeAndValueString(pCmd, &pSetting->eType, &pSetting->pCurValueString);
	pSetting->pDefaultValueString = strdup(pSetting->pCurValueString);

	return pSetting;
}

AutoSetting_SingleSetting *ControllerAutoSetting_SettingFromDataFileSetting(AutoSetting_ForDataFile *pDataSetting)
{
	AutoSetting_SingleSetting *pSetting = StructCreate(parse_AutoSetting_SingleSetting);
	pSetting->pCmdName = pDataSetting->pName;
	pSetting->pCategory = pDataSetting->pCategory;
	pSetting->eOrigin = ASORIGIN_DEFAULT;
	pSetting->pComment = pDataSetting->pComment;
	pSetting->bEarly = pDataSetting->bEarly;
	pSetting->eType = pDataSetting->eType;
	pSetting->pDefaultValueString = strdup(pDataSetting->pBuiltInVal);
	estrCopy2(&pSetting->pCurValueString, pSetting->pDefaultValueString);

	return pSetting;
}


void ControllerAutoSetting_AddToCategory(AutoSetting_SingleSetting *pSetting)
{
	ControllerAutoSetting_Category *pCategory;

	if (!stashFindPointer(gControllerAutoSettingCategories, pSetting->pCategory, &pCategory))
	{
		pCategory = StructCreate(parse_ControllerAutoSetting_Category);
		pCategory->pName = pSetting->pCategory;
		stashAddPointer(gControllerAutoSettingCategories, pCategory->pName, pCategory, false);
	}

	eaPush(&pCategory->ppSettings, pSetting);
	pCategory->iNumSettings++;

	if (strstr(pSetting->pComment, "__CATEGORY "))
	{
		char **ppLines = NULL;
		int i;
		char *pRebuiltComment = NULL;
		bool bDidSomething = false;
		
		DivideString(pSetting->pComment, "\n", &ppLines, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

		for (i=0; i < eaSize(&ppLines); i++)
		{
			if (strStartsWith(ppLines[i], "__CATEGORY "))
			{
				estrConcatf(&pCategory->pComment, "%s%s", estrLength(&pCategory->pComment) ? "\n" : "", ppLines[i] + 11);
				bDidSomething = true;
			}
			else
			{
				estrConcatf(&pRebuiltComment, "%s%s", estrLength(&pRebuiltComment) ? "\n" : "", ppLines[i]);
			}
		}

		eaDestroyEx(&ppLines, NULL);

		if (bDidSomething)
		{
			pSetting->pComment = pRebuiltComment;
		}
		else
		{
			estrDestroy(&pRebuiltComment);
		}
	}
}

void ControllerAutoSetting_PopulateFromList(CmdList *pList, bool bEarly)
{

	char *pCategoryString = NULL;
	FOR_EACH_IN_STASHTABLE(pList->sCmdsByName, Cmd, pCmd)
	{
		if (AutoSetting_CommandIsAutoSettingCommand(pCmd, &pCategoryString))
		{
			AutoSetting_SingleSetting *pSetting = ControllerAutoSetting_SettingFromCmd(pCmd, pCategoryString);

			if (stashFindPointer(sSettingsByName, pCmd->name, NULL))
			{
				assertmsgf(0, "Found two ControllerAutoSetting commands named %s", pCmd->name);
			}

			pSetting->bEarly = bEarly;

			eaPush(&sppSettings, pSetting);
			stashAddPointer(sSettingsByName, pSetting->pCmdName, pSetting, true);
			pCmd->flags |= CMDF_CONTROLLERAUTOSETTING;

			ControllerAutoSetting_AddToCategory(pSetting);
		}
	}
	FOR_EACH_END;

	estrDestroy(&pCategoryString);
}

static int SortByCategory(const AutoSetting_SingleSetting **ppSetting1, const AutoSetting_SingleSetting **ppSetting2)
{
	return stricmp((*ppSetting1)->pCategory, (*ppSetting2)->pCategory);
}

AUTO_STRUCT;
typedef struct ControllerAutoSetting_LoadingWidget
{
	char *pName; AST(STRUCTPARAM)
	char *pValue; AST(STRUCTPARAM)
	char *pDefault; AST(STRUCTPARAM)
	bool bFoundSpecialDefault;
	bool bFuture;
} ControllerAutoSetting_LoadingWidget;

AUTO_STRUCT;
typedef struct ControllerAutoSetting_LoadingWidgetList
{
	ControllerAutoSetting_LoadingWidget **ppWidgets; AST(FORMATSTRING(DEFAULT_FIELD=1))
} ControllerAutoSetting_LoadingWidgetList;

ControllerAutoSetting_LoadingWidget **sppDeferredLoadingWidgets = NULL;

bool ControllerAutoSetting_ValStringAppropriateForSetting(AutoSetting_SingleSetting *pSetting, char *pValString)
{
	switch (pSetting->eType)
	{
	xcase ASTYPE_STRING:
		return true;
	
	xcase ASTYPE_INT:
		{
			S32 i;
			U32 j;

			if (!pValString)
			{
				return false;
			}

			if (!StringToInt_Paranoid(pValString, &i) && !StringToUint_Paranoid(pValString, &j))
			{
				return false;
			}

		}

	xcase ASTYPE_FLOAT:
		{
			float f;

			if (!pValString)
			{
				return false;
			}

			if (!StringToFloat(pValString, &f))
			{
				return false;
			}

		}
	}

	return true;
}

void ControllerAutoSetting_SendNewValueToServers(AutoSetting_SingleSetting *pSetting)
{
	int i;

	char *pCommandString = GetCommandStringForApplyingSettingToOtherServer(pSetting);

	for (i=0 ; i < ea32Size(&pSetting->pServerTypes); i++)
	{
		GlobalType eServerType = pSetting->pServerTypes[i];
		TrackedServerState *pServer = gpServersByType[eServerType];

		while (pServer)
		{
			if (pServer->pLink)
			{
				Packet *pPak = pktCreate(pServer->pLink, FROM_CONTROLLER_AUTO_SETTING_COMMANDS);
				pktSendString(pPak, pCommandString);
				pktSendString(pPak, "");
				pktSend(&pPak);
			}
			else if (pServer->bHasGottenAutoSettingsViaRemoteCommand)
			{
				RemoteCommand_SendAutoSettingFromController(eServerType, pServer->iContainerID, pCommandString);
			}

			pServer = pServer->pNext;
		}
	}
}

int ControllerAutoSetting_SetSettingValue(AutoSetting_SingleSetting *pSetting, char *pValString, enumCmdContextHowCalled eHowCalled)
{
	CmdContext cmd_context = {0};
	int iResult;
	char *pFullCommandString = NULL;
	int i;

	if (!ControllerAutoSetting_ValStringAppropriateForSetting(pSetting, pValString))
	{
		return false;
	}

	estrDestroy(&pSetting->pCommandStringForApplyingToOtherServres);
	for (i = 0; i < ea32Size(&pSetting->pServerTypes); i++)
	{
		ControllerAutoSettings_ClearCommandStringsForServerType(pSetting->pServerTypes[i]);
	}


	cmd_context.eHowCalled = eHowCalled;
	cmd_context.access_level = 10;
	cmd_context.flags |= CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED;

	estrPrintf(&pFullCommandString, "%s ", pSetting->pCmdName);
	if (pSetting->eType == ASTYPE_STRING)
	{
		if (pValString && pValString[0])
		{
			estrConcatf(&pFullCommandString, "\"");
			estrAppendEscaped(&pFullCommandString, pValString);
			estrConcatf(&pFullCommandString, "\"");
		}
		else
		{
			estrConcatf(&pFullCommandString, "\"\"");
		}

	}
	else
	{
		estrConcatf(&pFullCommandString, "%s", pValString);
	}

	iResult = cmdParseAndExecute(pSetting->bEarly ? &gEarlyCmdList : &gGlobalCmdList, pFullCommandString, &cmd_context);
	estrDestroy(&pFullCommandString);


	return iResult;


}

static void ApplyLoadingWidgetToSetting(ControllerAutoSetting_LoadingWidget *pWidget, AutoSetting_SingleSetting *pSetting)
{
	if (stricmp_safe(pWidget->pValue, pSetting->pCurValueString) == 0)
	{
		//value from file is the default value
		//no errors here no matter what... just change setting's origin
		if (pWidget->bFoundSpecialDefault)
		{
			pSetting->eOrigin = ASORIGIN_FILE_DEFAULT;
		}
		else
		{
			pSetting->eOrigin = ASORIGIN_FILE;
		}
	}
	else
	{
		//value from the file is NOT the default value
		if (pWidget->bFoundSpecialDefault)
		{
			ControllerAutoSetting_Notify(0, "While loading settings from %s, setting %s, the file had what it thought was the default value (%s), but the default value is actually (%s). Someone may have edited the file incorrectly, or the default value in code may have changed. The value from the file will be used for now, but please resolve this",
				sMainControllerAutoSettingFileName, pSetting->pCmdName, pWidget->pValue, pSetting->pCurValueString);
		}

		if (!ControllerAutoSetting_SetSettingValue(pSetting, pWidget->pValue, CMD_CONTEXT_HOWCALLED_CONTROLLER_AUTO_SETTING_FILE))
		{
			ControllerAutoSetting_Notify(1, "While loading settings from %s, setting %s, could not apply value \"%s\", it may be badly formed", 
				sMainControllerAutoSettingFileName, pSetting->pCmdName, pWidget->pValue);
		}

		estrCopy2(&pSetting->pCurValueString, pWidget->pValue);
		pSetting->eOrigin = ASORIGIN_FILE;
	}
}

void ControllerAutoSetting_LoadSettingsFromFileFromLoadedString(char *pStr)
{
	ControllerAutoSetting_LoadingWidgetList list = {0};
	int i;
	
	ParserReadText(pStr, parse_ControllerAutoSetting_LoadingWidgetList, &list, 0);

	for (i=eaSize(&list.ppWidgets) - 1;  i >= 0; i--)
	{
		ControllerAutoSetting_LoadingWidget *pWidget = list.ppWidgets[i];
		AutoSetting_SingleSetting *pSetting;

		if (pWidget->pDefault && pWidget->pDefault[0])
		{
			if (stricmp(pWidget->pDefault, "(future)") == 0)
			{
				pWidget->bFuture = true;
			}
			else
			{

				if (stricmp(pWidget->pDefault, "(default)") != 0)
				{
					ControllerAutoSetting_Notify(1, "While loading settings from %s, found illegal token after value for setting %s. It must either be empty or \"(default)\"",
						sMainControllerAutoSettingFileName, pWidget->pName);
				}

				pWidget->bFoundSpecialDefault = true;
			}
		}

		if (stashFindPointer(sSettingsByName, pWidget->pName, &pSetting) && !pWidget->bFuture)
		{
			ApplyLoadingWidgetToSetting(pWidget, pSetting);
		}
		else
		{
			eaPush(&sppDeferredLoadingWidgets, pWidget);
			eaRemoveFast(&list.ppWidgets, i);

//			ControllerAutoSetting_Notify(0, "While loading settings from %s, found setting %s which doesn't seem to exist. It may have been removed from code, or this may be a typo of some sort",
	//			sMainControllerAutoSettingFileName, pWidget->pName);
		}
	}	

	StructDeInit(parse_ControllerAutoSetting_LoadingWidgetList, &list);


}

void ControllerAutoSetting_WriteOutFile(bool bFailureIsFatal)
{
	FILE *pFile;
	char *pStr = NULL;
	pFile = fopen(sMainControllerAutoSettingFileName, "wt");
	if (!pFile)
	{
		ControllerAutoSetting_Notify(bFailureIsFatal, "Can't create %s", sMainControllerAutoSettingFileName);
	}

	ControllerAutoSetting_DumpAllToString(&pStr);
	fprintf(pFile, "%s", pStr);

	estrDestroy(&pStr);
	fclose(pFile);
}

AUTO_RUN_POSTINTERNAL;
void ControllerAutoSetting_PopulateListFromCmdListsAndFile(void)
{
	char executableDir[CRYPTIC_MAX_PATH];
	FILE *pFile;
	char productName[64];
	char backupDirName[CRYPTIC_MAX_PATH];
	char isContinuousBuilder[3];

	productName[0] = 0;

	ParseCommandOutOfCommandLine("IsContinuousBuilder", isContinuousBuilder);
	if (isContinuousBuilder[0] == '1')
	{
		sSystemActive[0] = '0';
		return;
	}


	ParseCommandOutOfCommandLine("UseAutoSettings", sSystemActive);

  	if (!ControllerAutoSetting_SystemIsActive())
	{
		return;
	}
	
	ControllerAutoSetting_LazyInit();

	ParseCommandOutOfCommandLine("SetProductName", productName);

	if (!productName[0])
	{
		sprintf(productName, "Unknown");
		ControllerAutoSetting_Notify(0, "No product name passed to controller, this may be very serious. Using unknown.");
	}

	ControllerAutoSetting_PopulateFromList(&gEarlyCmdList, true);
	ControllerAutoSetting_PopulateFromList(&gGlobalCmdList, false);

	eaQSort(sppSettings, SortByCategory);

	getExecutableDir(executableDir);
	backSlashes(executableDir);
	sprintf(sMainControllerAutoSettingFileName, "%s\\%s_ControllerAutoSettings.txt", executableDir, productName);
	sprintf(backupDirName, "%s\\%s_OldControllerAutoSetup", executableDir, productName);

	pFile = fopen(sMainControllerAutoSettingFileName, "rb");
	if (pFile)
	{
		char *pStringFromFile = NULL;
		size_t iFileSize;
		char *pBackupFileName = NULL;
		int iResult;

		fseek(pFile, 0, SEEK_END);
		iFileSize = ftell(pFile);
		fseek(pFile, 0, SEEK_SET);

		estrSetSize(&pStringFromFile, (int)iFileSize);
		fread(pStringFromFile, iFileSize, 1, pFile);

		fclose(pFile);

		iResult = mkdir(backupDirName);
		estrCopy2(&pBackupFileName, timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000()));
		estrMakeAllAlphaNumAndUnderscores(&pBackupFileName);
		estrInsertf(&pBackupFileName, 0, "%s\\", backupDirName);
		estrConcatf(&pBackupFileName, ".txt");

		pFile = fopen(pBackupFileName, "wt");

		if (!pFile)
		{
			ControllerAutoSetting_Notify(0, "Could not open file %s to backup controller auto settings. This may or may not be a problem", pBackupFileName);
		}
		else
		{
			fprintf(pFile, "//Backup of controllerAutoSettings.txt for shard.\n//Full command line: %s\n\n\n%s", GetCommandLine(), pStringFromFile);
			fclose(pFile);
		}

		estrDestroy(&pBackupFileName);

		ControllerAutoSetting_LoadSettingsFromFileFromLoadedString(pStringFromFile);

		estrDestroy(&pStringFromFile);
	}
	else
	{
		ControllerAutoSetting_Notify(0, "No %s file was found, likely because this is the first time this shard has run on this machine, or with a version supporting that file. One is being automatically created. If this is alarming, backup versions should exist in %s", sMainControllerAutoSettingFileName, backupDirName);
		sbNeedToWriteFile = true;

//		ControllerAutoSetting_WriteOutFile(true);
	}
}

void ControllerAutoSetting_DumpAllToString(char **ppOutString)
{
	int i;
	const char *pLastCategory = NULL;

	for (i=0; i < eaSize(&sppSettings); i++)
	{
		AutoSetting_SingleSetting *pSetting = sppSettings[i];
		char *pFileString = NULL;
		FILE *fpBuff = fileOpenEString(&pFileString);

		if (pSetting->pCategory != pLastCategory)
		{
			estrConcatf(ppOutString, "//////////////////////////////////////////\n//%s\n//////////////////////////////////////////\n\n",
				pSetting->pCategory);
			pLastCategory = pSetting->pCategory;
		}

		if (pSetting->pComment)
		{
			char *pTempComment = NULL;
			estrCopy2(&pTempComment, pSetting->pComment);
			estrInsertf(&pTempComment, 0, "//");
			estrReplaceOccurrences(&pTempComment, "\n", "\n//");

			estrConcatf(ppOutString, "%s\n", pTempComment);
			estrDestroy(&pTempComment);
		}

		estrConcatf(ppOutString, "%s ", pSetting->pCmdName);

		WriteQuotedString(fpBuff, pSetting->bOfficialValueStringSet ? pSetting->pOfficialValueString : pSetting->pCurValueString, 0, false);
		fileClose(fpBuff);

		estrConcatf(ppOutString, "%s", pFileString);
		estrDestroy(&pFileString);

		if (pSetting->eOrigin == ASORIGIN_FUTURE)
		{
			estrConcatf(ppOutString, " (Future)");
		}
		else if (pSetting->eOrigin == ASORIGIN_DEFAULT || pSetting->eOrigin == ASORIGIN_FILE_DEFAULT)
		{
			estrConcatf(ppOutString, " (Default)");
		}

		estrConcatf(ppOutString, "\n\n");
	}
}


void OVERRIDE_LATELINK_ControllerAutoSetting_CmdWasCalled(const char *pCmdName, enumCmdContextHowCalled eHow)
{
	AutoSetting_SingleSetting *pSetting;
	static char *pNewValue = NULL;
	AutoSettingType eType;
	
	if (!stashFindPointer(sSettingsByName, pCmdName, &pSetting))
	{
		ControllerAutoSetting_Notify(0, "CmdParse thinks that %s is a ControllerAutoSetting command, but it's not",
			pCmdName);
		return;
	}

	estrClear(&pNewValue);
	AutoSetting_GetCmdTypeAndValueString(pSetting->pCmd, &eType, &pNewValue);

	switch (eHow)
	{
	case CMD_CONTEXT_HOWCALLED_COMMANDLINE:
	case CMD_CONTEXT_HOWCALLED_EARLY_COMMANDLINE:
		if (pSetting->eOrigin == ASORIGIN_DEFAULT)
		{
			ControllerAutoSetting_Notify(0, "ControllerAutoSetting %s is being set on the command line. In the future it will be read from the setting file (%s), please remove it from the command line",
				pSetting->pCmdName, sMainControllerAutoSettingFileName);
	
			estrCopy2(&pSetting->pCurValueString, pNewValue);
			pSetting->eOrigin = ASORIGIN_FILE; //because we expect to it be read from the file in the future.

			sbNeedToWriteFile = true;
			return;
		}

		if (stricmp(pNewValue, pSetting->pCurValueString) == 0) 
		{
			ControllerAutoSetting_Notify(0, "ControllerAutoSetting %s is being redundantly set to its correct value on the command line, even though it's official value is read from the setting file (%s). This is harmless for now, but might be confusing in the future if the value is changed through the servermon interface. Please remove it from the command line",
				pSetting->pCmdName, sMainControllerAutoSettingFileName);
			return;
		}

		ControllerAutoSetting_Notify(0, "ControllerAutoSetting %s has been set via %s to official value \"%s\", but it's being set on the command line to value \"%s\". This is presumed to be incorrect, so the official value is being restored. Please remove this from the command line.",
			pSetting->pCmdName, sMainControllerAutoSettingFileName, pSetting->pCurValueString, pNewValue);

		ControllerAutoSetting_SetSettingValue(pSetting, pSetting->pCurValueString, CMD_CONTEXT_HOWCALLED_CONTROLLER_AUTO_SETTING_FILE);

		return;

	default:
		ControllerAutoSetting_Notify(0, "A CmdParse command was executed via %s, and has set ControllerController setting %s to value \"%s\". This is dangerous. It is being allowed for now, but will not be reflected in saved file on disk.",
			StaticDefineIntRevLookup(enumCmdContextHowCalledEnum, eHow), pSetting->pCmdName, pNewValue);
		pSetting->eOrigin = ASORIGIN_COMMAND;
		pSetting->eCmdHowCalled = eHow;
		if (!pSetting->bOfficialValueStringSet)
		{
			pSetting->bOfficialValueStringSet = true;
			estrCopy(&pSetting->pOfficialValueString, &pSetting->pCurValueString);
		}
		estrCopy2(&pSetting->pCurValueString, pNewValue);
		return;
	}
}

void ControllerAutoSetting_NormalOperationStartedCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	int i;
	sbNoMoreBufferedWarnings = true;

	for (i=0; i < eaSize(&sppBufferedWarnings); i++)
	{
		WARNING_NETOPS_ALERT("SETTINGS_WARNING", "%s", sppBufferedWarnings[i]);
	}

	eaDestroyEx(&sppBufferedWarnings, NULL);
}

void ControllerAutoSetting_NormalOperationStarted(void)
{

	if (!ControllerAutoSetting_SystemIsActive())
	{
		return;
	}

	Controller_LoadInOtherServerAutoSettingsAndProcess();

	if (sbNeedToWriteFile)
	{
		ControllerAutoSetting_WriteOutFile(true);
	}

	//allow a two second delay for controller to hopefully get in touch with critical systems so alerts are likely
	//to be emailed
	TimedCallback_Run(ControllerAutoSetting_NormalOperationStartedCB, NULL, 2.0f);
}

AUTO_COMMAND;
char *SetAutoSetting(const char *pCmdName, const ACMD_SENTENCE pNewValue_in)
{
	AutoSetting_SingleSetting *pSetting;
	static char *spRetVal = NULL;
	static char *pNewValue = NULL;
	estrCopy2(&pNewValue, pNewValue_in);
	estrTrimLeadingAndTrailingWhitespace(&pNewValue);

	if (!stashFindPointer(sSettingsByName, pCmdName, &pSetting))
	{
		estrPrintf(&spRetVal, "Setting %s doesn't seem to exist", pCmdName);
		return spRetVal;		
	}
	
	if (pSetting->eOrigin == ASORIGIN_FUTURE)
	{
		estrCopy2(&pSetting->pCurValueString, pNewValue);
	}	
	else
	{

		if (!ControllerAutoSetting_SetSettingValue(pSetting, pNewValue, CMD_CONTEXT_HOWCALLED_CONTROLLER_AUTO_SETTING_SERVERMON))
		{
			estrPrintf(&spRetVal, "Something went wrong when trying to set %s to value \"%s\". Restoring it to \"%s\"",
				pCmdName, pNewValue, pSetting->pCurValueString);
			ControllerAutoSetting_SetSettingValue(pSetting, pSetting->pCurValueString, CMD_CONTEXT_HOWCALLED_CONTROLLER_AUTO_SETTING_SERVERMON);
			return spRetVal;
		}

		estrCopy2(&pSetting->pCurValueString, pNewValue);
		pSetting->bOfficialValueStringSet = false;

		pSetting->eOrigin = ASORIGIN_FILE;
	}
	

	ControllerAutoSetting_WriteOutFile(false);

	if (ea32Size(&pSetting->pServerTypes))
	{
		ControllerAutoSetting_SendNewValueToServers(pSetting);
	}


	return "Value successfuly set";
}

AUTO_COMMAND;
char *RestoreAutoSetting(char *pCmdName)
{
	AutoSetting_SingleSetting *pSetting;
	static char *spRetVal = NULL;

	if (!stashFindPointer(sSettingsByName, pCmdName, &pSetting))
	{
		estrPrintf(&spRetVal, "Setting %s doesn't seem to exist", pCmdName);
		return spRetVal;		
	}

	if (pSetting->eOrigin == ASORIGIN_FUTURE)
	{
		ControllerAutoSetting_Category *pCategory;

		if (!spAllocedFutureName)
		{
			return "System not initialized";
		}

		if (!stashFindPointer(gControllerAutoSettingCategories, spAllocedFutureName, &pCategory))
		{
			return "Future category does not exist";
		}		

		eaFindAndRemove(&sppSettings, pSetting);
		eaFindAndRemove(&pCategory->ppSettings, pSetting);
		pCategory->iNumSettings--;
		stashRemovePointer(sSettingsByName, pCmdName, NULL);

		StructDestroy(parse_AutoSetting_SingleSetting, pSetting);

		ControllerAutoSetting_WriteOutFile(false);

		return "Future value has been cleared";
	}
	else
	{
		ControllerAutoSetting_SetSettingValue(pSetting, pSetting->pDefaultValueString, CMD_CONTEXT_HOWCALLED_CONTROLLER_AUTO_SETTING_SERVERMON);
		estrCopy2(&pSetting->pCurValueString, pSetting->pDefaultValueString);
		pSetting->bOfficialValueStringSet = false;
		estrClear(&pSetting->pOfficialValueString);
		pSetting->eOrigin = ASORIGIN_FILE_DEFAULT;

		ControllerAutoSetting_WriteOutFile(false);

		if (ea32Size(&pSetting->pServerTypes))
		{
			ControllerAutoSetting_SendNewValueToServers(pSetting);
		}

		return "Value restored to default";
	}

}

AUTO_COMMAND;
void Controller_DumpAllAutoSettingsAndQuit(void)
{
	int eServerType;
	char *pFullCommandLine = NULL;
	char *pDirName = NULL;
	int iCount; 
	
	QueryableProcessHandle *spHandles[GLOBALTYPE_MAXTYPES] = {0};
	U32 iStartTime = timeSecondsSince2000();

	estrPrintf(&pDirName, "%s/server/AutoSettings", fileDataDir());

	if (dirExists(pDirName))
	{
		estrPrintf(&pFullCommandLine, "erase %s/*.txt", pDirName);
		backSlashes(pFullCommandLine);
		system(pFullCommandLine);
	}


	for (eServerType = 0; eServerType < GLOBALTYPE_MAXTYPES; eServerType++)
	{
		if (gServerTypeInfo[eServerType].bSupportsAutoSettings)
		{

			estrPrintf(&pFullCommandLine, "%s\\%s -SetProductName %s %s -DumpAutoSettingsAndQuitFileName %s/%s.txt",
				gServerTypeInfo[eServerType].bLaunchFromCoreDirectory ? gCoreExecutableDirectory : gExecutableDirectory,
				gServerTypeInfo[eServerType].executableName32_original, GetProductName(), GetShortProductName(),
				pDirName, GlobalTypeToName(eServerType));

			spHandles[eServerType] = StartQueryableProcess_WithFullDebugFixup(pFullCommandLine, NULL, false, false, false, NULL);

			printf("Executed %s\n", pFullCommandLine);
		}
	}

	do
	{
		int iRetVal;
		iCount = 0;

		Sleep(1000);
		for (eServerType = 0; eServerType < GLOBALTYPE_MAXTYPES; eServerType++)
		{
			if (spHandles[eServerType])
			{
				if (QueryableProcessComplete(&spHandles[eServerType], &iRetVal))
				{
					if (iRetVal != 0)
					{
						assertmsgf(0, "Couldn't dump auto settings for %s", GlobalTypeToName(eServerType));
						svrExit(-1);
					}
				}
				else
				{
					iCount++;
				}

			}
		}

		if (timeSecondsSince2000_ForceRecalc() > iStartTime + 600)
		{
			assertmsgf(0, "Taking > 5 minutes to do Controller_DumpAllAutoSettingsAndQuit, something is wrong");
			svrExit(-1);
		}
	}
	while (iCount);

	svrExit(0);


}

void CreateNewCmdForAutoSetting(AutoSetting_SingleSetting *pSetting)
{
	Cmd *pCmd = calloc(sizeof(Cmd), 1);
	pCmd->access_level = 9;
	pCmd->categories = strdupf(" %s%s ", AUTO_SETTING_CATEGORY_PREFIX, pSetting->pCategory);
	pCmd->comment = pSetting->pComment;
	pCmd->flags = CMDF_CONTROLLERAUTOSETTING;
	pCmd->name = pSetting->pCmdName;
	pCmd->iNumLogicalArgs = pCmd->iNumReadArgs = 1;
	
	switch (pSetting->eType)
	{
	xcase ASTYPE_INT:
		pCmd->data[0].data_size = sizeof(U64);
		pCmd->data[0].ptr = calloc(sizeof(U64), 1);
		pCmd->data[0].type = MULTI_INT;
	xcase ASTYPE_FLOAT:
		pCmd->data[0].data_size = sizeof(F32);
		pCmd->data[0].ptr = calloc(sizeof(F32), 1);
		pCmd->data[0].type = MULTI_FLOAT;
	xcase ASTYPE_STRING:
		pCmd->data[0].data_size = MAGIC_CMDPARSE_STRING_SIZE_ESTRING;
		pCmd->data[0].ptr = calloc(sizeof(void*), 1);
		pCmd->data[0].type = MULTI_STRING;
	}

	if (pSetting->bEarly)
	{
		cmdAddSingleCmdToList(&gEarlyCmdList, pCmd);
	}
	else
	{
		cmdAddSingleCmdToList(&gGlobalCmdList, pCmd);
	}

	ControllerAutoSetting_SetSettingValue(pSetting, pSetting->pCurValueString, CMD_CONTEXT_HOWCALLED_CONTROLLER_AUTO_SETTING_NEWCMDINIT);
}





void Controller_LoadInOtherServerAutoSettingsAndProcess(void)
{
	AutoSetting_SingleSetting *pSetting;
	int i;
	AutoSetting_ForDataFile_List *pList = StructCreate(parse_AutoSetting_ForDataFile_List);

	ParserLoadFiles("server/AutoSettings", ".txt", NULL, 0, parse_AutoSetting_ForDataFile_List, pList);

	FOR_EACH_IN_EARRAY(pList->ppSettings, AutoSetting_ForDataFile, pDataSetting)
	{
		GlobalType eServerType;
		char tempFileName[CRYPTIC_MAX_PATH];

		getFileNameNoExtNoDirs(tempFileName, pDataSetting->pFileName);
		eServerType = NameToGlobalType(tempFileName);

		assertmsgf(eServerType, "Autosetting %s thinks that it is for server type %s, which is unknown", pDataSetting->pName, tempFileName);
		assertmsgf(gServerTypeInfo[eServerType].bSupportsAutoSettings, "Autsetting %s thinks it is for server type %s, which does not support autoSettings",
			pDataSetting->pName, tempFileName);
		
		if (stashFindPointer(sSettingsByName, pDataSetting->pName, &pSetting))
		{
	
			assertmsgf(stricmp_safe(pSetting->pCategory, pDataSetting->pCategory) == 0, "AutoSetting %s has two different categories, %s and %s",
				pDataSetting->pName, pSetting->pCategory, pDataSetting->pCategory);
		
			assertmsgf(stricmp_safe(pSetting->pDefaultValueString, pDataSetting->pBuiltInVal) == 0, "AutoSetting %s has two different default values, %s and %s",
				pDataSetting->pName, pSetting->pDefaultValueString, pDataSetting->pBuiltInVal);

	
			ea32Push(&pSetting->pServerTypes, eServerType);
		
		}
		else
		{
			pSetting = ControllerAutoSetting_SettingFromDataFileSetting(pDataSetting);
			eaPush(&sppSettings, pSetting);
			stashAddPointer(sSettingsByName, pSetting->pCmdName, pSetting, true);

			ControllerAutoSetting_AddToCategory(pSetting);
			ea32Push(&pSetting->pServerTypes, eServerType);

			CreateNewCmdForAutoSetting(pSetting);
		}
	}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY(sppDeferredLoadingWidgets, ControllerAutoSetting_LoadingWidget, pWidget)
	{
		if (pWidget->bFuture)
		{
			//do nothing yet
		}
		else if (stashFindPointer(sSettingsByName, pWidget->pName, &pSetting))
		{
			ApplyLoadingWidgetToSetting(pWidget, pSetting);
		}
		else
		{
			ControllerAutoSetting_Notify(0, "While loading settings from %s, found setting %s which doesn't seem to exist. It may have been removed from code, or this may be a typo of some sort",
				sMainControllerAutoSettingFileName, pWidget->pName);
			sbNeedToWriteFile = true;
		}
	}
	FOR_EACH_END

	for (i=0; i < eaSize(&sppSettings); i++)
	{
		pSetting = sppSettings[i];

		if (pSetting->eOrigin == ASORIGIN_DEFAULT)
		{
			bool bFoundFutureWidget = false;
			//try to find a future widget that matches this
			FOR_EACH_IN_EARRAY(sppDeferredLoadingWidgets, ControllerAutoSetting_LoadingWidget, pWidget)
			{
				if (pWidget->bFuture && stricmp(pWidget->pName, pSetting->pCmdName) == 0)
				{
					if (stricmp_safe(pWidget->pValue, pSetting->pDefaultValueString) == 0)
					{
						ControllerAutoSetting_Notify(0, "While loading settings from %s, found no setting for %s, which is presumably a new AUTO_SETTING. But we DID find a future setting for it. But the future setting and the default value are both %s. This may or may not be weird. Leaving it at that value.",
							sMainControllerAutoSettingFileName, pSetting->pCmdName, pWidget->pValue);
						pSetting->eOrigin = ASORIGIN_FILE;
					}
					else
					{
						ControllerAutoSetting_Notify(0, "While loading settings from %s, found no setting for %s, which is presumably a new AUTO_SETTING. But we DID find a future setting for it, so setting it to value %s",
							sMainControllerAutoSettingFileName, pSetting->pCmdName, pWidget->pValue);
						ApplyLoadingWidgetToSetting(pWidget, pSetting);
					}
					eaFindAndRemove(&sppDeferredLoadingWidgets, pWidget);
					StructDestroy(parse_ControllerAutoSetting_LoadingWidget, pWidget);
					bFoundFutureWidget = true;
					break;
				}
			}
			FOR_EACH_END;

			if (!bFoundFutureWidget)
			{
				bool bFoundPotentiallyMisspelled = false;

				//look for a misspelled future widget
				FOR_EACH_IN_EARRAY(sppDeferredLoadingWidgets, ControllerAutoSetting_LoadingWidget, pWidget)
				{
					if (pWidget->bFuture && levenshtein_distance(pWidget->pName, pSetting->pCmdName) < LEV_DISTANCE_FOR_POTENTIAL_MISSPELLING)
					{
						bFoundPotentiallyMisspelled = true;
						ControllerAutoSetting_Notify(0, "While loading settings from %s, found no setting for %s. We did, however, find a future auto setting named %s with value %s, which is similar, and may represent a typo. In any case, adding %s with default value \"%s\"",
							sMainControllerAutoSettingFileName, pSetting->pCmdName, pWidget->pName, pWidget->pValue, pSetting->pCmdName, pSetting->pCurValueString);


						break;
					}



				}
				FOR_EACH_END;

				if (!bFoundPotentiallyMisspelled)
				{
//					ControllerAutoSetting_Notify(0, "While loading settings from %s, found no setting for %s. This may be a typo, or it may be a new field. Will add it to the file with default value \"%s\"",
//						sMainControllerAutoSettingFileName, pSetting->pCmdName, pSetting->pCurValueString);
				}
				
			}

			sbNeedToWriteFile = true;
		}
	}


	FOR_EACH_IN_EARRAY(sppDeferredLoadingWidgets, ControllerAutoSetting_LoadingWidget, pWidget)
	{
		if (pWidget->bFuture)
		{
			char *pErrorString = NULL;
			if (!CreateAndAddFutureSetting(pWidget->pName, pWidget->pValue, &pErrorString))
			{
				ControllerAutoSetting_Notify(0, "While loading settings from %s, found a future setting %s with value %s, but something went wrong while trying to load it: %s",
					sMainControllerAutoSettingFileName, pWidget->pName, pWidget->pValue, pErrorString);
				estrDestroy(&pErrorString);
				sbNeedToWriteFile = true;
			}
		}
	}
	FOR_EACH_END;
}

char *GetCommandStringForApplyingSettingToOtherServer(AutoSetting_SingleSetting *pSetting)
{
	char *pTemp = NULL;

	if (estrLength(&pSetting->pCommandStringForApplyingToOtherServres))
	{
		return pSetting->pCommandStringForApplyingToOtherServres;
	}


	estrConcatf(&pTemp, "%s ", pSetting->pCmdName);

	if (pSetting->eType == ASTYPE_STRING)
	{
		estrConcatf(&pTemp, "\"");
		estrAppendEscaped(&pTemp, pSetting->pCurValueString);
		estrConcatf(&pTemp, "\"");
	}
	else
	{
		estrAppendEscaped(&pTemp, pSetting->pCurValueString);
	}

	estrSuperEscapeString(&pSetting->pCommandStringForApplyingToOtherServres, pTemp);
	estrInsertf(&pSetting->pCommandStringForApplyingToOtherServres, 0, "%s ", pSetting->bEarly ? AUTOSETTING_CONSTSTRING_EARLY : AUTOSETTING_CONSTSTRING_NORMAL);
	estrDestroy(&pTemp);

	return pSetting->pCommandStringForApplyingToOtherServres;
}

static char **sCommandStringEarraysForServerTypes[GLOBALTYPE_MAX] = {0};

void ControllerAutoSettings_ClearCommandStringsForServerType(GlobalType eServerType)
{
	eaDestroy(&sCommandStringEarraysForServerTypes[eServerType]);
}

char **ControllerAutoSettings_GetCommandStringsForServerType(GlobalType eServerType)
{
	if (!gServerTypeInfo[eServerType].bSupportsAutoSettings)
	{
		return NULL;
	}

	if (sCommandStringEarraysForServerTypes[eServerType])
	{
		return sCommandStringEarraysForServerTypes[eServerType];
	}

	eaCreate(&sCommandStringEarraysForServerTypes[eServerType]);

	FOR_EACH_IN_EARRAY(sppSettings, AutoSetting_SingleSetting, pSetting)
	{
		if (ea32Find(&pSetting->pServerTypes, eServerType) != -1)
		{
			eaPush(&sCommandStringEarraysForServerTypes[eServerType], GetCommandStringForApplyingSettingToOtherServer(pSetting));
		}
	}
	FOR_EACH_END

	return sCommandStringEarraysForServerTypes[eServerType];
}

static void AuditUpcomingLaunch(GlobalType eServerType)
{
	char *pCommandString = NULL;
	char **ppCommands = NULL;
	int i;

	ControllerScripting_GetLikelyCommandLine(eServerType, &pCommandString);
	cmdGetPresumedCommandsFromCommandLine(pCommandString, &ppCommands);

	for (i=0; i < eaSize(&ppCommands); i++)
	{
		char *pCommandName = NULL;
		AutoSetting_SingleSetting *pSetting;

		estrCopy2(&pCommandName, ppCommands[i]);
		estrTruncateAtFirstOccurrence(&pCommandName, ' ');

		if (stashFindPointer(sSettingsByName, pCommandName, &pSetting))
		{
			if (ea32Find(&pSetting->pServerTypes, eServerType) != -1)
			{
				ControllerAutoSetting_Notify(false, "During upcoming launch of %s, expected command line includes <<%s>>, which appears to be attempting to conflict with AUTO_SETTING %s. This should almost certainly be removed from the command line",
					GlobalTypeToName(eServerType), ppCommands[i], pSetting->pCmdName);
			}
		}

		estrDestroy(&pCommandName);
	}

	eaDestroyEx(&ppCommands, NULL);
	estrDestroy(&pCommandString);
}



AUTO_COMMAND;
void AuditUpcomingLaunchesforAutoSettingConflicts(void)
{
	GlobalType eServerType;

	for (eServerType = 0; eServerType < GLOBALTYPE_MAXTYPES; eServerType++)
	{
		if (gServerTypeInfo[eServerType].bSupportsAutoSettings)
		{
			AuditUpcomingLaunch(eServerType);
		}
	}
}

bool CreateAndAddFutureSetting(char *pSettingName, char *pSettingValue, char **ppOutError)
{
	ControllerAutoSetting_Category *pCategory;
	AutoSetting_SingleSetting *pSetting;


	if (!(pSettingName && pSettingName[0]))
	{
		estrCopy2(ppOutError, "Invalid setting name");
		return false;
	}

	if (!spAllocedFutureName)
	{
		estrCopy2(ppOutError, "System not initialized");
		return false;
	}

	if (!stashFindPointer(gControllerAutoSettingCategories, spAllocedFutureName, &pCategory))
	{
		estrCopy2(ppOutError, "Future category does not exist");
		return false;
	}

	if (stashFindPointer(sSettingsByName, pSettingName, NULL))
	{
		estrCopy2(ppOutError, "There is already a setting with that name");
		return false;
	}

	pSetting = StructCreate(parse_AutoSetting_SingleSetting);
	pSetting->pCmdName = allocAddString(pSettingName);
	pSetting->eType = ASTYPE_STRING;
	pSetting->pCategory = spAllocedFutureName;
	estrCopy2(&pSetting->pCurValueString, pSettingValue);
	pSetting->pDefaultValueString = strdup(FUTURE_SETTING_DEFAULT);
	pSetting->eOrigin = ASORIGIN_FUTURE;
	pSetting->pComment = allocAddString("(Added via AUTO_COMMAND, will apply in the future)");

	eaPush(&sppSettings, pSetting);
	stashAddPointer(sSettingsByName, pSetting->pCmdName, pSetting, true);
	ControllerAutoSetting_AddToCategory(pSetting);

	return true;
}

AUTO_COMMAND;
char *AddFutureSetting(char *pSettingName, ACMD_SENTENCE pSettingValue)
{
	static char *spError = NULL;
	estrClear(&spError);
	if (CreateAndAddFutureSetting(pSettingName, pSettingValue, &spError))
	{
		ControllerAutoSetting_WriteOutFile(false);
		return "Future Setting added";
	}

	return spError;
}








#include "Controller_AutoSettings_c_ast.c"
