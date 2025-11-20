#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

/******
* The expression editor is a modal window that can be used for expression entry.  The expression
* editor supports some limited autocomplete functionality and multi-line entry.  In order to use it, you
* need only to call exprEdOpen.  The setup API should already have been called upon initialization of the
* editors.
*
* exprEdOpen takes a callback function, a starting expression, some data, and a line index.  The callback
* is invoked when the user completes expression entry, passing a new expression as a callback parameter.
* The new expression pointer passed to the callback should NOT be used directly, as it is destroyed
* immediately after the callback is invoked.  The correct behavior for the callback should be to copy out
* the new expression's contents (using exprCopy).  By default, you should set the singleLineIdx parameter
* to -1 to edit an entire expression.  If you wish to just edit a single line of the expression, set this
* parameter to the line's index.  This is useful in creating a cleaner and more organized expression editing
* interface.  See the FSM Editor for an example of multi-line expression editing.
******/
#ifndef __EXPRESSIONEDITOR_H__
#define __EXPRESSIONEDITOR_H__

typedef struct UIWindow UIWindow;
typedef struct Expression Expression;
typedef struct ExprContext ExprContext;
typedef const void *DictionaryHandleOrName;

typedef void (*ExprEdExprFunc) (Expression *expr, UserData data);
typedef void (*ExprEdValidationClicked)(int, const char *, void *);

#ifndef NO_EDITORS
#define EXPR_ED_MAX_DESC_LEN 80
#define EXPR_ED_MAX_EXPR_LEN 2000
#define EXPR_ED_MAX_LINE_LEN 2000
#define EXPR_ED_MAX_NUM_LINES 200

void exprEdRegisterValidationFunc(SA_PARAM_NN_STR const char *type, SA_PARAM_NN_VALID ExprEdValidationClicked clickFunc, void *clickData);
void exprEdSetValidationValue(int valRef, SA_PARAM_NN_STR const char *text);

void exprEdInit(void);
bool exprEdIsInitialized(void);
#endif // NO_EDITORS

UIWindow *exprEdOpen(ExprEdExprFunc exprFunc, Expression *expr, UserData data, ExprContext *context, int singleLineIdx);

#endif // __EXPRESSIONEDITOR_H__