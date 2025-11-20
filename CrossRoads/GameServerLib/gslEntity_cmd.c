/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character.h"
#include "CharacterClass.h"
#include "cmdparse.h"
#include "Entity.h"
#include "EntityLib.h"
#include "gslEntity.h"
#include "gslSendToClient.h"
#include "objTransactions.h"
#include "Player.h"
#include "EntitySavedData.h"
#include "rewardCommon.h"
#include "autogen/GameClientLib_AutoGen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/Player_h_ast.h"
#include "CostumeCommonEntity.h"


// ----------------------------------------------------------------------------------
// Debugging Commands
// ----------------------------------------------------------------------------------

AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_SERVERCMD;
void KillTarget(Entity *pEntity)
{
	if(pEntity)
		entCon(pEntity, "selected", "killwithdamage");
}

// This sets the player to be invincible and untargetable.  It's just an easily-remembered shorthand.
AUTO_COMMAND ACMD_NAME(GodMode) ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD;
void player_CmdGodMode(Entity *pPlayerEnt, int iSet)
{
	if (pPlayerEnt && pPlayerEnt->pChar) {
		gslEntityGodMode(pPlayerEnt, iSet);
		if (iSet) {
			ClientCmd_GodModeClient(pPlayerEnt, pPlayerEnt->pChar->bInvulnerable);
		}
	}
}


AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(GodMode);
void GodModeOn(Entity *pPlayerEnt)
{
	player_CmdGodMode(pPlayerEnt, 1);
}


AUTO_COMMAND_REMOTE;
void player_RemoteSendUpdatedClassList(CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		if (pEnt) {
			characterclasses_SendClassList(pEnt);
		}
	}
}

static void SetRetconConversions_CB(TransactionReturnVal *pVal, void* userData)
{
	Entity *pEnt = entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData);
	if(pEnt)
	{
		gslSendPrintf(pEnt, "SetRetconConversions: [%s].",
			pVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS ? "Success" : "Failure");
	}
}

AUTO_COMMAND ACMD_NAME(SetRetconConversions) ACMD_ACCESSLEVEL(7);
void player_SetRespecConversions(CmdContext *pContext, Entity *pEnt, int iVal)
{
	if(iVal >=0 && iVal <= U8_MAX)
	{
		TransactionReturnVal *pVal = objCreateManagedReturnVal(SetRetconConversions_CB, (void*)(intptr_t)entGetRef(pEnt));
		AutoTrans_trEntity_SetRespecConversions(pVal, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), iVal);
	}
	else
	{
		estrConcatf(pContext->output_msg, "SetRetconConversions: Invalid value [%d], it must be between [%d] and [%d]",
			iVal,
			0,
			U8_MAX);
	}
}

static void PlayerSetPlayerType_CB(TransactionReturnVal *pVal, void* userData)
{
	Entity *pEnt = entFromEntityRefAnyPartition((EntityRef)(intptr_t)userData);
	if(pEnt)
	{
		gslSendPrintf(pEnt, "PlayerSetPlayerType: [%s].",
			pVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS ? "Success" : "Failure");
	}
}

// change the player type of the entity
AUTO_COMMAND ACMD_NAME(PlayerSetPlayerType) ACMD_ACCESSLEVEL(7);
void PlayerSetPlayerType(Entity *pEntity, int iPlayerType)
{
	if(pEntity && pEntity->pPlayer && iPlayerType >= kPlayerType_Standard && iPlayerType <= kPlayerType_SuperPremium)
	{
		TransactionReturnVal *pVal = objCreateManagedReturnVal(PlayerSetPlayerType_CB, (void*)(intptr_t)entGetRef(pEntity));
		AutoTrans_trEntity_SetPlayerType(pVal, GetAppGlobalType(), entGetType(pEntity), entGetContainerID(pEntity), iPlayerType);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0);
void ToggleGoldenPath(Entity* pEnt)
{
	pEnt->pPlayer->pUI->bGoldenPathActive = !pEnt->pPlayer->pUI->bGoldenPathActive;
	entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, false);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE ACMD_SERVERONLY ACMD_CATEGORY(Interface);
void gslPlayerUI_SetLevelUpWizardDismissedAt(Entity *pEnt, U32 iLevel)
{
	if (SAFE_MEMBER2(pEnt, pPlayer, pUI))
	{
		pEnt->pPlayer->pUI->uiLevelUpWizardDismissedAt = (U8)iLevel;
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, false);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE ACMD_SERVERONLY ACMD_CATEGORY(Interface);
void gslPlayerUI_SetLastLevelPowersMenuOpenAt(Entity *pEnt, U32 iLevel)
{
	if (SAFE_MEMBER2(pEnt, pPlayer, pUI))
	{
		pEnt->pPlayer->pUI->uiLastLevelPowersMenuAt = (U8)iLevel;
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, false);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE ACMD_SERVERONLY ACMD_CATEGORY(Interface);
void gslEntity_SetCostumeSetIndexToShow(Entity *pEnt, U32 iCostumeSetIndexToShow)
{
	if (SAFE_MEMBER(pEnt, pSaved))
	{
		pEnt->pSaved->iCostumeSetIndexToShow = (U8)iCostumeSetIndexToShow;
		entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, false);
		costumeEntity_RegenerateCostume(pEnt);
	}
}

// Used for testing gated commands (cooldown left)
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD;
U32 gslPlayer_GetRewardGatedCooldownSeconds(Entity *pEntPlayer, S32 gatedType)
{
	U32 uTm = Reward_GetGatedCooldown(pEntPlayer, gatedType);
	return uTm;
};