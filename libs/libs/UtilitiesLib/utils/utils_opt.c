// Some functions that used to be in utils.c but were places here so that some of utils.c can have the optimizer
// turned off
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "utils.h"
//#include "Common.h"

#if !_PS3
#include <io.h>
#include <sys/types.h>
#include <time.h>
#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include "wininclude.h"
#include <process.h>
#endif

#include "timing.h"
#include "estring.h"
#include "ScratchStack.h"

int __ascii_stricmp(const char *a,const char *b);

int strStartsWith(const char* str, const char* start)
{
	if(!str || !start)
		return 0;

	return strnicmp(str, start, strlen(start))==0;
}

int strStartsWithIgnoreUnderscores(const char* str, const char* start)
{
	if(	!str ||
		!start)
	{
		return 0;
	}

	while(1){
		while(*start == '_'){
			start++;
		}
		
		if(!*start){
			return 1;
		}

		while(*str == '_'){
			str++;
		}
		
		if(	!*str
			||
			*str != *start &&
			tolower(*str) != tolower(*start))
		{
			return 0;
		}
		
		str++;
		start++;
	}
}

int strEndsWith(const char* str, const char* ending)
{
	int strLength;
	int endingLength;
	if(!str || !ending)
		return 0;

	strLength = (int)strlen(str);
	endingLength = (int)strlen(ending);

	if(endingLength > strLength)
		return 0;

	if(stricmp(str + strLength - endingLength, ending) == 0)
		return 1;
	else
		return 0;
}

int utils_stricmp(const char *a,const char *b)
{
	if(a == b){
		return 0;
	}
	if(!a){
		return -1;
	}
	if(!b){
		return 1;
	}

	for(;*a && *b;a++,b++)
	{
		if (*a != *b)
		{
			S32 t = tolower(*a) - tolower(*b);
			if (t)
				return t;
		}
	}
	return *a - *b;
}

int utils_strnicmp(const char *first, const char *last, size_t count)
{
    if(count)
    {
        int f=0;
        int l=0;

        do
        {
            f = tolower(*first);
            first++;
            l = tolower(*last);
            last++;
        }
        while ( --count && f && (f == l) );

        return ( f - l );
    }
    else
    {
        return 0;
    }
}

char *forwardSlashes(char *path)
{
	char	*s;

	if(!path)
		return NULL;

	for(s=path;*s;s++)
	{
		if (*s == '\\')
			*s = '/';
		if (s!=path && (s-1)!=path && *s == '/' && *(s-1)=='/') {
			strcpy_unsafe(s, s+1);
		}
	}
	return path;

}

char *backSlashes(char *path)
{
	char	*s;

	if(!path)
		return NULL;

	for(s=path;*s;s++)
	{
		if (*s == '/')
			*s = '\\';
	}
	return path;
}

char *fixDoubleSlashes(char *fn)
{
	char *c=fn;
	while (c=strstr(c, "//"))
		strcpy_unsafe(c, c+1);
	return fn;
}

void estrFixFilename(char **estrFn)
{
	char fn[1024];
	if (!estrFn)
	{
		return;
	}
	strcpy(fn, *estrFn);
	forwardSlashes(fn);
	fixDoubleSlashes(fn);
	estrCopy2(estrFn, fn);
}

void concatpath_s(const char *s1,const char *s2,char *full,size_t full_size)
{
	char	*s;

	if (s2[1] == ':' || s2[0] == '/' || s2[0] == '\\')
	{
		strcpy_s(full,full_size,s2);
		return;
	}
	strcpy_s(full,full_size,s1);
	s = &full[strlen(full)-1];
	if (*s != '/' && *s != '\\')
		strcat_s(full,full_size,"/");
	if (s2[0] == '/' || s2[0] == '\\')
		s2++;
	strcat_s(full,full_size,s2);
}

//r = reentrant. just like strtok, but you keep track of the pointer yourself (last)
char *strtok_r(char *target,const char *delim,char **last)
{
	int start;

	if ( target != 0 )
		*last = target;
	else
		target = *last;

	if ( !target || *target == '\0' )
		return 0;

	start = (int)strspn(target,delim);
	target = &target[start];
	if ( *target == '\0' )
	{
		/* failure to find 'start', remember and return */
		*last = target;
		return 0;
    }

    /* found beginning of token, now look for end */
	if ( *(*last = target + strcspn(target,delim)) != '\0')
	{
		*(*last)++ = '\0';
	}
	return target;
}

// safe/reentrant, non-destructive
char *strtok_nondestructive(char *target, const char *delim, char **last, char *delim_context)
{
	int start;

	if ( target != 0 ) {
		*last = target;
		*delim_context = '\0';
	} else {
		target = *last;
		if (*delim_context != '\0')
		{
			assert((*last)[-1]=='\0');
			(*last)[-1] = *delim_context;
			*delim_context = '\0';
		}
	}

	if ( !target || *target == '\0' )
		return 0;

	start = (int)strspn(target,delim);
	target = &target[start];
	if ( *target == '\0' )
	{
		/* failure to find 'start', remember and return */
		*last = target;
		return 0;
	}

	/* found beginning of token, now look for end */
	if ( *(*last = target + strcspn(target,delim)) != '\0')
	{
		assert(**last != '\0');
		*delim_context = **last;
		*(*last)++ = '\0';
	} else {
		*delim_context = '\0';
	}
	return target;
}

// Function to test all the various strtok variations (ran via debugger or make it an AUTO_RUN temporarily)
void strtokTest(void)
{
	const static struct 
	{
		const char *str;
		const char *delims;
	} tests[] = {
		{"Hello, world!", " "},
		{"Hello, world!", ", !"},
		{"Hello, world!", "Hello, world!"},
		{"a\n\nb", "\n"},
		{"\na\nb\n", "\n"},
	};
	int i;
	for (i=0; i<ARRAY_SIZE(tests); i++) 
	{
		char temp[3][100];
		char delims[100];
		int j;
		char *head[3];
		char *tok[3];
		char *context[3] = {0};
		char delim_context_2;
		strcpy(delims, tests[i].delims);
		for (j=0; j<3; j++)
		{
			strcpy(temp[j], tests[i].str);
			head[j] = temp[j];
		}
		do {
			tok[0] = strtok_s(head[0], delims, &context[0]);
			assert(strcmp(delims, tests[i].delims)==0);
			tok[1] = strtok_r(head[1], delims, &context[1]);
			assert(strcmp(delims, tests[i].delims)==0);
			tok[2] = strtok_nondestructive(head[2], delims, &context[2], &delim_context_2);
			assert(strcmp(delims, tests[i].delims)==0);
			head[0] = NULL;
			head[1] = NULL;
			head[2] = NULL;

			// Compare results
			if (tok[0]==NULL)
			{
				assert(!tok[1]);
				assert(!tok[2]);
				break;
			}
			assert(tok[1]);
			assert(tok[2]);
			assert(strcmp(tok[0], tok[1])==0);
			assert(strcmp(tok[1], tok[2])==0);
		} while (true);
		assert(strcmp(temp[2], tests[i].str)==0); // Should have been repaired
	}
}


// Just like strtok, but skips over any matching parethesis.  Useful
// when preprocessing macros
//
//r = reentrant. just like strtok, but you keep track of the pointer yourself (last)
char *strtok_paren_r(char *target, const char *delim, char **last)
{
	int start;
	char *paren;

	if( target != 0 )
		*last = target;
	else
		target = *last;

	if( !target || *target == '\0' )
		return 0;

	start = (int)strspn(target,delim);
	target = &target[start];
	if( *target == '\0' )
	{
		/* failure to find 'start', remember and return */
		*last = target;
		return 0;
	}

	/* found beginning of token, now look for end */
	*last = target;
	while( true )
	{
		paren = strpbrk(*last, "()");
		*last = *last + strcspn(*last,delim);

		if( !paren || *last < paren ) {
			break;
		}
	
		/* skip over matchin parenthesis */
		{
			int depth = 0;
			if(*paren == '(')
				++depth;
			else
				--depth;

			while(depth > 0) {
				paren = strpbrk(paren+1, "()");
				if(!paren) {
					paren = target+strlen(target) - 1;
					break;
				}

				if(*paren == '(')
					++depth;
				else
					--depth;
			}
			*last = paren + 1;
		}
	}
	
	if (**last != '\0')
	{
		*(*last)++ = '\0';
	}
	return target;
}

//r = reentrant. just like strtok, but you keep track of the pointer yourself (last)
char *strtok_delims_r(char *target,const char *startdelim, const char *enddelim,char **last)
{
	int start;

	if ( target != 0 )
		*last = target;
	else
		target = *last;

	if ( !target || *target == '\0' )
		return 0;

	start = (int)strspn(target,startdelim);
	target = &target[start];
	if ( *target == '\0' )
	{
		/* failure to find 'start', remember and return */
		*last = target;
		return 0;
	}

	/* found beginning of token, now look for end */
	if ( *(*last = target + strcspn(target,enddelim)) != '\0')
	{
		*(*last)++ = '\0';
	}
	return target;
}


/* Function strsep2()
 *	Similar to strsep(), except this function returns the deliminator
 *	through the given retrievedDelim buffer if possible.
 *
 */
char* strsep2(char** str, const char* delim, char* retrievedDelim){
	char* token;
	int spanLength;

	// Try to grab the token from the beginning of the string
	// being processed.
	token = *str;

	// If no token can be found from the string being processed,
	// return nothing.
	if('\0' == *token)
		return NULL;

	// Find out where the token ends.
	spanLength = (int)strcspn(*str, delim);

	// Advance the given string pointer to the end of the current token.
	*str = token + spanLength;

	// Extract the retrieved deliminator if requested.
	if(retrievedDelim)
		*retrievedDelim = **str;

	// If the end of the string has been reached, the string pointer is
	// pointing at the NULL terminating character.  Return the extracted
	// token.  The string pointer will be left pointing to the NULL
	// terminating character.  If the same string pointer is passed
	// back in a later call to this function, the function would return
	// NULL immediately.
	if('\0' == **str)
		return token;

	// Otherwise, the string pointer is pointing at a deliminator.  Turn
	// it into a NULL terminating character to mark the end of the token.
	**str = '\0';

	// Advance the string pointer to the next character in the string.
	// The string can continue to be processed if the same string pointer
	// is passed back later.
	(*str)++;

	return token;
}

/* Function strsep()
 *	A re-entrant replacement for strtok, similar to strtok_r.  Given a cursor into a string,
 *	and a list of deliminators, this function returns the next token in the string pointed
 *	to by the cursor.  The given cursor will be forwarded pass the found deliminator.
 *
 *	Note that the owner string pointer should not be passed to this function.  Doing so may
 *	result in memory leaks.  This function will alter the given string pointer (cursor).
 *
 *	Moved from entScript.c
 *
 *	Parameters:
 *		str - the cursor into the string to be used.
 *		delim - a list of deliminators to be used.
 *
 *	Returns:
 *		Valid char* - points to the next token in the string.
 *		NULL - no more tokens can be retrieved from the string.
 */
char* strsep(char** str, const char* delim){
	return strsep2(str, delim, NULL);
}

#if !_XBOX && !defined(_WIN64) && !_PS3
// stristr /////////////////////////////////////////////////////////
//
// performs a case-insensitive lookup of a string within another
// (see C run-time strstr)
//
// str1 : buffer
// str2 : string to search for in the buffer
//
// example char* s = stristr("Make my day","DAY");
//
// S.Rodriguez, Jan 11, 2004
//
char* strstri(const char* str1, const char* str2)
{
	__asm
	{
		mov ah, 'A'
		mov dh, 'Z'

		mov esi, str1
		mov ecx, str2
		mov dl, [ecx]
		test dl,dl ; NULL?
		jz short str2empty_label

outerloop_label:
		mov ebx, esi ; save esi
		inc ebx
innerloop_label:
		mov al, [esi]
		inc esi
		test al,al
		je short str2notfound_label ; not found!

        cmp     dl,ah           ; 'A'
        jb      short skip1
        cmp     dl,dh           ; 'Z'
        ja      short skip1
        add     dl,'a' - 'A'    ; make lowercase the current character in str2
skip1:		

        cmp     al,ah           ; 'A'
        jb      short skip2
        cmp     al,dh           ; 'Z'
        ja      short skip2
        add     al,'a' - 'A'    ; make lowercase the current character in str1
skip2:		

		cmp al,dl
		je short onecharfound_label
		mov esi, ebx ; restore esi value, +1
		mov ecx, str2 ; restore ecx value as well
		mov dl, [ecx]
		jmp short outerloop_label ; search from start of str2 again
onecharfound_label:
		inc ecx
		mov dl,[ecx]
		test dl,dl
		jnz short innerloop_label
		jmp short str2found_label ; found!
str2empty_label:
		mov eax, esi // empty str2 ==> return str1
		jmp short ret_label
str2found_label:
		dec ebx
		mov eax, ebx // str2 found ==> return occurence within str1
		jmp short ret_label
str2notfound_label:
		xor eax, eax // str2 nt found ==> return NULL
		jmp short ret_label
ret_label:
	}

}
#else
char *strstri(const char *s,const char *srch)
{
	int		len = (int)strlen(srch);

	for(;*s;s++)
	{
		if (strnicmp(s,srch,len)==0)
			return (char*)s;
	}
	return 0;
}
#endif

const char* strstriConst(const char* str1, const char* str2)
{
	return strstri((char*)str1, str2);
}

int tokenize_line_safe(char *buf,char *args[],int max_args,char **next_line_ptr)
{
	char	*s,*next_line;
	int		i,idx;

	if (!buf)
		return 0;
	s = buf;
	for(i=0;;)
	{
		if (i < max_args)
			args[i] = 0;
		if (*s == ' ' || *s == '\t')
			s++;
		else if (*s == 0)
		{
			next_line = 0;
			break;
		}
		else if (*s == '"')
		{
			if (i < max_args)
				args[i] = s+1;
			i++;
			s = strchr(s+1,'"');
			if (!s) // bad input string
			{
				if (next_line_ptr)
					*next_line_ptr = 0;
				return 0;
			}
			*s++ = 0;
		}
		else
		{
			if (*s != '\r' && *s != '\n')
			{
				if (i < max_args)
					args[i] = s;
				i++;
			}
			idx = (int)strcspn(s,"\n\r \t");
			s += idx;
			if (*s == ' ' || *s == '\t')
				*s++ = 0;
			else
			{
				if (*s == 0)
					next_line = 0;
				else
				{
					if (s[-1] == '\r')
						s[-1] = 0;
					if (s[0]=='\r' && s[1] == '\n') {
						*s=0;
						s++;
					}
					*s = 0;
					next_line = s+1;
				}
				if (i < max_args)
					args[i] = 0;
				break;
			}
		}
	}
	if (next_line_ptr)
		*next_line_ptr = next_line;
	return MIN(i, max_args);
}

int tokenize_line_quoted_safe(char *buf,char *args[],int maxargs,char **next_line_ptr)
{
	int		i,count;
	size_t  len;
	char	*s;

	count = tokenize_line_safe(buf,args,maxargs,next_line_ptr);
	for(i=0;i<count;i++)
	{
		s = args[i];
		len = (int)strlen(s);
		if (!s[0] || strcspn(s," \t") != len)
		{
			s[len+1] = 0;
			*(--args[i]) = s[len] = '"';
		}
	}
	return count;
}

char *strtok_quoted_r(char *target,const char *startdelim,const char *enddelim, char **last)
{
	int		start;
	char	*base;

	base = target;
	if ( target != 0 )
	{
		*last = target;
	}
	else target = *last;

	if ( !target || *target == '\0' ) 
		return 0;

	start = (int)strspn(target,startdelim);
	target = &target[start];
	if (target[0] == '"')
	{
		enddelim = "\"";
		target++;
	}

	if ( *target == '\0' )
	{
		/* failure to find 'start', remember and return */
		*last = target;
		return 0;
    }

    /* found beginning of token, now look for end */
	if ( *(*last = target + strcspn(target,enddelim)) != '\0')
	{
		*(*last)++ = '\0';
	}
	return target;
}

char *strtok_quoted(char *target,const char *startdelim,const char *enddelim)
{
	static char *last;
	return strtok_quoted_r(target,startdelim,enddelim,&last);
}

int tokenize_line_quoted_delim(char *buf,char *args[],int max_args,char **next_line_ptr, const char *startdelim, const char *enddelim)
{
	char *next_line, *s;
	int i;

	// first, get the line separated:
	s = buf + strcspn(buf,"\n\r");
	if (*s == 0)
		next_line = 0;
	else
	{
		if (s[-1] == '\r')
			s[-1] = 0;
		if (s[0]=='\r' && s[1] == '\n')
		{
			*s=0;
			s++;
		}
		*s = 0;
		next_line = s + 1;
	}

	// now tokenize the line:
	i = 0;
	s = strtok_quoted(buf, startdelim, enddelim);
	for (;;)
	{
		if (!s)
			break;

		if (i < max_args)
			args[i] = s;

		i++;
		s = strtok_quoted(0, startdelim, enddelim);
	}

	if (next_line_ptr)
		*next_line_ptr = next_line;

	return i;
}

char *getContainerValueStatic(char *container, char* fieldname)
{
	static char *fieldBuf = NULL;
	U32 nField;
	int i;
	
	if(!fieldBuf)
	{
		estrHeapCreate(&fieldBuf, 512, 0);
	}

	// ----------
	// try once, if the buffer is too small then resize and try again

	estrSetSize(&fieldBuf, 0);
	for( i = 0; i < 2; ++i ) 
	{
		if((nField = getContainerValue(container, fieldname, fieldBuf, estrGetCapacity(&fieldBuf))) < estrGetCapacity(&fieldBuf))
		{
			break;
		}
		else
		{
			// grow the buffer by a little more than is needed
			estrReserveCapacity(&fieldBuf, (nField*3)/2);
		}
	}

	return fieldBuf;
}

int getContainerValue(char *container, char* fieldname, char* result, int size)
{
	// scan lines looking for fieldname at start of line
	int lenRes = 0;
	int len = (int)strlen(fieldname);

	if(result)
		result[0] = 0;

	while (1)
	{
		if (!container || !container[0]) break;
		while (strchr(" \t", container[0])) container++;
		if (strnicmp(container, fieldname, len)==0)
		{
			char* valuestart = strchr(container, ' ');
			if (valuestart - container == len)
			{
				valuestart++;
				if (valuestart[0] == '"') valuestart++;
				while (1)
				{
					if (valuestart[0] == '"' || valuestart[0] == 0 || valuestart[0] == '\n')
					{
						if(result)
							result[0] = 0;
						return lenRes;				// BREAK IN FLOW
					}
					lenRes++;

					if(lenRes < size && result)
						*result++ = *valuestart++;
				}
			}
		}
		container = strchr(container, '\n');
		if (!container || !container[0]) break;
		container++;
	}
	return lenRes;
}

// WARNING: If you change this function, you must also update dynArrayAddBig_dbg() below.
void *dynArrayAddSmall_dbg(void **basep,int struct_size,int *countPtr,int *max_countPtr,int num_structs MEM_DBG_PARMS)
{
	char	*base = *basep;
	int count = *countPtr;
	int max_count = *max_countPtr;

	PERFINFO_AUTO_START_FUNC();
	
	if (count > max_count - num_structs)
	{
		if (!max_count)
			max_count = num_structs;
		(max_count) <<= 1;
		if (num_structs > 1)
			(max_count) += num_structs;
		base = srealloc(base,struct_size * max_count);
		assert(base);
		memset(base + struct_size * count,0,(max_count - count) * struct_size); //CD: see comment below
		*max_countPtr = max_count;
		*basep = base;
	}

	// CD: Putting this here guarantees that memory will be zeroed.  Otherwise, if the count is reset the memory will not get zeroed.
	memset(base + struct_size * count,0,struct_size * num_structs);

	count+=num_structs;
	*countPtr = count;
	
	PERFINFO_AUTO_STOP();
	
	return base + struct_size * (count - num_structs);
}

// WARNING: If you change this function, you must also update dynArrayAddSmall_dbg() above.
void *dynArrayAddBig_dbg(void **basep,size_t struct_size,size_t *countPtr,size_t *max_countPtr,size_t num_structs MEM_DBG_PARMS)
{
	char	*base = *basep;
	size_t count = *countPtr;
	size_t max_count = *max_countPtr;

	PERFINFO_AUTO_START_FUNC();

	if (count + num_structs > max_count)
	{
		if (!max_count)
			max_count = num_structs;
		(max_count) <<= 1;
		if (num_structs > 1)
			(max_count) += num_structs;
		base = srealloc(base,struct_size * max_count);
		assert(base);
		memset(base + struct_size * count,0,(max_count - count) * struct_size); //CD: see comment below
		*max_countPtr = max_count;
		*basep = base;
	}

	// CD: Putting this here guarantees that memory will be zeroed.  Otherwise, if the count is reset the memory will not get zeroed.
	memset(base + struct_size * count,0,struct_size * num_structs);

	count+=num_structs;
	*countPtr = count;
	
	PERFINFO_AUTO_STOP();
	
	return base + struct_size * (count - num_structs);
}

// same as dynArrayAdd, but for arrays of struct pointers instead of arrays of structs
// WARNING: If you change this function, you must also update dynArrayAddpBig_dbg() below.
void *dynArrayAddpSmall_dbg(void ***basep,int *count,int *max_count,void *ptr MEM_DBG_PARMS)
{
	void	**mem;

	mem = dynArrayAddSmall_dbg((void *)basep,sizeof(void *),count,max_count,1 MEM_DBG_PARMS_CALL);
	*mem = ptr;
	return mem;
}

// same as dynArrayAdd, but for arrays of struct pointers instead of arrays of structs
// WARNING: If you change this function, you must also update dynArrayAddpSmall_dbg() above.
void *dynArrayAddpBig_dbg(void ***basep,size_t *count,size_t *max_count,void *ptr MEM_DBG_PARMS)
{
	void	**mem;

	mem = dynArrayAddBig_dbg((void *)basep,sizeof(void *),count,max_count,1 MEM_DBG_PARMS_CALL);
	*mem = ptr;
	return mem;
}

// WARNING: If you change this function, you must also update dynArrayReserveBig_dbg() below.
void *dynArrayReserveSmall_dbg(void **basep,int struct_size,int *max_count,int reserve_max MEM_DBG_PARMS)
{
	int		last_max = *max_count;
	char	*base = *basep;

	PERFINFO_AUTO_START_FUNC();

	if (reserve_max > last_max)
	{
		// MAK - fix to memory bug (was trying to realloc to zero size when asked to fit in element 0)
		if (!reserve_max) 
			reserve_max = 1;
		*max_count = reserve_max;

		base = srealloc(base,struct_size * reserve_max);
		memset(base + struct_size * last_max,0,(reserve_max - last_max) * struct_size);
		*basep = base;
	}
	
	PERFINFO_AUTO_STOP();
	
	return base;
}

// WARNING: If you change this function, you must also update dynArrayReserveSmall_dbg() above.
void *dynArrayReserveBig_dbg(void **basep,size_t struct_size,size_t *max_count,size_t reserve_max MEM_DBG_PARMS)
{
	size_t		last_max = *max_count;
	char	*base = *basep;

	PERFINFO_AUTO_START_FUNC();

	if (reserve_max > last_max)
	{
		// MAK - fix to memory bug (was trying to realloc to zero size when asked to fit in element 0)
		if (!reserve_max) 
			reserve_max = 1;
		*max_count = reserve_max;

		base = srealloc(base,struct_size * reserve_max);
		memset(base + struct_size * last_max,0,(reserve_max - last_max) * struct_size);
		*basep = base;
	}
	
	PERFINFO_AUTO_STOP();
	
	return base;
}

// WARNING: If you change this function, you must also update dynArrayFitBig_dbg() below.
void *dynArrayFitSmall_dbg(void **basep,int struct_size,int *max_count,int idx_to_fit MEM_DBG_PARMS)
{
	int		last_max;
	char	*base = *basep;

	PERFINFO_AUTO_START_FUNC();

	if (idx_to_fit >= *max_count)
	{
		last_max = *max_count;
		if (!*max_count || !idx_to_fit)
			*max_count = idx_to_fit + 1;
		else
			*max_count = idx_to_fit * 2;
		base = srealloc(base,struct_size * *max_count);
		//if (struct_size * *max_count > 5000000)
		//	printf("");
		memset(base + struct_size * last_max,0,(*max_count - last_max) * struct_size);
		*basep = base;
	}
	
	PERFINFO_AUTO_STOP();
	
	return base + struct_size * idx_to_fit;
}

// WARNING: If you change this function, you must also update dynArrayFitSmall_dbg() above.
void *dynArrayFitBig_dbg(void **basep,size_t struct_size,size_t *max_count,size_t idx_to_fit MEM_DBG_PARMS)
{
	size_t	last_max;
	char	*base = *basep;

	PERFINFO_AUTO_START_FUNC();

	if (idx_to_fit >= *max_count)
	{
		last_max = *max_count;
		if (!*max_count || !idx_to_fit)
			*max_count = idx_to_fit + 1;
		else
			*max_count = idx_to_fit * 2;
		base = srealloc(base,struct_size * *max_count);
		//if (struct_size * *max_count > 5000000)
		//	printf("");
		memset(base + struct_size * last_max,0,(*max_count - last_max) * struct_size);
		*basep = base;
	}
	
	PERFINFO_AUTO_STOP();
	
	return base + struct_size * idx_to_fit;
}

char *getFileName(char *fname)
{
	char	*s;
	char *ret = fname;
	for (s=fname; *s; s++)
		if (*s == '/' || *s == '\\')
			ret = s+1;
	return ret;

}

const char *getFileNameConst(const char *fname)
{
	const char	*s;
	const char *ret = fname;
	for (s=fname; *s; s++)
		if (*s == '/' || *s == '\\')
			ret = s+1;
	return ret;
}

char *getFileNameNoExt_s(char *dest, size_t dest_size, const char *filename)
{
	char *s;
	strcpy_s(dest, dest_size, getFileNameConst(filename));
	s = strrchr(dest, '.');
	if (s)
		*s = '\0';
	return dest;
}

char *getFileNameNoDir_s(char *dest, size_t dest_size, const char *filename)
{
	char *s;
	size_t iLen;

	strcpy_s(dest, dest_size, filename);

	iLen = strlen(dest);

	s = dest + iLen - 1;

	while (s >=dest)
	{
		if (*s == '/' || *s == '\\')
		{
			memmove(dest, s + 1, iLen - (s - dest));
			break;
		}

		s--;
	}

	return dest;
}


char *getFileNameNoExtNoDirs_s(char *dest, size_t dest_size, const char *filename)
{
	int iLen;
	int iFirstChar = 0;
	int iLastChar;
	int i;
	bool bFoundExt = false;

	iLen = (int)strlen(filename);

	iLastChar = iLen - 1;

	for (i=iLastChar; i >= 0; i--)
	{
		if (filename[i] == '.' && !bFoundExt)
		{
			
			bFoundExt = true;
			iLastChar = i - 1;
		
		}
		else if (filename[i] == '/' || filename[i] == '\\')
		{
			iFirstChar = i+1;
			break;
		}
	}

	if (iFirstChar >= iLastChar - 1)
	{
		dest[0] = 0;
	}
	else
	{
		assertmsgf(iLastChar - iFirstChar < (int)dest_size - 2, "Insufficient buffer size doing getFileNameNoExtNoDirs on %s", filename);

		memcpy(dest, filename + iFirstChar, iLastChar - iFirstChar + 1);
		dest[iLastChar - iFirstChar + 1] = 0;
	}

	return dest;
}

/* getDirectoryName()
 *	Given a path to a file, this function returns the directory name
 *	where the file exists.
 *
 *	Note that this function will alter the given string to produce the
 *	directory name.
 */
char *getDirectoryName(char *fullPath)
{
	char	*cursor;

	if (!fullPath)
		return 0;
	forwardSlashes(fullPath);
	cursor = strrchr(fullPath,'/');
	if (cursor)
		*cursor = '\0';
	else
		fullPath[0] = '\0'; // Just a bare filename, no directory to return.
	if(fullPath[0] && fullPath[1] == ':' && !fullPath[2]){
		fullPath[2] = '\\';
		fullPath[3] = '\0';
	}
	return fullPath;
}


char *FindExtensionFromFilename(char *pFileName)
{
	char *pLastDot = strrchr(pFileName, '.');
	char *pLastBackSlash;
	char *pLastForwardSlash;

	if (!pLastDot)
	{
		return NULL;
	}

	pLastBackSlash = strrchr(pFileName, '\\');
	pLastForwardSlash = strrchr(pFileName, '/');

	if (pLastBackSlash > pLastDot || pLastForwardSlash > pLastDot)
	{
		return NULL;
	}

	return pLastDot;
}



// Takes a path such as C:\game\data and turns it into game\data (no trailing or leading slashes)
char *getDirString(const char *path)
{
	static char ret[CRYPTIC_MAX_PATH];
	strcpy(ret, path);
	backSlashes(ret);
	while (strEndsWith(ret, "\\")) {
		ret[strlen(ret)-1]=0;
	}
	if (ret[1]==':') {
		strcpy(ret, ret+2);
	}
	while (ret[0]=='\\') {
		strcpy(ret, ret+1);
	}
	return ret;
}

// adds this prefix to the filename part of the path
char *addFilePrefix(const char* path, const char* prefix)
{
	static char ret[CRYPTIC_MAX_PATH+20];

	strcpy(ret, path);
	getDirectoryName(ret);
	strcat(ret, "/");
	strcat(ret, prefix);
	strcat(ret, getFileNameConst(path));
	return ret;
}

//-----------------------------------------------------------------------
//
//
//
void initStuffBuff_dbg( StuffBuff *sb, int size MEM_DBG_PARMS)
{
	ZeroStruct(sb);
	MEM_DBG_STRUCT_PARMS_INIT(sb);
	sb->buff = stcalloc( size, 1, sb );
	if( !sb->buff )
		size = 0;
	sb->size = size;
}

void resizeStuffBuff( StuffBuff *sb, int size )
{
	if (size < sb->idx - 1)
	{
		//don't let it truncate
		return;
	}

	sb->buff = strealloc(sb->buff, size, sb);

	if (size > sb->size)
	{
		memset(sb->buff + sb->size,0,size - sb->size);
	}

	sb->size = size;
}



//
//
void clearStuffBuff(StuffBuff *sb)
{
	sb->idx = 0;

	if(sb->buff)
		sb->buff[0] = '\0';
}

void addStringToStuffBuff_fv(SA_PARAM_NN_VALID StuffBuff *sb,const char *fmt,va_list va)
{
	int			size,bytes_left;

	for(;;)
	{
		bytes_left = sb->size - sb->idx - 1;
		size = quick_vsnprintf(sb->buff + sb->idx,bytes_left+1,_TRUNCATE,fmt,va);

		if (size < 0)
		{
			// encoding failed
		}

		if (size >= 0) {
			// Wrote successfully
			sb->idx += size;
			if (size == bytes_left) {
				// It wasn't null terminated, but we saved a byte in advance
				sb->buff[sb->idx] = '\0';
			}

			sb->idx++; // keep the null terminator
			break;
		}

		sb->size *= 2;
		sb->buff = strealloc(sb->buff, sb->size, sb);
	}
}

//
//
#undef addStringToStuffBuff
void addStringToStuffBuff( StuffBuff *sb, const char *fmt, ... )
{
	va_list		va;
	va_start( va, fmt );

	addStringToStuffBuff_fv(sb,fmt,va);

	va_end( va );
}

void addIntToStuffBuff( StuffBuff *sb, int i)
{
	char buf[64];
	itoa(i, buf, 10);
	addSingleStringToStuffBuff(sb, buf);
}

//
//
void addSingleStringToStuffBuff( StuffBuff *sb, const char *s)
{
	int			bytes_left;
	char		*cursor;
	int			loop=1;

	assert(s);
	
	bytes_left = sb->size - sb->idx - 1;
	cursor = sb->buff + sb->idx;
	while (loop) {
		if (bytes_left == 0) {
			sb->size *= 2;
			sb->buff = strealloc(sb->buff, sb->size, sb);
			cursor = sb->buff + sb->idx;
			bytes_left = sb->size - sb->idx - 1;
		}
		if (*s) { // not the end of the string
			sb->idx++;
			bytes_left--;
		} else {
			loop=0;
		}
		*cursor++ = *s++;
	}
}

void addBinaryDataToStuffBuff( StuffBuff *sb, const char *data, int len)
{
	int			bytes_left;
	char		*cursor;

	bytes_left = sb->size - sb->idx - 1;
	cursor = sb->buff + sb->idx;
	while (len) {
		if (bytes_left <= 0) {
			sb->size *= 2;
			if (sb->size < sb->idx + len) {
				sb->size = sb->idx + len + 1;
			}
			sb->buff = strealloc(sb->buff, sb->size, sb);
			cursor = sb->buff + sb->idx;
			bytes_left = sb->size - sb->idx - 1;
		}
		sb->idx++;
		bytes_left--;
		*cursor++ = *data++;
		len--;
	}
}


//
//
void freeStuffBuff( StuffBuff * sb )
{
	free( sb->buff );
	ZeroStruct(sb);
}

int firstBits( int val )
{
	int	i = 0;

	while( val && !( val & 1 ) )
	{
		val = val >> 1;
		i++;
	}

	return i;
}



// Escapes data and places the escaped data in a static buffer
size_t escapeDataStatic(const char *instring, size_t inlen, char * outstring, size_t outlen, bool stopOnNull)
{
	const char *c;
	char *out;
	size_t incount,outcount;
	int nulSpace = 0;
	bool stopLoop = false;
	if (stopOnNull)
	{
		// Save room for a nul character
		nulSpace = 1;
		if (inlen == 0)
		{
			inlen = U32_MAX;
		}
	}

	if(!instring || !outstring)
	{
		return 0;
	}

	for (incount=0,outcount=0, c=instring, out=outstring; 
		!stopLoop && incount<inlen && outcount <outlen - nulSpace; 
		c++, incount++)
	{
		switch (*c)
		{
		case '\\':
			*out++='\\';outcount++;
			*out++='\\';outcount++;
			break;
		case '\n':
			*out++='\\';outcount++;
			*out++='n';outcount++;
			break;
		case '\r':
			*out++='\\';outcount++;
			*out++='r';outcount++;
			break;
		case '\t':
			*out++='\\';outcount++;
			*out++='t';outcount++;
			break;
		case '\"':
			*out++='\\';outcount++;
			*out++='q';outcount++;
			break;
		case '\'':
			*out++='\\';outcount++;
			*out++='s';outcount++;
			break;
		case '%':
			*out++='\\';outcount++;
			*out++='p';outcount++;
			break;
		case '\0':
			if (stopOnNull)
			{
				stopLoop = true;
				break;
			}
			*out++='\\';outcount++;
			*out++='0';outcount++;
			break;
		case '$':
			*out++='\\';outcount++;
			*out++='d';outcount++;
			break;
		default:
			*out++=*c;outcount++;
		}
	}
	if (stopOnNull)
	{
		// Add the trailing null if necessary
		*out=0;
	}
	return outcount;
}

size_t escapeString_s(const char *instring,char * outstring, size_t outlen)
{
	return escapeDataStatic(instring,0,outstring,outlen,true);
}

const char *escapeString_unsafe(const char *instring)
{
	size_t len = strlen(instring);
	static char *ret=NULL;
	static size_t retlen=0;
	size_t deslen;

	deslen = len*2+1;
	if (!ret || retlen < deslen) // We need a bigger buffer to return the data
	{
		if (ret)
			free(ret);
		if (deslen<256)
			deslen = 256; // Allocate at least 256 bytes the first time
		ret = malloc(deslen);
		assert(ret);
		retlen = deslen;
	}
	escapeString_s(instring,ret,retlen);
	return ret;
}



// Unescapes the given string and places the result in a static buffer

size_t unescapeDataStatic(const char *instring, size_t inlen, char * outstring, size_t outlen, bool stopOnNull)
{
	const char *c;
	char *out;
	size_t incount,outcount;
	int nulSpace = 0;
	bool inescape = false;
	if (stopOnNull)
	{
		// Save room for a nul character
		nulSpace = 1;
		if (inlen == 0)
		{
			inlen = U32_MAX;
		}
	}

	if(!instring || !outstring)
	{
		return 0;
	}

	for (incount=0,outcount=0, c=instring, out=outstring; 
		incount<inlen && outcount <outlen - nulSpace; 
		c++, incount++)
	{
		if (*c == '\0' && stopOnNull)
		{
			break;
		}
		else if (inescape)
		{
			inescape = false;
			outcount++;
			switch(*c){
			case 'n':
				*out++='\n';
				break;
			case 'r':
				*out++='\r';
				break;
			case 't':
				*out++='\t';
				break;
			case 'q':
				*out++='\"';
				break;
			case 's':
				*out++='\'';
				break;
			case '\\':
				*out++='\\';
				break;
			case 'p':
				*out++='%';
				break;
			case '0':
				*out++='\0';
				break;
			case 'd':
				*out++='$';
				break;
			default:
				*out++=*c;
			}
		}
		else if (*c == '\\')
		{
			inescape = true;
		}
		else
		{
			*out++=*c;
			outcount++;
		}		
	}
	if (stopOnNull)
	{
		// Add the trailing null if necessary
		*out=0;
	}
	return outcount;
}

size_t unescapeString_s(const char *instring,char * outstring, size_t outlen)
{
	return unescapeDataStatic(instring,0,outstring,outlen,true);
}

const char *unescapeString_unsafe(const char *instring)
{
	size_t len = strlen(instring);
	static char *ret=NULL;
	static size_t retlen=0;
	size_t deslen;

	deslen = (int)strlen(instring)+1;
	if (!ret || retlen < deslen) { // We need a bigger buffer to return the data
		if (ret)
			free(ret);
		if (deslen<256) deslen = 256; // Allocate at least 256 bytes the first time
		ret = malloc(deslen);
		assert(ret);
		retlen = deslen;
	}
	unescapeString_s(instring,ret,retlen);
	return ret;
}


static char lastmatch[CRYPTIC_MAX_PATH];
char *getLastMatch() { // Returns the portion that matched in the last call
	return lastmatch;
}

static bool eq(const char c0, const char c1) {
	return c0==c1 || (c0=='/' && c1=='\\') || (c0=='\\' && c1=='/') || toupper((unsigned char)c0)==toupper((unsigned char)c1);
}

// This version of simpleMatch doesn't do the weird prefix matching thing, so "the" does not match "then"
bool simpleMatchExact(const char *exp, const char *tomatch)
{
	int l1, l2, i;

	if (strchr(exp, '*'))
		return simpleMatch(exp, tomatch);

	l1 = (int)strlen(exp);
	l2 = (int)strlen(tomatch);

	if (l1 != l2)
		return 0;

	for (i = 0; i < l1; i++)
	{
		if (!eq(exp[i], tomatch[i]))
			return 0;
	}

	return 1;
}

// Like simpleMatchExact(), but without folding and faster.
bool simpleMatchExactSensitiveFast(const char *exp, const char *tomatch)
{
	const char *star = strchr(exp, '*');
	return !strncmp(tomatch, exp, star - exp) && strEndsWithSensitive(tomatch + (star - exp), star + 1);
}

// simpleMatch assumes that exp is a prefix, so simpleMatch("the", "then") is true
bool simpleMatch(const char *exp, const char *tomatch) {
	char exp2Buffer[1000];
	char *exp2;
	char *pre;
	char *post = 0;
	char *star;
	bool matches = true;
	if(strlen(exp) < 1000) {
		Strcpy(exp2Buffer, exp);
		exp2 = exp2Buffer;
	} else {
		exp2 = strdup(exp);
	}
	pre = exp2;
	star = strchr(exp2, '*');
	if (star) {
		post = star+1;
		*star=0;
		if (strchr(post, '*')) {
			static bool printed_error=false;
			if (!printed_error) {
				printed_error=true;
				printf("Error: filespec \"%s\" contains more than one wildcard\n", exp);
			}
		}
	} else {
		post = exp2+strlen(exp2); // point to a null string
	}
	if (eq(*tomatch, '/') && !eq(*pre, '/')) tomatch++;
	// gimmeLog(LOG_DEBUG, "'%s'; pre='%s'; post='%s'; ", tomatch, pre, post);
	while (*pre && *tomatch && eq(*pre, *tomatch)) {
		pre++;
		tomatch++;
	}
	if (*pre) { // could not match prefix
		// gimmeLog(LOG_DEBUG, "pre NOMATCH");
		matches=false;
	} else { // check postfix
		int postidx=(int)strlen(post);
		int tomatchidx=(int)strlen(tomatch);

		strcpy(lastmatch, tomatch);
		// gimmeLog(LOG_DEBUG, "pre match ");
		if (postidx>tomatchidx) { // post is longer than the remaining tomatch
			// gimmeLog(LOG_DEBUG, "post NOMATCH_LEN");
			matches=false;
		} else {
			while (postidx>=0 && eq(post[--postidx], tomatch[--tomatchidx]));
			if (postidx!=-1) {
				// gimmeLog(LOG_DEBUG, "post NOMATCH");
				matches=false;
			} else {
				// gimmeLog(LOG_DEBUG, "post match");
				lastmatch[tomatchidx+1]=0;
			}
		}
	}
	if(exp2 != exp2Buffer){
		free(exp2);
	}
	return matches;
}

// If you change this function, please change matchExactSensitive() below.
bool matchExact(const char *exp, const char *tomatch)
{
	bool ret;
	const char *pExp, *pStr;
	pExp=exp;
	pStr=tomatch;
	// Match up until first star or things don't match
	for (; *pExp && *pStr && *pExp!='*' && eq(*pExp, *pStr); pExp++, pStr++);
	if (*pExp=='*') {
		// Recursively try and match
		while (*pExp=='*')
			pExp++;
		// Now points at first character to match
		if (!*pExp) {
			// Matches!
			ret = true;
		} else {
			ret = false;
			for (; *pStr; pStr++) {
				if (eq(*pExp, *pStr) && matchExact(pExp, pStr)) {
					ret = true;
					break;
				}
			}
		}
	} else {
		// EOS or didn't match
		if (eq(*pExp, *pStr))
			ret = true;
		else
			ret = false;
	}
	return ret;
}

// This is a copy of matchExact() that doesn't use eq().  Please keep it in sync.
bool matchExactSensitive(const char *exp, const char *tomatch)
{
	bool ret;
	const char *pExp, *pStr;
	pExp=exp;
	pStr=tomatch;
	// Match up until first star or things don't match
	for (; *pExp && *pStr && *pExp!='*' && (*pExp == *pStr); pExp++, pStr++);
	if (*pExp=='*') {
		// Recursively try and match
		while (*pExp=='*')
			pExp++;
		// Now points at first character to match
		if (!*pExp) {
			// Matches!
			ret = true;
		} else {
			ret = false;
			for (; *pStr; pStr++) {
				if ((*pExp == *pStr) && matchExactSensitive(pExp, pStr)) {
					ret = true;
					break;
				}
			}
		}
	} else {
		// EOS or didn't match
		if (*pExp == *pStr)
			ret = true;
		else
			ret = false;
	}
	return ret;
}

// Slow O(n^m) where n is number of characters in tomatch and m is the number of wildcards + 1
bool match(const char *exp, const char *tomatch)
{
	bool ret;
	if (!strchr(exp, '*')) {
		size_t len = strlen(exp)+2;
		char *exp2=ScratchAlloc(len);
		strcpy_s(exp2, len, exp);
		if (!strchr(exp2, '*'))
			strcat_s(exp2, len, "*");
		ret = matchExact(exp2, tomatch);
		ScratchFree(exp2);
	} else {
		ret = matchExact(exp, tomatch);
	}
	return ret;
}

char *itoa_with_grouping(int i, char *buf, int radix, int groupsize, int decimalplaces, char sep, char dp, int leftzeropad)
{
	char ach[255];
	int iLen;
	int iStart = 0;
	bool whole = false;
	char *pch = buf;

	itoa(i, ach, radix);

	iLen = (int)strlen(ach);

	// Deal with a leading sign character.
	if (ach[iStart]<'0' || ach[iStart]>'9')
	{
		*pch++ = ach[iStart];
		iStart++;
	}

	leftzeropad -= iLen;
	while (leftzeropad-- > 0)
		*pch++ = '0';

	// Always take the first non-fractional digit to prevent accidental
	// prepending of a separator (eg. no ",100,000,000")
	if((iLen-iStart)>decimalplaces)
	{
		*pch++ = ach[iStart];
		iStart++;
		whole = true;
	}

	for(i=iLen-iStart; i>decimalplaces; i--)
	{
		if((i-decimalplaces)%groupsize==0)
		{
			*pch++ = sep;
		}
		*pch++ = ach[iLen-i];
		whole = true;
	}

	if(decimalplaces)
	{
		// There was no whole part (the number is just a fraction) so write
		// a zero in lieu of one.
		if(!whole)
		{
			*pch++ = '0';
		}

		*pch++ = dp;
		for(i=decimalplaces; i>0; i--)
		{
			// We might have been asked for more decimal places than
			// are available. Pad the fractional part with leading
			// zeroes if so.
			if((iLen-iStart)<i)
			{
				*pch++ = '0';
			}
			else
			{
				*pch++ = ach[iLen-i];
			}
		}
	}
	*pch++ = '\0';

	return buf;
}

// Like strstr(), but the string to be searched is not null-terminated.
// This function could be further optimized, if necessary.
char *memstr(const char *haystack, const char *needle, size_t haystack_size)
{
	size_t needle_len = strlen(needle);
	size_t i;

	if (!needle_len)
		return (char *)haystack;

	if (haystack_size < needle_len)
		return NULL;

	for (i = 0; i != haystack_size - needle_len + 1; ++i)
	{
		if (!memcmp(haystack + i, needle, needle_len))
			return (char *)(haystack + i);
	}

	return NULL;
}

bool memIsZero(const void *ptr, U32 num_bytes)
{
	const U8 *t, *p = ptr;
	U32 z = 0;

	if (num_bytes < 8)
		goto bytes_loop;

	t = (const U8 *) (((intptr_t) p + num_bytes) & ~7UL);
	num_bytes = ((intptr_t) p + num_bytes) & 7;

	switch ((intptr_t)p & 7)
	{
	case 1:
		z |= *p++;
	case 2:
		z |= *p++;
	case 3:
		z |= *p++;
	case 4:
		z |= *p++;
	case 5:
		z |= *p++;
	case 6:
		z |= *p++;
	case 7:
		z |= *p++;
		if (z)
			return false;
	}

	for (; p < t; p += 8)
		if (*(const U64 *) p)
			return false;

bytes_loop:
	switch (num_bytes)
	{
	case 7:
		z |= *p++;
	case 6:
		z |= *p++;
	case 5:
		z |= *p++;
	case 4:
		z |= *p++;
	case 3:
		z |= *p++;
	case 2:
		z |= *p++;
	case 1:
		z |= *p++;
	}

	return !z;
}

