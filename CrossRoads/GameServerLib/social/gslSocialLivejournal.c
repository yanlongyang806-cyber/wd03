#include "gslSocial.h"
#include "gslSocialUtils.h"
#include "gslStoredCredentials.h"
#include "StoredCredentialsCommon.h"
#include "NotifyCommon.h"
#include "GameStringFormat.h"
#include "GlobalTypes.h"
#include "Entity.h"
#include "Player.h"
#include "Character.h"
#include "EntitySavedData.h"
#include "url.h"
#include "EString.h"
#include "earray.h"
#include "rand.h"
#include "crypt.h"
#include "utils.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#ifdef ENABLE_SOCIAL

#define LJ_URL "http://www.livejournal.com/interface/flat"

typedef struct ljPending
{
	UrlArgumentList *args;
	char *password;
} ljPending;

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

static void ljEnroll(Entity *ent, const char *service, U32 state, const char *userdata, const char *input)
{
	char *username, *password, hash[33];
	username = strdup(input);
	password = strchr(username, '&');
	if(!password) { free(username); return; }
	*password = '\0';
	password += 1;
	cryptMD5Hex(password, (int)strlen(password), SAFESTR(hash));
	gslStoreCredentials(ent, "Livejournal", username, "", hash);
	free(username);
}

static void postCB(Entity *ent, const char *response, int response_code, void *userdata)
{
	char *success = parseResponse(response, "success");
	if(!success || stricmp(success, "OK")!=0)
	{
		char *message = NULL, *errmsg = parseResponse(response, "errmsg");
		if(!errmsg)
			estrPrintf(&errmsg, "Server error");
		entFormatGameMessageKey(ent, &message, "Social.Livejournal.Error", STRFMT_STRING("errmsg", NULL_TO_EMPTY(errmsg)), STRFMT_END);
		notify_NotifySend(ent, kNotifyType_LiveJournalError, message, NULL, NULL);
		estrDestroy(&errmsg);
		estrDestroy(&message);
	}
	estrDestroy(&success);
}

static void getChallengeTimeout(Entity *ent, ljPending *pending)
{
	urlDestroy(&pending->args);
	free(pending->password);
	free(pending);
}

static void getChallengeCB(Entity *ent, const char *response, int response_code, ljPending *pending)
{
	char *challenge = parseResponse(response, "challenge"), *hashtmp, hash[33];
	urlAddValue(pending->args, "auth_method", "challenge", HTTPMETHOD_POST);
	urlAddValue(pending->args, "auth_challenge", challenge, HTTPMETHOD_POST);
	hashtmp = strdupf("%s%s", challenge, pending->password);
	cryptMD5Hex(hashtmp, (int)strlen(hashtmp), SAFESTR(hash));
	urlAddValue(pending->args, "auth_response", hash, HTTPMETHOD_POST);
	suRequest(ent, pending->args, postCB, NULL, NULL);
	free(pending->password);
	free(pending);
	free(hashtmp);
	estrDestroy(&challenge);
}

static void ljInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, ActivityDataBlog *blog)
{
	ljPending *pending = calloc(1, sizeof(ljPending));
	time_t now;
	struct tm now_tm = {0};
	UrlArgumentList *args = urlToUrlArgumentList(LJ_URL);
	urlAddValue(args, "mode", "getchallenge", HTTPMETHOD_POST);
	pending->args = urlToUrlArgumentList(LJ_URL);
	urlAddValue(pending->args, "mode", "postevent", HTTPMETHOD_POST);
	urlAddValue(pending->args, "user", creds->user, HTTPMETHOD_POST);
	urlAddValue(pending->args, "lineendings", "unix", HTTPMETHOD_POST);
	urlAddValue(pending->args, "subject", blog->title, HTTPMETHOD_POST);
	urlAddValue(pending->args, "event", blog->text, HTTPMETHOD_POST);
	now = time(NULL);
	localtime_s(&now_tm, &now);
	urlAddValue(pending->args, "year", STACK_SPRINTF("%d", now_tm.tm_year+1900), HTTPMETHOD_POST);
	urlAddValue(pending->args, "mon", STACK_SPRINTF("%d", now_tm.tm_mon+1), HTTPMETHOD_POST);
	urlAddValue(pending->args, "day", STACK_SPRINTF("%d", now_tm.tm_mday), HTTPMETHOD_POST);
	urlAddValue(pending->args, "hour", STACK_SPRINTF("%d", now_tm.tm_hour), HTTPMETHOD_POST);
	urlAddValue(pending->args, "min", STACK_SPRINTF("%d", now_tm.tm_min), HTTPMETHOD_POST);
	pending->password = strdup(creds->secret);
	suRequest(ent, args, getChallengeCB, getChallengeTimeout, pending);
}

#endif

AUTO_RUN;
void gslSocialLivejournalRegister(void)
{
#ifdef ENABLE_SOCIAL
	gslSocialRegisterEnrollment("Livejournal", ljEnroll);
	gslSocialRegister("Livejournal", kActivityType_Blog, ljInvoke);
#endif
}
