/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "PowerVars.h"

#include "error.h"
#include "estring.h"
#include "Expression.h"
#include "fileutil.h"
#include "foldercache.h"
#include "GameBranch.h"
#include "timing.h"

#include "Character.h"
#include "CharacterClass.h"
#include "PowerTree.h"
#include "StringUtil.h"

#include "AutoGen/PowerVars_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Internal arrays of all power vars, tables and stats
PowerVars g_PowerVars;
PowerTables g_PowerTables;

// Define this for external use for now
PowerStats g_PowerStats;




// Returns the named variable, or NULL if the variable doesn't exist
MultiVal *powervar_Find(const char *pchName)
{
	PowerVar *pvar = NULL;
	stashFindPointer(g_PowerVars.stPowerVars,pchName,&pvar);
	if(pvar)
	{
		return &pvar->mvValue;
	}
	else
	{
		ErrorDetailsf("%s",pchName);
		Errorf("Bad power var");
		return NULL;
	}
}


// Returns the named power table
AUTO_TRANS_HELPER_SIMPLE;
PowerTable *powertable_Find(const char *pchName)
{
	PowerTable *ptable = NULL;
//	int i;
	stashFindPointer(g_PowerTables.stPowerTables,pchName,&ptable);
/*
	if(!ptable)
	{
		for(i=0;i<eaSize(&g_PowerTables.ppPowerTables);i++)
		{
			if(!stricmp(pchName,g_PowerTables.ppPowerTables[i]->pchName))
				return g_PowerTables.ppPowerTables[i];
		}
		return NULL;
	}
*/
	return ptable;
}

// Returns the sum of values from iMin to iMax in the named table, or 0 if the table doesn't exist.
//  If the table is a multi-table, it will recurse up to one level to find the proper table.
F32 powertable_SumMulti(const char *pchName, S32 iMin, S32 iMax, S32 idxMulti)
{
	F32 fRet = 0.f;
	S32 idx = 0;
	PowerTable *ptable = powertable_Find(pchName);
	if(!ptable)
	{
		if (pchName) 
		{
			ErrorDetailsf("Table %s", pchName);
			Errorf("powertable_LookupMulti: Unknown PowerTable");
		} 
		else if (isDevelopmentMode())
		{
			ErrorfForceCallstack("powertable_LookupMulti: No PowerTable Specified");
		}
	}
	else
	{
		int s = eaSize(&ptable->ppchTables);
		// Check if this is a multi-table
		if(s)
		{
			if(verify(s > idxMulti && idxMulti >= 0 && ptable->ppchTables[idxMulti]))
			{
				// Fetch the actual table
				ptable = powertable_Find(ptable->ppchTables[idxMulti]);
			}
		}

		if(verify(ptable) && (s = eafSize(&ptable->pfValues)))
		{
			iMin = max(iMin, 0);
			iMax = min(iMax, s);
			for (idx = iMin; idx < iMax; idx++)
			{
				// If the request index is larger than the table, return the last element.  This isn't considered
				// a bug.
				fRet += ptable->pfValues[idx];
			}
		}
	}
	return fRet;
}

// Returns the best value in the named table, or 0 if the table doesn't exist.
//  If the table is a multi-table, it will recurse up to one level to find the proper table.
F32 powertable_LookupMulti(const char *pchName, S32 idx, S32 idxMulti)
{
	F32 fRet = 0.f;
	PowerTable *ptable = powertable_Find(pchName);
	if(!ptable)
	{
		if (pchName) 
		{
			ErrorDetailsf("Table %s", pchName);
			Errorf("powertable_LookupMulti: Unknown PowerTable");
		} 
		else if (isDevelopmentMode())
		{
			ErrorfForceCallstack("powertable_LookupMulti: No PowerTable Specified");
		}
	}
	else
	{
		int s = eaSize(&ptable->ppchTables);
		// Check if this is a multi-table
		if(s)
		{
			if(verify(s > idxMulti && idxMulti >= 0 && ptable->ppchTables[idxMulti]))
			{
				// Fetch the actual table
				ptable = powertable_Find(ptable->ppchTables[idxMulti]);
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
				ErrorDetailsf("Table %s, Index %d",pchName,idx);
				ErrorfForceCallstack("THIS IS SAFE TO IGNORE: Jered just needs the call stack: Table lookup below 0"); // Temporary assert so I can get a call stack and debug it
				fRet = ptable->pfValues[0];
			}
		}
	}
	return fRet;
}

// Returns the value in the named table, or 0 if the table or index doesn't exist.
//  If the table is a multi-table, it uses the 0th subtable.
F32 powertable_Lookup(const char *pchName, S32 idx)
{
	return powertable_LookupMulti(pchName,idx,0);
}

// Validates that a PowerTable is properly constructed
S32 powertable_Validate(PowerTable *pTable, const char *cpchFile)
{
	S32 bValid = false;
	if(eaSize(&pTable->ppchTables))
	{
		bValid = true;
	}
	else if(eafSize(&pTable->pfValues))
	{
		bValid = true;
	}
	else
	{
		ErrorFilenamef(cpchFile ? cpchFile : pTable->cpchFile,"%s %s must contain either an array of values or an array of table names","PowerTable",pTable->pchName);
	}
	return bValid;
}

// Does load-time generation of derived data in a PowerTable
void powertable_Generate(PowerTable *pTable)
{
	int i, s = eafSize(&pTable->pfValues);
	pTable->bValuesNegative = pTable->bValuesZero = false;
	for(i=0; i<s; i++)
	{
		pTable->bValuesNegative |= (pTable->pfValues[i]<0);
		pTable->bValuesZero |= (pTable->pfValues[i]==0);
	}
}

// Compresses a PowerTable
void powertable_Compress(PowerTable *pTable)
{
	int i, s = eafSize(&pTable->pfValues);
	for(i=s-1; i>=1; i--)
	{
		if(pTable->pfValues[i]!=pTable->pfValues[i-1])
		{
			break;
		}
	}
	// i is the last index we need, everything past that was identical
	if(i>=0 && (i+1)!=s)
	{
		eafSetCapacity(&pTable->pfValues,i+1);
	}
}



/***** Stat Evaluation *****/

static F32 s_fEvalStatValue;
AUTO_EXPR_FUNC(exprFuncListPowerStats) ACMD_NAME(Stat);
F32 Stat()
{
	return s_fEvalStatValue;
}

static F32 s_fEvalStatMaxValue;
AUTO_EXPR_FUNC(exprFuncListPowerStats);
F32 StatMax()
{
	return s_fEvalStatMaxValue;
}

static S32 s_iEvalCharacterLevel;
AUTO_EXPR_FUNC(exprFuncListPowerStats);
S32 CharLevel()
{
	return s_iEvalCharacterLevel;
}

static S32 s_iEvalNodeRank;
AUTO_EXPR_FUNC(exprFuncListPowerStats);
S32 StatNodeRank()
{
	return s_iEvalNodeRank;
}

// Walks an assumed monotonically increasing table, looking for the first index that the parameter is
//  less than.  Return index is 1-based.
static CharacterClass *s_pEvalCharacterClass;
AUTO_EXPR_FUNC(exprFuncListPowerStats) ACMD_NAME(StatTable);
ExprFuncReturnVal TableIndexLT(ACMD_EXPR_INT_OUT piRet, const char* pchName, F32 fValue)
{
	ExprFuncReturnVal r = ExprFuncReturnFinished;

	PowerTable *pTable = s_pEvalCharacterClass ? powertable_FindInClass(pchName,s_pEvalCharacterClass) : powertable_Find(pchName);
	if(verify(pTable))
	{
		int i;
		for(i=0; i<eafSize(&pTable->pfValues); i++)
		{
			if(fValue<pTable->pfValues[i]) break;
		}
		*piRet = i;
	}
	else
	{
		r = ExprFuncReturnError;
	}
	return r;
}

AUTO_EXPR_FUNC(exprFuncListPowerStats);
bool IsCharClass(const char *pchClassName)
{
	if (s_pEvalCharacterClass == NULL)
	{
		false;
	}
	return stricmp_safe(pchClassName, s_pEvalCharacterClass->pchName) == 0;
}

static ExprContext* PowerStatContext(void)
{
	static ExprContext* s_pContext = NULL;
	if(!s_pContext)
	{
		ExprFuncTable* stTable;

		s_pContext = exprContextCreate();

		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable,"CEFuncsGeneric");
		exprContextAddFuncsToTableByTag(stTable,"exprFuncListPowerStats");
		exprContextAddFuncsToTableByTag(stTable,"util");
		exprContextSetFuncTable(s_pContext,stTable);
	}
	return s_pContext;
}

// Returns the output value of the stat, given the input stats, character level, and optional node rank
F32 powerstat_Eval(PowerStat *pStat,
				   CharacterClass *pClass,
				   F32 *pfStats,
				   S32 iLevel,
				   S32 iNodeRank)
{
	MultiVal mv;
	bool b;

	s_pEvalCharacterClass = pClass;
	if(eaSize(&pStat->ppSourceStats))
	{
		int i;
		//reset the value
		s_fEvalStatValue = 0;
		s_fEvalStatMaxValue = 0;
		for(i=eaSize(&pStat->ppSourceStats)-1; i>=0; i--)
		{
			s_fEvalStatValue += pfStats[i] * pStat->ppSourceStats[i]->fMultiplier;
			MAX1(s_fEvalStatMaxValue,pfStats[i]);
		}
	}
	else
	{
		s_fEvalStatValue = s_fEvalStatMaxValue = pfStats[0];
	}
	s_iEvalCharacterLevel = iLevel;
	s_iEvalNodeRank = iNodeRank;
	exprEvaluate(pStat->expr,PowerStatContext(),&mv);
	return MultiValGetFloat(&mv,&b);
}

// Returns if the PowerStat would be active for the Character
//  Does not check if the Character's actually has access to the PowerStat
//  Returns the PowerStat's required PTNode if ppNodeOut is provided
S32 powerstat_Active(PowerStat *pStat,
						Character *pchar,
						PTNode **ppNodeOut)
{
	S32 bHasRequirements = false;
	if(pStat->bInactive)
		return false;

	//Passing the category required check doesn't mean instant success. Still check the next requirements on the power stat
	if(pStat->eClassCategoryRequired)
	{
		CharacterClass *pClass = character_GetClassCurrent(pchar);

		if(pchar->bBecomeCritter || !pClass || pClass->eCategory != pStat->eClassCategoryRequired)
		{
			return false;
		}
	}

	if(pStat->pchPowerTreeNodeRequired)
	{
		if(!pchar->bBecomeCritter) // Node check isn't allowed when you're in BeCritter
		{
			PTNode *pnode = powertree_FindNode(pchar,NULL,pStat->pchPowerTreeNodeRequired);
			if(ppNodeOut)
				*ppNodeOut = pnode;
			
			if(pnode) // Don't need to check further if we found the node
				return true;
		}

		bHasRequirements = true;
	}

	if(pStat->pchPowerDefRequired)
	{
		PowerDef *pdefRequired = powerdef_Find(pStat->pchPowerDefRequired);
		Power *ppow = pdefRequired ? character_FindPowerByDef(pchar,pdefRequired) : NULL;
		if(ppow && (!pchar->bBecomeCritter || ppow->eSource==kPowerSource_AttribMod))
			return true;

		bHasRequirements = true;
	}

	return !bHasRequirements;
}

// Returns true if the attribs that define the stats have changed
bool powerstats_CheckDirty(PowerStat **ppStats, CharacterAttribs *pOldAttribs, CharacterAttribs *pNewAttribs)
{
	int i, j, s;
	char *pchTrickOld = (char*)pOldAttribs;
	char *pchTrickNew = (char*)pNewAttribs;

	s = eaSize(&ppStats);
	if(s<=0)
		return false;

	PERFINFO_AUTO_START_FUNC();

	for(i=s-1; i>=0; i--)
	{
		PowerStat *pStat = ppStats[i];
		F32 *pfOld, *pfNew;
		
		if(pStat->bInactive)
			continue;
		
		// If there aren't any source stats then we're probably working with
		//   a numeric item. The inventory knows to invalidate the powerstats
		//   when numerics are changed.
		if(!eaSize(&pStat->ppSourceStats))
			continue;

		for(j = eaSize(&pStat->ppSourceStats)-1; j >= 0; j--)
		{
			pfOld = (F32*)(pchTrickOld + pStat->ppSourceStats[j]->offSourceAttrib);
			pfNew = (F32*)(pchTrickNew + pStat->ppSourceStats[j]->offSourceAttrib);
		
			if(*pfOld!=*pfNew)
				break;
		}
		if(j>=0)
			break;
	}

	PERFINFO_AUTO_STOP();

	return i>=0;
}


/***** Utility *****/

// Fills the given char* earray with the names of the power tables
void powertables_FillNameEArray(const char ***pppchNames)
{
	int i;
	for(i=eaSize(&g_PowerTables.ppPowerTables)-1; i>=0; i--)
	{
		eaPush(pppchNames,g_PowerTables.ppPowerTables[i]->pchName);
	}
}


// Fills the given char* earray with alloc'd  names of the power tables
void powertables_FillAllocdNameEArray(const char ***pppchNames)
{
	int i;
	for(i=eaSize(&g_PowerTables.ppPowerTables)-1; i>=0; i--)
	{
		eaPush(pppchNames,strdup(g_PowerTables.ppPowerTables[i]->pchName));
	}
}



/***** Load Fixup Functions *****/

AUTO_FIXUPFUNC;
TextParserResult powervars_Fixup(PowerVars *pVars, enumTextParserFixupType eFixupType, void *pExtraData)
{
	int i;
	bool r = true;
	switch (eFixupType)
	{
	case FIXUPTYPE_POST_BINNING_DURING_LOADFILES:	// Intentional fallthrough
	case FIXUPTYPE_POST_LOAD_DURING_LOADFILES_FINAL_LOCATION:
		if(!g_PowerVars.stPowerVars)
			g_PowerVars.stPowerVars = stashTableCreateWithStringKeys(16,StashDefault);
		for(i=eaSize(&pVars->ppPowerVars)-1; i>=0; i--)
		{
			r = stashAddPointer(g_PowerVars.stPowerVars,pVars->ppPowerVars[i]->pchName,pVars->ppPowerVars[i],true)
				&& r;
		}
		break;
	}

	// We've 'inflated' the stash table prior to putting it in shared memory.  Now empty it without
	//  deflating so it doesn't have a bunch of bad pointers
	if(eFixupType==FIXUPTYPE_POST_BINNING_DURING_LOADFILES)
	{
		stashTableClear(g_PowerVars.stPowerVars);
	}

	return r;
}

AUTO_FIXUPFUNC;
TextParserResult powertables_Fixup(PowerTables *pTables, enumTextParserFixupType eFixupType, void *pExtraData)
{
	int i;
	bool r = true;
	switch (eFixupType)
	{
	case FIXUPTYPE_POST_BINNING_DURING_LOADFILES:	// Intentional fallthrough
	case FIXUPTYPE_POST_LOAD_DURING_LOADFILES_FINAL_LOCATION:
		
		if(!g_PowerTables.stPowerTables)
			g_PowerTables.stPowerTables = stashTableCreateWithStringKeys(16,StashDefault);

		g_PowerTables.bBasicFactBonusHitPointsMax = false;

		for(i=eaSize(&pTables->ppPowerTables)-1; i>=0; i--)
		{
			if(!stricmp(pTables->ppPowerTables[i]->pchName,POWERTABLE_BASICFACTBONUSHITPOINTSMAX))
				g_PowerTables.bBasicFactBonusHitPointsMax = true;

			powertable_Compress(pTables->ppPowerTables[i]);
			powertable_Generate(pTables->ppPowerTables[i]);
			r = powertable_Validate(pTables->ppPowerTables[i],NULL)
				&& stashAddPointer(g_PowerTables.stPowerTables,pTables->ppPowerTables[i]->pchName,pTables->ppPowerTables[i],true)
				&& r;
		}
		break;
	}

	// We've 'inflated' the stash table prior to putting it in shared memory.  Now empty it without
	//  deflating so it doesn't have a bunch of bad pointers
	if(eFixupType==FIXUPTYPE_POST_BINNING_DURING_LOADFILES)
	{
		stashTableClear(g_PowerTables.stPowerTables);
	}

	return r;
}

AUTO_FIXUPFUNC;
TextParserResult powerstat_Fixup(PowerStat *pStat, enumTextParserFixupType eFixupType, void *pExtraData)
{
	bool r = true;

	switch (eFixupType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		r = exprGenerate(pStat->expr,PowerStatContext());
		break;
	}

	return r;
}


/***** Reload Functions *****/

// Some aspect of the list of power vars was changed, handle that yo!
static int powervar_Reload_SubStructCallback(void *pStruct, void *pOldStruct, ParseTable *pTPI, eParseReloadCallbackType eType)
{
	if(eType==eParseReloadCallbackType_Add)
	{
		// Added a power var, add it to stash table
		stashAddPointer(g_PowerVars.stPowerVars,((PowerVar *)pStruct)->pchName,pStruct,0);
	}
	else if(eType==eParseReloadCallbackType_Delete)
	{
		// Removed a power var, remove it from the stash table
		stashRemovePointer(g_PowerVars.stPowerVars,((PowerVar *)pStruct)->pchName,NULL);
	}
	else if(eType==eParseReloadCallbackType_Update)
	{
		// Nada
	}
	return 1;
}

// Reload power vars top level callback
static void powervar_Reload(const char *pchRelPath, int UNUSED_when)
{
	// Yes, this leaks the array.  Tough luck.
	loadstart_printf("Reloading power vars...");
	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);
	if(!ParserReloadFile(pchRelPath, parse_PowerVars, &g_PowerVars, powervar_Reload_SubStructCallback, 0))
	{
		// Something went wrong, how do we handle that?
	}
	loadend_printf(" done (%d vars)", eaSize(&g_PowerVars.ppPowerVars));
}

// Some aspect of the list of power tables was changed, handle that yo!
static int powertable_Reload_SubStructCallback(void *pStruct, void *pOldStruct, ParseTable *pTPI, eParseReloadCallbackType eType)
{
	if(eType==eParseReloadCallbackType_Add)
	{
		// Added a power table, add it to stash table
		stashAddPointer(g_PowerTables.stPowerTables,((PowerTable *)pStruct)->pchName,pStruct,0);
	}
	else if(eType==eParseReloadCallbackType_Delete)
	{
		// Removed a power table, remove it from the stash table
		stashRemovePointer(g_PowerTables.stPowerTables,((PowerTable *)pStruct)->pchName,NULL);
	}
	else if(eType==eParseReloadCallbackType_Update)
	{
		// Nada
	}
	return 1;
}

// Reload power tables top level callback
static void powertable_Reload(const char *pchRelPath, int UNUSED_when)
{
	// Yes, this leaks the array.  Tough luck.
	loadstart_printf("Reloading power tables...");
	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);
	if(!ParserReloadFile(pchRelPath, parse_PowerTables, &g_PowerTables, powertable_Reload_SubStructCallback, 0))
	{
		// Something went wrong, how do we handle that?
	}
	loadend_printf(" done (%d tables)", eaSize(&g_PowerTables.ppPowerTables));
}



/***** Reload Power Stats *****/

// Some aspect of the list of powerstats was changed, handle that yo!
static int powerstat_Reload_SubStructCallback(void *pStruct, void *pOldStruct, ParseTable *pTPI, eParseReloadCallbackType eType)
{
	if(eType==eParseReloadCallbackType_Add)
	{
		// TODO(JW): Added a powerstat
	}
	else if(eType==eParseReloadCallbackType_Delete)
	{
		// TODO(JW): Removed a powerstat
	}
	else if(eType==eParseReloadCallbackType_Update)
	{
		// TODO(JW): Updated a powerstat
	}
	return 1;
}

static void powerstats_Validate(PowerStats *pPowerStats)
{
	int i, s = eaSize(&pPowerStats->ppPowerStats);
	for(i = 0; i<s; i++)
	{
		int j;
		PowerStat *pStat = pPowerStats->ppPowerStats[i];
		for(j = eaSize(&pStat->ppSourceStats)-1;j >= 0; j--)
		{
			if(pStat->ppSourceStats[j]->fMultiplier <= 0.0f)
			{
				const char* pchAttribName = StaticDefineIntRevLookup(AttribTypeEnum, pStat->ppSourceStats[j]->offSourceAttrib);
				loadupdate_printf("PowerStat %s attrib %s has invalid multiplier %f.",
					pStat->pchName,
					pchAttribName ? pchAttribName : "Unknown",
					pStat->ppSourceStats[j]->fMultiplier);
				pStat->bInactive = true;
			}
		}

		if(!eaSize(&pStat->ppSourceStats) && !pStat->pchSourceNumericItem)
		{
			loadupdate_printf("PowerStat %s specifies no source stats or numeric items!  Marking it as invalid",
				pStat->pchName);
			pStat->bInactive = true;
		}
	}
}

// Reload power stats top level callback
static void powerstat_Reload(const char *pchRelPath, int UNUSED_when)
{
	// Yes, this leaks the array.  Tough luck.
	loadstart_printf("Reloading power stats...");
	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);
	if(!ParserReloadFile(pchRelPath, parse_PowerStats, &g_PowerStats, powerstat_Reload_SubStructCallback, 0))
	{
		// Something went wrong, how do we handle that?
	}

	powerstats_Validate(&g_PowerStats);
	loadend_printf(" done (%d stats)", eaSize(&g_PowerStats.ppPowerStats));
}



/***** Load Power Vars, Tables and Stats *****/

// Loads all power vars, tables and stats, and sets up reload callbacks
AUTO_STARTUP(PowerVars) ASTRT_DEPS(AS_AttribSets AS_CharacterClassTypes);
void PowerVarsLoad(void)
{
	char *pSharedMemoryName = NULL;	
	char *pPowerVarsDir = NULL;
	char *pBuffer = NULL;

	switch( GetAppGlobalType() ) {
		case GLOBALTYPE_UGCSEARCHMANAGER: case GLOBALTYPE_UGCDATAMANAGER:
			return;
	}
	
	GameBranch_GetDirectory(&pPowerVarsDir, "defs/powervars");
	
	loadstart_printf("Loading power variables...");

	// Variables
	MakeSharedMemoryName(GameBranch_GetFilename(&pBuffer, "PowerVars"), &pSharedMemoryName);
	ParserLoadFilesShared(	pSharedMemoryName,
							pPowerVarsDir,
							".powervar",
							GameBranch_GetFilename(&pBuffer, "PowerVars.bin"),
							PARSER_OPTIONALFLAG, parse_PowerVars, &g_PowerVars);

	// Tables
	MakeSharedMemoryName(GameBranch_GetFilename(&pBuffer, "PowerTables"), &pSharedMemoryName);
	ParserLoadFilesShared(	pSharedMemoryName,
							pPowerVarsDir,
							".powertable",
							GameBranch_GetFilename(&pBuffer, "PowerTables.bin"),
							PARSER_OPTIONALFLAG, parse_PowerTables, &g_PowerTables);

	// Stats
	MakeSharedMemoryName(GameBranch_GetFilename(&pBuffer, "PowerStats"), &pSharedMemoryName);
	ParserLoadFilesShared(	pSharedMemoryName,
							pPowerVarsDir,
							".powerstat",
							GameBranch_GetFilename(&pBuffer, "PowerStats.bin"),
							PARSER_OPTIONALFLAG, parse_PowerStats, &g_PowerStats);
	powerstats_Validate(&g_PowerStats);

	estrDestroy(&pSharedMemoryName);
	
	// Reload callbacks
	estrPrintf(&pBuffer, "%s/*.powervar",
		pPowerVarsDir);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, pBuffer, powervar_Reload);
		
	estrPrintf(&pBuffer, "%s/*.powertable",
		pPowerVarsDir);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, pBuffer, powertable_Reload);
		
	estrPrintf(&pBuffer, "%s/*.powerstat",
		pPowerVarsDir);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, pBuffer, powerstat_Reload);

	estrDestroy(&pBuffer);
	estrDestroy(&pPowerVarsDir);

	loadend_printf(" done (%d vars, %d tables, %d stats).", eaSize(&g_PowerVars.ppPowerVars), eaSize(&g_PowerTables.ppPowerTables), eaSize(&g_PowerStats.ppPowerStats));
}

#include "AutoGen/PowerVars_h_ast.c"
