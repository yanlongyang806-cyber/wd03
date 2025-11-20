#pragma  once
GCC_SYSTEM

#include "earray.h"

#define DYNBITFIELD_ARRAY_SIZE 32

typedef U16 DynBit;
#define DYNBIT_DETAIL_BIT_FLAG (1 << 15)
#define DYNBIT_DETAIL_BIT_MASK (DYNBIT_DETAIL_BIT_FLAG - 1)

#define dynBitIsDetailBit(a) (a & DYNBIT_DETAIL_BIT_FLAG)
#define dynBitIsModeBit(a) (!dynBitIsDetailBit(a))

typedef enum eDynBitType
{
	eDynBitType_Mode,
	eDynBitType_Detail,
	iNumDynBitTypes,
	eDynBitType_Unknown,
} eDynBitType;

typedef enum eDynBitAction
{
	edba_Set,
	edba_Clear,
	edba_Toggle,
	edba_Test,
} eDynBitAction;


typedef struct DynBitField
{
	U32				uiNumBits;
	DynBit			aBits[DYNBITFIELD_ARRAY_SIZE];
} DynBitField;

AUTO_STRUCT;
typedef struct DynBitFieldStatic
{
	U32				uiNumBits;							NO_AST
	DynBit*			pBits;								NO_AST
	const char**	ppcBits; AST(STRUCTPARAM POOL_STRING)
} DynBitFieldStatic;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynLogicBlock
{
	DynBitFieldStatic	off;
	DynBitFieldStatic	on;
} DynLogicBlock;

typedef struct DynBitFieldGroup
{
	DynBitField			flashBits;
	DynBitField			toggleBits;
	bool bFreeOnRelease; // set this to true if you want the skeleton to free this group when the skeleton is freed
} DynBitFieldGroup;

AUTO_STRUCT;
typedef struct IRQGroup
{
	const char* pcIRQGroupName; AST(POOL_STRING STRUCTPARAM KEY)
	const char** eaIRQNames; AST(POOL_STRING STRUCTPARAM)
	const char* pcFileName; AST(CURRENTFILE)
} IRQGroup;

void dynBitListLoad(void);
void dynIRQGroupListLoad(void);

const IRQGroup* dynIRQGroupFromName(const char* pcName);
bool dynIRQGroupContainsBit( const char* pcIRQGroupName, const char* pcBitName );

U32 dynBitFieldNumDetailBits(const DynBitField* pBF);

bool dynBitIsValidName(const char* pcBitName);
bool dynBitActOnByName(SA_PARAM_NN_VALID DynBitField* pBF, SA_PARAM_NN_STR const char* pcBit, eDynBitAction edba );
bool dynBitFieldActOnBit(SA_PARAM_NN_VALID DynBitField* pBF, DynBit bit, eDynBitAction edba );

void dynBitArrayWriteBitString(SA_PARAM_NN_STR char* pcBuffer, U32 uiMaxBufferSize, SA_PARAM_NN_VALID const DynBit* pBits, U32 uiNumBits);

__forceinline static void dynBitFieldWriteBitString(SA_PARAM_NN_STR char* pcBuffer, U32 uiMaxBufferSize, SA_PARAM_NN_VALID const DynBitField* pBF)
{
	dynBitArrayWriteBitString(pcBuffer, uiMaxBufferSize, pBF->aBits, pBF->uiNumBits);
}

__forceinline static void dynBitFieldStaticWriteBitString(SA_PARAM_NN_STR char* pcBuffer, U32 uiMaxBufferSize, SA_PARAM_NN_VALID const DynBitFieldStatic* pBF)
{
	dynBitArrayWriteBitString(pcBuffer, uiMaxBufferSize, pBF->pBits, pBF->uiNumBits);
}

bool dynBitFieldCheckForExtraBits(SA_PARAM_NN_VALID const DynBitFieldStatic* pBFToCheck, SA_PARAM_NN_VALID const DynBitField* pBFAgainst);
U32 dynBitArrayCountBitsInCommon(SA_PARAM_NN_VALID const DynBit* pBFToCheck, U32 uiNumToCheck, SA_PARAM_NN_VALID const DynBit* pBFAgainst, U32 uiNumAgainst, eDynBitType eBitType, char** pcDebugLog);

bool dynBitFieldStaticSetFromStrings(SA_PARAM_NN_VALID DynBitFieldStatic* pBF, SA_PARAM_OP_VALID const char** ppcBadBit);
U32 dynBitArrayCountTotalBitsInCommon(SA_PARAM_NN_VALID const DynBit* pBFToCheck, U32 uiNumToCheck, SA_PARAM_NN_VALID const DynBit* pBFAgainst, U32 uiNumAgainst);
U32 dynBitFieldStaticCountCriticalBits(SA_PARAM_NN_VALID const DynBitFieldStatic* pBF, SA_PARAM_OP_VALID char** pcDebugLog);
void dynBitFieldSetAllFromBitField( SA_PARAM_NN_VALID DynBitField* pBF, SA_PARAM_NN_VALID const DynBitField* pSrc);
void dynBitFieldSetAllFromBitFieldStatic( SA_PARAM_NN_VALID DynBitField* pBF, SA_PARAM_NN_VALID const DynBitFieldStatic* pSrc);
bool dynBitFieldBitTest(SA_PARAM_NN_VALID const DynBitField* pBF, DynBit bit);
bool dynBitFieldBitSet(SA_PARAM_NN_VALID DynBitField* pBF, DynBit bit);
bool dynBitFieldBitClear(SA_PARAM_NN_VALID DynBitField* pBF, DynBit bit);


void dynBitFieldGroupClearAll( SA_PARAM_NN_VALID DynBitFieldGroup* pBFGroup );
void dynBitFieldGroupFlashBit( SA_PARAM_NN_VALID DynBitFieldGroup* pBFGroup, SA_PARAM_NN_STR const char* pcBit );
void dynBitFieldGroupToggleBit( SA_PARAM_NN_VALID DynBitFieldGroup* pBFGroup, SA_PARAM_NN_STR const char* pcBit );
void dynBitFieldGroupSetBit( SA_PARAM_NN_VALID DynBitFieldGroup* pBFGroup, SA_PARAM_NN_STR const char* pcBit );
void dynBitFieldGroupClearBit( SA_PARAM_NN_VALID DynBitFieldGroup* pBFGroup, SA_PARAM_NN_STR const char* pcBit );
void dynBitFieldGroupSetToMatchSentence( SA_PARAM_NN_VALID DynBitFieldGroup* pBFGroup, SA_PARAM_NN_STR char* pcBits );
void dynBitFieldGroupAddBits(DynBitFieldGroup * pBFG, const char *pcBits, bool bToggle);

DynBit dynBitFromName(const char* pcBitName);

__forceinline static void dynBitFieldClear(DynBitField* pBF)
{
	memset(pBF, 0, sizeof(DynBitField));
}

__forceinline static void dynBitFieldCopy(const DynBitField* pBFSrc, DynBitField* pBFDst)
{
	memcpy(pBFDst, pBFSrc, sizeof(DynBitField));
}

__forceinline static bool dynBitFieldsAreEqual(const DynBitField* pBFA, const DynBitField* pBFB)
{
	if (pBFA->uiNumBits != pBFB->uiNumBits)
		return false;
	assert(pBFA->uiNumBits <= DYNBITFIELD_ARRAY_SIZE);
	return (memcmp(pBFA->aBits, pBFB->aBits, sizeof(DynBit)*pBFA->uiNumBits) == 0);
}

__forceinline static bool dynBitFieldStaticsAreEqual(const DynBitFieldStatic* pBFA, const DynBitFieldStatic* pBFB)
{
	if (pBFA->uiNumBits != pBFB->uiNumBits)
		return false;
	return (memcmp(pBFA->pBits, pBFB->pBits, sizeof(DynBit)*pBFA->uiNumBits) == 0);
}

__forceinline static bool dynBitFieldSatisfiesLogicBlock( const DynBitField* pBF, const DynLogicBlock* pLogicBlock )
{
	return (
		dynBitArrayCountTotalBitsInCommon(pBF->aBits, pBF->uiNumBits, pLogicBlock->off.pBits, pLogicBlock->off.uiNumBits) == 0
		&& !dynBitFieldCheckForExtraBits(&pLogicBlock->on, pBF)
		);
}