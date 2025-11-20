#include "AuctionBrokerCommon.h"

#include "GlobalTypes.h"
#include "file.h"
#include "fileutil.h"
#include "ResourceManager.h"
#include "referencesystem.h"
#include "FolderCache.h"
#include "error.h"
#include "itemCommon.h"

#include "AutoGen/AuctionBrokerCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Dictionary holding the auction broker defs
DictionaryHandle g_hAuctionBrokerDictionary = NULL;

#define AUCTION_BROKER_BASE_DIR "defs/auctionbroker"

#include "AutoGen/AuctionBrokerCommon_h_ast.c"

static void AuctionBroker_Validate(AuctionBrokerDef * pAuctionBrokerDef)
{
	if (eaSize(&pAuctionBrokerDef->ppClassInfoList) == 0)
	{
		ErrorFilenamef(pAuctionBrokerDef->pchFilename, 
			"AuctionBroker '%s': Does not have any class information.", 
			pAuctionBrokerDef->pchName);
	}

	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pAuctionBrokerDef->ppClassInfoList, AuctionBrokerClassInfo, pClassInfo)
	{
		S32 i, j;
		AuctionBrokerLevelInfo *pLevelInfo;
		AuctionBrokerLevelInfo *pOtherLevelInfo;

		if (eaSize(&pClassInfo->ppLevelInfoList) == 0)
		{
			ErrorFilenamef(pAuctionBrokerDef->pchFilename, 
				"AuctionBroker '%s': Character class '%s' does not have any level range defined.", 
				pAuctionBrokerDef->pchName,
				REF_STRING_FROM_HANDLE(pClassInfo->hCharacterClass));
		}

		for (i = 0; i < eaSize(&pClassInfo->ppLevelInfoList); i++)
		{
			pLevelInfo = pClassInfo->ppLevelInfoList[i];

			if (pLevelInfo->iLevelRangeStart < 1 ||
				pLevelInfo->iLevelRangeEnd < 1 ||
				pLevelInfo->iLevelRangeEnd < pLevelInfo->iLevelRangeStart)
			{
				ErrorFilenamef(pAuctionBrokerDef->pchFilename, 
					"AuctionBroker '%s': Character class '%s' has an invalid level range of %d to %d.", 
					pAuctionBrokerDef->pchName,
					REF_STRING_FROM_HANDLE(pClassInfo->hCharacterClass),
					pLevelInfo->iLevelRangeStart,
					pLevelInfo->iLevelRangeEnd);
			}

			if (eaSize(&pLevelInfo->ppItemDropInfo) == 0)
			{
				ErrorFilenamef(pAuctionBrokerDef->pchFilename, 
					"AuctionBroker '%s': Character class '%s': Level range %d to %d does not reference any items.", 
					pAuctionBrokerDef->pchName,
					REF_STRING_FROM_HANDLE(pClassInfo->hCharacterClass),
					pLevelInfo->iLevelRangeStart,
					pLevelInfo->iLevelRangeEnd);
			}

			// Validate level ranges
			for (j = i + 1; j < eaSize(&pClassInfo->ppLevelInfoList); j++)
			{
				pOtherLevelInfo = pClassInfo->ppLevelInfoList[j];

				if (pOtherLevelInfo->iLevelRangeEnd < pLevelInfo->iLevelRangeStart)
				{
					continue;
				}

				if (pOtherLevelInfo->iLevelRangeStart > pLevelInfo->iLevelRangeEnd)
				{
					continue;
				}

				// Ranges do intersect and we don't want this to happen
				ErrorFilenamef(pAuctionBrokerDef->pchFilename, 
					"AuctionBroker '%s': Character class '%s': Two level ranges should not intersect. Level ranges %d to %d and %d to %d intersect.", 
					pAuctionBrokerDef->pchName,
					REF_STRING_FROM_HANDLE(pClassInfo->hCharacterClass),
					pLevelInfo->iLevelRangeStart,
					pLevelInfo->iLevelRangeEnd,
					pOtherLevelInfo->iLevelRangeStart,
					pOtherLevelInfo->iLevelRangeEnd);
			}
		}
	}
	FOR_EACH_END
}

static void AuctionBroker_ValidateRefs(AuctionBrokerDef * pAuctionBrokerDef)
{
	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pAuctionBrokerDef->ppClassInfoList, AuctionBrokerClassInfo, pClassInfo)
	{
		if (!IS_HANDLE_ACTIVE(pClassInfo->hCharacterClass))
		{
			ErrorFilenamef(pAuctionBrokerDef->pchFilename, 
				"AuctionBroker '%s': Class info does not reference any character class.", 
				pAuctionBrokerDef->pchName);
		}
		else if (!GET_REF(pClassInfo->hCharacterClass))
		{
			ErrorFilenamef(pAuctionBrokerDef->pchFilename, 
				"AuctionBroker '%s': Class info does not reference a valid class: '%s'.", 
				pAuctionBrokerDef->pchName,
				REF_STRING_FROM_HANDLE(pClassInfo->hCharacterClass));
		}

		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pClassInfo->ppLevelInfoList, AuctionBrokerLevelInfo, pLevelInfo)
		{
			FOR_EACH_IN_CONST_EARRAY_FORWARDS(pLevelInfo->ppItemDropInfo, AuctionBrokerItemDropInfo, pItemDropInfo)
			{
				ItemDefRef itemDefRef = pItemDropInfo->itemDefRef;
				if (!IS_HANDLE_ACTIVE(itemDefRef.hDef))
				{
					ErrorFilenamef(pAuctionBrokerDef->pchFilename, 
						"AuctionBroker '%s': Character class '%s': Level range %d to %d has an empty item reference.", 
						pAuctionBrokerDef->pchName,
						REF_STRING_FROM_HANDLE(pClassInfo->hCharacterClass),
						pLevelInfo->iLevelRangeStart,
						pLevelInfo->iLevelRangeEnd);
				}
				else if (!GET_REF(itemDefRef.hDef))
				{
					ErrorFilenamef(pAuctionBrokerDef->pchFilename, 
						"AuctionBroker '%s': Character class '%s': Level range %d to %d references an invalid item: '%s'.", 
						pAuctionBrokerDef->pchName,
						REF_STRING_FROM_HANDLE(pClassInfo->hCharacterClass),
						pLevelInfo->iLevelRangeStart,
						pLevelInfo->iLevelRangeEnd,
						REF_STRING_FROM_HANDLE(itemDefRef.hDef));
				}
			}
			FOR_EACH_END
		}
		FOR_EACH_END
	}
	FOR_EACH_END
}

static int AuctionBroker_ValidateEventsCB(enumResourceValidateType eType, const char* pcDictName, const char* pcResourceName, AuctionBrokerDef * pAuctionBrokerDef, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
		{
			if (IsGameServerBasedType())
			{
				AuctionBroker_Validate(pAuctionBrokerDef);
				return VALIDATE_HANDLED;
			}			
		}
		xcase RESVALIDATE_CHECK_REFERENCES:
		{
			if (IsGameServerBasedType())
			{
				AuctionBroker_ValidateRefs(pAuctionBrokerDef);
				return VALIDATE_HANDLED;
			}
		}
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void AuctionBroker_RegisterAuctionBrokerDictionary(void)
{
	g_hAuctionBrokerDictionary = RefSystem_RegisterSelfDefiningDictionary("AuctionBroker", false, parse_AuctionBrokerDef, true, true, NULL);

	resDictManageValidation(g_hAuctionBrokerDictionary, AuctionBroker_ValidateEventsCB);

	if (IsServer()) 
	{
		resDictProvideMissingResources(g_hAuctionBrokerDictionary);

		if (isDevelopmentMode() || isProductionEditMode()) 
		{
			resDictMaintainInfoIndex(g_hAuctionBrokerDictionary, ".Name", NULL, NULL, NULL, NULL);
		}
	} 
	else 
	{
		resDictRequestMissingResources(g_hAuctionBrokerDictionary, 8, false, resClientRequestSendReferentCommand);
	}
}

static void AuctionBroker_ReloadDefs(const char* pcRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Auction Broker Def(s)...");

	fileWaitForExclusiveAccess(pcRelPath);
	errorLogFileIsBeingReloaded(pcRelPath);

	ParserReloadFileToDictionary(pcRelPath, g_hAuctionBrokerDictionary);

	loadend_printf(" done (%d Auction Broker Def(s))", RefSystem_GetDictionaryNumberOfReferentInfos(g_hAuctionBrokerDictionary));
}

// Loads the auction broker defs from the disk
AUTO_STARTUP(AutoStart_AuctionBroker) ASTRT_DEPS(	Items,
													CharacterClasses,);
void AuctionBroker_LoadDefs(void)
{
	// Load game progression nodes, do not load into shared memory
	resLoadResourcesFromDisk(g_hAuctionBrokerDictionary, 
		AUCTION_BROKER_BASE_DIR, 
		".broker", 
		"AuctionBroker.bin", 
		PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);

	if (isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, AUCTION_BROKER_BASE_DIR "/*.broker", AuctionBroker_ReloadDefs);
	}
}

// Returns the info for the given character class in the def
const AuctionBrokerClassInfo * AuctionBroker_GetClassInfo(AuctionBrokerDef *pDef, const char *pchCharacterClassName)
{
	S32 iClassIndex = eaIndexedFindUsingString(&pDef->ppClassInfoList, pchCharacterClassName);

	if (iClassIndex >= 0)
	{
		return pDef->ppClassInfoList[iClassIndex];
	}

	return NULL;
}

// Returns the level info
const AuctionBrokerLevelInfo * AuctionBroker_GetLevelInfo(const AuctionBrokerClassInfo *pClassInfo, S32 iLevel)
{
	// We could do actual binary search here but the assumption is that there won't be too many level ranges
	// to affect the performance
	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pClassInfo->ppLevelInfoList, AuctionBrokerLevelInfo, pLevelInfo)
	{
		if (iLevel >= pLevelInfo->iLevelRangeStart && iLevel <= pLevelInfo->iLevelRangeEnd)
		{
			return pLevelInfo;
		}
	}
	FOR_EACH_END

	return NULL;
}