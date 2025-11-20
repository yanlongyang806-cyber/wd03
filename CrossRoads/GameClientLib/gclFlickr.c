#include "HttpClient.h"
#include "crypt.h"
#include "url.h"
#include "net.h"
#include "rand.h"
#include "EString.h"
#include "earray.h"
#include "textparser.h"
#include "Prefs.h"
#include "NotifyCommon.h"
#include "cmdparse.h"
#include "endian.h"
#include "GfxConsole.h"
#include "GameStringFormat.h"
#include "gclStoredCredentials.h"
#include "file.h"
#include "GraphicsLib.h"
#include "GlobalTypes.h"

#include "AutoGen/url_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static char *api_key = "ddd8fb09b7c2c12f8469c46b246fe30c";
static char *api_secret = "f5d1d854791c9ed7";

static char *frob = NULL;

static void argsAdd(UrlArgument ***args, const char *arg, const char *value)
{
	UrlArgument *new_arg = calloc(1, sizeof(UrlArgument));
	new_arg->arg = strdup(arg);
	new_arg->value = strdup(value);
	eaPush(args, new_arg);
}

static char *xmlParse(const char *data, const char *tag)
{
	char *val=NULL, *start, *end;
	start = strstri(data, STACK_SPRINTF("<%s>", tag));
	if(!start)
		return NULL;
	start += strlen(tag) + 2;
	end = strstri(data, STACK_SPRINTF("</%s>", tag));
	if(!end)
		return NULL;
	estrConcat(&val, start, end-start);
	return val;
}

static char *xmlParseAttr(const char *data, const char *tag, const char *attr)
{
	char *val=NULL, *start, *end;
	start = strstri(data, STACK_SPRINTF("<%s ", tag));
	if(!start)
		return NULL;
	start += strlen(tag) + 2;
	start = strstri(start, STACK_SPRINTF("%s=\"", attr));
	if(!start)
		return NULL;
	start += strlen(attr) + 2;
	end = strchr(start, '"');
	if(!end)
		return NULL;
	estrConcat(&val, start, end-start);
	return val;
}

static int flickrCmpArgs(const UrlArgument **left, const UrlArgument **right)
{
	return stricmp((*left)->arg, (*right)->arg);
}

static void flickrSign(UrlArgument ***args)
{
	char *sig_base = NULL, *sig = NULL;
	int i;
	U32 md5[4];

	// First add the secret
	estrConcatf(&sig_base, "%s", api_secret);

	// then add the cannonicalized arguments
	eaQSort(*args, flickrCmpArgs);
	for(i=0; i<eaSize(args); i++)
	{
		if((*args)[i]->method == HTTPMETHOD_MULTIPART)
			continue;
		estrConcatf(&sig_base, "%s%s", (*args)[i]->arg, (*args)[i]->value );
	}

	// Create the MD5 hash
	cryptMD5(sig_base, estrLength(&sig_base), md5);
	estrPrintf(&sig, "%08x%08x%08x%08x", endianSwapU32(md5[0]), endianSwapU32(md5[1]), endianSwapU32(md5[2]), endianSwapU32(md5[3]));

	// Add the signature to the arg list
	argsAdd(args, "api_sig", sig);

	// Profit!
	estrDestroy(&sig_base);
	estrDestroy(&sig);
}

typedef void(*flickrCallback)(const char *response, int response_code, void *userdata);
typedef struct flickrCallbackData
{
	char *request;
	flickrCallback cb;
	void *userdata;
} flickrCallbackData;

static void flickrRequestConnectedCallback(HttpClient *client, flickrCallbackData *pending)
{
	httpClientSendBytesRaw(client, pending->request, estrLength(&pending->request));
	estrDestroy(&pending->request);
}

static void flickrRequestRunCallback(HttpClient *client, const char *data, int len, flickrCallbackData *pending)
{
	if(pending->cb)
		pending->cb(data, httpClientGetResponseCode(client), pending->userdata);
	httpClientDestroy(&client);
	free(pending);
}

static void flickrRequestTimeout(HttpClient *client, flickrCallbackData *pending)
{
	httpClientDestroy(&client);
	free(pending);
}


static void flickrRequest(UrlArgument ***args, const char *method, const char *auth_token, flickrCallback cb, void *userdata)
{
	char *request=NULL, *arg_string=NULL, *response=NULL;
	int i, respCode=0;
	HttpClient *client;
	flickrCallbackData *cbdata;
	UrlArgument **args_tmp=NULL;

	// Process arguments
	if(!args)
	{
		eaCreate(&args_tmp);
		args = &args_tmp;
	}
	argsAdd(args, "method", method);
	argsAdd(args, "api_key", api_key);
	if(auth_token)
	{	
		argsAdd(args, "auth_token", auth_token);
	}
	flickrSign(args);

	// Bake the remaining args
	for(i=0; i<eaSize(args); i++)
	{
		estrConcatf(&arg_string, "%s%s=", (i?"&":""), (*args)[i]->arg);
		urlEscape((*args)[i]->value, &arg_string, true, false);
	}

	// Create the HTTP request
	estrPrintf(&request, "GET /services/rest/");
	if(arg_string)
	{
		estrConcatf(&request, "?%s", arg_string);
		estrDestroy(&arg_string);
	}
	estrConcatf(&request, " HTTP/1.1\r\n");
	estrConcatf(&request, "Host: api.flickr.com\r\n");
	estrConcatf(&request, "User-Agent: gclFlickr\r\n");
	estrConcatf(&request, "\r\n");

	// Create the callback data
	cbdata = calloc(1, sizeof(flickrCallbackData));
	cbdata->cb = cb;
	cbdata->request = request;
	cbdata->userdata = userdata;

	// Send the HTTP request
	client = httpClientConnect("api.flickr.com", 80, flickrRequestConnectedCallback, NULL, flickrRequestRunCallback, flickrRequestTimeout, commDefault(), false, HTTPCLIENT_DEFAULT_TIMEOUT);
	assert(client);
	httpClientSetUserData(client, cbdata);

	// Cleanup
	for(i=0; i<eaSize(args); i++)
	{
		free((*args)[i]->arg);
		free((*args)[i]->value);
		free((*args)[i]);
	}
	eaDestroy(args);
}

static void flickrRequestUpload(UrlArgument ***args, const char *auth_token, flickrCallback cb, void *userdata)
{
	char *request=NULL, *arg_string=NULL, *response=NULL, *multipart_boundary=NULL;
	int i, respCode=0;
	HttpClient *client;
	flickrCallbackData *cbdata;
	UrlArgument **args_tmp=NULL;

	// Process arguments
	if(!args)
	{
		eaCreate(&args_tmp);
		args = &args_tmp;
	}
	argsAdd(args, "api_key", api_key);
	if(auth_token)
	{	
		argsAdd(args, "auth_token", auth_token);
	}
	flickrSign(args);

	// Bake the remaining args
	for(i=0; i<eaSize(args); i++)
	{
		if((*args)[i]->method == HTTPMETHOD_MULTIPART)
		{
			// Switch gears, encode this as a multipart/form-data message
			estrPrintf(&multipart_boundary, "--------------------------------%u%u%u", randomU32(), randomU32(), randomU32());
			break;
		}
		estrConcatf(&arg_string, "%s%s=", (i?"&":""), (*args)[i]->arg);
		urlEscape((*args)[i]->value, &arg_string, true, false);
	}
	if(multipart_boundary)
	{
		estrPrintf(&arg_string, "--%s", multipart_boundary);
		for(i=0; i<eaSize(args); i++)
		{
			estrConcatf(&arg_string, "\r\nContent-Disposition: form-data");
			if((*args)[i]->arg)
				estrConcatf(&arg_string, "; name=\"%s\"", (*args)[i]->arg);
			if((*args)[i]->filename)
				estrConcatf(&arg_string, "; filename=\"%s\"", (*args)[i]->filename);
			estrConcatf(&arg_string, "\r\n");
			if((*args)[i]->content_type)
				estrConcatf(&arg_string, "Content-Type: %s\r\n", (*args)[i]->content_type);
			estrConcatf(&arg_string, "\r\n");
			if((*args)[i]->length)
				estrConcat(&arg_string, (*args)[i]->value, (*args)[i]->length);
			else
				estrConcatf(&arg_string, "%s", (*args)[i]->value);
			estrConcatf(&arg_string, "\r\n--%s", multipart_boundary);
		}
		estrConcatf(&arg_string, "--");
	}

	// Create the HTTP request
	estrPrintf(&request, "POST /services/upload/ HTTP/1.1\r\n");
	estrConcatf(&request, "Host: api.flickr.com\r\n");
	estrConcatf(&request, "User-Agent: gclFlickr\r\n");
	if(multipart_boundary)
		estrConcatf(&request, "Content-Type: multipart/form-data; boundary=%s\r\n", multipart_boundary);
	else
		estrConcatf(&request, "Content-Type: application/x-www-form-urlencoded\r\n");
	estrConcatf(&request, "Content-Length: %u\r\n", estrLength(&arg_string));
	estrConcatf(&request, "\r\n");
	estrConcat(&request, arg_string, estrLength(&arg_string));
	estrConcatf(&request, "\r\n");

	// Create the callback data
	cbdata = calloc(1, sizeof(flickrCallbackData));
	cbdata->cb = cb;
	cbdata->request = request;
	cbdata->userdata = userdata;

	// Send the HTTP request
	client = httpClientConnect("api.flickr.com", 80, flickrRequestConnectedCallback, NULL, flickrRequestRunCallback, flickrRequestTimeout, commDefault(), false, HTTPCLIENT_DEFAULT_TIMEOUT);
	assert(client);
	httpClientSetUserData(client, cbdata);

	// Cleanup
	for(i=0; i<eaSize(args); i++)
	{
		free((*args)[i]->arg);
		free((*args)[i]->value);
		free((*args)[i]);
	}
	eaDestroy(args);
	estrDestroy(&multipart_boundary);
}

static bool flickrIsError(const char *response, U32 *error_code, char **error_msg)
{
	if(response && strstri(response, "<rsp stat=\"fail\">")!=NULL)
	{
		char *err = strstri(response, "<err ");
		assert(err);
		if(error_code)
		{
			char *tmp = strstri(err, "code=\"");
			assert(tmp);
			tmp += 6; // 6 == strlen("code=\"")
			*error_code = atoi(tmp);
		}
		if(error_msg)
		{
			char *tmp = strstri(err, "msg=\""), *tmp2;
			assert(tmp);
			tmp += 5; // 5 == strlen("msg=\"")
			tmp2 = strchr(tmp, '"');
			assert(tmp2);
			estrConcat(error_msg, tmp, tmp2-tmp);
		}

		return true;
	}
	return false;
}

static void getFrobCB(const char *response, int response_code, void *userdata)
{
	char *url=NULL, *sig_base=NULL;
	U32 md5[4];
	estrDestroy(&frob);
	frob = xmlParse(response, "frob");
	conPrintf("frob = %s\n", frob);
	estrPrintf(&sig_base, "%sapi_key%sfrob%spermswrite", api_secret, api_key, frob);
	cryptMD5(sig_base, estrLength(&sig_base), md5);
	estrPrintf(&url, "http://flickr.com/services/auth/?api_key=%s&perms=write&frob=%s&api_sig=", api_key, frob);
	estrConcatf(&url, "%08x%08x%08x%08x", endianSwapU32(md5[0]), endianSwapU32(md5[1]), endianSwapU32(md5[2]), endianSwapU32(md5[3]));
	openURL(url);
	estrDestroy(&url);
	estrDestroy(&sig_base);
}

AUTO_COMMAND ACMD_NAME(flickr_get_frob) ACMD_HIDE;
void gclFlickrGetFrob(void)
{
	flickrRequest(NULL, "flickr.auth.getFrob", NULL, getFrobCB, NULL);
}

static void getTokenCB(const char *response, int response_code, void *userdata)
{
	U32 error_code;
	char *error_msg=NULL;
	//conPrintf("HTTP %d\n", response_code);
	//if(flickrIsError(response, &error_code, &error_msg))
	//{
	//	conPrintf("Error %u: %s\n", error_code, error_msg);
	//	estrDestroy(&error_msg);
	//}
	//conPrintf("%s\n\n", response);

	if(flickrIsError(response, &error_code, &error_msg))
	{
		conPrintf("Error %u: %s\n", error_code, error_msg);
		estrDestroy(&error_msg);
	}
	else
	{
		char *user = xmlParseAttr(response, "user", "username");
		char *token = xmlParse(response, "token");
		assert(user && user[0]);
		gclStoredCredentialsStore("Flickr", user, "", token);
		estrDestroy(&user);
		estrDestroy(&token);
	}
}

AUTO_COMMAND ACMD_NAME(flickr_get_token) ACMD_HIDE;
void gclFlickrGetToken(void)
{
	if(frob)
	{
		UrlArgument **args=NULL;
		argsAdd(&args, "frob", frob);
		flickrRequest(&args, "flickr.auth.getToken", NULL, getTokenCB, NULL);
	}
	else
		conPrintf("No frob, please run /flickr_get_frob to start an enrollment process.\n");
}

static void testLoginCB(const char *response, int response_code, void *userdata)
{
	U32 error_code;
	char *error_msg=NULL;
	conPrintf("HTTP %d\n", response_code);
	if(flickrIsError(response, &error_code, &error_msg))
	{
		conPrintf("Error %u: %s\n", error_code, error_msg);
		estrDestroy(&error_msg);
	}
	conPrintf("%s\n\n", response);
}

AUTO_COMMAND ACMD_NAME(flickr_test_login) ACMD_HIDE;
void gclFlickrTestLogin(void)
{
	char *auth_token = NULL;
	gclStoredCredentialsGet("Flickr", NULL, NULL, &auth_token);
	if(auth_token)
	{
		flickrRequest(NULL, "flickr.test.login", auth_token, testLoginCB, NULL);
	}
	else
		conPrintf("No auth token\n");
	estrDestroy(&auth_token);
}

static void uploadPhotoCB(const char *response, int response_code, char *title)
{
	U32 error_code;
	char *error_msg=NULL;
	if(flickrIsError(response, &error_code, &error_msg))
	{
		conPrintf("Error %u: %s\n", error_code, error_msg);
		estrDestroy(&error_msg);
	}
	else
	{
		char *message=NULL;
		FormatGameMessageKey(&message, "Flickr.ScreenshotUploaded", STRFMT_STRING("title", NULL_TO_EMPTY(title)), STRFMT_END);
		notify_NotifySend(NULL, kNotifyType_FlickrScreenshotUploaded, message, NULL, NULL);
		estrDestroy(&message);
	}
	SAFE_FREE(title);
}

static void screenshotCB(const char *filename, char *title)
{
	char *auth_token = NULL;
	gclStoredCredentialsGet("Flickr", NULL, NULL, &auth_token);
	if(auth_token)
	{
		UrlArgument **args=NULL;
		UrlArgument *photo = calloc(1, sizeof(UrlArgument));
		photo->arg = strdup("photo");
		photo->value = fileAlloc(filename, &photo->length);
		photo->filename = strdup("tempss.jpg");
		photo->content_type = strdup("image/jpg");
		photo->method = HTTPMETHOD_MULTIPART;
		eaPush(&args, photo);
		if(!title)
			title = strdupf("%s screenshot", GetProductDisplayName(0));
		argsAdd(&args, "title", title);
		flickrRequestUpload(&args, auth_token, uploadPhotoCB, title);
	}
	else
	{
		SAFE_FREE(title);
		conPrintf("No auth token\n");
	}
	estrDestroy(&auth_token);
}

// Upload a screenshot to Flickr
AUTO_COMMAND ACMD_NAME(flickr_screenshot) ACMD_HIDE;
void gclFlickrUploadScreenshot(const ACMD_SENTENCE title)
{
	char *auth_token = NULL;
	gclStoredCredentialsGet("Flickr", NULL, NULL, &auth_token);
	if(auth_token)
	{
		gfxRequestScreenshot(NULL, false, screenshotCB, title?strdup(title):NULL);
	}
	else
		conPrintf("No auth token\n");
	estrDestroy(&auth_token);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(flickr_screenshot);
void gclFlickrUploadScreenshotNoCaption(void)
{
	gclFlickrUploadScreenshot(NULL);
}

// Upload a screenshot with UI to Flickr
AUTO_COMMAND ACMD_NAME(flickr_screenshot_ui) ACMD_HIDE;
void gclFlickrUploadScreenshotUI(const ACMD_SENTENCE title)
{
	char *auth_token = NULL;
	gclStoredCredentialsGet("Flickr", NULL, NULL, &auth_token);
	if(auth_token)
	{
		gfxRequestScreenshot(NULL, true, screenshotCB, title?strdup(title):NULL);
	}
	else
		conPrintf("No auth token\n");
	estrDestroy(&auth_token);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(flickr_screenshot_ui);
void gclFlickrUploadScreenshotUINoCaption(void)
{
	gclFlickrUploadScreenshotUI(NULL);
}