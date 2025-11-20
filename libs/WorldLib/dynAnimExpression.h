#pragma once
GCC_SYSTEM

#include "dynNode.h"
#include "dynSkeleton.h"

AUTO_ENUM;
typedef enum eDynAnimExpressionToken
{
	eDynAnimExprTok_Other, // BAD value, if this happens it means there's a syntax error somewhere

	eDynAnimExprTok_Assign,

	eDynAnimExprTok_String,
	eDynAnimExprTok_Float,
	eDynAnimExprTok_Int,

	eDynAnimExprTok_Function,

	eDynAnimExprTok_Var,
	eDynAnimExprTok_VarWithComponent,
	eDynAnimExprTok_VarWithTwoComponents,
	eDynAnimExprTok_VarWithComponentAndAccessor,
	eDynAnimExprTok_VarWithTwoComponentsAndAccessor,

	eDynAnimExprTok_Quat,
	eDynAnimExprTok_Vec3,
	eDynAnimExprTok_Vec4,
	eDynAnimExprTok_QuatRT,
	eDynAnimExprTok_Vec3RT,
	eDynAnimExprTok_Vec4RT,

	eDynAnimExprTok_Neg,

	eDynAnimExprTok_Add,
	eDynAnimExprTok_Sub,
	eDynAnimExprTok_Mul,
	eDynAnimExprTok_Div,
	eDynAnimExprTok_Mod,

	eDynAnimExprTok_LParen,
	eDynAnimExprTok_RParen,
	eDynAnimExprTok_LChevron,
	eDynAnimExprTok_RChevron,
	eDynAnimExprTok_LCurley,
	eDynAnimExprTok_RCurley,
	eDynAnimExprTok_LBracket,
	eDynAnimExprTok_RBracket,

	eDynAnimExprTok_Comma,
	eDynAnimExprTok_Dot,
	eDynAnimExprTok_WhiteSpace,
}
eDynAnimExpressionToken;
extern StaticDefineInt eDynAnimExpressionTokenEnum[];

AUTO_STRUCT;
typedef struct DynAnimExpressionBlock
{
	eDynAnimExpressionToken eType;
	const char *pcName;	AST(POOL_STRING)
	F32 vVecValue[4]; AST(NAME(vecvalue))
	F32 fFloatValue;
	S32 iIntValue;
}
DynAnimExpressionBlock;
extern ParseTable parse_DynAnimExpressionBlock[];
#define TYPE_parse_DynAnimExpressionBlock DynAnimExpressionBlock

typedef struct DynAnimExpressionRuntimeData
{
	const char *pcNodeName;
	DynTransform xLastFrameWS;
	bool bNeedsInit;
}
DynAnimExpressionRuntimeData;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct DynAnimExpression
{
	const char* pcExpression; AST(STRUCTPARAM)
	DynAnimExpressionBlock **eaBlocksTarget;AST(NO_TEXT_SAVE)
	DynAnimExpressionBlock **eaBlocksCode;	AST(NO_TEXT_SAVE)
}
DynAnimExpression;
extern ParseTable parse_DynAnimExpression[];
#define TYPE_parse_DynAnimExpression DynAnimExpression

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynAnimExpressionSet
{
	const char*	pcName;		AST(KEY POOL_STRING)
	const char*	pcFileName;	AST(CURRENTFILE)
	DynAnimExpression **eaDynAnimExpression;
}
DynAnimExpressionSet;
extern ParseTable parse_DynAnimExpressionSet[];
#define TYPE_parse_DynAnimExpressionSet DynAnimExpressionSet

void dynAnimExpressionSetLoadAll(void);
void dynAnimExpressionDebugNode(DynNode *pNode);
void dynAnimExpressionRun(DynSkeleton *pSkeleton, DynNode *pNode, F32 fDeltaTime);
void dynAnimExpressionInvalidateData(DynSkeleton *pSkeleton, const DynNode *pNode);
void dynAnimExpressionUpdateData(DynSkeleton *pSkeleton, const DynNode *pNode);