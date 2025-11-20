/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "contact_common.h"
#include "Character.h"
#include "CharacterClass.h"
#include "Color.h"
#include "CostumeCommonEntity.h"
#include "dynFxInfo.h"
#include "EntitySavedData.h"
#include "EntityLib.h"
#include "error.h"
#include "Expression.h"
#include "ExpressionPrivate.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "gameaction_common.h"
#include "GfxEditorIncludes.h"
#include "ItemAssignments.h"
#include "Materials.h"
#include "mission_common.h"
#include "AutoGen/mission_common_h_ast.h"
#include "Player.h"
#include "powerStoreCommon.h"
#include "ResourceManager.h"
#include "SavedPetCommon.h"
#include "storeCommon.h"
#include "StringCache.h"
#include "Entity.h"
#include "StringUtil.h"
#include "AuctionBrokerCommon.h"
#include "WorldGrid.h"

#ifdef GAMESERVER
#include "gslContact.h"
#endif
#ifdef GAMECLIENT
#include "UIGen.h"
#endif


#include "../StaticWorld/group.h"

#include "contact_common_h_ast.h"
#include "contact_enums_h_ast.h"
#include "mission_enums_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_ContactDictionary = NULL;
DictionaryHandle g_HeadshotStyleDictionary = NULL;
DictionaryHandle g_PetContactListDictionary = NULL;

// Dictionary holding the dialog formatters
DictionaryHandle g_hContactDialogFormatterDefDictionary = NULL;

static ExprContext *s_pPetContactExprContext = NULL;
DefineContext *g_ExtraContactAudioPhrases;
ContactConfig g_ContactConfig = {0};

// Context for data-defined ContactIndicator enums
DefineContext* g_pContactIndicatorEnums = NULL;
static int contact_ValidateContactDialogFormatterDef(enumResourceValidateType eType, const char* pcDictName, const char* pcResourceName, ContactDialogFormatterDef* pDef, U32 userID)
{
	switch (eType)
	{
		case RESVALIDATE_CHECK_REFERENCES:
		{
			if (IsGameServerBasedType() && !isProductionMode())
			{
				if (!GET_REF(pDef->msgDialogFormat.hMessage))
				{
					ErrorFilenamef(pDef->pchFilename, "Contact dialog formatter '%s' does not have a valid dialog format message: %s",
						pDef->pchName,
						REF_STRING_FROM_HANDLE(pDef->msgDialogFormat.hMessage));
				}
			}
			return VALIDATE_HANDLED;
		}
	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void RegisterContactDialogFormatterDefDictionary(void)
{
	g_hContactDialogFormatterDefDictionary = RefSystem_RegisterSelfDefiningDictionary("ContactDialogFormatterDef", false, parse_ContactDialogFormatterDef, true, true, NULL);

	resDictManageValidation(g_hContactDialogFormatterDefDictionary, contact_ValidateContactDialogFormatterDef);

	if (IsServer()) 
	{
		resDictProvideMissingResources(g_hContactDialogFormatterDefDictionary);

		if (isDevelopmentMode() || isProductionEditMode()) 
		{
			resDictMaintainInfoIndex(g_hContactDialogFormatterDefDictionary, ".Name", ".Scope", NULL, NULL, NULL);
		}
	} 
	else 
	{
		resDictRequestMissingResources(g_hContactDialogFormatterDefDictionary, 8, false, resClientRequestSendReferentCommand);
	}
}

static void contact_ReloadContactDialogFormatterDefs(const char* pcRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading dialog formatters...");

	fileWaitForExclusiveAccess(pcRelPath);
	errorLogFileIsBeingReloaded(pcRelPath);

	ParserReloadFileToDictionary(pcRelPath, g_hContactDialogFormatterDefDictionary);

	loadend_printf(" done (%d dialog formatters)", RefSystem_GetDictionaryNumberOfReferentInfos(g_hContactDialogFormatterDefDictionary));
}

static void contact_LoadContactDialogFormatterDefs(void)
{
	// Load all dialog formatters
	resLoadResourcesFromDisk(g_hContactDialogFormatterDefDictionary, NULL, "defs/config/ContactDialogFormatters.def", "ContactDialogFormatterDef.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);

	if (isDevelopmentMode()) 
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ContactDialogFormatters.def", contact_ReloadContactDialogFormatterDefs);
	}
}

AUTO_STARTUP(ContactDialogFormatters);
void contact_AutoStartUpContactDialogFormatters(void)
{
	contact_LoadContactDialogFormatterDefs();
}

// ----------------------------------------------------------------------------------
// Contact Def Validation
// ----------------------------------------------------------------------------------

static bool contact_ValidateDialogBlock(ContactDef *def, DialogBlock ***peaBlock)
{
	int i;
	bool bResult = true;

	for(i=eaSize(peaBlock)-1; i>=0; --i)
	{
		DialogBlock *block = (*peaBlock)[i];
		if (!GET_REF(block->displayTextMesg.hMessage) &&
			REF_STRING_FROM_HANDLE(block->displayTextMesg.hMessage))
		{
			ErrorFilenamef( def->filename, "Contact references non-existent message: '%s'", REF_STRING_FROM_HANDLE(block->displayTextMesg.hMessage) );
			bResult = false;
		}
	}

	return bResult;
}

bool contact_ValidateSpecialDialogBlocks(ContactDef* def, SpecialDialogBlock ***peaBlock)
{
	int i;
	bool bResult = true;

	for(i=eaSize(peaBlock)-1; i>=0; --i)
	{
		contact_ValidateSpecialDialogBlock(def, eaGet(peaBlock,i), NULL, NULL, NULL);
	}
	return bResult;
}

static bool contact_HasValidTargetDialog(SA_PARAM_NN_VALID ContactDef *pContactDef, SA_PARAM_NN_STR const char *pchTargetDialogName, SA_PARAM_OP_STR const char* pchOwningMission, 
	SA_PARAM_NN_VALID const SpecialDialogOverride * const * const * const peaOverridesToInclude, 
	SA_PARAM_NN_VALID const MissionOfferOverride * const * const * const peaOfferOverridesToInclude)
{
	if (pContactDef && pchTargetDialogName && pchTargetDialogName[0])
	{
		// First search in special dialogs
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pContactDef->specialDialog, SpecialDialogBlock, pSpecialDialogBlock)
		{
			if (pSpecialDialogBlock->name && (stricmp(pSpecialDialogBlock->name, pchTargetDialogName) == 0))
			{
				return true;
			}
		}
		FOR_EACH_END

		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pContactDef->eaSpecialDialogOverrides, SpecialDialogBlock, pSpecialDialogBlock)
		{
			if (pSpecialDialogBlock->name && (stricmp(pSpecialDialogBlock->name, pchTargetDialogName) == 0))
			{
				return true;
			}
		}
		FOR_EACH_END

		// Search in mission offers
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pContactDef->offerList, ContactMissionOffer, pMissionOffer)
		{
			if (pMissionOffer->pchSpecialDialogName && (stricmp(pMissionOffer->pchSpecialDialogName, pchTargetDialogName) == 0))
			{
				return true;
			}
		}
		FOR_EACH_END

		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pContactDef->eaOfferOverrides, ContactMissionOffer, pMissionOffer)
		{
			if (pMissionOffer->pchSpecialDialogName && (stricmp(pMissionOffer->pchSpecialDialogName, pchTargetDialogName) == 0))
			{
				return true;
			}
		}
		FOR_EACH_END
	}

	return false;
}

bool contact_ValidateSpecialDialogBlock(ContactDef* def, SpecialDialogBlock *block, const char* pchOwningMission, SpecialDialogOverride** eaOverridesToInclude, MissionOfferOverride **eaOfferOverridesToInclude)
{
	int j;
	bool bResult = true;
	char* estrSpecialDialogName = NULL;
	MissionDef *pMissionDef = pchOwningMission ? (MissionDef *) RefSystem_ReferentFromString(g_MissionDictionary, pchOwningMission) : NULL;
	const char *pchFileName = pMissionDef ? pMissionDef->filename : def->filename;

	if(!block)
		return false;

	estrCreate(&estrSpecialDialogName);
	if(pchOwningMission)
		estrPrintf(&estrSpecialDialogName, "%s/%s", pchOwningMission, block->name);
	else
		estrCopy2(&estrSpecialDialogName, block->name);

	contact_ValidateDialogBlock(def, &block->dialogBlock);

	// Validate message
	if (!GET_REF(block->displayNameMesg.hMessage) &&
		REF_STRING_FROM_HANDLE(block->displayNameMesg.hMessage))
	{
		ErrorFilenamef(pchFileName, "Contact's special dialog references non-existent message: '%s'", REF_STRING_FROM_HANDLE(block->displayNameMesg.hMessage));
		bResult = false;
	}

	// Make sure the special dialog block has a name
	if(!estrSpecialDialogName)
	{
		ErrorFilenamef(pchFileName, "Contact has unnamed special dialog");
		bResult = false;
	}

	// Validate action blocks
	for(j=eaSize(&block->dialogActions)-1; j>=0; --j) 
	{
		SpecialDialogAction *action = block->dialogActions[j];

		// Validate action message key
		if (!GET_REF(action->displayNameMesg.hMessage) &&
			REF_STRING_FROM_HANDLE(action->displayNameMesg.hMessage))
		{
			ErrorFilenamef(pchFileName, "Contact's special dialog action references non-existent message: '%s'", REF_STRING_FROM_HANDLE(action->displayNameMesg.hMessage));
			bResult = false;
		}

#ifdef GAMESERVER
		// Validate action dialog name
		if (action->dialogName) {
			const char* nextDefName = REF_STRING_FROM_HANDLE(action->contactDef);
			ContactDef *nextDef;

			if (nextDefName) {
				nextDef = GET_REF(action->contactDef);
			} else {
				nextDef = def;
				nextDefName = def->name;
			}

			if (!nextDef)
			{
				ErrorFilenamef(pchFileName, "Contact's special dialog action references non-existent contact: %s", nextDefName);
				bResult = false;
			} 
			else if (!contact_HasValidTargetDialog(nextDef, action->dialogName, pchOwningMission, &eaOverridesToInclude, &eaOfferOverridesToInclude))
			{
				ErrorFilenamef(pchFileName, "Contact's special dialog action references non-existent dialog. Contact name: '%s', Dialog name: '%s'",
					nextDefName, action->dialogName);
				bResult = false;
			}
		}
#endif
			
		// Validate game actions
		bResult &= gameaction_ValidateActions(&action->actionBlock.eaActions, NULL, NULL, NULL, true, pchFileName);

		// Validate pet costume prefs
		if (REF_STRING_FROM_HANDLE(block->costumePrefs.costumeOverride))
		{
			if(IsServer() && !GET_REF(block->costumePrefs.costumeOverride)) {
				ErrorFilenamef(pchFileName, "Contact's special dialog block references non-existent costume: '%s'", REF_STRING_FROM_HANDLE(block->costumePrefs.costumeOverride));
				bResult = false;
			} 
			if(REF_STRING_FROM_HANDLE(block->costumePrefs.hPetOverride)) {
				ErrorFilenamef(pchFileName, "Contact's special dialog block specifies both a costume override and a pet contact list costume");
				bResult = false;
			}
			if(REF_STRING_FROM_HANDLE(block->costumePrefs.hCostumeCritterGroup) || EMPTY_TO_NULL(block->costumePrefs.pchCostumeMapVar)) {
				ErrorFilenamef(pchFileName, "Contact's special dialog block specifies both a costume override and a critter group costume");
				bResult = false;
			}
		} else if(REF_STRING_FROM_HANDLE(block->costumePrefs.hPetOverride)) {
			if(IsServer() && !GET_REF(block->costumePrefs.hPetOverride)) {
				ErrorFilenamef(pchFileName, "Contact's special dialog block references non-existent pet contact list: '%s'", REF_STRING_FROM_HANDLE(block->costumePrefs.hPetOverride));
				bResult = false;
			}
			if(REF_STRING_FROM_HANDLE(block->costumePrefs.hCostumeCritterGroup) || EMPTY_TO_NULL(block->costumePrefs.pchCostumeMapVar)) {
				ErrorFilenamef(pchFileName, "Contact's special dialog block specifies both a pet contact list costume and a critter group costume");
				bResult = false;
			}
		}
	}

	estrDestroy(&estrSpecialDialogName);

	if( !block->bUsesLocalCondExpression && block->pCondition ) {
		ErrorFilenamef( def->filename, "Contact's special dialog block has has UsesLocalCondExpression unset, but has a condition.  This condition will end up being ignored.");
		bResult = false;
	}

	return bResult;
}

bool contact_ValidateMissionOffer(ContactDef *def, ContactMissionOffer *offer, const char* pchOwningMission, SpecialDialogOverride** eaOverridesToInclude, MissionOfferOverride **eaOfferOverridesToInclude)
{
	MissionDef *pDef = GET_REF(offer->missionDef);
	MissionDef *pMissionDef = pchOwningMission ? (MissionDef *) RefSystem_ReferentFromString(g_MissionDictionary, pchOwningMission) : NULL;
	const char *pchFileName = pMissionDef ? pMissionDef->filename : def->filename;
	bool bResult = true;

	if (!GET_REF(offer->missionDef) &&
		REF_STRING_FROM_HANDLE(offer->missionDef))
	{
		ErrorFilenamef(pchFileName, "Contact references non-existent mission: '%s'", REF_STRING_FROM_HANDLE(offer->missionDef));
		bResult = false;
	}
	
	if (pDef && missiondef_GetType(pDef) != MissionType_Normal && missiondef_GetType(pDef) != MissionType_Nemesis && missiondef_GetType(pDef) != MissionType_Episode && missiondef_GetType(pDef) != MissionType_TourOfDuty)
	{
		ErrorFilenamef(pchFileName, "MissionDef has invalid type %s: '%s'", StaticDefineIntRevLookup(MissionTypeEnum, missiondef_GetType(pDef)), REF_STRING_FROM_HANDLE(offer->missionDef));
		bResult = false;
	}

	if (!GET_REF(offer->acceptStringMesg.hMessage) &&
		REF_STRING_FROM_HANDLE(offer->acceptStringMesg.hMessage))
	{
		ErrorFilenamef(pchFileName, "Contact references non-existent message: '%s'", REF_STRING_FROM_HANDLE(offer->acceptStringMesg.hMessage));
		bResult = false;
	}
	if (!GET_REF(offer->declineStringMesg.hMessage) &&
		REF_STRING_FROM_HANDLE(offer->declineStringMesg.hMessage))
	{
		ErrorFilenamef(pchFileName, "Contact references non-existent message: '%s'", REF_STRING_FROM_HANDLE(offer->declineStringMesg.hMessage));
		bResult = false;
	}

	// Missions that can be failed should have text on both the granting contact def
	// (which shows up when you restart the mission) and on the mission def (which shows
	// up in your mission journal).  If there's text in only one place, it's probably a
	// bug.
	// Don't return a fail result on this error, since it may be a bug on the mission
	// and not the contact.
	if((offer->allowGrantOrReturn==ContactMissionAllow_GrantAndReturn || offer->allowGrantOrReturn==ContactMissionAllow_GrantOnly)
			&& pDef && REF_STRING_FROM_HANDLE(pDef->failureMsg.hMessage)
			&& 0==eaSize(&offer->failureDialog))
	{
		ErrorFilenamef(pchFileName, "Contact offer for mission %s with no failure dialog, but the mission has failure text.", pDef->name);
	}
	if(pDef && !REF_STRING_FROM_HANDLE(pDef->failureMsg.hMessage) && eaSize(&offer->failureDialog))
	{
		ErrorFilenamef(pchFileName, "Contact offer for mission %s with failure dialog, but the mission has no failure text.", pDef->name);
	}

	// Validation for Sub-Mission Returns
	if (IsServer() && pDef && (offer->allowGrantOrReturn == ContactMissionAllow_SubMissionComplete)){
		MissionDef *pChildDef = missiondef_ChildDefFromName(pDef, offer->pchSubMissionName);
		if (!pChildDef){
			ErrorFilenamef(pchFileName, "Contact references non-existent mission: %s:%s", pDef->name, offer->pchSubMissionName);
			bResult = false;
		}
		if (pChildDef && !(pChildDef->meSuccessCond && pChildDef->meSuccessCond->type == MissionCondType_Expression && !stricmp(pChildDef->meSuccessCond->valStr, "0"))){
			ErrorFilenamef(pchFileName, "Contact cannot complete mission '%s:%s'.  Contacts can only complete sub-missions that have a Success Condition of '0'.", pDef->name, offer->pchSubMissionName);
			bResult = false;
		}
		if (pChildDef && !(pChildDef->doNotUncomplete)){
			ErrorFilenamef(pchFileName, "Contact cannot complete mission '%s:%s'.  Contacts can only complete sub-missions that have the 'Never Uncomplete' option set.", pDef->name, offer->pchSubMissionName);
			bResult = false;
		}
	}

	// Validation for Flashback Offers
	if (IsServer() && pDef && (offer->allowGrantOrReturn == ContactMissionAllow_FlashbackGrant)){
		// Flashback Missions must have exactly one Objective Map so that it knows which map to send you to.
		if (eaSize(&pDef->eaObjectiveMaps) == 0){
			ErrorFilenamef(pchFileName, "Contact tries to offer mission '%s' as a Flashback, but mission does not have an Objective Map!", REF_STRING_FROM_HANDLE(offer->missionDef));
			bResult = false;
		} else if (eaSize(&pDef->eaObjectiveMaps) > 1){
			ErrorFilenamef(pchFileName, "Contact tries to offer mission '%s' as a Flashback, but mission has multiple Objective Maps!", REF_STRING_FROM_HANDLE(offer->missionDef));
			bResult = false;
		}

		// Must have a fixed level, or it won't know what level to sidekick players to.
		if (pDef->levelDef.eLevelType != MissionLevelType_Specified){
			ErrorFilenamef(pchFileName, "Contact tries to offer mission '%s' as a Flashback, but mission doesn't have a specific level!", REF_STRING_FROM_HANDLE(offer->missionDef));
			bResult = false;
		}
	}

#ifdef GAMESERVER
	// Validate the target dialogs for accept, decline, reward accept and reward choose
	if (offer->pchAcceptTargetDialog && !contact_HasValidTargetDialog(def, offer->pchAcceptTargetDialog, pchOwningMission, &eaOverridesToInclude, &eaOfferOverridesToInclude))
	{
		ErrorFilenamef(pchFileName, "Mission offer for mission '%s' references non-existent dialog name '%s' for accept dialog",
			REF_STRING_FROM_HANDLE(offer->missionDef), offer->pchAcceptTargetDialog);
		bResult = false;
	}
	if (offer->pchDeclineTargetDialog && !contact_HasValidTargetDialog(def, offer->pchDeclineTargetDialog, pchOwningMission, &eaOverridesToInclude, &eaOfferOverridesToInclude))
	{
		ErrorFilenamef(pchFileName, "Mission offer for mission '%s' references non-existent dialog name '%s' for decline dialog",
			REF_STRING_FROM_HANDLE(offer->missionDef), offer->pchDeclineTargetDialog);
		bResult = false;
	}
	if (offer->pchRewardAcceptTargetDialog && !contact_HasValidTargetDialog(def, offer->pchRewardAcceptTargetDialog, pchOwningMission, &eaOverridesToInclude, &eaOfferOverridesToInclude))
	{
		ErrorFilenamef(pchFileName, "Mission offer for mission '%s' references non-existent dialog name '%s' for reward accept dialog",
			REF_STRING_FROM_HANDLE(offer->missionDef), offer->pchRewardAcceptTargetDialog);
		bResult = false;
	}
	if (offer->pchRewardChooseTargetDialog && !contact_HasValidTargetDialog(def, offer->pchRewardChooseTargetDialog, pchOwningMission, &eaOverridesToInclude, &eaOfferOverridesToInclude))
	{
		ErrorFilenamef(pchFileName, "Mission offer for mission '%s' references non-existent dialog name '%s' for reward choose dialog",
			REF_STRING_FROM_HANDLE(offer->missionDef), offer->pchRewardChooseTargetDialog);
		bResult = false;
	}
	if (offer->pchRewardAbortTargetDialog && !contact_HasValidTargetDialog(def, offer->pchRewardAbortTargetDialog, pchOwningMission, &eaOverridesToInclude, &eaOfferOverridesToInclude))
	{
		ErrorFilenamef(pchFileName, "Mission offer for mission '%s' references non-existent dialog name '%s' for reward abort dialog",
			REF_STRING_FROM_HANDLE(offer->missionDef), offer->pchRewardAbortTargetDialog);
		bResult = false;
	}
#endif

	bResult &= contact_ValidateDialogBlock(def, &offer->offerDialog);
	bResult &= contact_ValidateDialogBlock(def, &offer->inProgressDialog);
	bResult &= contact_ValidateDialogBlock(def, &offer->completedDialog);
	bResult &= contact_ValidateDialogBlock(def, &offer->failureDialog);

	return bResult;
}

bool contact_ValidateImageMenuItem(ContactDef* def, ContactImageMenuItem *pItem, const char* pchOwningMission)
{
	MissionDef *pMissionDef = pchOwningMission ? (MissionDef *)RefSystem_ReferentFromString(g_MissionDictionary, pchOwningMission) : NULL;
	const char *pchFileName = pMissionDef ? pMissionDef->filename : def->filename;
	bool bResult = true;

	if( pItem->x < 0 || pItem->x > 1 || pItem->y < 0 || pItem->y > 1 ) {
		ErrorFilenamef(pchFileName, "ImageMenuItem -- x,y must both be between 0 - 1.  x,y=(%f,%f)",
					   pItem->x, pItem->y);
		bResult = false;
	}
	if( !pItem->iconImage ) {
		ErrorFilenamef(pchFileName, "ImageMenuItem -- Item icon is not specified.");
		bResult = false;
	}
	if( pItem->action ) {
		bResult &= gameaction_ValidateActions(&pItem->action->eaActions, NULL, NULL, NULL, true, pchFileName);
	}

	return bResult;
}

static bool contact_ValidateHeadshotStyles(HeadshotStyleDef* pDef)
{
	bool bResult = true;
	
#if GAMECLIENT
	if(!resIsValidName(pDef->pchName))
	{
		ErrorFilenamef(pDef->pchFileName, "HeadshotStyleDef name is illegal: '%s'", pDef->pchName);
		bResult = false;
	}

	if (pDef->pchBackground && !texFind(pDef->pchBackground, false))
	{
		ErrorFilenamef(pDef->pchFileName, "HeadshotStyleDef (%s) Reference to non-existent texture: %s", pDef->pchName, pDef->pchBackground);
		bResult = false;
	}

	if (pDef->pchSky && !gfxSkyCheckSkyExists(pDef->pchSky))
	{
		ErrorFilenamef(pDef->pchFileName, "HeadshotStyleDef (%s) Reference to non-existent sky file: %s", pDef->pchName, pDef->pchSky);
		bResult = false;
	}

	if (pDef->pchMaterial && !materialExists(pDef->pchMaterial))
	{
		ErrorFilenamef(pDef->pchFileName, "HeadshotStyleDef (%s) Reference to non-existent material: %s", pDef->pchName, pDef->pchMaterial);
		bResult = false;
	}

	if (eaSize(&pDef->eaHeadshotFX))
	{
		int i;
		for (i = eaSize(&pDef->eaHeadshotFX)-1; i >= 0; i--)
		{
			HeadshotStyleFX* pStyleFX = pDef->eaHeadshotFX[i];
			if(!dynFxInfoExists(pStyleFX->pchFXName))
			{
				ErrorFilenamef(pDef->pchFileName, "HeadshotStyleDef (%s) Reference to non-existent FX: %s", pDef->pchName, pStyleFX->pchFXName);
				bResult = false;
			}
		}
	}

	//TODO: validate headshot frame
#endif

	return bResult;
}

bool contact_Validate(ContactDef* def)
{
	const char *pchTempFileName;
	bool bResult = true;
	ContactFlags eRemoteFlags = 0;
	int i;
	ContactMissionOffer** eaOfferList = NULL;
	SpecialDialogBlock** eaSpecialDialogs = NULL;

	if( !resIsValidName(def->name) )
	{
		ErrorFilenamef( def->filename, "Contact name is illegal: '%s'", def->name );
		bResult = false;
	}

	if( !resIsValidScope(def->scope) )
	{
		ErrorFilenamef( def->filename, "Contact scope is illegal: '%s'", def->scope );
		bResult = false;
	}

	pchTempFileName = def->filename;
	if (resFixPooledFilename(&pchTempFileName, def->scope && resIsInDirectory(def->scope, "maps/") ? NULL : "defs/contacts", def->scope, def->name, "contact")) {
		if (IsServer()) {
			char nameSpace[RESOURCE_NAME_MAX_SIZE];
			char baseObjectName[RESOURCE_NAME_MAX_SIZE];
			char baseObjectName2[RESOURCE_NAME_MAX_SIZE];
			if (!resExtractNameSpace(pchTempFileName, nameSpace, baseObjectName) || 
				!resExtractNameSpace(def->filename, nameSpace, baseObjectName2) ||
				stricmp(baseObjectName, baseObjectName2) != 0)
			{
				ErrorFilenamef( def->filename, "Contact filename does not match name '%s' scope '%s'", def->name, def->scope);
				bResult = false;
			}
		}
	}

/*	if (!GET_REF(def->infoStringMsg.hMessage) &&
		REF_STRING_FROM_HANDLE(def->infoStringMsg.hMessage))
	{
		ErrorFilenamef( def->filename, "Contact references non-existent message: '%s'", REF_STRING_FROM_HANDLE(def->infoStringMsg.hMessage) );
		bResult = false;
	}
	*/

	if (REF_STRING_FROM_HANDLE(def->costumePrefs.costumeOverride))
	{
		if(IsServer() && !GET_REF(def->costumePrefs.costumeOverride)) {
			ErrorFilenamef( def->filename, "Contact references non-existent costume: '%s'", REF_STRING_FROM_HANDLE(def->costumePrefs.costumeOverride) );
			bResult = false;
		} 
		if(REF_STRING_FROM_HANDLE(def->costumePrefs.hPetOverride)) {
			ErrorFilenamef( def->filename, "Contact specifies both a costume override and a pet contact list costume");
			bResult = false;
		}
		if(REF_STRING_FROM_HANDLE(def->costumePrefs.hCostumeCritterGroup) || EMPTY_TO_NULL(def->costumePrefs.pchCostumeMapVar)) {
			ErrorFilenamef( def->filename, "Contact specifies both a costume override and a critter group costume");
			bResult = false;
		}
	} else if(REF_STRING_FROM_HANDLE(def->costumePrefs.hPetOverride)) {
		if(IsServer() && !GET_REF(def->costumePrefs.hPetOverride)) {
			ErrorFilenamef( def->filename, "Contact references non-existent pet contact list: '%s'", REF_STRING_FROM_HANDLE(def->costumePrefs.hPetOverride) );
			bResult = false;
		}
		if(REF_STRING_FROM_HANDLE(def->costumePrefs.hCostumeCritterGroup) || EMPTY_TO_NULL(def->costumePrefs.pchCostumeMapVar)) {
			ErrorFilenamef( def->filename, "Contact specifies both a pet contact list costume and a critter group costume");
			bResult = false;
		}
	}

	// Check store references
	for(i=eaSize(&def->stores)-1; i>=0; --i)
	{
		if (!GET_REF(def->stores[i]->ref) &&
			REF_STRING_FROM_HANDLE(def->stores[i]->ref))
		{
			ErrorFilenamef( def->filename, "Contact references non-existent store: '%s'", REF_STRING_FROM_HANDLE(def->stores[i]->ref) );
			bResult = false;
		}
	}

	contact_GetMissionOfferList(def, NULL, &eaOfferList);
	for(i=eaSize(&eaOfferList)-1; i>=0; --i)
	{
		bResult &= contact_ValidateMissionOffer(def, eaOfferList[i], NULL, NULL, NULL);
	}
	if(eaOfferList)
		eaDestroy(&eaOfferList);

	// Check dialog
	bResult &= contact_ValidateDialogBlock(def, &def->generalCallout);
	bResult &= contact_ValidateDialogBlock(def, &def->missionCallout);
	bResult &= contact_ValidateDialogBlock(def, &def->rangeCallout);
	bResult &= contact_ValidateDialogBlock(def, &def->greetingDialog);
//	bResult &= contact_ValidateDialogBlock(def, &def->defaultDialog);
	bResult &= contact_ValidateDialogBlock(def, &def->infoDialog);
	bResult &= contact_ValidateDialogBlock(def, &def->missionListDialog);
	bResult &= contact_ValidateDialogBlock(def, &def->noMissionsDialog);
//	bResult &= contact_ValidateDialogBlock(def, &def->missionExitDialog);
	bResult &= contact_ValidateDialogBlock(def, &def->exitDialog);
	bResult &= contact_ValidateDialogBlock(def, &def->noStoreItemsDialog);
	bResult &= contact_ValidateSpecialDialogBlocks(def, &def->specialDialog);

	contact_GetSpecialDialogs(def, &eaSpecialDialogs);
	
	for (i=0; i < eaSize(&eaSpecialDialogs); i++) 
	{
		int j;
		const char* pchName = eaSpecialDialogs[i]->name;

		for (j=i+1; j < eaSize(&eaSpecialDialogs); j++) 
		{
			if(stricmp(eaSpecialDialogs[j]->name, pchName) == 0)
			{
				ErrorFilenamef( def->filename, "Contact has more than one special dialog named '%s'", pchName);
				bResult = false;
				break;
			}
		}

		// Also compare with the mission offers
		FOR_EACH_IN_EARRAY_FORWARDS(def->offerList, ContactMissionOffer, pMissionOffer)
		{
			if(pMissionOffer && stricmp(pMissionOffer->pchSpecialDialogName, pchName) == 0)
			{
				ErrorFilenamef( def->filename, "Contact has more than one special dialog and mission offer named '%s'", pchName);
				bResult = false;
				break;
			}
		}
		FOR_EACH_END
	}

	if( def->pImageMenuData ) {
		ContactImageMenuItem** eaItems = NULL;
		contact_GetImageMenuItems( def, &eaItems );
		for( i = 0; i < eaSize( &eaItems ); ++i ) {
			contact_ValidateImageMenuItem( def, eaItems[ i ], NULL );
		}
		eaDestroy( &eaItems );

		if( !def->pImageMenuData->backgroundImage ) {
			ErrorFilenamef( def->filename, "Contact has an ImageMenu, but the ImageMenu has no background." );
		}
	}

	// Check mission offer names for uniqueness
	for (i = 0; i < eaSize(&def->offerList) - 1; i++)
	{		
		const char* pchName = def->offerList[i] ? def->offerList[i]->pchSpecialDialogName : NULL;

		if (pchName)
		{
			S32 j;
			for (j = i + 1; j < eaSize(&def->offerList); j++)
			{
				if(stricmp(def->offerList[j]->pchSpecialDialogName, pchName) == 0)
				{
					ErrorFilenamef( def->filename, "Contact has more than one mission offer named '%s'", pchName);
					bResult = false;
					break;
				}
			}
		}
	}

	if(eaSpecialDialogs)
		eaDestroy(&eaSpecialDialogs);

//	if( (!def->defaultDialog && def->infoDialog) || (def->defaultDialog && !def->infoDialog) )
//	{
//		ErrorFilenamef( def->filename, "Contact %s has default dialog but not info dialog or vice versa", def->name );
//		bResult = false;
//	}


	// If the contact is set up to never show an options list, setting Info text is probably a mistake
	if ((def->type == ContactType_SingleDialog) && eaSize(&def->infoDialog)){
		ErrorFilenamef( def->filename, "Contact %s has an Info Dialog, but is set up to be a Single Screen contact.  The Info Dialog will never display.", def->name );
		bResult = false;
	}

	// If the contact appears in Mission Searches, make sure it has all the correct fields filled out
	if (contact_ShowInSearchResults(def)){
		if (IsServer() && !GET_REF(def->displayNameMsg.hMessage)){
			ErrorFilenamef( def->filename, "Contact %s will show in search results, but has no display name.", def->name );
			bResult = false;
		}
		if (IsServer() && !GET_REF(def->costumePrefs.costumeOverride)){
			ErrorFilenamef( def->filename, "Contact %s will show in search results, but has no costume.", def->name );
			bResult = false;
		}
		if (IsServer() && !def->pchMapName){
			ErrorFilenamef( def->filename, "Contact %s will show in search results, but has no map name.", def->name );
			bResult = false;
		}
		if (IsServer()){
			DialogBlock *pInfoDialog = eaGet(&def->infoDialog, 0);
			if (!pInfoDialog || !GET_REF(pInfoDialog->displayTextMesg.hMessage)){
				ErrorFilenamef( def->filename, "Contact %s will show in search results, but has no Info text.", def->name );
				bResult = false;
			}
		}
	}

	eRemoteFlags = contact_GenerateRemoteFlags(def);
	if(eRemoteFlags && !GET_REF(def->costumePrefs.costumeOverride) && !GET_REF(def->costumePrefs.hPetOverride) && !GET_REF(def->costumePrefs.hCostumeCritterGroup) && EMPTY_TO_NULL(def->costumePrefs.pchCostumeMapVar)) {
		ErrorFilenamef( def->filename, "Contact %s has remote capabilities, but has no default Costume Override.", def->name );
		bResult = false;
	}

	if (contact_IsMinigame(def) && def->eMinigameType == kMinigameType_None)
	{
		ErrorFilenamef(def->filename, "Contact %s is flagged as having a minigame, but a minigame type isn't set.", def->name);
		bResult = false;
	}

	if (contact_IsItemAssignmentGiver(def))
	{
		if (!def->pItemAssignmentData || !eaiSize(&def->pItemAssignmentData->peRarityCounts))
		{
			ErrorFilenamef(def->filename, "Contact %s is flagged as being an ItemAssignmentGiver, but doesn't specify a rarity count.", def->name);
			bResult = false;
		}
	}

	// Do validation for research store collections
	if (def->bIsResearchStoreCollection)
	{
		if (eaSize(&def->storeCollections)==0)
		{
			ErrorFilenamef(def->filename, "Contact %s is flagged as being a research store collection, but has no store collections.", def->name);
			bResult = false;
		}
		for (i = eaSize(&def->storeCollections)-1; i >= 0; i--)
		{
			int j;
			StoreCollection* pCollection = def->storeCollections[i];
			for (j = eaSize(&pCollection->eaStores)-1; j >= 0; j--)
			{
				StoreDef* pStore = GET_REF(pCollection->eaStores[j]->ref);
				if (pStore)
				{
					if (!store_ValidateResearchStore(pStore))
					{
						bResult = false;
					}
				}
			}
		}
	}

	// Do validation for puppet vendors
	if (contact_IsPuppetVendor(def))
	{
		if (!eaSize(&def->stores))
		{
			ErrorFilenamef(def->filename, "Contact '%s' is flagged as being a puppet vendor, but has no stores.", def->name);
			bResult = false;
		}
		else
		{
			for (i = eaSize(&def->stores)-1; i >= 0; i--)
			{
				StoreDef* pStoreDef = GET_REF(def->stores[i]->ref);
				if (!pStoreDef || !store_ValidatePuppetStore(pStoreDef))
				{
					ErrorFilenamef(def->filename, "Contact '%s' is flagged as being a puppet vendor, but has a store '%s' that sells non-puppet items.", 
						def->name, REF_STRING_FROM_HANDLE(def->stores[i]->ref));
					bResult = false;
				}
			}
		}
	}

	if (def->eSourceType == ContactSourceType_Encounter)
	{
		if (!(def->pchSourceName && def->pchSourceName[0]) || !(def->pchSourceSecondaryName && def->pchSourceSecondaryName[0]))
		{
			ErrorFilenamef(def->filename, "Contact '%s' must have both cam source 'Actor' and 'Encounter' fields specified", def->name);
			bResult = false;
		}
	}
	else if (def->eSourceType == ContactSourceType_Clicky)
	{
		if (!(def->pchSourceName && def->pchSourceName[0]))
		{
			ErrorFilenamef(def->filename, "Contact '%s' uses a camera source type 'Clicky' but does not have the source specified", def->name);
			bResult = false;
		}
	}
	else if (def->eSourceType == ContactSourceType_NamedPoint)
	{
		if (!(def->pchSourceName && def->pchSourceName[0]))
		{
			ErrorFilenamef(def->filename, "Contact '%s' uses a camera source type 'Named Point' but does not specify a named point to use as a source", def->name);
			bResult = false;
		}
	}
	else if (def->eSourceType != ContactSourceType_None)
	{
		if (!(def->pchSourceName && def->pchSourceName[0]))
		{
			ErrorFilenamef(def->filename, "Contact '%s' uses a camera source type other than 'None' but does not have the source specified", def->name);
			bResult = false;
		}
	}
	else
	{
		if (def->pchSourceName && def->pchSourceName[0])
		{
			ErrorFilenamef(def->filename, "Contact '%s' is using camera source type 'None' but still has a source field filled out.", def->name);
			bResult = false;
		}

		if (def->pchSourceSecondaryName && def->pchSourceSecondaryName[0])
		{
			ErrorFilenamef(def->filename, "Contact '%s' is using camera source type 'None' but still has the source actor name filled out", def->name);
			bResult = false;
		}
	}

	FOR_EACH_IN_CONST_EARRAY_FORWARDS(def->ppAuctionBrokerOptionList, AuctionBrokerContactData, pAuctionBrokerContactData)
	{
		if (!GET_REF(pAuctionBrokerContactData->hAuctionBrokerDef))
		{
			ErrorFilenamef(def->filename, "Contact '%s' has an auction broker option which references an invalid auction broker def '%s'.", 
				def->name,
				REF_STRING_FROM_HANDLE(pAuctionBrokerContactData->hAuctionBrokerDef));
			bResult = false;
		}
	}
	FOR_EACH_END

	//SIP TODO: validate UGC Search locations

	// Expressions
	//   interactReqs
	//   condition

#ifdef GAMESERVER
	if(bResult && eRemoteFlags && !def->bHideFromRemoteContactList) {
		contact_AddRemoteContactToServerList(def, eRemoteFlags);
	}
#endif

	return bResult;
}


// Validates all contacts
void contact_ValidateAll(void)
{
	DictionaryEArrayStruct *pContacts = resDictGetEArrayStruct(g_ContactDictionary);
	int i;

	for(i=eaSize(&pContacts->ppReferents)-1; i>=0; --i)
	{
		contact_Validate((ContactDef*)pContacts->ppReferents[i]);
	}
}


// ----------------------------------------------------------------------------------
// Contact Dictionary
// ----------------------------------------------------------------------------------

// Fixup old contact costume data
static void contact_DefFixupCostume(ContactCostume* pContactCostume)
{
	// Set "ContactCostumeType" to contacts that don't have it set properly.
	if(pContactCostume->eCostumeType == ContactCostumeType_Default) {
		if(IS_HANDLE_ACTIVE(pContactCostume->costumeOverride)) {
			pContactCostume->eCostumeType = ContactCostumeType_Specified;
		} else if(IS_HANDLE_ACTIVE(pContactCostume->hPetOverride)) {
			pContactCostume->eCostumeType = ContactCostumeType_PetContactList;
		} 
		// No need to fixup crittergroup fields as they were added after this change
	}
}

// Cleanup old data
static void contact_DefFixup(ContactDef* def)
{
	// Contact special dialogs used to be DialogBlocks, but now they're SpecialDialogBlocks
	int i, n = eaSize(&def->oldSpecialDialog);

	for(i=0; i<n; i++)
	{
		DialogBlock* oldDialog = def->oldSpecialDialog[i];
		SpecialDialogBlock* newDialog = StructCreate(parse_SpecialDialogBlock);
		DialogBlock* tempDialog = StructCreate(parse_DialogBlock);
		char buf[1024];

		StructCopyFields(parse_DialogBlock, oldDialog, tempDialog, 0, 0);
		eaPush(&newDialog->dialogBlock, tempDialog);

		sprintf(buf, "SpecialDialog%d", i+1);
		newDialog->name = (char*)allocAddString(buf);

		eaPush(&def->specialDialog, newDialog);
	}

	eaDestroyStruct(&def->oldSpecialDialog, parse_DialogBlock);

	contact_DefFixupCostume(&def->costumePrefs);
	if(def->specialDialog) {
		for(i = eaSize(&def->specialDialog)-1; i >= 0; i--) {
			contact_DefFixupCostume(&def->specialDialog[i]->costumePrefs);
		}
	}
}

static int contact_specialDialogOverrideSortCmp(const SpecialDialogOverride **pptr1, const SpecialDialogOverride **pptr2, const void *pContext)
{
	return stricmp((*pptr1)->pcContactName, (*pptr2)->pcContactName);
}

static int contact_missionOfferOverrideSortCmp(const MissionOfferOverride **pptr1, const MissionOfferOverride **pptr2, const void *pContext)
{
	return stricmp((*pptr1)->pcContactName, (*pptr2)->pcContactName);
}

static int contact_specialActionBlockOverrideSortCmp(const ActionBlockOverride **pptr1, const ActionBlockOverride **pptr2, const void *pContext)
{
	return stricmp((*pptr1)->pcContactName, (*pptr2)->pcContactName);
}

static int contact_ImageMenuItemOverrideSortCmp(const ImageMenuItemOverride **pptr1, const ImageMenuItemOverride **pptr2, const void *pContext)
{
	return stricmp((*pptr1)->pcContactName, (*pptr2)->pcContactName);
}

#ifdef GAMESERVER

void contact_MissionRemovedOrPreModifiedFixup(SA_PARAM_NN_VALID MissionDef *pMissionDef, SA_PARAM_OP_VALID ContactDef ***peaContactsToUpdate)
{
	S32 i;	
	const char *pchLastContactName = NULL;
	ContactDef *pUpdatedContactDef = NULL;
	SpecialDialogOverride **ppSpecialDialogOverrides = NULL;
	MissionOfferOverride **ppOfferOverrides = NULL;
	ActionBlockOverride **ppActionBlockOverrides = NULL;
	ImageMenuItemOverride **ppImageMenuItemOverrides = NULL;
	ContactDef **ppInternalUpdateList = NULL;

	// Clear the update list
	if (peaContactsToUpdate)
	{
		eaClear(peaContactsToUpdate);
	}
	eaIndexedEnable(&ppInternalUpdateList, parse_ContactDef);

	eaPushEArray(&ppSpecialDialogOverrides, &pMissionDef->ppSpecialDialogOverrides);
	eaStableSort(ppSpecialDialogOverrides, NULL, contact_specialDialogOverrideSortCmp);

	eaPushEArray(&ppOfferOverrides, &pMissionDef->ppMissionOfferOverrides);
	eaStableSort(ppOfferOverrides, NULL, contact_missionOfferOverrideSortCmp);

	eaPushEArray(&ppActionBlockOverrides, &pMissionDef->ppSpecialActionBlockOverrides);
	eaStableSort(ppActionBlockOverrides, NULL, contact_specialActionBlockOverrideSortCmp);

	eaPushEArray(&ppImageMenuItemOverrides, &pMissionDef->ppImageMenuItemOverrides);
	eaStableSort(ppImageMenuItemOverrides, NULL, contact_ImageMenuItemOverrideSortCmp);
	//special dialogs:
	for (i = 0; i < eaSize(&ppSpecialDialogOverrides); i++)
	{
		SpecialDialogOverride *pOverride = ppSpecialDialogOverrides[i];

		// Check if we're processing a new contact
		if (pOverride->pcContactName && pOverride->pcContactName != pchLastContactName)
		{
			ContactDef *pContactDef = (ContactDef *)RefSystem_ReferentFromString(g_ContactDictionary, pOverride->pcContactName);
			if (pContactDef)
			{
				pUpdatedContactDef = StructClone(parse_ContactDef, pContactDef);

				if (peaContactsToUpdate)
				{
					// Add to the update list
					eaIndexedAdd(peaContactsToUpdate, pUpdatedContactDef);
				}
				else
				{
					eaIndexedAdd(&ppInternalUpdateList, pUpdatedContactDef);
				}

				pchLastContactName = pOverride->pcContactName;
			}
		}

		if (pUpdatedContactDef)
		{
			// Check if the next contact is a different one
			if (i == (eaSize(&ppSpecialDialogOverrides) - 1) || ppSpecialDialogOverrides[i + 1]->pcContactName != pchLastContactName)
			{
				S32 j;
				// Remove any special dialog overrides added by this mission
				for (j = eaSize(&pUpdatedContactDef->eaSpecialDialogOverrides) - 1; j >= 0; j--)
				{
					MissionDef *pOverridingMission = GET_REF(pUpdatedContactDef->eaSpecialDialogOverrides[j]->overridingMissionDef);

					if (pOverridingMission && pOverridingMission->name == pMissionDef->name)
					{
						eaRemove(&pUpdatedContactDef->eaSpecialDialogOverrides, j);
					}							
				}
			}
		}
	}

	// Reset variables used in the loop
	pchLastContactName = NULL;
	pUpdatedContactDef = NULL;
	//mission offers:
	for (i = 0; i < eaSize(&ppOfferOverrides); i++)
	{
		MissionOfferOverride *pOverride = ppOfferOverrides[i];

		// Check if we're processing a new contact
		if (pOverride->pcContactName && pOverride->pcContactName != pchLastContactName)
		{			
			S32 iContactIndex = peaContactsToUpdate ? eaIndexedFindUsingString(peaContactsToUpdate, pOverride->pcContactName) : eaIndexedFindUsingString(&ppInternalUpdateList, pOverride->pcContactName);

			if (iContactIndex >= 0)
			{
				pUpdatedContactDef = peaContactsToUpdate ? (*peaContactsToUpdate)[iContactIndex] : ppInternalUpdateList[iContactIndex];
				pchLastContactName = pOverride->pcContactName;
			}
			else
			{
				ContactDef *pContactDef = (ContactDef *)RefSystem_ReferentFromString(g_ContactDictionary, pOverride->pcContactName);
				if (pContactDef)
				{
					pUpdatedContactDef = StructClone(parse_ContactDef, pContactDef);

					if (peaContactsToUpdate)
					{
						// Add to the update list
						eaIndexedAdd(peaContactsToUpdate, pUpdatedContactDef);
					}
					else
					{
						// Add to the internal update list
						eaIndexedAdd(&ppInternalUpdateList, pUpdatedContactDef);
					}
					pchLastContactName = pOverride->pcContactName;
				}
			}
		}

		if (pUpdatedContactDef)
		{
			// Check if the next contact is a different one
			if (i == (eaSize(&ppOfferOverrides) - 1) || ppOfferOverrides[i + 1]->pcContactName != pchLastContactName)
			{
				S32 j;

				// Remove any mission overrides added by this mission
				for (j = eaSize(&pUpdatedContactDef->eaOfferOverrides) - 1; j >= 0; j--)
				{
					MissionDef *pOverridingMission = GET_REF(pUpdatedContactDef->eaOfferOverrides[j]->overridingMissionDef);

					if (pOverridingMission && pOverridingMission->name == pMissionDef->name)
					{
						eaRemove(&pUpdatedContactDef->eaOfferOverrides, j);
					}							
				}
			}
		}
	}

	// Reset variables used in the loop
	pchLastContactName = NULL;
	pUpdatedContactDef = NULL;

	//Handle special action block overrides
	for (i = 0; i < eaSize(&ppActionBlockOverrides); i++)
	{
		ActionBlockOverride *pOverride = ppActionBlockOverrides[i];

		// Check if we're processing a new contact
		if (pOverride->pcContactName && pOverride->pcContactName != pchLastContactName)
		{			
			S32 iContactIndex = peaContactsToUpdate ? eaIndexedFindUsingString(peaContactsToUpdate, pOverride->pcContactName) : eaIndexedFindUsingString(&ppInternalUpdateList, pOverride->pcContactName);

			if (iContactIndex >= 0)
			{
				pUpdatedContactDef = peaContactsToUpdate ? (*peaContactsToUpdate)[iContactIndex] : ppInternalUpdateList[iContactIndex];
				pchLastContactName = pOverride->pcContactName;
			}
			else
			{
				ContactDef *pContactDef = (ContactDef *)RefSystem_ReferentFromString(g_ContactDictionary, pOverride->pcContactName);
				if (pContactDef)
				{
					pUpdatedContactDef = StructClone(parse_ContactDef, pContactDef);

					if (peaContactsToUpdate)
					{
						// Add to the update list
						eaIndexedAdd(peaContactsToUpdate, pUpdatedContactDef);
					}
					else
					{
						// Add to the internal update list
						eaIndexedAdd(&ppInternalUpdateList, pUpdatedContactDef);
					}
					pchLastContactName = pOverride->pcContactName;
				}
			}
		}

		if (pUpdatedContactDef)
		{
			// Check if the next contact is a different one
			if (i == (eaSize(&ppActionBlockOverrides) - 1) || ppActionBlockOverrides[i + 1]->pcContactName != pchLastContactName)
			{
				S32 j;
				// Remove any special action block overrides added by this mission
				for (j = eaSize(&pUpdatedContactDef->eaSpecialActionBlockOverrides) - 1; j >= 0; j--)
				{
					MissionDef *pOverridingMission = GET_REF(pUpdatedContactDef->eaSpecialActionBlockOverrides[j]->overridingMissionDef);

					if (pOverridingMission && pOverridingMission->name == pMissionDef->name)
					{
						eaRemove(&pUpdatedContactDef->eaSpecialActionBlockOverrides, j);
					}							
				}
			}
		}
	}
	// Reset variables used in the loop
	pchLastContactName = NULL;
	pUpdatedContactDef = NULL;

	//Handle Image Menu Item overrides
	for (i = 0; i < eaSize(&ppImageMenuItemOverrides); i++)
	{
		ImageMenuItemOverride *pOverride = ppImageMenuItemOverrides[i];

		// Check if we're processing a new contact
		if (pOverride->pcContactName && pOverride->pcContactName != pchLastContactName)
		{			
			S32 iContactIndex = peaContactsToUpdate ? eaIndexedFindUsingString(peaContactsToUpdate, pOverride->pcContactName) : eaIndexedFindUsingString(&ppInternalUpdateList, pOverride->pcContactName);

			if (iContactIndex >= 0)
			{
				pUpdatedContactDef = peaContactsToUpdate ? (*peaContactsToUpdate)[iContactIndex] : ppInternalUpdateList[iContactIndex];
				pchLastContactName = pOverride->pcContactName;
			}
			else
			{
				ContactDef *pContactDef = (ContactDef *)RefSystem_ReferentFromString(g_ContactDictionary, pOverride->pcContactName);
				if (pContactDef)
				{
					pUpdatedContactDef = StructClone(parse_ContactDef, pContactDef);

					if (peaContactsToUpdate)
					{
						// Add to the update list
						eaIndexedAdd(peaContactsToUpdate, pUpdatedContactDef);
					}
					else
					{
						// Add to the internal update list
						eaIndexedAdd(&ppInternalUpdateList, pUpdatedContactDef);
					}
					pchLastContactName = pOverride->pcContactName;
				}
			}
		}

		if (pUpdatedContactDef)
		{
			// Check if the next contact is a different one
			if (i == (eaSize(&ppImageMenuItemOverrides) - 1) 
				|| ppImageMenuItemOverrides[i + 1]->pcContactName != pchLastContactName)
			{
				S32 j;
				// Remove any image menu item overrides added by this mission
				for (j = eaSize(&pUpdatedContactDef->pImageMenuData->itemOverrides) - 1; j >= 0; j--)
				{
					MissionDef *pOverridingMission = GET_REF(
						pUpdatedContactDef->pImageMenuData->itemOverrides[j]->overridingMissionDef);

					if (pOverridingMission && pOverridingMission->name == pMissionDef->name)
					{
						eaRemove(&pUpdatedContactDef->pImageMenuData->itemOverrides, j);
					}							
				}
			}
		}
	}
	//update contacts:
	if (peaContactsToUpdate == NULL)
	{
		if (isDevelopmentMode() && eaSize(&ppInternalUpdateList) > 0)
		{
			// Set the edit mode to true for the contact dictionary
			resSetDictionaryEditMode(g_ContactDictionary, true);
		}

		// Update all contacts
		FOR_EACH_IN_EARRAY_FORWARDS(ppInternalUpdateList, ContactDef, pCurrentContactDef)
		{
			// Update the contact
			resSaveObjectInMemoryOnly(g_ContactDictionary, pCurrentContactDef, pCurrentContactDef->name);
		}
		FOR_EACH_END
	}
	
	// Destroy the temporary arrays
	eaDestroy(&ppSpecialDialogOverrides);
	eaDestroy(&ppOfferOverrides);
	eaDestroy(&ppActionBlockOverrides);
	eaDestroy(&ppImageMenuItemOverrides);
	eaDestroy(&ppInternalUpdateList);
}

/// This is one of THREE known places where Missions apply contact
/// overrides.  The three places are:
/// 
/// * contact_FixupOverrides (at Contact load)
/// * contact_MissionAddedOrPostModifiedFixup (at Mission load)
/// * contact_ApplyNamespacedMissionOfferOverride (at Namespace load)
///
/// In the normal startup case, Contact load does nothing and Mission
/// load does all the work.  Namespace load is primarily used by UGC.
///
/// I didn't write this code, I just updated it.
/// Jared F. (July/10/2012)
void contact_MissionAddedOrPostModifiedFixup(SA_PARAM_NN_VALID MissionDef *pMissionDef, SA_PARAM_OP_VALID ContactDef *** peaContactsToUpdate)
{
	S32 i;	
	const char *pchLastContactName = NULL;
	ContactDef *pUpdatedContactDef = NULL;
	SpecialDialogOverride **ppSpecialDialogOverrides = NULL;
	MissionOfferOverride **ppOfferOverrides = NULL;
	ActionBlockOverride **ppActionBlockOverrides = NULL;
	ImageMenuItemOverride **ppImageMenuItemOverrides = NULL;
	ContactDef **ppInternalUpdateList = NULL;
	bool bModifiedContact = false;

	eaIndexedEnable(&ppInternalUpdateList, parse_ContactDef);

	eaPushEArray(&ppSpecialDialogOverrides, &pMissionDef->ppSpecialDialogOverrides);
	eaStableSort(ppSpecialDialogOverrides, NULL, contact_specialDialogOverrideSortCmp);

	eaPushEArray(&ppOfferOverrides, &pMissionDef->ppMissionOfferOverrides);
	eaStableSort(ppOfferOverrides, NULL, contact_missionOfferOverrideSortCmp);

	eaPushEArray(&ppActionBlockOverrides, &pMissionDef->ppSpecialActionBlockOverrides);
	eaStableSort(ppActionBlockOverrides, NULL, contact_specialActionBlockOverrideSortCmp);

	eaPushEArray(&ppImageMenuItemOverrides, &pMissionDef->ppImageMenuItemOverrides);
	eaStableSort(ppImageMenuItemOverrides, NULL, contact_ImageMenuItemOverrideSortCmp);

	// Handle special dialog overrides
	for (i = 0; i < eaSize(&ppSpecialDialogOverrides); i++)
	{
		SpecialDialogOverride *pOverride = ppSpecialDialogOverrides[i];

		// Check if we're processing a new contact
		if (pOverride->pcContactName && pOverride->pcContactName != pchLastContactName)
		{
			S32 iIndex = peaContactsToUpdate ? eaIndexedFindUsingString(peaContactsToUpdate, pOverride->pcContactName) : eaIndexedFindUsingString(&ppInternalUpdateList, pOverride->pcContactName);
			if (iIndex >= 0)
			{
				pUpdatedContactDef = peaContactsToUpdate ? (*peaContactsToUpdate)[iIndex] : ppInternalUpdateList[iIndex];

				pchLastContactName = pOverride->pcContactName;

				// If the contact was already in the peaContactsToUpdate, it means that a premodify call updated the contact
				bModifiedContact = peaContactsToUpdate != NULL;
			}
			else
			{
				ContactDef *pContactDef = (ContactDef *)RefSystem_ReferentFromString(g_ContactDictionary, pOverride->pcContactName);
				if (pContactDef)
				{
					pUpdatedContactDef = StructClone(parse_ContactDef, pContactDef);
					pchLastContactName = pOverride->pcContactName;

					if (peaContactsToUpdate)
					{
						eaIndexedAdd(peaContactsToUpdate, pUpdatedContactDef);
					}
					else
					{
						eaIndexedAdd(&ppInternalUpdateList, pUpdatedContactDef);
					}

					bModifiedContact = false;
				}
			}
		}

		if (pUpdatedContactDef)
		{
			// Add the override dialog			
			char *estrSpecialDialogName = NULL;
			bool bDuplicate = false;

			estrStackCreate(&estrSpecialDialogName);
			estrPrintf(&estrSpecialDialogName, "%s/%s", pMissionDef->name, pOverride->pSpecialDialog->name);

			// Make sure this special dialog override does not exist in the contact
			FOR_EACH_IN_EARRAY_FORWARDS(pUpdatedContactDef->eaSpecialDialogOverrides, SpecialDialogBlock, pCurrentSpecialDialogBlock)
			{
				if (stricmp_safe(pCurrentSpecialDialogBlock->name, estrSpecialDialogName) == 0)
				{
					bDuplicate = true;
					break;
				}
			}
			FOR_EACH_END

			if (!bDuplicate)
			{
				SpecialDialogBlock *pSpecialDialogCopy = StructClone(parse_SpecialDialogBlock, pOverride->pSpecialDialog);
				pSpecialDialogCopy->name = allocAddString(estrSpecialDialogName);
				SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pSpecialDialogCopy->overridingMissionDef);
				eaPush(&pUpdatedContactDef->eaSpecialDialogOverrides, pSpecialDialogCopy);
				bModifiedContact = true;
			}

			estrDestroy(&estrSpecialDialogName);			

			// Check if the next contact is a different one
			if (i == (eaSize(&ppSpecialDialogOverrides) - 1) || ppSpecialDialogOverrides[i + 1]->pcContactName != pchLastContactName)
			{
				if (!bModifiedContact)
				{
					// Remove from the proper array
					S32 iIndex = peaContactsToUpdate ? eaIndexedFindUsingString(peaContactsToUpdate, pOverride->pcContactName) : eaIndexedFindUsingString(&ppInternalUpdateList, pOverride->pcContactName);

					devassert(iIndex >= 0);

					if (peaContactsToUpdate)
					{
						eaRemove(peaContactsToUpdate, iIndex);
					}
					else
					{
						eaRemove(&ppInternalUpdateList, iIndex);
					}

					StructDestroy(parse_ContactDef, pUpdatedContactDef);
					pUpdatedContactDef = NULL;
				}
			}
		}
	}

	// Reset variables used in the loop
	pchLastContactName = NULL;
	pUpdatedContactDef = NULL;

	// Handle mission offer overrides
	for (i = 0; i < eaSize(&ppOfferOverrides); i++)
	{
		MissionOfferOverride *pOverride = ppOfferOverrides[i];

		// Check if we're processing a new contact
		if (pOverride->pcContactName && pOverride->pcContactName != pchLastContactName)
		{
			S32 iIndex = peaContactsToUpdate ? eaIndexedFindUsingString(peaContactsToUpdate, pOverride->pcContactName) : eaIndexedFindUsingString(&ppInternalUpdateList, pOverride->pcContactName);
			if (iIndex >= 0)
			{
				pUpdatedContactDef = peaContactsToUpdate ? (*peaContactsToUpdate)[iIndex] : ppInternalUpdateList[iIndex];

				pchLastContactName = pOverride->pcContactName;

				bModifiedContact = true;
			}
			else
			{
				ContactDef *pContactDef = (ContactDef *)RefSystem_ReferentFromString(g_ContactDictionary, pOverride->pcContactName);
				if (pContactDef)
				{
					pUpdatedContactDef = StructClone(parse_ContactDef, pContactDef);
					pchLastContactName = pOverride->pcContactName;

					if (peaContactsToUpdate)
					{
						eaIndexedAdd(peaContactsToUpdate, pUpdatedContactDef);
					}
					else
					{
						eaIndexedAdd(&ppInternalUpdateList, pUpdatedContactDef);
					}

					bModifiedContact = false;
				}
			}
		}

		if (pUpdatedContactDef)
		{
			// Add the offer override
			bool bDuplicate = false;

			// Make sure this special dialog override does not exist in the contact
			FOR_EACH_IN_EARRAY_FORWARDS(pUpdatedContactDef->eaOfferOverrides, ContactMissionOffer, pCurrentMissionOffer)
			{
				// If there is a special dialog name and the name matches, this is a duplicate. Else, we need to check the mission def name instead
				if (pCurrentMissionOffer->pchSpecialDialogName || pOverride->pMissionOffer->pchSpecialDialogName)
				{
					if (stricmp_safe(pCurrentMissionOffer->pchSpecialDialogName, pOverride->pMissionOffer->pchSpecialDialogName) == 0)
					{
						bDuplicate = true;
						break;
					}
				}
				else
				{
					MissionDef *pCurrentOfferDef = GET_REF(pCurrentMissionOffer->missionDef);
					MissionDef *pOverrideOfferDef = GET_REF(pOverride->pMissionOffer->missionDef);

					if (pCurrentOfferDef && pOverrideOfferDef)
					{
						if (stricmp_safe(pCurrentOfferDef->name, pOverrideOfferDef->name) == 0)
						{
							bDuplicate = true;
							break;
						}
					}
					// This is a hack to make sure overrides that have no special dialog name and no mission def are marked
					// as duplicates. Fixes some crashes with special case contacts
					else
					{
						bDuplicate = true;
						break;
					}
				}
			}
			FOR_EACH_END

			if (!bDuplicate)
			{
				ContactMissionOffer *pMissionOfferCopy = StructClone(parse_ContactMissionOffer, pOverride->pMissionOffer);
				SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pMissionOfferCopy->overridingMissionDef);
				eaPush(&pUpdatedContactDef->eaOfferOverrides, pMissionOfferCopy);
				bModifiedContact = true;
			}

			// Check if the next contact is a different one
			if (i == (eaSize(&ppOfferOverrides) - 1) || ppOfferOverrides[i + 1]->pcContactName != pchLastContactName)
			{
				if (!bModifiedContact)
				{
					// Remove from the proper array
					S32 iIndex = peaContactsToUpdate ? eaIndexedFindUsingString(peaContactsToUpdate, pOverride->pcContactName) : eaIndexedFindUsingString(&ppInternalUpdateList, pOverride->pcContactName);

					devassert(iIndex >= 0);

					if (peaContactsToUpdate)
					{
						eaRemove(peaContactsToUpdate, iIndex);
					}
					else
					{
						eaRemove(&ppInternalUpdateList, iIndex);
					}

					StructDestroy(parse_ContactDef, pUpdatedContactDef);
					pUpdatedContactDef = NULL;
				}
			}
		}
	}

	

	// Reset variables used in the loop
	pchLastContactName = NULL;
	pUpdatedContactDef = NULL;


	// Handle special action block overrides
	for (i = 0; i < eaSize(&ppActionBlockOverrides); i++)
	{
		ActionBlockOverride *pOverride = ppActionBlockOverrides[i];

		// Check if we're processing a new contact
		if (pOverride->pcContactName && pOverride->pcContactName != pchLastContactName)
		{
			S32 iIndex = peaContactsToUpdate ? eaIndexedFindUsingString(peaContactsToUpdate, pOverride->pcContactName) : eaIndexedFindUsingString(&ppInternalUpdateList, pOverride->pcContactName);
			if (iIndex >= 0)
			{
				pUpdatedContactDef = peaContactsToUpdate ? (*peaContactsToUpdate)[iIndex] : ppInternalUpdateList[iIndex];

				pchLastContactName = pOverride->pcContactName;

				bModifiedContact = true;
			}
			else
			{
				ContactDef *pContactDef = (ContactDef *)RefSystem_ReferentFromString(g_ContactDictionary, pOverride->pcContactName);
				if (pContactDef)
				{
					pUpdatedContactDef = StructClone(parse_ContactDef, pContactDef);
					pchLastContactName = pOverride->pcContactName;

					if (peaContactsToUpdate)
					{
						eaIndexedAdd(peaContactsToUpdate, pUpdatedContactDef);
					}
					else
					{
						eaIndexedAdd(&ppInternalUpdateList, pUpdatedContactDef);
					}

					bModifiedContact = false;
				}
			}
		}

		if (pUpdatedContactDef)
		{
			// Add the override action block
			char *estrSpecialActionBlockName = NULL;
			bool bDuplicate = false;

			estrStackCreate(&estrSpecialActionBlockName);
			estrPrintf(&estrSpecialActionBlockName, "%s/%s", pMissionDef->name, pOverride->pSpecialActionBlock->name);

			// Make sure this special action block override does not exist in the contact
			FOR_EACH_IN_EARRAY_FORWARDS(pUpdatedContactDef->eaSpecialActionBlockOverrides, SpecialActionBlock, pCurrentSpecialActionBlock)
			{
				if (stricmp_safe(pCurrentSpecialActionBlock->name, estrSpecialActionBlockName) == 0)
				{
					bDuplicate = true;
					break;
				}
			}
			FOR_EACH_END

				if (!bDuplicate)
				{
					SpecialActionBlock *pSpecialActionBlockCopy = StructClone(parse_SpecialActionBlock, pOverride->pSpecialActionBlock);
					pSpecialActionBlockCopy->name = allocAddString(estrSpecialActionBlockName);
					SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pSpecialActionBlockCopy->overridingMissionDef);
					eaPush(&pUpdatedContactDef->eaSpecialActionBlockOverrides, pSpecialActionBlockCopy);
					bModifiedContact = true;
				}

				estrDestroy(&estrSpecialActionBlockName);			

				// Check if the next contact is a different one
				if (i == (eaSize(&ppActionBlockOverrides) - 1) || ppActionBlockOverrides[i + 1]->pcContactName != pchLastContactName)
				{
					if (!bModifiedContact)
					{
						// Remove from the proper array
						S32 iIndex = peaContactsToUpdate ? eaIndexedFindUsingString(peaContactsToUpdate, pOverride->pcContactName) : eaIndexedFindUsingString(&ppInternalUpdateList, pOverride->pcContactName);

						devassert(iIndex >= 0);

						if (peaContactsToUpdate)
						{
							eaRemove(peaContactsToUpdate, iIndex);
						}
						else
						{
							eaRemove(&ppInternalUpdateList, iIndex);
						}

						StructDestroy(parse_ContactDef, pUpdatedContactDef);
						pUpdatedContactDef = NULL;
					}
				}
		}
	}




	// Reset variables used in the loop
	pchLastContactName = NULL;
	pUpdatedContactDef = NULL;


	// Handle Image Menu item overrides
	for (i = 0; i < eaSize(&ppImageMenuItemOverrides); i++)
	{
		ImageMenuItemOverride *pOverride = ppImageMenuItemOverrides[i];

		// Check if we're processing a new contact
		if (pOverride->pcContactName && pOverride->pcContactName != pchLastContactName)
		{
			S32 iIndex = peaContactsToUpdate ? eaIndexedFindUsingString(peaContactsToUpdate, pOverride->pcContactName) : eaIndexedFindUsingString(&ppInternalUpdateList, pOverride->pcContactName);
			if (iIndex >= 0)
			{
				pUpdatedContactDef = peaContactsToUpdate ? (*peaContactsToUpdate)[iIndex] : ppInternalUpdateList[iIndex];

				pchLastContactName = pOverride->pcContactName;

				bModifiedContact = true;
			}
			else
			{
				ContactDef *pContactDef = (ContactDef *)RefSystem_ReferentFromString(g_ContactDictionary, pOverride->pcContactName);
				if (pContactDef)
				{
					pUpdatedContactDef = StructClone(parse_ContactDef, pContactDef);
					pchLastContactName = pOverride->pcContactName;

					if (peaContactsToUpdate)
					{
						eaIndexedAdd(peaContactsToUpdate, pUpdatedContactDef);
					}
					else
					{
						eaIndexedAdd(&ppInternalUpdateList, pUpdatedContactDef);
					}

					bModifiedContact = false;
				}
			}
		}

		if (pUpdatedContactDef && pUpdatedContactDef->pImageMenuData)
		{
			// Add the override image menu item
			bool bDuplicate = false;
			
			if (!bDuplicate)
			{
				ContactImageMenuItem *pImageMenuItemCopy = StructClone(parse_ContactImageMenuItem, pOverride->pImageMenuItem);
				SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pImageMenuItemCopy->overridingMissionDef);
				eaPush(&pUpdatedContactDef->pImageMenuData->itemOverrides, pImageMenuItemCopy);
				bModifiedContact = true;
			}
			
			// Check if the next contact is a different one
			if (i == (eaSize(&ppImageMenuItemOverrides) - 1) || ppImageMenuItemOverrides[i + 1]->pcContactName != pchLastContactName)
			{
				if (!bModifiedContact)
				{
					// Remove from the proper array
					S32 iIndex = peaContactsToUpdate ? eaIndexedFindUsingString(peaContactsToUpdate, pOverride->pcContactName) : eaIndexedFindUsingString(&ppInternalUpdateList, pOverride->pcContactName);

					devassert(iIndex >= 0);

					if (peaContactsToUpdate)
					{
						eaRemove(peaContactsToUpdate, iIndex);
					}
					else
					{
						eaRemove(&ppInternalUpdateList, iIndex);
					}

					StructDestroy(parse_ContactDef, pUpdatedContactDef);
					pUpdatedContactDef = NULL;
				}
			}
		}
	}





	if (peaContactsToUpdate)
	{
		if (isDevelopmentMode() && eaSize(peaContactsToUpdate) > 0)
		{
			// Set the edit mode to true for the contact dictionary
			resSetDictionaryEditMode(g_ContactDictionary, true);
		}

		// Save any contacts in the update list
		FOR_EACH_IN_EARRAY_FORWARDS(*peaContactsToUpdate, ContactDef, pContactDefToSave)
		{
			resSaveObjectInMemoryOnly(g_ContactDictionary, pContactDefToSave, pContactDefToSave->name);
		}
		FOR_EACH_END

			// Clear the update list
			eaClear(peaContactsToUpdate);
	}
	else
	{
		if (isDevelopmentMode() && eaSize(&ppInternalUpdateList) > 0)
		{
			// Set the edit mode to true for the contact dictionary
			resSetDictionaryEditMode(g_ContactDictionary, true);
		}

		// Save any contacts in the update list
		FOR_EACH_IN_EARRAY_FORWARDS(ppInternalUpdateList, ContactDef, pContactDefToSave)
		{
			resSaveObjectInMemoryOnly(g_ContactDictionary, pContactDefToSave, pContactDefToSave->name);
		}
		FOR_EACH_END
	}


	// Destroy the temporary arrays
	eaDestroy(&ppSpecialDialogOverrides);
	eaDestroy(&ppOfferOverrides);
	eaDestroy(&ppActionBlockOverrides);
	eaDestroy(&ppInternalUpdateList);
}

#endif

/// This is one of THREE known places where Missions apply contact
/// overrides.  The three places are:
/// 
/// * contact_FixupOverrides (at Contact load)
/// * contact_MissionAddedOrPostModifiedFixup (at Mission load)
/// * contact_ApplyNamespacedMissionOfferOverride (at Namespace load)
///
/// In the normal startup case, Contact load does nothing and Mission
/// load does all the work.  Namespace load is primarily used by UGC.
///
/// I didn't write this code, I just updated it.
/// Jared F. (July/10/2012)
static void contact_FixupOverrides(ContactDef *pContactDef)
{
	// Iterate through all missions and add any special dialog and mission offer overrides
	RefDictIterator iter;
	MissionDef *pMissionDef;

	// This should not handle any namespaced content because this data
	// will get put into shared mem.
	if( resExtractNameSpace_s( pContactDef->name, NULL, 0, NULL, 0 )) {
		return;
	}

	RefSystem_InitRefDictIterator(g_MissionDictionary, &iter);

	while (pMissionDef = (MissionDef *)RefSystem_GetNextReferentFromIterator(&iter))
	{
		// This should not handle any namespaced content because this
		// data will get put into shared mem.
		if( resExtractNameSpace_s( pMissionDef->name, NULL, 0, NULL, 0 )) {
			continue;
		}
		
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pMissionDef->ppSpecialDialogOverrides, SpecialDialogOverride, pOverride)
		{
			if (pOverride->pcContactName == pContactDef->name)
			{
				SpecialDialogBlock *pSpecialDialogCopy = StructClone(parse_SpecialDialogBlock, pOverride->pSpecialDialog);
				char *estrSpecialDialogName = NULL;
				estrStackCreate(&estrSpecialDialogName);
				estrPrintf(&estrSpecialDialogName, "%s/%s", pMissionDef->name, pOverride->pSpecialDialog->name);
				pSpecialDialogCopy->name = allocAddString(estrSpecialDialogName);
				estrDestroy(&estrSpecialDialogName);
				SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pSpecialDialogCopy->overridingMissionDef);

				eaPush(&pContactDef->eaSpecialDialogOverrides, pSpecialDialogCopy);
			}
		}
		FOR_EACH_END;

		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pMissionDef->ppSpecialActionBlockOverrides, ActionBlockOverride, pOverride)
		{
			if (pOverride->pcContactName == pContactDef->name)
			{
				SpecialActionBlock *pSpecialActionBlockCopy = StructClone(parse_SpecialActionBlock, pOverride->pSpecialActionBlock);
				char *estrSpecialActionBlockName = NULL;
				estrStackCreate(&estrSpecialActionBlockName);
				estrPrintf(&estrSpecialActionBlockName, "%s/%s", pMissionDef->name, pOverride->pSpecialActionBlock->name);
				pSpecialActionBlockCopy->name = allocAddString(estrSpecialActionBlockName);
				estrDestroy(&estrSpecialActionBlockName);
				SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pSpecialActionBlockCopy->overridingMissionDef);

				eaPush(&pContactDef->eaSpecialActionBlockOverrides, pSpecialActionBlockCopy);
			}
		}
		FOR_EACH_END;

		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pMissionDef->ppMissionOfferOverrides, MissionOfferOverride, pOverride)
		{
			if (pOverride->pcContactName == pContactDef->name)
			{
				ContactMissionOffer *pMissionOfferCopy = StructClone(parse_ContactMissionOffer, pOverride->pMissionOffer);
				SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pMissionOfferCopy->overridingMissionDef);
				eaPush(&pContactDef->eaOfferOverrides, pMissionOfferCopy);
			}
		}
		FOR_EACH_END;

		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pMissionDef->ppImageMenuItemOverrides, ImageMenuItemOverride, pOverride)
		{
			if (pOverride->pcContactName == pContactDef->name)
			{
				ContactImageMenuItem *pImageMenuCopy = StructClone(parse_ContactImageMenuItem, pOverride->pImageMenuItem);
				SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pImageMenuCopy->overridingMissionDef);

				if( !pContactDef->pImageMenuData ) {
					pContactDef->pImageMenuData = StructCreate( parse_ContactImageMenuData );
				}
				eaPush(&pContactDef->pImageMenuData->itemOverrides, pImageMenuCopy);
			}
		}
		FOR_EACH_END;
	}
}

static void
contact_GenerateStoreCollectionExpressions(ContactDef *pContact)
{
    int i;
    for ( i = eaSize(&pContact->storeCollections) - 1; i >= 0; i-- )
    {
        StoreCollection *storeCollection = pContact->storeCollections[i];
        exprGenerate(storeCollection->pCondition, store_GetBuyContext());
    }
}

static int contactResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, ContactDef *pContact, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_FIX_FILENAME:
			resFixPooledFilename((char**)&pContact->filename, pContact->scope && resIsInDirectory(pContact->scope, "maps/") ? NULL : "defs/contacts", pContact->scope, pContact->name, "contact");
			
			return VALIDATE_NOT_HANDLED;
		xcase RESVALIDATE_POST_TEXT_READING:
#ifdef GAMESERVER
			// Delete any special dialog and offer overrides in case they somehow got saved in the contact
			eaDestroyStruct(&pContact->eaSpecialDialogOverrides, parse_SpecialDialogBlock);
			eaDestroyStruct(&pContact->eaOfferOverrides, parse_ContactMissionOffer);
#endif
            contact_GenerateStoreCollectionExpressions(pContact);

			//Yenal says that fall-through here is deliberate
		case RESVALIDATE_POST_RESDB_RECEIVE:
			contact_DefFixup(pContact);

#ifdef GAMESERVER
			contact_DefPostProcess(pContact);
			return VALIDATE_HANDLED;
		xcase RESVALIDATE_POST_BINNING:
			contact_FixupOverrides(pContact);
#endif
			return VALIDATE_HANDLED;


		xcase RESVALIDATE_CHECK_REFERENCES:
			contact_Validate(pContact);
	}
	return VALIDATE_NOT_HANDLED;
}

static int contactResValidateHeadshotStylesCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, HeadshotStyleDef *pDef, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_CHECK_REFERENCES:
			contact_ValidateHeadshotStyles(pDef);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
int RegisterContactDictionary(void)
{
	g_ContactDictionary = RefSystem_RegisterSelfDefiningDictionary("Contact", false, parse_ContactDef, true, true, NULL);

	resDictManageValidation(g_ContactDictionary, contactResValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(g_ContactDictionary);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_ContactDictionary, ".name", ".scope", NULL, NULL, NULL);
		}
		
		resDictGetMissingResourceFromResourceDBIfPossible((void*)g_ContactDictionary);
	} 
	else if (IsClient())
	{
		resDictRequestMissingResources(g_ContactDictionary, 8, false, resClientRequestSendReferentCommand);
	}
	resDictProvideMissingRequiresEditMode(g_ContactDictionary);

	return 1;
}

//Currently there isn't a nice place to put this
AUTO_RUN;
int RegisterHeadshotStyleDictionary(void)
{
#ifdef GAMECLIENT
	g_HeadshotStyleDictionary = RefSystem_RegisterSelfDefiningDictionary("HeadshotStyleDef", false, parse_HeadshotStyleDef, true, true, NULL);
	resDictManageValidation(g_HeadshotStyleDictionary, contactResValidateHeadshotStylesCB);
	resDictMaintainInfoIndex(g_HeadshotStyleDictionary, ".name", NULL, ".tags", NULL, NULL);
#endif
	return 1;
}

void contact_ReloadHeadshotStyleDefs(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading HeadshotStyle Defs...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionary(pchRelPath,g_HeadshotStyleDictionary);

	loadend_printf(" done (%d HeadshotStyle Defs)", RefSystem_GetDictionaryNumberOfReferentInfos(g_HeadshotStyleDictionary));
}

AUTO_STARTUP(ContactHeadshotStyles);
void contact_LoadHeadshotStyleDefs(void)
{
	resLoadResourcesFromDisk(g_HeadshotStyleDictionary, NULL, "defs/config/HeadshotStyles.def", "HeadshotStyles.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/HeadshotStyles.def", contact_ReloadHeadshotStyleDefs);
	}
}

AUTO_STARTUP(ContactAudioPhrases);
void ContactAudioPhrases_Load(void)
{
	ContactAudioPhraseNames contactAudioPhraseNames = {0};
	int ii = 0;

	g_ExtraContactAudioPhrases = DefineCreate();

	if (g_ExtraContactAudioPhrases)
	{
		loadstart_printf("Loading Contact Audio Phrases... ");

		ParserLoadFiles(NULL, "defs/config/ContactAudioPhrases.def", "ContactAudioPhrases.bin", PARSER_OPTIONALFLAG, parse_ContactAudioPhraseNames, &contactAudioPhraseNames);

		for (ii = 0; ii < eaSize(&contactAudioPhraseNames.ppchPhrases); ii++)
			DefineAddInt(g_ExtraContactAudioPhrases, contactAudioPhraseNames.ppchPhrases[ii], 1 + ii);

		StructDeInit(parse_ContactAudioPhraseNames, &contactAudioPhraseNames);

		loadend_printf("done (%d).", ii); 
	}
}


static void Contact_LoadContactIndicatorEnumsInternal(const char *pchPath, S32 iWhen)
{
	// Create define context for the data-defined ContactIndicator enum values
	if (g_pContactIndicatorEnums)
	{
		DefineDestroy(g_pContactIndicatorEnums);
	}
	g_pContactIndicatorEnums = DefineCreate();

	// Load game-specific contact indicator enums
	DefineLoadFromFile(g_pContactIndicatorEnums, "ContactIndicator", "ContactIndicator", NULL,  "defs/config/ContactIndicatorEnums.def", "ContactIndicatorEnums.bin", ContactIndicator_FirstDataDefined);
}

static void Contact_LoadContactIndicatorEnums(void)
{
	Contact_LoadContactIndicatorEnumsInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ContactIndicatorEnums.def", Contact_LoadContactIndicatorEnumsInternal);

#ifdef GAMECLIENT
	ui_GenInitStaticDefineVars(ContactIndicatorEnum, "ContactIndicator_");
#endif
}


static void ContactConfig_LoadInternal(const char *pchPath, S32 iWhen)
{
	loadstart_printf("Loading ContactConfig... ");

	StructReset(parse_ContactConfig, &g_ContactConfig);

	ParserLoadFiles(NULL, 
		"defs/config/ContactConfig.def", 
		"ContactConfig.bin", 
		PARSER_OPTIONALFLAG, 
		parse_ContactConfig,
		&g_ContactConfig);

	loadend_printf(" done.");
}

AUTO_STARTUP(ContactConfig);
void ContactConfig_Load(void)
{
	Contact_LoadContactIndicatorEnums();
	
	ContactConfig_LoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ContactConfig.def", ContactConfig_LoadInternal);
}

int contact_LoadDefs(void)
{
	static int loadedOnce = false;

	if (IsServer() && !loadedOnce)
	{
		loadedOnce = true;
		resLoadResourcesFromDisk(g_ContactDictionary, "defs/contacts;maps", ".contact", NULL, PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY | RESOURCELOAD_USERDATA);
	}

	return 1;
}


// ----------------------------------------------------------------------------------
// Contact Utility Functions
// ----------------------------------------------------------------------------------

ContactDef* contact_DefFromName(const char* contactName)
{
	if(contactName)
		return (ContactDef*)RefSystem_ReferentFromString(g_ContactDictionary, contactName);
	return NULL;
}

HeadshotStyleDef* contact_HeadshotStyleDefFromName(const char* pchHeadshotStyleName)
{
#ifdef GAMECLIENT
	if(pchHeadshotStyleName && pchHeadshotStyleName[0])
	{
		return (HeadshotStyleDef*)RefSystem_ReferentFromString(g_HeadshotStyleDictionary, pchHeadshotStyleName);
	}
#endif
	return NULL;
}

//Use hardcoded FOV values for now
F32 contact_HeadshotStyleDefGetFOV(HeadshotStyleDef* pStyle, F32 fFallbackFOV)
{
	F32 fFOVy = -1;
	if (pStyle)
	{
		switch (pStyle->eFOV)
		{
			xcase kHeadshotStyleFOV_Default:
				fFOVy = 55;
			xcase kHeadshotStyleFOV_Fisheye:
				fFOVy = 120;
			xcase kHeadshotStyleFOV_Telephoto:
				fFOVy = 20;
			xcase kHeadshotStyleFOV_Portrait:
				fFOVy = 40;
		}
	}
	else
	{
		fFOVy = fFallbackFOV;
	}
	return fFOVy;
}

Color contact_HeadshotStyleDefGetBackgroundColor(HeadshotStyleDef* pStyle)
{
	return colorFromRGBA(pStyle ? pStyle->uiBackgroundColor : 0xFFFFFFFF);
}

void contact_HeadshotStyleDefSetAnimBits(DynBitFieldGroup* pBitFieldGroup, HeadshotStyleDef* pStyle, const char* pchFallbackAnimBits)
{
	if( pBitFieldGroup )
	{
		dynBitFieldGroupAddBits(pBitFieldGroup, pStyle ? pStyle->pchAnimBits : pchFallbackAnimBits, 0 );
	}
}

const char* contact_HeadshotStyleDefGetFrame(HeadshotStyleDef* pStyle)
{
	return pStyle ? pStyle->pchFrame : "";
}

const char* contact_HeadshotStyleDefGetSky(HeadshotStyleDef* pStyle)
{
	return pStyle ? pStyle->pchSky : "";
}

bool contact_CanApplySpecialDialogOverride(const char *pchMissionName, const char *pchFilename, ContactDef *pContactDef, SpecialDialogBlock *pDialog, bool bSilent)
{
	char pchSpecialDialogName[MAX_PATH];

	if (!pDialog) {
		if (!bSilent) {
			ErrorFilenamef(pchFilename, "Special Dialog block is NULL in Special Dialog override for mission '%s' to contact '%s' ", pchMissionName, SAFE_MEMBER(pContactDef, name));
		}
		return false;
	}

	// Check that the contact exists
	if (!pContactDef) {
		if (!bSilent) {
			ErrorFilenamef(pchFilename, "Special Dialog override for mission '%s' attempting to override non-existent contact", pchMissionName);
		}
		return false;
	}

	sprintf(pchSpecialDialogName, "%s/%s", pchMissionName, pDialog->name);

	// Check to see if the dialog already exists
	if(contact_SpecialDialogFromName(pContactDef, pchSpecialDialogName))
	{
		if (!bSilent) {
			ErrorFilenamef(pchFilename, "Mission '%s' is attempting to add a Special Dialog named '%s' to contact '%s', but one already exists under that name.", pchMissionName, pchSpecialDialogName, pContactDef->name);
		}
		return false;
	}

	if (!bSilent) {
		devassertmsgf(!pDialog->iSortOrder, "A dynamically added mission cannot use the sort order field in the special dialog blocks. Please see the contact_GetSpecialDialogs function. Special dialog name: %s", pDialog->name);
	}
	return true;
}

bool contact_CanApplyMissionOfferOverride(const char *pchMissionName, const char *pchFilename, ContactDef *pContactDef, ContactMissionOffer *pOffer, bool bSilent)
{
	MissionDef *pMissionDef = NULL;

	if(!pOffer) {
		if (!bSilent) {
			ErrorFilenamef(pchFilename, "Mission Offer is NULL in Mission Offer override for mission '%s' to contact '%s' ", pchMissionName, SAFE_MEMBER(pContactDef, name));
		}
		return false;
	}

	// Check that the contact exists
	if (!pContactDef) {
		if (!bSilent) {
			ErrorFilenamef(pchFilename, "Mission Offer override for mission '%s' attempting to override non-existent contact", pchMissionName);
		}
		return false;
	}

	pMissionDef = GET_REF(pOffer->missionDef);

	// Check to see if the dialog already exists
	if (pMissionDef) {
		ContactMissionOffer** eaOfferList = NULL;
		ContactMissionOffer* pExistingOffer = NULL;
		int i, j, k;

		contact_GetMissionOfferList(pContactDef, NULL, &eaOfferList);

		for (i = eaSize(&eaOfferList)-1; i >= 0; i--) {
			ContactMissionOffer* pMissionOffer = eaOfferList[i];
			if (contact_MissionOfferCheckMissionDef(pMissionOffer, pMissionDef)) {
				for (j = eaSize(&pMissionOffer->eaRequiredAllegiances)-1; j >= 0; j--) {
					AllegianceDef* pAllegianceDef = GET_REF(pMissionOffer->eaRequiredAllegiances[j]->hDef);
					if (pAllegianceDef) {
						for (k = eaSize(&pOffer->eaRequiredAllegiances)-1; k >= 0; k--) {
							if (pAllegianceDef == GET_REF(pOffer->eaRequiredAllegiances[k]->hDef)) {
								break;
							}
						}
						if (k >= 0) {
							break;
						}
					}
				}
				if (j >= 0) {
					pExistingOffer = pMissionOffer;
					break;
				}
			}
		}

		if (eaOfferList) {
			eaDestroy(&eaOfferList);
		}

		if (pExistingOffer) {
			if (!bSilent) {
				ErrorFilenamef(pchFilename, "Mission '%s' is attempting to add a Mission Offer for '%s' to contact '%s', but one already exists under that name or allegiance requirements.", pchMissionName, REF_STRING_FROM_HANDLE(pExistingOffer->missionDef), pContactDef->name);
			}
			return false;
		}
	}
	return true;
}

bool contact_CanApplyImageMenuItemOverride(const char *pchMissionName, const char *pchFilename, ContactDef *pContactDef, ContactImageMenuItem *pItem, bool bSilent)
{
	//char pchSpecialDialogName[MAX_PATH];
	
	if(!pItem) {
		if (!bSilent) {
			ErrorFilenamef(pchFilename, "ImageMenuItem is NULL in override for mission '%s' to contact '%s' ", pchMissionName, SAFE_MEMBER(pContactDef, name));
		}
		return false;
	}

	// Check that the contact exists
	if (!pContactDef) {
		if (!bSilent) {
			ErrorFilenamef(pchFilename, "ImageMenuItem override for mission '%s' attempting to override non-existent contact", pchMissionName);
		}
		return false;
	}

	return true;
}

bool contact_MissionOfferCheckMissionDef(ContactMissionOffer* pOffer, MissionDef* pMissionDef)
{
	MissionDef* pCurrDef = GET_REF(pOffer->missionDef);
	if (pOffer->pchSubMissionName)
		pCurrDef = missiondef_ChildDefFromName(pCurrDef, pOffer->pchSubMissionName);
	if (pCurrDef && (pCurrDef == pMissionDef)) 
	{
		return true;
	}
	return false;
}

int contact_FindMissionOfferInList(ContactMissionOffer** eaOfferList, MissionDef* pMissionDef)
{
	int i, n = eaSize(&eaOfferList);
	for (i = 0; i < n; i++)
	{
		if (contact_MissionOfferCheckMissionDef(eaOfferList[i], pMissionDef))
		{
			return i;
		}
	}
	return -1;
}

ContactMissionOffer* contact_GetMissionOffer(ContactDef* contact, Entity* pEnt, MissionDef* missionDef)
{
	ContactMissionOffer** eaOfferList = NULL;
	ContactMissionOffer* pFound = NULL;
	int iFoundIdx;

	PERFINFO_AUTO_START_FUNC();

	contact_GetMissionOfferList(contact, pEnt, &eaOfferList);
	iFoundIdx = contact_FindMissionOfferInList(eaOfferList, missionDef);
	if (iFoundIdx >= 0)
		pFound = eaOfferList[iFoundIdx];

	if(eaOfferList)
		eaDestroy(&eaOfferList);

	PERFINFO_AUTO_STOP();
	return pFound;
}

static bool contact_IsValidMissionOffer(Entity* pEnt, ContactMissionOffer* pMissionOffer)
{
	if (pMissionOffer) {
		if (pEnt && eaSize(&pMissionOffer->eaRequiredAllegiances)) {
			int i;
			for (i = eaSize(&pMissionOffer->eaRequiredAllegiances)-1; i >= 0; i--) {
				AllegianceDef* pAllegianceDef = GET_REF(pMissionOffer->eaRequiredAllegiances[i]->hDef);
				if (pAllegianceDef && (pAllegianceDef == GET_REF(pEnt->hAllegiance) || pAllegianceDef == GET_REF(pEnt->hSubAllegiance))) {
					return true;
				}
			}
		} else {
			return true;
		}
	}
	return false;
}

void contact_GetMissionOfferList(ContactDef* pContact, Entity* pEnt, ContactMissionOffer*** peaReturnList)
{
	PERFINFO_AUTO_START_FUNC();

	if(pContact && peaReturnList)
	{
		int i;

		// Add normal offers
		for (i = eaSize(&pContact->offerList)-1; i >= 0; i--) {
			if (contact_IsValidMissionOffer(pEnt, pContact->offerList[i])) {
				eaPush(peaReturnList, pContact->offerList[i]);
			}
		}
		// Add override offers
		for (i = eaSize(&pContact->eaOfferOverrides)-1; i >= 0; i--) {
			if (contact_IsValidMissionOffer(pEnt, pContact->eaOfferOverrides[i])) {
				eaPush(peaReturnList, pContact->eaOfferOverrides[i]);
			}
		}
#ifdef GAMESERVER
		{
			// Add namespaced override offers
			MissionOfferOverrideData** eaNamespacedOverrides = contact_GetNamespacedMissionOfferOverrideList(pContact);
			for (i=0; i < eaSize(&eaNamespacedOverrides); i++) {
				if (contact_IsValidMissionOffer(pEnt, eaNamespacedOverrides[i]->pMissionOffer)) {
					eaPush(peaReturnList, eaNamespacedOverrides[i]->pMissionOffer);
				}
			}
		}
#endif
	}

	PERFINFO_AUTO_STOP();
}

static S32 contact_SpecialDialogSortComparator(const SpecialDialogBlock **ppDialog1, const SpecialDialogBlock **ppDialog2, const void *pContext)
{
	const SpecialDialogBlock *pDialog1 = ppDialog1 ? *ppDialog1 : NULL;
	const SpecialDialogBlock *pDialog2 = ppDialog2 ? *ppDialog2 : NULL;

	if (pDialog1 == NULL && ppDialog2 == NULL)
	{
		return 0;
	}
	else if (pDialog1 == NULL)
	{
		return 1;
	}
	else if (pDialog2 == NULL)
	{
		return -1;
	}
	else
	{
		S32 iSortOrder1 = pDialog1->iSortOrder;
		S32 iSortOrder2 = pDialog2->iSortOrder;

		// Fix up sort order for a default value of 0
		if (iSortOrder1 == 0)
		{
			iSortOrder1 = INT_MAX;
		}
		if (iSortOrder2 == 0)
		{
			iSortOrder2 = INT_MAX;
		}

		if (iSortOrder1 != iSortOrder2)
		{
			// We can easily compare dialogs that has different priorities
			return iSortOrder1 - iSortOrder2;
		}
		else
		{
			MissionDef *pMissionDef1 = GET_REF(pDialog1->overridingMissionDef);
			MissionDef *pMissionDef2 = GET_REF(pDialog2->overridingMissionDef);
			const char *pchMissionName1 = pMissionDef1 && pMissionDef1->name ? pMissionDef1->name : "";
			const char *pchMissionName2 = pMissionDef2 && pMissionDef2->name ? pMissionDef2->name : "";
			return strcmp(pchMissionName1, pchMissionName2);
		}
	}
}

void contact_GetSpecialDialogsEx(ContactDef* pContact, SpecialDialogBlock*** peaReturnList, bool* pbListSorted)
{
	PERFINFO_AUTO_START_FUNC();

	if(pContact && peaReturnList)
	{
		static SpecialDialogBlock **eaSortedList = NULL;
		bool bHasOverrides = false;
		bool bHasSortOrder = false;
		int i, iDialogCount = eaSize(&pContact->specialDialog);
		
		eaClearFast(&eaSortedList);

		// Add all dialogs from the contact first
		for (i = 0; i < iDialogCount; i++)
		{
			SpecialDialogBlock* pDialog = pContact->specialDialog[i];
			bHasSortOrder = (bHasSortOrder || pDialog->iSortOrder != 0);
			eaPush(&eaSortedList, pDialog);
		}
		// Add dialog overrides
		if (eaSize(&pContact->eaSpecialDialogOverrides))
		{
			eaPushEArray(&eaSortedList, &pContact->eaSpecialDialogOverrides);
			bHasOverrides = true;
		}
		// Check to see if the list should be sorted. 
		// Doing a stable sort in this case wouldn't change the list, but the caller occasionally needs to know if it has been sorted.
		// This function purposely pushes all namespace overrides after the sort
		if (bHasSortOrder || bHasOverrides)
		{
			if (pbListSorted)
			{
				(*pbListSorted) = true;
			}
			// Sort the array
			eaStableSort(eaSortedList, NULL, contact_SpecialDialogSortComparator);
		}

#ifdef GAMESERVER
		{
			// Get all namepaced overrides for this contact
			SpecialDialogOverrideData** eaNamespacedOverrides = contact_GetNamespacedSpecialDialogOverrideList(pContact);
			if(eaNamespacedOverrides) 
			{
				FOR_EACH_IN_EARRAY_FORWARDS(eaNamespacedOverrides, SpecialDialogOverrideData, pOverride)
				{
					if (pOverride && pOverride->pSpecialDialogBlock)
					{
						eaPush(&eaSortedList, pOverride->pSpecialDialogBlock);
					}
				}
				FOR_EACH_END
			}
		}
#endif
		// Return the results
		eaPushEArray(peaReturnList, &eaSortedList);
	}

	PERFINFO_AUTO_STOP();
}

void contact_GetImageMenuItems(ContactDef* pContact, ContactImageMenuItem*** peaReturnList)
{
	PERFINFO_AUTO_START_FUNC();

	if(pContact && pContact->pImageMenuData && peaReturnList)
	{
		eaPushEArray(peaReturnList, &pContact->pImageMenuData->items);
		eaPushEArray(peaReturnList, &pContact->pImageMenuData->itemOverrides);
		
#ifdef GAMESERVER
		{
			// Get all namepaced overrides for this contact
			ImageMenuItemOverrideData** eaNamespacedOverrides = contact_GetNamespacedImageMenuItemOverrideList(pContact);
			if(eaNamespacedOverrides) 
			{
				FOR_EACH_IN_EARRAY_FORWARDS(eaNamespacedOverrides, ImageMenuItemOverrideData, pOverride)
				{
					if (pOverride && pOverride->pItem)
					{
						eaPush(peaReturnList, pOverride->pItem);
					}
				}
				FOR_EACH_END;
			}
		}
#endif
	}

	PERFINFO_AUTO_STOP();
}

ContactMissionOffer * contact_MissionOfferFromSpecialDialogName(ContactDef* pContactDef, const char* pchSpecialDialogName)
{
	if (pContactDef)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pContactDef->offerList, ContactMissionOffer, pMissionOffer)
		{
			if (pMissionOffer && stricmp(pMissionOffer->pchSpecialDialogName, pchSpecialDialogName) == 0)
				return pMissionOffer;
		}
		FOR_EACH_END

		FOR_EACH_IN_EARRAY_FORWARDS(pContactDef->eaOfferOverrides, ContactMissionOffer, pMissionOffer)
		{
			if (pMissionOffer && stricmp(pMissionOffer->pchSpecialDialogName, pchSpecialDialogName) == 0)
				return pMissionOffer;
		}
		FOR_EACH_END

#ifdef GAMESERVER
		{
			MissionOfferOverrideData** eaNamespacedOverrides = contact_GetNamespacedMissionOfferOverrideList(pContactDef);

			if(eaNamespacedOverrides) 
			{
				S32 i;
				for (i = 0; i < eaSize(&eaNamespacedOverrides); i++) 
				{
					if (eaNamespacedOverrides[i] &&
						eaNamespacedOverrides[i]->pMissionOffer &&
						stricmp(eaNamespacedOverrides[i]->pMissionOffer->pchSpecialDialogName, pchSpecialDialogName) == 0)
					{
						return eaNamespacedOverrides[i]->pMissionOffer;
					}
				}
			}
		}
#endif

	}
	return NULL;
}

SpecialDialogBlock* contact_SpecialDialogFromName(ContactDef* contact, const char* name)
{
	int i, n = eaSize(&contact->specialDialog);

	for (i = 0; i < n; i++)
	{
		SpecialDialogBlock* specialDialog = contact->specialDialog[i];
		if (specialDialog->name && stricmp(name, specialDialog->name) == 0) 
		{
			return specialDialog;
		}
	}

	n = eaSize(&contact->eaSpecialDialogOverrides);

	for (i = 0; i < n; i++)
	{
		SpecialDialogBlock* specialDialog = contact->eaSpecialDialogOverrides[i];
		if (specialDialog->name && stricmp(name, specialDialog->name) == 0) 
		{
			return specialDialog;
		}
	}

#ifdef GAMESERVER
	return contact_SpecialDialogOverrideFromName(contact, name);
#else
	return NULL;	
#endif
}

SpecialActionBlock* contact_SpecialActionBlockFromName(ContactDef* contact, const char* name)
{
	int i, n = eaSize(&contact->specialActions);

	for (i = 0; i < n; i++)
	{
		SpecialActionBlock* specialActionBlock = contact->specialActions[i];
		if (specialActionBlock->name && stricmp(name, specialActionBlock->name) == 0) 
		{
			return specialActionBlock;
		}
	}
	//TODO Fix for overrides
	n = eaSize(&contact->eaSpecialActionBlockOverrides);

	for (i = 0; i < n; i++)
	{
		SpecialActionBlock* specialActionBlock = contact->eaSpecialActionBlockOverrides[i];
		if (specialActionBlock->name && stricmp(name, specialActionBlock->name) == 0) 
		{
			return specialActionBlock;
		}
	}

#ifdef GAMESERVER
	return contact_SpecialActionBlockOverrideFromName(contact, name);
	return NULL;
#else
	return NULL;	
#endif
}

ContactImageMenuItem* contact_ImageMenuItemFromName(ContactDef* contact, int index)
{
	ContactImageMenuItem** eaItems = NULL;
	ContactImageMenuItem* pReturn;
	
	contact_GetImageMenuItems(contact, &eaItems);
	pReturn = eaGet( &eaItems, index );
	eaDestroy( &eaItems );

	return pReturn;
}

//Returns the number of actions in a special dialog block including any that are appended from another block
int contact_getNumberOfSpecialDialogActions(ContactDef *pContactDef, SpecialDialogBlock *pDialog) {
	int numActions = 0;
	SpecialActionBlock *pSpecialActionBlock = NULL;

	if(pDialog) {

		if(pDialog->dialogActions) {
			numActions += eaSize(&pDialog->dialogActions); //These are the regular actions in the special dialog block
		}
		if(pDialog->pchAppendName){
			pSpecialActionBlock = contact_SpecialActionBlockFromName(pContactDef, pDialog->pchAppendName);
			if(pSpecialActionBlock == NULL) {
				//pSpecialDialogToAppend = contact_SpecialDialogOverrideFromName(pContactDef, pDialog->pchAppendName);
			}
			//pSpecialDialogToAppend = contact_SpecialDialogFromName(pContactDef, "War_Wizard_Start");
		}
		if(pSpecialActionBlock) {

			if(pSpecialActionBlock->dialogActions) {
				numActions += eaSize(&pSpecialActionBlock->dialogActions); //These are the actions that we are appending from another block
			}

		}
	}

	return numActions;
}

//Returns the special dialog action from a block's actions array and any actions that are appended from another block
SpecialDialogAction * contact_getSpecialDialogActionByIndex( ContactDef *pContactDef, SpecialDialogBlock *pDialog, int iActionIndex )
{
	int numRegularActions = 0;
	int numAppendedActions = 0;

	SpecialActionBlock *pSpecialActionBlockToAppend = NULL;

	if(pDialog == NULL)
		return NULL;

	if(pDialog && pDialog->dialogActions) {
		numRegularActions = eaSize(&pDialog->dialogActions);

		if(iActionIndex < 0) {
			return NULL;
		} else if(iActionIndex < numRegularActions) {
			return pDialog->dialogActions[iActionIndex];
		}
	}

	if(pDialog->pchAppendName){
		pSpecialActionBlockToAppend = contact_SpecialActionBlockFromName(pContactDef, pDialog->pchAppendName);
		//pSpecialDialogToAppend = contact_SpecialDialogFromName(pContactDef, "War_Wizard_Start");
	}

	if(pSpecialActionBlockToAppend && pSpecialActionBlockToAppend->dialogActions) {
		numAppendedActions = eaSize(&pSpecialActionBlockToAppend->dialogActions);

		//Indexes that go above the number of regular actions should refer to appended actions
		iActionIndex -= numRegularActions;

		if(iActionIndex < numAppendedActions && iActionIndex >= 0) {
			//return pSpecialActionBlockToAppend->dialogActions[iActionIndex];
			return eaGet(&pSpecialActionBlockToAppend->dialogActions, iActionIndex);
		}
	}

	return NULL;
}

bool contact_HasSpecialDialog(SA_PARAM_NN_VALID ContactDef* contact, SA_PARAM_NN_STR const char* dialog_name)
{
	return contact_SpecialDialogFromName(contact, dialog_name) != NULL;
}

// Currently, a Contact is considered a "Single Screen" Contact if they don't have any
// Options List Text, or if they have a "SingleDialog" flag set.  We may wish to revisit
// this and make the logic more consistent.
bool contact_IsSingleScreen(ContactDef *pContactDef)
{
	if (pContactDef->type == ContactType_SingleDialog){
		return true;
	} else if (	!eaSize(&pContactDef->missionListDialog) 
		&& !eaSize(&pContactDef->noMissionsDialog) 
		&& !eaSize(&pContactDef->missionExitDialog) 
		&& !eaSize(&pContactDef->exitDialog)){
			return true;
	}
	return false;
}

bool contact_IsNearSharedBank(SA_PARAM_OP_VALID Entity *pEnt)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	ContactDef *pContactDef = pContactDialog ? GET_REF(pContactDialog->hContactDef) : NULL;
	if (!pEnt)
		return false;
	return (pContactDef && contact_IsSharedBank(pContactDef)) || (pContactDialog && pContactDialog->screenType == ContactScreenType_SharedBank) || (entGetAccessLevel(pEnt) >= ACCESS_DEBUG);
}

bool contact_IsNearMailBox(SA_PARAM_OP_VALID Entity *pEnt)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	ContactDef *pContactDef = pContactDialog ? GET_REF(pContactDialog->hContactDef) : NULL;
	if (!pEnt)
		return false;
	return (pContactDef && contact_IsMailbox(pContactDef)) || (pContactDialog && pContactDialog->screenType == ContactScreenType_MailBox) || (entGetAccessLevel(pEnt) >= ACCESS_DEBUG);
}

bool contact_IsNearRespec(Entity *pEnt)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	ContactDef *pContactDef = pContactDialog ? GET_REF(pContactDialog->hContactDef) : NULL;
	if (!pEnt)
		return false;
	return (pContactDef && contact_IsRespec(pContactDef)) || (pContactDialog && pContactDialog->screenType == ContactScreenType_Respec) || (entGetAccessLevel(pEnt) >= ACCESS_DEBUG);
}

// Whether this contact is a tailor
bool contact_IsTailor(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_Tailor )!=0);
}
// Whether this contact is a starship tailor
bool contact_IsStarshipTailor(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_StarshipTailor )!=0);
}
 // Whether this contact allows you to change your active starship
bool contact_IsStarshipChooser(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_StarshipChooser )!=0);
}
// Whether this contact is a weapon tailor
bool contact_IsWeaponTailor(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_WeaponTailor )!=0);
}
 // Whether this contact is a nemesis liason
bool contact_IsNemesis(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_Nemesis )!=0);
}
// Whether this contact is a guild registrar
bool contact_IsGuild(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_Guild )!=0);
}
// Whether this contact performs respecs
bool contact_IsRespec(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_Respec )!=0);
}
// Whether this Contact is a Powers Trainer
bool contact_IsPowersTrainer(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_PowersTrainer )!=0);
}
// Whether this contact is a banker
bool contact_IsBank(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_Bank )!=0);
}
// Whether this contact is a shared banker
bool contact_IsSharedBank(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_SharedBank )!=0);
}
// Whether this contact is a guild banker
bool contact_IsGuildBank(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_GuildBank )!=0);
}
// Whether this contact is a "Crime Computer" that helps the player search for more missions
bool contact_IsMissionSearch(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_MissionSearch )!=0);
}
// Whether this contact is included in Mission Search results
bool contact_ShowInSearchResults(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_ShowInSearchResults )!=0);
}
// Whether this contact is a user marketplace/auction
bool contact_IsMarket(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_Market )!=0);
}
// Whether this contact is an auction broker
bool contact_IsAuctionBroker(ContactDef* pContactDef)
{
	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pContactDef->ppAuctionBrokerOptionList, AuctionBrokerContactData, pAuctionBrokerData)
	{
		if (GET_REF(pAuctionBrokerData->hAuctionBrokerDef))
		{
			return true;
		}
	}
	FOR_EACH_END

		return false;
}
// Whether this contact is a UGC Search Agent

bool contact_IsUGCSearchAgent(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_UGCSearchAgent )!=0);
}

// Whether this contact opens the Zen Store

bool contact_IsZStore(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_ZStore )!=0);
}

// Whether this contact is a mailbox, where you can send/receive items
bool contact_IsMailbox(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_MailBox )!=0);
}

bool contact_IsReplayMissionGiver(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_ReplayMissionGiver)!=0);
}

bool contact_IsMinigame(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_Minigame)!=0);
}

bool contact_IsImageMenu(ContactDef* pContactDef)
{
	return (( pContactDef->eContactFlags & ContactFlag_ImageMenu)!=0);
}

// Whether or not this contact exclusively sells puppets
bool contact_IsPuppetVendor(ContactDef* pContactDef)
{
	return ((pContactDef->eContactFlags & ContactFlag_PuppetVendor)!=0);
}

// Whether or not this contact provides item assignments
bool contact_IsItemAssignmentGiver(ContactDef* pContactDef)
{
	return ((pContactDef->eContactFlags & ContactFlag_ItemAssignmentGiver)!=0);
}

// Determines if the name of a = the name of b
int remoteContact_CompareNames(const RemoteContact* a, const RemoteContact* b) {
	return (strcmp(a->pchContactDef, b->pchContactDef) == 0);
}

// Compares two remote contacts based on newness, then by priority, then by name
int remoteContact_Compare(const RemoteContact** a, const RemoteContact** b)
{
	if(!(*a) || !(*b)) {
		return 0;
	}

	if((*a)->bIsNew == (*b)->bIsNew) {
		if((*a)->iPriority == (*b)->iPriority) {
			const char* aName = langTranslateMessage(locGetLanguage(getCurrentLocale()), GET_REF((*a)->hDisplayNameMsg));
			const char* bName = langTranslateMessage(locGetLanguage(getCurrentLocale()), GET_REF((*b)->hDisplayNameMsg));
			if(!aName && !bName) {
				return 0;
			} else if(!aName && bName) {
				return -1;
			} else if(aName && !bName) {
				return 1;
			} else {
				return stricmp(aName, bName);
			}
		} else {
			return ((*b)->iPriority - (*a)->iPriority);
		}
	} else {
		return ((*b)->bIsNew - (*a)->bIsNew);
	}
}

ContactFlags contact_GenerateRemoteFlags(SA_PARAM_NN_VALID ContactDef* pContact)
{
	int i;
	bool bGrant = false;
	bool bRemoteGrant = false;
	bool bReturn = false;
	bool bRemoteReturn = false;
	ContactMissionOffer **ppOfferList = NULL;
	SpecialDialogBlock **eaSpecialDialogs = NULL;
	ContactFlags eFlags = 0;

	if(!pContact) {
		return 0;
	}

	contact_GetMissionOfferList(pContact, NULL, &ppOfferList);
	contact_GetSpecialDialogs(pContact, &eaSpecialDialogs);

	//Are there any grant/return missions?
	for(i = eaSize(&ppOfferList) - 1; i >= 0 && (!bGrant || !bReturn || !bRemoteGrant || !bRemoteReturn); i--) {
		if(ppOfferList[i]->allowGrantOrReturn == ContactMissionAllow_GrantAndReturn) {
			bGrant = true;
			bReturn = true;
		} else if(ppOfferList[i]->allowGrantOrReturn == ContactMissionAllow_GrantOnly) {
			bGrant = true;
		} else if(ppOfferList[i]->allowGrantOrReturn == ContactMissionAllow_ReturnOnly) {
			bReturn = true;
		}
		bRemoteGrant = bRemoteGrant || !!(ppOfferList[i]->eRemoteFlags & ContactMissionRemoteFlag_Grant);
		bRemoteReturn = bRemoteReturn || !!(ppOfferList[i]->eRemoteFlags & ContactMissionRemoteFlag_Return);
	}

	if(pContact->canAccessRemotely
			|| (g_ContactConfig.bIncludeMissionSearchResultContactsInRemoteContacts && contact_ShowInSearchResults(pContact))) {
		//Mark the appropriate flags
		if(bGrant) {
			eFlags = (eFlags | ContactFlag_RemoteOfferGrant);
		} else {
			eFlags = (eFlags & ~ContactFlag_RemoteOfferGrant);
		}
		if(bReturn) {
			eFlags = (eFlags | ContactFlag_RemoteOfferReturn);
		} else {
			eFlags = (eFlags & ~ContactFlag_RemoteOfferReturn);
		}
		//Check for special dialogs
		if(eaSpecialDialogs && eaSize(&eaSpecialDialogs)) {
			eFlags = (eFlags | ContactFlag_RemoteSpecDialog);
		} else {
			eFlags = (eFlags & ~ContactFlag_RemoteSpecDialog);
		}
	} else {
		eFlags = (eFlags & ~ContactFlag_RemoteSpecDialog);
		if(bRemoteGrant) {
			eFlags = (eFlags | ContactFlag_RemoteOfferGrant);
		} else {
			eFlags = (eFlags & ~ContactFlag_RemoteOfferGrant);
		}
		if(bRemoteReturn) {
			eFlags = (eFlags | ContactFlag_RemoteOfferReturn);
		} else {
			eFlags = (eFlags & ~ContactFlag_RemoteOfferReturn);
		}
	}

	if(ppOfferList)
		eaDestroy(&ppOfferList);
	if(eaSpecialDialogs)
		eaDestroy(&eaSpecialDialogs);

	return eFlags;
}

///////////////////////////////////////
// Pet Contact Lists
///////////////////////////////////////

bool PetContactList_Validate(PetContactList* def)
{
	bool bResult = true;
	int i;

	//Name
	if( !resIsValidName(def->pchName) )
	{
		ErrorFilenamef(def->pchFilename, "Pet Contact List name is illegal: '%s'", def->pchName );
		bResult = false;
	}
	//PetContact References
	for(i = eaSize(&def->ppNodes)-1; def->ppNodes && i >=0; i--) {
		PetContact* pNode = def->ppNodes[i];
		//Check name/type
		if(pNode->eType == PetContactType_AlwaysPropSlot) {
			if(IsServer()) {
				REF_TO(AlwaysPropSlotDef) hPropSlotDef;
				SET_HANDLE_FROM_STRING("AlwaysPropSlotDef", pNode->pchName, hPropSlotDef);
				if(!IS_HANDLE_ACTIVE(hPropSlotDef)) {
					ErrorFilenamef(def->pchFilename, "Invalid AlwaysPropSlot for PetContact: '%s' in PetContactList: %s", pNode->pchName, def->pchName );
					bResult = false;
				}
				REMOVE_HANDLE(hPropSlotDef);
			}
		} else if(pNode->eType == PetContactType_Class) {
			if(IsServer()) {
				REF_TO(CharacterClass) hClass;
				SET_HANDLE_FROM_STRING("CharacterClass", pNode->pchName, hClass);
				if(!IS_HANDLE_ACTIVE(hClass)) {
					ErrorFilenamef(def->pchFilename, "Invalid Class for PetContact: '%s' in PetContactList: %s", pNode->pchName, def->pchName );
					bResult = false;
				}
				REMOVE_HANDLE(hClass);
			}
		} else if(pNode->eType == PetContactType_Officer) {

		} else if(pNode->eType == PetContactType_AllPets) {
		
		} else {
			ErrorFilenamef(def->pchFilename, "Invalid type for PetContact: '%s' in PetContactList: %s", pNode->pchName, def->pchName );
			bResult = false;
		}
	}
	//Default critter def
	if(IS_HANDLE_ACTIVE(def->hDefaultCritter)) {
		if(IsServer()) {
			CritterDef* pCritterDef = GET_REF(def->hDefaultCritter);
			if(!pCritterDef) {
				ErrorFilenamef(def->pchFilename, "Invalid Default CritterDef for PetContactList: %s", def->pchName );
				bResult = false;
			}
		}
	} else {
		ErrorFilenamef(def->pchFilename, "Pet Contact List must have a default CritterDef");
		bResult = false;
	}

	return bResult;
}

static int PetContactList_ResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PetContactList *pDef, U32 userID)
{
	switch (eType)
	{
		case RESVALIDATE_CHECK_REFERENCES:
			PetContactList_Validate(pDef);
			return VALIDATE_HANDLED;
		case RESVALIDATE_POST_BINNING:
			PetContactList_PostProcess(pDef);
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void PetContactList_RegisterDictionary( void )
{
	g_PetContactListDictionary = RefSystem_RegisterSelfDefiningDictionary("PetContactList", false, parse_PetContactList, true, true, NULL);

	resDictManageValidation( g_PetContactListDictionary, PetContactList_ResValidateCB );

	if( IsServer() ) {
		resDictProvideMissingResources( g_PetContactListDictionary);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex( g_PetContactListDictionary, ".Name", NULL, ".Tags", NULL, NULL );
		}
	} else if (IsClient()) {
		resDictRequestMissingResources( g_PetContactListDictionary, 8, false, resClientRequestSendReferentCommand );
	}
	resDictProvideMissingRequiresEditMode(g_PetContactListDictionary);
}

void PetContactList_ReloadDef_CB(const char *pchRelPath, int when)
{
	loadstart_printf("Reloading PetContactLists...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionary(pchRelPath,g_PetContactListDictionary);

	loadend_printf(" done.");
}


AUTO_STARTUP(PetContactLists) ASTRT_DEPS(PetStore, EntityCostumes);
void PetContactList_LoadDefs(void)
{
	if (IsGameServerBasedType()) {
		loadstart_printf("Loading PetContactList Defs...");

		resLoadResourcesFromDisk(g_PetContactListDictionary, "defs/petcontactlists", ".petcontact", "PetContactList.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

		if (isDevelopmentMode())
		{
			// Have reload take effect immediately
			FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/petcontactlists", PetContactList_ReloadDef_CB);
		}

		loadend_printf(" done (%d PetContactList).",RefSystem_GetDictionaryNumberOfReferents(g_PetContactListDictionary));
	}
}

static int PetContactList_CompareEnts(const Entity** a, const Entity** b) {
	int ret = 0;

	if(!(*a) || !(*b)) {
		return 0;
	}

	//Hardcoded STO bridge officer station sort
	if((*a)->pSaved && (*b)->pSaved && (*a)->pSaved->conOwner.containerID && (*b)->pSaved->conOwner.containerID && ((*a)->pSaved->conOwner.containerID == (*b)->pSaved->conOwner.containerID) ) {
		Entity* pOwner = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, (*a)->pSaved->conOwner.containerID);
		if(pOwner) {
			CONST_EARRAY_OF(AlwaysPropSlot) ppAlwaysPropSlots = SAFE_MEMBER2(pOwner, pSaved, ppAlwaysPropSlots);
			if(ppAlwaysPropSlots) {
				int aStation = 0;
				int bStation = 0;
				int i;
				for(i = 0; i < eaSize(&ppAlwaysPropSlots) && (!aStation || !bStation); i++) {
					AlwaysPropSlot* pSlot = ppAlwaysPropSlots[i];
					if(pSlot->iPetID > 0){
						if(pSlot->iPetID == (*a)->pSaved->iPetID) {
							AlwaysPropSlotDef* pPropA = GET_REF(pSlot->hDef);
							aStation = pPropA->iImportanceRank;
						}
						else if(pSlot->iPetID == (*b)->pSaved->iPetID) {
							AlwaysPropSlotDef* pPropB = GET_REF(pSlot->hDef);
							bStation = pPropB->iImportanceRank;
						}
					}
					ret = bStation - aStation;
				}
			}
		}
	}

	//Hardcoded STO bridge officer skill points spent sort
	if(ret == 0) {
		ret = inv_GetNumericItemValue((*b), "OfficerSkillPointsSpent") - inv_GetNumericItemValue((*a), "OfficerSkillPointsSpent");
	}

	//Container ID sort (creation time)
	if(ret == 0) {
		ret = (*a)->myContainerID - (*b)->myContainerID;
	}

	return ret;
}

static void PetContactList_BuildExprContext(void)
{
	if(!s_pPetContactExprContext)
	{
		ExprFuncTable* contactFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(contactFuncTable, "util");
		exprContextAddFuncsToTableByTag(contactFuncTable, "encounter_action");
		exprContextAddFuncsToTableByTag(contactFuncTable, "player");
		exprContextAddFuncsToTableByTag(contactFuncTable, "Contact");
		exprContextAddFuncsToTableByTag(contactFuncTable, "PetContact");

		s_pPetContactExprContext = exprContextCreate();
		exprContextSetFuncTable(s_pPetContactExprContext, contactFuncTable);

		exprContextSetAllowRuntimeSelfPtr(s_pPetContactExprContext);
		exprContextSetAllowRuntimePartition(s_pPetContactExprContext);

		exprContextSetPointerVarPooled(s_pPetContactExprContext, g_PlayerVarName, NULL, parse_Entity, true, true);
	}
}

int PetContactList_PostProcess(PetContactList* def)
{
	int i;

	PetContactList_BuildExprContext();

	for(i = 0; def->ppNodes && i < eaSize(&def->ppNodes); i++) {
		PetContact* pNode = def->ppNodes[i];
		if(pNode->exprCondition)
			exprGenerate(pNode->exprCondition, s_pPetContactExprContext);
	}

	return 1;
}

int PetContactList_Evaluate(SA_PARAM_OP_VALID Entity* pEntPlayer, SA_PARAM_NN_VALID Entity* pEntPet, Expression* pExpr, WorldScope* pScope)
{
	MultiVal mvResultVal;

	// It's probably safe to assume this already exists, but we'll leave it anyway
	PetContactList_BuildExprContext();

	exprContextSetSelfPtr(s_pPetContactExprContext, pEntPet);
	exprContextSetPartition(s_pPetContactExprContext, entGetPartitionIdx(pEntPlayer));
	exprContextSetScope(s_pPetContactExprContext, pScope);

	// Add pEntPlayer to the context as "Player"
	if (pEntPlayer && entGetPlayer(pEntPlayer))
		exprContextSetPointerVarPooled(s_pPetContactExprContext, g_PlayerVarName, pEntPlayer, parse_Entity, true, true);
	else
		exprContextRemoveVarPooled(s_pPetContactExprContext, g_PlayerVarName);

	exprEvaluate(pExpr, s_pPetContactExprContext, &mvResultVal);
	return MultiValGetInt(&mvResultVal, NULL);
} 

static int petContactListNameEq( const char* name1, const char* name2 )
{
	return stricmp( name1, name2 ) == 0;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Pet Contact List processing

// Find the first valid pet on the pet contact list.  Return its entity.
static Entity* petContactList_GetEnt(Entity* pEntPlayer, PetContactList* pList, const char*** peaExcludeList)
{
	int i, j;

	///  WOLF[22Nov11] The peaExcludeList is used specifically to prevent duplicate bridge officers from
	// appearing on player bridges. In the encounter spawn, the name of the officer is added to a global
	// list so that across a set of encounters, a particular name will not be reused. The exclude
	// list has been made part of the partition structure and so is destroyed and reset when the partition
	// is destroyed

	if(!pList || !pList->ppNodes || !pEntPlayer || !pEntPlayer->pSaved) {
		return NULL;
	}
	
	//Go through each pet in order
	for(i=0; i < eaSize(&pList->ppNodes); i++) {
		PetContact* pNode = pList->ppNodes[i];

		//Search for the pet
		if(pNode->eType == PetContactType_AlwaysPropSlot && pEntPlayer->pSaved->ppAlwaysPropSlots) {
			//Find a matching AlwaysPropSlot
			for(j=0;j<eaSize(&pEntPlayer->pSaved->ppAlwaysPropSlots);j++) {
				AlwaysPropSlot* pSlot = pEntPlayer->pSaved->ppAlwaysPropSlots[j];
				AlwaysPropSlotDef* pSlotDef = pSlot ? GET_REF(pSlot->hDef) : NULL;
				if(pSlotDef && !stricmp(pSlotDef->pchName, pNode->pchName)) {
					//Get the pet occupying it, if any
					if(pSlot->iPetID) {
						Entity* pEntPet = SavedPet_GetEntityFromPetID(pEntPlayer, pSlot->iPetID);
						if(pEntPet && pEntPet->pSaved && (!peaExcludeList || eaFindCmp(peaExcludeList, pEntPet->pSaved->savedName, petContactListNameEq) == -1) ) {
							//Evaluate the condition
							if(!pNode->exprCondition || (pNode->exprCondition && PetContactList_Evaluate(pEntPlayer, pEntPet, pNode->exprCondition, NULL))) {
								return pEntPet;
							}
						}
					}
				}
			}
		} else if(pNode->eType == PetContactType_Class && pEntPlayer->pSaved->ppOwnedContainers) {
			Entity** eaMatches = NULL;
			int numMatches = 0;
			eaCreate(&eaMatches);
			//Find all pets of the specified class
			for(j=0;j<eaSize(&pEntPlayer->pSaved->ppOwnedContainers); j++) {
				PetRelationship *pRel = pEntPlayer->pSaved->ppOwnedContainers[j];
				Entity* pPet = pRel ? GET_REF(pRel->hPetRef) : NULL;
				Character* pPetChar = pPet ? pPet->pChar : NULL;
				CharacterClass* pPetClass = pPetChar ? GET_REF(pPetChar->hClass) : NULL;
				if(pPetClass && !stricmp(pPetClass->pchName, pNode->pchName) && pPet->pSaved && (!peaExcludeList || eaFindCmp(peaExcludeList, pPet->pSaved->savedName, petContactListNameEq) == -1)) {
					//Check the condition
					if(!pNode->exprCondition || (pNode->exprCondition && PetContactList_Evaluate(pEntPlayer, pPet, pNode->exprCondition, NULL))) {
						eaPush(&eaMatches, pPet);
					}
				}
			}
			//Sort the findings and return the first one in the array
			numMatches = eaSize(&eaMatches);
			if(numMatches) {
				Entity* pPet;
				if(numMatches > 1) {
					eaQSort(eaMatches, PetContactList_CompareEnts);
				}
				pPet = eaMatches[0];
				eaDestroy(&eaMatches);
				return pPet;
			}
			eaDestroy(&eaMatches);
		} else if(pNode->eType == PetContactType_AllPets) {
			Entity** eaMatches = NULL;
			int numMatches = 0;

			//Find all the player's pets
			eaCreate(&eaMatches);
			for(j = 0; j < eaSize(&pEntPlayer->pSaved->ppOwnedContainers); j++) {
				PetRelationship *pRel = pEntPlayer->pSaved->ppOwnedContainers[j];
				if ( pRel && !SavedPet_IsPetAPuppet( pEntPlayer, pRel ) ) {
					Entity* pPet = GET_REF(pRel->hPetRef);
					if(pPet && pPet->pSaved && (!pNode->exprCondition || (pNode->exprCondition && PetContactList_Evaluate(pEntPlayer, pPet, pNode->exprCondition, NULL)) ) ) {
						if((!peaExcludeList || eaFindCmp(peaExcludeList, pPet->pSaved->savedName, petContactListNameEq) == -1))
							eaPush(&eaMatches, pPet);
					}
				}
			}

			//Sort the findings and return the first one in the array
			numMatches = eaSize(&eaMatches);
			if(numMatches) {
				Entity* pPet;
				if(numMatches > 1) {
					eaQSort(eaMatches, PetContactList_CompareEnts);
				}
				pPet = eaMatches[0];
				eaDestroy(&eaMatches);
				return pPet;
			}
			eaDestroy(&eaMatches);
		} else if(pNode->eType == PetContactType_Officer) {
			// Not yet implemented
		}
	}

	return NULL;
}



// Get a random default costume from the list of costumes in the critter def specified by the pet contact list
static CritterCostume* petContactList_GetDefaultCostume(SA_PARAM_NN_VALID PetContactList* pList, Entity* pPlayerEnt, const char*** peaExcludeList)
{
	int i, iRand;
	CritterDef* pDef = GET_REF(pList->hDefaultCritter);
	int offset = 0;

	if(!pDef || !pDef->ppCostume || !eaSize(&pDef->ppCostume)) {
		return NULL;
	}

	if(pPlayerEnt) {
		//base the index off the player and list name. Don't base it off a particular part of a name since since the names can
		//  be very similar. Klingon_Eng, Klingon_Sci for example. As it turns out, for STO for Klingons, there are 27 costumes
		//  we are moduloing over, and "Science", "Tactical", and "Engineering" sum to one off of each other mod 27. This is not
		//  optimal, so throw in the arbitrary subtract 32 to get a better distribution. This is sort of horrible and sort of not.
		int iAsciiSum=0;
		int iChar;
		int iStrLen = (int)(strlen(pList->pchName));
		for (iChar=0;iChar<iStrLen;iChar++)
		{
			if (pList->pchName[iChar]>=32)
			{
				iAsciiSum+=(pList->pchName[iChar]-32);
			}
		}
		iRand = ( pPlayerEnt->myContainerID + iAsciiSum ) % eaSize(&pDef->ppCostume);
	} else {
		//randomize the index
		iRand = randInt(eaSize(&pDef->ppCostume)-1);
	}

	if(peaExcludeList && *(peaExcludeList)) {
		bool match = false;
		do {
			CritterCostume* pCostume = pDef->ppCostume[(iRand + offset)%eaSize(&pDef->ppCostume)];
			match = false;
			for(i = eaSize(peaExcludeList)-1; i >=0 && !match; i--) {
				if(!stricmp((*peaExcludeList)[i], TranslateDisplayMessage(pCostume->displayNameMsg))) {
					match = true;
					offset++;
				}
			}
		} while(match && offset < eaSize(&pDef->ppCostume));
	}

	if(offset >= eaSize(&pDef->ppCostume)) {
		return NULL;
	} else {
		return pDef->ppCostume[(iRand + offset)%eaSize(&pDef->ppCostume)];
	}
}


static PetContactList* petContactList_FixupList(Entity* pEntPlayer, PetContactList* pList)
{
	///  WOLF[29May12] Now for some truly horrific hacky code. But barring redoing the contactList system
	// and fixing up all sorts of mission data, probably the cleanest solution. Design has pretty freely
	// intermixed usage of the klingon and non-klingon petContactLists with the expectation that 'it does
	// the right thing'. As it turns out, it doesn't do the right thing at all. The worst case being
	// that a headshot can be requested via the klingon version and the associated display name via the
	// non-klingon version. Yay. To that end, we are putting a very hard-coded list switch here.
	//   If we get a non-klingon list and we are of the klingon faction, then switch to the corresponding
	// Klingon list. Horrible. But it should fix up a lot of the various bugs we're getting with the system.
	// In a better world, we'd start over with the PetContacts and redo all the data. Not likely.

	AllegianceDef *pAllegiance;		
	
	pAllegiance = GET_REF(pEntPlayer->hAllegiance);
	if (pAllegiance && pAllegiance->pcName && stricmp(pAllegiance->pcName, "Allegiance_Klingon") == 0)
	{
		// We are Klingon
		if (strnicmp(pList->pchName, "Klingon_", 8)!=0)
		{
			// We don't have the proper prefix

			REF_TO(PetContactList) hList;
			char *pNewListName = NULL;

			// Prepend "Klingon_" to whatever we already have. This presupposes the file names match properly
			//  which they did at the time this code was written. New allegiances and such will cause havoc
			estrStackCreate(&pNewListName);
			estrCopy2(&pNewListName,"Klingon_");
			estrAppend2(&pNewListName,pList->pchName);

			SET_HANDLE_FROM_STRING("PetContactList", pNewListName, hList);

			if(IS_HANDLE_ACTIVE(hList))
			{
				// Repoint the pList to the alternate list

				pList = GET_REF(hList);
			}
			REMOVE_HANDLE(hList);
			estrDestroy(&pNewListName);
		}
	}
	// Okay. Done with that. Back to our regularly scheduled codepath.

	return(pList);
}


// Process the PetContactList for the given player entity.
//		If we find an actual pet, return it in pPetEntity
//		If we can't, fill out the CritterDef and CritterCostume fields based on any fallbacks
//		If the excludeList is not NULL, skip any pet's that have names that are in the list. (Used for Bridge Encounter setup)
void PetContactList_GetPetOrCostume(Entity* pEntPlayer, PetContactList* pList, const char*** peaExcludeList, Entity** ppPetEntity, CritterDef** ppCritterDef,
									CritterCostume** ppCostume)
{
	PetContactList *pListActuallyUsed=NULL;
	Entity* pFoundPet=NULL;
	
	if (ppPetEntity!=NULL)	*ppPetEntity=NULL;
	if (ppCritterDef!=NULL)	*ppCritterDef=NULL;
	if (ppCostume!=NULL)	*ppCostume=NULL;

	if(!pList || !pList->ppNodes || !pList->pchName)
	{
		return;
	}
	
	if (pEntPlayer==NULL)
	{
		// This is a special case particularly used for encounter generation. In case we are trying to generate something where
		//  there is no player for some reason (I actually don't know why that might happen, but there was a code path for it
		//  in gslEncounter). We need to use the default fall-through costume to pick something random. Without a player,
		//  we can't do anything fancy like allegiance checking, so just call the costume look up and hope. It won't work very
		//  well for Klingon's.

		if (ppCostume!=NULL)
		{
			*ppCostume = petContactList_GetDefaultCostume(pList, NULL, peaExcludeList);
		}
		return;
	}

	if(!pEntPlayer->pSaved)
	{
		// Not sure what this indicates, but I'm preserving old code paths.
		return;
	}

	pListActuallyUsed = petContactList_FixupList(pEntPlayer, pList);

	pFoundPet=petContactList_GetEnt(pEntPlayer, pListActuallyUsed, peaExcludeList);
	if (pFoundPet)
	{
		// We found a pet in the list. That's all we need, so just return
		if (ppPetEntity!=NULL)	*ppPetEntity=pFoundPet;
		return;
	}

	// No actual pet was found, we are falling back on the default

	if (ppCritterDef != NULL)
	{
		*ppCritterDef = GET_REF(pListActuallyUsed->hDefaultCritter);
	}

	if (ppCostume!=NULL)
	{
		*ppCostume = petContactList_GetDefaultCostume(pListActuallyUsed, pEntPlayer, peaExcludeList);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Checks to see if the current pet (saved as the selfPtr) is on the specified player's away team
AUTO_EXPR_FUNC(PetContact) ACMD_NAME(IsPetOnAwayTeam);
int PetContactList_exprIsPetInAwayTeam(ExprContext *pContext, SA_PARAM_NN_VALID Entity* pEntPlayer)
{
	Entity* pEntPet = pContext->selfPtr;
	if(pEntPet && pEntPet->myEntityType == GLOBALTYPE_ENTITYSAVEDPET) {
		INT_EARRAY ppAwayTeam = SAFE_MEMBER2(pEntPlayer, pSaved, ppAwayTeamPetID);
		if(ppAwayTeam)
			return -1!=eaiFind(&ppAwayTeam, pEntPet->myContainerID);
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen, gameutil) ACMD_NAME("GetCurrentMap");
const char* exprGetCurrentMap(ExprContext *pContext)
{
	return zmapInfoGetPublicName(NULL);
}

ExprFuncTable* contact_CreateExprFuncTable(void)
{
	static ExprFuncTable* s_contactFuncTable = NULL;
	if(!s_contactFuncTable)
	{
		s_contactFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_contactFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_contactFuncTable, "encounter_action");
		exprContextAddFuncsToTableByTag(s_contactFuncTable, "player");
		exprContextAddFuncsToTableByTag(s_contactFuncTable, "Contact");
		exprContextAddFuncsToTableByTag(s_contactFuncTable, "gameutil");
		exprContextAddFuncsToTableByTag(s_contactFuncTable, "entity");
		exprContextAddFuncsToTableByTag(s_contactFuncTable, "entityutil");
	}
	return s_contactFuncTable;
}


void contact_SpecialDialogPostProcess(SpecialDialogBlock* pDialog, const char* pchFilename)
{
	if(IsServer())
	{
		static ExprContext* exprContext = NULL;
		int n,k;

		if (!exprContext)
		{
			exprContext = exprContextCreate();
			exprContextSetFuncTable(exprContext, contact_CreateExprFuncTable());
			exprContextSetAllowRuntimePartition(exprContext);
			exprContextSetAllowRuntimeSelfPtr(exprContext);
		}

		if(pDialog->pCondition)
		{
			exprGenerate(pDialog->pCondition, exprContext);
		}

		for(n = eaSize(&pDialog->dialogBlock) - 1; n >= 0; --n)
		{
			if (pDialog->dialogBlock[n]->condition)
			{
				exprGenerate(pDialog->dialogBlock[n]->condition, exprContext);
			}
		}
		for(k=eaSize(&pDialog->dialogActions)-1; k>=0; --k)
		{
			if(pDialog->dialogActions[k]->condition)
			{
				exprGenerate(pDialog->dialogActions[k]->condition, exprContext);
			}

			if(pDialog->dialogActions[k]->canChooseCondition)
			{
				exprGenerate(pDialog->dialogActions[k]->canChooseCondition, exprContext);
			}

			// Generate game actions
			gameaction_GenerateActions(&pDialog->dialogActions[k]->actionBlock.eaActions, NULL, pchFilename);
		}
	}
}

void contact_SpecialActionPostProcess(SpecialActionBlock* pAction, const char* pchFilename)
{
	if(IsServer())
	{
		static ExprContext* exprContext = NULL;
		int i;

		if (!exprContext)
		{
			exprContext = exprContextCreate();
			exprContextSetFuncTable(exprContext, contact_CreateExprFuncTable());
			exprContextSetAllowRuntimePartition(exprContext);
			exprContextSetAllowRuntimeSelfPtr(exprContext);
		}

		for(i=eaSize(&pAction->dialogActions)-1; i>=0; --i)
		{
			if(pAction->dialogActions[i]->condition)
			{
				exprGenerate(pAction->dialogActions[i]->condition, exprContext);
			}

			if(pAction->dialogActions[i]->canChooseCondition)
			{
				exprGenerate(pAction->dialogActions[i]->canChooseCondition, exprContext);
			}

			// Generate game actions
			gameaction_GenerateActions(&pAction->dialogActions[i]->actionBlock.eaActions, NULL, pchFilename);
		}
	}
}


void contact_ImageMenuItemPostProcess(ContactImageMenuItem* pItem, const char* pchFilename)
{
	if(IsServer()){
		static ExprContext* exprContext = NULL;

		if (!exprContext){
			exprContext = exprContextCreate();
			exprContextSetFuncTable(exprContext, contact_CreateExprFuncTable());
			exprContextSetAllowRuntimePartition(exprContext);
			exprContextSetAllowRuntimeSelfPtr(exprContext);
		}

		// Generate conditionss:
		if (pItem->visibleCondition){
			exprGenerate(pItem->visibleCondition, exprContext);
		}
		if (pItem->requiresCondition){
			exprGenerate(pItem->requiresCondition, exprContext);
		}
		if (pItem->recommendedCondition){
			exprGenerate(pItem->recommendedCondition, exprContext);
		}
		// Generate game actions:
		if( pItem->action && pItem->action->eaActions ){
			gameaction_GenerateActions(&pItem->action->eaActions, NULL, pchFilename);
		}
	}
}

static bool contact_GetAudioAssets_HandleContactImageMenuData		(const ContactImageMenuData			*pContactImageMenuData,			const char ***peaStrings);
static bool contact_GetAudioAssets_HandleContactMissionOffers		(const ContactMissionOffer			**ppContactMissionOffers,		const char ***peaStrings);
static bool contact_GetAudioAssets_HandleEndDialogAudios			(const EndDialogAudio				**ppEndDialogAudios,			const char ***peaStrings);
static bool contact_GetAudioAssets_HandleDialogBlocks				(const DialogBlock					**eaDialogBlocks,				const char ***peaStrings);
static bool contact_GetAudioAssets_HandleSpecialActionBlocks		(const SpecialActionBlock			**ppSpecialActionBlocks,		const char ***peaStrings);
static bool contact_GetAudioAssets_HandleSpecialDialogActions		(const SpecialDialogAction			**ppSpecialDialogAction,		const char ***peaStrings);
static bool contact_GetAudioAssets_HandleSpecialDialogBlocks		(const SpecialDialogBlock			**ppSpecialDialogBlocks,		const char ***peaStrings);
static bool contact_GetAudioAssets_HandleWorldGameActionProperties	(const WorldGameActionProperties	**ppWorldGameActionProperties,	const char ***peaStrings);

static bool contact_GetAudioAssets_HandleString(const char *pcAddString, const char ***peaStrings)
{
	if (pcAddString)
	{
		bool bDup = false;
		FOR_EACH_IN_EARRAY(*peaStrings, const char, pcHasString) {
			if (strcmpi(pcHasString, pcAddString) == 0) {
				bDup = true;
			}
		} FOR_EACH_END;
		if (!bDup) {
			eaPush(peaStrings, strdup(pcAddString));
		}
		return true;
	}
	return false;
}

static bool contact_GetAudioAssets_HandleContactImageMenuData(const ContactImageMenuData *pContactImageMenuData, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	if (pContactImageMenuData) {
		FOR_EACH_IN_EARRAY(pContactImageMenuData->items, const ContactImageMenuItem, pContactImageMenuItem) {
			if (pContactImageMenuItem->action) {
				bResourceHasAudio |= contact_GetAudioAssets_HandleWorldGameActionProperties(pContactImageMenuItem->action->eaActions, peaStrings);
			}
		} FOR_EACH_END;
		FOR_EACH_IN_EARRAY(pContactImageMenuData->itemOverrides, const ContactImageMenuItem, pContactImageMenuItem) {
			if (pContactImageMenuItem->action) {
				bResourceHasAudio |= contact_GetAudioAssets_HandleWorldGameActionProperties(pContactImageMenuItem->action->eaActions, peaStrings);
			}
		} FOR_EACH_END;
	}
	return bResourceHasAudio;
}

static bool contact_GetAudioAssets_HandleContactMissionOffers(const ContactMissionOffer **ppContactMissionOffers, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(ppContactMissionOffers, const ContactMissionOffer, pContactMissionOffer) {
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactMissionOffer->greetingDialog,	peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactMissionOffer->offerDialog,		peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactMissionOffer->inProgressDialog,	peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactMissionOffer->completedDialog,	peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactMissionOffer->failureDialog,		peaStrings);
	} FOR_EACH_END;
	return bResourceHasAudio;
}

static bool contact_GetAudioAssets_HandleEndDialogAudios(const EndDialogAudio **ppEndDialogAudios, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(ppEndDialogAudios, const EndDialogAudio, pEndDialogAudio) {
		bResourceHasAudio |= contact_GetAudioAssets_HandleString(pEndDialogAudio->pchAudioName, peaStrings);
	} FOR_EACH_END;
	return bResourceHasAudio;
}

static bool contact_GetAudioAssets_HandleDialogBlocks(const DialogBlock **eaDialogBlocks, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(eaDialogBlocks, const DialogBlock, pDialogBlock) {
		bResourceHasAudio |= contact_GetAudioAssets_HandleString(pDialogBlock->audioName, peaStrings);
	} FOR_EACH_END;
	return bResourceHasAudio;
}

static bool contact_GetAudioAssets_HandleSpecialActionBlocks(const SpecialActionBlock **ppSpecialActionBlocks, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(ppSpecialActionBlocks, const SpecialActionBlock, pSpecialActionBlock) {
		bResourceHasAudio |= contact_GetAudioAssets_HandleSpecialDialogActions(pSpecialActionBlock->dialogActions, peaStrings);
	} FOR_EACH_END;
	return bResourceHasAudio;
}

static bool contact_GetAudioAssets_HandleSpecialDialogActions(const SpecialDialogAction **ppSpecialDialogAction, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(ppSpecialDialogAction, const SpecialDialogAction, pSpecialDialogAction) {
		bResourceHasAudio |= contact_GetAudioAssets_HandleWorldGameActionProperties(pSpecialDialogAction->actionBlock.eaActions, peaStrings);
	} FOR_EACH_END;
	return bResourceHasAudio;
}

static bool contact_GetAudioAssets_HandleSpecialDialogBlocks(const SpecialDialogBlock **ppSpecialDialogBlocks, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(ppSpecialDialogBlocks, const SpecialDialogBlock, pSpecialDialogBlock) {
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pSpecialDialogBlock->dialogBlock, peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleSpecialDialogActions(pSpecialDialogBlock->dialogActions, peaStrings);
	} FOR_EACH_END;
	return bResourceHasAudio;
}

static bool contact_GetAudioAssets_HandleWorldGameActionProperties(const WorldGameActionProperties **ppWorldGameActionProperties, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	FOR_EACH_IN_EARRAY(ppWorldGameActionProperties, const WorldGameActionProperties, pWorldGameActionProperties) {
		if (pWorldGameActionProperties->pSendNotificationProperties) {
			bResourceHasAudio |= contact_GetAudioAssets_HandleString(pWorldGameActionProperties->pSendNotificationProperties->pchSound, peaStrings);
		}
	} FOR_EACH_END;
	return bResourceHasAudio;
}

void contact_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	ContactDef *pContactDef;
	ResourceIterator rI;

	*ppcType = strdup("ContactDef");

	resInitIterator(g_ContactDictionary, &rI);
	while (resIteratorGetNext(&rI, NULL, &pContactDef))
	{
		bool bResourceHasAudio = false;

		bResourceHasAudio |= contact_GetAudioAssets_HandleContactImageMenuData(pContactDef->pImageMenuData, peaStrings);

		bResourceHasAudio |= contact_GetAudioAssets_HandleContactMissionOffers(pContactDef->offerList,			peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleContactMissionOffers(pContactDef->eaOfferOverrides,	peaStrings);

		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactDef->generalCallout,			peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactDef->missionCallout,			peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactDef->rangeCallout,			peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactDef->greetingDialog,			peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactDef->infoDialog,				peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactDef->defaultDialog,			peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactDef->missionListDialog,		peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactDef->noMissionsDialog,		peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactDef->missionExitDialog,		peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactDef->exitDialog,				peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactDef->eaMissionSearchDialog,	peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactDef->oldSpecialDialog,		peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleDialogBlocks(pContactDef->noStoreItemsDialog,		peaStrings);

		bResourceHasAudio |= contact_GetAudioAssets_HandleEndDialogAudios(pContactDef->eaEndDialogAudios, peaStrings);

		bResourceHasAudio |= contact_GetAudioAssets_HandleSpecialActionBlocks(pContactDef->specialActions,					peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleSpecialActionBlocks(pContactDef->eaSpecialActionBlockOverrides,	peaStrings);

		bResourceHasAudio |= contact_GetAudioAssets_HandleSpecialDialogBlocks(pContactDef->specialDialog,				peaStrings);
		bResourceHasAudio |= contact_GetAudioAssets_HandleSpecialDialogBlocks(pContactDef->eaSpecialDialogOverrides,	peaStrings);

		*puiNumData = *puiNumData + 1;
		if (bResourceHasAudio) {
			*puiNumDataWithAudio = *puiNumDataWithAudio + 1;
		}
	}
	resFreeIterator(&rI);
}

#include "contact_common_h_ast.c"
#include "contact_enums_h_ast.c"
