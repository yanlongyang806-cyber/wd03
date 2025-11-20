#include "CmdParseJson.h"
#include "cJSON.h"
#include "StringUtil.h"
#include "cmdparse.h"
#include "multival.h"
#include "earray.h"
#include "textParserJson.h"
#include "textparser.h"
#include "globalEnums.h"

typedef enum JsonRPCVersion
{
	JSONRPC_10,
	JSONRPC_11,
	JSONRPC_20,
} JsonRPCVersion;

typedef struct TempStructHandle
{
	ParseTable *pTPI;
	void *pStruct;
} TempStructHandle;

typedef struct TempStructContext
{
	TempStructHandle **ppTempStructs;
} TempStructContext;

CmdParseJsonMissingCallback gMissingCallback = NULL;

static void ClearTempStructs(TempStructContext *pContext)
{
	FOR_EACH_IN_EARRAY(pContext->ppTempStructs, TempStructHandle, pHandle)
	{
		StructDestroyVoid(pHandle->pTPI, pHandle->pStruct);
	}
	FOR_EACH_END;

	eaDestroyEx(&pContext->ppTempStructs, NULL);
}

static void AddTempStruct(TempStructContext *pContext, ParseTable *pTPI, void *pStruct)
{
	TempStructHandle *pHandle = malloc(sizeof(TempStructHandle));
	pHandle->pTPI = pTPI;
	pHandle->pStruct = pStruct;

	eaPush(&pContext->ppTempStructs, pHandle);
}

static void *StructCreateTemp(ParseTable *pTPI, TempStructContext *pContext)
{

	void *pStruct = StructCreateVoid(pTPI);
	AddTempStruct(pContext, pTPI, pStruct);
	return pStruct;
}

char *GetStringChild(cJSON *pJson, char *pName)
{
	cJSON *pChild = cJSON_GetObjectItem(pJson, pName);
	if (!pChild)
	{
		return NULL;
	}

	if (pChild->type != cJSON_String)
	{
		return NULL;
	}

	return pChild->valuestring;
}

bool GetIntChild(cJSON *pJson, char *pName, int *pOutVal)
{
	cJSON *pChild = cJSON_GetObjectItem(pJson, pName);
	if (!pChild)
	{
		return false;
	}

	if (pChild->type != cJSON_Number)
	{
		return false;
	}

	*pOutVal = pChild->valueint;
	return true;
}


static char *GetJsonTypeName(int iType)
{
	switch (iType)
	{
	case cJSON_False: return "False";
	case cJSON_True: return "True";
	case cJSON_NULL: return "NULL";
	case cJSON_Number: return "Number";
	case  cJSON_String: return "String";
	case cJSON_Array: return "Array";
	case cJSON_Object: return "Object";
	default: return "Unknown";
	}
}

static char *CheckArg(DataDesc *pArg, cJSON *pParam)
{
	static char *spRetVal = NULL;

	estrDestroy(&spRetVal);

	switch (pArg->type)
	{
	xcase MULTI_INT:	
		if (pParam->type != cJSON_Number)
		{
			estrPrintf(&spRetVal, "Type mismatch for paramater %s... expected INT, got %s", 
				pArg->pArgName, GetJsonTypeName(pParam->type));
		}

	xcase MULTI_FLOAT:
		if (pParam->type != cJSON_Number)
		{
			estrPrintf(&spRetVal, "Type mismatch for paramater %s... expected FLOAT, got %s", 
				pArg->pArgName, GetJsonTypeName(pParam->type));
		}
				
	xcase MULTI_STRING:
		if (pParam->type != cJSON_String)
		{
			estrPrintf(&spRetVal, "Type mismatch for paramater %s... expected STRING, got %s", 
				pArg->pArgName, GetJsonTypeName(pParam->type));
		}

	xcase MULTI_NP_POINTER:
		if (pParam->type != cJSON_Object)
		{
			estrPrintf(&spRetVal, "Type mismatch for paramater %s... expected OBJECT, got %s", 
				pArg->pArgName, GetJsonTypeName(pParam->type));
		}
	xdefault:
		estrPrintf(&spRetVal, "arg %s has type %u, unsupported for JSONRPC",
			pArg->pArgName, pArg->type);
	}

	return spRetVal;
}

MultiVal *CreateMultiValArg(DataDesc *pArg, cJSON *pParam, TempStructContext *pTempStructContext)
{
	MultiVal *pRetVal = NULL;

	switch (pParam->type)
	{
	xcase cJSON_Number:
		pRetVal = MultiValCreate();
		if (pArg->type == MULTI_INT)
		{
			MultiValSetInt(pRetVal, pParam->valueint);
		}
		else
		{
			MultiValSetFloat(pRetVal, pParam->valuedouble);
		}

	xcase cJSON_String:
		pRetVal = MultiValCreate();
		MultiValSetString(pRetVal, pParam->valuestring);
		

	xcase cJSON_Object:
		{
			char *pResultString = NULL;
			void *pStruct = json_convert_struct(pParam, (ParseTable*)(pArg->ptr), &pResultString);

			//TODO - do something with this string
			estrDestroy(&pResultString);

			if (!pStruct)
			{
				return NULL;
			}

			pRetVal = MultiValCreate();
			MultiValReferencePointer(pRetVal, pStruct);
			AddTempStruct(pTempStructContext, (ParseTable*)(pArg->ptr), pStruct);
		}
	}

	return pRetVal;
}

MultiVal *MakeDefaultMultiValArg(Cmd *pCmd, DataDesc *pArg, TempStructContext *pTempStructContext)
{
	MultiVal *pRetVal = NULL;
	

	switch (pArg->type)
	{
	xcase MULTI_INT:	
		pRetVal = MultiValCreate();
		MultiValSetInt(pRetVal, cmdParseGetIntDefaultValueForArg(pArg));

	xcase MULTI_FLOAT:
		pRetVal = MultiValCreate();
		MultiValSetFloat(pRetVal, 0);
				
	xcase MULTI_STRING:
		pRetVal = MultiValCreate();
		MultiValSetString(pRetVal, cmdParseGetStringDefaultValueForArg(pArg));
	
	xcase MULTI_NP_POINTER:
		pRetVal = MultiValCreate();
		MultiValReferencePointer(pRetVal, StructCreateTemp((ParseTable*)(pArg->ptr), pTempStructContext));

	}

	return pRetVal;
}


cJSON *CreateResponseObject(int iCmdID, char *pCmdIDString, JsonRPCVersion eVersion)
{
	cJSON *pResponse = cJSON_CreateObject();
	switch (eVersion)
	{
	xcase JSONRPC_10:
		//do nothing
	xcase JSONRPC_11:
		cJSON_AddStringToObject(pResponse, "version", "1.1");
	xcase JSONRPC_20:
		cJSON_AddStringToObject(pResponse, "jsonrpc", "2.0");
	}

	if (pCmdIDString)
	{
		cJSON_AddStringToObject(pResponse, "id", pCmdIDString);
	}
	else
	{
		cJSON_AddNumberToObject(pResponse, "id", iCmdID);
	}

	return pResponse;
}

void GenerateFailureResponse(
	char **ppOutString,
	int iCmdID,
	char *pCmdIDString,
	JsonRPCVersion eVersion,
	JsonRPCErrorCode eErrCode,
	FORMAT_STR const char* format, ...)
{
	cJSON *pResponse = CreateResponseObject(iCmdID, pCmdIDString, eVersion);
	cJSON *pErrorObject = cJSON_CreateObject();
	char *pFullError = NULL;
	char *pTempString = NULL;


	estrGetVarArgs(&pFullError, format);

	cJSON_AddNumberToObject(pErrorObject, "code", eErrCode); // Internal error
	cJSON_AddStringToObject(pErrorObject, "message", pFullError);
	estrDestroy(&pFullError);
	cJSON_AddItemToObject(pResponse, "error", pErrorObject);

	pTempString = cJSON_Print(pResponse);
	estrCopy2(ppOutString, pTempString);
	free(pTempString);

	cJSON_Delete(pResponse);
}

void GenerateSuccessResponse(char **ppOutString, int iCmdID, char *pCmdIDString, JsonRPCVersion eVersion, const char *pResultString, MultiValType eCmdType)
{
	cJSON *pResponse = CreateResponseObject(iCmdID, pCmdIDString, eVersion);
	cJSON_AddNullToObject(pResponse, "error");

	if (eCmdType == MULTI_NP_POINTER)
	{
		//slightly kludgy here... we already have the child struct as a JSON string, but we don't want to decompose it
		//into a cJSON, so we just do a paste
		char *pTempString = NULL;

		cJSON_AddStringToObject(pResponse, "result", "__FOO__");

		pTempString = cJSON_Print(pResponse);

		estrCopy2(ppOutString, pTempString);

		estrReplaceOccurrences(ppOutString, "\"__FOO__\"", pResultString);

		free(pTempString);
	}
	else
	{
		char *pStr;

		switch (eCmdType)
		{
		xcase MULTI_INT:
			cJSON_AddNumberToObject(pResponse, "result", (float)atoi(pResultString));

		xcase MULTI_STRING:
			cJSON_AddStringToObject(pResponse, "result", pResultString);

		xcase MULTI_FLOAT:
			cJSON_AddNumberToObject(pResponse, "result", atof(pResultString));
		}

		pStr = cJSON_Print(pResponse);
		estrCopy2(ppOutString, pStr);
		free(pStr);
	}
}

#define CLEANUP { if (pJson) cJSON_Delete(pJson); eaDestroyEx(&ppMultiValArgs, MultiValDestroy); ClearTempStructs(&tempStructContext); estrDestroy(&pIntermediateResultString); }
#define FAIL(code, fmt, ...) {if (bReadID) GenerateFailureResponse(pContext->output_msg, iCmdID, pCmdIDString, eVersion, code, fmt, __VA_ARGS__); CLEANUP; return bReadID;}


bool CmdParseJsonRPC(char *pJsonString, CmdContext *pContext)
{
	cJSON *pJson = cJSON_Parse(pJsonString);
	char *pJsonRpcVersion;
	char *pCmdName;
	int iCmdID = 0;
	char *pCmdIDString = NULL;

	JsonRPCVersion eVersion = JSONRPC_10;

	bool bReadID = false;
	cJSON *pParams;
	Cmd *pCmd;
	MultiVal **ppMultiValArgs = NULL;
	CmdContext cmdContext = {0};
	int i;
	TempStructContext tempStructContext = {0};
	char *pIntermediateResultString = NULL;

	char *pRestrictedCategoryNames = NULL;

	assert(pContext->output_msg);

	if (!pJson)
	{
		FAIL(JSONRPCE_PARSE_ERROR, "Parse error");
	}

	if (pJson->type != cJSON_Object)
	{
		FAIL(JSONRPCE_INVALID_REQUEST, "Invalid Request");
	}

	if (GetIntChild(pJson, "id", &iCmdID))
	{
		//happy
	}
	else if (pCmdIDString = GetStringChild(pJson, "id"))
	{
		//happy
	}
	else
	{
		FAIL(JSONRPCE_INVALID_REQUEST, "Invalid Request (no ID)");
	}

	bReadID = true;

	pJsonRpcVersion = GetStringChild(pJson, "jsonrpc");
	if (!pJsonRpcVersion)
	{
		pJsonRpcVersion = GetStringChild(pJson, "version");
	}

	if (pJsonRpcVersion)
	{
		if (stricmp(pJsonRpcVersion, "1.0") == 0)
		{
			//already correct JSONRPC_10;
		}
		else
		if (stricmp(pJsonRpcVersion, "1.1") == 0)
		{
			eVersion = JSONRPC_11;
		}
		else if (stricmp(pJsonRpcVersion, "2.0") == 0)
		{
			eVersion = JSONRPC_20;
		}
		else
		{
			FAIL(JSONRPCE_INVALID_REQUEST, "Invalid Request (unknown version %s)", pJsonRpcVersion);
		}
	}

	// Find the method...

	pRestrictedCategoryNames = GetStringChild(pJson, "CategoryRestrictions");

	pCmdName = GetStringChild(pJson, "method");
	if (!pCmdName)
	{
		FAIL(JSONRPCE_INVALID_REQUEST, "Invalid Request (no method)");
	}

	pCmd = cmdListFind(&gGlobalCmdList, pCmdName);
	if (pCmd && pCmd->access_level > pContext->access_level)
	{
		pCmd = NULL; // Not allowed or doesn't exist
	}

	if (pCmd && pRestrictedCategoryNames)
	{
		char **ppAllCategories = NULL;
		char temp[128];
		bool bMatchedACategory = false;

		if (pCmd->categories)
		{
			DivideString(pRestrictedCategoryNames, ", ", &ppAllCategories, DIVIDESTRING_STANDARD);

			for (i = 0; i < eaSize(&ppAllCategories); i++)
			{
				sprintf(temp, " %s ", ppAllCategories[i]);

				if (strstri(pCmd->categories, temp))
				{
					bMatchedACategory = true;
					break;
				}
			}
		}

		eaDestroyEx(&ppAllCategories, NULL);

		if (!bMatchedACategory)
		{
			pCmd = NULL; // Not allowed
		}
	}


	// Make sure JSON is allowed...

	if (pContext->slowReturnInfo.eFlags & CMDSRV_ALWAYS_ALLOW_JSONRPC)
	{
		//happy
	}
	else if (pCmd && !(pCmd->flags & CMDF_ALLOW_JSONRPC))
	{
		FAIL(JSONRPCE_INTERNAL_ERROR, "JSON-RPC not allowed");
	}


	// Set up context...
	pContext->eHowCalled = CMD_CONTEXT_HOWCALLED_JSONRPC;
	pContext->slowReturnInfo.eHowCalled = CMD_CONTEXT_HOWCALLED_JSONRPC;

	pContext->slowReturnInfo.jsonInfo.iID = iCmdID;
	if (pCmdIDString)
	{
		strcpy(pContext->slowReturnInfo.jsonInfo.IDString, pCmdIDString);
	}
	else
	{
		pContext->slowReturnInfo.jsonInfo.IDString[0] = 0;
	}
	pContext->slowReturnInfo.jsonInfo.eJsonVersion = eVersion;


	// Make sure we have a command to call...

	if (!pCmd)
	{
		if (gMissingCallback)
		{
			// Consider it handled
			pContext->slowReturnInfo.bDoingSlowReturn = true;
			gMissingCallback(pContext, pJsonString);
			CLEANUP;
			return true;
		}
		else
		{
			FAIL(JSONRPCE_METHOD_NOT_FOUND, "Method not found");
		}
	}


	// Handle arguments...

	eaSetSize(&ppMultiValArgs, pCmd->iNumLogicalArgs);

	pParams = cJSON_GetObjectItem(pJson, "params");

	if (pParams)
	{
		if (pParams->type == cJSON_Array)
		{
			int iNumParams = cJSON_GetArraySize(pParams);

			if (iNumParams > pCmd->iNumLogicalArgs)
			{
				FAIL(JSONRPCE_INVALID_PARAMS, "Invalid params (got %d arguments, only expected %d)", iNumParams, pCmd->iNumLogicalArgs);
			}

			for (i = 0; i < iNumParams; i++)
			{
				DataDesc *pArg = &pCmd->data[i];
				cJSON *pParam = cJSON_GetArrayItem(pParams, i); 
				char *pError;
				MultiVal *pMultiValArg;

				pError = CheckArg(pArg, pParam);
				if (pError)
				{
					FAIL(JSONRPCE_INVALID_PARAMS, "Invalid params (%s)", pError);
				}

				pMultiValArg = CreateMultiValArg(pArg, pParam, &tempStructContext);
				if (!pMultiValArg)
				{
					FAIL(JSONRPCE_INVALID_PARAMS, "Invalid params (couldn't create multival arg for %s)", pArg->pArgName);
				}

				ppMultiValArgs[i] = pMultiValArg;
			}
		}
		else if (pParams->type == cJSON_Object)
		{
			cJSON *pParam = pParams->child;

			while (pParam)
			{
				char *pArgName = pParam->string;
				DataDesc *pArg = NULL;
				int iFoundArgNum;
				MultiVal *pMultiValArg;
				char *pError;

				if (!pArgName)
				{
					FAIL(JSONRPCE_INVALID_PARAMS, "Invalid params (a param doesn't have a name)");
				}

				//if the arg name is a literal int, then it's actually a position
				if (StringToInt_Paranoid(pArgName, &iFoundArgNum))
				{
					if (iFoundArgNum < 0 || iFoundArgNum >= pCmd->iNumLogicalArgs)
					{
						FAIL(JSONRPCE_INVALID_PARAMS, "Invalid params (parameter named %d is out of range)", iFoundArgNum);
					}

					pArg = &pCmd->data[iFoundArgNum];
				}
				else
				{
					for (i = 0; i < pCmd->iNumLogicalArgs; i++)
					{
						if (stricmp_safe(pArgName, pCmd->data[i].pArgName) == 0)
						{
							iFoundArgNum = i;
							pArg = &pCmd->data[i];
							break;
						}
					}
				}

				if (!pArg)
				{
					FAIL(JSONRPCE_INVALID_PARAMS, "Invalid params (parameter %s not found)", pArgName);
				}

				if (ppMultiValArgs[iFoundArgNum])
				{
					FAIL(JSONRPCE_INVALID_PARAMS, "Invalid params (duplicate params named %s)", pArgName);
				}

				pError = CheckArg(pArg, pParam);
				if (pError)
				{
					FAIL(JSONRPCE_INVALID_PARAMS, "Invalid params (%s)", pError);
				}

				pMultiValArg = CreateMultiValArg(pArg, pParam, &tempStructContext);
				if (!pMultiValArg)
				{
					FAIL(JSONRPCE_INVALID_PARAMS, "Invalid params (couldn't create multival arg for %s)", pArg->pArgName);
				}

				ppMultiValArgs[iFoundArgNum] = pMultiValArg;

				pParam = pParam->next;
			}
		}
		else
		{
			FAIL(JSONRPCE_INVALID_PARAMS, "Invalid params (params is %s, must be either array or object)", 
				GetJsonTypeName(pParams->type));
		}
	}

	for (i = 0; i < pCmd->iNumLogicalArgs; i++)
	{
		if (!ppMultiValArgs[i])
		{
			DataDesc *pArg = &pCmd->data[i];
			MultiVal *pDefaultArg = MakeDefaultMultiValArg(pCmd, pArg, &tempStructContext);
			if (!pDefaultArg)
			{
				FAIL(JSONRPCE_INVALID_PARAMS, "Invalid params (unable to make default multival arg for %s)", pArg->pArgName);
			}

			ppMultiValArgs[i] = pDefaultArg;
		}
	}


	// Execute...

	if (!cmdExecuteWithMultiVals(&gGlobalCmdList, pCmdName, pContext, &ppMultiValArgs))
	{
		FAIL(JSONRPCE_INTERNAL_ERROR, "Internal error (cmd execution failed)");
	}
	else
	{
		if (pContext->slowReturnInfo.bDoingSlowReturn)
		{
			//do nothing
		}
		else
		{
			switch (pCmd->return_type.type)
			{
			case MULTI_INT:
			case MULTI_STRING:
			case MULTI_FLOAT:
			case MULTI_NP_POINTER:
				break;

			default:
				FAIL(JSONRPCE_INTERNAL_ERROR, "Internal error (unsupported return type %d)", pCmd->return_type.type);
			}

			estrCopy(&pIntermediateResultString, pContext->output_msg);
			estrClear(pContext->output_msg);
			GenerateSuccessResponse(pContext->output_msg, iCmdID, pCmdIDString, eVersion, pIntermediateResultString, pCmd->return_type.type);
			estrDestroy(&pIntermediateResultString);
		}
	}

	CLEANUP;
	return true;
}

void DoSlowCmdReturn_JsonRPC(const char *pRetString, ParseTable *pTPI, void *pStruct, S64 iRetValInt, CmdSlowReturnForServerMonitorInfo *pSlowInfo)
{
	char *pFullRetString = NULL;

	if (pStruct)
	{
		char *pTempString = NULL;
		estrStackCreate(&pTempString);
		ParserWriteJSON(&pTempString, pTPI, pStruct, 0, 0, 0);

		GenerateSuccessResponse(&pFullRetString, pSlowInfo->jsonInfo.iID,
			pSlowInfo->jsonInfo.IDString[0] ? pSlowInfo->jsonInfo.IDString : NULL,
			pSlowInfo->jsonInfo.eJsonVersion, 
			pTempString,
			MULTI_NP_POINTER);

		estrDestroy(&pTempString);

	}
	else if (pRetString)
	{
		// In the context of returning a string, the error code must be JSONRPCE_SUCCESS for success
		if (iRetValInt == JSONRPCE_SUCCESS)
		{
			GenerateSuccessResponse(&pFullRetString, pSlowInfo->jsonInfo.iID,
				pSlowInfo->jsonInfo.IDString[0] ? pSlowInfo->jsonInfo.IDString : NULL,
				pSlowInfo->jsonInfo.eJsonVersion, 
				pRetString,
				MULTI_STRING);
		}
		else if (iRetValInt == JSONRPCE_SUCCESS_RAW)
		{
			estrCopy2(&pFullRetString, pRetString);
		}
		else
		{
			GenerateFailureResponse(&pFullRetString, pSlowInfo->jsonInfo.iID,
				pSlowInfo->jsonInfo.IDString[0] ? pSlowInfo->jsonInfo.IDString : NULL,
				pSlowInfo->jsonInfo.eJsonVersion,
				iRetValInt,
				"%s", pRetString);
		}
	}
	else
	{
		char temp[32];
		sprintf(temp, "%I64d", iRetValInt);
		GenerateSuccessResponse(&pFullRetString, pSlowInfo->jsonInfo.iID,
			pSlowInfo->jsonInfo.IDString[0] ? pSlowInfo->jsonInfo.IDString : NULL,
			pSlowInfo->jsonInfo.eJsonVersion, 
			temp,
			MULTI_INT);		
	}

	if (pSlowInfo->pSlowReturnCB)
	{
		pSlowInfo->pSlowReturnCB(pSlowInfo->iMCPID, pSlowInfo->iCommandRequestID, pSlowInfo->iClientID, pSlowInfo->eFlags, pFullRetString, pSlowInfo->pUserData);
	}
	else
	{
		printf("NO SLOW RETURN CB: %s", pFullRetString);
	}

	estrDestroy(&pFullRetString);
	
}



static bool GetJsonVersionAndID(char *pJsonString, char **ppOutIDString /*estring*/, int *piOutID, JsonRPCVersion *pOutVersion)
{
	cJSON *pJson = cJSON_Parse(pJsonString);
	char *pJsonRpcVersion;
	int iCmdID = 0;
	char *pCmdIDString = NULL;

	JsonRPCVersion eVersion = JSONRPC_10;

	if (!pJson)
	{
		return false;
	}

	if (pJson->type != cJSON_Object)
	{
		cJSON_Delete(pJson);
		return false;
	}

	if (GetIntChild(pJson, "id", &iCmdID))
	{
		//happy
	}
	else if (pCmdIDString = GetStringChild(pJson, "id"))
	{
		//happy
	}
	else
	{
		cJSON_Delete(pJson);
		return false;
	}


	pJsonRpcVersion = GetStringChild(pJson, "jsonrpc");
	if (!pJsonRpcVersion)
	{
		pJsonRpcVersion = GetStringChild(pJson, "version");
	}

	if (pJsonRpcVersion)
	{
		if (stricmp(pJsonRpcVersion, "1.0") == 0)
		{
			//already correct JSONRPC_10;
		}
		else
		if (stricmp(pJsonRpcVersion, "1.1") == 0)
		{
			eVersion = JSONRPC_11;
		}
		else if (stricmp(pJsonRpcVersion, "2.0") == 0)
		{
			eVersion = JSONRPC_20;
		}
		else
		{
			cJSON_Delete(pJson);
			return false;		
		}
	}


	*piOutID = iCmdID;
	if (pCmdIDString)
	{
		estrCopy2(ppOutIDString, pCmdIDString);
	}
	*pOutVersion = eVersion;


	cJSON_Delete(pJson);
	return true;
}



char *GetJsonRPCErrorString(char *pJsonString, FORMAT_STR const char *pFmt, ...)
{
	int iID;
	char *pIDString = NULL;
	JsonRPCVersion eVersion;
	static char *spRetVal = NULL;
	static char *spFullErrorString = NULL;

	estrClear(&spRetVal);
	estrClear(&spFullErrorString);
	estrGetVarArgs(&spFullErrorString, pFmt);

	if (!GetJsonVersionAndID(pJsonString, &pIDString, &iID, &eVersion))
	{
		estrPrintf(&spRetVal, "Can't parse JsonRPC to produce properly formatted error: %s", spFullErrorString);
		return spRetVal;
	}

	GenerateFailureResponse(&spRetVal, iID, pIDString, eVersion, JSONRPCE_INTERNAL_ERROR, "%s", spFullErrorString);
	estrDestroy(&pIDString);
	return spRetVal;
}
		
bool AddArgumentToJsonCommand(char **ppOutCmdString, char *pInCmdString, char *pArgName, char *pArgValue)
{
	cJSON *pJson = cJSON_Parse(pInCmdString);
	char *pOutStr;

	if (!pJson)
	{
		return false;
	}

	cJSON_DeleteItemFromObject(pJson, pArgName);

	cJSON_AddStringToObject(pJson, pArgName, pArgValue);

	pOutStr = cJSON_Print(pJson);

	estrCopy2(ppOutCmdString, pOutStr);

	cJSON_Delete(pJson);

	return true;
}

void RegisterJsonRPCMissingCallback(CmdParseJsonMissingCallback callback)
{
	assertmsg(!gMissingCallback, "Only one JSON missing callback supported");
	gMissingCallback = callback;
}