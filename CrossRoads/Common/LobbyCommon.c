/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "LobbyCommon.h"
#include "progression_common.h"
#include "inventoryCommon.h"
#include "Entity.h"
#include "mission_common.h"
#include "StringCache.h"
#include "ResourceManager.h"
#include "FolderCache.h"
#include "file.h"
#include "fileutil.h"
#include "ActivityCommon.h"
#include "queue_common.h"
#include "referencesystem.h"

#include "AutoGen/LobbyCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// All prefixes must be of same length
#define CONTENT_NAME_PREFIX_GAME_PROGRESSION_NODE "GPN_"
#define CONTENT_NAME_PREFIX_UGC "UGC_"
#define CONTENT_NAME_PREFIX_UGC_SERIES "UGS_"
#define CONTENT_NAME_PREFIX_MISSION "MSN_"
#define CONTENT_NAME_PREFIX_EVENT "EVT_"
#define CONTENT_NAME_PREFIX_QUEUE "QUE_"

// Dictionary holding the game progression nodes
DictionaryHandle g_hRecommendedPlayerContentDictionary = NULL;

// Global settings for the lobby
LobbyConfig g_LobbyConfig = {0};

// Returns a static string which will be shared by all calls to this function. This function never returns NULL.
const char* gameContentNode_GetUniqueNameByParams(GameContentNodeType eType, SA_PARAM_OP_STR const char *pchName, ContainerID iUGCProjectID, ContainerID iUGCProjectSeriesID, ContainerID iUGCProjectSeriesNodeID)
{
	static char buffer[512];

	buffer[0] = '\0';

	if (iUGCProjectID)
	{ 
		sprintf(buffer, CONTENT_NAME_PREFIX_UGC "%d", iUGCProjectID);
	}
	else if (iUGCProjectSeriesID)
	{
		sprintf(buffer, CONTENT_NAME_PREFIX_UGC_SERIES "%d_%d", iUGCProjectSeriesID, iUGCProjectSeriesNodeID);
	}
	else
	{
		switch (eType)
		{
		case GameContentNodeType_GameProgressionNode:
			strcat_s(buffer, sizeof(buffer), CONTENT_NAME_PREFIX_GAME_PROGRESSION_NODE);
			break;
		case GameContentNodeType_Event:
			strcat_s(buffer, sizeof(buffer), CONTENT_NAME_PREFIX_EVENT);
			break;
		case GameContentNodeType_Mission:
			strcat_s(buffer, sizeof(buffer), CONTENT_NAME_PREFIX_MISSION);
			break;
		case GameContentNodeType_Queue:
			strcat_s(buffer, sizeof(buffer), CONTENT_NAME_PREFIX_QUEUE);
			break;
		}
		if (pchName)
		{
			strcat_s(buffer, sizeof(buffer), pchName);
		}		
	}

	return buffer;
}

// Returns a static string which will be shared by all calls to this function. This function never returns NULL.
const char * gameContentNode_GetUniqueName(SA_PARAM_NN_VALID GameContentNodeRef *pNodeRef)
{
	const char *pchName = NULL;
	ContainerID iUGCProjectID = 0;
	ContainerID iUGCProjectSeriesID = 0;
	ContainerID iUGCProjectSeriesNodeID = 0;

	if (pNodeRef->eType == GameContentNodeType_GameProgressionNode)
	{
		pchName = REF_STRING_FROM_HANDLE(pNodeRef->hNode);
	}
	else if (pNodeRef->eType == GameContentNodeType_Queue)
	{
		pchName = REF_STRING_FROM_HANDLE(pNodeRef->hQueue);
	}
	else if (pNodeRef->eType == GameContentNodeType_Mission)
	{
		pchName = REF_STRING_FROM_HANDLE(pNodeRef->hMission);
	}
	else
	{
		pchName = pNodeRef->pchEventName;
	}
	iUGCProjectID = pNodeRef->iUGCProjectID;
	iUGCProjectSeriesID = pNodeRef->iUGCProjectSeriesID;
	iUGCProjectSeriesNodeID = pNodeRef->iUGCProjectSeriesNodeID;

	return gameContentNode_GetUniqueNameByParams(pNodeRef->eType, pchName,
												 iUGCProjectID, iUGCProjectSeriesID, iUGCProjectSeriesNodeID);
}

// Returns the reference to either a progression node or a UGC project given a destination string. The reference returns must be freed if not NULL.
SA_RET_OP_VALID GameContentNodeRef * gameContentNode_GetRefFromName(SA_PARAM_OP_STR const char *pchName)
{
	if (pchName && pchName[0])
	{
		// We're assuming that all prefixes have the same length
		const char *pchActualName = pchName + strlen(CONTENT_NAME_PREFIX_UGC);

		if (pchActualName >= pchName + strlen(pchName))
		{
			return NULL;
		}

		if (strStartsWith(pchName, CONTENT_NAME_PREFIX_UGC))
		{
			const char *pchUGCProjectID = pchName + strlen(CONTENT_NAME_PREFIX_UGC);

			if (*pchUGCProjectID)
			{
				GameContentNodeRef *pRef = StructCreate(parse_GameContentNodeRef);
				pRef->eType = GameContentNodeType_UGC;
				pRef->iUGCProjectID = atoi(pchActualName);

				return pRef;
			}
		}
		if (strStartsWith(pchName, CONTENT_NAME_PREFIX_UGC_SERIES))
		{
			const char* pchUGCSeriesAndNodeID = pchName + strlen(CONTENT_NAME_PREFIX_UGC_SERIES);
			if (*pchUGCSeriesAndNodeID)
			{
				GameContentNodeRef *pRef = StructCreate(parse_GameContentNodeRef);
				pRef->eType = GameContentNodeType_UGC;
				sscanf(pchUGCSeriesAndNodeID, "%d_%d", &pRef->iUGCProjectSeriesID, &pRef->iUGCProjectSeriesNodeID);

				return pRef;
			}
		}
		else
		{
			GameContentNodeRef *pRef = StructCreate(parse_GameContentNodeRef);

			if (strStartsWith(pchName, CONTENT_NAME_PREFIX_GAME_PROGRESSION_NODE))
			{
				pRef->eType = GameContentNodeType_GameProgressionNode;
				SET_HANDLE_FROM_STRING(g_hGameProgressionNodeDictionary, pchActualName, pRef->hNode);
			}
			else if (strStartsWith(pchName, CONTENT_NAME_PREFIX_MISSION))
			{
				pRef->eType = GameContentNodeType_Mission;
				SET_HANDLE_FROM_STRING(g_MissionDictionary, pchActualName, pRef->hMission);
			}
			else if (strStartsWith(pchName, CONTENT_NAME_PREFIX_EVENT))
			{
				pRef->eType = GameContentNodeType_Event;
				pRef->pchEventName = allocAddString(pchActualName);
			}
			else if (strStartsWith(pchName, CONTENT_NAME_PREFIX_QUEUE))
			{
				pRef->eType = GameContentNodeType_Queue;
				SET_HANDLE_FROM_STRING(g_hQueueDefDict, pchActualName, pRef->hQueue);
			}			

			return pRef;
		}
	}
	return NULL;
}

bool gameContentNode_RefsAreEqual(SA_PARAM_OP_VALID GameContentNodeRef *pRef1, SA_PARAM_OP_VALID GameContentNodeRef *pRef2)
{
	if (pRef1 == pRef2)
	{
		return true;
	}

	if (pRef1 == NULL || pRef2 == NULL)
	{
		return false;
	}

	return pRef1->iUGCProjectID == pRef2->iUGCProjectID && 
		pRef1->iUGCProjectSeriesID == pRef2->iUGCProjectSeriesID && 
		REF_COMPARE_HANDLES(pRef1->hNode, pRef2->hNode) &&
		REF_COMPARE_HANDLES(pRef1->hQueue, pRef2->hQueue) &&
		REF_COMPARE_HANDLES(pRef1->hMission, pRef2->hMission) &&
		pRef1->pchEventName == pRef2->pchEventName;
}

// Indicates whether the given node reference points to the given node
bool gameContentNode_RefPointsToNode(SA_PARAM_NN_VALID GameContentNodeRef *pRef, SA_PARAM_NN_VALID GameContentNode *pNode)
{
	return pRef->iUGCProjectID == pNode->contentRef.iUGCProjectID && 
		pRef->iUGCProjectSeriesID == pNode->contentRef.iUGCProjectSeriesID && 
		REF_COMPARE_HANDLES(pRef->hNode, pNode->contentRef.hNode) &&
		REF_COMPARE_HANDLES(pRef->hQueue, pNode->contentRef.hQueue) &&
		REF_COMPARE_HANDLES(pRef->hMission, pNode->contentRef.hMission) &&
		pRef->pchEventName == pNode->contentRef.pchEventName;
}

// Indicates whether both nodes point to the same content
bool gameContentNode_NodesAreEqual(GameContentNode *pNodeLeft, GameContentNode *pNodeRight)
{
	if (pNodeLeft == NULL && pNodeRight == NULL)
	{
		return true;
	}

	if (pNodeLeft == NULL || pNodeRight == NULL)
	{
		return false;
	}

	return gameContentNode_RefsAreEqual(&pNodeLeft->contentRef, &pNodeRight->contentRef);
}

// Fills a game content node with information from a progression node
void gameContentNode_FillFromGameProgressionNode(SA_PARAM_NN_VALID GameContentNode *pContentNode, SA_PARAM_NN_VALID const GameProgressionNodeDef *pNode)
{
	bool bMissionGroup = pNode->eFunctionalType == GameProgressionNodeFunctionalType_MissionGroup && pNode->pMissionGroupInfo;

	// Set header data
	pContentNode->contentRef.eType = GameContentNodeType_GameProgressionNode;
	SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, (GameProgressionNodeDef *)pNode, pContentNode->contentRef.hNode);
	StructCopyString( &pContentNode->strContentRefName, gameContentNode_GetUniqueName( &pContentNode->contentRef ));

	pContentNode->eProgressionNodeType = pNode->eType;

	// Assume Cryptic content is all the most awesome! 
	pContentNode->fRating = 1;

	// Set all the data fields
	pContentNode->strDisplayName = StructAllocString(TranslateDisplayMessage(pNode->msgDisplayName));
	pContentNode->strSummary = StructAllocString(TranslateDisplayMessage(pNode->msgSummary));
	pContentNode->pchTeaser = StructAllocString(TranslateDisplayMessage(pNode->msgTeaser));
	pContentNode->pchArtFileName = pNode->pchArtFileName;
	if (bMissionGroup)
	{
		pContentNode->bMajor = pNode->pMissionGroupInfo->bMajor;
		pContentNode->uiTimeToComplete = pNode->pMissionGroupInfo->uiTimeToComplete;
		pContentNode->iSuggestedPlayerLevel = pNode->pMissionGroupInfo->iRequiredPlayerLevel;
	}
}

static bool gameContentNode_RecommendedPlayerContentValidateRefs(RecommendedPlayerContent* pRecommendedContent)
{
	bool bSuccess = true;
	GameContentNodeType eValidContentType = GameContentNodeType_None;

	// Validate all references to the content
	FOR_EACH_IN_EARRAY_FORWARDS(pRecommendedContent->ppRecommendedPlayerContentForLevels, RecommendedPlayerContentForLevel, pLevelContent)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pLevelContent->ppContent, GameContentNodeRef, pContentNodeRef)
		{
			if (eValidContentType == GameContentNodeType_None)
			{
				eValidContentType = pContentNodeRef->eType;
			}

			if (eValidContentType != pContentNodeRef->eType)
			{
				ErrorFilenamef(pRecommendedContent->pchFilename, "You cannot mix and match different type of content.");
				bSuccess = false;
			}

			switch (pContentNodeRef->eType)
			{
			case GameContentNodeType_GameProgressionNode:
				{
					if (GET_REF(pContentNodeRef->hNode) == NULL)
					{
						// Invalid game progression node reference
						ErrorFilenamef(pRecommendedContent->pchFilename, "Recommended content for level %d in %s references an invalid game progression node: %s", 
							pLevelContent->iLevel,
							pRecommendedContent->pchName,
							REF_STRING_FROM_HANDLE(pContentNodeRef->hNode));

						bSuccess = false;
					}
					break;
				}
			case GameContentNodeType_Event:
				{
					if (EventDef_Find(pContentNodeRef->pchEventName) == NULL)
					{
						// Invalid event reference
						ErrorFilenamef(pRecommendedContent->pchFilename, "Recommended content for level %d in %s references an invalid event def: %s", 
							pLevelContent->iLevel,
							pRecommendedContent->pchName,
							pContentNodeRef->pchEventName);

						bSuccess = false;
					}
					break;
				}
			case GameContentNodeType_Queue:
				{
					if (GET_REF(pContentNodeRef->hQueue) == NULL)
					{
						// Invalid event reference
						ErrorFilenamef(pRecommendedContent->pchFilename, "Recommended content for level %d in %s references an invalid queue def: %s", 
							pLevelContent->iLevel,
							pRecommendedContent->pchName,
							REF_STRING_FROM_HANDLE(pContentNodeRef->hQueue));

						bSuccess = false;
					}
					break;
				}
			case GameContentNodeType_Mission:
				{
					if (GET_REF(pContentNodeRef->hMission) == NULL)
					{
						// Invalid event reference
						ErrorFilenamef(pRecommendedContent->pchFilename, "Recommended content for level %d in %s references an invalid mission: %s", 
							pLevelContent->iLevel,
							pRecommendedContent->pchName,
							REF_STRING_FROM_HANDLE(pContentNodeRef->hMission));

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

static int gameContentNode_RecommendedPlayerContentValidateCB(enumResourceValidateType eType, const char* pcDictName, const char* pcResourceName, RecommendedPlayerContent *pRecommendedContent, U32 userID)
{
	switch (eType)
	{
		case RESVALIDATE_CHECK_REFERENCES:
		{
			gameContentNode_RecommendedPlayerContentValidateRefs(pRecommendedContent);

			return VALIDATE_HANDLED;
		}
	}
	return VALIDATE_NOT_HANDLED;
}

// Indicates whether the given game content node points to a current progression node for the entity
bool gameContentNode_IsCurrentCharProgress(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID GameContentNode *pNode)
{
	GameProgressionNodeDef* pNodeDef;	

	if (pEnt && (pNodeDef = GET_REF(pNode->contentRef.hNode)))
	{
		GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pNodeDef);
		return (pNodeDef == progression_GetCurrentProgress(pEnt, pBranchNodeDef));
	}

	return false;
}

static void gameContentNode_ReloadRecommendedContent(const char* pcRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Recommended Player Content...");

	fileWaitForExclusiveAccess(pcRelPath);
	errorLogFileIsBeingReloaded(pcRelPath);

	ParserReloadFileToDictionary(pcRelPath, g_hRecommendedPlayerContentDictionary);

	loadend_printf(" done (%d Recommended Player Content)", RefSystem_GetDictionaryNumberOfReferentInfos(g_hRecommendedPlayerContentDictionary));
}

AUTO_RUN;
void RegisterGameContentNodeDictionaries(void)
{
	g_hRecommendedPlayerContentDictionary = RefSystem_RegisterSelfDefiningDictionary("RecommendedPlayerContent", false, parse_RecommendedPlayerContent, true, true, NULL);

	resDictManageValidation(g_hRecommendedPlayerContentDictionary, gameContentNode_RecommendedPlayerContentValidateCB);

	if (IsServer()) 
	{
		resDictProvideMissingResources(g_hRecommendedPlayerContentDictionary);

		if (isDevelopmentMode() || isProductionEditMode()) 
		{
			resDictMaintainInfoIndex(g_hRecommendedPlayerContentDictionary, ".Name", ".Scope", NULL, NULL, NULL);
		}
	} 
	else 
	{
		resDictRequestMissingResources(g_hRecommendedPlayerContentDictionary, 8, false, resClientRequestSendReferentCommand);
	}
}

// Loads the recommended player content
void gameContentNode_LoadRecommendedContent(void)
{
	// Load game progression nodes, do not load into shared memory
	resLoadResourcesFromDisk(g_hRecommendedPlayerContentDictionary, 
		GAME_CONTENT_NODE_BASE_DIR, 
		".rgcn", 
		"RecommendedPlayerContent.bin", 
		PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);

	if (isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, GAME_CONTENT_NODE_BASE_DIR "/*.rgcn", gameContentNode_ReloadRecommendedContent);
	}
}

// Gets the RecommendedPlayerContent from the dictionary
RecommendedPlayerContent * gameContentNode_RecommendedPlayerContentFromName(const char *pchName)
{
	return RefSystem_ReferentFromString(g_hRecommendedPlayerContentDictionary, pchName);
}

static void lobby_LoadConfigInternal(const char* pchPath, S32 iWhen)
{
	StructReset(parse_LobbyConfig, &g_LobbyConfig);

	loadstart_printf("Loading Lobby Config... ");

	ParserLoadFiles(NULL, 
		"defs/config/LobbyConfig.def", 
		"LobbyConfig.bin", 
		PARSER_OPTIONALFLAG, 
		parse_LobbyConfig,
		&g_LobbyConfig);

	loadend_printf(" done.");
}

// Load game-specific configuration settings
static void lobby_LoadConfig(void)
{
	lobby_LoadConfigInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/LobbyConfig.def", lobby_LoadConfigInternal);
}

AUTO_STARTUP(LobbyCommon);
void lobby_Load(void)
{
	lobby_LoadConfig();
}

#include "AutoGen/LobbyCommon_h_ast.c"
