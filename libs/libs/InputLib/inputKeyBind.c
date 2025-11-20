#include "cmdparse.h"
#include "eset.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "sysutil.h"
#include "expression.h"
#include "StringCache.h"
#include "Message.h"
#include "ThreadManager.h"
#include "StringUtil.h"

#include "input.h"
#include "inputLib.h"
#include "inputKeyBind.h"

#include "inputKeybind_h_ast.c"

#if !PLATFORM_CONSOLE
#include <windows.h>

// Seems like I shouldn't need these, but I can not find these values
// anywhere in windows.h
// JE: They likely exist if you have a newer PlatformSDK installed, our Intel contacts are getting errors here
#if !defined(MAPVK_VK_TO_VSC)
#	define MAPVK_VK_TO_VSC		0
#	define MAPVK_VSC_TO_VK		1
#	define MAPVK_VK_TO_CHAR		2
#	define MAPVK_VSC_TO_VK_EX	3
#	define MAPVK_VK_TO_VSC_EX	4
#endif
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// holder for parsed data list - bad structure naming here
static KeyBindStoredProfiles s_ParsedData = {0};

static KeyBindStoredProfiles s_ActiveProfiles[InputBindPriorityCount];

static KeyBindProfile s_GameProfile = {0};
static KeyBindProfile s_UserLowPriProfile = {0};
static KeyBindProfile s_UserProfile = {0};

KeyBindPrintf g_inpPrintf;
static KeyBindSetTimeStamp s_cbKeyBindSetTimeStamp;
static KeyBindQuit s_cbKeyBindQuit;

// Bind a key to a command; binds just beneath development priority
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(lowpribind) ACMD_CATEGORY(Standard) ACMD_HIDE;
void BindLowPriKeyCommand(const char *pchKey, ACMD_NAMELIST(pAllCmdNamesForAutoComplete) ACMD_SENTENCE pchCommand)
{
	keybind_BindKeyInProfile(&s_UserLowPriProfile, pchKey, pchCommand);
}

// Bind a key to a command.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(bind_local) ACMD_CATEGORY(Standard);
void BindKeyCommand(const char *pchKey, ACMD_NAMELIST(pAllCmdNamesForAutoComplete) ACMD_SENTENCE pchCommand)
{
	keybind_BindKeyInProfile(&s_UserProfile, pchKey, pchCommand);
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(bindexpr);
void BindKeyExpr(ExprContext* context, const char *pchKey, const char *pchCommand)
{
	keybind_BindKeyInProfile(&s_UserProfile, pchKey, pchCommand);
}

// Unbind a key from a command at game priority
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(lowpriunbind) ACMD_CATEGORY(Standard) ACMD_HIDE;
void UnbindLowPriKeyCommand(const char *pchKey)
{
	keybind_BindKeyInProfile(&s_UserLowPriProfile, pchKey, NULL);
}

// Unbind a key from a command (this happens automatically when rebinding as well).
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(unbind_local) ACMD_CATEGORY(Standard);
void UnbindKeyCommand(const char *pchKey)
{
	keybind_BindKeyInProfile(&s_UserProfile, pchKey, NULL);
}

// Load keybinds from keybinds.txt.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(bind_local_load) ACMD_CATEGORY(Standard);
void BindLoadCommand(void)
{
	keybind_LoadUserBinds(NULL);
}

// Save keybinds to keybinds.txt.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(bind_local_save) ACMD_CATEGORY(Standard);
void BindSaveCommand(void)
{
	keybind_SaveUserBinds(NULL);
}

// Load keybinds from the given filename.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(bind_local_load_file) ACMD_CATEGORY(Standard);
void BindLoadFileCommand(const char *pchFilename)
{
	keybind_LoadUserBinds(pchFilename);
}

// Save keybinds to the given filename.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(bind_local_save_file) ACMD_CATEGORY(Standard);
void BindSaveFileCommand(const char *pchFilename)
{
	keybind_SaveUserBinds(pchFilename);
}

// Push a specific key profile onto the stack
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(bind_push_profile) ACMD_CATEGORY(UI);
void BindPushProfile(const char *pchProfile)
{
	keybind_PushProfileName(pchProfile);
}

// Pop the given key profile from the stack
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(bind_pop_profile) ACMD_CATEGORY(UI);
void BindPopProfile(const char *pchProfile)
{
	keybind_PopProfileName(pchProfile);
}

AUTO_STARTUP(Keybinds) ASTRT_DEPS(AS_Messages);
void ValidateBindings(void)
{
	int i;

	for(i=eaSize(&s_ParsedData.eaProfiles)-1; i>=0; --i) {
		KeyBindProfile *pProfile = s_ParsedData.eaProfiles[i];
		if (!GET_REF(pProfile->hNameMsg) && REF_STRING_FROM_HANDLE(pProfile->hNameMsg)) {
			ErrorFilenamef(pProfile->pchFilename, "Keybind refers to non-existing message '%s'", REF_STRING_FROM_HANDLE(pProfile->hNameMsg));
		}
	}
}

static void ParseBindings(void)
{
	static bool s_bDoneOnce = false;
	if (!s_bDoneOnce)
	{
		s_bDoneOnce = true;

		loadstart_printf("Loading keybinds...");

		ParserLoadFiles("keybinds", ".kb", "kb.bin", 0, parse_KeyBindStoredProfiles, &s_ParsedData);

		loadend_printf(" done (%d KeyBind Profiles).", eaSize(&s_ParsedData.eaProfiles));
	}
}

static void KeyBindFree(KeyBind *pBind)
{
	StructDestroy(parse_KeyBind, pBind);
}

typedef struct
{
	S32 iKey;
	const char *pchName;
	const char *pchName2;
} SwitchToken;

#define 	SW_NAME(x) {x,#x},
#define 	SW_NAME2(x,c) {x,#x,c},

//Mapping between key value and key name for special keys only
static SwitchToken s_aKeyNames[] =
{
	SW_NAME(INP_MULTIPLY)
	SW_NAME(INP_BACKSPACE)
	SW_NAME(INP_LCONTROL)
	SW_NAME(INP_LSHIFT)
	SW_NAME(INP_RSHIFT)
	SW_NAME(INP_SPACE)
	SW_NAME(INP_SUBTRACT)
	SW_NAME(INP_CAPITAL)
	SW_NAME(INP_F1)
	SW_NAME(INP_F2)
	SW_NAME(INP_F3)
	SW_NAME(INP_F4)
	SW_NAME(INP_F5)
	SW_NAME(INP_F6)
	SW_NAME(INP_F7)
	SW_NAME(INP_F8)
	SW_NAME(INP_F9)
	SW_NAME(INP_F10)
	SW_NAME(INP_NUMLOCK)
	SW_NAME(INP_SCROLL)
	SW_NAME(INP_NUMPAD7)
	SW_NAME(INP_NUMPAD8)
	SW_NAME(INP_NUMPAD9)
	SW_NAME(INP_NUMPAD4)
	SW_NAME(INP_NUMPAD5)
	SW_NAME(INP_NUMPAD6)
	SW_NAME(INP_ADD)
	SW_NAME(INP_NUMPAD1)
	SW_NAME(INP_NUMPAD2)
	SW_NAME(INP_NUMPAD3)
	SW_NAME(INP_NUMPAD0)
	SW_NAME(INP_DECIMAL)
	SW_NAME(INP_F11)
	SW_NAME(INP_F12)
	SW_NAME(INP_F13)
	SW_NAME(INP_F14)
	SW_NAME(INP_F15)
	SW_NAME(INP_KANA)
	SW_NAME(INP_CONVERT)
	SW_NAME(INP_KANJI)
	SW_NAME(INP_RCONTROL)
	SW_NAME(INP_DIVIDE)
	SW_NAME(INP_TAB)
	SW_NAME(INP_HOME)
	SW_NAME(INP_UP)
	SW_NAME(INP_LEFT)
	SW_NAME(INP_RIGHT)
	SW_NAME(INP_END)
	SW_NAME(INP_DOWN)
	SW_NAME(INP_INSERT)
	SW_NAME(INP_LWIN)
	SW_NAME(INP_RWIN)
	SW_NAME(INP_APPS)
	SW_NAME(INP_LBUTTON)
	SW_NAME(INP_RBUTTON)
	SW_NAME(INP_MBUTTON)
	SW_NAME(INP_BUTTON4)
	SW_NAME(INP_BUTTON5)
	SW_NAME(INP_BUTTON6)
	SW_NAME(INP_BUTTON7)
	SW_NAME(INP_BUTTON8)
	SW_NAME(INP_MOUSEWHEEL)
	SW_NAME(INP_CONTROL)
	SW_NAME(INP_PAUSE)
	SW_NAME(INP_SYSRQ)
	SW_NAME(INP_NUMPADENTER) 
	SW_NAME2(INP_DELETE, "DELETE")
	SW_NAME2(INP_PRIOR,"PAGEUP")
	SW_NAME2(INP_NEXT,"PAGEDOWN")
	SW_NAME2(INP_RETURN,"ENTER")
	SW_NAME2(INP_ESCAPE,"ESC")
	SW_NAME2(INP_LMENU,"LALT")
	SW_NAME2(INP_RMENU,"RALT")
	SW_NAME2(INP_ALT,"ALT")
	SW_NAME2(INP_CONTROL,"CTRL")
	SW_NAME2(INP_LCONTROL,"LCTRL")
	SW_NAME2(INP_RCONTROL,"RCTRL")
	SW_NAME2(INP_SHIFT,"SHIFT")
	SW_NAME2(INP_UP,"UPARROW")
	SW_NAME2(INP_DOWN,"DOWNARROW")
	SW_NAME2(INP_LEFT,"LEFTARROW")
	SW_NAME2(INP_RIGHT,"RIGHTARROW")

	SW_NAME(INP_JOY1)	
	SW_NAME(INP_JOY2)
	SW_NAME(INP_JOY3)
	SW_NAME(INP_JOY4)
	SW_NAME(INP_JOY5)
	SW_NAME(INP_JOY6)
	SW_NAME(INP_JOY7)
	SW_NAME(INP_JOY8)
	SW_NAME(INP_JOY9)
	SW_NAME(INP_JOY10)

	SW_NAME(INP_JOY11)
	SW_NAME(INP_JOY12)
	SW_NAME(INP_JOY13)
	SW_NAME(INP_JOY14)
	SW_NAME(INP_JOY15)
	SW_NAME(INP_JOY16)
	SW_NAME(INP_JOY17)
	SW_NAME(INP_JOY18)
	SW_NAME(INP_JOY19)
	SW_NAME(INP_JOY20)
	SW_NAME(INP_JOY21)
	SW_NAME(INP_JOY22)
	SW_NAME(INP_JOY23)
	SW_NAME(INP_JOY24)
	SW_NAME(INP_JOY25)

	SW_NAME(INP_DJOY1)	
	SW_NAME(INP_DJOY2)
	SW_NAME(INP_DJOY3)
	SW_NAME(INP_DJOY4)
	SW_NAME(INP_DJOY5)
	SW_NAME(INP_DJOY6)
	SW_NAME(INP_DJOY7)
	SW_NAME(INP_DJOY8)
	SW_NAME(INP_DJOY9)
	SW_NAME(INP_DJOY10)

	SW_NAME(INP_DJOY11)
	SW_NAME(INP_DJOY12)
	SW_NAME(INP_DJOY13)
	SW_NAME(INP_DJOY14)
	SW_NAME(INP_DJOY15)
	SW_NAME(INP_DJOY16)
	SW_NAME(INP_DJOY17)
	SW_NAME(INP_DJOY18)
	SW_NAME(INP_DJOY19)
	SW_NAME(INP_DJOY20)
	SW_NAME(INP_DJOY21)
	SW_NAME(INP_DJOY22)
	SW_NAME(INP_DJOY23)
	SW_NAME(INP_DJOY24)
	SW_NAME(INP_DJOY25)

	SW_NAME(INP_JOYPAD_UP)
	SW_NAME(INP_JOYPAD_DOWN)
	SW_NAME(INP_JOYPAD_LEFT)
	SW_NAME(INP_JOYPAD_RIGHT)

	SW_NAME(INP_POV1_UP)
	SW_NAME(INP_POV1_DOWN)
	SW_NAME(INP_POV1_LEFT)
	SW_NAME(INP_POV1_RIGHT)

	SW_NAME(INP_POV2_UP)
	SW_NAME(INP_POV2_DOWN)
	SW_NAME(INP_POV2_LEFT)
	SW_NAME(INP_POV2_RIGHT)

	SW_NAME(INP_POV3_UP)
	SW_NAME(INP_POV3_DOWN)
	SW_NAME(INP_POV3_LEFT)
	SW_NAME(INP_POV3_RIGHT)


	SW_NAME(INP_JOYSTICK1_UP)
	SW_NAME(INP_JOYSTICK1_DOWN)
	SW_NAME(INP_JOYSTICK1_LEFT)
	SW_NAME(INP_JOYSTICK1_RIGHT)

	SW_NAME(INP_JOYSTICK2_UP)
	SW_NAME(INP_JOYSTICK2_DOWN)
	SW_NAME(INP_JOYSTICK2_LEFT)
	SW_NAME(INP_JOYSTICK2_RIGHT)

	SW_NAME(INP_JOYSTICK3_UP)
	SW_NAME(INP_JOYSTICK3_DOWN)
	SW_NAME(INP_JOYSTICK3_LEFT)
	SW_NAME(INP_JOYSTICK3_RIGHT)

	SW_NAME(INP_AB)
	SW_NAME(INP_BB)
	SW_NAME(INP_XB)
	SW_NAME(INP_YB)
	SW_NAME(INP_RB)
	SW_NAME(INP_LB)
	SW_NAME(INP_RSTICK)
	SW_NAME(INP_LSTICK)
	SW_NAME(INP_START)
	SW_NAME(INP_SELECT)

	SW_NAME(INP_LTRIGGER)
	SW_NAME(INP_RTRIGGER)

	SW_NAME(INP_DAB)
	SW_NAME(INP_DBB)
	SW_NAME(INP_DXB)
	SW_NAME(INP_DYB)
	SW_NAME(INP_DRB)
	SW_NAME(INP_DLB)
	SW_NAME(INP_DRSTICK)
	SW_NAME(INP_DLSTICK)
	SW_NAME(INP_DSTART)
	SW_NAME(INP_DSELECT)

	SW_NAME(INP_DLTRIGGER)
	SW_NAME(INP_DRTRIGGER)

	SW_NAME(INP_LSTICK_UP)
	SW_NAME(INP_LSTICK_DOWN)
	SW_NAME(INP_LSTICK_LEFT)
	SW_NAME(INP_LSTICK_RIGHT)
	SW_NAME(INP_RSTICK_UP)
	SW_NAME(INP_RSTICK_DOWN)
	SW_NAME(INP_RSTICK_LEFT)
	SW_NAME(INP_RSTICK_RIGHT)

	SW_NAME2(INP_MOUSE_CHORD,			"MouseChord")
	SW_NAME2(INP_LCLICK,				"LeftClick")
	SW_NAME2(INP_MCLICK,				"MiddleClick")
	SW_NAME2(INP_RCLICK,				"RightClick")
	SW_NAME2(INP_LDRAG,					"LeftDrag")
	SW_NAME2(INP_MDRAG,					"MiddleDrag")
	SW_NAME2(INP_RDRAG,					"RightDrag")
	SW_NAME2(INP_MOUSEWHEEL_FORWARD,	"wheelplus")
	SW_NAME2(INP_MOUSEWHEEL_BACKWARD,	"wheelminus")  
	SW_NAME2(INP_LDBLCLICK,				"LeftDoubleClick")
	SW_NAME2(INP_MDBLCLICK,				"MiddleDoubleClick")
	SW_NAME2(INP_RDBLCLICK,				"RightDoubleClick")	
	// 	SW_NAME(INP_CIRCUMFLEX) SW_NAME(INP_NUMPADCOMMA) SW_NAME(INP_NUMPADEQUALS) SW_NAME(INP_GRAVE)
	// SW_NAME(INP_NOCONVERT) SW_NAME(INP_AT)	SW_NAME(INP_COLON )	SW_NAME(INP_UNLABELED)	SW_NAME(INP_UNDERLINE)
	// SW_NAME(INP_SYSRQ)		SW_NAME(INP_YEN)	SW_NAME(INP_AX)
	// SW_NAME(INP_STOP)
};

static SwitchToken s_aNormalKeyNames[] = {
	SW_NAME(INP_1)
	SW_NAME(INP_2)
	SW_NAME(INP_3)
	SW_NAME(INP_4)
	SW_NAME(INP_5)
	SW_NAME(INP_6)
	SW_NAME(INP_7)
	SW_NAME(INP_8)
	SW_NAME(INP_9)
	SW_NAME(INP_0)

	SW_NAME(INP_Q)
	SW_NAME(INP_W)
	SW_NAME(INP_E)
	SW_NAME(INP_R)
	SW_NAME(INP_Y)
	SW_NAME(INP_U)
	SW_NAME(INP_I)
	SW_NAME(INP_O)
	SW_NAME(INP_P)
	SW_NAME(INP_A)
	SW_NAME(INP_S)
	SW_NAME(INP_D)
	SW_NAME(INP_F)
	SW_NAME(INP_G)
	SW_NAME(INP_H)
	SW_NAME(INP_J)
	SW_NAME(INP_K)
	SW_NAME(INP_L)
	SW_NAME(INP_Z)
	SW_NAME(INP_X)
	SW_NAME(INP_C)
	SW_NAME(INP_V)
	SW_NAME(INP_B)
	SW_NAME(INP_N)
	SW_NAME(INP_M)
	SW_NAME(INP_T)

	SW_NAME(INP_COMMA)
	SW_NAME(INP_EQUALS)

	SW_NAME2(INP_RBRACKET,"]")
	SW_NAME2(INP_TILDE,"`")
	SW_NAME2(INP_LBRACKET,"[")
	SW_NAME2(INP_MINUS,"-")
	SW_NAME2(INP_SEMICOLON,";")
	SW_NAME2(INP_APOSTROPHE,"'")
	SW_NAME2(INP_BACKSLASH,"\\")
	SW_NAME2(INP_SLASH,"/")
	SW_NAME2(INP_PERIOD,".")
};

#undef SW_NAME
#undef SW_NAME2

static S32 CompareKeyNames(const SwitchToken* pToken1, const SwitchToken* pToken2)
{
	// + 4 to skip "INP_"
	const char *pchName1 = pToken1->pchName2 ? pToken1->pchName2 : (pToken1->pchName + 4);
	const char *pchName2 = pToken2->pchName2 ? pToken2->pchName2 : (pToken2->pchName + 4);
	return stricmp(pchName1, pchName2);
}

static void SortKeyNames(void)
{
	static bool s_bSorted = false;
	if (!s_bSorted)
	{
		s_bSorted = true;
		qsort(s_aKeyNames, ARRAY_SIZE(s_aKeyNames), sizeof(s_aKeyNames[0]), CompareKeyNames);
	}
}

SA_RET_OP_STR const char * keybind_GetKeyName( S32 iKey, KeyboardLocale eLocale )
{
	// Keep a small ring buffer so we can call this twice in a row.
	static char s_apchBuffer[5][100];
	static int s_iBuffer;
	int iVKey = 0;
	S32 i;
	const ManagedThread *pInputThread = (gInput ? inpGetDeviceRenderThread(gInput) : NULL);
	HKL locale = 0;

	if (!eLocale)
	{
		if (!pInputThread)
		{
			pInputThread = tmGetMainThread();
		}

		locale = GetKeyboardLayout(tmGetThreadId(pInputThread));
	} 
	else
	{
		locale = (HKL)eLocale;
	}

	SortKeyNames();

	for (i = 0; i < ARRAY_SIZE_CHECKED(s_aKeyNames); i++)
	{
		if (s_aKeyNames[i].iKey == iKey)
			return s_aKeyNames[i].pchName2 ? s_aKeyNames[i].pchName2 : s_aKeyNames[i].pchName + 4;
	}

	iVKey = MapVirtualKeyEx(iKey, MAPVK_VSC_TO_VK_EX, locale);
	if (iVKey)
	{
		WCHAR key = (WCHAR)MapVirtualKeyEx(iVKey, MAPVK_VK_TO_CHAR, locale);

		if (key)
		{
			char *pchKey = WideToUTF8CharConvert(key);
			
			//German keyboards have a + key that is on the main portion of the keyboard.
			//Since we store key chords as "key+key", we return the word 'plus' instead
			//of the symbol to not confuse the parser.
			if (pchKey[0] == '+')
			{
				return "PLUS";
			}
			else
			{
				return pchKey;
			}
		}
	}

	s_iBuffer = (s_iBuffer + 1) % ARRAY_SIZE_CHECKED(s_apchBuffer);
	sprintf(s_apchBuffer[s_iBuffer], "0x%X", iKey);
	return s_apchBuffer[s_iBuffer];
}

const char *keybind_GetKeyNameByIndex(S32 iIndex)
{
	SortKeyNames();
	if (iIndex < ARRAY_SIZE_CHECKED(s_aKeyNames) && iIndex > 0)
	{
		return s_aKeyNames[iIndex].pchName2 ? s_aKeyNames[iIndex].pchName2 : s_aKeyNames[iIndex].pchName + 4;
	}
	return NULL;
}

S32 keybind_GetKeyCodeByIndex(S32 iIndex)
{
	SortKeyNames();
	if (iIndex < ARRAY_SIZE_CHECKED(s_aKeyNames) && iIndex > 0)
	{
		return s_aKeyNames[iIndex].iKey;
	}
	return -1;
}

S32 keybind_GetKeyArraySize(void)
{
	return ARRAY_SIZE_CHECKED(s_aKeyNames);
}

SA_RET_OP_STR const char * keybind_GetNames( SA_PARAM_OP_VALID const KeyBind *pBind, KeyboardLocale eLocale )
{
	static char achName[256];
	if (!pBind)
		return allocAddString("");
	else if (pBind->iKey2)
	{
		sprintf(achName, "%s+%s", keybind_GetKeyName(pBind->iKey1, eLocale), keybind_GetKeyName(pBind->iKey2, eLocale));
		return allocAddString(achName);
	}
	else
		return allocAddString(keybind_GetKeyName(pBind->iKey1, eLocale));
}

const char *keybind_GetKeyScancodeStrings(const KeyBind *pBind)
{
	static char achName[256];
	if (!pBind)
		return allocAddString("");
	else if (pBind->iKey2)
	{
		sprintf(achName, "0x%X+0x%X", pBind->iKey1, pBind->iKey2);
		return allocAddString(achName);
	}
	else
	{
		sprintf(achName, "0x%X", pBind->iKey1);
		return allocAddString(achName);
	}
}

typedef struct KeyNameChar
{
	const char *pchName;
	char chKey;
} KeyNameChar;

// We used to store these special symbols as names.
// This array maps the names to the actual chars.
// PLUS is a special case, we still sometimes use PLUS instead of +
//due to our key+key syntax.
static KeyNameChar s_specialKeys[] =
{
	{"comma", ','},
	{"equals", '='},
	{"lbracket", '['},
	{"rbracket", ']'},
	{"tilde", '`'},
	{"minus", '-'},
	{"apostrophe", '\''},
	{"semicolon", ';'},
	{"slash", '/'},
	{"backslash", '\\'},
	{"period", '.'},
	{"plus", '+'}
};

S32 keybind_GetKeyScancode( SA_PARAM_OP_STR const char *pchName, KeyboardLocale eLocale )
{
	S32 iScanCode = 0;
	const ManagedThread *pInputThread = (gInput ? inpGetDeviceRenderThread(gInput) : NULL);
	HKL locale = 0;
	S32 i;

	if (!eLocale)
	{
		if (!pInputThread)
		{
			pInputThread = tmGetMainThread();
		}
	
		locale = GetKeyboardLayout(tmGetThreadId(pInputThread));
	} 
	else
	{
		locale = (HKL)eLocale;
	}
	

	SortKeyNames();

	if (!pchName || !pchName[0])
		return iScanCode;

	//First check to see if the string is just a scancode
	if (strStartsWith(pchName, "0x"))
	{
		iScanCode = strtol(pchName + 2, NULL, 16);
	}
	else
	{
		char nameLower[100];
		for (i = 0; i < 100; ++i)
		{
			nameLower[i] = tolower(pchName[i]);
			if (nameLower[i] == 0)
				break;
		}

		// For legacy reasons we check the key name against a list
		//of punctuation keys that may be stored as their names
		if (!iScanCode)
		{
			for (i = 0; i < ARRAY_SIZE(s_specialKeys); ++i)
			{
				if (stricmp(nameLower, s_specialKeys[i].pchName) == 0)
				{
					S32 iVKey = VkKeyScanEx(s_specialKeys[i].chKey, locale);
					iScanCode = MapVirtualKeyEx(iVKey, MAPVK_VK_TO_VSC, locale);
				}
			}
		}

		//TODO FIXME
		// There is a problem when we have a named key, e.g. "semicolon" in a locale that doesn't have a key
		//that matches. It will get to this point and then it will usually take the first letter, e.g. "s",
		//and use that key. This is clearly wrong, but I am not sure how to check for a single character
		//since we are using UTF 8, and a single character might take up several bytes. ~DHOGBERG 2013/06/07
		if (pchName && !iScanCode)
		{
			// Now we check the name against the list of all of our special keys such as modifier keys
			//and joystick buttons etc.
			for (i = 0; i < ARRAY_SIZE(s_aKeyNames); i++)
			{
				if (!stricmp(pchName, s_aKeyNames[i].pchName + 4) || (s_aKeyNames[i].pchName2 && !stricmp(pchName, s_aKeyNames[i].pchName2)))
				{
					iScanCode = s_aKeyNames[i].iKey;
					break;
				}
			}

			// If we didn't find the name in those lists, then the key is probably a normal letter or number key
			if (!iScanCode)
			{
				S32 iVKey = 0;

				iVKey = VkKeyScanEx(UTF8ToWideCharConvert(nameLower), locale);
				iScanCode = MapVirtualKeyEx(iVKey, MAPVK_VK_TO_VSC, locale);
			}

			// If we STILL don't have a scancode, check to see if USEnglish is not installed.
			//If it isn't then we should use the fallback table
			if (!iScanCode &&
				(locale == (HKL)KeyboardLocale_EnglishUS && !keybind_CheckIfKeyboardLocaleIsInstalled(KeyboardLocale_EnglishUS)) ||
				getIsTransgaming()) {

				for (i = 0; i < ARRAY_SIZE(s_aNormalKeyNames); i++)
				{
					if (!stricmp(pchName, s_aNormalKeyNames[i].pchName + 4) || (s_aNormalKeyNames[i].pchName2 && !stricmp(pchName, s_aNormalKeyNames[i].pchName2)))
					{
						iScanCode = s_aNormalKeyNames[i].iKey;
						break;
					}
				}
			}

		}
	}

	// if we can't find the key name, maybe someone passed in a different format scancode instead.
	if (!iScanCode)
	{
		if (strStartsWith(pchName, "#"))
			iScanCode = strtol(pchName + 1, NULL, 16);
	}

	return iScanCode;
}

void keybind_ParseKeyString( SA_PARAM_NN_STR const char *pchKeys, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *piKey1, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *piKey2, KeyboardLocale eLocale )
{
	S32 iKey1, iKey2;
	char achCopy[1024];
	char *pchContext = NULL;
	const char *pchKey1 = NULL;
	const char *pchKey2 = NULL;
	strcpy(achCopy, pchKeys);

	// Extract key names separated by "+".
	pchKey2 = strtok_s(achCopy, "+", &pchContext);
	pchKey1 = strtok_s(NULL, "+", &pchContext);

	// Convert key names to internal scan codes.
	iKey1 = keybind_GetKeyScancode(pchKey1, eLocale);
	iKey2 = keybind_GetKeyScancode(pchKey2, eLocale);

	*piKey1 = max(iKey1, iKey2);
	*piKey2 = min(iKey1, iKey2);
}

KeyBind *keybind_BindKeyInProfileEx(KeyBindProfile *pProfile, const char *pchKey, const char *pchCommand, bool bSecondary)
{
	S32 iKey1;
	S32 iKey2;

	keybind_ParseKeyString(pchKey, &iKey1, &iKey2, KeyboardLocale_Current);

	if (iKey1 > 0)
	{
		KeyBind *pBind;
		S32 iBind = keybind_FindBindInProfileEx(pProfile, iKey1, iKey2, true);
		if (iBind >= 0)
		{
			pBind = pProfile->eaBinds[iBind];
			StructFreeString(pBind->pchCommand);
			pBind->iKey1 = iKey1;
			pBind->iKey2 = iKey2;
			pBind->pchKey = allocAddString(pchKey);
			pBind->pchCommand = StructAllocString(pchCommand);
			pBind->bSecondary = bSecondary;

			if (!pBind->pchCommand)
			{
				KeyBindFree(pBind);
				eaRemoveFast(&pProfile->eaBinds, iBind);
				pBind = NULL;
			}
			return pBind;
		}

		pBind = StructCreate(parse_KeyBind);
		pBind->iKey1 = iKey1;
		pBind->iKey2 = iKey2;
		pBind->pchKey = allocAddString(pchKey);
		pBind->pchCommand = StructAllocString(pchCommand);
		pBind->bSecondary = bSecondary;
		eaPush(&pProfile->eaBinds, pBind);
		return pBind;
	}
	return NULL;
}

KeyBind *keybind_BindKeyInProfile(KeyBindProfile *pProfile, const char *pchKey, const char *pchCommand)
{
	return keybind_BindKeyInProfileEx(pProfile, pchKey, pchCommand, false);
}

void keybind_CopyBindsFromName(const char *pchSourceProfile, KeyBindProfile *pDestProfile)
{
	KeyBindProfile *pSourceProfile = keybind_FindProfile(pchSourceProfile);
	S32 i;

	eaDestroyEx(&pDestProfile->eaBinds, KeyBindFree);
	if (pSourceProfile)
		for (i = 0; i < eaSize(&pSourceProfile->eaBinds); i++)
			keybind_BindKeyInProfile(pDestProfile, pSourceProfile->eaBinds[i]->pchKey, pSourceProfile->eaBinds[i]->pchCommand);
}


S32 keybind_FindInProfiles(KeyBindStoredProfiles *pProfiles, const char *pchProfile)
{
	S32 i;
	for (i = 0; i < eaSize(&pProfiles->eaProfiles); i++)
		if (pProfiles->eaProfiles[i]->pchName && !stricmp(pProfiles->eaProfiles[i]->pchName, pchProfile))
			return i;
	return -1;
}

KeyBindProfile *keybind_FindProfile(const char *pchProfile)
{
	return eaGet(&s_ParsedData.eaProfiles, keybind_FindInProfiles(&s_ParsedData,pchProfile));
}

S32 keybind_FindInProfilesForLocale(KeyBindStoredProfiles *pProfiles, const char *pchProfilePrefix, S32 locale)
{
	S32 i;
	for (i = 0; i < eaSize(&pProfiles->eaProfiles); i++)
	{
		if (pProfiles->eaProfiles[i]->pchName &&
			strStartsWith(pProfiles->eaProfiles[i]->pchName, pchProfilePrefix) &&
			(S32)pProfiles->eaProfiles[i]->eKeyboardLocale == locale)
		{
			return i;
		}
	}
	return -1;
}

KeyBindProfile *keybind_FindProfileForKeyboardLocale(const char *pchStartString, S32 locale)
{
	return eaGet(&s_ParsedData.eaProfiles, keybind_FindInProfilesForLocale(&s_ParsedData, pchStartString, locale));
}

// Run the command for this key bind.
void keybind_ExecuteCommand(KeyBind *pBind, bool bState)
{
	const char *pchCommand = pBind->pchCommand;

	if (pchCommand && pchCommand[0] == '+' && pchCommand[1] != '+')
	{
		pBind->bPressed = !!bState;

		if (bState)
		{
			globCmdParse(pchCommand);
		}
		else
		{
			char *buf = NULL;
			char *cmd, *line;
			estrStackCreate(&buf);
			estrCopy2(&buf, pchCommand);

			cmd = buf;
			while (cmd)
			{
				line = cmdReadNextLine(&cmd);
				if (line[0] == '+')
					line[0] = '-';
				globCmdParse(line);
			}
			estrDestroy(&buf);
		}
	}
	else if (bState)
	{
		globCmdParse(pchCommand);
	}
}

// Run the command for this key bind and do other key-related actions.
bool keybind_ExecuteKeyBind(KeyBind *pBind, bool bState, U32 uiTime)
{
	const char *pchCommand = pBind->pchCommand;

	if (pchCommand && pchCommand[0])
	{
		if (s_cbKeyBindSetTimeStamp)
			s_cbKeyBindSetTimeStamp(uiTime);
		
		keybind_ExecuteCommand(pBind, bState);

		// By default, binds prevent other binds in lower profiles from running.
		// But this can be turned off at the per-bind level. This is used to
		// create no-op binds for particular reverse lookups, that do not affect
		// actual key press behaviors.
		if (pBind->bContinueProcessing)
			return true;
	}

	return false;
}

static S32 BindNormalizeKey(S32 iKey)
{
	// Normalize modifier keys. Otherwise, a bind of shift+a will not work if a is
	// held down first (since keyScanCode = INP_LSHIFT).
	switch (iKey)
	{
	case INP_LSHIFT:
	case INP_RSHIFT:
		return INP_SHIFT;
	case INP_LALT:
	case INP_RALT:
		return INP_ALT;
	case INP_LCONTROL:
	case INP_RCONTROL:
		return INP_CONTROL;
	default:
		return iKey;
	}
}

KeyBind *keybind_BindIsActiveBest(KeyBind *pBind, S32 iKey, KeyBind *pBest)
{
	S32 iNormalizedKey = BindNormalizeKey(iKey);

	// inpLevelPeek does the shift normalization for us, so we don't
	// need to do anything special for the second key in a bind.
	if ((!pBest || !pBest->iKey2) && (pBind->iKey1 == iKey || pBind->iKey1 == iNormalizedKey) && pBind->iKey2 && inpLevelPeek(pBind->iKey2))
		return pBind;
	// If both keys are pressed on the same frame, we don't want to do the event twice, so
	// only figure if inpEdge is *not* true for the other key.
	else if ((!pBest || !pBest->iKey2) && pBind->iKey2 == iKey && inpLevelPeek(pBind->iKey1) && !inpEdgePeek(pBind->iKey1))
		return pBind;
	else if ((pBind->iKey1 == iKey || pBind->iKey1 == iNormalizedKey) && !pBind->iKey2 && !pBest)
		return pBind;
	else
		return pBest;
}

S32 keybind_GetProfileCount(void)
{
	S32 i;
	S32 iCount = 0;
	for (i = InputBindPriorityCount - 1; i >= 0; --i)
		iCount += eaSize(&s_ActiveProfiles[i].eaProfiles);
	return iCount;
}

void keybind_NewProfileIterator(KeyBindProfileIterator *pIter)
{
	*pIter = 0;
}

KeyBindProfile *keybind_ProfileIteratorNext(KeyBindProfileIterator *pIter)
{
	S32 i;
	S32 j;
	S32 iCurrent = 0;
	for (i = InputBindPriorityCount - 1 ; i >= 0; --i)
	{
		for (j = eaSize(&s_ActiveProfiles[i].eaProfiles) - 1; j >= 0; --j)
		{
			if (iCurrent == *pIter)
			{
				KeyBindProfile *pProfile = s_ActiveProfiles[i].eaProfiles[j];
				++(*pIter);
				return pProfile;
			}
			++iCurrent;
		}
	}
	return NULL;
}

#define VALIDATE_PRIORITY(ePriority) devassertmsg(ePriority >= 0 && ePriority < InputBindPriorityCount, "Priority is outside of valid keybind range");


KeyBindProfile *keybind_GetCurrentProfile(KeyBindPriority ePriority)
{
	S32 iCount;
	VALIDATE_PRIORITY(ePriority);
	iCount = eaSize(&s_ActiveProfiles[ePriority].eaProfiles);
	if (iCount > 0)
        return s_ActiveProfiles[ePriority].eaProfiles[iCount-1];
	return NULL;
}

bool keybind_IsProfileActive(KeyBindProfile *pProfile)
{
	VALIDATE_PRIORITY(pProfile->ePriority);
	return eaFind(&s_ActiveProfiles[pProfile->ePriority].eaProfiles, pProfile) != -1;
}

void keybind_PushProfile(KeyBindProfile *pProfile)
{
	keybind_PushProfileEx(pProfile, pProfile->ePriority);
}

void keybind_PushProfileName(const char *pchProfile)
{
	KeyBindProfile *pProfile = keybind_FindProfile(pchProfile);
	if (pProfile)
		keybind_PushProfileEx(pProfile, pProfile->ePriority);
}

void keybind_PushProfileEx(KeyBindProfile *pProfile, KeyBindPriority ePriority)
{
	VALIDATE_PRIORITY(ePriority);
	eaPushUnique(&s_ActiveProfiles[ePriority].eaProfiles, pProfile);
}

void keybind_PopProfile(SA_PARAM_NN_VALID KeyBindProfile *pProfile)
{
	keybind_PopProfileEx(pProfile, pProfile->ePriority);
}

void keybind_PopProfileName(const char *pchProfile)
{
	KeyBindProfile *pProfile = keybind_FindProfile(pchProfile);
	if (pProfile)
		keybind_PopProfileEx(pProfile, pProfile->ePriority);
}

void keybind_UnpressAllPressesInProfile(KeyBindProfile *pProfile)
{
	int i;
	
	for (i = 0; i < eaSize(&pProfile->eaBinds); i++)
	{
		KeyBind *pBind = pProfile->eaBinds[i];
		if (pBind && pBind->bPressed)
		{
			// Use a millisecond time stamp. TickCount seems to be
			//  similar units to what the device timestamps are
			//  returning.
			keybind_ExecuteKeyBind(pBind, false, inpGetTime());
		}
	}
}


void keybind_PopProfileEx(KeyBindProfile *pProfile, KeyBindPriority ePriority)
{
	VALIDATE_PRIORITY(ePriority);

	// We need to go through the profile and 'unpress' any pressed keys
	//  if they might have functionality associated. This was specifically
	//  a problem where we had a profile overlaying another where both
	//  had the same keybind defined. The press was intercepted by the
	//  bind in the overlay, then the overlay was popped, and the release
	//  never fired because the corresponding press had disappeared.
	// I have a slight worry this will cause problems due to execute commands
	//  being run at a wrong time in the main loop (it's unknown when the
	//  pop is happening, vs. the normal ExecuteKeyBinds being run at a
	//  particular time in the tick). WOLF[7Sep2012]
	// NOTE! There is still a 'problem' if we try to do something with a press
	//  action that is in both an overlay keybind and and the regular keybind.
	//  Since we don't transfer the press, the action will essentially be
	//  interrupted by the popping of the profile.
	if (pProfile!=NULL)
	{
		keybind_UnpressAllPressesInProfile(pProfile);
	}
	
	eaFindAndRemove(&s_ActiveProfiles[ePriority].eaProfiles, pProfile);

	// Always keep at least the game binding profile on the stack so
	// some key bindings and commands will be available.
	if (!eaSize(&s_ActiveProfiles[InputBindPriorityGame].eaProfiles))
	{
		Errorf("Error! KeyBinding game profile stack emptied.");
		keybind_PushProfileEx(&s_GameProfile, InputBindPriorityGame);
	}

	if (!eaSize(&s_ActiveProfiles[InputBindPriorityUser].eaProfiles))
	{
		Errorf("Error! KeyBinding user profile stack emptied.");
		keybind_PushProfileEx(&s_GameProfile, InputBindPriorityUser);
	}
}

bool keybind_ExecuteKey(S32 iKey, bool bState, U32 uiTime)
{
	bool bFound = false;
	KeyBindProfile *pProfile = NULL;
	KeyBindProfileIterator iter;
	KeyBind *pBestBind = NULL;
	S32 i;

	if (iKey == INP_CLOSEWINDOW && s_cbKeyBindQuit)
		s_cbKeyBindQuit();

	keybind_NewProfileIterator(&iter);
	if (bState)
	{
		while ( (pProfile = keybind_ProfileIteratorNext(&iter)) )
		{
			// Search the profile's keybinds for ones matching the scan code. If there's
			// one chorded and one unchorded bind, prefer the chorded one.
			for (i = 0; i < eaSize(&pProfile->eaBinds); i++)
			{
				pBestBind = keybind_BindIsActiveBest(pProfile->eaBinds[i], iKey, pBestBind);
			}

			if (!pProfile->bTrickleKeys && bState)
				break;
		}

		if (pBestBind)
		{
			bFound = keybind_ExecuteKeyBind(pBestBind, bState, uiTime);
		}
		else
		{
			// Go through the profiles and execute all "other key" callbacks
			keybind_NewProfileIterator(&iter);
			while ( (pProfile = keybind_ProfileIteratorNext(&iter)) )
			{
				if (pProfile->cbAllOtherKeys)
					pProfile->cbAllOtherKeys(iKey);

				if (!pProfile->bTrickleKeys)
					break;
				else
					bFound = true;
			}
		}
	}
	else
	{
		// If this is a key release, we need to stop all pressed binds.
		S32 iNormalizedKey = BindNormalizeKey(iKey);
		while ( (pProfile = keybind_ProfileIteratorNext(&iter)) )
		{
			for (i = 0; i < eaSize(&pProfile->eaBinds); i++)
			{
				KeyBind *pBind = pProfile->eaBinds[i];
				if (pBind && pBind->bPressed
					&& (pBind->iKey1 == iKey || pBind->iKey2 == iKey
						|| pBind->iKey1 == iNormalizedKey || pBind->iKey2 == iNormalizedKey))
				{
					keybind_ExecuteKeyBind(pBind, false, uiTime);
					bFound = true;
				}
			}
		}
	}
	if (bFound)
		inpCapture(iKey);

	return 1;
}

// Print out all active keybinds
AUTO_COMMAND ACMD_NAME(bind_print) ACMD_CLIENTONLY;
void keybind_PrintBinds(void)
{
	KeyBindPrintf cbPrintf = g_inpPrintf ? g_inpPrintf : printf;
	KeyBindProfile *pProfile;
	KeyBindProfileIterator iter;
	S32 i;

	cbPrintf("Key Bind Profiles:\n");

 	keybind_NewProfileIterator(&iter);
 	while ((pProfile = keybind_ProfileIteratorNext(&iter)))
 	{
		cbPrintf("%4d: %s (Trickle Keys: %s, Trickle Commands: %s)\n", iter, pProfile->pchName ? pProfile->pchName : "Unnamed",
			pProfile->bTrickleKeys ? "true" : "false", pProfile->bTrickleCommands ? "true" : "false");
		for (i = 0; i < eaSize(&pProfile->eaBinds); i++)
		{
			KeyBind *bind = pProfile->eaBinds[i];
			cbPrintf("\t%s: %s\n", bind->pchKey, bind->pchCommand);
		}
	}
}

void keybind_SaveProfile(const KeyBindProfile *pProfile, const char *pchFilename)
{
 	FILE *pFile;
 	S32 i;
 
	PERFINFO_AUTO_START_FUNC();

 	makeDirectoriesForFile(pchFilename);
 	pFile = fileOpen(pchFilename, "wt");
 
 	if (!pFile)
	{
		PERFINFO_AUTO_STOP();
 		return;
	}

	for (i = 0; i < eaSize(&pProfile->eaBinds); i++)
	{
		KeyBind *pBind = pProfile->eaBinds[i];
		fprintf(pFile, "%s \"%s\"\n", pBind->pchKey, pBind->pchCommand);
	}

	fclose(pFile);

	PERFINFO_AUTO_STOP();
}

void keybind_SaveUserBinds(const char *pchFilename)
{
	char achFullFilename[CRYPTIC_MAX_PATH];

	if (!pchFilename)
		pchFilename = KEYBIND_DEFAULT_FILENAME;

	strcpy(achFullFilename, pchFilename);
	if (!isFullPath(achFullFilename))
	{
		char dir[CRYPTIC_MAX_PATH];
		if (isDevelopmentMode())
			sprintf(achFullFilename, "%s/%s", fileLocalDataDir(), pchFilename);
		else
			sprintf(achFullFilename, "%s/%s", getExecutableDir(dir), pchFilename);
	}

	keybind_SaveProfile(&s_UserProfile, achFullFilename);
	strcat(achFullFilename, ".low");
	keybind_SaveProfile(&s_UserLowPriProfile, achFullFilename);
}

static char *StripQuotes(char *pch)
{
	if (pch[0] == '"')
	{
		pch++;
		if (pch[strlen(pch)-1] == '"')
			pch[strlen(pch)-1] = 0;
	}
	return pch;
}

void keybind_LoadProfile(KeyBindProfile *pProfile, const char *pchFilename)
{
	FILE *pFile;
	char achBuf[1000];

	PERFINFO_AUTO_START_FUNC();

	pFile = fileOpen(pchFilename, "rt");
	if (!pFile)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	while (fgets(achBuf, sizeof(achBuf), pFile))
	{
		char *pchStart = NULL;
		char *pchIter;
		for (pchIter = achBuf; *pchIter; pchIter++)
		{
			if (*pchIter == '\t')
				*pchIter = ' ';
			if (!pchStart && !(*pchIter == ' '))
				pchStart = pchIter;
		}
		if (!pchStart || !*pchStart ||
			strStartsWith(pchStart, "//") ||
			strStartsWith(pchStart, "#") ||
			strStartsWith(pchStart, ";"))
			continue;

		for (pchIter = pchStart + strlen(pchStart) - 1; pchIter >= pchStart; pchIter--)
		{
			if (*pchIter==' ' || *pchIter == '\r' || *pchIter == '\n')
				*pchIter = '\0';
			else
				break;
		}
		if (!*pchStart)
			continue;

		pchIter = strchr(pchStart, ' ');
		if (pchIter)
		{
			*pchIter = '\0';
			pchIter = StripQuotes(pchIter + 1);
			keybind_BindKeyInProfile(pProfile, pchStart, pchIter);
		}
		else
		{
			keybind_BindKeyInProfile(pProfile, pchStart, NULL);
		}
	}

	fclose(pFile);

	PERFINFO_AUTO_STOP();
}

void keybind_LoadUserBinds(const char *pchFilename)
{
	char achFullFilename[CRYPTIC_MAX_PATH];
	char achFolder[CRYPTIC_MAX_PATH];

	if (!pchFilename)
		pchFilename = KEYBIND_DEFAULT_FILENAME;

	strcpy(achFullFilename, pchFilename);
	if (!isFullPath(achFullFilename))
	{
		if(isDevelopmentMode())
			sprintf(achFullFilename, "%s/%s", fileLocalDataDir(), pchFilename);
		else
			sprintf(achFullFilename, "%s/%s", getExecutableDir(achFolder), pchFilename);
	}

	keybind_LoadProfile(&s_UserProfile, achFullFilename);
	strcat(achFullFilename, ".low");
	keybind_LoadProfile(&s_UserLowPriProfile, achFullFilename);
}

void keybind_SetQuit(KeyBindQuit kbQuit)
{
	s_cbKeyBindQuit = kbQuit;
}

KeyBindQuit keybind_GetQuit(void)
{
	return s_cbKeyBindQuit;
}

static void KeyBindReload(const char *pchFilename, S32 uiTime)
{
	KeyBindStoredProfiles profiles = {0};
	S32 i;
	fileWaitForExclusiveAccess(pchFilename);
	errorLogFileIsBeingReloaded(pchFilename);
	ParserReadTextFile(pchFilename, parse_KeyBindStoredProfiles, &profiles, 0);
	for (i = 0; i < eaSize(&profiles.eaProfiles); i++)
	{
		KeyBindProfile *pProfile = profiles.eaProfiles[i];
		if (pProfile && pProfile->pchName)
		{
			KeyBindProfile *pOldProfile = keybind_FindProfile(pProfile->pchName);
			loadstart_printf("Reloading keybind profile %s... ", pProfile->pchName);
			if (pOldProfile == pProfile)
			{
				loadend_printf("same profile?");
			}
			else if (pOldProfile)
			{
				if (pOldProfile->ePriority != pProfile->ePriority)
					Alertf("WARNING: Priority of keybind profile %s doesn't match the old value. If this keybind is on the stack, it won't be moved.", pProfile->pchName);
				StructCopyAll(parse_KeyBindProfile, pProfile, pOldProfile);
				loadend_printf("updated.");
			}
			else
			{
				eaPush(&s_ParsedData.eaProfiles, pProfile);
				loadend_printf("added.");
			}
		}
	}
	StructDeInit(parse_KeyBindStoredProfiles, &profiles);
}

void keybind_Init(KeyBindPrintf cbPrintf, KeyBindSetTimeStamp cbSetTimeStamp, KeyBindQuit cbQuit, const char *pchProfile)
{
	g_inpPrintf = cbPrintf;
	s_cbKeyBindSetTimeStamp = cbSetTimeStamp;
	s_cbKeyBindQuit = cbQuit;

	ParseBindings();

	keybind_PushProfileEx(&s_GameProfile, InputBindPriorityGame);
	s_GameProfile.pchName = "Game Binds";
	s_GameProfile.pCmdList = &gGlobalCmdList;

	s_UserLowPriProfile.pchName = "User Low Priority Binds";
	s_UserLowPriProfile.bTrickleCommands = true;
	s_UserLowPriProfile.bTrickleKeys = true;
	s_UserLowPriProfile.ePriority = InputBindPriorityDevelopment;
	keybind_PushProfile(&s_UserLowPriProfile);

	s_UserProfile.pchName = "User Binds";
	s_UserProfile.bTrickleCommands = true;
	s_UserProfile.bTrickleKeys = true;
	s_UserProfile.ePriority = InputBindPriorityUser;
	keybind_PushProfile(&s_UserProfile);

	if (pchProfile)
		keybind_CopyBindsFromName(pchProfile, &s_GameProfile);

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "keybinds/*.kb", KeyBindReload);
}

bool keybind_BindsBelow(KeyBindProfile *pProfile, KeyBindProfile ***peaProfiles)
{
	bool bFound = false;
	KeyBindProfileIterator iter;
	KeyBindProfile *pCurrent;
	if (!pProfile)
		return false;
	keybind_NewProfileIterator(&iter);
	while ((pCurrent = keybind_ProfileIteratorNext(&iter)))
	{
		if (bFound || pCurrent->ePriority < pProfile->ePriority)
			eaPush(peaProfiles, pCurrent);
		else if (pProfile == pCurrent)
			bFound = true;
	}

	return bFound;
}

bool keybind_BindIsJoystick(const KeyBind *pBind)
{
	return key_IsJoystick(pBind->iKey1) || key_IsJoystick(pBind->iKey2);
}

bool key_IsJoystick(S32 iKey)
{
	switch (iKey)
	{
	case INP_AB:
	case INP_BB:
	case INP_XB:
	case INP_YB:
	case INP_LTRIGGER:
	case INP_RTRIGGER:
	case INP_LB:
	case INP_RB:
	case INP_START:
	case INP_SELECT:
	case INP_JOYPAD_UP:
	case INP_JOYPAD_DOWN:
	case INP_JOYPAD_LEFT:
	case INP_JOYPAD_RIGHT:
	case INP_LSTICK:
	case INP_LSTICK_LEFT:
	case INP_LSTICK_RIGHT:
	case INP_LSTICK_UP:
	case INP_LSTICK_DOWN:
	case INP_RSTICK:
	case INP_RSTICK_LEFT:
	case INP_RSTICK_RIGHT:
	case INP_RSTICK_UP:
	case INP_RSTICK_DOWN:
		return true;
	default:
		return false;
	}
}

S32 keybind_FindBindInProfileEx(KeyBindProfile *pProfile, S32 iKey1, S32 iKey2, bool bIncludeNormalized)
{
	S32 iBind;
	S32 iNormalizedKey1 = bIncludeNormalized ? BindNormalizeKey(iKey1) : iKey1;

	for (iBind = 0; iBind < eaSize(&pProfile->eaBinds); iBind++)
	{
		KeyBind* pBind = pProfile->eaBinds[iBind];
		if ((iKey1 == pBind->iKey1 || iNormalizedKey1 == pBind->iKey1) && iKey2 == pBind->iKey2)
		{
			return iBind;
		}
	}
	return -1;
}

static bool keybind_IsBindInProfileAbove(KeyBindProfile *pProfile, KeyBind *pBind)
{
	S32 i, j;
	for (i = pProfile->ePriority + 1; i < InputBindPriorityCount; i++)
	{
		for (j = eaSize(&s_ActiveProfiles[i].eaProfiles) - 1; j >= 0; j--)
		{
			if (keybind_FindBindInProfile(s_ActiveProfiles[i].eaProfiles[j], pBind->iKey1, pBind->iKey2) >= 0)
			{
				return true;
			}
		}
	}
	return false;
}

KeyBind *keybind_BindForCommandInProfileEx(KeyBindProfile *pProfile, const char *pchCommand, bool bJoystick, bool bCheckAbove)
{
	S32 iBind;
	for (iBind = 0; iBind < eaSize(&pProfile->eaBinds); iBind++)
	{
		KeyBind *pBind = pProfile->eaBinds[iBind];
		if (pBind->pchCommand && cmdIsCommandIn(pBind->pchCommand, pchCommand))
		{
			if (!bCheckAbove || !keybind_IsBindInProfileAbove(pProfile, pBind))
			{
				bool bIsJoystick = keybind_BindIsJoystick(pBind);
				if ((bJoystick && bIsJoystick) || !(bJoystick || bIsJoystick))
					return pBind;
			}
		}
	}
	return NULL;
}

KeyBind *keybind_BindForCommandInProfile(KeyBindProfile *pProfile, const char *pchCommand, bool bJoystick)
{
	return keybind_BindForCommandInProfileEx(pProfile, pchCommand, bJoystick, false);
}

KeyBind *keybind_ProfileFindBindByCommand(KeyBindProfile *pProfile, const char *pchCommand)
{
	S32 iBind;
	for (iBind = 0; iBind < eaSize(&pProfile->eaBinds); iBind++)
	{
		KeyBind *pBind = pProfile->eaBinds[iBind];
		if (pBind->pchCommand == pchCommand || (pchCommand && !stricmp(pBind->pchCommand, pchCommand)))
			return pBind;
	}
	return NULL;
}


KeyBind *keybind_ProfileFindBindByKeyString(KeyBindProfile *pProfile, const char *pchKey)
{
	S32 iBind;
	if (!pchKey)
		return NULL;
	for (iBind = 0; iBind < eaSize(&pProfile->eaBinds); iBind++)
	{
		KeyBind *pBind = pProfile->eaBinds[iBind];
		if (pBind->pchKey == pchKey || (pchKey && !stricmp(pBind->pchKey, pchKey)))
			return pBind;
	}
	return NULL;
}

KeyBind *keybind_GetBindByKeys(S32 iKey1, S32 iKey2)
{
	KeyBindProfile *pProfile;
	KeyBindProfileIterator iter;
	
	keybind_NewProfileIterator(&iter);

	while ((pProfile = keybind_ProfileIteratorNext(&iter)))
	{
		S32 iBindIdx = keybind_FindBindInProfile(pProfile, iKey1, iKey2);
		if (iBindIdx >= 0)
		{
			return pProfile->eaBinds[iBindIdx];
		}
	}
	return NULL;
}

static S32 keybind_GetBindCount(void)
{
	S32 iCount = 0;
	KeyBindProfile *pProfile;
	KeyBindProfileIterator iter;
	
	keybind_NewProfileIterator(&iter);

	while ((pProfile = keybind_ProfileIteratorNext(&iter)))
	{
		iCount += eaSize(&pProfile->eaBinds);
	}
	return iCount;
}

void keybind_GetBinds(KeyBind ***peaBinds, bool bAlloc)
{
	ESet Set = NULL;
	KeyBindProfile *pProfile;
	KeyBindProfileIterator iter;
	
	keybind_NewProfileIterator(&iter);
	eSetCreate(&Set, keybind_GetBindCount());

	while ((pProfile = keybind_ProfileIteratorNext(&iter)))
	{
		S32 iBind;
		for (iBind = 0; iBind < eaSize(&pProfile->eaBinds); iBind++)
		{
			KeyBind *pBind = pProfile->eaBinds[iBind];
			if (!eSetFind(&Set, pBind->pchKey))
			{
				KeyBind *pPushBind = pBind;
				if (bAlloc)
					pPushBind = StructClone(parse_KeyBind, pBind);
				eaPush(peaBinds, pPushBind);
				eSetAdd(&Set, pBind->pchKey);
			}
		}
	}
	eSetDestroy(&Set);
}

void keybind_BindsForCommandInProfileEx(KeyBindProfile *pProfile, const char *pchCommand, bool bJoystick, bool bCheckAbove,
										KeyBind ***peaBinds)
{
	S32 iBind;
	for (iBind = 0; iBind < eaSize(&pProfile->eaBinds); iBind++)
	{
		KeyBind *pBind = pProfile->eaBinds[iBind];
		if (pBind->pchCommand && cmdIsCommandIn(pBind->pchCommand, pchCommand))
		{
			if (!bCheckAbove || !keybind_IsBindInProfileAbove(pProfile, pBind))
			{
				bool bIsJoystick = keybind_BindIsJoystick(pBind);
				if ((bJoystick && bIsJoystick) || !(bJoystick || bIsJoystick))
					eaPush(peaBinds, pBind);
			}
		}
	}
}

void keybind_BindsForCommandInProfile(KeyBindProfile *pProfile, const char *pchCommand, bool bJoystick, KeyBind ***peaBinds)
{
	keybind_BindsForCommandInProfileEx(pProfile, pchCommand, bJoystick, false, peaBinds);
}

KeyBind *keybind_BindForCommand(const char *pchCommand, bool bJoystick, bool bCheckAbove)
{
	KeyBindProfileIterator iter;
	KeyBindProfile *pProfile = NULL;
	PERFINFO_AUTO_START_FUNC();
	keybind_NewProfileIterator(&iter);
	while ( (pProfile = keybind_ProfileIteratorNext(&iter)) )
	{
		KeyBind *pBind = keybind_BindForCommandInProfileEx(pProfile, pchCommand, bJoystick, bCheckAbove);
		if (pBind)
		{
			PERFINFO_AUTO_STOP();
			return pBind;
		}
	}
	PERFINFO_AUTO_STOP();
	return NULL;
}

void keybind_BindsForCommand(const char *pchCommand, bool bJoystick, bool bCheckAbove, KeyBind ***peaBinds)
{
	KeyBindProfileIterator iter;
	KeyBindProfile *pProfile = NULL;
	PERFINFO_AUTO_START_FUNC();
	keybind_NewProfileIterator(&iter);
	while ( (pProfile = keybind_ProfileIteratorNext(&iter)) )
		keybind_BindsForCommandInProfileEx(pProfile, pchCommand, bJoystick, bCheckAbove, peaBinds);
	PERFINFO_AUTO_STOP();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("keybind_XboxControllerNameFromKey");
const char *keybind_XboxControllerNameFromKey(int iKey)
{
#define SW_NAME(x) {x, #x},
	static SwitchToken s_aXBoxMap[] =
	{
		SW_NAME(INP_AB)
		SW_NAME(INP_BB)
		SW_NAME(INP_XB)
		SW_NAME(INP_YB)
		SW_NAME(INP_RB)
		SW_NAME(INP_LB)
		SW_NAME(INP_RSTICK)
		SW_NAME(INP_LSTICK)
		SW_NAME(INP_START)
		SW_NAME(INP_SELECT)

		SW_NAME(INP_LTRIGGER)
		SW_NAME(INP_RTRIGGER)
	};
#undef SW_NAME
	int i;

	if(iKey)
	{
		for (i = 0; i < ARRAY_SIZE_CHECKED(s_aXBoxMap); i++)
		{
			if (s_aXBoxMap[i].iKey == iKey)
				return (s_aXBoxMap[i].pchName2 ? s_aXBoxMap[i].pchName2 : s_aXBoxMap[i].pchName + 4);
		}
	}

	return "";
}

//////////////////////////////////////////////////////////////////////////


int keybind_CmdParse(const char *pchCommandOrig, char **ppRetVal, CmdContextFlag iCmdContextFlags, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	int parseResult;
	KeyBindProfile *curProfile;	
	char *lineBuffer, *pchCommand = NULL, *pchCommandStart = NULL;
	int iRet = 0;
	char *pInternalRetVal = NULL;

	if(eHow == CMD_CONTEXT_HOWCALLED_UNSPECIFIED)
	{
		eHow = CMD_CONTEXT_HOWCALLED_KEYBIND;
	}

	estrStackCreate(&pchCommandStart);
	estrStackCreate(&pInternalRetVal);
	estrCopy2(&pchCommandStart,pchCommandOrig);
	pchCommand = pchCommandStart;

	while(pchCommand)
	{
		int iCntQuote=1;
		bool bHandled = false;
		CmdContext cmd_context = {0};

		cmd_context.eHowCalled = eHow;

		cmd_context.flags |= iCmdContextFlags;
		cmd_context.pStructList = pStructs;

		
		if (ppRetVal)
		{
			cmd_context.output_msg = ppRetVal;
		}
		else
		{
			cmd_context.output_msg = &pInternalRetVal;
		}

		lineBuffer = cmdReadNextLine(&pchCommand);

		if (!keybind_GetProfileCount()) // At startup
		{
			cmd_context.access_level = iOverrideAccessLevel != -1 ? iOverrideAccessLevel : 9;			
			parseResult = cmdParseAndExecute(&gGlobalCmdList,lineBuffer,&cmd_context);
			iRet = iRet || parseResult;
		}
		else
		{		
			KeyBindProfileIterator iter;
			keybind_NewProfileIterator(&iter);
			while ( (curProfile = keybind_ProfileIteratorNext(&iter)) )
			{
				cmdContextReset(&cmd_context);
				if (!curProfile->pCmdList)
				{
					// This must be a keybind-only profile. Let it go through.
					continue;
				}
				cmd_context.access_level = iOverrideAccessLevel != -1 ? iOverrideAccessLevel : 9;			
				parseResult = cmdParseAndExecute(curProfile->pCmdList,lineBuffer,&cmd_context);
				iRet = iRet || parseResult;
				// If the command has been handled, break...
				if (parseResult) {
					// Command OK!
					cmdPrintPrettyOutput(&cmd_context, g_inpPrintf);
					bHandled = true;
					break;
				}
				// Else command was not handled
				if (cmd_context.found_cmd) {
					// Found the command, but a syntax error must have occurred!
					cmdPrintPrettyOutput(&cmd_context, g_inpPrintf);
					bHandled = true;
					break;
				}

				// If the command cannot be trickle down, break...
				// However, if it's a toggle command that's turning off, we still
				// want it to turn off.
				if (!curProfile->bTrickleCommands && SAFE_DEREF(lineBuffer) != '-')
					break;
			}

			if (!bHandled) {
				cmdPrintPrettyOutput(&cmd_context, g_inpPrintf);
			}
		}

	}

	estrDestroy(&pInternalRetVal);
	estrDestroy(&pchCommandStart);

	return iRet; 
}

bool keybind_CheckIfKeyboardLocaleIsInstalled(KeyboardLocale eLocale)
{
	if (eLocale != KeyboardLocale_Current)
	{
		HKL localesInstalled[100];
		S32 iNumLocales = GetKeyboardLayoutList(100, localesInstalled);
		HKL locale = (HKL)eLocale;
		S32 i;

		for (i = 0; i < iNumLocales; ++i)
		{
			if (locale == localesInstalled[i])
			{
				return true;
			}
		}

		return false;
	}

	return true;
}

bool g_bEnableLocalizedKeybindValidation = false;
AUTO_CMD_INT(g_bEnableLocalizedKeybindValidation, EnableLocalizedKeybindValidation) ACMD_CMDLINE;

AUTO_FIXUPFUNC;
TextParserResult keybind_FixUpKeybindProfile(KeyBindProfile *pProfile, enumTextParserFixupType eType, void *pExtraData)
{
	S32 i;
	S32 j;

	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
	case FIXUPTYPE_POST_BIN_READ:

		if (!keybind_CheckIfKeyboardLocaleIsInstalled(pProfile->eKeyboardLocale))
		{
			if (g_bEnableLocalizedKeybindValidation)
			{
				const char *pchLocale = StaticDefineIntRevLookup(KeyboardLocaleEnum, pProfile->eKeyboardLocale);

				if (pchLocale && pchLocale[0])
				{
					ErrorFilenamef(pProfile->pchFilename, "Error parsing keybind profile. Keyboard locale %s is not installed on this machine.", pchLocale);
				}
				else
				{
					ErrorFilenamef(pProfile->pchFilename, "Error parsing keybind profile. Invalid keyboard locale 0x%X", pProfile->eKeyboardLocale);
				}
				
			}

			//We have special code to handle the parsing of US English QWERTY keys as a fallback case
			if (pProfile->eKeyboardLocale != KeyboardLocale_EnglishUS)
			{
				break;
			}
		}

		//First fixup all of the keybinds
		for (i = 0; i < eaSize(&pProfile->eaBinds); ++i)
		{
			KeyBind *pBind = pProfile->eaBinds[i];
			keybind_ParseKeyString(pBind->pchKey, &pBind->iKey1, &pBind->iKey2, pProfile->eKeyboardLocale);
			if (!pBind->iKey1)
			{
				ErrorDetailsf("iKey1 = %d iKey2 = %d Keyboard locale: 0x%X Command: %s", pBind->iKey1, pBind->iKey2, pProfile->eKeyboardLocale, pBind->pchCommand);
				ErrorFilenamef(pProfile->pchFilename, "Key name is not valid: %s", pBind->pchKey);
			}
		}

		//Then check for duplicates
		for (i = 0; i < eaSize(&pProfile->eaBinds); i++)
		{
			for (j = i + 1; j < eaSize(&pProfile->eaBinds); j++)
			{
				if (pProfile->eaBinds[i]->iKey1 == pProfile->eaBinds[j]->iKey1
					&& pProfile->eaBinds[i]->iKey2 == pProfile->eaBinds[j]->iKey2)
				{
					ErrorFilenamef(pProfile->pchFilename, "Duplicate bind %s for %s and %s.",
						pProfile->eaBinds[i]->pchKey,
						pProfile->eaBinds[i]->pchCommand,
						pProfile->eaBinds[j]->pchCommand);
				}
			}
		}
		break;
	case FIXUPTYPE_PRE_TEXT_WRITE:
		for (i = 0; i < eaSize(&pProfile->eaBinds); ++i)
		{
			pProfile->eaBinds[i]->pchKey = keybind_GetNames(pProfile->eaBinds[i], pProfile->eKeyboardLocale);
		}
		break;
	}

	return PARSERESULT_SUCCESS;
}

KeyBind *keybind_BindKeyInUserProfile(const char *pchKey, const char *pchCommand)
{
	return keybind_BindKeyInProfile(&s_UserProfile, pchKey, pchCommand);
}

static void CmdPrint(char *string, void *userData)
{
	g_inpPrintf("%s", string);
}

static void CmdPrintToFile(char *string, void *userData)
{
	fprintf((FILE *)userData,"%s",string);
}

// Print out all commands available
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(cmdlist);
void CommandClientCmdList(CmdContext *cmd_context)
{
	KeyBindProfileIterator iter;
	KeyBindProfile *curProfile;
	keybind_NewProfileIterator(&iter);
	if (!g_inpPrintf)
		return;
	if (cmd_context->access_level > 0)
		g_inpPrintf("Commands available to access level %d:", cmd_context->access_level);
	while ( (curProfile = keybind_ProfileIteratorNext(&iter)) )
	{
		if (curProfile->pCmdList)
			cmdPrintList(curProfile->pCmdList,cmd_context->access_level,NULL,false,CmdPrint,cmd_context->language,NULL);
	}
	globCmdParse("scmdlist");
}

// Print out client commands for commands containing <string>
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(cmds);
void CommandClientCmds(CmdContext *cmd_context, char *string)
{
	KeyBindProfileIterator iter;
	KeyBindProfile *curProfile;
	keybind_NewProfileIterator(&iter);
	if (!g_inpPrintf)
		return;
	g_inpPrintf("Commands available to access level %d:", cmd_context->access_level);
	while ( (curProfile = keybind_ProfileIteratorNext(&iter)) )
	{
		if (curProfile->pCmdList)
			cmdPrintList(curProfile->pCmdList,cmd_context->access_level,string,false,CmdPrint,cmd_context->language,NULL);
	}
	globCmdParsef("scmds %s", string);
}

// Print out private client commands for commands containing <string>
AUTO_COMMAND ACMD_NAME(cmdsp);
void CommandClientCmdsPrivate(CmdContext *cmd_context, char *string)
{
	cmdPrintList(&gPrivateCmdList,cmd_context->access_level,string,true,CmdPrint,cmd_context->language,NULL);
}

// Export a list of all commands
AUTO_COMMAND;
void ExportCmds(CmdContext *cmd_context)
{
	FILE *f = fopen("c:/cmdlist.txt","w");
	if (f)
	{	
		KeyBindProfileIterator iter;
		KeyBindProfile *curProfile;
		keybind_NewProfileIterator(&iter);
		fprintf(f, "Commands available to access level %d:\n", cmd_context->access_level);
		while ( (curProfile = keybind_ProfileIteratorNext(&iter)) )
		{
			if (curProfile->pCmdList)
				cmdPrintList(curProfile->pCmdList,cmd_context->access_level,NULL,false,CmdPrintToFile,cmd_context->language,f);
		}
		fclose(f);
	}
}

// Export a list of all commands as a CSV, with extra info
AUTO_COMMAND;
void ExportCmdsAsCSV(CmdContext *cmd_context, bool bShowHidden, S32 iMinAL, S32 iMaxAL)
{
	FILE *f = fopen("c:/ccmdlist.csv","w");
	if (f) {
		fprintf(f, "AL,Name,Hide,Category,Comment\n");
		FOR_EACH_IN_STASHTABLE(gGlobalCmdList.sCmdsByName, Cmd, pCmd)
		{
			if (!(pCmd->flags & (CMDF_COMMANDLINEONLY | CMDF_COMMANDLINE | (bShowHidden ? 0 : CMDF_HIDEPRINT))) &&
				pCmd->access_level >= iMinAL && pCmd->access_level <= iMaxAL) {
				
				fprintf(f, "%d,%s,%d,\"%s\",\"%s\"\n", pCmd->access_level, pCmd->name, (pCmd->flags & CMDF_HIDEPRINT) != 0, pCmd->categories, pCmd->comment);
			}
		}
		FOR_EACH_END;

		fclose(f);
	}
}

static const char *keybind_GetKeyDisplayName(S32 iKey, bool bAbbreviated)
{
	const char *pchName = keybind_GetKeyName(iKey, KeyboardLocale_Current);
	const char *pchTranslated;
	char achKey[1024];
	if (bAbbreviated)
		sprintf(achKey, "KeyName_Abbrev_%s", pchName);
	else 
		sprintf(achKey, "KeyName_%s", pchName);
	pchTranslated = TranslateMessageKey(achKey);
	return pchTranslated ? pchTranslated : pchName;
}

static void TitleCase(unsigned char *pch)
{
	bool bWasSpace = true;
	while (*pch)
	{
		if (bWasSpace)
			*pch = toupper(*pch);
		else
			*pch = tolower(*pch);
		bWasSpace = !isalpha(*pch);

		pch++;
	}
}

const char *keybind_GetDisplayNameFromKeys(S32 iKey1, S32 iKey2, bool bAbbreviated)
{
	const char *pchName1 = NULL;
	const char *pchName2 = NULL;
	static char s_ach[1024];
	if (iKey1)
		pchName1 = keybind_GetKeyDisplayName(iKey1, bAbbreviated);
	if (iKey2)
		pchName2 = keybind_GetKeyDisplayName(iKey2, bAbbreviated);
	if (pchName1 && pchName2)
	{
		sprintf(s_ach, FORMAT_OK(bAbbreviated? "%s%s" : "%s+%s"), pchName1, pchName2);
	}
	else if (pchName1)
		strcpy(s_ach, pchName1);
	else if (pchName2)
		strcpy(s_ach, pchName2);
	else
		strcpy(s_ach, "");
	TitleCase(s_ach);
	return s_ach;
}

const char *keybind_GetDisplayName(const char *pchKeyString, bool bAbbreviated)
{
	S32 iKey1;
	S32 iKey2;
	if (!pchKeyString)
		return "";
	keybind_ParseKeyString(pchKeyString, &iKey1, &iKey2, KeyboardLocale_Current);
	return keybind_GetDisplayNameFromKeys(iKey1, iKey2, bAbbreviated);
}

KeyBindStoredProfiles *keybind_GetUserSelectableProfiles(void)
{
	static KeyBindStoredProfiles *pKeybinds;
	KeyBindProfile *pProfile;

	if (!pKeybinds)
	{	
		int i;
		pKeybinds = StructCreate(parse_KeyBindStoredProfiles);
		for (i = 0; i < eaSize(&s_ParsedData.eaProfiles); i++)		
		{
			pProfile = s_ParsedData.eaProfiles[i];
			if (pProfile->bUserSelectable)
			{
				eaPush(&pKeybinds->eaProfiles, pProfile);
			}
		}
	}
	return pKeybinds;
}

S32 keybind_GetCurrentKeyboardLocale()
{
	const ManagedThread *pInputThread = (gInput ? inpGetDeviceRenderThread(gInput) : NULL);
	HKL locale = 0;

	if (!pInputThread)
	{
		pInputThread = tmGetMainThread();
	}

	locale = GetKeyboardLayout(tmGetThreadId(pInputThread));

	return (S32)locale;
}
