#include "url.h"
#include "url_h_ast.h"

#include "estring.h"
#include "mathutil.h"
#include "StringUtil.h"
#include "rand.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define MIMETYPE_DEFAULT MIMETYPE_FORM_ENCODED

char *urlEscape(const char *url, char **url_esc, bool use_plus_for_space, bool allow_slash)
{
	const char *u;
	static char *localstr = NULL;

	if(!url || !url[0])
	{
		estrConcatf(url_esc, "");
		return *url_esc;
	}

	if(!url_esc)
	{
		estrClear(&localstr);
		url_esc = &localstr;
	}

	for(u=url; *u; u++)
	{
		const char c = *u;
		if(use_plus_for_space && c == ' ')
			estrConcat(url_esc, "+", 1);
		else if(!(
				('a' <= c && c <= 'z') ||
				('A' <= c && c <= 'Z') ||
				('0' <= c && c <= '9') ||
				(c == '_') ||
				(c == '.') ||
				(c == '-') ||
				(c == '~') ||
				(c == '/' && allow_slash)
			))
		{
			estrConcatf(url_esc, "%%%02X", c);
		}
		else
			estrConcat(url_esc, u, 1);
	}
	return *url_esc;
}

char *urlUnescape(const char *url_esc,char *url,int url_len)
{
	char	*u,convert[3];
	const char *ue;
	int count = 1; // min size = 1 for the NULL terminator

	if (url_len == 0)
		return 0; // 0 length buffer
	if (!url_esc)
	{
		url[0] = 0;
		return 0;
	}
	for(ue=url_esc,u=url;*ue;ue++)
	{
		if (*ue=='+')
			*u++ = ' ';
		else if (*ue=='%')
		{
			if (!ue[1] || !ue[2])
				break;
			strncpy(convert,ue+1,2);
			convert[2] = 0;
			*u++ = strtol(convert,0,16);
			ue += 2;
		}
		else
			*u++ = *ue;
		count++;
		if (count > url_len)
		{
			url[0] = 0;
			return 0; // buffer too small
		}
	}
	*u = 0;
	return url;
}

char *urlFind(char *data,char *cmd)
{
	char	*s,*url=0;

	if (strnicmp(data,cmd,strlen(cmd))==0)
	{
		url = data+strlen(cmd)+1;
		s = strchr(url,'\r');
		if (s)
			*s = 0;
		s = strrchr(url,' ');
		if (s)
			*s = 0;
	}
	return url;
}

bool urlGetNextArg(char **url, char **argOutput, char **valueOutput)
{
	char *p = *url;
	char *tmp;
	char *arg;
	char *value;

	// If there are any question marks in the string, skip all the way past them.
	tmp = strchr(p, '?');
	if(tmp)
		p = tmp+1;

	arg = p;

	tmp = strchr(arg, '=');
	if(!tmp)
		return false;
	*tmp = 0;
	tmp++;
	value = tmp;

	tmp = strchr(value, '&');
	if(tmp)
	{
		*tmp = 0;
		*url = tmp+1;
	}

	*argOutput = arg;
	*valueOutput = value;
	return true;
}

int urlToArgs(char *url,char **args,char **values, int num_elements)
{
	char	*s;
	int		i,len;

	if (!url || !url[0])
		return 0;

	values[0] = 0;
	s = strchr(url,'?');
	if (!s++)
	{
		estrCopy2(&args[0], url);
		return 1;
	}
	s[-1] = 0;
	estrCopy2(&args[0], url);
	s[-1] = '?';
	
	for(i=1;*s;i++)
	{
		char temp;
		if (i >= num_elements)
			return i;


		len = (int)strcspn(s,"=");
		temp = s[len];
		s[len] = 0;
		estrCopy2(&args[i], s);
		s[len] = temp;

		s += len+1;
		len = (int)strcspn(s,"&");
		if (!s[len])
		{
			estrCopy2(&values[i], s);
			s += len;
		}
		else
		{
			temp = s[len];
			s[len] = 0;
			estrCopy2(&values[i], s);
			s[len] = temp;
			s += len+1;
		}
	}
	{
		int		count=i;

		for(i=0;i<count;i++)
		{
			len = (int)strlen(args[i]);
			s = alloca(len+1);
			urlUnescape(args[i],s,len+1);
			estrCopy2(&args[i], s);
			//strcpy_s(args[i],len+1,s);

			if (values[i])
			{
				len = (int)strlen(values[i]);
				s = alloca(len+1);
				urlUnescape(values[i],s,len+1);
				estrCopy2(&values[i], s);
				//strcpy_s(values[i],len+1,s);
			}
		}
	}
	return i;
}



void urlPopulateList(UrlArgumentList *list, const char *querystring, U32 method)
{
	int len = (int)strlen(querystring);
	char *tempBuffer = NULL;
	char *pBuffer = NULL;

	char *arg   = NULL;
	char *value = NULL;

	estrStackCreateSize(&tempBuffer, len+1);
	strcpy_s(tempBuffer, len+1, querystring);
	
	pBuffer = tempBuffer;
	
	while(urlGetNextArg(&pBuffer, &arg, &value))
	{
		UrlArgument *p = StructCreate(parse_UrlArgument);
		int iMaxLen = (int) max(strlen(arg), strlen(value));
		char *argvalueBuffer = malloc(iMaxLen+1);
		
		urlUnescape(arg, argvalueBuffer, iMaxLen+1);
		p->arg   = strdup(argvalueBuffer);

		urlUnescape(value, argvalueBuffer, iMaxLen+1);
		p->value = strdup(argvalueBuffer);

		free(argvalueBuffer);
		p->method = method;
		eaPush(&list->ppUrlArgList, p);
	}
	
	estrDestroy(&tempBuffer);
}

UrlArgumentList * urlToUrlArgumentList_Internal(const char *url, bool bNoBaseURL)
{
	UrlArgumentList *list = StructCreate(parse_UrlArgumentList);
	int len = (int)strlen(url);
	char *tempBuffer = NULL;
	char *pBuffer = NULL;

	char *arg   = NULL;
	char *value = NULL;

	char *tmp;

	if (bNoBaseURL)
	{
		list->pBaseURL = NULL;
	}
	else
	{
		// Grab the base URL
		tmp = strchr(url, '?');
		if(tmp)
		{
			estrConcat(&list->pBaseURL, url, (int)(tmp - url));
		}
		else
		{
			estrCopy2(&list->pBaseURL, url);
		}
	}

	urlPopulateList(list, url, HTTPMETHOD_GET);
	return list;
}

UrlArgument *urlAddValue(UrlArgumentList *list, const char *arg, const char *val, HttpMethod method)
{
	UrlArgument *p = StructCreate(parse_UrlArgument);
	if(arg)
		p->arg   = StructAllocString(arg);
	if(val)
		p->value = StructAllocString(val);
	p->method = method;
	eaPush(&list->ppUrlArgList, p);
	return p;
}

UrlArgument *urlAddValuef(UrlArgumentList *list, const char *arg, HttpMethod method, FORMAT_STR const char * fmt, ...)
{
	UrlArgument *ret;
	char *val = NULL;
	// Format value string.
	estrCreate(&val);
	estrGetVarArgs(&val, fmt);
	ret = urlAddValue(list, arg, val, method);
	estrDestroy(&val);
	return ret;
}

UrlArgument *urlAddValueExt(UrlArgumentList *list, const char *arg, const char *val, HttpMethod method)
{
	UrlArgument *p = StructCreate(parse_UrlArgument);
	if(arg)
		p->arg   = StructAllocString(arg);
	p->value_ext = val;
	p->method = method;
	eaPush(&list->ppUrlArgList, p);
	return p;
}

static void appendURLArgument(char **estr, const char *key, const char *val)
{
	char *hugeEscapedBuffer = NULL;

	if(val == NULL)
		return;

	estrStackCreate(&hugeEscapedBuffer);

	urlEscape(val, &hugeEscapedBuffer, true, false);
	estrConcatf(estr, "%s=%s&", key, hugeEscapedBuffer);

	estrDestroy(&hugeEscapedBuffer);
}

void urlAppendQueryStringWithOverrides(UrlArgumentList *list, char **estr, int keyvalpairs, ...)
{
	int i,j;
	char *key;
	char *val;
	char *paSawKey = alloca(sizeof(char) * (keyvalpairs+1));

	memset(paSawKey, 0, sizeof(char) * keyvalpairs);

	if(list && eaSize(&list->ppUrlArgList))
	{
		for(i=0;i<eaSize(&list->ppUrlArgList); i++)
		{
			bool bOverridden = false;

			VA_START(va, keyvalpairs);
			for(j=0; j < keyvalpairs; j++)
			{
				key = va_arg(va, char*);
				val = va_arg(va, char*);

				if(!stricmp(list->ppUrlArgList[i]->arg, key))
				{
					appendURLArgument(estr, key, val);
					paSawKey[j] = 1;
					bOverridden = true;
					break;
				}

			}
			VA_END();

			if(!bOverridden)
			{
				appendURLArgument(estr, list->ppUrlArgList[i]->arg, list->ppUrlArgList[i]->value);
			}
		}
	}

	VA_START(va, keyvalpairs);
	for(j=0; j < keyvalpairs; j++)
	{
		key = va_arg(va, char*);
		val = va_arg(va, char*);

		if(!paSawKey[j])
		{
			appendURLArgument(estr, key, val);
			break;
		}

	}
	VA_END();
}

const char * urlFindValue(UrlArgumentList *list, const char *arg)
{
	int i;

	if(!list)
		return NULL;

	for(i=0;i<eaSize(&list->ppUrlArgList); i++)
	{
		if(!stricmp(list->ppUrlArgList[i]->arg, arg))
		{
			return list->ppUrlArgList[i]->value;
		}
	}
	return NULL;
}

void urlRemoveValue(UrlArgumentList *list, const char *arg)
{
	int i;
	int size;
	if(!list) return;

	size = eaSize(&list->ppUrlArgList);

	for(i = size-1;i >= 0; i--)
	{
		if(!stricmp(list->ppUrlArgList[i]->arg, arg))
		{
			StructDestroy(parse_UrlArgument, list->ppUrlArgList[i]);
			eaRemove(&list->ppUrlArgList, i);
		}
	}
}

const char * urlFindSafeValue(UrlArgumentList *list, const char *arg)
{
	int i;

	if(!list)
		return "";

	for(i=0;i<eaSize(&list->ppUrlArgList); i++)
	{
		if(!stricmp(list->ppUrlArgList[i]->arg, arg))
		{
			return list->ppUrlArgList[i]->value ? list->ppUrlArgList[i]->value : "";
		}
	}
	return "";
}

//returns 1 on success, 0 on not found, -1 on failure
int urlFindBoundedInt(UrlArgumentList *list, char *pName, int *pOut, int iMin, int iMax)
{
	const char *pVal = urlFindValue(list, pName);
	int iTemp;

	if (!pVal)
	{
		return 0;
	}

	if (StringToInt(pVal, &iTemp))
	{
		
		if (iTemp < iMin || iTemp > iMax)
		{
			return -1;
		}
		

		*pOut = iTemp;
		return 1;
	}

	return -1;
}

//returns 1 on success, 0 on not found, -1 on failure
int urlFindBoundedUInt(UrlArgumentList *list, char *pName, U32 *pOut, U32 iMin, U32 iMax)
{
	const char *pVal = urlFindValue(list, pName);
	U32 iTemp;

	if (!pVal)
	{
		return 0;
	}

	if (StringToUint(pVal, &iTemp))
	{
		
		if (iTemp < iMin || iTemp > iMax)
		{
			return -1;
		}
		

		*pOut = iTemp;
		return 1;
	}

	return -1;
}

int urlFindVec3(UrlArgumentList *list, char *pName, Vec3 outVec)
{
	const char *pVal = urlFindValue(list, pName);
	char temp[64];
	char *pFirstComma;
	char *pSecondComma;
	Vec3 vTemp;

	if (!pVal)
	{
		return 0;
	}

	pFirstComma = strchr(pVal, ',');
	if (!pFirstComma)
	{
		return -1;
	}

	pSecondComma = strchr(pFirstComma+1, ',');
	if (!pSecondComma)
	{
		return -1;
	}

	if (pFirstComma - pVal > sizeof(temp)-1)
	{
		return -1;
	}

	if (pSecondComma - pFirstComma > sizeof(temp)-1)
	{
		return -1;
	}

	strncpy(temp, pVal, pFirstComma - pVal);

	if (!StringToFloat(temp, &vTemp[0]))
	{
		return -1;
	}

	strncpy(temp, pFirstComma + 1, pSecondComma - pFirstComma - 1);
	if (!StringToFloat(temp, &vTemp[1]))
	{
		return -1;
	}

	if (!StringToFloat(pSecondComma + 1, &vTemp[2]))
	{
		return -1;
	}

	copyVec3(vTemp, outVec);
	return 1;
}

void urlDestroy(UrlArgumentList **list)
{
	StructDestroySafe(parse_UrlArgumentList, list);
}

void urlArgDestroy(UrlArgument **arg)
{
	StructDestroySafe(parse_UrlArgument, arg);
}

int urlCmpArgs(const UrlArgument **left, const UrlArgument **right)
{
	int ret = stricmp((*left)->arg, (*right)->arg);
	if(!ret)
		ret = stricmp((*left)->value, (*right)->value);
	return ret;
}

void urlCreateHTTPRequest(char **estrRequest, const char *agent, const char *host, const char *path, UrlArgumentList *args)
{
	char *query_string=NULL, *post_data=NULL, *extra_headers=NULL, *multipart_boundary=NULL;
	int i, respCode=0;
	HttpMethod method = HTTPMETHOD_GET;

	// Bake the remaining args
	for(i=0; i<eaSize(&args->ppUrlArgList); i++)
	{
		UrlArgument *arg = args->ppUrlArgList[i];
		if(arg->method == HTTPMETHOD_HEADER)
		{
			estrConcatf(&extra_headers, "%s: %s\r\n", arg->arg, arg->value);
		}
		else if(multipart_boundary)
			continue; // If we already have a multipart boundary, we don't need to process non-header args in the first pass
		else if(arg->method == HTTPMETHOD_MULTIPART)
		{
			// Switch gears, encode this as a multipart/form-data message
			estrPrintf(&multipart_boundary, "--------------------------------%u%u%u", randomU32(), randomU32(), randomU32());
			method = HTTPMETHOD_POST;
		}
		else if(arg->method == HTTPMETHOD_GET)
		{
			if (query_string)
				estrConcatf(&query_string, "&");
			urlEscape(arg->arg, &query_string, true, false);
			estrConcatf(&query_string, "=");
			urlEscape(arg->value, &query_string, true, false);
		}
		else if(arg->method == HTTPMETHOD_POST)
		{
			if (post_data)
				estrConcatf(&post_data, "&");
			urlEscape(arg->arg, &post_data, true, false);
			estrConcatf(&post_data, "=");
			urlEscape(arg->value, &post_data, true, false);
			method = HTTPMETHOD_POST;
		}
		else if (arg->method == HTTPMETHOD_JSON)
		{
			// JSON is sent as POST data
			estrConcatf(&post_data, "%s%s", (post_data?"&":""), arg->arg);
			method = HTTPMETHOD_POST;
		}
	}
	// If we detected a multipart argument, run a second pass to create the multipart data
	if(multipart_boundary)
	{
		estrPrintf(&post_data, "--%s", multipart_boundary);
		for(i=0; i<eaSize(&args->ppUrlArgList); i++)
		{
			UrlArgument *arg = args->ppUrlArgList[i];
			estrConcatf(&post_data, "\r\nContent-Disposition: form-data");
			if(arg->arg)
				estrConcatf(&post_data, "; name=\"%s\"", arg->arg);
			if(arg->filename)
				estrConcatf(&post_data, "; filename=\"%s\"", arg->filename);
			estrConcatf(&post_data, "\r\n");
			if(arg->content_type)
				estrConcatf(&post_data, "Content-Type: %s\r\n", arg->content_type);
			estrConcatf(&post_data, "\r\n");
			if(arg->length)
				estrConcat(&post_data, arg->value_ext?arg->value_ext:arg->value, arg->length);
			else
				estrConcatf(&post_data, "%s", arg->value);
			estrConcatf(&post_data, "\r\n--%s", multipart_boundary);
		}
		estrConcatf(&post_data, "--");
	}

	// Create the HTTP request
	if(method == HTTPMETHOD_GET)
		estrPrintf(estrRequest, "GET");
	else if(method == HTTPMETHOD_POST)
		estrPrintf(estrRequest, "POST");
	estrConcatf(estrRequest, " %s", path);
	if(query_string)
	{
		estrConcatf(estrRequest, "?%s", query_string);
		estrDestroy(&query_string);
	}
	estrConcatf(estrRequest, " HTTP/1.1\r\n");
	estrConcatf(estrRequest, "Host: %s\r\n", host);  // // FIXME(VSarpeshkar) needs :port for non-default port
	estrConcatf(estrRequest, "User-Agent: %s\r\n", agent);
	if(post_data)
	{
		if(multipart_boundary)
			estrConcatf(estrRequest, "Content-Type: multipart/form-data; boundary=%s\r\n", multipart_boundary);
		else
			estrConcatf(estrRequest, "Content-Type: %s\r\n", args->pMimeType ? args->pMimeType : MIMETYPE_DEFAULT);
		estrConcatf(estrRequest, "Content-Length: %u\r\n", estrLength(&post_data));
	}
	if(extra_headers)
		estrConcatf(estrRequest, "%s", extra_headers);
	estrConcatf(estrRequest, "\r\n");
	if(post_data)
	{
		estrConcat(estrRequest, post_data, estrLength(&post_data));
//	Whoever put this line in here obviously never read the HTTP/1.1 spec
//	oh wait, it was noah - LOL
//		estrConcatf(estrRequest, "\r\n");
	}

	// Cleanup
	estrDestroy(&query_string);
	estrDestroy(&post_data);
	estrDestroy(&extra_headers);
	estrDestroy(&multipart_boundary);
}

#include "url_h_ast.c"
