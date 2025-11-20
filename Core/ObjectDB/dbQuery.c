/***************************************************************************
*     Copyright (c) 2005-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "BlockEarray.h"
#include "pub/dbQuery.h"
#include "HttpLib.h"
#include "objContainer.h"
#include "Expression.h"

#include "objTransactions.h"
#include "objIndex.h"

#include "dbQuery_h_ast.h"
#include "autogen/objcontainer_h_ast.h"

#include "ExpressionPrivate.h"


//AUTO_STRUCT;
//typedef struct ObjectWithLink
//{
//	char *pLink; AST(ESTRING, FORMATSTRING(HTML=1, HTML_LINKOVERRIDE=1, HTML_LINKOVERRIDE_SUFFIX=".Struct"))
//} ObjectWithLink;

typedef struct ApplyCBData
{
	GlobalType type;
	ContainerID id;	
} ApplyCBData;

static void ApplyUpdate_CB(TransactionReturnVal *returnVal, ApplyCBData *cbData)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		return;
	}
	else
	{
		//nocheckin
		assertmsg(false, "Transaction failed.");
	}
}

bool dbNonQueryXpathProcessingCB(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	ResultSet rs = {0};
	const char *queryString = urlFindValue(pArgList, DB_QUERY_URL_PARAMETER);

	StructInit(parse_ResultSet, &rs);
	rs.bSuccess = false;

	estrPrintf(&rs.pMessage, DBQUERY_ERROR "No more DbQuery. Parameter:\"%s\"", queryString);

	ProcessStructIntoStructInfoForHttp(pLocalXPath, pArgList, &rs, parse_ResultSet , iAccessLevel, 0, pStructInfo, eFlags);

	StructDeInit(parse_ResultSet, &rs);

	return false;
}

//this function will install the callback for the "ObjectDB[0].query" xpath handling.
void dbRegisterQueryXpathDomain(void)
{
	RegisterCustomXPathDomain(DB_QUERY_DOMAIN_NAME, dbNonQueryXpathProcessingCB, NULL);
}

//this callback forwards the query argument to the query processor and returns the result by reference.
bool dbQueryXpathProcessingCB(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	ResultSet rs = {0};
	ParseTable *tpi = NULL;
	bool result = false;
	const char *queryString = urlFindValue(pArgList, DB_QUERY_URL_PARAMETER);
	HybridObjHandle *returnObjHandle = NULL;

	StructInit(parse_ResultSet, &rs);

	if (queryString)
	{
		//parse and execute the query
		dbRunQuery(queryString, &rs, &tpi, &returnObjHandle);
		result = rs.bSuccess;
	}
	else
	{
		estrPrintf( &rs.pMessage, DBQUERY_ERROR "Query parameter \"%s\" was empty.", DB_QUERY_URL_PARAMETER);
	}
	
	//Cram the result into the StructInfo... 
	ProcessStructIntoStructInfoForHttp(pLocalXPath, pArgList, &rs, (tpi?tpi:parse_ResultSet) , iAccessLevel, 0, pStructInfo, eFlags);

	
	//clean up result set
	EARRAY_FOREACH_BEGIN(rs.ppRows,i);
	{
		HybridObjHandle_DestroyObject(returnObjHandle, rs.ppRows[i]);
	}
	EARRAY_FOREACH_END;
	eaDestroy(&rs.ppRows);

	StructDeInitVoid((tpi?tpi:parse_ResultSet), &rs);
	if (returnObjHandle) HybridObjHandle_Destroy(returnObjHandle);
	if (tpi) destroyResultSetTPI(tpi);

	return result;
}

void dbRunQuery(const char *queryString, ResultSet *pRSinout, ParseTable **pTPIout, HybridObjHandle **pReturnHandleOut)
{
	AgglomeratedQuery *pQuery = 0;
	ContainerIterator iter = {0};
	ContainerSchema *schema;
	ContainerStore *store;
	bool isIndexed = false;

	ExprContext *pContext = exprContextCreate();
	Expression *pSelectExpr = NULL;
	Expression *pWhereExpr = NULL;

	HybridObjHandle *hybridObjHandle = NULL;
	HybridObjHandle *returnObjHandle = NULL;
	ParseTable *hybridTpi = NULL;
	ParseTable *returnTpi = NULL;
	NameObjPair objAndMeta[3];

	char **ppProcdFields = 0;

	char *pTransString = 0;
	char *pTransVars = 0;
	//TransactionRequest *pTrans = NULL;

	char *pTypeName = 0;
	char *buf = 0;
	GlobalType gtype;
	int i = 0;
	int j = 0;

	int metacount = 0;

	pRSinout->row_count = 0;
	pRSinout->bSuccess = true;
	exprContextSetSilentErrors(pContext, true);

	//Make the context use default objectpath root object.
	exprContextSetUserPtrIsDefault(pContext, true);
	estrStackCreate(&pTypeName);
	estrStackCreate(&buf);

	estrStackCreate(&pTransVars);

	eaCreate(&ppProcdFields);
	do {
		//Parse the query.
		if (!dbMakeQueryFromString(queryString, &pQuery))
		{
			pRSinout->bSuccess = false;
			estrPrintf(&pRSinout->pMessage, DBQUERY_ERROR "%s", pQuery->parser_message);
			break;
		}
		
		//XXXXXX: We may want a more complex method of getting the table data if FROM is an expression.
		if (pQuery->expr_from && pQuery->expr_from[0]) 
		{
			estrConcatf(&pTypeName, "%s", pQuery->expr_from);
			estrTrimLeadingAndTrailingWhitespace(&pTypeName);
		}
		else
		{
			estrAppend2(&pTypeName, DB_QUERY_DEFAULT_GLOBALTYPE);	
		}
		gtype = NameToGlobalType(pTypeName);
		if (!gtype)
		{
			estrPrintf(&pRSinout->pMessage, DBQUERY_ERROR "Can't resolve global type \"%s\".", pTypeName);
			break;
		}

		store = objFindContainerStoreFromType(gtype);

		if (store && store->lazyLoad)
		{
			estrPrintf(&pRSinout->pMessage, DBQUERY_ERROR "Queries are not supported on lazy loaded stores (global type %s).", pTypeName);
			break;
		}

		schema = objFindContainerSchema(gtype);

		//Create the custom TPI for this result set
		if (!schema)
		{
			estrPrintf(&pRSinout->pMessage, DBQUERY_ERROR "Could not find object container schema for \"%s\".", pTypeName);
			break;
		}

		
		//Create the earray of results.
		if (!pRSinout->ppRows) eaCreate(&pRSinout->ppRows);

		exprContextAddStaticDefineIntAsVars(pContext, ContainerStateEnum, "");
		exprContextAddStaticDefineIntAsVars(pContext, GlobalTypeEnum, "");
		
		//Setup the querytpi
		//XXXXXX: CreatedName should only be pTypeName when there is no join table.
		hybridObjHandle = HybridObjHandle_Create();
		HybridObjHandle_AddObject(hybridObjHandle, schema->classParse, pTypeName);
		HybridObjHandle_AddObject(hybridObjHandle, parse_ContainerMeta, "meta");
		FORALL_PARSETABLE(parse_ContainerMeta, i)
		{
			StructTokenType type = TOK_GET_TYPE(parse_ContainerMeta[i].type);
			if ( type != TOK_START && type != TOK_END && type != TOK_IGNORE )
			{
				estrPrintf(&buf, ".%s", parse_ContainerMeta[i].name);
				HybridObjHandle_AddField(hybridObjHandle, "meta", buf);
			}
		}
		FORALL_PARSETABLE(schema->classParse, i)
		{
			StructTokenType type = TOK_GET_TYPE(schema->classParse[i].type);
			if ( type != TOK_START && type != TOK_END && type != TOK_IGNORE )
			{
				estrPrintf(&buf, ".%s", schema->classParse[i].name);
				HybridObjHandle_AddField(hybridObjHandle, pTypeName, buf);
			}
		}
		hybridTpi = HybridObjHandle_GetTPI(hybridObjHandle);
		
		//Set up the result tpi
		returnObjHandle = HybridObjHandle_CreateNamed(pTypeName);
		HybridObjHandle_AddObject(returnObjHandle, schema->classParse, pTypeName);
		HybridObjHandle_AddObject(returnObjHandle, parse_ContainerMeta, "meta");
		HybridObjHandle_AddObject(returnObjHandle, parse_Result, "resultrow");
		HybridObjHandle_AddField(returnObjHandle, "resultrow", ".row_index");

		*pReturnHandleOut = returnObjHandle;

		while (eaSize(&pQuery->field_list) > 0)
		{
			char *field = eaPop(&pQuery->field_list);
			if (field) eaPush(&ppProcdFields, field);
		}

		while (eaSize(&ppProcdFields) > 0)
		{
			char *result = NULL;
			char *field = eaPop(&ppProcdFields); //pQuery->field_list[i];
			char *procfield = field;
			if (strEndsWith(field, ".*")) field[strlen(field)-2] = '\0';

			if (field[0] != '\0')
			{
				char fieldbuf[MAX_OBJECT_PATH];
				if (strlen(field) > 1023) {
					estrPrintf(&pRSinout->pMessage, "Field name too large: %s", field);
					pRSinout->bSuccess = false;
				}
					
				//XXXXXX: Need to preprocess fields
				//here <--

				if (pQuery->type == QUERYTYPE_UPDATE)
				{
					char *fc = field;
					char *fd = fieldbuf;
					//copy 
					while (*fc == ' ' || *fc == '\n' || *fc == '\t') fc++;
					//copy the first token before whitespace;
					while (*fc != ' ' && *fc != '\n' && *fc != '\t' && *fc != '\0') *fd++ = *fc++;
					*fd = '\0';
					if (fieldbuf[0]) procfield = fieldbuf;
				}
				
				if (strStartsWith(procfield, "."))
				{
					char *message = 0;
					estrStackCreate(&message);
					if (ParserResolvePath(procfield, parse_ContainerMeta, NULL, NULL, NULL, NULL, NULL, &message, NULL, 0))
					{
						if (pQuery->type == QUERYTYPE_SELECT)
						{
							HybridObjHandle_AddField(returnObjHandle, "meta", procfield);
							eaPush(&pQuery->field_list, strdup(procfield));
							metacount++;
						}
					}
					else if (ParserResolvePath(procfield, schema->classParse, NULL, NULL, NULL, NULL, NULL, &message, NULL, 0))
					{
						HybridObjHandle_AddField(returnObjHandle, pTypeName, procfield);
						if (pQuery->type == QUERYTYPE_UPDATE)
						{
							char *fieldname = strchr(procfield, '.');
							if (fieldname)
							{
								fieldname++;
								estrConcatf(&pTransVars, " %s", fieldname);
							}
							eaPush(&pQuery->field_list, strdup(field));
							
						}
						else
						{
							eaPush(&pQuery->field_list, strdup(procfield));
						}
					}
					else
					{
						estrPrintf(&pRSinout->pMessage, "%s", message);
						pRSinout->bSuccess = false;
					}
					estrDestroy(&message);
					if (!pRSinout->bSuccess) break;
				}
				else
				{
					char *specifiedPath;

					//XXXXXX: this is broken right now, Function result fields should go here.
					if (specifiedPath = strstr(field, "."))					
					{
						char *specifiedField = 0;
						estrStackCreate(&specifiedField);
						estrInsert(&specifiedField, 0, field, specifiedPath - field);
						if (ParserResolvePath(specifiedPath, hybridTpi, NULL, NULL, NULL, NULL, NULL, &pRSinout->pMessage, NULL, 0))
						{
							HybridObjHandle_AddField(returnObjHandle, specifiedField, specifiedPath);
						}
						estrDestroy(&specifiedField);
					}

					//XXXXXX: Add expression fields like COUNT() and MAX()
					//This could be done by just pushing a string on to a stack
					// then evaluating the string for each object
					//
					//The hybrid object could take an aliased field struct. (probably have to make field safe)
					//HybridObjHandle_AddObject(returnObjHandle, parse_ResultField, field);
					//HybridObjHandle_AddFieldWithAlias(returnObjHandle, field, ".field", field);
				}
				
			}
			else
			{
				//XXXXXX: This is broken, we have to add the remaining fields if we get a .*
				FORALL_PARSETABLE(parse_ContainerMeta, j)
				{
					StructTokenType type = TOK_GET_TYPE(parse_ContainerMeta[j].type);
					if ( type != TOK_START && type != TOK_END && type != TOK_IGNORE )
					{
						estrPrintf(&buf, ".%s", parse_ContainerMeta[j].name);
						HybridObjHandle_AddField(returnObjHandle, "meta", buf);
					}
				}
				FORALL_PARSETABLE(schema->classParse, j)
				{
					StructTokenType type = TOK_GET_TYPE(schema->classParse[j].type);
					if ( type != TOK_START && type != TOK_END && type != TOK_IGNORE )
					{
						estrPrintf(&buf, ".%s", schema->classParse[j].name);
						HybridObjHandle_AddField(returnObjHandle, pTypeName, buf);
					}
				}
			}
			free(field);
		}

		//convert the field names
		if (pQuery->type == QUERYTYPE_UPDATE)
		{
			estrStackCreate(&pTransString);
			for (i = 0; i < eaSize(&pQuery->field_list); i++)
			{
				estrConcatf(&pTransString, "set %s\n", pQuery->field_list[i]);
			}
			if (i)
			{
				//pTrans = objCreateTransactionRequest();
			}
		}

		estrTrimLeadingAndTrailingWhitespace(&pTransString);

		returnTpi = HybridObjHandle_GetTPI(returnObjHandle);

		if (pRSinout->bSuccess)
		{
			Container *container = NULL;
			GetMethod getmethod = IterateObjects;
			ParseTable *tpi= NULL;

			ObjectIndex *idx = NULL;
			void **eaindexedset = NULL;

			objAndMeta[0].pObjName = pTypeName;
			objAndMeta[1].pObjName = "meta";
			objAndMeta[2].pObjName = "resultrow";

			*pTPIout = makeResultSetTPI(returnTpi, &isIndexed);
			//Add the earray index if the substructs are keyed.
			if (pTPIout && isIndexed) eaIndexedEnableVoid(&pRSinout->ppRows, returnTpi);
			

			//Iterate over the source data and select out the records to return.
			objInitContainerIteratorFromType(gtype, &iter);
			i = 0;
			while (pRSinout->bSuccess)
			{
				void *pObject;
				MultiVal answer = {0};
				int iAnswer = 0;

				switch (getmethod)
				{
				case IterateObjects:
					container = objGetNextContainerFromIterator(&iter);
					break;
				case DirectGet:
					//we already got it.
					break;
				case IndexGet: 
					{
						container = NULL;
						if (eaindexedset && idx && eaSize(&eaindexedset))
						{
							ContainerID id = 0;
							void *condata = eaPop(&eaindexedset);

							if (objGetKeyInt(idx->columns[0]->colPath->key->rootTpi, condata, &id))
							{
								container = objGetContainer(gtype, id);
							}
						}
					}
					break;
				default:
					container = NULL;
				}

				if (container == NULL)
					break;

				objAndMeta[0].pObj = container->containerData;
				objAndMeta[1].pObj = &container->meta;
				objAndMeta[2].pObj = StructCreate(parse_Result);
				((Result*)objAndMeta[2].pObj)->row_index = i;
				//this is a quick hack to avoid the hybrid object construction if need be.
				if (metacount)
				{
					pObject = HybridObjHandle_ConstructObject(hybridObjHandle, objAndMeta, 2);
					tpi = hybridTpi;
				}
				else
				{
					pObject = container->containerData;
					if (tpi && tpi != container->containerSchema->classParse)
					{
						pRSinout->bSuccess = 0;
						estrPrintf(&pRSinout->pMessage, "ObjectDB container iterator contained multiple object types.");
						break;
					}

					tpi = container->containerSchema->classParse;
				}

				if (pQuery->expr_where)
				{
					//Set the default objectpath root
					exprContextSetUserPtr(pContext, pObject, tpi);

					if (!pWhereExpr)
					{
						int whereSize;
						pWhereExpr = exprCreate();
						exprGenerateFromString(pWhereExpr, pContext, pQuery->expr_where, NULL);

						//Direct/Index Get
						whereSize = beaSize(&pWhereExpr->postfixEArray);
						ARRAY_FOREACH_BEGIN(pWhereExpr->postfixEArray,op);
						{
							if (op+2 >= whereSize) break;
							if (pWhereExpr->postfixEArray[op].type == MULTIOP_OBJECT_PATH &&
								pWhereExpr->postfixEArray[op+2].type == MULTIOP_EQUALITY)
							{
								if (objPathIsKey(tpi,pWhereExpr->postfixEArray[op].str))
								{	//handle direct gets
									if (pWhereExpr->postfixEArray[op+1].type == MULTI_INT)
									{
										container = objGetContainer(gtype,pWhereExpr->postfixEArray[op+1].int32);
										getmethod = DirectGet;
										whereSize = -1;
										break;
									}
								}
								else
								{
									//Currently this just grabs the first index in the expression it finds. 
									//TODO: This could be way smarter and use the smallest index.
									if (idx = objFindContainerStoreIndexWithPath(store, pWhereExpr->postfixEArray[op].str))
									{
										if (objIndexGetKeyType(idx) == pWhereExpr->postfixEArray[op+1].type)
										{
											ObjectIndexKey key = {0};
											switch (pWhereExpr->postfixEArray[op+1].type)
											{
											case MULTI_INT:		objIndexInitKey_Int(idx, &key, pWhereExpr->postfixEArray[op+1].intval); break;
											case MULTI_STRING:	objIndexInitKey_String(idx, &key, pWhereExpr->postfixEArray[op+1].str); break;
											case MULTI_FLOAT:	objIndexInitKey_F32(idx, &key, pWhereExpr->postfixEArray[op+1].floatval); break;
											}
											whereSize = -1;
											if (key.val.type)
											{
												objIndexObtainReadLock(idx); //Must be released later
												objIndexCopyEArrayOfKey(idx, &eaindexedset, &key, true);
												getmethod = IndexGet;
											}
											switch (pWhereExpr->postfixEArray[op+1].type)
											{
											case MULTI_INT:		objIndexDeinitKey_Int(idx, &key); break;
											case MULTI_STRING:	objIndexDeinitKey_String(idx, &key); break;
											case MULTI_FLOAT:	objIndexDeinitKey_F32(idx, &key); break;
											}

											break;
										}
									}
								}
							}
						}
						EARRAY_FOREACH_END;
						if (whereSize == -1)
						{
							if (metacount)
								HybridObjHandle_DestroyObject(hybridObjHandle, pObject);

							StructDestroy(parse_Result, objAndMeta[2].pObj);
							continue;
						}
					}

					exprEvaluate(pWhereExpr, pContext, &answer);

					if (exprContextCheckStaticError(pContext))
						break;

					if (answer.type == MULTI_INT)
						iAnswer = QuickGetInt(&answer);
					else
						iAnswer = 0;
				}
				
				if (metacount)
					HybridObjHandle_DestroyObject(hybridObjHandle, pObject);

				if (iAnswer) {
					if (pQuery->type == QUERYTYPE_SELECT)
					{
						void *outObj = HybridObjHandle_ConstructObject(returnObjHandle, objAndMeta, 2);
						
						eaPush(&pRSinout->ppRows, outObj);
						i++;
					}
					else if (pQuery->type == QUERYTYPE_UPDATE)
					{
						if (pTransString)
						{
							TransactionReturnVal *returnStruct = objCreateManagedReturnVal(ApplyUpdate_CB, NULL);
							//objAddToTransactionRequestf(pTrans, gtype, container->containerID,pTransVars, pTransString);
							objRequestTransactionSimple(returnStruct, gtype, container->containerID, "DbQueryUpdate", pTransString);
							if (returnStruct->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
							{
								objReleaseTransactionReturn(returnStruct);
								break;
							}
							objReleaseTransactionReturn(returnStruct);
							i++;
						}
					}
				}
				
				StructDestroy(parse_Result, objAndMeta[2].pObj);
				if (getmethod == DirectGet) break;
			}
			objClearContainerIterator(&iter);
			if (getmethod == IndexGet && eaindexedset)
			{
				eaDestroy(&eaindexedset);
				objIndexReleaseReadLock(idx);
			}
		}

		if (!pRSinout->bSuccess)
			break;

		if (pQuery->type == QUERYTYPE_UPDATE)
		{
			//TransactionReturnVal *returnStruct = objCreateManagedReturnVal(ApplyUpdate_CB, NULL);
			//int result;

			if (i) 
			{
				//printf("Locking and updating %d containers, please wait...", i);
				//result = objRequestTransaction(returnStruct,pTrans);
				//printf("done. \n");
			}
			pRSinout->row_count = i;
		}
		else
		{
			pRSinout->row_count = eaSize(&pRSinout->ppRows);
		}
	} while (false);

	//if (pTrans) objDestroyTransactionRequest(pTrans);

	eaDestroy(&ppProcdFields);
	estrDestroy(&pTransString);
	estrDestroy(&pTransVars);
	
	if (hybridObjHandle) HybridObjHandle_Destroy(hybridObjHandle);
	if (hybridTpi) HybridObjHandle_DestroyTPI(hybridTpi);

	estrDestroy(&buf);
	estrDestroy(&pTypeName);
	exprContextDestroy(pContext);
	if (pWhereExpr) exprDestroy(pWhereExpr);
	if (pSelectExpr) exprDestroy(pSelectExpr);
	dbDestroyQuery(&pQuery);
}

#include "dbQuery_h_ast.c"

static ParseTable * makeResultSetTPI(ParseTable* innerTPI, bool *isIndexedOut)
{
	ParseTable *tpi = (ParseTable*)calloc(sizeof(parse_ResultSet),1);
	int i;
	*isIndexedOut = false;
	memcpy(tpi, parse_ResultSet, sizeof(parse_ResultSet));
	tpi[PARSE_RESULTSET_ROWS_INDEX].subtable = innerTPI;

	FORALL_PARSETABLE(innerTPI, i)
	{
		if (innerTPI[i].type & TOK_KEY) 
		{
			*isIndexedOut = true;
			break;
		}
	}

	return tpi;
}

static void destroyResultSetTPI(ParseTable *rsTPI)
{
	if (rsTPI[PARSE_RESULTSET_ROWS_INDEX].subtable)
		HybridObjHandle_DestroyTPI(rsTPI[PARSE_RESULTSET_ROWS_INDEX].subtable);
	free(rsTPI);
}

