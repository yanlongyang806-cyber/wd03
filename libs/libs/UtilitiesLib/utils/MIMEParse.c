#include "MimeParse.h"
#include "utils.h"
#include "estring.h"
#include "Earray.h"
#include "stringUtil.h"
#include "MimeParse_h_Ast.h"

void GetMIMEExtraLineSegmentsRecurse(MIMEHeaderLine *pLine, char *pInString)
{
	char *pNextSemicolon = strchr(pInString, ';');
	char *pInternalString = NULL;
	char *pEquals;

	MIMEHeaderLineSegment *pSegment = StructCreate(parse_MIMEHeaderLineSegment);	

	if (pNextSemicolon)
	{
		estrInsert(&pInternalString, 0, pInString, pNextSemicolon - pInString);
	}
	else
	{
		estrCopy2(&pInternalString, pInString);
	}

	estrTrimLeadingAndTrailingWhitespace(&pInternalString);

	if ((pEquals = strchr(pInternalString, '=')))
	{
		estrInsert(&pSegment->pNameOrEntireString, 0, pInternalString, pEquals - pInternalString);
		estrCopy2(&pSegment->pValue, pEquals + 1);

		estrTrimLeadingAndTrailingWhitespace(&pSegment->pNameOrEntireString);
		estrTrimLeadingAndTrailingWhitespace(&pSegment->pValue);

		if (pSegment->pValue[0] == '"' && pSegment->pValue[estrLength(&pSegment->pValue) - 1] == '"')
		{
			estrRemove(&pSegment->pValue, 0, 1);
			estrRemove(&pSegment->pValue, estrLength(&pSegment->pValue) - 1, 1);
	
			estrTrimLeadingAndTrailingWhitespace(&pSegment->pValue);
		}


	}
	else
	{
		estrCopy2(&pSegment->pNameOrEntireString, pInternalString);
	}

	eaPush(&pLine->ppSegments, pSegment);

	estrDestroy(&pInternalString);
	if (pNextSemicolon)
	{
		GetMIMEExtraLineSegmentsRecurse(pLine, pNextSemicolon + 1);
	}
}

MIMEHeaderLine *GetMIMEHeaderLineFromString(char *pInString)
{
	MIMEHeaderLine *pRetVal = StructCreate(parse_MIMEHeaderLine);
	char *pColon;
	char *pRightOfColon;
	char *pSemiColon;
	int iInLength = (int)strlen(pInString);
		
		//special case for From line 
	if (strStartsWith(pInString, "From "))
	{
		estrPrintf(&pRetVal->pName, "From");
		estrCopy2(&pRetVal->pValue, pInString + 5);
		return pRetVal;
	}

	pColon = strchr(pInString, ':');

	if (!pColon)
	{
		estrPrintf(&pRetVal->pErrors, "Didn't find colon in presumed MIME header line begninning: ");
		estrConcat(&pRetVal->pErrors, pInString, iInLength > 20 ? 20 : iInLength);
		return pRetVal;
	}

	estrInsert(&pRetVal->pName, 0, pInString, pColon - pInString);

	pRightOfColon = pColon + 1;

	if ((pSemiColon = strchr(pRightOfColon, ';')))
	{
		estrInsert(&pRetVal->pValue, 0, pRightOfColon, pSemiColon - pRightOfColon);
		estrTrimLeadingAndTrailingWhitespace(&pRetVal->pValue);

		GetMIMEExtraLineSegmentsRecurse(pRetVal, pSemiColon + 1);
	}
	else
	{
		estrCopy2(&pRetVal->pValue, pRightOfColon);
		estrTrimLeadingAndTrailingWhitespace(&pRetVal->pValue);
	}

	return pRetVal;
}

int ReadMIMEHeader(MIMEHeaderLine ***pppOutHeader, char **ppInLines)
{
	int iReadLineIndex = 0;
	int iLinesInCurLine = 0;
	int iNumSourceLines = eaSize(&ppInLines);

	while (iReadLineIndex < iNumSourceLines && !StringIsAllWhiteSpace(ppInLines[iReadLineIndex]))
	{
		char *pWorkingLine = NULL;

		estrCopy2(&pWorkingLine, ppInLines[iReadLineIndex]);

		iReadLineIndex++;

		while (iReadLineIndex < iNumSourceLines && (ppInLines[iReadLineIndex][0] == ' ' || ppInLines[iReadLineIndex][0] == '\t'))
		{
			estrConcatf(&pWorkingLine, "%s", ppInLines[iReadLineIndex]);
			iReadLineIndex++;
		}

		eaPush(pppOutHeader, GetMIMEHeaderLineFromString(pWorkingLine));

		estrDestroy(&pWorkingLine);
	}

	return iReadLineIndex;
}

char *GetMIMEHeaderLineValue(MIMEHeaderLine ***pppHeader, char *pLineName)
{
	int i;

	for (i=0; i < eaSize(pppHeader); i++)
	{
		if (stricmp((*pppHeader)[i]->pName, pLineName) == 0)
		{
			return (*pppHeader)[i]->pValue;
		}
	}

	return NULL;
}

char *GetMIMEHeaderLineSegmentValue(MIMEHeaderLine ***pppHeader, char *pLineName, char *pSegmentName)
{

	int i;

	for (i=0; i < eaSize(pppHeader); i++)
	{
		MIMEHeaderLine *pLine = (*pppHeader)[i];
		if (stricmp(pLine->pName, pLineName) == 0)
		{
			int j;

			for (j=0; j < eaSize(&pLine->ppSegments); j++)
			{
				if (stricmp(pLine->ppSegments[j]->pNameOrEntireString, pSegmentName) == 0)
				{
					return pLine->ppSegments[j]->pValue;
				}
			}
		}
	}
			
	return NULL;
}



#include "MimeParse_h_Ast.c"
