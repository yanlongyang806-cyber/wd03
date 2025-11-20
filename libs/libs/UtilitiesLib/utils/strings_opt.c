#include <string.h>
#include "strings_opt.h"

#include "timing.h"
#include <errno.h>
#include <yvals.h>
#include "EString.h"
#include "MemAlloc.h"

#if _PS3

#ifdef strcpy
#undef strcpy
#undef strncpy
#undef strcat
#undef strncat
#undef sscanf
#endif

#ifdef sprintf


#undef sprintf_s 
#undef sprintf
#undef snprintf
#undef vsnprintf
#undef vsprintf
#undef vsprintf_s
#undef _vsnprintf

#endif

#ifdef printf

#undef printf 
#undef _vscprintf 
#undef vprintf 

#endif

//YVS

int _vscprintf(const char *pform, va_list al)
{
    return vsnprintf(0, 0, pform, al);
}

errno_t _itoa_s(int value, char * dst, size_t size, int radix)
{
    assert(radix==10);
    sprintf(dst, "%d", value);
    return 0;
}

errno_t memcpy_s(void * dst, size_t sizeInBytes, const void * src, size_t count)
{
    if (count == 0)
    {
        /* nothing to do */
        return 0;
    }

    /* validation section */
    if(dst == NULL)
        return EINVAL;
    if (src == NULL || sizeInBytes < count)
    {
        /* zeroes the destination buffer */
        memset(dst, 0, sizeInBytes);

        if(src == NULL)
            return EINVAL;
        if(sizeInBytes < count)
            return ERANGE;
        /* useless, but prefast is confused */
        return EINVAL;
    }

    memcpy(dst, src, count);
    return 0;
}


errno_t memmove_s(void * dst, size_t sizeInBytes, const void * src, size_t count)
{
    if (count == 0)
    {
        /* nothing to do */
        return 0;
    }

    /* validation section */
    if(dst == NULL)
        return EINVAL;
    if (src == NULL || sizeInBytes < count)
    {
        if(src == NULL)
            return EINVAL;
        if(sizeInBytes < count)
            return ERANGE;
        /* useless, but prefast is confused */
        return EINVAL;
    }

	/* depending on relative positions of pointer, controls order we can safely copy byte */
	if (dst < src)
	{
		while(count-- > 0)
		{
			*(char *)dst++ = *(char *)src++;
		}
	}
	else
	{
		dst += count;
		src += count;
		while(count-- > 0)
		{
			*(char *)dst-- = *(char *)src--;
		}
	}

    return 0;
}

errno_t strcat_s(char *dst, size_t size, const char *src)
{
    char *p;
    size_t available;

    /* validation section */
    if(!(dst != 0 && size > 0)) {
        return EINVAL;
    }
    if (src == NULL)
    {
        *dst = 0;
        return EINVAL;
    }

    p = dst;
    available = size;
    while (available > 0 && *p != 0)
    {
        p++;
        available--;
    }

    if (available == 0)
    {
		*dst = 0;
        return EINVAL;
    }

    while ((*p++ = *src++) != 0 && --available > 0)
    {
    }

    if (available == 0)
    {
		*dst = 0;
        return ERANGE;
    }
    return 0;
}

errno_t strncat_s(char *dst, size_t size, const char *src, size_t count)
{
    char *p;
    size_t available;

    if (count == 0 && dst == NULL && size == 0)
    {
        /* this case is allowed; nothing to do */
        return 0;
    }

    /* validation section */
    if(!(dst != 0 && size > 0)) {
        return EINVAL;
    }
    if (count != 0)
    {
        if (src == NULL)
        {
            *dst = 0;
            return EINVAL;
        }
    }

    p = dst;
    available = size;
    while (available > 0 && *p != 0)
    {
        p++;
        available--;
    }

    if (available == 0)
    {
        *dst = 0;
        return EINVAL;
    }

    if (count == _TRUNCATE)
    {
        while ((*p++ = *src++) != 0 && --available > 0)
        {
        }
    }
    else
    {
        assert(count < available); // Buffer is too small

        while (count > 0 && (*p++ = *src++) != 0 && --available > 0)
        {
            count--;
        }
        if (count == 0)
        {
            *p = 0;
        }
    }

    if (available == 0)
    {
        if (count == _TRUNCATE)
        {
            dst[size - 1] = 0;
            return STRUNCATE;
        }
        *dst = 0;
        return ERANGE;
    }
    return 0;
}


errno_t strcpy_s(char *dst, size_t size, const char *src)
{
    char *p;
    size_t available;

    /* validation section */
    if(!(dst != 0 && size > 0)) {
        return EINVAL;
    }
    if (src == NULL)
    {
        *dst = 0;
        return EINVAL;
    }

    p = dst;
    available = size;
    while ((*p++ = *src++) != 0 && --available > 0)
    {
    }

    if (available == 0)
    {
		*dst = 0;
        return ERANGE;
    }
    return 0;
}

errno_t strncpy_s(char *dst, size_t size, const char *src, size_t count)
{
    char *p;
    size_t available;

    if (count == 0 && dst == NULL && size == 0)
    {
        /* this case is allowed; nothing to do */
        return 0;
    }

    /* validation section */
    if(!(dst != 0 && size > 0)) {
        return EINVAL;
    }
    if (count == 0)
    {
        /* notice that the source string pointer can be NULL in this case */
        *dst = 0;
        return 0;
    }
    if (src == NULL)
    {
        *dst = 0;
        return EINVAL;
    }

    p = dst;
    available = size;
    if (count == _TRUNCATE)
    {
        while ((*p++ = *src++) != 0 && --available > 0)
        {
        }
    }
    else
    {
        // YVS: not counted correctly in CRT
        assert(count <= size); // Buffer is too small

        while ((*p++ = *src++) != 0 && --available > 0 && --count > 0)
        {
        }
        if (count == 0)
        {
            *p = 0;
        }
    }

    if (available == 0)
    {
        if (count == _TRUNCATE)
        {
            dst[size - 1] = 0;
            return STRUNCATE;
        }
        *dst = 0;
        return ERANGE;
    }
    return 0;
}

char *strtok_s(char *_String, const char *_Control, char **_Context)
{
    char *str;
    const char *ctl = _Control;
    char map[32];
    int count;

    /* validation section */
    if(!_Context)
        return NULL;
    if(!_Control)
        return NULL;
    if(!(_String != NULL || *_Context != NULL))
        return NULL;

    /* Clear control map */
    for (count = 0; count < 32; count++)
    {
        map[count] = 0;
    }

    /* Set bits in delimiter table */
    do {
        map[*ctl >> 3] |= (1 << (*ctl & 7));
    } while (*ctl++);

    /* If string is NULL, set str to the saved
    * pointer (i.e., continue breaking tokens out of the string
    * from the last strtok call) */
    if (_String != NULL)
    {
        str = _String;
    }
    else
    {
        str = *_Context;
    }

    /* Find beginning of token (skip over leading delimiters). Note that
    * there is no token iff this loop sets str to point to the terminal
    * null (*str == 0) */
    while ((map[*str >> 3] & (1 << (*str & 7))) && *str != 0)
    {
        str++;
    }

    _String = str;

    /* Find the end of the token. If it is not the end of the string,
    * put a null there. */
    for ( ; *str != 0 ; str++ )
    {
        if (map[*str >> 3] & (1 << (*str & 7)))
        {
            *str++ = 0;
            break;
        }
    }

    /* Update context */
    *_Context = str;

    /* Determine if a token has been found. */
    if (_String == str)
    {
        return NULL;
    }
    else
    {
        return _String;
    }
}

#endif

// Note: currently, opt_strnicmp and opt_strupr are not used

int opt_strnicmp(const char * src, const char * dst, size_t count)
{
	if ( count )
	{
		//		if ( __lc_handle[LC_CTYPE] == _CLOCALEHANDLE ) {
		int f, l;
		do {

			if ( ((f = (unsigned char)(*(src++))) >= 'A') &&
				(f <= 'Z') )
				f -= 'A'-'a';

			if ( ((l = (unsigned char)(*(dst++))) >= 'A') &&
				(l <= 'Z') )
				l -= 'A'-'a';

		} while ( --count && f && (f == l) );
		//		}
		//		else { // localized version?
		//			int f,l;
		//			do {
		//				f = tolower( (unsigned char)(*(dst++)) );
		//				l = tolower( (unsigned char)(*(src++)) );
		//			} while (--count && f && (f == l) );
		//		}

		return( f - l ); // note: that's not a "one" it's an "L"
	}
	return 0;
}

char * opt_strupr (char * string)
{
	//	if ( __lc_handle[LC_CTYPE] == _CLOCALEHANDLE )
	//	{
	char *cp;       /* traverses string for C locale conversion */

	for ( cp = string ; *cp ; ++cp )
		if ( ('a' <= *cp) && (*cp <= 'z') )
			*cp -= 'a'-'A';

	return(string);
	//	}   /* C locale */
	//
	//	return _strupr(string); // call localized version which handles all sorts of stuff.
}

int opt_stricmp (const char * dst, const char * src)
{
	//	if ( __lc_handle[LC_CTYPE] == _CLOCALEHANDLE ) {
	int f, l;

    do {
        if ( ((f = (unsigned char)(*(dst++))) >= 'A') &&
                (f <= 'Z') )
            f -= 'A' - 'a';
        if ( ((l = (unsigned char)(*(src++))) >= 'A') &&
                (l <= 'Z') )
				l -= 'A' - 'a';
	} while ( f && (f == l) );

	//	} else { // localized version
	//		int f,l;
	//		do {
	//			f = tolower( (unsigned char)(*(dst++)) );
	//			l = tolower( (unsigned char)(*(src++)) );
	//		} while ( f && (f == l) );
	//	}

	return(f - l);
}

static size_t g_strdup_dbg_len; // for debugging
char *strdup_dbg(const char *s MEM_DBG_PARMS)
{
	size_t len;
	char *temp;
	if (!s) return NULL;
	PERFINFO_AUTO_START("strdup", 1);
		g_strdup_dbg_len = len = strlen(s)+1;
		temp = smalloc(len);
	PERFINFO_AUTO_STOP();
	strcpy_s(temp, len, s);
	return temp;
}

char *strndup_dbg(const char *s, size_t n MEM_DBG_PARMS)
{
	size_t len;
	char *temp;
	if (!s) return NULL;
	PERFINFO_AUTO_START("strndup", 1);
		len = strlen(s);
		if (n > len) n = len;
		temp = smalloc(n+1);
		strncpy_s(temp, n+1, s, n);
		temp[n] = 0;
	PERFINFO_AUTO_STOP();
	return temp;
}

static size_t g_strdup_special_dbg_len; // for debugging
char *strdup_special_dbg(const char *s, int special_heap MEM_DBG_PARMS)
{
	size_t len;
	char *temp;
	if (!s) return NULL;
	PERFINFO_AUTO_START("strdup_special", 1);
		g_strdup_special_dbg_len = len = strlen(s)+1;
		temp = malloc_timed_canfail(len, special_heap, false MEM_DBG_PARMS_CALL);
	PERFINFO_AUTO_STOP();
	strcpy_s(temp, len, s);
	return temp;
}

char *strndup_special_dbg(const char *s, size_t n, int special_heap MEM_DBG_PARMS)
{
	size_t len;
	char *temp;
	if (!s) return NULL;
	PERFINFO_AUTO_START("strndup_special", 1);
		len = strlen(s);
		if (n > len) n = len;
		temp = malloc_timed_canfail(n+1, special_heap, false MEM_DBG_PARMS_CALL);
		strncpy_s(temp, n+1, s, n);
		temp[n] = 0;
	PERFINFO_AUTO_STOP();
	return temp;
}

// Concatenates two strings, up to a max number, and returns the number of characters copied
int strncat_count(char *strDest, const char *strSource, int max)
{
	register char *c=strDest;
	register const char *s;
	int left = max;
	while (*c && left) {
		c++;
		left--;
	}
	for (s=strSource;*s && left; )
	{
		*c++=*s++;
		left--;
	}
	if (!left) {
		*--c=0;
		left++;
	}
	else { 
		*c = 0;  //added to make sure string is always terminated after leaving function 
	}
	
	return max - left;

}


/***  copied from CRT, removed threaded code
*long atol(char *nptr) - Convert string to long
*
*Purpose:
*       Converts ASCII string pointed to by nptr to binary.
*       Overflow is not detected.
*
*Entry:
*       nptr = ptr to string to convert
*
*Exit:
*       return long int value of the string
*
*Exceptions:
*       None - overflow is not detected.
*
*******************************************************************************/

long opt_atol(const char *nptr)
{
	int c;              /* current char */
	long total;         /* current total */
	int sign;           /* if '-', then negative, otherwise positive */

	while ( *nptr == ' ' ) // was isspace
		++nptr;

	c = (int)*nptr++;
	sign = c;           /* save sign indication */
	if (c == '-' || c == '+')
		c = (int)*nptr++;    /* skip sign */

	total = 0;

	while ( (c = _tchartodigit(c)) != -1 ) {
		total = 10 * total + c;     /* accumulate digit */
		c = *nptr++;    /* get next char */
	}

	if (sign == '-')
		return -total;
	else
		return total;   /* return result, negated if necessary */
}

// 64bit version of above
S64 opt_atol64(const char *nptr)
{
	int c;              /* current char */
	S64 total;         /* current total */
	int sign;           /* if '-', then negative, otherwise positive */

	while ( *nptr == ' ' ) // was isspace
		++nptr;

	c = (int)*nptr++;
	sign = c;           /* save sign indication */
	if (c == '-' || c == '+')
		c = (int)*nptr++;    /* skip sign */

	total = 0;

	while ( (c = _tchartodigit(c)) != -1 ) {
		total = 10 * total + c;     /* accumulate digit */
		c = *nptr++;    /* get next char */
	}

	if (sign == '-')
		return -total;
	else
		return total;   /* return result, negated if necessary */
}

// Unsigned version of above
U64 opt_atoul64(const char *nptr)
{
	int c = 0;              /* current char */
	U64 total;         /* current total */

	while ( *nptr == ' ' ) // was isspace
		++nptr;

	c = (int)*nptr++;

	total = 0;

	while ( (c = _tchartodigit(c)) != -1 ) {
		total = 10 * total + c;     /* accumulate digit */
		c = *nptr++;    /* get next char */
	}

	return total;
}

double opt_atof(const char *s) {
	register const char *c=s;
	double ret=0;
	int sign=1;
	if (*c=='-') { sign=-1;  c++; }
	while (*c) {
		if (*c=='.') {
			double mult=1.0;
			c++;
			while (*c) {
				mult*=0.1;
				if (*c>='0' && *c<='9') {
					ret+=mult*(*c-'0');
					c++;
				} else { // bad character
					if (*c=='e') {
						int exponent = atoi(++c);
						if (exponent) {
							return atof(s);
						}
						return ret*sign;
					}
					return atof(s);
				}
			}
			return ret*sign;
		}
		ret*=10;
		if (*c>='0' && *c<='9') {
			ret+=*c-'0';
			c++;
		} else {
			return atof(s);
		}
	}
	return ret*sign;
}


// Underscore insensitive stricmp
int striucmp(const char * src, const char * dst)
{
	int f, l;
	do {
		while (*src=='_') src++;
		while (*dst=='_') dst++;

		if ( ((f = (unsigned char)(*(src++))) >= 'A') &&
			(f <= 'Z') )
			f -= 'A'-'a';

		if ( ((l = (unsigned char)(*(dst++))) >= 'A') &&
			(l <= 'Z') )
			l -= 'A'-'a';

	} while ( f && (f == l) );

	return( f - l ); // note: that's not a "one" it's an "L"
}

char *stripUnderscores(const char *src) // Returns a non-threadsafe static buffer
{
	static char buffer[1024];
	char *c;
	char *out;
	strcpy(buffer, src);
	for (c=out=buffer; *c; c++) {
		if (*c!='_') {
			*out++ = *c;
		}
	}
	*out = 0;
	return buffer;
}

void stripUnderscoresInPlace(char *src)
{
	char *c;
	char *out;
	for (c=out=src; *c; c++) {
		if (*c!='_') {
			*out++ = *c;
		}
	}
	*out = 0;
}


void stripUnderscoresSafe(const char *src, char **dest) 
{
	const char *underscore, *last;
	last = src;

	while (last && *last && (underscore = strchr(last,'_')))
	{
		int iIndexOfUnderscore = underscore - last;
		if (iIndexOfUnderscore)
		{
			estrConcat(dest,last,iIndexOfUnderscore);
		}
		last += iIndexOfUnderscore + 1;
	}
	if (last && *last)
	{
		estrAppend2(dest,last);
	}
}



#define _SECURECRT_FILL_BUFFER_THRESHOLD ((size_t)16384)

#define _SECURECRT__FILL_STRING(_String, _Size, _Offset)                            \
    if ((_Size) != ((size_t)-1) && (_Size) != INT_MAX &&                            \
        ((size_t)(_Offset)) < (_Size))                                              \
    {                                                                               \
        memset((_String) + (_Offset),                                               \
            _SECURECRT_FILL_BUFFER_PATTERN,                                         \
            (_SECURECRT_FILL_BUFFER_THRESHOLD < ((size_t)((_Size) - (_Offset))) ?   \
                _SECURECRT_FILL_BUFFER_THRESHOLD :                                  \
                ((_Size) - (_Offset))) * sizeof(*(_String)));                       \
    }

//_CRTIMP void __cdecl _invalid_parameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t);
//#define _CALL_INVALID_PARAMETER_FUNC(funcname, expr) funcname(expr, __FUNCTIONW__, __FILEW__, __LINE__, 0)
#define _INVALID_PARAMETER(expr) assertmsg(!"CrypticCRT Error", #expr)
#define _VALIDATE_RETURN( expr, errorcode, retexpr )                           \
    {                                                                          \
        int _Expr_val=!!(expr);                                                \
        assertmsg( ( _Expr_val ), #expr );	                                   \
        if ( !( _Expr_val ) )                                                  \
        {                                                                      \
            errno = errorcode;                                                 \
            return ( retexpr );                                                \
        }                                                                      \
    }


static bool debug_did_fill;
static const char *g_dst, *g_src;
static size_t g_size;
// Code mostly copied from strcpy_s implementation
// Does a strcpy_s, but does not fill if we detect that this buffer has previously been filled
// Forces a fill once every 256 uses of the same buffer
// We can change the frequency of the forced fill by changing how many 
errno_t fast_strcpy_s(char *_Dst, size_t _SizeInBytes, const char *_Src)
{
	char *p;
	size_t available;

	_VALIDATE_RETURN(_Dst != NULL && _SizeInBytes > 0, EINVAL, EINVAL)
	if (_Src == NULL)
	{
		g_dst = _Dst;
		g_src = _Src;
		g_size = _SizeInBytes;
		*_Dst = 0;
		_SECURECRT__FILL_STRING(_Dst, _SizeInBytes, 1);
		_VALIDATE_RETURN(_Src != NULL, EINVAL, EINVAL)
	}

	p = _Dst;
	available = _SizeInBytes;
	while ((*p++ = *_Src++) != 0 && --available > 0)
	{
	}

	if (available == 0)
	{
		g_dst = _Dst;
		g_src = _Src;
		g_size = _SizeInBytes;
		Errorf("fast_strcpy_s - Buffer is too small, %d _SizeInBytes, left to copy of \"%.*s\", _Dst \"%.*s\" ", _SizeInBytes, _SizeInBytes, _Src, _SizeInBytes, _Dst );
		*_Dst = 0;
		_SECURECRT__FILL_STRING(_Dst, _SizeInBytes, 1);
		return ERANGE;
		//_VALIDATE_RETURN((L"Buffer is too small" && 0), ERANGE, ERANGE)
	}
	// Copy succeeded, fill string based on our rules
	available--;
	if (available >= 7) { // Enough room for us to store 4 bytes, rounded down to word boundary (need to round down to sizeof(*) boundary?)
		size_t filloffset = (_SizeInBytes - 4) & ~3;
		U32 *fillptr = (U32*)&_Dst[filloffset];
		U32 fillvalue = ((((U32)(intptr_t)_Dst) ^ (U32)_SizeInBytes) & 0x00FFFF00) | 0xFC000000; // 0xFC followed by the ptr xored with the size, and the low byte used for a count
		int needToFill = 0;
		if ((*fillptr & 0xFFFFFF00) == fillvalue) {
			// Already been filled, increase the count, and fill periodically (every 256 times)
			(*fillptr)++;
		} else {
			// Never been filled
			*fillptr = fillvalue;
			needToFill = 1;
		}
		if (needToFill) {
			debug_did_fill=true;
			_SECURECRT__FILL_STRING(_Dst, (_SizeInBytes-4) & ~3, _SizeInBytes - available);
		} else {
			debug_did_fill=false;
		}
	} else {
		_SECURECRT__FILL_STRING(_Dst, _SizeInBytes, _SizeInBytes - available);
		debug_did_fill=true;
	}
	return 0;
}

void fast_strcpy_s_test(void)
{
	char buf10k[10240];
	char buf32[32];
	char buf33[33];
	char buf34[34];
	char buf35[35];
	char buf36[36];
	int i;
	int count=0;
#define TEST_STRING "blarg!"
#define RESET(buf) memset(buf, 0xAA, MIN(sizeof(buf), 15));
#define TEST_IT_SIZED(buf, buf_size) { int ret = fast_strcpy_s(buf, buf_size, TEST_STRING); assert(ret==0); assert(stricmp(buf, TEST_STRING)==0); }
#define TEST_IT(buf) RESET(buf); TEST_IT_SIZED(buf, ARRAY_SIZE_CHECKED(buf))

	TEST_IT(buf10k); // Should fill
	assert(debug_did_fill);

	TEST_IT(buf10k); // Should not fill
	assert(!debug_did_fill);

	TEST_IT(buf32); // Should fill
	assert(debug_did_fill);

	TEST_IT(buf10k); // Should not fill
	assert(!debug_did_fill);

	TEST_IT(buf32); // Should not fill
	assert(!debug_did_fill);

	for (i=0; i<300; i++) {
		TEST_IT(buf10k); // One of these should fill
		if (debug_did_fill)
			count++;
	}
	assert(count == 1);

	TEST_IT(buf33); // Should fill
	assert(debug_did_fill);
	TEST_IT(buf33); // Should not fill
	assert(!debug_did_fill);
	TEST_IT(buf34); // Should fill
	assert(debug_did_fill);
	TEST_IT(buf34); // Should not fill
	assert(!debug_did_fill);
	TEST_IT(buf35); // Should fill
	assert(debug_did_fill);
	TEST_IT(buf35); // Should not fill
	assert(!debug_did_fill);
	TEST_IT(buf36); // Should fill
	assert(debug_did_fill);
	TEST_IT(buf36); // Should not fill
	assert(!debug_did_fill);

/*	ZeroStruct(&buf36);
	for (i=2; i<20; i++) {
		errno_t ret = fast_strcpy_s(buf36, i, TEST_STRING);
		//assert(stricmp(buf36, TEST_STRING)==0);
		printf("");
	}*/

	assertHeapValidateAll();

	printf("");
}
