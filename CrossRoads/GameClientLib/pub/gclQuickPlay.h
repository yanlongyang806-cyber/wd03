#ifndef GCL_QUICK_PLAY_H
#define GCL_QUICK_PLAY_H
GCC_SYSTEM

// gclQuickPlay - a generic way to make "get in the game fast" scripts.
// A QuickPlayMenu is a nested set of scripts to run; each item in the menu
// can override some keybinds and automatically runs some scripts once
// in the game.

typedef struct KeyBind KeyBind;
typedef struct QuickPlayMenu QuickPlayMenu;
typedef struct QuickPlayMenuExtra QuickPlayMenuExtra;
typedef struct Login2CharacterCreationData Login2CharacterCreationData;

AUTO_STRUCT;
typedef struct QuickPlayMenus
{
	QuickPlayMenu **eaMenus; AST(NAME(QuickPlay))
} QuickPlayMenus;

AUTO_STRUCT;
typedef struct QuickPlayMenu
{
	const char *pchDisplayName; AST(NAME(DisplayName))

	const char *pchPlayerName; AST(NAME(PlayerName))
	const char *pchMapName; AST(NAME(MapName))

	char **eachScripts; AST(NAME(Script))

	// If there's only one submenu available, it's automatically chosen.
	QuickPlayMenu **eaSubMenus; AST(NAME(SubMenu))

	KeyBind **eaBinds; AST(NAME(KeyBind))

	bool bDefaultCharacter; AST(NAME(DefaultCharacter))

	Login2CharacterCreationData *pInfo; AST(LATEBIND)
} QuickPlayMenu;


void gclQuickPlay_PushBinds(void);
void gclQuickPlay_PopBinds(void);

// This should be called from gclGameplay's once per frame function.
void gclQuickPlay_ExecuteScripts(void);

// Queue a command to be run when gclQuickPlay_ExecuteScripts is called.
void gclQuickPlay_QueueCommand(const char *pchCommand);

// Add a keybind to the profile QuickPlay uses.
void gclQuickPlay_AddKeyBind(const KeyBind *pBind);

// Returns the number of quick play profiles available.
S32 gclQuickPlay_CountQuickPlayProfiles(void);

void gclQuickPlay_Start(void);

void gclQuickPlay_FillDefaultCharacter(Login2CharacterCreationData *characterCreationDAta);

void gclQuickPlay_QuickPlayFillCharacter(QuickPlayMenu ***peaChosenMenus, Login2CharacterCreationData *characterCreationDAta);

Login2CharacterCreationData *gclQuickPlay_GetCharacterCreationData(void);
const char *gclQuickPlay_GetMapName(void);

void gclQuickPlay_Reset(void);
void gclQuickPlay_Load(void);

#endif
