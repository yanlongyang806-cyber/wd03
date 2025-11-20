/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#include "AutoTransDefs.h"
#include "Character.h"
#include "CharacterClass.h"
#include "CostumeCommon.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "Entity.h"
#include "EntityBuild.h"
#include "EntitySavedData.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountData_h_ast.h"
#include "GameAccountDataCommon.h"
#include "gslCostume.h"
#include "gslEntity.h"
#include "LoggedTransactions.h"
#include "gslMailNPC.h"
#include "inventoryCommon.h"
#include "LocalTransactionManager.h"
#include "microtransactions_common.h"
#include "objTransactions.h"
#include "player.h"
#include "StringCache.h"
#include "transactionsystem.h"
#include "Guild.h"
#include "NotifyCommon.h"

#include "AutoGen/Player_h_ast.h"
#include "AutoGen/Character_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntityBuild_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "AutoGen/GameServerLib_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "../Common/AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// --------------------------------------------------------------------------
// Transaction to set player costume to a critter's costume file
// --------------------------------------------------------------------------

typedef struct CostumeChangeData {
	EntityRef entRef;
	EntityRef entOwnerRef;
	int containerID;
	int entType;
} CostumeChangeData;


AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Psaved.Costumedata.Uivalidatetag, .Psaved.Costumedata.Eacostumeslots, .Egender, .Psaved.Costumedata.Iactivecostume, .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome costume_tr_ChangePlayerCostume(ATR_ARGS, NOCONST(Entity)* pEnt, NON_CONTAINER PlayerCostume *pCostume, const ItemChangeReason *pReason, ChangePlayerCostumeParam *pParam)
{
	
	if( NONNULL(pEnt) &&
		NONNULL(pEnt->pSaved) &&
		(pEnt->pSaved->costumeData.uiValidateTag == pParam->uiValidateTag))
	{
		// Store the costume into the player's primary costume slot #1
		if (pParam->bIncrementCostumeCount) {
			// Make sure there is a second costume slot
			inv_ent_trh_SetNumeric(ATR_PASS_ARGS, pEnt, false, "PrimaryCostume", 1, pReason);
		}
		if (pParam->bCreateSlot) {
			NOCONST(PlayerCostumeSlot) *pSlot = StructCreateNoConst(parse_PlayerCostumeSlot);
			pSlot->pCostume = StructCloneDeConst(parse_PlayerCostume,pCostume);
			eaPush(&pEnt->pSaved->costumeData.eaCostumeSlots, pSlot);
		} else {
			if (pEnt->pSaved->costumeData.eaCostumeSlots[1]) {
				StructDestroyNoConstSafe(parse_PlayerCostume, &pEnt->pSaved->costumeData.eaCostumeSlots[1]->pCostume);
			}
			pEnt->pSaved->costumeData.eaCostumeSlots[1]->pCostume = StructCloneDeConst(parse_PlayerCostume,pCostume);
		}

		// Set the gender to match the costume
		pEnt->eGender = costumeTailor_GetGender(pCostume);

		// Also make this the active costume for the player
		pEnt->pSaved->costumeData.iActiveCostume = 1;

		// Increment the validate tag
		++pEnt->pSaved->costumeData.uiValidateTag;

		return TRANSACTION_OUTCOME_SUCCESS;
	}
	else
		return TRANSACTION_OUTCOME_FAILURE;
}


static void costumetransaction_ChangePlayerCostumeCallback( TransactionReturnVal *pReturn, CostumeChangeData *pData)
{
	Entity *pEnt = entFromEntityRefAnyPartition(pData->entRef);

	if (pEnt) {
		entity_SetDirtyBit(pEnt,parse_PlayerCostumeData, &pEnt->pSaved->costumeData, true);
		entity_SetDirtyBit(pEnt,parse_SavedEntityData, pEnt->pSaved, true);
		costumeEntity_ResetCostumeData(pEnt);
	}

	free(pData);
}


bool costumetransaction_InitChangePlayerCostumeParam(ChangePlayerCostumeParam *pParam, Entity *pEnt)
{
	int iNumCostumes;

	// Set up the data
	pParam->uiValidateTag = pEnt->pSaved->costumeData.uiValidateTag;

	iNumCostumes = costumeEntity_GetNumCostumeSlots(pEnt, entity_GetGameAccount(pEnt));
	if (iNumCostumes < 2) {
		pParam->bIncrementCostumeCount = true;
	}

	if (eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots) < 2) {
		pParam->bCreateSlot = true;
	}

	return true;
}

void costumetransaction_ChangePlayerCostume(Entity *pEnt, PlayerCostume *pCostume)
{
	if(pEnt && pEnt->pSaved)
	{
		TransactionReturnVal *pReturn;
		CostumeChangeData *data;
		ChangePlayerCostumeParam param = {0};
		ItemChangeReason reason = {0};

		if (!costumetransaction_InitChangePlayerCostumeParam(&param, pEnt)) {
			return;
		}

		data = calloc(1, sizeof(*data));
		data->entRef = pEnt->myRef;

		inv_FillItemChangeReason(&reason, pEnt, "Costume:ChangeCostume", "Player");

		pReturn = objCreateManagedReturnVal(costumetransaction_ChangePlayerCostumeCallback, data);
		AutoTrans_costume_tr_ChangePlayerCostume(
				pReturn, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), 
				pCostume, &reason, &param);
	}
}


// --------------------------------------------------------------------------
// Transaction to replace all of an entity's costumes
// --------------------------------------------------------------------------

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Costumedata.Uivalidatetag, .Psaved.Costumedata.Eacostumeslots, .Psaved.Costumedata.Iactivecostume");
enumTransactionOutcome costume_tr_ReplaceCostumes(ATR_ARGS, NOCONST(Entity)* pEnt, NON_CONTAINER PlayerCostume *pCostume, U32 uiValidateTag)
{
	if (NONNULL(pEnt) && 
		NONNULL(pEnt->pSaved) && 
		NONNULL(pCostume) &&
		(pEnt->pSaved->costumeData.uiValidateTag == uiValidateTag))
	{
		S32 i;
		
		// Destroy all costumes and replace with one that was passed in
		for(i = 0; i < eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots); ++i)	
		{
			if(NONNULL(pEnt->pSaved->costumeData.eaCostumeSlots[i]))
			{
				StructDestroyNoConstSafe(parse_PlayerCostume, &pEnt->pSaved->costumeData.eaCostumeSlots[i]->pCostume);
				pEnt->pSaved->costumeData.eaCostumeSlots[i]->pCostume = StructCloneDeConst(parse_PlayerCostume, pCostume);
			}
		}
		pEnt->pSaved->costumeData.iActiveCostume = 0;

		// Increment the validate tag
		++pEnt->pSaved->costumeData.uiValidateTag;
	}
	
	return TRANSACTION_OUTCOME_SUCCESS;
}


void costumetransaction_ReplaceCostumes(Entity *pEnt, PlayerCostume *pCostume)
{
	if (pEnt && pEnt->pSaved)
	{
		TransactionReturnVal *pReturn;
		CostumeChangeData *data;

		data = calloc(1, sizeof(*data));
		data->entRef = pEnt->myRef;

		pReturn = objCreateManagedReturnVal(costumetransaction_ChangePlayerCostumeCallback, data);
		AutoTrans_costume_tr_ReplaceCostumes(pReturn, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pCostume, pEnt->pSaved->costumeData.uiValidateTag);
	}
}


// --------------------------------------------------------------------------
// Transaction to set entity mood
// --------------------------------------------------------------------------

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Costumeref.Hmood");
enumTransactionOutcome costume_tr_ChangeMood(ATR_ARGS, NOCONST(Entity)* pEnt, const char *pcMood)
{
	PCMood *pMood = RefSystem_ReferentFromString(g_hCostumeMoodDict, pcMood);
	if (!pMood) {
		return TRANSACTION_OUTCOME_FAILURE;
	}
	SET_HANDLE_FROM_REFERENT(g_hCostumeMoodDict, pMood, pEnt->costumeRef.hMood);

	return TRANSACTION_OUTCOME_SUCCESS;
}


static void costumetransaction_ChangeMoodCallback( TransactionReturnVal *pReturn, CostumeChangeData *pData)
{
	Entity *pEnt = entFromEntityRefAnyPartition(pData->entRef);

	if (pEnt) {
		costumeEntity_RegenerateCostume(pEnt);     // Regenerate costume after change
		costumeEntity_SetCostumeRefDirty(pEnt);
	}

	free(pData);
}


void costumetransaction_ChangeMood(Entity *pEnt, const char *pcMood)
{
	TransactionReturnVal *pReturn;
	CostumeChangeData *data;

	data = calloc(1, sizeof(*data));
	data->entRef = pEnt->myRef;

	pReturn = objCreateManagedReturnVal(costumetransaction_ChangeMoodCallback, data);
	AutoTrans_costume_tr_ChangeMood(pReturn, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pcMood);
}


// --------------------------------------------------------------------------
// Transaction to store a costume costume on an entity
// --------------------------------------------------------------------------

AUTO_TRANS_HELPER
ATR_LOCKS(pCostEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
bool costume_trh_PayWithSimple(ATR_ARGS, ATH_ARG NOCONST(Entity) *pCostEnt, PCPaymentMethod ePayMethod, int eClassType, int iCost, const ItemChangeReason *pReason)
{
	if (ePayMethod == kPCPay_Resources)
	{
		NOCONST(InventoryBagLite)* pBag = CONTAINER_NOCONST(InventoryBagLite, eaIndexedGetUsingInt( &pCostEnt->pInventoryV2->ppLiteBags, 1 /* Literal InvBagIDs_Numeric */));

		// The above call to eaIndexedGetUsingInt uses a literal in the second argument, when it should e using
		// InvBagIDs_Numeric. However, TextParser can't deal with enums right now, and thus will spit out errors
		// if you try to reference that enum. Until that is fixed, it has to use a literal.
		if (!inv_lite_trh_AddNumeric(ATR_PASS_ARGS, pCostEnt, false, pBag, "Resources", -iCost, pReason))
		{
			TRANSACTION_APPEND_LOG_FAILURE("Can't afford to pay [%d Resources] on Entity [%s:%d].", iCost, pCostEnt->debugName, pCostEnt->myContainerID);
			return false;
		}
	}
	else
	{
		const char *pchCostumeToken = MicroTrans_GetFreeCostumeChangeKeyID();
		if (ePayMethod == kPCPay_FreeFlexToken)
		{
			pchCostumeToken = "FreeCostumeChangeFlex";
		}
		else if (eClassType == StaticDefineIntGetInt(CharClassTypesEnum, "Space"))
		{
			pchCostumeToken = MicroTrans_GetFreeShipCostumeChangeKeyID();
		}

		if (inv_trh_GetNumericValue(ATR_PASS_ARGS, pCostEnt, pchCostumeToken) > 0)
		{
			if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pCostEnt, false, pchCostumeToken, -1, pReason))
			{
				TRANSACTION_APPEND_LOG_FAILURE("Failed to subtract token [%s] by [%d] on Entity [%s:%d].", pchCostumeToken, -1, pCostEnt->debugName, pCostEnt->myContainerID);
				return false;
			}
		}
		else
		{
			TRANSACTION_APPEND_LOG_FAILURE("No tokens of type [%s] on Entity [%s:%d].", pchCostumeToken, pCostEnt->debugName, pCostEnt->myContainerID);
			return false;
		}
	}
	return true;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".Iaccountid, .Eakeys");
bool costume_trh_PayWithGADToken(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, PCPaymentMethod ePayMethod, int eClassType, const char *pcCostEntDebugName, int iCostEntContainerID)
{
	//BH: Assumes pData is not null.  It should be checked earlier
	const char *pcAttribKey = NULL;
	if(eClassType == StaticDefineIntGetInt(CharClassTypesEnum, "Space"))
	{
		pcAttribKey = MicroTrans_GetFreeShipCostumeChangeGADKey();
	}
	else
	{
		pcAttribKey = MicroTrans_GetFreeCostumeChangeGADKey();
	}

	if (gad_trh_GetAttribInt(pData, pcAttribKey) > 0)
	{
		if(!slGAD_trh_ChangeAttrib(ATR_PASS_ARGS, pData, pcAttribKey, -1))
		{
			TRANSACTION_APPEND_LOG_FAILURE("Failed to change the Game Account Data key [%s] by amount [-1] on Entity [%s:%d] account id [%d]",
				pcAttribKey,
				pcCostEntDebugName,
				iCostEntContainerID,
				pData->iAccountID);
			return false;
		}
	}
	else
	{
		TRANSACTION_APPEND_LOG_FAILURE("Game Account Data key [%s] was zero or less for Entity [%s:%d] account id [%d]",
			pcAttribKey,
			pcCostEntDebugName,
			iCostEntContainerID,
			pData->iAccountID);
		return false;
		
	}

	return true;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Costumedata.Uivalidatetag, .Psaved.Costumedata.Iactivecostume, .Psaved.Costumedata.Eacostumeslots");
bool costume_trh_StorePlayerCostumeWithCreateHelper(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, StorePlayerCostumeParam *pParam)
{
	// Check the index
	if (ISNULL(pEnt) || 
		ISNULL(pEnt->pSaved) || 
		(pEnt->pSaved->costumeData.uiValidateTag != pParam->uiValidateTag) ||
		!pParam->bCreateOrRemoveSlot) {
		TRANSACTION_APPEND_LOG_FAILURE("Validate tag mismatch.");
		return false;
	}

	// Check the index and type range
	switch(pParam->eCostumeType)
	{
	case kPCCostumeStorageType_SpacePet:
	case kPCCostumeStorageType_Pet:
	case kPCCostumeStorageType_Primary: 
	case kPCCostumeStorageType_Secondary:
		if (pParam->pCostume) {
			// If creating a slot, do so
			NOCONST(PlayerCostumeSlot) *pSlot;
			pSlot = StructCreateNoConst(parse_PlayerCostumeSlot);
			pSlot->pCostume = StructCloneDeConst(parse_PlayerCostume, pParam->pCostume);
			pSlot->pcSlotType = pParam->pcSlotType;
			eaPush(&pEnt->pSaved->costumeData.eaCostumeSlots, pSlot);
		}
		else
		{
			// If removing a slot, do so
			StructDestroyNoConstSafe(parse_PlayerCostume, &pEnt->pSaved->costumeData.eaCostumeSlots[pParam->iIndex]->pCostume);
			eaRemove(&pEnt->pSaved->costumeData.eaCostumeSlots, pParam->iIndex);
			if (pEnt->pSaved->costumeData.iActiveCostume >= pParam->iIndex) {
				--pEnt->pSaved->costumeData.iActiveCostume;
			}
		}
		break;
	default:
		TRANSACTION_APPEND_LOG_FAILURE("Not a valid costume type.");
		return false;
	}

	if (pParam->bIsActive && !pParam->pCostume) {
		// Active costume got cleared!  Fall back on the primary costume.
		pEnt->pSaved->costumeData.iActiveCostume = 0;
	}

	return true;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Costumedata.Uivalidatetag, .Psaved.Costumedata.Eacostumeslots, .Psaved.Costumedata.Iactivecostume");
bool costume_trh_StorePlayerCostumeNoCreateHelper(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, StorePlayerCostumeParam *pParam)
{
	NOCONST(PlayerCostumeSlot) *pSlot;

	// Check the index
	if (ISNULL(pEnt) || 
		ISNULL(pEnt->pSaved) || 
		(pEnt->pSaved->costumeData.uiValidateTag != pParam->uiValidateTag) ||
		pParam->bCreateOrRemoveSlot ||
		!pParam->pCostume) 
	{
		TRANSACTION_APPEND_LOG_FAILURE("Validate tag mismatch.");
		return false;
	}

	// Check the index and type range
	switch(pParam->eCostumeType)
	{
	case kPCCostumeStorageType_SpacePet:
	case kPCCostumeStorageType_Pet:
	case kPCCostumeStorageType_Primary: 
	case kPCCostumeStorageType_Secondary:
		// Just modifying a costume in place
		pSlot = eaGet(&pEnt->pSaved->costumeData.eaCostumeSlots, pParam->iIndex); // TODO: Make sure this locks just one array element
		StructDestroyNoConstSafe(parse_PlayerCostume, &pSlot->pCostume);
		pSlot->pCostume = StructCloneDeConst(parse_PlayerCostume, pParam->pCostume);
		pSlot->pcSlotType = pParam->pcSlotType;
		break;
	default:
		TRANSACTION_APPEND_LOG_FAILURE("Not a valid costume type");
		return false;
	}

	if (pParam->bIsActive && !pParam->pCostume) {
		// Active costume got cleared!  Fall back on the primary costume.
		pEnt->pSaved->costumeData.iActiveCostume = 0;
	}

	return true;
}


AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Psaved.Costumedata.Uivalidatetag, .Hallegiance, .Hsuballegiance, .Psaved.Costumedata.Iactivecostume, .Psaved.Costumedata.Eacostumeslots, .Pchar.Ilevelexp, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pCostEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome costume_tr_StorePlayerCostumeSimpleCostNoCreate(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity)* pCostEnt, const ItemChangeReason *pReason, StorePlayerCostumeParam *pParam)
{
	// Check the index
	if (ISNULL(pEnt) || 
		ISNULL(pEnt->pSaved) || 
		(pEnt->pSaved->costumeData.uiValidateTag != pParam->uiValidateTag)) {
		TRANSACTION_RETURN_FAILURE("Validate tag mismatch.");
	}

	// Store the costume
	if (!costume_trh_StorePlayerCostumeNoCreateHelper(ATR_PASS_ARGS, pEnt, pParam)) {
		TRANSACTION_RETURN_FAILURE("Not a valid costume type.");
	}

	// Increment the validate tag
	++pEnt->pSaved->costumeData.uiValidateTag;

	// Make payment
	if (NONNULL(pCostEnt))
	{
		if (ISNULL(pCostEnt->pInventoryV2)) {
			ErrorDetailsf("%s: Entity: %s, PetEntity: %s; Ent: %s, CostEnt: %s", __FUNCTION__, pParam->entDebugName, pParam->petEntDebugName, pEnt->debugName, pCostEnt->debugName);
			Errorf("StorePlayerCostume: Cost ent has no inventory");
			TRANSACTION_RETURN_FAILURE("Cost Entity %s missing inventory.", pCostEnt->debugName);
		}

		if (!costume_trh_PayWithSimple(ATR_PASS_ARGS, pCostEnt, pParam->ePayMethod, pParam->eClassType, pParam->iCost, pReason))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}
	else
	{
		if (ISNULL(pEnt->pInventoryV2)) {
			ErrorDetailsf("%s: Entity: %s, PetEntity: %s; Ent: %s, CostEnt: %s", __FUNCTION__, pParam->entDebugName, pParam->petEntDebugName, pEnt->debugName, "NULL");
			Errorf("StorePlayerCostume: Cost ent has no inventory");
			TRANSACTION_RETURN_FAILURE("Cost Entity %s missing inventory.", pEnt->debugName);
		}

		if (!costume_trh_PayWithSimple(ATR_PASS_ARGS, pEnt, pParam->ePayMethod, pParam->eClassType, pParam->iCost, pReason))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}

	TRANSACTION_RETURN_SUCCESS("");
}


AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Psaved.Costumedata.Uivalidatetag, .Hallegiance, .Hsuballegiance, .Psaved.Costumedata.Eacostumeslots, .Psaved.Costumedata.Iactivecostume, .Pchar.Ilevelexp, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pCostEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome costume_tr_StorePlayerCostumeSimpleCostWithCreate(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity)* pCostEnt, const ItemChangeReason *pReason, StorePlayerCostumeParam *pParam)
{
	// Check the index
	if (ISNULL(pEnt) || 
		ISNULL(pEnt->pSaved) || 
		(pEnt->pSaved->costumeData.uiValidateTag != pParam->uiValidateTag)) {
		TRANSACTION_RETURN_FAILURE("Validate tag mismatch.");
	}

	if (!costume_trh_StorePlayerCostumeWithCreateHelper(ATR_PASS_ARGS, pEnt, pParam)) {
		TRANSACTION_RETURN_FAILURE("Not a valid costume type.");
	}

	// Increment the validate tag
	++pEnt->pSaved->costumeData.uiValidateTag;

	// Make payment
	if (NONNULL(pCostEnt))
	{
		if (ISNULL(pCostEnt->pInventoryV2)) {
			ErrorDetailsf("%s: Entity: %s, PetEntity: %s; Ent: %s, CostEnt: %s", __FUNCTION__, pParam->entDebugName, pParam->petEntDebugName, pEnt->debugName, pCostEnt->debugName);
			Errorf("StorePlayerCostume: Cost ent has no inventory");
			TRANSACTION_RETURN_FAILURE("Cost Entity %s missing inventory.", pCostEnt->debugName);
		}

		if (!costume_trh_PayWithSimple(ATR_PASS_ARGS, pCostEnt, pParam->ePayMethod, pParam->eClassType, pParam->iCost, pReason))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}
	else
	{
		if (ISNULL(pEnt->pInventoryV2)) {
			ErrorDetailsf("%s: Entity: %s, PetEntity: %s; Ent: %s, CostEnt: %s", __FUNCTION__, pParam->entDebugName, pParam->petEntDebugName, pEnt->debugName, "NULL");
			Errorf("StorePlayerCostume: Cost ent has no inventory");
			TRANSACTION_RETURN_FAILURE("Cost Entity %s missing inventory.", pEnt->debugName);
		}

		if (!costume_trh_PayWithSimple(ATR_PASS_ARGS, pEnt, pParam->ePayMethod, pParam->eClassType, pParam->iCost, pReason))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}
	
	TRANSACTION_RETURN_SUCCESS("");
}


AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Costumedata.Uivalidatetag, .Psaved.Costumedata.Eacostumeslots, .Psaved.Costumedata.Iactivecostume")
ATR_LOCKS(pData, ".Iaccountid, .Eakeys");
enumTransactionOutcome costume_tr_StorePlayerCostumeGADCostNoCreate(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(GameAccountData) *pData, StorePlayerCostumeParam *pParam, const char *pcCostEntDebugName, int iCostEntContainerID)
{
	// Check the index
	if (ISNULL(pEnt) || 
		ISNULL(pEnt->pSaved) || 
		(pEnt->pSaved->costumeData.uiValidateTag != pParam->uiValidateTag)) {
		TRANSACTION_RETURN_FAILURE("Validate tag mismatch.");
	}

	// Store the costume
	if (!costume_trh_StorePlayerCostumeNoCreateHelper(ATR_PASS_ARGS, pEnt, pParam)) {
		TRANSACTION_RETURN_FAILURE("Not a valid costume type.");
	}

	// Increment the validate tag
	++pEnt->pSaved->costumeData.uiValidateTag;

	// Make payment
	if (!costume_trh_PayWithGADToken(ATR_PASS_ARGS, pData, pParam->ePayMethod, pParam->eClassType, pcCostEntDebugName, iCostEntContainerID))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	TRANSACTION_RETURN_SUCCESS("");
}


AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Costumedata.Uivalidatetag, .Psaved.Costumedata.Eacostumeslots, .Psaved.Costumedata.Iactivecostume")
ATR_LOCKS(pData, ".Iaccountid, .Eakeys");
enumTransactionOutcome costume_tr_StorePlayerCostumeGADCostWithCreate(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(GameAccountData) *pData, StorePlayerCostumeParam *pParam, const char *pcCostEntDebugName, int iCostEntContainerID)
{
	// Check the index
	if (ISNULL(pEnt) || 
		ISNULL(pEnt->pSaved) || 
		(pEnt->pSaved->costumeData.uiValidateTag != pParam->uiValidateTag)) {
		TRANSACTION_RETURN_FAILURE("Validate tag mismatch.");
	}

	if (!costume_trh_StorePlayerCostumeWithCreateHelper(ATR_PASS_ARGS, pEnt, pParam)) {
		TRANSACTION_RETURN_FAILURE("Not a valid costume type.");
	}

	// Increment the validate tag
	++pEnt->pSaved->costumeData.uiValidateTag;

	// Make payment
	if (!costume_trh_PayWithGADToken(ATR_PASS_ARGS, pData, pParam->ePayMethod, pParam->eClassType, pcCostEntDebugName, iCostEntContainerID))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	
	TRANSACTION_RETURN_SUCCESS("");
}


static void costumetransaction_StorePlayerCostumeCallback(TransactionReturnVal *pReturn, CostumeChangeData *pData)
{
	Entity *pEnt = entFromEntityRefAnyPartition(pData->entRef);
	Entity *pOwnerEnt = pEnt;

	if (pData->entOwnerRef) {
		S32 i;
		pOwnerEnt = entFromEntityRefAnyPartition(pData->entOwnerRef);
		if (pOwnerEnt && pOwnerEnt->pSaved) {
			for (i=eaSize(&pOwnerEnt->pSaved->ppOwnedContainers)-1; i>=0; --i) {
				PetRelationship *pRelationship = eaGet(&pOwnerEnt->pSaved->ppOwnedContainers, i);
				Entity *pPetEnt = pRelationship ? GET_REF(pRelationship->hPetRef) : NULL;
				if (pPetEnt && (entGetType(pPetEnt) == pData->entType && entGetContainerID(pPetEnt) == (ContainerID)pData->containerID)) {
					pEnt = pPetEnt;
					break;
				}
			}
		}
	}

	if (pEnt && (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)) {
		costumeEntity_ResetStoredCostume(pEnt);
		costumeEntity_RegenerateCostumeEx(entGetPartitionIdx(pOwnerEnt), pEnt, entity_GetCachedGameAccountDataExtract(pOwnerEnt));     // Regenerate costume after change
		if (pData->entOwnerRef) entSetActive(pEnt);
		costumeEntity_SetCostumeRefDirty(pEnt);
		entity_SetDirtyBit(pEnt,parse_PlayerCostumeData, &pEnt->pSaved->costumeData, true);
		entity_SetDirtyBit(pEnt,parse_SavedEntityData, pEnt->pSaved, true);
		ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pOwnerEnt, true, "StoreCostume.Saved");
	}
	else
	{
		ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pOwnerEnt, false, "StoreCostume.NotSaved");
	}

	free(pData);
}


bool costumetransaction_InitStorePlayerCostumeParam(StorePlayerCostumeParam *pParam, Entity *pEnt, Entity *pPetEnt, GameAccountData *pData, PCCostumeStorageType eCostumeType, int iIndex, PlayerCostume *pCostume, const char *pcSlotType, S32 iCost, PCPaymentMethod ePayMethod)
{
	Entity *pCostEnt;
	Entity *pChangeEnt;
	int iNumCostumes;
	CharacterClass* pClass = NULL;

	// Set up local tracking for the cost and change entity
	pCostEnt = pEnt;
	pChangeEnt = pPetEnt ? pPetEnt : pEnt;

	// Check outside transaction and rely on uiValidateTag to avoid problems
	iNumCostumes = costumeEntity_GetNumCostumeSlots(pChangeEnt, entity_GetGameAccount(pCostEnt));
	if ((iIndex < 0) || (iIndex >= iNumCostumes)) {
		return false; // Out of range
	}

	// Some cases are illegal
	if (!pCostume && (eCostumeType == kPCCostumeStorageType_Primary) && (iIndex == 0)) {
		return false; // "Can't clear costume 0."
	}
	if (!pCostume && (eCostumeType == kPCCostumeStorageType_Pet || eCostumeType == kPCCostumeStorageType_SpacePet)) {
		return false; // "Can't clear pet or space pet costume."
	}
	
	// Set data based on costume type
	switch(eCostumeType)
	{
	case kPCCostumeStorageType_SpacePet:
	case kPCCostumeStorageType_Pet:
		if (iIndex != 0) {
			return false; // "Only one pet or space pet costume allowed."
		}
		pParam->bIsActive = true;
		//Fall through
	case kPCCostumeStorageType_Primary: 
	case kPCCostumeStorageType_Secondary: 
		if (pChangeEnt->pSaved->costumeData.iActiveCostume == iIndex) {
			pParam->bIsActive = true;
		}
		if (pCostume) {
			if (iIndex >= eaSize(&pChangeEnt->pSaved->costumeData.eaCostumeSlots)) {
				pParam->bCreateOrRemoveSlot = true;
			}
		} else {
			// No costume so removing a slot
			pParam->bCreateOrRemoveSlot = true;
		}
		break;
	default:
		return false; // "Not a valid costume type."
	}

	// Set up class type info
	if (pChangeEnt->pChar) 
	{
		pClass = GET_REF(pChangeEnt->pChar->hClass);
	}
	if (pClass)
	{
		pParam->eClassType = pClass->eType;
	}
	else
	{
		pParam->eClassType = CharClassTypes_None;
	}

	// Handle default pay converted to specific pay method.  We don't want to pass "default" into a transaction.
	if (ePayMethod == kPCPay_Default)
	{
		// It changes to resources unless we find something better
		ePayMethod = kPCPay_Resources;
		if (iCost != 0) 
		{
			bool bCheckCostEnt = (pChangeEnt->myContainerID != pCostEnt->myContainerID) || (pChangeEnt->myEntityType != pCostEnt->myEntityType);
			int iFreeChange = 0;

			if (pParam->eClassType == StaticDefineIntGetInt(CharClassTypesEnum, "Space"))
			{
				iFreeChange = inv_GetNumericItemValue(pChangeEnt, MicroTrans_GetFreeShipCostumeChangeKeyID());
				if (iFreeChange > 0)
				{
					iCost = 1;
					ePayMethod = kPCPay_FreeToken;
					pCostEnt = pChangeEnt;
				}
				else if (bCheckCostEnt)
				{
					iFreeChange = inv_GetNumericItemValue(pCostEnt, MicroTrans_GetFreeShipCostumeChangeKeyID());
					if (iFreeChange > 0)
					{
						iCost = 1;
						ePayMethod = kPCPay_FreeToken;
					}
				}
				
				if (iFreeChange <= 0)
				{
					if (pData)
					{
						iFreeChange = gad_GetAttribInt(pData, MicroTrans_GetFreeShipCostumeChangeGADKey());
						if (iFreeChange > 0)
						{
							iCost = 1;
							ePayMethod = kPCPay_GADToken;
						}
					}
				}
			}
			else
			{
				iFreeChange = inv_GetNumericItemValue(pChangeEnt, MicroTrans_GetFreeCostumeChangeKeyID());
				if (iFreeChange > 0)
				{
					iCost = 1;
					ePayMethod = kPCPay_FreeToken;
					pCostEnt = pChangeEnt;
				}
				else if (bCheckCostEnt)
				{
					iFreeChange = inv_GetNumericItemValue(pCostEnt, MicroTrans_GetFreeCostumeChangeKeyID());
					if (iFreeChange > 0)
					{
						iCost = 1;
						ePayMethod = kPCPay_FreeToken;
					}
				}

				if (iFreeChange <= 0)
				{
					iFreeChange = inv_GetNumericItemValue(pChangeEnt, "FreeCostumeChangeFlex");
					if(iFreeChange > 0)
					{
						iCost = 1;
						ePayMethod = kPCPay_FreeFlexToken;
						pCostEnt = pChangeEnt;
					}
					else if (bCheckCostEnt)
					{
						iFreeChange = inv_GetNumericItemValue(pCostEnt, "FreeCostumeChangeFlex");
						if(iFreeChange > 0)
						{
							iCost = 1;
							ePayMethod = kPCPay_FreeFlexToken;
						}
					}

					if (iFreeChange <= 0)
					{
						if (pData)
						{
							iFreeChange = gad_GetAttribInt(pData, MicroTrans_GetFreeCostumeChangeGADKey());
							if (iFreeChange > 0)
							{
								iCost = 1;
								ePayMethod = kPCPay_GADToken;
							}
						}
					}
				}
			}
		}
	}

	// Init data
	pParam->uiValidateTag = pChangeEnt->pSaved->costumeData.uiValidateTag;
	pParam->eCostumeType = eCostumeType;
	pParam->iIndex = iIndex;
	pParam->pCostume = pCostume;
	pParam->iCost = iCost;
	pParam->ePayMethod = ePayMethod;
	pParam->ePayType = entGetType(pCostEnt);
	pParam->iPayContainerID = entGetContainerID(pCostEnt);
	pParam->pcSlotType = allocAddString(pcSlotType);

	strcpy(pParam->entDebugName, pEnt->debugName);
	strcpy(pParam->petEntDebugName, pPetEnt ? pPetEnt->debugName : "NULL");

	if (ePayMethod == kPCPay_GADToken) 
	{
		pParam->bNeedsGAD = true;
	}

	return true;
}


void costumetransaction_StorePlayerCostume(Entity *pEnt, Entity *pPetEnt, PCCostumeStorageType eCostumeType, int iIndex, PlayerCostume *pCostume, const char *pcSlotType, S32 iCost, PCPaymentMethod ePayMethod)
{
	if(pEnt && pEnt->pSaved && pEnt->pPlayer && pEnt->pPlayer->accountID > 0)
	{
		TransactionReturnVal *pReturn;
		CostumeChangeData *data;
		StorePlayerCostumeParam param = {0};
		Entity *pEntToChange = (pPetEnt ? pPetEnt : pEnt);
		ItemChangeReason reason = {0};

		if (!costumetransaction_InitStorePlayerCostumeParam(&param, pEnt, pPetEnt, entity_GetGameAccount(pEnt), eCostumeType, iIndex, pCostume, pcSlotType, iCost, ePayMethod)) {
			return; // Fail;
		}

		data = calloc(1, sizeof(*data));

		if (pPetEnt)
		{
			data->entRef = entGetRef(pPetEnt);
			data->entOwnerRef = entGetRef(pEnt);
			data->containerID = entGetContainerID(pPetEnt);
			data->entType = entGetType(pPetEnt);
		}
		else
		{
			data->entRef = entGetRef(pEnt);
			data->entOwnerRef = 0;
			data->containerID = 0;
			data->entType = 0;
		}

		pReturn = objCreateManagedReturnVal(costumetransaction_StorePlayerCostumeCallback, data);
		if (param.bCreateOrRemoveSlot)
		{
			if (param.bNeedsGAD)
			{
				AutoTrans_costume_tr_StorePlayerCostumeGADCostWithCreate(pReturn, GetAppGlobalType(), 
					entGetType(pEntToChange), entGetContainerID(pEntToChange), 
					GLOBALTYPE_GAMEACCOUNTDATA, pEnt->pPlayer->accountID, 
					&param, pEnt->debugName, pEnt->myContainerID);
			}
			else
			{
				inv_FillItemChangeReason(&reason, pEnt, "Costume:StoreCostume", pEntToChange->debugName);

				AutoTrans_costume_tr_StorePlayerCostumeSimpleCostWithCreate(pReturn, GetAppGlobalType(), 
					entGetType(pEntToChange), entGetContainerID(pEntToChange), 
					entGetType(pEntToChange)!=param.ePayType?param.ePayType:0, entGetContainerID(pEntToChange)!=param.iPayContainerID?param.iPayContainerID:0, 
					&reason, &param);
			}
		}
		else
		{
			if (param.bNeedsGAD)
			{
				AutoTrans_costume_tr_StorePlayerCostumeGADCostNoCreate(pReturn, GetAppGlobalType(), 
					entGetType(pEntToChange), entGetContainerID(pEntToChange), 
					GLOBALTYPE_GAMEACCOUNTDATA, pEnt->pPlayer->accountID, 
					&param, pEnt->debugName, pEnt->myContainerID);
			}
			else
			{
				inv_FillItemChangeReason(&reason, pEnt, "Costume:StoreCostume", pEntToChange->debugName);

				AutoTrans_costume_tr_StorePlayerCostumeSimpleCostNoCreate(pReturn, GetAppGlobalType(), 
					entGetType(pEntToChange), entGetContainerID(pEntToChange), 
					entGetType(pEntToChange)!=param.ePayType?param.ePayType:0, entGetContainerID(pEntToChange)!=param.iPayContainerID?param.iPayContainerID:0, 
					&reason, &param);
			}
		}
	}
	else if (pEnt && pEnt->pPlayer && pEnt->pPlayer->accountID == 0)
	{
		ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pEnt, false, "StoreCostume.FailAutoLogin");
	}
}


// --------------------------------------------------------------------------
// Transaction to set which costume is active for an entity
// --------------------------------------------------------------------------

// This is rolled out into a helper so other functions can change the active costume
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Costumedata.Uivalidatetag, .Psaved.Uitimestampcostumeset, .Egender, .Psaved.Costumedata.Iactivecostume");
enumTransactionOutcome costume_trh_SetPlayerActiveCostume(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, SetActiveCostumeParam *pParam)
{
	// Check the validate tag
	if (ISNULL(pEnt) || 
		ISNULL(pEnt->pSaved) ||
		(pEnt->pSaved->costumeData.uiValidateTag != pParam->uiValidateTag)) {
		return TRANSACTION_OUTCOME_FAILURE;
	}
	
	// Update timestamp for change
	pEnt->pSaved->uiTimestampCostumeSet = timeSecondsSince2000();
	
	// Update gender and active costume
	pEnt->eGender = pParam->eGender;
	pEnt->pSaved->costumeData.iActiveCostume = pParam->iIndex;

	// Increment the validate tag
	++pEnt->pSaved->costumeData.uiValidateTag;

	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Costumedata.Uivalidatetag, .Psaved.Uitimestampcostumeset, .Egender, .Psaved.Costumedata.Iactivecostume, .Psaved.Uibuildvalidatetag, .Psaved.Ppbuilds");
enumTransactionOutcome costume_tr_SetPlayerActiveCostume(ATR_ARGS, NOCONST(Entity)* pEnt, SetActiveCostumeParam *pParam, BuildSetCostumeParam *pBuildParam)
{
	if ((costume_trh_SetPlayerActiveCostume(ATR_PASS_ARGS, pEnt, pParam) != TRANSACTION_OUTCOME_SUCCESS) ||
		(pBuildParam && trEntity_BuildSetCostume(ATR_PASS_ARGS, pEnt, pBuildParam) != TRANSACTION_OUTCOME_SUCCESS))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}


static void costumetransaction_SetPlayerActiveCostumeCallback( TransactionReturnVal *pReturn, CostumeChangeData *pData)
{
	Entity *pEnt = entFromEntityRefAnyPartition(pData->entRef);

	if (pEnt && (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS))
	{
		costumeEntity_ResetCostumeData(pEnt);
	}

	free(pData);
}


bool costumetransaction_InitSetActiveCostumeParam(SetActiveCostumeParam *pParam, Entity *pEnt, PCCostumeStorageType eCostumeType, int iIndex)
{
	PlayerCostume *pCostume;
	int iNumCostumes;
	U32 iCurTime;

	// Make sure it's a saved entity
	if (!pEnt || !pEnt->pSaved) {
		return false;
	}
	// Make sure index is within number of available slots
	iNumCostumes = costumeEntity_GetNumCostumeSlots(pEnt, entity_GetGameAccount(pEnt));
	if (iIndex >= iNumCostumes) {
		return false;
	}
	// Make sure index is within number of saved slots
	if ((iIndex < 0) || (iIndex >= eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots))) {
		return false;
	}
	// Make sure costume is non-null
	pCostume = pEnt->pSaved->costumeData.eaCostumeSlots[iIndex]->pCostume;
	if (!pCostume) {
		return false;
	}
	// Make sure haven't done this too recently
	iCurTime = timeSecondsSince2000();
	if (iCurTime - pEnt->pSaved->uiTimestampCostumeSet < g_CostumeConfig.iChangeCooldown) {
		RemoteCommand_notify_RemoteSendNotification(pEnt->myEntityType, pEnt->myContainerID, "Costume.Error.ChangeCooldown", kNotifyType_CostumeChanged);
		return false;
	}
	// Make sure costume type param makes sense
	switch(eCostumeType)
	{
	case kPCCostumeStorageType_Primary:
	case kPCCostumeStorageType_Secondary: 
	case kPCCostumeStorageType_Pet: //Pet costume
		break; // Value OK
	default:
		return false;
	}

	// Init data
	pParam->uiValidateTag = pEnt->pSaved->costumeData.uiValidateTag;
	pParam->iIndex = iIndex;
	pParam->eCostumeType = eCostumeType;
	pParam->eGender = costumeTailor_GetGender(pCostume);

	return true;
}


void costumetransaction_SetPlayerActiveCostume(Entity *pEnt, PCCostumeStorageType eCostumeType, int iIndex)
{
	if(pEnt && pEnt->pSaved && pEnt->pPlayer && pEnt->pPlayer->accountID > 0)
	{
		TransactionReturnVal *pReturn;
		CostumeChangeData *data;
		SetActiveCostumeParam param = {0};
		BuildSetCostumeParam buildParam = {0};

		// Pre-transaction validation
		if (!costumetransaction_InitSetActiveCostumeParam(&param, pEnt, eCostumeType, iIndex) ||
			(eaSize(&pEnt->pSaved->ppBuilds) && !entity_InitBuildSetCostumeParam(&buildParam, pEnt, pEnt->pSaved->uiIndexBuild, eCostumeType, iIndex))) {
			return;
		}

		data = calloc(1, sizeof(*data));
		data->entRef = pEnt->myRef;

		pReturn = LoggedTransactions_CreateManagedReturnValEnt("", pEnt, costumetransaction_SetPlayerActiveCostumeCallback, data);
		AutoTrans_costume_tr_SetPlayerActiveCostume(pReturn, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			&param, (eaSize(&pEnt->pSaved->ppBuilds) ? &buildParam : NULL));
	}
}


// --------------------------------------------------------------------------
// Transaction to mark a costume as bad and give credit
// --------------------------------------------------------------------------

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
bool costume_trh_CostumeCredit(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, S32 costumeTickets, const ItemChangeReason *pReason)
{
	if(NONNULL(pEnt) && NONNULL(pEnt->pSaved))
	{
		int freeChange = inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, MicroTrans_GetFreeCostumeChangeKeyID()) + costumeTickets;
		if (freeChange < 0) freeChange = 0;
		inv_ent_trh_SetNumeric(ATR_PASS_ARGS, pEnt, false, MicroTrans_GetFreeCostumeChangeKeyID(), freeChange, pReason);
	}
	
	return true;
}


// replace all player primary costumes
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Costumedata.Uivalidatetag, .Psaved.Costumedata.Eacostumeslots, .Psaved.Costumedata.Iactivecostume, .Pchar.Hspecies, .Egender");
bool costume_trh_CostumeReplace(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U32 uiValidateTag, GameAccountDataExtract *pExtract)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pSaved))
	{
		S32 i;
		NOCONST(PlayerCostume) *pCostume = NULL;
		SpeciesDef *pSpecies = NULL;
		const char *pcCurrentSlotType = allocAddString("NOT_A_SLOT_TYPE");
		PCSlotType *pSlotType;
		
		if (pEnt->pSaved->costumeData.uiValidateTag != uiValidateTag) {
			return false;
		}
		if(NONNULL(pEnt->pChar)) {
			pSpecies = GET_REF(pEnt->pChar->hSpecies);
		}

		// replace old costumes	
		for(i=eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots)-1; i>=0; --i)
		{
			// Only create costume if the current pCostume is not compatible with the current slot type
			if(pEnt->pSaved->costumeData.eaCostumeSlots[i]->pcSlotType != pcCurrentSlotType || !pCostume)
			{
				if (pCostume) {
					StructDestroyNoConst(parse_PlayerCostume, pCostume);
				}
				pCostume = costumeEntity_trh_MakePlainCostume(pEnt);
				assert(pCostume);
				pcCurrentSlotType = pEnt->pSaved->costumeData.eaCostumeSlots[i]->pcSlotType;
				pSlotType = costumeLoad_GetSlotType(pcCurrentSlotType);

				costumeTailor_MakeCostumeValid(pCostume, pSpecies, NULL, pSlotType, false, false, false, /*Ignore their guild */ NULL, false, pExtract, false, NULL);
				costumeTailor_StripUnnecessary(pCostume);
			}

			// moved the destroy here as costumeEntity_MakePlainCostume() uses the active costume to make decisions. Destroying it before costumeEntity_MakePlainCostume() and pCostume ends up very incvomplete
			StructDestroyNoConstSafe(parse_PlayerCostume, &pEnt->pSaved->costumeData.eaCostumeSlots[i]->pCostume);
			pEnt->pSaved->costumeData.eaCostumeSlots[i]->pCostume = StructCloneNoConst(parse_PlayerCostume, pCostume);
		}
				
		// destroy the temp costume
		if (pCostume) {
			StructDestroyNoConst(parse_PlayerCostume, pCostume);
		}

		// set active
		pEnt->pSaved->costumeData.iActiveCostume = 0;

		// Modify uiValidateTage
		++pEnt->pSaved->costumeData.uiValidateTag;
	}
	
	return true;
}


AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Hallegiance, .Hsuballegiance, .Psaved.Costumedata.Iactivecostume, .Pchar.Hspecies, .Egender, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Costumedata.Uivalidatetag, .Psaved.Costumedata.Eacostumeslots, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome costume_tr_CostumeAndCredit(ATR_ARGS, NOCONST(Entity)* pEnt, S32 amount, U32 uiValidateTag, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if(costume_trh_CostumeCredit(ATR_PASS_ARGS, pEnt, amount, pReason))
	{
		if(costume_trh_CostumeReplace(ATR_PASS_ARGS, pEnt, uiValidateTag, pExtract))
		{
			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}
	
	TRANSACTION_RETURN_LOG_FAILURE("Unable to change costume and credit");
}


static void costumetransaction_CostumeReplace_CB(TransactionReturnVal* returnVal, void* pData)
{
	EntityRef *pRef = (EntityRef*)pData;
	Entity *pPlayerEnt = entFromEntityRefAnyPartition(*pRef);

	if(pPlayerEnt)
	{
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			// was successful
			costumeEntity_ResetStoredCostume(pPlayerEnt); // Copy costume up to public area
			costumeEntity_RegenerateCostume(pPlayerEnt);     // Regenerate costume after change
			costumeEntity_SetCostumeRefDirty(pPlayerEnt);
			
			entity_SetDirtyBit(pPlayerEnt, parse_PlayerCostumeData, &pPlayerEnt->pSaved->costumeData, true);
			entity_SetDirtyBit(pPlayerEnt, parse_SavedEntityData, pPlayerEnt->pSaved, true);
			
			// send mail
			if (pPlayerEnt->pPlayer){
				gslMailNPC_AddMail(pPlayerEnt,
					langTranslateMessageKeyDefault(entGetLanguage(pPlayerEnt), "Player_Costume_Replace_From_Name", "[UNTRANSLATED]CSR"),
					langTranslateMessageKeyDefault(entGetLanguage(pPlayerEnt), "Player_Costume_Replace_Subject", "[UNTRANSLATED]Bad Costume."),
					langTranslateMessageKeyDefault(entGetLanguage(pPlayerEnt), "Player_Costume_Replace_Body", "[UNTRANSLATED]Your bad costume has been replaced with a default costume by customer service. You may go to the costume tailor and fix it for free.")
					);
			}
		}
		else
		{
			// send fail
		}
	}

	SAFE_FREE(pRef);
}


void costumetransaction_CostumeAndCredit(Entity *pEnt)
{
	TransactionReturnVal* returnVal;
	EntityRef *pRef = calloc(1, sizeof(EntityRef));
	int iCount;
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemChangeReason reason = {0};

	// give player costume (transaction) and free costume change
	*pRef = entGetRef(pEnt);
	iCount = eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots);
	returnVal = LoggedTransactions_CreateManagedReturnValEnt("PlayerCostumeReplace", pEnt, costumetransaction_CostumeReplace_CB, pRef);

	inv_FillItemChangeReason(&reason, pEnt, "Costume:CostumeAndCredit", "Player");

	AutoTrans_costume_tr_CostumeAndCredit(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			iCount, pEnt->pSaved->costumeData.uiValidateTag, &reason, pExtract);
}


AUTO_TRANSACTION
ATR_LOCKS(pEntOwner, ".Psaved.Ugamespecificfixupversion")
ATR_LOCKS(pEnt, ".Psaved.Costumedata.Uivalidatetag, .Psaved.Ugamespecificfixupversion, .Psaved.Costumedata.Iactivecostume, .Psaved.Costumedata.Pcslotset, .Psaved.Costumedata.Islotsetversion, .Psaved.Costumedata.Eacostumeslots");
enumTransactionOutcome costume_tr_FixupCostumeSlots(ATR_ARGS, NOCONST(Entity)* pEntOwner, NOCONST(Entity)* pEnt, const char *pcSlotSet, U32 uiValidateTag)
{
	if ((pEnt->pSaved->costumeData.uiValidateTag == uiValidateTag) &&
		costumeEntity_trh_FixupCostumeSlots(ATR_PASS_ARGS, pEntOwner, pEnt, pcSlotSet)
		) {
		++pEnt->pSaved->costumeData.uiValidateTag;
		return TRANSACTION_OUTCOME_SUCCESS;
	} else {
		return TRANSACTION_OUTCOME_FAILURE;
	}
}

bool costumetransaction_ShouldUpdateCostumeSlots(Entity *pEnt, bool bIsPet)
{
	PCSlotSet *pSlotSet;

	pSlotSet = costumeEntity_GetSlotSet(pEnt, bIsPet);
	if (!pEnt || !pEnt->pSaved ||
		(!pSlotSet && !pEnt->pSaved->costumeData.pcSlotSet) ||
		(pSlotSet &&
		 (pSlotSet->pcName == pEnt->pSaved->costumeData.pcSlotSet) &&
		 (pSlotSet->iSetVersion == pEnt->pSaved->costumeData.iSlotSetVersion))) {
		// No change required
		return false;
	}
	return true;
}

// "bIsPet" should be true for pets but not for puppets or players
void costumetransaction_FixupCostumeSlots(Entity *pOwnerEnt, Entity *pEnt, bool bIsPet)
{
	TransactionReturnVal *pReturn;
	CostumeChangeData *data;
	PCSlotSet *pSlotSet;

	if (!costumetransaction_ShouldUpdateCostumeSlots(pEnt, bIsPet)) {
		return;
	}

	data = calloc(1, sizeof(*data));
	data->entRef = pEnt->myRef;
	pSlotSet = costumeEntity_GetSlotSet(pEnt, bIsPet);
	pReturn = objCreateManagedReturnVal(costumetransaction_ChangePlayerCostumeCallback, data);
	AutoTrans_costume_tr_FixupCostumeSlots(pReturn, GetAppGlobalType(), entGetType(pOwnerEnt), entGetContainerID(pOwnerEnt), entGetType(pEnt), entGetContainerID(pEnt), pSlotSet ? pSlotSet->pcName : NULL, pEnt->pSaved->costumeData.uiValidateTag);
}
