#include "BlockEarray.h"
#include "file.h"

#define PERFINFO_EXPR_DETAILED_START(name, count) 
#define PERFINFO_EXPR_DETAILED_STOP() 

#define EXPR_DEBUG_PRINTF(str, ...) 

#define RETURN_ERROR(errorStr) \
{ \
	if(answer) \
	{ \
		answer->type = MULTI_INVALID; \
		answer->str = errorStr; \
	} \
	PERFINFO_AUTO_STOP(); \
	return; \
}
 
#define EXPRESSIONASSERT(test)
#define EXPRESSIONASSERTMSG(test, msg, ...)
//#define EXPRESSIONASSERT(test) ((test) || devassertmsg(test, exprGenerateErrorMsg(context, true, false, true, #test)))
//#define EXPRESSIONASSERTMSG(test, msg, ...) ((test) || devassertmsg(test, exprGenerateErrorMsg(context, true, false, true, msg, ##__VA_ARGS__)))

__forceinline static MultiVal* stackPop(ExprContext* context, MultiVal stack[EXPR_STACK_SIZE], S32* stackidx)
{
	EXPRESSIONASSERT(*stackidx >= 0 && *stackidx < EXPR_STACK_SIZE);
	return &stack[(*stackidx)--];
}

#define ISFLOAT(val) ((val)->type == MULTI_FLOAT)
#define ISINT(val) ((val)->type == MULTI_INT)
#define ISNUMBER(val) ((val)->type == MULTI_INT || (val)->type == MULTI_FLOAT)
#define ISSTRING(val) ((val)->type == MULTI_STRING)
#define ISPOINTER(val) ((val)->type == MULTI_NP_POINTER)
#define ISIDENTIFIER(val) ((val)->type == MULTI_IDENTIFIER)

__forceinline static F64 getFloat(ExprContext* context, MultiVal* val)
{
	if(val->type == MULTI_INT)
		return val->intval;
	else if(val->type == MULTI_FLOAT)
		return val->floatval;
	else
	{
		EXPRESSIONASSERTMSG(0, "MultiValType cannot be cast to a float");
		return 0;
	}
}

__forceinline static S64 getInt(ExprContext* context, MultiVal* val)
{
	EXPRESSIONASSERT(val->type == MULTI_INT || val->type == MULTI_NP_POINTER);
	if (val->type == MULTI_INT)
		return val->intval;
	else
		return !!((ExprVarEntry*)val->ptr)->inptr;
}

__forceinline static int exprMultiValMatchesType(ExprContext* context, MultiVal* val, MultiValType type)
{
	return val->type == type || (type == MULTI_FLOAT && val->type == MULTI_INT);
}

#if STATIC_CHECKING_HYBRID

#define IS_STATIC_CHECK() context->staticCheck
#define SC_FUNC_NAME(funcname) funcname

#elif STATIC_CHECKING_ON

#define IS_STATIC_CHECK() 1

#define SC_FUNC_NAME(funcname) funcname##SC

#else

#define IS_STATIC_CHECK() 0

#define SC_FUNC_NAME(funcname) funcname

#endif

#if STATIC_CHECKING_HYBRID | STATIC_CHECKING_ON

	#define SC_PUSH_PARAMS , ExprSCInfo scStack[EXPR_STACK_SIZE], int clearSCInfo, int isConstant

	__forceinline static ExprSCInfo* SCGetInfoFunc(ExprContext* context, S32 stackidx)
	{
		if(IS_STATIC_CHECK())
			return &context->scInfoStack[stackidx];
		else
			return NULL;
	}

	__forceinline static void SCSetTopConstantFunc(ExprContext* context, S32 stackidx, int isConstant)
	{
		if(IS_STATIC_CHECK())
			context->scInfoStack[stackidx].isConstant = !!isConstant;
	}

	__forceinline static int SCIsConstant(ExprContext* context, S32 stackidx)
	{
		if(IS_STATIC_CHECK())
			return context->scInfoStack[stackidx].isConstant;
	}

	#define SCGetInfo(context, stackidx) SCGetInfoFunc(context, stackidx)
	#define SCSetTopConstant(context, stackidx, isConstant) SCSetTopConstantFunc(context, stackidx, isConstant)
	#define SCIsConstant(context, stackidx) SCIsConstant(context, stackidx)
	#define SCConstantFromInputs(context, stackidx1, stackidx2, result) result = SCIsConstant(context, stackidx1) && SCIsConstant(context, stackidx2)
	#define SCConstantFromInput(context, stackidx1, result) result = !!SCIsConstant(context, stackidx1)

	#define STATIC_CHECK(test, msg, ...)  \
	{ \
		if(IS_STATIC_CHECK() && !(test)) \
		{ \
			char* SCerror = exprGenerateErrorMsg(context, false, false, true, msg, ##__VA_ARGS__); \
			context->staticCheckError = true; \
			RETURN_ERROR(SCerror); \
		} \
	}

	#define STATIC_CHECK_NON_CRITICAL(test, msg, ...) \
	{ \
		if(!(test)) \
		{ \
			char* SCerror = exprGenerateErrorMsg(context, false, false, true, msg, ##__VA_ARGS__); \
		} \
	}

	#define EXPRESSION_ERROR(test, msg, ...) \
	{ \
		if(!(test)) \
		{ \
			if(IS_STATIC_CHECK()) \
			{ \
				STATIC_CHECK(test, msg, ##__VA_ARGS__) \
			} \
			else \
			{ \
				char* errorStr = exprGenerateErrorMsg(context, false, false, true, msg, ##__VA_ARGS__); \
				RETURN_ERROR(errorStr); \
			} \
		} \
	}

#else // not static checking

	#define SC_PUSH_PARAMS

	#define SCGetInfo(context, stackidx) NULL
	#define SCSetTopConstant(context, stackidx, isConstant)
	#define SCIsConstant(context, stackidx) 0
	#define SCConstantFromInputs(context, stackidx1, stackidx2, result)
	#define SCConstantFromInput(context, stackidx1, result)

	#define STATIC_CHECK(test, msg, ...)
	#define STATIC_CHECK_NON_CRITICAL(test, msg, ...)

	#define EXPRESSION_ERROR(test, msg, ...) \
	{ \
		if(!(test)) \
		{ \
			char* errorStr = exprGenerateErrorMsg(context, false, false, true, msg, ##__VA_ARGS__); \
			RETURN_ERROR(errorStr); \
		} \
	}

#endif


__forceinline static void SC_FUNC_NAME(stackPushInternal)(ExprContext* context, MultiVal stack[EXPR_STACK_SIZE],
											S32* __restrict stackidx, MultiValType type, U64 val SC_PUSH_PARAMS)
{
	(*stackidx)++;
	EXPRESSIONASSERT(*stackidx >= 0 && *stackidx < EXPR_STACK_SIZE);
#if STATIC_CHECKING_HYBRID | STATIC_CHECKING_ON
	if(IS_STATIC_CHECK())
	{
		if(clearSCInfo)
			memset(scStack + *stackidx, 0, sizeof(scStack[0]));
		scStack[*stackidx].isConstant = isConstant;
	}
#endif
	stack[*stackidx].intval = val;
	stack[*stackidx].type = type;
}

__forceinline static void SC_FUNC_NAME(stackPushIntInternal)(ExprContext* context, MultiVal stack[EXPR_STACK_SIZE],
											S32* __restrict stackidx, U64 val SC_PUSH_PARAMS)
{
	(*stackidx)++;
	EXPRESSIONASSERT(*stackidx >= 0 && *stackidx < EXPR_STACK_SIZE);
#if STATIC_CHECKING_HYBRID | STATIC_CHECKING_ON
	if(IS_STATIC_CHECK())
	{
		if(clearSCInfo)
			memset(scStack + *stackidx, 0, sizeof(scStack[0]));
		scStack[*stackidx].isConstant = isConstant;
	}
#endif
	stack[*stackidx].intval = val;
	stack[*stackidx].type = MULTI_INT;
}

__forceinline static void SC_FUNC_NAME(stackPushFloatInternal)(ExprContext* context, MultiVal stack[EXPR_STACK_SIZE],
											S32* __restrict stackidx, F64 val SC_PUSH_PARAMS)
{
	(*stackidx)++;
	EXPRESSIONASSERT(*stackidx >= 0 && *stackidx < EXPR_STACK_SIZE);
#if STATIC_CHECKING_HYBRID | STATIC_CHECKING_ON
	if(IS_STATIC_CHECK())
	{
		if(clearSCInfo)
			memset(scStack + *stackidx, 0, sizeof(scStack[0]));
		scStack[*stackidx].isConstant = isConstant;
	}
#endif
	stack[*stackidx].floatval = val;
	stack[*stackidx].type = MULTI_FLOAT;
}

__forceinline static void SC_FUNC_NAME(stackPushPointerInternal)(ExprContext* context, MultiVal stack[EXPR_STACK_SIZE],
											S32* __restrict stackidx, MultiValType type, void* ptr SC_PUSH_PARAMS)
{
	(*stackidx)++;
	EXPRESSIONASSERT(*stackidx >= 0 && *stackidx < EXPR_STACK_SIZE);
#if STATIC_CHECKING_HYBRID | STATIC_CHECKING_ON
	if(IS_STATIC_CHECK())
	{
		if(clearSCInfo)
			memset(scStack + *stackidx, 0, sizeof(scStack[0]));
		scStack[*stackidx].isConstant = isConstant;
	}
#endif
	stack[*stackidx].ptr = ptr;
	stack[*stackidx].type = type;
}

#if STATIC_CHECKING_HYBRID | STATIC_CHECKING_ON

#define stackPush(context, stack, stackidx, val) SC_FUNC_NAME(stackPushInternal)(context, stack, stackidx, (val)->type, (val)->intval, context->scInfoStack, true, staticVal)
#define stackPushKeepSCInfo(context, stack, stackidx, val) SC_FUNC_NAME(stackPushInternal)(context, stack, stackidx, (val)->type, (val)->intval, context->scInfoStack, false, staticVal)
#define stackPushFloat(context, stack, stackidx, val) SC_FUNC_NAME(stackPushFloatInternal)(context, stack, stackidx, val, context->scInfoStack, true, staticVal)
#define stackPushInt(context, stack, stackidx, val) SC_FUNC_NAME(stackPushIntInternal)(context, stack, stackidx, val, context->scInfoStack, true, staticVal)
#define stackPushString(context, stack, stackidx, ptr) SC_FUNC_NAME(stackPushPointerInternal)(context, stack, stackidx, MULTI_STRING, ptr, context->scInfoStack, true, staticVal)
#define stackPushPointer(context, stack, stackidx, ptr) SC_FUNC_NAME(stackPushPointerInternal)(context, stack, stackidx, MULTI_NP_POINTER, ptr, context->scInfoStack, true, staticVal)

#else

#define stackPush(context, stack, stackidx, val) SC_FUNC_NAME(stackPushInternal)(context, stack, stackidx, (val)->type, (val)->intval)
#define stackPushKeepSCInfo(context, stack, stackidx, val) SC_FUNC_NAME(stackPushInternal)(context, stack, stackidx, (val)->type, (val)->intval)
#define stackPushFloat(context, stack, stackidx, val) SC_FUNC_NAME(stackPushFloatInternal)(context, stack, stackidx, val)
#define stackPushInt(context, stack, stackidx, val) SC_FUNC_NAME(stackPushIntInternal)(context, stack, stackidx, val)
#define stackPushString(context, stack, stackidx, ptr) SC_FUNC_NAME(stackPushPointerInternal)(context, stack, stackidx, MULTI_STRING, ptr)
#define stackPushPointer(context, stack, stackidx, ptr) SC_FUNC_NAME(stackPushPointerInternal)(context, stack, stackidx, MULTI_NP_POINTER, ptr)

#endif

#define EXPR_BINARY_OPERATOR_TOP(name) \
	MultiVal* rhs; \
	MultiVal* lhs; \
	STATIC_CHECK(stackidx >= 1, name " requires two arguments"); \
	rhs = stackPop(context, stack, &stackidx); \
	lhs = stackPop(context, stack, &stackidx); \
	SCConstantFromInputs(context, stackidx+1, stackidx+2, staticVal)

#define EXPR_UNARY_OPERATOR_TOP(name) \
	MultiVal* val; \
	STATIC_CHECK(stackidx >= 0, name " requires one argument");\
	val = stackPop(context, stack, &stackidx); \
	SCConstantFromInput(context, stackidx+1, staticVal)

__forceinline static void SC_FUNC_NAME(exprEvaluateInternal)(const MultiVal* exprEArray, ExprContext* context, ExprFuncTable* funcTable,
	MultiVal* answer, int inputlen, MultiVal stack[EXPR_STACK_SIZE], int stackidxIn,
	int* stackidxOut, int* instrPtrOut, int deepCopyAnswer MEM_DBG_PARMS)
{
	int stackidx = stackidxIn;
	int instrIdx;
	int continueExecution = true;
	int continuation = false;

	PERFINFO_AUTO_START("exprEvaluateInternal", 1);

#ifdef EXPR_DEBUG_INFO
	context->exprFile = caller_fname;
	context->exprLine = line;
#endif

	if(exprCurAutoTrans)
	{
		ErrorDetailsf("Called from %s:%d", caller_fname, line); 
		Errorf("Expression evaluated from inside transaction %s, which is not allowed", exprCurAutoTrans);
		devassertmsgf(0,"Expression evaluated from inside transaction %s, which is not allowed", exprCurAutoTrans);
	}

	if(!context->funcTable)
	{
		Errorf("exprEvaluateInternal called from %s:%d without a funcTable specified" EXPR_DEBUG_INFO_PRINTF_STR,
			caller_fname, line, EXPR_DEBUG_INFO_PRINTF_PARAMS);
	}
	else
	{
#ifdef EXPRESSION_STRICT
		if(context->funcTable->requireSelfPtr)
		{
			// have to unroll the if to stop the "constant && <expression> is the same as <expression>"
			// compiler warning
#if STATIC_CHECKING_ON
			if(!context->staticCheckWillHaveSelfPtr)
#elif !STATIC_CHECKING_ON
			if(!context->selfPtr)
#else
			if(!IS_STATIC_CHECK() && !context->selfPtr || IS_STATIC_CHECK() && !context->staticCheckWillHaveSelfPtr)
#endif
			{
				if (IS_STATIC_CHECK()) {
					exprGenerateErrorMsg(context, false, true, false, "Self Pointer required but not marked SetAllow on context during expression generate (file: %s:%d, because of tag %s)", caller_fname, line, context->funcTable->selfPtrTag->name);
				} else {
					exprGenerateErrorMsg(context, false, true, false, "Self Pointer required but not set during expression evaluate (file: %s:%d, because of tag %s)", caller_fname, line, context->funcTable->selfPtrTag->name);
				}
			}
		}
		if(context->funcTable->requirePartition)
		{
#if STATIC_CHECKING_ON
			if(!context->staticCheckWillHavePartition)
#elif !STATIC_CHECKING_ON
			if(!context->partitionIsSet)
#else
			if(!IS_STATIC_CHECK() && !context->partitionIsSet || IS_STATIC_CHECK() && !context->staticCheckWillHavePartition)
#endif
			{
				if (IS_STATIC_CHECK()) {
					exprGenerateErrorMsg(context, false, true, false, "Partition required but not marked SetAllow on context during expression generate (file: %s:%d, because of tag %s)", caller_fname, line, context->funcTable->partitionTag->name);
				} else {
					exprGenerateErrorMsg(context, false, true, false, "Partition required but not set during expression evaluate (file: %s:%d, because of tag %s)", caller_fname, line, context->funcTable->partitionTag->name);
				}
			}
		}
#endif
	}
	
	EXPRESSION_ERROR(inputlen, "Trying to execute empty expression! (Expression not generated?)");
	EXPRESSIONASSERTMSG(stackidxIn >= -1 && stackidxIn < EXPR_STACK_SIZE, "Invalid stack ptr passed in");
	EXPRESSIONASSERTMSG(context, "Expressions need a context to evaluate with");

	if(instrPtrOut)
		instrIdx = *instrPtrOut;
	else
		instrIdx = 0;

	for(; instrIdx < inputlen && continueExecution; instrIdx++)
	{
		const MultiVal* curVal = exprEArray + instrIdx;
		MMOper op = MULTI_GET_OPER(curVal->type);
#if STATIC_CHECKING_HYBRID | STATIC_CHECKING_ON
		int staticVal = false;
#endif

		switch(op)
		{
		case MMO_NONE:
		case MMO_PAREN_OPEN:
		case MMO_PAREN_CLOSE:
#if STATIC_CHECKING_HYBRID | STATIC_CHECKING_ON
			staticVal = true;
#endif
			if(curVal->type == MULTI_STRING && !curVal->str)
				stackPushString(context, stack, &stackidx, "");
			else
				stackPush(context, stack, &stackidx, curVal);
			break;
		case MMO_LOCATION:
		{
			MultiVal val;
			Vec3* loc = ScratchStackAlloc(&context->scratch, sizeof(Mat4));
			if(exprMat4FromLocationString(context, curVal->str, loc,
				IS_STATIC_CHECK(), context->curExpr ? context->curExpr->filename : ""))
			{
				val.ptr = loc;
				val.type = MULTIOP_LOC_MAT4;
				stackPush(context, stack, &stackidx, &val);
			}
			else
			{
				EXPRESSION_ERROR(0, "%s is not a valid location", curVal->str);
			}

			break;
		}
		case MMO_IDENTIFIER:
		{
			ExprVarEntry* varEntry = NULL;
			ExprContext* tmpcontext = context;
			bool useUserPtr = (context->userPtrIsDefaultIdentifier && context->userPtr && curVal->str[0] == '\0');

			if (useUserPtr)
			{
				varEntry = ScratchStackAlloc(&context->scratch, sizeof(*varEntry));
				varEntry->name = "userPtr";
				varEntry->inptr = context->userPtr;
				varEntry->table = context->userTable;
				varEntry->allowVarAccess = true;
				varEntry->allowObjPath = true;

				stackPushPointer(context, stack, &stackidx, varEntry);
			}
			else 
			{
				PERFINFO_EXPR_DETAILED_START("var lookup", 1);
				while (tmpcontext && !stashAddressFindPointer(tmpcontext->varTable, curVal->str, &varEntry))
					tmpcontext = tmpcontext->parent;
				PERFINFO_EXPR_DETAILED_STOP();

				if(varEntry)
				{
					if(varEntry->simpleVar.type)
						stackPush(context, stack, &stackidx, &varEntry->simpleVar);
					else
					{
						// Can now have NULL pointer variables passed into functions as long as the
						// function is annotated with ALLOW_NULL
						//devassertmsg(varEntry->inptr || context->staticCheck, "Not allowed to have NULL lookup entry pointers other than for static checking");
						stackPushPointer(context, stack, &stackidx, varEntry);
					}
				}
				else
					EXPRESSION_ERROR(0, "Variable %s not found on context", curVal->str);
			}
			break;
		}
		case MMO_ROOT_PATH:
		{
			ParseTable* table = NULL;
			int column;
			void* ptr;
			char* path = (char*)curVal->str;
			int resolveRootPath;

			PERFINFO_EXPR_DETAILED_START("resolveRootPath", 1);
			resolveRootPath = ParserResolveRootPath(path, NULL, &table, &column, &ptr, NULL, 0);
			PERFINFO_EXPR_DETAILED_STOP();

			if(resolveRootPath || (IS_STATIC_CHECK() && table))
			{
				ExprVarEntry* varEntry = NULL;

				// not sure what column is supposed to mean as there's no column going 
				// in to objPathResolveField
				varEntry = ScratchStackAlloc(&context->scratch, sizeof(*varEntry));
				varEntry->name = "RootPath";
				varEntry->inptr = ptr;
				varEntry->table = table;
				varEntry->allowVarAccess = true;
				varEntry->allowObjPath = true;
				stackPushPointer(context, stack, &stackidx, varEntry);
			}
			else
				EXPRESSION_ERROR(0, "%s does not resolve to a valid object path base or variable (did you forget the quotes around a string?)", curVal->str);
			break;
		}
		case MMO_STATICVAR:
		{
			ExprVarEntry* varEntry = NULL;
			ExprContext *tmpcontext = context;

			while (tmpcontext && !(varEntry = tmpcontext->staticVars[curVal->int32]))
				tmpcontext = tmpcontext->parent;

			if(varEntry)
			{
				if(varEntry->simpleVar.type)
					stackPush(context, stack, &stackidx, &varEntry->simpleVar);
				else
				{
					// Can now have NULL pointer variables passed into functions as long as the
					// function is annotated with ALLOW_NULL
					//devassertmsg(varEntry->inptr || context->staticCheck, "Not allowed to have NULL lookup entry pointers other than for static checking");
					stackPushPointer(context, stack, &stackidx, varEntry);
				}
			}
			else
				EXPRESSION_ERROR(0, "%s is used in an expression for which the context does not have the variable", exprGetStaticVarName(curVal->int32));

			break;
		}
		case MMO_OBJECT_PATH:
		{
			MultiVal* basePath = stackPop(context, stack, &stackidx);
			MultiVal outVal = {0};
			ExprVarEntry* objPathBaseInfo;
			void *inPtr;

			ParseTable* outTable;
			int outColumn;
			void* outPtr;
			int outIdx;
			ObjectPath* objPathOut = NULL;
			ObjectPath** objPathOutPtr;
			U32 objPathFlags = OBJPATHFLAG_TRAVERSEUNOWNED | OBJPATHFLAG_SEARCHNONINDEXED | OBJPATHFLAG_DONTLOOKUPROOTPATH | OBJPATHFLAG_REPORTWHENENCOUNTERINGNULLPOLY;
			char* pathResult = NULL;
			char** pathResultPtr;

			STATIC_CHECK(basePath->type == MULTI_NP_POINTER, "Can't use a relative object path without a path base");
			
			EXPRESSIONASSERTMSG(curVal->str[0] == '.', "The first part of this object path should have been put in its own multival by the parser...");

			objPathBaseInfo = (ExprVarEntry*) basePath->ptr;

			if(IS_STATIC_CHECK())
			{
				estrStackCreate(&pathResult);
				inPtr = NULL;
				pathResultPtr = &pathResult;
				objPathOutPtr = &objPathOut;
			}
			else
			{
				EXPRESSION_ERROR(objPathBaseInfo->inptr, "Object path base %s is NULL", objPathBaseInfo->name);

				inPtr = objPathBaseInfo->inptr;
				pathResultPtr = NULL;
				objPathOutPtr = NULL;
			}

			PERFINFO_EXPR_DETAILED_START("ObjPath traversal", 1);

			if(ParserResolvePathEx(curVal->str, objPathBaseInfo->table, inPtr, &outTable, &outColumn, &outPtr, &outIdx, objPathOutPtr, pathResultPtr, NULL, NULL, objPathFlags))
			{
				//RAOUL see if what I'm doing with iRetVal here looks right
				int iRetVal;

				if(IS_STATIC_CHECK() && pathResult)
					estrDestroy(&pathResult);

				iRetVal = FieldToMultiVal(outTable, outColumn, outPtr, outIdx, &outVal, false, true);
				EXPRESSION_ERROR(iRetVal, "Failed FieldToMultiVal on succeeded ParserResolvePath for %s", curVal->str);
				if(IS_STATIC_CHECK())
				{
					ParseTable** outTables = NULL;
					int i;
					ObjectPathGetParseTables(objPathOut, inPtr, &outTables, NULL, objPathFlags);
					for(i = eaSize(&outTables)-1; i >= 0; i--)
					{
						EXPR_DEBUG_PRINTF("Adding parsetable %s for path %s\n", outTables[i]->name, curVal->str);
						resAddParseTableDep(outTables[i]);
					}

					eaDestroy(&outTables);
					EXPR_DEBUG_PRINTF("\n");
				}
			}
#if STATIC_CHECKING_HYBRID | STATIC_CHECKING_ON
			else if(IS_STATIC_CHECK() && (!pathResult || stricmp(SPECIAL_NULL_POLY_ERROR_STRING, pathResult)))
			{
				char error[4000];
				char possibly[4000] = "no alternative found";
				char* outPath = NULL;
				bool resolveSuccess = false;
				PERFINFO_EXPR_DETAILED_START("Misspelled?", 1);
				if (!pathResult)
					estrStackCreate(&pathResult);
				estrStackCreate(&outPath);
				resolveSuccess = ParserResolvePath((char*)curVal->str, objPathBaseInfo->table, inPtr, &outTable, &outColumn, &outPtr, &outIdx, &pathResult, &outPath, objPathFlags | OBJPATHFLAG_TOLERATEMISSPELLEDFIELDS);
				sprintf(error, "%s", NULL_TO_EMPTY(pathResult));
				sprintf(possibly, "%s", NULL_TO_EMPTY(outPath));
				estrDestroy(&pathResult);
				estrDestroy(&outPath);
				PERFINFO_EXPR_DETAILED_STOP();
				PERFINFO_EXPR_DETAILED_STOP();
				EXPRESSION_ERROR(0, "Failed to resolve object path: %s (Error was: %s; Suggestion: %s)", curVal->str, error, possibly);
			}
#endif
			else if (context->allowInvalidPaths)
			{
				MultiValReferenceString(&outVal, "");
			}

			else
			{
				estrDestroy(&pathResult);
				PERFINFO_EXPR_DETAILED_STOP();
				if(IS_STATIC_CHECK())
				{
					PERFINFO_AUTO_STOP();
					return;
				}
				else
					EXPRESSION_ERROR(0, "Failed to resolve object path:%s", curVal->str);
			}

			PERFINFO_EXPR_DETAILED_STOP();

			if(outVal.type == MULTI_STRING && !outVal.str)
			{
				// textparser can't tell the difference between "" and NULL - if
				// you define a field as "" in a file it'll get turned into NULL
				// when read. So if we find a NULL string at the end of an object
				// path, it's almost always meant to be empty instead.
				outVal.str = "";
			}

			if(outVal.type == MULTI_NP_POINTER)
			{
				ExprVarEntry* entry = ScratchStackAlloc(&context->scratch, sizeof(*entry));
				entry->table = outColumn >= 0 ? outTable[outColumn].subtable : NULL;
				entry->inptr = outVal.ptr_noconst;
				entry->allowVarAccess = true;
				entry->name = "ObjPathReturnPtr";
				outVal.ptr = entry;
			}

			stackPush(context, stack, &stackidx, &outVal);
			break;
		}
		case MMO_BRACE_OPEN:
		{
			S64 count = 0;
			MultiVal stackPtr;

			stackPtr.type = MULTIOP_NP_STACKPTR;
			stackPtr.ptr = exprEArray + instrIdx+1;
			stackPush(context, stack, &stackidx, &stackPtr);

			// everything between braces is specifically not evaluated
			while(++instrIdx < inputlen && exprEArray[instrIdx].type != MULTIOP_BRACE_CLOSE)
			{
				count++;
			}

			if(instrIdx == inputlen)
			{
				STATIC_CHECK(0, "Closing brace not found");
			}
			stackPushInt(context, stack, &stackidx, count);
			break;
		}
		// this is specifically not handled because open should eat up to and including close
		//case MULTIOP_BRACE_CLOSE:
		case MMO_ADD:
		{
			EXPR_BINARY_OPERATOR_TOP("Add");

			STATIC_CHECK((ISNUMBER(rhs) && ISNUMBER(lhs)) || (ISSTRING(rhs) && ISSTRING(lhs)),
				"Add only supports two numbers or two strings");

			if (ISSTRING(rhs) && ISSTRING(lhs))
			{
				size_t sz1 = strlen(rhs->str);
				size_t sz2 = strlen(lhs->str);
				size_t sz = sz1 + sz2 + 1;
				char *result = exprContextAllocScratchMemory(context, sz);
				strcpy_s(result, sz, lhs->str);
				strcat_s(result, sz, rhs->str);
				stackPushString(context, stack, &stackidx, result);
			}
			else if(ISFLOAT(rhs) || ISFLOAT(lhs))
				stackPushFloat(context, stack, &stackidx, getFloat(context, rhs) + getFloat(context, lhs));
			else
				stackPushInt(context, stack, &stackidx, getInt(context, rhs) + getInt(context, lhs));
			break;
		}
		case MMO_SUBTRACT:
		{
			EXPR_BINARY_OPERATOR_TOP("Subtract");

			STATIC_CHECK(ISNUMBER(lhs), "Subtract only supports numbers");
			STATIC_CHECK(ISNUMBER(rhs), "Subtract only supports numbers");

			if(ISFLOAT(lhs) || ISFLOAT(rhs))
				stackPushFloat(context, stack, &stackidx, getFloat(context, lhs) - getFloat(context, rhs));
			else
				stackPushInt(context, stack, &stackidx, getInt(context, lhs) - getInt(context, rhs));
			break;
		}
		case MMO_NEGATE:
		{
			EXPR_UNARY_OPERATOR_TOP("Negate");

			STATIC_CHECK(ISNUMBER(val), "Negate only supports numbers");

			if(ISFLOAT(val))
				stackPushFloat(context, stack, &stackidx, getFloat(context, val) * -1);
			else
				stackPushInt(context, stack, &stackidx, getInt(context, val) * -1);
			break;
		}
		case MMO_MULTIPLY:
		{
			EXPR_BINARY_OPERATOR_TOP("Multiply");

			STATIC_CHECK(ISNUMBER(rhs), "Multiply only supports numbers");
			STATIC_CHECK(ISNUMBER(lhs), "Multiply only supports numbers");

			if(ISFLOAT(rhs) || ISFLOAT(lhs))
				stackPushFloat(context, stack, &stackidx, getFloat(context, rhs) * getFloat(context, lhs));
			else
				stackPushInt(context, stack, &stackidx, getInt(context, rhs) * getInt(context, lhs));
			break;
		}
		case MMO_DIVIDE:
		{
			EXPR_BINARY_OPERATOR_TOP("Divide");

			STATIC_CHECK(ISNUMBER(lhs), "Divide only supports numbers");
			STATIC_CHECK(ISNUMBER(rhs), "Divide only supports numbers");

			if(ISINT(rhs) && !getInt(context, rhs))
			{
				if(IS_STATIC_CHECK())
				{
					stackPushInt(context, stack, &stackidx, 1);
					break;
				}
				else
					EXPRESSION_ERROR(getInt(context, rhs) != 0, "Cannot divide by zero!");
			}
			else if(!ISINT(rhs) && !getFloat(context, rhs))
			{
				if(IS_STATIC_CHECK())
				{
					stackPushFloat(context, stack, &stackidx, 1);
					break;
				}
				else
					EXPRESSION_ERROR(getFloat(context, rhs) != 0, "Cannot divide by zero!");
			}

			if(ISINT(rhs) && ISINT(lhs) && !(getInt(context, lhs) % getInt(context, rhs)))
				stackPushInt(context, stack, &stackidx, getInt(context, lhs) / getInt(context, rhs));
			else
				stackPushFloat(context, stack, &stackidx, getFloat(context, lhs) / getFloat(context, rhs));
			break;
		}
		case MMO_EXPONENT:
		{
			EXPR_BINARY_OPERATOR_TOP("Exponent");

			STATIC_CHECK(ISNUMBER(lhs), "Exponent only supports numbers");
			STATIC_CHECK(ISNUMBER(rhs), "Exponent only supports numbers");

			if(ISFLOAT(lhs) || ISFLOAT(rhs))
			{
				F64 flhs = getFloat(context, lhs);
				F64 frhs = getFloat(context, rhs);

				STATIC_CHECK(flhs >= 0 || frhs >= 1, "Exponentiation only supports positive bases with fractional exponents");
				stackPushFloat(context, stack, &stackidx, pow(flhs, frhs));
			}
			else
			{
				S64 llhs = getInt(context, lhs);
				S64 lrhs = getInt(context, rhs);

				STATIC_CHECK(llhs >= 0 || lrhs >= 1, "Exponentiation only supports positive bases with fractional exponents");
				stackPushInt(context, stack, &stackidx, pow(llhs, lrhs));
			}
			break;
		}
		case MMO_BIT_AND:
		{
			EXPR_BINARY_OPERATOR_TOP("Bitwise AND");

			STATIC_CHECK(ISINT(rhs), "Bitwise AND only supports integers");
			STATIC_CHECK(ISINT(lhs), "Bitwise AND only supports integers");

			stackPushInt(context, stack, &stackidx, getInt(context, rhs) & getInt(context, lhs));
			break;
		}
		case MMO_BIT_OR:
		{
			EXPR_BINARY_OPERATOR_TOP("Bitwise OR");

			STATIC_CHECK(ISINT(rhs), "Bitwise OR only supports integers");
			STATIC_CHECK(ISINT(lhs), "Bitwise OR only supports integers");

			stackPushInt(context, stack, &stackidx, getInt(context, rhs) | getInt(context, lhs));
			break;
		}
		case MMO_BIT_NOT:
		{
			EXPR_UNARY_OPERATOR_TOP("Bitwise NOT");

			STATIC_CHECK(ISINT(val), "Bitwise NOT only supports integers");

			stackPushInt(context, stack, &stackidx, ~getInt(context, val));
			break;
		}
		case MMO_EQUALITY:
		{
			EXPR_BINARY_OPERATOR_TOP("Equality");

			STATIC_CHECK(ISNUMBER(rhs) || ISSTRING(rhs) || ISPOINTER(rhs), "Equality only supports numbers and strings and pointers");
			STATIC_CHECK(ISNUMBER(lhs) || ISSTRING(lhs) || ISPOINTER(lhs), "Equality only supports numbers and strings and pointers");
			STATIC_CHECK(rhs->type == lhs->type || ISNUMBER(rhs) && ISNUMBER(lhs), "Equality only supports comparisons between two identical types");

			if(ISFLOAT(rhs) || ISFLOAT(lhs))
				stackPushInt(context, stack, &stackidx, nearf(getFloat(context, rhs), getFloat(context, lhs)));
			else if(ISSTRING(rhs) && ISSTRING(lhs))
				stackPushInt(context, stack, &stackidx, !stricmp(rhs->str ? rhs->str : "", lhs->str ? lhs->str : ""));
			else if(ISPOINTER(rhs) && ISPOINTER(lhs))
			{
				const ExprVarEntry *pVar1 = rhs->ptr;
				const ExprVarEntry *pVar2 = lhs->ptr;
				stackPushInt(context, stack, &stackidx, pVar1->inptr == pVar2->inptr);
			}
			else
				stackPushInt(context, stack, &stackidx, getInt(context, rhs) == getInt(context, lhs));
			break;
		}
		case MMO_LESSTHAN:
		{
			EXPR_BINARY_OPERATOR_TOP("Less than");

			STATIC_CHECK(ISNUMBER(lhs), "Less than only supports numbers");
			STATIC_CHECK(ISNUMBER(rhs), "Less than only supports numbers");

			if(ISFLOAT(lhs) || ISFLOAT(rhs))
				stackPushInt(context, stack, &stackidx, getFloat(context, lhs) < getFloat(context, rhs));
			else
				stackPushInt(context, stack, &stackidx, getInt(context, lhs) < getInt(context, rhs));
			break;
		}
		case MMO_LESSTHANEQUALS:
		{
			EXPR_BINARY_OPERATOR_TOP("Less than or equals");

			STATIC_CHECK(ISNUMBER(lhs), "Less than or equals only supports numbers");
			STATIC_CHECK(ISNUMBER(rhs), "Less than or equals only supports numbers");

			if(ISFLOAT(lhs) || ISFLOAT(rhs))
				stackPushInt(context, stack, &stackidx, getFloat(context, lhs) <= getFloat(context, rhs));
			else
				stackPushInt(context, stack, &stackidx, getInt(context, lhs) <= getInt(context, rhs));
			break;
		}
		case MMO_GREATERTHAN:
		{
			EXPR_BINARY_OPERATOR_TOP("Greater than");

			STATIC_CHECK(ISNUMBER(lhs), "Greater than only supports numbers");
			STATIC_CHECK(ISNUMBER(rhs), "Greater than only supports numbers");

			if(ISFLOAT(lhs) || ISFLOAT(rhs))
				stackPushInt(context, stack, &stackidx, getFloat(context, lhs) > getFloat(context, rhs));
			else
				stackPushInt(context, stack, &stackidx, getInt(context, lhs) > getInt(context, rhs));
			break;
		}
		case MMO_GREATERTHANEQUALS:
		{
			EXPR_BINARY_OPERATOR_TOP("Greater than or equals");

			STATIC_CHECK(ISNUMBER(lhs), "Greater than or equals only supports numbers");
			STATIC_CHECK(ISNUMBER(rhs), "Greater than or equals only supports numbers");

			if(ISFLOAT(lhs) || ISFLOAT(rhs))
				stackPushInt(context, stack, &stackidx, getFloat(context, lhs) >= getFloat(context, rhs));
			else
				stackPushInt(context, stack, &stackidx, getInt(context, lhs) >= getInt(context, rhs));
			break;
		}
		case MMO_AND:
		{
			EXPR_BINARY_OPERATOR_TOP("Logical AND");

			STATIC_CHECK(lhs->type == MULTI_INT || lhs->type == MULTI_NP_POINTER, "AND only supports integers and pointers");
			STATIC_CHECK(rhs->type == MULTI_INT || rhs->type == MULTI_NP_POINTER, "AND only supports integers and pointers");

			stackPushInt(context, stack, &stackidx, getInt(context, lhs) && getInt(context, rhs));
			break;
		}
		case MMO_OR:
		{
			EXPR_BINARY_OPERATOR_TOP("Logical OR");

			STATIC_CHECK(lhs->type == MULTI_INT || lhs->type == MULTI_NP_POINTER, "OR only supports integers and pointers");
			STATIC_CHECK(rhs->type == MULTI_INT || rhs->type == MULTI_NP_POINTER, "OR only supports integers and pointers");

			stackPushInt(context, stack, &stackidx, getInt(context, lhs) || getInt(context, rhs));
			break;
		}
		case MMO_NOT:
		{
			EXPR_UNARY_OPERATOR_TOP("Logical NOT");

			STATIC_CHECK(ISINT(val) || ISPOINTER(val), "NOT only supports integers and pointers");

			stackPushInt(context, stack, &stackidx, !getInt(context, val));
			break;
		}
		case MMO_FUNCTIONCALL:
		{
			ExprFuncDesc* funcDesc = NULL;
			int oldStackIdx = stackidx;
			MultiVal* closeParen = stackPop(context, stack, &stackidx);
			ExprContext *tmpcontext = context;

			PERFINFO_EXPR_DETAILED_START("func lookup", 1);
			if(funcTable)
				funcDesc = exprGetFuncDesc(funcTable, curVal->str);

			while (!funcDesc && tmpcontext)
			{
				funcDesc = exprGetFuncDesc(tmpcontext->funcTable, curVal->str);
				tmpcontext = tmpcontext->parent;
			}
			PERFINFO_EXPR_DETAILED_STOP();

			EXPRESSIONASSERT(closeParen->type == MULTIOP_PAREN_CLOSE);

			if(funcDesc)
			{
				MultiVal retval = {0};
				MultiVal* openParen;
				MultiVal* args[EXPR_MAX_ALLOWED_ARGS];
				ExprFuncReturnVal returnVal;
				int j;
				static char* errStr = NULL;

				//infotrackIncrementCountfInternal("exprfunc", "%s", funcDesc->funcName);

				if(IS_STATIC_CHECK())
				{
					resAddExprFuncDep(funcDesc->funcName);
					context->curExpr->cost = MAX(context->curExpr->cost, funcDesc->cost);
				}

				for(j = funcDesc->argc-1; j >= 0; j--)
				{
#if STATIC_CHECKING_HYBRID | STATIC_CHECKING_ON
					ExprSCInfo* scInfo = IS_STATIC_CHECK() ? SCGetInfo(context, stackidx) : NULL;
#endif
					MultiVal* param = stackPop(context, stack, &stackidx);
					ExprFuncArg* curArg = &funcDesc->args[j];

					if(curArg->type == MULTI_NP_ENTITYARRAY && param->type == MULTI_NP_POINTER)
					{
						const ExprVarEntry* entry = param->ptr;
						Entity* be;
						
						if(exprEvaluateRuntimeEntArrayFromLookupEntry(entry->table, entry->inptr, &be))
						{
							param->type = MULTI_NP_ENTITYARRAY;
							param->ptr = exprContextGetNewEntArray(context);

							if(be)
								eaPush((Entity***)param->ptr, be);

							// actually store this on the stack for possible continuation use
							stackPush(context, stack, &stackidx, param);
							stackPop(context, stack, &stackidx);
						}
					}
					else if(curArg->type == MULTI_NP_POINTER && param->type == MULTI_NP_ENTITYARRAY)
					{
						Entity *e = NULL;
						if(exprEvaluateRuntimeEntFromEntArray(*(Entity***)param->ptr, curArg->ptrType, &e))
						{
							ExprVarEntry* entry = ScratchStackAlloc(&context->scratch, sizeof(*entry));;
							entry->table = curArg->ptrType;
							entry->allowVarAccess = true;

							param->type = MULTI_NP_POINTER;
							param->ptr = entry;
							
							entry->inptr = e;

							stackPush(context, stack, &stackidx, param);
							stackPop(context, stack, &stackidx);
						}
					}

					EXPRESSION_ERROR(curArg->type == param->type || ISFLOAT(curArg) && ISINT(param) || curArg->type == MULTI_CMULTI,
						"Incompatible type for function parameter %s (expected %s in function %s). arg %s",
						MultiValPrint(param),
						MultiValTypeToReadableString(curArg->type),
									 funcDesc->funcName,
									 SAFE_MEMBER(curArg,name));

					if(curArg->type == MULTI_NP_POINTER)
					{
						const ExprVarEntry* var = param->ptr;
						STATIC_CHECK(curArg->ptrType, "Programmer error for function %s:"
							" Only allowed to have a pointer as function argument if you"
							" specify a parse table for it with ACMD_EXPR_POINTER_TYPE",
							funcDesc->funcName);
						STATIC_CHECK(var->allowVarAccess, "Not allowed to use variable %s"
							" in expressions", var->name);
						EXPRESSION_ERROR(curArg->ptrType == var->table, "Pointer is not of the"
							" correct type for function %s (function requires %s, argument"
							" %s is of type %s)",
							funcDesc->funcName, ParserGetTableName(curArg->ptrType),
							var->name, ParserGetTableName(var->table));
						EXPRESSION_ERROR(IS_STATIC_CHECK() || var->inptr || curArg->allowNULLPtr,
							"Passing in a NULL (currently not valid) pointer to a function"
							" that requires a valid pointer (function %s needs a valid"
							" pointer for arg %d, %s points to a NULL variable)",
							funcDesc->funcName, j+1, var->name);

						STATIC_CHECK(curArg->allowNULLPtr || !(var->SCMightBeNull),
							"Passing in a might-be-NULL pointer to a variable argument that doesn't allow NULL"
							" during static checking of function %s (argument %s)",
							funcDesc->funcName, curArg->name);
					}

#if STATIC_CHECKING_HYBRID | STATIC_CHECKING_ON
					if(IS_STATIC_CHECK())
					{
						if(curArg->staticCheckType)
						{
							if(scInfo->staticCheckCallback)
								scInfo->staticCheckCallback(context, scInfo->externVarSCUserData, curArg->staticCheckType, curArg->scTypeCategory);

							if(!scInfo->skipSCTypeCheck)
							{
								switch (curArg->scTypeCategory)
								{								
								case ExprStaticCheckCat_Reference:
								case ExprStaticCheckCat_Resource:
									{
										devassert(param->type == MULTI_STRING);									
										if (strcmp(param->str, MULTI_DUMMY_STRING))
										{
											// Dictionary indexes are not necessarily loaded in production so skip some static checking
											if (!isProductionMode() || isProductionEditMode())
											{
												bool bFound = RefSystem_ReferentFromString(curArg->staticCheckType, param->str) 
													|| resGetInfo(curArg->staticCheckType, param->str);

												if (!bFound)
												{
													char *error = exprGenerateErrorMsg(context, true, false, true, "String parameter %s was not found in Dictionary %s",
														param->str, curArg->staticCheckType);
													if (!resAddResourceDep(curArg->staticCheckType, param->str, REFTYPE_REFERENCE_TO, error))
													{	
														// If it got added as a resource dep with error, it'll get checked later
														STATIC_CHECK(0,"String parameter %s was not found in Dictionary %s",
															param->str, curArg->staticCheckType);
													}
												}
												else
												{
													resAddResourceDep(curArg->staticCheckType, param->str, REFTYPE_REFERENCE_TO, NULL);
												}
											}
										}
										break;
									}
								case ExprStaticCheckCat_Custom:
									{
										static char* errorStr = NULL;
										ExprArgumentTypeStaticCheckFunction func;
										func = exprGetArgumentTypeStaticCheckFunction(curArg->staticCheckType);

										// error handling is in exprGetArgumentTypeStaticCheckFunction now

										if(func)
										{
											STATIC_CHECK(func(context, param, &errorStr), "Parameter %s did not pass static checking (%s)", MultiValPrint(param), errorStr);
										}									
										break;
									}
								case ExprStaticCheckCat_Enum:
									{
										StaticDefineInt *pEnum = FindNamedStaticDefine(curArg->staticCheckType);										
										devassert(param->type == MULTI_STRING);
										if(pEnum)
										{
											int valid = !!StaticDefineLookup((StaticDefine*)pEnum, param->str);
											valid = valid || !stricmp(param->str, MULTI_DUMMY_STRING);
											STATIC_CHECK(valid, "Parameter %s not found in enum %s", MultiValPrint(param), curArg->staticCheckType);
										}									
										break;
									}
									break;
								}
							}
						}
					}
#endif

					if(curArg->type == MULTI_NP_POINTER)
					{
						const ExprVarEntry* var = param->ptr;

						EXPRESSION_ERROR(IS_STATIC_CHECK() || var->inptr || curArg->allowNULLPtr,
							"Passing in a NULL (currently not valid) pointer to a function"
							" that requires a valid pointer (function %s needs a valid"
							" pointer for arg %d, %s points to a NULL variable)",
							funcDesc->funcName, j+1, var->name);
					}

					args[j] = param;
				}

				openParen = stackPop(context, stack, &stackidx);
				STATIC_CHECK(openParen->type == MULTIOP_PAREN_OPEN, "Too many parameters passed in to function %s", funcDesc->funcName);

				estrClear(&errStr);

#if STATIC_CHECKING_HYBRID
				if(!IS_STATIC_CHECK())
				{
#endif
#if !STATIC_CHECKING_ON | STATIC_CHECKING_HYBRID
					PERFINFO_EXPR_DETAILED_START("func call", 1);
					PERFINFO_AUTO_START_STATIC(funcDesc->funcName, &funcDesc->perfInfo, 1);
					returnVal = exprCodeEvaluate_Autogen(args, &retval, context, &errStr, funcDesc, funcDesc->pExprFunc);
					PERFINFO_AUTO_STOP();
					PERFINFO_EXPR_DETAILED_STOP();
#endif
#if STATIC_CHECKING_HYBRID
				}
				else
#endif
#if STATIC_CHECKING_ON | STATIC_CHECKING_HYBRID
					 if(funcDesc->pExprStaticCheckFunc)
				{
					context->curSCInfo = &context->scInfoStack[stackidx+1];
					memset(context->curSCInfo, 0, sizeof(*context->curSCInfo));
					returnVal = exprCodeEvaluate_Autogen(args, &retval, context, &errStr, funcDesc, funcDesc->pExprStaticCheckFunc);
				}
				else
				{
					for(j = 0; j < funcDesc->argc; j++)
					{
						switch(args[j]->type)
						{
						xcase MULTI_NP_ENTITYARRAY:
							exprContextClearEntArray(context, args[j]->entarray);
						xcase MULTIOP_LOC_MAT4:
							exprContextFreeScratchMemory(context, args[j]->ptr_noconst);
						}
					}

					returnVal = ExprFuncReturnFinished;
					retval.type = funcDesc->returnType.type;
					if(retval.type == MULTI_NP_ENTITYARRAY)
					{
						// if the functions this gets passed into don't have static checking
						// functions then this might not get cleaned up correctly. Currently
						// this isn't a problem because nothing uses enough ent arrays for it to
						// run out. Should be made nicer eventually though
						retval.entarray = exprContextGetNewEntArray(context);
					}
					else if(retval.type == MULTIOP_LOC_MAT4)
					{
						retval.ptr = exprContextAllocScratchMemory(context, sizeof(Mat4));
						copyMat4(unitmat,retval.ptr_noconst);
					}
					else if(retval.type == MULTI_NP_POINTER)
					{
						ExprVarEntry *pVarEntry = retval.ptr_noconst = exprContextAllocScratchMemory(context, sizeof(ExprVarEntry));
						pVarEntry->table = funcDesc->returnType.ptrType;
						pVarEntry->SCMightBeNull = funcDesc->returnType.allowNULLPtr;
						pVarEntry->allowObjPath = true;
						pVarEntry->allowVarAccess = true;
					}
					else
						MultiValSetDummyType(&retval, retval.type);
				}
#endif

				if(returnVal == ExprFuncReturnContinuation)
				{
					devassertmsg(!IS_STATIC_CHECK(), "What does it even mean for a static checking function to return ExprFuncReturnContinuation?");
					// restore stack to its old position
					stackidx = oldStackIdx;
					instrIdx--; // put the instruction pointer back at the function call
					continuation = true;
					continueExecution = false;
				}
				else
				{
					if(returnVal == ExprFuncReturnError)
					{
						static int totalErrors = 0;
						++totalErrors;
						if(++context->numErrors <= 10 && totalErrors <= 20)
						{
							if(errStr && errStr[0])
							{
								EXPRESSION_ERROR(0, "Function %s returned an error (%s)", funcDesc->funcName, errStr);
							}
							else if(retval.type == MULTI_INVALID)
							{
								EXPRESSION_ERROR(0, "Function %s returned an error (%s)", funcDesc->funcName, retval.str);
							}
							else
							{
								EXPRESSION_ERROR(0, "Function %s returned an error", funcDesc->funcName);
							}
						}

						context->staticCheckError = true;
						RETURN_ERROR("Called function returned an error");
					}
					else if(returnVal != ExprFuncReturnFinished)
					{
						devassertmsg(0, "Unrecognized return value");
						RETURN_ERROR("Function returned unrecognized return value");
					}

					EXPRESSIONASSERTMSG(exprMultiValMatchesType(context, &retval, funcDesc->returnType.type), "Return type has to match function declaration");

					if(!IS_STATIC_CHECK())
					{
						if(funcDesc->returnType.type == MULTI_NP_POINTER && !funcDesc->returnType.allowNULLPtr)
						{
							const ExprVarEntry *pEntry = retval.ptr;
							EXPRESSIONASSERTMSG(pEntry->inptr, STACK_SPRINTF("NULL returned from function %s which doesn't allow NULL", funcDesc->funcName));
						}
					}

					if(retval.type)
						stackPushKeepSCInfo(context, stack, &stackidx, &retval);
				}
			}
			else
				EXPRESSION_ERROR(0, "Invalid function name %s", curVal->str);
			break;
		}
		case MMO_JUMP:
			// static checking just skips any jumps to check everything
			if(IS_STATIC_CHECK())
				break;

			stackidx = -1;
			// instrIdx gets incremented right after this...
			instrIdx = (int)(curVal->intval - 1);
			break;
		case MMO_JUMPIFZERO:
		{
			MultiVal* val;

			STATIC_CHECK(stackidx >= 0, "Conditional requires value");
				
			val = stackPop(context, stack, &stackidx);
			STATIC_CHECK(ISINT(val) || ISPOINTER(val), "Can only do conditionals on boolean values or pointers");

			// static checking just skips any jumps to check everything
			if(IS_STATIC_CHECK())
				break;

			if(!getInt(context, val))
			{
				stackidx = -1;
				// instrIdx gets incremented right after this...
				instrIdx = (int)(curVal->intval - 1);
			}
			break;
		}
		case MMO_RETURN:
			// static checking skips returns
			if(IS_STATIC_CHECK())
				break;

			continueExecution = false;
			break;
		case MMO_RETURNIFZERO:
		{
			MultiVal* val;

			STATIC_CHECK(stackidx >= 0, "Conditional requires value");
			
			val = stackPop(context, stack, &stackidx);

			STATIC_CHECK(ISINT(val) || ISPOINTER(val), "Can only do conditionals on boolean values or pointers");

			// static checking skips returns
			if(IS_STATIC_CHECK())
				break;

			if(!getInt(context, val))
				continueExecution = false;
			break;
		}
		//this is currently only used to make multiple statements not fill up the stack unnecessarily
		case MMO_STATEMENT_BREAK:
			stackidx = -1;
			break;
		default:
			devassertmsg(0, "Unsupported MultiVal Operator in exprEvaluate");
		}
	}

	if(continuation)
		instrIdx--;

	if(answer)
	{
		if(!continuation) // i.e. execution continued normally
		{
			if(stackidx >= 0)
			{
				if(deepCopyAnswer)
					MultiValDeepCopy(answer, &stack[stackidx]);
				else
				{
					answer->type = stack[stackidx].type;
					answer->intval = stack[stackidx].intval;
#if STATIC_CHECKING_ON | STATIC_CHECKING_HYBRID
					if(SCIsConstant(context, stackidx))
						context->scResults.isConstant = true;
#endif
#if !STATIC_CHECKING_ON | STATIC_CHECKING_HYBRID
					// String return values may be allocated on the scratch stack, in
					// which case, we need to make a local copy, because we're going
					// to free the scratch stack soon.
					if(!IS_STATIC_CHECK()) // on separate line because <const> && <expression> warning is lame
					{
						if((answer->type & MULTI_TYPE_MASK) == MULTI_STRING)
						{
							estrCopy2(&context->stringResult, answer->str);
							answer->str = context->stringResult;
						}
						else if((answer->type & MULTI_TYPE_MASK) == MULTI_NP_POINTER)
						{
							const ExprVarEntry* pEntry = answer->ptr;
							answer->ptr = pEntry ? pEntry->inptr : NULL;
						}
					}
#endif
				}
			}
			else
			{
				answer->type = MULTI_NONE;
				answer->intval = 0;
			}
		}
		else
		{
			answer->type = MULTIOP_CONTINUATION;
			answer->intval = 0;
		}
	}

	if(instrPtrOut && stackidxOut)
	{
		*instrPtrOut = instrIdx;
		*stackidxOut = stackidx;
	}
	PERFINFO_AUTO_STOP();
}

__forceinline static int exprInitializeAndUpdateContext(Expression* expr, ExprContext* context)
{
	int totalExprLen = beaSize(&expr->postfixEArray);
	int curExprInd = context->instrPtr;

	if(context->curExpr != expr || curExprInd < 0 || curExprInd >= totalExprLen)
	{
		context->stackidx = -1;
		context->instrPtr = 0;
		context->curExpr = expr;
		memset(context->entArrayInUse, 0, EXPR_MAX_ENTARRAY * sizeof(int));
		curExprInd = 0;
	}

	return totalExprLen;
}

__forceinline static void exprPostEvalCleanup(ExprContext* context)
{
	ScratchStackFreeAll(&context->scratch);
	context->curExpr = NULL;
}
