#include "aiConfig.h"

#include "aiAggro.h"
#include "aiBrawlerCombat.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiPowers.h"
#include "aiStruct.h"
#include "aiTeam.h"

#include "Character.h"
#include "EntityIterator.h"
#include "file.h"
#include "gslPetCommand.h"
#include "MemoryPool.h"
#include "objPath.h"
#include "ResourceManager.h"
#include "StringCache.h"
#include "StructMod.h"
#include "TextParserSimpleInheritance.h"

// this is now in baseentity.c to let it get sent down to the client
//#include "aiConfig_h_ast.c"

#include "aiBrawlerCombat.h"
#include "aiConfig_h_ast.h"
#include "aiMovement_h_ast.h"
#include "AILib_autogen_QueuedFuncs.h"

static S32 aiConfigUsedFieldOffset = 0;

static void freeOfftickInstance(AIOfftickInstance *inst)
{
	free(inst);
}

void aiConfigDestroyOfftickInstances(Entity *e, AIVarsBase *aib)
{
	stashTableClearEx(aib->offtickInstances, NULL, freeOfftickInstance);
}

int	aiTargetingGenerateConfigExpression(Expression* expr)
{
	int success;
	ExprContext* context = aiGetStaticCheckExprContext();

	aiTargetingExprVarsAdd(NULL, NULL, context, NULL);
	success = exprGenerate(expr, context);
	aiTargetingExprVarsRemove(NULL, NULL, context);

	return success;
}

// -----------------------------------------------------------------------------------------------------------------------------
static int aiConfigProcess(AIConfig* config)
{
	int success = true;
	ExprContext* staticCheckContext = aiGetStaticCheckExprContext();
	int i;

	if(config->targetingRequires)
		success &= aiTargetingGenerateConfigExpression(config->targetingRequires);

	if(config->targetingRating)
		success &= aiTargetingGenerateConfigExpression(config->targetingRating);

	if(config->offtickActions)
	{
		for(i=eaSize(&config->offtickActions)-1; i>=0; i--)
		{
			AIOfftickConfig *otc = config->offtickActions[i];

			success &= exprGenerate(otc->initialize, staticCheckContext);
			success &= exprGenerate(otc->coarseCheck, staticCheckContext);
			success &= exprGenerate(otc->fineCheck, staticCheckContext);
			success &= exprGenerate(otc->action, staticCheckContext);
		}
	}

	for(i=0; i<eafSize(&config->grievedHealingLevels); i++)
		config->grievedHealingLevels[i] /= 100;
	eafQSort(config->grievedHealingLevels);
	
	if (IS_HANDLE_ACTIVE(config->aggro.hOverrideAggroDef))
	{
		if (!aiGlobalSettings.useAggro2)
		{
			ErrorFilenamef(config->filename, "An Aggro2 def defined, but the project is not using aggro2.");
		}
		if (!GET_REF(config->aggro.hOverrideAggroDef))
		{
			const char *pchDefName = REF_STRING_FROM_HANDLE(config->aggro.hOverrideAggroDef);
			ErrorFilenamef(config->filename, "Aggro2 def not found %s", pchDefName);
		}
	}

	aiBrawlerCombat_ValidateConfig(config);

	return success;
}

// -----------------------------------------------------------------------------------------------------------------------------
static void aiConfigReapplyConfigToEnts(AIConfig* config)
{
	Entity *ent;
	EntityIterator *iter;

	if (!config)
		return;
	
	iter = entGetIteratorAllTypesAllPartitions(0, 0);
	while(ent = EntityIteratorGetNext(iter))
	{
		if(ent->aibase && GET_REF(ent->aibase->config_use_accessor)==config)
		{
			stashTableClearEx(ent->aibase->offtickInstances, NULL, freeOfftickInstance);

			ent->aibase->minGrievedHealthLevel = 1.0;

			ent->aibase->useDynamicPrefRange = config->useDynamicPrefRange; // copy the flag from config def to base instance

			aiConfigModReapplyAll(ent, ent->aibase);
		}
	}
	EntityIteratorRelease(iter);
}

// -----------------------------------------------------------------------------------------------------------------------------
static int aiConfigValidate(enumResourceValidateType eType, const char* pDictName, const char* pResourceName, void* pResource, U32 userID)
{
	AIConfig* config = pResource;

	switch (eType)
	{
	case RESVALIDATE_POST_TEXT_READING:
		aiConfigProcess(config);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

// -----------------------------------------------------------------------------------------------------------------------------
static int aiConfigInheritanceFunc(ParseTable *pti, int column, void *dst, void *src, void *unused)
{
	if(pti==parse_AIConfig)
	{
		AIConfig *configSrc = src, *configDst = dst;

		// Do special inheritance for offtick actions - inherit/override completely by name, not using usedfield
		// otherwise copy and add
		if(!stricmp(pti[column].name, "offtickActions"))
		{
			// Could do all this nonsense with tokenstore, but this is safer and more sensible
			int i;
			
			for(i=0; i<eaSize(&configSrc->offtickActions); i++)
			{
				int j;
				int found = 0;

				if(configSrc->offtickActions[i]->inherited)
					continue;

				for(j=0; j<eaSize(&configDst->offtickActions); j++)
				{
					if(!stricmp(configDst->offtickActions[j]->name, configSrc->offtickActions[i]->name))
					{
						found = 1;
						break;
					}
				}

				if(found)
				{
					if(!configDst->offtickActions[j]->inherited)
						continue;

					StructCopyAll(parse_AIOfftickConfig, configSrc->offtickActions[i], configDst->offtickActions[j]);
				}
				else
				{
					AIOfftickConfig *otc = StructCreate(parse_AIOfftickConfig);

					StructCopyAll(parse_AIOfftickConfig, configSrc->offtickActions[i], otc);
					otc->inherited = 1;

					eaPush(&configDst->offtickActions, otc);
				}
			}
			return 1;
		}
	}

	return 0;
}

static void aiInheritanceApply(AIConfig *config)
{
	int i;
	for(i=0; i<eaSize(&config->inheritConfigs); i++)
	{
		AIConfig *parent = RefSystem_ReferentFromString("AIConfig", config->inheritConfigs[i]);

		if(!parent)
		{
			ErrorFilenamef(config->filename, "Unable to find parent config to inherit from: %s", config->inheritConfigs[i]);
			continue;
		}

		SimpleInheritanceApply(parse_AIConfig, config, parent, aiConfigInheritanceFunc, NULL);
	}
	
}

static void aiConfig_ConstructorFixup(AIConfig* config)
{
	if (config->pedestrianScareDistance > config->pedestrianScareDistanceInCombat)
		config->pedestrianScareDistanceInCombat = config->pedestrianScareDistance;
}

AUTO_FIXUPFUNC;
TextParserResult fixupAIConfig(AIConfig* config, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_CONSTRUCTOR:
		aiFillInDefaultDangerFactors(config);
		aiConfig_ConstructorFixup(config);

	xcase FIXUPTYPE_POST_ALL_TEXT_READING_AND_INHERITANCE_DURING_LOADFILES:
		aiInheritanceApply(config);

	xcase FIXUPTYPE_POST_RELOAD:
		aiInheritanceApply(config);
		aiConfigReapplyConfigToEnts(config);

	case FIXUPTYPE_POST_BIN_READ:
	case FIXUPTYPE_POST_TEXT_READ:

		if (config->pBrawlerCombatConfig && aiGlobalSettings.pBrawlerConfig)
		{
			aiBrawlerCombat_InheritAIGlobalSettings(config->pBrawlerCombatConfig);
		}
	}

	return 1;
}

void aiConfigReloadAll()
{
	RefDictIterator iter;
	AIConfig* config;

	RefSystem_InitRefDictIterator("AIConfig", &iter);

	while(config = (AIConfig*)RefSystem_GetNextReferentFromIterator(&iter))
		ParserReloadFileToDictionary(config->filename, "AIConfig");
}

void aiConfigLoad()
{
	AIConfig* defaultConfig;

	if(!aiConfigUsedFieldOffset)
		ParserFindColumn(parse_AIConfig, "usedFields", &aiConfigUsedFieldOffset);

	assert(aiConfigUsedFieldOffset);

	RefSystem_RegisterSelfDefiningDictionary("AIConfig", false, parse_AIConfig, true, false, NULL);
	resDictManageValidation("AIConfig", aiConfigValidate);

	// this does forcerebuild to work correctly with the defaults being stored in a separate file
	if (isDevelopmentMode() || isProductionEditMode()) {
		resDictMaintainInfoIndex("AIConfig", ".name", NULL, NULL, NULL, NULL);
	}
	resLoadResourcesFromDisk("AIConfig", "ai/Config", ".aiconfig", "AIConfigs.bin", PARSER_FORCEREBUILD | RESOURCELOAD_SHAREDMEMORY);
	resDictProvideMissingResources("AIConfig");
	resDictProvideMissingRequiresEditMode("AIConfig");

	defaultConfig = RefSystem_ReferentFromString("AIConfig", "Default");

	if(!defaultConfig)
		Errorf("Could not find default AIConfig, which is required");

}

// Replaces the current critter's AIConfig with the specified AIConfig
// NOTE: this is currently a permanent replacement that cannot be undone
AUTO_EXPR_FUNC(ai) ACMD_NAME(AssignAIConfig);
ExprFuncReturnVal exprFuncAssignAIConfig(ACMD_EXPR_SELF Entity* e, const char* configname)
{
	AIConfig *base_config;
	if(!RefSystem_ReferentFromString("AIConfig", configname))
		return ExprFuncReturnError;

	base_config = GET_REF(e->aibase->config_use_accessor);
	if(base_config && allocAddString(configname)==base_config->name)
		return ExprFuncReturnFinished;

	REMOVE_HANDLE(e->aibase->config_use_accessor);
	SET_HANDLE_FROM_STRING("AIConfig", configname, e->aibase->config_use_accessor);

	aiConfigDestroyOfftickInstances(e, e->aibase);
	aiConfigModReapplyAll(e, e->aibase);
	
	base_config = GET_REF(e->aibase->config_use_accessor);
	if (base_config)
	{
		aiCheckOfftickActions(e, e->aibase, base_config);
	}
	return ExprFuncReturnFinished;
}

AIConfig* aiConfigGetModifiedConfig(Entity* be, AIVarsBase* aib)
{
	if(!aib->localModifiedAiConfig)
	{
		AIConfig* origConfig = GET_REF(aib->config_use_accessor);
		aib->localModifiedAiConfig = StructAlloc(parse_AIConfig);
		StructCopyAll(parse_AIConfig, origConfig, aib->localModifiedAiConfig);
	}

	return aib->localModifiedAiConfig;
}

struct aiConfigPreviousState{
	U32 valid : 1;
	U32 controlledPet : 1;
	U32 useDynamicPreferredRange : 1;
	U32 bankWhenMoving : 1;
}prevState;

void aiConfigRecordPreviousState(Entity* e, AIVarsBase* aib, AIConfig* config)
{
	devassert(!prevState.valid);
	prevState.valid = true;
	prevState.controlledPet = config->controlledPet;
	prevState.useDynamicPreferredRange = config->useDynamicPrefRange;
	prevState.bankWhenMoving = config->movementParams.bankWhenMoving;
}

void aiConfigUpdateOtherSystems(Entity* e, AIVarsBase* aib, AIConfig* config)
{
	devassert(prevState.valid);
	aiMovementUpdateConfigSettings(e, aib, config);
	aiTeamRescanSettings(aib->team);
	if(aib->combatTeam)
		aiTeamRescanSettings(aib->combatTeam);
	aiPowersUpdateConfigSettings(e, aib, config);
	
	if(prevState.controlledPet != config->controlledPet)
	{
		Entity* owner = entFromEntityRef(entGetPartitionIdx(e), e->erOwner);
		if(owner)
			PetCommands_UpdatePlayerPetInfo(owner, config->controlledPet, e->myRef);
	}

	if(prevState.useDynamicPreferredRange && !config->useDynamicPrefRange)
	{
		// just disable the flag
		aib->useDynamicPrefRange = false;	
	}
	if(!prevState.useDynamicPreferredRange && config->useDynamicPrefRange)
	{
		// should recalculate
		aib->useDynamicPrefRange = true;
		
		aiPowersCalcDynamicPreferredRange(e, aib);
	}
	if(prevState.bankWhenMoving != config->movementParams.bankWhenMoving)
	{
		if(e->pChar)
			e->pChar->bUpdateFlightParams = true;
	}

	aib->untargetable = config->untargetable;

	prevState.valid = false;
}

void aiConfigModApply(Entity* be, AIVarsBase* aib, AIConfig* config, StructMod* mod, int updateOtherSystems)
{
	if(updateOtherSystems)
		aiConfigRecordPreviousState(be, aib, config);

	structModApply(mod);

	if(updateOtherSystems)
		aiConfigUpdateOtherSystems(be, aib, config);
}

int aiConfigModAddFromString(Entity* be, AIVarsBase* aib, const char* relObjPath, const char* val, ACMD_EXPR_ERRSTRING errString)
{
	AIConfig* config = aiConfigGetModifiedConfig(be, aib);
	StructMod* mod;
	StructMod lookup = {0};
	static int id = 0;

	if(!structModResolvePath(config, parse_AIConfig, relObjPath, &lookup))
	{
		if(errString)
			estrPrintf(errString, "%s does not resolve to a valid AIConfig setting, please check the wiki for the correct name", relObjPath);
		return 0;
	}

	mod = structModCreate();

	*mod = lookup;

	mod->id = ++id;

	mod->val = allocAddString(val);
	mod->name = allocAddString(relObjPath);

	eaPush(&aib->configMods, mod);
	aiConfigModApply(be, aib, config, mod, true);
	return mod->id;
}

void aiConfigModReapplyAll(Entity* be, AIVarsBase* aib)
{
	AIConfig* origConfig = GET_REF(aib->config_use_accessor);
	AIConfig* localConfig = aiGetConfig(be, aib);
	int i, n;

	aiConfigRecordPreviousState(be, aib, localConfig);

	localConfig = origConfig;
	if(aib->localModifiedAiConfig)
	{
		localConfig = aib->localModifiedAiConfig;

		StructCopyAll(parse_AIConfig, origConfig, localConfig);
	}

	if(eaSize(&aib->configMods))
	{
		if(aib->localModifiedAiConfig == NULL)
		{
			localConfig = aiConfigGetModifiedConfig(be, aib);
		}

		// order actually matters
		for(i = 0, n = eaSize(&aib->configMods); i < n; i++)
			aiConfigModApply(be, aib, localConfig, aib->configMods[i], false);
	}

	aiConfigUpdateOtherSystems(be, aib, localConfig);
}

StructMod* aiConfigModFind(Entity* e, AIVarsBase* aib, S32 id)
{
	int i;

	for(i = eaSize(&aib->configMods)-1; i >= 0; i--)
	{
		if(aib->configMods[i]->id == id)
			return aib->configMods[i];
	}

	return NULL;
}

// Changes the AIConfig setting specified (effective until ClearAllConfigMods())
AUTO_EXPR_FUNC(ai) ACMD_NAME(AddConfigMod);
ExprFuncReturnVal exprFuncAddConfigMod(ACMD_EXPR_SELF Entity* e, ExprContext* context, const char* objPath, const char* value, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDAddStructMod* mydata = getMyData(context, parse_FSMLDAddStructMod, PTR_TO_UINT(objPath));
	if(mydata)
	{
		StructMod *mod = aiConfigModFind(e, e->aibase, mydata->id);

		if(!mod)
			mydata->id = aiConfigModAddFromString(e, e->aibase, objPath, value, errString);
		else if(stricmp(mod->val, value))
		{
			// TODO(AM): MAke this not reapply all since the add will just overwrite it
			aiConfigModRemove(e, e->aibase, mod->id);
			mydata->id = aiConfigModAddFromString(e, e->aibase, objPath, value, errString);
		}

		if(!mydata->id)
			return ExprFuncReturnError;
	}
	else
	{
		estrPrintf(errString, "Trying to call exprFuncAddConfigMod func and parse_FSMLDAddStructMod doesn't exist in context.");
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

// Changes the specified AIConfig setting for the current state only
AUTO_EXPR_FUNC(ai) ACMD_NAME(AddConfigModCurStateOnly);
ExprFuncReturnVal exprFuncAddConfigModCurStateOnly(ACMD_EXPR_SELF Entity* be, ExprContext* context, const char* objPath, const char* value, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDAddStructMod* mydata = getMyData(context, parse_FSMLDAddStructMod, PTR_TO_UINT(objPath));

	if(!mydata->dataIsSet)
	{
		CommandQueue* exitHandlers = NULL;
		ExprLocalData*** localData = NULL;

		exprContextGetCleanupCommandQueue(context, &exitHandlers, &localData);

		if(!exitHandlers)
		{
			estrPrintf(errString, "Trying to call CurStateOnly func without exitHandler reference");
			return ExprFuncReturnError;
		}

		mydata->dataIsSet = 1;
		mydata->id = aiConfigModAddFromString(be, be->aibase, objPath, value, errString);

		if(!mydata->id)
			return ExprFuncReturnError;

		QueuedCommand_deleteMyData(exitHandlers, context, parse_FSMLDAddStructMod, localData, PTR_TO_UINT(objPath));
		QueuedCommand_aiConfigModRemove(exitHandlers, be, be->aibase, mydata->id);
	}

	return ExprFuncReturnFinished;
}

AUTO_COMMAND_QUEUED();
void aiConfigModRemove(ACMD_POINTER Entity* be, ACMD_POINTER AIVarsBase* aib, int handle)
{
	int i;
	int found = false;

	if(entCheckFlag(be, ENTITYFLAG_DESTROY | ENTITYFLAG_PLAYER_LOGGING_OUT) || !be->aibase)
		return;		// AI Destroy has already cleaned up the configmods and reapplication is moot

	for(i = eaSize(&aib->configMods)-1; i >= 0; i--)
	{
		if(aib->configMods[i]->id == handle)
		{
			structModDestroy(aib->configMods[i]);

			// order actually matters for these in case they override the same setting in
			// multiple (sub) FSMs
			eaRemove(&aib->configMods, i);

			found = true;
			break;
		}
	}

	// It's ok to not find anything here, because it could have been removed
	if(found)
		aiConfigModReapplyAll(be, aib);
}

AUTO_COMMAND_QUEUED();
void aiConfigModRemoveAll(ACMD_POINTER Entity* be, ACMD_POINTER AIVarsBase* aib)
{
	int i;

	for(i = eaSize(&aib->configMods)-1; i >= 0; i--)
	{
		structModDestroy(aib->configMods[i]);

		// order actually matters for these in case they override the same setting in
		// multiple (sub) FSMs
		eaRemove(&aib->configMods, i);
	}

	// It's ok to not find anything here, because it could have been removed
	aiConfigModReapplyAll(be, aib);
}

void aiConfigModRemoveAllMatching(Entity* e, AIVarsBase* aib, const char* relObjPath, const char* val)
{
	StructMod lookup = {0};
	const char* allocVal = val ? allocFindString(val) : NULL;

	int found = false;

	int i;

	if(!structModResolvePath(NULL, parse_AIConfig, relObjPath, &lookup))
		return;

	for(i = eaSize(&aib->configMods)-1; i >= 0; i--)
	{
		StructMod* mod = aib->configMods[i];

		if(lookup.table == mod->table && lookup.column == mod->column && 
			lookup.idx == mod->idx && (!allocVal || allocVal == mod->val))
		{
			structModDestroy(mod);
			eaRemove(&aib->configMods, i);
			found = true;
		}
	}

	if(found)
		aiConfigModReapplyAll(e, aib);
}

// Clears all ConfigMods
AUTO_EXPR_FUNC(ai) ACMD_NAME(ClearAllConfigMods);
void exprFuncClearAllConfigMods(ACMD_EXPR_SELF Entity* be)
{
	AIVarsBase* aib = be->aibase;
	AIConfig *config;
	
	eaClearEx(&be->aibase->configMods, structModDestroy);
	if(aib->localModifiedAiConfig)
	{
		AIConfig* origConfig = GET_REF(aib->config_use_accessor);
		StructCopyAll(parse_AIConfig, origConfig, aib->localModifiedAiConfig);
	}

	config = aiGetConfig(be, aib);
	aiConfigRecordPreviousState(be, aib, config);
	aiConfigUpdateOtherSystems(be, aib, aiGetConfig(be, aib));
}

void aiConfigModDestroyAll(Entity* be, AIVarsBase* aib)
{
	eaDestroyEx(&aib->configMods, structModDestroy);
	if(aib->localModifiedAiConfig)
	{
		AIConfig* origConfig = GET_REF(aib->config_use_accessor);
		StructCopyAll(parse_AIConfig, origConfig, aib->localModifiedAiConfig);
	}
}

void aiConfigLocalCleanup(AIVarsBase * aib)
{
	if (eaSize(&aib->configMods) == 0 && aib->localModifiedAiConfig)
	{
		StructDestroy(parse_AIConfig, aib->localModifiedAiConfig);
		aib->localModifiedAiConfig = NULL;
	}
}

int aiConfigCheckSettingName(const char* setting)
{
	return structModResolvePath(NULL, parse_AIConfig, setting, NULL);
}

void aiTeamConfigApply(AITeamConfig *dst, const AITeamConfig *src, const Entity *srcEnt)
{
	dst->addLegalTargetWhenTargeted |= src->addLegalTargetWhenTargeted;
	dst->addLegalTargetWhenMemberAttacks |= src->addLegalTargetWhenMemberAttacks;

	dst->ignoreLevelDifference |= src->ignoreLevelDifference;

	dst->skipLeashing |= !!srcEnt->pPlayer || src->skipLeashing;

	// the team should use the longest leash in the encounter to minimize potential griefing
	// but no leash should only be true if every critter has no leash
	dst->ignoreMaxProtectRadius &= src->ignoreMaxProtectRadius;

	dst->useHealBuffAssignmentsOOC |= src->useHealBuffAssignmentsOOC;

	dst->shieldHealWeight = MAX(dst->shieldHealWeight, src->shieldHealWeight);

	dst->dontDoInCombatRezzing |= src->dontDoInCombatRezzing;

	dst->ressurectWeight = MAX(dst->ressurectWeight, src->ressurectWeight);
	
	dst->initialAggroWaitTimeMin = MAX(dst->initialAggroWaitTimeMin, src->initialAggroWaitTimeMin);
	dst->initialAggroWaitTimeRange = MAX(dst->initialAggroWaitTimeRange, src->initialAggroWaitTimeRange);

	dst->teamForceAttackTargetOnAggro |= src->teamForceAttackTargetOnAggro;

	dst->socialAggroAlwaysAddTeamToCombatTeam |= src->socialAggroAlwaysAddTeamToCombatTeam;
}


// ---------------------------------------------------------------------------------------------------
void aiConfigMods_RemoveConfigMods(Entity *e, S32 *peaTrackedConfigMods)
{
	S32 i;
	// clear all the other config mods
	for (i = eaiSize(&peaTrackedConfigMods) - 1; i >= 0; --i)
	{
		aiConfigModRemove(e, e->aibase, peaTrackedConfigMods[i]);
	}
	eaiClear(&peaTrackedConfigMods);
}

// ---------------------------------------------------------------------------------------------------
void aiConfigMods_ApplyConfigMods(Entity *e, const AIConfigMod** eaConfigMods, S32 **peaTrackedConfigMods)
{
	AIVarsBase *aib = e->aibase;

	if (peaTrackedConfigMods)
		aiConfigMods_RemoveConfigMods(e, *peaTrackedConfigMods);

	if (eaConfigMods)
	{
		S32 i;
		FOR_EACH_IN_EARRAY(eaConfigMods, const AIConfigMod, pConfigMod)
			i = aiConfigModAddFromString(e, aib, pConfigMod->setting, pConfigMod->value, NULL);
			if (i != 0 && peaTrackedConfigMods)
			{
				eaiPush(peaTrackedConfigMods, i);
			}
		FOR_EACH_END
	}
}


#include "aiConfig_h_ast.c"
