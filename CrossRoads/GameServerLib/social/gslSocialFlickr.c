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
#include "file.h"

#include "AutoGen\GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#ifdef ENABLE_SOCIAL

#define FLICKR_API_URL "http://api.flickr.com/services/rest/"
#define FLICKR_UPLOAD_URL "http://api.flickr.com/services/upload/"

static char *api_key = "ddd8fb09b7c2c12f8469c46b246fe30c";
static char *api_secret = "f5d1d854791c9ed7";

enum
{
	kState_Start,
	kState_Authorizing,
};

static void flickrSign(UrlArgumentList *args, const char *method, HttpMethod http_method, const char *token)
{
	char *sig_base = NULL, md5[33];
	int i;
	UrlArgument *arg;

	// Add standard arguments
	if(method)
		urlAddValue(args, "method", method, http_method);
	urlAddValue(args, "api_key", api_key, http_method);
	if(token)
		urlAddValue(args, "auth_token", token, http_method);

	// First add the secret
	estrConcatf(&sig_base, "%s", api_secret);

	// then add the cannonicalized arguments
	eaQSort(args->ppUrlArgList, urlCmpArgs);
	for(i=0; i<eaSize(&args->ppUrlArgList); i++)
	{
		arg = args->ppUrlArgList[i];
		if(arg->method == HTTPMETHOD_MULTIPART)
			continue;
		estrConcatf(&sig_base, "%s%s", arg->arg, arg->value);
	}

	// Create the MD5 hash
	cryptMD5Hex(sig_base, estrLength(&sig_base), SAFESTR(md5));

	// Add the signature to the arg list
	urlAddValue(args, "api_sig", md5, http_method);

	// Profit!
	estrDestroy(&sig_base);
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

static void getFrobCB(Entity *ent, const char *response, int response_code, void *userdata)
{
	char *url=NULL, *sig_base=NULL, *frob, md5[33];
	frob = suXmlParse(response, "frob");
	estrPrintf(&sig_base, "%sapi_key%sfrob%spermswrite", api_secret, api_key, frob);
	cryptMD5Hex(sig_base, estrLength(&sig_base), SAFESTR(md5));
	estrPrintf(&url, "http://flickr.com/services/auth/?api_key=%s&perms=write&frob=%s&api_sig=%s", api_key, frob, md5);
	gslSocialUpdateEnrollment(ent, "Flickr", kState_Authorizing, frob);
	ClientCmd_gclSocialOpenWebpage(ent, url);
	estrDestroy(&url);
	estrDestroy(&sig_base);
	estrDestroy(&frob);
}

static void getTokenCB(Entity *ent, const char *response, int response_code, void *userdata)
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
		estrDestroy(&error_msg);
	}
	else
	{
		char *user = suXmlParseAttr(response, "user", "username");
		char *token = suXmlParse(response, "token");
		if(user && user[0])
			gslStoreCredentials(ent, "Flickr", user, "", token);
		estrDestroy(&user);
		estrDestroy(&token);
	}
}

static void flickrEnroll(Entity *ent, const char *service, U32 state, const char *userdata, const char *input)
{
	UrlArgumentList *args = urlToUrlArgumentList(FLICKR_API_URL);
	switch(state)
	{
	xcase kState_Start:
		flickrSign(args, "flickr.auth.getFrob", HTTPMETHOD_GET, NULL);
		suRequest(ent, args, getFrobCB, NULL, NULL);
	xcase kState_Authorizing:
		gslSocialUpdateEnrollment(ent, "Flickr", kState_Start, NULL);
		urlAddValue(args, "frob", userdata, HTTPMETHOD_GET);
		flickrSign(args, "flickr.auth.getToken", HTTPMETHOD_GET, NULL);
		suRequest(ent, args, getTokenCB, NULL, NULL);
	}
}

static void uploadPhotoCB(Entity *ent, const char *response, int response_code, char *title)
{
	U32 error_code;
	char *error_msg=NULL;
	if(flickrIsError(response, &error_code, &error_msg))
	{
		estrDestroy(&error_msg);
	}
	else
	{
		char *message=NULL;
		entFormatGameMessageKey(ent, &message, "Flickr.ScreenshotUploaded", STRFMT_STRING("title", NULL_TO_EMPTY(title)), STRFMT_END);
		notify_NotifySend(ent, kNotifyType_FlickrScreenshotUploaded, message, NULL, NULL);
		estrDestroy(&message);
	}
	SAFE_FREE(title);
}

static void flickrInvoke(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, ActivityDataScreenshot *data)
{
	UrlArgumentList *args = urlToUrlArgumentList(FLICKR_UPLOAD_URL);
	UrlArgument *photo = urlAddValueExt(args, "photo", data->data, HTTPMETHOD_MULTIPART);
	photo->length = data->len;
	photo->filename = StructAllocString("tempss.jpg");
	photo->content_type = StructAllocString("image/jpg");
	if(data->title)
		urlAddValue(args, "title", data->title, HTTPMETHOD_POST);
	flickrSign(args, NULL, HTTPMETHOD_POST, creds->secret);
	suRequest(ent, args, uploadPhotoCB, NULL, data->title?strdup(data->title):NULL);
}

#endif

AUTO_RUN;
void gslSocialFlickrRegister(void)
{
#ifdef ENABLE_SOCIAL
	if(isDevelopmentMode())
	{
		gslSocialRegisterEnrollment("Flickr", flickrEnroll);
		gslSocialRegister("Flickr", kActivityType_Screenshot, flickrInvoke);
	}
#endif
}
