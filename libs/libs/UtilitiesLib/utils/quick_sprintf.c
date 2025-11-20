#include <stdio.h>
#include <stdarg.h>
#include "utils.h"
#include "timing.h"
#include "winInclude.h"

#undef sprintf
#undef vsnprintf
#undef vsprintf
#undef _vsnprintf
#undef quick_sprintf

#if _PS3

__forceinline int core_vsnprintf(char *buf,size_t buf_size,size_t max_bytes,const char* fmt, va_list ap)
{
	int space = (int)min(buf_size, max_bytes);
    int n = vsnprintf(buf, space, fmt, ap);
    if(n+1 > space)
        return -1;
    return n;
}

#else


// Algorithm copied from xtoa.c in the standard library
// Returns chars written

__forceinline int writeIntPaddingAndFlip(	char**const pInOut,
											int*const lengthInOut,
											int width,
											int dotWidth,
											int buf_size,
											char padc,
											char* firstdig)
{
	char*	p = *pInOut;
	int		length = *lengthInOut;

	while ((length < width || length < dotWidth) && length < buf_size) {
		*p++ = length < dotWidth ? '0' : padc;
		length++;
	}

	if (length >= buf_size)
	{
		*pInOut = p;
		*lengthInOut = length;
		return 0;
	}

	*p-- = '\0';

	do {
		char temp = *p;
		*p = *firstdig;
		*firstdig = temp;
		--p;
		++firstdig;
	} while (firstdig < p);

	*lengthInOut = length;
	*pInOut = p;

	return 1;
}

__forceinline void pushNegativeSignForward(char* buf){
	if(buf[0] == '-'){
		while(buf[1] == ' '){
			buf[0] = ' ';
			buf[1] = '-';
			buf++;
		}
	}
}

__forceinline int writeS32(int signedval, char *buf, int buf_size, int width, int dotWidth, char padc)
{
	char *p;
	char *firstdig;
	int length = 0;
	U32 val;

	p = buf;

	if (buf_size < 2)
	{
		return buf_size;
	}

	if (signedval < 0) {
		*p++ = '-';
		length++;
		val = (-signedval);
	}
	else
	{
		val = signedval;
	}

	firstdig = p;

	do {
		*p++ = ((val % 10) + '0');
		val /= 10;
		length++;
	} while (val > 0 && length < buf_size);

	if(!writeIntPaddingAndFlip(&p, &length, width, dotWidth, buf_size, padc, firstdig)){
		buf[0] = '\0';
		return length;
	}

	pushNegativeSignForward(buf);

	return length;
}

__forceinline static int writeU32(U32 val, char *buf, int buf_size, int width, int dotWidth, char padc)
{
	char *p;
	char *firstdig;
	int length = 0;

	p = buf;

	if (buf_size < 2)
	{
		return buf_size;
	}

	firstdig = p;

	do {
		*p++ = ((val % 10) + '0');
		val /= 10;
		length++;
	} while (val > 0 && length < buf_size);

	if(!writeIntPaddingAndFlip(&p, &length, width, dotWidth, buf_size, padc, firstdig)){
		buf[0] = '\0';
		return length;
	}

	return length;
}

__forceinline static int writeS64(S64 signedval, char *buf, int buf_size, int width, int dotWidth, char padc)
{
	char *p;
	char *firstdig;
	int length = 0;
	U64 val;

	p = buf;

	if (buf_size < 2)
	{
		return buf_size;
	}

	if (signedval < 0) {
		*p++ = '-';
		length++;
		val = (-signedval);
	}
	else
	{
		val = signedval;
	}

	firstdig = p;

	do {
		*p++ = ((val % 10LL) + '0');
		val /= 10LL;
		length++;
	} while (val > 0 && length < buf_size);

	if(!writeIntPaddingAndFlip(&p, &length, width, dotWidth, buf_size, padc, firstdig)){
		buf[0] = '\0';
		return length;
	}

	pushNegativeSignForward(buf);

	return length;
}

__forceinline static int writeU64(U64 val, char *buf, int buf_size, int width, int dotWidth, char padc)
{
	char *p;
	char *firstdig;
	int length = 0;

	p = buf;

	if (buf_size < 2)
	{
		return buf_size;
	}

	firstdig = p;

	do {
		*p++ = ((val % 10LL) + '0');
		val /= 10LL;
		length++;
	} while (val > 0 && length < buf_size);

	if(!writeIntPaddingAndFlip(&p, &length, width, dotWidth, buf_size, padc, firstdig)){
		buf[0] = '\0';
		return length;
	}

	return length;
}

__forceinline static int writehex(size_t val, char *buf, int buf_size, int width, int dotWidth, char padc, int ucase)
{
	char *p;
	char *firstdig;
	int length = 0;
	static char *chars[] = {
		"0123456789abcdef",
		"0123456789ABCDEF"};

	p = buf;

	if (buf_size < 2)
	{
		return buf_size;
	}

	firstdig = p;

	do {
		*p++ = chars[ucase][val & 0xf];
		val >>= 4;
		length++;
	} while (val > 0 && length < buf_size);

	if(!writeIntPaddingAndFlip(&p, &length, width, dotWidth, buf_size, padc, firstdig)){
		buf[0] = '\0';
		return length;
	}

	return length;
}

__forceinline static int writehex64(U64 val, char *buf, int buf_size, int width, int dotWidth, char padc, int ucase)
{
	char *p;
	char *firstdig;
	int length = 0;
	static char *chars[] = {
		"0123456789abcdef",
		"0123456789ABCDEF"};

	p = buf;

	if (buf_size < 2)
	{
		return buf_size;
	}

	firstdig = p;

	do {
		*p++ = chars[ucase][val & 0xf];
		val >>= 4;
		length++;
	} while (val > 0 && length < buf_size);

	if(!writeIntPaddingAndFlip(&p, &length, width, dotWidth, buf_size, padc, firstdig)){
		buf[0] = '\0';
		return length;
	}

	return length;
}

__forceinline static int writeF64(F64 f,char *buf, int buf_size)
{
	int pos=0,dp,num;

	if (buf_size < 2)
	{
		return buf_size;
	}

	if (f<0)
	{
		buf[pos++]='-';
		f = -f;
	}
	dp=0;
	while (f>=10.0) 
	{
		f=f/10.0;
		dp++;
	}
	// output a max of 6 decimal digits, as per normal %f
	while (dp >= -6 && pos < buf_size)
	{
		num = f;
		f=f-num;
		buf[pos++]='0'+num;
		if (dp==0 && pos < buf_size)
		{
			buf[pos++]='.';
		}
		f=f*10.0;
		dp--;
	}
	return pos;
}


__forceinline static void safecopy(char *src,char **dst,int *space)
{
	int		len = (int)strlen(src);

	*space -= len;
	if (*space < 0)
		len += *space;
	memcpy(*dst,src,len);
	*dst += len;
}

__forceinline static int writeIP(U32 val, char *pBuf, int iSize)
{
	int iLenNeeded = 3;
	unsigned char p1, p2, p3, p4;
	
	char *pWriteHead;
	
	p1 = val&255;
	p2 = (val>>8)&255;
	p3 = (val>>16)&255;
	p4 = (val>>24)&255;


	iLenNeeded += p1 > 99 ? 3 : (p1 > 9 ? 2 : 1);
	iLenNeeded += p2 > 99 ? 3 : (p2 > 9 ? 2 : 1);
	iLenNeeded += p3 > 99 ? 3 : (p3 > 9 ? 2 : 1);
	iLenNeeded += p4 > 99 ? 3 : (p4 > 9 ? 2 : 1);

	if (iLenNeeded >= iSize)
	{
		*pBuf = 0;
		return iSize;
	}

	pWriteHead = pBuf + iLenNeeded - 1;

	do 
	{
		*pWriteHead = '0' + p4 % 10;
		p4 /= 10;
		pWriteHead--;
	}
	while (p4);

	*pWriteHead = '.';
	pWriteHead--;

	do
	{
		*pWriteHead = '0' + p3 % 10;
		p3 /= 10;
		pWriteHead--;
	}
	while (p3);

	*pWriteHead = '.';
	pWriteHead--;

	do
	{
		*pWriteHead = '0' + p2 % 10;
		p2 /= 10;
		pWriteHead--;
	}
	while (p2);

	*pWriteHead = '.';
	pWriteHead--;

	do
	{
		*pWriteHead = '0' + p1 % 10;
		p1 /= 10;
		pWriteHead--;
	}
	while (p1);


	return iLenNeeded;
}

__forceinline static int writeF64WithWidth(	F64 val,
											char** tailInOut,
											S32* spaceInOut,
											S32 width,
											S32 dotWidth,
											char padc)
{
	F64 abs_val = fabs(val);

	if(	width < 0 ||
		width > 1 ||
		dotWidth >= 10 ||
		!FINITE(val) ||
		_isnan(val) ||
		abs_val > 1000000.f)
	{
		return 0;
	}else{
		char* tail = *tailInOut;
		S32 space = *spaceInOut;
		U32 wholeU32 = (U32)abs_val;
		U32 fractionU32 = 0;
		F64 fraction = abs_val - wholeU32;
		S32 len;

		if(	val < 0 &&
			space)
		{
			*tail = '-';
			tail++;
			space--;
		}

		if(!dotWidth){
			if(fraction >= 0.5f){
				wholeU32++;
			}
		}
		else if(dotWidth > 0){
			U32 max_fractionU32;

			// Write the decimal places.
			switch(dotWidth){
				xcase 1:fraction *= 10.f;max_fractionU32 = 9;
				xcase 2:fraction *= 100.f;max_fractionU32 = 99;
				xcase 3:fraction *= 1000.f;max_fractionU32 = 999;
				xcase 4:fraction *= 10000.f;max_fractionU32 = 9999;
				xcase 5:fraction *= 100000.f;max_fractionU32 = 99999;
				xcase 6:fraction *= 1000000.f;max_fractionU32 = 999999;
				xcase 7:fraction *= 10000000.f;max_fractionU32 = 9999999;
				xcase 8:fraction *= 100000000.f;max_fractionU32 = 99999999;
				xcase 9:fraction *= 1000000000.f;max_fractionU32 = 999999999;
				xdefault:{
					assert(0);
				}
			}

			fractionU32 = (U32)fraction;
			if(fraction - fractionU32 >= 0.5f){
				if(fractionU32 == max_fractionU32){
					fractionU32 = 0;
					wholeU32++;
				}else{
					fractionU32++;
				}
			}
		}

		len = writeU32(wholeU32, tail, space, 0, 0, padc);
		tail += len; space -= len;

		if(dotWidth > 0){
			if (space) {
				*tail = '.';
				tail++;
				space--;
			}

			len = writeU32(fractionU32, tail, space, dotWidth, 0, '0');
			tail += len; space -= len;
		}

		*tailInOut = tail;
		*spaceInOut = space;

		return 1;
	}
}

__forceinline static int core_vsnprintf(char *buf,size_t buf_size,size_t max_bytes,const char* fmt, va_list ap_orig)
{
	va_list		ap = ap_orig;
	const		char *sc;
	char		*tail = buf;//, temp[64];
	int			space = (int)min(buf_size, max_bytes);
	
	// No, really, this timer should be L3.
	PERFINFO_AUTO_START_FUNC_L3();

	// NOTE: Not an error if space == 0, as long
	//       as what's written is %s with an empty string param.
	//       Just make sure to check space at each output point.
	
	for(sc=fmt;*sc && space >= 0;sc++)
	{
		if (sc[0] == '%')
		{
			int notHandled=0;
			int width=-1;
			int dotWidth=-1;
			const char *s = sc + 1;
			char padc=(*s=='0')?'0':' ';

			if(*s >= '0' && *s <= '9'){
				width = 0;

				while (*s >= '0' && *s <= '9') {
					width *= 10;
					width += *s - '0';
					s++;
				}
			}

			if(s[0] == '.'){
				s++;

				if(width < 0){
					width = 1;
				}

				dotWidth = 0;

				if(*s >= '0' && *s <= '9'){
					while (*s >= '0' && *s <= '9') {
						dotWidth *= 10;
						dotWidth += *s - '0';
						s++;
					}
				}
			}
			else if(width < 0){
				width = 0;
			}

			switch(*s)
			{
				xcase 's':
				{
					if (width || dotWidth >= 0) {
						notHandled = 1;
					} else {
						char *ss = va_arg(ap,char *);
						if (!ss)
							ss = "(null)";
						safecopy(ss,&tail,&space);
					}
				}
				xcase 'c':
				{
					if (width || dotWidth) {
						notHandled = 1;
					} else {
						char val = va_arg(ap,char);
						if (space) {
							*tail = val;
							tail++;
							space--;
						}
					}
				}
				xcase 'd':
				case 'i':
				{
					int val = va_arg(ap,int);
					int len = writeS32(val,tail,space, width, dotWidth, padc);
					tail+= len; space -= len;
				}
				xcase 'u':
				{
					U32 val = va_arg(ap,U32);
					int len = writeU32(val,tail,space, width, dotWidth, padc);
					tail+= len; space -= len;
				}
				xcase 'x':
				case 'X':
				{
					U32 val = va_arg(ap,U32);
					int len = writehex(val,tail,space, width, dotWidth, padc, *s=='X');
					tail+= len; space -= len;
				}
				xcase 'p':
				{
					size_t val = va_arg(ap,size_t);
					int len = writehex(val,tail,space, width, dotWidth, padc, false);
					tail+= len; space -= len;
				}
				xcase 'f':
				{
					F64 val = va_arg(ap,F64);

					if(width && dotWidth < 0){
						dotWidth = 6;
					}

					if (width || dotWidth >= 0){
						if(!writeF64WithWidth(val, &tail, &space, width, dotWidth, padc)){
							notHandled = 1;
						}
					}
					else if(FINITE(val) && val > -FLT_MAX && val < FLT_MAX){							
						int len = writeF64(val,tail,space);
						tail += len; space -= len;
					}else{
						notHandled = 1;
					}
				}
					
				xcase 'A':
				{
					U32 val = va_arg(ap, U32);
					int len = writeIP(val, tail, space);
					tail += len;
					space -= len;
				}

				xcase '%':
				{
					if (width || dotWidth) {
						notHandled = 1;
					} else {
						safecopy("%",&tail,&space);
					}
				}

				xcase 'I':
				{
					if (s[1]=='6' && s[2]=='4')
					{
						switch(s[3]){
							xcase 'd':
							acase 'i':{
								S64 val = va_arg(ap,S64);
								int len = writeS64(val, tail, space, width, dotWidth, padc);
								tail+= len; space -= len;
								s += 3;
							}
							xcase 'u':{
								U64 val = va_arg(ap,U64);
								int len = writeU64(val, tail, space, width, dotWidth, padc);
								tail+= len; space -= len;
								s += 3;
							}
							xcase 'x':
							acase 'X':{
								U64 val = va_arg(ap,U64);
								int len = writehex64(val, tail, space, width, dotWidth, padc, s[3]=='X');
								tail+= len; space -= len;
								s += 3;
							}
							xdefault:{
								notHandled = 1;
							}
						}
					}else{
						notHandled = 1;
					}
				}

				#if _PS3
				xcase 'l':
				{
					if (width || dotWidth) {
						notHandled = 1;
					}
					else if (s[1]=='l' && s[2]=='d') // "lld"
					{
						S64 val = va_arg(ap,S64);
						int len = writeS64(val, tail, space, width, padc);
						tail+= len; space -= len;
						s += 2;
					}
					else
					{
						notHandled = 1;
					}
				}
				#endif

				xdefault:
				{
					notHandled = 1;
				} // default
			} // switch(*s)
			if (notHandled) {
				int retval;
				PERFINFO_AUTO_START_L3("_vsnprintf_s", 1);
				if (max_bytes >= buf_size)
				{
					retval = _vsnprintf_s(buf,buf_size,_TRUNCATE,fmt,ap_orig);
				}
				else
				{					
					retval = _vsnprintf_s(buf,buf_size,max_bytes,fmt,ap_orig);
				}
				PERFINFO_AUTO_STOP_L3();
				PERFINFO_AUTO_STOP_L3();// FUNC.
				return retval;
			}

			sc = s;
		} // sc[0] == '%'
		else
		{
			if(space > 0)
			{
				*tail++ = *sc;
			}
			space--;
		}
	}
	PERFINFO_AUTO_STOP_L3();
	if (space > 0 ||
		buf_size > max_bytes)
	{
		*tail = 0;
		return tail - buf;
	}
	else
	{
		if (buf_size > 0)
			tail[-1] = 0;
		return -1;
	}
}

#endif

int quick_sprintf(char *buf,size_t buf_size,const char* fmt, ...)
{
	va_list		ap;
	int			ret;

	va_start(ap, fmt);
	ret = core_vsnprintf(buf,buf_size,_TRUNCATE,fmt,ap);
	va_end(ap);
	return ret;
}

static int iStackSprintfTlsAddressIndex = -1;
static int iStackSprintfTlsCounterIndex = -1;
AUTO_RUN_FIRST;
void stackSprintfInit(void)
{
	iStackSprintfTlsAddressIndex = TlsAlloc();
	iStackSprintfTlsCounterIndex = TlsAlloc();
	TlsSetValue(iStackSprintfTlsAddressIndex, NULL);
	TlsSetValue(iStackSprintfTlsCounterIndex, 0);
}

void *checkForStackSprintfOverflow(void *pBuf, const char *pFile, int iLine)
{
	void *pLastAddress;
	int iIncIndex;
	if (iStackSprintfTlsAddressIndex == -1)
	{
		return pBuf;
	}

	pLastAddress = TlsGetValue(iStackSprintfTlsAddressIndex);
	iIncIndex = (int)((intptr_t)TlsGetValue(iStackSprintfTlsCounterIndex));
	
	if (!pLastAddress)
	{
		TlsSetValue(iStackSprintfTlsAddressIndex, pBuf);
		TlsSetValue(iStackSprintfTlsCounterIndex, (void*)((intptr_t)1));
		return pBuf;
	}

	if (((U8*)pBuf) + 1024 <= ((U8*)pLastAddress))
	{
		assertmsgf(iIncIndex < 128, "Presumed STACK_SPRINTF overflow. Is it in a loop? %s(%d)",
			pFile, iLine);

		TlsSetValue(iStackSprintfTlsAddressIndex, pBuf);
		TlsSetValue(iStackSprintfTlsCounterIndex, (void*)((intptr_t)(iIncIndex + 1)));
	}

	return pBuf;
}

char *quick_sprintf_returnbuf(char *buffer, size_t buf_size, const char *format, ...)
{
	va_list		ap;

	va_start(ap, format);
	core_vsnprintf(buffer,buf_size,_TRUNCATE,format,ap);
	va_end(ap);
	return buffer;
}


int quick_snprintf(char *buf,size_t buf_size,size_t maxlen,const char* fmt, ...)
{
	va_list		ap;
	int			ret;

	va_start(ap, fmt);
	ret = core_vsnprintf(buf,buf_size,maxlen,fmt,ap);
	va_end(ap);
	return ret;
}

int quick_vsnprintf(char *buf,size_t buf_size,size_t maxlen,const char* fmt, va_list ap)
{
	return core_vsnprintf(buf,buf_size,maxlen,fmt,ap);
}

int quick_vsprintf(char *buf,size_t buf_size,const char* fmt, va_list ap)
{
	return core_vsnprintf(buf,buf_size,_TRUNCATE,fmt,ap);
}

int quick_vscprintf(const char *format, va_list args)
{
	int		token = 0;
	char	*s,*str;

	strdup_alloca(str,format);
	for(s=str;*s;s++)
	{
		if (token && *s == 'F')
			*s = 'f';
		if (*s == '%')
			token = !token;
		else
			token = 0;
	}
	return _vscprintf(str, args);
}

#include "strings_opt.h"
char	zzz_dir2[50] = "zombiedir";
char	*zzz_filep	= "zombiefile";

void strCombineTest(char *buffer,size_t buffer_size,char *dir2,char *fileinfo_name)
{
	//sprintf(buffer, "%s/%s", dir2, fileinfo.name);
	STR_COMBINE_BEGIN_S(buffer,buffer_size);
	STR_COMBINE_CAT(dir2);
	STR_COMBINE_CAT("/");
	STR_COMBINE_CAT(fileinfo_name);
	STR_COMBINE_END(buffer);
}

void quickSprintfPerfTest()
{
	int		i,timer,reps=1000000;
	char	buffer[1000];

	timer = timerAlloc();
	timerStart(timer);
	buffer[0] = 0;
	for(i=0;i<reps;i++)
	{
		quick_sprintf(SAFESTR(buffer), "%s/%s", zzz_dir2, zzz_filep);
	}
	printf("quick_sprintf: %f (%s)\n",timerElapsed(timer),buffer);

	timerStart(timer);
	buffer[0] = 0;
	for(i=0;i<reps;i++)
	{
		strCombineTest(SAFESTR(buffer), zzz_dir2, zzz_filep);
	}
	printf("strCombine: %f (%s)\n",timerElapsed(timer),buffer);

	timerStart(timer);
	buffer[0] = 0;
	for(i=0;i<reps;i++)
	{
		sprintf(buffer, "%s/%s", zzz_dir2, zzz_filep);
	}
	printf("sprintf: %f (%s)\n",timerElapsed(timer),buffer);
	
	timerFree(timer);
}
