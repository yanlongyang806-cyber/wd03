/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "WebRequests.h"
#include "accountnet.h"
#include "AppLocale.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "Message.h"
#include "MicroTransactions.h"
#include "gslMission.h"
#include "logging.h"
#include "loggingEnums.h"
#include "mission_common.h"
#include "mission_enums.h"
#include "Powers.h"
#include "PowerTree.h"
#include "timing.h"
#include "gslAccountProxy.h"
#include "WebRequests_c_ast.h"
#include "gslAccountProxy_h_ast.h"
#include "cmdparse.h"
#include "StringCache.h"
#include "chatCommonstructs.h"
#include "gslMail.h"
#include "entity.h"
#include "Player.h"
#include "inventoryCommon.h"
#include "MailCommon.h"
#include "entity.h"
#include "ResourceManager.h"
#include "AutoGen/MailCommon_h_ast.h"
#include "AutoGen/mission_enums_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

// Number of results to return per request.
// Presently, the controller allocates a single memory block to relay an XML-RPC response, so the result needs to be small
// enough so that the controller doesn't run out of memory.
static const U64 uResultsPerRequest = 1024;


AUTO_STRUCT;
typedef struct MessageWeb
{
	const char *pcMessageKey; AST(KEY POOL_STRING NAME("MessageKey"))
	const char *pcString; AST(CASE_SENSITIVE)
} MessageWeb;

static const char *wrGetMessageKey(DisplayMessage *pDisplayMsg)
{
	Message *pMessage = GET_REF(pDisplayMsg->hMessage);
	if (!pMessage)
		return NULL;
	return pMessage->pcMessageKey;
}

// List of all ItemDefs.
//name, icon, type, scope, level, quality, mission
AUTO_STRUCT;
typedef struct ItemDefWeb
{
	const char *pchName; AST(POOL_STRING)
	const char *pchScope; AST(POOL_STRING)
	const char *pchIconName; AST(NAME(Icon) POOL_STRING)
	ItemType eType; AST(NAME(Type))
	S32 iLevel;
	ItemQuality Quality;
	REF_TO(MissionDef) hMission; AST(REFDICT(Mission) NAME(Mission))
} ItemDefWeb;

AUTO_STRUCT;
typedef struct DumpAllItemDefsResponse
{
	EARRAY_OF(ItemDefWeb) ItemDefs;
	U64 uSize;
	U64 uBase;
	U64 uBound;
} DumpAllItemDefsResponse;


static ItemDefWeb *wrCopyItemDef(ItemDef *pDef)
{
	ItemDefWeb *pDefCopy = StructCreate(parse_ItemDefWeb);
	pDefCopy->pchName = pDef->pchName;
	pDefCopy->pchScope = pDef->pchScope;
	pDefCopy->pchIconName = pDef->pchIconName;
	pDefCopy->eType = pDef->eType;
	pDefCopy->iLevel = pDef->iLevel;
	pDefCopy->Quality = pDef->Quality;
	COPY_HANDLE(pDefCopy->hMission, pDef->hMission);
	return pDefCopy;
}
// Return a list of all ItemDefs.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
DumpAllItemDefsResponse *DumpAllItemDefs(U64 uBase)
{
	DumpAllItemDefsResponse *response;
	U64 size;
	RefDictIterator iterator;
	ItemDef *item;
	U64 i;
	PERFINFO_AUTO_START_FUNC();

	// Create response.
	response = StructCreate(parse_DumpAllItemDefsResponse);
	size = RefSystem_GetDictionaryNumberOfReferents(g_hItemDict);
	response->uSize = size;
	response->uBase = uBase;
	if (response->uBase >= size)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return response;
	}
	response->uBound = MIN(size, uBase + uResultsPerRequest);
	eaSetSize(&response->ItemDefs, response->uBound - response->uBase);
	devassert(response->uBound >= response->uBase && response->uBound - response->uBase <= uResultsPerRequest);

	// Collect ItemDefs.
	RefSystem_InitRefDictIterator(g_hItemDict, &iterator);
	for (i = 0; item = RefSystem_GetNextReferentFromIterator(&iterator); ++i)
		if (i >= response->uBase && i < response->uBound)
			response->ItemDefs[i - response->uBase] = wrCopyItemDef(item);
	devassert(i <= size);

	PERFINFO_AUTO_STOP_FUNC();
	return response;
}

// List of all messages and their translations
AUTO_STRUCT;
typedef struct DumpAllMessagesResponse
{
	EARRAY_OF(MessageWeb) Messages;
	U64 uSize;
	U64 uBase;
	U64 uBound;
} DumpAllMessagesResponse;

static MessageWeb * wrCopyMessage(Message *msg)
{
	MessageWeb *pMsgCopy = StructCreate(parse_MessageWeb);
	pMsgCopy->pcMessageKey = msg->pcMessageKey;
	pMsgCopy->pcString = StructAllocString(msg->pcDefaultString);
	return pMsgCopy;
}

// Return a list of all Messages.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
DumpAllMessagesResponse *DumpAllMessages(U64 uBase)
{
	DumpAllMessagesResponse *response;
	U64 size;
	RefDictIterator iterator;
	Message *message;
	U64 i;
	PERFINFO_AUTO_START_FUNC();

	// Create response.
	response = StructCreate(parse_DumpAllMessagesResponse);
	size = RefSystem_GetDictionaryNumberOfReferents(gMessageDict);
	response->uSize = size;
	response->uBase = uBase;
	if (response->uBase >= size)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return response;
	}
	response->uBound = MIN(size, uBase + uResultsPerRequest);
	devassert(response->uBound >= response->uBase && response->uBound - response->uBase <= uResultsPerRequest);
	eaSetSize(&response->Messages, response->uBound - response->uBase);

	// Collect Messages.
	RefSystem_InitRefDictIterator(gMessageDict, &iterator);
	for (i = 0; message = RefSystem_GetNextReferentFromIterator(&iterator); ++i)
		if (i >= response->uBase && i < response->uBound)
		{
			response->Messages[i - response->uBase] = wrCopyMessage(message);
		}
	devassert(i <= size);

	PERFINFO_AUTO_STOP_FUNC();
	return response;
}

// List of all languages.
AUTO_STRUCT;
typedef struct DumpAllLanguagesResponse
{
	const char **pLangCodes;			AST(ESTRING UNOWNED)	// Array is owned, but members are not.
} DumpAllLanguagesResponse;

// Destroy array without destroying members.
AUTO_FIXUPFUNC;
TextParserResult FixupDumpAllLanguagesResponse(DumpAllLanguagesResponse *response, enumTextParserFixupType type, void *pExtraData)
{
	if (type == FIXUPTYPE_DESTRUCTOR)
		eaDestroy(&response->pLangCodes);
	return PARSERESULT_SUCCESS;
}

// Return a list of all languages.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
DumpAllLanguagesResponse *DumpAllLanguages()
{
	DumpAllLanguagesResponse *response;
	int i;
	int languages;
	PERFINFO_AUTO_START_FUNC();

	// Create response.
	response = StructCreate(parse_DumpAllLanguagesResponse);
	languages = locGetMaxLocaleCount();
	eaSetSize(&response->pLangCodes, languages);

	// Populate list of languages.
	for (i = 0; i != languages; ++i)
		response->pLangCodes[i] = locGetCode(i);

	PERFINFO_AUTO_STOP_FUNC();
	return response;
}

// List of all messages and their translations
AUTO_STRUCT;
typedef struct DumpAllTranslationsResponse
{
	char *pStatus;										AST(UNOWNED)
	EARRAY_OF(MessageWeb) pTranslations;
	U64 uSize;
	U64 uBase;
	U64 uBound;
} DumpAllTranslationsResponse;

static MessageWeb * wrCopyTranslatedMessage(TranslatedMessage *msg)
{
	if (msg)
	{
		MessageWeb *pMsgCopy = StructCreate(parse_MessageWeb);
		pMsgCopy->pcMessageKey = msg->pcMessageKey;
		pMsgCopy->pcString = StructAllocString(msg->pcTranslatedString);
		return pMsgCopy;
	}
	return NULL;
}

// Return a list of all Messages.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
DumpAllTranslationsResponse *DumpAllTranslations(SA_PARAM_NN_VALID const char *pLanguageCode, U64 uBase)
{
	DumpAllTranslationsResponse *response;
	U64 size;
	RefDictIterator iterator;
	Message *message;
	TranslatedMessage *translatedMsg;
	int locale;
	Language language;
	U64 i;
	PERFINFO_AUTO_START_FUNC();

	// Create response.
	response = StructCreate(parse_DumpAllTranslationsResponse);

	// Look up language.
	locale = locGetIDByCode(pLanguageCode);
	if (locale == DEFAULT_LOCALE_ID && stricmp(pLanguageCode, locGetCode(locale)))
	{
		response->pStatus = "No such language";
		return response;
	}
	else if (locale == DEFAULT_LOCALE_ID)
	{
		response->pStatus = "No translations available for default language.";
		return response;
	}
	language = locGetLanguage(locale);

	// Allocate response.
	response->pStatus = "Success";
	size = RefSystem_GetDictionaryNumberOfReferents(gMessageDict);
	response->uSize = size;
	response->uBase = uBase;
	if (response->uBase >= size)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return response;
	}
	response->uBound = MIN(size, uBase + uResultsPerRequest);
	eaSetSize(&response->pTranslations, response->uBound - response->uBase);
	devassert(response->uBound >= response->uBase && response->uBound - response->uBase <= uResultsPerRequest);

	// Collect Messages.
	RefSystem_InitRefDictIterator(gMessageDict, &iterator);
	for (i = 0; message = RefSystem_GetNextReferentFromIterator(&iterator); ++i)
		if (i >= response->uBase && i < response->uBound)
		{
			translatedMsg = langFindTranslatedMessage(language, message);
			response->pTranslations[i - response->uBase] = wrCopyTranslatedMessage(translatedMsg);
		}
	devassert(i <= size);

	PERFINFO_AUTO_STOP_FUNC();
	return response;
}

// List of all PowerDefs.
AUTO_STRUCT;
typedef struct PowerDefWeb
{
	char *pchName; AST(KEY POOL_STRING)

	const char *pchIconName;			AST(POOL_STRING)

	char *pchDisplayName; AST(NAME("DisplayName"))
	char *pchDescription; AST(NAME("Description"))
	char *pchDescriptionLong; AST(NAME("DescriptionLong"))
	char *pchDescriptionFlavor; AST(NAME("DescriptionFlavor"))
} PowerDefWeb;

AUTO_STRUCT;
typedef struct DumpAllPowerDefsResponse
{
	EARRAY_OF(PowerDefWeb) PowerDefs;
	U64 uSize;
	U64 uBase;
	U64 uBound;
} DumpAllPowerDefsResponse;

#include "structInternals.h"
static PowerDefWeb *wrCopyPowerDef(PowerDef *pDef)
{
	//static int si = 0;
	PowerDefWeb *pDefCopy = StructCreate(parse_PowerDefWeb);
	pDefCopy->pchName = pDef->pchName;
	pDefCopy->pchIconName = pDef->pchIconName;

	pDefCopy->pchDisplayName = StructAllocString(wrGetMessageKey(&pDef->msgDisplayName));
	pDefCopy->pchDescription = StructAllocString(wrGetMessageKey(&pDef->msgDescription));
	pDefCopy->pchDescriptionLong = StructAllocString(wrGetMessageKey(&pDef->msgDescriptionLong));
	pDefCopy->pchDescriptionFlavor = StructAllocString(wrGetMessageKey(&pDef->msgDescriptionFlavor));
	return pDefCopy;
}

// Return a list of all PowerDefs.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
DumpAllPowerDefsResponse *DumpAllPowerDefs(U64 uBase)
{
	DumpAllPowerDefsResponse *response;
	U64 size;
	RefDictIterator iterator;
	PowerDef *power;
	U64 i;
	PERFINFO_AUTO_START_FUNC();
	servLog(LOG_WEBREQUESTSERVER_XMLRPC, __FUNCTION__, "");

	// Create response.
	response = StructCreate(parse_DumpAllPowerDefsResponse);
	size = RefSystem_GetDictionaryNumberOfReferents(g_hPowerDefDict);
	response->uSize = size;
	response->uBase = uBase;
	if (response->uBase >= size)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return response;
	}
	response->uBound = MIN(size, uBase + uResultsPerRequest);
	eaSetSize(&response->PowerDefs, response->uBound - response->uBase);
	devassert(response->uBound >= response->uBase && response->uBound - response->uBase <= uResultsPerRequest);

	// Collect PowerDefs.
	RefSystem_InitRefDictIterator(g_hPowerDefDict, &iterator);
	for (i = 0; power = RefSystem_GetNextReferentFromIterator(&iterator); ++i)
	{
		if (i >= response->uBase && i < response->uBound)
			response->PowerDefs[i - response->uBase] = wrCopyPowerDef(power);
	}
	devassert(i <= size);

	PERFINFO_AUTO_STOP_FUNC();
	return response;
}

// List of all PowerTreeDefs.
AUTO_STRUCT;
typedef struct DumpAllPowerTreeDefsResponse
{
	EARRAY_OF(PowerTreeDef) PowerTreeDefs;			AST(UNOWNED)	// Array is owned, but members are not.
	U64 uSize;
	U64 uBase;
	U64 uBound;
} DumpAllPowerTreeDefsResponse;

// Destroy array without destroying members.
AUTO_FIXUPFUNC;
TextParserResult FixupDumpAllPowerTreeDefsResponse(DumpAllPowerTreeDefsResponse *response, enumTextParserFixupType type, void *pExtraData)
{
	if (type == FIXUPTYPE_DESTRUCTOR)
		eaDestroy(&response->PowerTreeDefs);
	return PARSERESULT_SUCCESS;
}

// Return a list of all PowerTreeDefs.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
DumpAllPowerTreeDefsResponse *DumpAllPowerTreeDefs(U64 uBase)
{
	DumpAllPowerTreeDefsResponse *response;
	U64 size;
	RefDictIterator iterator;
	PowerTreeDef *powerTree;
	U64 i;
	PERFINFO_AUTO_START_FUNC();
	servLog(LOG_WEBREQUESTSERVER_XMLRPC, __FUNCTION__, "");

	// Create response.
	response = StructCreate(parse_DumpAllPowerTreeDefsResponse);
	size = RefSystem_GetDictionaryNumberOfReferents(g_hPowerTreeDefDict);
	response->uSize = size;
	response->uBase = uBase;
	if (response->uBase >= size)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return response;
	}
	response->uBound = MIN(size, uBase + uResultsPerRequest);
	eaSetSize(&response->PowerTreeDefs, response->uBound - response->uBase);
	devassert(response->uBound >= response->uBase && response->uBound - response->uBase <= uResultsPerRequest);

	// Collect PowerTreeDefs.
	RefSystem_InitRefDictIterator(g_hPowerTreeDefDict, &iterator);
	for (i = 0; powerTree = RefSystem_GetNextReferentFromIterator(&iterator); ++i)
		if (i >= response->uBase && i < response->uBound)
			response->PowerTreeDefs[i - response->uBase] = powerTree;
	devassert(i <= size);

	PERFINFO_AUTO_STOP_FUNC();
	return response;
}

// List of all MissionDefs
AUTO_STRUCT;
typedef struct MissionDefWeb
{
	const char *pchName;					AST(KEY POOL_STRING)
	int iLevel;
	MissionType missionType;
	STRING_EARRAY eaObjectiveMaps;
	const char *pchReturnMap;				AST(POOL_STRING)

	char *pchDisplayName; AST(NAME("DisplayName"))
	char *pchDetailString; AST(NAME("DetailStringMsg"))
	char *pchSummary; AST(NAME("Summary"))
	const char *pchIconName;				AST(POOL_STRING)
} MissionDefWeb;
AUTO_STRUCT;
typedef struct DumpAllMissionDefsResponse
{
	EARRAY_OF(MissionDefWeb) MissionDefs;
	U64 uSize;
	U64 uBase;
	U64 uBound;
} DumpAllMissionDefsResponse;

static MissionDefWeb *wrCopyMissionDef(MissionDef *pDef)
{
	MissionDefWeb *pDefCopy = StructCreate(parse_MissionDefWeb);
	pDefCopy->pchName = pDef->name;
	pDefCopy->iLevel = pDef->levelDef.missionLevel;
	pDefCopy->missionType = pDef->missionType;
	EARRAY_FOREACH_BEGIN(pDef->eaObjectiveMaps, i);
	{
		if (pDef->eaObjectiveMaps[i]->pchMapName)
			eaPush(&pDefCopy->eaObjectiveMaps, StructAllocString(pDef->eaObjectiveMaps[i]->pchMapName));
	}
	EARRAY_FOREACH_END;
	pDefCopy->pchReturnMap = pDef->pchReturnMap;

	pDefCopy->pchDisplayName = StructAllocString(wrGetMessageKey(&pDef->displayNameMsg));
	pDefCopy->pchDetailString = StructAllocString(wrGetMessageKey(&pDef->detailStringMsg));
	pDefCopy->pchSummary = StructAllocString(wrGetMessageKey(&pDef->summaryMsg));
	pDefCopy->pchIconName = pDef->pchIconName;
	return pDefCopy;
}

// Return a list of all MissionDefs.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
DumpAllMissionDefsResponse *DumpAllMissionDefs(U64 uBase)
{
	DumpAllMissionDefsResponse *response;
	U64 size;
	RefDictIterator iterator;
	MissionDef *mission;
	U64 i;
	PERFINFO_AUTO_START_FUNC();
	servLog(LOG_WEBREQUESTSERVER_XMLRPC, __FUNCTION__, "");

	// Create response.
	response = StructCreate(parse_DumpAllMissionDefsResponse);
	size = RefSystem_GetDictionaryNumberOfReferents(g_MissionDictionary);
	response->uSize = size;
	response->uBase = uBase;
	if (response->uBase >= size)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return response;
	}
	response->uBound = MIN(size, uBase + uResultsPerRequest);
	eaSetSize(&response->MissionDefs, response->uBound - response->uBase);
	devassert(response->uBound >= response->uBase && response->uBound - response->uBase <= uResultsPerRequest);

	// Collect MissionDefs.
	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);
	for (i = 0; mission = RefSystem_GetNextReferentFromIterator(&iterator); ++i)
		if (i >= response->uBase && i < response->uBound)
			response->MissionDefs[i - response->uBase] = wrCopyMissionDef(mission);
	devassert(i <= size);

	PERFINFO_AUTO_STOP_FUNC();
	return response;
}

static U32 suLastProductCatalogChangeTime = 0;

AUTO_STRUCT;
typedef struct MicroTransactionCatalogRequest
{
	U32 characterID;
	U32 accountID;
} MicroTransactionCatalogRequest;


// Returns all the Microtransaction products for the shard
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
ACMD_STATIC_RETURN MicroTransactionInfo *GetMicrotransactionProducts(MicroTransactionCatalogRequest *request)
{
	MicroTransactionInfo *pInfo = NULL;

	PERFINFO_AUTO_START_FUNC();
	servLog(LOG_WEBREQUESTSERVER_XMLRPC, __FUNCTION__, "");
	pInfo = gslAPGetProductList();
	PERFINFO_AUTO_STOP();

	return pInfo;
}

AUTO_STRUCT;
typedef struct MicroTransactionCurrencyResponse
{
	char *currency;
} MicroTransactionCurrencyResponse;

AUTO_STRUCT;
typedef struct MicroTransactionCurrencyRequest
{
	// empty struct
	int uID; // dummy value so it compiles
} MicroTransactionCurrencyRequest;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
MicroTransactionCurrencyResponse *GetMicrotransactionCurrency(MicroTransactionCurrencyRequest *request)
{
	MicroTransactionCurrencyResponse *response = StructCreate(parse_MicroTransactionCurrencyResponse);
	response->currency = strdup(microtrans_GetShardCurrency());
	return response;
}

#define MICROTRANS_USERCATALOG_SUCCESS "success"
#define MICROTRANS_USERCATALOG_FAIL "failure"
#define MICROTRANS_USERCATALOG_PROCESSING "processing"

AUTO_STRUCT;
typedef struct MicroTransactionUserCatalogResponse
{
	char *result_string;
	char *error;
	EARRAY_OF(MTUserProduct) ppProducts;
} MicroTransactionUserCatalogResponse;

AUTO_STRUCT;
typedef struct MicroTransactionUserCatalogRequest
{
	U32 characterID;
	U32 accountID;
} MicroTransactionUserCatalogRequest;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
MicroTransactionUserCatalogResponse *GetMicrotransactionCatalogForCharacter(MicroTransactionUserCatalogRequest *request)
{
	WebDiscountRequest *discountInfo = NULL;
	MicroTransactionUserCatalogResponse *response = NULL;

	PERFINFO_AUTO_START_FUNC();
	response = StructCreate(parse_MicroTransactionUserCatalogResponse);

	if (!request->accountID && request->characterID)
	{
		response->result_string = strdup(MICROTRANS_USERCATALOG_FAIL);
		response->error = strdup(WDR_ERROR_PARTIAL_REQUEST);
		PERFINFO_AUTO_STOP();
		return response;
	}

	discountInfo = gslAPRequestDiscounts(request->characterID, request->accountID);

	if (discountInfo)
	{
		switch (discountInfo->eStatus)
		{
		case WDRSTATUS_SUCCESS:
			response->result_string = strdup(MICROTRANS_USERCATALOG_SUCCESS);
			servLog(LOG_WEBREQUESTSERVER_XMLRPC, __FUNCTION__, "User %d, Character %d, Result \"%s\"", 
				request->accountID, request->characterID, response->result_string);
		xcase WDRSTATUS_FAILED:
			response->result_string = strdup(MICROTRANS_USERCATALOG_FAIL);
			response->error = StructAllocString(discountInfo->pError);
			servLog(LOG_WEBREQUESTSERVER_XMLRPC, __FUNCTION__, "User %d, Character %d, Result \"%s\", Error \"%s\"", 
				request->accountID, request->characterID, response->result_string, response->error);
		}
		response->ppProducts = discountInfo->ppProducts;
		discountInfo->ppProducts = NULL;
		StructDestroy(parse_WebDiscountRequest, discountInfo);
	}
	else
		response->result_string = strdup(MICROTRANS_USERCATALOG_PROCESSING);
	PERFINFO_AUTO_STOP();

	return response;
}

AUTO_STRUCT;
typedef struct MicroTransactionUserPurchaseResponse
{
	int request_id;
	char *result_string;
	char *error;
} MicroTransactionUserPurchaseResponse;

AUTO_STRUCT;
typedef struct MicroTransactionUserPurchaseRequest
{
	U32 characterID;
	U32 accountID;
	U32 productID;
	int expectedPrice;  AST(DEFAULT(-1))
} MicroTransactionUserPurchaseRequest;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
MicroTransactionUserPurchaseResponse *MicrotransactionProductPurchase(MicroTransactionUserPurchaseRequest *request)
{
	MicroTransactionUserPurchaseResponse *response = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	response = StructCreate(parse_MicroTransactionUserPurchaseResponse);
	response->request_id = gslAP_InitiatePurchase(request->characterID, request->accountID, request->productID, request->expectedPrice);

	servLog(LOG_WEBREQUESTSERVER_XMLRPC, __FUNCTION__, "User %d, Character %d, Product %d, Request %d", 
		request->accountID, request->characterID, request->productID, 
		response->request_id);
	PERFINFO_AUTO_STOP();

	return response;
}

AUTO_STRUCT;
typedef struct MicroTransactionPurchaseStatusRequest
{
	U32 accountID;
	U32 requestID;
} MicroTransactionPurchaseStatusRequest;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
MicroTransactionUserPurchaseResponse *MicrotransactionProductPurchaseStatus(MicroTransactionPurchaseStatusRequest *request)
{
	char *error = NULL;
	MicroTransactionUserPurchaseResponse *response = NULL;
	PurchaseRequestStatus eStatus;
	
	PERFINFO_AUTO_START_FUNC();
	response = StructCreate(parse_MicroTransactionUserPurchaseResponse);
	eStatus = gslAP_GetPurchaseStatus(request->accountID, request->requestID, &error);
	response->request_id = request->requestID;
	
	if (error)
	{
		response->result_string = strdup(MICROTRANS_USERCATALOG_FAIL);
		response->error = error;
	}
	else
	{
		// These only log once on success/failure
		switch (eStatus)
		{
		case PURCHASE_SUCCESS:
			response->result_string = strdup(MICROTRANS_USERCATALOG_SUCCESS);
			servLog(LOG_WEBREQUESTSERVER_XMLRPC, __FUNCTION__, "User %d, Request %d, Result \"%s\"",
				request->accountID, request->requestID, response->result_string);
		xcase PURCHASE_FAILED:
			response->result_string = strdup(MICROTRANS_USERCATALOG_FAIL);
			servLog(LOG_WEBREQUESTSERVER_XMLRPC, __FUNCTION__, "User %d, Request %d, Result \"%s\"",
				request->accountID, request->requestID, response->result_string);
		xcase PURCHASE_PROCESSING:
		case PURCHASE_ROLLBACK:
			response->result_string = strdup(MICROTRANS_USERCATALOG_PROCESSING);
		}
	}
	PERFINFO_AUTO_STOP();

	return response;
}

AUTO_COMMAND_REMOTE;
void gslAPProductCatalogChanged(void)
{
	suLastProductCatalogChangeTime = timeSecondsSince2000();
	gslAPProductListUpdateCache();
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
bool HasProductCatalogChanged(U32 uLastTimeUpdated)
{
	if (suLastProductCatalogChangeTime == 0)
	{
		suLastProductCatalogChangeTime = timeSecondsSince2000();
		return false;
	}
	return suLastProductCatalogChangeTime < uLastTimeUpdated;
}

//
// Some utility functions for dealing with the inability of our XMLRPC code to directly handle slow command returns
//
void
WebRequestSlow_BuildXMLResponseString(char **responseString, ParseTable *tpi, void *struct_mem)
{
    char *resultString = NULL;

    ParserWriteXMLEx(&resultString, tpi, struct_mem, TPXML_FORMAT_XMLRPC);

    estrPrintf(responseString, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<methodResponse><params><param><value>%s</value></param></params></methodResponse>",
        resultString);

    estrDestroy(&resultString);
    return;
}

void
WebRequestSlow_BuildXMLResponseStringWithType(char **responseString, char *type, char *val)
{
    estrPrintf(responseString, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<methodResponse><params><param><value><%s>%s</%s></value></param></params></methodResponse>",
        type, val, type);
    return;
}

void
WebRequestSlow_SendXMLRPCReturn(bool success, CmdSlowReturnForServerMonitorInfo *slowReturnInfo)
{
    char *returnString = NULL;

    WebRequestSlow_BuildXMLResponseStringWithType(&returnString, "int", success ? "1" : "0");

    DoSlowCmdReturn(success, returnString, slowReturnInfo);

    estrDestroy(&returnString);
}

//
// Set up the context to do a slow return, and return a copy of the slow command info
//
CmdSlowReturnForServerMonitorInfo *
WebRequestSlow_SetupSlowReturn(CmdContext *pContext)
{
    CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo;

    pContext->slowReturnInfo.bDoingSlowReturn = true;
    pSlowReturnInfo = malloc(sizeof(CmdSlowReturnForServerMonitorInfo));
    memcpy(pSlowReturnInfo, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));

    return pSlowReturnInfo;
}

AUTO_STRUCT;
typedef struct SlotsAvailableCBData
{
    ContainerID accountID;
    U32 iVirtualShardID;
    STRING_POOLED allegianceName;                   AST(POOL_STRING)
    OPTIONAL_STRUCT(CmdSlowReturnForServerMonitorInfo) slowReturnInfo;		NO_AST
} SlotsAvailableCBData;

void
IsCharacterSlotAvailableForTransfer_CB(TransactionReturnVal *pReturn, SlotsAvailableCBData *cbData)
{
    enumTransactionOutcome outcome;
    bool retVal;

    outcome = RemoteCommandCheck_aslLogin_IsCharacterSlotAvailableForTransfer(pReturn, &retVal);
    if ( outcome != TRANSACTION_OUTCOME_SUCCESS )
    {
        retVal = false;
    }

    WebRequestSlow_SendXMLRPCReturn(retVal, cbData->slowReturnInfo);
    StructDestroy(parse_SlotsAvailableCBData, cbData);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
bool 
IsCharacterSlotAvailableForTransfer(CmdContext *pContext, ContainerID accountID, U32 iVirtualShardID, const char *allegianceName)
{
    SlotsAvailableCBData *cbData = StructCreate(parse_SlotsAvailableCBData);
    TransactionReturnVal *pReturn;

    cbData->accountID = accountID;
    cbData->iVirtualShardID = iVirtualShardID;
    cbData->allegianceName = allocAddString(allegianceName);
    cbData->slowReturnInfo = WebRequestSlow_SetupSlowReturn(pContext);

    pReturn = objCreateManagedReturnVal(IsCharacterSlotAvailableForTransfer_CB, cbData);

    RemoteCommand_aslLogin_IsCharacterSlotAvailableForTransfer(pReturn, GLOBALTYPE_LOGINSERVER, 0, accountID, iVirtualShardID, allegianceName);

    return false;
}

AUTO_STRUCT;
typedef struct WebGetCharacterChoicesCBData
{
    ContainerID accountID;
    OPTIONAL_STRUCT(CmdSlowReturnForServerMonitorInfo) slowReturnInfo;		NO_AST
} WebGetCharacterChoicesCBData;

void
WebGetCharacterChoicesCB(TransactionReturnVal *returnVal, WebGetCharacterChoicesCBData *cbData)
{
    char *returnString = NULL;
    Login2CharacterChoices *characterChoices;

    if ( RemoteCommandCheck_aslLogin2_GetCharacterChoicesCmd(returnVal, &characterChoices) == TRANSACTION_OUTCOME_SUCCESS )
    {
        WebRequestSlow_BuildXMLResponseString(&returnString, parse_Login2CharacterChoices, characterChoices);

        DoSlowCmdReturn(true, returnString, cbData->slowReturnInfo);

        estrDestroy(&returnString);

        StructDestroy(parse_Login2CharacterChoices, characterChoices);
    }
    else
    {
        WebRequestSlow_SendXMLRPCReturn(false, cbData->slowReturnInfo);
    }

    StructDestroy(parse_WebGetCharacterChoicesCBData, cbData);

}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
Login2CharacterChoices *
gslLoginWebRequest_GetCharacterChoices(CmdContext *cmdContext, ContainerID accountID)
{
    TransactionReturnVal *returnVal;
    WebGetCharacterChoicesCBData *cbData;

    // set up callback data
    cbData = StructAlloc(parse_WebGetCharacterChoicesCBData);
    cbData->accountID = accountID;
    cbData->slowReturnInfo = WebRequestSlow_SetupSlowReturn(cmdContext);

    returnVal = objCreateManagedReturnVal(WebGetCharacterChoicesCB, cbData);
    RemoteCommand_aslLogin2_GetCharacterChoicesCmd(returnVal, GLOBALTYPE_LOGINSERVER, SPECIAL_CONTAINERID_RANDOM, accountID);

    return NULL;
}

//Uses old chatserver structs for CS purposes, so old/new mail can be combined into a single list.
void EmailV3_GetCharacterMailAsChatMailList(Entity* pEnt, WebRequestChatMailList* pList, int iPageSize, int iPageOffset, int iOrder)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pEmailV2)
	{
		int i, j, iStart;
		if (iOrder)//Apparently this is actually a boolean value; "1" means "newest first".
		{
			iStart = eaSize(&pEnt->pPlayer->pEmailV2->mail) - (1 + iPageSize * iPageOffset);
			for (i = iStart; (i >= 0) && (i > iStart-iPageSize) ; i--)
			{
				NPCEMailData* pData = pEnt->pPlayer->pEmailV2->mail[i];
				WebRequestChatMailStruct* pChatMail = StructCreate(parse_WebRequestChatMailStruct);
				pChatMail->body = StructAllocString(pData->body);
				pChatMail->fromName = StructAllocString(pData->fromName);
				pChatMail->subject = StructAllocString(pData->subject);
				pChatMail->sent = pData->sentTime;
				pChatMail->bRead = pData->bRead;
				pChatMail->uID = pData->iNPCEMailID;

				for (j = 0; j < eaSize(&pData->ppItemSlot); j++)
				{
					if (pData->ppItemSlot[j]->pItem)
					{
						Item* pClonedItem = StructClone(parse_Item, pData->ppItemSlot[j]->pItem);
						eaPush(&pChatMail->eaAttachedItems, pClonedItem);
					}
				}

				eaPush(&pList->mail, pChatMail);
			}
		}
		else
		{
			iStart = min(eaSize(&pEnt->pPlayer->pEmailV2->mail)-iPageSize, iPageSize * iPageOffset);
			for (i = iStart; (i < eaSize(&pEnt->pEmailV3->eaMessages)) && (i < iStart+iPageSize) ; i++)
			{
				NPCEMailData* pData = pEnt->pPlayer->pEmailV2->mail[i];
				WebRequestChatMailStruct* pChatMail = StructCreate(parse_WebRequestChatMailStruct);
				pChatMail->body = StructAllocString(pData->body);
				pChatMail->fromName = StructAllocString(pData->fromName);
				pChatMail->subject = StructAllocString(pData->subject);
				pChatMail->sent = pData->sentTime;
				pChatMail->bRead = pData->bRead;
				pChatMail->uID = pData->iNPCEMailID;

				for (j = 0; j < eaSize(&pData->ppItemSlot); j++)
				{
					if (pData->ppItemSlot[j]->pItem)
					{
						Item* pClonedItem = StructClone(parse_Item, pData->ppItemSlot[j]->pItem);
						eaPush(&pChatMail->eaAttachedItems, pClonedItem);
					}
				}

				eaPush(&pList->mail, pChatMail);
			}
		}
		pList->uPage = iPageOffset;
		pList->uPageSize = iPageSize;
		pList->uTotalMail = eaSize(&pEnt->pPlayer->pEmailV2->mail);
	}
}

// Return a list of all ItemDefs.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
WebRequestChatMailList *ListCharacterEmailMessages(CmdContext *context, Entity* pEnt, int iPageSize, int iPageOffset, int iOrder)
{
	WebRequestChatMailList *list;

	list = StructCreate(parse_WebRequestChatMailList);
	list->uID = pEnt->myContainerID;
	EmailV3_GetCharacterMailAsChatMailList(pEnt, list, iPageSize, iPageOffset, iOrder);
	return list;
}

//Uses old chatserver structs for CS purposes, so old/new mail can be combined into a single list.
void EmailV3_GetAccountMailAsChatMailList(Entity* pEntitySharedBank, WebRequestChatMailList* pList, int iPageSize, int iPageOffset, int iOrder)
{
	if (pEntitySharedBank && pEntitySharedBank->pEmailV3->eaMessages)
	{
		int i, iStart;
		if (iOrder)//Apparently this is actually a boolean value; "1" means "newest first".
		{
			iStart = eaSize(&pEntitySharedBank->pEmailV3->eaMessages) - (1 + iPageSize * iPageOffset);
			for (i = iStart; (i >= 0) && (i > iStart-iPageSize) ; i--)
			{
				EmailV3Message* pMessage = pEntitySharedBank->pEmailV3->eaMessages[i];
				WebRequestChatMailStruct* pChatMail = StructCreate(parse_WebRequestChatMailStruct);
				pChatMail->body = StructAllocString(pMessage->pchBody);
				pChatMail->fromName = StructAllocString(pMessage->pchSenderName);
				pChatMail->subject = StructAllocString(pMessage->pchSubject);
				pChatMail->sent = pMessage->uSent;
				pChatMail->bRead = pMessage->bRead;
				pChatMail->uID = pMessage->uID;
				eaPushStructs(&pChatMail->eaAttachedItems, &pMessage->ppItems, parse_Item);

				eaPush(&pList->mail, pChatMail);
			}
		}
		else
		{
			iStart = min(eaSize(&pEntitySharedBank->pEmailV3->eaMessages)-iPageSize, iPageSize * iPageOffset);
			for (i = iStart; (i < eaSize(&pEntitySharedBank->pEmailV3->eaMessages)) && (i < iStart+iPageSize) ; i++)
			{
				EmailV3Message* pMessage = pEntitySharedBank->pEmailV3->eaMessages[i];
				WebRequestChatMailStruct* pChatMail = StructCreate(parse_WebRequestChatMailStruct);
				pChatMail->body = StructAllocString(pMessage->pchBody);
				pChatMail->fromName = StructAllocString(pMessage->pchSenderName);
				pChatMail->subject = StructAllocString(pMessage->pchSubject);
				pChatMail->sent = pMessage->uSent;
				pChatMail->bRead = pMessage->bRead;
				pChatMail->uID = pMessage->uID;
				eaPushStructs(&pChatMail->eaAttachedItems, &pMessage->ppItems, parse_Item);

				eaPush(&pList->mail, pChatMail);
			}
		}
		pList->uPage = iPageOffset;
		pList->uPageSize = iPageSize;
		pList->uTotalMail = eaSize(&pEntitySharedBank->pEmailV3->eaMessages);
	}
}

AUTO_STRUCT;
typedef struct WebRequestPendingAccountMailLookup
{
	int iContainerID; AST(KEY)
	REF_TO(Entity) hAccountSharedBank;	AST(COPYDICT(EntitySharedBank))
	CmdSlowReturnForServerMonitorInfo* pSlowReturnInfo; NO_AST
	int iPageSize;
	int iPageOffset;
	int iOrder;
}WebRequestPendingAccountMailLookup;

static WebRequestPendingAccountMailLookup** g_eaiPendingAccountIDLookups = NULL;

void
	SendAccountEmailMessagesSlowReturn(bool success, WebRequestPendingAccountMailLookup* pLookup, Entity* pAccountSharedBankEnt)
{
	char *returnString = NULL;
	WebRequestChatMailList list = {0};
	if (pAccountSharedBankEnt)
	{
		list.uID = pAccountSharedBankEnt->myContainerID;
		EmailV3_GetAccountMailAsChatMailList(pAccountSharedBankEnt, &list, pLookup->iPageSize, pLookup->iPageOffset, pLookup->iOrder);
	}

	WebRequestSlow_BuildXMLResponseString(&returnString, parse_WebRequestChatMailList, &list);

	DoSlowCmdReturn(success, returnString, pLookup->pSlowReturnInfo);
	free(pLookup->pSlowReturnInfo);
	StructDestroy(parse_WebRequestPendingAccountMailLookup, pLookup);
	estrDestroy(&returnString);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
void ListAccountEmailMessages(CmdContext *cmdContext, ContainerID id, int iPageSize, int iPageOffset, int iOrder)
{
	char idBuf[128];
	int i;
	WebRequestPendingAccountMailLookup* pLookup = NULL;

	i = eaIndexedFindUsingInt(&g_eaiPendingAccountIDLookups, id);

	if (i != -1)
	{
		//we already have a pending request for this account, calm down you silly CSRs!
		if (GET_REF(g_eaiPendingAccountIDLookups[i]->hAccountSharedBank))
		{
			//Something went wrong, but we have the entity now, so might as well process it.
			Entity* pEnt = GET_REF(g_eaiPendingAccountIDLookups[i]->hAccountSharedBank);
			pLookup = g_eaiPendingAccountIDLookups[i];
			SendAccountEmailMessagesSlowReturn(1, pLookup, pEnt);
			eaIndexedRemoveUsingInt(&g_eaiPendingAccountIDLookups, pEnt->myContainerID);
		}
		return;
	}
	pLookup = StructCreate(parse_WebRequestPendingAccountMailLookup);
	SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSHAREDBANK), ContainerIDToString(id, idBuf), pLookup->hAccountSharedBank);

	pLookup->iPageOffset = iPageOffset;
	pLookup->iPageSize = iPageSize;
	pLookup->iOrder = iOrder;
	pLookup->pSlowReturnInfo = WebRequestSlow_SetupSlowReturn(cmdContext);
	pLookup->iContainerID = id;
	
	if (GET_REF(pLookup->hAccountSharedBank))
	{
		//We already had this container lying around. Mysterious!
		Entity* pEnt = GET_REF(pLookup->hAccountSharedBank);
		SendAccountEmailMessagesSlowReturn(1, pLookup, pEnt);
		return;
	}
	
	eaIndexedEnable(&g_eaiPendingAccountIDLookups, parse_WebRequestPendingAccountMailLookup);
	eaIndexedPushUsingIntIfPossible(&g_eaiPendingAccountIDLookups, id, pLookup);

	return;
}

void AccountSharedBankReceived_CB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, Entity *pEnt, void *pUserData)
{
	switch (eType)
	{
	case RESEVENT_RESOURCE_ADDED:
		{
			int i = eaIndexedFindUsingInt(&g_eaiPendingAccountIDLookups,pEnt->myContainerID);
			if (i > -1)
			{
				WebRequestPendingAccountMailLookup* pLookup = g_eaiPendingAccountIDLookups[i];
				eaIndexedRemoveUsingInt(&g_eaiPendingAccountIDLookups, pEnt->myContainerID);
				SendAccountEmailMessagesSlowReturn(1, pLookup, pEnt);
			}
		}
		break;
	}
}



#include "WebRequests_c_ast.c"
