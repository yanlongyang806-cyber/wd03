#include "dynFxDamage.h"
#include "dynFxInfo.h"
#include "dynFx.h"
#include "dynFxManager.h"

#include "fileutil.h"
#include "FolderCache.h"
#include "error.h"
#include "StringCache.h"

#include "dynFxDamage_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

DictionaryHandle g_hDamageInfoDict = NULL;
static bool reloaded = false;

AUTO_RUN;
void registerDamageInfoDict(void)
{
	g_hDamageInfoDict = RefSystem_RegisterSelfDefiningDictionary("DynFxDamageInfo", false, parse_DynFxDamageInfo, true, false, NULL);
}

static void validateDynFxDamageInfo(DynFxDamageInfo* info)
{
	int i,j;
	for(i = 0; i < eaSize(&info->eaDamageRanges); ++i)
	{
		DynFxDamageRangeInfo* range = info->eaDamageRanges[i];
		if (range->minHitPoints == range->maxHitPoints || range->maxHitPoints < range->minHitPoints)
		{
			FxFileError(info->pcFileName, "A damage range has hitpoint values that are equal or backwards.");
		}

		for (j = 0; j < eaSize(&range->eaFxList); j++)
		{
			DynFxDamageRangeInfoFxRef* fxref = range->eaFxList[j];
			if (!GET_REF(fxref->hFx))
			{
				FxFileError(info->pcFileName, "A damage range references an unknown fx.");
			}
		}
	}
}

static void validateAllDynFxDamageInfos()
{
	FOR_EACH_IN_REFDICT(g_hDamageInfoDict, DynFxDamageInfo, info)
	{
		validateDynFxDamageInfo(info);
	}
	FOR_EACH_END
}

static void dynDamageFxReloadCallback(const char *relpath, int when)
{
	loadstart_printf("Reloading DynFxDamageInfo...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if(!ParserReloadFileToDictionary(relpath,g_hDamageInfoDict))
	{
		FxFileError(relpath, "Error reloading damage fx file: %s", relpath);
	}

	validateAllDynFxDamageInfos();

	loadend_printf("done");
	reloaded = true;
}


void dynFxDamageInfoLoadAll()
{
	loadstart_printf("Loading DynFxDamageInfo... ");
	ParserLoadFilesToDictionary("dyn/damage", ".damagefx", "DynFxDamage.bin", PARSER_OPTIONALFLAG|PARSER_BINS_ARE_SHARED, g_hDamageInfoDict);

	// Reload callbacks
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/damage/*.damagefx", dynDamageFxReloadCallback);
	}

	validateAllDynFxDamageInfos();

	loadend_printf("done (%d DynFxDamageInfo Loaded)", RefSystem_GetDictionaryNumberOfReferents(g_hDamageInfoDict));
}

bool dynFxDamageInfoReloadedThisFrame()
{
	return reloaded;
}

void dynFxDamageResetReloadedFlag()
{
	reloaded = false;
}

#include "dynFxDamage_h_ast.c"