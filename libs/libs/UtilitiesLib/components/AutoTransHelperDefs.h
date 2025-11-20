#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/


#include "stdtypes.h"
#include "transactionoutcomes.h"
#include "textparser.h"
#include "earray.h"

//if our AUTO_TRANS func includes "otherFunc(pContainer->x, pContainer->foo->y.z)",
//we'll get two potentialHelperFuncCalls, {"otherFunc", 0, "x"} and {"otherFunc", 1, "foo->y.z"}
typedef struct
{
	char *pFuncName;
	int iArgNum;
	char *pCurField;

	//where it is defined, for debugging purposes
	char *pFileName;
	int iLineNum;
} ATRPotentialHelperFuncCall;


typedef struct
{
	char *pFieldName;
	char *pFileName;
	int iLineNum;
} ATRStaticHelperField;

typedef struct
{
	char *pFieldName;
	char *pSourceString; //file names and line nums and func calls assembled
} ATRDynamicHelperField;

typedef struct 
{
	int iArgNum; 
	ATRStaticHelperField *pStaticFieldsToLock; //terminated by NULL pFieldName
	ATRDynamicHelperField **ppDynamicFieldsToLock; //earray
	ATRPotentialHelperFuncCall *pPotentialHelperFuncCalls; //terminated by NULL pFuncName
	int iRecursePreventionKey;
} ATRHelperArg;

typedef struct
{
	char *pFuncName;
	ATRHelperArg *pArgs; //terminated by iArgNum = -1
} ATRHelperFunc;

void RegisterATRHelperFunc(ATRHelperFunc *pHelperFunc);
