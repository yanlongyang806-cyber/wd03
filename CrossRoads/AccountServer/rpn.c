#include "rpn.h"
#include "rpn_h_ast.h"
#include "timing_profiler.h"
#include "EString.h"
#include "earray.h"
#include "StringUtil.h"
#include "accountnet.h"

// Get the priority of an operator
static int getOperatorPriority(const char *string)
{
	switch (*string)
	{
	case '|': 
		if (*(string + 1) == '|') return *(string + 2) ? 0 : 1;
		//	return *(string + 1) ? 0 : 3;
		return 0;
	case '&':
		if (*(string + 1) == '&') return *(string + 2) ? 0 : 2;
		//	return *(string + 1) ? 0 : 5;
		return 0;
		//case '^':
		//	return *(string + 1) ? 0 : 4;
	case '=':
		if (*(string + 1) == '=') return *(string + 2) ? 0 : 6;
		return 0;
	case '<':
		if (*(string + 1) == '=') return *(string + 2) ? 0 : 7;
		//	if (*(string + 1) == '<') return *(string + 2) ? 0 : 8;
		return *(string + 1) ? 0 : 7;
	case '>':
		if (*(string + 1) == '=') return *(string + 2) ? 0 : 7;
		//	if (*(string + 1) == '>') return *(string + 2) ? 0 : 8;
		return *(string + 1) ? 0 : 7;
	case '+':
		return *(string + 1) ? 0 : 9;
	case '-':
		return *(string + 1) ? 0 : 9;
	case '*':
		return *(string + 1) ? 0 : 10;
	case '/':
		return *(string + 1) ? 0 : 10;
	case '%':
		return *(string + 1) ? 0 : 10;
	case '!':
		if (*(string + 1) == '=') return *(string + 2) ? 0 : 6;
		return *(string + 1) ? 0 : 11;
		//case '~':
		//	return *(string + 1) ? 0 : 11;
	}
	return 0;
}

// Used by getRPNStack
#define RPN_IS_OPERATOR_PART(c)				(( !!strchr("|&^=<>+-*/%!~()", (c))								))
#define RPN_IS_VARIABLE_START(c)			(( ((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z')		))
#define RPN_IS_QUOTE(c)						(( (c) == '"'													))
#define RPN_IS_ESCAPE_CHAR(c)				(( (c) == '\\'													))
#define RPN_IS_FORBIDDEN_VARIABLE_PART(c)	(( (c) == '(' || (c) == ')' || IS_WHITESPACE(c)                 ))
#define RPN_IS_LITERAL_PART(c)				(( ((c) >= '0' && (c) <= '9') || (c) == '.'						))
#define RPN_IS_SUBSTITUTION_SIGIL(c)		(( (c) == '$'													))

// Get an RPN stack from an infix notation string
// peaRPNStackOut will be filled with an earray of estrings that contains the RPN stack
RPNParseResult infixToRPN(SA_PARAM_OP_STR const char *pInfixString,
						  STRING_EARRAY *peaRPNStackOut,
						  SA_PARAM_OP_VALID int *piErrorIndex)
{
	typedef enum {
		RPNSTATE_QuotedLiteral,
		RPNSTATE_Literal,
		RPNSTATE_Variable,
		RPNSTATE_Unknown,
		RPNSTATE_Operator,
	} RPNState;
	
	RPNParseResult eResult = RPNPR_Invalid;
	RPNState eRPNState = RPNSTATE_Unknown;
	STRING_EARRAY eaOperatorStack = NULL;
	char *pCurString = NULL;
	int iInfixIndex = 0;
	int iSolveError = 0;

	if (!peaRPNStackOut)
	{
		return RPNPR_InvalidOutPointer;
	}

	if (*peaRPNStackOut)
	{
		return RPNPR_InvalidOutPointer;
	}

	// If no infix, then no RPN either
	if (!pInfixString)
	{
		return RPNPR_Success;
	}

	// Not designed to be fast or called often, but might as well see
	PERFINFO_AUTO_START_FUNC();

	// Loop through the infix string, one character at a time (including the NULL terminator).
	while (iInfixIndex <= (int)strlen(pInfixString))
	{
		enum {
			RPNA_TryAsDifferentMode,
			RPNA_AddToWorkingString,
			RPNA_SkipCurrentChar,
		} eAction = RPNA_AddToWorkingString; // Action to be performed with current infix character

		RPNState eOldRPNState = eRPNState; // Used for a devassert later

		// Decide what to do based on the current state
		switch (eRPNState)
		{
		case RPNSTATE_QuotedLiteral: // Ex: "bob"
			if (!pInfixString[iInfixIndex] ||
				(RPN_IS_QUOTE(pInfixString[iInfixIndex]) && !RPN_IS_ESCAPE_CHAR(pInfixString[iInfixIndex - 1])))
			{
				char *temp = NULL;
				estrAppendUnescaped(&temp, pCurString);
				estrDestroy(&pCurString);
				eaPush(peaRPNStackOut, temp);
				pCurString = NULL;

				eRPNState = RPNSTATE_Unknown;
				eAction = RPNA_SkipCurrentChar;
			}

		xcase RPNSTATE_Literal: // Ex: 5
			if (!pInfixString[iInfixIndex] ||
				!RPN_IS_LITERAL_PART(pInfixString[iInfixIndex]))
			{
				eaPush(peaRPNStackOut, pCurString);
				pCurString = NULL;

				eRPNState = RPNSTATE_Unknown;
				eAction = RPNA_TryAsDifferentMode;
			}

		xcase RPNSTATE_Variable: // Ex: x
			if (!pInfixString[iInfixIndex] ||
				RPN_IS_FORBIDDEN_VARIABLE_PART(pInfixString[iInfixIndex]))
			{
				eaPush(peaRPNStackOut, pCurString);
				eaPush(peaRPNStackOut, estrDup("$"));
				pCurString = NULL;

				eRPNState = RPNSTATE_Unknown;
				eAction = RPNA_TryAsDifferentMode;
			}

		xcase RPNSTATE_Operator: // Ex: +=
			if (!pInfixString[iInfixIndex] ||
				!RPN_IS_OPERATOR_PART(pInfixString[iInfixIndex]))
			{
				if (stricmp_safe(pCurString, "(") == 0)
				{
					eaPush(&eaOperatorStack, pCurString);
					pCurString = NULL;
				}
				else if (stricmp_safe(pCurString, ")") == 0)
				{
					char *op = eaPop(&eaOperatorStack);
					while (op && stricmp_safe(op, "(") != 0)
					{
						eaPush(peaRPNStackOut, op);
						op = eaPop(&eaOperatorStack);
					}

					if (!op) // No opening parenthesis
					{
						eResult = RPNPR_MismatchedParen;
						goto error;
					}

					estrDestroy(&op);
					estrDestroy(&pCurString);
				}
				else
				{
					// Get the priority of the current operator
					int priority = getOperatorPriority(pCurString);
					if (!priority) // Invalid operator
					{
						eResult = RPNPR_InvalidOperator;
						goto error;
					}

					// Push all higher-priority operators onto the stack before pushing the current one
					while (eaSize(&eaOperatorStack) > 0 &&
						stricmp_safe(eaOperatorStack[eaSize(&eaOperatorStack) - 1], "(") != 0 &&
						getOperatorPriority(eaOperatorStack[eaSize(&eaOperatorStack) - 1]) >= priority)
					{
						eaPush(peaRPNStackOut, eaPop(&eaOperatorStack));
					}

					eaPush(&eaOperatorStack, pCurString);
					pCurString = NULL;
				}

				eRPNState = RPNSTATE_Unknown;
				eAction = RPNA_TryAsDifferentMode;
			}

		xcase RPNSTATE_Unknown: // Unknown yet
			if (pInfixString[iInfixIndex] &&
				!IS_WHITESPACE(pInfixString[iInfixIndex]))
			{
				if (RPN_IS_OPERATOR_PART(pInfixString[iInfixIndex]))
				{
					eRPNState = RPNSTATE_Operator;
					eAction = RPNA_TryAsDifferentMode;
				}
				else if (RPN_IS_VARIABLE_START(pInfixString[iInfixIndex]))
				{
					eRPNState = RPNSTATE_Variable;
					eAction = RPNA_TryAsDifferentMode;
				}
				else if (RPN_IS_QUOTE(pInfixString[iInfixIndex]))
				{
					eRPNState = RPNSTATE_QuotedLiteral;
					eAction = RPNA_SkipCurrentChar;
				}
				else if (RPN_IS_LITERAL_PART(pInfixString[iInfixIndex]))
				{
					eRPNState = RPNSTATE_Literal;
					eAction = RPNA_TryAsDifferentMode;
				}
				else if (RPN_IS_SUBSTITUTION_SIGIL(pInfixString[iInfixIndex]))
				{
					eRPNState = RPNSTATE_Variable;
					eAction = RPNA_TryAsDifferentMode;
				}
				else // Trash
				{
					eResult = RPNPR_UnknownCharacter;
					goto error;
				}
			}
			else // NULL or whitespace
			{
				eAction = RPNA_SkipCurrentChar;
			}
		}

		// Perform the desired action with the current infix character
		switch (eAction)
		{
		xcase RPNA_TryAsDifferentMode:
			devassert(eOldRPNState != eRPNState);

		xcase RPNA_AddToWorkingString:
			if (pInfixString[iInfixIndex] != '\0')
			{
				estrConcatChar(&pCurString, pInfixString[iInfixIndex]);
				iInfixIndex++;
			}

		xcase RPNA_SkipCurrentChar:
			iInfixIndex++;

		}
	}

	if (pCurString)
	{
		estrDestroy(&pCurString);
	}

	// Push the remaining operators onto the stack
	pCurString = eaPop(&eaOperatorStack);
	while (pCurString)
	{
		if (stricmp_safe(pCurString, "(") == 0) // Invalid
		{
			eResult = RPNPR_MismatchedParen;
			iInfixIndex = strchr(pInfixString, '(') - pInfixString + 1;
			goto error;
		}
		eaPush(peaRPNStackOut, pCurString);
		pCurString = eaPop(&eaOperatorStack);
	}

	eaDestroy(&eaOperatorStack);

	AccountProxyKeysMeetRequirements(NULL, *peaRPNStackOut, NULL, NULL, NULL, &iSolveError, NULL);
	if (iSolveError)
	{
		eResult = RPNPR_Unsolvable;
		goto error;
	}

	PERFINFO_AUTO_STOP_FUNC();

	devassert(eResult == RPNPR_Invalid);

	return RPNPR_Success;

error:
	devassert(eResult != RPNPR_Invalid);

	if (pCurString)
	{
		estrDestroy(&pCurString);
	}

	eaDestroyEString(&eaOperatorStack);
	eaDestroyEString(peaRPNStackOut);
	*peaRPNStackOut = NULL;

	if (piErrorIndex)
	{
		iInfixIndex--;
		if (iInfixIndex < (int)strlen(pInfixString))
		{
			*piErrorIndex = iInfixIndex;
		}
		else
		{
			*piErrorIndex = -1;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return eResult;
}

#include "rpn_h_ast.c"