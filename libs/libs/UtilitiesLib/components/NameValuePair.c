#include "NameValuePair.h"
#include "estring.h"
#include "earray.h"
#include "stringutil.h"
#include "textparser.h"

#include "NameValuePair_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping("nameValuePair.h", BUDGET_EngineMisc););


typedef enum eParseState
{
	PARSESTATE_INWHITESPACE,
	PARSESTATE_INTOKEN,
	PARSESTATE_INQUOTES,
} eParseState;



bool StringIsLegalTokenForPurposesOfNameValuePairs(char *pString)
{
	int iLen = (int)strlen(pString);
	int i;
	
	bool bAtLeastOneAlphaNumOrUnderscore = false;

	if (!iLen)
	{
		return false;
	}

	for (i=0; i < iLen; i++)
	{
		if (isalnum(pString[i]) || pString[i] == '_')
		{
			bAtLeastOneAlphaNumOrUnderscore = true;
		}
		else if (pString[i] == '.' || pString[i] == ':')
		{
			
		}
		else
		{
			return false;
		}
	}

	return true;
}
			

#define NVP_ISWHITSPACE(c) (IS_WHITESPACE(c) || (pExtraWhiteSpaceChars && strchr_fast(pExtraWhiteSpaceChars, (c))))


bool GetNameValuePairsFromString_dbg(const char *pString, NameValuePair ***pppPairs, char *pExtraWhiteSpaceChars,
	const char *caller_fname, int line)
{
	char **ppTokens = NULL;
	eParseState eCurState = PARSESTATE_INWHITESPACE;
	const char *pReadHead = pString;
	const char *pCurBeginning = pString;
	bool bFailed = false;


	int iFirstToken;
	int iLastToken;
	int iOpenParensIndex = -1;
	int iCloseParensIndex = -1;

	int i, j;
	int iNumTokens = eaSize(&ppTokens);
	int iNumTokensToPairify;
	int iNumPairs;


	do
	{
		switch (eCurState)
		{
		case PARSESTATE_INWHITESPACE:
			if (!(*pReadHead))
			{
				break;
			}
			else if (*pReadHead == ')' || *pReadHead == '(')
			{
				char tempString[2];
				sprintf(tempString, "%c", *pReadHead);
				eaPush_dbg(&ppTokens, strdup_dbg(tempString MEM_DBG_PARMS_CALL) MEM_DBG_PARMS_CALL);
			}
			else if (*pReadHead == '"')
			{
				eCurState = PARSESTATE_INQUOTES;
				pCurBeginning = pReadHead + 1;
			}
			else if (!NVP_ISWHITSPACE(*pReadHead))
			{
				eCurState = PARSESTATE_INTOKEN;
				pCurBeginning = pReadHead;
			}
			break;

		case PARSESTATE_INTOKEN:
			if (*pReadHead == 0 || *pReadHead == '(' || *pReadHead == ')' || *pReadHead == '"' || NVP_ISWHITSPACE(*pReadHead))
			{
				int iLen = pReadHead - pCurBeginning;
				char *pNewString = smalloc(iLen + 1);
				strncpy_s(pNewString, iLen + 1, pCurBeginning, iLen);
				eaPush_dbg(&ppTokens, pNewString MEM_DBG_PARMS_CALL);

				if (*pReadHead == ')')
				{
					char tempString[2];
					sprintf(tempString, "%c", *pReadHead);
					eaPush_dbg(&ppTokens, strdup_dbg(tempString MEM_DBG_PARMS_CALL) MEM_DBG_PARMS_CALL);
					eCurState = PARSESTATE_INWHITESPACE;
					break;
				}

				if (*pReadHead == '(')
				{
					char tempString[2];
					sprintf(tempString, "%c", *pReadHead);
					eaPush_dbg(&ppTokens, strdup_dbg(tempString MEM_DBG_PARMS_CALL) MEM_DBG_PARMS_CALL);
					eCurState = PARSESTATE_INWHITESPACE;
					break;
				}

				if (*pReadHead == '"')
				{
					eCurState = PARSESTATE_INQUOTES;
					pCurBeginning = pReadHead + 1;
					break;
				}

				eCurState = PARSESTATE_INWHITESPACE;
			}
			break;

		case PARSESTATE_INQUOTES:
			if (*pReadHead == 0)
			{
				bFailed = true;
				goto Failed;
			}
			else if (*pReadHead == '"')
			{
				int iLen = pReadHead - pCurBeginning;
				char *pNewString = smalloc(iLen + 1);
				if (iLen)
				{
					strncpy_s(pNewString, iLen + 1, pCurBeginning, iLen);
				}
				else
				{
					*pNewString = 0;
				}

				eaPush_dbg(&ppTokens, pNewString MEM_DBG_PARMS_CALL);
				eCurState = PARSESTATE_INWHITESPACE;
				break;
			}
		}
		if (*pReadHead == 0)
		{
			break;
		}

		pReadHead++;
	} 
	while (1);


	
	iNumTokens = eaSize(&ppTokens);	
		

	for (i=0; i < iNumTokens; i++)
	{
		if (ppTokens[i][0] == '(')
		{
			if (iOpenParensIndex != -1)
			{
				bFailed = true;
				goto Failed;
			}
		
			iOpenParensIndex = i;
			
		}
		else if (ppTokens[i][0] == ')')
		{
			if (iOpenParensIndex == -1 || iCloseParensIndex != -1)
			{
				bFailed = true;
				goto Failed;
			}

			iCloseParensIndex = i;
		}
	}

	if (iOpenParensIndex != -1)
	{
		if (iCloseParensIndex == -1)
		{
			bFailed = true;
			goto Failed;
		}
		
		iFirstToken = iOpenParensIndex + 1;
		iLastToken = iCloseParensIndex - 1;
	}
	else
	{
		iFirstToken = 0;
		iLastToken = iNumTokens - 1;
	}

	

	iNumTokensToPairify = iLastToken - iFirstToken + 1;
	if (iNumTokensToPairify < 2 || iNumTokensToPairify & 1)
	{
		bFailed = true;
		goto Failed;
	}

	iNumPairs = iNumTokensToPairify / 2;

	for (i=0; i < iNumPairs; i++)
	{
		if (!StringIsLegalTokenForPurposesOfNameValuePairs(ppTokens[iFirstToken + i * 2]))
		{
			bFailed = true;
			goto Failed;
		}

		for (j = i + 1; j < iNumPairs; j++)
		{
			if (stricmp(ppTokens[iFirstToken + i*2], ppTokens[iFirstToken + j*2]) == 0)
			{
				bFailed = true;
				goto Failed;
			}
		}
	}


	//no more failures possible
	for (i=0; i < iNumPairs; i++)
	{
		NameValuePair *pPair = StructCreate(parse_NameValuePair);
		pPair->pName = ppTokens[iFirstToken + i * 2];
		pPair->pValue = ppTokens[iFirstToken + i * 2 + 1];

		ppTokens[iFirstToken + i * 2] = NULL;
		ppTokens[iFirstToken + i * 2 + 1] = NULL;

		eaPush_dbg(pppPairs, pPair MEM_DBG_PARMS_CALL);
	}



Failed:
	eaDestroyEx(&ppTokens, NULL);

	return !bFailed;
}

char *GetValueFromNameValuePairs(NameValuePair ***pppPairs, const char *pName)
{
	int i;

	for (i=0; i < eaSize(pppPairs); i++)
	{
		if (stricmp(pName, (*pppPairs)[i]->pName) == 0)
		{
			return (*pppPairs)[i]->pValue;
		}
	}

	return NULL;
}

void UpdateOrSetValueInNameValuePairList(NameValuePair ***pppPairs, const char *pName, const char *pValue)
{
	int i;
	NameValuePair *pNewPair;

	for (i=0; i < eaSize(pppPairs); i++)
	{
		if (stricmp(pName, (*pppPairs)[i]->pName) == 0)
		{
			SAFE_FREE((*pppPairs)[i]->pValue);
			(*pppPairs)[i]->pValue = pValue ? strdup(pValue) : NULL;
			return;
		}
	}

	pNewPair = StructCreate(parse_NameValuePair);
	pNewPair->pName = strdup(pName);
	
	if (pValue)
	{
		pNewPair->pValue = strdup(pValue);
	}

	eaPush(pppPairs, pNewPair);
}
	
void RemovePairFromNameValuePairList(NameValuePair ***pppPairs, const char *pName)
{
	int i;

	for (i=0; i < eaSize(pppPairs); i++)
	{
		if (stricmp(pName, (*pppPairs)[i]->pName) == 0)
		{
			StructDestroy(parse_NameValuePair,  (*pppPairs)[i]);
			eaRemove(pppPairs, i);
			return;
		}
	}
}





#include "NameValuePair_h_ast.c"
