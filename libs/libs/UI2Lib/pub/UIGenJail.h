#pragma once
GCC_SYSTEM;

#include "UICore.h"

typedef struct UITextureAssembly UITextureAssembly;
typedef struct UIStyleFont UIStyleFont;
typedef struct UIGenJail UIGenJail;
typedef struct UIGenStateDef UIGenStateDef;
typedef struct UIGenComplexStateDef UIGenComplexStateDef;
typedef struct DisplaySprite DisplaySprite;

AUTO_STRUCT;
typedef struct UIAspectRatio
{
	S32 iWidth;			AST(STRUCTPARAM REQUIRED)
	S32 iHeight;		AST(STRUCTPARAM)
} UIAspectRatio;

AUTO_STRUCT;
typedef struct UIGenJailDefault
{
	REF_TO(UIGen) hDefaultGen; AST(NAME(DefaultGen) STRUCTPARAM REQUIRED)
} UIGenJailDefault;

AUTO_STRUCT;
typedef struct UIGenJailCellBlock
{
	const char *pchName; AST(POOL_STRING STRUCTPARAM KEY REQUIRED)
	REF_TO(Message) hDisplayName; AST(REQUIRED NON_NULL_REF NAME(DisplayName))
	REF_TO(UITextureAssembly) hAssembly; AST(NAME(Assembly))
	REF_TO(UITextureAssembly) hGhostAssembly; AST(NAME(GhostAssembly))
	REF_TO(UITextureAssembly) hHoverAssembly; AST(NAME(HoverAssembly))
	REF_TO(UITextureAssembly) hHoverGhostAssembly; AST(NAME(HoverGhostAssembly))
	REF_TO(UIStyleFont) hFont; AST(NAME(Font))
	REF_TO(UIStyleFont) hHoverFont; AST(NAME(HoverFont))
	S32 iDefaultCells; AST(DEFAULT(1))
	S32 iMaxCells; AST(DEFAULT(1))
	S32 iMinCells; AST(DEFAULT(1))
	S32 iResizeBorder; AST(DEFAULT(8))
	REF_TO(UITextureAssembly) hButtonAssembly; AST(NAME(ButtonAssembly) NON_NULL_REF)
	UIPosition **eaPosition; AST(NAME(Position) REQUIRED)
	UIGenJailDefault **eaDefaultGens; AST(NAME(DefaultGen))
	UIDirection eResizable; AST(NAME(Resizable))
	UIAspectRatio AspectRatio; AST(NAME(AspectRatio))
	bool bKeepDefaultCells;
	bool bRememberContents;
} UIGenJailCellBlock;

AUTO_STRUCT;
typedef struct UIGenJailDef
{
	const char *pchName; AST(POOL_STRING STRUCTPARAM KEY REQUIRED)
	const char *pchPrisonName; AST(POOL_STRING)
	UIGenJailCellBlock **eaCellBlock; AST(NAME(CellBlock) REQUIRED)
	REF_TO(UIStyleFont) hFont; AST(NAME(Font) NON_NULL_REF)
	S32 iVersion;
	UIGenStateDef **eaStateDef; AST(NAME(StateDef))
	UIGenComplexStateDef **eaComplexStateDef; AST(NAME(ComplexStateDef))
	UIGenAction *pBeforeCreate;
	UIGenAction *pBeforeHide;
	S16 ausScaleAsIf[2]; AST(NAME(ScaleAsIf))
	bool bScaleNoShrink;
	bool bScaleNoGrow;
	const char *pchFilename; AST(CURRENTFILE)
} UIGenJailDef;

AUTO_STRUCT;
typedef struct UIGenJailDefRef
{
	REF_TO(UIGenJailDef) hDef; AST(STRUCTPARAM REQUIRED NON_NULL_REF)
} UIGenJailDefRef;

AUTO_STRUCT;
typedef struct UIGenPrisonJailSet
{
	const char *pchName; AST(STRUCTPARAM KEY REQUIRED)
	UIGenJailDefRef **eaJails; AST(NAME(Jail))
} UIGenPrisonJailSet;

AUTO_STRUCT;
typedef struct UIGenPrisonDefaultJailSet
{
	UIGenJailDefRef **eaJails; AST(NAME(Jail))
} UIGenPrisonDefaultJailSet;

AUTO_STRUCT;
typedef struct UIGenPrisonDef
{
	const char *pchName; AST(POOL_STRING STRUCTPARAM KEY REQUIRED)
	UIGenPrisonJailSet **eaJailSets; AST(NAME(JailSet))
	UIGenPrisonDefaultJailSet *pDefaultJailSet; AST(NAME(DefaultJailSet))
} UIGenPrisonDef;

AUTO_STRUCT;
typedef struct UIGenPrisonDefLoading
{
	UIGenPrisonDef **eaPrisons; AST(NAME(UIGenPrisonDef))
} UIGenPrisonDefLoading;

AUTO_STRUCT;
typedef struct UIGenJailCell
{
	const char *pchName; AST(POOL_STRING)
	REF_TO(UIGenJailDef) hJail;
	REF_TO(UIGen) hGen;
	UIGenJailCellBlock *pBlock; AST(UNOWNED)
	UIGenJail *pJail; AST(UNOWNED)
	UIGen Parent;
	S32 iIndex;
	U32 iLastInteractTime;
	S16 iGrabbedX; AST(DEFAULT(-1))
	S16 iGrabbedY; AST(DEFAULT(-1))
	F32 fAspectRatio;
	UIDirection eResize;
	bool bSave;
} UIGenJailCell;

AUTO_STRUCT;
typedef struct UIGenJail
{
	REF_TO(UIGenJailDef) hJail;
	UIGenJailCell **eaCells;
	U32 bfStates[UI_GEN_STATE_BITFIELD];
	U32 bfComplexStates[UI_GEN_STATE_BITFIELD];
	F32 fScale;
} UIGenJail;

AUTO_STRUCT;
typedef struct UIGenPrison
{
	const char *pchPrisonName; AST(POOL_STRING)
	char *pchCurrentPrison;
} UIGenPrison;

typedef struct UIGenJailKeeper
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE);
	UIGenJail **eaJail;
	UIGen **eaGens;
	UIGenJailCell **eaCells;
	UIGenJailCell *pHovered; AST(UNOWNED)
	UIGenPrison **eaPrisons;
	bool bReprioritize;
	bool bSaveAllCells;
} UIGenJailKeeper;

void ui_GenJailOncePerFrameInput();

void ui_GenJailCellSetGen(UIGenJailCell *pCell, UIGen *pGen);

UIGenJailKeeper *ui_GenJailKeeperCreate(void);
void ui_GenJailKeeperFree(UIGenJailKeeper *pKeeper);
void ui_GenJailKeeperTick(UIGenJailKeeper *pKeeper, UI_PARENT_ARGS);
void ui_GenJailKeeperDraw(UIGenJailKeeper *pKeeper, UI_PARENT_ARGS);

UIGenJail *ui_GenJailShow(UIGenJailKeeper *pKeeper, UIGenJailDef *pDef);
UIGenJail *ui_GenJailHide(UIGenJailKeeper *pKeeper, UIGenJailDef *pDef);
void ui_GenJailDestroy(UIGenJail *pJail);

UIGenJailCell *ui_GenJailKeeperAdd(UIGenJailKeeper *pKeeper, UIGen *pGen);
bool ui_GenJailKeeperRemove(UIGenJailKeeper *pKeeper, UIGen *pGen);
bool ui_GenJailKeeperRemoveAll(UIGenJailKeeper *pKeeper, const char *pchCellBlock);

void ui_GenJailLoad(void);

extern UIGenJailKeeper *g_ui_pDefaultKeeper;
