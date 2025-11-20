#include "Expression.h"
#include "ExpressionParse.h"
#include "ExpressionPrivate.h"

#include "earray.h"
#include "error.h"
#include "EString.h"
#include "StringCache.h"
#include "textparserUtils.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

/*////////////////////////////////////////////////////////////////////////
ai expression language
 
operators and precedence:
 
Precedence: operators
1: field access
2: or
3: and
4: not
5: =, <, >
6: |,&
7: +,-
8: *,/
9: ^
10: - (negate), ~
11: parenthesis, function call, braces
 
simplified bnf:
 
expr -> orclause
orclause -> orclause or andclause | andclause
andclause -> andclause and notclause | notclause
notclause -> not equals | equals
equals -> equals = bitor | equals < bitor | equals <= bitor | equals > bitor | equals >= bitor | bitor
bitor -> bitor \| add | bitor & add | add
add -> add + multiply | add - multiply | multiply
multiply -> multiply * exponent | multiply / exponent | exponent
exponent -> fact ^ exponent | fact
fact -> NUM | neg NUM | ( expr ) | IDENT ( params ) | { expr }
params -> params expr | expr | NULL

fact -> NUM | neg NUM | ( expr ) | func
func -> func .IDENT ( params ) | IDENT | NULL
params -> params expr | expr | NULL

expanded bnf & eliminate left-recursion:
 
add -> multiply add_rest
add_rest -> + multiply add_rest | - multiply add_rest | NULL
multiply -> fact multiply_rest
multiply_rest -> * fact multiply_rest | / fact multiply_rest | NULL
fact -> NUM | NEG fact | ( add )
 
void expr(void)
{
      term();
      expr_rest();
}
 
void expr_rest(void)
{
      if (peek() == MULTIOP_ADD)
      {
            eatop(MULTIOP_ADD);
            term();
            pushop(MULTIOP_ADD);
            expr_rest();
      }
      else if (peek() == MULTIOP_SUBTRACT)
      {
            eatop(MULTIOP_SUBTRACT);
            term();
            pushop(MULTIOP_SUBTRACT);
            expr_rest();
      }
}
 
void term(void)
{
...
}
 
void fact(void)
{
      if (peek() == MULTI_INT)
      {
            eatval(MULTI_INT, filename);
            pushval(MULTI_INT);
      }
      else if (peek() == MULTIOP_NEGATE)
      {
            eatval(MULTIOP_NEGATE, filename);
            fact();
      }
      else if (peek() == MULTIOP_PAREN_OPEN)
      {
            eatval(MULTIOP_PAREN_OPEN, filename);
            expr();
            eatval(MULTIOP_PAREN_CLOSE, filename); // <<< eatval needs to produce errors if correct token isn't there
      }
      else
      { 
            // have multiple operator error here..
      }
}
 
//////// simplification:
 
void expr(void)
{
      term();
      while (1)
      {
            if (peek() == MULTIOP_ADD)
            {
                  eatop(MULTIOP_ADD);
                  term();
                  pushop(MULTIOP_ADD);
            }
            else if (peek() == MULTIOP_SUBTRACT)
            {
                  eatop(MULTIOP_SUBTRACT);
                  term();
                  pushop(MULTIOP_SUBTRACT);
            }
            else break;
      }
}
*/

int gParseError;

__forceinline static MultiValType peek(MultiVal*** valarray)
{
	if(eaSize(valarray))
		return (*valarray)[0]->type;
	else
		return -1;
}

__forceinline static MultiValType peek2(MultiVal*** valarray)
{
	if(eaSize(valarray) > 1)
		return (*valarray)[1]->type;
	else
		return -1;
}

__forceinline static char peekFirstChar(MultiVal*** valarray)
{
	MultiValType peekType = peek(valarray);
	if(peekType == MULTIOP_IDENTIFIER || peekType == MULTI_STRING)
		return (*valarray)[0]->str[0];
	else
		return 0;
}

SA_RET_NN_VALID __forceinline static MultiVal* eatval(MultiVal*** valarray, MultiValType type, Expression* expr)
{
	if(peek(valarray) == type)
		return (MultiVal*)eaRemove(valarray, 0);
	else
	{
		MultiVal* val = MultiValCreate();
		ErrorFilenamef(expr->filename, "Expression generated an error: Token %s does not match type %s\n Expression Body: \n%s",
			eaSize(valarray) ? MultiValPrint((*valarray)[0]) : "NONE",
			MultiValTypeToReadableString(type),
			exprGetCompleteString(expr));
		val->type = MULTI_INVALID;
		val->str = "Parse Error";
		gParseError = true;
		return val;
	}
}

#define pushval(valarray, val) eaPush(valarray, val)
#define pushvalat(valarray, val, idx) eaInsert(valarray, val, idx)

void code(MultiVal*** dst, MultiVal*** src, Expression* expr);
int ifclause(MultiVal*** dst, MultiVal*** src, MultiValType type, Expression* expr);
void statement(MultiVal*** dst, MultiVal*** src, Expression* expr);
void or(MultiVal*** dst, MultiVal*** src, Expression* expr);
void and(MultiVal*** dst, MultiVal*** src, Expression* expr);
void not(MultiVal*** dst, MultiVal*** src, Expression* expr);
void equals(MultiVal*** dst, MultiVal*** src, Expression* expr);
void bitor(MultiVal*** dst, MultiVal*** src, Expression* expr);
void add(MultiVal*** dst, MultiVal*** src, Expression* expr);
void multiply(MultiVal*** dst, MultiVal*** src, Expression* expr);
void exponent(MultiVal*** dst, MultiVal*** src, Expression* expr);
void fact(MultiVal*** dst, MultiVal*** src, Expression* expr);
void func(MultiVal*** dst, MultiVal*** src, Expression* expr);
void ident(MultiVal*** dst, const char* inStr, Expression* expr);
void params(MultiVal*** dst, MultiVal*** src, Expression* expr);

int exprParseInternal(MultiVal*** dst, MultiVal*** src, Expression* expr)
{
	gParseError = false;
	code(dst, src, expr);

	if(peek(src) == MULTIOP_RETURN)
	{
		MultiVal* ret = eatval(src, MULTIOP_RETURN, expr);
		pushval(dst, ret);
	}

	return !gParseError;
}

void code(MultiVal*** dst, MultiVal*** src, Expression* expr)
{
	int continueParsing = false;

	continueParsing = ifclause(dst, src, MULTIOP_IF, expr);

	while(1)
	{
		if(peek(src) == MULTIOP_STATEMENT_BREAK)
		{
			MultiVal* val = eatval(src, MULTIOP_STATEMENT_BREAK, expr);
			pushval(dst, val); //this is currently only used to make multiple statements not fill up the stack unnecessarily
			continueParsing = true;
		}

		if(continueParsing)
			continueParsing = ifclause(dst, src, MULTIOP_IF, expr);
		else
			break;
	}
}

int ifclause(MultiVal*** dst, MultiVal*** src, MultiValType type, Expression* expr)
{
	if(peek(src) == type)
	{
		MultiVal* ifVal = eatval(src, type, expr);
		MultiVal* paren;
		MultiVal* ifTrueJump;
		MultiVal* ifFalseJump;

		MultiValDestroy(ifVal);

		ifFalseJump = MultiValCreate();
		ifFalseJump->type = MULTIOP_JUMPIFZERO;

		if(peek(src) != MULTIOP_PAREN_OPEN)
			ErrorFilenamef(expr->filename, "if must be followed by an open parenthesis");
		paren = eatval(src, MULTIOP_PAREN_OPEN, expr);
		MultiValDestroy(paren);

		or(dst, src, expr);

		if(peek(src) != MULTIOP_PAREN_CLOSE)
			ErrorFilenamef(expr->filename, "if must be followed by an expression in parentheses");
		paren = eatval(src, MULTIOP_PAREN_CLOSE, expr);
		MultiValDestroy(paren);

		pushval(dst, ifFalseJump); // this is going to get updated with a jump location later

		code(dst, src, expr);

		// set the jump target for the if condition being false
		if(peek(src) == -1)
		{
			ifFalseJump->type = MULTIOP_RETURNIFZERO;
			return true;
		}

		if(type == MULTIOP_IF && peek(src) == MULTIOP_ENDIF)
		{
			MultiVal* endVal = eatval(src, MULTIOP_ENDIF, expr);
			MultiValDestroy(endVal);
			if(peek(src) == -1)
				ifFalseJump->type = MULTIOP_RETURNIFZERO;
			else
				ifFalseJump->intval = eaSize(dst);
			return true;
		}

		ifTrueJump = MultiValCreate();
		ifTrueJump->type = MULTIOP_JUMP;
		pushval(dst, ifTrueJump);

		ifFalseJump->intval = eaSize(dst);

		if(peek(src) == MULTIOP_ELIF)
			ifclause(dst, src, MULTIOP_ELIF, expr);
		else if(peek(src) == MULTIOP_ELSE)
		{
			MultiVal* elseVal = eatval(src, MULTIOP_ELSE, expr);
			MultiValDestroy(elseVal);
			code(dst, src, expr);
		}

		if(type == MULTIOP_IF && peek(src) == MULTIOP_ENDIF)
		{
			MultiVal* endVal = eatval(src, MULTIOP_ENDIF, expr);
			MultiValDestroy(endVal);
		}

		// set the jump target for where to jump to skip the possible elifs and else
		if(peek(src) == -1)
			ifTrueJump->type = MULTIOP_RETURN;
		else
			ifTrueJump->intval = eaSize(dst);

		return true;
	}

	statement(dst, src, expr);
	return false;
}

void statement(MultiVal*** dst, MultiVal*** src, Expression* expr)
{
	or(dst, src, expr);
}

void or(MultiVal*** dst, MultiVal*** src, Expression* expr)
{
	and(dst, src, expr);
	while(1)
	{
		if(peek(src) == MULTIOP_OR)
		{
			MultiVal* val = eatval(src, MULTIOP_OR, expr);
			and(dst, src, expr);
			pushval(dst, val);
		}
		else
			break;
	}
}

void and(MultiVal*** dst, MultiVal*** src, Expression* expr)
{
	not(dst, src, expr);
	while(1)
	{
		if(peek(src) == MULTIOP_AND)
		{
			MultiVal* val = eatval(src, MULTIOP_AND, expr);
			not(dst, src, expr);
			pushval(dst, val);
		}
		else
			break;
	}
}

void not(MultiVal*** dst, MultiVal*** src, Expression* expr)
{
	if(peek(src) == MULTIOP_NOT)
	{
		MultiVal* val = eatval(src, MULTIOP_NOT, expr);
		equals(dst, src, expr);
		pushval(dst, val);
	}
	else
		equals(dst, src, expr);
}

void equals(MultiVal*** dst, MultiVal*** src, Expression* expr)
{
	bitor(dst, src, expr);
	while(1)
	{
		if(peek(src) == MULTIOP_EQUALITY)
		{
			MultiVal* val = eatval(src, MULTIOP_EQUALITY, expr);
			bitor(dst, src, expr);
			pushval(dst, val);
		}
		else if(peek(src) == MULTIOP_LESSTHAN)
		{
			MultiVal* val = eatval(src, MULTIOP_LESSTHAN, expr);
			bitor(dst, src, expr);
			pushval(dst, val);
		}
		else if(peek(src) == MULTIOP_LESSTHANEQUALS)
		{
			MultiVal* val = eatval(src, MULTIOP_LESSTHANEQUALS, expr);
			bitor(dst, src, expr);
			pushval(dst, val);
		}
		else if(peek(src) == MULTIOP_GREATERTHAN)
		{
			MultiVal* val = eatval(src, MULTIOP_GREATERTHAN, expr);
			bitor(dst, src, expr);
			pushval(dst, val);
		}
		else if(peek(src) == MULTIOP_GREATERTHANEQUALS)
		{
			MultiVal* val = eatval(src, MULTIOP_GREATERTHANEQUALS, expr);
			bitor(dst, src, expr);
			pushval(dst, val);
		}
		else
			break;
	}
}


void bitor(MultiVal*** dst, MultiVal*** src, Expression* expr)
{
	add(dst, src, expr);
	while(1)
	{
		if(peek(src) == MULTIOP_BIT_OR)
		{
			MultiVal* val = eatval(src, MULTIOP_BIT_OR, expr);
			add(dst, src, expr);
			pushval(dst, val);
		}
		else if(peek(src) == MULTIOP_BIT_AND)
		{
			MultiVal* val = eatval(src, MULTIOP_BIT_AND, expr);
			add(dst, src, expr);
			pushval(dst, val);
		}
		else
			break;
	}
}


void add(MultiVal*** dst, MultiVal*** src, Expression* expr)
{
	multiply(dst, src, expr);
	while(1)
	{
		if(peek(src) == MULTIOP_ADD)
		{
			MultiVal* val = eatval(src, MULTIOP_ADD, expr);
			multiply(dst, src, expr);
			pushval(dst, val);
		}
		else if(peek(src) == MULTIOP_SUBTRACT)
		{
			MultiVal* val = eatval(src, MULTIOP_SUBTRACT, expr);
			multiply(dst, src, expr);
			pushval(dst, val);
		}
		else
			break;
	}
}

void multiply(MultiVal*** dst, MultiVal*** src, Expression* expr)
{
	exponent(dst, src, expr);
	while(1)
	{
		if(peek(src) == MULTIOP_MULTIPLY)
		{
			MultiVal* val = eatval(src, MULTIOP_MULTIPLY, expr);
			exponent(dst, src, expr);
			pushval(dst, val);
		}
		else if(peek(src) == MULTIOP_DIVIDE)
		{
			MultiVal* val = eatval(src, MULTIOP_DIVIDE, expr);
			exponent(dst, src, expr);
			pushval(dst, val);
		}
		else
			break;
	}
}

void exponent(MultiVal*** dst, MultiVal*** src, Expression* expr)
{
	fact(dst, src, expr);
	while(1)
	{
		if(peek(src) == MULTIOP_EXPONENT)
		{
			MultiVal* val = eatval(src, MULTIOP_EXPONENT, expr);
			fact(dst, src, expr);
			pushval(dst, val);
		}
		else
			break;
	}
}

void fact(MultiVal*** dst, MultiVal*** src, Expression* expr)
{
	if(peek(src) == MULTI_INT)
	{
		MultiVal* val = eatval(src, MULTI_INT, expr);
		pushval(dst, val);
	}
	else if(peek(src) == MULTI_FLOAT)
	{
		MultiVal* val = eatval(src, MULTI_FLOAT, expr);
		pushval(dst, val);
	}
	else if(peek(src) == MULTI_STRING)
	{
		MultiVal* val = eatval(src, MULTI_STRING, expr);
		pushval(dst, val);
	}
	else if(peek(src) == MULTIOP_LOC_STRING)
	{
		MultiVal* val = eatval(src, MULTIOP_LOC_STRING, expr);
		pushval(dst, val);
	}
	else if(peek(src) == MULTIOP_IDENTIFIER)
	{
		if(peek2(src) == MULTIOP_PAREN_OPEN)
		{
			func(dst, src, expr);
		}
		else
		{
			MultiVal* val = eatval(src, MULTIOP_IDENTIFIER, expr);
			ident(dst, val->str, expr);
			MultiValDestroy(val);
		}
	}
	else if(peek(src) == MULTIOP_NEGATE)
	{
		MultiVal* val = eatval(src, MULTIOP_NEGATE, expr);
		fact(dst, src, expr);
		pushval(dst, val);
	}
	else if(peek(src) == MULTIOP_BIT_NOT)
	{
		MultiVal* val = eatval(src, MULTIOP_BIT_NOT, expr);
		fact(dst, src, expr);
		pushval(dst, val);
	}
	else if (peek(src) == MULTIOP_PAREN_OPEN)
	{
		MultiVal* val = eatval(src, MULTIOP_PAREN_OPEN, expr);
		MultiValDestroy(val);
		statement(dst, src, expr);
		val = eatval(src, MULTIOP_PAREN_CLOSE, expr); // eatval errorfs whether this is the right token
		MultiValDestroy(val);
	}
	else if(peek(src) == MULTIOP_BRACE_OPEN)
	{
		MultiVal* val = eatval(src, MULTIOP_BRACE_OPEN, expr);
		pushval(dst, val);
		code(dst, src, expr);
		val = eatval(src, MULTIOP_BRACE_CLOSE, expr);
		pushval(dst, val);
	}

	// allow empty statement to allow a semicolon at the end of a block inside if/else
	// crappy but no better way to change this other than to change the grammar and update
	// all data
	/*
	else if(eaSize(src))
	{
		ErrorFilenamef(expr->filename, "The expression grammar does not allow %s here", MultiValPrint((*src)[0]));
		gParseError = true;
	}
	else
	{
		ErrorFilenamef(expr->filename, "Expected a token but did not find any (expression is %s)", expr->lines[0]->origStr);
		gParseError = true;
	}
	*/
}

void func(MultiVal*** dst, MultiVal*** src, Expression* expr)
{
	MultiVal* funcVal = eatval(src, MULTIOP_IDENTIFIER, expr);
	const char* baseVarName = NULL; // if this is an IDENT.FUNCTIONCALL function, this gets the basevar
	const char* realFuncName = NULL; // and this gets the function name

	MultiVal* openparen = eatval(src, MULTIOP_PAREN_OPEN, expr);
	MultiVal* closeparen;
	int openparenPushIdx = eaSize(dst);

	const char* tempStr = funcVal->str + strlen(funcVal->str) - 1;

	pushval(dst, openparen);

	// find and split up a possible identifier before the function call
	for(; tempStr != funcVal->str; tempStr--)
	{
		if(tempStr[0] == '.')
		{
			char* buf = NULL;

			estrStackCreate(&buf);

			realFuncName = allocAddString(tempStr + 1);
			estrCopy2(&buf, funcVal->str);
			buf[tempStr - funcVal->str] = 0;
			//strncpy(buf, funcVal->str, tempStr - funcVal->str);
			baseVarName = allocAddString(buf);

			funcVal->str = realFuncName;

			estrDestroy(&buf);
			break;
		}
	}

	if(baseVarName)
		ident(dst, baseVarName, expr);

	params(dst, src, expr);

	closeparen = eatval(src, MULTIOP_PAREN_CLOSE, expr);
	pushval(dst, closeparen);

	funcVal->type = MULTIOP_FUNCTIONCALL;
	pushval(dst, funcVal);



	while(peek(src) == MULTIOP_IDENTIFIER && peekFirstChar(src) == '.')
	{
		MultiVal* dotfunc = eatval(src, MULTIOP_IDENTIFIER, expr);

		if (peek(src) == MULTIOP_PAREN_OPEN)
		{
			MultiVal* secondopen = eatval(src, MULTIOP_PAREN_OPEN, expr);
			MultiVal* secondclose;

			if(strchr(dotfunc->str + 1, '.'))
			{
				ErrorFilenamef(expr->filename, "%s: Object paths are not allowed after function calls", dotfunc->str);
				gParseError = true;
			}

			pushvalat(dst, secondopen, openparenPushIdx);

			params(dst, src, expr);

			secondclose = eatval(src, MULTIOP_PAREN_CLOSE, expr);

			pushval(dst, secondclose);

			// fix up the str and type to not have the dot
			dotfunc->str = allocAddString(dotfunc->str + 1);
			dotfunc->type = MULTIOP_FUNCTIONCALL;
			pushval(dst, dotfunc);
		}
		else
		{
			dotfunc->type = MULTIOP_OBJECT_PATH;
			pushval(dst, dotfunc);
			break;
		}
	}
}

void ident(MultiVal*** dst, const char* inStr, Expression* expr)
{
	MultiVal* ident;
	MultiVal* objPath;
	char* dot = strchr(inStr, '.');

	// We need to parse Foo[Bar.Baz].Quux into "Foo[Bar.Baz]" and "Quux", not
	// "Foo[Bar" and ".Baz].Quux". Although, the object path system doesn't
	// understand this yet either.
	char* leftBracket = strchr(inStr, '[');
	char* rightBracket = strchr(inStr, ']');
	if (dot < rightBracket && dot > leftBracket)
		dot = strchr(rightBracket, '.');

	if(dot)
	{
		char* buf = NULL;
		estrStackCreate(&buf);
		estrCopy2(&buf, inStr);
		buf[dot-inStr] = '\0';
		ident = MultiValCreate();
		ident->str = allocAddString(buf);
		ident->type = MULTIOP_IDENTIFIER;
		pushval(dst, ident);

		objPath = MultiValCreate();
		objPath->str = allocAddString(dot);
		objPath->type = MULTIOP_OBJECT_PATH;
		pushval(dst, objPath);
		estrDestroy(&buf);
	}
	else
	{
		ident = MultiValCreate();
		ident->str = inStr; // inStr is already an allocAddString in all cases allocAddString(inStr);
		ident->type = MULTIOP_IDENTIFIER;
		pushval(dst, ident);
	}
}

void params(MultiVal*** dst, MultiVal*** src, Expression* expr)
{
	if(peek(src) != MULTIOP_PAREN_CLOSE)
	{
		statement(dst, src, expr);

		while(peek(src) == MULTIOP_COMMA)
		{
			MultiVal* comma = eatval(src, MULTIOP_COMMA, expr);
			MultiValDestroy(comma);
			statement(dst, src, expr);
		}
	}
}

void exprPostParseFixup(MultiVal*** vals)
{
	int i, n = eaSize(vals);
	int changed = true;
	for(i = 0; i < n; i++)
	{
		MultiVal* curVal = (*vals)[i];
		MultiVal* nextVal = (*vals)[i+1];

		int destroyed = false;
		if(curVal->type == MULTIOP_STATEMENT_BREAK)
		{
			if(i == n-1 || nextVal->type == MULTIOP_STATEMENT_BREAK ||
				nextVal->type == MULTIOP_RETURN || nextVal->type == MULTIOP_RETURNIFZERO ||
				nextVal->type == MULTIOP_JUMP || nextVal->type == MULTIOP_JUMPIFZERO)
			{
				MultiVal* val = eaRemove(vals, i);
				MultiValDestroy(val);
				n--;
				destroyed = true;
			}
		}

		if(destroyed)
		{
			int j;
			for(j = 0; j < n; j++)
			{
				MultiVal* updateVal = (*vals)[j];

				if((updateVal->type == MULTIOP_JUMP || updateVal->type == MULTIOP_JUMPIFZERO) &&
					updateVal->intval >= i)
				{
					updateVal->intval--;
				}
			}
		}

		if(curVal->type == MULTIOP_IDENTIFIER)
		{
			int staticIdx = exprGetStaticVarIndex(curVal->str);
			ParseTable* table = NULL;
			int column;
			void* ptr;

			if(staticIdx != STATIC_VAR_INVALID)
			{
				curVal->type = MULTIOP_STATICVAR;
				curVal->int32 = staticIdx;
			}
			else if(ParserResolveRootPath(curVal->str, NULL, &table, &column, &ptr, NULL, 0) || table)
				curVal->type = MULTIOP_ROOT_PATH;
		}
	}
	for(i = 0; i < n ; i++)
	{
		MultiVal* curVal = (*vals)[i];

		if((curVal->type == MULTIOP_JUMP || curVal->type == MULTIOP_JUMPIFZERO) &&
			curVal->intval >= n)
		{
			if(curVal->type == MULTIOP_JUMP)
				curVal->type = MULTIOP_RETURN;
			else
				curVal->type = MULTIOP_RETURNIFZERO;
		}
	}

	// convert jump-to-return to just returns, otherwise stack is lost during jump.
	while (changed)
	{
		changed = false;
		for(i = 0; i < n ; i++)
		{
			MultiVal *op = (*vals)[i];
			if(op->type == MULTIOP_JUMP && ((*vals)[op->intval]->type == MULTIOP_RETURN || (*vals)[op->intval]->type == MULTIOP_BRACE_CLOSE))
			{
				changed = true;
				op->type = MULTIOP_RETURN;
			}
			else if(op->type == MULTIOP_JUMPIFZERO && (*vals)[op->intval]->type == MULTIOP_RETURNIFZERO)
			{
				changed = true;
				op->type = MULTIOP_RETURNIFZERO;
			}
		}
	}
}