/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiFCExprFunc.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntityLib.h"
#include "error.h"
#include "EString.h"
#include "Expression.h"
#include "GameEvent.h"
#include "GlobalTypes.h"
#include "gslEncounter.h"
#include "gslEventTracker.h"
#include "gslMechanics.h"
#include "gslOldEncounter.h"
#include "gslPartition.h"
#include "mission_common.h"
#include "nemesis_common.h"
#include "Player.h"
#include "StringCache.h"
#include "Team.h"
#include "mapstate_common.h"
#include "nemesis.h"

// ----------------------------------------------------------------------------------
// Static checking
// ----------------------------------------------------------------------------------

static int StaticCheckNemesisMotivation(ExprContext *context, MultiVal *pMV, char **estrError)
{
	if(!pMV->type==MULTI_STRING)
	{
		estrPrintf(estrError, "Must be a string");
		return false;
	}
	else if (StaticDefineInt_FastStringToInt(NemesisMotivationEnum,pMV->str, INT_MIN) == INT_MIN)
	{
		estrPrintf(estrError, "Invalid %s %s", "NemesisMotivation", pMV->str);
		return false;
	}
	return true;
}
static int StaticCheckNemesisPersonality(ExprContext *context, MultiVal *pMV, char **estrError)
{
	if(!pMV->type==MULTI_STRING)
	{
		estrPrintf(estrError, "Must be a string");
		return false;
	}
	else if(StaticDefineInt_FastStringToInt(NemesisPersonalityEnum,pMV->str, INT_MIN) == INT_MIN)
	{
		estrPrintf(estrError, "Invalid %s %s", "NemesisPersonality", pMV->str);
		return false;
	}
	return true;
}
static int StaticCheckNemesisMinionCostumeSet(ExprContext *context, MultiVal *pMV, char **estrError)
{
	int i, n;
	if(!pMV->type==MULTI_STRING)
	{
		estrPrintf(estrError, "Must be a string");
		return false;
	}

	n = eaSize(&g_NemesisMinionCostumeSetList.sets);
	for(i=0; i<n; i++)
	{
		if(0 == stricmp(g_NemesisMinionCostumeSetList.sets[i]->pcName, pMV->str))
		{
			return true;
		}
	}

	estrPrintf(estrError, "Invalid %s %s", "NemesisMinionCostumeSet", pMV->str);
	return false;
}
static int StaticCheckNemesisMinionPowerSet(ExprContext *context, MultiVal *pMV, char **estrError)
{
	int i, n;
	if(!pMV->type==MULTI_STRING)
	{
		estrPrintf(estrError, "Must be a string");
		return false;
	}

	n = eaSize(&g_NemesisMinionPowerSetList.sets);
	for(i=0; i<n; i++)
	{
		if(0 == stricmp(g_NemesisMinionPowerSetList.sets[i]->pcName, pMV->str))
		{
			return true;
		}
	}

	estrPrintf(estrError, "Invalid %s %s", "NemesisMinionPowerSet", pMV->str);
	return false;
}


AUTO_STARTUP(ExpressionSCRegister);
void encounterEvalStaticCheckInit(void)
{
	// These static check types should be available on both the server and the client
	exprRegisterStaticCheckArgumentType("NemesisMotivation", NULL, StaticCheckNemesisMotivation);
	exprRegisterStaticCheckArgumentType("NemesisPersonality", NULL, StaticCheckNemesisPersonality);
	exprRegisterStaticCheckArgumentType("NemesisMinionCostumeSet", NULL, StaticCheckNemesisMinionCostumeSet);
	exprRegisterStaticCheckArgumentType("NemesisMinionPowerSet", NULL, StaticCheckNemesisMinionPowerSet);
}


// ----------------------------------------------------------------------------------
// Nemesis accessors (encounter_action)
// ----------------------------------------------------------------------------------

// Generic static check function to make an expression update on Nemesis state changes (when used on a Mission)
AUTO_EXPR_FUNC_STATIC_CHECK;
bool NemesisStateChangeBool_SC(ExprContext* context)
{
	MissionDef* missionDef = exprContextGetVarPointerUnsafePooled(context, g_MissionDefVarName);
	if (missionDef)
	{
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		pEvent->pchEventName = allocAddString("NemesisStateChange");
		pEvent->type = EventType_NemesisState;
		pEvent->tMatchSource = TriState_Yes;
		eventtracker_AddNamedEventToList(&missionDef->eaTrackedEventsNoSave, pEvent, missionDef->filename);
	}
	return true;
}

// Generic static check function to make an expression update on Nemesis state changes (when used on a Mission)
AUTO_EXPR_FUNC_STATIC_CHECK;
int NemesisStateChangeInt_SC(ExprContext* context)
{
	NemesisStateChangeBool_SC(context);
	return 0;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerHasAnyNemesis) ACMD_EXPR_STATIC_CHECK(NemesisStateChangeBool_SC);
bool exprFuncPlayerHasAnyNemesis(ExprContext* context)
{
	Entity* playerEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);

	if(playerEnt && playerEnt->pPlayer)
	{
		if(eaSize(&playerEnt->pPlayer->nemesisInfo.eaNemesisStates))
			return true;
	}

	return false;
}

// TRUE if the player has a Primary Nemesis and the Nemesis is loaded and ready to spawn, FALSE otherwise.
AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerHasPrimaryNemesisLoaded);
bool exprFuncPlayerHasPrimaryNemesisLoaded(ExprContext* context)
{
	Entity* playerEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if(playerEnt && player_GetPrimaryNemesis(playerEnt)){
		return true;
	}
	return false;
}

// TRUE if the player has a Primary Nemesis (even if the Nemesis is not loaded yet), FALSE otherwise.
AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerHasPrimaryNemesisLoadedOrNot) ACMD_EXPR_STATIC_CHECK(NemesisStateChangeBool_SC);
bool PlayerHasPrimaryNemesisLoadedOrNot(ExprContext* context)
{
	Entity* playerEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if(playerEnt && player_GetPrimaryNemesisID(playerEnt)){
		return true;
	}
	return false;
}

// DEPRECATED - Use "PlayerHasPrimaryNemesisLoaded" or "PlayerHasPrimaryNemesisLoadedOrNot" instead
// TRUE if the player has a Primary Nemesis and the Nemesis is loaded and ready to spawn, FALSE otherwise.
AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerHasPrimaryNemesis);
bool exprFuncPlayerHasPrimaryNemesis(ExprContext* context)
{
	return exprFuncPlayerHasPrimaryNemesisLoaded(context);
}


// Gets the number of Nemeses that the player currently has
AUTO_EXPR_FUNC(player, mission) ACMD_NAME(NemesisCount) ACMD_EXPR_STATIC_CHECK(NemesisStateChangeInt_SC);
int exprFuncNemesisCount(ExprContext* context)
{
	Entity* playerEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);

	if(playerEnt && playerEnt->pPlayer){
		return eaSize(&playerEnt->pPlayer->nemesisInfo.eaNemesisStates);
	}

	return 0;
}

// Gets the total number of times a player has defeated a Nemesis
AUTO_EXPR_FUNC(player, mission) ACMD_NAME(NemesisDefeatCountTotal) ACMD_EXPR_STATIC_CHECK(NemesisStateChangeInt_SC);
int exprFuncNemesisDefeatCountTotal(ExprContext* context)
{
	Entity* playerEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	U32 uDefeatCount = 0;

	if(playerEnt && playerEnt->pPlayer){
		int i;
		for (i = 0; i < eaSize(&playerEnt->pPlayer->nemesisInfo.eaNemesisStates); i++){
			uDefeatCount += playerEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->uTimesDefeated;
		}
	}

	return (int)uDefeatCount;
}

// Gets the highest number of times that the player has defeated a single Nemesis
AUTO_EXPR_FUNC(player, mission) ACMD_NAME(NemesisDefeatCountMax) ACMD_EXPR_STATIC_CHECK(NemesisStateChangeInt_SC);
int exprFuncNemesisDefeatCountMax(ExprContext* context)
{
	Entity* playerEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	U32 uDefeatCount = 0;

	if(playerEnt && playerEnt->pPlayer){
		int i;
		for (i = 0; i < eaSize(&playerEnt->pPlayer->nemesisInfo.eaNemesisStates); i++){
			if (playerEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->uTimesDefeated > uDefeatCount){
				uDefeatCount = playerEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->uTimesDefeated;
			}
		}
	}

	return (int)uDefeatCount;
}

// Commands to get a Nemesis.
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(GetMapOwnerNemesis);
SA_RET_OP_VALID Entity* exprFuncGetMapOwnerNemesis(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx)
{
	Entity* pEnt = partition_GetPlayerMapOwner(iPartitionIdx);

	if(pEnt)
	{
		return player_GetPrimaryNemesis(entFromContainerID(iPartitionIdx, entGetType(pEnt), entGetContainerID(pEnt)));
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "GetMapOwnerNemesis: map has no owner");
	}

	return NULL;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetEncounterOwnerNemesis);
SA_RET_OP_VALID Entity* exprFuncGetEncounterOwnerNemesis(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx)
{
	if(e && e->pCritter && e->pCritter->encounterData.parentEncounter)
	{
		Entity *pEnt = GET_REF(e->pCritter->encounterData.parentEncounter->hOwner);
		if (pEnt){
			return player_GetPrimaryNemesis(entFromContainerID(iPartitionIdx, entGetType(pEnt),entGetContainerID(pEnt)));
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "GetEncounterOwnerNemesis: Critter has no Encounter");
	}

	return NULL;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetSpawningPlayerNemesis);
SA_RET_OP_VALID Entity* exprFuncGetSpawningPlayerNemesis(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx)
{
	Entity* pEnt = NULL;
	GameEncounter *pEncounter = exprContextGetVarPointerUnsafePooled(context, g_Encounter2VarName);
	OldEncounter* pOldEncounter = exprContextGetVarPointerUnsafePooled(context, g_EncounterVarName);
	GameEncounterPartitionState *pState = NULL;

	// Get the encounter from the context or from the critter
	if(!pEncounter && be && be->pCritter && be->pCritter->encounterData.pGameEncounter) {
		pEncounter = be->pCritter->encounterData.pGameEncounter;
		pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
	}
	if(gConf.bAllowOldEncounterData && !pOldEncounter && be && be->pCritter && be->pCritter->encounterData.parentEncounter) {
		pOldEncounter = be->pCritter->encounterData.parentEncounter;
	}
	
	if (be && be->pCritter && be->pCritter->spawningPlayer)
	{
		pEnt = entFromEntityRef(iPartitionIdx, be->pCritter->spawningPlayer);
	}
	else if (pEncounter && pState && pState->playerData.uSpawningPlayer)
	{
		pEnt = entFromEntityRef(iPartitionIdx, pState->playerData.uSpawningPlayer);
	}
	else if (gConf.bAllowOldEncounterData && pOldEncounter && pOldEncounter->spawningPlayer)
	{
		pEnt = entFromEntityRef(iPartitionIdx, pOldEncounter->spawningPlayer);
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "GetSpawningPlayerNemesis: No spawning player found");
	}

	if(pEnt){
		return player_GetPrimaryNemesis(pEnt);
	}

	return NULL;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(GetPlayerNemesis);
SA_RET_OP_VALID Entity* exprFuncGetPlayerNemesis(ExprContext* context)
{
	Entity* pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);

	if(pEnt)
	{
		return player_GetPrimaryNemesis(pEnt);
	}

	return NULL;
}


AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(NemesisMotivationIsType);
ExprFuncReturnVal exprFuncNemesisMotivationIsType(ExprContext* context, ACMD_EXPR_INT_OUT ret, ACMD_EXPR_ENTARRAY_IN peaNemesisEnt, ACMD_EXPR_SC_TYPE(NemesisMotivation) const char* typeToCheck, ACMD_EXPR_ERRSTRING errString)
{
	NemesisMotivation motivationToCheck = StaticDefineIntGetInt(NemesisMotivationEnum, typeToCheck);
	Entity *pNemesisEnt = eaGet(peaNemesisEnt, 0);

	(*ret) = (pNemesisEnt && pNemesisEnt->pNemesis && pNemesisEnt->pNemesis->motivation == motivationToCheck);

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(NemesisPersonalityIsType);
ExprFuncReturnVal exprFuncNemesisPersonalityIsType(ExprContext* context, ACMD_EXPR_INT_OUT ret, ACMD_EXPR_ENTARRAY_IN peaNemesisEnt, ACMD_EXPR_SC_TYPE(NemesisPersonality) const char* typeToCheck, ACMD_EXPR_ERRSTRING errString)
{
	NemesisPersonality personalityToCheck = StaticDefineIntGetInt(NemesisPersonalityEnum, typeToCheck);
	Entity *pNemesisEnt = eaGet(peaNemesisEnt, 0);

	(*ret) = (pNemesisEnt && pNemesisEnt->pNemesis && pNemesisEnt->pNemesis->personality == personalityToCheck);

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(NemesisMinionCostumeIsType);
ExprFuncReturnVal exprFuncNemesisMinionCostumeIsType(ExprContext* context, ACMD_EXPR_INT_OUT ret, ACMD_EXPR_ENTARRAY_IN peaNemesisEnt, ACMD_EXPR_SC_TYPE(NemesisMinionCostumeSet) const char* costumeSetName, ACMD_EXPR_ERRSTRING errString)
{
	Entity *pNemesisEnt = eaGet(peaNemesisEnt, 0);
	(*ret) = (pNemesisEnt && pNemesisEnt->pNemesis && !stricmp(pNemesisEnt->pNemesis->pchMinionCostumeSet, costumeSetName));
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(NemesisMinionPowersetIsType);
ExprFuncReturnVal exprFuncNemesisMinionPowersetIsType(ExprContext* context, ACMD_EXPR_INT_OUT ret, ACMD_EXPR_ENTARRAY_IN peaNemesisEnt, ACMD_EXPR_SC_TYPE(NemesisMinionPowerSet) const char* powerSetName, ACMD_EXPR_ERRSTRING errString)
{
	Entity *pNemesisEnt = eaGet(peaNemesisEnt, 0);
	(*ret) = (pNemesisEnt && pNemesisEnt->pNemesis && !stricmp(pNemesisEnt->pNemesis->pchMinionPowerSet, powerSetName));
	return ExprFuncReturnFinished;
}

// Deprecated functions to get a nemesis from an entarray
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(PrimaryNemesisMotivationIsType);
ExprFuncReturnVal exprFuncPrimaryNemesisMotivationIsType(ExprContext* context, ACMD_EXPR_INT_OUT ret, ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_SC_TYPE(NemesisMotivation) const char* typeToCheck, ACMD_EXPR_ERRSTRING errString)
{
	Entity* ent;
	NemesisMotivation motivationToCheck = StaticDefineIntGetInt(NemesisMotivationEnum, typeToCheck);

	if(motivationToCheck == -1)
	{
		estrPrintf(errString, "%s is not a valid nemesis motivation", typeToCheck);
		return ExprFuncReturnError;
	}
	if(eaSize(ents)!=1)
	{
		estrPrintf(errString, "Too many or too few entities passed in: %d (1 allowed)", eaSize(ents));
		return ExprFuncReturnError;
	}

	devassert((*ents)); // Fool static check
	ent = (*ents)[0];

	if(ent)
	{
		Entity* pNemesisEnt = player_GetPrimaryNemesis(ent);
		if(pNemesisEnt && pNemesisEnt->pNemesis)
		{
			(*ret) = pNemesisEnt->pNemesis->motivation == motivationToCheck;
			return ExprFuncReturnFinished;
		}
	}

	estrPrintf(errString, "No nemesis on entity");
	return ExprFuncReturnError;
}

AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(PrimaryNemesisPersonalityIsType);
ExprFuncReturnVal exprFuncPrimaryNemesisPersonalityIsType(ExprContext* context, ACMD_EXPR_INT_OUT ret, ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_SC_TYPE(NemesisPersonality) const char* typeToCheck, ACMD_EXPR_ERRSTRING errString)
{
	Entity* ent = NULL;
	NemesisPersonality personalityToCheck = StaticDefineIntGetInt(NemesisPersonalityEnum, typeToCheck);

	if(personalityToCheck == -1)
	{
		estrPrintf(errString, "%s is not a valid nemesis personality", typeToCheck);
		return ExprFuncReturnError;
	}
	if(eaSize(ents)!=1)
	{
		estrPrintf(errString, "Too many or too few entities passed in: %d (1 allowed)", eaSize(ents));
		return ExprFuncReturnError;
	}
	devassert((*ents)); // Fool static check
	ent = (*ents)[0];

	if(ent)
	{
		Entity* pNemesisEnt = player_GetPrimaryNemesis(ent);
		if(pNemesisEnt && pNemesisEnt->pNemesis){
			(*ret) = pNemesisEnt->pNemesis->personality == personalityToCheck;
			return ExprFuncReturnFinished;
		}
	}

	estrPrintf(errString, "No nemesis on entity");
	return ExprFuncReturnError;
}

AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(PrimaryNemesisMinionCostumeIsType);
ExprFuncReturnVal exprFuncPrimaryNemesisMinionCostumeIsType(ExprContext* context, ACMD_EXPR_INT_OUT ret, ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_SC_TYPE(NemesisMinionCostumeSet) const char* costumeSetName, ACMD_EXPR_ERRSTRING errString)
{
	Entity* ent = NULL;

	if(eaSize(ents)!=1)
	{
		estrPrintf(errString, "Too many or too few entities passed in: %d (1 allowed)", eaSize(ents));
		return ExprFuncReturnError;
	}
	devassert((*ents)); // Fool static check
	ent = (*ents)[0];

	if(ent)
	{
		Entity* pNemesisEnt = player_GetPrimaryNemesis(ent);
		if(pNemesisEnt && pNemesisEnt->pNemesis)
		{
			(*ret) = !stricmp(pNemesisEnt->pNemesis->pchMinionCostumeSet, costumeSetName);
			return ExprFuncReturnFinished;
		}
	}

	estrPrintf(errString, "No nemesis on entity");
	return ExprFuncReturnError;
}

AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(PrimaryNemesisMinionPowersetIsType);
ExprFuncReturnVal exprFuncPrimaryNemesisMinionPowersetIsType(ExprContext* context, ACMD_EXPR_INT_OUT ret, ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_SC_TYPE(NemesisMinionPowerSet) const char* powerSetName, ACMD_EXPR_ERRSTRING errString)
{
	Entity* ent = NULL;

	if(eaSize(ents)!=1)
	{
		estrPrintf(errString, "Too many or too few entities passed in: %d (1 allowed)", eaSize(ents));
		return ExprFuncReturnError;
	}
	devassert((*ents)); // Fool static check
	ent = (*ents)[0];

	if(ent)
	{
		Entity* pNemesisEnt = player_GetPrimaryNemesis(ent);
		if(pNemesisEnt && pNemesisEnt->pNemesis)
		{
			(*ret) = !stricmp(pNemesisEnt->pNemesis->pchMinionPowerSet, powerSetName);
			return ExprFuncReturnFinished;
		}
	}

	estrPrintf(errString, "No nemesis on entity");
	return ExprFuncReturnError;
}

AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(GetPrimaryNemesisName);
ExprFuncReturnVal exprFuncGetPrimaryNemesisName(ExprContext* context, ACMD_EXPR_STRING_OUT retString, ACMD_EXPR_ENTARRAY_IN ents, ACMD_EXPR_ERRSTRING errString)
{
	Entity* ent;

	if(eaSize(ents)!=1)
	{
		estrPrintf(errString, "Too many or too few entities passed in: %d (1 allowed)", eaSize(ents));
		return ExprFuncReturnError;
	}
	devassert((*ents)); // Fool static check
	ent = (*ents)[0];

	if(ent)
	{
		Entity* pNemesisEnt = player_GetPrimaryNemesis(ent);
		if(pNemesisEnt)	{
			(*retString) = entGetPersistedName(pNemesisEnt);
			return ExprFuncReturnFinished;
		}
	}

	estrPrintf(errString, "GetPrimaryNemesisName : No entities given");
	return ExprFuncReturnError;
}

//-----------------------------------------------------------------------------
// Nemesis-specific actions
//-----------------------------------------------------------------------------

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSCSayExternMessageVarNemesis(ACMD_EXPR_SELF Entity* e, ExprContext* context, 
												const char* category, const char* pchVarPrefix, F32 duration,
												ACMD_EXPR_ERRSTRING_STATIC errString)
{
	int i;
	char *estrBuffer = NULL;
	estrStackCreate(&estrBuffer);

	// Add an Extern Var for each Nemesis Personality Type
	for (i = 0; i < NemesisPersonality_Count; i++){
		estrPrintf(&estrBuffer, "%s_%s", pchVarPrefix, StaticDefineIntRevLookup(NemesisPersonalityEnum, i));
		if(ExprFuncReturnFinished != exprContextExternVarSC(context, category, estrBuffer, NULL, NULL, MULTI_STRING, "message", true, errString)){
			estrDestroy(&estrBuffer);
			return ExprFuncReturnError;
		}
	}

	// Add a "Default" extern var
	estrPrintf(&estrBuffer, "%s_%s", pchVarPrefix, "Default");
	if(ExprFuncReturnFinished != exprContextExternVarSC(context, category, estrBuffer, NULL, NULL, MULTI_STRING, "message", true, errString)){
		estrDestroy(&estrBuffer);
		return ExprFuncReturnError;
	}

	estrDestroy(&estrBuffer);
	return ExprFuncReturnFinished;
}

// Makes a Nemesis critter say a message from an extern var named <VarPrefix>_<NemesisPersonality> from
// <category>. This is correctly localized for whoever sees it and stays up for <duration>
// seconds
AUTO_EXPR_FUNC(ai) ACMD_NAME(SayExternMessageVarNemesis) ACMD_EXPR_STATIC_CHECK(exprFuncSCSayExternMessageVarNemesis);
ExprFuncReturnVal exprFuncSayExternMessageVarNemesis(ACMD_EXPR_SELF Entity* e, ExprContext* context,
											  const char* category, const char* pchVarPrefix, F32 duration,
											  ACMD_EXPR_ERRSTRING_STATIC errString)
{
	Entity *pNemesisEnt = NULL;
	MultiVal answer = {0};
	ExprFuncReturnVal retval;
	char *estrBuffer = NULL;
	estrStackCreate(&estrBuffer);

	if (e && e->pCritter){
		pNemesisEnt = GET_REF(e->pCritter->hSavedPet);
		if (!pNemesisEnt){
			pNemesisEnt = GET_REF(e->pCritter->hSavedPetOwner);
		}
	}

	if (pNemesisEnt && pNemesisEnt->pNemesis){
		estrPrintf(&estrBuffer, "%s_%s", pchVarPrefix, StaticDefineIntRevLookup(NemesisPersonalityEnum, pNemesisEnt->pNemesis->personality));
	} else {
		estrPrintf(&estrBuffer, "%s_%s", pchVarPrefix, "Default");
	}

	retval = exprContextGetExternVar(context, category, estrBuffer, MULTI_STRING, &answer, errString);

	estrDestroy(&estrBuffer);

	if(retval != ExprFuncReturnFinished)
		return retval;

	if(!answer.str[0])
		return ExprFuncReturnFinished;

	if (e){
		aiSayMessageInternal(e, NULL, context, answer.str, NULL, duration);
	}

	return ExprFuncReturnFinished;
}

static bool Nemesis_TeamIsLeaderSet(Entity *pEntity)
{
	if(pEntity)
	{
		MapState *pState = mapState_FromEnt(pEntity);
		if(pState)
		{
			return pState->nemesisInfo.bLeaderSet;
		}
	}

	return false;
}


static bool Nemesis_TeamIsIndexSet(Entity *pEntity, S32 iIndex)
{
	if(pEntity && iIndex >= 0)
	{
		MapState *pState = mapState_FromEnt(pEntity);
		if(pState && iIndex < eaSize(&pState->nemesisInfo.eaNemesisTeam))
		{
			if(pState->nemesisInfo.eaNemesisTeam[iIndex]->iId > 0 || pState->nemesisInfo.eaNemesisTeam[iIndex]->bNoNemesis)
			{
				return true;
			}
		}
	}

	return false;
}

// True if the team leader (or with optional flag any team member) has nemesis loaded
AUTO_EXPR_FUNC(player, encounter, encounter_action, ai) ACMD_NAME(NemesisLeaderLoadedByEnt);
bool Nemesis_LeaderLoadedByEnt(SA_PARAM_OP_VALID Entity *pPlayerEnt, bool bAnyTeamMember)
{
	Entity *pEnt = Nemesis_TeamGetTeamLeader(pPlayerEnt, bAnyTeamMember);
	if(pEnt && player_GetPrimaryNemesis(pEnt))
	{
		return true;
	}

	return false;
}


// True if the team leader (or with optional flag any team member) has nemesis loaded
AUTO_EXPR_FUNC(player) ACMD_NAME(Nemesis_LeaderLoaded);
bool Nemesis_LeaderLoaded(ExprContext* context, bool bAnyTeamMember)
{
	Entity* pPlayerEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);

	return Nemesis_LeaderLoadedByEnt(pPlayerEnt, bAnyTeamMember);
}

// True if the team leader (or with optional flag any team member) has a nemesis
AUTO_EXPR_FUNC(player, encounter, encounter_action, ai) ACMD_NAME(NemesisLeaderLoadedOrNotByEnt);
bool Nemesis_LeaderLoadedOrNotByEnt(SA_PARAM_OP_VALID Entity *pPlayerEnt, bool bAnyTeamMember)
{
	Entity *pEnt = Nemesis_TeamGetTeamLeader(pPlayerEnt, bAnyTeamMember);
	if(pEnt && player_GetPrimaryNemesisID(pEnt))
	{
		return true;
	}

	return false;
}

// True if the team leader (or with optional flag any team member) has a nemesis
AUTO_EXPR_FUNC(player) ACMD_NAME(NemesisLeaderLoadedOrNot);
bool Nemesis_LeaderLoadedOrNot(ExprContext* context, bool bAnyTeamMember)
{
	Entity* pPlayerEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	return Nemesis_LeaderLoadedOrNotByEnt(pPlayerEnt, bAnyTeamMember);
}

// True if the team index (0 is for solo) has a nemesis
AUTO_EXPR_FUNC(player, encounter, encounter_action, ai) ACMD_NAME(NemesisTeamIndexLoadedByEnt);
bool Nemesis_TeamIndexLoadedByEnt(SA_PARAM_OP_VALID Entity *pPlayerEnt, S32 iIndex)
{
	Entity *pEnt = Nemesis_TeamGetTeamIndex(pPlayerEnt, iIndex);
	if(pEnt && player_GetPrimaryNemesis(pEnt))
	{
		return true;
	}

	return false;
}
// True if the team index (0 is for solo) has a nemesis
AUTO_EXPR_FUNC(player) ACMD_NAME(NemesisTeamIndexLoaded);
bool Nemesis_TeamIndexLoaded(ExprContext* context, S32 iIndex)
{
	Entity* pPlayerEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	return Nemesis_TeamIndexLoadedByEnt(pPlayerEnt, iIndex);
}

// True if the team index (0 is for solo) has a nemesis
AUTO_EXPR_FUNC(player, encounter, encounter_action, ai) ACMD_NAME(NemesisTeamIndexLoadedOrNotByEnt);
bool Nemesis_TeamIndexLoadedOrNotByEnt(SA_PARAM_OP_VALID Entity *pPlayerEnt, S32 iIndex)
{
	Entity *pEnt = Nemesis_TeamGetTeamIndex(pPlayerEnt, iIndex);
	if(pEnt && player_GetPrimaryNemesisID(pEnt))
	{
		return true;
	}

	return false;
}

// True if the team index (0 is for solo) has a nemesis
AUTO_EXPR_FUNC(player) ACMD_NAME(TeamIndexLoadedOrNot);
bool Nemesis_TeamIndexLoadedOrNot(ExprContext* context, S32 iIndex)
{
	Entity* pPlayerEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	return Nemesis_TeamIndexLoadedOrNotByEnt(pPlayerEnt, iIndex);
}

static Entity *Nemesis_GetFirstPlayer(ACMD_EXPR_ENTARRAY_IN playersIn)
{
	Entity *pEntity;
	if(eaSize(playersIn) < 1)
	{
		return NULL;
	}

	pEntity = (*playersIn)[0];

	if(!pEntity || !pEntity->pPlayer)
	{
		return NULL;
	}

	return pEntity;
}

// True if the team index (0 is for solo) has a nemesis
AUTO_EXPR_FUNC(player, encounter, encounter_action, ai) ACMD_NAME(NemesisTeamIndexLoadedOrNotEntArray);
bool Nemesis_TeamIndexLoadedOrNotEntArray(ACMD_EXPR_ENTARRAY_IN playersIn, S32 iIndex)
{
	Entity *pEntity = Nemesis_GetFirstPlayer(playersIn);

	return Nemesis_TeamIndexLoadedOrNotByEnt(pEntity, iIndex);
}

// True if the team index (0 is for solo) has a nemesis
AUTO_EXPR_FUNC(player, encounter, encounter_action, ai) ACMD_NAME(NemesisTeamIndexLoadedEntArray);
bool Nemesis_TeamIndexLoadedEntArray(ACMD_EXPR_ENTARRAY_IN playersIn, S32 iIndex)
{
	Entity *pEntity = Nemesis_GetFirstPlayer(playersIn);

	return Nemesis_TeamIndexLoadedByEnt(pEntity, iIndex);
}

// True if the team leader (or with optional flag any team member) has a nemesis
AUTO_EXPR_FUNC(player, encounter, encounter_action, ai) ACMD_NAME(NemesisLeaderLoadedOrNotEntArray);
bool Nemesis_LeaderLoadedOrNotEntArray(ACMD_EXPR_ENTARRAY_IN playersIn, bool bAnyTeamMember)
{
	Entity *pEntity = Nemesis_GetFirstPlayer(playersIn);

	return Nemesis_LeaderLoadedOrNotByEnt(pEntity, bAnyTeamMember);
}

// True if the team leader (or with optional flag any team member) has nemesis loaded
AUTO_EXPR_FUNC(player, encounter, encounter_action, ai) ACMD_NAME(NemesisLeaderLoadedEntArray);
bool Nemesis_LeaderLoadedEntArray(ACMD_EXPR_ENTARRAY_IN playersIn, bool bAnyTeamMember)
{
	Entity *pEntity = Nemesis_GetFirstPlayer(playersIn);

	return Nemesis_LeaderLoadedByEnt(pEntity, bAnyTeamMember);
}

AUTO_EXPR_FUNC(player, encounter, encounter_action, ai) ACMD_NAME(NemesisTeamIndexGetPlayer);
void Nemesis_TeamIndexGetPlayer(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT playerOut, S32 iIndex)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	eaClear(playerOut);
	if(pState && iIndex >= 0 && iIndex < eaSize(&pState->nemesisInfo.eaNemesisTeam) && pState->nemesisInfo.eaNemesisTeam[iIndex]->iId > 0)
	{
		Entity *pEnt = entFromContainerID(pState->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pState->nemesisInfo.eaNemesisTeam[iIndex]->iId);
		if(pEnt)
		{
			eaPush(playerOut, pEnt);
		}
	}
}

AUTO_EXPR_FUNC(player, encounter, encounter_action, ai) ACMD_NAME(NemesisTeamIndexGetLeader, NemesisTeamGetLeader);
void Nemesis_TeamGetLeader(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT playerOut)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	eaClear(playerOut);
	if(pState && pState->nemesisInfo.bLeaderSet)
	{
		if((S32)pState->nemesisInfo.iLeaderIdx < eaSize(&pState->nemesisInfo.eaNemesisTeam))
		{
			Entity *pEnt = entFromContainerID(pState->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pState->nemesisInfo.eaNemesisTeam[pState->nemesisInfo.iLeaderIdx]->iId);
			if(pEnt)
			{
				eaPush(playerOut, pEnt);
			}
		}
	}
}

// Get the nemesis for the team at index. This only works if the team index has already been set by other expressions.
AUTO_EXPR_FUNC(player, encounter, encounter_action, ai) ACMD_NAME(NemesisTeamIndexGetNemesis);
SA_RET_OP_VALID Entity* Nemesis_TeamIndexGetNemesis(ACMD_EXPR_PARTITION iPartitionIdx, S32 iIndex)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	if(pState && iIndex >= 0 && iIndex < eaSize(&pState->nemesisInfo.eaNemesisTeam) && pState->nemesisInfo.eaNemesisTeam[iIndex]->iId > 0)
	{
		Entity *pEnt = entFromContainerID(pState->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pState->nemesisInfo.eaNemesisTeam[iIndex]->iId);
		if(pEnt)
		{
			if(pEnt)
			{
				return player_GetPrimaryNemesis(pEnt);
			}
		}
	}

	return NULL;
}

// Get the nemesis for the team leader. This only works if the team leader has already been set by other commands.
AUTO_EXPR_FUNC(player, encounter, encounter_action, ai) ACMD_NAME(NemesisTeamLeaderGetNemesis);
SA_RET_OP_VALID Entity* Nemesis_TeamLeaderGetNemesis(ACMD_EXPR_PARTITION iPartitionIdx)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	if(pState && pState->nemesisInfo.bLeaderSet && (S32)pState->nemesisInfo.iLeaderIdx < eaSize(&pState->nemesisInfo.eaNemesisTeam))
	{
		Entity *pEnt = entFromContainerID(pState->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pState->nemesisInfo.eaNemesisTeam[pState->nemesisInfo.iLeaderIdx]->iId);
		if(pEnt)
		{
			if(pEnt)
			{
				return player_GetPrimaryNemesis(pEnt);
			}
		}
	}

	return NULL;
}

// Get the nemesis motivation, used for either team index or leader
AUTO_EXPR_FUNC(player, encounter, encounter_action, ai) ACMD_NAME(NemesisTeamGetMotivation);
S32 Nemesis_TeamGetMotivation(ACMD_EXPR_PARTITION iPartitionIdx, S32 iTeamIndex, bool bUseLeader)
{
	const NemesisTeamStruct *pNemTeam = Nemesis_GetTeamStructAtIndex(iPartitionIdx, iTeamIndex, bUseLeader);

	if(pNemTeam)
	{
		return pNemTeam->motivation;
	}

	return 0;
}

// Get the nemesis personality, used for either team index or leader
AUTO_EXPR_FUNC(player, encounter, encounter_action, ai) ACMD_NAME(NemesisTeamGetPersonality);
S32 Nemesis_TeamGetPersonality(ACMD_EXPR_PARTITION iPartitionIdx, S32 iTeamIndex, bool bUseLeader)
{
	const NemesisTeamStruct *pNemTeam = Nemesis_GetTeamStructAtIndex(iPartitionIdx, iTeamIndex, bUseLeader);

	if(pNemTeam)
	{
		return pNemTeam->personality;
	}

	return 0;
}


