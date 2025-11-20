/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/



#include "CombatDebug.h"
#include "AutoGen/CombatDebug_h_ast.h"
#include "CombatDebugViewer.h"
#include "GfxHeadshot.h"
#include "GfxTexturesPublic.h"
#include "GraphicsLib.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowersMovement.h"
#include "PowerTree.h"
#include "Character_combat.h" // For TEMP_character_ActivatePowerByName
#include "Character.h"
#include "CharacterAttribs.h"
#include "Character_tick.h"
#include "entCritter.h"
#include "EntitySavedData.h"
#include "nemesis_common.h"
#include "contact_common.h"
#include "player.h"
#include "dynFxInterface.h"
#include "UITray.h"
#include "GameAccountDataCommon.h"
#include "CostumeCommonLoad.h"

#include "UIGen.h"

#include "wlInteraction.h"

#include "structInternals.h"
#include "AutoGen/cmdClient_c_ast.h"
#include "AutoGen/Powers_h_ast.h"
#include "AutoGen/AttribMod_h_ast.h"
#include "AutoGen/PowerAnimFX_h_ast.h"
#include "CharacterClass.h"
#include "AutoGen/CharacterClass_h_ast.h"
#include "AutoGen/nemesis_common_h_ast.h"

// All this crap to get one function
#include "GameClientLib.h"
#include "gclEntity.h"

#include "GfxConsole.h"
#include "GfxAlien.h"
#include "Color.h"

#include "soundLib.h"
#include "CombatConfig.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););


// Implements the game-specific functions defined in gclCommandParse.h

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_I_AM_THE_ERROR_FUNCTION_FOR(CombatPredictionDisable);
void CombatPredictionStatusError()
{
	conPrintf("Prediction Status: %s", g_bCombatPredictionDisabled ? "Off" : "On");
}
// Disables client-side combat prediction
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_HIDE;
void CombatPredictionDisable(S32 disable)
{
	g_bCombatPredictionDisabled = !!disable;
}

// Activate a power by name
AUTO_COMMAND ACMD_NAME(Power_Exec) ACMD_ACCESSLEVEL(0);
void entUsePower(int start, const char* powerName)
{
	Entity* ent = entActivePlayerPtr();

	if(ent){
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(ent);

		// Shortcut this here for now, so the server doesn't even get
		//  NULL should be current target
		if(ent->pChar && character_ActivatePowerByNameClient(entGetPartitionIdx(ent), ent->pChar, powerName, NULL, NULL, start, pExtract))
		{
		}
	}
}

// Activate a power by id
AUTO_COMMAND ACMD_NAME(Power_Exec_ID) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void entUsePowerID(int start, U32 id)
{
	Entity* ent = entActivePlayerPtr();

	if(ent){
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(ent);

		// Shortcut this here for now, so the server doesn't even get
		//  NULL should be current target
		if(ent->pChar && character_ActivatePowerByIDClient(entGetPartitionIdx(ent), ent->pChar, id, NULL, NULL, start, pExtract))
		{
		}
	}
}

// Activate a power by category
AUTO_COMMAND ACMD_NAME(Power_Exec_Category) ACMD_ACCESSLEVEL(0);
void entUsePowerCategory(int start, const char *cpchCategory)
{
	Entity *e = entActivePlayerPtr();
	if(e && e->pChar)
	{
		Power *ppow = character_FindPowerByCategory(e->pChar,cpchCategory);
		if(ppow)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);

			character_ActivatePowerByIDClient(entGetPartitionIdx(e), e->pChar, ppow->uiID, NULL, NULL, start, pExtract);
		}
	}
}

// Activate a power by category
AUTO_COMMAND ACMD_NAME(PowerExecCategoryIfActivatable) ACMD_ACCESSLEVEL(0);
void entUsePowerCategoryIfActivatable(int start, const char *cpchCategory)
{
	Entity *e = entActivePlayerPtr();
	if(e && e->pChar)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);

		Power *ppow = character_FindPowerByCategory(e->pChar,cpchCategory);
		if(ppow && EntIsPowerTrayActivatable(e->pChar, ppow, NULL, pExtract))
		{
			character_ActivatePowerByIDClient(PARTITION_CLIENT, e->pChar, ppow->uiID, NULL, NULL, start, pExtract);
		}
	}
}

/*
AUTO_COMMAND;
void combatDump(void)
{
	int idefs = 0, imods = 0, imodsDerived = 0, igen = 0;
	size_t sizedefs = 0, sizemods = 0, sizemodsDerived = 0, sizegen = 0;

	RefDictIterator iter;

	PowerDef *pdef;
	PowerAnimFX *pafx;
	CharacterClass *pclass;

	RefSystem_InitRefDictIterator(g_hPowerDefDict, &iter);
	while(pdef = (PowerDef*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		int j;
		printf("%s %s\n",pdef->pchName,pdef->pchGroup);
		idefs++;
		sizedefs += StructGetMemoryUsage(parse_PowerDef,pdef);
		imods += eaSize(&pdef->ppOrderedMods);
		for(j=eaSize(&pdef->ppOrderedMods)-1; j>=0; j--)
		{
			size_t size = StructGetMemoryUsage(parse_AttribModDef,pdef->ppOrderedMods[j]);
			sizemods += size;
			if(pdef->ppOrderedMods[j]->bDerivedInternally)
			{
				imodsDerived++;
				sizemodsDerived += size;
			}
		}
	}
	printf("%d powerdefs (%d)\n%d moddefs (%d)\n%d derived moddefs (%d)\n",idefs,sizedefs,imods,sizemods,imodsDerived,sizemodsDerived);

	igen = 0; sizegen = 0;
	RefSystem_InitRefDictIterator(g_hPowerAnimFXDict, &iter);
	while(pafx = (PowerAnimFX*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		igen++;
		sizegen += StructGetMemoryUsage(parse_PowerAnimFX,pafx);
	}
	printf("%d powerart (%d)\n",igen,sizegen);

	igen = 0; sizegen = 0;
	RefSystem_InitRefDictIterator(g_hCharacterClassDict, &iter);
	while(pclass = (CharacterClass*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		igen++;
		sizegen += StructGetMemoryUsage(parse_CharacterClass,pclass);
	}
	printf("%d class (%d)\n",igen,sizegen);
}
*/

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(PowersResetArray) ACMD_PRIVATE ACMD_CLIENTCMD;
void cmdPowersResetArray(U32 er)
{
	Entity* ent = er ? entFromEntityRefAnyPartition(er) : entActivePlayerPtr();
	if(ent && ent->pChar && ent->pSaved)
	{
		// The actual reset call
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(ent);
		character_ResetPowersArray(entGetPartitionIdx(ent), ent->pChar, pExtract);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(PowersDirtyInnates) ACMD_PRIVATE ACMD_CLIENTCMD;
void cmdPowersDirtyInnates(U32 er)
{
	Entity* ent = er ? entFromEntityRefAnyPartition(er) : entActivePlayerPtr();
	if(ent && ent->pChar && ent->pSaved)
	{
		// The actual dirty call
		character_DirtyInnatePowers(ent->pChar);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void SetCooldownGlobalClient(F32 fTime)
{
	Entity *e = entActivePlayerPtr();

	if(e && e->pChar)
	{
		e->pChar->fCooldownGlobalTimer = fTime;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void PowersSetCooldownClient(S32 iCategory, F32 fTime)
{
	Entity *e = entActivePlayerPtr();

	if(e && e->pChar)
	{
		character_CategorySetCooldown(PARTITION_CLIENT,e->pChar,iCategory,fTime);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void PowersSetPetCooldownClient(U32 erPet, S32 iCategory, F32 fTime)
{
	Entity *e = entActivePlayerPtr();
	if(e && e->pPlayer)
	{
		PetCooldownTimer *pTimer = player_GetPetCooldownTimerForCategory(e->pPlayer, erPet, iCategory);
		if(pTimer)
		{
			pTimer->fCooldown = fTime;
		}
		else
		{
			PlayerPetInfo *pInfo = player_GetPetInfo(e->pPlayer, erPet);
			if(pInfo)
			{
				pTimer = StructCreate(parse_PetCooldownTimer);
				pTimer->iPowerCategory = iCategory;
				pTimer->fCooldown = fTime;
				eaPush(&pInfo->ppCooldownTimers, pTimer);
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void PowerSetRechargeClient(U32 uiID, F32 fTime)
{
	Entity* e = entActivePlayerPtr();

	if(e && e->pChar)
	{
		if(!uiID)
		{
			character_PowerSetRecharge(entGetPartitionIdx(e),e->pChar,uiID,fTime);
		}
		else
		{
			Power *ppow = character_FindPowerByIDComplete(e->pChar,uiID);

			if(ppow)
				power_SetRecharge(entGetPartitionIdx(e),e->pChar,ppow,fTime);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void PowerSetChargeRefillClient(U32 uiID, S32 iIdxSub, S16 iLinkedSub)
{
	Entity* e = entActivePlayerPtr();

	if (e && e->pChar)
	{
		PowerRef ref = {0};
		Power *pPow;

		ref.uiID = uiID;
		ref.iIdxSub = iIdxSub;
		ref.iLinkedSub = iLinkedSub;

		pPow = character_FindPowerByRef(e->pChar, &ref);
		
		if (pPow)
		{
			PowerDef *pDef = GET_REF(pPow->hDef);

			if (pDef &&
				pDef->fChargeRefillInterval > 0.f)
			{
				pPow->fTimeChargeRefill = pDef->fChargeRefillInterval;

				if (power_FindInRefs(pPow, &e->pChar->ppPowerRefChargeRefill) < 0)
				{
					// Add to the countdown tracking list
					eaPush(&e->pChar->ppPowerRefChargeRefill, power_CreateRef(pPow));
				}
			}
		}
	}
}

// Temporary hacky way to tell the Player about a modified recharge time for one
//  of its pet's Powers
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void PowerPetSetRechargeClient(U32 erPet, const char *pchPowerDefName, F32 fTime)
{
	Entity *e = entActivePlayerPtr();
	PowerDef *pdef = powerdef_Find(pchPowerDefName);
	if(e && e->pPlayer && pdef)
	{
		PetPowerState *pState;
		ANALYSIS_ASSUME(e);
		ANALYSIS_ASSUME(e->pPlayer);
		ANALYSIS_ASSUME(pdef);
		pState = player_GetPetPowerState(e->pPlayer,erPet,pdef);
		if(pState)
		{
			pState->fTimerRecharge = fTime;
			pState->fTimerRechargeBase = fTime;
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowerActivationFailedCooldown(U32 uiActID, S32 iCategory, F32 fCooldownTime)
{
	Entity *e = entActivePlayerPtr();

	
	if(e && e->pChar)
	{
		Character *pchar = e->pChar;
		int iPartitionIdx = entGetPartitionIdx(e);

		PowersSelectDebug("%s Power Failed due to Cooldown: %d\n",CHARDEBUGNAME(pchar),uiActID);
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Power Failed due to Cooldown: %d\n",uiActID);
		if(pchar->pPowActCurrent && pchar->pPowActCurrent->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Current %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActCurrentCancel(iPartitionIdx,pchar,true,false);
		}
		else if(pchar->pPowActQueued && pchar->pPowActQueued->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Queued %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActQueuedCancel(iPartitionIdx,pchar,NULL,0);
		}
		else if(pchar->pPowActOverflow && pchar->pPowActOverflow->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Overflow %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActOverflowCancel(iPartitionIdx,pchar,NULL,0);
		}
		else
		{
			// Check the toggles
			int i;

			for(i=eaSize(&pchar->ppPowerActToggle)-1; i>=0; i--)
			{
				if(pchar->ppPowerActToggle[i]->uchID==uiActID)
				{
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Toggle %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
					character_DeactivateToggle(iPartitionIdx,pchar,pchar->ppPowerActToggle[i],NULL,pmTimestamp(0),false);
					break;
				}
			}

			if(g_bPowersSelectDebug)
			{
				assertmsgf(i>-1,"No power activations with id %d found!",uiActID);
			}
		}
		character_CategorySetCooldown(PARTITION_CLIENT,pchar,iCategory,fCooldownTime);
	}
}


// Command sent by server to inform the client that a requested PowerActivation failed due to recharge
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowerActivationFailedRecharge(U32 uiActID, U32 uiPowerID, F32 fTime)
{
	Entity* e = entActivePlayerPtr();

	
	if(e && e->pChar)
	{
		Character *pchar = e->pChar;
		int iPartitionIdx = entGetPartitionIdx(e);

		PowersSelectDebug("%s Power Failed due to Recharge: %d\n",CHARDEBUGNAME(pchar),uiActID);

		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Power Failed due to Recharge: %d\n",uiActID);
		if(pchar->pPowActCurrent && pchar->pPowActCurrent->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Current %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActCurrentCancel(iPartitionIdx,pchar,true,false);
		}
		else if(pchar->pPowActQueued && pchar->pPowActQueued->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Queued %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActQueuedCancel(iPartitionIdx,pchar,NULL,0);
		}
		else if(pchar->pPowActOverflow && pchar->pPowActOverflow->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Overflow %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActOverflowCancel(iPartitionIdx,pchar,NULL,0);
		}
		else
		{
			// Check the toggles
			int i;

			for(i=eaSize(&pchar->ppPowerActToggle)-1; i>=0; i--)
			{
				if(pchar->ppPowerActToggle[i]->uchID==uiActID)
				{
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Toggle %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
					character_DeactivateToggle(iPartitionIdx,pchar,pchar->ppPowerActToggle[i],NULL,pmTimestamp(0),false);
					break;
				}
			}

			if(g_bPowersSelectDebug)
			{
				assertmsgf(i>-1,"No power activations with id %d found!",uiActID);
			}
		}
		character_PowerSetRecharge(iPartitionIdx,pchar,uiPowerID,fTime);
	}
}

// Command sent by server to inform the client that a requested PowerActivation failed due to cost
// TODO: There is a lot of copy/paste with these PowerActivationFailed functions. They need to be refactored.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowerActivationFailedNoChargesRemaining(U32 uiActID)
{
	Entity* e = entActivePlayerPtr();

	if(e && e->pChar)
	{
		Character *pchar = e->pChar;
		int iPartitionIdx = entGetPartitionIdx(e);

		PowersSelectDebug("%s Power Failed due to no charges remaining: %d\n",CHARDEBUGNAME(pchar),uiActID);

		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Power Failed due to no charges remaining: %d\n",uiActID);
		if(pchar->pPowActCurrent && pchar->pPowActCurrent->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Current %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActCurrentCancel(iPartitionIdx,pchar,true,false);
		}
		else if(pchar->pPowActQueued && pchar->pPowActQueued->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Queued %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActQueuedCancel(iPartitionIdx,pchar,NULL,0);
		}
		else if(pchar->pPowActOverflow && pchar->pPowActOverflow->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Overflow %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActOverflowCancel(iPartitionIdx,pchar,NULL,0);
		}
		else
		{
			// Check the toggles
			int i;

			for(i=eaSize(&pchar->ppPowerActToggle)-1; i>=0; i--)
			{
				if(pchar->ppPowerActToggle[i]->uchID==uiActID)
				{
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Toggle %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
					character_DeactivateToggle(iPartitionIdx,pchar,pchar->ppPowerActToggle[i],NULL,pmTimestamp(0),false);
					break;
				}
			}

			if(g_bPowersSelectDebug)
			{
				assertmsgf(i>-1,"No power activations with id %d found!",uiActID);
			}
		}
	}
}

// Command sent by server to inform the client that a requested PowerActivation failed due to cost
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowerActivationFailedCost(U32 uiActID)
{
	Entity* e = entActivePlayerPtr();

	
	if(e && e->pChar)
	{
		Character *pchar = e->pChar;
		int iPartitionIdx = entGetPartitionIdx(e);

		PowersSelectDebug("%s Power Failed due to Cost: %d\n",CHARDEBUGNAME(pchar),uiActID);

		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Power Failed due to Cost: %d\n",uiActID);
		if(pchar->pPowActCurrent && pchar->pPowActCurrent->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Current %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActCurrentCancel(iPartitionIdx,pchar,true,false);
		}
		else if(pchar->pPowActQueued && pchar->pPowActQueued->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Queued %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActQueuedCancel(iPartitionIdx,pchar,NULL,0);
		}
		else if(pchar->pPowActOverflow && pchar->pPowActOverflow->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Overflow %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActOverflowCancel(iPartitionIdx,pchar,NULL,0);
		}
		else
		{
			// Check the toggles
			int i;

			for(i=eaSize(&pchar->ppPowerActToggle)-1; i>=0; i--)
			{
				if(pchar->ppPowerActToggle[i]->uchID==uiActID)
				{
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Toggle %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
					character_DeactivateToggle(iPartitionIdx,pchar,pchar->ppPowerActToggle[i],NULL,pmTimestamp(0),false);
					break;
				}
			}

			if(g_bPowersSelectDebug)
			{
				assertmsgf(i>-1,"No power activations with id %d found!",uiActID);
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowerActivationFailedOther(U32 uiActID)
{
	Entity* e = entActivePlayerPtr();
	
	if(e && e->pChar)
	{
		Character *pchar = e->pChar;
		int iPartitionIdx = entGetPartitionIdx(e);

		PowersSelectDebug("%s Power Failed due to Other: %d\n",CHARDEBUGNAME(pchar),uiActID);

		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Power Failed due to Other: %d\n",uiActID);		
		if(pchar->pPowActCurrent && pchar->pPowActCurrent->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Current %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActCurrentCancel(iPartitionIdx,pchar,true,false);
		}
		else if(pchar->pPowActQueued && pchar->pPowActQueued->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Queued %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActQueuedCancel(iPartitionIdx,pchar,NULL,0);
		}
		else if(pchar->pPowActOverflow && pchar->pPowActOverflow->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Overflow %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActOverflowCancel(iPartitionIdx,pchar,NULL,0);
		}
		else
		{
			// Check the toggles
			int i;

			for(i=eaSize(&pchar->ppPowerActToggle)-1; i>=0; i--)
			{
				if(pchar->ppPowerActToggle[i]->uchID==uiActID)
				{
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Toggle %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
					character_DeactivateToggle(iPartitionIdx,pchar,pchar->ppPowerActToggle[i],NULL,pmTimestamp(0),false);
					break;
				}
			}

			if(g_bPowersSelectDebug)
			{
				assertmsgf(i>-1,"No power activations with id %d found!",uiActID);
			}
		}
	}
}

// Toggles BattleForm on or off
//AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRODUCTS(Creatures);
//void BattleFormToggle()
//{
//	Entity *e = entActivePlayerPtr();
//	if(e && e->pChar && character_CanToggleBattleForm(e->pChar))
//		ServerCmd_BattleForm(!e->pChar->bBattleForm);
//}

// When we get a combo mispredict, we cancel all the powers on the client with that ID.
// Safest thing to do right now
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void PowerActivationComboMispredict(U32 uiActID)
{
	Entity* e = entActivePlayerPtr();


	if(e && e->pChar)
	{
		Character *pchar = e->pChar;
		int iPartitionIdx = entGetPartitionIdx(e);

		PowersSelectDebug("%s Power mispredicted: %d\n",CHARDEBUGNAME(pchar),uiActID);

		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Power Mispredicted: %d\n",uiActID);		
		if(pchar->pPowActCurrent && pchar->pPowActCurrent->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Current %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActCurrentCancel(iPartitionIdx,pchar,true,false);
		}
		else if(pchar->pPowActQueued && pchar->pPowActQueued->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Queued %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActQueuedCancel(iPartitionIdx,pchar,NULL,0);
		}
		else if(pchar->pPowActOverflow && pchar->pPowActOverflow->uchID==uiActID)
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Overflow %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
			character_ActOverflowCancel(iPartitionIdx,pchar,NULL,0);
		}
		else
		{
			// Check the toggles
			int i;

			for(i=eaSize(&pchar->ppPowerActToggle)-1; i>=0; i--)
			{
				if(pchar->ppPowerActToggle[i]->uchID==uiActID)
				{
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, e, "Toggle %d CancelPowers: %d\n",uiActID,pmTimestamp(0));
					character_DeactivateToggle(iPartitionIdx,pchar,pchar->ppPowerActToggle[i],NULL,pmTimestamp(0),false);
					break;
				}
			}

			if(g_bPowersSelectDebug)
			{
				assertmsgf(i>-1,"No power activations with id %d found!",uiActID);
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void combatDebugEnt(Entity *e, const char *pchTarget)
{
	EntityRef erTarget = 0;
	entGetClientTarget(e,pchTarget,&erTarget);
	ServerCmd_combatDebugEntServer(erTarget);

	g_erCombatDebugEntRef = erTarget;
}

// Send a gen to display.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME("GenShowDef") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void ui_GenCmdShowDef(const ACMD_SENTENCE pchDef)
{
	UIGen *pGen = ui_GenCreateFromString(pchDef);
	if(pGen)
	{
		ui_GenAddWindow(pGen);
	}
}


// Command to recieve the list of nemesis power sets from the server
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void PopulateNemesisPowerSetList(NemesisPowerSetList *pList)
{
	int i;
	
	if (g_NemesisPowerSetList.sets) {
		for (i = eaSize(&g_NemesisPowerSetList.sets)-1; i >= 0; i--) {
			StructDestroy(parse_NemesisPowerSet, g_NemesisPowerSetList.sets[i]);
		}
		eaClear(&g_NemesisPowerSetList.sets);
	}
	
	for (i = eaSize(&pList->sets)-1; i >= 0; i--) {
		NemesisPowerSet *pPowerSet = StructClone(parse_NemesisPowerSet, pList->sets[i]);
		if (pPowerSet) {
			eaPush(&g_NemesisPowerSetList.sets, pPowerSet);
		}
	}
}

// Command to recieve the list of nemesis minion power sets from the server
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void PopulateNemesisMinionPowerSetList(NemesisMinionPowerSetList *pList)
{
	int i;
	
	if (g_NemesisMinionPowerSetList.sets) {
		for (i = eaSize(&g_NemesisMinionPowerSetList.sets)-1; i >= 0; i--) {
			StructDestroy(parse_NemesisMinionPowerSet, g_NemesisMinionPowerSetList.sets[i]);
		}
		eaClear(&g_NemesisMinionPowerSetList.sets);
	}
	
	for (i = eaSize(&pList->sets)-1; i >= 0; i--) {
		NemesisMinionPowerSet *pPowerSet = StructClone(parse_NemesisMinionPowerSet, pList->sets[i]);
		if (pPowerSet) {
			eaPush(&g_NemesisMinionPowerSetList.sets, pPowerSet);
		}
	}
}

// Command to recieve the list of nemesis minion costume sets from the server
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void PopulateNemesisMinionCostumeSetList(NemesisMinionCostumeSetList *pList)
{
	int i;
	
	if (g_NemesisMinionCostumeSetList.sets) {
		for (i = eaSize(&g_NemesisMinionCostumeSetList.sets)-1; i >= 0; i--) {
			StructDestroy(parse_NemesisMinionCostumeSet, g_NemesisMinionCostumeSetList.sets[i]);
		}
		eaClear(&g_NemesisMinionCostumeSetList.sets);
	}
	
	for (i = eaSize(&pList->sets)-1; i >= 0; i--) {
		NemesisMinionCostumeSet *pCostumeSet = StructClone(parse_NemesisMinionCostumeSet, pList->sets[i]);
		if (pCostumeSet) {
			eaPush(&g_NemesisMinionCostumeSetList.sets, pCostumeSet);
		}
	}
}

#define TARGET_RESET_DURATION (5.0) // seconds
#define TARGET_ANNOYED_CLICK_COUNT (5)

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void playVoiceSetSoundForTarget(U32 entRef, const char *filename)
{
	Entity *ent = entFromEntityRefAnyPartition(entRef);
	if(ent)
	{
		Critter *pCritter = ent->pCritter;
		if(pCritter && pCritter->voiceSet && pCritter->voiceSet[0]) 
		{
			Vec3 pos;
			char fullEventName[MAX_PATH];
			char **nameChoices = NULL;
			int numChoices;
			char *chosenName = NULL;
			U32 voiceSetExists;
			
			// Verify the voice set exists
			voiceSetExists = sndGetVoiceSetNamesForCategory(pCritter->voiceSet, NULL, NULL);
			if(!voiceSetExists)
			{
				ErrorFilenamef(NULL, "Voice Set %s does not exist for critter %s. (check Critter and CritterGroup Voice Sets)", pCritter->voiceSet, ent->debugName);
				return;
			}

			if(sndLastTargetEntRef() != entRef) 
			{
				sndSetLastTargetClickCount(0);
			}
			else
			{
				// same target ent, check time
				F32 delta;

				sndKillSourceIfPlaying(sndLastTargetSoundSource(), true);

				// did this event occur within the duration
				delta = timerSeconds(timerCpuTicks() -  sndLastTargetTimestamp());
				if(delta > TARGET_RESET_DURATION) 
				{
					sndSetLastTargetClickCount(0);
				}
				else
				{
					// have we reached the annoyance click count
					if( sndLastTargetClickCount() >= TARGET_ANNOYED_CLICK_COUNT )
					{
						int offset;

						// Sequence the Annoyed sounds
						sndGetVoiceSetNamesForCategory(pCritter->voiceSet, "Annoyed", &nameChoices);
						
						numChoices = eaSize(&nameChoices);
						offset = sndLastTargetClickCount() - TARGET_ANNOYED_CLICK_COUNT;

						// make sure an annoyed sound exists
						if(offset >= 0 && offset < numChoices)
						{
							chosenName = nameChoices[offset];
						}
						else
						{
							sndSetLastTargetClickCount(0);
						}
					}
				}
			}
			
			// default
			if(!chosenName)
			{
				// create a list of choices
				sndGetVoiceSetNamesForCategory(pCritter->voiceSet, "Greeting", &nameChoices);
				sndGetVoiceSetNamesForCategory(pCritter->voiceSet, "Comment", &nameChoices);
				sndGetVoiceSetNamesForCategory(pCritter->voiceSet, "Question", &nameChoices);

				numChoices = eaSize(&nameChoices);

				if(numChoices > 1)
				{
					static int lastChoice = -1;
					int choice;
					
					choice = randInt(numChoices);
					if(choice == lastChoice) {
						choice++;
						choice %= numChoices;
					}
					
					lastChoice = choice;

					chosenName = nameChoices[choice];
				}
				else if(numChoices == 1)
				{
					chosenName = nameChoices[0];
				}
			}

			if(chosenName)
			{
				SoundSource *source;

				strcpy(fullEventName, pCritter->voiceSet);
				strcat(fullEventName, "/");
				strcat(fullEventName, chosenName);

				entGetPos(ent, pos);
				
				source = sndPlayFromEntity(fullEventName, entRef, filename, true);

				sndSetLastTargetSoundSource(source);

				// update last target state
				sndSetLastTargetEntRef(entRef);
				sndSetLastTargetTimestamp(timerCpuTicks());
				sndSetLastTargetClickCount( sndLastTargetClickCount()+1 );
			}
			
			eaDestroy(&nameChoices);
		
		} // end if pCritter
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void playVoiceSetSound(const char *soundName, const char *filename, U32 entRef)
{
	if(soundName && soundName[0])
	{
		// look up the actual sound event
		Entity *ent = entFromEntityRefAnyPartition(entRef);
		if(ent)
		{
			Critter *pCritter = ent->pCritter;
			if(pCritter && pCritter->voiceSet && pCritter->voiceSet[0]) 
			{
				char fullEventName[MAX_PATH];
				char **nameChoices = NULL;
			
				if( sndGetVoiceSetNamesForCategory(pCritter->voiceSet, soundName, &nameChoices) )
				{
					int numChoices = eaSize(&nameChoices);
					if(numChoices > 0)
					{
						int choice = randInt(numChoices);

						strcpy(fullEventName, pCritter->voiceSet);
						strcat(fullEventName, "/");
						strcat(fullEventName, nameChoices[choice]);

						sndPlayFromEntity(fullEventName, entRef, filename, true);
					}
				}
				else
				{
					ErrorFilenamef(NULL, "Voice Set %s does not exist for critter %s. (check Critter and CritterGroup Voice Sets)", pCritter->voiceSet, ent->debugName);
					return;
				}

				eaDestroy(&nameChoices);
			}
		}
	}
}


// Play a sound event with eventName from an event group groupPath, otherwise play the event at defaultPath (if any)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PlaySoundFromGroup");
void exprFuncPlaySoundFromGroup(const char *groupPath, const char *eventName, const char *defaultPath)
{
	char dstPath[MAX_PATH];
	bool played = false;

	if(groupPath && groupPath[0])
	{
		if(eventName && eventName[0])
		{
			strcpy(dstPath, groupPath);
			strcat(dstPath, "/");
			strcat(dstPath, eventName);

			// check for exact path
			if(sndEventExists(dstPath))
			{
				sndPlayAtCharacter(dstPath, NULL, -1, NULL, NULL);
				played = true;
			} 
			else
			{
				// nope, look for an event in the group with a name that starts with the eventName
				void **events = NULL;
				int numEvents, i;

				// get all events in the group
				sndGetEventsFromEventGroupPath(groupPath, &events);

				// step through the events to find a match using the stripped name
				numEvents = eaSize(&events);
				for(i = 0; i < numEvents; i++)
				{
					char *childName = NULL;
					char *result;

					sndGetEventName(events[i], &childName);

					if(childName)
					{
						result = strstr(eventName, childName);
						if(result && result == eventName) // was it found, was it found at the beginning 
						{
							// construct path
							strcpy(dstPath, groupPath);
							strcat(dstPath, "/");
							strcat(dstPath, childName);

							sndPlayAtCharacter(dstPath, NULL, -1, NULL, NULL);
							played = true;

							break;
						}
					}
				}

				eaDestroy(&events); 
			}
		}

		if(!played && defaultPath && defaultPath[0])
		{
			sndPlayAtCharacter(defaultPath, NULL, -1, NULL, NULL);
			played = true;
		}
	}
}

// Prevent the left/right analog sticks from controlling the player or camera.
// These aren't keybinds, and so can't be disabled in the normal fashion just by
// pushing a keybind profile onto the stack. Called by the UI when entering fullscreen
// interfaces.
AUTO_CMD_INT(gGCLState.bLockPlayerAndCamera, LockPlayerAndCamera) ACMD_ACCESSLEVEL(0) ACMD_CLIENTONLY ACMD_CATEGORY(Interface) ACMD_HIDE;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CameraAndPlayerAreLocked");
bool exprCameraAndPlayerAreLocked(void)
{
	return gGCLState.bLockPlayerAndCamera;
}

// The client is allowed to trigger a remote contact update once every 5 seconds
#define REMOTE_CONTACT_PERIODIC_CLIENT_UPDATE_TIME 5

// Populates the entity's remote contact list
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME("RefreshRemoteContactList");
void gclCmdPopulateRemoteContactList(Entity* pEnt) 
{
	static U32 s_uNextUpdateTime = 0;
	U32 uCurrentTime = timeSecondsSince2000();
	Player* pPlayer = pEnt ? pEnt->pPlayer : NULL;

	if(!pEnt || !pPlayer || uCurrentTime < s_uNextUpdateTime) {
		return;
	}

	ServerCmd_entRefreshRemoteContactList();
	s_uNextUpdateTime = uCurrentTime + REMOTE_CONTACT_PERIODIC_CLIENT_UPDATE_TIME;
}

// This method is called from the game server when the loot is placed on the corpse
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD ACMD_NAME("SetGlowOnCorpseForLoot");
void gclCmdSetGlowOnCorpseForLoot(EntityRef iDeadEntRef, const char* pchFXStart)
{
	Entity *pEnt = entFromEntityRefAnyPartition(iDeadEntRef);
	Entity *pPlayerEnt = entActivePlayerPtr();

	if (!pEnt && pchFXStart && pchFXStart[0])
	{
		gclEntityAddPendingLootGlow(iDeadEntRef, pchFXStart);
		return;
	}

	// Validate
	if(!pEnt ||
		!pPlayerEnt ||
		!entCheckFlag(pEnt, ENTITYFLAG_DEAD) || // Not dead
		!pEnt->pCritter || // Not a critter
		!inv_HasLoot(pEnt)) 
	{
		return;
	}

	// Apply special FX for the corpses with loot
	// TODO: For the final version we will need switch to a data defined FX
	if (pEnt->pCritter->pcLootGlowFX && pEnt->pCritter->pcLootGlowFX[0])
	{
		dtFxManRemoveMaintainedFx(pEnt->dyn.guidFxMan, pEnt->pCritter->pcLootGlowFX);
		pEnt->pCritter->pcLootGlowFX = NULL;
	}
	if (pchFXStart && pchFXStart[0] && reward_MyDrop(pPlayerEnt, pEnt))
	{
		dtFxManAddMaintainedFx(pEnt->dyn.guidFxMan, pchFXStart, NULL, 0, 0, eDynFxSource_HardCoded);
		pEnt->pCritter->pcLootGlowFX = allocAddString(pchFXStart);
	}
}

// A collection of PlayerCostumes to save out.
AUTO_STRUCT;
typedef struct HeadshotCostumeList
{
	const char** ppchCostumeNames; AST(NAME(CostumeName))
} HeadshotCostumeList;

// A collection of PlayerCostumes to save out.
AUTO_STRUCT;
typedef struct HeadshotCostumeRef
{
	REF_TO(PlayerCostume) hCostume;
} HeadshotCostumeRef;

// A collection of PlayerCostumes to save out.
AUTO_STRUCT;
typedef struct HeadshotSaveScreenshotsData
{
	HeadshotCostumeRef** eaCostumeRefs;
	const char* pchOutputFileType;
	const char* pchHeadshotStyle; AST(POOL_STRING)
	int iWidth;
	int iHeight;
	U32 uiLastSuccessfulRender;
} HeadshotSaveScreenshotsData;

static HeadshotSaveScreenshotsData* s_HeadshotSaveScreenshotsData = NULL;

/// Creates and saves a collection of headshots using a list of costumes and a headshot style.
/// The headshots will be of size WIDTH x HEIGHT.
///
/// Screeshots will be named <COSTUME-NAME>.jpg
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME("HeadshotSaveScreenshotsForCostumes");
void gclHeadshotSaveScreenshotsForCostumes(const char* pchFile, 
										   const char* pchOutputFileType,
										   const char* pchHeadshotStyle,
										   int iWidth, 
										   int iHeight)
{
	HeadshotCostumeList CostumeList = {0};
	HeadshotStyleDef* pStyle = contact_HeadshotStyleDefFromName(pchHeadshotStyle);
	int i;

	if (s_HeadshotSaveScreenshotsData)
		StructDestroy(parse_HeadshotSaveScreenshotsData, s_HeadshotSaveScreenshotsData);

	if (!pStyle)
	{
		Alertf("Headshot style \"%s\" does not exist!", pchHeadshotStyle);
		return;
	}
	if (pStyle->bUseBackgroundOnly)
	{
		Alertf("HeadshotStyle \"%s\" has UseBackgroundOnly set.", pchHeadshotStyle);
		return;
	}
	if (!fileExists(pchFile)) 
	{
		Alertf("Could not find file \"%s\".", pchFile);
		return;
	}
	if (!ParserReadTextFile(pchFile, parse_HeadshotCostumeList, &CostumeList, 0)) 
	{
		Alertf("Could not read file \"%s\", bad syntax?", pchFile);
		return;
	}
	s_HeadshotSaveScreenshotsData = StructCreate(parse_HeadshotSaveScreenshotsData);
	s_HeadshotSaveScreenshotsData->iHeight = iHeight;
	s_HeadshotSaveScreenshotsData->iWidth = iWidth;
	s_HeadshotSaveScreenshotsData->pchOutputFileType = StructAllocString(pchOutputFileType);
	s_HeadshotSaveScreenshotsData->pchHeadshotStyle = pStyle->pchName;

	for (i = 0; i < eaSize(&CostumeList.ppchCostumeNames); ++i) 
	{
		const char* pchCostumeName = CostumeList.ppchCostumeNames[i];
		HeadshotCostumeRef* pNewRef = StructCreate(parse_HeadshotCostumeRef);
		SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, pchCostumeName, pNewRef->hCostume);

		eaPush(&s_HeadshotSaveScreenshotsData->eaCostumeRefs, pNewRef);
	}
	//We'll allow 10 seconds for completion or (num headshots to render), whichever is greater.
	s_HeadshotSaveScreenshotsData->uiLastSuccessfulRender = timeSecondsSince2000();
	StructDeInit(parse_HeadshotCostumeList, &CostumeList);
}

bool gclHeadshotHasPendingCostumeScreenshots()
{
	return (s_HeadshotSaveScreenshotsData && eaSize(&s_HeadshotSaveScreenshotsData->eaCostumeRefs) > 0);
}

void gclHeadshotAttemptSaveScreenshotsForCostumes()
{
	HeadshotStyleDef* pStyle = contact_HeadshotStyleDefFromName(s_HeadshotSaveScreenshotsData->pchHeadshotStyle);
	Color BackgroundColor = pStyle ? colorFromRGBA(pStyle->uiBackgroundColor) : ColorTransparent;
	F32 fFOVy = contact_HeadshotStyleDefGetFOV(pStyle, -1);
	const char* pchSky = pStyle ? pStyle->pchSky : NULL;
	const char* pchFrame = pStyle ? pStyle->pchFrame : "Status";
	BasicTexture* pBackground = pStyle ? texLoadBasic(pStyle->pchBackground, TEX_LOAD_IN_BACKGROUND, WL_FOR_UI) : NULL;
	const char* pchAnimBits = pStyle ? pStyle->pchAnimBits : "HEADSHOT IDLE NOLOD";
	const char* pcAnimKeyword = pStyle ? pStyle->pcAnimKeyword : NULL;
	DynBitFieldGroup bfg = {0};
	int i;

	dynBitFieldGroupAddBits(&bfg, (char*)pchAnimBits, false);

	if (s_HeadshotSaveScreenshotsData->uiLastSuccessfulRender+5 < timeSecondsSince2000())
	{
		//If we've gone 5 seconds without successfully rendering a headshot, give up.
		char* estrNames = NULL;
		
		for (i = 0; i < eaSize(&s_HeadshotSaveScreenshotsData->eaCostumeRefs); i++)
		{
			estrAppend2(&estrNames, REF_STRING_FROM_HANDLE(s_HeadshotSaveScreenshotsData->eaCostumeRefs[i]->hCostume));
			estrAppend2(&estrNames, ", ");
		}
		Errorf("5 seconds have passed since the last successful render, and the following costumes still have not been received from the server. Please double-check that you spelled their names correctly: %s", estrNames);

		StructDestroy(parse_HeadshotSaveScreenshotsData, s_HeadshotSaveScreenshotsData);
		s_HeadshotSaveScreenshotsData = NULL;
		return;
	}

	gfxHeadshotDisableValidateSize = true;
	for (i = eaSize(&s_HeadshotSaveScreenshotsData->eaCostumeRefs)-1; i >= 0; i--) 
	{
		PlayerCostume* pPlayerCostume = GET_REF(s_HeadshotSaveScreenshotsData->eaCostumeRefs[i]->hCostume);
		
		if (!pPlayerCostume)
			continue;
		else
		{
			WLCostume* pWLCostume = gclEntity_CreateWLCostumeFromPlayerCostume(NULL, pPlayerCostume, false);
			//As far as I can tell, these WLCostumes just get added to the client dictionary and forgotten, so you could consider them "leaked".
			// Spoke to Stephen and he said not to bother fixing it since this is a silly devmode-only command and leaking a few MB doesn't matter.
			if (pWLCostume)
			{
				char pchDestBuffer[MAX_PATH];

				sprintf(pchDestBuffer, "%s.%s", pPlayerCostume->pcName, s_HeadshotSaveScreenshotsData->pchOutputFileType);
				gfxHeadshotCaptureCostumeAndSave(pchDestBuffer, 
					s_HeadshotSaveScreenshotsData->iWidth, 
					s_HeadshotSaveScreenshotsData->iHeight, 
					pWLCostume, 
					pBackground, 
					pchFrame, 
					BackgroundColor, 
					false,
					&bfg,
					pcAnimKeyword,
					pStyle->pchStance,
					fFOVy,
					pchSky, 
					NULL, NULL, NULL);
				StructDestroy(parse_HeadshotCostumeRef, s_HeadshotSaveScreenshotsData->eaCostumeRefs[i]);
				eaRemove(&s_HeadshotSaveScreenshotsData->eaCostumeRefs, i);
				s_HeadshotSaveScreenshotsData->uiLastSuccessfulRender = timeSecondsSince2000();
			}
		}
	}
	gfxHeadshotDisableValidateSize = false;

	if (eaSize(&s_HeadshotSaveScreenshotsData->eaCostumeRefs) <= 0)
	{
		StructDestroy(parse_HeadshotSaveScreenshotsData, s_HeadshotSaveScreenshotsData);
		s_HeadshotSaveScreenshotsData = NULL;
	}
}

static void gclBagSetPredictedActiveSlot(SA_PARAM_NN_VALID InventoryBag* pBag, S32 iIndex, S32 iSlot)
{
	S32 i, iOldSize = eaiSize(&pBag->eaiPredictedActiveSlots);
	eaiSetSize(&pBag->eaiPredictedActiveSlots, iIndex+1);

	for (i = iOldSize; i < iIndex; i++)
	{
		if (i < eaiSize(&pBag->eaiActiveSlots))
			pBag->eaiPredictedActiveSlots[i] = pBag->eaiActiveSlots[i];
		else
			pBag->eaiPredictedActiveSlots[i] = -1;
	}
	
	pBag->eaiPredictedActiveSlots[iIndex] = iSlot;
}

// Do powers fixup after changing active weapon slots
static void gclChangeActiveSlotFixupItemPowers(InventoryBag* pBag, S32 iSlot)
{
	const InvBagDef* pDef = invbag_def(pBag);
	if (pDef && (pDef->flags & InvBagFlag_ActiveWeaponBag))
	{
		InventorySlot* pSlot = eaGet(&pBag->ppIndexedInventorySlots, iSlot);
		if (pSlot && pSlot->pItem)
		{
			S32 i;
			if (eaiFind(&pBag->eaiPredictedActiveSlots, iSlot) >= 0)
			{
				for (i = 0; i < eaSize(&pSlot->pItem->ppPowers); i++)
				{
					Power* pPower = pSlot->pItem->ppPowers[i];
					PowerDef* pPowerDef = GET_REF(pPower->hDef);

					if (pPowerDef && 
						pPowerDef->eType == kPowerType_Combo &&
						!eaSize(&pPower->ppSubPowers))
					{
						power_CreateSubPowers(pPower);
					}
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclResetActiveSlotsForBags(Entity* pEnt, IntEarrayWrapper* pBags)
{
	if (pEnt && pEnt->pChar && pEnt->pInventoryV2)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		
		if (pBags)
		{
			S32 i;
			// Reset predicted slots on all requested bags and restore stance states
			for (i = eaiSize(&pBags->eaInts)-1; i >= 0; i--)
			{
				InvBagIDs eBagID = pBags->eaInts[i];
				InvBagSlotSwitchRequest* pRequest = pEnt->pInventoryV2->ppSlotSwitchRequest[i];
				InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);
				if (pBag)
				{
					S32 iActiveSlot = eaiGet(&pBag->eaiActiveSlots, 0);
					eaiDestroy(&pBag->eaiPredictedActiveSlots);
					invbag_HandleMoveEvents(pEnt, pBag, iActiveSlot, 0, 0, false);
				}
			}
		}
		// Destroy all slot requests
		eaDestroyStruct(&pEnt->pInventoryV2->ppSlotSwitchRequest, parse_InvBagSlotSwitchRequest);
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclBagHandleMoveEvents(Entity* pEnt, S32 eBagID, S32 iSlot)
{
	if (pEnt && pEnt->pInventoryV2 && !eaSize(&pEnt->pInventoryV2->ppSlotSwitchRequest))
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);

		if (pBag)
		{
			invbag_HandleMoveEvents(pEnt, pBag, iSlot, 0, 1, true);
		}
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclChangeActiveBagSlotNow(Entity* pEnt, U32 uRequestID)
{
	if (pEnt && pEnt->pChar && pEnt->pInventoryV2)
	{
		int i;
		for (i = eaSize(&pEnt->pInventoryV2->ppSlotSwitchRequest)-1; i >= 0; i--)
		{
			InvBagSlotSwitchRequest* pRequest = pEnt->pInventoryV2->ppSlotSwitchRequest[i];
			
			// If the request is still pending, handle move events now
			if (pRequest->uRequestID == uRequestID)
			{
				if (!pRequest->bHasHandledMoveEvents)
				{
					U32 uTime = pmTimestamp(0.1f);
					GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
					InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), pRequest->eBagID, pExtract);
				
					// Clear the predicted slots and reset the powers array
					if (pBag)
					{
						eaiDestroy(&pBag->eaiPredictedActiveSlots);
					}
					ServerCmd_gslBagChangeActiveSlotSendMoveEventTime(pRequest->uRequestID, uTime);
					pRequest->bHasHandledMoveEvents = true;
				}
				break;
			}
		}
	}
}

#define CHANGE_ACTIVE_SLOT_ID_MAX 255
static U32 gclEntGetNextActiveSlotRequestID(SA_PARAM_NN_VALID Entity* pEnt)
{
	if (pEnt->pInventoryV2)
	{
		if (++pEnt->pInventoryV2->uiCurrentSlotSwitchID > CHANGE_ACTIVE_SLOT_ID_MAX)
			pEnt->pInventoryV2->uiCurrentSlotSwitchID = 1;
		return pEnt->pInventoryV2->uiCurrentSlotSwitchID;
	}
	return 0;
}

static void gclAttemptChangeActiveSlot(Entity* pEnt, InvBagSlotSwitchRequest* pRequest, GameAccountDataExtract* pExtract)
{
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), pRequest->eBagID, pExtract);
		
	if (!pRequest->bHasHandledMoveEvents && pBag && !pEnt->pChar->pPowActCurrent && !pEnt->pChar->pPowActQueued)
	{
		U32 uTime = pmTimestamp(0.1f);
		ANALYSIS_ASSUME(pBag != NULL);
		invbag_HandleMoveEvents(pEnt, pBag, pRequest->iNewActiveSlot, uTime, pRequest->uRequestID, false);

		// Set the predicted active slot, and reset the powers array
		gclBagSetPredictedActiveSlot(pBag, pRequest->iIndex, pRequest->iNewActiveSlot);
		gclChangeActiveSlotFixupItemPowers(pBag, pRequest->iNewActiveSlot);

		ServerCmd_gslBagChangeActiveSlotSendMoveEventTime(pRequest->uRequestID, uTime);
		pRequest->bHasHandledMoveEvents = true;
	}
}

void gclUpdateActiveSlotRequests(Entity* pEnt, F32 fElapsed, GameAccountDataExtract* pExtract)
{
	if (pEnt && pEnt->pChar && pEnt->pInventoryV2 && eaSize(&pEnt->pInventoryV2->ppSlotSwitchRequest))
	{
		InvBagSlotSwitchRequest* pRequest = pEnt->pInventoryV2->ppSlotSwitchRequest[0];

		gclAttemptChangeActiveSlot(pEnt, pRequest, pExtract);

		pRequest->fTimer += fElapsed;
		if (pRequest->bHasHandledMoveEvents && pRequest->fTimer >= pRequest->fDelay)
		{
			// Destroy the request
			eaRemove(&pEnt->pInventoryV2->ppSlotSwitchRequest, 0);
			StructDestroy(parse_InvBagSlotSwitchRequest, pRequest);
		}
	}
}

void gclBagChangeActiveSlotWhenReady(Entity* pEnt, S32 eBagID, S32 iIndex, S32 iNewActiveSlot)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);
	const InvBagDef* pDef = invbag_def(pBag);
	if (pDef && pEnt->pChar && pEnt->pInventoryV2)
	{
		const S32 iMaxRequests = 2;
		if (eaSize(&pEnt->pInventoryV2->ppSlotSwitchRequest) < iMaxRequests)
		{
			F32 fDelayOverride = 0.0f;
			U32 uiRequestID = gclEntGetNextActiveSlotRequestID(pEnt);
			U32 uiTime = character_PredictTimeForNewActivation(pEnt->pChar, false, false, NULL, &fDelayOverride);
			S32 i;
			for (i = 0; i < eaSize(&pEnt->pInventoryV2->ppSlotSwitchRequest); i++)
			{
				InvBagSlotSwitchRequest* pRequest = pEnt->pInventoryV2->ppSlotSwitchRequest[i];
				F32 fDelay = MAXF(pRequest->fDelay - pRequest->fTimer, 0.0f);
				uiTime = pmTimestampFrom(uiTime, fDelay);
			}
			if (inv_AddActiveSlotChangeRequest(pEnt, pBag, iIndex, iNewActiveSlot, uiRequestID, fDelayOverride))
			{
				ServerCmd_gslBagChangeActiveSlotWhenReady(pBag->BagID, 
														  iIndex, 
														  iNewActiveSlot, 
														  uiTime,
														  uiRequestID, 
														  fDelayOverride);
				// Try to switch immediately
				if (eaSize(&pEnt->pInventoryV2->ppSlotSwitchRequest) == 1)
				{
					gclAttemptChangeActiveSlot(pEnt, pEnt->pInventoryV2->ppSlotSwitchRequest[0], pExtract);
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_NAME(ChangeActiveBagSlotWhenReady) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclCmdBagChangeActiveSlotWhenReady(S32 eBagID, S32 iNewActiveSlot)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt)
	{
		gclBagChangeActiveSlotWhenReady(pEnt, eBagID, 0, iNewActiveSlot);
	}
}

AUTO_COMMAND ACMD_NAME(ChangeActiveBagSlotWhenReadyID) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclCmdBagIDChangeActiveSlotWhenReady(const char *pchBagID, S32 iNewActiveSlot)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt)
	{
		S32 iBagID = StaticDefineIntGetInt(InvBagIDsEnum,pchBagID);
		gclBagChangeActiveSlotWhenReady(pEnt, iBagID, 0, iNewActiveSlot);
	}
}

// For bags that have more than one active slot, choose what active slot index you are changing
AUTO_COMMAND ACMD_NAME(ChangeActiveBagSlotWithIndexWhenReady) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclCmdBagChangeActiveSlotWithIndexWhenReady(S32 eBagID, S32 iIndex, S32 iNewActiveSlot)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt)
	{
		gclBagChangeActiveSlotWhenReady(pEnt, eBagID, iIndex, iNewActiveSlot);
	}
}

// For bags that have more than one active slot, choose what active slot index you are changing
AUTO_COMMAND ACMD_NAME(ChangeActiveBagSlotWithIndexWhenReadyID) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclCmdBagIDChangeActiveSlotWithIndexWhenReady(const char *pchBagID, S32 iIndex, S32 iNewActiveSlot)
{
	Entity* pEnt = entActivePlayerPtr();

	if (pEnt)
	{
		S32 iBagID = StaticDefineIntGetInt(InvBagIDsEnum,pchBagID);
		gclBagChangeActiveSlotWhenReady(pEnt, iBagID, iIndex, iNewActiveSlot);
	}
}

// Set Alienware color
AUTO_COMMAND ACMD_NAME(AlienColor) ACMD_ACCESSLEVEL(9);
void AlienColor(U32 r, U32 g, U32 b, U32 a)
{
	Color c;
	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	gfxAlienChangeColor(c);
}


#include "AutoGen/cmdClient_c_ast.c"

/* End of File */
