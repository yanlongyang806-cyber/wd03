/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLoginCharacterSelect.h"
#include "aslLoginEntityMigrate.h"
#include "aslLoginServer.h"
#include "AutoTransDefs.h"
#include "CostumeCommonLoad.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "LocalTransactionManager.h"
#include "LoginCommon.h"
#include "logging.h"
#include "objTransactions.h"
#include "TransactionOutcomes.h"
#include "inventorycommon.h"
#include "player.h"

#include "player_h_ast.h"
#include "Entity_h_ast.h"
#include "EntitySavedData_h_ast.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"

typedef struct PetMigrateData PetMigrateData;

typedef struct PlayerMigrateData
{
	int loginCookie;
	GlobalType iType;
	ContainerID iID;
	int iMigrateTag;
	int iPlayerAccountID;

	PetMigrateData **eaPetData;
} PlayerMigrateData;

typedef struct PetMigrateData
{
	int loginCookie;
	GlobalType iType;
	ContainerID iID;

	PlayerMigrateData *pPlayerData;
} PetMigrateData;

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Obsolete_Costumedata, .Psaved.Costumedata, .Psaved.Ufixupversion");
void entity_trh_MigrateV4toV5(ATH_ARG NOCONST(Entity) *pEnt)
{
	// Copy data from old costume location to new location
	if (pEnt->pSaved->obsolete_costumeData) {
		NOCONST(PlayerCostumeListsV0) *pLists = pEnt->pSaved->obsolete_costumeData;
		NOCONST(PlayerCostumeData) *pData = &pEnt->pSaved->costumeData;
		int i;

		// Copy over active costume choice
		pData->iActiveCostume = pLists->activePrimaryCostume;

		// Copy over costumes
		for(i=0; i<eaSize(&pLists->eaPrimaryCostumes); ++i) {
			NOCONST(PlayerCostumeSlot) *pSlot = StructCreateNoConst(parse_PlayerCostumeSlot);
			pSlot->pCostume = costumeLoad_UpgradeCostumeV0toV5(pLists->eaPrimaryCostumes[i]);
			eaPush(&pData->eaCostumeSlots, pSlot);
		}

		// Copy over costume unlocks
		for(i=0; i<eaSize(&pLists->eaUnlockedCostumeRefs); ++i) {
			NOCONST(PlayerCostumeRef) *pRef = StructCreateNoConst(parse_PlayerCostumeRef);
			COPY_HANDLE(pRef->hCostume, pLists->eaUnlockedCostumeRefs[i]->hCostume);
			eaPush(&pData->eaUnlockedCostumeRefs, pRef);
		}

		// Ignore pLists->activeSecondaryCostume
		// Ignore pLists->activeCostumeType
		// Ignore pLists->eaSecondaryCostumes

		// Destroy the old V0 data
		StructDestroyNoConstSafe(parse_PlayerCostumeListsV0, &pEnt->pSaved->obsolete_costumeData);
	}

	// Set to version 1
	pEnt->pSaved->uFixupVersion = 1;
}


AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.Pscpdata, .Pinventoryv1_Deprecated, .Pplayer.Pemailv1_Deprecated, .Pplayer.pEmailV2, .Psaved.Ufixupversion, .Pchar.Hclass, .Pchar.Ilevelexp, .Psaved.Ppbuilds, .Pinventoryv2, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Pugckillcreditlimit, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Uiindexbuild, .Pplayer.Playertype");
void entity_trh_MigrateV5toV6(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt)
{
	if (pEnt->pInventoryV1_Deprecated)
	{
		if (!inv_ent_trh_VerifyInventoryData(ATR_EMPTY_ARGS, pEnt, true, true, NULL))
		{
			Errorf("Entity migration couldn't find a default inventory for entity %s. Forcefully creating an empty inventory.", pEnt->debugName);
			inv_ent_trh_AddInventory(ATR_PASS_ARGS, pEnt);
		}
		inv_trh_ent_MigrateInventoryV1ToV2(ATR_EMPTY_ARGS, pEnt);
		StructDestroyNoConstSafe(parse_InventoryV1, &pEnt->pInventoryV1_Deprecated);
	}

	//migrate player NPCmail data
	if (NONNULL(pEnt->pPlayer) && pEnt->pPlayer->pEmailV1_Deprecated)
	{
		NOCONST(NPCEMail)* pNewEmail = StructCreateNoConst(parse_NPCEMail);
		NOCONST(NPCEMailV1)* pOldEmail = pEnt->pPlayer->pEmailV1_Deprecated;
		int i, j;
		pNewEmail->iLastUsedID = pOldEmail->iLastUsedID;
		pNewEmail->bReadAll = pOldEmail->bReadAll;
		pNewEmail->uLastSyncTime = pOldEmail->uLastSyncTime;
		for (i = 0; i < eaSize(&pOldEmail->mail); i++)
		{
			eaPush(&pNewEmail->mail, StructCreateNoConst(parse_NPCEMailData));
			pNewEmail->mail[i]->body = pOldEmail->mail[i]->body;
			pOldEmail->mail[i]->body = NULL;
			pNewEmail->mail[i]->fromName = pOldEmail->mail[i]->fromName;
			pOldEmail->mail[i]->fromName = NULL;
			pNewEmail->mail[i]->subject = pOldEmail->mail[i]->subject;
			pOldEmail->mail[i]->subject = NULL;
			pNewEmail->mail[i]->sentTime = pOldEmail->mail[i]->sentTime;
			pNewEmail->mail[i]->iNPCEMailID = pOldEmail->mail[i]->iNPCEMailID;
			pNewEmail->mail[i]->bRead = pOldEmail->mail[i]->bRead;
			for (j = 0; j < eaSize(&pOldEmail->mail[i]->ppItemSlot); j++)
			{
				eaPush(&pNewEmail->mail[i]->ppItemSlot, StructCreateNoConst(parse_InventorySlot));
				inv_trh_ent_MigrateSlotV1ToV2(ATR_EMPTY_ARGS, pOldEmail->mail[i]->ppItemSlot[j], pNewEmail->mail[i]->ppItemSlot[j]);
			}
		}

		pEnt->pPlayer->pEmailV2 = pNewEmail;
		StructDestroyNoConst(parse_NPCEMailV1, pEnt->pPlayer->pEmailV1_Deprecated);
		pEnt->pPlayer->pEmailV1_Deprecated = NULL;
	}
	if (NONNULL(pEnt->pSaved))
		pEnt->pSaved->uFixupVersion = 6;
}


AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.Pscpdata, .Psaved.Ufixupversion, .Psaved.Obsolete_Costumedata, .Psaved.Costumedata, .Pinventoryv1_Deprecated, .Pplayer.Pemailv1_Deprecated, .Pplayer.pEmailV2, .Pchar.Hclass, .Pchar.Ilevelexp, .Psaved.Ppbuilds, .Pinventoryv2, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Pugckillcreditlimit, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Uiindexbuild, .Pplayer.Playertype");
enumTransactionOutcome entity_tr_MigrateEntityVersion(ATR_ARGS, NOCONST(Entity)* pEnt)
{
	//if (pEnt->pSaved->uFixupVersion < 1) {
	//}
	//if (pEnt->pSaved->uFixupVersion < 2) {
	//}
	//if (pEnt->pSaved->uFixupVersion < 3) {
	//}
	//if (pEnt->pSaved->uFixupVersion < 4) {
	//}
	if (NONNULL(pEnt->pSaved))
	{
		if (pEnt->pSaved->uFixupVersion < 5) {
			entity_trh_MigrateV4toV5(pEnt);
		}
		if (pEnt->pSaved->uFixupVersion < 6) {
			entity_trh_MigrateV5toV6(ATR_PASS_ARGS, pEnt);
		}
		pEnt->pSaved->uFixupVersion = CURRENT_ENTITY_FIXUP_VERSION;
	} 
	else if(pEnt->myEntityType == GLOBALTYPE_ENTITYSHAREDBANK && ISNULL(pEnt->pInventoryV2))
	{
		if (pEnt->pInventoryV1_Deprecated)
		{
			inv_ent_trh_InitAndFixupInventory(ATR_PASS_ARGS, pEnt, (DefaultInventory*)RefSystem_ReferentFromString(g_hDefaultInventoryDict,"SharedBank"),true, false, NULL);
			inv_trh_ent_MigrateInventoryV1ToV2(ATR_EMPTY_ARGS, pEnt);
			StructDestroyNoConstSafe(parse_InventoryV1, &pEnt->pInventoryV1_Deprecated);
			return TRANSACTION_OUTCOME_SUCCESS;

		}

	}


	return TRANSACTION_OUTCOME_SUCCESS;
}

