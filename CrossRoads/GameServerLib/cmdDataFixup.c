/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "stdtypes.h"
#include "aiStructCommon.h"
#include "AlgoPet.h"
#include "BlockEarray.h"
#include "cmdparse.h"
#include "contact_common.h"
#include "cutscene_common.h"
#include "CombatEval.h"
#include "encounter_common.h"
#include "entCritter.h"
#include "Entity.h"
#include "Expression.h"
#include "interaction_common.h"
#include "fileutil.h"
#include "gimmeDLLWrapper.h"
#include "gslDataFixupUtils.h"
#include "gslMapVariable.h"
#include "gslPartition.h"
#include "ItemAssignments.h"
#include "itemCommon.h"
#include "logging.h"
#include "MicroTransactions.h"
#include "mission_common.h"
#include "oldencounter_common.h"
#include "PowerAnimFX.h"
#include "Powers.h"
#include "PowerVars.h"
#include "reward.h"
#include "quat.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "structinternals.h"
#include "TextParserInheritance.h"
#include "fileutil2.h"
#include "StringUtil.h"
#include "SimpleParser.h"
#include "WorldLib.h"

#include "../StaticWorld/WorldGridPrivate.h"
#include "../StaticWorld/WorldGridLoadPrivate.h"
#include "../StaticWorld/ZoneMapReport.h"

#include "AutoGen/AlgoPet_h_ast.h"
#include "AutoGen/contact_common_h_ast.h"
#include "AutoGen/entcritter_h_ast.h"
#include "AutoGen/itemAssignments_h_ast.h"
#include "AutoGen/oldencounter_common_h_ast.h"
#include "AutoGen/entityinteraction_h_ast.h"
#include "AutoGen/MicroTransactions_h_ast.h"
#include "AutoGen/mission_common_h_ast.h"
#include "AutoGen/Powers_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "../AutoGen/WorldGridLoadPrivate_h_ast.h"
#include "AutoGen/Powers_h_ast.h"

// --------------------------------------------------------------------------------
// Logic to find mirror geometries for costume parts
// --------------------------------------------------------------------------------

AUTO_COMMAND ACMD_SERVERCMD;
void CostumeConvertMirror(void)
{
	DictionaryEArrayStruct *pGeometries = resDictGetEArrayStruct("CostumeGeometry");
	int i,j;
	char **eaFilenames = NULL;

	// Iterate all geometries
	for(i=eaSize(&pGeometries->ppReferents)-1; i>=0; --i) {
		PCGeometryDef *pGeo = pGeometries->ppReferents[i];
		PCBoneDef *pBone;
		PCBoneDef *pMirrorBone = NULL;
		const char *pcLocalName;

		// Ignore ones that already have a mirror geo
		if (pGeo->pcMirrorGeometry) {
			continue;
		}
		
		// Ignore ones on a non-mirrored bone
		pBone = GET_REF(pGeo->hBone);
		if (pBone) {
			pMirrorBone = GET_REF(pBone->hMirrorBone);
		}
		if (!pMirrorBone) {
			continue;
		}

		// Scan for mirror parts
		pcLocalName = TranslateDisplayMessage(pGeo->displayNameMsg);
		for(j=i-1; j>=0; --j) {
			PCGeometryDef *pOtherGeo = pGeometries->ppReferents[j];
			if (pMirrorBone == GET_REF(pOtherGeo->hBone) && !pGeo->pcMirrorGeometry &&
				(stricmp(pcLocalName, TranslateDisplayMessage(pOtherGeo->displayNameMsg)) == 0)) {
				pGeo->pcMirrorGeometry = allocAddString(pOtherGeo->pcName);
				pOtherGeo->pcMirrorGeometry = allocAddString(pGeo->pcName);
				eaPush(&eaFilenames, (char*)pGeo->pcFileName);
				eaPush(&eaFilenames, (char*)pOtherGeo->pcFileName);
				break;
			}
		}
	}

	// Write out file change
	for(i=eaSize(&eaFilenames)-1; i>=0; --i) {
		printf("## %s\n", eaFilenames[i]);
		ParserWriteTextFileFromDictionary(eaFilenames[i], "CostumeGeometry", 0, 0);
	}
}



// --------------------------------------------------------------------------------
// Conversion functions to convert TakeItem and GrantItem Expressions on Clickables
// to GameActions
// --------------------------------------------------------------------------------

//static bool ConvertGrantItemsCB(const char*** paramList, char **estrReplacementString, OldGameActionBlock *pActionBlock)
//{
//	// Make a new GiveItem action using the Item name
//	if (eaSize(paramList) == 1)
//	{
//		OldGameAction *pAction = StructCreate(parse_OldGameAction);
//		pAction->type = OldGameActionType_GiveItem;
//		eaPush(&pAction->params, MultiValCreate());
//		eaPush(&pAction->params, MultiValCreate());
//		MultiValSetString(pAction->params[0], (*paramList)[0]);
//		MultiValSetInt(pAction->params[1], 1);
//
//		// Add action to the Clickable
//		eaPush(&pActionBlock->ppActions, pAction);
//		return true;
//	}
//	else
//	{
//		Errorf("Error: Incorrect number of parameters on GrantItem Expression (ConvertGrantItemsCB)");
//		return false;
//	}
//}
//
//static bool ConvertTakeItemsCB(const char*** paramList, char **estrReplacementString, OldGameActionBlock *pActionBlock)
//{
//	if (eaSize(paramList) == 2)
//	{
//		// Make a new GiveItem action using the Item name
//		OldGameAction *pAction = StructCreate(parse_OldGameAction);
//		pAction->type = OldGameActionType_TakeItem;
//		eaPush(&pAction->params, MultiValCreate());
//		eaPush(&pAction->params, MultiValCreate());
//		MultiValSetString(pAction->params[0], (*paramList)[0]);
//		MultiValSetInt(pAction->params[1], atoi((*paramList)[1]));
//
//		// Add action to the Clickable
//		eaPush(&pActionBlock->ppActions, pAction);
//		return true;
//	}
//	else
//	{
//		Errorf("Error: Incorrect number of parameters on TakeItem Expression (ConvertTakeItemsCB)");
//		return false;
//	}
//}
//
//static bool ConvertGrantItems(ClickableObject *pClickable, EncounterLayer *pLayer, const char *pcPathString, void *unused)
//{
//	if (pClickable->oldInteractProps.interactAction)
//	{
//		char *origExpr = strdup(exprGetCompleteString(pClickable->oldInteractProps.interactAction));
//		bool success = datafixup_RemoveExprFuncWithCallback(&pClickable->oldInteractProps.interactAction, "GrantItem", pLayer->pchFilename, ConvertGrantItemsCB, &pClickable->oldInteractProps.interactGameActions);
//
//		if (success)
//			printf("Object %s%s:\n\tOrig: %s\n\tNew: %s\n\n", pLayer->name?pLayer->name:pLayer->pchFilename, pcPathString, origExpr, exprGetCompleteString(pClickable->oldInteractProps.interactAction));
//
//		free(origExpr);
//		return success;
//	}
//	return false;
//}
//
//static bool ConvertTakeItems(ClickableObject *pClickable, EncounterLayer *pLayer, const char *pcPathString, void *unused)
//{
//	if (pClickable->oldInteractProps.interactAction)
//	{
//		char *origExpr = strdup(exprGetCompleteString(pClickable->oldInteractProps.interactAction));
//		bool success = datafixup_RemoveExprFuncWithCallback(&pClickable->oldInteractProps.interactAction, "TakeItem", pLayer->pchFilename, ConvertTakeItemsCB, &pClickable->oldInteractProps.interactGameActions);
//
//		if (success)
//			printf("Object %s%s:\n\tOrig: %s\n\tNew: %s\n\n", pLayer->name?pLayer->name:pLayer->pchFilename, pcPathString, origExpr, exprGetCompleteString(pClickable->oldInteractProps.interactAction));
//
//		free(origExpr);
//		return success;
//	}
//	return false;
//}
//
//// This converts all "GiveItem" and "TakeItem" Expressions into GameActions
//AUTO_COMMAND ACMD_SERVERCMD;
//void FixupClickableActions(void)
//{
//	EncounterLayer **eaLayers = NULL;
//	EncounterLayer **eaChangedLayers = NULL;
//	EncounterLayer **eaChangedLayerCopies = NULL;
//	int i, n;
//
//	AssetLoadEncLayers(&eaLayers);
//	AssetPutEncLayersInEditMode(&eaLayers);
//
//	n = eaSize(&eaLayers);
//	for (i = 0; i < n; i++)
//	{
//		EncounterLayer *pLayer = eaLayers[i];
//		
//		// Change GrantItem expression to "GiveItem" Action
//		if (ParserScanForSubstruct(parse_EncounterLayer, pLayer, parse_ClickableObject, 0, TOK_NO_TEXT_SAVE | TOK_NO_WRITE, ConvertGrantItems, NULL))
//		{
//			if (eaFind(&eaChangedLayers, pLayer) == -1)
//			{
//				eaPush(&eaChangedLayers, pLayer);
//				eaPush(&eaChangedLayerCopies, oldencounter_SafeCloneLayer(pLayer));
//			}
//		}
//
//		// Change TakeItem expression to "TakeItem" Action
//		if (ParserScanForSubstruct(parse_EncounterLayer, pLayer, parse_ClickableObject, 0, TOK_NO_TEXT_SAVE | TOK_NO_WRITE, ConvertTakeItems, NULL))
//		{
//			if (eaFind(&eaChangedLayers, pLayer) == -1)
//			{
//				eaPush(&eaChangedLayers, pLayer);
//				eaPush(&eaChangedLayerCopies, oldencounter_SafeCloneLayer(pLayer));
//			}
//		}
//	}
//
//	AssetSaveEncLayers(&eaChangedLayers, &eaChangedLayerCopies, false);
//
//	AssetCleanupEncLayers(&eaLayers);
//	eaDestroy(&eaChangedLayers);
//	eaDestroyStruct(&eaChangedLayerCopies, parse_EncounterLayer);
//}


// --------------------------------------------------------------------------------
// Conversion functions to convert ObsoleteSendFloater Expressions to SendFloaterMsg
// --------------------------------------------------------------------------------

//typedef struct SendFloaterConversionData{
//	Message ***peaNewMessages;
//	EncounterLayer *pLayer;
//	const char *pchObjectType;
//	const char *pchObjectName;
//	const char *pchDescriptionString;
//} SendFloaterConversionData;
//
//static bool MessageExists(Message*** peaMessageList, const char *pchMessageKey)
//{
//	int i, n = eaSize(peaMessageList);
//	for (i = 0; i < n; i++)
//	{
//		Message *pMessage = (*peaMessageList)[i];
//		if (pMessage->pcMessageKey && pchMessageKey && !stricmp(pMessage->pcMessageKey, pchMessageKey))
//			return true;
//	}
//	if (RefSystem_ReferentFromString(gMessageDict, pchMessageKey))
//		return true;
//	return false;
//}
//
//static bool ConvertFloatersCB(const char*** paramList, char **estrReplacementString, SendFloaterConversionData *pData)
//{
//	if (eaSize(paramList) == 4)
//	{
//		Message *pMessage = NULL;
//		int i;
//
//		// See if a message already exists for this String
//		for (i = 0; i < eaSize(pData->peaNewMessages); i++)
//		{
//			if ((*paramList)[0] && (*pData->peaNewMessages)[i]->pcDefaultString && !strcmp((*paramList)[0], (*pData->peaNewMessages)[i]->pcDefaultString))
//			{
//				pMessage = (*pData->peaNewMessages)[i];
//				break;
//			}
//		}
//		
//		if (!pMessage)
//		{
//			int iMessageUniqueID = 1;
//			char *estrKeyStr = NULL;
//			char *estrDesiredKeyStr = NULL;
//			char *estrScope = NULL;
//			char layerScope[MAX_PATH];
//			char layerKey[MAX_PATH];
//			estrStackCreate(&estrKeyStr);
//			estrStackCreate(&estrDesiredKeyStr);
//			estrStackCreate(&estrScope);
//
//			oldencounter_GetLayerKeyPath(layerKey, pData->pLayer->pchFilename);
//			encounterlayer_GetLayerScopePath(layerScope, pData->pLayer->pchFilename);
//
//			estrPrintf(&estrDesiredKeyStr, "%s.%s.%s.interactFloater", layerKey, pData->pchObjectType, pData->pchObjectName);
//			estrPrintf(&estrScope, "%s/%ss/%s", layerScope, pData->pchObjectType, pData->pchObjectName);
//
//			// Make a unique Key
//			estrPrintf(&estrKeyStr, "%s", estrDesiredKeyStr);
//			while(MessageExists(pData->peaNewMessages, estrKeyStr))
//			{
//				estrPrintf(&estrKeyStr, "%s%d", estrDesiredKeyStr, ++iMessageUniqueID);
//			}
//			
//			pMessage = langCreateMessage(estrKeyStr, pData->pchDescriptionString, estrScope, (*paramList)[0]);
//			pMessage->pcFilename = allocAddFilename("messages\\FixedEncLayerFloaters.ms");
//			eaPush(pData->peaNewMessages, pMessage);
//			
//			estrDestroy(&estrKeyStr);
//			estrDestroy(&estrDesiredKeyStr);
//			estrDestroy(&estrScope);
//		}
//
//		// Replace with "SendFloaterMsg"
//		estrPrintf(estrReplacementString, "SendFloaterMsg(\"%s\", %s, %s, %s)", pMessage->pcMessageKey, (*paramList)[1], (*paramList)[2], (*paramList)[3]);
//		return true;
//	}
//	else
//	{
//		Errorf("Error: Incorrect number of parameters to ConvertFloatersCB");
//		return false;
//	}
//}
//
//static bool ConvertFloatersClickables(ClickableObject *pClickable, EncounterLayer *pLayer, const char *pcPathString, Message ***peaNewMessages)
//{
//	SendFloaterConversionData data = {0};
//	char *origExpr = strdup(exprGetCompleteString(pClickable->oldInteractProps.interactAction));
//	char *pchDescriptionString = NULL;
//	bool success = false;
//
//	data.peaNewMessages = peaNewMessages;
//	data.pLayer = pLayer;
//	data.pchObjectName = pClickable->name;
//	data.pchObjectType = "Clickable";
//
//	estrStackCreate(&pchDescriptionString);
//	estrPrintf(&pchDescriptionString, "Text that displays after successfully interacting with Clickable '%s'", pClickable->name);
//	data.pchDescriptionString = pchDescriptionString;
//
//	success = datafixup_RemoveExprFuncWithCallback(&pClickable->oldInteractProps.interactAction, "ObsoleteSendFloater", pLayer->pchFilename, ConvertFloatersCB, &data);
//
//	if (success)
//		printf("Object %s%s:\n\tOrig: %s\n\tNew: %s\n\n", pLayer->name?pLayer->name:pLayer->pchFilename, pcPathString, origExpr, exprGetCompleteString(pClickable->oldInteractProps.interactAction));
//
//	estrDestroy(&pchDescriptionString);
//	free(origExpr);
//	return success;
//}
//
//
//// This converts all "ObsoleteSendFloater" Expressions into localized "SendFloaterMsg" Expressions.
//AUTO_COMMAND ACMD_SERVERCMD;
//void FixupSendFloaters(void)
//{
//	EncounterLayer **eaLayers = NULL;
//	EncounterLayer **eaLayerCopies = NULL;
//	EncounterLayer **eaChangedLayers = NULL;
//	Message **ppNewMessages = NULL;
//	const char **ppMessageFilenames = NULL;
//	int i, n;
//
//	AssetLoadEncLayers(&eaLayers);
//	AssetPutEncLayersInEditMode(&eaLayers);
//
//	n = eaSize(&eaLayers);
//	for (i = 0; i < n; i++)
//	{
//		EncounterLayer *pLayer = eaLayers[i];
//		EncounterLayer *pOrigLayer = oldencounter_SafeCloneLayer(pLayer);
//		bool bChanged = false;
//		
//		// Change ObsoleteSendFloater Expression to "SendFloaterMsg"
//
//		// Scan Clickables
//		if (ParserScanForSubstruct(parse_EncounterLayer, pLayer, parse_ClickableObject, 0, TOK_NO_TEXT_SAVE | TOK_NO_WRITE, ConvertFloatersClickables, &ppNewMessages))
//			bChanged = true;
//
//		if (bChanged && eaFind(&eaChangedLayers, pLayer) == -1)
//		{
//			eaPush(&eaChangedLayers, pLayer);
//			eaPush(&eaLayerCopies, pOrigLayer);
//		}
//		else
//		{
//			StructDestroy(parse_EncounterLayer, pOrigLayer);
//		}
//	}
//
//	// Save Layers
//	AssetSaveEncLayers(&eaChangedLayers, &eaLayerCopies, false);
//	
//	// Save Messages
//	for (i = 0; i < eaSize(&ppNewMessages); i++)
//	{
//		RefSystem_AddReferent(gMessageDict, ppNewMessages[i]->pcMessageKey, ppNewMessages[i]);
//		eaPushUnique(&ppMessageFilenames, ppNewMessages[i]->pcFilename);
//	}
//
//	for (i = 0; i < eaSize(&ppMessageFilenames); i++)
//	{
//		ParserWriteTextFileFromDictionary(ppMessageFilenames[i], gMessageDict, 0, 0);
//	}
//
//	AssetCleanupEncLayers(&eaLayers);
//	eaDestroy(&eaChangedLayers);
//	eaDestroyStruct(&eaLayerCopies, parse_EncounterLayer);
//	eaDestroy(&ppNewMessages);
//	eaDestroy(&ppMessageFilenames);
//}

/*
static bool FixupRespawnFlagOnClickable(ClickableObject *pClickable, EncounterLayer *pLayer, const char *pcPathString, void * unused)
{
	bool retVal = false;

	if(pClickable)
	{
		InteractionProperties* props = &pClickable->oldInteractProps;

		if(props)
		{
			if(props->uInteractActiveFor || props->uInteractTime)
			{
				if(props->uInteractCoolDown == 0)
				{
					pClickable->oldInteractProps.eInteractType = pClickable->oldInteractProps.eInteractType | InteractType_NoRespawn;
					retVal = true;
				}
			}
		}
	}

	return retVal;
}

AUTO_COMMAND ACMD_SERVERCMD;
void FixupClickables1(void)
{
	EncounterLayer **eaLayers = NULL;
	EncounterLayer **eaLayerCopies = NULL;
	EncounterLayer **eaChangedLayers = NULL;
	int i, n;

	printf("Adding Do Not Respawn flag to all clickables with active times and no cooldown\n");

	AssetLoadLayers(&eaLayers);
	AssetPutLayersInEditMode(&eaLayers);

	n = eaSize(&eaLayers);
	for (i = 0; i < n; i++)
	{
		EncounterLayer *pLayer = eaLayers[i];
		EncounterLayer *pOrigLayer =  oldencounter_SafeCloneLayer(parse_EncounterLayer, pLayer);
		bool bChanged = false;

		if (ParserScanForSubstruct(parse_EncounterLayer, pLayer, parse_ClickableObject, 0, TOK_NO_TEXT_SAVE | TOK_NO_WRITE, FixupRespawnFlagOnClickable, NULL))
			bChanged = true;

		if (bChanged && eaFind(&eaChangedLayers, pLayer) == -1)
		{
			eaPush(&eaChangedLayers, pLayer);
			eaPush(&eaLayerCopies, pOrigLayer);
		}
		else
		{
			StructDestroy(parse_EncounterLayer, pOrigLayer);
		}
	}

	// Save Layers
	AssetSaveEncLayers(&eaChangedLayers, &eaLayerCopies, false);

	AssetCleanupEncLayers(&eaLayers);
	eaDestroy(&eaChangedLayers);
	eaDestroyStruct(&eaLayerCopies, parse_EncounterLayer);
}
*/

/*

static bool MoveDoorTargetToInteractProperties(ClickableObject *pClickable, EncounterLayer *pLayer, const char *pcPathString, void * unused)
{
	bool retVal = false;
	if(pClickable)
	{
		InteractionProperties* props = &pClickable->oldInteractProps;

		if(pClickable->mapName)
		{
			props->mapName = pClickable->mapName;
			pClickable->mapName = NULL;
			retVal = true;
		}
		if(pClickable->spawnTarget)
		{
			props->spawnTarget = pClickable->spawnTarget;
			pClickable->spawnTarget = NULL;
			retVal = true;
		}
	}

	return retVal;
}

AUTO_COMMAND ACMD_SERVERCMD;
void FixupClickableDoors(void)
{
	EncounterLayer **eaLayers = NULL;
	EncounterLayer **eaLayerCopies = NULL;
	EncounterLayer **eaChangedLayers = NULL;
	EncounterLayer **eaChangedLayerCopies = NULL;
	int i, n;

	printf("Running FixupClickableDoors command\n");

	AssetLoadEncLayers(&eaLayers);
	//AssetPutEncLayersInEditMode(&eaLayers);

	n = eaSize(&eaLayers);
	for (i = 0; i < n; i++)
	{
		EncounterLayer *pLayer = eaLayers[i];

		if (ParserScanForSubstruct(parse_EncounterLayer, pLayer, parse_ClickableObject, 0, TOK_NO_TEXT_SAVE | TOK_NO_WRITE, MoveDoorTargetToInteractProperties, NULL))
		{
			if (eaFind(&eaChangedLayers, pLayer) == -1)
			{
				eaPush(&eaChangedLayers, pLayer);
				eaPush(&eaChangedLayerCopies, oldencounter_SafeCloneLayer(pLayer));
			}
		}
	}

	AssetSaveEncLayers(&eaChangedLayers, &eaChangedLayerCopies, true);

	AssetCleanupEncLayers(&eaLayers);
	eaDestroy(&eaChangedLayers);
	eaDestroyStruct(&eaChangedLayerCopies, parse_EncounterLayer);
}

*/

// --------------------------------------------------------------------------------
// MissionDef Fixup
// --------------------------------------------------------------------------------

// Link Mission Items to the appropriate MissionDef
AUTO_COMMAND;
void FixUnlinkedMissionItems(void)
{
	int i, n;
	int iNumFixed = 0;
	int iNumSaved = 0;
	RefDictIterator iterator = {0};
	MissionDef *pDef = NULL;
	ItemDef **changedDefCopies = NULL;

	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);
	
	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		ItemDef **ppItemDefList = NULL;
		missiondef_fixup_FindItemsForMissionRecursive(pDef, &ppItemDefList);

		n = eaSize(&ppItemDefList);
		for (i = 0; i < n; i++)
		{
			ItemDef *pItemDef = ppItemDefList[i];
			if (pItemDef && pItemDef->eType == kItemType_Mission)
			{
				MissionDef *pLinkedDef = GET_REF(pItemDef->hMission);
				if (!pLinkedDef)
				{
					ItemDef *pItemDefCopy = StructClone(parse_ItemDef, pItemDef);
					if (pItemDefCopy)
					{
						SET_HANDLE_FROM_STRING(g_MissionDictionary, pDef->name, pItemDefCopy->hMission);
						SET_HANDLE_FROM_STRING(g_MissionDictionary, pDef->name, pItemDef->hMission);
						eaPush(&changedDefCopies, pItemDefCopy);
						iNumFixed++;
					}
				}
				else if(pLinkedDef && pLinkedDef != pDef)
					Errorf("Error: Mission %s takes Item %s, but Item references %s!", pDef->name, pItemDef->pchName, pLinkedDef->name);
			}
		}
		eaDestroy(&ppItemDefList);
	}
	printf("Total Fixed: %d\n", iNumFixed);
	
	// Write out all affected files
	n = eaSize(&changedDefCopies);
	for (i = 0; i < n; i++)
	{
		ItemDef *pItemDef = changedDefCopies[i];
		if (pItemDef)
		{
			if (ParserWriteTextFileFromSingleDictionaryStruct(pItemDef->pchFileName, g_hItemDict, pItemDef, 0, 0))
				iNumSaved++;
		}
	}
	eaDestroyStruct(&changedDefCopies, parse_ItemDef);
	printf("Total Saved: %d\n", iNumSaved);
}


// Fixup to set the category on star cluster missions to Exploration
AUTO_COMMAND;
void FixStarClusterMissionCategories()
{
	int i,n = 0;
	int iNumFixed = 0;
	int iNumSaved = 0;
	RefDictIterator iterator = {0};
	MissionDef *pDef = NULL;
	MissionDef **changedDefCopies = NULL;

	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);

	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		MissionCategory* pCategory = RefSystem_ReferentFromString(g_MissionCategoryDict, "Exploration");
		if(pCategory && strStartsWith(pDef->name, "Sc_")) {
			MissionDef* pDefCopy = NULL;
			pDefCopy = StructClone(parse_MissionDef, pDef);
			if(pDefCopy) {
				SET_HANDLE_FROM_REFERENT(g_MissionCategoryDict, pCategory, pDefCopy->hCategory);
				eaPush(&changedDefCopies, pDefCopy);
				iNumFixed++;
			}
		}
	}
	printf("Total Fixed: %d\n", iNumFixed);

	// Write out all affected files
	n = eaSize(&changedDefCopies);
	for (i = 0; i < n; i++)
	{
		MissionDef *pMissionDef = changedDefCopies[i];
		if (pMissionDef)
		{
			if (ParserWriteTextFileFromSingleDictionaryStruct(pMissionDef->filename, g_MissionDictionary, pMissionDef, 0, 0))
				iNumSaved++;
		}
	}
	eaDestroyStruct(&changedDefCopies, parse_MissionDef);
	printf("Total Saved: %d\n", iNumSaved);
}

// Fixup to change star cluster missions to use a door key game action instead of a reward table
AUTO_COMMAND;
void FixStarClusterDoorKeyGameActions(bool bSimulateFixup)
{
	int i;
	int iNumFixup = 0;
	int iNumSaved = 0;
	RefDictIterator iterator = {0};
	MissionDef *pDef = NULL;
	MissionDef **changedDefCopies = NULL;

	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);

	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		if (pDef->params && 
			strStartsWith(pDef->name, "Sc_") && 
			stricmp(pDef->params->OnstartRewardTableName,"Star_Cluster_On_Start")==0) 
		{
			MissionDef* pDefCopy = NULL;
			pDefCopy = StructClone(parse_MissionDef, pDef);
			if (pDefCopy) {
				WorldVariableDef* pDestDef = StructCreate(parse_WorldVariableDef);
				WorldGameActionProperties* pAction = StructCreate(parse_WorldGameActionProperties);
				pAction->eActionType = WorldGameActionType_GiveDoorKeyItem;
				pAction->pGiveDoorKeyItemProperties = StructCreate(parse_WorldGiveDoorKeyItemActionProperties);
				SET_HANDLE_FROM_STRING("ItemDef", ITEM_MISSION_DOOR_KEY_DEF, pAction->pGiveDoorKeyItemProperties->hItemDef);
				pAction->pGiveDoorKeyItemProperties->pDestinationMap = pDestDef;
				pDestDef->eType = WVAR_MAP_POINT;
				pDestDef->pSpecificValue = StructCreate(parse_WorldVariable);
				pDestDef->pSpecificValue->eType = WVAR_MAP_POINT;
				eaPush(&pDefCopy->ppOnStartActions, pAction);
				
				pDefCopy->params->OnstartRewardTableName = NULL;
				eaPush(&changedDefCopies, pDefCopy);
			}
		}
	}
	iNumFixup = eaSize(&changedDefCopies);
	printf("Number of missions that require fixup: %d\n", iNumFixup);

	// Write out all affected files
	for (i = 0; i < iNumFixup; i++)
	{
		MissionDef *pMissionDef = changedDefCopies[i];
		if (pMissionDef)
		{
			if (bSimulateFixup)
			{
				printf("Mission '%s' requires fixup\n", pMissionDef->name);
				continue;
			}

			if (!fixupCheckoutFile(pMissionDef->filename, true))
				continue;

			if (ParserWriteTextFileFromSingleDictionaryStruct(pMissionDef->filename, g_MissionDictionary, pMissionDef, 0, 0))
				iNumSaved++;
		}
	}
	eaDestroyStruct(&changedDefCopies, parse_MissionDef);
	printf("Number of missions saved: %d\n", iNumSaved);
}

// Fixup to multiply MicroTransactionDef prices by 1.25 for Zen conversion
AUTO_COMMAND;
void FixMicroTransactionZenPricing(bool bSimulateFixup)
{
	int i;
	int iNumFixup = 0;
	int iNumSaved = 0;
	RefDictIterator iterator = {0};
	MicroTransactionDef *pDef = NULL;
	MicroTransactionDef **changedDefCopies = NULL;

	RefSystem_InitRefDictIterator(g_hMicroTransDefDict, &iterator);

// Magical integer arithmetic to multiply by 1.25 and round up
#define ZEN_PRICE_CONVERT(price) (price) = (((price) * 5 + 3) / 4)

	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		if (pDef->uiPrice ||
			(pDef->pProductConfig && pDef->pProductConfig->uiOverridePrice) ||
			(pDef->pReclaimProductConfig && pDef->pReclaimProductConfig->uiOverridePrice))
		{
			MicroTransactionDef *pDefCopy = StructClone(parse_MicroTransactionDef, pDef);

			ZEN_PRICE_CONVERT(pDefCopy->uiPrice);

			if (pDefCopy->pProductConfig)
				ZEN_PRICE_CONVERT(pDefCopy->pProductConfig->uiOverridePrice);

			if (pDefCopy->pReclaimProductConfig)
				ZEN_PRICE_CONVERT(pDefCopy->pReclaimProductConfig->uiOverridePrice);

			eaPush(&changedDefCopies, pDefCopy);
		}
	}

#undef ZEN_PRICE_CONVERT

	iNumFixup = eaSize(&changedDefCopies);
	printf("Number of microtransactions that require fixup: %d\n", iNumFixup);

	// Write out all affected files
	for (i = 0; i < iNumFixup; i++)
	{
		MicroTransactionDef *pMicrotransactionDef = changedDefCopies[i];
		if (pMicrotransactionDef)
		{
			if (bSimulateFixup)
			{
				printf("Microtransaction '%s' requires fixup\n", pMicrotransactionDef->pchName);
				continue;
			}

			if (!fixupCheckoutFile(pMicrotransactionDef->pchFile, true))
				continue;

			if (ParserWriteTextFileFromSingleDictionaryStruct(pMicrotransactionDef->pchFile, g_hMicroTransDefDict, pMicrotransactionDef, 0, 0))
			{
				printf("Microtransaction '%s' was saved successfully\n", pMicrotransactionDef->pchName);
				iNumSaved++;
			}
		}
	}

	eaDestroyStruct(&changedDefCopies, parse_MicroTransactionDef);
	printf("Number of microtransactions saved: %d\n", iNumSaved);
}

// --------------------------------------------------------------------
// Fixup old message key formats
// --------------------------------------------------------------------

/// By default, simulate updating message keys. 
bool simulateUpdate = true;
AUTO_CMD_INT( simulateUpdate, simulateUpdate );

static FileScanAction ScanLayerFiles( char* dir, struct _finddata32_t* data, char*** filenames )
{
	char fullPath[ MAX_PATH ];
	sprintf( fullPath, "%s/%s", dir, data->name );
	
	if( !(data->attrib & _A_SUBDIR) && strEndsWith( data->name, ".layer" )) {
		eaPush( filenames, strdup( fullPath ));
	}

	return FSA_EXPLORE_DIRECTORY;
}

int UpdateMessageKey( FILE* log, DisplayMessage* displayMsg, const char* layerFName, const char* groupName, const char* scopeName, int* num )
{
	const char* messageKey = REF_STRING_FROM_HANDLE( displayMsg->hMessage );
	const char* messageKeyDesired = groupDefMessageKeyRaw( layerFName, groupName, scopeName, num, false );

	if(!messageKey || stricmp( messageKey, messageKeyDesired ) != 0 ) {
		if (log)
			fprintf( log, "  Key: %s => %s\n", messageKey, messageKeyDesired );

		if( !simulateUpdate ) {
			langMakeEditorCopy( parse_DisplayMessage, displayMsg, true );
			displayMsg->pEditorCopy->pcMessageKey = messageKeyDesired;
		}

		return 1;
	} else {
		return 0;
	}
}

int UpdateMessageKeyWorldVariableDefList( FILE* log, WorldVariableDef** vars, char* layerFName, char* groupName, char* scopeFmt )
{
	int numChangedAccum = 0;
	
	int it;
	for( it = 0; it != eaSize( &vars ); ++it ) {
		char buffer[ 256 ];

		sprintf( buffer, FORMAT_OK(scopeFmt), it );
		if( vars[ it ]->pSpecificValue ) {
			numChangedAccum += UpdateMessageKey( log, &vars[ it ]->pSpecificValue->messageVal, layerFName, groupName, buffer, NULL );
		}
	}

	return numChangedAccum;
}

int UpdateMessageKeyGameActions( FILE* log, WorldGameActionProperties** props, const char* layerFName, const char* groupName, const char* scopeName )
{
	int numChangedAccum = 0;
	
	int messageIt = 0;
	int it;
	for( it = 0; it != eaSize( &props ); ++it ) {
		WorldGameActionProperties* prop = props[ it ];

		if( prop->pSendFloaterProperties ) {
			numChangedAccum += UpdateMessageKey( log, &prop->pSendFloaterProperties->floaterMsg, layerFName, groupName, scopeName, &messageIt );
			++messageIt;
		}
		if( prop->pSendNotificationProperties ) {
			numChangedAccum += UpdateMessageKey( log, &prop->pSendNotificationProperties->notifyMsg, layerFName, groupName, scopeName, &messageIt );
			++messageIt;
		}
		if( prop->pWarpProperties ) {
			//NOT SUPPORTED IN EDITOR: UpdateMessageKeyWorldVariableDefList( log, prop->pWarpProperties->eaVariables );
		}
		if( prop->pNPCSendEmailProperties ) {
			numChangedAccum += UpdateMessageKey( log, &prop->pNPCSendEmailProperties->dFromName, layerFName, groupName, scopeName, &messageIt );
			++messageIt;
			numChangedAccum += UpdateMessageKey( log, &prop->pNPCSendEmailProperties->dSubject, layerFName, groupName, scopeName, &messageIt );
			++messageIt;
			numChangedAccum += UpdateMessageKey( log, &prop->pNPCSendEmailProperties->dBody, layerFName, groupName, scopeName, &messageIt );
			++messageIt;
		}
	}

	return numChangedAccum;
}

int UpdateMessageKeyInteractionPropertyList( FILE* log, WorldInteractionPropertyEntry** entries, char* layerFName, char* groupName )
{
	int numChangedAccum = 0;
	
	int it;
	for( it = 0; it != eaSize( &entries ); ++it ) {
		if( entries[ it ]->pActionProperties ) {
			char buffer[ 256 ];
			sprintf( buffer, "successactions%d", it );
			numChangedAccum += UpdateMessageKeyGameActions( log, entries[ it ]->pActionProperties->successActions.eaActions, layerFName, groupName, buffer );
		}
		if( entries[ it ]->pTextProperties ) {
			char buffer[ 256 ];
			
			sprintf( buffer, "interactOptionText%d", it );
			numChangedAccum += UpdateMessageKey( log, &entries[ it ]->pTextProperties->interactOptionText, layerFName, groupName, buffer, NULL );
			sprintf( buffer, "successText%d", it );
			numChangedAccum += UpdateMessageKey( log, &entries[ it ]->pTextProperties->successConsoleText, layerFName, groupName, buffer, NULL );
			sprintf( buffer, "failureText%d", it );
			numChangedAccum += UpdateMessageKey( log, &entries[ it ]->pTextProperties->failureConsoleText, layerFName, groupName, buffer, NULL );
		}
		if( entries[ it ]->pDoorProperties ) {
			char buffer[ 256 ];
			sprintf( buffer, "varMessage%d-%%d", it );
			numChangedAccum += UpdateMessageKeyWorldVariableDefList( log, entries[ it ]->pDoorProperties->eaVariableDefs, layerFName, groupName, buffer );
		}
		if( entries[ it ]->pDestructibleProperties ) {
			char buffer[ 256 ];
			
			sprintf( buffer, "InteractName%d", it );
			numChangedAccum += UpdateMessageKey( log, &entries[ it ]->pDestructibleProperties->displayNameMsg, layerFName, groupName, buffer, NULL );
		}
	}

	return numChangedAccum;
}

int UpdateMessageKeyOptionalActionList( FILE* log, WorldOptionalActionVolumeEntry** entries, const char* layerFName, const char* groupName )
{
	int numChangedAccum = 0;
	
	int it;
	for( it = 0; it != eaSize( &entries ); ++it ) {
		numChangedAccum += UpdateMessageKey( log, &entries[ it ]->display_name_msg, layerFName, groupName, "Optionalaction", &it);
		numChangedAccum += UpdateMessageKeyGameActions( log, entries[ it ]->actions.eaActions, layerFName, groupName, "optactAction" );
	}

	return numChangedAccum;
}

int UpdateMessageKeyLayer( FILE* log, LibFileLoad* layer, char* layerFName )
{
	int numChangedAccum = 0;
	
	FOR_EACH_IN_EARRAY( layer->defs, GroupDef, defLoad ) {
		if( defLoad->property_structs.interaction_properties ) {
			numChangedAccum += UpdateMessageKey( log, &defLoad->property_structs.interaction_properties->displayNameMsg,
												 layerFName, (char*)defLoad->name_str, "displayNameBasic", NULL );
			numChangedAccum += UpdateMessageKeyInteractionPropertyList( log, defLoad->property_structs.interaction_properties->eaEntries,
																		layerFName, (char*)defLoad->name_str );
		}
		if( defLoad->property_structs.server_volume.warp_volume_properties ) {
			char buffer[ 256 ];
			strcpy( buffer, "varMessage%d" );
			numChangedAccum += UpdateMessageKeyWorldVariableDefList( log, defLoad->property_structs.server_volume.warp_volume_properties->variableDefs, layerFName, (char*)defLoad->name_str, buffer );
		}
		if( defLoad->property_structs.server_volume.landmark_volume_properties ) {
			numChangedAccum += UpdateMessageKey( log, &defLoad->property_structs.server_volume.landmark_volume_properties->display_name_msg,
												 layerFName, (char*)defLoad->name_str, "landmarkDispName", NULL );
		}
		if( defLoad->property_structs.server_volume.neighborhood_volume_properties ) {
			numChangedAccum += UpdateMessageKey( log, &defLoad->property_structs.server_volume.neighborhood_volume_properties->display_name_msg,
												 layerFName, (char*)defLoad->name_str, "neighborhoodDispName", NULL );
		}
		if( defLoad->property_structs.server_volume.interaction_volume_properties ) {
			numChangedAccum += UpdateMessageKeyInteractionPropertyList( log, defLoad->property_structs.server_volume.interaction_volume_properties->eaEntries,
																   layerFName, (char*)defLoad->name_str );
		}
		if( defLoad->property_structs.layer_fsm_properties ) {
			//COMPLEX TO DO, ASSUME IT ALWAYS WORKED: -- UpdateMessageKeyWorldVariableDefList( log, defLoad->property_structs.layer_fsm_properties->fsmVars );
		}
	} FOR_EACH_END;

	return numChangedAccum;
}

/// Update all message keys across all layers
AUTO_COMMAND;
void UpdateAllMessageKeys( void )
{
	FILE* log = fopen( "c:\\UpdateAllMessageKeys.txt", "w" );
	char** layerFiles = NULL;
	fileScanAllDataDirs( "maps/", ScanLayerFiles, &layerFiles );

	sharedMemoryEnableEditorMode();

	{
		time_t rawtime;
		char buffer[ 256 ];

		time( &rawtime );
		ctime_s( SAFESTR(buffer), &rawtime );
		fprintf( log, "-*- truncate-lines: t -*-\nMESSAGE KEY CONVERSION -- %s\n", buffer );
	}

	FOR_EACH_IN_EARRAY( layerFiles, char, filename ) {
		LibFileLoad libFile = { 0 };

		if( !ParserReadTextFile( filename, parse_LibFileLoad, &libFile, 0 )) {
			fprintf( log, "Layer: %s -- Unable to read\n", filename );
			continue;
		}

		fprintf( log, "Layer: %s -- Starting conversion...", filename );
		{
			long beforeNLPos;
			int numChanged;

			beforeNLPos = ftell( log );
			fputc( '\n', log );
			numChanged = UpdateMessageKeyLayer( log, &libFile, filename );
			
			if( numChanged == 0 ) {
				fseek( log, beforeNLPos, SEEK_SET );
				fprintf( log, "done, no changes.\n" );
			} else {
				if( !simulateUpdate ) {
					langApplyEditorCopySingleFile( parse_LibFileLoad, &libFile, true, false );
					ParserWriteTextFile( filename, parse_LibFileLoad, &libFile, 0, 0 );
				}
			
				fprintf( log, "done, %d messages changed.\n", numChanged );
			}
		}

		StructDeInit( parse_LibFileLoad, &libFile );
	} FOR_EACH_END;

	fprintf( log, "DONE\n" );

	eaDestroyEx( &layerFiles, NULL );
	fclose( log );
}

/// Update all the message keys in a specific layer
AUTO_COMMAND;
void UpdateLayerMessageKeys( char* layerFName )
{
	FILE* log = fopen( "c:\\UpdateLayerMessageKeys.txt", "w" );

	sharedMemoryEnableEditorMode();
	
	{
		time_t rawtime;
		char buffer[ 256 ];
		time( &rawtime );
		ctime_s( SAFESTR(buffer), &rawtime );
		fprintf( log, "-*- truncate-lines: t -*-\nMESSAGE KEY CONVERSION -- %s\n", buffer );
	}

	{
		LibFileLoad libFile = { 0 };

		if( !ParserReadTextFile( layerFName, parse_LibFileLoad, &libFile, 0 )) {
			fprintf( log, "Layer: %s -- Unable to read\n", layerFName );
		} else {
			fprintf( log, "Layer: %s -- Starting conversion...", layerFName );
			{
				long beforeNLPos;
				int numChanged;

				beforeNLPos = ftell( log );
				fputc( '\n', log );
				numChanged = UpdateMessageKeyLayer( log, &libFile, layerFName );

				if( numChanged == 0 ) {
					fseek( log, beforeNLPos, SEEK_SET );
					fprintf( log, "done, no changes.\n" );
				} else {
					if( !simulateUpdate ) {
						langApplyEditorCopySingleFile( parse_LibFileLoad, &libFile, true, false );
						ParserWriteTextFile( layerFName, parse_LibFileLoad, &libFile, 0, 0 );
					}
				
					fprintf( log, "done, %d messages changed.\n", numChanged );
				}
			}
			
			StructDeInit( parse_LibFileLoad, &libFile );
		}
	}

	fprintf( log, "DONE\n" );
	fclose( log );
}

//// Fixes OnReturn GameActions related to the given Item to take the specified Count
//// returns TRUE if a change was made
//static bool FixMissionTakeItemCounts_FixActionsForItem(MissionDef *pDef, const char *pchItemName, int count)
//{
//	int i, n = eaSize(&pDef->ppOldOnReturnActions);
//	for (i = 0; i < n; i++)
//	{
//		OldGameAction *action = pDef->ppOldOnReturnActions[i];
//		if(action->type == OldGameActionType_TakeItem 
//			&& MultiValGetString(action->params[0], NULL)
//			&& pchItemName
//			&& !stricmp(MultiValGetString(action->params[0], NULL), pchItemName))
//		{
//			int currentCount = MultiValGetInt(action->params[1], NULL);
//			if (currentCount != -1 && currentCount != count)
//			{
//				Errorf("Error:  Mission %s takes %d of item %s, but requires %d!", pDef->pchRefString, currentCount, pchItemName, count);
//				return false;
//			}
//			else if (currentCount == -1)
//			{
//				MultiValSetInt(action->params[1], count);
//				return true;
//			}
//			else if (currentCount == count)
//				return true;
//		}
//	}
//	return false;
//}
//
//
//// returns TRUE if a change was made
//static bool FixMissionTakeItemCounts_CheckConditionsRecursive(MissionDef *pRootDef, MissionDef *pDef, MissionEditCond *pCond)
//{
//	bool result = false;
//
//	if (!pCond)
//		return false;
//
//	if (pCond->type == MissionCondType_Expression)
//	{
//		char *tmp = NULL;
//		char *pchItemName = NULL;
//		int numRequired = 0;
//		if (pCond->valStr && (tmp = strstri(pCond->valStr, "playeritemcount(")))
//		{
//			tmp += strlen("playeritemcount(");
//			if (strchr(tmp, '('))
//			{
//				//This has another set of parenthesis; too complicated for automatic fix-up
//				return false;
//			}
//
//			if (tmp && *tmp == '"')
//				tmp++;
//			if (tmp && *tmp)
//			{
//				ItemDef *pItemDef = NULL;
//				pchItemName = strdup(tmp);
//				tmp = strchr(pchItemName, ')');
//				if (tmp)
//				{
//					*tmp = '\0';
//					tmp--;
//					if (*tmp == '"')
//						*tmp = '\0';
//				}
//
//				// Find the number of items required
//				if (strstr(pCond->valStr, ">="))
//				{
//					char *count = strstr(pCond->valStr, ">=");
//					count += 2;
//					numRequired = atoi(count);
//				}
//				else
//					numRequired = 1;
//
//
//				// Now, find an Action that takes away this Item
//				result = FixMissionTakeItemCounts_FixActionsForItem(pRootDef, pchItemName, numRequired);
//				if (!result)
//					result = FixMissionTakeItemCounts_FixActionsForItem(pDef, pchItemName, numRequired);
//				if (!result)
//				{
//					Errorf("Error: Couldn't fix %s", pDef->pchRefString);
//				}
//				free(pchItemName);
//			}
//			else
//			{
//				Errorf("Error parsing string %s for Mission %s", pCond->valStr, pRootDef->name);
//				return false;
//			}
//		}
//	}
//	else if (pCond->type == MissionCondType_And)
//	{
//		int i, n = eaSize(&pCond->subConds);
//		for (i = 0; i < n; i++)
//		{
//			result |= FixMissionTakeItemCounts_CheckConditionsRecursive(pRootDef, pDef, pCond->subConds[i]);
//		}
//	}
//	return result;
//}
//
//static bool FixMissionTakeItemCountsRecursive(MissionDef *pDef, MissionDef *pRootDef)
//{
//	int i, n = eaSize(&pDef->subMissions);
//	bool result = false;
//	result |= FixMissionTakeItemCounts_CheckConditionsRecursive(pRootDef, pDef, pDef->meSuccessCond);
//	for (i = 0; i < n; i++)
//	{
//		result |= FixMissionTakeItemCountsRecursive(pDef->subMissions[i], pRootDef);
//	}
//	return result;
//}
//
//AUTO_COMMAND;
//void FixMissionTakeItemCounts(void)
//{
//	int iNumFixed = 0;
//	int iNumSaved = 0;
//	RefDictIterator iterator = {0};
//	MissionDef *pDef = NULL;
//	MissionDef **changedDefCopies = NULL;
//
//	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);
//	
//	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
//	{
//		MissionDef *pDefCopy = StructClone(parse_MissionDef, pDef);
//		if (pDefCopy)
//		{
//			if (FixMissionTakeItemCountsRecursive(pDefCopy, pDefCopy))
//			{
//				eaPush(&changedDefCopies, pDefCopy);
//				iNumFixed++;
//			}
//		}
//		StructDestroy(parse_MissionDef, pDefCopy);
//	}
//	printf("Total Fixed: %d\n", iNumFixed);
//	
//	// Write out all affected files
//	//n = eaSize(&changedDefCopies);
//	//for (i = 0; i < n; i++)
//	//{
//	//	ItemDef *pItemDef = changedDefCopies[i];
//	//	if (pItemDef)
//	//	{
//	//		//if (ParserWriteTextFileFromSingleDictionaryStruct(pItemDef->pchFileName, g_hItemDict, pItemDef, 0, 0))
//	//		//	iNumSaved++;
//	//	}
//	//}
//	//eaDestroyStruct(&changedDefCopies, parse_ItemDef);
//	//printf("Total Saved: %d\n", iNumSaved);
//}


// --------------------------------------------------------------------
// Convert some GrantMission actions to GrantSubMission on Missions
// --------------------------------------------------------------------

//static bool FixMissionGrantActionsRecursive(MissionDef *pDef, bool bIsRoot)
//{
//	int i, n = eaSize(&pDef->ppOldOnStartActions);
//	bool changed = false;
//	for (i = 0; i < n; i++)
//	{
//		OldGameAction *pAction = pDef->ppOldOnStartActions[i];
//		if (pAction->type == OldGameActionType_GrantMission)
//		{
//			pAction->type = OldGameActionType_GrantSubMission;
//			changed = true;
//		}
//	}
//
//	if (!bIsRoot)
//	{
//		n = eaSize(&pDef->ppOldSuccessActions);
//		for (i = 0; i < n; i++)
//		{
//			OldGameAction *pAction = pDef->ppOldSuccessActions[i];
//			if (pAction->type == OldGameActionType_GrantMission)
//			{
//				pAction->type = OldGameActionType_GrantSubMission;
//				changed = true;
//			}
//		}
//
//		n = eaSize(&pDef->ppOldFailureActions);
//		for (i = 0; i < n; i++)
//		{
//			OldGameAction *pAction = pDef->ppOldFailureActions[i];
//			if (pAction->type == OldGameActionType_GrantMission)
//			{
//				pAction->type = OldGameActionType_GrantSubMission;
//				changed = true;
//			}
//		}
//	}
//
//	n = eaSize(&pDef->subMissions);
//	for (i = 0; i < n; i++)
//		changed |= FixMissionGrantActionsRecursive(pDef->subMissions[i], false);
//
//	return changed;
//}
//
//AUTO_COMMAND;
//void FixMissionGrantActions(void)
//{
//	int iNumSaved = 0;
//	int i, n;
//	RefDictIterator iterator = {0};
//	MissionDef *pDef = NULL;
//	MissionDef **changedDefCopies = NULL;
//
//	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);
//	
//	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
//	{
//		MissionDef *pDefCopy = StructClone(parse_MissionDef, pDef);
//		if (pDefCopy)
//		{
//			if (FixMissionGrantActionsRecursive(pDefCopy, true))
//				eaPush(&changedDefCopies, pDefCopy);
//			else
//				StructDestroy(parse_MissionDef, pDefCopy);
//		}
//	}
//	printf("Total Fixed: %d\n", eaSize(&changedDefCopies));
//	
//	// Write out all affected files
//	n = eaSize(&changedDefCopies);
//	for (i = 0; i < n; i++)
//	{
//		pDef = changedDefCopies[i];
//		if (pDef)
//		{
//			if (ParserWriteTextFileFromSingleDictionaryStruct(pDef->filename, g_MissionDictionary, pDef, 0, 0))
//				iNumSaved++;
//		}
//	}
//	eaDestroyStruct(&changedDefCopies, parse_MissionDef);
//	printf("Total Saved: %d\n", iNumSaved);
//}

// --------------------------------------------------------------------
// Changes EventCount() to MissionEventCount() on all MissionDefs
// --------------------------------------------------------------------

//static void FixMissionEventCounts_MakeUniqueEventName(GameEvent*** peaEvents, GameEvent *pEvent)
//{
//	char *pchDesiredName = strdup(pEvent->pchEventName);
//	char *tmp;
//	int count = 1;
//	int i;
//
//	// Remove counts from the end of the event name (so if we copy event_2 we don't get event_2_2)
//	tmp = pchDesiredName + strlen(pchDesiredName)-1;
//	while (tmp > pchDesiredName && (*tmp >= '0' && *tmp <= '9'))
//		--tmp;
//	if (tmp > pchDesiredName && *tmp == '_')
//		*tmp = '\0';
//
//	while (count < 100){
//		// Append a count to the name
//		char *buffer = NULL;
//		estrStackCreate(&buffer);
//		estrPrintf(&buffer, "%s_%d", pchDesiredName, count);
//		pEvent->pchEventName = allocAddString(buffer);
//		estrDestroy(&buffer);
//
//		// Look for a duplicate name
//		for (i = 0; i < eaSize(peaEvents); i++){
//			if ((*peaEvents)[i] != pEvent && (*peaEvents)[i]->pchEventName == pEvent->pchEventName)
//				break;
//		}
//		if (i == eaSize(peaEvents)){
//			// done, exit loop
//			break;
//		}
//		count++;
//	}
//	free(pchDesiredName);
//}
//
//static bool FixMissionEventCounts_ReplaceCB(char*** parsedArgs, char **estrReplacementString, MissionDef *pDef)
//{
//	int i;
//
//	if (eaSize(parsedArgs) > 1){
//		Errorf("Error: Event string parsed into the wrong number of args?  %s", (*parsedArgs)[0]);
//	} else if (eaSize(parsedArgs) == 0){
//		Errorf("Error: Event string parsed into zero args?");
//	} else {
//		const char *pchEventString = (*parsedArgs)[0];
//		GameEvent *pEvent = gameevent_EventFromString(pchEventString);
//		
//		if (pEvent)
//		{
//			// See if this Event already exists on the Mission somewhere
//			for (i = 0; i < eaSize(&pDef->eaTrackedEvents); i++){
//				pEvent->pchEventName = pDef->eaTrackedEvents[i]->pchEventName; // hack so that StructCompare ignores the name
//				if (!StructCompare(parse_GameEvent, pEvent, pDef->eaTrackedEvents[i], 0, 0, 0)){
//					StructDestroy(parse_GameEvent, pEvent);
//					pEvent = pDef->eaTrackedEvents[i];
//					break;
//				}
//			}
//
//			if (i == eaSize(&pDef->eaTrackedEvents)){
//				// Make a new unique name for the Event
//				pEvent->pchEventName = allocAddString(StaticDefineIntRevLookup(EventTypeEnum, pEvent->type));
//				FixMissionEventCounts_MakeUniqueEventName(&pDef->eaTrackedEvents, pEvent);
//				eaPush(&pDef->eaTrackedEvents, pEvent);
//			}
//
//			// Replace this expression function with MissionEventCount()
//			estrPrintf(estrReplacementString, "MissionEventCount(\"%s\")", pEvent->pchEventName);
//		}
//	}
//	return true;
//}
//
//static bool FixMissionEventCounts_FixMissionEditCondRecursive(MissionEditCond *pCond, MissionDef *pDef)
//{
//	int i;
//	bool bChanged = false;
//	if (pCond->type == MissionCondType_Expression){
//		char *estrBuffer = NULL;
//		estrStackCreate(&estrBuffer);
//		estrAppend2(&estrBuffer, pCond->valStr);
//		if (datafixup_RemoveExprFuncFromEStringWithCallback(&estrBuffer, "EventCount", pDef->filename, FixMissionEventCounts_ReplaceCB, pDef)){
//			StructFreeString(pCond->valStr);
//			pCond->valStr = StructAllocString(estrBuffer);
//			bChanged = true;
//		}		
//		estrDestroy(&estrBuffer);
//	}
//	for (i = 0; i < eaSize(&pCond->subConds); i++){
//		bChanged |= FixMissionEventCounts_FixMissionEditCondRecursive(pCond->subConds[i], pDef);
//	}
//	return bChanged;
//}
//
//static bool FixMissionEventCountsRecursive(MissionDef *pDef)
//{
//	int i;
//	bool bChanged = false;
//
//	if (pDef->pDiscoverCond){
//		bChanged |= datafixup_RemoveExprFuncWithCallback(&pDef->pDiscoverCond, "EventCount", pDef->filename, FixMissionEventCounts_ReplaceCB, pDef);
//	}
//	if (pDef->meSuccessCond){
//		bChanged |= FixMissionEventCounts_FixMissionEditCondRecursive(pDef->meSuccessCond, pDef);
//	}
//	if (pDef->meFailureCond){
//		bChanged |= FixMissionEventCounts_FixMissionEditCondRecursive(pDef->meFailureCond, pDef);
//	}
//
//	for (i = 0; i < eaSize(&pDef->subMissions); i++){
//		bChanged |= FixMissionEventCountsRecursive(pDef->subMissions[i]);
//	}
//
//	return bChanged;
//}
//
//AUTO_COMMAND;
//void FixMissionEventCounts(void)
//{
//	int i;
//	int iNumFixed = 0;
//	int iNumSaved = 0;
//	MissionDef **eaDefs = NULL;
//	MissionDef **eaChangedDefs = NULL;
//	MissionDef **eaOrigDefCopies = NULL;
//
//	AssetLoadMissionDefs(&eaDefs);
//	//AssetLoadMissionDefsEx(&eaDefs, "Defs/missions/_Test_BF/");
//
//	for (i = 0; i < eaSize(&eaDefs); i++)
//	{
//		MissionDef *pDef = eaDefs[i];
//		MissionDef *pDefCopy = StructClone(parse_MissionDef, pDef);
//		MissionDef *pOrigDefCopy = StructClone(parse_MissionDef, pDef);
//		if (pDefCopy && pOrigDefCopy)
//		{
//			if (FixMissionEventCountsRecursive(pDefCopy))
//			{
//				pDefCopy->version++;
//				eaPush(&eaChangedDefs, pDefCopy);
//				eaPush(&eaOrigDefCopies, pOrigDefCopy);
//				iNumFixed++;
//			}
//			else
//			{
//				StructDestroy(parse_MissionDef, pDefCopy);
//				StructDestroy(parse_MissionDef, pOrigDefCopy);
//			}
//		}
//	}
//	printf("Total Fixed: %d\n", iNumFixed);
//	
//	// Write out all affected files
//	iNumSaved = AssetSaveMissionDefs(&eaChangedDefs, &eaOrigDefCopies, false);
//	printf("Total Saved: %d\n", iNumSaved);
//}

// --------------------------------------------------------------------
// Sets the Mission Journal Category for every Mission based on the Objective Map field
// --------------------------------------------------------------------

AUTO_COMMAND;
void FixMissionCategories(void)
{
	int iNumSaved = 0;
	int i, n;
	RefDictIterator iterator = {0};
	MissionDef *pDef = NULL;
	MissionDef **changedDefCopies = NULL;

	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);
	
	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		MissionDef *pDefCopy = StructClone(parse_MissionDef, pDef);
		if (pDefCopy)
		{
			if (!REF_STRING_FROM_HANDLE(pDefCopy->hCategory) && pDefCopy->eaObjectiveMaps && eaSize(&pDefCopy->eaObjectiveMaps)){
				char parentMapName[5] = {0};
				strncpy(parentMapName, pDefCopy->eaObjectiveMaps[0]->pchMapName, 3);
				if (RefSystem_ReferentFromString(g_MissionCategoryDict, parentMapName)){
					SET_HANDLE_FROM_STRING(g_MissionCategoryDict, parentMapName, pDefCopy->hCategory);
					eaPush(&changedDefCopies, pDefCopy);
				}
			} else {
				StructDestroy(parse_MissionDef, pDefCopy);
			}
		}
	}
	printf("Total Fixed: %d\n", eaSize(&changedDefCopies));
	
	// Write out all affected files
	n = eaSize(&changedDefCopies);
	for (i = 0; i < n; i++)
	{
		pDef = changedDefCopies[i];
		if (pDef)
		{
			if (ParserWriteTextFileFromSingleDictionaryStruct(pDef->filename, g_MissionDictionary, pDef, 0, 0))
				iNumSaved++;
		}
	}
	eaDestroyStruct(&changedDefCopies, parse_MissionDef);
	printf("Total Saved: %d\n", iNumSaved);
}


// --------------------------------------------------------------------
// Marks any mission with Events as DoNotUncomplete
// --------------------------------------------------------------------

static bool mission_exprHasFunctionsRecurse(const MissionEditCond *mec, const char *funcName)
{
	if(mec)
	{
		int i;
		for(i = 0; i < eaSize(&mec->subConds); i++)
			if(mission_exprHasFunctionsRecurse(mec->subConds[i], funcName))
				return true;

		if(mec->expression)
		{
			int *eaiEventFuncs = NULL;

			exprFindFunctions(mec->expression, funcName, &eaiEventFuncs);
			if(eaiSize(&eaiEventFuncs) > 0)
				return true;
		}
	}

	return false;
}

static bool FixMissionUncompleteFlag_Recurse(MissionDef *pDef)
{
	bool bChanged = false;
	int i;

	if(!pDef->doNotUncomplete && mission_exprHasFunctionsRecurse(pDef->meSuccessCond, "MissionEventCount")) {
		pDef->doNotUncomplete = true;
		bChanged = true;
	}

	for (i = 0; i < eaSize(&pDef->subMissions); i++)
		bChanged |= FixMissionUncompleteFlag_Recurse(pDef->subMissions[i]);
	
	return bChanged;
}

// This reports any Missions that have events in them, but can also uncomplete
AUTO_COMMAND ACMD_SERVERCMD;
void FixMissionUncompleteFlag(void)
{
	RefDictIterator iterator = {0};
	MissionDef *pDef = NULL;
	MissionDef **changedDefCopies = NULL;
	int i, n;
	int iNumSaved = 0;

	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);

	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		MissionDef *pDefCopy = StructClone(parse_MissionDef, pDef);
		if (pDefCopy)
		{
			if (FixMissionUncompleteFlag_Recurse(pDefCopy)){
				eaPush(&changedDefCopies, pDefCopy);
			} else {
				StructDestroy(parse_MissionDef, pDefCopy);
			}
		}
	}
	printf("Total Fixed: %d\n", eaSize(&changedDefCopies));
	
	// Write out all affected files
	n = eaSize(&changedDefCopies);
	for (i = 0; i < n; i++)
	{
		pDef = changedDefCopies[i];
		if (pDef)
		{
			if (ParserWriteTextFileFromSingleDictionaryStruct(pDef->filename, g_MissionDictionary, pDef, 0, 0))
				iNumSaved++;
		}
	}
	eaDestroyStruct(&changedDefCopies, parse_MissionDef);
	printf("Total Saved: %d\n", iNumSaved);

}


// --------------------------------------------------------------------------------
// Contact fixup
// --------------------------------------------------------------------------------

AUTO_COMMAND;
void FixContactGreetingDialogs(void)
{
	int iNumFixed = 0;
	int iNumSaved = 0;
	RefDictIterator iterator = {0};
	ContactDef *pDef = NULL;
	ContactDef **changedDefCopies = NULL;
	int i, n;

	RefSystem_InitRefDictIterator(g_ContactDictionary, &iterator);

	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		ContactDef *pDefCopy = StructClone(parse_ContactDef, pDef);
		if (pDefCopy)
		{
			if(pDefCopy->greetingDialog)
			{
				pDefCopy->infoDialog = pDefCopy->greetingDialog;
				pDefCopy->greetingDialog = NULL;

				eaPush(&changedDefCopies, pDefCopy);
			}
			else
				StructDestroy(parse_ContactDef, pDefCopy);
		}
	}
	printf("Total Fixed: %d\n", iNumFixed);

	// Write out all affected files
	n = eaSize(&changedDefCopies);
	for (i = 0; i < n; i++)
	{
		ContactDef *pChangedDef = changedDefCopies[i];
		if (pChangedDef)
		{
			if (ParserWriteTextFileFromSingleDictionaryStruct(pChangedDef->filename, g_ContactDictionary, pChangedDef, 0, 0))
				iNumSaved++;
		}
	}
	eaDestroyStruct(&changedDefCopies, parse_ContactDef);
	printf("Total Saved: %d\n", iNumSaved);
}

//AUTO_COMMAND;
//void FixContactFlags(void)
//{
//	int iNumFixed = 0;
//	int iNumSaved = 0;
//	RefDictIterator iterator = {0};
//	ContactDef *pDef = NULL;
//	ContactDef **changedDefCopies = NULL;
//	int i, n;
//
//	RefSystem_InitRefDictIterator(g_ContactDictionary, &iterator);
//
//	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
//	{
//		ContactDef *pDefCopy = StructClone(parse_ContactDef, pDef);
//		if (pDefCopy)
//		{
//			if(pDefCopy->bIsBank || pDefCopy->bIsGuild || pDefCopy->bIsGuildBank || pDefCopy->bIsMailBox || 
//				pDefCopy->bIsMarket || pDefCopy->bIsMissionSearch || pDefCopy->bIsNemesis || pDefCopy->bIsPowersTrainer ||
//				pDefCopy->bIsRespec || pDefCopy->bIsStarshipChooser || pDefCopy->bIsStarshipTailor || pDefCopy->bIsTailor ||
//				pDefCopy->bShowInSearchResults)
//			{
//				if ( pDefCopy->bIsTailor )
//				{
//					pDefCopy->eContactFlags |= ContactFlag_Tailor;
//					pDefCopy->bIsTailor = false;
//				}
//				if ( pDefCopy->bIsStarshipTailor )
//				{
//					pDefCopy->eContactFlags |= ContactFlag_StarshipTailor;
//					pDefCopy->bIsStarshipTailor = false;
//				}
//				if ( pDefCopy->bIsStarshipChooser )
//				{
//					pDefCopy->eContactFlags |= ContactFlag_StarshipChooser;
//					pDefCopy->bIsStarshipChooser = false;
//				}
//				if ( pDefCopy->bIsNemesis )
//				{
//					pDefCopy->eContactFlags |= ContactFlag_Nemesis;
//					pDefCopy->bIsNemesis = false;
//				}
//				if ( pDefCopy->bIsGuild )
//				{
//					pDefCopy->eContactFlags |= ContactFlag_Guild;
//					pDefCopy->bIsGuild = false;
//				}
//				if ( pDefCopy->bIsRespec )
//				{
//					pDefCopy->eContactFlags |= ContactFlag_Respec;
//					pDefCopy->bIsRespec = false;
//				}
//				if ( pDefCopy->bIsPowersTrainer )
//				{
//					pDefCopy->eContactFlags |= ContactFlag_PowersTrainer;
//					pDefCopy->bIsPowersTrainer = false;
//				}
//				if ( pDefCopy->bIsBank )
//				{
//					pDefCopy->eContactFlags |= ContactFlag_Bank;
//					pDefCopy->bIsBank = false;
//				}
//				if ( pDefCopy->bIsGuildBank )
//				{
//					pDefCopy->eContactFlags |= ContactFlag_GuildBank;
//					pDefCopy->bIsGuildBank = false;
//				}
//				if ( pDefCopy->bIsMissionSearch )
//				{
//					pDefCopy->eContactFlags |= ContactFlag_MissionSearch;
//					pDefCopy->bIsMissionSearch = false;
//				}
//				if ( pDefCopy->bShowInSearchResults )
//				{
//					pDefCopy->eContactFlags |= ContactFlag_ShowInSearchResults;
//					pDefCopy->bShowInSearchResults = false;
//				}
//				if ( pDefCopy->bIsMarket )
//				{
//					pDefCopy->eContactFlags |= ContactFlag_Market;
//					pDefCopy->bIsMarket = false;
//				}
//				if ( pDefCopy->bIsMailBox )
//				{
//					pDefCopy->eContactFlags |= ContactFlag_MailBox;
//					pDefCopy->bIsMailBox = false;
//				}
//
//				eaPush(&changedDefCopies, pDefCopy);
//			}
//			else
//				StructDestroy(parse_ContactDef, pDefCopy);
//		}
//	}
//	printf("Total Fixed: %d\n", iNumFixed);
//
//	// Write out all affected files
//	n = eaSize(&changedDefCopies);
//	for (i = 0; i < n; i++)
//	{
//		ContactDef *pChangedDef = changedDefCopies[i];
//		if (pChangedDef)
//		{
//			if (ParserWriteTextFileFromSingleDictionaryStruct(pChangedDef->filename, g_ContactDictionary, pChangedDef, 0, 0))
//				iNumSaved++;
//		}
//	}
//	eaDestroyStruct(&changedDefCopies, parse_ContactDef);
//	printf("Total Saved: %d\n", iNumSaved);
//}

AUTO_COMMAND;
void FixContactInfoDialogs(void)
{
	int iNumFixed = 0;
	int iNumSaved = 0;
	RefDictIterator iterator = {0};
	ContactDef *pDef = NULL;
	ContactDef **changedDefCopies = NULL;
	int i, n;

	RefSystem_InitRefDictIterator(g_ContactDictionary, &iterator);

	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		ContactDef *pDefCopy = StructClone(parse_ContactDef, pDef);
		if (pDefCopy)
		{
			if(pDefCopy->defaultDialog && pDefCopy->defaultDialog[0])
			{
				langMakeEditorCopy(parse_ContactDef, pDefCopy, false);

				if(!pDefCopy->infoDialog || !pDefCopy->infoDialog[0])
				{
					eaPush(&pDefCopy->infoDialog, pDefCopy->defaultDialog[0]);
					eaRemoveFast(&pDefCopy->defaultDialog, 0);
				}
				else
				{
					char buf[1024];

					// Append the default dialog to the end of the info
					strcpy(buf, pDefCopy->infoDialog[0]->displayTextMesg.pEditorCopy->pcDefaultString);
					strcat(buf, "<br><br>");
					strcat(buf, pDefCopy->defaultDialog[0]->displayTextMesg.pEditorCopy->pcDefaultString);

					StructFreeString(pDefCopy->infoDialog[0]->displayTextMesg.pEditorCopy->pcDefaultString);
					pDefCopy->infoDialog[0]->displayTextMesg.pEditorCopy->pcDefaultString = StructAllocString(buf);
				}

				eaDestroyStruct(&pDefCopy->defaultDialog, parse_DialogBlock);
				pDefCopy->defaultDialog = NULL;

				langApplyEditorCopy(parse_ContactDef, pDefCopy, pDef, true, false);
				eaPush(&changedDefCopies, pDefCopy);
			}
			else
				StructDestroy(parse_ContactDef, pDefCopy);
		}
	}
	printf("Total Fixed: %d\n", iNumFixed);

	// Write out all affected files
	n = eaSize(&changedDefCopies);
	for (i = 0; i < n; i++)
	{
		ContactDef *pChangedDef = changedDefCopies[i];
		if (pChangedDef)
		{
			if (ParserWriteTextFileFromSingleDictionaryStruct(pChangedDef->filename, g_ContactDictionary, pChangedDef, 0, 0))
				iNumSaved++;
		}
	}
	eaDestroyStruct(&changedDefCopies, parse_ContactDef);
	printf("Total Saved: %d\n", iNumSaved);
}

AUTO_COMMAND;
void FixLoreContacts(void)
{
	int iNumFixed = 0;
	int iNumSaved = 0;
	RefDictIterator iterator = {0};
	ContactDef *pDef = NULL;
	ContactDef **eaChangedDefCopies = NULL;
	ItemDef **eaNewItemDefs = NULL;
	int i, n, iUniqueItem;
	char buf[1024];
	char *tmpS = NULL;
	
	estrStackCreate(&tmpS);

	RefSystem_InitRefDictIterator(g_ContactDictionary, &iterator);

	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		// Find all Contacts in the "Lore" scope
		if (pDef && pDef->scope && strstri(pDef->scope, "lore"))
		{
			if (contact_IsSingleScreen(pDef) && eaSize(&pDef->specialDialog) == 1){
				ContactDef *pDefCopy = StructClone(parse_ContactDef, pDef);
				if (pDefCopy)
				{
					ItemDef *pItemDef = StructCreate(parse_ItemDef);
					ContactLoreDialog *pLoreDialog = NULL;
					
					langMakeEditorCopy(parse_ContactDef, pDefCopy, false);

					// Generate a unique name for the ItemDef
					iUniqueItem = 1;
					sprintf(buf, "Lore_%s", pDef->name);
					while (RefSystem_ReferentFromString(g_hItemDict, buf)){
						iUniqueItem++;
						sprintf(buf, "Lore_%s_%d", pDef->name, iUniqueItem);
					}
					
					// Set up basic Item data
					pItemDef->eType = kItemType_Lore;
					pItemDef->pchScope = allocAddString("Lore");
					pItemDef->pchName = allocAddString(buf);
					resFixPooledFilename(&pItemDef->pchFileName, "defs/items", pItemDef->pchScope, pItemDef->pchName, "item");

					// Set up text on the ItemDef
					pItemDef->displayNameMsg.pEditorCopy = StructClone(parse_Message, pDefCopy->specialDialog[0]->displayNameMesg.pEditorCopy);
					if (pItemDef->displayNameMsg.pEditorCopy){
						estrPrintf(&tmpS, "ItemDef.%s", pItemDef->pchName);
						pItemDef->displayNameMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
						
						StructFreeString(pItemDef->displayNameMsg.pEditorCopy->pcDescription);
						pItemDef->displayNameMsg.pEditorCopy->pcDescription = StructAllocString("Display name for an item definition");
						
						pItemDef->displayNameMsg.pEditorCopy->pcScope = allocAddString("ItemDef");
					}


					pItemDef->descriptionMsg.pEditorCopy = StructClone(parse_Message, pDefCopy->specialDialog[0]->dialogBlock[0]->displayTextMesg.pEditorCopy);
					if (pItemDef->displayNameMsg.pEditorCopy){
						estrPrintf(&tmpS, "ItemDef.%s.Description", pItemDef->pchName);
						pItemDef->descriptionMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
				
						StructFreeString(pItemDef->descriptionMsg.pEditorCopy->pcDescription);
						pItemDef->descriptionMsg.pEditorCopy->pcDescription = StructAllocString("Description for an item definition");

						pItemDef->displayNameMsg.pEditorCopy->pcScope = allocAddString("ItemDef");
					}

					// Remove special dialog from the Contact
					StructDestroy(parse_SpecialDialogBlock, eaRemove(&pDefCopy->specialDialog, 0));

					// Add a new Lore reference to the Contact pointing at this Item
					pLoreDialog = StructCreate(parse_ContactLoreDialog);
					SET_HANDLE_FROM_STRING(g_hItemDict, pItemDef->pchName, pLoreDialog->hLoreItemDef);
					eaPush(&pDefCopy->eaLoreDialogs, pLoreDialog);

					// The Contact should be a List Mode contact, not a Single Dialog
					pDefCopy->type = ContactType_List;

					eaPush(&eaChangedDefCopies, pDefCopy);
					eaPush(&eaNewItemDefs, pItemDef);
					iNumFixed++;
				}
			} else {
				// Error
				printf("Couldn't fix %s\n", pDef->name);
			}
		}
	}
	printf("Total Fixed: %d\n", iNumFixed);

	// Write out all new ItemDefs
	n = eaSize(&eaNewItemDefs);
	for (i = 0; i < n; i++)
	{
		ItemDef *pItemDef = eaNewItemDefs[i];
		if (pItemDef)
		{
			langApplyEditorCopy(parse_ItemDef, pItemDef, NULL, true, false);
			if (ParserWriteTextFileFromSingleDictionaryStruct(pItemDef->pchFileName, g_hItemDict, pItemDef, 0, 0)){
				iNumSaved++;
			}
		}
	}
	printf("Total Items Saved: %d\n", iNumSaved);

	// Write out all affected ContactDef files
	iNumSaved = 0;
	n = eaSize(&eaChangedDefCopies);
	for (i = 0; i < n; i++)
	{
		ContactDef *pChangedDef = eaChangedDefCopies[i];
		if (pChangedDef)
		{
			langApplyEditorCopy(parse_ContactDef, pChangedDef, pDef, true, false);
			if (ParserWriteTextFileFromSingleDictionaryStruct(pChangedDef->filename, g_ContactDictionary, pChangedDef, 0, 0)){
				iNumSaved++;
			}
		}
	}
	eaDestroyStruct(&eaChangedDefCopies, parse_ContactDef);
	eaDestroyStruct(&eaNewItemDefs, parse_ItemDef);
	estrDestroy(&tmpS);
	printf("Total Contacts Saved: %d\n", iNumSaved);
}

static int fixup_SpecialDialogActionEq(const SpecialDialogAction* a, const SpecialDialogAction* b)
{
	if(!a || !b)
		return 0;

	return (GET_REF(a->contactDef) && GET_REF(b->contactDef) && REF_COMPARE_HANDLES(a->contactDef, b->contactDef)
			&& a->dialogName && b->dialogName && !stricmp(a->dialogName, b->dialogName));
}

// Adds Force on Team flag to all special dialog blocks which are not pointed to by another special dialog block
AUTO_COMMAND;
void FixContactForceOnTeamDialogs(void)
{
	int iNumDialogFixed = 0;
	int iNumDefFixed = 0;
	int iNumSaved = 0;
	RefDictIterator iterator = {0};
	ContactDef *pDef = NULL;
	ContactDef **changedDefCopies = NULL;
	SpecialDialogAction **eaTargetDialogs = NULL;
	WorldContactActionProperties **eaGameActionTargetDialogs = NULL;
	int i, j, n, k;

	// Collect the lists of special dialogs and actions that target other dialogs
	RefSystem_InitRefDictIterator(g_ContactDictionary, &iterator);
	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator)) 
	{
		for(i = (eaSize(&pDef->specialDialog)-1); i >= 0; i--)
		{
			for(j = eaSize(&pDef->specialDialog[i]->dialogActions)-1; j>=0; j--) 
			{
				SpecialDialogAction* pAction = pDef->specialDialog[i]->dialogActions[j];

				if (GET_REF(pAction->contactDef) && 
					EMPTY_TO_NULL(pAction->dialogName) && 
					(!eaTargetDialogs || eaFindCmp(&eaTargetDialogs, pAction, fixup_SpecialDialogActionEq)==-1) ) 
				{
					eaPush(&eaTargetDialogs, pAction);
				} 
				else 
				{
					for(k = eaSize(&pAction->actionBlock.eaActions)-1; k>=0; k--) 
					{
						WorldGameActionProperties *pGameAction = pAction->actionBlock.eaActions[k];

						if (pGameAction && 
							pGameAction->eActionType == WorldGameActionType_Contact && 
							pGameAction->pContactProperties 
							&& GET_REF(pGameAction->pContactProperties->hContactDef) && 
							EMPTY_TO_NULL(pGameAction->pContactProperties->pcDialogName))
						{
							eaPush(&eaGameActionTargetDialogs, pGameAction->pContactProperties);
						}
					}
				}
			}
		}
	}

	// Change the required dialogs
	RefSystem_InitRefDictIterator(g_ContactDictionary, &iterator);
	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		ContactDef *pDefCopy = StructClone(parse_ContactDef, pDef);
		if (pDefCopy) 
		{
			bool bChanged = false;
			for(i = (eaSize(&pDefCopy->specialDialog)-1); i >= 0; i--)
			{
				bool bFound = false;

				// Search to see if the dialog is pointed to by other contactDefs
				for(j = eaSize(&eaTargetDialogs)-1; j>=0 && !bFound; j--) 
				{
					if (GET_REF(eaTargetDialogs[j]->contactDef) == pDef && 
						!stricmp(eaTargetDialogs[j]->dialogName, pDefCopy->specialDialog[i]->name))
					{
						bFound = true;
					}
				}

				// Search for game actions which point to this contact dialog
				for(j = eaSize(&eaGameActionTargetDialogs)-1; j >= 0 && !bFound; j--) 
				{
					if (GET_REF(eaGameActionTargetDialogs[j]->hContactDef) == pDef && 
						!stricmp(eaGameActionTargetDialogs[j]->pcDialogName, pDefCopy->specialDialog[i]->name))
					{
						bFound = true;
					}
				}

				// Search for special dialogs within the current contact def which point to this dialog
				for(j = eaSize(&pDefCopy->specialDialog)-1; j>=0 && !bFound; j--) {
					SpecialDialogBlock *pBlockToCheck = pDefCopy->specialDialog[j];

					for(k = eaSize(&pBlockToCheck->dialogActions)-1; k>=0 && !bFound; k--)
					{
						SpecialDialogAction *pActionToCheck = pBlockToCheck->dialogActions[k];
						if (!GET_REF(pActionToCheck->contactDef) && 
							!stricmp(pActionToCheck->dialogName, pDefCopy->specialDialog[i]->name))
						{
							bFound = true;
						}
					}
				}

				// If none are found, add the forceOnTeam flag
				if (!bFound) 
				{
					pDefCopy->specialDialog[i]->eFlags = (pDefCopy->specialDialog[i]->eFlags | SpecialDialogFlags_ForceOnTeam);
					bChanged = true;
					iNumDialogFixed++;
				}
			}

			if (bChanged) 
			{
				eaPush(&changedDefCopies, pDefCopy);
				iNumDefFixed++;
			} 
			else 
			{
				StructDestroy(parse_ContactDef, pDefCopy);
			}
		}
	}
	printf("Total Special Dialogs Fixed: %d\n", iNumDialogFixed);
	printf("Total Defs Fixed: %d\n", iNumDefFixed);

	// Write out all affected files
	n = eaSize(&changedDefCopies);
	for (i = 0; i < n; i++)
	{
		ContactDef *pChangedDef = changedDefCopies[i];
		if (pChangedDef)
		{
			if (ParserWriteTextFileFromSingleDictionaryStruct(pChangedDef->filename, g_ContactDictionary, pChangedDef, 0, 0))
			{
				iNumSaved++;
			}
		}
	}
	eaDestroyStruct(&changedDefCopies, parse_ContactDef);
	printf("Total Saved: %d\n", iNumSaved);
}

// Clears bSendComplete flag on special dialogs with display name "Cancel" or "Not Now"
AUTO_COMMAND;
void FixContactSpecialDialogComplete(void)
{
	int iNumActionsFixed = 0;
	int iNumDefFixed = 0;
	int iNumSaved = 0;
	RefDictIterator iterator = {0};
	ContactDef *pDef = NULL;
	ContactDef **changedDefCopies = NULL;
	int i, j;

	// Change the required dialogs
	RefSystem_InitRefDictIterator(g_ContactDictionary, &iterator);
	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		ContactDef *pDefCopy = StructClone(parse_ContactDef, pDef);
		if (pDefCopy) 
		{
			bool bChanged = false;
			for(i = (eaSize(&pDefCopy->specialDialog)-1); i >= 0; i--)
			{
				SpecialDialogBlock *pBlockToCheck = pDefCopy->specialDialog[i];

				for(j = eaSize(&pBlockToCheck->dialogActions)-1; j>=0; j--)
				{
					SpecialDialogAction *pActionToCheck = pBlockToCheck->dialogActions[j];
					Message *pMsg = GET_REF(pActionToCheck->displayNameMesg.hMessage);
					if (pMsg && pMsg->pcDefaultString && pActionToCheck->bSendComplete &&
						((stricmp(pMsg->pcDefaultString, "Cancel") == 0) || (stricmp(pMsg->pcDefaultString, "Not Now") == 0)))
					{
						pActionToCheck->bSendComplete = false;
						bChanged = true;
						iNumActionsFixed++;
					}
				}
			}

			if (bChanged) 
			{
				eaPush(&changedDefCopies, pDefCopy);
				iNumDefFixed++;
			} 
			else 
			{
				StructDestroy(parse_ContactDef, pDefCopy);
			}
		}
	}
	printf("Total Actions Fixed: %d\n", iNumActionsFixed);
	printf("Total Defs Fixed: %d\n", iNumDefFixed);

	// Write out all affected files
	for (i = 0; i < eaSize(&changedDefCopies); i++)
	{
		ContactDef *pChangedDef = changedDefCopies[i];
		if (pChangedDef)
		{
			printf("%s\n", pChangedDef->filename);
			//if (ParserWriteTextFileFromSingleDictionaryStruct(pChangedDef->filename, g_ContactDictionary, pChangedDef, 0, 0))
			//{
			//	iNumSaved++;
			//}
		}
	}
	eaDestroyStruct(&changedDefCopies, parse_ContactDef);
	printf("Total Saved: %d\n", iNumSaved);
}

// -----------------------------------------------------------------------
//  Fixup code to convert EventCountSinceSpawn to EventCountSinceComplete
//  on Encounter Spawn conditions
// -----------------------------------------------------------------------

static bool ConvertEventCountSinceSpawn(OldStaticEncounter *pStaticEnc, EncounterLayer *pLayer, const char *pcPathString, FILE *file)
{
	if (pStaticEnc->defOverride && pStaticEnc->defOverride->spawnCond)
	{
		char *origExpr = strdup(exprGetCompleteString(pStaticEnc->defOverride->spawnCond));
		if (strstri(origExpr, "EventCountSinceSpawn")){
			char *estrNewExpr = estrStackCreateFromStr(origExpr);
			estrReplaceOccurrences_CaseInsensitive(&estrNewExpr, "EventCountSinceSpawn", "EventCountSinceComplete");
			exprSetOrigStrNoFilename(pStaticEnc->defOverride->spawnCond, estrNewExpr);
			fprintf(file, "Object %s %s:\n\tOrig: %s\n\tNew:  %s\n\n", pLayer->name?pLayer->name:pLayer->pchFilename, pStaticEnc->name, origExpr, estrNewExpr);
			estrDestroy(&estrNewExpr);
			free(origExpr);
			return true;
		} else {
			free(origExpr);
			return false;
		}
	}
	return false;
}

// This changes EventCountSinceSpawn to EventCountSinceComplete on Encounter Spawn conditions
AUTO_COMMAND ACMD_SERVERCMD;
void FixupEventCountSinceSpawn(void)
{
	EncounterLayer **eaLayers = NULL;
	EncounterLayer **eaChangedLayers = NULL;
	EncounterLayer **eaChangedLayerCopies = NULL;
	FILE *file;
	int i, n;

	file = fopen("c:\\EventCountSinceSpawnResults.txt", "w");

	AssetLoadEncLayers(&eaLayers);
	AssetPutEncLayersInEditMode(&eaLayers);

	n = eaSize(&eaLayers);
	for (i = 0; i < n; i++)
	{
		EncounterLayer *pLayer = eaLayers[i];
		
		// Change GrantItem expression to "GiveItem" Action
		if (ParserScanForSubstruct(parse_EncounterLayer, pLayer, parse_OldStaticEncounter, 0, TOK_NO_TEXT_SAVE | TOK_NO_WRITE, ConvertEventCountSinceSpawn, file))
		{
			if (eaFind(&eaChangedLayers, pLayer) == -1)
			{
				eaPush(&eaChangedLayers, pLayer);
				eaPush(&eaChangedLayerCopies, oldencounter_SafeCloneLayer(pLayer));
			}
		}
	}

	AssetSaveEncLayers(&eaChangedLayers, &eaChangedLayerCopies, false);

	AssetCleanupEncLayers(&eaLayers);
	eaDestroy(&eaChangedLayers);
	eaDestroyStruct(&eaChangedLayerCopies, parse_EncounterLayer);
	fclose(file);
}

// ---------------------------------------------------------------------
// Encounter Layer to Geo Layer Conversion Logic
// ---------------------------------------------------------------------

extern ParseTable parse_ZoneMapInfo[];
#define TYPE_parse_ZoneMapInfo ZoneMapInfo

static ZoneMapInfo *Convert_LoadZone(char *pcFilename)
{
	DictionaryEArrayStruct *pZones = resDictGetEArrayStruct("ZoneMap");
	int i;

	for(i=eaSize(&pZones->ppReferents)-1; i>=0; --i) {
		ZoneMapInfo *pZone = pZones->ppReferents[i];
		if (stricmp(pcFilename, pZone->filename) == 0) {
			return StructClone(parse_ZoneMapInfo, pZone);
		}
	}
	return NULL;
}


static char *gZoneFile = NULL;

static FileScanAction Convert_FindZoneFile(char *dir, struct _finddata32_t* data, void *pUserData)
{
	if(data->attrib & _A_SUBDIR)
	{
		return FSA_EXPLORE_DIRECTORY;
	}
	else
	{
		if(strEndsWith(data->name, ".zone"))
		{
			char buf[1024];
			sprintf(buf, "%s/%s", dir, data->name);
			gZoneFile = (char*)allocAddString(buf);
		}
	}

	return FSA_EXPLORE_DIRECTORY;
}


static char *Convert_FindZoneFilename(const char* dirName)
{
	// Clear any previous file list
	gZoneFile = NULL;

	// Find all the layer files
	fileScanAllDataDirs(dirName, Convert_FindZoneFile, NULL);
	return gZoneFile;
}

static ZoneMapInfo *Convert_GetZoneForLayer(LibFileLoad *pGeoLayer)
{
	static ZoneMapInfo **eaZones = NULL;
	char buf[1024];
	char *c;
	char *pcFilename;
	ZoneMapInfo *pZone;

	// Find dir name
	strcpy(buf, pGeoLayer->defs[0]->filename);
	c = strrchr(buf, '/');
	assert(c);
	*c = '\0';
	++c;
	
	// Load the zone
	pcFilename = Convert_FindZoneFilename(buf);
	assert(pcFilename);
	pZone = Convert_LoadZone(pcFilename);

	return pZone;
}

static bool Convert_LoadRegionBounds(ZoneMapInfo *pZone, ZoneMapRegionBounds *pBounds)
{
	char boundsFileName[1024];

	sprintf(boundsFileName, "bin/geobin/%s/world_cells.region_bounds", pZone->filename);
	if (ParserReadTextFile(boundsFileName, parse_ZoneMapRegionBounds, pBounds, 0)) {
		return true;
	} else {
		return false;
	}
}

static void Convert_SaveChangedZone(ZoneMapInfo *pZone)
{
	ZoneMapInfo *pOldZone;

	// Check out the zone file
	fixupCheckoutFile(pZone->filename, true);

	// Save the zone back to disk
	pOldZone = RefSystem_ReferentFromString("ZoneMap", pZone->map_name);
	if (pOldZone) {
		RefSystem_MoveReferent(StructClone(parse_ZoneMapInfo, pZone), pOldZone);
	} else {
		assertmsg(0, "Zone file missing");
	}
	ParserWriteTextFileFromDictionary(pZone->filename, "ZoneMap", 0, 0);
}

static void Convert_AddGeoLayerToZone(LibFileLoad *pGeoLayer, Vec3 vSamplePos, char ***peaExtraFiles, ZoneMapInfo **ppZone)
{
	char buf[1024];
	char *c;
	char *pcFilename;
	ZoneMapInfo *pZone;
	ZoneMapLayerInfo *pZoneLayer;
	ZoneMapRegionBounds *pBounds;
	const char *pcRegionName = NULL;
	int i;

	// Find dir name
	strcpy(buf, pGeoLayer->defs[0]->filename);
	c = strrchr(buf, '/');
	assert(c);
	*c = '\0';
	++c;
	
	// Load the zone
	pcFilename = Convert_FindZoneFilename(buf);
	assert(pcFilename);
	pZone = Convert_LoadZone(pcFilename);
	if (ppZone) {
		*ppZone = pZone;
	}

	// See if layer is already present
	for(i=eaSize(&pZone->layers)-1; i>=0; --i) {
		if ((stricmp(pZone->layers[i]->filename, pGeoLayer->defs[0]->filename) == 0) ||
			(stricmp(pZone->layers[i]->filename, c) == 0)) {
			// Layer is already present
			return;
		}
	}

	// See if a region is required
	pBounds = StructCreate(parse_ZoneMapRegionBounds);
	if (Convert_LoadRegionBounds(pZone, pBounds)) {
		for(i=eaSize(&pBounds->regions)-1; i>=0; --i) {
			WorldRegionBounds *pRegion = pBounds->regions[i];
			if (pRegion->region_name && pointBoxCollision(vSamplePos, pRegion->world_min, pRegion->world_max)) {
				pcRegionName = pRegion->region_name;
			}
		}
	}
	StructDestroy(parse_ZoneMapRegionBounds, pBounds);

	// Layer not present so add it
	pZoneLayer = StructCreate(parse_ZoneMapLayerInfo);
	pZoneLayer->filename = allocAddFilename(c);
	pZoneLayer->region_name = pcRegionName;
	eaPush(&pZone->layers, pZoneLayer);

	// Save changes
	Convert_SaveChangedZone(pZone);

	eaPushUnique(peaExtraFiles, pcFilename);
	StructDestroy(parse_ZoneMapInfo, pZone);
}


static LibFileLoad *Convert_CreateGeoLayer(char *pcFilename)
{
	LibFileLoad *pGeoLayer;
	GroupDef *pDefLoad;

	pDefLoad = StructCreate(parse_GroupDef);
	pDefLoad->name_str = allocAddString("Geometry");
	pDefLoad->name_uid = 1;
	pDefLoad->filename = (char*)allocAddFilename(pcFilename);

	pGeoLayer = StructCreate(parse_LibFileLoad);
	pGeoLayer->version = 1;
	eaPush(&pGeoLayer->defs, pDefLoad);
	eaIndexedDisable(&pGeoLayer->defs);

	return pGeoLayer;
}

static bool Convert_GetSamplePositionForEncGroup(OldStaticEncounterGroup *pGroup, Vec3 vPos)
{
	int i;
	if (eaSize(&pGroup->staticEncList)) {
		copyVec3(pGroup->staticEncList[0]->encPos, vPos);
		return true;
	}
	for(i=eaSize(&pGroup->childList)-1; i>=0; --i) {
		if (Convert_GetSamplePositionForEncGroup(pGroup->childList[i], vPos)) {
			return true;
		}
	}
	return false;
}

static LibFileLoad *Convert_GetGeoLayerForEncLayer(EncounterLayer *pEncLayer, Vec3 vSamplePos, LibFileLoad ***peaGeoLayers, char ***peaExtraFiles, ZoneMapInfo **ppZone)
{
	LibFileLoad *pGeoLayer;
	char buf[1024];
	char *pos;
	int i;

	// Determine the geo layer filename
	strcpy(buf, pEncLayer->pchFilename);
	pos = strrchr(buf, '.');
	assert(pos);
	*pos = '\0';
	strcat(buf, "_Encounters.layer");

	// Search for it
	for(i=eaSize(peaGeoLayers)-1; i>=0; --i) {
		pGeoLayer = (*peaGeoLayers)[i];
		if (eaSize(&pGeoLayer->defs) && stricmp(buf, pGeoLayer->defs[0]->filename) == 0) {
			if (ppZone) {
				*ppZone = Convert_GetZoneForLayer(pGeoLayer);
			}
			return pGeoLayer;
		}
	}

	// Not found so create a new layer
	pGeoLayer = Convert_CreateGeoLayer(buf);
	eaPush(peaGeoLayers, pGeoLayer);

	// Add the geo layer to the zone
	Convert_AddGeoLayerToZone(pGeoLayer, vSamplePos, peaExtraFiles, ppZone);

	return pGeoLayer;
}


static int Convert_GetNextIdForGeoLayer(LibFileLoad *pGeoLayer)
{
	GroupDef *pDefLoad;
	GroupChild *pGroupLoad;
	int next_id = 100;
	int i,j;

	for(i=eaSize(&pGeoLayer->defs)-1; i>=0; --i) {
		pDefLoad = pGeoLayer->defs[i];
		if (pDefLoad->name_uid >= next_id) {
			next_id = pDefLoad->name_uid + 2;
		}

		for(j=eaSize(&pDefLoad->children)-1; j>=0; --j) {
			pGroupLoad = pDefLoad->children[j];
			if (pGroupLoad->name_uid >= next_id) {
				next_id = pGroupLoad->name_uid+2;
			}
			if ((int)pGroupLoad->uid_in_parent >= next_id) {
				next_id = (int)pGroupLoad->uid_in_parent+2;
			}
		}
	}

	return next_id;
}


static void ScanForPatrolRoutes(EncounterLayer ***peaEncLayers, FILE *file)
{
	int i;
	int iNumLayers=0, iNumRoutes=0;

	for(i=eaSize(peaEncLayers)-1; i>=0; --i) {
		EncounterLayer *pLayer = (*peaEncLayers)[i];
		if (eaSize(&pLayer->oldNamedRoutes)) {
			fprintf(file, "PatrolRoutes (%d) %s\n", eaSize(&pLayer->oldNamedRoutes), pLayer->pchFilename);
			++iNumLayers;
			iNumRoutes += eaSize(&pLayer->oldNamedRoutes);
		}
	}

	fprintf(file, "\n%d layers with %d named routes\n\n", iNumLayers, iNumRoutes);
}


static void ScanForDuplicateScopeNames(LibFileLoad **eaGeoLayers, FILE *file)
{
	LibFileLoad **eaLayers = NULL;
	char **eaNames = NULL;
	char buf[1024];
	char *ptr;
	int i, j, k, n;
	bool bFoundProblem;

	eaPushEArray(&eaLayers, &eaGeoLayers);

	for(i=eaSize(&eaLayers)-1; i>=0; --i) {
		LibFileLoad *pGeoLayer = eaLayers[i];

		if (!eaSize(&pGeoLayer->defs)) {
			continue;
		}

		bFoundProblem = false;

		// Get the filename prefix
		strcpy(buf, pGeoLayer->defs[0]->filename);
		ptr = strrchr(buf, '/');
		if (ptr != NULL) {
			*(ptr+1) = '\0';
		}

		// Push on scope names
		for(k=eaSize(&pGeoLayer->defs[0]->scope_entries_load)-1; k>=0; --k) {
			char *pcName = pGeoLayer->defs[0]->scope_entries_load[k]->name;
			for(n=eaSize(&eaNames)-1; n>=0; --n) {
				if (stricmp(eaNames[n], pcName) == 0) {
					if (!bFoundProblem) {
						bFoundProblem = true;
						fprintf(file, "\nD-Layer: %s\n", pGeoLayer->defs[0]->filename);
					}
					fprintf(file, "  Duplicate name '%s' (%s)\n", eaNames[n], pGeoLayer->defs[0]->filename);
					break;
				}
			}
			if (n < 0) {
				eaPush(&eaNames, pcName);
			}
		}

		for(j=i-1; j>=0; --j) {
			LibFileLoad *pOtherLayer = eaLayers[j];
			if (!eaSize(&pOtherLayer->defs)) {
				continue;
			}

			if (strnicmp(buf, pOtherLayer->defs[0]->filename, strlen(buf)) == 0) {
				eaRemove(&eaLayers, j);
				--i;

				// Push on scope names
				for(k=eaSize(&pOtherLayer->defs[0]->scope_entries_load)-1; k>=0; --k) {
					char *pcName = pOtherLayer->defs[0]->scope_entries_load[k]->name;
					for(n=eaSize(&eaNames)-1; n>=0; --n) {
						if (stricmp(eaNames[n], pcName) == 0) {
							if (!bFoundProblem) {
								bFoundProblem = true;
								fprintf(file, "\nD-Layer: %s\n", pGeoLayer->defs[0]->filename);
							}
							fprintf(file, "  Duplicate name '%s' (%s)\n", eaNames[n], pOtherLayer->defs[0]->filename);
							break;
						}
					}
					if (n < 0) {
						eaPush(&eaNames, pcName);
					}
				}
			}
		}

		eaDestroy(&eaNames);
	}
	fprintf(file, "\n\n");
}


static Message *Convert_SaveFileMessage(const char *pcFilename, const char *pcBaseName, const char *pcFinalName, Message *pMessage, char ***peaExtraFiles)
{
	Message *pSaveMessage;
	char buf[1024];
	char filecopy[1024];
	char *c;

	if (!pMessage) {
		return NULL;
	}

	// Setup Message
	pSaveMessage = StructClone(parse_Message, pMessage);
	assert(pSaveMessage);

	// Set filename
	strcpy(buf, pcFilename);
	strcat(buf, ".ms");
	pSaveMessage->pcFilename = (char*)allocAddFilename(buf);

	// Setup key
	sprintf(buf, "%s.", pcBaseName);
	strcpy(filecopy, pcFilename);
	c = strrchr(filecopy,'.');
	if (c)
		*c = '\0';
	c = strrchr(filecopy, '/');
	if (c)
		strcat(buf, c + 1);
	else
		strcat(buf, filecopy);
	strcat(buf, ".");
	strcat(buf, pcFinalName);
	pSaveMessage->pcMessageKey = allocAddString(buf);

	// Add to dictionary
	RefSystem_AddReferent("Message", pSaveMessage->pcMessageKey, pSaveMessage);

	// Save to disk
	ParserWriteTextFileFromDictionary(pSaveMessage->pcFilename, "Message", 0, 0);
	eaPushUnique(peaExtraFiles, (char*)pSaveMessage->pcFilename);

	return pSaveMessage;
}


#ifdef SDANGELO_CODE_TO_REMOVE_WHEN_DONE_WITH_ENCOUNTER_LAYER_CONVERSION

static void Convert_CompareVolumes(FILE *file, ELEVolume *pVolume1, ELEVolume *pVolume2)
{
	if (pVolume1->volType != pVolume2->volType) {
		fprintf(file, "  ** Warning: Volume types are not the same\n");
	}
	switch(pVolume1->volType) {
		xcase VolumeTypeEvent:
			// No specific data to compare

		xcase VolumeTypeNeighborhood:
			if (pVolume1->neighborhood && pVolume2->neighborhood && stricmp(pVolume1->neighborhood, pVolume2->neighborhood) != 0) {
				fprintf(file, "  ** Warning: Volume neighborhoods are not the same\n");
			} else if (!pVolume1->neighborhood || !pVolume2->neighborhood) {
				fprintf(file, "  ** Warning: Volume neighborhood is missing\n");
			}

		xcase VolumeTypeNemesisRace:
			// Never used so no conversion

		xcase VolumeTypeLandmark:
			if (pVolume1->landmarkData.pchIconName && pVolume2->landmarkData.pchIconName && stricmp(pVolume1->landmarkData.pchIconName, pVolume2->landmarkData.pchIconName) != 0) {
				fprintf(file, "  ** Warning: Volume icon names are not the same\n");
			} else if ((pVolume1->landmarkData.pchIconName && !pVolume2->landmarkData.pchIconName) ||
					   (!pVolume1->landmarkData.pchIconName && pVolume2->landmarkData.pchIconName)) {
				fprintf(file, "  ** Warning: Volume icon names are not the same\n");
			}
			if (GET_REF(pVolume1->landmarkData.displayNameMsg.hMessage) != GET_REF(pVolume2->landmarkData.displayNameMsg.hMessage)) {
				fprintf(file, "  ** Warning: Volume display names are not the same\n");
			}

		xcase VolumeTypePower:
			if (GET_REF(pVolume1->powerData.power) != GET_REF(pVolume2->powerData.power)) {
				fprintf(file, "  ** Warning: Volume powers are not the same\n");
			}
			if (pVolume1->powerData.strength != pVolume2->powerData.strength) {
				fprintf(file, "  ** Warning: Volume strengths are not the same\n");
			}
			if (pVolume1->powerData.repeatTime != pVolume2->powerData.repeatTime) {
				fprintf(file, "  ** Warning: Volume repeat times are not the same\n");
			}
			if (pVolume1->powerData.triggerCond && pVolume2->powerData.triggerCond && stricmp(pVolume1->powerData.triggerCond, pVolume2->powerData.triggerCond) != 0) {
				fprintf(file, "  ** Warning: Volume trigger conds are not the same\n");
			} else if ((pVolume1->powerData.triggerCond && !pVolume2->powerData.triggerCond) ||
					   (!pVolume1->powerData.triggerCond && pVolume2->powerData.triggerCond)) {
				fprintf(file, "  ** Warning: Volume trigger conds are not the same\n");
			}

		xcase VolumeTypeWarp:
			if (pVolume1->warpData.mapName && pVolume2->warpData.mapName && stricmp(pVolume1->warpData.mapName, pVolume2->warpData.mapName) != 0) {
				fprintf(file, "  ** Warning: Volume map names are not the same\n");
			} else if ((pVolume1->warpData.mapName && !pVolume2->warpData.mapName) ||
					   (!pVolume1->warpData.mapName && pVolume2->warpData.mapName)) {
				fprintf(file, "  ** Warning: Volume map names are not the same\n");
			}
			if (pVolume1->warpData.spawnTarget && pVolume2->warpData.spawnTarget && stricmp(pVolume1->warpData.spawnTarget, pVolume2->warpData.spawnTarget) != 0) {
				fprintf(file, "  ** Warning: Volume spawn target names are not the same\n");
			} else if ((pVolume1->warpData.spawnTarget && !pVolume2->warpData.spawnTarget) ||
					   (!pVolume1->warpData.spawnTarget && pVolume2->warpData.spawnTarget)) {
				fprintf(file, "  ** Warning: Volume spawn target names are not the same\n");
			}
	}
	if (exprCompare(pVolume1->enteredActionCond, pVolume2->enteredActionCond) != 0) {
		fprintf(file, "  ** Warning: volume entered cond actions are not the same\n");
	}
	if (exprCompare(pVolume1->enteredAction, pVolume2->enteredAction) != 0) {
		fprintf(file, "  ** Warning: volume entered actions are not the same\n");
	}
	if (exprCompare(pVolume1->exitedActionCond, pVolume2->exitedActionCond) != 0) {
		fprintf(file, "  ** Warning: volume exit cond actions are not the same\n");
	}
	if (exprCompare(pVolume1->exitedAction, pVolume2->exitedAction) != 0) {
		fprintf(file, "  ** Warning: volume exit actions are not the same\n");
	}
	if (pVolume1->avoid != pVolume2->avoid) {
		fprintf(file, "  ** Warning: volume avoids are not the same\n");
	}
}


static const char *Convert_GetELEVolumeName(ELEVolume *pVolume)
{
	if (pVolume->volType == VolumeTypeNeighborhood) {
		return pVolume->neighborhood;
	} else {
		return pVolume->volName;
	}
}


static TriggerCondition *Convert_GetTriggerCondition(EncounterLayer *pEncLayer, char *pcTriggerName)
{
	int i;

	for(i=eaSize(&pEncLayer->triggerConditions)-1; i>=0; --i) {
		if (stricmp(pEncLayer->triggerConditions[i]->name, pcTriggerName) == 0) {
			return pEncLayer->triggerConditions[i];
		}
	}
	return NULL;
}


static NeighborhoodData *Convert_GetNeighborhood(EncounterLayer *pEncLayer, char *pcHoodName)
{
	int i;

	for(i=eaSize(&pEncLayer->neighborhoods)-1; i>=0; --i) {
		if (stricmp(pEncLayer->neighborhoods[i]->name, pcHoodName) == 0) {
			return pEncLayer->neighborhoods[i];
		}
	}
	return NULL;
}


static void ConvertVolumes(EncounterLayer ***peaEncLayers, EncounterLayer ***peaModifiedEncLayers, EncounterLayer ***peaOrigEncLayers, LibFileLoad ***peaGeoLayers, LibFileLoad ***peaModifiedGeoLayers, char ***peaExtraFiles, FILE *file)
{
	LibFileLoad *pGeoLayer  = NULL;
	GroupDef *pContainerDefLoad;
	GroupDef *pDefLoad;
	PropertyLoad *pPropertyLoad;
	GroupChild *pGroupLoad;
	GroupChild *pContainerGroup;
	ScopeTableLoad *pScopeLoad;
	WorldActionVolumeProperties *pActionData;
	WorldAIVolumeProperties *pAIData;
	WorldEventVolumeProperties *pEventData;
	WorldLandmarkVolumeProperties *pLandmarkData;
	WorldNeighborhoodVolumeProperties *pHoodData;
	WorldPowerVolumeProperties *pPowerData;
	WorldWarpVolumeProperties *pWarpData;
	NeighborhoodData *pHood;
	TriggerCondition *pTriggerCond;
	int next_id;
	char buf[1024];
	char typebuf[1024];
	int i,j,k;
	bool bLayerAltered;
	int count;
	int numLayers = 0;
	int numVolumes = 0;
	int numUniqueVolumes = 0;
	int numHoods = 0;
	int numVolAction = 0;
	int numVolAvoid = 0;
	int numVolEvent = 0;
	int numVolHood = 0;
	int numVolLandmark = 0;
	int numVolRace = 0;
	int numVolPower = 0;
	int numVolWarp = 0;
	char *pcName;
	char **eaNames = NULL;
	Message *pMessage;

	fprintf(file, "==============================================================\n");
	fprintf(file, "Volume Conversion\n");
	fprintf(file, "==============================================================\n");

	// Scan layers
	for(i=eaSize(peaEncLayers)-1; i>=0; --i) {
		EncounterLayer *pEncLayer = (*peaEncLayers)[i];
		bLayerAltered = false;

		// Scan volumes
		for(j=eaSize(&pEncLayer->namedVolumes)-1; j>=0; --j) {
			ELEVolume *pVolume = pEncLayer->namedVolumes[j];
			ELEVolume **eaVolumes = NULL;
			
			if (!bLayerAltered) {
				// When layer is first altered get Geo Layer and put these layers on the modified lists
				if (eaFind(peaModifiedEncLayers, pEncLayer) == -1) {
					eaPush(peaModifiedEncLayers, pEncLayer);
					eaPush(peaOrigEncLayers, StructClone(parse_EncounterLayer, pEncLayer));
				}
				pGeoLayer = Convert_GetGeoLayerForEncLayer(pEncLayer, peaGeoLayers, peaExtraFiles);
				eaPushUnique(peaModifiedGeoLayers, pGeoLayer);
				next_id = Convert_GetNextIdForGeoLayer(pGeoLayer);

				// Create the group data for the container
				pContainerGroup = StructCreate(parse_GroupChild);
				pContainerGroup->name = allocAllocString("Volumes");
				pContainerGroup->name_uid = next_id++;
				pContainerGroup->uid_in_parent = next_id++;
				pContainerGroup->seed = 0; // TODO: WHAT IS SEED?
				eaPush(&pGeoLayer->defs[0]->groups, pContainerGroup);

				// Create DefLoad for the container
				pContainerDefLoad = StructCreate(parse_DefLoad);
				pContainerDefLoad->name_str = allocAddString("Volumes");
				pContainerDefLoad->name_uid = pContainerGroup->name_uid;
				eaPush(&pGeoLayer->defs, pContainerDefLoad);

				// Also collect stats
				fprintf(file, "\n");
				fprintf(file, "ENC-LAYER %s\n", pEncLayer->pchFilename);
				fprintf(file, "GEO-LAYER %s\n", pGeoLayer->defs[0]->filename);
				bLayerAltered = true;
				++numLayers;
			}
			assert(pGeoLayer);

			++numVolumes;
			
			// Remove the volume from the layer
			eaRemove(&pEncLayer->namedVolumes, j);

			// Combine same-named volumes
			pcName = (char*)allocAddString(Convert_GetELEVolumeName(pVolume));
			if (eaFind(&eaNames, pcName) >= 0) {
				continue;
			}
			++numUniqueVolumes;
			eaPush(&eaNames, pcName);
			count = 1;
			for(k=j-1; k>=0; --k) {
				if ((pcName && pEncLayer->namedVolumes[k] && stricmp(pcName,Convert_GetELEVolumeName(pEncLayer->namedVolumes[k])) == 0) ||
					(!pcName && !Convert_GetELEVolumeName(pEncLayer->namedVolumes[k]))) {
					// Track other volume
					++count;
					eaPush(&eaVolumes, pEncLayer->namedVolumes[k]);
					Convert_CompareVolumes(file, pVolume, pEncLayer->namedVolumes[k]);

					// Remove other volume
					eaRemove(&pEncLayer->namedVolumes, k);
				}
			}

			// Print and collect stats
			switch(pVolume->volType) {
				case VolumeTypeEvent:
					fprintf(file, "  Event Volume: %s", pVolume->volName);
					++numVolEvent;
				xcase VolumeTypeNeighborhood:
					fprintf(file, "  Neighborhood Volume: %s (%s)", pVolume->neighborhood, pVolume->volName);
					++numVolHood;
				xcase VolumeTypeLandmark:
					fprintf(file, "  Landmark Volume: %s", pVolume->volName);
					++numVolLandmark;
				xcase VolumeTypeNemesisRace:
					fprintf(file, "  NemesisRace Volume: %s", pVolume->volName);
					++numVolRace;
				xcase VolumeTypePower:
					fprintf(file, "  Power Volume: %s", pVolume->volName);
					++numVolPower;
				xcase VolumeTypeWarp:
					fprintf(file, "  Warp Volume: %s", pVolume->volName);
					++numVolWarp;
				xdefault:
					fprintf(file, "  Other Volume: %s", pVolume->volName);
			}
			if (pVolume->avoid) {
				++numVolAvoid;
			}
			if ((pVolume->enteredAction) || (pVolume->exitedAction)) {
				++numVolAction;
			}
			if (count > 1) {
				fprintf(file, " [%d PARTS]\n", count);
			} else {
				fprintf(file, "\n");
			}

			// Create the group data
			pGroupLoad = StructCreate(parse_GroupChild);
			pGroupLoad->name = allocAllocString(Convert_GetELEVolumeName(pVolume));
			pGroupLoad->name_uid = next_id++;
			pGroupLoad->uid_in_parent = next_id++;
			pGroupLoad->seed = 0; // TODO: WHAT IS SEED?
			copyVec3(pVolume->volLoc, pGroupLoad->pos);
			quatToPYR(pVolume->volRot, pGroupLoad->rot);
			eaPush(&pContainerDefLoad->groups, pGroupLoad);

			// Create the scope data
			pScopeLoad = StructCreate(parse_ScopeTableLoad);
			pScopeLoad->name = StructAllocString(pGroupLoad->name);
			sprintf(buf, "%d,%d,", pContainerGroup->uid_in_parent, pGroupLoad->uid_in_parent);
			pScopeLoad->path = StructAllocString(buf);
			eaPush(&pGeoLayer->defs[0]->scope_entries, pScopeLoad);

			// Create the Geo volume data
			pAIData = NULL;
			pActionData = NULL;
			pEventData = NULL;
			pHoodData = NULL;
			pLandmarkData = NULL;
			pPowerData = NULL;
			pWarpData = NULL;
			typebuf[0] = '\0';
			switch(pVolume->volType) {
				case VolumeTypeEvent:
						if (typebuf[0]) { strcat(typebuf, " "); }
						strcat(typebuf, "Event");

				xcase VolumeTypeNeighborhood:
						pHood = Convert_GetNeighborhood(pEncLayer, pVolume->neighborhood);
						if (pHood) {
							if (typebuf[0]) { strcat(typebuf, " "); }
							strcat(typebuf, "Neighborhood");
							pHoodData = StructCreate(parse_WorldNeighborhoodVolumeProperties);
							pMessage = Convert_SaveLayerMessage(pGeoLayer, "neighborhoodDispName", pHood->name, pHood->displayNameMsg.pEditorCopy, peaExtraFiles);
							if (pMessage) {
								SET_HANDLE_FROM_STRING("Message", pMessage->pcMessageKey, pHoodData->display_name_msg.hMessage);
							}
							pHoodData->sound_effect = StructAllocString(pHood->soundEffect);
						} else {
							fprintf(file, "  ** Warning: Missing neighborhood in conversion\n");
						}

				xcase VolumeTypeNemesisRace:
						// Never used so no conversion

				xcase VolumeTypeLandmark:
						if (typebuf[0]) { strcat(typebuf, " "); }
						strcat(typebuf, "Landmark");
						pLandmarkData = StructCreate(parse_WorldLandmarkVolumeProperties);
						pLandmarkData->icon_name = StructAllocString(pVolume->landmarkData.pchIconName);
						pMessage = Convert_SaveLayerMessage(pGeoLayer, "landmarkDispName", pVolume->volName, pVolume->landmarkData.displayNameMsg.pEditorCopy, peaExtraFiles);
						if (pMessage) {
							SET_HANDLE_FROM_STRING("Message", pMessage->pcMessageKey, pLandmarkData->display_name_msg.hMessage);
						}

				xcase VolumeTypePower:
						if (typebuf[0]) { strcat(typebuf, " "); }
						strcat(typebuf, "Power");
						pPowerData = StructCreate(parse_WorldPowerVolumeProperties);
						COPY_HANDLE(pPowerData->power, pVolume->powerData.power);
						pPowerData->level = 0;
						pPowerData->repeat_time = pVolume->powerData.repeatTime;
						pTriggerCond = Convert_GetTriggerCondition(pEncLayer, pVolume->powerData.triggerCond);
						if (pTriggerCond) {
							pPowerData->trigger_cond = exprClone(pTriggerCond->cond);
						}
						switch(pVolume->powerData.strength) {
							xcase PowerStrength_Harmless: pPowerData->strength = WorldPowerVolumeStrength_Harmless;
							xcase PowerStrength_Default:  pPowerData->strength = WorldPowerVolumeStrength_Default;
							xcase PowerStrength_Deadly:   pPowerData->strength = WorldPowerVolumeStrength_Deadly;		
						}

				xcase VolumeTypeWarp:
						if (typebuf[0]) { strcat(typebuf, " "); }
						strcat(typebuf, "Warp");
						pWarpData = StructCreate(parse_WorldWarpVolumeProperties);
						pWarpData->map_name = StructAllocString(pVolume->warpData.mapName);
						pWarpData->spawn_target_name = StructAllocString(pVolume->warpData.spawnTarget);
			}
			if (pVolume->enteredAction || pVolume->exitedAction) {
				if (typebuf[0]) { strcat(typebuf, " "); }
				strcat(typebuf, "Action");
				pActionData = StructCreate(parse_WorldActionVolumeProperties);
				pActionData->entered_action_cond = exprClone(pVolume->enteredActionCond);
				pActionData->entered_action = exprClone(pVolume->enteredAction);
				pActionData->exited_action_cond = exprClone(pVolume->exitedActionCond);
				pActionData->exited_action = exprClone(pVolume->exitedAction);
			} else if (pVolume->enteredActionCond || pVolume->exitedActionCond) {
				fprintf(file, "  ** Warning: action condition ignored because there is no appropriate action\n");
			}
			if (pVolume->avoid) {
				pAIData = StructCreate(parse_WorldAIVolumeProperties);
				pAIData->avoid = true;
			}

			// Create DefLoad
			pDefLoad = StructCreate(parse_DefLoad);
			pDefLoad->name_str = allocAddString(pVolume->volName);
			pDefLoad->name_uid = pGroupLoad->name_uid;
			pDefLoad->property_structs.server_volume.action_volume_properties = pActionData;
			pDefLoad->ai_volume_properties = pAIData;
			pDefLoad->event_volume_properties = pEventData;
			pDefLoad->property_structs.server_volume.landmark_volume_properties = pLandmarkData;
			pDefLoad->property_structs.server_volume.neighborhood_volume_properties = pHoodData;
			pDefLoad->power_volume_properties = pPowerData;
			pDefLoad->property_structs.server_volume.warp_volume_properties = pWarpData;
			pDefLoad->property_structs.volume = StructCreate(parse_GroupVolumeProperties);
			if (pVolume->shape == VolumeShapeBox) {
				pDefLoad->property_structs.volume->eShape = GVS_Box;
				copyVec3(pVolume->local_min, pDefLoad->property_structs.volume->vBoxMin);
				copyVec3(pVolume->local_max, pDefLoad->property_structs.volume->vBoxMax);
			} else {
				pDefLoad->property_structs.volume->eShape = GVS_Sphere;
				pDefLoad->property_structs.volume->fSphereRadius =  pVolume->radius;
			}
			groupDefAddVolumeType(pDefLoad, typebuf);
			eaPush(&pGeoLayer->defs, pDefLoad);

			for(k=eaSize(&eaVolumes)-1; k>=0; --k) {
				ELEVolume *pSubVolume = eaVolumes[k];
				GroupDef *pSubDefLoad;
				GroupLoad *pSubGroupLoad;
				Vec3 vTempRot;

				// Add the subvolume group to the main volume
				pSubGroupLoad = StructCreate(parse_GroupChild);
				sprintf(buf, "%s %d", Convert_GetELEVolumeName(pSubVolume), k);
				pSubGroupLoad->name = StructAllocString(buf);
				pSubGroupLoad->name_uid = next_id++;
				pSubGroupLoad->uid_in_parent = next_id++;
				pSubGroupLoad->seed = 0; // TODO: WHAT IS SEED?
				subVec3(pSubVolume->volLoc, pVolume->volLoc, pSubGroupLoad->pos);
				quatToPYR(pSubVolume->volRot, vTempRot);
				subVec3(vTempRot, pGroupLoad->rot, pSubGroupLoad->rot);
				eaPush(&pDefLoad->groups, pSubGroupLoad);

				// Add the subvolume def to the main file
				pSubDefLoad = StructCreate(parse_DefLoad);
				pSubDefLoad->name_str = allocAddString(buf);
				pSubDefLoad->name_uid = pSubGroupLoad->name_uid;
				pSubDefLoad->property_structs.volume = StructCreate(parse_GroupVolumeProperties);
				if (pSubVolume->shape == VolumeShapeBox) {
					pSubDefLoad->property_structs.volume->eShape = GVS_Box;
					copyVec3(pSubVolume->local_min, pSubDefLoad->property_structs.volume->vBoxMin);
					copyVec3(pSubVolume->local_max, pSubDefLoad->property_structs.volume->vBoxMax);
				} else {
					pSubDefLoad->property_structs.volume->eShape = GVS_Sphere;
					pSubDefLoad->property_structs.volume->fSphereRadius =  pSubVolume->radius;
				}
				pSubDefLoad->property_structs.volume->bSubVolume = 1;
				eaPush(&pGeoLayer->defs, pSubDefLoad);
			}

			// Clear extra volumes
			eaDestroy(&eaVolumes);
		}

		// Scan neighborhoods
		for(j=eaSize(&pEncLayer->neighborhoods)-1; j>=0; --j) {
			pHood = pEncLayer->neighborhoods[j];
			
			// Collect stats
			if (!bLayerAltered) {
				fprintf(file, "\nLAYER %s\n", pEncLayer->pchFilename);
				bLayerAltered = true;
				++numLayers;
			}
			fprintf(file, "  Neighborhood: %s\n", pHood->name);
			++numHoods;

			// Remove the data
			eaRemove(&pEncLayer->neighborhoods, j);
		}

		eaClear(&eaNames);
	}

	fprintf(file, "\n");
	fprintf(file, "%d volumes (%d unique) and %d hoods in %d Layers\n", numVolumes, numUniqueVolumes, numHoods, numLayers);
	fprintf(file, "\n");
	fprintf(file, "%d action volumes\n", numVolAction);
	fprintf(file, "%d AI avoid volumes\n", numVolAvoid);
	fprintf(file, "%d event volumes\n", numVolEvent);
	fprintf(file, "%d landmark volumes\n", numVolLandmark);
	fprintf(file, "%d neighborhood volumes\n", numVolHood);
	fprintf(file, "%d power volumes\n", numVolPower);
	fprintf(file, "%d race volumes\n", numVolRace);
	fprintf(file, "%d warp volumes\n", numVolWarp);
	fprintf(file, "\n");
}

static GroupDef *ConvertFindLibraryPiece(int id, const char *pcName, LibFileLoad ***peaLibraryLayers)
{
	int i, j;

	for(i=eaSize(peaLibraryLayers)-1; i>=0; --i) {
		LibFileLoad *pLayer = (*peaLibraryLayers)[i];
		for(j=eaSize(&pLayer->defs)-1; j>=0; --j) {
			GroupDef *pDef = pLayer->defs[j];
			if (pDef->name_uid == id) {
				return pDef;
			}
		}
	}
	for(i=eaSize(peaLibraryLayers)-1; i>=0; --i) {
		LibFileLoad *pLayer = (*peaLibraryLayers)[i];
		for(j=eaSize(&pLayer->defs)-1; j>=0; --j) {
			GroupDef *pDef = pLayer->defs[j];
			if (stricmp(pDef->name, pcName) == 0) {
				return pDef;
			}
		}
	}
	return NULL;
}


static GroupDef *ConvertFindMultiPiece(int id, LibFileLoad *pGeoLayer)
{
	int i, j;
	int count = 0;
	GroupDef *pTarget = NULL;

	for(i=eaSize(&pGeoLayer->defs)-1; i>=0; --i) {
		GroupDef *pDef = pGeoLayer->defs[i];
		for(j=eaSize(&pDef->groups)-1; j>=0; --j) {
			if (pDef->groups[j]->name_uid == id) {
				++count;
			}
		}
		if (pDef->name_uid == id) {
			pTarget = pDef;
		}
	}
	if (count > 1) {
		return pTarget;
	}
	return NULL;
}


static GroupDef *ConvertFindGroupDef(char *pcPath, LibFileLoad *pGeoLayer, LibFileLoad ***peaModifiedGeoLayers, LibFileLoad ***peaLibraryLayers, FILE *file)
{
	char buf[1024];
	char *startPtr, *endPtr;
	int i, j;
	int id;
	GroupDef *pDefLoad = NULL;
	bool bFound;
	int numInstances = 0;

	// find first
	strcpy(buf, pcPath);
	startPtr = buf;

	if (!eaSize(&pGeoLayer->defs)) {
		return NULL;
	}
	pDefLoad = pGeoLayer->defs[0];

	while (startPtr) {
		endPtr = strchr(startPtr, ':');
		if (endPtr) {
			*endPtr = '\0';
			++endPtr;
		}
		id = atoi(startPtr);

		bFound = false;
		for(i=eaSize(&pDefLoad->groups)-1; i>=0 && !bFound; --i) {
			if (pDefLoad->groups[i]->uid_in_parent == id) {
				if (pDefLoad->groups[i]->name_uid < 0) {
					GroupDef *pLibraryPiece = ConvertFindLibraryPiece(pDefLoad->groups[i]->name_uid, pDefLoad->groups[i]->name, peaLibraryLayers);
					if (pLibraryPiece) {
						int newId = Convert_GetNextIdForGeoLayer(pGeoLayer);
						fprintf(file, "    Note: instancing library piece %s(%d) - Nesting %d\n", pDefLoad->groups[i]->name, pDefLoad->groups[i]->name_uid, numInstances);

						// Modify the group to use the non-library piece
						pDefLoad->groups[i]->name_uid = newId;
						bFound = true;

						// clone the library piece and add it to the current layer
						pDefLoad = StructClone(parse_DefLoad, pLibraryPiece);
						pDefLoad->name_uid = newId;
						eaPush(&pGeoLayer->defs, pDefLoad);

						// Mark the layer as modified
						eaPushUnique(peaModifiedGeoLayers, pGeoLayer);
						++numInstances;
					} else {
						for(j=eaSize(&pGeoLayer->defs)-1; j>=0 && !bFound; --j) {
							if (pGeoLayer->defs[j]->name_uid == pDefLoad->groups[i]->name_uid) {
								pDefLoad = pGeoLayer->defs[j];
								bFound = true;
							}
						}
						if (!bFound) {
							fprintf(file, "    Warning: MISSING library piece %s(%d)\n", pDefLoad->groups[i]->name, pDefLoad->groups[i]->name_uid);
						} else {
							fprintf(file, "    Note2: Negative ID (%d) was a local piece instead of library piece\n", pDefLoad->groups[i]->name_uid);
						}
					}
				} else {
					GroupDef *pMultiPiece = ConvertFindMultiPiece(pDefLoad->groups[i]->name_uid, pGeoLayer);
					if (pMultiPiece) {
						int newId = Convert_GetNextIdForGeoLayer(pGeoLayer);
						fprintf(file, "    Note: instancing multi piece %s(%d)\n", pDefLoad->groups[i]->name, pDefLoad->groups[i]->name_uid);

						// Modify the group to use the non-multi piece
						pDefLoad->groups[i]->name_uid = newId;
						bFound = true;

						// clone the multi piece and add it to the current layer
						pDefLoad = StructClone(parse_DefLoad, pMultiPiece);
						pDefLoad->name_uid = newId;
						eaPush(&pGeoLayer->defs, pDefLoad);

						// Mark the layer as modified
						eaPushUnique(peaModifiedGeoLayers, pGeoLayer);
						++numInstances;
					} else {
						for(j=eaSize(&pGeoLayer->defs)-1; j>=0 && !bFound; --j) {
							if (pGeoLayer->defs[j]->name_uid == pDefLoad->groups[i]->name_uid) {
								pDefLoad = pGeoLayer->defs[j];
								bFound = true;
							}
						}
					}
				}
			}
		}
		if (!bFound) {
			return NULL;
		}
		startPtr = endPtr;
	} 

	return pDefLoad;
}

static char *ConvertGetGroupDefPath(char *pcPath)
{
	char *ptr;

	ptr = strchr(pcPath, ':');
	if (!ptr) {
		return NULL;
	}
	++ptr;
	ptr = strchr(ptr, ':');
	if (!ptr) {
		return NULL;
	}
	++ptr;
	return ptr;
}

static void ConvertMakeTrackerPath(char *pcPath, char **estrPath)
{
	char *ptr;

	estrConcatString(estrPath, pcPath, (int)strlen(pcPath));
	while(ptr = strchr(*estrPath, ':')) {
		*ptr = ',';
	}
	estrConcatChar(estrPath, ',');
}

static LibFileLoad *ConvertFindGeoLayer(char *pcPath, LibFileLoad ***peaGeoLayers, const char *pcEncLayerPath)
{
	char buf[1024];
	char buf2[1024];
	char *ptr, *ptr2;
	int i;

	// Extract the geo layer file name from the tracker path
	ptr = strchr(pcPath, ':');
	if (!ptr) {
		return NULL;
	}
	++ptr;
	if (*ptr == '"') {
		++ptr;
	}
	strcpy(buf, ptr);
	while(ptr=strchr(buf,'\\')) {
		*ptr = '/';
	}

	ptr = strchr(buf, ':');
	if (!ptr) {
		return NULL;
	}
	if (*(ptr-1) == '"') {
		--ptr;
	}
	*ptr = '\0';

	// Find the layer
	//for(i=eaSize(peaGeoLayers)-1; i>=0; --i) {
	//	LibFileLoad *pGeoLayer = (*peaGeoLayers)[i];
	//	if (eaSize(&pGeoLayer->defs) && (stricmp(buf, pGeoLayer->defs[0]->filename) == 0)) {
	//		return pGeoLayer;
	//	}
	//}

	// Check for file in location of enc layer
	strcpy(buf2, pcEncLayerPath);
	while(ptr=strchr(buf2,'\\')) {
		*ptr = '/';
	}
	ptr2 = strrchr(buf2, '/');
	if (!ptr2) {
		return NULL;
	}
	++ptr2;
	ptr = strrchr(buf, '/');
	if (!ptr) {
		return NULL;
	}
	++ptr;
	strcpy_s(ptr2,strlen(ptr)+1,ptr);

	for(i=eaSize(peaGeoLayers)-1; i>=0; --i) {
		LibFileLoad *pGeoLayer = (*peaGeoLayers)[i];
		if (eaSize(&pGeoLayer->defs) && (stricmp(buf2, pGeoLayer->defs[0]->filename) == 0)) {
			return pGeoLayer;
		}
	}

	return NULL;
}

typedef struct GroupChangeInfo 
{
	const char *pcMapName;
	const char *pcGroupName;
} GroupChangeInfo;

typedef struct ScanCallbackInfo
{
	const char *pcTypeName;
	const char *pcFileName;
	GroupChangeInfo **eaChangeInfos;
	FILE *file;
} ScanCallbackInfo;

static bool ScanExpression(Expression *pExpr, MissionDef *pMission, const char *pcPathString, ScanCallbackInfo *pInfo)
{
	char *pString = exprGetCompleteString(pExpr);
	char *ptr;
	int i;
	bool bFound = false;
	
	if (!pString) {
		return false;
	}
	
	ptr = strstri(pString, "clickablename");
	if (!ptr) {
		return false;
	}

	ptr += 14;

	for(i=eaSize(&pInfo->eaChangeInfos)-1; i>=0; --i) {
		if (pInfo->eaChangeInfos[i]->pcGroupName && strncmp(ptr, pInfo->eaChangeInfos[i]->pcGroupName, strlen(pInfo->eaChangeInfos[i]->pcGroupName)) == 0) {
			fprintf(pInfo->file, "  %s match for '%s' in '%s'\n", pInfo->pcTypeName, pInfo->eaChangeInfos[i]->pcGroupName, pInfo->pcFileName);
			bFound = true;
		}
	}
	if (!bFound) {
		fprintf(pInfo->file, "  %s NO match for '%s'\n", pInfo->pcTypeName, pInfo->pcFileName);
	}
	return bFound;
}

static void ScanExpressionsForClickableNameEvents(GroupChangeInfo **eaChangeInfos, EncounterLayer ***peaEncLayers, FILE *file)
{
	DictionaryEArrayStruct *pMissions = resDictGetEArrayStruct("MissionDef");
	DictionaryEArrayStruct *pFSMs = resDictGetEArrayStruct("FSM");
	DictionaryEArrayStruct *pEncounters = resDictGetEArrayStruct("EncounterDef");
	int i;
	ScanCallbackInfo sInfo;

	sInfo.eaChangeInfos = eaChangeInfos;
	sInfo.file = file;

	sInfo.pcTypeName = "Mission";
	for(i=eaSize(&pMissions->ppReferents)-1; i>=0; --i) {
		MissionDef *pDef = pMissions->ppReferents[i];
		sInfo.pcFileName = pDef->filename;
		ParserScanForSubstruct(parse_MissionDef, pDef, parse_Expression, 0, 0, ScanExpression, &sInfo);
	}

	sInfo.pcTypeName = "FSM";
	for(i=eaSize(&pFSMs->ppReferents)-1; i>=0; --i) {
		FSM *pFSM = pFSMs->ppReferents[i];
		sInfo.pcFileName = pFSM->fileName;
		ParserScanForSubstruct(parse_FSM, pFSM, parse_Expression, 0, 0, ScanExpression, &sInfo);
	}

	sInfo.pcTypeName = "Encounter";
	for(i=eaSize(&pEncounters->ppReferents)-1; i>=0; --i) {
		EncounterDef *pDef = pEncounters->ppReferents[i];
		sInfo.pcFileName = pDef->filename;
		ParserScanForSubstruct(parse_EncounterDef, pDef, parse_Expression, 0, 0, ScanExpression, &sInfo);
	}

	sInfo.pcTypeName = "EncLayer";
	for(i=eaSize(peaEncLayers)-1; i>=0; --i) {
		EncounterLayer *pLayer = (*peaEncLayers)[i];
		sInfo.pcFileName = pLayer->pchFilename;
		ParserScanForSubstruct(parse_EncounterLayer, pLayer, parse_Expression, 0, 0, ScanExpression, &sInfo);
	}

}

static void ScanClickablesInGroup(EncounterLayer *pLayer, StaticEncounterGroup *pGroup, StaticEncounterGroup *pClickParent, StaticEncounterGroup *pParent, int count, FILE *file)
{
	int i;

	// Set the parent
	pGroup->parentGroup = pParent;

	// Iterate the clickables
	for(i=eaSize(&pGroup->clickableList)-1; i>=0; --i) {
		ClickableObject *pClickable = pGroup->clickableList[i];
		pClickable->groupOwner = pClickParent;
		pClickable->id = count;
		eaPush(&pLayer->clickableObjects, pClickable);
	}

	// Recurse child groups
	for(i=eaSize(&pGroup->childList)-1; i>=0; --i) {
		ScanClickablesInGroup(pLayer, pGroup->childList[i], pGroup->childList[i], pGroup, count+1, file);
	}
}

static void ConvertRemoveClickables(EncounterLayer *pLayer, StaticEncounterGroup *pGroup)
{
	int i;

	// Remove the clickables
	pGroup->clickableList = NULL;

	// Recurse child groups
	for(i=eaSize(&pGroup->childList)-1; i>=0; --i) {
		ConvertRemoveClickables(pLayer, pGroup->childList[i]);
	}
}

typedef struct CooldownInfo 
{
	int value;
	int count;
} CooldownInfo;

static CooldownInfo **eaCooldownInfos = NULL;

static void ConvertTheClickable(LibFileLoad *pGeoLayer, EncounterLayer *pLayer, ClickableObject *pClickable, GroupDef *pDefLoad, LogicalGroup *pLogicalGroup, const char *pcName, const char *pcPath, FILE *file)
{
	WorldInteractionPropertyEntry *pEntry;
	ScopeTableLoad *pScopeLoad;
	int i;

	// Initialize properties
	if (!pDefLoad->property_structs.interaction_properties) {
		pDefLoad->property_structs.interaction_properties = StructCreate(parse_WorldInteractionProperties);
	}
	if (!eaSize(&pDefLoad->property_structs.interaction_properties->entries)) {
		pEntry = StructCreate(parse_WorldInteractionPropertyEntry);
		eaPush(&pDefLoad->property_structs.interaction_properties->entries, pEntry);
	} else {
		pEntry = pDefLoad->property_structs.interaction_properties->entries[0];
	}

	// Set up properties by type
	if (pClickable->eType == kClickableType_CraftingStation) {
		pEntry->interaction_class = (char*)allocAddString("CraftingStation");
		pEntry->crafting_properties = StructCreate(parse_WorldCraftingInteractionProperties);
		if (pClickable->objectParams) {
			pEntry->crafting_properties->skill_flags = pClickable->objectParams->eSkillType;
			pEntry->crafting_properties->max_skill = pClickable->objectParams->iSkillMax;
			COPY_HANDLE(pEntry->crafting_properties->craft_reward_table, pClickable->objectParams->hCraftRewardTable);
			COPY_HANDLE(pEntry->crafting_properties->deconstruct_reward_table, pClickable->objectParams->hDeconstructRewardTable);
			COPY_HANDLE(pEntry->crafting_properties->experiment_reward_table, pClickable->objectParams->hExperimentRewardTable);
		}

	} else if ((pClickable->eType == kClickableType_Door) && (pClickable->oldInteractProps.mapName || pClickable->oldInteractProps.spawnTarget)) {
		pEntry->interaction_class = (char*)allocAddString("Door");
		pEntry->door_properties = StructCreate(parse_WorldDoorInteractionProperties);
		pEntry->door_properties->map_name = StructAllocString(pClickable->oldInteractProps.mapName);
		pEntry->door_properties->spawn_target_name = StructAllocString(pClickable->oldInteractProps.spawnTarget);

	} else if (GET_REF(pClickable->oldInteractProps.hContactDef)) {
		pEntry->interaction_class = (char*)allocAddString("Contact");
		pEntry->contact_properties = StructCreate(parse_WorldContactInteractionProperties);
		COPY_HANDLE(pEntry->contact_properties->contact_def, pClickable->oldInteractProps.hContactDef);

	} else if (pClickable->eType == kClickableType_Mission) {
		pEntry->interaction_class = (char*)allocAddString("Clickable");
	}

	// Set up type-neutral properties
	pEntry->interact_cond = exprClone(pClickable->oldInteractProps.interactCond);
	pEntry->success_cond = exprClone(pClickable->oldInteractProps.interactSuccessCond);

	if (pClickable->oldInteractProps.interactText.pEditorCopy ||
		pClickable->oldInteractProps.interactFailedText.pEditorCopy) {
		Message *pMsg;
		char buf[1024];

		// Text
		pEntry->text_properties = StructCreate(parse_WorldTextInteractionProperties);
		if (pClickable->oldInteractProps.interactText.pEditorCopy) {
			sprintf(buf, "Geometry.interactOptionText.%s.%s", pGeoLayer->defs[0]->name, pcName);
			pMsg = RefSystem_ReferentFromString("Message", buf);
			if (!pMsg) {
				pMsg = StructClone(parse_Message, pClickable->oldInteractProps.interactText.pEditorCopy);
				pMsg->pcMessageKey = allocAddString(buf);
				RefSystem_AddReferent("Message", buf, pMsg);
			} else {
				StructCopyAll(parse_Message, pClickable->oldInteractProps.interactText.pEditorCopy, pMsg);
				fprintf(file, "    Overwriting message: %s\n", buf);
			}
			sprintf(buf, "%s.ms", pGeoLayer->defs[0]->filename);
			pMsg->pcFilename = allocAddString(buf);
			SET_HANDLE_FROM_REFERENT("Message", pMsg, pEntry->text_properties->interact_option_text.hMessage);
		}
		if (pClickable->oldInteractProps.interactFailedText.pEditorCopy) {
			sprintf(buf, "Geometry.FailureText.%s.%s", pGeoLayer->defs[0]->name, pcName);
			pMsg = RefSystem_ReferentFromString("Message", buf);
			if (!pMsg) {
				pMsg = StructClone(parse_Message, pClickable->oldInteractProps.interactFailedText.pEditorCopy);
				pMsg->pcMessageKey = allocAddString(buf);
				RefSystem_AddReferent("Message", buf, pMsg);
			} else {
				StructCopyAll(parse_Message, pClickable->oldInteractProps.interactFailedText.pEditorCopy, pMsg);
				fprintf(file, "    Overwriting message: %s\n", buf);
			}
			sprintf(buf, "%s.ms", pGeoLayer->defs[0]->filename);
			pMsg->pcFilename = allocAddString(buf);
			SET_HANDLE_FROM_REFERENT("Message", pMsg, pEntry->text_properties->failure_console_text.hMessage);
		}
	}

	{
		// Collect cooldown info
		for(i=eaSize(&eaCooldownInfos)-1; i>=0; --i) {
			if (eaCooldownInfos[i]->value == pClickable->oldInteractProps.uInteractCoolDown) {
				++eaCooldownInfos[i]->count;
				break;
			}
		}
		if (i < 0) {
			CooldownInfo *pInfo = calloc(1, sizeof(CooldownInfo));
			pInfo->value = pClickable->oldInteractProps.uInteractCoolDown;
			pInfo->count = 1;
			eaPush(&eaCooldownInfos, pInfo);
		}
	}

	// Timing
	pEntry->time_properties = StructCreate(parse_WorldTimeInteractionProperties);
	pEntry->time_properties->use_time = pClickable->oldInteractProps.uInteractTime;
	pEntry->time_properties->active_time = pClickable->oldInteractProps.uInteractActiveFor;

	if (pClickable->oldInteractProps.uInteractCoolDown == INTERACTABLE_COOLDOWN_SHORT) {
		pEntry->time_properties->cooldown_time = WorldCooldownTime_Short;
	} else if ((pClickable->oldInteractProps.uInteractCoolDown == 0) ||
		       (pClickable->oldInteractProps.uInteractCoolDown == INTERACTABLE_COOLDOWN_MEDIUM)) {
		// Clickables with zero default to 300
		pEntry->time_properties->cooldown_time = WorldCooldownTime_Medium;
	} else if (pClickable->oldInteractProps.uInteractCoolDown == INTERACTABLE_COOLDOWN_LONG) {
		pEntry->time_properties->cooldown_time = WorldCooldownTime_Long;
	} else {
		pEntry->time_properties->cooldown_time = WorldCooldownTime_Custom;
		pEntry->time_properties->custom_cooldown_time = pClickable->oldInteractProps.uInteractCoolDown;
	}

	pEntry->time_properties->no_respawn = ((pClickable->oldInteractProps.eInteractType & InteractType_NoRespawn) != 0);
	pEntry->time_properties->hide_during_cooldown = ((pClickable->oldInteractProps.eInteractType & InteractType_ConsumeOnUse) != 0);
	pEntry->time_properties->interrupt_on_damage = ((pClickable->oldInteractProps.eInteractType & InteractType_BreakOnDamage) != 0);
	pEntry->time_properties->interrupt_on_move = ((pClickable->oldInteractProps.eInteractType & InteractType_BreakOnMove) != 0);
	pEntry->time_properties->interrupt_on_power = ((pClickable->oldInteractProps.eInteractType & InteractType_BreakOnPower) != 0);

	if (pClickable->oldInteractProps.interactAction ||
		pClickable->oldInteractProps.interactFailAction ||
		pClickable->oldInteractProps.noLongerActiveAction ||
		(pClickable->oldInteractProps.interactGameActions && eaSize(&pClickable->oldInteractProps.interactGameActions->actions))) {
		// Actions
		pEntry->action_properties = StructCreate(parse_WorldActionInteractionProperties);
		pEntry->action_properties->success_expr = exprClone(pClickable->oldInteractProps.interactAction);
		pEntry->action_properties->failure_expr = exprClone(pClickable->oldInteractProps.interactFailAction);
		pEntry->action_properties->no_longer_active_expr = exprClone(pClickable->oldInteractProps.noLongerActiveAction);
		if (pClickable->oldInteractProps.interactGameActions && eaSize(&pClickable->oldInteractProps.interactGameActions->actions)) {
			StructCopyAll(parse_WorldGameActionBlock, pClickable->oldInteractProps.interactGameActions, &pEntry->action_properties->success_actions);
		}
	}

	if (GET_REF(pClickable->hRewardTable)) {
		// Rewards
		pEntry->reward_properties = StructCreate(parse_WorldRewardInteractionProperties);
		COPY_HANDLE(pEntry->reward_properties->reward_table, pClickable->hRewardTable);
		if (pLayer->layerLevel > 0) {
			pEntry->reward_properties->reward_level_type = WorldRewardLevelType_Custom;
			pEntry->reward_properties->custom_reward_level = pLayer->layerLevel;
		}
	}

	// Alter the scope data if present
	for(i=eaSize(&pGeoLayer->defs[0]->scope_entries)-1; i>=0; --i) {
		if (stricmp(pGeoLayer->defs[0]->scope_entries[i]->path, pcPath) == 0) {
			pGeoLayer->defs[0]->scope_entries[i]->name = allocAllocString(pcName);
			break;
		}
	}
	if (i<0) {
		// Create the scope data since not present
		pScopeLoad = StructCreate(parse_ScopeTableLoad);
		pScopeLoad->name = StructAllocString(pcName);
		pScopeLoad->path = StructAllocString(pcPath);
		eaPush(&pGeoLayer->defs[0]->scope_entries, pScopeLoad);
	}

	// Put scope data into logical group (if any)
	if (pLogicalGroup) {
		char *pcEntryName = StructAllocString(pcName);
		eaPush(&pLogicalGroup->child_names, pcEntryName);
	}
}

static void ConvertClickables(EncounterLayer ***peaEncLayers, EncounterLayer ***peaModifiedEncLayers, EncounterLayer ***peaOrigEncLayers, LibFileLoad ***peaGeoLayers, LibFileLoad ***peaModifiedGeoLayers, LibFileLoad ***peaLibraryLayers, char ***peaExtraFiles, FILE *file)
{
	int numTotalClickables = 0, numLayerClickables;
	int numTotalNestedZero = 0, numLayerNestedZero;
	int numTotalNestedOne = 0, numLayerNestedOne;
	int numTotalNestedMore = 0, numLayerNestedMore;
	int numTotalMultiName = 0, numLayerMultiName;
	int numTotalMultiNameCount = 0, numLayerMultiNameCount;
	int numTotalMultiNameNested = 0, numLayerMultiNameNested;
	int numTotalMultiNameNestedCount = 0, numLayerMultiNameNestedCount;
	int i, j, k;
	char **eaNames = NULL;
	LibFileLoad *pGeoLayer;
	GroupDef *pDefLoad;
	char *pcPath;
	EncounterLayer **eaOrphanLayers = NULL;
	GroupChangeInfo **eaChangeInfos = NULL;

	// Iterate the layers
	for(i=eaSize(peaEncLayers)-1; i>=0; --i) {
		EncounterLayer *pLayer = (*peaEncLayers)[i];

		// Recurse the groups to collect clickable info
		ScanClickablesInGroup(pLayer, &pLayer->rootGroup, NULL, NULL, 0, file);

		// Clear data
		numLayerClickables = 0;
		numLayerNestedZero = 0;
		numLayerNestedOne = 0;
		numLayerNestedMore = 0;
		numLayerMultiName = 0;
		numLayerMultiNameCount = 0;
		numLayerMultiNameNested = 0;
		numLayerMultiNameNestedCount = 0;

		if (eaSize(&pLayer->clickableObjects)) {
			fprintf(file, "Layer: %s\n", pLayer->pchFilename);
		}

		// Iterate the discovered clickables
		for(j=eaSize(&pLayer->clickableObjects)-1; j>=0; --j) {
			ClickableObject *pClickable = pLayer->clickableObjects[j];
			bool bFound = false;

			// Numbers for reporting
			++numLayerClickables;
			++numTotalClickables;
			if (pClickable->id == 0) {
				++numTotalNestedZero;
				++numLayerNestedZero;
			} else if (pClickable->id == 1) {
				++numTotalNestedOne;
				++numLayerNestedOne;
			} else {
				++numTotalNestedMore;
				++numLayerNestedMore;
			}

			// Look for duplicates
			bFound = false;
			for(k=eaSize(&eaNames)-1; k>=0; --k) {
				if (stricmp(eaNames[k], pClickable->name) == 0) {
					bFound = true;
				}
			}
			if (!bFound) {
				LogicalGroup *pLogicalGroup;
				int numNames = 1;
				int iNextNum;

				// Check for duplicate names
				eaPush(&eaNames, pClickable->name);
				for(k=j-1; k>=0; --k) {
					ClickableObject *pOther = pLayer->clickableObjects[k];
					if (stricmp(pOther->name, pClickable->name) == 0) {
						++numNames;
					}
				}

				// Logging info
				fprintf(file, "  Clickable = (%d) %s\n", numNames, pClickable->name);
				if (numNames > 1) {
					++numLayerMultiName;
					++numTotalMultiName;
					numLayerMultiNameCount += numNames;
					numTotalMultiNameCount += numNames;
					if (pClickable->id > 0) {
						++numLayerMultiNameNested;
						++numTotalMultiNameNested;
						numLayerMultiNameNestedCount += numNames;
						numTotalMultiNameNestedCount += numNames;
					}
				}
				
				pGeoLayer = ConvertFindGeoLayer(pClickable->trackerHandleStr, peaGeoLayers, pLayer->pchFilename);
				if (pGeoLayer) {
					// If duplicate names, set up a logical group
					pLogicalGroup = NULL;
					if (numNames > 1) {
						GroupChangeInfo *pInfo;

						fprintf(file, "    Moved %d into logical group named '%s'\n", numNames, pClickable->name);
						for(k=eaSize(&pGeoLayer->defs[0]->logical_groups)-1; k>=0; --k) {
							if (stricmp(pGeoLayer->defs[0]->logical_groups[k]->group_name, pClickable->name) == 0) {
								fprintf(file, "    Collision with existing group of same name\n");
								break;
							}
						}
						pLogicalGroup = StructCreate(parse_LogicalGroup);
						pLogicalGroup->group_name = StructAllocString(pClickable->name);

						pInfo = calloc(1, sizeof(GroupChangeInfo));
						pInfo->pcMapName = pLayer->pchFilename;
						pInfo->pcGroupName = pLogicalGroup->group_name;
						eaPush(&eaChangeInfos, pInfo);

						if (pClickable->groupOwner) {
							fprintf(file, "    Was member of group '%s'\n", pClickable->groupOwner->groupName);
						}
					} else if (pClickable->groupOwner) {
						fprintf(file, "    Kept logical group named '%s'\n", pClickable->groupOwner->groupName);
						for(k=eaSize(&pGeoLayer->defs[0]->logical_groups)-1; k>=0; --k) {
							if (stricmp(pGeoLayer->defs[0]->logical_groups[k]->group_name, pClickable->groupOwner->groupName) == 0) {
								pLogicalGroup = pGeoLayer->defs[0]->logical_groups[k];
								break;
							}
						}
						if (k<0) {
							pLogicalGroup = StructCreate(parse_LogicalGroup);
							pLogicalGroup->group_name = StructAllocString(pClickable->groupOwner->groupName);
						}
					}
				}

				// Perform conversion on this set
				if (!pGeoLayer) {
					char *estrText = NULL;
					fprintf(file, "    Cannot find geo layer for '%s'\n", pClickable->trackerHandleStr);
					ParserWriteTextEscaped(&estrText, parse_ClickableObject, pClickable, 0, 0);
					if (estrText) {
						fprintf(file, "    %s\n", estrText);
						estrDestroy(&estrText);
					}
					eaPushUnique(&eaOrphanLayers, pLayer);
				} else {
					if (pLogicalGroup) {
						for(k=eaSize(&pGeoLayer->defs[0]->logical_groups)-1; k>=0; --k) {
							if (pLogicalGroup == pGeoLayer->defs[0]->logical_groups[k]) {
								break;
							}
						}
						if (k<0) {
							eaPush(&pGeoLayer->defs[0]->logical_groups, pLogicalGroup);
						}
					}
					pcPath = ConvertGetGroupDefPath(pClickable->trackerHandleStr);
					pDefLoad = ConvertFindGroupDef(pcPath, pGeoLayer, peaModifiedGeoLayers, peaLibraryLayers, file);
					if (!pDefLoad) {
						char *estrText = NULL;
						fprintf(file, "    Cannot find def '%s' on layer '%s'\n", pcPath, pGeoLayer->defs[0]->filename);
						ParserWriteTextEscaped(&estrText, parse_ClickableObject, pClickable, 0, 0);
						if (estrText) {
							fprintf(file, "    %s\n", estrText);
							estrDestroy(&estrText);
						}
						eaPushUnique(&eaOrphanLayers, pLayer);
					} else {
						char *estrPath = NULL;
						char buf[1024];
						if (numNames > 1) {
							sprintf(buf, "%s_%d", pClickable->name, 1);
						} else {
							strcpy(buf, pClickable->name);
						}
						ConvertMakeTrackerPath(pcPath, &estrPath);
						ConvertTheClickable(pGeoLayer, pLayer, pClickable, pDefLoad, pLogicalGroup, buf, estrPath, file);
						eaPushUnique(peaModifiedGeoLayers, pGeoLayer);
						estrDestroy(&estrPath);
					}
				}
				iNextNum = 2;

				for(k=j-1; k>=0; --k) {
					ClickableObject *pOther = pLayer->clickableObjects[k];
					if (stricmp(pOther->name, pClickable->name) == 0) {
						pGeoLayer = ConvertFindGeoLayer(pOther->trackerHandleStr, peaGeoLayers, pLayer->pchFilename);
						if (!pGeoLayer) {
							char *estrText = NULL;
							fprintf(file, "    Cannot find geo layer for '%s'\n", pOther->trackerHandleStr);
							ParserWriteTextEscaped(&estrText, parse_ClickableObject, pOther, 0, 0);
							if (estrText) {
								fprintf(file, "    %s\n", estrText);
								estrDestroy(&estrText);
							}
							eaPushUnique(&eaOrphanLayers, pLayer);
						} else {
							pcPath = ConvertGetGroupDefPath(pOther->trackerHandleStr);
							pDefLoad = ConvertFindGroupDef(pcPath, pGeoLayer, peaModifiedGeoLayers, peaLibraryLayers, file);
							if (!pDefLoad) {
								char *estrText = NULL;
								fprintf(file, "    Cannot find def '%s' on layer '%s'\n", pcPath, pGeoLayer->defs[0]->filename);
								ParserWriteTextEscaped(&estrText, parse_ClickableObject, pOther, 0, 0);
								if (estrText) {
									fprintf(file, "    %s\n", estrText);
									estrDestroy(&estrText);
								}
								eaPushUnique(&eaOrphanLayers, pLayer);
							} else {
								char *estrPath = NULL;
								char buf[1024];
								sprintf(buf, "%s_%d", pClickable->name, iNextNum);
								++iNextNum;
								ConvertMakeTrackerPath(pcPath, &estrPath);
								ConvertTheClickable(pGeoLayer, pLayer, pOther, pDefLoad, pLogicalGroup, buf, estrPath, file);
								eaPushUnique(peaModifiedGeoLayers, pGeoLayer);
								estrDestroy(&estrPath);
							}
						}
					}
				}
			}
		}

		eaDestroy(&eaNames);

		if (eaSize(&pLayer->clickableObjects)) {
			// Mark layer as modified
			eaPush(peaOrigEncLayers, StructClone(parse_EncounterLayer, pLayer));
			eaPush(peaModifiedEncLayers, pLayer);

			// Remove the clickables from the encounter layer
			pLayer->clickableObjects = NULL;
			ConvertRemoveClickables(pLayer, &pLayer->rootGroup);

			// Print a layer report
			fprintf(file, "  Counts = %d (%d %d %d) (%d/%d %d/%d)\n", 
				numLayerClickables, numLayerNestedZero, numLayerNestedOne, numLayerNestedMore,
				numLayerMultiName, numLayerMultiNameCount, numLayerMultiNameNested, numLayerMultiNameNestedCount);
			fprintf(file, "\n\n");
		}
	}

	// Print a final report
	fprintf(file, "Total Enc Layers = %d\n", eaSize(peaModifiedEncLayers));
	fprintf(file, "Total Geo Layers = %d\n", eaSize(peaModifiedGeoLayers));
	fprintf(file, "Total Counts = %d (%d %d %d) (%d/%d %d/%d)\n", 
		numTotalClickables, numTotalNestedZero, numTotalNestedOne, numTotalNestedMore,
		numTotalMultiName, numTotalMultiNameCount, numTotalMultiNameNested, numTotalMultiNameNestedCount);

	if (eaSize(&eaOrphanLayers)) {
		fprintf(file, "\n");
		fprintf(file, "Layers with Orphan Clickables: (%d)\n", eaSize(&eaOrphanLayers));
		for(i=eaSize(&eaOrphanLayers)-1; i>=0; --i) {
			fprintf(file, "  Orphan Layer: %s\n", eaOrphanLayers[i]->pchFilename);
		}
		//eaDestroy(&eaOrphanLayers);
	}

	fprintf(file, "\nScanning for event users:\n");
	ScanExpressionsForClickableNameEvents(eaChangeInfos, peaEncLayers, file);

	if (eaSize(&eaChangeInfos)) {
		fprintf(file, "\n");
		fprintf(file, "Change to Group: (%d)\n", eaSize(&eaChangeInfos));
		for(i=eaSize(&eaChangeInfos)-1; i>=0; --i) {
			fprintf(file, "  Change to Group: %s (%s)\n", eaChangeInfos[i]->pcGroupName, eaChangeInfos[i]->pcMapName);
		}
		//eaDestroyEx(&eaChangeInfos, NULL);
	}

	if (eaSize(&eaCooldownInfos)) {
		fprintf(file, "\n");
		fprintf(file, "Cooldowns: (%d)\n", eaSize(&eaCooldownInfos));
		for(i=eaSize(&eaCooldownInfos)-1; i>=0; --i) {
			fprintf(file, "  Cooldown: %d = %d times\n", eaCooldownInfos[i]->value, eaCooldownInfos[i]->count);
		}
		//eaDestroyEx(&eaCooldownInfos, NULL);
	}
}

#endif

static void ConvertPatrolRoutesInternal(EncounterLayer ***peaEncLayers, EncounterLayer ***peaModifiedEncLayers, EncounterLayer ***peaOrigEncLayers, LibFileLoad ***peaGeoLayers, LibFileLoad ***peaModifiedGeoLayers, char ***peaExtraFiles, FILE *file)
{
	LibFileLoad *pGeoLayer  = NULL;
	GroupDef *pDefLoad;
	GroupDef *pContainerDefLoad = NULL;
	GroupChild *pGroupLoad;
	GroupChild *pContainerGroup = NULL;
	ScopeTableLoad *pScopeLoad;
	WorldPatrolProperties *pWorldRoute;
	char buf[1024];
	char nameBuf[1024];
	int i,j,k;
	bool bLayerAltered;
	int numLayers = 0;
	int numRoutes = 0;
	int next_id = 0;
	int name_id = 0;
	char *pcName;
	Vec3 vZero = {0,0,0};

	fprintf(file, "==============================================================\n");
	fprintf(file, "Patrol Route Conversion\n");
	fprintf(file, "==============================================================\n");

	// Scan layers
	for(i=eaSize(peaEncLayers)-1; i>=0; --i) {
		EncounterLayer *pEncLayer = (*peaEncLayers)[i];
		bLayerAltered = false;

		// Scan spawn points
		for(j=eaSize(&pEncLayer->oldNamedRoutes)-1; j>=0; --j) {
			OldPatrolRoute *pEncRoute = pEncLayer->oldNamedRoutes[j];

			if (!bLayerAltered) {
				// When layer is first altered get Geo Layer and put these layers on the modified lists
				if (eaFind(peaModifiedEncLayers, pEncLayer) == -1) {
					eaPush(peaModifiedEncLayers, pEncLayer);
					eaPush(peaOrigEncLayers, StructClone(parse_EncounterLayer, pEncLayer));
				}
				pGeoLayer = Convert_GetGeoLayerForEncLayer(pEncLayer, vZero, peaGeoLayers, peaExtraFiles, NULL);
				eaPushUnique(peaModifiedGeoLayers, pGeoLayer);
				next_id = Convert_GetNextIdForGeoLayer(pGeoLayer);

				// Create the group data for the container
				pContainerGroup = StructCreate(parse_GroupChild);
				pContainerGroup->name = allocAddString("Patrol Routes");
				pContainerGroup->name_uid = next_id++;
				pContainerGroup->uid_in_parent = next_id++;
				pContainerGroup->seed = 0; // TODO: WHAT IS SEED?
				eaPush(&pGeoLayer->defs[0]->children, pContainerGroup);

				// Create DefLoad for the container
				pContainerDefLoad = StructCreate(parse_GroupDef);
				pContainerDefLoad->name_str = allocAddString("Patrol Routes");
				pContainerDefLoad->name_uid = pContainerGroup->name_uid;
				eaPush(&pGeoLayer->defs, pContainerDefLoad);

				// Also collect stats
				fprintf(file, "\n");
				fprintf(file, "ENC-LAYER %s\n", pEncLayer->pchFilename);
				fprintf(file, "GEO-LAYER %s\n", pGeoLayer->defs[0]->filename);
				bLayerAltered = true;
				++numLayers;
			}
			assert(pGeoLayer);

			// Print and collect stats
			fprintf(file, "  Patrol Route: %s\n", pEncRoute->routeName);
			++numRoutes;

			// Remove the patrol route from the layer
			eaRemove(&pEncLayer->oldNamedRoutes, j);

			pcName = pEncRoute->routeName;
			if (!pcName || !pcName[0]) {
				sprintf(nameBuf, "UNNAMED_PatrolRoute");
				pcName = nameBuf;
			}

			// Create the group data
			pGroupLoad = StructCreate(parse_GroupChild);
			pGroupLoad->name = StructAllocString(pcName);
			pGroupLoad->name_uid = next_id++;
			pGroupLoad->uid_in_parent = next_id++;
			pGroupLoad->seed = 0;
			copyVec3(pEncRoute->patrolPoints[0]->pointLoc, pGroupLoad->pos);
			setVec3(pGroupLoad->rot, 0, 0, 0);
			eaPush(&pContainerDefLoad->children, pGroupLoad);

			pScopeLoad = StructCreate(parse_ScopeTableLoad);
			pScopeLoad->name = StructAllocString(pcName);
			sprintf(buf, "%d,%d,", pContainerGroup->uid_in_parent, pGroupLoad->uid_in_parent);
			pScopeLoad->path = StructAllocString(buf);
			eaPush(&pGeoLayer->defs[0]->scope_entries_load, pScopeLoad);

			// Create the Geo patrol route
			pWorldRoute = StructCreate(parse_WorldPatrolProperties);
			switch(pEncRoute->routeType)
			{
				xcase OldPatrolRouteType_Circle:   pWorldRoute->route_type = PATROL_CIRCLE;
				xcase OldPatrolRouteType_PingPong: pWorldRoute->route_type = PATROL_PINGPONG;
				xcase OldPatrolRouteType_OneWay:   pWorldRoute->route_type = PATROL_ONEWAY;
			}
			for(k=0; k<eaSize(&pEncRoute->patrolPoints); ++k) {
				WorldPatrolPointProperties *pPoint = StructCreate(parse_WorldPatrolPointProperties);
				subVec3(pEncRoute->patrolPoints[k]->pointLoc, pGroupLoad->pos, pPoint->pos);
				eaPush(&pWorldRoute->patrol_points, pPoint);
			}

			// Create DefLoad
			pDefLoad = StructCreate(parse_GroupDef);
			pDefLoad->name_str = allocAddString(pcName);
			pDefLoad->name_uid = pGroupLoad->name_uid;
			pDefLoad->model_name = StructAllocString("core_icons_objective");
			pDefLoad->property_structs.patrol_properties = pWorldRoute;
			pDefLoad->property_structs.physical_properties.bVisible = 0;
			eaPush(&pGeoLayer->defs, pDefLoad);
		}
	}

	fprintf(file, "\n");
	fprintf(file, "%d Patrol Routes in %d Layers\n", numRoutes, numLayers);
	fprintf(file, "\n");
}

#ifdef SDANGELO_CODE_TO_REMOVE_WHEN_DONE_WITH_ENCOUNTER_LAYER_CONVERSION

static void ConvertRanksOnEnc(EncounterLayer *pLayer, StaticEncounter *pEnc, EncounterLayer ***peaModifiedEncLayers, EncounterLayer ***peaOrigEncLayers, FILE *file)
{
	bool bChanged = false;

	if (pEnc->defOverride) {
		EncounterDef *pDef = pEnc->defOverride;
		bool bIsShipGroup = false;
		int i;

		// Figure out type of root
		if (REF_STRING_FROM_HANDLE(pDef->critterGroup)) {
			if (strstri(REF_STRING_FROM_HANDLE(pDef->critterGroup), "_Ship")) {
				bIsShipGroup = true;
			}
		} else {
			EncounterDef *pBaseDef = GET_REF(pEnc->baseDef);
			if (pBaseDef && REF_STRING_FROM_HANDLE(pBaseDef->critterGroup)) {
				if (strstri(REF_STRING_FROM_HANDLE(pBaseDef->critterGroup), "_Ship")) {
					bIsShipGroup = true;
				}
			}
		}

		// Iterate actors
		for(i=eaSize(&pDef->actors)-1; i>=0; --i) {
			Actor *pActor = pDef->actors[i];

			if (pActor->details.info) {
				const char *pcRank = NULL;
				const char *pcSubRank = NULL;
				bool bIsShip = bIsShipGroup;

				if (GET_REF(pActor->details.info->critterDef)) {
					continue;
				}
				if (REF_STRING_FROM_HANDLE(pActor->details.info->critterGroup)) {
					const char *pcName = REF_STRING_FROM_HANDLE(pActor->details.info->critterGroup);
					if (pcName && strstri(pcName, "_Ship")) {
						bIsShip = true;
					}
				}

				pcRank = pActor->details.info->pcCritterRank;
				pcSubRank = pActor->details.info->pcCritterSubRank;

				if (bIsShip) {
					if (!pcRank) {
						pcRank = "Fighter";
						bChanged = true;
					} else if (stricmp("Henchman", pcRank) == 0) {
						pcRank = "Fighter";
						bChanged = true;
					} else if (stricmp("Villain", pcRank) == 0) {
						pcRank = "Frigate";
						bChanged = true;
					} else if (stricmp("MasterVillain", pcRank) == 0) {
						pcRank = "Cruiser";
						bChanged = true;
					} else if (stricmp("SuperVillain", pcRank) == 0) {
						pcRank = "Battleship";
						bChanged = true;
					} else if (stricmp("Legendary", pcRank) == 0) {
						pcRank = "Dreadnought";
						bChanged = true;
					} else if (stricmp("Cosmic", pcRank) == 0) {
						pcRank = "Capital";
						bChanged = true;
					}
				} else {
					if (!pcRank) {
						pcRank = "Neophyte";
						bChanged = true;
					} else if (stricmp("Henchman", pcRank) == 0) {
						pcRank = "Neophyte";
						bChanged = true;
					} else if (stricmp("Villain", pcRank) == 0) {
						pcRank = "Ensign";
						bChanged = true;
					} else if (stricmp("MasterVillain", pcRank) == 0) {
						pcRank = "Lieutenant";
						bChanged = true;
					} else if (stricmp("SuperVillain", pcRank) == 0) {
						pcRank = "Commander";
						bChanged = true;
					} else if (stricmp("Legendary", pcRank) == 0) {
						pcRank = "Captain";
						bChanged = true;
					} else if (stricmp("Cosmic", pcRank) == 0) {
						pcRank = "Monster";
						bChanged = true;
					}
				}

				if (!pcSubRank) {
					pcSubRank = "Normal";
					bChanged = true;
				}

				if (bChanged) {
					if (eaFind(peaModifiedEncLayers, pLayer) < 0) {
						eaPush(peaOrigEncLayers, StructClone(parse_EncounterLayer, pLayer));
						eaPush(peaModifiedEncLayers, pLayer);
						printf("** Changed: %s\n", pLayer->pchFilename);
					}

					pActor->details.info->pcCritterRank = allocAddString(pcRank);
					pActor->details.info->pcCritterSubRank = allocAddString(pcSubRank);
				}
			}
		}
	}

}


static void ConvertRanksOnDef(EncounterDef *pDef, EncounterDef ***peaModifiedDefs, FILE *file)
{
	bool bChanged = false;

	bool bIsShipGroup = false;
	int i;

	// Figure out type of root
	if (REF_STRING_FROM_HANDLE(pDef->critterGroup)) {
		const char *pcName = REF_STRING_FROM_HANDLE(pDef->critterGroup);
		if (pcName && strstri(pcName, "_Ship")) {
			bIsShipGroup = true;
		}
	}

	// Iterate actors
	for(i=eaSize(&pDef->actors)-1; i>=0; --i) {
		Actor *pActor = pDef->actors[i];

		if (pActor->details.info) {
			const char *pcRank = NULL;
			const char *pcSubRank = NULL;
			bool bIsShip = bIsShipGroup;

			if (GET_REF(pActor->details.info->critterDef)) {
				continue;
			}
			if (REF_STRING_FROM_HANDLE(pActor->details.info->critterGroup)) {
				const char *pcName = REF_STRING_FROM_HANDLE(pActor->details.info->critterGroup);
				if (pcName && strstri(pcName, "_Ship")) {
					bIsShip = true;
				}
			}

			pcRank = pActor->details.info->pcCritterRank;
			pcSubRank = pActor->details.info->pcCritterSubRank;

			if (bIsShip) {
				if (!pcRank) {
					pcRank = "Fighter";
					bChanged = true;
				} else if (stricmp("Henchman", pcRank) == 0) {
					pcRank = "Fighter";
					bChanged = true;
				} else if (stricmp("Villain", pcRank) == 0) {
					pcRank = "Frigate";
					bChanged = true;
				} else if (stricmp("MasterVillain", pcRank) == 0) {
					pcRank = "Cruiser";
					bChanged = true;
				} else if (stricmp("SuperVillain", pcRank) == 0) {
					pcRank = "Battleship";
					bChanged = true;
				} else if (stricmp("Legendary", pcRank) == 0) {
					pcRank = "Dreadnought";
					bChanged = true;
				} else if (stricmp("Cosmic", pcRank) == 0) {
					pcRank = "Capital";
					bChanged = true;
				}
			} else {
				if (!pcRank) {
					pcRank = "Neophyte";
					bChanged = true;
				} else if (stricmp("Henchman", pcRank) == 0) {
					pcRank = "Neophyte";
					bChanged = true;
				} else if (stricmp("Villain", pcRank) == 0) {
					pcRank = "Ensign";
					bChanged = true;
				} else if (stricmp("MasterVillain", pcRank) == 0) {
					pcRank = "Lieutenant";
					bChanged = true;
				} else if (stricmp("SuperVillain", pcRank) == 0) {
					pcRank = "Commander";
					bChanged = true;
				} else if (stricmp("Legendary", pcRank) == 0) {
					pcRank = "Captain";
					bChanged = true;
				} else if (stricmp("Cosmic", pcRank) == 0) {
					pcRank = "Monster";
					bChanged = true;
				}
			}

			if (!pcSubRank) {
				pcSubRank = "Normal";
				bChanged = true;
			}

			if (bChanged) {
				if (eaFind(peaModifiedDefs, pDef) < 0) {
					printf("** Changed: %s (%s)\n", pDef->name, pDef->filename);
					pDef = StructClone(parse_EncounterDef, pDef);
					assert(pDef);
					pActor = pDef->actors[i];
					eaPush(peaModifiedDefs, pDef);
				}
				pActor->details.info->pcCritterRank = allocAddString(pcRank);
				pActor->details.info->pcCritterSubRank = allocAddString(pcSubRank);
			}
		}
	}

}


static void ConvertRanksOnTemplate(int iPartitionIdx, EncounterTemplate *pTemplate, EncounterTemplate ***peaModifiedTemplates, FILE *file)
{
	bool bChanged = false;

	bool bIsShipGroup = false;
	int i;

	// Figure out type of root
	if (encounterTemplate_GetCritterGroup(pTemplate, iPartitionIdx)) {
		const char *pcName = encounterTemplate_GetCritterGroup(pTemplate, iPartitionIdx)->pchName;
		if (pcName && strstri(pcName, "_Ship")) {
			bIsShipGroup = true;
		}
	}

	// Iterate actors
	for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
		EncounterActorProperties *pActor = pTemplate->eaActors[i];

		if (pActor) {
			const char *pcRank = NULL;
			const char *pcSubRank = NULL;
			bool bIsShip = bIsShipGroup;
			bool bNoGroup = false;

			if (encounterTemplate_GetActorCritterDef(pTemplate, pActor)) {
				continue;
			}
			if (encounterTemplate_GetActorCritterGroup(pTemplate, pActor, iPartitionIdx)) {
				const char *pcName = encounterTemplate_GetActorCritterGroup(pTemplate, pActor, iPartitionIdx)->pchName;
				if (pcName && strstri(pcName, "_Ship")) {
					bIsShip = true;
				}
			}

			pcRank = encounterTemplate_GetActorRank(pTemplate, pActor);
			pcSubRank = encounterTemplate_GetActorSubRank(pTemplate, pActor);

			if (bIsShip) {
				if (!pcRank) {
					pcRank = "Fighter";
					bChanged = true;
				} else if (stricmp("Henchman", pcRank) == 0) {
					pcRank = "Fighter";
					bChanged = true;
				} else if (stricmp("Villain", pcRank) == 0) {
					pcRank = "Frigate";
					bChanged = true;
				} else if (stricmp("MasterVillain", pcRank) == 0) {
					pcRank = "Cruiser";
					bChanged = true;
				} else if (stricmp("SuperVillain", pcRank) == 0) {
					pcRank = "Battleship";
					bChanged = true;
				} else if (stricmp("Legendary", pcRank) == 0) {
					pcRank = "Dreadnought";
					bChanged = true;
				} else if (stricmp("Cosmic", pcRank) == 0) {
					pcRank = "Capital";
					bChanged = true;
				}
			} else {
				if (!pcRank) {
					pcRank = "Neophyte";
					bChanged = true;
				} else if (stricmp("Henchman", pcRank) == 0) {
					pcRank = "Neophyte";
					bChanged = true;
				} else if (stricmp("Villain", pcRank) == 0) {
					pcRank = "Ensign";
					bChanged = true;
				} else if (stricmp("MasterVillain", pcRank) == 0) {
					pcRank = "Lieutenant";
					bChanged = true;
				} else if (stricmp("SuperVillain", pcRank) == 0) {
					pcRank = "Commander";
					bChanged = true;
				} else if (stricmp("Legendary", pcRank) == 0) {
					pcRank = "Captain";
					bChanged = true;
				} else if (stricmp("Cosmic", pcRank) == 0) {
					pcRank = "Monster";
					bChanged = true;
				}
			}

			if (!pcSubRank) {
				pcSubRank = "Normal";
				bChanged = true;
			}

			if (bChanged) {
				if (eaFind(peaModifiedTemplates, pTemplate) < 0) {
					printf("** Changed: %s (%s)\n", pTemplate->pcName, pTemplate->pcFilename);
					pTemplate = StructClone(parse_EncounterTemplate, pTemplate);
					assert(pTemplate);
					pActor = pTemplate->eaActors[i];
					eaPush(peaModifiedTemplates, pTemplate);
				}
				pActor->pcRank = allocAddString(pcRank);
				pActor->pcSubRank = allocAddString(pcSubRank);
			}
		}
	}

}


static void ConvertRanksOnCritter(CritterDef *pCritter, CritterDef ***peaModifiedCritters, FILE *file)
{
	bool bChanged = false;
	const char *pcRank = NULL;
	const char *pcSubRank = NULL;
	bool bIsShip = false;
	bool bIsShipGroup = false;

	// Figure out type of root
	if (GET_REF(pCritter->hGroup)) {
		const char *pcName = GET_REF(pCritter->hGroup)->pchName;
		if (pcName && strstri(pcName, "_Ship")) {
			bIsShipGroup = true;
		}
	}
	if (pCritter->pchClass) {
		if ((stricmp(pCritter->pchClass, "default") == 0) ||
			(stricmp(pCritter->pchClass, "starfleet_tactical") == 0) ||
			(stricmp(pCritter->pchClass, "starfleet_science") == 0) ||
			(stricmp(pCritter->pchClass, "starfleet_engineering") == 0) ||
			(stricmp(pCritter->pchClass, "bridge_officer_tactical") == 0) ||
			(stricmp(pCritter->pchClass, "bridge_officer_science") == 0) ||
			(stricmp(pCritter->pchClass, "bridge_officer_engineering") == 0) ||
			(stricmp(pCritter->pchClass, "away_team_redshirt") == 0) ||
			(stricmp(pCritter->pchClass, "critter_neophyte") == 0) ||
			(stricmp(pCritter->pchClass, "critter_ensign") == 0) ||
			(stricmp(pCritter->pchClass, "critter_lieutenant") == 0) ||
			(stricmp(pCritter->pchClass, "critter_commander") == 0) ||
			(stricmp(pCritter->pchClass, "critter_captain") == 0) ||
			(stricmp(pCritter->pchClass, "critter_monster") == 0) ||
			(stricmp(pCritter->pchClass, "safeguard_ground") == 0)
			) {
			bIsShip = false;
		} 
		else 
		if ((stricmp(pCritter->pchClass, "ship_tutorial") == 0) ||
			(stricmp(pCritter->pchClass, "lt_cruiser") == 0) ||
			(stricmp(pCritter->pchClass, "cruiser1") == 0) ||
			(stricmp(pCritter->pchClass, "escort1") == 0) ||
			(stricmp(pCritter->pchClass, "sciencevessel1") == 0) ||
			(stricmp(pCritter->pchClass, "cruiser2") == 0) ||
			(stricmp(pCritter->pchClass, "sciencevessel2") == 0) ||
			(stricmp(pCritter->pchClass, "cruiser3") == 0) ||
			(stricmp(pCritter->pchClass, "escort3") == 0) ||
			(stricmp(pCritter->pchClass, "sciencevessel3") == 0) ||
			(stricmp(pCritter->pchClass, "cruiser4") == 0) ||
			(stricmp(pCritter->pchClass, "escort4") == 0) ||
			(stricmp(pCritter->pchClass, "sciencevessel4") == 0) ||
			(stricmp(pCritter->pchClass, "cruiser5") == 0) ||
			(stricmp(pCritter->pchClass, "escort5") == 0) ||
			(stricmp(pCritter->pchClass, "escort4") == 0) ||
			(stricmp(pCritter->pchClass, "crittership_fighter") == 0) ||
			(stricmp(pCritter->pchClass, "crittership_runabout") == 0) ||
			(stricmp(pCritter->pchClass, "crittership_frigate") == 0) ||
			(stricmp(pCritter->pchClass, "crittership_raider") == 0) ||
			(stricmp(pCritter->pchClass, "crittership_cruiser") == 0) ||
			(stricmp(pCritter->pchClass, "crittership_escort") == 0) ||
			(stricmp(pCritter->pchClass, "crittership_science") == 0) ||
			(stricmp(pCritter->pchClass, "crittership_carrier") == 0) ||
			(stricmp(pCritter->pchClass, "crittership_battleship") == 0) ||
			(stricmp(pCritter->pchClass, "crittership_dreadnought") == 0) ||
			(stricmp(pCritter->pchClass, "crittership_capitol") == 0) ||
			(stricmp(pCritter->pchClass, "boarding_party_shuttle") == 0)
			){
			bIsShip = true;
		} else if (stricmp(pCritter->pchClass, "object_destructible_1hp") == 0) {
			if ((stricmp(pCritter->pchName, "Ground_Engineering_Force_Field_Generator_Critter") == 0) ||
				(stricmp(pCritter->pchName, "Plasma_Charge") == 0)) {
				bIsShip = false;
			} else {
				bIsShip = true;
			}
		} else {
			printf("** Unexpected class '%s' on '%s\n", pCritter->pchClass, pCritter->pchFileName);
			return;
		}
	}

	pcRank = pCritter->pcRank;

	if (bIsShip) {
		if (!pcRank) {
			pcRank = "Fighter";
			bChanged = true;
		} else if (stricmp("Henchman", pcRank) == 0) {
			pcRank = "Fighter";
			bChanged = true;
		} else if (stricmp("Villain", pcRank) == 0) {
			pcRank = "Frigate";
			bChanged = true;
		} else if (stricmp("MasterVillain", pcRank) == 0) {
			pcRank = "Cruiser";
			bChanged = true;
		} else if (stricmp("SuperVillain", pcRank) == 0) {
			pcRank = "Battleship";
			bChanged = true;
		} else if (stricmp("Legendary", pcRank) == 0) {
			pcRank = "Dreadnought";
			bChanged = true;
		} else if (stricmp("Cosmic", pcRank) == 0) {
			pcRank = "Capital";
			bChanged = true;
		}
	} else {
		if (!pcRank) {
			pcRank = "Neophyte";
			bChanged = true;
		} else if (stricmp("Henchman", pcRank) == 0) {
			pcRank = "Neophyte";
			bChanged = true;
		} else if (stricmp("Villain", pcRank) == 0) {
			pcRank = "Ensign";
			bChanged = true;
		} else if (stricmp("MasterVillain", pcRank) == 0) {
			pcRank = "Lieutenant";
			bChanged = true;
		} else if (stricmp("SuperVillain", pcRank) == 0) {
			pcRank = "Commander";
			bChanged = true;
		} else if (stricmp("Legendary", pcRank) == 0) {
			pcRank = "Captain";
			bChanged = true;
		} else if (stricmp("Cosmic", pcRank) == 0) {
			pcRank = "Monster";
			bChanged = true;
		}
	}

	if (bChanged) {
		if (eaFind(peaModifiedCritters, pCritter) < 0) {
			printf("** Changed: %s (%s)\n", pCritter->pchName, pCritter->pchFileName);
			pCritter = StructClone(parse_CritterDef, pCritter);
			assert(pCritter);
			eaPush(peaModifiedCritters, pCritter);
		}
		pCritter->pcRank = allocAddString(pcRank);

		if (pCritter->pInheritance && StructInherit_GetOverrideType(parse_CritterDef, pCritter, ".rank") == OVERRIDE_SET) {
			CritterDef *pParent = RefSystem_ReferentFromString("CritterDef", StructInherit_GetParentName(parse_CritterDef, pCritter));
			if (pParent && (pParent->pcRank == pCritter->pcRank)) {
				StructInherit_DestroyOverride(parse_CritterDef, pCritter, ".rank");
			} else {
				StructInherit_UpdateFromStruct(parse_CritterDef, pCritter, false);
			}
		}
	}

}


static void ConvertRanksOnGroup(EncounterLayer *pLayer, StaticEncounterGroup *pGroup, EncounterLayer ***peaModifiedEncLayers, EncounterLayer ***peaOrigEncLayers, FILE *file)
{
	int i;

	for(i=eaSize(&pGroup->staticEncList)-1; i>=0; --i) {
		ConvertRanksOnEnc(pLayer, pGroup->staticEncList[i], peaModifiedEncLayers, peaOrigEncLayers, file);
	}

	for(i=eaSize(&pGroup->childList)-1; i>=0; --i) {
		ConvertRanksOnGroup(pLayer, pGroup->childList[i], peaModifiedEncLayers, peaOrigEncLayers, file);
	}
}


static void ConvertRanks(int iPartitionIdx, EncounterLayer ***peaEncLayers, EncounterLayer ***peaModifiedEncLayers, EncounterLayer ***peaOrigEncLayers, FILE *file)
{
	DictionaryEArrayStruct *pEncounters = resDictGetEArrayStruct("EncounterDef");
	DictionaryEArrayStruct *pTemplates = resDictGetEArrayStruct("EncounterTemplate");
	DictionaryEArrayStruct *pCritters = resDictGetEArrayStruct("CritterDef");
	EncounterDef **eaModifiedDefs = NULL;
	EncounterTemplate **eaModifiedTemplates = NULL;
	CritterDef **eaModifiedCritters = NULL;
	int i;
	ResourceLoaderStruct loadStruct = {0};

	for(i=eaSize(peaEncLayers)-1; i>=0; --i) {
		EncounterLayer *pLayer = (*peaEncLayers)[i];
		ConvertRanksOnGroup(pLayer, &pLayer->rootGroup, peaModifiedEncLayers, peaOrigEncLayers, file);
	}

	for(i=eaSize(&pEncounters->ppReferents)-1; i>=0; --i) {
		EncounterDef *pDef = pEncounters->ppReferents[i];
		ConvertRanksOnDef(pDef, &eaModifiedDefs, file);
	}

	for(i=eaSize(&pTemplates->ppReferents)-1; i>=0; --i) {
		EncounterTemplate *pTemplate = pTemplates->ppReferents[i];
		ConvertRanksOnTemplate(iPartitionIdx, pTemplate, &eaModifiedTemplates, file);
	}

	for(i=eaSize(&pCritters->ppReferents)-1; i>=0; --i) {
		CritterDef *pDef = pCritters->ppReferents[i];
		ConvertRanksOnCritter(pDef, &eaModifiedCritters, file);
	}

	eaPush(&loadStruct.earrayOfStructs, NULL);

	SetUpResourceLoaderParse("EncounterDef", ParserGetTableSize(parse_EncounterDef), parse_EncounterDef, NULL);
	for(i=eaSize(&eaModifiedDefs)-1; i>=0; --i) {
		loadStruct.earrayOfStructs[0] = eaModifiedDefs[i];
		ParserWriteTextFile(eaModifiedDefs[i]->filename, parse_ResourceLoaderStruct, &loadStruct, 0, 0);
	}
	parse_ResourceLoaderStruct[0].name = NULL;

	SetUpResourceLoaderParse("EncounterTemplate", ParserGetTableSize(parse_EncounterTemplate), parse_EncounterTemplate, NULL);
	for(i=eaSize(&eaModifiedTemplates)-1; i>=0; --i) {
		loadStruct.earrayOfStructs[0] = eaModifiedTemplates[i];
		ParserWriteTextFile(eaModifiedTemplates[i]->pcFilename, parse_ResourceLoaderStruct, &loadStruct, 0, 0);
	}
	parse_ResourceLoaderStruct[0].name = NULL;

	SetUpResourceLoaderParse("CritterDef", ParserGetTableSize(parse_CritterDef), parse_CritterDef, NULL);
	for(i=eaSize(&eaModifiedCritters)-1; i>=0; --i) {
		loadStruct.earrayOfStructs[0] = eaModifiedCritters[i];
		ParserWriteTextFile(eaModifiedCritters[i]->pchFileName, parse_ResourceLoaderStruct, &loadStruct, 0, 0);
	}
	parse_ResourceLoaderStruct[0].name = NULL;
}

#endif


static int Convert_CompareStrings(const char** left, const char** right)
{
	return stricmp(*left,*right);
}


typedef struct TemplateAIActor {
	FSM *pFSM;
	WorldVariableDef **eaVarDefs;
} TemplateAIActor;

typedef struct TemplateAI {
	TemplateAIActor **eaActors;
} TemplateAI;

typedef struct EncTemplateData {
	const char *pcName;
	const char *pcScope;

	EncounterTemplate *pTemplate;
	TemplateAI *pTemplateAI;

	EncounterDef **eaSourceDefs;
	EncounterLayer **eaEncLayers;
	EncounterDef **eaLayerDefs;
	bool bPreExisting;
	bool bProcessed;
} EncTemplateData;

typedef struct EncConvertData {
	EncounterLayer **eaEncLayers;
	EncounterDef **eaSourceDefs;
	EncTemplateData **eaTemplateData;
	const char* pchLevelMapVar;
	bool bUsePlayerLevel;
} EncConvertData;

static bool ConvertHasOldInteractProps(OldInteractionProperties *pProps)
{
	if (pProps->interactCond ||
		pProps->interactSuccessCond ||
		pProps->interactAction ||
		(pProps->interactGameActions && eaSize(&pProps->interactGameActions->eaActions)) ||
		GET_REF(pProps->interactText.hMessage) ||
		GET_REF(pProps->interactFailedText.hMessage) ||
		pProps->uInteractTime ||
		pProps->uInteractActiveFor ||
		pProps->uInteractCoolDown ||
		GET_REF(pProps->hInteractAnim)) {
		return true;
	}
	return false;
}

static bool ConvertIsTemplateNameUnique(EncConvertData *pConvertData, EncounterTemplate *pNewTemplate)
{
	DictionaryEArrayStruct *pTemplates = resDictGetEArrayStruct("EncounterTemplate");
	int i;

	for(i=eaSize(&pConvertData->eaTemplateData)-1; i>=0; --i) {
		EncTemplateData *pOtherData = pConvertData->eaTemplateData[i];
		if ((pNewTemplate != pOtherData->pTemplate) &&
			pOtherData->pTemplate->pcName && 
			(stricmp(pOtherData->pTemplate->pcName, pNewTemplate->pcName) == 0)) {
			return false;
		}
	}

	for(i=eaSize(&pTemplates->ppReferents)-1; i>=0; --i) {
		EncounterTemplate *pDictTemplate = pTemplates->ppReferents[i];
		if (stricmp(pDictTemplate->pcName, pNewTemplate->pcName) == 0) {
			return false;
		}
	}

	return true;
}

static TemplateAI *ConvertCloneTemplateAI(TemplateAI *pTemplateAI)
{
	TemplateAI *pResult;
	int i, j;

	if (!pTemplateAI) {
		return NULL;
	}

	pResult = calloc(1,sizeof(TemplateAI));

	for(i=eaSize(&pTemplateAI->eaActors)-1; i>=0; --i) {
		TemplateAIActor *pAIActor = pTemplateAI->eaActors[i];
		TemplateAIActor *pResultActor = calloc(1,sizeof(TemplateAIActor));

		pResultActor->pFSM = pAIActor->pFSM;

		for(j=0; j<eaSize(&pAIActor->eaVarDefs); ++j) {
			eaPush(&pResultActor->eaVarDefs, StructClone(parse_WorldVariableDef, pAIActor->eaVarDefs[j]));
		}

		eaPush(&pResult->eaActors, pResultActor);
	}
	
	return pResult;
}


static void ConvertDestroyTemplateAI(TemplateAI *pTemplateAI)
{
	int i;

	if (!pTemplateAI) {
		return;
	}

	for(i=eaSize(&pTemplateAI->eaActors)-1; i>=0; --i) {
		TemplateAIActor *pAIActor = pTemplateAI->eaActors[i];
		eaDestroyStruct(&pAIActor->eaVarDefs, parse_WorldVariableDef);
		free(pAIActor);
	}
	free(pTemplateAI);
}


static void ConvertExtractTemplateAI(EncounterTemplate *pTemplate, TemplateAI **ppTemplateAI)
{
	TemplateAI *pTemplateAI = calloc(1,sizeof(TemplateAI));
	int i;

	for(i=0; i<eaSize(&pTemplate->eaActors); ++i) {
		EncounterActorProperties *pActor = pTemplate->eaActors[i];
		TemplateAIActor *pAIActor = calloc(1,sizeof(TemplateAIActor));
		
		// Copy out actor data
		pAIActor->pFSM = GET_REF(pActor->fsmProps.hFSM);
		pAIActor->eaVarDefs = pActor->eaVariableDefs;

		// Clean up actor
		pActor->fsmProps.eFSMType = EncounterTemplateOverrideType_FromTemplate;
		REMOVE_HANDLE(pActor->fsmProps.hFSM);
		pActor->eaVariableDefs = NULL;

		eaPush(&pTemplateAI->eaActors, pAIActor);
	}
	
	*ppTemplateAI = pTemplateAI;
}


static void ConvertApplyTemplateAI(EncounterTemplate *pTemplate, TemplateAI *pTemplateAI)
{
	int i, j;

	for(i=0; i<eaSize(&pTemplate->eaActors); ++i) {
		EncounterActorProperties *pActor = pTemplate->eaActors[i];
		TemplateAIActor *pAIActor = pTemplateAI->eaActors[i];

		if (pAIActor->pFSM) {
			pActor->fsmProps.eFSMType = EncounterTemplateOverrideType_Specified;
			SET_HANDLE_FROM_REFERENT("FSM", pAIActor->pFSM, pActor->fsmProps.hFSM);
		}

		for(j=0; j<eaSize(&pAIActor->eaVarDefs); ++j) {
			eaPush(&pActor->eaVariableDefs, StructClone(parse_WorldVariableDef, pAIActor->eaVarDefs[j]));
		}
	}
}


static void ConvertFindCommonTemplateAI(TemplateAI *pLeft, TemplateAI *pRight, TemplateAI **ppCommon)
{
	TemplateAI *pCommon = calloc(1,sizeof(TemplateAI));
	int i, j, k;

	for(i=0; i<eaSize(&pLeft->eaActors); ++i) {
		TemplateAIActor *pLeftActor = pLeft->eaActors[i];
		TemplateAIActor *pRightActor = pRight->eaActors[i];
		TemplateAIActor *pCommonActor = calloc(1,sizeof(TemplateAIActor));

		if (pLeftActor->pFSM == pRightActor->pFSM) {
			pCommonActor->pFSM = pLeftActor->pFSM;
		} else if (pLeftActor->pFSM && pRightActor->pFSM) {
			pCommonActor->pFSM = pLeftActor->pFSM;
		}

		for(j=0; j<eaSize(&pLeftActor->eaVarDefs); ++j) {
			WorldVariableDef *pLeftDef = pLeftActor->eaVarDefs[j];

			for(k=eaSize(&pRightActor->eaVarDefs)-1; k>=0; --k) {
				WorldVariableDef *pRightDef = pRightActor->eaVarDefs[k];

				if (pLeftDef->pcName && pRightDef->pcName && (stricmp(pLeftDef->pcName, pRightDef->pcName) == 0)) {
					break;
				}
			}
			if (k >=0) {
				eaPush(&pCommonActor->eaVarDefs, StructClone(parse_WorldVariableDef, pLeftDef));
			}
		}

		eaPush(&pCommon->eaActors, pCommonActor);
	}

	*ppCommon = pCommon;
}


static bool ConvertIsCompatibleTemplateAI(TemplateAI *pBase, TemplateAI *pFinal)
{
	int i, j, k;

	for(i=0; i<eaSize(&pBase->eaActors); ++i) {
		TemplateAIActor *pBaseActor = pBase->eaActors[i];
		TemplateAIActor *pFinalActor = pFinal->eaActors[i];

		for(j=0; j<eaSize(&pBaseActor->eaVarDefs); ++j) {
			WorldVariableDef *pBaseDef = pBaseActor->eaVarDefs[j];

			for(k=eaSize(&pFinalActor->eaVarDefs)-1; k>=0; --k) {
				WorldVariableDef *pFinalDef = pFinalActor->eaVarDefs[k];
				if (pBaseDef->pcName && pFinalDef->pcName && (stricmp(pBaseDef->pcName, pFinalDef->pcName) == 0)) {
					break;
				}
			}
			if (k < 0) {
				// Not found on actor, so see if it's simply not cared about by FSM
				FSM *pFSM = pFinalActor->pFSM;
				int n;
				bool bOkay = false;

				if (pFSM) {
					FSMExternVar **eaFSMVarDefs = NULL;
					fsmGetExternVarNamesRecursive( pFSM, &eaFSMVarDefs, "Encounter" );
					for(n=eaSize(&eaFSMVarDefs)-1; n>=0; --n) {
						FSMExternVar *pFinalDef = eaFSMVarDefs[n];
						if (pFinalDef->name && stricmp(pFinalDef->name, pBaseDef->pcName) == 0) {
							break;
						}
					}
					if (n < 0) {
						// FSM doesn't care about var so it's okay
						bOkay = true;
					}
					eaDestroy(&eaFSMVarDefs);
				}
				if (!bOkay) {
					// Not compatible because base has a variable that the final does not have
					return false;
				}
			}
		}
	}

	return true;
}


static void ConvertApplyDiffTemplateAI(WorldEncounterProperties *pEncounter, TemplateAI *pBase, TemplateAI *pFinal)
{
	int i, j, k;

	for(i=0; i<eaSize(&pBase->eaActors); ++i) {
		TemplateAIActor *pBaseActor = pBase->eaActors[i];
		TemplateAIActor *pFinalActor = pFinal->eaActors[i];
		WorldActorProperties *pEncActor = pEncounter->eaActors[i];

		if (pBaseActor->pFSM != pFinalActor->pFSM) {
			SET_HANDLE_FROM_REFERENT("FSM", pFinalActor->pFSM, pEncActor->hFSMOverride);
			if(pFinalActor->pFSM)
			{
				pEncActor->bOverrideFSM = true;
			}
		}

		for(j=0; j<eaSize(&pFinalActor->eaVarDefs); ++j) {
			WorldVariableDef *pFinalDef = pFinalActor->eaVarDefs[j];
			WorldVariableDef *pBaseDef = NULL;

			for(k=eaSize(&pBaseActor->eaVarDefs)-1; k>=0; --k) {
				pBaseDef = pBaseActor->eaVarDefs[k];
				if (pBaseDef->pcName && pFinalDef->pcName && (stricmp(pBaseDef->pcName, pFinalDef->pcName) == 0)) {
					break;
				}
			}
			if ((k < 0) || (StructCompare(parse_WorldVariableDef, pBaseDef, pFinalDef, 0, 0, 0) != 0)) {
				eaPush(&pEncActor->eaFSMVariableDefs, StructClone(parse_WorldVariableDef, pFinalDef));
			}
		}
	}
}


static WorldInteractionPropertyEntry *ConvertInteractProps(ContactDef *pContact, OldInteractionProperties *pProps, OldInteractionProperties *pCritterProps)
{
	WorldInteractionPropertyEntry *pEntry = StructCreate(parse_WorldInteractionPropertyEntry);
	int i;
	Message *pInteractMsg, *pFailedMsg;

	if (pContact) {
		pEntry->pcInteractionClass = pcPooled_Contact;
		pEntry->pContactProperties = StructCreate(parse_WorldContactInteractionProperties);
		SET_HANDLE_FROM_REFERENT("Contact", pContact, pEntry->pContactProperties->hContactDef);
	} else {
		pEntry->pcInteractionClass = pcPooled_Clickable;
	}

	pEntry->pInteractCond = exprClone(pProps->interactCond);
	if (pCritterProps && !pEntry->pInteractCond) {
		pEntry->pInteractCond = exprClone(pCritterProps->interactCond);
	}
	pEntry->pSuccessCond = exprClone(pProps->interactSuccessCond);
	if (pCritterProps && !pEntry->pSuccessCond) {
		pEntry->pSuccessCond = exprClone(pCritterProps->interactSuccessCond);
	}

	if (pProps->interactAction || (pProps->interactGameActions && eaSize(&pProps->interactGameActions->eaActions)) ||
		(pCritterProps && (pCritterProps->interactAction || (pCritterProps->interactGameActions && eaSize(&pCritterProps->interactGameActions->eaActions)) ))) {
		pEntry->pActionProperties = StructCreate(parse_WorldActionInteractionProperties);
		pEntry->pActionProperties->pSuccessExpr = exprClone(pProps->interactAction);
		if (pCritterProps && !pEntry->pActionProperties->pSuccessExpr) {
			pEntry->pActionProperties->pSuccessExpr = exprClone(pCritterProps->interactAction);
		}
		if (pProps->interactGameActions) {
			for(i=0; i<eaSize(&pProps->interactGameActions->eaActions); ++i) {
				eaPush(&pEntry->pActionProperties->successActions.eaActions, StructClone(parse_WorldGameActionProperties, pProps->interactGameActions->eaActions[i]));
			}
		} else if (pCritterProps && pCritterProps->interactGameActions) {
			for(i=0; i<eaSize(&pCritterProps->interactGameActions->eaActions); ++i) {
				eaPush(&pEntry->pActionProperties->successActions.eaActions, StructClone(parse_WorldGameActionProperties, pCritterProps->interactGameActions->eaActions[i]));
			}
		}
	}

	pInteractMsg = GET_REF(pProps->interactText.hMessage);
	if (!pInteractMsg) {
		pInteractMsg = pProps->interactText.pEditorCopy;
	}
	if (pCritterProps && !pInteractMsg) {
		pInteractMsg = GET_REF(pCritterProps->interactText.hMessage);
		if (!pInteractMsg) {
			pInteractMsg = pCritterProps->interactText.pEditorCopy;
		}
	}

	pFailedMsg = GET_REF(pProps->interactFailedText.hMessage);
	if (!pFailedMsg) {
		pFailedMsg = pProps->interactFailedText.pEditorCopy;
	}
	if (pCritterProps && !pInteractMsg) {
		pFailedMsg = GET_REF(pCritterProps->interactFailedText.hMessage);
		if (!pFailedMsg) {
			pFailedMsg = pCritterProps->interactFailedText.pEditorCopy;
		}
	}

	if (pInteractMsg || pFailedMsg) {
		pEntry->pTextProperties = StructCreate(parse_WorldTextInteractionProperties);
		if (pInteractMsg) {
			pEntry->pTextProperties->interactOptionText.pEditorCopy = StructCreate(parse_Message);
			pEntry->pTextProperties->interactOptionText.pEditorCopy->pcDefaultString = StructAllocString(pInteractMsg->pcDefaultString);
		}
		if (pFailedMsg) {
			pEntry->pTextProperties->failureConsoleText.pEditorCopy = StructCreate(parse_Message);
			pEntry->pTextProperties->failureConsoleText.pEditorCopy->pcDefaultString = StructAllocString(pFailedMsg->pcDefaultString);
		}
	}

	if (GET_REF(pProps->hInteractAnim) || (pCritterProps && GET_REF(pCritterProps->hInteractAnim))) {
		pEntry->pAnimationProperties = StructCreate(parse_WorldAnimationInteractionProperties);
		COPY_HANDLE(pEntry->pAnimationProperties->hInteractAnim, pProps->hInteractAnim);
		if (pCritterProps && !GET_REF(pProps->hInteractAnim)) {
			COPY_HANDLE(pEntry->pAnimationProperties->hInteractAnim, pCritterProps->hInteractAnim);
		}
	}

	if (pProps->uInteractTime || pProps->uInteractCoolDown || pProps->uInteractActiveFor || (pProps->eInteractType != 7) ||
		(pCritterProps && (pCritterProps->uInteractTime || pCritterProps->uInteractCoolDown || pCritterProps->uInteractActiveFor || (pCritterProps->eInteractType != 7)))
		) {
		pEntry->pTimeProperties = StructCreate(parse_WorldTimeInteractionProperties);

		pEntry->pTimeProperties->fUseTime = pProps->uInteractTime;
		if (pCritterProps && !pEntry->pTimeProperties->fUseTime) {
			pEntry->pTimeProperties->fUseTime = pCritterProps->uInteractTime;
		}

		if (pProps->uInteractCoolDown) {
			pEntry->pTimeProperties->eCooldownTime = WorldCooldownTime_Custom;
			pEntry->pTimeProperties->fCustomCooldownTime = pProps->uInteractCoolDown;
		} else if (pCritterProps && pCritterProps->uInteractCoolDown) {
			pEntry->pTimeProperties->eCooldownTime = WorldCooldownTime_Custom;
			pEntry->pTimeProperties->fCustomCooldownTime = pCritterProps->uInteractCoolDown;
		} else {
			pEntry->pTimeProperties->eCooldownTime = WorldCooldownTime_None;
		}

		pEntry->pTimeProperties->fActiveTime = pProps->uInteractActiveFor;
		if (pCritterProps && !pEntry->pTimeProperties->fActiveTime) {
			pEntry->pTimeProperties->fActiveTime = pCritterProps->uInteractActiveFor;
		}

		if (pProps->eInteractType & InteractType_NoRespawn) {
			pEntry->pTimeProperties->bNoRespawn = true;
		}
		if (pProps->eInteractType & InteractType_ConsumeOnUse) {
			pEntry->pTimeProperties->bHideDuringCooldown = true;
		}
		if (pProps->eInteractType & InteractType_BreakOnMove) {
			pEntry->pTimeProperties->bInterruptOnMove = true;
		}
		if (pProps->eInteractType & InteractType_BreakOnDamage) {
			pEntry->pTimeProperties->bInterruptOnDamage = true;
		}
		if (pProps->eInteractType & InteractType_BreakOnPower) {
			pEntry->pTimeProperties->bInterruptOnPower = true;
		}
	}

	return pEntry;
}


static EncounterTemplate *ConvertDefToTemplate(EncounterDef *pDef, CritterGroup *pDefaultCritterGroup, CritterFaction *pDefaultCritterFaction, const char* pchLevelMapVar, bool bUsePlayerLevel, const char *pcErrFile)
{
	EncounterTemplate *pTemplate = StructCreate(parse_EncounterTemplate);
	int i, j, k;
	int iNextActor = 1;

	if (pDef->bAmbushEncounter || pDef->spawnAnim) {
		pTemplate->pSpawnProperties = StructCreate(parse_EncounterSpawnProperties);
		pTemplate->pSpawnProperties->bIsAmbush = pDef->bAmbushEncounter;
		if (pDef->spawnAnim) {
			pTemplate->pSpawnProperties->eSpawnAnimType = EncounterSpawnAnimType_Specified;
			pTemplate->pSpawnProperties->pcSpawnAnim = pDef->spawnAnim;
		}
	}

	if (bUsePlayerLevel || pDef->bUsePlayerLevel) {
		pTemplate->pLevelProperties = StructCreate(parse_EncounterLevelProperties);
		pTemplate->pLevelProperties->eLevelType = EncounterLevelType_PlayerLevel;
	} else if (pDef->minLevel || pDef->maxLevel) {
		pTemplate->pLevelProperties = StructCreate(parse_EncounterLevelProperties);
		pTemplate->pLevelProperties->eLevelType = EncounterLevelType_Specified;
		pTemplate->pLevelProperties->iSpecifiedMin = pDef->minLevel;
		pTemplate->pLevelProperties->iSpecifiedMax = pDef->maxLevel;
	} else if(pchLevelMapVar) {
		pTemplate->pLevelProperties = StructCreate(parse_EncounterLevelProperties);
		pTemplate->pLevelProperties->eLevelType = EncounterLevelType_MapVariable;
		pTemplate->pLevelProperties->pcMapVariable = StructAllocString(pchLevelMapVar);
	}

	if (pDef->waveCond) {
		pTemplate->pWaveProperties = StructCreate(parse_EncounterWaveProperties);
		pTemplate->pWaveProperties->pWaveCond = exprClone(pDef->waveCond);
		pTemplate->pWaveProperties->eWaveDelayType = WorldEncounterWaveDelayTimerType_Custom;
		pTemplate->pWaveProperties->fWaveDelayMin = pDef->waveDelayMin;
		pTemplate->pWaveProperties->fWaveDelayMax = pDef->waveDelayMax;
		pTemplate->pWaveProperties->eWaveIntervalType = WorldEncounterWaveTimerType_Custom;
		pTemplate->pWaveProperties->fWaveInterval = pDef->waveInterval;
	}

	if (GET_REF(pDef->critterGroup) || GET_REF(pDef->faction) || pDefaultCritterGroup || pDefaultCritterFaction) {
		pTemplate->pActorSharedProperties = StructCreate(parse_EncounterActorSharedProperties);
		if (GET_REF(pDef->critterGroup)) {
			pTemplate->pActorSharedProperties->eCritterGroupType = EncounterSharedCritterGroupSource_Specified;
			COPY_HANDLE(pTemplate->pActorSharedProperties->hCritterGroup, pDef->critterGroup);
		} else if (pDefaultCritterGroup) {
			pTemplate->pActorSharedProperties->eCritterGroupType = EncounterSharedCritterGroupSource_Specified;
			SET_HANDLE_FROM_REFERENT("CritterGroup", pDefaultCritterGroup, pTemplate->pActorSharedProperties->hCritterGroup);
		}
		if (GET_REF(pDef->faction)) {
			pTemplate->pActorSharedProperties->eFactionType = EncounterCritterOverrideType_Specified;
			COPY_HANDLE(pTemplate->pActorSharedProperties->hFaction, pDef->faction);
			pTemplate->pActorSharedProperties->iGangID = pDef->gangID;
		} else if (pDefaultCritterFaction) {
			pTemplate->pActorSharedProperties->eFactionType = EncounterCritterOverrideType_Specified;
			SET_HANDLE_FROM_REFERENT("CritterFaction", pDefaultCritterFaction, pTemplate->pActorSharedProperties->hFaction);
			pTemplate->pActorSharedProperties->iGangID = pDef->gangID;
		}
	}

	for(i=0; i<eaSize(&pDef->encJobs); ++i) {
		// Skip jobs with no FSM
		if (pDef->encJobs[i]->fsmName) {
			eaPush(&pTemplate->eaJobs, StructClone(parse_AIJobDesc, pDef->encJobs[i]));
		}
	}

	// Actors
	for(i=0; i<eaSize(&pDef->actors); ++i) {
		OldActor *pOldActor = pDef->actors[i];
		EncounterActorProperties *pNewActor = StructCreate(parse_EncounterActorProperties);
		int iDifficultyCount = encounter_GetEncounterDifficultiesCount();
		TeamSizeFlags eSpawnAt;
		TeamSizeFlags eBossAt;
		Message *pNameMsg;

		eaPush(&pTemplate->eaActors, pNewActor);

		// Base Actor Properties
		pNewActor->pcName = allocAddString(pOldActor->name);
		if (!pNewActor->pcName || (strnicmp(pNewActor->pcName, "Actor", 5) == 0)) {
			char buf[128];
			sprintf(buf, "Actor%d", iNextActor++);
			pNewActor->pcName = allocAddString(buf);
		}
		
		pNameMsg = GET_REF(pOldActor->displayNameMsg.hMessage);
		if (!pNameMsg) {
			pNameMsg = pOldActor->displayNameMsg.pEditorCopy;
		}
		if (pNameMsg) {
			pNewActor->nameProps.eDisplayNameType = EncounterCritterOverrideType_Specified;
			pNewActor->nameProps.displayNameMsg.pEditorCopy = StructCreate(parse_Message);
			pNewActor->nameProps.displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString(pNameMsg->pcDefaultString);
		}

		// Determine spawn rules
		eSpawnAt = 
			(((pOldActor->disableSpawn & ActorScalingFlag_One) == 0) ? TeamSizeFlags_1 : 0) |
			(((pOldActor->disableSpawn & ActorScalingFlag_Two) == 0) ? TeamSizeFlags_2 : 0) |
			(((pOldActor->disableSpawn & ActorScalingFlag_Three) == 0) ? TeamSizeFlags_3 : 0) |
			(((pOldActor->disableSpawn & ActorScalingFlag_Four) == 0) ? TeamSizeFlags_4 : 0) |
			(((pOldActor->disableSpawn & ActorScalingFlag_Five) == 0) ? TeamSizeFlags_5 : 0);
		eBossAt = 
			(((pOldActor->useBossBar & ActorScalingFlag_One) != 0) ? TeamSizeFlags_1 : 0) |
			(((pOldActor->useBossBar & ActorScalingFlag_Two) != 0) ? TeamSizeFlags_2 : 0) |
			(((pOldActor->useBossBar & ActorScalingFlag_Three) != 0) ? TeamSizeFlags_3 : 0) |
			(((pOldActor->useBossBar & ActorScalingFlag_Four) != 0) ? TeamSizeFlags_4 : 0) |
			(((pOldActor->useBossBar & ActorScalingFlag_Five) != 0) ? TeamSizeFlags_5 : 0);
		
		// Set spawn rules to the same for all difficulties
		if (!iDifficultyCount)
			iDifficultyCount = 1;
		for(j = 0; j < iDifficultyCount; j++)		
		{
			EncounterActorSpawnProperties *pActorSpawnProps = StructCreate(parse_EncounterActorSpawnProperties);
			EncounterActorSpawnProperties *pActorBossProps = StructCreate(parse_EncounterActorSpawnProperties);

			pActorSpawnProps->eSpawnAtDifficulty = j;
			pActorSpawnProps->eSpawnAtTeamSize = eSpawnAt;
			eaPush(&pNewActor->eaSpawnProperties, pActorSpawnProps);

			pActorBossProps->eSpawnAtDifficulty = j;
			pActorBossProps->eSpawnAtTeamSize = eBossAt;
			eaPush(&pNewActor->eaBossSpawnProperties, pActorBossProps);
		}

		// Actor Info
		if (pOldActor->details.info) {
			CritterDef *pCritter = GET_REF(pOldActor->details.info->critterDef);

			if (pCritter) {
				pNewActor->critterProps.eCritterType = ActorCritterType_CritterDef;
				COPY_HANDLE(pNewActor->critterProps.hCritterDef, pOldActor->details.info->critterDef);
			} else if (GET_REF(pOldActor->details.info->critterGroup)) {
				pNewActor->critterProps.eCritterType = ActorCritterType_CritterGroup;
				COPY_HANDLE(pNewActor->critterProps.hCritterGroup, pOldActor->details.info->critterGroup);
				pNewActor->critterProps.pcRank = pOldActor->details.info->pcCritterRank;
				pNewActor->critterProps.pcSubRank = pOldActor->details.info->pcCritterSubRank;
			} else {
				pNewActor->critterProps.eCritterType = ActorCritterType_FromTemplate;
				pNewActor->critterProps.pcRank = pOldActor->details.info->pcCritterRank;
				pNewActor->critterProps.pcSubRank = pOldActor->details.info->pcCritterSubRank;
			}

			if (GET_REF(pOldActor->details.info->critterFaction)) {
				pNewActor->factionProps.eFactionType = EncounterTemplateOverrideType_Specified;
				COPY_HANDLE(pNewActor->factionProps.hFaction, pOldActor->details.info->critterFaction);
			}

			if (pOldActor->details.info->pchSpawnAnim) {
				pNewActor->spawnInfoProps.eSpawnAnimType = EncounterTemplateOverrideType_Specified;
				pNewActor->spawnInfoProps.pcSpawnAnim = StructAllocString(pOldActor->details.info->pchSpawnAnim);
			}

			if (GET_REF(pOldActor->details.info->contactScript) || 
				ConvertHasOldInteractProps(&pOldActor->details.info->oldActorInteractProps) ||
				(pCritter && ConvertHasOldInteractProps(&pCritter->oldInteractProps))) {
				WorldInteractionPropertyEntry *pEntry;

				pEntry = ConvertInteractProps(GET_REF(pOldActor->details.info->contactScript), &pOldActor->details.info->oldActorInteractProps, pCritter ? &pCritter->oldInteractProps : NULL);

				pNewActor->eInteractionType = EncounterCritterOverrideType_Specified;
				pNewActor->pInteractionProperties = StructCreate(parse_WorldInteractionProperties);
				eaPush(&pNewActor->pInteractionProperties->eaEntries, pEntry);
			}
		}

		// AI Info
		if (pOldActor->details.aiInfo) {
			FSMExternVar **eaFSMVarDefs = NULL;
			FSM *pFSM;
			
			if (GET_REF(pOldActor->details.aiInfo->hFSM)) {
				pNewActor->fsmProps.eFSMType = EncounterTemplateOverrideType_Specified;
				COPY_HANDLE(pNewActor->fsmProps.hFSM, pOldActor->details.aiInfo->hFSM);
			}

			pFSM = GET_REF(pNewActor->fsmProps.hFSM);
			if (!pFSM && pTemplate->pSharedAIProperties) {
				pFSM = GET_REF(pTemplate->pSharedAIProperties->hFSM);
			}
			if (pFSM) {
				fsmGetExternVarNamesRecursive( pFSM, &eaFSMVarDefs, "Encounter" );
			}

			for(j=0; j<eaSize(&pOldActor->details.aiInfo->actorVars); ++j) {
				OldEncounterVariable *pOldVar = pOldActor->details.aiInfo->actorVars[j];
				WorldVariableDef *pNewVarDef = StructCreate(parse_WorldVariableDef);
				WorldVariable *pNewVar = StructCreate(parse_WorldVariable);

				eaPush(&pNewActor->eaVariableDefs, pNewVarDef);

				pNewVarDef->pcName = allocAddString(pOldVar->varName);
				pNewVar->pcName = allocAddString(pOldVar->varName);
				pNewVarDef->eDefaultType = WVARDEF_SPECIFY_DEFAULT;

				for(k=eaSize(&eaFSMVarDefs)-1; k>=0; --k) {
					FSMExternVar *pVar = eaFSMVarDefs[k];
					if (stricmp(pVar->name, pOldVar->varName) == 0) {
						break;
					}
				}
				if (k >= 0) {
					pNewVar->eType = worldVariableTypeFromFSMExternVar(eaFSMVarDefs[k]);
					if (pNewVar->eType == WVAR_INT) {
						pNewVar->iIntVal = MultiValGetInt(&pOldVar->varValue, NULL);
					} else if (pNewVar->eType == WVAR_FLOAT) {
						pNewVar->fFloatVal = MultiValGetFloat(&pOldVar->varValue, NULL);
					} else if ((pNewVar->eType == WVAR_STRING) ||
								(pNewVar->eType == WVAR_LOCATION_STRING) ||
								(pNewVar->eType == WVAR_ANIMATION)) {
						pNewVar->pcStringVal = StructAllocString(MultiValGetString(&pOldVar->varValue, NULL));
					} else if (pNewVar->eType == WVAR_MESSAGE) {
						const char *pcKey = StructAllocString(MultiValGetString(&pOldVar->varValue, NULL));
						Message *pMsg = RefSystem_ReferentFromString("Message", pcKey);
						pNewVar->messageVal.pEditorCopy = StructCreate(parse_Message);
						if (pMsg) {
							pNewVar->messageVal.pEditorCopy->pcDefaultString = StructAllocString(pMsg->pcDefaultString);
						}
					}
				} else {
					if (pOldVar->varValue.type == MMT_STRING) {
						pNewVar->eType = WVAR_STRING;
						pNewVar->pcStringVal = StructAllocString(MultiValGetString(&pOldVar->varValue, NULL));
					} else if (pOldVar->varValue.type == MMT_INT32) {
						pNewVar->eType = WVAR_INT;
						pNewVar->iIntVal = MultiValGetInt(&pOldVar->varValue, NULL);
					} else if (pOldVar->varValue.type == MMT_FLOAT32) {
						pNewVar->eType = WVAR_FLOAT;
						pNewVar->fFloatVal = MultiValGetFloat(&pOldVar->varValue, NULL);
					} else {
						assertmsg(0, "Unexpected multi val type!\n");
					}
				}
				pNewVarDef->eType = pNewVar->eType;
				pNewVarDef->pSpecificValue = pNewVar;
			}

			eaDestroy(&eaFSMVarDefs);
		}
	}

	// Named point conversion
	for(i=0; i<eaSize(&pDef->namedPoints); ++i) {
		OldNamedPointInEncounter *pOldPoint = pDef->namedPoints[i];
		EncounterPointProperties *pNewPoint = StructCreate(parse_EncounterPointProperties);

		pNewPoint->pcName = StructAllocString(pOldPoint->pointName);

		eaPush(&pTemplate->eaPoints, pNewPoint);
	}


	// Skipped because they are in the world properties and not the template
	// pDef->spawnCond + pDef->bCheckSpawnCondPerPlayer
	// pDef->successCond + pDef->failCond
	// pDef->spawnRadius + pDef->spawnChance + pDef->respawnTimer + pDef->eDynamicSpawnType + pDef->lockoutRadius
	// pDef->actions
	// pdef->actors[*]->details.info->spawnCond
	// pDef->actors[*]->details.position

	// Simplify!
	// The following blocks attempt to normalize encounters and reduce them in size

	// Sort actors by name
	for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
		for(j=i-1; j>=0; --j) {
			int cmp = stricmp(pTemplate->eaActors[i]->pcName, pTemplate->eaActors[j]->pcName);
			if (cmp == 0) {
				printf("** Warning: Duplicate actor name '%s' (%s)\n", pTemplate->eaActors[i]->pcName, pcErrFile);
			} else if (cmp < 0) {
				EncounterActorProperties *pTemp = pTemplate->eaActors[i];
				pTemplate->eaActors[i] = pTemplate->eaActors[j];
				pTemplate->eaActors[j] = pTemp;
			}
		}
	}


	// Normalize AI for comparison
	encounterTemplate_Optimize(pTemplate);
	encounterTemplate_Deoptimize(pTemplate);

	return pTemplate;
}

#ifdef JDJ_NEW_VERSION_CONVERT_DEC2010
static void ClearOverridableTemplateFields(EncounterTemplate *pTemplate)
{
	int i;

	// TODO: clear encounter-level fields
	for (i = 0; i < eaSize(&pTemplate->eaActors); i++)
	{
		EncounterActorProperties *pActor = pTemplate->eaActors[i];
		if (pActor)
		{
			// display name
			StructInit(parse_DisplayMessage, &pActor->displayNameMsg);

			// critter
			pActor->eCritterType = ActorCritterType_FromTemplate;
			REMOVE_HANDLE(pActor->hCritterDef);

			// faction
			pActor->eFactionType = EncounterCritterOverrideType_FromCritter;
			REMOVE_HANDLE(pActor->hFaction);

			// interaction properties
			pActor->eInteractionType = EncounterCritterOverrideType_FromCritter;
			pActor->pInteractionProperties = NULL;

			// TODO: clear rest of actor level fields
		}
	}
}

static EncTemplateData *ConvertDataToTemplate(EncounterLayer *pLayer, EncounterDef *pSourceDef, EncounterDef *pBaseDef, EncounterDef *pSpawnRule, CritterGroup *pStaticEncCritterGroup, CritterFaction *pStaticEncFaction, const char *pcStaticEncName, TemplateAI **ppFinalAI, EncConvertData *pData)
{
	DictionaryEArrayStruct *pTemplates = resDictGetEArrayStruct("EncounterTemplate");
	EncounterTemplate *pEffectiveTemplate = NULL;
	EncounterDef *pDef = NULL;
	EncTemplateData *pTemplateData;
	TemplateAI *pEffectiveAI = NULL;
	int i;

	// Determine which encounter def to use as the basis for the resulting template
	if (pSpawnRule && !pBaseDef)
	{
		// Only spawn rule is specified, so use it
		pEffectiveTemplate = ConvertDefToTemplate(pSpawnRule, pStaticEncCritterGroup, pStaticEncFaction, pData->bUsePlayerLevel, pData->pchLevelMapVar, pLayer ? pLayer->pchFilename : pSourceDef->filename);
		pDef = pSpawnRule;
	}
	else if (pBaseDef && !pSpawnRule)
	{
		// Only base def is specified, so use it
		pEffectiveTemplate = ConvertDefToTemplate(pBaseDef, pStaticEncCritterGroup, pStaticEncFaction, pData->pchLevelMapVar, pData->bUsePlayerLevel, pLayer ? pLayer->pchFilename : pSourceDef->filename);
		pDef = pBaseDef;
	}
	else
	{
		EncounterTemplate *pSpawnRuleTemplate = NULL, *pSpawnRuleTemplateCopy = NULL;
		EncounterTemplate *pBaseDefTemplate = NULL, *pBaseDefTemplateCopy = NULL;

		// Make templates from both the spawn rule and the base def...
		pSpawnRuleTemplate = ConvertDefToTemplate(pSpawnRule, pStaticEncCritterGroup, pStaticEncFaction, pData->pchLevelMapVar, pData->bUsePlayerLevel, pLayer ? pLayer->pchFilename : pSourceDef->filename);
		encounterTemplate_Clean(pSpawnRuleTemplate);
		pBaseDefTemplate = ConvertDefToTemplate(pBaseDef, pStaticEncCritterGroup, pStaticEncFaction, pData->pchLevelMapVar, pData->bUsePlayerLevel, pLayer ? pLayer->pchFilename : pSourceDef->filename);
		encounterTemplate_Clean(pBaseDefTemplate);

		// ...then compare the resulting templates (disregarding fields that can be overridden);
		// if the two are equivalent, use the converted base def template; otherwise, we NEED to use
		// the template generated by the spawn rule, as it represents the properties that will actually
		// be applied during spawning
		pSpawnRuleTemplateCopy = StructClone(parse_EncounterTemplate, pSpawnRuleTemplate);
		pBaseDefTemplateCopy = StructClone(parse_EncounterTemplate, pBaseDefTemplate);
		assert(pSpawnRuleTemplateCopy && pBaseDefTemplateCopy);
		ClearOverridableTemplateFields(pSpawnRuleTemplateCopy);
		ClearOverridableTemplateFields(pBaseDefTemplateCopy);

		if (StructCompare(parse_EncounterTemplate, pSpawnRuleTemplateCopy, pBaseDefTemplateCopy, 0, 0, TOK_USEROPTIONBIT_1) == 0)
		{
			pEffectiveTemplate = pBaseDefTemplate;
			StructDestroy(parse_EncounterTemplate, pSpawnRuleTemplate);
			pDef = pBaseDef;
		}
		else
		{
			pEffectiveTemplate = pSpawnRuleTemplate;
			StructDestroy(parse_EncounterTemplate, pBaseDefTemplate);
			pDef = pSpawnRule;
		}
		StructDestroy(parse_EncounterTemplate, pSpawnRuleTemplateCopy);
		StructDestroy(parse_EncounterTemplate, pBaseDefTemplateCopy);
	}
	assert(pDef && pEffectiveTemplate);

	// Continue conversion with the appropriate template
	ConvertExtractTemplateAI(pEffectiveTemplate, &pEffectiveAI);
	if (ppFinalAI) {
		*ppFinalAI = ConvertCloneTemplateAI(pEffectiveAI);
	}

	// Look for existing template match
	for(i=eaSize(&pData->eaTemplateData)-1; i>=0; --i) {
		EncounterTemplate *pTestTemplate = pData->eaTemplateData[i]->pTemplate;

		pEffectiveTemplate->pcName = pTestTemplate->pcName;
		pEffectiveTemplate->pcScope = pTestTemplate->pcScope;
		pEffectiveTemplate->pcFilename = pTestTemplate->pcFilename;

		// Must fixup messages since templates in pData->eaTemplateData are fixed up before being added to the EArray
		// The fixup must be done at each loop iteration since it relies on the template name/scope/filename
		if (pEffectiveTemplate->pcName && pEffectiveTemplate->pcScope)
			encounterTemplate_FixupMessages(pEffectiveTemplate);

		if (StructCompare(parse_EncounterTemplate, pTestTemplate, pEffectiveTemplate, 0, 0, TOK_USEROPTIONBIT_1) == 0) {
			break;
		}
	}
	pEffectiveTemplate->pcName = NULL;
	pEffectiveTemplate->pcScope = NULL;
	pEffectiveTemplate->pcFilename = NULL;

	if (i < 0) {
		EncounterTemplate *pTestTemplate;
		TemplateAI *pDictAI = NULL;

		// New template, so record info on it
		pTemplateData = calloc(1, sizeof(EncTemplateData));

		if (pcStaticEncName) {
			char *estrName = NULL;
			if (!resFixName(pcStaticEncName, &estrName)) {
				pTemplateData->pcName = allocAddString(pcStaticEncName);
			} else {
				pTemplateData->pcName = allocAddString(estrName);
				estrDestroy(&estrName);
			}
			pTemplateData->pcScope = NULL;
		} else {
			char *estrName = NULL;
			if (!resFixName(pDef->name, &estrName)) {
				pTemplateData->pcName = allocAddString(pDef->name);
			} else {
				pTemplateData->pcName = allocAddString(estrName);
				estrDestroy(&estrName);
			}
			if (!resFixScope(pDef->scope, &estrName)) {
				pTemplateData->pcScope = allocAddString(pDef->scope);
			} else {
				pTemplateData->pcScope = allocAddString(estrName);
				estrDestroy(&estrName);
			}
		}
		pTemplateData->pTemplate = pEffectiveTemplate;
		pTemplateData->pTemplateAI = pEffectiveAI;
		if (pLayer) {
			eaPushUnique(&pTemplateData->eaLayerDefs, pDef);
			eaPushUnique(&pTemplateData->eaEncLayers, pLayer);
			eaPushUnique(&pData->eaEncLayers, pLayer);
		} else if (pSourceDef) {
			eaPush(&pTemplateData->eaSourceDefs, pSourceDef);
			eaPush(&pData->eaSourceDefs, pSourceDef);
		}
		eaPush(&pData->eaTemplateData, pTemplateData);

		// See if an existing template will match
		// Lots of complication here to make messages and expressions match properly
		pTestTemplate = StructClone(parse_EncounterTemplate, pEffectiveTemplate);
		assert(pTestTemplate);
		encounterTemplate_Clean(pTestTemplate);
		for(i=eaSize(&pTemplates->ppReferents)-1; i>=0; --i) {
			// Set up dictionary template in editor mode
			EncounterTemplate *pDictTemplate = StructClone(parse_EncounterTemplate, pTemplates->ppReferents[i]);
			assert(pDictTemplate);
			langMakeEditorCopy(parse_EncounterTemplate, pDictTemplate, false);
			encounterTemplate_Clean(pDictTemplate);
			encounterTemplate_Deoptimize(pDictTemplate);
			ConvertExtractTemplateAI(pDictTemplate, &pDictAI);

			// Set up test template with same message stuff
			pTestTemplate->pcName = pDictTemplate->pcName;
			pTestTemplate->pcScope = pDictTemplate->pcScope;
			pTestTemplate->pcFilename = pDictTemplate->pcFilename;
			encounterTemplate_FixupMessages(pTestTemplate);

			// Compare
			if ((StructCompare(parse_EncounterTemplate, pDictTemplate, pTestTemplate, 0, 0, TOK_USEROPTIONBIT_1) == 0) &&
				ConvertIsCompatibleTemplateAI(pDictAI, pEffectiveAI) ){
				break;
			}

			// Cleanup
			StructDestroy(parse_EncounterTemplate, pDictTemplate);
			ConvertDestroyTemplateAI(pDictAI);
		}
		if (i >= 0) {
			// Matched previously created template
			pTemplateData->pcName = pTestTemplate->pcName;
			pTemplateData->pcScope = pTestTemplate->pcScope;
			pTemplateData->pTemplateAI = pDictAI;
			pTemplateData->bPreExisting = true;
			ConvertDestroyTemplateAI(pEffectiveAI);
		}
		StructDestroy(parse_EncounterTemplate, pTestTemplate);

		return pTemplateData;
	} else {
		TemplateAI *pCommonAI = NULL;

		pTemplateData = pData->eaTemplateData[i];

		// Matched template being created in this conversion, so add to it and clean up
		if (pLayer) {
			eaPush(&pTemplateData->eaLayerDefs, pDef);
			eaPushUnique(&pTemplateData->eaEncLayers, pLayer);
			eaPushUnique(&pData->eaEncLayers, pLayer);
		} else if (pSourceDef) {
			eaPush(&pTemplateData->eaSourceDefs, pSourceDef);
			eaPush(&pData->eaSourceDefs, pSourceDef);
		}
		StructDestroy(parse_EncounterTemplate, pEffectiveTemplate);

		// Merge AI data
		ConvertFindCommonTemplateAI(pTemplateData->pTemplateAI, pEffectiveAI, &pCommonAI);
		ConvertDestroyTemplateAI(pTemplateData->pTemplateAI);
		pTemplateData->pTemplateAI = pCommonAI;

		return pTemplateData;
	}
}
#endif //JDJ_NEW_VERSION_CONVERT_DEC2010


static EncTemplateData *ConvertDataToTemplate(EncounterLayer *pLayer, EncounterDef *pSourceDef, EncounterDef *pDef, CritterGroup *pStaticEncCritterGroup, CritterFaction *pStaticEncFaction, const char *pcStaticEncName, TemplateAI **ppFinalAI, EncConvertData *pData)
{
	DictionaryEArrayStruct *pTemplates = resDictGetEArrayStruct("EncounterTemplate");
	EncounterTemplate *pEffectiveTemplate = NULL;
	EncTemplateData *pTemplateData;
	TemplateAI *pEffectiveAI = NULL;
	int i;

	// Make a template from a def
	pEffectiveTemplate = ConvertDefToTemplate(pDef, pStaticEncCritterGroup, pStaticEncFaction, pData->pchLevelMapVar, pData->bUsePlayerLevel, pLayer ? pLayer->pchFilename : pSourceDef->filename);
	ConvertExtractTemplateAI(pEffectiveTemplate, &pEffectiveAI);
	if (ppFinalAI) {
		*ppFinalAI = ConvertCloneTemplateAI(pEffectiveAI);
	}

	// Must clean since templates in pData->eaTemplateData are cleaned before being added to the EArray
	encounterTemplate_Clean(pEffectiveTemplate);

	// Look for existing template match
	for(i=eaSize(&pData->eaTemplateData)-1; i>=0; --i) {
		EncounterTemplate *pTestTemplate = pData->eaTemplateData[i]->pTemplate;
		pEffectiveTemplate->pcName = pTestTemplate->pcName;
		pEffectiveTemplate->pcScope = pTestTemplate->pcScope;
		pEffectiveTemplate->pcFilename = pTestTemplate->pcFilename;

		// Must fixup messages since templates in pData->eaTemplateData are fixed up before being added to the EArray
		// The fixup must be done at each loop iteration since it relies on the template name/scope/filename
		encounterTemplate_FixupMessages(pEffectiveTemplate);

		if (StructCompare(parse_EncounterTemplate, pTestTemplate, pEffectiveTemplate, 0, 0, TOK_USEROPTIONBIT_1) == 0) {
			break;
		}
	}
	pEffectiveTemplate->pcName = NULL;
	pEffectiveTemplate->pcScope = NULL;
	pEffectiveTemplate->pcFilename = NULL;

	if (i < 0) {
		EncounterTemplate *pTestTemplate;
		TemplateAI *pDictAI = NULL;

		// New template, so record info on it
		pTemplateData = calloc(1, sizeof(EncTemplateData));

		if (pcStaticEncName) {
			char *estrName = NULL;
			if (!resFixName(pcStaticEncName, &estrName)) {
				pTemplateData->pcName = allocAddString(pcStaticEncName);
			} else {
				pTemplateData->pcName = allocAddString(estrName);
				estrDestroy(&estrName);
			}
			pTemplateData->pcScope = NULL;
		} else {
			char *estrName = NULL;
			if (!resFixName(pDef->name, &estrName)) {
				pTemplateData->pcName = allocAddString(pDef->name);
			} else {
				pTemplateData->pcName = allocAddString(estrName);
				estrDestroy(&estrName);
			}
			if (!resFixScope(pDef->scope, &estrName)) {
				pTemplateData->pcScope = allocAddString(pDef->scope);
			} else {
				pTemplateData->pcScope = allocAddString(estrName);
				estrDestroy(&estrName);
			}
		}
		pTemplateData->pTemplate = pEffectiveTemplate;
		pTemplateData->pTemplateAI = pEffectiveAI;
		if (pLayer) {
			eaPush(&pTemplateData->eaLayerDefs, pDef);
			eaPush(&pTemplateData->eaEncLayers, pLayer);
			eaPushUnique(&pData->eaEncLayers, pLayer);
		} else if (pSourceDef) {
			eaPush(&pTemplateData->eaSourceDefs, pSourceDef);
			eaPush(&pData->eaSourceDefs, pSourceDef);
		}
		eaPush(&pData->eaTemplateData, pTemplateData);

		// See if an existing template will match
		// Lots of complication here to make messages and expressions match properly
		pTestTemplate = StructClone(parse_EncounterTemplate, pEffectiveTemplate);
		assert(pTestTemplate);
		encounterTemplate_Clean(pTestTemplate);
		for(i=eaSize(&pTemplates->ppReferents)-1; i>=0; --i) {
			// Set up dictionary template in editor mode
			EncounterTemplate *pDictTemplate = StructClone(parse_EncounterTemplate, pTemplates->ppReferents[i]);
			assert(pDictTemplate);
			langMakeEditorCopy(parse_EncounterTemplate, pDictTemplate, false);
			encounterTemplate_Clean(pDictTemplate);
			encounterTemplate_Deoptimize(pDictTemplate);
			ConvertExtractTemplateAI(pDictTemplate, &pDictAI);

			// Set up test template with same message stuff
			pTestTemplate->pcName = pDictTemplate->pcName;
			pTestTemplate->pcScope = pDictTemplate->pcScope;
			pTestTemplate->pcFilename = pDictTemplate->pcFilename;
			encounterTemplate_FixupMessages(pTestTemplate);

			// Compare
			if ((StructCompare(parse_EncounterTemplate, pDictTemplate, pTestTemplate, 0, 0, TOK_USEROPTIONBIT_1) == 0) &&
				ConvertIsCompatibleTemplateAI(pDictAI, pEffectiveAI) ){
				break;
			}

			// Cleanup
			StructDestroy(parse_EncounterTemplate, pDictTemplate);
			ConvertDestroyTemplateAI(pDictAI);
		}
		if (i >= 0) {
			// Matched previously created template
			pTemplateData->pcName = pTestTemplate->pcName;
			pTemplateData->pcScope = pTestTemplate->pcScope;
			pTemplateData->pTemplateAI = pDictAI;
			pTemplateData->bPreExisting = true;
			ConvertDestroyTemplateAI(pEffectiveAI);
		}
		StructDestroy(parse_EncounterTemplate, pTestTemplate);

		return pTemplateData;
	} else {
		TemplateAI *pCommonAI = NULL;

		pTemplateData = pData->eaTemplateData[i];

		// Matched template being created in this conversion, so add to it and clean up
		if (pLayer) {
			eaPush(&pTemplateData->eaLayerDefs, pDef);
			eaPushUnique(&pTemplateData->eaEncLayers, pLayer);
			eaPushUnique(&pData->eaEncLayers, pLayer);
		} else if (pSourceDef) {
			eaPush(&pTemplateData->eaSourceDefs, pSourceDef);
			eaPush(&pData->eaSourceDefs, pSourceDef);
		}
		StructDestroy(parse_EncounterTemplate, pEffectiveTemplate);

		// Merge AI data
		ConvertFindCommonTemplateAI(pTemplateData->pTemplateAI, pEffectiveAI, &pCommonAI);
		ConvertDestroyTemplateAI(pTemplateData->pTemplateAI);
		pTemplateData->pTemplateAI = pCommonAI;

		return pTemplateData;
	}
}


static void ConvertProcessTemplate(EncConvertData *pConvertData, EncTemplateData *pData)
{
	char buf[1024];
	char namebuf[1024];
	char *pcBase;
	int iNameCount = 0;

	if (pData->bPreExisting || pData->bProcessed || !eaSize(&pData->eaEncLayers)) {
		// No processing required if already existed or simply isn't used
		return;
	} if (eaSize(&pData->eaSourceDefs)) {
		// Came from an EncounterDef, so keep that def's name and scope
		pData->pTemplate->pcScope = allocAddString(pData->pcScope);
		pData->pTemplate->pcName = allocAddString(pData->pcName);
	} else if (eaSize(&pData->eaEncLayers) == 1) {
		// Came from exactly one encounter layer
		char *ptr, *ptr2;

		// Set up template scope
		strcpy(buf, pData->eaEncLayers[0]->pchFilename);
		ptr = strrchr(buf, '/');
		if (ptr) {
			*ptr = '\0';
		} else {
			assert(0);
		}
		++ptr;
		ptr2 = strrchr(ptr, '.');
		if (ptr2) {
			*ptr2 = '\0';
		} else {
			assert(0);
		}

		pData->pTemplate->pcScope = allocAddString(buf);

		// Set up template name
		if (!pData->pTemplate->pcName) {
			if (pData->pcName) {
				sprintf(namebuf, "%s_%s", ptr, pData->pcName);
				pData->pTemplate->pcName = allocAddString(namebuf);
			} else {
				pData->pTemplate->pcName = allocAddString("MapEncounter");
			}
		}
	} else {
		// Set up template scope
		pData->pTemplate->pcScope = allocAddString("Shared");

		// Set up template name
		if (!pData->pTemplate->pcName) {
			pData->pTemplate->pcName = allocAddString("SharedEncounter");
		}
	}

	// Make name unique
	pcBase = (char*)pData->pTemplate->pcName;
	while(!ConvertIsTemplateNameUnique(pConvertData, pData->pTemplate)) {
		++iNameCount;
		sprintf(buf, "%s_%d", pcBase, iNameCount);
		pData->pTemplate->pcName = allocAddString(buf);
	}

	// Set up filename
	if (pData->pTemplate->pcScope) {
		sprintf(buf, "defs/encounters2/%s/%s.encounter2", pData->pTemplate->pcScope, pData->pTemplate->pcName);
	} else {
		sprintf(buf, "defs/encounters2/%s.encounter2", pData->pTemplate->pcName);
	}
	pData->pTemplate->pcFilename = allocAddFilename(buf);

	// Set up display messages
	encounterTemplate_FixupMessages(pData->pTemplate);

	// Copy name up to root (a no-op in most cases)
	pData->pcName = pData->pTemplate->pcName;
	pData->bProcessed = true;
}

static void ConvertDataToWorldEncounter(ZoneMapInfo *pZone, LibFileLoad *pGeoLayer, GroupDef *pParentGroup, char *pcParentIDString, LogicalGroup *pLogicalGroup, OldStaticEncounter *pStaticEnc, EncounterDef *pSourceDef, EncTemplateData *pTemplateData, TemplateAI *pFinalAI, Vec3 vPos, Vec3 vRot, EncConvertData *pData, FILE *file)
{
	GroupDef *pContainerGroup;
	GroupDef *pGroup; 
	GroupChild *pGroupChild;
	WorldEncounterProperties *pEncounter;
	ScopeTableLoad *pScopeLoad;
	int next_id;
	const char *pcName = NULL;
	char nameBuf[256];
	char buf[256];
	char idBuf[256];
	char scopeBuf[1025];
	char *ptr;
	int i;
	int nameNum = 1;
	
	next_id = Convert_GetNextIdForGeoLayer(pGeoLayer);

	assert(pGeoLayer->defs);

	if (pParentGroup) {
		pContainerGroup = pParentGroup;
	} else {
		// Create the group data for the container
		pContainerGroup = pGeoLayer->defs[1];
		sprintf(idBuf, "%d,", pGeoLayer->defs[0]->children[0]->uid_in_parent);
		pcParentIDString = idBuf;
	}

	if (pStaticEnc) {
		pcName = pStaticEnc->name;
	}
	if (!pcName) {
		pcName = pSourceDef->name;
	}
	if (!pcName) {
		pcName = "UNNAMED_Encounter";
	}
	while(true) {
		if (nameNum == 1) {
			strcpy(nameBuf, pcName);
		} else {
			sprintf(nameBuf, "%s_%d", pcName, nameNum);
		}
		for(i=eaSize(&pGeoLayer->defs)-1; i>=0; --i) {
			GroupDef *pDef = pGeoLayer->defs[i];
			if (stricmp(pDef->name_str, nameBuf) == 0) {
				break;
			}
		}
		if (i < 0) {
			pcName = nameBuf;
			break;
		}
		++nameNum;
	}

	// Create the group data
	pGroupChild = StructCreate(parse_GroupChild);
	pGroupChild->name = allocAddString(pcName);
	pGroupChild->name_uid = next_id++;
	pGroupChild->uid_in_parent = next_id++;
	pGroupChild->seed = 0;

	copyVec3(vPos, pGroupChild->pos);
	copyVec3(vRot, pGroupChild->rot);
	eaPush(&pContainerGroup->children, pGroupChild);

	if (strnicmp(pcName, "UNNAMED_", 8) != 0) {
		// Create logical name
		pScopeLoad = StructCreate(parse_ScopeTableLoad);
		pScopeLoad->name = StructAllocString(pcName);
		sprintf(buf, "%s%d,", pcParentIDString, pGroupChild->uid_in_parent);
		pScopeLoad->path = StructAllocString(buf);
		eaPush(&pGeoLayer->defs[0]->scope_entries_load, pScopeLoad);

		// Create logical group entry if required
		if (pLogicalGroup) {
			eaPush(&pLogicalGroup->child_names, StructAllocString(pcName));
		}
	}

	// Create the encounter
	pEncounter = StructCreate(parse_WorldEncounterProperties);

	ConvertProcessTemplate(pData, pTemplateData);
	SET_HANDLE_FROM_STRING("EncounterTemplate", pTemplateData->pcName, pEncounter->hTemplate);

	if (pSourceDef->lockoutRadius) {
		fprintf(file, "    ** Spawn lockout radius was not converted\n");
	}

	if (pSourceDef->spawnCond || 
		pSourceDef->respawnTimer || 
		(pSourceDef->spawnChance != 100) || 
		pSourceDef->spawnRadius || 
		pSourceDef->eDynamicSpawnType ||
		pStaticEnc->bNoSnapToGround ||
		pStaticEnc->noDespawn
		) {
		pEncounter->pSpawnProperties = StructCreate(parse_WorldEncounterSpawnProperties);
		if (pSourceDef->spawnCond) {
			pEncounter->pSpawnProperties->pSpawnCond = exprClone(pSourceDef->spawnCond);
			if (pSourceDef->bCheckSpawnCondPerPlayer) {
				pEncounter->pSpawnProperties->eSpawnCondType = WorldEncounterSpawnCondType_RequiresPlayer;
			} else {
				pEncounter->pSpawnProperties->eSpawnCondType = WorldEncounterSpawnCondType_Normal;
			}
		}
		// Encounter 1 encounters are only allowed to respawn on static/shared maps.  If we're not on one of those maps, set respawn time to "never"
		if(pZone && !(zmapInfoGetMapType(pZone) == ZMTYPE_STATIC) && !(zmapInfoGetMapType(pZone) == ZMTYPE_SHARED))
		{
			pEncounter->pSpawnProperties->eRespawnTimerType = WorldEncounterTimerType_Never;
		}
		// Otherwise, follow rules as normal
		else if (pSourceDef->respawnTimer == 300) {
			pEncounter->pSpawnProperties->eRespawnTimerType = WorldEncounterTimerType_Medium;
		} else if (pSourceDef->respawnTimer) {
			pEncounter->pSpawnProperties->eRespawnTimerType = WorldEncounterTimerType_Custom;
			pEncounter->pSpawnProperties->fRespawnTimer = pSourceDef->respawnTimer;
		} else if (!pZone || (zmapInfoGetMapType(pZone) == ZMTYPE_STATIC) || (zmapInfoGetMapType(pZone) == ZMTYPE_SHARED)) {
			pEncounter->pSpawnProperties->eRespawnTimerType = WorldEncounterTimerType_Medium;
		} else {
			pEncounter->pSpawnProperties->eRespawnTimerType = WorldEncounterTimerType_None;
		}
		if (pSourceDef->spawnChance) {
			pEncounter->pSpawnProperties->fSpawnChance = pSourceDef->spawnChance;
		}
		if (pSourceDef->spawnRadius) {
			pEncounter->pSpawnProperties->eSpawnRadiusType = WorldEncounterRadiusType_Custom;
			pEncounter->pSpawnProperties->fSpawnRadius = pSourceDef->spawnRadius;
		} else {
			pEncounter->pSpawnProperties->eSpawnRadiusType = WorldEncounterRadiusType_Medium;
		}
		pEncounter->pSpawnProperties->eDyamicSpawnType = pSourceDef->eDynamicSpawnType;
		pEncounter->pSpawnProperties->bSnapToGround = !pStaticEnc->bNoSnapToGround;
		pEncounter->pSpawnProperties->bNoDespawn = pStaticEnc->noDespawn;
	} 

	if (pSourceDef->successCond || pSourceDef->failCond || eaSize(&pSourceDef->actions)) {
		pEncounter->pEventProperties = StructCreate(parse_WorldEncounterEventProperties);
		pEncounter->pEventProperties->pSuccessCond = exprClone(pSourceDef->successCond);
		pEncounter->pEventProperties->pFailureCond = exprClone(pSourceDef->failCond);
		for(i=0; i<eaSize(&pSourceDef->actions); ++i) {
			OldEncounterAction *pAction = pSourceDef->actions[i];
			if (pAction->state == EncounterState_Success) {
				pEncounter->pEventProperties->pSuccessExpr = exprClone(pAction->actionExpr);
			} else if (pAction->state == EncounterState_Failure) {
				pEncounter->pEventProperties->pFailureExpr = exprClone(pAction->actionExpr);
			}
		}
	}

	if (pStaticEnc->patrolRouteName) {
		pEncounter->pcPatrolRoute = StructAllocString(pStaticEnc->patrolRouteName);
	}

#ifdef JDJ_NEW_VERSION_CONVERT_DEC2010
	// TODO: apply encounter overrides
#endif

	// Create actors
	for(i=0; i<eaSize(&pSourceDef->actors); ++i) {
		OldActor *pOldActor = pSourceDef->actors[i];
		EncounterActorProperties *pNewActor = pTemplateData->pTemplate->eaActors[i];
		WorldActorProperties *pWorldActor = StructCreate(parse_WorldActorProperties);

		eaPush(&pEncounter->eaActors, pWorldActor);

		pWorldActor->pcName = pNewActor->pcName;
		if (pOldActor->details.position) {
			copyVec3(pOldActor->details.position->posOffset, pWorldActor->vPos);
			quatToPYR(pOldActor->details.position->rotQuat, pWorldActor->vRot);
		}
		if (pOldActor->details.info && pOldActor->details.info->spawnCond) {
			fprintf(file, "    ** Spawn condition on actor '%s' was not converted\n", pOldActor->name);
		}

#ifdef JDJ_NEW_VERSION_CONVERT_DEC2010
		// Apply actor overrides
		// TODO: Apply rest of overrides
		if (GET_REF(pOldActor->displayNameMsg.hMessage))
			StructCopy(parse_DisplayMessage, &pOldActor->displayNameMsg, &pWorldActor->displayNameMsg, 0, 0, 0);
		if (GET_REF(pOldActor->details.info->critterFaction))
		{
			pWorldActor->pFactionProperties = StructCreate(parse_WorldActorFactionProperties);
			COPY_HANDLE(pWorldActor->pFactionProperties->hCritterFaction, pOldActor->details.info->critterFaction);
		}
		if (GET_REF(pOldActor->details.info->critterDef))
		{
			pWorldActor->pCritterProperties = StructCreate(parse_WorldActorCritterProperties);
			COPY_HANDLE(pWorldActor->pCritterProperties->hCritterDef, pOldActor->details.info->critterDef);
		}
#endif
	}

	// Named point conversion
	for(i=0; i<eaSize(&pSourceDef->namedPoints); ++i) {
		OldNamedPointInEncounter *pOldPoint = pSourceDef->namedPoints[i];
		WorldEncounterPointProperties *pNewPoint = StructCreate(parse_WorldEncounterPointProperties);
		Quat tempQuat;

		pNewPoint->pcName = StructAllocString(pOldPoint->pointName);
		copyVec3(pOldPoint->relLocation[3], pNewPoint->vPos);
		mat3ToQuat(pOldPoint->relLocation, tempQuat);
		quatToPYR(tempQuat, pNewPoint->vRot);

		eaPush(&pEncounter->eaPoints, pNewPoint);
	}

	ConvertApplyDiffTemplateAI(pEncounter, pTemplateData->pTemplateAI, pFinalAI);

	strcpy(scopeBuf, pGeoLayer->defs[0]->filename);
	ptr = strrchr(scopeBuf, '/');
	if (ptr) {
		*ptr = '\0';
	}
	worldEncounter_FixupMessages(pEncounter, pGeoLayer->defs[0]->filename, pcName, scopeBuf);

	// Create DefLoad
	pGroup = StructCreate(parse_GroupDef);
	pGroup->name_str = allocAddString(pcName);
	pGroup->name_uid = pGroupChild->name_uid;
	pGroup->model_name = StructAllocString("core_icons_objective");
	pGroup->property_structs.encounter_properties = pEncounter;
	pGroup->property_structs.physical_properties.bVisible = 0;

	eaPush(&pGeoLayer->defs, pGroup);
}


static void ConvertStaticEncounter(EncounterLayer *pLayer, OldStaticEncounter *pStaticEnc, GroupDef *pParentGroup, char *pcParentIDString, LogicalGroup *pLogicalGroup, char ***peaGroupNames, bool bMakeWorldData, EncounterLayer ***peaModifiedEncLayers, EncounterLayer ***peaOrigEncLayers, LibFileLoad ***peaGeoLayers, LibFileLoad ***peaModifiedGeoLayers, char ***peaExtraFiles, EncConvertData *pData, FILE *file)
{
	EncTemplateData *pTemplateData;
	TemplateAI *pFinalAI = NULL;
	char idBuf[256];
	
	// Do the magic stuff to the static encounter to realize it
	oldencounter_UpdateStaticEncounterSpawnRule(pStaticEnc, pLayer);

	// Find the right template
	pTemplateData = ConvertDataToTemplate(pLayer, NULL, pStaticEnc->spawnRule, GET_REF(pStaticEnc->encCritterGroup), GET_REF(pStaticEnc->encFaction), pStaticEnc->name, &pFinalAI, pData);
#ifdef JDJ_NEW_VERSION_CONVERT_DEC2010
	pTemplateData = ConvertDataToTemplate(pLayer, NULL, GET_REF(pStaticEnc->baseDef), pStaticEnc->spawnRule, GET_REF(pStaticEnc->encCritterGroup), GET_REF(pStaticEnc->encFaction), pStaticEnc->name, &pFinalAI, pData);
#endif

	if (bMakeWorldData) {
		ZoneMapInfo *pZone = NULL;
		LibFileLoad *pGeoLayer;
		int index;
		Vec3 vRot;
		
		// Find the right geo layer to put the encounter into
		index = eaFind(peaOrigEncLayers, pLayer);
		if (index < 0) {
			EncounterLayer *pEmptyLayer = StructCreate(parse_EncounterLayer);
			int next_id;
			GroupChild *pContainerGroup;
			GroupDef *pContainerDef;

			pEmptyLayer->pchFilename = pLayer->pchFilename;
			pEmptyLayer->bRemoveOnSave = true;
			eaPush(peaModifiedEncLayers, pEmptyLayer);
			eaPush(peaOrigEncLayers, pLayer);

			pGeoLayer = Convert_GetGeoLayerForEncLayer(pLayer, pStaticEnc->encPos, peaGeoLayers, peaExtraFiles, NULL);
			eaPushUnique(peaModifiedGeoLayers, pGeoLayer);

			next_id = Convert_GetNextIdForGeoLayer(pGeoLayer);

			// Create the group data for the container
			pContainerGroup = StructCreate(parse_GroupChild);
			pContainerGroup->name = allocAddString("Encounters");
			pContainerGroup->name_uid = next_id++;
			pContainerGroup->uid_in_parent = next_id++;
			eaPush(&pGeoLayer->defs[0]->children, pContainerGroup);

			// Create Def for the container
			pContainerDef = StructCreate(parse_GroupDef);
			pContainerDef->name_str = allocAddString("Encounters");
			pContainerDef->name_uid = pContainerGroup->name_uid;
			eaPush(&pGeoLayer->defs, pContainerDef);

			sprintf(idBuf, "%d,", pContainerGroup->uid_in_parent);
			pcParentIDString = idBuf;

			fprintf(file, "\n\nConverting layer '%s' to '%s'\n", pLayer->pchFilename, pGeoLayer->filename);

		} else {
			pGeoLayer = Convert_GetGeoLayerForEncLayer(pLayer, pStaticEnc->encPos, peaGeoLayers, peaExtraFiles, &pZone);
		}

		// Set up world portion of encounter
		fprintf(file, "    Converted encounter '%s'\n", pStaticEnc->spawnRule->name);

		quatToPYR(pStaticEnc->encRot, vRot);
		ConvertDataToWorldEncounter(pZone, pGeoLayer, pParentGroup, pcParentIDString, pLogicalGroup, pStaticEnc, pStaticEnc->spawnRule, pTemplateData, pFinalAI, pStaticEnc->encPos, vRot, pData, file);
	}

	ConvertDestroyTemplateAI(pFinalAI);
}


static void ConvertStaticEncounterGroup(EncounterLayer *pLayer, OldStaticEncounterGroup *pGroup, GroupDef *pParentGroup, char *pcParentIDString, LogicalGroup *pLogicalGroup, char ***peaGroupNames, bool bMakeWorldData, EncounterLayer ***peaModifiedEncLayers, EncounterLayer ***peaOrigEncLayers, LibFileLoad ***peaGeoLayers, LibFileLoad ***peaModifiedGeoLayers, char ***peaExtraFiles, EncConvertData *pData, FILE *file)
{
	int i;

	for(i=0; i<eaSize(&pGroup->staticEncList); ++i) {
		ConvertStaticEncounter(pLayer, pGroup->staticEncList[i], pParentGroup, pcParentIDString, pLogicalGroup, peaGroupNames, bMakeWorldData, peaModifiedEncLayers, peaOrigEncLayers, peaGeoLayers, peaModifiedGeoLayers, peaExtraFiles, pData, file);
	}

	if(bMakeWorldData)
	{
		// Setup logical group spawn properties
		if(pGroup->numToSpawn && pLogicalGroup)
		{
			if(!pLogicalGroup->properties)
			{
				pLogicalGroup->properties = StructCreate(parse_LogicalGroupProperties);
			}

			pLogicalGroup->properties->encounterSpawnProperties.eRandomType = LogicalGroupRandomType_OnceOnLoad;
			pLogicalGroup->properties->encounterSpawnProperties.eSpawnAmountType = LogicalGroupSpawnAmountType_Number;
			pLogicalGroup->properties->encounterSpawnProperties.uSpawnAmount = pGroup->numToSpawn;
		}

	}

	for(i=0; i<eaSize(&pGroup->childList); ++i) {
		GroupDef *pPassedParentGroup = NULL;
		char *pcPassedParentIDString = NULL;
		char *pcGroupName;
		char groupNameBuf[256];
		char idBuf[256];
		char idPassedBuf[256];
		OldStaticEncounterGroup *pChildGroup = pGroup->childList[i];
		LogicalGroup *pPassedLogicalGroup = pLogicalGroup;

		pcGroupName = (char*)pChildGroup->groupName;

		if (bMakeWorldData) {
			int index;
			LibFileLoad *pGeoLayer = NULL;
			Vec3 vPos = {0,0,0};
			// Create parent group

			Convert_GetSamplePositionForEncGroup(pChildGroup, vPos);

			// Find the right geo layer to put the encounter into
			index = eaFind(peaOrigEncLayers, pLayer);
			if (index < 0) {
				EncounterLayer *pEmptyLayer = StructCreate(parse_EncounterLayer);
				int next_id;
				GroupChild *pContainerGroup;
				GroupDef *pContainerDef;

				pEmptyLayer->pchFilename = pLayer->pchFilename;
				pEmptyLayer->bRemoveOnSave = true;
				eaPush(peaModifiedEncLayers, pEmptyLayer);
				eaPush(peaOrigEncLayers, pLayer);

				pGeoLayer = Convert_GetGeoLayerForEncLayer(pLayer, vPos, peaGeoLayers, peaExtraFiles, NULL);
				eaPushUnique(peaModifiedGeoLayers, pGeoLayer);

				next_id = Convert_GetNextIdForGeoLayer(pGeoLayer);

				// Create the group data for the container
				pContainerGroup = StructCreate(parse_GroupChild);
				pContainerGroup->name = allocAddString("Encounters");
				pContainerGroup->name_uid = next_id++;
				pContainerGroup->uid_in_parent = next_id++;
				eaPush(&pGeoLayer->defs[0]->children, pContainerGroup);

				// Create Def for the container
				pContainerDef = StructCreate(parse_GroupDef);
				pContainerDef->name_str = allocAddString("Encounters");
				pContainerDef->name_uid = pContainerGroup->name_uid;
				eaPush(&pGeoLayer->defs, pContainerDef);

				pParentGroup = pContainerDef;
				sprintf(idBuf, "%d,", pContainerGroup->uid_in_parent);
				pcParentIDString = idBuf;

				fprintf(file, "\n\nConverting layer '%s' to '%s'\n", pLayer->pchFilename, pGeoLayer->filename);

			} else {
				pGeoLayer = Convert_GetGeoLayerForEncLayer(pLayer, vPos, peaGeoLayers, peaExtraFiles, NULL);
			}

			if (pGeoLayer && pChildGroup->groupName) {
				int next_id;
				int j;
				int nameNum = 1;
				GroupChild *pContainerGroup;
				GroupDef *pContainerDef;

				// Make sure group name doesn't collide with other defs
				while(true) {
					if (nameNum == 1) {
						strcpy(groupNameBuf, pChildGroup->groupName);
					} else {
						sprintf(groupNameBuf, "%s_%d", pChildGroup->groupName, nameNum);
					}
					for(j=eaSize(&pGeoLayer->defs)-1; j>=0; --j) {
						GroupDef *pDef = pGeoLayer->defs[j];
						if (stricmp(pDef->name_str, groupNameBuf) == 0) {
							break;
						}
					}
					if (j < 0) {
						pcGroupName = groupNameBuf;
						break;
					}
					++nameNum;
				}

				if (!pParentGroup) {
					pParentGroup = pGeoLayer->defs[1];
					sprintf(idBuf, "%d,", pGeoLayer->defs[0]->children[0]->uid_in_parent);
					pcParentIDString = idBuf;
				}

				next_id = Convert_GetNextIdForGeoLayer(pGeoLayer);

				// Create the group data for the container
				pContainerGroup = StructCreate(parse_GroupChild);
				pContainerGroup->name = allocAddString(pcGroupName);
				pContainerGroup->name_uid = next_id++;
				pContainerGroup->uid_in_parent = next_id++;
				eaPush(&pParentGroup->children, pContainerGroup);

				// Create Def for the container
				pContainerDef = StructCreate(parse_GroupDef);
				pContainerDef->name_str = allocAddString(pcGroupName);
				pContainerDef->name_uid = pContainerGroup->name_uid;
				eaPush(&pGeoLayer->defs, pContainerDef);

				// Create Logical group for this case
				pPassedLogicalGroup = StructCreate(parse_LogicalGroup);
				pPassedLogicalGroup->group_name = StructAllocString(pcGroupName);
				eaPush(&pGeoLayer->defs[0]->logical_groups, pPassedLogicalGroup);

				pPassedParentGroup = pContainerDef;
				sprintf(idPassedBuf, "%s%d,", pcParentIDString, pContainerGroup->uid_in_parent);
				pcPassedParentIDString = idPassedBuf;
			}
		}

		if (pcGroupName) {
			eaPush(peaGroupNames, (char*)allocAddString(pcGroupName));
		}

		ConvertStaticEncounterGroup(pLayer, pChildGroup, pPassedParentGroup, pcPassedParentIDString, pPassedLogicalGroup, peaGroupNames, bMakeWorldData, peaModifiedEncLayers, peaOrigEncLayers, peaGeoLayers, peaModifiedGeoLayers, peaExtraFiles, pData, file);

		if (pcGroupName) {
			eaPop(peaGroupNames);
		}
	}
}

static void ConvertSaveTemplates(EncConvertData *pConvertData, FILE *file)
{
	ResourceLoaderStruct loadStruct = {0};
	int i;

	// Process created templates
	for(i=eaSize(&pConvertData->eaTemplateData)-1; i>=0; --i) {
		EncTemplateData *pData = pConvertData->eaTemplateData[i];
		ConvertProcessTemplate(pConvertData, pData);
	}

	// Save out created templates
	eaPush(&loadStruct.earrayOfStructs, NULL);

	for(i=eaSize(&pConvertData->eaTemplateData)-1; i>=0; --i) {
		EncTemplateData *pData = pConvertData->eaTemplateData[i];
		if (!pData->bPreExisting && eaSize(&pData->eaEncLayers)) {
			EncounterTemplate *pSaveTemplate = StructClone(parse_EncounterTemplate, pData->pTemplate);
			assert(pSaveTemplate);
			ConvertApplyTemplateAI(pSaveTemplate, pData->pTemplateAI);
			encounterTemplate_Optimize(pSaveTemplate);
			encounterTemplate_FixupMessages(pSaveTemplate);

			loadStruct.earrayOfStructs[0] = pSaveTemplate;
			langApplyEditorCopySingleFile( parse_EncounterTemplate, pSaveTemplate, true, false );
			SetUpResourceLoaderParse("EncounterTemplate", ParserGetTableSize(parse_EncounterTemplate), parse_EncounterTemplate, NULL);
			ParserWriteTextFile(pData->pTemplate->pcFilename, parse_ResourceLoaderStruct, &loadStruct, 0, 0);
			parse_ResourceLoaderStruct[0].name = NULL;

			fprintf(file, "Template: %s (%s)\n", pSaveTemplate->pcName, pSaveTemplate->pcFilename);
			fprintf(file, "   %d source defs\n", eaSize(&pData->eaSourceDefs));
			fprintf(file, "   %d layer defs\n", eaSize(&pData->eaLayerDefs));
			fprintf(file, "   %d layers\n", eaSize(&pData->eaEncLayers));
			fprintf(file, "\n");

			StructDestroy(parse_EncounterTemplate, pSaveTemplate);
		}
	}

}


static void ConvertEncounterDefsInternal(EncounterLayer ***peaEncLayers, FILE *file)
{
	DictionaryEArrayStruct *pTemplates = resDictGetEArrayStruct("EncounterTemplate");
	DictionaryEArrayStruct *pEncounters = resDictGetEArrayStruct("EncounterDef");
	char **eaGroupNames = NULL;
	EncConvertData convertData = {0};
	int i;
	int iNumCreated = 0, iNumLayers = 0, iNumSourceDefs = 0, iNumLayerDefs = 0, iNumNonCustomLayerDefs = 0, iNumInOneLayer = 0, iNumUnused = 0;

	// Scan encounter defs first
	for(i=eaSize(&pEncounters->ppReferents)-1; i>=0; --i) {
		EncounterDef *pDef = pEncounters->ppReferents[i];
		ConvertDataToTemplate(NULL, pDef, pDef, NULL, NULL, NULL, NULL, &convertData);
#ifdef JDJ_NEW_VERSION_CONVERT_DEC2010
		ConvertDataToTemplate(NULL, pDef, NULL, pDef, NULL, NULL, NULL, NULL, &convertData);
#endif
	}

	// Scan encounter layers
	eaPush(&eaGroupNames, "Encounters");
	for(i=eaSize(peaEncLayers)-1; i>=0; --i) {
		EncounterLayer *pEncLayer = (*peaEncLayers)[i];
		ConvertStaticEncounterGroup(pEncLayer, &pEncLayer->rootGroup, NULL, NULL, NULL, &eaGroupNames, false, NULL, NULL, NULL, NULL, NULL, &convertData, file);
	}

	ConvertSaveTemplates(&convertData, file);

	// Collect stats
	for(i=eaSize(&convertData.eaTemplateData)-1; i>=0; --i) {
		EncTemplateData *pData = convertData.eaTemplateData[i];

		if (pData->bPreExisting || !eaSize(&pData->eaLayerDefs)) {
			// No processing required if already existed or not generated
			continue;
		} 

		++iNumCreated;
		iNumLayerDefs += eaSize(&pData->eaLayerDefs);
		iNumSourceDefs += eaSize(&pData->eaSourceDefs);

		if (eaSize(&pData->eaSourceDefs)) {
			iNumNonCustomLayerDefs +=eaSize(&pData->eaLayerDefs);
		}
		if (eaSize(&pData->eaSourceDefs)) {
			if (!eaSize(&pData->eaEncLayers)) {
				++iNumUnused;
			}
		} else if (eaSize(&pData->eaEncLayers) == 1) {
			++iNumInOneLayer;
		}
	}

	iNumLayers += eaSize(&convertData.eaEncLayers);

	fprintf(file, "** Created %d templates\n", iNumCreated);
	fprintf(file, "** Total %d encounter defs\n", eaSize(&pEncounters->ppReferents));
	fprintf(file, "** Replaced %d source defs\n", iNumSourceDefs);
	fprintf(file, "** Replaced %d layer defs\n", iNumLayerDefs);
	fprintf(file, "** NonCustom %d layer defs\n", iNumNonCustomLayerDefs);
	fprintf(file, "** Spanned %d layers\n", iNumLayers);
	fprintf(file, "** Unused templates = %d\n", iNumUnused);

	eaDestroy(&eaGroupNames);
}


AUTO_COMMAND ACMD_SERVERCMD;
char* ConvertEncounterDefs(void)
{
	EncounterLayer **eaEncLayers = NULL;
	FILE *file;

	AssetLoadEncLayers(&eaEncLayers);

	file = fopen("c:\\ConvertEncounterDefs.txt", "w");

	ConvertEncounterDefsInternal(&eaEncLayers, file);

	fclose(file);

	AssetCleanupEncLayers(&eaEncLayers);

	return "Convert notes written to 'c:\\ConvertEncounterDefs.txt'";
}


static void ConvertEncounters(EncounterLayer ***peaEncLayers, EncounterLayer ***peaModifiedEncLayers, EncounterLayer ***peaOrigEncLayers, LibFileLoad ***peaGeoLayers, LibFileLoad ***peaModifiedGeoLayers, char ***peaExtraFiles, const char* pchLevelMapVar, bool bUsePlayerLevel, FILE *file)
{
	EncConvertData convertData = {0};
	char **eaGroupNames = NULL;
	int i;
	int iNumCreated = 0, iNumLayers = 0, iNumLayerDefs = 0;

	convertData.pchLevelMapVar = pchLevelMapVar;
	convertData.bUsePlayerLevel = bUsePlayerLevel;

	// Scan encounter layers to create new templates
	eaPush(&eaGroupNames, "Encounters");
	for(i=eaSize(peaEncLayers)-1; i>=0; --i) {
		EncounterLayer *pEncLayer = (*peaEncLayers)[i];

		// Apply layer level and force team size to map
		if ((pEncLayer->layerLevel > 0) || (pEncLayer->forceTeamSize > 0)) {
			LibFileLoad *pGeoLayer;
			ZoneMapInfo *pZone;
			bool bChanged = false;
			Vec3 vPos = {0,0,0};

			Convert_GetSamplePositionForEncGroup(&pEncLayer->rootGroup, vPos);
			pGeoLayer = Convert_GetGeoLayerForEncLayer(pEncLayer, vPos, peaGeoLayers, peaExtraFiles, NULL);
			pZone = Convert_GetZoneForLayer(pGeoLayer);

			fprintf(file, "Zone: %s\n", pZone->filename);

			if ((pEncLayer->layerLevel > 0) && (pZone->level != pEncLayer->layerLevel)) {
				pZone->level = pEncLayer->layerLevel;
				fprintf(file, "    Set zone level to %d\n", pEncLayer->layerLevel);
				bChanged = true;
			}
			if ((pEncLayer->forceTeamSize > 0) && (pZone->force_team_size != pEncLayer->forceTeamSize)) {
				pZone->force_team_size = pEncLayer->forceTeamSize;
				fprintf(file, "    Set force team size to %d\n", pEncLayer->forceTeamSize);
				bChanged = true;
			}
			fprintf(file, "\n");

			if (bChanged) {
				Convert_SaveChangedZone(pZone);
				eaPushUnique(peaExtraFiles, (char*)pZone->filename);
			}
		}

		ConvertStaticEncounterGroup(pEncLayer, &pEncLayer->rootGroup, NULL, NULL, NULL, &eaGroupNames, false, NULL, NULL, NULL, NULL, NULL, &convertData, file);
	}

	// Save off template modified during this run
	ConvertSaveTemplates(&convertData, file);

	// Collect stats
	for(i=eaSize(&convertData.eaTemplateData)-1; i>=0; --i) {
		EncTemplateData *pData = convertData.eaTemplateData[i];

		if (pData->bPreExisting) {
			// No processing required if already existed
			continue;
		} 

		++iNumCreated;
		iNumLayerDefs += eaSize(&pData->eaLayerDefs);
	}

	iNumLayers += eaSize(&convertData.eaEncLayers);

	fprintf(file, "** Created %d templates\n", iNumCreated);
	fprintf(file, "** Replaced %d layer defs\n", iNumLayerDefs);
	fprintf(file, "** Spanning %d layers\n", iNumLayers);

	// Scan encounter layers to rework encounters
	for(i=eaSize(peaEncLayers)-1; i>=0; --i) {
		EncounterLayer *pEncLayer = (*peaEncLayers)[i];
		ConvertStaticEncounterGroup(pEncLayer, &pEncLayer->rootGroup, NULL, NULL, NULL, &eaGroupNames, true, peaModifiedEncLayers, peaOrigEncLayers, peaGeoLayers, peaModifiedGeoLayers, peaExtraFiles, &convertData, file);
	}

	eaDestroy(&eaGroupNames);
}

static char* ConvertEncounterLayers_LevelFromSpecified(const char* pchMapVar, bool bUsePlayerLevel)
{
	EncounterLayer **eaModifiedEncLayers = NULL;
	EncounterLayer **eaOrigEncLayers = NULL;
	LibFileLoad **eaGeoLayers = NULL;
	LibFileLoad **eaModifiedGeoLayers = NULL;
	char **eaExtraFiles = NULL;
	FILE *file;

	AssetLoadGeoLayers(&eaGeoLayers);

	AssetPutEncLayersInEditMode(&g_EncounterMasterLayer->encLayers);
	AssetPutGeoLayersInEditMode(&eaGeoLayers);

	file = fopen("c:\\ConvertEncounters.txt", "w");

	ConvertEncounters(&g_EncounterMasterLayer->encLayers, &eaModifiedEncLayers, &eaOrigEncLayers, &eaGeoLayers, &eaModifiedGeoLayers, &eaExtraFiles, pchMapVar, bUsePlayerLevel, file);

	if (eaSize(&eaExtraFiles) > 0) {
		int i;
		fprintf(file, "\nEXTRA FILES\n");
		eaQSort(eaExtraFiles, Convert_CompareStrings);
		for(i=eaSize(&eaExtraFiles)-1; i>=0; --i) {
			fprintf(file, "  Extra file: %s\n", eaExtraFiles[i]);
		}
		fprintf(file, "\n");
	}

	fclose(file);

	printf("** Saving %d enc layers\n", eaSize(&eaModifiedEncLayers));
	printf("** Saving %d geo layers\n", eaSize(&eaModifiedGeoLayers));
	AssetSaveEncLayers(&eaModifiedEncLayers, &eaOrigEncLayers, true);
	AssetSaveGeoLayers(&eaModifiedGeoLayers, true);

	AssetCleanupGeoLayers(&eaGeoLayers);

	eaDestroy(&eaOrigEncLayers);
	eaDestroy(&eaModifiedGeoLayers);
	eaDestroy(&eaExtraFiles);
	eaDestroyStruct(&eaModifiedEncLayers, parse_EncounterLayer);

	return "Encounter convert notes written to 'c:\\ConvertEncounters.txt'";
}

AUTO_COMMAND ACMD_SERVERCMD;
char* ConvertEncounterLayers_LevelFromMapVar(Entity *pPlayer, const char* pchMapVar)
{
	int iPartitionIdx = entGetPartitionIdx(pPlayer);

	if (!mapvariable_GetByName(iPartitionIdx, pchMapVar))  // Using Static Check to really mean "never runs at a real time"
	{
		printf("Cannot find map variable named, '%s'\n", pchMapVar);
		return "Unable to convert encounter layers.  Invalid argument.";
	}
	return ConvertEncounterLayers_LevelFromSpecified(pchMapVar, false);
}

AUTO_COMMAND ACMD_SERVERCMD;
char* ConvertEncounterLayers_LevelFromPlayerLevel(void)
{
	return ConvertEncounterLayers_LevelFromSpecified(NULL, true);
}

AUTO_COMMAND ACMD_SERVERCMD;
char* ConvertEncounterLayers(void)
{
	EncounterLayer **eaModifiedEncLayers = NULL;
	EncounterLayer **eaOrigEncLayers = NULL;
	LibFileLoad **eaGeoLayers = NULL;
	LibFileLoad **eaModifiedGeoLayers = NULL;
	char **eaExtraFiles = NULL;
	FILE *file;

	AssetLoadGeoLayers(&eaGeoLayers);

	AssetPutEncLayersInEditMode(&g_EncounterMasterLayer->encLayers);
	AssetPutGeoLayersInEditMode(&eaGeoLayers);

	file = fopen("c:\\ConvertEncounters.txt", "w");

	ConvertEncounters(&g_EncounterMasterLayer->encLayers, &eaModifiedEncLayers, &eaOrigEncLayers, &eaGeoLayers, &eaModifiedGeoLayers, &eaExtraFiles, NULL, false, file);

	if (eaSize(&eaExtraFiles) > 0) {
		int i;
		fprintf(file, "\nEXTRA FILES\n");
		eaQSort(eaExtraFiles, Convert_CompareStrings);
		for(i=eaSize(&eaExtraFiles)-1; i>=0; --i) {
			fprintf(file, "  Extra file: %s\n", eaExtraFiles[i]);
		}
		fprintf(file, "\n");
	}

	fclose(file);

	printf("** Saving %d enc layers\n", eaSize(&eaModifiedEncLayers));
	printf("** Saving %d geo layers\n", eaSize(&eaModifiedGeoLayers));
	AssetSaveEncLayers(&eaModifiedEncLayers, &eaOrigEncLayers, true);
	AssetSaveGeoLayers(&eaModifiedGeoLayers, true);

	AssetCleanupGeoLayers(&eaGeoLayers);

	eaDestroy(&eaOrigEncLayers);
	eaDestroy(&eaModifiedGeoLayers);
	eaDestroy(&eaExtraFiles);
	eaDestroyStruct(&eaModifiedEncLayers, parse_EncounterLayer);

	return "Encounter convert notes written to 'c:\\ConvertEncounters.txt'";
}


AUTO_COMMAND ACMD_SERVERCMD;
char* ConvertPatrolRoutes(void)
{
	EncounterLayer **eaModifiedEncLayers = NULL;
	EncounterLayer **eaOrigEncLayers = NULL;
	LibFileLoad **eaGeoLayers = NULL;
	LibFileLoad **eaModifiedGeoLayers = NULL;
	char **eaExtraFiles = NULL;
	FILE *file;

	AssetLoadGeoLayers(&eaGeoLayers);

	AssetPutEncLayersInEditMode(&g_EncounterMasterLayer->encLayers);
	AssetPutGeoLayersInEditMode(&eaGeoLayers);

	file = fopen("c:\\ConvertPatrols.txt", "w");

	ConvertPatrolRoutesInternal(&g_EncounterMasterLayer->encLayers, &eaModifiedEncLayers, &eaOrigEncLayers, &eaGeoLayers, &eaModifiedGeoLayers, &eaExtraFiles, file);

	if (eaSize(&eaExtraFiles) > 0) {
		int i;
		fprintf(file, "\nEXTRA FILES\n");
		eaQSort(eaExtraFiles, Convert_CompareStrings);
		for(i=eaSize(&eaExtraFiles)-1; i>=0; --i) {
			fprintf(file, "  Extra file: %s\n", eaExtraFiles[i]);
		}
		fprintf(file, "\n");
	}

	fclose(file);

	printf("** Saving %d enc layers\n", eaSize(&eaModifiedEncLayers));
	printf("** Saving %d geo layers\n", eaSize(&eaModifiedGeoLayers));
	AssetSaveEncLayers(&eaModifiedEncLayers, &eaOrigEncLayers, true);
	AssetSaveGeoLayers(&eaModifiedGeoLayers, true);

	AssetCleanupGeoLayers(&eaGeoLayers);

	eaDestroy(&eaOrigEncLayers);
	eaDestroy(&eaModifiedGeoLayers);
	eaDestroy(&eaExtraFiles);
	eaDestroyStruct(&eaModifiedEncLayers, parse_EncounterLayer);

	return "Patrol route convert notes written to 'c:\\ConvertPatrols.txt'";
}


AUTO_COMMAND ACMD_SERVERCMD;
char* ConvertLayersTest(Entity *pPlayer)
{
	EncounterLayer **eaEncLayers = NULL;
	EncounterLayer **eaModifiedEncLayers = NULL;
	EncounterLayer **eaOrigEncLayers = NULL;
	LibFileLoad **eaGeoLayers = NULL;
	LibFileLoad **eaModifiedGeoLayers = NULL;
	LibFileLoad **eaLibraryLayers = NULL;
	char **eaExtraFiles = NULL;
	FILE *file;
	int iPartitionIdx = entGetPartitionIdx(pPlayer);

	AssetLoadEncLayers(&eaEncLayers);
	AssetLoadGeoLayers(&eaGeoLayers);
	AssetLoadObjectLibraries(&eaLibraryLayers);

	AssetPutEncLayersInEditMode(&eaEncLayers);

	file = fopen("c:\\ConvertLayers.txt", "w");

	//ScanForDuplicateScopeNames(eaGeoLayers, file);

#ifdef SDANGELO_CODE_TO_REMOVE_WHEN_DONE_WITH_ENCOUNTER_LAYER_CONVERSION
	ConvertClickables(&eaEncLayers, &eaModifiedEncLayers, &eaOrigEncLayers, &eaGeoLayers, &eaModifiedGeoLayers, &eaLibraryLayers, &eaExtraFiles, file);
	ConvertVolumes(&eaEncLayers, &eaModifiedEncLayers, &eaOrigEncLayers, &eaGeoLayers, &eaModifiedGeoLayers, &eaExtraFiles, file);
	ScanForPatrolRoutes(&eaEncLayers, file);
	ConvertPatrolRoutes(&eaEncLayers, &eaModifiedEncLayers, &eaOrigEncLayers, &eaGeoLayers, &eaModifiedGeoLayers, &eaExtraFiles, file);
	ConvertRanks(iPartitionIdx, &eaEncLayers, &eaModifiedEncLayers, &eaOrigEncLayers, file);
#endif

	ConvertEncounterDefsInternal(&eaEncLayers, file);
	ConvertEncounters(&eaEncLayers, &eaModifiedEncLayers, &eaOrigEncLayers, &eaGeoLayers, &eaModifiedGeoLayers, &eaExtraFiles, NULL, false, file);

	//ScanForDuplicateScopeNames(eaModifiedGeoLayers, file);

	if (eaSize(&eaExtraFiles) > 0) {
		int i;
		fprintf(file, "\nEXTRA FILES\n");
		eaQSort(eaExtraFiles, Convert_CompareStrings);
		for(i=eaSize(&eaExtraFiles)-1; i>=0; --i) {
			fprintf(file, "  Extra file: %s\n", eaExtraFiles[i]);
		}
		fprintf(file, "\n");
	}

	fclose(file);

	printf("** Saving %d enc layers\n", eaSize(&eaModifiedEncLayers));
	printf("** Saving %d geo layers\n", eaSize(&eaModifiedGeoLayers));
	AssetSaveEncLayers(&eaModifiedEncLayers, &eaOrigEncLayers, true);
	AssetSaveGeoLayers(&eaModifiedGeoLayers, true);

	AssetCleanupEncLayers(&eaEncLayers);
	AssetCleanupGeoLayers(&eaGeoLayers);
	AssetCleanupObjectLibraries(&eaLibraryLayers);

	eaDestroy(&eaModifiedEncLayers);
	eaDestroy(&eaModifiedGeoLayers);
	eaDestroy(&eaExtraFiles);
	eaDestroyStruct(&eaOrigEncLayers, parse_EncounterLayer);

	return "Layer convert notes written to 'c:\\ConvertLayers.txt'";
}

typedef struct EncScanNumCount {
	int value;
	int count;
} EncScanNumCount;

typedef struct EncScanStringCount {
	char *value;
	int count;
} EncScanStringCount;

typedef struct EncScanData {
	int iNumLayers;
	EncounterDef **eaEncDefs;
	EncScanStringCount **eaDefSpawnCond;
	EncScanStringCount **eaDefActorSpawnCond;
	EncScanStringCount **eaDefSuccessCond;
	EncScanStringCount **eaDefFailureCond;
	EncScanStringCount **eaSpawnCond;
	EncScanStringCount **eaActorSpawnCond;
	EncScanStringCount **eaSuccessCond;
	EncScanStringCount **eaFailureCond;
	EncScanNumCount **eaSpawnRadius;
	EncScanNumCount **eaLockoutRadius;
	EncScanNumCount **eaSpawnChance;
	EncScanNumCount **eaRespawnTimer;

	int iNumEncounters;
	int iNumActors;
	int iNumBaseDef;
	int iNumNoOverride;
	int iNumTotalOverride;

	int iNumLevelOverride;
	int iNumCritterGroupOverride;
	int iNumFactionOverride;
	int iNumSpawnAnimOverride;

	int iNumNewData;
	int iNumNoDespawn;
	int iNumSpawnWeight;
	int iNumNoSnap;
	int iNumPatrolRoute;

	int iNumTimesActorsAdded;
	int iNumActorsAdded;
	int iNumTimesActorsChanged;
	int iNumActorsChanged;
	int iNumTimesActorsDeleted;
	int iNumActorsDeleted;
	int iNumTotalOverrideActors;
	int iNumActorsSame;

	int iNumActorInfoOverride;
	int iNumActorAIOverride;
	int iNumActorPositionOverride;
	int iNumActorDisableSpawnOverride;
	int iNumActorUseBossOverride;
	int iNumActorNameOverride;
	int iNumActorDispNameOverride;

	int iNumActorCritterDefOverride;
	int iNumActorCritterGroupOverride;
	int iNumActorCritterFactionOverride;
	int iNumActorRankOverride;
	int iNumActorSpawnAnimOverride;
	int iNumActorContactOverride;
	int iNumActorSpawnCondOverride;
	int iNumActorInteractOverride;
	int iNumActorFSMOverride;
	int iNumActorFSMVarsOverride;

	int iNumTActors;
	int iNumTActorsDeleted;
	int iNumTActorsChanged;
	int iNumTActorsSame;
	int iNumTActorsAdded;

	int iNumTAmbushOverride;
	int iNumTLevelOverride;
	int iNumTPlayerLevelOverride;
	int iNumTCritterGroupOverride;
	int iNumTFactionOverride;
	int iNumTGangOverride;
	int iNumTSpawnRadiusOverride;
	int iNumTLockoutRadiusOverride;
	int iNumTRespawnTimerOverride;
	int iNumTSpawnChanceOverride;
	int iNumTSpawnPerPlayerOverride;
	int iNumTSpawnAnimOverride;
	int iNumTSpawnCondOverride;
	int iNumTSuccessCondOverride;
	int iNumTFailCondOverride;
	int iNumTWaveDelayOverride;
	int iNumTWaveIntervalOverride;
	int iNumTWaveCondOverride;
	int iNumTActionsOverride;
	int iNumTJobsOverride;

	int iNumTActorInfoOverride;
	int iNumTActorAIOverride;
	int iNumTActorPositionOverride;
	int iNumTActorDisableSpawnOverride;
	int iNumTActorUseBossOverride;
	int iNumTActorNameOverride;
	int iNumTActorDispNameOverride;

	int iNumTActorCritterDefOverride;
	int iNumTActorCritterGroupOverride;
	int iNumTActorCritterFactionOverride;
	int iNumTActorRankOverride;
	int iNumTActorSpawnAnimOverride;
	int iNumTActorContactOverride;
	int iNumTActorSpawnCondOverride;
	int iNumTActorInteractOverride;
	int iNumTActorFSMOverride;
	int iNumTActorFSMVarsOverride;

	int iNumDefs;
	int iNumDefAmbush;
	int iNumDefLevel;
	int iNumDefPlayerLevel;
	int iNumDefCritterGroup;
	int iNumDefCritterFaction;
	int iNumDefGang;
	int iNumDefSpawnCond;
	int iNumDefSpawnPerPlayer;
	int iNumDefSpawnRadius;
	int iNumDefSpawnChance;
	int iNumDefLockoutRadius;
	int iNumDefRespawnTimer;
	int iNumDefSpawnAnim;
	int iNumDefSuccessCond;
	int iNumDefFailCond;
	int iNumDefWaveCond;
	int iNumDefWaveInterval;
	int iNumDefWaveDelay;
	int iNumDefJobs;
	int iNumDefActions;

	int iNumDefActors;
	int iNumDefActorDispName;
	int iNumDefActorDisable;
	int iNumDefActorBoss;
	int iNumDefActorCritterGroup;
	int iNumDefActorCritterDef;
	int iNumDefActorFaction;
	int iNumDefActorRank;
	int iNumDefActorSpawnAnim;
	int iNumDefActorContact;
	int iNumDefActorSpawnCond;
	int iNumDefActorInteract;
	int iNumDefActorFSM;
	int iNumDefActorVars;
} EncScanData;

static void ScanEncAddNumCount(EncScanNumCount ***peaCounts, int value)
{
	EncScanNumCount *pEntry;
	int i;

	for(i=eaSize(peaCounts)-1; i>=0; --i) {
		if ((*peaCounts)[i]->value == value) {
			++((*peaCounts)[i]->count);
			return;
		}
	}

	pEntry = calloc(1,sizeof(EncScanNumCount));
	pEntry->count = 1;
	pEntry->value = value;
	eaPush(peaCounts, pEntry);
}

static int ScanEncCompareNumCount(const EncScanNumCount **left, const EncScanNumCount **right)
{
	return ((*left)->count - (*right)->count);
}

static void ScanEncAddStringCount(EncScanStringCount ***peaCounts, char *value)
{
	EncScanStringCount *pEntry;
	int i;

	for(i=eaSize(peaCounts)-1; i>=0; --i) {
		if (stricmp((*peaCounts)[i]->value, value) == 0) {
			++((*peaCounts)[i]->count);
			return;
		}
	}

	pEntry = calloc(1,sizeof(EncScanStringCount));
	pEntry->count = 1;
	pEntry->value = (char*)allocAddString(value);
	eaPush(peaCounts, pEntry);
}

static int ScanEncCompareStringCount(const EncScanStringCount **left, const EncScanStringCount **right)
{
	return ((*left)->count - (*right)->count);
}

static OldActor *ScanEncLayerActorFound(EncounterDef *pDef, int id)
{
	int i;

	for(i=eaSize(&pDef->actors)-1; i>=0; --i) {
		if (pDef->actors[i]->uniqueID == id) {
			return pDef->actors[i];
		}
	}
	return NULL;
}


static void ScanEncLayerGroup(OldStaticEncounterGroup *pGroup, EncScanData *pData)
{
	int i,j;

	// Recurse into child groups
	for(i=eaSize(&pGroup->childList)-1; i>=0; --i) {
		ScanEncLayerGroup(pGroup->childList[i], pData);
	}

	// Check encounters
	for(i=eaSize(&pGroup->staticEncList)-1; i>=0; --i) {
		OldStaticEncounter *pEnc = pGroup->staticEncList[i];
		EncounterDef *pBaseDef, *pOverrideDef;
		bool bTotalOverride = false;
		bool bNewData = false;

		// Big Picture
		++pData->iNumEncounters;
		pBaseDef = GET_REF(pEnc->baseDef);
		if (pBaseDef) {
			++pData->iNumBaseDef;
			eaPushUnique(&pData->eaEncDefs, pBaseDef);
		}
		pOverrideDef = pEnc->defOverride;
		if (!pOverrideDef) {
			++pData->iNumNoOverride;
		}
		if (!pBaseDef || (pOverrideDef && pOverrideDef->name)) {
			++pData->iNumTotalOverride;
			bTotalOverride = true;
		}

		// Enc level settings
		if (pEnc->noDespawn) {
			++pData->iNumNoDespawn;
			bNewData = true;
		}
		if (pEnc->spawnWeight) {
			++pData->iNumSpawnWeight;
			bNewData = true;
		}
		if (pEnc->bNoSnapToGround) {
			++pData->iNumNoSnap;
			bNewData = true;
		}
		if (pEnc->patrolRouteName) {
			++pData->iNumPatrolRoute;
			bNewData = true;
		}
		if (bNewData) {
			++pData->iNumNewData;
		}

		if (!bTotalOverride) {
			// Enc Level overrides
			if (pEnc->minLevel || pEnc->maxLevel) {
				++pData->iNumLevelOverride;
			}
			if (GET_REF(pEnc->encCritterGroup)) {
				++pData->iNumCritterGroupOverride;
			}
			if (GET_REF(pEnc->encFaction)) {
				++pData->iNumFactionOverride;
			}
			if (pEnc->spawnAnim) {
				++pData->iNumSpawnAnimOverride;
			}

			// Actor tracking
			if (eaiSize(&pEnc->delActorIDs) > 0) {
				++pData->iNumTimesActorsDeleted;
				pData->iNumActorsDeleted += eaiSize(&pEnc->delActorIDs);
				pData->iNumActors -= eaiSize(&pEnc->delActorIDs);
			}
			if (pBaseDef) {
				pData->iNumActors += eaSize(&pBaseDef->actors);
			}
			if (pOverrideDef) {
				bool bAdded = false;
				bool bChanged = false;
				for(j=eaSize(&pBaseDef->actors)-1; j>=0; --j) {
					if (!ScanEncLayerActorFound(pOverrideDef, pBaseDef->actors[j]->uniqueID)) {
						if (eaiFind(&pEnc->delActorIDs, pBaseDef->actors[j]->uniqueID) >= 0) {
							++pData->iNumActorsSame;
						}
					}
				}
				for(j=eaSize(&pOverrideDef->actors)-1; j>=0; --j) {
					OldActor *pBaseActor = ScanEncLayerActorFound(pBaseDef, pOverrideDef->actors[j]->uniqueID);
					OldActor *pOverActor = pOverrideDef->actors[j];
					if (!pBaseActor) {
						++pData->iNumActorsAdded;
						++pData->iNumActors;
						bAdded = true;
					} else {
						++pData->iNumActorsChanged;
						bChanged = true;

						if (pOverActor->details.info) {
							++pData->iNumActorInfoOverride;

							if (GET_REF(pOverActor->details.info->critterDef) != GET_REF(pBaseActor->details.info->critterDef)) {
								++pData->iNumActorCritterDefOverride;
							}
							if (GET_REF(pOverActor->details.info->critterGroup) != GET_REF(pBaseActor->details.info->critterGroup)) {
								++pData->iNumActorCritterGroupOverride;
							}
							if (GET_REF(pOverActor->details.info->critterFaction) != GET_REF(pBaseActor->details.info->critterFaction)) {
								++pData->iNumActorCritterFactionOverride;
							}
							if (GET_REF(pOverActor->details.info->contactScript) != GET_REF(pBaseActor->details.info->contactScript)) {
								++pData->iNumActorContactOverride;
							}
							if ((pOverActor->details.info->pcCritterRank != pBaseActor->details.info->pcCritterRank) || (pOverActor->details.info->pcCritterSubRank != pBaseActor->details.info->pcCritterSubRank)) {
								++pData->iNumActorRankOverride;
							}
							if ((!pOverActor->details.info->pchSpawnAnim && pBaseActor->details.info->pchSpawnAnim) || (pOverActor->details.info->pchSpawnAnim && !pBaseActor->details.info->pchSpawnAnim) ||
								(pOverActor->details.info->pchSpawnAnim && pBaseActor->details.info->pchSpawnAnim && (stricmp(pOverActor->details.info->pchSpawnAnim, pBaseActor->details.info->pchSpawnAnim) != 0))) {
								++pData->iNumActorSpawnAnimOverride;
							}
							if (exprCompare(pOverActor->details.info->spawnCond, pBaseActor->details.info->spawnCond) != 0) {
								char *pcText = exprGetCompleteString(pOverActor->details.info->spawnCond);
								ScanEncAddStringCount(&pData->eaActorSpawnCond, pcText);
								++pData->iNumActorSpawnCondOverride;
							}
							if (StructCompare(parse_OldInteractionProperties, &pOverActor->details.info->oldActorInteractProps, &pBaseActor->details.info->oldActorInteractProps,0,0,0) != 0) {
								++pData->iNumActorInteractOverride;
							}
						}
						if (pOverActor->details.aiInfo) {
							++pData->iNumActorAIOverride;

							if (GET_REF(pOverActor->details.aiInfo->hFSM) != GET_REF(pBaseActor->details.aiInfo->hFSM)) {
								++pData->iNumActorFSMOverride;
							}
							if (eaSize(&pOverActor->details.aiInfo->actorVars) == eaSize(&pBaseActor->details.aiInfo->actorVars)) {
								for(j=eaSize(&pOverActor->details.aiInfo->actorVars)-1; j>=0; --j) {
									if (StructCompare(parse_OldEncounterVariable, pOverActor->details.aiInfo->actorVars[j], pBaseActor->details.aiInfo->actorVars[j],0,0,0) != 0) {
										++pData->iNumActorFSMVarsOverride;
									}
								}
							} else {
								++pData->iNumActorFSMVarsOverride;
							}
						}
						if (pOverActor->details.position) {
							++pData->iNumActorPositionOverride;
						}
						if (pOverActor->disableSpawn != ActorScalingFlag_Inherited) {
							++pData->iNumActorDisableSpawnOverride;
						}
						if (pOverActor->useBossBar != ActorScalingFlag_Inherited) {
							++pData->iNumActorUseBossOverride;
						}
						if (pOverActor->name) {
							++pData->iNumActorNameOverride;
						}
						if (GET_REF(pOverActor->displayNameMsg.hMessage)) {
							++pData->iNumActorDispNameOverride;
						}
					}
				}
				if (bAdded) {
					++pData->iNumTimesActorsAdded;
				}
				if (bChanged) {
					++pData->iNumTimesActorsChanged;
				}
			}
		} else if (pOverrideDef) {
			pData->iNumActors += eaSize(&pOverrideDef->actors);
			pData->iNumTotalOverrideActors += eaSize(&pOverrideDef->actors);

			if (pBaseDef) {
				// Enc Level overrides
				if (pBaseDef->bAmbushEncounter != pOverrideDef->bAmbushEncounter) {
					++pData->iNumTAmbushOverride;
				}
				if ((pBaseDef->minLevel != pOverrideDef->minLevel) || (pBaseDef->maxLevel != pOverrideDef->maxLevel)) {
					++pData->iNumTLevelOverride;
				}
				if (pBaseDef->bUsePlayerLevel != pOverrideDef->bUsePlayerLevel) {
					++pData->iNumTPlayerLevelOverride;
				}
				if (GET_REF(pBaseDef->critterGroup) != GET_REF(pOverrideDef->critterGroup)) {
					++pData->iNumTCritterGroupOverride;
				}
				if (GET_REF(pBaseDef->faction) != GET_REF(pOverrideDef->faction)) {
					++pData->iNumTFactionOverride;
				}
				if (pBaseDef->gangID != pOverrideDef->gangID) {
					++pData->iNumTGangOverride;
				}
				if (pBaseDef->spawnRadius != pOverrideDef->spawnRadius) {
					++pData->iNumTSpawnRadiusOverride;
					ScanEncAddNumCount(&pData->eaSpawnRadius, (int)pBaseDef->spawnRadius);
				}
				if (pBaseDef->lockoutRadius != pOverrideDef->lockoutRadius) {
					++pData->iNumTLockoutRadiusOverride;
					ScanEncAddNumCount(&pData->eaLockoutRadius, (int)pBaseDef->lockoutRadius);
				}
				if (pBaseDef->respawnTimer != pOverrideDef->respawnTimer) {
					++pData->iNumTRespawnTimerOverride;
					ScanEncAddNumCount(&pData->eaRespawnTimer, (int)pBaseDef->respawnTimer);
				}
				if (pBaseDef->spawnChance != pOverrideDef->spawnChance) {
					++pData->iNumTSpawnChanceOverride;
					ScanEncAddNumCount(&pData->eaSpawnChance, (int)pBaseDef->spawnChance);
				}
				if (pBaseDef->bCheckSpawnCondPerPlayer != pOverrideDef->bCheckSpawnCondPerPlayer) {
					++pData->iNumTSpawnPerPlayerOverride;
				}
				if ((!pBaseDef->spawnAnim && pOverrideDef->spawnAnim) || (pBaseDef->spawnAnim && !pOverrideDef->spawnAnim) ||
					(pBaseDef->spawnAnim && pOverrideDef->spawnAnim && (stricmp(pBaseDef->spawnAnim, pOverrideDef->spawnAnim) != 0))) {
					++pData->iNumTSpawnAnimOverride;
				}
				if (exprCompare(pBaseDef->spawnCond, pOverrideDef->spawnCond) != 0) {
					char *pcText = exprGetCompleteString(pOverrideDef->spawnCond);
					ScanEncAddStringCount(&pData->eaSpawnCond, pcText);
					++pData->iNumTSpawnCondOverride;
				}
				if (exprCompare(pBaseDef->successCond, pOverrideDef->successCond) != 0) {
					char *pcText = exprGetCompleteString(pOverrideDef->successCond);
					ScanEncAddStringCount(&pData->eaSuccessCond, pcText);
					++pData->iNumTSuccessCondOverride;
				}
				if (exprCompare(pBaseDef->failCond, pOverrideDef->failCond) != 0) {
					char *pcText = exprGetCompleteString(pOverrideDef->failCond);
					ScanEncAddStringCount(&pData->eaFailureCond, pcText);
					++pData->iNumTFailCondOverride;
				}
				if ((pBaseDef->waveDelayMin != pOverrideDef->waveDelayMin) || (pBaseDef->waveDelayMax != pOverrideDef->waveDelayMax)) {
					++pData->iNumTWaveDelayOverride;
				}
				if (pBaseDef->waveInterval != pOverrideDef->waveInterval) {
					++pData->iNumTWaveIntervalOverride;
				}
				if (exprCompare(pBaseDef->waveCond, pOverrideDef->waveCond) != 0) {
					++pData->iNumTWaveCondOverride;
				}

				if (eaSize(&pBaseDef->actions) == eaSize(&pOverrideDef->actions)) {
					for(j=eaSize(&pBaseDef->actions)-1; j>=0; --j) {
						if (StructCompare(parse_OldEncounterAction, pBaseDef->actions[j], pOverrideDef->actions[j], 0, 0, 0) != 0) {
							++pData->iNumTActionsOverride;
							break;
						}
					}
				} else {
					++pData->iNumTActionsOverride;
				}

				if (eaSize(&pBaseDef->encJobs) == eaSize(&pOverrideDef->encJobs)) {
					for(j=eaSize(&pBaseDef->encJobs)-1; j>=0; --j) {
						if (StructCompare(parse_AIJobDesc, pBaseDef->encJobs[j], pOverrideDef->encJobs[j], 0, 0, 0) != 0) {
							++pData->iNumTJobsOverride;
							break;
						}
					}
				} else {
					++pData->iNumTJobsOverride;
				}

				// Actor Compares
				for(j=eaSize(&pBaseDef->actors)-1; j>=0; --j) {
					if (!ScanEncLayerActorFound(pOverrideDef, pBaseDef->actors[j]->uniqueID)) {
						++pData->iNumTActorsDeleted;
					}
				}
				for(j=eaSize(&pOverrideDef->actors)-1; j>=0; --j) {
					OldActor *pOverActor = pOverrideDef->actors[j];
					OldActor *pBaseActor = ScanEncLayerActorFound(pBaseDef, pOverActor->uniqueID);
					++pData->iNumTActors;
					if (!pBaseActor) {
						++pData->iNumTActorsAdded;
					} else {
						if (StructCompare(parse_OldActor, pOverActor, pBaseActor, 0, 0, 0) == 0) {
							++pData->iNumTActorsSame;
						} else {
							++pData->iNumTActorsChanged;

							if (pOverActor->details.info) {
								++pData->iNumTActorInfoOverride;

								if (GET_REF(pOverActor->details.info->critterDef) != GET_REF(pBaseActor->details.info->critterDef)) {
									++pData->iNumTActorCritterDefOverride;
								}
								if (GET_REF(pOverActor->details.info->critterGroup) != GET_REF(pBaseActor->details.info->critterGroup)) {
									++pData->iNumTActorCritterGroupOverride;
								}
								if (GET_REF(pOverActor->details.info->critterFaction) != GET_REF(pBaseActor->details.info->critterFaction)) {
									++pData->iNumTActorCritterFactionOverride;
								}
								if (GET_REF(pOverActor->details.info->contactScript) != GET_REF(pBaseActor->details.info->contactScript)) {
									++pData->iNumTActorContactOverride;
								}
								if ((pOverActor->details.info->pcCritterRank != pBaseActor->details.info->pcCritterRank) || (pOverActor->details.info->pcCritterSubRank != pBaseActor->details.info->pcCritterSubRank)) {
									++pData->iNumTActorRankOverride;
								}
								if ((!pOverActor->details.info->pchSpawnAnim && pBaseActor->details.info->pchSpawnAnim) || (pOverActor->details.info->pchSpawnAnim && !pBaseActor->details.info->pchSpawnAnim) ||
									(pOverActor->details.info->pchSpawnAnim && pBaseActor->details.info->pchSpawnAnim && (stricmp(pOverActor->details.info->pchSpawnAnim, pBaseActor->details.info->pchSpawnAnim) != 0))) {
										++pData->iNumTActorSpawnAnimOverride;
								}
								if (exprCompare(pOverActor->details.info->spawnCond, pBaseActor->details.info->spawnCond) != 0) {
									++pData->iNumTActorSpawnCondOverride;
								}
								if (StructCompare(parse_OldInteractionProperties, &pOverActor->details.info->oldActorInteractProps, &pBaseActor->details.info->oldActorInteractProps,0,0,0) != 0) {
										++pData->iNumTActorInteractOverride;
								}
							}
							if (pOverActor->details.aiInfo) {
								++pData->iNumTActorAIOverride;

								if (GET_REF(pOverActor->details.aiInfo->hFSM) != GET_REF(pBaseActor->details.aiInfo->hFSM)) {
									++pData->iNumTActorFSMOverride;
								}
								if (eaSize(&pOverActor->details.aiInfo->actorVars) == eaSize(&pBaseActor->details.aiInfo->actorVars)) {
									for(j=eaSize(&pOverActor->details.aiInfo->actorVars)-1; j>=0; --j) {
										if (StructCompare(parse_OldEncounterVariable, pOverActor->details.aiInfo->actorVars[j], pBaseActor->details.aiInfo->actorVars[j],0,0,0) != 0) {
											++pData->iNumTActorFSMVarsOverride;
										}
									}
								} else {
									++pData->iNumTActorFSMVarsOverride;
								}
							}
							if (pOverActor->details.position) {
								++pData->iNumTActorPositionOverride;
							}
							if (pOverActor->disableSpawn != ActorScalingFlag_Inherited) {
								++pData->iNumTActorDisableSpawnOverride;
							}
							if (pOverActor->useBossBar != ActorScalingFlag_Inherited) {
								++pData->iNumTActorUseBossOverride;
							}
							if (pOverActor->name) {
								++pData->iNumTActorNameOverride;
							}
							if (GET_REF(pOverActor->displayNameMsg.hMessage)) {
								++pData->iNumTActorDispNameOverride;
							}
						}
					}
				}
			}
		}
	}
}

static void ScanEncDef(EncounterDef *pDef, EncScanData *pData)
{
	OldInteractionProperties props = {0};
	int i;

	props.eInteractType = 5;

	++pData->iNumDefs;

	if (pDef->bAmbushEncounter) {
		++pData->iNumDefAmbush;
	}
	if (pDef->minLevel || pDef->maxLevel) {
		++pData->iNumDefLevel;
	}
	if (pDef->bUsePlayerLevel) {
		++pData->iNumDefPlayerLevel;
	}
	if (GET_REF(pDef->critterGroup)) {
		++pData->iNumDefCritterGroup;
	}
	if (GET_REF(pDef->faction)) {
		++pData->iNumDefCritterFaction;
	}
	if (pDef->gangID) {
		++pData->iNumDefGang;
	}
	if (pDef->spawnCond) {
		char *pcText = exprGetCompleteString(pDef->spawnCond);
		ScanEncAddStringCount(&pData->eaDefSpawnCond, pcText);
		++pData->iNumDefSpawnCond;
	}
	if (pDef->bCheckSpawnCondPerPlayer) {
		++pData->iNumDefSpawnPerPlayer;
	}
	if (pDef->spawnRadius != 300) {
		ScanEncAddNumCount(&pData->eaRespawnTimer, pDef->spawnRadius);
		++pData->iNumDefSpawnRadius;
	}
	if (pDef->spawnChance != 100) {
		ScanEncAddNumCount(&pData->eaRespawnTimer, pDef->spawnChance);
		++pData->iNumDefSpawnChance;
	}
	if (pDef->lockoutRadius != 75) {
		ScanEncAddNumCount(&pData->eaRespawnTimer, pDef->lockoutRadius);
		++pData->iNumDefLockoutRadius;
	}
	if (pDef->respawnTimer != 300) {
		ScanEncAddNumCount(&pData->eaRespawnTimer, pDef->respawnTimer);
		++pData->iNumDefRespawnTimer;
	}
	if (pDef->spawnAnim) {
		++pData->iNumDefSpawnAnim;
	}
	if (pDef->successCond) {
		char *pcText = exprGetCompleteString(pDef->successCond);
		ScanEncAddStringCount(&pData->eaDefSuccessCond, pcText);
		++pData->iNumDefSuccessCond;
	}
	if (pDef->failCond) {
		char *pcText = exprGetCompleteString(pDef->failCond);
		ScanEncAddStringCount(&pData->eaDefFailureCond, pcText);
		++pData->iNumDefFailCond;
	}
	if (pDef->waveCond) {
		++pData->iNumDefWaveCond;
	}
	if (pDef->waveInterval) {
		++pData->iNumDefWaveInterval;
	}
	if (pDef->waveDelayMin || pDef->waveDelayMax) {
		++pData->iNumDefWaveDelay;
	}
	if (eaSize(&pDef->encJobs)) {
		++pData->iNumDefJobs;
	}
	if (eaSize(&pDef->actions)) {
		++pData->iNumDefActions;
	}

	for(i=eaSize(&pDef->actors)-1; i>=0; --i) {
		OldActor *pActor = pDef->actors[i];

		++pData->iNumDefActors;

		if (GET_REF(pActor->displayNameMsg.hMessage)) {
			++pData->iNumDefActorDispName;
		}
		if (pActor->disableSpawn > 1) {
			++pData->iNumDefActorDisable;
		}
		if (pActor->useBossBar > 1) {
			++pData->iNumDefActorBoss;
		}

		if (pActor->details.info) {
			if (GET_REF(pActor->details.info->critterGroup)) {
				++pData->iNumDefActorCritterGroup;
			}
			if (GET_REF(pActor->details.info->critterDef)) {
				++pData->iNumDefActorCritterDef;
			}
			if (GET_REF(pActor->details.info->critterFaction)) {
				++pData->iNumDefActorFaction;
			}
			if (GET_REF(pActor->details.info->contactScript)) {
				++pData->iNumDefActorContact;
			}
			if (pActor->details.info->pcCritterRank || pActor->details.info->pcCritterSubRank) {
				++pData->iNumDefActorRank;
			}
			if (pActor->details.info->pchSpawnAnim) {
				++pData->iNumDefActorSpawnAnim;
			}
			if (pActor->details.info->pchSpawnAnim) {
				++pData->iNumDefActorSpawnAnim;
			}
			if (pActor->details.info->spawnCond) {
				char *pcText = exprGetCompleteString(pActor->details.info->spawnCond);
				ScanEncAddStringCount(&pData->eaDefActorSpawnCond, pcText);
				++pData->iNumDefActorSpawnCond;
			}
			if (StructCompare(parse_OldInteractionProperties, &props, &pActor->details.info->oldActorInteractProps, 0,0,0) != 0) {
				++pData->iNumDefActorInteract;
			}
		}
		if (pActor->details.aiInfo) {
			if (GET_REF(pActor->details.aiInfo->hFSM)) {
				++pData->iNumDefActorFSM;
			}
			if (eaSize(&pActor->details.aiInfo->actorVars)) {
				++pData->iNumDefActorVars;
			}
		}
	}

}


AUTO_COMMAND ACMD_SERVERCMD;
char* ScanEncLayers(void)
{
	EncounterLayer **eaEncLayers = NULL;
	FILE *file;
	int i;
	EncScanData scanData = {0};

	AssetLoadEncLayers(&eaEncLayers);

	file = fopen("c:\\ScanEncLayers.txt", "w");

	for(i=eaSize(&eaEncLayers)-1; i>=0; --i) {
		EncounterLayer *pLayer = eaEncLayers[i];

		// Skip test map layers
		if (strstri(pLayer->pchFilename,"test")) {
			continue;
		}
		++scanData.iNumLayers;

		ScanEncLayerGroup(&pLayer->rootGroup, &scanData);
	}

	for(i=eaSize(&scanData.eaEncDefs)-1; i>=0; --i) {
		ScanEncDef(scanData.eaEncDefs[i], &scanData);
	}

	fprintf(file, "Encounter Layer Override Information\n");
	fprintf(file, "------------------------------------\n");
	fprintf(file, "\n");
	fprintf(file, "Num Layers       = %5d\n", scanData.iNumLayers);
	if (scanData.iNumLayers) {
		fprintf(file, "Num Encounters   = %5d (%.2g/layer)\n", scanData.iNumEncounters, scanData.iNumEncounters / (float)scanData.iNumLayers);
	}
	if (scanData.iNumEncounters) {
		fprintf(file, "Num Actors       = %5d (%.2g/encounter)\n", scanData.iNumActors, scanData.iNumActors / (float)scanData.iNumEncounters);
	}
	fprintf(file, "Num Defs Used    = %5d\n", scanData.iNumDefs);
	fprintf(file, "\n");
	fprintf(file, "\n");
	fprintf(file, "Encounters       = %5d encounters\n", scanData.iNumEncounters);
	if (scanData.iNumEncounters) {
		fprintf(file, "  No Override    = %5d (%5.2f%%)\n", scanData.iNumNoOverride, 100.0 * scanData.iNumNoOverride / (float)scanData.iNumEncounters);
		fprintf(file, "  Override Base  = %5d (%5.2f%%)\n", scanData.iNumEncounters - scanData.iNumNoOverride - scanData.iNumTotalOverride, 100.0 * (scanData.iNumEncounters - scanData.iNumNoOverride - scanData.iNumTotalOverride) / (float)scanData.iNumEncounters);
		fprintf(file, "  Total Override = %5d (%5.2f%%)\n", scanData.iNumTotalOverride, 100.0 * scanData.iNumTotalOverride / (float)scanData.iNumEncounters);
	}
	fprintf(file, "\n");
	fprintf(file, "\n");
	fprintf(file, "Layer Only Data  = %5d encounters (%5.2f%%)\n", scanData.iNumNewData, 100.0 * scanData.iNumNewData / (float)scanData.iNumEncounters);
	fprintf(file, "  No Despawn     = %5d (%5.2f%%)\n", scanData.iNumNoDespawn, 100.0 * scanData.iNumNoDespawn / (float)scanData.iNumEncounters);
	fprintf(file, "  Spawn Weight   = %5d (%5.2f%%)\n", scanData.iNumSpawnWeight, 100.0 * scanData.iNumSpawnWeight / (float)scanData.iNumEncounters);
	fprintf(file, "  No Snap        = %5d (%5.2f%%)\n", scanData.iNumNoSnap, 100.0 * scanData.iNumNoSnap / (float)scanData.iNumEncounters);
	fprintf(file, "  Patrol Route   = %5d (%5.2f%%)\n", scanData.iNumPatrolRoute, 100.0 * scanData.iNumPatrolRoute / (float)scanData.iNumEncounters);
	fprintf(file, "\n");
	fprintf(file, "\n");
	fprintf(file, "Override Data    = %5d encounters\n", scanData.iNumEncounters - scanData.iNumNoOverride - scanData.iNumTotalOverride);
	if (scanData.iNumEncounters - scanData.iNumTotalOverride) {
		fprintf(file, "  Level          = %5d (%5.2f%%)\n", scanData.iNumLevelOverride, 100.0 * scanData.iNumLevelOverride / (float)scanData.iNumEncounters);
		fprintf(file, "  Critter Group  = %5d (%5.2f%%)\n", scanData.iNumCritterGroupOverride, 100.0 * scanData.iNumCritterGroupOverride / (float)scanData.iNumEncounters);
		fprintf(file, "  Faction        = %5d (%5.2f%%)\n", scanData.iNumFactionOverride, 100.0 * scanData.iNumFactionOverride / (float)scanData.iNumEncounters);
		fprintf(file, "  Spawn Anim     = %5d (%5.2f%%)\n", scanData.iNumSpawnAnimOverride, 100.0 * scanData.iNumSpawnAnimOverride / (float)scanData.iNumEncounters);
	}
	fprintf(file, "\n");
	fprintf(file, "\n");
	fprintf(file, "Info on Actors from Non-Total Overrides\n");
	fprintf(file, "---------------------------------------\n");
	fprintf(file, "\n");
	fprintf(file, "Actors           = %5d actors\n", scanData.iNumActors - scanData.iNumTotalOverrideActors);
	fprintf(file, "  Added          = %5d in %5d encounters\n", scanData.iNumActorsAdded, scanData.iNumTimesActorsAdded);
	fprintf(file, "  Deleted        = %5d in %5d encounters\n", scanData.iNumActorsDeleted, scanData.iNumTimesActorsDeleted);
	fprintf(file, "  Changed        = %5d in %5d encounters\n", scanData.iNumActorsChanged, scanData.iNumTimesActorsChanged);
	fprintf(file, "  Unchanged      = %5d\n", scanData.iNumActorsSame);
	fprintf(file, "\n");
	fprintf(file, "\n");
	fprintf(file, "Actors Changed   = %5d actors\n", scanData.iNumActorsChanged);
	if (scanData.iNumActorsChanged) {
		fprintf(file, "  Name           = %5d (%5.2f%%)\n", scanData.iNumActorNameOverride, 100.0 * scanData.iNumActorNameOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "  Display Name   = %5d (%5.2f%%)\n", scanData.iNumActorDispNameOverride, 100.0 * scanData.iNumActorDispNameOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "  Disable Spawn  = %5d (%5.2f%%)\n", scanData.iNumActorDisableSpawnOverride, 100.0 * scanData.iNumActorDisableSpawnOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "  Boss Bar       = %5d (%5.2f%%)\n", scanData.iNumActorUseBossOverride, 100.0 * scanData.iNumActorUseBossOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "  Position       = %5d (%5.2f%%)\n", scanData.iNumActorPositionOverride, 100.0 * scanData.iNumActorPositionOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "  AI             = %5d (%5.2f%%)\n", scanData.iNumActorAIOverride, 100.0 * scanData.iNumActorAIOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "   -> FSM        = %5d (%5.2f%%)\n", scanData.iNumActorFSMOverride, 100.0 * scanData.iNumActorFSMOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "   -> Vars       = %5d (%5.2f%%)\n", scanData.iNumActorFSMVarsOverride, 100.0 * scanData.iNumActorFSMVarsOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "  Base Info      = %5d (%5.2f%%)\n", scanData.iNumActorInfoOverride, 100.0 * scanData.iNumActorInfoOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "   -> CritterDef = %5d (%5.2f%%)\n", scanData.iNumActorCritterDefOverride, 100.0 * scanData.iNumActorCritterDefOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "   -> CritrGroup = %5d (%5.2f%%)\n", scanData.iNumActorCritterGroupOverride, 100.0 * scanData.iNumActorCritterGroupOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "   -> Faction    = %5d (%5.2f%%)\n", scanData.iNumActorCritterFactionOverride, 100.0 * scanData.iNumActorCritterFactionOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "   -> Rank       = %5d (%5.2f%%)\n", scanData.iNumActorRankOverride, 100.0 * scanData.iNumActorRankOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "   -> SpawnAnim  = %5d (%5.2f%%)\n", scanData.iNumActorSpawnAnimOverride, 100.0 * scanData.iNumActorSpawnAnimOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "   -> SpawnCond  = %5d (%5.2f%%)\n", scanData.iNumActorSpawnCondOverride, 100.0 * scanData.iNumActorSpawnCondOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "   -> Contact    = %5d (%5.2f%%)\n", scanData.iNumActorContactOverride, 100.0 * scanData.iNumActorContactOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
		fprintf(file, "   -> Interact   = %5d (%5.2f%%)\n", scanData.iNumActorInteractOverride, 100.0 * scanData.iNumActorInteractOverride / (float)(scanData.iNumActors - scanData.iNumTotalOverrideActors));
	}
	fprintf(file, "\n");
	fprintf(file, "\n");
	fprintf(file, "Info on Encounters from Total Overrides\n");
	fprintf(file, "---------------------------------------\n");
	fprintf(file, "\n");
	fprintf(file, "Total Override   = %5d encounters [%% is of Total Override encounters]\n", scanData.iNumTotalOverride);
	if (scanData.iNumTotalOverride) {
		fprintf(file, "  Ambush         = %5d (%5.2f%%)\n", scanData.iNumTAmbushOverride, 100.0 * scanData.iNumTAmbushOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Min/Max Level  = %5d (%5.2f%%)\n", scanData.iNumTLevelOverride, 100.0 * scanData.iNumTLevelOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Use Player Lvl = %5d (%5.2f%%)\n", scanData.iNumTPlayerLevelOverride, 100.0 * scanData.iNumTPlayerLevelOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  CritterGroup   = %5d (%5.2f%%)\n", scanData.iNumTCritterGroupOverride, 100.0 * scanData.iNumTCritterGroupOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Faction        = %5d (%5.2f%%)\n", scanData.iNumTFactionOverride, 100.0 * scanData.iNumTFactionOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Gang           = %5d (%5.2f%%)\n", scanData.iNumTGangOverride, 100.0 * scanData.iNumTGangOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Spawn Radius   = %5d (%5.2f%%)\n", scanData.iNumTSpawnRadiusOverride, 100.0 * scanData.iNumTSpawnRadiusOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Lockout Radius = %5d (%5.2f%%)\n", scanData.iNumTLockoutRadiusOverride, 100.0 * scanData.iNumTLockoutRadiusOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Respawn Timer  = %5d (%5.2f%%)\n", scanData.iNumTRespawnTimerOverride, 100.0 * scanData.iNumTRespawnTimerOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Spawn Chance   = %5d (%5.2f%%)\n", scanData.iNumTSpawnChanceOverride, 100.0 * scanData.iNumTSpawnChanceOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Spawn Anim     = %5d (%5.2f%%)\n", scanData.iNumTSpawnAnimOverride, 100.0 * scanData.iNumTSpawnAnimOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Spawn Cond     = %5d (%5.2f%%)\n", scanData.iNumTSpawnCondOverride, 100.0 * scanData.iNumTSpawnCondOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Cond Per Player= %5d (%5.2f%%)\n", scanData.iNumTSpawnPerPlayerOverride, 100.0 * scanData.iNumTSpawnPerPlayerOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Success Cond   = %5d (%5.2f%%)\n", scanData.iNumTSuccessCondOverride, 100.0 * scanData.iNumTSuccessCondOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Fail Cond      = %5d (%5.2f%%)\n", scanData.iNumTFailCondOverride, 100.0 * scanData.iNumTFailCondOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Wave Delay     = %5d (%5.2f%%)\n", scanData.iNumTWaveDelayOverride, 100.0 * scanData.iNumTWaveDelayOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Wave Interval  = %5d (%5.2f%%)\n", scanData.iNumTWaveIntervalOverride, 100.0 * scanData.iNumTWaveIntervalOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Wave Cond      = %5d (%5.2f%%)\n", scanData.iNumTWaveCondOverride, 100.0 * scanData.iNumTWaveCondOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Actions        = %5d (%5.2f%%)\n", scanData.iNumTActionsOverride, 100.0 * scanData.iNumTActionsOverride / (float)scanData.iNumTotalOverride);
		fprintf(file, "  Jobs           = %5d (%5.2f%%)\n", scanData.iNumTJobsOverride, 100.0 * scanData.iNumTJobsOverride / (float)scanData.iNumTotalOverride);
	}
	fprintf(file, "\n");
	fprintf(file, "\n");
	fprintf(file, "Info on Actors from Total Overrides\n");
	fprintf(file, "-----------------------------------\n");
	fprintf(file, "\n");
	fprintf(file, "Actors           = %5d actors in %5d encounters\n", scanData.iNumTActors, scanData.iNumTotalOverride);
	fprintf(file, "  Added          = %5d\n", scanData.iNumTActorsAdded);
	fprintf(file, "  Deleted        = %5d\n", scanData.iNumTActorsDeleted);
	fprintf(file, "  Changed        = %5d\n", scanData.iNumTActorsChanged);
	fprintf(file, "  Unchanged      = %5d\n", scanData.iNumTActorsSame);
	fprintf(file, "\n");
	fprintf(file, "\n");
	fprintf(file, "Actors Changed   = %5d actors  [%% is of Total-Override Actors]\n", scanData.iNumTActorsChanged);
	if (scanData.iNumTActorsChanged) {
		fprintf(file, "  Name           = %5d (%5.2f%%)\n", scanData.iNumTActorNameOverride, 100.0 * scanData.iNumTActorNameOverride / (float)scanData.iNumTActors);
		fprintf(file, "  Display Name   = %5d (%5.2f%%)\n", scanData.iNumTActorDispNameOverride, 100.0 * scanData.iNumTActorDispNameOverride / (float)scanData.iNumTActors);
		fprintf(file, "  Disable Spawn  = %5d (%5.2f%%)\n", scanData.iNumTActorDisableSpawnOverride, 100.0 * scanData.iNumTActorDisableSpawnOverride / (float)scanData.iNumTActors);
		fprintf(file, "  Boss Bar       = %5d (%5.2f%%)\n", scanData.iNumTActorUseBossOverride, 100.0 * scanData.iNumTActorUseBossOverride / (float)scanData.iNumTActors);
		fprintf(file, "  Position       = %5d (%5.2f%%)\n", scanData.iNumTActorPositionOverride, 100.0 * scanData.iNumTActorPositionOverride / (float)scanData.iNumTActors);
		fprintf(file, "  AI             = %5d (%5.2f%%)\n", scanData.iNumTActorAIOverride, 100.0 * scanData.iNumTActorAIOverride / (float)scanData.iNumTActors);
		fprintf(file, "   -> FSM        = %5d (%5.2f%%)\n", scanData.iNumTActorFSMOverride, 100.0 * scanData.iNumTActorFSMOverride / (float)scanData.iNumTActors);
		fprintf(file, "   -> Vars       = %5d (%5.2f%%)\n", scanData.iNumTActorFSMVarsOverride, 100.0 * scanData.iNumTActorFSMVarsOverride / (float)scanData.iNumTActors);
		fprintf(file, "  Base Info      = %5d (%5.2f%%)\n", scanData.iNumTActorInfoOverride, 100.0 * scanData.iNumTActorInfoOverride / (float)scanData.iNumTActors);
		fprintf(file, "   -> CritterDef = %5d (%5.2f%%)\n", scanData.iNumTActorCritterDefOverride, 100.0 * scanData.iNumTActorCritterDefOverride / (float)scanData.iNumTActors);
		fprintf(file, "   -> CritrGroup = %5d (%5.2f%%)\n", scanData.iNumTActorCritterGroupOverride, 100.0 * scanData.iNumTActorCritterGroupOverride / (float)scanData.iNumTActors);
		fprintf(file, "   -> Faction    = %5d (%5.2f%%)\n", scanData.iNumTActorCritterFactionOverride, 100.0 * scanData.iNumTActorCritterFactionOverride / (float)scanData.iNumTActors);
		fprintf(file, "   -> Rank       = %5d (%5.2f%%)\n", scanData.iNumTActorRankOverride, 100.0 * scanData.iNumTActorRankOverride / (float)scanData.iNumTActors);
		fprintf(file, "   -> SpawnAnim  = %5d (%5.2f%%)\n", scanData.iNumTActorSpawnAnimOverride, 100.0 * scanData.iNumTActorSpawnAnimOverride / (float)scanData.iNumTActors);
		fprintf(file, "   -> SpawnCond  = %5d (%5.2f%%)\n", scanData.iNumTActorSpawnCondOverride, 100.0 * scanData.iNumTActorSpawnCondOverride / (float)scanData.iNumTActors);
		fprintf(file, "   -> Contact    = %5d (%5.2f%%)\n", scanData.iNumTActorContactOverride, 100.0 * scanData.iNumTActorContactOverride / (float)scanData.iNumTActors);
		fprintf(file, "   -> Interact   = %5d (%5.2f%%)\n", scanData.iNumTActorInteractOverride, 100.0 * scanData.iNumTActorInteractOverride / (float)scanData.iNumTActors);
	}
	fprintf(file, "\n");
	fprintf(file, "\n");
	fprintf(file, "Info on Encounter Defs\n");
	fprintf(file, "----------------------\n");
	fprintf(file, "\n");
	fprintf(file, "Enc Defs         = %5d defs [%% is of Total Override encounters]\n", scanData.iNumDefs);
	if (scanData.iNumTotalOverride) {
		fprintf(file, "  Ambush         = %5d (%5.2f%%)\n", scanData.iNumDefAmbush, 100.0 * scanData.iNumDefAmbush / (float)scanData.iNumDefs);
		fprintf(file, "  Min/Max Level  = %5d (%5.2f%%)\n", scanData.iNumDefLevel, 100.0 * scanData.iNumDefLevel / (float)scanData.iNumDefs);
		fprintf(file, "  Use Player Lvl = %5d (%5.2f%%)\n", scanData.iNumDefPlayerLevel, 100.0 * scanData.iNumDefPlayerLevel / (float)scanData.iNumDefs);
		fprintf(file, "  CritterGroup   = %5d (%5.2f%%)\n", scanData.iNumDefCritterGroup, 100.0 * scanData.iNumDefCritterGroup / (float)scanData.iNumDefs);
		fprintf(file, "  Faction        = %5d (%5.2f%%)\n", scanData.iNumDefCritterFaction, 100.0 * scanData.iNumDefCritterFaction / (float)scanData.iNumDefs);
		fprintf(file, "  Gang           = %5d (%5.2f%%)\n", scanData.iNumDefGang, 100.0 * scanData.iNumDefGang / (float)scanData.iNumDefs);
		fprintf(file, "  Spawn Radius   = %5d (%5.2f%%)\n", scanData.iNumDefSpawnRadius, 100.0 * scanData.iNumDefSpawnRadius / (float)scanData.iNumDefs);
		fprintf(file, "  Lockout Radius = %5d (%5.2f%%)\n", scanData.iNumDefLockoutRadius, 100.0 * scanData.iNumDefLockoutRadius / (float)scanData.iNumDefs);
		fprintf(file, "  Respawn Timer  = %5d (%5.2f%%)\n", scanData.iNumDefRespawnTimer, 100.0 * scanData.iNumDefRespawnTimer / (float)scanData.iNumDefs);
		fprintf(file, "  Spawn Chance   = %5d (%5.2f%%)\n", scanData.iNumDefSpawnChance, 100.0 * scanData.iNumDefSpawnChance / (float)scanData.iNumDefs);
		fprintf(file, "  Spawn Anim     = %5d (%5.2f%%)\n", scanData.iNumDefSpawnAnim, 100.0 * scanData.iNumDefSpawnAnim / (float)scanData.iNumDefs);
		fprintf(file, "  Spawn Cond     = %5d (%5.2f%%)\n", scanData.iNumDefSpawnCond, 100.0 * scanData.iNumDefSpawnCond / (float)scanData.iNumDefs);
		fprintf(file, "  Cond Per Player= %5d (%5.2f%%)\n", scanData.iNumDefSpawnPerPlayer, 100.0 * scanData.iNumDefSpawnPerPlayer / (float)scanData.iNumDefs);
		fprintf(file, "  Success Cond   = %5d (%5.2f%%)\n", scanData.iNumDefSuccessCond, 100.0 * scanData.iNumDefSuccessCond / (float)scanData.iNumDefs);
		fprintf(file, "  Fail Cond      = %5d (%5.2f%%)\n", scanData.iNumDefFailCond, 100.0 * scanData.iNumDefFailCond / (float)scanData.iNumDefs);
		fprintf(file, "  Wave Delay     = %5d (%5.2f%%)\n", scanData.iNumDefWaveDelay, 100.0 * scanData.iNumDefWaveDelay / (float)scanData.iNumDefs);
		fprintf(file, "  Wave Interval  = %5d (%5.2f%%)\n", scanData.iNumDefWaveInterval, 100.0 * scanData.iNumDefWaveInterval / (float)scanData.iNumDefs);
		fprintf(file, "  Wave Cond      = %5d (%5.2f%%)\n", scanData.iNumDefWaveCond, 100.0 * scanData.iNumDefWaveCond / (float)scanData.iNumDefs);
		fprintf(file, "  Actions        = %5d (%5.2f%%)\n", scanData.iNumDefActions, 100.0 * scanData.iNumDefActions / (float)scanData.iNumDefs);
		fprintf(file, "  Jobs           = %5d (%5.2f%%)\n", scanData.iNumDefJobs, 100.0 * scanData.iNumDefJobs / (float)scanData.iNumDefs);
	}
	fprintf(file, "\n");
	fprintf(file, "\n");
	fprintf(file, "Actors           = %5d actors  [%% is of Total Actors]\n", scanData.iNumDefActors);
	if (scanData.iNumTActorsChanged) {
		fprintf(file, "  Display Name   = %5d (%5.2f%%)\n", scanData.iNumDefActorDispName, 100.0 * scanData.iNumDefActorDispName / (float)scanData.iNumDefActors);
		fprintf(file, "  Disable Spawn  = %5d (%5.2f%%)\n", scanData.iNumDefActorDisable, 100.0 * scanData.iNumDefActorDisable / (float)scanData.iNumDefActors);
		fprintf(file, "  Boss Bar       = %5d (%5.2f%%)\n", scanData.iNumDefActorBoss, 100.0 * scanData.iNumDefActorBoss / (float)scanData.iNumDefActors);
		fprintf(file, "  AI\n");
		fprintf(file, "   -> FSM        = %5d (%5.2f%%)\n", scanData.iNumDefActorFSM, 100.0 * scanData.iNumDefActorFSM / (float)scanData.iNumDefActors);
		fprintf(file, "   -> Vars       = %5d (%5.2f%%)\n", scanData.iNumDefActorVars, 100.0 * scanData.iNumDefActorVars / (float)scanData.iNumDefActors);
		fprintf(file, "  Base Info\n");
		fprintf(file, "   -> CritterDef = %5d (%5.2f%%)\n", scanData.iNumDefActorCritterDef, 100.0 * scanData.iNumDefActorCritterDef / (float)scanData.iNumDefActors);
		fprintf(file, "   -> CritrGroup = %5d (%5.2f%%)\n", scanData.iNumDefActorCritterGroup, 100.0 * scanData.iNumDefActorCritterGroup / (float)scanData.iNumDefActors);
		fprintf(file, "   -> Faction    = %5d (%5.2f%%)\n", scanData.iNumDefActorFaction, 100.0 * scanData.iNumDefActorFaction / (float)scanData.iNumDefActors);
		fprintf(file, "   -> Rank       = %5d (%5.2f%%)\n", scanData.iNumDefActorRank, 100.0 * scanData.iNumDefActorRank / (float)scanData.iNumDefActors);
		fprintf(file, "   -> SpawnAnim  = %5d (%5.2f%%)\n", scanData.iNumDefActorSpawnAnim, 100.0 * scanData.iNumDefActorSpawnAnim / (float)scanData.iNumDefActors);
		fprintf(file, "   -> SpawnCond  = %5d (%5.2f%%)\n", scanData.iNumDefActorSpawnCond, 100.0 * scanData.iNumDefActorSpawnCond / (float)scanData.iNumDefActors);
		fprintf(file, "   -> Contact    = %5d (%5.2f%%)\n", scanData.iNumDefActorContact, 100.0 * scanData.iNumDefActorContact / (float)scanData.iNumDefActors);
		fprintf(file, "   -> Interact   = %5d (%5.2f%%)\n", scanData.iNumDefActorInteract, 100.0 * scanData.iNumDefActorInteract / (float)scanData.iNumDefActors);
	}

	fprintf(file, "\n");
	fprintf(file, "\n");
	fprintf(file, "Info on Numbers and Expressions\n");
	fprintf(file, "-------------------------------\n");

	fprintf(file, "\n");
	fprintf(file, "Spawn Condition Expressions (Encounter Defs)\n");
	eaQSort(scanData.eaDefSpawnCond, ScanEncCompareStringCount);
	for(i=eaSize(&scanData.eaDefSpawnCond)-1; i>=0; --i) {
		fprintf(file, "  %5d = %s\n", scanData.eaDefSpawnCond[i]->count, scanData.eaDefSpawnCond[i]->value);
	}
	eaDestroyEx(&scanData.eaDefSpawnCond, NULL);

	fprintf(file, "\n");
	fprintf(file, "Spawn Condition Expressions (Encounter Layer Overrides)\n");
	eaQSort(scanData.eaSpawnCond, ScanEncCompareStringCount);
	for(i=eaSize(&scanData.eaSpawnCond)-1; i>=0; --i) {
		fprintf(file, "  %5d = %s\n", scanData.eaSpawnCond[i]->count, scanData.eaSpawnCond[i]->value);
	}
	eaDestroyEx(&scanData.eaSpawnCond, NULL);

	fprintf(file, "\n");
	fprintf(file, "Actor Spawn Condition Expressions (Encounter Defs)\n");
	eaQSort(scanData.eaDefActorSpawnCond, ScanEncCompareStringCount);
	for(i=eaSize(&scanData.eaDefActorSpawnCond)-1; i>=0; --i) {
		fprintf(file, "  %5d = %s\n", scanData.eaDefActorSpawnCond[i]->count, scanData.eaDefActorSpawnCond[i]->value);
	}
	eaDestroyEx(&scanData.eaDefActorSpawnCond, NULL);

	fprintf(file, "\n");
	fprintf(file, "Actor Spawn Condition Expressions (Encounter Layer Overrides)\n");
	eaQSort(scanData.eaActorSpawnCond, ScanEncCompareStringCount);
	for(i=eaSize(&scanData.eaActorSpawnCond)-1; i>=0; --i) {
		fprintf(file, "  %5d = %s\n", scanData.eaActorSpawnCond[i]->count, scanData.eaActorSpawnCond[i]->value);
	}
	eaDestroyEx(&scanData.eaActorSpawnCond, NULL);

	fprintf(file, "\n");
	fprintf(file, "Success Condition Expressions (Encounter Defs)\n");
	eaQSort(scanData.eaDefSuccessCond, ScanEncCompareStringCount);
	for(i=eaSize(&scanData.eaDefSuccessCond)-1; i>=0; --i) {
		fprintf(file, "  %5d = %s\n", scanData.eaDefSuccessCond[i]->count, scanData.eaDefSuccessCond[i]->value);
	}
	eaDestroyEx(&scanData.eaDefSuccessCond, NULL);

	fprintf(file, "\n");
	fprintf(file, "Success Condition Expressions (Encounter Layer Overrides)\n");
	eaQSort(scanData.eaSuccessCond, ScanEncCompareStringCount);
	for(i=eaSize(&scanData.eaSuccessCond)-1; i>=0; --i) {
		fprintf(file, "  %5d = %s\n", scanData.eaSuccessCond[i]->count, scanData.eaSuccessCond[i]->value);
	}
	eaDestroyEx(&scanData.eaSuccessCond, NULL);

	fprintf(file, "\n");
	fprintf(file, "Failure Condition Expressions (Encounter Def)\n");
	eaQSort(scanData.eaDefFailureCond, ScanEncCompareStringCount);
	for(i=eaSize(&scanData.eaDefFailureCond)-1; i>=0; --i) {
		fprintf(file, "  %5d = %s\n", scanData.eaDefFailureCond[i]->count, scanData.eaDefFailureCond[i]->value);
	}
	eaDestroyEx(&scanData.eaDefFailureCond, NULL);

	fprintf(file, "\n");
	fprintf(file, "Failure Condition Expressions (Encounter Layer Overrides)\n");
	eaQSort(scanData.eaFailureCond, ScanEncCompareStringCount);
	for(i=eaSize(&scanData.eaFailureCond)-1; i>=0; --i) {
		fprintf(file, "  %5d = %s\n", scanData.eaFailureCond[i]->count, scanData.eaFailureCond[i]->value);
	}
	eaDestroyEx(&scanData.eaFailureCond, NULL);

	fprintf(file, "\n");
	fprintf(file, "Spawn Radius\n");
	eaQSort(scanData.eaSpawnRadius, ScanEncCompareNumCount);
	for(i=eaSize(&scanData.eaSpawnRadius)-1; i>=0; --i) {
		fprintf(file, "  %5d = %d\n", scanData.eaSpawnRadius[i]->count, scanData.eaSpawnRadius[i]->value);
	}
	eaDestroyEx(&scanData.eaSpawnRadius, NULL);

	fprintf(file, "\n");
	fprintf(file, "Lockout Radius\n");
	eaQSort(scanData.eaLockoutRadius, ScanEncCompareNumCount);
	for(i=eaSize(&scanData.eaLockoutRadius)-1; i>=0; --i) {
		fprintf(file, "  %5d = %d\n", scanData.eaLockoutRadius[i]->count, scanData.eaLockoutRadius[i]->value);
	}
	eaDestroyEx(&scanData.eaLockoutRadius, NULL);

	fprintf(file, "\n");
	fprintf(file, "Spawn Chance\n");
	eaQSort(scanData.eaSpawnChance, ScanEncCompareNumCount);
	for(i=eaSize(&scanData.eaSpawnChance)-1; i>=0; --i) {
		fprintf(file, "  %5d = %d\n", scanData.eaSpawnChance[i]->count, scanData.eaSpawnChance[i]->value);
	}
	eaDestroyEx(&scanData.eaSpawnChance, NULL);

	fprintf(file, "\n");
	fprintf(file, "Respawn Timer\n");
	eaQSort(scanData.eaRespawnTimer, ScanEncCompareNumCount);
	for(i=eaSize(&scanData.eaRespawnTimer)-1; i>=0; --i) {
		fprintf(file, "  %5d = %d\n", scanData.eaRespawnTimer[i]->count, scanData.eaRespawnTimer[i]->value);
	}
	eaDestroyEx(&scanData.eaRespawnTimer, NULL);

	fclose(file);

	AssetCleanupEncLayers(&eaEncLayers);

	return "Scan enc layer notes written to 'c:\\ScanEncLayers.txt'";
}


void FixupEventVolumeLayers(LibFileLoad ***peaGeoLayers, LibFileLoad ***peaModifiedGeoLayers, FILE *file)
{
	int i,j;

	for(i=eaSize(peaGeoLayers)-1; i>=0; --i) {
		LibFileLoad *pLayer = (*peaGeoLayers)[i];
		bool bModified = false;

		for(j=eaSize(&pLayer->defs)-1; j>=0; --j) {
			GroupDef *pDef = pLayer->defs[j];
			bool bIsEvent = groupDefIsVolumeType(pDef, "Event");

			if (!pDef->property_structs.server_volume.event_volume_properties &&
				(pDef->property_structs.server_volume.action_volume_properties || 
				 pDef->property_structs.server_volume.ai_volume_properties ||
				 pDef->property_structs.server_volume.landmark_volume_properties ||
				 pDef->property_structs.server_volume.neighborhood_volume_properties ||
				 pDef->property_structs.server_volume.obsolete_optionalaction_properties ||
				 pDef->property_structs.server_volume.interaction_volume_properties ||
				 pDef->property_structs.server_volume.power_volume_properties ||
				 pDef->property_structs.server_volume.warp_volume_properties ||
				 bIsEvent)) {
				
				// Mark this layer as modified
				eaPushUnique(peaModifiedGeoLayers, pLayer);
				if (!bModified) {
					bModified = true;
					fprintf(file, "Layer: %s\n", pLayer->defs[0]->filename);
				}
				fprintf(file, "  Def %s\n", pDef->name_str);

				// Make sure volume type includes "Event"
				groupDefAddVolumeType(pDef, "Event");

				// Make sure event structure exists
				pDef->property_structs.server_volume.event_volume_properties = StructCreate(parse_WorldEventVolumeProperties);

				// Copy action conditions into event conditions
				if (pDef->property_structs.server_volume.action_volume_properties && pDef->property_structs.server_volume.action_volume_properties->entered_action_cond) {
					pDef->property_structs.server_volume.event_volume_properties->entered_cond = exprClone(pDef->property_structs.server_volume.action_volume_properties->entered_action_cond);
				}
				if (pDef->property_structs.server_volume.action_volume_properties && pDef->property_structs.server_volume.action_volume_properties->exited_action_cond) {
					pDef->property_structs.server_volume.event_volume_properties->exited_cond = exprClone(pDef->property_structs.server_volume.action_volume_properties->exited_action_cond);
				}
			}
		}
	}
}


AUTO_COMMAND ACMD_SERVERCMD;
char* FixupEventVolumes(void)
{
	LibFileLoad **eaGeoLayers = NULL;
	LibFileLoad **eaModifiedGeoLayers = NULL;
	char **eaExtraFiles = NULL;
	FILE *file;

	AssetLoadGeoLayers(&eaGeoLayers);

	file = fopen("c:\\FixEventVolumes.txt", "w");

	FixupEventVolumeLayers(&eaGeoLayers, &eaModifiedGeoLayers,file);

	AssetSaveGeoLayers(&eaModifiedGeoLayers, false);

	AssetCleanupGeoLayers(&eaGeoLayers);

	eaDestroy(&eaModifiedGeoLayers);

	fclose(file);
	return "Convert notes written to 'c:\\FixEventVolumes.txt'";
}

void FixupAutoExecVolumeLayers(LibFileLoad ***peaGeoLayers, LibFileLoad ***peaModifiedGeoLayers, FILE *file)
{
	int i,j,k;

	for(i=eaSize(peaGeoLayers)-1; i>=0; --i) {
		LibFileLoad *pLayer = (*peaGeoLayers)[i];
		bool bModified = false;

		for(j=eaSize(&pLayer->defs)-1; j>=0; --j) {
			GroupDef *pDef = pLayer->defs[j];

			if ( pDef->property_structs.server_volume.obsolete_optionalaction_properties ) {
				for(k=eaSize(&pDef->property_structs.server_volume.obsolete_optionalaction_properties->entries)-1; k>=0; --k) {
					WorldOptionalActionVolumeEntry *pEntry = pDef->property_structs.server_volume.obsolete_optionalaction_properties->entries[k];
					if (pEntry->auto_execute && (pEntry->priority != WorldOptionalActionPriority_Low)) {
						// Mark this layer as modified
						eaPushUnique(peaModifiedGeoLayers, pLayer);
						if (!bModified) {
							bModified = true;
							fprintf(file, "Layer: %s\n", pLayer->defs[0]->filename);
						}
						fprintf(file, "  Def %s\n", pDef->name_str);

						pEntry->priority = WorldOptionalActionPriority_Low;
					}
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD;
char* FixupAutoExecVolumes(void)
{
	LibFileLoad **eaGeoLayers = NULL;
	LibFileLoad **eaModifiedGeoLayers = NULL;
	char **eaExtraFiles = NULL;
	FILE *file;

	AssetLoadGeoLayers(&eaGeoLayers);

	file = fopen("c:\\FixAutoExecVolumes.txt", "w");

	FixupAutoExecVolumeLayers(&eaGeoLayers, &eaModifiedGeoLayers,file);

	AssetSaveGeoLayers(&eaModifiedGeoLayers, true);

	AssetCleanupGeoLayers(&eaGeoLayers);

	eaDestroy(&eaModifiedGeoLayers);

	fclose(file);
	return "Convert notes written to 'c:\\FixAutoExecVolumes.txt'";
}

static bool ExpressionRemoveRecentlyCompletedDialog(FILE* log, Expression* expr)
{
	if(expr) {
		char* exprText = exprGetCompleteString(expr);
		const char* pchFind = strstri(exprText, "Beam");
		const char* pchStart = strstri(exprText, "HasRecentlyCompletedContactDialog");
		const char* pchEnd = pchStart ? strchr(pchStart, ')') : NULL;
		S32 iExprStrSize = (S32)strlen(exprText);
		char* newStr = NULL;
		bool bFound = true;
		int iParenCount = 0;

		if (!pchFind || !pchStart || !pchEnd || pchFind < pchStart || pchFind > pchEnd)
		{
			return false;
		}
		
		fprintf(log, "Before Expr: %s\n", exprText);

		if (strStartsWith(pchEnd+1, " = 0"))
		{
			pchEnd += 4;
		}
		if (pchStart - exprText >= (S32)strlen("not "))
		{
			const char* pchSearch = pchStart - 4;
			if (strStartsWith(pchSearch, "not "))
			{
				pchStart = pchSearch;
			}
		}
		if (pchStart - exprText >= 4)
		{
			const char* pchSearch = pchStart - 4;
			if (strStartsWith(pchSearch, "and "))
			{
				newStr = alloca(iExprStrSize+1);
				strncpy_s(newStr, iExprStrSize, exprText, pchSearch-exprText);
				strcat_s(newStr, iExprStrSize, pchEnd+1);
			}
		}
		if (!newStr && strStartsWith(pchEnd+1, " and"))
		{
			newStr = alloca(iExprStrSize+1);
			strncpy_s(newStr, iExprStrSize, exprText, pchStart-exprText);
			strcat_s(newStr, iExprStrSize, pchEnd+5);
		}
		if (newStr)
		{
			removeLeadingAndFollowingSpaces(newStr);
			fprintf(log, "After Expr: %s\n", newStr);
			exprSetOrigStrNoFilename(expr, newStr);
			return true;
		}
	}
	return false;
}

int FixupBeamdownOptActVolumeLayers( FILE* log, LibFileLoad* layer, char* layerFName )
{
	int numChangedAccum = 0;
	
	FOR_EACH_IN_EARRAY( layer->defs, GroupDef, defLoad ) {
		if( defLoad->property_structs.server_volume.obsolete_optionalaction_properties ) {
			FOR_EACH_IN_EARRAY( defLoad->property_structs.server_volume.obsolete_optionalaction_properties->entries, WorldOptionalActionVolumeEntry, entry ) {
				if( ExpressionRemoveRecentlyCompletedDialog( log, entry->visible_cond )) {
					++numChangedAccum;
				}
			} FOR_EACH_END;
		}
	} FOR_EACH_END;

	return numChangedAccum;
}

AUTO_COMMAND;
void FixupBeamdownOptActLayers( void )
{
	FILE* log = fopen( "c:\\FixupBeamdownOptActLayers.txt", "w" );
	FILE* layers = fopen( "c:\\FixupBeamdownOptActLayers.txt", "w");
	char** layerFiles = NULL;
	fileScanAllDataDirs( "ns/", ScanLayerFiles, &layerFiles );

	sharedMemoryEnableEditorMode();

	{
		time_t rawtime;
		char buffer[ 256 ];

		time( &rawtime );
		ctime_s( SAFESTR(buffer), &rawtime );
		fprintf( log, "-*- truncate-lines: t -*-\nBEAMDOWN OPTACT CONVERSION -- %s\n", buffer );
	}

	FOR_EACH_IN_EARRAY( layerFiles, char, filename ) {
		LibFileLoad libFile = { 0 };

		if( !ParserReadTextFile( filename, parse_LibFileLoad, &libFile, 0 )) {
			fprintf( log, "Layer: %s -- Unable to read\n", filename );
			continue;
		}

		fprintf( log, "Layer: %s -- Starting conversion...", filename );
		{
			long beforeNLPos;
			int numChanged;

			beforeNLPos = ftell( log );
			fputc( '\n', log );
			numChanged = FixupBeamdownOptActVolumeLayers( log, &libFile, filename );
			
			if( numChanged == 0 ) {
				fseek( log, beforeNLPos, SEEK_SET );
				fprintf( log, "done, no changes.\n" );
			} else {
				if( !simulateUpdate ) {
					ParserWriteTextFile( filename, parse_LibFileLoad, &libFile, 0, 0 );
				}
			
				fprintf( log, "done, %d exprs changed.\n", numChanged );
				fprintf( layers, "%s\n", filename );
			}
		}

		StructDeInit( parse_LibFileLoad, &libFile );
	} FOR_EACH_END;

	fprintf( log, "DONE\n" );

	eaDestroyEx( &layerFiles, NULL );
	fclose( layers );
	fclose( log );
}


AUTO_COMMAND ACMD_SERVERCMD;
char* FixupRegionLayers(void)
{
	ZoneMap **eaZones = NULL;
	int i,j;
	FILE *file;

	AssetLoadZones(&eaZones);

	file = fopen("c:\\ZoneRegionLayers.txt", "w");

	for(i=eaSize(&eaZones)-1; i>=0; --i) {
		ZoneMap *pZone = eaZones[i];
		int numRegionLayers = 0;

		for(j=eaSize(&pZone->layers)-1; j>=0; --j) {
			ZoneMapLayer *pLayer = pZone->layers[j];
			if (pLayer->region_name) {
				++numRegionLayers;
			}
		}
		if (numRegionLayers < eaSize(&pZone->layers)) {
			char **eaRegioned = NULL;
			char **eaUnregioned = NULL;

			fprintf(file, "ZONE = %s (%d of %d are regioned)\n", zmapGetFilename(pZone), numRegionLayers, eaSize(&pZone->layers));

			for(j=eaSize(&pZone->layers)-1; j>=0; --j) {
				ZoneMapLayer *pLayer = pZone->layers[j];
				if (pLayer->region_name) {
					eaPush(&eaRegioned, (char*)pLayer->filename);
				} else {
					eaPush(&eaUnregioned, (char*)pLayer->filename);
				}
			}

			for(j=0; j<eaSize(&eaRegioned); ++j) {
				fprintf(file, "  Regioned Layer:   %s\n", eaRegioned[j]);
			}
			eaDestroy(&eaRegioned);
			for(j=0; j<eaSize(&eaUnregioned); ++j) {
				fprintf(file, "  Unregioned Layer: %s\n", eaUnregioned[j]);
			}
			eaDestroy(&eaUnregioned);

			fprintf(file, "\n");
		}
	}

	fclose(file);

	AssetCleanupZones(&eaZones);

	return "Done";
}


// ---------------------------------------------------------------------
// Function to attach an animation to all Clickables
// ---------------------------------------------------------------------

// Changes 
void InteractableAddDefaultAnimationsToLayers(LibFileLoad ***peaGeoLayers, LibFileLoad ***peaModifiedGeoLayers, const char *pchInteractionClass, const char *pchAnimListName, FILE *file)
{
	int i,j,k;

	for(i=eaSize(peaGeoLayers)-1; i>=0; --i) {
		LibFileLoad *pLayer = (*peaGeoLayers)[i];
		bool bModified = false;

		for(j=eaSize(&pLayer->defs)-1; j>=0; --j) {
			GroupDef *pDef = pLayer->defs[j];
			bool bIsEvent = false;

			if (pDef->property_structs.interaction_properties){

				// Check all property entries
				for (k=eaSize(&pDef->property_structs.interaction_properties->eaEntries)-1; k>=0; --k){
					WorldInteractionPropertyEntry *pEntry = pDef->property_structs.interaction_properties->eaEntries[k];

					// Check if this interactable is the correct type
					if (!stricmp(pEntry->pcInteractionClass, pchInteractionClass)) {

						// Make sure this interactable doesn't already have an animation
						if (!pEntry->pAnimationProperties || !IS_HANDLE_ACTIVE(pEntry->pAnimationProperties->hInteractAnim)){

							// Make sure this object has a Use Time, or is a crafting station
							// (crafting stations play the animation the entire time the player is crafting)
							if ((pEntry->pTimeProperties && pEntry->pTimeProperties->fUseTime)
								|| (pEntry->pCraftingProperties && (stricmp(pEntry->pcInteractionClass, "CraftingStation") == 0))){

								// Mark this layer as modified
								eaPushUnique(peaModifiedGeoLayers, pLayer);
								if (!bModified) {
									bModified = true;
									fprintf(file, "Layer: %s\n", pLayer->defs[0]->filename);
								}
								fprintf(file, "  Def %s\n", pDef->name_str);

								// Make sure interactable has animation properties
								if (!pDef->property_structs.interaction_properties->eaEntries[k]->pAnimationProperties){
									pDef->property_structs.interaction_properties->eaEntries[k]->pAnimationProperties = StructCreate(parse_WorldAnimationInteractionProperties);
								}
								
								// Attach animation
								SET_HANDLE_FROM_STRING("AIAnimList", pchAnimListName, pDef->property_structs.interaction_properties->eaEntries[k]->pAnimationProperties->hInteractAnim);
							}
						}
					}
				}
			}
		}
	}
}


AUTO_COMMAND ACMD_SERVERCMD;
char* InteractableAddDefaultAnimations(const char *pchInteractionClass, const char *pchAnimListName)
{
	LibFileLoad **eaGeoLayers = NULL;
	LibFileLoad **eaModifiedGeoLayers = NULL;
	char **eaExtraFiles = NULL;
	FILE *file;

	AssetLoadGeoLayers(&eaGeoLayers);

	file = fopen("c:\\InteractableAddDefaultAnimations.txt", "w");

	InteractableAddDefaultAnimationsToLayers(&eaGeoLayers, &eaModifiedGeoLayers, pchInteractionClass, pchAnimListName, file);

	AssetSaveGeoLayers(&eaModifiedGeoLayers, false);

	AssetCleanupGeoLayers(&eaGeoLayers);

	eaDestroy(&eaModifiedGeoLayers);

	fclose(file);
	return "Convert notes written to 'c:\\InteractableAddDefaultAnimations.txt'";
}

void zmapInfoReport(FILE * report_file);

AUTO_COMMAND ACMD_SERVERCMD;
char* ZoneMapReportIndoorLightingOnSpaceMaps()
{
	FILE *file;
	file = fopen("c:\\ZoneMapInfo.txt", "w");

	zmapInfoReport(file);

	fclose(file);

	return "Report written to 'c:\\ZoneMapInfo.txt'";
}


// ---------------------------------------------------------------------
// Function to change "GrantMission()" expression functions on Volumes into Optional Actions
// ---------------------------------------------------------------------

bool VolumeFixupMissionGrants_ExprParseCB (char*** parsedArgs, char **estrReplacementString, GroupDef *pDef)
{
	MissionDef *pMissionDef = NULL;
	const char *pchMissionName = NULL;

	if (eaSize(parsedArgs) != 1){
		return false;
	}

	pchMissionName = (*parsedArgs)[0];

	pMissionDef = RefSystem_ReferentFromString(g_MissionDictionary, pchMissionName);
	if (pMissionDef && GET_REF(pMissionDef->displayNameMsg.hMessage)){
		// Create an Optional Action on the volume
		WorldOptionalActionVolumeEntry *pEntry = StructCreate(parse_WorldOptionalActionVolumeEntry);
		WorldGameActionProperties *pGameAction;
		
		// HACK - For my specific fix-up, the category should be "CrimeComputer"
		// If someone ever re-uses this, the category should probably be passed in
		pEntry->category_name = "CrimeComputer";

		// Add a "Mission Offer" gameaction
		pGameAction = StructCreate(parse_WorldGameActionProperties);
		pGameAction->eActionType = WorldGameActionType_MissionOffer;
		pGameAction->pMissionOfferProperties = StructCreate(parse_WorldMissionOfferActionProperties);
		SET_HANDLE_FROM_STRING(g_MissionDictionary, pchMissionName, pGameAction->pMissionOfferProperties->hMissionDef);
		eaPush(&pEntry->actions.eaActions, pGameAction);

		if (!pDef->property_structs.server_volume.obsolete_optionalaction_properties){
			pDef->property_structs.server_volume.obsolete_optionalaction_properties = StructCreate(parse_WorldOptionalActionVolumeProperties);
		}
		eaPush(&pDef->property_structs.server_volume.obsolete_optionalaction_properties->entries, pEntry);
		return true;		
	}
	return false;
}

// Changes 
void VolumeFixupMissionGrantsOnLayers(LibFileLoad ***peaGeoLayers, LibFileLoad ***peaModifiedGeoLayers, FILE *file)
{
	int i,j;

	for(i=eaSize(peaGeoLayers)-1; i>=0; --i) {
		LibFileLoad *pLayer = (*peaGeoLayers)[i];
		bool bModified = false;

		for(j=eaSize(&pLayer->defs)-1; j>=0; --j) {
			GroupDef *pDef = pLayer->defs[j];
			bool bIsEvent = false;

			if (pDef->property_structs.server_volume.action_volume_properties){

				if (datafixup_RemoveExprFuncWithCallback(&pDef->property_structs.server_volume.action_volume_properties->entered_action, "GrantMission", pLayer->defs[0]->filename, VolumeFixupMissionGrants_ExprParseCB, pDef)){

					// If there aren't any Actions anymore, destroy Action Volume properties
					if (!pDef->property_structs.server_volume.action_volume_properties->entered_action 
						&& !pDef->property_structs.server_volume.action_volume_properties->entered_action_cond
						&& !pDef->property_structs.server_volume.action_volume_properties->exited_action
						&& !pDef->property_structs.server_volume.action_volume_properties->exited_action_cond){
							StructDestroySafe(parse_WorldActionVolumeProperties, &pDef->property_structs.server_volume.action_volume_properties);
					}

					// Mark this layer as modified
					eaPushUnique(peaModifiedGeoLayers, pLayer);
					if (!bModified) {
						bModified = true;
						fprintf(file, "Layer: %s\n", pLayer->defs[0]->filename);
					}
					fprintf(file, "  Def %s\n", pDef->name_str);
				}
			}
		}
	}
}


AUTO_COMMAND ACMD_SERVERCMD;
char* VolumeFixupMissionGrants(void)
{
	LibFileLoad **eaGeoLayers = NULL;
	LibFileLoad **eaModifiedGeoLayers = NULL;
	char **eaExtraFiles = NULL;
	FILE *file;

	AssetLoadGeoLayers(&eaGeoLayers);

	file = fopen("c:\\VolumeFixupMissionGrants.txt", "w");

	VolumeFixupMissionGrantsOnLayers(&eaGeoLayers, &eaModifiedGeoLayers, file);

	AssetSaveGeoLayers(&eaModifiedGeoLayers, false);

	AssetCleanupGeoLayers(&eaGeoLayers);

	eaDestroy(&eaModifiedGeoLayers);

	fclose(file);
	return "Convert notes written to 'c:\\VolumeFixupMissionGrants.txt'";
}


// ---------------------------------------------------------------------
// Duplicate message analysis and fixup logic
// ---------------------------------------------------------------------

typedef struct DupMsg_CritterVarInfo {
	const char *pcVarName;  //pooled string
	char *pcVarValue;
	char *pcVarMsg;
	int count;
} DupMsg_CritterVarInfo;

typedef struct DupMsg_CritterInfo {
	CritterDef *pCritter;
	DupMsg_CritterVarInfo **eaVarInfo;
	int count;
} DupMsg_CritterInfo;


typedef struct DupMsg_CritterGroupInfo {
	CritterGroup *pGroup;
	DupMsg_CritterVarInfo **eaVarInfo;
	int count;
} DupMsg_CritterGroupInfo;


typedef struct DupMsg_FSMInfo {
	FSM *pFSM;
	DupMsg_CritterVarInfo **eaVarInfo;
	int count;
} DupMsg_FSMInfo;


int DupMsg_fixupSortCritterVarInfo(const DupMsg_CritterVarInfo **left, const DupMsg_CritterVarInfo **right)
{
	return stricmp((*left)->pcVarName, (*right)->pcVarName);
}

void DupMsg_AddGroupVar(DupMsg_CritterGroupInfo *pGroupInfo, OldEncounterVariable *pVar)
{
	DupMsg_CritterVarInfo *pInfo = NULL;
	int i;
	const char *pcVarString;
	const char *pcMsgString = NULL;

	pcVarString = MultiValGetString(&pVar->varValue, NULL);
	if (pcVarString) {
		Message *pMsg = RefSystem_ReferentFromString(gMessageDict, pcVarString);
		if (pMsg) {
			pcMsgString = pMsg->pcDefaultString;
		}
	}

	for(i=eaSize(&pGroupInfo->eaVarInfo)-1; i>=0; --i) {
		pInfo = pGroupInfo->eaVarInfo[i];
		if ((stricmp(pInfo->pcVarName, pVar->varName) == 0) &&
			((pcMsgString && pInfo->pcVarMsg && (stricmp(pInfo->pcVarMsg, pcMsgString) == 0)) ||
			 (pInfo->pcVarValue && pcVarString && (stricmp(pInfo->pcVarValue, pcVarString) == 0)))) {
			++pInfo->count;
			return;
		}
	}

	pInfo = calloc(1, sizeof(DupMsg_CritterVarInfo));
	pInfo->pcVarName = pVar->varName;
	pInfo->pcVarValue = (char*)pcVarString;
	pInfo->pcVarMsg = (char*)pcMsgString;
	pInfo->count = 1;
	eaPush(&pGroupInfo->eaVarInfo, pInfo);
}


void DupMsg_AddCritterVar(DupMsg_CritterInfo *pCritterInfo, OldEncounterVariable *pVar)
{
	DupMsg_CritterVarInfo *pInfo = NULL;
	int i;
	const char *pcVarString;
	const char *pcMsgString = NULL;

	pcVarString = MultiValGetString(&pVar->varValue, NULL);
	if (pcVarString) {
		Message *pMsg = RefSystem_ReferentFromString(gMessageDict, pcVarString);
		if (pMsg) {
			pcMsgString = pMsg->pcDefaultString;
		}
	}

	for(i=eaSize(&pCritterInfo->eaVarInfo)-1; i>=0; --i) {
		pInfo = pCritterInfo->eaVarInfo[i];
		if ((stricmp(pInfo->pcVarName, pVar->varName) == 0) &&
			((pcMsgString && pInfo->pcVarMsg && (stricmp(pInfo->pcVarMsg, pcMsgString) == 0)) ||
			 (pInfo->pcVarValue && pcVarString && (stricmp(pInfo->pcVarValue, pcVarString) == 0)))) {
			++pInfo->count;
			return;
		}
	}

	pInfo = calloc(1, sizeof(DupMsg_CritterVarInfo));
	pInfo->pcVarName = pVar->varName;
	pInfo->pcVarValue = (char*)pcVarString;
	pInfo->pcVarMsg = (char*)pcMsgString;
	pInfo->count = 1;
	eaPush(&pCritterInfo->eaVarInfo, pInfo);
}


void DupMsg_AddFSMVar(DupMsg_FSMInfo *pFSMInfo, OldEncounterVariable *pVar)
{
	DupMsg_CritterVarInfo *pInfo = NULL;
	int i;
	const char *pcVarString;
	const char *pcMsgString = NULL;

	pcVarString = MultiValGetString(&pVar->varValue, NULL);
	if (pcVarString) {
		Message *pMsg = RefSystem_ReferentFromString(gMessageDict, pcVarString);
		if (pMsg) {
			pcMsgString = pMsg->pcDefaultString;
		}
	}

	for(i=eaSize(&pFSMInfo->eaVarInfo)-1; i>=0; --i) {
		pInfo = pFSMInfo->eaVarInfo[i];
		if ((stricmp(pInfo->pcVarName, pVar->varName) == 0) &&
			((pcMsgString && pInfo->pcVarMsg && (stricmp(pInfo->pcVarMsg, pcMsgString) == 0)) ||
			 (pInfo->pcVarValue && pcVarString && (stricmp(pInfo->pcVarValue, pcVarString) == 0)))) {
			++pInfo->count;
			return;
		}
	}

	pInfo = calloc(1, sizeof(DupMsg_CritterVarInfo));
	pInfo->pcVarName = pVar->varName;
	pInfo->pcVarValue = (char*)pcVarString;
	pInfo->pcVarMsg = (char*)pcMsgString;
	pInfo->count = 1;
	eaPush(&pFSMInfo->eaVarInfo, pInfo);
}


DupMsg_CritterGroupInfo *DupMsg_GetCritterGroupInfo(DupMsg_CritterGroupInfo ***peaCritterGroupInfo, CritterGroup *pGroup)
{
	DupMsg_CritterGroupInfo *pInfo = NULL;
	int i;

	for(i=eaSize(peaCritterGroupInfo)-1; i>=0; --i) {
		pInfo = (*peaCritterGroupInfo)[i];
		if (pInfo->pGroup == pGroup) {
			++pInfo->count;
			return pInfo;
		}
	}

	pInfo = calloc(1, sizeof(DupMsg_CritterGroupInfo));
	pInfo->pGroup = pGroup;
	pInfo->count = 1;
	eaPush(peaCritterGroupInfo, pInfo);

	return pInfo;
}


DupMsg_CritterInfo *DupMsg_GetCritterInfo(DupMsg_CritterInfo ***peaCritterInfo, CritterDef *pCritter)
{
	DupMsg_CritterInfo *pInfo = NULL;
	int i;

	for(i=eaSize(peaCritterInfo)-1; i>=0; --i) {
		pInfo = (*peaCritterInfo)[i];
		if (pInfo->pCritter == pCritter) {
			++pInfo->count;
			return pInfo;
		}
	}

	pInfo = calloc(1, sizeof(DupMsg_CritterInfo));
	pInfo->pCritter = pCritter;
	pInfo->count = 1;
	eaPush(peaCritterInfo, pInfo);

	return pInfo;
}


DupMsg_FSMInfo *DupMsg_GetFSMInfo(DupMsg_FSMInfo ***peaFSMInfo, FSM *pFSM)
{
	DupMsg_FSMInfo *pInfo = NULL;
	int i;

	for(i=eaSize(peaFSMInfo)-1; i>=0; --i) {
		pInfo = (*peaFSMInfo)[i];
		if (pInfo->pFSM == pFSM) {
			++pInfo->count;
			return pInfo;
		}
	}

	pInfo = calloc(1, sizeof(DupMsg_FSMInfo));
	pInfo->pFSM = pFSM;
	pInfo->count = 1;
	eaPush(peaFSMInfo, pInfo);

	return pInfo;
}


void DupMsg_AnalyzeEncDefForMessages(EncounterLayer *pLayer, DupMsg_CritterInfo ***peaCritterInfo, DupMsg_CritterGroupInfo ***peaGroupInfo, DupMsg_FSMInfo ***peaFSMInfo, EncounterDef *pDef)
{
	int i,j;

	for(i=eaSize(&pDef->actors)-1; i>=0; --i) {
		OldActor *pActor = pDef->actors[i];
		CritterDef *pCritter = NULL;
		DupMsg_CritterInfo *pCritterInfo;
		FSM *pFSM = NULL;

		if (GET_REF(pActor->displayNameMsg.hMessage)) {
			// Display name override
		}

		if (pActor->details.info) {
			pCritter = GET_REF(pActor->details.info->critterDef);
		}
		if (pCritter) {
			pCritterInfo = DupMsg_GetCritterInfo(peaCritterInfo, pCritter);

			if (pActor->details.aiInfo) {
				OldActorAIInfo *pInfo = pActor->details.aiInfo;

				for(j=eaSize(&pInfo->actorVars)-1; j>=0; --j) {
					DupMsg_AddCritterVar(pCritterInfo, pInfo->actorVars[j]);
				}
			}
		} else {
			CritterGroup *pGroup = NULL;

			if (pActor->details.info) {
				pGroup = GET_REF(pActor->details.info->critterGroup);
			}
			if (!pGroup) {
				pGroup = GET_REF(pDef->critterGroup);
			}
			if (pGroup) {
				DupMsg_CritterGroupInfo * pGroupInfo = DupMsg_GetCritterGroupInfo(peaGroupInfo, pGroup);

				if (pActor->details.aiInfo) {
					OldActorAIInfo *pInfo = pActor->details.aiInfo;

					for(j=eaSize(&pInfo->actorVars)-1; j>=0; --j) {
						DupMsg_AddGroupVar(pGroupInfo, pInfo->actorVars[j]);
					}
				}
			} else {
				printf("## Critter & group are missing on layer %s, actor %d!\n", pLayer->pchFilename, pActor->uniqueID);
			}
		}

		if (pActor->details.aiInfo) {
			pFSM = GET_REF(pActor->details.aiInfo->hFSM);
		}
		if (!pFSM && pCritter) {
			pFSM = GET_REF(pCritter->hFSM);
		}
		if (pFSM) {
			DupMsg_FSMInfo *pFSMInfo = DupMsg_GetFSMInfo(peaFSMInfo, pFSM);
			if (pActor->details.aiInfo) {
				OldActorAIInfo *pInfo = pActor->details.aiInfo;

				for(j=eaSize(&pInfo->actorVars)-1; j>=0; --j) {
					DupMsg_AddFSMVar(pFSMInfo, pInfo->actorVars[j]);
				}
			}
		}

	}
}


void DupMsg_AnalyzeStaticEncForMessages(EncounterLayer *pLayer, DupMsg_CritterInfo ***peaCritterInfo, DupMsg_CritterGroupInfo ***peaGroupInfo, DupMsg_FSMInfo ***peaFSMInfo, OldStaticEncounter *pEnc)
{
	if (GET_REF(pEnc->baseDef)) {
		oldencounter_UpdateStaticEncounterSpawnRule(pEnc, pLayer);
		DupMsg_AnalyzeEncDefForMessages(pLayer, peaCritterInfo, peaGroupInfo, peaFSMInfo, pEnc->spawnRule);
	} else if (pEnc->defOverride) {
		DupMsg_AnalyzeEncDefForMessages(pLayer, peaCritterInfo, peaGroupInfo, peaFSMInfo, pEnc->defOverride);
	}
}


void DupMsg_AnalyzeGroupForMessages(EncounterLayer *pLayer, DupMsg_CritterInfo ***peaCritterInfo, DupMsg_CritterGroupInfo ***peaGroupInfo, DupMsg_FSMInfo ***peaFSMInfo, OldStaticEncounterGroup *pGroup)
{
	int i;

	for(i=eaSize(&pGroup->staticEncList)-1; i>=0; --i) {
		DupMsg_AnalyzeStaticEncForMessages(pLayer, peaCritterInfo, peaGroupInfo, peaFSMInfo, pGroup->staticEncList[i]);
	}
	for(i=eaSize(&pGroup->childList)-1; i>=0; --i) {
		DupMsg_AnalyzeGroupForMessages(pLayer, peaCritterInfo, peaGroupInfo, peaFSMInfo, pGroup->childList[i]);
	}
}


void DupMsg_AnalyzeLayerForMessages(FILE *file, EncounterLayer ***peaLayers)
{
	DupMsg_CritterInfo **eaCritterInfo = NULL;
	DupMsg_CritterGroupInfo **eaGroupInfo = NULL;
	DupMsg_FSMInfo **eaFSMInfo = NULL;
	int i,j;
	int numMsgs = 0, numGroupMsgs = 0, numFSMMsgs = 0;
	int numDups = 0, numGroupDups = 0, numFSMDups = 0;
	int numEasyDups = 0, numGroupEasyDups = 0, numFSMEasyDups = 0;

	for(i=eaSize(peaLayers)-1; i>=0; --i) {
		EncounterLayer *pLayer = (*peaLayers)[i];

		DupMsg_AnalyzeGroupForMessages(pLayer, &eaCritterInfo, &eaGroupInfo, &eaFSMInfo, &pLayer->rootGroup);
	}

	// Report on critters
	for(i=eaSize(&eaCritterInfo)-1; i>=0; --i) {
		DupMsg_CritterInfo *pCritterInfo = eaCritterInfo[i];

		if (eaSize(&pCritterInfo->eaVarInfo) > 0) {
			int matched = 0;
			int total = 0;

			eaQSort(pCritterInfo->eaVarInfo, DupMsg_fixupSortCritterVarInfo);

			for(j=eaSize(&pCritterInfo->eaVarInfo)-1; j>=0; --j) {
				DupMsg_CritterVarInfo *pInfo = pCritterInfo->eaVarInfo[j];
				if (pInfo->pcVarMsg) {
					matched += pInfo->count;
					total += pCritterInfo->count;
				}
			}

			if (total > 0) {
				fprintf(file, "Critter: %s (%g)\n", pCritterInfo->pCritter->pchName, total ? ((F32)matched)/((F32)total) : -1.0);

				for(j=eaSize(&pCritterInfo->eaVarInfo)-1; j>=0; --j) {
					DupMsg_CritterVarInfo *pInfo = pCritterInfo->eaVarInfo[j];
					if (pInfo->pcVarMsg) {
						fprintf(file, "  (%d/%d) Var=%s  Value=%0.100s\n", pInfo->count, pCritterInfo->count, pInfo->pcVarName, pInfo->pcVarMsg ? pInfo->pcVarMsg : pInfo->pcVarValue);
						++numMsgs;
						numDups += (pInfo->count - 1);
						if (pInfo->count == pCritterInfo->count) {
							numEasyDups += (pInfo->count - 1);
						}
					}
				}

				fprintf(file, "\n");
			}
		}
	}

	// Report on groups
	for(i=eaSize(&eaGroupInfo)-1; i>=0; --i) {
		DupMsg_CritterGroupInfo *pGroupInfo = eaGroupInfo[i];

		if (eaSize(&pGroupInfo->eaVarInfo) > 0) {
			int total = 0, matched = 0;

			eaQSort(pGroupInfo->eaVarInfo, DupMsg_fixupSortCritterVarInfo);

			for(j=eaSize(&pGroupInfo->eaVarInfo)-1; j>=0; --j) {
				DupMsg_CritterVarInfo *pInfo = pGroupInfo->eaVarInfo[j];
				if (pInfo->pcVarMsg) {
					matched += pInfo->count;
					total += pGroupInfo->count;
				}
			}

			if (total > 0) {
				fprintf(file, "Group: %s (%g)\n", pGroupInfo->pGroup->pchName, ((F32)matched)/((F32)total));

				for(j=eaSize(&pGroupInfo->eaVarInfo)-1; j>=0; --j) {
					DupMsg_CritterVarInfo *pInfo = pGroupInfo->eaVarInfo[j];
					if (pInfo->pcVarMsg) {
						fprintf(file, "  (%d/%d) Var=%s  Value=%0.100s\n", pInfo->count, pGroupInfo->count, pInfo->pcVarName, pInfo->pcVarMsg ? pInfo->pcVarMsg : pInfo->pcVarValue);
						++numGroupMsgs;
						numGroupDups += (pInfo->count - 1);
						if (pInfo->count == pGroupInfo->count) {
							numGroupEasyDups += (pInfo->count - 1);
						}
					}
				}

				fprintf(file, "\n");
			}
		}
	}

	// Report on FSMs
	for(i=eaSize(&eaFSMInfo)-1; i>=0; --i) {
		DupMsg_FSMInfo *pFSMInfo = eaFSMInfo[i];

		if (eaSize(&pFSMInfo->eaVarInfo) > 0) {
			int total = 0, matched = 0;

			eaQSort(pFSMInfo->eaVarInfo, DupMsg_fixupSortCritterVarInfo);

			for(j=eaSize(&pFSMInfo->eaVarInfo)-1; j>=0; --j) {
				DupMsg_CritterVarInfo *pInfo = pFSMInfo->eaVarInfo[j];
				if (pInfo->pcVarMsg) {
					matched += pInfo->count;
					total += pFSMInfo->count;
				}
			}

			if (total > 0) {
				fprintf(file, "FSM: %s (%g)\n", pFSMInfo->pFSM->name, ((F32)matched)/((F32)total));

				for(j=eaSize(&pFSMInfo->eaVarInfo)-1; j>=0; --j) {
					DupMsg_CritterVarInfo *pInfo = pFSMInfo->eaVarInfo[j];
					if (pInfo->pcVarMsg) {
						fprintf(file, "  (%d/%d) Var=%s  Value=%0.100s\n", pInfo->count, pFSMInfo->count, pInfo->pcVarName, pInfo->pcVarMsg ? pInfo->pcVarMsg : pInfo->pcVarValue);
						++numFSMMsgs;
						numFSMDups += (pInfo->count - 1);
						if (pInfo->count == pFSMInfo->count) {
							numFSMEasyDups += (pInfo->count - 1);
						}
					}
				}

				fprintf(file, "\n");
			}
		}
	}

	fprintf(file, "### Critter Messages: Count=%d  Duplicates=%d  Easy=%d\n", numMsgs, numDups, numEasyDups);
	fprintf(file, "### Group Messages:   Count=%d  Duplicates=%d  Easy=%d\n", numGroupMsgs, numGroupDups, numGroupEasyDups);
	fprintf(file, "### FSM Messages:     Count=%d  Duplicates=%d  Easy=%d\n", numFSMMsgs, numFSMDups, numFSMEasyDups);
}


AUTO_COMMAND ACMD_SERVERCMD;
char* FixupFindDuplicateMessages(void)
{
	EncounterLayer **eaLayers = NULL;
	FILE *outFile=NULL;

	outFile = fopen("c:/duplicatestrings.txt", "w");

	AssetLoadEncLayers(&eaLayers);

	DupMsg_AnalyzeLayerForMessages(outFile, &eaLayers);

	AssetCleanupEncLayers(&eaLayers);

	if(outFile)
		fclose(outFile);

	return "Done";
}

static void LockInAngle(F32* angle) {
	int gDiv;
	F32 remainder;
	F32 degrees;

	degrees = (*angle) * 180 / PI;
	gDiv = degrees / 45;
	remainder = degrees - (45.0 * (F32)gDiv);
	if (remainder != 0.0) {
		if (remainder > 0) {
			if (remainder > 44.0) {
				remainder -= 45.0;
				degrees -= remainder;
				*angle = degrees * PI / 180;
			} else if (remainder < 1.0) {
				degrees -= remainder;
				*angle = degrees * PI / 180;
			}
		} else {
			if (remainder < -44.0) {
				remainder += 45.0;
				degrees -= remainder;
				*angle = degrees * PI / 180;
			} else if (remainder > -1.0) {
				degrees -= remainder;
				*angle = degrees * PI / 180;
			}
		}
	}
	return;
}

AUTO_COMMAND;
// This command will look at all rotations and will lock them into the closest multiple of 45 degrees if it is off by less then a degree
void FixupOffAngles(const char* filename) {
	LibFileLoad	*layerFile;
	GroupDef	**dLoad;
	GroupChild	**gLoad;

	if (!fixupCheckoutFile(filename,true)) {
		return;
	}
	layerFile = loadLayerFromDisk(filename);
	dLoad = layerFile->defs;
	FOR_EACH_IN_EARRAY(dLoad,GroupDef,dObj) {
		gLoad = dObj->children;
		FOR_EACH_IN_EARRAY(gLoad,GroupChild,gObj) {
			LockInAngle(&(gObj->rot[0]));
			LockInAngle(&(gObj->rot[1]));
			LockInAngle(&(gObj->rot[2]));
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
	ParserWriteTextFile(filename, parse_LibFileLoad, layerFile, 0, 0);
	StructDestroy(parse_LibFileLoad, layerFile);

	return;
}

AUTO_COMMAND;
// This command will look at all rotations and will lock them into the closest multiple of 45 degrees if it is off by less then a degree
void FixupCurrentOffAngles(void) {
	ZoneMap *activeMap = worldGetActiveMap();

	FOR_EACH_IN_EARRAY(activeMap->layers,ZoneMapLayer,layer) {
		if (layer->locked) {
			FixupOffAngles(layer->filename);
		}
	}
	FOR_EACH_END;

	return;
}

static bool ObjectExistsInLayer(const char* filename, const char* objName) {
	LibFileLoad	*layerFile;
	GroupDef	**dLoad;
	GroupChild	**gLoad;
	char nameHolder[255];

	layerFile = loadLibFromDisk(filename);
	if (!layerFile)
		return false;
	dLoad = layerFile->defs;
	FOR_EACH_IN_EARRAY(dLoad,GroupDef,dObj) {
		gLoad = dObj->children;
		FOR_EACH_IN_EARRAY(gLoad,GroupChild,gObj) {
			U32 nameSize = (U32)strlen(gObj->name);
			if (gObj->name[nameSize-1] == '&') {
				strncpy(nameHolder, gObj->name, nameSize);
			} else {
				strcpy(nameHolder, gObj->name);
			}
			if (strcmpi(nameHolder, objName) == 0) {
				StructDestroy(parse_LibFileLoad, layerFile);
				return true;
			}
		} FOR_EACH_END;
	} FOR_EACH_END;

	StructDestroy(parse_LibFileLoad, layerFile);
	return false;
}

AUTO_COMMAND;
void CopyAllObjectLocations(const char* objName) {
	RefDictIterator	mapIterator;
	ZoneMapInfo		*mapInfo;
	char			fullPath[MAX_PATH];
	FILE			*objfile;

	sprintf(fullPath,"Export/TXT/%s_locationListing.txt",objName);
	makeDirectoriesForFile(fullPath);
	objfile = fileOpen(fullPath, "w");
	if (!objfile)
	{
		return;
	}

	worldGetZoneMapIterator(&mapIterator);
	while (mapInfo = worldGetNextZoneMap(&mapIterator)) {
		const char *pMapPublicName = zmapInfoGetPublicName(mapInfo);
		FOR_EACH_IN_EARRAY(mapInfo->layers,ZoneMapLayerInfo,mapLayer) {
			if (ObjectExistsInLayer(mapLayer->filename, objName)) {
				fprintf(objfile,"%s\n",mapLayer->filename);
			}
		} FOR_EACH_END;
	}
	fileClose(objfile);
}

typedef struct FindDupMsg_Data {
	FILE* logFile;
	StashTable messageStash;
} FindDupMsg_Data;

FileScanAction FindDupMsg_ProcessDir( char* dir, struct _finddata32_t* fileData, void* rawData )
{
	FindDupMsg_Data* data = (FindDupMsg_Data*)rawData;
	
	if( fileData->name[0] == '_' ) {
		return FSA_NO_EXPLORE_DIRECTORY;
	} else if( fileData->attrib & _A_SUBDIR ) {
		return FSA_EXPLORE_DIRECTORY;
	} else {
		char filename[ MAX_PATH ];

		sprintf( filename, "%s/%s", dir, fileData->name );

		if( !strEndsWith( filename, ".ms" )) {
			return FSA_EXPLORE_DIRECTORY;
		} else {
			MessageList msgList = { 0 };
			ParserReadTextFile( filename, parse_MessageList, &msgList, 0 );

			{
				int it;
				for( it = 0; it != eaSize( &msgList.eaMessages ); ++it ) {
					const char* key = msgList.eaMessages[it]->pcMessageKey;
					char* otherFilename;

					if( stashFindPointer( data->messageStash, key, &otherFilename )) {
						fprintf( data->logFile, "File: %s, File: %s -- Duplicate message key %s found.\n",
								 filename, otherFilename, key );
					} else {
						stashAddPointer( data->messageStash, key, msgList.eaMessages[it]->pcFilename, true );
					}
				}
			}

			StructDeInit( parse_MessageList, &msgList );
		}
	}

	return FSA_EXPLORE_DIRECTORY;
}

/// This command looks at all messages and reports any duplicate keys
/// it finds.
AUTO_COMMAND;
void FindDupMessageKeys(void)
{
	FindDupMsg_Data data = { 0 };
	data.messageStash = stashTableCreateWithStringKeys( 256, StashDefault );
	data.logFile = fopen( "C:/DupMsgKeys.txt", "w" );

	if( !data.logFile ) {
		Alertf( "Could not open C:/DupMsgKeys.txt for writting." );
		stashTableDestroy( data.messageStash );
		return;
	}

	fileScanAllDataDirs( "ui/", FindDupMsg_ProcessDir, &data );
	fileScanAllDataDirs( "messages/", FindDupMsg_ProcessDir, &data );
	fileScanAllDataDirs( "defs/", FindDupMsg_ProcessDir, &data );
	fileScanAllDataDirs( "ai/", FindDupMsg_ProcessDir, &data );
	fileScanAllDataDirs( "maps/", FindDupMsg_ProcessDir, &data );
	fileScanAllDataDirs( "object_library/", FindDupMsg_ProcessDir, &data );
	fileScanAllDataDirs( "genesis/", FindDupMsg_ProcessDir, &data );

	fclose( data.logFile );
	stashTableDestroy( data.messageStash );

	Alertf( "Duplicate data logged to C:/DupMsgKeys.txt." );
}

/// Checkout a list of files from Gimme in bulk.
AUTO_COMMAND ACMD_SERVERCMD;
void fixupCheckoutFiles( const char* fileList )
{
	FILE* file = fopen( fileList, "r" );
	char fileName[ 256 ];
	char** fileNames = NULL;

	while( fgets( fileName, sizeof( fileName ), file )) {
		if( !StringIsAllWhiteSpace( fileName )) {
			const char* buffer;

			forwardSlashes( fileName );
			removeTrailingWhiteSpaces( fileName );
			buffer = removeLeadingWhiteSpaces( fileName );
			
			eaPush( &fileNames, strdup( buffer ));
		}
	}
	fclose( file );
	// AssetMaster gets locked up while a huge checkout is happening.
	// Batching up the checkout makes this command less-DoSy.
	gimmeDLLDoOperations(fileNames, GIMME_CHECKOUT, GIMME_QUIET);
	eaDestroyEx( &fileNames, NULL );
}

//////////////////////////////////////////////////
// Fixup the Object Library:
//   - Check for private GroupDefs in a different
//     file from parent
//   - Check for private GroupDefs whose name
//     doesn't match the parent
//////////////////////////////////////////////////

typedef struct ObjFixup1
{
	GroupDef *def;
	const char *new_name;
} ObjFixup1;

typedef struct ObjFixup2
{
	const char *filename;
	ObjFixup1 **fixups;
} ObjFixup2;

static void objectLibraryFixupNames(GroupDef *root, GroupDef *parent, StashTable fixups)
{
	int i;
	for (i = 0; i < eaSize(&parent->children); i++)
	{
		GroupChild *child = parent->children[i];
		GroupDef *child_def = groupChildGetDef(parent, child, true);
		if (child_def && groupIsPrivate(child_def))
		{
			if (child_def->filename != root->filename)
			{
				filelog_printf("ObjLibFixes.log", "CROSS-FILE PRIVATE DEF REFERENCE IN %s: %d %s, BY %d %s IN %s\n", child_def->filename, child_def->name_uid, child_def->name_str, root->name_uid, root->name_str, root->filename);
			}
			/* Don't care about this case anymore --TomY
			else if (strnicmp(child_def->name_str, root->name_str, strlen(root->name_str)))
			{
				// Fix up name
				ObjFixup2 *fixup = NULL;
				ObjFixup1 *new_fix = NULL;
				char newname[256];

				groupLibMakeGroupName(child_def->def_lib, root->name_str, SAFESTR(newname), true);

				if (!stashFindPointer(fixups, child_def->filename, &fixup))
				{
					fixup = calloc(1, sizeof(ObjFixup2));
					fixup->filename = child_def->filename;
					stashAddPointer(fixups, child_def->filename, fixup, true);
				}
				new_fix = calloc(1, sizeof(ObjFixup1));
				new_fix->def = child_def;
				new_fix->new_name = allocAddString(newname);
				eaPush(&fixup->fixups, new_fix);

				{
					GroupDef *dummy = StructCreate(parse_GroupDef);
					dummy->name_str = new_fix->new_name;
					stashAddPointer(child_def->def_lib->defs_by_name, new_fix->new_name, dummy, false);
				}
			}
			*/
			objectLibraryFixupNames(root, child_def, fixups);
		}
	}
}

AUTO_COMMAND;
void objectLibraryFixupAllFiles(bool dry_run)
{
	StashTable fixups = stashTableCreateWithStringKeys(100, StashDefault);
	GroupDefLib *def_lib = objectLibraryGetDefLib();
	FOR_EACH_IN_STASHTABLE(def_lib->defs, GroupDef, def)
	{
		if (groupIsPublic(def))
		{
			objectLibraryFixupNames(def, def, fixups);
		}
	}
	FOR_EACH_END;
	FOR_EACH_IN_STASHTABLE(fixups, ObjFixup2, fixup2)
	{
		GimmeErrorValue val = GIMME_NO_ERROR;
		GroupDefList *list = StructCreate(parse_GroupDefList);
		filelog_printf("ObjLibFixes.log", "IN FILE %s:\n", fixup2->filename);
		if (!ParserReadTextFile(fixup2->filename, parse_GroupDefList, list, 0))
		{
			filelog_printf("ObjLibFixes.log", "ERROR OPENING %s!\n", fixup2->filename);
			continue;
		}
		if (!dry_run)
			val = gimmeDLLDoOperation(fixup2->filename, GIMME_CHECKOUT, GIMME_QUIET);
		if (!dry_run && val != GIMME_NO_ERROR && val != GIMME_ERROR_NOT_IN_DB && val != GIMME_ERROR_ALREADY_DELETED)
		{
			const char *lockee;
			filelog_printf("ObjLibFixes.log", "ERROR CHECKING OUT %s!\n", fixup2->filename);
			if (val == GIMME_ERROR_ALREADY_CHECKEDOUT && (lockee = gimmeDLLQueryIsFileLocked(fixup2->filename))) {
				filelog_printf("ObjLibFixes.log", "File \"%s\" unable to be checked out, currently checked out by %s", fixup2->filename, lockee);
			} else {
				filelog_printf("ObjLibFixes.log", "File \"%s\" unable to be checked out (%s)", fixup2->filename, gimmeDLLGetErrorString(val));
			}
			continue;
		}
		FOR_EACH_IN_EARRAY(list->defs, GroupDef, def)
		{
			FOR_EACH_IN_EARRAY(fixup2->fixups, ObjFixup1, fixup1)
			{
				if (def->name_uid == fixup1->def->name_uid ||
					!stricmp(def->name_str, fixup1->def->name_str))
				{
					def->name_str = fixup1->new_name;
					filelog_printf("ObjLibFixes.log", "RENAMED %d %s TO %s\n", fixup1->def->name_uid, fixup1->def->name_str, fixup1->new_name);
					break;
				}
			}
			FOR_EACH_END;

			FOR_EACH_IN_EARRAY(def->children, GroupChild, child)
			{
				FOR_EACH_IN_EARRAY(fixup2->fixups, ObjFixup1, fixup1)
				{
					if (child->name_uid == fixup1->def->name_uid ||
						!stricmp(child->name, fixup1->def->name_str))
					{
						child->name = fixup1->new_name;
						filelog_printf("ObjLibFixes.log", "FIXED REF %d %s TO %s\n", fixup1->def->name_uid, fixup1->def->name_str, fixup1->new_name);
						break;
					}
				}
				FOR_EACH_END;
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
		if (!dry_run)
			ParserWriteTextFile(fixup2->filename, parse_GroupDefList, list, 0, 0);
	}
	FOR_EACH_END;
}

static bool gameaction_FindBadMessages(WorldGameActionProperties *pAction, const char *pcBaseMessageKey, int iIndex, FILE* pFile, const char* pchFileName, const char*** peaVisitedKeys)
{
	char buf1[1024];
	bool bBadMessageFound = false;

	if (pAction->eActionType == WorldGameActionType_SendFloaterMsg) {
		DisplayMessage *pDispMsg = &pAction->pSendFloaterProperties->floaterMsg;
		Message* pMsg = pDispMsg->bEditorCopyIsServer ? pDispMsg->pEditorCopy : GET_REF(pDispMsg->hMessage);
		if (pMsg) {
			sprintf(buf1, "%s.action_%d.floater", pcBaseMessageKey, iIndex);
			if(stricmp(buf1, pMsg->pcMessageKey) != 0 && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
			{
				fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pchFileName, pMsg->pcMessageKey );
				bBadMessageFound = true;
			}
			eaPush(peaVisitedKeys, pMsg->pcMessageKey);
		}
	} else if (pAction->eActionType == WorldGameActionType_SendNotification) {
		DisplayMessage *pDispMsg = &pAction->pSendNotificationProperties->notifyMsg;
		Message* pMsg = pDispMsg->bEditorCopyIsServer ? pDispMsg->pEditorCopy : GET_REF(pDispMsg->hMessage);
		if (pMsg) {
			sprintf(buf1, "%s.action_%d.notify", pcBaseMessageKey, iIndex);
			if(stricmp(buf1, pMsg->pcMessageKey) != 0 && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
			{
				fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pchFileName, pMsg->pcMessageKey );
				bBadMessageFound = true;
			}
			eaPush(peaVisitedKeys, pMsg->pcMessageKey);
		}
	} else if (pAction->eActionType == WorldGameActionType_Warp) {
		int i;

		for(i=eaSize(&pAction->pWarpProperties->eaVariableDefs)-1; i>=0; --i) {
			DisplayMessage *pDispMsg = SAFE_MEMBER_ADDR( pAction->pWarpProperties->eaVariableDefs[i]->pSpecificValue, messageVal );
			Message* pMsg = pDispMsg->bEditorCopyIsServer ? pDispMsg->pEditorCopy : GET_REF(pDispMsg->hMessage);
			if (pMsg) {
				if (pDispMsg->pEditorCopy) {
					sprintf(buf1, "%s.action_%d.warp.var_%d", pcBaseMessageKey, iIndex, i);
					if(stricmp(buf1, pMsg->pcMessageKey) != 0 && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
					{
						fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pchFileName, pMsg->pcMessageKey );
						bBadMessageFound = true;
					}
					eaPush(peaVisitedKeys, pMsg->pcMessageKey);
				}
			}
		}
	}
	else if (pAction->eActionType == WorldGameActionType_NPCSendMail) {
		DisplayMessage *pDispMsg = &pAction->pNPCSendEmailProperties->dFromName;
		Message* pMsg = pDispMsg->bEditorCopyIsServer ? pDispMsg->pEditorCopy : GET_REF(pDispMsg->hMessage);
		if (pMsg) {
			sprintf(buf1, "%s.action_%d.FromName", pcBaseMessageKey, iIndex);
			if(stricmp(buf1, pMsg->pcMessageKey) != 0 && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
			{
				fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pchFileName, pMsg->pcMessageKey );
				bBadMessageFound = true;
			}
			eaPush(peaVisitedKeys, pMsg->pcMessageKey);
		}
		pDispMsg = &pAction->pNPCSendEmailProperties->dSubject;
		pMsg = pDispMsg->bEditorCopyIsServer ? pDispMsg->pEditorCopy : GET_REF(pDispMsg->hMessage);
		if (pMsg) {
			sprintf(buf1, "%s.action_%d.Subject", pcBaseMessageKey, iIndex);
			if(stricmp(buf1, pMsg->pcMessageKey) != 0 && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
			{
				fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pchFileName, pMsg->pcMessageKey );
				bBadMessageFound = true;
			}
			eaPush(peaVisitedKeys, pMsg->pcMessageKey);
		}
		pDispMsg = &pAction->pNPCSendEmailProperties->dBody;
		pMsg = pDispMsg->bEditorCopyIsServer ? pDispMsg->pEditorCopy : GET_REF(pDispMsg->hMessage);
		if (pMsg) {
			sprintf(buf1, "%s.action_%d.Body", pcBaseMessageKey, iIndex);
			if(stricmp(buf1, pMsg->pcMessageKey) != 0 && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
			{
				fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pchFileName, pMsg->pcMessageKey );
				bBadMessageFound = true;
			}
			eaPush(peaVisitedKeys, pMsg->pcMessageKey);
		}
	}
	else if (pAction->eActionType == WorldGameActionType_ShardVariable)
	{
		if ((pAction->pShardVariableProperties->eModifyType == WorldVariableActionType_Set) &&
			pAction->pShardVariableProperties->pVarValue &&
			(pAction->pShardVariableProperties->pVarValue->eType == WVAR_MESSAGE)) 
		{
			DisplayMessage *pDispMsg = &pAction->pShardVariableProperties->pVarValue->messageVal;
			Message* pMsg = pDispMsg->bEditorCopyIsServer ? pDispMsg->pEditorCopy : GET_REF(pDispMsg->hMessage);
			if (pMsg) {
				sprintf(buf1, "%s.action_%d.setshardvar", pcBaseMessageKey, iIndex);
				if(stricmp(buf1, pMsg->pcMessageKey) != 0 && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
				{
					fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pchFileName, pMsg->pcMessageKey );
					bBadMessageFound = true;
				}
				eaPush(peaVisitedKeys, pMsg->pcMessageKey);
			}
		}
	}
	return bBadMessageFound;
}

static bool interaction_FindBadMessages(WorldInteractionPropertyEntry *pEntry, const char *pcBaseMessageKey, const char *pcSubKey, int iIndex, FILE* pFile, const char* pchFileName, const char*** peaVisitedKeys)
{
	char buf1[1024];
	bool bBadMessageFound = false;

	if (pEntry->pDestructibleProperties) {
		Message* pMsg = pEntry->pDestructibleProperties->displayNameMsg.bEditorCopyIsServer ? pEntry->pDestructibleProperties->displayNameMsg.pEditorCopy : GET_REF(pEntry->pDestructibleProperties->displayNameMsg.hMessage);
		sprintf(buf1, "%s.InteractableProps.%s.%d.DestructibleName", pcBaseMessageKey, pcSubKey, 0);
		if (pMsg) {
			if(stricmp(buf1, pMsg->pcMessageKey) != 0 && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
			{
				fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pchFileName, pMsg->pcMessageKey );
				bBadMessageFound = true;
			}
			eaPush(peaVisitedKeys, pMsg->pcMessageKey);
		}
	}

	if (pEntry->pTextProperties) {
		Message* pMsg = pEntry->pTextProperties->interactOptionText.bEditorCopyIsServer ? pEntry->pTextProperties->interactOptionText.pEditorCopy : GET_REF(pEntry->pTextProperties->interactOptionText.hMessage);
		sprintf(buf1, "%s.InteractableProps.%s.%d.InteractText", pcBaseMessageKey, pcSubKey, iIndex);
		if (pMsg) {
			if(stricmp(buf1, pMsg->pcMessageKey) != 0 && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
			{
				fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pchFileName, pMsg->pcMessageKey );
				bBadMessageFound = true;
			}
			eaPush(peaVisitedKeys, pMsg->pcMessageKey);
		}

		sprintf(buf1, "%s.InteractableProps.%s.%d.SuccessText", pcBaseMessageKey, pcSubKey, iIndex);
		pMsg = pEntry->pTextProperties->successConsoleText.bEditorCopyIsServer ? pEntry->pTextProperties->successConsoleText.pEditorCopy : GET_REF(pEntry->pTextProperties->successConsoleText.hMessage);
		if (pMsg) {
			if(stricmp(buf1, pMsg->pcMessageKey) != 0 && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
			{
				fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pchFileName, pMsg->pcMessageKey );
				bBadMessageFound = true;
			}
			eaPush(peaVisitedKeys, pMsg->pcMessageKey);
		}

		sprintf(buf1, "%s.InteractableProps.%s.%d.FailureText", pcBaseMessageKey, pcSubKey, iIndex);
		pMsg = pEntry->pTextProperties->failureConsoleText.bEditorCopyIsServer ? pEntry->pTextProperties->failureConsoleText.pEditorCopy : GET_REF(pEntry->pTextProperties->failureConsoleText.hMessage);
		if (pMsg) {
			if(stricmp(buf1, pMsg->pcMessageKey) != 0 && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
			{
				fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pchFileName, pMsg->pcMessageKey );
				bBadMessageFound = true;
			}
			eaPush(peaVisitedKeys, pMsg->pcMessageKey);
		}
	}

	if (pEntry->pActionProperties) {
		int i;
		for(i=eaSize(&pEntry->pActionProperties->successActions.eaActions)-1; i>=0; --i) {
			WorldGameActionProperties *pAction = pEntry->pActionProperties->successActions.eaActions[i];
			sprintf(buf1, "%s.InteractableProps.%s.%d.GameAction", pcBaseMessageKey, pcSubKey, iIndex);
			bBadMessageFound = bBadMessageFound || gameaction_FindBadMessages(pAction, buf1, i, pFile, pchFileName, peaVisitedKeys);
		}
	}

	if (pEntry->pDoorProperties) {
		int i;
		for(i=eaSize(&pEntry->pDoorProperties->eaVariableDefs)-1; i>=0; --i) {
			WorldVariableDef *pVarDef = pEntry->pDoorProperties->eaVariableDefs[i];
			if ((pVarDef->eType == WVAR_MESSAGE) && pVarDef->pSpecificValue) {
				DisplayMessage *pDispMsg = &pVarDef->pSpecificValue->messageVal;
				Message* pMsg = pDispMsg->bEditorCopyIsServer ?  pDispMsg->pEditorCopy : GET_REF( pDispMsg->hMessage);
				sprintf(buf1, "%s.InteractableProps.%s.%d.doorvars.%s.id%d", pcBaseMessageKey, pcSubKey, iIndex, pVarDef->pcName, i);
				if (pMsg) {
					if(stricmp(buf1, pMsg->pcMessageKey) != 0 && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
					{
						fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pchFileName, pMsg->pcMessageKey );
						bBadMessageFound = true;
					}
					eaPush(peaVisitedKeys, pMsg->pcMessageKey);
				}
			}
		}
	}
	return bBadMessageFound;
}

static bool gameaction_FindBadMessageList(WorldGameActionProperties*** peaActionList, const char *pcBaseMessageKey, int iBaseIndex, FILE* pFile, const char* pchFileName, const char*** peaVisitedKeys)
{
	int i;
	bool bBadMessageFound = false;

	for(i=eaSize(peaActionList)-1; i>=0; --i) {
		WorldGameActionProperties *pAction = (*peaActionList)[i];
		bBadMessageFound = bBadMessageFound || gameaction_FindBadMessages(pAction, pcBaseMessageKey, i+iBaseIndex, pFile, pchFileName, peaVisitedKeys);
	}
	return bBadMessageFound;
}

static bool mission_FindBadTemplateVarMessages(MissionDef *pRootMission, MissionDef *pMission, TemplateVariableGroup *pGroup, FILE* pFile, const char*** peaVisitedKeys)
{
	char buf1[1024];
	int i;
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseObjectName[RESOURCE_NAME_MAX_SIZE];
	char baseMessageKey[RESOURCE_NAME_MAX_SIZE];
	bool bBadMessageFound = false;

	if (resExtractNameSpace(pMission->pchRefString, nameSpace, baseObjectName))
	{
		sprintf(baseMessageKey, "%s:MissionDef.%s", nameSpace, baseObjectName);
	}
	else
	{
		sprintf(baseMessageKey, "MissionDef.%s", pMission->pchRefString);
	}

	for(i=eaSize(&pGroup->variables)-1; i>=0; --i) {
		TemplateVariable *pVar = pGroup->variables[i];
		if (pVar->varType == TemplateVariableType_Message) {
			const char *pcMessageKey = MultiValGetString(&pVar->varValue, NULL);
			if (pcMessageKey) {
				DisplayMessage *pDispMsg = langGetDisplayMessageFromList(&pMission->varMessageList, pcMessageKey, true);
				Message* pMsg = pDispMsg->bEditorCopyIsServer ? pDispMsg->pEditorCopy : GET_REF(pDispMsg->hMessage);
				if (pMsg) {
					sprintf(buf1, "%s.vars.%s.id%d", baseMessageKey, pVar->varName, pVar->id);
					if(stricmp(buf1, pMsg->pcMessageKey) != 0 && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
					{
						fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pMission->filename, pMsg->pcMessageKey );
						bBadMessageFound = true;
					}
					eaPush(peaVisitedKeys, pMsg->pcMessageKey);
				}
			}
		}
	}

	for(i=eaSize(&pGroup->subGroups)-1; i>=0; --i) {
		bBadMessageFound = bBadMessageFound || mission_FindBadTemplateVarMessages(pRootMission, pMission, pGroup->subGroups[i], pFile, peaVisitedKeys);
	}
	return bBadMessageFound;
}

static bool mission_FindBadMessages(MissionDef *pRootMission, MissionDef *pMission, FILE* pFile, const char*** peaVisitedKeys)
{
	char buf1[1024];
	int i;
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseObjectName[RESOURCE_NAME_MAX_SIZE];
	char baseMessageKey[RESOURCE_NAME_MAX_SIZE];
	char* estrNewKey = NULL;
	Message* pMsg = NULL;
	bool bBadMessageFound = false;

	estrCreate(&estrNewKey);

	nameSpace[0] = '\0';
	if( pRootMission == pMission )
	{
		if (resExtractNameSpace(pMission->pchRefString, nameSpace, baseObjectName))
		{
			sprintf(baseMessageKey, "%s:MissionDef.%s", nameSpace, baseObjectName);
		}
		else
		{
			sprintf(baseMessageKey, "MissionDef.%s", pMission->pchRefString);
		}
	}
	else
	{
		if (resExtractNameSpace(pRootMission->pchRefString, nameSpace, baseObjectName))
		{
			sprintf(baseMessageKey, "%s:MissionDef.%s::%s", nameSpace, baseObjectName, pMission->name);
		}
		else
		{
			sprintf(baseMessageKey, "MissionDef.%s::%s", pRootMission->pchRefString, pMission->name);
		}
	}

	sprintf(buf1, "%s.DisplayName", baseMessageKey);
	pMsg = pMission->displayNameMsg.bEditorCopyIsServer ? pMission->displayNameMsg.pEditorCopy : GET_REF(pMission->displayNameMsg.hMessage);
	if(pMsg) {
		int idx = eaFindString(peaVisitedKeys, pMsg->pcMessageKey);
		estrPrintf(&estrNewKey, "%s", msgCreateUniqueKey(MKP_MISSIONNAME, buf1, pMsg->pcMessageKey));
		if(stricmp(estrNewKey, pMsg->pcMessageKey) != 0 && !strStartsWith(pMsg->pcMessageKey, "Missiondef.") && idx > -1)
		{
			fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pMission->filename, pMsg->pcMessageKey );
			bBadMessageFound = true;
		}
		eaPush(peaVisitedKeys, pMsg->pcMessageKey);
	}

	sprintf(buf1, "%s.UIString", baseMessageKey);
	pMsg = pMission->uiStringMsg.bEditorCopyIsServer ? pMission->uiStringMsg.pEditorCopy : GET_REF(pMission->uiStringMsg.hMessage);
	if(pMsg) {
		estrPrintf(&estrNewKey, "%s", msgCreateUniqueKey(MKP_MISSIONUISTR, buf1, pMsg->pcMessageKey));
		if(stricmp(estrNewKey, pMsg->pcMessageKey) != 0 && !strStartsWith(pMsg->pcMessageKey, "Missiondef.") && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
		{
			fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pMission->filename, pMsg->pcMessageKey );
			bBadMessageFound = true;
		}
		eaPush(peaVisitedKeys, pMsg->pcMessageKey);
	}

	sprintf(buf1, "%s.DetailString", baseMessageKey);
	pMsg = pMission->detailStringMsg.bEditorCopyIsServer ? pMission->detailStringMsg.pEditorCopy : GET_REF(pMission->detailStringMsg.hMessage);
	if(pMsg) {
		estrPrintf(&estrNewKey, "%s", msgCreateUniqueKey(MKP_MISSIONDETAIL, buf1, pMsg->pcMessageKey));
		if(stricmp(estrNewKey, pMsg->pcMessageKey) != 0 && !strStartsWith(pMsg->pcMessageKey, "Missiondef.") && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
		{
			fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pMission->filename, pMsg->pcMessageKey );
			bBadMessageFound = true;
		}
		eaPush(peaVisitedKeys, pMsg->pcMessageKey);
	}

	sprintf(buf1, "%s.Summary", baseMessageKey);
	pMsg = pMission->summaryMsg.bEditorCopyIsServer ? pMission->summaryMsg.pEditorCopy : GET_REF(pMission->summaryMsg.hMessage);
	if(pMsg) {
		estrPrintf(&estrNewKey, "%s", msgCreateUniqueKey(MKP_MISSIONSUMMARY, buf1, pMsg->pcMessageKey));
		if(stricmp(estrNewKey, pMsg->pcMessageKey) != 0 && !strStartsWith(pMsg->pcMessageKey, "Missiondef.") && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
		{
			fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pMission->filename, pMsg->pcMessageKey );
			bBadMessageFound = true;
		}
		eaPush(peaVisitedKeys, pMsg->pcMessageKey);
	}

	sprintf(buf1, "%s.FailureString", baseMessageKey);
	pMsg = pMission->failureMsg.bEditorCopyIsServer ? pMission->failureMsg.pEditorCopy : GET_REF(pMission->failureMsg.hMessage);
	if(pMsg) {
		estrPrintf(&estrNewKey, "%s", msgCreateUniqueKey(MKP_MISSIONFAIL, buf1, pMsg->pcMessageKey));
		if(stricmp(estrNewKey, pMsg->pcMessageKey) != 0 && !strStartsWith(pMsg->pcMessageKey, "Missiondef.") && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
		{
			fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pMission->filename, pMsg->pcMessageKey );
			bBadMessageFound = true;
		}
		eaPush(peaVisitedKeys, pMsg->pcMessageKey);
	}

	sprintf(buf1, "%s.FailReturnString", baseMessageKey);
	pMsg = pMission->failReturnMsg.bEditorCopyIsServer ? pMission->failReturnMsg.pEditorCopy : GET_REF(pMission->failReturnMsg.hMessage);
	if(pMsg) {
		estrPrintf(&estrNewKey, "%s", msgCreateUniqueKey(MKP_MISSIONFAILRETURN, buf1, pMsg->pcMessageKey));
		if(stricmp(estrNewKey, pMsg->pcMessageKey) != 0 && !strStartsWith(pMsg->pcMessageKey, "Missiondef.") && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
		{
			fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pMission->filename, pMsg->pcMessageKey );
			bBadMessageFound = true;
		}
		eaPush(peaVisitedKeys, pMsg->pcMessageKey);
	}

	sprintf(buf1, "%s.ReturnString", baseMessageKey);
	pMsg = pMission->msgReturnStringMsg.bEditorCopyIsServer ? pMission->msgReturnStringMsg.pEditorCopy : GET_REF(pMission->msgReturnStringMsg.hMessage);
	if(pMsg) {
		estrPrintf(&estrNewKey, "%s", msgCreateUniqueKey(MKP_MISSIONRETURN, buf1, pMsg->pcMessageKey));
		if(stricmp(estrNewKey, pMsg->pcMessageKey) != 0 && !strStartsWith(pMsg->pcMessageKey, "Missiondef.") && eaFindString(peaVisitedKeys, pMsg->pcMessageKey) > -1)
		{
			fprintf( pFile, "File: %s.ms -- Bad message key %s found.\n", pMission->filename, pMsg->pcMessageKey );
			bBadMessageFound = true;
		}
		eaPush(peaVisitedKeys, pMsg->pcMessageKey);
	}

	estrDestroy(&estrNewKey);

	// fix Template variables
	if (pMission->missionTemplate) {
		bBadMessageFound = bBadMessageFound || mission_FindBadTemplateVarMessages(pRootMission, pMission, pMission->missionTemplate->rootVarGroup, pFile, peaVisitedKeys);
	}

	// fix Action variables
	bBadMessageFound = bBadMessageFound || gameaction_FindBadMessageList(&pMission->ppOnStartActions, baseMessageKey, 0, pFile, pMission->filename, peaVisitedKeys);
	bBadMessageFound = bBadMessageFound || gameaction_FindBadMessageList(&pMission->ppSuccessActions, baseMessageKey, eaSize(&pMission->ppOnStartActions), pFile, pMission->filename, peaVisitedKeys);
	bBadMessageFound = bBadMessageFound || gameaction_FindBadMessageList(&pMission->ppFailureActions, baseMessageKey, eaSize(&pMission->ppOnStartActions) + eaSize(&pMission->ppSuccessActions), pFile, pMission->filename, peaVisitedKeys);
	bBadMessageFound = bBadMessageFound || gameaction_FindBadMessageList(&pMission->ppOnReturnActions, baseMessageKey, eaSize(&pMission->ppOnStartActions) + eaSize(&pMission->ppSuccessActions) + eaSize(&pMission->ppFailureActions), pFile, pMission->filename, peaVisitedKeys);

	// Fixup interactable override messages
	for(i=0; i<eaSize(&pMission->ppInteractableOverrides); i++) {
		InteractableOverride *pOverride = pMission->ppInteractableOverrides[i];
		if (pOverride->pcMapName) {
			sprintf(buf1, "%s.%s", baseMessageKey, pOverride->pcMapName);
		} else {
			sprintf(buf1, "%s.NO_MAP", baseMessageKey);
		}
		bBadMessageFound = bBadMessageFound || interaction_FindBadMessages(pOverride->pPropertyEntry, buf1, pOverride->pcInteractableName, i, pFile, pMission->filename, peaVisitedKeys);
	}

	// fix submissions
	for(i=eaSize(&pMission->subMissions)-1; i>=0; --i) {
		bBadMessageFound = bBadMessageFound || mission_FindBadMessages(pRootMission, pMission->subMissions[i], pFile, peaVisitedKeys);
	}
	return bBadMessageFound;
}

// Parses through all non-genesis missions, finds bad message keys
AUTO_COMMAND;
void Fixup_AllMissions_FindBadMessages(void)
{
	RefDictIterator iterator = {0};
	MissionDef *pDef = NULL;
	MissionDef **changedDefCopies = NULL;
	const char **eaVisitedKeys = NULL;
	FILE* pFile = NULL;
	pFile = fopen( "C:/BadMissionMessages.txt", "w" );

	if( !pFile ) {
		Alertf( "Could not open C:/BadMissionMessages.txt for writting." );
		return;
	}

	// Collect the lists of special dialogs and actions that target other dialogs
	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);
	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator)) 
	{
		if(!GET_REF(pDef->parentDef) && !pDef->genesisZonemap && (!pDef->comments || !strstr(pDef->comments, "Genesis")) && !strStartsWith(pDef->name, "Sc") )
		{
			if(mission_FindBadMessages(pDef, pDef, pFile, &eaVisitedKeys))
			{
				fprintf( pFile, "*** BAD MESSAGES FOUND IN MISSION: %s\n\n", pDef->name );
			}
		}
	}

	if(eaVisitedKeys)
		eaDestroy(&eaVisitedKeys);

	fclose(pFile);
}

/// Fixup to set CanRepeat on star cluster missions
AUTO_COMMAND;
void FixStarClusterMissions_CanRepeat()
{
	int i,n = 0;
	int iNumFixed = 0;
	int iNumSaved = 0;
	RefDictIterator iterator = {0};
	MissionDef *pDef = NULL;
	MissionDef **changedDefCopies = NULL;

	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);

	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		if(!pDef->repeatable && strStartsWith(pDef->name, "Sc_") && pDef->missionType != MissionType_OpenMission) {
			MissionDef* pDefCopy = NULL;
			pDefCopy = StructClone(parse_MissionDef, pDef);
			if(pDefCopy) {
				pDefCopy->repeatable = true;
				eaPush(&changedDefCopies, pDefCopy);
				iNumFixed++;
			}
		}
	}
	printf("Total Fixed: %d\n", iNumFixed);

	// Write out all affected files
	n = eaSize(&changedDefCopies);
	for (i = 0; i < n; i++)
	{
		MissionDef *pMissionDef = changedDefCopies[i];
		if (pMissionDef)
		{
			if (ParserWriteTextFileFromSingleDictionaryStruct(pMissionDef->filename, g_MissionDictionary, pMissionDef, 0, 0))
				iNumSaved++;
		}
	}
	eaDestroyStruct(&changedDefCopies, parse_MissionDef);
	printf("Total Saved: %d\n", iNumSaved);
}

/// Update all layers in Star_Cluster to not use "MissionNotInProgress" for prompts
static bool ExpressionRemoveMissionNotInProgress( FILE* log, Expression* expr )
{
	if(expr) {
		char* exprText = exprGetCompleteString(expr);
		char* afterFirstQ;
		char* afterSecondQ;
		char* afterThirdQ;
		char* afterPrefix;

		fprintf( log, "  Expr: %s\n", exprText );	
		if( !strStartsWith( exprText, "(not (MissionStateInProgress(\"" )) {
			fprintf( log, "    unchanged\n" );
			return false;
		}

		afterFirstQ = strchr( exprText + strlen("(not (MissionStateInProgress(\""), '\"' );
		if( !afterFirstQ || !strStartsWith( afterFirstQ, "\") or MissionStateSucceeded(\"" )) {
			fprintf( log, "    unchanged\n" );
			return false;
		}

		afterSecondQ = strchr( afterFirstQ + strlen( "\") or MissionStateSucceeded(\"" ), '\"' );
		if( !afterSecondQ || !strStartsWith( afterSecondQ, "\") or HasCompletedMission(\"" )) {
			fprintf( log, "    unchanged\n" );
			return false;
		}

		afterThirdQ = strchr( afterSecondQ + strlen( "\") or HasCompletedMission(\"" ), '\"' );
		if( !afterThirdQ || !strStartsWith( afterThirdQ, "\"))) and " )) {
			fprintf( log, "    unchanged\n" );
			return false;
		}

		afterPrefix = afterThirdQ + strlen( "\"))) and " );
		fprintf( log, "    => %s\n", afterPrefix );
		{
			char* newStr = strdup(afterPrefix);
			exprSetOrigStrNoFilename(expr, newStr);
			free( newStr );
		}
		return true;
	}
		
	return false;
}

int UpdateLayerRemoveMissionNotInProgress( FILE* log, LibFileLoad* layer, char* layerFName )
{
	int numChangedAccum = 0;
	
	FOR_EACH_IN_EARRAY( layer->defs, GroupDef, defLoad ) {
		if( defLoad->property_structs.server_volume.obsolete_optionalaction_properties ) {
			FOR_EACH_IN_EARRAY( defLoad->property_structs.server_volume.obsolete_optionalaction_properties->entries, WorldOptionalActionVolumeEntry, entry ) {
				if( ExpressionRemoveMissionNotInProgress( log, entry->visible_cond )) {
					++numChangedAccum;
				}
			} FOR_EACH_END;
		}
	} FOR_EACH_END;

	return numChangedAccum;
}

AUTO_COMMAND;
void FixStarClusterMaps_RemoveMissionNotInProgress( void )
{
	FILE* log = fopen( "c:\\RemoveMissionNotInProgress.txt", "w" );
	FILE* layers = fopen( "c:\\RemoveMissionNotInProgressLayers.txt", "w");
	char** layerFiles = NULL;
	fileScanAllDataDirs( "ns/", ScanLayerFiles, &layerFiles );

	sharedMemoryEnableEditorMode();

	{
		time_t rawtime;
		char buffer[ 256 ];

		time( &rawtime );
		ctime_s( SAFESTR(buffer), &rawtime );
		fprintf( log, "-*- truncate-lines: t -*-\nMISSION NOT IN PROGRESS CONVERSION -- %s\n", buffer );
	}

	FOR_EACH_IN_EARRAY( layerFiles, char, filename ) {
		LibFileLoad libFile = { 0 };

		if( !ParserReadTextFile( filename, parse_LibFileLoad, &libFile, 0 )) {
			fprintf( log, "Layer: %s -- Unable to read\n", filename );
			continue;
		}

		fprintf( log, "Layer: %s -- Starting conversion...", filename );
		{
			long beforeNLPos;
			int numChanged;

			beforeNLPos = ftell( log );
			fputc( '\n', log );
			numChanged = UpdateLayerRemoveMissionNotInProgress( log, &libFile, filename );
			
			if( numChanged == 0 ) {
				fseek( log, beforeNLPos, SEEK_SET );
				fprintf( log, "done, no changes.\n" );
			} else {
				if( !simulateUpdate ) {
					ParserWriteTextFile( filename, parse_LibFileLoad, &libFile, 0, 0 );
				}
			
				fprintf( log, "done, %d exprs changed.\n", numChanged );
				fprintf( layers, "%s\n", filename );
			}
		}

		StructDeInit( parse_LibFileLoad, &libFile );
	} FOR_EACH_END;

	fprintf( log, "DONE\n" );

	eaDestroyEx( &layerFiles, NULL );
	fclose( layers );
	fclose( log );
}

// This function fixes up the dropsystem table so that tiers whose DropRateQuality
// entries' weights sum to less than 1 will have an additional dummy quality entry
// (using the 0-value "Base" quality) that fills out the sum to 1; this is necessitated
// by cmiller's change to the dropsystem that forced all weights to be treated strictly
// as weights, whereas before his change, the weight values were treated as percentage
// chances if they did not total to a value greater than 1.
AUTO_COMMAND;
void FixItemDropSystemRate(void)
{
	RefDictIterator iterator;
	DropRateTable *pTable = NULL;
	const char *pchFilename = NULL;
	int i, j;

	RefSystem_InitRefDictIterator(g_hDropRateTableDict, &iterator);
	while (pTable = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		for (i = 0; i < eaSize(&pTable->tiers); i++)
		{
			DropRateQuality *pNewQuality;
			bool bFoundBase = false;
			F32 fWeightSum = 0.0f;

			// look through qualities
			for (j = 0; j < eaSize(&pTable->tiers[i]->qualities) && !bFoundBase; j++)
			{
				DropRateQuality *pQuality = pTable->tiers[i]->qualities[j];
				if (pQuality->quality == ItemGenRarity_Base)
				{
					bFoundBase = true;
					break;
				}
				fWeightSum += pQuality->rate;
			}

			// no need to process this tier if there's already a base entry or
			// if the sum of the weights equals or exceeds 1 (by some tiny margin of
			// error)
			if (bFoundBase || fWeightSum >= (0.999999f))
				continue;

			pNewQuality = StructCreate(parse_DropRateQuality);
			pNewQuality->quality = ItemGenRarity_Base;
			pNewQuality->rate = 1.0f - fWeightSum;
			eaInsert(&pTable->tiers[i]->qualities, pNewQuality, 0);
		}
		
		if (!pchFilename)
			pchFilename = pTable->file;
	}

	ParserWriteTextFileFromDictionary(pchFilename, g_hDropRateTableDict, 0, 0);
}


// Fixup all open missions that have contact actions on success to instead put an auto-execute action on
// the map's global volume that checks for the open mission's success state
typedef struct OnSuccessContactActionInstance
{
	const char *pchMissionRefString;
	WorldGameActionProperties *pGameAction;
} OnSuccessContactActionInstance;

typedef struct OnSuccessContactMissionInstance
{
	MissionDef *pRootMissionCopy;
	LibFileLoad *pLayerCopy;

	OnSuccessContactActionInstance **eaOnSuccessActions;
	const char *pchAutoGrantMap;
} OnSuccessContactMissionInstance;

static OnSuccessContactActionInstance *OnSuccessContactActionInstanceCreate(MissionDef *pMissionDef, WorldGameActionProperties *pGameActionProperties)
{
	OnSuccessContactActionInstance *pInstance = calloc(1, sizeof(*pInstance));
	pInstance->pchMissionRefString = pMissionDef->pchRefString;
	pInstance->pGameAction = pGameActionProperties;
	return pInstance;
}

static void OnSuccessContactActionInstanceDestroy(OnSuccessContactActionInstance *pInstance)
{
	StructDestroy(parse_WorldGameActionProperties, pInstance->pGameAction);
	free(pInstance);
}

static OnSuccessContactMissionInstance *OnSuccessContactMissionInstanceCreate(MissionDef *pRootDef)
{
	OnSuccessContactMissionInstance *pInstance = calloc(1, sizeof(*pInstance));
	pInstance->pRootMissionCopy = StructClone(parse_MissionDef, pRootDef);
	pInstance->pchAutoGrantMap = pRootDef->autoGrantOnMap;
	return pInstance;
}

static void OnSuccessContactMissionInstanceDestroy(OnSuccessContactMissionInstance *pInstance)
{
	StructDestroy(parse_MissionDef, pInstance->pRootMissionCopy);
	StructDestroy(parse_LibFileLoad, pInstance->pLayerCopy);
	eaDestroyEx(&pInstance->eaOnSuccessActions, OnSuccessContactActionInstanceDestroy);
	free(pInstance);
}

static OnSuccessContactMissionInstance *OpenMissionOnSuccessContactsGetInstance(MissionDef *pMissionDef)
{
	OnSuccessContactMissionInstance *pReturnInstance = NULL;
	int i, j;

	if (!pMissionDef)
		return NULL;
	if (missiondef_GetType(pMissionDef) != MissionType_OpenMission)
		return NULL;

	// check root-level success actions
	for (i = eaSize(&pMissionDef->ppSuccessActions) - 1; i >= 0; i--)
	{
		if (pMissionDef->ppSuccessActions[i]->eActionType == WorldGameActionType_Contact)
		{
			WorldGameActionProperties *pAction;

			if (!pReturnInstance)
			{
				pReturnInstance = OnSuccessContactMissionInstanceCreate(pMissionDef);
				pMissionDef = pReturnInstance->pRootMissionCopy;
			}
			pAction = eaRemove(&pMissionDef->ppSuccessActions, i);
			eaPush(&pReturnInstance->eaOnSuccessActions, OnSuccessContactActionInstanceCreate(pMissionDef, pAction));
		}
	}

	// check all submission success actions
	for (i = 0; i < eaSize(&pMissionDef->subMissions); i++)
	{
		for (j = eaSize(&pMissionDef->subMissions[i]->ppSuccessActions) - 1; j >= 0; j--)
		{
			if (pMissionDef->subMissions[i]->ppSuccessActions[j]->eActionType == WorldGameActionType_Contact)
			{
				WorldGameActionProperties *pAction;

				if (!pReturnInstance)
				{
					pReturnInstance = OnSuccessContactMissionInstanceCreate(pMissionDef);
					pMissionDef = pReturnInstance->pRootMissionCopy;
				}
				pAction = eaRemove(&pMissionDef->subMissions[i]->ppSuccessActions, j);
				eaPush(&pReturnInstance->eaOnSuccessActions, OnSuccessContactActionInstanceCreate(pMissionDef->subMissions[i], pAction));
			}
		}
	}

	return pReturnInstance;
}

AUTO_COMMAND;
void FixupOpenMissionOnSuccessContacts(void)
{
	OnSuccessContactMissionInstance **eaMissionInstances = NULL;
	LibFileLoad **eaLayers = NULL;
	RefDictIterator iterator = {0};
	MissionDef *pDef = NULL;
	int i, j, k, l, iNumSaved = 0;

	FILE* pLogFile = fopen("C:\\FixupOpenMissionOnSuccessContacts.txt", "w");
	FILE* pMissionsFile = fopen("C:\\FixupOpenMissionOnSuccessMissions.txt", "w");

	fprintf(pLogFile, "Starting fixup for open missions with on-success contact actions...\n");
	sharedMemoryEnableEditorMode();

	// search all open missions for bad on-success actions (i.e. those that have contact actions)
	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);
	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator)) 
	{
		OnSuccessContactMissionInstance *pInstance = OpenMissionOnSuccessContactsGetInstance(pDef);
		if (pInstance)
		{
			eaPush(&eaMissionInstances, pInstance);
			fprintf(pMissionsFile, "%s\n", pDef->filename);
			fprintf(pLogFile, "  \"%s\" has %i bad actions.\n", pDef->filename, eaSize(&pInstance->eaOnSuccessActions));
		}
	}

	// for every mission being fixed, move the mission's bad actions into its auto-grant map's global
	// optional action volume
	for (i = 0; i < eaSize(&eaMissionInstances); i++)
	{
		OnSuccessContactMissionInstance *pMissionInstance = eaMissionInstances[i];
		ZoneMapInfo *pZmap = zmapInfoGetByPublicName(pMissionInstance->pchAutoGrantMap);

		fprintf(pLogFile, "  Fixing \"%s\"; scanning map \"%s\".\n", pMissionInstance->pRootMissionCopy->name, pMissionInstance->pchAutoGrantMap);

		// go through the layers and load them to find the optional action volume
		for (j = 0; j < eaSize(&pZmap->layers) && !pMissionInstance->pLayerCopy; j++)
		{
			LibFileLoad *pLibFile = StructCreate(parse_LibFileLoad);
			char pchLayerFilename[MAX_PATH];

			// convert layer filename to absolute path
			if (strchr(pZmap->layers[j]->filename, '/'))
				sprintf(pchLayerFilename, "%s", pZmap->layers[j]->filename);
			else
			{
				char *c;
				sprintf(pchLayerFilename, "%s", pZmap->filename);
				c = strrchr(pchLayerFilename, '/');
				if (c)
					*(c + 1) = 0;
				strcatf(pchLayerFilename, "%s", pZmap->layers[j]->filename);
			}

			fprintf(pLogFile, "    loading \"%s\"...", pchLayerFilename);
			if (!ParserReadTextFile(pchLayerFilename, parse_LibFileLoad, pLibFile, 0))
			{
				fprintf(pLogFile, "failed.\n");
				continue;
			}
			fprintf(pLogFile, "succeeded.\n");

			// go through every def in the layer and find one that is an optional action volume
			for (k = 0; k < eaSize(&pLibFile->defs) && !pMissionInstance->pLayerCopy; k++)
			{
				if (pLibFile->defs[k]->property_structs.server_volume.obsolete_optionalaction_properties &&
					eaSize(&pLibFile->defs[k]->property_structs.server_volume.obsolete_optionalaction_properties->entries) > 0)
				{
					// add each action instance as a new optional action
					for (l = 0; l < eaSize(&pMissionInstance->eaOnSuccessActions); l++)
					{
						OnSuccessContactActionInstance *pActionInstance = pMissionInstance->eaOnSuccessActions[l];
						WorldOptionalActionVolumeEntry *pNewOptActEntry = StructCreate(parse_WorldOptionalActionVolumeEntry);
						char pchBuf[1024];

						// create new optional action entry for the volume
						assert(pActionInstance->pGameAction->pContactProperties);
						sprintf(pchBuf, "OpenMissionStateSucceeded(\"%s\") and HasRecentlyCompletedContactDialog(\"%s\", \"%s\") = 0",
							pActionInstance->pchMissionRefString,
							REF_STRING_FROM_HANDLE(pActionInstance->pGameAction->pContactProperties->hContactDef),
							pActionInstance->pGameAction->pContactProperties->pcDialogName);
						pNewOptActEntry->visible_cond = exprCreateFromString(pchBuf, pLibFile->filename);
						pNewOptActEntry->enabled_cond = exprCreateFromString("not PlayerIsInCombat()", pLibFile->filename);

						langMakeEditorCopy(parse_DisplayMessage, &pNewOptActEntry->display_name_msg, true);
						pNewOptActEntry->display_name_msg.pEditorCopy->pcDefaultString = StructAllocString("Ops");
						pNewOptActEntry->display_name_msg.pEditorCopy->pcDescription = StructAllocString("Optional action button text");
						pNewOptActEntry->display_name_msg.pEditorCopy->pcScope = StructAllocString("Optionalaction");

						pNewOptActEntry->priority = WorldOptionalActionPriority_Low;
						pNewOptActEntry->auto_execute = true;
						eaPush(&pNewOptActEntry->actions.eaActions, pMissionInstance->eaOnSuccessActions[l]->pGameAction);
						pMissionInstance->eaOnSuccessActions[l]->pGameAction = NULL;

						// add the new entry to the volume properties
						eaPush(&pLibFile->defs[k]->property_structs.server_volume.obsolete_optionalaction_properties->entries, pNewOptActEntry);
					}

					UpdateMessageKeyOptionalActionList(NULL, pLibFile->defs[k]->property_structs.server_volume.obsolete_optionalaction_properties->entries, pLibFile->filename, pLibFile->defs[k]->name_str);

					fprintf(pLogFile, "    Found optional action volume def \"%s\" and added actions.\n", pLibFile->defs[k]->name_str);
					pMissionInstance->pLayerCopy = pLibFile;
				}
			}

			if (!pMissionInstance->pLayerCopy)
				StructDestroy(parse_LibFileLoad, pLibFile);
		}

		if (!pMissionInstance->pLayerCopy)
			fprintf(pLogFile, "    ERROR: couldn't find optional action volume for \"%s\"!\n", pMissionInstance->pRootMissionCopy->name);
	}

	// write out all data
	fprintf(pLogFile, "Saving modified data...\n");
	for (i = 0; i < eaSize(&eaMissionInstances); i++)
	{
		if (!eaMissionInstances[i]->pLayerCopy || !eaMissionInstances[i]->pRootMissionCopy)
			continue;

		// attempt checkouts
		if (!fixupCheckoutFile(eaMissionInstances[i]->pLayerCopy->filename, true))
		{
			fprintf(pLogFile, "  ERROR: couldn't check out \"%s\".\n", eaMissionInstances[i]->pLayerCopy->filename);
			continue;
		}
		if (!fixupCheckoutFile(eaMissionInstances[i]->pRootMissionCopy->filename, true))
		{
			fprintf(pLogFile, "  ERROR: couldn't check out \"%s\".\n", eaMissionInstances[i]->pRootMissionCopy->filename);
			continue;
		}
			
		if (!simulateUpdate && ParserWriteTextFileFromSingleDictionaryStruct(eaMissionInstances[i]->pRootMissionCopy->filename, g_MissionDictionary, eaMissionInstances[i]->pRootMissionCopy, 0, 0))
		{
			eaPush(&eaLayers, eaMissionInstances[i]->pLayerCopy);
			iNumSaved++;
		}
		else if (!simulateUpdate)
			fprintf(pLogFile, "  ERROR: couldn't save \"%s\".\n", eaMissionInstances[i]->pRootMissionCopy->filename);
	}
	fprintf(pLogFile, "...saved missions: %i out of %i.\n", iNumSaved, eaSize(&eaMissionInstances));
	AssetSaveGeoLayers(&eaLayers, false);
	fprintf(pLogFile, "...saved layers.\n");

	// cleanup
	eaDestroyEx(&eaMissionInstances, OnSuccessContactMissionInstanceDestroy);
	eaDestroy(&eaLayers);

	fclose(pMissionsFile);
	fclose(pLogFile);
}

#include "CSVExport.h"
#include "file.h"
#include "CostumeCommon.h"
#include "CostumeCommon_h_ast.h"
#include "CostumeCommonLoad.h"

#include "cmdDataFixup_c_ast.h"

AUTO_STRUCT;
typedef struct CostumeGroupSet
{
	char *pchName;					AST(KEY)
	PCGeometryDef **eaRawGeos;		AST(UNOWNED)
	PCTextureDef **eaRawTexs;		AST(UNOWNED)
	PCPart **eaParts;
} CostumeGroupSet;

AUTO_STRUCT;
typedef struct CostumeGroupSets
{
	CostumeGroupSet **ppSets;
} CostumeGroupSets;

static void AddThePart(PCPart ***peaParts, PCGeometryDef *pGeo, PCMaterialDef *pMat, PCTextureDef *pTex)
{
	PCBoneDef *pBone = GET_REF(pGeo->hBone);
	PCPart *pPart = NULL;

	if(!pBone)
		return;

	pPart = StructCreate(parse_PCPart);
	SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pGeo, pPart->hGeoDef);
	COPY_HANDLE(pPart->hBoneDef, pGeo->hBone);
	SET_HANDLE_FROM_REFERENT(g_hCostumeMaterialDict, pMat, pPart->hMatDef);

	if(pTex->eTypeFlags & kPCTextureType_Detail)
	{
		SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pTex, pPart->hDetailTexture);
	}
	else if(pTex->eTypeFlags & kPCTextureType_Pattern)
	{
		SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pTex, pPart->hPatternTexture);
	}
	else if(pTex->eTypeFlags & kPCTextureType_Diffuse)
	{
		SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pTex, pPart->hDiffuseTexture);
	}
	else if(pTex->eTypeFlags & kPCTextureType_Specular)
	{
		SET_HANDLE_FROM_REFERENT(g_hCostumeTextureDict, pTex, pPart->hSpecularTexture);
	}
	else
		assert(0);

	eaPush(peaParts, pPart);
}

AUTO_COMMAND;
void Costumesets_CreateAllF2P(const char *pchOutputDir)
{
	RefDictIterator iter;

	PCGeometryDef *pGeo;
	//PCMaterialDef *pMat;
	PCTextureDef *pTex;
	S32 i;

	CostumeGroupSets *pGroups = StructCreate(parse_CostumeGroupSets);
	eaIndexedEnable(&pGroups->ppSets, parse_CostumeGroupSet);

	RefSystem_InitRefDictIterator(g_hCostumeGeometryDict, &iter);

	while(pGeo = (PCGeometryDef*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		if(pGeo->eRestriction & kPCRestriction_Player_Initial)
			continue;
		for(i=eaSize(&pGeo->eaCostumeGroups)-1; i>=0; i--)
		{
			CostumeGroupSet *pSet = eaIndexedGetUsingString(&pGroups->ppSets, pGeo->eaCostumeGroups[i]);
			if(!pSet)
			{
				pSet = StructCreate(parse_CostumeGroupSet);
				pSet->pchName = StructAllocString(pGeo->eaCostumeGroups[i]);
				eaPush(&pGroups->ppSets, pSet);
			}

			eaPush(&pSet->eaRawGeos, pGeo);
		}
	}

	RefSystem_InitRefDictIterator(g_hCostumeTextureDict, &iter);

	while(pTex = (PCTextureDef*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		if(pTex->eRestriction & kPCRestriction_Player_Initial)
			continue;

		for(i=eaSize(&pTex->eaCostumeGroups)-1; i>=0; i--)
		{
			CostumeGroupSet *pSet = eaIndexedGetUsingString(&pGroups->ppSets, pTex->eaCostumeGroups[i]);
			if(!pSet)
			{
				pSet = StructCreate(parse_CostumeGroupSet);
				pSet->pchName = StructAllocString(pTex->eaCostumeGroups[i]);
				eaPush(&pGroups->ppSets, pSet);
			}

			eaPush(&pSet->eaRawTexs, pTex);
		}
	}

	for(i=eaSize(&pGroups->ppSets)-1; i>=0; i--)
	{
		int j;

		for (j=eaSize(&pGroups->ppSets[i]->eaRawGeos)-1; j>=0; j--)
		{
			PCPart *pPart = NULL;
			PCBoneDef *pBone = NULL;
			
			pGeo = pGroups->ppSets[i]->eaRawGeos[j];
			pBone = GET_REF(pGeo->hBone);

			if(!pBone)
				continue;

			pPart = StructCreate(parse_PCPart);

			SET_HANDLE_FROM_REFERENT(g_hCostumeGeometryDict, pGeo, pPart->hGeoDef);
			COPY_HANDLE(pPart->hBoneDef, pGeo->hBone);
			
			eaPush(&pGroups->ppSets[i]->eaParts, pPart);
		}

		for (j=eaSize(&pGroups->ppSets[i]->eaRawTexs)-1; j>=0; j--)
		{
			PCMaterialDef **ppMats = NULL;
			PCMaterialDef *pMat;

			pTex = pGroups->ppSets[i]->eaRawTexs[j];

			eaIndexedEnable(&ppMats, parse_PCMaterialDef);
			RefSystem_InitRefDictIterator(g_hCostumeMaterialDict, &iter);
			while(pMat = (PCMaterialDef*)RefSystem_GetNextReferentFromIterator(&iter))
			{
				bool bFound = false;
				if(!(pMat->eRestriction & kPCRestriction_Player_Initial))
					continue;
				if(!stricmp(REF_STRING_FROM_HANDLE(pMat->hDefaultPattern), pTex->pcName)
					|| !stricmp(REF_STRING_FROM_HANDLE(pMat->hDefaultDetail), pTex->pcName)
					|| !stricmp(REF_STRING_FROM_HANDLE(pMat->hDefaultDiffuse), pTex->pcName)
					|| !stricmp(REF_STRING_FROM_HANDLE(pMat->hDefaultSpecular), pTex->pcName))
				{
					eaPush(&ppMats, pMat);
					bFound = true;
				}

				if(!bFound)
				{
					int k;
					for(k=eaSize(&pMat->eaAllowedTextureDefs)-1;k>=0; k--)
					{
						if(stricmp(pMat->eaAllowedTextureDefs[k],pTex->pcName)==0)
						{
							eaPush(&ppMats, pMat);
							break;
						}
					}
				}
			}

			RefSystem_InitRefDictIterator(g_hCostumeGeometryDict, &iter);
			while(pGeo = (PCGeometryDef*)RefSystem_GetNextReferentFromIterator(&iter))
			{
				PCMaterialDef *pDefMat = NULL;
				pMat = NULL;
				//Skip non-player initial
				if(!(pGeo->eRestriction & kPCRestriction_Player_Initial))
					continue;

				pDefMat = GET_REF(pGeo->hDefaultMaterial);
				if(pDefMat)
				{
					pMat = eaIndexedGetUsingString(&ppMats, pDefMat->pcName);
					if(pMat)
					{
						AddThePart(&pGroups->ppSets[i]->eaParts, pGeo, pMat, pTex);
					}
				}
				
				{
					int m;
					for(m=eaSize(&pGeo->eaAllowedMaterialDefs)-1; m>=0; m--)
					{
						pMat = eaIndexedGetUsingString(&ppMats, pGeo->eaAllowedMaterialDefs[m]);
						if(pMat && (!pDefMat || pDefMat != pMat))
						{
							AddThePart(&pGroups->ppSets[i]->eaParts, pGeo, pMat, pTex);
						}
					}
				}

				
			}
			eaDestroy(&ppMats);
		}
	}

	{
		FileWrapper *fw = NULL;
		char pcFileName[CRYPTIC_MAX_PATH];
		NOCONST(PlayerCostume) *pCostume = NULL;

		if(stricmp(pchOutputDir, "0")==0 || stricmp(pchOutputDir,"NULL")==0)
		{
			CSV_GetDocumentsDirEx(pcFileName, CRYPTIC_MAX_PATH);
			strcat(pcFileName, "/CostumeSets");
		}
		else
		{
			sprintf(pcFileName, "%s", pchOutputDir);
			forwardSlashes(pcFileName);
		}
		

		for(i=eaSize(&pGroups->ppSets)-1; i>=0; i--)
		{
			int j;
			char pcOutput[CRYPTIC_MAX_PATH];
			PCCostumeSet *pCostumeSet = NULL;

			if(!eaSize(&pGroups->ppSets[i]->eaParts))
				continue;

			
			//Male
			sprintf(pcOutput, "%s/%s_M.costume",
				pcFileName,
				pGroups->ppSets[i]->pchName);

			if(!makeDirectoriesForFile(pcOutput))
			{
				Errorf("Couldn't make the directories for %s", pcOutput);
				return;
			}

			fw = NULL;

			for(j=0;j<eaSize(&pGroups->ppSets[i]->eaParts); j++)
			{
				PCPart *pPart = pGroups->ppSets[i]->eaParts[j];
				PCBoneDef *pBoneDef = GET_REF(pGroups->ppSets[i]->eaParts[j]->hBoneDef);
				if(pBoneDef && pBoneDef->pcName && pBoneDef->pcName[0] == 'M')
				{
					if(!fw)
						fw = fopen(pcOutput, "w");

					assert(fw!=NULL);

					if(!pCostume)
					{
						pCostume = StructCreateNoConst(parse_PlayerCostume);
						SET_HANDLE_FROM_STRING(g_hCostumeSkeletonDict, "Male", pCostume->hSkeleton);
						eafPush(&pCostume->eafBodyScales, 20.f);
						eafPush(&pCostume->eafBodyScales, 50.f);
						eafPush(&pCostume->eafBodyScales, 50.f);
						eafPush(&pCostume->eafBodyScales, 50.f);
						pCostume->eCostumeType = kPCCostumeType_Item;
						pCostume->fMuscle = 30.f;
						pCostume->fHeight = 6.f;
						pCostume->pcStance = allocAddString("Heroic");
						pCostume->skinColor[0] = 212;
						pCostume->skinColor[1] = 137;
						pCostume->skinColor[2] = 114;
						pCostume->skinColor[0] = 255;
						pCostume->pcScope = allocAddString("F2P");
					}

					eaPush(&pCostume->eaParts, CONTAINER_NOCONST(PCPart, pPart));
				}
			}

			if(pCostume)
			{
				char *estrCostumeName = NULL;
				estrPrintf(&estrCostumeName, "%s_M", pGroups->ppSets[i]->pchName);
				fprintf(fw,"PlayerCostume %s", estrCostumeName);
				InnerWriteTextFile(fw, parse_PlayerCostume, pCostume, 0, 0, 0, 0, 0);

				if(!pCostumeSet)
				{
					char *estrExpression = NULL;
					estrPrintf(&estrExpression, "PermTokenTypePlayer(\"CostumeSet\",\"%s\")",
						pGroups->ppSets[i]->pchName);
					pCostumeSet = StructCreate(parse_PCCostumeSet);
					pCostumeSet->pcName = allocAddString(pGroups->ppSets[i]->pchName);
					pCostumeSet->eCostumeType = kPCCostumeType_Item;
					pCostumeSet->eCostumeSetFlags |= kPCCostumeSetFlags_Unlockable;
					pCostumeSet->pExprUnlock = exprCreate();
					exprSetOrigStrNoFilename(pCostumeSet->pExprUnlock, estrExpression);

					estrDestroy(&estrExpression);
					
				}

				{
					CostumeRefForSet *pRefSet = NULL;
					pRefSet = StructCreate(parse_CostumeRefForSet);
					pRefSet->pcName = allocAddString(estrCostumeName);
					SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, estrCostumeName, pRefSet->hPlayerCostume);
					eaPush(&pCostumeSet->eaPlayerCostumes, pRefSet);
				}

				eaClear(&pCostume->eaParts);
				StructDestroyNoConstSafe(parse_PlayerCostume, &pCostume);
				estrDestroy(&estrCostumeName);
			}


			if(fw)
			{
				fflush(fw);
				fclose(fw);
			}
			
			//Female
			sprintf(pcOutput, "%s/%s_F.costume",
				pcFileName,
				pGroups->ppSets[i]->pchName);

			fw = NULL;
			
			for(j=0;j<eaSize(&pGroups->ppSets[i]->eaParts); j++)
			{
				PCPart *pPart = pGroups->ppSets[i]->eaParts[j];
				PCBoneDef *pBoneDef = GET_REF(pGroups->ppSets[i]->eaParts[j]->hBoneDef);
				if(pBoneDef && pBoneDef->pcName && pBoneDef->pcName[0] == 'F')
				{
					if(!fw)
						fw = fopen(pcOutput, "w");

					assert(fw!=NULL);
					if(!pCostume)
					{
						pCostume = StructCreateNoConst(parse_PlayerCostume);
						SET_HANDLE_FROM_STRING(g_hCostumeSkeletonDict, "Female", pCostume->hSkeleton);
						eafPush(&pCostume->eafBodyScales, 29.f);
						eafPush(&pCostume->eafBodyScales, 50.f);
						pCostume->eCostumeType = kPCCostumeType_Item;
						pCostume->fMuscle = 11.f;
						pCostume->fHeight = 6.f;
						pCostume->pcStance = allocAddString("Femaleheroic");
						pCostume->skinColor[0] = 212;
						pCostume->skinColor[1] = 137;
						pCostume->skinColor[2] = 114;
						pCostume->skinColor[0] = 255;
						pCostume->pcScope = allocAddString("F2P");
					}

					eaPush(&pCostume->eaParts, CONTAINER_NOCONST(PCPart, pPart));
				}
			}

			if(pCostume)
			{
				char *estrCostumeName = NULL;
				estrPrintf(&estrCostumeName, "%s_F", pGroups->ppSets[i]->pchName);
				fprintf(fw,"PlayerCostume %s", estrCostumeName);

				InnerWriteTextFile(fw, parse_PlayerCostume, pCostume, 0, 0, 0, 0, 0);
				if(!pCostumeSet)
				{
					char *estrExpression = NULL;
					estrPrintf(&estrExpression, "PermTokenTypePlayer(\"CostumeSet\",\"%s\")",
						pGroups->ppSets[i]->pchName);
					pCostumeSet = StructCreate(parse_PCCostumeSet);
					pCostumeSet->pcName = allocAddString(pGroups->ppSets[i]->pchName);
					pCostumeSet->eCostumeType = kPCCostumeType_Item;
					pCostumeSet->eCostumeSetFlags |= kPCCostumeSetFlags_Unlockable;
					pCostumeSet->pExprUnlock = exprCreate();
					exprSetOrigStrNoFilename(pCostumeSet->pExprUnlock, estrExpression);

					estrDestroy(&estrExpression);
				}

				{
					CostumeRefForSet *pRefSet = NULL;
					pRefSet = StructCreate(parse_CostumeRefForSet);
					pRefSet->pcName = allocAddString(estrCostumeName);
					SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, estrCostumeName, pRefSet->hPlayerCostume);
					eaPush(&pCostumeSet->eaPlayerCostumes, pRefSet);
				}
				eaClear(&pCostume->eaParts);
				StructDestroyNoConstSafe(parse_PlayerCostume, &pCostume);
				estrDestroy(&estrCostumeName);
			}

			if(fw)
			{
				fflush(fw);
				fclose(fw);
			}

			if(pCostumeSet)
			{
				sprintf(pcOutput, "%s/%s.costumeset",
					pcFileName,
					pGroups->ppSets[i]->pchName);

				fw = fileOpen(pcOutput, "w");
				assert(fw!=NULL);
				fprintf(fw,"CostumeSet");
				InnerWriteTextFile(fw, parse_PCCostumeSet, pCostumeSet, 0, 0, 0, 0, 0);
				StructDestroySafe(parse_PCCostumeSet, &pCostumeSet);

				if(fw)
				{
					fflush(fw);
					fclose(fw);
				}
			}
		}
	}

	StructDestroy(parse_CostumeGroupSets, pGroups);
}

// Fixup all namespaced genesis layers to enable all harvest nodes
AUTO_COMMAND;
void FixupHarvestNodes(void)
{
	char **eaLayerFilenames = NULL;
	LibFileLoad **eaLayerCopies = NULL;
	const char *pchHarvestCategory = allocAddString("Harvest");

	FILE *pLogFile = fopen("C:\\FixupHarvestNodes.txt", "w");
	FILE *pLayersFile = fopen("C:\\FixupHarvestNodesLayers.txt", "w");

	fprintf(pLogFile, "Starting fixup to enable disabled namespaced genesis harvest nodes...\n");
	sharedMemoryEnableEditorMode();

	// compile list of namespaced layers
	fileScanAllDataDirs("ns/", ScanLayerFiles, &eaLayerFilenames);

	// begin opening layers
	FOR_EACH_IN_EARRAY(eaLayerFilenames, char, pchFilename)
	{
		LibFileLoad *pLibFile = StructCreate(parse_LibFileLoad);
		bool bCopied = false;
		int iNumFixedNodes = 0;

		if (!ParserReadTextFile(pchFilename, parse_LibFileLoad, pLibFile, 0 ))
		{
			fprintf(pLogFile, "  ERROR: Unable to read layer \"%s\"\n", pchFilename);
			continue;
		}

		// iterate over layer groupdefs
		FOR_EACH_IN_EARRAY(pLibFile->defs, GroupDef, pDef)
		{
			// ignore non-interactables
			if (!pDef->property_structs.interaction_properties || eaSize(&pDef->property_structs.interaction_properties->eaEntries) == 0)
				continue;

			FOR_EACH_IN_EARRAY(pDef->property_structs.interaction_properties->eaEntries, WorldInteractionPropertyEntry, pEntry)
			{
				char *pchExprString = NULL;

				// find harvest node interaction entries that have the "0" visible expression
				if (pEntry->pcCategoryName != pchHarvestCategory)
					continue;
				if (!pEntry->pVisibleExpr)
					continue;

				pchExprString = exprGetCompleteString(pEntry->pVisibleExpr);
				if (stricmp(pchExprString, "0") != 0)
					continue;

				// found a harvest node that needs fixing
				// add file to tracked list of layers, if it hasn't been added yet
				if (!bCopied)
				{
					fprintf(pLogFile, "  fixing layer \"%s\"...", pLibFile->filename);
					fprintf(pLayersFile, "%s\n", pLibFile->filename);
					eaPush(&eaLayerCopies, pLibFile);
					bCopied = true;
				}

				// fix the expression
				exprDestroy(pEntry->pVisibleExpr);
				pEntry->pVisibleExpr = NULL;
				iNumFixedNodes++;
			} FOR_EACH_END;
		} FOR_EACH_END;

		if (!bCopied)
			StructDestroy(parse_LibFileLoad, pLibFile);
		else
			fprintf(pLogFile, "done; fixed %i harvest nodes.\n", iNumFixedNodes);
	} FOR_EACH_END;

	// write out all the modified layers
	fprintf(pLogFile, "Checking out modified layers...\n");
	FOR_EACH_IN_EARRAY(eaLayerCopies, LibFileLoad, pLayerCopy)
	{
		// attempt checkout
		if (!fixupCheckoutFile(pLayerCopy->filename, true))
		{
			fprintf(pLogFile, "  ERROR: couldn't check out \"%s\".\n", pLayerCopy->filename);
			continue;
		}
	} FOR_EACH_END;
	fprintf(pLogFile, "Saving modified layers...\n");
	if (!simulateUpdate)
		AssetSaveGeoLayers(&eaLayerCopies, false);
	fprintf(pLogFile, "Saved %i layers.\n", eaSize(&eaLayerCopies));

	// cleanup
	eaDestroyEx(&eaLayerFilenames, NULL);
	eaDestroyStruct(&eaLayerCopies, parse_LibFileLoad);
	fclose(pLayersFile);
	fclose(pLogFile);
}

#include "InteriorCommon.h"
#include "InteriorCommon_h_ast.h"
#include "gslSpawnPoint.h"


AUTO_STRUCT;
typedef struct MapInteriorUpdates
{
	InteriorDef **eaInteriors;		  AST(UNOWNED)
	InteriorOptionDef **eaOptions;	  AST(UNOWNED)
	InteriorOptionChoice **eaChoices; AST(UNOWNED)
} MapInteriorUpdates;


// Makes the interior definition files from the maps directory
AUTO_COMMAND;
void CreateInteriorFiles(void)
{
	char **eaLayerFilenames = NULL;
	LibFileLoad **eaLayerCopies = NULL;
	MapInteriorUpdates *pList = StructCreate(parse_MapInteriorUpdates);

	FILE *pLogFile = fopen("C:\\GenerateInteriorDefs.txt", "w");

	fprintf(pLogFile, "Starting to generate the interior data def files..\n");
	sharedMemoryEnableEditorMode();

	// compile list of namespaced layers
	fileScanAllDataDirs("maps/Hideouts/", ScanLayerFiles, &eaLayerFilenames);

	// begin opening layers
	FOR_EACH_IN_EARRAY(eaLayerFilenames, char, pchFilename)
	{
		LibFileLoad *pLibFile = StructCreate(parse_LibFileLoad);
		ZoneMapInfo *pInfo = NULL;
		InteriorDef *pInterior = NULL;

		if (!ParserReadTextFile(pchFilename, parse_LibFileLoad, pLibFile, 0 ))
		{
			fprintf(pLogFile, "  ERROR: Unable to read layer \"%s\"\n", pchFilename);
			continue;
		}

		fprintf(pLogFile, "Processing File %s\n", pLibFile->filename);

		pInfo = Convert_GetZoneForLayer(pLibFile);
		if(!pInfo)
		{
			fprintf(pLogFile, "Skipping %s\n", pLibFile->filename);
		}
		else
		{
			pInterior = RefSystem_ReferentFromString(g_hInteriorDefDict, pInfo->map_name);

			if(!pInterior)
			{
				
				pInterior = StructCreate(parse_InteriorDef);
				pInterior->name = allocAddString(pInfo->map_name);
				pInterior->filename = allocAddFilename("defs\\config\\Interiors.def");
				pInterior->spawnPointName = allocAddString(START_SPAWN);
				pInterior->mapName = allocAddString(pInfo->map_name);
				pInterior->bUnlockable = true;
				RefSystem_AddReferent(g_hInteriorDefDict, pInterior->name, pInterior);

				fprintf(pLogFile,"Adding new interior %s\n", pInterior->name);
			}
			else if(!eaIndexedGetUsingString(&pList->eaInteriors, pInfo->map_name))
			{
				InteriorDef *pCopy = StructClone(parse_InteriorDef, pInterior);
				ANALYSIS_ASSUME(pCopy != NULL);
				RefSystem_RemoveReferent(pInterior,false);
				pInterior = pCopy;
				fprintf(pLogFile,"Clearing existing interior %s\n", pInterior->name);
				eaClearStruct(&pInterior->optionRefs, parse_InteriorOptionDefRef);
				RefSystem_AddReferent(g_hInteriorDefDict, pInterior->name, pInterior);
			}

			{
				Message *pMessage = NULL;
				char buf[MAX_PATH];
				sprintf(buf, "InteriorDef.%s.Name", pInfo->map_name);

				pMessage = RefSystem_ReferentFromString(gMessageDict, buf);
				if(!pMessage)
				{
					pMessage = StructCreate(parse_Message);
					pMessage->pcMessageKey = allocAddString(buf);
					pMessage->pcDefaultString = StructAllocString(pInfo->map_name);
					pMessage->pcFilename = allocAddFilename("defs\\config\\Interiors.def.ms");
					RefSystem_AddReferent(gMessageDict, pMessage->pcMessageKey, pMessage);
				}
				
				SET_HANDLE_FROM_STRING(gMessageDict, buf, pInterior->displayNameMsg.hMessage);
			}

			eaIndexedEnable(&pList->eaInteriors, parse_InteriorDef);
			eaPush(&pList->eaInteriors, pInterior);
		}


		// iterate over layer groupdefs
		FOR_EACH_IN_EARRAY(pLibFile->defs, GroupDef, pDef)
		{
			InteriorOptionDef *pOption = NULL;
			char *estrOptionName = NULL;
			// ignore non-interactables
			if (   !pDef->property_structs.interaction_properties 
				|| eaSize(&pDef->property_structs.interaction_properties->eaEntries) == 0
				|| !pDef->property_structs.interaction_properties->pChildProperties )
				continue;

			{
				int i;
				int *eaiIndices = NULL;
				Expression *pExpr = exprClone(pDef->property_structs.interaction_properties->pChildProperties->pChildSelectExpr);
				if (!exprGenerate(pExpr, g_pInteractionContext))
				{
						continue;
				}
				
				exprFindFunctions(pExpr, "InteriorMapGetOptionChoiceValue", &eaiIndices);

				for(i=0; i<eaiSize(&eaiIndices); i++)
				{
					const MultiVal *pNameVal = exprFindFuncParam(pExpr, eaiIndices[i], 0);
					estrPrintf(&estrOptionName, "%s", MultiValGetString(pNameVal, NULL));
				}
				eaiDestroy(&eaiIndices);
				exprDestroy(pExpr);
			}
			if(estrLength(&estrOptionName) <= 0)
			{
				fprintf(pLogFile, "Couldn't output option for \"%s\" map \"%s\" option\n", pInterior->name, pDef->name_str);
				continue;
			}

			//estrPrintf(&estrOptionName, "%s-%s", pInterior->name, pDef->name_str);
			//estrReplaceOccurrences(&estrOptionName, " ", "_");

			pOption = RefSystem_ReferentFromString(g_hInteriorOptionDefDict, estrOptionName);
			if(!pOption)
			{
				pOption = StructCreate(parse_InteriorOptionDef);
				pOption->name = allocAddString(estrOptionName);
				pOption->filename = allocAddFilename("defs\\config\\InteriorOptions.def");
				RefSystem_AddReferent(g_hInteriorOptionDefDict, pOption->name, pOption);

				fprintf(pLogFile,"Creating new interior option %s\n", pOption->name);
			}
			else
			{
				InteriorOptionDef *pCopy = StructClone(parse_InteriorOptionDef, pOption);
				RefSystem_RemoveReferent(pOption, false);
				pOption = pCopy;
				REMOVE_HANDLE(pOption->hDefaultChoice);
				eaClearStruct(&pOption->choiceRefs, parse_InteriorOptionChoiceRef);
				fprintf(pLogFile,"Clearing existing interior option %s\n", pOption->name);
				RefSystem_AddReferent(g_hInteriorOptionDefDict, pOption->name, pOption);
			}

			//if(!IS_HANDLE_ACTIVE(pOption->hDisplayName))
			{
				char buf[MAX_PATH];
				Message *pMessage = NULL;
				sprintf(buf, "InteriorOptionDef.%s.Name", estrOptionName);
				pMessage = RefSystem_ReferentFromString(gMessageDict, buf);
				if(!pMessage)
				{
					pMessage = StructCreate(parse_Message);
					pMessage->pcMessageKey = allocAddString(buf);
					pMessage->pcDefaultString = StructAllocString(estrOptionName);
					pMessage->pcFilename = allocAddFilename("defs\\config\\InteriorOptions.def.ms");
					RefSystem_AddReferent(gMessageDict, pMessage->pcMessageKey, pMessage);
				}

				SET_HANDLE_FROM_STRING(gMessageDict, buf, pOption->hDisplayName);
			}

			{
				InteriorOptionDefRef *pDefRef = StructCreate(parse_InteriorOptionDefRef);
				SET_HANDLE_FROM_REFERENT(g_hInteriorOptionDefDict,pOption,pDefRef->hOptionDef);
				eaPush(&pInterior->optionRefs, pDefRef);
				eaIndexedEnable(&pList->eaOptions, parse_InteriorOptionDef);
				eaPush(&pList->eaOptions, pOption);
			}

			FOR_EACH_IN_EARRAY_FORWARDS(pDef->children, GroupChild, pChild)
			{
				char *estrChoiceName = NULL;
				InteriorOptionChoice *pChoice = NULL;
				const char *pchChildName = pChild->debug_name ? pChild->debug_name : pChild->name;
				if(!pchChildName)
				{
					fprintf(pLogFile, "Option [%s] child index %d didn't have a debug name or a deprecated name",
						pOption->name,
						FOR_EACH_IDX(pDef->children, pChild));
					continue;
				}

				estrPrintf(&estrChoiceName, "%s-%s", pOption->name, pchChildName);
				estrReplaceOccurrences(&estrChoiceName, " ", "_");
				
				pChoice = RefSystem_ReferentFromString(g_hInteriorOptionChoiceDict,estrChoiceName);
				if(!pChoice)
				{
					pChoice = StructCreate(parse_InteriorOptionChoice);
					pChoice->name = allocAddString(estrChoiceName);
					pChoice->filename = allocAddFilename("defs\\config\\InteriorOptionChoices.def");
					pChoice->value = ipChildIndex;
					RefSystem_AddReferent(g_hInteriorOptionChoiceDict, pChoice->name, pChoice);

					fprintf(pLogFile,"Creating new interior option choice %s to %d\n", pChoice->name, pChoice->value);
				}
				else
				{
					InteriorOptionChoice *pCopy = StructClone(parse_InteriorOptionChoice, pChoice);
					RefSystem_RemoveReferent(pChoice, false);
					pChoice = pCopy;
					pChoice->value = ipChildIndex;
					fprintf(pLogFile,"Updating interior option choice %s to value %d\n", pChoice->name, pChoice->value);
					RefSystem_AddReferent(g_hInteriorOptionChoiceDict, pChoice->name, pChoice);
				}

				//if(!IS_HANDLE_ACTIVE(pChoice->hDisplayName))
				{
					char buf[MAX_PATH];
					Message *pMessage = NULL;
					sprintf(buf, "InteriorOptionChoice.%s.Name", estrChoiceName);

					pMessage = RefSystem_ReferentFromString(gMessageDict, buf);
					if(!pMessage)
					{
						pMessage = StructCreate(parse_Message);
						pMessage->pcMessageKey = allocAddString(buf);
						pMessage->pcDefaultString = StructAllocString(estrChoiceName);
						pMessage->pcFilename = allocAddFilename("defs\\config\\InteriorOptionChoices.def.ms");
						RefSystem_AddReferent(gMessageDict, pMessage->pcMessageKey, pMessage);
					}

					SET_HANDLE_FROM_STRING(gMessageDict, buf, pChoice->hDisplayName);
				}
				
				if(!IS_HANDLE_ACTIVE(pOption->hDefaultChoice))
				{
					SET_HANDLE_FROM_REFERENT(g_hInteriorOptionChoiceDict, pChoice, pOption->hDefaultChoice);
				}

				{
					InteriorOptionChoiceRef *pChoiceRef = StructCreate(parse_InteriorOptionChoiceRef);
					SET_HANDLE_FROM_REFERENT(g_hInteriorOptionChoiceDict, pChoice,pChoiceRef->hChoice);
					eaPush(&pOption->choiceRefs, pChoiceRef);
					eaIndexedEnable(&pList->eaChoices, parse_InteriorOptionChoice);
					eaPush(&pList->eaChoices, pChoice);
				}

				estrDestroy(&estrChoiceName);
				
			} FOR_EACH_END;
			estrDestroy(&estrOptionName);
			
		} FOR_EACH_END;

		StructDestroy(parse_LibFileLoad, pLibFile);

	} FOR_EACH_END;


	fprintf(pLogFile, "Checking data files...\n");

	if(!fixupCheckoutFile("defs\\config\\Interiors.def", true))
	{
		fprintf(pLogFile, "  ERROR: couldn't check out \"%s\".\n","defs\\config\\Interiors.def");
		return;
	}
	if(!fixupCheckoutFile("defs\\config\\InteriorOptions.def", true))
	{
		fprintf(pLogFile, "  ERROR: couldn't check out \"%s\".\n","defs\\config\\InteriorOptions.def");
		return;
	}
	if(!fixupCheckoutFile("defs\\config\\InteriorOptionChoices.def", true))
	{
		fprintf(pLogFile, "  ERROR: couldn't check out \"%s\".\n","defs\\config\\InteriorOptionChoices.def");
		return;
	}
	// write out all the interiors
	fprintf(pLogFile, "Saving data files...\n");
	if (!simulateUpdate)
	{
		fprintf(pLogFile, "Writing data file %s...\n", "defs\\config\\Interiors.def");
		ParserWriteTextFileFromDictionary(allocAddFilename("defs\\config\\Interiors.def"), g_hInteriorDefDict, 0, 0);
		//FOR_EACH_IN_EARRAY(pList->eaInteriors, InteriorDef, pInterior)
		//{
		//	langApplyEditorCopySingleFile(parse_InteriorDef, pInterior, false, true);
		//} FOR_EACH_END;
		ParserWriteTextFileFromDictionary(allocAddFilename("defs\\config\\Interiors.def.ms"), gMessageDict, 0, 0);
		
		fprintf(pLogFile, "Writing data file %s...\n", "defs\\config\\InteriorOptions.def");
		ParserWriteTextFileFromDictionary(allocAddFilename("defs\\config\\InteriorOptions.def"), g_hInteriorOptionDefDict, 0, 0);
		if(eaSize(&pList->eaOptions))
		{
			//langApplyEditorCopySingleFile(parse_InteriorOptionDef, pList->eaOptions[0], false, false);
			ParserWriteTextFileFromDictionary(allocAddFilename("defs\\config\\InteriorOptions.def.ms"), gMessageDict, 0, 0);
		}

		if(eaSize(&pList->eaChoices))
		{
			FOR_EACH_IN_EARRAY(pList->eaChoices, InteriorOptionChoice, pChoice)
			{
				char **eaEstrPermissions = NULL;
				FOR_EACH_IN_REFDICT(g_hInteriorSettingDict, InteriorSetting, pSetting)
				{
					FOR_EACH_IN_EARRAY(pSetting->eaOptionSettings, InteriorOptionRef, pRef)
					{
						if(GET_REF(pRef->hChoice) && 
							!stricmp(REF_STRING_FROM_HANDLE(pRef->hChoice),pChoice->name))
						{
							eaPush(&eaEstrPermissions, estrCreateFromStr(pSetting->pchPermission));
						}
					} FOR_EACH_END;
				} FOR_EACH_END;

				if(eaSize(&eaEstrPermissions))
				{
					char *estrPermission = NULL;
					int i;
					estrStackCreate(&estrPermission);
					for(i=eaSize(&eaEstrPermissions)-1;i>=0; i--)
					{
						if(estrLength(&estrPermission))
						{
							estrAppend2(&estrPermission, " OR ");
						}
						estrConcatf(&estrPermission, "PermTokenPlayer(\"%s\")",
							eaEstrPermissions[i]);
					}
					
					exprDestroy(pChoice->availableExpression);
					pChoice->availableExpression = exprCreateFromString(estrPermission, pChoice->filename);
					
					estrDestroy(&estrPermission);
				}
				eaDestroyEString(&eaEstrPermissions);
			} FOR_EACH_END;
			//langApplyEditorCopySingleFile(parse_InteriorOptionChoice, pList->eaChoices[0], false, false);
			ParserWriteTextFileFromDictionary(allocAddFilename("defs\\config\\InteriorOptionChoices.def.ms"), gMessageDict, 0, 0);
		}
		fprintf(pLogFile, "Writing data file %s...\n", "defs\\config\\InteriorOptionChoices.def");
		ParserWriteTextFileFromDictionary(allocAddFilename("defs\\config\\InteriorOptionChoices.def"), g_hInteriorOptionChoiceDict, 0, 0);
	}
	fprintf(pLogFile, "Saving complete...\n");

	// cleanup
	eaDestroyEx(&eaLayerFilenames, NULL);
	eaDestroyStruct(&eaLayerCopies, parse_LibFileLoad);
	StructDestroySafe(parse_MapInteriorUpdates,&pList);
	fclose(pLogFile);
}

AUTO_COMMAND;
void FixupPetStore(bool bCreateSubDirectories)
{
	const char* pchSubDir;
	const char* pchBaseDir = "defs/pets/";
	const char* pchExt = ".pet";
	char pchFilename[MAX_PATH];
	char pchMessageFilename[MAX_PATH];
	PetDef* pPetDef;
	RefDictIterator iter;
	RefSystem_InitRefDictIterator(g_hPetStoreDict, &iter);

	while (pPetDef = (PetDef*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		Message* pDisplayMsg = GET_REF(pPetDef->displayNameMsg.hMessage);
		if (bCreateSubDirectories)
		{
			if (pPetDef->bCanBePuppet) {
				pchSubDir = "Puppets/";
			} else if (pPetDef->bCritterPet) {
				pchSubDir = "CritterPets/";
			} else {
				pchSubDir = "SavedPets/";
			}
			sprintf(pchFilename, "%s%s%s%s", pchBaseDir, pchSubDir, pPetDef->pchPetName, pchExt);
		}
		else
		{
			sprintf(pchFilename, "%s%s%s", pchBaseDir, pPetDef->pchPetName, pchExt);
		}
		// Write out the def
		ParserWriteTextFileFromSingleDictionaryStruct(pchFilename, g_hPetStoreDict, pPetDef, 0, 0);

		if (pDisplayMsg)
		{
			sprintf(pchMessageFilename, "%s.ms", pchFilename);

			// Write out the message file
			ParserWriteTextFileFromSingleDictionaryStruct(pchMessageFilename, gMessageDict, pDisplayMsg, 0, 0);
		}
	}
}

#include "MicroTransactions.h"
#include "MicroTransactions_h_ast.h"

static int microtrans_ASStrToProductCategory(const char *pchProductCatStr)
{
	if( stricmp(pchProductCatStr, "AF") == 0)
		return kProductCategory_ActionFigure;
	else if( stricmp(pchProductCatStr, "AP") == 0)
		return kProductCategory_AdventurePack;

	else if( stricmp(pchProductCatStr, "AT") == 0)
		return kProductCategory_Archetype;

	else if( stricmp(pchProductCatStr, "BRG") == 0)
		return kProductCategory_BridgePack;

	else if( stricmp(pchProductCatStr, "CP") == 0)
		return kProductCategory_CostumePack;

	else if( stricmp(pchProductCatStr, "EM") == 0)
		return kProductCategory_EmotePack;

	else if( stricmp(pchProductCatStr, "EP") == 0)
		return kProductCategory_EmblemPack;

	else if( stricmp(pchProductCatStr, "FI") == 0)
		return kProductCategory_FunctionalItem;

	else if( stricmp(pchProductCatStr, "IT") == 0)
		return kProductCategory_Item;

	else if( stricmp(pchProductCatStr, "PO") == 0)
		return kProductCategory_Power;

	else if( stricmp(pchProductCatStr, "PR") == 0)
		return kProductCategory_Promo;

	else if( stricmp(pchProductCatStr, "PS") == 0)
		return kProductCategory_PlayableSpecies;

	else if( stricmp(pchProductCatStr, "PT") == 0)
		return kProductCategory_Pet;

	else if( stricmp(pchProductCatStr, "S") == 0)
		return kProductCategory_Ship;

	else if( stricmp(pchProductCatStr, "SC") == 0)
		return kProductCategory_ShipCostume;

	else if( stricmp(pchProductCatStr, "SV") == 0)
		return kProductCategory_Service;

	else if( stricmp(pchProductCatStr, "TI") == 0)
		return kProductCategory_Title;

	else if( stricmp(pchProductCatStr, "TK") == 0)
		return kProductCategory_Token;
	else
		return -1;
}

/*
AUTO_COMMAND;
void FixupMicroTransDefs()
{
	MicroTransactionDef *pDef = NULL;
	RefDictIterator iter;
	RefSystem_InitRefDictIterator(g_hMicroTransDefDict, &iter);

	while (pDef = (MicroTransactionDef*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		MicroTransactionDef *pNewMicroTransDef = NULL;
		char *pStrCopy;
		char *pTok;
		char *context = NULL;
		S32 i = 0;
		char *estrProductID = NULL;

		if(pDef->pchProductIdentifier)
			continue;

		if(!fixupCheckoutFile(pDef->pchFile, true))
		{
			printf("Couldn't checkout %s\n", pDef->pchFile);
			break;
		}

		pNewMicroTransDef = StructClone(parse_MicroTransactionDef, pDef);

		estrStackCreate(&estrProductID);
		pStrCopy = StructAllocString(pDef->pchProductName);
		pTok = pStrCopy;

		pTok = strtok_s(pTok, "-", &context);
		while(pTok != NULL)
		{
			switch(i)
			{
				//PRD
			case 0:
				{
					//assert that this token is PRD
					if(stricmp(pTok,"PRD"))
					{
						pNewMicroTransDef->bOldProductName = true;
					}
					break;
				}

				//GAME TITLE
			case 1:
				{
					//CO/STO
					break;
				}

				//MT or Promo
			case 2:
				{
					//M or P
					if(!strcmp(pTok, "P"))
					{
						pNewMicroTransDef->bPromoProduct = true;
					}
					else if(!strcmp(pTok, "M"))
					{
						pNewMicroTransDef->bPromoProduct = false;
					}
					else
					{
						pNewMicroTransDef->bOldProductName = true;
					}
					break;
				}

				// MT Type				
			case 3:
				{
					ProductCategory eCategory = microtrans_ASStrToProductCategory(pTok);
					if(eCategory == -1)
					{
						pNewMicroTransDef->bOldProductName = true;
					}
					else
						pNewMicroTransDef->eCategory = eCategory;
					break;
				}
				//If this is buy, then it's a buy product, else it's part of the product identifier
			case 4:
				if(stricmp(pTok, "BUY")==0)
				{
					pNewMicroTransDef->bBuyProduct = true;
				}
				else
				{
					pNewMicroTransDef->bBuyProduct = false;
					estrConcatf(&estrProductID, "%s", pTok);
				}
				break;
			default:
				if(i==5 && pNewMicroTransDef->bBuyProduct)
				{
					estrConcatf(&estrProductID, "%s", pTok);
				}
				else
				{
					estrConcatf(&estrProductID, "-%s", pTok);
				}
				break;
			}

			++i;
			pTok = strtok_s(NULL, "-", &context);
		}
		
		if(strchr(estrProductID, '-'))
		{
			pNewMicroTransDef->bOldProductName = true;
		}
		else
		{
			pNewMicroTransDef->pchProductIdentifier = StructAllocString(estrProductID);
		}
		estrDestroy(&estrProductID);
		StructFreeString(pStrCopy);

		ParserWriteTextFileFromSingleDictionaryStruct(pNewMicroTransDef->pchFile, g_hMicroTransDefDict, pNewMicroTransDef, 0, 0);
		StructDestroySafe(parse_MicroTransactionDef, &pNewMicroTransDef);
	}
}
*/

// STO Fixup to prevent specific clickables from interrupting powers
AUTO_COMMAND;
void FixupSTOContactInteractables(bool bModify)
{
	char **eaLayerFilenames = NULL;
	LibFileLoad **eaLayerCopies = NULL;

	FILE *pLogFile = fopen("C:\\FixupSTOContactInteractables.txt", "w");
	FILE *pLayersFile = fopen("C:\\FixupSTOContactInteractableLayers.txt", "w");

	fprintf(pLogFile, "Starting fixup to disable interrupting powers on STO Contact interactables...\n");
	sharedMemoryEnableEditorMode();

	// compile list of map layers
	fileScanAllDataDirs("maps/", ScanLayerFiles, &eaLayerFilenames);
	fileScanAllDataDirs("ns/", ScanLayerFiles, &eaLayerFilenames);

	// begin opening layers
	FOR_EACH_IN_EARRAY(eaLayerFilenames, char, pchFilename)
	{
		LibFileLoad *pLibFile = StructCreate(parse_LibFileLoad);
		bool bCopied = false;
		int iNumFixedNodes = 0;

		if (!ParserReadTextFile(pchFilename, parse_LibFileLoad, pLibFile, 0 ))
		{
			fprintf(pLogFile, "  ERROR: Unable to read layer \"%s\"\n", pchFilename);
			continue;
		}

		// iterate over layer groupdefs
		FOR_EACH_IN_EARRAY(pLibFile->defs, GroupDef, pDef)
		{
			WorldInteractionProperties *pInteractProps = NULL;
			// ignore non-interactables
			if (pDef->property_structs.interaction_properties)
				pInteractProps = pDef->property_structs.interaction_properties;
			else if (pDef->property_structs.server_volume.interaction_volume_properties)
				pInteractProps = pDef->property_structs.server_volume.interaction_volume_properties;
			
			if (!pInteractProps)
				continue;
			FOR_EACH_IN_EARRAY(pInteractProps->eaEntries, WorldInteractionPropertyEntry, pEntry)
			{
				if (pEntry->bDisablePowersInterrupt)
					continue;
				if (pEntry->pTimeProperties && pEntry->pTimeProperties->fUseTime > 0.0f)
					continue;

				if (pEntry->pActionProperties)
				{
					S32 i;
					for (i = eaSize(&pEntry->pActionProperties->successActions.eaActions)-1; i >= 0; i--)
					{
						WorldGameActionProperties* pAction = pEntry->pActionProperties->successActions.eaActions[i];
						
						if (pAction->pSendNotificationProperties && 
							pAction->eActionType == WorldGameActionType_SendNotification)
						{
							if (stricmp(pAction->pSendNotificationProperties->pchNotifyType, "Minicontact")==0)
							{
								break;
							}
						}
						else if (pAction->pContactProperties)
						{
							break;
						}
					}
					if (i < 0)
						continue;
				}
				else if (!pEntry->pContactProperties)
				{
					continue;
				}

				// found an interactable that requires fixup
				// add file to tracked list of layers, if it hasn't been added yet
				if (!bCopied && bModify)
				{
					fprintf(pLogFile, "  fixing layer \"%s\"...", pLibFile->filename);
					fprintf(pLayersFile, "%s\n", pLibFile->filename);
					eaPush(&eaLayerCopies, pLibFile);
					bCopied = true;
				}

				// fix the entry
				if (bModify) {
					pEntry->bDisablePowersInterrupt = true;
				}
				iNumFixedNodes++;
			} FOR_EACH_END;
		} FOR_EACH_END;

		if (!bCopied) {
			StructDestroy(parse_LibFileLoad, pLibFile);
			if (!bModify) {
				fprintf(pLogFile, "done; %i nodes require fixup.\n", iNumFixedNodes);
			}
		}
		else {
			fprintf(pLogFile, "done; %i nodes fixed.\n", iNumFixedNodes);
		}
	} FOR_EACH_END;

	if (bModify && eaSize(&eaLayerCopies)) {
		const char **ppchFilenames = NULL;
		// write out all the modified layers
		fprintf(pLogFile, "Checking out modified layers...\n");
		FOR_EACH_IN_EARRAY(eaLayerCopies, LibFileLoad, pLayerCopy)
		{
			eaPush(&ppchFilenames, pLayerCopy->filename);
		} FOR_EACH_END;

		// batch checkout
		gimmeDLLDoOperations(ppchFilenames, GIMME_CHECKOUT, GIMME_QUIET|GIMME_QUIET_LARGE_CHECKOUT);
		eaDestroy(&ppchFilenames);

		fprintf(pLogFile, "Saving modified layers...\n");
		if (!simulateUpdate)
			AssetSaveGeoLayers(&eaLayerCopies, false);
		fprintf(pLogFile, "Saved %i layers.\n", eaSize(&eaLayerCopies));
	}

	// cleanup
	eaDestroyEx(&eaLayerFilenames, NULL);
	eaDestroyStruct(&eaLayerCopies, parse_LibFileLoad);
	fclose(pLayersFile);
	fclose(pLogFile);
}

// takes all cylinder EffectArea powers and adds the radius of the power to the range. Optionally add an additional range.
AUTO_COMMAND;
void FixupPowersAECylinderRadiusToRange(Entity *e, bool bModify, bool bBatchCheckout, F32 fAdditionalRange)
{
	RefDictIterator Iter = {0};
	PowerDef* pDef = NULL;
	PowerDef** eaChangedCopies = NULL;
	S32 i, iSize;
	bool bExprErrors = false;
	int iPartitionIdx = entGetPartitionIdx(e);

	RefSystem_InitRefDictIterator(g_hPowerDefDict, &Iter);

	while (pDef = (PowerDef*)RefSystem_GetNextReferentFromIterator(&Iter))
	{
		if (pDef->eEffectArea == kEffectArea_Cylinder)
		{
			F32 fRadius = 0.f;

			if (pDef->pExprRadius)
			{
				char *pchError = NULL;
				combateval_ContextReset(kCombatEvalContext_Target);
				combateval_ContextSetupTarget(NULL,NULL,NULL);
				
				fRadius = combateval_EvalNew(iPartitionIdx, pDef->pExprRadius,kCombatEvalContext_Target,&pchError);
				if (pchError)
				{
					printf("ERROR: Could not process power: %s. %s\n", pDef->pchName, pchError);
					fRadius = 0.f;
					bExprErrors = true;
				}
				estrDestroy(&pchError);
			}
			
			if (fRadius > 0.f)
			{
				PowerDef *pNewDef = StructClone(parse_PowerDef, pDef);
				
				// set the new range to add in the radius
				pNewDef->fRange += fRadius + fAdditionalRange;

				eaPush(&eaChangedCopies, pNewDef);
			}
		}
	}

	iSize = eaSize(&eaChangedCopies);
	if (bExprErrors)
	{
		printf(	"\nNot all powers could be processed correctly. "
				"Some Expression could not be evaluated without more context variables. (character, target).\n");
	}

	if (bModify)
	{
		S32 iNumSaved = 0;

		if (bBatchCheckout)
		{
			const char** ppchFileList = NULL;
			for (i = 0; i < iSize; i++)
			{
				const char* pchFilename = eaChangedCopies[i]->pchFile;
				if (fileIsReadOnly(pchFilename))
				{
					eaPush(&ppchFileList, pchFilename);
				}
			}
			gimmeDLLDoOperations(ppchFileList, GIMME_CHECKOUT, GIMME_QUIET);
			eaDestroy(&ppchFileList);
		}
		for (i = 0; i < iSize; i++)
		{
			pDef = eaChangedCopies[i];

			if (!bBatchCheckout && !fixupCheckoutFile(pDef->pchFile, true))
			{
				Errorf("Couldn't checkout file: %s\n", pDef->pchFile);
				continue;
			}

			if (ParserWriteTextFileFromSingleDictionaryStruct(pDef->pchFile, g_hPowerDefDict, pDef, 0, 0))
				iNumSaved++;
		}
		printf("Total Files: %d\n", iSize);
		printf("Files Saved: %d\n", iNumSaved);
	}
	else
	{
		printf("Total Files Requiring Fixup: %d\n", iSize);
	}

	eaDestroy(&eaChangedCopies);
}

AUTO_COMMAND;
void FixupCutscenesToDefaultUntargetable(Entity *e, bool bModify, bool bBatchCheckout)
{
	RefDictIterator Iter = {0};
	CutsceneDef *pDef = NULL;
	CutsceneDef** eaChangedCopies = NULL;
	S32 iSize; 

	RefSystem_InitRefDictIterator(g_hCutsceneDict, &Iter);

	while (pDef = (CutsceneDef*)RefSystem_GetNextReferentFromIterator(&Iter))
	{
		if (!pDef->bPlayersAreUntargetable && 
			(U32)pDef->bPlayersAreUntargetable != gConf.bCutsceneDefault_bPlayersAreUntargetable)
		{
			CutsceneDef *pNewDef = StructClone(parse_CutsceneDef, pDef);

			pNewDef->bPlayersAreUntargetable = gConf.bCutsceneDefault_bPlayersAreUntargetable;

			eaPush(&eaChangedCopies, pNewDef);
		}
	}

	iSize = eaSize(&eaChangedCopies);

	if (bModify)
	{
		S32 iNumSaved = 0;
		S32 i;

		if (bBatchCheckout)
		{
			const char** ppchFileList = NULL;
			for (i = 0; i < iSize; i++)
			{
				const char* pchFilename = eaChangedCopies[i]->filename;
				if (fileIsReadOnly(pchFilename))
				{
					eaPush(&ppchFileList, pchFilename);
				}
			}
			gimmeDLLDoOperations(ppchFileList, GIMME_CHECKOUT, GIMME_QUIET);
			eaDestroy(&ppchFileList);
		}

		for (i = 0; i < iSize; i++)
		{
			pDef = eaChangedCopies[i];

			if (!bBatchCheckout && !fixupCheckoutFile(pDef->filename, true))
			{
				Errorf("Couldn't checkout file: %s\n", pDef->filename);
				continue;
			}

			if (ParserWriteTextFileFromSingleDictionaryStruct(pDef->filename, g_hCutsceneDict, pDef, 0, 0))
				iNumSaved++;
		}
		printf("Total Files: %d\n", iSize);
		printf("Files Saved: %d\n", iNumSaved);
	}
	else
	{
		printf("Total Files Requiring Fixup: %d\n", iSize);
	}

	eaDestroy(&eaChangedCopies);
}

// Removes obsolete item powers from items (STO-specific)
AUTO_COMMAND;
void FixupPersistentWeaponPowers(bool bModify, bool bBatchCheckout)
{
	RefDictIterator Iter = {0};
	ItemDef* pDef = NULL;
	ItemDef** eaChangedCopies = NULL;
	S32 i, iSize, iNumSaved = 0;

	RefSystem_InitRefDictIterator(g_hItemDict, &Iter);

	while (pDef = (ItemDef*)RefSystem_GetNextReferentFromIterator(&Iter))
	{
		ItemDef* pNewDef = NULL;
		for (i = eaSize(&pDef->ppItemPowerDefRefs)-1; i >= 0; i--)
		{
			ItemPowerDef* pItemPowDef = GET_REF(pDef->ppItemPowerDefRefs[i]->hItemPowerDef);
			PowerDef* pPowerDef = pItemPowDef ? GET_REF(pItemPowDef->hPower) : NULL;
			PowerAnimFX* pAnimFX = pPowerDef ? GET_REF(pPowerDef->hFX) : NULL;
				
			if (0)//if (pAnimFX && pAnimFX->bPersistedWeaponStanceSTO)
			{
				if (!pNewDef)
				{
					pNewDef = StructClone(parse_ItemDef, pDef);
					eaPush(&eaChangedCopies, pNewDef);
				}
				StructDestroy(parse_ItemPowerDefRef, eaRemove(&pNewDef->ppItemPowerDefRefs, i));
			}
		}
	}

	// Write out all affected files
	iSize = eaSize(&eaChangedCopies);
	if (bModify)
	{
		if (bBatchCheckout)
		{
			const char** ppchFileList = NULL;
			for (i = 0; i < iSize; i++)
			{
				const char* pchFilename = eaChangedCopies[i]->pchFileName;
				if (fileIsReadOnly(pchFilename))
				{
					eaPush(&ppchFileList, pchFilename);
				}
			}
			gimmeDLLDoOperations(ppchFileList, GIMME_CHECKOUT, GIMME_QUIET);
			eaDestroy(&ppchFileList);
		}
		for (i = 0; i < iSize; i++)
		{
			ItemDef* pItemDef = eaChangedCopies[i];

			if (!bBatchCheckout && !fixupCheckoutFile(pItemDef->pchFileName, true))
			{
				Errorf("Couldn't checkout file: %s\n", pItemDef->pchFileName);
				continue;
			}

			if (ParserWriteTextFileFromSingleDictionaryStruct(pItemDef->pchFileName, g_hItemDict, pItemDef, 0, 0))
				iNumSaved++;
		}
		printf("Total Files: %d\n", iSize);
		printf("Files Saved: %d\n", iNumSaved);
	}
	else
	{
		printf("Total Files Requiring Fixup: %d\n", iSize);
	}
	eaDestroyStruct(&eaChangedCopies, parse_ItemDef);
}

// Removes obsolete powers from critters (STO-specific)
AUTO_COMMAND;
void FixupPersistentCritterPowers(bool bModify, bool bBatchCheckout)
{
	RefDictIterator Iter = {0};
	CritterDef* pDef = NULL;
	CritterDef** eaChangedCopies = NULL;
	S32 i, iSize, iNumSaved = 0;

	RefSystem_InitRefDictIterator(g_hCritterDefDict, &Iter);

	while (pDef = (CritterDef*)RefSystem_GetNextReferentFromIterator(&Iter))
	{
		CritterDef* pNewDef = NULL;
		for (i = eaSize(&pDef->ppPowerConfigs)-1; i >= 0; i--)
		{
			CritterPowerConfig* pPowConfig = pDef->ppPowerConfigs[i];
			PowerDef* pPowerDef = GET_REF(pPowConfig->hPower);
			PowerAnimFX* pAnimFX = pPowerDef ? GET_REF(pPowerDef->hFX) : NULL;
				
			if (0)//if (pAnimFX && pAnimFX->bPersistedWeaponStanceSTO)
			{
				if (!pNewDef)
				{
					pNewDef = StructClone(parse_CritterDef, pDef);
					eaPush(&eaChangedCopies, pNewDef);
				}
				StructDestroy(parse_CritterPowerConfig, eaRemove(&pNewDef->ppPowerConfigs, i));
			}
		}
	}

	// Write out all affected files
	iSize = eaSize(&eaChangedCopies);
	if (bModify)
	{
		if (bBatchCheckout)
		{
			const char** ppchFileList = NULL;
			for (i = 0; i < iSize; i++)
			{
				const char* pchFilename = eaChangedCopies[i]->pchFileName;
				if (fileIsReadOnly(pchFilename))
				{
					eaPush(&ppchFileList, pchFilename);
				}
			}
			gimmeDLLDoOperations(ppchFileList, GIMME_CHECKOUT, GIMME_QUIET);
			eaDestroy(&ppchFileList);
		}
		for (i = 0; i < iSize; i++)
		{
			CritterDef* pCritterDef = eaChangedCopies[i];

			if (!bBatchCheckout && !fixupCheckoutFile(pCritterDef->pchFileName, true))
			{
				Errorf("Couldn't checkout file: %s\n", pCritterDef->pchFileName);
				continue;
			}

			if (ParserWriteTextFileFromSingleDictionaryStruct(pCritterDef->pchFileName, g_hCritterDefDict, pCritterDef, 0, 0))
				iNumSaved++;
		}
		printf("Total Files: %d\n", iSize);
		printf("Files Saved: %d\n", iNumSaved);
	}
	else
	{
		printf("Total Files Requiring Fixup: %d\n", iSize);
	}
	eaDestroyStruct(&eaChangedCopies, parse_CritterDef);
}


AUTO_COMMAND;
void FixupStarClusterEncounterClamping(bool bModify)
{
	char **eaLayerFilenames = NULL;
	LibFileLoad **eaLayerCopies = NULL;
	EncounterTemplate **eaNewTemplates = NULL;
	char* estrTemplate = NULL;

	FILE *pLogFile = fopen("C:\\FixupStarClusterEncounterClamping.txt", "w");
	FILE *pLayersFile = fopen("C:\\FixupStarClusterEncounterClamping.txt", "w");

	fprintf(pLogFile, "Starting fixup to disable level clamping on StarCluster encounters...\n");
	sharedMemoryEnableEditorMode();

	// Compile list of map layers
	fileScanAllDataDirs("ns/", ScanLayerFiles, &eaLayerFilenames);

	estrStackCreate(&estrTemplate);

	// Begin opening layers
	FOR_EACH_IN_EARRAY(eaLayerFilenames, char, pchFilename)
	{
		LibFileLoad *pLibFile = StructCreate(parse_LibFileLoad);
		bool bCopied = false;
		int iNumFixedTemplates = 0;

		if (!ParserReadTextFile(pchFilename, parse_LibFileLoad, pLibFile, 0 ))
		{
			fprintf(pLogFile, "  ERROR: Unable to read layer \"%s\"\n", pchFilename);
			continue;
		}

		// Iterate over layer groupdefs
		FOR_EACH_IN_EARRAY(pLibFile->defs, GroupDef, pDef)
		{
			WorldInteractionProperties *pInteractProps = NULL;
			if (pDef->property_structs.encounter_properties)
			{
				WorldEncounterProperties* pEncProps = pDef->property_structs.encounter_properties;
				EncounterTemplate* pTemplate = GET_REF(pEncProps->hTemplate);
				if (pTemplate && 
					pTemplate->pLevelProperties && 
					pTemplate->pLevelProperties->eLevelType == EncounterLevelType_PlayerLevel &&
					pTemplate->pLevelProperties->eClampType == EncounterLevelClampType_MapVariable)
				{
					EncounterTemplate* pNewTemplate;
					estrCopy2(&estrTemplate, pTemplate->pcName);
					estrReplaceOccurrences(&estrTemplate, "_Clamp_10", "");
					estrAppend2(&estrTemplate, "_PlayerLevel");
					pNewTemplate = RefSystem_ReferentFromString("EncounterTemplate", estrTemplate);
					if (!pNewTemplate)
					{
						S32 i;
						for (i = eaSize(&eaNewTemplates)-1; i >= 0; i--)
						{
							if (stricmp(eaNewTemplates[i]->pcName, estrTemplate)==0)
								break;
						}
						if (i < 0)
						{
							pNewTemplate = StructClone(parse_EncounterTemplate, pTemplate);
							pNewTemplate->pcName = allocAddString(estrTemplate);
							pNewTemplate->pcScope = allocAddString(pTemplate->pcScope);
							resFixPooledFilename(&pNewTemplate->pcFilename, "defs/Encounters2", pNewTemplate->pcScope, pNewTemplate->pcName, "Encounter2");
							StructReset(parse_EncounterLevelProperties, pNewTemplate->pLevelProperties);
							pNewTemplate->pLevelProperties->eLevelType = EncounterLevelType_PlayerLevel;
							eaPush(&eaNewTemplates, pNewTemplate);
							iNumFixedTemplates++;
						}
					}
					// Add the file to tracked list of layers, if it hasn't been added yet
					if (!bCopied && bModify)
					{
						fprintf(pLogFile, "  fixing layer \"%s\"...", pLibFile->filename);
						fprintf(pLayersFile, "%s\n", pLibFile->filename);
						eaPush(&eaLayerCopies, pLibFile);
						bCopied = true;
					}
					SET_HANDLE_FROM_STRING("EncounterTemplate", estrTemplate, pEncProps->hTemplate);
				}
			}
		} FOR_EACH_END;

		if (!bCopied) {
			if (!bModify) {
				fprintf(pLogFile, "done; %i templates require fixup for layer '%s'.\n", 
					iNumFixedTemplates, pLibFile->filename);
			}
			StructDestroy(parse_LibFileLoad, pLibFile);
		} else {
			fprintf(pLogFile, "done; %i templates fixed.\n", iNumFixedTemplates);
		}
	} FOR_EACH_END;

	AssetPutGeoLayersInEditMode(&eaLayerCopies);

	if (bModify && eaSize(&eaNewTemplates)) {
		fprintf(pLogFile, "Saving new templates...\n");
		FOR_EACH_IN_EARRAY(eaNewTemplates, EncounterTemplate, pTemplate)
		{
			if (ParserWriteTextFileFromSingleDictionaryStruct(pTemplate->pcFilename, 
															  g_hEncounterTemplateDict,
															  pTemplate,
															  0,
															  0))
			{
				fprintf(pLogFile, "Saving template '%s'\n", pTemplate->pcName);
			}
		} FOR_EACH_END;
		fprintf(pLogFile, "Saved %i templates.\n", eaSize(&eaNewTemplates));
	}
	if (bModify && eaSize(&eaLayerCopies)) {
		const char **ppchFilenames = NULL;
		
		fprintf(pLogFile, "Checking out modified layers...\n");
		FOR_EACH_IN_EARRAY(eaLayerCopies, LibFileLoad, pLayerCopy)
		{
			eaPush(&ppchFilenames, pLayerCopy->filename);
		} FOR_EACH_END;

		// Batch checkout
		gimmeDLLDoOperations(ppchFilenames, GIMME_CHECKOUT, GIMME_QUIET|GIMME_QUIET_LARGE_CHECKOUT);
		eaDestroy(&ppchFilenames);

		// Write out all the modified layers
		fprintf(pLogFile, "Saving modified layers...\n");
		AssetSaveGeoLayers(&eaLayerCopies, false);
		fprintf(pLogFile, "Saved %i layers.\n", eaSize(&eaLayerCopies));
	}

	// cleanup
	eaDestroyEx(&eaLayerFilenames, NULL);
	eaDestroyStruct(&eaLayerCopies, parse_LibFileLoad);
	eaDestroyStruct(&eaNewTemplates, parse_EncounterTemplate);
	estrDestroy(&estrTemplate);
	fclose(pLayersFile);
	fclose(pLogFile);
}

static int FindMissionDefInRefList(MissionDef* pDef, MissionRefList* pRefList)
{
	if (pDef)
	{
		int i;
		for (i = eaSize(&pRefList->eaRefs)-1; i >= 0; i--)
		{
			if (pDef == GET_REF(pRefList->eaRefs[i]->hMission))
			{
				return i;
			}
		}
	}
	return -1;
}

static int FindContactDefInList(ContactDef* pDef, ContactDef** eaContactDefs)
{
	if (pDef)
	{
		int i;
		for (i = eaSize(&eaContactDefs)-1; i >= 0; i--)
		{
			if (pDef->name == eaContactDefs[i]->name)
			{
				return i;
			}
		}
	}
	return -1;
}

static int FindContactMissionOffer(ContactDef* pDef, ContactMissionOffer* pOffer)
{
	if (pDef && pOffer)
	{
		int i;
		for (i = eaSize(&pDef->offerList)-1; i >= 0; i--)
		{
			if (GET_REF(pOffer->missionDef) == GET_REF(pDef->offerList[i]->missionDef) &&
				pOffer->allowGrantOrReturn == pDef->offerList[i]->allowGrantOrReturn)
			{
				return i;
			}
		}
	}
	return -1;
}

static int FindMissionDefInList(MissionDef* pDef, MissionDef** eaMissionDefs)
{
	if (pDef)
	{
		int i;
		for (i = eaSize(&eaMissionDefs)-1; i >= 0; i--)
		{
			if (pDef->name == eaMissionDefs[i]->name)
			{
				return i;
			}
		}
	}
	return -1;
}

static int FindContactMissionOfferOverride(MissionDef* pDef, ContactMissionOffer* pOffer, ContactDef* pContactDef)
{
	if (pDef && pOffer && pContactDef)
	{
		int i;
		for (i = eaSize(&pDef->ppMissionOfferOverrides)-1; i >= 0; i--)
		{
			if (pDef->ppMissionOfferOverrides[i]->pMissionOffer &&
				pContactDef->name == pDef->ppMissionOfferOverrides[i]->pcContactName &&
				GET_REF(pOffer->missionDef) == GET_REF(pDef->ppMissionOfferOverrides[i]->pMissionOffer->missionDef) &&
				pOffer->allowGrantOrReturn == pDef->ppMissionOfferOverrides[i]->pMissionOffer->allowGrantOrReturn)
			{
				return i;
			}
		}
	}
	return -1;
}

static void MakeContactMissionOfferEditorCopyMessage(DisplayMessage* pDisplayMessage, MissionDef* pMissionDef)
{
	Message* pMessage = GET_REF(pDisplayMessage->hMessage);
	if (pMessage)
	{
		// Create the editor copy
		pDisplayMessage->pEditorCopy = StructClone(parse_Message, pMessage);
	}

	if (pDisplayMessage->pEditorCopy)
	{
		char pchBuffer[MAX_PATH];
		
		// Fix the filename
		sprintf(pchBuffer, "%s.ms", pMissionDef->filename);
		pDisplayMessage->pEditorCopy->pcFilename = allocAddFilename(pchBuffer);

		// Fix the scope
		sprintf(pchBuffer, "MissionDef/%s/%s", pMissionDef->scope, pMissionDef->name);
		pDisplayMessage->pEditorCopy->pcScope = allocAddString(pchBuffer);
	}
}

static void MakeContactMissionOfferDialogBlockEditorCopyMessage(DialogBlock* pDialogBlock, MissionDef* pMissionDef)
{
	MakeContactMissionOfferEditorCopyMessage(&pDialogBlock->continueTextMesg, pMissionDef);
	MakeContactMissionOfferEditorCopyMessage(&pDialogBlock->displayTextMesg, pMissionDef);
}

static void FixupContactMissionOfferMessages(ContactMissionOffer* pOffer, MissionDef* pMissionDef)
{
	int i;
	MakeContactMissionOfferEditorCopyMessage(&pOffer->acceptStringMesg, pMissionDef);
	MakeContactMissionOfferEditorCopyMessage(&pOffer->declineStringMesg, pMissionDef);
	MakeContactMissionOfferEditorCopyMessage(&pOffer->rewardAbortMesg, pMissionDef);
	MakeContactMissionOfferEditorCopyMessage(&pOffer->rewardAcceptMesg, pMissionDef);
	MakeContactMissionOfferEditorCopyMessage(&pOffer->rewardChooseMesg, pMissionDef);
	MakeContactMissionOfferEditorCopyMessage(&pOffer->turnInStringMesg, pMissionDef);

	for (i = eaSize(&pOffer->completedDialog)-1; i >= 0; i--)
	{
		MakeContactMissionOfferDialogBlockEditorCopyMessage(pOffer->completedDialog[i], pMissionDef);
	}
	for (i = eaSize(&pOffer->failureDialog)-1; i >= 0; i--)
	{
		MakeContactMissionOfferDialogBlockEditorCopyMessage(pOffer->failureDialog[i], pMissionDef);
	}
	for (i = eaSize(&pOffer->greetingDialog)-1; i >= 0; i--)
	{
		MakeContactMissionOfferDialogBlockEditorCopyMessage(pOffer->greetingDialog[i], pMissionDef);
	}
	for (i = eaSize(&pOffer->inProgressDialog)-1; i >= 0; i--)
	{
		MakeContactMissionOfferDialogBlockEditorCopyMessage(pOffer->inProgressDialog[i], pMissionDef);
	}
	for (i = eaSize(&pOffer->offerDialog)-1; i >= 0; i--)
	{
		MakeContactMissionOfferDialogBlockEditorCopyMessage(pOffer->offerDialog[i], pMissionDef);
	}
}

// Move ContactMissionOffers from contacts to missions 
AUTO_COMMAND;
void FixupContactMissionOffers(bool bModify, const char* pchMissionListFilePath)
{
	MissionRefList MissionRefData = {0};
	RefDictIterator Iter = {0};
	ContactDef* pContactDef = NULL;
	ContactDef** eaChangedContactCopies = NULL;
	MissionDef** eaChangedMissionCopies = NULL;
	S32 i, iNumSaved = 0;

	ParserLoadFiles(NULL, pchMissionListFilePath, NULL, 0, parse_MissionRefList, &MissionRefData);

	RefSystem_InitRefDictIterator(g_ContactDictionary, &Iter);

	while (pContactDef = (ContactDef*)RefSystem_GetNextReferentFromIterator(&Iter))
	{
		for (i = eaSize(&pContactDef->offerList)-1; i >= 0; i--)
		{
			ContactMissionOffer* pOffer = pContactDef->offerList[i];
			int iMissionIndex = FindMissionDefInRefList(GET_REF(pOffer->missionDef), &MissionRefData);
			if (iMissionIndex >= 0)
			{
				MissionDef* pMissionDef = GET_REF(pOffer->missionDef);
				int iNewMissionOfferIndex;
				int iContactIndex = FindContactDefInList(pContactDef, eaChangedContactCopies);
				ContactDef* pNewContact = eaGet(&eaChangedContactCopies, iContactIndex);

				if (!pNewContact)
				{
					pNewContact = StructClone(parse_ContactDef, pContactDef);
					langMakeEditorCopy(parse_ContactDef, pNewContact, true);
					eaPush(&eaChangedContactCopies, pNewContact);
				}
				iNewMissionOfferIndex = FindContactMissionOffer(pNewContact, pOffer);
				if (iNewMissionOfferIndex >= 0)
				{
					ContactMissionOffer* pNewOffer = eaRemove(&pNewContact->offerList, iNewMissionOfferIndex);
					if (pNewOffer)
					{
						int iNewMissionIndex = FindMissionDefInList(pMissionDef, eaChangedMissionCopies);
						MissionDef* pNewMissionDef = eaGet(&eaChangedMissionCopies, iNewMissionIndex);

						if (!pNewMissionDef)
						{
							pNewMissionDef = StructClone(parse_MissionDef, pMissionDef);
							langMakeEditorCopy(parse_MissionDef, pNewMissionDef, true);
							eaPush(&eaChangedMissionCopies, pNewMissionDef);
						}
						if (FindContactMissionOfferOverride(pNewMissionDef, pNewOffer, pContactDef) < 0)
						{
							MissionOfferOverride* pOfferOverride = StructCreate(parse_MissionOfferOverride);
							FixupContactMissionOfferMessages(pNewOffer, pNewMissionDef);
							pOfferOverride->pcContactName = allocAddString(pContactDef->name);
							pOfferOverride->pMissionOffer = pNewOffer;
							eaPush(&pNewMissionDef->ppMissionOfferOverrides, pOfferOverride);
						}
						else
						{
							StructDestroy(parse_ContactMissionOffer, pNewOffer);
						}
					}
				}
			}
		}
	}

	// Write out all affected files
	if (bModify)
	{
		const char** ppchFileList = NULL;

		for (i = 0; i < eaSize(&eaChangedContactCopies); i++)
		{
			const char* pchFilename = eaChangedContactCopies[i]->filename;
			if (fileIsReadOnly(pchFilename))
			{
				eaPush(&ppchFileList, pchFilename);
			}
			else
			{
				Errorf("Couldn't checkout file %s", pchFilename);
			}
		}
		for (i = 0; i < eaSize(&eaChangedMissionCopies); i++)
		{
			const char* pchFilename = eaChangedMissionCopies[i]->filename;
			if (fileIsReadOnly(pchFilename))
			{
				eaPush(&ppchFileList, pchFilename);
			}
			else
			{
				Errorf("Couldn't checkout file %s", pchFilename);
			}
		}
		gimmeDLLDoOperations(ppchFileList, GIMME_CHECKOUT, GIMME_QUIET);

		for (i = 0; i < eaSize(&eaChangedContactCopies); i++)
		{
			ContactDef* pDef = eaChangedContactCopies[i];

			langApplyEditorCopySingleFile(parse_ContactDef, pDef, true, false);

			if (ParserWriteTextFileFromSingleDictionaryStruct(pDef->filename, g_ContactDictionary, pDef, 0, 0))
				iNumSaved++;
		}
		for (i = 0; i < eaSize(&eaChangedMissionCopies); i++)
		{
			MissionDef* pDef = eaChangedMissionCopies[i];

			langApplyEditorCopySingleFile(parse_MissionDef, pDef, true, false);

			if (ParserWriteTextFileFromSingleDictionaryStruct(pDef->filename, g_MissionDictionary, pDef, 0, 0))
				iNumSaved++;
		}

		printf("Total Files: %d\n", eaSize(&ppchFileList));
		printf("Files Saved: %d\n", iNumSaved);
		eaDestroy(&ppchFileList);
	}
	else
	{
		printf("Total Files Requiring Fixup: %d\n", eaSize(&eaChangedContactCopies) + eaSize(&eaChangedMissionCopies));
	}
	eaDestroyStruct(&eaChangedContactCopies, parse_ContactDef);
	eaDestroyStruct(&eaChangedMissionCopies, parse_MissionDef);
}

static AlgoCategory FixupAlgoPet_ParseExpr_GetExclusiveCategory(Expression* pExpr)
{
	MultiVal **eaMVStack = NULL;
	int i, s = beaSize(&pExpr->postfixEArray);
	
	for (i = 0; i < s; i++)
	{
		MultiVal *pVal = &pExpr->postfixEArray[i];
		if(pVal->type==MULTIOP_PAREN_OPEN || pVal->type==MULTIOP_PAREN_CLOSE)
			continue;
		if (pVal->type==MULTIOP_FUNCTIONCALL)
		{
			const char *pchFunction = pVal->str;
			if (stricmp(pchFunction,"PowersWithAlgoCategory") == 0)
			{
				MultiVal* pAlgoCategory = eaPop(&eaMVStack);

				if (pAlgoCategory)
				{
					const char *pchAlgoCategory = MultiValGetString(pAlgoCategory,NULL);
					AlgoCategory eAlgoCat = StaticDefineIntGetInt(AlgoCategoryEnum, pchAlgoCategory);

					if (eAlgoCat >= 0)
					{
						return eAlgoCat;
					}
				}
			}
		}
		eaPush(&eaMVStack,pVal);
	}
	return AlgoPetCat_Base;
}

AUTO_COMMAND;
void FixupAlgoPetExclusiveCategories(bool bModify)
{
	RefDictIterator Iter = {0};
	AlgoPetDef* pAlgoPetDef = NULL;
	AlgoPetDef** eaChangedAlgoPetCopies = NULL;
	S32 i, iNumSaved = 0;

	RefSystem_InitRefDictIterator(g_hAlgoPetDict, &Iter);

	while (pAlgoPetDef = (AlgoPetDef*)RefSystem_GetNextReferentFromIterator(&Iter))
	{
		AlgoPetDef* pNewAlgoPetDef = NULL;
		for (i = eaSize(&pAlgoPetDef->ppPowers)-1; i >= 0; i--)
		{
			Expression* pExpr = pAlgoPetDef->ppPowers[i]->pExprWeightMulti;
			if (pExpr)
			{
				AlgoCategory eAlgoCat = FixupAlgoPet_ParseExpr_GetExclusiveCategory(pExpr);
				if (eAlgoCat != AlgoPetCat_Base)
				{
					AlgoPetPowerDef* pAlgoPetPowerDef;
					if (!pNewAlgoPetDef)
					{
						pNewAlgoPetDef = StructClone(parse_AlgoPetDef, pAlgoPetDef);
						eaPush(&eaChangedAlgoPetCopies, pNewAlgoPetDef);
					}
					pAlgoPetPowerDef = eaGet(&pNewAlgoPetDef->ppPowers, i);
					if (pAlgoPetPowerDef)
					{
						exprDestroy(pAlgoPetPowerDef->pExprWeightMulti);
						pAlgoPetPowerDef->pExprWeightMulti = NULL;
						ea32Push(&pAlgoPetPowerDef->puiExclusiveCategory, eAlgoCat);
					}
				}
			}
		}
	}

	// Write out all affected files
	if (bModify)
	{
		const char** ppchFileList = NULL;

		for (i = 0; i < eaSize(&eaChangedAlgoPetCopies); i++)
		{
			const char* pchFilename = eaChangedAlgoPetCopies[i]->pchFileName;
			if (fileIsReadOnly(pchFilename))
			{
				eaPush(&ppchFileList, pchFilename);
			}
			else
			{
				Errorf("Couldn't checkout file %s", pchFilename);
			}
		}
		gimmeDLLDoOperations(ppchFileList, GIMME_CHECKOUT, GIMME_QUIET);

		for (i = 0; i < eaSize(&eaChangedAlgoPetCopies); i++)
		{
			AlgoPetDef* pDef = eaChangedAlgoPetCopies[i];

			if (ParserWriteTextFileFromSingleDictionaryStruct(pDef->pchFileName, g_hAlgoPetDict, pDef, 0, 0))
				iNumSaved++;
		}

		printf("Total Files: %d\n", eaSize(&ppchFileList));
		printf("Files Saved: %d\n", iNumSaved);
		eaDestroy(&ppchFileList);
	}
	else
	{
		printf("Total Files Requiring Fixup: %d\n", eaSize(&eaChangedAlgoPetCopies));
	}
	eaDestroyStruct(&eaChangedAlgoPetCopies, parse_AlgoPetDef);
}

// --------------------------------------------------------------------
// Global replace of one string in a set of .ms files with a different string
// Currently locations and strings are hardcoded.
// --------------------------------------------------------------------

static FileScanAction ScanMessageFiles( char* dir, struct _finddata32_t* data, char*** filenames )
{
	char fullPath[ MAX_PATH ];
	sprintf( fullPath, "%s/%s", dir, data->name );
	
	if( !(data->attrib & _A_SUBDIR) && strEndsWith( data->name, ".ms" )) {
		eaPush( filenames, strdup( fullPath ));
	}

	return FSA_EXPLORE_DIRECTORY;
}


AUTO_COMMAND;
void ReplaceStringsInMessages(bool bModify)
{
	const char* stringToReplace = "{CharacterRank}";
	const char* stringReplacement = "{CharacterRankAlt}";
	
	FILE* log = fopen( "c:\\ReplaceStringsInMessages.txt", "w" );
	char** messageFiles = NULL;

	const char** ppchChangeFileList = NULL;
	const char** ppchUnlockFileList = NULL;

	char* estrReplaceString = NULL;

	int i;
	int iFilesSaved=0;
	int iMessagesUpdated=0;

	fileScanAllDataDirs( "defs/missions", ScanMessageFiles, &messageFiles );
	fileScanAllDataDirs( "defs/contacts", ScanMessageFiles, &messageFiles );
	
	FOR_EACH_IN_EARRAY( messageFiles, char, filename )
	{
		MessageList msgList = { 0 };
		ParserReadTextFile( filename, parse_MessageList, &msgList, 0 );
		{
			bool bFileHasReplace=false;
			
			int it;
			for( it = 0; it != eaSize( &msgList.eaMessages ); it++ )
			{
				char* defaultStr = msgList.eaMessages[it]->pcDefaultString;

				if (defaultStr &&
					strstri(defaultStr,stringToReplace)!=0)
				{
					fprintf( log, "File to update: %s.\n", filename);
					
					eaPush(&ppchChangeFileList,filename);
					if (fileIsReadOnly(filename))
					{
//						fprintf( log, "File to unlock: %s.\n", filename);
						eaPush(&ppchUnlockFileList,filename);
					}
					break;	// We just need to count files at this point
				}
			}
		}
		StructDeInit( parse_MessageList, &msgList );
	} FOR_EACH_END;
	// Can't destroy messageFiles yet since the ppch lists are using the data inside


	// Check out the files

	if (bModify)
	{
		// Checkout files
		gimmeDLLDoOperations(ppchUnlockFileList, GIMME_CHECKOUT, GIMME_QUIET|GIMME_QUIET_LARGE_CHECKOUT);
	}

	// Run through our change file list and update the messages

	estrStackCreate(&estrReplaceString);

	// Set up resource loader parse for writing an array of messages
	SetUpResourceLoaderParse("Message", ParserGetTableSize(parse_Message), parse_Message, NULL);

	for (i = 0; i < eaSize(&ppchChangeFileList); i++)
	{
		int it;
		const char *filename = ppchChangeFileList[i];

		MessageList msgList = { 0 };
		ParserReadTextFile( filename, parse_MessageList, &msgList, 0 );
		for( it = 0; it != eaSize( &msgList.eaMessages ); ++it )
		{
			char* defaultStr = msgList.eaMessages[it]->pcDefaultString;

			if (defaultStr)
			{
				if (strstri(defaultStr, stringToReplace))
				{
					char *newStr = NULL;

					// Use EString utility to do the replace

					estrCopy2(&estrReplaceString, defaultStr);
					estrReplaceOccurrences_CaseInsensitive(&estrReplaceString, stringToReplace, stringReplacement);
					newStr = strdup(estrReplaceString);

					// fprintf( log, "Message Change: %s -> %s\n", defaultStr,newStr);
					
					if (newStr!=NULL)
					{
						free(msgList.eaMessages[it]->pcDefaultString);
						msgList.eaMessages[it]->pcDefaultString = newStr;
					}

					iMessagesUpdated++;
				}
			}
		}

		if (bModify)
		{
			// Not really necessary to check for writability if the check out worked.
			//  but check anyway in case we're doing something like testing on manually writable files
			
			if (!fileIsReadOnly(filename))
			{
				ParserWriteTextFile( filename, parse_ResourceLoaderStruct, &msgList, 0, 0 );				
				iFilesSaved++;
			}
			else
			{
				fprintf( log, "FILE NOT WRITTEN: %s\n", filename);
			}
		}
		StructDeInit( parse_MessageList, &msgList );
	}

	// We need to reset the ResourceLoaderStruct name since we are done using it
	parse_ResourceLoaderStruct[0].name = NULL;

	estrDestroy(&estrReplaceString);

	fprintf(log, "Total Files Check Out Needed: %d\n", eaSize(&ppchUnlockFileList));
	fprintf(log, "Total Files With Updates: %d\n", eaSize(&ppchChangeFileList));
	fprintf(log, "Total Messages With Updates: %d\n", iMessagesUpdated);
	fprintf(log, "Files Saved: %d\n", iFilesSaved);
	
	eaDestroy(&ppchChangeFileList);
	eaDestroy(&ppchUnlockFileList);

	// Done with the original file names.
	eaDestroyEx( &messageFiles, NULL );

	fprintf( log, "DONE\n" );
	fclose( log );
}

AUTO_COMMAND;
void FixupItemAssignmentModifierTypes(bool bModify)
{
	RefDictIterator Iter = {0};
	ItemAssignmentDef* pDef = NULL;
	ItemAssignmentDef** eaChangedCopies = NULL;
	S32 i, iNumSaved = 0;

	RefSystem_InitRefDictIterator(g_hItemAssignmentDict, &Iter);

	while (pDef = (ItemAssignmentDef*)RefSystem_GetNextReferentFromIterator(&Iter))
	{
		ItemAssignmentDef* pNewDef = NULL;
		for (i = eaSize(&pDef->eaModifiers)-1; i >= 0; i--)
		{
			ItemAssignmentOutcomeModifier* pModifier = pDef->eaModifiers[i];

			if (pModifier->eType <= kItemAssignmentOutcomeModifierType_None)
			{
				ItemAssignmentOutcomeModifier* pNewMod;
				if (!pNewDef)
				{
					pNewDef = StructClone(parse_ItemAssignmentDef, pDef);
					eaPush(&eaChangedCopies, pNewDef);
				}
				pNewMod = pNewDef->eaModifiers[i];
				pNewMod->eType = StaticDefineIntGetInt(ItemAssignmentOutcomeModifierTypeEnum, pModifier->pchName);

				if (pNewMod->eType <= kItemAssignmentOutcomeModifierType_None)
				{
					pNewMod->eType = kItemAssignmentOutcomeModifierType_None;
					Errorf("Couldn't find ItemAssignment modifier type '%s'", pModifier->pchName);
				}
			}
		}
	}

	// Write out all affected files
	if (bModify)
	{
		const char** ppchFileList = NULL;

		for (i = 0; i < eaSize(&eaChangedCopies); i++)
		{
			const char* pchFilename = eaChangedCopies[i]->pchFileName;
			if (fileIsReadOnly(pchFilename))
			{
				eaPush(&ppchFileList, pchFilename);
			}
			else
			{
				Errorf("Couldn't checkout file %s", pchFilename);
			}
		}
		gimmeDLLDoOperations(ppchFileList, GIMME_CHECKOUT, GIMME_QUIET);

		for (i = 0; i < eaSize(&eaChangedCopies); i++)
		{
			pDef = eaChangedCopies[i];

			if (ParserWriteTextFileFromSingleDictionaryStruct(pDef->pchFileName, g_hItemAssignmentDict, pDef, 0, 0))
				iNumSaved++;
		}

		printf("Total Files: %d\n", eaSize(&ppchFileList));
		printf("Files Saved: %d\n", iNumSaved);
		eaDestroy(&ppchFileList);
	}
	else
	{
		printf("Total Files Requiring Fixup: %d\n", eaSize(&eaChangedCopies));
	}
	eaDestroyStruct(&eaChangedCopies, parse_ItemAssignmentDef);
}

AUTO_COMMAND;
void FixupBadPropPowers(bool bModify)
{
	RefDictIterator Iter = {0};
	ItemDef* pDef = NULL;
	PowerDef** eaChangedCopies = NULL;
	S32 i, iNumSaved = 0;

	RefSystem_InitRefDictIterator(g_hItemDict, &Iter);

	while (pDef = (ItemDef*)RefSystem_GetNextReferentFromIterator(&Iter))
	{
		for (i = eaSize(&pDef->ppItemPowerDefRefs)-1; i >= 0; i--)
		{
			ItemPowerDef* pItemPowDef = GET_REF(pDef->ppItemPowerDefRefs[i]->hItemPowerDef);
			PowerDef* pPowerDef = pItemPowDef ? GET_REF(pItemPowDef->hPower) : NULL;

			if (pPowerDef && pPowerDef->powerProp.bPropPower && 
				!strStartsWith(pPowerDef->pchName, "Duty_Officer") && !strStartsWith(pPowerDef->pchName, "doff"))
			{
				PowerDef* pNewDef = StructClone(parse_PowerDef, pPowerDef);
				pNewDef->powerProp.bPropPower = false;
				pNewDef->powerProp.eCharacterTypes = 0;
				eaPush(&eaChangedCopies, pNewDef);
			}
		}
	}

	// Write out all affected files
	if (bModify)
	{
		const char** ppchFileList = NULL;

		for (i = 0; i < eaSize(&eaChangedCopies); i++)
		{
			const char* pchFilename = eaChangedCopies[i]->pchFile;
			if (fileIsReadOnly(pchFilename))
			{
				eaPush(&ppchFileList, pchFilename);
			}
			else
			{
				Errorf("Couldn't checkout file %s", pchFilename);
			}
		}
		gimmeDLLDoOperations(ppchFileList, GIMME_CHECKOUT, GIMME_QUIET);

		for (i = 0; i < eaSize(&eaChangedCopies); i++)
		{
			PowerDef* pPowerDef = eaChangedCopies[i];

			if (ParserWriteTextFileFromSingleDictionaryStruct(pPowerDef->pchFile, g_hPowerDefDict, pPowerDef, 0, 0))
				iNumSaved++;
		}

		printf("Total Files: %d\n", eaSize(&ppchFileList));
		printf("Files Saved: %d\n", iNumSaved);
		eaDestroy(&ppchFileList);
	}
	else
	{
		printf("Total Files Requiring Fixup: %d\n", eaSize(&eaChangedCopies));
	}
	eaDestroyStruct(&eaChangedCopies, parse_PowerDef);
}

AUTO_STRUCT;
typedef struct FixupGroupDefs
{
	const char* pchFilename; AST(KEY POOL_STRING)
	int* piGroupNameIds;
	LibFileLoad* pLibFile; AST(UNOWNED)
} FixupGroupDefs;

static int FixupGroupDefInteractableChairFX_Recurse(GroupDef* pGroupDef, LibFileLoad* pLibFile, bool bCommitChanges, FixupGroupDefs*** peaFixupGroupDefs)
{
	int i, iCount = 0;
	if (pGroupDef->def_lib)
	{
		for (i = 0; i < eaSize(&pGroupDef->children); i++)
		{
			GroupChild* pChild = pGroupDef->children[i];
			GroupDef* pChildDef = groupChildGetDef(pGroupDef, pChild, true);
			if (pChildDef)
			{
				iCount += FixupGroupDefInteractableChairFX_Recurse(pChildDef, pLibFile, bCommitChanges, peaFixupGroupDefs);
			}
		}
	}
	if (pGroupDef->property_structs.interaction_properties)
	{
		WorldInteractionProperties* pInteractProps = pGroupDef->property_structs.interaction_properties;

		for (i = eaSize(&pInteractProps->eaEntries)-1; i >= 0; i--)
		{
			WorldInteractionPropertyEntry* pEntry = pInteractProps->eaEntries[i];
			if (pEntry->pChairProperties)
			{
				if (!pInteractProps->pchOverrideFX)
				{
					if (peaFixupGroupDefs)
					{
						FixupGroupDefs* pFixup = eaIndexedGetUsingString(peaFixupGroupDefs, pGroupDef->filename);
						if (!pFixup)
						{
							pFixup = StructCreate(parse_FixupGroupDefs);
							pFixup->pchFilename = pGroupDef->filename;
							pFixup->pLibFile = pLibFile;
							eaPush(peaFixupGroupDefs, pFixup);
						}
						eaiPush(&pFixup->piGroupNameIds, pGroupDef->name_uid);
					}
					if (bCommitChanges)
					{
						pInteractProps->pchOverrideFX = allocAddString("FX_InteractChair");
					}
					iCount++;
				}
				break;
			}
		}
	}
	return iCount;
}

AUTO_COMMAND;
void FixupInteractableChairFX(bool bModify)
{
	int i, j;
	FixupGroupDefs** eaFixupObjLibGroups = NULL;
	FixupGroupDefs** eaFixupGeoLayerGroups = NULL;
	LibFileLoad** eaGeoLayers = NULL;
	GroupDefLib* pGroupDefLib = objectLibraryGetDefLib();

	eaIndexedEnable(&eaFixupObjLibGroups, parse_FixupGroupDefs);
	eaIndexedEnable(&eaFixupGeoLayerGroups, parse_FixupGroupDefs);

	AssetLoadGeoLayers(&eaGeoLayers);
	AssetPutGeoLayersInEditMode(&eaGeoLayers);

	for (i = 0; i < eaSize(&eaGeoLayers); i++)
	{
		LibFileLoad* pLibFile = eaGeoLayers[i];
		for (j = 0; j < eaSize(&pLibFile->defs); j++)
		{
			GroupDef* pGroupDef = pLibFile->defs[j];
			FixupGroupDefInteractableChairFX_Recurse(pGroupDef, pLibFile, true, &eaFixupGeoLayerGroups);
		}
	}

	FOR_EACH_IN_STASHTABLE(pGroupDefLib->defs, GroupDef, pGroupDef)
	{
		FixupGroupDefInteractableChairFX_Recurse(pGroupDef, NULL, false, &eaFixupObjLibGroups);
	} 
	FOR_EACH_END;

	// Write out all affected files
	if (bModify)
	{
		int iGroupsSaved = 0;
		const char** ppchFilenames = NULL;
		LibFileLoad** eaModifiedGeoLayers = NULL;

		for (i = eaSize(&eaFixupObjLibGroups)-1; i >= 0; i--)
		{
			const char* pchFilename = eaFixupObjLibGroups[i]->pchFilename;
			if (fileIsReadOnly(pchFilename))
			{
				eaPush(&ppchFilenames, pchFilename);
			}
			else
			{
				Errorf("Couldn't checkout file %s", pchFilename);
			}
		}
		for (i = eaSize(&eaFixupGeoLayerGroups)-1; i >= 0; i--)
		{
			const char* pchFilename = eaFixupGeoLayerGroups[i]->pchFilename;
			if (fileIsReadOnly(pchFilename))
			{
				eaPush(&ppchFilenames, pchFilename);
			}
			else
			{
				Errorf("Couldn't checkout file %s", pchFilename);
			}
		}

		gimmeDLLDoOperations(ppchFilenames, GIMME_CHECKOUT, GIMME_QUIET);

		for (i = 0; i < eaSize(&eaFixupGeoLayerGroups); i++)
		{
			eaPush(&eaModifiedGeoLayers, eaFixupGeoLayerGroups[i]->pLibFile);
			iGroupsSaved += eaiSize(&eaFixupGeoLayerGroups[i]->piGroupNameIds);
		}
		AssetSaveGeoLayers(&eaModifiedGeoLayers, false);
		eaDestroy(&eaModifiedGeoLayers);

		for (i = 0; i < eaSize(&eaFixupObjLibGroups); i++)
		{
			GroupDefList* pGroupList = StructCreate(parse_GroupDefList);
			FixupGroupDefs* pFixup = eaFixupObjLibGroups[i];
			int iSavedCount = 0;

			if (!ParserReadTextFile(pFixup->pchFilename, parse_GroupDefList, pGroupList, 0))
			{
				StructDestroy(parse_GroupDefList, pGroupList);
				continue;
			}
			for (j = 0; j < eaSize(&pGroupList->defs); j++)
			{
				GroupDef* pGroupDef = pGroupList->defs[j];
				iGroupsSaved += FixupGroupDefInteractableChairFX_Recurse(pGroupDef, NULL, true, NULL);
			}
			ParserWriteTextFile(pFixup->pchFilename, parse_GroupDefList, pGroupList, 0, 0);
			StructDestroy(parse_GroupDefList, pGroupList);
		}

		printf("Total Files: %d\n", eaSize(&ppchFilenames));
		printf("Groups Saved: %d\n", iGroupsSaved);
		eaDestroy(&ppchFilenames);
	}
	else
	{
		int* piUniqueGroupIds = NULL;

		for (i = eaSize(&eaFixupObjLibGroups)-1; i >= 0; i--)
		{
			for (j = eaiSize(&eaFixupObjLibGroups[i]->piGroupNameIds)-1; j >= 0; j--)
			{
				eaiPushUnique(&piUniqueGroupIds, eaFixupObjLibGroups[i]->piGroupNameIds[j]);
			}
		}
		for (i = eaSize(&eaFixupGeoLayerGroups)-1; i >= 0; i--)
		{
			for (j = eaiSize(&eaFixupGeoLayerGroups[i]->piGroupNameIds)-1; j >= 0; j--)
			{
				eaiPushUnique(&piUniqueGroupIds, eaFixupGeoLayerGroups[i]->piGroupNameIds[j]);
			}
		}
		printf("Total Files Requiring Fixup: %d\n", eaSize(&eaFixupObjLibGroups) + eaSize(&eaFixupGeoLayerGroups));
		printf("Total GroupDefs Requiring Fixup: %d\n", eaiSize(&piUniqueGroupIds));
		eaiDestroy(&piUniqueGroupIds);
	}
	eaDestroyStruct(&eaFixupObjLibGroups, parse_FixupGroupDefs);
	eaDestroyStruct(&eaFixupGeoLayerGroups, parse_FixupGroupDefs);
	AssetCleanupGeoLayers(&eaGeoLayers);
}

AUTO_COMMAND;
void FixupPowerTreeNodesToUseCostVars(int iFindCost, const char* pchFindCostPool, const char* pchReplaceWithCostVar, bool bModify)
{
	RefDictIterator Iter = {0};
	PowerTreeDef** eaChangedCopies = NULL;
	PowerTreeDef* pDef;
	S32 i, j, k, iNumSaved = 0;

	if (!powervar_Find(pchReplaceWithCostVar))
	{
		Errorf("Couldn't find PowerVar %s", pchReplaceWithCostVar);
		return;
	}

	RefSystem_InitRefDictIterator(g_hPowerTreeDefDict, &Iter);

	while (pDef = (PowerTreeDef*)RefSystem_GetNextReferentFromIterator(&Iter))
	{
		PowerTreeDef* pChangedCopy = NULL;
		for (i = eaSize(&pDef->ppGroups)-1; i >= 0; i--)
		{
			PTGroupDef* pGroupDef = pDef->ppGroups[i];
			for (j = eaSize(&pGroupDef->ppNodes)-1; j >= 0; j--)
			{
				PTNodeDef* pNodeDef = pGroupDef->ppNodes[j];
				for (k = eaSize(&pNodeDef->ppRanks)-1; k >= 0; k--)
				{
					PTNodeRankDef* pRankDef = pNodeDef->ppRanks[k];
					if (pRankDef->iCost && 
						pRankDef->iCost == iFindCost &&
						stricmp(pRankDef->pchCostTable, pchFindCostPool)==0)
					{
						PTNodeRankDef* pChangedRankDef;
						if (!pChangedCopy)
						{
							pChangedCopy = StructClone(parse_PowerTreeDef, pDef);
							eaPush(&eaChangedCopies, pChangedCopy);
						}
						pChangedRankDef = pChangedCopy->ppGroups[i]->ppNodes[j]->ppRanks[k];
						pChangedRankDef->iCost = 0;
						StructCopyString(&pChangedRankDef->pchCostVar, pchReplaceWithCostVar);
					}
				}
			}
		}
	}

	// Write out all affected files
	if (bModify)
	{
		const char** ppchFileList = NULL;

		for (i = 0; i < eaSize(&eaChangedCopies); i++)
		{
			const char* pchFilename = eaChangedCopies[i]->pchFile;
			if (fileIsReadOnly(pchFilename))
			{
				eaPush(&ppchFileList, pchFilename);
			}
			else if (!gimmeDLLQueryIsFileLockedByMeOrNew(pchFilename))
			{
				Errorf("Couldn't checkout file %s", pchFilename);
			}
		}
		gimmeDLLDoOperations(ppchFileList, GIMME_CHECKOUT, GIMME_QUIET);

		for (i = 0; i < eaSize(&eaChangedCopies); i++)
		{
			PowerTreeDef* pTreeDef = eaChangedCopies[i];

			if (ParserWriteTextFileFromSingleDictionaryStruct(pTreeDef->pchFile, g_hPowerTreeDefDict, pTreeDef, 0, 0))
				iNumSaved++;
		}

		printf("Total Files: %d\n", eaSize(&ppchFileList));
		printf("Files Saved: %d\n", iNumSaved);
		eaDestroy(&ppchFileList);
	}
	else
	{
		printf("Total Files Requiring Fixup: %d\n", eaSize(&eaChangedCopies));
	}
	eaDestroyStruct(&eaChangedCopies, parse_PowerTreeDef);
}

AUTO_COMMAND;
void FixupPowersDeprecatedAIFields(int limit, bool bDoCheckout, bool bModify)
{
	RefDictIterator Iter = {0};
	PowerDef** needChanges = NULL;
	PowerDef** eaChangedCopies = NULL;
	PowerDef* pDef;
	PowerDef defDef = {0};
	S32 i, iNumSaved = 0;
	char* estr = NULL;

	char* fields[][2] = {	{"AIMinRange", "minDist"},
							{"AIMaxRange", "maxDist"} };

	int fieldidx[40][2] = {-1};

	StructInit(parse_PowerDef, &defDef);

	for(i=0; i<ARRAY_SIZE(fields); i++)
	{
		estrPrintf(&estr, ".%s", fields[i][0]);
		ParserResolvePath(estr, parse_PowerDef, NULL, NULL, &fieldidx[i][0], NULL, NULL, NULL, NULL, 0);

		estrPrintf(&estr, ".%s", fields[i][1]);
		ParserResolvePath(estr, parse_AIPowerConfigDef, NULL, NULL, &fieldidx[i][1], NULL, NULL, NULL, NULL, 0);
	}

	RefSystem_InitRefDictIterator(g_hPowerDefDict, &Iter);

	while (pDef = (PowerDef*)RefSystem_GetNextReferentFromIterator(&Iter))
	{
		bool isInherit = StructInherit_IsInheriting(parse_PowerDef, pDef);

		for(i=0; i<ARRAY_SIZE(fields); i++)
		{
			bool fieldInherit = false;
			if(isInherit)
			{
				estrPrintf(&estr, ".%s", fields[i][0]);

				fieldInherit = StructInherit_GetOverrideType(parse_PowerDef, pDef, estr)==OVERRIDE_NONE;
			}

			// If we're doing some overriding or the token is non-default, we need to fix it up
			if(isInherit && !fieldInherit ||
				!isInherit && TokenCompare(parse_PowerDef, fieldidx[i][0], pDef, &defDef, 0, 0))
			{
				eaPush(&needChanges, pDef);
				break;
			}
		}

		if(limit > 0 && eaSize(&needChanges)>=limit)
			break;
	}

	FOR_EACH_IN_EARRAY_FORWARDS(needChanges, PowerDef, pNeedChanged)
	{
		PowerDef *changed = NULL;
		bool isInherit = StructInherit_IsInheriting(parse_PowerDef, pNeedChanged);

		changed = StructClone(parse_PowerDef, pNeedChanged);
		
		if(!changed->pAIPowerConfigDefInst)
			changed->pAIPowerConfigDefInst = StructCreate(parse_AIPowerConfigDef);

		for(i=0; i<ARRAY_SIZE(fields); i++)
		{
			char* srcfield = fields[i][0];
			char* dstfield = fields[i][1];
			bool fieldInherit = false;
			if(isInherit)
			{
				estrPrintf(&estr, ".%s", fields[i][0]);

				fieldInherit = StructInherit_GetOverrideType(parse_PowerDef, changed, estr)==OVERRIDE_NONE;
			}

			// A parent will take care of moving this and trying to modify it here does nothing
			if(fieldInherit)
				continue;

			// Field in non-inheriting struct that is default doesn't need to be moved
			if(!isInherit && !TokenCompare(parse_PowerDef, fieldidx[i][0], pNeedChanged, &defDef, 0, 0))
				continue;

			// Determine if target field is already specified and thus only needs to be cleaned out
			if(!TokenIsSpecifiedByName(parse_AIPowerConfigDef, changed->pAIPowerConfigDefInst, dstfield))
			{
				// Create override if necessary
				if(isInherit)
				{
					estrPrintf(&estr, ".aiPowerConfigDefInst.%s", dstfield);
					StructInherit_CreateFieldOverride(parse_PowerDef, changed, estr);
				}

				// Move data
				estrClear(&estr);
				FieldWriteText(parse_PowerDef, fieldidx[i][0], changed, 0, &estr, 0);
				FieldReadText(parse_AIPowerConfigDef, fieldidx[i][1], changed->pAIPowerConfigDefInst, 0, estr);
			}

			// Set old to default and delete inheritance data
			TokenCopy(parse_PowerDef, fieldidx[i][0], changed, &defDef, true);
			estrPrintf(&estr, ".%s", srcfield);
			if(isInherit)
				StructInherit_DestroyOverride(parse_PowerDef, changed, estr);
		}

		if(!StructInherit_UpdateFromStruct(parse_PowerDef, changed, false))
			printf("Error\n");

		eaPush(&eaChangedCopies, changed);
	}
	FOR_EACH_END;

	// Write out all affected files
	if (bDoCheckout)
	{
		const char** ppchFileList = NULL;

		for (i = eaSize(&eaChangedCopies)-1; i>=0; i--)
		{
			const char* pchFilename = eaChangedCopies[i]->pchFile;
			const char* pchUsername = NULL;
			if (fileIsReadOnly(pchFilename))
			{
				if(pchUsername = gimmeDLLQueryIsFileLocked(pchFilename))
					Errorf("Can't checkout %s because it is locked by %s", pchFilename, pchUsername);
				else
					eaPush(&ppchFileList, pchFilename);
			}
			else if(!gimmeDLLQueryIsFileLockedByMeOrNew(pchFilename))
			{
				StructDestroy(parse_PowerDef, eaChangedCopies[i]);
				eaRemoveFast(&eaChangedCopies, i);
				Errorf("Couldn't checkout file %s", pchFilename);
			}
		}
		gimmeDLLDoOperations(ppchFileList, GIMME_CHECKOUT, GIMME_QUIET);

		if(bModify)
		{
			for (i = 0; i < eaSize(&eaChangedCopies); i++)
			{
				PowerDef* changed = eaChangedCopies[i];

				printf("Converting %s\n", changed->pchFile);

				if (ParserWriteTextFileFromSingleDictionaryStruct(changed->pchFile, g_hPowerDefDict, changed, 0, 0))
					iNumSaved++;
			}
		}

		printf("Total Files: %d\n", eaSize(&ppchFileList));
		printf("Files Saved: %d\n", iNumSaved);
		eaDestroy(&ppchFileList);
	}
	else
	{
		printf("Total Files Requiring Fixup: %d\n", eaSize(&eaChangedCopies));
	}
	eaDestroyStruct(&eaChangedCopies, parse_PowerDef);
}

AUTO_COMMAND;
void FixupCrittersDeprecatedAIFields(int limit, bool bDoCheckout, bool bModify)
{
	RefDictIterator Iter = {0};
	CritterDef** eaChangedCopies = NULL;
	CritterDef* pDef;
	CritterDef** needChanges = NULL;

	CritterPowerConfig defCPC = {0};

	char* fields[][2] = {	{"AIPreferredMinRange", "minDist"},
							{"AIPreferredMaxRange", "maxDist"},
							{"AIWeight", "absWeight"},
							{"AIChainTime", "chainTime"},
							{"AIChainTarget", "chainTarget"},
							{"AIChainRequires", "chainRequires"},
							{"AICureRequires", "cureRequires"},
							{"AIRequires", "aiRequires"},
							{"AITargetOverride", "targetOverride"},
							{"AIEndCondition", "aiEndCondition"},
							{"AIWeightModifier", "weightModifier"} };
	int fieldidx[40][2] = {-1};

	S32 i, iNumSaved = 0;
	char* estr = NULL;

	for(i=0; i<ARRAY_SIZE(fields); i++)
	{
		estrPrintf(&estr, ".%s", fields[i][0]);
		ParserResolvePath(estr, parse_CritterPowerConfig, NULL, NULL, &fieldidx[i][0], NULL, NULL, NULL, NULL, 0);

		estrPrintf(&estr, ".%s", fields[i][1]);
		ParserResolvePath(estr, parse_AIPowerConfigDef, NULL, NULL, &fieldidx[i][1], NULL, NULL, NULL, NULL, 0);
	}

	StructInit(parse_CritterPowerConfig, &defCPC);

	RefSystem_InitRefDictIterator(g_hCritterDefDict, &Iter);

	while (pDef = (CritterDef*)RefSystem_GetNextReferentFromIterator(&Iter))
	{
		bool isInherit = StructInherit_IsInheriting(parse_CritterDef, pDef);

		FOR_EACH_IN_EARRAY(pDef->ppPowerConfigs, CritterPowerConfig, cpc)
		{
			bool cpcInherited = isInherit;
			
			estrPrintf(&estr, ".powerConfigs[\"%d\"]", cpc->iKey);
			cpcInherited = isInherit && StructInherit_GetOverrideType(parse_CritterDef, pDef, estr)!=OVERRIDE_ADD;

			for(i=0; i<ARRAY_SIZE(fields); i++)
			{
				bool fieldInherit = false;
				if(isInherit && cpcInherited)
				{
					estrPrintf(&estr, ".powerconfigs[\"%d\"].%s", cpc->iKey, fields[i][0]);

					fieldInherit = StructInherit_GetOverrideType(parse_CritterDef, pDef, estr)==OVERRIDE_NONE;
				}

				// If we're doing some overriding or the token is non-default, we need to fix it up
				if(isInherit && !fieldInherit ||
					!isInherit && (TokenCompare(parse_CritterPowerConfig, fieldidx[i][0], cpc, &defCPC, 0, 0)))
				{
					eaPush(&needChanges, pDef);
					break;
				}
			}

			if(eaTail(&needChanges)==pDef)
				break;
		}
		FOR_EACH_END;

		if(eaSize(&needChanges)>=limit)
			break;
	}

	FOR_EACH_IN_EARRAY_FORWARDS(needChanges, CritterDef, pNeedChanged)
	{
		CritterDef *changed = NULL;
		bool isInherit = StructInherit_IsInheriting(parse_CritterDef, pNeedChanged);

		changed = StructClone(parse_CritterDef, pNeedChanged);

		FOR_EACH_IN_EARRAY(changed->ppPowerConfigs, CritterPowerConfig, cpc)
		{
			int idx = FOR_EACH_IDX(-, cpc);
			bool cpcInherit = false;
			bool needOverride = false;
			bool changedAny = false;

			estrPrintf(&estr, ".powerconfigs[\"%d\"]", cpc->iKey);
			cpcInherit = isInherit && StructInherit_GetOverrideType(parse_CritterDef, pNeedChanged, estr)!=OVERRIDE_ADD;

			if(!cpc->aiPowerConfigDefInst)
				cpc->aiPowerConfigDefInst = StructCreate(parse_AIPowerConfigDef);

			for(i=0; i<ARRAY_SIZE(fields); i++)
			{
				char* srcfield = fields[i][0];
				char* dstfield = fields[i][1];
				bool fieldInherit = false;
				if(isInherit && cpcInherit)
				{
					estrPrintf(&estr, ".powerconfigs[\"%d\"].%s", cpc->iKey, fields[i][0]);

					fieldInherit = StructInherit_GetOverrideType(parse_CritterDef, changed, estr)==OVERRIDE_NONE;
				}

				// A parent will take care of moving this and trying to modify it here does nothing
				if(fieldInherit)
					continue;

				// Field in non-inheriting struct that is default doesn't need to be moved, it can just be erased
				if((!isInherit || !cpcInherit) && 
					 (!TokenCompare(parse_CritterPowerConfig, fieldidx[i][0], cpc, &defCPC, 0, 0)))
					continue;

				changedAny = true;

				// Determine if target field is already specified and thus only needs to be cleaned out
				if(!TokenIsSpecifiedByName(parse_AIPowerConfigDef, cpc->aiPowerConfigDefInst, dstfield))
				{
					// Move data
					if(TOK_HAS_SUBTABLE(parse_CritterPowerConfig[fieldidx[i][0]].type))
					{
						ParseTable *subtable = NULL;
						StructGetSubtable(parse_CritterPowerConfig, fieldidx[i][0], NULL, 0, &subtable, NULL);

						if(subtable==parse_Expression || subtable==parse_Expression_StructParam)
						{
							Expression *expr = TokenStoreGetPointer(parse_CritterPowerConfig, fieldidx[i][0], cpc, 0, NULL);
							Expression *copy = StructClone(parse_Expression, expr);

							TokenStoreSetPointer(parse_AIPowerConfigDef, fieldidx[i][1], cpc->aiPowerConfigDefInst, 0, copy, NULL);
						}
						else
							assert(0);
					}
					else
					{
						estrClear(&estr);
						FieldWriteText(parse_CritterPowerConfig, fieldidx[i][0], cpc, 0, &estr, 0);
						FieldReadText(parse_AIPowerConfigDef, fieldidx[i][1], cpc->aiPowerConfigDefInst, 0, estr);
					}
				}

				// Set old to default and delete inheritance data
				TokenCopy(parse_CritterPowerConfig, fieldidx[i][0], cpc, &defCPC, true);
				if(isInherit && !fieldInherit)
				{
					estrPrintf(&estr, ".powerconfigs[\"%d\"].%s", cpc->iKey, srcfield);

					if(StructInherit_GetOverrideType(parse_CritterDef, changed, estr)!=OVERRIDE_NONE)
						StructInherit_DestroyOverride(parse_CritterDef, changed, estr);
				}
			}

			if(changedAny && isInherit && cpcInherit)
			{
				int overridetype = OVERRIDE_NONE;
				estrPrintf(&estr, ".powerconfigs[\"%d\"].aiPowerConfigDefInst", cpc->iKey);

				overridetype = StructInherit_GetOverrideType(parse_CritterDef, changed, estr);
				if(overridetype==OVERRIDE_REMOVE)
					StructInherit_DestroyOverride(parse_CritterDef, changed, estr);

				if(overridetype==OVERRIDE_NONE || overridetype==OVERRIDE_REMOVE)
					StructInherit_CreateFieldOverride(parse_CritterDef, changed, estr);	
			}
		}
		FOR_EACH_END;

		if(!StructInherit_UpdateFromStruct(parse_CritterDef, changed, false))
			printf("Error\n");

		eaPush(&eaChangedCopies, changed);
	}
	FOR_EACH_END;

	// Write out all affected files
	if (bDoCheckout)
	{
		const char** ppchFileList = NULL;

		for (i = eaSize(&eaChangedCopies)-1; i>=0; i--)
		{
			const char* pchFilename = eaChangedCopies[i]->pchFileName;
			const char* pchUsername = NULL;
			if (fileIsReadOnly(pchFilename))
			{
				if(pchUsername = gimmeDLLQueryIsFileLocked(pchFilename))
					Errorf("Can't checkout %s because it is locked by %s", pchFilename, pchUsername);
				else
					eaPush(&ppchFileList, pchFilename);
			}
			else if (!gimmeDLLQueryIsFileLockedByMeOrNew(pchFilename))
			{
				StructDestroy(parse_CritterDef, eaChangedCopies[i]);
				eaRemoveFast(&eaChangedCopies, i);
				Errorf("Couldn't checkout file %s", pchFilename);
			}
		}
		gimmeDLLDoOperations(ppchFileList, GIMME_CHECKOUT, GIMME_QUIET);

		if(bModify)
		{
			for (i = 0; i < eaSize(&eaChangedCopies); i++)
			{
				CritterDef* changed = eaChangedCopies[i];

				printf("Converting %s\n", changed->pchFileName);

				if (ParserWriteTextFileFromSingleDictionaryStruct(changed->pchFileName, g_hCritterDefDict, changed, 0, 0))
					iNumSaved++;
			}
		}

		printf("Total Files: %d\n", eaSize(&ppchFileList));
		printf("Files Saved: %d\n", iNumSaved);
		eaDestroy(&ppchFileList);
	}
	else
	{
		printf("Total Files Requiring Fixup: %d\n", eaSize(&eaChangedCopies));
	}
	
	estrDestroy(&estr);
	eaDestroyStruct(&eaChangedCopies, parse_CritterDef);
}

static ItemGenData* FixupItemGenMessages_GetOrCreateChangedCopy(ItemGenData* pOrigDef, ItemGenData* pChangedCopy, ItemGenData*** peaChangedCopies)
{
	if (!pChangedCopy)
	{
		pChangedCopy = StructClone(parse_ItemGenData, pOrigDef);
		langMakeEditorCopy(parse_ItemGenData, pChangedCopy, true);
		eaPush(peaChangedCopies, pChangedCopy);
	}
	return pChangedCopy;
}

AUTO_COMMAND;
void FixupItemGenMessages(bool bModify)
{
	RefDictIterator Iter = {0};
	ItemGenData** eaChangedCopies = NULL;
	ItemGenData* pDef;
	S32 i, j, iNumSaved = 0;

	RefSystem_InitRefDictIterator(g_hItemGenDict, &Iter);

	sharedMemoryEnableEditorMode();

	while (pDef = (ItemGenData*)RefSystem_GetNextReferentFromIterator(&Iter))
	{
		ItemGenData* pChangedCopy = NULL;

		if (GET_REF(pDef->DisplayName_Obsolete.hMessage))
		{
			Message* pMessage = GET_REF(pDef->DisplayName_Obsolete.hMessage);
			pChangedCopy = FixupItemGenMessages_GetOrCreateChangedCopy(pDef, pChangedCopy, &eaChangedCopies);
			pChangedCopy->pchDisplayName = StructAllocString(pMessage->pcDefaultString);
			StructFreeStringSafe(&pChangedCopy->DisplayName_Obsolete.pEditorCopy->pcDefaultString);
		}
		if (GET_REF(pDef->DisplayDesc_Obsolete.hMessage))
		{
			Message* pMessage = GET_REF(pDef->DisplayDesc_Obsolete.hMessage);
			pChangedCopy = FixupItemGenMessages_GetOrCreateChangedCopy(pDef, pChangedCopy, &eaChangedCopies);
			pChangedCopy->pchDisplayDesc = StructAllocString(pMessage->pcDefaultString);
			StructFreeStringSafe(&pChangedCopy->DisplayDesc_Obsolete.pEditorCopy->pcDefaultString);
		}
		if (GET_REF(pDef->DisplayDescShort_Obsolete.hMessage))
		{
			Message* pMessage = GET_REF(pDef->DisplayDescShort_Obsolete.hMessage);
			pChangedCopy = FixupItemGenMessages_GetOrCreateChangedCopy(pDef, pChangedCopy, &eaChangedCopies);
			pChangedCopy->pchDisplayDescShort = StructAllocString(pMessage->pcDefaultString);
			StructFreeStringSafe(&pChangedCopy->DisplayDescShort_Obsolete.pEditorCopy->pcDefaultString);
		}

		for (i = eaSize(&pDef->ppItemTiers)-1; i >= 0; i--)
		{
			ItemGenTier* pTier = pDef->ppItemTiers[i];
			ItemGenTier* pChangedCopyTier;

			if (GET_REF(pTier->Prefix_Obsolete.hMessage))
			{
				Message* pMessage = GET_REF(pTier->Prefix_Obsolete.hMessage);
				pChangedCopy = FixupItemGenMessages_GetOrCreateChangedCopy(pDef, pChangedCopy, &eaChangedCopies);
				pChangedCopyTier = pChangedCopy->ppItemTiers[i];
				pChangedCopyTier->pchDisplayPrefix = StructAllocString(pMessage->pcDefaultString);
				StructFreeStringSafe(&pChangedCopyTier->Prefix_Obsolete.pEditorCopy->pcDefaultString);
			}
			if (GET_REF(pTier->Suffix_Obsolete.hMessage))
			{
				Message* pMessage = GET_REF(pTier->Suffix_Obsolete.hMessage);
				pChangedCopy = FixupItemGenMessages_GetOrCreateChangedCopy(pDef, pChangedCopy, &eaChangedCopies);
				pChangedCopyTier = pChangedCopy->ppItemTiers[i];
				pChangedCopyTier->pchDisplaySuffix = StructAllocString(pMessage->pcDefaultString);
				StructFreeStringSafe(&pChangedCopyTier->Suffix_Obsolete.pEditorCopy->pcDefaultString);
			}

			for (j = eaSize(&pTier->ppRarities)-1; j >= 0; j--)
			{
				ItemGenRarityDef* pRarity = pTier->ppRarities[j];
				ItemGenRarityDef* pChangedCopyRarity;

				if (GET_REF(pRarity->Prefix_Obsolete.hMessage))
				{
					Message* pMessage = GET_REF(pRarity->Prefix_Obsolete.hMessage);
					pChangedCopy = FixupItemGenMessages_GetOrCreateChangedCopy(pDef, pChangedCopy, &eaChangedCopies);
					pChangedCopyRarity = pChangedCopy->ppItemTiers[i]->ppRarities[j];
					pChangedCopyRarity->pchDisplayPrefix = StructAllocString(pMessage->pcDefaultString);
					StructFreeStringSafe(&pChangedCopyRarity->Prefix_Obsolete.pEditorCopy->pcDefaultString);
				}
				if (GET_REF(pRarity->Suffix_Obsolete.hMessage))
				{
					Message* pMessage = GET_REF(pRarity->Suffix_Obsolete.hMessage);
					pChangedCopy = FixupItemGenMessages_GetOrCreateChangedCopy(pDef, pChangedCopy, &eaChangedCopies);
					pChangedCopyRarity = pChangedCopy->ppItemTiers[i]->ppRarities[j];
					pChangedCopyRarity->pchDisplaySuffix = StructAllocString(pMessage->pcDefaultString);
					StructFreeStringSafe(&pChangedCopyRarity->Suffix_Obsolete.pEditorCopy->pcDefaultString);
				}
			}
		}

		for (i = eaSize(&pDef->ppPowerData)-1; i >= 0; i--)
		{
			ItemGenPowerData* pPowerData = pDef->ppPowerData[i];
			ItemGenPowerData* pChangedCopyPowerData;

			if (GET_REF(pPowerData->Prefix_Obsolete.hMessage))
			{
				Message* pMessage = GET_REF(pPowerData->Prefix_Obsolete.hMessage);
				pChangedCopy = FixupItemGenMessages_GetOrCreateChangedCopy(pDef, pChangedCopy, &eaChangedCopies);
				pChangedCopyPowerData = pChangedCopy->ppPowerData[i];
				pChangedCopyPowerData->pchDisplayPrefix = StructAllocString(pMessage->pcDefaultString);
				StructFreeStringSafe(&pChangedCopyPowerData->Prefix_Obsolete.pEditorCopy->pcDefaultString);
			}
			if (GET_REF(pPowerData->Suffix_Obsolete.hMessage))
			{
				Message* pMessage = GET_REF(pPowerData->Suffix_Obsolete.hMessage);
				pChangedCopy = FixupItemGenMessages_GetOrCreateChangedCopy(pDef, pChangedCopy, &eaChangedCopies);
				pChangedCopyPowerData = pChangedCopy->ppPowerData[i];
				pChangedCopyPowerData->pchDisplaySuffix = StructAllocString(pMessage->pcDefaultString);
				StructFreeStringSafe(&pChangedCopyPowerData->Suffix_Obsolete.pEditorCopy->pcDefaultString);
			}

			if (GET_REF(pPowerData->itemPowerDefData.displayNameMsg_Obsolete.hMessage))
			{
				Message* pMessage = GET_REF(pPowerData->itemPowerDefData.displayNameMsg_Obsolete.hMessage);
				pChangedCopy = FixupItemGenMessages_GetOrCreateChangedCopy(pDef, pChangedCopy, &eaChangedCopies);
				pChangedCopyPowerData = pChangedCopy->ppPowerData[i];
				pChangedCopyPowerData->itemPowerDefData.pchDisplayName = StructAllocString(pMessage->pcDefaultString);
				StructFreeStringSafe(&pChangedCopyPowerData->itemPowerDefData.displayNameMsg_Obsolete.pEditorCopy->pcDefaultString);
			}
			if (GET_REF(pPowerData->itemPowerDefData.displayNameMsg2_Obsolete.hMessage))
			{
				Message* pMessage = GET_REF(pPowerData->itemPowerDefData.displayNameMsg2_Obsolete.hMessage);
				pChangedCopy = FixupItemGenMessages_GetOrCreateChangedCopy(pDef, pChangedCopy, &eaChangedCopies);
				pChangedCopyPowerData = pChangedCopy->ppPowerData[i];
				pChangedCopyPowerData->itemPowerDefData.pchDisplayName2 = StructAllocString(pMessage->pcDefaultString);
				StructFreeStringSafe(&pChangedCopyPowerData->itemPowerDefData.displayNameMsg2_Obsolete.pEditorCopy->pcDefaultString);
			}
			if (GET_REF(pPowerData->itemPowerDefData.descriptionMsg_Obsolete.hMessage))
			{
				Message* pMessage = GET_REF(pPowerData->itemPowerDefData.descriptionMsg_Obsolete.hMessage);
				pChangedCopy = FixupItemGenMessages_GetOrCreateChangedCopy(pDef, pChangedCopy, &eaChangedCopies);
				pChangedCopyPowerData = pChangedCopy->ppPowerData[i];
				pChangedCopyPowerData->itemPowerDefData.pchDescription = StructAllocString(pMessage->pcDefaultString);
				StructFreeStringSafe(&pChangedCopyPowerData->itemPowerDefData.descriptionMsg_Obsolete.pEditorCopy->pcDefaultString);
			}
		}
	}

	// Write out all affected files
	if (bModify)
	{
		const char** ppchFileList = NULL;
		Message EmptyMessage = {0};

		for (i = 0; i < eaSize(&eaChangedCopies); i++)
		{
			const char* pchFilename = eaChangedCopies[i]->pchFileName;
			if (fileIsReadOnly(pchFilename))
			{
				eaPush(&ppchFileList, pchFilename);
			}
			else if (!gimmeDLLQueryIsFileLockedByMeOrNew(pchFilename))
			{
				Errorf("Couldn't checkout file %s", pchFilename);
			}
		}
		gimmeDLLDoOperations(ppchFileList, GIMME_CHECKOUT, GIMME_QUIET_LARGE_CHECKOUT);

		for (i = 0; i < eaSize(&eaChangedCopies); i++)
		{
			ItemGenData* pItemGen = eaChangedCopies[i];
			langApplyEditorCopySingleFile(parse_ItemGenData, pItemGen, true, false);

			if (ParserWriteTextFileFromSingleDictionaryStruct(pItemGen->pchFileName, g_hItemGenDict, pItemGen, 0, 0))
				iNumSaved++;
		}

		printf("Total Files: %d\n", eaSize(&ppchFileList));
		printf("Files Saved: %d\n", iNumSaved);
		eaDestroy(&ppchFileList);
	}
	else
	{
		printf("Total Files Requiring Fixup: %d\n", eaSize(&eaChangedCopies));
	}
	eaDestroyStruct(&eaChangedCopies, parse_ItemGenData);
}

#include "cmdDataFixup_c_ast.c"
