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

#include "AutoGen/url_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static char *api_key = "820d1f783c071a055e6793219f398d78";
static char *api_secret = "d6bbbd747587f4a8f7749f7fcc979d14";

static char *auth_token = NULL;

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

static int fbapiCmpArgs(const UrlArgument **left, const UrlArgument **right)
{
	int ret = stricmp((*left)->arg, (*right)->arg);
	if(!ret)
		ret = stricmp((*left)->value, (*right)->value);
	return ret;
}

static void fbapiSign(UrlArgument ***args)
{
	char *sig_base = NULL, *sig = NULL;
	int i;
	U32 md5[4];
	bool have_session = false;

	// First add the cannonicalized arguments
	eaQSort(*args, fbapiCmpArgs);
	for(i=0; i<eaSize(args); i++)
	{
		if(stricmp((*args)[i]->arg, "session_key")==0)
			have_session = true;
		if((*args)[i]->method == HTTPMETHOD_MULTIPART)
			continue;
		estrConcatf(&sig_base, "%s=%s", (*args)[i]->arg, (*args)[i]->value);
	}

	// then add the secret
	if(have_session)
	{
		char *session_secret = NULL;
		gclStoredCredentialsGet("Facebook", NULL, NULL, &session_secret);
		estrConcatf(&sig_base, "%s", session_secret);
		estrDestroy(&session_secret);
	}
	else
		estrConcatf(&sig_base, "%s", api_secret);

	// Create the MD5 hash
	cryptMD5(sig_base, estrLength(&sig_base), md5);
	estrPrintf(&sig, "%08x%08x%08x%08x", endianSwapU32(md5[0]), endianSwapU32(md5[1]), endianSwapU32(md5[2]), endianSwapU32(md5[3]));

	// Add the signature to the arg list
	argsAdd(args, "sig", sig);

	// Profit!
	estrDestroy(&sig_base);
	estrDestroy(&sig);
}

typedef void(*fbapiCallback)(const char *response, int response_code, void *userdata);
typedef struct fbapiCallbackData
{
	char *request;
	fbapiCallback cb;
	void *userdata;
} fbapiCallbackData;

static void fbapiRequestConnectedCallback(HttpClient *client, fbapiCallbackData *pending)
{
	httpClientSendBytesRaw(client, pending->request, estrLength(&pending->request));
	estrDestroy(&pending->request);
}

static void fbapiRequestRunCallback(HttpClient *client, const char *data, int len, fbapiCallbackData *pending)
{
	if(pending->cb)
		pending->cb(data, httpClientGetResponseCode(client), pending->userdata);
	httpClientDestroy(&client);
	free(pending);
}

static void fbapiRequestTimeout(HttpClient *client, fbapiCallbackData *pending)
{
	httpClientDestroy(&client);
	free(pending);
}

static void fbapiRequest(UrlArgument ***args, const char *method, const char *session, fbapiCallback cb, void *userdata)
{
	char *request=NULL, *arg_string=NULL, *response=NULL, *multipart_boundary=NULL;
	int i, respCode=0;
	HttpClient *client;
	fbapiCallbackData *cbdata;
	UrlArgument **args_tmp=NULL;

	// Process arguments
	if(!args)
	{
		eaCreate(&args_tmp);
		args = &args_tmp;
	}
	argsAdd(args, "method", method);
	argsAdd(args, "api_key", api_key);
	argsAdd(args, "v", "1.0");
	if(session)
	{	
		argsAdd(args, "session_key", session);
		argsAdd(args, "call_id", STACK_SPRINTF("%"FORM_LL"u", timerCpuTicks64()));
	}
	fbapiSign(args);

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
	estrPrintf(&request, "POST /restserver.php HTTP/1.1\r\n");
	estrConcatf(&request, "Host: api.facebook.com\r\n");
	estrConcatf(&request, "User-Agent: gclFacebook\r\n");
	if(multipart_boundary)
		estrConcatf(&request, "Content-Type: multipart/form-data; boundary=%s\r\n", multipart_boundary);
	else
		estrConcatf(&request, "Content-Type: application/x-www-form-urlencoded\r\n");
	estrConcatf(&request, "Content-Length: %u\r\n", estrLength(&arg_string));
	estrConcatf(&request, "\r\n");
	estrConcat(&request, arg_string, estrLength(&arg_string));
	estrConcatf(&request, "\r\n");
	estrDestroy(&arg_string);

	// Create the callback data
	cbdata = calloc(1, sizeof(fbapiCallbackData));
	cbdata->cb = cb;
	cbdata->request = request;
	cbdata->userdata = userdata;

	// Send the HTTP request
	client = httpClientConnect("api.facebook.com", 80, fbapiRequestConnectedCallback, NULL, fbapiRequestRunCallback, fbapiRequestTimeout, commDefault(), false, HTTPCLIENT_DEFAULT_TIMEOUT);
	assert(client);
	httpClientSetUserData(client, cbdata);

	// Cleanup
	for(i=0; i<eaSize(args); i++)
	{
		SAFE_FREE((*args)[i]->arg);
		SAFE_FREE((*args)[i]->value);
		SAFE_FREE((*args)[i]->filename);
		SAFE_FREE((*args)[i]->content_type);
		free((*args)[i]);
	}
	eaDestroy(args);
	estrDestroy(&multipart_boundary);
}

static bool fbapiIsError(const char *response, U32 *error_code, char **error_msg)
{
	if(response && strstri(response, "<error_response ")!=NULL)
	{
		if(error_code)
		{
			char *tmp = strstri(response, "<error_code>");
			assert(tmp);
			tmp += 12; // 12 == strlen("<error_code>")
			*error_code = atoi(tmp);
		}
		if(error_msg)
		{
			*error_msg = xmlParse(response, "error_msg");
		}
		
		return true;
	}
	return false;
}

static void createTokenCB(const char *response, int response_code, void *userdata)
{
	if(response_code == 200)
	{
		char *tmp;
		response += 1;
		tmp = strchr(response, '"');
		assert(tmp);
		*tmp = '\0';
		SAFE_FREE(auth_token);
		auth_token = strdup(response);
		conPrintf("auth_token = %s\n", auth_token);
		openURL(STACK_SPRINTF("http://www.facebook.com/login.php?api_key=%s&v=1.0&auth_token=%s", api_key, auth_token));
	}
}

// Start an enrollment process with Facebook
AUTO_COMMAND ACMD_NAME(fb_create_token) ACMD_HIDE;
void gclFacebookCreateToken(void)
{
	UrlArgument **args=NULL;
	argsAdd(&args, "format", "JSON");
	fbapiRequest(&args, "auth.createToken", NULL, createTokenCB, NULL);
}

// Open a browser window to request the offline_access extended permission from Facebook
AUTO_COMMAND ACMD_NAME(fb_grant_offline_access) ACMD_HIDE;
void gclFacebookGrantOfflineAccess(void)
{
	openURL(STACK_SPRINTF("http://www.facebook.com/authorize.php?api_key=%s&v=1.0&ext_perm=offline_access", api_key));
}

// Open a browser window to request the publish_stream extended permission from Facebook
AUTO_COMMAND ACMD_NAME(fb_grant_publish_stream) ACMD_HIDE;
void gclFacebookGrantPublishStream(void)
{
	openURL(STACK_SPRINTF("http://www.facebook.com/authorize.php?api_key=%s&v=1.0&ext_perm=publish_stream", api_key));
}

static void getSessionCB(const char *response, int response_code, void *userdata)
{
	U32 error_code;
	char *error_msg=NULL;
	conPrintf("HTTP %d\n", response_code);
	if(fbapiIsError(response, &error_code, &error_msg))
	{
		conPrintf("ERROR %u: %s\n", error_code, error_msg);
		estrDestroy(&error_msg);
		printf("%s\n\n", response);
	}
	conPrintf("%s\n\n", response);

	if(!fbapiIsError(response, NULL, NULL))
	{
		char *uid = xmlParse(response, "uid");
		char *session_key = xmlParse(response, "session_key");
		char *secret = xmlParse(response, "secret");
		gclStoredCredentialsStore("Facebook", uid, session_key, secret);
		estrDestroy(&uid);
		estrDestroy(&session_key);
		estrDestroy(&secret);
	}
}

// Finish an enrollment process with Facebook
AUTO_COMMAND ACMD_NAME(fb_get_session) ACMD_HIDE;
void gclFacebookGetSession(void)
{
	if(auth_token)
	{
		UrlArgument **args=NULL;
		argsAdd(&args, "auth_token", auth_token);
		fbapiRequest(&args, "auth.getSession", NULL, getSessionCB, NULL);
	}
}

static void setStatusCB(const char *response, int response_code, char *msg)
{
	char *message=NULL;
	FormatGameMessageKey(&message, "Facebook.StatusUpdated", STRFMT_STRING("msg", NULL_TO_EMPTY(msg)), STRFMT_END);
	notify_NotifySend(NULL, kNotifyType_FacebookStatusUpdated, message, NULL, NULL);
	estrDestroy(&message);
	SAFE_FREE(msg);
}

// Send a status update to Facebook
AUTO_COMMAND ACMD_NAME(fb_set_status);
void gclFacebookSetStatus(const ACMD_SENTENCE msg)
{
	char *session_key = NULL;
	gclStoredCredentialsGet("Facebook", NULL, &session_key, NULL);
	if(session_key)
	{
		UrlArgument **args=NULL;
		argsAdd(&args, "status", msg);
		argsAdd(&args, "format", "JSON");
		fbapiRequest(&args, "status.set", session_key, setStatusCB, strdup(msg));
	}
	else
		conPrintf("No session key\n");
	estrDestroy(&session_key);
}

static void uploadPhotoCB(const char *response, int response_code, void *userdata)
{
	U32 error_code;
	char *error_msg=NULL;
	if(fbapiIsError(response, &error_code, &error_msg))
	{
		conPrintf("Error %u: %s\n", error_code, error_msg);
		estrDestroy(&error_msg);
	}
	else
	{
		char *message=NULL, *caption = xmlParse(response, "caption");
		FormatGameMessageKey(&message, "Facebook.ScreenshotUploaded", STRFMT_STRING("caption", NULL_TO_EMPTY(caption)), STRFMT_END);
		notify_NotifySend(NULL, kNotifyType_FacebookScreenshotUploaded, message, NULL, NULL);
		estrDestroy(&message);
		estrDestroy(&caption);
	}
}

static void screenshotCB(const char *filename, char *caption)
{
	char *session_key = NULL;
	gclStoredCredentialsGet("Facebook", NULL, &session_key, NULL);
	if(session_key)
	{
		UrlArgument **args=NULL;
		UrlArgument *photo = calloc(1, sizeof(UrlArgument));
		photo->value = fileAlloc(filename, &photo->length);
		photo->filename = strdup("tempss.jpg");
		photo->content_type = strdup("image/jpg");
		photo->method = HTTPMETHOD_MULTIPART;
		eaPush(&args, photo);
		if(caption)
			argsAdd(&args, "caption", caption);
		fbapiRequest(&args, "photos.upload", session_key, uploadPhotoCB, NULL);
	}
	else
		conPrintf("No session key\n");
	estrDestroy(&session_key);
	SAFE_FREE(caption);
}

// Upload a screenshot to Facebook
AUTO_COMMAND ACMD_NAME(fb_screenshot) ACMD_HIDE;
void gclFacebookUploadScreenshot(const ACMD_SENTENCE caption)
{
	char *session_key = NULL;
	gclStoredCredentialsGet("Facebook", NULL, &session_key, NULL);
	if(session_key)
	{
		gfxRequestScreenshot(NULL, false, screenshotCB, caption?strdup(caption):NULL);
	}
	else
		conPrintf("No session key\n");
	estrDestroy(&session_key);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(fb_screenshot);
void gclFacebookUploadScreenshotNoCaption(void)
{
	gclFacebookUploadScreenshot(NULL);
}

// Upload a screenshot with UI to Facebook
AUTO_COMMAND ACMD_NAME(fb_screenshot_ui) ACMD_HIDE;
void gclFacebookUploadScreenshotUI(const ACMD_SENTENCE caption)
{
	char *session_key = NULL;
	gclStoredCredentialsGet("Facebook", NULL, &session_key, NULL);
	if(session_key)
	{
		gfxRequestScreenshot(NULL, true, screenshotCB, caption?strdup(caption):NULL);
	}
	else
		conPrintf("No session key\n");
	estrDestroy(&session_key);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(fb_screenshot_ui);
void gclFacebookUploadScreenshotUINoCaption(void)
{
	gclFacebookUploadScreenshotUI(NULL);
}