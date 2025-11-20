#include "dynAnimExpression.h"
#include "dynAnimExpression_h_ast.h"

#include "dynNodeInline.h"
#include "dynSeqData.h"
#include "error.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "mathutil.h"
#include "Quat.h"
#include "StringCache.h"
#include "timing.h"
#include "utils.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

TSMP_DEFINE(DynAnimExpressionRuntimeData);

DictionaryHandle hAnimExpressionSetDict;

static const char *s_pcPosComponent;
static const char *s_pcSclComponent;
static const char *s_pcRotComponent;

static const char *s_pcLastFrame;

static const char *s_pcSystem;
static const char *s_pcTime;
static const char *s_pcTimestep;

static const char *s_pcSinFunction;
static const char *s_pcCosFunction;

static const char *s_pcMaxfFunction;
static const char *s_pcMinfFunction;
static const char *s_pcClampfFunction;

static U32 uiDebugDynAnimExpression = 0;

// +-----------------------------------------------------+
// | List of features that this still needs (in my head) |
// +-----------------------------------------------------+

//should work across limbs of a skeleton - need to modify update transforms new
//pre-compute and cache things - I do a lot of find node on skeleton calls that could be done once when creating the skeleton
//doesn't auto-reload correctly when you update the text file, have to restart game client which sucks!
//safe execution instead of asserting w/ errors based on syntax & semantics instead of just crashing out
//support for cross products, dot products, equations in vectors
//support for matrices, matrix rows (over, up, look), inverse matrices, matrix columns, transposed matrices - similar to how I've done pos & scl & rot
//support for world space vs. local space
//support for alternative orientation representations - Euler and Axis-angle

// +--------------------+
// | Tokenizer Routines |
// +--------------------+

static eDynAnimExpressionToken dynAnimExpressionNextCharType(char c)
{
	switch (c)
	{
		xcase '=' : return eDynAnimExprTok_Assign;

		xcase '_' : return eDynAnimExprTok_String;

		xcase '+' : return eDynAnimExprTok_Add;
		xcase '-' : return eDynAnimExprTok_Sub;
		xcase '*' : return eDynAnimExprTok_Mul;
		xcase '/' : return eDynAnimExprTok_Div;
		xcase '%' : return eDynAnimExprTok_Mod;

		xcase '(' : return eDynAnimExprTok_LParen;
		xcase ')' : return eDynAnimExprTok_RParen;
		xcase '[' : return eDynAnimExprTok_LBracket;
		xcase ']' : return eDynAnimExprTok_RBracket;
		xcase '<' : return eDynAnimExprTok_LChevron;
		xcase '>' : return eDynAnimExprTok_RChevron;
		xcase '{' : return eDynAnimExprTok_LCurley;
		xcase '}' : return eDynAnimExprTok_RCurley;

		xcase ',' : return eDynAnimExprTok_Comma;
		xcase '.' : return eDynAnimExprTok_Dot;
		xcase ' ' : return eDynAnimExprTok_WhiteSpace;
		xcase '\t': return eDynAnimExprTok_WhiteSpace;
		xcase '\n': return eDynAnimExprTok_WhiteSpace;
	}

	if ('A' <= c && c <= 'Z') return eDynAnimExprTok_String;
	if ('a' <= c && c <= 'z') return eDynAnimExprTok_String;
	if ('0' <= c && c <= '9') return eDynAnimExprTok_Int;

	return eDynAnimExprTok_Other;
}

static void dynAnimExpressionTokenize(const char *pcExpression, DynAnimExpressionBlock ***peaBlocks)
{
	char buff[256];
	U32 buffLen;
	U32 i;
	bool bChr;
	bool bInt;
	bool bFlt;

	buff[0] = '\0';
	buffLen = i = 0;
	bChr = bInt = bFlt = false;

	while (i < strlen(pcExpression))
	{
		eDynAnimExpressionToken eNext = dynAnimExpressionNextCharType(pcExpression[i]);

		if (eNext == eDynAnimExprTok_Other) {
			assert(0);
		}

		if (buffLen > 0)
		{
			switch (eNext)
			{
				xcase eDynAnimExprTok_Dot :
				{
					if (TRUE_THEN_RESET(bChr)) {
						DynAnimExpressionBlock *pBlock;
						
						pBlock = calloc(1,sizeof(DynAnimExpressionBlock));
						pBlock->eType = eDynAnimExprTok_String;
						pBlock->pcName = allocAddString(buff);
						eaPush(peaBlocks, pBlock);
						buff[0] = '\0';
						buffLen = 0;

						pBlock = calloc(1,sizeof(DynAnimExpressionBlock));
						pBlock->eType = eDynAnimExprTok_Dot;
						eaPush(peaBlocks, pBlock);
					}
					else if (bInt) {
						buff[buffLen] = pcExpression[i];
						buff[++buffLen] = 0;
						bInt = false;
						bFlt = true;
					}
					else {
						assert(0);
					}
				}
				xcase eDynAnimExprTok_String :
				{
					if (bChr) {
						buff[buffLen] = pcExpression[i];
						buff[++buffLen] = '\0';
					} else {
						assert(0);
					}
				}
				xcase eDynAnimExprTok_Int :
				{
					if (bChr || bInt || bFlt) {
						buff[buffLen] = pcExpression[i];
						buff[++buffLen] = '\0';
					} else {
						assert(0);
					}
				}
				xdefault :
				{
					DynAnimExpressionBlock *pBlock = calloc(1,sizeof(DynAnimExpressionBlock));
					if (TRUE_THEN_RESET(bChr)) {
						pBlock->eType = eDynAnimExprTok_String;
						pBlock->pcName = allocAddString(buff);
					}
					else if (TRUE_THEN_RESET(bInt)) {
						pBlock->eType = eDynAnimExprTok_Int;
						pBlock->iIntValue = atoi(buff);
					}
					else if (TRUE_THEN_RESET(bFlt)) {
						pBlock->eType = eDynAnimExprTok_Float;
						pBlock->fFloatValue = atof(buff);
					}
					else {
						assert(0);
					}
					eaPush(peaBlocks, pBlock);
					buff[0] = '\0';
					buffLen = 0;

					if (eNext != eDynAnimExprTok_WhiteSpace) {
						pBlock = calloc(1,sizeof(DynAnimExpressionBlock));
						pBlock->eType = eNext;
						eaPush(peaBlocks, pBlock);
					}
				}
			}
		}
		else
		{
			switch (eNext)
			{
				xcase eDynAnimExprTok_String :
				{
					bChr = true;
					buff[buffLen] = pcExpression[i];
					buff[++buffLen] = '\0';
				}
				xcase eDynAnimExprTok_Int :
				{
					bInt = true;
					buff[buffLen] = pcExpression[i];
					buff[++buffLen] = '\0';
				}
				xcase eDynAnimExprTok_WhiteSpace :
				{
					;
				}
				xdefault :
				{
					DynAnimExpressionBlock *pBlock = calloc(1,sizeof(DynAnimExpressionBlock));
					pBlock->eType = eNext;
					eaPush(peaBlocks, pBlock);
				}
			}
		}
		i++;
	}

	if (buffLen)
	{
		DynAnimExpressionBlock *pBlock = calloc(1,sizeof(DynAnimExpressionBlock));
		if (TRUE_THEN_RESET(bChr)) {
			pBlock->eType = eDynAnimExprTok_String;
			pBlock->pcName = allocAddString(buff);
		}
		else if (TRUE_THEN_RESET(bInt)) {
			pBlock->eType = eDynAnimExprTok_Int;
			pBlock->iIntValue = atoi(buff);
		}
		else if (TRUE_THEN_RESET(bFlt)) {
			pBlock->eType = eDynAnimExprTok_Float;
			pBlock->fFloatValue = atof(buff);
		}
		else {
			assert(0);
		}
		eaPush(peaBlocks, pBlock);
		buff[0] = '\0';
		buffLen = 0;
	}
}

// +-----------------+
// | Parser Routines |
// +-----------------+

//note: I've written negation so it'll only be usable once per-fact/item, not recursively

// statement -> var = add
// add -> multiply add_rest
// add_rest -> + multiply add_rest | - multiply add_rest | NULL
// multiply -> fact multiply_rest
// multiply_rest -> * fact multiply_rest | / fact multiply_rest | % fact multiply_rest | NULL
// fact -> -posfact | posfact
// posfact -> (add) | positem | <add,add,add> | <add,add,add,add> | {add,add,add,add}
// positem = func | var | val
// func = STR(params) | STR()
// params = add, params | add
// var = STR | STR.STR | STR.STR.STR | STR.STR[NUM] | STR.STR.STR[NUM]
// val = NUM

static void dynAnimExpressionParse_Add		(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut);
static void dynAnimExpressionParse_Multiply	(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut);
static void dynAnimExpressionParse_Fact		(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut);
static void dynAnimExpressionParse_PosFact	(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut);
static void dynAnimExpressionParse_PosItem	(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut);
static void dynAnimExpressionParse_Func		(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut);
static void dynAnimExpressionParse_Params	(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut);
static void dynAnimExpressionParse_Var		(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut);
static void dynAnimExpressionParse_Val		(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut);
static eDynAnimExpressionToken dynAnimExpressionParse_Peek	(DynAnimExpressionBlock ***peaBlocks);
static eDynAnimExpressionToken dynAnimExpressionParse_Peek2	(DynAnimExpressionBlock ***peaBlocks);
static DynAnimExpressionBlock* dynAnimExpressionParse_Eat	(DynAnimExpressionBlock ***peaBlocks, eDynAnimExpressionToken eType);

static eDynAnimExpressionToken dynAnimExpressionParse_Peek(DynAnimExpressionBlock ***peaBlocks)
{
	if(eaSize(peaBlocks))
		return (*peaBlocks)[0]->eType;
	else
		return -1;
}

static eDynAnimExpressionToken dynAnimExpressionParse_Peek2(DynAnimExpressionBlock ***peaBlocks)
{
	if(eaSize(peaBlocks) >= 2)
		return (*peaBlocks)[1]->eType;
	else
		return -1;
}

static DynAnimExpressionBlock* dynAnimExpressionParse_Eat(DynAnimExpressionBlock ***peaBlocks, eDynAnimExpressionToken eType)
{
	if(dynAnimExpressionParse_Peek(peaBlocks) == eType)
		return (DynAnimExpressionBlock*)eaRemove(peaBlocks,0);
	else
		assert(0);
}

static void dynAnimExpressionParse_Val(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut)
{
	if (dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_Int)
	{
		DynAnimExpressionBlock *pBlock = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Int);
		eaPush(peaBlocksOut, pBlock);
	}
	else
	{
		DynAnimExpressionBlock *pBlock = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Float);
		eaPush(peaBlocksOut, pBlock);
	}
}

static void dynAnimExpressionParse_Var(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut)
{
	DynAnimExpressionBlock *pBlockVar, *pBlockExtra;
	DynAnimExpressionBlock *pBlockComp  = NULL;
	DynAnimExpressionBlock *pBlockComp2 = NULL;
	DynAnimExpressionBlock *pBlockAcc = NULL;
	
	pBlockVar = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_String);
	if (dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_Dot) {
		DynAnimExpressionBlock *pBlockDot;
		pBlockDot = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Dot);
		pBlockComp = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_String);
		free(pBlockDot);
		if (dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_LBracket) {
			DynAnimExpressionBlock *pBlockLac, *pBlockRac;
			pBlockLac = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_LBracket);
			pBlockAcc = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Int);
			pBlockRac = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_RBracket);
			free(pBlockLac);
			free(pBlockRac);
		} else if (dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_Dot) {
			DynAnimExpressionBlock *pBlockDot2;
			pBlockDot2 = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Dot);
			pBlockComp2 = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_String);
			free(pBlockDot2);
			if (dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_LBracket) {
				DynAnimExpressionBlock *pBlockLac, *pBlockRac;
				pBlockLac = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_LBracket);
				pBlockAcc = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Int);
				pBlockRac = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_RBracket);
				free(pBlockLac);
				free(pBlockRac);
			}
		}
	}

	if (pBlockVar->pcName == s_pcSystem)
	{
		assert(	!pBlockAcc &&
				pBlockComp &&
				(	pBlockComp->pcName == s_pcTime ||
					pBlockComp->pcName == s_pcTimestep));
	}

	if (pBlockAcc) eaPush(peaBlocksOut, pBlockAcc);
	if (pBlockComp2) eaPush(peaBlocksOut, pBlockComp2);
	if (pBlockComp) eaPush(peaBlocksOut, pBlockComp);
	eaPush(peaBlocksOut, pBlockVar);
	
	pBlockExtra = calloc(1,sizeof(DynAnimExpressionBlock));
	if (pBlockComp) {
		if (pBlockAcc) {
			pBlockExtra->eType = pBlockComp2 ? eDynAnimExprTok_VarWithTwoComponentsAndAccessor : eDynAnimExprTok_VarWithComponentAndAccessor;
		} else {
			pBlockExtra->eType = pBlockComp2 ? eDynAnimExprTok_VarWithTwoComponents : eDynAnimExprTok_VarWithComponent;
		}
	} else {
		pBlockExtra->eType = eDynAnimExprTok_Var;
		assert(0);
	}

	eaPush(peaBlocksOut, pBlockExtra);
}

static void dynAnimExpressionParse_Params(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut)
{
	dynAnimExpressionParse_Add(peaBlocksIn, peaBlocksOut);

	if (dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_Comma) {
		DynAnimExpressionBlock *pBlockComma = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Comma);
		free(pBlockComma);
		dynAnimExpressionParse_Params(peaBlocksIn, peaBlocksOut);
	}
}

static void dynAnimExpressionParse_Func(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut)
{
	DynAnimExpressionBlock *pBlockFunc, *pBlockExtra;
	DynAnimExpressionBlock *pBlockLParen;
	DynAnimExpressionBlock *pBlockRParen;

	pBlockFunc = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_String);
	pBlockLParen = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_LParen);
	if (dynAnimExpressionParse_Peek(peaBlocksIn) != eDynAnimExprTok_RParen) 	{
		dynAnimExpressionParse_Params(peaBlocksIn, peaBlocksOut);
	}
	pBlockRParen = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_RParen);

	free(pBlockLParen);
	free(pBlockRParen);

	eaPush(peaBlocksOut, pBlockFunc);

	pBlockExtra = calloc(1,sizeof(DynAnimExpressionBlock));
	pBlockExtra->eType = eDynAnimExprTok_Function;
	eaPush(peaBlocksOut, pBlockExtra);
}

static void dynAnimExpressionParse_PosItem(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut)
{
	if (dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_String) {
		if (dynAnimExpressionParse_Peek2(peaBlocksIn) == eDynAnimExprTok_LParen) {
			dynAnimExpressionParse_Func(peaBlocksIn, peaBlocksOut);
		} else {
			dynAnimExpressionParse_Var(peaBlocksIn, peaBlocksOut);
		}
	} else {
		dynAnimExpressionParse_Val(peaBlocksIn, peaBlocksOut);
	}
}

static void dynAnimExpressionParse_PosFact(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut)
{
	if (dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_LParen)
	{
		DynAnimExpressionBlock *pBlockLpr, *pBlockRpr;

		pBlockLpr = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_LParen);
		dynAnimExpressionParse_Add(peaBlocksIn, peaBlocksOut);
		pBlockRpr = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_RParen);

		free(pBlockLpr);
		free(pBlockRpr);
	}
	else if (dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_LChevron)
	{
		DynAnimExpressionBlock *pBlockVec;
		DynAnimExpressionBlock *pBlockLvc, *pBlockRvc;
		DynAnimExpressionBlock *pBlockComma1, *pBlockComma2, *pBlockComma3;
		bool bVec4 = false;

		pBlockLvc = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_LChevron);
		dynAnimExpressionParse_Add(peaBlocksIn, peaBlocksOut);
		pBlockComma1 = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Comma);
		dynAnimExpressionParse_Add(peaBlocksIn, peaBlocksOut);
		pBlockComma2 = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Comma);
		dynAnimExpressionParse_Add(peaBlocksIn, peaBlocksOut);		
		if (dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_Comma) {
			bVec4 = true;
			pBlockComma3 = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Comma);
			dynAnimExpressionParse_Add(peaBlocksIn, peaBlocksOut);
			free(pBlockComma3);
		}
		pBlockRvc = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_RChevron);

		free(pBlockLvc);
		free(pBlockComma1);
		free(pBlockComma2);
		free(pBlockRvc);

		pBlockVec = calloc(1,sizeof(DynAnimExpressionBlock));
		pBlockVec->eType = bVec4 ? eDynAnimExprTok_Vec4 : eDynAnimExprTok_Vec3;
		eaPush(peaBlocksOut, pBlockVec);
	}
	else if (dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_LCurley)
	{
		DynAnimExpressionBlock *pBlockQuat;
		DynAnimExpressionBlock *pBlockLCurley, *pBlockRCurley;
		DynAnimExpressionBlock *pBlockComma1, *pBlockComma2, *pBlockComma3;

		pBlockLCurley = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_LCurley);
		dynAnimExpressionParse_Add(peaBlocksIn, peaBlocksOut);
		pBlockComma1 = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Comma);
		dynAnimExpressionParse_Add(peaBlocksIn, peaBlocksOut);
		pBlockComma2 = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Comma);
		dynAnimExpressionParse_Add(peaBlocksIn, peaBlocksOut);
		pBlockComma3 = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Comma);
		dynAnimExpressionParse_Add(peaBlocksIn, peaBlocksOut);
		pBlockRCurley = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_RCurley);

		free(pBlockLCurley);
		free(pBlockComma1);
		free(pBlockComma2);
		free(pBlockComma3);
		free(pBlockRCurley);
		
		pBlockQuat = calloc(1,sizeof(DynAnimExpressionBlock));
		pBlockQuat->eType = eDynAnimExprTok_Quat;
		eaPush(peaBlocksOut, pBlockQuat);
	}
	else
	{
		dynAnimExpressionParse_PosItem(peaBlocksIn, peaBlocksOut);
	}
}

static void dynAnimExpressionParse_Fact(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut)
{
	DynAnimExpressionBlock *pBlockSub = NULL;

	if (dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_Sub) {
		pBlockSub = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Sub);
		pBlockSub->eType = eDynAnimExprTok_Neg;
	}

	dynAnimExpressionParse_PosFact(peaBlocksIn, peaBlocksOut);

	if (pBlockSub) {
		eaPush(peaBlocksOut, pBlockSub);
	}
}

static void dynAnimExpressionParse_Multiply(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut)
{
	dynAnimExpressionParse_Fact(peaBlocksIn, peaBlocksOut);
	while(1)
	{
		if(dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_Mul)
		{
			DynAnimExpressionBlock *pBlock = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Mul);
			dynAnimExpressionParse_Fact(peaBlocksIn, peaBlocksOut);
			eaPush(peaBlocksOut, pBlock);
		}
		else if(dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_Div)
		{
			DynAnimExpressionBlock *pBlock = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Div);
			dynAnimExpressionParse_Fact(peaBlocksIn, peaBlocksOut);
			eaPush(peaBlocksOut, pBlock);
		}
		else if (dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_Mod)
		{
			DynAnimExpressionBlock *pBlock = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Mod);
			dynAnimExpressionParse_Fact(peaBlocksIn, peaBlocksOut);
			eaPush(peaBlocksOut, pBlock);
		}
		else
		{
			break;
		}
	}
}

static void dynAnimExpressionParse_Add(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksOut)
{
	dynAnimExpressionParse_Multiply(peaBlocksIn, peaBlocksOut);
	while(1)
	{
		if(dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_Add)
		{
			DynAnimExpressionBlock *pBlock = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Add);
			dynAnimExpressionParse_Multiply(peaBlocksIn, peaBlocksOut);
			eaPush(peaBlocksOut, pBlock);
		}
		else if(dynAnimExpressionParse_Peek(peaBlocksIn) == eDynAnimExprTok_Sub)
		{
			DynAnimExpressionBlock *pBlock = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Sub);
			dynAnimExpressionParse_Multiply(peaBlocksIn, peaBlocksOut);
			eaPush(peaBlocksOut, pBlock);
		}
		else
		{
			break;
		}
	}
}

static void dynAnimExpressionParse(DynAnimExpressionBlock ***peaBlocksIn, DynAnimExpressionBlock ***peaBlocksTarget, DynAnimExpressionBlock ***peaBlocksCode)
{
	DynAnimExpressionBlock *pBlock;

	dynAnimExpressionParse_Var(peaBlocksIn, peaBlocksTarget);
	pBlock = dynAnimExpressionParse_Eat(peaBlocksIn, eDynAnimExprTok_Assign);
	dynAnimExpressionParse_Add(peaBlocksIn, peaBlocksCode);

	free(pBlock);
}

static void dynAnimExpressionDebugTokens(const char *pcExpression, DynAnimExpressionBlock ***peaBlocks);
static void dynAnimExpressionDebugParse(const char *pcExpression, DynAnimExpressionBlock ***peaBlocksTarget, DynAnimExpressionBlock ***peaBlocksCode);

static bool dynAnimExpressionSetBuildBlocks(DynAnimExpressionSet *pSet)
{
	if (uiDebugDynAnimExpression)
		printfColor(COLOR_BLUE|COLOR_BRIGHT,"\nDynAnimExpression TEXT Debug for %s:\n",pSet->pcName);

	FOR_EACH_IN_EARRAY(pSet->eaDynAnimExpression, DynAnimExpression, pData)
	{
		if (pData->pcExpression)
		{
			DynAnimExpressionBlock **eaBlocks = NULL;
			assert(pData->eaBlocksTarget == NULL && pData->eaBlocksCode == NULL);
			eaCreate(&eaBlocks);
			eaCreate(&pData->eaBlocksTarget);
			eaCreate(&pData->eaBlocksCode);

			dynAnimExpressionTokenize(pData->pcExpression, &eaBlocks);
			if (uiDebugDynAnimExpression) dynAnimExpressionDebugTokens(pData->pcExpression, &eaBlocks);

			dynAnimExpressionParse(&eaBlocks, &pData->eaBlocksTarget, &pData->eaBlocksCode);
			if (uiDebugDynAnimExpression) dynAnimExpressionDebugParse(pData->pcExpression, &pData->eaBlocksTarget, &pData->eaBlocksCode);

			assert(!eaSize(&eaBlocks));
			eaDestroyEx(&eaBlocks,NULL);
		}
	}
	FOR_EACH_END;

	return true;
}

// +----------------+
// |                |
// | Debug Routines |
// |                |
// +----------------+

void dynAnimExpressionDebugNode(DynNode *pNode)
{
	if (uiDebugDynAnimExpression)
	{
		Vec3 vPos, vScl;
		Quat qRot;

		//local space data
		dynNodeGetLocalPos(pNode, vPos);
		dynNodeGetLocalScale(pNode, vScl);
		dynNodeGetLocalRot(pNode, qRot);
		printfColor(COLOR_BLUE, "LS pos: %f %f %f\n", vecParamsXYZ(vPos));
		printfColor(COLOR_BLUE, "LS scl: %f %f %f\n", vecParamsXYZ(vScl));
		printfColor(COLOR_BLUE, "LS rot: %f %f %f %f\n", quatParamsXYZW(qRot));

		//world space data
		dynNodeGetWorldSpacePos(pNode, vPos);
		dynNodeGetWorldSpaceScale(pNode, vScl);
		dynNodeGetWorldSpaceRot(pNode, qRot);
		printfColor(COLOR_BLUE, "WS pos: %f %f %f\n", vecParamsXYZ(vPos));
		printfColor(COLOR_BLUE, "WS scl: %f %f %f\n", vecParamsXYZ(vScl));
		printfColor(COLOR_BLUE, "WS rot: %f %f %f %f\n", quatParamsXYZW(qRot));
	}
}

static void dynAnimExpressionDebugBlock(DynAnimExpressionBlock *pBlock)
{
	switch (pBlock->eType)
	{
		xcase eDynAnimExprTok_Assign	: printfColor(COLOR_BLUE, "=");

		xcase eDynAnimExprTok_Neg		: printfColor(COLOR_BLUE, "(-)");

		xcase eDynAnimExprTok_Add		: printfColor(COLOR_BLUE, "+");
		xcase eDynAnimExprTok_Sub		: printfColor(COLOR_BLUE, "-");
		xcase eDynAnimExprTok_Mul		: printfColor(COLOR_BLUE, "*");
		xcase eDynAnimExprTok_Div		: printfColor(COLOR_BLUE, "/");
		xcase eDynAnimExprTok_Mod		: printfColor(COLOR_BLUE, "%%");

		xcase eDynAnimExprTok_String	: printfColor(COLOR_BLUE, "%s", pBlock->pcName);
		xcase eDynAnimExprTok_Float		: printfColor(COLOR_BLUE, "%ff", pBlock->fFloatValue);
		xcase eDynAnimExprTok_Int		: printfColor(COLOR_BLUE, "%di", pBlock->iIntValue);

		xcase eDynAnimExprTok_Function	: printfColor(COLOR_BLUE, "F:");

		xcase eDynAnimExprTok_Var		: printfColor(COLOR_BLUE, "V:");
		xcase eDynAnimExprTok_VarWithComponent : printfColor(COLOR_BLUE, "V.C:");
		xcase eDynAnimExprTok_VarWithTwoComponents : printfColor(COLOR_BLUE, "V.C.C:");
		xcase eDynAnimExprTok_VarWithComponentAndAccessor : printfColor(COLOR_BLUE, "V.C[A]:");
		xcase eDynAnimExprTok_VarWithTwoComponentsAndAccessor : printfColor(COLOR_BLUE, "V.C.C[A]:");

		xcase eDynAnimExprTok_Vec3		: printfColor(COLOR_BLUE, "<3>");
		xcase eDynAnimExprTok_Vec3RT	: printfColor(COLOR_BLUE, "<3:%f,%f,%f>", vecParamsXYZ(pBlock->vVecValue));
		xcase eDynAnimExprTok_Vec4		: printfColor(COLOR_BLUE, "<4>");
		xcase eDynAnimExprTok_Vec4RT	: printfColor(COLOR_BLUE, "<4:%f,%f,%f,%f>", quatParamsXYZW(pBlock->vVecValue));
		xcase eDynAnimExprTok_Quat		: printfColor(COLOR_BLUE, "{4}");
		xcase eDynAnimExprTok_QuatRT	: printfColor(COLOR_BLUE, "{4:%f,%f,%f,%f}", quatParamsXYZW(pBlock->vVecValue));
		
		xcase eDynAnimExprTok_LParen	: printfColor(COLOR_BLUE, "(");
		xcase eDynAnimExprTok_RParen	: printfColor(COLOR_BLUE, ")");
		xcase eDynAnimExprTok_LBracket	: printfColor(COLOR_BLUE, "[");
		xcase eDynAnimExprTok_RBracket	: printfColor(COLOR_BLUE, "]");
		xcase eDynAnimExprTok_LChevron	: printfColor(COLOR_BLUE, "<");
		xcase eDynAnimExprTok_RChevron	: printfColor(COLOR_BLUE, ">");

		xcase eDynAnimExprTok_Comma		: printfColor(COLOR_BLUE, ",");
		xcase eDynAnimExprTok_Dot		: printfColor(COLOR_BLUE, ".");
		xcase eDynAnimExprTok_WhiteSpace: printfColor(COLOR_BLUE, "_WS_");

		xcase eDynAnimExprTok_Other	: printfColor(COLOR_RED, "BAD");

		xdefault: assert(0);
	}
}

static void dynAnimExpressionDebugTokens(const char *pcExpression, DynAnimExpressionBlock ***peaBlocks)
{
	printfColor(COLOR_BLUE|COLOR_BRIGHT, "%s : ", pcExpression);
	FOR_EACH_IN_EARRAY_FORWARDS(*peaBlocks, DynAnimExpressionBlock, pBlock)
	{
		dynAnimExpressionDebugBlock(pBlock);
	}
	FOR_EACH_END;
	printf("\n");
}

static void dynAnimExpressionDebugParse(const char *pcExpression, DynAnimExpressionBlock ***peaBlocksTarget, DynAnimExpressionBlock ***peaBlocksCode)
{
	printfColor(COLOR_BLUE|COLOR_BRIGHT, "%s : ", pcExpression);
	FOR_EACH_IN_EARRAY(*peaBlocksTarget, DynAnimExpressionBlock, pBlockTarget)
	{
		dynAnimExpressionDebugBlock(pBlockTarget);
	}
	FOR_EACH_END;
	printfColor(COLOR_BLUE, "=");
	FOR_EACH_IN_EARRAY(*peaBlocksCode, DynAnimExpressionBlock, pBlockCode)
	{
		dynAnimExpressionDebugBlock(pBlockCode);
	}
	FOR_EACH_END;
	printf("\n");
}

// +---------------+
// |               |
// | Load Routines |
// |               |
// +---------------+

static bool dynAnimExpressionVerify(DynAnimExpressionSet *pSet, DynAnimExpression* pData)
{
	bool bRet = true;

	if (!pData->pcExpression) {
		ErrorFilenamef(pSet->pcFileName, "Found an empty expression!\n");
	}

	return bRet;
}

static bool dynAnimExpressionSetVerify(DynAnimExpressionSet *pSet)
{
	bool bRet = true;
	FOR_EACH_IN_EARRAY(pSet->eaDynAnimExpression, DynAnimExpression, pData) {
		bRet = bRet && dynAnimExpressionVerify(pSet, pData);
	} FOR_EACH_END;
	return bRet;
}

static bool dynAnimExpressionSetFixup(DynAnimExpressionSet* pSet)
{
	char cName[256];
	getFileNameNoExt(cName, pSet->pcFileName);
	pSet->pcName = allocAddString(cName);

	return true;
}

static void dynAnimExpressionSetReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath)) {
		; // File was deleted, do we care here?
	}

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if(!ParserReloadFileToDictionary(relpath,hAnimExpressionSetDict)) {
		CharacterFileError(relpath, "Error reloading DynAnimExpressionSet file: %s", relpath);
	}
}

AUTO_FIXUPFUNC;
TextParserResult fixupDynAnimExpressionSet(DynAnimExpressionSet* pSet, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
			if (!dynAnimExpressionSetVerify(pSet) || !dynAnimExpressionSetFixup(pSet) || !dynAnimExpressionSetBuildBlocks(pSet) )
				return PARSERESULT_INVALID; // remove entry
		xcase FIXUPTYPE_POST_BIN_READ:
			if (!dynAnimExpressionSetFixup(pSet))
				return PARSERESULT_INVALID; // remove entry
			else if (uiDebugDynAnimExpression) {
				printfColor(COLOR_BLUE|COLOR_BRIGHT,"\nDynAnimExpression BIN Debug for %s:\n",pSet->pcName);
				FOR_EACH_IN_EARRAY(pSet->eaDynAnimExpression, DynAnimExpression, pExpr) {
					dynAnimExpressionDebugParse(pExpr->pcExpression, &pExpr->eaBlocksTarget, &pExpr->eaBlocksCode);
				} FOR_EACH_END;
			}
	}
	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void registerDynAnimExpressionSetDict(void)
{
	hAnimExpressionSetDict = RefSystem_RegisterSelfDefiningDictionary("DynAnimExpressionSet", false, parse_DynAnimExpressionSet, true, false, NULL);
}

void dynAnimExpressionSetLoadAll(void)
{
	loadstart_printf("Loading DynAnimExpressionSet...");
	ParserLoadFilesToDictionary("dyn/AnimExpression", ".animexpr", "DynAnimExpression.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, hAnimExpressionSetDict);
	if(isDevelopmentMode()) {
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/AnimExpression/*.animexpr", dynAnimExpressionSetReloadCallback);
	}
	loadend_printf("done (%d DynAnimExpressionSets)", RefSystem_GetDictionaryNumberOfReferents(hAnimExpressionSetDict));
}

// +------------------+
// |                  |
// | Compute Routines |
// |                  |
// +------------------+

static void dynAnimExpressionCompute(DynSkeleton *pSkeleton, F32 fDeltaTime, DynAnimExpressionBlock **eaBlocks, U32 *i, DynAnimExpressionBlock *pValue);
static const DynTransform *dynAnimExpressionFind_LastFrameTransformWS(DynSkeleton *pSkeleton, const char *pcNodeName);

static void dynAnimExpressionCompute_Operation_IntInt(	eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Int;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add : pResult->iIntValue = pArg1->iIntValue + pArg2->iIntValue;
		xcase eDynAnimExprTok_Sub : pResult->iIntValue = pArg1->iIntValue - pArg2->iIntValue;
		xcase eDynAnimExprTok_Mul : pResult->iIntValue = pArg1->iIntValue * pArg2->iIntValue;
		xcase eDynAnimExprTok_Div : pResult->iIntValue = pArg1->iIntValue / pArg2->iIntValue;
		xcase eDynAnimExprTok_Mod : pResult->iIntValue = pArg1->iIntValue % pArg2->iIntValue;
		xdefault : assert(0);
	}
}

static void dynAnimExpressionCompute_Operation_IntFloat(eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Float;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add : pResult->fFloatValue = pArg1->iIntValue + pArg2->fFloatValue;
		xcase eDynAnimExprTok_Sub : pResult->fFloatValue = pArg1->iIntValue - pArg2->fFloatValue;
		xcase eDynAnimExprTok_Mul : pResult->fFloatValue = pArg1->iIntValue * pArg2->fFloatValue;
		xcase eDynAnimExprTok_Div : pResult->fFloatValue = pArg1->iIntValue / pArg2->fFloatValue;
		xcase eDynAnimExprTok_Mod : pResult->fFloatValue = fmod(pArg1->iIntValue, pArg2->fFloatValue);
		xdefault : assert(0);
	}
}

static void dynAnimExpressionCompute_Operation_IntVec3(	eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Vec3RT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->iIntValue + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->iIntValue + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->iIntValue + pArg2->vVecValue[2];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->iIntValue - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->iIntValue - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->iIntValue - pArg2->vVecValue[2];
		}
		xcase eDynAnimExprTok_Mul :
		{
			pResult->vVecValue[0] = pArg1->iIntValue * pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->iIntValue * pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->iIntValue * pArg2->vVecValue[2];
		}
		xcase eDynAnimExprTok_Div :
		{
			pResult->vVecValue[0] = pArg1->iIntValue / pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->iIntValue / pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->iIntValue / pArg2->vVecValue[2];
		}
		xcase eDynAnimExprTok_Mod :
		{
			pResult->vVecValue[0] = fmod(pArg1->iIntValue, pArg2->vVecValue[0]);
			pResult->vVecValue[1] = fmod(pArg1->iIntValue, pArg2->vVecValue[1]);
			pResult->vVecValue[2] = fmod(pArg1->iIntValue, pArg2->vVecValue[2]);
		}
		xdefault :
		{
			assert(0);
		}
	}
}

static void dynAnimExpressionCompute_Operation_IntVec4(	eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Vec4RT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->iIntValue + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->iIntValue + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->iIntValue + pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->iIntValue + pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->iIntValue - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->iIntValue - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->iIntValue - pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->iIntValue - pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mul :
		{
			pResult->vVecValue[0] = pArg1->iIntValue * pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->iIntValue * pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->iIntValue * pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->iIntValue * pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Div :
		{
			pResult->vVecValue[0] = pArg1->iIntValue / pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->iIntValue / pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->iIntValue / pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->iIntValue / pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mod :
		{
			pResult->vVecValue[0] = fmod(pArg1->iIntValue, pArg2->vVecValue[0]);
			pResult->vVecValue[1] = fmod(pArg1->iIntValue, pArg2->vVecValue[1]);
			pResult->vVecValue[2] = fmod(pArg1->iIntValue, pArg2->vVecValue[2]);
			pResult->vVecValue[3] = fmod(pArg1->iIntValue, pArg2->vVecValue[3]);
		}
		xdefault :
		{
			assert(0);
		}
	}
}

static void dynAnimExpressionCompute_Operation_IntQuat(	eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
//pArg1 = (bi, cj, dk, a)	
#define qA pArg1->iIntValue
#define qB pArg1->iIntValue
#define qC pArg1->iIntValue
#define qD pArg1->iIntValue

//pArg2 = (fi, gj, hk, e)
#define qE pArg2->vVecValue[quatWidx]
#define qF pArg2->vVecValue[quatXidx]
#define qG pArg2->vVecValue[quatYidx]
#define qH pArg2->vVecValue[quatZidx]

	pResult->eType = eDynAnimExprTok_QuatRT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->iIntValue + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->iIntValue + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->iIntValue + pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->iIntValue + pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->iIntValue - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->iIntValue - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->iIntValue - pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->iIntValue - pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mul :
		{
			//   (a*e - b*f - c*g - d*h) +
			// i (b*e + a*f + c*h - d*g) +
			// j (a*g - b*h + c*e + d*f) +
			// k (a*h + b*g - c*f + d*e)

			pResult->vVecValue[quatWidx] = qA*qE - qB*qF - qC*qG - qD*qH;
			pResult->vVecValue[quatXidx] = qB*qE + qA*qF + qC*qH - qD*qG;
			pResult->vVecValue[quatYidx] = qA*qG - qB*qH + qC*qE + qD*qF;
			pResult->vVecValue[quatXidx] = qA*qH + qB*qG - qC*qF + qD*qE;
		}
		xcase eDynAnimExprTok_Div :
		acase eDynAnimExprTok_Mod :
		{
			assert(0); // undefined
		}
		xdefault :
		{
			assert(0);
		}
	}

#undef qH
#undef qG
#undef qF
#undef qE

#undef qD
#undef qC
#undef qB
#undef qA
}

static void dynAnimExpressionCompute_Operation_FloatInt(eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Float;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add : pResult->fFloatValue = pArg1->fFloatValue + pArg2->iIntValue;
		xcase eDynAnimExprTok_Sub : pResult->fFloatValue = pArg1->fFloatValue - pArg2->iIntValue;
		xcase eDynAnimExprTok_Mul : pResult->fFloatValue = pArg1->fFloatValue * pArg2->iIntValue;
		xcase eDynAnimExprTok_Div : pResult->fFloatValue = pArg1->fFloatValue / pArg2->iIntValue;
		xcase eDynAnimExprTok_Mod : pResult->fFloatValue = fmod(pArg1->fFloatValue, pArg2->iIntValue);
		xdefault : assert(0);
	}
}

static void dynAnimExpressionCompute_Operation_FloatFloat(	eDynAnimExpressionToken operation,
															DynAnimExpressionBlock *pArg1,
															DynAnimExpressionBlock *pArg2,
															DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Float;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add : pResult->fFloatValue = pArg1->fFloatValue + pArg2->fFloatValue;
		xcase eDynAnimExprTok_Sub : pResult->fFloatValue = pArg1->fFloatValue - pArg2->fFloatValue;
		xcase eDynAnimExprTok_Mul : pResult->fFloatValue = pArg1->fFloatValue * pArg2->fFloatValue;
		xcase eDynAnimExprTok_Div : pResult->fFloatValue = pArg1->fFloatValue / pArg2->fFloatValue;
		xcase eDynAnimExprTok_Mod : pResult->fFloatValue = fmod(pArg1->fFloatValue, pArg2->fFloatValue);
		xdefault : assert(0);
	}
}

static void dynAnimExpressionCompute_Operation_FloatVec3(	eDynAnimExpressionToken operation,
															DynAnimExpressionBlock *pArg1,
															DynAnimExpressionBlock *pArg2,
															DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Vec3RT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->fFloatValue + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->fFloatValue + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->fFloatValue + pArg2->vVecValue[2];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->fFloatValue - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->fFloatValue - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->fFloatValue - pArg2->vVecValue[2];
		}
		xcase eDynAnimExprTok_Mul :
		{
			pResult->vVecValue[0] = pArg1->fFloatValue * pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->fFloatValue * pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->fFloatValue * pArg2->vVecValue[2];
		}
		xcase eDynAnimExprTok_Div :
		{
			pResult->vVecValue[0] = pArg1->fFloatValue / pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->fFloatValue / pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->fFloatValue / pArg2->vVecValue[2];
		}
		xcase eDynAnimExprTok_Mod :
		{
			pResult->vVecValue[0] = fmod(pArg1->fFloatValue, pArg2->vVecValue[0]);
			pResult->vVecValue[1] = fmod(pArg1->fFloatValue, pArg2->vVecValue[1]);
			pResult->vVecValue[2] = fmod(pArg1->fFloatValue, pArg2->vVecValue[2]);
		}
		xdefault :
		{
			assert(0);
		}
	}
}

static void dynAnimExpressionCompute_Operation_FloatVec4(	eDynAnimExpressionToken operation,
															DynAnimExpressionBlock *pArg1,
															DynAnimExpressionBlock *pArg2,
															DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Vec4RT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->fFloatValue + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->fFloatValue + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->fFloatValue + pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->fFloatValue + pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->fFloatValue - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->fFloatValue - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->fFloatValue - pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->fFloatValue - pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mul :
		{
			pResult->vVecValue[0] = pArg1->fFloatValue * pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->fFloatValue * pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->fFloatValue * pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->fFloatValue * pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Div :
		{
			pResult->vVecValue[0] = pArg1->fFloatValue / pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->fFloatValue / pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->fFloatValue / pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->fFloatValue / pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mod:
		{
			pResult->vVecValue[0] = fmod(pArg1->fFloatValue, pArg2->vVecValue[0]);
			pResult->vVecValue[1] = fmod(pArg1->fFloatValue, pArg2->vVecValue[1]);
			pResult->vVecValue[2] = fmod(pArg1->fFloatValue, pArg2->vVecValue[2]);
			pResult->vVecValue[3] = fmod(pArg1->fFloatValue, pArg2->vVecValue[3]);
		}
		xdefault :
		{
			assert(0);
		}
	}
}

static void dynAnimExpressionCompute_Operation_FloatQuat(	eDynAnimExpressionToken operation,
															DynAnimExpressionBlock *pArg1,
															DynAnimExpressionBlock *pArg2,
															DynAnimExpressionBlock *pResult )
{
//pArg1 = (bi, cj, dk, a)	
#define qA pArg1->fFloatValue
#define qB pArg1->fFloatValue
#define qC pArg1->fFloatValue
#define qD pArg1->fFloatValue

//pArg2 = (fi, gj, hk, e)
#define qE pArg2->vVecValue[quatWidx]
#define qF pArg2->vVecValue[quatXidx]
#define qG pArg2->vVecValue[quatYidx]
#define qH pArg2->vVecValue[quatZidx]

	pResult->eType = eDynAnimExprTok_QuatRT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->fFloatValue + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->fFloatValue + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->fFloatValue + pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->fFloatValue + pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->fFloatValue - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->fFloatValue - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->fFloatValue - pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->fFloatValue - pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mul :
		{
			//   (a*e - b*f - c*g - d*h) +
			// i (b*e + a*f + c*h - d*g) +
			// j (a*g - b*h + c*e + d*f) +
			// k (a*h + b*g - c*f + d*e)

			pResult->vVecValue[quatWidx] = qA*qE - qB*qF - qC*qG - qD*qH;
			pResult->vVecValue[quatXidx] = qB*qE + qA*qF + qC*qH - qD*qG;
			pResult->vVecValue[quatYidx] = qA*qG - qB*qH + qC*qE + qD*qF;
			pResult->vVecValue[quatZidx] = qA*qH + qB*qG - qC*qF + qD*qE;
		}
		xcase eDynAnimExprTok_Div :
		acase eDynAnimExprTok_Mod :
		{
			assert(0); // undefined
		}
		xdefault :
		{
			assert(0);
		}
	}

#undef qH
#undef qG
#undef qF
#undef qE

#undef qD
#undef qC
#undef qB
#undef qA
}

static void dynAnimExpressionCompute_Operation_Vec3Int(	eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Vec3RT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->iIntValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->iIntValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->iIntValue;
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->iIntValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->iIntValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->iIntValue;
		}
		xcase eDynAnimExprTok_Mul :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] * pArg2->iIntValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] * pArg2->iIntValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] * pArg2->iIntValue;
		}
		xcase eDynAnimExprTok_Div :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] / pArg2->iIntValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] / pArg2->iIntValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] / pArg2->iIntValue;
		}
		xcase eDynAnimExprTok_Mod :
		{
			pResult->vVecValue[0] = fmod(pArg1->vVecValue[0], pArg2->iIntValue);
			pResult->vVecValue[1] = fmod(pArg1->vVecValue[1], pArg2->iIntValue);
			pResult->vVecValue[2] = fmod(pArg1->vVecValue[2], pArg2->iIntValue);
		}
		xdefault :
		{
			assert(0);
		}
	}
}

static void dynAnimExpressionCompute_Operation_Vec3Float(	eDynAnimExpressionToken operation,
															DynAnimExpressionBlock *pArg1,
															DynAnimExpressionBlock *pArg2,
															DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Vec3RT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->fFloatValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->fFloatValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->fFloatValue;
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->fFloatValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->fFloatValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->fFloatValue;
		}
		xcase eDynAnimExprTok_Mul :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] * pArg2->fFloatValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] * pArg2->fFloatValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] * pArg2->fFloatValue;
		}
		xcase eDynAnimExprTok_Div :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] / pArg2->fFloatValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] / pArg2->fFloatValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] / pArg2->fFloatValue;
		}
		xcase eDynAnimExprTok_Mod :
		{
			pResult->vVecValue[0] = fmod(pArg1->vVecValue[0], pArg2->fFloatValue);
			pResult->vVecValue[1] = fmod(pArg1->vVecValue[1], pArg2->fFloatValue);
			pResult->vVecValue[2] = fmod(pArg1->vVecValue[2], pArg2->fFloatValue);
		}
		xdefault :
		{
			assert(0);
		}
	}
}

static void dynAnimExpressionCompute_Operation_Vec3Vec3(eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Vec3RT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->vVecValue[2];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->vVecValue[2];
		}
		xcase eDynAnimExprTok_Mul :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] * pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] * pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] * pArg2->vVecValue[2];
		}
		xcase eDynAnimExprTok_Div :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] / pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] / pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] / pArg2->vVecValue[2];
		}
		xcase eDynAnimExprTok_Mod :
		{
			pResult->vVecValue[0] = fmod(pArg1->vVecValue[0], pArg2->vVecValue[0]);
			pResult->vVecValue[1] = fmod(pArg1->vVecValue[1], pArg2->vVecValue[1]);
			pResult->vVecValue[2] = fmod(pArg1->vVecValue[2], pArg2->vVecValue[2]);
		}
		xdefault :
		{
			assert(0);
		}
	}
}

static void dynAnimExpressionCompute_Operation_Vec3Vec4(eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Vec4RT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->vVecValue[2];
			pResult->vVecValue[3] =                       pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->vVecValue[2];
			pResult->vVecValue[3] =                      -pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mul :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] * pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] * pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] * pArg2->vVecValue[2];
			pResult->vVecValue[3] = 0.f;
		}
		xcase eDynAnimExprTok_Div :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] / pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] / pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] / pArg2->vVecValue[2];
			pResult->vVecValue[3] = 0.f;
		}
		xcase eDynAnimExprTok_Mod :
		{
			pResult->vVecValue[0] = fmod(pArg1->vVecValue[0], pArg2->vVecValue[0]);
			pResult->vVecValue[1] = fmod(pArg1->vVecValue[1], pArg2->vVecValue[1]);
			pResult->vVecValue[2] = fmod(pArg1->vVecValue[2], pArg2->vVecValue[2]);
			pResult->vVecValue[3] = 0.f;
		}
		xdefault :
		{
			assert(0);
		}
	}
}

static void dynAnimExpressionCompute_Operation_Vec3Quat(eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
//pArg1 = (bi, cj, dk, a)	
#define qA 0.f
#define qB pArg1->vVecValue[quatXidx]
#define qC pArg1->vVecValue[quatYidx]
#define qD pArg1->vVecValue[quatZidx]

//pArg2 = (fi, gj, hk, e)
#define qE pArg2->vVecValue[quatWidx]
#define qF pArg2->vVecValue[quatXidx]
#define qG pArg2->vVecValue[quatYidx]
#define qH pArg2->vVecValue[quatZidx]

	pResult->eType = eDynAnimExprTok_QuatRT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->vVecValue[2];
			pResult->vVecValue[3] =	                      pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->vVecValue[2];
			pResult->vVecValue[3] =                      -pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mul :
		{
			//   (a*e - b*f - c*g - d*h) +
			// i (b*e + a*f + c*h - d*g) +
			// j (a*g - b*h + c*e + d*f) +
			// k (a*h + b*g - c*f + d*e)

			pResult->vVecValue[quatWidx] = qA*qE - qB*qF - qC*qG - qD*qH;
			pResult->vVecValue[quatXidx] = qB*qE + qA*qF + qC*qH - qD*qG;
			pResult->vVecValue[quatYidx] = qA*qG - qB*qH + qC*qE + qD*qF;
			pResult->vVecValue[quatZidx] = qA*qH + qB*qG - qC*qF + qD*qE;
		}
		xcase eDynAnimExprTok_Div :
		acase eDynAnimExprTok_Mod :
		{
			assert(0); // undefined
		}
		xdefault :
		{
			assert(0);
		}
	}

#undef qH
#undef qG
#undef qF
#undef qE

#undef qD
#undef qC
#undef qB
#undef qA
}

static void dynAnimExpressionCompute_Operation_Vec4Int(	eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Vec4RT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->iIntValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->iIntValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->iIntValue;
			pResult->vVecValue[3] = pArg1->vVecValue[3] + pArg2->iIntValue;
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->iIntValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->iIntValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->iIntValue;
			pResult->vVecValue[3] = pArg1->vVecValue[3] - pArg2->iIntValue;
		}
		xcase eDynAnimExprTok_Mul :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] * pArg2->iIntValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] * pArg2->iIntValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] * pArg2->iIntValue;
			pResult->vVecValue[3] = pArg1->vVecValue[3] * pArg2->iIntValue;
		}
		xcase eDynAnimExprTok_Div :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] / pArg2->iIntValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] / pArg2->iIntValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] / pArg2->iIntValue;
			pResult->vVecValue[3] = pArg1->vVecValue[3] / pArg2->iIntValue;
		}
		xcase eDynAnimExprTok_Mod :
		{
			pResult->vVecValue[0] = fmod(pArg1->vVecValue[0], pArg2->iIntValue);
			pResult->vVecValue[1] = fmod(pArg1->vVecValue[1], pArg2->iIntValue);
			pResult->vVecValue[2] = fmod(pArg1->vVecValue[2], pArg2->iIntValue);
			pResult->vVecValue[3] = fmod(pArg1->vVecValue[3], pArg2->iIntValue);
		}
		xdefault :
		{
			assert(0);
		}
	}
}

static void dynAnimExpressionCompute_Operation_Vec4Float(	eDynAnimExpressionToken operation,
															DynAnimExpressionBlock *pArg1,
															DynAnimExpressionBlock *pArg2,
															DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Vec4RT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->fFloatValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->fFloatValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->fFloatValue;
			pResult->vVecValue[3] = pArg1->vVecValue[3] + pArg2->fFloatValue;
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->fFloatValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->fFloatValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->fFloatValue;
			pResult->vVecValue[3] = pArg1->vVecValue[3] - pArg2->fFloatValue;
		}
		xcase eDynAnimExprTok_Mul :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] * pArg2->fFloatValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] * pArg2->fFloatValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] * pArg2->fFloatValue;
			pResult->vVecValue[3] = pArg1->vVecValue[3] * pArg2->fFloatValue;
		}
		xcase eDynAnimExprTok_Div :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] / pArg2->fFloatValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] / pArg2->fFloatValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] / pArg2->fFloatValue;
			pResult->vVecValue[3] = pArg1->vVecValue[3] / pArg2->fFloatValue;
		}
		xcase eDynAnimExprTok_Mod :
		{
			pResult->vVecValue[0] = fmod(pArg1->vVecValue[0], pArg2->fFloatValue);
			pResult->vVecValue[1] = fmod(pArg1->vVecValue[1], pArg2->fFloatValue);
			pResult->vVecValue[2] = fmod(pArg1->vVecValue[2], pArg2->fFloatValue);
			pResult->vVecValue[3] = fmod(pArg1->vVecValue[3], pArg2->fFloatValue);
		}
		xdefault :
		{
			assert(0);
		}
	}
}

static void dynAnimExpressionCompute_Operation_Vec4Vec3(eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Vec4RT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mul :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] * pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] * pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] * pArg2->vVecValue[2];
			pResult->vVecValue[3] = 0.f;
		}
		xcase eDynAnimExprTok_Div :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] / pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] / pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] / pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mod :
		{
			pResult->vVecValue[0] = fmod(pArg1->vVecValue[0], pArg2->vVecValue[0]);
			pResult->vVecValue[1] = fmod(pArg1->vVecValue[1], pArg2->vVecValue[1]);
			pResult->vVecValue[2] = fmod(pArg1->vVecValue[2], pArg2->vVecValue[2]);
			pResult->vVecValue[3] = pArg1->vVecValue[3];
		}
		xdefault :
		{
			assert(0);
		}
	}
}

static void dynAnimExpressionCompute_Operation_Vec4Vec4(eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
	pResult->eType = eDynAnimExprTok_Vec4RT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3] + pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3] - pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mul :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] * pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] * pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] * pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3] * pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Div :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] / pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] / pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] / pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3] / pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mod :
		{
			pResult->vVecValue[0] = fmod(pArg1->vVecValue[0], pArg2->vVecValue[0]);
			pResult->vVecValue[1] = fmod(pArg1->vVecValue[1], pArg2->vVecValue[1]);
			pResult->vVecValue[2] = fmod(pArg1->vVecValue[2], pArg2->vVecValue[2]);
			pResult->vVecValue[3] = fmod(pArg1->vVecValue[3], pArg2->vVecValue[3]);
		}
		xdefault :
		{
			assert(0);
		}
	}
}

static void dynAnimExpressionCompute_Operation_Vec4Quat(eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
//pArg1 = (bi, cj, dk, a)	
#define qA pArg1->vVecValue[quatWidx]
#define qB pArg1->vVecValue[quatXidx]
#define qC pArg1->vVecValue[quatYidx]
#define qD pArg1->vVecValue[quatZidx]

//pArg2 = (fi, gj, hk, e)
#define qE pArg2->vVecValue[quatWidx]
#define qF pArg2->vVecValue[quatXidx]
#define qG pArg2->vVecValue[quatYidx]
#define qH pArg2->vVecValue[quatZidx]

	pResult->eType = eDynAnimExprTok_QuatRT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3] + pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3] - pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mul :
		{
			//   (a*e - b*f - c*g - d*h) +
			// i (b*e + a*f + c*h - d*g) +
			// j (a*g - b*h + c*e + d*f) +
			// k (a*h + b*g - c*f + d*e)

			pResult->vVecValue[quatWidx] = qA*qE - qB*qF - qC*qG - qD*qH;
			pResult->vVecValue[quatXidx] = qB*qE + qA*qF + qC*qH - qD*qG;
			pResult->vVecValue[quatYidx] = qA*qG - qB*qH + qC*qE + qD*qF;
			pResult->vVecValue[quatZidx] = qA*qH + qB*qG - qC*qF + qD*qE;
		}
		xcase eDynAnimExprTok_Div :
		acase eDynAnimExprTok_Mod :
		{
			assert(0); // undefined
		}
		xdefault :
		{
			assert(0);
		}
	}

#undef qH
#undef qG
#undef qF
#undef qE

#undef qD
#undef qC
#undef qB
#undef qA
}

static void dynAnimExpressionCompute_Operation_QuatInt(	eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
//pArg1 = (bi, cj, dk, a)	
#define qA pArg1->vVecValue[quatWidx]
#define qB pArg1->vVecValue[quatXidx]
#define qC pArg1->vVecValue[quatYidx]
#define qD pArg1->vVecValue[quatZidx]

//pArg2 = (fi, gj, hk, e)
#define qE pArg2->iIntValue
#define qF pArg2->iIntValue
#define qG pArg2->iIntValue
#define qH pArg2->iIntValue

	pResult->eType = eDynAnimExprTok_QuatRT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->iIntValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->iIntValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->iIntValue;
			pResult->vVecValue[3] = pArg1->vVecValue[3] + pArg2->iIntValue;
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->iIntValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->iIntValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->iIntValue;
			pResult->vVecValue[3] = pArg1->vVecValue[3] - pArg2->iIntValue;
		}
		xcase eDynAnimExprTok_Mul :
		{
			//   (a*e - b*f - c*g - d*h) +
			// i (b*e + a*f + c*h - d*g) +
			// j (a*g - b*h + c*e + d*f) +
			// k (a*h + b*g - c*f + d*e)

			pResult->vVecValue[quatWidx] = qA*qE - qB*qF - qC*qG - qD*qH;
			pResult->vVecValue[quatXidx] = qB*qE + qA*qF + qC*qH - qD*qG;
			pResult->vVecValue[quatYidx] = qA*qG - qB*qH + qC*qE + qD*qF;
			pResult->vVecValue[quatZidx] = qA*qH + qB*qG - qC*qF + qD*qE;
		}
		xcase eDynAnimExprTok_Div :
		acase eDynAnimExprTok_Mod :
		{
			assert(0); // undefined
		}
		xdefault :
		{
			assert(0);
		}
	}

#undef qH
#undef qG
#undef qF
#undef qE

#undef qD
#undef qC
#undef qB
#undef qA
}

static void dynAnimExpressionCompute_Operation_QuatFloat(	eDynAnimExpressionToken operation,
															DynAnimExpressionBlock *pArg1,
															DynAnimExpressionBlock *pArg2,
															DynAnimExpressionBlock *pResult )
{
//pArg1 = (bi, cj, dk, a)	
#define qA pArg1->vVecValue[quatWidx]
#define qB pArg1->vVecValue[quatXidx]
#define qC pArg1->vVecValue[quatYidx]
#define qD pArg1->vVecValue[quatZidx]

//pArg2 = (fi, gj, hk, e)
#define qE pArg2->fFloatValue
#define qF pArg2->fFloatValue
#define qG pArg2->fFloatValue
#define qH pArg2->fFloatValue

	pResult->eType = eDynAnimExprTok_QuatRT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->fFloatValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->fFloatValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->fFloatValue;
			pResult->vVecValue[3] = pArg1->vVecValue[3] + pArg2->fFloatValue;
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->fFloatValue;
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->fFloatValue;
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->fFloatValue;
			pResult->vVecValue[3] = pArg1->vVecValue[3] - pArg2->fFloatValue;
		}
		xcase eDynAnimExprTok_Mul :
		{
			//   (a*e - b*f - c*g - d*h) +
			// i (b*e + a*f + c*h - d*g) +
			// j (a*g - b*h + c*e + d*f) +
			// k (a*h + b*g - c*f + d*e)

			pResult->vVecValue[quatWidx] = qA*qE - qB*qF - qC*qG - qD*qH;
			pResult->vVecValue[quatXidx] = qB*qE + qA*qF + qC*qH - qD*qG;
			pResult->vVecValue[quatYidx] = qA*qG - qB*qH + qC*qE + qD*qF;
			pResult->vVecValue[quatZidx] = qA*qH + qB*qG - qC*qF + qD*qE;
		}
		xcase eDynAnimExprTok_Div :
		acase eDynAnimExprTok_Mod :
		{
			assert(0); // undefined
		}
		xdefault :
		{
			assert(0);
		}
	}

#undef qH
#undef qG
#undef qF
#undef qE

#undef qD
#undef qC
#undef qB
#undef qA
}

static void dynAnimExpressionCompute_Operation_QuatVec3(eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
//pArg1 = (bi, cj, dk, a)	
#define qA pArg1->vVecValue[quatWidx]
#define qB pArg1->vVecValue[quatXidx]
#define qC pArg1->vVecValue[quatYidx]
#define qD pArg1->vVecValue[quatZidx]

//pArg2 = (fi, gj, hk, e)
#define qE 0.f
#define qF pArg2->vVecValue[quatXidx]
#define qG pArg2->vVecValue[quatYidx]
#define qH pArg2->vVecValue[quatZidx]

	pResult->eType = eDynAnimExprTok_QuatRT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mul :
		{
			//   (a*e - b*f - c*g - d*h) +
			// i (b*e + a*f + c*h - d*g) +
			// j (a*g - b*h + c*e + d*f) +
			// k (a*h + b*g - c*f + d*e)

			pResult->vVecValue[quatWidx] = qA*qE - qB*qF - qC*qG - qD*qH;
			pResult->vVecValue[quatXidx] = qB*qE + qA*qF + qC*qH - qD*qG;
			pResult->vVecValue[quatYidx] = qA*qG - qB*qH + qC*qE + qD*qF;
			pResult->vVecValue[quatZidx] = qA*qH + qB*qG - qC*qF + qD*qE;
		}
		xcase eDynAnimExprTok_Div :
		acase eDynAnimExprTok_Mod :
		{
			assert(0); // undefined
		}
		xdefault :
		{
			assert(0);
		}
	}

#undef qH
#undef qG
#undef qF
#undef qE

#undef qD
#undef qC
#undef qB
#undef qA
}

static void dynAnimExpressionCompute_Operation_QuatVec4(eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
//pArg1 = (bi, cj, dk, a)	
#define qA pArg1->vVecValue[quatWidx]
#define qB pArg1->vVecValue[quatXidx]
#define qC pArg1->vVecValue[quatYidx]
#define qD pArg1->vVecValue[quatZidx]

//pArg2 = (fi, gj, hk, e)
#define qE pArg2->vVecValue[quatWidx]
#define qF pArg2->vVecValue[quatXidx]
#define qG pArg2->vVecValue[quatYidx]
#define qH pArg2->vVecValue[quatZidx]

	pResult->eType = eDynAnimExprTok_QuatRT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3] + pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3] - pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mul :
		{
			//   (a*e - b*f - c*g - d*h) +
			// i (b*e + a*f + c*h - d*g) +
			// j (a*g - b*h + c*e + d*f) +
			// k (a*h + b*g - c*f + d*e)

			pResult->vVecValue[quatWidx] = qA*qE - qB*qF - qC*qG - qD*qH;
			pResult->vVecValue[quatXidx] = qB*qE + qA*qF + qC*qH - qD*qG;
			pResult->vVecValue[quatYidx] = qA*qG - qB*qH + qC*qE + qD*qF;
			pResult->vVecValue[quatZidx] = qA*qH + qB*qG - qC*qF + qD*qE;
		}
		xcase eDynAnimExprTok_Div :
		acase eDynAnimExprTok_Mod :
		{
			assert(0); // undefined
		}
		xdefault :
		{
			assert(0);
		}
	}

#undef qH
#undef qG
#undef qF
#undef qE

#undef qD
#undef qC
#undef qB
#undef qA
}

static void dynAnimExpressionCompute_Operation_QuatQuat(eDynAnimExpressionToken operation,
														DynAnimExpressionBlock *pArg1,
														DynAnimExpressionBlock *pArg2,
														DynAnimExpressionBlock *pResult )
{
//pArg1 = (bi, cj, dk, a)	
#define qA pArg1->vVecValue[quatWidx]
#define qB pArg1->vVecValue[quatXidx]
#define qC pArg1->vVecValue[quatYidx]
#define qD pArg1->vVecValue[quatZidx]

//pArg2 = (fi, gj, hk, e)
#define qE pArg2->vVecValue[quatWidx]
#define qF pArg2->vVecValue[quatXidx]
#define qG pArg2->vVecValue[quatYidx]
#define qH pArg2->vVecValue[quatZidx]

	pResult->eType = eDynAnimExprTok_QuatRT;
	switch (operation)
	{
		xcase eDynAnimExprTok_Add :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] + pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] + pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] + pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3] + pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Sub :
		{
			pResult->vVecValue[0] = pArg1->vVecValue[0] - pArg2->vVecValue[0];
			pResult->vVecValue[1] = pArg1->vVecValue[1] - pArg2->vVecValue[1];
			pResult->vVecValue[2] = pArg1->vVecValue[2] - pArg2->vVecValue[2];
			pResult->vVecValue[3] = pArg1->vVecValue[3] - pArg2->vVecValue[3];
		}
		xcase eDynAnimExprTok_Mul :
		{
			//   (a*e - b*f - c*g - d*h) +
			// i (b*e + a*f + c*h - d*g) +
			// j (a*g - b*h + c*e + d*f) +
			// k (a*h + b*g - c*f + d*e)

			pResult->vVecValue[quatWidx] = qA*qE - qB*qF - qC*qG - qD*qH;
			pResult->vVecValue[quatXidx] = qB*qE + qA*qF + qC*qH - qD*qG;
			pResult->vVecValue[quatYidx] = qA*qG - qB*qH + qC*qE + qD*qF;
			pResult->vVecValue[quatZidx] = qA*qH + qB*qG - qC*qF + qD*qE;
			
		}
		xcase eDynAnimExprTok_Div :
		acase eDynAnimExprTok_Mod :
		{
			assert(0); // undefined
		}
		xdefault :
		{
			assert(0);
		}
	}

#undef qH
#undef qG
#undef qF
#undef qE

#undef qD
#undef qC
#undef qB
#undef qA
}

static void dynAnimExpressionCompute_Operation(	eDynAnimExpressionToken operation,
												DynAnimExpressionBlock *pArg1,
												DynAnimExpressionBlock *pArg2,
												DynAnimExpressionBlock *pResult )
{
	if (!pArg1 && pArg2)
	{
		assert(operation == eDynAnimExprTok_Neg);
		pResult->eType = pArg2->eType;
		switch (pArg2->eType)
		{
			xcase eDynAnimExprTok_Int:		pResult->iIntValue = -pArg2->iIntValue;
			xcase eDynAnimExprTok_Float:	pResult->fFloatValue = -pArg2->fFloatValue;
			xcase eDynAnimExprTok_Vec3RT:	scaleVec3(pArg2->vVecValue, -1, pResult->vVecValue);
			xcase eDynAnimExprTok_Vec4RT:
			acase eDynAnimExprTok_QuatRT:	scaleVec4(pArg2->vVecValue, -1, pResult->vVecValue);
		}
	}
	else if (pArg1->eType == eDynAnimExprTok_Int)
	{
		if      (pArg2->eType == eDynAnimExprTok_Int   ) dynAnimExpressionCompute_Operation_IntInt  (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Float ) dynAnimExpressionCompute_Operation_IntFloat(operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Vec3RT) dynAnimExpressionCompute_Operation_IntVec3 (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Vec4RT) dynAnimExpressionCompute_Operation_IntVec4 (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_QuatRT) dynAnimExpressionCompute_Operation_IntQuat	(operation, pArg1, pArg2, pResult);
		else assert(0);
	}
	else if (pArg1->eType == eDynAnimExprTok_Float)
	{
		if      (pArg2->eType == eDynAnimExprTok_Int   ) dynAnimExpressionCompute_Operation_FloatInt  (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Float ) dynAnimExpressionCompute_Operation_FloatFloat(operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Vec3RT) dynAnimExpressionCompute_Operation_FloatVec3 (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Vec4RT) dynAnimExpressionCompute_Operation_FloatVec4 (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_QuatRT) dynAnimExpressionCompute_Operation_FloatQuat (operation, pArg1, pArg2, pResult);
		else assert(0);
	}
	else if (pArg1->eType == eDynAnimExprTok_Vec3RT)
	{
		if      (pArg2->eType == eDynAnimExprTok_Int   ) dynAnimExpressionCompute_Operation_Vec3Int  (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Float ) dynAnimExpressionCompute_Operation_Vec3Float(operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Vec3RT) dynAnimExpressionCompute_Operation_Vec3Vec3 (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Vec4RT) dynAnimExpressionCompute_Operation_Vec3Vec4 (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_QuatRT) dynAnimExpressionCompute_Operation_Vec3Quat (operation, pArg1, pArg2, pResult);
		else assert(0);
	}
	else if (pArg1->eType == eDynAnimExprTok_Vec4RT)
	{
		if      (pArg2->eType == eDynAnimExprTok_Int   ) dynAnimExpressionCompute_Operation_Vec4Int  (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Float ) dynAnimExpressionCompute_Operation_Vec4Float(operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Vec3RT) dynAnimExpressionCompute_Operation_Vec4Vec3 (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Vec4RT) dynAnimExpressionCompute_Operation_Vec4Vec4 (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_QuatRT) dynAnimExpressionCompute_Operation_Vec4Quat (operation, pArg1, pArg2, pResult);
		else assert(0);
	}
	else if (pArg1->eType == eDynAnimExprTok_QuatRT)
	{
		if      (pArg2->eType == eDynAnimExprTok_Int   ) dynAnimExpressionCompute_Operation_QuatInt  (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Float ) dynAnimExpressionCompute_Operation_QuatFloat(operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Vec3RT) dynAnimExpressionCompute_Operation_QuatVec3 (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_Vec4RT) dynAnimExpressionCompute_Operation_QuatVec4 (operation, pArg1, pArg2, pResult);
		else if (pArg2->eType == eDynAnimExprTok_QuatRT) dynAnimExpressionCompute_Operation_QuatQuat (operation, pArg1, pArg2, pResult);
		else assert(0);
	}
	else
	{
		assert(0);
	}
}

static F32 dynAnimExpressionAsF32(DynAnimExpressionBlock *pValue)
{
	switch (pValue->eType)
	{
		xcase eDynAnimExprTok_Int		: return (float)pValue->iIntValue;
		xcase eDynAnimExprTok_Float		: return pValue->fFloatValue;
		xcase eDynAnimExprTok_Vec3RT	: return lengthVec3(pValue->vVecValue);
		xcase eDynAnimExprTok_Vec4RT	:
		acase eDynAnimExprTok_QuatRT	: return lengthVec4(pValue->vVecValue);
		xdefault:
		{
			assert(0);
		}
	}
}

static void dynAnimExpressionCompute_SinFunction(DynSkeleton *pSkeleton, F32 fDeltaTime, DynAnimExpressionBlock **eaBlocks, U32 *i, DynAnimExpressionBlock *pValue)
{
	DynAnimExpressionBlock bOut;
	dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut);
	pValue->eType = eDynAnimExprTok_Float;
	pValue->fFloatValue = sinf(dynAnimExpressionAsF32(&bOut));
}

static void dynAnimExpressionCompute_CosFunction(DynSkeleton *pSkeleton, F32 fDeltaTime, DynAnimExpressionBlock **eaBlocks, U32 *i, DynAnimExpressionBlock *pValue)
{
	DynAnimExpressionBlock bOut;
	dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut);
	pValue->eType = eDynAnimExprTok_Float;
	pValue->fFloatValue = cosf(dynAnimExpressionAsF32(&bOut));
}

static void dynAnimExpressionCompute_MaxfFunction(DynSkeleton *pSkeleton, F32 fDeltaTime, DynAnimExpressionBlock **eaBlocks, U32 *i, DynAnimExpressionBlock *pValue)
{
	DynAnimExpressionBlock bOut0, bOut1;
	dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut1);
	dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut0);
	pValue->eType = eDynAnimExprTok_Float;
	pValue->fFloatValue = MAX(dynAnimExpressionAsF32(&bOut0), dynAnimExpressionAsF32(&bOut1));
}

static void dynAnimExpressionCompute_MinfFunction(DynSkeleton *pSkeleton, F32 fDeltaTime, DynAnimExpressionBlock **eaBlocks, U32 *i, DynAnimExpressionBlock *pValue)
{
	DynAnimExpressionBlock bOut0, bOut1;
	dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut1);
	dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut0);
	pValue->eType = eDynAnimExprTok_Float;
	pValue->fFloatValue = MINF(dynAnimExpressionAsF32(&bOut0), dynAnimExpressionAsF32(&bOut1));
}

static void dynAnimExpressionCompute_ClampfFunction(DynSkeleton *pSkeleton, F32 fDeltaTime, DynAnimExpressionBlock **eaBlocks, U32 *i, DynAnimExpressionBlock *pValue)
{
	DynAnimExpressionBlock bOut0, bOut1, bOut2;
	dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut2);
	dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut1);
	dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut0);
	pValue->eType = eDynAnimExprTok_Float;
	pValue->fFloatValue = CLAMPF(dynAnimExpressionAsF32(&bOut0), dynAnimExpressionAsF32(&bOut1), dynAnimExpressionAsF32(&bOut2));
}

//evaluates the expression stored in eaBlocks, using any variable values from the skeleton, and stores the result in pValue
static void dynAnimExpressionCompute(DynSkeleton *pSkeleton, F32 fDeltaTime, DynAnimExpressionBlock **eaBlocks, U32 *i, DynAnimExpressionBlock *pValue)
{
	assert(*i >= 0);

	switch (eaBlocks[*i]->eType)
	{
		xcase eDynAnimExprTok_Float:
		{
			pValue->eType = eDynAnimExprTok_Float;
			pValue->fFloatValue = eaBlocks[(*i)--]->fFloatValue;
		}
		xcase eDynAnimExprTok_Int:
		{
			pValue->eType = eDynAnimExprTok_Int;
			pValue->iIntValue = eaBlocks[(*i)--]->iIntValue;
		}
		xcase eDynAnimExprTok_Vec3:
		{
			DynAnimExpressionBlock bOut0, bOut1, bOut2;
			(*i)--;
			pValue->eType = eDynAnimExprTok_Vec3RT;
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut2);
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut1);
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut0);
			pValue->vVecValue[2] = dynAnimExpressionAsF32(&bOut2);
			pValue->vVecValue[1] = dynAnimExpressionAsF32(&bOut1);
			pValue->vVecValue[0] = dynAnimExpressionAsF32(&bOut0);
		}
		xcase eDynAnimExprTok_Vec4:
		{
			DynAnimExpressionBlock bOut0, bOut1, bOut2, bOut3;
			(*i)--;
			pValue->eType = eDynAnimExprTok_Vec4RT;
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut3);
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut2);
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut1);
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut0);
			pValue->vVecValue[3] = dynAnimExpressionAsF32(&bOut3);
			pValue->vVecValue[2] = dynAnimExpressionAsF32(&bOut2);
			pValue->vVecValue[1] = dynAnimExpressionAsF32(&bOut1);
			pValue->vVecValue[0] = dynAnimExpressionAsF32(&bOut0);
		}
		xcase eDynAnimExprTok_Quat:
		{
			DynAnimExpressionBlock bOut0, bOut1, bOut2, bOut3;
			(*i)--;
			pValue->eType = eDynAnimExprTok_QuatRT;
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut3);
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut2);
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut1);
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut0);
			pValue->vVecValue[3] = dynAnimExpressionAsF32(&bOut3);
			pValue->vVecValue[2] = dynAnimExpressionAsF32(&bOut2);
			pValue->vVecValue[1] = dynAnimExpressionAsF32(&bOut1);
			pValue->vVecValue[0] = dynAnimExpressionAsF32(&bOut0);
		}
		xcase eDynAnimExprTok_Neg:
		{
			DynAnimExpressionBlock bOut1;
			(*i)--;
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut1);
			dynAnimExpressionCompute_Operation(eDynAnimExprTok_Neg, NULL, &bOut1, pValue);
		}
		xcase eDynAnimExprTok_Add:
		{
			DynAnimExpressionBlock bOut1, bOut2;
			(*i)--;
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut1);
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut2);
			dynAnimExpressionCompute_Operation(eDynAnimExprTok_Add, &bOut2, &bOut1, pValue);
		}
		xcase eDynAnimExprTok_Sub:
		{
			DynAnimExpressionBlock bOut1, bOut2;
			(*i)--;
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut1);
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut2);
			dynAnimExpressionCompute_Operation(eDynAnimExprTok_Sub, &bOut2, &bOut1, pValue);
		}
		xcase eDynAnimExprTok_Mul:
		{
			DynAnimExpressionBlock bOut1, bOut2;
			(*i)--;
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut1);
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut2);
			dynAnimExpressionCompute_Operation(eDynAnimExprTok_Mul, &bOut2, &bOut1, pValue);
		}
		xcase eDynAnimExprTok_Div:
		{
			DynAnimExpressionBlock bOut1, bOut2;
			(*i)--;
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut1);
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut2);
			dynAnimExpressionCompute_Operation(eDynAnimExprTok_Div, &bOut2, &bOut1, pValue);
		}
		xcase eDynAnimExprTok_Mod:
		{
			DynAnimExpressionBlock bOut1, bOut2;
			(*i)--;
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut1);
			dynAnimExpressionCompute(pSkeleton, fDeltaTime, eaBlocks, i, &bOut2);
			dynAnimExpressionCompute_Operation(eDynAnimExprTok_Mod, &bOut2, &bOut1, pValue);
		}
		xcase eDynAnimExprTok_Function:
		{
			const char *pcFunc;

			(*i)--;
			pcFunc = eaBlocks[(*i)--]->pcName;

			if (pcFunc == s_pcSinFunction)
			{
				dynAnimExpressionCompute_SinFunction(pSkeleton, fDeltaTime, eaBlocks, i, pValue);
			}
			else if (pcFunc == s_pcCosFunction)
			{
				dynAnimExpressionCompute_CosFunction(pSkeleton, fDeltaTime, eaBlocks, i, pValue);
			}
			else if (pcFunc == s_pcMaxfFunction)
			{
				dynAnimExpressionCompute_MaxfFunction(pSkeleton, fDeltaTime, eaBlocks, i, pValue);
			}
			else if (pcFunc == s_pcMinfFunction)
			{
				dynAnimExpressionCompute_MinfFunction(pSkeleton, fDeltaTime, eaBlocks, i, pValue);
			}
			else if (pcFunc == s_pcClampfFunction)
			{
				dynAnimExpressionCompute_ClampfFunction(pSkeleton, fDeltaTime, eaBlocks, i, pValue);
			}
		}
		xcase eDynAnimExprTok_Var:
		{
			assert(0);
		}
		xcase eDynAnimExprTok_VarWithComponent:
		{
			const DynNode *pNode;
			const char *pcNode;
			const char *pcComponent;

			(*i)--;
			pcNode		= eaBlocks[(*i)--]->pcName;
			pcComponent	= eaBlocks[(*i)--]->pcName;

			if (pcNode == s_pcSystem)
			{
				if (pcComponent == s_pcTime)
				{
					pValue->eType = eDynAnimExprTok_Float;
					pValue->fFloatValue = timerSeconds(timerCpuTicks());
				}
				else if (pcComponent == s_pcTimestep)
				{
					pValue->eType = eDynAnimExprTok_Float;
					pValue->fFloatValue = fDeltaTime;
				}
				else
				{
					assert(0);
				}
			}
			else
			{
				pNode = dynSkeletonFindNode(pSkeleton, pcNode);
				if (pNode)
				{
					if (pcComponent == s_pcPosComponent)
					{
						Vec3 vPosWS;
						dynNodeGetWorldSpacePos(pNode, vPosWS);
						pValue->eType = eDynAnimExprTok_Vec3RT;
						copyVec3(vPosWS, pValue->vVecValue);
					}
					else if (pcComponent == s_pcSclComponent)
					{
						Vec3 vSclWS;
						dynNodeGetWorldSpaceScale(pNode, vSclWS);
						pValue->eType = eDynAnimExprTok_Vec3RT;
						copyVec3(vSclWS, pValue->vVecValue);
					}
					else if (pcComponent == s_pcRotComponent)
					{
						Quat qRotWS;
						dynNodeGetWorldSpaceRot(pNode, qRotWS);
						pValue->eType = eDynAnimExprTok_QuatRT;
						copyVec4(qRotWS, pValue->vVecValue);
					}
					else {
						assert(0);
					}
				} else {
					assert(0);
				}
			}
		}
		xcase eDynAnimExprTok_VarWithTwoComponents:
		{
			const DynTransform *pxForm;
			const char *pcNode;
			const char *pcComponent;
			const char *pcComponent2;

			(*i)--;
			pcNode		= eaBlocks[(*i)--]->pcName;
			pcComponent	= eaBlocks[(*i)--]->pcName;
			pcComponent2= eaBlocks[(*i)--]->pcName;

			assert(pcComponent == s_pcLastFrame);

			if (pxForm = dynAnimExpressionFind_LastFrameTransformWS(pSkeleton, pcNode))
			{
				if (pcComponent2 == s_pcPosComponent)
				{
					pValue->eType = eDynAnimExprTok_Vec3RT;
					copyVec3(pxForm->vPos, pValue->vVecValue);
				}
				else if (pcComponent2 == s_pcSclComponent)
				{
					pValue->eType = eDynAnimExprTok_Vec3RT;
					copyVec3(pxForm->vScale, pValue->vVecValue);
				}
				else if (pcComponent2 == s_pcRotComponent)
				{
					pValue->eType = eDynAnimExprTok_QuatRT;
					copyVec4(pxForm->qRot, pValue->vVecValue);
				}
				else {
					assert(0);
				}
			} else {
				assert(0);
			}
		}
		xcase eDynAnimExprTok_VarWithComponentAndAccessor:
		{
			const DynNode *pNode;
			const char *pcNode;
			const char *pcComponent;
			S32 offset;

			(*i)--;
			pcNode		= eaBlocks[(*i)--]->pcName;
			pcComponent	= eaBlocks[(*i)--]->pcName;
			offset		= eaBlocks[(*i)--]->iIntValue;
			assert(0 <= offset);

			pNode = dynSkeletonFindNode(pSkeleton, pcNode);
			if (pNode)
			{
				if (pcComponent == s_pcPosComponent)
				{
					Vec3 vPosWS;
					assert(offset < 3);
					dynNodeGetWorldSpacePos(pNode, vPosWS);
					pValue->eType = eDynAnimExprTok_Float;
					pValue->fFloatValue = vPosWS[offset];
				}
				else if (pcComponent == s_pcSclComponent)
				{
					Vec3 vSclWS;
					assert(offset < 3);
					dynNodeGetWorldSpaceScale(pNode, vSclWS);
					pValue->eType = eDynAnimExprTok_Float;
					pValue->fFloatValue = vSclWS[offset];
				}
				else if (pcComponent == s_pcRotComponent)
				{
					assert(0); // add support for Euler angles when accessing a rotation component
				}
				else {
					assert(0);
				}
			} else {
				assert(0);
			}
		}
		xcase eDynAnimExprTok_VarWithTwoComponentsAndAccessor:
		{
			const DynTransform *pxForm;
			const char *pcNode;
			const char *pcComponent;
			const char *pcComponent2;
			S32 offset;

			(*i)--;
			pcNode		= eaBlocks[(*i)--]->pcName;
			pcComponent	= eaBlocks[(*i)--]->pcName;
			pcComponent2= eaBlocks[(*i)--]->pcName;
			offset		= eaBlocks[(*i)--]->iIntValue;

			assert(pcComponent == s_pcLastFrame);
			assert(0 <= offset);

			if (pxForm = dynAnimExpressionFind_LastFrameTransformWS(pSkeleton, pcNode))
			{
				if (pcComponent2 == s_pcPosComponent)
				{
					assert(offset < 3);
					pValue->eType = eDynAnimExprTok_Float;
					pValue->fFloatValue = pxForm->vPos[offset];
				}
				else if (pcComponent2 == s_pcSclComponent)
				{
					assert(offset < 3);
					pValue->eType = eDynAnimExprTok_Float;
					pValue->fFloatValue = pxForm->vScale[offset];
				}
				else if (pcComponent2 == s_pcRotComponent)
				{
					assert(0); // add support for Euler angles when accessing a rotation component
				}
				else {
					assert(0);
				}
			} else {
				assert(0);
			}
		}
		xdefault: {
			assert(0);
		}
	}
}

// +----------------+
// |                |
// | Store Routines |
// |                |
// +----------------+

static void dynAnimExpressionStore_F32(DynAnimExpressionBlock *pValue, F32 *fNewValue)
{
	switch(pValue->eType)
	{
		xcase eDynAnimExprTok_Int:		*fNewValue = pValue->iIntValue;
		xcase eDynAnimExprTok_Float:	*fNewValue = pValue->fFloatValue;
		xcase eDynAnimExprTok_Vec3RT:	*fNewValue = lengthVec3(pValue->vVecValue);
		xcase eDynAnimExprTok_Vec4RT:
		acase eDynAnimExprTok_QuatRT:	*fNewValue = lengthVec4(pValue->vVecValue);;
		xdefault: assert(0);
	}
}

static void dynAnimExpressionStore_Vec3(DynAnimExpressionBlock *pValue, Vec3 vNewValue)
{
	switch(pValue->eType)
	{
		xcase eDynAnimExprTok_Int:		vNewValue[0] = vNewValue[1] = vNewValue[2] = pValue->iIntValue;
		xcase eDynAnimExprTok_Float:	vNewValue[0] = vNewValue[1] = vNewValue[2] = pValue->fFloatValue;
		xcase eDynAnimExprTok_Vec3RT:
		acase eDynAnimExprTok_Vec4RT:
		acase eDynAnimExprTok_QuatRT:	copyVec3(pValue->vVecValue, vNewValue);
		xdefault: assert(0);
	}
}

static void dynAnimExpressionStore_Vec4(DynAnimExpressionBlock *pValue, Vec4 vNewValue)
{
	switch(pValue->eType)
	{
		xcase eDynAnimExprTok_Int:		vNewValue[0] = vNewValue[1] = vNewValue[2] = pValue->iIntValue;   vNewValue[3] = 0.f;
		xcase eDynAnimExprTok_Float:	vNewValue[0] = vNewValue[1] = vNewValue[2] = pValue->fFloatValue; vNewValue[3] = 0.f;
		xcase eDynAnimExprTok_Vec3RT:	copyVec3(pValue->vVecValue, vNewValue); vNewValue[3] = 0.0f;
		xcase eDynAnimExprTok_Vec4RT:
		acase eDynAnimExprTok_QuatRT:	copyVec4(pValue->vVecValue, vNewValue);
		xdefault: assert(0);
	}
}

static void dynAnimExpressionStore_Quat(DynAnimExpressionBlock *pValue, Quat qNewValue)
{
	switch(pValue->eType)
	{
		xcase eDynAnimExprTok_Int:		qNewValue[0] = qNewValue[1] = qNewValue[2] = 0.f; qNewValue[3] = pValue->iIntValue;
		xcase eDynAnimExprTok_Float:	qNewValue[0] = qNewValue[1] = qNewValue[2] = 0.f; qNewValue[3] = pValue->fFloatValue;
		xcase eDynAnimExprTok_Vec3RT:	copyVec3(pValue->vVecValue, qNewValue); qNewValue[3] = 1.0f;
		xcase eDynAnimExprTok_Vec4RT:
		acase eDynAnimExprTok_QuatRT:	copyVec4(pValue->vVecValue, qNewValue);
		xdefault: assert(0);
	}
}

//evaluates the expression stored in eaBlocks to determine the variable's location on the skeleton, then sets that variable equal to pValue, the code blocks
//that are passed in here should only ever be used for debugging
static void dynAnimExpressionStore(	DynSkeleton *pSkeleton,
									DynNode *pNode,
									DynAnimExpressionBlock **eaBlocks,
									DynAnimExpressionBlock **eaCodeBlocks, // should only be used for DEBUGGING here
									DynAnimExpressionBlock *pValue )
{
	U32 i = eaSize(&eaBlocks);

	if (uiDebugDynAnimExpression)
	{
		printfColor(COLOR_BLUE, "Setting ");
		FOR_EACH_IN_EARRAY(eaBlocks, DynAnimExpressionBlock, pVarBlock) {
			dynAnimExpressionDebugBlock(pVarBlock);
		} FOR_EACH_END;
		printfColor(COLOR_BLUE," = ");
		dynAnimExpressionDebugBlock(pValue);
		printfColor(COLOR_BLUE," = ");
		FOR_EACH_IN_EARRAY(eaCodeBlocks, DynAnimExpressionBlock, pCodeBlock) {
			dynAnimExpressionDebugBlock(pCodeBlock);
		} FOR_EACH_END;
		printf("\n");
	}

	switch (eaBlocks[i-1]->eType)
	{
		xcase eDynAnimExprTok_VarWithComponentAndAccessor :
		{
			F32 fNewValue;
			const char *pcNode;
			const char *pcComponent;
			S32 offset;

			dynAnimExpressionStore_F32(pValue, &fNewValue);
			pcNode		= eaBlocks[i-2]->pcName;
			pcComponent	= eaBlocks[i-3]->pcName;
			offset		= eaBlocks[i-4]->iIntValue;
			assert(	0 <= offset &&
					pNode &&
					pNode->pcTag == pcNode);

			if (pcComponent == s_pcPosComponent)
			{
				assert(0 <= offset && offset < 3);
				if (pNode->pParent) // && we've requested to write a world space value
				{
					Vec3 vPosOld, vPosNew;
					dynNodeGetWorldSpacePos(pNode, vPosOld);
					copyVec3(vPosOld, vPosNew);
					vPosNew[offset] = fNewValue;
					dynNodeCalcLocalSpacePosFromWorldSpacePos(pNode, vPosNew);
					CLAMPVEC3(vPosNew,-MAX_PLAYABLE_COORDINATE,MAX_PLAYABLE_COORDINATE);
					dynNodeSetPos(pNode, vPosNew);
				}
				else
				{
					Vec3 vOldLS, vNewLS;
					dynNodeGetLocalPos(pNode, vOldLS);
					copyVec3(vOldLS, vNewLS);
					vNewLS[offset] = fNewValue;
					dynNodeSetPos(pNode, vNewLS);
				}
			}
			else if (pcComponent == s_pcSclComponent)
			{
				assert(0 <= offset && offset < 3);
				if (pNode->pParent) // && we've requested to write a world space value
				{
					Vec3 vScl;
					dynNodeGetWorldSpaceScale(pNode->pParent, vScl);
					fNewValue = fNewValue / vScl[offset];
					dynNodeGetLocalScale(pNode, vScl);
					vScl[offset] = fNewValue;
					dynNodeSetScale(pNode, vScl);
				}
				else
				{
					Vec3 vOldLS, vNewLS;
					dynNodeGetLocalScale(pNode, vOldLS);
					copyVec3(vOldLS, vNewLS);
					vNewLS[offset] = fNewValue;
					dynNodeSetScale(pNode, vNewLS);
				}
			}
			else if (pcComponent == s_pcRotComponent)
			{
				assert(0); // add support Euler angles when accessing a rotation component
			}
			else
			{
				assert(0);
			}
		}
		xcase eDynAnimExprTok_VarWithComponent :
		{
			const char *pcNode;
			const char *pcComponent;

			pcNode		= eaBlocks[i-2]->pcName;
			pcComponent	= eaBlocks[i-3]->pcName;
			assert(	pNode &&
					pNode->pcTag == pcNode);

			if (pcComponent == s_pcPosComponent)
			{
				Vec3 vNewValue;
				dynAnimExpressionStore_Vec3(pValue, vNewValue);

				if (pNode->pParent) // && we've requested to write a world space value
				{
					dynNodeCalcLocalSpacePosFromWorldSpacePos(pNode, vNewValue);
					CLAMPVEC3(vNewValue,-MAX_PLAYABLE_COORDINATE,MAX_PLAYABLE_COORDINATE);
					dynNodeSetPos(pNode, vNewValue);
				}
				else
				{
					dynNodeSetPos(pNode, vNewValue);
				}
			}
			else if (pcComponent == s_pcSclComponent)
			{
				Vec3 vNewValue;
				dynAnimExpressionStore_Vec3(pValue, vNewValue);

				if (pNode->pParent) // && we've requested to write a world space value
				{
					Vec3 vParentSclWS;
					dynNodeGetWorldSpaceScale(pNode->pParent, vParentSclWS);
					vNewValue[0] = vNewValue[0] / vParentSclWS[0];
					vNewValue[1] = vNewValue[1] / vParentSclWS[1];
					vNewValue[2] = vNewValue[2] / vParentSclWS[2];
					dynNodeSetScale(pNode, vNewValue);
				}
				else
				{
					dynNodeSetScale(pNode, vNewValue);
				}
			}
			else if (pcComponent == s_pcRotComponent)
			{
				Quat qNewValue;
				dynAnimExpressionStore_Quat(pValue, qNewValue);

				if (pNode->pParent) // && we've requested to write a world space value
				{
					Quat qRot, qRotInv;
					Quat qDiff;
					dynNodeGetWorldSpaceRot(pNode->pParent,qRot);
					quatInverse(qRot, qRotInv);
					quatMultiply(qNewValue, qRotInv, qDiff);
					dynNodeSetRot(pNode, qDiff);
				}
				else
				{
					dynNodeSetRot(pNode, qNewValue);
				}
			}
			else
			{
				assert(0);
			}
		}
		xdefault :
		{
			assert(0);
		}
	}
}

// +------------------+
// |                  |
// | Utility Routines |
// |                  |
// +------------------+

AUTO_CMD_INT( uiDebugDynAnimExpression, danimDebugDynAnimExpressions ) ACMD_COMMANDLINE ACMD_CATEGORY(dynAnimation);

AUTO_RUN;
void initDynAnimExpressionStrings(void)
{
	s_pcPosComponent = allocAddStaticString("pos");
	s_pcSclComponent = allocAddStaticString("scl");
	s_pcRotComponent = allocAddStaticString("rot");

	s_pcLastFrame = allocAddStaticString("lastframe");

	s_pcSystem = allocAddString("System");
	s_pcTime = allocAddStaticString("Time");
	s_pcTimestep = allocAddStaticString("Timestep");

	s_pcSinFunction = allocAddStaticString("Sin");
	s_pcCosFunction = allocAddStaticString("Cos");

	s_pcMaxfFunction = allocAddStaticString("Maxf");
	s_pcMinfFunction = allocAddStaticString("Minf");
	s_pcClampfFunction = allocAddStaticString("Clampf");
}

// +-----------------------+
// |                       |
// | Runtime Data Routines |
// |                       |
// +-----------------------+

static const DynTransform *dynAnimExpressionFind_LastFrameTransformWS(DynSkeleton *pSkeleton, const char *pcNodeName)
{
	const DynNode *pNode;

	FOR_EACH_IN_EARRAY(pSkeleton->eaAnimExpressionData, DynAnimExpressionRuntimeData, pData) {
		if (pData->pcNodeName == pcNodeName)
		{
			if (pData->bNeedsInit)
			{
				pNode = dynSkeletonFindNode(pSkeleton, pcNodeName);
				dynNodeGetWorldSpaceTransform(pNode, &pData->xLastFrameWS);
				pData->bNeedsInit = false;
			}
			return &pData->xLastFrameWS;
		}
	} FOR_EACH_END;

	if (pNode = dynSkeletonFindNode(pSkeleton, pcNodeName))
	{
		DynAnimExpressionRuntimeData *pNewData = malloc(sizeof(DynAnimExpressionRuntimeData));
		pNewData->pcNodeName = pcNodeName;
		dynNodeGetWorldSpaceTransform(pNode, &pNewData->xLastFrameWS);
		pNewData->bNeedsInit = false;
		eaPush(&pSkeleton->eaAnimExpressionData, pNewData);
		return &pNewData->xLastFrameWS;
	}

	return NULL;
}

void dynAnimExpressionInvalidateData(DynSkeleton *pSkeleton, const DynNode *pNode)
{
	if (pNode)
	{
		FOR_EACH_IN_EARRAY(pSkeleton->eaAnimExpressionData, DynAnimExpressionRuntimeData, pData) {
			if (pNode->pcTag == pData->pcNodeName)
			{
				pData->bNeedsInit = true;
			}
		} FOR_EACH_END;
	}
	else
	{
		FOR_EACH_IN_EARRAY(pSkeleton->eaAnimExpressionData, DynAnimExpressionRuntimeData, pData) {
			pData->bNeedsInit = true;
		} FOR_EACH_END;
	}
}

void dynAnimExpressionUpdateData(DynSkeleton *pSkeleton, const DynNode *pNode)
{
	FOR_EACH_IN_EARRAY(pSkeleton->eaAnimExpressionData, DynAnimExpressionRuntimeData, pData)
	{
		if (pData->pcNodeName == pNode->pcTag)
		{
			dynNodeAssumeCleanGetWorldSpaceTransform(pNode, &pData->xLastFrameWS);
		}
	}
	FOR_EACH_END;
}

// +---------------+
// |               |
// | Main Routines |
// |               |
// +---------------+

void dynAnimExpressionRun(DynSkeleton *pSkeleton, DynNode *pNode, F32 fDeltaTime)
{
	if (pSkeleton->pAnimExpressionSet)
	{
		FOR_EACH_IN_EARRAY(pSkeleton->pAnimExpressionSet->eaDynAnimExpression, DynAnimExpression, pExpression)
		{
			assert(eaSize(&pExpression->eaBlocksTarget) > 1);
			if(pNode->pcTag == pExpression->eaBlocksTarget[eaSize(&pExpression->eaBlocksTarget)-2]->pcName)
			{
				DynAnimExpressionBlock bOut;
				U32 i = eaSize(&pExpression->eaBlocksCode) - 1;
				dynAnimExpressionDebugNode(pNode);
				dynAnimExpressionCompute(pSkeleton, fDeltaTime, pExpression->eaBlocksCode, &i, &bOut);
				dynAnimExpressionStore(pSkeleton, pNode, pExpression->eaBlocksTarget, pExpression->eaBlocksCode, &bOut);
				dynNodeCalcWorldSpaceOneNode(pNode);
				dynAnimExpressionDebugNode(pNode);
			}
		}
		FOR_EACH_END;
	}
}

// +--------------+
// | AST includes |
// +--------------+

#include "dynAnimExpression_h_ast.c"