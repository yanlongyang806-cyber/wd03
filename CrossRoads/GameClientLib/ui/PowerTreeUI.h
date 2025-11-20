#pragma once
GCC_SYSTEM
#ifndef POWER_TREE_UI_H
#define POWER_TREE_UI_H

#include "UICore.h"
#include "ReferenceSystem.h"

typedef struct PTNodeDef PTNodeDef;
typedef struct PTGroupDef PTGroupDef;
typedef struct PTNodeTopDown PTNodeTopDown;
typedef struct PTGroupTopDown PTGroupTopDown;
typedef struct FCUIPowerTreeEntry FCUIPowerTreeEntry;
typedef struct SMFBlock SMFBlock;
typedef struct FCUISMFPane FCUISMFPane;
typedef struct UIButton UIButton;
typedef struct UIGen UIGen;

#define FCUI_POWER_NODE_NAME_LIFETIME 2.f
#define FCUI_POWER_NODE_SLIDE_TIME (1.f/4.f)
#define FCUI_POWER_NODE_ARROW_SLIDE_TIME (2.f/5.f)

typedef enum FCUIPowerTreeEntryPos
{
	kPowerTreeEntryAlone,
	kPowerTreeEntryStart,
	kPowerTreeEntryMiddle,
	kPowerTreeEntryEnd,
} FCUIPowerTreeEntryPos;

typedef struct FCUIPowerTreeEntry
{
	FCUIPowerTreeEntry *pParent;
	REF_TO(PTNodeDef) hNode;
	REF_TO(PTGroupDef) hGroup;
	REF_TO(PowerTreeDef) hTree;
	FCUIPowerTreeEntry **eaChildren;
	FCUIPowerTreeEntryPos eGroupType;
	S32 iActiveChild;
	S32 iLastActiveChild;

	// The rank the player currently owns, *not* the rank about to be bought.
	S32 iRank;

	// The level in the tree, the root is 0, etc.
	S32 iLevel;
	bool bCanBuy : 1;
	bool bCanEverBuy : 1;

	F32 fDisplayName;
} FCUIPowerTreeEntry;

typedef struct FCUIPowerTreeView
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE);
	FCUIPowerTreeEntry *pTop;
	FCUIPowerTreeEntry *pActive;
	Character *pChar;
	FCUISMFPane *pPane;
	AtlasTex *pDefault;

	F32 fNodeSpacing;
	F32 fGroupSpacing;
	F32 fRowSpacing;

	char **eaBoughtNodes;
	char **eaBoughtGroups;

	AtlasTex *pArrowOn;
	AtlasTex *pArrowTipOn;
	AtlasTex *pArrowOff;
	AtlasTex *pArrowTipOff;
	AtlasTex *pRankOn;
	AtlasTex *pRankOff;

	F32 fNodeWidth;
	F32 fNodeHeight;
	F32 fGroupHeight;
	F32 fIconBorder;
	F32 fGroupSideBorder;
	F32 fGroupTopBorder;
	F32 fGroupBottomBorder;
	F32 fRankSpacing;

	F32 fHorizontalSlideState;
	F32 fVerticalSlideState;
	F32 fArrowSlideTime;

	S32 iLastLevel;

	UIButton *pBuyButton;
	UIGen *pBuyGen;
	UIActivationFunc cbPurchase;
	UserData pPurchaseData;
	UIActivationFunc cbUndo;
	UserData pUndoData;
} FCUIPowerTreeView;

NN_PTR_MAKE FCUIPowerTreeEntry *ptui_PowerTreeEntryCreateTop(NN_PTR_GOOD PowerTreeTopDown *pTree);
NN_PTR_MAKE FCUIPowerTreeEntry *ptui_PowerTreeEntryCreate(FCUIPowerTreeEntry *pParent, PTNodeTopDown *pNode, PTGroupTopDown *pGroup, PowerTreeTopDown *pTree);
void ptui_PowerTreeEntryFree(NN_PTR_GOOD FCUIPowerTreeEntry *pEntry);
void ptui_PowerTreeEntryDraw(NN_PTR_GOOD FCUIPowerTreeEntry *pEntry, CBox *pBox, F32 fZ, bool bActive);
OPT_PTR_GOOD FCUIPowerTreeEntry *ptui_PowerTreeEntryLeft(NN_PTR_GOOD FCUIPowerTreeEntry *pEntry);
OPT_PTR_GOOD FCUIPowerTreeEntry *ptui_PowerTreeEntryRight(NN_PTR_GOOD FCUIPowerTreeEntry *pEntry);
void ptui_PowerTreeEntryAppendGroup(FCUIPowerTreeEntry *pParent, PTGroupTopDown *pGroup, PowerTreeTopDown *pTree);
void ptui_PowerTreeEntryPresentTip(FCUIPowerTreeEntry *pEntry);
void ptui_PowerTreeEntryClearDisplay(FCUIPowerTreeEntry *pEntry);

NN_PTR_MAKE FCUIPowerTreeView *ptui_PowerTreeViewCreate(PowerTreeDef *pTree, Character *pChar, FCUISMFPane *pPane, UIButton *pBuy, UIGen *pBuyGen);
void ptui_PowerTreeViewTick(FCUIPowerTreeView *pView, UI_PARENT_ARGS);
void ptui_PowerTreeViewDraw(FCUIPowerTreeView *pView, UI_PARENT_ARGS);
void ptui_PowerTreeViewFree(FCUIPowerTreeView *pView);
bool ptui_PowerTreeViewInput(FCUIPowerTreeView *pView, KeyInput *pKey);
void ptui_PowerTreeUpdateSMF(FCUIPowerTreeView *pView);
void ptui_PowerTreeViewBuy(FCUIPowerTreeView *pView, UserData pDummy);
void ptui_PowerTreeViewUndo(FCUIPowerTreeView *pView, UserData pDummy);
void ptui_PowerTreeViewSetVisible(FCUIPowerTreeView *pView, FCUIPowerTreeEntry *pEntry);
void ptui_PowerTreeEntryTickInLine(FCUIPowerTreeView *pView, FCUIPowerTreeEntry *pEntry, F32 fCenterX, F32 fTopY, F32 fZ, bool bBorder, F32 fScale, F32 *pfTop, F32 *pfBottom);
void ptui_PowerTreeEntryDrawSingle(FCUIPowerTreeView *pView, FCUIPowerTreeEntry *pEntry, F32 fX, F32 fY, F32 fZ, F32 fScale, bool bActive);
void ptui_PowerTreeEntryDrawInLine(FCUIPowerTreeView *pView, FCUIPowerTreeEntry *pEntry, F32 fCenterX, F32 fTopY, F32 fZ, bool bBorder, F32 fScale, F32 *pfTop, F32 *pfBottom);
void ptui_PowerTreeViewSetActive(FCUIPowerTreeView *pView, S32 iActive);
bool ptui_PowerTreeAreAnyBuyable(FCUIPowerTreeView *pView);
void ptui_PowerTreeEntryUpdate(FCUIPowerTreeEntry *pEntry, Character *pChar);

#endif