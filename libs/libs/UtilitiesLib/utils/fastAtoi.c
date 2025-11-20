#include "StringUtil.h"
#include "fastAtoi.h"
#include "errno.h"

#undef strtol
#undef atoi
#undef strtoul

#undef _atoi64
#undef atoi64
#undef _strtoi64 

typedef enum eStringToIntResult
{
	S2I_SUCCEED,
	S2I_OUTOFRANGE,
	S2I_MALFORMED
} eStringToIntResult;

static __forceinline eStringToIntResult StringToUInt_Fast(const char *pStr, U32 *pOutInt, char **ppOutEndPtr)
{
	U32 iRetVal = 0;
	char c;
	int iLastDigit = 0;
	bool bNeg = false;

	while (IS_WHITESPACE(*pStr))
	{
		pStr++;
	}

	while (*pStr == '-')
	{
		if (bNeg)
		{
			return S2I_MALFORMED;
		}
		bNeg = !bNeg;
		pStr++;
	}

	c = pStr[0];

	if (!(c >= '0' && c <= '9'))
	{
		return S2I_MALFORMED;
	}
	else
	{
		iRetVal = c - '0';
		c = pStr[1];
		iLastDigit = 0;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[2];
		iLastDigit = 1;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[3];
		iLastDigit = 2;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[4];
		iLastDigit = 3;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[5];
		iLastDigit = 4;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[6];
		iLastDigit = 5;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[7];
		iLastDigit = 6;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[8];
		iLastDigit = 7;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[9];
		iLastDigit = 8;
	if (c >= '0' && c <= '9')
	{
		char d = pStr[10];
		if (d >= '0' && d <= '9')
		{
			goto overflow;
		}

		//catch overflow on the last digit (fortunately the vast majority of calls to this don't have all 10 digits)
		if (iRetVal > UINT_MAX / 10)
		{
			goto overflow;
		}

		iRetVal = iRetVal * 10;
		c -= '0';

		if (iRetVal > (U32)(UINT_MAX - c))
		{
			goto overflow;
		}

		iRetVal += c;
		iLastDigit = 9;
	}}}}}}}}}}

	pStr += iLastDigit + 1;

	if (ppOutEndPtr)
	{
		*ppOutEndPtr = (char*)pStr;
	}

	if (bNeg)
	{
		*pOutInt = -((int)iRetVal);
	}
	else
	{	
		*pOutInt = iRetVal;
	}
	return S2I_SUCCEED;

overflow:
	if (ppOutEndPtr)
	{
		pStr += iLastDigit + 1;
		while (*pStr >= '0' && *pStr <= '9')
		{
			pStr++;
		}

		*ppOutEndPtr = (char*)pStr;
	}

	if (bNeg)
	{
		//for reasons that are unclear to me, strotul("-10000000000") returns 1
		*pOutInt = 1;
	}
	else
	{
		*pOutInt = UINT_MAX;
	}
	return S2I_OUTOFRANGE;


}




static __forceinline eStringToIntResult StringToInt_Fast(const char *pStr, int *pOutInt, char **ppOutEndPtr)
{
	U32 iRetVal = 0;
	char c;
	int iLastDigit = 0;
	bool bNeg = false;

	while (IS_WHITESPACE(*pStr))
	{
		pStr++;
	}

	while (*pStr == '-')
	{
		if (bNeg)
		{
			return S2I_MALFORMED;
		}
		bNeg = !bNeg;
		pStr++;
	}

	c = pStr[0];

	if (!(c >= '0' && c <= '9'))
	{
		return S2I_MALFORMED;
	}
	else
	{
		iRetVal = c - '0';
		c = pStr[1];
		iLastDigit = 0;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[2];
		iLastDigit = 1;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[3];
		iLastDigit = 2;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[4];
		iLastDigit = 3;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[5];
		iLastDigit = 4;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[6];
		iLastDigit = 5;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[7];
		iLastDigit = 6;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[8];
		iLastDigit = 7;
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[9];
		iLastDigit = 8;
	if (c >= '0' && c <= '9')
	{
		char d = pStr[10];
		if (d >= '0' && d <= '9')
		{
			goto overflow;
		}

		//catch overflow on the last digit (fortunately the vast majority of calls to this don't have all 10 digits)
		if (iRetVal > UINT_MAX / 10)
		{
			goto overflow;
		}

		iRetVal = iRetVal * 10;
		c -= '0';

		if (iRetVal > (U32)(UINT_MAX - c))
		{
			goto overflow;
		}

		iRetVal += c;
		iLastDigit = 9;
	}}}}}}}}}}

	pStr += iLastDigit + 1;

	if (ppOutEndPtr)
	{
		*ppOutEndPtr = (char*)pStr;
	}

	if (bNeg)
	{
		if (iRetVal > ((U32)INT_MAX) + 1)
		{
			*pOutInt = INT_MIN;
			return S2I_OUTOFRANGE;
		}
		else
		{
			*pOutInt = -((int)iRetVal);
		}
	}
	else
	{	
		if (iRetVal > ((U32)INT_MAX) )
		{
			*pOutInt = INT_MAX;
			return S2I_OUTOFRANGE;
		}
		else
		{
			*pOutInt = iRetVal;
		}
	}
	
	return S2I_SUCCEED;


	overflow:
	if (ppOutEndPtr)
	{
		pStr += iLastDigit + 1;
		while (*pStr >= '0' && *pStr <= '9')
		{
			pStr++;
		}

		*ppOutEndPtr = (char*)pStr;
	}

	*pOutInt = bNeg ? INT_MIN : INT_MAX;
	return S2I_OUTOFRANGE;
}


static __forceinline S64 fastAtoi64_Signed(const char *pStr)
{
	bool bNeg = false;
	U64 iRetVal = 0;
	char c;

	while (IS_WHITESPACE(*pStr))
	{
		pStr++;
	}

	while (*pStr == '-')
	{
		//windows atoi return 0 for "--1"
		if (bNeg)
		{
			return 0;
		}
		bNeg = !bNeg;
		pStr++;
	}

	while (1)
	{
		c = pStr[0];
		pStr++;

		if (c < '0' || c > '9')
		{
			return bNeg ? -((S64)iRetVal) : iRetVal;
		}

		c -= '0';

		if (iRetVal > (LLONG_MAX / 10))
		{
			goto overflow;
		}

		iRetVal *= 10;

		//note that for LLONG_MIN, we internally think we hit an overflow, but atoi just returns
		//LLONG_MIN on overflow, so it's OK
		if (iRetVal > (U64)(LLONG_MAX - c))
		{
			goto overflow;
		}

		iRetVal += c;
	}

overflow:
	if (bNeg)
	{
		return LLONG_MIN;
	}
	else
	{
		return LLONG_MAX;
	}
}


static __forceinline S32 fastAtoi_Signed(const char *pStr)
{
	U32 iRetVal = 0;
	char c;
	bool bNeg = false;

	while (IS_WHITESPACE(*pStr))
	{
		pStr++;
	}

	while (*pStr == '-')
	{
		//windows atoi return 0 for "--1"
		if (bNeg)
		{
			return 0;
		}
		bNeg = !bNeg;
		pStr++;
	}

	c = pStr[0];

	if (c >= '0' && c <= '9')
	{
		iRetVal = c - '0';
		c = pStr[1];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[2];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[3];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[4];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[5];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[6];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[7];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[8];
	if (c >= '0' && c <= '9')
	{
		iRetVal = iRetVal * 10 + c - '0';
		c = pStr[9];
	if (c >= '0' && c <= '9')
	{
		//windows atoi returns 2147483647 (max int) for all overflows,
		//-2147483648 for underflows

		//first check if the next character is ANOTHER digit, in which case we're automatically over/underflowing
		char d = pStr[10];
		if (d >= '0' && d <= '9')
		{
			return bNeg ? INT_MIN : INT_MAX;
		}
		else
		{
			S64 iRetVal64 = (S64)iRetVal * 10 + c - '0';
			if (bNeg)
			{
				return -iRetVal64 < INT_MIN ? INT_MIN : -iRetVal64;
			}
			else
			{
				return iRetVal64 > INT_MAX ? INT_MAX : iRetVal64;
			}
		}
	}}}}}}}}}}



	if (bNeg)
	{
		return -((S32)iRetVal);
	}
	else
	{
		return iRetVal;
	}
}

static __forceinline eStringToIntResult StringToInt64_Fast(const char *pStr, S64 *pOutInt, char **ppOutEndPtr)
{
	U64 iRetVal = 0;
	char c;
	bool bNeg = false;
	bool bReadOneDigit = false;

	while (IS_WHITESPACE(*pStr))
	{
		pStr++;
	}

	while (*pStr == '-')
	{
		if (bNeg)
		{
			return S2I_MALFORMED;
		}
		bNeg = !bNeg;
		pStr++;
	}

	while (1)
	{
		c = pStr[0];

		if (c < '0' || c > '9')
		{
			if (!bReadOneDigit)
			{
				return S2I_MALFORMED;
			}

			*pOutInt =  bNeg ? -((S64)iRetVal) : iRetVal;
			if (ppOutEndPtr)
			{
				*ppOutEndPtr = (char*)pStr;
			}

			return S2I_SUCCEED;
		}

		c -= '0';
		pStr++;
		bReadOneDigit = true;

		if (iRetVal > (LLONG_MAX / 10))
		{
			goto overflow;
		}

		iRetVal *= 10;
		if (iRetVal > (U64)(LLONG_MAX - c +( bNeg ? 1 : 0)))
		{
			goto overflow;
		}

		iRetVal += c;
	}

overflow:
	if (ppOutEndPtr)
	{
		while (*pStr >= '0' && *pStr <= '9')
		{
			pStr++;
		}

		*ppOutEndPtr = (char*)pStr;
	}

	*pOutInt = bNeg ? LLONG_MIN : LLONG_MAX;
	return S2I_OUTOFRANGE;



}




S32 strtol_fast( const char * str, char ** endptr, int base )
{
	if (base != 10)
	{
		return strtol( str, endptr, base);
	}
	else
	{
		S32 iResult;
		eStringToIntResult eResult = StringToInt_Fast(str, &iResult, endptr);

		switch (eResult)
		{
		case S2I_SUCCEED:
			return iResult;

		case S2I_OUTOFRANGE:
			errno = ERANGE;
			return iResult;

		case S2I_MALFORMED:
			if (endptr)
			{
				*endptr = (char*)str;
			}
			return 0;
		}
	}

	return 0;
}


U32 strtoul_fast( const char * str, char ** endptr, int base )
{
	if (base != 10)
	{
		return strtoul( str, endptr, base);
	}
	else
	{
		U32 iResult;
		eStringToIntResult eResult = StringToUInt_Fast(str, &iResult, endptr);

		switch (eResult)
		{
		case S2I_SUCCEED:
			return iResult;

		case S2I_OUTOFRANGE:
			errno = ERANGE;
			return iResult;

		case S2I_MALFORMED:
			if (endptr)
			{
				*endptr = (char*)str;
			}
			return 0;
		}
	}

	return 0;
}

int atoi_fast( const char * str )
{
	return fastAtoi_Signed(str);
}

S64 atoi64_fast( const char *str )
{
	return fastAtoi64_Signed(str);
}

S64 strtoi64_fast( const char *str, char **endptr, int base)
{
	if (base != 10)
	{
		return _strtoi64( str, endptr, base);
	}
	else
	{
		S64 iResult;
		eStringToIntResult eResult = StringToInt64_Fast(str, &iResult, endptr);

		switch (eResult)
		{
		case S2I_SUCCEED:
			return iResult;

		case S2I_OUTOFRANGE:
			errno = ERANGE;
			return iResult;

		case S2I_MALFORMED:
			if (endptr)
			{
				*endptr = (char*)str;
			}
			return 0;
		}
	}

	return 0;


}


/*
char *pAtoiTestNums[] = 
{
	"",
	" ",
	"0",
	" 0",
	" 1",
	" -1",
	" --1",
	" 1a"
	" 1 1",
	"\t1 1",
	"\n1 1",
	"1000000000",
	"-1000000000",
	"3000000000",
	"-3000000000",
	"10000000000",
	"4294967295",
	"4294967296",
	"f0",
	" f0",
	"-10000000000",
	"2147483646",
	"2147483647",
	"2147483648",
	"-2147483646",
	"-2147483647",
	"-2147483648",
	"-2147483649",
	"2147483646 ",
	"2147483646a ",
	"2147483646 a",
	"9223372036854775806",
	"9223372036854775807",
	"9223372036854775808",
	"9223372036854775809",
	"92233720368547758080000",
	"-9223372036854775806",
	"-9223372036854775807",
	"-9223372036854775808",
	"-9223372036854775809",
	"-92233720368547758080000",

	
};

AUTO_RUN;
void DoAtoiTest(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(pAtoiTestNums); i++)
	{
		int iInt1, iInt2;
		U32 iUInt1, iUInt2;
		char *pPtr1, *pPtr2;
		S64 iS641, iS642;
		U32 iErrno1, iErrno2;
		char *pStr = pAtoiTestNums[i];

		iInt1 = atoi(pStr);
		iInt2 = atoi_fast(pStr);

		assertmsgf(iInt1 == iInt2, "for input <<%s>>, atoi returns %d but atoi_fast returns %d\n", pStr, iInt1, iInt2);

		errno = 0;
		iInt1 = strtol(pStr, &pPtr1, 10);
		iErrno1 = errno;

		errno = 0;
		iInt2 = strtol_fast(pStr, &pPtr2, 10);
		iErrno2 = errno;

		assertmsgf(iInt1 == iInt2 && iErrno1 == iErrno2 && pPtr1 == pPtr2, "For input <<%s>>, strtol returns %d (errno %d, ptr %d), but strtol_fast returns %d (errno %d, ptr %d)\n",
			pStr, iInt1, iErrno1, pPtr1 - pStr, iInt2, iErrno2, pPtr2 - pStr);

		errno = 0;
		iUInt1 = strtoul(pStr, &pPtr1, 10);
		iErrno1 = errno;

		errno = 0;
		iUInt2 = strtoul_fast(pStr, &pPtr2, 10);
		iErrno2 = errno;

		assertmsgf(iUInt1 == iUInt2 && iErrno1 == iErrno2 && pPtr1 == pPtr2, "For input <<%s>>, strtoul returns %u (errno %d, ptr %d), but strtoul_fast returns %u (errno %d, ptr %d)\n",
			pStr, iUInt1, iErrno1, pPtr1 - pStr, iUInt2, iErrno2, pPtr2 - pStr);


		iS641 = _atoi64(pStr);
		iS642 = atoi64_fast(pStr);

		assertmsgf(iS641 == iS642, "for input <<%s>>, _atoi64 returns %"FORM_LL"d but atoi64_fast returns %"FORM_LL"d\n", pStr, iS641, iS642);

		errno = 0;
		iS641 = _strtoi64(pStr, &pPtr1, 10);
		iErrno1 = errno;

		errno = 0;
		iS642 = strtoi64_fast(pStr, &pPtr2, 10);
		iErrno2 = errno;

		assertmsgf(iS641 == iS642 && iErrno1 == iErrno2 && pPtr1 == pPtr2, "For input <<%s>>, _stroi64 returns %"FORM_LL"d (errno %d, ptr %d), but strtoi64_fast returns %"FORM_LL"d (errno %d, ptr %d)\n",
			pStr, iS641, iErrno1, pPtr1 - pStr, iS642, iErrno2, pPtr2 - pStr);




	}


}*/