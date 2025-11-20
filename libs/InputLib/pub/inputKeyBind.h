/***************************************************************************



***************************************************************************/

#ifndef KEYBIND_H
#define KEYBIND_H
GCC_SYSTEM
#include "referencesystem.h"
typedef struct CmdList CmdList;

typedef void (*KeyBindPrintf)(FORMAT_STR const char* string, ...);
typedef void (*KeyBindSetTimeStamp)(int timestamp);
typedef void (*KeyBindQuit)(void);

typedef S32 KeyBindProfileIterator;
typedef enum CmdContextFlag CmdContextFlag;
typedef enum enumCmdContextHowCalled enumCmdContextHowCalled;
typedef struct Message Message;
typedef struct CmdParseStructList CmdParseStructList;

#define KEYBIND_DEFAULT_FILENAME "keybinds.txt"

AUTO_ENUM;
typedef enum KeyBindPriority
{
	// Used for standard in-game keybinds, e.g. WASD.
	InputBindPriorityGame = 0,

	// Preempt game keybinds, for e.g. freecam mode.
	InputBindPriorityCamera,

	// Keybinds used for development tools.
	InputBindPriorityDevelopment,

	// Reserved for user-specified keybinds.
	InputBindPriorityUser,

	// Used mostly by the editor and other UIs to set up a "wall" between the game and UI, to ensure modality.
	InputBindPriorityBlock,

	// Used by modal UIs such as the loading screen.
	InputBindPriorityUI,

	// Used by the debug console, and basically disables all other binds.
	InputBindPriorityConsole,

	InputBindPriorityCount

} KeyBindPriority;

AUTO_ENUM;
typedef enum KeyboardLocale
{
	KeyboardLocale_Current		= 0, //Use when calling functions that take a locale and you just want what the client is set to
	KeyboardLocale_EnglishUS	= 0x04090409,
	KeyboardLocale_FrenchFR		= 0x040C040C,
	KeyboardLocale_GermanDE		= 0x04070407,
	KeyboardLocale_ItalianIT	= 0x04100410,
	KeyboardLocale_TurkishTR	= 0x041F041F,
	KeyboardLocale_PolishPL		= 0xF0070415,
	KeyboardLocale_RussianRU	= 0x04190419
} KeyboardLocale;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_IGNORE(IsChar);
typedef struct KeyBind
{
	S32 iKey1; AST(NO_WRITE)
	S32 iKey2; AST(NO_WRITE)
	const char *pchKey; AST(NAME("Key") STRUCTPARAM POOL_STRING)
	char *pchCommand; AST(STRUCTPARAM)

	bool bSecondary : 1;
		// Hint to the keybind UI about which column to display this keybind in.

	bool bContinueProcessing : 1;
		// If true, do not capture this key; continue processing more binds after
		// running the command associated with it.

	bool bPressed : 1; NO_AST
} KeyBind;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct KeyBindProfile
{
	const char *pchName; AST(REQUIRED)
	const char *pchFilename; AST(CURRENTFILE)

	//The locale indicates which keyboard to use to interpret the keybinds
	KeyboardLocale eKeyboardLocale; AST(DEFAULT(KeyboardLocale_EnglishUS))

	// An EArray of binds in this profile.
	KeyBind **eaBinds; AST(NAME("KeyBind"))

	bool bTrickleKeys; AST(DEFAULT(1))// allow unhandled keys to be handled by other profiles?
	bool bTrickleCommands; AST(DEFAULT(1)) // allow unhandled commands to be passed along

	// Called if a key is pressed but no command is found; if trickle keys
	// is off, this will handle the key and stop other profiles.
	void (*cbAllOtherKeys)(S32 iKey); NO_AST

	CmdList *pCmdList; NO_AST

	KeyBindPriority ePriority;
	REF_TO(Message) hNameMsg; AST(ADDNAMES(DisplayName)) // Display name, if this is a public profile
	
	bool bUserSelectable; // Profile that can be selected by the user in the UI
	bool bJoystick; // Contains exclusively joystick binds and so is exempt from some normal masking rules
	bool bEntityBinds; // Add to the list of entity binds

	//Control scheme regions that this profile can attach to
	const char** ppchSchemeRegions; AST(NAME(SchemeRegion) POOL_STRING)

} KeyBindProfile;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct KeyBindStoredProfiles
{
	KeyBindProfile **eaProfiles; AST(NAME("KeyProfile"))
} KeyBindStoredProfiles;

// Get the name for a key, given its value.
SA_RET_OP_STR const char *keybind_GetKeyName(S32 iKey, KeyboardLocale eLocale);
S32 keybind_GetKeyScancode(SA_PARAM_OP_STR const char *pchName, KeyboardLocale eLocale);

// Get a key name and value by its index in the internal key array.
// Some keys have more than one name, so to handle them, you need
// to iterate from 0 to the array size rather than to the max keycode.
S32 keybind_GetKeyArraySize(void);
S32 keybind_GetKeyCodeByIndex(S32 iIndex);
const char *keybind_GetKeyNameByIndex(S32 iIndex);

// Find the key bind profile read in with this name.
S32 keybind_FindInProfiles(SA_PARAM_NN_VALID KeyBindStoredProfiles *pProfiles, SA_PARAM_NN_STR const char *pchProfile);
SA_RET_OP_VALID KeyBindProfile *keybind_FindProfile(SA_PARAM_NN_STR const char *pchProfile);

//Find the keybind profile that starts with the given string and matches the given keyboard locale
KeyBindProfile *keybind_FindProfileForKeyboardLocale(const char *pchStartString, S32 locale);

// Put the binds from profileName into kbp. This does not adjust kbp's priority,
// trickle settings, name, or display name.
void keybind_CopyBindsFromName(SA_PARAM_NN_STR const char *pchSourceProfile, SA_PARAM_NN_VALID KeyBindProfile *pDestProfile);

KeyBind *keybind_BindKeyInProfile(SA_PARAM_NN_VALID KeyBindProfile *pProfile, SA_PARAM_NN_STR const char *pchKey, SA_PARAM_OP_STR const char *pchCommand);
KeyBind *keybind_BindKeyInProfileEx(SA_PARAM_NN_VALID KeyBindProfile *pProfile, SA_PARAM_NN_STR const char *pchKey, SA_PARAM_OP_STR const char *pchCommand, bool bSecondary);

KeyBind *keybind_BindKeyInUserProfile(SA_PARAM_NN_STR const char *pchKey, SA_PARAM_NN_STR const char *pchCommand);

// Search active profiles for the key, run its command, trickle appropriately.
bool keybind_ExecuteKey(S32 iKey, bool bState, U32 uiTime);

// Run the command for this key bind.
void keybind_ExecuteCommand(KeyBind *pBind, bool bState);

// Run the command for this key bind and do other key-related actions.
bool keybind_ExecuteKeyBind(SA_PARAM_NN_VALID KeyBind *pBind, bool bState, U32 uiTime);

// Return pBind if pBind is the best keybind for the pressed key, given that
// pBest is the current best keybind. Keybinds with two matching keys are better
// than keybinds with one matching key.
KeyBind *keybind_BindIsActiveBest(KeyBind *pBind, S32 iKey, KeyBind *pBest);

// Push a new profile onto the given priority's list. The most recently
// pushed profile are checked first when executing commands.
void keybind_PushProfile(SA_PARAM_NN_VALID KeyBindProfile *profile);
void keybind_PushProfileName(SA_PARAM_NN_STR const char *pchProfile);
void keybind_PushProfileEx(SA_PARAM_NN_VALID KeyBindProfile *profile, KeyBindPriority priority);

// Remove a profile from the given priority's list (it does not need to
// be the most recently pushed one).
void keybind_PopProfile(SA_PARAM_NN_VALID KeyBindProfile *profile);
void keybind_PopProfileName(SA_PARAM_NN_STR const char *pchProfile);
void keybind_PopProfileEx(SA_PARAM_NN_VALID KeyBindProfile *profile, KeyBindPriority priority);

// Return the topmost profile for the given priority.
SA_RET_OP_VALID KeyBindProfile *keybind_GetCurrentProfile(KeyBindPriority priority);

// Returns true if the given profile is on the list of active profiles
bool keybind_IsProfileActive(KeyBindProfile *pProfile);

// Return the total number of profiles in all priorities.
S32 keybind_GetProfileCount(void);

// Iterate over all profiles in all priorities, in order. This is not
// a safe iterator; modifying the profile list while it is iterating
// will cause repeats or skips.
void keybind_NewProfileIterator(SA_PRE_NN_FREE SA_POST_NN_VALID KeyBindProfileIterator *iter);
SA_RET_OP_VALID KeyBindProfile *keybind_ProfileIteratorNext(SA_PARAM_NN_VALID KeyBindProfileIterator *iter);

// Print active binds using the function passed into keybind_Init.
void keybind_PrintBinds(void);

// Save current game bindings to filename (or keybinds.txt).
void keybind_SaveUserBinds(SA_PARAM_OP_STR const char *pchFilename);

// Load current game bindings from filename (or keybinds.txt).
void keybind_LoadUserBinds(SA_PARAM_OP_STR const char *pchFilename);

// Load/save using the given profile.
void keybind_LoadProfile(SA_PARAM_NN_VALID KeyBindProfile *pProfile, SA_PARAM_NN_STR const char *pchFilename);
void keybind_SaveProfile(SA_PARAM_NN_VALID const KeyBindProfile *pProfile, SA_PARAM_NN_STR const char *pchFilename);

// Default CmdParse function. This is replaced by gclCmdParse (or similar) in
// some cases, but is the main cmdParse callback in others (e.g. AssetManager).
int keybind_CmdParse(SA_PARAM_OP_STR const char *pchCommand, char **ppRetVal, CmdContextFlag iCmdContextFlags, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);

// Initialize the key bind system.
void keybind_Init(SA_PARAM_OP_VALID KeyBindPrintf cbPrintf, SA_PARAM_OP_VALID KeyBindSetTimeStamp cbSetTimeStamp, SA_PARAM_OP_VALID KeyBindQuit cbQuit, SA_PARAM_OP_STR const char *pchProfile);

// Get/set function to call when the close button on the window is pressed.
SA_RET_OP_VALID KeyBindQuit keybind_GetQuit(void);
void keybind_SetQuit(SA_PARAM_OP_VALID KeyBindQuit cbQuit);

S32 keybind_FindBindInProfileEx(KeyBindProfile *pProfile, S32 iKey1, S32 iKey2, bool bIncludeNormalized);
#define keybind_FindBindInProfile(pProfile, iKey1, iKey2) keybind_FindBindInProfileEx(pProfile, iKey1, iKey2, false)
SA_RET_OP_VALID KeyBind *keybind_BindForCommandInProfile(SA_PARAM_NN_VALID KeyBindProfile *pProfile, SA_PARAM_NN_STR const char *pchCommand, bool bJoystick);
SA_RET_OP_VALID KeyBind *keybind_BindForCommandInProfileEx(SA_PARAM_NN_VALID KeyBindProfile *pProfile, const char *pchCommand, bool bJoystick, bool bCheckAbove);
void keybind_BindsForCommandInProfile(SA_PARAM_NN_VALID KeyBindProfile *pProfile, SA_PARAM_NN_STR const char *pchCommand, bool bJoystick, SA_PRE_NN_NN_VALID KeyBind ***peaBinds);
void keybind_BindsForCommandInProfileEx(SA_PARAM_NN_VALID KeyBindProfile *pProfile, SA_PARAM_NN_STR const char *pchCommand, bool bJoystick, bool bCheckAbove, SA_PRE_NN_NN_VALID KeyBind ***peaBinds);
SA_RET_OP_VALID KeyBind *keybind_BindForCommand(SA_PARAM_NN_STR const char *pchCommand, bool bJoystick, bool bCheckAbove);
void keybind_BindsForCommand(SA_PARAM_NN_STR const char *pchCommand, bool bJoystick, bool bCheckAbove, SA_PRE_NN_NN_VALID KeyBind ***peaBinds);

// Returns a static buffer.
SA_RET_OP_STR const char *keybind_GetNames(SA_PARAM_OP_VALID const KeyBind *pBind, KeyboardLocale eLocale);
SA_RET_OP_STR const char *keybind_GetKeyScancodeStrings(const KeyBind *pBind);

void keybind_ParseKeyString(SA_PARAM_NN_STR const char *pchKeys, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *piKey1, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *piKey2, KeyboardLocale eLocale);

SA_RET_NN_STR const char *keybind_GetDisplayNameFromKeys(S32 iKey1, S32 iKey2, bool bAbbreviated);
SA_RET_NN_STR const char *keybind_GetDisplayName(SA_PARAM_OP_STR const char *pchKeyString, bool bAbbreviate);

// Get a list of all keybind profiles below the given one, in the keybind stack.
// If the given profile is not actually on the stack, everything with a lower priority
// is returned. Returns whether it was found or not.
bool keybind_BindsBelow(SA_PARAM_OP_VALID KeyBindProfile *pProfile, SA_PARAM_NN_OP_VALID KeyBindProfile ***peaProfiles);

extern ParseTable parse_KeyBind[];
#define TYPE_parse_KeyBind KeyBind
extern ParseTable parse_KeyBindProfile[];
#define TYPE_parse_KeyBindProfile KeyBindProfile


//generally speaking no one should call this command directly. I'm using it to set up GodMode because I don't
//really know how to do this stuff ABW
void BindKeyCommand(const char *pchKey, ACMD_NAMELIST(pAllCmdNamesForAutoComplete) ACMD_SENTENCE pchCommand);

extern KeyBindPrintf g_inpPrintf;

// Gets the list of user selectable profiles
KeyBindStoredProfiles *keybind_GetUserSelectableProfiles(void);

KeyBind *keybind_ProfileFindBindByCommand(KeyBindProfile *pProfile, const char *pchCommand);
KeyBind *keybind_ProfileFindBindByKeyString(KeyBindProfile *pProfile, const char *pchKey);
KeyBind *keybind_GetBindByKeys(S32 iKey1, S32 iKey2);
void keybind_GetBinds(KeyBind ***peaBinds, bool bAlloc);

bool key_IsJoystick(S32 iKey);

S32 keybind_GetCurrentKeyboardLocale(void);
bool keybind_CheckIfKeyboardLocaleIsInstalled(KeyboardLocale eLocale);

#endif
