/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "entdebugmenu.h"
#include "entdebugmenu_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct DebugMenuGroup
{
	char* groupName;
	DebugMenuGroupFillCB fillFunc;
} DebugMenuGroup;

static DebugMenuGroup** s_DebugMenuGroupList = NULL;

void debugmenu_RegisterNewGroup(const char* groupName, DebugMenuGroupFillCB groupFillCB)
{
	DebugMenuGroup* newGroup = calloc(1, sizeof(DebugMenuGroup));
	newGroup->fillFunc = groupFillCB;
	newGroup->groupName = strdup(groupName);
	eaPush(&s_DebugMenuGroupList, newGroup);
}

DebugMenuItem* debugmenu_AddNewCommand(DebugMenuItem* parent, const char* displayText, const char* commandString)
{
	DebugMenuItem* newItem = StructCreate(parse_DebugMenuItem);
	newItem->displayText = StructAllocString(displayText);
	newItem->commandString = StructAllocString(commandString);
	eaPush(&parent->subItems, newItem);
	newItem->parentItem = parent;
	return newItem;
}

DebugMenuItem* debugmenu_AddNewCommandGroup(DebugMenuItem* parent, const char* displayText, const char* rolloverText, bool startOpen)
{
	DebugMenuItem* newItem = StructCreate(parse_DebugMenuItem);
	newItem->displayText = StructAllocString(displayText);
	newItem->open = !!startOpen;
	if (rolloverText)
		newItem->rolloverText = StructAllocString(rolloverText);
	eaPush(&parent->subItems, newItem);
	newItem->parentItem = parent;
	return newItem;
}

void debugmenu_AddLocalCommands(Entity* playerEnt, DebugMenuItem* rootItem)
{
	int i, n = eaSize(&s_DebugMenuGroupList);
	for (i = 0; i < n; i++)
	{
		DebugMenuGroup* currGroup = s_DebugMenuGroupList[i];
		DebugMenuItem* childItem = debugmenu_AddNewCommandGroup(rootItem, currGroup->groupName, "", false);
		currGroup->fillFunc(playerEnt, childItem);
	}
}

#include "entdebugmenu_h_ast.c"
