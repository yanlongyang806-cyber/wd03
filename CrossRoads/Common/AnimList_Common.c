/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AnimList_Common.h"
#include "DynFxInfo.h"
#include "error.h"
#include "EntityMovementManager.h"
#include "file.h"
#include "ResourceManager.h"
#include "AnimList_Common_h_ast.h"
#include "StringCache.h"
#include "wlUGC.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_AnimListDict;

static void animlist_Verify(AIAnimList* al)
{
	if (IsServer())
	{
		if (!gConf.bNewAnimationSystem)
		{
			if(!eaSize(&al->bits) && !eaSize(&al->FX) && !eaSize(&al->FlashFX))
				ErrorFilenamef(al->filename, "No bits or fx specified for anim list %s", al->name);
		}
		else
		{
			if(!al->animKeyword && !eaSize(&al->FX) && !eaSize(&al->FlashFX))
				ErrorFilenamef(al->filename, "No animkeyword or fx specified for anim list %s", al->name);
		}
		FOR_EACH_IN_EARRAY(al->FX, const char, pcFxName)
			if (!dynFxInfoExists(pcFxName))
			{
				ErrorFilenamef(al->filename, "FX %s is missing or invalid", pcFxName);
				eaRemoveFast(&al->FX, ipcFxNameIndex);
			}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(al->FlashFX, const char, pcFxName)
		{
			if(!dynFxInfoExists(pcFxName))
			{
				ErrorFilenamef(al->filename, "FlashFX %s is missing or invalid", pcFxName);
				eaRemoveFast(&al->FlashFX, ipcFxNameIndex);
			}
		}
		FOR_EACH_END;
	}
}

static void animlist_RegisterBitHandles(AIAnimList* al)
{
	int i, n = eaSize(&al->bits);

	for(i = 0; i < n; i++)
		eaiPush(&al->bitHandles, mmGetAnimBitHandleByName(al->bits[i], 0));
}

static int animlist_Validate(enumResourceValidateType eType, const char* pDictName, const char* pResourceName, void* pResource, U32 userID)
{
	AIAnimList* al = pResource;

	switch (eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
		resAddValueDep("AIAnimListLoadingVer");
		animlist_Verify(al);
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void RegisterAnimListDictionary(void)
{
	g_AnimListDict = RefSystem_RegisterSelfDefiningDictionary("AIAnimList", false, parse_AIAnimList, true, true, NULL);
	ParserBinRegisterDepValue("AIAnimListLoadingVer", 1);

	resDictManageValidation(g_AnimListDict, animlist_Validate);

	if (IsServer())
	{
		resDictProvideMissingResources(g_AnimListDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_AnimListDict, ".name", NULL, ".Tags", NULL, NULL);
		}
	} 
	else
	{
		resDictRequestMissingResources(g_AnimListDict, 8, false, resClientRequestSendReferentCommand);
	}
}

AUTO_STARTUP(AnimLists) ASTRT_DEPS(WorldLibMain);
void animlist_Load(void)
{
	if (IsServer()){
		resLoadResourcesFromDisk(g_AnimListDict, "ai/animlists", ".al", "AnimLists.bin", PARSER_SERVERSIDE | RESOURCELOAD_SHAREDMEMORY);
	}
}

#include "AnimList_Common_h_ast.c"
