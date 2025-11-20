#pragma once
GCC_SYSTEM
#ifndef __EDITORMANAGERUIMOTD_H__
#define __EDITORMANAGERUIMOTD_H__

#ifndef NO_EDITORS

#include "earray.h"
#include "estring.h"

typedef struct EMMessageOfTheDay
{
	// registration data
	const char *keyname;
	const char *text;
	__time32_t registration_timestamp; // set this so the Editor users can sort by it and see how old a Message is
	const char *const *relevant_editors;
	const char *const *relevant_groups;

	// locally saved data
	__time32_t last_shown_timestamp;
	bool never_show_again;

	// internal data
	bool rendered_this_time;
} EMMessageOfTheDay;

/********************
* REGISTRATION
********************/

void tokenizeStr(char ***dest, const char *tokens, const char *src);
void emRegisterMotDEx(int year, int month, int day, const char *keyname, const char *const *relevant_editors, const char *const *relevant_groups, const char *text);
void emRegisterMotDForEditorAndGroup(int year, int month, int day, const char *keyname, const char *editor, const char *group, const char *text);
#define emRegisterMotD(year, month, day, keyname, text) emRegisterMotDEx(year, month, day, keyname, NULL, NULL, text)
#define emRegisterMotDForEditor(year, month, day, keyname, editor, text)  emRegisterMotDForEditorAndGroup(year, month, day, keyname, editor, NULL, text);
#define emRegisterMotDForGroup(year, month, day, keyname, group, text) emRegisterMotDForEditorAndGroup(year, month, day, keyname, NULL, group, text);

/********************
* OPTIONS
********************/
bool emMotDGetShowInViewport(void);
void emMotDSetShowInViewport(bool show_in_viewport);

/********************
* REFRESH/DRAW
********************/
void emMotDCycle(void);
void emMotDRefresh(void);

#endif // NO_EDITORS

#endif // __EDITORMANAGERUIMOTD_H__
