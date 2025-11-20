#pragma once
GCC_SYSTEM
#ifndef __WORLDEDITORAPPEARANCESWAPS_H__
#define __WORLDEDITORAPPEARANCESWAPS_H__

#ifndef NO_EDITORS

typedef struct EMPanel EMPanel;
typedef struct UIList UIList;
typedef struct UIComboBox UIComboBox;
typedef struct UITab UITab;
typedef struct UIMenu UIMenu;
typedef struct UIMenuItem UIMenuItem;

typedef bool (*WleAESwapFunc)(const char * const *texSwaps, const char * const *matSwaps, void *userData);

typedef struct LocalSwap
{
	const char *origName;
	const char *replaceName;
} LocalSwap;

typedef struct WleAESwapUI
{
	EMPanel *panel;
	UITab *tab;
	UIWidget *parent;

	UIComboBox *swapType;
	UIList *texSwapList, *matSwapList;

	bool colAsc;
	bool sortNewCol;

	char **eaSwapType;
	LocalSwap **eaTexSwap, **eaMatSwap;

	UIMenu *matRClickMenu;
	UIMenu *texRClickMenu;
	UIMenuItem *item_clear, *item_new_texword, *item_edit_texword;

	bool bForWorldEditor;

	// callbacks
	WleAESwapFunc setSwapsF;
	void *userData;
} WleAESwapUI;

typedef enum wleSwapOptions {
	MATERIAL_SWAP = (1<<0),
	TEXTURE_SWAP = (1<<1),
} wleSwapOptions;

F32 wleAESwapsUICreate(WleAESwapUI *ui, EMPanel *panel, UITab *tab, UIWidget *parent, WleAESwapFunc swapF, void *userData, wleSwapOptions swapOptions, bool bForWorldEditor, F32 x, F32 startY);
void wleAESwapsRebuildUI(WleAESwapUI *ui, StashTable materials, StashTable textures);
void wleAESwapsFreeSwaps(WleAESwapUI *ui);
void wleAESwapsFreeData(WleAESwapUI *ui);

#endif // NO_EDITORS

#endif // __WORLDEDITORAPPEARANCESWAPS_H__