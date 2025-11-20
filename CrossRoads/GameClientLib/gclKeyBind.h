#pragma once
GCC_SYSTEM

typedef struct Entity Entity;
typedef struct Message Message;
typedef struct EntityBindCommands EntityBindCommands;
typedef struct EntityKeyBinds EntityKeyBinds;
typedef struct KeyBind KeyBind;
typedef struct KeyBindProfile KeyBindProfile;

extern StaticDefineInt ControlSchemeRegionTypeEnum[];

AUTO_ENUM;
typedef enum EntityBindPolicy
{
	kEntityBindPolicy_Default = 0,
	kEntityBindPolicy_KeepExisting = 0x00000001,
	kEntityBindPolicy_KeepOnRebind = 0x00000002,
} EntityBindPolicy;

AUTO_ENUM;
typedef enum EntityBindVisibility
{
	kEntityBindVisibility_Never = 0x00000001,
	kEntityBindVisibility_AnyPrimaryBinds = 0x00000002,
	kEntityBindVisibility_AnySecondaryBinds = 0x00000004,
	kEntityBindVisibility_AnyGamepadBinds = 0x00000008,
} EntityBindVisibility;

AUTO_STRUCT;
typedef struct EntityBindCommand
{
	const char *pchCommand; AST(REQUIRED STRUCTPARAM)
	REF_TO(Message) hDisplayName; AST(REQUIRED NON_NULL_REF STRUCTPARAM NAME(DisplayName))
	S32 eBindPolicy; AST(NAME(BindPolicy) FLAGS SUBTABLE(EntityBindPolicyEnum))
	const char **eaOldCommands; AST(NAME(OldCommand))
} EntityBindCommand;

AUTO_STRUCT;
typedef struct EntityBindCommands
{
	REF_TO(Message) hDisplayName;		AST(NAME(DisplayName) NON_NULL_REF)
	EntityBindCommand **eaCommand;		AST(NAME(Command))
	EntityBindCommands **eaCategory;	AST(NAME(Category))
	S32 eSchemeRegions;					AST(NAME(SchemeRegions) FLAGS SUBTABLE(ControlSchemeRegionTypeEnum))
		// Display the bind commands for only the following scheme regions (Empty=All)
	const char** ppchProfiles;			AST(NAME(Profile))
		// Display the bind commands for only the following profiles (Empty=All)
	S32 eVisible;						AST(NAME(Visible) FLAGS SUBTABLE(EntityBindVisibilityEnum))
	bool bHidden;						AST(NO_WRITE)
	S32 iPrimaryBinds;					AST(NO_WRITE)
	S32 iSecondaryBinds;				AST(NO_WRITE)
	S32 iGamepadBinds;					AST(NO_WRITE)
} EntityBindCommands;

AUTO_STRUCT;
typedef struct EntityBindCommandKey
{
	char *pchKey;
	S32 iKey1; AST(NO_WRITE)
	S32 iKey2; AST(NO_WRITE)
} EntityBindCommandKey;

AUTO_STRUCT;
typedef struct EntityBindEntry
{
	REF_TO(Message) hCategoryName;	AST(NAME(CategoryName))
	EntityBindCommand *pDef;		AST(UNOWNED)
	EntityBindCommands *pCategoryDef; AST(UNOWNED)
	EntityBindCommandKey Primary;
	EntityBindCommandKey Secondary;
	EntityBindCommandKey Gamepad;
	bool bIsCategory;
} EntityBindEntry;

AUTO_STRUCT;
typedef struct BindSpecialEntry
{
	const char *pchKey; AST(POOL_STRING STRUCTPARAM)
	S32 iKeyCode; AST(NO_WRITE)
} BindSpecialEntry;

AUTO_STRUCT;
typedef struct BindSpecialEntries
{
	BindSpecialEntry **eaEntry; AST(NAME(Key))
} BindSpecialEntries;

AUTO_STRUCT;
typedef struct KeyboardLayoutKey
{
	const char *pchKey; AST(POOL_STRING STRUCTPARAM REQUIRED)
	S32 iKeyCode; AST(NO_WRITE)
	S16 iX;
	S16 iY;
	S16 iWidth;
	S16 iHeight;
	char *pchChar;
	const char *pchTexture; AST(RESOURCEDICT(Texture) POOL_STRING)
} KeyboardLayoutKey;

AUTO_STRUCT;
typedef struct KeyboardLayoutSublayout
{
	const char *pchSubLayout; AST(POOL_STRING STRUCTPARAM REQUIRED)
	S16 iX;
	S16 iY;
	KeyboardLayoutKey **eaTransformedKeys; AST(NAME(TransformedKey) NO_WRITE)
} KeyboardLayoutSublayout;

AUTO_STRUCT;
typedef struct KeyboardLayout
{
	const char *pchName; AST(POOL_STRING STRUCTPARAM REQUIRED KEY)
	const char *pchTexture; AST(RESOURCEDICT(Texture) POOL_STRING)
	REF_TO(Message) hDisplayName; AST(NAME(DisplayName) NON_NULL_REF)
	KeyboardLayoutKey **eaKeys; AST(NAME(Key))
	KeyboardLayoutSublayout **eaSubLayouts; AST(NAME(Layout) NO_INDEX)
	S16 iWidth;
	S16 iHeight;
} KeyboardLayout;

AUTO_STRUCT;
typedef struct KeyboardLayouts
{
	KeyboardLayout **eaKeyboards; AST(NAME(Keyboard))
	KeyboardLayout **eaListedKeyboards; AST(NAME(ListedKeyboard) UNOWNED NO_WRITE)
} KeyboardLayouts;

AUTO_STRUCT;
typedef struct KeyCommandEntry
{
	const char *pchKey; AST(POOL_STRING)
	S32 iKey1; AST(NO_WRITE)
	S32 iKey2; AST(NO_WRITE)
	KeyboardLayoutKey *pDef; AST(UNOWNED)
	EntityBindCommand *pCommand; AST(UNOWNED)
	char *pchBindCommand;
	bool bUsed;
} KeyCommandEntry;

void gclKeyBindFillBinds(void);

// Push or pop the entity keybind profile from the stack.
void gclKeyBindEnable(void);
void gclKeyBindDisable(void);
bool gclKeyBindIsEnabled(void);

// Copy key bind states of eaBinds onto profile binds, 
// and cleanup any toggle commands that are no longer bound to the same keys as before.
void gclKeyBindProfileFixKeyBindStates(KeyBind** eaBinds);

// Translate information stored on the Entity into a keybind profile.
void gclKeyBindFillFromEntity(SA_PARAM_OP_VALID Entity *pEnt);

// Clear entity-derived keybind profile.
void gclKeyBindClear(void);

// Translate the internal keybind profile into EntityKeyBinds, and
// update the entity on the server.
void gclKeyBindSyncToServerEx(KeyBindProfile* pProfile);
void gclKeyBindSyncToServer(void);

void gclKeyBindUnbindAll(void);

void gclKeyBindAddBindableRegion( S32 eSchemeRegion );

// Currently, just a hack to change the way keybinds are loaded for NW
void gclKeyBindEnterGameplay();
void gclKeyBindExitGameplay();
