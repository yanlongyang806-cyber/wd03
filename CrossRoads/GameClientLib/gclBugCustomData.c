#include "gclBugReport.h"
#include "gclBugCustomData_c_ast.h"
#include "ticketnet.h"

#include "uiComboBox.h"
#include "estring.h"
#include "Expression.h"
#include "UIGen.h"

#include "EntCritter.h"
#include "EntitySavedData.h"
#include "SavedPetCommon.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"
#include "mission_common.h"
#include "Powers.h"
#include "chat/gclChatLog.h"
#include "gclEntity.h"
#include "Character.h"
#include "chatCommonStructs.h"
#include "GameStringFormat.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

//extern void TicketUI_DisableButton(const char *genName, int disable);
static char *sDataString = NULL;

///////////////////////////////
// Item Ticket information

AUTO_STRUCT;
typedef struct CBugItemDef
{
	char *estrDisplayName;					AST(ESTRING)
	const char *pchItemDefName;				AST(UNOWNED)
	char *estrAlgoItem;						AST(ESTRING)
} CBugItemDef;

static CBugItemDef *sSelectedItem = NULL;

void cItems_ItemDataString(char **estr, const CBugItemDef *selected)
{
	if (!selected)
		return;
	estrCopy2(estr, selected->pchItemDefName);
}

void cItems_ItemDisplayName(char **estrDisplayName, const CBugItemDef *selected)
{
	if (selected)
		estrCopy2(estrDisplayName, selected->estrDisplayName);
}

CBugItemDef *CategoryItem_GetSelectedStruct(void)
{
	if (sSelectedItem && sSelectedItem->pchItemDefName == NULL)
		return NULL;
	return sSelectedItem;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryItem_GetList");
void CategoryItem_GetList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	S32 iCount = 0;
	CBugItemDef ***peaSlots = ui_GenGetManagedListSafe(pGen, CBugItemDef);
	// Dummy blank slot
	eaGetStruct(peaSlots, parse_CBugItemDef, iCount++);
	if (pEnt && pEnt->pInventoryV2)
	{
		S32 i;
		S32 j;
		S32 iPet;
		for (j = 0; j < eaSize(&pEnt->pInventoryV2->ppInventoryBags); j++)
		{
			InventoryBag *pBag = pEnt->pInventoryV2->ppInventoryBags[j];

			// See if this bag contains reportable items
			if (invbag_isReportable(pBag))
			{
				for (i = 0; i < eaSize(&pBag->ppIndexedInventorySlots); i++)
				{
					InventorySlot *pSlot = pBag->ppIndexedInventorySlots[i];

					if (pSlot && pSlot->pItem)
					{
						CBugItemDef *pBugDef = eaGetStruct(peaSlots, parse_CBugItemDef, iCount++);
						pBugDef->pchItemDefName = REF_STRING_FROM_HANDLE(pSlot->pItem->hItem);
						estrCopy2(&pBugDef->estrDisplayName, item_GetName(pSlot->pItem,pEnt));
						if (pSlot->pItem->flags & kItemFlag_Algo)
						{
							estrClear(&pBugDef->estrAlgoItem);
							ParserWriteText(&pBugDef->estrAlgoItem, parse_Item, pSlot->pItem, 0, 0, 0);
						}
					}
				}
			}
		}

		if(pEnt->pSaved && pEnt->pSaved->ppOwnedContainers)
		{
			for(iPet=eaSize(&pEnt->pSaved->ppOwnedContainers)-1;iPet>=0;iPet--)
			{
				PuppetEntity *pPuppet = SavedPet_GetPuppetFromPet(pEnt,pEnt->pSaved->ppOwnedContainers[iPet]);
				Entity *pEntPet = SavedPet_GetEntity(entGetPartitionIdx(pEnt), pEnt->pSaved->ppOwnedContainers[iPet]);
				PetDef *pPetDef = pEntPet && pEntPet->pCritter ? GET_REF(pEntPet->pCritter->petDef) : NULL;
				ItemDef *pItemDef = pPetDef ? GET_REF(pPetDef->hTradableItem) : NULL;
				CBugItemDef *pBugDef = NULL;

				if(!pEntPet || !pItemDef) {
					continue;
				}

				// Success
				pBugDef = eaGetStruct(peaSlots, parse_CBugItemDef, iCount++);
				pBugDef->pchItemDefName = REF_STRING_FROM_HANDLE(pPetDef->hTradableItem);
			}
		}
	}

	while (eaSize(peaSlots) > iCount)
		StructDestroy(parse_CBugItemDef, eaPop(peaSlots));
	ui_GenSetManagedListSafe(pGen, peaSlots, CBugItemDef, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryItem_GetText");
SA_RET_OP_STR const char * CategoryItem_GetRowDisplayText(SA_PARAM_NN_VALID CBugItemDef *item)
{
	if (!item->estrDisplayName)
	{
		if (item->pchItemDefName)
			cItems_ItemDisplayName(&item->estrDisplayName, item);
		else
			FormatGameMessageKey(&item->estrDisplayName, "Ticket.Item.None", STRFMT_END);
	}
	return item->estrDisplayName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryItem_GetLabel");
SA_RET_OP_STR const char * CategoryItem_GetLabel(void)
{
	if (sSelectedItem)
	{
		if (sSelectedItem->estrAlgoItem)
			return sSelectedItem->estrAlgoItem;
		else
			return CategoryItem_GetRowDisplayText(sSelectedItem);
	}
	estrClear(&sDataString);
	FormatGameMessageKey(&sDataString, "Ticket.Item.Select", STRFMT_END);
	return sDataString;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryItem_GetSelected");
SA_RET_OP_STR const char * CategoryItem_GetSelected(void)
{
	if (sSelectedItem)
	{
		return CategoryItem_GetRowDisplayText(sSelectedItem);
	}
	estrClear(&sDataString);
	FormatGameMessageKey(&sDataString, "Ticket.Item.Select", STRFMT_END);
	return sDataString;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryItem_SetSelected");
bool CategoryItem_SetSelected(SA_PARAM_OP_VALID CBugItemDef *pCategoryItem)
{
	StructDestroy(parse_CBugItemDef, sSelectedItem);
	sSelectedItem = StructClone(parse_CBugItemDef, pCategoryItem);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryItem_Reset");
void CategoryItem_Reset(void)
{
	StructDestroy(parse_CBugItemDef, sSelectedItem);
}

///////////////////////////////
// Mission Ticket information

AUTO_STRUCT;
typedef struct CBugMissionDef
{
	char *missionDefName;
	bool bIsCompleted;
	char *displayString; AST(ESTRING)
	ContainerID ugcProjectID; AST( NAME(UGCProjectID) )
} CBugMissionDef;

static CBugMissionDef ** sppMissions = NULL;
static CBugMissionDef *sSelectedMission = NULL;
static bool sbSessionMissionInit = false;

static int sortCompletedMissions(const CompletedMission **a, const CompletedMission **b)
{
	MissionDef * aDef = GET_REF((*a)->def);
	MissionDef * bDef = GET_REF((*b)->def);

	if (aDef && bDef)
	{
		if (aDef->missionType == MissionType_Normal && bDef->missionType != MissionType_Normal)
			return -1;
		if (aDef->missionType != MissionType_Normal && bDef->missionType == MissionType_Normal)
			return 1;
	}
	return (int)(completedmission_GetLastCompletedTime(*b) - completedmission_GetLastCompletedTime(*a));
}

static void createMissionList(void)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(entActivePlayerPtr());
	CompletedMission ** completedCopy = NULL;
	int i; // go through the MissionDefs and add the names
	int size;

	sSelectedMission = NULL;
	eaClearStruct(&sppMissions, parse_CBugMissionDef);
	eaPush(&sppMissions, StructCreate(parse_CBugMissionDef)); // push a dummy empty mission
	if (!pInfo)
		return; // TODO?

	size = eaSize(&pInfo->missions); // Active Missions
	for (i=0; i<size; i++)
	{
		MissionDef * pDef = mission_GetDef(pInfo->missions[i]);
		if (pDef && missiondef_HasDisplayName(pDef) && (pDef->missionType == MissionType_Normal
			|| pDef->missionType == MissionType_Nemesis 
			|| pDef->missionType == MissionType_NemesisArc 
			|| pDef->missionType == MissionType_NemesisSubArc 
			|| pDef->missionType == MissionType_Episode 
			|| pDef->missionType == MissionType_TourOfDuty
			|| pDef->missionType == MissionType_AutoAvailable)) // don't get Perks, Schemes
		{
			CBugMissionDef *pCurMission = StructCreate(parse_CBugMissionDef);
			pCurMission->missionDefName = strdup(pDef->name);
			pCurMission->ugcProjectID = pDef->ugcProjectID;
			eaPush(&sppMissions, pCurMission);
		}
	}

	eaCopy(&completedCopy, &pInfo->completedMissions);
	eaQSort(completedCopy, sortCompletedMissions);
	size = eaSize(&completedCopy); // Completed Missions

	// restrict completed list to 5 most recent missions (which should all be on top)
	for (i=0; i<size && i < 5; i++)
	{
		MissionDef * pDef = GET_REF(completedCopy[i]->def);
		if (pDef && missiondef_HasDisplayName(pDef) && (pDef->missionType == MissionType_Normal
			|| pDef->missionType == MissionType_Nemesis 
			|| pDef->missionType == MissionType_NemesisArc
			|| pDef->missionType == MissionType_NemesisSubArc
			|| pDef->missionType == MissionType_Episode 
			|| pDef->missionType == MissionType_TourOfDuty
			|| pDef->missionType == MissionType_AutoAvailable
			|| pDef->missionType == MissionType_Perk)) // don't get Perks, Schemes
		{
			CBugMissionDef *pCurMission = StructCreate(parse_CBugMissionDef);
			pCurMission->missionDefName = strdup(pDef->name);
			pCurMission->bIsCompleted = true;
			eaPush(&sppMissions, pCurMission);
		}
	}
	eaDestroy(&completedCopy);
}

void cMissions_MissionDataString(char **estr, const CBugMissionDef *selected)
{
	MissionDef *pDef;
	if (!selected)
		return;
	pDef = missiondef_DefFromRefString(selected->missionDefName);
	if (pDef)
		estrCopy2(estr, pDef->pchRefString);
}

void cMissions_MissionDisplayName(char **estr, const CBugMissionDef *selected)
{
	MissionDef *pDef;
	if (!selected)
		return;
	pDef = missiondef_DefFromRefString(selected->missionDefName);
	if (pDef)
	{
		if (pDef && GET_REF(pDef->displayNameMsg.hMessage))
			FormatGameMessage(estr, GET_REF(pDef->displayNameMsg.hMessage), STRFMT_END);
		else if (pDef && pDef->name)
			estrCopy2(estr, pDef->name);
		else
			FormatGameMessageKey(estr, "Ticket.Mission.UnknownMission", STRFMT_END);
		if (selected->bIsCompleted)
		{
			char *completedString = NULL;
			FormatGameMessageKey(&completedString, "MissionUI.CompletedMissionShort", STRFMT_END);
			estrConcatf(estr, " (%s)", completedString);
			estrDestroy(&completedString);
		}
	}
}

void cMissions_MissionName(UIComboBox *cb, S32 iRow, bool bInBox, UserData userData, char **ppchOutput)
{
	if (iRow < 0)
	{
		estrCopy2(ppchOutput, cb->defaultDisplay ? cb->defaultDisplay : "");
	}
	else
	{
		CBugMissionDef * pCBugDef = ((CBugMissionDef**)(*cb->model))[iRow];
		MissionDef *pDef = missiondef_DefFromRefString(pCBugDef->missionDefName);

		if (pDef && GET_REF(pDef->displayNameMsg.hMessage))
			FormatGameMessage(ppchOutput, GET_REF(pDef->displayNameMsg.hMessage), STRFMT_END);
		else if (pDef && pDef->name)
			estrCopy2(ppchOutput, pDef->name);
		else
			FormatGameMessageKey(ppchOutput, "Ticket.Mission.UnknownMission", STRFMT_END);

		if (pCBugDef->bIsCompleted)
		{
			char *completedString = NULL;
			FormatGameMessageKey(&completedString, "MissionUI.CompletedMissionShort", STRFMT_END);
			estrConcatf(ppchOutput, " (%s)", completedString);
			estrDestroy(&completedString);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryMission_GetList");
void CategoryMission_GetList(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (!sbSessionMissionInit)
	{
		createMissionList();
		//sSelectedMission = sppMissions[0];
		sbSessionMissionInit = true;
	}
	ui_GenSetList(pGen, &sppMissions, parse_CBugMissionDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryMission_GetText");
SA_RET_OP_STR const char * CategoryMission_GetRowDisplayText(SA_PARAM_NN_VALID CBugMissionDef *mission)
{
	if (mission->displayString)
		return mission->displayString;

	if (mission->missionDefName == NULL)
		FormatGameMessageKey(&mission->displayString, "Ticket.Mission.None", STRFMT_END);
	else
		cMissions_MissionDisplayName(&mission->displayString, mission);
	return mission->displayString;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryMission_GetSelected");
SA_RET_OP_STR const char * CategoryMission_GetSelected(void)
{
	if (sSelectedMission)
	{
		return CategoryMission_GetRowDisplayText(sSelectedMission);
	}
	estrClear(&sDataString);
	FormatGameMessageKey(&sDataString, "Ticket.Mission.Select", STRFMT_END);
	return sDataString;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryMission_GetLabel");
SA_RET_OP_STR const char * CategoryMission_GetLabel(void)
{
	estrClear(&sDataString);
	if (sSelectedMission)
	{
		cMissions_MissionDataString(&sDataString, sSelectedMission);
		return sDataString;
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryMission_SetSelected");
bool CategoryMission_SetSelected(SA_PARAM_OP_VALID CBugMissionDef *pchCategoryMission)
{
	sSelectedMission = pchCategoryMission;
	//Category_SendTicketUpdateRequest();
	//TicketUI_DisableButton("Ticket_Update", sSelectedMission == NULL);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryMission_Reset");
void CategoryMission_Reset(void)
{
	sSelectedMission = NULL;
	sbSessionMissionInit = false;
	eaDestroyStruct(&sppMissions, parse_CBugMissionDef);
}

CBugMissionDef * CategoryMission_GetSelectedStruct(void)
{
	if (sSelectedMission && sSelectedMission->missionDefName == NULL)
		return NULL;
	return sSelectedMission;
}

///////////////////////////////
// Powers Ticket information

AUTO_STRUCT;
typedef struct CBugPowerDef
{
	char *powerDefName;
	char *displayString; AST(ESTRING)
} CBugPowerDef;

AUTO_ENUM;
typedef enum BugPowerListFlag
{
	kBugPowerListFlag_MustNotHavePurpose   = 1 << 0,
	kBugPowerListFlag_MustHavePurpose      = 1 << 1,
} BugPowerListFlag;

static CBugPowerDef ** sppPowers = NULL;
static CBugPowerDef *sSelectedPower= NULL;
static bool sbSessionPowerInit = false;
static U32 sePowersListFlags = 0;

static void createPowersList(U32 eFlags)
{
	int i;
	int size;
	Entity *e = entActivePlayerPtr();
	
	sSelectedPower = NULL;
	eaClearStruct(&sppPowers, parse_CBugPowerDef);
	eaPush(&sppPowers, StructCreate(parse_CBugPowerDef)); // push a dummy empty power

	sePowersListFlags = eFlags;

	if(e && e->pChar)
	{
		size = eaSize(&e->pChar->ppPowers); // Active Powers
		for (i=0; i<size; i++)
		{
			PowerDef * pDef = GET_REF(e->pChar->ppPowers[i]->hDef);
			CBugPowerDef *pCurPower;
			if(!pDef)
				continue;

			if( (eFlags & kBugPowerListFlag_MustHavePurpose) && !pDef->ePurpose )
				continue;
			else if( (eFlags & kBugPowerListFlag_MustNotHavePurpose) && pDef->ePurpose )
				continue;

			pCurPower = StructCreate(parse_CBugPowerDef);
			pCurPower->powerDefName = strdup(pDef->pchName);
			eaPush(&sppPowers, pCurPower);
		}
	}
}

void cPowers_PowerDataString(char **estr, const CBugPowerDef *selected)
{
	if (!selected)
		return;
	estrCopy2(estr, selected->powerDefName);
}

void cPowers_PowerDisplayName(char **estr, const CBugPowerDef *selected)
{
	PowerDef *pDef;
	if (!selected)
		return;
	pDef = powerdef_Find(selected->powerDefName);
	if (pDef)
	{
		estrCopy2(estr, powerdef_GetLocalName(pDef));
	}
}


void cPowers_PowerName(UIComboBox *cb, S32 iRow, bool bInBox, UserData userData, char **ppchOutput)
{
	if (iRow < 0)
	{
		estrCopy2(ppchOutput, cb->defaultDisplay ? cb->defaultDisplay : "");
	}
	else
	{
		CBugPowerDef * pCBugDef = ((CBugPowerDef**)(*cb->model))[iRow];
		PowerDef *pDef = powerdef_Find(pCBugDef->powerDefName);
		
		if (pDef && GET_REF(pDef->msgDisplayName.hMessage))
			FormatGameMessage(ppchOutput, GET_REF(pDef->msgDisplayName.hMessage), STRFMT_END);
		else if (pDef && pDef->pchName)
			estrCopy2(ppchOutput, pDef->pchName);
		else
			FormatGameMessageKey(ppchOutput, "Ticket.Powers.UnknownPower", STRFMT_END);

	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryPower_ResetList");
void CategoryPowers_ResetList(void)
{
	sbSessionPowerInit = false;
	sSelectedPower = NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryPower_GetListWithFlags");
void CategoryPowers_GetListWithFlags(SA_PARAM_NN_VALID UIGen *pGen, U32 eFlags)
{
	if (!sbSessionPowerInit || eFlags != sePowersListFlags)
	{
		createPowersList(eFlags);
		//sSelectedPower = sppPowers[0];
		sbSessionPowerInit = true;
	}
	ui_GenSetList(pGen, &sppPowers, parse_CBugPowerDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryPower_GetList");
void CategoryPowers_GetList(SA_PARAM_NN_VALID UIGen *pGen)
{
	CategoryPowers_GetListWithFlags(pGen, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryPower_GetText");
SA_RET_OP_STR const char * CategoryPowers_GetRowDisplayText(SA_PARAM_NN_VALID CBugPowerDef *power)
{
	if (power->displayString)
		return power->displayString;

	if (power->powerDefName == NULL)
		FormatGameMessageKey(&power->displayString, "Ticket.Powers.None", STRFMT_END);
	else
		cPowers_PowerDisplayName(&power->displayString, power);
	return power->displayString;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryPower_GetSelected");
SA_RET_OP_STR const char * CategoryPowers_GetSelected(void)
{
	if (sSelectedPower)
	{
		return CategoryPowers_GetRowDisplayText(sSelectedPower);
	}
	estrClear(&sDataString);
	FormatGameMessageKey(&sDataString, "Ticket.Powers.Select", STRFMT_END);
	return sDataString;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryPower_SetSelected");
bool CategoryPowers_SetSelected(SA_PARAM_OP_VALID CBugPowerDef *pchCategoryPower)
{
	sSelectedPower = pchCategoryPower;
	//Category_SendTicketUpdateRequest();
	//TicketUI_DisableButton("Ticket_Update", sSelectedPower == NULL);
	return true;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryPower_Reset");
void CategoryPowers_Reset(void)
{
	sSelectedPower = NULL;
	sbSessionPowerInit = false;
	eaDestroyStruct(&sppPowers, parse_CBugPowerDef);
}

CBugPowerDef * CategoryPower_GetSelectedStruct(void)
{
	if (sSelectedPower && sSelectedPower->powerDefName == NULL)
		return NULL;
	return sSelectedPower;
}

///////////////////////////////
// Chat log

AUTO_STRUCT;
typedef struct CBugChatLog
{
	//char **ppchLines;
	ChatLogEntry **ppChatEntries;
} CBugChatLog;

extern ParseTable parse_ChatLogEntry[];
#define TYPE_parse_ChatLogEntry ChatLogEntry
extern ParseTable parse_ChatLogFilter[];
#define TYPE_parse_ChatLogFilter ChatLogFilter

#define TICKET_CHATLOG_NUMBER_LINES 50
#define TICKET_MAXIMUM_CHAT_AGE 30*60 // 30 minutes
void CBugChatLogCallback(void **ppStruct, ParseTable** ppti, char **estrLabel)
{
	ChatLogEntry **ppChatLog = NULL;
	static ChatLogFilter filter = {0};
	static bool bFilterInitialized = false;
	int i;

	if (!bFilterInitialized)
	{
		eaiPush(&filter.pTypeFilter, kChatLogEntryType_Channel);
		for (i=kChatLogEntryType_Guild; i<kChatLogEntryType_Count; i++)
			eaiPush(&filter.pTypeFilter, i);
		bFilterInitialized = true;
	}
	filter.uStartTime = timeSecondsSince2000() - TICKET_MAXIMUM_CHAT_AGE;

	ChatLog_FilterMessages(&ppChatLog, &filter);
	if (ppChatLog)
	{
		CBugChatLog *pChatLog = StructAlloc(parse_CBugChatLog);
		int size;
		size = eaSize(&ppChatLog);
		for (i = 0; i < size; i++)
		{
			ChatLogEntry *pEntry = ppChatLog[i];
			eaPush(&pChatLog->ppChatEntries, StructClone(parse_ChatLogEntry, pEntry));
		}
		*ppStruct = pChatLog;
		*ppti = parse_CBugChatLog;
	}
}

////////////////////////////////
// NPC / Mob

static EntityRef sTargetRef = 0;
static char sTargetName[MAX_NAME_LEN] = "";

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void setTicketTargetName(EntityRef targetRef, ACMD_SENTENCE entityName)
{
	if (entityName)
		strcpy(sTargetName, entityName);
	else
		sTargetName[0] = 0;

	sTargetRef = targetRef;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryNPC_GetName");
SA_RET_NN_STR char * CategoryNPC_GetTicketTargetName(void)
{
	if (sTargetName[0])
		return sTargetName;
	return "[No Target]";
}

AUTO_EXPR_FUNC(UIGen);
void CategoryNPC_TargetUpdate(void)
{
	ServerCmd_gslGrabEntityTarget();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategoryNPC_Reset");
void CategoryNPC_Reset(void)
{
	sTargetName[0] = 0;
	sTargetRef = 0;
}

////////////////////////////////
// General Stuff

AUTO_RUN;
void cBug_RegisterCategories(void)
{
	cBug_AddCategoryChoiceCallbacks("CBug.Category.GameSupport.Missions", cMissions_MissionDataString, cMissions_MissionDataString, 
		CategoryMission_GetSelectedStruct, "Ticket_CategoryMission");
	cBug_AddCategoryChoiceCallbacks("CBug.Category.GameSupport.Powers", cPowers_PowerDataString, cPowers_PowerDisplayName, 
		CategoryPower_GetSelectedStruct, "Ticket_CategoryPower");
	cBug_AddCategoryChoiceCallbacks("CBug.Category.GameSupport.Items", cItems_ItemDataString, cItems_ItemDisplayName, 
		CategoryItem_GetSelectedStruct, "Ticket_CategoryItem");

	// Removing this one
	//cBug_AddCategoryChoiceCallbacks("CBug.Category.InGame.Environment", NULL, NULL, NULL, "Ticket_CategoryNPC");

	// Generic callback for chat log
	cBugAddCustomDataCallback(TICKETDATA_ALL_CATEGORY_STRING,CBugChatLogCallback);
}

AUTO_RUN;
void cBug_AutoRegister(void)
{
	ui_GenInitStaticDefineVars(BugPowerListFlagEnum, "BugPowerList");
}

#include "gclBugCustomData_c_ast.c"
