#pragma once
GCC_SYSTEM

#include "applocale.h"

//////////////////////////////////////////////////////////////////////////
// Support for formatting messages within expressions, as well as formatting
// messages based on expression contexts. The variables in the message are
// replaced by the values from the expression context.

//#define MESSAGE_EXPR_TAG "Message"

typedef struct ExprContext ExprContext;

// Format pchFormat directly from the context using the given (or default) language.
void exprLangFormat(unsigned char **ppchResult, const char *pchFormat, ExprContext *pContext, Language langID, const char *pchFilename);
void exprFormat(unsigned char **ppchResult, const char *pchFormat, ExprContext *pContext, const char *pchFilename);

// Translate pchKey into the given (or default) language and format it.
void exprLangTranslate(unsigned char **ppchResult, ExprContext *pContext, Language langID, const char *pchKey, const char *pchFilename);
void exprTranslate(unsigned char **ppchResult, ExprContext *pContext, const char *pchKey, const char *pchFilename);

