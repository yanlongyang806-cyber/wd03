/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "SuggestedContentCommon.h"
#include "Entity.h"
#include "StringCache.h"
#include "ResourceManager.h"
#include "FolderCache.h"
#include "file.h"
#include "fileutil.h"
#include "ActivityCommon.h"
#include "queue_common.h"
#include "referencesystem.h"
#include "progression_common.h"

#include "AutoGen/SuggestedContentCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// Dictionary holding the game progression nodes
DictionaryHandle g_hSuggestedContentListDictionary = NULL;

static bool suggestedContent_SuggestedContentListValidate(SuggestedContentList* pSuggestedContent)
{
	bool bSuccess = true;
	SuggestedContentType eValidContentType = SuggestedContentType_None;

	// Validate all references to the content
	FOR_EACH_IN_EARRAY_FORWARDS(pSuggestedContent->ppSuggestedContentForLevels, SuggestedContentForLevel, pLevelContent)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pLevelContent->ppContent, SuggestedContentNode, pContentNode)
		{
			// TODO: Validate Queue Levels against Our Level.

			switch (pContentNode->eType)
			{
				case SuggestedContentType_Event:
				{
					if (EventDef_Find(pContentNode->pchContentEventName) == NULL)
					{
						// Invalid event reference
						ErrorFilenamef(pSuggestedContent->pchFilename, "Suggested content for level %d in %s references an invalid event def: %s", 
							pLevelContent->iLevel,
							pSuggestedContent->pchName,
							pContentNode->pchContentEventName);

						bSuccess = false;
					}
					break;
				}
				case SuggestedContentType_Queue:
				{
					if (GET_REF(pContentNode->hContentQueue) == NULL)
					{
						// Invalid event reference
						ErrorFilenamef(pSuggestedContent->pchFilename, "Suggested content for level %d in %s references an invalid queue def: %s", 
							pLevelContent->iLevel,
							pSuggestedContent->pchName,
							REF_STRING_FROM_HANDLE(pContentNode->hContentQueue));

						bSuccess = false;
					}
					break;
				}
				case SuggestedContentType_StoryNode:
				{
					if (progression_NodeDefFromName(pContentNode->pchContentStoryName) == NULL)
					{
						// Invalid event reference
						ErrorFilenamef(pSuggestedContent->pchFilename, "Suggested content for level %d in %s references an invalid Story def: %s", 
							pLevelContent->iLevel,
							pSuggestedContent->pchName,
							pContentNode->pchContentStoryName);

						bSuccess = false;
					}
					break;
				}
			}
		}
		FOR_EACH_END
	}
	FOR_EACH_END

	return bSuccess;
}

static int suggestedContent_SuggestedContentListValidateCB(enumResourceValidateType eType, const char* pcDictName, const char* pcResourceName, SuggestedContentList *pSuggestedContent, U32 userID)
{
	switch (eType)
	{
		case RESVALIDATE_CHECK_REFERENCES:
		{
			suggestedContent_SuggestedContentListValidate(pSuggestedContent);

			return VALIDATE_HANDLED;
		}
	}
	return VALIDATE_NOT_HANDLED;
}

static void suggestedContent_ReloadSuggestedContent(const char* pcRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Suggested Player Content...");

	fileWaitForExclusiveAccess(pcRelPath);
	errorLogFileIsBeingReloaded(pcRelPath);

	ParserReloadFileToDictionary(pcRelPath, g_hSuggestedContentListDictionary);

	loadend_printf(" done (%d Suggested Player Content)", RefSystem_GetDictionaryNumberOfReferentInfos(g_hSuggestedContentListDictionary));
}

AUTO_RUN;
void RegisterSuggestedContentListDictionaries(void)
{
	g_hSuggestedContentListDictionary = RefSystem_RegisterSelfDefiningDictionary("SuggestedContentList", false, parse_SuggestedContentList, true, true, NULL);

	resDictManageValidation(g_hSuggestedContentListDictionary, suggestedContent_SuggestedContentListValidateCB);

	if (IsServer()) 
	{
		resDictProvideMissingResources(g_hSuggestedContentListDictionary);

		if (isDevelopmentMode() || isProductionEditMode()) 
		{
			resDictMaintainInfoIndex(g_hSuggestedContentListDictionary, ".Name", NULL, NULL, NULL, NULL);
		}
	} 
	else 
	{
		resDictRequestMissingResources(g_hSuggestedContentListDictionary, 8, false, resClientRequestSendReferentCommand);
	}
}

// Loads the suggested player content
void suggestedContent_LoadSuggestedContent(void)
{
	// Load game progression nodes, do not load into shared memory
	resLoadResourcesFromDisk(g_hSuggestedContentListDictionary, 
		GAME_CONTENT_NODE_BASE_DIR, 
		".sgc", 
		"SuggestedContent.bin", 
		PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);

	if (isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, GAME_CONTENT_NODE_BASE_DIR "/*.sgc", suggestedContent_ReloadSuggestedContent);
	}
}

// Gets the SuggestedContentList from the dictionary
SuggestedContentList * suggestedContent_SuggestedContentListFromName(const char *pchName)
{
	return RefSystem_ReferentFromString(g_hSuggestedContentListDictionary, pchName);
}

#include "AutoGen/SuggestedContentCommon_h_ast.c"
