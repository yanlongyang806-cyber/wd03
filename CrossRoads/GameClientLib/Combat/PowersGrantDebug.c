/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "cmdparse.h"
#include "file.h"
#include "ResourceManager.h"
#include "TimedCallback.h"

#include "Powers.h"
#include "Powers_h_ast.h"
#include "PowerAnimFX.h"

#include "inputKeyBind.h"

#include "UIButton.h"
#include "UIList.h"
#include "UIFilteredList.h"
#include "UITabs.h"
#include "UIWindow.h"

static KeyBindProfile s_PowersDebugBinds = { "Powers Debug", __FILE__, NULL, true, true };

static UIWindow *s_Window;

static void GrantPowerClickedCallback(UIFilteredList *list, char *button)
{
	PowerDef *pDef = ui_FilteredListGetSelectedObject(list);
	if (pDef)
	{
		char command[1024];
		sprintf(command, "addpower \"%s\"", pDef->pchName);
		globCmdParse(command);
		sprintf(command, "bind %s +power_exec \"%s\"", button, pDef->pchName);
		keybind_BindKeyInProfile(&s_PowersDebugBinds, button, command);
	}
	else
		keybind_BindKeyInProfile(&s_PowersDebugBinds, button, NULL);
}

static void EditArtFileClickedCallback(UIButton *button, UITabGroup *tabs)
{
	UITab *ptab = ui_TabGroupGetActive(tabs);
	if(ptab)
	{
		//UIList *plist = (UIList*)eaGet(&ptab->eaChildren,0);
		UIFilteredList *plist = (UIFilteredList*)eaGet(&ptab->eaChildren,0);
		if(plist)
		{
			PowerDef *pdef = ui_FilteredListGetSelectedObject(plist);
			if(pdef && GET_REF(pdef->hFX))
			{
				PowerAnimFX *pafx = GET_REF(pdef->hFX);
				char cFileNameBuf[512];
				if(fileLocateWrite(pafx->cpchFile, cFileNameBuf))
				{
					fileOpenWithEditor(cFileNameBuf);
				}
			}
		}
	}
}

static void GetPowerArtFileName(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	PowerDef *pDef = eaGet(list->peaModel, row);
	PowerAnimFX *pAnimDef = pDef ? GET_REF(pDef->hFX) : NULL;
	if (pAnimDef)
		estrPrintf(output, "%s", pAnimDef->cpchFile ? pAnimDef->cpchFile : "Error in PowerAnimFX Def");
	else
		estrPrintf(output, "No Art");
}

static void PowersGrantDebugDialogCreate(void)
{
	const char *buttons[] = {"BB", "XB", "YB", "LTrigger+AB", "LTrigger+BB", "LTrigger+XB", "LTrigger+YB"};
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct("PowerDef");
	UITabGroup *tabs = ui_TabGroupCreate(0, 0, 0, 0);
	UIButton *artedit = ui_ButtonCreate("Edit Art File",0,0,EditArtFileClickedCallback,tabs);
	S32 i;
	s_Window = ui_WindowCreate("Grant Powers", 0, 0, 350, 400);

	ui_WidgetSetPositionEx(UI_WIDGET(artedit),0,0,0,0,UIBottomLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(artedit), 1.f, 0.1f, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(s_Window,artedit);

	tabs->bEqualWidths = true;
	ui_WidgetSetPadding(UI_WIDGET(tabs), UI_STEP, UI_STEP);
	ui_WidgetSetDimensionsEx(UI_WIDGET(tabs), 1.f, 0.9f, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(s_Window, tabs);

	for (i = 0; i < ARRAY_SIZE_CHECKED(buttons); i++)
	{
		UITab *tab = ui_TabCreate(buttons[i]);
		UIFilteredList *list = ui_FilteredListCreate(NULL, UIListPTName, parse_PowerDef, &pArray->ppReferents, (intptr_t)"Name", 16);
		//ui_ListAppendColumn(list->pList, ui_ListColumnCreate(UIListPTName, "Name", (intptr_t)"Name", NULL));
		ui_ListAppendColumn(list->pList, ui_ListColumnCreate(UIListTextCallback, "Art File", (intptr_t)GetPowerArtFileName, NULL));
		list->pList->eaColumns[0]->bAutoSize = true;
		list->pList->eaColumns[1]->bAutoSize = true;
		ui_FilteredListSetSelectedCallback(list, GrantPowerClickedCallback, (char *)buttons[i]);
		//ui_ListSetSelectedCallback(list, GrantPowerClickedCallback, (char *)buttons[i]);
		ui_WidgetSetDimensionsEx(UI_WIDGET(list), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
		ui_TabAddChild(tab, list);
		ui_TabGroupAddTab(tabs, tab);
	}

	s_PowersDebugBinds.ePriority = InputBindPriorityDevelopment;
	keybind_PushProfile(&s_PowersDebugBinds);
	ui_WindowShow(s_Window);
}

static bool s_bRequestedPowers = false;
static bool s_bTimedCallback = false;

static void PowersGrantDebugDialogCreateCallback(TimedCallback *pCallback, F32 fTime, UserData data)
{
	PowersGrantDebugDialogCreate();
	s_bTimedCallback = false;
}

// Debug tool for listing powers and powerart, and assigning powers to Xbox controller buttons
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug, Powers);
void PowersGrantDebugDialog(void)
{
	if(isDevelopmentMode())
	{
		if(!s_bRequestedPowers)
		{
			resRequestAllResourcesInDictionary("PowerDef");
			TimedCallback_Run(PowersGrantDebugDialogCreateCallback,NULL,5.f);
			s_bRequestedPowers = true;
			s_bTimedCallback = true;
		}
		else if(!s_bTimedCallback)
		{
			PowersGrantDebugDialogCreate();
		}
	}
}

// externed in the one place it's called from to avoid making a one line header file.
void PowersGrantDebugDialogDisable(void)
{
	if(s_Window!=NULL)
	{
		ui_WindowHide(s_Window);
		s_Window = NULL;
	}
}