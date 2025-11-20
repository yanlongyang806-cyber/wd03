#pragma once
GCC_SYSTEM

typedef struct Expression	Expression;
typedef struct MultiVal		MultiVal;

int exprTokenizeEx(SA_PARAM_NN_STR const char *str, MultiVal ***result, const char ***locs, int showErrors, SA_PARAM_OP_VALID Expression* expr);
int exprTokenize(const char* str, MultiVal*** result, Expression* expr);

