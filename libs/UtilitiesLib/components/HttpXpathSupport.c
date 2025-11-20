#include "HttpXpathSupport.h"
#include "autogen/HttpXpathSupport_h_ast.h"
#include "autogen/HttpXpathSupport_c_ast.h"
#include "objpath.h"
#include "CachedGlobalObjectList.h"

#include "cmdparse_http.h"
#include "cmdparse.h"
#include "filteredList.h"
#include "stringcache.h"
#include "structnet.h"
#include "ResourceInfo.h"
#include "HttpJpegLibrary.h"
#include "GlobalComm.h"
#include "ControllerLink.h"
#include "StringUtil.h"
#include "UtilitiesLib.h"
#include "url.h"
#include "AutoGen/url_h_ast.h"
#include "windefinclude.h"
#include "ugcProjectUtils.h"
#include "Logging.h"
#include "../../libs/HttpLib/httpServing.h"
#include "rand.h"
#include "ResourceSystem_Internal.h"
#include "globalEnums.h"

int gMaxDictSizeForNormalBrowsing = 10000;

void HttpSetMaxDictSizeForNormalBrowsing(int iVal)
{
	gMaxDictSizeForNormalBrowsing = iVal;
}


int siAccessLevel = 0;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););


typedef struct
{
	char *pDomainName;
	CustomXpathProcessingCB_Immediate *pCBImmediate;
	CustomXpathProcessingCB_Delayed *pCBDelayed;
} CustomDomainInfo;

static CustomDomainInfo **ppCustomDomains = NULL;



//returns a string containing an HTTP link to the server on which it is run, ie,
// "/xpath=GameServer[17]"
char *LinkToThisServer(void)
{
	static char *buf = NULL;
	ATOMIC_INIT_BEGIN;
	estrPrintf(&buf, "/viewxpath?xpath=%s[%u]", GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID());
	ATOMIC_INIT_END;
	return buf;
}

char *GetAlternateDictionaryKeyForServerMonitoring(const char *pDictName, const char *pKey)
{
	static char retVal[64];
	if (stricmp(pDictName, "UGCProject") == 0 || stricmp(pDictName, "CopyDict_UGCProject") == 0)
	{
		U32 iID;
		bool bIsSeries;
		if (UGCIDString_StringToInt(pKey, &iID, &bIsSeries) && !bIsSeries)
		{
			sprintf(retVal, "%d", iID);
			return retVal;
		}
	}

	return NULL;
}








//---------------------------------------------------------------------------------
//stuff for parsing servermonitor command lines in the ".globObj." domain




#define charIsOkForFieldName(c) (isalnum(c) || (c) == '_' || (c) == ':' || (c) == '.' || (c) == '[' || (c) == ']' || (c) == '"' || (c) == '(' || (c) == ')')
void ExtractFieldsToShow(char ***pppFieldsToReturn, const char *pInString)
{
	const char *pReadHead = pInString;
	char *pTempString = NULL; //estring

	estrStackCreate(&pTempString);

	do
	{
		int fstart = 0;
		while (*pReadHead && !charIsOkForFieldName(*pReadHead))
		{
			pReadHead++;
		}

		if (!(*pReadHead))
		{	//No more valid characters before end of string, end.
			break;
		}

		while (*pReadHead && charIsOkForFieldName(*pReadHead))
		{
			estrConcatChar(&pTempString, *pReadHead);
			pReadHead++;
		}

		assert(pTempString);

		if (pTempString[fstart] == '(') 
		{
			fstart = 1;
			if (pReadHead[-1] != ')')
			{	//parens don't close properly, bail out.
				break;
			}
		}

		if (pTempString[fstart] != '.')
		{
			estrInsert(&pTempString, fstart, ".", 1);
		}

		eaPush(pppFieldsToReturn, strdup(pTempString));
		estrSetSize(&pTempString, 0);
	} while (*pReadHead);

	estrDestroy(&pTempString);
}



/*the string passed in will be in one of three forms:
dictionaryName
dictionaryName[itemName]
dictionaryName[itemName].xpath

returns true on success, false on failure
*/
typedef struct FilteredListWrapper FilteredListWrapper;

AUTO_STRUCT AST_RUNTIME_MODIFIED;
struct FilteredListWrapper
{
	char *pLinkToThisList; AST(ESTRING, FORMATSTRING(HTML=1))
	char *pExtraLink; AST(FORMATSTRING(HTML=1, HTML_NO_HEADER=1), ESTRING)
	FilteredListWrapper *pList; //dummy type
	char *pComment; AST(FORMATSTRING(HTML=1,HTML_NO_HEADER=1), POOL_STRING)
	char *pExtraCommand; AST(FORMATSTRING(command=1), ESTRING)
};

int gHttpXpathListCutoffSize = 200;
AUTO_CMD_INT(gHttpXpathListCutoffSize, ListCutoffSize) ACMD_COMMANDLINE;


AUTO_STRUCT;
typedef struct DefaultFieldsForTable
{
	char *pTableName; AST(KEY STRUCTPARAM)
	char *pDefaultFields; AST(STRUCTPARAM)
} DefaultFieldsForTable;

AUTO_STRUCT;
typedef struct DefaultFieldsList
{
	DefaultFieldsForTable **ppDefaultFields;
} DefaultFieldsList;

char *GetDefaultFieldsString(const char *pTableName)
{
	static DefaultFieldsList *spList = NULL;
	DefaultFieldsForTable *pFields;

	if (!spList)
	{
		spList = StructCreate(parse_DefaultFieldsList);
		ParserLoadFiles(GetDirForBaseConfigFiles(), "DefaultServerMonitoringFields.txt", NULL, 0, parse_DefaultFieldsList, spList);
	}

	pFields = eaIndexedGetUsingString(&spList->ppDefaultFields, pTableName);

	if (pFields)
	{
		return pFields->pDefaultFields;
	}

	return NULL;
}




bool ProcessGlobalObjectIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	char *pFirstLeftBracket = strchr(pLocalXPath, '[');
	char *pFirstRightBracket;
	char dictionaryName[256];
	const char *pFilterString;
	const char *pFieldsToShowString;
	int limit = gHttpXpathListCutoffSize;
	int offset = 0;
	ParseTable *pGlobObjTPI;

	void *pObject;
	char *pObjectName;
	char *pRandomObjectName = NULL;
	Container *con = NULL;


	if (!pFirstLeftBracket)
	{
		GenericListOfLinksForHttpXpath listOfLinks = {0};
		ResourceIterator iterator;
		int iTotalCount;
		strcpy_trunc(dictionaryName, pLocalXPath);

		iTotalCount = resDictGetNumberOfObjects(dictionaryName);

		if (iTotalCount > gMaxDictSizeForNormalBrowsing && !stricmp(urlFindSafeValue(pArgList, "svrOverrideMaxDictSize"), "yes") == 0)
		{
			char *pErrorString = NULL;
			estrPrintf(&pErrorString, "RefDict %s can not be browsed, as it contains %d objects. Max limit for browsing is %d. To look at a specific object, add [ID] to the current URL",
				dictionaryName, iTotalCount, gMaxDictSizeForNormalBrowsing);
			GetMessageForHttpXpath(pErrorString, pStructInfo, false);
			estrDestroy(&pErrorString);
			return true;
		}


		pGlobObjTPI = resDictGetParseTable(dictionaryName);

		if (!pGlobObjTPI)
		{
			return false;
		}

		pFilterString = urlFindValue(pArgList, "svrFilter");
		pFieldsToShowString = urlFindValue(pArgList, "svrFieldsToShow");

		if (resDictGetFlags(dictionaryName) & RESDICTLFAG_NO_PAGINATION)
		{
			limit = 0;
			offset = 0;
		}
		else
		{
			if (urlFindBoundedInt(pArgList, "svrLimit", &limit, 0, INT_MAX) < 0)
				limit = gHttpXpathListCutoffSize;

			if (urlFindBoundedInt(pArgList, "svrOffset", &offset, 0, INT_MAX) < 0)
				offset = 0;
		}

		listOfLinks.offset = offset;
		listOfLinks.limit = limit;

		if (pFilterString && StringIsAllWhiteSpace(pFilterString))
		{
			pFilterString = NULL;
		}

		if (!pFieldsToShowString || StringIsAllWhiteSpace(pFieldsToShowString))
		{
			if ((pFieldsToShowString = GetDefaultFieldsString(dictionaryName)))
			{
			}			
			else if ((pFieldsToShowString = GetDefaultFieldsString(ParserGetTableName(pGlobObjTPI))))
			{
			}
			else if (!GetStringFromTPIFormatString(&pGlobObjTPI[0], "HTML_DEF_FIELDS_TO_SHOW", &pFieldsToShowString))
			{
				pFieldsToShowString = NULL;
			}
		}

		//if we have a filter string or fields to show, then we need to do the fancy-pants filtered list thing
		if (pFilterString || pFieldsToShowString)
		{
			char **ppFieldsToReturn = NULL;
			FilteredList *pFilteredList;
			char *pLinkEString = NULL;
			char *pTitleEString = NULL;
			char **ppNamesOfFoundObjects = NULL;
			int iCachedListID;
			enumResDictFlags eDictFlags = resDictGetFlags(dictionaryName);


			FilteredListWrapper filteredListWrapper = {0};

			estrStackCreate(&pLinkEString);
			estrStackCreate(&pTitleEString);

			//if fieldsToShow is unspecified, then display just the key field (handled in FilteredList.c)
			if (pFieldsToShowString)
			{
				//special tokenizing that transparently works with various types of 
				//separation (comma, semicolon, space), and also prefixes period
				ExtractFieldsToShow(&ppFieldsToReturn, pFieldsToShowString);
			}

			if (!pFilterString)
			{
				pFilterString = "1";
			}

			if (!(eDictFlags & RESDICTFLAG_NOLINKINSERVERMONITOR))
			{
				estrPrintf(&pLinkEString, "<a href=\"%s%s%s[%%s]\">%%s</a>",
						LinkToThisServer(),
						GLOBALOBJECTS_DOMAIN_NAME,
						dictionaryName);
			}


			estrPrintf(&pTitleEString, "Filtered search results from global dictionary %s (in-code struct name: %s)", 
				dictionaryName, ParserGetTableName(pGlobObjTPI));

			PERFINFO_AUTO_START("GetFilteredListOfObjects",1);
			pFilteredList = GetFilteredListOfObjects(pTitleEString, pFilterString, ppFieldsToReturn,
				pGlobObjTPI, GenericIteratorBegin_GlobObj, GenericIteratorGetNext_GlobObj, dictionaryName, pLinkEString, &ppNamesOfFoundObjects, limit, offset, true);
			PERFINFO_AUTO_STOP();

			if (eaSize(&ppNamesOfFoundObjects) && strcmp(pFilterString, "1") != 0)
			{
				char *pCacheName = NULL;
				estrStackCreate(&pCacheName);
				estrPrintf(&pCacheName, "Cached list of %d objects of type %s, filtered by \"%s\"",
					eaSize(&ppNamesOfFoundObjects), dictionaryName, pFilterString);

				iCachedListID = CreateCachedGlobalObjectList(pCacheName, dictionaryName, &ppNamesOfFoundObjects);
				estrPrintf(&filteredListWrapper.pLinkToThisList, "<a href=\"%s%sCachedGlobObjList[%d]\">Apply operations to this search result</a>",
						LinkToThisServer(),
						GLOBALOBJECTS_DOMAIN_NAME,
						iCachedListID);
				estrDestroy(&pCacheName);
			}

			eaDestroyEx(&ppNamesOfFoundObjects, NULL);

			estrDestroy(&pLinkEString);
			estrDestroy(&pTitleEString);
			eaDestroyEx(&ppFieldsToReturn, NULL);

			filteredListWrapper.pList = pFilteredList->pObject;
			assert(stricmp(parse_FilteredListWrapper[4].name, "List") == 0);
			parse_FilteredListWrapper[4].subtable = pFilteredList->pTPI;

			filteredListWrapper.pComment = resDictGetHTMLCommentString(dictionaryName);
			resDictGetHTMLExtraCommand(dictionaryName, &filteredListWrapper.pExtraCommand);
			estrCopy2(&filteredListWrapper.pExtraLink, resDictGetHTMLExtraLink(dictionaryName));

			assert(ProcessStructIntoStructInfoForHttp("", pArgList, &filteredListWrapper, parse_FilteredListWrapper, iAccessLevel, SERVERMON_FIXUP_ALREADY_DONE, pStructInfo, eFlags));

			DestroyFilteredList(pFilteredList);

			estrDestroy(&filteredListWrapper.pLinkToThisList);
	

			return true;


		}


		sprintf(listOfLinks.ListName, "Global Object Type \"%s\"", dictionaryName);

		if (!resInitIterator(dictionaryName, &iterator))
		{
			return false;
		}

		while (resIteratorGetNext(&iterator, &pObjectName, &pObject))
		{
			//if (iDefaultListSize <= gListCutoffSize || bOverrideCutoff)
			//{
				GenericLinkForHttpXpath *pLink = StructCreate(parse_GenericLinkForHttpXpath);
				estrPrintf(&pLink->pLink, "<a href=\"%s%s%s[%s]\">%s</a>",
					LinkToThisServer(),
					GLOBALOBJECTS_DOMAIN_NAME,
					dictionaryName, pObjectName, pObjectName);
				estrPrintf(&pLink->pName, "%s", pObjectName);
				eaPush(&listOfLinks.ppLinks, pLink);
			//}
			//iDefaultListSize++;
		}
		resFreeIterator(&iterator);

		//default sorting.
		eaStableSortUsingColumn(&listOfLinks.ppLinks, parse_GenericLinkForHttpXpath, PARSE_GENERICLINKFORHTTPXPATH_NAME_INDEX);

		if (offset)
		{
			int i;
			if (offset > eaSize(&listOfLinks.ppLinks))
			{
				offset = eaSize(&listOfLinks.ppLinks);
			}
			for (i = 0; i < offset; i++)
			{
				StructDestroy(parse_GenericLinkForHttpXpath,listOfLinks.ppLinks[i]);
			}
			eaRemoveRange(&listOfLinks.ppLinks, 0, offset);
		}
		if (limit)
		{
			U32 size = eaSize(&listOfLinks.ppLinks);
			int more = size - limit;
			if (more > 0) 
			{
				U32 i;
				listOfLinks.more = more;
				for (i = limit; i < size; i++)
				{
					StructDestroy(parse_GenericLinkForHttpXpath,listOfLinks.ppLinks[i]);
				}
				eaRemoveRange(&listOfLinks.ppLinks, limit, more);
			}
		}
		listOfLinks.count = eaSize(&listOfLinks.ppLinks);

		siAccessLevel = iAccessLevel;

		//if (iDefaultListSize > gListCutoffSize && !bOverrideCutoff)
		//{
		//	GenericLinkForHttpXpath *pLink = StructCreate(parse_GenericLinkForHttpXpath);
		//	estrClear(&pLink->pLink);
		//	estrPrintf(&pLink->pLink, "Warning: Dictionary \"%s\" contains %d more elements. [<a href=\"%s%s%s&",
		//		dictionaryName, iDefaultListSize - gListCutoffSize, LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME, dictionaryName);
		//	urlAppendQueryStringWithOverrides(pArgList, &pLink->pLink, 1, DEFAULT_LIST_CUTOFF_OVERRIDE, "1");
		//	estrConcatf(&pLink->pLink,"\">Show All</a>]");

		//	//eaClearStruct(&listOfLinks.ppLinks,parse_GenericLinkForHttpXpath);
		//	eaPush(&listOfLinks.ppLinks, pLink);
		//}

		listOfLinks.pComment = resDictGetHTMLCommentString(dictionaryName);
		resDictGetHTMLExtraCommand(dictionaryName, &listOfLinks.pExtraCommand);
		estrCopy2(&listOfLinks.pExtraLink, resDictGetHTMLExtraLink(dictionaryName));

		if (!ParseTableWriteText(&pStructInfo->pTPIString, parse_GenericListOfLinksForHttpXpath, "GenericListOfLinks", PARSETABLESENDFLAG_FOR_HTTP_EXTERNAL))
		{
			assertmsg(0, "ParseTableWriteText failed");
		}

		if (!ParserWriteText(&pStructInfo->pStructString, parse_GenericListOfLinksForHttpXpath, &listOfLinks, WRITETEXTFLAG_FORCEWRITECURRENTFILE | WRITETEXTFLAG_USEHTMLACCESSLEVEL, 0, 0))
		{
			assertmsg(0, "ParserWriteText failed");
		}
		

		pStructInfo->bIsRootStruct = true;

		StructDeInit(parse_GenericListOfLinksForHttpXpath, &listOfLinks);
	
		return true;
	}
	
	pFirstRightBracket = strchr(pLocalXPath, ']');

	if (!pFirstRightBracket)
	{
		return false;
	}

	*pFirstLeftBracket = 0;

	strcpy_trunc(dictionaryName, pLocalXPath);
	pGlobObjTPI = resDictGetParseTable(dictionaryName);

	*pFirstLeftBracket = '[';

	if (!pGlobObjTPI)
	{
		return false;
	}

	/*if (!RefSystem_DictionaryCanBeBrowsed(hHandle))
	{
		GetMessageForHttpXpath(STACK_SPRINTF("Reference Dictionary %s can not be browsed, as it has no built-in TPI",
			RefSystem_GetDictionaryNameFromNameOrHandle(hHandle)), pStructInfo);
		return true;
	}*/


	*pFirstRightBracket = 0;

	//special case... [] means just give me one of those at random (actually random from among the first
	//100 youc an iterate through, to avoid super slowness
	if (StringIsAllWhiteSpace(pFirstLeftBracket + 1))
	{
		int iNumObjects = resDictGetNumberOfObjects(dictionaryName);
		pObject = NULL;

		if (iNumObjects)
		{
			int iMyObjectIndex;
			ResourceIterator iter = {0};
			pObject = NULL;

			if (iNumObjects > 100)
			{
				iNumObjects = 100;
			}
		
			iMyObjectIndex = randomIntRange(1, iNumObjects);

			resInitIterator(dictionaryName, &iter);

			for (;iMyObjectIndex; iMyObjectIndex--)
			{
				resIteratorGetNext(&iter, &pRandomObjectName, &pObject);
			}

			resFreeIterator(&iter);
		}


	}
	else
	{
		ResourceDictionary * resDict = resGetDictionary(dictionaryName);
		if(resDict->pDictCategoryName == allocAddString(RESCATEGORY_CONTAINER))
		{
			con = objGetContainerEx(NameToGlobalType(dictionaryName), atoi(pFirstLeftBracket + 1), true, false, true);
		}
		pObject = resGetObject(dictionaryName, pFirstLeftBracket + 1);
	}

	if (!pObject)
	{
		char *pAlternateKey = GetAlternateDictionaryKeyForServerMonitoring(dictionaryName, pFirstLeftBracket + 1);
		if (pAlternateKey)
		{
			ResourceDictionary * resDict = resGetDictionary(dictionaryName);
			if(resDict->pDictCategoryName == allocAddString(RESCATEGORY_CONTAINER))
			{
				con = objGetContainerEx(NameToGlobalType(dictionaryName), atoi(pFirstLeftBracket + 1), true, false, true);
			}
			pObject = resGetObject(dictionaryName, pAlternateKey);
		}

		if (!pObject)
		{

			GetMessageForHttpXpath(STACK_SPRINTF("Global Object Dictionary %s has no member named %s.",
				dictionaryName, pFirstLeftBracket + 1), pStructInfo, true);
			*pFirstRightBracket = ']';
			return true;
		}
	}
	

	pLocalXPath = pFirstRightBracket + 1;

	if (pLocalXPath[0])
	{
		*pFirstRightBracket = ']';
		if (strStartsWith(pLocalXPath, ".Struct"))
		{
			bool retVal;
			pLocalXPath += strlen(".Struct");
			retVal = ProcessStructIntoStructInfoForHttp(pLocalXPath, pArgList, pObject, pGlobObjTPI, iAccessLevel, 0, pStructInfo, eFlags);
			if(con)
				objUnlockContainer(&con);
			return retVal;
		}
		else
		{
			GetMessageForHttpXpath("Parse error... expected .Struct", pStructInfo, true);
			if(con)
				objUnlockContainer(&con);
			return true;
		}

	}
	else
	{
		HttpGlobObjWrapper wrapperStruct = {0};

		assert(strcmp(parse_HttpGlobObjWrapper[3].name, "Struct") == 0);
		parse_HttpGlobObjWrapper[3].subtable = pGlobObjTPI;
		parse_HttpGlobObjWrapper[3].param = 0;

		estrPrintf(&wrapperStruct.pLabel, "%sGlobal Object %s in dictionary %s", pRandomObjectName ? "(RANDOMLY SELECTED) " : "", pRandomObjectName ? pRandomObjectName : pFirstLeftBracket + 1, dictionaryName);

		wrapperStruct.pStruct = pObject;

		assert(ProcessStructIntoStructInfoForHttp("", pArgList, &wrapperStruct, parse_HttpGlobObjWrapper, iAccessLevel, 0, pStructInfo, eFlags));

		estrDestroy(&wrapperStruct.pLabel);

		*pFirstRightBracket = ']';
		if(con)
			objUnlockContainer(&con);
		return true;
	}
}


















void DEFAULT_LATELINK_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	*ppTPI = NULL;
	*ppStruct = NULL;
}


void DEFAULT_LATELINK_GetCommandsForGenericServerMonitoringPage(GenericServerInfoForHttpXpath *pInfo)
{

}


GenericServerInfoForHttpXpath *GetGenericServerInfo(int iAccessLevel)
{
	static GenericServerInfoForHttpXpath *pInfo = NULL;
	static U32 iTimeStamp = 0;
	int i;
	int iNumCommandCategories = GetNumCommandCategories();

	ResourceDictionaryIterator resDictIterator;
	ResourceDictionaryInfo *pGlobDictInfo;

	static const char *spRefDict_Alloced = NULL;
	static const char *spArt_Alloced = NULL;
	static const char *spDesign_Alloced = NULL;
	static const char *spContainer_Alloced = NULL;
	static const char *spSystem_Alloced = NULL;

	if (pInfo && timeSecondsSince2000() - iTimeStamp < 3)
	{
		return pInfo;
	}

	if (!spRefDict_Alloced)
	{
		spRefDict_Alloced = allocAddString(RESCATEGORY_REFDICT);
		spArt_Alloced = allocAddString(RESCATEGORY_ART);
		spDesign_Alloced = allocAddString(RESCATEGORY_DESIGN);
		spContainer_Alloced = allocAddString(RESCATEGORY_CONTAINER);
		spSystem_Alloced = allocAddString(RESCATEGORY_SYSTEM);
	}


	if (pInfo)
	{
		StructDestroy(parse_GenericServerInfoForHttpXpath, pInfo);
	}

	pInfo = StructCreate(parse_GenericServerInfoForHttpXpath);

	for (i=0; i < eaSize(&ppCustomDomains); i++)
	{
		estrConcatf(&pInfo->pDomainLinks, "%s<a href=\"%s%s\">%s</a>",
						i == 0 ? "" : ", ",
						LinkToThisServer(),
						ppCustomDomains[i]->pDomainName,
						ppCustomDomains[i]->pDomainName);
	}

	estrCopy2(&pInfo->pLoggingStatus, GetLoggingStatusString());

	sprintf(pInfo->serverType, "%s (version: %s)", GlobalTypeToName(GetAppGlobalType()), GetUsefulVersionString());
	estrPrintf(&pInfo->pCommandLine, "%s", GetCommandLine());

//	estrPrintf(&pInfo->pAccessLevel, "You are servermonitoring with AccessLevel: %d", iAccessLevel);

	pInfo->iID = GetAppGlobalID();

	resDictInitIterator(&resDictIterator);

	while ((pGlobDictInfo = resDictIteratorGetNextInfo(&resDictIterator)))
	{
		int iNumObjects;

		if (resDictGetFlags(pGlobDictInfo->pDictName) & RESDICTFLAG_HIDE)
		{
			continue;
		}

		iNumObjects = resDictGetNumberOfObjects(pGlobDictInfo->pDictName);

		if (iNumObjects && pGlobDictInfo->pDictTable)
		{
			GlobObjWrapper *pGlobObjWrapper = StructCreate(parse_GlobObjWrapper);
			pGlobObjWrapper->iNumObjects = iNumObjects;
		
			estrPrintf(&pGlobObjWrapper->pName, "<a href=\"%s%s%s\">%s</a>",
				LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME, pGlobDictInfo->pDictName, pGlobDictInfo->pDictName);

			if (pGlobDictInfo->pDictCategoryName == spRefDict_Alloced || pGlobDictInfo->pDictCategoryName == spArt_Alloced || pGlobDictInfo->pDictCategoryName == spDesign_Alloced)
			{
				eaPush(&pInfo->ppRefDicts, pGlobObjWrapper);
			}
			else if (pGlobDictInfo->pDictCategoryName == spContainer_Alloced)
			{
				eaPush(&pInfo->ppContainers, pGlobObjWrapper);
			} 
			else if (pGlobDictInfo->pDictCategoryName == spSystem_Alloced)
			{
				eaPush(&pInfo->ppSystemObjects, pGlobObjWrapper);
			} 
			else
			{
				eaPush(&pInfo->ppMiscObjects, pGlobObjWrapper);
			}
		}
	}

	
	for (i=0; i < iNumCommandCategories; i++)
	{
		const char *pCategoryName = GetNthCommandCategoryName(i);
		CommandCategoryWrapper *pCatWrapper = StructCreate(parse_CommandCategoryWrapper);
		estrPrintf(&pCatWrapper->pName, "<a href=\"%s%s%s\">%s commands</a>",
			LinkToThisServer(), COMMANDCATEGORY_DOMAIN_NAME, pCategoryName, pCategoryName);

		eaPush(&pInfo->ppCommandCategories, pCatWrapper);
	}

	estrPrintf(&pInfo->pSummary, "%s[%d] (version: %s) (accesslevel %d)", GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID(), GetUsefulVersionString(), iAccessLevel);

//	estrPrintf(&pInfo->pOtherTestCommand, "SuperHappyFunCommand $INT(What should the other int be on the $FIELD(serverType) $FIELD(ID)?) $FIELD(serverType)");

	GetCommandsForGenericServerMonitoringPage(pInfo);

	locGetImplementedLocaleNames(&pInfo->ppImplementedLocales);

	return pInfo;
}

AUTO_STRUCT;
typedef struct CachedParseTableText_OneAccessLevel
{
	int iAccessLevel; AST(KEY)
	char *pOutString; AST(ESTRING)
} CachedParseTableText_OneAccessLevel;


AUTO_STRUCT;
typedef struct CachedParseTableText_AllAccessLevels
{
	U32 iCRC;
	CachedParseTableText_OneAccessLevel **ppStringsByAccessLevel;
} CachedParseTableText_AllAccessLevels;

StashTable sCachedTextByTableAddress = NULL;

bool ParseTableWriteText_MaybeCache(char **ppEString, ParseTable pti[], const char* name, enumParseTableSendType eSendType)
{
	U32 iCRC = ParseTableCRC(pti, NULL, 0);
	CachedParseTableText_AllAccessLevels *pCache;
	CachedParseTableText_OneAccessLevel *pAccessLevelCache;
	int iAccessLevel = GetHTMLAccessLevel();

	//if this can ever change, need to split the cache
	assert(eSendType == PARSETABLESENDFLAG_FOR_HTTP_EXTERNAL);

	if (!sCachedTextByTableAddress)
	{
		sCachedTextByTableAddress = stashTableCreateAddress(16);
	}

	if (stashFindPointer(sCachedTextByTableAddress, pti, &pCache))
	{
		if (pCache->iCRC != iCRC)
		{
			StructReset(parse_CachedParseTableText_AllAccessLevels, pCache);
			
			//normally not necessary, but on the object DB things work differently, so irritating!
			eaIndexedEnable(&pCache->ppStringsByAccessLevel, parse_CachedParseTableText_OneAccessLevel);
			
			pCache->iCRC = iCRC;
		}
	}

	if (!pCache)
	{
		pCache = StructCreate(parse_CachedParseTableText_AllAccessLevels);
	
		//normally not necessary, but on the object DB things work differently, so irritating!
		eaIndexedEnable(&pCache->ppStringsByAccessLevel, parse_CachedParseTableText_OneAccessLevel);
				
		pCache->iCRC = iCRC;
		
		stashAddPointer(sCachedTextByTableAddress, pti, pCache, true);
	}

	pAccessLevelCache = eaIndexedGetUsingInt(&pCache->ppStringsByAccessLevel, iAccessLevel);
	if (pAccessLevelCache)
	{
		estrCopy(ppEString, &pAccessLevelCache->pOutString);
		return true;
	}

	pAccessLevelCache = StructCreate(parse_CachedParseTableText_OneAccessLevel);
	pAccessLevelCache->iAccessLevel = iAccessLevel;
	eaPush(&pCache->ppStringsByAccessLevel, pAccessLevelCache);

	assertmsg(ParseTableWriteText(&pAccessLevelCache->pOutString, pti, name, eSendType), "ParseTableWriteText failed");
	
	estrCopy(ppEString, &pAccessLevelCache->pOutString);

	return true;
}


		




bool ProcessStructIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, void *pStruct, ParseTable *pTPI, int iAccessLevel, ProcessStructForHttpFlags eProcessFlags, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eGetFlags)
{
	PERFINFO_AUTO_START_FUNC();

	if (strcmp(pLocalXPath, "") == 0)
	{
		if (!(eProcessFlags & SERVERMON_FIXUP_ALREADY_DONE))
		{
			FixupStructLeafFirst(pTPI, pStruct, FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED, NULL);
		}

		siAccessLevel = iAccessLevel;
	
		if ((eGetFlags & GETHTTPFLAG_FULLY_LOCAL_SERVERING) && (eGetFlags & GETHTTPFLAG_STATIC_STRUCT_OK_FOR_LOCAL_RETURN))
		{
			pStructInfo->pLocalStruct = pStruct;
			pStructInfo->pLocalTPI = pTPI;
		}
		else
		{
			if (!ParseTableWriteText_MaybeCache(&pStructInfo->pTPIString, pTPI, "insert_real_tpi_name_here", PARSETABLESENDFLAG_FOR_HTTP_EXTERNAL))
			{
				assertmsg(0, "ParseTableWriteText failed");
			}

			if (!ParserWriteText(&pStructInfo->pStructString, pTPI, pStruct, WRITETEXTFLAG_FORCEWRITECURRENTFILE | WRITETEXTFLAG_USEHTMLACCESSLEVEL | WRITETEXTFLAG_WRITINGFORHTML, 0, 0))
			{
				assertmsg(0, "ParserWriteText failed");
			}
		}

		pStructInfo->bIsRootStruct = true;

		PERFINFO_AUTO_STOP();
		return true;
	}
	else
	{
		ParseTable *pFoundTPI;
		int iFoundColumn;
		void *pFoundStruct;
		int iFoundIndex;

		if (objPathResolveField(pLocalXPath, pTPI, pStruct, 
			&pFoundTPI, &iFoundColumn, &pFoundStruct, &iFoundIndex, OBJPATHFLAG_TRAVERSEUNOWNED))
		{

			if (!(eProcessFlags & SERVERMON_FIXUP_ALREADY_DONE))
			{
				FixupStructLeafFirst(pFoundTPI, pFoundStruct, FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED, NULL);
			}


			if ((eGetFlags & GETHTTPFLAG_FULLY_LOCAL_SERVERING) && (eGetFlags & GETHTTPFLAG_STATIC_STRUCT_OK_FOR_LOCAL_RETURN))
			{
				pStructInfo->pLocalStruct = pFoundStruct;
				pStructInfo->pLocalTPI = pFoundTPI;
			}
			else
			{
				siAccessLevel = iAccessLevel;
				if (!ParseTableWriteText_MaybeCache(&pStructInfo->pTPIString, pFoundTPI, "GenericServerInfo", PARSETABLESENDFLAG_FOR_HTTP_EXTERNAL))
				{
					assertmsg(0, "ParseTableWriteText failed");
				}

				if (!ParserWriteText(&pStructInfo->pStructString, pFoundTPI, pFoundStruct, WRITETEXTFLAG_FORCEWRITECURRENTFILE | WRITETEXTFLAG_USEHTMLACCESSLEVEL | WRITETEXTFLAG_WRITINGFORHTML, 0, 0))
				{
					assertmsg(0, "ParserWriteText failed");
				}
			}

			pStructInfo->iColumn = iFoundColumn;
			pStructInfo->iIndex = iFoundIndex;

			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	PERFINFO_AUTO_STOP();
	return false;
}


bool GetStructForHttpXpath_internal(UrlArgumentList *pURL, int iAccessLevel, U32 iReqID1, U32 iReqID2, GetStructForHttpXpath_CB *pCB, GetHttpFlags eFlags)
{
	if (strStartsWith(pURL->pBaseURL, GENERIC_DOMAIN_NAME))
	{

		GenericServerInfoForHttpXpath * pGenericStruct = GetGenericServerInfo(iAccessLevel);
		StructInfoForHttpXpath structInfo = {0};
	
		if (ProcessStructIntoStructInfoForHttp(pURL->pBaseURL + strlen(GENERIC_DOMAIN_NAME), pURL, pGenericStruct, parse_GenericServerInfoForHttpXpath, iAccessLevel, 0, &structInfo, eFlags))
		{
			pCB(iReqID1, iReqID2, &structInfo);
			StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
			return true;
		}
	}
	else if (strStartsWith(pURL->pBaseURL, CUSTOM_DOMAIN_NAME))
	{
		char *pStruct = NULL;
		ParseTable *pTPI;

		GetCustomServerInfoStructForHttp(pURL, &pTPI, &pStruct);

		if (pStruct)
		{
			StructInfoForHttpXpath structInfo = {0};


			if (ProcessStructIntoStructInfoForHttp(pURL->pBaseURL + strlen(CUSTOM_DOMAIN_NAME), pURL, pStruct, pTPI, iAccessLevel, 0, &structInfo, eFlags))
			{
				pCB(iReqID1, iReqID2, &structInfo);
				StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
				return true;
			}
		}
		else
		{
			StructInfoForHttpXpath structInfo = {0};
			if (strcmp(pURL->pBaseURL + strlen(CUSTOM_DOMAIN_NAME), "") == 0)
			{
				estrPrintf(&structInfo.pRedirectString, "%s%s", LinkToThisServer(), GENERIC_DOMAIN_NAME);
				pCB(iReqID1, iReqID2, &structInfo);
				StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
				return true;
			}
		}
	}
	else if (strStartsWith(pURL->pBaseURL, GLOBALOBJECTS_DOMAIN_NAME))
	{
		StructInfoForHttpXpath structInfo = {0};

		if (ProcessGlobalObjectIntoStructInfoForHttp(pURL->pBaseURL + strlen(GLOBALOBJECTS_DOMAIN_NAME), pURL, iAccessLevel, &structInfo, eFlags))
		{
			pCB(iReqID1, iReqID2, &structInfo);
			StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
			return true;
		}
	}
	else if (strStartsWith(pURL->pBaseURL, COMMANDCATEGORY_DOMAIN_NAME))
	{
		StructInfoForHttpXpath structInfo = {0};
		if (ProcessCommandCategoryIntoStructInfoForHttp(pURL->pBaseURL + strlen(COMMANDCATEGORY_DOMAIN_NAME), pURL, iAccessLevel, &structInfo, eFlags))
		{
			pCB(iReqID1, iReqID2, &structInfo);
			StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
			return true;
		}
	}
	else
	{
		int i;

		for (i=0; i < eaSize(&ppCustomDomains); i++)
		{
			if (strStartsWith(pURL->pBaseURL, ppCustomDomains[i]->pDomainName))
			{
				if (ppCustomDomains[i]->pCBImmediate)
				{
					StructInfoForHttpXpath structInfo = {0};
					if (ppCustomDomains[i]->pCBImmediate(pURL->pBaseURL + strlen(ppCustomDomains[i]->pDomainName), pURL, iAccessLevel, &structInfo, eFlags))
					{
						pCB(iReqID1, iReqID2, &structInfo);
						StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
						return true;
					}
				}
				else
				{
					ppCustomDomains[i]->pCBDelayed(pURL->pBaseURL + strlen(ppCustomDomains[i]->pDomainName), pURL, iAccessLevel, iReqID1, iReqID2, pCB, eFlags);
					return true;
				}
			}
		}
	}

	return false;
}


void GetStructForHttpXpath(UrlArgumentList *pURL, int iAccessLevel, U32 iReqID1, U32 iReqID2, GetStructForHttpXpath_CB *pCB, GetHttpFlags eFlags)
{
	//MASSIVE KLUDGE. Due the way Kelvin wrote the editable field stuff, he automatically tacks ".struct" onto the end of every edit request.
	//This happens to work properly for the case he originally wrote it for, but utterly fails for other cases. So any time an xpath can't
	//be looked up, we try again without .struct on the end, if it has .struct on the end.
	
	PERFINFO_AUTO_START_FUNC();

	if (GetStructForHttpXpath_internal(pURL, iAccessLevel, iReqID1, iReqID2, pCB, eFlags))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if (strEndsWith(pURL->pBaseURL, ".struct"))
	{
		estrTruncateAtLastOccurrence(&pURL->pBaseURL, '.');
		if (GetStructForHttpXpath_internal(pURL, iAccessLevel, iReqID1, iReqID2, pCB, eFlags))
		{
			PERFINFO_AUTO_STOP();
			return;
		}
	}



	{
		StructInfoForHttpXpath structInfo = {0};
		GetMessageForHttpXpath(STACK_SPRINTF("Couldn't parse xpath %s", pURL->pBaseURL), &structInfo, 1);
		pCB(iReqID1, iReqID2, &structInfo);
		StructDeInit(parse_StructInfoForHttpXpath, &structInfo);
	}
	
	PERFINFO_AUTO_STOP();

}

void RegisterCustomXPathDomain(char *pDomainName, CustomXpathProcessingCB_Immediate *pCBImmediate, CustomXpathProcessingCB_Delayed *pCBDelayed)
{
	CustomDomainInfo *pInfo = calloc(sizeof(CustomDomainInfo), 1);
	pInfo->pDomainName = pDomainName;

	//must have one but not both callback (C has no logical XOR)?
	assert((pCBImmediate || pCBDelayed) && !(pCBImmediate && pCBDelayed));

	pInfo->pCBImmediate = pCBImmediate;
	pInfo->pCBDelayed = pCBDelayed;

	eaPush(&ppCustomDomains, pInfo);
}




AUTO_STRUCT;
typedef struct HttpMessageStruct
{
	char *pMessage;
} HttpMessageStruct;

void GetMessageForHttpXpath(char *pMessage, StructInfoForHttpXpath *pStructInfo, bool bError)
{
	HttpMessageStruct messageStruct;

	if (bError)
	{
		estrCopy2(&pStructInfo->pErrorString, pMessage);
		return;
	}

	messageStruct.pMessage = pMessage;

	if (!ParseTableWriteText(&pStructInfo->pTPIString, parse_HttpMessageStruct, "HttpMessageStruct", PARSETABLESENDFLAG_FOR_HTTP_EXTERNAL))
	{
		assertmsg(0, "ParseTableWriteText failed");
	}

	if (!ParserWriteText(&pStructInfo->pStructString, parse_HttpMessageStruct, &messageStruct, WRITETEXTFLAG_FORCEWRITECURRENTFILE, 0, 0))
	{
		assertmsg(0, "ParserWriteText failed");
	}

	pStructInfo->bIsRootStruct = true;
}

AUTO_STRUCT;
typedef struct HttpRawHTMLStruct
{
	char *pRawHTML; AST(FORMATSTRING(HTML=1, HTML_NO_HEADER=1, HTML_NO_DIV=1))
} HttpRawHTMLStruct;


void GetRawHTMLForHttpXpath(char *pRawHTML, StructInfoForHttpXpath *pStructInfo)
{
	HttpRawHTMLStruct htmlStruct = {pRawHTML};

	if (!ParseTableWriteText(&pStructInfo->pTPIString, parse_HttpRawHTMLStruct, "HttpMessageStruct", PARSETABLESENDFLAG_FOR_HTTP_EXTERNAL))
	{
		assertmsg(0, "ParseTableWriteText failed");
	}

	if (!ParserWriteText(&pStructInfo->pStructString, parse_HttpRawHTMLStruct, &htmlStruct, WRITETEXTFLAG_FORCEWRITECURRENTFILE, 0, 0))
	{
		assertmsg(0, "ParserWriteText failed");
	}

	pStructInfo->bIsRootStruct = true;
}

void HandleMonitoringInforRequestFromPacket_CB(U32 iReqID1, U32 iReqID2, StructInfoForHttpXpath *pStructInfo)
{

	if (GetControllerLink())
	{
		Packet *pReturnPacket;
		
		pktCreateWithCachedTracker(pReturnPacket, GetControllerLink(), TO_CONTROLLER_HERE_IS_REQUESTED_MONITORING_INFO);

		PutContainerTypeIntoPacket(pReturnPacket, GetAppGlobalType());
		PutContainerIDIntoPacket(pReturnPacket, GetAppGlobalID());

		pktSendBits(pReturnPacket, 32, iReqID1);
		pktSendBits(pReturnPacket, 32, iReqID2);

		ParserSendStruct(parse_StructInfoForHttpXpath, pReturnPacket, pStructInfo);

		pktSend(&pReturnPacket);
	}
}

void HandleMonitoringInfoRequestFromPacket(Packet *pPacket, NetLink *pNetLink)
{
	int iRequestID = pktGetBits(pPacket, 32);
	ContainerID iMCPID = pktGetBits(pPacket, 32);
	int iAccessLevel = pktGetBits(pPacket, 32);
	GetHttpFlags eFlags = pktGetBits(pPacket, 32);

	UrlArgumentList argList = {0};

	ParserRecv(parse_UrlArgumentList, pPacket, &argList, 0);

	GetStructForHttpXpath(&argList, iAccessLevel, iRequestID, iMCPID, HandleMonitoringInforRequestFromPacket_CB, eFlags);

	
	StructDeInit(parse_UrlArgumentList, &argList);
}

void DoSlowReturn_NetLink(ContainerID iMCPID, int iRequestID, int iClientID, CommandServingFlags eFlags, char *pMessageString, void *pUserData)
{
	if (GetControllerLink())
	{
		Packet *pOutPak = pktCreate(GetControllerLink(), TO_CONTROLLER_HERE_IS_REQUESTED_MONITORING_COMMAND_RESULT);

		pktSendBits(pOutPak, 32, iClientID);
		pktSendBits(pOutPak, 32, iRequestID);
		PutContainerIDIntoPacket(pOutPak, iMCPID);
		pktSendBits(pOutPak, 32, eFlags);
		pktSendString(pOutPak, pMessageString);

		pktSend(&pOutPak);		
	}
}




void GetJpegForServerMonitoringPacketReturnCB(char *pData, int iDataSize, int iLifeSpan, char *pMessage, GetJpegCache *pUserData)
{	
	Packet *pOutPack;
	pOutPack = pktCreate(pUserData->pNetLink, TO_CONTROLLER_HERE_IS_REQUESTED_MONITORING_JPEG);
	pktSendBits(pOutPack, 32, pUserData->iRequestID);
	PutContainerIDIntoPacket(pOutPack, pUserData->iMCPID);
	pktSendBits(pOutPack, 32, iDataSize);

	if (iDataSize)
	{
		pktSendBytes(pOutPack, iDataSize, pData);
		pktSendBits(pOutPack, 32, iLifeSpan);
	}
	else
	{
		pktSendString(pOutPack, pMessage);
	}

	pktSend(&pOutPack);

	free(pUserData);

}

void HandleMonitoringJpegRequestFromPacket(Packet *pPacket, NetLink *pNetLink)
{
	char *pName;
	GetJpegCache *pCache = malloc(sizeof(GetJpegCache));
	UrlArgumentList argList = {0};
	pCache->iRequestID = pktGetBits(pPacket, 32);
	pCache->iMCPID = GetContainerIDFromPacket(pPacket);
	pCache->pNetLink = pNetLink;
	pName = pktGetStringTemp(pPacket);
	ParserRecv(parse_UrlArgumentList, pPacket, &argList, 0);

	JpegLibrary_GetJpeg(pName, &argList, GetJpegForServerMonitoringPacketReturnCB, pCache);
}


void DEFAULT_LATELINK_HandleMonitoringCommandRequestFromPacket(Packet *pPacket, NetLink *pNetLink)
{
	int iClientID = pktGetBits(pPacket, 32);
	int iCommandRequestID = pktGetBits(pPacket, 32);
	ContainerID iMCPID = GetContainerIDFromPacket(pPacket);
	CommandServingFlags eFlags = pktGetBits(pPacket, 32);
	char *pCommandString = pktGetStringTemp(pPacket);
	const char *pAuthNameAndIP = pktGetStringTemp(pPacket);
	int iAccessLevel = pktGetBits(pPacket, 32);

	Packet *pOutPak;

	if (strStartsWith(pCommandString, "<?xml"))
	{
		pOutPak = pktCreate(pNetLink, TO_CONTROLLER_HERE_IS_REQUESTED_MONITORING_COMMAND_RESULT);

		pktSendBits(pOutPak, 32, iClientID);
		pktSendBits(pOutPak, 32, iCommandRequestID);
		PutContainerIDIntoPacket(pOutPak, iMCPID);
		pktSendBits(pOutPak, 32, eFlags);
		pktSendString(pOutPak,
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
			"<methodResponse><fault><value><struct>"
			"<member><name>faultCode</name><value><i4>8</i4></value></member>"
			"<member><name>faultString</name><value><string>The executable does not support XMLRPC calls.</string></value></member>"
			"</struct></value></fault></methodResponse>"
			);

		pktSend(&pOutPak);	
		return;
	}

	if (eFlags & CMDSRV_NORETURN)
	{
		if (!cmdParseForServerMonitor(eFlags, pCommandString, iAccessLevel, NULL, 0, 0, 0, 0, 0, pAuthNameAndIP, NULL))
		{
			pOutPak = pktCreate(pNetLink, TO_CONTROLLER_HERE_IS_REQUESTED_MONITORING_COMMAND_RESULT);

			pktSendBits(pOutPak, 32, iClientID);
			pktSendBits(pOutPak, 32, iCommandRequestID);
			PutContainerIDIntoPacket(pOutPak, iMCPID);
			pktSendBits(pOutPak, 32, eFlags);
			pktSendString(pOutPak, "Execution failed... talk to a programmer. Most likely the AST_COMMAND was set up wrong");

			pktSend(&pOutPak);			

		}
		else
		{
			pOutPak = pktCreate(pNetLink, TO_CONTROLLER_HERE_IS_REQUESTED_MONITORING_COMMAND_RESULT);

			pktSendBits(pOutPak, 32, iClientID);
			pktSendBits(pOutPak, 32, iCommandRequestID);
			PutContainerIDIntoPacket(pOutPak, iMCPID);
			pktSendBits(pOutPak, 32, eFlags);
			pktSendString(pOutPak,SERVERMON_CMD_RESULT_HIDDEN);

			pktSend(&pOutPak);			

		}

/*	if (!cmdParseForServerMonitor(pCommand, iAccessLevel, NULL, NULL, 0, 0, 0, 0, 0, pAuthNameAndIP))
		{
			RemoteCommand_ReturnHtmlStringFromRemoteCommand(GLOBALTYPE_CONTROLLER, 0, iClientID, iCommandRequestID, iMCPID, "Execution failed... talk to a programmer. Most likely the AST_COMMAND was set up wrong");
		}
		else
		{
			RemoteCommand_ReturnHtmlStringFromRemoteCommand(GLOBALTYPE_CONTROLLER, 0, iClientID, iCommandRequestID, iMCPID, SERVERMON_CMD_RESULT_HIDDEN);
		}*/

	}
	else
	{
		char *pRetString = NULL;
		bool bSlowReturn = false;

		estrStackCreate(&pRetString);

		cmdParseForServerMonitor(eFlags, pCommandString, iAccessLevel, &pRetString, iClientID, iCommandRequestID, iMCPID, DoSlowReturn_NetLink, NULL, pAuthNameAndIP, &bSlowReturn);

		if (!bSlowReturn)
		{
			pOutPak = pktCreate(pNetLink, TO_CONTROLLER_HERE_IS_REQUESTED_MONITORING_COMMAND_RESULT);

			pktSendBits(pOutPak, 32, iClientID);
			pktSendBits(pOutPak, 32, iCommandRequestID);
			PutContainerIDIntoPacket(pOutPak, iMCPID);
			pktSendBits(pOutPak, 32, eFlags);
			pktSendString(pOutPak, pRetString);

			pktSend(&pOutPak);		
		}

		estrDestroy(&pRetString);
	}
}




//Callback that gets the HTML header for an entity. In ServerLib because it needs to work on MCPs and GameServers.
AUTO_COMMAND;
char *GetEntityHTMLHeader(char *pKeyString)
{
	static char *pTempString = NULL;
	GlobalType eServerTypeBeingViewed = XPathSupport_GetServerType();
	ContainerID eServerIDBeingViewed = XPathSupport_GetContainerID();

	if (eServerTypeBeingViewed == GLOBALTYPE_GAMESERVER)
	{
		estrPrintf(&pTempString, "<a href=\"/viewimage?imageName=%s_%u_SCREENSHOT_%s.jpg\" target=\"_blank\">Screenshot</a>",
			GlobalTypeToName(eServerTypeBeingViewed), eServerIDBeingViewed, pKeyString);
	}
	else
	{
		estrPrintf(&pTempString, "");
	}

	return pTempString;
}


int GetHTMLAccessLevel(void)
{
	return siAccessLevel;
}


void SetHTMLAccessLevel(int level)
{
	siAccessLevel = level;
}

GlobalType DEFAULT_LATELINK_XPathSupport_GetServerType(void)
{
	return GetAppGlobalType();
}

ContainerID DEFAULT_LATELINK_XPathSupport_GetContainerID(void)
{
	return GetAppGlobalID();
}

#include "autogen/HttpXpathSupport_c_ast.c"
#include "autogen/HttpXpathSupport_h_ast.c"
