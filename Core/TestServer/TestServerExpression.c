#include "TestServerExpression.h"
#include "earray.h"
#include "StringCache.h"
#include "TestServerGlobal.h"
#include "TestServerHttp.h"
#include "TestServerIntegration.h"

AUTO_COMMAND ACMD_NAME(ClearExpression);
void TestServer_ClearExpression(const char *pcScope, const char *pcName)
{
	TestServer_ClearGlobal(pcScope, pcName);
}

AUTO_COMMAND ACMD_NAME(NewExpression);
void TestServer_NewExpression(const char *pcScope, const char *pcName, ACMD_FORCETYPE(U32) TestServerExpressionOp eOp)
{
	if(eOp > TSE_Op_Arithmetic_Min && eOp < TSE_Op_Arithmetic_Max)
	{
		TestServer_SetGlobal_Float(pcScope, pcName, -1, 0.0f);
	}
	else if(eOp > TSE_Op_Boolean_Min && eOp < TSE_Op_Boolean_Max)
	{
		TestServer_SetGlobal_Boolean(pcScope, pcName, -1, false);
	}
	else
	{
		return;
	}

	TestServer_SetGlobalValueLabel(pcScope, pcName, -1, "Value");
	TestServer_InsertGlobal_Integer(pcScope, pcName, -1, eOp);
	TestServer_SetGlobalValueLabel(pcScope, pcName, -1, "Type");
	TestServer_SetGlobalType(pcScope, pcName, TSG_Expression);
}

TestServerExpressionOp TestServer_GetExpressionType(const char *pcScope, const char *pcName)
{
	TestServerExpressionOp eOp = TSE_Op_NoOp;
	TestServerGlobalType eType = TSG_Unset;

	TestServer_GlobalAtomicBegin();
	eType = TestServer_GetGlobalType(pcScope, pcName);

	if(eType == TSG_Expression)
	{
		eOp = TestServer_GetGlobal_Integer(pcScope, pcName, 1);
	}
	TestServer_GlobalAtomicEnd();

	return eOp;
}

AUTO_COMMAND ACMD_NAME(IsArithmeticExpression);
bool TestServer_IsArithmeticExpression(const char *pcScope, const char *pcName)
{
	TestServerExpressionOp eOp = TestServer_GetExpressionType(pcScope, pcName);
	return (eOp > TSE_Op_Arithmetic_Min) && (eOp < TSE_Op_Arithmetic_Max);
}

AUTO_COMMAND ACMD_NAME(IsBooleanExpression);
bool TestServer_IsBooleanExpression(const char *pcScope, const char *pcName)
{
	TestServerExpressionOp eOp = TestServer_GetExpressionType(pcScope, pcName);
	return (eOp > TSE_Op_Boolean_Min) && (eOp < TSE_Op_Boolean_Max);
}

AUTO_COMMAND ACMD_NAME(EvalArithmeticExpression);
float TestServer_CmdEvalArithmeticExpression(const char *pcScope, const char *pcName)
{
	float fVal = 0.0f;
	TestServer_EvalArithmeticExpression(pcScope, pcName, TSE_Op_NoOp, &fVal);
	return fVal;
}

void TestServer_EvaluateExpressions(const char *pcScope, const char *pcName)
{
	float fVal = 0.0f;
	bool bVal = false;
	int i = 0, count;
	TestServerGlobalType eType;

	TestServer_GlobalAtomicBegin();
	eType = TestServer_GetGlobalType(pcScope, pcName);

	if(eType == TSG_Metric)
	{
		TestServer_GlobalAtomicEnd();
		return;
	}

	if(eType == TSG_Expression)
	{
		if(TestServer_IsArithmeticExpression(pcScope, pcName) && TestServer_EvalArithmeticExpression(pcScope, pcName, TSE_Op_NoOp, &fVal))
		{
			TestServer_SetGlobal_Float(pcScope, pcName, 0, fVal);
		}
		else if(TestServer_IsBooleanExpression(pcScope, pcName) && TestServer_EvalBooleanExpression(pcScope, pcName, TSE_Op_NoOp, &bVal))
		{
			TestServer_SetGlobal_Boolean(pcScope, pcName, 0, bVal);
		}

		TestServer_GlobalAtomicEnd();
		return;
	}

	count = TestServer_GetGlobalValueCount(pcScope, pcName);
	for(i = 0; i < count; ++i)
	{
		TestServerGlobalValueType eSubType = TestServer_GetGlobalValueType(pcScope, pcName, i);

		if(eSubType == TSGV_Global)
		{
			TestServer_EvaluateExpressions(TestServer_GetGlobal_RefScope(pcScope, pcName, i), TestServer_GetGlobal_RefName(pcScope, pcName, i));
		}
	}
	TestServer_GlobalAtomicEnd();
}

bool TestServer_EvalArithmeticExpression(const char *pcScope, const char *pcName, TestServerExpressionOp eOp, float *fResult)
{
	float fVal = 0.0f;
	int i = 1, count;
	bool bSet = false;
	TestServerGlobalType eType;

	TestServer_GlobalAtomicBegin();
	eType = TestServer_GetGlobalType(pcScope, pcName);

	if(eType == TSG_Unset || eType == TSG_Metric)
	{
		TestServer_GlobalAtomicEnd();
		return false;
	}

	count = TestServer_GetGlobalValueCount(pcScope, pcName);

	if(eType == TSG_Expression)
	{
		eOp = TestServer_GetGlobal_Integer(pcScope, pcName, 1);
		i = 2;
	}

	if(eOp <= TSE_Op_Arithmetic_Min || eOp >= TSE_Op_Arithmetic_Max)
	{
		TestServer_GlobalAtomicEnd();
		return false;
	}

	for( ; i < count; ++i)
	{
		TestServerGlobalValueType eValType = TestServer_GetGlobalValueType(pcScope, pcName, i);
		float fSubVal = 0.0f;
		bool bValid = true;

		switch(eValType)
		{
		case TSGV_Integer:
			fSubVal = TestServer_GetGlobal_Integer(pcScope, pcName, i);
		xcase TSGV_Float:
			fSubVal = TestServer_GetGlobal_Float(pcScope, pcName, i);
		xcase TSGV_Global:
			{
				const char *pcSubScope = TestServer_GetGlobal_RefScope(pcScope, pcName, i);
				const char *pcSubName = TestServer_GetGlobal_RefName(pcScope, pcName, i);
				TestServerGlobalType eSubType = TestServer_GetGlobalType(pcSubScope, pcSubName);

				if(eSubType != TSG_Unset && eSubType != TSG_Metric && TestServer_EvalArithmeticExpression(pcSubScope, pcSubName, eOp, &fSubVal))
				{
					break;
				}
			}
		default:
			bValid = false;
			break;
		}

		if(!bValid)
		{
			continue;
		}

		if(!bSet)
		{
			fVal = fSubVal;
			bSet = true;
			continue;
		}

		switch(eOp)
		{
		case TSE_Op_Add:
			fVal += fSubVal;
		xcase TSE_Op_Sub:
			fVal -= fSubVal;
		xcase TSE_Op_Mul:
			fVal *= fSubVal;
		xcase TSE_Op_Div:
			if(fSubVal == 0.0f)
			{
				printf("Arithmetic expression \"%s::%s\" contained division by zero at index %d!\n", pcScope, pcName, i);
			}
			else
			{
				fVal /= fSubVal;
			}
		xdefault:
			break;
		}
	}
	TestServer_GlobalAtomicEnd();

	if(!bSet)
	{
		return false;
	}

	*fResult = fVal;
	return true;
}

AUTO_COMMAND ACMD_NAME(EvalBooleanExpression);
bool TestServer_CmdEvalBooleanExpression(const char *pcScope, const char *pcName)
{
	bool bVal = 0.0f;
	TestServer_EvalBooleanExpression(pcScope, pcName, TSE_Op_NoOp, &bVal);
	return bVal;
}

bool TestServer_EvalBooleanExpression(const char *pcScope, const char *pcName, TestServerExpressionOp eOp, bool *bResult)
{
	bool bVal = false;
	int i = 0, count;
	bool bSet = false;
	TestServerGlobalType eType;

	TestServer_GlobalAtomicBegin();
	eType = TestServer_GetGlobalType(pcScope, pcName);

	if(eType == TSG_Unset || eType == TSG_Metric)
	{
		TestServer_GlobalAtomicEnd();
		return false;
	}

	count = TestServer_GetGlobalValueCount(pcScope, pcName);

	if(eType == TSG_Expression)
	{
		eOp = TestServer_GetGlobal_Integer(pcScope, pcName, 1);
		i = 1;
	}

	if(eOp <= TSE_Op_Boolean_Min || eOp >= TSE_Op_Boolean_Max)
	{
		TestServer_GlobalAtomicEnd();
		return false;
	}

	for( ; i < count; ++i)
	{
		TestServerGlobalValueType eValType = TestServer_GetGlobalValueType(pcScope, pcName, i);
		bool bSubVal = 0.0f;
		bool bValid = true;

		switch(eValType)
		{
		case TSGV_Boolean:
			bSubVal = TestServer_GetGlobal_Boolean(pcScope, pcName, i);

			if(eOp == TSE_Op_Not)
			{
				bSubVal = !bSubVal;
			}
		xcase TSGV_Global:
			{
				const char *pcSubScope = TestServer_GetGlobal_RefScope(pcScope, pcName, i);
				const char *pcSubName = TestServer_GetGlobal_RefName(pcScope, pcName, i);
				TestServerGlobalType eSubType = TestServer_GetGlobalType(pcSubScope, pcSubName);

				if(eSubType != TSG_Unset && eSubType != TSG_Metric && TestServer_EvalBooleanExpression(pcSubScope, pcSubName, eOp, &bSubVal))
				{
					break;
				}
			}
		default:
			bValid = false;
			break;
		}

		if(!bValid)
		{
			continue;
		}

		if(!bSet)
		{
			bVal = bSubVal;

			if(eOp == TSE_Op_Not)
			{
				break;
			}

			bSet = true;
			continue;
		}

		switch(eOp)
		{
		case TSE_Op_And:
			bVal &= bSubVal;
		xcase TSE_Op_Or:
			bVal |= bSubVal;
		xdefault:
			break;
		}
	}
	TestServer_GlobalAtomicEnd();

	if(!bSet)
	{
		return false;
	}

	*bResult = bVal;
	return true;
}