#include "GenericPreProcess.h"
#include "estring.h"
#include "Error.h"
#include "filecache.h"
#include "StructInternals.h"
#include "SimpleParser.h"
#include "crypt.h"
#include "wininclude.h"

//copied from the shader preprocessing stuff so that it can be used generically

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););



typedef struct MacroParams {
	char *paramNames[12];
	int num_params;
} MacroParams;

static struct
{
	char commentmarker[2];
	int numMacros;
	struct {
		char *name; // pointer to other data
		MacroParams params;
		char *body; // estring, destroy me!
	} macros[64];

	int numDefines;
	int numSavedDefines;
	char *defines[64];
	char *savedDefines[64];

	char *workingBase;
	char *origBase;
	char *defines_string;
} preproc_state;


void genericPreProcSetCommentMarkers(char c0, char c1)
{
	preproc_state.commentmarker[0] = c0;
	preproc_state.commentmarker[1] = c1;
}

int genericPreProcGetNumDefines(void)
{
	return preproc_state.numDefines;
}
char *genericPreProcGetNthDefine(int n)
{
	return preproc_state.defines[n];
}



static char *initPreProcess(char *data)
{
	if (data) {
		preproc_state.workingBase=strdup(data);
		preproc_state.origBase = data;
		return preproc_state.workingBase;
	}
	return NULL;
}


static void commentLine(char *linestart)
{
	char *origLineStart = preproc_state.origBase + (linestart - preproc_state.workingBase);

	if (origLineStart[0] == '\r' || origLineStart[0] == '\n')
		return;
	if (origLineStart[1] == '\r' || origLineStart[1] == '\n')
	{
		origLineStart[0] = ' ';
		return;
	}

	//if we have one or more comment markers, use them to comment out the line. Otherwise, empty it by filling with spaces
	if (preproc_state.commentmarker[0])
	{
		origLineStart[0] = preproc_state.commentmarker[0];
		if (preproc_state.commentmarker[1])
			origLineStart[1] = preproc_state.commentmarker[1];
	}
	else
	{
		while (*origLineStart && *origLineStart != '\r' && *origLineStart != '\n')
		{
			*origLineStart = ' ';
			origLineStart++;
		}
	}

}


static int meetsSingleCondition(char *condition)
{
	int i=0;
	// Constants
	if (stricmp(condition, "true")==0 ||
		stricmp(condition, "1")==0)
	{
		return 1;
	}

	// Defines
	for (i=0; i<preproc_state.numDefines; i++) {
		if (stricmp(condition, preproc_state.defines[i])==0)
			return 1;
	}

	/*
	// /shadertest variables
	for (i=0; s=shaderDebugSpecialDefine(i); i++) {
	if (s && stricmp(condition, s)==0)
	return 1;
	}
	*/

	return 0;
}

static int meetsCondition(char *condition)
{
	char *last=NULL;
	char *tok;
#define CONDITIONDELIMS "\t |"
	tok = strtok_r(condition, CONDITIONDELIMS, &last);
	while (tok) {
		if (meetsSingleCondition(tok))
			return 1;
		tok = strtok_r(NULL, CONDITIONDELIMS, &last);
	}
	return 0;
}


void genericPreProcAddDefine(const char *define)
{
	assert(preproc_state.numDefines < ARRAY_SIZE(preproc_state.defines));
	preproc_state.defines[preproc_state.numDefines++] = strdup(define);
}

void genericPreProcRemoveDefine(const char *define)
{
	int i;
	for (i = 0; i < preproc_state.numDefines; ++i)
	{
		if (strcmp(preproc_state.defines[i], define)==0)
			preproc_state.defines[i][0] = 0;
	}
}

static void finishPreProcess(char *data)
{
	free(data);
	preproc_state.workingBase=NULL;
	preproc_state.origBase=NULL;
}


void genericPreProcIfDefs(char *data, char **strtokcontext, int ignore, const char *filename)
{
	char *working=initPreProcess(data);
#define DELIMS "\r\n"
#define TOK(x) (stricmp(tok, x)==0)
	char *line;

	line = strtok_s(working, DELIMS, strtokcontext);
	while (line) {
		char *s;
		s = line;
		while (*s==' ' || *s=='\t') s++;
		if (*s=='#') {
			char *last=NULL;
			char *tok=NULL;
			bool doNotCommentLine = false;
			s++;
			if (preproc_state.commentmarker[0] != '#' &&
                (ignore || !(s[0]=='p' && s[1]=='r' && s[2]=='a'))
            )
			//printf("%s\n", line);
			// Comment or preprocessor directive
			tok = strtok_s(s, " \t", &last);
			if (!tok) {
				// nothing
			} else if (TOK("ifdef")) {
				if (ignore) {
					// Nested if
					genericPreProcIfDefs(NULL, strtokcontext, 2, filename);
				} else {
					if (meetsCondition(last)) {
						// doit
						genericPreProcIfDefs(NULL, strtokcontext, 0, filename);
					} else {
						// don't do it!
						genericPreProcIfDefs(NULL, strtokcontext, 1, filename);
					}
				}
			} else if (TOK("ifndef")) {
				if (ignore) {
					// Nested if
					genericPreProcIfDefs(NULL, strtokcontext, 2, filename);
				} else {
					if (meetsCondition(last)) {
						// don't do it!
						genericPreProcIfDefs(NULL, strtokcontext, 1, filename);
					} else {
						// doit
						genericPreProcIfDefs(NULL, strtokcontext, 0, filename);
					}
				}
			} else if (TOK("else")) {
				if (ignore==0) {
					// All after this will not do anything
					ignore = 1;
				} else if (ignore==1) {
					// We failed the first clause of the if, do this one!
					ignore = 0;
				}
			} else if (TOK("elseifdef") || TOK("elifdef") || TOK("elif") || TOK("elseif")) {
				if (ignore==0) {
					// All elseifdefs after this will not do anything
					ignore = 2;
				} else if (ignore==1) {
					// We failed the first clause of the if, do this one if applicable
					if (meetsCondition(last)) {
						// doit
						ignore = 0;
					} else {
						// don't do it!
						ignore = 1;
					}
				} else if (ignore==2) {
					// continue ignoring
				}
			} else if (TOK("endif")) {
				commentLine(line);
				if (working)
					ErrorFilenamef(filename, "#endif without matching #ifdef");
				return;
			} else if (TOK("define")) {
				if (!ignore)
					genericPreProcAddDefine(last);
			} else if (TOK("undef")) {
				if (!ignore)
					genericPreProcRemoveDefine(last);
			} else {
				if (!ignore)
					doNotCommentLine = true;
			}
			if (!doNotCommentLine)
				commentLine(line);
		} else {
			if (ignore) {
				// ignore it
				commentLine(line);
				//printf("-%s\n", line);
			} else {
				// Just a line, leave it be
				//printf("+%s\n", line);
			}
		}

		line = strtok_s(NULL, DELIMS, strtokcontext);
	}

	if (working) {
		finishPreProcess(working);
	}
#undef DELIMS
#undef TOK
}

static char *preAddMacro(void)
{
	char *ret=NULL;
	if (preproc_state.macros[preproc_state.numMacros].body) {
		// Already have an estring created
		ret = preproc_state.macros[preproc_state.numMacros].body;
		preproc_state.macros[preproc_state.numMacros].body = NULL;
	} else {
		estrCreate(&ret);
	}
	return ret;
}
static char *macroParamDelims = ",()";


//takes <<<antyhing,;sdafjpoiw4jf>>><<<this is the second argument>>>, destroys it,
//returns character 1 after the last ">>>"
static char *fillParamsTripleLTGTStyle(char *line, MacroParams *params)
{
	char *pEnd;

	params->num_params=0;

	while (line[0] == '<' && line[1] == '<' && line[2] == '<' && (pEnd = strstr(line, ">>>")))
	{
		assert(params->num_params < ARRAY_SIZE(params->paramNames));

		//if we have >>>> make sure we get the last 3 >'s as our end token
		while (pEnd[3] == '>')
		{
			pEnd++;
		}

		*pEnd = 0;
		params->paramNames[params->num_params++] = line+3;

		line = pEnd + 3;
	}

	return line;
}




// Takes "(a,b)", destroys it, returns character 1 after the ")"
// alternatively, takes <<<antyhing,;sdafjpoiw4jf>>><<<this is the second argument>>> in case your
//arguments are complicated and ugly
static char *fillParams(char *line, MacroParams *params)
{
	char *lineCopy = line;

	if (!line || !*line)
		return line;

	while (*lineCopy == ' ')
	{
		lineCopy++;
	}

	if (lineCopy[0] == '<' && lineCopy[1] == '<' && lineCopy[2] == '<')
	{
		return fillParamsTripleLTGTStyle(lineCopy, params);
	}
	else
	{
		char *last=NULL;
		char *s;
		char *tok;
		params->num_params=0;

		// find closing parenthesis
		{
			int depth=1;
			s = line - 1;
			while(depth>0) {
				s = strpbrk(s + 1, "()");
				if(!s)
					break;
				
				if(*s == '(')
					++depth;
				else
					--depth;
			}
		}
		if (!s)
			return line;
		*s = '\0';

		
		tok = strtok_paren_r(line, macroParamDelims, &last);
		while (tok) {
			assert(params->num_params < ARRAY_SIZE(params->paramNames));
			tok = (char*)removeLeadingWhiteSpaces(tok);
			removeTrailingWhiteSpaces(tok);
			params->paramNames[params->num_params++] = tok;
			tok = strtok_paren_r(NULL, macroParamDelims, &last);
		}
		return last+1;
	}
}

static void addMacro(char *macroName, char *macroBody)
{
	char *last=NULL;
	char *tok;
	assert(preproc_state.numMacros < ARRAY_SIZE(preproc_state.macros));
	tok = strtok_r(macroName, macroParamDelims, &last);
	tok = (char*)removeLeadingWhiteSpaces(tok);
	removeTrailingWhiteSpaces(tok);
	preproc_state.macros[preproc_state.numMacros].name = tok;
	fillParams(last, &preproc_state.macros[preproc_state.numMacros].params);
	preproc_state.macros[preproc_state.numMacros++].body = macroBody;
}



#define isvalidchar(c) (isalnum(((unsigned char)(c)))||(c)=='_')
//static bool isvalidchar(unsigned char c)
//{
//	return isalnum(c) || c=='_';
//}

// Compares two strings, stop at the first non-alphanumeric character
// This is slow in Full Debug, spending most of it's time filling 32 bytes on the stack with 0xCCCCCCCC
__forceinline static int strcmpalnum(const unsigned char *a, const unsigned char *b)
{
	for (; *a==*b && isvalidchar(*a) && isvalidchar(*b); a++, b++);
	if (!isvalidchar(*a) && !isvalidchar(*b))
		return 0; // Off end of word on both
	return *a-*b;
}





static void macroConcat(char **dst, int index, MacroParams *replaceParams)
{
	char *walk;
	bool wordStart=true;
	int i;
	MacroParams *sourceParams = &preproc_state.macros[index].params;
	int numParams = MIN(replaceParams->num_params, sourceParams->num_params);
	bool bFirstChar=true;
	bool bNeedCR=false;
	for (walk=preproc_state.macros[index].body; *walk;)
	{
		char *next = walk+1;
		bool replaced=false;
		if (wordStart)
		{
			for (i=0; i<numParams && !replaced; i++) {
				if (strcmpalnum(walk, sourceParams->paramNames[i])==0)
				{
					// strip ## between a macro parameter and other text
					int oldlen = estrLength(dst);
					if (oldlen > 2 && (*dst)[oldlen-2] == '#' && (*dst)[oldlen-1] == '#')
						estrSetSize(dst, oldlen-2);
					// Copy the replaced word, advance past end of source word
					estrConcat(dst, replaceParams->paramNames[i], (int)strlen(replaceParams->paramNames[i]));
					next = walk + (int)strlen(sourceParams->paramNames[i]);

					// strip ## between a macro parameter and other text
					if (next[0] == '#' && next[1] == '#')
						next+=2;

					replaced = true;
					bFirstChar = false;
				}
			}
		}
		if (!replaced) {
			if (bFirstChar && *walk == '#')
				estrConcatChar(dst, '\n');
			// Copy 1 character
			estrConcatChar(dst, *walk);
			bFirstChar = false;
		}
		// Determine whether the next character could be the beginning of a symbol
		if (isvalidchar((unsigned char)*walk))
			wordStart = false;
		else {
			if (*walk == '\n')
				bNeedCR = false;
			if (*walk == '#')
				bNeedCR = true;
			wordStart = true;
		}
		walk = next;
	}
	//if (bNeedCR)
		//estrConcatChar(dst, '\n');
}


static char *expandMacros(char *line)
{
	static char *buffer=NULL;
	char **ret=NULL;
	char *walk;
	bool wordStart=true;
	int i;
	for (walk=line; *walk==' ' || *walk=='\t'; walk++);
	if (walk[0] == '/' && walk[1] == '/' || walk[0] == '#')
		return NULL; // Line starts with a comment
	if (!buffer)
		estrCreate(&buffer);
	estrClear(&buffer);
	for (walk=line; *walk && !ret; walk++)
	{
		if (wordStart) {
			for (i=0; i<preproc_state.numMacros && !ret; i++)
			{
				if (strcmpalnum(walk, preproc_state.macros[i].name)==0)
				{
					char *s;
					MacroParams params;
					// Found this macro!
					// Concat the part before the macro
					ret = &buffer;
					estrConcat(ret, line, walk - line);
					// Concat the macro
					// Parse the variable replacements
					s = fillParams(walk + strlen(preproc_state.macros[i].name) + 1, &params);
					macroConcat(ret, i, &params);
					// Concat the part after the macro
					estrConcat(ret, s, (int)strlen(s));
				}
			}
		}
		// Determine whether the next character could be the beginning of a symbol
		if (isvalidchar((unsigned char)*walk))
			wordStart = false;
		else
			wordStart = true;
	}
	return ret?*ret:NULL;
}


int genericPreProcMacros_dbg(char **data MEM_DBG_PARMS)
{
	char *working = initPreProcess(*data);
	static char *ret=NULL;
	int i;
	char *line_text;
	int macro_count=0;
	bool inmacro=false;
	char *macroName=NULL;
	char *macroBody=NULL;
	char *strtokcontext = NULL;
#define DELIMS "\r\n"
#define TOK(x) (tok && stricmp(tok, x)==0)
#define CRLF "\r\n"

	if (!ret)
		estrCreate(&ret);
	estrClear(&ret);

	// Reset macro state so we can run the preprocessor multiple times.
	for (i=0; i<preproc_state.numMacros; i++) {
		estrClear(&preproc_state.macros[i].body);
	}
	preproc_state.numMacros = 0;

	line_text = strtok_s(working, DELIMS, &strtokcontext);
	while (line_text) {
		char *s;
		s = line_text;
		while (*s==' ' || *s=='\t') s++;
		if (*s=='#') {
			char *last=NULL;
			char *tok;
			//printf("%s\n", line_text);
			s++;
			// Comment or preprocessor directive
			tok = strtok_s(s, " \t", &last);
			if (!inmacro && TOK("macro")) {
				++macro_count;
				macroName = last;
				inmacro = true;
				macroBody = preAddMacro();
			} else if (TOK("endmacro")) {
				assert(inmacro);
				if (inmacro) {
					inmacro = false;
					// finish up the macro
					if (strEndsWith(macroBody, CRLF)) {
						estrSetSize(&macroBody, (int)strlen(macroBody)-(int)strlen(CRLF));
					}
					addMacro(macroName, macroBody);
					macroBody = NULL;
				}
			} else {
				estrConcat(&ret, line_text, (int)strlen(line_text));
				if (last) {
					estrConcatChar(&ret, ' ');
					estrConcat(&ret, last, (int)strlen(last));
				}
				estrConcat(&ret, CRLF, (int)strlen(CRLF));
				if (inmacro) {
					estrConcat(&macroBody, line_text, (int)strlen(line_text));
					if (last) {
						estrConcatChar(&macroBody, ' ');
						estrConcat(&macroBody, last, (int)strlen(last));
					}
					estrConcat(&macroBody, CRLF, (int)strlen(CRLF));
				}
			}
		} else {
			char *expanded=NULL;
			if (inmacro) {
				if (*s == '/')
					if (s[1]=='/')
						goto ProcessNextMacroLine;	// commented line, move along.
				estrConcat(&macroBody, line_text, (int)strlen(line_text));
				estrConcat(&macroBody, CRLF, (int)strlen(CRLF));
			} else {
				// non-comment, search for macros
				expanded = expandMacros(line_text);
				if (expanded) {
					++macro_count;
					estrConcat(&ret, expanded, (int)strlen(expanded));
					//estrDestroy(&expanded);
					estrConcat(&ret, CRLF, (int)strlen(CRLF));
				} else {
					estrConcat(&ret, line_text, (int)strlen(line_text));
					estrConcat(&ret, CRLF, (int)strlen(CRLF));
				}
			}
		}
ProcessNextMacroLine:
		line_text = strtok_s(NULL, DELIMS, &strtokcontext);
	}

	finishPreProcess(working);
	if (macro_count) {
		// Replace the string!
		SAFE_FREE(*data);
		*data = strdup_dbg(ret MEM_DBG_PARMS_CALL);
	}
	assert(!macroBody); // We were parsing a macro?  Leaking memory here!
	//	estrDestroy(&ret);
	//	printf("New:\n%s\n", *data);

	return macro_count;
}

int genericPreProcIncludes(char **data, const char *path, const char *sourcefilename, FileList *file_list, PreProcFlags flags)
{
	char *working = initPreProcess(*data);
	static char *ret=NULL;
	char *line;
	bool foundinclude = false;
	char *working_last=NULL;
	int include_count = 0;

#define DELIMS "\r\n"
#define TOK(x) (tok && stricmp(tok, x)==0)
#define CRLF "\r\n"

	if (!ret)
		estrCreate(&ret);
	estrClear(&ret);

	line = strtok_r(working, DELIMS, &working_last);
	while (line) {
		char *s;
		s = line;
		while (*s==' ' || *s=='\t') s++;
		if (*s=='#')
		{
			char *last=NULL;
			char *tok;
			//printf("%s\n", line);
			s++;
			// Comment or preprocessor directive
			tok = strtok_r(s, " \t", &last);
			if (TOK("include"))
			{
				if (last)
				{
					char filename[MAX_PATH] = {0};
					char *includeText;
					foundinclude = true;
					if (last[0] == '\"')
					{
						char *stemp;
						last++;
						stemp = strrchr(last, '\"');
						if (stemp && stemp >= last)
							*stemp = 0;
					}

					if ((!strchr(last, '/') && !strchr(last, '\\')) || last[0] == '.')
						sprintf(filename, "%s/%s", path, last);
					else
						sprintf(filename, "%s", last);
					includeText = strdup( fileCachedData(filename, NULL) );
					if (includeText)
					{
						estrConcat(&ret, includeText, (int)strlen(includeText));
						estrConcat(&ret, CRLF, (int)strlen(CRLF));
						free(includeText);
						include_count++;
					}
					else
					{
						ErrorFilenamef(sourcefilename, "Include file not found: \"%s\"", filename);
					}
					if (file_list) {
						if (flags & PreProc_UseCRCs)
							FileListInsertChecksum(file_list, filename, 0);
						else
							FileListInsert(file_list, filename, 0);
					}
				}
			}
			else
			{
				estrConcat(&ret, line, (int)strlen(line));
				if (last)
				{
					estrConcatChar(&ret, ' ');
					estrConcat(&ret, last, (int)strlen(last));
				}
				estrConcat(&ret, CRLF, (int)strlen(CRLF));
			}
		}
		else
		{
			// non-include
			estrConcat(&ret, line, (int)strlen(line));
			estrConcat(&ret, CRLF, (int)strlen(CRLF));
		}

		line = strtok_r(NULL, DELIMS, &working_last);
	}

	finishPreProcess(working);
	if (foundinclude) {
		// Replace the string!
		SAFE_FREE(*data);
		*data = strdup(ret);
	}
	//estrDestroy(&ret);
	//	printf("New:\n%s\n", *data);

	return include_count;
}




void genericPreProcSaveDefines(void)
{
	int i;
	for (i = 0; i < preproc_state.numSavedDefines; i++)
		SAFE_FREE(preproc_state.savedDefines[i]);
	preproc_state.numSavedDefines = preproc_state.numDefines;
	for (i = 0; i < preproc_state.numSavedDefines; i++)
		preproc_state.savedDefines[i] = strdup(preproc_state.defines[i]);
}

static void genericPreProcRestoreDefines(void)
{
	int i;
	assert(preproc_state.numDefines == 0);
	for (i = 0; i < preproc_state.numSavedDefines; i++)
		genericPreProcAddDefine(preproc_state.savedDefines[i]);
}




char *genericPreProcGetDefinesString(void)
{
	static char emptybuf[2] = {0};
	if (preproc_state.defines_string)
		return preproc_state.defines_string;
	return emptybuf;
}



U32 genericPreProcHashDefines(void)
{
	int i;
	cryptAdler32Init();
	for (i=0; i<preproc_state.numDefines; i++)
		cryptAdler32Update(preproc_state.defines[i], (int)strlen(preproc_state.defines[i]));
	return cryptAdler32Final();
}

void genericPreProcHashDefines_ongoing(void)
{
	int i;
	for (i=0; i<preproc_state.numDefines; i++)
		cryptAdler32Update(preproc_state.defines[i], (int)strlen(preproc_state.defines[i]));
}

void genericPreProcReset(void)
{
	int i;
	if (!preproc_state.defines_string)
		estrCreate(&preproc_state.defines_string);
	estrClear(&preproc_state.defines_string);
	// Fill in preproc_state.defines_string in case of needing to print it for an error
	for (i=0; i<preproc_state.numDefines; i++) {
		estrConcat(&preproc_state.defines_string, preproc_state.defines[i], (int)strlen(preproc_state.defines[i]));
		estrConcatChar(&preproc_state.defines_string, ' ');
		SAFE_FREE(preproc_state.defines[i]);
	}
	for (i=0; i<preproc_state.numMacros; i++) {
		estrClear(&preproc_state.macros[i].body);
	}
	preproc_state.numDefines = 0;
	preproc_state.numMacros = 0;
}

static CRITICAL_SECTION genericPreProcCritSec;
static int genericPreProcCritSec_depth;

void genericPreProcEnterCriticalSection(void)
{
	static bool init = false;
	if(!init)
	{
		init = true;
		InitializeCriticalSection(&genericPreProcCritSec);
	}
	
	EnterCriticalSection(&genericPreProcCritSec);

    if(!genericPreProcCritSec_depth) {
        assert(!preproc_state.numDefines && !preproc_state.numMacros);
    }
    genericPreProcCritSec_depth++;
}

void genericPreProcLeaveCriticalSection(void)
{
    genericPreProcCritSec_depth--;
    if(!genericPreProcCritSec_depth) {
        assert(!preproc_state.numDefines && !preproc_state.numMacros);
    }

    LeaveCriticalSection(&genericPreProcCritSec);
}
