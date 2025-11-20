#pragma once

#include "referenceSystem.h"

static __forceinline bool ParserTableHasDirtyBitAndGetIt_Inline(ParseTable table[], const void *pStruct, bool *pOutBit)
{
	ParseTableInfo *pTPIInfo = ParserGetTableInfo(table);

	if (pTPIInfo)
	{
		if (pTPIInfo->bHasDirtyBit)
		{
			*pOutBit = (bool)(((U8*)pStruct)[pTPIInfo->iDirtyBitOffset]);
			return true;
		}
	}

	return false;
}

//no optimization possible... do callback
#define ALWAYS_COMPARE 0

//field is irrelevant, always skip
#define NEVER_COMPARE -1

//this is a pointer. If both sides are equal AND are not null, then
//they match, otherwise do callback
#define SPECIAL_REFERENCE_COMPARE -2

#define STRING_COMPARE -3

#define STRING_INDIRECT_COMPARE -4

#define BIT_COMPARE -5

static __forceinline int GetSimpleByteCompareSizeFromType(StructTypeField eType, intptr_t param)
{
	int eStorageType;

	switch (TOK_GET_TYPE(eType))
	{
	case TOK_IGNORE:
	case TOK_START:
	case TOK_END:
		return NEVER_COMPARE;
	}

	eStorageType = TokenStoreGetStorageType(eType);

	switch (eStorageType)
	{
	case TOK_STORAGE_DIRECT_SINGLE:
		{
			switch (TOK_GET_TYPE(eType))
			{
			case TOK_U8_X:
			case TOK_BOOL_X:
			case TOK_BOOLFLAG_X:
				return 1;
			case TOK_INT16_X:
				return 2;
			case TOK_INT_X:
			case TOK_F32_X:
			case TOK_TIMESTAMP_X:
			case TOK_LINENUM_X:		
				return 4;
			case TOK_INT64_X:
				return 8;
			case TOK_STRING_X:
				return STRING_COMPARE;
			case TOK_COMMAND:
				return NEVER_COMPARE;
			case TOK_BIT:
				return BIT_COMPARE;
			}
		}
		break;
	case TOK_STORAGE_INDIRECT_SINGLE:
		{
			switch (TOK_GET_TYPE(eType))
			{
			case TOK_STRING_X:
			case TOK_FILENAME_X:
				return STRING_INDIRECT_COMPARE;

			case TOK_REFERENCE_X:
				return SPECIAL_REFERENCE_COMPARE;

			}
		}
		break;

	case TOK_STORAGE_DIRECT_FIXEDARRAY:
		{
			switch (TOK_GET_TYPE(eType))
			{
			case TOK_U8_X:
				return 1 * param;
			case TOK_INT16_X:
				return 2 * param;
			case TOK_INT_X:
			case TOK_F32_X:
			case TOK_QUATPYR_X:
			case TOK_MATPYR_X:
				return 4 * param;
			case TOK_INT64_X:
				return 8 * param;
			}
		}
		break;
	}

	return ALWAYS_COMPARE;
}

static int simpleEarrayElementCompareParams(ParseTable tpi[], int column,const void* lhs, const void* rhs,void **left_start,void **right_start,int *size)
{
	StructTypeField eType;

	//for arrays of ints, floats, or poolstrings, we can do a quick pass-check
	eType = TOK_GET_TYPE(tpi[column].type);
	if (eType == TOK_INT_X || eType == TOK_F32_X || eType == TOK_STRING_X && (tpi[column].type & TOK_POOL_STRING))
	{
		void ***pppLeftEArray = (void***)((intptr_t)lhs + tpi[column].storeoffset);
		void ***pppRightEArray = (void***)((intptr_t)rhs + tpi[column].storeoffset);

		*left_start = *pppLeftEArray;
		*right_start = *pppRightEArray;
		*size = eType == TOK_STRING_X ? sizeof(intptr_t) : 4;
		return 1;
	}
	return 0;
}

static __forceinline int FieldsMightDiffer(ParseTable pti[],int i, const void *structptr1, const void *structptr2)
{
	int	iSize;
	StructTypeField type = pti[i].type;
	int eStorageType = TokenStoreGetStorageType(type);

	if (!structptr1)
		return 1;
	if (eStorageType == TOK_STORAGE_DIRECT_EARRAY || eStorageType == TOK_STORAGE_INDIRECT_EARRAY)
	{
		U8	*left_start,*right_start;
		int	size;
		int numold = structptr1?TokenStoreGetNumElems_inline(pti, &pti[i], i, structptr1, NULL):0;
		int numnew = TokenStoreGetNumElems_inline(pti, &pti[i], i, structptr2, NULL);
		if (numold != numnew)
			return 1;

		if (simpleEarrayElementCompareParams(pti,i,structptr1,structptr2,&left_start,&right_start,&size))
		{
			if (memcmp(left_start, right_start, numnew * size) == 0)
				return 0;
			else
				return 1;
		}
		return -1;
	}

	iSize = GetSimpleByteCompareSizeFromType(type, pti[i].param);

	switch (iSize)
	{
	case ALWAYS_COMPARE:
		return -1;
	case NEVER_COMPARE:
		return 0;
	case SPECIAL_REFERENCE_COMPARE:
		{
			size_t iOffset = pti[i].storeoffset;
			void **pp1 = (void**)((intptr_t*)((char*)structptr1 + iOffset));
			void **pp2 = (void**)((intptr_t*)((char*)structptr2 + iOffset));

			if (*pp1 != *pp2)
			{
				return 1;
			}

			if (*pp1 == REFERENT_SET_BUT_ABSENT)
			{
				return 1;
			}

			return 0;
		}
		break;
	case STRING_COMPARE:
		{
			size_t iOffset = pti[i].storeoffset;
			if (strcmp((char*)structptr1 + iOffset, (char*)structptr2 + iOffset) == 0)
				return 0;
		}
		break;
	case STRING_INDIRECT_COMPARE:
		{
			char	**s1,**s2;
			size_t iOffset = pti[i].storeoffset;
			s1 = (char **)((char*)structptr1 + iOffset);
			s2 = (char **)((char*)structptr2 + iOffset);
			if (*s1 == *s2)
				return 0;
			if (!*s1 || !*s2)
				return 1;
			if (strcmp(*s1, *s2) == 0)
				return 0;
		}
		break;
	case BIT_COMPARE:
		{
			int iBit;
			int iCount;
			size_t iWord = pti[i].storeoffset >> 2;
			U32 w1 = ((U32*)structptr1)[iWord];
			U32 w2 = ((U32*)structptr2)[iWord];

			if (w1 == w2)
				return 0;
			iBit = TextParserBitField_GetBitNum(pti, i);
			iCount  = TextParserBitField_GetBitCount(pti, i);
			if (iCount == 32)
				return w1 ^ w2;
			else
				return ((w1 >> iBit) & ((1 << iCount) - 1)) ^ ((w2 >> iBit) & ((1 << iCount) - 1));
		}
		break;
	default:
		{
			size_t iOffset = pti[i].storeoffset;
			if (memcmp((char*)structptr1 + iOffset, (char*)structptr2 + iOffset, iSize) == 0)
				return 0;
		}
	}
	return 1;
}
