#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef struct ExprContext ExprContext;

// Fetches the static context used for generating and evaluating expressions in PowerTreeRespecConfig
ExprContext *powerTreeEval_GetContextRespec(void);
