/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#define GENESIS_ALLOW_OLD_HEADERS 1

#include "encounter_common.h"
#include "entCritter.h"
#include "error.h"
#include "estring.h"
#include "Expression.h"
#include "GameEvent.h"
#include "GenesisMissions.h" // For genesis map level
#include "oldencounter_common.h"
#include "quat.h"
#include "StateMachine.h"
#include "stringcache.h"
#include "timing_profiler.h"
#include "wlEncounter.h"
#include "../../../libs/WorldLib/StaticWorld/WorldCellStreaming.h"
#include "../../../libs/WorldLib/StaticWorld/WorldGridPrivate.h" // For TomY ENCOUNTER_HACK
#include "worldgrid.h"
#include "worldlib.h"
#include "wlGenesis.h" // For genesis map level
#include "wlGenesisMissions.h" // For genesis map level

#include "AutoGen/encounter_enums_h_ast.h"
#include "AutoGen/oldencounter_common_h_ast.h"

#ifdef GAMESERVER
#include "gslOldEncounter.h"
#include "gslOldEncounter_events.h"
#include "gslPatrolRoute.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

#define ENCOUNTER_CHECK_HEIGHT_DIST 200

DictionaryHandle g_EncounterLayerDictionary = NULL;
DictionaryHandle g_StaticEncounterDictionary = NULL;

OldEncounterMasterLayer* g_EncounterMasterLayer = NULL;
char** g_EncounterLayersToIgnore = NULL;

static EncounterMapChangeCallback s_MapLoadCallback = NULL;
static EncounterMapChangeCallback s_MapUnloadCallback = NULL;

static U32 s_EncounterLoadingDisabled = 0;
AUTO_CMD_INT(s_EncounterLoadingDisabled, DisableEncounterLoading) ACMD_CMDLINE;

DictionaryHandle g_EncounterDictionary = NULL;


// ----------------------------------------------------------------------------------
// Encounter Def Dictionary Loading
// ----------------------------------------------------------------------------------

static void oldencounter_CalculateDefScope(EncounterDef* def)
{
	if (!def->scope && def->filename) {
		// Auto calculate scope
		char *start, *end;
		char tempScope[512];
		if ((start = strstri(def->filename, "defs/encounters/")) &&
			(end = strstri(def->filename, ".encounter")))
		{
			start += strlen("defs/encounters/");
			while (end >= start && *end != '/') {
				end--;
			}
			if (end > start) {
				strncpy(tempScope, start, end - start);
				def->scope = (char*)allocAddString(tempScope);
			}
		}			
	}
}


static void oldencounter_ValidateDef(EncounterDef *def)
{
	const char *pchTempFileName;
	int i;

	if ( !resIsValidName(def->name) ) {
		ErrorFilenamef( def->filename, "Encounter Def name is illegal: '%s'", def->name );
	}

	if ( !resIsValidScope(def->scope) ) {
		ErrorFilenamef( def->filename, "Encounter Def scope is illegal: '%s'", def->scope );
	}

	pchTempFileName = def->filename;
	if (resFixPooledFilename(&pchTempFileName, "defs/encounters", def->scope, def->name, "encounter")) {
		ErrorFilenamef( def->filename, "Encounter Def filename does not match name '%s' scope '%s'", def->name, def->scope);
	}

	if (IsServer()) {
	//	if (!GET_REF(def->critterGroup) && REF_STRING_FROM_HANDLE(def->critterGroup)) {
	//		ErrorFilenamef( def->filename, "Encounter Def %s refers to non-existent critter group '%s'", def->name, REF_STRING_FROM_HANDLE(def->critterGroup));
	//	}
	//	if (!GET_REF(def->faction) && REF_STRING_FROM_HANDLE(def->faction)) {
	//		ErrorFilenamef( def->filename, "Encounter Def %s refers to non-existent critter faction '%s'", def->name, REF_STRING_FROM_HANDLE(def->faction));
	//	}

		for(i=eaSize(&def->actors)-1; i>=0; --i) {
			OldActor *pActor = def->actors[i];

	//		if (!GET_REF(pActor->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(pActor->displayNameMsg.hMessage)) {
	//			ErrorFilenamef( def->filename, "Encounter Def %s refers to non-existent message '%s'", def->name, REF_STRING_FROM_HANDLE(pActor->displayNameMsg.hMessage));
	//		}

			if (pActor->details.info) {
				if (pActor->details.info->pcCritterRank && !critterRankExists(pActor->details.info->pcCritterRank)) {
					ErrorFilenamef( def->filename, "Encounter Def %s refers to non-existent critter rank '%s'", def->name, pActor->details.info->pcCritterRank);
				}
				if (pActor->details.info->pcCritterSubRank && !critterSubRankExists(pActor->details.info->pcCritterSubRank)) {
					ErrorFilenamef( def->filename, "Encounter Def %s refers to non-existent critter sub-rank '%s'", def->name, pActor->details.info->pcCritterSubRank);
				}
	//			if (!GET_REF(pActor->details.info->critterDef) && REF_STRING_FROM_HANDLE(pActor->details.info->critterDef)) {
	//				ErrorFilenamef( def->filename, "Encounter Def %s refers to non-existent critter def '%s'", def->name, REF_STRING_FROM_HANDLE(pActor->details.info->critterDef));
	//			}
	//			if (!GET_REF(pActor->details.info->critterGroup) && REF_STRING_FROM_HANDLE(pActor->details.info->critterGroup)) {
	//				ErrorFilenamef( def->filename, "Encounter Def %s refers to non-existent critter group '%s'", def->name, REF_STRING_FROM_HANDLE(pActor->details.info->critterGroup));
	//			}
	//			if (!GET_REF(pActor->details.info->critterFaction) && REF_STRING_FROM_HANDLE(pActor->details.info->critterFaction)) {
	//				ErrorFilenamef( def->filename, "Encounter Def %s refers to non-existent critter faction '%s'", def->name, REF_STRING_FROM_HANDLE(pActor->details.info->critterFaction));
	//			}
	//			if (!GET_REF(pActor->details.info->contactScript) && REF_STRING_FROM_HANDLE(pActor->details.info->contactScript)) {
	//				ErrorFilenamef( def->filename, "Encounter Def %s refers to non-existent contact def '%s'", def->name, REF_STRING_FROM_HANDLE(pActor->details.info->contactScript));
	//			}
			}
			if (pActor->details.aiInfo) {
	//			if (!GET_REF(pActor->details.aiInfo->hFSM) && REF_STRING_FROM_HANDLE(pActor->details.aiInfo->hFSM)) {
	//				ErrorFilenamef( def->filename, "Encounter Def %s refers to non-existent FSM '%s'", def->name, REF_STRING_FROM_HANDLE(pActor->details.aiInfo->hFSM));
	//			}
			}
		}
	}
}


static void oldencounter_DefResourceCallBack(enumResourceEventType eType, const char *pDictName, const char *pDefName, EncounterDef *pDef, void *pUserData)
{
#ifdef GAMESERVER
	switch (eType)
	{
	case RESEVENT_RESOURCE_ADDED:
	case RESEVENT_RESOURCE_MODIFIED:
		oldencounter_StartTrackingForName(pDefName);
		break;

	case RESEVENT_RESOURCE_REMOVED:
	case RESEVENT_RESOURCE_PRE_MODIFIED:
		oldencounter_StopTrackingForName(pDefName);
		break;
	}
#endif
}


static int oldencounter_ValidateDefCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, EncounterDef *pEncounterDef, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
		oldencounter_CalculateDefScope(pEncounterDef);
#ifdef GAMESERVER
		// Post-processing uses functions from other libraries that are only available on the server
		oldencounter_DefFixupPostProcess(pEncounterDef);
#endif
		oldencounter_InitFSMVarMessages(pEncounterDef, NULL);
		oldencounter_ValidateDef(pEncounterDef);
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_FIX_FILENAME:
		oldencounter_CalculateDefScope(pEncounterDef);
		resFixPooledFilename(&pEncounterDef->filename, "defs/encounters", pEncounterDef->scope, pEncounterDef->name, "encounter");
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}


AUTO_RUN;
int oldencounter_RegisterEncounterDefDictionary(void)
{
	g_EncounterDictionary = RefSystem_RegisterSelfDefiningDictionary("EncounterDef", false, parse_EncounterDef, true, true, NULL);

	resDictManageValidation(g_EncounterDictionary, oldencounter_ValidateDefCB);
	resDictSetDisplayName(g_EncounterDictionary, "Encounter", "Encounters", RESCATEGORY_DESIGN);

	if (IsServer()) {
		resDictRegisterEventCallback(g_EncounterDictionary, oldencounter_DefResourceCallBack, NULL);
		resDictProvideMissingResources(g_EncounterDictionary);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_EncounterDictionary, ".name", ".scope", NULL, NULL, NULL);
		}
	} else {
		resDictRequestMissingResources(g_EncounterDictionary, 8, false, resClientRequestSendReferentCommand);
	}
	resDictProvideMissingRequiresEditMode(g_EncounterDictionary);

	return 1;
}


int oldencounter_LoadEncounterDefs(void)
{
	static int loadedOnce = false;
	DictionaryEArrayStruct *dictionaryStruct = NULL;

	if (IsServer()) {
		if (loadedOnce) {
			return 1;
		}

		resLoadResourcesFromDisk(g_EncounterDictionary, "defs/encounters", ".encounter", NULL,  PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);

		loadedOnce = true;
	}
	return 1;
}


EncounterDef* oldencounter_DefFromName(const char* encounterName)
{
	return (EncounterDef*)RefSystem_ReferentFromString(g_EncounterDictionary, encounterName);
}


// ----------------------------------------------------------------------------------
// Fixup Functions
// ----------------------------------------------------------------------------------

// NOTE: There is a fixup function for EncounterDef that ONLY runs on the SERVER
//       defined in "encounter.c"


// ----------------------------------------------------------------------------------
// Encounter Def Actor Utilities
// ----------------------------------------------------------------------------------

OldActor* oldencounter_FindDefActorByID(EncounterDef* def, int uniqueID, bool remove)
{
	int i, n = eaSize(&def->actors);
	for (i = 0; i < n; i++) {
		OldActor* actor = def->actors[i];
		if (actor->uniqueID == uniqueID) {
			if (remove) {
				eaRemove(&def->actors, i);
			}
			return actor;
		}
	}
	return NULL;
}


OldNamedPointInEncounter* oldencounter_FindDefPointByID(EncounterDef* def, int uniqueID, bool remove)
{
	int i, n = eaSize(&def->namedPoints);
	for (i = 0; i < n; i++) {
		OldNamedPointInEncounter* point = def->namedPoints[i];
		if (point->id == uniqueID) {
			if (remove) {
				eaRemove(&def->namedPoints, i);
			}
			return point;
		}
	}
	return NULL;
}


OldActor* oldencounter_FindDefActorByName(EncounterDef* def, const char* name)
{
	int i, n = eaSize(&def->actors);
	for (i = 0; i < n; i++) {
		OldActor* actor = def->actors[i];
		if (name && actor->name && !stricmp(name, actor->name)) {
			return actor;
		}
	}
	return NULL;
}


OldEncounterAction* oldencounter_GetDefFirstAction(EncounterDef* def, EncounterState state)
{
	int i, size = eaSize(&def->actions);
	for(i = 0; i < size; ++i) {
		if (def->actions[i]->state == state) {
			return def->actions[i];
		}
	}
	return NULL;
}


static bool oldencounter_TryCleanupActorScaling(SA_PARAM_NN_VALID OldActor* actor, SA_PARAM_OP_VALID const OldActor* baseActor)
{
	bool retVal = false;
	OldActorScaling* scaling = &actor->details;
	OldActorInfo* nextInfo;
	OldActorAIInfo* nextAIInfo;

	// Check whether each entry is the same as the one for a team one player smaller.  If so, it should inherit from the smaller team instead of holding duplicate information

	// If there's an info, find out which info it would otherwise inherit from
	if (scaling->info) {
		nextInfo = NULL;
		if (baseActor && baseActor->details.info) {
			nextInfo = baseActor->details.info;
		}

		// See if these two infos are the same.  If so, free the current info so that it inherits its data instead
		if (0 == StructCompare(parse_OldActorInfo, scaling->info, nextInfo, 0, 0, 0)) {
			StructDestroy(parse_OldActorInfo, scaling->info);
			scaling->info = NULL;
			retVal = true;
		}
	}

	// If there's an AI info, find out which info it would otherwise inherit from
	if (scaling->aiInfo) {
		nextAIInfo = NULL;
		if (baseActor && baseActor->details.aiInfo) {
			nextAIInfo = baseActor->details.aiInfo;
		}

		// See if these two infos are the same.  If so, free the current info so that it inherits its data instead
		if (0 == StructCompare(parse_OldActorAIInfo, scaling->aiInfo, nextAIInfo, 0, 0, 0)) {
			StructDestroy(parse_OldActorAIInfo, scaling->aiInfo);
			scaling->aiInfo = NULL;
			retVal = true;
		}
	}

	// Don't bother checking the position for now.  A position will never end up being returned to exactly where it was, since floats are involved.
/*
	// Check the position.  Positions are additive, so we should only delete it if it doesn't change the position at all
	if(scaling->position) {
		if(sameVec3(scaling->position->posOffset, zerovec3) && sameQuat(scaling->position->rotQuat, unitquat)) {
			StructDestroy(parse_OldActorPosition, scaling->position);
			scaling->position = NULL;
		}
	}
*/

	return retVal;
}


bool oldencounter_ActorIsEmpty(OldActor* actor)
{
	// Check whether the actor is holding any information
	if (actor->name || actor->uniqueID < 0) {
		return false;
	}
	if (actor->disableSpawn != ActorScalingFlag_Inherited || actor->useBossBar != ActorScalingFlag_Inherited) {
		return false;
	}

	if (actor->details.info || actor->details.aiInfo || actor->details.position) {
		return false;
	}
	return true;
}


bool oldencounter_TryCleanupActors(OldActor*** baseDefActors, OldActor*** overrideActors)
{
	int i, n, j, k;
	OldActor* actor;
	OldActor* baseActor;
	bool retVal = false;

	// First clean up any inheritance issues within the actors' scaling info arrays
	if (baseDefActors) {
		n = eaSize(baseDefActors);
	} else {
		n = 0;
	}

	for(i=0; i<n; i++) {
		actor = (*baseDefActors)[i];
		retVal |= oldencounter_TryCleanupActorScaling(actor, NULL);

		// Actors created before AI info existed have no AIinfo to inherit from.  Add an empty one if it's missing.
		if (NULL == actor->details.aiInfo) {
			actor->details.aiInfo = StructCreate(parse_OldActorAIInfo);
			retVal = true;
		}
	}

	k = eaSize(overrideActors);
	for(j=0; j<k; j++) {
		actor = (*overrideActors)[j];
		baseActor = NULL;
		
		if (!actor) {
			continue;
		}

		// Try to find the corresponding baseDefActor, if there is one
		for(i=0; i<n; i++) {
			if (actor->uniqueID == (*baseDefActors)[i]->uniqueID) {
				baseActor = (*baseDefActors)[i];
				break;
			}
		}
		retVal |= oldencounter_TryCleanupActorScaling(actor, baseActor);

		// If the name is instanced but the same as the base's, un-instance it
		if (baseActor && baseActor->name && actor->name && 0 == stricmp(actor->name, baseActor->name)) {
 			actor->name = NULL;
			retVal = true;
		}

		// Actors created before AI info existed have no AIinfo to inherit from.  Add an empty one if it's missing.
		if (NULL == baseActor && NULL == actor->details.aiInfo) {
			actor->details.aiInfo = StructCreate(parse_OldActorAIInfo);
			retVal = true;
		}

		if (oldencounter_ActorIsEmpty(actor)) {
			StructDestroy(parse_OldActor, actor);
			(*overrideActors)[j] = NULL;
			retVal = true;
		}
	}

	for(j=k-1; j>=0; j--) {
		// Remove any NULL actors.  Don't need to update retVal here; it should have been set when we NULL'ed the actor
		if(*overrideActors && NULL == (*overrideActors)[j]) {
			eaRemove(overrideActors, j);
		}
	}

	return retVal;
}


// Check for overrides first, and use the previous override before using the base
void oldencounter_GetActorPositionOffset(OldActor* actor, Quat outQuat, Vec3 outPos)
{
	OldActorPosition* basePos = actor->details.position;
	
	PERFINFO_AUTO_START("oldencounter_GetActorPositionOffset",1);
	assertmsg(basePos, "This is should never be NULL, if this happened something is terribly wrong");
	copyQuat(basePos->rotQuat, outQuat);
	copyVec3(basePos->posOffset, outPos);
	PERFINFO_AUTO_STOP();
}


OldActorInfo* oldencounter_GetActorInfo(OldActor* actor)
{
	if (actor->details.info) {
		return actor->details.info;
	} else {
		assertmsg(0, "This is should never be NULL, if this happened something is terribly wrong");
	}
}


OldActorAIInfo* oldencounter_GetActorAIInfo(OldActor* actor)
{
	if (actor->details.aiInfo) {
		return actor->details.aiInfo;
	}

	// If the actor has no AI info, create some.  This leaks memory, so isn't a great long-term fix.
	Errorf("Actor has no AI info, probably because it's from a very old encounter.  Talk to James L.\n");
	return StructCreate(parse_OldActorAIInfo);
}


bool oldencounter_IsEnabledAtTeamSize(OldActor* actor, U32 teamSize)
{
	return !(actor->disableSpawn & (1 << teamSize));
}


// Calling this should no longer be necessary, since we now postprocess all Actors to have names
void oldencounter_GetActorName(OldActor* actor, char** dstStr)
{
	if (actor->name) {
		estrCopy2(dstStr, actor->name);
	} else {
		estrPrintf(dstStr, "Actor%i", actor->uniqueID);
	}
}


FSM* oldencounter_GetActorFSM(const OldActorInfo* actorInfo, const OldActorAIInfo* actorAIInfo)
{
	FSM* actorFSM = NULL;
	CritterDef* critterDef = actorInfo?GET_REF(actorInfo->critterDef):NULL;
	if (actorAIInfo && GET_REF(actorAIInfo->hFSM)) {
		actorFSM = GET_REF(actorAIInfo->hFSM);
	}
	if (!actorFSM && critterDef && GET_REF(critterDef->hFSM)) {
		actorFSM = GET_REF(critterDef->hFSM);
	}
	if (!actorFSM) {
		actorFSM = (FSM*)RefSystem_ReferentFromString(gFSMDict, "Combat");
	}
	return actorFSM;
}


const char* oldencounter_GetFSMName(const OldActorInfo* actorInfo, const OldActorAIInfo* actorAIInfo)
{
	const char* actorFSM = NULL;
	CritterDef* critterDef = actorInfo?GET_REF(actorInfo->critterDef):NULL;
	if (actorAIInfo) {
		actorFSM = REF_STRING_FROM_HANDLE(actorAIInfo->hFSM);
	}
	if (!actorFSM && critterDef) {
		actorFSM = REF_STRING_FROM_HANDLE(critterDef->hFSM);
	}
	if (!actorFSM) {
		actorFSM = "Combat";
	}
	return actorFSM;
}


bool oldencounter_ShouldUseBossBar(OldActor* actor, U32 teamSize)
{
	return (actor->useBossBar & (1 << teamSize));
}


OldEncounterVariable* oldencounter_LookupActorVariable(OldActorAIInfo* actorAIInfo, const char* variableName)
{
	int i, n = eaSize(&actorAIInfo->actorVars);
	for (i = 0; i < n; i++) {
		OldEncounterVariable* actorVar = actorAIInfo->actorVars[i];
		if (actorVar->varName && !stricmp(actorVar->varName, variableName)) {
			return actorVar;
		}
	}
	return NULL;
}


void oldencounter_GetAllActorVariables(OldActorAIInfo *actorAIInfo, OldEncounterVariable ***peaVars)
{
	int i, n = eaSize(&actorAIInfo->actorVars);
	for (i = 0; i < n; i++) {
		OldEncounterVariable* actorVar = actorAIInfo->actorVars[i];
		if (actorVar->varName) {
			eaPush(peaVars, actorVar);
		}
	}
}


bool oldencounter_HasInteractionProperties(OldActor *pActor)
{
	if (pActor && pActor->details.info && (
		GET_REF(pActor->details.info->contactScript) || 
		pActor->details.info->oldActorInteractProps.interactCond ||
		pActor->details.info->oldActorInteractProps.interactSuccessCond ||
		GET_REF(pActor->details.info->oldActorInteractProps.interactText.hMessage))) {
		return true;
	}
	return false;
}


bool oldencounter_IsDefDynamicSpawn(EncounterDef *pDef)
{
	if (pDef->eDynamicSpawnType == WorldEncounterDynamicSpawnType_Dynamic
		|| (pDef->eDynamicSpawnType == WorldEncounterDynamicSpawnType_Default && (zmapInfoGetMapType(NULL) == ZMTYPE_STATIC || zmapInfoGetMapType(NULL) == ZMTYPE_SHARED))){
			return true;
	}
	return false;
}


// ----------------------------------------------------------------------------------
// Encounter Def Display Message Utilities
// ----------------------------------------------------------------------------------

Message *oldencounter_CreateDisplayNameMessageForEncDefActor(EncounterDef *def, OldActor *actor)
{
	Message *message;
	char *estrKeyStr = NULL;
	char *estrScope = NULL;
	char *estrActorName = NULL;
	char filename[MAX_PATH];
	estrStackCreate(&estrKeyStr);
	estrStackCreate(&estrActorName);
	estrStackCreate(&estrScope);

	oldencounter_GetActorName(actor, &estrActorName);
	getFileNameNoExt(filename, def->filename);

	estrPrintf(&estrKeyStr, "EncounterDef.%s.%s.displayName", def->name, estrActorName);
	if (strncmp(filename,"defs/encounters/",16) == 0) {
		estrPrintf(&estrScope, "EncounterDef/%s", def->filename+16);
	} else {
		estrPrintf(&estrScope, "EncounterDef/%s", def->filename);
	}

	message = langCreateMessage(estrKeyStr, "Critter Display Name override", estrScope, NULL);
	
	estrDestroy(&estrKeyStr);
	estrDestroy(&estrActorName);
	estrDestroy(&estrScope);
	
	return message;
}


Message *oldencounter_CreateVarMessageForEncounterDefActor(EncounterDef* def, OldActor* actor, const char *varName, const char *fsmName)
{
	Message *message;
	char *estrKeyStr = NULL;
	char *estrScope = NULL;
	char *estrActorName = NULL;
	char *estrDescription = NULL;
	char filename[MAX_PATH];
	estrStackCreate(&estrKeyStr);
	estrStackCreate(&estrActorName);
	estrStackCreate(&estrScope);
	estrStackCreate(&estrDescription);

	oldencounter_GetActorName(actor, &estrActorName);
	getFileNameNoExt(filename, def->filename);

	estrPrintf(&estrKeyStr, "EncounterDef.%s.%s.fsmvars.%s", def->name, estrActorName, varName);
	if (strncmp(filename,"defs/encounters/",16) == 0) {
		estrPrintf(&estrScope, "EncounterDef/%s", def->filename+16);
	} else {
		estrPrintf(&estrScope, "EncounterDef/%s", def->filename);
	}

	estrPrintf(&estrDescription, "Value for the \"%s\" extern var in FSM \"%s\"", varName, fsmName);
	message = langCreateMessage(estrKeyStr, estrDescription, estrScope, NULL);
	
	estrDestroy(&estrKeyStr);
	estrDestroy(&estrActorName);
	estrDestroy(&estrScope);
	estrDestroy(&estrDescription);
	
	return message;
}


void oldencounter_InitFSMVarMessages(EncounterDef *def, EncounterDef *baseDef)
{
	if (def) {
		int i, n = eaSize(&def->actors);
		for (i = 0; i < n; i++) {
			OldActor *actor = def->actors[i];
			OldActor *baseDefActor = baseDef ? oldencounter_FindDefActorByID(baseDef, actor->uniqueID, false) : NULL;
			OldActorInfo *actorInfo = NULL;

			OldActorAIInfo *aiInfo = actor->details.aiInfo;

			if (actor->details.info) {
				actorInfo = actor->details.info;
			} else if (baseDefActor && baseDefActor->details.info) {
				actorInfo = baseDefActor->details.info;
			}

			if (aiInfo) {
				int iVar, numVars = eaSize(&aiInfo->actorVars);
				for (iVar = numVars-1; iVar >= 0; --iVar) {
					OldEncounterVariable *var = aiInfo->actorVars[iVar];
					FSM *fsm = oldencounter_GetActorFSM(actorInfo, aiInfo);
					FSMExternVar *externVar = (fsm&&var&&var->varName)?fsmExternVarFromName(fsm, var->varName, "encounter"):0;
					if (externVar && externVar->scType && (stricmp(externVar->scType, "message") == 0)) {
#ifndef NO_EDITORS
						SET_HANDLE_FROM_STRING(gMessageDict, MultiValGetString(&var->varValue, NULL), var->message.hMessage);
#endif
					}
				}
			}
		}
	}
}


// ----------------------------------------------------------------------------------
// Encounter Layer Fixup
// ----------------------------------------------------------------------------------

static void oldencounter_InitLayerFSMVarMessages(EncounterLayer* encLayer)
{
	int i, n;

	// Create references to all extern var messages
	n = eaSize(&encLayer->staticEncounters);
	for (i = 0; i < n; i++) {
		if (encLayer->staticEncounters[i]->defOverride)
			oldencounter_InitFSMVarMessages(encLayer->staticEncounters[i]->defOverride, GET_REF(encLayer->staticEncounters[i]->baseDef));
	}
}


static void oldencounter_CheckLayerStaticEncounterHeights(int iPartitionIdx, EncounterLayer* encLayer)
{
	// Flag any static encounters whose distance to the ground has changed
	int i, numEncounters = eaSize(&encLayer->staticEncounters);
	for (i = 0; i < numEncounters; i++) {
		OldStaticEncounter* staticEnc = encLayer->staticEncounters[i];
		S32 foundFloor = false;
		F32 distToGround = oldencounter_GetStaticEncounterHeight(iPartitionIdx, staticEnc, &foundFloor);

		// Check the absolute difference (since this is floating-point math)
		// If the floor wasn't found, it's probably because geometry wasn't loaded for that region
		if( foundFloor && ABS(staticEnc->distToGround - distToGround) > 2) {
			staticEnc->bDistToGroundChanged = true;
		} else {
			staticEnc->bDistToGroundChanged = false;
		}
	}
}


static void oldencounter_UpdateLayerSpawnRules(EncounterLayer* encLayer)
{
	int i, numEncs;

	// Update the spawn rule for all loaded static encounters
	numEncs = eaSize(&encLayer->staticEncounters);
	for (i = 0; i < numEncs; i++) {
		oldencounter_UpdateStaticEncounterSpawnRule(encLayer->staticEncounters[i], encLayer);

#ifdef GAMESERVER
		// Post-processing uses functions from other libraries that are only available on the server
		oldencounter_DefFixupPostProcess(encLayer->staticEncounters[i]->spawnRule);
#endif
	}
}


void oldencounter_FixLayerBackPointers(EncounterLayer* encLayer, OldStaticEncounterGroup* staticEncGroup, OldStaticEncounterGroup* parentGroup)
{
	int i, n;

	staticEncGroup->parentGroup = parentGroup;

	// Setup the back pointer for all static encounters that are contained in the group
	n = eaSize(&staticEncGroup->staticEncList);
	for (i = 0; i < n; i++) {
		OldStaticEncounter* staticEnc = staticEncGroup->staticEncList[i];
		staticEnc->groupOwner = staticEncGroup;
		staticEnc->layerParent = encLayer;
	}

	// Recursively do the same for all child groups
	n = eaSize(&staticEncGroup->childList);
	for (i = 0; i < n; i++) {
		oldencounter_FixLayerBackPointers(encLayer, staticEncGroup->childList[i], staticEncGroup);
	}
}


// Encounters are stored on the layer in the static encounter group, but all of the functions look at the staticEncList (likewise for clickables)
static void oldencounter_PopulateStaticEncounterLists(EncounterLayer* encLayer, OldStaticEncounterGroup* staticEncGroup)
{
	int i, n;

	// Add the encounters and clickables in this group to the lists on the Layers
	// (this extra list is maintained for compatibility with old code)
	eaPushEArray(&encLayer->staticEncounters, &staticEncGroup->staticEncList);

	// Recursively do the same for all child groups
	n = eaSize(&staticEncGroup->childList);
	for (i = 0; i < n; i++) {
		oldencounter_PopulateStaticEncounterLists(encLayer, staticEncGroup->childList[i]);
	}
}


// ----------------------------------------------------------------------------------
// Encounter Layer Dictionary
// ----------------------------------------------------------------------------------

static int oldencounter_ValidateLayerCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, EncounterLayer *pEncLayer, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_POST_BINNING:
		oldencounter_PopulateStaticEncounterLists(pEncLayer, &pEncLayer->rootGroup);

		// These functions could be done pre-binning, except that the lists of encounters are NO_AST
		oldencounter_UpdateLayerSpawnRules(pEncLayer);
		oldencounter_InitLayerFSMVarMessages(pEncLayer);

		// Check whether encounters have had the ground moved from under them.  Should be editor only?
		oldencounter_CheckLayerStaticEncounterHeights(worldGetAnyCollPartitionIdx(), pEncLayer);
		return VALIDATE_HANDLED;

		xcase RESVALIDATE_FINAL_LOCATION:
		oldencounter_FixLayerBackPointers(pEncLayer, &pEncLayer->rootGroup, NULL);
		return VALIDATE_HANDLED;

//	xcase RESVALIDATE_FIX_FILENAME:
//		resFixPooledFilename();
	}
	return VALIDATE_NOT_HANDLED;
}


AUTO_RUN;
void oldencounter_RegisterEncounterLayerDictionary(void)
{
	static bool bLoaded = false;

	if (gConf.bAllowOldEncounterData && !bLoaded) {
		bLoaded = true;
		g_EncounterLayerDictionary = RefSystem_RegisterSelfDefiningDictionary("EncounterLayer", false, parse_EncounterLayer, true, true, NULL);
		g_StaticEncounterDictionary = RefSystem_RegisterSelfDefiningDictionary("StaticEncounter", false, parse_OldStaticEncounter, true, false, NULL);

		resDictManageValidation(g_EncounterLayerDictionary, oldencounter_ValidateLayerCB);
		// The static encounter dictionary is used to allow fast lookups by name; static encounter fixup/validation
		// should happen when the encounter layer is loaded, in oldencounter_ValidateLayerCB.
		resDictManageValidation(g_StaticEncounterDictionary, NULL);

		if (IsServer()) {
			resDictProvideMissingResources(g_EncounterLayerDictionary);
			if (isDevelopmentMode() || isProductionEditMode()) {
				resDictMaintainInfoIndex(g_EncounterLayerDictionary, ".name", NULL, NULL, NULL, NULL);	
			}
		} else {
			resDictRequestMissingResources(g_EncounterLayerDictionary, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		}
		resDictProvideMissingRequiresEditMode(g_EncounterLayerDictionary);
	}
}


// This loads all layers from the specified map into the dictionary.
static void oldencounter_LoadLayersToDictionary(const char* mapFileName)
{
	if (IsServer()) {
		char layerBinName[MAX_PATH];
		char relFileName[MAX_PATH];	
		BinFileListWithCRCs *deps_list;
		bool found = false;
		fileRelativePath(mapFileName, relFileName);
		strcpy(layerBinName, relFileName);
		strcat(layerBinName, ".encounters");
		getDirectoryName(relFileName);

		// Clear the dictionary
		// (If we ever have layers from other maps in the dictionary, we'll have to rethink this a bit)
		RefSystem_ClearDictionary(g_EncounterLayerDictionary, false);

		resLoadResourcesFromDisk(g_EncounterLayerDictionary, relFileName, ".encounterlayer", worldMakeBinName(layerBinName),  PARSER_OPTIONALFLAG);

		worldSetEncounterLayerCRC(ParseTableCRC(parse_EncounterLayer, NULL, 0)); // Called at least once (for EmptyMap) before we do fast bin dependency checking

		deps_list = zmapGetExternalDepsList(NULL);
		FOR_EACH_IN_REFDICT(g_EncounterLayerDictionary, EncounterLayer, layer) {
			bflAddDepsSourceFile(deps_list, layer->pchFilename);
			found = true;
		} FOR_EACH_END;
		bflSetDepsEncounterLayerCRC(deps_list, found ? ParseTableCRC(parse_EncounterLayer, NULL, 0) : 0);

	} else {
		// Request all layers from the server
		resRequestAllResourcesInDictionary(g_EncounterLayerDictionary);
	}
}


// ----------------------------------------------------------------------------------
// Static Encounter Dictionary
// ----------------------------------------------------------------------------------

void oldencounter_RemoveStaticEncounterReference(OldStaticEncounter* staticEnc)
{
	RefSystem_RemoveReferent(staticEnc, false);
}


void oldencounter_AddStaticEncounterReference(OldStaticEncounter* staticEnc)
{
	OldStaticEncounter* existingEnc;
	if (!staticEnc->name) {
		ErrorFilenameGroupf(staticEnc->pchFilename, "Design", 2, "All static encounters must have names.");
	} else if ((existingEnc = RefSystem_ReferentFromString(g_StaticEncounterDictionary, staticEnc->name))) {
		ErrorFilenameDup(existingEnc->pchFilename, staticEnc->pchFilename, staticEnc->name, "Static Encounter");
	} else {
		RefSystem_AddReferent(g_StaticEncounterDictionary, staticEnc->name, staticEnc);
	}
}


OldStaticEncounter* oldencounter_StaticEncounterFromName(const char* name)
{
	return (OldStaticEncounter*)RefSystem_ReferentFromString(g_StaticEncounterDictionary, name);
}


// ----------------------------------------------------------------------------------
// Constructor Functions
// ----------------------------------------------------------------------------------

// Allocates a clones of the Encounter Layer
EncounterLayer* oldencounter_SafeCloneLayer(const EncounterLayer *pEncounterLayer)
{
	EncounterLayer *pNewLayer = StructCloneFields(parse_EncounterLayer, pEncounterLayer);

	if (pNewLayer) {
		oldencounter_PopulateStaticEncounterLists(pNewLayer, &pNewLayer->rootGroup);
		oldencounter_UpdateLayerSpawnRules(pNewLayer);
		oldencounter_FixLayerBackPointers(pNewLayer, &pNewLayer->rootGroup, NULL);
	}
	
	return pNewLayer;
}


// Copies all data to the target Encounter Layer
void oldencounter_SafeLayerCopyAll(const EncounterLayer* pSource, EncounterLayer *pDest)
{
	if (pSource && pDest) {
		StructReset(parse_EncounterLayer, pDest);
		StructCopyFields(parse_EncounterLayer, pSource, pDest, 0, 0);

		oldencounter_PopulateStaticEncounterLists(pDest, &pDest->rootGroup);
		oldencounter_UpdateLayerSpawnRules(pDest);
		oldencounter_FixLayerBackPointers(pDest, &pDest->rootGroup, NULL);
	}
}


// ----------------------------------------------------------------------------------
// Destructor Functions
// ----------------------------------------------------------------------------------

void oldencounter_FreeSpawnRule(OldStaticEncounter* staticEnc)
{
	if (staticEnc->spawnRule) {
		StructDestroy(parse_EncounterDef, staticEnc->spawnRule);
	}
	staticEnc->spawnRule = NULL;
}


AUTO_FIXUPFUNC;
TextParserResult fixupStaticEncounter(OldStaticEncounter* staticEnc, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
		case FIXUPTYPE_DESTRUCTOR:
		{
			oldencounter_FreeSpawnRule(staticEnc);
			break;
		}
	}
	return 1;
}


static char* oldencounter_LayerNameFromFilename(const char *filename)
{
	if (filename) {
		char layerName[MAX_PATH];
		char *tmp;

		fileRelativePath(filename, layerName);
		for (tmp = layerName; tmp && *tmp; tmp++) {
			if (*tmp == '/') {
				*tmp = '.';
			}
		}

		return StructAllocString(layerName);
	}
	return NULL;
}


AUTO_FIXUPFUNC;
TextParserResult fixupEncounterLayer(EncounterLayer* encLayer, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
		case FIXUPTYPE_DESTRUCTOR:
		{
			eaDestroy(&encLayer->staticEncounters);
			break;
		}

		// This can't be moved to the dictionary callback because it needs to change the name that this object will use in the dictionary
		case FIXUPTYPE_POST_TEXT_READ:
		{
			// This should happen before the object is put in the dictionary
			encLayer->name = (char*)allocAddString(oldencounter_LayerNameFromFilename(encLayer->pchFilename));
			break;
		}
	}
	return 1;
}


// ----------------------------------------------------------------------------------
// Layer Ignore Utilities
// ----------------------------------------------------------------------------------

static bool oldencounter_LayerIsOnIgnoreList(const char* layerName)
{
	if (layerName) {
		int i, n = eaSize(&g_EncounterLayersToIgnore);
		char shortLayerName[1024];

		// The layer name passed in is the full file name; strip off the path and extension
		getFileNameNoExt_s(SAFESTR(shortLayerName), layerName);

		for(i=0; i<n; i++) {
			if(0 == stricmp(shortLayerName, g_EncounterLayersToIgnore[i])) {
				return true;
			}
		}
	}

	return false;
}


void oldencounter_IgnoreLayer(const char* layerName)
{
	if (layerName) {
		char* structName = StructAllocString(layerName);
		eaPush(&g_EncounterLayersToIgnore, structName);
	}
}


// Clear the list of ignored layers
void oldencounter_UnignoreAllLayers(void)
{
	eaDestroy(&g_EncounterLayersToIgnore);
}


void oldencounter_IgnoreAllLayersExcept(const char* layerName)
{
	// Reset the list of ignored layers
	oldencounter_UnignoreAllLayers();

	// Add every layer but this one
	if (layerName && g_EncounterMasterLayer) {
		int i, n = eaSize(&g_EncounterMasterLayer->encLayers);

		for(i=0; i<n; i++) {
			char currLayerName[1024];

			getFileNameNoExt_s(SAFESTR(currLayerName), g_EncounterMasterLayer->encLayers[i]->pchFilename);

			if (stricmp(currLayerName, layerName)) {
				char* structName = StructAllocString(currLayerName);
				eaPush(&g_EncounterLayersToIgnore, structName);
			}
		}
	}
}


// ----------------------------------------------------------------------------------
// Master Layer Utilities
// ----------------------------------------------------------------------------------

// Returns TRUE if the specified EncounterLayer belongs to the same map as the Master Layer
bool oldencounter_MatchesMasterLayer(const EncounterLayer *pEncounterLayer, const OldEncounterMasterLayer *pMasterLayer)
{
	char mapRelPath[MAX_PATH];
	char layerRelPath[MAX_PATH];

	if (pMasterLayer) {
		fileRelativePath(pMasterLayer->mapFileName, mapRelPath);
		getDirectoryName(mapRelPath);
	}

	if (pEncounterLayer) {
		fileRelativePath(pEncounterLayer->pchFilename, layerRelPath);
		getDirectoryName(layerRelPath);
	}

	if (pMasterLayer && pEncounterLayer && 0 == stricmp(mapRelPath, layerRelPath)) {
		return true;
	}

	{
		ResourceNameSpace *pNameSpace;
		ResourceNameSpaceIterator iterator;
		resNameSpaceInitIterator(&iterator);
		while (pNameSpace = resNameSpaceIteratorGetNext(&iterator)) {
			char userRelPath[MAX_PATH];

			sprintf(userRelPath, NAMESPACE_PATH"%s/%s", pNameSpace->pName, mapRelPath);
			if (pMasterLayer && pEncounterLayer && 0 == stricmp(layerRelPath, userRelPath)) {
				return true;
			}
		}
	}

	return false;
}


// This initializes and adds a copy of the specified encounter layer to the global master layer
void oldencounter_LoadToMasterLayer(const EncounterLayer *pEncounterLayer)
{
	// Add this layer to the Master Layer
	// Add a copy so that dictionary reloads won't affect the currently running map.
	if (pEncounterLayer && g_EncounterMasterLayer) {
		EncounterLayer *layerCopy = NULL;
		int i, n;

		// see if this map alreay exists in the masterlayer.  If so, we want to refresh it.
		n = eaSize(&g_EncounterMasterLayer->encLayers);
		for (i = 0; i < n; i++) {
			if (0 == stricmp(g_EncounterMasterLayer->encLayers[i]->pchFilename, pEncounterLayer->pchFilename)) {
				// Deinitialize the existing version and copy the new version to it
				bool visible = g_EncounterMasterLayer->encLayers[i]->visible;
				layerCopy = eaRemove(&g_EncounterMasterLayer->encLayers, i);
				eaForEach(&layerCopy->staticEncounters, oldencounter_RemoveStaticEncounterReference);
				oldencounter_SafeLayerCopyAll(pEncounterLayer, layerCopy);
				layerCopy->visible = visible;
				break;
			}
		}

		// see if this map already exists in the masterlayer list of ignored layers.  If so, we want to refresh it.
		if (!layerCopy) {
			n = eaSize(&g_EncounterMasterLayer->ignoredLayers);
			for (i = 0; i < n; i++) {
				if (0 == stricmp(g_EncounterMasterLayer->ignoredLayers[i]->pchFilename, pEncounterLayer->pchFilename)) {
					// Deinitialize the existing version and copy the new version to it
					bool visible = g_EncounterMasterLayer->ignoredLayers[i]->visible;
					layerCopy = eaRemove(&g_EncounterMasterLayer->ignoredLayers, i);
					oldencounter_SafeLayerCopyAll(pEncounterLayer, layerCopy);
					layerCopy->visible = visible;
					break;
				}
			}
		}

		if (!layerCopy) {
			layerCopy = oldencounter_SafeCloneLayer(pEncounterLayer);
		}

		// Check whether encounters have had the ground moved from under them.  Should be editor only?
		oldencounter_CheckLayerStaticEncounterHeights(worldGetAnyCollPartitionIdx(), layerCopy);

		// If the layer has its "ignore" flag set, set it aside so it doesn't get loaded
		// Also ignore layers that the player has asked us not to load
		if (!layerCopy->ignore && !oldencounter_LayerIsOnIgnoreList(layerCopy->pchFilename)) {
			int j, numEncounters;

			// Add all Static Encounters to the encounter dictionary
			// Adding this reference calls oldencounter_ValidateLayerCB, which will fixup and initialize the encounter layer
			numEncounters = eaSize(&layerCopy->staticEncounters);
			for (j = 0; j < numEncounters; j++) {
				OldStaticEncounter* staticEnc = layerCopy->staticEncounters[j];
				oldencounter_AddStaticEncounterReference(staticEnc);			
			}
			
			eaPush(&g_EncounterMasterLayer->encLayers, layerCopy);
		} else { // Layer is ignored
			eaPush(&g_EncounterMasterLayer->ignoredLayers, layerCopy);
		}
	}
}


// This function creates an Encounter Master Layer for the given map out of 
// encounterlayers that are already loaded into the dictionary.  Should probably use
// oldencounter_LoadLayersToDictionary first to ensure that the layers exist.
static OldEncounterMasterLayer* oldencounter_CreateMasterLayer(const char* mapFileName, const char* publicName)
{
	OldEncounterMasterLayer* newMasterLayer = StructCreate(parse_OldEncounterMasterLayer);
	char mapRelPath[MAX_PATH];

	fileRelativePath(mapFileName, mapRelPath);
	newMasterLayer->mapFileName = StructAllocString(mapRelPath);
	newMasterLayer->mapPublicName = allocAddString(publicName);
	
	return newMasterLayer;
}

EncounterLayer* oldencounter_FindSubLayer(OldEncounterMasterLayer* masterLayer, const char* layerFileName)
{
	if (masterLayer && layerFileName)
	{
		char subLayerFileName[MAX_PATH];
		int i, n = eaSize(&masterLayer->encLayers);
		fileRelativePath(layerFileName, subLayerFileName);
		if (!strEndsWith(subLayerFileName, ".encounterlayer"))
			strcat(subLayerFileName, ".encounterlayer");	
		for (i = 0; i < n; i++)
			if (masterLayer->encLayers[i]->pchFilename && !stricmp(subLayerFileName, masterLayer->encLayers[i]->pchFilename))
				return masterLayer->encLayers[i];
	}
	return NULL;
}


// ----------------------------------------------------------------------------------
// Map Loading Utilities
// ----------------------------------------------------------------------------------

void oldencounter_RegisterLayerChangeCallbacks(EncounterMapChangeCallback mapLoadCB, EncounterMapChangeCallback mapUnloadCB )
{
	s_MapLoadCallback = mapLoadCB;
	s_MapUnloadCallback = mapUnloadCB;
}


void oldencounter_UnloadLayers(void)
{
	if (!gConf.bAllowOldEncounterData) {
		return;
	}

	if (s_MapUnloadCallback) {
		s_MapUnloadCallback(NULL);
	}

	// Destroy the existing layer and clear the dictionary of all encounter layers
	if (g_EncounterMasterLayer) {
		eaDestroy(&g_EncounterMasterLayer->ignoredLayers);	// Ignored layers are NO_AST, so won't be freed by StructDestroy
		StructDestroy(parse_OldEncounterMasterLayer, g_EncounterMasterLayer);
		g_EncounterMasterLayer = NULL;
	}

	RefSystem_ClearDictionary(g_StaticEncounterDictionary, false);
#ifdef GAMESERVER
	RefSystem_ClearDictionary(g_EncounterLayerDictionary, false);
#endif
}


// Disables loading encounters when entering the editor
bool oldencounter_AllowLayerLoading(void)
{
	U32 oldState = s_EncounterLoadingDisabled;
	s_EncounterLoadingDisabled = 0;
	return (oldState != s_EncounterLoadingDisabled);
}


// Warning!
// Using this function to load more than one map at a time may do bad things; there are still a few global
// lists that are shared between all open master layers (encounter dictionary), and any collisions
// aren't handled.  Don't do it.
void oldencounter_LoadLayersForMapByName(const char *mapFileName, const char *publicname)
{
	DictionaryEArrayStruct *pStruct = NULL;
	int i, n;

	if ((g_EncounterMasterLayer) || s_EncounterLoadingDisabled || !mapFileName || !mapFileName[0]) {
		return;
	}

	// Create the MasterLayer
	g_EncounterMasterLayer = oldencounter_CreateMasterLayer(mapFileName, publicname);

	// Load layers for that map into the dictionary
	oldencounter_LoadLayersToDictionary(mapFileName);

	// If there are any existing EncounterLayers in the dictionary that belong to this map,
	// load them into the Master Layer.  Layers that are loaded later will get added
	// in the dictionary change callback.
	pStruct = resDictGetEArrayStruct(g_EncounterLayerDictionary);
	n = eaSize(&pStruct->ppReferents);
	for (i = 0; i < n; i++) {
		if (oldencounter_MatchesMasterLayer((EncounterLayer*)pStruct->ppReferents[i], g_EncounterMasterLayer)) {
			oldencounter_LoadToMasterLayer((EncounterLayer*)pStruct->ppReferents[i]);
		}
	}
}


// TomY ENCOUNTER_HACK
typedef struct DummyEncounterLibrary {
	EncounterLayer *layer;
} DummyEncounterLibrary;

ParseTable parse_DummyEncounterLibrary[] = {
	{ "EncounterLayer", TOK_OPTIONALSTRUCT(DummyEncounterLibrary, layer, parse_EncounterLayer) },
	{ "", 0, 0 }
};

// TomY ENCOUNTER_HACK
void oldencounter_SaveDummyEncounterLayer()
{
	char enc_file_name[256];
	Referent pRef;
	char *enc_layer_name;
	sprintf(enc_file_name, "%s.encounterlayer", zmapGetFilename(NULL));
	enc_layer_name = oldencounter_LayerNameFromFilename(enc_file_name);
	pRef = RefSystem_ReferentFromString("EncounterLayer", enc_layer_name);
	StructFreeString(enc_layer_name);
	
	if (pRef != NULL) {
		DummyEncounterLibrary dummy_layer = { (EncounterLayer*)pRef };
		ParserWriteTextFile(enc_file_name, parse_DummyEncounterLibrary, &dummy_layer, 0, 0);
	}
}


// TomY's Hacky intermediate solution for encounters in GroupDefs
void oldencounter_GenesisLoadHack(ZoneMap* pZoneMap)
{
	WorldZoneMapScope *pScope;
	int i, j;

	// Get zone map scope
	pScope = zmapGetScope(pZoneMap);

	// TomY ENCOUNTER_HACK
	if (zmapInfoHasGenesisData(zmapGetInfo(pZoneMap)) || zmapInfoAllowEncounterHack(zmapGetInfo(pZoneMap))) {
		// Find all spawn points in all scopes
		if (pScope && eaSize(&pScope->encounter_hacks)) {
			bool new_layer = false;
			EncounterLayer *encounter_layer;
			char enc_file_name[256];
			char *layer_name;
			GenesisZoneMapInfo *info = zmapInfoGetGenesisInfo(zmapGetInfo(pZoneMap));

			sprintf(enc_file_name, "%s.encounterlayer", zmapGetFilename(NULL));
			layer_name = oldencounter_LayerNameFromFilename(enc_file_name);
			
			printf("Rebuilding encounters-from-bin layer.\n");

			if (encounter_layer = (EncounterLayer *)RefSystem_ReferentFromString("EncounterLayer", layer_name)) {
				eaDestroyStruct(&encounter_layer->rootGroup.staticEncList, parse_OldStaticEncounter);
				eaDestroyStruct(&encounter_layer->rootGroup.childList, parse_OldStaticEncounterGroup);
				StructFreeString(layer_name);
			} else {
				encounter_layer = StructCreate(parse_EncounterLayer);
				encounter_layer->name = layer_name;
				encounter_layer->visible = 1;
				encounter_layer->pchFilename = StructAllocString(enc_file_name);
				RefSystem_AddReferent("EncounterLayer", encounter_layer->name, encounter_layer);
				new_layer = true;
			}

			encounter_layer->layerLevel = info ? info->mission_level : 0;

			for(i=eaSize(&pScope->encounter_hacks)-1; i>=0; --i) {
				EncounterDef *encounter_def = GET_REF(pScope->encounter_hacks[i]->properties->base_def);
				if (encounter_def) {
					const char *pcName = worldScopeGetObjectName(&pScope->scope, &pScope->encounter_hacks[i]->common_data);
					const char *pcGroup = worldScopeGetObjectGroupName(&pScope->scope, &pScope->encounter_hacks[i]->common_data);
					OldStaticEncounter *new_encounter = StructCreate(parse_OldStaticEncounter);
					bool found = false;

					if (!pcGroup) {
						pcGroup = "Default";
					}
					
					new_encounter->name = StructAllocString(pcName);
					copyVec3(pScope->encounter_hacks[i]->encounter_pos, new_encounter->encPos);
					copyQuat(pScope->encounter_hacks[i]->encounter_rot, new_encounter->encRot);
					COPY_HANDLE(new_encounter->baseDef, pScope->encounter_hacks[i]->properties->base_def);

					if (GET_REF(new_encounter->baseDef) && info) {
						for (j = 0; j < eaSize(&info->encounter_overrides); j++) {
							if (!stricmp(pcName, info->encounter_overrides[j]->encounter_name)) {
								new_encounter->defOverride = StructClone(parse_EncounterDef, GET_REF(new_encounter->baseDef));
								new_encounter->defOverride->spawnCond = genesisCreateEncounterSpawnCond(NULL, zmapInfoGetPublicName(zmapGetInfo(pZoneMap)), info->encounter_overrides[j]);
								if (info && info->level_type == MissionLevelType_PlayerLevel) {
									new_encounter->defOverride->bUsePlayerLevel = true;
								}
								
								if (info->encounter_overrides[j]->has_patrol) {
									char patrol_name[256];
									sprintf(patrol_name, "%s_Patrol", pcName);
									new_encounter->patrolRouteName = StructAllocString(patrol_name);
								}
								break;
							}
						}
					}

					for (j = eaSize(&encounter_layer->rootGroup.childList)-1; j >= 0; --j) {
						if (encounter_layer->rootGroup.childList[j]->groupName &&
							!strcmp(encounter_layer->rootGroup.childList[j]->groupName, pcGroup)) {
							eaPush(&encounter_layer->rootGroup.childList[j]->staticEncList, new_encounter);
							found = true;
							break;
						}
					}
					if (!found) {
						OldStaticEncounterGroup *group = StructCreate(parse_OldStaticEncounterGroup);
						group->groupName = allocAddString(pcGroup);
						eaPush(&group->staticEncList, new_encounter);
						eaPush(&encounter_layer->rootGroup.childList, group);
					}
				}
			}

			if (new_layer) {
				oldencounter_LoadToMasterLayer(encounter_layer);
			} else {
				if (s_MapLoadCallback) {
					s_MapLoadCallback(g_EncounterMasterLayer);
				}
			}
		}

		worldLibSetEncounterHackSaveLayerFunc(oldencounter_SaveDummyEncounterLayer);
	}
}


void oldencounter_LoadLayers(ZoneMap* zmap)
{
	const char *mapname = zmapGetFilename(zmap);
	assertmsg(mapname, "This should never fail, if it did, what mode was the server running in?");

	if (!gConf.bAllowOldEncounterData) {
		return;
	}

	oldencounter_LoadLayersForMapByName(mapname, zmapInfoGetPublicName(zmapGetInfo(zmap)));		// Sets up g_EncounterMasterLayer
#ifdef GAMESERVER
	patrolroute_ResetPatrols();
#endif
	oldencounter_GenesisLoadHack(zmap);

	// Call the map load callback.  Note that with server saving, all encounter layers may not be loaded
	// at this point on the client.
	if (s_MapLoadCallback)
		s_MapLoadCallback(g_EncounterMasterLayer);
}


#ifndef SDANGELO_REMOVE_PATROL_ROUTE
// ----------------------------------------------------------------------------------
// Patrol Route Utilities
// ----------------------------------------------------------------------------------

OldPatrolRoute* oldencounter_OldPatrolRouteFromName(EncounterLayer* encLayer, const char* name)
{
	int i, n = eaSize(&encLayer->oldNamedRoutes);
	for (i = 0; i < n; i++) {
		if (encLayer->oldNamedRoutes[i]->routeName && !stricmp(encLayer->oldNamedRoutes[i]->routeName, name)) {
			return encLayer->oldNamedRoutes[i];
		}
	}
	return NULL;
}

#endif


// ----------------------------------------------------------------------------------
// Static Encounter Utilities
// ----------------------------------------------------------------------------------

OldStaticEncounterGroup* oldencounter_StaticEncounterGroupFindWeightedParent(OldStaticEncounterGroup* staticEncGroup)
{
	OldStaticEncounterGroup* parent = staticEncGroup->parentGroup;
	while (parent) {
		if (oldencounter_StaticEncounterGroupIsWeighted(parent)) {
			return parent;
		}
		parent = parent->parentGroup;
	}
	return NULL;
}


OldStaticEncounterGroup* oldencounter_StaticEncounterFindWeightedParent(OldStaticEncounter* staticEnc)
{
	if (oldencounter_StaticEncounterGroupIsWeighted(staticEnc->groupOwner)) {
		return staticEnc->groupOwner;
	}
	return oldencounter_StaticEncounterGroupFindWeightedParent(staticEnc->groupOwner);
}


void oldencounter_FunctionalGroupGetStaticEncounterChildren(OldStaticEncounterGroup* staticEncGroup, OldStaticEncounter*** encListPtr)
{
	int i, n = eaSize(&staticEncGroup->childList);
	for (i = 0; i < n; i++) {
		OldStaticEncounterGroup* childGroup = staticEncGroup->childList[i];
		if (!oldencounter_StaticEncounterGroupIsFunctional(childGroup)) {
			oldencounter_FunctionalGroupGetStaticEncounterChildren(childGroup, encListPtr);
		}
	}

	n = eaSize(&staticEncGroup->staticEncList);
	for (i = 0; i < n; i++) {
		OldStaticEncounter* staticEnc = staticEncGroup->staticEncList[i];
		if (staticEnc) {
			eaPush(encListPtr, staticEnc);
		}
	}
}


void oldencounter_FunctionalGroupGetFunctionalGroupChildren(OldStaticEncounterGroup* staticEncGroup, OldStaticEncounterGroup*** groupListPtr)
{
	int i, n = eaSize(&staticEncGroup->childList);
	for (i = 0; i < n; i++) {
		OldStaticEncounterGroup* childGroup = staticEncGroup->childList[i];
		if (oldencounter_StaticEncounterGroupIsFunctional(childGroup)) {
			eaPush(groupListPtr, childGroup);
		} else {
			oldencounter_FunctionalGroupGetFunctionalGroupChildren(childGroup, groupListPtr);
		}
	}
}


bool oldencounter_StaticEncounterGroupIsWeighted(OldStaticEncounterGroup* staticEncGroup)
{
	return (staticEncGroup->numToSpawn != 0);
}


bool oldencounter_StaticEncounterGroupIsFunctional(OldStaticEncounterGroup* staticEncGroup)
{
	return oldencounter_StaticEncounterGroupIsWeighted(staticEncGroup);
}


// Note that it isn't safe to update the spawn rule while the editor is running unless you immediately refresh
// the encounter tree.
void oldencounter_UpdateStaticEncounterSpawnRule(OldStaticEncounter* staticEnc, EncounterLayer* encLayer)
{
	int i, n;
	Quat tempQuat;
	Mat4 encMat;
	EncounterDef* baseDef = GET_REF(staticEnc->baseDef);
	EncounterDef* spawnRule = staticEnc->spawnRule;
	EncounterDef* defOverride = staticEnc->defOverride;

	if (!spawnRule) {
		spawnRule = staticEnc->spawnRule = StructAlloc(parse_EncounterDef);
	}
	StructReset(parse_EncounterDef, spawnRule);

	// Fill out the encounter properties and use the basedef for the list of actors
	// If defOverride->name is not NULL, it tells us that defOverride is an instanced copy of the basedef.
	if (defOverride && (defOverride->name || !baseDef)) {
		StructCopyAll(parse_EncounterDef, defOverride, spawnRule);
		eaDestroyStruct(&spawnRule->actors, parse_OldActor);
		eaDestroyStruct(&spawnRule->namedPoints, parse_OldNamedPointInEncounter);
		
		// First start by copying all of the actors and named points from the basedef
		n = baseDef ? eaSize(&baseDef->actors) : 0;
		for (i = 0; i < n; i++) {
			OldActor* actorCopy = StructAlloc(parse_OldActor);
			StructCopyAll(parse_OldActor, baseDef->actors[i], actorCopy);
			eaPush(&spawnRule->actors, actorCopy);
		}
		n = baseDef ? eaSize(&baseDef->namedPoints) : 0;
		for (i = 0; i < n; i++) {
			OldNamedPointInEncounter* pointCopy = StructAlloc(parse_OldNamedPointInEncounter);
			StructCopyAll(parse_OldNamedPointInEncounter, baseDef->namedPoints[i], pointCopy);
			eaPush(&spawnRule->namedPoints, pointCopy);
		}

	} else { // No override on the properties use the base def if it exists, otherwise the default
		if (baseDef) {
			StructCopyAll(parse_EncounterDef, baseDef, spawnRule);
		} else {
			StructInit(parse_EncounterDef, spawnRule);
		}
	}

	// Set the filename of the encounter for error messages.  It should be the encounter layer's file, not the base def's file
	spawnRule->filename = NULL;
	if (encLayer->pchFilename) {
		spawnRule->filename = (const char*) allocAddFilename(encLayer->pchFilename);
	}

	// Merge all of the actor changes into the actor list
	if (defOverride) {
		// Now apply all the actor changes and add new actors if neccessary
		n = eaSize(&defOverride->actors);
		for (i = 0; i < n; i++) {
			OldActor* actorOverride = defOverride->actors[i];
			OldActor* baseActor = oldencounter_FindDefActorByID(spawnRule, actorOverride->uniqueID, false);
			if (baseActor) {
				OldActorInfo* baseInfo = baseActor->details.info;
				OldActorAIInfo* baseAIInfo = baseActor->details.aiInfo;
				OldActorPosition* basePos = baseActor->details.position;
				OldActorInfo* overrideInfo = actorOverride->details.info;
				OldActorAIInfo* overrideAIInfo = actorOverride->details.aiInfo;
				OldActorPosition* overridePos = actorOverride->details.position;
				if (overrideInfo) {
					if (!baseInfo) {
						baseInfo = baseActor->details.info = StructAlloc(parse_OldActorInfo);
					}
					StructCopyAll(parse_OldActorInfo, overrideInfo, baseInfo);
				}
				if (overrideAIInfo) {
					if (!baseAIInfo) {
						baseAIInfo = baseActor->details.aiInfo= StructAlloc(parse_OldActorAIInfo);
					}
					StructCopyAll(parse_OldActorAIInfo, overrideAIInfo, baseAIInfo);
				}
				if (overridePos) {
					if (overridePos->absoluteOverride) {
						copyVec3(overridePos->posOffset, basePos->posOffset);
						copyQuat(overridePos->rotQuat, basePos->rotQuat);
					} else {
						if (!basePos) {
							basePos = baseActor->details.position = StructCreate(parse_OldActorPosition);
							copyQuat(unitquat, basePos->rotQuat);
						}
						copyQuat(basePos->rotQuat, tempQuat);
						addVec3(basePos->posOffset, overridePos->posOffset, basePos->posOffset);
						quatMultiply(tempQuat, overridePos->rotQuat, basePos->rotQuat);
					}
				}

				if (actorOverride->name) {
					baseActor->name = (char*)allocAddString(actorOverride->name);
				}
				if (IS_HANDLE_ACTIVE(actorOverride->displayNameMsg.hMessage)) {
					REMOVE_HANDLE(baseActor->displayNameMsg.hMessage);
					COPY_HANDLE(baseActor->displayNameMsg.hMessage, actorOverride->displayNameMsg.hMessage);
				}
#ifndef NO_EDITORS
				if (actorOverride->displayNameMsg.pEditorCopy) {
					StructDestroy(parse_Message, baseActor->displayNameMsg.pEditorCopy);
					baseActor->displayNameMsg.pEditorCopy = StructClone(parse_Message, actorOverride->displayNameMsg.pEditorCopy);
				}
#endif
				if (actorOverride->disableSpawn != ActorScalingFlag_Inherited) {
					baseActor->disableSpawn = actorOverride->disableSpawn;
				}
				if (actorOverride->useBossBar != ActorScalingFlag_Inherited) {
					baseActor->useBossBar = actorOverride->useBossBar;
				}
				baseActor->overridden = true;

			} else if (actorOverride->details.info && actorOverride->details.aiInfo
						&& actorOverride->details.position) {
				OldActor* newActor = StructAlloc(parse_OldActor);
				StructCopyAll(parse_OldActor, actorOverride, newActor);
				newActor->overridden = true;
				eaPush(&spawnRule->actors, newActor);

			} else {
				// This actor is unused and is just excess data.  Remove it.
				StructDestroy(parse_OldActor, defOverride->actors[i]);
				defOverride->actors[i] = NULL;
			}
		}

		// We NULLed any actors that had partial override information and would never spawn.  Finish cleaning
		// up this excess data.
		for(i=n-1; i>=0; i--) {
			if (NULL == defOverride->actors[i]) {
				eaRemove(&defOverride->actors, i);
			}
		}

		n = eaSize(&defOverride->namedPoints);
		for (i = 0; i < n; i++) {
			OldNamedPointInEncounter* pointOverride = defOverride->namedPoints[i];
			OldNamedPointInEncounter* basePoint = oldencounter_FindDefPointByID(spawnRule, pointOverride->id, false);
			if (basePoint) {
				// Adjust the position based on the override
				addVec3(basePoint->relLocation[3], pointOverride->relLocation[3], basePoint->relLocation[3]);
			} else {
				// Create a new point in the spawn rule
				basePoint = StructAlloc(parse_OldNamedPointInEncounter);
				StructCopyAll(parse_OldNamedPointInEncounter, pointOverride, basePoint);
				eaPush(&spawnRule->namedPoints, basePoint);
			}
		}
	}

	// Remove all actors that are considered deleted
	for (i = eaSize(&spawnRule->actors) - 1; i >= 0; i--) {
		OldActor* actor = spawnRule->actors[i];
		if (eaiFind(&staticEnc->delActorIDs, actor->uniqueID) != -1) {
			StructDestroy(parse_OldActor, actor);
			eaRemove(&spawnRule->actors, i);
		}
	}

	// For any named points in encounters, calculate their absolute location from their location
	// within the encounter
	quatVecToMat4(staticEnc->encRot, staticEnc->encPos, encMat);
	n = eaSize(&spawnRule->namedPoints);
	for (i = 0; i < n; i++) {
		OldNamedPointInEncounter* point = spawnRule->namedPoints[i];
		mulMat4(encMat, point->relLocation, point->absLocation);
		point->hasAbsLoc = true;
	}
}


bool oldencounter_TryCleanupEncDefs(OldStaticEncounter* staticEnc)
{
	EncounterDef* baseDef = GET_REF(staticEnc->baseDef);
	EncounterDef* defOverride = staticEnc->defOverride;
	bool retVal = false;

	if (defOverride) {
		// Cleanup the arrays of actors
		if (baseDef) {
			retVal = oldencounter_TryCleanupActors(&baseDef->actors, &defOverride->actors);
		} else {
			retVal = oldencounter_TryCleanupActors(NULL, &defOverride->actors);
		}

		// If the defOverride is instanced, see if it contains any different information than the base def
		if (defOverride->name && baseDef) {
			OldActor** baseDefActors = baseDef->actors;
			OldActor** overrideActors = defOverride->actors;

			// Compare the two encounter defs, except for their actors
			baseDef->actors = NULL;
			defOverride->actors = NULL;
			if (0 == StructCompare(parse_EncounterDef, baseDef, defOverride, 0, 0, 0)) {
				// The two structs are identical, so the override is superfluous.  De-instance the override def
				StructDestroy(parse_EncounterDef, defOverride);

				// If we need to hold actor information, re-instance the override def
				if (overrideActors) {
					defOverride = StructCreate(parse_EncounterDef);
				} else {	// Otherwise, remove it entirely
					defOverride = NULL;
				}

				staticEnc->defOverride = defOverride;
				retVal = true;
			}

			// Replace the arrays of actors (unless we decided that the defOverride doesn't hold any useful information)
			baseDef->actors = baseDefActors;
			if (defOverride) {
				defOverride->actors = overrideActors;
			}

		} else if(baseDef && eaSize(&defOverride->actors) == 0 && eaSize(&defOverride->namedPoints) == 0) {
			// The two structs are identical, so the override is superfluous.  De-instance the override def
			StructDestroy(parse_EncounterDef, defOverride);

			staticEnc->defOverride = NULL;
			retVal = true;
		}
	}

	return retVal;
}


void oldencounter_MakeMasterStaticEncounterNameList(OldEncounterMasterLayer* masterLayer, const char*** staticEncounters, const char*** encounterDefs)
{
	EncounterDef** uniqueDefs = NULL;
	int i, n = (masterLayer ? eaSize(&masterLayer->encLayers) : 0);

	for (i = 0; i < n; i++) {
		EncounterLayer *layer = masterLayer->encLayers[i];
		if (layer) {
			int j, m = eaSize(&layer->staticEncounters);
			for (j = 0; j < m; j++) {
				OldStaticEncounter *staticEnc = layer->staticEncounters[j];
				EncounterDef *baseDef = GET_REF(staticEnc->baseDef);
				if (staticEncounters && staticEnc->name) {
					eaPush(staticEncounters, staticEnc->name);
				}
				if (baseDef) {
					eaPushUnique(&uniqueDefs, baseDef);
				}
			}
		}
	}

	if (encounterDefs) {
		n = eaSize(&uniqueDefs);
		for (i = 0; i < n; i++) {
			eaPush(encounterDefs, uniqueDefs[i]->name);
		}
	}
	eaDestroy(&uniqueDefs);
}


F32 oldencounter_GetStaticEncounterHeight(int iPartitionIdx, OldStaticEncounter* staticEnc, S32* foundFloor)
{
	// Use a copy so the encounter's actual location doesn't change
	Vec3 encPos;
	copyVec3(staticEnc->encPos, encPos);
	// Note: this actually returns the same values for "encounter is too far from the ground" and "encounter is exactly on the ground"
	return worldSnapPosToGround(iPartitionIdx, encPos, ENCOUNTER_CHECK_HEIGHT_DIST, -ENCOUNTER_CHECK_HEIGHT_DIST, foundFloor);
}


// ----------------------------------------------------------------------------------
// Display Message Utilities
// ----------------------------------------------------------------------------------

void oldencounter_GetLayerScopePath(char *dest, SA_PARAM_NN_VALID const char *filename)
{
	char *ptr;

	if (strnicmp(filename, "maps/", 5) == 0) {
		quick_sprintf(dest, MAX_PATH, "Maps/%s", filename+5);
	} else {
		quick_sprintf(dest, MAX_PATH, "Maps/%s", filename);
	}

	ptr = strstri(dest, ".encounterlayer");
	if (ptr) {
		*ptr = '\0';
	}
}


void oldencounter_GetLayerKeyPath(char *dest, SA_PARAM_NN_VALID const char *filename)
{
	char *ptr;

	if (strnicmp(filename, "maps/", 5) == 0) {
		quick_sprintf(dest, MAX_PATH, "Maps.%s", filename+5);
	} else {
		quick_sprintf(dest, MAX_PATH, "Maps.%s", filename);
	}

	ptr = strstri(dest, ".encounterlayer");
	if (ptr) {
		*ptr = '\0';
	}

	while(ptr = strchr(dest, '/')) {
		*ptr = '.';
	}
}


Message *oldencounter_CreateDisplayNameMessageForStaticEncActor(OldStaticEncounter* staticEnc, OldActor* actor)
{
	Message *message;
	char *estrKeyStr = NULL;
	char *estrScope = NULL;
	char *estrActorName = NULL;
	char layerKey[MAX_PATH];
	char layerScope[MAX_PATH];
	estrStackCreate(&estrKeyStr);
	estrStackCreate(&estrActorName);
	estrStackCreate(&estrScope);

	oldencounter_GetActorName(actor, &estrActorName);
	oldencounter_GetLayerKeyPath(layerKey, staticEnc->layerParent->pchFilename);
	oldencounter_GetLayerScopePath(layerScope, staticEnc->layerParent->pchFilename);

	estrPrintf(&estrKeyStr, "%s.Encounter.%s.%s.displayName", layerKey, staticEnc->name, estrActorName);
	estrPrintf(&estrScope, "%s/Encounters/%s", layerScope, staticEnc->name);
	
	message = langCreateMessage(estrKeyStr, "Critter Display Name override", estrScope, NULL);
	
	estrDestroy(&estrKeyStr);
	estrDestroy(&estrActorName);
	estrDestroy(&estrScope);
	
	return message;
}


Message *oldencounter_CreateVarMessageForStaticEncounterActor(OldStaticEncounter* staticEnc, OldActor* actor, const char *varName, const char *fsmName)
{
	Message *message;
	char *estrKeyStr = NULL;
	char *estrScope = NULL;
	char *estrActorName = NULL;
	char *estrDescription = NULL;
	char layerKey[MAX_PATH];
	char layerScope[MAX_PATH];
	estrStackCreate(&estrKeyStr);
	estrStackCreate(&estrActorName);
	estrStackCreate(&estrScope);
	estrStackCreate(&estrDescription);

	oldencounter_GetActorName(actor, &estrActorName);
	oldencounter_GetLayerKeyPath(layerKey, staticEnc->layerParent->pchFilename);
	oldencounter_GetLayerScopePath(layerScope, staticEnc->layerParent->pchFilename);

	estrPrintf(&estrKeyStr, "%s.Encounter.%s.%s.fsmvars.%s", layerKey, staticEnc->name, estrActorName, varName);
	estrPrintf(&estrScope, "%s/Encounters/%s", layerScope, staticEnc->name);
	estrPrintf(&estrDescription, "Value for the \"%s\" extern var in FSM \"%s\"", varName, fsmName);
	
	message = langCreateMessage(estrKeyStr, estrDescription, estrScope, NULL);
	
	estrDestroy(&estrKeyStr);
	estrDestroy(&estrActorName);
	estrDestroy(&estrScope);
	estrDestroy(&estrDescription);
	
	return message;
}


#include "AutoGen/oldencounter_common_h_ast.c"

