#include "ExpressionTokenize.h"
#include "ExpressionPrivate.h"

#include "earray.h"
#include "error.h"
#include "StringCache.h"
#include "structTokenizer.h"

#include "strings_opt.h"
#include "stringutil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


static int acceptString(char* str, MultiVal*** result, const char ***locs, const char *origStr)
{
	MultiVal* curVal = MultiValCreate();
	Mat4 dummyPos;

	if(exprMat4FromLocationString(NULL, str, dummyPos, true, NULL))
		curVal->type = MULTIOP_LOC_STRING;
	else
		curVal->type = MULTI_STRING;

	curVal->str = allocAddString(str);

	eaPush(result, curVal);
	if (locs)
		eaPush(locs, origStr);

	return false;
}

#define	MMO_WORDOP_START    MMO_AND
#define MMO_WORDOP_END		(MMO_ENDIF + 1)
#define MMO_WORDOP_COUNT	(MMO_WORDOP_END - MMO_WORDOP_START)
static int acceptIdentifier(char* str, MultiVal*** result, const char ***locs, const char *origStr)
{
	int i;
	MultiVal* curVal = MultiValCreate();
	const char* inStr = allocAddString(str);
	static const char* identifiers[MMO_WORDOP_COUNT];
	static int initted = false;


	if(!initted)
	{
		int count = MMO_WORDOP_START;

#define REGISTER_IDENTIFIER(enum, str) \
	devassertmsg(count == enum, "You must add all identifiers and add them in order"); \
	identifiers[count - MMO_WORDOP_START] = allocAddString(str); \
	count++;

		REGISTER_IDENTIFIER(MMO_AND, "and");
		REGISTER_IDENTIFIER(MMO_OR,  "or");
		REGISTER_IDENTIFIER(MMO_NOT, "not");
		REGISTER_IDENTIFIER(MMO_IF, "if");
		REGISTER_IDENTIFIER(MMO_ELSE, "else");
		REGISTER_IDENTIFIER(MMO_ELIF, "elif");
		REGISTER_IDENTIFIER(MMO_ENDIF, "endif");

		devassertmsg(count == MMO_WORDOP_END, "Missing word operator at end of identifiers");
	}

	for(i = 0; i < MMO_WORDOP_COUNT; i++)
	{
		if(inStr == identifiers[i])
		{
			curVal->type = MTE(0, MMO_WORDOP_START + i, MMT_NONE);
			eaPush(result, curVal);
			if (locs)
				eaPush(locs, origStr);
			return false;
		}
	}

	curVal->str = inStr;
	curVal->type = MULTIOP_IDENTIFIER;
	eaPush(result, curVal);
	if (locs)
		eaPush(locs, origStr);
	return false;
}

static int acceptNumber(char* str, MultiVal*** result, const char ***locs, const char *origStr)
{
	MultiVal* curVal = MultiValCreate();

	if(strchr(str, '.'))
	{
		curVal->floatval = atof(str);
		curVal->type = MULTI_FLOAT;
	}
	else
	{
		curVal->intval = atoi64(str);
		curVal->type = MULTI_INT;
	}

	eaPush(result, curVal);
	if (locs)
		eaPush(locs, origStr);

	return false;
}

static int acceptOperator(char* str, MultiVal*** result, const char ***locs, const char *origStr, int showErrors, Expression* expr)
{

	while(str && str[0])
	{
		MultiVal* curVal;
		int topIdx;

#define ADD_OPERATOR(opType) \
	curVal = MultiValCreate(); \
	curVal->type = opType; \
	eaPush(result, curVal); \
	if (locs) \
		eaPush(locs, origStr++); \
	str++;

		switch(str[0])
		{
		xcase '+':
			ADD_OPERATOR(MULTIOP_ADD);
		xcase '-':
		case 150:  // This is the MS mangled dash, Alt+0150
			curVal = MultiValCreate();
			topIdx = eaSize(result)-1;
			if(topIdx >= 0 && ((*result)[topIdx]->type == MULTI_INT || (*result)[topIdx]->type == MULTI_FLOAT
				|| (*result)[topIdx]->type == MULTIOP_PAREN_CLOSE || (*result)[topIdx]->type == MULTIOP_IDENTIFIER))
				curVal->type = MULTIOP_SUBTRACT;
			else
				curVal->type = MULTIOP_NEGATE;
			eaPush(result, curVal);
			if (locs)
				eaPush(locs, origStr++);
			str++;
		xcase '*':
			ADD_OPERATOR(MULTIOP_MULTIPLY);
		xcase '/':
			ADD_OPERATOR(MULTIOP_DIVIDE);
		xcase '^':
			ADD_OPERATOR(MULTIOP_EXPONENT);
		xcase '&':
			ADD_OPERATOR(MULTIOP_BIT_AND);
		xcase '|':
			ADD_OPERATOR(MULTIOP_BIT_OR);
		xcase '~':
			ADD_OPERATOR(MULTIOP_BIT_NOT);
		xcase '(':
			ADD_OPERATOR(MULTIOP_PAREN_OPEN);
		xcase ')':
			ADD_OPERATOR(MULTIOP_PAREN_CLOSE);
		xcase '{':
			ADD_OPERATOR(MULTIOP_BRACE_OPEN);
		xcase '}':
			ADD_OPERATOR(MULTIOP_BRACE_CLOSE);
		xcase '=':
			ADD_OPERATOR(MULTIOP_EQUALITY);
		xcase '>':
			if(str[1] == '=')
			{
				ADD_OPERATOR(MULTIOP_GREATERTHANEQUALS);
				str++; // get rid of the = too
			}
			else
			{
				ADD_OPERATOR(MULTIOP_GREATERTHAN);
			}
		xcase '<':
			if(str[1] == '=')
			{
				ADD_OPERATOR(MULTIOP_LESSTHANEQUALS);
				str++; // get rid of the = too
			}
			else
			{
				ADD_OPERATOR(MULTIOP_LESSTHAN);
			}
		xcase ',':
			ADD_OPERATOR(MULTIOP_COMMA);
		xcase ';':
			ADD_OPERATOR(MULTIOP_STATEMENT_BREAK);
		xdefault:
			if (showErrors)
			{
				ErrorFilenamef(expr ? expr->filename : NULL,
					"Error tokenizing:\n%s\nToken not recognized (got %c expected an operator)",
					exprGetCompleteString(expr), str[0]);
			}
			return true;
			//str++;
		}
	}

	return false;
}

typedef enum 
{
	IN_NOTHING,
	IN_IDENTIFIER,
	IN_STRING,
	IN_NUMBER,
	IN_OPERATOR,
    IN_COMMENT,
} TokenizerState;

int exprTokenizeEx(const char *str, MultiVal ***result, const char ***locs, int showErrors, Expression* expr)
{
	TokenizerState state = IN_NOTHING;
	char buf[MAX_STRING_LENGTH];
	int bufpos = 0;
	int error = false; // have different bits for different errors eventually?

	// "pe.prune(team="FOO").count > 2 and 2*4+3 < 20"
	while(!error && str && str[0])
	{
		const char *origStr;
		switch(state)
		{
		case IN_NOTHING:
			if(IS_WHITESPACE(str[0]))
				str++;
			else if (isalpha((unsigned char) str[0]) || (str[0] == '.' && isalpha(str[1])))
				state = IN_IDENTIFIER;
			else if (str[0] == '"')
			{
				str++;
				state = IN_STRING;
			}
			else if (str[0] >= '0' && str[0] <= '9' || (str[0] == '.' && str[1] >= '0' && str[1] <= '9'))
				state = IN_NUMBER;
			else if(str[0] == '.')
			{
				error |= true;
				if (showErrors)
				{
					ErrorFilenamef(expr ? expr->filename : NULL, "Error tokenizing:\n %s\n"
						"No . allowed at the start of a token unless it's followed"
						"by an alphanumeric character", exprGetCompleteString(expr));
				}
				str++;
			}
            else if(str[0] == '#')
                state = IN_COMMENT;
			else // assume anything else weird is an operator
				state = IN_OPERATOR;
			break;
		case IN_STRING:
			origStr = str-1;
			while(str[0] && str[0] != '"')
				buf[bufpos++] = *str++;
			if(str[0]) // eat the ending "
				str++;

			buf[bufpos] = 0;
			error |= acceptString(buf, result, locs, origStr);
			bufpos = 0;
			state = IN_NOTHING;
			break;
		case IN_IDENTIFIER:
			origStr = &str[0];
			while(isalnum((unsigned char)str[0]) || str[0] == '_' || str[0] == '.' || str[0] == '[' || str[0] == ']' || str[0] =='"')
				buf[bufpos++] = *str++;

			buf[bufpos] = 0;
			error |= acceptIdentifier(buf, result, locs, origStr);
			bufpos = 0;
			state = IN_NOTHING;
			break;
		case IN_NUMBER:
			origStr = &str[0];
			while(str[0] && (isdigit((unsigned char) str[0]) || str[0] == '.'))
				buf[bufpos++] = *str++;

			buf[bufpos] = 0;
			error |= acceptNumber(buf, result, locs, origStr);
			bufpos = 0;
			if(!error)
			{
				MultiVal* lastResult = (*result)[eaSize(result)-1];
				U64 multiplier = 0;
				while(isalnum((unsigned char)str[0]))
					buf[bufpos++] = *str++;
				buf[bufpos] = 0;
				if(bufpos)
				{
					if(!strnicmp(buf,"kb",2))
						multiplier = 1024;
					else if(!strnicmp(buf,"mb",2))
						multiplier = 1024 * 1024;
					else if(!strnicmp(buf,"gb",2))
						multiplier = 1024 * 1024 * 1024;
					else if(!strnicmp(buf,"sec",1))
						multiplier = 1;
					else if(!strnicmp(buf,"min",3))
						multiplier = 60;
					else if(!strnicmp(buf,"hr",2))
						multiplier = 60 * 60;
					else
					{
						if(showErrors)
						{
							ErrorFilenamef(expr ? expr->filename : NULL,
								"Error tokenizing expression %s,"
								" illegal format specifier %s found", origStr, buf);
						}
						error = true;
					}

					if(lastResult->type == MULTI_INT)
						lastResult->intval *= multiplier;
					else if(lastResult->type == MULTI_FLOAT)
						lastResult->floatval *= multiplier;
					else
						error = true;
				}
			}
			state = IN_NOTHING;
			break;
        case IN_COMMENT:
            if(*str++ == '\n')
                state = IN_NOTHING;
            break;
		case IN_OPERATOR:
			// accepts "<=-" as part of "foo <=-10"
			origStr = &str[0];
			while(str[0] && !isalnum(str[0]) && !IS_WHITESPACE(str[0]) &&
				str[0] != '"' &&  str[0] != '.')
			{
				buf[bufpos++] = *str++;
			}

			buf[bufpos] = 0;

			// deals with negate vs. minus
			error |= acceptOperator(buf, result, locs, origStr, showErrors, expr);
			bufpos = 0;
			state = IN_NOTHING;
		}
	}

	return error;
}

int exprTokenize(const char* str, MultiVal*** result, Expression* expr)
{
	return exprTokenizeEx(str, result, NULL, true, expr);
}
