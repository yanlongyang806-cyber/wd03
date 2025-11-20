/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AutoTransDefs.h"
#include "contact_common.h"
#include "error.h"
#include "Expression.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "mission_common.h"
#include "progression_common.h"
#include "ResourceManager.h"
#include "Entity.h"
#include "Player.h"
#include "qsortG.h"
#include "LoginCommon.h"
#include "GameSession.h"
#include "Team.h"
#include "AutoGen/Team_h_ast.h"
#include "StringCache.h"
#include "StashTable.h"
#include "EntityLib.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "progression_transact.h"

#ifdef GAMESERVER
#include "GameServerLib.h"
#include "gslProgression.h"
#include "objTransactions.h"
#include "autogen/gameserverlib_autotransactions_autogen_wrappers.h"
#include "gslMission_transact.h"
#include "MapDescription.h"
#include "NotifyCommon.h"
#include "Message.h"
#include "StringFormat.h"
#endif

#ifdef APPSERVER
#include "objTransactions.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#endif

#ifdef GAMECLIENT
#include "UIGen.h"
#endif

#include "mission_common_h_ast.h"
#include "progression_common_h_ast.h"

#define PROGRESSION_SYSTEM_RUN_FIXUP_TRANS_INTERVAL 10

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

// Context for data-defined progression node types
DefineContext* g_pGameProgressionNodeTypes = NULL;

static int s_hPlayerVar;

// List of top-level game progression nodes
GameProgressionNodeRef** g_eaStoryArcNodes = NULL;

// Global settings for game progression
GameProgressionConfig g_GameProgressionConfig = {0};

// Dictionary holding the game progression nodes
DictionaryHandle g_hGameProgressionNodeDictionary = NULL;

// ----------------------------------------------------------------------------------
// Progression Expressions
// ----------------------------------------------------------------------------------

// Accessor to get the progression expression context
ExprContext* progression_GetContext(Entity *pEnt)
{
	static ExprContext *s_pProgressionContext = NULL;

	if (!s_pProgressionContext)
	{
		ExprFuncTable* stTable;

		s_pProgressionContext = exprContextCreate();
		stTable = exprContextCreateFunctionTable();

		exprContextAddFuncsToTableByTag(stTable,"entity");
		exprContextAddFuncsToTableByTag(stTable,"entityutil");
		exprContextAddFuncsToTableByTag(stTable,"player");
		exprContextAddFuncsToTableByTag(stTable,"util");

		exprContextSetFuncTable(s_pProgressionContext, stTable);

		exprContextSetAllowRuntimePartition(s_pProgressionContext);
		exprContextSetAllowRuntimeSelfPtr(s_pProgressionContext);

		assert(g_PlayerVarName != NULL);
	}
	
	exprContextSetPointerVarPooledCached(s_pProgressionContext,g_PlayerVarName,pEnt,parse_Entity,false,true,&s_hPlayerVar);

	if (pEnt) {
		exprContextSetSelfPtr(s_pProgressionContext, pEnt);
		exprContextSetPartition(s_pProgressionContext, entGetPartitionIdx(pEnt));
	} else {
		exprContextClearSelfPtrAndPartition(s_pProgressionContext);
	}
	return s_pProgressionContext;
}

// ----------------------------------------------------------------------------------
// Progression Logic
// ----------------------------------------------------------------------------------

static int progression_SortStoryRootNodes(const GameProgressionNodeRef** ppA, const GameProgressionNodeRef** ppB)
{
	const GameProgressionNodeRef* pA = (*ppA);
	const GameProgressionNodeRef* pB = (*ppB);
	GameProgressionNodeDef* pDefA = GET_REF(pA->hDef);
	GameProgressionNodeDef* pDefB = GET_REF(pB->hDef);

	if (!pDefA)
		return 1;
	if (!pDefB)
		return -1;

	if (pDefA->iSortOrder != pDefB->iSortOrder)
		return pDefA->iSortOrder - pDefB->iSortOrder;

	return stricmp(pDefA->pchName, pDefB->pchName);
}

static int progression_FindStoryRootNode(GameProgressionNodeDef* pNodeDef)
{
	int i;
	for (i = eaSize(&g_eaStoryArcNodes)-1; i >= 0; i--)
	{
		if (pNodeDef == GET_REF(g_eaStoryArcNodes[i]->hDef))
		{
			return i;
		}
	}
	return -1;
}

static void progression_UpdateStoryArcNode(GameProgressionNodeDef* pNodeDef)
{
	int i = progression_FindStoryRootNode(pNodeDef);

	if (pNodeDef->eFunctionalType == GameProgressionNodeFunctionalType_StoryRoot)
	{
		if (i < 0)
		{
			GameProgressionNodeRef* pRef = StructCreate(parse_GameProgressionNodeRef);
			SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNodeDef, pRef->hDef);
			eaPush(&g_eaStoryArcNodes, pRef);
			eaQSort(g_eaStoryArcNodes, progression_SortStoryRootNodes);
		}
	}
	else
	{
		if (i >= 0)
		{
			StructDestroy(parse_GameProgressionNodeRef, eaRemove(&g_eaStoryArcNodes, i));
		}
	}
}

static bool progression_UpdateDependencies(GameProgressionNodeDef* pNodeDef)
{
	bool bSuccess = true;
	int i;

	if (GET_REF(pNodeDef->hRequiredNode))
	{
		GameProgressionNodeDef* pRequiredNodeDef = GET_REF(pNodeDef->hRequiredNode);
		for (i = eaSize(&pRequiredNodeDef->eaDependentNodes)-1; i >= 0; i--)
		{
			if (pNodeDef == GET_REF(pRequiredNodeDef->eaDependentNodes[i]->hDef))
			{
				break;
			}
		}
		if (i < 0)
		{
			GameProgressionNodeRef* pRef = StructCreate(parse_GameProgressionNodeRef);
			SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNodeDef, pRef->hDef);
			eaPush(&pRequiredNodeDef->eaDependentNodes, pRef);
		}
	}

	if (eaSize(&pNodeDef->eaChildren))
	{
		int iChildCount = eaSize(&pNodeDef->eaChildren);

		for (i = 0; i < iChildCount; i++)
		{
			GameProgressionNodeDef* pChildNodeDef = GET_REF(pNodeDef->eaChildren[i]->hDef);

			if (pChildNodeDef)
			{
				// Set the parent
				SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNodeDef, pChildNodeDef->hParent);

				if (pChildNodeDef->pMissionGroupInfo)
				{
					// Set the previous node
					if (i > 0)
					{
						COPY_HANDLE(pChildNodeDef->hPrevSibling, pNodeDef->eaChildren[i-1]->hDef);
					}
					// Set the next node
					if (i < iChildCount - 1) 
					{
						COPY_HANDLE(pChildNodeDef->hNextSibling, pNodeDef->eaChildren[i+1]->hDef);
					}
				}
				else
				{
					REMOVE_HANDLE(pChildNodeDef->hPrevSibling);
					REMOVE_HANDLE(pChildNodeDef->hNextSibling);
				}

				if (i + 1 < iChildCount)
				{
					GameProgressionNodeDef* pNextChildDef = GET_REF(pNodeDef->eaChildren[i+1]->hDef);
					GameProgressionNodeDef* pRightLeaf = progression_FindRightMostLeaf(pChildNodeDef);
					GameProgressionNodeDef* pLeftLeaf = progression_FindLeftMostLeaf(pNextChildDef);
					if (pRightLeaf && pLeftLeaf)
					{
						if (pNodeDef->bBranchStory)
						{
							REMOVE_HANDLE(pRightLeaf->hNextSibling);
							REMOVE_HANDLE(pLeftLeaf->hPrevSibling);
						}
						else
						{
							SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pLeftLeaf, pRightLeaf->hNextSibling);
							SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pRightLeaf, pLeftLeaf->hPrevSibling);
						}
					}
					else
					{
						if (pRightLeaf)
							REMOVE_HANDLE(pRightLeaf->hNextSibling);
						if (pLeftLeaf)
							REMOVE_HANDLE(pLeftLeaf->hPrevSibling);
					}
				}
			}
		}
	}

#ifdef GAMESERVER
	if (pNodeDef->pMissionGroupInfo)
	{
		for (i = eaSize(&pNodeDef->pMissionGroupInfo->ppchAllowedMissionMaps)-1; i >= 0; i--)
		{
			const char* pchAllowedMap = pNodeDef->pMissionGroupInfo->ppchAllowedMissionMaps[i];
			gslProgression_AddTrackedProgressionMap(pchAllowedMap);
		}
	}
#endif

	progression_UpdateStoryArcNode(pNodeDef);

	return bSuccess;
}

static bool progression_ValidateAllButRefs(GameProgressionNodeDef* pNodeDef)
{
	bool bSuccess = true;

	if (pNodeDef->eFunctionalType == GameProgressionNodeFunctionalType_MissionGroup && !pNodeDef->pMissionGroupInfo)
	{
		ErrorFilenamef(pNodeDef->pchFilename, "Game progression node is flagged as a MissionGroup, but doesn't have any mission data");
		bSuccess = false;
	}
	else if (pNodeDef->eFunctionalType != GameProgressionNodeFunctionalType_MissionGroup && pNodeDef->pMissionGroupInfo)
	{
		ErrorFilenamef(pNodeDef->pchFilename, "Game progression node is not flagged as a MissionGroup, but has mission data");
		bSuccess = false;
	}
	if (pNodeDef->eFunctionalType != GameProgressionNodeFunctionalType_MissionGroup && IS_HANDLE_ACTIVE(pNodeDef->hRequiredNode))
	{
		ErrorFilenamef(pNodeDef->pchFilename, "Game progression node is not flagged as a MissionGroup, but has a required node");
		bSuccess = false;
	}
	if (pNodeDef->pMissionGroupInfo)
	{
		bool bAnyRequired = false;
		int i;

		if (eaSize(&pNodeDef->eaChildren))
		{
			ErrorFilenamef(pNodeDef->pchFilename, "MissionGroup nodes cannot have children");
			bSuccess = false;
		}

		// Make sure that there is at least one required mission
		for (i = eaSize(&pNodeDef->pMissionGroupInfo->eaMissions)-1; i >= 0; i--)
		{
			GameProgressionMission* pProgMission = pNodeDef->pMissionGroupInfo->eaMissions[i];
			if (!pProgMission->bOptional)
			{
				bAnyRequired = true;
			}
			if (pProgMission->bOptional && pProgMission->bSkippable)
			{
				ErrorFilenamef(pNodeDef->pchFilename, "ProgressionMission %s is marked as both optional and skippable", pProgMission->pchMissionName);
				bSuccess = false;
			}
			if (IsGameServerBasedType() && pProgMission->pExprVisible &&
				!exprGenerate(pProgMission->pExprVisible, progression_GetContext(NULL)))
			{
				bSuccess = false;
			}
		}
		if (!bAnyRequired)
		{
			ErrorFilenamef(pNodeDef->pchFilename, "Game progression node has a MissionGroupInfo with no required missions");
			bSuccess = false;
		}
	}
	return bSuccess;
}

static bool progression_ValidateMissionRefs(GameProgressionNodeDef* pNodeDef, GameProgressionMission* pProgMission)
{
	bool bSuccess = true;

	if (!pProgMission->pchMissionName)
	{
		ErrorFilenamef(pNodeDef->pchFilename, "Game progression node has a mission entry that does not specify a MissionDef");
		bSuccess = false;
	}
	else if (IsGameServerBasedType())
	{
		MissionDef* pMissionDef = missiondef_FindMissionByName(NULL, pProgMission->pchMissionName);
		if (pMissionDef)
		{
			int i, iIndex = eaFind(&pMissionDef->ppchProgressionNodes, pNodeDef->pchName);
			if (iIndex >= 0)
			{
				GameProgressionNodeDef* pRootDef = progression_GetStoryRootNode(pNodeDef);
				if (pRootDef) {
					for (i = eaSize(&pMissionDef->ppchProgressionNodes)-1; i >= 0; i--) {
						if (i != iIndex) {
							GameProgressionNodeDef* pCheckNodeDef = progression_NodeDefFromName(pMissionDef->ppchProgressionNodes[i]);
							GameProgressionNodeDef* pCheckRootDef = progression_GetStoryRootNode(pCheckNodeDef);
							if (pCheckRootDef) {
								if (GET_REF(pRootDef->hRequiredAllegiance) == GET_REF(pCheckRootDef->hRequiredAllegiance) &&
									GET_REF(pRootDef->hRequiredSubAllegiance) == GET_REF(pCheckRootDef->hRequiredSubAllegiance)) {
									ErrorFilenamef(pNodeDef->pchFilename, "Mission %s is referenced by multiple progression nodes (%s and %s) with the same allegiance requirement", 
										pProgMission->pchMissionName, pNodeDef->pchName, pCheckNodeDef->pchName);
									bSuccess = false;
									break;
								}
							}
						}
					}
				}
			}
		}
		else
		{
			ErrorFilenamef(pNodeDef->pchFilename, "Game progression mission references non-existent MissionDef '%s'", 
				pProgMission->pchMissionName);
			bSuccess = false;
		}
	}
	if (!GET_REF(pProgMission->msgDescription.hMessage) && REF_STRING_FROM_HANDLE(pProgMission->msgDescription.hMessage))
	{
		ErrorFilenamef(pNodeDef->pchFilename, "Game progression mission references non-existent description message %s", 
			REF_STRING_FROM_HANDLE(pProgMission->msgDescription.hMessage));	
		bSuccess = false;
	}
	return bSuccess;
}

static bool progression_ValidateRefs(GameProgressionNodeDef* pNodeDef)
{
	bool bSuccess = true;

	if (!GET_REF(pNodeDef->msgDisplayName.hMessage) && REF_STRING_FROM_HANDLE(pNodeDef->msgDisplayName.hMessage))
	{
		ErrorFilenamef(pNodeDef->pchFilename, "Game progression node references non-existent display message %s", 
			REF_STRING_FROM_HANDLE(pNodeDef->msgDisplayName.hMessage));
		bSuccess = false;
	}
	if (!GET_REF(pNodeDef->msgSummary.hMessage) && REF_STRING_FROM_HANDLE(pNodeDef->msgSummary.hMessage))
	{
		ErrorFilenamef(pNodeDef->pchFilename, "Game progression node references non-existent summary message %s", 
			REF_STRING_FROM_HANDLE(pNodeDef->msgSummary.hMessage));
		bSuccess = false;
	}
	if (!GET_REF(pNodeDef->msgTeaser.hMessage) && REF_STRING_FROM_HANDLE(pNodeDef->msgTeaser.hMessage))
	{
		ErrorFilenamef(pNodeDef->pchFilename, "Game progression node references non-existent teaser message %s", 
			REF_STRING_FROM_HANDLE(pNodeDef->msgTeaser.hMessage));
		bSuccess = false;
	}

	if (pNodeDef->eFunctionalType != GameProgressionNodeFunctionalType_MissionGroup)
	{
		if (IS_HANDLE_ACTIVE(pNodeDef->hRequiredAllegiance) && !REF_STRING_FROM_HANDLE(pNodeDef->hRequiredAllegiance))
		{
			ErrorFilenamef(pNodeDef->pchFilename, "Game progression node references a non-existent allegiance %s",
				REF_STRING_FROM_HANDLE(pNodeDef->hRequiredAllegiance));
			bSuccess = false;
		}

		if (IS_HANDLE_ACTIVE(pNodeDef->hRequiredSubAllegiance) && !REF_STRING_FROM_HANDLE(pNodeDef->hRequiredSubAllegiance))
		{
			ErrorFilenamef(pNodeDef->pchFilename, "Game progression node references a non-existent sub-allegiance %s",
				REF_STRING_FROM_HANDLE(pNodeDef->hRequiredSubAllegiance));
			bSuccess = false;
		}
	}
	else
	{
		if (pNodeDef->bDebug)
		{
			ErrorFilenamef(pNodeDef->pchFilename, "Game progression node is not a root node, but specifies that it is a debug node");
			bSuccess = false;
		}
		if (IS_HANDLE_ACTIVE(pNodeDef->hRequiredAllegiance))
		{
			ErrorFilenamef(pNodeDef->pchFilename, "Game progression node is not a root node, but specifies a required allegiance");
			bSuccess = false;
		}
		if (IS_HANDLE_ACTIVE(pNodeDef->hRequiredSubAllegiance))
		{
			ErrorFilenamef(pNodeDef->pchFilename, "Game progression node is not a root node, but specifies a required sub-allegiance");
			bSuccess = false;
		}
	}

	if (IS_HANDLE_ACTIVE(pNodeDef->hRequiredNode))
	{
		GameProgressionNodeDef* pRequiredNode = GET_REF(pNodeDef->hRequiredNode);
		if (pRequiredNode == NULL)
		{
			ErrorFilenamef(pNodeDef->pchFilename, "Game progression node references non-existent required node '%s'",
				REF_STRING_FROM_HANDLE(pNodeDef->hRequiredNode));
			bSuccess = false;
		}
	}

	if (IS_HANDLE_ACTIVE(pNodeDef->hNodeToWindBack))
	{
		GameProgressionNodeDef* pWindBackNode = GET_REF(pNodeDef->hRequiredNode);
		if (pWindBackNode)
		{
			bool bFound = false;
			GameProgressionNodeDef* pCurrNode = pNodeDef;
			while (pCurrNode = GET_REF(pCurrNode->hPrevSibling))
			{
				if (pCurrNode == pWindBackNode)
				{
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				ErrorFilenamef(pNodeDef->pchFilename, "Game progression node references wind-back node '%s' that isn't before it in the story",
					REF_STRING_FROM_HANDLE(pNodeDef->hNodeToWindBack));
				bSuccess = false;
			}
		}
		else
		{
			ErrorFilenamef(pNodeDef->pchFilename, "Game progression node references non-existent wind-back node '%s'",
				REF_STRING_FROM_HANDLE(pNodeDef->hNodeToWindBack));
			bSuccess = false;
		}
	}
	if (pNodeDef->pMissionGroupInfo)
	{
		int i;
		// Validate mission references
		for (i = eaSize(&pNodeDef->pMissionGroupInfo->eaMissions)-1; i >= 0; i--)
		{
			GameProgressionMission* pReqMission = pNodeDef->pMissionGroupInfo->eaMissions[i];
			if (!progression_ValidateMissionRefs(pNodeDef, pReqMission))
			{
				bSuccess = false;
			}
		}
	}

	// Validate children
	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pNodeDef->eaChildren, GameProgressionNodeRef, pNodeRef)
	{
		if (pNodeRef && GET_REF(pNodeRef->hDef) == NULL)
		{
			ErrorFilenamef(pNodeDef->pchFilename, "Game progression node references a node named '%s' that does not exist",
				REF_STRING_FROM_HANDLE(pNodeRef->hDef));
		}
	}
	FOR_EACH_END

	return bSuccess;
}

bool progression_Validate(GameProgressionNodeDef* pNodeDef)
{
	bool bSuccess = true;

	if (!progression_ValidateAllButRefs(pNodeDef))
	{
		bSuccess = false;
	}
	if (!progression_ValidateRefs(pNodeDef))
	{
		bSuccess = false;
	}
	return bSuccess;
}

static int progression_ValidateCB(enumResourceValidateType eType, const char* pcDictName, const char* pcResourceName, GameProgressionNodeDef* pNodeDef, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_FIX_FILENAME:
		{
			resFixPooledFilename(&pNodeDef->pchFilename, GAME_PROGRESSION_BASE_DIR, pNodeDef->pchScope, pNodeDef->pchName, GAME_PROGRESSION_EXT);
			return VALIDATE_HANDLED;
		}
		xcase RESVALIDATE_POST_TEXT_READING:
		{
			if (IsGameServerBasedType())
			{
				progression_ValidateAllButRefs(pNodeDef);
				return VALIDATE_HANDLED;
			}
		}
		xcase RESVALIDATE_CHECK_REFERENCES:
		{
			progression_UpdateDependencies(pNodeDef);
			if (IsGameServerBasedType() && !isProductionMode())
			{
				progression_ValidateRefs(pNodeDef);
			}
			return VALIDATE_HANDLED;
		}
	}
	return VALIDATE_NOT_HANDLED;
}

static void progression_Reload(const char* pcRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Game Progression Nodes...");

	fileWaitForExclusiveAccess(pcRelPath);
	errorLogFileIsBeingReloaded(pcRelPath);

	ParserReloadFileToDictionary(pcRelPath, g_hGameProgressionNodeDictionary);

	loadend_printf(" done (%d Game Progression Nodes)", RefSystem_GetDictionaryNumberOfReferentInfos(g_hGameProgressionNodeDictionary));
}

AUTO_RUN;
void RegisterGameProgressionNodeDictionary(void)
{
	g_hGameProgressionNodeDictionary = RefSystem_RegisterSelfDefiningDictionary("GameProgressionNodeDef", false, parse_GameProgressionNodeDef, true, true, NULL);

	resDictManageValidation(g_hGameProgressionNodeDictionary, progression_ValidateCB);

	if (IsServer()) {
		resDictProvideMissingResources(g_hGameProgressionNodeDictionary);

		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hGameProgressionNodeDictionary, ".Name", ".Scope", NULL, NULL, NULL);
		}
	} else {
		resDictRequestMissingResources(g_hGameProgressionNodeDictionary, 8, false, resClientRequestSendReferentCommand);
	}
}

static void progression_LoadNodeTypesInternal(const char *pchPath, S32 iWhen)
{
	// Create define context for the data-defined GameProgressionNodeType enum values
	if (g_pGameProgressionNodeTypes)
	{
		DefineDestroy(g_pGameProgressionNodeTypes);
	}
	g_pGameProgressionNodeTypes = DefineCreate();

	// Load game progression node types
	DefineLoadFromFile(g_pGameProgressionNodeTypes, "GameProgressionNodeType", "GameProgressionNodeType", NULL,  GAME_PROGRESSION_BASE_DIR"/GameProgressionNodeTypes.def", "GameProgressionNodeTypes.bin", GameProgressionNodeType_FirstDataDefined);
}

static void progression_LoadNodeTypes(void)
{
	progression_LoadNodeTypesInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, GAME_PROGRESSION_BASE_DIR"/GameProgressionNodeTypes.def", progression_LoadNodeTypesInternal);

#ifdef GAMECLIENT
	ui_GenInitStaticDefineVars(GameProgressionNodeTypeEnum, "GameProgressionNodeType_");
#endif
}

static void progression_LoadDefs(void)
{
	// Load game progression nodes, do not load into shared memory
	resLoadResourcesFromDisk(g_hGameProgressionNodeDictionary, GAME_PROGRESSION_BASE_DIR, "."GAME_PROGRESSION_EXT, "GameProgression.bin", PARSER_OPTIONALFLAG);

	if (isDevelopmentMode()) {
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, GAME_PROGRESSION_BASE_DIR"/*."GAME_PROGRESSION_EXT, progression_Reload);
	}
}

// Indicates whether the game progression node is unlocked by the character choice
bool progression_NodeUnlockedByCharacterChoice(PossibleCharacterChoice* pChoice, GameProgressionNodeDef* pNodeDef, bool bAllowNonMissionGroups)
{
	if (pChoice && pNodeDef)
	{
		if (bAllowNonMissionGroups && !pNodeDef->pMissionGroupInfo)
		{
			return true;
		}
		if (pNodeDef->pMissionGroupInfo)
		{
			Entity* pEnt = GET_REF(pChoice->hEnt);
			return pEnt && progression_ProgressionNodeUnlocked(pEnt, pNodeDef);
		}
	}

	return false;
}

// Indicates whether the game progression node is completed by the character choice
bool progression_NodeCompletedByCharacterChoice(PossibleCharacterChoice* pChoice, GameProgressionNodeDef* pNodeDef, bool bAllowNonMissionGroups)
{
	if (pChoice && pNodeDef)
	{
		if (bAllowNonMissionGroups && !pNodeDef->pMissionGroupInfo)
		{
			return true;
		}
		if (pNodeDef->pMissionGroupInfo)
		{
			Entity* pEnt = GET_REF(pChoice->hEnt);
			return pEnt && progression_ProgressionNodeCompleted(pEnt, pNodeDef);
		}
	}

	return false;
}

static bool progression_NodeHasUnlockedChildMissionGroupByGameSession(GameSession* pGameSession, 
																	  GameProgressionNodeDef* pNodeDef)
{
	if (pNodeDef->pMissionGroupInfo)
	{
		return progression_NodeUnlockedByGameSession(pGameSession, pNodeDef);
	}
	else
	{
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pNodeDef->eaChildren, GameProgressionNodeRef, pRef)
		{
			GameProgressionNodeDef* pChildNode = GET_REF(pRef->hDef);
			if (pChildNode && progression_NodeHasUnlockedChildMissionGroupByGameSession(pGameSession, pChildNode))
			{
				return true;
			}
		}
		FOR_EACH_END

		return false;
	}
}

// Indicates whether the node is unlocked by the game session given
bool progression_NodeUnlockedByGameSession(GameSession* pGameSession, GameProgressionNodeDef* pNodeDef)
{
	if (pGameSession && pNodeDef)
	{
		if (!pNodeDef->pMissionGroupInfo)
		{
			return progression_NodeHasUnlockedChildMissionGroupByGameSession(pGameSession, pNodeDef);
		}
		else
		{
			GameProgressionNodeDef* pPrevDef = GET_REF(pNodeDef->hPrevSibling);			

			if (pNodeDef->pMissionGroupInfo->iRequiredPlayerLevel)
			{
				bool bAtLeastOneMemberIsHighEnoughLevel = false;
				FOR_EACH_IN_EARRAY_FORWARDS(pGameSession->eaParticipants, GameSessionParticipant, pParticipant)
				{
					if (pParticipant->iExpLevel >= pNodeDef->pMissionGroupInfo->iRequiredPlayerLevel)
					{
						bAtLeastOneMemberIsHighEnoughLevel = true;
						break;
					}
				}
				FOR_EACH_END

				if (!bAtLeastOneMemberIsHighEnoughLevel)
				{
					return false;
				}
			}

			if (!progression_NodeUnlockRequiresPreviousNodeToBeCompleted(pNodeDef) || pPrevDef == NULL)
			{
				return true;
			}

			// Make sure at least one member has completed the previous node
			FOR_EACH_IN_EARRAY_FORWARDS(pGameSession->eaParticipants, GameSessionParticipant, pParticipant)
			{
				if (eaBSearch(pParticipant->ppchCompletedNodes, strCmp, pPrevDef->pchName))
				{
					return true;
				}
			}
			FOR_EACH_END

			return false;
		}
	}

	return false;
}

static bool progression_NodeAllChildMissionGroupsCompletedByGameSession(GameSession* pGameSession, 
	GameProgressionNodeDef* pNodeDef)
{
	if (pNodeDef->pMissionGroupInfo)
	{
		return progression_NodeCompletedByGameSession(pGameSession, pNodeDef);
	}
	else
	{
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pNodeDef->eaChildren, GameProgressionNodeRef, pRef)
		{
			GameProgressionNodeDef* pChildNode = GET_REF(pRef->hDef);
			if (pChildNode && !progression_NodeAllChildMissionGroupsCompletedByGameSession(pGameSession, pChildNode))
			{
				return false;
			}
		}
		FOR_EACH_END

		return true;
	}
}

// Indicates whether the node is completed by the game session given
bool progression_NodeCompletedByGameSession(GameSession* pGameSession, GameProgressionNodeDef* pNodeDef)
{
	if (pGameSession && pNodeDef)
	{
		if (!pNodeDef->pMissionGroupInfo)
		{
			return progression_NodeAllChildMissionGroupsCompletedByGameSession(pGameSession, pNodeDef);
		}
		else
		{
			// Make sure at least one member has completed the previous node
			FOR_EACH_IN_EARRAY_FORWARDS(pGameSession->eaParticipants, GameSessionParticipant, pParticipant)
			{
				if (eaBSearch(pParticipant->ppchCompletedNodes, strCmp, pNodeDef->pchName))
				{
					return true;
				}
			}
			FOR_EACH_END

			return false;
		}
	}

	return false;
}

GameProgressionNodeDef* progression_NodeDefFromName(SA_PARAM_OP_STR const char* pchName)
{
	if (pchName && pchName[0])
	{
		return (GameProgressionNodeDef*)RefSystem_ReferentFromString(g_hGameProgressionNodeDictionary, pchName);
	}
	return NULL;
}

// Check to see if the player meets the requirements for a progression mission
AUTO_TRANS_HELPER;
bool progression_trh_CheckMissionRequirementsEx(ATH_ARG NOCONST(Entity)* pPlayerEnt, GameProgressionNodeDef* pNodeDef, int iProgressionMissionIndex)
{
	if (NONNULL(pPlayerEnt) && 
		pNodeDef &&
		pNodeDef->pMissionGroupInfo &&
		g_GameProgressionConfig.bValidateProgression)
	{
		int iIndex = iProgressionMissionIndex;
		GameProgressionNodeDef* pProgMissionNodeDef = NULL;
		GameProgressionMission* pCurrProgMission = pNodeDef->pMissionGroupInfo->eaMissions[iIndex];
		GameProgressionMission* pProgMission = NULL;

		if (iIndex >= 0 && !progression_trh_IsMissionOptional(pPlayerEnt, pCurrProgMission))
		{
			bool bCompletedMission = false;

			// For backwards compatibility, consider the player having met the requirements for this progression mission if they have already completed it
			if (NONNULL(pPlayerEnt->pPlayer) && NONNULL(pPlayerEnt->pPlayer->missionInfo))
			{
				if (eaIndexedFindUsingString(&pPlayerEnt->pPlayer->missionInfo->completedMissions, pCurrProgMission->pchMissionName) >= 0)
				{
					bCompletedMission = true;
				}
			}
			if (!bCompletedMission)
			{
				int i;
				for (i = iIndex-1; i >= 0; i--)
				{
					if (!progression_trh_IsMissionOptional(pPlayerEnt, pNodeDef->pMissionGroupInfo->eaMissions[i]))
					{
						pProgMission = pNodeDef->pMissionGroupInfo->eaMissions[i];
						pProgMissionNodeDef = pNodeDef;
						break;
					}
				}
				if (!pProgMission)
				{
					GameProgressionNodeDef* pPrevNodeDef = GET_REF(pNodeDef->hPrevSibling);
					if (pPrevNodeDef && pPrevNodeDef->pMissionGroupInfo)
					{
						for (i = eaSize(&pPrevNodeDef->pMissionGroupInfo->eaMissions)-1; i >= 0; i--)
						{
							if (!progression_trh_IsMissionOptional(pPlayerEnt, pPrevNodeDef->pMissionGroupInfo->eaMissions[i]))
							{
								pProgMission = pPrevNodeDef->pMissionGroupInfo->eaMissions[i];
								pProgMissionNodeDef = pPrevNodeDef;
								break;
							}
						}
					}
				}
			}
		}

		// Check to see if the previous mission is completed
		if (pProgMission && pProgMissionNodeDef)
		{
			if (!progression_trh_IsMissionCompleteForNode(pPlayerEnt, pProgMission->pchMissionName, pProgMissionNodeDef))
			{
				return false;
			}
		}
	}
	return true;
}

// Check to see if the player meets the requirements for a progression mission
bool progression_trh_CheckMissionRequirements(ATH_ARG NOCONST(Entity)* pPlayerEnt, MissionDef* pMissionDef)
{
	if (NONNULL(pPlayerEnt) && pMissionDef && g_GameProgressionConfig.bValidateProgression)
	{
		GameProgressionNodeDef* pNodeDef = progression_trh_GetNodeFromMissionDef(pPlayerEnt, pMissionDef, NULL);
		int iMissionIndex = progression_FindMissionForNode(pNodeDef, pMissionDef->name);
		return progression_trh_CheckMissionRequirementsEx(pPlayerEnt, pNodeDef, iMissionIndex);
	}
	return true;
}

AUTO_TRANS_HELPER;
bool progression_trh_IsMissionOptional(ATH_ARG NOCONST(Entity)* pEnt, GameProgressionMission* pProgMission)
{
	if (pProgMission && !pProgMission->bOptional)
	{
		if (pProgMission->bSkippable &&
			NONNULL(pEnt) &&
			NONNULL(pEnt->pPlayer) &&
			NONNULL(pEnt->pPlayer->pProgressionInfo))
		{
			int iIndex = (int)eaBFind(pEnt->pPlayer->pProgressionInfo->ppchSkippedMissions, strCmp, pProgMission->pchMissionName);

			if (pProgMission->pchMissionName == eaGet(&pEnt->pPlayer->pProgressionInfo->ppchSkippedMissions, iIndex))
			{
				return true;
			}
		}
		return false;
	}
	return true;
}

AUTO_TRANS_HELPER;
bool progression_trh_IsMissionSkippable(ATH_ARG NOCONST(Entity)* pEnt, GameProgressionNodeDef* pNodeDef, int iProgMissionIdx)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && pNodeDef && pNodeDef->pMissionGroupInfo)
	{
		GameProgressionMission* pProgMission = eaGet(&pNodeDef->pMissionGroupInfo->eaMissions, iProgMissionIdx);
		
		if (pProgMission && pProgMission->bSkippable)
		{
			if (NONNULL(pEnt->pPlayer->missionInfo))
			{
				NOCONST(CompletedMission)* pCompletedMission;
				pCompletedMission = eaIndexedGetUsingString(&pEnt->pPlayer->missionInfo->completedMissions, pProgMission->pchMissionName);
				
				// Don't allow a player to skip a mission if the mission is already complete
				if (NONNULL(pCompletedMission))
				{
					return false;
				}
			}
			if (!g_GameProgressionConfig.bMustMeetRequirementsToSkipMissions ||
				progression_trh_CheckMissionRequirementsEx(pEnt, pNodeDef, iProgMissionIdx))
			{
				return true;
			}
		}
	}
	return false;
}

// Finds the game progression node which requires the given mission
AUTO_TRANS_HELPER;
GameProgressionNodeDef* progression_trh_GetNodeFromMissionDef(ATH_ARG NOCONST(Entity)* pEnt, MissionDef* pMissionDef, GameProgressionMission** ppProgMission)
{
	if (pMissionDef)
	{
		GameProgressionNodeDef* pNodeDef = NULL;
		int i;
		for (i = eaSize(&pMissionDef->ppchProgressionNodes)-1; i >= 0; i--)
		{
			GameProgressionNodeDef* pCheckNodeDef = progression_NodeDefFromName(pMissionDef->ppchProgressionNodes[i]);
			GameProgressionNodeDef* pCheckRootNodeDef = progression_GetStoryRootNode(pCheckNodeDef);
			AllegianceDef* pAllegianceDef = SAFE_GET_REF(pCheckRootNodeDef, hRequiredAllegiance);
			AllegianceDef* pSubAllegianceDef = SAFE_GET_REF(pCheckRootNodeDef, hRequiredSubAllegiance);

			if (!pAllegianceDef || (NONNULL(pEnt) &&
				pAllegianceDef == GET_REF(pEnt->hAllegiance) &&
				pSubAllegianceDef == GET_REF(pEnt->hSubAllegiance)))
			{
				pNodeDef = pCheckNodeDef;
				break;
			}
		}
		if (pNodeDef && ppProgMission)
		{
			i = progression_FindMissionForNode(pNodeDef, pMissionDef->name);
			if (i >= 0)
			{
				(*ppProgMission) = pNodeDef->pMissionGroupInfo->eaMissions[i];
			}
		}
		return pNodeDef;
	}
	return NULL;
}

// Finds the game progression node which requires the given mission
AUTO_TRANS_HELPER;
GameProgressionNodeDef* progression_trh_GetNodeFromMissionName(ATH_ARG NOCONST(Entity)* pEnt, const char* pchMissionName, GameProgressionMission** ppProgMission)
{
	if (pchMissionName && pchMissionName[0])
	{
		MissionDef* pMissionDef = RefSystem_ReferentFromString(g_MissionDictionary, pchMissionName);

		return progression_trh_GetNodeFromMissionDef(pEnt, pMissionDef, ppProgMission);
	}
	return NULL;
}

GameProgressionNodeDef* progression_GetStoryRootNode(SA_PARAM_OP_VALID GameProgressionNodeDef* pNodeDef)
{
	if (pNodeDef)
	{
		do {
			if (pNodeDef->eFunctionalType == GameProgressionNodeFunctionalType_StoryRoot)
			{
				return pNodeDef;
			}
		} while (pNodeDef = GET_REF(pNodeDef->hParent));
	}
	return NULL;
}

GameProgressionNodeDef* progression_GetStoryBranchNode(SA_PARAM_OP_VALID GameProgressionNodeDef* pNodeDef)
{
	if (pNodeDef)
	{
		do {
			if (pNodeDef->eFunctionalType == GameProgressionNodeFunctionalType_StoryRoot)
			{
				if (pNodeDef->bBranchStory)
				{
					return NULL;
				}
				return pNodeDef;
			}
			else if (pNodeDef->eFunctionalType == GameProgressionNodeFunctionalType_StoryGroup)
			{
				GameProgressionNodeDef* pParentNode = GET_REF(pNodeDef->hParent);
				if (pParentNode && pParentNode->bBranchStory)
				{
					return pNodeDef;
				}
			}
		} while (pNodeDef = GET_REF(pNodeDef->hParent));
	}
	return NULL;
}

GameProgressionNodeDef* progression_FindRightMostLeaf(GameProgressionNodeDef* pNodeDef)
{
	GameProgressionNodeDef* pCurrNodeDef = pNodeDef;

	while (pCurrNodeDef && eaSize(&pCurrNodeDef->eaChildren))
	{
		GameProgressionNodeRef* pRef = eaGetLast(&pCurrNodeDef->eaChildren);
		pCurrNodeDef = SAFE_GET_REF(pRef, hDef);
	}
	return pCurrNodeDef;
}

GameProgressionNodeDef* progression_FindLeftMostLeaf(GameProgressionNodeDef* pNodeDef)
{
	GameProgressionNodeDef* pCurrNodeDef = pNodeDef;

	while (pCurrNodeDef && eaSize(&pCurrNodeDef->eaChildren))
	{
		GameProgressionNodeRef* pRef = eaGet(&pCurrNodeDef->eaChildren, 0);
		pCurrNodeDef = SAFE_GET_REF(pRef, hDef);
	}
	return pCurrNodeDef;
}

// Find a required mission in a progression node by def
int progression_FindMissionForNode(GameProgressionNodeDef* pNodeDef, const char* pchMissionName)
{
	pchMissionName = allocFindString(pchMissionName);

	if (pNodeDef && pNodeDef->pMissionGroupInfo && pchMissionName && pchMissionName[0])
	{
		int i;
		for (i = eaSize(&pNodeDef->pMissionGroupInfo->eaMissions)-1; i >= 0; i--)
		{
			GameProgressionMission* pNodeMission = pNodeDef->pMissionGroupInfo->eaMissions[i];
			if (pchMissionName == pNodeMission->pchMissionName)
			{
				return i;
			}
		}
	}
	return -1;
}

static void progression_LoadConfigInternal(const char* pchPath, S32 iWhen)
{
	StructReset(parse_GameProgressionConfig, &g_GameProgressionConfig);

	loadstart_printf("Loading Game Progression Config... ");

	ParserLoadFiles(NULL, 
		"defs/config/GameProgressionConfig.def", 
		"GameProgressionConfig.bin", 
		PARSER_OPTIONALFLAG, 
		parse_GameProgressionConfig,
		&g_GameProgressionConfig);

	loadend_printf(" done.");
}

// Load game-specific configuration settings
static void progression_LoadConfig(void)
{
	progression_LoadConfigInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/GameProgressionConfig.def", progression_LoadConfigInternal);
}

AUTO_STARTUP(AS_GameProgression);
void progression_Load(void)
{
	progression_LoadConfig();
	progression_LoadNodeTypes();
	progression_LoadDefs();
}

// Returns the progression info for the player
ProgressionInfo* progression_GetInfoFromPlayer(const Entity* pEnt)
{
	return SAFE_MEMBER2(pEnt, pPlayer, pProgressionInfo);
}

bool progression_NodeUnlockedByAnyTeamMember(Team* pTeam, GameProgressionNodeDef* pNodeDef)
{
	if (pNodeDef->pMissionGroupInfo == NULL)
	{
		return false;
	}
	else
	{
		GameProgressionNodeDef* pPrevNode = GET_REF(pNodeDef->hPrevSibling);
		GameProgressionNodeDef* pRequiredNode = GET_REF(pNodeDef->hRequiredNode);

		if (pNodeDef->pMissionGroupInfo->iRequiredPlayerLevel)
		{
			bool bAtLeastOneMemberIsHighEnoughLevel = false;

			FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
			{
				Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
				if (!pEnt)
				{
					pEnt = GET_REF(pTeamMember->hEnt);
				}

				if (pEnt && entity_GetSavedExpLevel(pEnt) >= pNodeDef->pMissionGroupInfo->iRequiredPlayerLevel)
				{
					bAtLeastOneMemberIsHighEnoughLevel = true;
					break;
				}
			}
			FOR_EACH_END

			if (!bAtLeastOneMemberIsHighEnoughLevel)
			{
				return false;
			}
		}

		if (pRequiredNode && !progression_NodeCompletedByAnyTeamMember(pTeam, pRequiredNode))
		{
			return false;
		}
		if (progression_NodeUnlockRequiresPreviousNodeToBeCompleted(pNodeDef) && 
			pPrevNode && !progression_NodeCompletedByAnyTeamMember(pTeam, pPrevNode))
		{
			return false;
		}
		return true;
	}
}

bool progression_NodeCompletedByAnyTeamMember(Team* pTeam, GameProgressionNodeDef* pNodeDef)
{
	if (pNodeDef->pMissionGroupInfo)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
		{
			Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
			if (!pEnt)
			{
				pEnt = GET_REF(pTeamMember->hEnt);
			}

			if (pEnt && progression_trh_ProgressionNodeCompleted(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), pNodeDef))
			{
				return true;
			}
		}
		FOR_EACH_END
	}
	return false;
}

bool progression_NodeCompletedByTeam(Team* pTeam, GameProgressionNodeDef* pNodeDef)
{
	if (pNodeDef->pMissionGroupInfo == NULL)
	{
		return false;
	}
	FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
	{
		ProgressionInfo* pInfo;
		Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
		if (!pEnt)
		{
			pEnt = GET_REF(pTeamMember->hEnt);
		}
		pInfo = progression_GetInfoFromPlayer(pEnt);

		if (pInfo && pInfo->pTeamData && eaSize(&pInfo->pTeamData->ppchCompletedMissions) > 0)
		{
			bool bCompleted = true;
			FOR_EACH_IN_EARRAY_FORWARDS(pNodeDef->pMissionGroupInfo->eaMissions, GameProgressionMission, pProgMission)
			{
				if (!pProgMission->bOptional)
				{
					int iIndex = (int)eaBFind(pInfo->pTeamData->ppchCompletedMissions, strCmp, pProgMission->pchMissionName);
					
					if (pProgMission->pchMissionName != eaGet(&pInfo->pTeamData->ppchCompletedMissions, iIndex))
					{
						bCompleted = false;
						break;
					}
				}
			}
			FOR_EACH_END

			if (bCompleted)
			{
				return true;
			}
		}
	}
	FOR_EACH_END

	return false;
}

static GameProgressionNodeDef* progression_StoryWindBackCheckTeam(Entity* pEnt, 
																  GameProgressionNodeDef* pNodeJustCompleted, 
																  const char*** pppchMissionsToUncomplete)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pProgressionInfo && pEnt->pPlayer->pProgressionInfo->pTeamData && pNodeJustCompleted)
	{
		ProgressionInfo* pProgressInfo = pEnt->pPlayer->pProgressionInfo;
		GameProgressionNodeDef* pNodeToWindBack = GET_REF(pNodeJustCompleted->hNodeToWindBack);
		GameProgressionNodeDef* pCurrNodeDef = pNodeJustCompleted;
		
		if (pNodeToWindBack)
		{
			GameProgressionNodeDef* pLastValidNode = NULL;
			while (pCurrNodeDef && pCurrNodeDef->pMissionGroupInfo)
			{
				pLastValidNode = pCurrNodeDef;

				// Remove the missions which are required by this node from the completed mission list
				FOR_EACH_IN_EARRAY_FORWARDS(pCurrNodeDef->pMissionGroupInfo->eaMissions, GameProgressionMission, pProgMission)
				{
					if (pProgMission->bOptional)
					{
						continue;
					}
					eaPush(pppchMissionsToUncomplete, pProgMission->pchMissionName);
				}
				FOR_EACH_END

				if (pCurrNodeDef == pNodeToWindBack)
				{
					break;
				}
				pCurrNodeDef = GET_REF(pCurrNodeDef->hPrevSibling);
			}
			return pLastValidNode;
		}
	}

	return NULL;
}

static ProgressionUpdateParams* progression_AdvanceTeamProgress(Team* pTeam, Entity* pLeader)
{
	ProgressionUpdateParams* pUpdateParams = NULL;
	TeamProgressionData* pTeamData = progression_GetCurrentTeamProgress(pTeam);
	ProgressionInfo* pProgressInfo = progression_GetInfoFromPlayer(pLeader);
	bool bSuccess = false;

	if (pTeamData)
	{
		GameProgressionNodeDef* pWindbackNode = NULL;
		GameProgressionNodeDef* pNodeToAdvanceTo = NULL;
		GameProgressionNodeDef* pCurrNode = GET_REF(pTeamData->hNode);
		const char** ppchMissionsToUncomplete = NULL;

		while (pCurrNode)
		{
			// The current progress is valid. Now is the time to see if we can progress to the next node.
			if (progression_NodeCompletedByTeam(pTeam, pCurrNode))
			{
				if (NONNULL(pLeader->pTeam))
				{
					if (pWindbackNode = progression_StoryWindBackCheckTeam(pLeader, pCurrNode, &ppchMissionsToUncomplete))
					{
						break;
					}
				}
				pNodeToAdvanceTo = GET_REF(pCurrNode->hNextSibling);
			}
			pCurrNode = GET_REF(pCurrNode->hNextSibling);
		}

		if (pNodeToAdvanceTo || pWindbackNode)
		{
			GameProgressionNodeDef* pNodeDef = pNodeToAdvanceTo ? pNodeToAdvanceTo : pWindbackNode;
			GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pNodeDef);

			if (pBranchNodeDef)
			{
				// Return the params, indicating that the team progression update transaction should be called
				pUpdateParams = StructCreate(parse_ProgressionUpdateParams);
				SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNodeDef, pUpdateParams->hOverrideNode);
				pUpdateParams->iOverrideTime = timeSecondsSince2000();
				eaCopy(&pUpdateParams->ppchMissionsToUncomplete, &ppchMissionsToUncomplete);
			}
		}
		eaDestroy(&ppchMissionsToUncomplete);
	}

	return pUpdateParams;
}

static void progression_UpdateTeamProgression_CB(TransactionReturnVal* pReturn, void* pData)
{
#if defined(APPSERVER) || defined(GAMESERVER)
	Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, (uintptr_t)pData);
	ProgressionInfo* pInfo = progression_GetInfoFromPlayer(pEnt);

	if (pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		progression_UpdateCurrentProgression(pEnt);
	}
	if (pInfo)
	{
		pInfo->bTeamTransactionPending = false;
	}
#endif
}

#ifdef GAMESERVER

static void progression_SendStoryIntroNotificationToPlayer(SA_PARAM_NN_VALID Entity *pEnt, GameProgressionNodeDef *pNode)
{
	char *estrFormattedMessage = NULL;
	FormatDisplayMessage(&estrFormattedMessage, 
		g_GameProgressionConfig.msgStoryIntroNotification, 
		STRFMT_STRING("DisplayNameText", TranslateDisplayMessage(pNode->msgDisplayName)),
		STRFMT_STRING("SummaryText", TranslateDisplayMessage(pNode->msgSummary)),
		STRFMT_STRING("TeaserText", TranslateDisplayMessage(pNode->msgTeaser)),
		STRFMT_END);
	notify_NotifySend(pEnt, kNotifyType_StoryIntro, estrFormattedMessage, pNode->pchName, NULL);
	estrDestroy(&estrFormattedMessage);
}

// This is called when a player logs in to a map
void progression_PlayerLoggedIn(SA_PARAM_NN_VALID Entity *pEnt, bool bJustLoggedIn)
{
	if (g_GameProgressionConfig.bSendStoryIntroNotificationOnMapEnter && bJustLoggedIn)
	{
		ProgressionInfo *pInfo = progression_GetInfoFromPlayer(pEnt);	

		if (pInfo)
		{
			pInfo->bEvaluateStoryNotificationSending = true;
		}
	}
}

// Player tick function for the progression system
void progression_ProcessPlayer(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID ProgressionInfo *pInfo)
{
	// Make sure the entity does not have the ignore flag. This makes sure the notification is sent when the player is done with loading
	if (pInfo->bEvaluateStoryNotificationSending && !entCheckFlag(pEnt, ENTITYFLAG_IGNORE))
	{
		GameProgressionNodeDef *pNodeDef = GET_REF(pInfo->hMostRecentlyPlayedNode);

		pInfo->bEvaluateStoryNotificationSending = false;

		if (pNodeDef && pEnt->pPlayer)
		{
			// Send the notification
			progression_SendStoryIntroNotificationToPlayer(pEnt, pNodeDef);
		}
	}
}
#endif

// Returns the first required mission name for a game progression node
const char * progression_GetFirstRequiredMissionNameByNode(SA_PARAM_NN_VALID GameProgressionNodeDef *pNode)
{
	MissionDef *pFirstRequiredMissionDef = NULL;

	devassert(pNode->pMissionGroupInfo);

	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pNode->pMissionGroupInfo->eaMissions, GameProgressionMission, pMission)
	{
		if (pMission && !pMission->bOptional)
		{
			return pMission->pchMissionName;
		}
	}
	FOR_EACH_END

	return NULL;
}

// Returns the first required mission for a game progression node
MissionDef * progression_GetFirstRequiredMissionByNode(SA_PARAM_NN_VALID GameProgressionNodeDef *pNode)
{
	const char *pchFirstRequiredMissionName = progression_GetFirstRequiredMissionNameByNode(pNode);
	return pchFirstRequiredMissionName ? missiondef_FindMissionByName(NULL, pchFirstRequiredMissionName) : NULL;
}

void progression_ProcessTeamProgression(Entity* pEnt, bool bEvaluateTeamStoryAdvancement)
{
	ProgressionUpdateParams* pUpdateParams = NULL;
	U32 iTimeNow = timeSecondsSince2000();
	bool bCanValidateTeamProgress = true;
	ProgressionInfo* pProgressionInfo = progression_GetInfoFromPlayer(pEnt);

	// See if the player is with a team
	Team* pTeam = team_GetTeam(pEnt);

	// Most recent story override by another team member
	TeamProgressionData* pMostRecentTeamData = NULL;

	if (!pProgressionInfo || pProgressionInfo->bTeamTransactionPending)
		return;

	// If the player was on a team, clean up the team progression data
	if (!team_IsWithTeam(pEnt) && pProgressionInfo->pTeamData)
	{
#if defined(APPSERVER) || defined(GAMESERVER)
		GameProgressionNodeDef* pNodeDef = GET_REF(pProgressionInfo->pTeamData->hNode);
		GameProgressionNodeDef* pBranchNodeDef = GET_REF(pProgressionInfo->pTeamData->hStoryArcNode);
		if (pBranchNodeDef &&
			g_GameProgressionConfig.bAllowReplay &&
			progression_ProgressionNodeUnlocked(pEnt, pNodeDef))
		{
			// Since the player completed the node, we want his team progress to override his personal progress when they leave the team.
			ProgressionTrackingData* pTrackingData = eaIndexedGetUsingString(&pProgressionInfo->eaTrackingData, pBranchNodeDef->pchName);

			if (!pTrackingData || pNodeDef != GET_REF(pTrackingData->hNode))
			{
				TransactionReturnVal* pReturn = objCreateManagedReturnVal(progression_UpdateTeamProgression_CB, (void *)(intptr_t)entGetContainerID(pEnt));
				pUpdateParams = StructCreate(parse_ProgressionUpdateParams);
				COPY_HANDLE(pUpdateParams->hOverrideNode, pProgressionInfo->pTeamData->hNode);
				pUpdateParams->iOverrideTime = iTimeNow;
				pUpdateParams->bDestroyTeamData = true;
				AutoTrans_progression_tr_SetProgression(pReturn, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pUpdateParams);
				pProgressionInfo->bTeamTransactionPending = true;
			}
		}
		if (!pProgressionInfo->bTeamTransactionPending)
		{
			TransactionReturnVal* pReturn = objCreateManagedReturnVal(progression_UpdateTeamProgression_CB, (void *)(intptr_t)entGetContainerID(pEnt));
			pUpdateParams = StructCreate(parse_ProgressionUpdateParams);
			pUpdateParams->bDestroyTeamData = true;
			AutoTrans_progression_tr_UpdateTeamProgress(pReturn, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pUpdateParams);
			pProgressionInfo->bTeamTransactionPending = true;
		}
		
#endif
	}
	if (pTeam && g_GameProgressionConfig.bEnableTeamProgressionTracking)
	{
		if (bEvaluateTeamStoryAdvancement && team_IsTeamLeader(pEnt))
		{
			// See if the team progress need to advance
			if (pUpdateParams = progression_AdvanceTeamProgress(pTeam, pEnt))
			{
				bCanValidateTeamProgress = false;
			}
		}

		// We want to detect if we need to update the progress override. To do this
		// we compare the progress override in all team members and use the latest
		// progress override.
		FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
		{
			if (pTeamMember && pTeamMember->iEntID != entGetContainerID(pEnt))
			{
				Entity* pTeamMemberEnt = GET_REF(pTeamMember->hEnt);
				ProgressionInfo* pTeamMemberInfo = pTeamMemberEnt ? progression_GetInfoFromPlayer(pTeamMemberEnt) : NULL;

				if (pTeamMemberInfo &&
					pTeamMemberInfo->pTeamData)
				{
					if (pMostRecentTeamData == NULL || 
						pTeamMemberInfo->pTeamData->iLastUpdated > pMostRecentTeamData->iLastUpdated)
					{
						pMostRecentTeamData = pTeamMemberInfo->pTeamData;
					}
				}
			}
		}
		FOR_EACH_END

		if (pMostRecentTeamData &&
			pProgressionInfo->pTeamData &&
			(pProgressionInfo->pTeamData->iLastUpdated > pMostRecentTeamData->iLastUpdated ||
			(GET_REF(pProgressionInfo->pTeamData->hStoryArcNode) == GET_REF(pMostRecentTeamData->hStoryArcNode) &&
			REF_COMPARE_HANDLES(pProgressionInfo->pTeamData->hNode, pMostRecentTeamData->hNode))))
		{
			// No need to update the override
			pMostRecentTeamData = NULL;
		}
	}

	if (bCanValidateTeamProgress && pTeam && team_IsTeamLeader(pEnt) && pMostRecentTeamData == NULL && pProgressionInfo->pTeamData)
	{
		// Validate the current progression
		GameProgressionNodeDef* pNodeDef = GET_REF(pProgressionInfo->pTeamData->hNode);

		if (pNodeDef && !progression_NodeUnlockedByAnyTeamMember(pTeam, pNodeDef))
		{
			// Invalidate the progression
			pUpdateParams = StructCreate(parse_ProgressionUpdateParams);
			pUpdateParams->iOverrideTime = iTimeNow;
		}
	}

	if (bCanValidateTeamProgress && pMostRecentTeamData)
	{
		pUpdateParams = StructCreate(parse_ProgressionUpdateParams);
		COPY_HANDLE(pUpdateParams->hOverrideNode, pMostRecentTeamData->hNode);
		pUpdateParams->iOverrideTime = pMostRecentTeamData->iLastUpdated;
		eaCopy(&pUpdateParams->ppchMissionsToUncomplete, &pMostRecentTeamData->ppchMissionsToUncomplete);
	}	

	if (pUpdateParams) 
	{
#if defined(APPSERVER) || defined(GAMESERVER)
		// We only need to update the team progress
		TransactionReturnVal* pReturn = objCreateManagedReturnVal(progression_UpdateTeamProgression_CB, (void *)(intptr_t)entGetContainerID(pEnt));
		AutoTrans_progression_tr_UpdateTeamProgress(pReturn, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pUpdateParams);
		pProgressionInfo->bTeamTransactionPending = true;
#endif
	}

	StructDestroySafe(parse_ProgressionUpdateParams, &pUpdateParams);
}

TeamProgressionData* progression_GetCurrentTeamProgress(Team* pTeam)
{
	TeamProgressionData* pTeamData = NULL;

	FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
	{
		Entity* pTeamMemberEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
		if (pTeamMemberEnt == NULL)
		{
			pTeamMemberEnt = GET_REF(pTeamMember->hEnt);
		}
		if (pTeamMemberEnt)
		{
			ProgressionInfo* pInfo = progression_GetInfoFromPlayer(pTeamMemberEnt);
			if (pInfo &&
				pInfo->pTeamData &&
				(pTeamData == NULL || pInfo->pTeamData->iLastUpdated > pTeamData->iLastUpdated))
			{
				pTeamData = pInfo->pTeamData;
			}
		}
	}
	FOR_EACH_END

	if (pTeamData && !GET_REF(pTeamData->hStoryArcNode))
	{
		pTeamData = NULL;
	}
	return pTeamData;
}

GameProgressionNodeDef* progression_GetCurrentProgress(Entity* pEnt, GameProgressionNodeDef* pNodeDef)
{
	ProgressionInfo* pInfo = progression_GetInfoFromPlayer(pEnt);
	GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pNodeDef);

	if (pInfo && pBranchNodeDef)
	{
		if (pInfo->pTeamData)
		{
			if (GET_REF(pInfo->pTeamData->hStoryArcNode) == pBranchNodeDef)
			{
				return GET_REF(pInfo->pTeamData->hNode);
			}
		}
		else
		{
			ReplayProgressionData* pReplayData = eaIndexedGetUsingString(&pInfo->eaReplayData, pBranchNodeDef->pchName);
			if (pReplayData)
			{
				return GET_REF(pReplayData->hNode);
			}
			else
			{
				GameProgressionNodeDef *pStoryNodeDef = progression_GetStoryRootNode(pNodeDef);

				if (pStoryNodeDef && !pStoryNodeDef->bDontAdvanceStoryAutomatically)
				{
					ProgressionTrackingData* pData = eaIndexedGetUsingString(&pInfo->eaTrackingData, pBranchNodeDef->pchName);
					if (pData)
					{
						return GET_REF(pData->hNode);
					}
					else
					{
						// Find the first mission group in the story
						GameProgressionNodeDef* pFirstNode = progression_FindLeftMostLeaf(pBranchNodeDef);

						if (pFirstNode && pFirstNode->eFunctionalType == GameProgressionNodeFunctionalType_MissionGroup && progression_ProgressionNodeUnlocked(pEnt, pFirstNode))
						{
							return pFirstNode;
						}
					}
				}
			}
		}
	}
	return NULL;
}


static ProgressionTrackingData* progression_GetOrCreateTrackingDataForBranch(ProgressionInfo* pProgressInfo, 
																			 GameProgressionNodeDef* pBranchNodeDef)
{
	ProgressionTrackingData* pData = eaIndexedGetUsingString(&pProgressInfo->eaTrackingData, pBranchNodeDef->pchName);
	if (!pData)
	{
		pData = StructCreate(parse_ProgressionTrackingData);
		SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pBranchNodeDef, pData->hStoryArcNode);
		eaIndexedEnable(&pProgressInfo->eaTrackingData, parse_ProgressionTrackingData);
		eaIndexedAdd(&pProgressInfo->eaTrackingData, pData);
	}
	return pData;
}

static bool progression_UpdateProgressionForBranch(Entity* pEnt, GameProgressionNodeDef* pBranchNodeDef)
{
	ProgressionInfo* pProgressInfo = progression_GetInfoFromPlayer(pEnt);
	bool bUpdated = false;

	if (pProgressInfo && pBranchNodeDef)
	{
		ReplayProgressionData* pReplayData = eaIndexedGetUsingString(&pProgressInfo->eaReplayData, pBranchNodeDef->pchName);
		if (pReplayData)
		{
			ProgressionTrackingData* pData = progression_GetOrCreateTrackingDataForBranch(pProgressInfo, pBranchNodeDef);
			if (GET_REF(pData->hNode) != GET_REF(pReplayData->hNode))
			{
				COPY_HANDLE(pData->hNode, pReplayData->hNode);
				bUpdated = true;
			}
		}
		else
		{
			GameProgressionNodeDef *pStoryRootNodeDef = progression_GetStoryRootNode(pBranchNodeDef);
			GameProgressionNodeDef *pCurrNodeDef;
			GameProgressionNodeDef *pProgressNodeDef = NULL;
			bool bStoryArcComplete = false;

			if (pStoryRootNodeDef && pStoryRootNodeDef->bDontAdvanceStoryAutomatically)
			{
				return false;
			}

			pCurrNodeDef = progression_FindLeftMostLeaf(pBranchNodeDef);

			while (pCurrNodeDef)
			{
				if (!progression_ProgressionNodeUnlocked(pEnt, pCurrNodeDef))
				{
					break;
				}
				if (pCurrNodeDef)
				{
					pProgressNodeDef = pCurrNodeDef;
				}
				if (!GET_REF(pCurrNodeDef->hNextSibling) && progression_ProgressionNodeCompleted(pEnt, pCurrNodeDef))
				{
					bStoryArcComplete = true;
					pProgressNodeDef = NULL;
					break;
				}
				pCurrNodeDef = GET_REF(pCurrNodeDef->hNextSibling);
			}

			if (pProgressNodeDef || bStoryArcComplete)
			{
				ProgressionTrackingData* pData = progression_GetOrCreateTrackingDataForBranch(pProgressInfo, pBranchNodeDef);
				SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pProgressNodeDef, pData->hNode);
				bUpdated = true;
			}
		}
	}
	return bUpdated;
}

static bool progression_UpdateProgressionForArc(Entity* pEnt, GameProgressionNodeDef* pNodeDef)
{
	GameProgressionNodeDef* pRootNodeDef = progression_GetStoryRootNode(pNodeDef);

	if (!progression_IsValidStoryArcForPlayer(pEnt, pRootNodeDef))
	{
		return false;
	}
	if (pRootNodeDef->bBranchStory)
	{
		bool bUpdated = false;
		int i;
		for (i = 0; i < eaSize(&pRootNodeDef->eaChildren); i++)
		{
			bUpdated |= progression_UpdateProgressionForBranch(pEnt, GET_REF(pRootNodeDef->eaChildren[i]->hDef));
		}
		return bUpdated;
	}
	return progression_UpdateProgressionForBranch(pEnt, pRootNodeDef);
}

static bool progression_UpdateCurrentProgressionNodes(Entity* pEnt)
{
	bool bUpdated = false;
	int i, iNodes = eaSize(&g_eaStoryArcNodes);

	for (i = 0; i < iNodes; i++)
	{
		GameProgressionNodeDef* pNodeDef = GET_REF(g_eaStoryArcNodes[i]->hDef);
		if (progression_UpdateProgressionForArc(pEnt, pNodeDef))
		{
			bUpdated = true;
		}
	}
	return bUpdated;
}

// Update the current non-persisted progression information for the player
void progression_UpdateCurrentProgression(Entity* pEnt)
{
	ProgressionInfo* pProgressInfo = progression_GetInfoFromPlayer(pEnt);

	if (pProgressInfo)
	{
		bool bDirty = false;

		if (eaSize(&pProgressInfo->eaTrackingData))
		{
			eaClearStruct(&pProgressInfo->eaTrackingData, parse_ProgressionTrackingData);
			bDirty = true;
		}
		// Traverse the Story Arc list and add arcs that the player is currently playing
		if (progression_UpdateCurrentProgressionNodes(pEnt))
		{
			bDirty = true;
		}

		if (bDirty)
		{
			// Set dirty bits
			entity_SetDirtyBit(pEnt, parse_ProgressionInfo, pProgressInfo, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, true);
		}
	}
}

// Indicates whether the node requires the player to complete the previous node in order to be unlocked
bool progression_NodeUnlockRequiresPreviousNodeToBeCompleted(SA_PARAM_OP_VALID GameProgressionNodeDef *pNodeDef)
{
	if (pNodeDef)
	{
		// Get the story root node
		GameProgressionNodeDef *pStoryRootNode = progression_GetStoryRootNode(pNodeDef);

		return pStoryRootNode == NULL || !pStoryRootNode->bMissionGroupsAreUnlockedByDefault;
	}
	return true;
}

#include "progression_common_h_ast.c"