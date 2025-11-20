#include "aiGroupCombat.h"

// AILib includes
#include "aiConfig.h"
#include "aiLib.h"
#include "aiMultiTickAction.h"
#include "aiStruct.h"
#include "aiTeam.h"

// WorldLib
#include "RegionRules.h"

// Utilities
#include "fileutil.h"
#include "FolderCache.h"
#include "referencesystem.h"
#include "rand.h"
#include "ResourceManager.h"

// GSL
#include "gslMapState.h"

// Autogen includes
#include "aiGroupCombat_h_ast.h"

DictionaryHandle g_AIGroupCombatDict;

static AIGroupCombatSettings* aigcGetSettings(const Vec3 pos)
{
	RegionRules *rules = RegionRulesFromVec3(pos);
	AIGroupCombatSettings *settings = NULL;

	if(aiGlobalSettings.aiGroupCombatDefaults)
		settings = RefSystem_ReferentFromString(g_AIGroupCombatDict, aiGlobalSettings.aiGroupCombatDefaults);

	if(rules && rules->aiGroupCombatSettings)
	{
		AIGroupCombatSettings *specific = RefSystem_ReferentFromString(g_AIGroupCombatDict, rules->aiGroupCombatSettings);

		if(specific)
			settings = specific;
	}

	return settings;
}

void aigcTeamEnterCombat(AITeam *team)
{
	Vec3 teamLeashPoint;
	AIGroupCombatSettings *settings;

	aiTeamGetLeashPosition(team, teamLeashPoint);
	settings = aigcGetSettings(teamLeashPoint);
	
	if(settings)
		SET_HANDLE_FROM_STRING(g_AIGroupCombatDict, settings->name, team->aigcSettings);

	// Reset combat tokens and timers
	team->combatTokenAccum = 0;
	FOR_EACH_IN_EARRAY(team->members, AITeamMember, member)
	{
		member->numCombatTokens = 0;
		member->timeCombatActiveEnded = 0;
		member->timeCombatActiveExpires = 0;
		member->timeCombatActiveStarted = 0;
		member->timeCombatInactiveExpires = 0;
	}
	FOR_EACH_END

	eaClearFast(&team->activeCombatants);
}

void aigcTeamExitCombat(AITeam *team)
{
	REMOVE_HANDLE(team->aigcSettings);
	eaClearFast(&team->activeCombatants);
}

static int aigcSetActive(AITeam *team, AITeamMember *member, AIGroupCombatSettings *settings, int token, F32 elapsed)
{
	int partitionIdx = entGetPartitionIdx(member->memberBE);

	eaPush(&team->activeCombatants, member);

	// Since we're now adding the combatant, add his combat tokens for this tick
	if(settings->onlyCombatantsGenerateTokens)
	{
		Entity *memberE = member->memberBE;
		AIVarsBase *memberAIB = memberE->aibase;
		AIConfig *memberConfig = aiGetConfig(memberE, memberAIB);

		member->numCombatTokens += memberConfig->combatTokenRateSelf * elapsed;
		team->combatTokenAccum += memberConfig->combatTokenRateSocial * elapsed;
	}

	member->timeCombatActiveStarted = ABS_TIME_PARTITION(partitionIdx);
	member->timeCombatActiveEnded = 0;
	member->timeCombatActiveExpires = 0;
	if(token)
		member->timeCombatActiveExpires = ABS_TIME_PARTITION(partitionIdx)+SEC_TO_ABS_TIME(settings->tokenActiveDuration);

	return 1;
}

static void aigcSetInactive(AITeam *team, AITeamMember *member, AIGroupCombatSettings *settings, int token)
{
	int res = eaFindAndRemoveFast(&team->activeCombatants, member);
	int partitionIdx = entGetPartitionIdx(member->memberBE);

	if(res==-1)
		return;

	member->timeCombatActiveStarted = 0;
	member->timeCombatActiveEnded = ABS_TIME_PARTITION(partitionIdx);
	member->timeCombatActiveExpires = 0;

	member->timeCombatInactiveExpires = ABS_TIME_PARTITION(partitionIdx)+SEC_TO_ABS_TIME(settings->generalInactiveDuration);
}

void aigcEntDestroyed(Entity* e)
{
	AITeam *team = aiTeamGetCombatTeam(e, e->aibase);
	if (!team)
		return;
	FOR_EACH_IN_EARRAY(team->activeCombatants, AITeamMember, member)
	{
		if(member->memberBE==e)
		{
			eaRemoveFast(&team->activeCombatants, FOR_EACH_IDX(team->activeCombatants, member));
		}
	}
	FOR_EACH_END
}

static AITeamMember* aigcFindInactive(AITeam* team, S32 ignoreInactiveTimer)
{
	FOR_EACH_IN_EARRAY(team->members, AITeamMember, member)
	{
		int partitionIdx = entGetPartitionIdx(member->memberBE);

		// Active
		if(member->timeCombatActiveStarted>0)
			continue;

		if(!ignoreInactiveTimer && member->timeCombatInactiveExpires>ABS_TIME_PARTITION(partitionIdx))
			continue;

		return member;
	}
	FOR_EACH_END

	return NULL;
}

static AITeamMember* aigcGetWeightedActive(AITeam *team)
{
	// Weights are token generation rates (self+social if both specified)
	static AITeamMember **validMembers = NULL;
	F32 totalWeight = 0;
	F32 roll;

	eaClearFast(&validMembers);
	FOR_EACH_IN_EARRAY(team->activeCombatants, AITeamMember, member)
	{
		if(aiMultiTickAction_HasAction(member->memberBE, member->memberBE->aibase))
			continue;

		eaPush(&validMembers, member);
	}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY(validMembers, AITeamMember, member)
	{
		AIConfig *memberConfig = aiGetConfig(member->memberBE, member->memberBE->aibase);

		totalWeight += memberConfig->combatTokenRateSelf + memberConfig->combatTokenRateSocial;
	}
	FOR_EACH_END

	roll = randomPositiveF32()*totalWeight;

	FOR_EACH_IN_EARRAY(validMembers, AITeamMember, member)
	{
		AIConfig *memberConfig = aiGetConfig(member->memberBE, member->memberBE->aibase);

		roll -= memberConfig->combatTokenRateSelf + memberConfig->combatTokenRateSocial;

		if(roll<=0)
			return member;
	}
	FOR_EACH_END

	return eaGet(&validMembers, randomIntRange(0, eaSize(&validMembers)));
}

static S32 aigcGetActiveCount(AITeam *team, int includeToken)
{
	S32 count = 0;

	FOR_EACH_IN_EARRAY(team->activeCombatants, AITeamMember, member)
	{
		Entity *memberE = member->memberBE;
		AIVarsBase *memberAIB = memberE->aibase;
		AITeamMember *combatMember = aiTeamGetMember(memberE, memberAIB, team);

		if(combatMember->timeCombatActiveExpires==0)
			count++;
		else if(combatMember->timeCombatActiveExpires>0 && includeToken)
			count++;
	}
	FOR_EACH_END

	return count;
}

static S32 aigcCountPlayerOpponents(AITeam *team)
{
	S32 count = 0;

	FOR_EACH_IN_EARRAY(team->statusTable, AITeamStatusEntry, teamStatus)
	{
		Entity *e = entFromEntityRef(team->partitionIdx, teamStatus->entRef);

		if(e && entIsPlayer(e))
			count++;
	}
	FOR_EACH_END

	return count;
}

static S32 aigcGetNumCombatants(AITeam *team, AIGroupCombatSettings *settings)
{
	F32 count = settings->numCombatants;
	S32 numTeammates = eaSize(&team->members);
	S32 numPlayers;

	if(settings->numAddtlCombatantsPerTeammate && numTeammates>settings->baseTeammatesForAddtlCombatants)
		count += (F32)settings->numAddtlCombatantsPerTeammate * (numTeammates - settings->baseTeammatesForAddtlCombatants);

	if(settings->numAddtlCombatantsPerPlayer)
	{
		numPlayers = aigcCountPlayerOpponents(team);

		if(numPlayers>settings->basePlayersForAddtlCombatants)
			count += (F32)settings->numAddtlCombatantsPerPlayer * (numPlayers - settings->basePlayersForAddtlCombatants);
	}

	return (S32)count;
}

void aigcTick(AITeam *team, F32 elapsed)
{
	AIGroupCombatSettings *settings = NULL;
	S32 activeCount;
	S32 numCombatants;

	if(team->combatState!=AITEAM_COMBAT_STATE_FIGHT)
		return;

	settings = GET_REF(team->aigcSettings);

	if(!settings)
		return;

	if(settings->tokenSetting!=AITS_TokensNotUsed)
	{
		AITeamMember **memberArray = settings->onlyCombatantsGenerateTokens ? team->activeCombatants : team->members;
		
		FOR_EACH_IN_EARRAY(memberArray, AITeamMember, member)
		{
			Entity *memberE = member->memberBE;
			AIVarsBase *memberAIB = memberE->aibase;
			AIConfig *memberConfig = aiGetConfig(memberE, memberAIB);

			team->combatTokenAccum += memberConfig->combatTokenRateSocial * elapsed;

			// If everyone generates tokens, don't let them simply accrue infinite personal attack tokens while not active
			if(settings->tokenSetting==AITS_TokensForActive ||
				member->timeCombatActiveStarted>0)
			{
				member->numCombatTokens += memberConfig->combatTokenRateSelf * elapsed;
			}
		}
		FOR_EACH_END
	}

	// Check expiration
	FOR_EACH_IN_EARRAY(team->activeCombatants, AITeamMember, member)
	{
		int partitionIdx = entGetPartitionIdx(member->memberBE);

		if(settings->tokenSetting==AITS_TokensForActive)
		{
			if(member->timeCombatActiveExpires>0 && member->timeCombatActiveExpires<=ABS_TIME_PARTITION(partitionIdx))
				aigcSetInactive(team, member, settings, true);
		}

		if(!aiIsEntAlive(member->memberBE))
			aigcSetInactive(team, member, settings, false);

		if(settings->generalActiveDuration &&
			ABS_TIME_SINCE_PARTITION(partitionIdx, member->timeCombatActiveStarted)>SEC_TO_ABS_TIME(settings->generalActiveDuration))
		{
			aigcSetInactive(team, member, settings, false);
		}
	}
	FOR_EACH_END

	activeCount = aigcGetActiveCount(team, false);
	numCombatants = aigcGetNumCombatants(team, settings);
	if(activeCount<numCombatants)
	{
		// Members are sorted in descending rank
		int i;
		int remaining = numCombatants - activeCount;
		
		if(settings->tokenSetting==AITS_TokensForActive && team->combatTokenAccum>=1)
		{
			int adds;
			
			// Find anyone whose tokens >1 (self tokens)
			FOR_EACH_IN_EARRAY(team->members, AITeamMember, member)
			{
				if(member->timeCombatActiveStarted>0)
					continue;

				if(member->numCombatTokens>1)
				{
					member->numCombatTokens -= 1;
					aigcSetActive(team, member, settings, true, elapsed);
				}
			}
			FOR_EACH_END
			
			adds = team->combatTokenAccum;
			team->combatTokenAccum -= adds;
			remaining += adds;
		}

		for(i=0; i<eaSize(&team->members) && remaining>0; i++)
		{
			AITeamMember *member = team->members[i];

			if(member->timeCombatActiveStarted>0)
				continue;

			if(aigcSetActive(team, member, settings, false, elapsed))
			{
				remaining--;

				// Recalc this in case this team member completed a token?
				if(settings->tokenSetting==AITS_TokensForActive && team->combatTokenAccum>=1)
				{
					int adds = team->combatTokenAccum;

					team->combatTokenAccum -= adds;
					remaining += adds;
				}
			}
		}
	}
	else if(activeCount>settings->numCombatants)
	{
		int i;
		int remaining = activeCount - settings->numCombatants;

		for(i=eaSize(&team->activeCombatants)-1; i>=0 && remaining>0; i--)
		{
			AITeamMember *member = team->activeCombatants[i];

			// Only remove non-token members
			if(member->timeCombatActiveExpires==0)
			{
				aigcSetInactive(team, member, settings, false);
				remaining--;
			}
		}
	}

	if(settings->tokenSetting!=AITS_TokensNotUsed)
	{
		for(; team->combatTokenAccum>=1; team->combatTokenAccum--)
		{
			switch(settings->tokenSetting)
			{
				xcase AITS_TokensForActive: {
					AITeamMember *toAdd = aigcFindInactive(team, false);

					if(!toAdd && settings->forceInactiveForToken)
						toAdd = aigcFindInactive(team, true);

					if(toAdd)
						aigcSetActive(team, toAdd, settings, true, elapsed);
					else
					{
						return;
					}
				}
				xcase AITS_TokensForAttacks: {
					AITeamMember *receiver = aigcGetWeightedActive(team);

					if(receiver)
						receiver->numCombatTokens += 1;
					else
					{
						//No active members somehow
						return;
					}
				}
			}
		}
	}
}

AUTO_FIXUPFUNC;
TextParserResult fixupAIGroupCombatSettings(AIGroupCombatSettings* config, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_CONSTRUCTOR: {
			
		}

		xcase FIXUPTYPE_POST_ALL_TEXT_READING_AND_INHERITANCE_DURING_LOADFILES: {
			
		}

		xcase FIXUPTYPE_POST_RELOAD: {
			
		}
	}

	return 1;
}


static void aigcReload(const char *relPath, int when)
{
	loadstart_printf("Reloading AIGroupCombat...");

	fileWaitForExclusiveAccess(relPath);
	errorLogFileIsBeingReloaded(relPath);

	ParserReloadFileToDictionary(relPath, g_AIGroupCombatDict);

	loadend_printf(" done (%d)", RefSystem_GetDictionaryNumberOfReferentInfos(g_AIGroupCombatDict));
}

AUTO_STARTUP(AIGroupCombat);
void aigcLoad(void)
{
	loadstart_printf("Loading AIGroupCombatSettings...");

	resLoadResourcesFromDisk(g_AIGroupCombatDict, "ai/GroupCombat/", ".aigc", "AIGroupCombat.bin", PARSER_SERVERSIDE|PARSER_OPTIONALFLAG);

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ai/GroupCombat/*.aigc", aigcReload);

	loadend_printf(" done (%d).", RefSystem_GetDictionaryNumberOfReferents(g_AIGroupCombatDict));
}

AUTO_RUN;
void aigcRegisterDict(void)
{
	g_AIGroupCombatDict = RefSystem_RegisterSelfDefiningDictionary("AIGroupCombatSettings", false, parse_AIGroupCombatSettings, true, true, NULL);
}


#include "aiGroupCombat_h_ast.c"
