/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "GameEventDebugger.h"

#include "UICore.h"
#include "UISkin.h"
#include "UIBoxSizer.h"
#include "UIWindow.h"
#include "UIScrollbar.h"
#include "UILabel.h"
#include "UIButton.h"
#include "UICheckButton.h"
#include "UIList.h"
#include "UISlider.h"
#include "UIMenu.h"
#include "GameEvent.h"
#include "gameevent_h_ast.h" // GameEventEnum
#include "earray.h"
#include "Prefs.h"
#include "cmdparse.h"
#include "GfxFont.h"
#include "GfxSpriteText.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

GCC_SYSTEM

#ifndef NO_EDITORS

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static GameEvent **s_eaGameEventArray = NULL;
static GameEventParticipants **s_eaGameEventParticipantsArray = NULL;

//
// Root Event Debug UI Structure
//
typedef struct EventDebugUI {
	UIWindow *mainWindow;

	UIList *eventList;
	UIList *sourceList;
	UIList *targetList;

	UISlider *opacitySlider;
} EventDebugUI;

static EventDebugUI eDebugUI = { 0 };

static void eDebugSetWindowOpacity(F32 opacity)
{
	if(eDebugUI.mainWindow)
	{
		U8 scaled;
		CLAMP(opacity, 0.0, 1.0);

		scaled = (U8)(opacity * 255.0); 
		eDebugUI.mainWindow->widget.pOverrideSkin->background[0].a = scaled;
	}
}

static bool eDebugUICloseCallback(UIAnyWidget *widget, UserData userdata_unused)
{
	GamePrefStoreFloat("EDebug.x", ui_WidgetGetX((UIWidget*)widget));
	GamePrefStoreFloat("EDebug.y", ui_WidgetGetY((UIWidget*)widget));
	GamePrefStoreFloat("EDebug.w", ui_WidgetGetWidth((UIWidget*)widget));
	GamePrefStoreFloat("EDebug.h", ui_WidgetGetHeight((UIWidget*)widget));

	if(eDebugUI.mainWindow && UI_GET_SKIN(eDebugUI.mainWindow))
	{
		F32 scaled = UI_GET_SKIN(eDebugUI.mainWindow)->background[0].a / 255.0;

		GamePrefStoreFloat("EDebug.windowOpacity", scaled);
	}

	if(eDebugUI.eventList)
	{
		S32 i;
		for(i = 0; i < eaSize(&eDebugUI.eventList->eaColumns); i++)
		{
			static char *hidden_str = NULL;
			UIListColumn *col = eDebugUI.eventList->eaColumns[i];
			estrPrintf(&hidden_str, "EDebug.HiddenEventColumns.%s", ui_ListColumnGetTitle(col));
			GamePrefStoreInt(hidden_str, col->bHidden);
		}
	}

	if(eDebugUI.sourceList)
	{
		S32 i;
		for(i = 0; i < eaSize(&eDebugUI.sourceList->eaColumns); i++)
		{
			static char *hidden_str = NULL;
			UIListColumn *col = eDebugUI.sourceList->eaColumns[i];
			estrPrintf(&hidden_str, "EDebug.HiddenSourceColumns.%s", ui_ListColumnGetTitle(col));
			GamePrefStoreInt(hidden_str, col->bHidden);
		}
	}

	if(eDebugUI.targetList)
	{
		S32 i;
		for(i = 0; i < eaSize(&eDebugUI.targetList->eaColumns); i++)
		{
			static char *hidden_str = NULL;
			UIListColumn *col = eDebugUI.targetList->eaColumns[i];
			estrPrintf(&hidden_str, "EDebug.HiddenTargetColumns.%s", ui_ListColumnGetTitle(col));
			GamePrefStoreInt(hidden_str, col->bHidden);
		}
	}

	ZeroStruct(&eDebugUI);

	return 1;
}

static GameEventParticipant **s_DummyGameEventParticipantList = NULL;
static void eventSelectedCB(UIAnyWidget *widget_unused, UserData data_unused)
{
	S32 row = ui_ListGetSelectedRow(eDebugUI.eventList);
	if(row >= 0 && row < eaSize(&s_eaGameEventArray))
	{
		ui_ListSetSelectedRowAndCallback(eDebugUI.sourceList, eaSize(&s_eaGameEventParticipantsArray[row]->eaSources) ? 0 : -1);
		ui_ListSetModel(eDebugUI.sourceList, parse_GameEventParticipant, &s_eaGameEventParticipantsArray[row]->eaSources);
		ui_ListSetSelectedRowAndCallback(eDebugUI.targetList, eaSize(&s_eaGameEventParticipantsArray[row]->eaTargets) ? 0 : -1);
		ui_ListSetModel(eDebugUI.targetList, parse_GameEventParticipant, &s_eaGameEventParticipantsArray[row]->eaTargets);
	}
	else
	{
		ui_ListSetSelectedRowAndCallback(eDebugUI.sourceList, -1);
		ui_ListSetModel(eDebugUI.sourceList, parse_GameEventParticipant, NULL);
		ui_ListSetSelectedRowAndCallback(eDebugUI.targetList, -1);
		ui_ListSetModel(eDebugUI.targetList, parse_GameEventParticipant, NULL);
	}
}

static void togglePauseCB(UIAnyWidget *widget_unused, UserData data_unused)
{
	ServerCmd_timeStepPause();
}

static void opacityChangedCB(UIAnyWidget *widget_unused, bool finished_unused, UserData data_unused)
{
	eDebugSetWindowOpacity(ui_SliderGetValue(eDebugUI.opacitySlider));
}

static void clearEventsCB(UIAnyWidget *widget_unused, UserData data_unused)
{
	if(eDebugUI.eventList)
		ui_ListSetSelectedRowAndCallback(eDebugUI.eventList, -1);
	if(eDebugUI.sourceList)
	{
		ui_ListSetSelectedRowAndCallback(eDebugUI.sourceList, -1);
		ui_ListSetModel(eDebugUI.sourceList, parse_GameEventParticipant, NULL);
	}
	if(eDebugUI.targetList)
	{
		ui_ListSetSelectedRowAndCallback(eDebugUI.targetList, -1);
		ui_ListSetModel(eDebugUI.targetList, parse_GameEventParticipant, NULL);
	}
	
	eaClear(&s_eaGameEventArray);
	eaClear(&s_eaGameEventParticipantsArray);
}

static UIListColumn *AddEventListColumn(const char *label, const char *field)
{
	if(eDebugUI.eventList)
	{
		static char *hidden_str = NULL;
		UIListColumn *col = ui_ListColumnCreate(UIListPTName, label, (intptr_t)field, NULL);
		ui_ListColumnSetWidth(col, true, 1.0f);
		estrPrintf(&hidden_str, "EDebug.HiddenEventColumns.%s", label);
		ui_ListColumnSetHidden(col, GamePrefGetInt(hidden_str, 0));
		ui_ListAppendColumn(eDebugUI.eventList, col);
		return col;
	}
	return NULL;
}

static void AddEventParticipantListColumn(const char *label, const char *field)
{
	if(eDebugUI.sourceList)
	{
		static char *hidden_str = NULL;
		UIListColumn *col = ui_ListColumnCreate(UIListPTName, label, (intptr_t)field, NULL);
		ui_ListColumnSetWidth(col, true, 1.0f);
		estrPrintf(&hidden_str, "EDebug.HiddenSourceColumns.%s", label);
		ui_ListColumnSetHidden(col, GamePrefGetInt(hidden_str, 0));
		ui_ListAppendColumn(eDebugUI.sourceList, col);
	}
	if(eDebugUI.targetList)
	{
		static char *hidden_str = NULL;
		UIListColumn *col = ui_ListColumnCreate(UIListPTName, label, (intptr_t)field, NULL);
		ui_ListColumnSetWidth(col, true, 1.0f);
		estrPrintf(&hidden_str, "EDebug.HiddenTargetColumns.%s", label);
		ui_ListColumnSetHidden(col, GamePrefGetInt(hidden_str, 0));
		ui_ListAppendColumn(eDebugUI.targetList, col);
	}
}

// Toggle command to hide or show the Event Debugger
static void eDebugUIToggle(void)
{
	if(eDebugUI.mainWindow)
	{
		// Destroy it all
		ui_WindowClose(eDebugUI.mainWindow);
	}
	else
	{
		F32 x, y, w, h, opacity;

		globCmdParse("eventlog_Debug 1");

		// Create the main window
		x = GamePrefGetFloat("EDebug.x", 5);
		y = GamePrefGetFloat("EDebug.y", 5);
		w = GamePrefGetFloat("EDebug.w", 600);
		h = GamePrefGetFloat("EDebug.h", 600);
		opacity = GamePrefGetFloat("EDebug.windowOpacity", 0.5);

		eDebugUI.mainWindow = ui_WindowCreate("Event Debugger", x, y, w, h);
		ui_WindowSetDimensions(eDebugUI.mainWindow, w, h, 500, 200);
		eDebugUI.mainWindow->widget.pOverrideSkin = ui_SkinCreate(NULL);

		eDebugUI.opacitySlider = ui_SliderCreate(0, 0, 120, 0.0, 1.0, opacity);
		ui_SliderSetChangedCallback(eDebugUI.opacitySlider, opacityChangedCB, /*changedData=*/NULL);
		ui_SliderSetPolicy(eDebugUI.opacitySlider, UISliderContinuous);

		eDebugSetWindowOpacity(opacity);
		ui_WindowSetCloseCallback(eDebugUI.mainWindow, eDebugUICloseCallback, NULL);

		eDebugUI.eventList = ui_ListCreate(parse_GameEvent, &s_eaGameEventArray, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
		ui_ListSetAutoColumnContextMenu(eDebugUI.eventList, true);
		ui_ListSetSelectedCallback(eDebugUI.eventList, eventSelectedCB, NULL);

		ui_ListColumnSetCanHide(AddEventListColumn("Type", "Type"), false);
		AddEventListColumn("Pos", "Pos");
		AddEventListColumn("Contact Name", "ContactName");
		AddEventListColumn("Store Name", "StoreName");
		AddEventListColumn("Mission Ref String", "MissionRefString");
		AddEventListColumn("Mission Category Name", "MissionCategoryName");
		AddEventListColumn("Item Name", "ItemName");
		AddEventListColumn("Item Categories", "ItemCategories");
		AddEventListColumn("Bag Type", "BagType");
		AddEventListColumn("Gem Name", "GemName");
		AddEventListColumn("Cutscene Name", "CutsceneName");
		AddEventListColumn("FSM Name", "FSMName");
		AddEventListColumn("FSM State Name", "FsmStateName");
		AddEventListColumn("Item Assignment Name", "ItemAssignmentName");
		AddEventListColumn("Item Assignment Outcome", "ItemAssignmentOutcome");
		AddEventListColumn("Power Name", "PowerName");
		AddEventListColumn("Power Event Name", "PowerEventName");
		AddEventListColumn("Damage Type", "DamageType");
		AddEventListColumn("Dialog Name", "DialogName");
		AddEventListColumn("Nemesis Name", "NemesisName");
		AddEventListColumn("Nemesis ID", "NemesisID");
		AddEventListColumn("Emote Name", "EmoteName");
		AddEventListColumn("Message", "Message");
		AddEventListColumn("Part Of UGC Project?", "PartOfUGCProject");
		AddEventListColumn("Encounter State", "EncState");
		AddEventListColumn("Mission Type", "MissionType");
		AddEventListColumn("Root Mission?", "IsRootMission");
		AddEventListColumn("Mission State", "MissionState");
		AddEventListColumn("Mission Lockout State", "MissionLockoutState");
		AddEventListColumn("Nemesis State", "NemesisState");
		AddEventListColumn("Health State", "HealthState");
		AddEventListColumn("Victory Type", "VictoryType");
		AddEventListColumn("Minigame Type", "MinigameType");
		AddEventListColumn("PvP Queue Match Result", "PvPQueueMatchResult");
		AddEventListColumn("PvP Event", "PvPEvent");
		AddEventListColumn("Count", "Count");
		AddEventListColumn("Map Name", "MapName");
		AddEventListColumn("Door Key", "DoorKey");

		eDebugUI.sourceList = ui_ListCreate(parse_GameEventParticipant, NULL, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
		ui_ListSetAutoColumnContextMenu(eDebugUI.sourceList, true);

		eDebugUI.targetList = ui_ListCreate(parse_GameEventParticipant, NULL, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
		ui_ListSetAutoColumnContextMenu(eDebugUI.targetList, true);
		
		AddEventParticipantListColumn("Actor Name", "ActorName");
		AddEventParticipantListColumn("Critter Name", "CritterName");
		AddEventParticipantListColumn("Critter Group Name", "CritterGroupName");
		AddEventParticipantListColumn("Encounter Name", "EncounterName");
		AddEventParticipantListColumn("Object Name", "ObjectName");
		AddEventParticipantListColumn("Rank", "Rank");
		AddEventParticipantListColumn("Region Type", "RegionType");
		AddEventParticipantListColumn("Nemesis Type", "NemesisType");
		AddEventParticipantListColumn("Is Player?", "IsPlayer");
		AddEventParticipantListColumn("Faction Name", "FactionName");
		AddEventParticipantListColumn("Allegiance Name", "AllegianceName");
		AddEventParticipantListColumn("Class Name", "ClassName");
		AddEventParticipantListColumn("Has Credit", "HasCredit");
		AddEventParticipantListColumn("Credit Percentage", "CreditPercentage");
		AddEventParticipantListColumn("Has Team Credit", "HasTeamCredit");
		AddEventParticipantListColumn("Team Credit Percentage", "TeamCreditPercentage");

		if(eaSize(&s_eaGameEventArray) > 0)
			ui_ListSetSelectedRowAndCallback(eDebugUI.eventList, 0);

		// Setup sizers for widget layout
		{
			UIBoxSizer *mainVerticalBoxSizer = ui_BoxSizerCreate(UIVertical);
			UIBoxSizer *topHorizontalBoxSizer = ui_BoxSizerCreate(UIHorizontal);
			UIBoxSizer *participantsHorizontalBoxSizer = ui_BoxSizerCreate(UIHorizontal);
			UIBoxSizer *sourcesVerticalBoxSizer = ui_BoxSizerCreate(UIVertical);
			UIBoxSizer *targetsVerticalBoxSizer = ui_BoxSizerCreate(UIVertical);

			ui_WidgetSetSizer(UI_WIDGET(eDebugUI.mainWindow), UI_SIZER(mainVerticalBoxSizer));

			ui_BoxSizerAddSizer(mainVerticalBoxSizer, UI_SIZER(topHorizontalBoxSizer), 0, UIWidth, 2);
			ui_BoxSizerAddWidget(mainVerticalBoxSizer, UI_WIDGET(eDebugUI.eventList), 1, UIWidth, 2);
			ui_BoxSizerAddSizer(mainVerticalBoxSizer, UI_SIZER(participantsHorizontalBoxSizer), 1, UIWidth, 2);

			ui_BoxSizerAddWidget(topHorizontalBoxSizer, UI_WIDGET(ui_LabelCreate("Events:", 0, 0)), 0, UINoDirection, 2);
			ui_BoxSizerAddFiller(topHorizontalBoxSizer, 1);
			ui_BoxSizerAddWidget(topHorizontalBoxSizer, UI_WIDGET(ui_ButtonCreate("Toggle Pause", 0, 0, togglePauseCB, /*clickedData=*/NULL)), 0, UINoDirection, 2);
			ui_BoxSizerAddFiller(topHorizontalBoxSizer, 1);
			ui_BoxSizerAddWidget(topHorizontalBoxSizer, UI_WIDGET(ui_LabelCreate("Window Opacity:", 0, 0)), 0, UINoDirection, 2);
			ui_BoxSizerAddWidget(topHorizontalBoxSizer, UI_WIDGET(eDebugUI.opacitySlider), 0, UINoDirection, 2);
			ui_BoxSizerAddFiller(topHorizontalBoxSizer, 1);
			ui_BoxSizerAddWidget(topHorizontalBoxSizer, UI_WIDGET(ui_ButtonCreate("Clear Events", 0, 0, clearEventsCB, /*clickedData=*/NULL)), 0, UINoDirection, 2);

			ui_BoxSizerAddSizer(participantsHorizontalBoxSizer, UI_SIZER(sourcesVerticalBoxSizer), 1, UIHeight, 2);
			ui_BoxSizerAddSizer(participantsHorizontalBoxSizer, UI_SIZER(targetsVerticalBoxSizer), 1, UIHeight, 2);

			ui_BoxSizerAddWidget(sourcesVerticalBoxSizer, UI_WIDGET(ui_LabelCreate("Sources:", 0, 0)), 0, UILeft, 2);
			ui_BoxSizerAddWidget(sourcesVerticalBoxSizer, UI_WIDGET(eDebugUI.sourceList), 1, UIWidth, 2);

			ui_BoxSizerAddWidget(targetsVerticalBoxSizer, UI_WIDGET(ui_LabelCreate("Targets:", 0, 0)), 0, UILeft, 2);
			ui_BoxSizerAddWidget(targetsVerticalBoxSizer, UI_WIDGET(eDebugUI.targetList), 1, UIWidth, 2);
		}

		ui_WindowShow(eDebugUI.mainWindow);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CLIENTONLY;
void eDebug(void)
{
	eDebugUIToggle();
}

#endif // #endif NO_EDITORS

void gameeventdebug_SendEvent(GameEvent *pGameEvent, GameEventParticipants *pGameEventParticipants)
{
#ifndef NO_EDITORS
	S32 row = -1;
	F32 scrollX = -1.0f;
	F32 scrollY = -1.0f;

	if(eDebugUI.eventList)
	{
		row = ui_ListGetSelectedRow(eDebugUI.eventList);
		ui_ListGetScrollbarPosition(eDebugUI.eventList, &scrollX, &scrollY);
	}

	eaInsert(&s_eaGameEventArray, StructClone(parse_GameEvent, pGameEvent), 0);
	eaInsert(&s_eaGameEventParticipantsArray, StructClone(parse_GameEventParticipants, pGameEventParticipants), 0);

	if(eDebugUI.eventList)
	{
		ui_ListSetSelectedRowAndCallback(eDebugUI.eventList, row + 1); // if previous selected row was -1, then we will still select the next row, which will be 0
		ui_ListSetScrollbar(eDebugUI.eventList, scrollX, scrollY + gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
	}
#endif // #endif NO_EDITORS
}
