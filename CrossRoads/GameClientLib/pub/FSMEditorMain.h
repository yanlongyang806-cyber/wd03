/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#pragma once
GCC_SYSTEM

typedef struct FSMGroup FSMGroup;
typedef struct ExprContext ExprContext;
typedef struct FSMPickerFolder FSMPickerFolder;
typedef struct FSM FSM;

AUTO_STRUCT;
typedef struct FSMGroup
{
	char *name;						AST(NAME("Name","Name:"))
	char *dir;						AST(NAME("Dir","Dir:"))
}FSMGroup;

AUTO_STRUCT;
typedef struct FSMPickerFolder
{
	char *folderName;
	char *folderPath;
	FSMPickerFolder **fsmFolders;
	FSMGroup *rootGroup;
	FSM **fsms;
};

AUTO_STRUCT;
typedef struct FSMEditorGroupRoot
{
	FSMGroup **fsmGroups;
} FSMEditorGroupRoot;

#ifndef NO_EDITORS
 
typedef void (*FSMEditorGroupLoadCallback)(const char *path, const char *uniqueName, ExprContext *context);

#define fsmEditorRegisterGroup(path, uniqueName) fsmEditorRegisterGroupEx(path, uniqueName, NULL, NULL)
void fsmEditorRegisterGroupEx(SA_PARAM_NN_STR const char *path, SA_PARAM_NN_STR const char *uniqueName, SA_PARAM_OP_VALID ExprContext *context, SA_PARAM_OP_VALID FSMEditorGroupLoadCallback onLoadF);
ExprContext* fsmEditorFindContextByName(SA_PARAM_NN_STR const char *name);

#endif