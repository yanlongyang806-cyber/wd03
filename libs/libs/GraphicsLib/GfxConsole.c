#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include "GfxConsole.h"
#include "MRUList.h"
#include "Prefs.h"
#include "inputText.h"
#include "inputLib.h"
#include "GfxTexAtlas.h"
#include "Clipper.h" //include this before GfxSprite.h when inside GraphicsLib to get the fully inlined clipper functions
#include "GfxSprite.h"
#include "GfxDebug.h"
#include "GfxSpriteText.h"
#include "GraphicsLibPrivate.h"
#include "inputKeyBind.h"
#include "inputMouse.h"
#include "strings_opt.h"
#include "ScratchStack.h"
#include "sysutil.h"
#include "earray.h"
#include "wininclude.h"
#include "qsortG.h"

#define CONSOLE_Z (GRAPHICSLIB_Z+20)

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define MAX_LINES 400
#define MAX_HIST_LINES 32
#define LINE_SIZE 1024

typedef struct
{
	char	*estr_lines[MAX_LINES];
	int		oldest_line,newest_line;
	int		hist_line;
	char	last_input[LINE_SIZE];
	char	input_buf[LINE_SIZE];
	char	tab_comp_buf[LINE_SIZE];
	char	raw_tab_comp_buf[LINE_SIZE];
	int		input_pos;
	int		v_chars;
	int		scroll_up;
	int		cursor_anim;
	bool	show_console;
	bool	console_visible_this_frame; // one-frame delay for gfxConsoleVisible to make sure input isn't leaked
	bool	resize_dragging;
	F32		console_scale;
	MRUList *mruHist;
} Console;

Console	*curr_con;
static ConPrintCallback s_conPrintCallback = NULL;

static bool enableDebugConsole;
// Enables the ~ console
AUTO_CMD_INT(enableDebugConsole,enableDebugConsole) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CMDLINEORPUBLIC;
static U8 consoleAlpha;
// Sets the alpha of the ~ console
AUTO_CMD_INT(consoleAlpha,consoleAlpha) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(consoleAlphaSet);
void consoleAlphaSet(void)
{
	GamePrefStoreInt("ConsoleAlpha", consoleAlpha);
}

static bool consoleShowOptions=false;
// Shows command and parameters options as you type in the console
AUTO_CMD_INT(consoleShowOptions,consoleShowOptions) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CMDLINEORPUBLIC;


MRUList *conGetMRUList(void)
{
	return curr_con->mruHist;
}


void conCreate(void)
{
	curr_con = calloc(1,sizeof(*curr_con));
	curr_con->mruHist = createMRUList("Console", MAX_HIST_LINES, LINE_SIZE);
	curr_con->v_chars = 30;
	curr_con->hist_line = curr_con->mruHist->count;
	curr_con->console_scale = GamePrefGetFloat("ConsoleScale", 0.5f);
	consoleAlpha = GamePrefGetInt("ConsoleAlpha", 0x70);
}

static int lineDist(int old,int curr)
{
int		dist;

	dist = curr - old;
	if (dist < 0)
		dist += MAX_LINES;

	return dist;
}

static int addLine(int line,int amt)
{
	line += amt;
	if (line >= MAX_LINES)
		line -= MAX_LINES;
	return line;
}

static int subLine(int line,int amt)
{
	line -= amt;
	if (line < 0)
		line += MAX_LINES;
	return line;
}

void conSetPrintCallback(ConPrintCallback cb)
{
	s_conPrintCallback = cb;
}

ConPrintCallback conGetPrintCallback(void)
{
	return s_conPrintCallback;
}

void conPrint(char *s)
{
	if (!curr_con)
		return;
	if (lineDist(curr_con->oldest_line,curr_con->newest_line) >= MAX_LINES -1)
		curr_con->oldest_line = addLine(curr_con->oldest_line,1);
	estrCopy2(&(curr_con->estr_lines[curr_con->newest_line]),s);
	curr_con->newest_line = addLine(curr_con->newest_line,1);

	// make sure the string is more than just a 1 or 0 (from cmdparse return values)
	if (s_conPrintCallback && s && *s && !((s[0] == '1' || s[0] == '0') && s[1] == 0))
		s_conPrintCallback(s);
}

#undef conPrintf
void conPrintf(char const *fmt, ...)
{
	char *str = ScratchAlloc(20000),*s,*s2;
	va_list ap;

	va_start(ap, fmt);
	vsprintf_s(str, ScratchSize(str), fmt, ap);
	va_end(ap);
	str[ScratchSize(str)-1]='\0';

	for(s2=str;;)
	{
		s = strchr(s2,'\n');
		if (s)
		{
			*s = 0;
			if (s[1] == 0)
				s = 0;
		}
		if (gfxConsoleVisible())
			conPrint(s2);
		else
		{
			// make sure the string is more than just a 1 or 0 (from cmdparse return values).
			// Output starting with "Cmd " is probably from a command that returns numerics, so
			// check for that as well.  (See CmdPretyPrintResult() for "Cmd " string).
			if (s_conPrintCallback && s2 && *s2 && !strStartsWith(s2, "Cmd ")) 
			{
				s_conPrintCallback(s2);
			}
			
			//ABW commenting this out entirely. For commands called from the dd console, the printf
			//how happens instead of conPrintf entirely in GameClientParsePublic
			//printf( "%s\n", s2 );
		}
		if (!s)
			break;
		s2 = s + 1;
	}
	ScratchFree(str);
}

#undef conPrintfUpdate
void conPrintfUpdate(char const *fmt, ...)
{
char str[MAX_PATH];
va_list ap;

	va_start(ap, fmt);
	vsprintf(str, fmt, ap);
	str[ARRAY_SIZE(str)-1]='\0';
	va_end(ap);
	conPrint(str);
}

static void conResizeInputCheck(void)
{
	int x, y;
	int w, h, consoleh;
	if (!curr_con->show_console) {
		curr_con->resize_dragging=false;
		return;
	}
	gfxGetActiveSurfaceSize(&w,&h);
	consoleh = h;
	if (curr_con->console_scale <= 0 || curr_con->console_scale > 1)
		curr_con->console_scale = 0.5f;
	consoleh = (int)(consoleh * curr_con->console_scale) + gfxXYgetY(0);
	mousePos(&x, &y);
	if (!curr_con->resize_dragging && x >= w - 16 && x <= w && y<consoleh && y>=consoleh-16)
	{
		if (inpLevel(INP_LBUTTON))
			gfxConsoleEnable(0);
	}
	else 
	{
		if (ABS(y-consoleh) < 6 && x >= 0 && x <= w)
		{
			if (inpLevel(INP_LBUTTON))
				curr_con->resize_dragging=true;
		}
	}
	if (!inpLevelPeek(INP_LBUTTON))
		curr_con->resize_dragging = false;
	if (curr_con->resize_dragging) {
		curr_con->console_scale = (y - gfxXYgetY(0)) / (F32)h;
		GamePrefStoreFloat("ConsoleScale", curr_con->console_scale);
	}
}

static void conResizeCheck(void)
{
	int x, y;
	int w, h, consoleh;
	if (!curr_con->show_console) {
		curr_con->resize_dragging=false;
		return;
	}
	gfxGetActiveSurfaceSize(&w,&h);
	consoleh = h;
	if (curr_con->console_scale <= 0 || curr_con->console_scale > 1)
		curr_con->console_scale = 0.5f;
	consoleh = (int)(consoleh * curr_con->console_scale) + gfxXYgetY(0);
	mousePos(&x, &y);
	if (!curr_con->resize_dragging && x >= w - 16 && x <= w && y<consoleh && y>=consoleh-16)
	{
		int color[4] = {0xff0000ff, 0xff0000ff, 0xff0000ff, 0xff0000ff};
		gfxfont_PrintEx(&g_font_Mono, w - 16, consoleh, CONSOLE_Z+4, 2, 1.8, 0, "X", (int)strlen("X"), color, NULL);
	}
	else 
	{
		int color_nonrollover[4] = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
		gfxfont_PrintEx(&g_font_Mono, w - 16, consoleh, CONSOLE_Z+4, 2, 1.8, 0, "X", (int)strlen("X"), color_nonrollover, NULL);

		if (ABS(y-consoleh) < 6 && x >= 0 && x <= w)
		{
			int color_dragging[4] = {0xffffff1f, 0xffffff1f, 0xffffff1f, 0xffffff1f};
			if (!curr_con->resize_dragging)
				gfxfont_PrintEx(&g_font_Mono, w/2 - 10*8, consoleh + 8, CONSOLE_Z+4, 1, 1, 0, "Click and drag to resize console", (int)strlen("Click and drag to resize console"), color_nonrollover, NULL);
			else
				gfxfont_PrintEx(&g_font_Mono, w/2 - 10*8, consoleh + 8, CONSOLE_Z+4, 1, 1, 0, "Click and drag to resize console", (int)strlen("Click and drag to resize console"), color_dragging, NULL);
		}
	}
}


static int conGetCurCommandStart()
{
#define CHECK_FOR_TWO_CHARACTERS(c) (curr_con->input_buf[cmd_start_pos-1] == (c) && cmd_start_pos >= 1 && curr_con->input_buf[cmd_start_pos-2] == (c))
#define THERE_WERE_TWO_CHARACTERS(c) (curr_con->input_buf[cmd_start_pos] == (c) && curr_con->input_buf[cmd_start_pos-1] == (c))

	int cmd_start_pos = (int)strlen(curr_con->input_buf);
	assert(cmd_start_pos < ARRAY_SIZE(curr_con->input_buf));
	while ( cmd_start_pos && curr_con->input_buf[cmd_start_pos-1] != ' ' &&
		curr_con->input_buf[cmd_start_pos-1] != '\t' &&
		curr_con->input_buf[cmd_start_pos-1] != '+' &&
		!CHECK_FOR_TWO_CHARACTERS('$') &&
		!CHECK_FOR_TWO_CHARACTERS('-') && 
		!CHECK_FOR_TWO_CHARACTERS('+') )
	{
		--cmd_start_pos;
	}

	if (cmd_start_pos && (THERE_WERE_TWO_CHARACTERS('$') || THERE_WERE_TWO_CHARACTERS('-') || THERE_WERE_TWO_CHARACTERS('+')) )
		++cmd_start_pos;

	return cmd_start_pos;
}


void gfxConsoleRender(void)
{
	int		i,line,vis_lines,show_cursor,drawto_line,max_scroll,w,consoleh,screenw;
	char	buf[LINE_SIZE+20];
	AtlasTex *white = atlasLoadTexture("white.tga");

	if (!gfxConsoleVisible())
		return;

	curr_con->console_visible_this_frame = curr_con->show_console;

	PERFINFO_AUTO_START_FUNC_PIX();

	conResizeCheck();

	gfxGetActiveSurfaceSize(&screenw,&consoleh);
	w = gfxXYgetX(TEXT_JUSTIFY+80);

	if (curr_con->console_scale <= 0 || curr_con->console_scale > 1)
		curr_con->console_scale = 0.5f;
	consoleh = (int)(curr_con->console_scale * consoleh);

	curr_con->v_chars = consoleh / 12;

	if (inpLevel(INP_PRIOR)
		||
		inpLevel(INP_UP) &&
		inpLevel(INP_CONTROL))
	{
		curr_con->scroll_up++;
	}
	if (inpLevel(INP_NEXT)
		||
		inpLevel(INP_DOWN) &&
		inpLevel(INP_CONTROL))
	{
		curr_con->scroll_up--;
	}
	if (inpLevel(INP_HOME))
		curr_con->scroll_up = MAX_LINES;
	if (inpLevel(INP_END))
		curr_con->scroll_up = 0;

#if !PLATFORM_CONSOLE
	display_sprite(white, 0, gfxXYgetY(0), CONSOLE_Z, w / (F32)white->width, consoleh / (F32)white->height, 0x3f3f3f00|consoleAlpha);
#else
	// Reverse hack for moving text into visible area, but keep the console background large!
	display_sprite(white, 0, 0, CONSOLE_Z, screenw / (F32)white->width, (consoleh + gfxXYgetY(0)) / (F32)white->height, 0x3f3f3f00|consoleAlpha);
#endif

	vis_lines = curr_con->v_chars - 1;
	max_scroll = subLine(curr_con->newest_line,curr_con->oldest_line) - vis_lines;
	if (curr_con->scroll_up > max_scroll)
		curr_con->scroll_up = max_scroll;
 	if (curr_con->scroll_up < 0)
		curr_con->scroll_up = 0;
	drawto_line = subLine(curr_con->newest_line,curr_con->scroll_up);
	if (lineDist(curr_con->oldest_line,drawto_line) < vis_lines)
		line = 0;
	else
		line = subLine(drawto_line,vis_lines);
	for(i=0;line != drawto_line;line = addLine(line,1),i++)
	{
		if (line < ARRAY_SIZE(curr_con->estr_lines) && curr_con->estr_lines[line]) {
			gfxXYZprintfColor(1, 1 + i, CONSOLE_Z+1, 0xff, 0xff, 0x80, 0xff, "%s", curr_con->estr_lines[line]);
		}
	}

	// tab complete buffer
	{
		int cmd_start = conGetCurCommandStart();
		int x;
		U8 r, g, b;
		char temp = curr_con->input_buf[cmd_start], 
			*cmpInp =&curr_con->input_buf[cmd_start],
			*cmpTab =curr_con->tab_comp_buf;

		curr_con->input_buf[cmd_start] = 0;
		x = (int)gfxfont_StringWidthf(&g_font_Mono, 1, 1, "%s", curr_con->input_buf);
		curr_con->input_buf[cmd_start] = temp;

		/*
		if ( gclIsServerCommand(curr_con->tab_comp_buf) )
			r = 0xff, g = 0xff, b = 0x00;
		else
		*/
			r = 0xff, g = 0xff, b = 0xff;
		// correct the capitalization on the tab complete buffer
		while ( *cmpInp && *cmpTab )
		{
			if ( *cmpInp == '_' && *cmpTab != '_' )
			{
				memmove( cmpTab + 1, cmpTab, (int)strlen(cmpTab) + 1 );
				*cmpTab = '_';
			}
			else if ( *cmpTab != *cmpInp )
				*cmpTab = *cmpInp;
			++cmpTab;
			++cmpInp;
		}
		gfxXYZprintfColor(x/8+2, curr_con->v_chars, CONSOLE_Z+1, r, g, b, 0xff, "%s", curr_con->tab_comp_buf);
	}

	// input buffer
	show_cursor = ((++curr_con->cursor_anim) >> 3) & 1;
	sprintf(buf,">%s",curr_con->input_buf);
	gfxXYZprintfColor(1, curr_con->v_chars, CONSOLE_Z+1, 0x00, 0xff, 0x00, 0xff, "%s", buf);

	if(show_cursor){
		char temp = curr_con->input_buf[curr_con->input_pos];
		int x;

		curr_con->input_buf[curr_con->input_pos] = '\0';
	
		x = (int)gfxfont_StringWidthf(&g_font_Mono, 1, 1, "%s", curr_con->input_buf);

		curr_con->input_buf[curr_con->input_pos] = temp;
		
		gfxXYZprintfColor(2 + x/8, curr_con->v_chars, CONSOLE_Z+2, 0x00, 0xff, 0x00, 0xff, "_");
	}

	// tab-complete options
	{
		if (consoleShowOptions && curr_con->input_pos > 1)
		{
			int cmd_start = conGetCurCommandStart();
			NameList *pListToUse = NULL;

			PERFINFO_AUTO_START("Complete drop-down", 1);

			PERFINFO_AUTO_START("top", 1);
			if (cmd_start == 0)
			{
				pListToUse = pAllCmdNamesForAutoComplete;
			}
			else if (curr_con->input_pos >= cmd_start)
			{
				pListToUse = cmdGetNameListFromPartialCommandString(curr_con->input_buf);
			}

			if (pListToUse)
			{
				static char **ppStrings = NULL;
				static NameList *cachedNameList=NULL;
				static char cachedLastString[LINE_SIZE];
				bool bUsedCached=false;
				PERFINFO_AUTO_STOP_START("NameList_FindAllMatchingStrings", 1);

				if (cachedNameList == pListToUse && stricmp(cachedLastString, &curr_con->input_buf[cmd_start])==0)
				{
					// use previous value
					bUsedCached = true;
				} else {
					eaClear(&ppStrings);
					NameList_FindAllMatchingStrings(pListToUse, &curr_con->input_buf[cmd_start], &ppStrings);
					cachedNameList = pListToUse;
					strcpy(cachedLastString, &curr_con->input_buf[cmd_start]);
				}

				if (eaSize(&ppStrings))
				{
					int numStrings = eaSize(&ppStrings);
#define MAX_COMPLETE_ENTRIES 50
					if (!bUsedCached)
					{
						PERFINFO_AUTO_STOP_START("sort", 1);
						if (numStrings <= MAX_COMPLETE_ENTRIES)
							eaQSort(ppStrings, strCmp);
						else {
							// just want top N
							// sort first N
							PERFINFO_AUTO_START("qsort", 1);
							qsort(ppStrings, MAX_COMPLETE_ENTRIES, sizeof(ppStrings[0]), strCmp);
							PERFINFO_AUTO_STOP_START("insertion sort", 1);
							for (i=MAX_COMPLETE_ENTRIES; i<numStrings; i++)
							{
								char *v = ppStrings[i];
								if (stricmp(v, ppStrings[MAX_COMPLETE_ENTRIES-1]) < 0)
								{
									// should be in the list
									int insertAt;
									insertAt = MAX_COMPLETE_ENTRIES-2;
									// could make this a binary search?
									while (insertAt >= 0 && stricmp(v, ppStrings[insertAt]) < 0)
										insertAt--;
									insertAt++;
									ppStrings[i] = ppStrings[MAX_COMPLETE_ENTRIES-1];
									memmove(&ppStrings[insertAt + 1], &ppStrings[insertAt], sizeof(ppStrings[0])*(MAX_COMPLETE_ENTRIES-1 - insertAt));
									ppStrings[insertAt] = v;
								}
							}
							PERFINFO_AUTO_STOP();
						}
					}
					PERFINFO_AUTO_STOP_START("display", 1);
					// Show drop-down
					for (i=0; i < eaSize(&ppStrings); i++)
					{
						if (i>=MAX_COMPLETE_ENTRIES)
						{
							gfxXYZprintfColor(2 + cmd_start, curr_con->v_chars + 1 + i, CONSOLE_Z+3, 0x7f, 0x7f, 0x7f, 0xff, "...");
							break;
						}
						// TODO: allow clicking on these to complete
						gfxXYZprintfColor(2 + cmd_start, curr_con->v_chars + 1 + i, CONSOLE_Z+3, 0x7f, 0x7f, 0x7f, 0xff, "%s", ppStrings[i]);
					}
				}

				PERFINFO_AUTO_STOP();
			}
			PERFINFO_AUTO_STOP();

			PERFINFO_AUTO_STOP();
		}
	}

	PERFINFO_AUTO_STOP_FUNC_PIX();
}

static void conInsertChar(char c)
{
	if (strlen(curr_con->input_buf) + 1 >= ARRAY_SIZE(curr_con->input_buf))
	{
		// Buffer overflow
		conPrintf("%s", "Console text buffer full");
	} else {
		// Move everything after the cursor one character forward.
		
		memmove(curr_con->input_buf + curr_con->input_pos + 1,
				curr_con->input_buf + curr_con->input_pos,
				(int)strlen(curr_con->input_buf + curr_con->input_pos) + 1);
		
		curr_con->input_buf[curr_con->input_pos++] = c;
	}
}

static void conDeleteCharAt(int pos)
{
	if(pos >= 0){
		memmove(curr_con->input_buf + pos,
				curr_con->input_buf + pos + 1,
				(int)strlen(curr_con->input_buf + pos));
	}
}

static void conRunCommand(void)
{
	if(curr_con->input_buf[0]){
		strcpy(curr_con->last_input,curr_con->input_buf);
		mruAddToList(curr_con->mruHist, curr_con->input_buf);
		{
			char buf[1024];
			char *s;
			strcpy(buf, curr_con->input_buf);
			s = strchr(buf, ' ');
			if (s)
				*s = 0;
			for (s=buf; *s && strchr(" +/-", *s); s++);
			NameList_MRUList_AddName(pMRUCmdNameList, s);
		}
		curr_con->input_pos = 0;
		curr_con->scroll_up = 0;
		curr_con->hist_line = curr_con->mruHist->count;
		curr_con->input_buf[0] = '\0';
		if (curr_con->last_input[0]=='/')
			globCmdParseSpecifyHow(curr_con->last_input+1, CMD_CONTEXT_HOWCALLED_TILDE_WINDOW);
		else
			globCmdParseSpecifyHow(curr_con->last_input, CMD_CONTEXT_HOWCALLED_TILDE_WINDOW);
	}
}
	
// 0 means we couldn't find anything, otherwise, returns the "nth" result that matches the substring pcStart
static int doAutoCompletion(const char* pcStart, const char** pcResult, int iSearchID, bool bSearchBackwards, NameList *pList)
{
	int iResultIndex=0;
	int iSearchIndex = iSearchID + (bSearchBackwards?-1:1);
	const char *pName;
	const char *pFirst = NULL, *pLast = NULL;
	int iLastIndex = 0;
	char start_noUnderscores[1024];
	
	PERFINFO_AUTO_START_FUNC();

	pList->pResetCB(pList);

	strcpy(start_noUnderscores, stripUnderscores(pcStart));

	while ((pName = pList->pGetNextCB(pList, false)))
	{
		if (strStartsWithIgnoreUnderscores(pName, start_noUnderscores) &&
			!NameList_HasEarlierDupe(pList, pName))
		{
			if (iResultIndex == 0)
			{
				pFirst = pName;
			}

			iResultIndex++;

			pLast = pName;
			iLastIndex = iResultIndex;

			if ( iSearchIndex == iResultIndex )
			{
				*pcResult = pName;
				PERFINFO_AUTO_STOP();
				return iResultIndex;
			}
		}
	}
	
	// Wrap at the extremes.

	if (pFirst)
	{
		if ( bSearchBackwards)
		{
			*pcResult = pLast;
			PERFINFO_AUTO_STOP();
			return iLastIndex;
		}
		else
		{
			*pcResult = pFirst;
			PERFINFO_AUTO_STOP();
			return 1;
		}
	}
	
	PERFINFO_AUTO_STOP();
	return 0;
}

static void DumpAutoCompletionList(NameList *pNameList, char *pString)
{
	char **ppStrings = NULL;
	int i;
	
	PERFINFO_AUTO_START_FUNC();

	NameList_FindAllMatchingStrings(pNameList, pString, &ppStrings);

	eaQSort(ppStrings, strCmp);
	if (eaSize(&ppStrings))
	{
		conPrintf("-------------------");
		for (i=0; i < eaSize(&ppStrings); i++)
		{
			conPrintf("%s", ppStrings[i]);
		}
	}
	else
	{
		conPrintf("No matches found");
	}

	eaDestroy(&ppStrings);
	
	PERFINFO_AUTO_STOP();
}
	

static void conGetInput(void)
{
	KeyInput* input;
	static int last_tab_search = 0, last_key_hit_was_tab = 0;
	static char last_tab_search_string[256] = {0};
	static int saved_last_tab_search = 0;
	static char saved_last_tab_search_string[256] = {0};

	PERFINFO_AUTO_START_FUNC();

	conResizeInputCheck();

	for (input = inpGetKeyBuf(); input; inpGetNextKey(&input))
	{
		curr_con->cursor_anim = 8;
	
		if(KIT_EditKey == input->type){
			if(	!(input->attrib & KIA_CONTROL) &&
				(	INP_UP == input->scancode ||
					INP_DOWN == input->scancode))
			{
				if (INP_UP == input->scancode)
					curr_con->hist_line--;
				if (INP_DOWN == input->scancode)
					curr_con->hist_line++;
				if (curr_con->hist_line < 0){
					curr_con->hist_line = 0;
				}
				if (curr_con->hist_line >= curr_con->mruHist->count)
					curr_con->hist_line = curr_con->mruHist->count;

				if(curr_con->hist_line < curr_con->mruHist->count)
					strcpy(curr_con->input_buf,curr_con->mruHist->values[curr_con->hist_line]);
				else
					curr_con->input_buf[0] = '\0';

				curr_con->input_pos = (int)strlen(curr_con->input_buf);

				// clear out tab completion buffers
				last_tab_search_string[0] = 0;
				curr_con->tab_comp_buf[0] = 0;
				curr_con->raw_tab_comp_buf[0] = 0;
			}
			if (input->scancode == INP_BACKSPACE && curr_con->input_pos > 0)
			{
				if (inpLevel(INP_CONTROL) && curr_con->input_pos && curr_con->input_buf[curr_con->input_pos-1]!=' ')
				{
					while (curr_con->input_pos && curr_con->input_buf[curr_con->input_pos-1]!=' ')
					{
						conDeleteCharAt(curr_con->input_pos - 1);
						curr_con->input_pos--;
					}
				} else {
					conDeleteCharAt(curr_con->input_pos - 1);
					curr_con->input_pos--;
				}
				curr_con->tab_comp_buf[0] = 0;
				curr_con->raw_tab_comp_buf[0] = 0;
			}
			if (input->scancode == INP_DELETE && curr_con->input_buf[curr_con->input_pos] != '\0')
			{
				if(inpLevel(INP_CONTROL)){
					if (curr_con->input_buf[curr_con->input_pos]==' ')
						conDeleteCharAt(curr_con->input_pos);
					while (curr_con->input_buf[curr_con->input_pos]!=' ' &&
							curr_con->input_buf[curr_con->input_pos]!='\0')
						conDeleteCharAt(curr_con->input_pos);
					if (curr_con->input_pos && curr_con->input_buf[curr_con->input_pos]==' ' &&
							curr_con->input_buf[curr_con->input_pos-1]==' ')
						conDeleteCharAt(curr_con->input_pos);
				}else{
					conDeleteCharAt(curr_con->input_pos);
				}
			}
			if (input->scancode == INP_RETURN || input->scancode == INP_NUMPADENTER)
			{
				//conPrint(curr_con->input_buf);
				if((int)strlen(curr_con->input_buf)){
					conRunCommand();
					curr_con->tab_comp_buf[0] = 0;
					curr_con->raw_tab_comp_buf[0] = 0;
				}
			}
			if (input->scancode == INP_LEFT && curr_con->input_pos > 0){
				if(input->attrib & KIA_CONTROL)
				{
					// Go to character after first non-whitespace to the left.

					for(;
						curr_con->input_pos > 0 && isspace((unsigned char)curr_con->input_buf[curr_con->input_pos - 1]);
						curr_con->input_pos--);
						
					// Go to character after first whitespace to the left.

					for(;
						curr_con->input_pos > 0 && !isspace((unsigned char)curr_con->input_buf[curr_con->input_pos - 1]);
						curr_con->input_pos--);
				}
				else
				{
					curr_con->input_pos--;
				}
			}
			if (input->scancode == INP_RIGHT && curr_con->input_buf[curr_con->input_pos] != '\0'){
				if(input->attrib & KIA_CONTROL)
				{
					// Go to first whitespace to the right.

					for(;
						curr_con->input_buf[curr_con->input_pos] && !isspace((unsigned char)curr_con->input_buf[curr_con->input_pos]);
						curr_con->input_pos++);
						
					// Go to first non-whitespace to the right.

					for(;
						curr_con->input_buf[curr_con->input_pos] && isspace((unsigned char)curr_con->input_buf[curr_con->input_pos]);
						curr_con->input_pos++);
				}
				else
				{
					curr_con->input_pos++;
				}
			}
			if (input->scancode == INP_HOME)
			{
				curr_con->input_pos = 0;
			}
			if (input->scancode == INP_END)
			{
				curr_con->input_pos = (int)strlen(curr_con->input_buf);
			}
			if (input->scancode == INP_V &&input->attrib & KIA_CONTROL)
			{
#if !PLATFORM_CONSOLE
				int runLast = 0;

				if(OpenClipboard((HWND)rdrGetWindowHandle(gfx_state.currentDevice->rdr_device))){
					HANDLE handle = GetClipboardData(CF_TEXT);

					if(handle){
						char* data = GlobalLock(handle);

						for(; data && *data; data++){
							if(*data == '\n' || *data == '\r'){
								conRunCommand();
								runLast = 1;
							}else{
								conInsertChar(*data);
							}
						}
					}

					GlobalUnlock(handle);
					CloseClipboard();

					if(runLast && curr_con->input_buf[0]){
						conRunCommand();
					}
				}
#endif
			}
			if (input->scancode == INP_C &&input->attrib & KIA_CONTROL)
			{
#if !PLATFORM_CONSOLE
				winCopyToClipboard(curr_con->input_buf);
#endif
			}
			if (input->scancode == INP_ESCAPE)
			{
				curr_con->input_buf[0] = 0;
				curr_con->tab_comp_buf[0] = 0;
				curr_con->raw_tab_comp_buf[0] = 0;
				curr_con->input_pos = 0;
			}
			if (input->scancode == INP_TAB)
			{
				bool bWantDumpOfMatchingNames = false;
					const char* pcTabCompletedString = NULL;
				int search_backwards = 0, cmd_start;

				if ( last_key_hit_was_tab )
				{
					int i = (int)strlen(curr_con->input_buf) - 1;
					// remove trailing spaces
					while ( curr_con->input_buf[i] == ' ' )
					{
						 curr_con->input_buf[i] = 0; 
						 if (curr_con->input_pos > i)
						 {
							 curr_con->input_pos = i;
						 }
						 i--;
					}
				}

				cmd_start = conGetCurCommandStart();

				if ( input->attrib & KIA_CONTROL)
				{
					bWantDumpOfMatchingNames = true;
				}
				else if ( input->attrib & KIA_SHIFT )
					search_backwards = 1;
				
				if ( !last_tab_search_string[0] )
					strcpy(last_tab_search_string,&curr_con->input_buf[cmd_start]);
				if ( bWantDumpOfMatchingNames || curr_con->input_pos - cmd_start >= (int)strlen(curr_con->tab_comp_buf) )
				{
					//if we're at the beginning of the line, do autocompletion on global command names, otherwise query
					//cmdParse to figure out what autcompletion to do, if any
					if (cmd_start == 0)
					{
						if (bWantDumpOfMatchingNames)
						{
							DumpAutoCompletionList(pAllCmdNamesForAutoComplete, last_tab_search_string);
							continue;

						}
						else
						{
							last_tab_search = doAutoCompletion(last_tab_search_string,&pcTabCompletedString,last_tab_search,search_backwards, pAllCmdNamesForAutoComplete);
						}
					}
					else if (curr_con->input_pos >= cmd_start)
					{
						NameList *pListToUse = cmdGetNameListFromPartialCommandString(curr_con->input_buf);
						if (pListToUse)
						{
							if (bWantDumpOfMatchingNames)
							{
								DumpAutoCompletionList(pListToUse, last_tab_search_string);
								continue;
							}
							else
							{	
								last_tab_search = doAutoCompletion(last_tab_search_string,&pcTabCompletedString,last_tab_search,search_backwards, pListToUse);
							}
						}
					}				
				}

				if ( pcTabCompletedString )
				{
					strncpy_s(&curr_con->input_buf[cmd_start], ARRAY_SIZE_CHECKED(curr_con->input_buf) - cmd_start, pcTabCompletedString, _TRUNCATE);
					curr_con->input_pos = (int)strlen(curr_con->input_buf);
					conInsertChar(' ');
					curr_con->tab_comp_buf[0] = 0;
					//strcpy(curr_con->tab_comp_buf,pcTabCompletedString);
				}
				else if ( curr_con->raw_tab_comp_buf[0] )
				{
					// remove trailing spaces from the string
					int i;
					strncpy_s(&curr_con->input_buf[cmd_start], ARRAY_SIZE_CHECKED(curr_con->input_buf) - cmd_start, curr_con->raw_tab_comp_buf, _TRUNCATE);
					curr_con->raw_tab_comp_buf[0] = 0;
					curr_con->tab_comp_buf[0] = 0;
					i = (int)strlen(curr_con->input_buf) - 1;
					while ( curr_con->input_buf[i] == ' ' )
					{
						 curr_con->input_buf[i] = 0; --i;
					}
					curr_con->input_pos = (int)strlen(curr_con->input_buf);
					conInsertChar(' ');
				}

				last_key_hit_was_tab = 1;
			}
			else 
			{
				last_tab_search = 0;
				last_tab_search_string[0] = 0;
				last_key_hit_was_tab = 0;
			}

		}
		
		if(KIT_Text == input->type){
			if (input->character < 128 && 
				input->character >= 32 && 
				input->character != '`' && 
				isprint(input->character) && 
				curr_con->input_pos < (LINE_SIZE - 2))
			{
				const char* pcTabCompletedString = NULL;
				int newSearch = 0, cmd_start_pos;

				last_key_hit_was_tab = 0;

				// find the start of the command to complete
				cmd_start_pos = conGetCurCommandStart();

				if ( !curr_con->tab_comp_buf[0] ||
					input->character != curr_con->tab_comp_buf[curr_con->input_pos-cmd_start_pos] )
				{
					newSearch = 1;
					last_tab_search = 0;
				}
				else {
					last_tab_search = saved_last_tab_search;
					strcpy(last_tab_search_string, saved_last_tab_search_string);
					conDeleteCharAt(curr_con->input_pos);
				}

				// we are performing a new search and we havent done a search from this input position yet
				if ( newSearch && cmd_start_pos <= curr_con->input_pos &&
					!curr_con->tab_comp_buf[curr_con->input_pos-cmd_start_pos] )
					last_tab_search = 0;

				conInsertChar(input->character);

				// dont try to tab complete if they are editing previous input
				if ( cmd_start_pos <= curr_con->input_pos )
				{
					strncpy(last_tab_search_string,&curr_con->input_buf[cmd_start_pos],curr_con->input_pos-cmd_start_pos);
					last_tab_search_string[curr_con->input_pos-cmd_start_pos] = 0;
					// do a new search if the last character they typed didnt match the 
					// string we gave them
					if (newSearch)
					{
						if ( input->character == ' ' )
							last_tab_search = 0;

						//if we're at the beginning of the line, do autocompletion on global command names, otherwise query
						//cmdParse to figure out what autcompletion to do, if any
						if (cmd_start_pos == 0)
						{
							last_tab_search = doAutoCompletion(last_tab_search_string,&pcTabCompletedString,last_tab_search,0, pAllCmdNamesForAutoComplete);
							saved_last_tab_search = last_tab_search;
							strcpy(saved_last_tab_search_string, last_tab_search_string);
						}
						else
						{
							NameList *pListToUse = cmdGetNameListFromPartialCommandString(curr_con->input_buf);
							if (pListToUse)
							{
								last_tab_search = doAutoCompletion(last_tab_search_string,&pcTabCompletedString,last_tab_search,0, pListToUse);
							}
						}

						if ( pcTabCompletedString )
						{
							strcpy(curr_con->tab_comp_buf,pcTabCompletedString);
							strcpy(curr_con->raw_tab_comp_buf,pcTabCompletedString);
						}
						else
						{
							curr_con->tab_comp_buf[0] = 0;
							curr_con->raw_tab_comp_buf[0] = 0;
						}
					}	
				}
			}
		}

		if (input->scancode)
			inpCapture(input->scancode);
	}
	
	PERFINFO_AUTO_STOP();
}



void gfxConsoleProcessInput(void)
{
	PERFINFO_AUTO_START_FUNC();
	
	// The tilde key toggles the console.
	if (enableDebugConsole || gfxGetAccessLevel() > 0)
	{
		KeyInput *input;
		for (input = inpGetKeyBuf(); input; inpGetNextKey(&input))
		{
			if (input->type == KIT_EditKey && input->scancode == INP_GRAVE)
			{
				gfxConsoleEnable(!gfxConsoleVisible());
				break;
			}
		}
	}
	else
	{
		gfxConsoleEnable(0);
	}

	if (!gfxConsoleVisible()){
		PERFINFO_AUTO_STOP();
		return;
	}

	conGetInput();
	
	PERFINFO_AUTO_STOP();
}

int gfxConsoleVisible(void)
{
	if ( !curr_con )
		return false;
	return curr_con->show_console || curr_con->console_visible_this_frame;
}

static KeyBindProfile console_keybinds;

void gfxConsoleEnable(int enable)
{
	static int keybinds_inited = 0;
	if (!keybinds_inited)
	{
		console_keybinds.pchName = "Console Commands";
		console_keybinds.pCmdList = NULL;
		console_keybinds.bTrickleCommands = 1;
		console_keybinds.bTrickleKeys = 0;
		keybinds_inited = 1;
	}

	if (!curr_con)
		return;

	if (enable)
	{
		if (!curr_con->show_console)
			rdrStartTextInput(gfxGetPrimaryDevice());

		keybind_PushProfileEx(&console_keybinds, InputBindPriorityConsole);
		curr_con->show_console = 1;
	}
	else
	{
		if (curr_con->show_console)
			rdrStopTextInput(gfxGetPrimaryDevice());

		keybind_PopProfileEx(&console_keybinds, InputBindPriorityConsole);
		curr_con->show_console = 0;
	}
}

void gfxConsoleAllow(int allow)
{
	enableDebugConsole = allow;
}