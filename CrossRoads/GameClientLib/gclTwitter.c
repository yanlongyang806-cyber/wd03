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
#include "GameStringFormat.h"
#include "StoredCredentialsCommon.h"
#include "GfxConsole.h"
#include "gclStoredCredentials.h"
#include "TimedCallback.h"

#include "AutoGen/gclTwitter_c_ast.h"
#include "AutoGen/url_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static char *consumer_key = "6oZ4WdVj9GAAtl9CyHwsGA";
static char *consumer_secret = "c9nXpepmFvvh3hmVqHMu1PkakBxwj9hiB6brVCylZBY";

static char *tmp_token = NULL;
static char *tmp_secret = NULL;
//static char current_token[1024] = {0};
//static char current_token_secret[1024] = {0};
static bool need_pin = false;
static char *delayed_tweet = NULL;

void gclTwitterUpdate(const char *msg);

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

static void oauthAddStdArgs(UrlArgument ***args, const char *token)
{
	argsAdd(args, "oauth_consumer_key", consumer_key);
	argsAdd(args, "oauth_signature_method", "HMAC-SHA1");
	argsAdd(args, "oauth_timestamp", STACK_SPRINTF("%"FORM_LL"u", (U64)time(NULL)));
	argsAdd(args, "oauth_nonce", STACK_SPRINTF("%u", randomU32()));
	argsAdd(args, "oauth_version", "1.0");
	if(token)
		argsAdd(args, "oauth_token", token);
}

static int oauthCmpArgs(const UrlArgument **left, const UrlArgument **right)
{
	int ret = stricmp((*left)->arg, (*right)->arg);
	if(!ret)
		ret = stricmp((*left)->value, (*right)->value);
	return ret;
}

static void oauthSign(UrlArgument ***args, HttpMethod method, const char *url)
{
	char *sig_base = NULL, *key=NULL, sig[128], *sig_esc=NULL, *arg_string=NULL, *secret=NULL;
	int i;
	bool have_token = false;

	// First add the request type
	if(method == HTTPMETHOD_GET)
		estrConcatf(&sig_base, "GET&");
	else if(method == HTTPMETHOD_POST)
		estrConcatf(&sig_base, "POST&");

	// then the URL
	urlEscape(url, &sig_base, false, false);
	estrConcatf(&sig_base, "&");

	// then the cannonicalized arguments
	eaQSort(*args, oauthCmpArgs);
	for(i=0; i<eaSize(args); i++)
	{
		if(stricmp((*args)[i]->arg, "oauth_token")==0)
			have_token = true;
		estrConcatf(&arg_string, "%s%s=", (i?"&":""), (*args)[i]->arg);
		urlEscape((*args)[i]->value, &arg_string, false, false);
	}
	urlEscape(arg_string, &sig_base, false, false);

	// Concatenate the keys
	if(have_token)
	{
		if(tmp_secret)
			estrPrintf(&secret, "%s", tmp_secret);
		else
			gclStoredCredentialsGet("Twitter", NULL, NULL, &secret);
	}
	estrPrintf(&key, "%s&%s", consumer_secret, have_token?secret:"");

	// Create the HMAC-SHA1 signature
	cryptHMACSHA1Create(key, sig_base, SAFESTR(sig));
	urlEscape(sig, &sig_esc, false, false);

	// Add the signature to the arg list
	argsAdd(args, "oauth_signature", sig_esc);

	// Profit!
	estrDestroy(&sig_base);
	estrDestroy(&key);
	estrDestroy(&sig_esc);
	estrDestroy(&secret);
}

static char *oauthCreateHeader(UrlArgument ***args, const char *realm)
{
	char *header = NULL;
	int i;
	estrPrintf(&header, "Authorization: OAuth realm=\"%s\",\r\n", realm);
	for(i=eaSize(args)-1; i>=0; i--)
	{
		UrlArgument *arg = (*args)[i];
		if(strStartsWith(arg->arg, "oauth"))
		{
			estrConcatf(&header, " %s=\"%s\"", arg->arg, arg->value);
			if(i!=0)
				estrConcatf(&header, ",\r\n");
			eaRemove(args, i);
			free(arg->arg);
			free(arg->value);
			free(arg);
		}
	}
	return header;
}

typedef void(*oauthCallback)(const char *response, int response_code);
typedef struct oauthCallbackData
{
	char *request;
	oauthCallback cb;
} oauthCallbackData;

static void oauthRequestConnectedCallback(HttpClient *client, oauthCallbackData *pending)
{
	httpClientSendBytesRaw(client, pending->request, estrLength(&pending->request));
	estrDestroy(&pending->request);
}

static void oauthRequestRunCallback(HttpClient *client, const char *data, int len, oauthCallbackData *pending)
{
	if(pending->cb)
		pending->cb(data, httpClientGetResponseCode(client));
	httpClientDestroy(&client);
	free(pending);
}

static void oauthRequestTimeout(HttpClient *client, oauthCallbackData *pending)
{
	httpClientDestroy(&client);
	free(pending);
}

static void oauthRequest(UrlArgument ***args, HttpMethod method, const char *url, const char *token, oauthCallback cb)
{
	char *request=NULL, *arg_string=NULL, *oauth_header, *response=NULL;
	char *url_tmp, *host, *host_tmp, *path;
	int i, respCode=0;
	HttpClient *client;
	oauthCallbackData *cbdata;
	UrlArgument **args_tmp=NULL;

	// Parse the URL
	url_tmp = strdup(url);
	host_tmp = strchr(url_tmp, '/');
	host_tmp += 2;
	path = strchr(host_tmp, '/');
	*path = '\0';
	host = strdup(host_tmp);
	*path = '/';

	// Process arguments
	if(!args)
	{
		eaCreate(&args_tmp);
		args = &args_tmp;
	}
	oauthAddStdArgs(args, token);
	oauthSign(args, method, url);
	oauth_header = oauthCreateHeader(args, url);

	// Bake the remaining args
	for(i=0; i<eaSize(args); i++)
	{
		estrConcatf(&arg_string, "%s%s=", (i?"&":""), (*args)[i]->arg);
		urlEscape((*args)[i]->value, &arg_string, true, false);
	}

	// Create the HTTP request
	if(method == HTTPMETHOD_GET)
		estrPrintf(&request, "GET");
	else if(method == HTTPMETHOD_POST)
		estrPrintf(&request, "POST");
	estrConcatf(&request, " %s", path);
	if(arg_string)
	{
		estrConcatf(&request, "?%s", arg_string);
		estrDestroy(&arg_string);
	}
	estrConcatf(&request, " HTTP/1.1\r\n");
	estrConcatf(&request, "Host: %s\r\n", host);
	estrConcatf(&request, "User-Agent: gclTwitter\r\n");
	estrConcatf(&request, "%s\r\n", oauth_header);
	estrDestroy(&oauth_header);
	estrConcatf(&request, "\r\n");

	// Create the callback data
	cbdata = calloc(1, sizeof(oauthCallbackData));
	cbdata->cb = cb;
	cbdata->request = request;

	// Send the HTTP request
	client = httpClientConnect(host, 80, oauthRequestConnectedCallback, NULL, oauthRequestRunCallback, oauthRequestTimeout, commDefault(), false, HTTPCLIENT_DEFAULT_TIMEOUT);
	assert(client);
	httpClientSetUserData(client, cbdata);
	
	// Cleanup
	SAFE_FREE(host);
	SAFE_FREE(url_tmp);
	for(i=0; i<eaSize(args); i++)
	{
		free((*args)[i]->arg);
		free((*args)[i]->value);
		free((*args)[i]);
	}
	eaDestroy(args);
}

AUTO_EXPR_FUNC(UIGen) ACMD_CLIENTONLY ACMD_IFDEF(GAMECLIENT);
bool gclTwitterNeedPin(void)
{
	return need_pin;
}

AUTO_EXPR_FUNC(UIGen) ACMD_CLIENTONLY ACMD_IFDEF(GAMECLIENT);
void gclTwitterClosePin(void)
{
	need_pin = false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_CLIENTONLY ACMD_IFDEF(GAMECLIENT);
bool gclTwitterEnrolled(void)
{
	char *token=NULL, *secret=NULL;
	bool ret = false;
	gclStoredCredentialsGet("Twitter", NULL, &token, &secret);
	ret = token && token[0] && secret && secret[0];
	estrDestroy(&token);
	estrDestroy(&secret);
	return ret;
}

static void enrollCB(const char *response, int response_code)
{
	UrlArgumentList *response_args;

	// Parse the response
	if(response_code == 200)
	{
		response_args = urlToUrlArgumentList_Internal(response, true);
		SAFE_FREE(tmp_token);
		SAFE_FREE(tmp_secret);
		tmp_token = strdup(urlFindValue(response_args, "oauth_token"));
		tmp_secret = strdup(urlFindValue(response_args, "oauth_token_secret"));
		StructDestroy(parse_UrlArgumentList, response_args);
#if !PLATFORM_CONSOLE
		openURL(STACK_SPRINTF("http://twitter.com/oauth/authorize?oauth_token=%s&oauth_callback=oob", tmp_token));
		need_pin = true;
#endif
	}
}

// Start an OAuth enrollment process with Twitter
AUTO_COMMAND ACMD_NAME(twitter_enroll) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void gclTwitterEnroll(void)
{
	oauthRequest(NULL, HTTPMETHOD_GET, "http://twitter.com/oauth/request_token", NULL, enrollCB);
}

static void pinCB(const char *response, int response_code)
{
	UrlArgumentList *response_args;

	// Parse the response
	if(response_code == 200)
	{
		response_args = urlToUrlArgumentList_Internal(response, true);
		//strcpy(current_token, urlFindValue(response_args, "oauth_token"));
		//strcpy(current_token_secret, urlFindValue(response_args, "oauth_token_secret"));
		//StructDestroy(parse_UrlArgumentList, response_args);
		//ServerCmd_store_credentials("Twitter", current_token, current_token_secret);
		gclStoredCredentialsStore("Twitter", urlFindValue(response_args, "screen_name"), urlFindValue(response_args, "oauth_token"), urlFindValue(response_args, "oauth_token_secret"));
		if(delayed_tweet)
		{
			gclTwitterUpdate(delayed_tweet);
			SAFE_FREE(delayed_tweet);
		}
	}
	need_pin = false;
	SAFE_FREE(tmp_token);
	SAFE_FREE(tmp_secret);
}

// Complete an OAuth enrollment process with Twitter
AUTO_COMMAND ACMD_NAME(twitter_pin) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void gclTwitterPin(char *pin)
{
	if(tmp_token)
	{
		UrlArgument **args = NULL;
		argsAdd(&args, "oauth_verifier", pin);
		oauthRequest(&args, HTTPMETHOD_GET, "http://twitter.com/oauth/access_token", tmp_token, pinCB);
	}
	else
		conPrintf("You must run /twitter_enroll first to start the enrollment process");
}

static void updateCB(const char *response, int response_code)
{
	if(response_code == 200)
	{
		char *message = NULL, *tweet=NULL, *tmp, *tmp2;
		tmp = strstri(response, "<text>");
		if(tmp)
		{
			tmp += 6; // 6 == strlen("<text>")
			tmp2 = strchr(tmp, '<');
			assert(tmp2);
			estrConcat(&tweet, tmp, tmp2-tmp);
			estrReplaceOccurrences(&tweet, "&amp;", "&");
			estrReplaceOccurrences(&tweet, "&gt;", ">");
			estrReplaceOccurrences(&tweet, "&lt;", "<");
		}
		FormatGameMessageKey(&message, "Twitter.TweetSend", STRFMT_STRING("msg", NULL_TO_EMPTY(tweet)), STRFMT_END);
		notify_NotifySend(NULL, kNotifyType_TweetSent, message, NULL, NULL);
		estrDestroy(&message);
		estrDestroy(&tweet);
	}
}

// Post a status update to Twitter
AUTO_COMMAND ACMD_NAME(tweet) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void gclTwitterUpdate(const ACMD_SENTENCE msg)
{
	UrlArgument **args = NULL;
	char *token=NULL;

	// Check if we are setup
	gclStoredCredentialsGet("Twitter", NULL, &token, NULL);
	if(!token || !token[0])
	{
		// We aren't, store the last tweet and open the Twitter options gen
		SAFE_FREE(delayed_tweet);
		delayed_tweet = strdup(msg);
		globCmdParse("twitter_show");
		return;
	}

	// Send status update
	argsAdd(&args, "status", msg);
	oauthRequest(&args, HTTPMETHOD_POST, "http://twitter.com/statuses/update.xml", token, updateCB);
	estrDestroy(&token);
}

AUTO_STRUCT;
typedef struct TwitterStatus
{
	U64 id;
	char *text; AST(ESTRING)
	char *name; AST(ESTRING)
	char *screen_name; AST(ESTRING)
} TwitterStatus;
static TwitterStatus **g_twitter_statuses = NULL;
static U64 g_twitter_max_id = 0;

static void friendsTimelineCB(const char *response, int response_code)
{
	if(response_code == 200)
	{
		U64 new_max_id = g_twitter_max_id;
		TwitterStatus **new_statuses = NULL;
		const char *tmp = response;
		while(tmp = strstri(tmp, "<status>"))
		{
			TwitterStatus *status;
			U64 id;
			char *id_str;
			tmp += 8;
			id_str = xmlParse(tmp, "id");
			assert(id_str);
			id = _atoi64(id_str);
			assert(id);
			estrDestroy(&id_str);
			if(id <= g_twitter_max_id)
				continue;
			status = StructCreate(parse_TwitterStatus);
			status->id = id;
			status->text = xmlParse(tmp, "text");
			status->name = xmlParse(tmp, "name");
			status->screen_name = xmlParse(tmp, "screen_name");
			eaPush(&new_statuses, status);
			if(new_max_id < id)
				new_max_id = id;
		}
		FOR_EACH_IN_EARRAY(new_statuses, TwitterStatus, status)
			if(g_twitter_max_id != 0)
			{
				char *message = NULL;
				FormatGameMessageKey(&message, "Twitter.FriendUpdated", STRFMT_STRING("msg", NULL_TO_EMPTY(status->text)), STRFMT_STRING("name", NULL_TO_EMPTY(status->name)), STRFMT_END);
				notify_NotifySend(NULL, kNotifyType_TwitterFriendUpdated, message, NULL, NULL);
				estrDestroy(&message);
			}
		FOR_EACH_END
		g_twitter_max_id = new_max_id;
		eaInsertEArray(&g_twitter_statuses, &new_statuses, 0);
		eaDestroy(&new_statuses);
	}
	else
		conPrintf("Error %d: %s\n", response_code, response);
}

TimedCallback *g_twitter_timed_callback = NULL;

static void watchCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	char *token=NULL;
	gclStoredCredentialsGet("Twitter", NULL, &token, NULL);
	if(token && token[0])
		oauthRequest(NULL, HTTPMETHOD_GET, "http://twitter.com/statuses/friends_timeline.xml", token, friendsTimelineCB);
	estrDestroy(&token);
}

AUTO_COMMAND ACMD_NAME(twitter_watch) ACMD_ACCESSLEVEL(0);
void gclTwitterEnableTimedCallback(bool enabled)
{
	if(enabled && !g_twitter_timed_callback)
	{
		watchCB(NULL, 0, NULL);
		TimedCallback_Add(watchCB, NULL, 600);
	}
	else if(!enabled && g_twitter_timed_callback)
	{
		TimedCallback_Remove(g_twitter_timed_callback);
	}
}

AUTO_COMMAND ACMD_NAME(twitter_friends_timeline) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void gclTwitterFriendsTimeline(void)
{
	FOR_EACH_IN_EARRAY_FORWARDS(g_twitter_statuses, TwitterStatus, status)
		conPrintf("%"FORM_LL"u %s: %s\n", status->id, status->screen_name, status->text);
	FOR_EACH_END
}

#include "AutoGen/gclTwitter_c_ast.c"
