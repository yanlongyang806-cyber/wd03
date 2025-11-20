#pragma once

AUTO_ENUM;
typedef enum RPNParseResult {
	RPNPR_Invalid = 0,
	RPNPR_Success,
	RPNPR_MismatchedParen,
	RPNPR_InvalidOperator,
	RPNPR_UnknownCharacter,
	RPNPR_InvalidOutPointer,
	RPNPR_Unsolvable,
} RPNParseResult;

// Get an RPN stack from an infix notation string
// peaRPNStackOut will be filled with an earray of estrings that contains the RPN stack
RPNParseResult infixToRPN(SA_PARAM_OP_STR const char *pInfixString,
						  STRING_EARRAY *peaRPNStackOut,
						  SA_PARAM_OP_VALID int *piErrorIndex);