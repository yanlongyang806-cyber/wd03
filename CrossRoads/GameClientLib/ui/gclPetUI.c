/***************************************************************************
*     Copyright (c) 2005-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "earray.h"



#include "UIButton.h"
#include "UIGen.h"
#include "EString.h"
#include "StringCache.h"

#include "gclEntity.h"
#include "gclMapState.h"
#include "CharacterAttribs.h"
#include "Expression.h"
#include "Character.h"
#include "CharacterClass.h"
#include "cmdClient.h"
#include "contact_common.h"
#include "dynFxManager.h"
#include "dynFxInterface.h"
#include "soundLib.h"
#include "mission_common.h"
#include "Player.h"
#include "Powers.h"
#include "Powers_h_ast.h"
#include "PowerTree.h"
#include "Autogen/gclPetUI_c_ast.h"

#include "mapstate_common.h"
#include "SavedPetCommon.h"
#include "itemCommon.h"
#include "StringUtil.h"
#include "notifycommon.h"
#include "charactercreationUI.h"
#include "simpleparser.h"
#include "cmdparse.h"
#include "GameAccountDataCommon.h"
#include "Character_target.h"
#include "WorldGrid.h"

#include "GameClientLib.h"
#include "EntitySavedData.h"
#include "EntityLib.h"
#include "entCritter.h"
#include "WorldColl.h"
#include "WorldLib.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/AILib_autogen_ServerCmdWrappers.h"

#include "gclUIGen.h"
#include "UITray.h"
#include "Tray.h"
#include "OfficerCommon.h"
#include "Entity_h_ast.h"
#include "Player_h_ast.h"
#include "Tray_h_ast.h"
#include "UITray_h_ast.h"
#include "SavedPetCommon_h_ast.h"

static U32 s_LastLuckyCharmAssignment = 0;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct PetRowDataID
{
	Entity*				pEnt;					AST(UNOWNED)
	GlobalType			iType;
	ContainerID			iID;
	char*				pcName;
	char*				pcInternalName;
	char*				pcPetDefName;
	const char*			pcPuppetCategorySet;	AST(POOL_STRING)
	int					iLen;
	int					iClass;
	U32					iCritterID;
	S32					iMinActivePuppetLevel;
	char*				pcCritterName;
	bool				bActive;
		//Whether or not this is an active puppet
	bool				bCurrent;
		//Whether or not this is the player entity
	bool				bPuppetActive;
	bool				bIsExtraPetSlot;
	bool				bIsExtraPuppetSlot;
	bool				bIsCritterPet;

} PetRowDataID;

AUTO_STRUCT;
typedef struct PetData
{
	Entity*				pEntity;		AST(UNOWNED)
	const char*			pchAiState;		AST(POOL_STRING)
	const char*			pchAiStance;	AST(POOL_STRING)
	const char*			pchName;		AST(UNOWNED)
	S32					iLevel;
	S32					iCombatLevel;

	bool				bPet : 1;
	bool				bSummon : 1;
	bool				bDead : 1;
} PetData;

AUTO_ENUM;
typedef enum UIPetListFlags
{
	kUIPetListFlag_NoPets = 0, ENAMES(NoPets NoPuppets)
	// Pets
	kUIPetListFlag_SavedPets = (1<<0),
	kUIPetListFlag_Critters = (1<<1),
	kUIPetListFlag_TrainingPets = (1<<2),
	kUIPetListFlag_NotTrainingPets = (1<<3),
	// Puppets
	kUIPetListFlag_CurrentAndActivePuppets = (1<<4),
	kUIPetListFlag_ActivePuppets = (1<<5),
	kUIPetListFlag_InactivePuppets = (1<<6),
	// Extra pets and puppets
	kUIPetListFlag_ExtraPets = (1<<7),
	kUIPetListFlag_ExtraPuppets = (1<<8),
	// All pets and puppets
	kUIPetListFlag_AllPets = (kUIPetListFlag_SavedPets|kUIPetListFlag_Critters|kUIPetListFlag_TrainingPets|kUIPetListFlag_NotTrainingPets),
	kUIPetListFlag_AllPuppets = (kUIPetListFlag_CurrentAndActivePuppets|kUIPetListFlag_ActivePuppets|kUIPetListFlag_InactivePuppets),
} UIPetListFlags;

const char* gclCursorPetRally_GetRallyPointFXBaseName();

static S32 s_iPuppetCount;
static S32 s_iPetEntityCount;
static S32 s_iPuppetLimit;
static S32 s_iPetEntityLimit;
static bool s_bDisableRallyPointFX;

// Toggle pet rally point FX
AUTO_CMD_INT(s_bDisableRallyPointFX, DisableRallyPointFX) ACMD_CALLBACK(gclPetRallyPoint_ToggleFX) ACMD_ACCESSLEVEL(0);

AUTO_RUN;
void PetUI_Init(void)
{
	ui_GenInitStaticDefineVars(UIPetListFlagsEnum, "PetListFlag_");
	ui_GenInitStaticDefineVars(PuppetContainerStateEnum, "PuppetState_");
	ui_GenInitIntVar("PetEntityCount", 0);
	ui_GenInitIntVar("PetEntityLimit", 0);
	ui_GenInitIntVar("PuppetCount", 0);
	ui_GenInitIntVar("PuppetLimit", 0);
}

void gclPetUI_OncePerFrame(void)
{
	Entity *pEntity = entActivePlayerPtr();
	if (pEntity)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		s_iPetEntityCount = Entity_CountPets(pEntity,true,false,false);
		s_iPetEntityLimit = Officer_GetMaxAllowedPets(pEntity, GET_REF(pEntity->hAllegiance),pExtract);
		s_iPuppetCount = Entity_CountPets(pEntity,false,true,true);
		s_iPuppetLimit = g_PetRestrictions.iMaxPuppets + Officer_GetExtraPuppets(pEntity,pExtract);
	}
	else
	{
		s_iPetEntityCount = 0;
		s_iPetEntityLimit = 0;
		s_iPuppetCount = 0;
		s_iPuppetLimit = 0;
	}
	ui_GenSetIntVar("PetEntityCount", s_iPetEntityCount);
	ui_GenSetIntVar("PetEntityLimit", s_iPetEntityLimit);
	ui_GenSetIntVar("PuppetCount", s_iPuppetCount);
	ui_GenSetIntVar("PuppetLimit", s_iPuppetLimit);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetCurrentPetEntityCount);
S32 gclPetExpr_GetCurrentPetEntityCount(void)
{
	return s_iPetEntityCount;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetCurrentPuppetCount);
S32 gclPetExpr_GetCurrentPuppetCount(void)
{
	return s_iPuppetCount;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetMaximumPetEntityCount);
S32 gclPetExpr_GetMaximumPetEntityCount(void)
{
	return s_iPetEntityLimit;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetMaximumPuppetCount);
S32 gclPetExpr_GetMaximumPuppetCount(void)
{
	return s_iPuppetLimit;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_ClearAttackTarget");
void PetCommands_ClearAttackTarget(SA_PARAM_OP_VALID Entity* pPet)
{
	if (!pPet) 
		return;

	ServerCmd_PetCommands_ClearAttackTarget(entGetRef(pPet));
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_SetAttackTarget");
void PetCommands_SetAttackTarget(SA_PARAM_OP_VALID Entity* pPet, SA_PARAM_OP_VALID Entity *pTarget)
{
	if (!pPet) 
		return;
	else if (!pTarget)
		PetCommands_ClearAttackTarget(pPet);
	else
		ServerCmd_PetCommands_SetAttackTarget(entGetRef(pPet), entGetRef(pTarget));
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_AllPetsEnterCombat");
void PetCommands_AllPetsEnterCombat()
{
	ServerCmd_PetCommands_EnterCombat();
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_SetMatchingTargetsLuckyCharm");
void PetCommands_SetAllMatchingTargetsLuckyCharm(EntityRef erTarget, S32 eType)
{
	bool clearOldTargets = (timeSecondsSince2000() - s_LastLuckyCharmAssignment) >= 15;
	s_LastLuckyCharmAssignment = timeSecondsSince2000();
	ServerCmd_PetCommands_SetLuckyCharmOnAllMatchingTargets(erTarget, eType, clearOldTargets);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_SetTargetLuckyCharm");
void PetCommands_SetTargetLuckyCharm(EntityRef erTarget, S32 eType)
{
	bool clearOldTargets = (timeSecondsSince2000() - s_LastLuckyCharmAssignment) >= 15;
	s_LastLuckyCharmAssignment = timeSecondsSince2000();
	ServerCmd_PetCommands_SetLuckyCharm(erTarget, eType, false, clearOldTargets);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_SetTargetLuckyCharmEx");
void PetCommands_SetTargetLuckyCharmEx(EntityRef erTarget, S32 eType, bool bAddAsFirstTarget)
{
	bool clearOldTargets = (timeSecondsSince2000() - s_LastLuckyCharmAssignment) >= 15;
	s_LastLuckyCharmAssignment = timeSecondsSince2000();
	ServerCmd_PetCommands_SetLuckyCharm(erTarget, eType, bAddAsFirstTarget, clearOldTargets);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_ClearAllLuckyCharms");
void PetCommands_ClearAllLuckyCharms()
{
	s_LastLuckyCharmAssignment = timeSecondsSince2000();
	ServerCmd_PetCommands_ClearAllPlayerAttackTargets();
}

AUTO_COMMAND ACMD_CLIENTONLY ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void PetCommands_SetCurrentTargetLuckyCharm(S32 eType)
{
	S32 erTarget = entity_GetTargetRef(entActivePlayerPtr());
	bool clearOldTargets = (timeSecondsSince2000() - s_LastLuckyCharmAssignment) >= 15;
	s_LastLuckyCharmAssignment = timeSecondsSince2000();
	ServerCmd_PetCommands_SetLuckyCharm(erTarget, eType, clearOldTargets, false);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_SetStateByEnt");
void PetCommands_SetStateByEnt(SA_PARAM_OP_VALID Entity* ent, const char* state)
{
	if (!ent) return;
	ServerCmd_PetCommands_SetSpecificPetState(entGetRef(ent), state);
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_SetFollowOwner");
void PetCommands_SetFollow(SA_PARAM_OP_VALID Entity* ent)
{
	if (!ent) return;
	ServerCmd_PetCommands_SetFollowOwner(entGetRef(ent));
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_SetHoldPosition");
void PetCommands_SetHoldPosition(SA_PARAM_OP_VALID Entity* ent)
{
	if (!ent) return;
	ServerCmd_PetCommands_SetHoldPosition(entGetRef(ent));
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_GetPetNameWithOffset");
const char *PetCommands_GetPetNameWithOffset(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, int offset)
{
	int len;
	if (!pEntity) return "";
	len = (int)strlen(entGetLocalName(pEntity));
	if (offset > len) offset = len;
	return entGetLocalName(pEntity) + offset;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_DoesPetSubNameBeginWith");
int PetCommands_DoesPetSubNameBeginWith(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *prefix)
{
	const char *temp;
	if (!pEntity) return false;
	if (!prefix) return false;
	temp = entGetLocalSubName(pEntity);
	if (!temp) return false;
	return strnicmp(temp, prefix, strlen(prefix)) == 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_GetPetSubNameWithOffset");
const char *PetCommands_GetPetSubNameWithOffset(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, int offset)
{
	int len;
	const char *temp;
	if (!pEntity) return "";
	temp = entGetLocalSubName(pEntity);
	if (!temp) return "";
	len = (int)strlen(temp);
	if (offset > len) offset = len;
	return temp + offset;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_GetPetSubNameNumbers");
const char *PetCommands_GetPetSubNameNumbers(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	int len;
	const char *temp;
	char buf[64];
	if (!pEntity) return "";
	temp = entGetLocalSubName(pEntity);
	if (!temp) return "";
	len = 0;
	while (*temp)
	{
		if (*temp >= '0' && *temp <= '9')
		{
			buf[len] = *temp;
			if (++len >= 63) break;
		}
		++temp;
	}
	buf[len] = '\0';
	return allocAddString(buf);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_GetPetSubNameLastIfLetter");
const char *PetCommands_GetPetSubNameLastIfLetter(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	const char *name;
	int len;
	char c;
	if (!pEntity) return "";
	name = entGetLocalSubName(pEntity);
	len = (int)strlen(name);
	if (!len) return "";
	c = name[(len-1)];
	if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
	{
		return name + len - 1;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_GetPlayerPetInfo");
SA_RET_OP_VALID PlayerPetInfo* PetCommands_GetPlayerPetInfo(SA_PARAM_NN_VALID Entity* pEnt)
{
	if(pEnt && pEnt->pPlayer)
		return eaGet(&pEnt->pPlayer->petInfo, 0);
	else
		return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_GetPlayerPetInfoForEnt");
SA_RET_OP_VALID PlayerPetInfo* PetCommands_GetPlayerPetInfoForEnt(SA_PARAM_OP_VALID Entity* pEnt)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	
	if ( pPlayerEnt && pPlayerEnt->pPlayer && pEnt )
	{
		S32 i;
		for (i = eaSize(&pPlayerEnt->pPlayer->petInfo) - 1; i >= 0; i-- )
		{
			if ( pPlayerEnt->pPlayer->petInfo[i]->iPetRef == pEnt->myRef )
			{
				return pPlayerEnt->pPlayer->petInfo[i];
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_GetPetTargetRef");
U32 PetCommands_GetPetTargetRef(Entity *pPet)
{
	S32 i;
	Entity* pPlayerEnt = entActivePlayerPtr();
	
	if ( pPlayerEnt && pPet)
	{
		EntityRef erPet = entGetRef(pPet);
		if ( team_IsMember(pPlayerEnt) ) //if the player is on a team, search the per-team list
		{
			TeamMapValues* pTeamMapValues = mapState_FindTeamValues(mapStateClient_Get(),pPlayerEnt->pTeam->iTeamID);

			if ( pTeamMapValues )
			{
				for ( i = 0; i < eaSize(&pTeamMapValues->eaPetTargetingInfo); i++) 
				{
					if ( pTeamMapValues->eaPetTargetingInfo[i]->erPet == erPet )
					{
						return pTeamMapValues->eaPetTargetingInfo[i]->erTarget;
					}
				}
			}
		}
		else //if the player is not on team, search the per-player list
		{
			PlayerMapValues* pPlayerMapValues = mapState_FindPlayerValues(PARTITION_CLIENT, entGetContainerID(pPlayerEnt));

			if ( pPlayerMapValues )
			{
				for ( i = 0; i < eaSize(&pPlayerMapValues->eaPetTargetingInfo); i++) 
				{
					if ( pPlayerMapValues->eaPetTargetingInfo[i]->erPet == erPet )
					{
						return pPlayerMapValues->eaPetTargetingInfo[i]->erTarget;
					}
				}
			}
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_GetTargettedEntRefByType");
U32 PetCommands_GetTargettedEntRefByType(S32 eType)
{
	S32 i;
	Entity* pPlayerEnt = entActivePlayerPtr();

	if ( pPlayerEnt)
	{
		if ( team_IsMember(pPlayerEnt) ) //if the player is on a team, search the per-team list
		{
			TeamMapValues* pTeamMapValues = mapState_FindTeamValues(mapStateClient_Get(),pPlayerEnt->pTeam->iTeamID);

			if ( pTeamMapValues )
			{
				for ( i = 0; i < eaSize(&pTeamMapValues->eaPetTargetingInfo); i++) 
				{
					if ( pTeamMapValues->eaPetTargetingInfo[i]->eType == eType )
					{
						return pTeamMapValues->eaPetTargetingInfo[i]->erTarget;
					}
				}
			}
		}
		else //if the player is not on team, search the per-player list
		{
			PlayerMapValues* pPlayerMapValues = mapState_FindPlayerValues(PARTITION_CLIENT, entGetContainerID(pPlayerEnt));

			if ( pPlayerMapValues )
			{
				for ( i = 0; i < eaSize(&pPlayerMapValues->eaPetTargetingInfo); i++) 
				{
					if ( pPlayerMapValues->eaPetTargetingInfo[i]->eType == eType )
					{
						return pPlayerMapValues->eaPetTargetingInfo[i]->erTarget;
					}
				}
			}
		}
	}

	return 0;
}


AUTO_STRUCT;
typedef struct PetTargetingData
{
	EntityRef iPetRef;
} PetTargetingData;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_GetTargetTypeForEnt");
S32 PetCommands_GetTargetTypeForEnt(SA_PARAM_OP_VALID Entity* pEntity)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	S32 i, iCount = 0;
	EntityRef erTarget;

	if ( pEntity==NULL || pPlayerEnt==NULL )
		return 0;

	erTarget = entGetRef(pEntity);

	if ( team_IsMember(pPlayerEnt) ) //if the player is on a team, search the per-team list
	{
		TeamMapValues* pTeamMapValues = mapState_FindTeamValues(mapStateClient_Get(),pPlayerEnt->pTeam->iTeamID);

		if ( pTeamMapValues )
		{
			for ( i = 0; i < eaSize(&pTeamMapValues->eaPetTargetingInfo); i++ )
			{
				if ( pTeamMapValues->eaPetTargetingInfo[i]->erTarget == erTarget )
				{
					return pTeamMapValues->eaPetTargetingInfo[i]->eType;
				}
			}
		}
	}
	else //if the player is not on team, search the per-player list
	{
		PlayerMapValues* pPlayerMapValues = mapState_FindPlayerValues(PARTITION_CLIENT, entGetContainerID(pPlayerEnt));

		if ( pPlayerMapValues )
		{
			for ( i = 0; i < eaSize(&pPlayerMapValues->eaPetTargetingInfo); i++ )
			{
				if ( pPlayerMapValues->eaPetTargetingInfo[i]->erTarget == erTarget )
				{
					return pPlayerMapValues->eaPetTargetingInfo[i]->eType;
				}
			}
		}
	}
	return -1;
}

int PetCommands_LuckyCharmsSortByIndex(const PetTargetingInfo **pptr1, const PetTargetingInfo **pptr2)
{
	return (*pptr1)->iIndex - (*pptr2)->iIndex;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_IsEntFirstLuckyCharmTarget");
bool PetCommands_IsEntFirstLuckyCharmTarget(SA_PARAM_OP_VALID Entity* pEntity, int eType)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	S32 i;
	S32 iLowestIndex = INT_MAX;
	S32 iEntIndex = INT_MIN;
	EntityRef erTarget;

	if (pEntity == NULL || pPlayerEnt == NULL)
		return false;

	erTarget = entGetRef(pEntity);

	if ( team_IsMember(pPlayerEnt) ) //if the player is on a team, search the per-team list
	{
		TeamMapValues* pTeamMapValues = mapState_FindTeamValues(mapStateClient_Get(),pPlayerEnt->pTeam->iTeamID);

		if ( pTeamMapValues )
		{
			for ( i = 0; i < eaSize(&pTeamMapValues->eaPetTargetingInfo); i++ )
			{
				Entity* pEnt = entFromEntityRefAnyPartition(pTeamMapValues->eaPetTargetingInfo[i]->erTarget);
				if ( pTeamMapValues->eaPetTargetingInfo[i]->eType == eType && 
					pEnt && entIsAlive(pEnt))
				{
					if (pEnt == pEntity)
					{
						iEntIndex = pTeamMapValues->eaPetTargetingInfo[i]->iIndex;
					}
					iLowestIndex = min(iLowestIndex, pTeamMapValues->eaPetTargetingInfo[i]->iIndex);
				}
			}
		}
	}
	else //if the player is not on team, search the per-player list
	{
		PlayerMapValues* pPlayerMapValues = mapState_FindPlayerValues(PARTITION_CLIENT, entGetContainerID(pPlayerEnt));

		if ( pPlayerMapValues )
		{
			for ( i = 0; i < eaSize(&pPlayerMapValues->eaPetTargetingInfo); i++ )
			{
				Entity* pEnt = entFromEntityRefAnyPartition(pPlayerMapValues->eaPetTargetingInfo[i]->erTarget);
				if ( pPlayerMapValues->eaPetTargetingInfo[i]->eType == eType && 
					pEnt && entIsAlive(pEnt))
				{
					if (pEnt == pEntity)
					{
						iEntIndex = pPlayerMapValues->eaPetTargetingInfo[i]->iIndex;
					}
					iLowestIndex = min(iLowestIndex, pPlayerMapValues->eaPetTargetingInfo[i]->iIndex);
				}
			}
		}
	}

	return iLowestIndex == iEntIndex;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_GetLuckyCharmIndexForEntOfType");
int PetCommands_GetLuckyCharmIndexForEntOfType(SA_PARAM_OP_VALID Entity* pEntity, int eType)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	S32 i;
	EntityRef erTarget;
	PetTargetingInfo** eaTempLuckyCharms = NULL;


	if ( pEntity==NULL || pPlayerEnt==NULL )
		return 0;

	erTarget = entGetRef(pEntity);

	if ( team_IsMember(pPlayerEnt) ) //if the player is on a team, search the per-team list
	{
		TeamMapValues* pTeamMapValues = mapState_FindTeamValues(mapStateClient_Get(),pPlayerEnt->pTeam->iTeamID);

		if ( pTeamMapValues )
		{
			for ( i = 0; i < eaSize(&pTeamMapValues->eaPetTargetingInfo); i++ )
			{
				Entity* pEnt = entFromEntityRefAnyPartition(pTeamMapValues->eaPetTargetingInfo[i]->erTarget);
				if ( pTeamMapValues->eaPetTargetingInfo[i]->eType == eType && 
					pEnt && entIsAlive(pEnt))
				{
					eaPush(&eaTempLuckyCharms, pTeamMapValues->eaPetTargetingInfo[i]);
				}
			}
		}
	}
	else //if the player is not on team, search the per-player list
	{
		PlayerMapValues* pPlayerMapValues = mapState_FindPlayerValues(PARTITION_CLIENT, entGetContainerID(pPlayerEnt));

		if ( pPlayerMapValues )
		{
			for ( i = 0; i < eaSize(&pPlayerMapValues->eaPetTargetingInfo); i++ )
			{
				Entity* pEnt = entFromEntityRefAnyPartition(pPlayerMapValues->eaPetTargetingInfo[i]->erTarget);
				if ( pPlayerMapValues->eaPetTargetingInfo[i]->eType == eType && 
					pEnt && entIsAlive(pEnt))
				{
					eaPush(&eaTempLuckyCharms, pPlayerMapValues->eaPetTargetingInfo[i]);
				}
			}
		}
	}

	if (eaSize(&eaTempLuckyCharms))
	{
		eaQSort(eaTempLuckyCharms, PetCommands_LuckyCharmsSortByIndex);
		for (i = 0; i < eaSize(&eaTempLuckyCharms); i++)
		{
			if (eaTempLuckyCharms[i]->erTarget == erTarget)
			{
				eaDestroy(&eaTempLuckyCharms);
				return i;
			}
		}
		eaDestroy(&eaTempLuckyCharms);
	}
	return -1;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_GetPetsTargetingEnt");
S32 PetCommands_GetPetsTargetingEnt(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEntity)
{
	PetTargetingData ***peaData = ui_GenGetManagedListSafe(pGen, PetTargetingData);
	Entity* pPlayerEnt = entActivePlayerPtr();
	S32 i, iCount = 0;
	EntityRef erTarget;

	if ( pEntity==NULL || pPlayerEnt==NULL )
		return 0;

	erTarget = entGetRef(pEntity);

	if ( team_IsMember(pPlayerEnt) ) //if the player is on a team, search the per-team list
	{
		TeamMapValues* pTeamMapValues = mapState_FindTeamValues(mapStateClient_Get(),pPlayerEnt->pTeam->iTeamID);

		if ( pTeamMapValues )
		{
			for ( i = 0; i < eaSize(&pTeamMapValues->eaPetTargetingInfo); i++ )
			{
				if ( pTeamMapValues->eaPetTargetingInfo[i]->erTarget == erTarget )
				{
					PetTargetingData* pData = eaGetStruct( peaData, parse_PetTargetingData, iCount++ );
					pData->iPetRef = pTeamMapValues->eaPetTargetingInfo[i]->erPet;
				}
			}
		}
	}
	else //if the player is not on team, search the per-player list
	{
		PlayerMapValues* pPlayerMapValues = mapState_FindPlayerValues(PARTITION_CLIENT, entGetContainerID(pPlayerEnt));

		if ( pPlayerMapValues )
		{
			for ( i = 0; i < eaSize(&pPlayerMapValues->eaPetTargetingInfo); i++ )
			{
				if ( pPlayerMapValues->eaPetTargetingInfo[i]->erTarget == erTarget )
				{
					PetTargetingData* pData = eaGetStruct( peaData, parse_PetTargetingData, iCount++ );
					pData->iPetRef = pPlayerMapValues->eaPetTargetingInfo[i]->erPet;
				}
			}
		}
	}

	while ( eaSize(peaData) > iCount )
		StructDestroy(parse_PetTargetingData, eaPop(peaData));

	ui_GenSetManagedListSafe(pGen, peaData, PetTargetingData, true);
	return eaSize(peaData);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_GetPetState");
const char* PetCommands_GetPetState(SA_PARAM_OP_VALID PlayerPetInfo* petInfo)
{
	if ( petInfo )
		return petInfo->curPetState;

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_GetPetStance");
const char* PetCommands_GetPetStance(SA_PARAM_OP_VALID PlayerPetInfo* petInfo, int iStance)
{
	if (petInfo)
	{
		PetStanceInfo *pStanceInfo = eaGet(&petInfo->eaStances, iStance);
		if (pStanceInfo)
			return pStanceInfo->curStance;
	}
	

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_GetPetStanceDisplayName");
const char* PetCommands_GetPetStanceDisplayName(SA_PARAM_NN_VALID Entity* pEnt, S32 iStance)
{
	PlayerPetInfo *petInfo = PetCommands_GetPlayerPetInfoForEnt(pEnt);
	
	if (petInfo)
	{
		PetStanceInfo *pStanceInfo = eaGet(&petInfo->eaStances, iStance);
		
		if (pStanceInfo)
		{
			FOR_EACH_IN_EARRAY(pStanceInfo->validStances, PetCommandNameInfo, pPetCmdInfo);
				if (pPetCmdInfo->pchName == pStanceInfo->curStance)
				{
					Message *pMsg = GET_REF(pPetCmdInfo->pchDisplayName);
					return (pMsg) ? pMsg->pcMessageKey : NULL;
				}
			FOR_EACH_END;
		}
		
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_GetPetStateList");
void PetCommands_GetPetStateList(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID PlayerPetInfo* petInfo)
{
	PetCommandNameInfo ***peaList = ui_GenGetManagedListSafe(pGen, PetCommandNameInfo);
	int i;

	eaClearFast(peaList);
	if (petInfo)
	{
		for (i=0; i<eaSize(&petInfo->validStates); i++)
		{
			if (petInfo->validStates[i]->bHidden)
				continue;

			eaPush(peaList, petInfo->validStates[i]);
		}
	}
	ui_GenSetManagedListSafe(pGen, peaList, PetCommandNameInfo, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_GetPetStanceList");
void PetCommands_GetPetStanceList(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_NN_VALID Entity* pEnt, int iStance)
{
	PlayerPetInfo *petInfo = PetCommands_GetPlayerPetInfoForEnt(pEnt);

	if (petInfo)
	{
		PetStanceInfo *pStanceInfo = eaGet(&petInfo->eaStances, iStance);
		if (pStanceInfo)
		{
			ui_GenSetList(pGen, &pStanceInfo->validStances, parse_PetCommandNameInfo);
			return;
		}
	}
	
	ui_GenSetList(pGen, NULL, parse_PetCommandNameInfo);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_SetStanceByEnt");
void PetCommands_SetStanceByEnt(SA_PARAM_OP_VALID Entity* ent, int iStance, const char* stance)
{
	if (!ent) return;
	ServerCmd_PetCommands_SetSpecificPetStance(entGetRef(ent), iStance, stance);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetPetEntityFromName");
SA_RET_OP_VALID Entity* PetExpr_EntGetPetEntityFromName(SA_PARAM_OP_VALID Entity* pOwner, const char *pcName)
{
	S32 i, iPetArraySize = (pOwner && pOwner->pSaved) ? eaSize(&pOwner->pSaved->ppOwnedContainers) : 0;

	for ( i = 0; i < iPetArraySize; i++ )
	{
		PetRelationship* pPet = pOwner->pSaved->ppOwnedContainers[i];

		Entity* pEnt = GET_REF( pPet->hPetRef );

		if ( pEnt && pEnt->pSaved && !stricmp(entGetLocalName(pEnt), pcName) )
		{
			return pEnt;
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetPetEntityFromPetID");
SA_RET_OP_VALID Entity* PetExpr_EntGetPetEntityFromPetID(SA_PARAM_OP_VALID Entity* pOwner, U32 uiPetID)
{
	return SavedPet_GetEntityFromPetID(pOwner, uiPetID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetPetEntity");
SA_RET_OP_VALID Entity* PetExpr_EntGetPetEntity(SA_PARAM_OP_VALID Entity* pOwner, U32 iContainerID)
{
	S32 i, iPetArraySize = (pOwner && pOwner->pSaved) ? eaSize(&pOwner->pSaved->ppOwnedContainers) : 0;

	for ( i = 0; i < iPetArraySize; i++ )
	{
		PetRelationship* pPet = pOwner->pSaved->ppOwnedContainers[i];

		Entity* pEnt = GET_REF( pPet->hPetRef );

		if ( pEnt && pEnt->myContainerID == iContainerID )
			return pEnt;
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetGetEnt");
SA_RET_OP_VALID Entity* PetExpr_GetPetEntity(SA_PARAM_OP_VALID PetRelationship* pPet)
{
	return pPet ? SavedPet_GetEntity(PARTITION_CLIENT, pPet) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PuppetGetEnt");
SA_RET_OP_VALID Entity* PuppetExpr_GetPuppetEntity(SA_PARAM_OP_VALID PuppetEntity* pPuppet)
{
	return pPuppet ? SavedPuppet_GetEntity(PARTITION_CLIENT, pPuppet) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetCharClassCategorySet");
SA_RET_OP_VALID CharClassCategorySet* PuppetExpr_GetGetCharClassCategorySet(SA_PARAM_OP_VALID Entity* pEntity)
{
	CharacterClass *pClass = SAFE_GET_REF2(pEntity, pChar, hClass);
	return CharClassCategorySet_getCategorySetFromClass(pClass);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetCurrentOrPreferredCharClassCategorySet");
SA_RET_OP_VALID CharClassCategorySet* PuppetExpr_GetCurrentOrPreferredCharClassCategorySet(SA_PARAM_OP_VALID Entity* pEntity, const char *pchType)
{
	CharacterClass *pClass = SAFE_GET_REF2(pEntity, pChar, hClass);
	S32 eType = StaticDefineIntGetInt(CharClassTypesEnum, pchType);
	if (SAFE_MEMBER(pClass, eType) == eType)
	{
		return CharClassCategorySet_getCategorySetFromClass(pClass);
	}
	else
	{
		CharClassCategorySet *pSet = CharClassCategorySet_getPreferredSet(pEntity);
		if (SAFE_MEMBER(pSet, eClassType) == eType)
			return pSet;
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPuppetBySetNameAndType");
SA_RET_OP_VALID Entity* PuppetExpr_GetPuppetEntityBySetNameAndType(SA_PARAM_OP_VALID Entity* pEntity,  const char *pchSet, const char *pchType)
{
	return pEntity ? entity_GetPuppetEntityByType( pEntity, pchType, RefSystem_ReferentFromString(g_hCharacterClassCategorySetDict, pchSet), false, true ) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPuppetBySetAndType");
SA_RET_OP_VALID Entity* PuppetExpr_GetPuppetEntityBySetAndType(SA_PARAM_OP_VALID Entity* pEntity, SA_PARAM_OP_VALID CharClassCategorySet *pSet, const char *pchType)
{
	return pEntity ? entity_GetPuppetEntityByType( pEntity, pchType, pSet, false, true ) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPuppetByType");
SA_RET_OP_VALID Entity* PuppetExpr_GetPuppetEntityByType(SA_PARAM_OP_VALID Entity* pEntity, const char *pchType)
{
	return pEntity ? entity_GetPuppetEntityByType( pEntity, pchType, NULL, false, true ) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetEntityOrPuppetByType");
SA_RET_OP_VALID Entity* PetExpr_GenGetEntityOrPuppetByType(SA_PARAM_OP_VALID Entity* pEnt, const char* pchClassType)
{
	return pEnt ? entity_GetPuppetEntityByType( pEnt, pchClassType, NULL, true, true ) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetEntityOrPuppetBySetAndType");
SA_RET_OP_VALID Entity* PetExpr_GenGetEntityOrPuppetBySetAndType(SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_OP_VALID CharClassCategorySet *pSet, const char* pchClassType)
{
	return pEnt ? entity_GetPuppetEntityByType( pEnt, pchClassType, pSet, true, true ) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetEntityOrPuppetBySetNameAndType");
SA_RET_OP_VALID Entity* PetExpr_GenGetEntityOrPuppetBySetNameAndType(SA_PARAM_OP_VALID Entity* pEnt,  const char *pchSet, const char* pchClassType)
{
	return pEnt ? entity_GetPuppetEntityByType( pEnt, pchClassType, RefSystem_ReferentFromString(g_hCharacterClassCategorySetDict, pchSet), true, true ) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetPetEntityByType");
SA_RET_OP_VALID Entity* PetExpr_GenGetPetEntityByType(SA_PARAM_OP_VALID Entity* pOwner, const char* pchClassTypes)
{
	static CharClassTypes *s_eaiTypes;
	S32 i, iBest = -1, iPetArraySize = (pOwner && pOwner->pSaved) ? eaSize(&pOwner->pSaved->ppOwnedContainers) : 0;
	char *pchBuffer = NULL, *pchContext = NULL, *pchToken;
	Entity *pBestPet = NULL;

	eaiClearFast(&s_eaiTypes);
	strdup_alloca(pchBuffer, pchClassTypes);
	if (pchToken = strtok_r(pchBuffer, " \r\n\t,|%", &pchContext))
	{
		do
		{
			CharClassTypes type = StaticDefineIntGetInt(CharClassTypesEnum, pchToken);
			if (type != -1)
				eaiPush(&s_eaiTypes, type);
		} while (pchToken = strtok_r(NULL, " \r\n\t,|%", &pchContext));
	}

	for (i = 0; i < iPetArraySize; i++)
	{
		PetRelationship *pPet = pOwner->pSaved->ppOwnedContainers[i];
		Entity *pEnt = GET_REF(pPet->hPetRef);
		CharacterClass *pClass = pEnt && pEnt->pChar ? GET_REF(pEnt->pChar->hClass) : NULL;
		S32 iPos = pClass ? eaiFind(&s_eaiTypes, pClass->eType) : -1;

		// prioritize the return value based on the position in the list of class types
		if (iPos == 0)
		{
			return pEnt;
		}
		else if (iPos > 0 && (iPos < iBest || iBest < 0))
		{
			iBest = iPos;
			pBestPet = pEnt;
		}
	}

	return pBestPet;
}

AUTO_STRUCT;
typedef struct SavedPetSortData
{
	S32* piSortFirst;
	S32* piSortLast;
} SavedPetSortData;

static S32 PetUI_Names_sortfunc_s(const SavedPetSortData* pSortData, const PetRowDataID **ppParamA, const PetRowDataID **ppParamB )
{
	const PetRowDataID* pPetA = (*ppParamA);
	const PetRowDataID* pPetB = (*ppParamB);
	S32 iOrderA, iOrderB;

	if (pPetA->bIsExtraPetSlot)
		return 1;
	if (pPetB->bIsExtraPetSlot)
		return -1;
	if (pPetA->bIsExtraPuppetSlot)
		return 1;
	if (pPetB->bIsExtraPuppetSlot)
		return -1;

	if ( pSortData->piSortLast )
	{
		S32 iLastA = ea32Find( &pSortData->piSortLast, pPetA->iClass );
		S32 iLastB = ea32Find( &pSortData->piSortLast, pPetB->iClass );

		if ( iLastA >= 0 && iLastB >= 0 )
		{
			if ( iLastA != iLastB )
				return iLastB - iLastA;

			if ( pPetA->bActive != pPetB->bActive )
				return ( pPetB->bActive - pPetA->bActive );

			return stricmp(pPetA->pcName,pPetB->pcName);
		}

		if ( iLastA >= 0 )
			return 1;

		if ( iLastB >= 0 )
			return -1;
	}

	iOrderA = ea32Find( &pSortData->piSortFirst, pPetA->iClass );
	iOrderB = ea32Find( &pSortData->piSortFirst, pPetB->iClass );

	if ( iOrderA != iOrderB )
	{
		if ( iOrderA >= 0 && iOrderB < 0 )
			return -1;

		if ( iOrderB >= 0 && iOrderA < 0 )
			return 1;

		return iOrderA - iOrderB;
	}
	else if ( pPetA->bActive != pPetB->bActive )
	{
		return ( pPetB->bActive - pPetA->bActive );
	}

	return stricmp(pPetA->pcName,pPetB->pcName);
}

static S32 PetUI_Names_sortfunc(const PetRowDataID **ppParamA, const PetRowDataID **ppParamB )
{
	const PetRowDataID* pPetA = (*ppParamA);
	const PetRowDataID* pPetB = (*ppParamB);

	if (pPetA->bIsExtraPetSlot)
		return 1;
	if (pPetB->bIsExtraPetSlot)
		return -1;
	if (pPetA->bIsExtraPuppetSlot)
		return 1;
	if (pPetB->bIsExtraPuppetSlot)
		return -1;

	return stricmp(pPetA->pcName,pPetB->pcName);
}

static void PetUI_CreateInternalName( char* pchTemp, S32 iSize )
{
	pchTemp[5] = '\0';
	pchTemp[4] = (iSize % 10) + '0'; iSize /= 10;
	pchTemp[3] = (iSize % 10) + '0'; iSize /= 10;
	pchTemp[2] = (iSize % 10) + '0'; iSize /= 10;
	pchTemp[1] = 'n';
	pchTemp[0] = 'i';
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetClassTypeNameFromID");
const char* PetExpr_GetClassTypeNameFromID(U32 iClass) 
{
	return StaticDefineIntRevLookup(CharClassTypesEnum, iClass);
}

// Set the list model of a UIGen to the default list of pets to add in the away team picker
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPreferredPetList");
S32 PetExpr_GetPreferredPetList( SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity* pEntity, bool bUseLastName, int iMaxLen )
{
	PetRowDataID ***peaPets = ui_GenGetManagedListSafe(pGen, PetRowDataID);
	S32 i, iSize = 0;

	if (!pEntity || !pEntity->pSaved)
	{
		eaClearStruct(peaPets, parse_PetRowDataID);
		ui_GenSetManagedListSafe(pGen, peaPets, PetRowDataID, true);
		return 0;
	}

	for ( i = 0; i < ea32Size(&pEntity->pSaved->ppPreferredPetIDs); i++ )
	{
		U32 uiPetID = pEntity->pSaved->ppPreferredPetIDs[i];
		PetRelationship* pPet = SavedPet_GetPetFromContainerID( pEntity, uiPetID, true );
		Entity* pPetEnt = pPet ? GET_REF(pPet->hPetRef) : NULL;
		CharacterClass* pClass = pPetEnt && pPetEnt->pChar ? GET_REF(pPetEnt->pChar->hClass) : NULL;
		if ( pPetEnt && pClass )
		{
			PetRowDataID* pData = eaGetStruct( peaPets, parse_PetRowDataID, iSize++ );
			const char *pchName = entGetLocalName(pPetEnt);
			pData->pEnt = pPetEnt;
			pData->bActive = pPet->eState == OWNEDSTATE_ACTIVE;
			pData->iID = entGetContainerID( pPetEnt );
			pData->iType = entGetType( pPetEnt );
			pData->iClass = pClass->eType;
			if (stricmp_safe(pData->pcName, pchName))
				StructCopyString(&pData->pcName, EMPTY_TO_NULL(pchName));
			pData->iLen = pData->pcName ? (int)strlen(pData->pcName) : 0;
		}
	}

	for ( i = iSize; i < TEAM_MAX_SIZE-1; i++ )
	{
		PetRowDataID* pData = eaGetStruct( peaPets, parse_PetRowDataID, iSize++ );
		pData->pEnt = NULL;
		pData->bActive = false;
		pData->iID = 0;
		pData->iType = 0;
		pData->iClass = 0;
		pData->pcName = NULL;
		pData->iLen = 0;
	}

	ui_GenSetManagedListSafe(pGen, peaPets, PetRowDataID, true);
	return iSize;
}

static bool PetExpr_IsCritterPetSummoned(SA_PARAM_OP_VALID Entity* pOwner, PetDef *pPetDef)
{
	if (pOwner && pOwner->pSaved && 
		eaSize(&pOwner->pSaved->ppCritterPets) && pPetDef)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pOwner->pSaved->ppCritterPets, CritterPetRelationship, pRelationShip)
		{
			PetDef *pCurPetDef = pRelationShip ? GET_REF(pRelationShip->hPetDef) : NULL;
			if (pCurPetDef == pPetDef)
			{
				return pRelationShip->erPet > 0;
			}
		}
		FOR_EACH_END
	}
	return false;
}

static void gclPetList_AddPet(PetRowDataID*** peaPets,
							  S32* piPetListSize,
							  Entity *pOwner,
							  Entity *pEntity, 
							  PetDef *pPetDef,
							  CharacterClass *pClass,
							  U32 uCritterPetID,
							  bool bPuppetActive,
							  bool bActive,
							  bool bCurrent)
{
	const char* pchPetName = NULL;

	// Get the pet name
	if (pEntity)
	{
		pPetDef = pEntity->pCritter ? GET_REF(pEntity->pCritter->petDef) : NULL;
		if (pEntity != pOwner)
		{
			pchPetName = entGetLocalName(pEntity);
		}
		if (!pchPetName)
		{
			if (pPetDef)
			{
				pchPetName = TranslateDisplayMessage(pPetDef->displayNameMsg);
			}
			else if (pClass)
			{
				pchPetName = TranslateDisplayMessage(pClass->msgDisplayName);
			}
		}
	}
	else if (pPetDef)
	{
		pchPetName = TranslateDisplayMessage(pPetDef->displayNameMsg);
	}
	else
	{
		return;
	}

	// Add info about the pet to the list if the pet's class is in the filter list
	if (pOwner)
	{
		char pchBuffer[1024];
		U32 iID = uCritterPetID ? entGetContainerID(pOwner) : entGetContainerID(pEntity);
		GlobalType iType = uCritterPetID ? GLOBALTYPE_ENTITYCRITTER : entGetType(pEntity);
		PetRowDataID* pData = NULL;
		S32 i;

		// Keep list pointers stable
		for (i = *piPetListSize; i < eaSize(peaPets); i++)
		{
			if ((*peaPets)[i]->iID == iID && (*peaPets)[i]->iType == iType && (*peaPets)[i]->iCritterID == uCritterPetID)
			{
				if (i != *piPetListSize)
					eaSwap(peaPets, i, *piPetListSize);
				pData = eaGetStruct(peaPets, parse_PetRowDataID, *piPetListSize);
				break;
			}
		}
		if (!pData)
		{
			pData = StructCreate(parse_PetRowDataID);
			eaInsert(peaPets, pData, *piPetListSize);
		}

		if (pClass)
		{
			CharClassCategorySet *pSet = CharClassCategorySet_getCategorySetFromClass(pClass);
			pData->pcPuppetCategorySet = SAFE_MEMBER(pSet, pchName);
		}

		pData->pEnt = pEntity ? pEntity : pOwner;
		pData->bActive = bActive;
		pData->bCurrent = bCurrent;
		pData->bPuppetActive = bPuppetActive;
		pData->iID = iID;
		pData->iType = iType;
		pData->iClass = SAFE_MEMBER(pClass, eType);
		StructCopyString(&pData->pcName, pchPetName);
		PetUI_CreateInternalName(pchBuffer, *piPetListSize);
		StructCopyString(&pData->pcInternalName, pchBuffer);
		pData->iLen = pData->pcName ? (int)strlen(pData->pcName) : 0;
		pData->iCritterID = uCritterPetID;
		pData->bIsExtraPetSlot = pData->bIsExtraPuppetSlot = false;
		pData->bIsCritterPet = (uCritterPetID > 0);
		StructFreeStringSafe(&pData->pcPetDefName);
		StructFreeStringSafe(&pData->pcCritterName);
		pData->iMinActivePuppetLevel = 0;
		if (pPetDef)
		{
			pData->iMinActivePuppetLevel = pPetDef->iMinActivePuppetLevel;
			pData->pcPetDefName = StructAllocString(pPetDef->pchPetName);
			pData->pcCritterName = StructAllocString(TranslateDisplayMessage(pPetDef->displayNameMsg));
		}
		(*piPetListSize)++;
	}
}

// Create an 'extra' slot, indicating that the player may choose to fill the slot, but that it's currently empty
static void gclPetList_CreateExtraSlot(PetRowDataID*** peaPets, 
									   S32* piPetListSize, 
									   Entity* pEntity, 
									   bool bPetSlot, 
									   bool bPuppetSlot)
{
	PetRowDataID* pData = NULL;
	S32 i;

	// Keep list pointers stable
	for (i = *piPetListSize; i < eaSize(peaPets); i++)
	{
		if ((*peaPets)[i]->iID == 0 && (*peaPets)[i]->iType == 0 && (*peaPets)[i]->iCritterID == 0 && (!pData || pData > (*peaPets)[i]))
		{
			if (i != *piPetListSize)
				eaSwap(peaPets, i, *piPetListSize);
			pData = eaGetStruct(peaPets, parse_PetRowDataID, *piPetListSize);
		}
	}
	if (!pData)
	{
		pData = StructCreate(parse_PetRowDataID);
		eaInsert(peaPets, pData, *piPetListSize);
	}

	pData->pEnt = pEntity;
	pData->iID = pData->iType = pData->iCritterID = pData->iClass = pData->iLen = 0;
	pData->iMinActivePuppetLevel = 0;
	pData->bActive = pData->bCurrent = false;
	StructFreeStringSafe(&pData->pcName);
	StructFreeStringSafe(&pData->pcInternalName);
	StructFreeStringSafe(&pData->pcCritterName);
	pData->bIsExtraPetSlot = bPetSlot;
	pData->bIsExtraPuppetSlot = bPuppetSlot;
	pData->bIsCritterPet = false;
	StructFreeStringSafe(&pData->pcPetDefName);
	(*piPetListSize)++;
}

// Check to see if the pet should be added to the list based on filter rules
static bool gclPetList_CheckPetFilter(Entity* pEntity, 
									  Entity* pPetEnt, 
									  PetRelationship* pPet, 
									  PuppetEntity* pPuppet,
									  UIPetListFlags ePetFlags,
									  bool bExcludeAlreadySummoned)
{
	if (bExcludeAlreadySummoned &&
		(pPet->eState == OWNEDSTATE_AUTO_SUMMON || pPet->eState == OWNEDSTATE_ACTIVE))
	{
		return false;
	}
	if (ePetFlags & (kUIPetListFlag_ActivePuppets|kUIPetListFlag_CurrentAndActivePuppets))
	{
		if (pPuppet && pPuppet->eState == PUPPETSTATE_ACTIVE)
			return true;
	}
	if (ePetFlags & kUIPetListFlag_InactivePuppets)
	{
		if (pPuppet && pPuppet->eState != PUPPETSTATE_ACTIVE && Entity_CanModifyPuppet(pEntity, pPetEnt))
			return true;
	}
	if (ePetFlags & kUIPetListFlag_SavedPets)
	{
		if (!pPuppet && pPetEnt && pPetEnt->pSaved)
			return true;
	}
	if (ePetFlags & kUIPetListFlag_TrainingPets)
	{
		if (!pPuppet && pPetEnt && pPetEnt->pChar && eaSize(&pPetEnt->pChar->ppTraining) > 0)
			return true;
	}
	if (ePetFlags & kUIPetListFlag_NotTrainingPets)
	{
		if (!pPuppet && pPetEnt && pPetEnt->pChar && eaSize(&pPetEnt->pChar->ppTraining) == 0)
			return true;
	}
	return false;
}

static bool gclPetList_CheckClass(CharacterClass* pClass, 
								  CharClassTypes* piAllowedClassTypes, 
								  CharClassCategory* piIncludeClassCategories)
{
	if (piAllowedClassTypes && pClass && eaiFind(&piAllowedClassTypes, pClass->eType) < 0)
	{
		return false;
	}
	if (piIncludeClassCategories && pClass && eaiFind(&piIncludeClassCategories, pClass->eCategory) < 0)
	{
		return false;
	}
	return true;
}

S32 gclGenGetPetList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity* pEntity,
					 CharClassTypes* piIncludeClassTypes,
					 UIPetListFlags ePetFlags, 
					 SavedPetSortData* pSortData,
					 bool bExcludeAlreadySummoned)
{
	PetRowDataID ***peaPets = ui_GenGetManagedListSafe(pGen, PetRowDataID);
	int i, iSize = 0;

	if(!pEntity || !pEntity->pChar || !pEntity->pSaved || !ePetFlags)
	{
		eaClearStruct(peaPets, parse_PetRowDataID);
		ui_GenSetManagedListSafe(pGen, peaPets, PetRowDataID, true);
		return 0;
	}

	// Create list elements for saved pets
	if (ePetFlags & (kUIPetListFlag_AllPets|kUIPetListFlag_AllPuppets))
	{
		if ((ePetFlags & kUIPetListFlag_CurrentAndActivePuppets) &&
			SAFE_MEMBER(pEntity->pSaved->pPuppetMaster, curTempID))
		{
			CharacterClass* pClass = GET_REF(pEntity->pChar->hClass);

			if (gclPetList_CheckClass(pClass, piIncludeClassTypes, NULL))
			{
				gclPetList_AddPet(peaPets, &iSize, pEntity, pEntity, NULL, pClass, 0, false, false, true);
			}
		}
		for (i = 0;i < eaSize(&pEntity->pSaved->ppOwnedContainers); i++)
		{
			PetRelationship* pPet = pEntity->pSaved->ppOwnedContainers[i];
			PuppetMaster* pPuppetMaster = pEntity->pSaved->pPuppetMaster;
			PuppetEntity* pPuppet = SavedPet_GetPuppetFromPet(pEntity, pPet);
			Entity* pPetEnt = GET_REF(pPet->hPetRef);
			
			if (!gclPetList_CheckPetFilter(pEntity, 
										   pPetEnt, 
										   pPet, 
										   pPuppet, 
										   ePetFlags,
										   bExcludeAlreadySummoned))
			{
				continue;
			}
			if (pPetEnt && pPetEnt->pChar)
			{
				bool bActive = (pPet->eState == OWNEDSTATE_ACTIVE);
				bool bCurrent = false;
				bool bPuppetActive = false;
				CharacterClass* pClass = GET_REF(pPetEnt->pChar->hClass);

				if (!gclPetList_CheckClass(pClass, piIncludeClassTypes, NULL))
				{
					continue;
				}
				if (pPuppet)
				{
					bCurrent = (pPuppetMaster->curType == pPuppet->curType && pPuppetMaster->curID == pPuppet->curID);
					bPuppetActive = (pPuppet->eState == PUPPETSTATE_ACTIVE);
				}
				gclPetList_AddPet(peaPets, &iSize, pEntity, pPetEnt, NULL, pClass, 0, bPuppetActive, bActive, bCurrent);
			}
		}
	}

	// Create list elements for critter pets
	if (ePetFlags & kUIPetListFlag_Critters)
	{
		for(i=0;i<eaSize(&pEntity->pSaved->ppAllowedCritterPets);i++)
		{
			PetDef *pDef = GET_REF(pEntity->pSaved->ppAllowedCritterPets[i]->hPet);
			CharacterClass* pClass = pDef ? GET_REF(pDef->hClass) : NULL;
			U32 uCritterID = pEntity->pSaved->ppAllowedCritterPets[i]->uiPetID;

			if (!gclPetList_CheckClass(pClass, piIncludeClassTypes, NULL))
			{
				continue;
			}
			if (!bExcludeAlreadySummoned || !PetExpr_IsCritterPetSummoned(pEntity, pDef))
			{
				gclPetList_AddPet(peaPets, &iSize, pEntity, NULL, pDef, pClass, uCritterID, false, false, false);
			}
		}
	}

	// If requested, show extra slots for pets and/or puppets
	if (GET_REF(pEntity->hAllegiance))
	{
		if (ePetFlags & kUIPetListFlag_ExtraPets)
		{
			int iNumOfficers = gclPetExpr_GetCurrentPetEntityCount();
			int iMaxOfficers = gclPetExpr_GetMaximumPetEntityCount();
			for (i = iMaxOfficers - iNumOfficers; i > 0; --i)
			{
				gclPetList_CreateExtraSlot(peaPets, &iSize, pEntity, true, false);
			}
		}
		if (ePetFlags & kUIPetListFlag_ExtraPuppets) 
		{
			int iNumPuppets = gclPetExpr_GetCurrentPuppetCount();
			int iMaxPuppets = gclPetExpr_GetMaximumPuppetCount();
			for (i = iMaxPuppets - iNumPuppets; i > 0; --i)
			{
				gclPetList_CreateExtraSlot(peaPets, &iSize, pEntity, false, true);
			}
		}
	}

	eaSetSizeStruct(peaPets, parse_PetRowDataID, iSize);

	if (pSortData != NULL)
	{
		eaQSort_s(*peaPets, PetUI_Names_sortfunc_s, pSortData);
	}
	else
	{
		eaQSort(*peaPets, PetUI_Names_sortfunc);
	}

	ui_GenSetManagedListSafe(pGen, peaPets, PetRowDataID, true);
	return eaSize(peaPets);
}

static S32 gclGenGetPetListEx(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity* pEntity,
							  const char* pchClassTypeList,
							  UIPetListFlags ePetFlags, 
							  const char *pchSortFirst, 
							  const char* pchSortLast, 
							  bool bExcludeAlreadySummoned)
{
	char *pContext = NULL;
	char pchBuffer[1024];
	const char *pchClassType;
	SavedPetSortData *pSortData = NULL;
	CharClassTypes *piClassTypes = NULL;
	S32 iSize;

	// Generate class-specific sort data based on the input strings pchSortFirst and pchSortLast
	if (pchSortFirst && pchSortFirst[0])
	{
		pSortData = StructCreate(parse_SavedPetSortData);

		strcpy(pchBuffer, pchSortFirst);
		pchClassType = strtok_s(pchBuffer, ",", &pContext);
		while (pchClassType)
		{
			S32 iType = StaticDefineIntGetInt(CharClassTypesEnum, pchClassType);

			if (iType >= 0)
				ea32Push(&pSortData->piSortFirst, iType);

			pchClassType = strtok_s(NULL, ",", &pContext);
		}
	}
	if (pchSortLast && pchSortLast[0])
	{
		if (pSortData==NULL)
		{
			pSortData = StructCreate(parse_SavedPetSortData);
		}

		strcpy(pchBuffer, pchSortLast);
		pchClassType = strtok_s(pchBuffer, ",", &pContext);
		while (pchClassType)
		{
			S32 iType = StaticDefineIntGetInt(CharClassTypesEnum, pchClassType);

			if (iType >= 0)
				ea32Push(&pSortData->piSortLast, iType);

			pchClassType = strtok_s(NULL, ",", &pContext);
		}
	}

	//Generate a list of classes types used to filter the pet list
	if (pchClassTypeList && pchClassTypeList[0])
	{
		strcpy(pchBuffer, pchClassTypeList);
		pchClassType = strtok_s(pchBuffer, ",", &pContext);
		while (pchClassType)
		{
			CharClassTypes eType = StaticDefineIntGetInt(CharClassTypesEnum,pchClassType);
			if(eType != -1)
				eaiPushUnique(&piClassTypes,eType);
			pchClassType = strtok_s(NULL, ",", &pContext);
		}
	}

	iSize = gclGenGetPetList(pGen,pEntity,piClassTypes,ePetFlags,pSortData,bExcludeAlreadySummoned); 

	eaiDestroy(&piClassTypes);
	StructDestroy(parse_SavedPetSortData, pSortData);
	return iSize;
}

//bUseLastName and iMaxLen no longer do anything
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPetList");
S32 PetExpr_GetPetList(SA_PARAM_NN_VALID UIGen *pGen,
					   const char* pchClassTypeList,
					   S32 ePetFlags, 
					   bool bUseLastName, int iMaxLen,
					   const char *pchSortFirst, const char* pchSortLast)
{
	Entity* pEnt = entActivePlayerPtr();
	return gclGenGetPetListEx(pGen,pEnt,pchClassTypeList,ePetFlags,pchSortFirst,pchSortLast,false);
}

//bUseLastName and iMaxLen no longer do anything
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetUnsummonedPetList");
S32 PetExpr_GetUnsummonedPetList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity* pEntity,
								 const char* pchClassTypeList,  
								 ACMD_EXPR_ENUM(UIPetListFlags) const char* pchIncludePetType, 
								 ACMD_EXPR_ENUM(UIPetListFlags) const char* pchIncludePuppetType, 
								 bool bUseLastName, int iMaxLen,
								 const char *pchSortFirst, const char* pchSortLast)
{
	UIPetListFlags ePetFlag = StaticDefineIntGetInt(UIPetListFlagsEnum,pchIncludePetType);
	UIPetListFlags ePuppetFlag = StaticDefineIntGetInt(UIPetListFlagsEnum,pchIncludePuppetType);
	return gclGenGetPetListEx(pGen,pEntity,pchClassTypeList,ePetFlag|ePuppetFlag,pchSortFirst,pchSortLast,true);
}

static U32 PetExpr_GetNumSummonedCritterPets(SA_PARAM_OP_VALID Entity* pOwner)
{
	if (pOwner && pOwner->pSaved && 
		eaSize(&pOwner->pSaved->ppCritterPets))
	{
		U32 iCount = 0;
		FOR_EACH_IN_EARRAY_FORWARDS(pOwner->pSaved->ppCritterPets, CritterPetRelationship, pRelationShip)
		{
			if (pRelationShip && pRelationShip->erPet > 0)
			{
				iCount++;
			}
		}
		FOR_EACH_END

		return iCount;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetNumSummonedPets");
S32 PetExpr_GetNumSummonedPets(SA_PARAM_OP_VALID Entity* pEntity)
{
	if (!pEntity) return 0;
	return Entity_CountPetsWithState(pEntity, OWNEDSTATE_ACTIVE, false) + PetExpr_GetNumSummonedCritterPets(pEntity);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetExpr_IsPetOnGround");
int PetExpr_IsPetOnGround(SA_PARAM_OP_VALID Entity *pEnt, ContainerID petContainerID)
{
	if (pEnt && pEnt->pSaved) {
		int j;
		S32 iOwnedPetSize = eaSize(&pEnt->pSaved->ppOwnedContainers);
		for ( j = 0; j < iOwnedPetSize; j++ )
		{
			PetRelationship *pRelation = pEnt->pSaved->ppOwnedContainers[j];
			Entity *pPet = pRelation ? GET_REF(pRelation->hPetRef) : NULL;
			if (pPet && pRelation->conID == petContainerID && pPet->myEntityType == GLOBALTYPE_ENTITYSAVEDPET)
			{
				if (entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYSAVEDPET, petContainerID)) return true;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CanEntityRenamePet");
bool PetExpr_CanEntityRenamePet(SA_PARAM_OP_VALID Entity* pPlayerEnt, SA_PARAM_OP_VALID Entity* pPetEnt)
{
	return pPlayerEnt && pPetEnt ? Entity_CanRenamePet(pPlayerEnt,pPetEnt) : false;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CanEntityChangeSubNameOnPet");
bool PetExpr_CanEntityChangeSubNameOnPet(SA_PARAM_OP_VALID Entity* pPlayerEnt, SA_PARAM_OP_VALID Entity* pPetEnt)
{
	return pPlayerEnt && pPetEnt ? Entity_CanChangeSubNameOnPet(pPlayerEnt,pPetEnt) : false;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CanEntityChangeSubName");
bool EntExpr_CanEntityChangeSubName(SA_PARAM_OP_VALID Entity* pPlayerEnt)
{
	return pPlayerEnt ? Entity_CanChangeSubName(pPlayerEnt) : false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCostNumericToRenamePet");
const char* PetExpr_GetCostNumericToRenamePet(void)
{
	return g_PetRestrictions.pchRenameCostNumeric;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCostNumericToChangeSubNameOnPet");
const char* PetExpr_GetCostNumericToChangeSubNameOnPet(void)
{
	return g_PetRestrictions.pchChangeSubNameCostNumeric;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCostToRenamePet");
S32 PetExpr_GetCostToRenamePet(SA_PARAM_OP_VALID Entity* pPetEnt)
{
	return pPetEnt ? Pet_GetCostToRename(entActivePlayerPtr(),pPetEnt,entity_GetGameAccount(entActivePlayerPtr())) : 0;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCostToChangeSubNameOnPet");
S32 PetExpr_GetCostToChangeSubNameOnPet(SA_PARAM_OP_VALID Entity* pPetEnt)
{
	return pPetEnt ? Pet_GetCostToChangeSubName(entActivePlayerPtr(),pPetEnt,entity_GetGameAccount(entActivePlayerPtr())) : 0;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCostToChangeSubName");
S32 EntExpr_GetCostToChangeSubName(SA_PARAM_OP_VALID Entity* pEnt)
{
	return pEnt ? Ent_GetCostToChangeSubName(pEnt,entity_GetGameAccount(pEnt)) : 0;
}

static int SortTrayElemsByPowerDefPurpose(const UITrayElem **ppLeft, const UITrayElem **ppRight, const void *pContext)
{
	return (*ppLeft)->ePurpose - (*ppRight)->ePurpose;
}

static void PetEntGetPowersTrayElems(UIGen *pGen,
									PlayerPetInfo *pPetInfo,
									Entity *petEnt,
									const char* pchCategory,
									const char* pchSecondaryCategoy,
									const char* pchFallbackIcon,
									bool bSortByPurpose)
{
	UITrayElem ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayElem);
	PetPowerState **ppPetPowerState = pPetInfo ? pPetInfo->ppPowerStates : NULL;
	Entity *playerEnt = entActivePlayerPtr();

	if(playerEnt && petEnt)
	{
		int i,j,s = eaSize(&ppPetPowerState);

		Entity *pSubPetEnt = entity_GetSubEntity(PARTITION_CLIENT, playerEnt, GLOBALTYPE_ENTITYSAVEDPET, petEnt->myContainerID);

		// if we will require any more categories, allow for a formatted string
		// since we only need two different categories right now, just allowing two different inputs
		#define INVALID_CATEGORY -1
		int iSecondaryCat = INVALID_CATEGORY;
		int iCategory = INVALID_CATEGORY;

		if (pchSecondaryCategoy && !pchCategory)
			pchCategory = pchSecondaryCategoy;
		if (pchCategory && pchCategory[0])
			iCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory);
		if (pchSecondaryCategoy && pchSecondaryCategoy[0])
			iSecondaryCat = StaticDefineIntGetInt(PowerCategoriesEnum,pchSecondaryCategoy);

		for(i=0,j=0; i<s; i++)
		{
			PetPowerState *pPetPowerState = ppPetPowerState[i];
			PowerDef *pdef = pPetPowerState ? GET_REF(pPetPowerState->hdef) : NULL;
			
			if(pdef && POWERTYPE_ACTIVATABLE(pdef->eType) && !pdef->bHideInUI)
			{
				UITrayElem *pelem;
				PowerDef *pdefExec;
				
				// check if we match one of the categories
				{
					bool bMatchedCategory = false;
					if (iCategory != INVALID_CATEGORY)
					{
						bMatchedCategory = eaiFind(&pdef->piCategories,iCategory) >= 0;
					}

					if (!bMatchedCategory && iSecondaryCat != INVALID_CATEGORY)
					{
						bMatchedCategory = eaiFind(&pdef->piCategories,iSecondaryCat) >= 0;
					}
					
					if (!bMatchedCategory)
						continue;
				}

				// Make sure we have a place for this in the array
				pelem = eaGetStruct(peaElems,parse_UITrayElem,j);
				j++;
				
				if (petEnt->pSaved)
				{
					pelem->pchDragType = "SavedPetPower";
					pelem->iKey = entGetContainerID(petEnt);
				}
				else
				{
					pelem->pchDragType = "PetPower";
					pelem->iKey = 0;
				}
				pelem->bReferencesTrayElem = false;
				pelem->iSlot = i;
				pelem->bValid = true;

				if (pelem->bOwnedTrayElem)
				{
					StructDestroySafe(parse_TrayElem, &pelem->pTrayElem);
				}
				else
				{
					pelem->pTrayElem = NULL;
				}
				if (!pelem->pPetData)
				{
					pelem->pPetData = StructCreate(parse_PetTrayElemData);
				}

				// Figure out what power will get executed
				pdefExec = PetEntGuessExecutedPower(petEnt, pdef);

				pelem->pPetData->erOwner = entGetRef(petEnt);
				pelem->pPetData->pchPower = pdef->pchName;

				if (pelem->pTrayElem)
				{
					pelem->pTrayElem->erOwner = entGetRef(petEnt);
				}
				EntSetPetTrayElemPowerDataEx(pelem,playerEnt,pPetInfo,pPetPowerState,pdefExec,pchFallbackIcon);
			}
		}

		// Make sure the array isn't too big
		while (eaSize(peaElems) > j)
			StructDestroy(parse_UITrayElem, eaPop(peaElems));
		if (bSortByPurpose)
			eaStableSort(*peaElems, NULL, SortTrayElemsByPowerDefPurpose);
	}
	ui_GenSetManagedListSafe(pGen, peaElems, UITrayElem, true);
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetTrayElemsFromPowers");
void exprPetTrayElemsFromPowers(	SA_PARAM_NN_VALID UIGen *pGen,
								SA_PARAM_OP_VALID Entity *pPetEnt,
								const char* pchCategory, const char *pchSecondaryCategoy,
								const char* pchFallbackIcon)
{
	Entity *playerEnt = entActivePlayerPtr();
	PlayerPetInfo *pPetInfo;

	if (playerEnt 
		&& pPetEnt 
		&& (pPetInfo = PetCommands_GetPlayerPetInfoForEnt(pPetEnt)))
	{
		PetEntGetPowersTrayElems(pGen,pPetInfo,pPetEnt,pchCategory,pchSecondaryCategoy,pchFallbackIcon,false);
	}
	else
	{
		UITrayElem ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayElem);
		eaDestroyStruct(peaElems, parse_UITrayElem);
		ui_GenSetManagedListSafe(pGen, NULL, UITrayElem, true);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetTrayElemsFromPowersSortByPurpose");
void exprPetTrayElemsFromPowersSortByPurpose(	SA_PARAM_NN_VALID UIGen *pGen,
								SA_PARAM_OP_VALID Entity *pPetEnt,
								const char* pchCategory, const char *pchSecondaryCategoy,
								const char* pchFallbackIcon)
{
	Entity *playerEnt = entActivePlayerPtr();
	PlayerPetInfo *pPetInfo;

	if (playerEnt 
		&& pPetEnt 
		&& (pPetInfo = PetCommands_GetPlayerPetInfoForEnt(pPetEnt)))
	{
		PetEntGetPowersTrayElems(pGen,pPetInfo,pPetEnt,pchCategory,pchSecondaryCategoy,pchFallbackIcon,true);
	}
	else
	{
		UITrayElem ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayElem);
		eaDestroyStruct(peaElems, parse_UITrayElem);
		ui_GenSetManagedListSafe(pGen, NULL, UITrayElem, true);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetSummonTrayElemsFromPowers");
void exprSummonPetTrayElemsFromPowers(	SA_PARAM_NN_VALID UIGen *pGen,
								SA_PARAM_OP_VALID Entity *pPetEnt,
								const char* pchCategory, const char *pchSecondaryCategoy,
								const char* pchFallbackIcon)
{
	Entity *playerEnt = entActivePlayerPtr();
	PlayerPetInfo *pPetInfo;

	if (playerEnt 
		&& pPetEnt 
		&& (pPetInfo = PetCommands_GetPlayerPetInfoForEnt(pPetEnt)))
	{
		PetEntGetPowersTrayElems(pGen,pPetInfo,pPetEnt,pchCategory,pchSecondaryCategoy,pchFallbackIcon,false);
	}
	else
	{
		UITrayElem ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayElem);
		eaDestroyStruct(peaElems, parse_UITrayElem);
		ui_GenSetManagedListSafe(pGen, NULL, UITrayElem, true);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetTrayToggleAutocast");
void exprPetTrayToggleAutocast(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *pOwner = UITrayGetOwner(pelem);
	if (pelem && pelem->bValid && pOwner)
	{
		PetPowerState *pState = UITrayElemGetPetPowerState(pelem);
		if(pState)
		{
			ServerCmd_aiSetPowerAutoCastForPet(entGetRef(pOwner), REF_STRING_FROM_HANDLE(pState->hdef), !pState->bAIUsageDisabled);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntPetTrayIsOnAutocast");
bool exprEntPetTrayIsOnAutocast(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	if (pelem && pelem->bValid)
	{
		PetPowerState *pState = UITrayElemGetPetPowerState(pelem);
		if(pState)
			return !pState->bAIUsageDisabled;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntPetTrayIsQueuedForCast");
bool exprEntPetTrayIsQueuedForCast(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	if (pelem && pelem->bValid)
	{
		PetPowerState *pState = UITrayElemGetPetPowerState(pelem);
		if(pState)
			return pState->bQueuedForUse;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntPetTrayClearQueuedCast");
void exprEntPetTrayClearQueuedCast(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	if (pelem && pelem->bValid)
	{
		Entity *pPetEnt = UITrayGetOwner(pelem);
		PetPowerState *pState = UITrayElemGetPetPowerState(pelem);
		if(pPetEnt && pState)
		{
			if (pelem->pTrayElem)
			{
				Power *ppow = EntTrayGetActivatedPower(pPetEnt,pelem->pTrayElem,false,NULL);
				if(ppow)
				{
					ServerCmd_PetCommands_StopPowerUse(entGetRef(pPetEnt), REF_STRING_FROM_HANDLE(ppow->hDef));
				}
			}
			else if (pelem->pPetData)
			{
				ServerCmd_PetCommands_StopPowerUse(entGetRef(pPetEnt), pelem->pPetData->pchPower);
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_GetNumPets");
int PetCommands_GetNumPets( SA_PARAM_OP_VALID Entity *pEnt)
{
	if (!pEnt) return 0;
	return eaSize(&pEnt->pSaved->ppOwnedContainers);
}

AUTO_EXPR_FUNC(UIGen) ACMD_ACCESSLEVEL(0) ACMD_NAME("PetCommands_SummonPetByID");
void PetCommands_SummonPetByID(int ID)
{
	ServerCmd_SummonPetByID(ID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_ACCESSLEVEL(0) ACMD_NAME("PetCommands_SummonPetByDef");
void PetCommands_SummonPetByDef(SA_PARAM_OP_STR const char* pchPetDefName)
{
	ServerCmd_SummonCritterPetByDef(pchPetDefName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_ACCESSLEVEL(0) ACMD_NAME("PetCommands_DismissPetByID");
void PetCommands_DismissPetByID(int ID)
{
	ServerCmd_DismissPetByID(ID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FormalName_SetFormalName");
const char *FormalName_SetFormalName( SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_STR const char* pchFirstName, SA_PARAM_OP_STR const char* pchMiddleName, SA_PARAM_OP_STR const char* pchLastName, int bLFM, SA_PARAM_OP_STR const char* pchFirstNameGen, SA_PARAM_OP_STR const char* pchMiddleNameGen, SA_PARAM_OP_STR const char* pchLastNameGen )
{
	char name1[64], name2[64], name3[64];
	Entity *pPlayerEnt = entActivePlayerPtr();
	const char *pcName = pEnt && pEnt->pSaved ? pEnt->pSaved->savedName : NULL;
	char text[256];
	const char *fng = pchFirstNameGen, *lng = pchLastNameGen;
	NotifyType fni = kNotifyType_FirstNameInvalid, lni = kNotifyType_LastNameInvalid;

	if (!pEnt) return false;
	if (!pPlayerEnt) return false;
	if (!pPlayerEnt->pSaved) return false;

	*text = '\0';
	if (((!pchFirstName) || !*pchFirstName) &&
		((!pchMiddleName) || !*pchMiddleName) &&
		((!pchLastName) || !*pchLastName))
	{
		strcat(text, "NONE");
	}
	else
	{
		if (pchFirstName && strlen(pchFirstName) > 20)
		{
			char* pcError = NULL;
			estrCreate( &pcError );
			StringCreateNameError( &pcError, STRINGERR_MAX_LENGTH );
			notify_NotifySend(NULL, kNotifyType_FirstNameInvalid, pcError, NULL, NULL);
			estrDestroy(&pcError);
			return pchFirstNameGen;
		}
		if (pchMiddleName && strlen(pchMiddleName) > 20)
		{
			char* pcError = NULL;
			estrCreate( &pcError );
			StringCreateNameError( &pcError, STRINGERR_MAX_LENGTH );
			notify_NotifySend(NULL, kNotifyType_MiddleNameInvalid, pcError, NULL, NULL);
			estrDestroy(&pcError);
			return pchMiddleNameGen;
		}
		if (pchLastName && strlen(pchLastName) > 20)
		{
			char* pcError = NULL;
			estrCreate( &pcError );
			StringCreateNameError( &pcError, STRINGERR_MAX_LENGTH );
			notify_NotifySend(NULL, kNotifyType_LastNameInvalid, pcError, NULL, NULL);
			estrDestroy(&pcError);
			return pchLastNameGen;
		}

		if (((!pchLastName) || !*pchLastName))
		{
			if (pchMiddleName && *pchMiddleName)
			{
				pchLastName = pchMiddleName;
				pchMiddleName = NULL;
				lng = pchMiddleNameGen;
				lni = kNotifyType_MiddleNameInvalid;
			}
			else if (pchFirstName && *pchFirstName)
			{
				pchLastName = pchFirstName;
				pchFirstName = NULL;
				lng = pchFirstNameGen;
				lni = kNotifyType_FirstNameInvalid;
			}
		}
		else if (((!pchFirstName) || !*pchFirstName) && pchMiddleName && *pchMiddleName)
		{
			pchFirstName = pchMiddleName;
			pchMiddleName = NULL;
			fng = pchMiddleNameGen;
			fni = kNotifyType_MiddleNameInvalid;
		}

		if (pchFirstName && *pchFirstName && CharacterCreation_NotifyNameError(fni, pchFirstName, -1, -1, false))
		{
			return fng;
		}
		if (CharacterCreation_NotifyNameError(lni, pchLastName, -1, -1, false))
		{
			return lng;
		}

		if (bLFM)
		{
			if (pchLastName && *pchLastName)
			{
				*name1 = '\0';
				strcat(name1, pchLastName);
				pchLastName = name1;
				removeTrailingWhiteSpaces(name1);
				strcat(text, pchLastName);
			}
			if (pchFirstName && *pchFirstName)
			{
				if (*text) strcat(text, " ");
				*name2 = '\0';
				strcat(name2, pchFirstName);
				pchFirstName = name2;
				removeTrailingWhiteSpaces(name2);
				strcat(text, pchFirstName);
			}
			if (pchMiddleName && *pchMiddleName)
			{
				if (*text) strcat(text, " ");
				*name3 = '\0';
				strcat(name3, pchMiddleName);
				pchMiddleName = name3;
				removeTrailingWhiteSpaces(name3);
				strcat(text, pchMiddleName);
			}
			if (!CharacterCreation_IsNameValidWithErrorMessage( text, kNotifyType_FormalNameInvalid ))
			{
				return "All";
			}

			*text = '\0';
			strcat(text, "LFM:");
			if (pchLastName && *pchLastName) strcat(text, pchLastName);
			strcat(text, ":");
			if (pchFirstName && *pchFirstName) strcat(text, pchFirstName);
			strcat(text, ":");
			if (pchMiddleName && *pchMiddleName) strcat(text, pchMiddleName);
		}
		else
		{
			if (pchFirstName && *pchFirstName)
			{
				*name1 = '\0';
				strcat(name1, pchFirstName);
				pchFirstName = name1;
				removeTrailingWhiteSpaces(name1);
				strcat(text, pchFirstName);
			}
			if (pchMiddleName && *pchMiddleName)
			{
				if (*text) strcat(text, " ");
				*name2 = '\0';
				strcat(name2, pchMiddleName);
				pchMiddleName = name2;
				removeTrailingWhiteSpaces(name2);
				strcat(text, pchMiddleName);
			}
			if (pchLastName && *pchLastName)
			{
				if (*text) strcat(text, " ");
				*name3 = '\0';
				strcat(name3, pchLastName);
				pchLastName = name3;
				removeTrailingWhiteSpaces(name3);
				strcat(text, pchLastName);
			}
			if (!CharacterCreation_IsNameValidWithErrorMessage( text, kNotifyType_FormalNameInvalid ))
			{
				return "All";
			}

			*text = '\0';
			strcat(text, "FML:");
			if (pchFirstName && *pchFirstName) strcat(text, pchFirstName);
			strcat(text, ":");
			if (pchMiddleName && *pchMiddleName) strcat(text, pchMiddleName);
			strcat(text, ":");
			if (pchLastName && *pchLastName) strcat(text, pchLastName);
		}
	}
	if (!stricmp(pcName, pPlayerEnt->pSaved->savedName))
	{
		ServerCmd_RenameFormal(text);
	}
	else if (pcName)
	{
		ServerCmd_RenamePetFormal(pcName, text);
	}
	return "";
}


static void gclPetRallyPoint_KillFX(PlayerPetRallyPoint *pPetRallyPoint)
{
	if (pPetRallyPoint->hRallyPointFx)
	{
		dtFxKill(pPetRallyPoint->hRallyPointFx);
		pPetRallyPoint->hRallyPointFx = 0;
	}
}


static PlayerPetRallyPoint* gclPetRallyPoint_GetRallyForPet(Entity *pEnt, EntityRef erPet, bool bCreate)
{
	if (pEnt->pPlayer)
	{
		FOR_EACH_IN_EARRAY(pEnt->pPlayer->eaPetRallyPoints, PlayerPetRallyPoint, pRallyPt)
		{
			if (pRallyPt->erPet == erPet)
				return pRallyPt;
		}
		FOR_EACH_END

		if (bCreate)
		{
			PlayerPetRallyPoint *pRallyPt = malloc(sizeof(PlayerPetRallyPoint));
			if (pRallyPt)
			{
				pRallyPt->hRallyPointFx = 0;
				zeroVec3(pRallyPt->vPosition);
				pRallyPt->erPet = erPet;
				eaPush(&pEnt->pPlayer->eaPetRallyPoints, pRallyPt);
				return pRallyPt;
			}
		}
	}

	return NULL;
}

static void gclPetRallyPoint_Destroy(PlayerPetRallyPoint *pPetRallyPoint)
{
	if (pPetRallyPoint)
	{
		gclPetRallyPoint_KillFX(pPetRallyPoint);
		free(pPetRallyPoint);
	}
}

static void gclPetRallyPoint_SetPosition(PlayerPetRallyPoint *pPetRallyPoint, const Vec3 vPosition, S32 index, bool bPlaySound)
{
	Quat qRot;
	const char *pszFXBaseName = gclCursorPetRally_GetRallyPointFXBaseName();
	char *pszFXName = NULL;
	S32 bufferLen;
	
	unitQuat(qRot);

	gclPetRallyPoint_KillFX(pPetRallyPoint);

	copyVec3(vPosition, pPetRallyPoint->vPosition);

	if (!pszFXBaseName) 
		return;
	if (index > 5)
		index = 5;	// sanity capping, it may need to be higher for some game. 
					// feel free to change this
	
	bufferLen = (S32)strlen(pszFXBaseName) + 3;
	estrStackCreateSize(&pszFXName, bufferLen);
	estrPrintf(&pszFXName, "%s%d", pszFXBaseName, index);

	if (!s_bDisableRallyPointFX)
	{
		pPetRallyPoint->hRallyPointFx = dtAddFxFromLocation(pszFXName, NULL, 0, vPosition, vPosition, 
															qRot, 1.f, 0, eDynFxSource_UI);
	}

	if (bPlaySound)
	{
		sndPlayAtCharacter("UI/SetRallyPoint", "", -1, NULL, NULL);
	}

	estrDestroy(&pszFXName);
}

void gclPetRallyPoint_ToggleFX(void)
{
	Entity *pEnt = entActivePlayerPtr();

	if (!pEnt)
		return;

	FOR_EACH_IN_EARRAY(pEnt->pPlayer->eaPetRallyPoints, PlayerPetRallyPoint, pRallyPt)
	{
		Entity *pPetEnt = entFromEntityRefAnyPartition(pRallyPt->erPet);
		PlayerPetInfo *pPetInfo = pPetEnt ? PetCommands_GetPlayerPetInfoForEnt(pPetEnt) : NULL;
		S32 petIdx = pPetInfo ? eaFind(&pEnt->pPlayer->petInfo, pPetInfo) + 1 : -1;
		if (petIdx > 0)
			gclPetRallyPoint_SetPosition(pRallyPt, pRallyPt->vPosition, petIdx, false);
	}
	FOR_EACH_END
}

static void PetCommands_SetRallyPointInternal(Entity *pOwner, Entity *petEnt, PlayerPetInfo *pPetInfo, const Vec3 vPosition)
{
	S32 petIdx;
	PlayerPetRallyPoint *pRallyPt;
	
	petIdx = eaFind(&pOwner->pPlayer->petInfo, pPetInfo) + 1;
	
	pRallyPt = gclPetRallyPoint_GetRallyForPet(pOwner, entGetRef(petEnt), true);

	if (pRallyPt)
	{
		gclPetRallyPoint_SetPosition(pRallyPt, vPosition, petIdx, true);
	}
	
	ServerCmd_PetCommands_SetRallyPosition(pPetInfo->iPetRef, vPosition);
	PetCommands_SetStateByEnt(petEnt, "Hold");
}


#define GROUND_STANDING_THRESHOLD	RAD(60.f)
static int rallypoint_ValidatePosition(Vec3 vInOutPosition)
{
	WorldCollCollideResults results = {0};
	Vec3 vRayEnd, vRayStart;

	copyVec3(vInOutPosition, vRayEnd);
	copyVec3(vInOutPosition, vRayStart);
	vRayStart[1] += 5.f;
	vRayEnd[1] -= 5.f;
	
	if (! worldCollideRay(PARTITION_CLIENT, vRayStart, vRayEnd, WC_QUERY_BITS_WORLD_ALL, &results))
	{	// no ground?
		return false;
	}

	if (getAngleBetweenVec3(upvec, results.normalWorld) > GROUND_STANDING_THRESHOLD)
	{// too steep
		return false;
	}

	copyVec3(results.posWorldImpact,vInOutPosition);
	return true;
}

static S32 gclPetRallyPoint_GetValidPositions(const Vec3 vBasePosition, Vec3 *apRallyPositions, S32 iNeededPositions, 
											F32 fStep, F32 fBaseAngle, F32 fStartAngle, F32 fNormalAngle)
{
	WorldCollCollideResults results = {0};
	S32 iValidPositions = 0;
	F32 fAngle;
	const F32 fRallySpacing = 6.f;

	for(fAngle = fStartAngle; fAngle < TWOPI && iValidPositions < iNeededPositions; fAngle += fStep)
	{
		Vec3 vDir, vRallyPos; 
		// get a direction along the normal ground. 
		// note the fNormalAngle of PI/2 is straight forward. 
		sphericalCoordsToVec3(vDir, fBaseAngle + fAngle, fNormalAngle, 1.f);

		scaleAddVec3(vDir, fRallySpacing, vBasePosition, vRallyPos);
		if (worldCollideRay(PARTITION_CLIENT, vBasePosition, vRallyPos, WC_QUERY_BITS_WORLD_ALL, &results))
		{	// we hit something

			if (getAngleBetweenNormalizedUpVec3(results.normalWorld) < GROUND_STANDING_THRESHOLD)
			{
				Vec3 vRayStart;
				F32 fLastDist = results.distance;
				F32 fDistRemainder = fRallySpacing - results.distance;

				// note: getVec3Pitch will return PI/2 for straight up, and for 
				// sphericalCoordsToVec3, pitch of 0 is straight up, 
				sphericalCoordsToVec3(vDir, fAngle, getVec3Pitch(results.normalWorld), 1.f);
				scaleAddVec3(results.normalWorld, 0.1f, results.posWorldImpact, vRayStart);
				scaleAddVec3(vDir, fDistRemainder, vRayStart, vRallyPos);	

				if (worldCollideRay(PARTITION_CLIENT, vRayStart, vRallyPos, WC_QUERY_BITS_WORLD_ALL, &results))
				{
					if (fLastDist + results.distance < fRallySpacing*.75f)
						continue;

					scaleAddVec3(vDir, -1.f, results.posWorldImpact, vRallyPos);	
				}
			}
			else 
			{
				if (results.distance < fRallySpacing*.5f)
					continue; // a wall or something is too close

				scaleAddVec3(vDir, -1.f, results.posWorldImpact, vRallyPos);	
			}
		}

		if (!rallypoint_ValidatePosition(vRallyPos))
			continue;

		copyVec3(vRallyPos, apRallyPositions[iValidPositions]);
		iValidPositions++;
	}

	return iValidPositions;
}


void PetCommands_SetRallyPoint(Entity *pOwner, EntityRef erPet, const Vec3 vPosition, const Vec3 vNormal)
{
	if(erPet != 0)
	{	// this rally point is for every pet the owner has, create multiple rally points around this position.
		PlayerPetInfo *pPetInfo;
		Entity *pPetEnt = entFromEntityRefAnyPartition(erPet);
		
		if (!pPetEnt)
			return;

		pPetInfo = PetCommands_GetPlayerPetInfoForEnt(pPetEnt);
		if (!pPetInfo)
			return;

		PetCommands_SetRallyPointInternal(pOwner, pPetEnt, pPetInfo, vPosition);
	}
	else
	{
		S32 numPets = eaSize(&pOwner->pPlayer->petInfo);
		Vec3 *avRallyPositions = (Vec3*)alloca(sizeof(Vec3) * numPets);
		S32 iValidPositions = 0;
		F32 fStep = TWOPI / (F32)numPets;
		F32 fNormalAngle = getVec3Pitch(vNormal);
		Vec3 vBasePosition;
		Vec3 vFormationDir;
		Vec3 vOwnerPos;
		F32 fFormationYaw;

		entGetPos(pOwner, vOwnerPos);
		subVec3(vPosition, vOwnerPos, vFormationDir);
		fFormationYaw = getVec3Yaw(vFormationDir);
		
		devassert(avRallyPositions);
		copyVec3(vPosition, vBasePosition);
		vBasePosition[1] += 1.f;
		iValidPositions = gclPetRallyPoint_GetValidPositions(vBasePosition, avRallyPositions, numPets, 
														fStep, fFormationYaw, fStep * 0.5f, fNormalAngle);
		if (iValidPositions < numPets)
		{
			iValidPositions += gclPetRallyPoint_GetValidPositions(vBasePosition, avRallyPositions + iValidPositions, 
															numPets - iValidPositions, fStep, fFormationYaw, 0.f, fNormalAngle);

			// if we have any valid positions that need filling still, just use the base postion
			while(iValidPositions < numPets)
			{
				copyVec3(vPosition, avRallyPositions[iValidPositions]);
				iValidPositions++;
			}
		}

		iValidPositions = 0;
		FOR_EACH_IN_EARRAY(pOwner->pPlayer->petInfo, PlayerPetInfo, pPetInfo)
		{
			Entity *pPetEnt = entFromEntityRefAnyPartition(pPetInfo->iPetRef);
		
			if (pPetEnt)
			{
				PetCommands_SetRallyPointInternal(pOwner, pPetEnt, pPetInfo, avRallyPositions[iValidPositions]);
				iValidPositions++;
			}
		}
		FOR_EACH_END

	}

	
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_RemoveRallyPoint");
void PetCommands_RemoveRallyPoint(SA_PARAM_OP_VALID Entity *pPet)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	PlayerPetInfo *pPetInfo;
	PlayerPetRallyPoint* pRally;
	if(!pPet || !pPlayerEnt)
		return;
	
	pRally = gclPetRallyPoint_GetRallyForPet(pPlayerEnt, entGetRef(pPet), false);
	if (pRally)
	{
		gclPetRallyPoint_KillFX(pRally);
	}

	pPetInfo = PetCommands_GetPlayerPetInfoForEnt(pPet);
	if (!pPetInfo)
		return;
		
	ServerCmd_PetCommands_ClearRallyPosition(pPetInfo->iPetRef);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_RemoveAllRallyPoints");
void PetCommands_RemoveAllRallyPoints()
{
	Entity *pOwner = entActivePlayerPtr();

	if (pOwner && pOwner->pPlayer)
	{
		FOR_EACH_IN_EARRAY(pOwner->pPlayer->eaPetRallyPoints, PlayerPetRallyPoint, pRally)
			gclPetRallyPoint_KillFX(pRally);
		FOR_EACH_END
		
		ServerCmd_PetCommands_ClearAllRallyPositions();
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(AI) ACMD_CLIENTCMD ACMD_PRIVATE;
void PetCommands_ServerRemovedRallyPoints(Entity *pOwner)
{
	if (pOwner && pOwner->pPlayer)
	{
		FOR_EACH_IN_EARRAY(pOwner->pPlayer->eaPetRallyPoints, PlayerPetRallyPoint, pRally)
			gclPetRallyPoint_KillFX(pRally);
		FOR_EACH_END
	}
}

// Return true if this pet is controlled by the active player.
AUTO_EXPR_FUNC(entityutil);
bool PetCommands_IsControlledPet(SA_PARAM_OP_VALID Entity *pPet)
{
	return !!PetCommands_GetPlayerPetInfoForEnt(pPet);
}

// Return true if this pet is controlled by the active player.
AUTO_EXPR_FUNC(entityutil);
bool PetCommands_IsOwnedByMe(SA_PARAM_OP_VALID Entity *pPet)
{
	Entity* pPlayer = entActivePlayerPtr();
	if (!pPet || !pPlayer) return false;
	return (pPet->erOwner == pPlayer->myRef);
}

// Return true if this pet is controlled by the active player.
AUTO_EXPR_FUNC(entityutil);
bool PetCommands_IsOwnedByMeOrMyTeamMates(SA_PARAM_OP_VALID Entity *pPet)
{
	return team_IsPetOwnedByPlayerOrTeam(entActivePlayerPtr(), pPet);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PetCommands_GetNewPetID");
U32 PetCommands_GetNewPetID(void)
{
	Entity* pEnt = entActivePlayerPtr();

	if (pEnt && pEnt->pSaved)
	{
		return pEnt->pSaved->uNewPetID;
	}
	return 0;
}

static PetCreatedInfo *s_pLastNewPet;
static PetCreatedInfo *s_pLastNewPuppet;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_SetCreatedPetVars");
void PetCommands_SetCreatedPetVars(SA_PARAM_NN_VALID UIGen *pGen)
{
	UIGenVarTypeGlob* pGlob;
	if (pGlob = eaIndexedGetUsingString(&pGen->eaVars, "PetID"))
		pGlob->iInt = SAFE_MEMBER(s_pLastNewPet, iPetID);
	if (pGlob = eaIndexedGetUsingString(&pGen->eaVars, "PetType"))
		pGlob->iInt = SAFE_MEMBER(s_pLastNewPet, iPetType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PetCommands_SetCreatedPuppetVars");
void PetCommands_SetCreatedPuppetVars(SA_PARAM_NN_VALID UIGen *pGen)
{
	UIGenVarTypeGlob* pGlob;
	if (pGlob = eaIndexedGetUsingString(&pGen->eaVars, "PuppetID"))
		pGlob->iInt = SAFE_MEMBER(s_pLastNewPuppet, iPetID);
	if (pGlob = eaIndexedGetUsingString(&pGen->eaVars, "PuppetType"))
		pGlob->iInt = SAFE_MEMBER(s_pLastNewPuppet, iPetType);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void PetCommands_SendClientPetCreated(PetCreatedInfo *pPetCreatedInfo)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pPetCreatedInfo)
	{
		if (pPetCreatedInfo->iPetIsPuppet)
		{
			UIGen* pGen = ui_GenFind("PuppetIntroduction_Root", kUIGenTypeNone);
			StructDestroySafe(parse_PetCreatedInfo, &s_pLastNewPuppet);
			s_pLastNewPuppet = StructClone(parse_PetCreatedInfo, pPetCreatedInfo);
			if (pGen)
			{
				PetCommands_SetCreatedPuppetVars(pGen);
				globCmdParse("GenSendMessage PuppetIntroduction_Root Show");
			}
		}
		else
		{
			UIGen* pGen = ui_GenFind("PetIntroduction_Root", kUIGenTypeNone);
			StructDestroySafe(parse_PetCreatedInfo, &s_pLastNewPet);
			s_pLastNewPet = StructClone(parse_PetCreatedInfo, pPetCreatedInfo);
			if (pGen)
			{
				PetCommands_SetCreatedPetVars(pGen);
				globCmdParse("GenSendMessage PetIntroduction_Root Show");
			}
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(PetIntroWarp);
void exprPetIntroductionWarp(void)
{
	Entity* pEnt = entActivePlayerPtr();

	if (pEnt && pEnt->pSaved)
	{
		PetIntroductionWarp* pWarp = Entity_GetPetIntroductionWarp(pEnt, pEnt->pSaved->uNewPetID);
		if (pWarp)
		{
			ServerCmd_PetIntroWarp(pEnt->pSaved->uNewPetID);
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(PetIntroRequiresWarp);
bool exprPetIntroRequiresWarp(void)
{
	Entity* pEnt = entActivePlayerPtr();

	if (pEnt && pEnt->pSaved)
	{
		PetIntroductionWarp* pWarp = Entity_GetPetIntroductionWarp(pEnt, pEnt->pSaved->uNewPetID);
		if (pWarp)
		{
			const char* pchMapName = zmapInfoGetPublicName(NULL);
			if (pchMapName != pWarp->pchMapName)
			{
				return true;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(PetIntroGetWarpMapDisplayName);
const char* exprPetIntroductionGetWarpMapDisplayName(void)
{
	Entity* pEnt = entActivePlayerPtr();

	if (pEnt && pEnt->pSaved)
	{
		PetIntroductionWarp* pWarp = Entity_GetPetIntroductionWarp(pEnt, pEnt->pSaved->uNewPetID);
		if (pWarp)
		{
			ZoneMapInfo* pMapInfo = zmapInfoGetByPublicName(pWarp->pchMapName);
			if (pMapInfo)
			{
				DisplayMessage* pDisplayMessage = zmapInfoGetDisplayNameMessage(pMapInfo);
				if (pDisplayMessage)
				{
					return TranslateDisplayMessage(*pDisplayMessage);
				}
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(PetIntroCanPlayerUseWarp);
bool exprPetIntroductionCanPlayerUseWarp(void)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pSaved)
	{
		PetIntroductionWarp* pWarp = Entity_GetPetIntroductionWarp(pEnt, pEnt->pSaved->uNewPetID);
		if (pWarp && Entity_CanUsePetIntroWarp(pEnt, pWarp))
		{
			return true;
		}
	}
	return false;
}

static int PetDataSort(const PetData **ppLeft, const PetData **ppRight)
{
	S32 iValueLeft, iValueRight;
	S32 iLenLeft, iLenRight;
	int iNameCmp;

	// Sort by type first
	iValueLeft = (*ppLeft)->bPet * 2 + (*ppLeft)->bSummon;
	iValueRight = (*ppRight)->bPet * 2 + (*ppRight)->bSummon;
	if (iValueLeft != iValueRight)
		return iValueLeft - iValueRight;

	// Sort by name
	iLenLeft = (*ppLeft)->pchName ? (S32)strlen((*ppLeft)->pchName) : 0;
	iLenRight = (*ppRight)->pchName ? (S32)strlen((*ppRight)->pchName) : 0;
	iNameCmp = strnicmp((*ppLeft)->pchName, (*ppRight)->pchName, MIN(iLenLeft, iLenRight));
	if (iNameCmp)
	{
		// Sort alphabetically
		return iNameCmp;
	}
	if (iLenLeft != iLenRight)
	{
		// Sort by length
		return iLenLeft - iLenRight;
	}

	// Sort by ref
	return (*ppLeft)->pEntity->myRef - (*ppRight)->pEntity->myRef;
}

// Get the number of controllable pets & summons
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetPetAndSummonListSize);
S32 exprPetGetPetAndSummonListSize(SA_PARAM_OP_VALID Entity *pPlayer)
{
	S32 i;
	S32 iCount = 0;

	if (pPlayer && pPlayer->pPlayer)
	{
		for (i = 0; i < eaSize(&pPlayer->pPlayer->petInfo); i++)
		{
			PlayerPetInfo *pPetInfo = pPlayer->pPlayer->petInfo[i];
			Entity *pEntity = entFromEntityRefAnyPartition(pPetInfo->iPetRef);
			if (!pEntity)
				continue;

			iCount++;
		}
	}

	return iCount;
}

// Get the list of controllable pets & summons
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetPetAndSummonList);
void exprPetGetPetAndSummonList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pPlayer)
{
	PetData ***peaPetData = ui_GenGetManagedListSafe(pGen, PetData);
	S32 i, j;
	S32 iCount = 0;

	if (pPlayer && pPlayer->pPlayer)
	{
		for (i = 0; i < eaSize(&pPlayer->pPlayer->petInfo); i++)
		{
			PlayerPetInfo *pPetInfo = pPlayer->pPlayer->petInfo[i];
			Entity *pEntity = entFromEntityRefAnyPartition(pPetInfo->iPetRef);
			PetStanceInfo *pStanceInfo;
			PetData *pData;
			if (!pEntity)
				continue;

			pData = eaGetStruct(peaPetData, parse_PetData, iCount++);
			pData->pEntity = pEntity;

			// Determine if it's a pet or a summon
			j = -1;
			if (pPlayer->pSaved)
			{
				for (j = eaSize(&pPlayer->pSaved->ppCritterPets) - 1; j >= 0; j--)
				{
					if (pPlayer->pSaved->ppCritterPets[j]->erPet == pPetInfo->iPetRef)
					{
						break;
					}
				}
			}
			pData->bPet = j >= 0;
			pData->bSummon = j < 0;

			// Copy stance/state
			pStanceInfo = eaGet(&pPetInfo->eaStances, PetStanceType_PRECOMBAT);
			pData->pchAiState = pPetInfo->curPetState;
			pData->pchAiStance = pStanceInfo ? pStanceInfo->curStance : NULL;

			// Copy name
			pData->pchName = entGetLocalName(pEntity);

			// Set level
			pData->iLevel = entity_GetSavedExpLevel(pEntity);
			pData->iCombatLevel = entity_GetCombatLevel(pEntity);

			// Set state flags
			pData->bDead = entCheckFlag(pEntity, ENTITYFLAG_DEAD);
		}
	}

	eaSetSizeStruct(peaPetData, parse_PetData, iCount);
	//eaQSort(*peaPetData, PetDataSort);
	ui_GenSetManagedListSafe(pGen, peaPetData, PetData, true);
}

static void SetPetTrayElemFromAiCommand(UITrayElem *pElem, PetCommandNameInfo *pPetState, PlayerPetInfo *pPetInfo, const char *pchIconPrefix)
{
	char achIconName[256];

	if (!pElem || !pElem->pPetData|| !pPetState)
		return;

	if (pPetState->pchIcon && *pPetState->pchIcon)
		strcpy(achIconName, pPetState->pchIcon);
	else
		sprintf(achIconName, "%s%s", pchIconPrefix, pPetState->pchName);

	pElem->pPetData->pchAiState = pPetState->pchName;
	estrCopy2(&pElem->estrShortDesc, TranslateMessageRef(pPetState->pchDisplayName));
	pElem->pchIcon = allocAddString(achIconName);

	pElem->bActive = true;
	pElem->bCurrent = false; 
	pElem->bQueued = false; 
	pElem->bCharging = false;
	pElem->bMaintaining = false; 
	pElem->bRecharging = false;
	pElem->bInCooldown = false;
	pElem->bModeEnabled = false;
	pElem->bActivatable = true;
	pElem->bAutoActivating = false;

	if (pPetInfo)
	{
		pElem->bActive = pPetInfo->curPetState == pPetState->pchName;
	}
}

static bool FindTraySlotLocation(PlayerPetPersistedOrder *pCommand, PlayerPetPersistedOrder ***peaSavedOrder, PlayerPetPersistedOrder ***peaLocalOrder, S32 iFirstSlot, S32 iLastSlot)
{
	static S32 s_iSlotsUsedLastFrame;
	static S32 s_iSlotsUsedThisFrame;
	static U32 s_iLastFrame;

	PlayerPetPersistedOrder *pOrder;
	int i, iFirstEmpty;
	bool bSaved = false;

	if (s_iLastFrame != gGCLState.totalElapsedTimeMs)
	{
		s_iSlotsUsedLastFrame = s_iSlotsUsedThisFrame;
		s_iSlotsUsedThisFrame = 0;
		s_iLastFrame = gGCLState.totalElapsedTimeMs;
	}

	if (!pCommand || !peaLocalOrder || !pCommand->pchCommand || !*pCommand->pchCommand)
		return false;
	if (!(pCommand->bAiState ^ pCommand->bPower))
		return false;

	for (i = 0; i < eaSize(peaSavedOrder); i++)
	{
		pOrder = (*peaSavedOrder)[i];
		if (pCommand->pchCommand == pOrder->pchCommand &&
			pCommand->bAiState == pOrder->bAiState &&
			pCommand->bPower == pOrder->bPower)
		{
			pCommand->iSlot = pOrder->iSlot;
			s_iSlotsUsedThisFrame |= (1 << pOrder->iSlot);
			bSaved = true;
			break;
		}
	}

	// check to see if it's in a temp location
	for (i = 0; i < eaSize(peaLocalOrder); i++)
	{
		pOrder = (*peaLocalOrder)[i];
		if (pCommand->pchCommand == pOrder->pchCommand &&
			pCommand->bAiState == pOrder->bAiState &&
			pCommand->bPower == pOrder->bPower)
		{
			if (bSaved)
			{
				StructDestroy(parse_PlayerPetPersistedOrder, eaRemove(peaLocalOrder, i));
				return true;
			}

			if (pCommand->iSlot >= MAX_PLAYER_PET_PERSISTED_ORDER)
			{
				// It's a soft persisted value
				pCommand->iSlot = pOrder->iSlot;
				s_iSlotsUsedThisFrame |= (1 << pOrder->iSlot);
				return true;
			}

			// Not in a persisted location yet
			pCommand->iSlot = pOrder->iSlot;
			s_iSlotsUsedThisFrame |= (1 << pOrder->iSlot);
			return false;
		}
	}

	if (bSaved)
		return true;

	// new command, find empty slot
	for (iFirstEmpty = iFirstSlot; iFirstEmpty < 100; iFirstEmpty++)
	{
		pOrder = eaIndexedGetUsingInt(peaSavedOrder, iFirstEmpty);
		if (!pOrder)
			pOrder = eaIndexedGetUsingInt(peaLocalOrder, iFirstEmpty);

		// If nothing was found, then it's not in use
		if (!pOrder)
		{
			break;
		}
	}

	if (iFirstEmpty > iLastSlot)
	{
		// try picking an unused visible slot
		for (iFirstEmpty = iFirstSlot; iFirstEmpty < 32; iFirstEmpty++)
		{
			if ((s_iSlotsUsedLastFrame & (1 << iFirstEmpty)) == 0)
			{
				break;
			}
		}
	}

	// picked a visible slot
	if (iFirstEmpty <= iLastSlot)
	{
		pCommand->iSlot = iFirstEmpty;
		pOrder = StructClone(parse_PlayerPetPersistedOrder, pCommand);
		pOrder->uTime = 0;
		s_iSlotsUsedThisFrame |= (1 << pOrder->iSlot);
		if (pOrder)
			eaPush(peaLocalOrder, pOrder);
		return false;
	}

	// set the slot to some very large number
	pCommand->iSlot = 1000;
	return false;
}

// Get the list of pet powers and pet states
void PetGetPetStateAndPowerTray(SA_PARAM_NN_VALID UITrayElem ***peaElems, SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Entity *pPet, const char *pchCategory, const char *pchSecondaryCategory, int iFirstSlot, int iLastSlot, bool bTrimRight)
{
	static PlayerPetPersistedOrder **s_eaLocalOrder;
	PlayerPetPersistedOrder command = {0};
	int i,j,s;
	PlayerPetInfo *pPetInfo;
	PetPowerState *pPowerState;
	PetCommandNameInfo *pPetState;
	Entity *pPetEnt;
	PowerDef *pdef;
	UITrayElem *pElem;
	S32 iMaxSlot = 0;
	U32 uNow = timeSecondsSince2000();

	// if we will require any more categories, allow for a formatted string
	// since we only need two different categories right now, just allowing two different inputs
	int iSecondaryCat = INVALID_CATEGORY;
	int iCategory = INVALID_CATEGORY;

	if (!s_eaLocalOrder)
	{
		eaCreate(&s_eaLocalOrder);
		eaIndexedEnable(&s_eaLocalOrder, parse_PlayerPetPersistedOrder);
	}

	if (pchSecondaryCategory && !pchCategory)
		pchCategory = pchSecondaryCategory;
	if (pchCategory && pchCategory[0])
		iCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory);
	if (pchSecondaryCategory && pchSecondaryCategory[0])
		iSecondaryCat = StaticDefineIntGetInt(PowerCategoriesEnum,pchSecondaryCategory);

	// Invalidate tray
	for (i=0; i<eaSize(peaElems); i++)
		(*peaElems)[i]->bValid = false;

	if(pPlayer && pPlayer->pPlayer)
	{
		PlayerPetPersistedOrder **eaOrder = SAFE_MEMBER2(pPlayer->pPlayer->pUI, pLooseUI, eaPetCommandOrder);
		s = eaSize(&pPlayer->pPlayer->petInfo);

		for (i=0; i<s; i++)
		{
			pPetInfo = pPlayer->pPlayer->petInfo[i];
			if (!pPetInfo)
				continue;

			pPetEnt = entFromEntityRefAnyPartition(pPetInfo->iPetRef);
			if (!pPetEnt)
				continue;

			if (pPet && pPet != pPetEnt)
				continue;

			for (j=0; j<eaSize(&pPetInfo->validStates); j++)
			{
				pPetState = pPetInfo->validStates[j];
				if (pPetState->bHidden)
					continue;

				command.pchCommand = pPetState->pchName;
				command.bPower = false;
				command.bAiState = true;
				if (FindTraySlotLocation(&command, &eaOrder, &s_eaLocalOrder, iFirstSlot, iLastSlot))
				{
					if (iFirstSlot <= command.iSlot && command.iSlot <= iLastSlot)
					{
						pElem = eaGetStruct(peaElems, parse_UITrayElem, command.iSlot - iFirstSlot);
						if (!pElem->bValid)
						{
							pElem->bReferencesTrayElem = false;
							pElem->pchDragType = "PetAndSummonTrayElem";
							pElem->iSlot = command.iSlot;
							pElem->bValid = true;
							pElem->pTrayElem = NULL;
							if (!pElem->pPetData)
								pElem->pPetData = StructCreate(parse_PetTrayElemData);
							SetPetTrayElemFromAiCommand(pElem, pPetState, pPetInfo, "Pet_Stance_");
						}
					}
				}
			}

			for (j=0; j<eaSize(&pPetInfo->ppPowerStates); j++)
			{
				pPowerState = pPetInfo->ppPowerStates[j];
				pdef = GET_REF(pPowerState->hdef);
				if (pdef)
				{
					if (iCategory != INVALID_CATEGORY && eaiFind(&pdef->piCategories, iCategory) < 0)
						continue;
					if (iSecondaryCat != INVALID_CATEGORY && eaiFind(&pdef->piCategories, iSecondaryCat) < 0)
						continue;

					command.pchCommand = pdef->pchName;
					command.bPower = true;
					command.bAiState = false;
					if (FindTraySlotLocation(&command, &eaOrder, &s_eaLocalOrder, iFirstSlot, iLastSlot))
					{
						if (iFirstSlot <= command.iSlot && command.iSlot <= iLastSlot)
						{
							pElem = eaGetStruct(peaElems, parse_UITrayElem, command.iSlot - iFirstSlot);
							if (!pElem->bValid)
							{
								pElem->bReferencesTrayElem = false;
								pElem->pchDragType = "PetAndSummonTrayElem";
								pElem->iSlot = command.iSlot;
								pElem->bValid = true;
								pElem->pTrayElem = NULL;
								if (!pElem->pPetData)
									pElem->pPetData = StructCreate(parse_PetTrayElemData);
								pElem->pPetData->pchPower = pdef->pchName;
								EntSetPetTrayElemPowerDataEx(pElem, pPlayer, pPetInfo, pPowerState, pdef, "");
							}
						}
					}
				}
			}
		}
	}

	if (!bTrimRight)
	{
		eaSetSizeStruct(peaElems, parse_UITrayElem, iLastSlot - iFirstSlot + 1);
	}
	else
	{
		for (i=0; i<eaSize(peaElems); i++)
		{
			pElem = eaGet(peaElems, i);
			if (pElem->bValid)
			{
				MAX1(iMaxSlot, pElem->iSlot);
			}
		}
		eaSetSizeStruct(peaElems, parse_UITrayElem, iMaxSlot - iFirstSlot + 1);
	}

	// Cleanup invalid elements
	for (i=0; i<eaSize(peaElems); i++)
	{
		pElem = (*peaElems)[i];
		if (!pElem->bValid)
		{
			pElem->pTrayElem = NULL;
			if (pElem->pPetData)
				StructDestroySafe(parse_PetTrayElemData, &pElem->pPetData);
			ZeroStruct((*peaElems)[i]);
		}
	}

	// Send save requests for unsaved slots
	for (i=0; i<eaSize(&s_eaLocalOrder); i++)
	{
		if (s_eaLocalOrder[i]->uTime < uNow && s_eaLocalOrder[i]->iSlot < MAX_PLAYER_PET_PERSISTED_ORDER)
		{
			command = *(s_eaLocalOrder[i]);
			command.uTime = 0;
			ServerCmd_gslSetPetTraySlot(&command);
			s_eaLocalOrder[i]->uTime = uNow + 10;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetAllPetStateAndPowerTray);
void PetGenGetAllPetStateAndPowerTray(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pPlayer, const char *pchCategory, const char *pchSecondaryCategory, int iFirstSlot, int iLastSlot)
{
	UITrayElem ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayElem);
	PetGetPetStateAndPowerTray(peaElems, pPlayer, NULL, pchCategory, pchSecondaryCategory, iFirstSlot, iLastSlot, true);
	ui_GenSetManagedListSafe(pGen, peaElems, UITrayElem, true);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRODUCTS(StarTrek);
void PetCommands_GlobalPetTrayExec(int active, int slot)
{
	static UITrayElem s_elem;
	static PetTrayElemData s_petTrayElemData;
	Entity *pPlayer = entActivePlayerPtr();
	PlayerLooseUI *pLooseUI = SAFE_MEMBER3(pPlayer, pPlayer, pUI, pLooseUI);

	if (pLooseUI)
	{
		PlayerPetPersistedOrder *pCommand = eaGet(&pLooseUI->eaPetCommandOrder, slot);
		if (pCommand)
		{
			s_elem.pPetData = &s_petTrayElemData;
			s_elem.pTrayElem = NULL;
			s_elem.bValid = true;
			s_petTrayElemData.erOwner = 0;
			s_petTrayElemData.pchAiState = pCommand->bAiState ? pCommand->pchCommand : NULL;
			s_petTrayElemData.pchPower = pCommand->bPower ? pCommand->pchCommand : NULL;
			UITrayExec(!!active, &s_elem);
		}
	}
}

void gclPet_UpdateLocalPlayerPetInfo()
{
	Entity* pPlayerEnt = entActivePlayerPtr();

	if (pPlayerEnt && pPlayerEnt->pPlayer)
	{
		// go through all the rally points and make sure they are still valid
		S32 i = eaSize(&pPlayerEnt->pPlayer->eaPetRallyPoints) -1;
		for(; i >= 0; --i)
		{
			PlayerPetRallyPoint *pRally = pPlayerEnt->pPlayer->eaPetRallyPoints[i];
			Entity *e = entFromEntityRefAnyPartition(pRally->erPet);

			if (!e || e->erOwner != entGetRef(pPlayerEnt))
			{
				gclPetRallyPoint_Destroy(pRally);
				eaRemoveFast(&pPlayerEnt->pPlayer->eaPetRallyPoints, i);
			}
		}
	}
}

void gclPet_DestroyRallyPoints(Entity* pPlayerEnt)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer)
		eaDestroyEx(&pPlayerEnt->pPlayer->eaPetRallyPoints, gclPetRallyPoint_Destroy);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetPetIsDeceasedByDef);
bool gclPetExpr_GetPetIsDeceasedByDef(SA_PARAM_OP_VALID Entity *pPlayer, const char *pchPetDefName)
{
	if (pPlayer && pPlayer->pSaved)
	{
		PetDef *pPetDef = RefSystem_ReferentFromString(g_hPetStoreDict,pchPetDefName);
		if (pPetDef)
		{
			PetDefRefCont* pPetRef = Entity_FindAllowedCritterPetByDef(pPlayer, pPetDef);
			return pPetRef ? pPetRef->bPetIsDeceased : false;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(HasRequirementsToSummonDeceasedPet);
bool gclPetExpr_HasRequirementsToSummonDeceasedPet(SA_PARAM_OP_VALID Entity *pPlayer)
{
	if (pPlayer)
	{
		return SavedPet_HasRequirementsToResummonDeceasedPet(pPlayer);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetReqItemNameToSummonDeceasedPet);
const char* gclPetExpr_GetReqItemNameToSummonDeceasedPet(SA_PARAM_OP_VALID Entity *pPlayer)
{
	if (g_PetRestrictions.pchRequiredItemForDeceasedPets)
	{
		return inv_GetNumericItemDisplayName(pPlayer, g_PetRestrictions.pchRequiredItemForDeceasedPets);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntCanSetAsActivePuppet);
bool PetExprEntCanSetAsActivePuppet(SA_PARAM_OP_VALID Entity* pEnt, U32 uiPuppetID)
{
	return Entity_CanSetAsActivePuppetByID(pEnt, uiPuppetID);
}

// See if it's possible to set a pet as the preferred pet in the slot. Use pet id 0 to see if there are any pets that may be slotted.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntCanSetPreferredPet);
bool PetExprCanSetPreferredPet(SA_PARAM_OP_VALID Entity* pEnt, U32 iPetId, int iIndex)
{
	NOCONST(Entity) *pEntity = CONTAINER_NOCONST(Entity, pEnt);
	S32 i, slot;

	if (!pEntity->pSaved || eaSize(&pEntity->pSaved->ppOwnedContainers) <= 0)
		return false;

	if (iPetId != 0)
	{
		if (eaiGet(&pEntity->pSaved->ppPreferredPetIDs, iIndex) == iPetId)
			return true;

		slot = eaiFind(&pEntity->pSaved->ppPreferredPetIDs, iPetId);
		if (slot >= 0 && eaiSize(&pEntity->pSaved->ppPreferredPetIDs) <= iIndex)
			return false;

		return trhEntity_CanSetPreferredPet(pEntity, iPetId, iIndex);
	}

	if (eaiGet(&pEntity->pSaved->ppPreferredPetIDs, iIndex) != 0)
		return true;

	for (i = 0; i < eaSize(&pEntity->pSaved->ppOwnedContainers); i++)
	{
		NOCONST(PetRelationship) *pPet = pEntity->pSaved->ppOwnedContainers[i];

		if (trhSavedPet_IsPetAPuppet(pEntity, pPet))
			continue;

		slot = eaiFind(&pEntity->pSaved->ppPreferredPetIDs, pPet->conID);
		if (slot >= 0)
			continue;

		if (trhEntity_CanSetPreferredPet(pEntity, pPet->conID, iIndex))
			return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SetPreferredCategorySet);
void gclPetExpr_SetPreferredCategorySet(const char* pchCategorySetName)
{
	ServerCmd_gslSetPreferredCategorySet(pchCategorySetName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetPreferredCategorySet);
const char* gclPetExpr_GetPreferredCategorySetForClassType(SA_PARAM_OP_VALID Entity *pEnt, const char* pchType)
{
	CharClassCategorySet *pSet = CharClassCategorySet_getPreferredSet(pEnt);
	return SAFE_MEMBER(pSet, pchName);
}

#include "Autogen/gclPetUI_c_ast.c"
