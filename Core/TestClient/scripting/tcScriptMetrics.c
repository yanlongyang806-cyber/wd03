#if 0

#include "tcScriptInternal.h"
#include "tcScriptShared.h"

#include "ClientControllerLib.h"
#include "LocalTransactionManager.h"
#include "ServerLib.h"
#include "TestServerIntegration.h"

GLUA_FUNC( metric_push )
{
	TransactionReturnVal returnVal = {0};
	//enumTransactionOutcome eOutcome;
	int count = 0;

	GLUA_ARG_COUNT_CHECK( metric_push, 2, 2 );
	GLUA_ARG_CHECK( metric_push, 1, GLUA_TYPE_STRING, false, "" );
	GLUA_ARG_CHECK( metric_push, 2, GLUA_TYPE_NUMBER | GLUA_TYPE_INT, false, "" );

// 	RemoteCommand_TestClient_PushMetric(&returnVal, GLOBALTYPE_TESTSERVER, 0, glua_getString(1), glua_getNumber(2));
// 
// 	do 
// 	{
// 		TestClientLib_Tick();
// 	} while ((eOutcome = RemoteCommandCheck_TestClient_PushMetric(&returnVal, &count)) == TRANSACTION_OUTCOME_NONE);
// 
// 	if(eOutcome == TRANSACTION_OUTCOME_FAILURE)
// 	{
// 		glua_pushNil();
// 		break;
// 	}

	glua_pushInteger(count);
}
GLUA_END

GLUA_FUNC( metric_clear )
{
	GLUA_ARG_COUNT_CHECK( metric_clear, 1, 1 );
	GLUA_ARG_CHECK( metric_clear, 1, GLUA_TYPE_STRING, false, "" );

	//RemoteCommand_TestClient_ClearMetric(GLOBALTYPE_TESTSERVER, 0, glua_getString(1));
}
GLUA_END

GLUA_FUNC( metric_get_count )
{
	TransactionReturnVal returnVal = {0};
	//enumTransactionOutcome eOutcome;
	int count = 0;

	GLUA_ARG_COUNT_CHECK( metric_get_count, 1, 1 );
	GLUA_ARG_CHECK( metric_get_count, 1, GLUA_TYPE_STRING, false, "" );

// 	RemoteCommand_TestClient_GetMetricCount(&returnVal, GLOBALTYPE_TESTSERVER, 0, glua_getString(1));
// 
// 	do 
// 	{
// 		TestClientLib_Tick();
// 	} while ((eOutcome = RemoteCommandCheck_TestClient_GetMetricCount(&returnVal, &count)) == TRANSACTION_OUTCOME_NONE);
// 
// 	if(eOutcome == TRANSACTION_OUTCOME_FAILURE)
// 	{
// 		glua_pushNil();
// 		break;
// 	}

	glua_pushInteger(count);
}
GLUA_END

GLUA_FUNC( metric_get_average )
{
	TransactionReturnVal returnVal = {0};
	//enumTransactionOutcome eOutcome;
	F32 avg = 0.0f;

	GLUA_ARG_COUNT_CHECK( metric_get_average, 1, 1 );
	GLUA_ARG_CHECK( metric_get_average, 1, GLUA_TYPE_STRING, false, "" );

// 	RemoteCommand_TestClient_GetMetricAvg(&returnVal, GLOBALTYPE_TESTSERVER, 0, glua_getString(1));
// 
// 	do 
// 	{
// 		TestClientLib_Tick();
// 	} while ((eOutcome = RemoteCommandCheck_TestClient_GetMetricAvg(&returnVal, &avg)) == TRANSACTION_OUTCOME_NONE);
// 
// 	if(eOutcome == TRANSACTION_OUTCOME_FAILURE)
// 	{
// 		glua_pushNil();
// 		break;
// 	}

	glua_pushNumber(avg);
}
GLUA_END

GLUA_FUNC( metric_get_minimum )
{
	TransactionReturnVal returnVal = {0};
	//enumTransactionOutcome eOutcome;
	F32 min = 0.0f;

	GLUA_ARG_COUNT_CHECK( metric_get_minimum, 1, 1 );
	GLUA_ARG_CHECK( metric_get_minimum, 1, GLUA_TYPE_STRING, false, "" );

// 	RemoteCommand_TestClient_GetMetricMin(&returnVal, GLOBALTYPE_TESTSERVER, 0, glua_getString(1));
// 
// 	do 
// 	{
// 		TestClientLib_Tick();
// 	} while ((eOutcome = RemoteCommandCheck_TestClient_GetMetricMin(&returnVal, &min)) == TRANSACTION_OUTCOME_NONE);
// 
// 	if(eOutcome == TRANSACTION_OUTCOME_FAILURE)
// 	{
// 		glua_pushNil();
// 		break;
// 	}

	glua_pushNumber(min);
}
GLUA_END

GLUA_FUNC( metric_get_maximum )
{
	TransactionReturnVal returnVal = {0};
	//enumTransactionOutcome eOutcome;
	F32 max = 0.0f;

	GLUA_ARG_COUNT_CHECK( metric_get_maximum, 1, 1 );
	GLUA_ARG_CHECK( metric_get_maximum, 1, GLUA_TYPE_STRING, false, "" );

// 	RemoteCommand_TestClient_GetMetricMax(&returnVal, GLOBALTYPE_TESTSERVER, 0, glua_getString(1));
// 
// 	do 
// 	{
// 		TestClientLib_Tick();
// 	} while ((eOutcome = RemoteCommandCheck_TestClient_GetMetricMax(&returnVal, &max)) == TRANSACTION_OUTCOME_NONE);
// 
// 	if(eOutcome == TRANSACTION_OUTCOME_FAILURE)
// 	{
// 		glua_pushNil();
// 		break;
// 	}

	glua_pushNumber(max);
}
GLUA_END

GLUA_FUNC( metric_get )
{
	TransactionReturnVal returnVal = {0};
	//enumTransactionOutcome eOutcome;
	int count = 0;
	F32 avg = 0.0f, min = 0.0f, max = 0.0f;

	GLUA_ARG_COUNT_CHECK( metric_get, 1, 1 );
	GLUA_ARG_CHECK( metric_get, 1, GLUA_TYPE_STRING, false, "" );

// 	RemoteCommand_TestClient_GetMetricCount(&returnVal, GLOBALTYPE_TESTSERVER, 0, glua_getString(1));
// 
// 	do 
// 	{
// 		TestClientLib_Tick();
// 	} while ((eOutcome = RemoteCommandCheck_TestClient_GetMetricCount(&returnVal, &count)) == TRANSACTION_OUTCOME_NONE);
// 
// 	if(eOutcome == TRANSACTION_OUTCOME_FAILURE)
// 	{
// 		glua_pushNil();
// 		break;
// 	}
// 
// 	if (returnVal.iNumBaseTransactions)
// 	{
// 		for (i=0; i < returnVal.iNumBaseTransactions; i++)
// 		{
// 			estrDestroy(&returnVal.pBaseReturnVals[i].returnString);
// 		}
// 
// 		free(returnVal.pBaseReturnVals);
// 	}
// 	memset(&returnVal,0,sizeof(TransactionReturnVal));
// 
// 	RemoteCommand_TestClient_GetMetricAvg(&returnVal, GLOBALTYPE_TESTSERVER, 0, glua_getString(1));
// 
// 	do 
// 	{
// 		TestClientLib_Tick();
// 	} while ((eOutcome = RemoteCommandCheck_TestClient_GetMetricAvg(&returnVal, &avg)) == TRANSACTION_OUTCOME_NONE);
// 
// 	if(eOutcome == TRANSACTION_OUTCOME_FAILURE)
// 	{
// 		glua_pushNil();
// 		break;
// 	}
// 
// 	if (returnVal.iNumBaseTransactions)
// 	{
// 		for (i=0; i < returnVal.iNumBaseTransactions; i++)
// 		{
// 			estrDestroy(&returnVal.pBaseReturnVals[i].returnString);
// 		}
// 
// 		free(returnVal.pBaseReturnVals);
// 	}
// 	memset(&returnVal,0,sizeof(TransactionReturnVal));
// 
// 	RemoteCommand_TestClient_GetMetricMin(&returnVal, GLOBALTYPE_TESTSERVER, 0, glua_getString(1));
// 
// 	do 
// 	{
// 		TestClientLib_Tick();
// 	} while ((eOutcome = RemoteCommandCheck_TestClient_GetMetricMin(&returnVal, &min)) == TRANSACTION_OUTCOME_NONE);
// 
// 	if(eOutcome == TRANSACTION_OUTCOME_FAILURE)
// 	{
// 		glua_pushNil();
// 		break;
// 	}
// 
// 	if (returnVal.iNumBaseTransactions)
// 	{
// 		for (i=0; i < returnVal.iNumBaseTransactions; i++)
// 		{
// 			estrDestroy(&returnVal.pBaseReturnVals[i].returnString);
// 		}
// 
// 		free(returnVal.pBaseReturnVals);
// 	}
// 	memset(&returnVal,0,sizeof(TransactionReturnVal));
// 
// 	RemoteCommand_TestClient_GetMetricMax(&returnVal, GLOBALTYPE_TESTSERVER, 0, glua_getString(1));
// 
// 	do 
// 	{
// 		TestClientLib_Tick();
// 	} while ((eOutcome = RemoteCommandCheck_TestClient_GetMetricMax(&returnVal, &max)) == TRANSACTION_OUTCOME_NONE);
// 
// 	if(eOutcome == TRANSACTION_OUTCOME_FAILURE)
// 	{
// 		glua_pushNil();
// 		break;
// 	}

	glua_pushInteger(count);
	glua_pushNumber(avg);
	glua_pushNumber(min);
	glua_pushNumber(max);
}
GLUA_END

void TestClient_InitMetrics(LuaContext *pContext)
{
	lua_State *_lua_ = pContext->luaState;
	int namespaceTbl = _glua_create_tbl(_lua_, "tc", 0);

	GLUA_DECL( namespaceTbl )
	{
		glua_func( metric_push ),
		glua_func( metric_clear ),
		glua_func( metric_get_count ),
		glua_func( metric_get_average ),
		glua_func( metric_get_minimum ),
		glua_func( metric_get_maximum ),
		glua_func( metric_get )
	}
	GLUA_DECL_END
}

#endif