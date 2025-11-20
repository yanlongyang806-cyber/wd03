/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "PowerSubtarget.h"

#include "Entity.h"
#include "error.h"
#include "EString.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "MemoryPool.h"
#include "ResourceManager.h"
#include "timing.h"

#include "Character.h"
#include "itemCommon.h"

#include "AutoGen/PowerSubtarget_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_hPowerSubtargetCategoryDict;
DictionaryHandle g_hPowerSubtargetDict;

S32 g_bPowerSubtargets = false;

MP_DEFINE(PowerSubtargetChoice);


// Loading

static void PowerSubtargetCategoryReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading PowerSubtargetCategories...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionary(pchRelPath,g_hPowerSubtargetCategoryDict);

	loadend_printf(" done (%d PowerSubtargetCategories)", RefSystem_GetDictionaryNumberOfReferents(g_hPowerSubtargetCategoryDict));
}

static void PowerSubtargetCategoryLoad(void)
{
	resLoadResourcesFromDisk(g_hPowerSubtargetCategoryDict, NULL, "defs/config/PowerSubtargetCategories.def", "PowerSubtargetCategories.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/PowerSubtargetCategories.def", PowerSubtargetCategoryReload);
	}

	g_bPowerSubtargets |= RefSystem_GetDictionaryNumberOfReferents(g_hPowerSubtargetCategoryDict);
}

static int PowerSubtargetValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, void *pResource, U32 userID)
{
	PowerSubtarget *pSubtarget = pResource;
	switch(eType)
	{	
	case RESVALIDATE_POST_BINNING:
		{
			if(!GET_REF(pSubtarget->hCategory))
			{
				ErrorFilenamef(pSubtarget->cpchFile,"PowerSubtarget %s must have a valid Category, currently %s",pSubtarget->cpchName,REF_STRING_FROM_HANDLE(pSubtarget->hCategory));
			}
		}
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

static void PowerSubtargetReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading PowerSubtargets...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionary(pchRelPath,g_hPowerSubtargetDict);

	loadend_printf(" done (%d PowerSubtargets)", RefSystem_GetDictionaryNumberOfReferents(g_hPowerSubtargetDict));
}

AUTO_STARTUP(PowerSubtarget);
void PowerSubtargetLoad(void)
{
	PowerSubtargetCategoryLoad();

	resLoadResourcesFromDisk(g_hPowerSubtargetDict, NULL, "defs/config/PowerSubtargets.def", "PowerSubtargets.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/PowerSubtargets.def", PowerSubtargetReload);
	}

	g_bPowerSubtargets |= RefSystem_GetDictionaryNumberOfReferents(g_hPowerSubtargetDict);
}



// Init

AUTO_RUN;
void InitPowerSubtarget(void)
{
	// Create memory pools
	MP_CREATE(PowerSubtargetChoice,25);

	// Set up reference dictionaries
	g_hPowerSubtargetDict = RefSystem_RegisterSelfDefiningDictionary("PowerSubtarget", false, parse_PowerSubtarget, true, true, NULL);
	g_hPowerSubtargetCategoryDict = RefSystem_RegisterSelfDefiningDictionary("PowerSubtargetCategory", false, parse_PowerSubtargetCategory, true, true, NULL);

	resDictManageValidation(g_hPowerSubtargetDict, PowerSubtargetValidateCB);
	if (isDevelopmentMode() || isProductionEditMode()) {
		resDictMaintainInfoIndex(g_hPowerSubtargetDict, NULL, NULL, NULL, NULL, NULL);
	}
}




// Clears out the Character's subtarget choice
void character_ClearSubtarget(Character *pchar)
{
	StructDestroySafe(parse_PowerSubtargetChoice,&pchar->pSubtarget);
	entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
}

// Sets the Character's subtarget choice to the PowerSubtargetCategory
void character_SetSubtargetCategory(Character *pchar, PowerSubtargetCategory *pcat)
{
	character_ClearSubtarget(pchar);
	pchar->pSubtarget = StructCreate(parse_PowerSubtargetChoice);
	entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
	SET_HANDLE_FROM_REFERENT(g_hPowerSubtargetCategoryDict,pcat,pchar->pSubtarget->hCategory);
}

// Updates the Character's PowerSubtargetNet data
void character_SubtargetUpdateNet(Character *pchar, GameAccountDataExtract *pExtract)
{
	static S32 *s_piHealthOld = NULL;
	static S32 *s_piHealthMaxOld = NULL;

	int i,s,iSizeOld,bDirty;
	
	PERFINFO_AUTO_START_FUNC();

	iSizeOld = s = eaSize(&pchar->ppSubtargets);
	eaiSetSize(&s_piHealthOld,iSizeOld);
	eaiSetSize(&s_piHealthMaxOld,iSizeOld);
	
	// Save and reset health of everything in the list
	for(i=s-1; i>=0; i--)
	{
		s_piHealthOld[i] = pchar->ppSubtargets[i]->iHealth;
		s_piHealthMaxOld[i] = pchar->ppSubtargets[i]->iHealthMax;
		pchar->ppSubtargets[i]->iHealth = pchar->ppSubtargets[i]->iHealthMax = 0;
	}


	// Check to see if size changed
	s = eaSize(&pchar->ppSubtargets);
	bDirty = (iSizeOld!=s);

	// Remove Subtargets with 0 max health, and check if health or health max changed on old ones
	for(i=s-1; i>=0; i--)
	{
		if(!pchar->ppSubtargets[i]->iHealthMax)
		{
			StructDestroy(parse_PowerSubtargetNet,pchar->ppSubtargets[i]);
			eaRemoveFast(&pchar->ppSubtargets,i);
			bDirty = true;
		}
		else if(!bDirty && i<iSizeOld)
		{
			if(pchar->ppSubtargets[i]->iHealth!=s_piHealthOld[i]
				|| pchar->ppSubtargets[i]->iHealthMax!=s_piHealthMaxOld[i])
			{
				bDirty = true;
			}
		}
	}

	if(bDirty)
	{
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
	}

	PERFINFO_AUTO_STOP();
}

// Returns true if the subtarget matches the subtarget choice
S32 powersubtarget_MatchChoice(PowerSubtarget *pSubtarget, PowerSubtargetChoice *pChoice)
{
	S32 bMatch = false;

	if(IS_HANDLE_ACTIVE(pChoice->hCategory))
	{
		PowerSubtargetCategory *pcat = GET_REF(pChoice->hCategory);
		if(pcat && pcat==GET_REF(pSubtarget->hCategory))
		{
			bMatch = true;
		}
	}

	return bMatch;
}

// Lets you know if the category exists
S32 powersubtarget_CategoryExists(const char *pchCategory)
{
	if(RefSystem_ReferentFromString(g_hPowerSubtargetCategoryDict, pchCategory))
		return 1;

	return 0;
}

// Gets the category
PowerSubtargetCategory* powersubtarget_GetCategoryByName(const char *pchCategory)
{
	return RefSystem_ReferentFromString(g_hPowerSubtargetCategoryDict, pchCategory);
}

PowerSubtarget* powersubtarget_GetByName( const char* pchName )
{
	return RefSystem_ReferentFromString(g_hPowerSubtargetDict, pchName);
}

#include "AutoGen/PowerSubtarget_h_ast.c"
