#include "instringcommands.h"
#include "cmdparse.h"
#include "file.h"
#include "stringutil.h"
#include "utils.h"
#include "earray.h"
#include "Expression.h"



bool GenericIsExpressionStringTrue(char *pString)
{
	Expression *pExpr = exprCreateFromString(pString, NULL);
	static ExprContext *pContext = NULL;
	static ExprFuncTable* pFuncTable;
	MultiVal answer = {0};

	if (!pContext)
	{
		pContext = exprContextCreate();
		pFuncTable = exprContextCreateFunctionTable("InStringCommands");
		exprContextAddFuncsToTableByTag(pFuncTable, "util");
		exprContextSetFuncTable(pContext, pFuncTable);
	}

	exprGenerate(pExpr, pContext);

	exprEvaluate(pExpr, pContext, &answer);

	exprDestroy(pExpr);

	return QuickGetInt(&answer);	
}

InStringCommandsAllCBs sGenericCBs = {0};




int InStringCommands_Apply(char **ppDestEString, InStringCommandsAllCBs *pCBs, void *pUserData)
{
	int iRetVal = 0;
	char *pFoundBraces;
	int i;

	int iStartingLength = estrLength(ppDestEString);
	char *pStartingString = alloca(iStartingLength + 1);

	if (!pCBs)
	{
		pCBs = &sGenericCBs;
	}

	memcpy(pStartingString, (*ppDestEString), iStartingLength + 1);

	//now look for {{ commands
	while (pFoundBraces = strrstr(*ppDestEString, "{{"))
	{
		char *pFoundCommands = pFoundBraces + 2;
		char *pFoundEndBraces = strstr(pFoundCommands, "}}");
		char *pReplacementString = NULL; //estring
		int iToBeReplacedLength;
		int iReplacementLength;
		int iTotalLength = estrLength(ppDestEString);
		int iSuffixLength;
		int iFoundBracesOffset;

		if (!pFoundEndBraces)
		{
			if (pCBs->pFailCB)
			{
				pCBs->pFailCB(STACK_SPRINTF("Malformed {{ command in string %s", pStartingString), pUserData);
			}
			return -1;
		}

		iRetVal++;

		if (strStartsWith(pFoundCommands, INSTRINGCOMMAND_LOADFILE))
		{
			char fileName[MAX_PATH] = {0};
			char fixedUpFileName[MAX_PATH];
			char *pFileBuffer;
			int iLen;
			strncpy(fileName, pFoundCommands + strlen(INSTRINGCOMMAND_LOADFILE), 
				pFoundEndBraces - pFoundCommands - strlen(INSTRINGCOMMAND_LOADFILE));

			
			if (pCBs->pFindFileCB)
			{
				if (!pCBs->pFindFileCB(fileName, fixedUpFileName, pUserData))
				{
					if (pCBs->pFailCB)
					{
						pCBs->pFailCB(STACK_SPRINTF("Couldn't find file %s", fileName), pUserData);
					}
					return -1;
				}
			}
			else
			{
				strcpy(fixedUpFileName, fileName);
			}	

			pFileBuffer = fileAlloc(fixedUpFileName, &iLen);

			if (!pFileBuffer)
			{
				char tempString[1024];
				sprintf(tempString, "File %s didn't exist\n", fixedUpFileName);
				pFileBuffer = strdup(tempString);
			}

			if (pFileBuffer)
			{
				ANALYSIS_ASSUME(pFileBuffer);
				iLen = (int)strlen(pFileBuffer);
				estrSetSize(&pReplacementString, iLen);
				memcpy(pReplacementString, pFileBuffer, iLen);
			}

//this is generally actually bad behavior
//			estrTrimLeadingAndTrailingWhitespace(&pReplacementString);

			free(pFileBuffer);
		}
#if !_PS3
		else if (strStartsWith(pFoundCommands, INSTRINGCOMMAND_SYSCMDOUTPUT))
		{
			char *pCommand = pFoundCommands + strlen(INSTRINGCOMMAND_SYSCMDOUTPUT);
			char *pFullCommandString = 0;
			char *pFileBuffer;
			int iLen;

			*pFoundEndBraces = 0;

			estrStackCreate(&pFullCommandString);
			estrPrintf(&pFullCommandString, "%s > c:\\tempVar.txt", pCommand);

			system(pFullCommandString);

			estrDestroy(&pFullCommandString);


			pFileBuffer = fileAlloc("c:/tempVar.txt", &iLen);
			
			while (IS_WHITESPACE(pFileBuffer[iLen - 1]))
			{
				pFileBuffer[iLen - 1] = 0;
				iLen--;
			}

			estrSetSize(&pReplacementString, iLen);
			memcpy(pReplacementString, pFileBuffer, iLen);

			free(pFileBuffer);
		}		
#endif
		else if (strStartsWith(pFoundCommands, INSTRINGCOMMAND_COMMAND))
		{
			char *pCommand = pFoundCommands + strlen(INSTRINGCOMMAND_COMMAND);

			*pFoundEndBraces = 0;

			cmdParseAndReturn(pCommand, &pReplacementString, CMD_CONTEXT_HOWCALLED_INSTRING_COMMAND);
			if (estrLength(&pReplacementString) == 0)
			{
				if (pCBs->pFailCB)
				{
					pCBs->pFailCB(STACK_SPRINTF("Got no response from command %s... time overflow perhaps?",
						pCommand), pUserData);
				}
			}
		}
		else if (strStartsWith(pFoundCommands, INSTRINGCOMMAND_SUPERESCAPE))
		{
			char *pCommand = pFoundCommands + strlen(INSTRINGCOMMAND_SUPERESCAPE);

			*pFoundEndBraces = 0;

			estrSuperEscapeString(&pReplacementString, pCommand);
		}
		else if (strStartsWith(pFoundCommands, INSTRINGCOMMAND_ALPHANUM))
		{
			char *pCommand = pFoundCommands + strlen(INSTRINGCOMMAND_ALPHANUM);

			*pFoundEndBraces = 0;

			estrCopy2(&pReplacementString, pCommand);
			estrMakeAllAlphaNumAndUnderscores(&pReplacementString);
		}		
		else if (strStartsWith(pFoundCommands, INSTRINGCOMMAND_STRIPNEWLINES))
		{
			char *pCommand = pFoundCommands + strlen(INSTRINGCOMMAND_STRIPNEWLINES);

			*pFoundEndBraces = 0;

			estrCopy2(&pReplacementString, pCommand);
			estrReplaceOccurrences(&pReplacementString, "\n", "");
			estrReplaceOccurrences(&pReplacementString, "\r", "");
		}
#if !_PS3
		else if (strStartsWith(pFoundCommands, INSTRINGCOMMAND_SYSCMDRETVAL))
		{
			char *pCommand = pFoundCommands + strlen(INSTRINGCOMMAND_SYSCMDRETVAL);
			char *pFullCommandString = 0;
			int iRetVal2;

			*pFoundEndBraces = 0;

			estrStackCreate(&pFullCommandString);
			estrPrintf(&pFullCommandString, "%s > c:\\tempVar.txt", pCommand);

			iRetVal2 = system(pFullCommandString);

			estrDestroy(&pFullCommandString);

			estrPrintf(&pReplacementString, "%d", iRetVal2);
		}
#endif
		else if (strStartsWith(pFoundCommands, INSTRINGCOMMAND_EXPAND))
		{
			char *pBeginningOfExpandString = pFoundCommands + strlen(INSTRINGCOMMAND_EXPAND);
			char *pOpeningSquareBraces = strstr(pBeginningOfExpandString, "[[");
			char **ppItemsInList = NULL;
			char *pEndingSquareBraces;

			*pFoundEndBraces = 0;
		
			pEndingSquareBraces = strrstr(pBeginningOfExpandString, "]]");

			if (!(pOpeningSquareBraces && pEndingSquareBraces))
			{
				if (pCBs->pFailCB)
				{
					pCBs->pFailCB(STACK_SPRINTF("Badly formated {{EXPAND command in string %s", pStartingString), pUserData);
				}

				return -1;
			}

			*pEndingSquareBraces = 0;

			//when called on an empty string, DoVariableListSeparation pushes an empty string into the list. We don't want
			//that behavior here, so first make sure the string isn't empty
			if (!StringIsAllWhiteSpace(pOpeningSquareBraces + 2))
			{
				DoVariableListSeparation(&ppItemsInList, pOpeningSquareBraces + 2, false);
			}

			*pOpeningSquareBraces = 0;
			*pFoundEndBraces = 0;

			for (i=0; i < eaSize(&ppItemsInList); i++)
			{
				estrConcatf(&pReplacementString, "%s%s%s", pBeginningOfExpandString, ppItemsInList[i], pEndingSquareBraces + 2);
			}

			eaDestroyEx(&ppItemsInList, NULL);
		}
		else if (strStartsWith(pFoundCommands, INSTRINGCOMMAND_IF))
		{
			char *pReadHead = pFoundCommands + strlen(INSTRINGCOMMAND_IF);
			char *pEndingParens;
			bool bIsTrue;

			*pFoundEndBraces = 0;


			while (IS_WHITESPACE(*pReadHead))
			{
				pReadHead++;
			}

			pEndingParens = strrstr(pFoundCommands, "))");
		
			if (!(strStartsWith(pReadHead, "((") && pEndingParens))
			{
				if (pCBs->pFailCB)
				{
					pCBs->pFailCB(STACK_SPRINTF("Badly formatted {{IF command in string %s", pStartingString), pUserData);
				}

				return -1;
			}

			*pEndingParens = 0;

			if (pCBs->pIsExpressionTrueCB)
			{
				bIsTrue = pCBs->pIsExpressionTrueCB(pReadHead + 2, pUserData);
			}
			else
			{	
				bIsTrue = GenericIsExpressionStringTrue(pReadHead + 2);
			}

			if (bIsTrue)
			{
				*pFoundEndBraces = 0;
				estrConcatf(&pReplacementString, "%s", pEndingParens + 2);
			}
		}
		else
		{
			bool bFound = false;

			for ( i = 0; i < eaSize(&pCBs->ppAuxCommands); i++)
			{
				if (strStartsWith(pFoundCommands, pCBs->ppAuxCommands[i]->pCommand))
				{
					bFound = true;
					*pFoundEndBraces = 0;

					if (!pCBs->ppAuxCommands[i]->pCB(pFoundCommands + strlen(pCBs->ppAuxCommands[i]->pCommand), &pReplacementString, pUserData))
					{
						if (pCBs->pFailCB)
						{
							pCBs->pFailCB(pReplacementString, pUserData);
						}
						estrDestroy(&pReplacementString);
						return -1;
					}

					break;
				}
			}

			if (!bFound)
			{
				if (pCBs->pFailCB)
				{
					pCBs->pFailCB(STACK_SPRINTF("Unknown InStringCommand %s", pFoundCommands), pUserData);
				}

				return -1;	
			}
		}

		iToBeReplacedLength = pFoundEndBraces - pFoundBraces + 2;
		iReplacementLength = estrLength(&pReplacementString);
		iSuffixLength = iTotalLength - (pFoundEndBraces - *ppDestEString + 2);
		iFoundBracesOffset = pFoundBraces - *ppDestEString;

		if (iReplacementLength >= iToBeReplacedLength)
		{
			estrSetSize(ppDestEString, iTotalLength - iToBeReplacedLength + iReplacementLength);
		}

		memmove(*ppDestEString + iFoundBracesOffset + iReplacementLength, *ppDestEString + iFoundBracesOffset + iToBeReplacedLength, iSuffixLength + 1);
		memcpy(*ppDestEString + iFoundBracesOffset, pReplacementString, iReplacementLength);

		if (iReplacementLength < iToBeReplacedLength)
		{
			estrSetSize(ppDestEString, iTotalLength - iToBeReplacedLength + iReplacementLength);
		}


		estrDestroy(&pReplacementString);		

		if (pCBs->pPostReplacementCB)
		{
			if (!pCBs->pPostReplacementCB(ppDestEString, pUserData))
			{
				if (pCBs->pFailCB)
				{
					pCBs->pFailCB("Failed during postReplacment CB", pUserData);
				}

				return -1;	
			}
		}
	}

	return iRetVal;
}