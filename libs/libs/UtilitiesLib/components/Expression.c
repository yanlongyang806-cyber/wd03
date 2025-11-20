#include "Expression.h"
#include "ExpressionPrivate.h"

#include "ExpressionFunc.h"
#include "ExpressionParse.h"
#include "ExpressionTokenize.h"

#include "BlockEarray.h"
#include "error.h"
#include "estring.h"
#include "file.h"
#include "MemoryPool.h"
#include "ThreadSafeMemoryPool.h"
#include "mathutil.h"
#include "referencesystem.h"
#include "ResourceManager.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "timing.h"
#include "cmdParse.h"

#include "ExpressionMinimal_h_ast.h"
#include "ExpressionPrivate_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("ExpressionPrivate.h", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("ExpressionMinimal.h", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("ExpressionEvaluateBody.c", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("ExpressionEvaluateForAutoGenIncluding.c", BUDGET_GameSystems););

#define EXPR_MAX_INDENT_LEVEL 10

char * exprCurAutoTrans;

TSMP_DEFINE(ExprLine);
TSMP_DEFINE(Expression);

AUTO_RUN;
void RegisterExpressionMemoryPools(void)
{
	TSMP_CREATE(ExprLine, 1024);
	TSMP_CREATE(Expression, 1024);
}

ExprLine *exprLineCreate(const char *desc, const char *orig)
{
	ExprLine *line = StructAlloc(parse_ExprLine);
	if (desc)
		line->descStr = StructAllocString(desc);
	if (orig)
		line->origStr = StructAllocString(orig);
	return line;
}

void exprLineSetDescStr(ExprLine *line, const char *desc)
{
	if (line->descStr)
		StructFreeString(line->descStr);
	line->descStr = StructAllocString(desc);
}

void exprLineSetOrigStr(ExprLine *line, const char *orig)
{
	if (line->origStr)
		StructFreeString(line->origStr);
	line->origStr = StructAllocString(orig);
}

int exprIsEmpty(Expression* expr)
{
	if(!expr)
		return true;

	return !eaSize(&expr->lines);
}

int exprMatchesString(Expression *expr, const char* matchStr)
{
	char realMatch[1024];
	int i;
	
	if (!matchStr || !matchStr[0])
		return false;

	if(exprIsEmpty(expr))
		return false;

	sprintf(realMatch, "*%s*", matchStr);

	for (i = 0; i < eaSize(&expr->lines); i++)
	{
		if (expr->lines[i]->origStr && matchExact(realMatch, expr->lines[i]->origStr))
			return true;
	}
	return false;
}

int exprIsZero(Expression* expr)
{
	F64 f = 1.f;

	if(exprIsEmpty(expr))
		return true;

	if(exprIsSimpleNumber(expr,&f) && f==0.f)
		return true;

	return false;
}

int exprIsSimpleNumber(Expression* expr, F64* numOut)
{
	if(eaSize(&expr->lines)==1)
	{
		if(beaSize(&expr->postfixEArray)==1)
		{
			bool bValid = false;
			F64 fVal = MultiValGetFloat(&expr->postfixEArray[0],&bValid);
			if(bValid)
			{
				*numOut = fVal;
				return true;
			}
		}
		else if(beaSize(&expr->postfixEArray)==0)
		{
			char* end = 0;
			F64 fVal = strtod(expr->lines[0]->origStr, &end);
			if(end > expr->lines[0]->origStr && *end=='\0')
			{
				*numOut = fVal;
				return true;
			}
		}
	}

	return false;
}


int exprUsesNOT(Expression* expr)
{
	int i;
	
	if(exprIsEmpty(expr))
		return false;

	for(i=beaSize(&expr->postfixEArray)-1; i>=0; i--)
	{
		if(expr->postfixEArray[i].type==MULTIOP_NOT)
			return true;
	}
	
	return false;
}


void exprLineDestroy(ExprLine *line)
{
	StructDestroy(parse_ExprLine, line);
}

Expression* exprCreate(void)
{
	return StructCreate(parse_Expression);
}

Expression* exprCreateFromString(const char* origStr, const char* filename)
{
	Expression* expr = NULL;
	expr = exprCreate();
	exprSetOrigStr(expr, origStr, filename);
	return expr;
}

void exprAppendStringLines(Expression* expr, const char *str)
{
	char *nextLine = NULL;
	bool inQuotes = false;
	while (str[0])
	{
		if (str[0] == '"')
			inQuotes = !inQuotes;
		else if (str[0] == '\n' || (str[0] == ';' && !inQuotes))
		{
			eaPush(&expr->lines, exprLineCreate("", nextLine));
			estrClear(&nextLine);
			str++;
			continue;
		}

		if ((str[0] != ' ') || (nextLine[0] != '\0')) // Trim leading spaces, but not middle ones
			estrConcatChar(&nextLine, str[0]);
		str++;
	}
	if (estrLength(&nextLine) > 0)
		eaPush(&expr->lines, exprLineCreate("", nextLine));
	estrDestroy(&nextLine);
}

void exprCopy(Expression *dest, const Expression *source)
{
	if (dest && source)
		StructCopyAll(parse_Expression, source, dest);
}

Expression* exprClone(Expression *source)
{
	return StructClone(parse_Expression, source);
}

void exprDestroy(Expression* expr)
{
	if (expr)
		StructDestroy(parse_Expression,expr);
}

int exprCompare(Expression* expr1, Expression* expr2)
{
	if(!!expr1 != !!expr2)
	{
		return !!expr1 - !!expr2;
	}
	else if(expr1)
	{
		int i, n1, n2;

		n1 = eaSize(&expr1->lines);
		n2 = eaSize(&expr2->lines);
		if(n1 != n2)
			return n1 - n2;

		for(i = 0; i < n1; i++)
		{
			int diff = stricmp(expr1->lines[i]->origStr, expr2->lines[i]->origStr);

			if(diff)
				return diff;
		}
	}
	return 0;
}

// This function is used to remove compiled information and filename information from an expression
void exprClean(Expression *expr)
{
	if(expr) 
	{
		if (expr->postfixEArray)
			beaDestroy(&expr->postfixEArray);
		expr->filename = NULL;
	}
}

void exprSetOrigStrNoFilename(Expression* expr, const char* origStr)
{
	if(expr->postfixEArray)
		beaDestroy(&expr->postfixEArray);

	eaClearStruct(&expr->lines, parse_ExprLine);
	if (origStr)
	{
		eaPush(&expr->lines, StructAlloc(parse_ExprLine));
		expr->lines[0]->origStr = StructAllocString(origStr);
	}
}

void exprSetOrigStr(Expression* expr, const char* origStr, const char* filename)
{
	if(expr->postfixEArray)
		beaDestroy(&expr->postfixEArray);

	eaClearStruct(&expr->lines, parse_ExprLine);
	if (origStr)
	{
		eaPush(&expr->lines, StructAlloc(parse_ExprLine));
		expr->lines[0]->origStr = StructAllocString(origStr);
	}
	expr->filename = allocAddFilename(filename);
}

int exprGenerateFromString_dbg(Expression* expr, ExprContext* context, const char* exprStr, const char* filename MEM_DBG_PARMS)
{
	//if(!eaSize(&expr->origStr) || stricmp(exprGetCompleteString(expr), exprStr))
	if(!eaSize(&expr->lines) || stricmp(exprGetCompleteString(expr), exprStr))
	{
		exprSetOrigStr(expr, exprStr, filename);
	}

	return exprGenerate_dbg(expr, context MEM_DBG_PARMS_CALL);
}

static bool sbPrintAllExprGenerates = false;
AUTO_CMD_INT(sbPrintAllExprGenerates, PrintAllExprGenerates) ACMD_HIDE ACMD_COMMANDLINE;

// context is used for static type checking
int exprGenerate_dbg(Expression* expr, ExprContext* context MEM_DBG_PARMS)
{
	static MultiVal** tokenized = NULL;
	static MultiVal** parsed = NULL;
	int i, num;
	MultiVal answer;
	int success = true;

	if(!expr)
		return false;

	PERFINFO_AUTO_START_FUNC();

	if (sbPrintAllExprGenerates)
	{
		printfColor(COLOR_RED | COLOR_BLUE | COLOR_BRIGHT, "ExprGenerate called with expr string %s and func table %s\n",
			expr->lines && expr->lines[0] ? expr->lines[0]->origStr : "(UNKNOWN)",
			context->funcTable ? context->funcTable->pName : "(NONE)");
	}

	ZeroStruct(&context->scResults);
	context->scResults.scExpr = expr;

	if(expr->postfixEArray)
		beaDestroy(&expr->postfixEArray);

	//if(!eaSize(&expr->origStr) || stricmp(expr->origStr[0], src))
		//eaPush(&expr->origStr, allocAddString(src));

	resAddValueDep("ExprStaticVarCRC");
	resAddValueDep("ExprGenerateVersion");

	num = eaSize(&expr->lines);

	if(!num)
	{
		ErrorFilenamef(expr->filename, "Empty Expression");
		PERFINFO_AUTO_STOP();
		return false;
	}

	for(i = 0; success && i < num; i++)
	{
		MultiVal* val;

		success &= !exprTokenize(expr->lines[i]->origStr, &tokenized, expr);

		//exprInsertStatementSeparator(&tokenized);
		if(i < num - 1)
		{
			val = MultiValCreate();
			val->type = MULTIOP_STATEMENT_BREAK;
			eaPush(&tokenized, val);
		}
	}

	if(success)
		success &= exprParseInternal(&parsed, &tokenized, expr);

	if(success && eaSize(&tokenized))
	{
		success = false;
		ErrorFilenamef(expr->filename, "Leftover tokens after parsing %s", exprGetCompleteString(expr));
	}

	if(success)
		exprPostParseFixup(&parsed);

	if(success)
	{
		beaSetCapacity(&expr->postfixEArray, eaSize(&parsed));

		for(i = 0; i < eaSize(&parsed); i++)
		{
			MultiVal* val = beaPushEmpty(&expr->postfixEArray);
			*val = *parsed[i];
		}

		success = exprEvaluateStaticCheck_dbg(expr, context, &answer MEM_DBG_PARMS_CALL);
	}

	if(!success)
	{
		MultiVal* zero;

		beaDestroy(&expr->postfixEArray);

		zero = beaPushEmpty(&expr->postfixEArray);
		zero->type = MULTI_INT;
		zero->intval = 0;
	}

	eaClearEx(&tokenized, MultiValDestroy);
	eaClearEx(&parsed, MultiValDestroy);

	PERFINFO_AUTO_STOP();

	return success;
}

int exprEvaluateRawString_dbg(const char* str MEM_DBG_PARMS)
{
	Expression* pExpr = exprCreate();
	static ExprContext* pContext = NULL;
	MultiVal answer = {0};

	if (!pContext)
	{
		ExprFuncTable* funcTable = exprContextCreateFunctionTable("RawString");
		pContext = exprContextCreate();
		exprContextAddFuncsToTableByTag(funcTable, "util");
		exprContextSetFuncTable(pContext, funcTable);
	}

	exprGenerateFromString_dbg(pExpr, pContext, str, NULL MEM_DBG_PARMS_CALL);

	exprEvaluate_dbg(pExpr, pContext, &answer MEM_DBG_PARMS_CALL);

	exprDestroy(pExpr);

	return QuickGetInt(&answer);
}

F32 exprEvaluateRawStringSafe_dbg(const char* str MEM_DBG_PARMS)
{
	Expression* pExpr = exprCreate();
	static ExprContext* pContext = NULL;
	MultiVal answer = {0};
	bool tst;

	if (!pContext)
	{
		ExprFuncTable* funcTable = exprContextCreateFunctionTable("RawStringSafe");
		pContext = exprContextCreate();
		exprContextAddFuncsToTableByTag(funcTable, "util");
		exprContextSetFuncTable(pContext, funcTable);
	}

	exprGenerateFromString_dbg(pExpr, pContext, str, NULL MEM_DBG_PARMS_CALL);

	exprEvaluate_dbg(pExpr, pContext, &answer MEM_DBG_PARMS_CALL);

	exprDestroy(pExpr);

	return (F32)MultiValGetFloat(&answer, &tst);
}

typedef struct
{
	Expression *pExpr;
	ExprContext *pContext;
	ParseTable *pTPI;
} CachedContextAndExpressionForRawStringObjectEvaluation;


int exprEvaluateRawStringWithObject_dbg(const char* str, const char* name, void* obj, ParseTable* tpi, bool cacheContext MEM_DBG_PARMS)
{
	static StashTable cachedContextTable = NULL;
	MultiVal answer = {0};
	static 	ExprFuncTable* funcTable;
	
	if (!funcTable)
	{
		funcTable = exprContextCreateFunctionTable("RawStringWithObject");
		exprContextAddFuncsToTableByTag(funcTable, "util");
	}


	if (cacheContext)
	{
		CachedContextAndExpressionForRawStringObjectEvaluation *pCache;

		if (!cachedContextTable)
		{
			cachedContextTable = stashTableCreateAddress(4);
		}

		//str should be pooled, so we just treat it as a pointer
		if (!stashFindPointer(cachedContextTable, str, &pCache))
		{
		
			pCache = calloc(sizeof(CachedContextAndExpressionForRawStringObjectEvaluation), 1);
			
			pCache->pExpr = exprCreate();
			pCache->pContext = exprContextCreate();
			exprContextSetFuncTable(pCache->pContext, funcTable);
			exprContextSetPointerVar(pCache->pContext, "me", obj, tpi, false, true);
			if(name)
				exprContextSetPointerVar(pCache->pContext, name, obj, tpi, false, true);
			if (!exprGenerateFromString_dbg(pCache->pExpr, pCache->pContext, str, NULL MEM_DBG_PARMS_CALL))
			{
				assertmsgf(0, "Couldn't generate expression from string %s\n", str);
			}

			stashAddPointer(cachedContextTable, str, pCache, false);

		}
		else
		{
			exprContextSetPointerVar(pCache->pContext, "me", obj, tpi, false, true);
			if(name)
				exprContextSetPointerVar(pCache->pContext, name, obj, tpi, false, true);
		}


		exprEvaluate_dbg(pCache->pExpr, pCache->pContext, &answer MEM_DBG_PARMS_CALL);

		return QuickGetIntSafe(&answer);
	}
	else
	{
		Expression *pExpr = exprCreate();
		ExprContext *pContext = exprContextCreate();
		
		exprContextSetFuncTable(pContext, funcTable);	
		exprContextSetPointerVar(pContext, "me", obj, tpi, false, true);
		if(name)
			exprContextSetPointerVar(pContext, name, obj, tpi, false, true);

		exprGenerateFromString_dbg(pExpr, pContext, str, NULL MEM_DBG_PARMS_CALL);

		exprEvaluate_dbg(pExpr, pContext, &answer MEM_DBG_PARMS_CALL);
		exprDestroy(pExpr);
		exprContextDestroy(pContext);

		return QuickGetIntSafe(&answer);
	}
}


F32 exprGetCost(Expression* expr)
{
	return expr->cost;
}

static StashTable locationLookupTable = NULL;

#define EXPR_LOCATION_MAX_PREFIX_SIZE 16

typedef struct ExprLocationLookupInfo
{
	char prefix[EXPR_LOCATION_MAX_PREFIX_SIZE];
	ExprResolveLocationCallback callback;
	int constant;
}ExprLocationLookupInfo;

int exprMat4FromLocationString(ExprContext* context, const char* location, Mat4 loc,
							   int getConstantOnly, const char* blamefile)
{
	char prefix[EXPR_LOCATION_MAX_PREFIX_SIZE];
	const char* colon = (location ? strchr(location, ':') : NULL);
	U32 prefixlen = colon - location;
	ExprLocationLookupInfo* info;

	if(!colon || prefixlen >= EXPR_LOCATION_MAX_PREFIX_SIZE || prefixlen == strlen(location) - 1)
		return false;

	strncpy(prefix, location, prefixlen);

	if(!stashFindPointer(locationLookupTable, prefix, &info))
		return false;

	if(getConstantOnly && !info->constant)
		return true;

	copyMat4(unitmat, loc);

	return info->callback(context, colon + 1, loc, blamefile);
}

void exprRegisterLocationPrefix(const char* prefix, ExprResolveLocationCallback callback, int constant)
{
	ExprLocationLookupInfo* info;

	devassert(strlen(prefix) < EXPR_LOCATION_MAX_PREFIX_SIZE);

	if(!locationLookupTable)
		locationLookupTable = stashTableCreateWithStringKeys(2, StashDefault);

	info = calloc(1, sizeof(*info));
	strncpy(info->prefix, prefix, EXPR_LOCATION_MAX_PREFIX_SIZE);
	info->callback = callback;
	info->constant = constant;

	stashAddPointer(locationLookupTable, info->prefix, info, true);
}

typedef struct ExprStaticCheckInfo {
	const char* scType;
	const char* scTypeMsg;							// For use in UGC, where extern var types can be seen
	ExprArgumentTypeStaticCheckFunction scFunc;
	ExprStaticCheckCategory scTypeCategory;	
	U32 valid : 1;
}ExprStaticCheckInfo;

static StashTable staticCheckArgTypes = NULL;

void exprRegisterStaticCheckArgumentType(const char* argtype, const char* argtypemsg, ExprArgumentTypeStaticCheckFunction callback)
{
	ExprStaticCheckInfo* scInfo;

	if(!staticCheckArgTypes)
		staticCheckArgTypes = stashTableCreateWithStringKeys(1, StashDefault);

	if(stashFindPointer(staticCheckArgTypes, argtype, &scInfo))
	{
		if(scInfo->scTypeCategory == ExprStaticCheckCat_Custom)
			scInfo->scFunc = callback;
	}
	else
	{
		scInfo = malloc(sizeof(ExprStaticCheckInfo));
		scInfo->scType = strdup(argtype);
		scInfo->scTypeMsg = argtypemsg ? strdup(argtypemsg) : NULL;
		scInfo->scFunc = callback;
		scInfo->scTypeCategory = ExprStaticCheckCat_Custom;
		stashAddPointer(staticCheckArgTypes, argtype, scInfo, false);
	}

	scInfo->valid = true;
}

const char* exprGetStaticCheckArgumentMsgKey(MultiValType type, const char* argtype, const char* argname)
{
	ExprStaticCheckInfo *info = NULL;
	char buf[1024];

	if(!staticCheckArgTypes)
		return NULL;

	if(!stashFindPointer(staticCheckArgTypes, argtype, &info))
	{
		switch(type)
		{
			xcase MULTI_INT: {
				sprintf(buf, "Int.%s", argname);
				return allocAddString(buf);
			}
			xcase MULTI_FLOAT: {
				sprintf(buf, "Float.%s", argname);
				return allocAddString(buf);
			}
			xcase MULTI_STRING: {
				sprintf(buf, "String.%s", argname);
				return allocAddString(buf);
			}
		}

		return NULL;
	}
 
	switch(info->scTypeCategory)
	{
		xcase ExprStaticCheckCat_Enum: {
			sprintf(buf, "Enum.%s.%s", info->scType, argname);
			return allocAddString(buf);
		}
		xcase ExprStaticCheckCat_Resource: {
			sprintf(buf, "Res.%s.%s", info->scType, argname);
			return allocAddString(buf);
		}
		xcase ExprStaticCheckCat_Reference: {
			sprintf(buf, "Ref.%s.%s", info->scType, argname);
			return allocAddString(buf);
		}
		xcase ExprStaticCheckCat_Custom: {
			sprintf(buf, "%s.%s", info->scTypeMsg, argname);
			return allocAddString(buf);
		}
	}

	return info->scTypeMsg;
}

void exprCheckStaticCheckType(const char* scType, ExprStaticCheckCategory scTypeCategory)
{
	ExprStaticCheckInfo* scInfo;

	if(!staticCheckArgTypes)
		staticCheckArgTypes = stashTableCreateWithStringKeys(1, StashDefault);

	if(!stashFindPointer(staticCheckArgTypes, scType, &scInfo))
	{
		scInfo = malloc(sizeof(ExprStaticCheckInfo));
		scInfo->scType = strdup(scType);
		scInfo->scFunc = NULL;
		scInfo->scTypeCategory = scTypeCategory;
		scInfo->valid = false;
		stashAddPointer(staticCheckArgTypes, scType, scInfo, false);
	}
	else if(scInfo->scTypeCategory==ExprStaticCheckCat_Custom && scTypeCategory!=scInfo->scTypeCategory)
	{
		// Allow explicit types to override customs, which are generally placeholders
		scInfo->scType = strdup(scType);
		scInfo->scFunc = NULL;
		scInfo->scTypeCategory = scTypeCategory;
		scInfo->valid = false;
	}

	devassertmsgf(scInfo, "Using unregistered static check type %s", scType);
	devassertmsgf(scTypeCategory == scInfo->scTypeCategory, "Found conflicting category defintions for registrations for %s", scType);
}

int exprVerifySCInfo(StashElement el)
{
	ExprStaticCheckInfo* scInfo = stashElementGetPointer(el);

	if(!scInfo->valid)
	{
		if(scInfo->scTypeCategory == ExprStaticCheckCat_Reference)
		{
			devassertmsgf(RefSystem_GetDictionaryHandleFromNameOrHandle(scInfo->scType),
				"Reference Dictionary %s is not registered yet or is a typo, but is listed as a static check type",
				scInfo->scType);
		}
		else if (scInfo->scTypeCategory == ExprStaticCheckCat_Resource)
		{
			devassertmsgf(resDictGetInfo(scInfo->scType),
				"Resource Dictionary %s is not registered yet or is a typo, but is listed as a static check type",
				scInfo->scType);
		}
		else if (scInfo->scTypeCategory == ExprStaticCheckCat_Enum)
		{
			devassertmsgf(FindNamedStaticDefine(scInfo->scType),
				"Enum %s is not registered yet or is a typo, but is listed as a static check type",
				scInfo->scType);
		}
		else
			devassertmsgf(0, "Found unregistered scType %s", scInfo->scType);
	}
	return 1;
}

AUTO_STARTUP(ExpressionSCRegister);
void exprExpressionSCRegisterDummy(void)
{

}

AUTO_STARTUP(ExpressionSC) ASTRT_DEPS(ExpressionSCRegister);
void exprVerifyStaticCheckTypes(void)
{
	if(staticCheckArgTypes)
		stashForEachElement(staticCheckArgTypes, exprVerifySCInfo);
}

ExprArgumentTypeStaticCheckFunction exprGetArgumentTypeStaticCheckFunction(const char* argtype)
{
	ExprStaticCheckInfo* scInfo;

	if(stashFindPointer(staticCheckArgTypes, argtype, &scInfo))
	{
		devassertmsg(scInfo->scTypeCategory == ExprStaticCheckCat_Custom, "Calling this function doesn't make sense for non-custom static checks");
		return scInfo->scFunc;
	}
	else
	{
		Errorf("Couldn't find static checking function for specified argument type %s", argtype);
		return NULL;
	}
}

int exprStaticCheckWithType(ExprContext* context, MultiVal* val, const char* scType, ExprStaticCheckCategory scTypeCategory, const char* blamefile)
{
	int valid = false;
	char* errorStr = NULL;	

	switch (scTypeCategory)
	{
	case ExprStaticCheckCat_Reference:
	case ExprStaticCheckCat_Resource:
		{		
			estrStackCreate(&errorStr);
			devassert(MULTI_FLAGLESS_TYPE(val->type) == MULTI_STRING);
			if (val->str != NULL && strcmp(val->str, MULTI_DUMMY_STRING))
			{
				// Dictionary indexes are not necessarily loaded in production so skip some static checking
				if (!isProductionMode() || isProductionEditMode())
				{
					valid = RefSystem_ReferentFromString(scType, val->str) 
						|| resGetInfo(scType, val->str);

					if (!valid && !RefSystem_GetDictionaryIgnoreNullReferences(scType))
					{
						estrPrintf(&errorStr,"String parameter %s was not found in Dictionary %s",
							val->str, scType);
						valid = resAddResourceDep(scType, val->str, REFTYPE_REFERENCE_TO, errorStr);
						if (!valid)
						{
							ErrorFilenamef(blamefile, "%s", errorStr);							
						}
					}
					else
					{
						resAddResourceDep(scType, val->str, REFTYPE_REFERENCE_TO, NULL);
					}
				}
				else
				{
					valid = true;
				}
			}
			else
			{
				valid = true;
			}
			estrDestroy(&errorStr);
			return valid;
		}
	case ExprStaticCheckCat_Enum:
		{
			StaticDefineInt *pEnum = FindNamedStaticDefine(scType);										
			devassert(MULTI_FLAGLESS_TYPE(val->type) == MULTI_STRING);
			if(pEnum)
			{
				valid = !!StaticDefineLookup((StaticDefine*)pEnum, val->str);
				if (!valid && stricmp(val->str, MULTI_DUMMY_STRING))
					ErrorFilenamef(blamefile, "String parameter %s not found in enum %s", val->str, scType);
			}									
			break;

			return true; // Doesn't work yet
		}
	case ExprStaticCheckCat_Custom:
		{
			ExprArgumentTypeStaticCheckFunction func;
			func = exprGetArgumentTypeStaticCheckFunction(scType);

			if(func)
			{
				estrStackCreate(&errorStr);
				valid = func(context, val, &errorStr);
				if(!valid)
					ErrorFilenamef(blamefile, "Parameter %s did not pass static checking (%s)", MultiValPrint(val), errorStr);
				estrDestroy(&errorStr);
			}

			return valid;
		}
	case ExprStaticCheckCat_None:
		return true;
	}	
	return true;
}

int exprResolveLocationCoord(ExprContext* context, const char* location, Mat4 loc, const char* blamefile)
{
	if(sscanf(location, "%f,%f,%f", &loc[3][0], &loc[3][1], &loc[3][2]) != 3)
	{
		ErrorFilenamef(blamefile, "GetPointFromLocation given invalid format for coord: \"%s\"", location);
		return false;
	}
	return true;
}

AUTO_RUN;
int exprAddDefaultLocationCallbacks(void)
{
	exprRegisterLocationPrefix("coord", exprResolveLocationCoord, true);
	return 0;
}

static void exprDestroyStrings(char *estr)
{
	if (estr)
		estrDestroy(&estr);
}

void exprPrintDecompiledVersion(Expression *pExpr, char** estr, U32 printWarning)
{
	MultiVal *pCurrentMultiVal = NULL;
	MultiValType eMultiValType = MULTI_INVALID;
	char **eaExpressionStack = NULL;
	char *estrTemp = NULL;
	MultiVal **eaIfBlocks = NULL;
	S32 *eaElsePositions = NULL;
	char pstrIndentation[EXPR_MAX_INDENT_LEVEL + 1] = { 0 };
	bool bPlaceSemiColon = false;
	bool bAddEndIfAsFinalStatement = false;
	static const char *pstrHeader = "Decompiled Expression (not necessarily same as the original expression):\n";

	S32 i, iElseIterator, iIndentLevel, iIndentLevelModifier, iTokenCount = beaSize(&pExpr->postfixEArray);

	devassert(estr);

	if(printWarning)
		estrConcat(estr, pstrHeader, (S32)strlen(pstrHeader));

	for(i = 0; i < iTokenCount; i++)
	{
		pCurrentMultiVal = &pExpr->postfixEArray[i];
		eMultiValType = pCurrentMultiVal->type;

		// Check the top of the if block stack
		// see if we have reached the end of the if block
		if (eaSize(&eaIfBlocks) > 0 && eaIfBlocks[eaSize(&eaIfBlocks) - 1]->int32 == i)
		{
			if (i == 0 || pExpr->postfixEArray[i - 1].type != MULTIOP_RETURN) // If the previous element is a return, if block continues
			{
				// Pop from the if block stack
				if (eaIfBlocks[eaSize(&eaIfBlocks) - 1]->type == MULTIOP_JUMP) // Pop one extra if top of the stack is jump rather than jumpifzero
					eaPop(&eaIfBlocks);
				eaPop(&eaIfBlocks);

				estrTemp = NULL;				
				estrCopy2(&estrTemp, "endif");
				eaPush(&eaExpressionStack, estrTemp);
			}
		}

		switch(eMultiValType)
		{
		case MULTIOP_RETURNIFZERO:
			{
				char *pClause = NULL;
				bAddEndIfAsFinalStatement = true;
				estrTemp = NULL;

				if (eaSize(&eaExpressionStack) > 0)
					pClause = eaPop(&eaExpressionStack);

				estrConcatf(&estrTemp, "if (%s)", pClause);
				eaPush(&eaExpressionStack, estrTemp);

				// Clean up
				estrDestroy(&pClause);
			}
			break;
		// if statements
		case MULTIOP_JUMPIFZERO:
			{
				bool bElif = false;
				char *pClause = NULL;
				int iIfBlocksSizeBeforeAdd = eaSize(&eaIfBlocks);

				MultiVal *pTopIfBlock = NULL;
				if (iIfBlocksSizeBeforeAdd > 0)
				{
					if (eaIfBlocks[iIfBlocksSizeBeforeAdd - 1]->type == MULTIOP_JUMPIFZERO)
						pTopIfBlock = eaIfBlocks[iIfBlocksSizeBeforeAdd - 1];
					else if (iIfBlocksSizeBeforeAdd > 1)
						pTopIfBlock = eaIfBlocks[iIfBlocksSizeBeforeAdd - 2];
				}

				if (pTopIfBlock == NULL || // Root level if statement
					pTopIfBlock->int32 > pCurrentMultiVal->int32)
				{
					// Add this jump as a new if block
					eaPush(&eaIfBlocks, pCurrentMultiVal);

					// Peak ahead to see if we have a return right before jump point
					if (i > 0 && 
						pExpr->postfixEArray[pCurrentMultiVal->int32 - 1].type == MULTIOP_RETURN)
					{
						// Add the else position we found to the array
						ea32Push(&eaElsePositions, pCurrentMultiVal->int32 - 1);
					}
				}
				else
				{
					bElif = true;
				}

				if (eaSize(&eaExpressionStack) > 0)
					pClause = eaPop(&eaExpressionStack);
				estrTemp = NULL;

				if (!bElif)
					estrConcatf(&estrTemp, "if (%s)", pClause);
				else
					estrConcatf(&estrTemp, "elif (%s)", pClause);
				eaPush(&eaExpressionStack, estrTemp);

				// Clean up
				estrDestroy(&pClause);
			}
			break;
		// returns
		case MULTIOP_RETURN:
			{
				estrTemp = NULL;
				estrCopy2(&estrTemp, "return");
				eaPush(&eaExpressionStack, estrTemp);
			}
			break;
		// jumps
		case MULTIOP_JUMP:
			{								
				if (eaSize(&eaIfBlocks) > 0 && eaIfBlocks[eaSize(&eaIfBlocks) - 1]->type == MULTIOP_JUMPIFZERO)
				{
					S32 j = 0;
					S32 pElseBlockPos = i;

					// Add this jump to determine the end point of the if block
					eaPush(&eaIfBlocks, pCurrentMultiVal);

					// Find the else by looping until the end point
					for (j = i + 1; j <= pCurrentMultiVal->int32 && j < iTokenCount; j++)
					{
						if (pExpr->postfixEArray[j].type == MULTIOP_JUMP &&
							pExpr->postfixEArray[j].int32 == pCurrentMultiVal->int32)
						{
							pElseBlockPos = j;
						}
					}

					// Add the else position we found to the array
					ea32Push(&eaElsePositions, pElseBlockPos);
				}
			}
			break;
		// function calls
		case MULTIOP_FUNCTIONCALL:
			{
				char *estrFunctionCall = NULL;
				char *estrOperand = NULL;
				S32 j, iCount = eaSize(&eaExpressionStack);
				S32 iOpenParenPos = -1;

				// Add the function name
				MultiValToEString(&pExpr->postfixEArray[i], &estrFunctionCall);

				// Back up until we find the open paren
				for (j = iCount - 1; j >= 0; j--)
				{
					if (strcmp(eaExpressionStack[j], "(") == 0)
					{
						iOpenParenPos = j;
						break;
					}
				}

				// Add all the params
				if (iOpenParenPos >= 0)
				{
					for (j = iOpenParenPos; j < iCount; j++)
					{
						// Concatenate comma if necessary
						if (strcmp(eaExpressionStack[j], ")") != 0 && 
							eaExpressionStack[j][0] != '.' && // Object path stuff
							j > iOpenParenPos + 1)
						{
							estrConcat(&estrFunctionCall, ", ", 2);
						}

						// Concatenate the operand
						estrConcat(&estrFunctionCall, eaExpressionStack[j], (S32)strlen(eaExpressionStack[j]));					
					}

					// Pop all operands and parens from the stack
					for (j = iCount - 1; j >= iOpenParenPos; j--)
					{
						estrOperand = eaPop(&eaExpressionStack);
						estrDestroy(&estrOperand);
					}
				}

				// Add the function call to the stack
				eaPush(&eaExpressionStack, estrFunctionCall);
			}
			break;
		// Operators with generic processing
		case MULTIOP_PAREN_OPEN:
		case MULTIOP_PAREN_CLOSE:
			{
				estrTemp = NULL;
				estrCopy2(&estrTemp, MultiValTypeToReadableString(eMultiValType));
				eaPush(&eaExpressionStack, estrTemp);
			}
			break;
		// Static var special handling
		case MULTIOP_STATICVAR:
			{
				estrTemp = NULL;
				estrCopy2(&estrTemp, exprGetStaticVarName(pExpr->postfixEArray[i].int32));
				eaPush(&eaExpressionStack, estrTemp);
			}
			break;
		// Unary operators
		case MULTIOP_NEGATE:
		case MULTIOP_BIT_NOT:
		case MULTIOP_NOT:
			{
				char *estrOperator = NULL;
				char *estrOperand = NULL;
				if (eaSize(&eaExpressionStack) > 0)
					estrOperand = eaPop(&eaExpressionStack);
				estrCopy2(&estrOperator, MultiValTypeToReadableString(eMultiValType));

				estrTemp = NULL;
				if (eMultiValType == MULTIOP_NEGATE)
					estrConcatf(&estrTemp, "(-%s)", estrOperand);
				else
					estrConcatf(&estrTemp, "(%s%s)", estrOperator, estrOperand);
				eaPush(&eaExpressionStack, estrTemp);

				// Clean up
				estrDestroy(&estrOperand);
				estrDestroy(&estrOperator);
			}
			break;
		// Binary operators
		case MULTIOP_ADD:
		case MULTIOP_SUBTRACT:
		case MULTIOP_MULTIPLY:
		case MULTIOP_DIVIDE:
		case MULTIOP_BIT_AND:
		case MULTIOP_BIT_OR:
		case MULTIOP_BIT_XOR: // Currently not implemented
		case MULTIOP_EXPONENT:
		case MULTIOP_EQUALITY:
		case MULTIOP_LESSTHAN:
		case MULTIOP_LESSTHANEQUALS:
		case MULTIOP_GREATERTHAN:
		case MULTIOP_GREATERTHANEQUALS:
		case MULTIOP_AND:
		case MULTIOP_OR:
			{
				char *estrOperator = NULL;
				char *estrOperand1 = NULL;
				char *estrOperand2 = NULL;
				if (eaSize(&eaExpressionStack) > 0)
					estrOperand2 = eaPop(&eaExpressionStack);
				if (eaSize(&eaExpressionStack) > 0)
					estrOperand1 = eaPop(&eaExpressionStack);
				estrCopy2(&estrOperator, MultiValTypeToReadableString(eMultiValType));

				estrTemp = NULL;
				estrConcatf(&estrTemp, "(%s %s %s)", estrOperand1, estrOperator, estrOperand2);
				eaPush(&eaExpressionStack, estrTemp);

				// Clean up
				estrDestroy(&estrOperand1);
				estrDestroy(&estrOperand2);
				estrDestroy(&estrOperator);
			}
			break;
		// Ignored tokens
		case MULTIOP_NOOP:		
		case MULTIOP_STATEMENT_BREAK:
			break;
		// Everything else
		default:
			{
				estrTemp = NULL;
				if (MULTI_GET_TYPE(eMultiValType) == MULTI_STRING &&
					eMultiValType != MULTIOP_IDENTIFIER &&
					eMultiValType != MULTIOP_ROOT_PATH &&
					eMultiValType != MULTIOP_OBJECT_PATH)
				{
					estrConcat(&estrTemp, "\"", 1);
				}
				MultiValToEString(&pExpr->postfixEArray[i], &estrTemp);
				if (MULTI_GET_TYPE(eMultiValType) == MULTI_STRING &&
					eMultiValType != MULTIOP_IDENTIFIER &&
					eMultiValType != MULTIOP_ROOT_PATH &&
					eMultiValType != MULTIOP_OBJECT_PATH)
				{
					estrConcat(&estrTemp, "\"", 1);
				}
				eaPush(&eaExpressionStack, estrTemp);
			}
		} // switch(eMultiValType)

		// Add else statements if we are in the correct position
		for (iElseIterator = ea32Size(&eaElsePositions) - 1; iElseIterator >= 0; iElseIterator--)
		{
			if (eaElsePositions[iElseIterator] == i)
			{
				estrTemp = NULL;
				estrCopy2(&estrTemp, "else");
				eaPush(&eaExpressionStack, estrTemp);
				ea32Pop(&eaElsePositions);
				break;
			}
		}
	} // for(i = 0; i < iTokenCount; i++)

	if (bAddEndIfAsFinalStatement)
	{
		estrTemp = NULL;
		estrCopy2(&estrTemp, "endif");
		eaPush(&eaExpressionStack, estrTemp);
	}

	// Process the expression
	iIndentLevel = 0;	
	for (i = 0; i < eaSize(&eaExpressionStack); i++)
	{
		iIndentLevelModifier = 0;
		bPlaceSemiColon = false;

		// if
		if (strstr(eaExpressionStack[i], "if") == eaExpressionStack[i])
		{
			iIndentLevelModifier = -1;
			++iIndentLevel;
		}
		// elif, else
		else if (strstr(eaExpressionStack[i], "elif") == eaExpressionStack[i] ||
				strstr(eaExpressionStack[i], "else") == eaExpressionStack[i])
		{
			iIndentLevelModifier = -1;
		}
		// endif
		else if (strstr(eaExpressionStack[i], "endif") == eaExpressionStack[i])
		{
			--iIndentLevel;
		}
		else
		{
			bPlaceSemiColon = true;
		}

		// Add indentation
		if ((iIndentLevel + iIndentLevelModifier) > 0)
		{
			memset(pstrIndentation, '\t', MIN(EXPR_MAX_INDENT_LEVEL, (iIndentLevel + iIndentLevelModifier)));
			pstrIndentation[MIN(EXPR_MAX_INDENT_LEVEL, (iIndentLevel + iIndentLevelModifier))] = 0;
			estrConcat(estr, pstrIndentation, (S32)strlen(pstrIndentation));
		}

		// Add the expression line
		estrConcat(estr, eaExpressionStack[i], (S32)strlen(eaExpressionStack[i]));
		if (bPlaceSemiColon)
			estrConcatChar(estr, ';');
		// New line
		estrConcatChar(estr, '\n');
	}

	// Clean up
	eaDestroyEx(&eaExpressionStack, exprDestroyStrings);
	eaDestroy(&eaIfBlocks);
	ea32Destroy(&eaElsePositions);
}

void exprPrintMultiVals(Expression* expr, char** estr)
{
	int i, j, n = beaSize(&expr->postfixEArray);
	int* jumps = NULL;

	// Print the decompiled version as well
	exprPrintDecompiledVersion(expr, estr, true);

	estrConcatf(estr, "Postfix EArray: ");

	if(!n)
		estrConcatf(estr, "(empty)");

	for(i = 0; i < n; i++)
	{
		if(expr->postfixEArray[i].type == MULTIOP_JUMPIFZERO ||
			expr->postfixEArray[i].type == MULTIOP_JUMP)
		{
			eaiPush(&jumps, expr->postfixEArray[i].intval);
		}
	}

	for(i = 0; i < n; i++)
	{
		for(j = eaiSize(&jumps)-1; j >= 0; j--)
		{
			if(jumps[j] == i)
			{
				estrConcatf(estr, "[%d:]", i);
				break;
			}
		}
		estrConcatf(estr, "%s ", MultiValPrint(&expr->postfixEArray[i]));
	}

	eaiDestroy(&jumps);
}

void exprGetCompleteStringEstr(Expression* expr, char** estr)
{
	int i, num;
	estrClear(estr);

	num = eaSize(&expr->lines);
	if(num)
	{
		for(i = 0; i < num - 1; i++)
		{
			estrConcatf(estr, "%s; ", expr->lines[i]->origStr ? expr->lines[i]->origStr : "(null)");
		}
		estrConcatf(estr, "%s", expr->lines[i]->origStr ? expr->lines[i]->origStr : "(null)");
	}
	else
	{
		exprPrintMultiVals(expr, estr);
	}
}

char* exprGetCompleteString(Expression* expr)
{
	static char* exprStr = NULL;

	if(!expr)
		return NULL;

	exprGetCompleteStringEstr(expr, &exprStr);

	return exprStr;
}

int exprMultiValGetPointer(CMultiVal* mv, ParseTable **table, void **ptr)
{
	if (MULTI_GET_TYPE(mv->type) == MULTI_NP_POINTER && mv->ptr)
	{
		const ExprVarEntry *entry = mv->ptr;
		*table = entry->table;
		*ptr = entry->inptr;
		return true;
	}
	else
	{
		*table = NULL;
		*ptr = NULL;
		return false;
	}
}

const char *exprMultiValGetVarName(CMultiVal* mv, ExprContext* context)
{
	if (MULTI_GET_TYPE(mv->type) == MULTI_NP_POINTER)
	{
		const ExprVarEntry* entry = mv->ptr;
		return entry ? entry->name : NULL;
	}
	else if (MULTI_GET_OPER(mv->type) == MMO_IDENTIFIER)
	{
		return mv->str;
	}
	else if (MULTI_GET_OPER(mv->type) == MMO_STATICVAR)
	{
		const ExprVarEntry* entry = NULL;
		while (context && !(entry = context->staticVars[mv->int32]))
			context = context->parent;
		return entry ? entry->name : NULL;
	}
	else
		return NULL;
}

void exprFindFunctions(Expression* expr, const char* func, int** eaFuncsOut)
{
	int i;

	for(i=0; i<beaSize(&expr->postfixEArray); i++)
	{
		MultiVal *val = &expr->postfixEArray[i];

		if(val->type==MULTIOP_FUNCTIONCALL)
		{
			char *estrFunctionCall = NULL;

			MultiValToEString(val, &estrFunctionCall);

			if(!func || !func[0] || !stricmp(func, estrFunctionCall))
			{
				eaiPush(eaFuncsOut, i);
			}

			estrDestroy(&estrFunctionCall);
		}
	}
}

const MultiVal* exprFindFuncParam(Expression* expr, int funcIndex, int paramIndex)
{
	int openParenIdx = -1, closeParenIdx = -1, i;
	char *estr = NULL;

	if(funcIndex<0 || funcIndex>=beaSize(&expr->postfixEArray))
		return NULL;

	for(i=funcIndex-1; i>=0; i--)
	{
		// find
		MultiVal *val = &expr->postfixEArray[i];

		if(val->type==MULTIOP_PAREN_OPEN)
		{
			openParenIdx = i;
			break;
		}
		else if(val->type==MULTIOP_PAREN_CLOSE)
		{
			closeParenIdx = i;
		}
	}

	estrDestroy(&estr);

	if(openParenIdx==-1 || closeParenIdx==-1)
		return NULL;

	paramIndex+=1;
	if(openParenIdx+paramIndex<0 || openParenIdx+paramIndex>beaSize(&expr->postfixEArray))
		return NULL;

	return &expr->postfixEArray[openParenIdx+paramIndex];
}

// This is not for general use, please at least ask Raoul before using this
int exprIsNonGenerated(Expression* expr)
{
	return expr && !beaSize(&expr->postfixEArray);
}

ParseTable parse_ExprLine_StructParam[] =
{
	{ "ExprLine", 	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(ExprLine), 0, NULL, 0 },
	{ "",			TOK_STRING(ExprLine, descStr, 0), NULL },
	{ "origStr",	TOK_STRUCTPARAM | TOK_STRING(ExprLine, origStr, 0), NULL },
	{ "\n",			TOK_END, 0 },
	{ "", 0, 0 }
};

ParseTable parse_Expression_StructParam[] =
{
	{ "Expression", 	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(Expression), 0, NULL, 0 },
	{ "",			TOK_MULTIBLOCKARRAY(Expression, postfixEArray), NULL},
	{ "lines",		TOK_EDIT_ONLY | TOK_STRUCTPARAM | TOK_STRUCT(Expression, lines, parse_ExprLine_StructParam) },
	{ "filename",	TOK_EDIT_ONLY | TOK_POOL_STRING | TOK_CURRENTFILE(Expression, filename), NULL},
	{ "",			TOK_EDIT_ONLY | TOK_F32(Expression, cost, 0), NULL },
	{ "\n",			TOK_END, 0 },
	{ "", 0, 0 }
};

AUTO_RUN;
void initParseExpressionBlock(void)
{
	ParserBinRegisterDepValue("ExprGenerateVersion", 19);
	ParserSetTableInfo(parse_Expression_StructParam, sizeof(Expression), "Expression_StructParam", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(parse_ExprLine_StructParam, sizeof(ExprLine), "ExprLine_StructParam", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTPIUsesThreadSafeMemPool(parse_Expression_StructParam, &tsmemPoolExpression);
	ParserSetTPIUsesThreadSafeMemPool(parse_ExprLine_StructParam, &tsmemPoolExprLine);

	ParserSetTableNotLegalForWriting(parse_Expression_StructParam, "You are trying to write something out with parse_Expression_StructParam. This is not legal. You need to set up your parent struct with a REDUNDANT_STRUCT. Talk to an appropriate programmer. Programmers: talk to Raoul or Alex or Ben Z");


	//make sure that EXPR_MAX_ALLOWED_ARGS is correct
	{
		ExprFuncDesc dummyFunc;
		assertmsgf(ARRAY_SIZE(dummyFunc.args) == EXPR_MAX_ALLOWED_ARGS, "EXPR_MAX_ALLOWED_ARGS is out of sync with the numeric constants definining ExprFuncDesc array sizes. Please fix");
		assertmsgf(ARRAY_SIZE(dummyFunc.tags) == EXPR_MAX_ALLOWED_TAGS, "EXPR_MAX_ALLOWED_ARGS is out of sync with the numeric constants definining ExprFuncDesc array sizes. Please fix");
		assertmsgf(EXPR_MAX_ALLOWED_ARGS == CMDMAXARGS, "EXPR_MAX_ALLOWED_ARGS and CMDMAXARGS are different... this means structparser won't be able to properly check for invalid expr funcs. Please fix");
	}
}

AUTO_STARTUP(Expression) ASTRT_DEPS(ExpressionSC);
void exprStartupLimits(void)
{
}

char* exprGenerateErrorMsg(ExprContext* context, bool noReport, bool skipExprString, bool isFileError, char* msg, ...)
{
	static char* errorStr = NULL;

	VA_START(ap, msg);

	if(!skipExprString)
		estrPrintf(&errorStr, "Expression generated an error:\n");
	else
		estrClear(&errorStr);
	estrConcatfv(&errorStr, msg, ap);

#ifdef EXPR_DEBUG_INFO
	estrConcatf(&errorStr, EXPR_DEBUG_INFO_PRINTF_STR, EXPR_DEBUG_INFO_PRINTF_PARAMS);
#endif

	estrConcatf(&errorStr, "\nExpression body:\n %s", exprGetCompleteString(context->curExpr));

	if (!context->silentErrors && !noReport && context && context->curExpr)
	{	
		ErrorFilenamef(isFileError ? context->curExpr->filename : NULL, "%s", errorStr);
	}

	VA_END();

	return errorStr;
}

#include "Expression_h_ast.c"
#include "ExpressionPrivate_h_ast.c"
#include "ExpressionMinimal_h_ast.c"
