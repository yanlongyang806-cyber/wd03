#include "aiCombatRoles.h"

#include "aiCombatRoles_h_ast.h"
#include "aiConfig.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiStructCommon.h"
#include "aiStruct.h"
#include "aiTeam.h"
#include "file.h"
#include "gslMapState.h"
#include "mapstate_common.h"
#include "MemoryPool.h"
#include "ResourceManager.h"
#include "StringCache.h"
#include "StateMachine.h"
#include "TextParserEnums.h"
#include "WorldGrid.h"

static void aiCombatRole_SetupTeamGuards(AITeam *pTeam);
static void aiCombatRole_SetupSharedAggro(AITeam *pTeam);
static int aiCombatRoles_Validate(enumResourceValidateType eType, const char* pDictName, 
								   const char* pResourceName, void* pResource, U32 userID);
static void aiCombatRole_DoLuckyCharmAssignments(AITeam *pTeam);

static AICombatFormationRoleDef* aiCombatFormation_GetFormationRoleByName(const AICombatFormationDef *pFormation, const char *pchName);

static S32 s_bUseBestFormationPositions = true;

typedef struct AICombatRolesGuardSlot
{
	Vec3	vGuardOffset;
	Quat	qFacingRot;

	U32		bIsValid : 1;
	U32		bIsTaken : 1;
} AICombatRolesGuardSlot;

struct 
{
	bool bDidStartup;
} s_aiCombatRoles = {0};

// ---------------------------------------------------------------------------------------------------
MP_DEFINE(AICombatRoleTeamMember);
MP_DEFINE(AICombatRolesGuardSlot);
MP_DEFINE(AICombatRolesTeamRole);
MP_DEFINE(AICombatRoleFormationSlot);
MP_DEFINE(AICRole_AssignedTarget);
MP_DEFINE(AICombatRoleAIVars);

// ---------------------------------------------------------------------------------------------------
static AICombatRolesGuardSlot* aiMemberGuardSlot_Alloc()
{
	MP_CREATE(AICombatRolesGuardSlot, 16);

	return MP_ALLOC(AICombatRolesGuardSlot);
}

void aiMemberGuardSlot_Free(AICombatRolesGuardSlot* slot)
{
	if (slot)
		MP_FREE(AICombatRolesGuardSlot, slot);
}

static AICombatRoleTeamMember* aiTeamMemberCombatRole_Alloc()
{
	MP_CREATE(AICombatRoleTeamMember, 16);

	return MP_ALLOC(AICombatRoleTeamMember);
}

void aiTeamMemberCombatRole_Free(AICombatRoleTeamMember* role)
{
	if (role)
	{
		eaDestroyEx(&role->eaGuardSlots, aiMemberGuardSlot_Free);
		MP_FREE(AICombatRoleTeamMember, role);
	}
}

static AICombatRolesTeamRole* aiCombatRolesTeamRole_Alloc()
{
	MP_CREATE(AICombatRolesTeamRole, 16);
	return MP_ALLOC(AICombatRolesTeamRole);
}

static void aiFormationSlots_Free(AICombatRoleFormationSlot *info);

static void aiCombatRolesTeamRole_Free(AICombatRolesTeamRole *info)
{
	if (info)
	{
		eaDestroyEx(&info->eaFormationSlots, aiFormationSlots_Free);
		MP_FREE(AICombatRolesTeamRole, info);
	}
}

static AICombatRoleFormationSlot* aiFormationSlots_Alloc()
{
	MP_CREATE(AICombatRoleFormationSlot, 16);
	return MP_ALLOC(AICombatRoleFormationSlot);
}

static void aiFormationSlots_Free(AICombatRoleFormationSlot *info)
{
	if (info)
	{
		MP_FREE(AICombatRoleFormationSlot, info);
	}
}

static AICRole_AssignedTarget* aiCRole_AssignedTarget_Alloc()
{
	MP_CREATE(AICRole_AssignedTarget, 16);
	return MP_ALLOC(AICRole_AssignedTarget);
}

static void aiCRole_AssignedTarget_Free(AICRole_AssignedTarget *p)
{
	if (p)
	{
		MP_FREE(AICRole_AssignedTarget, p);
	}
}

static AICombatRoleAIVars* aiCombatRoleAIVars_Alloc()
{
	MP_CREATE(AICombatRoleAIVars, 128);
	return MP_ALLOC(AICombatRoleAIVars);
}

static void aiCombatRoleAIVars_Free(AICombatRoleAIVars *p)
{
	if (p)
	{	// these should be released prior to freeing this struct
		devassert(p->combatFSMContext == NULL);
		devassert(p->combatRoleConfigMods == NULL);
		MP_FREE(AICombatRoleAIVars, p);
	}
}

// ---------------------------------------------------------------------------------------------------
void aiCombatRoles_Startup()
{
	assert(s_aiCombatRoles.bDidStartup == false);

	s_aiCombatRoles.bDidStartup = true;
	RefSystem_RegisterSelfDefiningDictionary(g_pcAICombatRolesDictName, false, parse_AICombatRolesDef, true, false, NULL);
	resDictManageValidation(g_pcAICombatRolesDictName, aiCombatRoles_Validate);

	if (isDevelopmentMode() || isProductionEditMode()) {
		resDictMaintainInfoIndex(g_pcAICombatRolesDictName, ".Name", NULL, NULL, NULL, NULL);
	}

	resLoadResourcesFromDisk(g_pcAICombatRolesDictName, "ai/combatRole", ".crole", "AICombatRoles.bin", PARSER_FORCEREBUILD | RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	
	resDictProvideMissingResources(g_pcAICombatRolesDictName);
	resDictProvideMissingRequiresEditMode(g_pcAICombatRolesDictName);
}

// ---------------------------------------------------------------------------------------------------
__forceinline static int aiCombatRole_DoesRoleShareAggro(AICombatRole *pRole, const char *pchRole)
{
	S32 idx = eaFind(&pRole->ppchShareAggroWithRole, pchRole);
	return idx != -1;
}

// ---------------------------------------------------------------------------------------------------
static int aiCombatRoles_Process(AICombatRolesDef* rolesDef)
{
	FOR_EACH_IN_EARRAY(rolesDef->eaRoles, AICombatRole, pRole)
	{
		if (!pRole->pchName)
		{
			ErrorFilenamef(rolesDef->pchFilename,"CombatRole Role's name must be specified.");
			return 0;
		}

		if (pRole->pchRoleToGuard == pRole->pchName)
		{
			ErrorFilenamef(rolesDef->pchFilename,"CombatRole Roles are not allowed to guard themselves (If you really need this, ask).");
			return 0;
		}

		FOR_EACH_IN_EARRAY(pRole->configMods, AIConfigMod, pMod)
		{
			if(!aiConfigCheckSettingName(pMod->setting))
				ErrorFilenamef(rolesDef->pchFilename, "ConfigMod setting %s for Role %s is not valid.", pMod->setting, pRole->pchName);
		}
		FOR_EACH_END

	}
	FOR_EACH_END


	return 1;
}


// ---------------------------------------------------------------------------------------------------
static int aiCombatRoles_Validate(enumResourceValidateType eType, const char* pDictName, const char* pResourceName, void* pResource, U32 userID)
{
	AICombatRolesDef* combatRolesDef = pResource;

	switch (eType)
	{
		case RESVALIDATE_POST_TEXT_READING:
			aiCombatRoles_Process(combatRolesDef);
			return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}


// ---------------------------------------------------------------------------------------------------
AICombatRolesDef* aiCombatRolesDef_GetDef(const char *pchDefName)
{
	if (pchDefName)
	{
		AICombatRolesDef *pDef = RefSystem_ReferentFromString("AICombatRolesDef", pchDefName);
		return pDef;
	}

	return NULL;
}


// ---------------------------------------------------------------------------------------------------
__forceinline static AICombatRole* _findRoleByName(AICombatRolesDef* pDef, const char *pchName)
{
	FOR_EACH_IN_EARRAY(pDef->eaRoles, AICombatRole, pRole)
	{
		if (pRole->pchName == pchName)
			return pRole;
	}
	FOR_EACH_END
	return NULL;
}


// ---------------------------------------------------------------------------------------------------
AICombatRole* aiCombatRolesDef_FindRoleByName(AICombatRolesDef* pDef, const char *pchName)
{
	if (pDef)
	{
		pchName = allocFindString(pchName);

		return _findRoleByName(pDef, pchName);
	}

	return NULL;
}

// ---------------------------------------------------------------------------------------------------
void aiCombatRole_ApplyRoleConfigMods(Entity *e, AIVarsBase* aib, const AICombatRole *pRole)
{
	AICombatRoleAIVars *pRoleVars = aib->combatRoleVars;
	if (pRoleVars)
	{
		aiConfigMods_ApplyConfigMods(e, pRole->configMods, &pRoleVars->combatRoleConfigMods);
	}
}

// ---------------------------------------------------------------------------------------------------
static AICombatRolesDef* _getCombatRolesDefFile(AITeam* pTeam, AICombatRolesDef* pCombatRoleDefFile)
{
	if (!pTeam)
	{
		return pCombatRoleDefFile;
	}
	else
	{
		AICombatRolesDef *pTeamCombatRoleDefFile = aiTeamGetCombatRolesDef(pTeam);
		if (!pTeamCombatRoleDefFile)
			return pCombatRoleDefFile;

		if (pCombatRoleDefFile && pCombatRoleDefFile != pTeamCombatRoleDefFile)
			return NULL;

		return pTeamCombatRoleDefFile;
	}
}

// ---------------------------------------------------------------------------------------------------
static void aiCombatRoleAIVars_SetCombatFSM(AIVarsBase *aib, const char *pchCombatFSM)
{
	// delete the old FSM override if it exists 
	if (aib->combatRoleVars->combatFSMContext)
	{
		if (aib->currentCombatFSMContext == aib->combatRoleVars->combatFSMContext)
		{
			aib->currentCombatFSMContext = NULL;
		}

		fsmContextDestroy(aib->combatRoleVars->combatFSMContext);
		aib->combatRoleVars->combatFSMContext = NULL;
	}

	if (pchCombatFSM)
	{
		aib->combatFSMContext = fsmContextCreateByName(pchCombatFSM, NULL);
		if (aib->combatFSMContext)
		{
			aib->combatFSMContext->messages = aib->fsmMessages;
			aiSetCurrentCombatFSMContext(aib, true);
		}
	}
	else
	{
		aiSetCurrentCombatFSMContext(aib, false);
	}
}

// ---------------------------------------------------------------------------------------------------
void aiCombatRole_SetCombatRole(Entity *e, 
								AIVarsBase *aib, 
								const char *pchNewCombatRole,
								AICombatRolesDef* pCombatRoleDefFile)
{

	const char* pchOldCombatRole = NULL;
	AICombatRole *pNewRole;

	if (pchNewCombatRole)
		pchNewCombatRole = allocFindString(pchNewCombatRole);

	if (aib->pchCombatRole == pchNewCombatRole && !aib->combatRolesDirty)
		return; // already have this combat role

	if (!aib->combatRoleVars)
		aib->combatRoleVars = aiCombatRoleAIVars_Alloc();

	pchOldCombatRole = aib->pchCombatRole;
	aib->pchCombatRole = pchNewCombatRole;

	if (!pchNewCombatRole)
	{	
		aiCombatRole_ApplyRoleConfigMods(e, aib, NULL);
		aiCombatRoleAIVars_SetCombatFSM(aib, NULL);
		return;
	}

	pCombatRoleDefFile = _getCombatRolesDefFile(aiTeamGetAmbientTeam(e, aib), pCombatRoleDefFile);
	if (!pCombatRoleDefFile)
	{
		// todo: set dirty flag on the combat role stuff, 
		// we can't do anything without our team's combat role def
		aib->combatRolesDirty = true;
		return;
	}

	aib->combatRolesDirty = false;

	pNewRole = _findRoleByName(pCombatRoleDefFile, pchNewCombatRole);
	
	// config mods
	aiCombatRole_ApplyRoleConfigMods(e, aib, pNewRole);
		
	// FSM override
	if (pNewRole->pchCombatFSMOverride)
	{
		AICombatRole *pOldCombatRole = pchOldCombatRole ? 
											_findRoleByName(pCombatRoleDefFile, pchOldCombatRole) : NULL;

		if (!pOldCombatRole || pOldCombatRole->pchCombatFSMOverride != pNewRole->pchCombatFSMOverride)
		{	// this is a new FSM
			aiCombatRoleAIVars_SetCombatFSM(aib, pNewRole->pchCombatFSMOverride);
		}
	}
	else
	{
		aiCombatRoleAIVars_SetCombatFSM(aib, NULL);
	}
}	

// ---------------------------------------------------------------------------------------------------
void aiCombatRole_DestroyCombatRoleVars(Entity *e, AIVarsBase *aib)
{
	if (!aib->combatRoleVars)
		return;

	// kill the FSM
	if (aib->combatRoleVars->combatFSMContext)
	{
		fsmContextDestroy(aib->combatRoleVars->combatFSMContext);
		aib->combatRoleVars->combatFSMContext = NULL;
	}

	// remove the config mods
	aiConfigMods_RemoveConfigMods(e, aib->combatRoleVars->combatRoleConfigMods);
	eaiDestroy(&aib->combatRoleVars->combatRoleConfigMods);
	
	// free the struct
	aiCombatRoleAIVars_Free(aib->combatRoleVars);
	aib->combatRoleVars = NULL;
}



// ---------------------------------------------------------------------------------------------------
// when combat starts, sets up team guards and shared aggro ents
void aiCombatRole_SetupStartCombat(AITeam *pTeam)
{
	aiCombatRole_SetupSharedAggro(pTeam);
	aiCombatRole_SetupTeamGuards(pTeam);
	
}

// ---------------------------------------------------------------------------------------------------
static void aiCombatRole_DestroyTeamRoleInfo(AICombatRolesTeamInfo *pTeamInfo)
{
	FOR_EACH_IN_EARRAY(pTeamInfo->eaTeamRolesInfo, AICombatRolesTeamRole, pRole)
		aiCombatRolesTeamRole_Free(pRole);
	FOR_EACH_END
	eaDestroy(&pTeamInfo->eaTeamRolesInfo);

	{
		S32 i;
		for (i = 0; i < kPetTargetType_COUNT; i++)
		{
			if (pTeamInfo->aLuckyCharmInfo[i].eaAssignedTargets)
			{
				eaDestroyEx(&pTeamInfo->aLuckyCharmInfo[i].eaAssignedTargets, aiCRole_AssignedTarget_Free);
			}
		}

	}
}

// ---------------------------------------------------------------------------------------------------
static void aiCombatRole_SetupTeamRoleFormationInfo(AICombatRolesTeamRole* pRoleInfo, AICombatRolesDef *pDef)
{
	AICombatFormationRoleDef* pFormationRoleDef;
	pFormationRoleDef = aiCombatFormation_GetFormationRoleByName(pDef->pFormation, pRoleInfo->pchName);
	if (pFormationRoleDef)
	{
		// create a AICombatRoleFormationSlot for each slot in the def
		S32 i, count = eaSize(&pFormationRoleDef->eaSlots);
		for (i = 0; i < count; ++i)
		{
			AICombatRoleFormationSlot* pSlot = aiFormationSlots_Alloc();
			eaPush(&pRoleInfo->eaFormationSlots, pSlot);
		}
	}
}

// sets up the pTeamInfo based on the given AICombatRolesDef 
static void aiCombatRole_SetupTeamRoleInfo(AICombatRolesDef *pDef, AICombatRolesTeamInfo *pTeamInfo)
{
	FOR_EACH_IN_EARRAY(pDef->eaRoles, AICombatRole, pRole)
	{
		AICombatRolesTeamRole* pRoleInfo = aiCombatRolesTeamRole_Alloc();

		pRoleInfo->pchName = pRole->pchName;

		// allocate runtime formation data
		if (pDef->pFormation)
		{
			aiCombatRole_SetupTeamRoleFormationInfo(pRoleInfo, pDef);
		}
		
		eaPush(&pTeamInfo->eaTeamRolesInfo, pRoleInfo);
	}
	FOR_EACH_END

	// add a dummy role for those that do not have a defined one
	// pTeamInfo->dummyRole.pchName = "UndefRole";
	if (pDef->pFormation)
		aiCombatRole_SetupTeamRoleFormationInfo(&pTeamInfo->dummyRole, pDef);
}

// ---------------------------------------------------------------------------------------------------
// pchName: Assumed pooled string
// If the name is not found, then return the dummyRole 
__forceinline static AICombatRolesTeamRole* aiCombatRole_GetTeamRoleByName(AICombatRolesTeamInfo *pTeamInfo, const char *pchName)
{
	if (pchName)
	{
		FOR_EACH_IN_EARRAY(pTeamInfo->eaTeamRolesInfo, AICombatRolesTeamRole, pRole)
			if (pRole->pchName == pchName)
				return pRole;
		FOR_EACH_END
	}
	
	return &pTeamInfo->dummyRole;
}

// ---------------------------------------------------------------------------------------------------
void aiCombatRole_TeamSetCombatRolesDef(AITeam* team, const char *combatRolesDef)
{
	// first release any combat role definition stuff we have
	aiCombatRole_TeamReleaseCombatRolesDef(team);
	
	if (combatRolesDef)
	{
		AICombatRolesDef *pDef;
		SET_HANDLE_FROM_STRING("AICombatRolesDef", combatRolesDef, team->combatRoleInfo.hCombatRolesDef);
		
		pDef = aiTeamGetCombatRolesDef(team);
		if (!pDef)
			return;

		aiCombatRole_SetupTeamRoleInfo(pDef, &team->combatRoleInfo);
	}
}


// ---------------------------------------------------------------------------------------------------
// removes the handle to the combat roles def file, and then destroys the runtime team role info
// as well as freeing all AITeamMember combat role information
void aiCombatRole_TeamReleaseCombatRolesDef(SA_PARAM_NN_VALID AITeam* team)
{
	AICombatRolesTeamInfo *pTeamCombatRoleInfo = &team->combatRoleInfo;

	if (IS_HANDLE_ACTIVE(pTeamCombatRoleInfo->hCombatRolesDef))
	{
		REMOVE_HANDLE(pTeamCombatRoleInfo->hCombatRolesDef);
	}
	
	aiCombatRole_DestroyTeamRoleInfo(pTeamCombatRoleInfo);

	FOR_EACH_IN_EARRAY(team->members, AITeamMember, pMember)
	{
		aiTeamMemberCombatRole_Free(pMember->pCombatRole);
		pMember->pCombatRole = NULL;
	}
	FOR_EACH_END
}

// ---------------------------------------------------------------------------------------------------
AICombatRolesDef* aiTeamGetCombatRolesDef(SA_PARAM_NN_VALID AITeam* team)
{
	return GET_REF(team->combatRoleInfo.hCombatRolesDef);
}

// ---------------------------------------------------------------------------------------------------
// Finds the closest role that the given member is to guard, trying to split up guarders evenly based on
// the desiredNumGuards on the role
static void aiCombatRole_SetupGuard(AITeam *pTeam,
									AICombatRolesDef* pCombatRolesDef, 
									AITeamMember* pMember,
									AICombatRole* pRoleTemplate)
{
	F32 fClosest = FLT_MAX;
	AITeamMember *pBestMember = NULL;
	Vec3 vMemberPos;
	bool bBestRequiresMore = false;

	entGetPos(pMember->memberBE, vMemberPos);

	// go through the members and find the members with roles that the current role will guard
	// Find the closest one that still needs to be guarded
	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pOtherMember)
	{
		if (pOtherMember->memberBE->aibase->pchCombatRole == pRoleTemplate->pchRoleToGuard && 
			aiIsEntAlive(pOtherMember->memberBE) )
		{
			Vec3 vOtherMemberPos;
			F32 fDistSQ;
			entGetPos(pOtherMember->memberBE, vOtherMemberPos);

			fDistSQ = distance3Squared(vOtherMemberPos, vMemberPos);
			if (!bBestRequiresMore || fDistSQ < fClosest)
			{
				bool bRequiresMoreGuards = (pOtherMember->pCombatRole->guardCount < pRoleTemplate->desiredNumGuards);

				if (!pBestMember || // we don't have anyone yet
					bRequiresMoreGuards || // this is closer and it requires more guards
					// or, our current best and this don't need anymore guards, 
					// and this one has less guards than our current best
					(!bBestRequiresMore && !bRequiresMoreGuards && 
					pOtherMember->pCombatRole->guardCount < pBestMember->pCombatRole->guardCount)
					)
				{
					fClosest = fDistSQ;
					pBestMember = pOtherMember;
					bBestRequiresMore = bRequiresMoreGuards;
				}
			}
		}
	}
	FOR_EACH_END
	
	if (pBestMember)
	{
		S32 iSlot = pBestMember->pCombatRole->guardCount;
		AICombatRolesGuardSlot *pGuardSlot;

		pBestMember->pCombatRole->guardCount ++;

		// create a guard slot if one doesn't exist yet
		pGuardSlot = eaGet(&pBestMember->pCombatRole->eaGuardSlots, iSlot);
		if (!pGuardSlot)
		{
			pGuardSlot = aiMemberGuardSlot_Alloc();
			devassert(pGuardSlot);
			eaPush(&pBestMember->pCombatRole->eaGuardSlots, pGuardSlot);
		}

		aiSetLeashTypeEntity(pMember->memberBE, pMember->memberBE->aibase, pBestMember->memberBE);

		// check the def file if we are to have aggro shared with the person that is guarding us
		if (pCombatRolesDef->bShareAggroWithGuarders)
		{
			AIVarsBase *bestMemberAIB = pBestMember->memberBE->aibase;
			// due to the ppchShareAggroWithRole, 
			// check to see if we haven't already added this guy to share aggro with
			if (eaiFind(&bestMemberAIB->eaSharedAggroEnts, pMember->memberBE->myRef) == -1) 
			{
				eaiPush(&bestMemberAIB->eaSharedAggroEnts, pMember->memberBE->myRef);
			}
		}
	}

}

// ---------------------------------------------------------------------------------------------------
// Given the team that has a valid combatRolesDef, goes through the members in the team and based
// on the roles and guarding vars, will assign guards to the appropriate entities 
//	Right now guarding is assumed to be done through the leashing and having the appropriate config mods...
static void aiCombatRole_SetupTeamGuards(AITeam *pTeam)
{
	AICombatRolesDef* pCombatRolesDef = GET_REF(pTeam->combatRoleInfo.hCombatRolesDef);

	if (!pCombatRolesDef)
		return;

	// alloc AICombatRoleTeamMember if not allocated yet
	// clear the guard count so we can reassign guards
	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
	{
		if (!pMember->pCombatRole)
		{	
			pMember->pCombatRole = aiTeamMemberCombatRole_Alloc();
			devassert(pMember->pCombatRole);
			pMember->pCombatRole->pTeamRole = aiCombatRole_GetTeamRoleByName(&pTeam->combatRoleInfo, 
																				pMember->memberBE->aibase->pchCombatRole);
		}
		
		pMember->pCombatRole->guardCount = 0;
		pMember->pCombatRole->bAssignedFormationSlot = false;
	}
	FOR_EACH_END
	

	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
	{
		AIVarsBase *aib = pMember->memberBE->aibase;
		AICombatRole* pRoleTemplate;
		
		if (!aib->pchCombatRole)
			continue;
		pRoleTemplate = _findRoleByName(pCombatRolesDef, aib->pchCombatRole);
		if (!pRoleTemplate)
			continue;

		if (pRoleTemplate->pchRoleToGuard)
		{
			aiCombatRole_SetupGuard(pTeam, pCombatRolesDef, pMember, pRoleTemplate);
		}
		else if (pRoleTemplate->bUseLeashPositionCoherency)
		{
			Vec3 vPos;
			if(pTeam->roamingLeash && pTeam->roamingLeashPointValid)
			{
				copyVec3(pTeam->roamingLeashPoint, vPos);
			}
			else
			{
				copyVec3(pTeam->spawnPos, vPos);
			}
			
			aiSetLeashTypePos(pMember->memberBE, aib, vPos);
		}
		
	}
	FOR_EACH_END
	
}

// ---------------------------------------------------------------------------------------------------
static void aiCombatRole_SetupSharedAggro(AITeam *pTeam)
{
	AICombatRolesDef* pCombatRolesDef = GET_REF(pTeam->combatRoleInfo.hCombatRolesDef);
	if (!pCombatRolesDef)
		return;

	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
	{
		AIVarsBase *aib = pMember->memberBE->aibase;
		AICombatRole* pRoleTemplate;
		
		if (!aib->pchCombatRole)
			continue;
		pRoleTemplate = _findRoleByName(pCombatRolesDef, aib->pchCombatRole);
		if (!pRoleTemplate || !pRoleTemplate->ppchShareAggroWithRole)
			continue;

		eaiClear(&aib->eaSharedAggroEnts);
		
		FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pOtherMember)
		{
			if (pOtherMember != pMember)
			{
				S32 idx = eaFind(&pRoleTemplate->ppchShareAggroWithRole, pOtherMember->memberBE->aibase->pchCombatRole);
				if (idx != -1)
				{
					eaiPush(&aib->eaSharedAggroEnts, pOtherMember->memberBE->myRef);
				}
			}
		}
		FOR_EACH_END

	}
	FOR_EACH_END
}

// ---------------------------------------------------------------------------------------------------
static int cmpStatusTableDanger(const AIStatusTableEntry** lhs, const AIStatusTableEntry** rhs)
{
	F32 diff = (*lhs)->totalBaseDangerVal - (*rhs)->totalBaseDangerVal;

	if(diff == 0) return 0;
	else return SIGN(diff);
}


// ---------------------------------------------------------------------------------------------------
static void aiCombatRole_GetMainAttackers(Entity *e, AIVarsBase *aib, AIStatusTableEntry ***peaBestStatus, S32 numDesiredAttackers)
{
	PERFINFO_AUTO_STOP_START("rolesGetMainAttackers", 1);

	eaClear(peaBestStatus);

	FOR_EACH_IN_EARRAY(aib->statusTable, AIStatusTableEntry, status)
	{
		AITeamStatusEntry *teamStatus = aiGetTeamStatus(e, aib, status);
		if (!teamStatus || !status->visible || !teamStatus->legalTarget)	
			continue;
		
		eaPush(peaBestStatus, status);
	}
	FOR_EACH_END
		
	qsort((*peaBestStatus), eaSize(peaBestStatus), sizeof(void*), cmpStatusTableDanger);
	
	PERFINFO_AUTO_STOP();
}


// ---------------------------------------------------------------------------------------------------
// for each of the members that have people guarding them
// find the best position for the guard to be in
static void aiCombatRole_UpdateGuardPositions(AITeam *pTeam, AICombatRolesDef* pCombatRolesDef)
{
	static AIStatusTableEntry **eaSortedLegalStatus = NULL;
	
	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
	{
		Vec3 vCurPos;
		AIVarsBase *aib = pMember->memberBE->aibase;
		AICombatRoleTeamMember *pMemberCombatRole = pMember->pCombatRole;
		S32 i, count;
		S32 placedSlots, numDesiredSlots;
		
		if (!pMemberCombatRole || pMemberCombatRole->guardCount <= 0)
			continue;
		
		entGetPos(pMember->memberBE, vCurPos);

		// invalidate and clear all slots as taken
		FOR_EACH_IN_EARRAY(pMemberCombatRole->eaGuardSlots, AICombatRolesGuardSlot, pSlot)
			pSlot->bIsTaken = false;
			pSlot->bIsValid = false;
		FOR_EACH_END

		placedSlots = 0;
		numDesiredSlots = pMemberCombatRole->guardCount;
		aiCombatRole_GetMainAttackers(pMember->memberBE, aib, &eaSortedLegalStatus, numDesiredSlots);

		for(i = 0, count = eaSize(&eaSortedLegalStatus); 
				i < count && placedSlots < numDesiredSlots; ++i)
		{
			AIStatusTableEntry *pStatus;
			Entity *pStatusEnt;
			Vec3 vEntToStatusEnt;
			AICombatRolesGuardSlot *pGuardSlot;

			pStatus = eaSortedLegalStatus[i];

			pStatusEnt = entFromEntityRef(pTeam->partitionIdx, pStatus->entRef);
			if (!pStatusEnt)
				continue;

			{
				Vec3 vStatusEntPos;
				entGetPos(pStatusEnt, vStatusEntPos);
				subVec3(vStatusEntPos, vCurPos, vEntToStatusEnt);
			}
			
			normalVec3(vEntToStatusEnt);

			pGuardSlot = eaGet(&pMemberCombatRole->eaGuardSlots, placedSlots);
			devassert(pGuardSlot);

			copyVec3(vEntToStatusEnt, pGuardSlot->vGuardOffset);
			{
				F32 yaw = getVec3Yaw(vEntToStatusEnt);
				yawQuat(-yaw, pGuardSlot->qFacingRot);
			}
			pGuardSlot->bIsValid = true;

			placedSlots++;
		}

		// for all the unplaced slots, disperse them around in the front facing of the critter
		if (placedSlots < numDesiredSlots)
		{
			AICombatRolesGuardSlot *pGuardSlot;
			Vec3 pyFace;
			Quat qRot;

			F32 fAngleStep = PI / (F32)numDesiredSlots;
			F32 fAngle = fAngleStep * 0.5f;

			entGetFacePY(pMember->memberBE, pyFace);
			yawQuat(-pyFace[1], qRot);
			
			for (; placedSlots < numDesiredSlots; ++placedSlots)
			{
				Vec3 vDir;
				sphericalCoordsToVec3(vDir, pyFace[1] + fAngle, HALFPI, 1.f);
				
				pGuardSlot = eaGet(&pMemberCombatRole->eaGuardSlots, placedSlots);
				devassert(pGuardSlot);
				
				copyVec3(vDir, pGuardSlot->vGuardOffset);
				copyQuat(qRot, pGuardSlot->qFacingRot);
				pGuardSlot->bIsValid = true;

				fAngle += fAngleStep;
			}
		}
		
	}	
	FOR_EACH_END
}


// ---------------------------------------------------------------------------------------------------
int aiCombatRole_RequestGuardPosition(AITeam *pTeam, Entity *e)
{
	Vec3 vCurPos;
	AICombatRole* pRoleTemplate;
	AICombatRolesGuardSlot *pBestSlot = NULL;
	F32 fBestDist = FLT_MAX;
	AITeamMember *pGuardMember;
	AIVarsBase *aib = e->aibase;
	Entity *guardEnt = aib->erLeashEntity ? entFromEntityRef(pTeam->partitionIdx, aib->erLeashEntity) : NULL;
	AICombatRolesDef* pCombatRolesDef;

	if (!guardEnt)
		return false;

	pGuardMember = aiTeamFindMemberByEntity(pTeam, guardEnt);
	if (!pGuardMember || !pGuardMember->pCombatRole)
		return false;

	pCombatRolesDef = GET_REF(pTeam->combatRoleInfo.hCombatRolesDef);
	if (!pCombatRolesDef)
		return false;

	pRoleTemplate = _findRoleByName(pCombatRolesDef, aib->pchCombatRole);
	if (!pRoleTemplate)
		return false;

	entGetPos(guardEnt, vCurPos);

	FOR_EACH_IN_EARRAY(pGuardMember->pCombatRole->eaGuardSlots, AICombatRolesGuardSlot, pSlot)
	{
		if (pSlot->bIsValid && !pSlot->bIsTaken)
		{
			F32 fDist = distance3Squared(vCurPos, pSlot->vGuardOffset);
			if (fDist < fBestDist)
			{
				pBestSlot = pSlot;
				fBestDist = fDist;
			}
		}
		
	}
	FOR_EACH_END

	if (pBestSlot)
	{
		Vec3 vOffset;
		pBestSlot->bIsTaken = true;
		
		scaleVec3(pBestSlot->vGuardOffset, pRoleTemplate->fGuardOffset, vOffset);

		aiMovementSetTargetEntity(e, e->aibase, guardEnt, vOffset, false, 
						AI_MOVEMENT_ORDER_ENT_FOLLOW_OFFSET, AI_MOVEMENT_TARGET_CRITICAL);
		aiMovementSetFinalFaceRot(e, e->aibase, pBestSlot->qFacingRot);

		return true;
	}
			
	return false;		
}

// ---------------------------------------------------------------------------------------------------
static void aiCombatRole_GuardingUpdate(AITeam *pTeam, AICombatRolesDef* pCombatRolesDef)
{
	if (!pCombatRolesDef->bPickUpGuardeeOnDeath)
		return;

	// TODO: it probably needs to update the current guarders so we can get current guard counts
	// so when reassigning we can better spread out guarders.
	// and keeping the current guard counts low helps aiCombatRole_UpdateGuardPositions not have to evaluate
	// a growing number of guarders
	
	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
	{
		AIVarsBase *aib = pMember->memberBE->aibase;
		AICombatRole *pRoleTemplate;
		
		if (aib->erLeashEntity || // if we already have an entity we're leashing to, ignore
			!aib->pchCombatRole) // or if we don't have an assigned combat role
			continue;
		pRoleTemplate = _findRoleByName(pCombatRolesDef, aib->pchCombatRole);
		if (!pRoleTemplate || !pRoleTemplate->pchRoleToGuard)
			continue;

		aiCombatRole_SetupGuard(pTeam, pCombatRolesDef, pMember, pRoleTemplate);
	
	}
	FOR_EACH_END
}

// ---------------------------------------------------------------------------------------------------
void aiCombatRole_CombatTick(AITeam *pTeam)
{
	AICombatRolesDef* pCombatRolesDef = GET_REF(pTeam->combatRoleInfo.hCombatRolesDef);
	if (!pCombatRolesDef || eaSize(&pTeam->members) == 0)
		return;
	if (!pTeam->members[0]->pCombatRole)
		return; 

	aiCombatRole_GuardingUpdate(pTeam, pCombatRolesDef);
	aiCombatRole_UpdateGuardPositions(pTeam, pCombatRolesDef);
	aiCombatRole_DoLuckyCharmAssignments(pTeam);
}


// ---------------------------------------------------------------------------------------------------
static void aiCombatRole_CountEntitesPerRole(AITeam *pTeam)
{
	// reset all the counts to 0
	FOR_EACH_IN_EARRAY(pTeam->combatRoleInfo.eaTeamRolesInfo, AICombatRolesTeamRole, pRole)
		pRole->numEntitiesInRole = 0;
	FOR_EACH_END

	pTeam->combatRoleInfo.dummyRole.numEntitiesInRole = 0;

	//
	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
	{
		AICombatRolesTeamRole* pTeamRole;
		pTeamRole = aiCombatRole_GetTeamRoleByName(&pTeam->combatRoleInfo, pMember->memberBE->aibase->pchCombatRole);
		++pTeamRole->numEntitiesInRole;
	}
	FOR_EACH_END
}

// ---------------------------------------------------------------------------------------------------
void aiCombatRole_GetEntitesInRole(AITeam *pTeam, const char* pchRoleName, Entity*** eaEntsOut)
{
	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
	{
		if (strcmp(pMember->memberBE->aibase->pchCombatRole, pchRoleName) == 0)
		{
			eaPushUnique(eaEntsOut, pMember->memberBE);
		}
	}
	FOR_EACH_END
}

AICombatRolesDef* aiCombatRole_GetTeamRolesDef(AITeam *pTeam)
{
	return SAFE_GET_REF(pTeam, combatRoleInfo.hCombatRolesDef);
}


// ---------------------------------------------------------------------------------------------------
static AICRole_AssignedTarget* aiCRoleTeamInfo_LC_FindAssignmentForTarget(	AICombatRolesTeamInfo *pTeamCombatRoleInfo, 
																			PetTargetType type, 
																			EntityRef erTarget)
{
	AICRole_LuckyCharmInfo *pLuckyCharmInfo;

	devassert(type > kPetTargetType_NONE && type < kPetTargetType_COUNT);

	pLuckyCharmInfo = &pTeamCombatRoleInfo->aLuckyCharmInfo[type];
	FOR_EACH_IN_EARRAY(pLuckyCharmInfo->eaAssignedTargets, AICRole_AssignedTarget, pTarget)
	{
		if (pTarget->erTarget == erTarget)
			return pTarget;
	}
	FOR_EACH_END

	return NULL;
}

// ---------------------------------------------------------------------------------------------------
// 
static void aiCRoleTeamInfo_LC_AssignToTarget(	AICombatRolesTeamInfo *pTeamCombatRoleInfo, 
												AITeamMember *pMember, 
												PetTargetType type, 
												EntityRef erTarget)
{
	AICRole_AssignedTarget *pTarget;
	pTarget = aiCRoleTeamInfo_LC_FindAssignmentForTarget(pTeamCombatRoleInfo, type, erTarget);
	if (!pTarget)
	{
		pTarget = aiCRole_AssignedTarget_Alloc();
		pTarget->erTarget = erTarget;
		pTarget->assignedCount = 1;
		
		eaPush(&pTeamCombatRoleInfo->aLuckyCharmInfo[type].eaAssignedTargets, pTarget);
	}
	else
	{
		pTarget->assignedCount++;
	}

	pMember->pCombatRole->erLuckyCharmTarget = erTarget;
}

// ---------------------------------------------------------------------------------------------------
static void aiCRoleTeamInfo_LC_ClearAssignments(AICombatRolesTeamInfo *pTeamCombatRoleInfo)
{
	S32 i;
	for (i = 0; i < kPetTargetType_COUNT; ++i)
	{
		if (pTeamCombatRoleInfo->aLuckyCharmInfo[i].eaAssignedTargets)
			eaDestroyEx(&pTeamCombatRoleInfo->aLuckyCharmInfo[i].eaAssignedTargets, aiCRole_AssignedTarget_Free);
	}
}


// ---------------------------------------------------------------------------------------------------



int aiCombatRole_LuckyCharmsSortByIndex(const PetTargetingInfo **pptr1, const PetTargetingInfo **pptr2)
{
	return (*pptr1)->iIndex - (*pptr2)->iIndex;
}

static void aiCombatRole_DoLuckyCharmAssignments(AITeam *pTeam)
{
	const PetTargetingInfo **eaPetTargetingInfo = NULL; 
	AICombatRolesDef* pCombatRolesDef;
	static const PetTargetingInfo **s_eaValidTargets = NULL;
	AICombatRolesTeamInfo *pTeamCombatRoleInfo;

	// clear out the lucky charm targets in case we early out
	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
	{
		if (!pMember->pCombatRole)
			continue;
		
		// 
		pMember->pCombatRole->erLuckyCharmTarget = 0;
	}
	FOR_EACH_END

	// Get the lucky charm list, for now it is a list of PetTargetingInfo
	if (pTeam->teamOwner)
		eaPetTargetingInfo = mapState_GetPetTargetingInfo(pTeam->teamOwner);
	
	if (!eaPetTargetingInfo || eaSize(&eaPetTargetingInfo) == 0)
		return; 
	
	pCombatRolesDef = GET_REF(pTeam->combatRoleInfo.hCombatRolesDef);
	if (!pCombatRolesDef)
		return;

	pTeamCombatRoleInfo = &pTeam->combatRoleInfo;
	aiCRoleTeamInfo_LC_ClearAssignments(pTeamCombatRoleInfo);

	// go through the team's combat roles and assign targets 
	// 
	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
	{
		S32 iBestIdx;
		EntityRef bestTarget;
		AIVarsBase *aib;
		AICombatRole *pRoleTemplate;

		if (!pMember->pCombatRole)
			continue;

		iBestIdx = 0x00FFFFFF;
		bestTarget = 0;
		aib = pMember->memberBE->aibase;
		pRoleTemplate = _findRoleByName(pCombatRolesDef, aib->pchCombatRole);

		if (!pRoleTemplate || !pRoleTemplate->peTargetTags)
			continue;

		eaClear(&s_eaValidTargets);

		// go through the targeting info and get the list of targets 
		FOR_EACH_IN_EARRAY(eaPetTargetingInfo, const PetTargetingInfo, pPetTargeting)
		{
			S32 idx = eaiFind(&pRoleTemplate->peTargetTags, pPetTargeting->eType);
			Entity* pEnt = entFromEntityRef(pTeam->partitionIdx, pPetTargeting->erTarget);
			if (idx == -1)
				continue;

			if (!pEnt || !aiIsEntAlive(pEnt))
				continue;

			// we care about this tag in some form
			if (idx < iBestIdx)
			{	// this type has priority over what we had before
				// set the best index and clear the valid targets
				iBestIdx = idx;
				eaClear(&s_eaValidTargets);
				eaPush(&s_eaValidTargets, pPetTargeting);
			}
			else if (idx == iBestIdx)
			{	// this type is the same as the current best tag
				// add it to the list of targets we care about
				eaPush(&s_eaValidTargets, pPetTargeting);
			}
		}
		FOR_EACH_END

		if (eaSize(&s_eaValidTargets))
		{	
			PetTargetType targetType = s_eaValidTargets[0]->eType;
			eaQSort(s_eaValidTargets, aiCombatRole_LuckyCharmsSortByIndex);

			// depending on the tag type, we want to assign targets appropriately
			// TODO: allow the different target types to say whether they spread, all go after first, etc
			switch (targetType)
			{
				// kill type does not spread out its targets
				case kPetTargetType_Kill:
				{
					aiCRoleTeamInfo_LC_AssignToTarget(	pTeamCombatRoleInfo, 
														pMember, 
														targetType, 
														s_eaValidTargets[0]->erTarget);
				}
					
				// the tank target type will attempt to spread out the tanks
				xcase kPetTargetType_Tank:
				{
					const PetTargetingInfo *pBestTarget = NULL;
					S32 leastCount = 0x00FFFFFF;

					// Get the target that has the least amount of entities assigned to it
					FOR_EACH_IN_EARRAY_FORWARDS(s_eaValidTargets, const PetTargetingInfo, pPetTargeting)
					{
						AICRole_AssignedTarget *assignedTarget;
						S32 numAssigned = 0;
						assignedTarget = aiCRoleTeamInfo_LC_FindAssignmentForTarget(pTeamCombatRoleInfo, 
																					targetType,
																					pPetTargeting->erTarget);
						if (!assignedTarget)
						{	// this one has no assigned target, so use this one
							pBestTarget = pPetTargeting;
							break;
						}
	
						// get the target that has the least amount of members assigned
						if (assignedTarget->assignedCount < leastCount)
						{
							leastCount = assignedTarget->assignedCount;
							pBestTarget = pPetTargeting;
						}
					}
					FOR_EACH_END

					if (pBestTarget)
					{
						aiCRoleTeamInfo_LC_AssignToTarget(	pTeamCombatRoleInfo, 
															pMember,
															targetType, 
															pBestTarget->erTarget);
					}
				}
			}

		}
			
			
	}
	FOR_EACH_END
	


}


// ---------------------------------------------------------------------------------------------------
EntityRef aiCombatRole_RequestPreferredTarget(Entity *e)
{
	AITeamMember *teamMember = aiGetTeamMember(e, e->aibase);
	if (teamMember && teamMember->pCombatRole)
	{
		return teamMember->pCombatRole->erLuckyCharmTarget;
	}
	
	return 0;
}

// Clears all preferred targets set by lucky charms
void aiCombatRole_ClearAllPreferredTargets(SA_PARAM_NN_VALID AITeam *pTeam)
{
	if (pTeam)
	{
		FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pTeamMember)
		{
			if (pTeamMember && pTeamMember->pCombatRole)
			{
				pTeamMember->pCombatRole->erLuckyCharmTarget = 0;
			}
		}
		FOR_EACH_END
	}	
}



// ---------------------------------------------------------------------------------------------------
// Formations

// ---------------------------------------------------------------------------------------------------
// pchName: Assumed pooled string
__forceinline static AICombatFormationRoleDef* aiCombatFormation_GetFormationRoleByName(const AICombatFormationDef *pFormation, 
																					 const char *pchName)
{
	FOR_EACH_IN_EARRAY(pFormation->eaRoleFormations, AICombatFormationRoleDef, pFormationRole)
	{
		if (pFormationRole->pchName == pchName)
			return pFormationRole;
	}
	FOR_EACH_END

	return NULL;
}

// ---------------------------------------------------------------------------------------------------
// Calculates the formation world position as well as initializing some of the slot information
static void aiCombatFormation_FixupFormationSlots(int iPartitionIdx,
													AICombatFormationDef *pFormationDef, 
													AICombatRolesTeamRole *pRole, 
													const Vec3 vBarycenter,
													const Vec3 vTeamSpawnPos, 
													const Quat qFormationRot)
{
	S32 i, numRoleFormationSlots, count = 0;
	AICombatFormationRoleDef *pFormationRoleDef;

	pFormationRoleDef = aiCombatFormation_GetFormationRoleByName(pFormationDef, pRole->pchName);

	if (pFormationRoleDef)
		count = eaSize(&pFormationRoleDef->eaSlots);

	numRoleFormationSlots = eaSize(&pRole->eaFormationSlots);
	MIN1(count, numRoleFormationSlots);
	MIN1(count, pRole->numEntitiesInRole);

	for (i = 0; i < count; i++)
	{
		AICombatFormationSlotDef *pSlotDef = NULL;
		AICombatRoleFormationSlot *pSlot = pRole->eaFormationSlots[i];

		pSlot->erUsed = 0;
		pSlot->bValidSlot  = true;

		if (pFormationRoleDef)
			pSlotDef = pFormationRoleDef->eaSlots[i];

		// transform the formation slot offset into world space
		if (pSlotDef)
		{
			Vec3 vDir;
			Vec3 vGroundPos;

			setVec3(vDir, pSlotDef->x, 0, pSlotDef->z);
			subVec3(vDir, vBarycenter, vDir);

			quatRotateVec3(qFormationRot, vDir, pSlot->vWorldPos);
			addVec3(pSlot->vWorldPos, vTeamSpawnPos, pSlot->vWorldPos);
			
			// todo: further validate & possibly fix-up positions
			if (aiFindGroundDistance(worldGetActiveColl(iPartitionIdx), pSlot->vWorldPos, vGroundPos) != -FLT_MAX)
			{	
				copyVec3(vGroundPos, pSlot->vWorldPos);
			}
		}
	}
	
	while (i < numRoleFormationSlots)
	{
		pRole->eaFormationSlots[i]->bValidSlot = false;
		++i;
	}
}

// ---------------------------------------------------------------------------------------------------
static void aiCombatFormation_CalculateFormationCenter(AICombatRolesDef *pDef, 
														AICombatRolesTeamRole *pRole, 
														F32 fDivisor,
														Vec3 vFormationCenterOut )
{
	S32 count, i;
	AICombatFormationRoleDef *pFormationRole;

	pFormationRole = aiCombatFormation_GetFormationRoleByName(pDef->pFormation, pRole->pchName);
	if (!pFormationRole)
		return;

	count = eaSize(&pFormationRole->eaSlots);
	MIN1(count, pRole->numEntitiesInRole);

	for (i = 0; i < count; ++i)
	{
		AICombatFormationSlotDef *pSlotDef = pFormationRole->eaSlots[i];

		vFormationCenterOut[0] += pSlotDef->x * fDivisor;
		vFormationCenterOut[2] += pSlotDef->z * fDivisor;
	}
}

// ---------------------------------------------------------------------------------------------------
int aiCombatRoleFormation_RequestSlot(AITeam *pTeam, AITeamMember* pMember, AICombatRolesDef *pDef, int bUseRotation) 
{
	AICombatRolesTeamInfo* pCombatRoleInfo = &pTeam->combatRoleInfo;
	AICombatRoleFormationSlot *pBestSlot = NULL;
	AIVarsBase *aib;

	if (!pDef)
	{
		pDef = aiTeamGetCombatRolesDef(pTeam);
		if (!pDef)
			return false;
	}
	if (!pDef->pFormation || !pMember->pCombatRole)
		return false;

	if (pMember->pCombatRole->bAssignedFormationSlot)
	{	// we were already assigned a formation slot
		return true;
	}

	aib = pMember->memberBE->aibase;
	
	if (pCombatRoleInfo->bFormationSlotsAssigned)
	{
		EntityRef entRef = entGetRef(pMember->memberBE);
		AICombatRolesTeamRole *pTeamRole = aiCombatRole_GetTeamRoleByName(pCombatRoleInfo, aib->pchCombatRole);

		FOR_EACH_IN_EARRAY(pTeamRole->eaFormationSlots, AICombatRoleFormationSlot, pSlot)
		{
			if (pSlot->erUsed == entRef)
			{
				pBestSlot = pSlot;
				break;
			}
		}
		FOR_EACH_END
	}
	else
	{
		AICombatRolesTeamRole *pTeamRole = aiCombatRole_GetTeamRoleByName(pCombatRoleInfo, aib->pchCombatRole);
		S32 numSlots = eaSize(&pTeamRole->eaFormationSlots);
		F32 fClosestDistSQ = FLT_MAX;
		S32 i;
		Vec3 vMemberPos;

		entGetPos(pMember->memberBE, vMemberPos);

		// get the closest available slot 
		for (i = 0; 
			(i < numSlots && i < pTeamRole->numEntitiesInRole); ++i)
		{
			AICombatRoleFormationSlot *pSlot = pTeamRole->eaFormationSlots[i];
			
			if(!pSlot->erUsed)
			{
				F32 fDistSQ = distance3Squared(pSlot->vWorldPos, vMemberPos);
				if (fDistSQ < fClosestDistSQ)
				{
					fClosestDistSQ = fDistSQ;
					pBestSlot = pSlot;
				}
			}
		}
	}

	if (pBestSlot)
	{
		aiMovementSetTargetPosition(pMember->memberBE, aib, pBestSlot->vWorldPos, NULL, 0);
		if (bUseRotation) 
			aiMovementSetFinalFaceRot(pMember->memberBE, aib, pCombatRoleInfo->qFormationRot);
		pBestSlot->erUsed = pMember->memberBE->myRef;
	}
	else
	{
		// calculate a reasonable position?
		// possibly print out warning 
		// for now do nothing
		if (bUseRotation)
			aiMovementSetFinalFaceRot(pMember->memberBE, aib, pCombatRoleInfo->qFormationRot);
	}

	pMember->pCombatRole->bAssignedFormationSlot = true;
	
	return true;
}

// ---------------------------------------------------------------------------------------------------
static void aiCombatRole_GetFormationAnchorAndOrientation(AITeam *pTeam, 
														  Vec3 vFormationAnchorPos, 
														  Quat qFormationRot)
{
	AITeamMember *pClosestMember = NULL;
	AITeamStatusEntry *pClosestTarget = NULL;
	F32 fClosestDistSQR = FLT_MAX;
	zeroVec3(vFormationAnchorPos);

	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
	{
		Vec3 vMemberPos;
		entGetPos(pMember->memberBE, vMemberPos);

		FOR_EACH_IN_EARRAY(pTeam->statusTable, AITeamStatusEntry, pStatus)
		{
			// using the status' lastKnownPos, is this recent enough / valid ?
			F32 fDistSQR = distance3Squared(vMemberPos, pStatus->lastKnownPos);
			if (fDistSQR < fClosestDistSQR)
			{
				fClosestDistSQR = fDistSQR;
				pClosestTarget = pStatus;
				pClosestMember = pMember;
			}
		}
		FOR_EACH_END
	}
	FOR_EACH_END

	if (! pClosestTarget)
		return;

	entGetPos(pClosestMember->memberBE, vFormationAnchorPos);

	{
		Vec3 vDir; 
		subVec3(pClosestTarget->lastKnownPos, vFormationAnchorPos, vDir);
		yawQuat(-getVec3Yaw(vDir), qFormationRot);
	}
}

// this looks like a function that should be on AI team, but I'm only needing a subset of members
static AITeamMember* _findClosestMemberToPos(AITeamMember **eaMembers, const Vec3 vPos, F32 *pfClosestDistSQR)
{
	F32 fClosestSQR = FLT_MAX;
	AITeamMember *pClosestMember = NULL;

	// for each of the formation slots, find the nearest unclaimed member
	FOR_EACH_IN_EARRAY_FORWARDS(eaMembers, AITeamMember, pMember)
	{
		Vec3 vMemberPos; 
		F32 fDistSQR;
		entGetPos(pMember->memberBE, vMemberPos);
			
		fDistSQR = distance3Squared(vMemberPos, vPos);
		if (fDistSQR < fClosestSQR)
		{
			fClosestSQR = fDistSQR;
			pClosestMember = pMember;
		}
	}
	FOR_EACH_END

	*pfClosestDistSQR = fClosestSQR;
	return pClosestMember;
}

static AICombatRoleFormationSlot* _findClosestSlotToPos(AICombatRoleFormationSlot **eaFormationSlots, const Vec3 vPos, F32 *pfClosestDistSQR)
{
	F32 fClosestSQR = FLT_MAX;
	AICombatRoleFormationSlot *pClosestSlot = NULL;

	FOR_EACH_IN_EARRAY_FORWARDS(eaFormationSlots, AICombatRoleFormationSlot, pSlot)
	{
		F32 fDistSQR;

		if (!pSlot->bValidSlot)
			break;
		if (pSlot->erUsed)
			continue;

		fDistSQR = distance3Squared(pSlot->vWorldPos, vPos);

		if (fDistSQR < fClosestSQR)
		{
			pClosestSlot = pSlot;
			fClosestSQR = fDistSQR;
		}
	}
	FOR_EACH_END

	*pfClosestDistSQR = fClosestSQR;
	return pClosestSlot;
}

// ---------------------------------------------------------------------------------------------------
// this will assign the best member to the best slot
// by finding the closest member to each slot, then finding the slot that has the furthest closest member
// and that slot gets assigned that member
static AITeamMember* _matchNextToSlot(AITeamMember **eaTeamMemberList, AICombatRolesTeamRole *pCombatTeamRole)
{

	// first, for each member find the closest valid slot to each member
	FOR_EACH_IN_EARRAY(eaTeamMemberList, AITeamMember, pMember)
	{
		AICombatRoleTeamMember *pCombatRole = pMember->pCombatRole;
		if (pCombatRole)
		{
			Vec3 vMemberPos; 
			entGetPos(pMember->memberBE, vMemberPos);

			pCombatRole->pClosestSlot = _findClosestSlotToPos(pCombatTeamRole->eaFormationSlots, 
																vMemberPos, 
																&pCombatRole->fClosestSlotDist);
		}
	}
	FOR_EACH_END

	// next find the member that is furthest away from a slot, and give them that slot
	{
		F32 fFurthestDist = -FLT_MAX;
		AITeamMember *pBestMember = NULL;

		FOR_EACH_IN_EARRAY(eaTeamMemberList, AITeamMember, pMember)
			// assign the member that has the furthest slot
			AICombatRoleTeamMember *pCombatRole = pMember->pCombatRole;
			if (pCombatRole && pCombatRole->fClosestSlotDist > fFurthestDist )
			{
				fFurthestDist = pCombatRole->fClosestSlotDist;
				pBestMember = pMember;
				
			}
		FOR_EACH_END

		if (!pBestMember || !pBestMember->pCombatRole || !pBestMember->pCombatRole->pClosestSlot)
			return NULL;

		pBestMember->pCombatRole->pClosestSlot->erUsed = entGetRef(pBestMember->memberBE);
		return pBestMember;
	}
	
}

// ---------------------------------------------------------------------------------------------------
static void aiCombatRole_AssignFormationSlotsForRole(AITeam *pTeam, AICombatRolesTeamRole *pCombatTeamRole)
{
	static AITeamMember **s_eaTeamMemberList = NULL;

	eaClear(&s_eaTeamMemberList);

	// get a list of all the members for this role
	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
		if (pMember->pCombatRole && pMember->pCombatRole->pTeamRole->pchName == pCombatTeamRole->pchName)
		{
			eaPush(&s_eaTeamMemberList, pMember);
		}
	FOR_EACH_END

	while(eaSize(&s_eaTeamMemberList))
	{
		AITeamMember *pMatchedMember = _matchNextToSlot(s_eaTeamMemberList, pCombatTeamRole);
		if (pMatchedMember)
		{
			eaFindAndRemoveFast(&s_eaTeamMemberList, pMatchedMember);
		}
		else
		{
			return;
		}
	}

	//
}

// ---------------------------------------------------------------------------------------------------
void aiCombatRole_SetupTeamFormation(int iPartitionIdx, AITeam *pTeam)
{
	Vec3 vFormationAnchorPos;
	AICombatFormationDef *pFormationDef;
	AICombatRolesTeamInfo *pTeamCombatInfo;

	AICombatRolesDef *pDef = aiTeamGetCombatRolesDef(pTeam);
	if (!pDef || !pDef->pFormation)
		return;

	
	if (ABS_TIME_SINCE_PARTITION(iPartitionIdx, pTeam->combatRoleInfo.lastEnterFormationTime) < SEC_TO_ABS_TIME(5.f))
	{	// too soon since we've last entered formation, wait
		return;
	}
	pTeamCombatInfo = &pTeam->combatRoleInfo;

	pTeamCombatInfo->lastEnterFormationTime = ABS_TIME_PARTITION(iPartitionIdx);
	

	pFormationDef = pDef->pFormation;
	unitQuat(pTeamCombatInfo->qFormationRot);

	aiCombatRole_CountEntitesPerRole(pTeam);

	// find the closest member / status target combo
	aiCombatRole_GetFormationAnchorAndOrientation(pTeam, vFormationAnchorPos, pTeam->combatRoleInfo.qFormationRot);

	// initialize all the formation slot data- calculate the world positions for all the formation slots 
	{
		Vec3 vLocalFormationCenter;

		zeroVec3(vLocalFormationCenter);

		// calculate the barycenter of all the needed slots
		if (eaSize(&pTeam->members))
		{
			U32 bPositionSet = false;
			F32 fDivisor = 1.f/(F32)eaSize(&pTeam->members);
			zeroVec3(vLocalFormationCenter);

			FOR_EACH_IN_EARRAY(pTeamCombatInfo->eaTeamRolesInfo, AICombatRolesTeamRole, pRole)
			{
				aiCombatFormation_CalculateFormationCenter(pDef, pRole, fDivisor, vLocalFormationCenter);
			}
			FOR_EACH_END

			aiCombatFormation_CalculateFormationCenter(pDef, &pTeamCombatInfo->dummyRole, fDivisor, vLocalFormationCenter);
		}
		
		// mark all the formation slots unused, and calculate the world positions from the given direction
		{
			// for each of the roles + the unnamed role

			FOR_EACH_IN_EARRAY(pTeamCombatInfo->eaTeamRolesInfo, AICombatRolesTeamRole, pRole)
			{
				aiCombatFormation_FixupFormationSlots(iPartitionIdx, pFormationDef, pRole, 
														vLocalFormationCenter, vFormationAnchorPos, 
														pTeamCombatInfo->qFormationRot);
			}
			FOR_EACH_END
			
			aiCombatFormation_FixupFormationSlots(iPartitionIdx, pFormationDef, &pTeamCombatInfo->dummyRole, 
											vLocalFormationCenter, vFormationAnchorPos, 
											pTeamCombatInfo->qFormationRot);
		}
	}
	
	// for each member on the team, invalidate the assigned formation slot
	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
		if (pMember->pCombatRole)
		{
			pMember->pCombatRole->bAssignedFormationSlot = false;
			// if we have an override speed, use it
			if (aiGlobalSettings.combatRoleFormationOverrideMovespeed > 0.f)
				aiMovementSetOverrideSpeed( pMember->memberBE, 
											pMember->memberBE->aibase, 
											aiGlobalSettings.combatRoleFormationOverrideMovespeed);
		}
		
	FOR_EACH_END

	if (s_bUseBestFormationPositions)
	{
		FOR_EACH_IN_EARRAY(pTeamCombatInfo->eaTeamRolesInfo, AICombatRolesTeamRole, pRole)
			aiCombatRole_AssignFormationSlotsForRole(pTeam, pRole);
		FOR_EACH_END
			aiCombatRole_AssignFormationSlotsForRole(pTeam, &pTeamCombatInfo->dummyRole);
		pTeamCombatInfo->bFormationSlotsAssigned = true;
	}
	else
	{
		pTeamCombatInfo->bFormationSlotsAssigned = false;
	}
	
}

// ---------------------------------------------------------------------------------------------------
void aiCombatRole_CleanupFormation(AITeam *pTeam)
{
	AICombatRolesDef *pDef = aiTeamGetCombatRolesDef(pTeam);
	if (!pDef || !pDef->pFormation)
		return;

	// the only thing to clean-up is the override movement speed
	if (aiGlobalSettings.combatRoleFormationOverrideMovespeed > 0.f)
	{
		FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
			if (pMember->pCombatRole)
			{
				aiMovementClearOverrideSpeed(pMember->memberBE, pMember->memberBE->aibase);
			}
		FOR_EACH_END
	}
}


#include "aiCombatRoles_h_ast.c"
