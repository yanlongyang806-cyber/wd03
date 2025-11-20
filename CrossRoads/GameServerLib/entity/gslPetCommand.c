#include "gslPetCommand.h"
#include "aiConfig.h"
#include "aiLib.h"
#include "aiMultiTickAction.h"
#include "aiPowers.h"
#include "aiStruct.h"
#include "aiTeam.h"
#include "Character.h"
#include "Character_target.h"
#include "entCritter.h"
#include "EntitySavedData.h"
#include "Entity.h"
#include "EntityLib.h"
#include "file.h"
#include "GameStringFormat.h"
#include "gslMapState.h"
#include "gslSavedPet.h"
#include "mapstate_common.h"
#include "NotifyEnum.h"
#include "Player.h"
#include "Powers.h"
#include "ReferenceSystem.h"
#include "ResourceManager.h"
#include "StringCache.h"
#include "SavedPetCommon.h"
#include "gslEncounter.h"

#include "Entity_h_ast.h"
#include "Player_h_ast.h"
#include "Team.h"

#include "mapstate_common_h_ast.h"
#include "gslPetCommand_c_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AIStanceConfigMod* aiStanceConfigModCreate();

#define PETCOMMAND_NOTIFYMSG_OUTOFRANGE	"PetCommand.NotifyOutOfRange"

AUTO_STRUCT;
typedef struct PetStanceConfigMod
{
	const char* setting; AST(REQUIRED POOL_STRING STRUCTPARAM)
	const char* value; AST(REQUIRED STRUCTPARAM)
}PetStanceConfigMod;

AUTO_STRUCT;
typedef struct PetStancePowerConfigTagMod
{
	const char* powerAITag; AST(REQUIRED POOL_STRING STRUCTPARAM)
	const char* setting; AST(REQUIRED POOL_STRING STRUCTPARAM)
	const char* value; AST(REQUIRED STRUCTPARAM)
} PetStancePowerConfigTagMod;

AUTO_STRUCT;
typedef struct PetState
{
	PetCommandNameInfo commandInfo; AST(EMBEDDED_FLAT)
	PetStanceConfigMod** configMods; AST(NAME(ConfigMod))
} PetState;

AUTO_STRUCT;
typedef struct PetStance
{
	PetCommandNameInfo nameInfo; AST(EMBEDDED_FLAT)
	PetStanceConfigMod** configMods; AST(NAME(ConfigMod))
	PetStancePowerConfigTagMod** powerConfigModsTag; AST(NAME(PowerConfigMod))
	PetStancePowerConfigTagMod** powerConfigModsTagExcluded; AST(NAME(PowerConfigModExcluded))
}PetStance;

AUTO_STRUCT;
typedef struct PetCommandConfig
{
	const char* name;								AST(KEY STRUCTPARAM)
		
	PetStance** StanceLists[PetStanceType_COUNT];	AST(INDEX(0, Stance) INDEX(1, Role))
	
	PetState** states;								AST(NAME(State))
	const char* filename;							AST(CURRENTFILE)

	const char* pchSpawnState;						AST(POOL_STRING)
	const char* pchSpawnStance[PetStanceType_COUNT]; AST(INDEX(0, SpawnStance) INDEX(1, SpawnRole) POOL_STRING)
		
	U32 setAttackTargetForcesAttack : 1;			AST(ADDNAMES("SetAttackTargetForcesAttack:") DEFAULT(1))
}PetCommandConfig;

DictionaryHandle g_PetCommandConfigDict;
REF_TO(PetCommandConfig) defaultConfigRef;

static void petStance_Validate(const PetCommandConfig* config, const PetStance *stance)
{
	S32 j;
	for(j = eaSize(&stance->configMods)-1; j >= 0; j--)
	{
		PetStanceConfigMod* mod = stance->configMods[j];
		if(!aiConfigCheckSettingName(mod->setting))
			ErrorFilenamef(config->filename, "ConfigMod setting %s for stance %s is not valid", mod->setting, stance->nameInfo.pchName);
	}
	for(j = eaSize(&stance->powerConfigModsTag)-1; j>=0; j--)
	{
		PetStancePowerConfigTagMod *mod = stance->powerConfigModsTag[j];
		if(!aiPowerConfigCheckSettingName(mod->setting))
			ErrorFilenamef(config->filename, "PowerConfigTagMod setting %s for stance %s is not valid", mod->setting, stance->nameInfo.pchName);
		if(StaticDefineIntGetInt(PowerAITagsEnum, mod->powerAITag)==-1)
			ErrorFilenamef(config->filename, "PowerConfigTagMod powerAITag %s for stance %s is not valid", mod->powerAITag, stance->nameInfo.pchName);
	}
	for(j = eaSize(&stance->powerConfigModsTagExcluded)-1; j>=0; j--)
	{
		PetStancePowerConfigTagMod *mod = stance->powerConfigModsTagExcluded[j];
		if(!aiPowerConfigCheckSettingName(mod->setting))
			ErrorFilenamef(config->filename, "PowerConfigTagMod setting %s for stance %s is not valid", mod->setting, stance->nameInfo.pchName);
		if(StaticDefineIntGetInt(PowerAITagsEnum, mod->powerAITag)==-1)
			ErrorFilenamef(config->filename, "PowerConfigTagMod powerAITag %s for stance %s is not valid", mod->powerAITag, stance->nameInfo.pchName);
	}
}


static int petCommandConfigValidate(enumResourceValidateType eType, const char* pDictName, const char* pResourceName, void* pResource, U32 userID)
{
	PetCommandConfig* config = pResource;

	switch (eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
		{
			S32 i, x;

			for(x = 0; x < PetStanceType_COUNT; x++)
			{
				PetStance** eaStance = config->StanceLists[x];
				for(i = eaSize(&eaStance)-1; i >= 0; i--)
				{
					petStance_Validate(config, eaStance[i]);
				}
			}

		

			resAddValueDep("PetCommandLoadingVer");
		}
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void RegisterPetCommandConfigDictionary(void)
{
	g_PetCommandConfigDict = RefSystem_RegisterSelfDefiningDictionary("PetCommandConfig", false, parse_PetCommandConfig, true, true, NULL);
	ParserBinRegisterDepValue("PetCommandLoadingVer", 1);

	resDictManageValidation(g_PetCommandConfigDict, petCommandConfigValidate);

	if (isDevelopmentMode() || isProductionEditMode()) {
		resDictMaintainInfoIndex(g_PetCommandConfigDict, ".name", NULL, NULL, NULL, NULL);
	}
}

AUTO_STARTUP(PetCommandConfig) ASTRT_DEPS(AI);
void petCommandConfigLoad(void)
{
	resLoadResourcesFromDisk(g_PetCommandConfigDict, NULL, "defs/config/PetCommands.def", "PetCommands.bin", PARSER_SERVERSIDE | PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	SET_HANDLE_FROM_STRING(g_PetCommandConfigDict, "Default", defaultConfigRef);
}

static int PetCommands_IsControlledPet(Entity* pOwner, Entity* pet)
{
	AIConfig* config;

	if(entGetRef(pOwner) != pet->erOwner)
		return false;

	config = aiGetConfig(pet, pet->aibase);

	return config->controlledPet;
}

void PetCommands_GetAllPets(Entity* pOwner, Entity*** pets)
{
	int i;
	AITeam* team;
	EntityRef ownerRef;
	if (pOwner && pOwner->pSaved)
	{
		int iPartitionIdx = entGetPartitionIdx(pOwner);

		for (i = 0; i < eaSize(&pOwner->pSaved->ppOwnedContainers); i++)
		{
			PetRelationship *conRelation = pOwner->pSaved->ppOwnedContainers[i];
			Entity *pet = SavedPet_GetEntity(iPartitionIdx, conRelation);
			if (pet)
			{
				pet = entFromContainerID(iPartitionIdx, pet->myEntityType, entGetContainerID(pet));	// for some reason, the GET_REF(conRelation->hPet) gives the wrong memory address
				// but this gets the proper address.
			}
			if (pet)
			{
				if (conRelation->eRelationship == CONRELATION_PET || conRelation->eRelationship == CONRELATION_PRIMARY_PET)
					eaPushUnique(pets, pet);
			}
		}

		team = pOwner->aibase->team;
		ownerRef = entGetRef(pOwner);

		for (i = 0; i < eaSize(&team->members); i++)
		{
			Entity* pet = team->members[i]->memberBE;
			if(pet->erOwner == ownerRef)
				eaPushUnique(pets, pet);
		}

		for(i = eaSize(pets)-1; i >= 0; i--)
		{
			Entity* pet = (*pets)[i];
			if(!PetCommands_IsControlledPet(pOwner, pet))
				eaRemoveFast(pets, i);
		}
	}
}

static int PetCommands_FindPetCommandListIndex(Entity* pOwner, int petRef)
{
	int i;

	for(i = eaSize(&pOwner->pPlayer->petInfo)-1; i >= 0; i--)
	{
		if (pOwner->pPlayer->petInfo[i]->iPetRef == (EntityRef)petRef)
		{
			return i;
		}
	}

	return -1;
}

void entity_RebuildPetPowerStates(Entity *pentOwner, PlayerPetInfo *pInfo)
{
	Entity *e = entFromEntityRef(entGetPartitionIdx(pentOwner), pInfo->iPetRef);

	if(!e || !e->pChar)
	{
		eaDestroyStruct(&pInfo->ppPowerStates, parse_PetPowerState);
		return;
	}
	
	if (pInfo->ppPowerStates)
	{	// only add/remove required
		FOR_EACH_IN_EARRAY(pInfo->ppPowerStates, PetPowerState, pPetPowerState);
			pPetPowerState->bResetDirty = true;
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(e->pChar->ppPowers, Power, pPower);
		{
			PowerDef *pdef = GET_REF(pPower->hDef);
			if(pdef && POWERTYPE_ACTIVATABLE(pdef->eType))
			{	
				bool bFound = false;
				FOR_EACH_IN_EARRAY(pInfo->ppPowerStates, PetPowerState, pPetPowerState);
					if (pPetPowerState->bResetDirty && REF_COMPARE_HANDLES(pPetPowerState->hdef, pPower->hDef))
					{
						pPetPowerState->bResetDirty = false;
						bFound = true;
						break;
					}
				FOR_EACH_END;

				if (!bFound)
				{
					PetPowerState *pPetPowerState = StructCreate(parse_PetPowerState);
					COPY_HANDLE(pPetPowerState->hdef,pPower->hDef);
					eaPush(&pInfo->ppPowerStates,pPetPowerState);
				}
			}
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pInfo->ppPowerStates, PetPowerState, pPetPowerState);
			if (pPetPowerState->bResetDirty)
			{
				PetPowerState *pRem = eaRemoveFast(&pInfo->ppPowerStates, ipPetPowerStateIndex);
				StructDestroy(parse_PetPowerState, pRem);
			}
		FOR_EACH_END;
	}
	else
	{	// add all powers
		FOR_EACH_IN_EARRAY(e->pChar->ppPowers, Power, pPower);
		{
			PowerDef *pdef = GET_REF(pPower->hDef);
			if(pdef && POWERTYPE_ACTIVATABLE(pdef->eType))
			{
				PetPowerState *pState = StructCreate(parse_PetPowerState);
				COPY_HANDLE(pState->hdef,pPower->hDef);
				eaPush(&pInfo->ppPowerStates,pState);
			}
		}
		FOR_EACH_END;
	}

	entity_SetDirtyBit(pentOwner, parse_Player, pentOwner->pPlayer, true);
}

int PetCommands_InitializeControlledPetInfo(Entity *e, CritterDef* critter, AIConfig *config)
{
	Entity* owner;
	S32 iNewPetInfo;
	S32 i;
	PlayerPetInfo *pBasePet = NULL;
	AIVarsBase *aib = e->aibase;
	

	if(!config->controlledPet)
		return false;

	owner = entGetOwner(e);
	if(!owner)
	{
		ErrorFilenamef(critter->pchFileName, "Critter spawned with an aiconfig specifying controlledpet but no owner");
		return false;
	}
	if(!owner->pPlayer)
	{
		ErrorFilenamef(critter->pchFileName, "Critter spawned with an aiconfig specifying controlledpet but the owner is not a player");
		return false;
	}
	
	// TODO(RP): This is temporary until I can find a better place to know to turn this on
	if (config->controlledPetUseRally)
		aiSetLeashTypeOwner(e, aib);

	// Allocate the array of stance config mods 
	for (i = 0; i < PetStanceType_COUNT; i++)
	{
		AIStanceConfigMod *pMod = aiStanceConfigModCreate();
		eaPush(&aib->stanceConfigMods, pMod);
	}

	iNewPetInfo = PetCommands_UpdatePlayerPetInfo(owner, true, e->myRef);
	if (iNewPetInfo < 0)
		return false;
	
	if(config->controlledPetStartInCurStance && iNewPetInfo != 0)
	{
		int iPartitionIdx = entGetPartitionIdx(owner);

		// Find an existing non-dead (destroyed is ok) pet
		for(i=0; i<iNewPetInfo; i++)
		{
			Entity *eExistingPet = entFromEntityRef(iPartitionIdx, owner->pPlayer->petInfo[i]->iPetRef);
			if(eExistingPet && !entCheckFlag(eExistingPet,ENTITYFLAG_DEAD))
			{
				pBasePet = owner->pPlayer->petInfo[i];
				break;
			}
		}

		// Make sure all further existing non-dead (destroyed is ok) pets having matching data
		if(pBasePet)
		{
			int iStances = eaSize(&pBasePet->eaStances);
			for(i=i+1; i<iNewPetInfo; i++)
			{
				PlayerPetInfo *pExistingPet = owner->pPlayer->petInfo[i];
				Entity *eExistingPet = entFromEntityRef(iPartitionIdx, pExistingPet->iPetRef);
				if(eExistingPet && !entCheckFlag(eExistingPet,ENTITYFLAG_DEAD))
				{
					int j;

					if(pBasePet->curPetState!=pExistingPet->curPetState)
						break;

					if(iStances!=eaSize(&pExistingPet->eaStances))
						break;

					for(j=iStances-1; j>=0; j--)
					{
						if(pBasePet->eaStances[j]->curStance!=pExistingPet->eaStances[j]->curStance)
							break;
					}
					if(j>=0)
						break;
				}
			}

			if(i!=iNewPetInfo)
				pBasePet = NULL;
		}
	}

	if(config->controlledPetStartInCurStance && pBasePet && eaSize(&pBasePet->eaStances))
	{	
		S32 count = eaSize(&pBasePet->eaStances);
		for (i = 0; i < count; i++)
		{
			PetStanceInfo* pStanceInfo = pBasePet->eaStances[i];
			PetCommands_SetSpecificPetStance(owner, entGetRef(e), (PetStanceType)i, pStanceInfo->curStance);
		}
	}
	else
	{
		PetCommands_SetDefaultPetStances(owner, e);
	}

	if(config->controlledPetStartInCurState && pBasePet && pBasePet->curPetState)
	{
		// we already have a pet out and we want to sync up the pet states 
		PetCommands_SetSpecificPetState(owner, entGetRef(e), pBasePet->curPetState);
	}
	else
	{
		PetCommands_SetDefaultPetState(owner, e);
	}

	return true;
}

static int PetCommands_CreatePlayerPetInfo(Entity* pOwner, PetCommandConfig* config, int petRef)
{
	int id;

	if(!config)
		return -1;

	if (petRef != -1)
	{
		id = PetCommands_FindPetCommandListIndex(pOwner, petRef);
		if (id != -1)
		{
			StructDestroy(parse_PlayerPetInfo, eaGet(&pOwner->pPlayer->petInfo,id));
			eaRemove(&pOwner->pPlayer->petInfo,id);
			entity_SetDirtyBit(pOwner, parse_Player, pOwner->pPlayer, true);
		}
	}

	if (petRef != -1 || eaSize(&pOwner->pPlayer->petInfo) == 0)
	{
		int i;
		PlayerPetInfo* petInfo = StructCreate(parse_PlayerPetInfo);
		eaPush(&pOwner->pPlayer->petInfo, petInfo);
		entity_SetDirtyBit(pOwner, parse_Player, pOwner->pPlayer, true);

		// allocate all the pet stance info
		for (i = 0; i < PetStanceType_COUNT; i++)
		{
			PetStanceInfo *pStanceInfo = calloc(1, sizeof(PetStanceInfo));
			PetStance** eaStanceConfig = config->StanceLists[i];

			eaPush(&petInfo->eaStances, pStanceInfo);
			if (eaSize(&eaStanceConfig))
			{
				S32 x;
				
				pStanceInfo->curStance = eaStanceConfig[0]->nameInfo.pchName;

				for(x = eaSize(&eaStanceConfig)-1; x >= 0; x--)
				{
					PetCommandNameInfo *pPetCmdInfo = StructClone(parse_PetCommandNameInfo, &eaStanceConfig[x]->nameInfo);
					eaPush(&pStanceInfo->validStances, pPetCmdInfo);
				}
			}
		}

		petInfo->iPartitionIdx = entGetPartitionIdx(pOwner);
		if (petRef == -1) petInfo->iPetRef = 0;
		else petInfo->iPetRef = petRef;
		
		if(eaSize(&config->states))
			petInfo->curPetState = config->states[0]->commandInfo.pchName;
		
		for(i = eaSize(&config->states)-1; i >= 0; i--)
			eaPush(&petInfo->validStates, StructClone(parse_PetCommandNameInfo, &config->states[i]->commandInfo));

		// Update PetPowerState data
		entity_RebuildPetPowerStates(pOwner,petInfo);

		return eaSize(&pOwner->pPlayer->petInfo) - 1;
	}

	return -1;
}

int PetCommands_UpdatePlayerPetInfo(Entity* pOwner, int added, int petRef)
{
	int i;

	if(!pOwner || !pOwner->pPlayer)
		return -1;

	if(added)
	{
		PetCommandConfig* defaultConfig = GET_REF(defaultConfigRef);
		return PetCommands_CreatePlayerPetInfo(pOwner, defaultConfig, petRef);
	}
	else 
	{
		// find the petinfo that corresponds to the petRef and destroy & remove it from the 
		// player's petInfo list
		for(i = eaSize(&pOwner->pPlayer->petInfo)-1; i >= 0; i--)
		{
			PlayerPetInfo *petInfo = pOwner->pPlayer->petInfo[i];
			if (petInfo->iPetRef == (EntityRef)petRef)
			{
				StructDestroy(parse_PlayerPetInfo, petInfo);
				eaRemove(&pOwner->pPlayer->petInfo,i);
				entity_SetDirtyBit(pOwner, parse_Player, pOwner->pPlayer, true);
				break;
			}
		}
	}
	
	return -1;
}

int PetCommands_CheckStateValidity(PetCommandConfig* config, const char* allocState)
{
	int i;

	for (i = eaSize(&config->states)-1; i >= 0; i--)
	{
		if(config->states[i]->commandInfo.pchName == allocState)
			return true;
	}

	return false;
}
static PetState* PetCommands_GetPetState(PetCommandConfig* config, const char* allocState)
{
	FOR_EACH_IN_EARRAY(config->states, PetState, pPetState)
		if(pPetState->commandInfo.pchName == allocState)
			return pPetState;
	FOR_EACH_END

	return NULL;
}

static void PetCommands_SetPetState(Entity* pOwner, Entity* pet, PetState *petState)
{
	PlayerPetInfo *ppi;
	S32 index = PetCommands_FindPetCommandListIndex(pOwner, pet->myRef);
	S32 i;
	AIVarsBase *aib = pet->aibase;
	
	if(index == -1 || !aib)
		return;
	
	ppi = (PlayerPetInfo*)eaGet(&pOwner->pPlayer->petInfo, index);
	devassert(ppi);

	// force an update to the pet's think tick
	aiForceThinkTick(pet, aib);

	// ignore if it's the same pet state 
	if(ppi->curPetState == petState->commandInfo.pchName)
	{
		//aiChangeState(pet, petState->commandInfo.pchName);
		return;
	}
	
	// remove the old state config mods
	for(i = eaiSize(&aib->stateConfigMods)-1; i >= 0; i--)
		aiConfigModRemove(pet, aib, aib->stateConfigMods[i]);

	eaiClearFast(&aib->stateConfigMods);
	
	// add the new state config mods

	FOR_EACH_IN_EARRAY(petState->configMods, PetStanceConfigMod, pMod)
	
		int iConfigModID = aiConfigModAddFromString(pet, aib, pMod->setting, pMod->value, NULL);
	
		if (iConfigModID)
		{
			eaiPush(&aib->stateConfigMods, iConfigModID);
		}

	FOR_EACH_END


	
	// change the PlayerPetInfo's current state
	entity_SetDirtyBit(pOwner, parse_Player, pOwner->pPlayer, true);
	ppi->curPetState = petState->commandInfo.pchName;
	
	aiChangeState(pet, petState->commandInfo.pchName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void PetCommands_SetSpecificPetState(SA_PARAM_NN_VALID Entity *pOwner, EntityRef ent, const char* state)
{	
	Entity *pet = entFromEntityRef(entGetPartitionIdx(pOwner), ent);
	PetCommandConfig* defaultConfig = GET_REF(defaultConfigRef);
	const char* allocState = allocFindString(state);
	PetState *pPetState;

	if(!pet || !defaultConfig || !PetCommands_IsControlledPet(pOwner, pet))
		return;

	pPetState = PetCommands_GetPetState(defaultConfig, allocState);
	if (!pPetState)
		return;

	PetCommands_SetPetState(pOwner, pet, pPetState);
	return;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void PetCommands_SetAllPetsState(Entity *pOwner, const char* state)
{
	int i;
	PetCommandConfig* defaultConfig = GET_REF(defaultConfigRef);
	const char* allocState = allocFindString(state);
	int found = false;
	Entity** pets = NULL;
	PetState *pPetState;

	if (!pOwner || !pOwner->pSaved || !defaultConfig)
		return;

	pPetState = PetCommands_GetPetState(defaultConfig, allocState);
	if (!pPetState)
		return;

	PetCommands_GetAllPets(pOwner, &pets);

	if(eaSize(&pets))
	{
		for(i = eaSize(&pets)-1; i >= 0; i--)
		{
			PetCommands_SetPetState(pOwner, pets[i], pPetState);
		}
	}
	eaDestroy(&pets);
}

AUTO_EXPR_FUNC(ai);
void SetAllPetsState(ACMD_EXPR_SELF Entity* e, const char* state)
{
	if(e && state)
		PetCommands_SetAllPetsState(e,state);
}

void PetCommands_SetDefaultPetState(Entity* pOwner, Entity *pPetEnt)
{
	PetCommandConfig* defaultConfig = GET_REF(defaultConfigRef);
	PetState *pState = NULL;
	
	if(!pPetEnt || !defaultConfig || !eaSize(&defaultConfig->states))
		return;

	if(defaultConfig->pchSpawnState)
		pState = PetCommands_GetPetState(defaultConfig, defaultConfig->pchSpawnState);
	
	if(!pState)
		pState = defaultConfig->states[0];

	PetCommands_SetPetState(pOwner, pPetEnt, pState);
}


// ------------------------------------------------------------------------------------------------
// Pet stance functions

static PetStance* PetCommands_GetStanceByName(PetCommandConfig* commandConfig, PetStanceType eStance, const char* allocStance)
{
	S32 i;
	PetStance **eaStance;
	
	eaStance = commandConfig->StanceLists[eStance];
	
	for(i = eaSize(&eaStance)-1; i >= 0; i--)
	{
		PetStance* curStance = eaStance[i];
		if(curStance->nameInfo.pchName == allocStance)
			return curStance;
	}

	return NULL;
}

static void PetCommands_SetPlayersPetInfoStance(Entity* pOwner, Entity* pet, PetStanceType stanceType, const PetStance* stance)
{
	S32 index = PetCommands_FindPetCommandListIndex(pOwner, pet->myRef);
	PlayerPetInfo *ppi;

	if (index == -1)
		ppi = (PlayerPetInfo*)eaGet(&pOwner->pPlayer->petInfo, 0);
	else 
		ppi = (PlayerPetInfo*)eaGet(&pOwner->pPlayer->petInfo, index);

	if (ppi)
	{
		PetStanceInfo* pStanceInfo = eaGet(&ppi->eaStances, stanceType);
		devassert(pStanceInfo);

		if (pStanceInfo->curStance != stance->nameInfo.pchName) 
		{
			entity_SetDirtyBit(pOwner, parse_Player, pOwner->pPlayer, true);
			pStanceInfo->curStance = stance->nameInfo.pchName;
		}
	}
}

static void PetCommands_SetPetStance(Entity* pOwner, Entity* pet, PetStanceType stanceType, const PetStance* stance)
{
	int i;
	AIVarsBase* petAIB = pet->aibase;
	AIStanceConfigMod* curConfigMods;
	
	curConfigMods = eaGet(&petAIB->stanceConfigMods, stanceType);
	devassert(curConfigMods);

	// have to clean up the old mods.
	for(i = eaiSize(&curConfigMods->configMods)-1; i >= 0; i--)
		aiConfigModRemove(pet, petAIB, curConfigMods->configMods[i]);
	
	for(i = eaiSize(&curConfigMods->powerConfigMods)-1; i >= 0; i--)
		aiPowerConfigModRemove(pet, petAIB, curConfigMods->powerConfigMods[i]);
	
	eaiClearFast(&curConfigMods->configMods);
	eaiClearFast(&curConfigMods->powerConfigMods);

	// push the new mods
	for(i = eaSize(&stance->configMods)-1; i >= 0; i--)
	{
		PetStanceConfigMod* curMod = stance->configMods[i];
		eaiPush(&curConfigMods->configMods,
			aiConfigModAddFromString(pet, pet->aibase, curMod->setting, curMod->value, NULL));
	}

	for(i = eaSize(&stance->powerConfigModsTag)-1; i >= 0; i--)
	{
		PetStancePowerConfigTagMod* curMod = stance->powerConfigModsTag[i];
		aiAddPowerConfigModByTag(pet, petAIB, &curConfigMods->powerConfigMods, curMod->powerAITag, NULL, curMod->setting, curMod->value);
	}

	for(i = eaSize(&stance->powerConfigModsTagExcluded)-1; i>=0; i--)
	{
		PetStancePowerConfigTagMod* curMod = stance->powerConfigModsTagExcluded[i];
		aiAddPowerConfigModByTag(pet, petAIB, &curConfigMods->powerConfigMods, NULL, curMod->powerAITag, curMod->setting, curMod->value);
	}

	PetCommands_SetPlayersPetInfoStance(pOwner, pet, stanceType, stance);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void PetCommands_SetSpecificPetStance(Entity *pOwner, EntityRef ent, int stanceType, const char* stance)
{	
	Entity *pet = entFromEntityRef(entGetPartitionIdx(pOwner), ent);
	PetCommandConfig* defaultConfig = GET_REF(defaultConfigRef);
	const char* pchStance = allocFindString(stance);
	PetStance *pStance;

	if(!pet || !defaultConfig || stanceType < 0 || stanceType >= PetStanceType_COUNT)
		return;

	pStance = PetCommands_GetStanceByName(defaultConfig, stanceType, pchStance);
	
	if (pStance && PetCommands_IsControlledPet(pOwner, pet))
	{
		PetCommands_SetPetStance(pOwner, pet, stanceType, pStance);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void PetCommands_SetAllPetsStance(Entity *pOwner, int stanceType, const char* stance)
{
	PetCommandConfig* defaultConfig = GET_REF(defaultConfigRef);
	PetStance *pStance;
	const char* pchStance = allocFindString(stance);
	Entity** pets = NULL;

	if (!pOwner || !pOwner->pSaved || !defaultConfig || stanceType < 0 || stanceType >= PetStanceType_COUNT)
	{
		return;
	}

	pStance = PetCommands_GetStanceByName(defaultConfig, stanceType, pchStance);

	if(!pStance)
		return;

	PetCommands_GetAllPets(pOwner, &pets);

	if(eaSize(&pets))
	{
		S32 i;
		for(i = eaSize(&pets)-1; i >= 0; i--)
		{
			int index = PetCommands_FindPetCommandListIndex(pOwner, pets[i]->myRef);

			PetCommands_SetPetStance(pOwner, pets[i], stanceType, pStance);
		}
	}

	eaDestroy(&pets);
}


void PetCommands_SetDefaultPetStance(Entity* pOwner, Entity *pPetEnt, PetStanceType stanceType)
{
	PetCommandConfig* defaultConfig = GET_REF(defaultConfigRef);
	PetStance *pStance = NULL;
	devassert(stanceType >= 0 && stanceType < PetStanceType_COUNT);

	if(!pPetEnt || !defaultConfig || !eaSize(&defaultConfig->StanceLists[stanceType]))
		return;

	if (defaultConfig->pchSpawnStance[stanceType])
		pStance = PetCommands_GetStanceByName(defaultConfig, stanceType, defaultConfig->pchSpawnStance[stanceType]);

	if(!pStance)
		pStance = defaultConfig->StanceLists[stanceType][0];
	
	PetCommands_SetPetStance(pOwner, pPetEnt, stanceType, pStance);
}

void PetCommands_SetDefaultPetStances(SA_PARAM_NN_VALID Entity* pOwner, Entity *pPetEnt)
{
	PetCommandConfig* defaultConfig = GET_REF(defaultConfigRef);
	S32 i;
	
	if(!pPetEnt || !defaultConfig)
		return;
	
	for (i = 0; i < PetStanceType_COUNT; i++)
	{
		PetStance *pStance = NULL;

		if( !eaSize(&defaultConfig->StanceLists[i]) )
			continue;

		if (defaultConfig->pchSpawnStance[i])
			pStance = PetCommands_GetStanceByName(defaultConfig, i, defaultConfig->pchSpawnStance[i]);
		
		if(!pStance)
			pStance = defaultConfig->StanceLists[i][0];
		
		PetCommands_SetPetStance(pOwner, pPetEnt, (PetStanceType)i, pStance);
	}
}
void PetCommands_RemoveAllCommandsTargetingEnt(Entity* e)
{
	MapState *state = e ? mapState_FromEnt(e) : NULL;	

	if(!state)
		return;

	mapState_ClearAllTeamCommandsTargetingEnt(state, e);
	mapState_ClearAllPlayerCommandsTargetingEnt(state, e);
}

void PetCommands_Targeting_Cleanup( SA_PARAM_NN_VALID Entity* pEnt )
{
	MapState *state = pEnt ? mapState_FromEnt(pEnt) : NULL;

	if(!state)
		return;
	
	mapState_ClearAllCommandsForPlayer(state, pEnt);

	if ( team_IsMember(pEnt) )
	{
		mapState_ClearAllCommandsForTeam(state, pEnt->pTeam->iTeamID, pEnt);
	}
}

static void PetCommands_SetMyTarget_Cleanup( Entity *pEnt, bool bPeriodic )
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	MapState *state = mapState_FromPartitionIdx(iPartitionIdx);
	S32 i;

	if(!state)
		return;
	
	if ( bPeriodic )
	{
		static U32 uiLastTime = 0;
		U32 uiCurrTime = timeSecondsSince2000();

		if ( uiCurrTime < uiLastTime + 3600 )
		{
			return; //only do this check once an hour, at most
		}

		uiLastTime = uiCurrTime;
	}

	if (state->pTeamValueData) 
	{
		for ( i = eaSize(&state->pTeamValueData->eaTeamValues) - 1; i >= 0; i-- )
		{
			state->pTeamValueData->eaTeamValues[i]->bHasTeamMembers = false;
		}
	}

	//clean-up per-player pet targeting data
	if (state->pPlayerValueData) 
	{
		for ( i = 0; i < eaSize(&state->pPlayerValueData->eaPlayerValues); i++ )
		{
			PlayerMapValues *pPlayerValues = state->pPlayerValueData->eaPlayerValues[i];
			Entity* pPlayerEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pPlayerValues->iEntID);

			//if the player has a team, destroy all player-specific targeting data
			if ( pPlayerEnt && team_IsMember(pPlayerEnt) )
			{
				mapState_ClearAllCommandsForPlayerEx(state, pPlayerValues);

				if (state->pTeamValueData)
				{
					TeamMapValues *pTeamValues = eaIndexedGetUsingInt( &state->pTeamValueData->eaTeamValues, pPlayerEnt->pTeam->iTeamID );
					if ( pTeamValues )
					{
						pTeamValues->bHasTeamMembers = true;
					}
				}
			}
		}
	}

	if (state->pTeamValueData)
	{
		//clean-up team pet targeting data
		for ( i = eaSize(&state->pTeamValueData->eaTeamValues) - 1; i >= 0; i-- )
		{
			if ( !state->pTeamValueData->eaTeamValues[i]->bHasTeamMembers )
			{
				mapState_DestroyTeamValues(state, i);
			}
		}
	}
}


static void PetCommands_UpdatePlayerInfoAttackTarget(SA_PARAM_OP_VALID Entity *pOwner, Entity *pPetEnt, EntityRef erTarget, PetTargetType eType, bool bAddAsFirstTarget, bool onePerType)
{
	MapState *state;

	if(pOwner == NULL)
	{
		return;
	}

	ANALYSIS_ASSUME(pOwner != NULL);
	state = mapState_FromEnt(pOwner);

	if(state == NULL)
	{
		return;
	}

	mapState_UpdatePlayerInfoAttackTarget(state,pOwner,pPetEnt,erTarget,eType,bAddAsFirstTarget,onePerType);

	//Clean-up existing data
	PetCommands_SetMyTarget_Cleanup(pOwner, true);
}


static void PetCommands_PreferredTargetClearedCallback(Entity *e)
{
	Entity *pOwner;

	if (!e || !e->aibase)
		return;
	pOwner = entFromEntityRef(entGetPartitionIdx(e), e->erOwner);
		
	PetCommands_UpdatePlayerInfoAttackTarget(pOwner, e, 0, 0, false, true);
}


static void PetCommands_SetAttackTargetInternal(Entity *pPetEnt, Entity *pOwner, EntityRef erTarget, PetTargetType eType, bool bAddAsFirstTarget, bool onePerType)
{
	PetCommandConfig* defaultConfig = GET_REF(defaultConfigRef);
	int bForceAttack = true;
	Entity* pTarget = entFromEntityRef(entGetPartitionIdx(pOwner), erTarget);

	if (pPetEnt && eType == 0)
	{
		if (!PetCommands_IsControlledPet(pOwner, pPetEnt))
			return;

		if (defaultConfig)
		{
			bForceAttack = defaultConfig->setAttackTargetForcesAttack;
		}
		
		if (!aiSetPreferredAttackTarget(pPetEnt, pPetEnt->aibase, pTarget, bForceAttack))
		{	// failed to set the target for some reason, clear it out
			erTarget = 0;
		}

		aiSetPreferredAttackTargetClearedCallback(PetCommands_PreferredTargetClearedCallback);
	}
	PetCommands_UpdatePlayerInfoAttackTarget(pOwner, pPetEnt, erTarget, eType, bAddAsFirstTarget, onePerType);
}

static bool PetCommands_IsTargetOutOfRangeOfControlledPets(Entity *pPlayer, Entity *pTarget)
{
	AITeam* pAITeam = aiTeamGetCombatTeam(pPlayer, pPlayer->aibase);

	if (pAITeam)
	{
		FOR_EACH_IN_EARRAY(pAITeam->members, AITeamMember, pMember)
		{
			if (!PetCommands_IsControlledPet(pPlayer, pMember->memberBE))
				continue;
						
			if (!aiIsTargetWithinLeash(pMember->memberBE, pMember->memberBE->aibase, pTarget, 0.f))
			{
				return true;
			}
		}
		FOR_EACH_END
	}

	return false;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_PRIVATE;
void PetCommands_EnterCombat(SA_PARAM_NN_VALID Entity *pOwner)
{
	Entity** pPetList = NULL;
	int i, j;
	Team* currentTeam = NULL;
	AITeamMember** eaEntitiesWillingToInitiate = NULL;
	int iPartitionIdx = entGetPartitionIdx(pOwner);

	// TODO(RP): Make the combat roles give the right target for the given pet
	// for now just assign it any one of the targets and the next combat tick it will 
	// choose the right target

	Entity* pTank,* pKill,* pControl, *pTargetEnt = NULL;
	pKill = entFromEntityRef(iPartitionIdx, PetCommands_GetLowestIndexEntRefByLuckyCharm(pOwner, kPetTargetType_Kill));
	pTank = entFromEntityRef(iPartitionIdx, PetCommands_GetLowestIndexEntRefByLuckyCharm(pOwner, kPetTargetType_Tank));
	pControl = entFromEntityRef(iPartitionIdx, PetCommands_GetLowestIndexEntRefByLuckyCharm(pOwner, kPetTargetType_Control));
	
	if (pKill) 
	{
		pTargetEnt = pKill;
	}
	else if (pTank) 
	{
		pTargetEnt = pTank;
	}
	else if (pControl)
	{
		pTargetEnt = pControl;
	}
	else if (pOwner->pChar)
	{
		pTargetEnt = entFromEntityRef(iPartitionIdx, pOwner->pChar->currentTargetRef);
		if (!pTargetEnt)
			return;
	}
	else 
	{
		return;
	}

	if (PetCommands_IsTargetOutOfRangeOfControlledPets(pOwner, pTargetEnt))
	{
		char* estrBuffer = NULL;
		entFormatGameMessageKey(pOwner, &estrBuffer, PETCOMMAND_NOTIFYMSG_OUTOFRANGE, 
								STRFMT_STRING("Name", entGetLangName(pTargetEnt, entGetLanguage(pOwner))), 
								STRFMT_END);
		ClientCmd_NotifySend(pOwner, kNotifyType_ControlledPetFeedback, estrBuffer, NULL, NULL);
		estrDestroy(&estrBuffer);
		return;
	}

	//check all AITeams for entities who are willing to CHARGE!
	if (team_IsWithTeam(pOwner) && pTank)
	{
		currentTeam = team_GetTeam(pOwner);
		if (!currentTeam) return;
		for (i = 0; i < eaSize(&currentTeam->eaMembers); i++)
		{
			Entity* pMember = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, currentTeam->eaMembers[i]->iEntID);
			AITeam* pAITeam = NULL;
			AICombatRolesDef* pCombatRolesDef = NULL;

			if (!pMember)
				continue;

			pAITeam = aiTeamGetCombatTeam(pMember, pMember->aibase);
			pCombatRolesDef = GET_REF(pAITeam->combatRoleInfo.hCombatRolesDef);

			for (j = 0; j < eaSize(&pAITeam->members); j++)
			{
				AICombatRole* pRole = aiCombatRolesDef_FindRoleByName(pCombatRolesDef, SAFE_MEMBER4(pAITeam, members[j], memberBE, aibase, pchCombatRole));
				if (pRole && !pRole->bUnwillingToInitiateCombat)
					eaPushUnique(&eaEntitiesWillingToInitiate, pAITeam->members[j]);
			}
		}
	}
	else if (pTank)
	{
 		AITeam* pAITeam = aiTeamGetCombatTeam(pOwner, pOwner->aibase);
		if (pAITeam)
		{
			AICombatRolesDef* pCombatRolesDef = GET_REF(pAITeam->combatRoleInfo.hCombatRolesDef);

			for (j = 0; j < eaSize(&pAITeam->members); j++)
			{
				AICombatRole* pRole = aiCombatRolesDef_FindRoleByName(pCombatRolesDef, SAFE_MEMBER4(pAITeam, members[j], memberBE, aibase, pchCombatRole));
				if (pRole && !pRole->bUnwillingToInitiateCombat)
					eaPushUnique(&eaEntitiesWillingToInitiate, pAITeam->members[j]);
			}
		}
	}
 
	if (eaSize(&eaEntitiesWillingToInitiate) != 0)
	{
		//If we found some entities willing to initiate, tell them to do so.
		AIPowerRateOutput powerRating = {0};
		for (i = 0; i < eaSize(&eaEntitiesWillingToInitiate); i++)
		{
			Entity* pEnt = eaEntitiesWillingToInitiate[i]->memberBE;
			aiPowersGetBestPowerForTarget(pEnt, pTank, kPowerAITag_Attack, true, &powerRating);
			if (powerRating.targetPower)
			{
				aiMultiTickAction_QueuePower(pEnt, pEnt->aibase, pTank, AI_POWER_ACTION_USE_POWINFO, 
											powerRating.targetPower, 
											(MTAFlag_FORCEUSETARGET | MTAFlag_USERFORCEDACTION), 
											NULL);
			}
		}
	}
	else
	{
		//if none exist, just use the old behavior.
		if (team_IsWithTeam(pOwner))
		{
			currentTeam = team_GetTeam(pOwner);
			if (!currentTeam) return;
			for (i = 0; i < eaSize(&currentTeam->eaMembers); i++)
			{
				Entity* pMember = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, currentTeam->eaMembers[i]->iEntID);

				eaClear(&pPetList);
				PetCommands_GetAllPets(pMember, &pPetList);

				for (j = 0; j < eaSize(&pPetList); j++)
				{
					if (!PetCommands_IsControlledPet(pMember, pPetList[j]))
						return;

					aiSetAttackTarget(pPetList[j], pPetList[j]->aibase, pTargetEnt, NULL, 1);
				}
			}
		}
		else
		{
			PetCommands_GetAllPets(pOwner, &pPetList);
		
			for (i = 0; i < eaSize(&pPetList); i++)
			{
				if (!PetCommands_IsControlledPet(pOwner, pPetList[i]))
					return;

				aiSetAttackTarget(pPetList[i], pPetList[i]->aibase, pTargetEnt, NULL, 1);
				
			}
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_PRIVATE;
void PetCommands_ClearAllPlayerAttackTargets(Entity *pOwner)
{
	MapState *state = pOwner ? mapState_FromEnt(pOwner) : NULL;

	if ( state==NULL || pOwner == NULL )
	{
		return;
	}

	if ( pOwner)
	{
		AITeam *pTeam = aiTeamGetCombatTeam(pOwner, pOwner->aibase);

		// Clear all preferred targets
		aiCombatRole_ClearAllPreferredTargets(pTeam);

		mapState_ClearAllCommandsForOwner(state,pOwner);
	}
	//Clean-up existing data
	PetCommands_SetMyTarget_Cleanup(pOwner, true);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_PRIVATE;
void PetCommands_ClearAttackTarget(SA_PARAM_NN_VALID Entity *pOwner, EntityRef erPet)
{
	Entity *pPetEnt = entFromEntityRef(entGetPartitionIdx(pOwner), erPet);

	if (pOwner && pOwner->pChar && pPetEnt && pPetEnt->aibase)
	{
		PetCommands_SetAttackTargetInternal(pPetEnt, pOwner, 0, 0, false, true);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void PetCommands_SetAllToOwnerAttackTarget(Entity *pOwner)
{
	if (pOwner && pOwner->pChar)
	{
		Entity** ppPets = NULL;
		PetCommands_GetAllPets(pOwner, &ppPets);

		FOR_EACH_IN_EARRAY(ppPets, Entity, pPet);
			PetCommands_SetAttackTargetInternal(pPet, pOwner, pOwner->pChar->currentTargetRef, 0, false, true);
		FOR_EACH_END;

		eaDestroy(&ppPets);
	}
}

// Sets the pet's attack target.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void PetCommands_SetAttackTarget(Entity *pOwner, EntityRef erPet, EntityRef erTarget)
{
	Entity *pPetEnt = entFromEntityRef(entGetPartitionIdx(pOwner), erPet);

	if (pOwner && pOwner->pChar && pPetEnt && pPetEnt->aibase)
	{
		PetCommands_SetAttackTargetInternal(pPetEnt, pOwner, erTarget, 0, false, true);
	}
}

// Sets the pet's attack target.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void PetCommands_SetLuckyCharmOnAllMatchingTargets(Entity *pOwner, EntityRef erTarget, PetTargetType eType, bool onePerType)
{
	Entity* pEnt = entFromEntityRef(entGetPartitionIdx(pOwner), erTarget);
	Critter* pCritter = pEnt ? pEnt->pCritter : NULL;
	bool alreadyCleared = false;
	if (pOwner && pOwner->pChar && pCritter)
	{
		GameEncounter* pEncounter = pCritter->encounterData.pGameEncounter;
		if (pEncounter)
		{
			GameEncounterPartitionState *pState = encounter_GetPartitionState(entGetPartitionIdx(pOwner), pEncounter);
			FOR_EACH_IN_EARRAY(pState->eaEntities, Entity, pEncounterEntity)
			{
				if (pEncounterEntity && pEncounterEntity->pCritter)
				{
					if (REF_COMPARE_HANDLES(pEncounterEntity->pCritter->critterDef, pCritter->critterDef))
					{
						PetCommands_SetAttackTargetInternal(NULL, pOwner, pEncounterEntity->myRef, eType, false, onePerType && !alreadyCleared);
						//onePerType should only apply to the first call of SetAttackTargetInternal.
						alreadyCleared = true;
					}
				}
			}
			FOR_EACH_END;
		}
	}
}

// Sets the pet's attack target.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void PetCommands_SetLuckyCharm(Entity *pOwner, EntityRef erTarget, PetTargetType eType, bool bAddAsFirstTarget, bool onePerType)
{
	if (pOwner && pOwner->pChar)
	{
		PetCommands_SetAttackTargetInternal(NULL, pOwner, erTarget, eType, bAddAsFirstTarget, onePerType);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void PetCommands_SetAllToFollowOwner(Entity *pOwner)
{
	if (pOwner && pOwner->pChar)
	{
		Entity** ppPets = NULL;
		PetCommands_GetAllPets(pOwner, &ppPets);

		FOR_EACH_IN_EARRAY(ppPets, Entity, pPet);
			aiSetLeashTypeOwner(pPet, pPet->aibase);
		FOR_EACH_END;

		eaDestroy(&ppPets);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void PetCommands_SetFollowOwner(Entity *pOwner, EntityRef erPet)
{
	Entity *pPetEnt = entFromEntityRef(entGetPartitionIdx(pOwner), erPet);

	if (pOwner && pOwner->pChar && pPetEnt && pPetEnt->aibase)
	{
		if (!PetCommands_IsControlledPet(pOwner, pPetEnt))
			return;

		aiSetLeashTypeOwner(pPetEnt, pPetEnt->aibase);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void PetCommands_SetAllToHoldPosition(Entity *pOwner)
{
	if (pOwner && pOwner->pChar)
	{
		Vec3 vHoldPos;
		Entity** ppPets = NULL;
		PetCommands_GetAllPets(pOwner, &ppPets);

		FOR_EACH_IN_EARRAY(ppPets, Entity, pPet);
			entGetPos(pPet, vHoldPos);
			aiSetLeashTypePos(pPet, pPet->aibase, vHoldPos);
		FOR_EACH_END;

		eaDestroy(&ppPets);
	}
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void PetCommands_SetHoldPosition(Entity *pOwner, EntityRef erPet)
{
	Entity *pPetEnt = entFromEntityRef(entGetPartitionIdx(pOwner), erPet);

	if (pOwner && pOwner->pChar && pPetEnt && pPetEnt->aibase)
	{
		Vec3 vHoldPos;
		if (!PetCommands_IsControlledPet(pOwner, pPetEnt))
			return;

		entGetPos(pPetEnt, vHoldPos);
		aiSetLeashTypePos(pPetEnt, pPetEnt->aibase, vHoldPos);
	}
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void PetCommands_SetRallyPosition(Entity *pOwner, EntityRef erPet, const Vec3 vPosition)
{
	Entity *pPetEnt = entFromEntityRef(entGetPartitionIdx(pOwner), erPet);

	if (pOwner && pOwner->pChar && pPetEnt && pPetEnt->aibase)
	{
		if (!PetCommands_IsControlledPet(pOwner, pPetEnt))
			return;

		aiMultiTickAction_ClearQueue(pPetEnt, pPetEnt->aibase);
		aiSetLeashTypePos(pPetEnt, pPetEnt->aibase, vPosition);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void PetCommands_ClearRallyPosition(Entity *pOwner, EntityRef erPet)
{
	Entity *pPetEnt = entFromEntityRef(entGetPartitionIdx(pOwner), erPet);

	if (pOwner && pOwner->pChar && pPetEnt && pPetEnt->aibase)
	{
		if (!PetCommands_IsControlledPet(pOwner, pPetEnt))
			return;

		aiSetLeashTypeOwner(pPetEnt, pPetEnt->aibase);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void PetCommands_ClearAllRallyPositions(Entity *pOwner)
{
	if (pOwner && pOwner->pPlayer)
	{
		FOR_EACH_IN_EARRAY(pOwner->pPlayer->petInfo, PlayerPetInfo, pPetInfo)
			PetCommands_ClearRallyPosition(pOwner, pPetInfo->iPetRef);
		FOR_EACH_END
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_HIDE;
void PetCommands_RequestResurrection(Entity *pOwner)
{
	if (pOwner && pOwner->pPlayer && pOwner->aibase->team)
	{
		aiTeamRequestResurrectForMember( pOwner->aibase->team, pOwner);
	}
}

static PlayerPetInfo* PetCommands_FindPetInfo(Entity* pOwner, EntityRef entRef)
{
	FOR_EACH_IN_EARRAY(pOwner->pPlayer->petInfo, PlayerPetInfo, pPetInfo)
		if (pPetInfo->iPetRef == entRef)
			return pPetInfo;
	FOR_EACH_END

	return NULL;
}

static void PetActionClearedCallback(Entity *e, Power *power, int bPowerUsed)
{
	Entity *pOwner = entFromEntityRef(entGetPartitionIdx(e), e->erOwner);
	if (pOwner && pOwner->pPlayer && power)
	{
		PowerDef *pDef = GET_REF(power->hDef);
		if (pDef)
		{
			PetPowerState *pState = player_GetPetPowerState(pOwner->pPlayer, entGetRef(e), pDef);
			if (pState && pState->bQueuedForUse)
			{
				pState->bQueuedForUse = false;
				entity_SetDirtyBit(pOwner, parse_Player, pOwner->pPlayer, true);
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void PetCommands_UsePowerForPet(Entity* pOwner, EntityRef entRef, const char *pchName) 
{
	if (pOwner && pOwner->pChar && pOwner->pPlayer)
	{
		PlayerPetInfo *pPetInfo = PetCommands_FindPetInfo(pOwner, entRef);
		PetPowerState *pPowerState;
		Entity *pPetEnt;
		if (!pPetInfo)
			return;

		pPetEnt = entFromEntityRef(entGetPartitionIdx(pOwner), entRef);
		if (!pPetEnt || !entIsAlive(pPetEnt))
			return;

		pPowerState = playerPetInfo_FindPetPowerStateByName(pPetInfo, pchName);
		if (!pPowerState)
			return; // can't find

		// call the AI force use power function
		if (aiPowerForceUsePower(pPetEnt, pchName, PetActionClearedCallback))
		{
			// success on queuing the power for use
			pPowerState->bQueuedForUse = true;
			entity_SetDirtyBit(pOwner, parse_Player, pOwner->pPlayer, true);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void PetCommands_UsePowerForAllPets(Entity* pOwner, const char *pchName) 
{
	if (pOwner && pOwner->pChar && pOwner->pPlayer)
	{
		PetPowerState *pPowerState;
		Entity *pPetEnt;

		FOR_EACH_IN_EARRAY(pOwner->pPlayer->petInfo, PlayerPetInfo, pPetInfo)
			if (!pPetInfo)
				continue;

			pPetEnt = entFromEntityRef(entGetPartitionIdx(pOwner), pPetInfo->iPetRef);
			if (!pPetEnt || !entIsAlive(pPetEnt))
				continue;

			pPowerState = playerPetInfo_FindPetPowerStateByName(pPetInfo, pchName);
			if (!pPowerState)
				continue; // can't find

			// call the AI force use power function
			if (aiPowerForceUsePower(pPetEnt, pchName, PetActionClearedCallback))
			{
				// success on queuing the power for use
				pPowerState->bQueuedForUse = true;
				entity_SetDirtyBit(pOwner, parse_Player, pOwner->pPlayer, true);
			}
		FOR_EACH_END
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void PetCommands_StopPowerUse(Entity* pOwner, EntityRef entRef, const char *pchName)
{
	if (pOwner && pOwner->pChar && pOwner->pPlayer)
	{
		PlayerPetInfo *pPetInfo = PetCommands_FindPetInfo(pOwner, entRef);
		PetPowerState *pPowerState;
		Entity *pPetEnt;
		if (!pPetInfo)
			return;

		pPetEnt = entFromEntityRef(entGetPartitionIdx(pOwner), entRef);
		if (!pPetEnt || !entIsAlive(pPetEnt))
			return;

		pPowerState = playerPetInfo_FindPetPowerStateByName(pPetInfo, pchName);
		if (!pPowerState)
			return; // can't find

		if (pPowerState->bQueuedForUse)
		{
			AIPowerInfo *pAIPowInfo = aiPowersFindInfo(pPetEnt, pPetEnt->aibase, pchName);
			
			aiMultiTickAction_RemoveQueuedAIPowerInfos(pPetEnt, pPetEnt->aibase, pAIPowInfo);

			pPowerState->bQueuedForUse = false;
			entity_SetDirtyBit(pOwner, parse_Player, pOwner->pPlayer, true);
		}
	}
}

void PetCommands_RespawnPets(Entity *pOwner)
{
	if (pOwner && pOwner->pPlayer)
	{
		PetCommandConfig* defaultConfig = GET_REF(defaultConfigRef);
		Entity** ppPets = NULL;
		bool bRallyPointReset = false;

		PetCommands_GetAllPets(pOwner, &ppPets);
		
		FOR_EACH_IN_EARRAY(ppPets, Entity, pPetEnt)
		{
			//Don't respawn critter entities (Like EntCreates)
			if(!pPetEnt || 
				(entGetType(pPetEnt) == GLOBALTYPE_ENTITYCRITTER &&
				 !entCheckFlag(pPetEnt, ENTITYFLAG_CRITTERPET)))
				continue;

			if(pPetEnt->pChar && pPetEnt->pChar->pattrBasic)
			{
				character_ResetPartial(entGetPartitionIdx(pPetEnt), pPetEnt->pChar, pPetEnt, 
										true, true, true, true, false, true, NULL);
				pPetEnt->pChar->pattrBasic->fHitPoints = pPetEnt->pChar->pattrBasic->fHitPointsMax;
				pPetEnt->pChar->pattrBasic->fPower = pPetEnt->pChar->pattrBasic->fPowerEquilibrium;
				character_DirtyAttribs(pPetEnt->pChar);
			}


			if(aiGetLeashType(pPetEnt->aibase) == AI_LEASH_TYPE_RALLY_POSITION)
			{
				aiSetLeashTypeOwner(pPetEnt, pPetEnt->aibase);
				bRallyPointReset = true;
			}

			aiResetForRespawn(pPetEnt);

			if(defaultConfig)
			{
				S32 i;

				// only reset the pet state if we have a valid spawn state
				if(defaultConfig->pchSpawnState)
				{
					PetCommands_SetDefaultPetState(pOwner, pPetEnt);
				}
				
				for (i = 0; i < PetStanceType_COUNT; i++)
				{
					if(!defaultConfig->pchSpawnStance[i])
						continue;
					PetCommands_SetDefaultPetStance(pOwner, pPetEnt, i);
				}
			}
		}
		FOR_EACH_END
			
		eaDestroy(&ppPets);

		if (bRallyPointReset)
		{
			ClientCmd_PetCommands_ServerRemovedRallyPoints(pOwner);
		}
	}
}

static void DismissAllPetsEx(Entity *pEnt, S32 bDeadOnly)
{
	// go through the AITeam and for every entity that is owned by the given entity, dismiss the pet
	AITeam *pAITeam = aiTeamGetAmbientTeam(pEnt, pEnt->aibase);
	if (pAITeam)
	{
		FOR_EACH_IN_EARRAY(pAITeam->members, AITeamMember, pMember)
		{
			Entity *pMemberEnt = pMember->memberBE;
			if (pMemberEnt && pMemberEnt->erOwner == entGetRef(pEnt) && 
				(!bDeadOnly || !entIsAlive(pMemberEnt)) )
			{
				DismissPetEx(pEnt, pMemberEnt);
			}
		}
		FOR_EACH_END
	}
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(DismissAllPets);
void exprFuncDismissAllPets(ACMD_EXPR_SELF Entity *pEnt)
{
	// go through the AITeam and for every entity that is owned by the given entity, dismiss the pet
	if (pEnt && pEnt->aibase)
	{
		DismissAllPetsEx(pEnt, false);
	}
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(DismissAllDeadPets);
void exprFuncDismissAllDeadPets(ACMD_EXPR_SELF Entity *pEnt)
{
	if (pEnt && pEnt->aibase)
	{
		DismissAllPetsEx(pEnt, true);
	}
}

// For player entities only- resets all pets 
AUTO_EXPR_FUNC(ai) ACMD_NAME(ResetAllPets);
void exprFuncResetAllPets(ACMD_EXPR_SELF Entity *pEnt)	
{
	PetCommands_RespawnPets(pEnt);
}

#include "gslPetCommand_c_ast.c"
