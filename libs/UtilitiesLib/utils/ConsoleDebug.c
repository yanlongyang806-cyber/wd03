#include "consoledebug.h"

#if !PLATFORM_CONSOLE

#include <stdio.h>
#include <conio.h>
#include "wininclude.h"
#include "earray.h"
#include "MemoryMonitor.h"
#include "MemReport.h"
#include "cmdparse.h"
#include "sysutil.h"
#include "file.h"
#include "ThreadManager.h"
#include "timing_profiler_interface.h"
#include "GlobalTypes.h"
#include "RegistryReader.h"
#include "utils.h"
#include "stringutil.h"
#include "qsortG.h"
#include "UTF8.h"

#define CRYPTIC_REG_KEY		"HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:conioThread", BUDGET_EngineMisc););

static int gDebugEnabled = 0;

static int gDeactivateConsoleDebug = 0;
AUTO_CMD_INT(gDeactivateConsoleDebug, DeactivateConsoleDebug) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

//this says that we're in the middle of typing in the debug console, in which case we do some special conversion to getch
static bool sbDoSpecialKeyConversionForDebugConsole = false;

bool ConsoleDebugSomeoneIsTyping(void)
{
	return sbDoSpecialKeyConversionForDebugConsole;
}

//because we're just passing ascii chars from the background thread to the console debug, we make up some internal
//8-bit codes for keys we care about. 
#define FAKEKEY_UP ((char)-120)
#define FAKEKEY_DOWN ((char)-121)
#define FAKEKEY_SHIFTTAB ((char)-122)



void ConsoleDisablePrintfToggle(void)
{
	if (IsPrintfDisabled())
	{
		DisablePrintf(false);
		printf_NoTimeStamp("printf is now ENABLED.\n");
	}
	else
	{
		printf_NoTimeStamp("printf is now DISABLED.  (Press 'P' again to reenable.)\n");
		DisablePrintf(true);
	}
}

void ConsoleDebugDisable(void)
{
	printf_NoTimeStamp("Debugging hotkeys disabled.\n");
	gDebugEnabled = 0;
}

static void ConsoleDebugPrintHelp(ConsoleDebugMenu **menus)
{
	FOR_EACH_IN_EARRAY(menus, ConsoleDebugMenu, menu)
	while (menu->cmd || menu->helptext)
	{
		if (menu->cmd && menu->helptext)
		{
			printf_NoTimeStamp("   %c - %s\n", menu->cmd, menu->helptext);
		}
		else if (menu->cmd)
		{
			; // (hidden command)
		}
		else
		{
			printf_NoTimeStamp("%s\n", menu->helptext);
		};
		menu++;
	}
	FOR_EACH_END
}

static ConsoleDebugMenu* ConsoleDebugFind(ConsoleDebugMenu **menus, char ch)
{
	FOR_EACH_IN_EARRAY(menus, ConsoleDebugMenu, menu)
	while (menu->cmd || menu->helptext)
	{
		if (menu->cmd == ch)
			return menu;
		menu++;
	}
	FOR_EACH_END
	if (ch == '`') // tick -> tilde
		return ConsoleDebugFind(menus, '~');
	return NULL;
}

static bool no_apr1=false;
static bool apr1force = false;
// Disables development April Fool's gags
AUTO_CMD_INT(no_apr1, no_apr1) ACMD_COMMANDLINE;
AUTO_CMD_INT(apr1force, apr1force) ACMD_COMMANDLINE;

bool isAprilFools(void)
{
	static bool doneonce=false;
	static bool apr1=false;;
	if (no_apr1)
		return false;
	if (!doneonce)
	{
		if (isDevelopmentMode())
		{
			struct tm t;
			time_t tt = time(NULL);
			localtime_s(&t, &tt);
			if (t.tm_mon == 3 &&
				t.tm_mday == 1)
			{
				apr1 = true;
			} else {
				apr1 = false;
			}
			if(apr1force)
				apr1 = true;
		}
		doneonce = true;
	}
	return apr1;
}


void checkAprilFools(void)
{
#if !PLATFORM_CONSOLE
	if (isAprilFools())
	{
		static int last_time;
		static int last_go_time;
		static bool going=false;
		int now = timeGetTime();
		static int tick;
		static int top_y;
		if (!last_time)
		{
			last_go_time = last_time = now;
		}
		if (now - last_time > 15)
		{
			HANDLE getConsoleHandle(void);
			// Been long enough to consider it
			last_time = now;
			if (!going && now - last_go_time > 60000)
			{
				HANDLE console = getConsoleHandle();
				CONSOLE_SCREEN_BUFFER_INFO cbi;
				GetConsoleScreenBufferInfo(getConsoleHandle(), &cbi);
				last_go_time = now;
				going = true;
				tick = -200;
				top_y = cbi.srWindow.Top - 1;
			}
			if (going)
			{
				HANDLE console = getConsoleHandle();
				WORD rainbow[] = {
					FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE,
					FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE,
					FOREGROUND_RED,
					FOREGROUND_RED,
					FOREGROUND_RED|FOREGROUND_INTENSITY,
					FOREGROUND_RED|FOREGROUND_INTENSITY,
					FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY,
					FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY,
					FOREGROUND_RED|FOREGROUND_GREEN,
					FOREGROUND_RED|FOREGROUND_GREEN,
					FOREGROUND_GREEN,
					FOREGROUND_GREEN,
					FOREGROUND_GREEN|FOREGROUND_INTENSITY,
					FOREGROUND_GREEN|FOREGROUND_INTENSITY,
					FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY,
					FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY,
					FOREGROUND_GREEN|FOREGROUND_BLUE,
					FOREGROUND_GREEN|FOREGROUND_BLUE,
					FOREGROUND_BLUE,
					FOREGROUND_BLUE,
					FOREGROUND_BLUE|FOREGROUND_INTENSITY,
					FOREGROUND_BLUE|FOREGROUND_INTENSITY,
					FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY,
					FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY,
					FOREGROUND_BLUE|FOREGROUND_RED,
					FOREGROUND_BLUE|FOREGROUND_RED,
					FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE,
					FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE,
				};
				DWORD dummy;
				int i;
				CONSOLE_SCREEN_BUFFER_INFO cbi;
				GetConsoleScreenBufferInfo(console, &cbi);
				for (i=0; i<cbi.srWindow.Bottom - cbi.srWindow.Top + 2; i++)
				{
		#define M_PI_2     1.57079632679489661923
					COORD coord = {-tick + (int)(cos(asin(fabs(1-fmod(i / 30.f, 2))))*30), top_y + i};
					WORD *r=rainbow;
					int nr = ARRAY_SIZE(rainbow);
					while (coord.X < 0)
					{
						nr--;
						r++;
						coord.X++;
					}
					if (coord.X + nr > cbi.dwSize.X)
					{
						nr = cbi.dwSize.X - coord.X;
					}
					if (nr>0)
						WriteConsoleOutputAttribute(console, r, nr, coord, &dummy);
				}
				if (0) {
					WORD yellow[16];
					char *unicorn[] = {" \\ji             "," /.(((           ","(,/\"(((__,--.    "," __,_\\  )__( /{  ","\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"  ",};
					for (i=0; i<16; i++)
					{
						yellow[i] = FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
					}
					for (i=0; i<ARRAY_SIZE(unicorn); i++)
					{
						COORD coord2 = {cbi.dwSize.X - 17, i};
						WriteConsoleOutputCharacter_UTF8(console, unicorn[i], (DWORD)strlen(unicorn[i]), coord2, &dummy);
						WriteConsoleOutputAttribute(console, yellow, 16, coord2, &dummy);
					}
				}
				tick++;
				if (tick > cbi.dwSize.X)
				{
					going = false;
				}
			}
		}
	}
#endif
}

#define CONSOLE_HISTORY_SIZE 10

typedef struct ConsoleHistory
{
	char **ppOldCommands; //0 = most recent
	int iMostRecentReturned;
} ConsoleHistory;

static char *ConsoleHistory_GetRegKeyName(void)
{
	static char *spRetVal = NULL;
	if (!spRetVal)
	{
		estrPrintf(&spRetVal, "CONSOLE_HISTORY_%s_%s", GetProductName(), GlobalTypeToName(GetAppGlobalType()));
	}

	return spRetVal;
}

static void ConsoleHistory_WriteToRegistry(ConsoleHistory *pHistory)
{
	char *pOutString = NULL;

	int i;
	RegReader reader = createRegReader();

	if (!eaSize(&pHistory->ppOldCommands))
	{
		return;
	}

	for (i = 0; i < eaSize(&pHistory->ppOldCommands); i++)
	{
		//don't worry about escaping or anything, because we can't get newlines into commands in this console no matter what
		estrConcatf(&pOutString, "%s\n", pHistory->ppOldCommands[i]);
	}

	initRegReader(reader, CRYPTIC_REG_KEY);

	//if we ever have more than one type of string-typable console in the console menu (ie, not just ~ for commands but something else)
	//then you'll have to add some distinguishing info to the key name here
	rrWriteString(reader, ConsoleHistory_GetRegKeyName(), pOutString);
	destroyRegReader(reader);

	estrDestroy(&pOutString);
}

static void ConsoleHistory_ReadFromRegistry(ConsoleHistory *pHistory)
{
	char *pInString = NULL;
	char inBuffer[4096];
	RegReader reader = createRegReader();
	initRegReader(reader, CRYPTIC_REG_KEY);
	if (rrReadString(reader, ConsoleHistory_GetRegKeyName(), SAFESTR(inBuffer)))
	{
		DivideString(inBuffer, "\r\n", &pHistory->ppOldCommands, DIVIDESTRING_STANDARD);
	}

	destroyRegReader(reader);
}

static void ConsoleHistory_AddCommand(ConsoleHistory **ppHandle, char *pString)
{
	ConsoleHistory *pHistory;

	if (!ppHandle)
	{
		return;
	}

	if (!(*ppHandle))
	{
		*ppHandle = calloc(sizeof(ConsoleHistory), 1);
		ConsoleHistory_ReadFromRegistry(*ppHandle);
		(*ppHandle)->iMostRecentReturned = -1;
	}

	pHistory = *ppHandle;

	//check if the command is alreayd in ou rhistory. If so, move it to the front
	if (eaSize(&pHistory->ppOldCommands))
	{
		int iIndex = eaFindString(&pHistory->ppOldCommands, pString);
		if (iIndex != -1)
		{
			if (iIndex == 0)
			{
				pHistory->iMostRecentReturned = -1;
				return;
			}
			else
			{
				char *pRemoved = eaRemove(&pHistory->ppOldCommands, iIndex);
				eaInsert(&pHistory->ppOldCommands, pRemoved, 0);
				pHistory->iMostRecentReturned = -1;
				ConsoleHistory_WriteToRegistry(pHistory);
				return;		
			}
		}
	}

	eaInsert(&pHistory->ppOldCommands, strdup(pString), 0);
	pHistory->iMostRecentReturned = -1;

	if (eaSize(&pHistory->ppOldCommands) > CONSOLE_HISTORY_SIZE)
	{
		free(eaPop(&pHistory->ppOldCommands));
	}

	ConsoleHistory_WriteToRegistry(pHistory);
}


static char *ConsoleHistory_GetCommand(ConsoleHistory **ppHandle, bool bUp)
{
	ConsoleHistory *pHistory;
	int iHistorySize;

	if (!ppHandle)
	{
		return NULL;
	}

	if (!(*ppHandle))
	{
		*ppHandle = calloc(sizeof(ConsoleHistory), 1);
		ConsoleHistory_ReadFromRegistry(*ppHandle);
		(*ppHandle)->iMostRecentReturned = -1;
	}

	pHistory = *ppHandle;

	iHistorySize = eaSize(&pHistory->ppOldCommands);

	if (!iHistorySize)
	{
		return NULL;
	}

	if (iHistorySize == 1)
	{
		pHistory->iMostRecentReturned = 0;
	}
	else if (pHistory->iMostRecentReturned == -1)
	{
		pHistory->iMostRecentReturned = 0;
	}
	else
	{
		if (bUp)
		{
			pHistory->iMostRecentReturned++;
		}
		else
		{
			pHistory->iMostRecentReturned += iHistorySize - 1;
		}

		pHistory->iMostRecentReturned %= iHistorySize;
	}

	return pHistory->ppOldCommands[pHistory->iMostRecentReturned];
}


typedef struct ConsoleAutoComplete
{
	char **ppCurNames;
	int iLastReturnedIndex; //if -1, means that we have not searched for names currently
} ConsoleAutoComplete;

char *ConsoleAutoComplete_DoAutoComplete(ConsoleAutoComplete **ppHandle, char *pInString, int iInLen, bool bBackward)
{
	ConsoleAutoComplete *pAutoComplete;
	static char *pWorkString = NULL;

	if (!ppHandle)
	{
		return NULL;
	}

	if (!iInLen)
	{
		return NULL;
	}

	estrSetSize(&pWorkString, iInLen);
	memcpy(pWorkString, pInString, iInLen);

	if (IS_WHITESPACE(pWorkString[iInLen - 1]))
	{
		return NULL;
	}

	estrTrimLeadingAndTrailingWhitespace(&pWorkString);
	if (strchr(pWorkString, ' '))
	{
		return NULL;
	}

	if (!(*ppHandle))
	{
		*ppHandle = calloc(sizeof(ConsoleAutoComplete), 1);
		(*ppHandle)->iLastReturnedIndex = -1;
	}

	pAutoComplete = *ppHandle;

	if (pAutoComplete->iLastReturnedIndex == -1)
	{
		NameList_FindAllPrefixMatchingStrings(pAllCmdNamesForAutoComplete, pWorkString, &pAutoComplete->ppCurNames);
		if (eaSize(&pAutoComplete->ppCurNames))
		{
			eaQSort(pAutoComplete->ppCurNames, strCmp);
		}
	}

	if (!eaSize(&pAutoComplete->ppCurNames))
	{
		return NULL;
	}

	if (pAutoComplete->iLastReturnedIndex == -1)
	{
		pAutoComplete->iLastReturnedIndex = 0;
	}
	else
	{
		pAutoComplete->iLastReturnedIndex += bBackward ? (eaSize(&pAutoComplete->ppCurNames) - 1) : 1;
		pAutoComplete->iLastReturnedIndex %= eaSize(&pAutoComplete->ppCurNames);
	}

	return pAutoComplete->ppCurNames[pAutoComplete->iLastReturnedIndex];
}

void ConsoleAutoComplete_Clear(ConsoleAutoComplete **ppHandle)
{
	ConsoleAutoComplete *pAutoComplete;

	if (!ppHandle)
	{
		return;
	}

	pAutoComplete = *ppHandle;

	if (!pAutoComplete)
	{
		return;
	}

	eaDestroy(&pAutoComplete->ppCurNames);
	pAutoComplete->iLastReturnedIndex = -1;
}


void DoConsoleDebugMenu(ConsoleDebugMenu ***pmenus)
{
	static char param[200];
	static int param_idx=0;			// i'th letter in param string, 0 if not entering a param
	static char param_cmd=' ';
	static int keyWasPressed=1;
	char ch;
	ConsoleDebugMenu* cmd;
	ConsoleDebugMenu **menus = *pmenus;
	U32 startTime = timeGetTime();

	if(gDeactivateConsoleDebug)
	{
		gDebugEnabled = 0;
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	checkAprilFools();

	while(timeGetTime() - startTime < 10){
		HWND	hwnd = compatibleGetConsoleWindow();
		S32		keyIsPressed = 0;
		
		if(hwnd){
			S32 isVisible = keyWasPressed;
			
			if(!keyWasPressed){
				PERFINFO_AUTO_START("IsWindowVisible", 1);
					isVisible = IsWindowVisible(hwnd);
				PERFINFO_AUTO_STOP();
				
				if(isVisible){
					PERFINFO_AUTO_START("IsIconic", 1);
						isVisible = !IsIconic(hwnd);
					PERFINFO_AUTO_STOP();

					if(isVisible){
						#if 1
							WINDOWINFO wi;
							
							wi.cbSize = sizeof(wi);
							
							PERFINFO_AUTO_START("GetWindowInfo", 1);
								GetWindowInfo(hwnd, &wi);
								
								isVisible = wi.dwWindowStatus == WS_ACTIVECAPTION;
							PERFINFO_AUTO_STOP();
						#else
							PERFINFO_AUTO_START("GetForegroundWindow", 1);
							{
								isVisible = GetForegroundWindow() == hwnd;
							}
							PERFINFO_AUTO_STOP();
						#endif
					}
				}
			}
			
			if(isVisible){
				PERFINFO_AUTO_START("kbhit", 1);
					keyIsPressed = _kbhit();
				PERFINFO_AUTO_STOP();
			}
		}

		if(!keyIsPressed){
			keyWasPressed = 0;
			break;
		}
		
		keyWasPressed = 1;

		if (gDebugEnabled < 2)
		{
			if (_getch()=='d') {
				gDebugEnabled++;
				if (gDebugEnabled==2) {
					printf_NoTimeStamp("Debugging hotkeys enabled (Press '?' for a list).\n");
				}
			} else {
				gDebugEnabled = 0;
			}
		}
		// cheesy way to get a parameter from the user..
		else if (param_idx)
		{
			ch = _getch();
			if (ch == FAKEKEY_UP || ch == FAKEKEY_DOWN)
			{
				cmd = ConsoleDebugFind(menus, param_cmd);
				if (cmd)
				{
					char *pNewString = ConsoleHistory_GetCommand(&cmd->pConsoleHistory, ch == FAKEKEY_UP);
					if (pNewString)
					{
						int iPrevLen = param_idx - 1;
						int iNewLen = (int)strlen(pNewString);
						int i;

						if (iNewLen >= ARRAY_SIZE(param))
						{
							iNewLen = ARRAY_SIZE(param) - 1;
							pNewString[iNewLen] = 0;
						}

						for (i = 0; i < iPrevLen; i++)
						{
							printf_NoTimeStamp("\b \b");
						}
						printf_NoTimeStamp("%s", pNewString);

						memcpy(param, pNewString, iNewLen + 1);
						param_idx = iNewLen + 1;
					}
				}
			}
			else if (ch == '\t' || ch == FAKEKEY_SHIFTTAB)
			{
				cmd = ConsoleDebugFind(menus, param_cmd);
				if (cmd)
				{
					char *pNewString = ConsoleAutoComplete_DoAutoComplete(&cmd->pAutoComplete, param, param_idx - 1, ch == FAKEKEY_SHIFTTAB);
					if (pNewString)
					{
						int iPrevLen = param_idx - 1;
						int iNewLen = (int)strlen(pNewString);
						int i;

						if (iNewLen >= ARRAY_SIZE(param))
						{
							iNewLen = ARRAY_SIZE(param) - 1;
							pNewString[iNewLen] = 0;
						}

						for (i = 0; i < iPrevLen; i++)
						{
							printf_NoTimeStamp("\b \b");
						}
						printf_NoTimeStamp("%s", pNewString);

						memcpy(param, pNewString, iNewLen + 1);
						param_idx = iNewLen + 1;
					}
				}			



			}
			else if (ch == '\b')
			{
				if (param_idx > 1) 
				{
					param_idx--;
					printf_NoTimeStamp("\b \b");
					cmd = ConsoleDebugFind(menus, param_cmd);
					if (cmd && cmd->paramfunc)
					{
						ConsoleAutoComplete_Clear(&cmd->pAutoComplete);
					}
				}
			}
			else if (ch == '\n' || ch == '\r')
			{
				param[param_idx-1] = 0;
				printf_NoTimeStamp("\n");
				cmd = ConsoleDebugFind(menus, param_cmd);
				if (cmd && cmd->paramfunc)
				{
					ConsoleHistory_AddCommand(&cmd->pConsoleHistory, param);
					ConsoleAutoComplete_Clear(&cmd->pAutoComplete);

					cmd->paramfunc(param);
					//after executing a paramfunc, eat all remaining keystrokes, in case someone accidentally pasted a bunch of crap
					//into the window

					while (_kbhit())
					{
						char c = _getch();
					}
				}
				param_idx = 0;
				sbDoSpecialKeyConversionForDebugConsole = false;

			}
			else
			{
				param[param_idx-1] = ch;
				param_idx++;
				printf_NoTimeStamp("%c", ch);
				if (param_idx >= ARRAY_SIZE(param)-1)
				{
					param[ARRAY_SIZE(param)-1] = 0;
					printf_NoTimeStamp("\nString too long!  Ignoring..\n");
					param_idx = 0;

					//eat all keystrokes, in case someone pasted a bunch of crap
					while (_kbhit())
					{
						char c = _getch();
					}
				}

				cmd = ConsoleDebugFind(menus, param_cmd);
				if (cmd && cmd->paramfunc)
				{
					ConsoleAutoComplete_Clear(&cmd->pAutoComplete);
				}
			}
		}
		// otherwise, just a single-keystroke command..
		else
		{
			ch = _getch();
			cmd = ConsoleDebugFind(menus, ch);
			if (cmd)
			{
				if (cmd->singlekeyfunc)
					cmd->singlekeyfunc();
				if (cmd->paramfunc)
				{
					param_idx = 1;
					param_cmd = ch;
					printf_NoTimeStamp(">");
					sbDoSpecialKeyConversionForDebugConsole = true;
				}
			}
			else if (ch == '?')
			{
				ConsoleDebugPrintHelp(menus);
			}
			else
			{
				printf_NoTimeStamp("unrecognized command ('?' for list of commands)\n");
			}
		}
	}	
	PERFINFO_AUTO_STOP();
}

static void ConsoleDebugMemLogDump(void)
{
	memCheckDumpAllocs();
	printf_NoTimeStamp("Done.\n");
}

static void ConsoleCmdParse(char *str)
{
	char *pRetString = NULL;
	globCmdParseAndReturnWithFlags(str, &pRetString, CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL, CMD_CONTEXT_HOWCALLED_DDCONSOLE);
	if (estrLength(&pRetString))
		printf_NoTimeStamp("%s\n", pRetString);
	estrDestroy(&pRetString);

}

static ConsoleDebugMenu default_menu_items[] = {
	{'m', "Display memory usage", mmdsShort, NULL},
	{'M', "Display per-line memory usage", mmplShort, NULL},
	{'n', "Dump memlog.txt/memlogSA.txt", ConsoleDebugMemLogDump, NULL},
	{'P', "Toggle printf on/off", ConsoleDisablePrintfToggle, NULL},
	{'~', "Execute arbitrary command", NULL, ConsoleCmdParse},
	{'D', "Disable debug console", ConsoleDebugDisable, NULL},
	{0},
};

ConsoleDebugMenu ***GetDefaultConsoleDebugMenu(void)
{
	static ConsoleDebugMenu **default_menu;
	if (!default_menu) {
		eaPush(&default_menu, default_menu_items);
	}
	return &default_menu;
}

void ConsoleDebugAddToDefault(ConsoleDebugMenu* menu){
	ConsoleDebugMenu*** menuDefault = GetDefaultConsoleDebugMenu();
	
	eaPush(menuDefault, menu);
}

HANDLE hAteChar;
HANDLE hCharReady;
ManagedThread *threadConio;

#define GETCH_RINGBUFFER_SIZE (1<<8)
#define GETCH_RINGBUFFER_MASK (GETCH_RINGBUFFER_SIZE - 1)
S32 getch_ringbuffer[GETCH_RINGBUFFER_SIZE];
volatile S32 getch_ringbuffer_read_idx=0;

#pragma push_macro("_getch")
#pragma push_macro("_kbhit")
#undef _getch
#undef _kbhit


// If inside of getch() calls, Ctrl+C is ignored, this should work as a
//  replacement without that side effect
int getchAllowCtrlC(HANDLE hConsole)
{
	S32 ch=0;
	DWORD num_read=0;
	INPUT_RECORD input_record;

	do 
	{
		if (!ReadConsoleInput(hConsole, &input_record, 1, &num_read))
		{
			// Error reading from the console, probably reading from an empty pipe
			// sleep and retry?  Though will likely never succeed
			do {
				Sleep(1000);
			} while (!ReadConsoleInput(hConsole, &input_record, 1, &num_read));
		}
		assert(num_read == 1);
		num_read = 0;
		if (input_record.EventType == KEY_EVENT)
		{
			if (input_record.Event.KeyEvent.bKeyDown)
			{
				num_read = 1;

				if (sbDoSpecialKeyConversionForDebugConsole)
				{
					if (input_record.Event.KeyEvent.uChar.AsciiChar == '\t' && (input_record.Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED))
					{
						ch = FAKEKEY_SHIFTTAB;
					}
					else if (!input_record.Event.KeyEvent.uChar.AsciiChar)
					{
	
						switch (input_record.Event.KeyEvent.wVirtualKeyCode)
						{
						xcase VK_UP:
							ch = FAKEKEY_UP;
						xcase VK_DOWN:
							ch = FAKEKEY_DOWN;
						xdefault:
							ch = 0;
						}
					
					}
					else
					{
						ch = input_record.Event.KeyEvent.uChar.AsciiChar;
					}
				}
				else
				{
					ch = input_record.Event.KeyEvent.uChar.AsciiChar;
				}
			}
		}
	}
	while (!num_read || !ch);

	assert(num_read == 1);
	return ch;
}

DWORD WINAPI conioThread(LPVOID lpParam)
{
	S32 getch_ringbuffer_write_idx=0;
	HANDLE hConsole;
	EXCEPTION_HANDLER_BEGIN;
	hConsole = GetStdHandle(STD_INPUT_HANDLE);
	SetConsoleMode(hConsole, ENABLE_PROCESSED_INPUT);
	while (true)
	{
		S32 ch;
		autoTimerThreadFrameBegin("conioThread");
		PERFINFO_AUTO_START_BLOCKING("getch()", 1);
		ch = getchAllowCtrlC(hConsole);
		PERFINFO_AUTO_STOP();
		assert(ch);
		while (getch_ringbuffer[getch_ringbuffer_write_idx])
		{
			// Full, wait for character consumed
			HRESULT hr;
			WaitForSingleObjectWithReturn(hAteChar, INFINITE, hr);
			assert(hr == WAIT_OBJECT_0);
		}
		getch_ringbuffer[getch_ringbuffer_write_idx] = ch;
		getch_ringbuffer_write_idx = (getch_ringbuffer_write_idx + 1) & GETCH_RINGBUFFER_MASK;
		MemoryBarrier();
		SetEvent(hCharReady);
		autoTimerThreadFrameEnd();
	}
	EXCEPTION_HANDLER_END;
	return 0;
}

void conioInit(void)
{
	ATOMIC_INIT_BEGIN;
	hAteChar = CreateEvent(NULL, FALSE, FALSE, NULL);
	hCharReady = CreateEvent(NULL, FALSE, FALSE, NULL);
	threadConio= tmCreateThread(conioThread, NULL);
	ATOMIC_INIT_END;
}

int getchFast(void)
{
	if (isCrashed())
		return _getch();
	conioInit();
	while (true)
	{
		S32 oldread_idx, newread_idx;
		S32 ret;
		assert(getch_ringbuffer_read_idx < ARRAY_SIZE(getch_ringbuffer));
		while (!getch_ringbuffer[getch_ringbuffer_read_idx])
		{
			HRESULT hr;
			WaitForSingleObjectWithReturn(hCharReady, INFINITE, hr);
			assert(hr == WAIT_OBJECT_0);
		}
		// Get value
		ret = getch_ringbuffer[getch_ringbuffer_read_idx];
		// Increment read_idx
		oldread_idx = getch_ringbuffer_read_idx;
		newread_idx = (oldread_idx+1) & GETCH_RINGBUFFER_MASK;
		if (oldread_idx == InterlockedCompareExchange(&getch_ringbuffer_read_idx, newread_idx, oldread_idx))
		{
			// If we got it, zero out the value, and return
			getch_ringbuffer[oldread_idx] = 0;
			MemoryBarrier();
			SetEvent(hAteChar);
			return ret;
		}
	}
}

int kbhitFast(void)
{
	if (isCrashed())
		return _kbhit();
	// TODO: In extreme cases, this could return false negatives if the buffer
	//  was completely emptied during a single getch() call in the background
	//  thread.
	conioInit();
	return getch_ringbuffer[getch_ringbuffer_read_idx];
}

#pragma pop_macro("_kbhit")
#pragma pop_macro("_getch")

#else

void DoConsoleDebugMenu(ConsoleDebugMenu ***menus)
{
}
ConsoleDebugMenu ***GetDefaultConsoleDebugMenu(void)
{
	return NULL;
}



#endif
