#include "oauth.h"
#include "crypt.h"
#include "earray.h"
#include "estring.h"
#include "rand.h"
#include "url.h"
#include "utils.h"

void oauthSign(UrlArgumentList *args, HttpMethod method, const char *consumer_key, const char *consumer_secret, const char *token, const char *secret)
{
	char *sig_base = NULL, *key=NULL, sig[128], *sig_esc=NULL, *arg_string=NULL, *header=NULL;
	int i;
	UrlArgument *arg;

	// Add the standard arguments to the URL
	urlAddValue(args, "oauth_consumer_key", consumer_key, method);
	urlAddValue(args, "oauth_signature_method", "HMAC-SHA1", method);
	urlAddValue(args, "oauth_timestamp", STACK_SPRINTF("%"FORM_LL"u", (U64)time(NULL)), method);
	urlAddValue(args, "oauth_nonce", STACK_SPRINTF("%u", randomU32()), method);
	urlAddValue(args, "oauth_version", "1.0", method);
	if(token)
		urlAddValue(args, "oauth_token", token, method);

	// First add the request type
	if(method == HTTPMETHOD_GET)
		estrConcatf(&sig_base, "GET&");
	else if(method == HTTPMETHOD_POST)
		estrConcatf(&sig_base, "POST&");

	// then the URL
	urlEscape(args->pBaseURL, &sig_base, false, false);
	estrConcatf(&sig_base, "&");

	// then the cannonicalized arguments
	eaQSort(args->ppUrlArgList, urlCmpArgs);
	for(i=0; i<eaSize(&args->ppUrlArgList); i++)
	{
		arg = args->ppUrlArgList[i];
		if (stricmp(args->pMimeType, MIMETYPE_JSON) != 0 || strStartsWith(arg->arg, "oauth"))
		{
			estrConcatf(&arg_string, "%s%s=", (i?"&":""), arg->arg);
			urlEscape(arg->value, &arg_string, false, false);
		}
	}
	urlEscape(arg_string, &sig_base, false, false);

	// Concatenate the keys
	estrPrintf(&key, "%s&%s", consumer_secret, (token && token[0])?secret:"");

	// Create the HMAC-SHA1 signature
	cryptHMACSHA1Create(key, sig_base, SAFESTR(sig));
	urlEscape(sig, &sig_esc, false, false);

	// Add the signature to the arg list
	urlAddValue(args, "oauth_signature", sig_esc, method);

	// Collect the oauth parameters into an auth header
	estrPrintf(&header, "OAuth realm=\"%s\",\r\n", args->pBaseURL);
	for(i=eaSize(&args->ppUrlArgList)-1; i>=0; i--)
	{
		arg = args->ppUrlArgList[i];
		if(strStartsWith(arg->arg, "oauth"))
		{
			estrConcatf(&header, " %s=\"%s\"", arg->arg, arg->value);
			if(i!=0)
				estrConcatf(&header, ",\r\n");
			eaRemoveFast(&args->ppUrlArgList, i);
			urlArgDestroy(&arg);
		}
	}
	urlAddValue(args, "Authorization", header, HTTPMETHOD_HEADER);

	// Profit!
	estrDestroy(&sig_base);
	estrDestroy(&key);
	estrDestroy(&sig_esc);
	estrDestroy(&header);
}