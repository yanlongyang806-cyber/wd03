/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CharacterClass.h"

#include "entCritter.h"
#include "EntityLib.h"
#include "Expression.h"
#include "fileutil.h"
#include "foldercache.h"
#include "Player.h"
#include "resourcemanager.h"
#include "StringCache.h"

#include "AttribCurve.h"
#include "Character.h"
#include "Character_h_ast.h"
#include "CharacterAttribs.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "oldencounter_common.h"
#include "PowerVars.h"
#include "PowerVars_h_ast.h"
#include "PowerSlots_h_ast.h"
#include "PowerTree.h"
#include "PowerTree_h_ast.h"
#include "PowerTreeHelpers.h"
#include "rewardCommon.h"
#include "sharedmemory.h"
#include "AbilityScores_DD.h"
#include "CombatConfig.h"
#include "GamePermissionsCommon.h"
#include "GameAccountDataCommon.h"
#include "EntitySavedData.h"
#include "RegionRules.h"
#include "CostumeCommonGenerate.h"
#include "cmdparse.h"

#include "AutoGen/CharacterClass_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/GameAccountData_h_ast.h"

#ifdef GAMESERVER
	#include "AutoGen/GameClientLib_autogen_clientcmdwrappers.h"
#endif
#ifdef GAMECLIENT
	#include "UIGen.h"	
#endif

DefineContext *g_ExtraCharClassTypeIDs;
DefineContext *g_pExtraCharClassCategories;
DefineContext *g_pExtraCharacterPathTypes;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

extern ParseTable parse_CharacterAttribs[];
#define TYPE_parse_CharacterAttribs CharacterAttribs
extern ParseTable parse_Entity[];
#define TYPE_parse_Entity Entity
StaticDefineInt ClassAttribAspectEnum[] =
{
	DEFINE_INT
	{ "Basic", kClassAttribAspect_Basic},
	{ "Str", kClassAttribAspect_Str},
	{ "Res", kClassAttribAspect_Res},
	DEFINE_END
};
DictionaryHandle g_hCharacterClassDict;

// The dictionary holding the assigned stats for each class
DictionaryHandle g_hClassAssignedStats;

// The dictionary handle for the character paths
DictionaryHandle g_hCharacterPathDict;

// The dictionary handle for the Character Category Sets
DictionaryHandle g_hCharacterClassCategorySetDict;

CharacterPathTypeStructs g_CharacterPathTypeStructs;

// Validates the class assigned stats
static int ClassAssignedStatsValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, ClassAssignedStats *pClassAssignedStats, U32 userID)
{
	switch (eType)
	{	
		case RESVALIDATE_POST_TEXT_READING: // Called after load/reload but before binning
		{
			// Special validation for D&D
			S32 i;
			AssignedStats **eaAssignedStats = NULL;

			if (gConf.eCCGetBaseAttribValues == CCGETBASEATTRIBVALUES_RETURN_DD_BASE)
			{
				if (pClassAssignedStats == NULL || pClassAssignedStats->eaAssignedStats == NULL)
					return VALIDATE_HANDLED;

				// Add all non 0 assigned stats
				for (i = 0; i < eaSize(&pClassAssignedStats->eaAssignedStats); i++)
				{
					if (pClassAssignedStats->eaAssignedStats[i]->iPoints != 0)
					{
						// Add the assigned stat
						eaPush(&eaAssignedStats, pClassAssignedStats->eaAssignedStats[i]);
					}
				}

				// Validate the attributes
				if (!DDIsAbilityScoreSetValid((NOCONST(AssignedStats) **)eaAssignedStats, false))
				{
					Errorf("Invalid assigned stats for character class: %s based on D&D ruleset.", pClassAssignedStats->pchName);
				}

				// Destroy the temporary array
				eaDestroy(&eaAssignedStats);
			}
		}
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

static ExprContext *CharacterPath_GetContext(void)
{
	static ExprContext *s_pContext = NULL;

	if(!s_pContext)
	{
		ExprFuncTable* stTable;

		s_pContext = exprContextCreate();

		stTable = exprContextCreateFunctionTable();

		exprContextAddFuncsToTableByTag(stTable,"Player");
		exprContextAddFuncsToTableByTag(stTable, "util");

		exprContextSetFuncTable(s_pContext, stTable);

		exprContextSetAllowRuntimeSelfPtr(s_pContext);
		exprContextSetAllowRuntimePartition(s_pContext);
	}

	devassert(s_pContext != NULL);

	return s_pContext;
}


void CharacterPath_Generate(CharacterPath *pPath)
{
	ExprContext *pContext = CharacterPath_GetContext();

	exprContextSetUserPtr(pContext, pPath, parse_CharacterPath);
	exprGenerate(pPath->pExprRequires, pContext);
	exprContextSetUserPtr(pContext, NULL, NULL);
}

static bool CharacterPath_Validate(CharacterPath *pCharacterPath)
{
	S32 i, j, iSuggestedPurchaseCount, iValid = 1;
	CharacterPathSuggestedPurchase *pSuggestedPurchase = NULL;

	// Make sure there are not multiple definitions for a power table
	iSuggestedPurchaseCount = eaSize(&pCharacterPath->eaSuggestedPurchases);

	for (i = 0; i < iSuggestedPurchaseCount; i++)
	{
		pSuggestedPurchase = pCharacterPath->eaSuggestedPurchases[i];

		// See if this is already defined
		for (j = 0; j < iSuggestedPurchaseCount; j++)
		{
			if (i != j && stricmp(pCharacterPath->eaSuggestedPurchases[j]->pchPowerTable, pSuggestedPurchase->pchPowerTable) == 0)
			{
				Errorf("Invalid character path definition: Character path '%s' contains a duplicate definition for power table '%s'.", pCharacterPath->pchName, pSuggestedPurchase->pchPowerTable);
				iValid = 0;
			}
		}

		if (IsServer())
		{
			// If we are in the server we can actually validate the power tree nodes
			S32 itNode, itChoice;
			bool bFoundNonOneRanks = false;

			for(itChoice = 0; itChoice < eaSize(&pSuggestedPurchase->eaChoices); itChoice++)
			{
				CharacterPathChoice *pChoice = pSuggestedPurchase->eaChoices[itChoice];
				for (itNode = 0; itNode < eaSize(&pChoice->eaSuggestedNodes); itNode++)
				{
					if(pChoice->eaSuggestedNodes[itNode]->iMaxRanksToBuy != 1 && eaSize(&pChoice->eaSuggestedNodes) > 1)
						bFoundNonOneRanks = true;
				}
			}

			if(bFoundNonOneRanks)
			{
				Errorf("Invalid character path definition: Character path '%s' contains choices that have ranks specified that aren't '1'.", 
					pCharacterPath->pchName);
				iValid = 0;
			}
		}
	}
	return !!iValid;
}

static bool CharacterPath_ValidateRefs(CharacterPath *pCharacterPath)
{
	S32 i, iSuggestedPurchaseCount, iPreviewItems, iValid = 1;
	CharacterPathSuggestedPurchase *pSuggestedPurchase = NULL;

	iSuggestedPurchaseCount = eaSize(&pCharacterPath->eaSuggestedPurchases);

	for (i = 0; i < iSuggestedPurchaseCount; i++)
	{
		pSuggestedPurchase = pCharacterPath->eaSuggestedPurchases[i];

		if (IsServer())
		{
			// If we are in the server we can actually validate the power tree nodes
			S32 itNode, itRank, itChoice;
			PTNodeDef *pNodeDef = NULL;

			for(itChoice = 0; itChoice < eaSize(&pSuggestedPurchase->eaChoices); itChoice++)
			{
				CharacterPathChoice *pChoice = pSuggestedPurchase->eaChoices[itChoice];
				for (itNode = 0; itNode < eaSize(&pChoice->eaSuggestedNodes); itNode++)
				{
					bool bFoundRankWithMatchingPowerTable = false;

					pNodeDef = GET_REF(pChoice->eaSuggestedNodes[itNode]->hNodeDef);
					if (pNodeDef)
					{
						// Validate the max ranks to purchase
						if( pChoice->eaSuggestedNodes[itNode]->iMaxRanksToBuy != 0 
							&& (pChoice->eaSuggestedNodes[itNode]->iMaxRanksToBuy < 0 
							|| pChoice->eaSuggestedNodes[itNode]->iMaxRanksToBuy > eaSize(&pNodeDef->ppRanks)))
						{
							Errorf("Invalid character path definition: Character path '%s' contains an invalid MaxRanksToBuy parameter for node '%s'.", 
								pCharacterPath->pchName, 
								pNodeDef->pchNameFull);
							iValid = 0;
						}

						// Expect at least one rank that uses this powertable for its cost
						for (itRank = 0; itRank < eaSize(&pNodeDef->ppRanks); itRank++)
						{
							if (stricmp(pNodeDef->ppRanks[itRank]->pchCostTable, pSuggestedPurchase->pchPowerTable) == 0)
							{
								bFoundRankWithMatchingPowerTable = true;
								break;
							}
						}

						if (!bFoundRankWithMatchingPowerTable)
						{
							Errorf("Invalid character path definition: Character path '%s' contains a suggested node '%s' which does not contain any ranks that uses the power table '%s' for its cost.", 
								pCharacterPath->pchName,
								pNodeDef->pchNameFull,
								pSuggestedPurchase->pchPowerTable);
							iValid = 0;
						}
					}
					else
					{
						Errorf("Invalid character path definition: Character path '%s' contains an invalid node definition '%s'",
							pCharacterPath->pchName,
							REF_STRING_FROM_HANDLE(pChoice->eaSuggestedNodes[itNode]->hNodeDef));
						iValid = 0;
					}
				}
			}
		}
	}

	if (!IsClient())
	{
		iPreviewItems = eaSize(&pCharacterPath->eaPreviewItems);

		for (i = 0; i < iPreviewItems; i++)
		{
			if (!GET_REF(pCharacterPath->eaPreviewItems[i]->hDef))
			{
				ErrorFilenamef(pCharacterPath->pchFile, "Character path '%s' has an invalid preview item '%s'",
					pCharacterPath->pchName,
					REF_STRING_FROM_HANDLE(pCharacterPath->eaPreviewItems[i]->hDef));
				iValid = 0;
			}
		}
	}
	return !!iValid;
}


// Validates the character paths
static int CharacterPathsValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, CharacterPath *pCharacterPath, U32 userID)
{
	switch (eType)
	{
	case RESVALIDATE_POST_BINNING:
		{
			CharacterPath_Generate(pCharacterPath);
			return VALIDATE_HANDLED;
		}

	case RESVALIDATE_POST_TEXT_READING: // Called after load/reload but before binning
		{
			CharacterPath_Validate(pCharacterPath);
			return VALIDATE_HANDLED;
		}

	case RESVALIDATE_CHECK_REFERENCES:
		{
			CharacterPath_ValidateRefs(pCharacterPath);
			return VALIDATE_HANDLED;;
		}
	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
// Registers the character paths dictionary
int CharacterPaths_Startup(void)
{
	// Set up reference dictionaries
	g_hCharacterPathDict = RefSystem_RegisterSelfDefiningDictionary("CharacterPath", false, parse_CharacterPath, true, true, NULL);

	resDictManageValidation(g_hCharacterPathDict, CharacterPathsValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hCharacterPathDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hCharacterPathDict, ".pDisplayName.Message", NULL, NULL, NULL, NULL);			
		}
	} 
	else if (IsClient())
	{
		resDictRequestMissingResources(g_hCharacterPathDict, 16, false, resClientRequestSendReferentCommand);
	}

	return 1;
}

AUTO_STARTUP(CharacterPaths) ASTRT_DEPS(PowerTrees, PowerVars, Items);
void CharacterPaths_Load(void)
{
	int i = 0; 

	g_pExtraCharacterPathTypes = DefineCreate();

	if (g_pExtraCharacterPathTypes)
	{
		loadstart_printf("Loading Character Path Types... ");

		ParserLoadFiles(NULL, "defs/config/"CHAR_PATH_TYPES_ID_FILE".def", CHAR_PATH_TYPES_ID_FILE".bin", PARSER_OPTIONALFLAG, parse_CharacterPathTypeStructs, &g_CharacterPathTypeStructs);

		for (i = 0; i < eaSize(&g_CharacterPathTypeStructs.eaTypes); i++)
			DefineAddInt(g_pExtraCharacterPathTypes, g_CharacterPathTypeStructs.eaTypes[i]->pchName, i);

		loadend_printf("done (%d).", i); 
	}

	// Load all character paths into the dictionary
	resLoadResourcesFromDisk(g_hCharacterPathDict, "defs/classes", ".charpath",  "CharacterPaths.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
}


AUTO_RUN;
int ClassAssignedStats_Startup(void)
{
#ifdef GAMECLIENT
	// Set up reference dictionaries
	g_hClassAssignedStats = RefSystem_RegisterSelfDefiningDictionary("ClassAssignedStats", false, parse_ClassAssignedStats, true, true, NULL);

	resDictManageValidation(g_hClassAssignedStats, ClassAssignedStatsValidateCB);
#endif

	return 1;
}

AUTO_STARTUP(ClassAssignedStats);
void ClassAssignedStats_Load(void)
{
#ifdef GAMECLIENT
		resLoadResourcesFromDisk(g_hClassAssignedStats, NULL, "defs/classes/ClassAssignedStats.def",  "ClassAssignedStats.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
#endif
}

// Sets the Character's class.  This is an autotransaction helper, and doesn't do any sanity checks.
AUTO_TRANS_HELPER;
void character_SetClassHelper(ATH_ARG NOCONST(Character) *pchar, const char *cpchClass)
{
	SET_HANDLE_FROM_STRING(g_hCharacterClassDict,cpchClass,pchar->hClass);
}


CharacterClass* ActivatePowerClassFromStrength(const char *pcStrength)
{
	if (!stricmp(pcStrength, "Medium") || !stricmp(pcStrength, "Default")) {
		return characterclasses_FindByName("Object_Default");

	} else if (!stricmp(pcStrength, "Small") || !stricmp(pcStrength, "Harmless")) {
		return characterclasses_FindByName("Object_Harmless");

	} else if (!stricmp(pcStrength, "Large") || !stricmp(pcStrength, "Deadly")) {
		return characterclasses_FindByName("Object_Deadly");
	}
	return NULL;
}


// Returns a pointer to Character's Class' attrib table for the given aspect at the Character's combat level
//  May return null in cases of a bad class, table, etc.
CharacterAttribs *character_GetClassAttribs(Character *pchar, ClassAttribAspect eAspect)
{
	CharacterAttribs *pattr = NULL;
	CharacterClass *pclass = character_GetClassCurrent(pchar);
	if(pclass)
	{
		CharacterAttribs **ppAttribs = *(CharacterAttribs***)((char*)pclass + eAspect);
		int iLevel = pchar->iLevelCombat-1;
		iLevel = CLAMP(iLevel,0,MAX_LEVELS-1);

		if(ppAttribs)
		{
			pattr = ppAttribs[iLevel];
		}

		// Client didn't find a complete table, so make one and save it
		if(!pattr && !entIsServer())
		{
			AttribType attrib;
			pattr = StructAlloc(parse_CharacterAttribs);
			for(attrib=0; attrib<SIZE_OF_ALL_NORMAL_ATTRIBS; attrib+=SIZE_OF_NORMAL_ATTRIB)
			{
				int i = ATTRIB_INDEX(attrib);
				*F32PTR_OF_ATTRIB(pattr,attrib) = character_GetClassAttrib(pchar,eAspect,attrib);
			}
			eaSetSize(&ppAttribs,MAX_LEVELS);
			ppAttribs[iLevel] = pattr;
		}
	}
	return pattr;
}

// Returns the Character's Class' attrib for the given aspect at the Character's combat level
//  Potentially faster/safer/smarter than getting the entire table
F32 character_GetClassAttrib(Character *pchar, ClassAttribAspect eAspect, AttribType eAttrib)
{
	F32 f = 0;
	CharacterClass *pclass = character_GetClassCurrent(pchar);
	if(pclass)
	{
		S32 iLevel = pchar->iLevelCombat-1;
		switch(eAspect)
		{
		case kClassAttribAspect_Basic:
			f = class_GetAttribBasic(pclass,eAttrib,iLevel);
			break;
		case kClassAttribAspect_Str:
			f = class_GetAttribStrength(pclass,eAttrib,iLevel);
			break;
		case kClassAttribAspect_Res:
			f = class_GetAttribResist(pclass,eAttrib,iLevel);
			break;
		}
	}
	else
	{
		if(eAspect!=kClassAttribAspect_Basic)
			f = 1;
	}
	return f;
}


// Returns the named power table, or NULL if the table doesn't exist.  Looks in the Class first.
PowerTable *powertable_FindInClass(const char *pchName, CharacterClass *pClass)
{
	PowerTable *ptable = NULL;
	if(pClass)
	{
		if(pClass->stTables)
		{
			stashFindPointer(pClass->stTables,pchName,&ptable);
		}
		else if(!entIsServer())
		{
			// TODO(JW): Bugs: This doesn't properly take into account inheritance
			//  and furthermore is a total hack
			int i;
			for(i=eaSize(&pClass->ppTables)-1; i>=0; i--)
			{
				if(!stricmp(pClass->ppTables[i]->pchName,pchName))
				{
					ptable = pClass->ppTables[i];
					break;
				}
			}
		}

	}
	if(!ptable)
	{
		ptable = powertable_Find(pchName);
	}
	return ptable;
}


// Returns the best value in the named table, or 0 if the table doesn't exist.
//  If the class doesn't specify the table itself, it will fall back to the default table.
//  If the table is a multi-table, it will recurse up to one level to find the proper table.
F32 class_powertable_LookupMulti(CharacterClass *pClass, const char *pchName, S32 idx, S32 idxMulti)
{
	F32 fRet = 0.f;
	PowerTable *ptable = powertable_FindInClass(pchName,pClass);
	if(!ptable)
	{
		if (pchName) 
		{
			ErrorDetailsf("Table %s", pchName);
			Errorf("class_powertable_LookupMulti: Unknown PowerTable");
		} 
		else if (isDevelopmentMode())
		{
			ErrorfForceCallstack("class_powertable_LookupMulti: No PowerTable Specified");
		}
	}
	else
	{
		// Check if this is a multi-table
		int s = eaSize(&ptable->ppchTables);
		if(s)
		{
			if(verify(s > idxMulti && idxMulti >= 0 && ptable->ppchTables[idxMulti]))
			{
				// Fetch the actual table
				ptable = powertable_FindInClass(ptable->ppchTables[idxMulti],pClass);
			}
		}

		if(verify(ptable) && (s = eafSize(&ptable->pfValues)))
		{
			if(idx >= 0)
			{
				// If the request index is larger than the table, return the last element.  This isn't considered
				// a bug.
				fRet = ptable->pfValues[min(idx,s-1)];
			}
			else
			{
				// If the request index is below 0, return the first element.  This is considered a bug.
				ErrorDetailsf("Table %s, Index %d, Class %s",pchName,idx,pClass->pchName);
				ErrorfForceCallstack("THIS IS SAFE TO IGNORE: Jered just needs the call stack: Class table lookup below 0"); // Temporary assert so I can get a call stack and debug it
				fRet = ptable->pfValues[0];
			}
		}
	}
	return fRet;
}

// Returns the best value in the named table, or 0 if the table doesn't exist.
//  If the class doesn't specify the table itself, it will fall back to the default table.
//  If the table is a multi-table, it uses the 0th subtable.
F32 class_powertable_Lookup(CharacterClass *pClass, const char *pchName, S32 idx)
{
	return class_powertable_LookupMulti(pClass,pchName,idx,0);
}

MultiVal *powervar_FindInClass(const char *pchName, CharacterClass *pClass)
{
	if(pClass->stVars)
	{
		PowerVar *pvar = NULL;
		stashFindPointer(pClass->stVars,pchName,&pvar);
		if(pvar)
		{
			return &pvar->mvValue;
		}
		else if(!entIsServer())
		{
			// TODO(JW): Bugs: This doesn't properly take into account inheritance
			//  and furthermore is a total hack
			int i;
			for(i=eaSize(&pClass->ppVars)-1; i>=0; i--)
			{
				if(!stricmp(pClass->ppVars[i]->pchName,pchName))
				{
					return &pClass->ppVars[i]->mvValue;
				}
			}
		}
	}
	return powervar_Find(pchName);
}

// Returns the entire CharacterClassAttrib structure for a particular Attribute
CharacterClassAttrib *class_GetAttrib(CharacterClass *pClass, AttribType eAttrib)
{
	CharacterClassAttrib *p = NULL;
	if(pClass->ppAttributes)
	{
		int i = ATTRIB_INDEX(eAttrib);
		int s = eaSize(&pClass->ppAttributes);
		if(s && i<s)
		{
			p = pClass->ppAttributes[i];
		}
	}
	return p;
}

// Returns the Class's base Basic value for the Attribute at the specified level
F32 class_GetAttribBasic(CharacterClass *pClass, AttribType eAttrib, S32 iLevel)
{
	F32 f = 0;
	CharacterClassAttrib *pAttrib = class_GetAttrib(pClass,eAttrib);
	if(pAttrib)
	{
		int s;
		if(s=eafSize(&pAttrib->pfBasic))
		{
			iLevel = CLAMP(iLevel,0,s-1);
			f = pAttrib->pfBasic[iLevel];
		}
	}
	return f;
}

// Returns the Class's base Strength value for the Attribute at the specified level
F32 class_GetAttribStrength(CharacterClass *pClass, AttribType eAttrib, S32 iLevel)
{
	F32 f = 1;
	CharacterClassAttrib *pAttrib = class_GetAttrib(pClass,eAttrib);
	if(pAttrib)
	{
		int s;
		if(s=eafSize(&pAttrib->pfStrength))
		{
			iLevel = CLAMP(iLevel,0,s-1);
			f = pAttrib->pfStrength[iLevel];
		}
	}
	return f;
}

// Returns the Class's base Resist value for the Attribute at the specified level
F32 class_GetAttribResist(CharacterClass *pClass, AttribType eAttrib, S32 iLevel)
{
	F32 f = 1;
	CharacterClassAttrib *pAttrib = class_GetAttrib(pClass,eAttrib);
	if(pAttrib)
	{
		int s;
		if(s=eafSize(&pAttrib->pfResist))
		{
			iLevel = CLAMP(iLevel,0,s-1);
			f = pAttrib->pfResist[iLevel];
		}
	}
	return f;
}



// Get the specified AttribCurve, returns NULL if none exists
AttribCurve *class_GetAttribCurve(CharacterClass *pClass,
								  AttribType offAttrib,
								  AttribAspect offAspect)
{
	AttribCurve *pcurve = NULL;
	AttribCurve **ppArray = class_GetAttribCurveArray(pClass,offAttrib);
	int s = eaSize(&ppArray);
	if(s)
	{
		int i=ATTRIBASPECT_INDEX(offAspect);
		if(verify(i<s))
		{
			pcurve = ppArray[i];
		}
	}
	return pcurve;
}

// Get the array of AttribCurves for the attrib, returns NULL if none exists
AttribCurve **class_GetAttribCurveArray(CharacterClass *pClass,
										AttribType offAttrib)
{
	AttribCurve **ppArray = NULL;
	PERFINFO_AUTO_START_FUNC();

// 	if(pClass->stAttribCurve)
// 	{
// 		stashIntFindPointer(pClass->stAttribCurve,offAttrib+1,(void**)&ppArray);
// 	}
	if(pClass->ppAttributes)
	{
		int i = ATTRIB_INDEX(offAttrib);
		int s = eaSize(&pClass->ppAttributes);
		if(s && i<s && pClass->ppAttributes[i])
		{
			ppArray = pClass->ppAttributes[i]->ppCurves;
		}
	}
	else if(!entIsServer())
	{
		// TODO(JW): Bugs: This doesn't properly take into account inheritance
		//  and furthermore is a total hack
		int i;
		static AttribCurve **ppCurves = NULL;
		eaClear(&ppCurves);
		eaSetSize(&ppCurves,ATTRIBASPECT_INDEX(kAttribAspect_Immunity)+1);
		for(i=eaSize(&pClass->ppAttribCurve)-1; i>=0; i--)
		{
			if(pClass->ppAttribCurve[i]->offAttrib==offAttrib)
			{
				eaSet(&ppCurves,pClass->ppAttribCurve[i],ATTRIBASPECT_INDEX(pClass->ppAttribCurve[i]->offAspect));
			}
		}
		ppArray = ppCurves;
	}

	PERFINFO_AUTO_STOP();
	return ppArray;
}


// Returns true if the Character's Class's type is in the types earray
S32 character_ClassTypeInTypes(Character *pchar, CharClassTypes *peTypes)
{
	CharClassTypes eType = GetCharacterClassEnum(pchar->pEntParent);
	return (-1!=eaiFind(&peTypes, eType));
}


AUTO_TRANS_HELPER_SIMPLE;
CharacterClass * characterclasses_FindByName( char * pchName )
{
	if(pchName && *pchName)
	{
		return (CharacterClass*)RefSystem_ReferentFromString(g_hCharacterClassDict,pchName);
	}
	return NULL;
}

CharacterClass * characterclass_GetAdjustedClass( char * pchClassInfoName, int iTeamSize, const char *pcSubRank, CritterDef * pCritterDef )
{
	CharacterClass *pClass = 0;
	CharacterClassInfo *pClassInfo = NULL;
	const char *pcClassInfoType;
	char *pchClassName = "";
	int idx;
	char tmpClassInfoName[256];

	PERFINFO_AUTO_START_FUNC();

	//!! hack for backward compat for existing critters w/ old class names
	//class info names should not end in _default, for example it should be henchman not henchman_default
	//anything that starts with object passes through as-is, objects are a special case
	if ( pchClassInfoName && *pchClassInfoName )
	{
		int len;

		strcpy(tmpClassInfoName, pchClassInfoName);
		len = (int)strlen(tmpClassInfoName);

		if ( (len>8) && ( strnicmp(tmpClassInfoName, "object", 6) != 0 ) && ( stricmp(&tmpClassInfoName[len-8], "_default") == 0 ) )
		{
			//remove the _Default
			tmpClassInfoName[len-8] = 0;
		}
	}
	else
	{
		//should never get here, but try to recover just in case
		Errorf("Critter %s has NULL class\n", (pCritterDef ? pCritterDef->pchName : "Unknown" ) );
		strcpy(tmpClassInfoName, "Default");
	}

	pClassInfo = CharacterClassInfo_FindByName(tmpClassInfoName);

	if ( !pClassInfo )
	{
		if (IsClient())
		{
			//If we're trying to look up a class on the client, try just finding one with the same name as the class info dict, since they may be the same.
			pClass = characterclasses_FindByName(tmpClassInfoName);
			if (pClass)
				return pClass;
		}
		//should never get here, but try to recover just in case
		Errorf("Critter %s has bad class: %s\n", (pCritterDef ? pCritterDef->pchName : "Unknown" ), tmpClassInfoName);
		pClass = characterclasses_FindByName("Default");
		return pClass;
	}

	//get index for this team size	 
	idx = CLAMP(iTeamSize, 1, MAX_TEAM_SIZE) - 1;

	pcClassInfoType = critterSubRankGetClassInfoType(pcSubRank);
	if (!pcClassInfoType || (stricmp(pcClassInfoType, "Normal") == 0)) {
		pchClassName = (char*)eaGet(&pClassInfo->ppchNormal, idx);
	} else if (stricmp(pcClassInfoType, "Weak") == 0) {
		pchClassName = (char*)eaGet(&pClassInfo->ppchWeak, idx);
	} else if (stricmp(pcClassInfoType, "Tough") == 0) {
		pchClassName = (char*)eaGet(&pClassInfo->ppchTough, idx);
	} else {
		assertmsg(0, "Unexpected class info type on subrank");
	}

	pClass = characterclasses_FindByName(pchClassName);

	if(!pClass)
	{
		//should never get here, but try to recover just in case
		Errorf("Critter %s CritterInfo table has bad class entry %s\n", (pCritterDef ? pCritterDef->pchName : "Unknown" ), pchClassName);
		pClass = characterclasses_FindByName("Default");

		PERFINFO_AUTO_STOP();

		return pClass;
	}

	PERFINFO_AUTO_STOP();

	return pClass;
}




void CharacterClassFixAttribTables(CharacterClass *pClass)
{
	int i,c;

	FORALL_PARSETABLE(parse_CharacterAttribs,c)
	{
		AttribType attrib = parse_CharacterAttribs[c].storeoffset;
		StructTypeField type = TOK_GET_TYPE(parse_CharacterAttribs[c].type);

		if(parse_CharacterAttribs[c].type & (TOK_REDUNDANTNAME | TOK_DIRTY_BIT))
			continue;

		if(type == TOK_START || type == TOK_END || type == TOK_IGNORE)
			continue;

		if(ATTRIB_SCALAR(attrib))
		{
			// These attributes default their base value to 1
			for(i=eaSize(&pClass->ppAttrBasic)-1;i>=0;i--)
			{
				F32* pfValue = F32PTR_OF_ATTRIB(pClass->ppAttrBasic[i],attrib);
				if(*pfValue == 0)
					*pfValue = 1;
			}
		}

		// All attributes default strength and resist values to 1
		for(i=eaSize(&pClass->ppAttrStr)-1;i>=0;i--)
		{
			F32* pfValue = F32PTR_OF_ATTRIB(pClass->ppAttrStr[i],attrib);
			if(*pfValue == 0)
				*pfValue = 1;
		}
		for(i=eaSize(&pClass->ppAttrRes)-1;i>=0;i--)
		{
			F32* pfValue = F32PTR_OF_ATTRIB(pClass->ppAttrRes[i],attrib);
			if(*pfValue == 0)
				*pfValue = 1;
		}
	}
}

S32 CharacterClassValidate(SA_PARAM_NN_VALID CharacterClass *pClass)
{
	S32 brtn = true;

	// This can't be in CharacterClassValidateRefs because it changes the pClass
	if(pClass->bPlayerClass && !GET_REF(pClass->hInventorySet))
	{
		DefaultInventory *pInv = (DefaultInventory *)RefSystem_ReferentFromString(g_hDefaultInventoryDict, "PlayerDefault");
		if(pInv)
		{
			SET_HANDLE_FROM_REFERENT(g_hDefaultInventoryDict, pInv, pClass->hInventorySet);
		}
		else
		{
			ErrorFilenamef( pClass->cpchFile, "Player class %s must have an inventory set specified, or there needs to be a \"PlayerDefault\" inventory set defined!", pClass->pchName);
			brtn = false;
		}
	}

	return brtn;
}

S32 CharacterClassValidateRefs(SA_PARAM_NN_VALID CharacterClass *pClass)
{
	S32 brtn = true;
	int i;
	Message *pMsgDisplayName;
	const char *pchDisplayName;

	if( !pClass->pchName || !pClass->pchName[0] )
	{
		ErrorFilenamef( pClass->cpchFile, "Class has no name!" );
		brtn = false;
	}

	pMsgDisplayName = GET_REF(pClass->msgDisplayName.hMessage);
	pchDisplayName = TranslateMessagePtrSafe(pMsgDisplayName,pClass->pchName);

	if( !pchDisplayName || !pchDisplayName[0] )
	{
		ErrorFilenamef( pClass->cpchFile, "Class %s has no display name!", pClass->pchName );
		brtn = false;
	}

	for(i=0;i<eaSize(&pClass->ppTableAdjustments);i++)
	{
		int j;
		PowerTable *pTable = powertable_Find(pClass->ppTableAdjustments[i]->pchName);
		
		if(!pTable)
		{
			ErrorFilenamef(pClass->cpchFile,"Invalid Table: %s\n(Create generic power table)",pClass->ppTableAdjustments[i]->pchName);
			brtn = false;
		}

		for(j=0; j<eaSize(&pClass->ppTableAdjustments[i]->ppchTables); j++)
		{
			if(!powertable_Find(pClass->ppTableAdjustments[i]->ppchTables[j]))
			{
				ErrorFilenamef(pClass->cpchFile,"Invalid SubTable: %s\n(Create generic power table)",pClass->ppTableAdjustments[i]->ppchTables[j]);
				brtn = false;
			}
		}

		/*
		if(!exprIsEmpty(pClass->ppTableAdjustments[i]->pExpr))
		{
			ErrorFilenamef(pClass->cpchFile,"Expression currently disabled in class table adjustments");
			brtn = false;
		}
		*/
	}
	if (IS_HANDLE_ACTIVE(pClass->hParentClass))
	{
		CharacterClass *pParent = GET_REF(pClass->hParentClass);
		
		if(!pParent)
		{
			ErrorFilenamef(pClass->cpchFile, "Cannot find Parent Class %s",REF_STRING_FROM_HANDLE(pClass->hParentClass));
			brtn = false;
		}
		if(eaSize(&pClass->ppAdjustAttrib) < 1 && eaSize(&pClass->ppTables) < 1 && eaSize(&pClass->ppTableAdjustments) < 1)
		{
			ErrorFilenamef(pClass->cpchFile, "Child Class has no adjustments and no Power Tables");
			brtn = false;
		}

		for(i=eaSize(&pClass->ppAdjustAttrib)-1;i>=0;i--)
		{
			if(pClass->ppAdjustAttrib[i]->level > MAX_LEVELS || pClass->ppAdjustAttrib[i]->level < 0)
			{
				ErrorFilenamef(pClass->cpchFile, "Adjustment %d level is out of range (0 - %d)",i,MAX_LEVELS);
				brtn = false;
			}
		}
	}

	for(i=0;i<eaSize(&pClass->ppExamplePowers);i++)
	{
		PowerDef *pPowerDef = GET_REF(pClass->ppExamplePowers[i]->hdef);

		if(!pPowerDef)
		{
			ErrorFilenamef(pClass->cpchFile,"Invalid ExamplePower '%s'.",REF_STRING_FROM_HANDLE(pClass->ppExamplePowers[i]->hdef));
			brtn = false;
		}
	}

	return brtn;
}

static void CharacterClass_SetupContext(ExprContext *pContext, F32 fParentValue, F32 fTableValue, F32 fAdjustLevel)
{
	exprContextSetFloatVar(pContext,"ParentValue",fParentValue);
	exprContextSetFloatVar(pContext,"TableValue",fTableValue);
	exprContextSetFloatVar(pContext,"AdjustLevel",fAdjustLevel);
}

ExprContext *CharacterClass_ContextCreate(void)
{
	ExprContext *pReturn = exprContextCreate();

	CharacterClass_SetupContext(pReturn,0,0,0);

	return pReturn;
}



static void OverrideStats(PowerStat ***pppBasicStats, PowerStat ***pppOverrides)
{
	int i,j;
	if((*pppBasicStats)==NULL)
	{
		eaCopyStructs(&g_PowerStats.ppPowerStats,pppBasicStats,parse_PowerStat);
	}
	for(i=eaSize(pppOverrides)-1; i>=0; i--)
	{
		for(j=eaSize(pppBasicStats)-1; j>=0; j--)
		{
			if(0==stricmp((*pppBasicStats)[j]->pchName,(*pppOverrides)[i]->pchName))
			{
				// Name matches, override
				StructDestroy(parse_PowerStat,(*pppBasicStats)[j]);
				(*pppBasicStats)[j] = StructClone(parse_PowerStat,(*pppOverrides)[i]);
				break;
			}
		}
		
		if(j<0)
		{
			// No prior stat of this name, append
			eaPush(pppBasicStats,StructClone(parse_PowerStat,(*pppOverrides)[i]));
		}
	}
}
void CharacterClassLoadFixArraySize(SA_PARAM_NN_VALID CharacterClass *pClass)
{
	int i;
	for(i=eaSize(&pClass->ppAttrBasic);i<MAX_LEVELS;i++)
	{
		CharacterAttribs *pAttrib;

		pAttrib = StructAlloc(parse_CharacterAttribs);

		if(pClass->ppAttrBasic && pClass->ppAttrBasic[i-1])
			StructCopyFields(parse_CharacterAttribs,pClass->ppAttrBasic[i-1],pAttrib,0,0);

		eaPush(&pClass->ppAttrBasic,pAttrib);
	}

	for(i=eaSize(&pClass->ppAttrStr);i<MAX_LEVELS;i++)
	{
		CharacterAttribs *pAttrib;

		pAttrib = StructAlloc(parse_CharacterAttribs);

		if(pClass->ppAttrStr && pClass->ppAttrStr[i-1])
			StructCopyFields(parse_CharacterAttribs,pClass->ppAttrStr[i-1],pAttrib,0,0);

		eaPush(&pClass->ppAttrStr,pAttrib);
	}

	for(i=eaSize(&pClass->ppAttrRes);i<MAX_LEVELS;i++)
	{
		CharacterAttribs *pAttrib;

		pAttrib = StructAlloc(parse_CharacterAttribs);

		if(pClass->ppAttrRes && pClass->ppAttrRes[i-1])
			StructCopyFields(parse_CharacterAttribs,pClass->ppAttrRes[i-1],pAttrib,0,0);

		eaPush(&pClass->ppAttrRes,pAttrib);
	}

}

// Checks to see if the class already has a table of the given name, and returns it if
//  it does.  If not, it tries to find a generic table of the given name, copy and insert
//  it into the class list.  If it can't find a generic table it returns NULL
static PowerTable *characterclass_GetExistingTable(CharacterClass *pClass,char *pchName)
{
	int i;
	PowerTable *pReturn = NULL;

	for(i=eaSize(&pClass->ppTables)-1; i>=0; i--)
	{
		if(!stricmp(pClass->ppTables[i]->pchName,pchName))
		{
			pReturn = pClass->ppTables[i];
			break;
		}
	}

	if(!pReturn)
	{
		PowerTable *pGeneric = powertable_Find(pchName);

		if(pGeneric)
		{
			pReturn = StructAlloc(parse_PowerTable);	
			StructCopyFields(parse_PowerTable,pGeneric,pReturn,0,0);
			eaPush(&pClass->ppTables,pReturn);
		}
	}
	
	return pReturn;
}

void CharacterClassApplyTableAdjustment(CharacterClass *pParent,CharacterClass *pClass)
{
	static ExprContext* s_pContext = NULL;
	int i;

	if(!s_pContext)
		s_pContext = CharacterClass_ContextCreate();

	// Destroy my data
	eaDestroyStruct(&pClass->ppTables,parse_PowerTable);

	if(pParent)
	{
		// Copy parent data
		for(i=0;i<eaSize(&pParent->ppTables);i++)
		{
			PowerTable *pTable = StructAlloc(parse_PowerTable);
			StructCopyFields(parse_PowerTable,pParent->ppTables[i],pTable,0,0);
			eaPush(&pClass->ppTables,pTable);
		}
	}

	//Apply table adjustments (in order)
	for(i=0;i<eaSize(&pClass->ppTableAdjustments);i++)
	{
		PowerTableAdjustment *pAdjust = pClass->ppTableAdjustments[i];
		PowerTable *pTable = characterclass_GetExistingTable(pClass,pClass->ppTableAdjustments[i]->pchName);
		F32 fParentValue, fTableValue, fAdjustLevel = 0;
		MultiVal mv;

		if(!pTable)
		{
			// Couldn't figure out a starting point, so this adjustment fails (Error messge?)
			continue;
		}

		// Apply adjustment's float values
		if(eafSize(&pAdjust->pfValues) > 0)
		{
			eafCopy(&pTable->pfValues,&pAdjust->pfValues);
		}

		// Apply adjustment's table values
		if(eaSize(&pAdjust->ppchTables) > 0)
		{
			eaCopy(&pTable->ppchTables,&pAdjust->ppchTables);
		}

		// Apply expression
		if(!exprIsEmpty(pAdjust->pExpr))
		{
			exprGenerate(pAdjust->pExpr,s_pContext);
			fParentValue = 0;
			if(pAdjust->ilevel > 0)
			{
				if(pParent)
					fParentValue = class_powertable_Lookup(pParent,pAdjust->pchName,pAdjust->ilevel);
				fTableValue = powertable_Lookup(pAdjust->pchName,pAdjust->ilevel);
				fAdjustLevel = pAdjust->ilevel;
				CharacterClass_SetupContext(s_pContext,fParentValue,fTableValue,fAdjustLevel);
				exprEvaluate(pAdjust->pExpr,s_pContext,&mv);
				pTable->pfValues[pAdjust->ilevel] = (F32)MultiValGetFloat(&mv,NULL);

			}
			else
			{
				int c;
				for(c=0;c<eafSize(&pTable->pfValues);c++)
				{
					if(pParent)
						fParentValue = class_powertable_Lookup(pParent,pAdjust->pchName,c);
					fTableValue = powertable_Lookup(pAdjust->pchName,c);
					fAdjustLevel = c;
					CharacterClass_SetupContext(s_pContext,fParentValue,fTableValue,fAdjustLevel);
					exprEvaluate(pAdjust->pExpr,s_pContext,&mv);
					pTable->pfValues[c] = (F32)MultiValGetFloat(&mv,NULL);
				}
			}
		}
		else
		{
			if(pAdjust->ilevel > 0)
			{
				pTable->pfValues[pAdjust->ilevel] = pAdjust->pfValues[pAdjust->ilevel];
			}
			else
			{
				// Not sure this is even needed anymore, seems like it's handled above?
				eafCopy(&pTable->pfValues,&pAdjust->pfValues);
			}
		}
	}
}
void CharacterClassApplyAdjustment(CharacterClass *pParent,CharacterClass *pClass)
{

	static ExprContext* s_pContext = NULL;
	int i;

	if(!s_pContext)
		s_pContext = CharacterClass_ContextCreate();

	// Destroy my data
	eaDestroyStruct(&pClass->ppAttrBasic,parse_CharacterAttribs);
	eaDestroyStruct(&pClass->ppAttrStr,parse_CharacterAttribs);
	eaDestroyStruct(&pClass->ppAttrRes,parse_CharacterAttribs);

	// Copy parent data
	for(i=0;i<eaSize(&pParent->ppAttrBasic); i++)
	{
		CharacterAttribs *pAttr = StructAlloc(parse_CharacterAttribs);
		StructCopyFields(parse_CharacterAttribs,pParent->ppAttrBasic[i],pAttr,0,0);
		eaPush(&pClass->ppAttrBasic,pAttr);
	}
	for(i=0;i<eaSize(&pParent->ppAttrStr); i++)
	{
		CharacterAttribs *pAttr = StructAlloc(parse_CharacterAttribs);
		StructCopyFields(parse_CharacterAttribs,pParent->ppAttrStr[i],pAttr,0,0);
		eaPush(&pClass->ppAttrStr,pAttr);
	}
	for(i=0;i<eaSize(&pParent->ppAttrRes); i++)
	{
		CharacterAttribs *pAttr = StructAlloc(parse_CharacterAttribs);
		StructCopyFields(parse_CharacterAttribs,pParent->ppAttrRes[i],pAttr,0,0);
		eaPush(&pClass->ppAttrRes,pAttr);
	}

	// Apply adjustments
	for(i=0; i<eaSize(&pClass->ppAdjustAttrib); i++)
	{
		CharacterClassAdjustAttrib *pAdjust = pClass->ppAdjustAttrib[i];
		CharacterAttribs **ppClassAspect = *(CharacterAttribs***)((char*)pClass + pAdjust->eAspect);
		CharacterAttribs **ppParentAspect = *(CharacterAttribs***)((char*)pParent + pAdjust->eAspect);
		F32 fParentValue, fAdjustLevel = 0;

		if(pAdjust->pExpr && !exprIsEmpty(pAdjust->pExpr))
			exprGenerate(pAdjust->pExpr,s_pContext);

		{
			if(pAdjust->level > 0)
			{
				F32 *pfClass = (F32*)((char*)(ppClassAspect[pAdjust->level - 1]) + pAdjust->eType);
				F32 *pfParent = (F32*)((char*)(ppParentAspect[pAdjust->level - 1]) + pAdjust->eType);
				bool bGood;
				MultiVal mv;
				fParentValue = *pfParent;
				fAdjustLevel = pAdjust->level;
				if(pAdjust->pExpr && !exprIsEmpty(pAdjust->pExpr))
				{
					CharacterClass_SetupContext(s_pContext,fParentValue,0,fAdjustLevel);
					exprEvaluate(pAdjust->pExpr,s_pContext,&mv);
					*pfClass = (F32)(MultiValGetFloat(&mv,&bGood));
				}
				else
					*pfClass = pAdjust->fValues[pAdjust->level];
			}
			else
			{
				int j;
				for(j=0;j<eaSize(&ppClassAspect);j++)
				{
					F32 *pfClass = (F32*)((char*)(ppClassAspect[j]) + pAdjust->eType);
					F32 *pfParent = (F32*)((char*)(ppParentAspect[j]) + pAdjust->eType);
					bool bGood;
					MultiVal mv;
					fParentValue = *pfParent;
					fAdjustLevel = j+1;
					if(pAdjust->pExpr && !exprIsEmpty(pAdjust->pExpr))
					{
						CharacterClass_SetupContext(s_pContext,fParentValue,0,fAdjustLevel);
						exprEvaluate(pAdjust->pExpr,s_pContext,&mv);
						*pfClass = (F32)(MultiValGetFloat(&mv,&bGood));
					}
					else
					{
						*pfClass = pAdjust->fValues[j];
					}
				}
			}
		}
	}
	// Generate expressions for stats
	for(i=eaSize(&pClass->ppStats)-1; i>=0; i--)
	{
		exprGenerate(pClass->ppStats[i]->expr,s_pContext);
		// TODO(JW): Flag inactive stats as inactive
	}
}

static void CharacterClassLoadComputeTables(CharacterClass *pBaseClass, CharacterClass *pClass)
{
	int i;
	CharacterClass *pParentClass;
	for(i=eaSize(&pClass->ppTables)-1; i>=0; i--)
	{
		powertable_Compress(pClass->ppTables[i]);
		powertable_Generate(pClass->ppTables[i]);
		if(powertable_Validate(pClass->ppTables[i],pClass->cpchFile))
		{
			stashAddPointer(pBaseClass->stTables,pClass->ppTables[i]->pchName,pClass->ppTables[i],false);
		}
	}

	for(i=eaSize(&pClass->ppVars)-1; i>=0; i--)
	{
		stashAddPointer(pBaseClass->stVars,pClass->ppVars[i]->pchName,pClass->ppVars[i],false);
	}

	for(i=eaSize(&pClass->ppAttribCurve)-1; i>=0; i--)
	{
		AttribCurve *pcurve = pClass->ppAttribCurve[i];
		int iAttrib = ATTRIB_INDEX(pcurve->offAttrib);
		int iAspect = ATTRIBASPECT_INDEX(pcurve->offAspect);
		CharacterClassAttrib *pAttribute = NULL;
		int s = eaSize(&pClass->ppAttributes);

		if(s && iAttrib<s)
		{
			pAttribute = pClass->ppAttributes[iAttrib];

			if(!pAttribute)
			{
				pClass->ppAttributes[iAttrib] = pAttribute = StructAlloc(parse_CharacterClassAttrib);
			}

			eaSetSize(&pAttribute->ppCurves,ATTRIBASPECT_INDEX(kAttribAspect_Immunity)+1);
			pAttribute->ppCurves[iAspect] = pcurve;
		}
		
// 		AttribCurve **ppArray = NULL;
// 		int idx = ATTRIBASPECT_INDEX(pcurve->offAspect);
// 		stashIntFindPointer(pBaseClass->stAttribCurve,pcurve->offAttrib+1,(void**)&ppArray);
// 		eaSetSize(&ppArray,ATTRIBASPECT_INDEX(kAttribAspect_Immunity)+1);
// 		eaSet(&ppArray,pcurve,idx);
// 		stashIntAddPointer(pBaseClass->stAttribCurve,pcurve->offAttrib+1,ppArray,true);
	}

	pParentClass = GET_REF(pClass->hParentClass);

	if (pParentClass)
	{
		CharacterClassLoadComputeTables(pBaseClass,pParentClass);
	}

}
static void CharacterClassLoadComputeStats(CharacterClass *pBaseClass, CharacterClass *pClass)
{
	CharacterClass *pParentClass;
	pParentClass = GET_REF(pClass->hParentClass);

	if (pParentClass)
	{
		// Start with parent, work down
		CharacterClassLoadComputeStats(pBaseClass,pParentClass);
	}

	if(pClass->ppStats)
	{
		OverrideStats(&pBaseClass->ppStatsFull,&pClass->ppStats);
	}
}

static void CompressAttribArray(F32 **ppfValues, F32 fDefault)
{
	int i, s = eafSize(ppfValues);
	for(i=s-1; i>=1; i--)
	{
		if((*ppfValues)[i]!=(*ppfValues)[i-1])
		{
			break;
		}
	}
	// i is the last index we need, everything past that was identical
	if(i>=0 && (i+1)!=s)
	{
		eafSetCapacity(ppfValues,i+1);
	}

	if(eafSize(ppfValues)==1)
	{
		if((*ppfValues)[0]==fDefault)
		{
			// It only has a single value in the array, and it matches the default, so destroy the entire array
			eafDestroy(ppfValues);
		}
	}
}

static void CharacterClassSetNativeSpeedRunning(SA_PARAM_NN_VALID CharacterClass *pClass, CharacterAttribs *pattr)
{
	pClass->fNativeSpeedRunning = pattr->fSpeedRunning;
}

static void CharacterClassLoadAdjust(SA_PARAM_NN_VALID CharacterClass *pClass)
{
	CharacterClass *pParent;

	if (pClass->stTables || pClass->stVars || pClass->ppStatsFull)
	{
		return; //already processed
	}

	pParent = GET_REF(pClass->hParentClass);

	if(pParent)
	{
		// set up parent first
		CharacterClassLoadAdjust(pParent);
	}
	else if (IS_HANDLE_ACTIVE(pClass->hParentClass))
	{
		// Active handle, but not found = error
		ErrorFilenamef(pClass->cpchFile, "Class %s has unknown parent class %s.",pClass->pchName,REF_STRING_FROM_HANDLE(pClass->hParentClass));
	}

	// Set up attributes earray
	eaSetSize(&pClass->ppAttributes,ATTRIB_INDEX(kAttribType_LAST)+1);

	if (pParent)
	{
		eaCopyStructs(&pParent->ppPowers,&pClass->ppPowers,parse_CharacterClassPower);
		eaCopyStructs(&pParent->ppPowerSlots,&pClass->ppPowerSlots,parse_PowerSlot);
		CharacterClassApplyAdjustment(pParent,pClass);
		CharacterClassApplyTableAdjustment(pParent,pClass);
	}
	else
	{
		CharacterClassApplyTableAdjustment(NULL,pClass);
	}

	// Set up tables stashtable
	pClass->stTables = stashTableCreateWithStringKeys(32,StashDefault);

	// Set up vars stashtable
	pClass->stVars = stashTableCreateWithStringKeys(16,StashDefault);

	// This fills out the stash tables that were just created
	CharacterClassLoadComputeTables(pClass,pClass);

	// This fills in the ppFullStats array if it is needed. it may still be null if it isn't needed
	CharacterClassLoadComputeStats(pClass,pClass);

	{
		int attrib;
		for(attrib=0; attrib<SIZE_OF_ALL_NORMAL_ATTRIBS; attrib+=SIZE_OF_NORMAL_ATTRIB)
		{
			int i = ATTRIB_INDEX(attrib);
			int j;
			CharacterClassAttrib *pAttrib = pClass->ppAttributes[i];
			if(!pAttrib)
			{
				pAttrib = pClass->ppAttributes[i] = StructAlloc(parse_CharacterClassAttrib);
			}

			for(j=0; j<MAX_LEVELS; j++)
			{
				CharacterAttribs *pattr = pClass->ppAttrBasic[j];
				CharacterClassSetNativeSpeedRunning(pClass, pattr);
				eafPush(&pAttrib->pfBasic,*F32PTR_OF_ATTRIB(pattr,attrib));
				pattr = pClass->ppAttrStr[j];
				eafPush(&pAttrib->pfStrength,*F32PTR_OF_ATTRIB(pattr,attrib));
				pattr = pClass->ppAttrRes[j];
				eafPush(&pAttrib->pfResist,*F32PTR_OF_ATTRIB(pattr,attrib));
			}
		}

		for(attrib=0; attrib<=ATTRIB_INDEX(kAttribType_LAST); attrib++)
		{
			CharacterClassAttrib *pAttrib = pClass->ppAttributes[attrib];
			if(!pAttrib)
				continue;

			CompressAttribArray(&pAttrib->pfBasic, 0);
			CompressAttribArray(&pAttrib->pfStrength, 1);
			CompressAttribArray(&pAttrib->pfResist, 1);

			if(!eafSize(&pAttrib->pfBasic)
				&& !eafSize(&pAttrib->pfStrength)
				&& !eafSize(&pAttrib->pfResist)
				&& !eaSize(&pAttrib->ppCurves))
			{
				StructDestroy(parse_CharacterClassAttrib,pClass->ppAttributes[attrib]);
				pClass->ppAttributes[attrib] = NULL;
			}
			else if(eaSize(&pAttrib->ppCurves))
			{
				if(pAttrib->ppCurves[ATTRIBASPECT_INDEX(kAttribAspect_BasicAbs)]
					|| pAttrib->ppCurves[ATTRIBASPECT_INDEX(kAttribAspect_BasicFactPos)]
					|| pAttrib->ppCurves[ATTRIBASPECT_INDEX(kAttribAspect_BasicFactNeg)])
					{
						eaiPush(&pClass->piAttribCurveBasic,attrib*SIZE_OF_NORMAL_ATTRIB);
					}
			}
		}
	}
}

static void CharacterClassFixupPointers(CharacterClass *pBaseClass, CharacterClass *pClass)
{
	CharacterClass *pParentClass;
	int i;
	for(i=eaSize(&pClass->ppTables)-1; i>=0; i--)
	{
		if(powertable_Validate(pClass->ppTables[i],pClass->cpchFile))
		{
			stashAddPointer(pBaseClass->stTables,pClass->ppTables[i]->pchName,pClass->ppTables[i],true);
		}
	}

	for(i=eaSize(&pClass->ppVars)-1; i>=0; i--)
	{
		stashAddPointer(pBaseClass->stVars,pClass->ppVars[i]->pchName,pClass->ppVars[i],true);
	}

	pParentClass = GET_REF(pClass->hParentClass);

	if (pParentClass)
	{
		CharacterClassFixupPointers(pBaseClass,pParentClass);
	}
}

AUTO_FIXUPFUNC;
TextParserResult characterclassattribute_Fixup(CharacterClassAttrib *pAttribute, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if (!isSharedMemory(pAttribute->ppCurves))
		{		
			eaDestroy(&pAttribute->ppCurves);
		}
	}

	return true;
}

static ExprContext *CharacterClass_GetContext(void)
{
	static ExprContext *s_pContext = NULL;

	if(!s_pContext)
	{
		ExprFuncTable* stTable;

		s_pContext = exprContextCreate();

		stTable = exprContextCreateFunctionTable();

		exprContextAddFuncsToTableByTag(stTable,"Player");
		exprContextAddFuncsToTableByTag(stTable, "util");

		exprContextSetFuncTable(s_pContext, stTable);

		exprContextSetPointerVar(s_pContext,"Player", NULL, parse_Entity, false, true);
		exprContextSetIntVar(s_pContext,"Level",0);

		exprContextSetAllowRuntimePartition(s_pContext);
		exprContextSetAllowRuntimeSelfPtr(s_pContext);
	}

	devassert(s_pContext!=NULL);

	return s_pContext;
}

void CharacterClass_Generate(CharacterClass *pClass)
{
	ExprContext *pContext = CharacterClass_GetContext();

	exprContextSetUserPtr(pContext, pClass, parse_CharacterClass);
	exprGenerate(pClass->exprRequires, pContext);
	exprContextSetUserPtr(pContext, NULL, NULL);
}

static int CharacterClassValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, CharacterClass *pClass, U32 userID)
{
	switch (eType)
	{
	case RESVALIDATE_POST_TEXT_READING:
		CharacterClassLoadFixArraySize(pClass);
		CharacterClassFixAttribTables(pClass);
		return VALIDATE_HANDLED;
	case RESVALIDATE_POST_BINNING:
#if defined(GAMESERVER) || defined(APPSERVER)
		CharacterClass_Generate(pClass);
#endif
		CharacterClassLoadAdjust(pClass);	
		CharacterClassValidate(pClass);
		return VALIDATE_HANDLED;
	case RESVALIDATE_FINAL_LOCATION:
		CharacterClassFixupPointers(pClass, pClass);
		return VALIDATE_HANDLED;
	case RESVALIDATE_CHECK_REFERENCES:
		CharacterClassValidateRefs(pClass);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_TRANS_HELPER;
CharacterPath* entity_trh_GetPrimaryCharacterPath(ATH_ARG NOCONST(Entity) *pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pChar))
	{
		return GET_REF(pEnt->pChar->hPath);
	}
	return NULL;
}

AUTO_TRANS_HELPER;
void entity_trh_GetChosenCharacterPaths(ATH_ARG NOCONST(Entity) *pEnt, CharacterPath*** peaPathsOut)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pChar) && GET_REF(pEnt->pChar->hPath))
	{
		int i;
		eaPush(peaPathsOut, GET_REF(pEnt->pChar->hPath));

		for (i = 0; i < eaSize(&pEnt->pChar->ppSecondaryPaths); i++)
			eaPush(peaPathsOut, GET_REF(pEnt->pChar->ppSecondaryPaths[i]->hPath));
	}
}

AUTO_TRANS_HELPER;
void entity_trh_GetChosenCharacterPathsOfType(ATH_ARG NOCONST(Entity) *pEnt, CharacterPath*** peaPathsOut, CharacterPathType eType)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pChar) && GET_REF(pEnt->pChar->hPath))
	{
		int i;
		CharacterPath* pPath = GET_REF(pEnt->pChar->hPath);
		if (pPath && pPath->eType == eType)
			eaPush(peaPathsOut, pPath);

		for (i = 0; i < eaSize(&pEnt->pChar->ppSecondaryPaths); i++)
		{
			pPath = GET_REF(pEnt->pChar->ppSecondaryPaths[i]->hPath);
			if (pPath && pPath->eType == eType)
				eaPush(peaPathsOut, pPath);
		}
	}
}

AUTO_TRANS_HELPER;
bool entity_trh_HasCharacterPath( SA_PARAM_NN_VALID ATH_ARG NOCONST(Entity) *pEnt, const char* pchPath)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pChar))
	{
		int i;
		const char* pchPooledPath = allocFindString(pchPath);
		if (REF_STRING_FROM_HANDLE(pEnt->pChar->hPath) == pchPooledPath)
			return true;

		for (i = 0; i < eaSize(&pEnt->pChar->ppSecondaryPaths); i++)
		{
			if (REF_STRING_FROM_HANDLE(pEnt->pChar->ppSecondaryPaths[i]->hPath) == pchPooledPath)
				return true;
		}
	}
	return false;
}

AUTO_TRANS_HELPER;
bool entity_trh_HasAnyCharacterPath(ATH_ARG NOCONST(Entity) *pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pChar))
	{
		return (GET_REF(pEnt->pChar->hPath) || eaSize(&pEnt->pChar->ppSecondaryPaths) > 0);
	}
	return false;
}

bool entity_PlayerCanBecomeClass(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID CharacterClass *pClass)
{
	S32 iCharacterLevel = entity_GetSavedExpLevel(pEnt);
	CharacterPath** eaPaths = NULL;
	int i, j;

	if(!pClass->bPlayerClass || (pClass->bPlayerClassRestricted && pEnt->pPlayer->accessLevel < ACCESS_GM))
		return(false);

	if(pClass->iLevelRequired > iCharacterLevel)
		return(false);

	eaStackCreate(&eaPaths, eaSize(&pEnt->pChar->ppSecondaryPaths) + 1);
	entity_GetChosenCharacterPaths(pEnt, &eaPaths);

	//All paths must allow this class.
	for (i = 0; i < eaSize(&eaPaths); i++)
	{
		if (eaSize(&eaPaths[i]->eaRequiredClasses) > 0)
		{
			for (j=eaSize(&eaPaths[i]->eaRequiredClasses)-1; j>=0; j--)
			{
				if (GET_REF(eaPaths[i]->eaRequiredClasses[j]->hClass) == pClass)
					break;
			}

			if (j < 0)
				return(false);
		}
	}

#if defined(GAMESERVER) || defined(APPSERVER)
	if(pClass->exprRequires)
	{
		ExprContext *pContext = CharacterClass_GetContext();
		MultiVal mVal;

		exprContextSetSelfPtr(pContext, pEnt);
		exprContextSetPartition(pContext, entGetPartitionIdx(pEnt));
		exprContextSetPointerVar(pContext,"Player", pEnt, parse_Entity, false, true);
		exprContextSetIntVar(pContext,"Level",iCharacterLevel);
	
		exprEvaluate(pClass->exprRequires ,pContext,&mVal);

		if(MultiValGetInt(&mVal,NULL) <= 0)
			return(false);
	}
#endif

	return(true);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_IFDEF(GAMECLIENT) ACMD_PRIVATE;
void characterclasses_SendClassList(Entity *pEnt)
{
	if(pEnt && pEnt->pPlayer)
	{
		RefDictIterator iter;
		CharacterClass *pClass;
		char *pchRefName;
		CharacterClassNameList *pList = StructCreate(parse_CharacterClassNameList);

		RefSystem_InitRefDictIterator(g_hCharacterClassDict, &iter);
		RefSystem_GetNextReferentAndRefDataFromIterator(&iter, (Referent*)(&pClass), (ReferenceData*)(&pchRefName));
		
		while(pchRefName && pClass)
		{
			if(entity_PlayerCanBecomeClass(pEnt, pClass))
			{
				eaPush(&pList->ppCharacterClassNameList, StructAllocString(pchRefName));
			}
			RefSystem_GetNextReferentAndRefDataFromIterator(&iter, (Referent*)(&pClass), (ReferenceData*)(&pchRefName));
		}

		if(eaSize(&pList->ppCharacterClassNameList))
		{
#ifdef GAMESERVER
			ClientCmd_UpdateClassList(pEnt, pList);
#endif
		}

		StructDestroy(parse_CharacterClassNameList, pList);
	}
}

AUTO_RUN;
int RegisterClassDictionary(void)
{
	g_hCharacterClassDict = RefSystem_RegisterSelfDefiningDictionary("CharacterClass",false, parse_CharacterClass, true, true, "Class");

	resDictManageValidation(g_hCharacterClassDict, CharacterClassValidateCB);
	if (IsServer())
	{
		resDictProvideMissingResources(g_hCharacterClassDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hCharacterClassDict, ".msgDisplayName.Message", NULL, NULL, NULL, NULL);			
		}
	} 
	else if (IsClient())
	{
		resDictRequestMissingResources(g_hCharacterClassDict, 16, false, resClientRequestSendReferentCommand);
	}
	return 1;
}


CharClassTypes GetCharacterClassEnum(Entity *pent)
{
	CharClassTypes eClassType = CharClassTypes_None;
	if (pent)
	{
		if (pent->pChar)
		{
			CharacterClass* pClass = GET_REF(pent->pChar->hClass);
			if (pClass)
			{
				eClassType = pClass->eType;
			}
		}
	}
	return eClassType; 
}

CharClassCategory CharClassCategory_getCategoryFromEntity(Entity *pEnt)
{
	CharacterClass *pClass = SAFE_GET_REF2(pEnt, pChar, hClass);
	return SAFE_MEMBER(pClass, eCategory);
}

CharClassCategory CharClassCategory_getCategoryFromPuppetEntity(int iPartitionIdx, PuppetEntity *pPuppet)
{
	if (pPuppet)
	{
		Entity *pEnt = GET_REF(pPuppet->hEntityRef) ? GET_REF(pPuppet->hEntityRef) : SavedPuppet_GetEntity(iPartitionIdx, pPuppet);
		return CharClassCategory_getCategoryFromEntity(pEnt);
	}
	else
	{
		return CharClassCategory_None;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetCharClassCategorySet);
SA_RET_OP_VALID CharClassCategorySet *CharClassCategorySet_find(const char* pchName)
{
	return RefSystem_ReferentFromString(g_hCharacterClassCategorySetDict, pchName);
}

CharClassCategorySet *CharClassCategorySet_getCategorySet(CharClassCategory eCategory, CharClassTypes eType)
{
	CharClassCategorySet *pSet;
	RefDictIterator iter;
	RefSystem_InitRefDictIterator(g_hCharacterClassCategorySetDict, &iter);
	while (pSet = RefSystem_GetNextReferentFromIterator(&iter))
	{
		if (pSet->eClassType == eType && ea32Find(&pSet->eaCategories, eCategory) >= 0)
		{
			return pSet;
		}
	}
	return NULL;
}

CharClassCategorySet *CharClassCategorySet_getCategorySetFromClass(CharacterClass *pClass)
{
	return pClass ? CharClassCategorySet_getCategorySet(pClass->eCategory, pClass->eType) : NULL;
}

CharClassCategorySet *CharClassCategorySet_getCategorySetFromEntity(Entity *pEnt, CharClassTypes eType)
{
	CharClassCategory eCategory = CharClassCategory_getCategoryFromEntity(pEnt);
	return eCategory ? CharClassCategorySet_getCategorySet(eCategory, eType) : NULL;
}

CharClassCategorySet *CharClassCategorySet_getCategorySetFromPuppetEntity(int iPartitionIdx, PuppetEntity *pPuppet)
{
	CharClassCategory eCategory = CharClassCategory_getCategoryFromPuppetEntity(iPartitionIdx, pPuppet);
	return eCategory ? CharClassCategorySet_getCategorySet(eCategory, pPuppet->eType) : NULL;
}

bool CharClassCategorySet_checkIfPass(CharClassCategorySet *pSet, CharClassCategory eCategory, CharClassTypes eType)
{
	if (!pSet)
		return true;

	if (pSet->eClassType != eType)
		return false;
			
	return ea32Find(&pSet->eaCategories, eCategory) >= 0;
}

bool CharClassCategorySet_checkIfPassClass(CharClassCategorySet *pSet, CharacterClass *pClass)
{
	return pClass && CharClassCategorySet_checkIfPass(pSet, pClass->eCategory, pClass->eType);
}

bool CharClassCategorySet_checkIfPassEntity(CharClassCategorySet *pSet, Entity* pEnt)
{
	return pEnt && CharClassCategorySet_checkIfPassClass(pSet, SAFE_GET_REF2(pEnt, pChar, hClass));
}

bool CharClassCategorySet_checkIfPassPuppetEntity(CharClassCategorySet *pSet, int iPartitionIdx, PuppetEntity* pPuppet)
{
	return CharClassCategorySet_checkIfPassEntity(pSet, SavedPuppet_GetEntity(iPartitionIdx, pPuppet));
}

static REF_TO(CharClassCategorySet) s_hDefaultSet;

CharClassCategorySet *CharClassCategorySet_getPreferredSet(Entity* pEnt)
{
	CharClassCategorySet *pSet = SAFE_GET_REF3(pEnt, pSaved, pPuppetMaster, hPreferredCategorySet);
	return pSet ? pSet : GET_REF(s_hDefaultSet);
}

CharClassCategorySet *CharClassCategorySet_getPreferredSetForRegion(Entity* pEnt, RegionRules *pRegionRules)
{
	CharClassCategorySet *pSet = CharClassCategorySet_getPreferredSet(pEnt);
	return (pSet && ea32Find(&pRegionRules->peCharClassTypes, pSet->eClassType) >= 0) ? pSet : NULL;
}

CharClassCategorySet *CharClassCategorySet_getPreferredSetForClassType(Entity* pEnt, S32 eClassType)
{
	CharClassCategorySet *pPreferredSet = SAFE_GET_REF3(pEnt, pSaved, pPuppetMaster, hPreferredCategorySet);
	if (pPreferredSet && pPreferredSet->eClassType == eClassType)
		return pPreferredSet;
	else if ((pPreferredSet = GET_REF(s_hDefaultSet)) && pPreferredSet->eClassType == eClassType)
		return pPreferredSet;
	else
		return NULL;
}

AUTO_EXPR_FUNC(UIGen);
int CheckCharacterClassAgainstKey(SA_PARAM_OP_VALID Entity *pent, SA_PARAM_NN_STR const char* key)
{
	CharClassTypes eClassType = GetCharacterClassEnum(pent);

	if (eClassType != CharClassTypes_None && eClassType == StaticDefineIntGetInt(CharClassTypesEnum, key))
	{
		return 1;
	}
	return 0;
}

// This is used for CharClassCategorySet validation
static int iMaxCategoryValue;

static void CharacterClassCategories_Load(void)
{ 
	CharClassCategories charClassCategories = {0};
	int i = 0; 

	g_pExtraCharClassCategories = DefineCreate();

	if (g_pExtraCharClassCategories)
	{
		loadstart_printf("Loading Character Class Categories... ");

		ParserLoadFiles(NULL, "defs/config/"CHAR_CATEGORY_ID_FILE".def", CHAR_CATEGORY_ID_FILE".bin", PARSER_OPTIONALFLAG, parse_CharClassCategories, &charClassCategories);

		for (i = 0; i < eaSize(&charClassCategories.pchNames); i++)
			DefineAddInt(g_pExtraCharClassCategories, charClassCategories.pchNames[i], i+1);

		iMaxCategoryValue = i;

		StructDeInit(parse_CharClassCategories, &charClassCategories);

		loadend_printf("done (%d).", i); 
	}
}

AUTO_RUN;
void CharacterClassCategorySets_AutoRun(void)
{
	g_hCharacterClassCategorySetDict = RefSystem_RegisterSelfDefiningDictionary("CharClassCategorySet", false, parse_CharClassCategorySet, false, false, NULL);
	if (isDevelopmentMode() || isProductionEditMode())
	{
		resDictMaintainInfoIndex(g_hCharacterClassCategorySetDict, ".name", NULL, NULL, NULL, NULL);
	}
}

#define CHAR_CLASS_CATEGORY_SET_FILE "defs/config/CharacterClassCategorySets.def"

static int CharacterClassCategorySets_Validate(enumResourceValidateType eType, const char *pDictName, const char *pSetName, CharClassCategorySet *pSet, U32 iUserID)
{
	int i;
	switch (eType)
	{
	case RESVALIDATE_POST_BINNING:
		for (i = 0; i < ea32Size(&pSet->eaCategories); i++)
		{
			U32 eCat = pSet->eaCategories[i];
			if (eCat == 0)
				ErrorFilenamef(CHAR_CLASS_CATEGORY_SET_FILE, "CharClassCategorySets: None is not a valid category");
		}
		if (pSet->bDefaultPreferredSet && GET_REF(s_hDefaultSet))
			ErrorFilenamef(CHAR_CLASS_CATEGORY_SET_FILE, "CharClassCategorySets: More than one set declared DefaultPreferredSet.");
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static void CharacterClassCategorySets_Load(void)
{
	CharClassCategorySet *pSet;
	RefDictIterator iter;
	resDictManageValidation(g_hCharacterClassCategorySetDict, CharacterClassCategorySets_Validate);
	resLoadResourcesFromDisk(g_hCharacterClassCategorySetDict, NULL, CHAR_CLASS_CATEGORY_SET_FILE, NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED);

	RefSystem_InitRefDictIterator(g_hCharacterClassCategorySetDict, &iter);
	while (pSet = RefSystem_GetNextReferentFromIterator(&iter))
	{
		if (pSet->bDefaultPreferredSet)
		{
			REF_HANDLE_SET_FROM_REFERENT(g_hCharacterClassCategorySetDict, pSet, s_hDefaultSet);
			break;
		}
	}
}

AUTO_STARTUP(AS_CharacterClassTypes);
void characterclasstypes_Load(void)
{
	CharClassTypeExtraIDs CharClassTypeIDs = {0};
	int ii = 0; 

	g_ExtraCharClassTypeIDs = DefineCreate();

	if (g_ExtraCharClassTypeIDs)
	{
		// Read in per-project mode IDs and add them to the lookup table.

		loadstart_printf("Loading Character Types... ");

		ParserLoadFiles(NULL, "defs/config/"CHAR_TYPE_ID_FILE".def", CHAR_TYPE_ID_FILE".bin", PARSER_OPTIONALFLAG, parse_CharClassTypeExtraIDs, &CharClassTypeIDs);

		for (ii = 0; ii < eaSize(&CharClassTypeIDs.eachID); ii++)
			DefineAddInt(g_ExtraCharClassTypeIDs, CharClassTypeIDs.eachID[ii], CharClassTypes_None + 1 + ii);

		StructDeInit(parse_CharClassTypeExtraIDs, &CharClassTypeIDs);

		loadend_printf("done (%d).", ii); 
	}

	CharacterClassCategories_Load();
	CharacterClassCategorySets_Load();

#ifdef GAMECLIENT
	ui_GenInitStaticDefineVars(CharClassTypesEnum, "CharacterClassType_");
#endif

}

AUTO_STARTUP(CharacterClasses) ASTRT_DEPS(AS_AttribSets, PowerVars, PowerSlots, InventoryBags, AS_CharacterClassTypes, ClassAssignedStats);
void characterclasses_Load(void)
{
	switch( GetAppGlobalType() ) {
		case GLOBALTYPE_UGCSEARCHMANAGER: case GLOBALTYPE_UGCDATAMANAGER:
			return;
	}
	
	if (!IsClient()) 
	{
		resLoadResourcesFromDisk(g_hCharacterClassDict, "defs/classes", ".class", "CharacterClasses.bin", RESOURCELOAD_SHAREDMEMORY);
	}
}

AUTO_STARTUP(CharacterClassPowers) ASTRT_DEPS(CharacterClasses, Powers);
void CharacterClassPowersValidate(void)
{
	RefDictIterator iter;
	CharacterClass *pClass;

	RefSystem_InitRefDictIterator(g_hCharacterClassDict, &iter);
	while(pClass = RefSystem_GetNextReferentFromIterator(&iter))
	{
		if(pClass->ppPowers)
		{
			int i;
			PowerDef *pPowerDef;
			for(i = 0; i < eaSize(&pClass->ppPowers); i++)
			{
				pPowerDef = GET_REF(pClass->ppPowers[i]->hdef);
				if (!pPowerDef)
				{
					ErrorFilenamef(pClass->cpchFile, "Non-player class %s has an invalid power definition!", pClass->pchName);
				}
			}
		}
	}
}

// Handle to the class info dictionary
DictionaryHandle g_hCharacterClassInfoDict;

static int CharacterClassInfoValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, CharacterClassInfo *pClassInfo, U32 userID)
{
	switch (eType)
	{	
	xcase RESVALIDATE_POST_TEXT_READING: // Called after load/reload but before binning
		if ( (eaSize(&pClassInfo->ppchNormal) != 5 ) )
			Errorf("Error invalid size for CharacterClassInfo Normal earray : %s\n", pClassInfo->pchName);

		if ( (eaSize(&pClassInfo->ppchWeak) != 5 ) )
			Errorf("Error invalid size for CharacterClassInfo Weak earray : %s\n", pClassInfo->pchName);

		if ( (eaSize(&pClassInfo->ppchTough) != 5 ) )
			Errorf("Error invalid size for CharacterClassInfo Tough earray : %s\n", pClassInfo->pchName);

		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_STARTUP(CharacterClassInfos) ASTRT_DEPS(CharacterClasses);
void CharacterClassInfoLoad(void)
{
	if(!IsClient())
	{
		resLoadResourcesFromDisk(g_hCharacterClassInfoDict, NULL, "defs/config/characterclassinfo.def",  "characterclassinfo.bin", RESOURCELOAD_SHAREDMEMORY);
	}
}


CharacterClassInfo * CharacterClassInfo_FindByName( char * pchName )
{
	if(pchName && *pchName)
	{
		return (CharacterClassInfo*)RefSystem_ReferentFromString(g_hCharacterClassInfoDict,pchName);
	}
	return NULL;
}


AUTO_RUN;
int CharacterClassInfo_Startup(void)
{
	// Set up reference dictionaries
	g_hCharacterClassInfoDict = RefSystem_RegisterSelfDefiningDictionary("CharacterClassInfo",false, parse_CharacterClassInfo, true, true, NULL);

	resDictManageValidation(g_hCharacterClassInfoDict, CharacterClassInfoValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hCharacterClassInfoDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hCharacterClassInfoDict, ".Name", NULL, NULL, NULL, NULL);			
		}
	} 
	else
	{
		resDictRequestMissingResources(g_hCharacterClassInfoDict, 8, false, resClientRequestSendReferentCommand);
	}

	return 1;
}

// Returns the suggested purchase struct matching the power table from a character path structure
CharacterPathSuggestedPurchase * CharacterPath_GetSuggestedPurchaseByPowerTable(SA_PARAM_NN_VALID CharacterPath *pCharacterPath, SA_PARAM_NN_STR const char *pchPowerTable)
{
	S32 i;

	if (pCharacterPath == NULL || pchPowerTable == NULL || pchPowerTable[0] == '\0')
	{
		return NULL;
	}

	for (i = 0; i < eaSize(&pCharacterPath->eaSuggestedPurchases); i++)
	{
		if (stricmp(pCharacterPath->eaSuggestedPurchases[i]->pchPowerTable, pchPowerTable) == 0)
		{
			return pCharacterPath->eaSuggestedPurchases[i];
		}
	}

	return NULL;
}

bool Entity_EvalCharacterPathRequiresExpr(Entity *pEnt, CharacterPath *pPath)
{
	if(pPath->pExprRequires)
	{
		ExprContext *pContext = CharacterPath_GetContext();
		MultiVal mVal;

		exprContextSetSelfPtr(pContext,pEnt);
		exprContextSetPartition(pContext,entGetPartitionIdx(pEnt));
		exprContextSetPointerVar(pContext,"Player", pEnt, parse_Entity, false, true);

		exprEvaluate(pPath->pExprRequires ,pContext, &mVal);

		if(MultiValGetInt(&mVal,NULL) <= 0)
			return false;
	}
	return true;
}

AUTO_TRANS_HELPER;
bool character_trh_CanPickSecondaryPath(ATH_ARG NOCONST(Character)* pChar, CharacterPath* pPathToAdd, bool bObeyRequiredLevel)
{
	if (ISNULL(pChar) || ISNULL(pPathToAdd))
		return false;
	else
	{
		CharacterPathTypeInfo* pTypeInfo = (pPathToAdd && pPathToAdd->eType > kCharacterPathType_Primary) ? g_CharacterPathTypeStructs.eaTypes[pPathToAdd->eType] : NULL;
		int i;

		//if no typeinfo, it's a primary path.
		if (!pTypeInfo)
			return false;

		for (i = eaSize(&pPathToAdd->eaRequiredClasses) - 1; i >= 0; i--)
		{
			//check to make sure we're one of the allowed classes
			if (REF_COMPARE_HANDLES(pChar->hClass, pPathToAdd->eaRequiredClasses[i]->hClass))
				break;
		}

		if (i < 0)
			return false;

		if (pTypeInfo->iMaxNumberOwned > 0)
		{
			int iCount = 0;
			for (i = 0; i < eaSize(&pChar->ppSecondaryPaths); i++)
			{
				CharacterPath* pPath = GET_REF(pChar->ppSecondaryPaths[i]->hPath);
				if (pPath->eType == pPathToAdd->eType)
					iCount++;

				if (iCount >= pTypeInfo->iMaxNumberOwned)
					return false;
			}
		}

		if (bObeyRequiredLevel && pTypeInfo->iMinLevel > pChar->iLevelExp)
			return false;

		return true;
	}
}

// Populates the given array with the matching CharacterPath structures for the given power tree
void CharacterPath_GetCharacterPaths(CharacterPath ***peaCharacterPaths, const char *pchPowerTreeFilter, bool bIncludeDevRestricted, bool bPrimaryPathsOnly)
{
	// The character path retrieved from the iterator
	CharacterPath *pCharacterPath = NULL;

	// The dictionary iterator
	RefDictIterator itCharacterPath;

	devassert(peaCharacterPaths);

	if (peaCharacterPaths == NULL)
		return;

	// Initialize the iterator
	RefSystem_InitRefDictIterator(g_hCharacterPathDict, &itCharacterPath);

	// Iterate through all referents in the dictionary
	while(pCharacterPath = RefSystem_GetNextReferentFromIterator(&itCharacterPath))
	{
		// Check to see if we have a match or didn't filter
		if ((bIncludeDevRestricted || !pCharacterPath->bPlayerPathDevRestricted) &&
			(!pchPowerTreeFilter
			|| stricmp(REF_STRING_FROM_HANDLE(pCharacterPath->hPowerTree), pchPowerTreeFilter) == 0) &&
			(!bPrimaryPathsOnly || pCharacterPath->eType == kCharacterPathType_Primary))
		{
			// Add to the list
			eaPush(peaCharacterPaths, pCharacterPath);
		}
	}
}

void Entity_GetCharacterPaths(Entity *pEnt, CharacterPath ***peaCharacterPaths, const char *pchPowerTreeFilter)
{
	if(pEnt && pEnt->pChar && pEnt->pPlayer)
	{
		int i;
		CharacterPath_GetCharacterPaths(peaCharacterPaths, pchPowerTreeFilter, true, false);

		for(i=eaSize(peaCharacterPaths)-1; i>=0; i--)
		{
			if(!Entity_EvalCharacterPathRequiresExpr(pEnt, (*peaCharacterPaths)[i]))
			{
				eaRemove(peaCharacterPaths, i);
			}
		}
	}
}

PTNode *CharacterPath_GetChosenNode(Entity *pEnt, CharacterPathChoice *pChoice)
{
	int iNode;
	PTNodeDef *pNodeDef;
	PTNode *pOwnedNode = NULL;

	// Find the first power tree node the character can purchase
	for (iNode = 0; iNode < eaSize(&pChoice->eaSuggestedNodes); iNode++)
	{
		// Get the suggested node
		pNodeDef = GET_REF(pChoice->eaSuggestedNodes[iNode]->hNodeDef);

		if (pNodeDef == NULL)
			continue;

		pOwnedNode = powertree_FindNode(pEnt->pChar, NULL, pNodeDef->pchNameFull);
		if(pOwnedNode)
			return pOwnedNode;
	}

	return NULL;
}

// Returns the next suggested node from the given cost table
CharacterPathChoice * CharacterPath_GetNextChoiceFromCostTable(Entity *pEnt, CharacterPath* pCharacterPath, const char *pchCostTable, bool bIgnoreCanBuy)
{
	PTNode *pOwnedNode = NULL;
	CharacterPathSuggestedPurchase *pSuggestedPurchase = NULL;
	S32 iChoice;

	devassert(pEnt);
	devassert(pchCostTable && pchCostTable[0]);

	// Validate input
	if (pEnt == NULL || pCharacterPath == NULL || pEnt->pChar == NULL ||
		pchCostTable == NULL || pchCostTable[0] == '\0')
	{
		return NULL;
	}

	pSuggestedPurchase = CharacterPath_GetSuggestedPurchaseByPowerTable(pCharacterPath, pchCostTable);

	if (pSuggestedPurchase == NULL)
	{
		return NULL;
	}

	for(iChoice = 0; iChoice < eaSize(&pSuggestedPurchase->eaChoices); iChoice++)
	{
		CharacterPathChoice *pChoice = pSuggestedPurchase->eaChoices[iChoice];

		pOwnedNode = CharacterPath_GetChosenNode(pEnt, pChoice);
		if( pOwnedNode )
		{
			continue;
		}

		return pSuggestedPurchase->eaChoices[iChoice];
	}

	return NULL;
}

// Returns the next suggested node from the given cost table
PTNodeDef * CharacterPath_GetNextSuggestedNodeFromCostTable(int iPartitionIdx, Entity *pEnt, const char *pchCostTable, bool bIgnoreCanBuy)
{
	PowerTree *pOwnedPowerTree = NULL;
	PTNode *pOwnedNode = NULL;
	PTNodeDef *pSuggestedAndPurchasableNodeDef = NULL;
	PTNodeDef *pSuggestedNodeDef = NULL;
	CharacterPath **pPaths = NULL;
	CharacterPathSuggestedPurchase *pSuggestedPurchase = NULL;
	S32 iChoice, iNode;
	int i;

	devassert(pEnt);
	devassert(pchCostTable && pchCostTable[0]);

	// Validate input
	if (pEnt == NULL || pEnt->pChar == NULL ||
		pchCostTable == NULL || pchCostTable[0] == '\0')
	{
		return NULL;
	}

	eaStackCreate(&pPaths, eaSize(&pEnt->pChar->ppSecondaryPaths) + 1);
	entity_GetChosenCharacterPaths(pEnt, &pPaths);

	for (i = 0; i < eaSize(&pPaths); i++)
	{
		pSuggestedPurchase = CharacterPath_GetSuggestedPurchaseByPowerTable(pPaths[i], pchCostTable);

		if (pSuggestedPurchase == NULL)
		{
			return NULL;
		}

		for(iChoice = 0; iChoice < eaSize(&pSuggestedPurchase->eaChoices); iChoice++)
		{
			CharacterPathChoice *pChoice = pSuggestedPurchase->eaChoices[iChoice];

			if(eaSize(&pChoice->eaSuggestedNodes) > 0)
			{
				pOwnedNode = CharacterPath_GetChosenNode(pEnt, pChoice);
				if( pOwnedNode )
				{
					continue;
				}

				for(iNode = 0; iNode < eaSize(&pChoice->eaSuggestedNodes); iNode++)
				{
					pSuggestedNodeDef = GET_REF(pChoice->eaSuggestedNodes[iNode]->hNodeDef);
					if(!pSuggestedNodeDef)
						continue;

					// Can the character buy this node (or the function was called to ignore the can purchase rules)
					if (bIgnoreCanBuy 
						|| character_CanBuyPowerTreeNodeNextRank(iPartitionIdx, pEnt->pChar, powertree_GroupDefFromNodeDef(pSuggestedNodeDef), pSuggestedNodeDef))
					{
						pSuggestedAndPurchasableNodeDef = pSuggestedNodeDef;
						break;
					}
				}

				//If we're done in this loop too
				if(pSuggestedAndPurchasableNodeDef)
					break;
			}
			else
			{
				CharacterPathSuggestedNode *pSuggestedNode = pChoice->eaSuggestedNodes[0];
				// Get the suggested node
				pSuggestedNodeDef = GET_REF(pSuggestedNode->hNodeDef);

				if (pSuggestedNodeDef == NULL)
					continue;

				// Do we need a special rank check
				if (pSuggestedNode->iMaxRanksToBuy > 0)
				{
					// See if the character already owns this power
					pOwnedNode = powertree_FindNode(pEnt->pChar, &pOwnedPowerTree, pSuggestedNodeDef->pchNameFull);

					// If the character already has this power, check the rank to see if we want to still buy more ranks
					if (pOwnedNode && // Character owns the power
						pOwnedNode->iRank + 1 >= pSuggestedNode->iMaxRanksToBuy) // Character already has suggested number of ranks
					{
						// Skip this node as it has already the suggested rank
						continue;
					}
				}

				// Can the character buy this node (or the function was called to ignore the can purchase rules)
				if (bIgnoreCanBuy 
					|| character_CanBuyPowerTreeNodeNextRank(iPartitionIdx, pEnt->pChar, powertree_GroupDefFromNodeDef(pSuggestedNodeDef), pSuggestedNodeDef))
				{
					pSuggestedAndPurchasableNodeDef = pSuggestedNodeDef;
					break;
				}
			}
		}

		if (pSuggestedAndPurchasableNodeDef)
			break;
	}

	return pSuggestedAndPurchasableNodeDef;
}

// Returns if the given node is the next suggested node from the given cost table
bool CharacterPath_IsNextSuggestedNodeFromCostTable(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_STR const char *pchCostTable, SA_PARAM_NN_VALID PTNodeDef *pNodeDef)
{
	devassert(pNodeDef);

	if (pNodeDef == NULL)
	{
		return false;
	}

	return pNodeDef == CharacterPath_GetNextSuggestedNodeFromCostTable(iPartitionIdx, pEnt, pchCostTable, false);
}

// Returns the names of the suggested nodes from the given cost table in case of character creation
// Certain assumptions such as the player is level 1 and all level 1 powers have single ranks for this function
void CharacterPath_GetSuggestNodeNamesFromCostTableInCharacterCreation(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_STR const char *pchCharacterPathName, SA_PARAM_NN_STR const char *pchCostTable, SA_PARAM_NN_VALID char **pestrNodeNames)
{
	PTNodeDef *pSuggestedNodeDef = NULL;
	CharacterPath *pCharacterPath = NULL;
	CharacterPathSuggestedPurchase *pSuggestedPurchase = NULL;
	S32 i, iPointsGiven, iChoice, iNodeCount = 0;

	devassert(pEnt);
	devassert(pchCharacterPathName && pchCharacterPathName[0]);
	devassert(pchCostTable && pchCostTable[0]);
	devassert(pestrNodeNames);

	// Validate input
	if (pEnt == NULL ||
		pchCharacterPathName == NULL || pchCharacterPathName[0] == '\0' ||
		pchCostTable == NULL || pchCostTable[0] == '\0' ||
		pestrNodeNames == NULL)
	{
		return;
	}

	// Get the character path
	pCharacterPath = RefSystem_ReferentFromString(g_hCharacterPathDict, pchCharacterPathName);

	if (pCharacterPath == NULL)
	{
		return;
	}

	// Get the suggested purchase for the cost table
	pSuggestedPurchase = CharacterPath_GetSuggestedPurchaseByPowerTable(pCharacterPath, pchCostTable);

	// Get the number of points earned
	iPointsGiven = entity_PointsEarned(CONTAINER_NOCONST(Entity, pEnt), pchCostTable);

	if (pSuggestedPurchase == NULL || iPointsGiven <= 0)
	{
		return;
	}

	for(iChoice = 0; iChoice < eaSize(&pSuggestedPurchase->eaChoices) && iPointsGiven > 0; iChoice++)
	{
		CharacterPathChoice *pChoice = pSuggestedPurchase->eaChoices[iChoice];
	
		//Did I purchase something this choice?
		bool bPurchased = false;

		for(i=0; i<eaSize(&pChoice->eaSuggestedNodes) && !bPurchased; i++)
		{
			// Get the suggested node
			pSuggestedNodeDef = GET_REF(pChoice->eaSuggestedNodes[i]->hNodeDef);

			if (pSuggestedNodeDef == NULL)
				continue;

			// We are only interested in level 1 powers with single rank
			if (eaSize(&pSuggestedNodeDef->ppRanks) == 1 && 
				pSuggestedNodeDef->ppRanks[0]->pRequires->iTableLevel == 0)
			{
				PowerTreeDef *pTreeDef = powertree_TreeDefFromNodeDef(pSuggestedNodeDef);
				PowerTree *pTree = (PowerTree*)entity_FindPowerTreeHelper(CONTAINER_NOCONST(Entity, pEnt), pTreeDef);

				// Can the character buy this node regardless of points remaining			
				if (character_CanBuyPowerTreeNodeIgnorePointsRank(pEnt->pChar, pTree, powertree_GroupDefFromNodeDef(pSuggestedNodeDef), pSuggestedNodeDef, 0))
				{
					bPurchased = true;
					iPointsGiven -= entity_PowerTreeNodeRankCostHelper(CONTAINER_NOCONST(Character,pEnt->pChar), pSuggestedNodeDef, 0);

					// Add to the list of names
					if (iNodeCount > 0)
					{
						estrAppend2(pestrNodeNames, ", ");
					}
					estrAppend2(pestrNodeNames, TranslateMessageRef(pSuggestedNodeDef->pDisplayMessage.hMessage));

					iNodeCount++;
				}
			}
		}
	}
}

// Returns if the given node is the a suggested node from the given cost table in case of character creation
// Certain assumptions such as the player is level 1 and all level 1 powers have single ranks for this function
bool CharacterPath_IsSuggestedNodeFromCostTableInCharacterCreation(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_STR const char *pchCostTable, SA_PARAM_NN_VALID PTNodeDef *pNodeDef)
{
	PTNodeDef *pSuggestedNodeDef = NULL;
	CharacterPath *pCharacterPath = NULL;
	CharacterPathSuggestedPurchase *pSuggestedPurchase = NULL;
	S32 i, iChoice, iPointsGiven;

	devassert(pEnt);
	devassert(pchCostTable && pchCostTable[0]);

	// Validate input
	if (pEnt == NULL || pEnt->pChar == NULL ||
		pchCostTable == NULL || pchCostTable[0] == '\0')
	{
		return false;
	}

	// Get the PRIMARY character path, since this is character creation.
	pCharacterPath = entity_GetPrimaryCharacterPath(pEnt);

	if (pCharacterPath == NULL)
	{
		return false;
	}

	// Get the suggested purchase for the cost table
	pSuggestedPurchase = CharacterPath_GetSuggestedPurchaseByPowerTable(pCharacterPath, pchCostTable);

	// Get the number of points earned
	iPointsGiven = entity_PointsEarned(CONTAINER_NOCONST(Entity, pEnt), pchCostTable);

	if (pSuggestedPurchase == NULL || iPointsGiven <= 0)
	{
		return false;
	}

	for(iChoice = 0; iChoice < eaSize(&pSuggestedPurchase->eaChoices) && iPointsGiven > 0; iChoice++)
	{
		CharacterPathChoice *pChoice = pSuggestedPurchase->eaChoices[iChoice];

		//Did I purchase something this choice?
		bool bPurchased = false;

		for(i=0; i<eaSize(&pChoice->eaSuggestedNodes) && !bPurchased; i++)
		{
			// Get the suggested node
			pSuggestedNodeDef = GET_REF(pChoice->eaSuggestedNodes[i]->hNodeDef);
		
			if (pSuggestedNodeDef == NULL)
				continue;

			// We are only interested in level 1 powers with single rank
			if (eaSize(&pSuggestedNodeDef->ppRanks) == 1 && 
				pSuggestedNodeDef->ppRanks[0]->pRequires->iTableLevel == 0)
			{
				PowerTreeDef *pTreeDef = powertree_TreeDefFromNodeDef(pSuggestedNodeDef);
				PowerTree *pTree = (PowerTree*)entity_FindPowerTreeHelper(CONTAINER_NOCONST(Entity, pEnt), pTreeDef);

				// Can the character buy this node regardless of points remaining			
				if (character_CanBuyPowerTreeNodeIgnorePointsRank(pEnt->pChar, pTree, powertree_GroupDefFromNodeDef(pSuggestedNodeDef), pSuggestedNodeDef, 0))
				{
					bPurchased = true;
					iPointsGiven -= entity_PowerTreeNodeRankCostHelper(CONTAINER_NOCONST(Character, pEnt->pChar), pSuggestedNodeDef, 0);

					if (pNodeDef == pSuggestedNodeDef)
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

//  Return: 1 if the Entity's Character is the named class, otherwise 0
AUTO_EXPR_FUNC(entityutil);
int EntIsClass(SA_PARAM_OP_VALID Entity *entity,
			   ACMD_EXPR_DICT(CharacterClass) const char *className)
{
	int r = 0;
	if(entity && entity->pChar && IS_HANDLE_ACTIVE(entity->pChar->hClass))
	{
		r = !stricmp(REF_STRING_FROM_HANDLE(entity->pChar->hClass),className);
	}
	return r;
}

AUTO_TRANS_HELPER;
bool CharacterPath_trh_CanUseEx(ATH_ARG NOCONST(GameAccountData) *pData, CharacterPath *pCharPath, const char *pKey)
{
	if(pKey && NONNULL(pData) && pCharPath)
	{
		char *estrBuffer = NULL;
		bool ret;

		GenerateGameTokenKey(&estrBuffer,
			kGameToken_PowerSet,
			pKey,
			pCharPath->pcGamePermissionValue);

		ret = eaIndexedGetUsingString(&pData->eaTokens, estrBuffer) != NULL;
		estrDestroy(&estrBuffer);

		return ret;

	}

	return false;
}

// Ent will be most likely be NULL on the app server and at login. This is ok as the app server will use the login link and the client will use possible character choices to get
// the game account data
// These are the non-expression versions that can take a NULL ent. The character path must have the field pcGamePermissionValue set
// missing character path means this is a free-form character and will always return true. Use of this type is gated through other expressions
AUTO_TRANS_HELPER;
bool CharacterPath_trh_CanUse(ATH_ARG NOCONST(GameAccountData) *pData, CharacterPath *pCharPath)
{
	// Get the game account data. On app server it will use login link, on client it will use 
	if(pCharPath)
	{
		if(!pCharPath->pExprRequires)
		{
			// no require 
			return true;
		}
		if(NONNULL(pData))
		{
			bool ret = CharacterPath_trh_CanUseEx(pData, pCharPath, GAME_PERMISSION_OWNED);
			if(ret)
			{
				return true;
			}

			ret = CharacterPath_trh_CanUseEx(pData, pCharPath, GAME_PERMISSION_FREE);

			return ret;

		}
		return false;	// no game account data or it doesn't have permission
	}
	return true;
}

AUTO_TRANS_HELPER;
bool CharacterPath_trh_FromNameCanUse(ATH_ARG NOCONST(GameAccountData) *pData, const char *pcCharPathName)
{
	if(pcCharPathName)
	{
		CharacterPath *pCharacterPath = RefSystem_ReferentFromString(g_hCharacterPathDict, pcCharPathName);
		if(pCharacterPath)
		{
			return CharacterPath_trh_CanUse(pData, pCharacterPath);
		}
	}

	return true;
}

AUTO_TRANS_HELPER;
bool CharacterPath_trh_FromNameHasKey(ATH_ARG NOCONST(GameAccountData) *pData, const char *pcCharPathName, const char *pKey)
{
	if(pcCharPathName && pKey)
	{
		CharacterPath *pCharacterPath = RefSystem_ReferentFromString(g_hCharacterPathDict, pcCharPathName);
		if(pCharacterPath)
		{
			return CharacterPath_trh_CanUseEx(pData, pCharacterPath, pKey);
		}
	}

	return true;
}

// Is this a free characterpath but not owned?
bool CharacterPath_FreeNotOwned(GameAccountData *pData, const CharacterPath *pPath)
{
	if(pData && pPath &&	
		pPath->pExprRequires &&
		GamePermission_HasTokenKeyType(pData, kGameToken_PowerSet, GAME_PERMISSION_FREE, pPath->pcGamePermissionValue) &&
		!GamePermission_HasTokenKeyType(pData, kGameToken_PowerSet, GAME_PERMISSION_OWNED, pPath->pcGamePermissionValue))
	{
		return true;
	}

	return false;
}

//  return true if this is a usable character class, uses pcGamePermissionValue field of CharacterPath
// NULL is a valid character path name (free form) and will always return true
AUTO_EXPR_FUNC(Entityutil) ACMD_NAME(CharacterPathFromNameCanUse);
bool CharacterPath_FromNameCanUse(SA_PARAM_OP_VALID Entity *pEntity,	const char *className)
{
	GameAccountData *pData = entity_GetGameAccount(pEntity);
	return CharacterPath_trh_FromNameCanUse(CONTAINER_NOCONST(GameAccountData, pData), className);
}

//Debug commands to disable new "stancewords on classes" feature

extern bool g_bDisableClassStances;

AUTO_CMD_INT(g_bDisableClassStances, ServerDisableClassStances) ACMD_ACCESSLEVEL(9) ACMD_HIDE ACMD_SERVERONLY;

void cmd_DisableClassStancesCB()
{
	char tmp[32];
	sprintf(tmp, "ServerDisableClassStances %i", g_bDisableClassStances);
	globCmdParse(tmp);
}

AUTO_CMD_INT(g_bDisableClassStances, DisableClassStances) ACMD_ACCESSLEVEL(9)  ACMD_CLIENTONLY ACMD_CALLBACK(cmd_DisableClassStancesCB);


// The autogen for this is in the product-specific CharacterAttribs.c file
