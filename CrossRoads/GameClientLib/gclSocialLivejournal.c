#include "HttpClient.h"
#include "crypt.h"
#include "url.h"
#include "net.h"
#include "EString.h"
#include "earray.h"
#include "textparser.h"
#include "NotifyCommon.h"
#include "cmdparse.h"
#include "GfxConsole.h"
#include "GameStringFormat.h"
#include "gclStoredCredentials.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static void argsAdd(UrlArgument ***args, const char *arg, const char *value)
{
	UrlArgument *new_arg = calloc(1, sizeof(UrlArgument));
	new_arg->arg = strdup(arg);
	new_arg->value = strdup(value);
	eaPush(args, new_arg);
}

static char *parseResponse(const char *data, const char *key)
{
	char *val=NULL, *start, *end;
	start = strstri(data, STACK_SPRINTF("%s\n", key));
	if(!start) return NULL;
	start += strlen(key) + 1;
	end = strchr(start, '\n');
	if(!end) return NULL;
	estrConcat(&val, start, end-start);
	return val;
}

typedef void(*ljCallback)(const char *response, int response_code, void *userdata);
typedef struct ljCallbackData
{
	char *request;
	ljCallback cb;
	void *userdata;
	UrlArgument **inner_args;
	char *inner_password;
} ljCallbackData;

static void ljRequestConnectedCallback(HttpClient *client, ljCallbackData *pending)
{
	httpClientSendBytesRaw(client, pending->request, estrLength(&pending->request));
	estrDestroy(&pending->request);
}

static void ljRequestRunCallback(HttpClient *client, const char *data, int len, ljCallbackData *pending)
{
	if(pending->cb)
		pending->cb(data, httpClientGetResponseCode(client), pending->userdata);
	httpClientDestroy(&client);
	free(pending);
}

static void ljRequestTimeout(HttpClient *client, ljCallbackData *pending)
{
	httpClientDestroy(&client);
	free(pending);
}

static void ljRequestInner(UrlArgument ***args, ljCallbackData *pending)
{
	char *request=NULL, *arg_string=NULL, *response=NULL;
	int i, respCode=0;
	HttpClient *client;
	UrlArgument **args_tmp=NULL;

	// Process arguments
	if(!args)
	{
		eaCreate(&args_tmp);
		args = &args_tmp;
	}


	// Bake the remaining args
	for(i=0; i<eaSize(args); i++)
	{
		estrConcatf(&arg_string, "%s%s=", (i?"&":""), (*args)[i]->arg);
		urlEscape((*args)[i]->value, &arg_string, true, false);
	}

	// Create the HTTP request
	estrPrintf(&request, "POST /interface/flat HTTP/1.1\r\n");
	estrConcatf(&request, "Host: www.livejournal.com\r\n");
	estrConcatf(&request, "User-Agent: gclSocialLivejournal\r\n");
	estrConcatf(&request, "Content-Type: application/x-www-form-urlencoded\r\n");
	estrConcatf(&request, "Content-Length: %u\r\n", estrLength(&arg_string));
	estrConcatf(&request, "\r\n");
	estrConcat(&request, arg_string, estrLength(&arg_string));
	estrConcatf(&request, "\r\n");
	estrDestroy(&arg_string);

	pending->request = request;

	// Send the HTTP request
	client = httpClientConnect("www.livejournal.com", 80, ljRequestConnectedCallback, NULL, ljRequestRunCallback, ljRequestTimeout, commDefault(), false, HTTPCLIENT_DEFAULT_TIMEOUT);
	assert(client);
	httpClientSetUserData(client, pending);

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
}

static void ljRequestRunInner(HttpClient *client, const char *data, int len, ljCallbackData *pending)
{
	char *challenge = parseResponse(data, "challenge"), *hashtmp, hash[33];
	argsAdd(&pending->inner_args, "auth_method", "challenge");
	argsAdd(&pending->inner_args, "auth_challenge", challenge);
	hashtmp = strdupf("%s%s", challenge, pending->inner_password);
	cryptMD5Hex(hashtmp, (int)strlen(hashtmp), SAFESTR(hash));
	argsAdd(&pending->inner_args, "auth_response", hash);
	ljRequestInner(&pending->inner_args, pending);

	httpClientDestroy(&client);
	estrDestroy(&challenge);
	free(hashtmp);
	SAFE_FREE(pending->inner_password);
}

static void ljRequest(UrlArgument ***args_inner, const char *method, const char *user, const char *password, ljCallback cb, void *userdata)
{
	char *request=NULL, *arg_string=NULL, *response=NULL;
	int i, respCode=0;
	HttpClient *client;
	ljCallbackData *cbdata;
	UrlArgument **args_tmp=NULL, **args=NULL;

	// Prepare the arguments for the inner request
	if(!args_inner)
	{
		eaCreate(&args_tmp);
		args_inner = &args_tmp;
	}
	argsAdd(args_inner, "mode", method);
	argsAdd(args_inner, "user", user);
	argsAdd(args_inner, "lineendings", "unix");


	// Bake the arguments for the auth request
	argsAdd(&args, "mode", "getchallenge");
	for(i=0; i<eaSize(&args); i++)
	{
		estrConcatf(&arg_string, "%s%s=", (i?"&":""), args[i]->arg);
		urlEscape(args[i]->value, &arg_string, true, false);
	}

	// Create the HTTP request
	estrPrintf(&request, "POST /interface/flat HTTP/1.1\r\n");
	estrConcatf(&request, "Host: www.livejournal.com\r\n");
	estrConcatf(&request, "User-Agent: gclSocialLivejournal\r\n");
	estrConcatf(&request, "Content-Type: application/x-www-form-urlencoded\r\n");
	estrConcatf(&request, "Content-Length: %u\r\n", estrLength(&arg_string));
	estrConcatf(&request, "\r\n");
	estrConcat(&request, arg_string, estrLength(&arg_string));
	estrConcatf(&request, "\r\n");
	estrDestroy(&arg_string);

	// Create the callback data
	cbdata = calloc(1, sizeof(ljCallbackData));
	cbdata->cb = cb;
	cbdata->request = request;
	cbdata->userdata = userdata;
	cbdata->inner_args = *args_inner;
	cbdata->inner_password = strdup(password);

	// Send the HTTP request
	client = httpClientConnect("www.livejournal.com", 80, ljRequestConnectedCallback, NULL, ljRequestRunInner, ljRequestTimeout, commDefault(), false, HTTPCLIENT_DEFAULT_TIMEOUT);
	assert(client);
	httpClientSetUserData(client, cbdata);

	// Cleanup
	for(i=0; i<eaSize(&args); i++)
	{
		SAFE_FREE(args[i]->arg);
		SAFE_FREE(args[i]->value);
		free(args[i]);
	}
	eaDestroy(&args);
}

AUTO_COMMAND ACMD_NAME(lj_enroll) ACMD_HIDE;
void gclSocialLivejournalEnroll(const char *username, const char *password)
{
	char hash[33];
	cryptMD5Hex(password, (int)strlen(password), SAFESTR(hash));
	gclStoredCredentialsStore("LiveJournal", username, "", hash);
}

static void postCB(const char *response, int response_code, char *title)
{
	char *message = NULL;
	char *success = parseResponse(response, "success");
	if(success && stricmp(success, "OK")==0)
	{
		FormatGameMessageKey(&message, "LiveJournal.PostSent", STRFMT_STRING("title", NULL_TO_EMPTY(title)), STRFMT_END);
		notify_NotifySend(NULL, kNotifyType_LiveJournalPostSent, message, NULL, NULL);
	}
	else
	{
		char *errmsg = parseResponse(response, "errmsg");
		FormatGameMessageKey(&message, "LiveJournal.PostError", STRFMT_STRING("errmsg", NULL_TO_EMPTY(errmsg)), STRFMT_END);
		notify_NotifySend(NULL, kNotifyType_LiveJournalError, message, NULL, NULL);
		estrDestroy(&errmsg);
	}
	estrDestroy(&success);
	estrDestroy(&message);
	SAFE_FREE(title);
}

AUTO_COMMAND ACMD_NAME(lj_post) ACMD_HIDE;
void gclSocialLivejournalPost(const char *title, const char *post)
{
	char *username = NULL, *password=NULL;
	gclStoredCredentialsGet("LiveJournal", &username, NULL, &password);
	if(username && password)
	{
		UrlArgument **args=NULL;
		time_t now;
		struct tm now_tm = {0};
		argsAdd(&args, "subject", title);
		argsAdd(&args, "event", post);
		now = time(NULL);
		localtime_s(&now_tm, &now);
		argsAdd(&args, "year", STACK_SPRINTF("%d", now_tm.tm_year+1900));
		argsAdd(&args, "mon", STACK_SPRINTF("%d", now_tm.tm_mon+1));
		argsAdd(&args, "day", STACK_SPRINTF("%d", now_tm.tm_mday));
		argsAdd(&args, "hour", STACK_SPRINTF("%d", now_tm.tm_hour));
		argsAdd(&args, "min", STACK_SPRINTF("%d", now_tm.tm_min));
		ljRequest(&args, "postevent", username, password, postCB, strdup(title));
	}
	else
		conPrintf("No username/password saved.\n");
	estrDestroy(&username);
	estrDestroy(&password);
}
