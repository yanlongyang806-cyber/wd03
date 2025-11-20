#ifndef UI_EXPRESSION_ENTRY_H
#define UI_EXPRESSION_ENTRY_H
GCC_SYSTEM

#include "UICore.h"

typedef struct Expression Expression;
typedef struct ExprContext ExprContext;
typedef struct UITextEntry UITextEntry;
typedef struct UIButton UIButton;
typedef struct UIWindow UIWindow;

typedef struct UIExpressionEntry
{
	UIWidget widget;

	UITextEntry *pEntry;
	UIButton *pButton;

	UIActivationFunc cbChanged;
	UserData pChangedData;
	UIActivationFunc cbEnter;
	UserData pEnterData;
	UIWindow *pExprEditor;

	Expression *pExpr;
	ExprContext *pContext;
} UIExpressionEntry;

SA_RET_NN_VALID UIExpressionEntry *ui_ExpressionEntryCreate(SA_PARAM_NN_STR const char *pExprText, SA_PARAM_OP_VALID ExprContext *pContext);
void ui_ExpressionEntryFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIExpressionEntry *pExprEntry);
void ui_ExpressionEntryTick(SA_PARAM_NN_VALID UIExpressionEntry *pExprEntry, UI_PARENT_ARGS);
void ui_ExpressionEntryDraw(SA_PARAM_NN_VALID UIExpressionEntry *pExprEntry, UI_PARENT_ARGS);

const char *ui_ExpressionEntryGetText(SA_PARAM_NN_VALID UIExpressionEntry *pExprEntry);
void ui_ExpressionEntrySetText(SA_PARAM_NN_VALID UIExpressionEntry *pExprEntry, const char *pchExprText);

void ui_ExpressionEntrySetChangedCallback(SA_PARAM_NN_VALID UIExpressionEntry *pExprEntry, UIActivationFunc cbChanged, UserData pChangedData);
void ui_ExpressionEntrySetEnterCallback(SA_PARAM_NN_VALID UIExpressionEntry *pExprEntry, UIActivationFunc cbEnter, UserData pEnterData);

#endif