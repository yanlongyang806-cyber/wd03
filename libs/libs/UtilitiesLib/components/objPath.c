/***************************************************************************



***************************************************************************/

#include "objPath.h"

#include "EString.h"
#include "tokenstore.h"
#include "tokenstore_inline.h"
#include "timing.h"

#include "structinternals.h"
#include "Expression.h"
#include "MemoryPool.h"
#include "qsortG.h"
#include "ReferenceSystem.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "ScratchStack.h"
#include "fastatoi.h"
#include "ThreadSafeMemoryPool.h"
#include "objPath_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_RUN_ANON(memBudgetAddMapping("ObjectPathOperation", BUDGET_GameSystems););

TSMP_DEFINE(ObjectPathOperation);
TSMP_DEFINE(NamedPathQueryAndResult);

AUTO_RUN;
void InitializeObjPathTSMPs(void)
{
	TSMP_SMART_CREATE(NamedPathQueryAndResult, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_NamedPathQueryAndResult, &TSMP_NAME(NamedPathQueryAndResult));
}

bool gbUseNewPathFlow = false;
AUTO_CMD_INT(gbUseNewPathFlow, UseNewPathFlow) ACMD_CATEGORY(ObjectDB);

bool gReportPathFailure = true;

// Useful utility functions

// Gets the key value as a string if possible
bool objGetKeyString(ParseTable tpi[], void * structptr, char *str, int str_size)
{
	int keycolumn;
	if ((keycolumn = ParserGetTableKeyColumn(tpi))  < 0)
	{
		return false;
	}
	return FieldToSimpleString(tpi,keycolumn,structptr,0,str,str_size,0);
}

bool objPathIsKey(ParseTable tpi[], const char *str)
{
	int keycolumn;
	const char *keyname;
	if (!str) return false;
	if ((keycolumn = ParserGetTableKeyColumn(tpi)) < 0) return false;
	
	keyname = tpi[keycolumn].name;

	if (keyname[0] == '.') keyname++;
	if (str[0] == '.') str++;

	return (stricmp(keyname, str) == 0);
}

// Gets the key value as a string if possible
bool objGetKeyEString(ParseTable tpi[], void * structptr, char **str)
{
	int keycolumn;
	if ((keycolumn = ParserGetTableKeyColumn(tpi)) < 0)
	{
		return false;
	}
	return FieldWriteText(tpi,keycolumn,structptr,0,str,WRITETEXTFLAG_DONTWRITEENDTOKENIFNOTHINGELSEWRITTEN);
}

// Gets the key value as a string if possible
bool objGetEscapedKeyEString(ParseTable tpi[], void * structptr, char **str)
{
	bool bResult = true;
	char *value = NULL;
	int keycolumn;
	if ((keycolumn = ParserGetTableKeyColumn(tpi)) < 0)
	{
		return false;
	}
	estrStackCreate(&value);
	bResult = FieldWriteText(tpi,keycolumn,structptr,0,&value,WRITETEXTFLAG_DONTWRITEENDTOKENIFNOTHINGELSEWRITTEN);
	if (estrLength(&value) == 0)
		bResult = false;

	//temporary code to see if we ever escape the characters we're sticking into hdiffs as keys
	if (isDevelopmentMode())
	{
		if (strpbrk(value, "\"\\\n"))
		{
			ErrorOrAlert("BAD_ESCAPING_KEY", "Escaping %s to act as a key for %s. It contains characters that would need to be escaped. Tell Alex W",
				value, ParserGetTableName(tpi));
		}
	}

	if (bResult)
	{
		estrAppendEscaped(str, value);
	}
	estrDestroy(&value);
	return bResult;
}

// Gets the key value as an int if possible
bool objGetKeyInt(ParseTable tpi[], void * structptr, int *key)
{
	char intString[MAX_TOKEN_LENGTH];
	bool result = objGetKeyString(tpi,structptr,SAFESTR(intString));
	if (result)
	{
		*key = atoi(intString);
	}
	return result;
}

bool objPathResolveField(const char* path_in, ParseTable table_in[], void* structptr_in, 
						 ParseTable** table_out, int* column_out, void** structptr_out, int* index_out,
						 U32 iObjPathFlags)
{
	bool result = ParserResolvePath(path_in, table_in, structptr_in, 
		table_out, column_out, structptr_out, index_out, NULL, NULL, iObjPathFlags);
	if (!result || (column_out && *column_out < -1))
	{
		return false;
	}
	return true;
}

bool objPathResolveFieldWithResult(const char* path_in, ParseTable table_in[], void* structptr_in, 
								   ParseTable** table_out, int* column_out, void** structptr_out, int* index_out,
								   U32 iObjPathFlags, char **ppchResult)
{
	bool result = ParserResolvePath(path_in, table_in, structptr_in, 
		table_out, column_out, structptr_out, index_out, ppchResult, NULL, iObjPathFlags);
	if (!result || (column_out && *column_out < -1))
	{
		return false;
	}
	return true;
}

bool objPathNormalize(const char* path_in, ParseTable table_in[], char **path_out)
{
	bool result = ParserResolvePath(path_in, table_in, NULL, 
		NULL, NULL, NULL, NULL, NULL, path_out, 0);
	if (!result)
	{
		return false;
	}
	return true;
}

bool objPathGetString(const char* path, ParseTable table[], void* structptr, 
					  char* str, int str_size)
{
	int indexNew,columnNew;
	ParseTable *tableNew;
	void *structptrNew;
	
	if (!objPathResolveField(path,table,structptr,&tableNew,&columnNew,&structptrNew,&indexNew,OBJPATHFLAG_TRAVERSEUNOWNED) || columnNew < 0)
	{
		return false;
	}
	return FieldToSimpleString(tableNew,columnNew,structptrNew,indexNew,str,str_size,0);
}

bool objPathGetInt(const char* path, ParseTable table[], void* structptr, int* val)
{
	int indexNew,columnNew;
	ParseTable *tableNew;
	void *structptrNew;
	int retval = 0;
	TextParserResult result = PARSERESULT_SUCCESS;
	
	if (!objPathResolveField(path,table,structptr,&tableNew,&columnNew,&structptrNew,&indexNew,OBJPATHFLAG_TRAVERSEUNOWNED) || columnNew < 0)
	{
		return false;
	}
	retval = TokenStoreGetInt_inline(tableNew,&tableNew[columnNew],columnNew,structptrNew,indexNew,&result);
	*val = retval;
	return result == PARSERESULT_SUCCESS;
}

bool objPathGetBit(const char* path, ParseTable table[], void* structptr, U32* val)
{
	int indexNew,columnNew;
	ParseTable *tableNew;
	void *structptrNew;
	U32 retval = 0;
	TextParserResult result = PARSERESULT_SUCCESS;

	if (!objPathResolveField(path,table,structptr,&tableNew,&columnNew,&structptrNew,&indexNew,OBJPATHFLAG_TRAVERSEUNOWNED) || columnNew < 0)
	{
		return false;
	}
	retval = TokenStoreGetBit_inline(tableNew,&tableNew[columnNew],columnNew,structptrNew,indexNew,&result);
	*val = retval;
	return result == PARSERESULT_SUCCESS;
}

bool objPathSetInt(const char* path, ParseTable table[], void* structptr, int val, bool force)
{
	int indexNew,columnNew;
	ParseTable *tableNew;
	void *structptrNew;
	int retval = 0;
	TextParserResult result = PARSERESULT_SUCCESS;

	if (!objPathResolveField(path,table,structptr,&tableNew,&columnNew,&structptrNew,&indexNew,(force?OBJPATHFLAG_CREATESTRUCTS:0)|OBJPATHFLAG_DONTLOOKUPROOTPATH) || columnNew < 0)
	{
		return false;
	}
	TokenStoreSetInt_inline(tableNew,&tableNew[columnNew],columnNew,structptrNew,indexNew, val, &result, NULL);
	return result == PARSERESULT_SUCCESS;
}

bool objPathSetBit(const char* path, ParseTable table[], void* structptr, U32 val, bool force)
{
	int indexNew,columnNew;
	ParseTable *tableNew;
	void *structptrNew;
	int retval = 0;
	TextParserResult result = PARSERESULT_SUCCESS;

	if (!objPathResolveField(path,table,structptr,&tableNew,&columnNew,&structptrNew,&indexNew,(force?OBJPATHFLAG_CREATESTRUCTS:0)|OBJPATHFLAG_DONTLOOKUPROOTPATH) || columnNew < 0)
	{
		return false;
	}
	TokenStoreSetBit_inline(tableNew,&tableNew[columnNew],columnNew,structptrNew,indexNew, val, &result, NULL);
	return result == PARSERESULT_SUCCESS;
}

bool objPathGetF32(const char* path, ParseTable table[], void* structptr, F32* val)
{
	int indexNew,columnNew;
	ParseTable *tableNew;
	void *structptrNew;
	F32 retval = 0;
	TextParserResult result = PARSERESULT_SUCCESS;

	if (!objPathResolveField(path,table,structptr,&tableNew,&columnNew,&structptrNew,&indexNew,OBJPATHFLAG_TRAVERSEUNOWNED) || columnNew < 0)
	{
		return false;
	}
	retval = TokenStoreGetF32_inline(tableNew,&tableNew[columnNew],columnNew,structptrNew,indexNew,&result);
	*val = retval;
	return result == PARSERESULT_SUCCESS;
}

bool objPathSetF32(const char* path, ParseTable table[], void* structptr, F32 val, bool force)
{
	int indexNew,columnNew;
	ParseTable *tableNew;
	void *structptrNew;
	F32 retval = 0;
	TextParserResult result = PARSERESULT_SUCCESS;

	if (!objPathResolveField(path,table,structptr,&tableNew,&columnNew,&structptrNew,&indexNew,(force?OBJPATHFLAG_CREATESTRUCTS:0)|OBJPATHFLAG_DONTLOOKUPROOTPATH) || columnNew < 0)
	{
		return false;
	}
	TokenStoreSetF32_inline(tableNew,&tableNew[columnNew],columnNew,structptrNew,indexNew,val,&result,NULL);
	return result == PARSERESULT_SUCCESS;
}

bool objPathGetEStringWithResult(const char* path, ParseTable table[], void* structptr, 
					   char** estr, char **ppResultString)
{
	int indexNew,columnNew;
	ParseTable *tableNew;
	void *structptrNew;

	if (!objPathResolveFieldWithResult(path,table,structptr,&tableNew,&columnNew,&structptrNew,&indexNew,OBJPATHFLAG_TRAVERSEUNOWNED, ppResultString) || columnNew < 0)
	{
		return false;
	}
	return FieldWriteText(tableNew,columnNew,structptrNew,indexNew,estr,0);
}

bool objPathGetMultiValWithResult(const char* path, ParseTable table[], void* structptr, 
					  MultiVal *result, char **ppResult)
{
	int indexNew,columnNew;
	ParseTable *tableNew;
	void *structptrNew;

	if (!objPathResolveFieldWithResult(path,table,structptr,&tableNew,&columnNew,&structptrNew,&indexNew,OBJPATHFLAG_TRAVERSEUNOWNED, ppResult) || columnNew < 0)
	{
		return false;
	}
	return FieldToMultiVal(tableNew,columnNew,structptrNew,indexNew,result,false,false);
}

bool objPathSetString(const char* path, ParseTable table[], void* structptr, 
					  const char* str)
{
	int indexNew,columnNew;
	ParseTable *tableNew;
	void *structptrNew;

	if (!objPathResolveField(path,table,structptr,&tableNew,&columnNew,&structptrNew,&indexNew,0) || columnNew < 0)
	{
		return false;
	}
	return FieldFromSimpleString(tableNew,columnNew,structptrNew,indexNew,str);
}

bool objPathSetMultiVal(const char* path, ParseTable table[], void* structptr, 
						const MultiVal *result)
{
	int indexNew,columnNew;
	ParseTable *tableNew;
	void *structptrNew;

	if (!objPathResolveField(path,table,structptr,&tableNew,&columnNew,&structptrNew,&indexNew,0) || columnNew < 0)
	{
		return false;
	}
	return FieldFromMultiVal(tableNew,columnNew,structptrNew,indexNew,result);
}


bool objPathGetStruct(const char* path, ParseTable table[], void* structptr, 
					  ParseTable **table_out, void **struct_out)
{
	int indexNew,columnNew;
	ParseTable *tableNew;
	void *structptrNew;
	void *returnStruct;
	void *returnTable;

	if (!ParserResolvePath(path,table,structptr,&tableNew,&columnNew,&structptrNew,&indexNew,NULL,NULL,OBJPATHFLAG_TRAVERSEUNOWNED))
	{
		return false;
	}
	if (columnNew < 0)
	{
		returnStruct = structptrNew;
		returnTable = tableNew;
	}
	else
	{	
		if (!TOK_HAS_SUBTABLE(tableNew[columnNew].type))
		{
			return false;
		}
		returnStruct = TokenStoreGetPointer_inline(tableNew,&tableNew[columnNew],columnNew,structptrNew,indexNew, NULL);
		returnTable = tableNew[columnNew].subtable;
	}
	if (!returnStruct || !returnTable)
	{
		return false;
	}
	*struct_out = returnStruct;
	*table_out = returnTable;
	return true;
}

bool objPathGetEArrayInt(const char* path, ParseTable table[], void* structptr, 
						 int **earrayint_out)
{
	return objPathGetEArrayIntWithResult(path, table, structptr, earrayint_out, NULL);
}

bool objPathGetEArrayIntWithResult(const char* path, ParseTable table[], void* structptr, 
								   int **earrayint_out, char **ppchResult)
{
	int indexNew,columnNew;
	ParseTable *tableNew;
	void *structptrNew;
	int **returnEArray;

	if (!ParserResolvePath(path, table, structptr, &tableNew, &columnNew, &structptrNew, &indexNew, ppchResult, NULL, OBJPATHFLAG_TRAVERSEUNOWNED))
	{
		return false;
	}
	if (indexNew >= 0 || columnNew < 0)
	{
		return false;
	}
	if (!(tableNew[columnNew].type & TOK_EARRAY))
	{
		return false;
	}
	returnEArray = TokenStoreGetEArrayInt_inline(tableNew,&tableNew[columnNew],columnNew,structptrNew, NULL);
	if (!returnEArray)
	{
		return false;
	}
	*earrayint_out = *returnEArray;
	return true;
}

bool objPathGetEArray(const char* path, ParseTable table[], void* structptr, 
					  void ***earray_out, ParseTable **table_out)
{
	return objPathGetEArrayWithResult(path, table, structptr, earray_out, table_out, NULL);
}

// Returns the EArray corresponding to a given path, and the ParseTable of 
// the members of the EArray, if they are structures
bool objPathGetEArrayWithResult(const char* path, ParseTable table[], void* structptr, 
					  void ***earray_out, ParseTable **table_out, char **ppchResult)
{
	int indexNew,columnNew;
	ParseTable *tableNew;
	void *structptrNew;
	void ***returnEArray;

	if (!ParserResolvePath(path, table, structptr, &tableNew, &columnNew, &structptrNew, &indexNew, ppchResult, NULL, OBJPATHFLAG_TRAVERSEUNOWNED))
	{
		return false;
	}
	if (indexNew >= 0 || columnNew < 0)
	{
		return false;
	}
	if (!(tableNew[columnNew].type & TOK_EARRAY))
	{
		return false;
	}
	returnEArray = TokenStoreGetEArray_inline(tableNew,&tableNew[columnNew],columnNew,structptrNew, NULL);
	if (!returnEArray)
	{
		return false;
	}
	*earray_out = *returnEArray;
	*table_out = tableNew[columnNew].subtable;
	return true;
}


bool objPathGetStructFields(const char* path, ParseTable table[], void* structptr, const char ***earray)
{
	ParseTable *newTable;
	void *newObject;
	int i;

	if (!objPathGetStruct(path,table,structptr,&newTable,&newObject))
	{	
		return false;
	}

	FORALL_PARSETABLE(newTable, i)
	{
		int type = TOK_GET_TYPE(newTable[i].type);
		if (newTable[i].type & TOK_REDUNDANTNAME)
		{
			continue;
		}
		if (type == TOK_IGNORE || type == TOK_START || type == TOK_END)
		{
			continue;
		}

		eaPush(earray,newTable[i].name);

	}
	return true;
}

static void addDefinesToContext(ExprContext *context, StaticDefine *definePointer)
{
	if (((StaticDefine *)definePointer)->key == U32_TO_PTR(DM_INT))
	{
		StaticDefineInt *defineList = (StaticDefineInt *)definePointer;
		int i;
		for (i = 1; defineList[i].key != U32_TO_PTR(DM_END); i++)
		{
			if (defineList[i].key == U32_TO_PTR(DM_DYNLIST))
			{
				assertmsg(0,"DynLists are not supported in persisted enums!");
				return;
			}
			if (defineList[i].key == U32_TO_PTR(DM_TAILLIST))
			{
				defineList = (StaticDefineInt *)defineList[i].value;
				i = 0;
				continue;
			}
			exprContextSetIntVar(context,defineList[i].key,defineList[i].value);
		}	
	}
	else
	{
		assertmsgf(0,"Bad Static define table!");
	}
}

typedef struct objPathParseAndApplyOperationsThreadData
{
	ObjectPathOperation **pathOperations;
} objPathParseAndApplyOperationsThreadData;

int objPathParseAndApplyOperations(ParseTable *table, void *object, const char *query)
{
	objPathParseAndApplyOperationsThreadData *threadData;
	int success = 0;
	STATIC_THREAD_ALLOC(threadData);
	if (!table || !object || !query)
	{
		return 0;
	}
	PERFINFO_AUTO_START_FUNC();
	success = objPathParseOperations(table, query, &threadData->pathOperations);
	success &= objPathApplyOperations(table, object, threadData->pathOperations, NULL);
	eaClearEx(&threadData->pathOperations, DestroyObjectPathOperation);
	PERFINFO_AUTO_STOP();
	return success;
}

int objPathApplySingleOperation(ParseTable *table, int column, void *object, int index, ObjectPathOpType op, const char *value, bool quotedValue, char **resultString)
{

	int success = (object && table);

	PERFINFO_AUTO_START_FUNC_L2();

	if (!success)
	{
		estrPrintf(resultString,"Bad object or table");
		PERFINFO_AUTO_STOP_L2();
		return 0;
	}

	switch (op)
	{
	case TRANSOP_CREATE:
		{
			int keyColumn;
			void *subStruct;

			PERFINFO_AUTO_START_L3("create", 1);
			if (TOK_GET_TYPE(table[column].type) == TOK_POLYMORPH_X)
			{
				estrPrintf(resultString,"can't create polymorphic types. Field %s in parent %s",
					table[column].name, ParserGetTableName(table));
				success = 0;
				PERFINFO_AUTO_STOP_L3();
				break;
			}



			if (table[column].type & TOK_EARRAY)
			{			
				if (TOK_HAS_SUBTABLE(table[column].type))
				{
					if (ParserColumnIsIndexedEArray(table, column, &keyColumn))
					{
						void ***earray;					
						earray = TokenStoreGetEArray_inline(table,&table[column],column,object,NULL);

						subStruct = StructCreateVoid(table[column].subtable);
						FieldReadText(table[column].subtable,keyColumn,subStruct,0,value);

						if (!*earray)
						{						
							const char *pTPIName = ParserGetTableName(table);							
							eaIndexedEnable_dbg(earray,table[column].subtable, pTPIName?pTPIName:__FILE__, pTPIName?LINENUM_FOR_EARRAYS:__LINE__);
						}

						if (!eaIndexedAdd(earray,subStruct))
						{
//							estrPrintf(resultString,"Can't add key %s", value);
							StructDestroyVoid(table[column].subtable,subStruct);
//							success = 0;							
						}
					}
					else
					{
						int newindex = atoi(value);
						if (!TokenStoreGetPointer_inline(table, &table[column], column, object, newindex, NULL))
						{
							if (!TokenStoreAlloc(table,column,object,newindex, table[column].param, NULL, NULL, NULL))
							{
								estrPrintf(resultString,"Allocing pointer in array failed, sparse arrays not supported");
								success = 0;
								PERFINFO_AUTO_STOP_L3();
								break;
							}
							if (TYPE_INFO(table[column].type).initstruct)
							{
								TYPE_INFO(table[column].type).initstruct(table, column, object, newindex);
							}
						}
					}
				}
				else
				{
					int newindex = atoi(value);
					if (TYPE_INFO(table[column].type).initstruct)
					{
						TYPE_INFO(table[column].type).initstruct(table, column, object, newindex);
					}
				}
			}
			else
			{
				if (table[column].type & TOK_INDIRECT && TOK_HAS_SUBTABLE(table[column].type))
				{
					if (!TokenStoreGetPointer_inline(table, &table[column], column, object, -1, NULL))
					{					
						TokenStoreAlloc(table,column,object,-1, table[column].param, NULL, NULL, NULL);
						if (TYPE_INFO(table[column].type).initstruct)
						{
							TYPE_INFO(table[column].type).initstruct(table, column, object, -1);
						}
					}
				}				
			}
			PERFINFO_AUTO_STOP_L3();
			break;
		}
	case TRANSOP_DESTROY:
		{			
			int newindex;
			PERFINFO_AUTO_START_L3("destroy", 1);
			if (table[column].type & TOK_EARRAY)
			{
				if (ParserResolveKey(value,table,column,object,&newindex,0, NULL, NULL))
				{
					if (TYPE_INFO(table[column].type).destroystruct_func)
					{
						TYPE_INFO(table[column].type).destroystruct_func(table, &table[column], column, object, newindex);
					}
					TokenStoreRemoveElement(table,column,object,newindex,NULL);
				}
			}
			else if (TYPE_INFO(table[column].type).destroystruct_func)
			{
				TYPE_INFO(table[column].type).destroystruct_func(table, &table[column], column, object, index);		
			}
			PERFINFO_AUTO_STOP_L3();
			break;
		}
	case TRANSOP_GET:
		{
			PERFINFO_AUTO_START_L3("get", 1);
			if (!FieldWriteText(table, column, object, index, resultString, 0))
			{
				success = 0;
			}
			PERFINFO_AUTO_STOP_L3();
		}
		break;
	case TRANSOP_SUB:
	case TRANSOP_ADD:
	case TRANSOP_SET:
	case TRANSOP_MULT:
	case TRANSOP_DIV:
	case TRANSOP_OR:
	case TRANSOP_AND:
		{
			if (quotedValue)
			{
				PERFINFO_AUTO_START_L3("set", 1);
				if (op != TRANSOP_SET)
				{
					estrPrintf(resultString,"Only = is valid with quoted values");
					success = 0;
				}
				if (!FieldReadText(table,column,object,index,value))
				{
					estrPrintf(resultString,"Failure to write value");
					success = 0;
				}
				PERFINFO_AUTO_STOP_L3();
			}
			else
			{
				char *exprString;
				MultiVal oldValue = {0};
				ExprContext *pContext = exprContextCreate();
				Expression *pExpr = exprCreate();
				MultiVal result = {0};
				static ExprFuncTable *s_funcTable = NULL;

				PERFINFO_AUTO_START_L3("set expr", 1);
				if(!s_funcTable)
					s_funcTable = exprContextCreateFunctionTable("ObjPath");
				estrStackCreate(&exprString);
				exprContextSetFuncTable(pContext, s_funcTable);
				if (!FieldToMultiVal(table,column,object,index,&oldValue,false,false))
				{
					success = 0;
				}
				else
				{
					StaticDefine *defines;
					exprContextSetSimpleVar(pContext,"pathOldValue",&oldValue);

					switch(op)
					{
						xcase TRANSOP_ADD: estrCopy2(&exprString,"pathOldValue + (");
						xcase TRANSOP_SUB: estrCopy2(&exprString,"pathOldValue - (");
						xcase TRANSOP_MULT: estrCopy2(&exprString,"pathOldValue * (");
						xcase TRANSOP_DIV: estrCopy2(&exprString,"pathOldValue / (");
						xcase TRANSOP_OR: estrCopy2(&exprString,"pathOldValue | (");
						xcase TRANSOP_AND: estrCopy2(&exprString,"pathOldValue & (");
					}

					estrAppend2(&exprString,value);
					if (op != TRANSOP_SET)
					{
						estrAppend2(&exprString,")");
					}

					if (defines = ParserColumnGetStaticDefineList(table,column))
					{
						addDefinesToContext(pContext,defines);
					}

					exprGenerateFromString(pExpr,pContext,exprString,NULL);
					if (exprContextCheckStaticError(pContext))
					{
						estrPrintf(resultString,"Failed to parse expression");
						success = 0;
					}
					else
					{				
						exprEvaluate(pExpr,pContext,&result);
		
						if (!FieldFromMultiVal(table,column,object,index,&result))
						{
							estrPrintf(resultString,"Failed to evaluate and set value");
							success = 0;
						}
					}
				}
				exprDestroy(pExpr);
				exprContextDestroy(pContext);
				MultiValClear(&result);
				estrDestroy(&exprString);
				PERFINFO_AUTO_STOP_L3();
			}
		}
		break;		
	xdefault:
		estrPrintf(resultString, "Invalid operation type!");
		success = 0;
	}

	PERFINFO_AUTO_STOP_L2();
	return success;

}

#define isWhitespace(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')



// Sort deletes before anything else, sort short paths after long paths, and sort low numbers after highs
// Sort creates after deletes, and sort using the inversion of the delete order
// Everything else is last

static int cmpPathHolders(const ObjectPathOperation **path1, const ObjectPathOperation **path2)
{
	PERFINFO_AUTO_START_FUNC_L3();
	if ((*path1)->op == TRANSOP_DESTROY)
	{
		int pathDiff;
		if ((*path2)->op != TRANSOP_DESTROY)
		{
			PERFINFO_AUTO_STOP_L3();
			return -1;
		}
		pathDiff = -stricmp((*path1)->pathEString,(*path2)->pathEString);
		if (pathDiff != 0)
		{

			PERFINFO_AUTO_STOP_L3();
			return pathDiff;
		}
		if (!(*path1)->quotedValue && !(*path2)->quotedValue)
		{

			PERFINFO_AUTO_STOP_L3();
			return -(atoi((*path1)->valueEString) - atoi((*path2)->valueEString));
		}
		else
		{

			PERFINFO_AUTO_STOP_L3();
			return -stricmp((*path1)->valueEString,(*path2)->valueEString);
		}
	}
	else if ((*path2)->op == TRANSOP_DESTROY)
	{

			PERFINFO_AUTO_STOP_L3();
		return 1;
	}
	else if ((*path1)->op == TRANSOP_CREATE)
	{
		int pathDiff;
		if ((*path2)->op != TRANSOP_CREATE)
		{
	
			PERFINFO_AUTO_STOP_L3();
			return -1;
		}
		pathDiff = stricmp((*path1)->pathEString,(*path2)->pathEString);
		if (pathDiff != 0)
		{

			PERFINFO_AUTO_STOP_L3();
			return pathDiff;
		}
		if (!(*path1)->quotedValue && !(*path2)->quotedValue)
		{

			PERFINFO_AUTO_STOP_L3();
			return (atoi((*path1)->valueEString) - atoi((*path2)->valueEString));
		}
		else
		{

			PERFINFO_AUTO_STOP_L3();
			return stricmp((*path1)->valueEString,(*path2)->valueEString);
		}
	}
	else if ((*path2)->op == TRANSOP_CREATE)
	{

		PERFINFO_AUTO_STOP_L3();
		return 1;
	}

	PERFINFO_AUTO_STOP_L3();
	return 0;
}

#undef objPathParseOperations
#define PATH_OPERATION_HINTS "Your schema files need to be regenerated, your manual transaction has an error, your data defined enum is not a dependency of AUTO_STARTUP(Schemas), or there is an error in the object system"
int objPathParseOperations(ParseTable *table, const char *query, ObjectPathOperation ***pathOperations)
{
	const char *path, *line, *nextline;
	int success = 1;

	if ( !query || !table)
	{
		return 0;
	}
	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("Parse", 1);
	line = query;
	while (isWhitespace(*line))
	{
		line++;
	}

	while (line && success)
	{
		ObjectPathOperation *pathHolder;
		size_t pathLength;
		nextline = strchr(line,'\n');
		if(nextline)
		{
			while (isWhitespace(*nextline))
			{
				nextline++;
			}
		}
		path = strchr(line,' ');
		if (!path)
		{
			line = nextline;
			continue;
		}
		while (isWhitespace(*path))
		{
			path++;
		}

		// Expected beginnings to operations are:
		// set, create, destroy, comment
		// Anything other than set, create, or destroy results in ignoring the line
		pathHolder = CreateObjectPathOperation();
		#define IS(x, a, b) (line[x] == a || line[x] == b)
		if (IS(0, 's', 'S') &&
			IS(1, 'e', 'E') &&
			IS(2, 't', 'T'))
		{
			pathHolder->op = TRANSOP_SET;
		}
		else if(IS(0, 'c', 'C') &&
				IS(1, 'r', 'R') &&
				IS(2, 'e', 'E') &&
				IS(3, 'a', 'A') &&
				IS(4, 't', 'T') &&
				IS(5, 'e', 'E'))
		{
			pathHolder->op = TRANSOP_CREATE;
		}
		else if(IS(0, 'd', 'D') &&
				IS(1, 'e', 'E') &&
				IS(2, 's', 'S') &&
				IS(3, 't', 'T') &&
				IS(4, 'r', 'R') &&
				IS(5, 'o', 'O') &&
				IS(6, 'y', 'Y'))
		{
			pathHolder->op = TRANSOP_DESTROY;
		}
		else
		{
			line = nextline;
			DestroyObjectPathOperation(pathHolder);
			continue;
		}
		#undef IS

		if (nextline > path)
		{
			pathLength = nextline - path;
		}
		else
		{
			pathLength = strlen(path);
		}

		while (pathLength-1 && isWhitespace(path[pathLength-1]))
		{
			pathLength--;
		}

		if (objPathParseSingleOperation(path,pathLength,&pathHolder->op,&pathHolder->pathEString,&pathHolder->valueEString,&pathHolder->quotedValue))
		{
			eaPush(pathOperations,pathHolder);
		}
		else 
		{
			success = 0;
			if (isDevelopmentMode() && gReportPathFailure)
			{
				char errorString[10000];
				strncpy(errorString,line,nextline - line);
				Errorf("Failure to parse path operation: %s. " PATH_OPERATION_HINTS, errorString);			
			}
			DestroyObjectPathOperation(pathHolder);
		}
		line = nextline;
	}
	PERFINFO_AUTO_STOP();

	if (success && eaSize(pathOperations) > 1)
	{
		PERFINFO_AUTO_START("Sort", 1);
		eaQSortG(*pathOperations, cmpPathHolders);
		PERFINFO_AUTO_STOP();
	}
	
	PERFINFO_AUTO_STOP();
	
	return success;

}

int objPathApplyOperations(ParseTable *table, void *object, ObjectPathOperation **pathOperations, ObjectPath ***cachedpaths)
{
	int success = 1, i;
	bool bUsingCache = false;
	PERFINFO_AUTO_START_FUNC();

	if(cachedpaths && eaSize(cachedpaths) == eaSize(&pathOperations))
	{
		bUsingCache = true;
	}

	for (i = 0; i < eaSize(&pathOperations) && success; i++)
	{
		ParseTable *table_out;
		int column_out;
		void *structptr_out;
		int index_out;
		int result;
		int pathFlags = OBJPATHFLAG_DONTLOOKUPROOTPATH;

		if(bUsingCache && (*cachedpaths)[i])
		{
			result = ParserResolvePathComp((*cachedpaths)[i], object, &table_out, &column_out, &structptr_out, &index_out, NULL, pathFlags);
		}
		else
		{
			ObjectPath *path = NULL;

			result = ParserResolvePathEx(pathOperations[i]->pathEString, table, object, &table_out, &column_out, &structptr_out, &index_out, &path, NULL, NULL, NULL, pathFlags);

			if(bUsingCache)
			{
				(*cachedpaths)[i] = path;
			}
		}

		if(result)
		{
			if (!objPathApplySingleOperation(table_out,column_out,structptr_out,index_out,pathOperations[i]->op,pathOperations[i]->valueEString,pathOperations[i]->quotedValue,NULL))
			{
				success = 0;
				if (isDevelopmentMode() && gReportPathFailure)
				{
					Errorf("Failure to execute path operation: %d %s %s. " PATH_OPERATION_HINTS,
						pathOperations[i]->op, pathOperations[i]->pathEString, pathOperations[i]->valueEString);
				}
			}
		}
		else 
		{
			success = 0;
			if (isDevelopmentMode() && gReportPathFailure)
			{
				Errorf("Failure to resolve path %s. " PATH_OPERATION_HINTS,
					pathOperations[i]->pathEString);
			}
		}
	}

	PERFINFO_AUTO_STOP();

	return success;
}

static const char* CharSearchBackwards(const char *str, size_t end, char c)
{
	while (end >= 0)
	{
		if (str[end] == c)
		{
			return str + end;
		}
		end--;
	}
	return NULL;
}

int objPathParseSingleOperation(const char *path, size_t length, ObjectPathOpType *op, char ** pathEstr, char ** valueEstr, bool *quotedValue)
{
	int result = 1;
	const char *value, *setop;
	size_t startvalue, endvalue, pathLength;

	if (!path || !op || !pathEstr || !valueEstr)
	{
		return 0;
	}
	PERFINFO_AUTO_START_FUNC_L2();
	
	estrClear(pathEstr);
	estrClear(valueEstr);

	if (length == 0)
	{
		length = strlen(path);
	}

	while (length-1 && isWhitespace(path[length-1]))
	{
		length--;
	}

	if (path[0] != '\0' && path[0] != '.')
	{
		estrConcatChar(pathEstr,'.');
	}

	if (*op == TRANSOP_GET)
	{
		estrConcat(pathEstr,path,(int)length);
		PERFINFO_AUTO_STOP_L2();
		return 1;
	}
	else if (*op == TRANSOP_CREATE || *op == TRANSOP_DESTROY)
	{
		int endbracket = (int)length - 1;
		int startbracket;

		if (path[endbracket] != ']')
		{
			estrConcat(pathEstr,path,(int)length);
			PERFINFO_AUTO_STOP_L2();
			return 1;			
		}
		else
		{
			int startkey,endkey;
			enumReadObjectPathIdentifierResult eIdentResult;
			if (path[endbracket - 1] == '"')
			{
				//path is quoted
				startbracket = CharSearchBackwards(path,endbracket - 2,'"') - path;
				startbracket = CharSearchBackwards(path,startbracket,'[') - path;
			}
			else
			{			
				startbracket = CharSearchBackwards(path,length,'[') - path;
			}
			startkey = startbracket + 1;
			endkey = endbracket - 1;

			*quotedValue = false;

			value = path + startkey;

			estrConcat(pathEstr,path,(int)startbracket);		
			eIdentResult = ReadObjectPathIdentifier(valueEstr, &value, quotedValue);

			PERFINFO_AUTO_STOP_L2();
			return (eIdentResult == READOBJPATHIDENT_OK);
		}
	}

	if (path)
	{
		setop = path;

		while (!isWhitespace(*setop))
		{
			if (*setop == '"')
			{
				setop++;
				while (*setop && *setop != '"')
				{
					setop++;
				}
				if(*setop)
				{
					setop++;
				}
			}
			else if(*setop)
			{
				setop++;
			}
			else
			{
				break;
			}
		}
		pathLength = setop - path;
		if(*setop)
		{
			while (isWhitespace(*setop))
			{
				setop++;
			}
		}
	}
	else
	{	
		PERFINFO_AUTO_STOP_L2();
		return 0;
	}

	value = strchr(setop,' ');
	if (value)
	{
		while (isWhitespace(*value))
		{
			value++;
		}
	}
	else
	{
		PERFINFO_AUTO_STOP_L2();
		return 0;
	}

	if (*setop == '=')
	{
		*op = TRANSOP_SET;
	}
	else if(setop[0] && setop[1] == '=')
	{
		switch(setop[0])
		{
			xcase '+': *op = TRANSOP_ADD;
			xcase '-': *op = TRANSOP_SUB;
			xcase '*': *op = TRANSOP_MULT;
			xcase '/': *op = TRANSOP_DIV;
			xcase '|': *op = TRANSOP_OR;
			xcase '&': *op = TRANSOP_AND;
			xdefault:
			{
				PERFINFO_AUTO_STOP_L2();
				return 0;
			}
		}
	}
	else
	{
		PERFINFO_AUTO_STOP_L2();
		return 0;
	}

	endvalue = length - 1;

	startvalue = value - path;

	if (startvalue > endvalue)
	{
		PERFINFO_AUTO_STOP_L2();
		return 0;
	}

	*quotedValue = false;
	if (path[startvalue] == '"' && path[endvalue] == '"')
	{
		*quotedValue = true;
		startvalue++;
		endvalue--;
	}

	estrConcat(pathEstr,path,(int)pathLength);
	estrAppendUnescapedCount(valueEstr,path + startvalue,(int)(endvalue-startvalue + 1));

	PERFINFO_AUTO_STOP_L2();
	return 1;
}

void objPathWriteSingleOperation(char **resultString, ObjectPathOperation *pOperation)
{	
	bool bIsCreateDestroy = false;
	PERFINFO_AUTO_START_FUNC_L2();
	switch (pOperation->op)
	{
		case TRANSOP_ADD:
			estrConcatf(resultString, "set %s += ", pOperation->pathEString);
			break;
		case TRANSOP_SUB:
			estrConcatf(resultString, "set %s -= ", pOperation->pathEString);
			break;
		case TRANSOP_MULT:
			estrConcatf(resultString, "set %s *= ", pOperation->pathEString);
			break;
		case TRANSOP_DIV:
			estrConcatf(resultString, "set %s /= ", pOperation->pathEString);
			break;
		case TRANSOP_OR:
			estrConcatf(resultString, "set %s |= ", pOperation->pathEString);
			break;
		case TRANSOP_AND:
			estrConcatf(resultString, "set %s &= ", pOperation->pathEString);
			break;
		case TRANSOP_SET:
			estrConcatf(resultString, "set %s = ", pOperation->pathEString);
			break;
		case TRANSOP_CREATE:
			estrConcatf(resultString, "create %s", pOperation->pathEString);		
			bIsCreateDestroy = 1;
			break;
		case TRANSOP_DESTROY:
			estrConcatf(resultString, "destroy %s", pOperation->pathEString);
			bIsCreateDestroy = 1;
			break;
	}
	if (bIsCreateDestroy)
	{		
		if (estrLength(&pOperation->valueEString))
		{
			if (pOperation->quotedValue)
			{
				estrAppend2(resultString, "[\"");
				estrAppendEscaped(resultString, pOperation->valueEString);
				estrAppend2(resultString, "\"]");
			}
			else
			{
				estrAppend2(resultString, "[");
				estrAppendEscaped(resultString, pOperation->valueEString);
				estrAppend2(resultString, "]");
			}
		}
	}
	else
	{
		if (pOperation->quotedValue || !estrLength(&pOperation->valueEString))
		{
			estrAppend2(resultString, "\"");
			estrAppendEscaped(resultString, pOperation->valueEString);
			estrAppend2(resultString, "\"");
		}
		else
		{
			estrAppendEscaped(resultString, pOperation->valueEString);
		}
	}
	PERFINFO_AUTO_STOP_L2();
}



// Command section

// Returns a string version of whatever the passed in object path resolves to
AUTO_COMMAND ACMD_CATEGORY(Test);
char *ReadObjectPath(const char *path)
{
	static char *returnString = NULL;

	if (returnString)
	{
		estrClear(&returnString);
	}
	
	if (!objPathGetEString(path,NULL,NULL,&returnString))
	{
		estrPrintf(&returnString,"OPATHERROR: Invalid Path\n");
	}
	return returnString;
}

// Sets what path resolves to to the passed in string value
AUTO_COMMAND ACMD_CATEGORY(Test);
char *SetObjectPath(const char *path, const ACMD_SENTENCE value)
{
	static char *returnString = NULL;

	if (returnString)
	{
		estrClear(&returnString);
	}

	if (!objPathSetString(path,NULL,NULL,value))
	{
		estrPrintf(&returnString,"OPATHERROR: Invalid Path\n");
	}
	else
	{
		estrPrintf(&returnString,"%s modified",path);
	}
	return returnString;
}

// Applies a series of ObjectPath operations to the passed in path
AUTO_COMMAND ACMD_CATEGORY(Test);
char *ApplyPathOperations(const char *path, const ACMD_SENTENCE operations)
{
	static char *returnString = NULL;
	ParseTable *table;
	void *structptr;

	if (returnString)
	{
		estrClear(&returnString);
	}

	if (!objPathGetStruct(path,NULL,NULL,&table,&structptr))
	{
		estrPrintf(&returnString,"OPATHERROR: Invalid Path\n");
	}
	if (!objPathParseAndApplyOperations(table,structptr,operations))
	{
		estrPrintf(&returnString,"OPATHERROR: Error applying changes\n");
	}
	else
	{
		estrPrintf(&returnString,"Changes applied to %s.",path);
	}
	return returnString;
}

// List all structure fields found in the structure that path resolves to
AUTO_COMMAND ACMD_CATEGORY(Test);
char *ListObjectPathFields(const char *path)
{
	static char *returnString = NULL;
	char **earray = NULL;
	int i;

	estrClear(&returnString);

	if (!objPathGetStructFields(path,NULL,NULL,&earray))
	{
		estrPrintf(&returnString,"OPATHERROR: Does not resolve to struct\n");
		eaDestroy(&earray);
		return returnString;
	}
	for (i = 0; i < eaSize(&earray);i++)
	{
		estrConcatf(&returnString,"%s\n",earray[i]);
	}
	eaDestroy(&earray);
	return returnString;
}

// List all keys found in the EArray that path resolves to
AUTO_COMMAND ACMD_CATEGORY(Test);
char *ListObjectPathKeys(const char *path)
{
	static char *keyString = NULL;
	static char *returnString = NULL;
	ParseTable *table;
	void **earray;
	int i;
	estrClear(&returnString);

	if (!objPathGetEArray(path,NULL,NULL,&earray,&table) || !table)
	{
		int indexNew,columnNew;
		ParseTable *tableNew;
		void *structptrNew;
		int numElements;

		if (!ParserResolvePath(path,NULL,NULL,&tableNew,&columnNew,&structptrNew,&indexNew,NULL,NULL,OBJPATHFLAG_TRAVERSEUNOWNED))
		{
			estrPrintf(&returnString,"OPATHERROR: Invalid path\n");
			return returnString;
		}
		if (!(tableNew[columnNew].type & TOK_FIXED_ARRAY) || indexNew >= 0)
		{
			estrPrintf(&returnString,"OPATHERROR: Does not resolve to a collection\n");
			return returnString;
		}

		numElements = TokenStoreGetNumElems_inline(tableNew,&tableNew[columnNew],columnNew,structptrNew, NULL);

		for (i = 0; i < numElements; i++)
		{
			estrConcatf(&returnString,"%d\n",i);
		}
		return returnString;
	}

	for (i = 0; i < eaSize(&earray); i++)
	{
		estrClear(&keyString);
		if (objGetKeyEString(table,earray[i],&keyString))
		{
			estrConcatf(&returnString,"%s\n",keyString);
		}
		else
		{
			estrConcatf(&returnString,"%d\n",i);
		}
	}
	return returnString;
}

MultiValType objPathGetType(const char *path_in, ParseTable table_in[])
{
	ParseTable *pTableOut = NULL;
	int iColumnOut;
	void *pStructOut;
	int iIndexOut;

	objPathResolveField(path_in, table_in, NULL, &pTableOut, &iColumnOut, &pStructOut, &iIndexOut, OBJPATHFLAG_TRAVERSEUNOWNED);

	if (pTableOut)
	{
		return TYPE_INFO(pTableOut[iColumnOut].type).eMultiValType;
	}

	return MULTI_NONE;
}


ObjectPathOperation *CreateObjectPathOperation(void)
{
    ObjectPathOperation *fieldEntry = NULL;
	
	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(ObjectPathOperation, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;
	
	fieldEntry = TSMP_ALLOC(ObjectPathOperation);
	fieldEntry->op = 0;
	fieldEntry->quotedValue = 0;
	fieldEntry->iValueAtoid = 0;

	estrBufferCreate(&fieldEntry->pathEString, SAFESTR(fieldEntry->pathStringInitialBuff));
	estrBufferCreate(&fieldEntry->valueEString, SAFESTR(fieldEntry->valueStringInitialBuff));

    return fieldEntry;
}

void DestroyObjectPathOperation(ObjectPathOperation *fieldEntry)
{
	if (!fieldEntry)
	{
		return;
	}

	estrDestroy(&fieldEntry->valueEString);
	estrDestroy(&fieldEntry->pathEString);

	TSMP_FREE(ObjectPathOperation, fieldEntry);
}

#include "AutoGen/objPath_h_ast.h"

DictionaryHandle gQueryDict;

AUTO_RUN;
void registerQueryDict(void)
{
	gQueryDict = RefSystem_RegisterSelfDefiningDictionary("NamedPathQuery", false, parse_NamedPathQuery, true, true, NULL);
}

NamedPathQuery* objGetNamedQuery(const char *name)
{
	NamedPathQuery *query = RefSystem_ReferentFromString(gQueryDict, name);
	return query;
}

static void objReloadNamedQueries(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Query Strings...");
	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);
	ParserReloadFileToDictionary(pchRelPath, gQueryDict);
	loadend_printf("Done.");
	
	
}

void objLoadNamedQueries(const char *directory, const char *filespec, const char *binFile)
{
	int i;
	char **ppFileSpecs = NULL;

	loadstart_printf("Loading Query Strings...");
	ParserLoadFilesToDictionary(directory, filespec, binFile, PARSER_OPTIONALFLAG, gQueryDict);
	if (isDevelopmentMode())
	{
	
		MakeFileSpecFromDirFilename(directory, filespec, &ppFileSpecs);
		for (i = 0; i < eaSize(&ppFileSpecs); i++)
		{
			FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, ppFileSpecs[i], objReloadNamedQueries);
		}
		eaDestroyEx(&ppFileSpecs, NULL);
	}

	loadend_printf("Done.");

}

bool objDoWildCardQuery(char *pQueryString, ParseTable tpi[], void *pStructPtr, WildCardQueryResult ***pppOutList,
	char **ppResultString)
{
	char *pAsterisk = strchr(pQueryString, '*');
	char *pTemp = NULL;

	ParseTable *pArrayTable;
	int iArrayColumn;
	void *pArrayStruct;
	int iArrayIndex;

	int iKeyColumn;

	void ***pppEArray;

	ParseTable *pSubTable;
	int i;

	if (!pAsterisk)
	{
		if (ppResultString)
		{
			estrPrintf(ppResultString, "Couldn't find *");
		}
		return false;
	}

	//query string must have exactly one * and it must be between braces
	if (strchr(pAsterisk + 1, '*') || *(pAsterisk + 1) != ']' || pAsterisk == pQueryString || *(pAsterisk - 1) != '[' || *(pAsterisk + 2) != '.')
	{
		if (ppResultString)
		{
			estrPrintf(ppResultString, "Found extra *, or didn't find [*].");
		}		
		return false;
	}

	estrStackCreate(&pTemp);
	estrConcat(&pTemp, pQueryString, pAsterisk - pQueryString  - 1);

	if (!objPathResolveFieldWithResult(pTemp, tpi, pStructPtr, 
		&pArrayTable, &iArrayColumn, &pArrayStruct, &iArrayIndex, 
		0, ppResultString))
	{
		estrDestroy(&pTemp);
		return false;
	}

	if (!ParserColumnIsIndexedEArray(pArrayTable, iArrayColumn, &iKeyColumn))
	{
		if (ppResultString)
		{
			estrPrintf(ppResultString, "%s doesn't seem to be an indexed earray", pTemp);
		}
		estrDestroy(&pTemp);
		return false;
	}
						
	pppEArray = TokenStoreGetEArray_inline(pArrayTable, &pArrayTable[iArrayColumn], iArrayColumn, pArrayStruct, NULL);

	pSubTable = pArrayTable[iArrayColumn].subtable;

	for (i=0; i < eaSize(pppEArray); i++)
	{
		void *pSubStruct = (*pppEArray)[i];

		if (pSubStruct)
		{
			char keyString[256];
			char *pValueString = NULL;
			WildCardQueryResult *pResult;
			

			FieldToSimpleString(pSubTable, iKeyColumn, pSubStruct, 0, SAFESTR(keyString), 0);

			if (!objPathGetEString(pAsterisk + 2, pSubTable, pSubStruct, &pValueString))
			{
				if (ppResultString)
				{
					estrPrintf(ppResultString, "Couldn't resolve sub xpath %s", pAsterisk + 2);
				}
				estrDestroy(&pTemp);
				estrDestroy(&pValueString);
				return false;
			}

			pResult = StructCreate(parse_WildCardQueryResult);
			pResult->pKey = strdup(keyString);
			pResult->pValue = pValueString;

			eaPush(pppOutList, pResult);
		}
	}

	estrDestroy(&pTemp);

	return true;




}

bool objPathParseRootDictionaryAndRefData(const char *pchPath, char **ppchDict, char **ppchRefData)
{
	const char *pchStart;
	while (IS_WHITESPACE(*pchPath))
		pchPath++;
	pchStart = pchPath;

	// Extract up until the first [.
	while (*pchPath && *pchPath != '[')
		pchPath++;
	if (!*pchPath)
		return false;
	estrClear(ppchDict);
	estrConcat(ppchDict, pchStart, pchPath - pchStart);

	// Skip the [, and the " if the string is quoted.
	pchPath++;
	if (*pchPath == '"')
		pchPath++;

	// Extract up until " or ] or whitespace.
	while (IS_WHITESPACE(*pchPath))
		pchPath++;
	pchStart = pchPath;
	while (*pchPath && *pchPath != '"' && *pchPath != ']' && !IS_WHITESPACE(*pchPath))
		pchPath++;
	if (!*pchPath)
		return false;
	estrClear(ppchRefData);
	estrConcat(ppchRefData, pchStart, pchPath - pchStart);
	return true;

}




static int cmpDestroys(const ObjectPathOperation **path1, const ObjectPathOperation **path2)
{
	int iRetVal;
	int pathDiff;
	PERFINFO_AUTO_START_FUNC_L3();

	pathDiff = -stricmp((*path1)->pathEString,(*path2)->pathEString);
	if (pathDiff != 0)
	{

		PERFINFO_AUTO_STOP_L3();
		return pathDiff;
	}
	if (!(*path1)->quotedValue && !(*path2)->quotedValue)
	{
		iRetVal =  -((*path1)->iValueAtoid - (*path2)->iValueAtoid);
		PERFINFO_AUTO_STOP_L3();
		return iRetVal;
	}
	else
	{
		iRetVal = -stricmp((*path1)->valueEString,(*path2)->valueEString);
		PERFINFO_AUTO_STOP_L3();
		return iRetVal;
	}
}


static int cmpCreates(const ObjectPathOperation **path1, const ObjectPathOperation **path2)
{
	int iRetVal;
	int pathDiff;
	PERFINFO_AUTO_START_FUNC_L3();

	pathDiff = stricmp((*path1)->pathEString,(*path2)->pathEString);
	if (pathDiff != 0)
	{
		PERFINFO_AUTO_STOP_L3();
		return pathDiff;
	}
	if (!(*path1)->quotedValue && !(*path2)->quotedValue)
	{
		iRetVal = (*path1)->iValueAtoid - (*path2)->iValueAtoid;
		PERFINFO_AUTO_STOP_L3();
		return iRetVal;
	}
	else
	{
		iRetVal = stricmp((*path1)->valueEString,(*path2)->valueEString);
		PERFINFO_AUTO_STOP_L3();
		return iRetVal;
	}
}

#define IS(x, a, b) (pReadHead[x] == a || pReadHead[x] == b)


#define SKIP_0_OR_MORE_SPACES_EXPECT_SOMETHING() { while (*pReadHead == ' ') pReadHead++; if (!*pReadHead) goto SkipRestOfLine; }
#define SKIP_1_OR_MORE_SPACES_EXPECT_SOMETHING() { 	if (*pReadHead != ' ') goto SkipRestOfLine; pReadHead++; SKIP_0_OR_MORE_SPACES_EXPECT_SOMETHING() }

#define READ_UNTIL_END_OF_STRING_OR_LINE_EXPECTING_NOTHING() { while (*pReadHead == ' ') pReadHead++; if (!(*pReadHead == '\n' || *pReadHead == 0)) goto SkipRestOfLine; }



bool ReadUntilQuoteUnescaping(const char **ppReadHead, char **ppOutEString)
{
	const char *pReadHead = *ppReadHead;
	char c;

	while (1)
	{
		switch (*pReadHead)
		{
		case 0:
			return false;
	
		case '"':
			*ppReadHead = pReadHead + 1;
			estrTerminateString_Unsafe_inline(ppOutEString);
			return true;

		case '\\':
			c = *(pReadHead + 1);

			switch(c){
			case '"':
				return false;

			case 'n':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '\n');
				break;
			case 'r':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '\r');
				break;
			case 't':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '\t');
				break;
			case 'q':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '"');
				break;
			case 's':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '\'');
				break;
			case '\\':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '\\');
				break;
			case 'p':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '%');
				break;
			case '0':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '\0');
				break;
			case 'd':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '$');
				break;
			default:
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, c);
				break;
			}
			pReadHead++;
			break;

		default:
			estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, *pReadHead);
			break;
		}
	
		pReadHead++;
	}
}


bool ReadUntilEndOfLineOrStringUnescaping(const char **ppReadHead, char **ppOutEString)
{
	const char *pReadHead = *ppReadHead;
	char c;

	while (1)
	{
		switch (*pReadHead)
		{
		case 0:
		case '\n':
			*ppReadHead = pReadHead + 1;
			estrTerminateString_Unsafe_inline(ppOutEString);
			return true;

		case '\\':
			c = *(pReadHead + 1);

			switch(c){
			case 'n':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '\n');
				break;
			case 'r':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '\r');
				break;
			case 't':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '\t');
				break;
			case 'q':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '"');
				break;
			case 's':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '\'');
				break;
			case '\\':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '\\');
				break;
			case 'p':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '%');
				break;
			case '0':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '\0');
				break;
			case 'd':
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, '$');
				break;
			default:
				estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, c);
				break;
			}
			pReadHead++;
			break;

		default:
			estrConcatCharNonTerminating_Unsafe_inline(ppOutEString, *pReadHead);
			break;
		}
	
		pReadHead++;
	}
}

static __inline const char *FindNonQuotedSpace(const char *pStart)
{
	bool bInQuotes = false;
	while (1)
	{
		char c = *pStart;

		switch (c)
		{
		case 0:
			return NULL;
		case '"':
			bInQuotes = !bInQuotes;
			break;
		case ' ':
			if (!bInQuotes)
			{
				return pStart;
			}
			break;
		}

		pStart++;
	}
}


#define MAYBE_DONE { pathHolder = NULL; if (*pReadHead) goto BeginningOfLine; goto Done; }

int objPathParseOperations_Fast(ParseTable *table, const char *query, ObjectPathOperation ***pathOperations)
{
	const char *pReadHead = query;
	ObjectPathOperation *pathHolder = NULL;
	int iReadCount;
	int iSuccess = 1;
	ObjectPathOperation **ppDestroys = NULL;
	ObjectPathOperation **ppCreates = NULL;
	ObjectPathOperation **ppSets = NULL;

	if ( !query || !table)
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();
	PERFINFO_AUTO_START("Parse", 1);
	goto FirstLine;

BeginningOfLine:
	PERFINFO_AUTO_STOP_L3();
FirstLine:
	PERFINFO_AUTO_START_L3("BeginningOfLine", 1);

	while (isWhitespace(*pReadHead))
	{
		pReadHead++;
	}

	if (!*pReadHead)
	{
		goto Done;
	}

	if (IS(0, 's', 'S') &&
			IS(1, 'e', 'E') &&
			IS(2, 't', 'T'))
	{
		pReadHead += 3;
		goto BeginningOfSet;
	}
	else if(IS(0, 'c', 'C') &&
			IS(1, 'r', 'R') &&
			IS(2, 'e', 'E') &&
			IS(3, 'a', 'A') &&
			IS(4, 't', 'T') &&
			IS(5, 'e', 'E'))
	{
		pReadHead += 6;
		goto BeginningOfCreate;
	}
	else if(IS(0, 'd', 'D') &&
			IS(1, 'e', 'E') &&
			IS(2, 's', 'S') &&
			IS(3, 't', 'T') &&
			IS(4, 'r', 'R') &&
			IS(5, 'o', 'O') &&
			IS(6, 'y', 'Y'))
	{
		pReadHead += 7;
		goto BeginningOfDestroy;
	}
	else
	{
		goto SkipRestOfLine;
	}

	assert(0);
BeginningOfSet:
	PERFINFO_AUTO_STOP_START_L3("BeginningOfSet", 1);
	{
		const char *pBeginningOfPath, *pEndOfPath;
	
		PERFINFO_AUTO_START_L3("SkipSpaces", 1);
		SKIP_1_OR_MORE_SPACES_EXPECT_SOMETHING();
		PERFINFO_AUTO_STOP_START_L3("CreateOPO", 1);
		pathHolder = CreateObjectPathOperation();
		PERFINFO_AUTO_STOP_START_L3("OtherBits", 1);

		if (*pReadHead != '.')
		{
			estrConcatChar(&pathHolder->pathEString, '.');
		}

		pBeginningOfPath = pReadHead;
		PERFINFO_AUTO_STOP_START_L3("FindNonQuotedSpace", 1);
		pReadHead = FindNonQuotedSpace(pReadHead);
		PERFINFO_AUTO_STOP_L3();


		if (!pReadHead)
		{
			goto SkipRestOfLine;
		}

		pEndOfPath = pReadHead;
		pReadHead++;


		SKIP_0_OR_MORE_SPACES_EXPECT_SOMETHING();

		// Expected beginnings to operations are:
		// set, create, destroy, comment
		// Anything other than set, create, or destroy results in ignoring the line

		if (*pReadHead == '=')
		{
			pathHolder->op = TRANSOP_SET;
			pReadHead++;
		}
		else if(pReadHead[1] == '=')
		{
			switch(pReadHead[0])
			{
				xcase '+': pathHolder->op = TRANSOP_ADD;
				xcase '-': pathHolder->op = TRANSOP_SUB;
				xcase '*': pathHolder->op = TRANSOP_MULT;
				xcase '/': pathHolder->op = TRANSOP_DIV;
				xcase '|': pathHolder->op = TRANSOP_OR;
				xcase '&': pathHolder->op = TRANSOP_AND;
				xdefault:
				{
					goto SkipRestOfLine;
				}
			}

			pReadHead += 2;
		}	

		SKIP_1_OR_MORE_SPACES_EXPECT_SOMETHING();

		if (*pReadHead == '"')
		{
			bool bResult;
			pathHolder->quotedValue = true;
			pReadHead++;
			PERFINFO_AUTO_START_L3("ReadUntilQuote", 1);
			bResult = ReadUntilQuoteUnescaping(&pReadHead, &pathHolder->valueEString);
			PERFINFO_AUTO_STOP_L3();
			
			if (!bResult)
			{
				goto SkipRestOfLine;
			}
		}
		else
		{	bool bResult;
			PERFINFO_AUTO_START_L3("ReadUntilEOL", 1);
			bResult = ReadUntilEndOfLineOrStringUnescaping(&pReadHead, &pathHolder->valueEString);
			PERFINFO_AUTO_STOP_L3();

			if (!bResult)
			{
				goto SkipRestOfLine;
			}
		}

		READ_UNTIL_END_OF_STRING_OR_LINE_EXPECTING_NOTHING();

		PERFINFO_AUTO_START_L3("EstrConcat", 1);
		estrConcat_dbg_inline(&pathHolder->pathEString, pBeginningOfPath, pEndOfPath - pBeginningOfPath, __FILE__, __LINE__);
		PERFINFO_AUTO_STOP_L3();

		eaPush(&ppSets, pathHolder);

		MAYBE_DONE;
	}


	assert(0);
BeginningOfDestroy:
	PERFINFO_AUTO_STOP_START_L3("BeginningOfDestroy", 1);
	SKIP_1_OR_MORE_SPACES_EXPECT_SOMETHING();
	
	pathHolder = CreateObjectPathOperation();
	pathHolder->op = TRANSOP_DESTROY;
	goto CreateOrDestroyShared;

	assert(0);
BeginningOfCreate:
	PERFINFO_AUTO_STOP_START_L3("BeginningOfCreate", 1);
	SKIP_1_OR_MORE_SPACES_EXPECT_SOMETHING();
	
	pathHolder = CreateObjectPathOperation();
	pathHolder->op = TRANSOP_CREATE;
	goto CreateOrDestroyShared;

	assert(0);
CreateOrDestroyShared:
	PERFINFO_AUTO_STOP_START_L3("CreateOrDestroyShared", 1);
	{
		const char *pBeginningOfKey;
		const char *pLastLeftBracket = NULL;
		const char *pLastRightBracket = NULL;
		const char *pLastQuote = NULL;
		const char *pSecondToLastQuote = NULL;
		bool bInString = false;

		pBeginningOfKey = pReadHead;

		while (1)
		{
			switch (*pReadHead)
			{
			case 0:
			case '\n':
			case ' ':
				goto EndOfCreateOrDestroyWhileLoop;

			case '[':
				pLastLeftBracket = pReadHead;
				break;

			case ']':
				pLastRightBracket = pReadHead;
				break;

			case '"':
				pSecondToLastQuote = pReadHead;
				pReadHead++;
				pLastQuote = strchr(pReadHead, '"');
				if (!pLastQuote)
				{
					goto SkipRestOfLine;
				}
				pReadHead = pLastQuote;
				break;
			}
			pReadHead++;
		}
	EndOfCreateOrDestroyWhileLoop:
		PERFINFO_AUTO_STOP_START_L3("EndOfCreateOrDestroyWhileLoop", 1);

		//pReadHead now points directly after the end of the create token... so check for the various possibilities of brackets and quotes
		if (pLastRightBracket == pReadHead - 1)
		{
			if (!pLastLeftBracket)
			{
				goto SkipRestOfLine;
			}

			//if we end with brackets, and have quotes inside the brackets, the only legal configuration is for them to
			//be immediately inside the brackets
			if (pSecondToLastQuote > pLastLeftBracket)
			{
				if (!(pSecondToLastQuote == pLastLeftBracket + 1 && pLastQuote == pLastRightBracket -1))
				{
					goto SkipRestOfLine;
				}

				//our main token ends with ["foo"]
				//now assemble our estrings
				PERFINFO_AUTO_START_L3("estrAppendUnescaped_brackets_quotes", 1);
				iReadCount = estrAppendUnescapedCount(&pathHolder->valueEString, pSecondToLastQuote + 1, pLastQuote - pSecondToLastQuote - 1);
				PERFINFO_AUTO_STOP_L3();

				if (!iReadCount)
				{
					goto SkipRestOfLine;
				}

				pathHolder->quotedValue = true;

				PERFINFO_AUTO_START_L3("READUNTILEND_brackets_quotes", 1);
				READ_UNTIL_END_OF_STRING_OR_LINE_EXPECTING_NOTHING();
				PERFINFO_AUTO_STOP_L3();

				PERFINFO_AUTO_START_L3("estrconcat_brackets_quotes", 1);
				estrConcat_dbg_inline(&pathHolder->pathEString, pBeginningOfKey, pLastLeftBracket - pBeginningOfKey, __FILE__, __LINE__);
				PERFINFO_AUTO_STOP_L3();
				
				eaPush(pathHolder->op == TRANSOP_CREATE ? &ppCreates : &ppDestroys, pathHolder);
				MAYBE_DONE;
			}

			//we end with [foo]... because we assume that surrounding whitespace will be rare, we'll strip it at the very end
			PERFINFO_AUTO_START_L3("estrAppend_brackets", 1);
			iReadCount = estrAppendUnescapedCount(&pathHolder->valueEString, pLastLeftBracket + 1, pLastRightBracket - pLastLeftBracket - 1);
			PERFINFO_AUTO_STOP_L3();
			if (!iReadCount)
			{
				goto SkipRestOfLine;
			}


			PERFINFO_AUTO_START_L3("estrTrim_brackets", 1);
			estrTrimLeadingAndTrailingWhitespace(&pathHolder->valueEString);
			PERFINFO_AUTO_STOP_L3();

			PERFINFO_AUTO_START_L3("atoi", 1);
			pathHolder->iValueAtoid = atoi(pathHolder->valueEString);
			PERFINFO_AUTO_STOP_L3();

			PERFINFO_AUTO_START_L3("READUNTILEND_brackets", 1);
			READ_UNTIL_END_OF_STRING_OR_LINE_EXPECTING_NOTHING();
			PERFINFO_AUTO_STOP_L3();
			
			PERFINFO_AUTO_START_L3("estrConcat_brackets", 1);
			estrConcat_dbg_inline(&pathHolder->pathEString, pBeginningOfKey, pLastLeftBracket - pBeginningOfKey, __FILE__, __LINE__);
			PERFINFO_AUTO_STOP_L3();

			eaPush(pathHolder->op == TRANSOP_CREATE ? &ppCreates : &ppDestroys, pathHolder);

			MAYBE_DONE;
		}

		//we don't end with a close bracket... so whatever our big token is, we put it all in the path, nothing in the value
		PERFINFO_AUTO_START_L3("estrConcat_nobrackets", 1);
		estrConcat_dbg_inline(&pathHolder->pathEString, pBeginningOfKey, pReadHead - pBeginningOfKey, __FILE__, __LINE__);
		PERFINFO_AUTO_STOP_L3();

		PERFINFO_AUTO_START_L3("READUNTILEND_nobrackets", 1);
		READ_UNTIL_END_OF_STRING_OR_LINE_EXPECTING_NOTHING();
		PERFINFO_AUTO_STOP_L3();
	
		eaPush(pathHolder->op == TRANSOP_CREATE ? &ppCreates : &ppDestroys, pathHolder);
		MAYBE_DONE;
	}

	assert(0);
SkipRestOfLine:
	PERFINFO_AUTO_STOP_START_L3("SkipRestOfLine", 1);
	{
		const char *pEOL = strchr_fast(pReadHead, '\n');
		
		iSuccess = 0;

		if (pathHolder)
		{
			DestroyObjectPathOperation(pathHolder);
			pathHolder = NULL;
		}

		if (pEOL)
		{
			pReadHead = pEOL + 1;
			goto BeginningOfLine;
		}
		else
		{
			goto Done;
		}
	}

	assert(0);
Done:
	PERFINFO_AUTO_STOP_L3();
	PERFINFO_AUTO_STOP_START("Qsort", 1);

	eaQSortG(ppCreates, cmpCreates);
	eaQSortG(ppDestroys, cmpDestroys);
	
	PERFINFO_AUTO_STOP_START("eaPushEarray", 1);
	eaPushEArray(pathOperations, &ppDestroys);
	eaPushEArray(pathOperations, &ppCreates);
	eaPushEArray(pathOperations, &ppSets);

	eaDestroy(&ppDestroys);
	eaDestroy(&ppCreates);
	eaDestroy(&ppSets);

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
	return iSuccess;
}


#if 0
//------------------------------
//Test stuff for objPathParseOperations_Fast




#include "objPath.h"
#include "StringUtil.h"

extern int objPathParseOperations_Fast(ParseTable *table, const char *query, ObjectPathOperation ***pathOperations);
extern int objPathParseOperations(ParseTable *table, const char *query, ObjectPathOperation ***pathOperations);

bool objPathOperationCompare(ObjectPathOperation *pOperation1, ObjectPathOperation *pOperation2)
{
	if (!!pOperation1 != !!pOperation2)
	{
		return false;
	}

	if (!pOperation1)
	{
		return true;
	}

	if (pOperation1->op != pOperation2->op)
	{
		return false;
	}

	if (stricmp_safe(pOperation1->pathEString, pOperation2->pathEString) != 0)
	{
		return false;
	}

	if (stricmp_safe(pOperation1->valueEString, pOperation2->valueEString) != 0)
	{
		return false;
	}

	if (pOperation1->quotedValue != pOperation2->quotedValue)
	{
		return false;
	}

	return true;


}

void ParseOperationsTest(void)
{
	ObjectPathOperation **ppOperations1 = NULL;
	ObjectPathOperation **ppOperations2 = NULL;
	char *pStr_raw = fileAlloc("c:\\temp\\path_ops.txt", NULL);
	
	char **ppStringsToTest = NULL;
	char *pCurReadHead = pStr_raw;
	char *pCurStringBeingBuilt = NULL;

	S64 siStartTime;
	S64 iTime1, iTime2;
	int i;
//	int j;

	while (1)
	{
		char *pNextEOL = strchr(pCurReadHead, '\n');
		char *pTempString = NULL;
		assert(pNextEOL);
		*pNextEOL = 0;
		
		estrCopy2(&pTempString, pCurReadHead);
		estrTrimLeadingAndTrailingWhitespace(&pTempString);

		if (estrLength(&pTempString) == 0)
		{
			eaPush(&ppStringsToTest, pCurStringBeingBuilt);
			pCurStringBeingBuilt = NULL;
			if (eaSize(&ppStringsToTest) == 10240)
			{
				break;
			}
		}

		estrConcatf(&pCurStringBeingBuilt, "%s%s", estrLength(&pCurStringBeingBuilt) == 0 ? "" : "\n", pTempString);
		pCurReadHead = pNextEOL + 1;
	}

	free(pStr_raw);
	

	for (i = 0; i < eaSize(&ppStringsToTest); i++)
	{
		int iResult1 = objPathParseOperations_Fast(NULL, ppStringsToTest[i], &ppOperations1);
		int iResult2 = objPathParseOperations(NULL, ppStringsToTest[i], &ppOperations2);

		if (iResult1 != iResult2)
		{
			assert(0);
		}

	/*	if (eaSize(&ppOperations1) != eaSize(&ppOperations2))
		{
			assert(0);
		}
		for (j = 0; j < eaSize(&ppOperations1); j++)
		{
			if (!objPathOperationCompare(ppOperations1[j], ppOperations2[j]))
			{
				assert(0);
			}
		}*/

		eaDestroyEx(&ppOperations1, DestroyObjectPathOperation);
		eaDestroyEx(&ppOperations2, DestroyObjectPathOperation);

	}



//	while (1)
	{
		siStartTime = timeGetTime();
		for (i = 0; i < eaSize(&ppStringsToTest); i++)
		{
			objPathParseOperations_Fast(NULL, ppStringsToTest[i], &ppOperations1);

			PERFINFO_AUTO_START("eadestroy", 1);
			eaDestroyEx(&ppOperations1, DestroyObjectPathOperation);
			PERFINFO_AUTO_STOP();
		}
		iTime1 = timeGetTime() - siStartTime;
	}
	
	siStartTime = timeGetTime();
	for (i = 0; i < eaSize(&ppStringsToTest); i++)
	{
		objPathParseOperations(NULL, ppStringsToTest[i], &ppOperations1);
		eaDestroyEx(&ppOperations1, DestroyObjectPathOperation);
	}	
	
	iTime2 = timeGetTime() - siStartTime;


	printf("Time1: %d msecs. Time2: %d msecs\n", (int)iTime1, (int)iTime2);

	siStartTime = timeGetTime();
	for (i = 0; i < eaSize(&ppStringsToTest); i++)
	{
		objPathParseOperations_Fast(NULL, ppStringsToTest[i], &ppOperations1);
		eaDestroyEx(&ppOperations1, DestroyObjectPathOperation);
	}
	iTime1 = timeGetTime() - siStartTime;
	
	siStartTime = timeGetTime();
	for (i = 0; i < eaSize(&ppStringsToTest); i++)
	{
		objPathParseOperations(NULL, ppStringsToTest[i], &ppOperations1);
		eaDestroyEx(&ppOperations1, DestroyObjectPathOperation);
	}	
	
	iTime2 = timeGetTime() - siStartTime;


	printf("Time1: %d msecs. Time2: %d msecs\n", (int)iTime1, (int)iTime2);

}


#endif




#include "AutoGen/objPath_h_ast.c"
