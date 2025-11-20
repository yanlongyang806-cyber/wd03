#include "pvp_common.h"
#include "pvp_common_h_ast.h"

#include "Character.h"
#include "Character_target.h"
#include "chatCommon.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntityLib.h"
#include "entEnums_h_ast.h"
#include "GlobalTypes.h"
#include "player.h"
#include "RegionRules.h"
#include "StringCache.h"
#include "wlVolumes.h"
#include "WorldGrid.h"
#include "mapstate_common.h"

AUTO_RUN_ANON(memBudgetAddMapping("PVPDuelState", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("PVPFlag", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

const char* pvpastring;
const char* pvpbstring;

int pvpIgnoreMapType = 0;
AUTO_CMD_INT(pvpIgnoreMapType, pvpIgnoreMapType);
DictionaryHandle g_PVPGroupDefs;

AUTO_RUN;
void pvpRegisterPooledStrings(void)
{
	pvpastring = allocAddStaticString("pvpTeamA");
	pvpbstring = allocAddStaticString("pvpTeamB");
}

EntityRelation entity_PVP_GetFlagHalfRelationIn(PVPFlag *flag)
{
	if(flag->infectious)
	{
		if(flag->infect_heal)
			return kEntityRelation_FriendAndFoe;
		return kEntityRelation_Foe;
	}

	return kEntityRelation_Neutral;
}

EntityRelation entity_PVP_GetFlagHalfRelationOut(PVPFlag *flag)
{
	if(flag->infectious && flag->infect_push)
		return kEntityRelation_Foe;
	
	if(flag->combat_out)
		return kEntityRelation_Unknown;

	return kEntityRelation_Neutral;
}

// -1 = no relation here, 0 = friendly, 1 = enemy, 2 = neither friendly nor enemy, but defined
int entity_PVP_GetRelation(Entity *e1, Entity *e2)
{
	PVPFlag *flag1, *flag2;
	PVPTeamFlag *teamFlag1, *teamFlag2;

	if(!e1 || !e2)
		return kEntityRelation_Unknown;
	if(!e1->pChar || !e2->pChar)
		return kEntityRelation_Unknown;
	
	flag1 = e1->pChar->pvpFlag;
	flag2 = e2->pChar->pvpFlag;
	teamFlag1 = e1->pChar->pvpTeamDuelFlag;
	teamFlag2 = e2->pChar->pvpTeamDuelFlag;

	if(!flag1 && !flag2 && !teamFlag1 && !teamFlag2)
		return kEntityRelation_Unknown;

	if(!flag2 && flag1 && !teamFlag2)
		return entity_PVP_GetFlagHalfRelationOut(flag1);

	if(!flag1 && flag2 && !teamFlag1)
	{
		if(e1->pPlayer)
			return entity_PVP_GetFlagHalfRelationIn(flag2);
		return kEntityRelation_Unknown;
	}

	if(teamFlag1 || teamFlag2)
	{
		if(!teamFlag1 && !e1->pPlayer)
			return kEntityRelation_Unknown;

		if(!teamFlag2 && !e2->pPlayer)
			return kEntityRelation_Unknown;

		if(teamFlag1 && teamFlag2)
		{
			if(teamFlag1->group != teamFlag2->group)
				return kEntityRelation_Neutral;

			if(teamFlag1->eState == DuelState_Active || teamFlag2->eState == DuelState_Active)
			{
				if(teamFlag1->eState != DuelState_Active || teamFlag2->eState != DuelState_Active)
					return kEntityRelation_Neutral;

				if(teamFlag1->team != teamFlag2->team)
					return kEntityRelation_Foe;

				return kEntityRelation_Friend;
			}

			if(teamFlag1->team != teamFlag2->team)
				return kEntityRelation_Neutral;

			return kEntityRelation_Friend;
		}

		return kEntityRelation_Neutral;
	}
	
	if(flag1->group!=flag2->group)
		return kEntityRelation_Neutral;

	if(flag1->subgroup!=flag2->subgroup)
		return kEntityRelation_Foe;

	
	return kEntityRelation_Friend;
}

int pvpCanDuelInArea(Entity *e)
{
	static U32 vt_duel_enable = 0;
	static U32 vt_duel_disable = 0;

	int i;
	const WorldVolume **volumes;
	RegionRules *rules;
	Vec3 pos;

	if(!vt_duel_disable)
		vt_duel_disable = wlVolumeTypeNameToBitMask("DuelDisable");
	if(!vt_duel_enable)
		vt_duel_enable = wlVolumeTypeNameToBitMask("DuelEnable");

	volumes = wlVolumeCacheGetCachedVolumes(e->volumeCache);
	 
	for(i=0; i<eaSize(&volumes); i++)
	{
		const WorldVolume *vol = volumes[i];

		if(wlVolumeIsType(vol, vt_duel_enable))
			return 1;
		if(wlVolumeIsType(vol, vt_duel_disable))
			return -1;
	}

	entGetPos(e, pos);
	rules = RegionRulesFromVec3(pos);
	if(rules)
	{
		if(rules->bEnableDueling)
			return 1;
		if(rules->bDisableDueling)
			return -1;
	}

	if(zmapInfoGetMapType(NULL)!=ZMTYPE_STATIC && !pvpIgnoreMapType)
		return -1;

	return 0;
}

int pvpCanDuel(Entity *e1, Entity *e2)
{
	CritterFaction *f1, *f2;
	int vol_res_1, vol_res_2;
	int e1_pidx = e1 ? entGetPartitionIdx(e1) : 0;
	int e2_pidx = e2 ? entGetPartitionIdx(e2) : 0;
	
	if ( zmapInfoGetDisableDuels(NULL) || mapState_ArePVPQueuesDisabled(mapState_FromPartitionIdx(e1_pidx)) || e1_pidx != e2_pidx)
		return 0;
	if(!e1 || !e2) 
		return 0;
	if(e1->pCritter || e2->pCritter)  // Can't duel critters
		return 0;
	if(!e1->pChar || !e2->pChar)		// Can only duel combat enabled stuff
		return 0;
	//(BH) if pchar->piPowerModes changes to SELF_ONLY, then this won't work on the client
	if(entIsInCombat(e1) || entIsInCombat(e2))  // Can't duel people in combat
		return 0;
	f1 = entGetFaction(e1);	f2 = entGetFaction(e2);
	if(f1 && (f1->pchName==pvpastring || f1->pchName==pvpbstring))
		return 0;
	if(f2 && (f2->pchName==pvpastring || f2->pchName==pvpbstring))   // Can't duel pvping players (hacky)
		return 0;
	if(entity_GetRelation(e1_pidx, e1, e2)!=kEntityRelation_Friend) // Can't duel enemies or other pvp'ers
		return 0;
	if(e1->pChar->pvpDuelState || e2->pChar->pvpDuelState)  // Can't duel people dueling or infected
		return 0;
	if(e1->pChar->pvpTeamDuelFlag || e2->pChar->pvpTeamDuelFlag) // Can't duel people who are currently in a team duel
		return 0;

	//If either the to or the from has whitelisting on, you cannot duel.
	// NOTE: This function does not work on the client since the friends list is self-only (BH)
	if(!entIsWhitelisted(e2, e1, kPlayerWhitelistFlags_PvPInvites) || !entIsWhitelisted(e1, e2, kPlayerWhitelistFlags_PvPInvites))
		return 0;
	// NOTE: This check does not work well on the client since the ignore list is self-only (BH)
	if(e1->pPlayer && e2->pPlayer && 
		(ChatCommon_FindIgnoreByAccount(e1, e2->pPlayer->accountID) ||
		 ChatCommon_FindIgnoreByAccount(e2, e1->pPlayer->accountID)))
		return 0;
	if((e1->pPlayer && e1->pPlayer->iStasis) || (e2->pPlayer && e2->pPlayer->iStasis)) // Can't duel players in stasis.
		return 0;
	if((e1->pPlayer && e1->pPlayer->bStuckRespawn) || (e2->pPlayer && e2->pPlayer->bStuckRespawn)) //Can't duel while stuck and waiting to respawn
		return 0;

	vol_res_1 = pvpCanDuelInArea(e1);
	vol_res_2 = pvpCanDuelInArea(e2);

	if(vol_res_1>0 && vol_res_2>0)
		return 1;
	if(vol_res_1<0 || vol_res_2<0)
		return 0;

	return 1;
}

int pvpCanTeamDuel(Entity *e1, Entity *e2)
{
	if(!e1 || !e2)
		return 0;

	if(!e2->pTeam || !e2->pTeam->eState == TeamState_Member || e2->pTeam->iTeamID == 0)
		return 0;

	if(e1->pTeam && e1->pTeam->iTeamID != 0 && e1->pTeam->eState == TeamState_Member && e1->pTeam->iTeamID == e2->pTeam->iTeamID)
		return 0;

	return pvpCanDuel(e1,e2);
}

AUTO_STARTUP(PVPCommon) ASTRT_DEPS();
void pvpCommonInit(void)
{
	/*
	g_PVPGroupDefs = RefSystem_RegisterSelfDefiningDictionary("PVPGroupDef", false, parse_PVPGroupDef, true, false, NULL);

	resLoadResourcesFromDisk(g_PVPGroupDefs, "defs/pvp/", ".pvpgrp", "PVPGroups.bin", PARSER_OPTIONALFLAG|RESOURCELOAD_SHAREDMEMORY);
	*/
}

#include "AutoGen/pvp_common_h_ast.c"
