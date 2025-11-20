/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#include "NameGen.h"
#include "ReferenceSystem.h"
#include "ResourceManager.h"
#include "estring.h"
#include "Entity.h"
#include "file.h"
#include "rand.h"
#include "error.h"
#include "species_common.h"
#include "TextFilter.h"
#include "Expression.h"
#include "StringCache.h"

#ifdef GAMECLIENT
#include "chat/gclChatLog.h"
#include "gclEntity.h"
#include "gclLogin.h"
#include "GlobalStateMachine.h"
#include "gclBaseStates.h"
#include "player.h"
#include "UIGen.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#endif

#ifdef GAMESERVER
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#endif
#include "AutoGen/NameGen_h_ast.h"

#define NAMEGEN_NAMES_PER_REQUEST 25

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// --------------------------------------------------------------------------
// Static Data
// --------------------------------------------------------------------------

DictionaryHandle g_hPhonemeSetDict = NULL;
DictionaryHandle g_hNameTemplateListDict = NULL;

static bool s_bIsLoading = false;

// --------------------------------------------------------------------------
// Regular Functions
// --------------------------------------------------------------------------

const char *namegen_GenerateName(NameTemplateListRef *pNameTemplateList, U32 *bitsRestrictGroup, U32 *pSeed)
{
	F32 fTotalWeight = 0;
	static char name[256];
	NameTemplateList *pList = NULL;
	int i, iRand;
	F32 fRand;
	NameTemplate *pNameTemplate = NULL;

	*name = '\0';
	if (!pNameTemplateList) 
		return NULL;
	pList = GET_REF(pNameTemplateList->hNameTemplateList);
	
	if (!pList) 
		return NULL;
	if (!eaSize(&pList->eaNameTemplates)) 
		return NULL;

	for (i = eaSize(&pList->eaNameTemplates)-1; i >= 0; --i)
	{
		if ((!bitsRestrictGroup) || (pList->eaNameTemplates[i]->bitsBelongGroup & *bitsRestrictGroup))
		{
			fTotalWeight += pList->eaNameTemplates[i]->fWeight;
		}
	}

	if(pSeed)
		fRand = randomPositiveF32Seeded(pSeed,RandType_LCG) * fTotalWeight;
	else
		fRand = randomPositiveF32() * fTotalWeight;

	for (i = 0; i < eaSize(&pList->eaNameTemplates); i++)
	{
		if ((!bitsRestrictGroup) || (pList->eaNameTemplates[i]->bitsBelongGroup & *bitsRestrictGroup))
		{
			if(fRand <= pList->eaNameTemplates[i]->fWeight)
			{
				pNameTemplate = pList->eaNameTemplates[i];
				break;
			}
			fRand -= pList->eaNameTemplates[i]->fWeight;
		}
	}
	if (!devassertmsg(pNameTemplate, "Couldn't find a valid name template during name generation"))
		pNameTemplate = pList->eaNameTemplates[0];
	if (bitsRestrictGroup) 
		*bitsRestrictGroup &= pNameTemplate->bitsRestrictGroup;

	for (i = 0; i < eaSize(&pNameTemplate->eaPhonemeSets); ++i)
	{
		PhonemeSet *pPhonemeSet = GET_REF(pNameTemplate->eaPhonemeSets[i]->hPhonemeSet);
		if (!pPhonemeSet) continue;
		if(pSeed)
			iRand = randomIntRangeSeeded(pSeed,RandType_LCG,0,eaSize(&pPhonemeSet->pcPhonemes)-1);
		else
			iRand = randomIntRange(0,eaSize(&pPhonemeSet->pcPhonemes)-1);
		
		if (!pPhonemeSet->pcPhonemes[iRand]) 
			continue;
		
		strcat(name,pPhonemeSet->pcPhonemes[iRand]);
		if (i == 0 && pNameTemplate->bAutoCapsFirstLetter)
		{
			//Capitalize First Letter
			char *c = name + (strlen(name) - strlen(pPhonemeSet->pcPhonemes[iRand]));
			*c = toupper(*c);
		}
	}

	return name;
}

const char *namegen_GenerateFullName(NameTemplateListRef **eaNameTemplateLists, U32 *pSeed)
{
	const char *temp;
	static char name[256];
	U32 restrictGroup = 0xFFFFFFFF;
	int j;

	*name = '\0';
	if (!eaSize(&eaNameTemplateLists)) return name;

	for (j = eaSize(&eaNameTemplateLists)-1; j >= 0; --j)
	{
		//We want the order to go from last name to first name because the last name chosen can restrict which first name can be chosen
		temp = namegen_GenerateName(eaNameTemplateLists[j], &restrictGroup, pSeed);
		if ((!temp) || !*temp) continue;
		if (*name)
		{
			char nametemp[256];
			*nametemp = '\0';
			strcat(nametemp, name);
			*name = '\0';
			strcat(name, temp);
			strcat(name, " ");
			strcat(name, nametemp);
		}
		else
		{
			strcat(name, temp);
		}
	}

	return name;
}

// --------------------------------------------------------------------------
// Dictionary Management
// --------------------------------------------------------------------------

static void validatePhonemeSet(PhonemeSet *pDef)
{
	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"PhonemeSet '%s' does not have a valid name\n",pDef->pcName);
	}

	//if (!GET_REF(pDef->displayNameMsg.hMessage)) {
	//	if (REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage)) {
	//		ErrorFilenamef(pDef->pcFileName,"PhonemeSet '%s' refers to non-existent message '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage));
	//	} else {
	//		ErrorFilenamef(pDef->pcFileName,"PhonemeSet '%s' has no display name\n",pDef->pcName);
	//	}
	//}
}

static void validateNameTemplateList(NameTemplateList *pDef)
{
	int i, j;

	if (!resIsValidName(pDef->pcName)) {
		ErrorFilenamef(pDef->pcFileName,"NameTemplateList '%s' does not have a valid name\n",pDef->pcName);
	}

	//if (!GET_REF(pDef->displayNameMsg.hMessage)) {
	//	if (REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage)) {
	//		ErrorFilenamef(pDef->pcFileName,"NameTemplateList '%s' refers to non-existent message '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage));
	//	} else {
	//		ErrorFilenamef(pDef->pcFileName,"NameTemplateList '%s' has no display name\n",pDef->pcName);
	//	}
	//}

	for (i = eaSize(&pDef->eaNameTemplates)-1; i >= 0; --i)
	{
		NameTemplate *nt = pDef->eaNameTemplates[i];
		if (!nt) continue;
		for (j = eaSize(&nt->eaPhonemeSets)-1; j >= 0; --j)
		{
			if (!nt->eaPhonemeSets[j]) continue;
			if (!GET_REF(nt->eaPhonemeSets[j]->hPhonemeSet)) {
				if (REF_STRING_FROM_HANDLE(nt->eaPhonemeSets[j]->hPhonemeSet)) {
					ErrorFilenamef(pDef->pcFileName,"NameTemplateList '%s' refers to non-existent PhonemeSet '%s'\n",pDef->pcName,REF_STRING_FROM_HANDLE(nt->eaPhonemeSets[j]->hPhonemeSet));
				}
			}
		}
	}
}


static int phonemeSetResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PhonemeSet *pPhonemeSet, U32 userID)
{
	switch(eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
			if (IsClient() && s_bIsLoading) {
				if (pPhonemeSet->pcScope && strStartsWith(pPhonemeSet->pcScope, "SpeciesGen")) {
					resDoNotLoadCurrentResource(); // Performs free
				}
			}
			return VALIDATE_HANDLED;
		xcase RESVALIDATE_CHECK_REFERENCES:
			// Only validate on server
			if (IsServer()) {
				validatePhonemeSet(pPhonemeSet);
				return VALIDATE_HANDLED;
			}
		xcase RESVALIDATE_FIX_FILENAME:
			resFixPooledFilename((char**)&pPhonemeSet->pcFileName, "defs/namegen", pPhonemeSet->pcScope, pPhonemeSet->pcName, "phoneme");\
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static int nameTemplateListResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, NameTemplateList *pNameTemplateList, U32 userID)
{
	switch(eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
			if (IsClient() && s_bIsLoading) {
				if (pNameTemplateList->pcScope && strStartsWith(pNameTemplateList->pcScope, "SpeciesGen")) {
					resDoNotLoadCurrentResource(); // Performs free
				}
			}
			return VALIDATE_HANDLED;
		xcase RESVALIDATE_CHECK_REFERENCES:
			// Only validate on server
			if (IsServer()) {
				validateNameTemplateList(pNameTemplateList);
				return VALIDATE_HANDLED;
			}
		xcase RESVALIDATE_FIX_FILENAME:
			resFixPooledFilename((char**)&pNameTemplateList->pcFileName, "defs/namegen", pNameTemplateList->pcScope, pNameTemplateList->pcName, "namegen");
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_STARTUP(NameGen);
void nameGen_Load(void)
{
	s_bIsLoading = true;
	if (IsGameServerBasedType() || IsLoginServer() || IsAuctionServer()) {
		resLoadResourcesFromDisk(g_hPhonemeSetDict, "defs/namegen", ".phoneme", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
		resLoadResourcesFromDisk(g_hNameTemplateListDict, "defs/namegen", ".namegen", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	} else if (IsClient()) {
		ParserLoadFilesToDictionary("defs/namegen", ".phoneme", "PhonemeSetClient.bin", PARSER_OPTIONALFLAG, g_hPhonemeSetDict);
		ParserLoadFilesToDictionary("defs/namegen", ".namegen", "NameTemplateListClient.bin", PARSER_OPTIONALFLAG, g_hNameTemplateListDict);
	}
	s_bIsLoading = false;
}


AUTO_RUN;
int RegisterNameGenDict(void)
{
	// Set up reference dictionary for parts and such
	g_hPhonemeSetDict = RefSystem_RegisterSelfDefiningDictionary("PhonemeSet", false, parse_PhonemeSet, true, true, NULL);
	g_hNameTemplateListDict = RefSystem_RegisterSelfDefiningDictionary("NameTemplateList", false, parse_NameTemplateList, true, true, NULL);

	resDictManageValidation(g_hPhonemeSetDict, phonemeSetResValidateCB);
	resDictManageValidation(g_hNameTemplateListDict, nameTemplateListResValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hPhonemeSetDict);
		resDictProvideMissingResources(g_hNameTemplateListDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hPhonemeSetDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hNameTemplateListDict, ".DisplayNameMsg.Message", NULL, NULL, NULL, NULL);
		}
	} else if (IsClient()) {
		resDictRequestMissingResources(g_hPhonemeSetDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hNameTemplateListDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
	}

	return 1;
}

GenNameList *nameGen_GenerateRandomNamesInternal(GenNameListReq *pReq)
{
	int i, j;
	SpeciesDef *pSpecies = pReq->pcSpecies ? RefSystem_ReferentFromString("Species", pReq->pcSpecies) : NULL;
	char **eaNameParts = NULL;
	char *estrFullName = NULL;
	GenNameList *pNameList;
	GenNameEntry *pEntry;

	if (!pSpecies) return NULL;
	if (!eaSize(&pSpecies->eaNameTemplateLists)) return NULL;

	pNameList = StructCreate(parse_GenNameList);
	pNameList->pcSpecies = pSpecies->pcName;

	for (i = 0; i < NAMEGEN_NAMES_PER_REQUEST; ++i)
	{
		U32 restrictGroup = 0xFFFFFFFF;

		for (j = eaSize(&pSpecies->eaNameTemplateLists) - 1; j >= 0; j--)
		{
			const char *pcName = namegen_GenerateName(pSpecies->eaNameTemplateLists[j], &restrictGroup, NULL);
			int len;
			if (!pcName || !*pcName)
				continue;

			len = (int)strlen(pcName);
			if (len < 3 || len > 20)
				continue;
			if (IsAnyProfane(pcName) || IsAnyRestricted(pcName) || IsDisallowed(pcName))
				continue;

			eaInsert(&eaNameParts, StructAllocString(pcName), 0);
		}

		// If the generate name is completely valid
		if (eaSize(&eaNameParts) == eaSize(&pSpecies->eaNameTemplateLists))
		{
			char *pcFirstName = NULL, *pcMiddleName = NULL, *pcLastName = NULL;

			// Extract name parts
			switch (eaSize(&eaNameParts))
			{
				xcase 0:
				{
					// This case shouldn't happen
				}
				xcase 1:
				{
					// Last name should be defined
					pcLastName = eaNameParts[0];
				}
				xcase 2:
				{
					// First & last name should be defined
					if (pSpecies->eNameOrder == kNameOrder_FML)
					{
						pcFirstName = eaNameParts[0];
						pcLastName = eaNameParts[1];
					}
					else
					{
						pcLastName = eaNameParts[0];
						pcFirstName = eaNameParts[1];
					}
				}
				xdefault:
				{
					// All name parts are defined
					if (pSpecies->eNameOrder == kNameOrder_FML)
					{
						pcFirstName = eaNameParts[0];
						pcMiddleName = eaNameParts[1];
						pcLastName = eaNameParts[2];
					}
					else
					{
						pcLastName = eaNameParts[0];
						pcFirstName = eaNameParts[1];
						pcMiddleName = eaNameParts[2];
					}
				}
			}

			// Generate full name
			if (pcLastName && pSpecies->eNameOrder == kNameOrder_LFM)
			{
				if (estrLength(&estrFullName))
					estrConcatChar(&estrFullName, ' ');
				estrAppend2(&estrFullName, pcLastName);
			}
			if (pcFirstName)
			{
				if (estrLength(&estrFullName))
					estrConcatChar(&estrFullName, ' ');
				estrAppend2(&estrFullName, pcFirstName);
			}
			if (pcMiddleName)
			{
				if (estrLength(&estrFullName))
					estrConcatChar(&estrFullName, ' ');
				estrAppend2(&estrFullName, pcMiddleName);
			}
			if (pcLastName && pSpecies->eNameOrder == kNameOrder_FML)
			{
				if (estrLength(&estrFullName))
					estrConcatChar(&estrFullName, ' ');
				estrAppend2(&estrFullName, pcLastName);
			}

			// Validate full name
			if (estrLength(&estrFullName) > 0 && !IsAnyProfane(estrFullName) && !IsAnyRestricted(estrFullName) && !IsDisallowed(estrFullName))
			{
				pEntry = StructCreate(parse_GenNameEntry);
				pEntry->pcName = StructAllocString(estrFullName);
				pEntry->pcFirstName = StructAllocString(pcFirstName);
				pEntry->pcMiddleName = StructAllocString(pcMiddleName);
				pEntry->pcLastName = StructAllocString(pcLastName);
				eaPush(&pNameList->eaNames, pEntry);
				pNameList->iAvailable++;
			}
		}

		estrClear(&estrFullName);
		while (eaSize(&eaNameParts) > 0)
			StructFreeString(eaPop(&eaNameParts));
	}

	estrDestroy(&estrFullName);
	eaDestroy(&eaNameParts);
	return pNameList;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(GenerateRandomNames) ACMD_ACCESSLEVEL(0) ACMD_SERVERONLY ACMD_PRIVATE;
void nameGen_GenerateRandomNames(Entity *e, GenNameListReq *pReq)
{
	GenNameList *pNameList = nameGen_GenerateRandomNamesInternal(pReq);
#ifdef GAMESERVER
	if(pNameList)
		ClientCmd_nameGen_ClientReceiveNames(e, pNameList);
#endif
	StructDestroy(parse_GenNameList, pNameList);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(GenerateListRandomNames) ACMD_ACCESSLEVEL(9) ACMD_SERVERONLY;
void nameGen_GenerateListRandomNames(Entity *e, const char *pcSpecies)
{
	GenNameListReq sReq = {0};
	sReq.pcSpecies = pcSpecies;
	sReq.iListNames = 1;
	nameGen_GenerateRandomNames(e, &sReq);
}

typedef struct NameGenRequest
{
	const char *pcSpecies; // The species of the request
	const char *pcCallbackGenName; // The gen that is to receive notification when the name is ready
	const char *pcCallbackMessage; // The message to send to the gen when the name is ready
	const char *pcCallbackVar; // The var to store the key to the generated name
	S32 iInternal; // The internal name entry (backwards compatibility with old expression interface)
} NameGenRequest;

static NameGenRequest **s_eaRequests; // The pending random name requests
static GenNameListSet s_NameSet; // The buffered list of random names
static const char **s_eapchPending; // The set of species with a pending generation request
static GenNameEntry s_NameEntryInternal[2]; // The internal name entries (backwards compatibility with old expression interface)
static const char *s_NameEntryInternalFilled[2]; // If the internal name entries were filled recently (backwards compatibility with old expression interface)

void nameGen_ClientReceiveNames(GenNameList *pNewList);

void nameGen_FillRequests(void)
{
	S32 i;

	for (i = 0; i < eaSize(&s_eaRequests); i++)
	{
		NameGenRequest *pRequest = s_eaRequests[i];
		GenNameList *pList = eaIndexedGetUsingString(&s_NameSet.eaList, pRequest->pcSpecies);
		GenNameEntry *pEntry = NULL;
		char *pcKey = NULL;
		S32 iConsumed = -1;

		// Need a new list?
		if (!pList)
		{
			eaIndexedEnable(&s_NameSet.eaList, parse_GenNameList);
			pList = StructCreate(parse_GenNameList);
			pList->pcSpecies = pRequest->pcSpecies;
			eaPush(&s_NameSet.eaList, pList);
		}

		if (pList->iAvailable > 0)
		{
			// Consume a name
			iConsumed = eaSize(&pList->eaNames) - pList->iAvailable;
			pEntry = pList->eaNames[iConsumed];
			pList->iAvailable--;
		}
		else if (eaFind(&s_eapchPending, pRequest->pcSpecies) < 0)
		{
			// Request more names
			GenNameListReq Req = {0};
			GenNameList *pNewList = NULL;
			Req.pcSpecies = pRequest->pcSpecies;
			eaPush(&s_eapchPending, pRequest->pcSpecies);

#if GAMECLIENT
			if(GSM_IsStateActive(GCL_LOGIN))
				gclLoginRequestRandomNames(&Req);
			else
				ServerCmd_GenerateRandomNames(&Req);
#else
			pNewList = nameGen_GenerateRandomNamesInternal(&Req);
			nameGen_ClientReceiveNames(pNewList);
			StructDestroySafe(parse_GenNameList, &pNewList);
#endif
		}

		if (pEntry)
		{
			// The name was generated, process the request

#if GAMECLIENT
			UIGen *pGen = pRequest->pcCallbackGenName ? ui_GenFind(pRequest->pcCallbackGenName, kUIGenTypeNone) : NULL;
			if (pGen)
			{
				UIGenVarTypeGlob *pVar = eaIndexedGetUsingString(&pGen->eaVars, pRequest->pcCallbackVar);
				if (pVar)
					estrPrintf(&pVar->pchString, "%s,%d", pRequest->pcSpecies, iConsumed);
				ui_GenSendMessage(pGen, pRequest->pcCallbackMessage);
			}
#endif

			if (0 <= pRequest->iInternal && pRequest->iInternal < ARRAY_SIZE(s_NameEntryInternal))
			{
				StructCopyAll(parse_GenNameEntry, pEntry, &s_NameEntryInternal[pRequest->iInternal]);
				s_NameEntryInternalFilled[pRequest->iInternal] = pRequest->pcSpecies;
			}

			eaRemove(&s_eaRequests, i);
			i--;
			free(pRequest);
		}
	}
}

bool nameGen_ParseKey(const char *pchKey, GenNameEntry **ppEntry)
{
	GenNameList *pList;
	char *pchSpecies, *pchIndex;

	strdup_alloca(pchSpecies, pchKey);
	*ppEntry = NULL;

	pchIndex = strchr(pchSpecies, ',');
	if (!pchIndex)
		return false;
	*pchIndex++ = '\0';

	pList = eaIndexedGetUsingString(&s_NameSet.eaList, pchSpecies);
	if (!pList)
		return false;

	*ppEntry = eaGet(&pList->eaNames, atoi(pchIndex));
	return *ppEntry != NULL;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void nameGen_ClientReceiveNames(GenNameList *pNewList)
{
	GenNameList *pLastList = pNewList ? eaIndexedGetUsingString(&s_NameSet.eaList, pNewList->pcSpecies) : NULL;
	S32 i, iStart;

	if (!pNewList)
		return;

	// Need a new list?
	if (!pLastList)
	{
		eaIndexedEnable(&s_NameSet.eaList, parse_GenNameList);
		pLastList = StructCreate(parse_GenNameList);
		pLastList->pcSpecies = pNewList->pcSpecies;
		eaPush(&s_NameSet.eaList, pLastList);
	}

	// Merge the list of names
	iStart = eaSize(&pNewList->eaNames) - pNewList->iAvailable;
	MAX1(iStart, 0);
	for (i = iStart; i < eaSize(&pNewList->eaNames); i++)
	{
		eaPush(&pLastList->eaNames, StructClone(parse_GenNameEntry, pNewList->eaNames[i]));
		pLastList->iAvailable++;
	}

	// If there's a pending request, handle requests
	if (eaFindAndRemove(&s_eapchPending, pNewList->pcSpecies) >= 0)
		nameGen_FillRequests();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NameGenGetName") ACMD_CLIENTONLY;
const char *nameGenExpr_GetRandomName(const char *pchKey)
{
	GenNameEntry *pEntry;
	if (nameGen_ParseKey(pchKey, &pEntry))
		return pEntry->pcName;
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NameGenGetFirstName") ACMD_CLIENTONLY;
const char *nameGenExpr_GetFirstName(const char *pchKey)
{
	GenNameEntry *pEntry;
	if (nameGen_ParseKey(pchKey, &pEntry))
		return pEntry->pcFirstName;
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NameGenGetMiddleName") ACMD_CLIENTONLY;
const char *nameGenExpr_GetMiddleName(const char *pchKey)
{
	GenNameEntry *pEntry;
	if (nameGen_ParseKey(pchKey, &pEntry))
		return pEntry->pcMiddleName;
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NameGenGetLastName") ACMD_CLIENTONLY;
const char *nameGenExpr_GetLastName(const char *pchKey)
{
	GenNameEntry *pEntry;
	if (nameGen_ParseKey(pchKey, &pEntry))
		return pEntry->pcLastName;
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NameGenNext") ACMD_CLIENTONLY;
void nameGenExpr_GenerateName(ExprContext *pContext, const char *pcSpecies, const char *pcCallbackGenName, const char *pcCallbackMessage, const char *pcCallbackVar)
{
#if GAMECLIENT
	NameGenRequest *pRequest;
	UIGen *pGen = ui_GenFind(pcCallbackGenName, kUIGenTypeNone);

	if (!pGen)
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "NameGenNext: Undefined callback UIGen %s", pcCallbackGenName);
		return;
	}
	if (!ui_GenFindMessage(pGen, pcCallbackMessage))
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "NameGenNext: Undefined callback message %s in UIGen %s", pcCallbackMessage, pcCallbackGenName);
		return;
	}
	if (!eaIndexedGetUsingString(&pGen->eaVars, pcCallbackVar))
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "NameGenNext: Undefined callback var %s in UIGen %s", pcCallbackVar, pcCallbackGenName);
		return;
	}

	pRequest = calloc(1, sizeof(NameGenRequest));
	if (pRequest)
	{
		pRequest->pcSpecies = allocAddString(pcSpecies);
		pRequest->pcCallbackGenName = allocAddString(pcCallbackGenName);
		pRequest->pcCallbackMessage = allocAddString(pcCallbackMessage);
		pRequest->pcCallbackVar = allocAddString(pcCallbackVar);
		pRequest->iInternal = -1;
		eaPush(&s_eaRequests, pRequest);
		nameGen_FillRequests();
	}
#endif
}

//////////////////////////////////////////////////////////////////////////
// TODO(jm): Remove this weird random name polling system

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NameGen_GetCurFirstName") ACMD_CLIENTONLY;
const char *nameGen_GetCurFirstName1(void)
{
	return s_NameEntryInternal[0].pcFirstName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NameGen_GetCurFirstName2") ACMD_CLIENTONLY;
const char *nameGen_GetCurFirstName2(void)
{
	return s_NameEntryInternal[1].pcFirstName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NameGen_GetCurMiddleName") ACMD_CLIENTONLY;
const char *nameGen_GetCurMiddleName1(void)
{
	return s_NameEntryInternal[0].pcMiddleName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NameGen_GetCurMiddleName2") ACMD_CLIENTONLY;
const char *nameGen_GetCurMiddleName2(void)
{
	return s_NameEntryInternal[1].pcMiddleName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NameGen_GetCurLastName") ACMD_CLIENTONLY;
const char *nameGen_GetCurLastName1(void)
{
	return s_NameEntryInternal[0].pcLastName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NameGen_GetCurLastName2") ACMD_CLIENTONLY;
const char *nameGen_GetCurLastName2(void)
{
	return s_NameEntryInternal[1].pcLastName;
}

static const char *nameGen_GetNextRandomName(const char *pcSpecies, int iNameGroupIndex)
{
	S32 i;

	if (iNameGroupIndex < 0 || ARRAY_SIZE(s_NameEntryInternal) <= iNameGroupIndex)
		return "";

	pcSpecies = pcSpecies && *pcSpecies ? allocAddString(pcSpecies) : NULL;

	// Has it been filled since the last request?
	if (pcSpecies && s_NameEntryInternalFilled[iNameGroupIndex] == pcSpecies)
	{
		s_NameEntryInternalFilled[iNameGroupIndex] = NULL;
		return s_NameEntryInternal[iNameGroupIndex].pcName;
	}

	// Find an existing request
	for (i = eaSize(&s_eaRequests) - 1; i >= 0; i--)
	{
		if (s_eaRequests[i]->iInternal == iNameGroupIndex)
			break;
	}
	if (i >= 0 && s_eaRequests[i]->pcSpecies != pcSpecies)
	{
		// Old request is out dated
		free(eaRemove(&s_eaRequests, i));
		i = -1;
	}

	if (pcSpecies && i < 0)
	{
		// Make a new request
		NameGenRequest *pReq = calloc(1, sizeof(NameGenRequest));
		if (pReq)
		{
			pReq->pcSpecies = pcSpecies;
			pReq->iInternal = iNameGroupIndex;
			eaPush(&s_eaRequests, pReq);
			nameGen_FillRequests();

			// Check to see if the request was filled immediately
			if (s_NameEntryInternalFilled[iNameGroupIndex] == pcSpecies)
			{
				s_NameEntryInternalFilled[iNameGroupIndex] = NULL;
				return s_NameEntryInternal[iNameGroupIndex].pcName;
			}
		}
	}
	else if (!pcSpecies)
	{
		// Clear the random name
		StructReset(parse_GenNameEntry, &s_NameEntryInternal[iNameGroupIndex]);
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetNextRandomName") ACMD_CLIENTONLY;
const char *nameGen_GetNextRandomName1(const char *pcSpecies)
{
	return nameGen_GetNextRandomName(pcSpecies, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetNextRandomName2") ACMD_CLIENTONLY;
const char *nameGen_GetNextRandomName2(const char *pcSpecies)
{
	return nameGen_GetNextRandomName(pcSpecies, 1);
}

#include "AutoGen/NameGen_h_ast.c"
