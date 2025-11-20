#include "GlobalTypes.h"
#include "LogParser.h"
#include "estring.h"
#include "timing.h"
#include "LogParser_Http_c_ast.h"
#include "../../libs/ServerLib/pub/ServerLib.h"
#include "LogParserGraphs.h"
#include "HttpXpathSupport.h"
#include "LogParser_h_ast.h"
#include "LogParserLaunching.h"
#include "LogParserFilteredLogFile.h"
#include "objPath.h"

LogParserStandAloneOptions gStandAloneOptions = {0};


AUTO_STRUCT;
typedef struct GraphLinks
{
	char *pGraphName; AST(KEY)
	char *pLiveGraphLink; AST(ESTRING, FORMATSTRING(HTML=1))
	char *pMostRecentGraphLink; AST(ESTRING, FORMATSTRING(HTML=1))
	char *pLongTermGraphLink; AST(ESTRING, FORMATSTRING(HTML=1))
	char *pArchived; AST(ESTRING, FORMATSTRING(HTML=1))
	U32 iMostRecentDataTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT=1))
	int iNumDataPoints; 
	char *pSwitch; AST(ESTRING, FORMATSTRING(HTML=1))
} GraphLinks;

AUTO_STRUCT;
typedef struct LogParserOverview
{
	LogParserStandAloneOptions *pStandAloneOptions;
	GraphLinks **ppGraphs;
	GraphLinks **ppInactiveGraphs;
	char *pGenericInfo; AST(ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1))

	char *pSaveConfiguration; AST(ESTRING, FORMATSTRING(command=1))
	char *pLoadConfiguration; AST(ESTRING, FORMATSTRING(command=1))
		
	char *pScanNow; AST(ESTRING, FORMATSTRING(command=1))
	char *pKillStandAlone; AST(ESTRING, FORMATSTRING(command=1))
	char *pLongTimeout; AST(ESTRING, FORMATSTRING(command=1))
	char *pDumpLongTermCSVs; AST(ESTRING, FORMATSTRING(command=1))
	char *pScanBins; AST(ESTRING, FORMATSTRING(command=1))

	char *pLaunchStandAlone; AST(ESTRING, FORMATSTRING(command=1))

	char *pAddRunTimeConfig; AST(ESTRING, FORMATSTRING(command=1))

	char *pNeverTimeOut; AST(ESTRING, FORMATSTRING(command=1))
	char *pCancelNeverTimeOut; AST(ESTRING, FORMATSTRING(command=1))

	AST_COMMAND("Apply Transaction","ApplyEditToLogParserOptions $STRING(Transaction String)$CONFIRM(Really apply this transaction?) $HIDE")

} LogParserOverview;


#define HOUR 60 * 60
#define DAY 24 * 60 * 60
#define WEEK 7 * 24 * 60 * 60
#define HALFDAY 12 * 60 * 60

char *DayName[] = 
{
	"Sun",
	"Mon",
	"Tue",
	"Wed",
	"Thu",
	"Fri",
	"Sat",
};

char *MonthName[] = 
{
	"Jan",
	"Feb",
	"Mar",
	"Apr",
	"May",
	"Jun",
	"Jul",
	"Aug",
	"Sep",
	"Oct",
	"Nov",
	"Dec",
};

//returns a string describing a time in the past that is as easy to understand as possible, given the 
//graph's lifespan. For instance, "yesterday" or "Last Week".
char *GetDescriptiveTimeNameForGraph(Graph *pGraph, U32 iTime)
{
	static char sRetString[256];
	static char *spRetEString = NULL;

	U32 iCurTime = timeSecondsSince2000();

	if (pGraph->pDefinition->iGraphLifespan == DAY)
	{
		struct tm tm = {0};
		
		if (iCurTime - iTime < DAY)
		{
			return "Yesterday";
		}

		timeMakeLocalTimeStructFromSecondsSince2000(iTime - HALFDAY, &tm);

		estrPrintf(&spRetEString, "%s, %s %d", DayName[tm.tm_wday], MonthName[tm.tm_mon], tm.tm_mday);

		if (iCurTime - iTime > 365 * DAY)
		{
			estrConcatf(&spRetEString, ", %d", tm.tm_year + 1900);
		}

		return spRetEString;
	}
	else if (pGraph->pDefinition->iGraphLifespan == WEEK)
	{
		struct tm tm1 = {0};
		struct tm tm2 = {0};


		if (iCurTime - iTime < WEEK)
		{
			return "Last Week";
		}

		//first day of week
		timeMakeLocalTimeStructFromSecondsSince2000(iTime - WEEK + HALFDAY, &tm1);
		timeMakeLocalTimeStructFromSecondsSince2000(iTime - HALFDAY, &tm2);

		estrPrintf(&spRetEString, "%s %d - ", MonthName[tm1.tm_mon], tm1.tm_mday);

		if (tm1.tm_mon != tm2.tm_mon)
		{
			estrConcatf(&spRetEString, "%s %d", MonthName[tm2.tm_mon], tm2.tm_mday);
		}
		else
		{
			estrConcatf(&spRetEString, "%d", tm2.tm_mday);
		}
	
		if (iCurTime - iTime > 365 * DAY)
		{
			estrConcatf(&spRetEString, ", %d", tm2.tm_year + 1900);
		}

		return spRetEString;





	}
	timeMakeLocalDateStringFromSecondsSince2000(sRetString, iTime);

	return sRetString;
}


static LogParserOverview *gpLogParserOverview = NULL;

LogParserOverview *GetLogParserOverview(void)
{
	static U32 iTimeStamp = 0;

	if (gpLogParserOverview && (timeSecondsSince2000() - iTimeStamp < 3))
	{
		return gpLogParserOverview;
	}

	if (gpLogParserOverview)
	{
		gpLogParserOverview->pStandAloneOptions = NULL;
		StructDestroy(parse_LogParserOverview, gpLogParserOverview);
	}
	
	gpLogParserOverview = StructCreate(parse_LogParserOverview);

	if (gbStandAlone)
	{
		gpLogParserOverview->pStandAloneOptions = &gStandAloneOptions;
	}

	estrPrintf(&gpLogParserOverview->pGenericInfo, "<input type=\"hidden\" id=\"defaultautorefresh\" value=\"1\"><a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID,GlobalTypeToName(GetAppGlobalType()));

	{
		StashTableIterator iterator;
		StashElement element;

		stashGetIterator(sGraphsByGraphName, &iterator);
		
		while (stashGetNextElement(&iterator, &element))
		{
			Graph *pGraph = stashElementGetPointer(element);
			if (!pGraph->pDefinition->bIAmALongTermGraph)
			{
				GraphLinks *pLinks = StructCreate(parse_GraphLinks);
			
				pLinks->pGraphName = strdup(pGraph->pGraphName);

			
				if (pGraph->iTotalDataCount)
				{
					estrPrintf(&pLinks->pLiveGraphLink, "<span><a href=\"%s.graph[%s].live\">Graph</a></span> <span><a href=\"/file/logparser/%u/graphDownLoad/%s.html\">DL</a></span> <span><a href=\"%s.csv[%s].live\">CSV</a></span>", LinkToThisServer(), pGraph->pGraphName, GetAppGlobalID(), pGraph->pGraphName, LinkToThisServer(), pGraph->pGraphName);
				}
				
				if (!gbStandAlone)
				{
					if (ea32Size(&pGraph->ppTimesOfOldGraphs))
					{
						estrPrintf(&pLinks->pMostRecentGraphLink, "<span><a href=\"%s.graph[%s].%u\">%s</a></span> <span><a href=\"%s.csv[%s].%u\">CSV</a></span>", LinkToThisServer(), pGraph->pGraphName, pGraph->ppTimesOfOldGraphs[0], 
							GetDescriptiveTimeNameForGraph(pGraph, pGraph->ppTimesOfOldGraphs[0]), LinkToThisServer(), pGraph->pGraphName, pGraph->ppTimesOfOldGraphs[0]);
					
						estrPrintf(&pLinks->pArchived, "<a href=\"%s.archived[%s]\">%d Archived Graphs and CSVs</a>", LinkToThisServer(), pGraph->pGraphName, ea32Size(&pGraph->ppTimesOfOldGraphs));
					}
				}

				if (!gbStandAlone && pGraph->pDefinition->bLongTermGraph)
				{
					estrPrintf(&pLinks->pLongTermGraphLink, "<span><a href=\"%s.graph[%sLongTerm].live\">Graph</a></span> <span><a href=\"%s.csv[%sLongTerm].live\">CSV</a></span>", LinkToThisServer(), pGraph->pGraphName, LinkToThisServer(), pGraph->pGraphName);
				}


			/*	if (pGraph->iTotalDataCount)
				{
					estrPrintf(&pLinks->pMainLink, 	"<span><a href=\"/viewimage?imageName=LOGPARSER_0_GRAPH_%s.jpg\" target=\"_blank\">Graph</a></span> <span><a href=\"%s%s.MAIN.%s\">CSV</a></span>\n", pGraph->pGraphName,
						LinkToThisServer(), CSV_DOMAIN_NAME, pGraph->pGraphName);		
				}*/

/*				if (!gbStandAlone && pGraph->pLiveImageLink)
				{
					estrPrintf(&pLinks->pLiveLink, 	"<span><a href=\"/viewimage?imageName=LOGPARSER_0_GRAPH_LIVE_%s.jpg\" target=\"_blank\">Graph</a></span> <span><a href=\"%s%s.LIVE.%s\">CSV</a></span>\n", pGraph->pGraphName,
						LinkToThisServer(), CSV_DOMAIN_NAME, pGraph->pGraphName);					
				}

				if (!gbStandAlone && pGraph->pDefinition->bLongTermGraph)
				{
					estrPrintf(&pLinks->pLongTermLink, 	"<span><a href=\"/viewimage?imageName=LOGPARSER_0_GRAPH_%sLongTerm.jpg\" target=\"_blank\">Graph</a></span> <span><a href=\"%s%s.MAIN.%sLongTerm\">CSV</a></span>\n", pGraph->pGraphName,
						LinkToThisServer(), CSV_DOMAIN_NAME, pGraph->pGraphName);		
				}
				*/

				pLinks->iMostRecentDataTime = pGraph->iMostRecentDataTime;
				pLinks->iNumDataPoints = pGraph->iUniqueDataPoints;			

				if (gbStandAlone && !pGraph->bActiveInStandaloneMode)
				{
					if (gbStandAlone)
					{
						estrPrintf(&pLinks->pSwitch, "<a href=\"/directcommand?command=ActivateGraph %s\" class=\"js-button\">Activate</a>",
							pGraph->pGraphName);
					}
					eaPush(&gpLogParserOverview->ppInactiveGraphs, pLinks);
				}
				else
				{
					if (gbStandAlone)
					{
						estrPrintf(&pLinks->pSwitch, "<a href=\"/directcommand?command=InactivateGraph %s\" class=\"js-button\">Deactivate</a>",
							pGraph->pGraphName);
					}					
					eaPush(&gpLogParserOverview->ppGraphs, pLinks);
				}

			}
		}
	}

	if (LogParserLaunching_IsActive())
	{
		estrPrintf(&gpLogParserOverview->pLaunchStandAlone, "LaunchStandaloneAndReturnLink $USERNAME $NOCONFIRM");
	}


	if (gbStandAlone)
	{
		int i;
		
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetDatesToSearch_LocalGimmeTime, "SetSearchTimes_LocalGimmeTime $STRING(From) $STRING(TO) $CONFIRM(Set restricted search time? Use MMDDYYHH{:MM{:SS}})");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetDatesToSearch_SecsSince2000, "SetSearchTimes_SecsSince2000 $INT(From) $INT(TO) $CONFIRM(Set restricted search time in SecsSince2000?)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetDatesToSearch_UtcLogDate, "SetSearchTimes_UtcLogDate \"$STRING(From)\" \"$STRING(TO)\" $CONFIRM(Set restricted search time? Use YYMMDD HH:MM:SS)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetFilenamesToMatch, "SetFileNameRestrictions $STRING(comma separated list of names to match) $CONFIRM(Directory scanning will only load files that match one of these names)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetDirectoriesToScan_Precise, "SetDirectoriesToScan_Precise $STRING(comma separated list of directories to scan) $CONFIRM(Sets which directories directory scanning will scan)");
		estrPrintf(&gpLogParserOverview->pScanNow, "BeginDirectoryScanning $CONFIRM(Scan directories for new log files now? WARNING: This will erase all currently loaded data and replace it)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetDatesToSearch_Easy, "SetSearchTimes_Easy $SELECT(Time|");
		estrPrintf(&gpLogParserOverview->pScanBins, "LoadLogsFromBin $CONFIRM(Scan locally created bin files to redo last full scan?) $NORETURN");
		estrPrintf(&gpLogParserOverview->pSaveConfiguration, "SaveAndNameStandaloneOptions $STRING(Name of this setup configuration?) $NORETURN");
		estrPrintf(&gpLogParserOverview->pLoadConfiguration, "LoadStandAloneOptions $SELECT(Which config file?|NAMELIST_ConfigFilesNameList) $NORETURN");
		estrPrintf(&gpLogParserOverview->pKillStandAlone, "KillStandAlone $CONFIRM(Kill this StandAlone LogParser?)");
		estrPrintf(&gpLogParserOverview->pLongTimeout, "LongLogParserTimeout $CONFIRM(Increase this StandAlone LogParsers timeout to 36 hours?)");

		if (gbStandAloneLogParserNeverTimeOut)
		{
			estrPrintf(&gpLogParserOverview->pCancelNeverTimeOut, "NeverTimeOut 0 $NORETURN");
		}
		else
		{
			estrPrintf(&gpLogParserOverview->pNeverTimeOut, "NeverTimeOut 1 $CONFIRM(Don't time out due to inactivity... for 3 days) $NORETURN");
		}

		for (i=0; i < SEARCHTIME_LAST; i++)
		{
			estrConcatf(&gpLogParserOverview->pStandAloneOptions->pSetDatesToSearch_Easy, "%s%s", 
				StaticDefineIntRevLookup(enumSearchTimeTypeEnum, i), i == SEARCHTIME_LAST - 1 ? ")" : ",");
		}

		estrConcatf(&gpLogParserOverview->pStandAloneOptions->pSetDatesToSearch_Easy, "$NORETURN $CONFIRM(Sets restricted search time)");

		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetDirectoriesToScan_Easy, "SetDirectoriesToScan_Easy $SELECT(Where To Scan|");

		for (i=0; i < eaSize(&gLogParserConfig.ppCommonSearchLocations); i++)
		{
			estrConcatf(&gpLogParserOverview->pStandAloneOptions->pSetDirectoriesToScan_Easy, "%s%s",
				gLogParserConfig.ppCommonSearchLocations[i]->pName, i == eaSize(&gLogParserConfig.ppCommonSearchLocations) - 1? ")" : ",");
		}

		estrConcatf(&gpLogParserOverview->pStandAloneOptions->pSetDirectoriesToScan_Easy, " $CONFIRM(Set where to look for log files)");

//		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetMapToScan, "SetMapToScan $SELECT(MapName|NAMELIST_MapNameList) $CONFIRM(Set which map to process logs from)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pAddMapRestriction, "AddMapRestriction $SELECT(MapName|NAMELIST_MapNameList) $CONFIRM(Add a map restriction)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pClearMapRestrictions, "ClearMapRestrictions $CONFIRM(Clear map restrictions)");

		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pAddObjectRestriction, "AddObjectRestriction \"$STRING(Object Name)\" $CONFIRM(Add an object name restriction)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pClearObjectRestrictions, "ClearObjectRestrictions $CONFIRM(Clear object restrictions)");

		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pAddActionRestriction, "AddActionRestriction $STRING(Action Name) $CONFIRM(Add an Action restriction)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pClearActionRestrictions, "ClearActionRestrictions $CONFIRM(Clear Action restrictions)");

		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pAddOwnerRestriction, "AddOwnerRestriction \"$STRING(Owner Name)\" $CONFIRM(Add an Owner name restriction)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pClearOwnerRestrictions, "ClearOwnerRestrictions $CONFIRM(Clear Owner restrictions)");

		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pAddPlayerRestriction, "AddPlayerRestriction \"$STRING(Owner Name)\" $CONFIRM(Add an Owner name restriction)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pClearPlayerRestrictions, "ClearPlayerRestrictions $CONFIRM(Clear Owner restrictions)");

		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetExpressionRestriction, "SetExpressionRestriction \"$STRING(expression)\" $CONFIRM(Set the expression restriction)");

		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetSubstringRestriction, "SetSubstringRestriction \"$STRING(expression)\" $CONFIRM(Set the substring restriction)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetSubstringInverseRestriction, "SetSubstringInverseRestriction \"$STRING(expression)\" $CONFIRM(Set the inverse substring restriction)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetSubstringCaseSensitiveRestriction, "SetSubstringCaseSensitiveRestriction \"$STRING(expression)\" $CONFIRM(Set the substring restriction)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetSubstringCaseSensitiveInverseRestriction, "SetSubstringCaseSensitiveInverseRestriction \"$STRING(expression)\" $CONFIRM(Set the inverse substring restriction)");

		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetRegexRestriction, "SetRegexRestriction \"$STRING(expression)\" $CONFIRM(Set the regex restriction)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetRegexInverseRestriction, "SetRegexInverseRestriction \"$STRING(expression)\" $CONFIRM(Set the inverse regex restriction)");

		estrPrintf(&gpLogParserOverview->pAddRunTimeConfig, "AddRunTimeConfig $STRINGBOX(Stuff from your local logparserconfig.txt or the equivalent)");

		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pAddNewPlayerFilterCategory, "AddPlayerFilter \"$STRING(Filter Name)\" \"$INT(Min Level) $INT(Max Level) $CHECKBOX(Use AccessLevel restrictions) $INT(Min AccessLevel) $INT(Max AccessLevel) $CHECKBOX(Balanced) $CHECKBOX(Offense) $CHECKBOX(Defense) $CHECKBOX(Support) $INT(Min group size) $INT(Max group size)\" $CONFIRM(Add a new player filtering category?)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pAddPlayerExclusionFilter, "AddPlayerExclusionFilter $INT(Min Level) $INT(Max Level) $CHECKBOX(Use AccessLevel restrictions) $INT(Min AccessLevel) $INT(Max AccessLevel) $CHECKBOX(Balanced) $CHECKBOX(Offense) $CHECKBOX(Defense) $CHECKBOX(Support) $INT(Min team size) $INT(Max team size) $CONFIRM(Add a new player exclusion filter?)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pAddNewGameServerFilterCategory, "AddGameServerFilter \"$STRING(Filter Name)\" $CONFIRM(Add a new gameserver filtering category?)");
		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pAddNewGameServerExclusionFilter, "AddGameServerExclusionFilter $CONFIRM(Add a new gameserver exclusion category?)");

		if (gStandAloneOptions.bCreateFilteredLogFile)
		{
			estrPrintf(&gpLogParserOverview->pStandAloneOptions->pStopCreatingFilteredFile, "StartCreatingFilteredFile 0 $NORETURN");
			estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetFilteredFileIncludesProcedural, "SetFilteredFileIncludesProcedural $INT(Should filtered files include procedural logs) $NORETURN");
			estrDestroy(&gpLogParserOverview->pStandAloneOptions->pBeginCreatingFilteredFile);
		}
		else
		{
			estrPrintf(&gpLogParserOverview->pStandAloneOptions->pBeginCreatingFilteredFile, "StartCreatingFilteredFile 1 $NORETURN");
			estrDestroy(&gpLogParserOverview->pStandAloneOptions->pStopCreatingFilteredFile);
			estrDestroy(&gpLogParserOverview->pStandAloneOptions->pSetFilteredFileIncludesProcedural);
		}

		if (gStandAloneOptions.bCreateBinnedLogFiles)
		{
			estrPrintf(&gpLogParserOverview->pStandAloneOptions->pStopCreatingBinnedFile, "StartCreatingBinnedFile 0 $NORETURN");
			estrDestroy(&gpLogParserOverview->pStandAloneOptions->pBeginCreatingBinnedFile);
		}
		else
		{
			estrPrintf(&gpLogParserOverview->pStandAloneOptions->pBeginCreatingBinnedFile, "StartCreatingBinnedFile 1 $NORETURN");
			estrDestroy(&gpLogParserOverview->pStandAloneOptions->pStopCreatingBinnedFile);
		}

		if (gStandAloneOptions.bCompressFilteredLogFile)
		{
			estrPrintf(&gpLogParserOverview->pStandAloneOptions->pDontCompressFilteredFile, "CompressFilteredFile 0 $NORETURN");
			estrDestroy(&gpLogParserOverview->pStandAloneOptions->pCompressFilteredFile);
		}
		else
		{
			estrPrintf(&gpLogParserOverview->pStandAloneOptions->pCompressFilteredFile, "CompressFilteredFile 1 $NORETURN");
			estrDestroy(&gpLogParserOverview->pStandAloneOptions->pDontCompressFilteredFile);
		}

		estrPrintf(&gpLogParserOverview->pStandAloneOptions->pSetBinnedLogFileDirectory, "SetBinnedLogFileDirectory \"$STRING(directory)\" $CONFIRM(Set the directory) $NORETURN");

		if (FilteredLogFile_GetRecentFileName())
		{
			estrPrintf(&gpLogParserOverview->pStandAloneOptions->pDownloadLink, "<a href=\"%s\">Download Filtered Log File</a>", FilteredLogFile_GetDownloadPath());
		}
		else
		{
			estrDestroy(&gpLogParserOverview->pStandAloneOptions->pDownloadLink);
		}

	}
	else
	{
		estrPrintf(&gpLogParserOverview->pDumpLongTermCSVs, "Graph_DumpLongTermCSVs $NORETURN");
	}
	

	return gpLogParserOverview;

}
	char *pWarning; AST(ESTRING, FORMATSTRING(HTML_CLASS="structheader"))
	char **ppLinks; AST(FORMATSTRING(HTML=1, HTML_NO_HEADER=1))
AUTO_STRUCT;
typedef struct ScanningStatusStruct
{
	char *pStatus; AST(ESTRING, FORMATSTRING(HTML_CLASS="structheader", HTML_NO_HEADER=1))
	AST_COMMAND("Abort scanning", "AbortDirectoryScanning $NORETURN")
} ScanningStatusStruct;

ScanningStatusStruct scanningStatusStruct = {0};

void OVERRIDE_LATELINK_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	if (gbCurrentlyScanningDirectories)
	{
		estrPrintf(&scanningStatusStruct.pStatus, "Currently scanning for files, %d%% complete. %s", 
			giDirScanningPercent, gpDirScanningStatus);
		*ppTPI = parse_ScanningStatusStruct;
		*ppStruct = &scanningStatusStruct;

	}
	else
	{
		*ppTPI = parse_LogParserOverview;
		*ppStruct = GetLogParserOverview();
	}

}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void AddRunTimeConfig(ACMD_SENTENCE pStr)
{
	estrDestroy(&gpRunTimeConfig);
	estrAppendUnescaped(&gpRunTimeConfig, pStr);
	LogParser_FullyResetEverything();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void ApplyEditToLogParserOptions(ACMD_SENTENCE pTransactionString)
{
	objPathParseAndApplyOperations(parse_LogParserOverview, gpLogParserOverview, pTransactionString);
	SaveStandAloneOptions(NULL);
}



#include "LogParser_Http_c_ast.c"
