/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclentity.h"
#include "gameclientlib.h"
#include "gclBaseStates.h"
#include "gfxdebug.h"
#include "gfxprimitive.h"
#include "graphicslib.h"
#include "inputmouse.h"
#include "EntDebugMenu.h"
#include "cmdparse.h"
#include "strings_opt.h"
#include "file.h"
#include "stringutil.h"
#include "GlobalStateMachine.h"
#include "GfxConsole.h"
#include "EditorManager.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/gclEntDebugMenu_c_ast.h"

#if !_XBOX
#include "wininclude.h"
#endif

GCC_SYSTEM

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct DebugState
{
	int cursel;
	DebugMenuItem* menuItem;
	StashTable menuItemNameTable;
	int	keyDelayMsecStart;
	int	keyDelayMsecWait;
	int	firstLine;
	int	windowHeight;
	int	openingTime;
	int	lastTime;
	int	closingTime;
	int	old_mouse_x, old_mouse_y;
	int	mouseMustMove;
	int	moveMouseToSel;
	int	retainMenuState;
	int	motdVisible;
	char motdText[1000];

	U32 upHeld : 1;
	U32 pageUpHeld : 1;
	U32 downHeld : 1;
	U32 pageDownHeld : 1;
} DebugState;

static DebugState s_DebugMenuState = {0};
CmdList DebugMenuCmdList = {0};

#define DEBUGMENU_WIDTH 500

AUTO_RUN;
void initEntDebug(void)
{
	s_DebugMenuState.moveMouseToSel = -1;
	s_DebugMenuState.retainMenuState = 1;
	s_DebugMenuState.motdVisible = 1;
	s_DebugMenuState.menuItemNameTable = stashTableCreateWithStringKeys(100, StashDeepCopyKeys | StashCaseSensitive);
}

static DebugMenuItem* getCurMenuItem(DebugMenuItem* item, int* selIndex)
{
	if (!item || !*selIndex)
		return item;

	if (item->open)
	{
		DebugMenuItem* selItem;
		int i, n = eaSize(&item->subItems);
		for (i = 0; i < n; i++)
		{
			DebugMenuItem* subItem = item->subItems[i];
			(*selIndex)--;
			if ((selItem = getCurMenuItem(subItem, selIndex)))
				return selItem;
		}
	}

	return NULL;
}

static void parseDebugCommands(char* commands){
	char* str;
	char* cur;
	char delim;
	char buffer[10000];

	strcpy(buffer, commands);

	cur = buffer;

	for(str = strsep2(&cur, "\n", &delim); str; str = strsep2(&cur, "\n", &delim)){
		globCmdParse(str);
		if(delim){
			cur[-1] = delim;
		}
	}
}

static StashElement menuItemGetElement(DebugMenuItem* item, int create){
	char buffer[10000];
	DebugMenuItem* curItem = item;
	StashElement element;

	STR_COMBINE_BEGIN(buffer);

	while(curItem){
		char* displayText = strstr(curItem->displayText, "$$");

		if(displayText)
			*displayText = 0;

		if(item != curItem){
			STR_COMBINE_CAT("::");
		}

		STR_COMBINE_CAT(curItem->displayText);

		curItem = curItem->parentItem;

		if(displayText)
			*displayText = '$';
	}

	STR_COMBINE_END(buffer);

	stashFindElement(s_DebugMenuState.menuItemNameTable, buffer, &element);

	if(!element && create){
		stashAddPointerAndGetElement(s_DebugMenuState.menuItemNameTable, buffer, 0, false, &element);
	}

	return element;
}

static void menuItemSetOpen(DebugMenuItem* item, int open){
	StashElement element = menuItemGetElement(item, 1);

	item->open = open;

	stashElementSetPointer(element, S32_TO_PTR(open));
}

static void menuItemInitOpenState(DebugMenuItem* item){
	if(s_DebugMenuState.retainMenuState){
		StashElement element = menuItemGetElement(item, 0);

		if(element){
			void* openPtr = stashElementGetPointer(element);
			item->open = PTR_TO_S32(openPtr) ? 1 : 0;
		}
		eaForEach(&item->subItems, menuItemInitOpenState);
	}
}

static int getTotalOpenItemCount(DebugMenuItem* item)
{
	int total = 1;
	if (!item)
	{
		return 0;
	}
	if (item->open)
	{
		int i, n = eaSize(&item->subItems);
		for (i = 0; i < n; i++)
			total += getTotalOpenItemCount(item->subItems[i]);
	}
	return total;
}

void entDebugInitMenu(void)
{
	s_DebugMenuState.cursel = min(s_DebugMenuState.cursel, getTotalOpenItemCount(s_DebugMenuState.menuItem) - 1);
	s_DebugMenuState.firstLine = min(s_DebugMenuState.firstLine, getTotalOpenItemCount(s_DebugMenuState.menuItem) - 1);
	s_DebugMenuState.closingTime = 0;
	s_DebugMenuState.mouseMustMove = 1;

	s_DebugMenuState.menuItem->open = 1;
	s_DebugMenuState.openingTime = 1;
	s_DebugMenuState.lastTime = timeGetTime();
}

static int displayMenuItem(DebugMenuItem* item, int* line, int left, int x, int top, float scale, U8 alpha){
	char buffer[1000];
	int curLine = (*line)++;
	int selected = curLine == s_DebugMenuState.cursel;
	int y;
	int flash = 0;
	int x_off = 0;
	float openAmount = 1;
	int openable;
	char* displayText;

	if(item->flash)
	{
		int timeDiff = timeGetTime() - item->flashStartTime;
		if(timeDiff < 300){
			if(timeDiff >= 0){
				openAmount = (float)timeDiff / 300;
				flash = 1;
				x_off = (sin(openAmount * PI)) * 10;
			}
		}else{
			item->flash = 0;
		}
	}

	openable = eaSize(&item->subItems) || !item->commandString;

	if(openable){
		sprintf(buffer, "%s", item->open ? "^1--" : "^2+");
	}else{
		wchar_t temp[4];
		sprintf(buffer, "^5");
		mbtowc(temp, "»", 1);
		strcat(buffer, WideToUTF8CharConvert(temp[0]));
	}

	y = top + (curLine + 1) * 16;

	if(curLine == s_DebugMenuState.moveMouseToSel){
		if(s_DebugMenuState.moveMouseToSel < s_DebugMenuState.firstLine){
			s_DebugMenuState.firstLine = s_DebugMenuState.moveMouseToSel - 4;
		}else{
			
#if !PLATFORM_CONSOLE
			{
				int mouse_x, mouse_y;
				int win_x, win_y;
				int new_x, new_y;

				mousePos(&mouse_x, &mouse_y);
				gfxGetActiveDevicePosition(&win_x, &win_y);

				new_x = win_x + mouse_x + GetSystemMetrics(SM_CXSIZEFRAME);
				new_y = win_y + y + GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CYSIZE);

				SetCursorPos(new_x, new_y);
			}
#endif

			s_DebugMenuState.cursel = s_DebugMenuState.moveMouseToSel;
			s_DebugMenuState.moveMouseToSel = -1;
		}
	}

	if(y < s_DebugMenuState.windowHeight){
		float startScale = item->commandString ? 1.2 : 1.5;

		printDebugString(	buffer,
			left + x + x_off,
			y,
			1.5 + 0.2 - 0.2 * min(1, 3 * scale),
			11,
			flash ? (gfxGetFrameCount() & 3 ? 1 : 0) : selected ? 7 : -1,
			0,
			alpha,
			NULL);

		displayText = strstr(item->displayText, "$$");

		if(displayText)
			displayText += 2;
		else
			displayText = item->displayText;

		sprintf(buffer, "%s%s", displayText, openable ? "^0..." : "");

		if(item->usedCount){
			strcatf(buffer, " ^r[x%d]", item->usedCount);
		}

		printDebugString(	buffer,
			left + x + x_off + 20,
			y,
			startScale + 0.2 - 0.2 * min(1, 3 * scale),
			11,
			flash ? (gfxGetFrameCount()& 3 ? 1 : 0) : selected ? 7 : -1,
			openable ? 9 : 0,
			alpha,
			NULL);

		if(selected){
			int w, h;
			int widthHeight[2];

			gfxGetActiveDeviceSize(&w, &h);

			printDebugString(buffer, 0, 0, 1, 1, -1, -1, 0, widthHeight);

			printDebugString(buffer, w - widthHeight[0] - 4, 13, 1, 1, -1, -1, 192, NULL);

			if(item->commandString){
				printDebugString(	displayText,
					left + x_off + 485,
					28,
					2.5,
					11,
					flash ? (gfxGetFrameCount() & 3 ? 1 : 0) : -1,
					0,
					alpha,
					NULL);

				printDebugString(	item->commandString,
					left + x_off + 495,
					60,
					1.8,
					11,
					flash ? (gfxGetFrameCount() & 3 ? 1 : 0) : 7,
					0,
					alpha,
					NULL);
			}
			else if(item->rolloverText && item->rolloverText[0]){
				printDebugString(	displayText,
					left + x_off + 485,
					28,
					2.5,
					11,
					flash ? (gfxGetFrameCount() & 3 ? 1 : 0) : -1,
					0,
					alpha,
					NULL);

				printDebugString(	item->rolloverText,
					left + 485,
					60,
					1.5,
					11,
					-1,
					0,
					alpha,
					NULL);
			}
			else{
				char* commands =	"^t50t"
					"Mouse Buttons^s\n"
					"  Left:\t^8^sOpen/Close Group ^0OR ^8Execute Command\n"
					"  Right:\t^8Open/Close Group ^0OR ^8Execute Command, Don't Close Menu\n"
					"  Middle:\t^8Close Group ^0OR ^8Close Selection's Group\n"
					"^l400l\n\n"
					"^nKeyboard^s\n"
					"  Arrows:\t^8^sNavigate\n"
					"  Enter:\t^8^sOpen/Close Group ^0OR ^8Execute Command\n"
					"  Space:\t^8^sOpen/Close Group ^0OR ^8Execute Command, Don't Close Menu\n";

				printDebugString(	"Menu Help",
					left+ 485,
					28,
					2.5,
					11,
					-1,
					0,
					alpha,
					NULL);

				printDebugString(	commands,
					left + 485,
					60,
					1.5,
					11,
					-1,
					0,
					alpha,
					NULL);
			}
		}
	}

	if(item->open)
	{
		int done = 0;
		float subScale = scale * openAmount;
		int i, n = eaSize(&item->subItems);
		for (i = 0; i < n; i++)
		{
			DebugMenuItem* subItem = item->subItems[i];
			if (!displayMenuItem(subItem, line, left, x + 25, top, subScale, alpha))
			{
				done = 1;
				break;
			}
		}
		if (done)
			return 0;
	}
	return 1;
}

static void debugmenu_StateOncePerFrame(void)
{
	int curTime;
	int w, h;

	if (!s_DebugMenuState.menuItem)
		return;

	curTime = (int)timeGetTime();

	gfxGetActiveDeviceSize(&w,&h);

	s_DebugMenuState.windowHeight = h;

	if(	(	s_DebugMenuState.downHeld || s_DebugMenuState.upHeld ||
		s_DebugMenuState.pageDownHeld || s_DebugMenuState.pageUpHeld) &&
		curTime - s_DebugMenuState.keyDelayMsecStart > s_DebugMenuState.keyDelayMsecWait)
	{
		int sel;

		s_DebugMenuState.mouseMustMove = 1;

		s_DebugMenuState.keyDelayMsecStart += s_DebugMenuState.keyDelayMsecWait;

		s_DebugMenuState.cursel +=	s_DebugMenuState.downHeld + 20 * s_DebugMenuState.pageDownHeld -
			(s_DebugMenuState.upHeld + 20 * s_DebugMenuState.pageUpHeld);

		while(curTime - s_DebugMenuState.keyDelayMsecStart > 30){
			s_DebugMenuState.keyDelayMsecStart += 30;
			s_DebugMenuState.cursel +=	s_DebugMenuState.downHeld + 20 * s_DebugMenuState.pageDownHeld -
				(s_DebugMenuState.upHeld + 20 * s_DebugMenuState.pageUpHeld);
		}

		s_DebugMenuState.keyDelayMsecWait = 30;

		if(s_DebugMenuState.cursel < 0){
			s_DebugMenuState.cursel = 0;
		}

		sel = s_DebugMenuState.cursel;

		while(s_DebugMenuState.cursel > 0 && !getCurMenuItem(s_DebugMenuState.menuItem, &sel)){
			s_DebugMenuState.cursel--;
			sel = s_DebugMenuState.cursel;
		}
	}

	if(s_DebugMenuState.menuItem){
		int line = 0;
		int maxLines = (h - 20) / 16;
		int totalLines = getTotalOpenItemCount(s_DebugMenuState.menuItem);
		int mouseLine;
		int mouse_x, mouse_y;
		int x_off = 0;
		int alpha = 255;
		int lines_from_bottom;

		mousePos(&mouse_x, &mouse_y);

		if(	!s_DebugMenuState.closingTime && !s_DebugMenuState.openingTime &&
			(	!s_DebugMenuState.mouseMustMove ||
			mouse_x != s_DebugMenuState.old_mouse_x ||
			mouse_y != s_DebugMenuState.old_mouse_y))
		{
			s_DebugMenuState.mouseMustMove = 0;

			mouseLine = s_DebugMenuState.firstLine + (mouse_y - 22) / 16;

			if(mouse_x < DEBUGMENU_WIDTH && mouse_x > 0 && mouseLine >= 0 && mouseLine < totalLines){
				s_DebugMenuState.cursel = mouseLine;
			}
		}

		if((s_DebugMenuState.cursel - s_DebugMenuState.firstLine) > maxLines - 5){
			s_DebugMenuState.firstLine += (s_DebugMenuState.cursel - s_DebugMenuState.firstLine) - (maxLines - 5);
		}

		if(s_DebugMenuState.cursel < s_DebugMenuState.firstLine + 4){
			s_DebugMenuState.firstLine -= 4 - (s_DebugMenuState.cursel - s_DebugMenuState.firstLine);
		}

		if(s_DebugMenuState.firstLine + maxLines > totalLines){
			s_DebugMenuState.firstLine = totalLines - maxLines;
		}

		if(s_DebugMenuState.firstLine < 0){
			s_DebugMenuState.firstLine = 0;
		}

		lines_from_bottom = max(0, totalLines - (s_DebugMenuState.firstLine + maxLines));

		s_DebugMenuState.old_mouse_x = mouse_x;
		s_DebugMenuState.old_mouse_y = mouse_y;

		if(s_DebugMenuState.closingTime){
			float enterTime = 150.0;
			s_DebugMenuState.closingTime += curTime - s_DebugMenuState.lastTime;

			if(s_DebugMenuState.closingTime > enterTime){
				float maxTime = min(enterTime, s_DebugMenuState.closingTime - 150);
				x_off = (2.0 - (1.0 + cos(maxTime * (PI) / enterTime))) * -250;
				alpha = ((enterTime - maxTime) * alpha) / enterTime;
			}
		}
		else if(s_DebugMenuState.openingTime){
			float enterTime = 100.0;
			s_DebugMenuState.openingTime += curTime - s_DebugMenuState.lastTime;

			if(s_DebugMenuState.openingTime < enterTime){
				float maxTime = min(enterTime, s_DebugMenuState.openingTime);
				x_off = -DEBUGMENU_WIDTH + (2.0 - (1.0 + cos(maxTime * (PI) / enterTime))) * 250;
				alpha = (maxTime * alpha) / enterTime;
			}else{
				s_DebugMenuState.openingTime = 0;
			}
		}

		s_DebugMenuState.lastTime = curTime;

		gfxDrawQuad(	x_off + 0, 0, x_off + DEBUGMENU_WIDTH, h, 1, CreateColor(
			(((alpha * 7) / 8) << 24) | 0x402040,
			(((alpha * 7) / 8) << 24) | 0x302040,
			(((alpha * 7) / 8) << 24) | 0x503060,
			(((alpha * 7) / 8) << 24) | 0x403080));
		gfxDrawQuad(x_off + DEBUGMENU_WIDTH, 0, x_off + DEBUGMENU_WIDTH + 1, h, 1, ARGBToColor((alpha << 24) | 0xff8000));
		gfxDrawQuad(x_off + DEBUGMENU_WIDTH + 1, 0, w, 34, 1, ARGBToColor(((alpha / 2) << 24) | 0xff6600));
		gfxDrawQuad(x_off + DEBUGMENU_WIDTH + 1, 34, w, 35, 1, ARGBToColor((alpha << 24) | 0xff8000));
		gfxDrawQuad(x_off + DEBUGMENU_WIDTH + 1, 35, w, 220, 1, ARGBToColor(((alpha / 2) << 24) | 0xffff00));
		gfxDrawQuad(x_off + DEBUGMENU_WIDTH + 1, 220, w, 221, 1, ARGBToColor((alpha << 24) | 0xff8000));

		if(!s_DebugMenuState.openingTime && !s_DebugMenuState.closingTime){
			gfxDrawQuad(	x_off + 2,
				19 + (s_DebugMenuState.cursel - s_DebugMenuState.firstLine + 1) * 16,
				x_off + DEBUGMENU_WIDTH - 2,
				25 + (s_DebugMenuState.cursel - s_DebugMenuState.firstLine) * 16, 1,
				ARGBToColor(0x40000000));

			gfxDrawQuad(	x_off + 3,
				20 + (s_DebugMenuState.cursel - s_DebugMenuState.firstLine + 1) * 16,
				x_off + DEBUGMENU_WIDTH - 3,
				24 + (s_DebugMenuState.cursel - s_DebugMenuState.firstLine) * 16, 1,
				ARGBToColor(s_DebugMenuState.mouseMustMove || (mouse_x > 0 && mouse_x < DEBUGMENU_WIDTH) ? 0x80ffff00 | abs(((gfxGetFrameCount() * 10) & 0xff) - 127) : 0x40ffff80));
		}

		displayMenuItem(s_DebugMenuState.menuItem, &line, x_off + 20, 0, 0 + 20 - s_DebugMenuState.firstLine * 16, 1, alpha);

		gfxDrawQuad(	x_off + 0, 0, x_off + DEBUGMENU_WIDTH, h / 6, 1,
			CreateColor(0,
			0,
			(((min(lines_from_bottom, 10) * alpha * 7 / 10) / 16) << 24),
			(((min(lines_from_bottom, 10) * alpha * 7 / 10) / 16) << 24)));

		gfxDrawQuad(	x_off + 0, h - h / 6, x_off + DEBUGMENU_WIDTH, h, 1,
			CreateColor((((min(s_DebugMenuState.firstLine, 10) * alpha * 7 / 10) / 16) << 24),
			(((min(s_DebugMenuState.firstLine, 10) * alpha * 7 / 10) / 16) << 24),
			0,
			0));

		if(s_DebugMenuState.closingTime > 300){
			StructDestroy(parse_DebugMenuItem, s_DebugMenuState.menuItem);
			s_DebugMenuState.menuItem = NULL;
			GSM_SwitchToState_Complex(GCL_GAMEPLAY);
			return;
		}

		if(!s_DebugMenuState.closingTime && !s_DebugMenuState.openingTime)
		{
			if(drawButton2D(DEBUGMENU_WIDTH + 5,
				232,
				130,
				25,
				1,
				s_DebugMenuState.retainMenuState ? "Keep State Is ^2ON" : "Keep State Is ^1OFF", 1, NULL, NULL))
			{
				s_DebugMenuState.retainMenuState ^= 1;
			}

			if(drawButton2D(DEBUGMENU_WIDTH + 140,
				232,
				130,
				25,
				1,
				s_DebugMenuState.motdVisible ? "MOTD Is ^2ON" : "MOTD Is ^1OFF", 1, NULL, NULL))
			{
				s_DebugMenuState.motdVisible ^= 1;
			}
		}

		if(s_DebugMenuState.motdVisible && s_DebugMenuState.motdText){
			printDebugString(	s_DebugMenuState.motdText,
				505 + DEBUGMENU_WIDTH * powf((float)max(0, s_DebugMenuState.closingTime - 150) / 150, 3),
				h - 260 - 300 * powf((float)(s_DebugMenuState.openingTime ? (100 - s_DebugMenuState.openingTime) : 0) / 100, 2),
				1.5,
				15,
				-1,
				-1,
				alpha,
				NULL);
		}
	}
}

AUTO_STRUCT AST_ENDTOK("\n");
typedef struct DebugMenuOverrideItem
{
	char *name; AST(STRUCTPARAM)
	char *command; AST(STRUCTPARAM)
	char *rolloverText; AST(STRUCTPARAM)
} DebugMenuOverrideItem;

typedef struct DebugMenuOverrideParent DebugMenuOverrideParent;

AUTO_STRUCT AST_STARTTOK("{") AST_ENDTOK("}");
typedef struct DebugMenuOverrideParent
{
	char *name; AST(STRUCTPARAM)
	char *rolloverText; AST(STRUCTPARAM)
	DebugMenuOverrideItem **subItem;
	DebugMenuOverrideParent **subMenu;
} DebugMenuOverrideParent;

static void copySubMenuOverride(DebugMenuItem *curMenu, DebugMenuOverrideParent *toCopy)
{
	S32 i;
	DebugMenuItem *newItem;
	for (i = 0; i < eaSize(&toCopy->subMenu); ++i)
	{
		newItem = StructCreate(parse_DebugMenuItem);
		newItem->displayText = StructAllocString(toCopy->subMenu[i]->name);
		newItem->rolloverText = StructAllocString(toCopy->subMenu[i]->rolloverText);
		copySubMenuOverride(newItem, toCopy->subMenu[i]);
		eaPush(&curMenu->subItems, newItem);
	}

	for (i = 0; i < eaSize(&toCopy->subItem); ++i)
	{
		newItem = StructCreate(parse_DebugMenuItem);
		newItem->displayText = StructAllocString(toCopy->subItem[i]->name);
		newItem->commandString = StructAllocString(toCopy->subItem[i]->command);
		newItem->rolloverText = StructAllocString(toCopy->subItem[i]->rolloverText);
		eaPush(&curMenu->subItems, newItem);
	}
}

AUTO_COMMAND ACMD_NAME("DebugMenu.Load") ACMD_CATEGORY(DebugMenu);
void DebugMenuLoadFromFile(const char *fname)
{
	DebugMenuOverrideParent *root = NULL;
	GSM_SwitchToState_Complex(GCL_GAMEPLAY "/" GCL_DEBUG_MENU);
	root = StructCreate(parse_DebugMenuOverrideParent);
	ParserLoadFiles(NULL, fname, NULL, PARSER_OPTIONALFLAG, parse_DebugMenuOverrideParent, root);

	StructDestroy(parse_DebugMenuItem, s_DebugMenuState.menuItem);
	s_DebugMenuState.menuItem = StructCreate(parse_DebugMenuItem);
	s_DebugMenuState.menuItem->displayText = StructAllocString(root->name);
	s_DebugMenuState.menuItem->rolloverText = StructAllocString(root->rolloverText);

	copySubMenuOverride(s_DebugMenuState.menuItem, root);
	entDebugInitMenu();

	StructDestroy(parse_DebugMenuOverrideParent, root);
}

AUTO_COMMAND ACMD_NAME(mmm);
void debugmenu_Open(void)
{
#define DEBUG_FILE_OVERRIDE "c:\\debugmenu.txt"
	if (GSM_IsStateActive(GCL_GAMEPLAY))
	{
		gfxDebugClearAccessLevelCmdWarnings();
		gfxDebugDisableAccessLevelWarnings(true);
		if (fileExists(DEBUG_FILE_OVERRIDE))
		{
			DebugMenuLoadFromFile(DEBUG_FILE_OVERRIDE);
		}
		else
		{
			GSM_SwitchToState_Complex(GCL_GAMEPLAY "/" GCL_DEBUG_MENU);
			StructDestroy(parse_DebugMenuItem, s_DebugMenuState.menuItem);
			s_DebugMenuState.menuItem = NULL;
			ServerCmd_RequestDebugMenu();
		}

		gfxConsoleEnable(0);		
	}
}

static void FixParentPointer(DebugMenuItem* item, DebugMenuItem* parent)
{
	int i, n = eaSize(&item->subItems);
	item->parentItem = parent;
	for (i = 0; i < n; i++)
		FixParentPointer(item->subItems[i], item);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE;
void RefreshDebugMenu(DebugMenuItem* rootItem)
{
	s_DebugMenuState.menuItem = StructCreate(parse_DebugMenuItem);
	s_DebugMenuState.menuItem->displayText = StructAllocString("Debug Menu");
	s_DebugMenuState.menuItem->open = 1;
	eaCopyStructs(&rootItem->subItems, &s_DebugMenuState.menuItem->subItems, parse_DebugMenuItem);
	debugmenu_AddLocalCommands(entActivePlayerPtr(), s_DebugMenuState.menuItem);
	menuItemInitOpenState(s_DebugMenuState.menuItem);
	FixParentPointer(s_DebugMenuState.menuItem, NULL);
	entDebugInitMenu();
}

AUTO_COMMAND;
void debugmenu_Close(int closingTime)
{
	s_DebugMenuState.closingTime = closingTime;
	s_DebugMenuState.lastTime = timeGetTime();
	gfxDebugDisableAccessLevelWarnings(false);
}

static void debugmenu_StateEnter(void)
{
	gGCLState.bLockPlayerAndCamera = true;
	ui_RemoveActiveFamilies(UI_FAMILY_GAME);
	globCmdParse("ShowGameUINoExtraKeyBinds 0");
}

static void debugmenu_StateLeave(void)
{
	gGCLState.bLockPlayerAndCamera = false;
	if (!emIsEditorActive())
		ui_AddActiveFamilies(UI_FAMILY_GAME);
	globCmdParse("ShowGameUINoExtraKeyBinds 1");
}

AUTO_RUN;
void debugmenu_AutoRegister(void)
{
	GSM_AddGlobalState(GCL_DEBUG_MENU);
	GSM_AddGlobalStateCallbacks(GCL_DEBUG_MENU, debugmenu_StateEnter, NULL, debugmenu_StateOncePerFrame, debugmenu_StateLeave);
}

AUTO_COMMAND ACMD_NAME("DebugMenu.Up") ACMD_CATEGORY(DebugMenu);
void DebugMenuMoveUp(int keyDown, int pageUp)
{
	if (pageUp)
		s_DebugMenuState.pageUpHeld = !!keyDown;
	else
		s_DebugMenuState.upHeld = !!keyDown;
	if (keyDown)
	{
		int sel;

		s_DebugMenuState.keyDelayMsecStart = timeGetTime();
		s_DebugMenuState.keyDelayMsecWait = 200;

		s_DebugMenuState.cursel -= pageUp ? 20 : 1;

		if(s_DebugMenuState.cursel < 0){
			s_DebugMenuState.cursel = 0;
		}

		sel = s_DebugMenuState.cursel;

		while(s_DebugMenuState.cursel > 0 && !getCurMenuItem(s_DebugMenuState.menuItem, &sel)){
			s_DebugMenuState.cursel--;
			sel = s_DebugMenuState.cursel;
		}

		s_DebugMenuState.mouseMustMove = 1;
	}
}

AUTO_COMMAND ACMD_NAME("DebugMenu.Down") ACMD_CATEGORY(DebugMenu);
void DebugMenuMoveDown(int keyDown, int pageDown)
{
	if (pageDown)
		s_DebugMenuState.pageDownHeld = !!keyDown;
	else
		s_DebugMenuState.downHeld = !!keyDown;
	if (keyDown)
	{
		int sel;

		s_DebugMenuState.cursel += pageDown ? 20 : 1;

		s_DebugMenuState.keyDelayMsecStart = timeGetTime();
		s_DebugMenuState.keyDelayMsecWait = 200;

		sel = s_DebugMenuState.cursel;

		while(s_DebugMenuState.cursel > 0 && !getCurMenuItem(s_DebugMenuState.menuItem, &sel)){
			s_DebugMenuState.cursel--;
			sel = s_DebugMenuState.cursel;
		}

		s_DebugMenuState.mouseMustMove = 1;
	}
}

AUTO_COMMAND ACMD_NAME("DebugMenu.Select") ACMD_CATEGORY(DebugMenu);
void DebugMenuSelect(int closeMenu, int fromMouse)
{
	int sel;
	DebugMenuItem* item;

	if (fromMouse)
	{
		int w, h;
		int mouse_x, mouse_y;
		int totalLines = getTotalOpenItemCount(s_DebugMenuState.menuItem);
		int mouseLine;
		int x_off = 0;

		gfxGetActiveDeviceSize(&w, &h);
		mousePos(&mouse_x, &mouse_y);

		if (mouse_y < 0 || mouse_y >= h || mouse_x < 0)
			return;

		mouseLine = s_DebugMenuState.firstLine + (mouse_y - 22) / 16;

		if(mouseLine < 0 || mouseLine >= totalLines){
			return;
		}else{
			s_DebugMenuState.cursel = mouseLine;
		}
	}

	sel = s_DebugMenuState.cursel;
	if ((item = getCurMenuItem(s_DebugMenuState.menuItem, &sel)))
	{
		if (eaSize(&item->subItems) || !item->commandString)
			menuItemSetOpen(item, !item->open);
		else
		{
			parseDebugCommands(item->commandString);
			if (closeMenu) {
				debugmenu_Close(1);
				gfxDebugDisableAccessLevelWarningsTemporarily(2.f);
			} else
				item->usedCount++;
		}
		item->flash = 1;
		item->flashStartTime = timeGetTime();
	}
}

AUTO_COMMAND ACMD_NAME("DebugMenu.Expand") ACMD_CATEGORY(DebugMenu);
void DebugMenuExpand(void)
{
	int sel = s_DebugMenuState.cursel;
	DebugMenuItem* item = getCurMenuItem(s_DebugMenuState.menuItem, &sel);
	if (item && eaSize(&item->subItems))
	{
		if(item->open)
			s_DebugMenuState.cursel++;
		else
		{
			menuItemSetOpen(item, 1);
			item->flash = 1;
			item->flashStartTime = timeGetTime();
		}
		s_DebugMenuState.mouseMustMove = 1;
	}
}

AUTO_COMMAND ACMD_NAME("DebugMenu.Collapse") ACMD_CATEGORY(DebugMenu);
void DebugMenuCollapse(void)
{
	int sel = s_DebugMenuState.cursel;
	DebugMenuItem* item = getCurMenuItem(s_DebugMenuState.menuItem, &sel);
	if (item)
	{
		if (item->open)
		{
			menuItemSetOpen(item, 0);
			item->flash = 1;
			item->flashStartTime = timeGetTime();
		}
		else if (item->parentItem)
		{
			int total = 0;
			int i, n = eaSize(&item->parentItem->subItems);
			for (i = 0; i < n; i++)
			{
				DebugMenuItem* subItem = item->parentItem->subItems[i];
				if (subItem == item)
				{
					s_DebugMenuState.cursel -= total + 1;
					break;
				}
				else
				{
					total += getTotalOpenItemCount(subItem);
				}
			}
		}

		s_DebugMenuState.mouseMustMove = 1;
	}
}

AUTO_COMMAND ACMD_NAME("DebugMenu.CloseParent") ACMD_CATEGORY(DebugMenu);
void DebugMenuCloseParent(void)
{
	int sel = s_DebugMenuState.cursel;
	int mouse_x, mouse_y;
	DebugMenuItem* item = getCurMenuItem(s_DebugMenuState.menuItem, &sel);

	mousePos(&mouse_x, &mouse_y);

	if(mouse_x >= 0 && mouse_x < DEBUGMENU_WIDTH)
	{
		if(item && (item->open || item->parentItem)){
			if(!item->open){
				sel = 1;

				while(1){
					DebugMenuItem* findItem;
					int index = sel;

					findItem = getCurMenuItem(item->parentItem, &index);

					if(findItem == item){
						break;
					}

					sel++;
				}

				s_DebugMenuState.cursel -= sel;

				item = item->parentItem;
			}

			menuItemSetOpen(item, 0);
			item->flash = 1;
			item->flashStartTime = timeGetTime();

			s_DebugMenuState.mouseMustMove = 1;
			s_DebugMenuState.moveMouseToSel = s_DebugMenuState.cursel;
		}
	}
}

AUTO_COMMAND ACMD_NAME("DebugMenu.Exit") ACMD_CATEGORY(DebugMenu);
void DebugMenuExit(void)
{
	debugmenu_Close(150);	
}

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0);
void SetMotd(ACMD_SENTENCE pText)
{
	strcpy_trunc(s_DebugMenuState.motdText, pText);
	s_DebugMenuState.motdVisible = (pText[0] ? true : false);
}

#include "AutoGen/gclEntDebugMenu_c_ast.c"