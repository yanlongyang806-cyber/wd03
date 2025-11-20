/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "StatPoints.h"
#include "ResourceManager.h"
#include "AutoTransDefs.h"
#include "Character.h"
#include "CharacterClass.h"
#include "EntityLib.h"
#include "PowerVars.h"

#include "AutoGen/StatPoints_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Character_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// The dictionary handle for the stat point pools
DictionaryHandle g_hStatPointPoolDict;

static void StatPointPool_ValidateSecondPass(SA_PARAM_NN_VALID StatPointPoolDef *pDef)
{
	if (pDef)
	{
		StatPointPoolDef *pOtherDef;
		RefDictIterator iter;

		RefSystem_InitRefDictIterator(g_hStatPointPoolDict, &iter);

		// Make sure none of the attribs defined in this pool is used in another pool
		while(pOtherDef = (StatPointPoolDef*)RefSystem_GetNextReferentFromIterator(&iter))
		{
			if (pOtherDef != pDef)
			{
				FOR_EACH_IN_EARRAY_FORWARDS(pDef->ppValidAttribs, StatPointDef, pStatPointDef)
				{
					if (pStatPointDef && 
						pStatPointDef->pchAttribName && 
						pStatPointDef->pchAttribName[0] && 
						StatPointPool_ContainsAttrib(pOtherDef, StaticDefineIntGetInt(AttribTypeEnum, pStatPointDef->pchAttribName)))
					{
						Errorf("Invalid stat point pool definition: Stat point pool '%s' uses the attribute '%s' which is also used in stat point pool '%s'.", pDef->pchName, pStatPointDef->pchAttribName, pOtherDef->pchName);
					}
				}
				FOR_EACH_END
			}
		}
	}
	
}

static void StatPointPool_Validate(SA_PARAM_NN_VALID StatPointPoolDef *pDef)
{
	if (pDef)
	{
		// Make sure the power table is valid
		if (pDef->pchPowerTableName == NULL || pDef->pchPowerTableName[0] == '/0' || powertable_Find(pDef->pchPowerTableName) == NULL)
		{
			Errorf("Invalid stat point pool definition: Stat point pool '%s' does not have a valid power table.", pDef->pchName);
		}

		// Make sure there is at least one attribute defined
		if (eaSize(&pDef->ppValidAttribs) <= 0)
		{
			Errorf("Invalid stat point pool definition: Stat point pool '%s' does not have any valid attributes.", pDef->pchName);
		}
		else
		{
			S32 i, j;
			
			for (i = 0; i < eaSize(&pDef->ppValidAttribs); i++)
			{
				// Make sure this attribute already exists
				if (pDef->ppValidAttribs[i]->pchAttribName == NULL || 
					pDef->ppValidAttribs[i]->pchAttribName[0] == '/0' || 
					StaticDefineIntGetInt(AttribTypeEnum, pDef->ppValidAttribs[i]->pchAttribName) < 0)
				{
					Errorf("Invalid stat point pool definition: Stat point pool '%s' contains invalid ValidAttrib definition. The attribute '%s' does not exist.", pDef->pchName, pDef->ppValidAttribs[i]->pchAttribName);
				}
				else
				{
					// Make sure this attribute is not already used in this pool
					for (j = i + 1; j < eaSize(&pDef->ppValidAttribs); j++)
					{
						if (stricmp(pDef->ppValidAttribs[i]->pchAttribName, pDef->ppValidAttribs[j]->pchAttribName) == 0)
						{
							Errorf("Invalid stat point pool definition: Stat point pool '%s' contains duplicate attribute '%s'.", pDef->pchName, pDef->ppValidAttribs[i]->pchAttribName);
							break;
						}
					}
				}
			}
			
		}
	}
}

// Validates the stat point pools
static int StatPointPool_ValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, StatPointPoolDef *pStatPointPoolDef, U32 userID)
{
	switch (eType)
	{
		case RESVALIDATE_POST_TEXT_READING: // Called after load/reload but before binning
		{
			StatPointPool_Validate(pStatPointPoolDef);
			return VALIDATE_HANDLED;
		}
		case RESVALIDATE_POST_BINNING:
		{
			StatPointPool_ValidateSecondPass(pStatPointPoolDef);
			return VALIDATE_HANDLED;
		}
	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
int StatPointPool_Startup(void)
{
	// Set up reference dictionaries
	g_hStatPointPoolDict = RefSystem_RegisterSelfDefiningDictionary("StatPointPool", false, parse_StatPointPoolDef, true, true, NULL);

	resDictManageValidation(g_hStatPointPoolDict, StatPointPool_ValidateCB);

	return 1;
}

AUTO_STARTUP(StatPointPools) ASTRT_DEPS(AS_CharacterAttribs, PowerVars);
void StatPointPool_Load(void)
{
	// Load all stat point pools into the dictionary
	resLoadResourcesFromDisk(g_hStatPointPoolDict, "defs/statpointpools", ".statpointpool", "StatPointPools.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
}

AUTO_TRANS_HELPER_SIMPLE;
StatPointPoolDef * StatPointPool_DefFromName(SA_PARAM_NN_STR const char *pchStatPointPoolName)
{
	if (pchStatPointPoolName && pchStatPointPoolName[0])
	{
		return RefSystem_ReferentFromString(g_hStatPointPoolDict, pchStatPointPoolName);
	}
	return NULL;
}

StatPointPoolDef * StatPointPool_DefFromAttrib(AttribType eAttribType)
{
	if (eAttribType >= 0)
	{
		StatPointPoolDef *pDef;
		RefDictIterator iter;

		RefSystem_InitRefDictIterator(g_hStatPointPoolDict, &iter);

		// Make sure none of the attribs defined in this pool is used in another pool
		while(pDef = (StatPointPoolDef*)RefSystem_GetNextReferentFromIterator(&iter))
		{
			if (StatPointPool_ContainsAttrib(pDef, eAttribType))
				return pDef;
		}
	}

	return NULL;
}

bool StatPointPool_ContainsAttrib(SA_PARAM_NN_VALID StatPointPoolDef *pDef, AttribType eAttribType)
{
	if (pDef && eAttribType >= 0)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pDef->ppValidAttribs, StatPointDef, pStatPointDef)
		{
			if (pStatPointDef && 
				pStatPointDef->pchAttribName && 
				StaticDefineIntGetInt(AttribTypeEnum, pStatPointDef->pchAttribName) == eAttribType)
			{
				return true;
			}
		}
		FOR_EACH_END
	}
	return false;
}

/****** Character Stat points functions ******/

int character_StatPointsSpentPerAttrib(Character *pChar, AttribType eType)
{
	int i;

	for(i=0;i<eaSize(&pChar->ppAssignedStats);i++)
	{
		if(pChar->ppAssignedStats[i]->eType == eType)
			return pChar->ppAssignedStats[i]->iPoints + pChar->ppAssignedStats[i]->iPointPenalty;
	}

	return 0;
}

bool character_IsValidStatPoint(AttribType eAttrib, SA_PARAM_NN_STR const char *pchStatPointPoolName)
{
	if (pchStatPointPoolName && pchStatPointPoolName[0] && eAttrib >= 0)
	{
		StatPointPoolDef *pDef = StatPointPool_DefFromName(pchStatPointPoolName);

		if (pDef)
		{
			return StatPointPool_ContainsAttrib(pDef, eAttrib);
		}
	}

	return false;
}

// Returns the number of AssignedStat points actually assigned
AUTO_TRANS_HELPER;
int entity_GetAssignedStatAssigned(ATH_ARG NOCONST(Entity) *pEnt, SA_PARAM_NN_STR const char *pchStatPointPoolName)
{
	StatPointPoolDef *pDef = StatPointPool_DefFromName(pchStatPointPoolName);
	int i, iAssigned = 0;
	if(pDef && NONNULL(pEnt) && NONNULL(pEnt->pChar))
	{
		for(i=eaSize(&pEnt->pChar->ppAssignedStats)-1; i>=0; i--)
		{
			if (StatPointPool_ContainsAttrib(pDef, pEnt->pChar->ppAssignedStats[i]->eType))
			{
				iAssigned += pEnt->pChar->ppAssignedStats[i]->iPoints + pEnt->pChar->ppAssignedStats[i]->iPointPenalty;
			}			
		}
	}
	return iAssigned;
}

// Returns the number of AssignedStat points the entity is allowed to spend.
//  Does not exclude points already assigned.
//  Lookup is hard-coded to ASSIGNEDSTAT_TABLE table.
AUTO_TRANS_HELPER;
int entity_GetAssignedStatAllowed(ATH_ARG NOCONST(Entity) *pEnt, SA_PARAM_NN_STR const char *pchStatPointPoolName)
{
	StatPointPoolDef *pDef = StatPointPool_DefFromName(pchStatPointPoolName);
	int iAllowed = 0;

	if(pDef && powertable_Find(pDef->pchPowerTableName))
	{
		int iLevel = entity_trh_GetSavedExpLevel(pEnt);
		iAllowed = entity_PowerTableLookupAtHelper(pEnt, pDef->pchPowerTableName, iLevel-1);
	}

	return iAllowed;
}

// Returns the number of unspent AssignedStat points (just a wrapper for (allowed - assigned))
AUTO_TRANS_HELPER;
int entity_GetAssignedStatUnspent(ATH_ARG NOCONST(Entity) *pEnt, SA_PARAM_NN_STR const char *pchStatPointPoolName)
{
	int iAllowed = entity_GetAssignedStatAllowed(pEnt, pchStatPointPoolName);
	int iAssigned = entity_GetAssignedStatAssigned(pEnt, pchStatPointPoolName);
	return iAllowed - iAssigned;
}

/******* End Character Stat Points ******/

#include "AutoGen/StatPoints_h_ast.c"