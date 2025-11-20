/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CombatMods.h"

#include "EString.h"

#include "Character.h"
#include "Character_target.h"
#include "Entity.h"
#include "entCritter.h"

#include "CombatMods_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static CombatList s_CombatList;

AUTO_STARTUP(CombatMods) ASTRT_DEPS(AS_CharacterAttribs, AS_AttribSets);
void CombatModsLoad(void)
{
	char *pchSharedMemory = NULL;
	loadstart_printf("Loading CombatMods...");
	MakeSharedMemoryName("CombatMods.bin",&pchSharedMemory);
	ParserLoadFilesShared(pchSharedMemory, NULL, "defs/config/CombatMods.def", "CombatMods.bin", PARSER_OPTIONALFLAG, parse_CombatList, &s_CombatList);
	estrDestroy(&pchSharedMemory);
	loadend_printf(" done.");
}

bool CombatMod_ignore(int iPartitionIdx, Character *pSource, Character *pTarget)
{

	if(!s_CombatList.ppCombatMods || eaSize(&s_CombatList.ppCombatMods) == 0) //No combat mods loaded
		return true;

	if(pSource == pTarget 
		|| (pSource->pEntParent->pCritter && GET_REF(pSource->pEntParent->pCritter->critterDef) && GET_REF(pSource->pEntParent->pCritter->critterDef)->bIgnoreCombatMods) 
		|| (pTarget->pEntParent->pCritter && GET_REF(pTarget->pEntParent->pCritter->critterDef) && GET_REF(pTarget->pEntParent->pCritter->critterDef)->bIgnoreCombatMods))
	{
		return true;
	}
	else
	{
		Entity *pSourceEnt = pSource->pEntParent;
		Entity *pSourceTarget = pTarget->pEntParent;

		if(entGetType(pSourceTarget) == GLOBALTYPE_ENTITYPLAYER && entGetType(pSourceEnt) == GLOBALTYPE_ENTITYPLAYER)
			return true;
	}
	
	if(character_TargetMatchesTypeRequire(iPartitionIdx,pSource,pTarget,kTargetType_Friend | kTargetType_Teammate | kTargetType_PrimaryPet | kTargetType_Owner))
	{
		return true;
	}
	return false;
}


CombatMod *CombatMod_getMod(int iSourceLevel, int iTargetLevel, bool bPlayer)
{
	int iDifference = ABS(iSourceLevel - iTargetLevel);
	bool bHigher = iSourceLevel >= iTargetLevel;
	CombatMods *pCombatMod = NULL;

	if(!s_CombatList.ppCombatMods || eaSize(&s_CombatList.ppCombatMods) == 0) //No combat mods loaded
		return NULL;

	pCombatMod = iDifference >= eaSize(&s_CombatList.ppCombatMods) ? s_CombatList.ppCombatMods[eaSize(&s_CombatList.ppCombatMods) -1] : s_CombatList.ppCombatMods[iDifference];

	if(bHigher)
		if(bPlayer)
			return pCombatMod->pHigherPlayer;
		else
			return pCombatMod->pHigher;
	else
		if(bPlayer)
			return pCombatMod->pLowerPlayer;
		else
			return pCombatMod->pLower;
}

F32 CombatMod_getMagnitude(CombatMod *pMod)
{
	return pMod->fMagnitude;
}

F32 CombatMod_getDuration(CombatMod *pMod)
{
	return pMod->fDuration;
}

bool CombatMod_DoesAffectMagnitude(AttribType attrib)
{
	if (s_CombatList.eAllowedMagnitudeAttrib == -2)
		return true;
	if (s_CombatList.eAllowedMagnitudeAttrib == -1)
		return false;
	
	return attrib_Matches(attrib, s_CombatList.eAllowedMagnitudeAttrib);
}

bool CombatMod_DoesAffectDuration(AttribType attrib)
{
	if (s_CombatList.eAllowedDurationAttrib == -2)
		return true;
	if (s_CombatList.eAllowedDurationAttrib == -1)
		return false;

	return attrib_Matches(attrib, s_CombatList.eAllowedDurationAttrib);
}

#include "CombatMods_h_ast.c"