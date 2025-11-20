#ifndef UI_BUTTON_COMBO_H
#define UI_BUTTON_COMBO_H
GCC_SYSTEM

#include "UICore.h"

typedef struct UIButton UIButton;
typedef struct UIPane UIPane;
typedef struct UISprite UISprite;
typedef struct UIButtonCombo UIButtonCombo;
typedef struct UILabel UILabel;

//Direction in relation to the main button that the pane will open.
typedef enum UIButtonComboDirection {
	POP_UP=0,
	POP_DOWN,
	POP_LEFT,
	POP_RIGHT,
} UIButtonComboDirection;

typedef struct UIButtonComboItem
{
	UIButton *button;
	UILabel *label;
	UILabel *active_label;
	UISprite *sprite;
	UISprite *active_sprite;

	UIButtonCombo *parent;

	F32 icon_width;
	U8 order;

	UIActivationFunc clickedF;
	UserData clickedData;
}UIButtonComboItem;

typedef struct UIButtonCombo
{
	UIWidget widget;

	UIButton *pButton;
	UIButton *pComboButton;
	UIPane *pPane;

	UIButtonComboItem *selected_item;
	UIButtonComboItem **items;
	bool active;
	UIButtonComboDirection direction;

	UIActivationFunc cbChanged;
	UserData pChangedData;

} UIButtonCombo;

SA_RET_NN_VALID UIButtonCombo *ui_ButtonComboCreate(F32 x, F32 y, F32 width, F32 height, UIButtonComboDirection direction, bool use_combo_button);
void ui_ButtonComboFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIButtonCombo *pButtonCombo);
void ui_ButtonComboTick(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo, UI_PARENT_ARGS);
void ui_ButtonComboDraw(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo, UI_PARENT_ARGS);

UIButton* ui_ButtonComboAddItem(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo, SA_PARAM_NN_STR const char *texture, SA_PARAM_OP_STR const char *active_texture, SA_PARAM_OP_STR const char *label, UIActivationFunc clickedF, UserData clickedData);
UIButton* ui_ButtonComboAddOrderedItem(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo, SA_PARAM_NN_STR const char *texture, SA_PARAM_OP_STR const char *active_texture, SA_PARAM_OP_STR const char *label, U8 order, UIActivationFunc clickedF, UserData clickedData);

int ui_ButtonComboGetSelected(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo);

void ui_ButtonComboSetDirection(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo, UIButtonComboDirection direction);
void ui_ButtonComboSetActive(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo, bool active);
void ui_ButtonComboSetSelected(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo, int button);

//If not active then sets active, otherwise changes selected to next in list.
void ui_ButtonComboSelectNext(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo);

void ui_ButtonComboSetChangedCallback(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo, UIActivationFunc cbChanged, UserData pChangedData);

#endif