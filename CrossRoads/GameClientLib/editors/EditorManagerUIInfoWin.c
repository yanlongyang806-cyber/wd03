#include "EditorManagerUIInfoWin.h"

#ifndef NO_EDITORS
#include "EditorManagerPrivate.h"
#include "EditorPrefs.h"
#include "GfxPrimitive.h"
#include "GfxSpriteText.h"
#include "GfxClipper.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

/********************
* MANAGEMENT
********************/
/******
* This function creates a line of text for display in the info window.
* PARAMS:
*   text - string text to display
*   rgba - U32 color for the line of text; if 0, the text will default to white
******/
EMInfoWinText *emInfoWinCreateTextLineWithColor(const char *text, U32 rgba)
{
	EMInfoWinText *line = StructCreate(parse_EMInfoWinText);
	line->text = StructAllocString(text);
	if (rgba)
		line->rgba = rgba;
	else
		line->rgba = 0xFFFFFFFF;
	return line;
}

/******
* This function registers an info window entry for the specified editor.
* PARAMS:
*   editor - EMEditor to which the entry will be associated
*   indexed_name - string name reference for the entry
*   display_name - string displayed to the user
*   text_func - EMInfoWinTextFunc callback invoked to get the estring(s) to print every frame in the info
*               window
******/
void emInfoWinEntryRegister(EMEditor *editor, const char *indexed_name, const char *display_name, EMInfoWinTextFunc text_func)
{
	if (editor)
	{
		EMInfoWinEntry *entry = StructCreate(parse_EMInfoWinEntry);

		entry->indexed_name = StructAllocString(indexed_name);
		entry->display_name = StructAllocString(display_name);
		entry->text_func = text_func;

		if (!editor->avail_entries)
			editor->avail_entries = stashTableCreateWithStringKeys(16, StashDefault);
		stashAddPointer(editor->avail_entries, entry->indexed_name, entry, false);
	}
}

/******
* This function retrieves the registered info window entry for an editor.
* PARAMS:
*   editor - EMEditor to search for the entry
*   indexed_name - string index to the entry
* RETURNS:
*   EMInfoWinEntry associated with the specified index
******/
EMInfoWinEntry *emInfoWinEntryGet(EMEditor *editor, const char *indexed_name)
{
	EMInfoWinEntry *entry = NULL;

	if (!editor)
		return NULL;

	stashFindPointer(editor->avail_entries, indexed_name, &entry);
	return entry;
}


/********************
* MAIN
********************/
/******
* This function renders the info window at the specified location with the specified entries.
* PARAMS:
*   x - int screen x coordinate of top left corner
*   y - int screen y coordinate of top left corner
*   width - int screen width of window
*   height - int screen height of window
*   z - int screen depth of window
*   c - U32 rgba color used to draw; if 0, defaults to white
*   entries - EArray of EMInfoWinEntries that are to be rendered
******/
static void emInfoWinDrawEx(int x, int y, int width, int height, int z, U32 c, EMInfoWinEntry **entries)
{
	int i;
	CBox box;

	gfxDrawLine(x, y, z, x + width, y, c ? colorFromRGBA(c) : ColorWhite);
	gfxDrawLine(x + width, y, z, x + width, y + height, c ? colorFromRGBA(c) : ColorWhite);
	gfxDrawLine(x + width, y + height, z, x, y + height, c ? colorFromRGBA(c) : ColorWhite);
	gfxDrawLine(x, y + height, z, x, y, c ? colorFromRGBA(c) : ColorWhite);
	x += 5 * g_ui_State.scale;
	y += 5 * g_ui_State.scale;
	width -= 10 * g_ui_State.scale;
	height -= 10 * g_ui_State.scale;

	// set up a clipping box
	box.left = x;
	box.top = y;
	box.right = x + width;
	box.bottom = y + height;
	clipperPushRestrict(&box);

	y += 15 * g_ui_State.scale;
	for (i = 0; i < eaSize(&entries); i++)
	{
		EMInfoWinText **lines = NULL;
		EMInfoWinEntry *entry = entries[i];
		int temp_x = x;

		gfxfont_SetFontEx(&g_font_Sans, 0, 0, 0, 0, c ? c : 0xFFFFFFFF, c ? c : 0xFFFFFFFF);
		gfxfont_Printf(temp_x, y, z, g_ui_State.scale, g_ui_State.scale, 0, "%s: ", entry->display_name);
		temp_x += (5 * g_ui_State.scale + gfxfont_StringWidthf(&g_font_Sans, g_ui_State.scale, g_ui_State.scale, "%s: ", entry->display_name));
		entry->text_func(entry->indexed_name, &lines);
		if (eaSize(&lines) == 0)
			y += (15 * g_ui_State.scale);
		while (eaSize(&lines) > 0)
		{
			EMInfoWinText *line = lines[0];

			gfxfont_SetFontEx(&g_font_Sans, 0, 0, 0, 0, c ? c : line->rgba, c ? c : line->rgba);
			gfxfont_Printf(temp_x, y, z, g_ui_State.scale, g_ui_State.scale, 0, "%s", line->text);
			StructDestroy(parse_EMInfoWinText, line);
			eaRemove(&lines, 0);
			y += (15 * g_ui_State.scale);
		}
		eaDestroy(&lines);
	}

	clipperPop();
}

/******
* This function draws the info window according to the user's preferences.
******/
void emInfoWinDraw(void)
{
	if (em_data.current_editor && EditorPrefGetInt("Editor Manager", "Info Win", "Enabled", 0) && !em_data.current_editor->hide_info_window)
	{
		EMEditor *editor = em_data.current_editor;
		EMInfoWin *info_win;
		EMInfoWinEntry **entries = NULL;
		int x, y, width, height;
		int i;
		int z = -10000;

		// get preferences
		info_win = StructCreate(parse_EMInfoWin);
		assert(info_win);
		EditorPrefGetStruct(editor->editor_name, "Info Win", "Contents", parse_EMInfoWin, info_win);

		x = EditorPrefGetInt("Editor Manager", "Info Win", "X", 100) * g_ui_State.scale;
		y = EditorPrefGetInt("Editor Manager", "Info Win", "Y", 100) * g_ui_State.scale;
		width = EditorPrefGetInt("Editor Manager", "Info Win", "Width", 200) * g_ui_State.scale;
		height = EditorPrefGetInt("Editor Manager", "Info Win", "Height", 100) * g_ui_State.scale;

		for (i = 0; i < eaSize(&info_win->entry_indexes); i++)
		{
			EMInfoWinEntry *entry;
			if (stashFindPointer(editor->avail_entries, info_win->entry_indexes[i], &entry))
				eaPush(&entries, entry);
		}

		if (EditorPrefGetInt("Editor Manager", "Info Win", "In Front", 1))
		{
			emInfoWinDrawEx(x + 2, y + 2, width, height, 9999, 0x00000055, entries);
			z = 10000;
		}

		emInfoWinDrawEx(x, y, width, height, z, 0, entries);

		// clean up
		StructDestroy(parse_EMInfoWin, info_win);
		eaDestroy(&entries);
	}
}

#endif

#include "EditorManagerUIInfoWin_h_ast.c"