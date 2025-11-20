#include "cmdparse.h"
#include "file.h"
#include "textparser.h"
#include "estring.h"
#include "earray.h"
#include "GlobalTypes.h"
#include "StringUtil.h"
#include "structInternals.h"
#include "Stashtable.h"

#include "CommandAndStructContracts_c_ast.h"

//the basic idea of this file is to allow someone to set up a "contract", which is a file saying "here are some commands,
//parsetables, and staticdefines that I expect to exist, and some details about them that I expect to be true. If any
//of these are not true, please verbosely explain what and how


AUTO_STRUCT;
typedef struct CommandContractArg
{
	char *pArgName; AST(STRUCTPARAM)
	char *pArgTypeName; AST(STRUCTPARAM)
	char *pArgTypeStructName; AST(STRUCTPARAM)
} CommandContractArg;

AUTO_STRUCT;
typedef struct CommandContractRetVal
{
	char *pRetValTypeName; AST(STRUCTPARAM)
	char *pRetValStructTypeName; AST(STRUCTPARAM)
} CommandContractRetVal;

AUTO_STRUCT;
typedef struct CommandContractDependency
{
	char *pObjectName; AST(KEY)
	char *pComment; AST(ESTRING)
} CommandContractDependency;

AUTO_STRUCT;
typedef struct CommandContract
{
	int iLineNum; AST(LINENUM)
	char *pCommandName; AST(STRUCTPARAM KEY)
	CommandContractArg **ppArgs; AST(NAME(Arg))
	CommandContractRetVal *pRetVal;


	//used internally while calculating the above
	CommandContractDependency **ppDependentStructs_Unprocessed;
	CommandContractDependency **ppDependentStructs_Processed;

	CommandContractDependency **ppDependentStaticDefines;
} CommandContract;

AUTO_STRUCT;
typedef struct StructContractField
{
	char *pFieldName; AST(STRUCTPARAM)
	char *pTypeName; AST(STRUCTPARAM)
	char *pStructOrEnumName; AST(STRUCTPARAM)
} StructContractField;

AUTO_STRUCT;
typedef struct StructContract
{
	int iLineNum; AST(LINENUM)
	char *pStructName; AST(STRUCTPARAM KEY)
	StructContractField **ppFields; AST(NAME(Field))
	bool bErrors;
} StructContract;

AUTO_STRUCT;
typedef struct StaticDefineField
{
	char *pName; AST(STRUCTPARAM)
	int iValue; AST(STRUCTPARAM INDEX_DEFINE)
	U32 iUsed[1]; AST(USEDFIELD INDEX_DEFINE)
} StaticDefineField;


AUTO_STRUCT;
typedef struct StaticDefineContract
{
	int iLineNum; AST(LINENUM)
	char *pStaticDefineName; AST(STRUCTPARAM KEY)
	StaticDefineField **ppFields; AST(NAME(Field))
	bool bErrors;
} StaticDefineContract;

AUTO_STRUCT;
typedef struct CommandAndStructContract
{
	CommandContract **ppCommandContracts; AST(NAME(Command))
	StructContract **ppStructContracts; AST(NAME(Struct))
	StaticDefineContract **ppStaticDefineContracts; AST(NAME(StaticDefine))
} CommandAndStructContract;

static char *spOutFileName = NULL;
static char *spInFileName = NULL;
static FILE *spOutFile = NULL;

static void ERROR(FORMAT_STR const char *pFmt, ...)
{
	char *pFullErrorString = NULL;
	estrGetVarArgs(&pFullErrorString, pFmt);
	
	printf("CommandAndStructContract error: %s\n", pFullErrorString);

	if (!spOutFile)
	{
		mkdirtree_const(spOutFileName);
		spOutFile = fopen(spOutFileName, "at");
		assertmsgf(spOutFile, "Unable to open file %s to report CommandAndStructContract errors coming from file %s. First error: %s\n",
			spOutFileName, spInFileName, pFullErrorString);
		fprintf(spOutFile, "\n-------Contract errors from file %s, server type %s---------\n",
			spInFileName, GlobalTypeToName(GetAppGlobalType()));
	}

	fprintf(spOutFile, "\n%s\n", pFullErrorString);
}

static void DONE(void)
{
	printf("Done with CommandAndStructContract, quitting\n");
	if (spOutFile)
	{
		fclose(spOutFile);
	}

	exit(-1);
}

void GetTypeAndStructName(DataDesc *pArg, /*NOT ESTRNGS*/ const char **ppTypeName, const char **ppStructTypeName)
{
	*ppTypeName = MultiValTypeToReadableString(pArg->type);
	if (pArg->type == MULTI_NP_POINTER)
	{
		*ppStructTypeName = ParserGetTableName(pArg->ptr);
		if (!(*ppStructTypeName))
		{
			*ppStructTypeName = "UNKNOWN";
		}
	}
	else
	{
		*ppStructTypeName = NULL;
	}
}

void VerifyCommandContract(CommandContract *pCommandContract)
{	
	Cmd *pCmd;
	int i;

	if (!(pCommandContract->pCommandName && pCommandContract->pCommandName[0]))
	{
		ERROR("Found a command on line %d with no name", pCommandContract->iLineNum);
		return;
	}

	pCmd = cmdListFind(&gGlobalCmdList, pCommandContract->pCommandName);
	if (!pCmd)
	{
		pCmd = cmdListFind(&gPrivateCmdList, pCommandContract->pCommandName);
		
		if (!pCmd)
		{
			ERROR("Command %s does not exist", pCommandContract->pCommandName);
			return;
		}
	}

	if (pCmd->return_type.type == MULTI_NONE)
	{
		if (pCommandContract->pRetVal)
		{
			ERROR("Command %s has no return value, but one is specified in the contract", pCommandContract->pCommandName);
		}
	}
	else
	{
		const char *pTypeName;
		const char *pTypeStructName;

		GetTypeAndStructName(&pCmd->return_type, &pTypeName, &pTypeStructName);

		if (!pCommandContract->pRetVal)
		{
			ERROR("Command %s has a return value (type %s), but none is specified in the contract", 
				pCommandContract->pCommandName, pTypeName);
		}
		else
		{
			if (stricmp_safe(pTypeName, pCommandContract->pRetVal->pRetValTypeName) != 0)
			{
				ERROR("Command %s has return value of type %s, contract thinks it should be %s", 
					pCommandContract->pCommandName, pTypeName, pCommandContract->pRetVal->pRetValTypeName);
			}

			if (stricmp_safe(pTypeStructName, pCommandContract->pRetVal->pRetValStructTypeName) != 0)
			{
				ERROR("Command %s has return value struct %s, contract thinks it should be %s",
					pCommandContract->pCommandName, pTypeStructName, pCommandContract->pRetVal->pRetValStructTypeName);
			}
		}
	}

	if (pCmd->iNumLogicalArgs != eaSize(&pCommandContract->ppArgs))
	{
		ERROR("Command %s had %d args, contract thinks it should have %d args", pCommandContract->pCommandName,
			pCmd->iNumLogicalArgs, eaSize(&pCommandContract->ppArgs));
		return;
	}

	for (i = 0; i < pCmd->iNumLogicalArgs; i++)
	{
		DataDesc *pCurArg = &pCmd->data[i];
		CommandContractArg *pCurArgContract = pCommandContract->ppArgs[i];
		const char *pTypeName;
		const char *pTypeStructName;

		GetTypeAndStructName(pCurArg, &pTypeName, &pTypeStructName);


		if (stricmp_safe(pCurArg->pArgName, pCurArgContract->pArgName) != 0)
		{
			ERROR("Command %s's arg #%d is named %s, contract thinks it should be named %s", 
				pCommandContract->pCommandName, i, pCurArg->pArgName, pCurArgContract->pArgName);
		}

		if (stricmp_safe(pCurArgContract->pArgTypeName, pTypeName) != 0)
		{
			ERROR("Command %s, arg %s, type is %s, contract thinks it should be %s", 
				pCommandContract->pCommandName, pCurArg->pArgName, pTypeName, pCurArgContract->pArgTypeName);
		}

		if (stricmp_safe(pCurArgContract->pArgTypeStructName, pTypeStructName) != 0)
		{
			ERROR("Command %s, arg %s, struct type is %s, contract thinks it should be %s", 
				pCommandContract->pCommandName, pCurArg->pArgName, pTypeStructName, pCurArgContract->pArgTypeStructName);
		}
	}
}

#define STRUCT_ERROR(...) { pStructContract->bErrors = true; ERROR(__VA_ARGS__); }

void VerifyStructContract(StructContract *pStructContract)
{
	ParseTable *pParseTable;

	if (!(pStructContract->pStructName && pStructContract->pStructName[0]))
	{
		STRUCT_ERROR("Found a struct with no name on line %d", pStructContract->iLineNum);
		return;
	}

	pParseTable = ParserGetTableFromStructName(pStructContract->pStructName);

	if (!pParseTable)
	{
		STRUCT_ERROR("Struct %s does not exist", pStructContract->pStructName);
		return;
	}

	FOR_EACH_IN_EARRAY(pStructContract->ppFields, StructContractField, pField)
	{
		int iFoundIndex = -1;
		int i;
		const char *pExpectedTypeName = NULL;
		const char *pExpectedStructOrStaticDefineName = NULL;

		if(!(pField->pFieldName && pField->pFieldName[0]))
		{
			STRUCT_ERROR("Found a field with no name in struct %s", pStructContract->pStructName);
			continue;
		}

		FORALL_PARSETABLE(pParseTable, i)
		{
	
			if (stricmp_safe(pField->pFieldName, pParseTable[i].name) == 0)
			{
				if (pParseTable[i].type & TOK_REDUNDANTNAME)
				{
					iFoundIndex = ParseInfoFindAliasedField(pParseTable, i);
				}
				else
				{
					iFoundIndex = i;
				}
				break;
			}
		}

		if (iFoundIndex == -1)
		{
			STRUCT_ERROR("Struct %s has no field named %s, contract wants one", pStructContract->pStructName, pField->pFieldName);
			continue;
		}

		pExpectedTypeName = ParseInfoGetType(pParseTable, iFoundIndex, NULL);

		if (TOK_HAS_SUBTABLE(pParseTable[iFoundIndex].type))
		{
			if (pParseTable[iFoundIndex].subtable)
			{
				pExpectedStructOrStaticDefineName = ParserGetTableName(pParseTable[iFoundIndex].subtable);
			}
			else
			{
				pExpectedStructOrStaticDefineName = "UNKNOWN";
			}
		}

		if (TYPE_INFO(pParseTable[iFoundIndex].type).interpretfield(pParseTable, iFoundIndex, SubtableField) == StaticDefineList && pParseTable[iFoundIndex].subtable)
		{
			const char *pName = FindStaticDefineName(pParseTable[iFoundIndex].subtable);
			if (pName)
			{
				pExpectedStructOrStaticDefineName = pName;
			}
			else
			{
				pExpectedStructOrStaticDefineName = "UNKNOWN";
			}
		}

		if (stricmp_safe(pExpectedTypeName, pField->pTypeName) != 0)
		{
			STRUCT_ERROR("Struct %s, field %s, has type %s, contract specifies %s", pStructContract->pStructName, pField->pFieldName,
				pExpectedTypeName, pField->pTypeName);
		}

		if (stricmp_safe(pExpectedStructOrStaticDefineName, pField->pStructOrEnumName) != 0)
		{
			STRUCT_ERROR("Struct %s, field %s, has struct or enum name %s, contract specifies %s", pStructContract->pStructName, pField->pFieldName,
				pExpectedStructOrStaticDefineName, pField->pStructOrEnumName);
		}
	}
	FOR_EACH_END;
}

#define STATICDEFINE_ERROR(...) { pStaticDefineContract->bErrors = true; ERROR(__VA_ARGS__); }

void VerifyStaticDefineContract(StaticDefineContract *pStaticDefineContract)
{
	StaticDefineInt *pStaticDefine;

	if (!(pStaticDefineContract->pStaticDefineName && pStaticDefineContract->pStaticDefineName[0]))
	{
		STATICDEFINE_ERROR("Found a staticDefine with no name on line %d", pStaticDefineContract->iLineNum);
		return;
	}

	pStaticDefine = FindNamedStaticDefine(pStaticDefineContract->pStaticDefineName);

	if (!pStaticDefine)
	{
		STATICDEFINE_ERROR("StaticDefine %s does not exist", pStaticDefineContract->pStaticDefineName);
		return;
	}

	FOR_EACH_IN_EARRAY(pStaticDefineContract->ppFields, StaticDefineField, pField)
	{
		int iActualVal;

		if (!(pField->pName && pField->pName[0]))
		{
			STATICDEFINE_ERROR("StaticDefine %s has a field with no name", pStaticDefineContract->pStaticDefineName);
			continue;
		}

		if ((iActualVal = StaticDefineInt_FastStringToInt(pStaticDefine, pField->pName, INT_MIN)) == INT_MIN)
		{
			STATICDEFINE_ERROR("StaticDefine %s has no field named %s, contract expects one", pStaticDefineContract->pStaticDefineName, pField->pName);
			continue;
		}

		if (TokenIsSpecified(parse_StaticDefineField, PARSE_STATICDEFINEFIELD_VALUE_INDEX, pField, PARSE_STATICDEFINEFIELD_USED_INDEX))
		{
			if (iActualVal != pField->iValue)
			{
				STATICDEFINE_ERROR("StaticDefine %s, field %s, has value %d, contract wants value %d", pStaticDefineContract->pStaticDefineName, pField->pName,
					iActualVal, pField->iValue);
			}
		}	
	}
	FOR_EACH_END;
}

void AddPotentialDependency(CommandAndStructContract *pOverallContract, CommandContract *pCommandContract, char *pTypeName, FORMAT_STR const char *pCommentFmt, ...)
{
	char *pComment = NULL;
	StructContract *pStructContract = NULL;
	StaticDefineContract *pStaticDefineContract = NULL;
	CommandContractDependency *pDependency;

	
	if (eaIndexedFindUsingString(&pCommandContract->ppDependentStructs_Unprocessed, pTypeName) != -1)
	{
		return;
	}

	if (eaIndexedFindUsingString(&pCommandContract->ppDependentStructs_Processed, pTypeName) != -1)
	{
		return;
	}

	if (eaIndexedFindUsingString(&pCommandContract->ppDependentStaticDefines, pTypeName) != -1)
	{
		return;
	}

	pStructContract = eaIndexedGetUsingString(&pOverallContract->ppStructContracts, pTypeName);
	pStaticDefineContract = eaIndexedGetUsingString(&pOverallContract->ppStaticDefineContracts, pTypeName);

	if (pStructContract)
	{
		pDependency = StructCreate(parse_CommandContractDependency);
		pDependency->pObjectName = strdup(pTypeName);
		estrGetVarArgs(&pDependency->pComment, pCommentFmt);

		eaPush(&pCommandContract->ppDependentStructs_Unprocessed, pDependency);
	}

	if (pStaticDefineContract)
	{
		pDependency = StructCreate(parse_CommandContractDependency);
		pDependency->pObjectName = strdup(pTypeName);
		estrGetVarArgs(&pDependency->pComment, pCommentFmt);

		eaPush(&pCommandContract->ppDependentStaticDefines, pDependency);
	}
}




void CalculateCommandDependencies(CommandAndStructContract *pContract)
{
	FOR_EACH_IN_EARRAY(pContract->ppCommandContracts, CommandContract, pCommandContract)
	{
		if (pCommandContract->pRetVal && pCommandContract->pRetVal->pRetValStructTypeName)
		{
			AddPotentialDependency(pContract, pCommandContract, pCommandContract->pRetVal->pRetValStructTypeName, "Return value of command");
		}

		FOR_EACH_IN_EARRAY(pCommandContract->ppArgs, CommandContractArg, pArg)
		{
			if (pArg->pArgTypeStructName)
			{
				AddPotentialDependency(pContract, pCommandContract, pArg->pArgTypeStructName, "Arg %s has struct/staticefine type %s",
					pArg->pArgName, pArg->pArgTypeStructName);
			}
		}
		FOR_EACH_END;

		while (eaSize(&pCommandContract->ppDependentStructs_Unprocessed))
		{
			CommandContractDependency *pCurDependency = eaPop(&pCommandContract->ppDependentStructs_Unprocessed);
			StructContract *pStructContract = eaIndexedGetUsingString(&pContract->ppStructContracts, pCurDependency->pObjectName);
	
			eaPush(&pCommandContract->ppDependentStructs_Processed, pCurDependency);

			assert(pStructContract);

			FOR_EACH_IN_EARRAY(pStructContract->ppFields, StructContractField, pField)
			{
				if (!pField->pStructOrEnumName)
				{
					continue;
				}

				if (eaIndexedGetUsingString(&pCommandContract->ppDependentStructs_Processed, pField->pStructOrEnumName) 
					|| eaIndexedGetUsingString(&pCommandContract->ppDependentStructs_Unprocessed, pField->pStructOrEnumName) 
					|| eaIndexedGetUsingString(&pCommandContract->ppDependentStaticDefines, pField->pStructOrEnumName))
				{
					continue;
				}

				AddPotentialDependency(pContract, pCommandContract, pField->pStructOrEnumName, "%s THEN struct %s, field %s, has struct/enum type %s", 
					pCurDependency->pComment, pStructContract->pStructName, pField->pFieldName, pField->pStructOrEnumName);
			}
			FOR_EACH_END;
		}
	}
	FOR_EACH_END;
}

AUTO_COMMAND;
void VerifyCommandAndStructContractAndQuit(char *pInFileName, char *pOutFileName)
{
	CommandAndStructContract *pContract = StructCreate(parse_CommandAndStructContract);
	char *pErrorString = NULL;
	int iRetVal;

	SetLateCreateIndexedEArrays(false);

	printf("CommandAndStructContract: going to load from %s, put output into %s\n", 
		pInFileName, pOutFileName);

	estrCopy2(&spOutFileName, pOutFileName);
	estrCopy2(&spInFileName, pInFileName);

	
	ErrorfPushCallback(EstringErrorCallback, (void*)(&pErrorString));
	iRetVal = ParserReadTextFile(pInFileName, parse_CommandAndStructContract, pContract, 0);
	ErrorfPopCallback();

	if (!iRetVal)
	{
		ERROR("Textparser errors while reading %s:\n%s\n", pInFileName, pErrorString);
		DONE();
	}

	FOR_EACH_IN_EARRAY(pContract->ppCommandContracts, CommandContract, pCommandContract)
	{
		VerifyCommandContract(pCommandContract);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pContract->ppStructContracts, StructContract, pStructContract)
	{
		if (eaIndexedFindUsingString(&pContract->ppStaticDefineContracts, pStructContract->pStructName) != -1)
		{
			ERROR("We have both a struct and a staticdefine named %s. This may lead to dependency errors", pStructContract->pStructName);
		}

		VerifyStructContract(pStructContract);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pContract->ppStaticDefineContracts, StaticDefineContract, pStaticDefineContract)
	{
		VerifyStaticDefineContract(pStaticDefineContract);
	}
	FOR_EACH_END;

	CalculateCommandDependencies(pContract);

	FOR_EACH_IN_EARRAY(pContract->ppCommandContracts, CommandContract, pCommandContract)
	{
		FOR_EACH_IN_EARRAY(pCommandContract->ppDependentStructs_Processed, CommandContractDependency, pDependency)
		{
			StructContract *pStruct = eaIndexedGetUsingString(&pContract->ppStructContracts, pDependency->pObjectName);

			if (pStruct && pStruct->bErrors)
			{
				ERROR("\nCommand %s is potentially broken, because of errors in struct %s.\nDependency because:\n%s\n\n",
					pCommandContract->pCommandName, pStruct->pStructName, pDependency->pComment);
			}
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pCommandContract->ppDependentStaticDefines, CommandContractDependency, pDependency)
		{
			StaticDefineContract *pStaticDefine = eaIndexedGetUsingString(&pContract->ppStaticDefineContracts, pDependency->pObjectName);

			if (pStaticDefine && pStaticDefine->bErrors)
			{
				ERROR("\nCommand %s is potentially broken, because of errors in StaticDefine %s.\nDependency because:\n%s\n\n",
					pCommandContract->pCommandName, pStaticDefine->pStaticDefineName, pDependency->pComment);
			}
		}
		FOR_EACH_END;
	}
	FOR_EACH_END


	DONE();
}

void CreateAndAddCommandContract(CommandAndStructContract *pMainContract, Cmd *pCmd)
{
	CommandContract *pContract = StructCreate(parse_CommandContract);
	int i;
	const char *pTypeName;
	const char *pTypeStructName;

	pContract->pCommandName = strdup(pCmd->name);

	if (pCmd->return_type.type != MULTI_NONE)
	{

		pContract->pRetVal = StructCreate(parse_CommandContractRetVal);
	
		GetTypeAndStructName(&pCmd->return_type, &pTypeName, &pTypeStructName);

		if (pTypeName)
		{
			pContract->pRetVal->pRetValTypeName = strdup(pTypeName);
		}
		if (pTypeStructName)
		{
			pContract->pRetVal->pRetValStructTypeName = strdup(pTypeStructName);
		}
	}

	for (i = 0; i < pCmd->iNumLogicalArgs; i++)
	{
		DataDesc *pCurArg = &pCmd->data[i];
		CommandContractArg *pArgContract = StructCreate(parse_CommandContractArg);

		pArgContract->pArgName = strdup(pCurArg->pArgName);
		GetTypeAndStructName(pCurArg, &pTypeName, &pTypeStructName);

		if (pTypeName)
		{
			pArgContract->pArgTypeName = strdup(pTypeName);
		}

		if (pTypeStructName)
		{
			pArgContract->pArgTypeStructName = strdup(pTypeStructName);
		}

		eaPush(&pContract->ppArgs, pArgContract);
	}

	eaPush(&pMainContract->ppCommandContracts, pContract);

}

void CreateAndAddStructContract(CommandAndStructContract *pMainContract, ParseTable *pTPI)
{
	int i;

	StructContract *pContract = StructCreate(parse_StructContract);
	pContract->pStructName = strdup(ParserGetTableName(pTPI));

	FORALL_PARSETABLE(pTPI, i)
	{
		StructTypeField type = TOK_GET_TYPE(pTPI[i].type);
		StructContractField *pFieldContract;

		if (type == TOK_IGNORE) continue;
		if (pTPI[i].type & TOK_REDUNDANTNAME) continue;
		if (type == TOK_START) continue;
		if (type == TOK_END) continue;



		pFieldContract = StructCreate(parse_StructContractField);
		pFieldContract->pFieldName = strdup(pTPI[i].name);

		pFieldContract->pTypeName = strdup(ParseInfoGetType(pTPI, i, NULL));

		if (TOK_HAS_SUBTABLE(pTPI[i].type))
		{
			if (pTPI[i].subtable)
			{
				pFieldContract->pStructOrEnumName = strdup(ParserGetTableName(pTPI[i].subtable));
			}
			else
			{
				pFieldContract->pStructOrEnumName = strdup("UNKNOWN");
			}
		}

		if (TYPE_INFO(pTPI[i].type).interpretfield(pTPI, i, SubtableField) == StaticDefineList && pTPI[i].subtable)
		{
			const char *pName = FindStaticDefineName(pTPI[i].subtable);
			if (pName)
			{
				pFieldContract->pStructOrEnumName = strdup(pName);
			}
			else
			{
				pFieldContract->pStructOrEnumName = strdup("UNKNOWN");
			}
		}

		eaPush(&pContract->ppFields, pFieldContract);
	}

	eaPush(&pMainContract->ppStructContracts, pContract);

}

void CreateAndAddStaticDefineContract(CommandAndStructContract *pMainContract, StaticDefineInt *pStaticDefine)
{
	char **ppKeys = NULL;
	U32 *piValues = NULL;
	int i;
	
	StaticDefineContract *pContract = StructCreate(parse_StaticDefineContract);

	pContract->pStaticDefineName = strdup(FindStaticDefineName(pStaticDefine));

	DefineFillAllKeysAndValues(pStaticDefine, &ppKeys, &piValues);

	for (i = 0; i < eaSize(&ppKeys); i++)
	{
		StaticDefineField *pField = StructCreate(parse_StaticDefineField);
		pField->pName = strdup(ppKeys[i]);
		pField->iValue = piValues[i];

		eaPush(&pContract->ppFields, pField);
	}

	eaPush(&pMainContract->ppStaticDefineContracts, pContract);
}

AUTO_COMMAND;
void DumpAllContracts(char *pOutFileName)
{
	CommandAndStructContract *pContract = StructCreate(parse_CommandAndStructContract);

	FOR_EACH_IN_STASHTABLE(gGlobalCmdList.sCmdsByName, Cmd, pCmd)
	{
		CreateAndAddCommandContract(pContract, pCmd);
	}
	FOR_EACH_END;

	FOR_EACH_IN_STASHTABLE(gPrivateCmdList.sCmdsByName, Cmd, pCmd)
	{
		CreateAndAddCommandContract(pContract, pCmd);
	}
	FOR_EACH_END;


	FOR_EACH_IN_STASHTABLE(gParseTablesByName, ParseTable, pTPI)
	{
		CreateAndAddStructContract(pContract, pTPI);
	}
	FOR_EACH_END;

	FOR_EACH_IN_STASHTABLE(sStaticDefinesByName, StaticDefineInt, pStaticDefineInt)
	{

		CreateAndAddStaticDefineContract(pContract, pStaticDefineInt);
	}
	FOR_EACH_END;

	ParserWriteTextFile(pOutFileName, parse_CommandAndStructContract, pContract, 0, 0);

	StructDestroy(parse_CommandAndStructContract, pContract);
}


#include "CommandAndStructContracts_c_ast.c"

