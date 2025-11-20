/***************************************************************************
 
 
 
 ***************************************************************************/
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


#include "estring.h"
#include "earray.h"
#include "StashTable.h"
#include "mathutil.h"  // for rule30Float
#include "timing.h"    // for timergetSecondsSince20000FromString

#include "eval.h"

typedef struct EvalContext
{
	bool bError;
	bool bDebug;
	char **piVals;			// MK: switched this to 64-bit compatible earray
	int *piTypes;
	StashTable hashInts;
	StashTable hashStrings;
	StashTable hashFloats;
	StashTable hashFuncs;

	char **ppchExpr;
	int iNumTokens;
	int iCurToken;
	bool bDone;

	char *pchFileBlame;
	char *pchGroupBlame;
				// allow blame to be assigned on a per-context basis
} EvalContext;


typedef enum CellType
{
	kCellType_Void,
	kCellType_Int,
	kCellType_String,
	kCellType_Float,
} CellType;

typedef void (*EvalFunc)(EvalContext *pcontext);

typedef struct FuncDesc
{
	char *pchName;
	EvalFunc pfn;
	int iNumIn;
	int iNumOut;
} FuncDesc;


/***************************************************************************/
/* Helper functions */
/***************************************************************************/

/**********************************************************************func*
 * FindFuncDesc
 *
 */
static FuncDesc *FindFuncDesc(EvalContext *pcontext, char *pchName)
{
	FuncDesc *pdesc;

	if(stashFindInt(pcontext->hashFuncs, pchName, (int *)&pdesc))
	{
		return pdesc;
	}

	return NULL;
}

/**********************************************************************func*
 * FindFunc
 *
 */
static EvalFunc FindFunc(EvalContext *pcontext, char *pchName)
{
	FuncDesc *pdesc = FindFuncDesc(pcontext, pchName);

	if(pdesc)
	{
		return pdesc->pfn;
	}

	return NULL;
}

/**********************************************************************func*
 * eval_Dump
 *
 */
void eval_Dump(EvalContext *pcontext)
{
	int i;
	for(i=eaSize(&pcontext->piVals)-1; i>=0; i--)
	{
		int itype = pcontext->piTypes[i];
		switch(itype)
		{
			case kCellType_Int:
			{
				printf("    %2d: int: %d\n", i, PTR_TO_S32(pcontext->piVals[i]));
				break;
			}

			case kCellType_String:
			{
				printf("    %2d: str: <%s>\n", i, pcontext->piVals[i]);
				break;
			}

			case kCellType_Float:
			{
				printf("    %2d: flt: %f\n", i, PTR_TO_F32(pcontext->piVals[i]));
				break;
			}

			default:
			{
				printf("    %2d: ???: <%08x>\n", i, PTR_TO_S32(pcontext->piVals[i]));
				break;
			}
		}
	}
}

/***************************************************************************/
/* Stack functions */
/***************************************************************************/

bool eval_StackIsEmpty(EvalContext *pcontext)
{
	return eaiSize(&pcontext->piTypes)<=0;
}

/**********************************************************************func*
 * ToNum
 *
 */
void ToNum(EvalContext *pcontext)
{
	int n = eaiSize(&pcontext->piTypes)-1;
	
	if(n>=0)
	{
		if(pcontext->piTypes[n]!=kCellType_Int
			&& pcontext->piTypes[n]!=kCellType_Float)
		{
			EvalFunc pfn;

			if(pcontext->bDebug) 
			{
				printf("  (convert to number)\n");
				printf("  {\n");
			}

			if(pfn = FindFunc(pcontext, "num>"))
			{
				if(pcontext->bDebug) printf("    (num>)\n");
				pfn(pcontext);
			}
			else if(pfn = FindFunc(pcontext, "int>"))
			{
				if(pcontext->bDebug) printf("    (int>)\n");
				pfn(pcontext);
			}
			else if(pfn = FindFunc(pcontext, "float>"))
			{
				if(pcontext->bDebug) printf("    (float>)\n");
				pfn(pcontext);
			}

			if(pcontext->bDebug) 
			{
				printf("    Result:\n");
				eval_Dump(pcontext);
				printf("  }\n");
			}
		}
	}
}

/**********************************************************************func*
 * ToInt
 *
 */
void ToInt(EvalContext *pcontext)
{
	int n = eaiSize(&pcontext->piTypes)-1;

	if(n>=0)
	{
		ToNum(pcontext);

		switch(pcontext->piTypes[n])
		{
			case kCellType_Int:
				break;

			case kCellType_Float:
				{
					char* sval = eaPop(&pcontext->piVals);
					float fval = PTR_TO_F32(sval);
					eaiPop(&pcontext->piTypes);

					eval_IntPush(pcontext, (int)fval);
				}
				break;
		}
	}
}

/**********************************************************************func*
 * ToFloat
 *
 */
void ToFloat(EvalContext *pcontext)
{
	int n = eaiSize(&pcontext->piTypes)-1;

	if(n>=0)
	{
		ToNum(pcontext);

		switch(pcontext->piTypes[n])
		{
			case kCellType_Int:
				{
					int ival = PTR_TO_S32(eaPop(&pcontext->piVals));
					eaiPop(&pcontext->piTypes);

					eval_FloatPush(pcontext, (float)ival);
				}
				break;

			case kCellType_Float:
				break;
		}
	}
}

/**********************************************************************func*
 * ToDate
 *
 */
void ToDate(EvalContext *pcontext)
{
	int n = eaiSize(&pcontext->piTypes)-1;
	if(n>=0)
	{
		U32 usecs = 0;

		int itype = eaiPop(&pcontext->piTypes);
		switch(itype)
		{
			case kCellType_String:
				{
					char *pch;
					pch = eaPop(&pcontext->piVals);
					usecs = timeGetSecondsSince2000FromDateString(pch);

					eval_IntPush(pcontext, usecs);
				}
				break;

			case kCellType_Float:
			case kCellType_Int:
				eaiPush(&pcontext->piTypes, itype);
				break;
		}
	}
}

/**********************************************************************func*
 * eval_IntPop
 *
 */
int eval_IntPop(EvalContext *pcontext)
{
	int itype;
	int ival;

	ToInt(pcontext);

	itype = eaiPop(&pcontext->piTypes);
	ival = PTR_TO_S32(eaPop(&pcontext->piVals));

	if(itype!=kCellType_Int)
		pcontext->bError = true;

	return ival;
}

/**********************************************************************func*
 * eval_IntPush
 *
 */
void eval_IntPush(EvalContext *pcontext, int i)
{
	eaiPush(&pcontext->piTypes, kCellType_Int);
	eaPush(&pcontext->piVals, (char*)S32_TO_PTR(i));
}

/**********************************************************************func*
 * eval_IntPeek
 *
 */
int eval_IntPeek(EvalContext *pcontext)
{
	int iRet = 0;
	int n;

	ToInt(pcontext);

	n = eaiSize(&pcontext->piTypes)-1;
	if(n>=0)
	{
		int itype = pcontext->piTypes[n];
		iRet = PTR_TO_S32(pcontext->piVals[n]);

		if(itype!=kCellType_Int)
			pcontext->bError = true;
	}

	return iRet;
}

/**********************************************************************func*
 * eval_StringPop
 *
 */
char *eval_StringPop(EvalContext *pcontext)
{
	int itype = eaiPop(&pcontext->piTypes);
	char *pch = eaPop(&pcontext->piVals);

	switch(itype)
	{
		case kCellType_String:
			break;

		case kCellType_Int:
			pcontext->bError = true;
			pch = "*integer*";
			break;

		case kCellType_Float:
			pcontext->bError = true;
			pch = "*float*";
			break;

		case kCellType_Void:
			pcontext->bError = true;
			pch = "*empty stack*";
			break;
	}

	if(!pch)
		pch="*null*";

	return pch;
}

/**********************************************************************func*
 * eval_StringPush
 *
 */
void eval_StringPush(EvalContext *pcontext, char *pch)
{
	eaiPush(&pcontext->piTypes, kCellType_String);
	eaPush(&pcontext->piVals, pch);
}

/**********************************************************************func*
 * eval_StringPeek
 *
 */
char *eval_StringPeek(EvalContext *pcontext)
{
	char *pchRet = NULL;
	int n = eaiSize(&pcontext->piTypes)-1;

	if(n>=0)
	{
		int itype = pcontext->piTypes[n];

		if(itype!=kCellType_String)
		{
			pcontext->bError = true;
		}
		else
		{
			pchRet = pcontext->piVals[n];
		}
	}
	else
	{
		pcontext->bError = true;
	}

	return pchRet;
}

/**********************************************************************func*
 * eval_BoolPop
 *
 */
bool eval_BoolPop(EvalContext *pcontext)
{
	char *pch;
	bool bRet = false;
	int itype = eaiPop(&pcontext->piTypes);

	switch(itype)
	{
		case kCellType_Int:
			bRet = PTR_TO_S32(eaPop(&pcontext->piVals))!=0;
			break;

		case kCellType_Float:
			pch = eaPop(&pcontext->piVals);
			bRet = PTR_TO_F32(pch)!=0;
			break;

		case kCellType_String:
		{
			pch = eaPop(&pcontext->piVals);
			bRet = (pch && strlen(pch)>0);
			break;
		}

		default:
			eaPop(&pcontext->piVals);
			break;
	}

	return bRet;
}

/**********************************************************************func*
 * eval_BoolPush
 *
 */
void eval_BoolPush(EvalContext *pcontext, bool b)
{
	eval_IntPush(pcontext, b);
}

/**********************************************************************func*
 * eval_BoolPeek
 *
 */
bool eval_BoolPeek(EvalContext *pcontext)
{
	bool bRet = false;
	int n = eaiSize(&pcontext->piTypes)-1;

	if(n>=0)
	{
		int itype = pcontext->piTypes[n];

		switch(itype)
		{
			case kCellType_Int:
				bRet = PTR_TO_S32(pcontext->piVals[n])!=0;
				break;

			case kCellType_Float:
				bRet = PTR_TO_F32(pcontext->piVals[n]) != 0.0f;
				break;

			case kCellType_String:
			{
				char *pch = pcontext->piVals[n];
				bRet = (pch && strlen(pch)>0);
				break;
			}
		}
	}

	return bRet;
}

/**********************************************************************func*
 * eval_FloatPop
 *
 */
float eval_FloatPop(EvalContext *pcontext)
{
	int itype;
	float fval;
	char* pch;

	ToFloat(pcontext);

	itype = eaiPop(&pcontext->piTypes);
	pch = eaPop(&pcontext->piVals);
	fval = PTR_TO_F32(pch);

	if(itype!=kCellType_Float)
		pcontext->bError = true;

	return fval;
}

/**********************************************************************func*
 * eval_FloatPush
 *
 */
void eval_FloatPush(EvalContext *pcontext, float f)
{
	char* pch;
	eaiPush(&pcontext->piTypes, kCellType_Float);
	PTR_ASSIGNFROM_F32(pch, f);
	eaPush(&pcontext->piVals, pch);
}

/**********************************************************************func*
 * eval_FloatPeek
 *
 */
float eval_FloatPeek(EvalContext *pcontext)
{
	float fRet = 0;
	int n;

	ToFloat(pcontext);

	n = eaiSize(&pcontext->piTypes)-1;
	if(n>=0)
	{
		int itype = pcontext->piTypes[n];
		fRet = PTR_TO_F32(pcontext->piVals[n]);

		if(itype!=kCellType_Float)
			pcontext->bError = true;
	}

	return fRet;
}

/**********************************************************************func*
 * eval_Dup
 *
 */
void eval_Dup(EvalContext *pcontext)
{
	int n = eaiSize(&pcontext->piTypes)-1;
	if(n>=0)
	{
		eaiPush(&pcontext->piTypes, pcontext->piTypes[n]);
		eaPush(&pcontext->piVals, pcontext->piVals[n]);
	}
}

/**********************************************************************func*
 * eval_Drop
 *
 */
void eval_Drop(EvalContext *pcontext)
{
	eaiPop(&pcontext->piTypes);
	eaPop(&pcontext->piVals);
}

/***************************************************************************/
/* Intrinsic functions */
/***************************************************************************/

static void LogicalAnd(EvalContext *pcontext)
{
	int rhs = eval_IntPop(pcontext);
	int lhs = eval_IntPop(pcontext);
	eval_IntPush(pcontext, lhs&&rhs);
}

static void LogicalOr(EvalContext *pcontext)
{
	int rhs = eval_IntPop(pcontext);
	int lhs = eval_IntPop(pcontext);
	eval_IntPush(pcontext, lhs||rhs);
}

static void LogicalNot(EvalContext *pcontext)
{
	int lhs = eval_IntPop(pcontext);
	eval_IntPush(pcontext, !lhs);
}

#define NUMERIC_OP(type, op) \
{                                                      \
	int n = eaiSize(&pcontext->piTypes)-1;    \
	if(n>=1                                            \
		&& (pcontext->piTypes[n]!=kCellType_Int        \
			|| pcontext->piTypes[n-1]!=kCellType_Int)) \
	{                                                  \
		float rhs = eval_FloatPop(pcontext);           \
		float lhs = eval_FloatPop(pcontext);           \
		eval_##type##Push(pcontext, op);       \
	}                                                  \
	else                                               \
	{                                                  \
		int rhs = eval_IntPop(pcontext);               \
		int lhs = eval_IntPop(pcontext);               \
		eval_IntPush(pcontext, op);            \
	}                                                  \
}


static void NumericGreater(EvalContext *pcontext)
{
	NUMERIC_OP(Int, lhs > rhs);
}

static void NumericGreaterEqual(EvalContext *pcontext)
{
	NUMERIC_OP(Int, lhs >= rhs);
}

static void NumericLess(EvalContext *pcontext)
{
	NUMERIC_OP(Int, lhs < rhs );
}

static void NumericLessEqual(EvalContext *pcontext)
{
	NUMERIC_OP(Int, lhs <= rhs);
}

static void NumericAdd(EvalContext *pcontext)
{
	NUMERIC_OP(Float, lhs + rhs);
}

static void NumericSubtract(EvalContext *pcontext)
{
	NUMERIC_OP(Float, lhs - rhs);
}

static void NumericMultiply(EvalContext *pcontext)
{
	NUMERIC_OP(Float, lhs * rhs);
}

static void StringEqual(EvalContext *pcontext)
{
	int n = eaiSize(&pcontext->piTypes)-1;
	if(n>=1)
	{
		if(pcontext->piTypes[n]==kCellType_String
			&& pcontext->piTypes[n-1]==kCellType_String)
		{
			char *rhs = eval_StringPop(pcontext);
			char *lhs = eval_StringPop(pcontext);
			eval_IntPush(pcontext, stricmp(lhs, rhs)==0);
			return;
		} else {
			if(pcontext->bDebug) 
				printf("Found an eq comparing two non-strings.\n");
			eval_BoolPop(pcontext);
			eval_BoolPop(pcontext);
			eval_IntPush(pcontext, 0);
		}
	}
}

static void NumericEqual(EvalContext *pcontext)
{
	{
		int n = eaiSize(&pcontext->piTypes)-1;
		if(n>=1)
		{
			if(pcontext->piTypes[n]==kCellType_String
				&& pcontext->piTypes[n-1]==kCellType_String)
			{
				// The user probably meant to use string equality. Help them out.
				if(pcontext->bDebug) printf("Found a == comparing two strings. Using eq instead.\n");
				StringEqual(pcontext);
				return;
			}
		}
	}

	// Finally test for numeric equality
	NUMERIC_OP(Int, lhs==rhs);
}

static void NumericDivide(EvalContext *pcontext)
{
	float rhs = eval_FloatPop(pcontext);
	float lhs = eval_FloatPop(pcontext);

	if(rhs==0)
		eval_FloatPush(pcontext, 0);
	else
		eval_FloatPush(pcontext, lhs / rhs);
}

static void NumericModulo(EvalContext *pcontext)
{
	int rhs = eval_IntPop(pcontext);
	int lhs = eval_IntPop(pcontext);

	if(rhs==0)
		eval_IntPush(pcontext, 0);
	else
		eval_IntPush(pcontext, lhs % rhs);
}

static void NumericPower(EvalContext *pcontext)
{
	float rhs = eval_FloatPop(pcontext);
	float lhs = eval_FloatPop(pcontext);
	eval_FloatPush(pcontext, powf(lhs, rhs));
}

static void NumericMinMax(EvalContext *pcontext)
{
	float max = eval_FloatPop(pcontext);
	float min = eval_FloatPop(pcontext);
	float val = eval_FloatPop(pcontext);

	if(val<min)
		val=min;
	else if(val>max)
		val=max;

	eval_FloatPush(pcontext, val);
}

static void NumericNegate(EvalContext *pcontext)
{
	int n = eaiSize(&pcontext->piTypes)-1;
	if(n>=0)
	{
		if(pcontext->piTypes[n]==kCellType_Int)
		{
			int i = eval_IntPop(pcontext);
			eval_IntPush(pcontext, -i);
		}
		else
		{
			float f = eval_FloatPop(pcontext);
			eval_FloatPush(pcontext, -f);
		}
	}
	else
	{
		eval_FloatPush(pcontext, 0.0f);
	}
}

static void Random(EvalContext *pcontext)
{
	float fRand = rule30Float();
	if (fRand < 0.0f) fRand *= -1;
	eval_FloatPush(pcontext, fRand);
}

static void Store(EvalContext *pcontext)
{
	char *rhs = eval_StringPop(pcontext);

	int itype = eaiPop(&pcontext->piTypes);
	switch(itype)
	{
		case kCellType_Int:
		{
			int i = PTR_TO_S32(eaPop(&pcontext->piVals));
			eval_StoreInt(pcontext, rhs, i);
			break;
		}

		case kCellType_Float:
		{
			char* pch = eaPop(&pcontext->piVals);
			float f = PTR_TO_F32(pch);
			eval_StoreFloat(pcontext, rhs, f);
			break;
		}

		case kCellType_String:
		{
			char *pch = eaPop(&pcontext->piVals);
			eval_StoreString(pcontext, rhs, pch);
			break;
		}

		default:
			eaPop(&pcontext->piVals);
			break;
	}
}

static void StoreAdd(EvalContext *pcontext)
{
	char *rhs = eval_StringPop(pcontext);

	int itype = eaiPop(&pcontext->piTypes);
	switch(itype)
	{
		case kCellType_Int:
		{
			int cur;
			int i = PTR_TO_S32(eaPop(&pcontext->piVals));
			eval_FetchInt(pcontext, rhs, &cur);
			eval_StoreInt(pcontext, rhs, cur+i);
			break;
		}

		case kCellType_Float:
		{
			char* pch = eaPop(&pcontext->piVals);
			float cur;
			float f = PTR_TO_F32(pch);
			eval_FetchFloat(pcontext, rhs, &cur);
			eval_StoreFloat(pcontext, rhs, cur+f);
			break;
		}

		case kCellType_String:
		{
			char *pch = eaPop(&pcontext->piVals);
			eval_StoreString(pcontext, rhs, pch);
			break;
		}

		default:
			eaPop(&pcontext->piVals);
			break;
	}
}

static void FetchString(EvalContext *pcontext)
{
	char *rhs = eval_StringPop(pcontext);
	char *pch;

	if(eval_FetchString(pcontext, rhs, &pch))
	{
		eval_StringPush(pcontext, pch);
	}
	else
	{
		eval_StringPush(pcontext, rhs);
	}
}

static void FetchInt(EvalContext *pcontext)
{
	char *rhs = eval_StringPop(pcontext);
	int i;

	if(eval_FetchInt(pcontext, rhs, &i))
	{
		eval_IntPush(pcontext, i);
	}
	else
	{
		eval_IntPush(pcontext, 0);
	}
}

static void FetchFloat(EvalContext *pcontext)
{
	char *rhs = eval_StringPop(pcontext);
	float f;

	if(eval_FetchFloat(pcontext, rhs, &f))
	{
		eval_FloatPush(pcontext, f);
	}
	else
	{
		eval_FloatPush(pcontext, 0);
	}
}

static void Fetch(EvalContext *pcontext)
{
	char *rhs = eval_StringPop(pcontext);
	int i;

	if(eval_FetchInt(pcontext, rhs, &i))
	{
		eval_IntPush(pcontext, i);
	}
	else
	{
		float f;
		if(eval_FetchFloat(pcontext, rhs, &f))
		{
			eval_FloatPush(pcontext, f);
		}
		else
		{
			char *pch;

			if(eval_FetchString(pcontext, rhs, &pch))
			{
				eval_StringPush(pcontext, pch);
			}
			else
			{
				eval_StringPush(pcontext, rhs);
			}
		}
	}
}

static void Forget(EvalContext *pcontext)
{
	eval_StringPop(pcontext);
}

static void Conditional(EvalContext *pcontext)
{
	bool bIsTrue = eval_BoolPop(pcontext);

	if(!bIsTrue)
	{
		// iCurToken is pre-incremented past the Conditional already.
		while(pcontext->iCurToken < pcontext->iNumTokens)
		{
			if(stricmp(pcontext->ppchExpr[pcontext->iCurToken], "endif")==0)
			{
				break;
			}
			pcontext->iCurToken++;
		}
	}
}

static void Goto(EvalContext *pcontext)
{
	int i = 0;
	char *rhs = eval_StringPop(pcontext);

	while(i < pcontext->iNumTokens)
	{
		if(stricmp(pcontext->ppchExpr[i], rhs)==0)
		{
			pcontext->iCurToken = i;
			break;
		}
		i++;
	}
}

static void ConditionalGoto(EvalContext *pcontext)
{
	int i = 0;
	char *pchLabel = eval_StringPop(pcontext);
	bool bIsTrue = eval_BoolPop(pcontext);

	// iCurToken is pre-incremented past the Conditional already.
	if(bIsTrue)
	{
		while(i < pcontext->iNumTokens)
		{
			if(stricmp(pcontext->ppchExpr[i], pchLabel)==0)
			{
				pcontext->iCurToken = i;
				break;
			}
			i++;
		}
	}
}

static void Noop(EvalContext *pcontext)
{
	return;
}

static FuncDesc s_FuncTable[] =
{
	{ "&&",     LogicalAnd          , 2, 1},
	{ "||",     LogicalOr           , 2, 1},
	{ "!",      LogicalNot          , 1, 1},
	{ ">",      NumericGreater      , 2, 1},
	{ "<",      NumericLess         , 2, 1},
	{ ">=",     NumericGreaterEqual , 2, 1},
	{ "<=",     NumericLessEqual    , 2, 1},
	{ "==",     NumericEqual        , 2, 1},

	{ "eq",     StringEqual         , 2, 1},

	{ "+",      NumericAdd          , 2, 1},
	{ "*",      NumericMultiply     , 2, 1},
	{ "/",      NumericDivide       , 2, 1},
	{ "%",      NumericModulo       , 2, 1},
	{ "-",      NumericSubtract     , 2, 1},
	{ "pow",    NumericPower        , 2, 1},

	{ "negate", NumericNegate       , 1, 1},
	{ "rand",   Random              , 0, 1},
	{ "minmax", NumericMinMax       , 3, 1},

	{ "date>",  ToDate              , 1, 1},

	{ ">var",   Store               , 2, 0},
	{ ">var+",  StoreAdd            , 2, 0},
	{ "var>",   Fetch               , 1, 1},
	{ "auto>",  Fetch               , 1, 1},
	{ "forget", Forget              , 1, 0},

	{ "dup",    eval_Dup            , 1, 2},
	{ "drop",   eval_Drop           , 1, 0},

	{ "if",     Noop                , 0, 0},
	{ "then",   Conditional         , 1, 0},
	{ "endif",  Noop                , 0, 0},

	{ "label",  eval_Drop           , 1, 0},
	{ ":",      eval_Drop           , 1, 0},
	{ "goto",   Goto                , 1, 0},
	{ "gotoif", ConditionalGoto     , 2, 0},
};

/***************************************************************************/
/* External variable store and fetch */
/***************************************************************************/

/**********************************************************************func*
 * eval_StoreString
 *
 */
void eval_StoreString(EvalContext *pcontext, char *pchName, char *pchVal)
{
	stashAddPointer(pcontext->hashStrings, pchName, &pchVal, true);
}

/**********************************************************************func*
 * eval_FetchString
 *
 */
bool eval_FetchString(EvalContext *pcontext, char *pchName, char **pchVal)
{
	return stashFindInt(pcontext->hashStrings, pchName, (int *)pchVal);
}

/**********************************************************************func*
 * eval_ForgetString
 *
 */
void eval_ForgetString(EvalContext *pcontext, char *pchName)
{
	stashRemovePointer(pcontext->hashStrings, pchName, NULL);
}

/**********************************************************************func*
 * eval_StoreInt
 *
 */
void eval_StoreInt(EvalContext *pcontext, char *pchName, int iVal)
{
	stashAddInt(pcontext->hashInts, pchName, iVal, true);
}

/**********************************************************************func*
 * eval_FetchInt
 *
 */
bool eval_FetchInt(EvalContext *pcontext, char *pchName, int *piVal)
{
	return stashFindInt(pcontext->hashInts, pchName, piVal);
}

/**********************************************************************func*
 * eval_ForgetInt
 *
 */
void eval_ForgetInt(EvalContext *pcontext, char *pchName)
{
	stashRemovePointer(pcontext->hashInts, pchName, NULL);
}

/**********************************************************************func*
 * eval_StoreFloat
 *
 */
void eval_StoreFloat(EvalContext *pcontext, char *pchName, float fVal)
{
	stashAddInt(pcontext->hashFloats, pchName, *(int *)&fVal, true);
}

/**********************************************************************func*
 * eval_FetchFloat
 *
 */
bool eval_FetchFloat(EvalContext *pcontext, char *pchName, float *pfVal)
{
	int iVal;
	bool b = stashFindInt(pcontext->hashFloats, pchName, &iVal);
	
	*pfVal = *(float *)&iVal;
	return b;
}

/**********************************************************************func*
 * eval_ForgetFloat
 *
 */
void eval_ForgetFloat(EvalContext *pcontext, char *pchName)
{
	stashRemovePointer(pcontext->hashFloats, pchName, NULL);
}

/***************************************************************************/
/* EvalContext create/destruct/config */
/***************************************************************************/

/**********************************************************************func*
 * eval_ReRegisterFunc
 *
 */
void eval_ReRegisterFunc(EvalContext *pcontext, char *pchName, EvalFunc pfn, int iNumIn, int iNumOut)
{
	eval_UnregisterFunc( pcontext, pchName );
	eval_RegisterFunc( pcontext, pchName, pfn, iNumIn, iNumOut );
}

/**********************************************************************func*
 * eval_RegisterFunc
 *
 */
void eval_RegisterFunc(EvalContext *pcontext, char *pchName, EvalFunc pfn,
	int iNumIn, int iNumOut)
{
	FuncDesc *pdesc = calloc(sizeof(FuncDesc), 1);
	pdesc->pchName = pchName;
	pdesc->pfn = pfn;
	pdesc->iNumIn = iNumIn;
	pdesc->iNumOut = iNumOut;

	verify(stashAddPointer(pcontext->hashFuncs, pchName, pdesc, true));
}


/**********************************************************************func*
 * eval_UnregisterFunc
 *
 */
void eval_UnregisterFunc(EvalContext *pcontext, char *pchName)
{
	FuncDesc *pdesc;
	stashRemovePointer(pcontext->hashFuncs, pchName, &pdesc);
	if(pdesc)
	{
		free(pdesc);
	}
}

/**********************************************************************func*
 * eval_SetDebug
 *
 */
void eval_SetDebug(EvalContext *pcontext, bool bDebug)
{
	pcontext->bDebug = bDebug;
}

/**********************************************************************func*
 * eval_ClearStack
 *
 */
void eval_ClearStack(EvalContext *pcontext)
{
	eaSetSize(&pcontext->piVals, 0);
	eaiSetSize(&pcontext->piTypes, 0);
}

/**********************************************************************func*
 * eval_ClearVariables
 *
 */
void eval_ClearVariables(EvalContext *pcontext)
{
	stashTableClear(pcontext->hashInts);
	stashTableClear(pcontext->hashStrings);
	stashTableClear(pcontext->hashFloats);
}

/**********************************************************************func*
 * eval_Free
 *
 */
static void eval_Free(void* mem)
{
	free(mem);
}

/**********************************************************************func*
 * eval_ClearFuncs
 *
 */
void eval_ClearFuncs(EvalContext *pcontext)
{
	stashTableClearEx(pcontext->hashFuncs, NULL, eval_Free);
}

/**********************************************************************func*
 * eval_Init
 *
 */
void eval_Init(EvalContext *pcontext)
{
	int j;

	if(pcontext->piVals)
	{
		eval_ClearStack(pcontext);
		eval_ClearVariables(pcontext);
		eval_ClearFuncs(pcontext);
	}
	else
	{
		eaCreate(&pcontext->piVals);
		eaiCreate(&pcontext->piTypes);
		pcontext->hashInts = stashTableCreateWithStringKeys(16, StashDefault);
		pcontext->hashStrings = stashTableCreateWithStringKeys(16, StashDefault);
		pcontext->hashFloats = stashTableCreateWithStringKeys(16, StashDefault);
		pcontext->hashFuncs = stashTableCreateWithStringKeys(16, StashDefault);
	}


	for(j=0; j<ARRAY_SIZE(s_FuncTable); j++)
	{
		eval_RegisterFunc(pcontext, s_FuncTable[j].pchName, s_FuncTable[j].pfn,
			s_FuncTable[j].iNumIn, s_FuncTable[j].iNumOut);
	}
}

/**********************************************************************func*
 * eval_Create
 *
 */
EvalContext *eval_Create(void)
{
	EvalContext *pcontext = calloc(sizeof(EvalContext), 1);

	eval_Init(pcontext);
	return pcontext;
}

/**********************************************************************func*
 * eval_Destroy
 *
 */
void eval_Destroy(EvalContext *pcontext)
{
	eaDestroy(&pcontext->piVals);
	eaiDestroy(&pcontext->piTypes);
	stashTableDestroy(pcontext->hashInts);
	stashTableDestroy(pcontext->hashFloats);
	stashTableDestroy(pcontext->hashStrings);
	stashTableDestroyEx(pcontext->hashFuncs, NULL, eval_Free);

	free(pcontext);
}

/***************************************************************************/
/* Expression handling */
/***************************************************************************/

/**********************************************************************func*
 * eval_Validate
 *
 */
bool eval_Validate(EvalContext *pcontext, char *pchItem, char **ppchExpr, char **ppchOut)
{
	int n, i;
	int iSize = 0;
	bool res = true;
	static char errorString[512];
	
	n = eaSize(&ppchExpr);
	if(n==0)
		return true;

	for(i=0; i<n; i++)
	{
		FuncDesc *pfn = FindFuncDesc(pcontext, ppchExpr[i]);
		if(pfn)
		{
			if(iSize<pfn->iNumIn)
			{
				sprintf(errorString,"Bad eval for %s: Not enough operands for %s!\n", pchItem, ppchExpr[i]);
				res = false;
				break;
			}
			iSize = iSize + pfn->iNumOut - pfn->iNumIn;
		}
		else
		{
			iSize++;
		}
	}

	if(iSize<1 && res)
	{
		sprintf(errorString,"Bad eval for %s: No result!\n", pchItem);
		res = false;
	}
	else if(iSize>1 && res)
	{
		sprintf(errorString,"Bad eval for %s: Multiple items (%d) left on stack! (probably means invalid function name)\n", pchItem, iSize);       
		res = false;
	}

	if (res)
	{
		if (ppchOut)
			*ppchOut = NULL;
		return true;
	}
	else
	{
		verbose_printf("%s", errorString);
		if (ppchOut)
			*ppchOut = errorString;
		return false;
	}
}

/**********************************************************************func*
 * eval_Evaluate
 *
 */
bool eval_Evaluate(EvalContext *pcontext, char **ppchExpr)
{
	pcontext->bError = false;
	pcontext->bDone = false;
	pcontext->iCurToken = 0;
	pcontext->ppchExpr = ppchExpr;
	pcontext->iNumTokens = eaSize(&ppchExpr);

	if(pcontext->bDebug) printf("------\n");

	if(pcontext->iNumTokens==0) return true;

	while(pcontext->iCurToken < pcontext->iNumTokens && !pcontext->bDone)
	{
		EvalFunc pfn = NULL;
		EvalFunc pfnPost = NULL;
		char *pchToken = ppchExpr[pcontext->iCurToken];

		if(pcontext->bDebug) printf("--> %s\n", pchToken);

		if(pchToken[0]=='@')
		{
			if(pcontext->bDebug) printf("  (var)\n");
			pfnPost = FindFunc(pcontext, "var>");
			pchToken++;
		}
		else if(pchToken[0]=='$')
		{
			if(pcontext->bDebug) printf("  (auto)\n");
			pfnPost = FindFunc(pcontext, "auto>");
			pchToken++;
		}
		else
		{
			pfn = FindFunc(pcontext, pchToken);
		}

		if(pfn)
		{
			if(pcontext->bDebug) printf("  (func)\n");
			pcontext->iCurToken++;
			pfn(pcontext);
		}
		else
		{
			char *pch;
			bool bCvt = false;

			pch = strchr(pchToken, '.');
			if(pch)
			{
				float f = (float)strtod(pchToken, &pch);
				if(pch>pchToken && *pch=='\0')
				{
					// It seems to have converted to an float
					if(pcontext->bDebug) printf("  (float)\n");
					eval_FloatPush(pcontext, f);
					bCvt = true;
				}
			}
			else
			{
				int j = strtol(pchToken, &pch, 10);
				if(pch>pchToken && *pch=='\0')
				{
					// It seems to have converted to an integer
					if(pcontext->bDebug) printf("  (int)\n");
					eval_IntPush(pcontext, j);
					bCvt = true;
				}
			}

			if(!bCvt)
			{
				// It didn't convert, put the string on the stack
				if(pcontext->bDebug) printf("  (string)\n");
				eval_StringPush(pcontext, pchToken);
			}

			pcontext->iCurToken++;
		}

		if(pfnPost)
		{
			if(pcontext->bDebug)
			{
				printf("  (post func)\n");
				printf("  {\n");
				printf("    stack on entry:\n");
				eval_Dump(pcontext);
			}

			pfnPost(pcontext);

			if(pcontext->bDebug) printf("  }\n");
		}

		if(pcontext->bDebug)
			eval_Dump(pcontext);

	}

	if(pcontext->bError)
	{
		int i;
		char *pch;
		estrStackCreate(&pch);
		estrConcatf(&pch, "Error evaluating expression:\n");
		for(i=0; i<pcontext->iNumTokens; i++)
		{
			estrConcatf(&pch, "%s ", ppchExpr[i]);
		}

		if( pcontext->pchFileBlame && pcontext->pchGroupBlame )
		{
			ErrorFilenameGroupf(  pcontext->pchFileBlame, pcontext->pchGroupBlame, 5, "%s", pch);
		}
		else
		{ 
			Errorf("%s", pch );
		}

		estrDestroy(&pch);
	}

	// This is done in order to force num> evaluation of the top item
	//   on the stack before return. This is used by the badges (at minimum)
	//   and probably any other evaluator which has an implicit num> (or int>).
	// TODO: Perhaps a better way to handle this is by splitting this function
	//   up.
	return (bool)eval_FloatPeek(pcontext);
}


static char **s_eaFilenamesForEvalBlame = NULL;

/**********************************************************************func*
 * eval_SetBlame
 * NOTE: does not intern string name. assumes it persists
 */
void eval_SetBlame(EvalContext *pcontext, char *pchFileBlame, char *pchGroupBlame)
{
	if( pcontext )
	{
		pcontext->pchFileBlame = pchFileBlame;
		pcontext->pchGroupBlame = pchGroupBlame;
	}
}



#ifdef TEST_EVAL
int wmain(int argc, WCHAR** argv_wide)
{
	EvalContext *pcontext;
	char **ppch = NULL;
	int i;

	for(i=1; i<argc; i++)
	{
		printf("%2d:<%s>\n", i-1, argv[i]);
		eaPush(&ppch, argv[i]);
	}

	pcontext = eval_Create();
	eval_SetDebug(pcontext, true);

	eval_Validate(pcontext, "cmdline", ppch,NULL);

	i = eval_Evaluate(pcontext, ppch);
	if(i)
		printf("--> true\n");
	else
		printf("--> false\n");

	eval_Destroy(pcontext);

	return 0;
}
#endif


/* End of File */
