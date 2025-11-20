/***************************************************************************
 *     Copyright (c) 2006-, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/

#include "missionui_eval.h"
#include "Character.h"
#include "EString.h"
#include "EntitySavedData.h"
#include "expression.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "gclEntity.h"
#include "inventoryCommon.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "mission_common.h"
#include "mapdescription.h"
#include "mapstate_common.h"
#include "nemesis_common.h"
#include "Player.h"
#include "contact_common.h"
#include "rewardCommon.h"
#include "UIGen.h"
#include "worldgrid.h"
#include "EntityLib.h"
#include "StringCache.h"
#include "CharacterClass.h"
#include "entCritter.h"
#include "playerstats_common.h"
#include "gclMapState.h"
#include "gclEntity.h"
#include "progression_common.h"
#include "progression_transact.h"
#include "LobbyCommon.h"
#include "StringUtil.h"
#include "CostumeCommonEntity.h"
#include "ActivityCommon.h"
#include "contactui_eval.h"
#include "gclGoldenPath.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

#include "mission_common_h_ast.h"
#include "mission_enums_h_ast.h"
#include "missionui_eval_c_ast.h"
#include "contactui_eval_h_ast.h"
#include "progression_common_h_ast.h"

#include "Team.h"

#define MAX_MISSION_HELPER_LINES 100
#define STORY_ARC_PERIODIC_REQUEST_TIME 5
#define MAX_MISSION_PROGRESSION_NODE_LINKS 10
#define OPEN_MISSION_EVENT_REQUEST_TIME 10

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_ENUM;
typedef enum MissionJournalRowType
{
	MissionJournalRowType_None = 0,
	MissionJournalRowType_Mission,
	MissionJournalRowType_CompletedMission,
	MissionJournalRowType_ProgressionMission,
	MissionJournalRowType_MapHeader,
	MissionJournalRowType_DefaultMapHeader,
	MissionJournalRowType_CurrentMapHeader,
} MissionJournalRowType;

AUTO_STRUCT;
typedef struct ProgressionMissionGroupData
{
	REF_TO(GameProgressionNodeDef) hNodeDef;	AST(NAME(NodeDef))
	const char* pchDisplayName;					AST(NAME(DisplayName) UNOWNED)
	const char* pchSummary;						AST(NAME(Summary) UNOWNED)
	const char* pchTeaser;						AST(NAME(Teaser) UNOWNED)
	const char* pchIcon;						AST(NAME(Icon) POOL_STRING)
	const char* pchImage;						AST(NAME(Image) POOL_STRING)
	U32 bCurrent : 1;							AST(NAME(IsCurrent))
	U32 bComplete : 1;							AST(NAME(Complete))
	U32 uTimeToComplete;						AST(NAME(TimeToComplete))
	bool bMajor;								AST(NAME(Major))
} ProgressionMissionGroupData;

AUTO_STRUCT;
typedef struct ProgressionMissionData
{
	REF_TO(GameProgressionNodeDef) hNode;			AST(NAME(NodeDef) REFDICT(GameProgressionNodeDef))
	REF_TO(GameProgressionNodeDef) hStoryArcNode;	AST(NAME(StoryArcNodeDef) REFDICT(GameProgressionNodeDef))
	const char* pchMissionDef;						AST(NAME(MissionDef) POOL_STRING)
	const char* pchMissionDisplayName;				AST(NAME(MissionDisplayName) UNOWNED)
	const char* pchDescription;						AST(NAME(Description) UNOWNED)
	const char* pchImage;							AST(NAME(Image) POOL_STRING)
	const char* pchContact;							AST(NAME(Contact) POOL_STRING)
	const char* pchContactDisplayName;				AST(NAME(ContactDisplayName) UNOWNED)
	char* pchContactKey;							AST(NAME(ContactKey))
	const char* pchCreditType;						AST(NAME(CreditType) UNOWNED)
	U32 uSecondaryLockoutTime;						AST(NAME(SecondaryLockoutTime))
	U32 bAvailable : 1;								AST(NAME(Available))
	U32 bOptional : 1;								AST(NAME(Optional))
	U32 bSkippable : 1;								AST(NAME(Skippable))
} ProgressionMissionData;

AUTO_STRUCT;
typedef struct MissionJournalRow
{
	MissionJournalRowType type;

	Mission *mission;  AST(UNOWNED)
	CompletedMission *pCompletedMission; AST(UNOWNED)

	ProgressionMissionData* pProgressionData; AST(NAME(ProgressionData))

	EARRAY_OF(TeammateMission) eaTeamMissions; AST(UNOWNED)
		// If this is a team mission entry, it needs to include the instances of the
		//   mission for all team members.

	bool bCompleted;
		// true if the mission has been completed. Indicates that data is coming from
		//   the pCompleteMission member rather than mission.

	char *estrName;          AST(NAME(Name) ESTRING)
		// The translated name of the mission/completed mission

	const char *pchTranslatedCategoryName; AST(UNOWNED)
		// For perks, the category name

	const char *pchTranslatedUITypeName; AST(UNOWNED)
		// The UIType display name

	const char *pchIconName; AST(UNOWNED)
		// The name of the icon associated with the mission. This comes from the mission
		//   or completed mission as appropriate.

	U32 iPerkPoints;
		// If the mission is a perk, how many points it's worth once completed. Zero otherwise.

	U32 iCount;
		// If the mission has a counter, how many the mission currently has. Zero otherwise.
		//   Zero for all completed missions.
	U32 iTarget;
		// If the mission has a counter, the goal count.
		//   Zero for all completed missions.

	U32 iLevel;
		// The level of the mission
	S32 iLevelDelta;
		// How many levels above or below the player this mission is

	S32 iMinLevel;

	U32 iTeamSize;
		// The team size required for the mission

	bool bScalesForTeam;
		// Whether the mission scales to any team size

	bool bCanDrop;
		// Whether the mission can be dropped

	REF_TO(MissionDef) hDef; AST(REFDICT(Mission))
		// The MissionDef for the mission/completed mission

	// These are not auto-populated, but are cached after they are set.

	char *estrDescription;  AST(NAME(Description) ESTRING)
		// The mission/completed mission's description. Comes from the Summary or UIString
		//   on the MissionDef

	char *estrDetail;       AST(NAME(Detail) ESTRING)
		// The mission/completed mission's description. Comes from the detailString on the
		//   the MissionDef

	S32 iIndent;
	S32 eUIType;

	Entity *pEnt;           AST(UNOWNED)  // The owner of the mission

	S32 iMissionNumber;

} MissionJournalRow;

AUTO_ENUM;
typedef enum MissionListNodeVisibility
{
	MissionListNodeVisibility_Normal = 0,
	MissionListNodeVisibility_AlwaysShow,
	MissionListNodeVisibility_AlwaysHide,
} MissionListNodeVisibility;

AUTO_ENUM;
typedef enum MissionListNodeType
{
	MissionListNodeType_None = 0,
	MissionListNodeType_DisplayName,
	MissionListNodeType_UIString,
	MissionListNodeType_ReturnString,
	MissionListNodeType_FailedReturnString,
	MissionListNodeType_MapString,
	MissionListNodeType_CompletedUIString,
	MissionListNodeType_Separator,
} MissionListNodeType;

AUTO_STRUCT;
typedef struct MissionListNode
{
	Mission *pMission;                        AST(NAME(Mission) UNOWNED)
	CompletedMission *pCompletedMission;      AST(NAME(CompletedMission) UNOWNED)
	Entity *pEnt;                             AST(UNOWNED)  // The owner of the mission
	MissionListNodeType eType;
	S32 iIndent;                              AST(NAME(Indent))
	bool bShadowMission;                      AST(NAME(ShadowMission))  // TRUE if the player doesn't actually have this mission
	S32 iMissionNumber;

	U32 iLevel;
		// The level of the mission
	S32 iLevelDelta;
		// How many levels above or below the player this mission is

	MissionUIType eUIType;
		// The mission UI type

	const char *pchTranslatedUITypeName; AST(UNOWNED)
		// The UIType display name

	const char *pchIconName; AST(UNOWNED)
		// The name of the icon associated with the mission.

	MissionListNodeVisibility eVisibility;
		//Used to say whether to show or hide a specific mission
} MissionListNode;

AUTO_STRUCT;
typedef struct MissionProgressionNodeLink
{
	const char* pchMissionName; AST(POOL_STRING KEY)
	REF_TO(GameProgressionNodeDef) hNodeDef;
	int iMissionIndex;
	U32 uTimestamp;
} MissionProgressionNodeLink;

AUTO_STRUCT;
typedef struct LoreCategoryRow
{
	const char *pchCategoryName;

}LoreCategoryRow;

AUTO_STRUCT;
typedef struct MissionRewardCache
{
	const char *pchMission; AST(NAME(Mission) KEY)
	ContainerID uPlayer;

	InvRewardRequest RewardData;
} MissionRewardCache;

typedef struct EntityMissionUpdateTime {
	const char *pchMissionName;
	U32 uContainerID;
	U32 uLastUpdateTime;
} EntityMissionUpdateTime;

static CritterLoreList s_CritterDataReceived;
static CachedMissionData** s_eaCachedMissionData = NULL;
static CachedMissionReward** s_eaCachedMissionReward = NULL;
static MissionProgressionNodeLink** s_eaMissionProgressionNodeLinks = NULL;
static Item **s_eaOpenMissionRewards = NULL;
static EntityMissionUpdateTime **s_eaMissionUpdateTimes = NULL;
bool g_bDisableAutoHail = false;

void mission_NotifyUpdate(const char *pchMission);
U32 mission_LastUpdateTime(const char *pchMission);

static ItemDef* Lore_GetItemDefFromPage(const char* pchSelectedCategory, S32 iIndex)
{
	LoreCategory* pCat = g_LoreCategories.ppCategories[StaticDefineIntGetInt(LoreCategoryEnum, pchSelectedCategory)];
	int i = 0;

	if (pCat && iIndex >= 0 && eaSize(&pCat->ppPages) > iIndex)
	{
		return GET_REF(pCat->ppPages[iIndex]->hItemDef);
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenLoreJournalRequestCritterDataIfMissing");
void exprGenLoreJournalRequestCritterDataIfMissing(const char* pchSelectedCategory, S32 iIndex, const char* pchAttribString)
{
	ItemDef* pDef = Lore_GetItemDefFromPage(pchSelectedCategory, iIndex);
	bool bFound = false;
	int i;
	if (!pDef || !pDef->pJournalData || !pDef->pJournalData->pchCritterName)
		return;
	for (i = 0; i < eaSize(&s_CritterDataReceived.eaCritterData); i++)
	{
		if (strcmp(pDef->pJournalData->pchCritterName, s_CritterDataReceived.eaCritterData[i]->pchName)==0)
		{
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		RequestedCritterAttribs attribRequest;
		char* context = NULL;
		char* pchString = strdup(pchAttribString);
		char* pTok = strtok_s(pchString, " ", &context);
		attribRequest.eaiAttribs = NULL;
		while(pTok)
		{
			S32 attr = StaticDefineIntGetInt(AttribTypeEnum, pTok);
			eaiPush(&attribRequest.eaiAttribs, attr);
			pTok = strtok_s(NULL, " ", &context);
		}
		free(pchString);
		ServerCmd_RequestCritterDataForLoreJournal(&attribRequest);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenLoreJournalRequestCritterData");
void exprGenLoreJournalRequestCritterData(const char* pchAttribString)
{
	RequestedCritterAttribs attribRequest;
	char* context = NULL;
	char* pchString = strdup(pchAttribString);
	char* pTok = strtok_s(pchString, " ", &context);
	attribRequest.eaiAttribs = NULL;
	while(pTok)
	{
		S32 attr = StaticDefineIntGetInt(AttribTypeEnum, pTok);
		eaiPush(&attribRequest.eaiAttribs, attr);
		pTok = strtok_s(NULL, " ", &context);
	}
	free(pchString);
	ServerCmd_RequestCritterDataForLoreJournal(&attribRequest);
}

//For client-side journal data
AUTO_COMMAND ACMD_NAME("LoreJournal_ReceiveCritterData") ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_IFDEF(GAMESERVER);
void LoreJournal_ReceiveCritterData(CritterLoreList* pCritterData)
{
	int i;
	int j;
	if (s_CritterDataReceived.eaCritterData)
	{
		for (i = 0; i < eaSize(&s_CritterDataReceived.eaCritterData); i++)
		{
			if (s_CritterDataReceived.eaCritterData[i]->pAttrStash)
				stashTableDestroySafe(&s_CritterDataReceived.eaCritterData[i]->pAttrStash);
		}
		eaDestroyStruct(&s_CritterDataReceived.eaCritterData, parse_StoredCritterLoreEntry);
	}
	eaCopyStructs(&pCritterData->eaCritterData, &s_CritterDataReceived.eaCritterData, parse_StoredCritterLoreEntry);
	for (i = 0; i < eaSize(&s_CritterDataReceived.eaCritterData); i++)
	{
		if (!s_CritterDataReceived.eaCritterData[i]->eaiAttribs || !s_CritterDataReceived.eaCritterData[i]->eafValues)
			continue;
		s_CritterDataReceived.eaCritterData[i]->pAttrStash = stashTableCreateInt(eaiSize(&s_CritterDataReceived.eaCritterData[i]->eaiAttribs));
		for (j = 0; j < eaiSize(&s_CritterDataReceived.eaCritterData[i]->eaiAttribs); j++)
		{
			stashIntAddFloat(s_CritterDataReceived.eaCritterData[i]->pAttrStash,s_CritterDataReceived.eaCritterData[i]->eaiAttribs[j], s_CritterDataReceived.eaCritterData[i]->eafValues[j], true);
		}
		eaiDestroy(&s_CritterDataReceived.eaCritterData[i]->eaiAttribs);
		eafDestroy(&s_CritterDataReceived.eaCritterData[i]->eafValues);
		s_CritterDataReceived.eaCritterData[i]->eaiAttribs = NULL;
		s_CritterDataReceived.eaCritterData[i]->eafValues = NULL;
	}
}


AUTO_RUN;
void mission_InitMissionUI(void)
{
	ui_GenInitStaticDefineVars(MissionStateEnum, "MissionState_");
}

// ---------------------------------------------
// Message Formatting Utilities
// ---------------------------------------------

void missionsystem_ClientFormatMessagePtr(const char* sourceName, const Entity *pEnt, const MissionDef *def, U32 iNemesisID, char** ppchResult, Message *pMessage)
{
	missionsystem_FormatMessagePtr(locGetLanguage(getCurrentLocale()), sourceName, pEnt, def, iNemesisID, ppchResult, pMessage);
}

#define missionsystem_ClientFormatMessageRef(sourceName, pEnt, def, eString, handle) missionsystem_ClientFormatMessagePtr(sourceName, pEnt, def, 0, eString, GET_REF(handle))
#define missionsystem_ClientFormatMessageKey(sourceName, pEnt, def, eString, key) missionsystem_ClientFormatMessagePtr(sourceName, pEnt, def, 0, eString, RefSystem_ReferentFromString("Message", key))

// ----------------------------------------------------------------------------
// Interaction
// ----------------------------------------------------------------------------

#define UIVAR_INTERACT_KEY "Interact"

// Get the size of the interact bar to display for this entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetInteractTimePercent");
float exprEntGetInteractTimePercent(SA_PARAM_OP_VALID Entity *pEntity)
{
	float ret = 0;

	// The interact percentage is sent down as a percentage from the server
	if (pEntity)
	{
		UIVar *var = eaIndexedGetUsingString(&pEntity->UIVars, UIVAR_INTERACT_KEY);
		if (var && var->Value.type == MULTI_FLOAT)
		{
			ret = MultiValGetFloat(&var->Value, NULL);
		}
	}
	return ret;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetInteractUseText");
const char* exprEntGetInteractUseText(SA_PARAM_OP_VALID Entity *pEntity, const char* pchDefaultMsgKey)
{
	const char* pchInteractText = NULL;

	if (pEntity && pEntity->pPlayer)
	{
		if (IS_HANDLE_ACTIVE(pEntity->pPlayer->InteractStatus.hInteractUseTimeMsg))
		{
			Message* pMessage = GET_REF(pEntity->pPlayer->InteractStatus.hInteractUseTimeMsg);
			if (pMessage)
			{
				pchInteractText = entTranslateMessage(pEntity, pMessage);
			}
		}
		else if (pchDefaultMsgKey && pchDefaultMsgKey[0])
		{
			pchInteractText = entTranslateMessageKey(pEntity, pchDefaultMsgKey);
		}
	}
	return pchInteractText;
}

// ----------------------------------------------------------------------------
// Misc. Mission utility functions
// ----------------------------------------------------------------------------

// There is a flag on Missions that makes them display as InProgress
// even though they are Complete.  In the future there may be more flags
// like this.
static MissionState mission_GetStateForUI(const Mission *mission)
{
	if(mission)
	{
		MissionDef *def = mission_GetDef(mission);
		if (def)
		{
			if (def->bIsHandoff && mission->state == MissionState_Succeeded)
				return MissionState_InProgress;
			else
				return mission->state;
		}
	}
	return 0;
}

const char* missiondef_GetTranslatedCategoryName(const MissionDef *pDef)
{
	if (pDef)
	{
		if (IS_HANDLE_ACTIVE(pDef->hCategory))
		{
			MissionCategory *pCategory = GET_REF(pDef->hCategory);
			if (pCategory){
				return TranslateDisplayMessage(pCategory->displayNameMsg);
			}else{
				return REF_STRING_FROM_HANDLE(pDef->hCategory);
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionDefGetCategory");
const char* mission_GetLogicalCategoryName(SA_PARAM_OP_VALID const Mission *mission)
{
	MissionDef *pDef = mission_GetDef(mission);
	if (pDef)
	{
		if (IS_HANDLE_ACTIVE(pDef->hCategory))
		{
			MissionCategory *pCategory = GET_REF(pDef->hCategory);
			return pCategory->name;
		}
	}
	return NULL;
}

static MissionDef *missionjournalrow_GetMissionDef(const MissionJournalRow *pRow)
{
	if (pRow->mission){
		return mission_GetDef(pRow->mission);
	} else if (pRow->pCompletedMission){
		return GET_REF(pRow->pCompletedMission->def);
	}
	return NULL;
}

// ----------------------------------------------------------------------------
// Objective Map Helpers
// ----------------------------------------------------------------------------

//Determines if the missiondef has an objective on the current map (checks map name and map variables)
bool missiondef_gclHasObjectiveOnCurrentMap(SA_PARAM_OP_VALID Entity* pEnt, MissionDef *pDef) {
	const char* pchMapName = zmapInfoGetPublicName(NULL);
	MissionMap* pMap = pchMapName ? missiondef_GetMissionMap(pDef, pchMapName) : NULL;
	WorldVariable ** eaVars = NULL;
	int i,j;
	SavedMapDescription *pCurrentMapDesc;

	if(!pMap)
		return false;

	if(!pMap->eaWorldVars || !eaSize(&pMap->eaWorldVars))
		return true;

	if(!pEnt || !pEnt->pSaved)
		return false;

	pCurrentMapDesc = entity_GetLastMap(pEnt);
	if ( !pCurrentMapDesc )
	{
		return false;
	}

	worldVariableStringToArray(pCurrentMapDesc->mapVariables, &eaVars);

	if(!eaVars) {
		eaDestroyStruct(&eaVars, parse_WorldVariable);
		return false;
	}

	for(i = eaSize(&pMap->eaWorldVars)-1; i>=0; i--) {
		bool found = false;
		for(j = eaSize(&eaVars)-1; j>=0 && !found; j--) {
			if(eaVars[j] && !stricmp(eaVars[j]->pcName, pMap->eaWorldVars[i]->pcName) && worldVariableEquals(eaVars[j], pMap->eaWorldVars[i]))
				found = true;
		}
		if(!found) {
			eaDestroyStruct(&eaVars, parse_WorldVariable);
			return false;
		}
	}
	eaDestroyStruct(&eaVars, parse_WorldVariable);
	return true;
}


void mission_gclUpdateCurrentMapMissions(Entity* pEnt)
{
	CONST_EARRAY_OF(Mission) eaMissions = SAFE_MEMBER3(pEnt, pPlayer, missionInfo, missions);
	int i, j;

	if (!eaMissions) {
		return;
	}

	for(i = 0; i < eaSize(&eaMissions); i++) {
		MissionDef *pDef = mission_GetDef(eaMissions[i]);
		if (pDef) {
			Mission **eaSubMissions = NULL;
			mission_GetSubmissions(eaMissions[i], &eaSubMissions, -1, NULL, NULL, NULL, true);

			if (missiondef_gclHasObjectiveOnCurrentMap(pEnt, pDef)) {
				mission_NotifyUpdate(pDef->pchRefString);
			}

			if (eaSubMissions) {
				for(j=0; j < eaSize(&eaSubMissions); j++) {
					MissionDef* pSubDef = mission_GetDef(eaSubMissions[j]);
					if (pSubDef && missiondef_gclHasObjectiveOnCurrentMap(pEnt, pSubDef)) {
						mission_NotifyUpdate(pSubDef->pchRefString);
						mission_NotifyUpdate(pDef->pchRefString);
					}
				}
			}
		}
	}
}


//Determines if the mission has an objective on the current map (checks map name and map variables)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionHasObjectiveOnCurrentMap");
bool exprMissionHasObjectiveOnCurrentMap(ExprContext *pContext, SA_PARAM_NN_VALID Mission* pMission)
{
	MissionDef* pDef = mission_GetDef(pMission);
	Entity* pEnt = entActivePlayerPtr();
	if(pDef && pEnt)
		return missiondef_gclHasObjectiveOnCurrentMap(pEnt, pDef);

	return false;
}

//Determines if the mission or any of its submissions have an objective on the current map (checks map name and map variables)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionOrSubMissionHasObjectiveOnCurrentMap");
bool exprMissionOrSubMissionHasObjectiveOnCurrentMap(ExprContext *pContext, Mission* pMission)
{
	MissionDef* pDef = mission_GetDef(pMission);
	Entity* pEnt = entActivePlayerPtr();
	bool bCurrentMap = false;
	int i;

	if( pEnt && pDef )
	{
		bCurrentMap = missiondef_gclHasObjectiveOnCurrentMap(pEnt, pDef);

		if ( !bCurrentMap )
		{
			for(i = eaSize(&pDef->subMissions)-1; i>=0; i--)
			{
				if ( missiondef_gclHasObjectiveOnCurrentMap(pEnt, pDef->subMissions[i]) )
				{
					bCurrentMap = true;
					break;
				}
			}
		}
	}

	return bCurrentMap;
}



// ----------------------------------------------------------------------------
// Mission Comparators
// ----------------------------------------------------------------------------

static int mission_SortByDisplayOrder(const Mission** a, const Mission** b)
{
	if (a && *a && b && *b)
	{
		// Open missions should be sorted after the personal mission
		MissionDef *pRootDefA = GET_REF((*a)->rootDefOrig);
		MissionDef *pRootDefB = GET_REF((*b)->rootDefOrig);
		S32 iAAdjustmentForOpenMission = pRootDefA && pRootDefA->missionType == MissionType_OpenMission ? 1000 : 0;
		S32 iBAdjustmentForOpenMission = pRootDefB && pRootDefB->missionType == MissionType_OpenMission ? 1000 : 0;

		return ((*a)->displayOrder + iAAdjustmentForOpenMission) - ((*b)->displayOrder + iBAdjustmentForOpenMission);
	}
	return 0;
}

static int mission_SortByStartTime(const Mission** a, const Mission** b, const void *unused)
{
	if (a && *a && b && *b)
		return (*a)->startTime - (*b)->startTime;
	return 0;
}

static U32 mission_GetlastClientNotifiedTimeRecursive(const Mission* mission)
{
	Entity *pEnt = entActivePlayerPtr();
	int i, n = eaSize(&mission->children);
	MissionDef *pDef = mission_GetDef(mission);
	U32 maxTime = pDef ? mission_LastUpdateTime(pDef->pchRefString) : 0;
	for (i = 0; i < n; i++)
	{
		U32 time =  mission_GetlastClientNotifiedTimeRecursive(mission->children[i]);
		MAX1(maxTime, time);
	}
	return maxTime;
}

static int mission_SortByNodePrimary(const MissionListNode** a, const MissionListNode** b, const void *unused)
{
	if (a && *a && (*a)->pMission && b && *b && (*b)->pMission)
	{
		bool aIsPrimary = (*a)->pEnt && gclEntIsPrimaryMission((*a)->pEnt, (*a)->pMission->missionNameOrig);
		bool bIsPrimary = (*b)->pEnt && gclEntIsPrimaryMission((*b)->pEnt, (*b)->pMission->missionNameOrig);
		if(aIsPrimary && !bIsPrimary) return 1;
		if(!aIsPrimary && bIsPrimary) return -1;
	}
	return 0;
}

static int mission_SortByNodeUpdateTime(const MissionListNode** a, const MissionListNode** b, const void *unused)
{
	if (a && *a && (*a)->pMission && b && *b && (*b)->pMission)
		return mission_GetlastClientNotifiedTimeRecursive((*b)->pMission) - mission_GetlastClientNotifiedTimeRecursive((*a)->pMission);
	return 0;
}

static int mission_SortByUpdateTimeStable(const Mission** a, const Mission** b, const void *unused)
{
	if (a && *a && b && *b)
		return mission_GetlastClientNotifiedTimeRecursive((*b)) - mission_GetlastClientNotifiedTimeRecursive((*a));
	return 0;
}

static int mission_SortByUpdateTime(const Mission** a, const Mission** b)
{
	if (a && *a && b && *b)
		return mission_GetlastClientNotifiedTimeRecursive((*b)) - mission_GetlastClientNotifiedTimeRecursive((*a));
	return 0;
}

static int mission_SortByTracked(const Mission** a, const Mission** b, const void *unused)
{
	if (a && *a && b && *b)
		return (*b)->tracking - (*a)->tracking;
	return 0;
}

static int mission_SortByState(const Mission** a, const Mission** b, const void *unused)
{
	if (a && *a && b && *b)
		return mission_GetStateForUI((*b)) - mission_GetStateForUI((*a));
	return 0;
}

static int mission_SortByLevel(const Mission** a, const Mission** b, const void *unused)
{
	if (a && *a && b && *b)
	{
		int iLevelA = (*a)->iLevel;
		int iLevelB = (*b)->iLevel;

		if(!iLevelA) {
			MissionDef *defA = mission_GetDef((*a));
			if(defA)
				iLevelA = defA->levelDef.missionLevel;
		}

		if(!iLevelB) {
			MissionDef *defB = mission_GetDef((*b));
			if(defB)
				iLevelB = defB->levelDef.missionLevel;
		}
		return iLevelA - iLevelB;
	}
	return 0;
}

static int mission_SortByCategory(const Mission** a, const Mission** b, const void *unused)
{
	MissionDef *adef = mission_GetDef(*a);
	MissionDef *bdef = mission_GetDef(*b);
	const char *acat = missiondef_GetTranslatedCategoryName(adef);
	const char *bcat = missiondef_GetTranslatedCategoryName(bdef);
	
	if (!acat && !bcat)
		return 0;
	else if (!(acat && bcat))
		return !!bcat - !!acat;
	else
		return stricmp(acat, bcat);
	return 0;
}

// ----------------------------------------------------------------------------
// Mission Journal Row Comparators
// ----------------------------------------------------------------------------

static int missionjournalrow_SortByPrimary(const MissionJournalRow** a, const MissionJournalRow** b, const void *unused)
{
	if (a && *a && (*a)->mission && b && *b && (*b)->mission)
	{
		bool aIsPrimary = (*a)->pEnt && gclEntIsPrimaryMission((*a)->pEnt, (*a)->mission->missionNameOrig);
		bool bIsPrimary = (*b)->pEnt && gclEntIsPrimaryMission((*b)->pEnt, (*b)->mission->missionNameOrig);
		if(aIsPrimary && !bIsPrimary) return 1;
		if(!aIsPrimary && bIsPrimary) return -1;
	}
	return 0;
}

static int missionjournalrow_SortByLevel(const MissionJournalRow** a, const MissionJournalRow** b, const void *unused)
{
	if (a && *a && b && *b)
	{
		return (*a)->iLevel - (*b)->iLevel;
	}
	return 0;
}

static int missionjournalrow_SortByState(const MissionJournalRow** a, const MissionJournalRow** b, const void *unused)
{
	if (a && *a && b && *b)
	{
		if ((*a)->mission && (*b)->mission)
		{
			Mission *pMissionA = (*a)->mission;
			Mission *pMissionB = (*b)->mission;
			return mission_SortByState(&pMissionA, &pMissionB, unused);
		}
		else if ((*a)->mission && (*b)->pCompletedMission)
			return 1;
		else if ((*b)->mission && (*a)->pCompletedMission)
			return -1;
	}
	return 0;
}

static int missionjournalrow_SortByPriority(const MissionJournalRow** a, const MissionJournalRow** b, const void *unused)
{
	if (a && *a && b && *b)
	{
		const MissionDef *pMissionA = GET_REF((*a)->hDef);
		const MissionDef *pMissionB = GET_REF((*b)->hDef);
		S32 iSortPriorityA = pMissionA ? pMissionA->iSortPriority : 0;
		S32 iSortPriorityB = pMissionB ? pMissionB->iSortPriority : 0;

		if (!iSortPriorityA)
			return 1;
		if (!iSortPriorityB)
			return -1;

		return iSortPriorityA - iSortPriorityB;
	}
	return 0;
}

static int missionjournalrow_SortByReturnMap(const MissionJournalRow** a, const MissionJournalRow** b, const void *unused)
{
	if (a && *a && b && *b)
	{
		const MissionDef *pMissionA = GET_REF((*a)->hDef);
		const MissionDef *pMissionB = GET_REF((*b)->hDef);
		MissionCategory *pCategoryA = pMissionA ? GET_REF(pMissionA->hCategory) : NULL;
		MissionCategory *pCategoryB = pMissionB ? GET_REF(pMissionB->hCategory) : NULL;
		const char *pchMapA = pCategoryA ? pCategoryA->name : "";
		const char *pchMapB = pCategoryB ? pCategoryB->name : "";

		if (!pchMapA && !pchMapB)
			return 0;
		else if (!(pchMapA && pchMapB))
			return !!pchMapB - !!pchMapA;
		else
		{
			int strCmpVal = stricmp(pchMapA, pchMapB);
			if (strCmpVal == 0)
				return (*a)->type - (*b)->type;
			else
				return strCmpVal;
		}
	}
	return 0;
}

static int missionjournalrow_SortByUIType(const MissionJournalRow** a, const MissionJournalRow** b, const void *unused)
{
	if (a && *a && b && *b)
	{
		MissionUIType eUITypeA = (*a)->eUIType;
		MissionUIType eUITypeB = (*b)->eUIType;

		if (eUITypeA == kMissionUIType_None && eUITypeB == kMissionUIType_None)
			return 0;
		else if (eUITypeA == kMissionUIType_None || eUITypeB == kMissionUIType_None)
			return (eUITypeB != kMissionUIType_None) - (eUITypeA != kMissionUIType_None);
		else
		{
			if (eUITypeA == eUITypeB)
				return (*a)->type - (*b)->type;
			else
				return eUITypeA - eUITypeB;
		}
	}
	return 0;
}

static int missionjournalrow_SortByCategory(const MissionJournalRow** a, const MissionJournalRow** b, const void *unused)
{
	if (a && *a && b && *b)
	{
		const char *pchNameA = (*a)->pchTranslatedCategoryName;
		const char *pchNameB = (*b)->pchTranslatedCategoryName;

		if (!pchNameA && !pchNameB)
			return 0;
		else if (!(pchNameA && pchNameB))
			return !!pchNameB - !!pchNameA;
		else
		{
			int strCmpVal = stricmp(pchNameA, pchNameB);
			if (strCmpVal == 0)
				return (*a)->type - (*b)->type;
			else
				return strCmpVal;
		}
	}
	return 0;
}

static int missionjournalrow_SortByCategoryWithSucceededCategory(const MissionJournalRow** a,
																 const MissionJournalRow** b,
																 const void *unused)
{
	if (a && *a && b && *b)
	{
		bool bSucceededA = (*a)->mission && (*a)->mission->state == MissionState_Succeeded;
		bool bSucceededB = (*b)->mission && (*b)->mission->state == MissionState_Succeeded;

		if (bSucceededA && !bSucceededB)
			return -1;
		if (bSucceededB && !bSucceededA)
			return 1;

		return missionjournalrow_SortByCategory(a, b, unused);
	}
	return 0;
}

static int missionjournalrow_SortByDisplayNameStable(const MissionJournalRow** a, const MissionJournalRow** b, const void *unused)
{
	if (a && *a && b && *b)
	{
		const char *pchNameA = (*a)->estrName;
		const char *pchNameB = (*b)->estrName;

		if (!pchNameA && !pchNameB)
			return 0;
		else if (!(pchNameA && pchNameB))
			return !!pchNameA - !!pchNameB;
		else
		{
			return stricmp(pchNameA, pchNameB);
		}
	}
	return 0;
}

static int missionjournalrow_SortByLogicalNameStable(const MissionJournalRow** a, const MissionJournalRow** b, const void *unused)
{
	if (a && *a && b && *b)
	{
		MissionDef *pMissionDefA = GET_REF((*a)->hDef);
		MissionDef *pMissionDefB = GET_REF((*b)->hDef);
		const char *pchNameA = SAFE_MEMBER(pMissionDefA, name);
		const char *pchNameB = SAFE_MEMBER(pMissionDefB, name);

		if (!pchNameA && !pchNameB)
			return 0;
		else if (!(pchNameA && pchNameB))
			return !!pchNameA - !!pchNameB;
		else
		{
			return stricmp(pchNameA, pchNameB);
		}
	}
	return 0;
}

static int missionjournalrow_SortBySortPriorityStable(const MissionJournalRow** a, const MissionJournalRow** b, const void *unused)
{
	if (a && *a && b && *b)
	{
		MissionDef *pMissionDefA = GET_REF((*a)->hDef);
		MissionDef *pMissionDefB = GET_REF((*b)->hDef);
		S32 iSortPriorityA = SAFE_MEMBER(pMissionDefA, iSortPriority);
		S32 iSortPriorityB = SAFE_MEMBER(pMissionDefB, iSortPriority);

		if( iSortPriorityA == iSortPriorityB )
		{
			return missionjournalrow_SortByDisplayNameStable(a, b, NULL);
		}

		return iSortPriorityA < iSortPriorityB ? -1 : 1;
	}
	return 0;
}

static int missionjournalrow_SortByDisplayName(const MissionJournalRow** a, const MissionJournalRow** b)
{
	return missionjournalrow_SortByDisplayNameStable(a, b, NULL);
}

static int missionjournalrow_SortByLogicalName(const MissionJournalRow** a, const MissionJournalRow** b)
{
	return missionjournalrow_SortByLogicalNameStable(a, b, NULL);
}

static int missionjournalrow_SortBySortPriority(const MissionJournalRow** a, const MissionJournalRow** b)
{
	return missionjournalrow_SortBySortPriorityStable(a, b, NULL);
}


// ----------------------------------------------------------------------------
// Mission UI expressions
// ----------------------------------------------------------------------------

// Set a mission def to be the current GenData
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionSetGenDataFromMissionDef");
void exprMissionSetGenDataFromMissionDef(ExprContext *pContext, SA_PARAM_OP_VALID MissionDef* missionDef)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);

	if(pGen)
	{
		ui_GenSetPointer(pGen, missionDef, parse_MissionDef);
	}
}

// Set a mission def to be the current GenData by name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionSetGenDataFromMissionDefName");
void exprMissionSetGenDataFromMissionDefName(ExprContext *pContext, const char* missionName)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	MissionDef* missionDef = missiondef_DefFromRefString(missionName);

	if(pGen)
	{
		ui_GenSetPointer(pGen, missionDef, parse_MissionDef);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionUIString");
char *exprGetMissionUIString(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Mission *mission)
{
	MissionDef *def = mission_GetDef(mission);
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (def)
		missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, player_GetPrimaryNemesisID(pEnt), &estrBuffer, GET_REF(def->uiStringMsg.hMessage));

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCompletedMissionUIString");
char *exprGetCompletedMissionUIString(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID CompletedMission *completedMission)
{
	MissionDef *def = completedMission ? GET_REF(completedMission->def) : NULL;
	static char *estrBuffer = NULL;
	ContainerID iNemesisID = 0;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	// For now, always use the first Nemesis
	if (completedMission && eaSize(&completedMission->eaStats)){
		iNemesisID = completedMission->eaStats[0]->iNemesisID;
	}

	if (def)
		missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, iNemesisID, &estrBuffer, GET_REF(def->uiStringMsg.hMessage));

	return NULL_TO_EMPTY(estrBuffer);
}

static void mission_AppendReturnString(Entity *pEnt, Mission *mission, char** estrBuffer, const char *pchRowHeader, const char *pchIndentSpacer, const char *pchObjectiveHeader, const char *pchCompletedHeader, const char *pchFailedHeader, const char *pchRowFooter, int depth, bool withCollapse)
{
	MissionDef *def = mission_GetDef(mission);
	char* estrReturnBuffer = NULL;
	estrStackCreate(&estrReturnBuffer);
	if (def)
	{
		if (def->eReturnType == MissionReturnType_Message)
		{
			missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, 0, &estrReturnBuffer, GET_REF(def->msgReturnStringMsg.hMessage));
		}
		if (estrReturnBuffer && estrReturnBuffer[0])
		{
			estrAppend2(estrBuffer, pchRowHeader);
			estrAppend2(estrBuffer, pchObjectiveHeader);
			estrAppend(estrBuffer, &estrReturnBuffer);
			estrAppend2(estrBuffer, pchRowFooter);
		}
	}
	estrDestroy(&estrReturnBuffer);
}

static void mission_AppendFailedReturnString(Entity *pEnt, Mission *mission, char** estrBuffer, const char *pchRowHeader, const char *pchIndentSpacer, const char *pchObjectiveHeader, const char *pchCompletedHeader, const char *pchFailedHeader, const char *pchRowFooter, int depth, bool withCollapse)
{
	MissionDef *def = mission_GetDef(mission);
	char* estrReturnBuffer = NULL;
	estrStackCreate(&estrReturnBuffer);
	if (def)
	{
		missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, 0, &estrReturnBuffer, GET_REF(def->failReturnMsg.hMessage));
		if (estrReturnBuffer && estrReturnBuffer[0])
		{
			estrAppend2(estrBuffer, pchRowHeader);
			estrAppend2(estrBuffer, pchObjectiveHeader);
			estrAppend(estrBuffer, &estrReturnBuffer);
			estrAppend2(estrBuffer, pchRowFooter);
		}
	}
	estrDestroy(&estrReturnBuffer);
}

static void mission_AppendObjectiveCountString(Mission *mission, MissionDef *missionDef, char** estrBuffer)
{
	if (mission && mission->target && missionDef->showCount != MDEShowCount_Normal)
	{
		switch (missionDef->showCount)
		{
		case MDEShowCount_Only_Count:
			estrConcatf(estrBuffer, "(%d)", mission->count);
			break;
		case MDEShowCount_Count_Down:
			estrConcatf(estrBuffer, "(%d)", mission->target - mission->count);
			break;
		case MDEShowCount_Percent:
			estrConcatf(estrBuffer, "(%d%%)", (int)((100.0 * mission->count) / mission->target));
			break;
		default:
			estrConcatf(estrBuffer, "(%d/%d)", mission->count, mission->target);
			break;
		}
	}
}

static void mission_AppendObjectiveStringRecursive(Entity *pEnt, Mission *mission, char** estrBuffer, const char *pchRowHeader, const char *pchIndentSpacer, const char *pchObjectiveHeader, const char *pchCompletedHeader, const char *pchFailedHeader, const char *pchSummaryHeader, const char *pchRowFooter, int depth, bool withCollapse, bool includeSummary, bool bExpandCompletedMissions)
{
	Mission** childrenInOrder = NULL;
	int i, n;

	if (mission && mission_HasUIString(mission))
	{
		MissionDef *def = mission_GetDef(mission);
		if (def)
		{
			int iDepth = 0;
			estrAppend2(estrBuffer, pchRowHeader);
			for (iDepth = 0; iDepth < depth; iDepth++)
				estrAppend2(estrBuffer, pchIndentSpacer);
			if (mission_GetStateForUI(mission) == MissionState_Succeeded) // not sure I want to do this yet || (timeServerSecondsSince2000() - mission->lastClientNotifiedTime <=3)
				estrAppend2(estrBuffer, pchCompletedHeader);
			else if (mission_GetStateForUI(mission) == MissionState_InProgress)
				estrAppend2(estrBuffer, pchObjectiveHeader);
			else if (mission_GetStateForUI(mission) == MissionState_Failed)
				estrAppend2(estrBuffer, pchFailedHeader);

			missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, player_GetPrimaryNemesisID(pEnt), estrBuffer, GET_REF(def->uiStringMsg.hMessage));

			if (mission->target){
				estrConcatf(estrBuffer, " ");
				mission_AppendObjectiveCountString(mission, def, estrBuffer);
			}
			if (mission->expirationTime && mission_GetStateForUI(mission) == MissionState_InProgress){
				U32 seconds = mission_GetTimeRemainingSeconds(mission);
				estrConcatf(estrBuffer, " %d:%02d", seconds/60, seconds%60);
			}
			estrAppend2(estrBuffer, pchRowFooter);
			if (includeSummary && missiondef_HasSummaryString(def) && mission_GetStateForUI(mission) == MissionState_InProgress && depth != 0)
			{
				estrAppend2(estrBuffer, pchRowHeader);
				for (iDepth = 0; iDepth < depth+1; iDepth++)
					estrAppend2(estrBuffer, pchIndentSpacer);
				estrAppend2(estrBuffer, pchSummaryHeader);
				missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, player_GetPrimaryNemesisID(pEnt), estrBuffer, GET_REF(def->summaryMsg.hMessage));
				estrAppend2(estrBuffer, pchRowFooter);
			}
		}
		depth++; // depth incremented here because if this is a root mission with no UI string,
				 //children should be displayed at root depth
	}

	if (mission && (mission_HasUIString(mission) || mission->depth == 0))
	{
		if (mission_GetStateForUI(mission) == MissionState_InProgress || mission->openChildren > 0 || eaSize(&mission->childFullMissions) || !withCollapse)
		{
			if (gConf.bAddRelatedOpenMissionsToPersonalMissionHelper)
			{
				// Check if the current mission is related to any open mission
				MissionDef *pCurrentMissionDef = mission_GetDef(mission);
				const char *pcRelatedOpenMissionDef = pCurrentMissionDef ? pCurrentMissionDef->pcRelatedMission : NULL;
				MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pEnt);
				OpenMission *pOpenMission = pMissionInfo && pcRelatedOpenMissionDef && stricmp(pMissionInfo->pchCurrentOpenMission, pcRelatedOpenMissionDef) == 0 ?
					mapState_OpenMissionFromName(mapStateClient_Get(),pMissionInfo->pchCurrentOpenMission) : NULL;

				if (pOpenMission && pOpenMission->pMission)
				{
					mission_AppendObjectiveStringRecursive(pEnt, pOpenMission->pMission, estrBuffer, pchRowHeader, pchIndentSpacer, pchObjectiveHeader, pchCompletedHeader, pchFailedHeader, pchSummaryHeader, pchRowFooter, depth, withCollapse, includeSummary, bExpandCompletedMissions);
				}
			}

			eaPushEArray(&childrenInOrder, &mission->children);
			eaQSort(childrenInOrder, mission_SortByDisplayOrder);

			if (mission_GetStateForUI(mission) == MissionState_InProgress || bExpandCompletedMissions)
			{
				n = eaSize(&childrenInOrder);
				for (i = 0; i < n; i++)
				{
					mission_AppendObjectiveStringRecursive(pEnt, childrenInOrder[i], estrBuffer, pchRowHeader, pchIndentSpacer, pchObjectiveHeader, pchCompletedHeader, pchFailedHeader, pchSummaryHeader, pchRowFooter, depth, withCollapse, includeSummary, bExpandCompletedMissions);
				}
			}

			// Add child full missions that are hidden at the top level as if they were normal children
			n = eaSize(&mission->childFullMissions);
			for ( i = 0; i< n; i++)
			{
				Mission *childMission = mission_GetMissionByName(pEnt->pPlayer->missionInfo, mission->childFullMissions[i]);
				if (childMission)
				{
					if (childMission->bHiddenFullChild)
					{
						mission_AppendObjectiveStringRecursive(pEnt, childMission, estrBuffer, pchRowHeader, pchIndentSpacer, pchObjectiveHeader, pchCompletedHeader, pchFailedHeader, pchSummaryHeader, pchRowFooter, depth, withCollapse, includeSummary, bExpandCompletedMissions);
					}
				}
				else
				{
					CompletedMission *childCompletedMission = mission_GetCompletedMissionByName(pEnt->pPlayer->missionInfo, mission->childFullMissions[i]);
					if (childCompletedMission && childCompletedMission->bHidden)
					{
						MissionDef *def = missiondef_FindMissionByName(NULL, mission->childFullMissions[i]);
						if (def)
						{
							int iDepth = 0;
							estrAppend2(estrBuffer, pchRowHeader);
							for (iDepth = 0; iDepth < depth; iDepth++)
								estrAppend2(estrBuffer, pchIndentSpacer);
							estrAppend2(estrBuffer, pchCompletedHeader);
							missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, player_GetPrimaryNemesisID(pEnt), estrBuffer, GET_REF(def->uiStringMsg.hMessage));
							estrAppend2(estrBuffer, pchRowFooter);
						}
					}
				}
			}
		}
	}

	eaDestroy(&childrenInOrder);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionObjectiveListSMF");
char *exprGetMissionObjectiveListSMF(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Mission *mission, const char *pchRowHeader, const char *pchIndentSpacer, const char *pchObjectiveHeader, const char *pchCompletedHeader, const char *pchFailedHeader, const char *pchRowFooter)
{
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	mission_AppendObjectiveStringRecursive(pEnt, mission, &estrBuffer, pchRowHeader, pchIndentSpacer, pchObjectiveHeader, pchCompletedHeader, pchFailedHeader, NULL, pchRowFooter, 0, false, false, true);
	if (mission_GetStateForUI(mission) == MissionState_Succeeded || mission_GetStateForUI(mission) == MissionState_InProgress){
		mission_AppendReturnString(pEnt, mission, &estrBuffer, pchRowHeader, pchIndentSpacer, pchObjectiveHeader, pchCompletedHeader, pchFailedHeader, pchRowFooter, 0, false);
	} else if (mission_GetStateForUI(mission) == MissionState_Failed){
		mission_AppendFailedReturnString(pEnt, mission, &estrBuffer, pchRowHeader, pchIndentSpacer, pchObjectiveHeader, pchCompletedHeader, pchFailedHeader, pchRowFooter, 0, false);
	}

	return NULL_TO_EMPTY(estrBuffer);
}

// I am making the assumption that this function is only called on Neverwinter right now.  I have made changes to the way mission_AppendObjectiveStringRecursive
// behaves based on this.  Unfortunately that makes this function very poorly named.  Someone can do some cleaning later if they feel so inclined.
// My first instinct would be to have this be a game-specific command that behaves the way we want it to.  [RMARR - 12/16/10]
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionObjectiveListSMFWithSummaries");
char *exprGetMissionObjectiveListSMFWithSummaries(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Mission *mission, const char *pchRowHeader, const char *pchIndentSpacer, const char *pchObjectiveHeader, const char *pchCompletedHeader, const char *pchFailedHeader, const char* pchSummaryHeader, const char *pchRowFooter)
{
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	{
		const bool bExpandCompletedMissionSubObjectives = true;
		mission_AppendObjectiveStringRecursive(pEnt, mission, &estrBuffer, pchRowHeader, pchIndentSpacer, pchObjectiveHeader, pchCompletedHeader, pchFailedHeader, pchSummaryHeader, pchRowFooter, 0, false, true, bExpandCompletedMissionSubObjectives);
	}
	if (mission_GetStateForUI(mission) == MissionState_Succeeded){
		mission_AppendReturnString(pEnt, mission, &estrBuffer, pchRowHeader, pchIndentSpacer, pchObjectiveHeader, pchCompletedHeader, pchFailedHeader, pchRowFooter, 0, false);
	} else if (mission_GetStateForUI(mission) == MissionState_Failed){
		mission_AppendFailedReturnString(pEnt, mission, &estrBuffer, pchRowHeader, pchIndentSpacer, pchObjectiveHeader, pchCompletedHeader, pchFailedHeader, pchRowFooter, 0, false);
	}

	return NULL_TO_EMPTY(estrBuffer);
}

// Same as GetMissionObjectiveListSMF but does not display the return string
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionObjectiveListSMFNoReturn");
char *exprGetMissionObjectiveListSMFNoReturn(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Mission *mission, const char *pchRowHeader, const char *pchIndentSpacer, const char *pchObjectiveHeader, const char *pchCompletedHeader, const char *pchFailedHeader, const char *pchRowFooter)
{
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	mission_AppendObjectiveStringRecursive(pEnt, mission, &estrBuffer, pchRowHeader, pchIndentSpacer, pchObjectiveHeader, pchCompletedHeader, pchFailedHeader, NULL, pchRowFooter, 0, false, false, true);

	return NULL_TO_EMPTY(estrBuffer);
}

// Returns the count and target progress of this mission if they are displayable as single numbers.
// Count and target are 0 if no progress can be displayed.
static void mission_GetObjectiveProgressStatsRecursive(Entity *pEnt, Mission *mission, U32 *piCount, U32 *piTarget)
{
	int i;

	if (!mission) {
		return;
	}

	if (mission->target) {
		*piCount += mission->count;
		*piTarget += mission->target;
	}

	for (i=0; i < eaSize(&mission->children); i++) {
		Mission *pChildMission = mission->children[i];
		mission_GetObjectiveProgressStatsRecursive(pEnt, pChildMission, piCount, piTarget);
	}

	// Add child full missions that are hidden at the top level as if they were normal children
	for (i=0; i < eaSize(&mission->childFullMissions); i++) {
		Mission *pChildMission = mission_GetMissionByName(pEnt->pPlayer->missionInfo, mission->childFullMissions[i]);
		if (pChildMission) {
			if (pChildMission->bHiddenFullChild) {
				mission_GetObjectiveProgressStatsRecursive(pEnt, pChildMission, piCount, piTarget);
			}
		}
		// Note: we don't have count info in completed missions since we just have the defs for those, so those are ignored.
	}
}


// Returns the current progress of this mission if it's displayable as a single number.
// Returns 0 if no progress can be displayed.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetMissionObjectiveCount);
U32 exprGetMissionObjectiveCount(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Mission *mission)
{
	U32 iCount = 0;
	U32 iTarget = 0;
	mission_GetObjectiveProgressStatsRecursive(pEnt, mission, &iCount, &iTarget);
	return iCount;
}

// Returns the target amount required to complete this mission, if it can be displayed as a single number.
// Returns 0 if no progress can be displayed.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetMissionObjectiveTarget);
U32 exprGetMissionObjectiveTarget(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Mission *mission)
{
	U32 iCount = 0;
	U32 iTarget = 0;
	mission_GetObjectiveProgressStatsRecursive(pEnt, mission, &iCount, &iTarget);
	return iTarget;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionIsType");
int exprMissionIsType(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Mission *mission, const char* type)
{
	MissionDef *def = mission_GetDef(mission);
	MissionType missionType = StaticDefineIntGetInt(MissionTypeEnum, type);

	return def ? def->missionType == missionType : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionDisplayName");
char *exprGetMissionDisplayName(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Mission *mission)
{
	MissionDef *def = mission_GetDef(mission);
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (def)
		missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, 0, &estrBuffer, GET_REF(def->displayNameMsg.hMessage));

	if (mission && mission->eCreditType == MissionCreditType_Flashback && GET_REF(g_MissionSystemMsgs.hFlashbackDisplayName)){
		static char *estrBuffer2 = NULL;
		if (!estrBuffer2)
			estrCreate(&estrBuffer2);
		estrClear(&estrBuffer2);
		FormatMessagePtr(&estrBuffer2, GET_REF(g_MissionSystemMsgs.hFlashbackDisplayName), STRFMT_STRING("MissionName", estrBuffer), STRFMT_END);
		return NULL_TO_EMPTY(estrBuffer2);
	}

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCompletedMissionDisplayName");
char *exprGetCompletedMissionDisplayName(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID CompletedMission *pCompletedMission)
{
	MissionDef *def = NULL;
	static char *estrBuffer = NULL;

	if (pCompletedMission){
		def = GET_REF(pCompletedMission->def);
	}

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (def)
		missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, 0, &estrBuffer, GET_REF(def->displayNameMsg.hMessage));

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionDetailString");
char *exprGetMissionDetailString(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Mission *mission)
{
	MissionDef *def = mission_GetDef(mission);
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (def)
		missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, player_GetPrimaryNemesisID(pEnt), &estrBuffer, GET_REF(def->detailStringMsg.hMessage));

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCompletedMissionDetailString");
char *exprGetCompletedMissionDetailString(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID CompletedMission *pCompletedMission)
{
	MissionDef *def = NULL;
	ContainerID iNemesisID = 0;
	static char *estrBuffer = NULL;

	if (pCompletedMission){
		def = GET_REF(pCompletedMission->def);

		// For now, always use the first Nemesis
		if (eaSize(&pCompletedMission->eaStats)){
			iNemesisID = pCompletedMission->eaStats[0]->iNemesisID;
		}
	}

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (def)
		missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, iNemesisID, &estrBuffer, GET_REF(def->detailStringMsg.hMessage));

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionSummary");
char *exprGetMissionSummary(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Mission *mission)
{
	MissionDef *def = mission_GetDef(mission);
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (def)
		missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, player_GetPrimaryNemesisID(pEnt), &estrBuffer, GET_REF(def->summaryMsg.hMessage));

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionFailureString");
char *exprGetMissionFailureString(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Mission *mission)
{
	MissionDef *def = mission_GetDef(mission);
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (def)
		missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, 0, &estrBuffer, GET_REF(def->failureMsg.hMessage));

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCompletedMissionSummary");
char *exprGetCompletedMissionSummary(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID CompletedMission *pCompletedMission)
{
	MissionDef *def = NULL;
	ContainerID iNemesisID = 0;
	static char *estrBuffer = NULL;

	if (pCompletedMission){
		def = GET_REF(pCompletedMission->def);

		// For now, always use the first Nemesis
		if (eaSize(&pCompletedMission->eaStats)){
			iNemesisID = pCompletedMission->eaStats[0]->iNemesisID;
		}
	}

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (def)
		missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, iNemesisID, &estrBuffer, GET_REF(def->summaryMsg.hMessage));

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCompletedMissionPerkPoints");
U32 exprGetCompletedMissionPerkPoints(SA_PARAM_OP_VALID CompletedMission *pCompletedMission)
{
	if (pCompletedMission)
	{
		MissionDef *pMissionDef = GET_REF(pCompletedMission->def);
		if (pMissionDef)
		{
			return pMissionDef->iPerkPoints;
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionReturnString");
char *exprGetMissionReturnString(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Mission *mission)
{
	MissionDef *def = mission_GetDef(mission);
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (def)
	{
		if (def->eReturnType == MissionReturnType_Message)
			missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, 0, &estrBuffer, GET_REF(def->msgReturnStringMsg.hMessage));
	}

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionFailedReturnString");
char *exprGetMissionFailedReturnString(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Mission *mission)
{
	MissionDef *def = mission_GetDef(mission);
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (def)
	{
		missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, def, 0, &estrBuffer, GET_REF(def->failReturnMsg.hMessage));
	}

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionName");
const char *exprGetMissionName(SA_PARAM_OP_VALID Mission *mission)
{
	if (mission)
	{
		MissionDef *def = mission_GetDef(mission);
		if (def)
			return NULL_TO_EMPTY(def->pchRefString);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetParentMissionName");
const char *exprGetParentMissionName(SA_PARAM_OP_VALID Mission *mission)
{
	if (mission && mission->parent)
	{
		MissionDef *def = mission_GetDef(mission->parent);
		if (def)
			return NULL_TO_EMPTY(def->pchRefString);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionDefDisplayName");
const char *exprGetMissionDefDisplayName(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID MissionDef *def)
{
	static char *estrBuffer = NULL;
	MissionDef *pRootDef = def;

	while (pRootDef && GET_REF(pRootDef->parentDef))
		pRootDef = GET_REF(pRootDef->parentDef);

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (pRootDef && GET_REF(pRootDef->displayNameMsg.hMessage))
		missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, pRootDef, 0, &estrBuffer, GET_REF(pRootDef->displayNameMsg.hMessage));

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("MissionFromName");
SA_RET_OP_VALID Mission *exprMissionFromName(SA_PARAM_OP_VALID Entity *pEntity, const char *pchMissionName)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	if (pInfo)
	{
		return mission_GetMissionOrSubMissionByName(pInfo, pchMissionName);
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("MissionDefFromName");
SA_RET_OP_VALID MissionDef *exprMissionDefFromName(SA_PARAM_OP_VALID Entity *pEntity, const char *pchMissionName)
{
	if (pchMissionName)
	{
		return RefSystem_ReferentFromString(g_MissionDictionary, pchMissionName);
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CompletedMissionFromName");
SA_RET_OP_VALID CompletedMission *exprCompletedMissionFromName(SA_PARAM_OP_VALID Entity *pEntity, const char *pchMissionName)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	if (pInfo)
	{
		return eaIndexedGetUsingString(&pInfo->completedMissions, pchMissionName);
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetCompletedMissionName");
const char *exprGetCompletedMissionName(SA_PARAM_OP_VALID CompletedMission *mission)
{
	if (mission)
		return REF_STRING_FROM_HANDLE(mission->def);
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("MissionIsUGCFromName");
int exprMissionIsUGCFromName(SA_PARAM_OP_VALID Entity *pEntity, const char *pchMissionName)
{
	if (pchMissionName)
	{
		MissionDef *pMissionDef =  RefSystem_ReferentFromString(g_MissionDictionary, pchMissionName);
		return pMissionDef ? pMissionDef->ugcProjectID > 0 : 0;
	}
	return 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("MissionRepeatableCooldownFromName");
int exprMissionRepeatableCooldownFromName(SA_PARAM_OP_VALID Entity *pEntity, const char *pchMissionName)
{
	if (pchMissionName)
	{
		MissionDef *pMissionDef =  RefSystem_ReferentFromString(g_MissionDictionary, pchMissionName);
		if (pMissionDef)
		{
			if(pMissionDef->fRepeatCooldownHours)
				return pMissionDef->fRepeatCooldownHours;
			else if (pMissionDef->fRepeatCooldownHoursFromStart)
				return pMissionDef->fRepeatCooldownHoursFromStart;
			else return 0;
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("MissionCooldownSecondsLeftFromName");
int exprMissionCooldownSecondsLeftFromName(SA_PARAM_OP_VALID Entity *pEntity, const char *pchMissionName)
{
	if (pchMissionName)
	{
		const MissionCooldownInfo* pCooldownInfo = mission_GetCooldownInfo(pEntity, pchMissionName);
		if (pCooldownInfo)
		{
			return pCooldownInfo->uCooldownSecondsLeft;
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("MissionIsRepeatableFromName");
int exprMissionIsRepeatableFromName(SA_PARAM_OP_VALID Entity *pEntity, const char *pchMissionName)
{
	if (pchMissionName)
	{
		MissionDef *pMissionDef =  RefSystem_ReferentFromString(g_MissionDictionary, pchMissionName);
		return pMissionDef ? pMissionDef->repeatable : 0;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionCountString");
char *exprGetMissionCountString(SA_PARAM_OP_VALID Mission *mission)
{
	static char *estrBuffer = NULL;
	MissionDef *missionDef = mission ? GET_REF(mission->rootDefOrig) : NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (missionDef && missionDef->name != mission->missionNameOrig)
	{
		S32 i;
		for (i = 0; i < eaSize(&missionDef->subMissions); i++)
		{
			if (missionDef->subMissions[i]->name == mission->missionNameOrig)
			{
				missionDef = missionDef->subMissions[i];
				break;
			}
		}
	}

	mission_AppendObjectiveCountString(mission, missionDef, &estrBuffer);

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionTimerString");
char *exprGetMissionTimerString(SA_PARAM_OP_VALID Mission *mission)
{
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (mission && mission->expirationTime && mission_GetStateForUI(mission) == MissionState_InProgress){
		U32 seconds = mission_GetTimeRemainingSeconds(mission);
		estrConcatf(&estrBuffer, "%d:%02d", seconds/60, seconds%60);
	}

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SecondsToTimerString");
char *exprSecondsToTimerString(const char *pcMessageKey, U32 uSeconds)
{
	static char *estrBuffer = NULL;

	if(!estrBuffer)
	{
		estrCreate(&estrBuffer);
	}
	estrClear(&estrBuffer);

	FormatGameMessageKey(&estrBuffer,
		pcMessageKey,
		STRFMT_TIMER("Timer", uSeconds), STRFMT_END);

	return NULL_TO_EMPTY(estrBuffer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionCooldownCountString");
char *MissionCooldownCountString(SA_PARAM_OP_VALID Entity *pEntity, const char *pcMissionName, const char *pcMessageKey)
{
	static char *estrBuffer = NULL;

	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);

	if(!estrBuffer)
	{
		estrCreate(&estrBuffer);
	}
	estrClear(&estrBuffer);

	if(pInfo)
	{
		CompletedMission *pCompleted = eaIndexedGetUsingString(&pInfo->completedMissions, pcMissionName);
		if(pCompleted)
		{
			MissionDef *pMissionDef = GET_REF(pCompleted->def);
			if(pMissionDef)
			{
				const MissionCooldownInfo *missionInfo = mission_GetCooldownInfo(pEntity, pcMissionName);

				FormatGameMessageKey(&estrBuffer,
					pcMessageKey,
					STRFMT_INT("Count", missionInfo->uRepeatCount),
					STRFMT_INT("MaxCount", pMissionDef->iRepeatCooldownCount),
					STRFMT_INT("CountLeft", pMissionDef->iRepeatCooldownCount - missionInfo->uRepeatCount),
					STRFMT_TIMER("Timer", missionInfo->uCooldownSecondsLeft),
					STRFMT_END);
			}
		}
	}

	return NULL_TO_EMPTY(estrBuffer);
}

// Returns the state of the Mission:  "InProgress", "Succeeded", "Failed"
// This will always be the actual state of the mission; see "GetMissionDisplayState".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionState");
const char *exprGetMissionState(SA_PARAM_OP_VALID Mission *mission)
{
	const char *state = NULL;
	if(mission)
		state = StaticDefineIntRevLookup(MissionStateEnum, mission->state);
	return NULL_TO_EMPTY(state);
}

// Returns the Mission state for displaying in the UI: "InProgress", "Succeeded", "Failed"
// This may not be the actual state of the mission.  Some special Missions should be
// displayed as InProgress even though they are actually Succeeded.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionDisplayState");
const char *exprGetMissionDisplayState(SA_PARAM_OP_VALID Mission *mission)
{
	const char *state = NULL;
	if(mission)
		state = StaticDefineIntRevLookup(MissionStateEnum, mission_GetStateForUI(mission));
	return (state?state:"");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionHasUIString");
bool exprMissionHasUIString(SA_PARAM_OP_VALID Mission *mission)
{
	return mission_HasUIString(mission);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionHasDisplayName");
bool exprMissionHasDisplayName(SA_PARAM_OP_VALID Mission *mission)
{
	if (mission)
	{
		MissionDef *def = mission_GetDef(mission);
		if (def && GET_REF(def->displayNameMsg.hMessage))
		{
			const char *text = TranslateDisplayMessage(def->displayNameMsg);
			if (text && text[0])
				return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionHasReturnString");
bool exprMissionHasReturnString(SA_PARAM_OP_VALID Mission *mission)
{
	if (mission)
		return mission_HasReturnString(mission);
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsQualifyingUGCMission");
bool exprIsQualifyingUGCMission(SA_PARAM_OP_VALID Mission *mission)
{
	return (mission && mission->pUGCMissionData) ? mission->pUGCMissionData->bStatsQualifyForUGCRewards : false;
}

// Fill the given gen's list with all missions.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillMissionList");
void exprFillMissionList(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	if (pGen)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
		Mission ***peaMission = ui_GenGetManagedListSafe(pGen, Mission);
		int i, n;

		eaClear(peaMission);
		if (pInfo)
		{
			n = eaSize(&pInfo->missions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = pInfo->missions[i] ? mission_GetDef(pInfo->missions[i]) : NULL;
				if (pInfo->missions[i] && def && missiondef_HasDisplayName(def))
					eaPush(peaMission, pInfo->missions[i]);
			}
			n = eaSize(&pInfo->eaNonPersistedMissions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = pInfo->eaNonPersistedMissions[i] ? mission_GetDef(pInfo->eaNonPersistedMissions[i]) : NULL;
				if (pInfo->eaNonPersistedMissions[i] && def && missiondef_HasDisplayName(def))
					eaPush(peaMission, pInfo->eaNonPersistedMissions[i]);
			}
			n = eaSize(&pInfo->eaDiscoveredMissions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = pInfo->eaDiscoveredMissions[i] ? mission_GetDef(pInfo->eaDiscoveredMissions[i]) : NULL;
				if (pInfo->eaDiscoveredMissions[i] && def && missiondef_HasDisplayName(def))
					eaPush(peaMission, pInfo->eaDiscoveredMissions[i]);
			}
		}

		// Missions are owned by pEntity
		ui_GenSetManagedListSafe(pGen, peaMission, Mission, false);
	}
}

// Helper function; treat Normal and Nemesis missions as the same mission type for UI sorting purposes
static bool mission_MissionTypesSortTogether(MissionType a, MissionType b)
{
	bool aIsNormal = (a==MissionType_Normal || a==MissionType_Nemesis || a==MissionType_NemesisArc || a==MissionType_NemesisSubArc || a==MissionType_AutoAvailable || a==MissionType_Episode);
	bool bIsNormal = (b==MissionType_Normal || b==MissionType_Nemesis || b==MissionType_NemesisArc || b==MissionType_NemesisSubArc || b==MissionType_AutoAvailable || b==MissionType_Episode);
	if(a==b || (aIsNormal && bIsNormal))
		return true;
	return false;
}

// Fill the given gen's list with this type of mission ("Normal", "Perk").
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillMissionListByType");
void exprFillMissionListByType(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, const char *pchType)
{
	if (pGen)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
		Mission ***peaMission = ui_GenGetManagedListSafe(pGen, Mission);
		MissionType missionType = StaticDefineIntGetInt(MissionTypeEnum, pchType);
		int i, n;

		eaClear(peaMission);
		if (pInfo)
		{
			n = eaSize(&pInfo->missions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = pInfo->missions[i] ? mission_GetDef(pInfo->missions[i]) : NULL;
				if (def && mission_MissionTypesSortTogether(missionType, def->missionType) && missiondef_HasDisplayName(def))
					eaPush(peaMission, pInfo->missions[i]);
			}
			n = eaSize(&pInfo->eaNonPersistedMissions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = pInfo->eaNonPersistedMissions[i] ? mission_GetDef(pInfo->eaNonPersistedMissions[i]) : NULL;
				if (def && mission_MissionTypesSortTogether(missionType, def->missionType) && missiondef_HasDisplayName(def))
					eaPush(peaMission, pInfo->eaNonPersistedMissions[i]);
			}
			n = eaSize(&pInfo->eaDiscoveredMissions);
			for (i = 0; i < n; i++)
			{
				MissionDef *def = pInfo->eaDiscoveredMissions[i] ? mission_GetDef(pInfo->eaDiscoveredMissions[i]) : NULL;
				if (def && mission_MissionTypesSortTogether(missionType, def->missionType) && missiondef_HasDisplayName(def))
					eaPush(peaMission, pInfo->eaDiscoveredMissions[i]);
			}
		}

		// Missions are owned by pEntity
		ui_GenSetManagedListSafe(pGen, peaMission, Mission, false);
	}
}


// Crops the MissionList to X entries.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionListCrop");
void exprMissionListCrop(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, int iNumMissions)
{
	Mission ***peaList = ui_GenGetManagedListSafe(pGen, Mission);
	if (eaSize(peaList) > iNumMissions)
		eaSetSize(peaList, iNumMissions);
	ui_GenSetListSafe(pGen, peaList, Mission);
}

// Crops the MissionList to missions on the current map
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionListCropToCurrentMap");
void exprMissionListCropToCurrentMap(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	Mission ***peaList = ui_GenGetManagedListSafe(pGen, Mission);
	Entity* pEnt = entActivePlayerPtr();
	S32 iSize = eaSize(peaList);
	S32 i;

	if(!pEnt)
	{
		ui_GenSetListSafe(pGen, NULL, Mission);
		return;
	}

	for (i=iSize-1; i>=0; i--)
	{
		MissionDef *pMissionDef = mission_GetDef((*peaList)[i]);

		// Prune off all missions that whose maps are both non-null and
		// not the current map (we show mapless missions all the time)
		if (pMissionDef && pMissionDef->eaObjectiveMaps != NULL)
		{
			if(!missiondef_gclHasObjectiveOnCurrentMap(pEnt, pMissionDef))
				eaRemove(peaList, i);
		}
	}
	ui_GenSetListSafe(pGen, peaList,  Mission);
}

static void PopulateNodeLevelFromMissionDef(MissionListNode *pNode, MissionDef *pDef, Entity *pEntity)
{
	pNode->iLevel = pDef ? pDef->levelDef.missionLevel : 1;
	if(pEntity && pEntity->pChar)
		pNode->iLevelDelta = pNode->iLevel - pEntity->pChar->iLevelCombat;
	else
		pNode->iLevelDelta = 0;
}

static void PopulateNodeLevelFromMission(MissionListNode *pNode, Mission *pMission, Entity *pEntity)
{
	pNode->iLevel = pMission ? pMission->iLevel : 0;
	if(pEntity && pEntity->pChar)
		pNode->iLevelDelta = pNode->iLevel - pEntity->pChar->iLevelCombat;
	else
		pNode->iLevelDelta = 0;
}

static void PopulateNodeUITypeDataFromMissionDef(SA_PARAM_NN_VALID MissionListNode *pNode, SA_PARAM_NN_VALID MissionDef *pMissionDef)
{
	MissionUITypeData *pUITypeData = mission_GetMissionUITypeData(pMissionDef->eUIType);

	pNode->eUIType = pMissionDef->eUIType;

	if (pUITypeData) 
	{
		pNode->pchTranslatedUITypeName = TranslateDisplayMessage(pUITypeData->msgDisplayName);
		pNode->pchIconName = pUITypeData->pchIcon;
	}
}

static void PopulateNodeFromMission(MissionListNode *pNode, Mission *pMission, Entity *pEntity, MissionListNodeType type, S32 iIndent, bool bShadowMission, S32 iMissionNumber)
{
	MissionDef *pMissionDef = mission_GetDef(pMission);
	pMissionDef = missiondef_GetRootDef(pMissionDef);

	pNode->eType = type;
	pNode->pMission = pMission;
	pNode->pCompletedMission = NULL;
	pNode->pEnt = pEntity;
	pNode->iIndent = iIndent;
	pNode->iMissionNumber = iMissionNumber;
	pNode->bShadowMission = bShadowMission;

	if (pMissionDef) 
	{
		PopulateNodeUITypeDataFromMissionDef(pNode, pMissionDef);
	}

	PopulateNodeLevelFromMission(pNode, pMission, pEntity);
}

static void PopulateNodeFromCompletedMission(MissionListNode *pNode, CompletedMission *pCompletedMission, Entity *pEntity, MissionListNodeType type, S32 iIndent, bool bShadowMission, S32 iMissionNumber)
{
	MissionDef *pMissionDef = pCompletedMission ? GET_REF(pCompletedMission->def) : NULL;
	pMissionDef = missiondef_GetRootDef(pMissionDef);

	pNode->eType = type;
	pNode->pMission = NULL;
	pNode->pCompletedMission = pCompletedMission;
	pNode->pEnt = pEntity;
	pNode->iIndent = iIndent;
	pNode->iMissionNumber = iMissionNumber;
	pNode->bShadowMission = bShadowMission;

	if (pMissionDef) 
	{
		PopulateNodeUITypeDataFromMissionDef(pNode, pMissionDef);
	}
	
	PopulateNodeLevelFromMissionDef(pNode, pMissionDef, pEntity);
}

static void PopulateRowFromDef(MissionJournalRow *pRow, MissionDef *pDef, Entity *pEntity, MissionCreditType eCreditType)
{
	MissionDef *pRootDef = missiondef_GetRootDef(pDef);
	ContainerID idNemesis = player_GetPrimaryNemesisID(pEntity);

	pRow->pchIconName = SAFE_MEMBER(pDef, pchIconName); // pDef->pchIconName is pooled
	pRow->iPerkPoints = SAFE_MEMBER(pDef, iPerkPoints);

	estrClear(&pRow->estrName);
	if (pDef)
	{
		missionsystem_ClientFormatMessagePtr("MissionUI", pEntity, pDef, idNemesis,
			&pRow->estrName, GET_REF(pDef->displayNameMsg.hMessage));
	}

	// Flashback Missions need to show the Display Name differently
	if (eCreditType == MissionCreditType_Flashback && GET_REF(g_MissionSystemMsgs.hFlashbackDisplayName)){
		static char *estrBuffer = NULL;
		if (!estrBuffer)
			estrCreate(&estrBuffer);
		estrClear(&estrBuffer);
		FormatMessagePtr(&estrBuffer, GET_REF(g_MissionSystemMsgs.hFlashbackDisplayName), STRFMT_STRING("MissionName", pRow->estrName), STRFMT_END);
		estrCopy(&pRow->estrName, &estrBuffer);
	}

	estrClear(&pRow->estrDescription);
//	if(GET_REF(pDef->summaryMsg.hMessage))
//	{
//		missionsystem_ClientFormatMessagePtr("MissionUI", pEntity, pDef, idNemesis,
//			&pRow->estrDescription, GET_REF(pDef->summaryMsg.hMessage));
//	}
//	else
//	{
//		missionsystem_ClientFormatMessagePtr("MissionUI", pEntity, pDef, idNemesis,
//			&pRow->estrDescription, GET_REF(pDef->uiStringMsg.hMessage));
//	}

	estrClear(&pRow->estrDetail);
//	missionsystem_ClientFormatMessagePtr("MissionUI", pEntity, pDef, idNemesis,
//		&pRow->estrDetail, GET_REF(pDef->detailStringMsg.hMessage));

	if(pDef && pDef->missionType == MissionType_Perk)
		pRow->pchTranslatedCategoryName = missiondef_GetTranslatedCategoryName(pDef);
	else
		pRow->pchTranslatedCategoryName = NULL;

	pRow->iLevel = pDef ? pDef->levelDef.missionLevel : 0;
	pRow->iMinLevel = pDef ? pDef->iMinLevel : 0;

	if(pEntity && pEntity->pChar)
		pRow->iLevelDelta = pRow->iLevel - pEntity->pChar->iLevelCombat;
	else
		pRow->iLevelDelta = 0;

	pRow->iTeamSize = SAFE_MEMBER(pDef, iSuggestedTeamSize);
	pRow->bScalesForTeam = SAFE_MEMBER(pDef, bScalesForTeamSize);
	pRow->bCanDrop = !SAFE_MEMBER(pDef, doNotAllowDrop);
	pRow->pEnt = pEntity;
	pRow->pchTranslatedUITypeName = NULL;
	pRow->eUIType = SAFE_MEMBER(pRootDef, eUIType);

	if (pRow->eUIType != kMissionUIType_None) {
		MissionUITypeData* pUITypeData = mission_GetMissionUITypeData(pRootDef->eUIType);
		if (pUITypeData) {
			pRow->pchTranslatedUITypeName = TranslateDisplayMessage(pUITypeData->msgDisplayName);
			pRow->pchIconName = pUITypeData->pchIcon;
		}
	}
}

static void PopulateRowFromMission(MissionJournalRow *pRow, Mission *pMission, Entity *pEntity)
{
	MissionDef *pDef = mission_GetDef(pMission);

	if(pDef)
		PopulateRowFromDef(pRow, pDef, pEntity, pMission->eCreditType);

	pRow->iLevel = pMission->iLevel;

	if(pEntity && pEntity->pChar)
		pRow->iLevelDelta = pRow->iLevel - pEntity->pChar->iLevelCombat;
	else
		pRow->iLevelDelta = 0;

	SET_HANDLE_FROM_REFERENT(g_MissionDictionary, mission_GetDef(pMission), pRow->hDef);

	pRow->bCompleted = false;

	if(pMission->iLevel)
		pRow->iLevel = pMission->iLevel;

	pRow->iCount = 0;
	pRow->iTarget = 0;
	mission_GetObjectiveProgressStatsRecursive(pEntity,
		pMission,
		&pRow->iCount,
		&pRow->iTarget);

	pRow->type = MissionJournalRowType_Mission;
	pRow->mission = pMission;
	pRow->pCompletedMission = NULL;
	pRow->iIndent = 0;
	pRow->pEnt = pEntity;
}

static bool CachedMissionIsCurrent(Entity* pEnt, CachedMissionData* pData)
{
	ProgressionInfo* pInfo = progression_GetInfoFromPlayer(pEnt);

	if (pInfo && pData->pchProgressionNode)
	{
		GameProgressionNodeDef* pNodeDef = progression_NodeDefFromName(pData->pchProgressionNode);
		int iMissionIndex = progression_FindMissionForNode(pNodeDef, pData->pchMissionDef);
		return progression_CheckMissionRequirementsEx(pEnt, pNodeDef, iMissionIndex);
	}
	return false;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void ReceiveMissionDisplayData(CachedMissionList* pRequestData)
{
	if (pRequestData)
	{
		Entity* pEnt = entActivePlayerPtr();
		S32 i;
		U32 uNow = timeSecondsSince2000();

		if (!s_eaCachedMissionData)
		{
			eaCreate(&s_eaCachedMissionData);
			eaIndexedEnable(&s_eaCachedMissionData, parse_CachedMissionData);
		}
		if (!s_eaCachedMissionReward)
		{
			eaCreate(&s_eaCachedMissionReward);
			eaIndexedEnable(&s_eaCachedMissionReward, parse_CachedMissionReward);
		}

		for (i = eaSize(&pRequestData->eaData) - 1; i >= 0; i--)
		{
			CachedMissionData *pData = pRequestData->eaData[i];
			CachedMissionData *pOld = eaIndexedGetUsingString(&s_eaCachedMissionData, pData->pchMissionDef);
			if (pOld)
			{
				StructCopyAll(parse_CachedMissionData, pData, pOld);
			}
			else
			{
				pOld = StructClone(parse_CachedMissionData, pData);
				if (!pOld)
					continue;
				eaPush(&s_eaCachedMissionData, pOld);
			}
			pOld->uTime = uNow;
			pOld->bUpdate = false;
			pOld->bCurrent = CachedMissionIsCurrent(pEnt, pOld);
		}

		for (i = eaSize(&pRequestData->eaRewardData) - 1; i >= 0; i--)
		{
			CachedMissionReward *pReward = pRequestData->eaRewardData[i];
			CachedMissionReward *pOld = eaIndexedGetUsingString(&s_eaCachedMissionReward, pReward->pchMissionDef);
			if (pOld)
			{
				StructCopyAll(parse_CachedMissionReward, pReward, pOld);
			}
			else
			{
				pOld = StructClone(parse_CachedMissionReward, pReward);
				if (!pOld)
					continue;
				eaPush(&s_eaCachedMissionReward, pOld);
			}
			pOld->uTime = uNow;
			pOld->bUpdate = false;
		}
	}
}

/////// Display Data Request Queue
#define PERIODIC_PROGRESSION_MISSION_INFO_REQUEST_TIME 30
static const char **s_ppchUpdateQueue = NULL;
static const char **s_ppchUpdateRewardQueue = NULL;
static U32 s_uLastRequestTime = 0;

static void CheckMissionDisplayDataRequestQueue()
{
	U32 uCurrentTime = timeSecondsSince2000();
	if (s_uLastRequestTime != uCurrentTime && (eaSize(&s_ppchUpdateQueue) > 0 || eaSize(&s_ppchUpdateRewardQueue) > 0))
	{
		CachedMissionRequest Request;
		StructInit(parse_CachedMissionRequest, &Request);
		if (eaSize(&s_ppchUpdateQueue) > 0)
		{
			eaCopy(&Request.ppchMissionDefs, &s_ppchUpdateQueue);
			ServerCmd_RequestMissionDisplayData(&Request);
			eaClear(&s_ppchUpdateQueue);
		}
		if (eaSize(&s_ppchUpdateRewardQueue) > 0)
		{
			eaCopy(&Request.ppchMissionDefs, &s_ppchUpdateRewardQueue);
			ServerCmd_RequestMissionRewardData(&Request);
			eaClear(&s_ppchUpdateRewardQueue);
		}
		StructDeInit(parse_CachedMissionRequest, &Request);
		s_uLastRequestTime = uCurrentTime;
	}
}

static void AddMissionRequestToQueue(const char ***peachQueue, Entity* pEnt, MissionInfo *pMissionInfo, SA_PARAM_OP_STR const char* pchMissionDefs)
{
	U32 uCurrentTime = timeSecondsSince2000();
	
	const char *pchMissionDef = allocAddString(pchMissionDefs);
	Mission* pMission = mission_FindMissionFromRefString(pMissionInfo, pchMissionDef);
	MissionState eState = pMission ? pMission->state : -1;

	if (peachQueue == &s_ppchUpdateQueue)
	{
		CachedMissionData *pData = eaIndexedGetUsingString(&s_eaCachedMissionData, pchMissionDef);
		if (!pData || 
			(!pData->bUpdate && 
			(pData->eState != eState ||
			pData->uTime + PERIODIC_PROGRESSION_MISSION_INFO_REQUEST_TIME <= uCurrentTime || 
			(!pData->bCurrent && CachedMissionIsCurrent(pEnt, pData)))))
		{
			eaPushUnique(peachQueue, pchMissionDef);
			if (pData)
				pData->bUpdate = true;
		}
	}
	else if (peachQueue == &s_ppchUpdateRewardQueue)
	{
		CachedMissionReward *pReward = eaIndexedGetUsingString(&s_eaCachedMissionReward, pchMissionDef);
		if (!pReward || 
			(!pReward->bUpdate &&
			pReward->uTime + PERIODIC_PROGRESSION_MISSION_INFO_REQUEST_TIME <= uCurrentTime))
		{
			eaPushUnique(peachQueue, pchMissionDef);
			if (pReward)
				pReward->bUpdate = true;
		}
	}
}

static void RequestMissionDisplayData(Entity* pEnt, const char** ppchMissionDefs)
{
	int i;
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	for (i = eaSize(&ppchMissionDefs) - 1; i >= 0; i--)
	{
		AddMissionRequestToQueue(&s_ppchUpdateQueue, pEnt, pInfo, ppchMissionDefs[i]);
	}
	CheckMissionDisplayDataRequestQueue();
}

static void RequestMissionRewardData(Entity* pEnt, const char** ppchMissionDefs)
{
	int i;
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	for (i = eaSize(&ppchMissionDefs) - 1; i >= 0; i--)
	{
		AddMissionRequestToQueue(&s_ppchUpdateRewardQueue, pEnt, pInfo, ppchMissionDefs[i]);
	}
	CheckMissionDisplayDataRequestQueue();
}

static void PopulateRowFromProgressionMissionInternal(MissionJournalRow *pRow,
													  GameProgressionNodeDef *pProgNode,
													  int iProgMissionIndex,
													  Entity *pEnt)
{
	GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pProgNode);
	GameProgressionMission* pProgMission = pProgNode->pMissionGroupInfo->eaMissions[iProgMissionIndex];
	CachedMissionData* pData = eaIndexedGetUsingString(&s_eaCachedMissionData, pProgMission->pchMissionName);

	if (!pRow->pProgressionData)
		pRow->pProgressionData = StructCreate(parse_ProgressionMissionData);

	pRow->type = MissionJournalRowType_ProgressionMission;
	pRow->pProgressionData->pchMissionDef = pProgMission->pchMissionName;
	pRow->pProgressionData->pchDescription = TranslateDisplayMessage(pProgMission->msgDescription);
	pRow->pProgressionData->pchImage = pProgMission->pchImage;
	pRow->pProgressionData->bAvailable = progression_CheckMissionRequirementsEx(pEnt, pProgNode, iProgMissionIndex);
	pRow->pProgressionData->bOptional = !!progression_IsMissionOptional(pEnt, pProgMission);
	pRow->pProgressionData->bSkippable = progression_IsMissionSkippable(pEnt, pProgNode, iProgMissionIndex);
	SET_HANDLE_FROM_REFERENT("GameProgressionNodeDef", pBranchNodeDef, pRow->pProgressionData->hStoryArcNode);
	SET_HANDLE_FROM_REFERENT("GameProgressionNodeDef", pProgNode, pRow->pProgressionData->hNode);

	if (g_GameProgressionConfig.bMustMeetRequirementsToSkipMissions &&
		(!pRow->mission || !pRow->mission->state == MissionState_InProgress) && (!pData || !pData->bAvailable || pData->eCreditType != MissionCreditType_Primary))
	{
		pRow->pProgressionData->bSkippable = false;
	}
	if (pData)
	{
		pRow->pProgressionData->pchContact = pData->pchContact;
		if (stricmp_safe(pRow->pProgressionData->pchContactKey, pData->pchContactKey))
		{
			StructFreeStringSafe(&pRow->pProgressionData->pchContactKey);
			pRow->pProgressionData->pchContactKey = StructAllocString(pData->pchContactKey);
		}
		pRow->pProgressionData->pchContactDisplayName = TranslateMessageRefDefault(pData->hContactDisplayName, pData->pchContact);
		pRow->pProgressionData->pchCreditType = StaticDefineIntRevLookup(MissionCreditTypeEnum, pData->eCreditType);
		pRow->pProgressionData->uSecondaryLockoutTime = pData->uSecondaryLockoutTime;
	}
	else
	{
		pRow->pProgressionData->pchContact = NULL;
		StructFreeStringSafe(&pRow->pProgressionData->pchContactKey);
		pRow->pProgressionData->pchContactDisplayName = NULL;
		pRow->pProgressionData->pchCreditType = NULL;
		pRow->pProgressionData->uSecondaryLockoutTime = 0;
	}

	if (pData && !pData->bAvailable)
		pRow->pProgressionData->bAvailable = false;

	if (pData && GET_REF(pData->hDisplayName))
		pRow->pProgressionData->pchMissionDisplayName = TranslateMessageRefDefault(pData->hDisplayName, pData->pchMissionDef);
	else
		pRow->pProgressionData->pchMissionDisplayName = NULL;

	pRow->iMinLevel = pData ? pData->iMinLevel : 0;
}

static void PopulateRowFromProgressionMission(MissionJournalRow *pRow,
											  Mission *pMission,
											  GameProgressionNodeDef *pProgNode,
											  int iProgMissionIndex,
											  Entity *pEntity)
{
	if (pMission)
	{
		PopulateRowFromMission(pRow, pMission, pEntity);
	}
	else
	{
		GameProgressionMission *pProgMission = pProgNode->pMissionGroupInfo->eaMissions[iProgMissionIndex];
		MissionDef *pMissionDef = missiondef_FindMissionByName(NULL, pProgMission->pchMissionName);

		PopulateRowFromDef(pRow, pMissionDef, pEntity, MissionCreditType_Primary);
		if (pMissionDef) {
			SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pRow->hDef);
		} else {
			REMOVE_HANDLE(pRow->hDef);
		}
		pRow->bCompleted = false;
		pRow->iLevel = 0;
		pRow->iCount = 0;
		pRow->iTarget = 0;
		pRow->mission = NULL;
		pRow->pCompletedMission = NULL;
		pRow->iIndent = 0;
		pRow->pEnt = pEntity;
	}

	PopulateRowFromProgressionMissionInternal(pRow, pProgNode, iProgMissionIndex, pEntity);
}

static void PopulateRowFromCompletedMission(MissionJournalRow *pRow, CompletedMission *pMission, Entity *pEntity)
{
	MissionDef *pDef = GET_REF(pMission->def);

	PopulateRowFromDef(pRow, pDef, pEntity, MissionCreditType_Primary);

	COPY_HANDLE(pRow->hDef, pMission->def);

	if(pDef && pDef->levelDef.eLevelType == MissionLevelType_PlayerLevel) {
		pRow->iLevel = entity_GetSavedExpLevel(pEntity);
	}

	pRow->bCompleted = true;

	pRow->iCount = 0;
	pRow->iTarget = 0;

	pRow->type = MissionJournalRowType_CompletedMission;
	pRow->mission = NULL;
	pRow->pCompletedMission = pMission;
	pRow->pEnt = pEntity;
}

static void PopulateRowFromCompletedProgressionMission(MissionJournalRow *pRow,
													   CompletedMission *pMission,
													   GameProgressionNodeDef *pProgNode,
													   int iProgMissionIndex,
													   Entity *pEntity)
{
	PopulateRowFromCompletedMission(pRow, pMission, pEntity);
	PopulateRowFromProgressionMissionInternal(pRow, pProgNode, iProgMissionIndex, pEntity);
}

// Fill in the description and other strings on the MissionJournalRow if they haven't already
//   been set.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PopulateMissionJournalRowStrings");
void PopulateMissionJournalRowStrings(ExprContext *pContext, SA_PARAM_NN_VALID MissionJournalRow *pRow, SA_PARAM_OP_VALID Entity *pEntity)
{
	MissionDef *pDef = GET_REF(pRow->hDef);

	if(pDef)
	{
		ContainerID idNemesis = 0;

		if(!pRow->estrDescription || !pRow->estrDescription[0])
		{
			if(!idNemesis)
				idNemesis = player_GetPrimaryNemesisID(pEntity);

			if(GET_REF(pDef->summaryMsg.hMessage))
			{
				missionsystem_ClientFormatMessagePtr("MissionUI", pEntity, pDef, idNemesis,
					&pRow->estrDescription, GET_REF(pDef->summaryMsg.hMessage));
			}
			else
			{
				missionsystem_ClientFormatMessagePtr("MissionUI", pEntity, pDef, idNemesis,
					&pRow->estrDescription, GET_REF(pDef->uiStringMsg.hMessage));
			}
		}

		if(!pRow->estrDetail || !*pRow->estrDetail)
		{
			if(!idNemesis)
				idNemesis = player_GetPrimaryNemesisID(pEntity);

			missionsystem_ClientFormatMessagePtr("MissionUI", pEntity, pDef, idNemesis,
				&pRow->estrDetail, GET_REF(pDef->detailStringMsg.hMessage));
		}
	}
}

void PopulateEpisodeRowsFromMission(Entity *pEntity, Mission *pParentMission, MissionDef *pEpisodeMissionDef, MissionJournalRow*** peaMissionRowList, int *piNumValidRows)
{
	int i;
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	MissionDef *pParentMissionDef = mission_GetDef(pParentMission);
	for (i = 0; i < eaSize(&pParentMission->childFullMissions); i++)
	{
		Mission *pSubMission = mission_GetMissionByName(pInfo, pParentMission->childFullMissions[i]);
		MissionDef *pSubMissionDef = pSubMission ? mission_GetDef(pSubMission) : NULL;
		if (pSubMission)
		{
			if (GET_REF(pSubMissionDef->displayNameMsg.hMessage))
			{
				MissionJournalRow *pRow = eaGetStruct(peaMissionRowList, parse_MissionJournalRow, (*piNumValidRows)++);
				PopulateRowFromMission(pRow, pSubMission, pEntity);
				pRow->iIndent = 1;
			}
			PopulateEpisodeRowsFromMission(pEntity, pSubMission, pEpisodeMissionDef, peaMissionRowList, piNumValidRows);
		}
	}
	for (i = 0; i < eaSize(&pParentMission->children); i++)
	{
		Mission *pSubMission = pParentMission->children[i];
		MissionDef *pSubMissionDef = pSubMission ? mission_GetDef(pSubMission) : NULL;
		if (pSubMission)
		{
			if (GET_REF(pSubMissionDef->displayNameMsg.hMessage))
			{
				MissionJournalRow *pRow = eaGetStruct(peaMissionRowList, parse_MissionJournalRow, (*piNumValidRows)++);
				PopulateRowFromMission(pRow, pSubMission, pEntity);
				pRow->iIndent = 1;
			}
			PopulateEpisodeRowsFromMission(pEntity, pSubMission, pEpisodeMissionDef, peaMissionRowList, piNumValidRows);
		}
	}
}

void PopulateRowsFromMissionLists(Entity *pEntity, CONST_EARRAY_OF(Mission) eaMissions, MissionJournalRow*** peaMissionRowList, int *piNumValidRows, bool bHideHiddenChildren)
{
	int i, iSize = eaSize(&eaMissions);
	for (i = 0; i < iSize; i++)
	{
		if (bHideHiddenChildren && eaMissions[i]->bHiddenFullChild)
			continue;
		if (mission_HasDisplayName(eaMissions[i]))
		{
			MissionDef *pDef = mission_GetDef(eaMissions[i]);
			MissionJournalRow *pRow = eaGetStruct(peaMissionRowList, parse_MissionJournalRow, (*piNumValidRows)++);
			PopulateRowFromMission(pRow, eaMissions[i], pEntity);

			if(pDef)
				pRow->pchTranslatedCategoryName = missiondef_GetTranslatedCategoryName(pDef);

			if (SAFE_MEMBER(pDef, missionType) == MissionType_Episode)
				PopulateEpisodeRowsFromMission(pEntity, eaMissions[i], pDef, peaMissionRowList, piNumValidRows);
		}
	}
}

// Fill the gen's model with only uncompleted missions
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillMissionJournalList");
void exprFillMissionJournalList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	MissionJournalRow*** peaMissionRowList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	int iNumValidRows = 0;

	if (pInfo)
	{
		PopulateRowsFromMissionLists(pEntity, pInfo->missions, peaMissionRowList, &iNumValidRows, true);
		PopulateRowsFromMissionLists(pEntity, pInfo->eaNonPersistedMissions, peaMissionRowList, &iNumValidRows, false);
		PopulateRowsFromMissionLists(pEntity, pInfo->eaDiscoveredMissions, peaMissionRowList, &iNumValidRows, false);
	}

	eaSetSizeStruct(peaMissionRowList, parse_MissionJournalRow, iNumValidRows);

	ui_GenSetManagedListSafe(pGen, peaMissionRowList, MissionJournalRow, true);
}

// Fill the gen's model with only completed missions.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillCompletedMissionJournalList");
void exprFillCompletedMissionJournalList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	MissionJournalRow*** peaCompletedMissionRowList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	int i, n, numValidRows = 0;

	if (pInfo)
	{
		n = eaSize(&pInfo->completedMissions);
		for (i = 0; i < n; i++)
		{
			CompletedMission *mission = pInfo->completedMissions[i];
			const MissionCooldownInfo *missionCooldownInfo;
			MissionDef *pMissionDef = GET_REF(mission->def);

			if(!pMissionDef)
			{
				continue;
			}

			missionCooldownInfo = mission_GetCooldownInfo(pEntity, pMissionDef->name);

			if(mission->bHidden && !missionCooldownInfo->bIsInCooldownBlock)
			{
				continue;
			}

			if(missiondef_HasDisplayName(pMissionDef))
			{
				MissionJournalRow *pRow = eaGetStruct(peaCompletedMissionRowList, parse_MissionJournalRow, numValidRows++);
				PopulateRowFromCompletedMission(pRow, pInfo->completedMissions[i], pEntity);
			}
		}
	}

	eaSetSizeStruct(peaCompletedMissionRowList, parse_MissionJournalRow, numValidRows);

	if (pGen)
		ui_GenSetManagedListSafe(pGen, peaCompletedMissionRowList, MissionJournalRow, true);
}

// Fill the gen's model with all active missions held by any team mates.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillTeamMissionJournalList");
void exprFillTeamMissionJournalList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pPlayer)
{
	MissionJournalRow ***peaMissionRowList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayer);
	int iMember, iMission;
	int iNumRows = 0;

	if(pPlayer && pInfo)
	{
		for (iMission = 0; iMission < eaSize(&pInfo->eaTeamMissions); iMission++) {
			TeamMissionInfo *pTeamInfo = pInfo->eaTeamMissions[iMission];
			MissionDef *pDef = GET_REF(pTeamInfo->hDef);
			bool bSet = false;

			if (!pDef) {
				continue;
			}

			while (eaSize(peaMissionRowList) <= iNumRows) {
				eaPush(peaMissionRowList, StructCreate(parse_MissionJournalRow));
			}
			eaCopy(&(*peaMissionRowList)[iNumRows]->eaTeamMissions, &pTeamInfo->eaTeammates);
			for (iMember = 0; iMember < eaSize(&pTeamInfo->eaTeammates); iMember++) {
				if (pTeamInfo->eaTeammates[iMember]->iEntID == pPlayer->myContainerID) {
					if (pTeamInfo->eaTeammates[iMember]->pMission) {
						PopulateRowFromMission((*peaMissionRowList)[iNumRows], pTeamInfo->eaTeammates[iMember]->pMission, pPlayer);
						(*peaMissionRowList)[iNumRows]->pchTranslatedCategoryName = missiondef_GetTranslatedCategoryName(pDef);
						bSet = true;
					}
					break;
				}
			}
			if (!bSet) {
				for (iMember = 0; iMember < eaSize(&pTeamInfo->eaTeammates); iMember++) {
					if (pTeamInfo->eaTeammates[iMember]->pMission) {
						PopulateRowFromMission((*peaMissionRowList)[iNumRows], pTeamInfo->eaTeammates[iMember]->pMission, pPlayer);
						(*peaMissionRowList)[iNumRows]->pchTranslatedCategoryName = missiondef_GetTranslatedCategoryName(pDef);
						break;
					}
				}
			}

			iNumRows++;
		}
	}

	eaSetSizeStruct(peaMissionRowList, parse_MissionJournalRow, iNumRows);
	ui_GenSetManagedListSafe(pGen, peaMissionRowList, MissionJournalRow, true);
}

// Get a specific team member's instance of a mission from a mission row
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetTeamMissionFromRow");
bool exprGetTeamMissionFromRow(ExprContext *pContext, SA_PARAM_NN_VALID MissionJournalRow *pRow, S32 iMemberNum)
{
	TeammateMission *pStatus = pRow ? eaGet(&pRow->eaTeamMissions, iMemberNum) : NULL;
	return pStatus && pStatus->pMission ? true : false;
}

// Get a specific team member's instance of a mission from a mission row
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetTeamMissionStateFromRow");
int exprGetTeamMissionStateFromRow(ExprContext *pContext, SA_PARAM_NN_VALID MissionJournalRow *pRow, S32 iMemberNum)
{
	TeammateMission *pStatus = pRow ? eaGet(&pRow->eaTeamMissions, iMemberNum) : NULL;
	return pStatus && pStatus->pMission ? pStatus->pMission->state : MissionState_InProgress;
}

// Get a specific team member's instance of a mission from a mission row
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetTeamMissionCreditTypeFromRow");
int exprGetTeamMissionCreditTypeFromRow(ExprContext *pContext, SA_PARAM_NN_VALID MissionJournalRow *pRow, S32 iMemberNum)
{
	TeammateMission *pStatus = pRow ? eaGet(&pRow->eaTeamMissions, iMemberNum) : NULL;
	return pStatus ? (pStatus->pMission ? pStatus->pMission->eCreditType : pStatus->eCreditType) : MissionCreditType_Ineligible;
}

// Get a specific team member's entity from a mission row
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetTeamMissionEntityFromRow");
SA_RET_OP_VALID Entity *exprGetTeamMissionEntityFromRow(ExprContext *pContext, SA_PARAM_NN_VALID MissionJournalRow *pRow, S32 iMemberNum)
{
	if (pRow) {
		Entity *pEnt = entActivePlayerPtr();
		Team *pTeam = pEnt ? team_GetTeam(pEnt) : NULL;
		TeammateMission *pStatus = eaGet(&pRow->eaTeamMissions, iMemberNum);
		TeamMember *pMember = pStatus ? team_FindMemberID(pTeam, pStatus->iEntID) : NULL;
		return pMember ? GET_REF(pMember->hEnt) : NULL;
	}
	return NULL;
}

// Fill the gen's model with completed and uncompleted missions.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillAllMissionJournalList");
void exprFillAllMissionJournalList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	MissionJournalRow*** peaMissionRowList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	int i, numValidRows = 0;

	if (pInfo)
	{
		for (i = 0; i < eaSize(&pInfo->missions); i++)
		{
			if (pInfo->missions[i]->bHiddenFullChild)
				continue;

			if (mission_HasDisplayName(pInfo->missions[i]))
			{
				PopulateRowFromMission(eaGetStruct(peaMissionRowList, parse_MissionJournalRow, numValidRows++), pInfo->missions[i], pEntity);
			}
		}

		for (i = 0; i < eaSize(&pInfo->eaNonPersistedMissions); i++)
		{
			if (mission_HasDisplayName(pInfo->eaNonPersistedMissions[i]))
			{
				PopulateRowFromMission(eaGetStruct(peaMissionRowList, parse_MissionJournalRow, numValidRows++), pInfo->eaNonPersistedMissions[i], pEntity);
			}
		}

		for (i = 0; i < eaSize(&pInfo->eaDiscoveredMissions); i++)
		{
			if (mission_HasDisplayName(pInfo->eaDiscoveredMissions[i]))
			{
				PopulateRowFromMission(eaGetStruct(peaMissionRowList, parse_MissionJournalRow, numValidRows++), pInfo->eaDiscoveredMissions[i], pEntity);
			}
		}

		for (i = 0; i < eaSize(&pInfo->completedMissions); i++)
		{
			CompletedMission *mission = pInfo->completedMissions[i];

			if (mission->bHidden)
				continue;

			if (missiondef_HasDisplayName(GET_REF(mission->def)))
			{
				PopulateRowFromCompletedMission(eaGetStruct(peaMissionRowList, parse_MissionJournalRow, numValidRows++), pInfo->completedMissions[i], pEntity);
			}
		}
	}

	eaSetSizeStruct(peaMissionRowList, parse_MissionJournalRow, numValidRows);
	ui_GenSetManagedListSafe(pGen, peaMissionRowList, MissionJournalRow, true);
}

void exprFillSelectedPerksListEx(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_STR const char *pchCategoryName, S32 bShowCompleted, S32 bShowInProgress, int (*comparitorFunc)(const MissionJournalRow**, const MissionJournalRow**))
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	MissionJournalRow*** peaMissionRowList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	int i, numValidRows = 0;

	if (pInfo)
	{
		for (i = 0; i < eaSize(&pInfo->missions); i++)
		{
			MissionDef *pDef = mission_GetDef(pInfo->missions[i]);
			const char *pchDefCategory = missiondef_GetTranslatedCategoryName(pDef);

			if (pDef
				&& missiondef_HasDisplayName(pDef)
				&& pDef->missionType == MissionType_Perk
				&& bShowInProgress
				&& pInfo->missions[i]->bDiscovered
				&& !pDef->bIsTutorialPerk
				&& (!pchCategoryName || pchCategoryName[0] == '\0' || !stricmp(pchCategoryName, pchDefCategory))
				)
			{
				PopulateRowFromMission(eaGetStruct(peaMissionRowList, parse_MissionJournalRow, numValidRows++), pInfo->missions[i], pEntity);
			}
		}

		for (i = 0; i < eaSize(&pInfo->eaDiscoveredMissions); i++)
		{
			MissionDef *pDef = mission_GetDef(pInfo->eaDiscoveredMissions[i]);
			const char *pchDefCategory = missiondef_GetTranslatedCategoryName(pDef);

			if (pDef
				&& missiondef_HasDisplayName(pDef)
				&& pDef->missionType == MissionType_Perk
				&& bShowInProgress
				&& pInfo->eaDiscoveredMissions[i]->bDiscovered
				&& !pDef->bIsTutorialPerk
				&& (!pchCategoryName || pchCategoryName[0] == '\0' || !stricmp(pchCategoryName, pchDefCategory))
				)
			{
				PopulateRowFromMission(eaGetStruct(peaMissionRowList, parse_MissionJournalRow, numValidRows++), pInfo->eaDiscoveredMissions[i], pEntity);
			}
		}

		if (bShowCompleted)
		{
			for (i = 0; i < eaSize(&pInfo->completedMissions); i++)
			{
				CompletedMission *mission = pInfo->completedMissions[i];
				MissionDef *pDef = GET_REF(mission->def);
				const char *pchDefCategory = missiondef_GetTranslatedCategoryName(pDef);

				if (pDef
					&& missiondef_HasDisplayName(pDef)
					&& pDef->missionType == MissionType_Perk
					&& !pDef->bIsTutorialPerk
					&& (!pchCategoryName || pchCategoryName[0] == '\0' || !stricmp(pchCategoryName, pchDefCategory))
					)
				{
					PopulateRowFromCompletedMission(eaGetStruct(peaMissionRowList, parse_MissionJournalRow, numValidRows++), pInfo->completedMissions[i], pEntity);
				}
			}
		}
	}

	eaSetSizeStruct(peaMissionRowList, parse_MissionJournalRow, numValidRows);
	eaQSort(*peaMissionRowList, comparitorFunc);
	ui_GenSetManagedListSafe(pGen, peaMissionRowList, MissionJournalRow, true);
}

// Fill the gen's model with completed and uncompleted Perks.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillPerksList");
void exprFillSelectedPerksList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_STR const char *pchCategoryName, S32 bShowCompleted, S32 bShowInProgress)
{
	exprFillSelectedPerksListEx(pContext, pGen, pEntity, pchCategoryName, bShowCompleted, bShowInProgress, missionjournalrow_SortByDisplayName);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillTutorialPerksList");
void exprFillTutorialPerksList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	MissionJournalRow*** peaMissionRowList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	int i, numValidRows = 0;

	if (pInfo)
	{
		for (i = 0; i < eaSize(&pInfo->completedMissions); i++)
		{
			CompletedMission *mission = pInfo->completedMissions[i];

			if (mission->bHidden || (GET_REF(mission->def) ? !GET_REF(mission->def)->bIsTutorialPerk : true))
				continue;

			if (missiondef_HasDisplayName(GET_REF(mission->def)))
			{
				PopulateRowFromCompletedMission(eaGetStruct(peaMissionRowList, parse_MissionJournalRow, numValidRows++), pInfo->completedMissions[i], pEntity);
			}
		}
	}

	eaSetSizeStruct(peaMissionRowList, parse_MissionJournalRow, numValidRows);
	eaQSort(*peaMissionRowList, missionjournalrow_SortByDisplayName);
	ui_GenSetManagedListSafe(pGen, peaMissionRowList, MissionJournalRow, true);
}

// Fill the gen's model with completed and uncompleted Perks.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillSortedPerksList");
void exprFillSelectedPerksListSorted(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_STR const char *pchCategoryName, S32 bShowCompleted, S32 bShowInProgress, const char* pchSortMethod)
{
	int (*comparitorFunc)(const MissionJournalRow**, const MissionJournalRow**);
	if (stricmp("DisplayName", pchSortMethod) == 0)
		comparitorFunc = missionjournalrow_SortByDisplayName;
	else if (stricmp("Rank", pchSortMethod) == 0)
		comparitorFunc = missionjournalrow_SortByLogicalName;
	else if (stricmp("SortPriority", pchSortMethod) == 0)
		comparitorFunc = missionjournalrow_SortBySortPriority;
	else
		comparitorFunc = NULL;

	if (comparitorFunc)
		exprFillSelectedPerksListEx(pContext, pGen, pEntity, pchCategoryName, bShowCompleted, bShowInProgress, comparitorFunc);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillAllPerkCategoriesListEx");
void exprFillAllPerksCategoriesListEx(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_VALID UIGen *pGen, bool bShowCompleted, bool bShowInProgress)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	MissionJournalRow*** peaMissionRowList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	static MissionCategory **s_eaCategories = NULL;
	int i;

	if (pInfo)
	{
		eaClearFast(&s_eaCategories);
		eaPushUnique(&s_eaCategories, NULL);

		if(bShowInProgress)
		{
			for (i = 0; i < eaSize(&pInfo->missions); i++)
			{
				MissionDef *pDef = mission_GetDef(pInfo->missions[i]);
				if (pDef && IS_HANDLE_ACTIVE(pDef->hCategory) && missiondef_HasDisplayName(pDef) && pDef->missionType == MissionType_Perk && pInfo->missions[i]->bDiscovered)
				{
					eaPushUnique(&s_eaCategories, GET_REF(pDef->hCategory));
				}
			}

			for (i = 0; i < eaSize(&pInfo->eaDiscoveredMissions); i++)
			{
				MissionDef *pDef = mission_GetDef(pInfo->eaDiscoveredMissions[i]);
				if (pDef && IS_HANDLE_ACTIVE(pDef->hCategory) && missiondef_HasDisplayName(pDef) && pDef->missionType == MissionType_Perk && pInfo->eaDiscoveredMissions[i]->bDiscovered)
				{
					eaPushUnique(&s_eaCategories, GET_REF(pDef->hCategory));
				}
			}
		}

		if(bShowCompleted)
		{
			for (i = 0; i < eaSize(&pInfo->completedMissions); i++)
			{
				CompletedMission *mission = pInfo->completedMissions[i];
				MissionDef *pDef = GET_REF(mission->def);
				if (pDef && IS_HANDLE_ACTIVE(pDef->hCategory) && missiondef_HasDisplayName(pDef) && pDef->missionType == MissionType_Perk)
				{
					eaPushUnique(&s_eaCategories, GET_REF(pDef->hCategory));
				}
			}
		}
	}

	eaSetSizeStruct(peaMissionRowList, parse_MissionJournalRow, eaSize(&s_eaCategories));

	for(i = eaSize(&s_eaCategories)-1; i >= 0; i--)
	{
		if (i == 0)
		{
			(*peaMissionRowList)[i]->type = MissionJournalRowType_MapHeader;
			(*peaMissionRowList)[i]->pchTranslatedCategoryName = "All";
			(*peaMissionRowList)[i]->mission = NULL;
			(*peaMissionRowList)[i]->pCompletedMission = NULL;
		}
		else
		{
			(*peaMissionRowList)[i]->type = MissionJournalRowType_MapHeader;
			(*peaMissionRowList)[i]->pchTranslatedCategoryName = TranslateDisplayMessage(s_eaCategories[i]->displayNameMsg);
			// This is a stupid place to put this, but its easiest and it's better than making a new field for it
			estrClear(&(*peaMissionRowList)[i]->estrDescription);
			estrAppend2(&(*peaMissionRowList)[i]->estrDescription, s_eaCategories[i]->name);
			(*peaMissionRowList)[i]->mission = NULL;
			(*peaMissionRowList)[i]->pCompletedMission = NULL;
		}
	}

	eaStableSort(*peaMissionRowList, NULL, missionjournalrow_SortByCategory);
	ui_GenSetManagedListSafe(pGen, peaMissionRowList, MissionJournalRow, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillAllPerkCategoriesList");
void exprFillAllPerksCategoriesList(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_VALID UIGen *pGen)
{
	exprFillAllPerksCategoriesListEx(pContext, pEntity, pGen, true, true);
}

static int missionjournal_SortLoreItemList(const ItemDef** ppItemA, const ItemDef** ppItemB)
{
	if (ppItemA && *ppItemA && ppItemB && *ppItemB){
		const char *pchDispNameA = TranslateDisplayMessage((*ppItemA)->displayNameMsg);
		const char *pchDispNameB = TranslateDisplayMessage((*ppItemB)->displayNameMsg);
		if (pchDispNameA && pchDispNameB){
			return strcmp(pchDispNameA, pchDispNameB);
		}
	}
	return 0;
}

static int missionjournal_SortLoreCategoryTree(const S32* catA, const S32* catB)
{
	if (*catA && *catB)
	{
		const char *pchDispNameA = StaticDefineGetTranslatedMessage(LoreCategoryEnum, *catA);
		const char *pchDispNameB = StaticDefineGetTranslatedMessage(LoreCategoryEnum, *catB);
		if (pchDispNameA && pchDispNameB){
			return strcmp(pchDispNameA, pchDispNameB);
		}
	}
	return 0;
}

AUTO_STRUCT;
typedef struct LoreJournalPageInfo
{
	const char* pchDisplayName;	AST(UNOWNED)
	S32 iLayoutType;			AST(NAME(LayoutType))
	bool bPageIsOwned;			AST(NAME(PageIsOwned))

}LoreJournalPageInfo;


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetLorePageButtons");
void exprGenGetLorePageButtons(SA_PARAM_NN_VALID UIGen *pGen, const char* pchSelectedCategory, SA_PARAM_NN_VALID UIGen *pProgressBarGen)
{
	LoreCategory* pCat = g_LoreCategories.ppCategories[StaticDefineIntGetInt(LoreCategoryEnum, pchSelectedCategory)];
	static LoreJournalPageInfo** s_ActivePages = NULL;
	Entity* pPlayerEnt = entActivePlayerPtr();
	bool bFoundUnownedPage = false;
	char* estrText = NULL;
	int i = 0;

	if (pCat && eaSize(&pCat->ppPages) > 0)
	{
		PlayerStatDef* pStatDef = GET_REF(pCat->hPlayerStatDef);
		S32 iStatVal = pStatDef ? playerstat_GetValue(pPlayerEnt->pPlayer->pStatsInfo, pStatDef->pchName) : 0;
		//loop through all categories, store UI-relevant info.
		for (i = 0; i < eaSize(&pCat->ppPages); i++)
		{
			ItemDef* pDef = GET_REF(pCat->ppPages[i]->hItemDef);
			if (i >= eaSize(&s_ActivePages))
			{
				eaPush(&s_ActivePages, StructCreate(parse_LoreJournalPageInfo));
			}
			s_ActivePages[i]->pchDisplayName = TranslateDisplayMessage(pDef->displayNameMsg);
			s_ActivePages[i]->iLayoutType = pDef->pJournalData->eType;
			s_ActivePages[i]->bPageIsOwned = inv_ent_AllBagsCountItems(entActivePlayerPtr(), pDef->pchName) > 0;
			if (!s_ActivePages[i]->bPageIsOwned && !bFoundUnownedPage)
			{
					if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pStatsInfo && pStatDef)
					{
						ui_GenSetVar(pProgressBarGen, "UnlockProgress", ((F32)iStatVal)/pCat->ppPages[i]->iUnlockValue, NULL);
						estrStackCreate(&estrText);
						estrPrintf(&estrText, "%i/%i %s", iStatVal, pCat->ppPages[i]->iUnlockValue, pStatDef ? TranslateDisplayMessage(pStatDef->displayNameMsg) : "");
						ui_GenSetVar(pProgressBarGen, "UnlockText", 0, estrText);
						estrDestroy(&estrText);
						bFoundUnownedPage = true;
					}
			}
		}
		if (pStatDef && !bFoundUnownedPage)
		{
			ui_GenSetVar(pProgressBarGen, "UnlockProgress", iStatVal, NULL);
			estrStackCreate(&estrText);
			estrPrintf(&estrText, "%i %s", iStatVal, pStatDef ? TranslateDisplayMessage(pStatDef->displayNameMsg) : "");
			ui_GenSetVar(pProgressBarGen, "UnlockText", 0, estrText);
			estrDestroy(&estrText);
		}
		while (i < eaSize(&s_ActivePages))
		{
			StructDestroy(parse_LoreJournalPageInfo, eaPop(&s_ActivePages));
		}
		ui_GenSetManagedListSafe(pGen, &s_ActivePages, LoreJournalPageInfo, false);
		return;
	}
	else
	{
		for (i = 0; i < eaSize(&s_ActivePages); i++)
		{
			StructDestroy(parse_LoreJournalPageInfo, s_ActivePages[i]);
		}
		eaClearFast(&s_ActivePages);
		ui_GenSetManagedListSafe(pGen, NULL, LoreJournalPageInfo, false);
	}

}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenLorePageGetEntryTitle");
const char* exprGenLorePageGetEntryTitle(const char* pchSelectedCategory, S32 iIndex)
{
	ItemDef* pDef = Lore_GetItemDefFromPage(pchSelectedCategory, iIndex);

	if (pDef)
	{
		return TranslateDisplayMessage(pDef->displayNameMsg);
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenLorePageGetEntryTextBody");
const char* exprGenLorePageGetEntryTextBody(const char* pchSelectedCategory, S32 iIndex)
{
	ItemDef* pDef = Lore_GetItemDefFromPage(pchSelectedCategory, iIndex);

	if (pDef)
	{
		return TranslateDisplayMessage(pDef->descriptionMsg);
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenLorePageGetLayoutType");
S32 exprGenLorePageGetLayoutType(const char* pchSelectedCategory, S32 iIndex)
{
	ItemDef* pDef = Lore_GetItemDefFromPage(pchSelectedCategory, iIndex);
	Entity *pEnt = entActivePlayerPtr();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	S32 iResult = -1;

	if (pDef && inv_ent_CountItems(pEnt, InvBagIDs_Lore, pDef->pchName, pExtract) > 0)
	{
		if (pDef && pDef->pJournalData)
		{
			iResult = pDef->pJournalData->eType;
		}
	}
	return iResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenLorePageGetCostume");
SA_RET_OP_VALID PlayerCostume* exprGenLorePageGetCostume(const char* pchSelectedCategory, S32 iIndex)
{
	static char* s_pchFormattedString = NULL;
	ItemDef* pDef = Lore_GetItemDefFromPage(pchSelectedCategory, iIndex);
	static int lastEntryIndex = 0;

	estrDestroy(&s_pchFormattedString);

	if (!pDef->pJournalData)
		return NULL;

	if (!s_CritterDataReceived.eaCritterData)
	{
		return NULL;
	}
	else
	{
		int i;
		StoredCritterLoreEntry* pEntry = NULL;
		PlayerCostume* pCostume = NULL;
		if (lastEntryIndex < eaSize(&s_CritterDataReceived.eaCritterData) && strcmp(s_CritterDataReceived.eaCritterData[lastEntryIndex]->pchName, pDef->pJournalData->pchCritterName) == 0)
		{
			pEntry = s_CritterDataReceived.eaCritterData[lastEntryIndex];
		}
		else
		{
			for (i = 0; i < eaSize(&s_CritterDataReceived.eaCritterData); i++)
			{
				if (strcmp(s_CritterDataReceived.eaCritterData[i]->pchName, pDef->pJournalData->pchCritterName) == 0)
				{
					pEntry = s_CritterDataReceived.eaCritterData[i];
					lastEntryIndex = i;
				}
			}
		}
		pCostume = pEntry ? GET_REF(pEntry->hCostume) : NULL;
		return pCostume;
	}
	return NULL;
}

AUTO_STRUCT;
typedef struct LoreJournalPageConceptArt
{
	const char* pchTextureName;	AST(POOL_STRING)
}LoreJournalPageConceptArt;


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenLorePageGetArtThumbnails");
void exprGGenLorePageGetArtThumbnails(SA_PARAM_NN_VALID UIGen *pGen, const char* pchSelectedCategory, int iIndex)
{
	ItemDef* pDef = Lore_GetItemDefFromPage(pchSelectedCategory, iIndex);
	static LoreJournalPageConceptArt** s_ActiveArt = NULL;
	int i = 0;

	if (pDef && pDef->pJournalData)
	{
		//loop through all categories, store UI-relevant info.
		for (i = 0; i < eaSize(&pDef->pJournalData->ppchTextures); i++)
		{
			if (i >= eaSize(&s_ActiveArt))
			{
				eaPush(&s_ActiveArt, StructCreate(parse_LoreJournalPageConceptArt));
			}
			s_ActiveArt[i]->pchTextureName = pDef->pJournalData->ppchTextures[i];
		}
		while (i < eaSize(&s_ActiveArt))
		{
			StructDestroy(parse_LoreJournalPageConceptArt, eaPop(&s_ActiveArt));
		}
		ui_GenSetManagedListSafe(pGen, &s_ActiveArt, LoreJournalPageConceptArt, false);
		return;
	}
	else
	{
		for (i = 0; i < eaSize(&s_ActiveArt); i++)
		{
			StructDestroy(parse_LoreJournalPageConceptArt, s_ActiveArt[i]);
		}
		eaClearFast(&s_ActiveArt);
		ui_GenSetManagedListSafe(pGen, NULL, LoreJournalPageConceptArt, false);
	}

}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenLorePageStatblockStringFormat");
const char* exprGenLorePageStatblockStringFormat(const char* pchSelectedCategory, S32 iIndex, const char* pchFormatString)
{
	static char* s_pchFormattedString = NULL;
	ItemDef* pDef = Lore_GetItemDefFromPage(pchSelectedCategory, iIndex);

	estrDestroy(&s_pchFormattedString);

	if (!pDef->pJournalData)
		return NULL;

	if (!s_CritterDataReceived.eaCritterData)
	{
		FormatGameString(&s_pchFormattedString, "Fetching data from server...", STRFMT_END);
	}
	else
	{
		int i;
		StoredCritterLoreEntry* pEntry = NULL;
		static int lastEntryIndex = 0;

		if (lastEntryIndex < eaSize(&s_CritterDataReceived.eaCritterData) && strcmp(s_CritterDataReceived.eaCritterData[lastEntryIndex]->pchName, pDef->pJournalData->pchCritterName) == 0)
		{
			pEntry = s_CritterDataReceived.eaCritterData[lastEntryIndex];
		}
		else
		{
			for (i = 0; i < eaSize(&s_CritterDataReceived.eaCritterData); i++)
			{
				if (strcmp(s_CritterDataReceived.eaCritterData[i]->pchName, pDef->pJournalData->pchCritterName) == 0)
				{
					pEntry = s_CritterDataReceived.eaCritterData[i];
					lastEntryIndex = i;
				}
			}
		}
		if (!pEntry)
			return NULL;

		FormatGameString(&s_pchFormattedString, pchFormatString,
			STRFMT_STRING("NAME", pEntry->estrDisplayName),
			STRFMT_STASHEDINTS(pEntry->pAttrStash, AttribTypeEnum),
			STRFMT_END);
	}
	return s_pchFormattedString;
}

AUTO_STRUCT;
typedef struct LoreJournalRowInfo
{
	const char* pchName;	AST(POOL_STRING)
	const char* pchDisplayName;	AST(UNOWNED)
	S32 iCat;
	bool bIsChild;
	bool bPlayerOwnsPages;
}LoreJournalRowInfo;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillLoreCategoryTree");
void exprFillLoreCategoryTree(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, const char* pchSelectedCategory)
{
	InventoryBagLite *pLoreBag;
	static S32* eaiLoreCategories = NULL;
	static S32* eaiLoreChildCategories = NULL;
	LoreJournalRowInfo*** peaLoreItemTreeRows = ui_GenGetManagedListSafe(pGen, LoreJournalRowInfo);
	LoreCategory* pSelectedParentCat = NULL;
	const char** eaItemNames = NULL;
	GameAccountDataExtract *pExtract;
	S32 iSelected = -1;
	int i;
	int j;

	pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
	pLoreBag = (InventoryBagLite*)inv_GetLiteBag(pEntity, InvBagIDs_Lore, pExtract);

	iSelected = StaticDefineIntGetInt(LoreCategoryEnum, pchSelectedCategory);
	pSelectedParentCat = g_LoreCategories.ppCategories[iSelected];

	//find the parent, if applicable, so that the child dropdown doesn't close when you select a child.
	if (pSelectedParentCat && pSelectedParentCat->pchParentCategoryName)
	{
		iSelected = StaticDefineIntGetInt(LoreCategoryEnum, pSelectedParentCat->pchParentCategoryName);
		pSelectedParentCat = g_LoreCategories.ppCategories[iSelected];
	}
	//loop through all categories, store UI-relevant info.
	for (i = 0; i < eaSize(&g_LoreCategories.ppCategories); i++)
	{
		LoreCategory* pCat = g_LoreCategories.ppCategories[i];
		if (pCat->pchParentCategoryName && StaticDefineIntGetInt(LoreCategoryEnum, pCat->pchParentCategoryName) == iSelected)
		{
			//store children of the active category for later insertion
			eaiPush(&eaiLoreChildCategories, i);
		}
		else if (!pCat->pchParentCategoryName)
			eaiPush(&eaiLoreCategories, i);
	}

	eaiQSort(eaiLoreCategories, missionjournal_SortLoreCategoryTree);
	eaiQSort(eaiLoreChildCategories, missionjournal_SortLoreCategoryTree);

	//iSelected is now the location of itself in the sorted array
	iSelected = eaiFind(&eaiLoreCategories, iSelected);

	//stick child categories under parent
	for (i = 0; i < eaiSize(&eaiLoreChildCategories); i++)
		eaiInsert(&eaiLoreCategories, eaiLoreChildCategories[i], ++iSelected);

	for (i = 0; i < eaiSize(&eaiLoreCategories); i++)
	{
		LoreCategory* pCat = g_LoreCategories.ppCategories[eaiLoreCategories[i]];
		int iChild = -1;
		if (i >= eaSize(peaLoreItemTreeRows))
		{
			eaPush(peaLoreItemTreeRows, StructCreate(parse_LoreJournalRowInfo));
		}
		(*peaLoreItemTreeRows)[i]->pchName = pCat->pchName;
		(*peaLoreItemTreeRows)[i]->pchDisplayName = StaticDefineGetTranslatedMessage(LoreCategoryEnum, eaiLoreCategories[i]);
		(*peaLoreItemTreeRows)[i]->iCat = eaiLoreCategories[i];
		(*peaLoreItemTreeRows)[i]->bIsChild = !!pCat->pchParentCategoryName;
		eaClear(&eaItemNames);
		for (j = 0; j < eaSize(&pCat->ppPages); j++)
		{
			eaPush(&eaItemNames, REF_HANDLE_GET_STRING(pCat->ppPages[j]->hItemDef));
		}
		(*peaLoreItemTreeRows)[i]->bPlayerOwnsPages = inv_ent_CountItemList(pEntity, InvBagIDs_Lore, &eaItemNames, pExtract);
		//show our parent if we own pages
		//since the list is sorted we can assume the first non-child node behind us is our parent.
		for (iChild = i; iChild >= 0 && (*peaLoreItemTreeRows)[iChild]->bIsChild; iChild--);//just iterates backwards
		if (iChild > -1)
			(*peaLoreItemTreeRows)[iChild]->bPlayerOwnsPages |= (*peaLoreItemTreeRows)[i]->bPlayerOwnsPages;
	}

	for (j = eaSize(peaLoreItemTreeRows)-1; j >=0; j--)
	{
		if (!(*peaLoreItemTreeRows)[j]->bPlayerOwnsPages)
			StructDestroy(parse_LoreJournalRowInfo, eaRemove(peaLoreItemTreeRows, j));
	}

	eaSetSizeStruct(peaLoreItemTreeRows, parse_LoreJournalRowInfo, i);
	eaiClearFast(&eaiLoreCategories);
	eaiClearFast(&eaiLoreChildCategories);
	if (pGen)
		ui_GenSetManagedListSafe(pGen, peaLoreItemTreeRows, LoreJournalRowInfo, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillLoreList");
void exprFillLoreList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	static ItemDef** s_LoreItemList = NULL;

	InventoryBagLite *pLoreBag;
	GameAccountDataExtract *pExtract;
	int i;

	pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
	pLoreBag = (InventoryBagLite*)inv_GetLiteBag(pEntity, InvBagIDs_Lore, pExtract);

	eaClearFast(&s_LoreItemList);

	if (pLoreBag)
	{
		for (i = 0; i < eaSize(&pLoreBag->ppIndexedLiteSlots); i++)
		{
			ItemDef *pItemDef = GET_REF(pLoreBag->ppIndexedLiteSlots[i]->hItemDef);
			if (pItemDef && pItemDef->eType == kItemType_Lore){
				eaPush(&s_LoreItemList, pItemDef);
			}
		}
	}

	eaQSort(s_LoreItemList, missionjournal_SortLoreItemList);

	// ItemDef's are owned by the entity inventory.
	ui_GenSetManagedListSafe(pGen, &s_LoreItemList, ItemDef, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillLoreListByCategory");
void exprFillLoreListByCategory(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, const char *pchCategoryName)
{
	static ItemDef** s_LoreItemList = NULL;

	GameAccountDataExtract *pExtract;
	InventoryBagLite *pLoreBag;
	int i;

	pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
	pLoreBag = (InventoryBagLite*)inv_GetLiteBag(pEntity, InvBagIDs_Lore, pExtract);

	eaClearFast(&s_LoreItemList);

	if (pLoreBag)
	{
		for (i = 0; i < eaSize(&pLoreBag->ppIndexedLiteSlots); i++)
		{
			ItemDef *pItemDef = GET_REF(pLoreBag->ppIndexedLiteSlots[i]->hItemDef);
			if (pItemDef && pItemDef->eType == kItemType_Lore){
				const char *pchItemCategory = StaticDefineIntRevLookup(LoreCategoryEnum, pItemDef->iLoreCategory);
				if (!stricmp(pchItemCategory, pchCategoryName)){
					eaPush(&s_LoreItemList, pItemDef);
				}
			}
		}
	}

	eaQSort(s_LoreItemList, missionjournal_SortLoreItemList);

	// ItemDef's are owned by the entity inventory.
	ui_GenSetManagedListSafe(pGen, &s_LoreItemList, ItemDef, false);
}

static int missionjournal_SortLoreCategoryList(const LoreCategoryRow** ppRowA, const LoreCategoryRow** ppRowB)
{
	if (ppRowA && *ppRowA && ppRowB && *ppRowB){
		int iCategoryA = StaticDefineIntGetInt(LoreCategoryEnum, (*ppRowA)->pchCategoryName);
		int iCategoryB = StaticDefineIntGetInt(LoreCategoryEnum, (*ppRowB)->pchCategoryName);
		const char *pchDispNameA = StaticDefineGetTranslatedMessage(LoreCategoryEnum, iCategoryA);
		const char *pchDispNameB = StaticDefineGetTranslatedMessage(LoreCategoryEnum, iCategoryB);
		if (pchDispNameA && pchDispNameB){
			return strcmp(pchDispNameA, pchDispNameB);
		}
	}
	return 0;
}


void fillNewOrOldLoreCategoryList(SA_PARAM_OP_VALID Entity *pEntity, bool bNew, LoreCategoryRow*** peaLoreCategoryList)
{

	GameAccountDataExtract *pExtract;
	InventoryBagLite *pLoreBag;
	int i, j;
	bool found;
	const char *pchItemCategory = NULL, *pchPrevItemCategory = NULL;

	pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
	pLoreBag = (InventoryBagLite*)inv_GetLiteBag(pEntity, InvBagIDs_Lore, pExtract);
	
	if (pLoreBag){

		for (i = 0; i < eaSize(&pLoreBag->ppIndexedLiteSlots); i++){
			ItemDef *pItemDef = GET_REF(pLoreBag->ppIndexedLiteSlots[i]->hItemDef);


			if (pItemDef && pItemDef->eType == kItemType_Lore){
				pchPrevItemCategory = pchItemCategory;
				pchItemCategory = StaticDefineIntRevLookup(LoreCategoryEnum, pItemDef->iLoreCategory);
				if(!stricmp(pchPrevItemCategory, pchItemCategory)){
					continue; //same category as last time; move on. (The earray is indexed so this is the common case).
				}

				found = false;
				for (j = 0; j < eaSize(peaLoreCategoryList); j++){
					if(!stricmp((*peaLoreCategoryList)[j]->pchCategoryName, pchItemCategory)){
						//have this category; move on. 
						found = true;
						break;
					}
				}
				if(!found){
					if(!bNew){
						//SIP TODO: add new lore feature
						LoreCategoryRow *pRow = StructCreate(parse_LoreCategoryRow);
						pRow->pchCategoryName = StructAllocString(pchItemCategory);
						eaPush(peaLoreCategoryList, pRow);
					}
				}
			}
		}

		eaQSort(*peaLoreCategoryList, missionjournal_SortLoreCategoryList);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillNewLoreCategoryList");
void exprFillNewLoreCategoryList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	//SIP TODO: make this work.  Right now it always gets an empty list, but I want feedback on 
	// hiding empty catagories before I finish this.  Maybe we don't need it.
	static LoreCategoryRow** s_LoreCategoryList = NULL;
	eaClearStruct(&s_LoreCategoryList, parse_LoreCategoryRow);
	fillNewOrOldLoreCategoryList(pEntity, true, &s_LoreCategoryList);
	// LoreCategoryRows are owned by the static list.
	ui_GenSetManagedListSafe(pGen, &s_LoreCategoryList, LoreCategoryRow, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillOldLoreCategoryList");
void exprFillOldLoreCategoryList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	static LoreCategoryRow** s_LoreCategoryList = NULL;
	eaClearStruct(&s_LoreCategoryList, parse_LoreCategoryRow);
	fillNewOrOldLoreCategoryList(pEntity, false, &s_LoreCategoryList);
	// LoreCategoryRows are owned by the static list.
	ui_GenSetManagedListSafe(pGen, &s_LoreCategoryList, LoreCategoryRow, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillLoreCategoryList");
void exprFillLoreCategoryList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static LoreCategoryRow** s_LoreCategoryList = NULL;
	static bool bRunOnce = false;
	int i;

	// This only initializes the category list once, since it doesn't refresh when it changes anyway
	if (!bRunOnce){
		const char **eaCategoryKeys = NULL;
		DefineFillAllKeysAndValues(LoreCategoryEnum, &eaCategoryKeys, NULL);
		for (i = eaSize(&eaCategoryKeys)-1; i>=0; --i){
			LoreCategoryRow *pRow = StructCreate(parse_LoreCategoryRow);
			pRow->pchCategoryName = StructAllocString(eaCategoryKeys[i]);
			eaPush(&s_LoreCategoryList, pRow);
		}
		eaDestroy(&eaCategoryKeys);
		bRunOnce = true;

		eaQSort(s_LoreCategoryList, missionjournal_SortLoreCategoryList);
	}

	// LoreCategoryRows are owned by the static list.
	ui_GenSetManagedListSafe(pGen, &s_LoreCategoryList, LoreCategoryRow, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetLoreCategoryRowDisplayName");
const char* exprGetLoreCategoryRowDisplayName(LoreCategoryRow *pRow)
{
	if (pRow && pRow->pchCategoryName){
		int iCategory = StaticDefineIntGetInt(LoreCategoryEnum, pRow->pchCategoryName);
		const char* pchDisplayName = StaticDefineGetTranslatedMessage(LoreCategoryEnum, iCategory);
		return NULL_TO_EMPTY(pchDisplayName);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetLoreItemDescription);
const char *exprGetLoreItemDescription(ExprContext* pContext, SA_PARAM_OP_VALID ItemDef *pItemDef)
{
	Entity *pEnt = entActivePlayerPtr();
	static char *estrBuffer = NULL;
	estrClear(&estrBuffer);
	if (pItemDef){
		Message *pMessage = GET_REF(pItemDef->descriptionMsg.hMessage);
		if (pMessage){
			FormatGameMessage(&estrBuffer, pMessage, STRFMT_ENTITY(pEnt), STRFMT_END);
			return NULL_TO_EMPTY(estrBuffer);
		}
	}
	return "";
}

typedef int (__cdecl *StableSortComparator)(const void* a, const void* b, const void* unused);
void exprMissionJournalListGroupByCriteria(ExprContext *pContext,
										   SA_PARAM_OP_VALID Entity *pEntity,
										   SA_PARAM_NN_VALID UIGen *pGen,
										   StableSortComparator comp,
										   bool bAddSucceededCategory)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	ParseTable *pType;
	MissionJournalRow ***peaList = (MissionJournalRow ***)ui_GenGetList(pGen, NULL, &pType);
	const char *prevCategory = NULL;
	int i;
	int numHeaders = 0;

	if (pInfo && pType == parse_MissionJournalRow)
	{
		// Sort mission list by map.  Then iterate through mission list and add headers where needed.
		eaStableSort((*peaList), NULL, comp);

		for (i = 0; i < eaSize(peaList); i++)
		{
			const char *pchTranslatedCategoryName = NULL;
			MissionDef *missionDef = mission_GetDef((*peaList)[i]->mission);
			if (!missionDef && (*peaList)[i]->pCompletedMission)
				missionDef = GET_REF((*peaList)[i]->pCompletedMission->def);

			if (bAddSucceededCategory &&
				(*peaList)[i]->mission &&
				(*peaList)[i]->mission->state == MissionState_Succeeded)
			{
				pchTranslatedCategoryName = entTranslateMessageKey(pEntity, "MissionJournal_SucceededCategory");
			}
			else if (missionDef)
			{
				pchTranslatedCategoryName = missiondef_GetTranslatedCategoryName(missionDef);
			}

			if (pchTranslatedCategoryName && (!prevCategory || stricmp(prevCategory, pchTranslatedCategoryName)))
			{
				MissionJournalRow *pNewHeader = StructCreate(parse_MissionJournalRow);
				pNewHeader->type = MissionJournalRowType_MapHeader;
				pNewHeader->pchTranslatedCategoryName = pchTranslatedCategoryName;
				eaInsert(peaList, pNewHeader, i++);
				numHeaders++;

				prevCategory = pchTranslatedCategoryName;
			}
			else if (missionDef && !pchTranslatedCategoryName && (prevCategory || !numHeaders))
			{
				MissionJournalRow *pNewHeader = StructCreate(parse_MissionJournalRow);
				pNewHeader->type = MissionJournalRowType_DefaultMapHeader;
				pNewHeader->mission = NULL;
				pNewHeader->pCompletedMission = NULL;
				pNewHeader->pchTranslatedCategoryName = NULL;
				eaInsert(peaList, pNewHeader, i++);
				numHeaders++;

				prevCategory = NULL;
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListGroupByCategory");
void exprMissionJournalListGroupByCategory(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_VALID UIGen *pGen)
{
	exprMissionJournalListGroupByCriteria(pContext, pEntity, pGen, missionjournalrow_SortByCategory, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListGroupByCategoryWithSucceededCategory");
void MissionJournalListGroupByCategoryWithSucceededCategory(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_VALID UIGen *pGen)
{
	exprMissionJournalListGroupByCriteria(pContext, pEntity, pGen, missionjournalrow_SortByCategoryWithSucceededCategory, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListGroupByReturnMap");
void exprMissionJournalListGroupByReturnMap(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_VALID UIGen *pGen)
{
	exprMissionJournalListGroupByCriteria(pContext, pEntity, pGen, missionjournalrow_SortByReturnMap, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListGroupByUIType");
void exprMissionJournalListGroupByUIType(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_VALID UIGen *pGen)
{
	exprMissionJournalListGroupByCriteria(pContext, pEntity, pGen, missionjournalrow_SortByUIType, false);
}

// This adds a category at the top of the Mission Journal list for missions that are relevant for the current map.
// This is almost like a "start page" for the Mission Journal.  It shows any InProgress missions that must be completed
// on the current map, or any Completed mission that must be turned in to the current map.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListAddCurrentMapCategory");
void exprMissionJournalListAddCurrentMapCategory(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_VALID UIGen *pGen)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	ParseTable *pTable = NULL;
	MissionJournalRow ***peaList = (MissionJournalRow ***) ui_GenGetList(pGen, NULL, &pTable);
	static MissionJournalRow **peaCurrentMapRows = NULL;
	MissionJournalRow *pCurrentMapHeader;
	const char *currentMap = zmapInfoGetPublicName(NULL);
	int i, n;
	int numValidRows = 0;

	if (pInfo && currentMap && pTable == parse_MissionJournalRow)
	{
		// Use the list of missions from the existing list model to ensure that all filters, etc. are obeyed
		n = eaSize(peaList);
		for (i = n - 1; i >= 0; i--)
		{
			Mission *mission = (*peaList)[i]->mission;
			MissionDef *def = mission_GetDef(mission);
			if (mission && def)
			{
				bool found = false;

				if (mission->state == MissionState_Succeeded && def->pchReturnMap && !stricmp(def->pchReturnMap, currentMap))
					found = true;
				else if ( (mission->state == MissionState_InProgress) || (mission->state == MissionState_Succeeded && !def->pchReturnMap) ) {
					found = exprMissionOrSubMissionHasObjectiveOnCurrentMap(pContext, mission);
				}

				if (found)
				{
					// Probably just created a crash here. The whole journal functionality
					// needs to be rewritten from scratch. It's design is such that it is
					// impossible to not introduce subtle crashes in the Gen code. -JM
					//PopulateRowFromMission(eaGetStruct(&peaCurrentMapRows, parse_MissionJournalRow, numValidRows++), mission, pEntity);
					eaSet(&peaCurrentMapRows, eaRemove(peaList, i), numValidRows++);
				}
			}
			else if ((*peaList)[i]->type == MissionJournalRowType_MapHeader
				|| (*peaList)[i]->type == MissionJournalRowType_DefaultMapHeader
				|| (*peaList)[i]->type == MissionJournalRowType_CurrentMapHeader)
			{
				if (i + 1 < n && ((*peaList)[i + 1]->type == MissionJournalRowType_MapHeader
					|| (*peaList)[i + 1]->type == MissionJournalRowType_DefaultMapHeader
					|| (*peaList)[i + 1]->type == MissionJournalRowType_CurrentMapHeader))
				{
					StructDestroy(parse_MissionJournalRow, eaRemove(peaList, i));
				}
			}
		}

		// Destroy extra rows
		eaSetSizeStruct(&peaCurrentMapRows, parse_MissionJournalRow, numValidRows);

		// Sort the rows
		eaStableSort(peaCurrentMapRows, NULL, missionjournalrow_SortByLevel);
		eaStableSort(peaCurrentMapRows, NULL, missionjournalrow_SortByState);

		// Insert new rows at top of Mission Journal list
		eaInsertEArray(peaList, &peaCurrentMapRows, 0);
		if (eaSize(&peaCurrentMapRows))
		{
			pCurrentMapHeader = StructCreate(parse_MissionJournalRow);
			pCurrentMapHeader->type = MissionJournalRowType_CurrentMapHeader;
			eaInsert(peaList, pCurrentMapHeader, 0);
		}

		eaClear(&peaCurrentMapRows);
	}
}

// This sorts up the current map section to the top and converts the header type to
// MissionJournalRowType_CurrentMapHeader.
// DEPRECATED - This function doesn't even work in the first place. It is comparing Mission Category string to current map string, which are never equal.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListRaiseCurrentMap");
void exprMissionJournalListRaiseCurrentMap(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	return;
#if 0
	MissionJournalRow ***peaList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	const char *currentMap;
	MissionJournalRow *pRow = NULL;
	MissionJournalRowType rowType;
	int insert = 0, i;
	bool copyMissions = false;

	const char* pchMapName = TranslateMessagePtr(zmapInfoGetDisplayNameMessagePtr(NULL));
	currentMap = NULL_TO_EMPTY(pchMapName);

	if (peaList)
	{
		for (i = 0; i < eaSize(peaList); i++)
		{
			pRow = (*peaList)[i];
			rowType = pRow->type;

			if (rowType == MissionJournalRowType_MapHeader || rowType == MissionJournalRowType_DefaultMapHeader)
			{
				if (copyMissions)
				{
					copyMissions = false;
				}
				// Look for the current map row header
				if ((*peaList)[i]->pchTranslatedCategoryName && !stricmp(currentMap, (*peaList)[i]->pchTranslatedCategoryName))
				{
					pRow = eaRemove(peaList, i);
					eaInsert(peaList, pRow, insert++);
					copyMissions = true;
				}
			}
			else if (rowType == MissionJournalRowType_Mission || rowType == MissionJournalRowType_CompletedMission)
			{
				if (copyMissions)
				{
					pRow = eaRemove(peaList, i);
					eaInsert(peaList, pRow, insert++);
				}
			}
		}
	}
	ui_GenSetListSafe(pGen, peaList, MissionJournalRow);
#endif
}

// This sorts up the primary mission section to the top
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListRaisePrimary");
void exprMissionJournalListRaisePrimary(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	MissionJournalRow ***peaList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	MissionJournalRow *pRow = NULL;
	MissionJournalRowType categoryType = MissionJournalRowType_None;
	const char *pchTranslatedCategoryName = NULL;
	MissionJournalRowType rowType;
	int listsize, i, j;

	if (peaList)
	{
		listsize = eaSize(peaList);
		for (i = 0; i < listsize; i++)
		{
			pRow = (*peaList)[i];
			rowType = pRow->type;

			if (rowType == MissionJournalRowType_Mission || rowType == MissionJournalRowType_CompletedMission)
			{
				if(gclEntIsPrimaryMission(pRow->pEnt, pRow->mission->missionNameOrig))
				{
					if(i != 1)
					{
						for (j = i - 1; j >= 0; j--)
						{
							if ((*peaList)[j]->type == MissionJournalRowType_MapHeader || (*peaList)[j]->type == MissionJournalRowType_DefaultMapHeader)
							{
								pchTranslatedCategoryName = (*peaList)[j]->pchTranslatedCategoryName;
								categoryType = (*peaList)[j]->type;
								break;
							}
						}

						pRow = eaRemove(peaList, i);
						listsize = eaSize(peaList);

						// If the original category is now empty, remove and destroy original header. Remember, we removed a row already. 
						if((i >= listsize || (*peaList)[i]->type == MissionJournalRowType_MapHeader || (*peaList)[i]->type == MissionJournalRowType_DefaultMapHeader)
							&& (*peaList)[i - 1]->type == MissionJournalRowType_MapHeader || (*peaList)[i - 1]->type == MissionJournalRowType_DefaultMapHeader)
						{
							StructDestroy(parse_MissionJournalRow, eaRemove(peaList, i - 1));
						}

						// if category is the same, insert after the header
						if((*peaList)[0]->pchTranslatedCategoryName == pchTranslatedCategoryName)
						{
							eaInsert(peaList, pRow, 1);
						}
						else // otherwise insert at front and insert a new category header
						{
							eaInsert(peaList, pRow, 0);

							pRow = StructCreate(parse_MissionJournalRow);
							pRow->type = categoryType;
							pRow->pchTranslatedCategoryName = pchTranslatedCategoryName;
							eaInsert(peaList, pRow, 0);
						}
					}

					break;
				}
			}
		}
	}
	ui_GenSetListSafe(pGen, peaList, MissionJournalRow);
}

// This sorts up the primary mission section to the top
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListSetMissionNumbers");
void exprMissionJournalListSetMissionNumbers(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	MissionJournalRow ***peaList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	MissionJournalRow *pRow = NULL;
	int number = 0;
	int listsize, i;

	if (peaList)
	{
		listsize = eaSize(peaList);
		for (i = 0; i < listsize; i++)
		{
			pRow = (*peaList)[i];

			if (pRow->type == MissionJournalRowType_Mission || pRow->type == MissionJournalRowType_CompletedMission)
				pRow->iMissionNumber = number++;
		}
	}
	ui_GenSetListSafe(pGen, peaList, MissionJournalRow);
}

// Filters a mission list to get only a specific type ("Normal", "Perk", etc.)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListFilterByMissionType");
void exprMissionJournalListFilterByMissionType(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *type)
{
	MissionJournalRow ***peaList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	MissionType missionType = StaticDefineIntGetInt(MissionTypeEnum, type);

	int i, n = eaSize(peaList);
	for (i = n-1; i >= 0; --i)
	{
		MissionDef *def = NULL;
		if ((*peaList)[i]->type == MissionJournalRowType_Mission)
		{
			Mission *mission = (*peaList)[i]->mission;
			def = mission_GetDef(mission);
		}
		else if ((*peaList)[i]->type == MissionJournalRowType_CompletedMission)
		{
			CompletedMission *mission = (*peaList)[i]->pCompletedMission;
			def = GET_REF(mission->def);
		}

		if (def && !mission_MissionTypesSortTogether(missionType, def->missionType))
			StructDestroy(parse_MissionJournalRow, eaRemove(peaList, i));
	}
	ui_GenSetListSafe(pGen, peaList, MissionJournalRow);
}

// Filters a mission list to get only NOT a specific type ("Normal", "Perk", etc.)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListFilterByNotMissionType");
void exprMissionJournalListFilterByNotMissionType(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *type)
{
	MissionJournalRow ***peaList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	MissionType missionType = StaticDefineIntGetInt(MissionTypeEnum, type);

	int i, n = eaSize(peaList);
	for (i = n-1; i >= 0; --i)
	{
		MissionDef *def = NULL;
		if ((*peaList)[i]->type == MissionJournalRowType_Mission)
		{
			Mission *mission = (*peaList)[i]->mission;
			def = mission_GetDef(mission);
		}
		else if ((*peaList)[i]->type == MissionJournalRowType_CompletedMission)
		{
			CompletedMission *mission = (*peaList)[i]->pCompletedMission;
			def = GET_REF(mission->def);
		}

		if (def && (missionType == def->missionType))
			StructDestroy(parse_MissionJournalRow, eaRemove(peaList, i));
	}
	ui_GenSetListSafe(pGen, peaList, MissionJournalRow);
}

// Checks whether this MissionJournal list row matches the given type
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListRowMatchesType");
bool exprMissionJournalListRowMatchesType(ExprContext *pContext, SA_PARAM_OP_VALID MissionJournalRow *pListRow, const char *type)
{
	if (pListRow)
	{
		MissionJournalRowType rowType = StaticDefineIntGetInt(MissionJournalRowTypeEnum, type);
		return (rowType == pListRow->type);
	}
	return false;
}

// Sorts the entire mission list by the level of the mission
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListSortByLevel");
void exprMissionJournalListSortByLevel(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEntity, SA_PARAM_NN_VALID UIGen *pGen)
{
	ParseTable *pType;
	MissionJournalRow ***peaList = (MissionJournalRow ***)ui_GenGetList(pGen, NULL, &pType);
	if (pType == parse_MissionJournalRow)
		eaStableSort((*peaList), NULL, missionjournalrow_SortByLevel);
}

// Sorts by the mission state - InProgress, Completed, Failed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListSortByState");
void exprMissionJournalListSortByState(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	ParseTable *pType;
	MissionJournalRow ***peaList = (MissionJournalRow ***)ui_GenGetList(pGen, NULL, &pType);
	if (pType == parse_MissionJournalRow)
		eaStableSort((*peaList), NULL, missionjournalrow_SortByState);
}

// Sorts the entire mission list by the sort priority defined on the MissionDef
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListSortByPriority");
void exprMissionJournalListSortByPriority(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEntity, SA_PARAM_NN_VALID UIGen *pGen)
{
	ParseTable *pType;
	MissionJournalRow ***peaList = (MissionJournalRow ***)ui_GenGetList(pGen, NULL, &pType);
	if (pType == parse_MissionJournalRow)
		eaStableSort((*peaList), NULL, missionjournalrow_SortByPriority);
}

// This is a little icky, but it works without allocating extra memory.
// Each mission only has 1-3 subobjectives so O(n^2) probably doesn't matter.
static void mission_GetObjectiveList(Mission* mission, Mission*** objectiveList, bool withCollapse)
{
	int i, n;

	if (mission && (mission_HasUIString(mission) || mission->depth == 0))
	{
		if (mission_HasUIString(mission) && !(mission->depth == 0 && mission->openChildren == 0 && mission_GetStateForUI(mission) == MissionState_Succeeded && withCollapse))
			eaPush(objectiveList, mission);
		if (eaSize(&mission->children) && (mission_GetStateForUI(mission) == MissionState_InProgress || mission->openChildren > 0 || !withCollapse))
		{
			int iDispOrder;
			int iNextDispOrder = mission->children[0]->displayOrder;

			// Find minimum displayOrder
			n = eaSize(&mission->children);
			for (i = 0; i < n; i++)
				if (mission->children[i]->displayOrder < iNextDispOrder)
					iNextDispOrder = mission->children[i]->displayOrder;

			// Iterate through submissions for each displayOrder value to call recursively in correct order.
			do{
				iDispOrder = iNextDispOrder;
				for (i = 0; i < n; i++)
				{
					if (mission->children[i]->displayOrder == iDispOrder)
						mission_GetObjectiveList(mission->children[i], objectiveList, withCollapse);
					if (mission->children[i]->displayOrder > iDispOrder && (iNextDispOrder == iDispOrder || mission->children[i]->displayOrder < iNextDispOrder ))
						iNextDispOrder = mission->children[i]->displayOrder;
				}
			}while(iNextDispOrder > iDispOrder);
		}
	}
}

S32 s_iMaxMissionHelperLines = MAX_MISSION_HELPER_LINES;

// Fill the given gen's list with all missions, objectives, and subobjectives.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("SetMaxMissionHelperLines");
void exprSetMaxMissionHelperLines(ExprContext *pContext, S32 iMaxMissionHelperLines)
{
	s_iMaxMissionHelperLines = iMaxMissionHelperLines;
}

static MissionListNode **s_ExtraNodes = NULL;

static MissionListNode* MissionListNodeCreate(void)
{
	if (eaSize(&s_ExtraNodes))
		return eaPop(&s_ExtraNodes);
	return StructCreate(parse_MissionListNode);
}

static void MissionListNodeFree(MissionListNode *pNode)
{
	ZeroStruct(pNode);
	eaPush(&s_ExtraNodes, pNode);
}

static MissionListNode *GetOrCreateMissionListNode(MissionListNode ***peaNodes, S32 iNode)
{
	assert(iNode >= 0);
	while (eaSize(peaNodes) <= iNode)
	{
		eaPush(peaNodes, MissionListNodeCreate());
	}
	assertmsg(*peaNodes, "Failed to make node");
	return (*peaNodes)[iNode];
}

static void MissionListNodeListSetLength(MissionListNode ***peaNodes, S32 iLength)
{
	while (eaSize(peaNodes) > iLength)
		MissionListNodeFree(eaPop(peaNodes));
}

static bool missionui_DisplaySubmissionCB(const Mission *mission)
{
	if (!mission_HasUIString(mission))
		return false;
	return true;
}

static bool missionui_MissionShouldExpand(const Mission *mission)
{
	if (mission_GetStateForUI(mission) == MissionState_InProgress ||
		mission->openChildren > 0 ||
		(mission_GetStateForUI(mission) == MissionState_InProgress && eaSize(&mission->childFullMissions)))
		return true;
	return false;
}

static bool mission_IsForCurrentMap(Mission *mission)
{
	MissionState state = mission_GetStateForUI(mission);
	MissionDef *pDef = mission_GetDef(mission);
	bool bIsForCurrentMap = false;
	const char *pchCurrentMap = zmapInfoGetPublicName(NULL);
	Entity* pEnt = entActivePlayerPtr();

	if (pDef)
	{
		switch (state)
		{
			xcase MissionState_InProgress:
				bIsForCurrentMap = (pDef->eaObjectiveMaps == NULL || !eaSize(&pDef->eaObjectiveMaps) || (pEnt && missiondef_gclHasObjectiveOnCurrentMap(pEnt, pDef)) );
			xcase MissionState_Succeeded:
				if (pDef->pchReturnMap){
					bIsForCurrentMap = !stricmp(pDef->pchReturnMap, pchCurrentMap);
				}else {
					bIsForCurrentMap = (pDef->eaObjectiveMaps == NULL || !eaSize(&pDef->eaObjectiveMaps) || (pEnt && missiondef_gclHasObjectiveOnCurrentMap(pEnt,pDef)) );
				}
			xcase MissionState_Failed:
				// TODO - no way of knowing this yet
				bIsForCurrentMap = true;
		}
	}

	return bIsForCurrentMap;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMapNameFromMission");
const char* exprGetMapNameFromMission(SA_PARAM_NN_VALID Mission *pMission)
{
	MissionDef *pMissionDef = mission_GetDef(pMission);
	return mission_GetMapDisplayNameFromMissionDef(pMissionDef);
}


// Finds all children missions which have a display string.  If a submission does not have a display string,
// this function will recursively add its children
static int mission_getSubmissionsForDisplayRecursive(Entity *pEnt, Mission* mission, Mission*** peaMissionList, bool bAddCompletedMissions)
{
	int i, n, numAdded = 0;
	if(mission)
	{
		if (gConf.bAddRelatedOpenMissionsToPersonalMissionHelper && pEnt)
		{
			// Get the mission def for this mission
			MissionDef *pMissionDef = mission_GetDef(mission);
			if (pMissionDef)
			{
				MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pEnt);
				OpenMission *pOpenMission = pMissionInfo ? mapState_OpenMissionFromName(mapStateClient_Get(),pMissionInfo->pchCurrentOpenMission) : NULL;
				const char *pcRelatedMissionDef = pMissionDef->pcRelatedMission;
				Mission *pRelatedMission = NULL;

				if (pOpenMission && pcRelatedMissionDef && stricmp(pOpenMission->pMission->missionNameOrig, pcRelatedMissionDef) == 0)
				{
					pRelatedMission = pOpenMission->pMission;
				}

				if (pRelatedMission)
				{
					// The mission has a related mission, add it to the list as well
					numAdded += mission_getSubmissionsForDisplayRecursive(pEnt, pRelatedMission, peaMissionList, bAddCompletedMissions);
				}
			}
		}


		n = eaSize(&mission->children);
		for (i = 0; i < n; i++){
			Mission *subMission = mission->children[i];
			if (subMission) {
				if(bAddCompletedMissions || (mission_GetStateForUI(subMission) != MissionState_Succeeded)) {
					if(!missionui_DisplaySubmissionCB(subMission)) {
						numAdded+= mission_getSubmissionsForDisplayRecursive(pEnt, subMission, peaMissionList, bAddCompletedMissions);
					} else {
						eaPush(peaMissionList, subMission);
						++numAdded;
					}
				}
			}
		}
	}
	return numAdded;
}

Mission * mission_FindRelatedMissionByOpenMission(Mission *pMission, OpenMission *pOpenMission)
{
	if (pMission && pOpenMission)
	{
		MissionDef *pMissionDef = mission_GetDef(pMission);


		const char *pcRelatedMission = SAFE_MEMBER(pMissionDef, pcRelatedMission);

		if (pcRelatedMission && stricmp(pcRelatedMission, pOpenMission->pMission->missionNameOrig) == 0)
			return pMission;

		// See if the children has the
		FOR_EACH_IN_EARRAY_FORWARDS(pMission->children, Mission, pChildMission)
		{
			Mission *pMissionFound = mission_FindRelatedMissionByOpenMission(pChildMission, pOpenMission);
			if (pMissionFound)
				return pMissionFound;
		}
		FOR_EACH_END

		return NULL;
	}
	return NULL;
}

S32 mission_GetDepthAdjustmentForOpenMissionNode(Mission *pRootPersonalMission, OpenMission *pOpenMission)
{
	if (pRootPersonalMission && pOpenMission)
	{
		// Find the personal mission that is related to the open mission
		Mission *pRelatedMission = mission_FindRelatedMissionByOpenMission(pRootPersonalMission, pOpenMission);

		if (pRelatedMission)
		{
			// We found the mission related to the open mission.
			return pRelatedMission->depth;
		}
		return 0;
	}
	return 0;
}

void mission_AdjustDepthForOpenMissionNode(Entity *pEnt, Mission *pRootMission, MissionListNode *pMissionListNode, Mission *pCurrentMission)
{
	if (gConf.bAddRelatedOpenMissionsToPersonalMissionHelper && pEnt && pRootMission && pMissionListNode && pCurrentMission)
	{
		// If this mission is an open mission or submission and the root mission is a personal mission,
		// we can assume that the open mission is related to some personal mission.
		// We need to find that mission and adjust the indentation accordingly.
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pEnt);
		Mission *pMissionFromPlayer = mission_GetMissionByName(pMissionInfo, pRootMission->missionNameOrig);
		if (pMissionFromPlayer)
		{
			// At this point we know that the root mission is a personal mission
			MissionDef *pDepthMissionRootDef = GET_REF(pCurrentMission->rootDefOrig);

			OpenMission *pOpenMission = pDepthMissionRootDef ? mapState_OpenMissionFromName(mapStateClient_Get(),pDepthMissionRootDef->name) : NULL;

			if (pOpenMission)
			{
				// This mission is a part of an open mission. Adjust the depth
				pMissionListNode->iIndent += mission_GetDepthAdjustmentForOpenMissionNode(pRootMission, pOpenMission);
			}
		}
	}
}

// Returns the number of items added
int mission_MakeSubmissionNodes(S32 iMissionNumber, Entity *pEnt, Mission *pRootMission, Mission* mission, MissionListNode*** peaMissionListNodes, int index, int iExtraIndent, bool bShadowMission, bool bAddCompletedMissions)
{
	int i;
	Mission **missionList = NULL;
	int numAdded = 0;
	int totalAdded = 0;
	MissionDef *pDef = NULL;

	// Insert all children into the array
	numAdded = mission_getSubmissionsForDisplayRecursive(pEnt, mission, &missionList, bAddCompletedMissions);

	// Sort the children that were added
	if (eaSize(&missionList)) {
		qsort(missionList, numAdded, sizeof(Mission*), mission_SortByDisplayOrder);

		// Insert nodes for sub-missions
		for (i = 0; i < numAdded; i++) {
			MissionListNode *node = GetOrCreateMissionListNode(peaMissionListNodes,index);
			Mission *pDepthMission = missionList[i];

			PopulateNodeFromMission(node, pDepthMission, pEnt, MissionListNodeType_UIString, missionList[i]->depth + iExtraIndent, bShadowMission, iMissionNumber);
			
			mission_AdjustDepthForOpenMissionNode(pEnt, pRootMission, node, pDepthMission);

			totalAdded++;
			index++;

			if (!mission_IsForCurrentMap(missionList[i]) && (mission_GetStateForUI(missionList[i]) == MissionState_InProgress) && EMPTY_TO_NULL(exprGetMapNameFromMission(missionList[i]))){
				// Create a node for the Map string, if needed
				node = GetOrCreateMissionListNode(peaMissionListNodes, index);

				PopulateNodeFromMission(node, missionList[i], pEnt, MissionListNodeType_MapString, missionList[i]->depth + iExtraIndent + 1, bShadowMission, iMissionNumber);

				mission_AdjustDepthForOpenMissionNode(pEnt, pRootMission, node, pDepthMission);

				totalAdded++;
				index++;
			} else {
				// Recurse to sub-missions
				if (missionui_MissionShouldExpand(missionList[i])) {
					int added = mission_MakeSubmissionNodes(iMissionNumber, pEnt, pRootMission, missionList[i], peaMissionListNodes, index, iExtraIndent, bShadowMission, bAddCompletedMissions);
					index += added;
					totalAdded += added;
				}
			}
		}
	}

	// Recurse to full child missions
	for (i = 0; i < eaSize(&mission->childFullMissions); i++) {
		Mission *childMission = mission_GetMissionByName(pEnt->pPlayer->missionInfo, mission->childFullMissions[i]);
		if (childMission && childMission->bHiddenFullChild) {
			int added;

			if(bAddCompletedMissions || (mission_GetStateForUI(childMission) != MissionState_Succeeded) ) {
				if(mission_HasUIString(childMission))
				{
					MissionListNode *node = GetOrCreateMissionListNode(peaMissionListNodes,index);

					PopulateNodeFromMission(node, childMission, pEnt, MissionListNodeType_UIString, mission->depth + iExtraIndent, bShadowMission, iMissionNumber);

					totalAdded++;
					index++;
				}

				if (missionui_MissionShouldExpand(childMission)) {
					added = mission_MakeSubmissionNodes(iMissionNumber, pEnt, pRootMission, childMission, peaMissionListNodes, index, iExtraIndent+1, bShadowMission, bAddCompletedMissions);
					index += added;
					totalAdded += added;
				}
			}
		} else if (!childMission) {
			CompletedMission *childCompletedMission = mission_GetCompletedMissionByName(pEnt->pPlayer->missionInfo, mission->childFullMissions[i]);
			if (childCompletedMission && childCompletedMission->bHidden && bAddCompletedMissions) {
				MissionListNode *node = GetOrCreateMissionListNode(peaMissionListNodes,index);

				PopulateNodeFromCompletedMission(node, childCompletedMission, pEnt, MissionListNodeType_CompletedUIString, mission->depth + iExtraIndent, bShadowMission, iMissionNumber);

				totalAdded++;
				index++;
			}
		}
	}

	eaDestroy(&missionList);

	return totalAdded;
}

int mission_MakeFinalNodeForRootMission(S32 iMissionNumber, Entity *pEnt, Mission *pRootMission, int numValidNodes, MissionListNode ***peaMissionListNodes, bool bShadowMission, bool bIsForCurrentMap)
{
	MissionListNode *node = NULL;
	int iNumAdded = 0;
	MissionDef *pDef = NULL;

	// Create a node for the Return String, if needed
	if (mission_GetStateForUI(pRootMission) == MissionState_Succeeded && mission_HasReturnString(pRootMission)){
		node = GetOrCreateMissionListNode(peaMissionListNodes, numValidNodes++);

		PopulateNodeFromMission(node, pRootMission, pEnt, MissionListNodeType_ReturnString, /*iIndent=*/1, bShadowMission, iMissionNumber);
		
		++iNumAdded;
	}
	// Create a node for the Failed Return String, if needed
	else if (mission_GetStateForUI(pRootMission) == MissionState_Failed && mission_HasFailedReturnString(pRootMission)){
		node = GetOrCreateMissionListNode(peaMissionListNodes, numValidNodes++);

		PopulateNodeFromMission(node, pRootMission, pEnt, MissionListNodeType_FailedReturnString, /*iIndent=*/1, bShadowMission, iMissionNumber);
		
		++iNumAdded;
	}
	// Create a node for the Map string, if needed
	else if (!bIsForCurrentMap && EMPTY_TO_NULL(exprGetMapNameFromMission(pRootMission))){
		node = GetOrCreateMissionListNode(peaMissionListNodes, numValidNodes++);

		PopulateNodeFromMission(node, pRootMission, pEnt, MissionListNodeType_MapString, /*iIndent=*/1, bShadowMission, iMissionNumber);
		
		++iNumAdded;
	}
	return iNumAdded;
}

int mission_MakeMissionNodes(S32 iMissionNumber, Mission *pRootMission, Entity *pEntity, int numValidNodes, MissionListNode ***peaMissionListNodes, bool bShadowMission, bool bAddCompletedMissions, bool bSeparator)
{
	MissionListNode *node = NULL;
	static Mission** s_eaSubMissions = NULL;
	U32 iExtraIndent = 0;
	bool bIsForCurrentMap = mission_IsForCurrentMap(pRootMission);
	MissionDef *pDef = NULL;

	// Create a node for the Display Name
	node = GetOrCreateMissionListNode(peaMissionListNodes, numValidNodes++);

	PopulateNodeFromMission(node, pRootMission, pEntity, MissionListNodeType_DisplayName, /*iIndent=*/0, bShadowMission, iMissionNumber);

	// Create the list of Objectives (UI strings)
	if (missionui_MissionShouldExpand(pRootMission) && bIsForCurrentMap)
	{
		// If the root mission has a UI string, create a node for that
		if (mission_HasUIString(pRootMission) && !pRootMission->openChildren){
			node = GetOrCreateMissionListNode(peaMissionListNodes, numValidNodes++);

			PopulateNodeFromMission(node, pRootMission, pEntity, MissionListNodeType_UIString, /*iIndent=*/1, bShadowMission, iMissionNumber);
			
			iExtraIndent = 1;
		}

		// Create a node for the UI string of each sub-mission
		numValidNodes += mission_MakeSubmissionNodes(iMissionNumber, pEntity, pRootMission, pRootMission, peaMissionListNodes, numValidNodes, iExtraIndent, bShadowMission, bAddCompletedMissions);
	}

	// This creates a Return String or Map String depending on the state of the mission
	numValidNodes += mission_MakeFinalNodeForRootMission(iMissionNumber, pEntity, pRootMission, numValidNodes, peaMissionListNodes, bShadowMission, bIsForCurrentMap);

	// If there needs to be a separator, add a node for that
	if (bSeparator)
	{
		if(gclEntIsPrimaryMission(pEntity, pRootMission->missionNameOrig))
		{
			node = GetOrCreateMissionListNode(peaMissionListNodes, numValidNodes++);

			PopulateNodeFromMission(node, pRootMission, pEntity, MissionListNodeType_Separator, /*iIndent=*/1, /*bShadowMission=*/0, iMissionNumber);
		}
	}

	// If the list has exceeded the maximum number of lines, remove them until you
	// reach a root level mission
	if (numValidNodes > s_iMaxMissionHelperLines)
	{
		while (node = eaGet(peaMissionListNodes, numValidNodes-1)){
			--numValidNodes;
			if (node->iIndent == 0) break;
		}
	}

	return numValidNodes;
}


// Fill the given gen's list with all objectives and subobjectives.
// Does not make an entry for the root mission's name.
// There are a lot of special cases and little gotchas here.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillSingleMissionList");
void exprFillSingleMissionList(ExprContext *pContext, SA_PARAM_OP_VALID Mission *pRootMission)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	MissionListNode ***peaMissionListNodes = ui_GenGetManagedListSafe(pGen, MissionListNode);
	S32 numValidNodes=0;
	MissionListNode *node = NULL;
	static Mission** s_eaSubMissions = NULL;
	int iExtraIndent = -1;
	bool bIsForCurrentMap = mission_IsForCurrentMap(pRootMission);
	bool bShadowMission = false;
	MissionDef *pDef = NULL;

	// Create the list of Objectives (UI strings)
	if (missionui_MissionShouldExpand(pRootMission) && bIsForCurrentMap)
	{
		// If the root mission has a UI string, create a node for that
		if (mission_HasUIString(pRootMission) && !pRootMission->openChildren){
			node = GetOrCreateMissionListNode(peaMissionListNodes, numValidNodes++);

			PopulateNodeFromMission(node, pRootMission, /*pEntity=*/NULL, MissionListNodeType_UIString, /*iIndent=*/1, /*bShadowMission=*/0, /*iMissionNumber=*/0);
			
			iExtraIndent = 0;
		}

		// Create a node for the UI string of each sub-mission
		numValidNodes += mission_MakeSubmissionNodes(0, NULL, pRootMission, pRootMission, peaMissionListNodes, numValidNodes, iExtraIndent, bShadowMission, true);
	}

	numValidNodes += mission_MakeFinalNodeForRootMission(0, NULL, pRootMission, numValidNodes, peaMissionListNodes, bShadowMission, bIsForCurrentMap);

	// Delete extra MissionListNodes
	MissionListNodeListSetLength(peaMissionListNodes, numValidNodes);
	ui_GenSetManagedListSafe(pGen, peaMissionListNodes, MissionListNode, true);
}

// Fill the given gen's list with all objectives and subobjectives.
// Does not make an entry for the root mission's name.
// There are a lot of special cases and little gotchas here.
// Optionally adds completed missions to the list.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillSingleMissionList_AddCompleted");
void exprFillSingleMissionList_AddCompleted(ExprContext *pContext, SA_PARAM_OP_VALID Mission *pRootMission, bool bAddCompletedMissions)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	MissionListNode ***peaMissionListNodes = ui_GenGetManagedListSafe(pGen, MissionListNode);
	S32 numValidNodes=0;
	MissionListNode *node = NULL;
	static Mission** s_eaSubMissions = NULL;
	int iExtraIndent = -1;
	bool bIsForCurrentMap = mission_IsForCurrentMap(pRootMission);
	bool bShadowMission = false;
	Entity *pEnt = entActivePlayerPtr();
	MissionDef *pDef = NULL;

	// Create the list of Objectives (UI strings)
	if (missionui_MissionShouldExpand(pRootMission) && bIsForCurrentMap)
	{
		// If the root mission has a UI string, create a node for that
		if (mission_HasUIString(pRootMission) && !pRootMission->openChildren) {
			node = GetOrCreateMissionListNode(peaMissionListNodes, numValidNodes++);

			PopulateNodeFromMission(node, pRootMission, /*pEntity=*/NULL, MissionListNodeType_UIString, /*iIndent=*/0, bShadowMission, /*iMissionNumber=*/0);

			iExtraIndent = 0;
		}

		if(bAddCompletedMissions || (mission_GetStateForUI(pRootMission) != MissionState_Succeeded)) {
			// Create a node for the UI string of each sub-mission
			numValidNodes += mission_MakeSubmissionNodes(0, NULL, pRootMission, pRootMission, peaMissionListNodes, numValidNodes, iExtraIndent, bShadowMission, bAddCompletedMissions);
		}
	}

	numValidNodes += mission_MakeFinalNodeForRootMission(0, NULL, pRootMission, numValidNodes, peaMissionListNodes, bShadowMission, bIsForCurrentMap);

	// Delete extra MissionListNodes
	MissionListNodeListSetLength(peaMissionListNodes, numValidNodes);
	ui_GenSetManagedListSafe(pGen, peaMissionListNodes, MissionListNode, true);
}

void FillFilterAndSortMissionsWithNumbers(Mission ***peaMissions)
{
	Entity *pEntity = entActivePlayerPtr();
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	const char *pcPrimaryMission = entGetPrimaryMission(pEntity);
	Mission *pPrimaryMission = NULL;
	U32 uHiTime = 0;
	S32 i;

	eaClearFast(peaMissions);

	if (pInfo)
	{
		S32 iHiTimestamp = -1, iCurTimestamp;
		Mission *pTimestampMission = NULL;
		// Get all root-level Missions
		for (i = 0; i < eaSize(&pInfo->missions); i++)
		{
			// If the mission has a display name and is the correct type then add it to the list
			if (pInfo->missions[i]
				&& mission_HasDisplayName(pInfo->missions[i])
				&& (mission_GetType(pInfo->missions[i]) == MissionType_Normal
					|| mission_GetType(pInfo->missions[i]) == MissionType_Nemesis
					|| mission_GetType(pInfo->missions[i]) == MissionType_NemesisArc
					|| mission_GetType(pInfo->missions[i]) == MissionType_NemesisSubArc
					|| mission_GetType(pInfo->missions[i]) == MissionType_Episode
					|| mission_GetType(pInfo->missions[i]) == MissionType_AutoAvailable)
				&& !pInfo->missions[i]->bHiddenFullChild)
			{
				iCurTimestamp = mission_GetlastClientNotifiedTimeRecursive(pInfo->missions[i]);
				if(iCurTimestamp > iHiTimestamp)
				{
					iHiTimestamp = iCurTimestamp;
					pTimestampMission = pInfo->missions[i];
				}
				// make the current primary mission on top
				if(pInfo->missions[i]->missionNameOrig == pcPrimaryMission){
					pPrimaryMission = pInfo->missions[i];
				} else if(!pInfo->missions[i]->bHidden){
					eaPush(peaMissions, pInfo->missions[i]);
				}
			}
		}

		// make sure at least one mission shows in UI (the highest timestamp mission)
		if(eaSize(peaMissions) < 1 && pTimestampMission && !pPrimaryMission)
		{
			eaPush(peaMissions, pTimestampMission);
		}
		
		eaStableSort(*peaMissions, NULL, mission_SortByLevel);
		eaStableSort(*peaMissions, NULL, mission_SortByState);
		eaStableSort(*peaMissions, NULL, mission_SortByCategory);

		// Insert the primary mission at the beginning
		if (pPrimaryMission)
			eaInsert(peaMissions, pPrimaryMission, 0);
	}
}

void fillUnifiedMissionListEx(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, S32 iMaxMissions, bool bSeparator)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	MissionListNode ***peaMissionListNodes = ui_GenGetManagedListSafe(pGen, MissionListNode);
	static Mission **s_eaSortedMissions = NULL;
	Mission *pPrimaryMission = NULL;
	S32 i, numValidNodes=0;
	const char *pcPrimaryMission = NULL;
	U32 uHiTime = 0;
	S32 iPrimaryIndex = -1;

	eaClearFast(&s_eaSortedMissions);

	pcPrimaryMission = entGetPrimaryMission(pEntity);

	if (pInfo)
	{
		S32 iHiTimestamp = -1, iCurTimestamp;
		Mission *pTimestampMission = NULL;
		// Get all root-level Missions
		for (i = 0; i < eaSize(&pInfo->missions); i++)
		{
			// If the mission has a display name and is the correct type then add it to the list
			if (pInfo->missions[i]
				&& mission_HasDisplayName(pInfo->missions[i])
				&& (mission_GetType(pInfo->missions[i]) == MissionType_Normal
					|| mission_GetType(pInfo->missions[i]) == MissionType_Nemesis
					|| mission_GetType(pInfo->missions[i]) == MissionType_NemesisArc
					|| mission_GetType(pInfo->missions[i]) == MissionType_NemesisSubArc
					|| mission_GetType(pInfo->missions[i]) == MissionType_Episode
					|| mission_GetType(pInfo->missions[i]) == MissionType_AutoAvailable)
				&& !pInfo->missions[i]->bHiddenFullChild)
			{
				iCurTimestamp = mission_GetlastClientNotifiedTimeRecursive(pInfo->missions[i]);
				if(iCurTimestamp > iHiTimestamp)
				{
					iHiTimestamp = iCurTimestamp;
					pTimestampMission = pInfo->missions[i];
				}
				// make the current primary mission on top
				if(pInfo->missions[i]->missionNameOrig == pcPrimaryMission){
					pPrimaryMission = pInfo->missions[i];
				} else if(!pInfo->missions[i]->bHidden){
					eaPush(&s_eaSortedMissions, pInfo->missions[i]);
				}
			}
		}
		// make sure at least one mission shows in UI (the highest timestamp mission)
		if(eaSize(&s_eaSortedMissions) < 1 && pTimestampMission && !pPrimaryMission)
		{
			eaPush(&s_eaSortedMissions, pTimestampMission);
		}
		
		eaStableSort(s_eaSortedMissions, NULL, mission_SortByLevel);
		eaStableSort(s_eaSortedMissions, NULL, mission_SortByState);
		eaStableSort(s_eaSortedMissions, NULL, mission_SortByCategory);

		// Insert the primary mission at the beginning
		if (pPrimaryMission){
			eaInsert(&s_eaSortedMissions, pPrimaryMission, 0);
		}

		// Truncate list to the maximum size
		eaRemoveRange(&s_eaSortedMissions, iMaxMissions, eaSize(&s_eaSortedMissions)-iMaxMissions);
	}

	// Create MissionListNodes for each mission or submission
	for (i = 0; i < eaSize(&s_eaSortedMissions); i++)
	{
		bool bShadowMission = (s_eaSortedMissions[i]->eCreditType != MissionCreditType_Primary);
		numValidNodes = mission_MakeMissionNodes(i, s_eaSortedMissions[i], pEntity, numValidNodes, peaMissionListNodes, bShadowMission, true, bSeparator);
	}

	// Delete extra MissionListNodes
	MissionListNodeListSetLength(peaMissionListNodes, numValidNodes);
	ui_GenSetManagedListSafe(pGen, peaMissionListNodes, MissionListNode, true);
}


// Fill the given gen's list with all missions, objectives, and subobjectives.
// There are a lot of special cases and little gotchas here.
// If a mission has a UI String, that should be displayed as a submission.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillUnifiedMissionList");
void exprFillUnifiedMissionList(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, S32 iMaxMissions)
{
	fillUnifiedMissionListEx(pContext, pEntity, iMaxMissions, false);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillUnifiedMissionListWithSeparator");
void exprFillUnifiedMissionListWithSeparator(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, S32 iMaxMissions, bool bSeparator)
{
	fillUnifiedMissionListEx(pContext, pEntity, iMaxMissions, bSeparator);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillUnifiedMissionList_GoldenPathFirst");
void exprFillUnifiedMissionList_GoldenPathFirst(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, S32 iMaxMissions, S32 iMaxNodes, bool bAddCompletedMissions, bool bAddCompletedSubMissions, bool bIsForMissionTracker)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	MissionListNode ***peaMissionListNodes = ui_GenGetManagedListSafe(pGen, MissionListNode);
	static Mission **s_eaSortedMissions = NULL;
	static S32* s_eaiCurrentMapIndicies = NULL;
	Mission *pPrimaryMission = NULL;
	S32 i, numValidNodes=0;
	const char *pcPrimaryMission = NULL;
	U32 uHiTime = 0;

	eaClearFast(&s_eaSortedMissions);
	eaiClearFast(&s_eaiCurrentMapIndicies);

	pcPrimaryMission = entGetPrimaryMission(pEntity);

	if (pInfo)
	{
		// Get all root-level Missions
		for (i = 0; i < eaSize(&pInfo->missions); i++)
		{
			// If the mission has a display name and is the correct type then add it to the list
			if (pInfo->missions[i]
			&& mission_HasDisplayName(pInfo->missions[i])
				&& (mission_GetType(pInfo->missions[i]) != MissionType_Perk
				&& mission_GetType(pInfo->missions[i]) != MissionType_OpenMission)
				&& !pInfo->missions[i]->bHiddenFullChild
				&& !pInfo->missions[i]->bHidden)
			{
				MissionDef* pDef = mission_GetDef(pInfo->missions[i]);
				if(!bAddCompletedMissions && (mission_GetStateForUI(pInfo->missions[i]) == MissionState_Succeeded)) {
					continue;
				}

				if (bIsForMissionTracker && pDef->bOmitFromMissionTracker)
					continue;

				// make the current primary mission the best time, i.e. top
				if(!goldenPath_IsNoTargetAvailable() && goldenPath_isPathingToMission(pInfo->missions[i]))
				{
					pPrimaryMission = pInfo->missions[i];
				}
				else if (goldenPath_IsNoTargetAvailable() && pInfo->missions[i]->missionNameOrig == pcPrimaryMission)
				{
					pPrimaryMission = pInfo->missions[i];
				}
				else if(!gConf.bUpdateCurrentMapMissions && exprMissionOrSubMissionHasObjectiveOnCurrentMap(pContext, pInfo->missions[i]))
				{
					eaiPush(&s_eaiCurrentMapIndicies, i);
				}
				else
				{
					eaPush(&s_eaSortedMissions, pInfo->missions[i]);
				}
			}
		}

		// Sort the list
		eaQSort(s_eaSortedMissions, mission_SortByUpdateTime);

		// Insert the Current Map Missions
		for(i = 0; i < eaiSize(&s_eaiCurrentMapIndicies); i++) {
			eaInsert(&s_eaSortedMissions, pInfo->missions[s_eaiCurrentMapIndicies[i]], 0);
		}

		//Pull the tracked missions to the top
		eaStableSort(s_eaSortedMissions, NULL, mission_SortByTracked);

		// Insert the primary mission at the beginning
		if (pPrimaryMission){
			eaInsert(&s_eaSortedMissions, pPrimaryMission, 0);
		}

		// Truncate list to the maximum size
		eaRemoveRange(&s_eaSortedMissions, iMaxMissions, eaSize(&s_eaSortedMissions)-iMaxMissions);
	}

	// Create MissionListNodes for each mission or submission
	for (i = 0; i < eaSize(&s_eaSortedMissions) && ((iMaxNodes <= 0) || (iMaxNodes > numValidNodes)); i++)
	{
		bool bShadowMission = (s_eaSortedMissions[i]->eCreditType != MissionCreditType_Primary);
		numValidNodes = mission_MakeMissionNodes(i, s_eaSortedMissions[i], pEntity, numValidNodes, peaMissionListNodes, bShadowMission, bAddCompletedSubMissions, false);
	}

	// Delete extra MissionListNodes
	MissionListNodeListSetLength(peaMissionListNodes, numValidNodes);

	// Restrict down to desired number of nodes
	if ((iMaxNodes > 0) && (iMaxNodes < numValidNodes))
	{
		// Look back to see if we can chop off a mission
		for(i=numValidNodes-1; i>=0; --i)
		{
			MissionListNode *pNode = (*peaMissionListNodes)[i];
			if (pNode->eType == MissionListNodeType_DisplayName)
			{
				break;
			}
		}
		if (i > 0)
		{
			// Found a mission we can drop off the list to save space
			MissionListNodeListSetLength(peaMissionListNodes, i);
		}
		else
		{
			// Only one mission in tracker and it's too long.
			// Try removing succeeded nodes to create space from the top down
			for(i=1; (i<eaSize(peaMissionListNodes)) && (iMaxNodes < eaSize(peaMissionListNodes)); )
			{
				MissionListNode *pNode = (*peaMissionListNodes)[i];
				if ((pNode->eType == MissionListNodeType_UIString) && pNode->pMission && (pNode->pMission->state == MissionState_Succeeded))
				{
					MissionListNodeFree(pNode);
					eaRemove(peaMissionListNodes, i);
				}
				else
				{
					++i;
				}
			}
		}
	}

	ui_GenSetManagedListSafe(pGen, peaMissionListNodes, MissionListNode, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillUnifiedMissionListRespectOmissions_CurMapFirst");
void exprFillUnifiedMissionListRespectOmissions_CurMapFirst(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, S32 iMaxMissions, S32 iMaxNodes, bool bAddCompletedMissions, bool bAddCompletedSubMissions, bool bIsForMissionTracker)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	MissionListNode ***peaMissionListNodes = ui_GenGetManagedListSafe(pGen, MissionListNode);
	static Mission **s_eaSortedMissions = NULL;
	static S32* s_eaiCurrentMapIndicies = NULL;
	Mission *pPrimaryMission = NULL;
	S32 i, numValidNodes=0;
	const char *pcPrimaryMission = NULL;
	U32 uHiTime = 0;

	eaClearFast(&s_eaSortedMissions);
	eaiClearFast(&s_eaiCurrentMapIndicies);

	pcPrimaryMission = entGetPrimaryMission(pEntity);

	if (pInfo)
	{
		// Get all root-level Missions
		for (i = 0; i < eaSize(&pInfo->missions); i++)
		{
			// If the mission has a display name and is the correct type then add it to the list
			if (pInfo->missions[i]
				&& mission_HasDisplayName(pInfo->missions[i])
				&& (mission_GetType(pInfo->missions[i]) != MissionType_Perk
					&& mission_GetType(pInfo->missions[i]) != MissionType_OpenMission)
				&& !pInfo->missions[i]->bHiddenFullChild)
			{
				MissionDef* pDef = mission_GetDef(pInfo->missions[i]);
				if(!bAddCompletedMissions && (mission_GetStateForUI(pInfo->missions[i]) == MissionState_Succeeded)) {
					continue;
				}

				if (bIsForMissionTracker && pDef->bOmitFromMissionTracker)
					continue;

				// make the current primary mission the best time, i.e. top
				if(pInfo->missions[i]->missionNameOrig == pcPrimaryMission)
				{
					pPrimaryMission = pInfo->missions[i];
				}
				else if(!gConf.bUpdateCurrentMapMissions && exprMissionOrSubMissionHasObjectiveOnCurrentMap(pContext, pInfo->missions[i]))
				{
					eaiPush(&s_eaiCurrentMapIndicies, i);
				}
				else
				{
					eaPush(&s_eaSortedMissions, pInfo->missions[i]);
				}
			}
		}

		// Sort the list
		eaQSort(s_eaSortedMissions, mission_SortByUpdateTime);

		// Insert the Current Map Missions
		for(i = 0; i < eaiSize(&s_eaiCurrentMapIndicies); i++) {
			eaInsert(&s_eaSortedMissions, pInfo->missions[s_eaiCurrentMapIndicies[i]], 0);
		}

		// Insert the primary mission at the beginning
		if (pPrimaryMission){
			eaInsert(&s_eaSortedMissions, pPrimaryMission, 0);
		}

		// Truncate list to the maximum size
		eaRemoveRange(&s_eaSortedMissions, iMaxMissions, eaSize(&s_eaSortedMissions)-iMaxMissions);
	}

	// Create MissionListNodes for each mission or submission
	for (i = 0; i < eaSize(&s_eaSortedMissions) && ((iMaxNodes <= 0) || (iMaxNodes > numValidNodes)); i++)
	{
		bool bShadowMission = (s_eaSortedMissions[i]->eCreditType != MissionCreditType_Primary);
		numValidNodes = mission_MakeMissionNodes(i, s_eaSortedMissions[i], pEntity, numValidNodes, peaMissionListNodes, bShadowMission, bAddCompletedSubMissions, false);
	}

	// Delete extra MissionListNodes
	MissionListNodeListSetLength(peaMissionListNodes, numValidNodes);

	// Restrict down to desired number of nodes
	if ((iMaxNodes > 0) && (iMaxNodes < numValidNodes))
	{
		// Look back to see if we can chop off a mission
		for(i=numValidNodes-1; i>=0; --i)
		{
			MissionListNode *pNode = (*peaMissionListNodes)[i];
			if (pNode->eType == MissionListNodeType_DisplayName)
			{
				break;
			}
		}
		if (i > 0)
		{
			// Found a mission we can drop off the list to save space
			MissionListNodeListSetLength(peaMissionListNodes, i);
		}
		else
		{
			// Only one mission in tracker and it's too long.
			// Try removing succeeded nodes to create space from the top down
			for(i=1; (i<eaSize(peaMissionListNodes)) && (iMaxNodes < eaSize(peaMissionListNodes)); )
			{
				MissionListNode *pNode = (*peaMissionListNodes)[i];
				if ((pNode->eType == MissionListNodeType_UIString) && pNode->pMission && (pNode->pMission->state == MissionState_Succeeded))
				{
					MissionListNodeFree(pNode);
					eaRemove(peaMissionListNodes, i);
				}
				else
				{
					++i;
				}
			}
		}
	}

	ui_GenSetManagedListSafe(pGen, peaMissionListNodes, MissionListNode, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillUnifiedMissionList_CurMapFirst");
void exprFillUnifiedMissionList_CurMapFirst(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, S32 iMaxMissions, S32 iMaxNodes, bool bAddCompletedMissions, bool bAddCompletedSubMissions)
{
	exprFillUnifiedMissionListRespectOmissions_CurMapFirst( pContext, pEntity, iMaxMissions, iMaxNodes, bAddCompletedMissions, bAddCompletedSubMissions, 0);
}

// Fill the given gen's list with all missions, objectives, and subobjectives for all mission on the current map.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillUnifiedCurrentMapMissionList");
void exprFillUnifiedCurrentMapMissionList(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, S32 iMaxMissions)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	MissionListNode ***peaMissionListNodes = ui_GenGetManagedListSafe(pGen, MissionListNode);
	static Mission **s_eaSortedMissions = NULL;
	Mission *pPrimaryMission = NULL;
	S32 i, numValidNodes=0;
	const char *pcPrimaryMission = NULL;
	U32 uHiTime = 0;
	S32 iPrimaryIndex = -1;

	eaClearFast(&s_eaSortedMissions);

	pcPrimaryMission = entGetPrimaryMission(pEntity);

	if (pInfo)
	{
		// Get all root-level Missions
		for (i = 0; i < eaSize(&pInfo->missions); i++)
		{
			// If the mission has a display name and is the correct type then add it to the list
			if (pInfo->missions[i]
			&& mission_HasDisplayName(pInfo->missions[i])
				&& (mission_GetType(pInfo->missions[i]) != MissionType_Perk
				&& mission_GetType(pInfo->missions[i]) != MissionType_OpenMission)
				&& !pInfo->missions[i]->bHiddenFullChild
				&& exprMissionOrSubMissionHasObjectiveOnCurrentMap(NULL, pInfo->missions[i]))
			{
				// make the current primary mission the best time, i.e. top
				if(pcPrimaryMission && pInfo->missions[i]->missionNameOrig == pcPrimaryMission){
					pPrimaryMission = pInfo->missions[i];
				} else {
					eaPush(&s_eaSortedMissions, pInfo->missions[i]);
				}
			}
		}

		// Sort the list
		eaQSort(s_eaSortedMissions, mission_SortByUpdateTime);

		// Insert the primary mission at the beginning
		if (pPrimaryMission){
			eaInsert(&s_eaSortedMissions, pPrimaryMission, 0);
		}

		// Truncate list to the maximum size
		eaRemoveRange(&s_eaSortedMissions, iMaxMissions, eaSize(&s_eaSortedMissions)-iMaxMissions);
	}

	// Create MissionListNodes for each mission or submission
	for (i = 0; i < eaSize(&s_eaSortedMissions); i++)
	{
		bool bShadowMission = (s_eaSortedMissions[i]->eCreditType != MissionCreditType_Primary);
		numValidNodes = mission_MakeMissionNodes(i, s_eaSortedMissions[i], pEntity, numValidNodes, peaMissionListNodes, bShadowMission, true, false);
	}

	// Delete extra MissionListNodes
	MissionListNodeListSetLength(peaMissionListNodes, numValidNodes);
	ui_GenSetListSafe(pGen, peaMissionListNodes, MissionListNode);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetConForMissionAndLevelDeltaWithSeparator");
const char* exprGetConForMissionAndLevelDelta(ExprContext *pContext, SA_PARAM_NN_VALID Mission *pMission, S32 iLevelDelta, SA_PARAM_OP_STR const char *pchSeparator)
{
	char result[32] = {0};
	const char *pchConAdverb = NULL;
	const char *pchConAdjective = NULL;

	if (pMission->eCreditType == MissionCreditType_Flashback) // Special Case - Flashback Missions should appear as Equal
	{
		pchConAdverb = NULL;
		pchConAdjective = "Equal";
	}
	else if (pMission->eCreditType != MissionCreditType_Primary) // Special Case - Missions that give Secondary credit should appear grey
	{
		pchConAdverb = NULL;
		pchConAdjective = "Secondary";
	}
	else if (iLevelDelta > 3)
	{
		pchConAdverb = "Extremely";
		pchConAdjective = "Hard";
	}
	else if (iLevelDelta == 3)
	{
		pchConAdverb = "Very";
		pchConAdjective = "Hard";
	}
	else if (iLevelDelta == 2)
	{
		pchConAdverb = NULL;
		pchConAdjective = "Hard";
	}
	else if (iLevelDelta == 1)
	{
		pchConAdverb = "Slightly";
		pchConAdjective = "Hard";
	}
	else if (iLevelDelta == 0)
	{
		pchConAdverb = NULL;
		pchConAdjective = "Equal";
	}
	else if (iLevelDelta == -1)
	{
		pchConAdverb = "Slightly";
		pchConAdjective = "Easy";
	}
	else if (iLevelDelta == -2)
	{
		pchConAdverb = NULL;
		pchConAdjective = "Easy";
	}
	else if (iLevelDelta == -3)
	{
		pchConAdverb = "Very";
		pchConAdjective = "Easy";
	}
	else if (iLevelDelta < -3)
	{
		pchConAdverb = "Extremely";
		pchConAdjective = "Easy";
	}

	if (pchConAdverb || pchConAdjective)
	{
		if (pchConAdverb && pchConAdjective)
		{
			if(pchSeparator)
				snprintf(result, sizeof(result) - 1, "%s%s%s", pchConAdverb, pchSeparator, pchConAdjective);
			else
				snprintf(result, sizeof(result) - 1, "%s%s", pchConAdverb, pchConAdjective);
		}
		else if(pchConAdverb)
			snprintf(result, sizeof(result) - 1, "%s", pchConAdverb);
		else if(pchConAdjective)
			snprintf(result, sizeof(result) - 1, "%s", pchConAdjective);
	}

	return exprContextAllocString(pContext, result);;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionListNodeMissionNumber");
S32 exprGetMissionListNodeMissionNumber(SA_PARAM_OP_VALID MissionListNode *node)
{
	if (node)
		return node->iMissionNumber + 1; // they are set starting at zero, but we want to display starting at 1
	return 1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionListJournalRowMissionNumber");
S32 GetMissionListJournalRowMissionNumber(SA_PARAM_OP_VALID MissionJournalRow *pRow)
{
	if (pRow)
		return pRow->iMissionNumber + 1; // they are set starting at zero, but we want to display starting at 1
	return 1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionListNodeType");
const char* exprGetMissionListNodeType(SA_PARAM_OP_VALID MissionListNode *node)
{
	if (node)
		return StaticDefineIntRevLookup(MissionListNodeTypeEnum, node->eType);
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillMissionObjectiveList");
void exprFillMissionObjectiveList(ExprContext *pContext, SA_PARAM_OP_VALID Mission *pMission)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	static Mission** s_SubMissionList = NULL;

	eaClear(&s_SubMissionList);
	if (pMission)
		mission_GetObjectiveList(pMission, &s_SubMissionList, true);

	if (pGen)
		// Missions are owned by pMission.
		ui_GenSetManagedListSafe(pGen, &s_SubMissionList, Mission, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillMissionObjectiveListNoCollapse");
void exprFillMissionObjectiveListNoCollapse(ExprContext *pContext, SA_PARAM_OP_VALID Mission *pMission)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	static Mission** s_SubMissionList = NULL;

	eaClear(&s_SubMissionList);
	if (pMission)
		mission_GetObjectiveList(pMission, &s_SubMissionList, false);

	if (pGen)
		// Missions are owned by pMission
		ui_GenSetManagedListSafe(pGen, &s_SubMissionList, Mission, false);
}

// This fills a small list of MissionListNodes for whatever objective this player is working on.
// It will always be exactly 2 rows - the mission title, and a UIString/objective row.
// This is slightly wasteful, but I think it's the easiest way to make sure the formatting matches
// the formatting on the HUD tracker list.
//
// If there is a Primary Mission, this will show the in-progress objective from the Primary Mission.
// Otherwise, it's the first in-progress objective from the most recently active mission
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCurrentMissionObjective");
void exprGetCurrentMissionObjective(ExprContext *pContext, Entity *pEnt)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	MissionListNode ***peaMissionListNodes = ui_GenGetManagedListSafe(pGen, MissionListNode);
	MissionInfo *pInfo = SAFE_MEMBER2(pEnt, pPlayer, missionInfo);
	MissionDef *pDef = NULL;
	S32 numValidNodes=0;

	if (pEnt && pInfo){
		MissionDef *pRootDef = NULL;
		Mission *pRootMission = NULL;
		Mission *pObjective = NULL;

		// Get the correct mission to display for this ent
		if (pInfo->pTeamPrimaryMission){
			pRootMission = mission_GetMissionByName(pInfo, pInfo->pTeamPrimaryMission->missionNameOrig);
			if (pRootMission) {
				if (pInfo->pchTeamCurrentObjective == pRootMission->missionNameOrig){
					pObjective = pRootMission;
				} else if (pInfo->pchTeamCurrentObjective) {
					pObjective = mission_FindChildByName(pRootMission, pInfo->pchTeamCurrentObjective);
				}
			}
		} else if (pInfo->pchPrimarySoloMission) {
			pRootMission = mission_GetMissionByName(pInfo, pInfo->pchPrimarySoloMission);
			if (pRootMission){
				pObjective = mission_GetFirstInProgressObjective(pRootMission);
			}
		} else if (pInfo->pchLastActiveMission){
			pRootMission = mission_GetMissionByName(pInfo, pInfo->pchLastActiveMission);
			if (pRootMission){
				pObjective = mission_GetFirstInProgressObjective(pRootMission);
			}
		}

		if (pRootMission){
			MissionListNode *node = GetOrCreateMissionListNode(peaMissionListNodes, numValidNodes);
			bool bShadowMission = (pRootMission->eCreditType != MissionCreditType_Primary);

			PopulateNodeFromMission(node, pRootMission, pEnt, MissionListNodeType_DisplayName, /*iIndent=*/0, bShadowMission, /*iMissionNumber=*/0);
			
			numValidNodes++;

			if (pObjective){
				node = GetOrCreateMissionListNode(peaMissionListNodes, numValidNodes);

				PopulateNodeFromMission(node, pObjective, pEnt, MissionListNodeType_UIString, /*iIndent=*/0, bShadowMission, /*iMissionNumber=*/0);
				
				numValidNodes++;
			} else {
				numValidNodes += mission_MakeFinalNodeForRootMission(0, pEnt, pRootMission, numValidNodes, peaMissionListNodes, bShadowMission, mission_IsForCurrentMap(pRootMission));
			}
		}
	}

	// Delete extra MissionListNodes
	MissionListNodeListSetLength(peaMissionListNodes, numValidNodes);
	ui_GenSetListSafe(pGen, peaMissionListNodes, MissionListNode);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetNumObjectives");
int exprMissionGetNumObjectives(SA_PARAM_OP_VALID Mission *pMission)
{
	static Mission** s_SubMissionList = NULL;

	if (pMission)
	{
		eaClear(&s_SubMissionList);
		mission_GetObjectiveList(pMission, &s_SubMissionList, true);
		return eaSize(&s_SubMissionList);
	}
	return 0;
}

// Sorts the entire mission list by Start Time
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionListSortByStartTime");
void exprMissionListSortByStartTime(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	Mission ***peaList = ui_GenGetManagedListSafe(pGen, Mission);
	eaStableSort((*peaList), NULL, mission_SortByStartTime);
}

// Pulls any tracked missions to the top of the list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionListTrackedFirst");
void exprMissionListTrackedFirst(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	Mission ***peaList = ui_GenGetManagedListSafe(pGen, Mission);
	eaStableSort((*peaList), NULL, mission_SortByTracked);
}

// Sorts by the mission state - InProgress, Completed, Failed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionListSortByState");
void exprMissionListSortByState(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	Mission ***peaList = ui_GenGetManagedListSafe(pGen, Mission);
	eaStableSort((*peaList), NULL, mission_SortByState);
}

// Sorts by when the mission was last updated (count changed or sub-objective completed)
// This could be slow and may need optimization at some point
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionListSortByUpdateTime");
void exprMissionListSortByUpdateTime(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	Mission ***peaList = ui_GenGetManagedListSafe(pGen, Mission);
	eaStableSort((*peaList), NULL, mission_SortByUpdateTimeStable);
}

// Filters a mission list to get only a specific type ("Normal", "Perk", etc.)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionListFilterByType");
void exprMissionListFilterByType(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *type)
{
	Mission ***peaList = ui_GenGetManagedListSafe(pGen, Mission);
	MissionType missionType = StaticDefineIntGetInt(MissionTypeEnum, type);

	int i, n = eaSize(peaList);
	for (i = n-1; i >= 0; --i)
	{
		Mission *mission = (*peaList)[i];
		MissionDef *def = mission_GetDef(mission);
		if (def && !mission_MissionTypesSortTogether(missionType, def->missionType))
			eaRemove(peaList, i);
	}
	ui_GenSetListSafe(pGen, peaList, Mission);
}

// Filters a mission list to include only those that have been updated within some amount of time.
// Ignores missions that are being "tracked" by the player.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionListFilterBylastClientNotifiedTime");
void exprMissionListFilterBylastClientNotifiedTime(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, U32 maxUpdateTimeSeconds)
{
	Mission ***peaList = ui_GenGetManagedListSafe(pGen, Mission);
	U32 currTime = timeSecondsSince2000();
	int i, n = eaSize(peaList);

	for (i = n-1; i >= 0; --i)
	{
		Mission *mission = (*peaList)[i];
		U32 time =  mission_GetlastClientNotifiedTimeRecursive(mission);
		if (!mission->tracking && (currTime > time) && (currTime - mission_GetlastClientNotifiedTimeRecursive(mission) > maxUpdateTimeSeconds))
			eaRemove(peaList, i);
	}
	ui_GenSetListSafe(pGen, peaList, Mission);
}

// Gets the time since the mission has been updated, including if children have been updated
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetTimeSinceUpdate");
U32 exprMissionGetTimeSinceUpdate(ExprContext *pContext, SA_PARAM_NN_VALID Mission *mission)
{
	U32 currTime = timeSecondsSince2000();
	U32 time = mission_GetlastClientNotifiedTimeRecursive(mission);
	if (currTime > time)
		return currTime - mission_GetlastClientNotifiedTimeRecursive(mission);
	else
		return 0;
}

// Gets the time since the mission has been updated.  Doesn't recurse from children.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetTimeSinceUpdateSelfOnly");
U32 exprMissionGetTimeSinceUpdateSelfOnly(ExprContext *pContext, SA_PARAM_NN_VALID Mission *mission)
{
	U32 currTime = timeSecondsSince2000();
	MissionDef *pDef = mission_GetDef(mission);
	U32 lastTime = pDef ? mission_LastUpdateTime(pDef->pchRefString) : 0;
	if (currTime > lastTime)
		return currTime - lastTime;
	else
		return 0;
}

// Gets the depth of the mission to be used in displaying the mission in the objective list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetDepth");
U32 exprMissionGetDepth(ExprContext *pContext, SA_PARAM_NN_VALID Mission *mission)
{
	if (mission)
	{
		MissionDef *rootMissionDef = mission_GetDef(mission);
		while(rootMissionDef && GET_REF(rootMissionDef->parentDef))
			rootMissionDef = GET_REF(rootMissionDef->parentDef);
		if (mission->depth > 0 && !missiondef_HasUIString(rootMissionDef))
			return mission->depth-1;
		else
			return mission->depth;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetLevel");
U32 exprMissionGetLevel(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID Mission *mission)
{
	if (mission)
	{
		if(mission->iLevel) {
			return mission->iLevel;
		} else {
			MissionDef *def = mission_GetDef(mission);
			if (def)
			{
				return def->levelDef.missionLevel;
			}
		}
	}
	return 0;
}

// Gets the level of a mission as a string
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetLevelAsString");
const char* exprMissionGetLevelAsString(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_VALID Mission *mission)
{
	if (mission)
	{
		static char buffer[12];
		itoa(exprMissionGetLevel(pContext, pEntity, mission), buffer, 10);
		return buffer;
	}
	return "";
}

// Gets the recommended team size for a mission
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("MissionGetTeamSize");
U32 exprMissionGetTeamSize(ExprContext *pContext, SA_PARAM_OP_VALID Mission *mission)
{
	if (mission){
		MissionDef *def = mission_GetDef(mission);
		if (def)
			return def->iSuggestedTeamSize;
	}
	return 0;
}

// Gets the recommended team size for a mission as a string
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("MissionGetTeamSizeString");
const char* exprMissionGetTeamSizeString(ExprContext *pContext, SA_PARAM_OP_VALID Mission *mission)
{
	static char buffer[12];
	if (mission){
		MissionDef *def = mission_GetDef(mission);
		if (def){
			sprintf(buffer, "%d", def->iSuggestedTeamSize);
			return buffer;
		}
	}
	return "";
}

// Gets the recommended team size for a mission
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("MissionDefGetTeamSize");
U32 exprMissionDefGetTeamSize(ExprContext *pContext, SA_PARAM_OP_VALID MissionDef *def)
{
	if (def)
		return def->iSuggestedTeamSize;
	return 0;
}

// Gets the recommended team size for a mission as a string
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("MissionDefGetTeamSizeString");
const char* exprMissionDefGetTeamSizeString(ExprContext *pContext, SA_PARAM_OP_VALID MissionDef *def)
{
	static char buffer[12];
	if (def){
		sprintf(buffer, "%d", def->iSuggestedTeamSize);
		return buffer;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalRowGetMapDisplayName");
const char* exprMissionJournalRowGetMapDisplayName(ExprContext *pContext, SA_PARAM_NN_VALID MissionJournalRow *pListRow)
{
	if (pListRow->pchTranslatedCategoryName)
	{
		return pListRow->pchTranslatedCategoryName;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MapTimerGetString");
const char *exprMapTimerGetString(void)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(entActivePlayerPtr());
	static char buffer[128];

	sprintf(buffer, "");
	if (pInfo && eaSize(&pInfo->clientGameTimers))
	{
		GameTimer *pTimer = pInfo->clientGameTimers[0];
		F32 seconds = 0.0f;
		if (pTimer && (seconds = gametimer_GetRemainingSeconds(pTimer)) > -30.0f)
		{
			int minutes = 0;
			seconds = MAX(0, seconds); // clamp to 0
			minutes = floor(seconds/60);
			while (seconds >= 60.0f)
				seconds -= 60.0f;
			sprintf(buffer, "%d:%05.2f", minutes, seconds);
		}
	}
	return buffer;
}

// Inform the client of a changed mission.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void mission_NotifyUpdate(const char *pchMission)
{
	Entity *pEnt = entActivePlayerPtr();
	EntityMissionUpdateTime *pTime = NULL;
	S32 i;

	if (!pEnt)
		return;

	pchMission = allocAddString(pchMission);

	for (i = eaSize(&s_eaMissionUpdateTimes) - 1; i >= 0; i--)
	{
		if (s_eaMissionUpdateTimes[i]->uContainerID == entGetContainerID(pEnt) &&
			s_eaMissionUpdateTimes[i]->pchMissionName == pchMission)
		{
			pTime = s_eaMissionUpdateTimes[i];
			break;
		}
	}
	if (!pTime)
	{
		pTime = calloc(1, sizeof(EntityMissionUpdateTime));
		if (!pTime)
			return;

		eaPush(&s_eaMissionUpdateTimes, pTime);
		pTime->pchMissionName = pchMission;
		pTime->uContainerID = entGetContainerID(pEnt);
	}

	pTime->uLastUpdateTime = timeSecondsSince2000();
}

U32 mission_LastUpdateTime(const char *pchMission)
{
	Entity *pEnt = entActivePlayerPtr();
	S32 i;

	if (!pEnt)
		return 0;

	pchMission = allocFindString(pchMission);
	if (!pchMission)
		return 0;

	for (i = eaSize(&s_eaMissionUpdateTimes) - 1; i >= 0; i--)
	{
		if (s_eaMissionUpdateTimes[i]->uContainerID == entGetContainerID(pEnt) &&
			s_eaMissionUpdateTimes[i]->pchMissionName == pchMission)
		{
			return s_eaMissionUpdateTimes[i]->uLastUpdateTime;
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetPerkIcon");
const char* exprGetPerkIcon(ExprContext *pContext, SA_PARAM_NN_VALID MissionJournalRow *pListRow)
{
	MissionDef *pDef = missionjournalrow_GetMissionDef(pListRow);
	if (pDef && pDef->missionType == MissionType_Perk)
	{
		const char* pchIconName = pDef->pchIconName;
		return pchIconName ? pchIconName : NULL_TO_EMPTY(pDef->pchIconName);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalRowGetPerkPoints");
const char* exprMissionJournalRowGetPerkPoints(ExprContext *pContext, SA_PARAM_NN_VALID MissionJournalRow *pListRow)
{
	MissionDef *pDef = missionjournalrow_GetMissionDef(pListRow);
	static char buffer[10];
	if (pDef && pDef->missionType == MissionType_Perk){
		sprintf(buffer, "%d", pDef->iPerkPoints);
		return buffer;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalRowGetPerkPointCount");
S32 exprMissionJournalRowGetPerkPointCount(ExprContext *pContext, SA_PARAM_NN_VALID MissionJournalRow *pListRow)
{
	MissionDef *pDef = missionjournalrow_GetMissionDef(pListRow);
	if (pDef && pDef->missionType == MissionType_Perk){
		return pDef->iPerkPoints;
	}
	return -1;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PlayerGetTotalPerkPoints");
const char* exprPlayerGetTotalPerkPoints(SA_PARAM_OP_VALID Entity *pEntity)
{
	static char buffer[10];
	if (pEntity && pEntity->pPlayer && pEntity->pPlayer->missionInfo){
		sprintf(buffer, "%d", pEntity->pPlayer->missionInfo->iTotalPerkPoints);
		return buffer;
	}
	return "0";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PlayerGetTotalPerkPointCount");
S32 exprPlayerGetTotalPerkPointCount(SA_PARAM_OP_VALID Entity *pEntity)
{
	return SAFE_MEMBER3(pEntity, pPlayer, missionInfo, iTotalPerkPoints);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionCreditType");
const char* exprGetMissionCreditType(SA_PARAM_OP_VALID Mission *pMission) {
	if(pMission)
		return StaticDefineIntRevLookup(MissionCreditTypeEnum, pMission->eCreditType);
	else
		return "";
}


//*****************************************************************************
// Description: Gets the logical name of the mission stored in this journal row
//				by looking at the stored mission def reference.
//
//				This function does not rely on the mission or completed mission
//				pointer, and thus should be safer than looking up the name from
//				one of those pointers.
//
// Returns:     < const char* > Logical name of the mission or empty string if
//								journal row has no stored mission
// Parameter:   < SA_PARAM_OP_VALID MissionJournalRow * pRow >
//*****************************************************************************
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMissionDefNameFromJournalRow");
const char* exprGetMissionDefNameFromJournalRow(SA_PARAM_OP_VALID MissionJournalRow* pRow)
{
	if(pRow && IS_HANDLE_ACTIVE(pRow->hDef))
	{
		return REF_STRING_FROM_HANDLE(pRow->hDef);
	}
	return "";
}

//*****************************************************************************
// Description: Determines if the mission journal row is being tracked
//
//				This function does not rely on the mission pointer, and thus
//				should be safer than determining if the mission is tracked by
//				using that pointer.
//
// Returns:     < bool > True if mission is tracked
// Parameter:   < SA_PARAM_OP_VALID MissionJournalRow * pRow >
//*****************************************************************************
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsMissionJournalRowTracked");
bool exprIsMissionJournalRowTracked(SA_PARAM_OP_VALID Entity* pPlayerEnt, SA_PARAM_OP_VALID MissionJournalRow* pRow)
{
	if(pRow && pRow->type == MissionJournalRowType_Mission)
	{
		const char* pchMission = REF_STRING_FROM_HANDLE(pRow->hDef);
		if(pchMission)
		{
			Mission* pMission = exprMissionFromName(pPlayerEnt, pchMission);
			return (pMission && pMission->tracking);
		}
	}
	return false;
}

// ------------------------------------------------------------------
//  Open Mission expressions
// ------------------------------------------------------------------

static bool s_bShouldShowLeaderboard = false;

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetActiveOpenMission");
SA_RET_OP_VALID OpenMission* exprGetActiveOpenMission(SA_PARAM_OP_VALID Entity *pEntity)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	if (pInfo)
		return mapState_OpenMissionFromName(mapStateClient_Get(),pInfo->pchCurrentOpenMission);
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetOpenMissionChildMission");
SA_RET_OP_VALID Mission* exprGetOpenMissionChildMission(SA_PARAM_OP_VALID Entity *pEntity, const char *pchName)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	OpenMission *pOpenMission = NULL;
	Mission *pSubMission = NULL;
	if (pInfo)
	{
		pOpenMission = mapState_OpenMissionFromName(mapStateClient_Get(),pInfo->pchCurrentOpenMission);
		if(pOpenMission && pOpenMission->pMission)
		{
			int i;
			for(i = eaSize(&pOpenMission->pMission->children)-1; i>=0; i--)
			{
				if(stricmp(pOpenMission->pMission->children[i]->missionNameOrig, pchName) == 0)
				{
					pSubMission = pOpenMission->pMission->children[i];
					break;
				}
			}
		}
	}
	return pSubMission;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("HasActiveOpenMission");
bool exprHasActiveOpenMission(SA_PARAM_OP_VALID Entity *pEntity)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	if (pInfo && mapState_OpenMissionFromName(mapStateClient_Get(),pInfo->pchCurrentOpenMission))
		return true;
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ActiveOpenMissionIsRelatedToAnyPersonalMission");
bool exprActiveOpenMissionIsRelatedToAnyPersonalMission(SA_PARAM_OP_VALID Entity *pEntity)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	OpenMission *pOpenMission = NULL;
	if (pInfo && (pOpenMission = mapState_OpenMissionFromName(mapStateClient_Get(),pInfo->pchCurrentOpenMission)))
	{
		// See if this open mission is related to any mission
		FOR_EACH_IN_EARRAY_FORWARDS(pInfo->missions, Mission, pCurrentMission)
		{
			if (mission_FindRelatedMissionByOpenMission(pCurrentMission, pOpenMission))
				return true;
		}
		FOR_EACH_END

		return false;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionHasResetTimer");
bool exprOpenMissionHasResetTimer(SA_PARAM_OP_VALID OpenMission *pOpenMission)
{
	if (pOpenMission && pOpenMission->uiResetTimeRemaining)
		return true;
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionGetResetTimeString");
char* exprOpenMissionGetResetTimeString(SA_PARAM_OP_VALID OpenMission *pOpenMission)
{
	static char *estrBuffer = NULL;

	if (!estrBuffer)
		estrCreate(&estrBuffer);
	estrClear(&estrBuffer);

	if (pOpenMission && pOpenMission->uiResetTimeRemaining){
		U32 seconds = pOpenMission->uiResetTimeRemaining;
		if (seconds < 3600)
			estrConcatf(&estrBuffer, "%d:%02d", seconds/60, seconds%60);
		else
			estrConcatf(&estrBuffer, "%d:%d:%02d", seconds/3600, (seconds%3600)/60, seconds%60);
	}

	return NULL_TO_EMPTY(estrBuffer);
}

static int openmission_CompareScoreEntires(const OpenMissionScoreEntry** a, const OpenMissionScoreEntry** b)
{
	if (a && *a && b && *b){
		if ((int)(*a)->fPoints != (int)(*b)->fPoints){
			return (int)(*b)->fPoints - (int)(*a)->fPoints;
		} else {
			return stricmp((*a)->pchPlayerName, (*b)->pchPlayerName);
		}
	}
	return 0;
}

static int openmission_CompareScoreEntriesFloat(const OpenMissionScoreEntry** a, const OpenMissionScoreEntry** b)
{
	if (a && *a && b && *b){
		if ((*a)->fPoints != (*b)->fPoints){
			return round((*b)->fPoints - (*a)->fPoints);
		} else {
			return stricmp((*a)->pchPlayerName, (*b)->pchPlayerName);
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionFillScoreboardList");
void exprOpenMissionFillScoreboardList(ExprContext *pContext, SA_PARAM_OP_VALID OpenMission *pOpenMission)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	if (pGen){
		OpenMissionScoreEntry*** peaScoreEntires = ui_GenGetManagedListSafe(pGen, OpenMissionScoreEntry);
		eaClear(peaScoreEntires);
		if (pOpenMission){
			eaPushEArray(peaScoreEntires, &pOpenMission->eaScores);
			eaQSort((*peaScoreEntires), openmission_CompareScoreEntires);
		}

		// OpenMissionScoreEntrys are owned by pOpenMission.
		ui_GenSetManagedListSafe(pGen, peaScoreEntires, OpenMissionScoreEntry, false);
	}
}


//Fills a list with the top players for an open mission
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionFillScoreboardListTopPlayers");
void exprOpenMissionFillRankedScoreboardList(ExprContext *pContext, SA_PARAM_OP_VALID OpenMission *pOpenMission, SA_PARAM_OP_VALID Entity *pPlayer, bool bIncludeHeaders)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	if (pGen && pPlayer && pPlayer->pPlayer && pPlayer->pPlayer->missionInfo)
	{
		OpenMissionScoreEntry*** peaScoreEntries = ui_GenGetManagedListSafe(pGen, OpenMissionScoreEntry);

		if(pPlayer->pPlayer->missionInfo->pLeaderboardInfo && pPlayer->pPlayer->missionInfo->pLeaderboardInfo->eaLeaders)
		{
			OpenMissionScoreEntry *pHeader = NULL;
			S32 iNumParticipants = pPlayer->pPlayer->missionInfo->pLeaderboardInfo->iTotalParticipants;
			int i;
			int iLastRewardTier = 0;

			eaClearStruct(peaScoreEntries, parse_OpenMissionScoreEntry);
			for (i = 0; i < eaSize(&pPlayer->pPlayer->missionInfo->pLeaderboardInfo->eaLeaders); ++i)
			{
				OpenMissionScoreEntry *pEntry = StructClone(parse_OpenMissionScoreEntry, pPlayer->pPlayer->missionInfo->pLeaderboardInfo->eaLeaders[i]);

				if (pEntry->playerID != entGetContainerID(pPlayer))
				{
					pEntry->iRewardTier = openmission_GetRewardTierForScoreEntry(&pPlayer->pPlayer->missionInfo->pLeaderboardInfo->eaLeaders, i, iNumParticipants);
				}
				else
				{
					pEntry->iRewardTier = pPlayer->pPlayer->missionInfo->pLeaderboardInfo->iRewardTier;
				}

				if (bIncludeHeaders && i > 0 && i < iNumParticipants && pEntry->playerID && pEntry->iRewardTier != iLastRewardTier)
				{
					pHeader = StructCreate(parse_OpenMissionScoreEntry);
					pHeader->bIsHeader = true;
					pHeader->iRewardTier = pEntry->iRewardTier;
					eaPush(peaScoreEntries, pHeader);
				}

				iLastRewardTier = pEntry->iRewardTier;

				eaPush(peaScoreEntries, pEntry);
			}
		}

		ui_GenSetManagedListSafe(pGen, peaScoreEntries, OpenMissionScoreEntry, false);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionScoreEntryGetPoints");
char* exprOpenMissionScoreEntryGetPoints(SA_PARAM_OP_VALID OpenMissionScoreEntry *pScoreEntry)
{
	static char buffer[20];
	int points = pScoreEntry?round(pScoreEntry->fPoints):0;
	sprintf(buffer, "%d", points);
	return buffer;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionScoreEntryGetEntity");
SA_RET_OP_VALID Entity* exprOpenMissionScoreEntryGetEntity(SA_PARAM_OP_VALID OpenMissionScoreEntry *pScoreEntry)
{
	Entity *pEnt = NULL;

	if(pScoreEntry)
	{
		pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pScoreEntry->playerID);
	}

	return pEnt;
}


//Gets the player costume for an open mission score entry for creating headshot
//If the player is on the same game server but not within entity send distance, it will request that the costume be sent
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionScoreEntryGetPlayerCostume");
SA_RET_OP_VALID PlayerCostume* exprOpenMissionScoreEntryGetPlayerCostume(SA_PARAM_OP_VALID OpenMissionScoreEntry *pScoreEntry)
{
	Entity *pEnt = NULL;
	PlayerCostume *pCostume = NULL;

	if(pScoreEntry)
	{
		pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pScoreEntry->playerID);

		if(pEnt)
		{
			pCostume = costumeEntity_GetEffectiveCostume(pEnt);
		}
		else if(pScoreEntry->playerID != 0 )
		{
			//We do the string comparison to make sure we aren't using the wrong costume data
			//This might not be the best way to do this, and may need revisiting ~DHOGBERG 04/03/2012
			if(pScoreEntry->pCostume && stricmp(pScoreEntry->pCostume->pcName, pScoreEntry->pchPlayerName) == 0)
			{
				pCostume = pScoreEntry->pCostume;
			}
			else
			{
				ServerCmd_OpenMissionRequestScoreEntryCostume(pScoreEntry->playerID);
			}
		}
	}

	return pCostume;
}

//Sets a costume for a specified entity on the leaderboard
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void OpenMissionSetScoreEntryCostume(Entity *pPlayerEnt, ContainerID scorePlayerID, PlayerCostume *pCostume)
{
	if(pPlayerEnt && pCostume)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		if(pInfo)
		{
			OpenMissionLeaderboardInfo *pLeaderboardInfo = pInfo->pLeaderboardInfo;
			if (pLeaderboardInfo && pLeaderboardInfo->eaLeaders)
			{
				int i;

				for(i = 0; i < eaSize(&pLeaderboardInfo->eaLeaders); ++i)
				{
					OpenMissionScoreEntry *pEntry = pLeaderboardInfo->eaLeaders[i];
					if(pEntry && pEntry->playerID == scorePlayerID)
					{
						pEntry->pCostume = StructClone(parse_PlayerCostume, pCostume);
						return;
					}
				}
			}
		}
	}
}


//Returns true if a contest has ended and the leaderboard should be shown
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionSetShouldShowLeaderboard");
void exprOpenMissionSetShouldShowLeaderboard(bool bShowLeaderboard)
{
	s_bShouldShowLeaderboard = bShowLeaderboard;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionGetShouldShowLeaderboard");
bool exprOpenMissionGetShouldShowLeaderboard()
{
	return s_bShouldShowLeaderboard;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void OpenMissionShowLeaderboard(Entity *pPlayerEnt)
{
	s_bShouldShowLeaderboard = true;
}

//Returns the total participants of an open mission
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionGetTotalParticipants");
S32 exprOpenMissionGetTotalParticipants(SA_PARAM_OP_VALID OpenMission *pOpenMission)
{
	if(pOpenMission)
	{
		return eaSize(&pOpenMission->eaScores);
	}

	return 0;
}

//Returns the name of the open mission being tracked by a player's leaderboard
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionLeaderboardGetDisplayName");
SA_RET_OP_VALID char *exprOpenMissionLeaderboardGetDisplayName(SA_PARAM_OP_VALID Entity *pPlayerEnt)
{
	if(pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->missionInfo && pPlayerEnt->pPlayer->missionInfo->pLeaderboardInfo)
	{
		return NULL_TO_EMPTY(pPlayerEnt->pPlayer->missionInfo->pLeaderboardInfo->pOpenMissionDisplayName);
	}

	return "";
}

//Returns the number of participants on a player's leaderboard
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionLeaderboardGetTotalParticipants");
S32 exprOpenMissionLeaderboardGetTotalParticipants(SA_PARAM_OP_VALID Entity *pPlayerEnt)
{
	if(pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->missionInfo && pPlayerEnt->pPlayer->missionInfo->pLeaderboardInfo)
	{
		return pPlayerEnt->pPlayer->missionInfo->pLeaderboardInfo->iTotalParticipants;
	}

	return 0;
}

//Returns the number corresponding to the player's open mission reward tier
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionLeaderboardGetRewardTier");
S32 exprOpenMissionLeaderboardGetRewardTier(SA_PARAM_OP_VALID Entity *pPlayerEnt)
{
	if(pPlayerEnt)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);

		if(pInfo && pInfo->pLeaderboardInfo)
		{
			return pInfo->pLeaderboardInfo->iRewardTier;
		}
	}

	return 4;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionGetEventIsActive");
bool exprOpenMissionGetEventIsActive(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID OpenMission *pOpenMission)
{
	if (pPlayerEnt)
	{
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);

		if (pMissionInfo)
		{
			OpenMissionLeaderboardInfo *pLeaderboardInfo = pMissionInfo->pLeaderboardInfo;

			if (pLeaderboardInfo && pOpenMission && pOpenMission->pMission)
			{
				MissionDef *pDef = mission_GetDef(pOpenMission->pMission);

				if (pDef && timeSecondsSince2000() % OPEN_MISSION_EVENT_REQUEST_TIME == 0)
				{
					ServerCmd_OpenMissionRequestIsEventActive(pDef->pchRelatedEvent);
				}

				return pLeaderboardInfo->bIsRelatedEventActive;
			}
		}
	}
	return false;
}

//Returns the short description for the event on the leaderboard
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionLeaderboardGetEventShortDesc");
const char * exprOpenMissionLeaderboardGetEventShortDesc(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID OpenMission *pOpenMission)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer)
	{
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);

		if (pMissionInfo)
		{
			OpenMissionLeaderboardInfo *pLeaderboardInfo = pMissionInfo->pLeaderboardInfo;

			if (pLeaderboardInfo && pOpenMission && pOpenMission->pMission)
			{
				MissionDef *pDef = mission_GetDef(pOpenMission->pMission);

				if (pDef)
				{
					EventDef *pEventDef = NULL;
					SET_HANDLE_FROM_STRING(g_hEventDictionary, pDef->pchRelatedEvent, pLeaderboardInfo->hRelatedEvent);

					pEventDef = GET_REF(pLeaderboardInfo->hRelatedEvent);

					if(pEventDef && exprOpenMissionGetEventIsActive(pPlayerEnt, pOpenMission))
					{
						return NULL_TO_EMPTY(TranslateDisplayMessage(pEventDef->msgDisplayShortDesc));
					}
				}
			}
		}
	}
	return "";
}

//Returns the long description for the event on the leaderboard
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionLeaderboardGetEventLongDesc");
const char * exprOpenMissionLeaderboardGetEventLongDesc(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID OpenMission *pOpenMission)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer)
	{
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);

		if (pMissionInfo)
		{
			OpenMissionLeaderboardInfo *pLeaderboardInfo = pMissionInfo->pLeaderboardInfo;

			if (pLeaderboardInfo && pOpenMission && pOpenMission->pMission)
			{
				MissionDef *pDef = mission_GetDef(pOpenMission->pMission);

				if (pDef)
				{
					EventDef *pEventDef = NULL;
					SET_HANDLE_FROM_STRING(g_hEventDictionary, pDef->pchRelatedEvent, pLeaderboardInfo->hRelatedEvent);

					pEventDef = GET_REF(pLeaderboardInfo->hRelatedEvent);

					if(pEventDef && exprOpenMissionGetEventIsActive(pPlayerEnt, pOpenMission))
					{
						return NULL_TO_EMPTY(TranslateDisplayMessage(pEventDef->msgDisplayLongDesc));
					}
				}
			}
		}
	}
	return "";
}

//Returns the background texture for the event on the leaderboard
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionLeaderboardGetEventBackground");
const char * exprOpenMissionLeaderboardGetEventBackground(SA_PARAM_OP_VALID Entity *pPlayerEnt)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer)
	{
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);

		if (pMissionInfo)
		{
			OpenMissionLeaderboardInfo *pLeaderboardInfo = pMissionInfo->pLeaderboardInfo;

			if (pLeaderboardInfo && pLeaderboardInfo->pchRelatedEvent)
			{
				EventDef *pEventDef = NULL;
				SET_HANDLE_FROM_STRING(g_hEventDictionary, pLeaderboardInfo->pchRelatedEvent, pLeaderboardInfo->hRelatedEvent);

				pEventDef = GET_REF(pLeaderboardInfo->hRelatedEvent);

				if(pEventDef)
				{
					return NULL_TO_EMPTY(pEventDef->pchBackground);
				}
			}
		}
	}
	return "";
}

//Returns true if an OpenMissionScoreEntry is the player
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionScoreEntryIsPlayer");
bool exprOpenMissionScoreEntryIsPlayer(SA_PARAM_OP_VALID OpenMissionScoreEntry *pEntry, SA_PARAM_OP_VALID Entity *pPlayerEnt)
{
	if(pEntry && pPlayerEnt)
	{
		return pPlayerEnt->myContainerID == pEntry->playerID;
	}

	return false;
}

// If pOpenMission has a related mission and it is pMission, this will return true.
// If pOpenMission does not have a related mission, but has an objective on the current map, this will return true.
// All other cases return false.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsMissionRelatedToOpenMission");
bool exprIsMissionRelatedToOpenMission(ExprContext *pContext, SA_PARAM_NN_VALID Mission* pOpenMission, SA_PARAM_NN_VALID Mission* pMission)
{
	int i;
	if(pOpenMission && pMission) {
		MissionDef *pDef = mission_GetDef(pMission);
		MissionDef *pOpenDef = mission_GetDef(pOpenMission);

		if (pOpenDef) {
			if(pDef) {
				if(pDef->pcRelatedMission) {
					if (stricmp(pOpenDef->name, pDef->pcRelatedMission) == 0) return true;
				}
			}
			for (i = eaSize(&pMission->children)-1; i >= 0; --i)
			{
				pDef = mission_GetDef(pMission->children[i]);
				if (!pDef) continue;
				if(pDef->pcRelatedMission) {
					if (stricmp(pOpenDef->name, pDef->pcRelatedMission) == 0) return true;
				}
			}
		}
	}

	return false;
}

//Returns true if the passed player's leaderboard is from an open mission that has a related event that is the same as the event name parameter
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsEventRelatedToLeaderboard");
bool exprIsEventRelatedToLeaderboard(SA_PARAM_NN_VALID Entity *pPlayerEnt, SA_PARAM_NN_VALID const char *pchEventName)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->missionInfo && pPlayerEnt->pPlayer->missionInfo->pLeaderboardInfo)
	{
		return pPlayerEnt->pPlayer->missionInfo->pLeaderboardInfo->pchRelatedEvent == pchEventName;
	}

	return false;
}

//Returns the number of points for the passed player entity if that player has an entry on their current leaderboard
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetOpenMissionLeaderboardPlayerPoints");
int exprOpenMissionLeaderboardGetPlayerPoints(SA_PARAM_OP_VALID Entity *pPlayerEnt)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->missionInfo && pPlayerEnt->pPlayer->missionInfo->pLeaderboardInfo)
	{
		OpenMissionLeaderboardInfo *pInfo = pPlayerEnt->pPlayer->missionInfo->pLeaderboardInfo;
		
		if(pInfo->iPlayerIndex >= 0 && pInfo->iPlayerIndex < eaSize(&pInfo->eaLeaders))
		{
			return round(pInfo->eaLeaders[pInfo->iPlayerIndex] &&
				pInfo->eaLeaders[pInfo->iPlayerIndex]->playerID == pPlayerEnt->myContainerID ? pInfo->eaLeaders[pInfo->iPlayerIndex]->fPoints : 0);
		}
	}

	return 0;
}

//Returns the leaderboard rank for the passed player entity if that player has an entry on their current leaderboard
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetOpenMissionLeaderboardPlayerRank");
int exprOpenMissionLeaderboardGetRank(SA_PARAM_OP_VALID Entity *pPlayerEnt)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->missionInfo && pPlayerEnt->pPlayer->missionInfo->pLeaderboardInfo)
	{
		OpenMissionLeaderboardInfo *pInfo = pPlayerEnt->pPlayer->missionInfo->pLeaderboardInfo;

		if(pInfo->iPlayerIndex >=0 && pInfo->iPlayerIndex < eaSize(&pInfo->eaLeaders))
		{
			return (pInfo->eaLeaders[pInfo->iPlayerIndex] && pInfo->eaLeaders[pInfo->iPlayerIndex]->playerID == pPlayerEnt->myContainerID ? pInfo->eaLeaders[pInfo->iPlayerIndex]->iRank : 0);
		}
	}

	return 0;
}

//Returns true if the player has recieved any open mission rewards
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PlayerReceivedOpenMissionRewards");
bool exprPlayerReceivedOpenMissionRewards(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt)
{
	return (eaSize(&s_eaOpenMissionRewards) > 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetOpenMissionRewardList");
void exprGetOpenMissionRewardList(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt)
{
	MissionInfo *pInfo = pEnt? mission_GetInfoFromPlayer(pEnt) : NULL;
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);

	if(pInfo && eaSize(&s_eaOpenMissionRewards))
	{
		ui_GenSetList(pGen, &s_eaOpenMissionRewards, parse_Item);
	} else {
		ui_GenSetList(pGen, NULL, NULL);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ClearOpenMissionRewardList");
void exprClearOpenMissionRewardList(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt)
{
	if(eaSize(&s_eaOpenMissionRewards)) {
		eaDestroyStruct(&s_eaOpenMissionRewards, parse_Item);
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void AddOpenMissionRewardNumeric(SA_PARAM_NN_VALID const char *pchItemDef, S32 iNumericValue)
{
	Entity* pEnt = entActivePlayerPtr();
	NOCONST(Item)* pItem = NULL;

	if (pEnt && pchItemDef) {
		pItem = CONTAINER_NOCONST(Item, item_FromEnt(CONTAINER_NOCONST(Entity, pEnt),pchItemDef,0,NULL,0));
	}
	if(pItem) {
		pItem->count = iNumericValue;
		eaPush(&s_eaOpenMissionRewards, (Item*)pItem);
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void AddOpenMissionRewardItem(Item *pItem)
{
	if(pItem) {
		Item* pItemCopy = StructClone(parse_Item, pItem);
		item_trh_FixupPowers(CONTAINER_NOCONST(Item, pItemCopy));
		eaPush(&s_eaOpenMissionRewards, pItemCopy);
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void OpenMissionLeaderboardSetEventIsActive(Entity *pPlayerEnt, bool bIsActive)
{
	if (pPlayerEnt)
	{
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);

		if (pMissionInfo)
		{
			OpenMissionLeaderboardInfo *pLeaderboardInfo = pMissionInfo->pLeaderboardInfo;

			if (pLeaderboardInfo)
			{
				pLeaderboardInfo->bIsRelatedEventActive = bIsActive;
			}
		}
	}
}

extern U32 gclActivity_EventClock_GetSecondsSince2000();

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("OpenMissionGetEventTimeRemaining");
U32 exprOpenMissionGetRemainingEventTime(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID OpenMission *pOpenMission)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer)
	{
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);

		if (pMissionInfo)
		{
			OpenMissionLeaderboardInfo *pLeaderboardInfo = pMissionInfo->pLeaderboardInfo;

			if (pLeaderboardInfo && pOpenMission && pOpenMission->pMission)
			{
				MissionDef *pDef = mission_GetDef(pOpenMission->pMission);

				if (pDef)
				{
					EventDef *pEventDef = NULL;
					SET_HANDLE_FROM_STRING(g_hEventDictionary, pDef->pchRelatedEvent, pLeaderboardInfo->hRelatedEvent);

					pEventDef = GET_REF(pLeaderboardInfo->hRelatedEvent);

					if(pEventDef && exprOpenMissionGetEventIsActive(pPlayerEnt, pOpenMission))
					{
						U32 uLastStart = 0;
						U32 uEndOfLastStart = 0;
						U32 uNextStart;
						U32 uEventClockTime = gclActivity_EventClock_GetSecondsSince2000();

						// This only pays attention to the actual def and does not take into consideration the iRunMode of the EventDef. It is up
						//  to the calling function to deal appropriately with the different run modes. The monitor display in aslMapManagerActivity pays
						//  appropriate attention to the runMode e.g.
						
						ShardEventTiming_GetUsefulTimes(&(pEventDef->ShardTimingDef), uEventClockTime, &uLastStart, &uEndOfLastStart, &uNextStart);

						if (uEventClockTime < uEndOfLastStart)
						{
							// It's active. 
							// This may eventually need to pay attention to the RunMode of the EventDef, though we currently do not
							//   pass the run mode through to the client. WOLF[21Aug12]
							U32 uTimeRemaining = uEndOfLastStart - uEventClockTime;
							return uTimeRemaining;
						}
					}
				}
			}
		}
	}

	return 0;
}


// ------------------------------------------------------------------
//  Crime Computer expressions
// ------------------------------------------------------------------

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PlayerUseCrimeComputer");
void exprPlayerUseCrimeComputer(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity){
		ServerCmd_player_UseCrimeComputer();
	}
}


// ------------------------------------------------------------------
// Logout timer UI.  Not technically part of mission UI
// ------------------------------------------------------------------

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("LogoutTimerType");
U32 exprLogoutTimerType(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pPlayerEnt)
{
	if(pPlayerEnt)
	{
		if(!pPlayerEnt->pPlayer)
		{
			ErrorFilenamef(exprContextGetBlameFile(pContext), "LogoutTimerType: Non-player entity passed in.");
			return LogoutTimerType_None;
		}

		if(!pPlayerEnt->pPlayer->pLogoutTimer)
			return LogoutTimerType_None;

		return pPlayerEnt->pPlayer->pLogoutTimer->eType;
	}
	return LogoutTimerType_None;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("LogoutTimerTimeRemaining");
U32 exprLogoutTimerTimeRemaining(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pPlayerEnt)
{
	if(pPlayerEnt)
	{
		if(!pPlayerEnt->pPlayer)
		{
			ErrorFilenamef(exprContextGetBlameFile(pContext), "LogoutTimerType: Non-player entity passed in.");
			return 0;
		}

		if(!pPlayerEnt->pPlayer->pLogoutTimer)
			return 0;

		return pPlayerEnt->pPlayer->pLogoutTimer->timeRemaining;
	}
	return 0;
}


// ------------------------------------------------------------------
// Cooldown timer expressions for when you drop missions
// ------------------------------------------------------------------

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionCooldownTimerRemaining");
S32 exprMissionCooldownTimerRemaining(SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_OP_VALID Mission *pMission)
{
	MissionDef *pDef = mission_GetDef(pMission);
	if (pEnt && pMission && pDef)
	{
		CONST_EARRAY_OF(MissionCooldown) *peaCooldowns = SAFE_MEMBER2(pEnt, pPlayer, missionInfo) ? &pEnt->pPlayer->missionInfo->eaMissionCooldowns : NULL;
		const MissionCooldown *pCooldown = eaIndexedGetUsingString(peaCooldowns, pMission->missionNameOrig);
		S32 iCooldownTimer = pDef->fRepeatCooldownHours*60*60;
		if (pCooldown)
		{
			if(pCooldown->iRepeatCooldownCount< pDef->iRepeatCooldownCount)
			{
				// not in cooldown yet
				return 0;
			}
			else
			{
				S32 iCooldownTimerFromStart = MAX(0, (pDef->fRepeatCooldownHoursFromStart*60*60) - (pCooldown->completedTime - timeSecondsSince2000()));
				return MAX(iCooldownTimer, iCooldownTimerFromStart);
			}
		}
		return iCooldownTimer;
	}
	return 0;

}

void exprFillMissionListFromRefs(UIGen *pGen, Entity *pEnt, MissionDefRef **eaMissionDefRefs, S32 maxMissions, const char *pchExcludeTag)
{
	MissionJournalRow*** peaMissionRowList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	S32 iExcludeTag = pchExcludeTag ? StaticDefineIntGetInt(MissionTagEnum, pchExcludeTag) : -1;
	S32 numValidMissions = 0;
	if (!pInfo)
		return;


	FOR_EACH_IN_EARRAY_FORWARDS(eaMissionDefRefs, MissionDefRef, pMissionDefRef)
	{
		MissionJournalRow *pMissionJournalRow = NULL;
		CompletedMission *pCompletedMission;
		MissionDef *pMissionDef;

		if (numValidMissions >= maxMissions)
			break;

		pMissionDef = GET_REF(pMissionDefRef->hMission);
		if (!pMissionDef)
			continue;

		if (iExcludeTag >= 0 && eaiFind(&pMissionDef->peMissionTags, iExcludeTag) >= 0)
			continue;

		// find the completed mission on the entity
		pCompletedMission = mission_GetCompletedMissionByDef(pInfo, pMissionDef);
		if (!pCompletedMission)
			continue;

		pMissionJournalRow = eaGetStruct(peaMissionRowList, parse_MissionJournalRow, numValidMissions++);
		PopulateRowFromCompletedMission(pMissionJournalRow, pCompletedMission, pEnt);
	}
	FOR_EACH_END

	eaSetSizeStruct(peaMissionRowList, parse_MissionJournalRow, numValidMissions);
	ui_GenSetManagedListSafe(pGen, peaMissionRowList, MissionJournalRow, true);
}


static void mission_GetMissions(Mission **eaMissions, MissionInfo *pInfo,
								SA_PARAM_OP_STR const char *pchCategoryName, S32 iExcludeTag, F32 percentThreshold,
								Mission ***peaMissionsOut)
{
	// go through all the missions and find the ones we are interested in.
	FOR_EACH_IN_EARRAY(eaMissions, Mission, pMission)
	{
		MissionDef *pDef;
		F32 percentComplete = pMission->target ? ((F32)pMission->count / (F32)pMission->target) : 0.f;
		const char *pchDefCategory;

		if (percentComplete < percentThreshold)
			continue;

		pDef = mission_GetDef(pMission);
		pchDefCategory = missiondef_GetTranslatedCategoryName(pDef);
		if (pDef
			&& pDef->missionType == MissionType_Perk
			&& missiondef_HasDisplayName(pDef)
			&& (!pchCategoryName || pchCategoryName[0] == '\0' || !stricmp(pchCategoryName, pchDefCategory))
			&& (iExcludeTag == -1 || eaiFind(&pDef->peMissionTags, iExcludeTag) < 0)
			)
		{
			eaPush(peaMissionsOut, pMission);
		}
	}
	FOR_EACH_END
}

static int missionjournalrow_SortByPercentComplete(const MissionJournalRow** a, const MissionJournalRow** b)
{
	if (a && *a && b && *b)
	{
		// Open missions should be sorted after the personal mission
		F32 percentA = (*a)->iTarget ? (F32)(*a)->iCount/(F32)(*a)->iTarget : 0.f;
		F32 percentB = (*b)->iTarget ? (F32)(*b)->iCount/(F32)(*b)->iTarget : 0.f;

		if (percentA == percentB)
			return *a > *b ? -1 : 1;
		return percentA > percentB ? -1 : 1;
	}
	return 0;
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetNearlyCompletedPerks");
void exprGetNearlyCompletedMissions(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity,
									SA_PARAM_OP_STR const char *pchCategoryName, SA_PARAM_OP_STR const char *pchExcludeTag,
									F32 fPercentThreshold, S32 maxMissions)
{
	static Mission **s_pMissions = NULL;
	MissionJournalRow*** peaMissionRowList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);
	S32 numValidMissions = 0;

	eaClear(&s_pMissions);

	if (pInfo && maxMissions)
	{
		S32 iExcludeTag = pchExcludeTag ? StaticDefineIntGetInt(MissionTagEnum, pchExcludeTag) : -1;

		fPercentThreshold = fPercentThreshold / 100.f;

		mission_GetMissions((Mission**)pInfo->missions, pInfo, pchCategoryName, iExcludeTag, fPercentThreshold, &s_pMissions);
		mission_GetMissions(pInfo->eaDiscoveredMissions, pInfo, pchCategoryName, iExcludeTag, fPercentThreshold, &s_pMissions);

		eaQSort(s_pMissions, missionjournalrow_SortByPercentComplete);

		// get the first
		FOR_EACH_IN_EARRAY_FORWARDS(s_pMissions, Mission, pMission)
		{
			MissionJournalRow *pMissionJournalRow = eaGetStruct(peaMissionRowList, parse_MissionJournalRow, numValidMissions++);
			PopulateRowFromMission(pMissionJournalRow, pMission, pEntity);
			if (numValidMissions >= maxMissions)
				break;
		}
		FOR_EACH_END
	}

	eaSetSizeStruct(peaMissionRowList, parse_MissionJournalRow, numValidMissions);
	ui_GenSetManagedListSafe(pGen, peaMissionRowList, MissionJournalRow, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetProgressionArtFileName");
const char * exprMissionGetProgressionArtFileName(SA_PARAM_OP_STR const char *pchMissionName)
{
	Entity* pEnt = entActivePlayerPtr();
	GameProgressionNodeDef *pNode = progression_GetNodeFromMissionName(pEnt, pchMissionName, NULL);
	if (pNode && pNode->pchArtFileName && pNode->pchArtFileName[0])
	{
		return pNode->pchArtFileName;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetNthParentProgressionNode");
SA_RET_OP_VALID GameProgressionNodeDef * exprMissionGetNthParentProgressionNode(SA_PARAM_OP_STR const char *pchMissionName, U32 iLevel)
{
	Entity* pEnt = entActivePlayerPtr();
	GameProgressionNodeDef * pNode = progression_GetNodeFromMissionName(pEnt, pchMissionName, NULL);

	U32 iCurrentLevel = 0;

	while (pNode && iCurrentLevel < iLevel)
	{
		pNode = GET_REF(pNode->hParent);
		iCurrentLevel++;
	}

	return pNode;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetFirstLevelParentProgressionNode", "MissionGetParentProgressionNode");
SA_RET_OP_VALID GameProgressionNodeDef * exprMissionGetParentProgressionNode(SA_PARAM_OP_STR const char *pchMissionName)
{
	return exprMissionGetNthParentProgressionNode(pchMissionName, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetSecondLevelParentProgressionNode", "MissionGetGrandParentProgressionNode");
SA_RET_OP_VALID GameProgressionNodeDef * exprMissionGetGrandParentProgressionNode(SA_PARAM_OP_STR const char *pchMissionName)
{
	return exprMissionGetNthParentProgressionNode(pchMissionName, 1);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetThirdLevelParentProgressionNode", "MissionGetGreatGrandParentProgressionNode");
SA_RET_OP_VALID GameProgressionNodeDef * exprMissionGetGreatGrandParentProgressionNode(SA_PARAM_OP_STR const char *pchMissionName)
{
	return exprMissionGetNthParentProgressionNode(pchMissionName, 2);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionIsLinkedWithAProgressionNode");
bool exprMissionIsLinkedWithAProgressionNode(SA_PARAM_OP_STR const char *pchMissionName)
{
	Entity* pEnt = entActivePlayerPtr();
	return progression_GetNodeFromMissionName(pEnt, pchMissionName, NULL) != NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionIsLinkedWithCurrentProgressionNode");
bool exprMissionIsLinkedWithCurrentProgressionNode(SA_PARAM_OP_STR const char *pchMissionName, SA_PARAM_OP_VALID Entity *pEnt, bool bMustBeRequiredMission)
{
	GameProgressionMission* pProgMission = NULL;
	GameProgressionNodeDef* pNodeDef = progression_GetNodeFromMissionName(pEnt, pchMissionName, &pProgMission);
	GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pNodeDef);
	bool bIsRequired = pProgMission && !progression_IsMissionOptional(pEnt, pProgMission);

	if (pEnt && pBranchNodeDef && pNodeDef == progression_GetCurrentProgress(pEnt, pBranchNodeDef) && (bIsRequired || !bMustBeRequiredMission))
	{
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionIsFirstRequiredMissionInProgressionNode");
bool exprMissionIsFirstRequiredMissionInProgressionNode(SA_PARAM_OP_STR const char *pchMissionName)
{
	Entity* pEnt = entActivePlayerPtr();
	GameProgressionMission *pProgMission = NULL;
	GameProgressionNodeDef *pNode = progression_GetNodeFromMissionName(pEnt, pchMissionName, &pProgMission);
	bool bIsRequired = pProgMission && !progression_IsMissionOptional(pEnt, pProgMission);

	if (bIsRequired && pNode)
	{
		const char* pchFirstRequiredMissionName = NULL;
		int i;
		for (i = 0; i < eaSize(&pNode->pMissionGroupInfo->eaMissions); i++)
		{
			GameProgressionMission* pCheckProgMission = pNode->pMissionGroupInfo->eaMissions[i];
			if (!progression_IsMissionOptional(pEnt, pCheckProgMission))
			{
				pchFirstRequiredMissionName = pCheckProgMission->pchMissionName;
				break;
			}
		}
		if (pchFirstRequiredMissionName && stricmp_safe(pchFirstRequiredMissionName, pchMissionName) == 0)
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionJournalListFilterByLinkToProgressionNode");
void exprMissionJournalListFilterByLinkToProgressionNode(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, bool bLinkedWithAProgressionNode)
{
	MissionJournalRow ***peaList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);

	int i, n = eaSize(peaList);
	for (i = n-1; i >= 0; --i)
	{
		MissionDef *def = NULL;
		if ((*peaList)[i]->type == MissionJournalRowType_Mission)
		{
			Mission *mission = (*peaList)[i]->mission;
			def = mission_GetDef(mission);
		}
		else if ((*peaList)[i]->type == MissionJournalRowType_CompletedMission)
		{
			CompletedMission *mission = (*peaList)[i]->pCompletedMission;
			def = GET_REF(mission->def);
		}

		if (def)
		{
			bool bLinked = exprMissionIsLinkedWithAProgressionNode(def->name);
			if ((bLinkedWithAProgressionNode && !bLinked) || (!bLinkedWithAProgressionNode && bLinked))
				StructDestroy(parse_MissionJournalRow, eaRemove(peaList, i));
		}
	}
	ui_GenSetListSafe(pGen, peaList, MissionJournalRow);
}

AUTO_EXPR_FUNC(UIGen) ACMD_ACCESSLEVEL(0) ACMD_NAME("MissionSetProgression");
void exprMissionSetProgression(SA_PARAM_OP_STR const char *pchMissionName, bool bWarpToSpawnPoint)
{
	if (pchMissionName && exprMissionIsFirstRequiredMissionInProgressionNode(pchMissionName))
	{
		if (g_GameProgressionConfig.bAllowReplay)
		{
			ServerCmd_SetProgressionByMission(pchMissionName, bWarpToSpawnPoint);
		}
	}
}

static void mission_FillMissionJournalListByGameProgressionNodeMissions(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID MissionInfo *pInfo,
	SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_VALID GameProgressionNodeDef *pProgNode, SA_PARAM_NN_VALID S32 *piCount, bool bIncludeMissionsNotEncounteredByPlayer)
{
	MissionJournalRow ***peaJournalRowList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	int i;
	const char** ppchRequestMissionDefs = NULL;

	for (i = 0; i < eaSize(&pProgNode->pMissionGroupInfo->eaMissions); i++)
	{
		GameProgressionMission *pProgMission = pProgNode->pMissionGroupInfo->eaMissions[i];
		CompletedMission *pCompletedMission;
		bool bVisible = false;

		if (pProgMission->pExprVisible)
		{
			CachedMissionData* pData = eaIndexedGetUsingString(&s_eaCachedMissionData, pProgMission->pchMissionName);
			if (pData && pData->bVisible)
			{
				bVisible = true;
			}
			eaPush(&ppchRequestMissionDefs, pProgMission->pchMissionName);
		}
		else
		{
			bVisible = true;
		}

		if (!bVisible)
		{
			continue;
		}
		pCompletedMission = eaIndexedGetUsingString(&pInfo->completedMissions, pProgMission->pchMissionName);
		eaPush(&ppchRequestMissionDefs, pProgMission->pchMissionName);

		if (pCompletedMission && !pCompletedMission->bHidden)
		{
			MissionJournalRow *pRow = eaGetStruct(peaJournalRowList, parse_MissionJournalRow, (*piCount)++);
			PopulateRowFromCompletedProgressionMission(pRow, pCompletedMission, pProgNode, i, pEntity);
		}
		else
		{
			Mission *pMission = mission_FindMissionFromRefString(pInfo, pProgMission->pchMissionName);

			if (pMission || bIncludeMissionsNotEncounteredByPlayer)
			{
				MissionDef* pMissionDef = mission_GetDef(pMission);
				
				MissionJournalRow *pRow = eaGetStruct(peaJournalRowList, parse_MissionJournalRow, (*piCount)++);

				PopulateRowFromProgressionMission(pRow, pMission, pProgNode, i, pEntity);

				if (pMissionDef)
				{
					pRow->pchTranslatedCategoryName = missiondef_GetTranslatedCategoryName(pMissionDef);
				}
				else
				{
					pRow->pchTranslatedCategoryName = NULL;
				}
			}
		}
	}
	RequestMissionDisplayData(pEntity, ppchRequestMissionDefs);
	eaDestroy(&ppchRequestMissionDefs);
	ui_GenSetManagedListSafe(pGen, peaJournalRowList, MissionJournalRow, true);
}

typedef enum MissionsSelection {
	// Only add the first incomplete, (available or in-progress) missions in unlocked mission groups
	kMissionSelection_HeadNodes,
	// Only add completed mission nodes
	kMissionSelection_Completed
} MissionsSelection;

// Get the list of mission nodes in various chains that are not yet completed.
static void GameProgressionNode_GetMissions(SA_PARAM_NN_VALID MissionInfo *pInfo, 
											SA_PARAM_OP_VALID Entity *pEntity,
											SA_PARAM_NN_VALID GameProgressionNodeDef *pProgNode,
											SA_PARAM_NN_VALID S32 *piCount,
											MissionsSelection eSelection,
											MissionJournalRow ***peaJournalRowList,
											const char ***peaRequestMissionDefs)
{
	const char **eaRequestMissionDefs = NULL;
	S32 i;

	if (!peaRequestMissionDefs)
		peaRequestMissionDefs = &eaRequestMissionDefs;

	if (pProgNode->eFunctionalType == GameProgressionNodeFunctionalType_MissionGroup && pProgNode->pMissionGroupInfo)
	{
		bool bAdded = false;

		for (i = 0; i < eaSize(&pProgNode->pMissionGroupInfo->eaMissions); i++)
		{
			GameProgressionMission *pProgMission = pProgNode->pMissionGroupInfo->eaMissions[i];
			CachedMissionData* pData = eaIndexedGetUsingString(&s_eaCachedMissionData, pProgMission->pchMissionName);
			CompletedMission *pCompletedMission;
			Mission *pMission;
			MissionDef *pMissionDef;
			MissionJournalRow *pRow;

			pCompletedMission = eaIndexedGetUsingString(&pInfo->completedMissions, pProgMission->pchMissionName);
			if (pCompletedMission)
			{
				if (pProgMission->pExprVisible)
				{
					eaPushUnique(peaRequestMissionDefs, pProgMission->pchMissionName);
				}
				if (eSelection == kMissionSelection_Completed)
				{
					pRow = eaGetStruct(peaJournalRowList, parse_MissionJournalRow, (*piCount)++);
					PopulateRowFromCompletedProgressionMission(pRow, pCompletedMission, pProgNode, i, pEntity);
				}
				continue;
			}

			if (!pData || pData->eCreditType == MissionCreditType_Ineligible)
			{
				eaPushUnique(peaRequestMissionDefs, pProgMission->pchMissionName);
				continue;
			}

			if (pProgMission->pExprVisible && !pData->bVisible)
			{
				eaPushUnique(peaRequestMissionDefs, pProgMission->pchMissionName);
				continue;
			}

			pMission = mission_FindMissionFromRefString(pInfo, pProgMission->pchMissionName);
			if (!pMission && !pData->bAvailable)
			{
				eaPushUnique(peaRequestMissionDefs, pProgMission->pchMissionName);
				continue;
			}

			pMissionDef = mission_GetDef(pMission);

			// Only add the first available/in-progress mission
			if (!bAdded && eSelection == kMissionSelection_HeadNodes)
			{
				pRow = eaGetStruct(peaJournalRowList, parse_MissionJournalRow, (*piCount)++);
				PopulateRowFromProgressionMission(pRow, pMission, pProgNode, i, pEntity);
				pRow->pchTranslatedCategoryName = pMissionDef ? missiondef_GetTranslatedCategoryName(pMissionDef) : NULL;
				bAdded = true;
			}

			if (!pMissionDef)
				eaPushUnique(peaRequestMissionDefs, pProgMission->pchMissionName);
		}
	}
	else if (pProgNode->eFunctionalType == GameProgressionNodeFunctionalType_StoryRoot
		|| pProgNode->eFunctionalType == GameProgressionNodeFunctionalType_StoryGroup)
	{
		for (i = 0; i < eaSize(&pProgNode->eaChildren); i++)
		{
			GameProgressionNodeDef *pChild = GET_REF(pProgNode->eaChildren[i]->hDef);
			if (pChild && progression_ProgressionNodeUnlocked(pEntity, pChild))
			{
				GameProgressionNode_GetMissions(pInfo, pEntity, pChild, piCount, eSelection, peaJournalRowList, peaRequestMissionDefs);
			}
		}
	}

	if (eaSize(&eaRequestMissionDefs))
	{
		RequestMissionDisplayData(pEntity, eaRequestMissionDefs);
		eaDestroy(&eaRequestMissionDefs);
	}
}

// Fill the gen's model with missions linked to the given game progression node name
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillMissionJournalListByGameProgressionNodeNameEx");
void exprFillMissionJournalListByGameProgressionNodeNameEx(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, const char* pchNodeName, bool bIncludeMissionsNotEncounteredByPlayer)
{
	MissionJournalRow ***peaJournalRowList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);

	S32 iCount = 0;

	if (pchNodeName && pchNodeName[0])
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);

		GameProgressionNodeDef *pActualNode = progression_NodeDefFromName(pchNodeName);

		if (pInfo &&
			pEntity &&
			pActualNode &&
			pActualNode->pMissionGroupInfo)
		{
			mission_FillMissionJournalListByGameProgressionNodeMissions(pGen, pInfo, pEntity, pActualNode, &iCount, bIncludeMissionsNotEncounteredByPlayer);
		}
	}

	eaSetSizeStruct(peaJournalRowList, parse_MissionJournalRow, iCount);

	ui_GenSetManagedListSafe(pGen, peaJournalRowList, MissionJournalRow, true);
}

// Fill the gen's model with missions linked to the given game progression node name
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillMissionJournalListByGameProgressionNodeName");
void exprFillMissionJournalListByGameProgressionNodeName(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, const char* pchNodeName)
{
	exprFillMissionJournalListByGameProgressionNodeNameEx(pContext, pGen, pEntity, pchNodeName, true);
}

// Fill the gen's model with missions linked to the given game progression node
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillMissionJournalListByGameProgressionNode");
void exprFillMissionJournalListByGameProgressionNode(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID GameContentNode *pNode)
{
	const char* pchNodeName = REF_STRING_FROM_HANDLE(pNode->contentRef.hNode);
	exprFillMissionJournalListByGameProgressionNodeNameEx(pContext, pGen, pEntity, pchNodeName, true);
}

// Fill the gen's model with missions linked to the given game progression node
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillMissionJournalListByGameProgressionNodeEx");
void exprFillMissionJournalListByGameProgressionNodeEx(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID GameContentNode *pNode, bool bIncludeMissionsNotEncounteredByPlayer)
{
	const char* pchNodeName = REF_STRING_FROM_HANDLE(pNode->contentRef.hNode);
	exprFillMissionJournalListByGameProgressionNodeNameEx(pContext, pGen, pEntity, pchNodeName, bIncludeMissionsNotEncounteredByPlayer);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetStoryArcMissionGroupList");
void exprGetStoryArcMissionGroupList(SA_PARAM_NN_VALID UIGen *pGen, const char* pchRootNodeDef)
{
	ProgressionMissionGroupData ***peaData = ui_GenGetManagedListSafe(pGen, ProgressionMissionGroupData);
	Entity* pEnt = entActivePlayerPtr();
	S32 iCount = 0;
	GameProgressionNodeDef* pRootNodeDef = progression_NodeDefFromName(pchRootNodeDef);
	GameProgressionNodeDef* pCurrNodeDef = progression_FindLeftMostLeaf(pRootNodeDef);
	GameProgressionNodeDef* pCurrProgressionNodeDef = progression_GetCurrentProgress(pEnt, pRootNodeDef);

	while (pCurrNodeDef)
	{
		ProgressionMissionGroupData* pData = eaGetStruct(peaData, parse_ProgressionMissionGroupData, iCount++);
		pData->bCurrent = (pCurrNodeDef == pCurrProgressionNodeDef);
		pData->bComplete = progression_ProgressionNodeCompleted(pEnt, pCurrNodeDef);
		pData->bMajor = SAFE_MEMBER(pCurrNodeDef->pMissionGroupInfo, bMajor);
		pData->uTimeToComplete = SAFE_MEMBER(pCurrNodeDef->pMissionGroupInfo, uiTimeToComplete);
		SET_HANDLE_FROM_REFERENT("GameProgressionNodeDef", pCurrNodeDef, pData->hNodeDef);
		pData->pchDisplayName = TranslateDisplayMessage(pCurrNodeDef->msgDisplayName);
		pData->pchSummary = TranslateDisplayMessage(pCurrNodeDef->msgSummary);
		pData->pchTeaser = TranslateDisplayMessage(pCurrNodeDef->msgTeaser);
		pData->pchIcon = pCurrNodeDef->pchIcon;
		pData->pchImage = pCurrNodeDef->pchArtFileName;
		pCurrNodeDef = GET_REF(pCurrNodeDef->hNextSibling);
	}
	eaSetSizeStruct(peaData, parse_ProgressionMissionGroupData, iCount);
	ui_GenSetManagedListSafe(pGen, peaData, ProgressionMissionGroupData, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetStoryArcList");
void exprGetStoryArcList(SA_PARAM_NN_VALID UIGen *pGen)
{
	GameProgressionNodeRef ***peaRefs = ui_GenGetManagedListSafe(pGen, GameProgressionNodeRef);
	Entity* pEnt = entActivePlayerPtr();
	S32 i, iCount = 0;

	if (pEnt)
	{
		for (i = 0; i < eaSize(&g_eaStoryArcNodes); i++)
		{
			GameProgressionNodeRef* pNodeRef = g_eaStoryArcNodes[i];
			GameProgressionNodeDef* pNodeDef = GET_REF(pNodeRef->hDef);
			if (progression_IsValidStoryArcForPlayer(pEnt, pNodeDef))
			{
				GameProgressionNodeRef* pRef = eaGetStruct(peaRefs, parse_GameProgressionNodeRef, iCount++);
				COPY_HANDLE(pRef->hDef, pNodeRef->hDef);
			}
		}
	}
	eaSetSizeStruct(peaRefs, parse_GameProgressionNodeRef, iCount);
	ui_GenSetManagedListSafe(pGen, peaRefs, GameProgressionNodeRef, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FillMissionsFromStoryArcHeads");
void exprFillMissionsFromStoryArc(SA_PARAM_NN_VALID UIGen *pGen, const char *pchRootNodeDef, U32 bFillWithCompleteIfNoHeads)
{
	static const char **s_eaRequestMissionDefs = NULL;
	MissionJournalRow ***peaJournalRowList = ui_GenGetManagedListSafe(pGen, MissionJournalRow);
	GameProgressionNodeDef *pRootNodeDef = progression_NodeDefFromName(pchRootNodeDef);
	Entity *pEnt = entActivePlayerPtr();
	MissionInfo *pInfo = SAFE_MEMBER2(pEnt, pPlayer, missionInfo);
	S32 iCount = 0;

	if (!pRootNodeDef || !pInfo)
	{
		eaClearStruct(peaJournalRowList, parse_MissionJournalRow);
		ui_GenSetManagedListSafe(pGen, peaJournalRowList, MissionJournalRow, true);
		return;
	}

	eaClearFast(&s_eaRequestMissionDefs);

	GameProgressionNode_GetMissions(pInfo, pEnt, pRootNodeDef, &iCount, kMissionSelection_HeadNodes, peaJournalRowList, &s_eaRequestMissionDefs);
	if (iCount <= 0 && bFillWithCompleteIfNoHeads)
		GameProgressionNode_GetMissions(pInfo, pEnt, pRootNodeDef, &iCount, kMissionSelection_Completed, peaJournalRowList, &s_eaRequestMissionDefs);

	RequestMissionDisplayData(pEnt, s_eaRequestMissionDefs);

	eaSetSizeStruct(peaJournalRowList, parse_MissionJournalRow, iCount);
	ui_GenSetManagedListSafe(pGen, peaJournalRowList, MissionJournalRow, true);
}

static void mission_UpdateJournalMissionCountByMissionList(SA_PARAM_NN_VALID MissionInfo *pInfo, 
	SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_VALID const GameProgressionMission * const * const * const pppMissions, SA_PARAM_NN_VALID S32 *piCount)
{
	FOR_EACH_IN_CONST_EARRAY_FORWARDS(*pppMissions, GameProgressionMission, pProgMission)
	{
		Mission* pMission = mission_FindMissionFromRefString(pInfo, pProgMission->pchMissionName);
		MissionDef* pMissionDef = mission_GetDef(pMission);
		CompletedMission *pCompletedMission;

		if ((pMission && mission_HasDisplayName(pMission)) ||
			(pMission == NULL && (pCompletedMission = eaIndexedGetUsingString(&pInfo->completedMissions, pProgMission->pchMissionName)) &&
			!pCompletedMission->bHidden &&
			missiondef_HasDisplayName(GET_REF(pCompletedMission->def))))
		{
			++(*piCount);
		}
	}
	FOR_EACH_END
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetJournalMissionCountByGameProgressionNode");
S32 exprGetJournalMissionCountByGameProgressionNode(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID GameContentNode *pNode)
{
	S32 iCount = 0;
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);

	GameProgressionNodeDef *pActualNode = GET_REF(pNode->contentRef.hNode);

	if (pInfo &&
		pEntity &&
		pActualNode &&
		pActualNode->pMissionGroupInfo)
	{
		mission_UpdateJournalMissionCountByMissionList(pInfo, pEntity, &pActualNode->pMissionGroupInfo->eaMissions, &iCount);
	}

	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntMissionCountStates");
S32 exprEntMissionCountStates(SA_PARAM_OP_VALID Entity *pEnt, ACMD_EXPR_ENUM(MissionState) const char *pchExpectedState)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	MissionState eState = StaticDefineIntGetInt(MissionStateEnum, pchExpectedState);
	S32 i, iCount = 0;

	if (pInfo && eState != -1)
	{
		for (i = 0; i < eaSize(&pInfo->missions); ++i)
		{
			if (mission_GetStateForUI(pInfo->missions[i]) == eState)
				iCount++;
		}
		for (i = 0; i < eaSize(&pInfo->eaNonPersistedMissions); ++i)
		{
			if (mission_GetStateForUI(pInfo->eaNonPersistedMissions[i]) == eState)
				iCount++;
		}
		for (i = 0; i < eaSize(&pInfo->eaDiscoveredMissions); ++i)
		{
			if (mission_GetStateForUI(pInfo->eaDiscoveredMissions[i]) == eState)
				iCount++;
		}
	}

	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetMissionListForState");
void exprEntGetMissionListForState(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, ACMD_EXPR_ENUM(MissionState) const char *pchExpectedState)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	MissionState eState = StaticDefineIntGetInt(MissionStateEnum, pchExpectedState);
	MissionListNode ***peaMissionListNodes = ui_GenGetManagedListSafe(pGen, MissionListNode);
	S32 i, iCount = 0;
	Mission *pMission;

	if (pInfo && eState != -1)
	{
		for (i = 0; i < eaSize(&pInfo->missions); ++i)
		{
			if (mission_GetStateForUI(pInfo->missions[i]) == eState)
			{
				pMission = pInfo->missions[i];
				iCount = mission_MakeMissionNodes(iCount, pInfo->missions[i], pEnt, iCount, peaMissionListNodes, pMission->eCreditType != MissionCreditType_Primary, false, false);
			}
		}
		for (i = 0; i < eaSize(&pInfo->eaNonPersistedMissions); ++i)
		{
			if (mission_GetStateForUI(pInfo->eaNonPersistedMissions[i]) == eState)
			{
				pMission = pInfo->eaNonPersistedMissions[i];
				iCount = mission_MakeMissionNodes(iCount, pInfo->missions[i], pEnt, iCount, peaMissionListNodes, pMission->eCreditType != MissionCreditType_Primary, false, false);
			}
		}
		for (i = 0; i < eaSize(&pInfo->eaDiscoveredMissions); ++i)
		{
			if (mission_GetStateForUI(pInfo->eaDiscoveredMissions[i]) == eState)
			{
				pMission = pInfo->eaDiscoveredMissions[i];
				iCount = mission_MakeMissionNodes(iCount, pInfo->missions[i], pEnt, iCount, peaMissionListNodes, pMission->eCreditType != MissionCreditType_Primary, false, false);
			}
		}
	}

	MissionListNodeListSetLength(peaMissionListNodes, iCount);
	ui_GenSetManagedListSafe(pGen, peaMissionListNodes, MissionListNode, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("RemoteContactStartForProgressionData");
void exprRemoteContactStartForProgressionData(SA_PARAM_OP_VALID ProgressionMissionData *pProgressionData)
{
	if (pProgressionData && pProgressionData->pchContact)
	{
		if (pProgressionData->pchContactKey)
		{
			ServerCmd_contact_StartRemoteContactWithOption(pProgressionData->pchContact, pProgressionData->pchContactKey);
		}
		else
		{
			ServerCmd_contact_StartRemoteContactWithMission(pProgressionData->pchContact, pProgressionData->pchMissionDef);
		}
	}
}

static GameProgressionNodeDef* gclMission_FindProgressionNodeForMissionExhaustive(const char* pchMissionName, int* piMissionIndex)
{
	GameProgressionNodeDef* pNodeDef;
	ResourceIterator ResIterator;
	int i;
	resInitIterator(g_hGameProgressionNodeDictionary, &ResIterator);
	while (resIteratorGetNext(&ResIterator, NULL, &pNodeDef))
	{
		if (pNodeDef->pMissionGroupInfo)
		{
			for (i = eaSize(&pNodeDef->pMissionGroupInfo->eaMissions)-1; i >= 0; i--)
			{
				GameProgressionMission* pProgMission = pNodeDef->pMissionGroupInfo->eaMissions[i];
				if (pProgMission->pchMissionName == pchMissionName)
				{
					if (piMissionIndex)
					{
						(*piMissionIndex) = i;
					}
					return pNodeDef;
				}
			}
		}
	}
	resFreeIterator(&ResIterator);
	return NULL;
}

static void gclMission_RemoveOldestMissionProgressionNodeLink(void)
{
	int i;
	int iSelectedIndex = -1;
	U32 uMinTimestamp = 0;
	
	for (i = eaSize(&s_eaMissionProgressionNodeLinks)-1; i >= 0; i--)
	{
		if (!uMinTimestamp || s_eaMissionProgressionNodeLinks[i]->uTimestamp < uMinTimestamp)
		{
			uMinTimestamp = s_eaMissionProgressionNodeLinks[i]->uTimestamp;
			iSelectedIndex = i;
		}
	}
	if (iSelectedIndex >= 0)
	{
		StructDestroy(parse_MissionProgressionNodeLink, eaRemove(&s_eaMissionProgressionNodeLinks, iSelectedIndex));
	}
}


// Attempts to find a GameProgressionMission for a MissionDef that isn't on the client
static bool gclMission_GetProgressionMissionFromMissionNameEx(const char* pchMissionName, GameProgressionNodeDef** ppNodeDef, int* piProgMissionIdx)
{
	MissionProgressionNodeLink* pLink;
	
	pchMissionName = allocFindString(pchMissionName);

	if (!pchMissionName || !pchMissionName[0])
		return false;

	if (!s_eaMissionProgressionNodeLinks)
		eaIndexedEnable(&s_eaMissionProgressionNodeLinks, parse_MissionProgressionNodeLink);

	pLink = eaIndexedGetUsingString(&s_eaMissionProgressionNodeLinks, pchMissionName);
	if (!pLink)
	{
		int iMissionIndex = -1;
		GameProgressionNodeDef* pNodeDef = gclMission_FindProgressionNodeForMissionExhaustive(pchMissionName, &iMissionIndex);

		if (eaSize(&s_eaMissionProgressionNodeLinks) >= MAX_MISSION_PROGRESSION_NODE_LINKS)
		{
			gclMission_RemoveOldestMissionProgressionNodeLink();
		}

		pLink = StructCreate(parse_MissionProgressionNodeLink);
		pLink->pchMissionName = allocAddString(pchMissionName);
		pLink->iMissionIndex = iMissionIndex;
		pLink->uTimestamp = timeSecondsSince2000();
		SET_HANDLE_FROM_REFERENT("GameProgressionNodeDef", pNodeDef, pLink->hNodeDef);
		eaPush(&s_eaMissionProgressionNodeLinks, pLink);
	}
	if (pLink)
	{
		GameProgressionNodeDef* pNodeDef = GET_REF(pLink->hNodeDef);
		if (pNodeDef && pNodeDef->pMissionGroupInfo)
		{
			GameProgressionMission* pProgMission = eaGet(&pNodeDef->pMissionGroupInfo->eaMissions, pLink->iMissionIndex);
			if (!pProgMission || pProgMission->pchMissionName != pchMissionName)
			{
				pLink->iMissionIndex = progression_FindMissionForNode(pNodeDef, pchMissionName);
			}
			if (ppNodeDef)
			{
				(*ppNodeDef) = pNodeDef;
			}
			if (piProgMissionIdx)
			{
				(*piProgMissionIdx) = pLink->iMissionIndex;
			}
			return true;
		}
	}
	return false;
}

static GameProgressionMission* gclMission_GetProgressionMissionFromMissionName(const char* pchMissionName)
{
	int iProgMissionIdx = -1;
	GameProgressionNodeDef* pNodeDef = NULL;
	if (gclMission_GetProgressionMissionFromMissionNameEx(pchMissionName, &pNodeDef, &iProgMissionIdx))
	{
		if (pNodeDef->pMissionGroupInfo)
		{
			return eaGet(&pNodeDef->pMissionGroupInfo->eaMissions, iProgMissionIdx);
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionIsRequiredForProgression");
bool exprMissionIsRequiredForProgression(SA_PARAM_OP_STR const char *pchMissionName)
{
	Entity* pEnt = entActivePlayerPtr();
	GameProgressionMission* pProgMission = NULL;
	MissionDef* pMissionDef = RefSystem_ReferentFromString(g_MissionDictionary, pchMissionName);
	if (pMissionDef)
	{
		progression_GetNodeFromMissionDef(pEnt, pMissionDef, &pProgMission);
	}
	else
	{
		pProgMission = gclMission_GetProgressionMissionFromMissionName(pchMissionName);
	}
	
	return pProgMission && !progression_IsMissionOptional(pEnt, pProgMission);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionIsSkippableInProgression");
bool exprMissionIsSkippableInProgression(SA_PARAM_OP_STR const char *pchMissionName, bool bValidateRequirements)
{
	int iProgMissionIdx = -1;
	Entity* pEnt = entActivePlayerPtr();
	GameProgressionNodeDef* pNodeDef = NULL;
	MissionDef* pMissionDef = RefSystem_ReferentFromString(g_MissionDictionary, pchMissionName);
	if (pMissionDef)
	{
		pNodeDef = progression_GetNodeFromMissionDef(pEnt, pMissionDef, NULL);
		iProgMissionIdx = progression_FindMissionForNode(pNodeDef, pchMissionName);
	}
	else
	{
		gclMission_GetProgressionMissionFromMissionNameEx(pchMissionName, &pNodeDef, &iProgMissionIdx);
	}
	if (pNodeDef && pNodeDef->pMissionGroupInfo)
	{
		if (bValidateRequirements)
		{
			return progression_IsMissionSkippable(pEnt, pNodeDef, iProgMissionIdx);
		}
		else
		{
			GameProgressionMission* pProgMission = eaGet(&pNodeDef->pMissionGroupInfo->eaMissions, iProgMissionIdx);
			return pProgMission && pProgMission->bSkippable;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetWarpMapDisplayName");
const char* exprMissionGetWarpMapDisplayName(SA_PARAM_OP_STR const char *pchMissionName)
{
	Entity* pEnt = entActivePlayerPtr();
	MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(pEnt);
	Mission* pMission = mission_FindMissionFromRefString(pMissionInfo, pchMissionName);
	MissionDef* pMissionDef = mission_GetDef(pMission); 
	if (pMissionDef && pMissionDef->pWarpToMissionDoor)
	{
		ZoneMapInfo *pMapInfo = zmapInfoGetByPublicName(pMissionDef->pWarpToMissionDoor->pchMapName);
		if (pMapInfo)
		{
			DisplayMessage* pDisplayMessage = zmapInfoGetDisplayNameMessage(pMapInfo);
			if (pDisplayMessage)
			{
				return TranslateDisplayMessage(*pDisplayMessage);
			}
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetWarpCostNumeric");
const char* exprMissionGetWarpCostNumeric(SA_PARAM_OP_STR const char *pchMissionName)
{
	Entity* pEnt = entActivePlayerPtr();
	const char* pchMapName = zmapInfoGetPublicName(NULL);
	MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(pEnt);
	Mission* pMission = mission_FindMissionFromRefString(pMissionInfo, pchMissionName);
	MissionDef* pMissionDef = mission_GetDef(pMission); 
	MissionWarpCostDef* pCostDef = missiondef_GetWarpCostDef(pMissionDef);

	if (pCostDef && pchMapName != pMissionDef->pWarpToMissionDoor->pchMapName)
	{
		return pCostDef->pchNumeric;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetWarpCost");
S32 exprMissionGetWarpCost(SA_PARAM_OP_STR const char *pchMissionName)
{
	Entity* pEnt = entActivePlayerPtr();
	const char* pchMapName = zmapInfoGetPublicName(NULL);
	MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(pEnt);
	Mission* pMission = mission_FindMissionFromRefString(pMissionInfo, pchMissionName);
	MissionDef* pMissionDef = mission_GetDef(pMission); 
	MissionWarpCostDef* pCostDef = missiondef_GetWarpCostDef(pMissionDef);

	if (pCostDef && pMissionDef->pWarpToMissionDoor->pchMapName != pchMapName)
	{
		return pCostDef->iNumericCost;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionIsWarpAvailable");
bool exprMissionIsWarpAvailable(SA_PARAM_OP_STR const char *pchMissionName)
{
	Entity* pEnt = entActivePlayerPtr();
	const char* pchMapName = zmapInfoGetPublicName(NULL);
	MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(pEnt);
	Mission* pMission = mission_FindMissionFromRefString(pMissionInfo, pchMissionName);
	MissionDef* pMissionDef = mission_GetDef(pMission);
	ZoneMapType eMapType = zmapInfoGetMapType(NULL);

	if (pEnt &&
		(eMapType == ZMTYPE_SHARED || eMapType == ZMTYPE_STATIC) &&
		pMissionDef && pMissionDef->pWarpToMissionDoor)
	{
		if (!mission_CanPlayerUseMissionWarp(pEnt, pMission))
		{
			return false;
		}
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionIsPlayerOnMissionDoorMap");
bool exprMissionIsPlayerOnMissionDoorMap(SA_PARAM_OP_STR const char *pchMissionName)
{
	Entity* pEnt = entActivePlayerPtr();
	const char* pchMapName = zmapInfoGetPublicName(NULL);
	MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(pEnt);
	Mission* pMission = mission_FindMissionFromRefString(pMissionInfo, pchMissionName);
	MissionDef* pMissionDef = mission_GetDef(pMission); 

	if (pMissionDef && pMissionDef->pWarpToMissionDoor)
	{
		return (pMissionDef->pWarpToMissionDoor->pchMapName == pchMapName);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionMoveOrWarpToDoor");
bool exprMissionMoveOrWarpToDoor(SA_PARAM_OP_STR const char *pchMissionName)
{
	Entity* pEnt = entActivePlayerPtr();
	const char* pchMapName = zmapInfoGetPublicName(NULL);
	ZoneMapType eMapType = zmapInfoGetMapType(NULL);
	MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(pEnt);
	Mission* pMission = mission_FindMissionFromRefString(pMissionInfo, pchMissionName);
	MissionDef* pMissionDef = mission_GetDef(pMission); 

	if (pEnt &&
		(eMapType == ZMTYPE_SHARED || eMapType == ZMTYPE_STATIC) &&
		pMissionDef && pMissionDef->pWarpToMissionDoor)
	{
		if (pMissionDef->pWarpToMissionDoor->pchMapName != pchMapName)
		{
			if (mission_CanPlayerUseMissionWarp(pEnt, pMission))
			{
				ServerCmd_mission_WarpToMissionDoor(pchMissionName);
				return true;
			}
		}
		else
		{
			ServerCmd_spawnpoint_NavToSpawn(pMissionDef->pWarpToMissionDoor->pchSpawn);
			return true;
		}
	}
	return false;
}


// ----------------------------------------------------------------------------
// Mission Auto-Hail functionality
// ----------------------------------------------------------------------------


// We need to store off when we auto hail the player so that we don't keep popping
//  the same missions over and over if the player closes or declines the hail.
// Store every hail we initiate in a static list that will exist for the life
// of the client and never offer the items in the list a second time to the same player

typedef struct HailOfferedListEntry
{
	ContainerID	playerEntityContainerID;
	bool bComplete; // The mission needs a completion hail, as opposed to being a new hail for a new mission
	const char *pchMissionName;	// Pooled
} HailOfferedListEntry;

static HailOfferedListEntry **sppHailOfferedList = NULL;

static void AddHailOffered(ContainerID playerID, bool bComplete, const char *pchMissionName)
{
	// This malloc will live for the duration of the client.
	HailOfferedListEntry *pHailOfferedEntry = malloc(sizeof(HailOfferedListEntry));
	if (pHailOfferedEntry!=NULL)
	{
		pHailOfferedEntry->playerEntityContainerID = playerID;
		pHailOfferedEntry->bComplete = bComplete;
		pHailOfferedEntry->pchMissionName = allocAddString(pchMissionName); // Pooled so we can do direct compares
		eaPush(&sppHailOfferedList, pHailOfferedEntry);
	}
}

static bool HailAlreadyOffered(ContainerID playerID, bool bComplete, const char *pchMissionName)
{
	int i;
	for (i=0; i < eaSize(&sppHailOfferedList); i++)
	{
		HailOfferedListEntry *pHailOfferedEntry = sppHailOfferedList[i];
		if (pHailOfferedEntry!=NULL && pHailOfferedEntry->playerEntityContainerID==playerID &&
			pHailOfferedEntry->bComplete==bComplete && 
			pHailOfferedEntry->pchMissionName==allocAddString(pchMissionName))
		{
			return(true);
		}
	}
	return(false);
}

///-------------------------------------

/////////////////////////////////////////////////
// Go through all episode missions and look for the 'next' one. It should
//  be the first (and only) skippable one. Or the first available and not complete one.
// But skip the very first episode as it gets tangled up with completing the tutorial

static void FindAutoHailCandidate(Entity *pPlayerEnt, MissionInfo *pMissionInfo, const char* pchRootNodeDefName,
									const char** ppchHailContact,
									char** ppchHailContactKey,
								    const char** ppchHailMissionName)

{
	if (ppchHailContact==NULL || ppchHailContactKey==NULL || ppchHailMissionName==NULL)
	{
		return;
	}
	*ppchHailContact=NULL;
	*ppchHailContactKey=NULL;
	*ppchHailMissionName=NULL;
	
	{
		bool bAmFirstEpisode=true;
		bool bDataQueued=false;
		bool bContinueSearch=true;

		// Choose the story arc list for the right faction
		GameProgressionNodeDef* pRootNodeDef = progression_NodeDefFromName(pchRootNodeDefName);
		GameProgressionNodeDef* pCurrNodeDef = progression_FindLeftMostLeaf(pRootNodeDef);

		// Loop through all arcs (GetStoryArcMissionGroupList)
		while (bContinueSearch && pCurrNodeDef!=NULL)
		{
			bool bIncludeMissionsNotEncounteredByPlayer= true;
			int iProgMissionIndex;

			// Go through each mission in the arc
			for (iProgMissionIndex=0; bContinueSearch && (iProgMissionIndex < eaSize(&pCurrNodeDef->pMissionGroupInfo->eaMissions)); iProgMissionIndex++)
			{
				GameProgressionMission *pProgMission = pCurrNodeDef->pMissionGroupInfo->eaMissions[iProgMissionIndex];
				CompletedMission *pCompletedMission;

				if (bAmFirstEpisode)
				{
					// skip the very first episode as it gets tangled up with completing the tutorial
					bAmFirstEpisode=false;
					continue;
				}

				pCompletedMission = eaIndexedGetUsingString(&pMissionInfo->completedMissions, pProgMission->pchMissionName);
				if (pCompletedMission)
				{
					// It's completed, it shouldn't be auto-hailed.
					continue;
				}
				else
				{
					Mission *pMission = mission_FindMissionFromRefString(pMissionInfo, pProgMission->pchMissionName);
					if (!pMission && bIncludeMissionsNotEncounteredByPlayer)
					{
						MissionDef* pMissionDef = missiondef_DefFromRefString(pProgMission->pchMissionName);

						// Get the data we need (PopulateRowFromProgressMission(...), which
						//    calls PopulateRowFromMission(...) and PopulateRowFromProgressionMissionInternal(...) )

						bool bAvailable=false;
						bool bHasContact=false;
						bool bOptional=false;
						bool bSkippable=false;
						bool bIneligible=false;
						bool bVisible=false;
						
						bAvailable = progression_CheckMissionRequirementsEx(pPlayerEnt, pCurrNodeDef, iProgMissionIndex);
						bOptional = !!progression_IsMissionOptional(pPlayerEnt, pProgMission);
						bSkippable = progression_IsMissionSkippable(pPlayerEnt, pCurrNodeDef, iProgMissionIndex);

						// The rest of the info we need has to be requested from the server and put in the mission cache data.
						//   Only check for that if we pass the basic tests of being an Available mission that is not optional.
						if (bAvailable && !bOptional)
						{
							// Check the Mission Cache
							CachedMissionData* pMissionCacheData = eaIndexedGetUsingString(&s_eaCachedMissionData, pProgMission->pchMissionName);
							if (pMissionCacheData==NULL)
							{
								// It's not there. Add the request to the queue. Continue looking for missions
								//  until we get the next one that is skippable (the one we really want) if it exists.
								// Note that we could theoretically request all available missions if none
								//  are actually skippable.
								bDataQueued=true;
								AddMissionRequestToQueue(&s_ppchUpdateQueue, pPlayerEnt, pMissionInfo, pProgMission->pchMissionName);
								if (bSkippable)
								{
									// Only queue for data up to the skippable mission.
									// Stop looking and process whatever we already have
									bContinueSearch=false;
								}
							}
							else if (!bDataQueued)
							{
								// Only continue if we haven't yet queued for mission data because we should wait
								//  for the data to come back for the earlier queued stuff before
								//  making a decisision as to the best candidate for autohail.
								
								bHasContact = (pMissionCacheData->pchContact!=NULL);
								bIneligible = (pMissionCacheData->eCreditType==MissionCreditType_Ineligible);
								bVisible = (!pProgMission->pExprVisible || (pMissionCacheData && pMissionCacheData->bVisible));

								// First make sure it's a valid mission
								if (bVisible && bHasContact)
								{
									// Treat skippable differently
									if (bSkippable)
									{
										// If skippable, it's the 'latest' and the one we would like to use.
										// The player is either eligible for it or not yet.

										if (bIneligible)
										{
											// We may be operating on old data in the cache at this point.
											//  Rerequest the data just in case we have become eligible
											//  since the last time our data was updated.
											AddMissionRequestToQueue(&s_ppchUpdateQueue, pPlayerEnt, pMissionInfo, pProgMission->pchMissionName);

											// But we don't want to use a non-skippable mission if we've already encountered one
											*ppchHailContact=NULL;
											*ppchHailContactKey=NULL;
											*ppchHailMissionName=NULL;
										}
										else
										{
											*ppchHailContact=pMissionCacheData->pchContact;
											*ppchHailContactKey=pMissionCacheData->pchContactKey;
											*ppchHailMissionName=pProgMission->pchMissionName;
										}
	
										// Regardless of eligibilty or caching, we're done looking
										bContinueSearch=false;
									}
									else
									{
										// Regular non-skippable hail.
										// We may already have skipped this mission
										// or there may be no more skippable missions in the entire list.
									
										if (bIneligible)
										{
											// We may be operating on old data in the cache at this point.
											//  Rerequest the data just in case we have become eligible
											//  since the last time our data was updated.
											AddMissionRequestToQueue(&s_ppchUpdateQueue, pPlayerEnt, pMissionInfo, pProgMission->pchMissionName);
										}
										else
										{
											// Store off the first one. If we never get a skippable then we'll use it
											if (*ppchHailContact==NULL)
											{
												*ppchHailContact=pMissionCacheData->pchContact;
												*ppchHailContactKey=pMissionCacheData->pchContactKey;
												*ppchHailMissionName=pProgMission->pchMissionName;
											}
										}
									}
								}
							}
						}
					}
				}
			}
			// Get the next node
			pCurrNodeDef = GET_REF(pCurrNodeDef->hNextSibling);
		}
		// We processed everything we needed to. Finish up and return
		
		if (bDataQueued)
		{
			// We need more mission data. Fire off the requests.
			
			CheckMissionDisplayDataRequestQueue();

			// And reset the Hail info since we don't want to use it this frame.
			
			*ppchHailContact=NULL;
			*ppchHailContactKey=NULL;
			*ppchHailMissionName=NULL;
		}
	}
}


/////////////////////////////////////////////////
// Go through all episode missions and look for the mission passed in.

static bool MissionIsStoryArcMission(Mission *pTestMission, const char* pchRootNodeDefName)
{
	GameProgressionNodeDef* pRootNodeDef = progression_NodeDefFromName(pchRootNodeDefName);
	GameProgressionNodeDef* pCurrNodeDef = progression_FindLeftMostLeaf(pRootNodeDef);

	// Loop through all arcs (GetStoryArcMissionGroupList)
	while (pCurrNodeDef!=NULL)
	{
		int iProgMissionIndex;

		// Go through each mission in the arc
		for (iProgMissionIndex=0; iProgMissionIndex < eaSize(&pCurrNodeDef->pMissionGroupInfo->eaMissions); iProgMissionIndex++)
		{
			GameProgressionMission *pProgMission = pCurrNodeDef->pMissionGroupInfo->eaMissions[iProgMissionIndex];
			if (stricmp_safe(pProgMission->pchMissionName, pTestMission->missionNameOrig)==0)
			{
				// It matches. It's a story arc mission
				return(true);
			}
		}
		// Get the next node
		pCurrNodeDef = GET_REF(pCurrNodeDef->hNextSibling);
	}
	return(false);
}


// Return true if there is already a contact up for this player
bool AlreadyAContact(Entity* pPlayerEnt)
{
	InteractInfo* pInfo = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);
	if(pInfo && pInfo->pContactDialog)
	{
		return(true);
	}
	return(false);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionsCheckForAutoHail");
void exprMissionsCheckForAutoHail(const char* pchRootNodeDefName)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	ZoneMapType eMapType = zmapInfoGetMapType(NULL);
	MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);

	// If the basic options setting is turned on, don't do anything
	if (g_bDisableAutoHail)
	{
		return;
	}

	// If we're not on a static map or if the player has no MissionInfo or if a contact is already up, don't do anything
	//  (note that we are relying on the static map restriction to make sure we are also not in combat. This will need
	//   to be modified for other games in all likelihood)
	if (eMapType == ZMTYPE_STATIC && pMissionInfo!=NULL && !AlreadyAContact(pPlayerEnt))
	{
		/////////////////////////////////////////////////
		/// First check for any complete remote contact missions in the player's mission list
		{
			S32 iMission;
			for (iMission = 0; iMission < eaSize(&pMissionInfo->missions); iMission++)
			{
				Mission *pMission = pMissionInfo->missions[iMission];
				
				if (pMission->state == MissionState_Succeeded)
				{
					// Make sure we haven't already offered this session (or that we already declined to make the offer)
					if (!HailAlreadyOffered(pPlayerEnt->myContainerID, true, pMission->missionNameOrig))
					{
						// WOLF[2Jan12] This is a 'slow' call. It searches through all episodes to see if we are on the list.
						//  It will be replaced (hopefully in the near future) with actual mission tagging that lets
						//  is specify types. At that point we should replace this function.
						if (MissionIsStoryArcMission(pMission, pchRootNodeDefName))
						{
							RemoteContact** eaRemoteContacts = SAFE_MEMBER3(pPlayerEnt, pPlayer, pInteractInfo, eaRemoteContacts);
							int i, j;
							for (i=0; i < eaSize(&eaRemoteContacts); i++) 
							{
								for (j=0; j < eaSize(&eaRemoteContacts[i]->eaOptions); j++) 
								{
									RemoteContactOption *pRemoteOption = eaRemoteContacts[i]->eaOptions[j];
									const char* pchOptionMission = pRemoteOption->pcMissionName;
									if (pchOptionMission && stricmp_safe(pMission->missionNameOrig, pchOptionMission) == 0 &&
												pRemoteOption->pOption && pRemoteOption->pOption->eType == ContactIndicator_MissionCompleted)
									{
										ServerCmd_contact_StartRemoteContactWithOption(eaRemoteContacts[i]->pchContactDef,pRemoteOption->pchKey);
										AddHailOffered(pPlayerEnt->myContainerID, true, pMission->missionNameOrig);
										// Don't try to fire off anymore contacts this pass
										return;
									}
								}
							}
						}
						else
						{
							// Add this to the already offered list so that we don't have to search through the story arcs in the future
							AddHailOffered(pPlayerEnt->myContainerID, true, pMission->missionNameOrig);
						}
					}
				}
			}
		}

		/////////////////////////////////////////////////
		// Look for AutoHail candidates and fire off the auto hail if we find one
		{
			const char* pchHailContact=NULL;
			char* pchHailContactKey=NULL;
			const char* pchHailMissionName=NULL;

			FindAutoHailCandidate(pPlayerEnt, pMissionInfo, pchRootNodeDefName,
									&pchHailContact,
									&pchHailContactKey,
								    &pchHailMissionName);

			if (pchHailContact!=NULL && pchHailMissionName!=NULL)
			{
				// Check to see if the candidate mission has already been autohailed
				if (!HailAlreadyOffered(pPlayerEnt->myContainerID, false, pchHailMissionName))
				{
					// Fire off a contact if we have one
					if (pchHailContactKey)
					{
						ServerCmd_contact_StartRemoteContactWithOption(pchHailContact, pchHailContactKey);
					}
					else
					{
						ServerCmd_contact_StartRemoteContactWithMission(pchHailContact, pchHailMissionName);
					}

					// Store off that we sent this auto hail
					AddHailOffered(pPlayerEnt->myContainerID, false, pchHailMissionName);
				}
			}
		}
	}
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionUITypeGetIcon");
const char * exprMissionUITypeGetIcon(S32 eMissionUIType)
{
	MissionUITypeData *pMissionUITypeData = mission_GetMissionUITypeData(eMissionUIType);

	return pMissionUITypeData && pMissionUITypeData->pchIcon ? pMissionUITypeData->pchIcon : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionUITypeGetDisplayName");
const char * exprMissionUITypeGetDisplayName(S32 eMissionUIType)
{
	MissionUITypeData *pMissionUITypeData = mission_GetMissionUITypeData(eMissionUIType);

	const char *pchTranslatedDisplayName = NULL;

	if (pMissionUITypeData)
	{
		pchTranslatedDisplayName = TranslateDisplayMessage(pMissionUITypeData->msgDisplayName);
	}

	return pchTranslatedDisplayName ? pchTranslatedDisplayName : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetRewardCount");
S32 exprMissionGetRewardCount(const char *pchMissionDef)
{
	Entity *pEnt = entActivePlayerPtr();
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	CachedMissionReward *pReward = NULL;

	// Request reward data
	AddMissionRequestToQueue(&s_ppchUpdateRewardQueue, pEnt, pInfo, pchMissionDef);
	CheckMissionDisplayDataRequestQueue();

	pReward = eaIndexedGetUsingString(&s_eaCachedMissionReward, pchMissionDef);
	if (pReward)
	{
		S32 i, iCount = 0;
		for (i = eaSize(&pReward->eaRewardBags) - 1; i >= 0; i--)
		{
			InventoryBag *pBag = pReward->eaRewardBags[i];
			if (pBag->pRewardBagInfo && pBag->pRewardBagInfo->PickupType != kRewardPickupType_Choose)
				iCount += inv_bag_CountItems(pBag, NULL);
		}
		return iCount;
	}

	return -1;
}

static void FilterItemList(Item ***peaItems, const char *pchFilter)
{
	static const char **s_eaRequireAll;
	static const char **s_eaRequireAny;
	static const char **s_eaExcludeAll;
	static const char **s_eaExcludeAny;
	char *pchBuffer, *pchContext, *pchToken;
	S32 i, j;
	strdup_alloca(pchBuffer, pchFilter);

	if (pchToken = strtok_r(pchBuffer, " ,\r\n\t", &pchContext))
	{
		do
		{
			if (pchToken[0] == '+')
				eaPush(&s_eaRequireAll, pchToken + 1);
			else if (pchToken[0] == '-')
				eaPush(&s_eaExcludeAny, pchToken + 1);
			else
				eaPush(&s_eaRequireAny, pchToken);
		} while (pchToken = strtok_r(NULL, " ,\r\n\t", &pchContext));
	}

	for (i = eaSize(peaItems) - 1; i >= 0; i--)
	{
		ItemDef *pDef = GET_REF((*peaItems)[i]->hItem);
		const char *pchDef = pDef ? pDef->pchName : REF_STRING_FROM_HANDLE((*peaItems)[i]->hItem);
		bool bExclude = false;

		if (!pchDef)
			continue;

		for (j = eaSize(&s_eaExcludeAny) - 1; j >= 0; j--)
			if (strstri(pchDef, s_eaExcludeAny[j]))
				break;
		if (j >= 0)
			bExclude = true;

		if (!bExclude)
		{
			for (j = eaSize(&s_eaRequireAny) - 1; j >= 0; j--)
				if (strstri(pchDef, s_eaRequireAny[j]))
					break;
			if (j < 0)
				bExclude = true;
		}

		if (!bExclude)
		{
			for (j = eaSize(&s_eaExcludeAll) - 1; j >= 0; j--)
				if (!strstri(pchDef, s_eaExcludeAny[j]))
					break;
			if (j < 0)
				bExclude = true;
		}

		if (!bExclude)
		{
			for (j = eaSize(&s_eaRequireAll) - 1; j >= 0; j--)
				if (!strstri(pchDef, s_eaRequireAll[j]))
					break;
			if (j >= 0)
				bExclude = true;
		}

		if (bExclude)
			eaRemove(peaItems, i);
	}

	eaClear(&s_eaRequireAll);
	eaClear(&s_eaRequireAny);
	eaClear(&s_eaExcludeAll);
	eaClear(&s_eaExcludeAny);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMissionGetRewards");
void exprMissionGetRewards(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_STR const char *pchMissionDef, SA_PARAM_OP_STR const char *pchFilter)
{
	static Item **s_eaRewardItems;
	Entity *pEnt = entActivePlayerPtr();
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	CachedMissionReward *pReward = NULL;
	S32 i;

	// Request reward data
	AddMissionRequestToQueue(&s_ppchUpdateRewardQueue, pEnt, pInfo, pchMissionDef);
	CheckMissionDisplayDataRequestQueue();

	eaClear(&s_eaRewardItems);

	pReward = eaIndexedGetUsingString(&s_eaCachedMissionReward, pchMissionDef);
	if (pReward)
	{
		for (i = eaSize(&pReward->eaRewardBags) - 1; i >= 0; i--)
		{
			InventoryBag *pBag = pReward->eaRewardBags[i];
			if (pBag->pRewardBagInfo && pBag->pRewardBagInfo->PickupType != kRewardPickupType_Choose)
				inv_bag_GetSimpleItemList(pBag, &s_eaRewardItems, false);
		}
	}

	if (eaSize(&s_eaRewardItems) > 0 && pchFilter && *pchFilter)
		FilterItemList(&s_eaRewardItems, pchFilter);

	ui_GenSetManagedList(pGen, &s_eaRewardItems, parse_Item, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MissionGetChoosableRewardCount");
S32 exprMissionGetChoosableRewardCount(const char *pchMissionDef)
{
	Entity *pEnt = entActivePlayerPtr();
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	CachedMissionReward *pReward = NULL;

	// Request reward data
	AddMissionRequestToQueue(&s_ppchUpdateRewardQueue, pEnt, pInfo, pchMissionDef);
	CheckMissionDisplayDataRequestQueue();

	pReward = eaIndexedGetUsingString(&s_eaCachedMissionReward, pchMissionDef);
	if (pReward)
	{
		S32 i, iCount = 0;
		for (i = eaSize(&pReward->eaRewardBags) - 1; i >= 0; i--)
		{
			InventoryBag *pBag = pReward->eaRewardBags[i];
			if (pBag->pRewardBagInfo && pBag->pRewardBagInfo->PickupType == kRewardPickupType_Choose && pBag->pRewardBagInfo->NumPicks > 0)
			{
				iCount += inv_bag_CountItems(pBag, NULL);
				if (iCount > 0)
					break;
			}
		}
		return iCount;
	}

	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMissionGetChoosableRewards");
void exprMissionGetChoosableRewards(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_STR const char *pchMissionDef, const char *pchFilter)
{
	// pchFilter not used
	static Item **s_eaBagItems;
	Entity *pEnt = entActivePlayerPtr();
	ChoosableItem ***peaChoosable = ui_GenGetManagedListSafe(pGen, ChoosableItem);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	CachedMissionReward *pReward = NULL;
	S32 i, j, iChoosable = 0;

	// Request reward data
	AddMissionRequestToQueue(&s_ppchUpdateRewardQueue, pEnt, pInfo, pchMissionDef);
	CheckMissionDisplayDataRequestQueue();

	pReward = eaIndexedGetUsingString(&s_eaCachedMissionReward, pchMissionDef);
	if (pReward)
	{
		for (i = 0; i < eaSize(&pReward->eaRewardBags); i++)
		{
			InventoryBag *pBag = pReward->eaRewardBags[i];
			if (pBag->pRewardBagInfo && pBag->pRewardBagInfo->PickupType == kRewardPickupType_Choose && pBag->pRewardBagInfo->NumPicks > 0)
			{
				ChoosableItem *pChoosable;
				inv_bag_GetSimpleItemList(pBag, &s_eaBagItems, false);

				// Bag Header
				if (eaSize(&s_eaBagItems) > 0)
				{
					pChoosable = eaGetStruct(peaChoosable, parse_ChoosableItem, iChoosable++);
					if (IS_HANDLE_ACTIVE(pChoosable->hItemDef))
						REMOVE_HANDLE(pChoosable->hItemDef);
					pChoosable->bSelected = false;
					pChoosable->iBagIdx = i;
					pChoosable->iNumPicks = pBag->pRewardBagInfo->NumPicks;
					pChoosable->pItem = NULL;
				}

				// Bag Items
				for (j = 0; j < eaSize(&s_eaBagItems); j++)
				{
					pChoosable = eaGetStruct(peaChoosable, parse_ChoosableItem, iChoosable++);
					if (GET_REF(pChoosable->hItemDef) != GET_REF(s_eaBagItems[j]->hItem))
						COPY_HANDLE(pChoosable->hItemDef, s_eaBagItems[j]->hItem);
					pChoosable->bSelected = false;
					pChoosable->iBagIdx = i;
					pChoosable->iNumPicks = 0;
					pChoosable->pItem = s_eaBagItems[j];
				}

				eaClear(&s_eaBagItems);
			}
		}
	}

	eaSetSizeStruct(peaChoosable, parse_ChoosableItem, iChoosable);
	ui_GenSetManagedListSafe(pGen, peaChoosable, ChoosableItem, false);
}

#include "missionui_eval_c_ast.c"
