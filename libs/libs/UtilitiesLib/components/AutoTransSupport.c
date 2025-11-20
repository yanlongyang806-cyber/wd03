
/***************************************************************************



***************************************************************************/

#include "autotransdefs.h"

#include "estring.h"
#include "AutoTransDefs_h_ast.h"
#include "ResourceInfo.h"
#include "AutoTransSupport.h"
#include "AutoTransSupport_c_ast.h"
#include "Error.h"
#include "timing.h"
#include "ContinuousBuilderSupport.h"
#include "file.h"
#include "stringutil.h"
#include "alerts.h"
#include "objPath.h"
#include "Stringcache.h"
#include "qsortG.h"
#include "globalTypes.h"
#include "logging.h"
#include "TransactionOutcomes.h"
#include "windefinclude.h"

//if true, then error when someone tries to lock a full container arg without ATR_ALLOW_FULL_LOCK
bool gbErrorOnFullLocks = true;
AUTO_CMD_INT(gbErrorOnFullLocks, ErrorOnFullLocks);

//if true, then error when a transaction does not have ATR_LOCKS defined
bool gbErrorOnNoAtrLocks = true;
AUTO_CMD_INT(gbErrorOnNoAtrLocks, ErrorOnNoAtrLocks);

static void CheckSuspiciousFuncCalls(char *pAutoTransFuncName, char *pFuncNames);

#define NO_COMMENTS_IN_PRODUCTION_MODE "(No comments in production mode)"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

// Debug values
static char **eaFullLockFuncs = NULL;
static char **eaNoAtrLockFuncs = NULL;

#define MAX_AUTOTRANSFUNC_RECURSEDEPTH 32

typedef struct
{
	union
	{
		void *pPtr;
		int iInt;
		float fFloat;
		S64 iInt64;
	};
} ATRSingleArgStruct;

ATRSingleArgStruct sATRArgs[MAX_ARGS_SINGLE_ATR_FUNC];

int GetATRArg_Int(int iIndex)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	return sATRArgs[iIndex].iInt;
}

int *GetATRArg_IntPtr(int iIndex)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	return (int*)sATRArgs[iIndex].pPtr;
}

S64 GetATRArg_Int64(int iIndex)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	return sATRArgs[iIndex].iInt64;
}
S64 *GetATRArg_Int64Ptr(int iIndex)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	return (S64*)sATRArgs[iIndex].pPtr;
}

float GetATRArg_Float(int iIndex)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	return sATRArgs[iIndex].fFloat;
}

float *GetATRArg_FloatPtr(int iIndex)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	return (float*)sATRArgs[iIndex].pPtr;
}

char *GetATRArg_String(int iIndex)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	return (char*)sATRArgs[iIndex].pPtr;
}

void *GetATRArg_Container(int iIndex)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	return (void*)sATRArgs[iIndex].pPtr;
}

void *GetATRArg_ContainerEArray(int iIndex)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	return (void*)sATRArgs[iIndex].pPtr;
}

void *GetATRArg_Struct(int iIndex)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	return (void*)sATRArgs[iIndex].pPtr;
}

void SetATRArg_Int(int iIndex, int iVal)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	sATRArgs[iIndex].iInt = iVal;
}
void SetATRArg_IntPtr(int iIndex, int *piVal)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	sATRArgs[iIndex].pPtr = piVal;
}

void SetATRArg_Int64(int iIndex, S64 iVal)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	sATRArgs[iIndex].iInt64 = iVal;
}

void SetATRArg_Int64Ptr(int iIndex, S64 *piVal)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	sATRArgs[iIndex].pPtr = piVal;
}

void SetATRArg_Float(int iIndex, float fVal)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	sATRArgs[iIndex].fFloat = fVal;
}
void SetATRArg_FloatPtr(int iIndex, float *pfVal)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	sATRArgs[iIndex].pPtr = pfVal;
}

void SetATRArg_String(int iIndex, char *pString)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	sATRArgs[iIndex].pPtr = pString;
}
void SetATRArg_Container(int iIndex, void *pContainer)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	sATRArgs[iIndex].pPtr = pContainer;
}

void SetATRArg_ContainerEArray(int iIndex, void *ppContainerEArray)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	sATRArgs[iIndex].pPtr = ppContainerEArray;
}

void SetATRArg_Struct(int iIndex, void *pStruct)
{
	assert(iIndex >= 0 && iIndex < MAX_ARGS_SINGLE_ATR_FUNC);

	sATRArgs[iIndex].pPtr = pStruct;
}


StashTable ATRFuncDefTable = NULL;
StashTable ATRSimpleHelperFuncsByName = NULL;

void RegisterATRFuncDef(ATR_FuncDef *pFuncDef)
{
	if (!ATRFuncDefTable)
	{
		ATRFuncDefTable = stashTableCreateWithStringKeys(16, StashDefault);
	}

	if (stashFindPointer(ATRFuncDefTable, pFuncDef->pFuncName, NULL))
	{
		assertmsgf(0, "ATR func name collision... found two functions named %s", 
			pFuncDef->pFuncName);
	}

	stashAddPointer(ATRFuncDefTable, pFuncDef->pFuncName, pFuncDef, false);
}

ATR_FuncDef *FindATRFuncDef(const char *name)
{
	ATR_FuncDef *pRetVal;

	if (!ATRFuncDefTable)
	{
		return NULL;
	}

	if (stashFindPointer(ATRFuncDefTable, name, &pRetVal))
	{
		return pRetVal;
	}

	return NULL;
}


void RegisterSimpleATRHelper(char *pName, char *pFuncsCalled)
{
	if (isProductionMode())
	{
		return;
	}

	if (!ATRSimpleHelperFuncsByName)
	{
		ATRSimpleHelperFuncsByName = stashTableCreateWithStringKeys(16, StashDefault);
	}

	if (stashFindPointer(ATRSimpleHelperFuncsByName, pName, NULL))
	{
		assertmsgf(0, "ATR func name collision for simple helpers... found two functions named %s", 
			pName);
	}

	stashAddPointer(ATRSimpleHelperFuncsByName, pName, pFuncsCalled, false);
}


#if 0
StashTable HelperFuncTable = NULL;

void RegisterATRHelperFunc(ATRHelperFunc *pHelperFunc)
{
	if (!HelperFuncTable)
	{
		HelperFuncTable = stashTableCreateWithStringKeys(16, StashDefault);
	}

	stashAddPointer(HelperFuncTable, pHelperFunc->pFuncName, pHelperFunc, false);
}

ATRHelperFunc *FindHelperFuncByName(char *pFuncName)
{
	void *pFunc;

	if (!HelperFuncTable)
	{
		return NULL;
	}

	if (stashFindPointer(HelperFuncTable, pFuncName, &pFunc))
	{
		return pFunc;
	}

	return NULL;
}

void AddDynamicFieldsToATRHelperArg(ATRHelperArg *pArg, char *pNewFieldString, char *pSourceString)
{
	int i;
	ATRDynamicHelperField *pNewField;

	if (!pNewFieldString[0])
	{
		pNewFieldString = ".*";
	}

	if (pArg->pStaticFieldsToLock)
	{
		ATRStaticHelperField *pStaticField = pArg->pStaticFieldsToLock;

		while (pStaticField->pFieldName)
		{
			if (stricmp(pStaticField->pFieldName, pNewFieldString) == 0)
			{
				return;
			}

			pStaticField++;
		}
	}

	for (i=0; i < eaSize(&pArg->ppDynamicFieldsToLock); i++)
	{
		if (stricmp(pArg->ppDynamicFieldsToLock[i]->pFieldName, pNewFieldString) == 0)
		{
			return;
		}
	}


	pNewField = calloc(sizeof(ATRDynamicHelperField), 1);
	pNewField->pFieldName = strdup(pNewFieldString);
	pNewField->pSourceString = strdup(pSourceString);

	eaPush(&pArg->ppDynamicFieldsToLock, pNewField);
}

void RecursivelyFixUpHelperFunc(ATRHelperFunc *pHelperFunc, int iKey, bool bMarkCompleteWhenDone)
{
	ATRHelperArg *pArg = pHelperFunc->pArgs;

	while (pArg->iArgNum != -1)
	{
		if (!(pArg->iRecursePreventionKey == RECURSION_COMPLETED || pArg->iRecursePreventionKey == iKey))
		{
			pArg->iRecursePreventionKey = iKey;

			if (pArg->pPotentialHelperFuncCalls)
			{
				ATRPotentialHelperFuncCall *pCall = pArg->pPotentialHelperFuncCalls;

				while (pCall->pFuncName)
				{
					bool bFoundOtherFuncAndProperArg = false;
					ATRHelperFunc *pOtherFunc = FindHelperFuncByName(pCall->pFuncName);

					if (pOtherFunc)
					{
						ATRHelperArg *pOtherArg;

						RecursivelyFixUpHelperFunc(pOtherFunc, iKey, false);

						pOtherArg = pOtherFunc->pArgs;

						while (pOtherArg->iArgNum != -1 && pOtherArg->iArgNum != pCall->iArgNum)
						{
							pOtherArg++;
						}

						if (pOtherArg->iArgNum == pCall->iArgNum)
						{
							int i;
							ATRStaticHelperField *pStaticField = pOtherArg->pStaticFieldsToLock;

							bFoundOtherFuncAndProperArg = true;

							//now add all static and dynamic fields-to-lock from otherArg to current arg

							if (pStaticField)
							{
								while (pStaticField->pFieldName)
								{
									char tempFields[512];
									char tempSource[1024];

									sprintf(tempFields, "%s%s", pCall->pCurField, pStaticField->pFieldName);
									sprintf(tempSource, "Helper function %s, %s(%d). Then %s(%d)",
										pCall->pFuncName, pCall->pFileName, pCall->iLineNum, 
										pStaticField->pFileName, pStaticField->iLineNum);


									AddDynamicFieldsToATRHelperArg(pArg, tempFields, tempSource);

									pStaticField++;
								}
							}
							

							for (i=0; i < eaSize(&pOtherArg->ppDynamicFieldsToLock); i++)
							{
								char tempFields[512];
								char tempSource[1024];

								sprintf(tempFields, "%s%s", pCall->pCurField, pOtherArg->ppDynamicFieldsToLock[i]->pFieldName);
								sprintf(tempSource, "Helper function %s, %s(%d). Then %s",
									pCall->pFuncName, pCall->pFileName, pCall->iLineNum, pOtherArg->ppDynamicFieldsToLock[i]->pSourceString);


								AddDynamicFieldsToATRHelperArg(pArg, tempFields, tempSource);
							}
						}
					}

					if (!bFoundOtherFuncAndProperArg)
					{
						//the other function is NOT actually a helper function, or if it is, 
						//not with the relevant argument number
						char sourceString[1024];

						sprintf(sourceString, "Call to non-helper function %s, %s(%d)",
							pCall->pFuncName, pCall->pFileName, pCall->iLineNum);

						AddDynamicFieldsToATRHelperArg(pArg, pCall->pCurField, sourceString);
					}

					pCall++;
				}
			}
		}

		if (bMarkCompleteWhenDone)
		{
			pArg->iRecursePreventionKey = RECURSION_COMPLETED;
		}

		pArg++;
	}
}


char *SkipDotOrArrow(char *pIn)
{
	if (pIn[0] == '.')
	{
		return pIn+1;
	}

	if (pIn[0] == '-' && pIn[1] == '>')
	{
		return pIn + 2;
	}

	return pIn;
}

bool FieldToLocksAreEqual(ATRFieldToLock *pField1, ATRFieldToLock *pField2)
{
	char *pName1 = SkipDotOrArrow(pField1->pName);
	char *pName2 = SkipDotOrArrow(pField2->pName);

	if (strcmp(pName1, pName2) != 0)
	{
		return false;
	}

	if (pField1->eFieldType != pField2->eFieldType)
	{
		return false;
	}

	if (pField1->eLockType != pField2->eLockType)
	{
		return false;
	}

	if (pField1->iIndexNum != pField2->iIndexNum)
	{
		return false;
	}

	if (pField1->pIndexString)
	{
		if (!pField2->pIndexString)
		{
			return false;
		}

		if (strcmp(pField1->pIndexString, pField2->pIndexString) != 0)
		{
			return false;
		}
	}
	else
	{
		if (pField2->pIndexString)
		{
			return false;
		}
	}

	return true;
}

bool AddFieldToLockIfUnique(ATRArgDef *pArg, ATRFieldToLock *pNewField, char *pNewOriginString)
{
	int i;
	ATRFieldToLock *pCurField;


	if (pArg->pStaticFieldsToLock)
	{
		pCurField = pArg->pStaticFieldsToLock;

		while (pCurField->pName)
		{
			if (FieldToLocksAreEqual(pCurField, pNewField))
			{
				return false;
			}

			pCurField++;
		}
	}

	for (i=0; i < eaSize(&pArg->ppRunTimeFieldsToLock); i++)
	{
		if (FieldToLocksAreEqual(pArg->ppRunTimeFieldsToLock[i], pNewField))
		{
			return false;
		}
	}

	pCurField = calloc(sizeof(ATRFieldToLock), 1);
	memcpy(pCurField, pNewField, sizeof(ATRFieldToLock));
	eaPush(&pArg->ppRunTimeFieldsToLock, pCurField);
	pCurField->pOriginString = strdup(pNewOriginString);

	return true;

}

void ATRArg_AddSimpleFieldToLockIfUnique(ATRArgDef *pArg, char *pFieldString, char *pOriginString)
{
	ATRFieldToLock simpleField;
	int iLen;

	if (!pFieldString[0])
	{
		pFieldString = ".*";
	}

	iLen = (int) strlen(pFieldString);

	//".*" is a special case, and can only be by itself. "foo.*" should just be "foo"
	if (strEndsWith(pFieldString, ".*") && iLen > 2)
	{
		simpleField.pName = calloc(iLen - 1, 1);
		memcpy(simpleField.pName, pFieldString, iLen - 2);
	}
	else
	{
		simpleField.pName = strdup(pFieldString);
	}
	simpleField.eFieldType = ATR_FIELD_NORMAL;
	simpleField.eLockType = ATR_LOCK_NORMAL;
	simpleField.iIndexNum = 0;
	simpleField.pIndexString = NULL;

	if (!AddFieldToLockIfUnique(pArg, &simpleField, pOriginString))
	{
		free(simpleField.pName);
	}
}




void DoAutoRunArgFuncRecursion(ATR_FuncDef *pFunc, ATRArgDef *pArg, ATRRecursingFuncCall *pRecurseFunc, int iRecurseKey)
{
	ATR_FuncDef *pOtherFunc = FindATRFuncDef(pRecurseFunc->pFuncName);
	ATRArgDef *pOtherArg;
	ATRFieldToLock *pNewField;
	int i;

	if (!pOtherFunc)
	{
		assertmsgf("DoAutoRunArgFuncRecursion - Func %s references unknown func %s", pFunc->pFuncName, pRecurseFunc->pFuncName);
		return;
	}

	pOtherArg = &pOtherFunc->pArgs[pRecurseFunc->iArgNum];

	if (pOtherArg->iRecursePreventionKey == iRecurseKey)
	{
		return;
	}

	if (pOtherArg->iRecursePreventionKey != RECURSION_COMPLETED)
	{
		pOtherArg->iRecursePreventionKey = iRecurseKey;

		if (pOtherArg->pRecursingFuncCalls)
		{
			ATRRecursingFuncCall *pOtherRecurseFunc = pOtherArg->pRecursingFuncCalls;

			while (pOtherRecurseFunc->pFuncName)
			{
				DoAutoRunArgFuncRecursion(pOtherFunc, pOtherArg, pOtherRecurseFunc, iRecurseKey);

				pOtherRecurseFunc++;
			}
		}
	}

	if (pOtherArg->pStaticFieldsToLock)
	{
		pNewField = pOtherArg->pStaticFieldsToLock;

		while (pNewField->pName)
		{
			char newOriginString[1024];
			sprintf(newOriginString, "Recurse Func %s (%s(%d)). Then %s", 
				pRecurseFunc->pFuncName, pRecurseFunc->pFileName, pRecurseFunc->iLineNum, pNewField->pOriginString);
			AddFieldToLockIfUnique(pArg, pNewField, newOriginString);
			pNewField++;
		}
	}

	for (i=0; i < eaSize(&pOtherArg->ppRunTimeFieldsToLock); i++)
	{
		char newOriginString[1024];
		sprintf(newOriginString, "Recurse Func %s (%s(%d)). Then %s", 
			pRecurseFunc->pFuncName, pRecurseFunc->pFileName, pRecurseFunc->iLineNum, pOtherArg->ppRunTimeFieldsToLock[i]->pOriginString);
		AddFieldToLockIfUnique(pArg, pOtherArg->ppRunTimeFieldsToLock[i], newOriginString);
	}
}

void ApplyHelperFunctions(ATR_FuncDef *pFuncDef)
{
	ATRArgDef *pArg = pFuncDef->pArgs;

	while (pArg->eArgType != ATR_ARG_NONE)
	{
		if (pArg->pPotentialHelperFuncCalls)
		{
			ATRPotentialHelperFuncCall *pCall = pArg->pPotentialHelperFuncCalls;

			while (pCall->pFuncName)
			{
				bool bActuallyFoundHelperFunc = false;
				ATRHelperFunc *pHelperFunc = FindHelperFuncByName(pCall->pFuncName);

				if (pHelperFunc)
				{
					ATRHelperArg *pHelperArg = pHelperFunc->pArgs;

					while (pHelperArg->iArgNum != -1 && pHelperArg->iArgNum != pCall->iArgNum)
					{
						pHelperArg++;
					}

					if (pHelperArg->iArgNum == pCall->iArgNum)
					{
						int i;

						bActuallyFoundHelperFunc = true;

						if (pHelperArg->pStaticFieldsToLock)
						{
							ATRStaticHelperField *pStaticField = pHelperArg->pStaticFieldsToLock;

							while (pStaticField->pFieldName)
							{
								char curField[512];
								char newOriginString[1024];
								sprintf(newOriginString, "Helper func %s (%s(%d)). Then %s(%d)",
									pCall->pFuncName, pCall->pFileName, pCall->iLineNum, pStaticField->pFileName, pStaticField->iLineNum);

								sprintf(curField, "%s%s", pCall->pCurField, pStaticField->pFieldName);

								ATRArg_AddSimpleFieldToLockIfUnique(pArg, curField, newOriginString);

								pStaticField++;
							}
						}

						for (i=0; i < eaSize(&pHelperArg->ppDynamicFieldsToLock); i++)
						{
							char curField[512];
							char newOriginString[1024];
							sprintf(curField, "%s%s", pCall->pCurField, pHelperArg->ppDynamicFieldsToLock[i]->pFieldName);
							sprintf(newOriginString, "Helper func %s (%s(%d)). Then %s",
								pCall->pFuncName, pCall->pFileName, pCall->iLineNum, pHelperArg->ppDynamicFieldsToLock[i]->pSourceString);

							ATRArg_AddSimpleFieldToLockIfUnique(pArg, curField, newOriginString);
						}
					}
				}
				
				if (!bActuallyFoundHelperFunc)
				{
					char newOriginString[1024];
					sprintf(newOriginString, "%s(%d)", pCall->pFileName, pCall->iLineNum);
					//this is not actually a helper func... just add the fields to the dynamic list
					ATRArg_AddSimpleFieldToLockIfUnique(pArg, pCall->pCurField, newOriginString);
				}

				pCall++;
			}
		}
		pArg++;
	}
}

AUTO_RUN_LATE;
void FixupAutoRunFuncRecursion(void)
{
	StashTableIterator iterator;
	StashElement element;
	int iCurKey = RECURSION_COMPLETED + 1;

	if (HelperFuncTable)
	{
		stashGetIterator(HelperFuncTable, &iterator);

		while (stashGetNextElement(&iterator, &element))
		{
			ATRHelperFunc *pHelperFunc = stashElementGetPointer(element);

			RecursivelyFixUpHelperFunc(pHelperFunc, iCurKey, true);
		}
	}

	if (!FuncDefTable)
	{
		return;
	}

	iCurKey = RECURSION_COMPLETED + 1;

	stashGetIterator(FuncDefTable, &iterator);

	while (stashGetNextElement(&iterator, &element))
	{
		ATR_FuncDef *pFuncDef = stashElementGetPointer(element);
		
		if (pFuncDef->pArgs)
		{
			ATRArgDef *pArg;

			ApplyHelperFunctions(pFuncDef);

			pArg = pFuncDef->pArgs;

			while (pArg->eArgType != ATR_ARG_NONE)
			{
				if (pArg->pRecursingFuncCalls)
				{
					ATRRecursingFuncCall *pRecursingFuncCall = pArg->pRecursingFuncCalls;
					pArg->iRecursePreventionKey = ++iCurKey;

					while (pRecursingFuncCall->pFuncName)
					{
						DoAutoRunArgFuncRecursion(pFuncDef, pArg, pRecursingFuncCall, iCurKey);
						
						pRecursingFuncCall++;
					}

					pArg->iRecursePreventionKey = RECURSION_COMPLETED;

				}

				pArg++;
			}
		}
	}
}

void DumpUsedFieldsForFuncDef(ATR_FuncDef *pFunc)
{
	ATRArgDef *pArg;

	printf("***********************\nDumping all fields for AUTO_TRANSACTION %s\n\n", pFunc->pFuncName);

	assert(pFunc->pArgs);

	pArg = pFunc->pArgs;

	while (pArg->eArgType != ATR_ARG_NONE)
	{
		if (pArg->eArgType == ATR_ARG_CONTAINER || pArg->eArgType == ATR_ARG_CONTAINER_EARRAY)
		{
			ATRFieldToLock *pField;
			int i;

			printf("Locking fields for argument %s (TPI %s):\n--------------------------\n",
				pArg->pArgName, ParserGetTableName(pArg->pParseTable));

			if (pArg->pStaticFieldsToLock)
			{
				pField = pArg->pStaticFieldsToLock;

				while (pField->pName != NULL)
				{
					char *pLockTypeString = "NORMAL LOCK";
					switch (pField->eLockType)
					{
					case ATR_LOCK_ARRAY_OPS:
						pLockTypeString = "ARRAYOPS";
						break;
					case ATR_LOCK_INDEXED_NULLISOK:
						pLockTypeString = "NULLISOK";
						break;
					case ATR_LOCK_INDEXED_FAILONNULL:
						pLockTypeString = "FAILONNULL";
						break;
					}

					switch (pField->eFieldType)
					{
					xcase ATR_FIELD_NORMAL:
						printf("%s(%s)", pField->pName, pLockTypeString);
					xcase ATR_FIELD_INDEXED_LITERAL_INT:
						printf("%s[%d](%s)", pField->pName, pField->iIndexNum, pLockTypeString);
					xcase ATR_FIELD_INDEXED_LITERAL_STRING:
						printf("%s[%s](%s)", pField->pName, pField->pIndexString, pLockTypeString);
					xcase ATR_FIELD_INDEXED_INT_ARG:
						printf("%s[INT ARG %d](%s)", pField->pName, pField->iIndexNum, pLockTypeString);
					xcase ATR_FIELD_INDEXED_STRING_ARG:
						printf("%s[STRING ARG %d](%s)", pField->pName, pField->iIndexNum, pLockTypeString);
					}

					printf("     %s\n", pField->pOriginString);

					pField++;
				}
			}

			for (i=0; i < eaSize(&pArg->ppRunTimeFieldsToLock); i++)
			{
				printf("%s     %s\n", pArg->ppRunTimeFieldsToLock[i]->pName, pArg->ppRunTimeFieldsToLock[i]->pOriginString);
			}

			printf("------------------------\n\n");
		}

		pArg++;
	}
}


AUTO_COMMAND ACMD_SERVERONLY;
void DumpAutoTransUsedFields(char *pAutoTransFuncName ACMD_NAMELIST(FuncDefTable, STASHTABLE))
{
	ATR_FuncDef *pFunc = FindATRFuncDef(pAutoTransFuncName);

	if (!pFunc)
	{
		printf("AUTO_TRANSACTION %s does not exist.\n", pAutoTransFuncName);
		return;
	}

	DumpUsedFieldsForFuncDef(pFunc);
}

AUTO_COMMAND ACMD_SERVERONLY;
void DumpUsedFieldsForAllAutoTrans(void)
{
	StashTableIterator iterator;
	StashElement element;
	
	if (!FuncDefTable)
	{
		return;
	}

	stashGetIterator(FuncDefTable, &iterator);

	while (stashGetNextElement(&iterator, &element))
	{
		ATR_FuncDef *pFuncDef = stashElementGetPointer(element);
		DumpUsedFieldsForFuncDef(pFuncDef);
	}
}

AUTO_COMMAND ACMD_SERVERONLY;
void DumpATRHelperUsedFields(char *pFuncName ACMD_NAMELIST(HelperFuncTable, STASHTABLE))
{
	ATRHelperFunc *pHelperFunc = FindHelperFuncByName(pFuncName);
	int i;
	int j;

	if (!pHelperFunc)
	{
		printf("nonexistent function %s\n", pFuncName);
		return;
	}

	if (!pHelperFunc->pArgs)
	{
		printf("Helper func %s has no args. That is weird\n", pFuncName);
		return;
	}

	printf("***********************\nDumping all fields for AutoTrans helper function %s\n\n", pFuncName);

	for (i=0; pHelperFunc->pArgs[i].iArgNum != -1; i++)
	{
		ATRHelperArg *pArg = &pHelperFunc->pArgs[i];

		printf("Locking fields for argument %d:\n--------------------------\n",
			pArg->iArgNum);

		if (pArg->pStaticFieldsToLock)
		{
			ATRStaticHelperField *pStaticField = pArg->pStaticFieldsToLock;

			while (pStaticField->pFieldName)
			{
				printf("%s (%s(%d))\n", pStaticField->pFieldName, pStaticField->pFileName, pStaticField->iLineNum);
				pStaticField++;
			}
		}

		for (j=0; j < eaSize(&pArg->ppDynamicFieldsToLock); j++)
		{
			printf("%s (%s)\n", pArg->ppDynamicFieldsToLock[j]->pFieldName, pArg->ppDynamicFieldsToLock[j]->pSourceString);
		}
	
		printf("------------------------\n\n");
	}
}

#endif


ATRContainerArgDef *FindContainerArgDefByIndex(ATR_FuncDef *pFunc, int iContainerArgIndex)
{
	int i;

	if (pFunc->pContainerArgs)
	{
		for (i=0; pFunc->pContainerArgs[i].pArgName; i++)
		{
			if (pFunc->pContainerArgs[i].iArgIndex == iContainerArgIndex)
			{
				return &pFunc->pContainerArgs[i];
			}
		}
	}

	return NULL;
}


static ATRSimpleArgDef *FindSimpleArgDefByIndex(ATR_FuncDef *pFunc, int iSimpleArgIndex)
{
	int i;

	if (pFunc->pSimpleArgs)
	{
		for (i=0; pFunc->pSimpleArgs[i].pArgName; i++)
		{
			if (pFunc->pSimpleArgs[i].iArgIndex == iSimpleArgIndex)
			{
				return &pFunc->pSimpleArgs[i];
			}
		}
	}

	return NULL;
}

ATRFuncCallSimpleArg *FindFuncCallSimpleArgByIndex(ATRFunctionCallDef *pFuncCall, int iIndex)
{
	int i;
	if (pFuncCall->pSimpleArgs)
	{
		for (i=0; pFuncCall->pSimpleArgs[i].iArgIndex != -1; i++)
		{
			if (pFuncCall->pSimpleArgs[i].iArgIndex == iIndex)
			{
				return &pFuncCall->pSimpleArgs[i];
			}
		}
	}

	return NULL;
}

bool LockStringIsPrefix(char *pShorter, char *pLonger)
{
	int iShortLen = (int)strlen(pShorter);
	int iLongLen = (int)strlen(pLonger);

	if (iShortLen == 0)
	{
		return true;
	}

	if (iShortLen > iLongLen)
	{
		return false;
	}

	if (iShortLen == iLongLen && strcmp(pLonger, pShorter) == 0)
	{
		return true;
	}

	if (strncmp(pLonger, pShorter, iShortLen) != 0)
	{
		return false;
	}

/*now we know that long is longer than short and all the characters they share match. But we need to distinguish between
these two cases: 
foo.bar
foo.bar.wakka
(prefixes)

and

foo.bar
foo.barter
(not prefixes)
*/

	if (isalnumorunderscore(pLonger[iShortLen]) && isalnumorunderscore(pLonger[iShortLen-1]))
	{
		return false;
	}

	return true;
}

static bool FirstLockIncludesSecondLock(ATRFixedUpLock *pLock1, ATRFixedUpLock *pLock2)
{

	if (pLock1->pDerefString[0] == 0 && pLock1->eLockType == ATR_LOCK_NORMAL)
	{
		return true;
	}

	if (strcmp(pLock1->pDerefString, pLock2->pDerefString) == 0)
	{
		if (pLock1->eLockType == pLock2->eLockType)
		{
			return true;
		}

		if (pLock1->eLockType == ATR_LOCK_NORMAL)
		{
			return true;
		}
	}

	if (pLock1->eLockType == ATR_LOCK_NORMAL && (pLock2->eLockType == ATR_LOCK_NORMAL || pLock2->eLockType == ATR_LOCK_ARRAY_OPS) && LockStringIsPrefix(pLock1->pDerefString, pLock2->pDerefString))
	{
		return true;
	}

	return false;
}


static bool FirstLockIncludesEarrayUse(ATRFixedUpLock *pLock1, ATRFixedUpEarrayUse *pLock2)
{

	if (pLock1->pDerefString[0] == 0 && pLock1->eLockType == ATR_LOCK_NORMAL)
	{
		return true;
	}

	if (strcmp(pLock1->pDerefString, pLock2->pContainerDerefString) == 0)
	{
		if (pLock1->eLockType == ATR_LOCK_NORMAL)
		{
			return true;
		}
	}


	if (pLock1->eLockType == ATR_LOCK_NORMAL && LockStringIsPrefix(pLock1->pDerefString, pLock2->pContainerDerefString))
	{
		return true;
	}

	return false;
}


//if there is an [] somewhere in the deref string other than at the beginning, then 
//we can't process it so we just lock the whole array
static void FixupDerefString(char **ppOutEStr, const char *pInStr)
{
	char *pFirstBracketNotAtBeginning;
	while (pInStr[0] == '.')
	{
		pInStr++;
	}
	estrCopy2(ppOutEStr, pInStr);
	estrReplaceOccurrences(ppOutEStr, "->", ".");
	
	pFirstBracketNotAtBeginning = estrLength(ppOutEStr) ? strchr((*ppOutEStr)+1, '[') : NULL;
	if (pFirstBracketNotAtBeginning)
	{
		estrSetSize(ppOutEStr, pFirstBracketNotAtBeginning - (*ppOutEStr));
	}
}

//returns true if something changed
static bool AddLock(ATR_FuncDef *pParentFunc, ATRContainerArgDef *pContainerArg, const char *pDerefString, enumLockType eLockType,
	 char *pComment)
{
	ATRFixedUpLock lock = {0};
	int i;

	ATRFixedUpLock *pNewLock;
	int iNumCurLocks;
	int iCurNumEarrayUses;

	assert(pContainerArg);

	iNumCurLocks = eaSize(&pContainerArg->ppLocks);


	FixupDerefString(&lock.pDerefString, pDerefString);
	lock.eLockType = eLockType;

	for (i=0; i < iNumCurLocks; i++)
	{
		if (FirstLockIncludesSecondLock(pContainerArg->ppLocks[i], &lock))
		{
			estrDestroy(&lock.pDerefString);
			return false;
		}
	}

	pNewLock = StructCreate(parse_ATRFixedUpLock);
	pNewLock->eLockType = eLockType;
	
	if (isDevelopmentMode())
	{
		pNewLock->pComment = strdup(pComment);
	}
	else
	{
		//this is invalid string memory use... but fixed in destructor
		pNewLock->pComment = NO_COMMENTS_IN_PRODUCTION_MODE;
	}

	estrCopy2(&pNewLock->pDerefString,lock.pDerefString);
	estrDestroy(&lock.pDerefString);

	for (i=iNumCurLocks-1; i >= 0; i--)
	{
		if (FirstLockIncludesSecondLock(pNewLock, pContainerArg->ppLocks[i]))
		{
			StructDestroy(parse_ATRFixedUpLock, pContainerArg->ppLocks[i]);
			eaRemoveFast(&pContainerArg->ppLocks, i);
		}
	}

	iCurNumEarrayUses = eaSize(&pContainerArg->ppEarrayUses);
	for (i=iCurNumEarrayUses-1; i >= 0; i--)
	{
		if (FirstLockIncludesEarrayUse(pNewLock, pContainerArg->ppEarrayUses[i]))
		{
			StructDestroy(parse_ATRFixedUpEarrayUse, pContainerArg->ppEarrayUses[i]);
			eaRemoveFast(&pContainerArg->ppEarrayUses, i);
		}
	}

	eaPush(&pContainerArg->ppLocks, pNewLock);

	return true;
}



//returns true if something changed
bool AddLockf(ATR_FuncDef *pParentFunc, ATRContainerArgDef *pContainerArg, char *pDerefString, enumLockType eLockType,
	 char *pComment, ...)
{
	char comment[10000];
	va_list ap;

	comment[0] = 0;

	va_start(ap, pComment);
	if (pComment)
	{
		vsprintf(comment, pComment, ap);
	}
	va_end(ap);

	return AddLock(pParentFunc, pContainerArg, pDerefString, eLockType, comment);
}

StashTable AutoTransNonHelperTable;

static void AddEverythingFromUnknownFuncCall(ATR_FuncDef *pParent, ATRFunctionCallDef *pFuncCallDef)
{
	if (pFuncCallDef->pContainerArgs)
	{
		int i;
		for (i=0; pFuncCallDef->pContainerArgs[i].iParentArgIndex != -1; i++)
		{
			ATRFuncCallContainerArg *pArg = &pFuncCallDef->pContainerArgs[i];
			ATRContainerArgDef *pArgFromParent = FindContainerArgDefByIndex(pParent, pArg->iParentArgIndex);
			assert(pArgFromParent);
			

			AddLockf(pParent, pArgFromParent,
				pArg->pDerefString, ATR_LOCK_NORMAL, "%s.%s passed to non-helper-or-ATR function %s on line %d of %s",
				pArgFromParent->pArgName, pArg->pDerefString[0] ? pArg->pDerefString : "*", pFuncCallDef->pFuncName, pFuncCallDef->iLineNum, pParent->pFileName);

			stashAddPointer(AutoTransNonHelperTable, pFuncCallDef->pFuncName, pFuncCallDef, false);
		}
	}
}

AUTO_COMMAND;
void printAutoTransNonHelpers(void)
{
	StashTableIterator iter;
	StashElement elem;

	stashGetIterator(AutoTransNonHelperTable, &iter);

	while(stashGetNextElement(&iter, &elem))
	{
		ATRFunctionCallDef *pFuncCallDef = stashElementGetPointer(elem);
		printf("%s\n", pFuncCallDef->pFuncName);
	}
}

//returns true if something changed
static bool AddEarrayUse(ATR_FuncDef *pParent, ATRContainerArgDef *pArg, char *pDerefString,
	 enumLockType eLockType, enumEarrayIndexType eIndexType, 
	 int iVal, char *pSVal, char *pCommentString)
{
	int iCurNumEarrayUses = eaSize(&pArg->ppEarrayUses);
	int	iNumCurLocks = eaSize(&pArg->ppLocks);
	int i;
	ATRFixedUpEarrayUse *pNewUse = StructCreate(parse_ATRFixedUpEarrayUse);

	FixupDerefString(&pNewUse->pContainerDerefString, pDerefString);
	pNewUse->eLockType = eLockType;
	pNewUse->eIndexType = eIndexType;
	pNewUse->iVal = iVal;
	pNewUse->pSVal = pSVal;

	if (isDevelopmentMode())
	{
		pNewUse->pComment = strdup(pCommentString);
	}
	else
	{
		//this is invalid string memory use... but fixed in destructor
		pNewUse->pComment = NO_COMMENTS_IN_PRODUCTION_MODE;
	}

	for (i=0; i < iNumCurLocks; i++)
	{
		if (FirstLockIncludesEarrayUse(pArg->ppLocks[i], pNewUse))
		{
			StructDestroy(parse_ATRFixedUpEarrayUse, pNewUse);
			return false;
		}
	}

	for (i=0; i < iCurNumEarrayUses; i++)
	{
		if (StructCompare(parse_ATRFixedUpEarrayUse, pNewUse, pArg->ppEarrayUses[i], 0, 0,TOK_USEROPTIONBIT_1) == 0)
		{
			StructDestroy(parse_ATRFixedUpEarrayUse, pNewUse);
			return false;
		}
	}

	eaPush(&pArg->ppEarrayUses, pNewUse);
	return true;
}

//returns true if something changed
static bool AddEarrayUsef(ATR_FuncDef *pParent, ATRContainerArgDef *pArg, char *pDerefString,
	 enumLockType eLockType, enumEarrayIndexType eIndexType, 
	 int iVal, char *pSVal, char *pCommentString, ...)
{
	char comment[10000];
	va_list ap;

	comment[0] = 0;

	va_start(ap, pCommentString);
	if (pCommentString)
	{
		vsprintf(comment, pCommentString, ap);
	}
	va_end(ap);

	return AddEarrayUse(pParent, pArg, pDerefString, eLockType, eIndexType, iVal, pSVal, comment);
}


static void AddEverythingFromStaticEarrayUse(ATR_FuncDef *pParent, ATREarrayUseDef *pEarrayUseDef)
{
	char *pCommentString = NULL;
	ATRContainerArgDef *pArg = FindContainerArgDefByIndex(pParent, pEarrayUseDef->iContainerArgIndex);
	
	estrPrintf(&pCommentString, "Earray lock type %s applied to %s%s%s on line %d of %s. ",
		StaticDefineIntRevLookup(enumLockTypeEnum, pEarrayUseDef->eLockType), pArg->pArgName, 
		pEarrayUseDef->pContainerDerefString[0] ? "." : "",
		pEarrayUseDef->pContainerDerefString, pEarrayUseDef->iLineNum, pParent->pFileName);

	switch (pEarrayUseDef->eIndexType)
	{
	case ATR_INDEX_LITERAL_INT:
		estrConcatf(&pCommentString, "Indexed by literal int %d.", pEarrayUseDef->iVal);
		break;
	case ATR_INDEX_LITERAL_STRING:
		estrConcatf(&pCommentString, "Indexed by literal string \"%s\".", pEarrayUseDef->pSVal);
		break;
	case ATR_INDEX_SIMPLE_ARG:
		{
			ATRSimpleArgDef *pSimpleArg = FindSimpleArgDefByIndex(pParent, pEarrayUseDef->iVal);
			assert(pSimpleArg);
			estrConcatf(&pCommentString, "Indexed by parent's simple arg \"%s\".", pSimpleArg->pArgName);
		}
		break;
	}

	AddEarrayUse(pParent, pArg, pEarrayUseDef->pContainerDerefString, pEarrayUseDef->eLockType, pEarrayUseDef->eIndexType, pEarrayUseDef->iVal,
		pEarrayUseDef->pSVal, pCommentString);
	
	estrDestroy(&pCommentString);
}

static void AddAllStaticDereferencesForArg(ATR_FuncDef *pParent, ATRContainerArgDef *pArg)
{
	if (pArg->pStaticDerefs)
	{
		int i;
		for (i=0; pArg->pStaticDerefs[i].pDerefString; i++)
		{
			AddLockf(pParent, pArg, pArg->pStaticDerefs[i].pDerefString, pArg->pStaticDerefs[i].eLockType, 
				"%s.%s directly locked in code on line %d of %s%s",
				pArg->pArgName, pArg->pStaticDerefs[i].pDerefString[0] ? pArg->pStaticDerefs[i].pDerefString : "*", pArg->pStaticDerefs[i].iLineNum,
				pParent->pFileName, pArg->pStaticDerefs[i].eLockType == ATR_LOCK_ARRAY_OPS ? " (ARRAY OPS LOCK)" : "");
		}
	}
}

static void FixUpAutoTransFuncStaticStuff(ATR_FuncDef *pFunc)
{

	int i;


	if (pFunc->pStaticEarrayUses)
	{
		for (i=0; pFunc->pStaticEarrayUses[i].iContainerArgIndex != -1; i++)
		{
			AddEverythingFromStaticEarrayUse(pFunc, &pFunc->pStaticEarrayUses[i]);
		}
	}

	if (pFunc->pContainerArgs)
	{
		for (i=0; pFunc->pContainerArgs[i].pArgName; i++)
		{
			AddAllStaticDereferencesForArg(pFunc, &pFunc->pContainerArgs[i]);
		}
	}

	if (pFunc->pFunctionCalls)
	{
		for (i=0; pFunc->pFunctionCalls[i].pFuncName; i++)
		{
			if (!FindATRFuncDef(pFunc->pFunctionCalls[i].pFuncName))
			{
				AddEverythingFromUnknownFuncCall(pFunc, &pFunc->pFunctionCalls[i]);
			}
		}
	}

}

//returns true if something changed
bool AddEverythingFromRecursingFunc(ATR_FuncDef *pParentFunc, ATR_FuncDef *pChildFunc,
	ATRFunctionCallDef *pFuncCall, ATRContainerArgDef *pParentContainerArg,
	ATRFuncCallContainerArg *pContainerArgInFuncCall)
{
	ATRContainerArgDef *pChildContainerArg = FindContainerArgDefByIndex(pChildFunc, pContainerArgInFuncCall->iArgIndex);
	int i;
	bool bSomethingChanged = false;

	for (i=0; i < eaSize(&pChildContainerArg->ppLocks); i++)
	{
		char newDerefString[2048];
		sprintf(newDerefString, "%s.%s", pContainerArgInFuncCall->pDerefString, pChildContainerArg->ppLocks[i]->pDerefString);
		bSomethingChanged |= AddLockf(pParentFunc, pParentContainerArg, newDerefString, pChildContainerArg->ppLocks[i]->eLockType,
			"%s.%s passed as arg %d into %s (%s:%d). Then %s",
			pParentContainerArg->pArgName, pContainerArgInFuncCall->pDerefString,
			pContainerArgInFuncCall->iArgIndex, pFuncCall->pFuncName, pParentFunc->pFileName,
			pFuncCall->iLineNum, pChildContainerArg->ppLocks[i]->pComment);
	}

	for (i=0;i < eaSize(&pChildContainerArg->ppEarrayUses); i++)
	{
		ATRFixedUpEarrayUse *pEarrayUse = pChildContainerArg->ppEarrayUses[i];
		char newDerefString[2048];
		sprintf(newDerefString, "%s.%s", pContainerArgInFuncCall->pDerefString, pEarrayUse->pContainerDerefString);

		switch(pEarrayUse->eIndexType)
		{
		case ATR_INDEX_LITERAL_INT:
		case ATR_INDEX_LITERAL_STRING:
			bSomethingChanged |= AddEarrayUsef(pParentFunc, pParentContainerArg, newDerefString, pEarrayUse->eLockType, pEarrayUse->eIndexType,
				pEarrayUse->iVal, pEarrayUse->pSVal, "%s.%s passed into %s (%s:%d). Then %s",
				pParentContainerArg->pArgName, pContainerArgInFuncCall->pDerefString, pFuncCall->pFuncName,
				pParentFunc->pFileName, pFuncCall->iLineNum, pEarrayUse->pComment);
			break;

			//this is the tricky case where the called function wants to use one of its passed-in arguments as
			//an array index. This will only work if the argument the parent function passes in is one of ITS
			//passed-in arguments, or a literal string or a literal float.
		case ATR_INDEX_SIMPLE_ARG:
			{
				int iSimpleArgIndex = pEarrayUse->iVal;
				ATRFuncCallSimpleArg *pSimpleArgInParent = FindFuncCallSimpleArgByIndex(pFuncCall, iSimpleArgIndex);

				if (pSimpleArgInParent)
				{
					switch (pSimpleArgInParent->eArgType)
					{
					case ATR_INDEX_LITERAL_INT:
						bSomethingChanged |= AddEarrayUsef(pParentFunc, pParentContainerArg, newDerefString, pEarrayUse->eLockType,
							ATR_INDEX_LITERAL_INT, pSimpleArgInParent->iVal, NULL,
							"%s.%s passed into %s (%s:%d) along with literal int %d as simple arg %d. Then %s",
								pParentContainerArg->pArgName, pContainerArgInFuncCall->pDerefString, pFuncCall->pFuncName,
								pParentFunc->pFileName, pFuncCall->iLineNum, pSimpleArgInParent->iVal,
								iSimpleArgIndex, pEarrayUse->pComment);
						break;
								
					case ATR_INDEX_LITERAL_STRING:
						bSomethingChanged |= AddEarrayUsef(pParentFunc, pParentContainerArg, newDerefString, pEarrayUse->eLockType,
							ATR_INDEX_LITERAL_STRING, 0, pSimpleArgInParent->pSVal,
							"%s.%s passed into %s (%s:%d) along with literal string %s as simple arg %d. Then %s",
								pParentContainerArg->pArgName, pContainerArgInFuncCall->pDerefString, pFuncCall->pFuncName,
								pParentFunc->pFileName, pFuncCall->iLineNum, pSimpleArgInParent->pSVal,
								iSimpleArgIndex, pEarrayUse->pComment);
						break;

					case ATR_INDEX_SIMPLE_ARG:
						bSomethingChanged |= AddEarrayUsef(pParentFunc, pParentContainerArg, newDerefString, pEarrayUse->eLockType,
							ATR_INDEX_SIMPLE_ARG, pSimpleArgInParent->iVal, NULL, "%s.%s passed into %s (%s:%d) along with simple arg %s passed through. Then %s",
								pParentContainerArg->pArgName, pContainerArgInFuncCall->pDerefString, pFuncCall->pFuncName,
								pParentFunc->pFileName, pFuncCall->iLineNum, FindSimpleArgDefByIndex(pParentFunc, pSimpleArgInParent->iVal)->pArgName,
								pEarrayUse->pComment);
						break;
					}
				}
				else
				{
					bSomethingChanged |= AddLockf(pParentFunc, pParentContainerArg, newDerefString, ATR_LOCK_NORMAL, 
						"%s.%s passed into %s (%s:%d), which wanted to do an earray lookup with simple arg %d, but the argument in the parent function was not itself a simple arg, or literal int or string. (Then %s)",
							pParentContainerArg->pArgName, pContainerArgInFuncCall->pDerefString, pFuncCall->pFuncName,
							pParentFunc->pFileName, pFuncCall->iLineNum, iSimpleArgIndex,
							pEarrayUse->pComment);
				}
			}
			break;



		}

		/*
static void AddEarrayUsef(ATR_FuncDef *pParent, ATRContainerArgDef *pArg, char *pDerefString,
	 enumLockType eLockType, enumEarrayIndexType eIndexType, 
	 int iVal, char *pSVal, char *pCommentString, ...)*/
	}

	return bSomethingChanged;
}

#define RECURSE_STOPPER ((char*)0x1)

//returns either NULL, meaning that no logging occurs, or the name of a root-level function which does logging (possibly this function, possibly a helper)
static char * FixupReturnLoggingInfoRecurse(ATR_FuncDef *pFunc)
{
	char *pFound = NULL;
	int i;
	if (pFunc->bDoesReturnLogging)
	{
		return pFunc->pHelperFuncWhichReturnsLogging ? pFunc->pHelperFuncWhichReturnsLogging : pFunc->pFuncName;
	}

	if (pFunc->pHelperFuncWhichReturnsLogging == RECURSE_STOPPER)
	{
		return NULL;
	}

	pFunc->pHelperFuncWhichReturnsLogging = RECURSE_STOPPER;

	if (pFunc->pFunctionCalls)
	{
		for (i=0; pFunc->pFunctionCalls[i].pFuncName; i++)
		{
			ATRFunctionCallDef *pFuncCall = &pFunc->pFunctionCalls[i];

			ATR_FuncDef *pChildFunc = FindATRFuncDef(pFuncCall->pFuncName);

			if (pChildFunc)
			{
				pFound = FixupReturnLoggingInfoRecurse(pChildFunc);
				if (pFound)
				{
					break;
				}
			}
		}
	}

	pFunc->pHelperFuncWhichReturnsLogging = pFound;
	if (pFound)
	{
		pFunc->bDoesReturnLogging = true;
	}

	return pFound;
}
	


//returns true if something actually changed
static bool FixupAutoTransRecurse(ATR_FuncDef *pParentFunc, int iContainerArgIndex)
{
	int i, j;
	bool bSomethingChanged = false;

	ATRContainerArgDef *pContainerArg = FindContainerArgDefByIndex(pParentFunc, iContainerArgIndex);


	assert(pContainerArg);

	if (pContainerArg->bFixedUp)
	{
		return false;
	}

	pContainerArg->bFixedUp = true;


	if (pParentFunc->pFunctionCalls)
	{
		for (i=0; pParentFunc->pFunctionCalls[i].pFuncName; i++)
		{
			ATRFunctionCallDef *pFuncCall = &pParentFunc->pFunctionCalls[i];

			ATR_FuncDef *pChildFunc = FindATRFuncDef(pFuncCall->pFuncName);

			if (pChildFunc)
			{
				if (pFuncCall->pContainerArgs)
				{
					for (j=0; pFuncCall->pContainerArgs[j].iParentArgIndex != -1; j++)
					{
						if (pFuncCall->pContainerArgs[j].iParentArgIndex == iContainerArgIndex)
						{
							//just because the parent is passing in a container argument to the child, that doesn't
							//mean the child actually thinks it's a container object... if not, just lock everything
							if (FindContainerArgDefByIndex(pChildFunc, pFuncCall->pContainerArgs[j].iArgIndex))
							{
								bSomethingChanged |= FixupAutoTransRecurse(pChildFunc, pFuncCall->pContainerArgs[j].iArgIndex);

								bSomethingChanged |= AddEverythingFromRecursingFunc(pParentFunc, pChildFunc, pFuncCall, 
									pContainerArg, &pFuncCall->pContainerArgs[j]);
							}
							else
							{
								bSomethingChanged |= AddLockf(pParentFunc, pContainerArg, pFuncCall->pContainerArgs[j].pDerefString, 
									ATR_LOCK_NORMAL, "%s.%s passed as a non-magical argument into function %s on line %d of %s",
									pContainerArg->pArgName, 
									pFuncCall->pContainerArgs[j].pDerefString[0] ? pFuncCall->pContainerArgs[j].pDerefString : "*", 
									pChildFunc->pFuncName, pFuncCall->iLineNum, pParentFunc->pFileName);
							}

						}
					}
				}
			}
		}
	}

	return bSomethingChanged;
}

void CEL_Fail(ATR_FuncDef *pFunc, ATRContainerArgDef *pContainerArg)
{
	char *pTemp = NULL;
	int i;

	estrPrintf(&pTemp, "For AUTO_TRANS %s, arg %s, found non-matching ATR_LOCKS. ATR_LOCKS: \"%s\". Actual locks:\n",
		pFunc->pFuncName, pContainerArg->pArgName, pContainerArg->pExpectedLocks);

	for (i=0; i < eaSize(&pContainerArg->ppLocks); i++)
	{
		estrConcatf(&pTemp, "%s (%s)\n\n", pContainerArg->ppLocks[i]->pDerefString, pContainerArg->ppLocks[i]->pComment);
	}

	estrConcatf(&pTemp, "\nEarray uses:\n");

	for (i=0; i < eaSize(&pContainerArg->ppEarrayUses); i++)
	{
		estrConcatf(&pTemp, "%s[] (%s)\n\n", pContainerArg->ppEarrayUses[i]->pContainerDerefString, pContainerArg->ppEarrayUses[i]->pComment);
	}

	Errorf("%s", pTemp);
}

bool LocksMatch(char *pStr1, char *pStr2)
{
	if (pStr1[0] == '.')
	{
		pStr1++;
	}

	if (pStr2[0] == '.')
	{
		pStr2++;
	}

	return stricmp(pStr1, pStr2) == 0;
}

bool CheckExpectedLocks(ATR_FuncDef *pFunc, ATRContainerArgDef *pContainerArg)
{
	char **ppExpectedLockStrings = NULL;
	char **ppActualLockStrings = NULL;
	
	char **ppExtraExpectedStrings = NULL;
	char **ppExtraActualStrings = NULL;

	int i;
	bool bFailed = false;

	if (!pContainerArg->pExpectedLocks)
	{
		//only do this check for non-helper funcs
		if (gbErrorOnNoAtrLocks && pFunc->pWrapperFunc)
		{
			Errorf("Func %s arg %s has no ATR_LOCKS specified. This is horribly dangerous and illegal", pFunc->pFuncName, pContainerArg->pArgName);

			/*
			// Start Debug Logic
			for(i=eaSize(&eaNoAtrLockFuncs)-1; i>=0; --i) {
				if (stricmp(eaNoAtrLockFuncs[i], pFunc->pFuncName) == 0) {
					break;
				}
			}
			if (i < 0) {
				eaPush(&eaNoAtrLockFuncs, pFunc->pFuncName);
			}
			// End Debug Logic
			*/
		}
		return false;
	}

	if (!(stricmp(pContainerArg->pExpectedLocks, "(none)") == 0))
	{
		DivideString(pContainerArg->pExpectedLocks, ",", &ppExpectedLockStrings, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);
		assertmsgf(eaSize(&ppExpectedLockStrings), "Invalid or corrupt ATR_LOCKS for func %s arg %s", pFunc->pFuncName, pContainerArg->pArgName);
	}

	for (i=0; i < eaSize(&pContainerArg->ppLocks); i++)
	{
		char derefStringToUse[1024];
	
		sprintf(derefStringToUse, "%s%s", pContainerArg->ppLocks[i]->pDerefString, 
			pContainerArg->ppLocks[i]->eLockType == ATR_LOCK_ARRAY_OPS ? "[AO]": "");
		
		eaPush(&ppActualLockStrings, strdup(derefStringToUse));
	}

	for (i=0; i < eaSize(&pContainerArg->ppEarrayUses); i++)
	{
		char derefStringToUse[1024];
		sprintf(derefStringToUse, "%s[]", pContainerArg->ppEarrayUses[i]->pContainerDerefString);
		eaPush(&ppActualLockStrings, strdup(derefStringToUse));
	}

	FindDifferencesBetweenEarraysOfStrings(&ppExtraExpectedStrings, &ppExtraActualStrings, &ppExpectedLockStrings, &ppActualLockStrings, LocksMatch);
	
	if (eaSize(&ppExtraExpectedStrings) || eaSize(&ppExtraActualStrings))
	{
		char *pErrorString = NULL;
		estrPrintf(&pErrorString, "ATR_LOCKS error for func %s arg %s. ", pFunc->pFuncName, pContainerArg->pArgName);

		if (eaSize(&ppExtraExpectedStrings))
		{
			estrConcatf(&pErrorString, "In ATR_LOCKS but not actually locked:(");
			for (i=0; i < eaSize(&ppExtraExpectedStrings); i++)
			{
				estrConcatf(&pErrorString, "%s%s", i == 0 ? "" : ", ", ppExtraExpectedStrings[i]);
			}
			estrConcatf(&pErrorString, ")");
		}
		if (eaSize(&ppExtraActualStrings))
		{
			estrConcatf(&pErrorString, "Not in ATR_LOCKS but locked:(");
			for (i=0; i < eaSize(&ppExtraActualStrings); i++)
			{
				estrConcatf(&pErrorString, "%s%s", i == 0 ? "" : ", ", ppExtraActualStrings[i]);
			}
			estrConcatf(&pErrorString, ")");
		}
		Errorf("%s", pErrorString);
		log_printf(LOG_ERRORS, "%s", pErrorString);

		bFailed = true;
	}
	

	eaDestroy(&ppExtraExpectedStrings);
	eaDestroy(&ppExtraActualStrings);
	eaDestroyEx(&ppExpectedLockStrings, NULL);
	eaDestroyEx(&ppActualLockStrings, NULL);
	
	return bFailed;
}

//undo any weird side effects of the %s.%s melding of
//recursive locks, and remove a leading [], then
//add a leading . and make the full field lock be .*
static void DoFinalLockFixup(ATRFixedUpLock *pLock, ATRContainerArgDef *pContainerArg, ATR_FuncDef *pFunc)
{
	int len;
	char *pTemp;

	while (estrReplaceOccurrences(&pLock->pDerefString, "..", "."))
	{
	}

	while (pLock->pDerefString[0] == '.')
	{
		estrRemove(&pLock->pDerefString, 0, 1);
	}

	if (strStartsWith(pLock->pDerefString, "[]"))
	{
		estrRemove(&pLock->pDerefString, 0, 2);
	}

	while (pLock->pDerefString[0] == '.')
	{
		estrRemove(&pLock->pDerefString, 0, 1);
	}

	estrInsert(&pLock->pDerefString, 0, ".", 1);

	len = estrLength(&pLock->pDerefString);
	if ( len == 1)
	{
		estrPrintf(&pLock->pDerefString, ".*");
	}
	else 
	{
		if (pLock->pDerefString[len - 1] == '.')
		{
			// if the string ends with a dot, then remove the dot
			estrRemove(&pLock->pDerefString, len - 1, 1);
		}
	}

	//only do this check for non-helper funcs
	if (gbErrorOnFullLocks && pFunc->pWrapperFunc && !pContainerArg->bAllowFullLock)
	{
		if (stricmp(pLock->pDerefString, ".*") == 0)
		{
			Errorf("Auto trans %s is locking all of arg %s: %s", pFunc->pFuncName, pContainerArg->pArgName, pLock->pComment);

			/*
			// Start Debug Logic
			{
				int i;
				for(i=eaSize(&eaFullLockFuncs)-1; i>=0; --i) {
					if (stricmp(eaFullLockFuncs[i], pFunc->pFuncName) == 0) {
						break;
					}
				}
				if (i < 0) {
					eaPush(&eaFullLockFuncs, pFunc->pFuncName);
				}
			}
			// End Debug Logic
			*/
		}
	}

	//warning: horrible abuse of textparser. Don't try this at home
	pTemp = (char*)allocAddString(pLock->pDerefString);
	estrDestroy(&pLock->pDerefString);
	pLock->pDerefString = pTemp;

}

//for simplicity's sake, a NULL-terminated list, consisting of sublists of
//tpi name then fields to lock. fields to lock must all start with ., which is
//how they're differentiated
//
//NOTE NOTE NOTE when modifying theses... if there's a case where one type of top level container
//contains an internal (not-container) struct copy of a different top level container, and you pass
//the internal container into a helper function, the helper function won't know the difference
//and will apply this always-include logic and conceivably remove some needed fields.
static char *spFieldsToAlwaysIncludeSourceList[] = 
{
	"Entity",
		".myEntityType",
		".myContainerID",
		".debugName",
	NULL
};

AUTO_STRUCT;
typedef struct FieldsList
{
	char *pTPIName;
	char **ppFields;
} FieldsList;


static StashTable sFieldListsByTPIName = NULL;

// CRITICAL_SECTION for accessing sFieldListsByTPIName

static CRITICAL_SECTION sFieldListsByTPINameMutex = {0};

static void EnterFieldListsByTPINameCriticalSection(void)
{
	ATOMIC_INIT_BEGIN;

	InitializeCriticalSection(&sFieldListsByTPINameMutex); 

	ATOMIC_INIT_END;

	EnterCriticalSection(&sFieldListsByTPINameMutex);
}

static void LeaveFieldListsByTPINameCriticalSection(void)
{
	LeaveCriticalSection(&sFieldListsByTPINameMutex);
}


char ***GetListOfFieldsThatAreAlwaysIncluded(ParseTable containerTPI[])
{
	bool bFound;
	const char *pTableName = ParserGetTableName(containerTPI);
	static char **sppEmpty = NULL;
	FieldsList *pList;

	EnterFieldListsByTPINameCriticalSection();
	if (!sFieldListsByTPIName)
	{
		char **ppTemp = spFieldsToAlwaysIncludeSourceList;
		sFieldListsByTPIName = stashTableCreateWithStringKeys(2, StashDefault);

		while (*ppTemp)
		{
			pList = StructCreate(parse_FieldsList);
			pList->pTPIName = *ppTemp;
			ppTemp++;

			while (*ppTemp && (*ppTemp)[0] == '.')
			{
				eaPush(&pList->ppFields, *ppTemp);
				ppTemp++;
			}

			stashAddPointer(sFieldListsByTPIName, pList->pTPIName, pList, true);
		}

	}

	bFound = stashFindPointer(sFieldListsByTPIName, pTableName, &pList);
	LeaveFieldListsByTPINameCriticalSection();

	if (bFound)
	{
		return &pList->ppFields;
	}

	return &sppEmpty;
}

//some fields are always included in every auto transaction, but NOT locked. These are things like
//.debugName. If the auto trans system thinks it found a lock on that field we just remove that lock
void RemoveLocksOnFieldsThatAreAlwaysIncluded(ATRContainerArgDef *pContainerArg, ParseTable containerTPI[], char *pTransName)
{
	int i;
	char ***pppList = GetListOfFieldsThatAreAlwaysIncluded(containerTPI);

	if (!eaSize(&pContainerArg->ppLocks))
	{
		return;
	}

	for (i=eaSize(&pContainerArg->ppLocks) - 1; i >= 0; i--)
	{
		if (eaFindString(pppList, pContainerArg->ppLocks[i]->pDerefString) != -1)
		{
			StructDestroy(parse_ATRFixedUpLock, pContainerArg->ppLocks[i]);
			eaRemoveFast(&pContainerArg->ppLocks, i);
		}
	}

	if (!eaSize(&pContainerArg->ppLocks) && !eaSize(&pContainerArg->ppEarrayUses))
	{
		if (UserIsInGroup("software") && !g_isContinuousBuilder && timeSecondsSince2000() > 297367656 + 60 * 60 * 24 * 7)
		{
			Errorf("SERIOUS ERROR: after removing always include fields, no fields are locked at all for arg %s of auto trans %s. If you are using only fields mycontainerId, myContainerType and debugName, then come up with a different way to do what you're doing.", pContainerArg->pArgName, pTransName);
		}
	}
}


bool TPIColumnIsLegalForATR(ParseTable *pColumn)
{
	if (!(pColumn->type & TOK_PERSIST))
	{
		return false;
	}

	if (pColumn->type & TOK_NO_TRANSACT)
	{
		return false;
	}

	return true;
}

static void CheckLockLegality(ATR_FuncDef *pFunc, ATRContainerArgDef *pContainerArg, ParseTable *pParseTable, ATRFixedUpLock *pLock)
{
	char *pLockStringCopy = NULL;
	char **ppAllLockStrings = NULL;
	int i;


	if (stricmp(pLock->pDerefString, ".*") == 0)
	{
		return;
	}

	estrCopy2(&pLockStringCopy, pLock->pDerefString);

	//want to first chop out and extract ".foo" ".foo.bar" ".foo.bar.wakka", etc., then traverse them top down. This 
	//should result in more comprehensible error messages

	if (pLockStringCopy[0] != '.')
	{
		estrInsert(&pLockStringCopy, 0, ".", 1);
	}

	while (estrLength(&pLockStringCopy))
	{
		eaInsert(&ppAllLockStrings, strdup(pLockStringCopy), 0);
		estrTruncateAtLastOccurrence(&pLockStringCopy, '.');
	}

	for (i=0; i < eaSize(&ppAllLockStrings); i++)
	{
		ParseTable *pOutTPI;
		int iOutIndex;
		int iOutColumn;
		void *pOutStruct;

		if (!objPathResolveField(ppAllLockStrings[i], pParseTable, NULL, &pOutTPI, &iOutColumn, &pOutStruct, &iOutIndex, 0))
		{
			Errorf("While checking legality of AUTO_TRANS %s, arg %s, lock %s, couldn't resolve substring %s. Lock comment: %s\n",
				pFunc->pFuncName, pContainerArg->pArgName, pLock->pDerefString, ppAllLockStrings[i], pLock->pComment);
			return;
		}

		if (!TPIColumnIsLegalForATR(&pOutTPI[iOutColumn]))
		{
			Errorf("While checking legality of AUTO_TRANS %s, arg %s, lock %s, substring %s resolved to TPI %s column %d (%s), which is either not PERSIST or is NO_TRANSACT. Lock comment: %s\n",
				pFunc->pFuncName, pContainerArg->pArgName, pLock->pDerefString, ppAllLockStrings[i], ParserGetTableName(pOutTPI), iOutColumn, pOutTPI[iOutColumn].name, pLock->pComment);
			return;
		}
	}

	eaDestroyEx(&ppAllLockStrings, NULL);
	estrDestroy(&pLockStringCopy);
}

static void CheckEarrayUseLegality(ATR_FuncDef *pFunc, ATRContainerArgDef *pContainerArg, ParseTable *pParseTable, ATRFixedUpEarrayUse *pEarrayUse)
{
	char *pLockStringCopy = NULL;
	char **ppAllLockStrings = NULL;
	int i;


	if (stricmp(pEarrayUse->pContainerDerefString, ".*") == 0)
	{
		return;
	}

	estrCopy2(&pLockStringCopy, pEarrayUse->pContainerDerefString);

	//want to first chop out and extract ".foo" ".foo.bar" ".foo.bar.wakka", etc., then traverse them top down. This 
	//should result in more comprehensible error messages

	if (pLockStringCopy[0] != '.')
	{
		estrInsert(&pLockStringCopy, 0, ".", 1);
	}
	
	//[] is prefixed to any erray use that is hanging off an earray of containers... get rid of it for these purposes
	estrReplaceOccurrences(&pLockStringCopy, "[]", "");


	while (estrLength(&pLockStringCopy))
	{
		eaInsert(&ppAllLockStrings, strdup(pLockStringCopy), 0);
		estrTruncateAtLastOccurrence(&pLockStringCopy, '.');
	}

	for (i=0; i < eaSize(&ppAllLockStrings); i++)
	{
		ParseTable *pOutTPI;
		int iOutIndex;
		int iOutColumn;
		void *pOutStruct;

		if (!objPathResolveField(ppAllLockStrings[i], pParseTable, NULL, &pOutTPI, &iOutColumn, &pOutStruct, &iOutIndex, 0))
		{
			Errorf("While checking legality of AUTO_TRANS %s, arg %s, earray use %s, couldn't resolve substring %s. Lock comment: %s\n",
				pFunc->pFuncName, pContainerArg->pArgName, pEarrayUse->pContainerDerefString, ppAllLockStrings[i], pEarrayUse->pComment);
			return;
		}

		if (!TPIColumnIsLegalForATR(&pOutTPI[iOutColumn]))
		{
			Errorf("While checking legality of AUTO_TRANS %s, arg %s, earray use %s, substring %s resolved to TPI %s column %d (%s), which is either not PERSIST or is NO_TRANSACT. Lock comment: %s\n",
				pFunc->pFuncName, pContainerArg->pArgName, pEarrayUse->pContainerDerefString, ppAllLockStrings[i], ParserGetTableName(pOutTPI), iOutColumn, pOutTPI[iOutColumn].name, pEarrayUse->pComment);
			return;
		}
	}

	eaDestroyEx(&ppAllLockStrings, NULL);
	estrDestroy(&pLockStringCopy);
}



static void CheckContainerArgForIllegalLocks(ATR_FuncDef *pFunc, ATRContainerArgDef *pContainerArg, ParseTable *pParseTable)
{
	int i;

	for (i=0; i < eaSize(&pContainerArg->ppLocks); i++)
	{
		CheckLockLegality(pFunc, pContainerArg, pParseTable, pContainerArg->ppLocks[i]);
	}

	for (i=0; i < eaSize(&pContainerArg->ppEarrayUses); i++)
	{
		CheckEarrayUseLegality(pFunc, pContainerArg, pParseTable, pContainerArg->ppEarrayUses[i]);
	}
}


static bool FixupAutoTransFinal(ATR_FuncDef *pFunc)
{
	int i, j;
	bool bBadLock = false;

	for (i=0; pFunc->pContainerArgs[i].pArgName; i++)
	{
		ATRContainerArgDef *pContainerArg = &pFunc->pContainerArgs[i];

		eaPush(&pFunc->ppContainerArgsEarray_ServerMonitoringOnly, pContainerArg);

		for (j=eaSize(&pContainerArg->ppLocks)-1; j >=0; j--)
		{
			DoFinalLockFixup(pContainerArg->ppLocks[j], pContainerArg, pFunc);

			//if not a helper, do special logic for ARRAY_OPS_SPECIAL locks
			if (pFunc->pWrapperFunc) //checks if this is an ATR func as opposed to a helper func
			{
				if (pContainerArg->ppLocks[j]->eLockType == ATR_LOCK_ARRAY_OPS_SPECIAL)
				{
					if (strcmp(pContainerArg->ppLocks[j]->pDerefString, ".*") == 0)
					{
						StructDestroy(parse_ATRFixedUpLock, pContainerArg->ppLocks[j]);
						eaRemoveFast(&pContainerArg->ppLocks, j);
					}
					else
					{
						pContainerArg->ppLocks[j]->eLockType = ATR_LOCK_ARRAY_OPS;
					}
				}
			}
		}
	}

	if (pFunc->pWrapperFunc)
	{
		for (i=0; pFunc->pContainerArgs[i].pArgName; i++)
		{
			ATRContainerArgDef *pContainerArg = &pFunc->pContainerArgs[i];

			RemoveLocksOnFieldsThatAreAlwaysIncluded(pContainerArg, pContainerArg->pParseTable, pFunc->pFuncName);

			if (!isProductionMode())
			{
				bBadLock |= CheckExpectedLocks(pFunc, pContainerArg);
			}
		}

		for (i=0; pFunc->pContainerArgs[i].pArgName; i++)
		{
			ATRContainerArgDef *pContainerArg = &pFunc->pContainerArgs[i];

			CheckContainerArgForIllegalLocks(pFunc, pContainerArg, pContainerArg->pParseTable);
		}
	}
	else
	{
		for (i=0; pFunc->pContainerArgs[i].pArgName; i++)
		{
			ATRContainerArgDef *pContainerArg = &pFunc->pContainerArgs[i];

			RemoveLocksOnFieldsThatAreAlwaysIncluded(pContainerArg, pContainerArg->pParseTable, pFunc->pFuncName);

			if (!isProductionMode())
			{
				bBadLock |= CheckExpectedLocks(pFunc, pContainerArg);	
			}
		}
	}

	CheckSuspiciousFuncCalls(pFunc->pFuncName, pFunc->pSuspiciousFunctionCalls);

	return bBadLock;

}

AUTO_STRUCT;
typedef struct SuspiciousFuncCall
{
	char *pSuspiciousFuncName;
	char **ppAutoTransNames;
} SuspiciousFuncCall;

static StashTable sSuspiciousFuncCallsByName = NULL;

static void CheckSuspiciousFuncCall(char *pAutoTransFuncName, char *pFuncName)
{
	SuspiciousFuncCall *pCall;

	if (stashFindPointer(ATRFuncDefTable, pFuncName, NULL))
	{
		return;
	}

	if (stashFindPointer(ATRSimpleHelperFuncsByName, pFuncName, NULL))
	{
		return;
	}
	
	if (!sSuspiciousFuncCallsByName)
	{
		sSuspiciousFuncCallsByName = stashTableCreateWithStringKeys(16, StashDefault);
	}

	if (stashFindPointer(sSuspiciousFuncCallsByName, pFuncName, &pCall))
	{

	}
	else
	{
		pCall = StructCreate(parse_SuspiciousFuncCall);
		pCall->pSuspiciousFuncName = strdup(pFuncName);
		stashAddPointer(sSuspiciousFuncCallsByName, pCall->pSuspiciousFuncName, pCall, false);
	}

	eaPush(&pCall->ppAutoTransNames, strdup(pAutoTransFuncName));
}

static void CheckSuspiciousFuncCalls(char *pAutoTransFuncName, char *pFuncNames)
{
	char **ppSuspiciousFuncNames = NULL;
	int i;

	if (!pFuncNames || !pFuncNames[0])
	{
		return;
	}

	if (isProductionMode())
	{
		return;
	}

	DivideString(pFuncNames, ",", &ppSuspiciousFuncNames, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	for (i=0; i < eaSize(&ppSuspiciousFuncNames); i++)
	{
		CheckSuspiciousFuncCall(pAutoTransFuncName, ppSuspiciousFuncNames[i]);
	}

	eaDestroyEx(&ppSuspiciousFuncNames, NULL);
}

static void GenerateSuspiciousFuncCallReport(void)
{
	char *pFullString = NULL;
	if (!sSuspiciousFuncCallsByName)
	{
		return;
	}

	FOR_EACH_IN_STASHTABLE(sSuspiciousFuncCallsByName, SuspiciousFuncCall, pCall)
	{
		int i;
		estrConcatf(&pFullString, "Unknown presumed-function-call \"%s\" was called by transaction funcs. It should either be whitelisted, blacklisted, or made into an AUTO_TRANS_HELPER_SIMPLE. Called by: ",
			pCall->pSuspiciousFuncName);

		for (i=0; i < eaSize(&pCall->ppAutoTransNames); i++)
		{
			estrConcatf(&pFullString, "%s%s", i == 0 ? "" : ", ", pCall->ppAutoTransNames[i]);
		}

		estrConcatf(&pFullString, "\n");

//		Errorf("%s", pFullString);
		log_printf(LOG_ERRORS, "%s", pFullString);

		estrDestroy(&pFullString);

	}
	FOR_EACH_END


	stashTableDestroyStruct(sSuspiciousFuncCallsByName, NULL, parse_SuspiciousFuncCall);


}

void ATR_DoLateInitialization(void)
{
	char *pStackString = NULL;
	int i;

	StashTableIterator iterator;
	StashElement element;

	static int bFirst = true;

	if (!bFirst)
	{
		return;
	}

	bFirst = false;

	AutoTransNonHelperTable = stashTableCreateWithStringKeys(8, StashDefault);

	ATR_CallIdentifierFixupFuncs();

	if (ATRFuncDefTable)
	{
		bool bSomethingChanged;
		bool bBadLock = false;

		stashGetIterator(ATRFuncDefTable, &iterator);

		while (stashGetNextElement(&iterator, &element))
		{
			ATR_FuncDef *pFuncDef = stashElementGetPointer(element);

			FixUpAutoTransFuncStaticStuff(pFuncDef);
			FixupReturnLoggingInfoRecurse(pFuncDef);
		}


/*Keep recursively fixing up everything until there are no more changes*/

		while (1)
		{
			bSomethingChanged = false;

			stashGetIterator(ATRFuncDefTable, &iterator);

			while (stashGetNextElement(&iterator, &element))
			{
				ATR_FuncDef *pFuncDef = stashElementGetPointer(element);

				if (pFuncDef->pContainerArgs)
				{
					for (i=0; pFuncDef->pContainerArgs[i].pArgName; i++)
					{
						bSomethingChanged |= FixupAutoTransRecurse(pFuncDef, pFuncDef->pContainerArgs[i].iArgIndex);
					}
				}
			}

			if (!bSomethingChanged)
			{
				break;
			}

			stashGetIterator(ATRFuncDefTable, &iterator);

			while (stashGetNextElement(&iterator, &element))
			{
				ATR_FuncDef *pFuncDef = stashElementGetPointer(element);

				if (pFuncDef->pContainerArgs)
				{
					for (i=0; pFuncDef->pContainerArgs[i].pArgName; i++)
					{
						pFuncDef->pContainerArgs[i].bFixedUp = false;
					}
				}
			}
		}
		
		stashGetIterator(ATRFuncDefTable, &iterator);

		while (stashGetNextElement(&iterator, &element))
		{
			ATR_FuncDef *pFuncDef = stashElementGetPointer(element);

			bBadLock |= FixupAutoTransFinal(pFuncDef);
		}
		
		if(!isProductionMode())
		{
			if (bBadLock)
			{
				Errorf("Bad ATR_LOCKS, see errorf for failed locks");
			}

			/*
			// Start Debug Logic
			eaQSort(eaNoAtrLockFuncs, strCmp);
			for(i=0; i<eaSize(&eaNoAtrLockFuncs); ++i) {
				printf("Missing ATR_LOCKS on transaction: %s\n", eaNoAtrLockFuncs[i]);
			}
			eaQSort(eaFullLockFuncs, strCmp);
			for(i=0; i<eaSize(&eaFullLockFuncs); ++i) {
				printf("Full container lock on transaction: %s\n", eaFullLockFuncs[i]);
			}
			// End Debug Logic
			*/
		}

	}

	estrDestroy(&pStackString);

	if (ATRSimpleHelperFuncsByName)
	{	
		stashGetIterator(ATRSimpleHelperFuncsByName, &iterator);

		while (stashGetNextElement(&iterator, &element))
		{
			char *pName = stashElementGetStringKey(element);
			char *pFuncCalls = stashElementGetPointer(element);

			CheckSuspiciousFuncCalls(pName, pFuncCalls);
		}
	}

	GenerateSuspiciousFuncCallReport();

	resRegisterDictionaryForStashTable("Auto_Trans_Funcs", RESCATEGORY_SYSTEM, 0, ATRFuncDefTable, parse_ATR_FuncDef);

}

void **ppIdentifierFixupFuncs = NULL;

void RegisterATRIdentifierFixupFunc(ATRIdentifierFixupFunc *pFunc)
{
	eaPush(&ppIdentifierFixupFuncs, pFunc);
}

void ATR_CallIdentifierFixupFuncs(void)
{
	int i;

	for (i=0; i < eaSize(&ppIdentifierFixupFuncs); i++)
	{
		ATRIdentifierFixupFunc *pFunc = ppIdentifierFixupFuncs[i];
		pFunc();
	}
}

AUTO_FIXUPFUNC;
TextParserResult fixupATRFixedUpLock(ATRFixedUpLock *pLock, enumTextParserFixupType eType, void *pExtraData)
{
	switch(eType)
	{
		xcase FIXUPTYPE_DESTRUCTOR:
		{
			//during final fixup, we may have turned this into a pool string. If so, NULL it so it doesn't get estr destroyed
			if (allocFindString(pLock->pDerefString) == pLock->pDerefString)
			{
				pLock->pDerefString = NULL;
			}


			if (stricmp_safe(pLock->pComment, NO_COMMENTS_IN_PRODUCTION_MODE) == 0)
			{
				pLock->pComment = NULL;
			}
		}
	}
	return PARSERESULT_SUCCESS;
}



AUTO_FIXUPFUNC;
TextParserResult fixupATRFixedUpEarrayUse(ATRFixedUpEarrayUse *pEarrayUse, enumTextParserFixupType eType, void *pExtraData)
{
	switch(eType)
	{
		xcase FIXUPTYPE_DESTRUCTOR:
		{
			if (stricmp_safe(pEarrayUse->pComment, NO_COMMENTS_IN_PRODUCTION_MODE) == 0)
			{
				pEarrayUse->pComment = NULL;
			}
		}
	}
	return PARSERESULT_SUCCESS;
}

typedef struct LockReportStruct {
	ParseTable *pPTI;
	char* pArgName;
	ATRFixedUpLock *pLock;
} LockReportStruct;

void DestroyLockReport(LockReportStruct* pLock)
{
	estrDestroy(&pLock->pArgName);

	free(pLock);
}

AUTO_COMMAND;
void ATR_GenerateLockReport(ACMD_SENTENCE search)
{
	StashTableIterator iter;
	StashElement elem;

	stashGetIterator(ATRFuncDefTable, &iter);
	while(stashGetNextElement(&iter, &elem))
	{
		ATR_FuncDef *pFuncDef = stashElementGetPointer(elem);
		LockReportStruct **ppLockReports = NULL;
		LockReportStruct *pTmpStruct;
		const char* pTableName;
		int i;

		if(!pFuncDef->pWrapperFunc)
			continue;

		for(i=0; pFuncDef->pContainerArgs && pFuncDef->pContainerArgs[i].pArgName; i++)
		{
			int j;
			ATRContainerArgDef *pContainerArgDef = &pFuncDef->pContainerArgs[i];
			ATRArgDef *pArgDef = &pFuncDef->pArgs[pContainerArgDef->iArgIndex];

			for(j=0; j<eaSize(&pContainerArgDef->ppLocks); j++)
			{
				ATRFixedUpLock *pLock = pContainerArgDef->ppLocks[j];

				if(!stricmp(pLock->pDerefString, ".*"))
				{
					pTableName = ParserGetTableName(pArgDef->pParseTable);

					if(!search || !search[0] || strstri(search, pTableName))
					{
						pTmpStruct = calloc(1, sizeof(LockReportStruct));

						pTmpStruct->pArgName = estrDup(pContainerArgDef->pArgName);
						pTmpStruct->pPTI = pFuncDef->pArgs[pContainerArgDef->iArgIndex].pParseTable;
						pTmpStruct->pLock = pLock;
						eaPush(&ppLockReports, pTmpStruct);
						pTmpStruct = NULL;

						break;
					}
				}
				else if(pLock->eLockType==ATR_LOCK_NORMAL)
				{
					ParseTable *pFinalTable = NULL;
					char *estr = NULL;
					int iFinalColumn = 0;

					estr = estrDup(pLock->pDerefString);
					estrReplaceOccurrences(&estr, "->", ".");
					if(estr && estr[0]!='.')
						estrInsert(&estr, 0, ".", 1);

					if(ParserResolvePath(estr, pArgDef->pParseTable, NULL, &pFinalTable, &iFinalColumn, NULL, NULL, NULL, NULL, 0))
					{
						if(TOK_HAS_SUBTABLE(pFinalTable[iFinalColumn].type))
						{
							ParseTable *subTable = pFinalTable[iFinalColumn].subtable;
							pTableName = ParserGetTableName(subTable);

							if(!search || !search[0] || strstri(search, pTableName))
							{
								pTmpStruct = calloc(1, sizeof(LockReportStruct));
								estrInsert(&estr, 0, pContainerArgDef->pArgName, (int)strlen(pContainerArgDef->pArgName));
								pTmpStruct->pArgName = estr;
								pTmpStruct->pPTI = subTable;
								pTmpStruct->pLock = pLock;
								eaPush(&ppLockReports, pTmpStruct);
								pTmpStruct = NULL;

								break;
							}
						}
					}
					else
						estrDestroy(&estr);
				}
			}
		}

		if(eaSize(&ppLockReports))
		{
			printfColor(COLOR_RED, "%s(%s):", pFuncDef->pFuncName, pFuncDef->pFileName);
			for(i=0; i<eaSize(&ppLockReports); i++)
			{
				LockReportStruct *pLockReport = ppLockReports[i];
				const char* pTypeName = ParserGetTableName(pLockReport->pPTI);

				printf("\n\t%s:%s", pTypeName, pLockReport->pLock->pComment);
			}
			printf("\n\n");
		}

		eaDestroyEx(&ppLockReports, DestroyLockReport);
	}
}

AUTO_COMMAND;
char *GetATRLocksString(char *pFuncName)
{
	ATR_FuncDef *pFunc = FindATRFuncDef(pFuncName);
	static char *pRetString = NULL;
	int iArgNum;
	int i;

	if (!pFunc)
	{
		return "Can't find funcdef";
	}

	estrClear(&pRetString);

	for (iArgNum = 0; iArgNum < eaSize(&pFunc->ppContainerArgsEarray_ServerMonitoringOnly); iArgNum++)
	{
		ATRContainerArgDef *pArg = pFunc->ppContainerArgsEarray_ServerMonitoringOnly[iArgNum];
		bool bFirst = true;
		if (eaSize(&pArg->ppLocks) || eaSize(&pArg->ppEarrayUses))
		{
			estrConcatf(&pRetString, "%sATR_LOCKS(%s, \"", iArgNum == 0 ? "" : "\n", pArg->pArgName);
			for (i=0; i < eaSize(&pArg->ppLocks); i++)
			{
				estrConcatf(&pRetString, "%s%s%s", bFirst ? "" : ", ", pArg->ppLocks[i]->pDerefString, pArg->ppLocks[i]->eLockType == ATR_LOCK_ARRAY_OPS ? "[AO]": "");
				bFirst = false;
			}
			for (i=0; i < eaSize(&pArg->ppEarrayUses); i++)
			{
				static char *spTemp = NULL;

				estrPrintf(&spTemp, "%s[]", pArg->ppEarrayUses[i]->pContainerDerefString);

				if (!CommaSeparatedListContainsWord(pRetString, spTemp))
				{
					estrConcatf(&pRetString, "%s%s", bFirst ? "" : ", ", spTemp);
					bFirst = false;
				}
			}
			estrConcatf(&pRetString, "\")");
		}
		else
		{
			estrConcatf(&pRetString, "%sATR_LOCKS(%s, \"(none)\") //Nothing locked... unusual and worrying, but not necessarily illegal",
				iArgNum == 0 ? "" : "\n", pArg->pArgName);
		}

	}

	return pRetString;
}

AUTO_COMMAND ACMD_SERVERONLY;
void DumpAtrLocksStringsFromFile(char *filename)
{
	int n = 0;
	char *f = fileAlloc(filename,NULL);
	char *s;
	char fileName[CRYPTIC_MAX_PATH];
	FILE *pFile;

	ATR_DoLateInitialization();

	sprintf(fileName, "c:\\temp\\%s_ATR_LOCKS_from_DumpAtrLocksStringsFromFile.txt", GlobalTypeToName(GetAppGlobalType()));
	printf("writing to %s...", fileName);
	mkdirtree_const(fileName);
	pFile = fopen(fileName, "wt");
	if (!pFile)
	{
		return;
	}

	s = strtok_r(f,"\n", &f);
	while(s){
		char *args[10];
		int i = tokenize_line(s,args,NULL);

		if(i == 1) {
			char *str = GetATRLocksString(args[0]);
			fprintf(pFile, "%s:\n%s\n\n", args[0], str);
		} else {
			printf("error: too many tokens for line %s", s);
		}
		s = strtok_r(NULL,"\n",&f);
	}
	fclose(pFile);
	printf("done\n");
}


AUTO_COMMAND;
void DumpAllATRLocksToFile(void)
{
	char fileName[CRYPTIC_MAX_PATH];
	FILE *pFile;
	StashTableIterator iter;
	StashElement elem;


	ATR_DoLateInitialization();

	sprintf(fileName, "c:\\temp\\%s_ATR_LOCKS.txt", GlobalTypeToName(GetAppGlobalType()));
	mkdirtree_const(fileName);
	pFile = fopen(fileName, "wt");
	if (!pFile)
	{
		return;
	}

	stashGetIterator(ATRFuncDefTable, &iter);
	while(stashGetNextElement(&iter, &elem))
	{
		ATR_FuncDef *pFuncDef = stashElementGetPointer(elem);

		char *pString = GetATRLocksString(pFuncDef->pFuncName);

		fprintf(pFile, "ATR_LOCKS for %s%s\n%s\n\n\n", 
			pFuncDef->pFuncName, pFuncDef->pWrapperFunc ? "" : " (HELPER)", pString);
	}

	fclose(pFile);
}


#define LOCALSTRUCTSTRINGPREFIX "<&__LOCAL_STRUCT(\"\""

//writes a specially coded string which includes the address of the struct, and will
//be replaced by ParserWriteText if the transaction is going to be executed remotely
void AutoTrans_WriteLocalStructString(ParseTable *pTPI, void *pStructPtr, char **ppOutString)
{
	if (pStructPtr)
	{
		estrConcatf(ppOutString, LOCALSTRUCTSTRINGPREFIX "%s,%p\"\")&>", ParserGetTableName(pTPI), pStructPtr);
	}
	else
	{
		estrConcatf(ppOutString, "NULL");
	}
}

static bool AutoTrans_DecodeLocalStructString(char *pString, int *piFoundLength, void **ppStructAddress, ParseTable **ppTPI)
{
	char *pEnd;
	int iSourceLength;
	char *pInnerString = NULL;
	char *pComma;
	void *pStructAddress = NULL;
	ParseTable *pTPI;
	
	if (!strStartsWith(pString, LOCALSTRUCTSTRINGPREFIX))
	{
		return false;
	}

	pEnd = strstr(pString, "&>");
	if (!pEnd)
	{
		return false;
	}

	estrStackCreate(&pInnerString);

	iSourceLength = pEnd - pString + 2;

	estrSetSize(&pInnerString, iSourceLength);
	memcpy(pInnerString, pString, iSourceLength);

	estrRemoveUpToFirstOccurrence(&pInnerString, '(');
	estrTruncateAtLastOccurrence(&pInnerString, ')');

	if (estrLength(&pInnerString) < 4)
	{
		estrDestroy(&pInnerString);
		return false;
	}

	estrRemove(&pInnerString, 0, 2);
	estrRemove(&pInnerString, estrLength(&pInnerString) - 2, 2);

	pComma = strchr(pInnerString, ',');
	if (!pComma)
	{
		estrDestroy(&pInnerString);
		return false;
	}

	*pComma = 0;
	pTPI = ParserGetTableFromStructName(pInnerString);
	if (!pTPI)
	{
		estrDestroy(&pInnerString);
		return false;
	}

	sscanf(pComma + 1, "%p", &pStructAddress);
	if (!pStructAddress)
	{
		estrDestroy(&pInnerString);
		return false;
	}

	*ppTPI = pTPI;
	*ppStructAddress = pStructAddress;
	*piFoundLength = iSourceLength;
	estrDestroy(&pInnerString);
	return true;
}


static void AutoTrans_FixupSingleLocalStructString(char **ppFixupString, int iStartingOffset)
{
	char *pStartString = (*ppFixupString) + iStartingOffset;
	void *pStructAddress = NULL;
	ParseTable *pTPI;
	char *pStructString = NULL;
	int iSourceLength;

	if (!AutoTrans_DecodeLocalStructString(pStartString, &iSourceLength, &pStructAddress, &pTPI))
	{
		AssertOrAlert("BAD_AUTOTRANS_LOCALSTRUCTSTRING", "Auto trans string %s has a bad <&__LOCAL_STRUCT",
			*ppFixupString);
		return;
	}

	estrStackCreateSize(&pStructString, 16000);

	ParserWriteTextEscaped(&pStructString, pTPI, pStructAddress, 0, 0, 0);

	estrRemove(ppFixupString, iStartingOffset, iSourceLength);
	estrInsert(ppFixupString, iStartingOffset, pStructString, estrLength(&pStructString));

	estrDestroy(&pStructString);
}

//given a string which may have one or more of the above in it, returns NULL if no changes
//were needed, or else a string with all the local structs replaced with ParserWriteText
int AutoTrans_FixupLocalStructStringIntoParserWriteText(char **ppFixupString)
{
	U32 *piFoundOffsets = NULL;
	char *pReadHead = *ppFixupString;
	char *pFound;
	int iFoundCount = 0;
	int i;

	if (!pReadHead)
	{
		return 0;
	}

	ea32StackCreate(&piFoundOffsets, 32);

	while ((pFound = strstr(pReadHead, LOCALSTRUCTSTRINGPREFIX)))
	{
		iFoundCount++;
		ea32Push(&piFoundOffsets, pFound - *ppFixupString);
		pReadHead = pFound + 1;
	}

	for (i = ea32Size(&piFoundOffsets)-1; i >= 0; i--)
	{
		AutoTrans_FixupSingleLocalStructString(ppFixupString, piFoundOffsets[i]);
	}

	ea32Destroy(&piFoundOffsets);
	return iFoundCount;
}

void *AutoTrans_ParserReadTextEscapedOrMaybeFromLocalStructString(ParseTable *pTPI, char *pString, bool bReturnLocalCopyWithNoCloneIfPossible, bool *pbOutReturnedLocalCopy)
{
	if (stricmp(pString, "NULL") == 0)
	{
		return NULL;
	}

	if (strStartsWith(pString, LOCALSTRUCTSTRINGPREFIX))
	{
		ParseTable *pFoundTPI;
		void *pStruct;
		int iLength;

		if (AutoTrans_DecodeLocalStructString(pString, &iLength, &pStruct, &pFoundTPI))
		{
			assertmsgf(pFoundTPI == pTPI, "While decoding LocalStructString, found nonmatching TPIs %p and %p (%s and %s)",
				pFoundTPI, pTPI, ParserGetTableName(pFoundTPI), ParserGetTableName(pTPI));

			if (bReturnLocalCopyWithNoCloneIfPossible)
			{
				*pbOutReturnedLocalCopy = true;
				return pStruct;
			}
			else
			{
				return StructCloneVoid(pTPI, pStruct);
			}
		}
		else
		{
			assertmsgf(0, "Unable to decode LocalStructString %s", pString);
			return NULL;
		}
	}
	else
	{
		void *pOutStruct = StructCreateVoid(pTPI);
		ParserReadTextEscaped(&pString,pTPI,pOutStruct, 0);
		return pOutStruct;
	}
}



void AutoTrans_VerifyReturnLoggingCompatibility(const char *pAutoTransFuncName, GlobalType eTypeToExecuteOn, 
	TransactionReturnVal *pReturnVal)
{
	if (eTypeToExecuteOn == GetAppGlobalType())
	{
		ATR_FuncDef *pFunc = FindATRFuncDef(pAutoTransFuncName);

		//this function is only called in dev mode, so just asserting
		assertmsgf(pFunc, "Someone trying to call unknown local AUTO_TRANS %s", pAutoTransFuncName);
	
		if (pFunc->bDoesReturnLogging)
		{
			if (!pReturnVal || !(pReturnVal->eFlags & TRANSACTIONRETURN_FLAG_LOGGED_RETURN))
			{
				char *pErrorString = NULL;
				estrPrintf(&pErrorString, "AUTO_TRANS %s being called %s. ", pAutoTransFuncName, pReturnVal ? "with a managed return val that does not do returnLogging (ie, one from LoggedTransactions.h)" : "with no return val");

				if (pFunc->pHelperFuncWhichReturnsLogging)
				{
					estrConcatf(&pErrorString, "However, that AUTO_TRANS, or one of its helper functions, calls helper function %s, which emits logged return values. This will cause that logging to be lost, which is bad",
						pFunc->pHelperFuncWhichReturnsLogging);
				}
				else
				{
					estrConcatf(&pErrorString, "However, that AUTO_TRANS emits logged return values. This will cause that logging to be lost, which is bad");
				}

				Errorf("%s", pErrorString);
				estrDestroy(&pErrorString);
			}
		}
	}
	else
	{
		if (!pReturnVal || !(pReturnVal->eFlags & TRANSACTIONRETURN_FLAG_LOGGED_RETURN))
		{
			//this is a big ugly mess because utilitiesLib can't have remote commands, yurg, finally ends up in FinalCallRemoteVerifyNoReturnLogging
			RemotelyVerifyNoReturnLogging(pAutoTransFuncName, eTypeToExecuteOn);
		}
	}
}

void DEFAULT_LATELINK_RemotelyVerifyNoReturnLogging(const char *pAutoTransFuncName, GlobalType eTypeToExecuteOn)
{
	assertmsg(0, "How is this being called? How is someone calling an AUTO_TRANS from outside serverLib?");
}

void FinalCallRemoteVerifyNoReturnLogging(char *pAutoTransFuncName, GlobalType eCallingType)
{
	ATR_FuncDef *pFunc = FindATRFuncDef(pAutoTransFuncName);

	//this function is only called in dev mode, so just asserting
	assertmsgf(pFunc, "Someone on server type %s trying to call unknown local AUTO_TRANS %s", GlobalTypeToName(eCallingType), pAutoTransFuncName);
	
	if (pFunc->bDoesReturnLogging)
	{
	
		char *pErrorString = NULL;
		estrPrintf(&pErrorString, "Someone on %s trying to call AUTO_TRANS %s either with no return value or with one that doesn't do return val logging. ", GlobalTypeToName(eCallingType), pAutoTransFuncName);
		
		if (pFunc->pHelperFuncWhichReturnsLogging)
		{
			estrConcatf(&pErrorString, "However, that AUTO_TRANS, or one of its helper functions, calls helper function %s, which emits logged return values. This will cause that logging to be lost, which is bad",
				pFunc->pHelperFuncWhichReturnsLogging);
		}
		else
		{
			estrConcatf(&pErrorString, "However, that AUTO_TRANS emits logged return values. This will cause that logging to be lost, which is bad");
		}

		estrConcatf(&pErrorString, ". Please make sure to use one of the special managed return value functions found in LoggedTransactions.h");
		
		Errorf("%s", pErrorString);
		estrDestroy(&pErrorString);
		
	}
}

#include "AutoTransDefs_h_ast.c"
#include "AutoTransSupport_c_ast.c"
