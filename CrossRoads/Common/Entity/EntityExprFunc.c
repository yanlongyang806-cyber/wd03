#include "Character.h"
#include "ChoiceTable_common.h"
#include "encounter_common.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "EString.h"
#include "Expression.h"
#include "GameAccountDataCommon.h"
#include "Guild.h"
#include "HashFunctions.h"
#include "ItemAssignments.h"
#include "PowerActivation.h"
#include "rand.h"
#include "SavedPetCommon.h"
#include "species_common.h"
#include "StashTable.h"
#include "StringCache.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "../StaticWorld/ZoneMap.h"
#include "ExpressionFunc.h"
#include "Player.h"
#include "skills_dd.h"
#include "CharacterClass.h"
#include "Skills_DD.h"
#include "AttribModFragility.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "CostumeCommonentity.h"
#include "storeCommon.h"
#include "GamePermissionsCommon.h"
#include "InventoryCommon.h"
#include "itemEnums.h"
#include "itemEnums_h_ast.h"
#include "Guild_h_ast.h"

#if GAMESERVER || GAMECLIENT
	#include "EntityMovementManager.h"
	#include "PowersMovement.h"
	#include "EntityMovementDefault.h"
#endif

#if GAMESERVER
	#include "CombatConfig.h"
	#include "GameEvent.h"
	#include "gslInteractable.h"
	#include "gslEventTracker.h"
	#include "aiFCExprFunc.h"
	#include "aiCombatRoles.h"
	#include "AutoGen/UI2Lib_autogen_ClientCmdWrappers.h"
	#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#endif

#include "AutoGen/SoundLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

#include "EntityExprFunc_c_ast.h"
#include "rewardCommon_h_ast.h"

extern StaticDefineInt WorldRegionTypeEnum[];
extern ParseTable parse_Entity[];
#define TYPE_parse_Entity Entity

// Name of a player in the expression context.  Defined in mission_common.c
extern const char *g_PlayerVarName;

int OVERRIDE_LATELINK_exprEvaluateRuntimeEntArrayFromLookupEntry(ParseTable* table, void* ptr, Entity** entOut)
{
	if(table == parse_Entity)
	{
		*entOut = ptr;
		return true;
	}
	else
		return false;
}

int OVERRIDE_LATELINK_exprEvaluateRuntimeEntFromEntArray(Entity** ents, ParseTable* table, Entity** entOut)
{
	if(table == parse_Entity)
	{
		*entOut = eaGet(&ents, 0);
		return true;
	}

	return false;
}

int OVERRIDE_LATELINK_exprFuncHelperEntIsAlive(Entity* e)
{
	return entIsAlive(e);
}

U32 OVERRIDE_LATELINK_exprFuncHelperGetEntRef(Entity* e)
{
	return entGetRef(e);
}

Entity* OVERRIDE_LATELINK_exprFuncHelperBaseEntFromEntityRef(int iPartitionIdx, U32 ref)
{
	return entFromEntityRef(iPartitionIdx, ref);
}

AUTO_EXPR_FUNC(player) ACMD_NAME(IsPlayer);
int exprFuncIsPlayer(ACMD_EXPR_SELF Entity* e)
{
	if(e && e->pPlayer)
		return true;

	return false;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
int exprFuncPlayerXPLevel_StaticCheck(ExprContext* context)
{
#ifdef GAMESERVER
	MissionDef* missionDef = exprContextGetVarPointerUnsafePooled(context, "MissionDef");
	if (missionDef)
	{
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		pEvent->type = EventType_LevelUp;
		if (missiondef_GetType(missionDef) != MissionType_OpenMission) {
			pEvent->pchEventName = allocAddString("PlayerLevelChange");
			pEvent->tMatchSource = TriState_Yes;
		} else {
			pEvent->pchEventName = allocAddString("AnyPlayerLevelChange");
		}
		eventtracker_AddNamedEventToList(&missionDef->eaTrackedEventsNoSave, pEvent, missionDef->filename);
	}
#endif
	return 0;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerXPLevel) ACMD_EXPR_STATIC_CHECK(exprFuncPlayerXPLevel_StaticCheck);
int exprFuncPlayerXPLevel(ExprContext* context)
{
	Entity* be = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	int level = 0;

	// There's no reason this function couldn't also get the level of a critter, except that
	// it might confuse designers.  Return 0 if this isn't a player.
	if(!be || !be->pPlayer)
		return 0;

	if(be)
		level = entity_GetSavedExpLevel(be);

	return level;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerCombatLevel);
int exprFuncPlayerCombatLevel(ExprContext* context)
{
	Entity* e = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	int level = 0;

	// There's no reason this function couldn't also get the level of a critter, except that
	// it might confuse designers.  Return 0 if this isn't a player.
	if(!e || !e->pPlayer)
		return 0;

	if(e && e->pChar)
		level = e->pChar->iLevelCombat;

	return level;
}

AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(PlayerSidekickLevelDelta);
int exprFuncCharacterSidekickLevelDelta(ExprContext* context, Character* pChar)
{
	int level = 0;

	if(pChar && pChar->pEntParent)
		level = (pChar->iLevelCombat - entity_GetSavedExpLevel(pChar->pEntParent));

	return level;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetMyLevel);
int exprFuncGetMyLevel(ACMD_EXPR_SELF Entity* e)
{
	int level = 0;

	if(e && e->pChar)
		level = e->pChar->iLevelCombat;

	return level;
}

// Returns the combat level of the Entity in the expressino context.  Returns 0 on failure.
AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME("EntCharGetLevelCombat");
S32 exprEntCharGetLevelCombat(ExprContext* context)
{
	Entity *pEntity = exprContextGetVarPointerUnsafe(context, "Source");
	if (!pEntity) pEntity = exprContextGetSelfPtr(context);
	if (pEntity && pEntity->pChar)
	{
		return pEntity->pChar->iLevelCombat;
	}
	return 0;
}

// Returns the average level of the context Entity's team.
AUTO_EXPR_FUNC(player) ACMD_NAME("PlayerGetTeamLevel");
int exprPlayerGetTeamLevel(ExprContext *pContext)
{
	Entity *pPlayer = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pPlayer)
		return encounter_getTeamLevelInRange(pPlayer, NULL, false);
	return 0;
}

// Returns true if the player is solo or the team leader
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(PlayerIsTeamLeaderOrSolo);
int exprPlayerIsTeamLeaderOrSolo(ExprContext *pContext)
{
	Entity *pPlayer = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if(pPlayer && pPlayer->pChar)
	{
		if(!team_IsWithTeam(pPlayer) || team_IsTeamLeader(pPlayer))
		{
			return true;
		}	
	}
	return false;
}

// Returns the average level of the group, ignoring non-combat characters, rounds down
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GetEntArrayCombatLevel);
int exprFuncGetEntArrayLevel(ACMD_EXPR_ENTARRAY_IN ents)
{
	int totalLevel = 0;
	int count = 0;
	int i;

	for(i=eaSize(ents)-1; i>=0; i--)
	{
		Entity *e = (*ents)[i];

		if(e && e->pChar)
		{
			totalLevel += e->pChar->iLevelCombat;
			count++;
		}
	}

	if(count)
		return totalLevel / count;

	return 0;
}

// Turns the invincible flag for this entity on or off 
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetInvincible);
void exprFuncSetInvincible(ACMD_EXPR_SELF Entity* e, S32 on)
{
	if(e->pChar)
	{
		e->pChar->bInvulnerable = !!on;
		e->pChar->bUnstoppable = !!on;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	}
}

// Turns invisibility for this entity on or off 
// NOTE: invisibility in this context means the entity doesn't get sent down to any clients
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetInvisible);
void exprFuncSetInvisible(ACMD_EXPR_SELF Entity* e, S32 on)
{
	if(on)
		entSetDataFlagBits(e, ENTITYFLAG_DONOTSEND);
	else
		entClearDataFlagBits(e, ENTITYFLAG_DONOTSEND);
}

// Turns the powers unstoppable flag for this entity on or off 
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetUnstoppable);
void exprFuncSetUnstoppable(ACMD_EXPR_SELF Entity* e, S32 on)
{
	if(e->pChar)
	{
		e->pChar->bUnstoppable = !!on;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	}
}

// Turns the powers unkillable flag for this entity on or off 
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetUnkillable);
void exprFuncSetUnkillable(ACMD_EXPR_SELF Entity* e, S32 on)
{
	if(e->pChar)
	{
		e->pChar->bUnkillable = !!on;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	}
}

// Turns the "safe" state for this Entity on or off
AUTO_EXPR_FUNC(ai);
void SetSafe(ACMD_EXPR_SELF Entity *e, S32 on)
{
#ifdef GAMESERVER
	if(e->pChar)
	{
		e->pChar->bSafe = !!on;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
		if(on && g_CombatConfig.fCooldownGlobal)
		{
			MAX1(e->pChar->fCooldownGlobalTimer,g_CombatConfig.fCooldownGlobal);
			ClientCmd_SetCooldownGlobalClient(e,e->pChar->fCooldownGlobalTimer);
		}
	}
#endif GAMESERVER
}


// Turns the powers disableface flag for this entity on or off 
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetDisableFace);
void exprFuncSetDisableFace(ACMD_EXPR_SELF Entity* e, S32 on)
{
	if(e->pChar)
	{
		e->pChar->bDisableFaceActivate = !!on;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	}
}

// Turns collision on or off for this entity
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetNoColl);
void exprFuncSetNoColl(ACMD_EXPR_SELF Entity* e, S32 on)
{
#if GAMESERVER || GAMECLIENT
	if(on){
		mmNoCollHandleCreateFG(e->mm.movement, &e->mm.mnchExpression, __FILE__, __LINE__);
	}else{
		mmNoCollHandleDestroyFG(&e->mm.mnchExpression);
	}
#endif
}

// Ignores pushy geometry
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetIgnorePushyGeometry);
void exprFuncSetIgnorePushyGeometry(ACMD_EXPR_SELF Entity *e, S32 on)
{
#if GAMESERVER || GAMECLIENT
	mmSetIgnoreActorCreateFG(e->mm.movement, on);
#endif
}

// Returns the geometric average of the passed in entities
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetPos);
void exprFuncGetPos(ACMD_EXPR_LOC_MAT4_OUT matOut, ACMD_EXPR_ENTARRAY_IN entsIn)
{
	int num = eaSize(entsIn);
	if(num)
	{
		Vec3 pos;
		int i;

		for(i = 0; i < num; i++)
		{
			entGetPos((*entsIn)[i], pos);
			addVec3(matOut[3], pos, matOut[3]);
		}

		matOut[3][0] /= num;
		matOut[3][1] /= num;
		matOut[3][2] /= num;
	}
}

// Returns a unique identifier from the entity (i.e. the entRef for now)
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntGetUniqueID);
int exprFuncEntGetUniqueID(ACMD_EXPR_SELF Entity *e)
{
	return entGetRef(e);
}

// Returns the current entity
AUTO_EXPR_FUNC(entity) ACMD_NAME(GetMyEnt);
void exprFuncGetMyEnt(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	eaPush(entsOut, e);

	devassert(eaSize(entsOut) == 1);
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(SetMyEntAsNonCombatantForComplete);
void exprFuncSetMyEntAsNonCombatantForComplete(ACMD_EXPR_SELF Entity* e)
{
	if( e->pCritter ) {
		e->pCritter->encounterData.bNonCombatant = true;
	}
}

// Returns the shared bank entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSharedBankEnt);
SA_RET_OP_VALID Entity *exprFuncEntGetSharedBankEnt(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (SAFE_MEMBER(pEntity, pPlayer))
	{
		return GET_REF(pEntity->pPlayer->hSharedBank);
	}
	else
	{
		return NULL;
	}
}

// Returns true if the passed entity has the specified power mode
AUTO_EXPR_FUNC(ai, entityutil, UIGen) ACMD_NAME(EntHasMode, EntGetHasPowerMode);
bool exprFuncEntHasMode(SA_PARAM_OP_VALID Entity* pEntity, ACMD_EXPR_ENUM(PowerMode) const char *pchModeName)
{
	if (pEntity && pEntity->pChar)
	{
		PowerMode eMode = StaticDefineIntGetInt(PowerModeEnum, pchModeName);
		return character_HasMode(pEntity->pChar, eMode);
	}
	return false;
}

// get the class from the first entity in the entity array
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntArrayGetClass);
const char* exprEntArrayGetClass(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN peaEnts)
{
	S32 i;
	for(i = 0; i < eaSize(peaEnts); ++i)
	{
		EntityRef entRef = entGetRef((*peaEnts)[i]);
		Entity *pEntity = entFromEntityRef(iPartitionIdx, entRef);
		if (pEntity && pEntity->pChar)
		{
			CharacterClass *pClass = GET_REF(pEntity->pChar->hClass);
			if (pClass && pClass->pchName)
			{
				return pClass->pchName;
			}
			else
			{
				return REF_STRING_FROM_HANDLE(pEntity->pChar->hClass);
			}
		}
	}
	return "";
}

// Returns the owner of the given ent
AUTO_EXPR_FUNC(entity) ACMD_NAME(EntGetOwner);
ExprFuncReturnVal exprFuncGetOwner(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT ownerOut, ACMD_EXPR_ENTARRAY_IN entIn, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	Entity* e;
	Entity* owner = NULL;

	if(!eaSize(entIn))
		return ExprFuncReturnFinished;

	if(eaSize(entIn)>1)
	{
		*errString = "Cannot get owner of more than one entity";
		return ExprFuncReturnError;
	}

	e = (*entIn)[0];

	if(e->erOwner)
	{
		owner = entFromEntityRef(iPartitionIdx, e->erOwner);
	}
	else if (!e->myRef && e->pSaved && e->pSaved->conOwner.containerID)
	{
		owner = entFromContainerIDAnyPartition(e->pSaved->conOwner.containerType, e->pSaved->conOwner.containerID);
	}

	if(owner)
		eaPush(ownerOut, owner);

	return ExprFuncReturnFinished;
}

// Returns the entity's owner if the entity is a pet or an empty ent array if the entity is not a pet
AUTO_EXPR_FUNC(entity) ACMD_NAME(GetMyOwner);
void exprFuncGetMyOwner(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	Entity* owner = e->erOwner ? entFromEntityRef(iPartitionIdx, e->erOwner) : NULL;

	if (!owner && !e->myRef && e->pSaved && e->pSaved->conOwner.containerID)
	{
		owner = entFromContainerID(iPartitionIdx, e->pSaved->conOwner.containerType, e->pSaved->conOwner.containerID);
	}

	if(owner && entIsAlive(owner))
		eaPush(entsOut, owner);
}

// Returns the number of lucky charms of a certain type set by the player or player's team
AUTO_EXPR_FUNC(entity) ACMD_NAME(GetLuckyCharmCountOfType);
S32 exprFuncGetLuckyCharmCountOfType(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN entIn, S32 eType)
{
	S32 iCount = 0;
	if (entIn && eaSize(entIn) > 0)
	{
		Entity *pEnt = (*entIn)[0];
		Entity *pTargetEnt;

		// Only players can have lucky charms
		if (pEnt && pEnt->pPlayer)
		{
			if (team_IsMember(pEnt)) //if the player is on a team, use the per-team list
			{
				TeamMapValues* pTeamMapValues = mapState_FindTeamValues(mapState_FromPartitionIdx(iPartitionIdx),pEnt->pTeam->iTeamID);

				if (pTeamMapValues)
				{
					FOR_EACH_IN_EARRAY_FORWARDS(pTeamMapValues->eaPetTargetingInfo, PetTargetingInfo, pPetTargetInfo)
					{
						pTargetEnt = entFromEntityRef(iPartitionIdx, pPetTargetInfo->erTarget);
						if (pTargetEnt && entIsAlive(pTargetEnt) && (eType == -1 || pPetTargetInfo->eType == eType))
						{
							++iCount;
						}
					}
					FOR_EACH_END
				}
			}
			else //if the player is not on team, use the per-player list
			{
				PlayerMapValues* pPlayerMapValues = mapState_FindPlayerValues(iPartitionIdx, entGetContainerID(pEnt));

				if (pPlayerMapValues)
				{
					FOR_EACH_IN_EARRAY_FORWARDS(pPlayerMapValues->eaPetTargetingInfo, PetTargetingInfo, pPetTargetInfo)
					{
						pTargetEnt = entFromEntityRef(iPartitionIdx, pPetTargetInfo->erTarget);
						if (pTargetEnt && entIsAlive(pTargetEnt) && (eType == -1 || pPetTargetInfo->eType == eType))
						{
							++iCount;
						}
					}
					FOR_EACH_END
				}
			}
		}
	}

	return iCount;
}

// Returns the number of lucky charms set by the player or player's team
AUTO_EXPR_FUNC(entity) ACMD_NAME(GetLuckyCharmCount);
S32 exprFuncGetLuckyCharmCount(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN entIn)
{
	return exprFuncGetLuckyCharmCountOfType(iPartitionIdx, entIn, -1);
}

// Returns true if the entity has a preferred target set by a lucky charm
AUTO_EXPR_FUNC(entity) ACMD_NAME(HasPreferredTarget);
bool exprFuncHasPreferredTarget(ACMD_EXPR_ENTARRAY_IN entIn)
{
#ifdef GAMESERVER
	if (entIn && eaSize(entIn) > 0)
	{
		Entity *pEnt = (*entIn)[0];
		return aiCombatRole_RequestPreferredTarget(pEnt);
	}
#endif
	return false;
}



// Returns the entity's owner if the entity is a pet or an empty ent array if the entity is not a pet
// Will return the owner regardless if it is alive or not
AUTO_EXPR_FUNC(entity) ACMD_NAME(GetMyOwnerDeadAll);
void exprFuncGetMyOwnerDeadAll(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	Entity* owner = e->erOwner ? entFromEntityRef(iPartitionIdx, e->erOwner) : NULL;

	if (!owner && !e->myRef && e->pSaved && e->pSaved->conOwner.containerID)
	{
		owner = entFromContainerID(iPartitionIdx, e->pSaved->conOwner.containerType, e->pSaved->conOwner.containerID);
	}

	if(owner)
		eaPush(entsOut, owner);
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerGetOwner);
SA_RET_OP_VALID Entity* exprPlayerGetOwner(ExprContext* pContext, ACMD_EXPR_PARTITION iPartitionIdx)
{
	Entity* e = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	Entity* pOwner = entGetOwner(e);

	if (!pOwner && !e->myRef && e->pSaved && e->pSaved->conOwner.containerID)
	{
		pOwner = entFromContainerID(iPartitionIdx, e->pSaved->conOwner.containerType, e->pSaved->conOwner.containerID);
	}
	if (!pOwner)
	{
		pOwner = e;
	}
	return pOwner;
}

// Returns an entity named "player" from the context, if one exists.
AUTO_EXPR_FUNC(player) ACMD_NAME(GetPlayerEnt);
SA_RET_OP_VALID Entity* exprFuncGetPlayerEnt(ExprContext* context)
{
	Entity* playerEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);

	return playerEnt;
}

// The following functions need to be on the client for editors.  It allows static checking
// to run on them.  The actual call to perform the work is ifdef'd to only happen on the server

// Makes the critter say the message specified by <messageKey> for <duration> seconds
AUTO_EXPR_FUNC(entity) ACMD_NAME(SayMessage);
void exprFuncSayMessage(ACMD_EXPR_SELF Entity* e, ExprContext* context,
				ACMD_EXPR_DICT(message) const char* messageKey, F32 duration)
{
#ifdef GAMESERVER
	const char *pchBubble = entity_GetChatBubbleDefName(e);
	aiSayMessageInternal(e, NULL, context, messageKey, pchBubble, duration);
#endif
}

// <Chance> to makes the critter say the message specified by <messageKey> for <duration> seconds
AUTO_EXPR_FUNC(entity) ACMD_NAME(SayMessageChance);
void exprFuncSayMessageChance(ACMD_EXPR_SELF Entity* e, ExprContext* context,
							  ACMD_EXPR_DICT(message) const char* messageKey, F32 duration, S32 chance)
{
#ifdef GAMESERVER
	if(randomPositiveF32()*100.f < chance)
	{
		const char *pchBubble = entity_GetChatBubbleDefName(e);
		aiSayMessageInternal(e, NULL, context, messageKey, pchBubble, duration);
	}
#endif
}

AUTO_EXPR_FUNC(entity) ACMD_NAME(SayMessageAbout);
ExprFuncReturnVal exprFuncSayMessageAbout(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ENTARRAY_IN ents,
											ACMD_EXPR_DICT(message) const char* messageKey, F32 duration, ACMD_EXPR_ERRSTRING errString)
{
#ifdef GAMESERVER
	const char *pchBubble = entity_GetChatBubbleDefName(e);
	if(!eaSize(ents))
	{
		estrPrintf(errString, "No entities found for SayMessageAbout.  Requires one target.");
		return ExprFuncReturnError;
	}
	if(eaSize(ents)>1)
	{
		estrPrintf(errString, "Too many entities found for SayMessageAbout.  Requires only one target.");
		return ExprFuncReturnError;
	}

	aiSayMessageInternal(e, (*ents)[0], context, messageKey, pchBubble, duration);
#endif
	return ExprFuncReturnFinished;
}

// Makes the critter say the message specified by <messageKey> for <duration> seconds
// Uses chatBubble <chatBubbleName>
AUTO_EXPR_FUNC(entity) ACMD_NAME(SayMessageWithBubble);
void exprFuncSayMessageWithBubble(ACMD_EXPR_SELF Entity* e, ExprContext* context,
						ACMD_EXPR_DICT(message) const char* messageKey, F32 duration,
						const char* chatBubbleName)
{
#ifdef GAMESERVER
	aiSayMessageInternal(e, NULL, context, messageKey, chatBubbleName, duration);
#endif
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSCSayExternMessageVar(ACMD_EXPR_SELF Entity* e, ExprContext* context, 
				const char* category, const char* name, F32 duration,
				ACMD_EXPR_ERRSTRING_STATIC errString)
{
	if(ExprFuncReturnFinished == exprContextExternVarSC(context, category, name, NULL, NULL, MULTI_STRING, "message", true, errString))
		return ExprFuncReturnFinished;

	return ExprFuncReturnError;
}

// Makes the a critter say the message specified by <name> from <category>. This is correctly
// localized for whoever sees it and stays up for <duration> seconds
AUTO_EXPR_FUNC(entity) ACMD_NAME(SayExternMessageVar) ACMD_EXPR_STATIC_CHECK(exprFuncSCSayExternMessageVar);
ExprFuncReturnVal exprFuncSayExternMessageVar(ACMD_EXPR_SELF Entity* e, ExprContext* context,
				const char* category, const char* name, F32 duration,
				ACMD_EXPR_ERRSTRING_STATIC errString)
{
	MultiVal answer = {0};
	ExprFuncReturnVal retval;

	retval = exprContextGetExternVar(context, category, name, MULTI_STRING, &answer, errString);

	if(retval != ExprFuncReturnFinished)
		return retval;

	if(!answer.str[0])
		return ExprFuncReturnFinished;

#ifdef GAMESERVER
	exprFuncSayMessage(e, context, answer.str, duration);
#endif

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSCSayExternMessageVarWithBubble(ACMD_EXPR_SELF Entity* e, ExprContext* context, 
												const char* category, const char* name, F32 duration,
												const char* chatBubbleName,
												ACMD_EXPR_ERRSTRING_STATIC errString)
{
	if(ExprFuncReturnFinished == exprContextExternVarSC(context, category, name, NULL, NULL, MULTI_STRING, "message", true, errString))
		return ExprFuncReturnFinished;

	return ExprFuncReturnError;
}
// Makes the a critter say the message specified by <name> from <category>. This is correctly
// localized for whoever sees it and stays up for <duration> seconds.
// Uses the chat bubble defined by chatBubbleName
AUTO_EXPR_FUNC(entity) ACMD_NAME(SayExternMessageVarWithBubble) ACMD_EXPR_STATIC_CHECK(exprFuncSCSayExternMessageVarWithBubble);
ExprFuncReturnVal exprFuncSayExternMessageVarWithBubble(ACMD_EXPR_SELF Entity* e, ExprContext* context,
											  const char* category, const char* name, F32 duration, const char* chatBubbleName,
											  ACMD_EXPR_ERRSTRING_STATIC errString)
{
	MultiVal answer = {0};
	ExprFuncReturnVal retval;

	retval = exprContextGetExternVar(context, category, name, MULTI_STRING, &answer, errString);

	if(retval != ExprFuncReturnFinished)
		return retval;

	if(!answer.str[0])
		return ExprFuncReturnFinished;

#ifdef GAMESERVER
	exprFuncSayMessageWithBubble(e, context, answer.str, duration, chatBubbleName);
#endif

	return ExprFuncReturnFinished;
}

// Removes all critters that do not have the given tag
AUTO_EXPR_FUNC(entity, ai, OpenMission, encounter_action) ACMD_NAME(EntCropCritterTag);
ExprFuncReturnVal exprFuncEntCropCritterTag(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut,
											const char* critterTag,
											ACMD_EXPR_ERRSTRING errString)
{
	int i;
	int tag = StaticDefineIntGetInt(CritterTagsEnum,critterTag);

	if(tag == -1)
	{
		estrPrintf(errString, "Invalid critter tag %s", critterTag);
		return ExprFuncReturnError;
	}

	for(i = eaSize(entsInOut)-1; i >= 0; i--)
	{
		int hasTag = false;
		Entity* e = (*entsInOut)[i];
		if(e->pCritter)
		{
			CritterDef* def = GET_REF(e->pCritter->critterDef);
			if( def )
			{
				if ( ea32Find(&def->piTags, tag) >= 0 )
				{
					hasTag = true;
				}
			}
		}

		if(!hasTag)
			eaRemoveFast(entsInOut, i);
	}

	return ExprFuncReturnFinished;
}

// Removes all critters that have the given tag
AUTO_EXPR_FUNC(entity, ai, OpenMission, encounter_action) ACMD_NAME(EntCropNotCritterTag);
ExprFuncReturnVal exprFuncEntCropNotCritterTag(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut,
													const char* critterTag,
													ACMD_EXPR_ERRSTRING errString)
{
	int i;
	int tag = StaticDefineIntGetInt(CritterTagsEnum,critterTag);
	if(tag == -1)
	{
		estrPrintf(errString, "Invalid critter tag %s", critterTag);
		return ExprFuncReturnError;
	}

	for(i = eaSize(entsInOut)-1; i >= 0; i--)
	{
		int hasTag = true;
		Entity* e = (*entsInOut)[i];
		if(e->pCritter)
		{
			CritterDef* def = GET_REF(e->pCritter->critterDef);
			if( def )
			{
				if ( ea32Find(&def->piTags, tag) < 0 )
				{
					hasTag = false;
				}
			}
		}

		if (hasTag)
			eaRemoveFast(entsInOut, i);
	}

	return ExprFuncReturnFinished;
}

static int entArrayGetLuckyCharmIndexSortFunction(const PetTargetingInfo **pptr1, const PetTargetingInfo **pptr2)
{
	return (*pptr1)->iIndex - (*pptr2)->iIndex;
}

// Returns the lucky charm index for the entity. 
// If there is more than one entity or the entity does not have the given lucky charm, the function returns -1
AUTO_EXPR_FUNC(ai, OpenMission) ACMD_NAME("EntArrayGetLuckyCharmWeightedIndex");
S32 exprEntArrayGetLuckyCharmWeightedIndex(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_ENTARRAY_IN pppOwner, int eType)
{
	S32 iEntCount = eaSize(peaEnts);

	if (iEntCount == 1 && eaSize(pppOwner) == 1)
	{
		S32 i;

		// Get the owner
		Entity *pOwner = (*pppOwner)[0];

		EntityRef iTargetEntRef = entGetRef((*peaEnts)[0]);
		
		PetTargetingInfo** eaTempLuckyCharms = NULL;

		bool bTargetEntFound = false;

		if ( team_IsMember(pOwner) ) //if the player is on a team, search the per-team list
		{
			TeamMapValues* pTeamMapValues = mapState_FindTeamValues(mapState_FromPartitionIdx(iPartitionIdx),pOwner->pTeam->iTeamID);

			if ( pTeamMapValues )
			{
				for ( i = 0; i < eaSize(&pTeamMapValues->eaPetTargetingInfo); i++ )
				{
					Entity* pEnt = entFromEntityRef(iPartitionIdx, pTeamMapValues->eaPetTargetingInfo[i]->erTarget);

					if (pEnt && entIsAlive(pEnt) && pTeamMapValues->eaPetTargetingInfo[i]->eType == eType)
					{
						eaPush(&eaTempLuckyCharms, pTeamMapValues->eaPetTargetingInfo[i]);
						if (entGetRef(pEnt) == iTargetEntRef)
						{
							bTargetEntFound = true;
						}
					}
				}
			}
		}
		else //if the player is not on team, search the per-player list
		{
			PlayerMapValues* pPlayerMapValues = mapState_FindPlayerValues(iPartitionIdx, entGetContainerID(pOwner));

			if ( pPlayerMapValues )
			{
				for ( i = 0; i < eaSize(&pPlayerMapValues->eaPetTargetingInfo); i++ )
				{
					Entity* pEnt = entFromEntityRef(iPartitionIdx, pPlayerMapValues->eaPetTargetingInfo[i]->erTarget);

					if (pEnt && entIsAlive(pEnt) && pPlayerMapValues->eaPetTargetingInfo[i]->eType == eType)
					{
						eaPush(&eaTempLuckyCharms, pPlayerMapValues->eaPetTargetingInfo[i]);
						if (entGetRef(pEnt) == iTargetEntRef)
						{
							bTargetEntFound = true;
						}
					}
				}
			}
		}

		if (eaSize(&eaTempLuckyCharms) > 0)
		{
			if (bTargetEntFound)
			{
				S32 iCount = eaSize(&eaTempLuckyCharms);
				// Sort the array
				eaQSort(eaTempLuckyCharms, entArrayGetLuckyCharmIndexSortFunction);

				for (i = 0; i < iCount; i++)
				{
					if (eaTempLuckyCharms[i]->erTarget == iTargetEntRef)
					{
						eaDestroy(&eaTempLuckyCharms);
						return iCount - i;
					}
				}				
			}
			eaDestroy(&eaTempLuckyCharms);
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(ai, OpenMission) ACMD_NAME(EntCropHasNoLuckyCharm);
ExprFuncReturnVal exprFuncEntCropHasNoLuckyCharm(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_ENTARRAY_IN pppOwner)
{
	S32 i, iRemoveIndex;
	Entity* pOwner = NULL;

	if (!eaSize(pppOwner))
	{
		return ExprFuncReturnFinished; 
	}
	pOwner = (*pppOwner)[0];
	if ( team_IsMember(pOwner) ) //if the player is on a team, search the per-team list
	{
		TeamMapValues* pTeamMapValues = mapState_FindTeamValues(mapState_FromPartitionIdx(iPartitionIdx),pOwner->pTeam->iTeamID);

		if ( pTeamMapValues )
		{
			for ( i = 0; i < eaSize(&pTeamMapValues->eaPetTargetingInfo); i++ )
			{
				Entity* pMatchingEnt = entFromEntityRef(iPartitionIdx, pTeamMapValues->eaPetTargetingInfo[i]->erTarget);
				if ((iRemoveIndex = eaFind(entsInOut, pMatchingEnt)) > -1)
					eaRemoveFast(entsInOut, iRemoveIndex);
			}
		}
	}
	else //if the player is not on team, search the per-player list
	{
		PlayerMapValues* pPlayerMapValues = mapState_FindPlayerValues(iPartitionIdx, entGetContainerID(pOwner));

		if ( pPlayerMapValues )
		{
			for ( i = 0; i < eaSize(&pPlayerMapValues->eaPetTargetingInfo); i++ )
			{
				Entity* pMatchingEnt = entFromEntityRef(iPartitionIdx, pPlayerMapValues->eaPetTargetingInfo[i]->erTarget);
				if ((iRemoveIndex = eaFind(entsInOut, pMatchingEnt)) > -1)
					eaRemoveFast(entsInOut, iRemoveIndex);
			}
		}
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai, OpenMission) ACMD_NAME(EntCropExcludeLuckyCharm);
ExprFuncReturnVal exprFuncEntCropExcludeLuckyCharm(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_ENTARRAY_IN pppOwner, int eType)
{
	S32 i, iRemoveIndex;
	Entity* pOwner = NULL;

	if (!eaSize(pppOwner))
	{
		return ExprFuncReturnFinished; 
	}
	pOwner = (*pppOwner)[0];
	if ( team_IsMember(pOwner) ) //if the player is on a team, search the per-team list
	{
		TeamMapValues* pTeamMapValues = mapState_FindTeamValues(mapState_FromPartitionIdx(iPartitionIdx),pOwner->pTeam->iTeamID);

		if ( pTeamMapValues )
		{
			for ( i = 0; i < eaSize(&pTeamMapValues->eaPetTargetingInfo); i++ )
			{
				if (pTeamMapValues->eaPetTargetingInfo[i]->eType == eType)
				{
					Entity* pMatchingEnt = entFromEntityRef(iPartitionIdx, pTeamMapValues->eaPetTargetingInfo[i]->erTarget);
					if ((iRemoveIndex = eaFind(entsInOut, pMatchingEnt)) > -1)
						eaRemoveFast(entsInOut, iRemoveIndex);
				}
			}
		}
	}
	else //if the player is not on team, search the per-player list
	{
		PlayerMapValues* pPlayerMapValues = mapState_FindPlayerValues(iPartitionIdx, entGetContainerID(pOwner));

		if ( pPlayerMapValues )
		{
			for ( i = 0; i < eaSize(&pPlayerMapValues->eaPetTargetingInfo); i++ )
			{
				if (pPlayerMapValues->eaPetTargetingInfo[i]->eType == eType)
				{
					Entity* pMatchingEnt = entFromEntityRef(iPartitionIdx, pPlayerMapValues->eaPetTargetingInfo[i]->erTarget);
					if ((iRemoveIndex = eaFind(entsInOut, pMatchingEnt)) > -1)
						eaRemoveFast(entsInOut, iRemoveIndex);
				}
			}
		}
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai, OpenMission) ACMD_NAME(EntCropLuckyCharm);
ExprFuncReturnVal exprFuncEntCropLuckyCharm(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_ENTARRAY_IN pppOwner,
											int eType)
{
	S32 i;
	Entity* pOwner = NULL;
	Entity** eaMatchingEnts = NULL;
	if (!eaSize(pppOwner))
	{
		//no owner means there's no lucky charms to check, so clear the ent array
		eaClear(entsInOut);
		return ExprFuncReturnFinished; 
	}
	pOwner = (*pppOwner)[0];
	if ( team_IsMember(pOwner) ) //if the player is on a team, search the per-team list
	{
		TeamMapValues* pTeamMapValues = mapState_FindTeamValues(mapState_FromPartitionIdx(iPartitionIdx),pOwner->pTeam->iTeamID);

		if ( pTeamMapValues )
		{
			for ( i = 0; i < eaSize(&pTeamMapValues->eaPetTargetingInfo); i++ )
			{
				if ((pTeamMapValues->eaPetTargetingInfo[i]->eType == eType))
				{
					Entity* pMatchingEnt = entFromEntityRef(iPartitionIdx, pTeamMapValues->eaPetTargetingInfo[i]->erTarget);
					if (eaFind(entsInOut, pMatchingEnt) > -1)
						eaPush(&eaMatchingEnts, pMatchingEnt);
				}
			}
		}
	}
	else //if the player is not on team, search the per-player list
	{
		PlayerMapValues* pPlayerMapValues = mapState_FindPlayerValues(iPartitionIdx, entGetContainerID(pOwner));

		if ( pPlayerMapValues )
		{
			for ( i = 0; i < eaSize(&pPlayerMapValues->eaPetTargetingInfo); i++ )
			{
				if ((pPlayerMapValues->eaPetTargetingInfo[i]->eType == eType))
				{
					Entity* pMatchingEnt = entFromEntityRef(iPartitionIdx, pPlayerMapValues->eaPetTargetingInfo[i]->erTarget);
					if (eaFind(entsInOut, pMatchingEnt) > -1)
						eaPush(&eaMatchingEnts, pMatchingEnt);
				}
			}
		}
	}
	eaClear(entsInOut);
	if (eaSize(&eaMatchingEnts) > 0)
		eaPushEArray(entsInOut, &eaMatchingEnts);
	eaDestroy(&eaMatchingEnts);
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(EntCropLuckyCharmHighestPriority);
ExprFuncReturnVal exprFuncEntCropLuckyCharmHighestPriority(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_ENTARRAY_IN pppOwner,
											int eType)
{
	S32 i;
	S32 iMostImportant = -1;
	Entity* pOwner = NULL;
	int refToKeep = -1;
	if (!eaSize(pppOwner))
	{
		//no owner means there's no lucky charms to check, so clear the ent array
		eaClear(entsInOut);
		return ExprFuncReturnFinished; 
	}
	pOwner = (*pppOwner)[0];
	if ( team_IsMember(pOwner) ) //if the player is on a team, search the per-team list
	{
		TeamMapValues* pTeamMapValues = mapState_FindTeamValues(mapState_FromPartitionIdx(iPartitionIdx),pOwner->pTeam->iTeamID);

		if ( pTeamMapValues )
		{
			for ( i = 0; i < eaSize(&pTeamMapValues->eaPetTargetingInfo); i++ )
			{
				if ((pTeamMapValues->eaPetTargetingInfo[i]->eType == eType))
				{
					if (iMostImportant == -1 || pTeamMapValues->eaPetTargetingInfo[i]->iIndex < pTeamMapValues->eaPetTargetingInfo[iMostImportant]->iIndex)
					{
						iMostImportant = i;
						refToKeep = pTeamMapValues->eaPetTargetingInfo[i]->erTarget;
					}
				}
			}
		}
	}
	else //if the player is not on team, search the per-player list
	{
		PlayerMapValues* pPlayerMapValues = mapState_FindPlayerValues(iPartitionIdx, entGetContainerID(pOwner));

		if ( pPlayerMapValues )
		{
			for ( i = 0; i < eaSize(&pPlayerMapValues->eaPetTargetingInfo); i++ )
			{
				if ((pPlayerMapValues->eaPetTargetingInfo[i]->eType == eType))
				{
					if (iMostImportant == -1 || pPlayerMapValues->eaPetTargetingInfo[i]->iIndex < pPlayerMapValues->eaPetTargetingInfo[iMostImportant]->iIndex)
					{
						iMostImportant = i;
						refToKeep = pPlayerMapValues->eaPetTargetingInfo[i]->erTarget;
					}
				}
			}
		}
	}

	eaClear(entsInOut);
	if (refToKeep > -1 && entFromEntityRef(iPartitionIdx, refToKeep))
		eaPush(entsInOut, entFromEntityRef(iPartitionIdx, refToKeep));

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai, OpenMission) ACMD_NAME(EntCropTargettingEnt);
ExprFuncReturnVal exprFuncEntCropTargettingEnt(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_ENTARRAY_IN pppTarget)
{
	S32 i;
	Entity* pTarget = NULL;
	if (eaSize(pppTarget))
		pTarget = (*pppTarget)[0];
	if(pTarget)
	{
		EntityRef er = entGetRef(pTarget);
		for(i = eaSize(entsInOut)-1; i >= 0; i--)
		{
			Entity* e = (*entsInOut)[i];
			if (entity_GetTargetRef(e) != er)
				eaRemoveFast(entsInOut, i);
		}
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropHasItemEquipped);
ExprFuncReturnVal exprFuncEntCropHasItemEquipped(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_RES_DICT(ItemDef) const char *pcItemName)
{
	int i, j;
	bool hasItem;
	for(i = eaSize(entsInOut)-1; i >= 0; i--)
	{
		Entity* e = (*entsInOut)[i];
		hasItem = false;

		if (!e->pInventoryV2) {
			continue;
		}

		for (j = eaSize(&e->pInventoryV2->ppInventoryBags)-1; j >= 0; j--) {
			if (invbag_flags(e->pInventoryV2->ppInventoryBags[j]) & (InvBagFlag_EquipBag | InvBagFlag_WeaponBag | InvBagFlag_ActiveWeaponBag | InvBagFlag_DeviceBag)) {
				if (inv_bag_GetItemByName(e->pInventoryV2->ppInventoryBags[j], pcItemName) != NULL) {
					hasItem = true;
					break;
				}
			}
		}

		if (!hasItem)
			eaRemoveFast(entsInOut, i);
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropBagHasSlotOccupied);
ExprFuncReturnVal exprFuncEntCropBagHasSlotOccupied(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_ENUM(InvBagIDs) const char *pcBagName, int iSlot)
{
	int i;
	InvBagIDs eID = StaticDefineIntGetInt(InvBagIDsEnum, pcBagName);
	for(i = eaSize(entsInOut)-1; i >= 0; i--)
	{
		Entity* e = (*entsInOut)[i];

		if (inv_ent_GetSlotItemCount(e, eID, iSlot, NULL) <= 0)
			eaRemoveFast(entsInOut, i);
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(EntCropAffectedByPowerWithTag);
ExprFuncReturnVal exprFuncEntCropAffectedByPowerWithTag(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char *pszTag)
{
	S32 eTag = StaticDefineIntGetInt(PowerTagsEnum,pszTag);

	int iEnt;

	if (eTag == -1)
		return ExprFuncReturnError;

	for(iEnt=eaSize(entsInOut)-1; iEnt>=0; iEnt--)
	{
		Entity* pEntity = (*entsInOut)[iEnt];

		int iMod;

		Character * pChar = pEntity->pChar;
			
		bool bTagFound = false;

		if (pChar)
		{
			for(iMod=eaSize(&pChar->modArray.ppMods)-1; iMod>=0; iMod--)
			{
				AttribMod * pmod = pChar->modArray.ppMods[iMod];
				AttribModDef *pmoddef = mod_GetDef(pmod);
				if(pmoddef && !pmoddef->bDerivedInternally && eaiFind(&pmoddef->tags.piTags,eTag) != -1)
				{
					if( pmod->pFragility == NULL || (pmod->pFragility->fHealth != 0.f))
					{
						bTagFound = true;
						break;
					}
				}
			}
		}

		if (!bTagFound)
		{
			eaRemoveFast(entsInOut, iEnt);
		}
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(EntCropNotAffectedByPowerWithTag);
ExprFuncReturnVal exprFuncEntCropNotAffectedByPowerWithTag(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char *pszTag)
{
	S32 eTag = StaticDefineIntGetInt(PowerTagsEnum, pszTag);

	int iEnt;

	if (eTag == -1)
		return ExprFuncReturnError;

	for(iEnt = eaSize(entsInOut) - 1; iEnt >= 0; iEnt--)
	{
		Entity* pEntity = (*entsInOut)[iEnt];

		int iMod;

		Character * pChar = pEntity->pChar;
			
		bool bTagFound = false;

		if (pChar)
		{
			for(iMod = eaSize(&pChar->modArray.ppMods) - 1; iMod >= 0; iMod--)
			{
				AttribMod * pmod = pChar->modArray.ppMods[iMod];
				AttribModDef *pmoddef = mod_GetDef(pmod);
				if(pmoddef && !pmoddef->bDerivedInternally && eaiFind(&pmoddef->tags.piTags, eTag) != -1)
				{
					if( pmod->pFragility == NULL || (pmod->pFragility->fHealth != 0.f))
					{
						bTagFound = true;
						break;
					}
				}
			}
		}

		if (bTagFound)
		{
			eaRemoveFast(entsInOut, iEnt);
		}
	}

	return ExprFuncReturnFinished;
}

//NNO-specific function that crops ents based on a skillcheck.
AUTO_EXPR_FUNC(player, ai, OpenMission) ACMD_NAME(EntCropDDTake10); 
void exprFuncEntCropDDTake10(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char *skillName, S32 DC)
{
	int i;
	int num = eaSize(entsInOut);

	for(i = 0; i < num; i++)
	{
		if(SAFE_MEMBER2((*entsInOut)[i], pChar, pattrBasic))
		{
			S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,skillName);
			if(eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
			{
				if (*F32PTR_OF_ATTRIB((*entsInOut)[i]->pChar->pattrBasic,eAttrib)  + 10 < DC)
					eaRemoveFast(entsInOut, i);
			}
		}
	}
}

// Removes all entities that were not created by the given entity
AUTO_EXPR_FUNC(ai) ACMD_NAME(EntCropCreatedByEnt);
void exprFuncEntCropCreatedByEnt(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, SA_PARAM_NN_VALID Entity *e)
{
	int i;
	int num = eaSize(entsInOut);

	if (!e)
		return;

	for(i = 0; i < num; i++)
	{
		if((*entsInOut)[i]->erCreator != entGetRef(e))
		{
			eaRemoveFast(entsInOut, i);
		}
	}
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(EntCropHasActiveItemAssignment);
void exprFuncEntCropHasActiveItemAssignment(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char* pchDefName)
{
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchDefName);
	int i;
	int num = eaSize(entsInOut);

	if (!pDef)
		return;

	for(i = 0; i < num; i++)
	{
		if(!ItemAssignment_EntityGetActiveAssignmentByDef((*entsInOut)[i], pDef))
		{
			eaRemoveFast(entsInOut, i);
		}
	}
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(EntCropHasRecentlyCompletedItemAssignment);
void exprFuncEntCropHasRecentlyCompletedItemAssignment(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, const char* pchDefName, const char* pchOutcome)
{
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchDefName);
	int i;
	int num = eaSize(entsInOut);

	if (!pDef)
		return;

	for(i = 0; i < num; i++)
	{
		if(!ItemAssignments_PlayerGetRecentlyCompletedAssignment((*entsInOut)[i], pDef, pchOutcome))
		{
			eaRemoveFast(entsInOut, i);
		}
	}
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerHasActiveItemAssignment);
bool exprPlayerHasActiveItemAssignment(ExprContext* pContext, const char* pchDefName)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	ItemAssignmentPersistedData* pPersistedData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchDefName);
	
	if (pDef && pPersistedData)
	{
		return !!ItemAssignment_EntityGetActiveAssignmentByDef(pEnt, pDef);
	}
	return false;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerHasRecentlyCompletedAssignment);
bool exprPlayerHasRecentlyCompletedItemAssignment(ExprContext* pContext, const char* pchDefName, const char* pchOutcome)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	ItemAssignmentPersistedData* pPersistedData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchDefName);
	if (pDef && pPersistedData)
	{
		return !!ItemAssignments_PlayerGetRecentlyCompletedAssignment(pEnt, pDef, pchOutcome);
	}
	return false;
}


// This only works on assignments with dependencies or assignments in cooldown
AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerHasCompletedAssignment);
bool exprPlayerHasCompletedItemAssignment(ExprContext* pContext, const char* pchDefName)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	ItemAssignmentPersistedData* pPersistedData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchDefName);
	if (pDef && pPersistedData)
	{
		return !!ItemAssignments_PlayerGetCompletedAssignment(pEnt, pDef);
	}
	return false;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerGetAssignmentCooldownRemaining);
U32 exprPlayerGetAssignmentCooldownRemaining(ExprContext* pContext, const char* pchDefName)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	ItemAssignmentPersistedData* pPersistedData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchDefName);
	if (pDef && pDef->uCooldownAfterCompletion && pPersistedData)
	{
		ItemAssignmentCompleted* pCompleted = ItemAssignments_PlayerGetCompletedAssignment(pEnt, pDef);
		if (pCompleted)
		{
			U32 uCurrentTime = timeSecondsSince2000();
			U32 uFinishCooldown = pCompleted->uCompleteTime + pDef->uCooldownAfterCompletion;
			if (uFinishCooldown > uCurrentTime)
			{
				return uFinishCooldown - uCurrentTime;
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerHasGrantedAssignment);
bool exprPlayerHasGrantedAssignment(ExprContext* pContext, const char* pchDefName)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	ItemAssignmentPlayerData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentData);
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchDefName);
	if (pDef && pPlayerData)
	{
		if (ItemAssignments_PlayerFindGrantedAssignment(pEnt, pDef) >= 0)
		{
			return true;
		}
	}
	return false;
}

// Either pchSpecies or pchCategories must be valid
// If pchSpecies is null or empty, it is ignored
// If pchCategories is null or empty, it is ignored
AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerHasItemWithSpeciesAndCategories);
bool exprPlayerHasItemWithSpeciesAndCategories(ExprContext* pContext,
											   const char* pchBag, 
											   const char* pchSpeciesList, 
											   const char* pchItemCategories,
											   const char* pchItemQualities)
{
	bool bFound = false;
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pEnt && pEnt->pInventoryV2)
	{
		InvBagIDs eBagID = StaticDefineIntGetInt(InvBagIDsEnum, pchBag);
		const char* pchBlameFile = exprContextGetBlameFile(pContext);

		if (eBagID >= InvBagIDs_None)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);
			
			if (pBag)
			{
				char* estrError = NULL;
				const char** ppchSpecies = NULL;
				int* peCategories = NULL;
				int* peQualities = NULL;
				int i;

				if (pchSpeciesList && pchSpeciesList[0])
				{
					char* pchContext;
					char* pchStart;
					char* pchCopy;
					strdup_alloca(pchCopy, pchSpeciesList);
					pchStart = strtok_r(pchCopy, " ,\t\r\n", &pchContext);
					do
					{
						if (pchStart)
						{
							SpeciesDef* pSpeciesDef = RefSystem_ReferentFromString("SpeciesDef", pchStart);
							if (pSpeciesDef)
							{
								eaPush(&ppchSpecies, pSpeciesDef->pcName);
							}
							else
							{
								ErrorFilenamef(pchBlameFile, "SpeciesDef %s not found", pchStart);
							}
						}
					} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
				}

				estrStackCreate(&estrError);
				StaticDefineIntGetIntsFromKeyList(ItemCategoryEnum, pchItemCategories, &peCategories, &estrError);
				if (estrError && estrError[0])
				{
					ErrorFilenamef(pchBlameFile, "%s", estrError);
				}
				estrClear(&estrError);
				StaticDefineIntGetIntsFromKeyList(ItemQualityEnum, pchItemQualities, &peQualities, &estrError);
				if (estrError && estrError[0])
				{
					ErrorFilenamef(pchBlameFile, "%s", estrError);
				}
				estrDestroy(&estrError);

				for (i = eaSize(&pBag->ppIndexedInventorySlots)-1; i >= 0; i--)
				{
					InventorySlot* pSlot = pBag->ppIndexedInventorySlots[i];
					ItemDef* pItemDef = pSlot->pItem ? GET_REF(pSlot->pItem->hItem) : NULL;
					if (pItemDef)
					{
						if (pchSpeciesList && pchSpeciesList[0])
						{
							const char* pchItemSpeciesDef = REF_STRING_FROM_HANDLE(pItemDef->hSpecies);
							if (!pchItemSpeciesDef || eaFind(&ppchSpecies, pchItemSpeciesDef) < 0)
							{
								continue;
							}
						}
						if (pchItemCategories && pchItemCategories[0])
						{
							if (!itemdef_HasAllItemCategories(pItemDef, (ItemCategory*)peCategories))
							{
								continue;
							}
						}
						if (pchItemQualities && pchItemQualities[0])
						{
							if(eaiFind(&peQualities, item_GetQuality(pSlot->pItem)) < 0)
							{
								continue;
							}
						}
						bFound = true;
						break;
					}
				}
				eaiDestroy(&peCategories);
				eaiDestroy(&peQualities);
				eaDestroy(&ppchSpecies);
			}
		}
		else
		{
			ErrorFilenamef(pchBlameFile, "InventoryBag %s not recognized", pchBag);
		}
	}
	return bFound;
}

AUTO_EXPR_FUNC(Player) ACMD_NAME("IsPlayerInRegion");
bool exprIsPlayerInRegion(ExprContext *pContext, const char* pchRegionType)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);

	if ( pPlayerEnt )
	{
		return stricmp( StaticDefineIntRevLookup(WorldRegionTypeEnum,entGetWorldRegionTypeOfEnt(pPlayerEnt)), pchRegionType ) == 0;
	}

	return false;
}

// Get the gender as a string
AUTO_EXPR_FUNC(entityutil, Player) ACMD_NAME(EntGetGender);
const char *gclExprEntGetGender(SA_PARAM_OP_VALID Entity *pEnt)
{
	Gender eGender = SAFE_MEMBER(pEnt, eGender);
	if (eGender == Gender_Unknown && SAFE_MEMBER(pEnt, pChar))
	{
		SpeciesDef *pSpecies = GET_REF(pEnt->pChar->hSpecies);
		if (pSpecies)
			eGender = pSpecies->eGender;
	}
	return StaticDefineIntRevLookupNonNull(GenderEnum, eGender);
}

AUTO_EXPR_FUNC(Entity, UIGen) ACMD_NAME(EntGetFaction);
const char *exprGetFaction(SA_PARAM_OP_VALID Entity *pEnt)
{
	CritterFaction *f = pEnt ? entGetFaction(pEnt) : NULL;
	if (!f) return "";
	return f->pchName;
}

AUTO_EXPR_FUNC(Entity, UIGen) ACMD_NAME(EntGetSubFaction);
const char *exprGetSubFaction(SA_PARAM_OP_VALID Entity *pEnt)
{
	CritterFaction *f = pEnt ? entGetSubFaction(pEnt) : NULL;
	if (!f) return "";
	return f->pchName;
}

AUTO_EXPR_FUNC(Player, Entity) ACMD_NAME(GetAllegiance);
SA_RET_OP_VALID const AllegianceDef *allegiance_GetAllegiance(ExprContext* context, SA_PARAM_NN_VALID Entity* pEnt)
{
	return GET_REF(pEnt->hAllegiance);
}

AUTO_EXPR_FUNC(Player, Entity) ACMD_NAME(GetSubAllegiance);
SA_RET_OP_VALID const AllegianceDef *allegiance_GetSubAllegiance(ExprContext* context, SA_PARAM_NN_VALID Entity* pEnt)
{
	return GET_REF(pEnt->hSubAllegiance);
}

AUTO_EXPR_FUNC(ai, Emote) ACMD_NAME(GetMyAllegiance);
const char* allegiance_GetMyAllegiance(ACMD_EXPR_SELF Entity *pEnt)
{
	if (pEnt)
	{
		return REF_STRING_FROM_HANDLE(pEnt->hAllegiance);
	}
	return NULL;
}

AUTO_EXPR_FUNC(ai, Emote) ACMD_NAME(GetMySubAllegiance);
const char* allegiance_GetMySubAllegiance(ACMD_EXPR_SELF Entity *pEnt)
{
	if (pEnt)
	{
		return REF_STRING_FROM_HANDLE(pEnt->hSubAllegiance);
	}
	return NULL;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
bool allegiance_IsAllegiance_LoadVerify(ExprContext* context, SA_PARAM_OP_VALID Entity* pEnt, const char *pchAllegiance)
{
#if GAMESERVER
    MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(context, g_MissionDefVarName);
    if (pMissionDef) {
        GameEvent *pEvent = StructCreate(parse_GameEvent);
        char *estrBuffer = NULL;

        estrPrintf(&estrBuffer, "IsAllegiance_%s", pchAllegiance);
        pEvent->type = EventType_AllegianceSet;
        pEvent->pchEventName = allocAddString(estrBuffer);
        pEvent->pchAllegianceName = allocAddString(pchAllegiance);
        pEvent->tMatchSource = TriState_Yes;

        eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);

        estrDestroy(&estrBuffer);
    }
#endif

    return true;
}

AUTO_EXPR_FUNC(Player, Entity) ACMD_NAME(IsAllegiance) ACMD_EXPR_STATIC_CHECK(allegiance_IsAllegiance_LoadVerify);
bool allegiance_IsAllegiance(ExprContext* context, SA_PARAM_OP_VALID Entity* pEnt, const char *pchAllegiance)
{
	AllegianceDef *a = pEnt ? GET_REF(pEnt->hAllegiance) : NULL;
	AllegianceDef *sa = pEnt ? GET_REF(pEnt->hSubAllegiance) : NULL;
	if (a && pchAllegiance)
	{
		if (!stricmp(a->pcName,pchAllegiance))
		{
			return true;
		}
	}
	if (sa && pchAllegiance)
	{
		if (!stricmp(sa->pcName,pchAllegiance))
		{
			return true;
		}
	}
	return false;
}



AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(IsEntAllegiance);
bool allegiance_IsEntAllegiance(ExprContext* context, const char *pchAllegiance)
{
	Entity *pEnt = exprContextGetVarPointerUnsafe(context, "Source");
	AllegianceDef *a;
	AllegianceDef *sa;

	if (!pEnt) pEnt = exprContextGetSelfPtr(context);
	a = pEnt ? GET_REF(pEnt->hAllegiance) : NULL;
	sa = pEnt ? GET_REF(pEnt->hSubAllegiance) : NULL;

	if (a && pchAllegiance)
	{
		if (!stricmp(a->pcName,pchAllegiance))
		{
			return true;
		}
	}
	if (sa && pchAllegiance)
	{
		if (!stricmp(sa->pcName,pchAllegiance))
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(IsPlayerAllegiance);
bool allegiance_IsPlayerAllegiance(ExprContext* context, const char *pchAllegiance)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	AllegianceDef *a = pEnt ? GET_REF(pEnt->hAllegiance) : NULL;
	AllegianceDef *sa = pEnt ? GET_REF(pEnt->hSubAllegiance) : NULL;
	if (a && pchAllegiance)
	{
		if (!stricmp(a->pcName,pchAllegiance))
		{
			return true;
		}
	}
	if (sa && pchAllegiance)
	{
		if (!stricmp(sa->pcName,pchAllegiance))
		{
			return true;
		}
	}
	return false;
}

// Check the player's guild's allegiance
AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(IsPlayerGuildAllegiance);
bool allegiance_IsPlayerGuildAllegiance(ExprContext* context, const char *pchAllegiance)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);

	if (pEnt!=NULL)
	{
		Guild *pGuild = guild_GetGuild(pEnt);
		if (pGuild!=NULL)
		{
			if (stricmp(pGuild->pcAllegiance, pchAllegiance)==0)
			{
				return(true);
			}
		}
	}
	return(false);
}


// Check guild permission on the player
AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(PlayerHasGuildPermission);
bool exprFuncPlayerHasGuildPermission(ExprContext* context, const char* pcPermission)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);

	if (pEnt!=NULL)
	{
		Guild *pGuild = guild_GetGuild(pEnt);
		if (pGuild!=NULL)
		{
			GuildMember *pGuildMember = guild_FindMember(pEnt);
			if (pGuildMember!=NULL)
			{
				int iRank = pGuildMember->iRank;
				GuildRankPermissions ePerm = StaticDefineIntGetInt(GuildRankPermissionsEnum, pcPermission);
				return guild_HasPermission(iRank, pGuild, ePerm);
			}
		}
	}
	return false;
}


AUTO_EXPR_FUNC(Player) ACMD_NAME(IsPlayerCharPath);
bool exprFuncIsPlayerCharPath(ExprContext* context, ACMD_EXPR_RES_DICT(CharacterPath) const char *pchCharPath)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	return entity_HasCharacterPath(pEnt, pchCharPath);
}

AUTO_EXPR_FUNC(Player) ACMD_NAME(PlayerHasCharPath);
bool exprFuncPlayerHasCharPath(ExprContext* context)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	return entity_HasAnyCharacterPath(pEnt);
}

AUTO_EXPR_FUNC(Player, Mission, UIGen) ACMD_NAME(PlayerHasMission);
int exprFuncPlayerHasMission(ExprContext *pContext, const char* pchMissionName)
{
	int bHasMission = false;
	MissionDef* pTargetDef = missiondef_DefFromRefString(pchMissionName);
	Entity* pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pPlayerEnt && pTargetDef)
	{
		MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		if (pMissionInfo && mission_GetMissionFromDef(pMissionInfo, pTargetDef))
			bHasMission = true;
	}
	return bHasMission;
}

AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(PlayerHasCompletedMission);
int exprFuncPlayerHasCompletedMission(ExprContext* pContext, const char* pchMissionName)
{
	int bHasCompleted = false;
	MissionDef* pTargetDef = missiondef_DefFromRefString(pchMissionName);
	Entity* pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pPlayerEnt && pTargetDef)
	{
		MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		if (pMissionInfo && mission_GetCompletedMissionByDef(pMissionInfo, pTargetDef))
			bHasCompleted = true;
	}
	return bHasCompleted;
}

// return the number of seconds left in the current cooldown block for this mission
AUTO_EXPR_FUNC(gameutil, UIGen) ACMD_NAME(PlayerMissionCooldownLeftSeconds);
U32 exprFuncPlayerMissionCooldownLeftSeconds(ExprContext *pContext, const char *pMissionName)
{
	Entity *pPlayer = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if(pPlayer && pPlayer->pChar)
	{
		const MissionCooldownInfo *missionInfo = mission_GetCooldownInfo(pPlayer, pMissionName);
		
		if(missionInfo->bIsInCooldownBlock)
		{
			return missionInfo->uCooldownSecondsLeft;
		}
	}
	
	return 0;
}

// Returns the value of the Entity's Character's attribute.  Returns 0 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetAttrib");
F32 exprEntGetAttrib(SA_PARAM_OP_VALID Entity *pEntity,
					 ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	F32 r = 0;
	if (pEntity && pEntity->pChar && pEntity->pChar->pattrBasic)
	{
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		if(eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
		{
			r = *F32PTR_OF_ATTRIB(pEntity->pChar->pattrBasic,eAttrib);
		}
	}
	return r;
}

// Entity
//  Inputs: Entity
//  Return: Estimated time to complete the current cast, or -1 if invalid
AUTO_EXPR_FUNC(Entity, UIGen);
F32 EntGetRemainingChargeTime(SA_PARAM_OP_VALID Entity *pEnt)
{
#if !GAMESERVER && !GAMECLIENT
	return 0;
#else
	if (pEnt && pEnt->pChar && pEnt->pChar->pChargeData)
	{
		return ((F32)(pmTimestamp(0) - pEnt->pChar->pChargeData->uiTimestamp))/(MM_PROCESS_COUNTS_PER_SECOND);
	}
	return -1;
#endif
}

AUTO_EXPR_FUNC(Entity, UIGen);
F32 EntGetTotalChargeTime(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt && pEnt->pChar && pEnt->pChar->pChargeData)
	{
		return pEnt->pChar->pChargeData->fTimeCharge;
	}
	return -1;
}

// Entity
//  Inputs: Entity
//  Return: Name of the current charging power, or "" if invalid.
AUTO_EXPR_FUNC(Entity, UIGen);
const char* EntGetChargingPowerName(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt && pEnt->pChar && pEnt->pChar->pChargeData && (pEnt->pChar->pChargeData->fTimeCharge > 0.0))
	{
		return langTranslateMessage(locGetLanguage(getCurrentLocale()), GET_REF(pEnt->pChar->pChargeData->hMsgName));
	}
	return "";
}

// Check if the player is currently working on a mission.
AUTO_EXPR_FUNC(Entity, Reward, Emote) ACMD_NAME(MissionInProgress);
bool emoteExprMissionInProgress(ExprContext *pContext, ACMD_EXPR_DICT(Mission) const char *pchMission)
{
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	
	if (pEnt && pEnt->pPlayer)
	{
		Mission *pMission = mission_FindMissionFromRefString(pEnt->pPlayer->missionInfo, pchMission);
		return (pMission && pMission->state == MissionState_InProgress);
	}
	return false;
}

// Return the number of times the player has succeeded/completed this mission.
AUTO_EXPR_FUNC(Entity, Reward, Emote) ACMD_NAME(MissionCompleted);
S32 emoteExprMissionCompleted(ExprContext *pContext, ACMD_EXPR_DICT(Mission) const char *pchMission)
{
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	
	if (pEnt && pEnt->pPlayer)
	{
		Mission *pMission = mission_FindMissionFromRefString(pEnt->pPlayer->missionInfo, pchMission);
		S32 iCount = mission_GetNumTimesCompletedByName(pEnt->pPlayer->missionInfo, pchMission);
		return iCount + ((pMission && pMission->state == MissionState_Succeeded) ? 1 : 0);
	}
	return 0;
}

// returns true if this mission can no longer be taken as it is in cooldown
AUTO_EXPR_FUNC(gameutil, UIGen) ACMD_NAME(PlayerMissionInCooldown);
U32 exprFuncPlayerMissionInCooldown(ExprContext *pContext, const char *pMissionName)
{
	Entity *pPlayer = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if(pPlayer && pPlayer->pChar)
	{
		const MissionCooldownInfo *missionInfo = mission_GetCooldownInfo(pPlayer, pMissionName);
		return missionInfo->bIsInCooldown;
	}
	return false;
}

// returns the cooldown count (number of times mission taken in the current cooldown block).
AUTO_EXPR_FUNC(gameutil, UIGen) ACMD_NAME(PlayerMissionCooldownCount);
U32 exprFuncPlayerMissionCooldownCount(ExprContext *pContext, const char *pMissionName)
{
	Entity *pPlayer = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if(pPlayer && pPlayer->pChar)
	{
		const MissionCooldownInfo *missionInfo = mission_GetCooldownInfo(pPlayer, pMissionName);
		if(missionInfo->bIsInCooldownBlock)
		{
			return missionInfo->uRepeatCount;
		}
	}
	return 0;
}

//	Inputs: pEntSource, pEntTarget
//	Return: Angle in degrees from the source to the target on XZ plane, 0 if either source or target is NULL
AUTO_EXPR_FUNC(Entity, UIGen);
F32 EntGetAngleToTarget(SA_PARAM_OP_VALID Entity* pEntSource, SA_PARAM_OP_VALID Entity* pEntTarget)
{
	if ( pEntSource && pEntTarget )
	{
		F32 fAngleTo, fAngleDir;
		Vec2 pyFace;
		Vec3 vecSourcePos, vecTargetPos, vecTo;
		entGetCombatPosDir(pEntSource,NULL,vecSourcePos,NULL);
		entGetCombatPosDir(pEntTarget,NULL,vecTargetPos,NULL);

		// Angle to Target from Source on XZ plane
		entGetFacePY(pEntSource,pyFace);
		subVec3(vecTargetPos,vecSourcePos,vecTo);
		fAngleTo = atan2(vecTo[0],vecTo[2]);
		fAngleDir = pyFace[1];
		return DEG(subAngle(fAngleTo,fAngleDir));
	}
	return 0;
}

AUTO_EXPR_FUNC(entityutil, Entity, UIGen) ACMD_NAME(EntGetTarget);
SA_RET_OP_VALID Entity* exprEntGetTarget(SA_PARAM_OP_VALID Entity* pEnt)
{
	return entity_GetTarget(pEnt);
}

// Get the public name of the current map
// This is available in the powers context, but it is highly recommended
// that you not use it for anything past prototyping.
AUTO_EXPR_FUNC(gameutil,UIGen, CEFuncsGeneric) ACMD_NAME(GetMapName);
const char *exprGetMapName(void)
{
	return zmapInfoGetPublicName(NULL);
}

AUTO_EXPR_FUNC(gameutil,UIGen,CEFuncsGeneric) ACMD_NAME(IsOnUGCMap);
bool exprIsOnUGCMap(void)
{
	return !!zmapIsUGCGeneratedMap(NULL);
}

AUTO_EXPR_FUNC(gameutil,UIGen) ACMD_NAME(IsOnMapNamed);
bool exprIsOnMapNamed(char *pMapName)
{
	const char * pName = zmapInfoGetPublicName(NULL);
	
	if(pName && pMapName && stricmp(pMapName, pName) == 0)
	{
		return true;
	}
	
	return false;
}

AUTO_EXPR_FUNC(gameutil,UIGen) ACMD_NAME(IsNotOnMapNamed);
bool exprIsNotOnMapNamed(char *pMapName)
{
	const char * pName = zmapInfoGetPublicName(NULL);

	if(pName && pMapName && stricmp(pMapName, pName) == 0)
	{
		return false;
	}

	return true;
}

AUTO_EXPR_FUNC(gameutil,UIGen) ACMD_NAME(MapForcedTeamSize);
bool exprGetMapForcedTeamSize()
{
	ZoneMapInfo* pZmapInfo = zmapGetInfo(NULL);

	if(pZmapInfo)
	{
		return zmapInfoGetMapForceTeamSize(pZmapInfo);
	}

	return 0;
}

AUTO_EXPR_FUNC(Player) ACMD_NAME(PlayerCreateTime);
U32 exprFuncPlayerCreateTime(ACMD_EXPR_SELF Entity* e)
{
	if(e && e->pPlayer)
	{
		return e->pPlayer->iCreatedTime;
	}

	return 0;
}

AUTO_EXPR_FUNC(entity) ACMD_NAME(CritterSetOverrideName);
bool exprCritterSetOverrideName(ACMD_EXPR_SELF Entity* pEnt, char* pchNewName)
{
	if (pEnt && pEnt->pCritter && pchNewName && pchNewName[0])
	{
		if (pEnt->pCritter->displayNameOverride)
			StructFreeStringSafe(&pEnt->pCritter->displayNameOverride);
		pEnt->pCritter->displayNameOverride = StructAllocString(pchNewName);
		entity_SetDirtyBit(pEnt, parse_Critter, pEnt->pCritter, false);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(Entity, player, encounter_action) ACMD_NOTESTCLIENT;
int EntOwnsPower(ACMD_EXPR_ENTARRAY_IN peaEnt, ACMD_EXPR_DICT(PowerDef) const char* powerDefName)
{
	Entity* pEnt = eaGet(peaEnt, 0);
	return (pEnt && pEnt->pChar && character_FindPowerByName(pEnt->pChar,powerDefName));
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerGetDDSkillBonus) ACMD_IFDEF(GAMESERVER);
F32 exprFuncPlayerGetDDSkillBonus(ExprContext *pContext, SA_PARAM_NN_STR const char *pchSkillName)
{
#ifdef GAMESERVER
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);

	return DDCharacterGetSkillBonus(pContext, SAFE_MEMBER(pEnt, pChar), pchSkillName);
#else
	return -999.f;
#endif
}

AUTO_EXPR_FUNC(player, Mission) ACMD_NAME(PlayerCompareClassName);
bool exprPlayerCompareClassName(ExprContext* context, const char *className)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if (pEnt && pEnt->pChar && className)
	{
		CharacterClass *pClass = GET_REF(pEnt->pChar->hClass);

		if( pClass )
		{
			return stricmp(pClass->pchName,className) == 0;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerPuppetCompareClassName);
bool exprPlayerPuppetCompareClassName(ExprContext* pContext, const char* pchPuppetType, const char* pchClassName)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pEnt && pchPuppetType && pchClassName)
	{
		Entity* pPuppetEntity = entity_GetPuppetEntityByType(pEnt, pchPuppetType, NULL, true, true);

		if (pPuppetEntity && pPuppetEntity->pChar)
		{
			CharacterClass* pClass = GET_REF(pPuppetEntity->pChar->hClass);

			if (pClass)
			{
				return stricmp(pClass->pchName, pchClassName) == 0;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerCompareClassCategory);
bool exprPlayerCompareClassCategory(ExprContext* pContext, const char* pchCategoryName)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	
	if (pEnt && pEnt->pChar && pchCategoryName)
	{
		CharacterClass *pClass = GET_REF(pEnt->pChar->hClass);
		CharClassCategory eCategory = StaticDefineIntGetInt(CharClassCategoryEnum, pchCategoryName);

		return (pClass && pClass->eCategory == eCategory);
	}
	return false;	
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerPuppetCompareClassCategory);
bool exprPlayerPuppetCompareClassCategory(ExprContext* pContext, const char* pchPuppetType, const char* pchCategoryName)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pEnt && pchPuppetType && pchCategoryName)
	{
		Entity* pPuppetEntity = entity_GetPuppetEntityByType(pEnt, pchPuppetType, NULL, true, true);

		if (pPuppetEntity && pPuppetEntity->pChar)
		{
			CharacterClass* pClass = GET_REF(pPuppetEntity->pChar->hClass);
			CharClassCategory eCategory = StaticDefineIntGetInt(CharClassCategoryEnum, pchCategoryName);

			return (pClass && pClass->eCategory == eCategory);
		}
	}
	return false;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerPuppetIsSpecies);
bool exprPlayerPuppetIsSpecies(ExprContext* pContext, const char* pchPuppetType, ACMD_EXPR_RES_DICT(SpeciesDef) const char* pchSpeciesName)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	const char* pchSpeciesNamePooled = allocFindString(pchSpeciesName);

	if (pEnt && pchPuppetType && pchSpeciesNamePooled)
	{
		Entity* pPuppetEntity;

		ANALYSIS_ASSUME(pEnt != NULL);
		pPuppetEntity = entity_GetPuppetEntityByType(pEnt, pchPuppetType, NULL, true, true);

		if (pPuppetEntity && pPuppetEntity->pChar)
		{
			SpeciesDef* pSpeciesDef = GET_REF(pPuppetEntity->pChar->hSpecies);

			if (pSpeciesDef && pSpeciesDef->pcName == pchSpeciesNamePooled)
			{
				return true;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerOwnsPet);
bool exprPlayerOwnsPet(ExprContext* pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char* pchPetDefName)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pEnt && pEnt->pSaved && pchPetDefName)
	{
		S32 i;
		for (i = eaSize(&pEnt->pSaved->ppOwnedContainers)-1; i >= 0; i--)
		{
			PetRelationship* pPet = pEnt->pSaved->ppOwnedContainers[i];
			Entity* pPetEnt = SavedPet_GetEntityEx(iPartitionIdx, pPet, false);
			PetDef* pPetDef = pPetEnt && pPetEnt->pCritter ? GET_REF(pPetEnt->pCritter->petDef) : NULL;
			if (pPetDef && stricmp(pPetDef->pchPetName, pchPetDefName)==0)
			{
				return true;
			}
		}
		for (i = eaSize(&pEnt->pSaved->ppCritterPets)-1; i >= 0; i--)
		{
			CritterPetRelationship* pCritterPet = pEnt->pSaved->ppCritterPets[i];
			PetDef* pPetDef = GET_REF(pCritterPet->hPetDef);
			if (pPetDef && stricmp(pPetDef->pchPetName, pchPetDefName)==0)
			{
				return true;
			}
		}
	}
	return false;
}

// Sets the Player's movement throttle, clamped to [-0.25..1]
AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerSetMovementThrottle);
void exprPlayerSetMovementThrottle(SA_PARAM_OP_VALID Entity *pEntity, F32 fThrottle)
{
	if(pEntity && pEntity->pPlayer)
	{
		pEntity->pPlayer->fMovementThrottle = CLAMPF32(fThrottle,PLAYER_MIN_THROTTLE,PLAYER_MAX_THROTTLE);
		if(pEntity->pChar)
			pEntity->pChar->bUpdateFlightParams = true;
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerNavToInteractable);
void exprPlayerNavToInteractable(ExprContext* pContext, const char* pchInteractableName)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if(pEnt && pEnt->pPlayer)
	{
#ifdef GAMESERVER
		GameInteractable* pInteractable = interactable_GetByName(pchInteractableName, NULL);
		if (pInteractable)
		{
			if (pInteractable && pInteractable->pWorldInteractable && pInteractable->pWorldInteractable->entry)
			{
				Vec3 vNavPos;
				copyVec3(pInteractable->pWorldInteractable->entry->base_entry.bounds.world_matrix[3], vNavPos);
				ClientCmd_NavToPosition(pEnt, vNavPos[0], vNavPos[1], vNavPos[2]);
			}
		}
#endif
	}
}

// Compare two ent arrays and return if their elements are identical
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCompareAll);
int exprFuncEntCompareAll(ACMD_EXPR_ENTARRAY_IN left, ACMD_EXPR_ENTARRAY_IN right)
{
	int i;

	if(eaSize(left)!=eaSize(right))
		return 0;

	for(i=eaSize(left)-1; i>=0; i--)
	{
		if(eaFind(right, (*left)[i])==-1)
			return 0;
	}

	return 1;
}

// Remove all entities but the Nth one.  Sets the Nth one as the first and only in the array.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropNth);
ExprFuncReturnVal exprFuncEntCropNth(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, int nth, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	if(nth<0)
	{
		nth = eaSize(entsInOut)+nth;
	}

	if(nth<0 || nth>=eaSize(entsInOut))
	{
		*errString = "nth arg out of bounds";
		return ExprFuncReturnError;
	}

	(*entsInOut)[0] = (*entsInOut)[nth];
	eaSetSize(entsInOut, 1);

	return ExprFuncReturnFinished;
}

// Return the index of the entity in entsIn, or -1 if not found
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntArrayFindEnt);
int exprFuncEntArrayFindEnt(ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_EXPR_ENTARRAY_IN entToFind)
{
	Entity* e = eaGet(entToFind, 0);
	if (e)
	{
		return eaFind(entsIn, e);
	}
	return -1;
}

// Remove entities from the passed in array
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropNotThese);
void exprFuncEntCropNotThese(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_ENTARRAY_IN these)
{
	int i;

	for(i=eaSize(these)-1; i>=0; i--)
		eaFindAndRemoveFast(entsInOut, (*these)[i]);
}

// Gets rid of all entities that don't have the specified owners
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCropOwner);
void exprFuncEntCropOwner(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_ENTARRAY_IN owners)
{
	int i, j;

	for(i = eaSize(entsInOut)-1; i >= 0; i--)
	{
		Entity* curEnt = (*entsInOut)[i];
		int found = false;

		for(j = eaSize(owners)-1; j >= 0; j--)
		{
			EntityRef curOwnerRef = entGetRef((*owners)[j]);
			if(curEnt->erOwner == curOwnerRef)
			{
				found = true;
				break;
			}
		}

		if(!found)
			eaRemoveFast(entsInOut, i);
	}
}

// Returns a point from the given entity in the direction of the specified yaw angle relative to the entity's facing
// the angle is in degrees
// the point has an orientation of the entity's facing 
// there must only be one entity in the array
AUTO_EXPR_FUNC(entity) ACMD_NAME(PointFromEntityFacingOffsetAngle);
void PointFromEntityFacingOffsetAngle(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_OUT pointOut, ACMD_EXPR_ENTARRAY_IN ents, F32 distanceOffset, F32 angle, ACMD_EXPR_ERRSTRING_STATIC errStr)
{
	Vec3 pyrFace;
	Vec3 curPos;
	Entity *e;
	WorldCollCollideResults results = {0};
	S32 numEnts = eaSize(ents);

	if(numEnts == 0 || numEnts > 1)
	{
		if (numEnts)
			*errStr = "Must only be one entity in the array";
		else
			*errStr = "No ents in array";

		identityMat4(pointOut);
		return;
	}

	e = eaHead(ents);
	angle = RAD(angle);
	
	entGetFacePY(e, pyrFace);
	pyrFace[2] = 0.f;
	pyrFace[1] = addAngle(pyrFace[1], angle);
	
	createMat3YPR(pointOut, pyrFace);
	entGetPos(e, curPos);
	curPos[1] += 4.f;
	scaleAddVec3(pointOut[2], distanceOffset, curPos, pointOut[3]);
	
	if(wcRayCollide(worldGetActiveColl(iPartitionIdx), curPos, pointOut[3], WC_FILTER_BIT_MOVEMENT, &results))
	{
		Vec3 dir;
		F32 len;

		subVec3(results.posWorldImpact, curPos, dir);
		len = normalVec3(dir);
		len -= 2;
		scaleAddVec3(dir, len, curPos, pointOut[3]);
	}
	
	worldGetPointFloorDistance(worldGetActiveColl(iPartitionIdx), pointOut[3], 6.f, 30.f, NULL);
}

// Returns a point from the given entity in the direction the entity is facing
// the point has an orientation of the entity's facing 
// there must only be one entity in the array
AUTO_EXPR_FUNC(entity) ACMD_NAME(PointFromEntityFacingOffset);
void exprFuncPointFromEntityFacingOffset(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_OUT pointOut, ACMD_EXPR_ENTARRAY_IN ents, F32 distanceOffset, ACMD_EXPR_ERRSTRING_STATIC errStr)
{
	PointFromEntityFacingOffsetAngle(iPartitionIdx, pointOut, ents, distanceOffset, 0, errStr);
}

// Returns true if the 
// moved here as it needs to be in a common file
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntHasCharacterPath);
bool CharacterPath_HasCharacterPath(SA_PARAM_OP_VALID Entity *pEntity)
{
	return entity_HasAnyCharacterPath(pEntity);
}

//  Inputs: entity
//  Return: 1 if the entity is not flying and not on the ground, otherwise 0
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntIsFalling);
int exprEntIsFalling(SA_PARAM_OP_VALID Entity *pEntity)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	S32 r = false;

	if(pEntity)
	{
		S32 bFlying = (pEntity->pChar && pEntity->pChar->pattrBasic->fFlight > 0);
		S32 bOnGround = mrSurfaceGetOnGround(pEntity->mm.mrSurface);
		r = !bFlying && !bOnGround;
	}

	return r;
#endif
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(CritterTeamSizeAtSpawn);
int exprFuncCritterTeamSizeAtSpawn(SA_PARAM_OP_VALID Entity *pEntity)
{
	if( pEntity && pEntity->pCritter )
	{
		return pEntity->pCritter->encounterData.activeTeamSize;
	}
	return 0;
}

// Returns the entity's owner if the entity is a pet or an empty ent array if the entity is not a pet
AUTO_EXPR_FUNC(entity) ACMD_NAME(GetCreatorTarget);
void exprFuncGetCreatorTarget(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut)
{
	Entity* owner = e-> erCreatorTarget ? entFromEntityRef(iPartitionIdx, e->erCreatorTarget) : NULL;

	if(owner && entIsAlive(owner))
		eaPush(entsOut, owner);
}

AUTO_STRUCT;
typedef struct IsFullMoonData
{
	// force the full moon to be off
	S32 bForceFullMoonOff;
	
	// force the full moon to be on
	S32 bForceFullMoonOn;
	

} IsFullMoonData;

IsFullMoonData gIsFullMoon;

AUTO_CMD_INT(gIsFullMoon.bForceFullMoonOff, ForceFullMoonOff) ACMD_CMDLINE;
AUTO_CMD_INT(gIsFullMoon.bForceFullMoonOn, ForceFullMoonOn) ACMD_CMDLINE;

#define MOON_OFFSET_DAYS 20.3629f
#define MOON_CYCLE_DAYS 29.5306f
#define FLOAT_SECONDS_PER_DAY 86400.0f

// Are we close to a full moon, added new parameter to allow number of +- number of days
// there is actually a very slight shift over time (the moon is slowing down?) but its too insignificant to matter
// Time is based on GMT
bool IsFullMoon(U32 curSeconds, U32 uDayRange)
{
	U32 moonDay;
	F32 fMoon, fMoonSeconds;

	// get moon phase, n.0 is full moon, n == moons since 2000
	fMoon = (((F32)curSeconds) - FLOAT_SECONDS_PER_DAY * MOON_OFFSET_DAYS ) / (FLOAT_SECONDS_PER_DAY * MOON_CYCLE_DAYS);

	// get closest moon
	fMoon = (U32)(fMoon + 0.5f);

	// seconds since 2000 at full moon
	fMoonSeconds = FLOAT_SECONDS_PER_DAY * MOON_OFFSET_DAYS + FLOAT_SECONDS_PER_DAY * MOON_CYCLE_DAYS * fMoon;

	// Get the moon day since 2000
	moonDay = (fMoonSeconds / FLOAT_SECONDS_PER_DAY);

	if(curSeconds >= (moonDay - uDayRange) * FLOAT_SECONDS_PER_DAY && curSeconds < (moonDay + 1 + uDayRange) * FLOAT_SECONDS_PER_DAY)
	{
		return true;
	}

	return false;
}

// for testing fullmoon
AUTO_COMMAND ACMD_NAME("FullMoonOn") ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD;
void FullMoonOn(S32 isOn)
{
	gIsFullMoon.bForceFullMoonOn = isOn;

#ifdef GAMECLIENT
	ServerCmd_FullMoonOn(gIsFullMoon.bForceFullMoonOn);
#endif
}


// Is this the full moon
AUTO_EXPR_FUNC(util) ACMD_NAME(IsFullMoon) ;
bool exprIsFullMoon(void)
{
	// the following is for testing
	if(gIsFullMoon.bForceFullMoonOn)
	{
		return true;
	}

	return IsFullMoon(timeServerSecondsSince2000(), 0);
}

// Is this the full moon with a number of days in the range
AUTO_EXPR_FUNC(util) ACMD_NAME(IsFullMoonRange) ;
bool exprIsFullMoonRange(U32 uDaysRange)
{
	return IsFullMoon(timeServerSecondsSince2000(), uDaysRange);
}

// Returns 1 if the Entity has the specified personal powermode with any target, 0 if it's not.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetHasPersonalPowerModeAnyTarget");
S32 exprEntGetHasPersonalPowerModeAnyTarget(SA_PARAM_OP_VALID Entity *pEntity, ACMD_EXPR_ENUM(PowerMode) const char *modeName)
{
	int iMode = StaticDefineIntGetInt(PowerModeEnum,modeName);
	if (pEntity && pEntity->pChar)
	{
		return character_HasModePersonalAnyTarget(pEntity->pChar,iMode);
	}
	return 0;
}

// Sends a gen message to a client
AUTO_EXPR_FUNC(player) ACMD_NAME("PlayerSendGenMessage");
void exprPlayerSendGenMessage(ExprContext* pContext, const char* pchGenName, const char* pchMessage)
{
#ifdef GAMESERVER
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pEnt)
	{
		ClientCmd_GenSendMessage(pEnt, pchGenName, pchMessage);
	}
#endif
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ChooseEntityStrict);
SA_RET_NN_VALID Entity *exprChooseEntityStrict(int which, SA_PARAM_NN_VALID Entity *a, SA_PARAM_NN_VALID Entity *b)
{
	return which ? a : b;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ChooseEntity);
SA_RET_OP_VALID Entity *exprChooseEntity(int which, SA_PARAM_OP_VALID Entity *a, SA_PARAM_OP_VALID Entity *b)
{
	return which ? a : b;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCanUseCharacterPath);
bool exprFuncEntCanUseCharacterPath(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_OP_VALID CharacterPath *pPath)
{
	return pEnt && pPath && Entity_EvalCharacterPathRequiresExpr(pEnt, pPath);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCanUseCharacterPathByName);
bool exprFuncEntCanUseCharacterPathByName(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_STR const char* pchPath)
{
	CharacterPath *pPath = RefSystem_ReferentFromString(g_hCharacterPathDict, pchPath);
	return pEnt && pPath && Entity_EvalCharacterPathRequiresExpr(pEnt, pPath);
}

AUTO_EXPR_FUNC(util) ACMD_NAME(EntGameAccountCharacterSlotTest);
bool exprFuncEntGetGameAccountCharacterSlots(ExprContext* pContext, S32 iValue)
{
	// This expression requires that the game account data is set in the expression context.
	// This is done as this expression can be used on the game account server, login server and the client. 
	// If this context is used more often it will require a new AUTO_EXPR_FUNC(some new type)
	const GameAccountData *pAccountData = exprContextGetGAD(pContext);
	if(pAccountData)
	{
		S32 iGameAccountSlots = gad_GetAttribInt(pAccountData, MicroTrans_GetCharSlotsGADKey());
		if(iGameAccountSlots <= iValue)
		{
			return true;
		}
	}

	return false;
}


// Get the value of this game account attrib
AUTO_EXPR_FUNC(player) ACMD_NAME("GadAttribValueTest");
bool exprGadAttribValueTest(ExprContext* pContext, const char* pchAttrib, S32 iValue)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if(pEnt)
	{
		GameAccountData *pData = entity_GetGameAccount(pEnt);
		if(pData)
		{
			S32 iVal = gad_GetAttribInt(pData, pchAttrib);
			if(iVal <= iValue)
			{
				return true;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(GadSharedBankSlotTest);
bool exprFuncGadSharedBankSlotTest(ExprContext* pContext, S32 iValue)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if(pEnt)
	{
		GameAccountData *pData = entity_GetGameAccount(pEnt);
		if(pData)
		{
			S32 iVal = gad_GetAttribInt(pData, MicroTrans_GetSharedBankSlotGADKey()) + gad_GetAccountValueInt(pData, MicroTrans_GetSharedBankSlotASKey());
			if(iVal <= iValue)
			{
				return true;
			}
		}
	}
	return false;
}

// Check to see if the given item is a costume unlock and
// if the given player never has never unlocked the costume on the item before
AUTO_EXPR_FUNC(player) ACMD_NAME(StoreItemHasUnlocksForPlayer);
bool ExprStoreItemHasUnlocksForPlayer(ExprContext* context)
{
	Entity* pPlayerEnt = exprContextGetVarPointerUnsafePooled(context, "Player");
	ItemDef *pItemDef = exprContextGetVarPointerUnsafePooled(context, store_GetItemNameContextPtr());

	if(pPlayerEnt && pItemDef)
	{
		SavedEntityData *pSaved = SAFE_MEMBER(pPlayerEnt, pSaved);
		GameAccountData *pAccountData = entity_GetGameAccount(pPlayerEnt);
		S32 i;

		if(pSaved && pAccountData && pItemDef->eCostumeMode == kCostumeDisplayMode_Unlock)
		{
			if (eaSize(&pItemDef->ppCostumes) > 0)
			{
				for (i = eaSize(&pItemDef->ppCostumes) - 1; i >= 0; i--)
				{
					if (!costumeEntity_IsUnlockedCostumeRef(pSaved->costumeData.eaUnlockedCostumeRefs, pAccountData, pPlayerEnt, pPlayerEnt, REF_STRING_FROM_HANDLE(pItemDef->ppCostumes[i]->hCostumeRef)))
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(entityutil);
bool GamePermissions_CanBuyBag(SA_PARAM_NN_VALID Entity *pEntity, U32 eBagId)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);

	bool bResult = GamePermissions_trh_CanBuyBag(CONTAINER_NOCONST(Entity, pEntity), eBagId, pExtract);

	return bResult;
}

AUTO_EXPR_FUNC(entityutil, Player) ACMD_NAME(EntGetHiddenInGateway);
bool gclExprEntGetHiddenInGateway(SA_PARAM_OP_VALID Entity *pEnt)
{
	return SAFE_MEMBER3(pEnt, pPlayer, pGatewayInfo, bHidden);
}

AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME("EntGetDistanceFromPoint");
F32 gclEntGetDistanceFromPoint(SA_PARAM_NN_VALID Entity *pEnt, F32 fX, F32 fY, F32 fZ)
{
	Vec3 vPoint;
	setVec3(vPoint, fX, fY, fZ);
	return entGetDistance(pEnt, NULL, NULL, vPoint, NULL);
}

//	Inputs: pPlayerEnt
//	Time in seconds remaining for Cooldown
AUTO_EXPR_FUNC(Entity, UIGen);
U32 EntGetRewardGatedCooldownSeconds(SA_PARAM_OP_VALID Entity* pEntPlayer, const char *pRewardGatedName)
{
	if(pEntPlayer && pEntPlayer->pPlayer && pRewardGatedName)
	{
		S32 gatedType = StaticDefineIntGetInt(RewardGatedTypeEnum,pRewardGatedName);

		if(gatedType != -1)
		{
			return Reward_GetGatedCooldown(pEntPlayer, gatedType);
		}
		else
		{
			ErrorDetailsf("Invalid reward gated type name %s", pRewardGatedName);
			Errorf("EntGetRewardGatedCooldownSeconds call with invalid RewardGatedTypeName.");
		}
	}

	return 0;
}


#include "EntityExprFunc_c_ast.c"
