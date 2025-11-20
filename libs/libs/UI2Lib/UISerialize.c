/***************************************************************************



***************************************************************************/

#include "fileutil.h"
#include "FolderCache.h"
#include "EString.h"
#include "Error.h"

#include "UISerialize.h"

#include "UISerialize_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// FIXME: Once there are a lot of layouts it might be necessary to use
// a hash table lookup instead of a linear search on names.
static UILayouts s_Layouts;

static void ReloadLayoutsInGroup(UIWidgetGroup *group)
{
	S32 i;
	for (i = 0; i < eaSize(group); i++)
	{
		UIWidget *widget = (*group)[i];
		if (widget && widget->name)
			ui_LoadLayout(widget);
		if (widget && widget->children)
			ReloadLayoutsInGroup(&widget->children);
	}
}

static void ReloadLayouts(void)
{
	StashTableIterator i;
	StashElement e;

	stashGetIterator(g_ui_State.states, &i);
	while (stashGetNextElement(&i, &e))
	{
		UIDeviceState *state = stashElementGetPointer(e);
		ReloadLayoutsInGroup(&state->topgroup);
		ReloadLayoutsInGroup(&state->panegroup);
		ReloadLayoutsInGroup(&state->maingroup);
	}
}

static void UILayoutReloadCallback(const char *path, S32 when)
{
	S32 i;
	fileWaitForExclusiveAccess(path);
	errorLogFileIsBeingReloaded(path);

	for (i = eaSize(&s_Layouts.layouts) - 1; i >= 0; i--)
	{
		if (s_Layouts.layouts[i]->filename && !stricmp(s_Layouts.layouts[i]->filename, path))
		{
			StructDestroy(parse_UIWidget, s_Layouts.layouts[i]);
			eaRemove(&s_Layouts.layouts, i);
		}
	}

	if (ParserReloadFile(path, parse_UILayouts, &s_Layouts, NULL, 0))
		ReloadLayouts();
	else
		Errorf("Error reloading UI layout: %s", path);
}

void ui_LoadLayout(UIWidget *widget)
{
	S32 i;
	for (i = 0; i < eaSize(&s_Layouts.layouts); i++)
	{
		if (widget->name && !stricmp(widget->name, s_Layouts.layouts[i]->name))
			ui_ApplyLayout(s_Layouts.layouts[i], widget);
	}
#if _XBOX
	if (widget && widget->name)
	{
		char *xboxname = estrStackCreateFromStr(widget->name);
		estrAppend2(&xboxname, "#Xbox");
		for (i = 0; i < eaSize(&s_Layouts.layouts); i++)
		{
			if (!stricmp(xboxname, s_Layouts.layouts[i]->name))
				ui_ApplyLayout(s_Layouts.layouts[i], widget);
		}
		if (g_ui_State.mode == UISD)
			estrAppend2(&xboxname, "SD");
		else
			estrAppend2(&xboxname, "HD");
		for (i = 0; i < eaSize(&s_Layouts.layouts); i++)
		{
			if (!stricmp(xboxname, s_Layouts.layouts[i]->name))
				ui_ApplyLayout(s_Layouts.layouts[i], widget);
		}
		estrDestroy(&xboxname);
	}
#endif
	for (i = 0; i < eaSize(&widget->children); i++)
		ui_LoadLayout(widget->children[i]);
}

void ui_ApplyLayout(UIWidget *layout, UIWidget *widget)
{
	StructOverride(parse_UIWidget, widget, layout, 0, true, true);
}

void ui_AutoLoadLayouts(void)
{
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/layouts/*.layout", UILayoutReloadCallback);
	loadstart_printf("Loading UI layouts... ");
	ParserLoadFiles("ui/layouts", ".layout", "Layouts.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, parse_UILayouts, &s_Layouts);
	loadend_printf("done (%d layouts).", eaSize(&s_Layouts.layouts));
}
