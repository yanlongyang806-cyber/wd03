/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

typedef struct Entity Entity;
typedef struct DebugMenuItem DebugMenuItem;

AUTO_STRUCT;
typedef struct DebugMenuItem
{
	char* displayText;
	char* commandString;
	char* rolloverText;

	DebugMenuItem* parentItem; NO_AST
	DebugMenuItem** subItems;
	U32	open : 1;

	// Client-side state variables
	U32	usedCount; NO_AST
	U32 flash : 1; NO_AST
	int	flashStartTime; NO_AST
} DebugMenuItem;

extern ParseTable parse_DebugMenuItem[];
#define TYPE_parse_DebugMenuItem DebugMenuItem

DebugMenuItem* debugmenu_AddNewCommand(SA_PARAM_NN_VALID DebugMenuItem* parent, SA_PARAM_NN_STR const char* displayText, SA_PARAM_NN_STR const char* commandString);
DebugMenuItem* debugmenu_AddNewCommandGroup(SA_PARAM_NN_VALID DebugMenuItem* parent, SA_PARAM_NN_STR const char* displayText, SA_PARAM_OP_STR const char* rolloverText, bool startOpen);

typedef void (*DebugMenuGroupFillCB)(Entity* playerEnt, DebugMenuItem* groupRoot);

void debugmenu_RegisterNewGroup(SA_PARAM_NN_STR const char* groupName, DebugMenuGroupFillCB groupFillCB);

void debugmenu_AddLocalCommands(SA_PARAM_OP_VALID Entity* playerEnt, SA_PARAM_NN_VALID DebugMenuItem* rootItem);
