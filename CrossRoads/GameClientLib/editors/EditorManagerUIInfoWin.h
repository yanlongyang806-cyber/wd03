#pragma once
GCC_SYSTEM
#ifndef __EDITORMANAGERUIINFOWIN_H__
#define __EDITORMANAGERUIINFOWIN_H__

#ifndef NO_EDITORS

typedef struct EMEditor EMEditor;

#endif
AUTO_STRUCT;
typedef struct EMInfoWinText
{
	char *text;
	U32 rgba;
} EMInfoWinText;

typedef void (*EMInfoWinTextFunc)(const char *indexed_name, EMInfoWinText ***text_lines);

AUTO_STRUCT;
typedef struct EMInfoWinEntry
{
	char *indexed_name;
	char *display_name;
	EMInfoWinTextFunc text_func;	NO_AST
} EMInfoWinEntry;

AUTO_STRUCT;
typedef struct EMInfoWin
{
	char **entry_indexes;
} EMInfoWin;
#ifndef NO_EDITORS

/********************
* MANAGEMENT
********************/
SA_RET_OP_VALID EMInfoWinEntry *emInfoWinEntryGet(SA_PARAM_NN_VALID EMEditor *editor, SA_PARAM_NN_STR const char *indexed_name);

/********************
* MAIN
********************/
void emInfoWinDraw(void);

/********************
* EXTERNS
********************/
extern ParseTable parse_EMInfoWin[];
#define TYPE_parse_EMInfoWin EMInfoWin
extern ParseTable parse_EMInfoWinEntry[];
#define TYPE_parse_EMInfoWinEntry EMInfoWinEntry
extern ParseTable parse_EMInfoWinText[];
#define TYPE_parse_EMInfoWinText EMInfoWinText

#endif // NO_EDITORS

#endif // __EDITORMANAGERUIINFOWIN_H__