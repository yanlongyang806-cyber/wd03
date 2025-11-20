/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "error.h"
#include "earray.h"
#include "wlCostume.h"
#include "dynSkeleton.h"
#include "dynDraw.h"
#include "dynNode.h"
#include "quat.h"
#include "EntityIterator.h"
#include "CommandQueue.h"

#include "aiExtern.h"
#include "AttribMod.h"
#include "AutoTransDefs.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "CharacterClass.h"
#include "character_combat.h"
#include "Character_mods.h"
#include "cmdparse.h"
#include "cmdServerCombat.h"
#include "CombatCallbacks.h"
#include "CombatConfig.h"
#include "crypt.h"
#include "DamageTracker.h"
#include "entCritter.h"
#include "EntityLib.h"
#include "EntityMovementManager.h"
#include "entitysysteminternal.h"
#include "file.h"
#include "GameAccountDataCommon.h"
#include "GameServerLib.h"
#include "gslAccountProxy.h"
#include "gslCommandParse.h"
#include "gslControlScheme.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslEntityNet.h"
#include "gslEventSend.h"
#include "gslInteraction.h"
#include "gslMechanics.h"
#include "gslOldEncounter.h"
#include "gslSavedPet.h"
#include "gslSendToClient.h"
#include "gslTray.h"
#include "gslPowerTransactions.h"
#include "gslPVP.h"
#include "gslUserExperience.h"
#include "itemCommon.h"
#include "logging.h"
#include "mission_common.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "Powers.h"
#include "PowerAnimFX.h"
#include "PowerSubtarget.h"
#include "PowersMovement.h"
#include "Store.h"
#include "textparser.h"
#include "wlCostume.h"
#include "wlSkelInfo.h"
#include "ItemArt.h"
#include "inventoryTransactions.h"
#include "gslTeamUp.h"

#include "objtransactions.h"
#include "cmdServerCharacter.h"

#include "Team.h"
#include "Guild.h"
#include "gslChat.h"
#include "gslQueue.h"
#include "gslMission.h"
#include "chatCommon.h"
#include "aiLib.h"
#include "aiConfig.h"
#include "aiStruct.h"
#include "Leaderboard.h"

#include "GameStringFormat.h"
#include "Skills_DD.h"
#include "GamePermissionsCommon.h"
#include "LoggedTransactions.h"
#include "AccountProxyCommon.h"
#include "Login2Common.h"

extern ParseTable parse_Team[];
#define TYPE_parse_Team Team

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "autogen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "Autogen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Character_h_ast.h"
#include "Player_h_ast.h"
#include "itemart_h_ast.h"

extern ParseTable parse_Critter[];
#define TYPE_parse_Critter Critter
extern ParseTable parse_EquippedArt[];
#define TYPE_parse_EquippedArt EquippedArt

// This happens after gslExternPlayerFinishCreation, and happens for both new and loaded characters
// Can modify transacted data
void gslExternInitializeEntity(Entity * ent)
{
	if(gConf.bItemArt)
	{
		PERFINFO_AUTO_START_FUNC();

		entity_UpdateItemArtAnimFX(ent);

		PERFINFO_AUTO_STOP();
	}
}

// Happens right before an entity is destroyed
void gslExternCleanupEntity(int iPartitionIdx, Entity * ent)
{
	PERFINFO_AUTO_START_FUNC();

	// Interaction
	interaction_EndInteractionAndDialog(iPartitionIdx, ent, false, true, false);
	if (ent && ent->pPlayer && ent->pPlayer->InteractStatus.pEndInteractCommandQueue){
		CommandQueue_Destroy(ent->pPlayer->InteractStatus.pEndInteractCommandQueue);
		ent->pPlayer->InteractStatus.pEndInteractCommandQueue = NULL;
	}

	encounter_RemoveActor(ent); // New encounter system
	if (gConf.bAllowOldEncounterData)
		oldencounter_RemoveActor(ent);	// Old encounter system
	mission_PreEntityDestroyMissionDeinit(iPartitionIdx, ent);
	mechanics_LeaveMapEntityCleanup(ent);

	PERFINFO_AUTO_STOP();
}

void character_preSaveTransact(Character *pChar)
{
	CharacterPreSaveInfo preSaveInfo = {0};

	character_FillInPreSaveInfo(pChar,&preSaveInfo);

	AutoTrans_trCharacterPreSave(LoggedTransactions_CreateManagedReturnVal("trCharacterPreSave", NULL, NULL),
		GetAppGlobalType(),
		entGetType(pChar->pEntParent),
		entGetContainerID(pChar->pEntParent),
		&preSaveInfo);

	CharacterPreSaveInfo_Destroy(&preSaveInfo);
}

// Happens before persisted non-transacted data is sent to DB.
// Can not modify transacted data, but should modify persisted, non-transact data
void gslExternPlayerSave(Entity *ent, bool bRunTransact)
{
	PERFINFO_AUTO_START_FUNC();

	character_PreSave(ent);

	entity_TrayPreSave(ent);

	if(bRunTransact)
		character_preSaveTransact(ent->pChar);


	if (ent->pPlayer)
	{
		// Translate the loose UI data into a string for saving.
		if (ent->pPlayer->pUI && ent->pPlayer->pUI->pLooseUI)
		{
			char *pchLooseUI = NULL;
			ParserWriteText(&pchLooseUI, parse_PlayerLooseUI, ent->pPlayer->pUI->pLooseUI, 0, 0, 0);
			estrClear(&ent->pPlayer->pUI->pchLooseUI);
			estrBase64Encode(&ent->pPlayer->pUI->pchLooseUI, pchLooseUI, estrLength(&pchLooseUI));
			estrDestroy(&pchLooseUI);
			entity_SetDirtyBit(ent, parse_PlayerUI, ent->pPlayer->pUI, true);
			entity_SetDirtyBit(ent, parse_Player, ent->pPlayer, false);
		}

		ent->pPlayer->iLastPlayedTime = timeSecondsSince2000();

        if ( ent->pPlayer->addictionPlaySessionEndTime )
        {
            U32 remainingPlayTime;
            
            // Update the play time of the current session for anti-addiction policy enforcement.
            if ( ent->pPlayer->addictionPlaySessionEndTime > timeSecondsSince2000() )
            {
                remainingPlayTime = ent->pPlayer->addictionPlaySessionEndTime - timeSecondsSince2000();
            }
            else
            {
                remainingPlayTime = 0;
            }

            APSetKeyValueSimple(ent->pPlayer->accountID, "AddictionPlayTime", gAddictionMaxPlayTime - remainingPlayTime, false, NULL, NULL);
        }
	}

	PERFINFO_AUTO_STOP();
}

bool gslExternPlayerLoadLooseUI(Entity* ent)
{
	if (ent->pPlayer && ent->pPlayer->pUI)
	{
		PlayerLooseUI *pLooseUI;

		PERFINFO_AUTO_START_FUNC();
		
		pLooseUI = ent->pPlayer->pUI->pLooseUI ? ent->pPlayer->pUI->pLooseUI : StructCreate(parse_PlayerLooseUI);
		if ( ent->pPlayer->pUI->pLooseUI )
			StructReset(parse_PlayerLooseUI, pLooseUI);
		if (ent->pPlayer->pUI->pchLooseUI && ent->pPlayer->pUI->pchLooseUI[0])
		{
			size_t sz = estrLength(&ent->pPlayer->pUI->pchLooseUI) * 2 + 1;
			char *pchLooseUI = calloc(1, sz);
			decodeBase64String(ent->pPlayer->pUI->pchLooseUI, estrLength(&ent->pPlayer->pUI->pchLooseUI), pchLooseUI, sz);
			if (!ParserReadText(pchLooseUI, parse_PlayerLooseUI, pLooseUI, PARSER_NOERRORFSONPARSE))
				entLog(LOG_PLAYER, ent, "UI", "Unable to parse Player's LooseUI data (this is a normal occurance), resetting it.");
			ent->pPlayer->pUI->pLooseUI = pLooseUI;
			free(pchLooseUI);
		}
		entity_SetDirtyBit(ent, parse_PlayerUI, ent->pPlayer->pUI, true);
		entity_SetDirtyBit(ent, parse_Player, ent->pPlayer, false);

		PERFINFO_AUTO_STOP();
		return true;
	}
	return false;
}

// Happens when a player finalizes the login process and is ready to start playing
// Can not modify transacted data
void gslExternPlayerLogin(Entity *ent)
{
	U32 iCurTime = timeSecondsSince2000();
	char pcNameBuff[512];

	PERFINFO_AUTO_START_FUNC();
		
	if (ent && ent->pTeam && ent->pTeam->iTeamID) {
		sprintf(pcNameBuff, "%d", ent->pTeam->iTeamID);
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM), pcNameBuff, ent->pTeam->hTeam);
		ent->pTeam->iTimeSinceHandleInit = iCurTime;
		entity_SetDirtyBit(ent, parse_PlayerTeam, ent->pTeam, true);
	}
	
	if (ent && ent->pPlayer && ent->pPlayer->pGuild && ent->pPlayer->pGuild->iGuildID) {
		sprintf(pcNameBuff, "%d", ent->pPlayer->pGuild->iGuildID);
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), pcNameBuff, ent->pPlayer->pGuild->hGuild);
		if(!guild_LazySubscribeToBank())
		{
			SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYGUILDBANK), pcNameBuff, ent->pPlayer->pGuild->hGuildBank);
		}
		ent->pPlayer->pGuild->iTimeSinceHandleInit = iCurTime;
	}

	if (ent)
	{
		gslQueue_HandlePlayerLogin(ent);
	}

	PERFINFO_AUTO_STOP();
}

// Happens when a player is leaving the game
// Can not modify transacted data
void gslExternPlayerLogout(Entity *pEnt, LogoffType eLogoffType)
{
	UserExp_LogLogout(pEnt); // do this first before anything else changes

	if (eLogoffType != kLogoffType_MeetPartyInLobby)
	{
		if (team_IsMember(pEnt)) {
			RemoteCommand_aslTeam_DoLogout(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, entGetType(pEnt), entGetContainerID(pEnt), true);
		} else if (team_IsInvitee(pEnt)) {
			RemoteCommand_aslTeam_DeclineInvite(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, NULL, NULL, NULL);
		} else if (team_IsRequester(pEnt)) {
			RemoteCommand_aslTeam_CancelRequest(GLOBALTYPE_TEAMSERVER, 0, pEnt->pTeam->iTeamID, pEnt->myContainerID, NULL, NULL, NULL);
		}
	}

	if (pEnt && pEnt->pPlayer)
	{
		gslQueue_HandlePlayerLogout(pEnt);

		if (gConf.bEnablePersistedStores &&
			SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog))
		{
			gslPersistedStore_PlayerRemoveRequests(pEnt);
		}
	}

	gslTeam_AwayTeamMemberRemove(pEnt,false);
	entity_updateLeaderboardStatsAll(pEnt);
}

// Return true if the given ent is detectable by the clientLink
bool gslExternEntityDetectable(ClientLink *pLink, Entity *ent)
{
	Entity *playerEnt = gslPrimaryEntity(pLink);

	if (playerEnt && playerEnt->pPlayer && playerEnt->pPlayer->pPresenceInfo)
	{
		//Check the list of entities that are hidden due to the "presence" feature.
		if(ea32Size(&playerEnt->pPlayer->pPresenceInfo->perHidden) > 0)
		{
			if(ea32Find(&playerEnt->pPlayer->pPresenceInfo->perHidden,ent->myRef) > -1)
				return false;
		}
	}

	if(combatcbCharacterCanPerceive)
	{
		
		if(playerEnt && playerEnt->pChar && ent->pChar)
		{
			if (!combatcbCharacterCanPerceive(playerEnt->pChar,ent->pChar))
			{
				return false;
			}
		}
	}
	return true;
}

AUTO_COMMAND ACMD_NAME(recharge) ACMD_LIST(gEntConCmdList);
void entConRecharge(Entity* e, U32 uiID, F32 fTime)
{
	if(e && e->pChar)
	{
		character_PowerSetRecharge(entGetPartitionIdx(e),e->pChar,uiID,fTime);
	}
}

AUTO_COMMAND ACMD_NAME(cooldown) ACMD_LIST(gEntConCmdList);
void entConCooldown(Entity* e, int iCategory, F32 fTime)
{
	if(e)
	{
		int iPartitionIdx = entGetPartitionIdx(e);
		if(iCategory > -1)
		{
			character_CategorySetCooldown(iPartitionIdx,e->pChar,iCategory,fTime);
		}
		else
		{
			for(iCategory=0;iCategory<eaSize(&e->pChar->ppCooldownTimers);iCategory++)
			{
				character_CategorySetCooldown(iPartitionIdx,e->pChar,e->pChar->ppCooldownTimers[iCategory]->iPowerCategory,fTime);
			}
		}
	}
}

AUTO_COMMAND ACMD_NAME(rechargeAll) ACMD_LIST(gEntConCmdList);
void entConRefresh(Entity* e)
{
	if(e && e->pChar)
	{
		int iPartitionIdx = entGetPartitionIdx(e);
		int iCategory;
		character_PowerSetRecharge(iPartitionIdx,e->pChar, 0, 0);

		for(iCategory=0;iCategory<eaSize(&e->pChar->ppCooldownTimers);iCategory++)
		{
			character_CategorySetCooldown(iPartitionIdx,e->pChar,e->pChar->ppCooldownTimers[iCategory]->iPowerCategory,0);
		}
	}
}

AUTO_COMMAND ACMD_NAME(AttribModExpire) ACMD_LIST(gEntConCmdList);
void entConAttribModExpire(Entity* e, ACMD_NAMELIST("PowerDef", REFDICTIONARY) const char *pchName, U32 uiDefIdx, U32 uiApplyID)
{
	if(e && e->pChar)
	{
		int i;
		PowerDef *pdef = powerdef_Find(pchName);
		for(i=eaSize(&e->pChar->modArray.ppMods)-1; i>=0; i--)
		{
			AttribMod *pmod = e->pChar->modArray.ppMods[i];
			if(pmod->uiApplyID==uiApplyID && pmod->uiDefIdx==uiDefIdx && GET_REF(pmod->hPowerDef)==pdef)
				mod_Expire(pmod);
		}
	}
}

AUTO_COMMAND ACMD_NAME(invincible) ACMD_LIST(gEntConCmdList);
void entConInvincible(Entity* e, S32 on)
{
	if(e && e->pChar)
	{
		e->pChar->bInvulnerable = on ? 1 : 0;
		e->pChar->bUnstoppable = on ? 1 : 0;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	}
}

AUTO_COMMAND ACMD_NAME(invisible) ACMD_LIST(gEntConCmdList);
void entConInvisible(Entity* e, S32 on)
{
	gslEntitySetInvisibleTransient(e, on);
	
	if(!on){
		gslEntitySetInvisiblePersistent(e, 0);
	}
}

// Toggles whether the player is untargetable or not
AUTO_COMMAND ACMD_NAME(untargetable, PlayerUntargetable) ACMD_CATEGORY(csr) ACMD_LIST(gEntConCmdList) ACMD_GLOBAL ACMD_SERVERCMD ACMD_ACCESSLEVEL(4);
void entConUntargetable(Entity* e, S32 on)
{
	if(e && e->pChar)
	{
		if(on)
			entSetCodeFlagBits(e, ENTITYFLAG_UNTARGETABLE);
		else
			entClearCodeFlagBits(e, ENTITYFLAG_UNTARGETABLE);
	}
}

AUTO_COMMAND ACMD_NAME(unselectable) ACMD_LIST(gEntConCmdList);
void entConUnselectable(Entity* e, S32 on)
{
	if(e && e->pChar)
	{
		if(on)
			entSetCodeFlagBits(e, ENTITYFLAG_UNSELECTABLE);
		else
			entClearCodeFlagBits(e, ENTITYFLAG_UNSELECTABLE);
	}
}

AUTO_COMMAND ACMD_NAME(invulnerable) ACMD_LIST(gEntConCmdList);
void entConInvulnerable(Entity* e, S32 on)
{
	if(e && e->pChar)
	{
		e->pChar->bInvulnerable = on ? 1 : 0;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	}
}

AUTO_COMMAND ACMD_NAME(unstoppable) ACMD_LIST(gEntConCmdList);
void entConUnstoppable(Entity* e, S32 on)
{
	if(e && e->pChar)
	{
		e->pChar->bUnstoppable = on ? 1 : 0;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	}
}

AUTO_COMMAND ACMD_NAME(unkillable) ACMD_LIST(gEntConCmdList);
void entConUnkillable(Entity* e, S32 on)
{
	if(e && e->pChar)
	{
		e->pChar->bUnkillable = on ? 1 : 0;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	}
}

AUTO_COMMAND ACMD_NAME(disablefaceactivate) ACMD_LIST(gEntConCmdList);
void entConDisableFaceActivate(Entity* e, S32 disable)
{
	if(e && e->pChar)
	{
		e->pChar->bDisableFaceActivate = disable ? 1 : 0;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	}
}

AUTO_COMMAND ACMD_NAME(disablefaceselected) ACMD_LIST(gEntConCmdList);
void entConDisableFaceSelected(Entity* e, S32 disable)
{
	if(e && e->pChar)
	{
		e->pChar->bDisableFaceSelected = disable ? 1 : 0;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
		pmUpdateSelectedTarget(e,true);
	}
}


AUTO_COMMAND ACMD_NAME(kill) ACMD_LIST(gEntConCmdList);
void entConKill(CmdContext *cmd_context, Entity* e)
{
	if(e && !entCheckFlag(e, ENTITYFLAG_IS_PLAYER))
	{
		if(e->pChar)
		{
			e->pChar->bKill = true;
			character_Wake(e->pChar);
		}
		else if(e->pCritter)
		{
			gslQueueEntityDestroy(e);
		}
		else
		{
			entDie(e, 0, 1, 1, NULL);
		}
	}
}

AUTO_COMMAND ACMD_NAME(killwithdamage) ACMD_LIST(gEntConCmdList);
void entConKillWithDamage(CmdContext *cmd_context, Entity* e)
{
	if(e && !entCheckFlag(e, ENTITYFLAG_IS_PLAYER))
	{
		if(e->pChar)
		{

			// Add damage to the target entity, so that the player who executed this will get rewards
			if (cmd_context->clientType == GLOBALTYPE_ENTITYPLAYER){
				Entity *pPlayerEnt = entFromContainerID(entGetPartitionIdx(e), cmd_context->clientType, cmd_context->clientID);
				if (pPlayerEnt){
					//Add the damage to the tracker
					damageTracker_AddTick(entGetPartitionIdx(e),
						e->pChar,
						pPlayerEnt->myRef,
						pPlayerEnt->myRef,
						e->myRef,
						e->pChar->pattrBasic->fHitPoints,
						e->pChar->pattrBasic->fHitPoints,
						kAttribType_HitPoints,
						0,
						NULL,
						0,
						NULL,
						0);
				}
			}

			e->pChar->bKill = true;
		}
		else if(e->pCritter)
		{
			gslQueueEntityDestroy(e);
		}
		else
		{
			entDie(e, 0, 1, 1, NULL);
		}
	}
}

AUTO_COMMAND ACMD_NAME("destroy") ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entConDestroy(Entity* e)
{
	// Not really a destroy.  Just marks as killed and calls die.
	//  Why it's called destroy when it doesn't actually destroy is beyond me.
	
	if(e && !entCheckFlag(e, ENTITYFLAG_IS_PLAYER))
	{
		if(e->pChar)
		{
			e->pChar->bKill = true;
			character_Wake(e->pChar);
		}
		
		entDie(e, 0, 1, 1, NULL);
	}
}

AUTO_COMMAND ACMD_NAME("destroyNoFade") ACMD_LIST(gEntConCmdList) ACMD_NOTESTCLIENT;
void entConDestroyNoFade(Entity* e)
{
	// Not really a destroy.  Just marks as killed and calls die.
	//  Why it's called destroy when it doesn't actually destroy is beyond me.
	
	if(e && !entCheckFlag(e, ENTITYFLAG_IS_PLAYER))
	{
		if(e->pChar)
		{
			e->pChar->bKill = true;
			character_Wake(e->pChar);
		}
		
		entDie(e, 0, 1, 1, NULL);
		
		entSetCodeFlagBits(e, ENTITYFLAG_DONOTFADE);
	}
}

AUTO_COMMAND ACMD_NAME("Refill_HP_POW") ACMD_LIST(gEntConCmdList);
void entConRefill_HP_Pow(Entity* e)
{
	Refill_HP_POW(e);
}

AUTO_COMMAND ACMD_NAME(AddPower) ACMD_LIST(gEntConCmdList);
void AddPowerCmd(Entity *clientEntity, ACMD_NAMELIST("PowerDef", REFDICTIONARY) char *pchName)
{
	if(clientEntity && clientEntity->pChar)
	{
		PowerDef *pdef = powerdef_Find(pchName);
		if(pdef)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(clientEntity);
			int iPartitionIdx = entGetPartitionIdx(clientEntity);
			character_AddPowerPersonal(iPartitionIdx, clientEntity->pChar,pdef,0,false,pExtract);
			character_ResetPowersArray(iPartitionIdx, clientEntity->pChar, pExtract);
		}
	}
}

AUTO_COMMAND ACMD_NAME(PowerApply) ACMD_LIST(gEntConCmdList);
void PowerApplyCmd(Entity *clientEntity, ACMD_NAMELIST("PowerDef", REFDICTIONARY) char *pchName)
{
	if(clientEntity && clientEntity->pChar)
	{
		PowerDef *pdef = powerdef_Find(pchName);
		if(pdef)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(clientEntity);
			character_ApplyUnownedPowerDefToSelf(entGetPartitionIdx(clientEntity), clientEntity->pChar,pdef,pExtract);
		}
	}
}

AUTO_COMMAND ACMD_NAME(sethealth) ACMD_LIST(gEntConCmdList);
void entConSetHealth(Entity *e, F32 fValue)
{
	if(e && e->pChar)
	{
		F32 old = e->pChar->pattrBasic->fHitPoints;
		e->pChar->pattrBasic->fHitPoints = fValue;
		MIN1(e->pChar->pattrBasic->fHitPoints, e->pChar->pattrBasic->fHitPointsMax);
		character_DirtyAttribs(e->pChar);
		character_Wake(e->pChar);
		eventsend_RecordHealthState(e, old, e->pChar->pattrBasic->fHitPoints);
	}
}

AUTO_COMMAND ACMD_NAME(sethealthpct) ACMD_LIST(gEntConCmdList);
void entConSetHealthPct(Entity *e, F32 fValue)
{
	if(e && e->pChar)
	{
		F32 old = e->pChar->pattrBasic->fHitPoints;
		e->pChar->pattrBasic->fHitPoints = fValue * e->pChar->pattrBasic->fHitPointsMax;
		MIN1(e->pChar->pattrBasic->fHitPoints, e->pChar->pattrBasic->fHitPointsMax);
		character_DirtyAttribs(e->pChar);
		character_Wake(e->pChar);
		eventsend_RecordHealthState(e, old, e->pChar->pattrBasic->fHitPoints);
	}
}

AUTO_COMMAND ACMD_NAME(setpower) ACMD_LIST(gEntConCmdList);
void entConSetPower(Entity *e, F32 fValue)
{
	if(e && e->pChar)
	{
		e->pChar->pattrBasic->fPower = fValue;
		character_DirtyAttribs(e->pChar);
	}
}

AUTO_COMMAND ACMD_NAME(setpowerpct) ACMD_LIST(gEntConCmdList);
void entConSetPowerPct(Entity *e, F32 fValue)
{
	if(e && e->pChar)
	{
		e->pChar->pattrBasic->fPower = fValue * e->pChar->pattrBasic->fPowerMax;
		character_DirtyAttribs(e->pChar);
	}
}

AUTO_COMMAND ACMD_NAME(setattribpool) ACMD_LIST(gEntConCmdList);
void entConSetAttribPool(Entity *e, ACMD_NAMELIST(AttribTypeEnum, STATICDEFINE) const char* attrib, F32 fValue)
{
	if(e && e->pChar)
	{
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attrib);
		if(eAttrib > 0)
		{
			AttribPool *ppool = attrib_getAttribPoolByCur(eAttrib);
			if(ppool)
				*F32PTR_OF_ATTRIB(e->pChar->pattrBasic,eAttrib) = fValue;
		}
	}
}

AUTO_COMMAND ACMD_NAME(setattribpoolpct) ACMD_LIST(gEntConCmdList);
void entConSetAttribPoolPct(Entity *e, ACMD_NAMELIST(AttribTypeEnum, STATICDEFINE) const char* attrib, F32 fValue)
{
	if(e && e->pChar)
	{
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attrib);
		if(eAttrib > 0)
		{
			AttribPool *ppool = attrib_getAttribPoolByCur(eAttrib);
			if(ppool && ppool->eAttribMax)
			{
				F32 fMax = *F32PTR_OF_ATTRIB(e->pChar->pattrBasic,ppool->eAttribMax);
				*F32PTR_OF_ATTRIB(e->pChar->pattrBasic,eAttrib) = fValue * fMax;
			}
		}
	}
}


// PowerHue <PowerID> <Hue>: Sets the hue of the Power's FX.  PowerID of 0 applies to all Powers, Hue of 0 reverts to default.
AUTO_COMMAND ACMD_NAME(PowerHue) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void cmdPowerHue(CmdContext *context, Entity *e, U32 uiID, F32 fHue)
{
	if(e && e->pChar)
	{
		Character *pchar = e->pChar;
		
		if((g_CombatConfig.bPowerCustomizationDisabled || !GamePermission_EntHasToken(e, GAME_PERMISSION_POWER_HUE))
			&& isProductionMode()
			&& entGetAccessLevel(e)< ACCESS_GM)
		{
			if(context)
				langFormatMessageKey(entGetLanguage(e),context->output_msg,"CmdParse_DisabledCommand",STRFMT_STRING("Command", "PowerHue"),STRFMT_END);
			return;
		}

		character_SetPowerHue(pchar,uiID,fHue);
	}
}

AUTO_COMMAND ACMD_NAME(PowerHue) ACMD_LIST(gEntConCmdList) ACMD_HIDE;
void entConPowerHue(Entity *e, U32 uiID, F32 fHue)
{
	cmdPowerHue(NULL,e,uiID,fHue);
}

// PowerEmit <PowerID> <Emit>: Sets the emit point of the Power.  PowerID of 0 applies to all Powers, invalid Emit reverts to default.
AUTO_COMMAND ACMD_NAME(PowerEmit) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void cmdPowerEmit(CmdContext *context, Entity *e, U32 uiID, ACMD_NAMELIST("PowerEmit", REFDICTIONARY) char *pchEmit)
{
	if(e && e->pChar)
	{
		Character *pchar = e->pChar;
		PowerEmit *pEmit = RefSystem_ReferentFromString(g_hPowerEmitDict,pchEmit);

		if(!GamePermission_EntHasToken(e, GAME_PERMISSION_POWER_EMIT))
		{
			return;
		}

		if((g_CombatConfig.bPowerCustomizationDisabled)
			&& isProductionMode()
			&& entGetAccessLevel(e)< ACCESS_GM)
		{
			if(context)
				langFormatMessageKey(entGetLanguage(e),context->output_msg,"CmdParse_DisabledCommand",STRFMT_STRING("Command", "PowerEmit"),STRFMT_END);
			return;
		}

		if(uiID)
		{
			Power *ppow = character_FindPowerByID(pchar,uiID);
			PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
			if(pdef && powerdef_EmitCustomizable(pdef))
			{
				character_SetPowerEmit(pchar,uiID,pEmit ? pEmit->cpchName : NULL);
			}
		}
		else
		{
			int i;
			for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
			{
				PowerDef *pdef = GET_REF(pchar->ppPowers[i]->hDef);
				if(pdef && powerdef_EmitCustomizable(pdef))
				{
					character_SetPowerEmit(pchar,pchar->ppPowers[i]->uiID,pEmit ? pEmit->cpchName : NULL);
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_NAME(PowerEmit) ACMD_LIST(gEntConCmdList) ACMD_HIDE;
void entConPowerEmit(Entity *e, U32 uiID, ACMD_NAMELIST("PowerEmit", REFDICTIONARY) char *pchEmit)
{
	cmdPowerEmit(NULL,e,uiID,pchEmit);
}


// PowerEntCreateCostume <PowerID> <EntCreateCostume>: Sets the EntCreateCostume of the Power.  PowerID of 0 applies to all Powers, EntCreateCostume of 0 reverts to default.
AUTO_COMMAND ACMD_NAME(PowerEntCreateCostume) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void cmdPowerEntCreateCostume(CmdContext *context, Entity *e, U32 uiID, S32 iEntCreateCostume)
{
	if(e && e->pChar && GamePermission_EntHasToken(e, GAME_PERMISSION_POWER_PET_COSTUME))
	{
		Character *pchar = e->pChar;

		if(uiID)
		{
			Power *ppow = character_FindPowerByID(pchar,uiID);
			PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
			AttribModDef *pmoddef;
			if(pdef && (pmoddef=powerdef_EntCreateCostumeCustomizable(pdef)))
			{
				int iKey = 0;
				if(iEntCreateCostume>0)
				{
					CritterDef *pdefCritter = GET_REF(((EntCreateParams*)pmoddef->pParams)->hCritter);
					if(pdefCritter)
						iKey = critterdef_GetCostumeKeyFromIndex(pdefCritter,iEntCreateCostume);
				}
				character_SetPowerEntCreateCostume(pchar,uiID,iKey);
			}
		}
		else
		{
			int i;
			for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
			{
				if(pchar->ppPowers[i]->uiID)
					cmdPowerEntCreateCostume(context,e,pchar->ppPowers[i]->uiID,iEntCreateCostume);
			}
		}
	}
}

AUTO_COMMAND ACMD_NAME(PowerEntCreateCostume) ACMD_LIST(gEntConCmdList) ACMD_HIDE;
void entConPowerEntCreateCostume(Entity *e, U32 uiID, S32 iEntCreateCostume)
{
	cmdPowerEntCreateCostume(NULL,e,uiID,iEntCreateCostume);
}


// PowerStance <PowerName>: Sets the default stance of the entity to the given Power.  Any unknown Power will set the default stance empty.
AUTO_COMMAND ACMD_NAME(PowerStance) ACMD_LIST(gEntConCmdList);
void CmdPowerStance(Entity *e, ACMD_NAMELIST("PowerDef", REFDICTIONARY) char *pchPowerName)
{
	if(e && e->pChar)
	{
		character_SetDefaultStance(entGetPartitionIdx(e),e->pChar,powerdef_Find(pchPowerName));
	}
}

// SetClass <ClassName>: Sets the class of the entity
AUTO_COMMAND ACMD_NAME(SetClass) ACMD_LIST(gEntConCmdList);
void CmdSetClass(Entity *e, ACMD_NAMELIST("CharacterClass", REFDICTIONARY) char *pchClassName)
{
	if(e && e->pChar)
	{
		character_SetClass(e->pChar,pchClassName);
	}
}

// SetSubtarget <CategoryName>: Sets the subtarget category of the entity
AUTO_COMMAND ACMD_NAME(SetSubtarget) ACMD_LIST(gEntConCmdList);
void CmdSetSubtarget(Entity *e, ACMD_NAMELIST("PowerSubtargetCategory", REFDICTIONARY) char *pchCategory)
{
	if(e && e->pChar)
	{
		PowerSubtargetCategory *pcat = RefSystem_ReferentFromString(g_hPowerSubtargetCategoryDict,pchCategory);
		if(pcat)
		{
			character_SetSubtargetCategory(e->pChar,pcat);
		}
		else
		{
			character_ClearSubtarget(e->pChar);
		}
	}
}



AUTO_COMMAND ACMD_NAME(setgang) ACMD_LIST(gEntConCmdList);
void entConSetGang(Entity *e, U32 Value )
{
	if(e && e->pChar)
	{
		e->pChar->gangID = Value;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	}
}

AUTO_COMMAND ACMD_NAME(duel) ACMD_LIST(gEntConCmdList);
void entConDuelTarget(Entity *e, const char* target)
{
	Entity *etarget = entGetClientTarget(e, target, NULL);

	gslPVPDuelRequest(e, etarget);
}

AUTO_COMMAND ACMD_NAME(duelaccept) ACMD_LIST(gEntConCmdList);
void entConDuelAccept(Entity *e)
{
	gslPVPDuelAccept(e);
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME(GetCritterDescription);
void entCmdCritterDescription(Entity *e, EntityRef iRef)
{
	Entity *pEnt = e ? entFromEntityRef(entGetPartitionIdx(e), iRef) : NULL;
	char *estrResult = NULL;

	if (e && e->pPlayer && pEnt)
	{
		estrStackCreate(&estrResult);
		langFormatGameString(e->pPlayer->langID, &estrResult, "{Entity.Critter.Description}",
			STRFMT_ENTITY(pEnt),
			STRFMT_END);
	}

	ClientCmd_SetCritterDescription(e, iRef, NULL_TO_EMPTY(estrResult));

	if (estrResult)
	{
		estrDestroy(&estrResult);
	}
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME(GetCritterGroupDescription);
void entCmdCritterGroupDescription(Entity *e, EntityRef iRef)
{
	Entity *pEnt = e ? entFromEntityRef(entGetPartitionIdx(e), iRef) : NULL;
	char *estrResult = NULL;

	if (e && e->pPlayer && pEnt)
	{
		estrStackCreate(&estrResult);
		langFormatGameString(e->pPlayer->langID, &estrResult, "{Entity.Critter.Group.Description}",
			STRFMT_ENTITY(pEnt),
			STRFMT_END);
	}

	ClientCmd_SetCritterGroupDescription(e, iRef, NULL_TO_EMPTY(estrResult));

	if (estrResult)
	{
		estrDestroy(&estrResult);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void entCmdReportOnClickTarget(Entity *e, EntityRef entRef)
{
	Entity *pEnt = e ? entFromEntityRef(entGetPartitionIdx(e), entRef) : NULL;
	if (e && pEnt)
	{
		if (entCheckFlag(pEnt, ENTITYFLAG_CIV_PROCESSING_ONLY))
		{
			aiCivilianOnClick(pEnt, e);
		}
		else if (pEnt->pCritter && pEnt->aibase)
		{
			AIConfig *pConfig = aiGetConfig(pEnt, pEnt->aibase);
			if (pConfig && pConfig->doesCivilianBehavior)
			{
				aiCivilianOnClick(pEnt, e);				
			}
		}

	}
}
