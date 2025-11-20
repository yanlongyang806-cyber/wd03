#include "ExprHttp.h"
#include "Textparser.h"
#include "ExprHttp_h_ast.h"
#include "StashTable.h"
#include "ResourceInfo.h"
#include "ExpressionFunc.h"
#include "qsortg.h"
#include "HttpXpathSupport.h"
#include "NotesServerComm.h"
#include "ExpressionFunc_h_ast.h"
#include "Expression.h"
#include "file.h"

#include "ExprHttp_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

AUTO_STRUCT;
typedef struct ImportantFuncTable
{
	char *pCodeName;
	char *pDesignerName;
} ImportantFuncTable;

static ImportantFuncTable **sppImportantFuncTables = NULL;

static void AddDesignerName(char *pTableName, char *pDesignerName)
{
	ExprFuncTable *pTable = NULL;

	if (stashFindPointer(sFuncTablesByName, pTableName, &pTable))
	{
		if (eaFindString(&pTable->ppDesignerNames, pDesignerName) == -1)
		{
			eaPush(&pTable->ppDesignerNames, strdup(pDesignerName));
		}
	}
}


static void LoadImportantFuncTableNamesFromFile(char *pFileName)
{
	char *pBuf = fileAlloc(pFileName, NULL);
	char **ppLines = NULL;

	if (!pBuf)
	{
		return;
	}

	DivideString(pBuf, "\r\n", &ppLines, DIVIDESTRING_STANDARD);

	FOR_EACH_IN_EARRAY_FORWARDS(ppLines, char, pLine)
	{
		if (pLine[0] != '/')
		{
			char **ppWords = NULL;
			ImportantFuncTable *pTable = NULL;

			DivideString(pLine, ":", &ppWords, DIVIDESTRING_STANDARD);

			assertmsgf(eaSize(&ppWords) == 2, "Badly formatted line %s in ImportantFuncTables",
				pLine);
		
			pTable = StructCreate(parse_ImportantFuncTable);
			pTable->pCodeName = strdup(ppWords[0]);
			pTable->pDesignerName = strdup(ppWords[1]);

			AddDesignerName(pTable->pCodeName, pTable->pDesignerName);

			eaPush(&sppImportantFuncTables, pTable);

			eaDestroyEx(&ppWords, NULL);
		}
	}
	FOR_EACH_END;

	eaDestroyEx(&ppLines, NULL);
	free(pBuf);
}

			


static void InitImportantFuncTableNames(void)
{
	char fileName[CRYPTIC_MAX_PATH];

	if (sppImportantFuncTables)
	{
		return;
	}

	LoadImportantFuncTableNamesFromFile("server/ImportantFuncTables.txt");
	sprintf(fileName, "server/ImportantFuncTables_%s.txt", GetProductName());
	LoadImportantFuncTableNamesFromFile(fileName);
}





StashTable sAllExpressionsByName = NULL;

static char *MultiValTypeToString(MultiValType type)
{
	switch(type)
	{
	xcase MULTI_NONE:
		return "VOID";
	xcase MULTI_INT:
		return "INT";
	xcase MULTI_FLOAT:
		return "FLOAT";
	xcase MULTI_INTARRAY:
		return "INTARRAY";
	xcase MULTI_FLOATARRAY:
		return "FLOATARRAY";
	xcase MULTI_STRING:
		return "STRING";
	xcase MULTI_NP_ENTITYARRAY:
		return "ENTITYARRAY";
	xcase MULTIOP_NP_STACKPTR:
		return "EXPRESSION";
	xcase MULTI_NP_POINTER:
		return "STRUCT";
	xcase MULTIOP_LOC_MAT4:
	case MULTIOP_LOC_STRING:
		return "LOCATION";
	xdefault:
		return "OTHER";
	}
}

static void GetExpressionTypeName(ExprFuncArg* pArg, bool bIncludeName, char **estrOut)
{
	if (pArg->staticCheckType)
	{
		estrConcatf(estrOut, "%s(%s)", MultiValTypeToString(pArg->type), pArg->staticCheckType);
	}
	else if (pArg->ptrTypeName)
	{
		estrConcatf(estrOut, "%s(%s)", MultiValTypeToString(pArg->type), pArg->ptrTypeName);
	}
	else
	{
		estrConcatf(estrOut, "%s", MultiValTypeToString(pArg->type));
	}

	if (pArg->name && bIncludeName)
	{
		estrConcatf(estrOut, " %s", pArg->name);
	}
}
/*
<span style="font-weight: bold;"> href | href </span>
*/

static void SetLinkString(void)
{
	static char *spLinkString = NULL;
	estrClear(&spLinkString);

	if (eaSize(&sppImportantFuncTables))
	{
		int i;
		estrConcatf(&spLinkString, "<span style=\"font-weight: bold;\">");

		for (i = 0; i < eaSize(&sppImportantFuncTables); i++)
		{
			estrConcatf(&spLinkString, "<a href = \"%s%sExprFuncs&svrFilter=ExprFuncTableIncludesExpr%%28%%22%s%%22%%2C+me.name%%29\">%s</a> | ",
				LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME, sppImportantFuncTables[i]->pCodeName, sppImportantFuncTables[i]->pDesignerName);
		}

		estrConcatf(&spLinkString, "</span>\n");
	}

	estrConcatf(&spLinkString, "<a href=\"%s%sFunctionTables\">All function tables</a>",
		LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME);
	resDictSetHTMLExtraLink("ExprFuncs", "%s", spLinkString);
}




static void AddFuncTables(ExpressionFuncForServerMonitor *pFuncForServerMon, ExprFuncDesc *pFunc)
{

	FOR_EACH_IN_STASHTABLE(sFuncTablesByName, ExprFuncTable, pTable)
	{
		if (exprAllowedToUseFunc(pTable, pFunc))
		{
			eaPush(&pFuncForServerMon->ppFuncTables, pTable->pName);
			pTable->iNumExprFuncs++;
		}
	}
	FOR_EACH_END;	
}



AUTO_COMMAND;
void BeginExpressionServerMonitoring(void)
{
	static char *pLinkString = NULL;
	int i;

	if (sAllExpressionsByName)
	{
		return;
	}

	InitImportantFuncTableNames();

	sAllExpressionsByName = stashTableCreateWithStringKeys(256, StashDefault);
	resRegisterDictionaryForStashTable("ExprFuncs", RESCATEGORY_SYSTEM, RESDICTLFAG_NO_PAGINATION, sAllExpressionsByName, parse_ExpressionFuncForServerMonitor);

	resRegisterDictionaryForStashTable("FunctionTables", RESCATEGORY_SYSTEM, 0, sFuncTablesByName, parse_ExprFuncTable);

	SetLinkString();

	FOR_EACH_IN_STASHTABLE(sFuncTablesByName, ExprFuncTable, pFuncTable)
	{
		estrPrintf(&pFuncTable->pFuncs, "<a href = \"%s%sExprFuncs&svrFilter=ExprFuncTableIncludesExpr%%28%%22%s%%22%%2C+me.name%%29\">ExprFuncs</a>",
			LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME, pFuncTable->pName);
	}
	FOR_EACH_END;



	FOR_EACH_IN_STASHTABLE(globalFuncTable, ExprFuncDesc, pExprFunc)
	{
		ExpressionFuncForServerMonitor *pFuncForServerMon = StructCreate(parse_ExpressionFuncForServerMonitor);
		char *pTemp = NULL;

		pFuncForServerMon->pName = pExprFunc->funcName;
		GetExpressionTypeName(&pExprFunc->returnType, false, &pFuncForServerMon->pReturnType);
		
		pFuncForServerMon->pComment = pExprFunc->comment;
		pFuncForServerMon->pSourceFile = pExprFunc->pSourceFile;
		pFuncForServerMon->iLineNum = pExprFunc->iLineNum;

		for (i = 0; i < pExprFunc->argc; i++)
		{
			GetExpressionTypeName(&pExprFunc->args[i], true,  &pTemp);
			eaPush(&pFuncForServerMon->ppArgs, pTemp);
			pTemp = NULL;
		}

		for (i = 0; i < ARRAY_SIZE(pExprFunc->tags); i++)
		{
			if (pExprFunc->tags[i].str)
			{
				estrConcatf(&pFuncForServerMon->pTags, "%s%s", estrLength(&pFuncForServerMon->pTags) ? ", " : "", 
					pExprFunc->tags[i].str);
			}
		}

		GetLocationNameForExpressionFuncServerMonitoring(&pFuncForServerMon->pLocation);

		AddFuncTables(pFuncForServerMon, pExprFunc);

		stashAddPointer(sAllExpressionsByName, pFuncForServerMon->pName, pFuncForServerMon, true);

		estrPrintf(&pFuncForServerMon->pNotes, "ExprFunc.%s, Expression Function %s, 0, 1",
			pFuncForServerMon->pName, pFuncForServerMon->pName);
	}
	FOR_EACH_END;

	SetLinkString();

	RequestAdditionalExprFuncs();
}
	
void DEFAULT_LATELINK_GetLocationNameForExpressionFuncServerMonitoring(char **ppOutString)
{
}


void DEFAULT_LATELINK_RequestAdditionalExprFuncs(void)
{
}

static void AddLocation(ExpressionFuncForServerMonitor *pFunc, char *pLoc)
{
	if (strstri(pFunc->pLocation, pLoc))
	{
		return;
	}

	if (stricmp(pFunc->pLocation, pLoc))
	{
		estrConcatf(&pFunc->pLocation, "+%s", pLoc);
	}
	else
	{
		estrInsertf(&pFunc->pLocation, 0, "%s+", pLoc);
	}
}

static void AddFuncTablesToPreexisting(ExpressionFuncForServerMonitor *pNewFunc, ExpressionFuncForServerMonitor *pPreexisting)
{
	FOR_EACH_IN_EARRAY(pNewFunc->ppFuncTables, char, pFuncTable)
	{
		if (eaFindString(&pPreexisting->ppFuncTables, pFuncTable) == -1)
		{
			ExprFuncTable *pActualTable;

			eaPush(&pPreexisting->ppFuncTables, pFuncTable);

			if (stashFindPointer(sFuncTablesByName, pFuncTable, &pActualTable))
			{
				pActualTable->iNumExprFuncs++;
			}
		}
	}
	FOR_EACH_END;
}

void AddListOfExpressionFuncsFromOtherLocation(ExpressionFuncForServerMonitorList *pList)
{
	FOR_EACH_IN_EARRAY(pList->ppFuncTableNames, char, pTableName)
	{
		if (!stashFindPointer(sFuncTablesByName, pTableName, NULL))
		{
			ExprFuncTable *pTable = exprContextCreateFunctionTable(pTableName);
			estrPrintf(&pTable->pFuncs, "<a href = \"%s%sExprFuncs&svrFilter=ExprFuncTableIncludesExpr%%28%%22%s%%22%%2C+me.name%%29\">ExprFuncs</a>",
				LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME, pTableName);

		}
	}
	FOR_EACH_END;



	FOR_EACH_IN_EARRAY(pList->ppFuncs, ExpressionFuncForServerMonitor, pFunc)
	{
		ExpressionFuncForServerMonitor *pPreExistingFunc;

		if (stashFindPointer(sAllExpressionsByName, pFunc->pName, &pPreExistingFunc))
		{
			AddLocation(pPreExistingFunc, pFunc->pLocation);
			AddFuncTablesToPreexisting(pFunc, pPreExistingFunc);
		}
		else
		{
			pFunc = StructClone(parse_ExpressionFuncForServerMonitor, pFunc);
			stashAddPointer(sAllExpressionsByName, pFunc->pName, pFunc, true);

			FOR_EACH_IN_EARRAY(pFunc->ppFuncTables, char, pFuncTable)
			{
				ExprFuncTable *pActualTable;

				if (stashFindPointer(sFuncTablesByName, pFuncTable, &pActualTable))
				{
					pActualTable->iNumExprFuncs++;
				}
			}
			FOR_EACH_END;

		}

	}
	FOR_EACH_END;

	//this is obviously inefficient but who cares
	FOR_EACH_IN_EARRAY( sppImportantFuncTables, ImportantFuncTable, pTable )
	{
		AddDesignerName(pTable->pCodeName, pTable->pDesignerName);
	}
	FOR_EACH_END;

}


//there are about 8 levels of meta going on here, but basically this expression takes in a func table
//name (specified in exprContextCreateFunctionTable()) and a func name, and returns true if that 
//func is in that func table
AUTO_EXPR_FUNC(util);
bool ExprFuncTableIncludesExpr(char *pTableName, char *pExprName)
{
	ExpressionFuncForServerMonitor *pFunc;

	if (!stashFindPointer(sAllExpressionsByName, pExprName, &pFunc))
	{
		return false;
	}

	if (eaFindString(&pFunc->ppFuncTables, pTableName) == -1)
	{
		return false;
	}

	return true;
}


#include "exprhttp_h_ASt.c"
#include "ExprHttp_c_ast.c"