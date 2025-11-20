#include "WebInterface.h"

#include "AccountServer.h"
#include "AccountManagement.h"
#include "Account/billingAccount.h"
#include "accountCommon.h"
#include "AccountLog.h"
#include "AccountReporting.h"
#include "AccountSearch.h"
#include "AccountTransactionLog.h"
#include "AutoBill/UpdateActiveSubscriptions.h"
#include "crypt.h"
#include "earray.h"
#include "estring.h"
#include "file.h"
#include "GenericHttpServing.h"
#include "GlobalData.h"
#include "htmlForms.h"
#include "HttpClient.h"
#include "HttpLib.h"
#include "InternalSubs.h"
#include "KeyValues/KeyValues.h"
#include "logging.h"
#include "MachineID.h"
#include "Money.h"
#include "net/accountnet.h"
#include "net/net.h"
#include "objContainerIO.h"
#include "Product.h"
#include "ProductKey.h"
#include "qsortG.h"
#include "redact.h"
#include "rpn.h"
#include "sock.h"
#include "strings_opt.h"
#include "StringUtil.h"
#include "Subscription.h"
#include "SubscriptionHistory.h"
#include "sysutil.h"
#include "textparser.h"
#include "timing.h"
#include "UpdatePaymentMethod.h"
#include "utilitiesLib.h"
#include "utils.h"
#include "WikiToHTML.h"
#include "zutils.h"

#include "Autogen/HttpLib_h_ast.h"
#include "Autogen/HttpClient_h_ast.h"
#include "rpn_h_ast.h"

#define CONTENT_TYPE "text/html; charset=UTF-8"

#define FMT_DESCRIPTION "<div class=\"description\">%s</div>\n"
#define HTML_DESCRIPTION(out, in) estrConcatf( (out), FMT_DESCRIPTION, (in) )

#define FMT_HEADING "<div class=\"heading\">%s</div>\n"
#define HTML_HEADING(out, in) estrConcatf( (out), FMT_HEADING, (in) )

#define FMT_ERROR "<div class=\"error\">%s</div>\n"
#define HTML_ERROR(out, in) estrConcatf( (out), FMT_ERROR, (in) )

#define HTML_INSERT_TEXTINPUT(name, value) HTMLInsertInput("text", name, value)
#define HTML_INSERT_HIDDEN(name, value) HTMLInsertInput("hidden", name, value)
#define HTML_INSERT_SUBMIT(value) HTMLInsertInput("submit", "submit", value)
#define HTML_INSERT_FORM_START(action, method) HTMLInsertFormStart(action, method)
#define HTML_INSERT_FORM_END() "</form>"

// Simple ring buffer to make it possible to call the insert functions multiple times
// as parameters to a table creation function (or some other function)
#define MAX_TEMP_STRINGS 20
static char *HTMLGetTempString(void)
{
	static char *ppTempStrings[MAX_TEMP_STRINGS];
	static int iCurIndex = 0;
	char *pReturn = ppTempStrings[iCurIndex++];
	if (iCurIndex >= MAX_TEMP_STRINGS) iCurIndex = 0;
	return pReturn;
}

static const char *HTMLInsertInput(SA_PARAM_NN_STR const char *pType,
								   SA_PARAM_NN_STR const char *pName,
								   SA_PARAM_NN_STR const char *pValue)
{
	char *pInput = HTMLGetTempString();
	estrPrintf(&pInput, "<input type=\"%s\" name=\"%s\" value=\"%s\">", pType, pName, pValue);
	return pInput;
}

#define APPEND_URL_ARGS(ESTR,OFFSET,SORTORDER,DESC) \
{ \
	appendToURL(ESTR, "offset", "%d", (OFFSET)); \
	appendToURL(ESTR, "sort",   "%d", (SORTORDER)); \
	appendToURL(ESTR, "asc",    "%d", (DESC)?0:1); \
	appendToURLIfNotNull(ESTR, "name",     "%s", sd.name); \
	appendToURLIfNotNull(ESTR, "email",    "%s", sd.email); \
	appendToURLIfNotNull(ESTR, "firstn",   "%s", sd.firstName); \
	appendToURLIfNotNull(ESTR, "lastn",    "%s", sd.lastName); \
	appendToURLIfNotNull(ESTR, "prsub",    "%s", sd.productSub); \
	appendToURLIfNotNull(ESTR, "pkey",     "%s", sd.productKey); \
	appendToURL(ESTR, "namef",  "%d", sd.iNameFilter); \
    appendToURL(ESTR, "prsubf", "%d", sd.iProductFilter); \
	appendToURLIfNotNull(ESTR, "any", "%s", sd.pAny); \
}

#define APPEND_URL_ARGS_PSD(ESTR,OFFSET,SORTORDER,DESC) \
{ \
	appendToURL(ESTR, "offset", "%d", (OFFSET)); \
	appendToURL(ESTR, "sort",   "%d", (SORTORDER)); \
	appendToURL(ESTR, "asc",    "%d", (DESC)?0:1); \
	appendToURLIfNotNull(ESTR, "name",     "%s", psd->name); \
	appendToURLIfNotNull(ESTR, "email",    "%s", psd->email); \
	appendToURLIfNotNull(ESTR, "firstn",   "%s", psd->firstName); \
	appendToURLIfNotNull(ESTR, "lastn",    "%s", psd->lastName); \
	appendToURLIfNotNull(ESTR, "prsub",    "%s", psd->productSub); \
	appendToURLIfNotNull(ESTR, "pkey",     "%s", psd->productKey); \
	appendToURL(ESTR, "namef",  "%d", psd->iNameFilter); \
    appendToURL(ESTR, "prsubf", "%d", psd->iProductFilter); \
	appendToURLIfNotNull(ESTR, "any", "%s", psd->pAny); \
}

extern int giServerMonitorPort;
extern int giWebInterfacePort;
extern char gAccountWebDirPath[MAX_PATH];

static int giCurrentAccessLevel = -1;

static void wiRedirectf(NetLink *pLink, const char *pURLFormat, ...)
{
	char *pFullURL = NULL;

	va_list ap;

	va_start(ap, pURLFormat);
		estrConcatfv(&pFullURL, pURLFormat, ap);
	va_end(ap);

	if (pFullURL[0] == '/' && estrLength(&pFullURL) > 1)
	{
		httpRedirect(pLink, pFullURL + 1);
	}
	else
	{
		httpRedirect(pLink, pFullURL);
	}

	estrDestroy(&pFullURL);
}

void wiFormAppendStart(char **estr, const char *pAction, const char *pMethod, const char *pName, const char *pExtraAttrs)
{
	if (pAction && pAction[0] == '/') pAction += 1; // Start after

	estrConcatf(estr, "<form name=\"%s\" %s action=\"%s\" method=%s>\n", pName, (pExtraAttrs)?pExtraAttrs:"", pAction, pMethod);
}

// Determine if the current web session has the specified permissions
static bool wiAccess(U32 uPermissions)
{
	AccountServerAccessLevel eLevel = ASGetAccessLevel(giCurrentAccessLevel);

	if (eLevel == ASAL_Invalid) return false;

	return ASHasPermissions(eLevel, uPermissions);
}

// Get the string representation of the access level
static const char *wiGetAccessLevelString(void)
{
	AccountServerAccessLevel eLevel = ASGetAccessLevel(giCurrentAccessLevel);

	return StaticDefineIntRevLookupNonNull(AccountServerAccessLevelEnum, eLevel);
}

static void setCurrentAccessLevel(int level)
{
	giCurrentAccessLevel = level;
}

SA_RET_NN_STR static const char *getBackgroundColor(void)
{
	if (!isProductionMode()) return "373";

	switch (giCurrentAccessLevel)
	{
	default:
	case 0: return "DDF";
	case 1: return "CCD";
	case 3: return "AAC";
	case 4: return "88A";
	case 5: return "668";
	case 6: return "446";
	case 7: return "224";
	case 8: return "002";
	case 9: return "000";
	}
}

static int httpDisconnect(NetLink* link, HttpClientStateDefault *pClientState);

bool initWebInterface(void)
{
	NetListen *listen_socket;
	int tries = 0;
	
	for(;;)
	{
		setHttpDefaultPostHandler(httpLegacyHandlePost);
		setHttpDefaultGetHandler(httpLegacyHandleGet);

		listen_socket = commListen(commDefault(), LINKTYPE_HTTP_SERVER, LINK_HTTP, giWebInterfacePort,
			httpDefaultMsgHandler, httpDefaultConnectHandler, httpDisconnect, sizeof(HttpClientStateDefault));
		if (listen_socket)
			break;
		Sleep(1);
		if(0==(tries++%5000))
			printf("failed to listen on port %i: %s",giWebInterfacePort,lastWinErr());
	}

	httpEnableAuthentication(giWebInterfacePort, ACCOUNT_SERVER_INTERNAL_NAME, DEFAULT_HTTP_CATEGORY_FILTER);

	printf("Listening for web connections on port %d.\n", giWebInterfacePort);
	return true;
}

void shutdownWebInterface(void)
{
	cleanupLinkStates();
}

// ------------------------------------------------------
// HTTP Send functions

// Get the contents of a file as an estring
SA_RET_OP_STR static char * fileGetContentsTemp(SA_PARAM_NN_STR const char *pFileName)
{
	FILE *file = fileOpen(pFileName, "rb");
	static char *out = NULL;
	char buff[255];

	estrClear(&out);

	if (file)
	{
		memset(buff, 0, ARRAY_SIZE(buff));

		while (fread(buff, sizeof(char), ARRAY_SIZE(buff) - 1, file) > 0)
		{
			estrConcatf(&out, "%s", buff);
			memset(buff, 0, ARRAY_SIZE(buff));
		}

		fclose(file);
	}
	else
	{
		AssertOrAlert("WEB_INTERFACE_FILE_FAIL", "Could not open '%s' for reading.", pFileName);
	}

	return out;
}

// Append a file's contents to an estring
static void estrAppendFile(SA_PRE_NN_NN_STR char **estr, SA_PARAM_NN_STR const char *pFileName)
{
	estrConcatf(estr, "%s", fileGetContentsTemp(pFileName));
}

static const char *webFile(SA_PARAM_NN_STR const char *pFile)
{
	static char *pReturn = NULL;
	estrPrintf(&pReturn, "%s%s", gAccountWebDirPath, pFile);
	return pReturn;
}

void httpSendWrappedString(NetLink *link, const char *pString, CookieList *pList)
{
	static char *pHTML = NULL;
	const char *pBackgroundColor = getBackgroundColor();
	static char *pVersion = NULL;

	// Build the HTML string
	estrClear(&pHTML);
	estrAppendFile(&pHTML, webFile("template.html"));

	estrStackCreate(&pVersion);
	estrPrintf(&pVersion, "%s (%s)", ACCOUNT_SERVER_VERSION, GetUsefulVersionString());

	// Replace template stuff
	estrReplaceOccurrences(&pHTML, "<!-- INSTANCE -->", GetShardNameFromShardInfoString());
	estrReplaceOccurrences(&pHTML, "<!-- VERSION -->", pVersion);
	estrReplaceOccurrences(&pHTML, "<!-- CONTENT -->", pString);
	estrReplaceOccurrences(&pHTML, "<!-- BACKGROUND_COLOR -->", pBackgroundColor);
	estrReplaceOccurrences(&pHTML, "<!-- BACKGROUND_IMAGE -->", isProductionMode() ? "/prod.gif" : "/notprod.gif");
	estrReplaceOccurrences(&pHTML, "<!-- ACCESS_LEVEL -->", wiGetAccessLevelString());
	estrDestroy(&pVersion);

	// Send the file
	httpSendBasicHeader(link, estrLength(&pHTML), CONTENT_TYPE);
	httpSendBytesRaw(link, pHTML, estrLength(&pHTML));
	httpSendComplete(link);
}

// Determine if a file is a safe name (no slashes)
static bool httpIsSafeFile(SA_PARAM_NN_STR const char *pFileName)
{
	unsigned int index;
	int periods = 0;

	for (index = 0; index < strlen(pFileName); index++)
	{
		if (tolower(pFileName[index]) < 'a' && tolower(pFileName[index]) > 'z' && pFileName[index] != '.')
		{
			return false;
		}

		if (pFileName[index] == '.' && index && pFileName[index - 1] == '.')
		{
			return false;
		}
	}

	return true;
}

// Get a file extension
SA_RET_OP_STR static const char *httpGetExtensionPortion(SA_PARAM_NN_STR const char *pFileName)
{
	int index = 0;

	if (!*pFileName) return NULL;

	for (index = (int)strlen(pFileName) - 1; index >= 0; index--)
	{
		if (pFileName[index] == '.') return pFileName + index + 1;
	}
	
	return NULL;
}

static struct MediaType {
	const char *pExtension;
	const char *pMediaType;
} gMediaTypeMap[] = {
	{"html", "text/html"},
	{"gif", "image/gif"},
	{"jpeg", "image/jpeg"},
	{"jpg", "image/jpeg"},
	{"png", "image/png"},
	{"css", "text/css"},
	{"js", "application/javascript"},
	{"txt", "text/plain"},
	{"xml", "text/xml"},
	{0, 0}
};

// Get a file media type
SA_RET_OP_STR static const char *httpGetMediaType(SA_PARAM_NN_STR const char *pFileName)
{
	const char *pExtension = httpGetExtensionPortion(pFileName);
	struct MediaType type;
	unsigned int index = 0;

	if (!pExtension) return NULL;

	while (true) {
		type = gMediaTypeMap[index++];
		if (!type.pExtension) break;

		if (!stricmp(type.pExtension, pExtension)) return type.pMediaType;
	}

	return NULL;
}

// Send a file if it is safe to do so
static bool httpSendFileSafe(SA_PARAM_NN_VALID NetLink *pLink, SA_PARAM_OP_STR const char *pFileName)
{
	const char *pMediaType;

	if (!pFileName) return false;
	if (!httpIsSafeFile(pFileName)) return false;

	pMediaType = httpGetMediaType(pFileName);

	if (!pMediaType) return false;

	if (!fileExists(webFile(pFileName))) return false;

	httpSendFile(pLink, webFile(pFileName), pMediaType);

	return true;
}

// ------------------------------------------------------
// Webpages

static void appendAccountAddressForm(char **estr, const AccountInfo *pAccount, bool bOutputFormAndTableWrapper)
{
	const char *address1 = "";
	const char *address2 = "";
	const char *city = "";
	const char *district = "";
	const char *postalCode = "";
	const char *country = "";
	const char *phone = "";

	if(pAccount)
	{
		address1   = NULL_TO_EMPTY(pAccount->personalInfo.shippingAddress.address1);
		address2   = NULL_TO_EMPTY(pAccount->personalInfo.shippingAddress.address2);
		city       = NULL_TO_EMPTY(pAccount->personalInfo.shippingAddress.city);
		district   = NULL_TO_EMPTY(pAccount->personalInfo.shippingAddress.district);
		postalCode = NULL_TO_EMPTY(pAccount->personalInfo.shippingAddress.postalCode);
		country    = NULL_TO_EMPTY(pAccount->personalInfo.shippingAddress.country);
		phone      = NULL_TO_EMPTY(pAccount->personalInfo.shippingAddress.phone);
	}

	if(bOutputFormAndTableWrapper)
	{
		estrConcatf(estr, "<div class=\"heading\">Address</div>\n");

		wiFormAppendStart(estr, "/changeAddress", "POST", "addressform", NULL);
		formAppendHidden(estr, "accountname", (pAccount) ? pAccount->accountName : "");
		estrConcatf(estr, "<table border=1 cellpadding=3 cellspacing=0>\n");
	}

	estrConcatf(estr, "<tr><td>Address1</td><td>");
	formAppendEdit(estr, 100, "address1", address1);
	estrConcatf(estr, "</td></tr>\n<tr><td>Address2</td><td>");
	formAppendEdit(estr, 100, "address2", address2);
	estrConcatf(estr, "</td></tr>\n<tr><td>City</td><td>");
	formAppendEdit(estr, 100, "city", city);
	estrConcatf(estr, "</td></tr>\n<tr><td>District (State)</td><td>");
	formAppendEdit(estr, 100, "district", district);
	estrConcatf(estr, "</td></tr>\n<tr><td>Postal Code (ZIP)</td><td>");
	formAppendEdit(estr, 100, "postalcode", postalCode);
	estrConcatf(estr, "</td></tr>\n<tr><td>Country (US)</td><td>");
	formAppendEdit(estr, 100, "country", country);
	estrConcatf(estr, "</td></tr>\n<tr><td>Phone (408-555-5555)</td><td>");
	formAppendEdit(estr, 100, "phone", phone);
	estrConcatf(estr, "</td></tr>");

	if(bOutputFormAndTableWrapper)
	{
		estrConcatf(estr, "</table>\n");
		formAppendSubmit(estr, "Change Address");
		formAppendEnd(estr);
	}
}

#define ACCOUNT_CREATE_ERR_NAME         0x01
#define ACCOUNT_CREATE_ERR_PWD          0x02
#define ACCOUNT_MISMATCH_ERR_PWD        0x04
#define ACCOUNT_CREATE_ERR_NAME_TAKEN   0x08
#define ACCOUNT_LOGIN_ERR_INVALID       0x10
#define ACCOUNT_PARAM_INVALID           0x20 // generic invalid or missing argument flag
#define ACCOUNT_MISMATCH_ERR_EMAIL      0x40
#define ACCOUNT_CREATE_ERR_EMAIL        0x80

static void appendAccountCreation (char **estr, const char *pAccountName, const char *pEmail, int iAccountErrors)
{
	estrConcatf(estr, "<div class=\"heading\">Create New Account</div>");

	if (iAccountErrors & ACCOUNT_CREATE_ERR_NAME)
	{
		estrConcatf(estr, "<div class=\"error\">Account name must be between %d and %d characters long and contain only numbers, letters, periods ('.'), hyphens ('-'), and underscores ('_') with at least one alphanumeric character.</div>\n", 
			ASCII_NAME_MIN_LENGTH, ASCII_NAME_MAX_LENGTH);
	}
	if (iAccountErrors & ACCOUNT_CREATE_ERR_NAME_TAKEN)
	{
		estrConcatf(estr, "<div class=\"error\">Account name '%s' is already taken. Please choose a new name.</div>\n", pAccountName);
	}
	if (iAccountErrors & ACCOUNT_CREATE_ERR_PWD)
	{
		estrConcatf(estr, "<div class=\"error\">Password must be between %d and %d characters long.</div>\n",
			PASSWORD_MIN_LENGTH, PASSWORD_MAX_LENGTH);
	}
	if (iAccountErrors & ACCOUNT_MISMATCH_ERR_PWD)
	{
		estrConcatf(estr, "<div class=\"error\">Passwords do not match.</div>\n");
	}
	if (iAccountErrors & ACCOUNT_MISMATCH_ERR_EMAIL)
	{
		estrConcatf(estr, "<div class=\"error\">Emails do not match.</div>\n");
	}
	if (iAccountErrors & ACCOUNT_CREATE_ERR_EMAIL)
	{
		estrConcatf(estr, "<div class=\"error\">Email '%s' is already taken. Please choose a new one.</div>\n", pEmail);
	}
	estrConcatf(estr, "<table>");
	wiFormAppendStart(estr, "/create", "POST", "account", NULL);

	estrConcatf(estr, "<tr><td>Account Name: </td>\n");
	estrConcatf(estr, "<td>");
	formAppendEdit(estr, 40, "accountname", pAccountName);
	estrConcatf(estr, "</td></tr>\n");

	estrConcatf(estr, "<tr><td>Password: </td>\n");
	estrConcatf(estr, "<td>");
	formAppendPasswordEdit(estr, 60, "password", "");
	estrConcatf(estr, "</td></tr>\n");

	estrConcatf(estr, "<tr><td>Confirm Password: </td>\n");
	estrConcatf(estr, "<td>");
	formAppendPasswordEdit(estr, 60, "passwordConfirm", "");
	estrConcatf(estr, "</td></tr>\n");

	estrConcatf(estr, "<tr><td>Email: </td>\n");
	estrConcatf(estr, "<td>");
	formAppendEdit(estr, 60, "email", pEmail);
	estrConcatf(estr, "</td></tr>\n");

	estrConcatf(estr, "<tr><td>Confirm Email: </td>\n");
	estrConcatf(estr, "<td>");
	formAppendEdit(estr, 60, "emailConfirm", "");
	estrConcatf(estr, "</td></tr>\n");

	// TODO?
	// appendAccountAddressForm(estr, NULL, false);

	estrConcatf(estr, "</table>");
	
	formAppendSubmit(estr, "Create Account");
	formAppendEnd(estr);
}

static void updateAddressFromArgumentList(AccountInfo *pAccount, UrlArgumentList *args)
{
	const char *address1 = urlFindSafeValue(args, "address1");
	const char *address2 = urlFindSafeValue(args, "address2");
	const char *city = urlFindSafeValue(args, "city");
	const char *district = urlFindSafeValue(args, "district");
	const char *postalcode = urlFindSafeValue(args, "postalcode");
	const char *country = urlFindSafeValue(args, "country");
	const char *phonenum = urlFindSafeValue(args, "phone");

	setShippingAddress(pAccount, address1, address2, city, district, postalcode, country, phonenum);
}

// Toggle key validity
static void wiKeyInvalidate(DEFAULT_HTTPPOST_PARAMS)
{
	const char *keyName = urlFindSafeValue(args, "key");
	ProductKey key;
	bool success;
	bool valid;

	// Look up key.
	success = findProductKey(&key, keyName);
	if (!success)
	{
		char *s = NULL;
		estrStackCreate(&s);
		estrPrintf(&s, "There is no product key \"%s\" in the database.", keyName);
		httpSendWrappedString(link, s, pCookies);
		estrDestroy(&s);
		return;
	}

	// Toggle key validity.
	valid = productKeyGetInvalid(keyName);
	success = productKeySetInvalid(keyName, !valid);
	if (!success)
	{
		httpSendWrappedString(link, "Unable to mark key validity", pCookies);
		return;
	}

	// Return to key page.
	wiRedirectf(link, "/keyView?key=%s", keyName);
}

static void wiBatchInvalidate(DEFAULT_HTTPPOST_PARAMS)
{
	U32 batchID = 0;

	// Parse parameters.
	urlFindUInt(args, "batch", &batchID);

	// Update batch validity.
	if (batchID)
	{
		ProductKeyBatch *keyBatch = getKeyBatchByID(batchID);
		productKeyBatchSetInvalid(keyBatch, !keyBatch->batchInvalidated);
		wiRedirectf(link, "/batchView?id=%lu", batchID);
	}
	else
		httpSendWrappedString(link, "Form error", pCookies);
}

static void wiBatchDistributed(DEFAULT_HTTPPOST_PARAMS)
{
	U32 batchID = 0;

	// Parse parameters.
	urlFindUInt(args, "batch", &batchID);

	// Update batch validity.
	if (batchID)
	{
		ProductKeyBatch *keyBatch = getKeyBatchByID(batchID);
		productKeyBatchSetDistributed(keyBatch, !keyBatch->batchDistributed);
		wiRedirectf(link, "/batchView?id=%lu", batchID);
	}
	else
		httpSendWrappedString(link, "Form error", pCookies);
}

static void wiCreateAccountBlank(NetLink *link, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	estrCopy2(&s, "");
	appendAccountCreation(&s, "", "", 0);
	httpSendWrappedString(link, s, pCookies);
}


static void wiCreateAccount(DEFAULT_HTTPPOST_PARAMS)
{
	static char *s = NULL;
	const char *pAccountName = urlFindValue(args, "accountname");
	const char *pPassword = urlFindValue(args, "password");
	const char *pPasswordConfirm = urlFindValue(args, "passwordConfirm");
	const char *pEmail = urlFindValue(args, "email");
	const char *pEmailConfirm = urlFindValue(args, "emailConfirm");
	int iErrors = 0;

	estrCopy2(&s, "");
	if (!pAccountName && !pPassword && !pPasswordConfirm)
	{
		appendAccountCreation(&s, "", "", 0);
		httpSendWrappedString(link, s, pCookies);
		return;
	}
	if (StringIsInvalidAccountName(pAccountName, 0) || StringIsInvalidDisplayName(pAccountName, 0))
	{
		iErrors |= ACCOUNT_CREATE_ERR_NAME;
	}
	if (!StringIsValidPassword(pPassword))
	{
		iErrors |= ACCOUNT_CREATE_ERR_PWD;
	}
	if (!pPassword || !pPasswordConfirm || strcmp(pPassword, pPasswordConfirm) != 0)
	{
		iErrors |= ACCOUNT_MISMATCH_ERR_PWD;
	}
	if (!pEmail || !pEmailConfirm || stricmp(pEmail, pEmailConfirm) != 0)
	{
		iErrors |= ACCOUNT_MISMATCH_ERR_EMAIL;
	}
	if (pEmail && pEmail[0] && findAccountByEmail(pEmail))
	{
		iErrors |= ACCOUNT_CREATE_ERR_EMAIL;
	}

	if (findAccountByName(pAccountName) || findAccountByDisplayName(pAccountName))
	{
		iErrors |= ACCOUNT_CREATE_ERR_NAME_TAKEN;
	}
	if (iErrors)
	{
		// go back to account creation page with errors
		appendAccountCreation (&s, pAccountName, pEmail, iErrors);
	}
	else
	{
		AccountInfo *pAccount = NULL;
		char hashedPassword[MAX_PASSWORD] = "";
		const char *loggedIn = httpFindAuthenticationUsername(pClientState);
		char ipBuf[17];
		char **ips = NULL;
	
		accountHashPassword(pPassword, hashedPassword);
        //TODO JDRAGO - needs updateAddressFromArgumentList() somehow

		// Get Internet address of web interface user.
		eaPush(&ips, linkGetIpStr(link, ipBuf, sizeof(ipBuf)));

		if (createFullAccount(pAccountName, hashedPassword, pAccountName, pEmail, NULL, NULL, NULL, NULL, NULL, ips, false, false, 9))
		{
			pAccount = findAccountByName(pAccountName);
		}
		eaDestroy(&ips);
		if (pAccount)
		{
			estrConcatf(&s, "Successfully created account '%s'", pAccountName);
		}
		else
		{
			estrConcatf(&s, "Failed to create account '%s'", pAccountName);
		}
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: createAccount(%s,%s), %s, %s", pAccountName, pEmail, 
			loggedIn ? loggedIn : "Unknown User", 
			pAccount ? "Success" : "Failed");
	}
	httpSendWrappedString(link, s, pCookies);
}

static void appendPasswordChange (char **estr, const char *pAccountName, int iAccountErrors)
{
	estrConcatf(estr, "<div class=\"heading\">Change Password</div>");
	
	estrConcatf(estr, "<table>");
	wiFormAppendStart(estr, "/changePassword", "POST", "pwdchange", NULL);

	estrConcatf(estr, "<tr><td colspan=2>");
	if (iAccountErrors & ACCOUNT_LOGIN_ERR_INVALID)
	{
		estrConcatf(estr, "<div class=\"error\">Invalid account name and/or password.</div>");
	}
	estrConcatf(estr, "</td></tr>\n");

	estrConcatf(estr, "<tr><td>Account Name: </td>\n");
	estrConcatf(estr, "<td>");
	formAppendEdit(estr, 40, "accountname", pAccountName);
	estrConcatf(estr, "</td></tr>\n");

	estrConcatf(estr, "<tr><td colspan=2>");
	if (iAccountErrors & ACCOUNT_CREATE_ERR_PWD)
	{
		estrConcatf(estr, "<div class=\"error\">New password must be an alphanumeric string of less than 128 characters.</div>");
	}
	if (iAccountErrors & ACCOUNT_MISMATCH_ERR_PWD)
	{
		estrConcatf(estr, "<div class=\"error\">Passwords do not match.</div>");
	}
	estrConcatf(estr, "</td></tr>\n");

	estrConcatf(estr, "<tr><td>New Password: </td>\n");
	estrConcatf(estr, "<td>");
	formAppendPasswordEdit(estr, 40, "newpassword", "");
	estrConcatf(estr, "</td></tr>\n");

	estrConcatf(estr, "<tr><td>Confirm New Password: </td>\n");
	estrConcatf(estr, "<td>");
	formAppendPasswordEdit(estr, 40, "passwordConfirm", "");
	estrConcatf(estr, "</td></tr>\n");

	estrConcatf(estr, "</table>");
	
	formAppendSubmit(estr, "Change Password");
	formAppendEnd(estr);
}

static void wiChangePasswordBlank(NetLink *link, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	estrCopy2(&s, "");
	appendPasswordChange(&s, "", 0);
	httpSendWrappedString(link, s, pCookies);
}

static void wiChangePassword(DEFAULT_HTTPPOST_PARAMS)
{
	static char *s = NULL;
	const char *pAccountName = urlFindValue(args, "accountname");
	const char *pPassword = urlFindValue(args, "newpassword");
	const char *pPasswordConfirm = urlFindValue(args, "passwordConfirm");
	int iErrors = 0;

	estrCopy2(&s, "");
	if (!pAccountName && !pPassword && !pPasswordConfirm)
	{
		appendPasswordChange(&s, "", 0);
		httpSendWrappedString(link, s, pCookies);
		return;
	}

	if (!pPassword || !pPasswordConfirm || strcmp(pPassword, pPasswordConfirm) != 0)
	{
		iErrors |= ACCOUNT_MISMATCH_ERR_PWD;
	}
	if (!pPassword || strlen(pPassword) >= MAX_PASSWORD_PLAINTEXT || !StringIsValidPassword(pPassword))
	{
		iErrors |= ACCOUNT_CREATE_ERR_PWD;
	}

	if (iErrors)
	{
		appendPasswordChange(&s, pAccountName, iErrors);
	}
	else
	{
		bool bSuccess = forceChangePasswordInternal(findAccountByName(pAccountName), pPassword);
		const char *loggedIn = httpFindAuthenticationUsername(pClientState);
		if (bSuccess)
		{
			estrConcatf(&s, "Password changed for account '%s'", pAccountName);
		}
		else
		{
			estrConcatf(&s, "Failed to change password for account '%s'", pAccountName);
		}
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: changePassword(%s), %s, %s", pAccountName, 
			loggedIn ? loggedIn : "Unknown User", 
			bSuccess ? "Success" : "Failed");
	}
	httpSendWrappedString(link, s, pCookies);
}

static void appendDeleteAccount (char **estr, const char *pAccountName, int iAccountErrors)
{
	estrConcatf(estr, "<div class=\"heading\">Delete Account</div>");
	
	estrConcatf(estr, "<table>");
	wiFormAppendStart(estr, "/deleteAccount", "POST", "pwdchange", NULL);

	estrConcatf(estr, "<tr><td colspan=2>");
	if (iAccountErrors & ACCOUNT_LOGIN_ERR_INVALID)
	{
		estrConcatf(estr, "<div class=\"error\">Account does not exist.</div>");
	}
	estrConcatf(estr, "</td></tr>\n");

	estrConcatf(estr, "<tr><td>Account Name: </td>\n");
	estrConcatf(estr, "<td>");
	formAppendEdit(estr, 20, "accountname", pAccountName);
	estrConcatf(estr, "</td></tr>\n");

	estrConcatf(estr, "</table>");
	
	formAppendSubmit(estr, "Delete Account");
	formAppendEnd(estr);
}

static void wiDeleteAccountBlank(NetLink *link, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	estrCopy2(&s, "");
	appendDeleteAccount(&s, "", 0);
	httpSendWrappedString(link, s, pCookies);
}

static void wiChangeAddress(DEFAULT_HTTPPOST_PARAMS)
{
	static char *s = NULL;
	const char *pAccountName = urlFindValue(args, "accountname");
	Container *con;
	int iCount = 0;
	AccountInfo *pAccount = NULL;
	NOCONST(AccountAddress) *pAddress = NULL;
	
	estrCopy2(&s, "");
	if (!pAccountName)
	{
		wiRedirectf(link, "/detail");
		return;
	}

	con = findAccountContainerByName(pAccountName);
	if (!con || !con->containerData)
	{
		httpSendWrappedString(link, "Account not found", pCookies);
		return;
	}
	pAccount = (AccountInfo*) con->containerData;
	updateAddressFromArgumentList(pAccount, args);
	btAccountPush(pAccount);
	wiRedirectf(link, "/detail?accountname=%s", pAccount->accountName);
}

static void wiToggleInternalUseOnlyFlag (DEFAULT_HTTPGET_PARAMS)
{
	int i;
	U32 accountID = 0;
	int internalflag = -1;
	AccountInfo *pAccount = NULL;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "accountid") == 0)
		{
			accountID = atoi(values[i]);
		}
		else if (stricmp(args[i], "internalflag") == 0)
		{
			internalflag = atoi(values[i]);
		}
	}

	if (internalflag < 0 || accountID == 0)
		return;

	pAccount = findAccountByID(accountID);
	if (!pAccount)
		return;

	setInternalUse(pAccount->uID, !!internalflag);
	if (strstri(pReferer, "detail"))
		wiRedirectf(link, "/detail?accountName=%s", pAccount->accountName);
	else
		wiRedirectf(link, "/");
}

static void wiToggleLoginDisabledFlag(DEFAULT_HTTPGET_PARAMS)
{
	int i;
	U32 accountID = 0;
	int logindisabledflag = -1;
	AccountInfo *pAccount = NULL;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "accountid") == 0)
		{
			accountID = atoi(values[i]);
		}
		else if (stricmp(args[i], "disableloginflag") == 0)
		{
			logindisabledflag = atoi(values[i]);
		}
	}

	if (logindisabledflag < 0 || accountID == 0)
		return;

	pAccount = findAccountByID(accountID);
	if (!pAccount)
		return;

	setLoginDisabled(pAccount->uID, !!logindisabledflag);
	if (strstri(pReferer, "detail"))
		wiRedirectf(link, "/detail?accountName=%s", pAccount->accountName);
	else
		wiRedirectf(link, "/");
}

static void appendFancyTableHeaderInternal(char **estr, bool bPaginated, int num, ...)
{
	int i;

	estrConcatf(estr, "<div class=\"summary-container\">");
	if (bPaginated)
	{
		estrConcatf(estr, "<div class=\"pager\" id=\"pager\">"
			"<form>"
			"<img src=\"/first.png\" class=\"first\"/>"
			"<img src=\"/prev.png\" class=\"prev\"/>"
			"<input type=\"text\" class=\"pagedisplay\"/>"
			"<img src=\"/next.png\" class=\"next\"/>"
			"<img src=\"/last.png\" class=\"last\"/>"
			"<select class=\"pagesize\">"
			"<option value=\"10\">10</option>"
			"<option value=\"20\">20</option>"
			"<option value=\"30\">30</option>"
			"<option value=\"40\">40</option>"
			"<option selected=\"selected\" value=\"50\">50</option>"
			"</select>"
			"</form>"
			"</div>");
	}

	estrConcatf(estr, "<table width=\"100%%\" class=\"summarytable tablesorter");
	
	if (bPaginated)
	{
		estrConcatf(estr, " summary-paginated");
	}

	estrConcatf(estr, "\" border=\"0\" cellpadding=\"0\" cellspacing=\"1\">\n"
		"\t<thead>\n\t\t<tr>\n");

	VA_START(args, num);
		for (i = 0; i < num; i++)
		{
			char *header = va_arg(args, char*);
			estrConcatf(estr, "<th align=right class=\"summary-head\">%s</th>\n", header);
		}
	VA_END();

	estrConcatf(estr, "\t\t</tr>\n\t</thead><tbody>\n");
}

#define appendFancyTableHeader(estr, num, ...) appendFancyTableHeaderInternal(estr, false, num,  ##__VA_ARGS__)
#define appendPaginatedFancyTableHeader(estr, num, ...) appendFancyTableHeaderInternal(estr, true, num,  ##__VA_ARGS__)

static void appendFancyTableEntry(char **estr, bool odd, int num, ...)
{
	int i;
	VA_START(args, num);
	estrConcatf(estr, "<tr>\n");
	for (i=0; i<num; i++)
	{
		char *value = va_arg(args, char*);
		estrConcatf(estr, "<td align=right class=\"summary-%s\">%s</td>\n", odd ? "odd" : "even", 
			value && *value ? value : "&nbsp;");
	}
	estrConcatf(estr, "</tr>\n");
	VA_END();
}

static void appendFancyTableFooter(char **estr)
{
	estrConcatf(estr, "</tbody></table></div>");
}

static const char *subStatusToString(SubscriptionStatus s)
{
	return StaticDefineIntRevLookupNonNull(SubscriptionStatusEnum, s);
}

typedef struct UpdatePaymentMethodWIHolder
{
	NetLink *pLink;
	AccountInfo *pAccount;
} UpdatePaymentMethodWIHolder;

static void wiUpdatePaymentMethodCB(UpdatePMResult eResult,
									SA_PARAM_OP_VALID BillingTransaction *pTrans,
									SA_PARAM_OP_VALID const CachedPaymentMethod *pCachedPaymentMethod,
									SA_PARAM_NN_VALID UpdatePaymentMethodWIHolder *holder)
{
	wiRedirectf(holder->pLink, "/detail?accountname=%s", holder->pAccount->accountName);

	free(holder);
}

static void wiUpdatePaymentMethod(DEFAULT_HTTPPOST_PARAMS)
{
	PaymentMethod *pm = StructCreate(parse_PaymentMethod);
	U32 accountID;
	AccountInfo *account;

	urlFindUInt(args, "accountID", &accountID);
	account = findAccountByID(accountID);

	if(account)
	{
		UpdatePaymentMethodWIHolder *holder = callocStruct(UpdatePaymentMethodWIHolder);
		pm->creditCard = StructCreate(parse_CreditCard);

		estrCopy2(&pm->creditCard->account, urlFindValue(args, "account"));
		estrCopy2(&pm->accountHolderName, urlFindValue(args, "accountHolderName"));
		pm->active = urlFindValue(args, "active") ? true : false;
		estrCopy2(&pm->addr1, urlFindValue(args, "address1"));
		estrCopy2(&pm->addr2, urlFindValue(args, "address2"));
		estrCopy2(&pm->addressName, urlFindValue(args, "addressName"));
		estrCopy2(&pm->city, urlFindValue(args, "city"));
		estrCopy2(&pm->country, urlFindValue(args, "country"));
		estrCopy2(&pm->county, urlFindValue(args, "county"));
		estrCopy2(&pm->currency, urlFindValue(args, "currency"));
		estrCopy2(&pm->customerDescription, urlFindValue(args, "customerDescription"));
		estrCopy2(&pm->customerSpecifiedType, urlFindValue(args, "customerSpecifiedType"));
		estrCopy2(&pm->creditCard->CVV2, urlFindValue(args, "CVV2"));
		estrCopy2(&pm->district, urlFindValue(args, "district"));
		estrCopy2(&pm->creditCard->expirationDate, urlFindValue(args, "expirationDate"));
		estrCopy2(&pm->phone, urlFindValue(args, "phone"));
		estrCopy2(&pm->postalCode, urlFindValue(args, "postalCode"));
		estrCopy2(&pm->VID, urlFindValue(args, "VID"));

		holder->pAccount = account;
		holder->pLink = link;

		if (pm->currency && *pm->currency)
		{
			UpdatePaymentMethod(account, pm, ADMIN_IP, NULL, NULL, wiUpdatePaymentMethodCB, holder);
		}
	}

	StructDestroy(parse_PaymentMethod, pm);
}

static void wiLocalizeProduct(DEFAULT_HTTPPOST_PARAMS)
{
	const ProductContainer *pProduct;
	U32 uProductID;
	const char *pLanguageTag = urlFindSafeValue(args, "languageTag");
	const char *pName = urlFindSafeValue(args, "name");
	const char *pDescription = urlFindSafeValue(args, "description");

	urlFindUInt(args, "productID", &uProductID);
	
	pProduct = findProductByID(uProductID);

	if (pProduct)
	{
		productLocalize(uProductID, pLanguageTag, pName, pDescription);
		wiRedirectf(link, "/productDetail?product=%s", pProduct->pName);
	}
}

static void wiUnlocalizeProduct(DEFAULT_HTTPPOST_PARAMS)
{
	const ProductContainer *pProduct;
	U32 uProductID;
	const char *pLanguageTag = urlFindSafeValue(args, "languageTag");

	urlFindUInt(args, "productID", &uProductID);

	pProduct = findProductByID(uProductID);

	if (pProduct)
	{
		productUnlocalize(uProductID, pLanguageTag);
		wiRedirectf(link, "/productDetail?product=%s", pProduct->pName);
	}
}

static void wiLocalizeSubscription(DEFAULT_HTTPPOST_PARAMS)
{
	const SubscriptionContainer *pSubscription;
	U32 uSubscriptionID;
	const char *pLanguageTag = urlFindSafeValue(args, "languageTag");
	const char *pName = urlFindSafeValue(args, "name");
	const char *pDescription = urlFindSafeValue(args, "description");

	urlFindUInt(args, "subscriptionID", &uSubscriptionID);

	pSubscription = findSubscriptionByID(uSubscriptionID);

	if (pSubscription)
	{
		subscriptionLocalize(uSubscriptionID, pLanguageTag, pName, pDescription);
		wiRedirectf(link, "/subscriptionDetail?subscription=%s", pSubscription->pName);
	}
}

static void wiUnlocalizeSubscription(DEFAULT_HTTPPOST_PARAMS)
{
	const SubscriptionContainer *pSubscription;
	U32 uSubscriptionID;
	const char *pLanguageTag = urlFindSafeValue(args, "languageTag");

	urlFindUInt(args, "subscriptionID", &uSubscriptionID);

	pSubscription = findSubscriptionByID(uSubscriptionID);

	if (pSubscription)
	{
		subscriptionUnlocalize(uSubscriptionID, pLanguageTag);
		wiRedirectf(link, "/subscriptionDetail?subscription=%s", pSubscription->pName);
	}
}

static void wiSetProductKeyValue(DEFAULT_HTTPPOST_PARAMS)
{
	const ProductContainer *pProduct;
	U32 uProductID;
	const char *pKey = urlFindSafeValue(args, "key");
	const char *pValue = urlFindSafeValue(args, "value");

	urlFindUInt(args, "productID", &uProductID);

	pProduct = findProductByID(uProductID);

	if (pProduct)
	{
		productSetKeyValue(uProductID, pKey, pValue);
		wiRedirectf(link, "/productDetail?product=%s", pProduct->pName);
	}
}

static void wiEditPaymentMethod(DEFAULT_HTTPGET_PARAMS)
{
	static char *estr = NULL;
	int i;
	U32 accountID = 0;
	int pmid = -1;
	AccountInfo *pAccount = NULL;
	CachedPaymentMethod *pPayment = NULL;


	for (i = 0; i < count; i++)
	{
		if (stricmp(args[i], "accountid") == 0)
		{
			accountID = atoi(values[i]);
		}

		if (stricmp(args[i], "pmid") == 0)
		{
			pmid = atoi(values[i]);
		}
	}

	pAccount = findAccountByID(accountID);
	if (!pAccount) return;

	pPayment = (pmid >= 0 && pmid < eaSize(&pAccount->personalInfo.ppPaymentMethods)) ?
			   pAccount->personalInfo.ppPaymentMethods[pmid] : NULL;

	estrCopy2(&estr, "");

	estrConcatf(&estr, "<div class=\"heading\">Payment Method</div>\n");
	wiFormAppendStart(&estr, "/updatePaymentMethod", "POST", "paymentMethod", NULL);
	estrConcatf(&estr, "<fieldset>\n");
	estrConcatf(&estr, "<legend>Credit Card</legend>\n");
	estrConcatf(&estr, "<table border=\"1\" cellpadding=\"3\" cellspacing=\"0\">\n");

	if (pPayment)
	{
		estrConcatf(&estr, "<tr>\n");
		estrConcatf(&estr, "<td>VID</td>");
		estrConcatf(&estr, "<td>%s</td>", pPayment->VID);
		estrConcatf(&estr, "</tr>\n");

		estrConcatf(&estr, "<tr>\n");
		estrConcatf(&estr, "<td>Credit Card Number</td>");
		estrConcatf(&estr, "<td>%s-xxxx-%s</td>", pPayment->creditCard->bin, pPayment->creditCard->lastDigits);
		estrConcatf(&estr, "</tr>\n");
	}
	else
	{
		estrConcatf(&estr, "<tr>\n");
		estrConcatf(&estr, "<td><label for=\"account\">Credit Card Number</label></td>\n");
		estrConcatf(&estr, "<td>");
		formAppendEdit(&estr, 100, "account", "");
		estrConcatf(&estr, "</td>\n");
		estrConcatf(&estr, "</tr>\n");

		estrConcatf(&estr, "<tr>\n");
		estrConcatf(&estr, "<td><label for=\"CVV2\" title=\"Card Verification Value\">CVV2</label></td>\n");
		estrConcatf(&estr, "<td>");
		formAppendEdit(&estr, 100, "CVV2", "");
		estrConcatf(&estr, "</td>\n");
		estrConcatf(&estr, "</tr>\n");
	}

	estrConcatf(&estr, "<tr>\n");
	estrConcatf(&estr, "<td><label for=\"expirationDate\">Expiration Date</label></td>\n");
	estrConcatf(&estr, "<td>");
	formAppendEdit(&estr, 100, "expirationDate", pPayment && pPayment->creditCard->expireDate ? pPayment->creditCard->expireDate : "");
	estrConcatf(&estr, "</td>\n");
	estrConcatf(&estr, "</tr>\n");
	estrConcatf(&estr, "<tr>\n");
	estrConcatf(&estr, "<td><label for=\"accountHolderName\">Account Holder Name</label></td>\n");
	estrConcatf(&estr, "<td>");
	formAppendEdit(&estr, 100, "accountHolderName", pPayment && pPayment->accountName ? pPayment->accountName : "");
	estrConcatf(&estr, "</td>\n");
	estrConcatf(&estr, "</tr>\n");
	estrConcatf(&estr, "<tr>\n");
	estrConcatf(&estr, "<td><label for=\"customerDescription\">Customer Description</label></td>\n");
	estrConcatf(&estr, "<td>");
	formAppendEdit(&estr, 100, "customerDescription", pPayment && pPayment->description ? pPayment->description : "");
	estrConcatf(&estr, "</td>\n");
	estrConcatf(&estr, "</tr>\n");
	estrConcatf(&estr, "<tr>\n");
	estrConcatf(&estr, "<td><label for=\"customerSpecifiedType\">Customer Specified Type</label></td>\n");
	estrConcatf(&estr, "<td>");
	formAppendEdit(&estr, 100, "customerSpecifiedType", pPayment && pPayment->type ? pPayment->type : "");
	estrConcatf(&estr, "</td>\n");
	estrConcatf(&estr, "</tr>\n");
	estrConcatf(&estr, "</table>\n");
	estrConcatf(&estr, "</fieldset>\n");
	estrConcatf(&estr, "<fieldset>\n");
	estrConcatf(&estr, "<legend>Billing Address</legend>\n");
	estrConcatf(&estr, "<table border=\"1\" cellpadding=\"3\" cellspacing=\"0\">\n");
	estrConcatf(&estr, "<tr>\n");
	estrConcatf(&estr, "<td><label for=\"address1\" title=\"Address Line 1\">Address 1</label></td>\n");
	estrConcatf(&estr, "<td>");
	formAppendEdit(&estr, 100, "address1", pPayment && pPayment->billingAddress.address1 ? pPayment->billingAddress.address1 : "");
	estrConcatf(&estr, "</td>\n");
	estrConcatf(&estr, "</tr>\n");
	estrConcatf(&estr, "<tr>\n");
	estrConcatf(&estr, "<td><label for=\"address2\" title=\"Address Line 2\">Address 2</label></td>\n");
	estrConcatf(&estr, "<td>");
	formAppendEdit(&estr, 100, "address2", pPayment && pPayment->billingAddress.address2 ? pPayment->billingAddress.address2 : "");
	estrConcatf(&estr, "</td>\n");
	estrConcatf(&estr, "</tr>\n");
	estrConcatf(&estr, "<tr>\n");
	estrConcatf(&estr, "<td><label for=\"city\">City</label></td>\n");
	estrConcatf(&estr, "<td>");
	formAppendEdit(&estr, 100, "city", pPayment && pPayment->billingAddress.city ? pPayment->billingAddress.city : "");
	estrConcatf(&estr, "</td>\n");
	estrConcatf(&estr, "</tr>\n");
	estrConcatf(&estr, "<tr>\n");
	estrConcatf(&estr, "<td><label for=\"country\">Country</label></td>\n");
	estrConcatf(&estr, "<td>");
	formAppendEdit(&estr, 100, "country", pPayment && pPayment->billingAddress.country ? pPayment->billingAddress.country : "");
	estrConcatf(&estr, "</td>\n");
	estrConcatf(&estr, "</tr>\n");
	estrConcatf(&estr, "<tr>\n");
	estrConcatf(&estr, "<td><label for=\"district\">District/State</label></td>\n");
	estrConcatf(&estr, "<td>");
	formAppendEdit(&estr, 100, "district", pPayment && pPayment->billingAddress.district ? pPayment->billingAddress.district : "");
	estrConcatf(&estr, "</td>\n");
	estrConcatf(&estr, "</tr>\n");
	estrConcatf(&estr, "<tr>\n");
	estrConcatf(&estr, "<td><label for=\"postalCode\">Postal/Zip Code</label></td>\n");
	estrConcatf(&estr, "<td>");
	formAppendEdit(&estr, 100, "postalCode", pPayment && pPayment->billingAddress.postalCode ? pPayment->billingAddress.postalCode : "");
	estrConcatf(&estr, "</td>\n");
	estrConcatf(&estr, "</tr>\n");
	estrConcatf(&estr, "<tr>\n");
	estrConcatf(&estr, "<td><label for=\"phone\">Phone Number</label></td>\n");
	estrConcatf(&estr, "<td>");
	formAppendEdit(&estr, 100, "phone", pPayment && pPayment->billingAddress.phone ? pPayment->billingAddress.phone : "");
	estrConcatf(&estr, "</td>\n");
	estrConcatf(&estr, "</tr>\n");
	estrConcatf(&estr, "</table>\n");
	estrConcatf(&estr, "</fieldset>\n");
	estrConcatf(&estr, "<fieldset>\n");
	estrConcatf(&estr, "<legend>Other</legend>\n");
	estrConcatf(&estr, "<table border=\"1\" cellpadding=\"3\" cellspacing=\"0\">\n");
	estrConcatf(&estr, "<tr>\n");
	estrConcatf(&estr, "<td><label for=\"currency\">Currency</label></td>\n");
	estrConcatf(&estr, "<td>");
	formAppendEdit(&estr, 100, "currency", pPayment && pPayment->currency ? pPayment->currency : "");
	estrConcatf(&estr, "</td>\n");
	estrConcatf(&estr, "</tr>\n");
	estrConcatf(&estr, "<tr>\n");
	estrConcatf(&estr, "<td><label for=\"active\">Active</label></td>\n");
	estrConcatf(&estr, "<td>");
	estrConcatf(&estr, "<input type=\"checkbox\" name=\"active\" checked=\"checked\" value=\"1\"> (uncheck to disable/hide this payment method)");
	estrConcatf(&estr, "</td>\n");
	estrConcatf(&estr, "</tr>\n");
	estrConcatf(&estr, "</table>\n");
	estrConcatf(&estr, "</fieldset>\n");
	formAppendHidden(&estr, "VID", pPayment ? pPayment->VID : "");
	formAppendHiddenInt(&estr, "accountID", accountID);
	formAppendSubmit(&estr, "Save");
	formAppendEnd(&estr);

	httpSendWrappedString(link, estr, pCookies);
}

static void wiAppendInternalSubscriptions(SA_PARAM_NN_VALID char **s, U32 uAccountID)
{
	EARRAY_OF(const InternalSubscription) eaSubs = findInternalSubsByAccountID(uAccountID);

	HTML_HEADING(s, "Internal Subscriptions");
	HTML_DESCRIPTION(s, "These are internal subscriptions granted by products.  They are not stored in Vindicia.");

	if (eaSubs)
	{
		appendFancyTableHeader(s, 4, "Internal Name", "Creation", "Expiration", "Product From");
		EARRAY_CONST_FOREACH_BEGIN(eaSubs, i, size);
			char expiration[256];
			char creation[256];
			char *productLink = NULL;
			const ProductContainer *pProduct = findProductByID(eaSubs[i]->uProductID);

			if (pProduct)
			{
				estrPrintf(&productLink, "<a href=\"productDetail?product=%s\">%s</a>", pProduct->pName, pProduct->pName);
			}
			else
			{
				estrPrintf(&productLink, "None");
			}

			if (eaSubs[i]->uExpiration)
			{
				timeMakeLocalDateStringFromSecondsSince2000(expiration, eaSubs[i]->uExpiration);
			}
			else
			{
				sprintf(expiration, "Never");
			}
			timeMakeLocalDateStringFromSecondsSince2000(creation, eaSubs[i]->uCreated);
			appendFancyTableEntry(s, i%2, 4,
				eaSubs[i]->pSubInternalName, creation, expiration, productLink);
			estrDestroy(&productLink);
		EARRAY_FOREACH_END;
		eaDestroy(&eaSubs);
		appendFancyTableFooter(s);
	}
}

static void wiAppendSubHistoryTable(SA_PRE_NN_NN_VALID char **estr, SA_PARAM_NN_VALID const AccountInfo *pAccount)
{
	int i = 0;
	static char *pToggleLink = NULL;

	HTML_HEADING(estr, "Archived Subscription History");
	HTML_DESCRIPTION(estr, "All archived subscription history for an account.  Does not include current subscriptions.");
	estrConcatf(estr, "[<a href=\"recalculateSubHistory?account=%d\">Recalculate All</a>]", pAccount->uID);
	appendFancyTableHeader(estr, 7,
		"Prod Internal Name",
		"Sub Internal Name",
		"Reason",
		"Enabled",
		"Adjusted Total Time",
		"Source",
		"Details");

	EARRAY_CONST_FOREACH_BEGIN(pAccount->ppSubscriptionHistory, iHistory, size);
	{
		const SubscriptionHistory *pHistory = pAccount->ppSubscriptionHistory[iHistory];
		const char *pProductInternalName;

		if (!devassert(pHistory)) continue;

		pProductInternalName = pHistory->pProductInternalName;

		if (!devassert(pProductInternalName && *pProductInternalName)) continue;

		EARRAY_CONST_FOREACH_BEGIN(pHistory->eaArchivedEntries, iEntry, numEntries);
		{
			const SubscriptionHistoryEntry *pEntry = pHistory->eaArchivedEntries[iEntry];
			static char *pLink = NULL;

			estrPrintf(&pLink, "<a href=\"subHistory?account=%d&product=%s&id=%d\">Details</a>", pAccount->uID, pProductInternalName, pEntry->uID);

			if (!devassert(pEntry)) continue;

			estrPrintf(&pToggleLink, "<a href=\"toggleSubHistory?account=%d&product=%s&id=%d\">%s</a>",
				pAccount->uID, pHistory->pProductInternalName, pEntry->uID, pEntry->bEnabled ? "Yes" : "No");

			appendFancyTableEntry(estr, i%2, 7,
				pProductInternalName,
				NULL_TO_EMPTY(pEntry->pSubInternalName),
				StaticDefineIntRevLookupNonNull(SubscriptionHistoryEntryReasonEnum, pEntry->eReason),
				pToggleLink,
				GetPrettyDurationString(subHistoryEntrySeconds(pEntry)),
				StaticDefineIntRevLookupNonNull(SubscriptionTimeSourceEnum, pEntry->eSubTimeSource),
				pLink);

			i++;
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	appendFancyTableFooter(estr);
}

static void wiAppendAccountInfo_KeyValues(char **estr, AccountInfo *pAccount)
{
	bool bOdd = false;
	STRING_EARRAY eaKeys = NULL;

	estrConcatf(estr, "<div class=\"heading\">Add or Change Key Value</div>\n");
	wiFormAppendStart(estr, "/accountAddKeyValuePair", "POST", "setkey", NULL);
	formAppendHiddenInt(estr, "id", pAccount->uID);
	formAppendEdit(estr, 20, "key", "");
	formAppendEdit(estr, 20, "value", "");
	formAppendSubmit(estr, "Add/Change Value");
	formAppendEnd(estr);

	eaKeys = AccountKeyValue_GetAccountKeyList(pAccount);

	appendFancyTableHeader(estr, 4, "Key", "Value", "Locked", "Remove");
	EARRAY_CONST_FOREACH_BEGIN(eaKeys, iCurKey, iNumKeys);
	{
		char buffer[256];
		const char *pKey = eaKeys[iCurKey];
		bool locked = AccountKeyValue_IsLocked(pAccount, pKey);
		S64 iValue = 0;

		if (AccountKeyValue_Get(pAccount, pKey, &iValue) != AKV_SUCCESS)
		{
			continue;
		}

		bOdd = !bOdd;

		estrConcatf(estr, "<tr><td class=\"summary%std\">%s</td>",
			(bOdd)?"even":"odd", pKey);

		estrConcatf(estr, "<td class=\"summary%std\">%"FORM_LL"d</td>\n",
			(bOdd)?"even":"odd", iValue);

		if (locked)
		{
			estrConcatf(estr, "<td class=\"summary%std\">", (bOdd)?"even":"odd");
			sprintf(buffer, "unlockKeyValuePair_%s", pKey);
			wiFormAppendStart(estr, "/accountUnlockKeyValuePair", "POST", buffer, NULL);
			formAppendHiddenInt(estr, "id", pAccount->uID);
			formAppendHidden(estr, "key", pKey);
			formAppendSubmit(estr, "Unlock");
			formAppendEnd(estr);
			estrConcatf(estr, "</td>\n");
		}
		else
		{
			estrConcatf(estr, "<td class=\"summary%std\">not locked</td>",
				(bOdd)?"even":"odd");
		}

		estrConcatf(estr, "<td class=\"summary%std\">", (bOdd)?"even":"odd");
		sprintf(buffer, "removeKeyValuePair_%s", pKey);
		wiFormAppendStart(estr, "/accountRemoveKeyValuePair", "POST", buffer, NULL);
		formAppendHiddenInt(estr, "id", pAccount->uID);
		formAppendHidden(estr, "key", pKey);
		formAppendSubmit(estr, "Remove");
		formAppendEnd(estr);
		estrConcatf(estr, "</td></tr>\n");
	}
	EARRAY_FOREACH_END;
	appendFancyTableFooter(estr);

	AccountKeyValue_DestroyAccountKeyList(&eaKeys);
}

static void wiAppendMachineLocking(char **estr, AccountInfo *pAccount)
{
	HTML_HEADING(estr, "Account Guard");
	HTML_DESCRIPTION(estr, "Enable/Disable Account Guard and manage the list of saved machine IDs.");
	if (accountMachineLockingIsEnabled(pAccount))
	{
		char dateTime[64];
		char *pRemoveLink = NULL;
		int iCount = 0;
		char *pMachineNameHtmlSafe = NULL;

		estrConcatf(estr, "<a href=\"machinelock?accountid=%d&flag=0\">Yes</a> (Click to Toggle)\n", pAccount->uID);
		appendFancyTableHeader(estr, 6, "Machine ID", "Machine Name", "Last Seen", "Type", "Last IP", "Remove");
		EARRAY_FOREACH_BEGIN(pAccount->eaSavedClients, i);
		{
			SavedMachine *pMachine = pAccount->eaSavedClients[i];

			estrCopyWithHTMLEscaping(&pMachineNameHtmlSafe, pMachine->pMachineName, false);
			estrPrintf(&pRemoveLink, "<a href =\"removemachine?accountid=%d&machineid=%s&type=%d\">Remove</a>", pAccount->uID, pMachine->pMachineID, MachineType_CrypticClient);
			timeMakeLocalDateStringFromSecondsSince2000(dateTime, pMachine->uLastSeenTime);
			appendFancyTableEntry(estr, iCount%2, 6, pMachine->pMachineID, pMachineNameHtmlSafe, dateTime, "Cryptic Client", pMachine->ip, pRemoveLink);
			iCount++;
		}
		EARRAY_FOREACH_END;
		EARRAY_FOREACH_BEGIN(pAccount->eaSavedBrowsers, i);
		{
			SavedMachine *pMachine = pAccount->eaSavedBrowsers[i];

			estrCopyWithHTMLEscaping(&pMachineNameHtmlSafe, pMachine->pMachineName, false);
			estrPrintf(&pRemoveLink, "<a href =\"removemachine?accountid=%d&machineid=%s&type=%d\">Remove</a>", pAccount->uID, pMachine->pMachineID, MachineType_WebBrowser);
			timeMakeLocalDateStringFromSecondsSince2000(dateTime, pMachine->uLastSeenTime);
			appendFancyTableEntry(estr, iCount%2, 6, pMachine->pMachineID, pMachineNameHtmlSafe, dateTime, "Browser", pMachine->ip, pRemoveLink);
			iCount++;
		}
		EARRAY_FOREACH_END;
		estrDestroy(&pRemoveLink);
		estrDestroy(&pMachineNameHtmlSafe);
		appendFancyTableFooter(estr);

		HTML_DESCRIPTION(estr, "Save next Cryptic Client machine ID that logs in.");
		if (pAccount->flags & ACCOUNT_FLAG_AUTOSAVE_MACHINEID_CLIENT)
			estrConcatf(estr, "<a href=\"machinelocknext?accountid=%d&flag=0&type=%d\">Yes</a> (Click to Toggle)\n", pAccount->uID, MachineType_CrypticClient);
		else
			estrConcatf(estr, "<a href=\"machinelocknext?accountid=%d&flag=1&type=%d\">No</a> (Click to Toggle)\n", pAccount->uID, MachineType_CrypticClient);
		HTML_DESCRIPTION(estr, "Save next web browser machine ID that logs in.");
		if (pAccount->flags & ACCOUNT_FLAG_AUTOSAVE_MACHINEID_BROWSER)
			estrConcatf(estr, "<a href=\"machinelocknext?accountid=%d&flag=0&type=%d\">Yes</a> (Click to Toggle)\n", pAccount->uID, MachineType_WebBrowser);
		else
			estrConcatf(estr, "<a href=\"machinelocknext?accountid=%d&flag=1&type=%d\">No</a> (Click to Toggle)\n", pAccount->uID, MachineType_WebBrowser);
	}
	else
	{
		estrConcatf(estr, "<a href=\"machinelock?accountid=%d&flag=1\">No</a> (Click to Toggle)\n", pAccount->uID);
	}
}

static void wiAppendAccountInfo(char **estr, AccountInfo *pAccount, int iAccountErrors)
{
	int i;
	char lastUpdatedStr[512];
	STRING_EARRAY eaProductNames;
	bool bOdd = false;
	EARRAY_OF(PurchaseLog) eaPurchaseLog;

	wiFormAppendStart(estr, "/detail", "GET", "view", NULL);
	estrConcatf(estr, "Account name: ");
	formAppendEdit(estr, 20, "accountname", pAccount ? pAccount->accountName : "");
	formAppendSubmit(estr, "View Account");
	formAppendEnd(estr);

	wiFormAppendStart(estr, "/detail", "GET", "view", NULL);
	estrConcatf(estr, "GUID: ");
	formAppendEdit(estr, 20, "guid", pAccount ? pAccount->globallyUniqueID : "");
	formAppendSubmit(estr, "View Account");
	formAppendEnd(estr);

	wiFormAppendStart(estr, "/detail", "GET", "view", NULL);
	estrConcatf(estr, "Account ID: ");
	formAppendEdit(estr, 20, "id", pAccount ? STACK_SPRINTF("%d", pAccount->uID) : "");
	formAppendSubmit(estr, "View Account");
	formAppendEnd(estr);
	
	if (pAccount)
		estrConcatf(estr, "<a href=\"http://%s:%u/viewxpath?xpath=AccountServer[1].globObj.Account[%u]\">[server monitor]</a>", getHostName(), giServerMonitorPort, pAccount->uID);

	if (pAccount)
	{
		HTML_HEADING(estr, "Billing Enabled");
		if (pAccount->bBillingEnabled) estrConcatf(estr, "Yes");
		else estrConcatf(estr, "No");

		HTML_HEADING(estr, "Activity Log");
		estrConcatf(estr, "<a href=\"activityLog?accountname=%s\">View Activity Log</a>\n", pAccount->accountName);
	}

	if (!pAccount) // TODO
		return;

	estrConcatf(estr, "<div class=\"heading\">Account Name</div>\n%s", pAccount->accountName);
	estrConcatf(estr, "<div class=\"heading\">Display Name</div>\n%s", pAccount->displayName);
	estrConcatf(estr, "<div class=\"heading\">Globally Unique ID</div>\n%s", pAccount->globallyUniqueID);

	appendAccountAddressForm(estr, pAccount, true);

	HTML_HEADING(estr, "Activated Product Keys");
	HTML_DESCRIPTION(estr, "These are keys that have been activated on this account.");
	for (i=0; i<eaSize(&pAccount->ppProductKeys); i++)
	{
		estrConcatf(estr, "<a href=\"keyView?key=%s\" class=\"key\">%s</a><br>\n", pAccount->ppProductKeys[i], pAccount->ppProductKeys[i]);
	}

	HTML_HEADING(estr, "Activate New Key");
	HTML_DESCRIPTION(estr, "Activate a product key on this account.");
	wiFormAppendStart(estr, "/activateKey", "POST", "activatekey", NULL);
	formAppendHiddenInt(estr, "accountID", pAccount->uID);
	formAppendEdit(estr, 20, "key", "");
	formAppendSubmit(estr, "Activate Key");
	formAppendEnd(estr);

	HTML_HEADING(estr, "Distributed Product Keys");
	HTML_DESCRIPTION(estr, "These are keys that have been distributed to this account.");
	EARRAY_CONST_FOREACH_BEGIN(pAccount->ppDistributedKeys, iCurDistKey, iNumDistKeys);
	{
		const DistributedKeyContainer *pDistributedKey = pAccount->ppDistributedKeys[iCurDistKey];

		if (!devassert(pDistributedKey)) continue;

		estrConcatf(estr, "<a href=\"keyView?key=%s\" class=\"key\">%s</a><br>\n", pDistributedKey->pActivationKey, pDistributedKey->pActivationKey);
	}
	EARRAY_FOREACH_END;

	HTML_HEADING(estr, "Distribute New Key");
	HTML_DESCRIPTION(estr, "Distribute a product key to this account.");
	wiFormAppendStart(estr, "/distributeKey", "POST", "distributekey", NULL);
	formAppendHiddenInt(estr, "accountID", pAccount->uID);
	formAppendEdit(estr, 20, "key", "");
	formAppendSubmit(estr, "Distribute Key");
	formAppendEnd(estr);

	HTML_HEADING(estr, "Flagged For Internal-Only Use?");
	HTML_DESCRIPTION(estr, "Accounts with the internal-only flag can only login from a trusted IP.");
	if (pAccount->bInternalUseLogin)
	{
		estrConcatf(estr, "<a href=\"internal?accountid=%d&internalflag=0\">Yes</a> (Click to Toggle)\n", 
			pAccount->uID);
	} else {
		estrConcatf(estr, "<a href=\"internal?accountid=%d&internalflag=1\">No</a> (Click to Toggle)\n", 
			pAccount->uID);
	}

	HTML_HEADING(estr, "Login Disabled?");
	HTML_DESCRIPTION(estr,
		"Accounts with login disabled will not be able to log in, will have no permission pairs, and will be flagged as disabled to the website.");
	if (pAccount->bLoginDisabled)
	{
		estrConcatf(estr, "<a href=\"disableLoginFlag?accountid=%d&disableloginflag=0\">Yes</a> (Click to Toggle)\n", 
			pAccount->uID);
	} else {
		estrConcatf(estr, "<a href=\"disableLoginFlag?accountid=%d&disableloginflag=1\">No</a> (Click to Toggle)\n", 
			pAccount->uID);
	}

	HTML_HEADING(estr, "Personal Info");
	 //TODO tweak html
	estrConcatf(estr, "<table border=1 cellpadding=3 cellspacing=0>\n"
		"<tr><td><div class=\"heading\">First Name</div>%s</td>\n"
		"<td><div class=\"heading\">Last Name</div>%s</td></tr>\n"
		"<tr><td colspan=2><div class=\"heading\">Email</div>%s</td></tr>\n"
		"</table>\n", 
		pAccount->personalInfo.firstName ? pAccount->personalInfo.firstName : "", 
		pAccount->personalInfo.lastName ? pAccount->personalInfo.lastName : "", 
		pAccount->personalInfo.email ? pAccount->personalInfo.email : "");

	HTML_HEADING(estr, "Payment Methods");
	HTML_DESCRIPTION(estr, "These payment methods are cached representations of what are stored in Vindicia.");
	appendFancyTableHeader(estr, 7, "Type", "VID", "Account Name", "E-mail Address", "Card Number", "Expiration Date", "Supported Actions");
	for (i=0; i<eaSize(&pAccount->personalInfo.ppPaymentMethods); i++)
	{
		const CachedPaymentMethod *pCachedPM = pAccount->personalInfo.ppPaymentMethods[i];
		static char *pCreditCardNumber = NULL;
		static char *pLink = NULL;

		if (pCachedPM->creditCard)
		{
			estrPrintf(&pCreditCardNumber, "%s-xxx-%s", pCachedPM->creditCard->bin, pCachedPM->creditCard->lastDigits);
			estrPrintf(&pLink, "<a href=\"editPaymentMethod?accountid=%d&pmid=%d\">Edit</a>", pAccount->uID, i);
		}

		appendFancyTableEntry(estr, i%2, 7,
			pCachedPM->creditCard ? "Credit Card" : pCachedPM->payPal ? "PayPal" : "Direct Debit",
			pCachedPM->VID,
			pCachedPM->accountName,
			pCachedPM->payPal ? (pCachedPM->payPal->emailAddress ? pCachedPM->payPal->emailAddress : "Not returned by Vindicia") : "N/A",
			pCachedPM->creditCard ? pCreditCardNumber : "N/A",
			pCachedPM->creditCard ? pCachedPM->creditCard->expireDate : "N/A",
			pCachedPM->creditCard ? pLink : "(edit not supported yet)"
			);
	}
	appendFancyTableFooter(estr);
	estrConcatf(estr, "<a href=\"editPaymentMethod?accountid=%d\">New Payment Method</a><br />", pAccount->uID);

	HTML_HEADING(estr, "Products");
	for (i=0; i<eaSize(&pAccount->ppProducts); i++)
	{
		char buffer[32];
		
		if (pAccount->ppProducts[i]->uAssociatedTimeSS2000)
			timeMakeLocalDateStringFromSecondsSince2000(buffer, pAccount->ppProducts[i]->uAssociatedTimeSS2000);

		estrConcatf(estr, "<a href=\"productDetail?product=%s\">%s</a> (%s) %s\n", pAccount->ppProducts[i]->name, pAccount->ppProducts[i]->name, 
			pAccount->ppProducts[i]->key ? pAccount->ppProducts[i]->key : "No Key",
			pAccount->ppProducts[i]->uAssociatedTimeSS2000 ? buffer : "");

		sprintf(buffer, "removeProduct_%d", i);
		wiFormAppendStart(estr, "/accountRemoveProduct", "POST", buffer, NULL);
		formAppendHiddenInt(estr, "id", pAccount->uID);
		formAppendHidden(estr, "product", pAccount->ppProducts[i]->name);
		formAppendSubmit(estr, "Remove");
		formAppendEnd(estr);

		estrConcatf(estr, "<br>\n");
	}

	HTML_HEADING(estr, "Activate Product");
	HTML_DESCRIPTION(estr, "Activate a product on an account, which may or may not associate it.");
	wiFormAppendStart(estr, "/accountAddProduct", "POST", "product", NULL);
	formAppendHiddenInt(estr, "id", pAccount->uID);
	formAppendEdit(estr, 20, "product", "");
	formAppendSubmit(estr, "Activate Product");
	formAppendEnd(estr);

	eaProductNames = getProductNameList(PRODUCTS_ASSOCIATIVE);
	if (eaProductNames)
	{
		// Remove products already associated from the list
		EARRAY_FOREACH_REVERSE_BEGIN(eaProductNames, j);
		{
			EARRAY_CONST_FOREACH_BEGIN(pAccount->ppProducts, k, size);
			{
				const AccountProductSub *pProduct = pAccount->ppProducts[k];

				if (!stricmp_safe(eaProductNames[j], pProduct->name))
				{
					char *pRemoved = eaRemoveFast(&eaProductNames, j);
					estrDestroy(&pRemoved);
				}
			}
			EARRAY_FOREACH_END;
		}
		EARRAY_FOREACH_END;

		// Sort alphabetically
		eaQSort(eaProductNames, strCmp);

		HTML_HEADING(estr, "Associate Product");
		HTML_DESCRIPTION(estr, "Same as the above, but with a nice drop-down listing products that can be associated with the account.");
		HTML_DESCRIPTION(estr, "If a product is not in this list, it may already be associated or it may be configured to not associate.");
		wiFormAppendStart(estr, "/accountAddProduct", "POST", "product", NULL);
		formAppendHiddenInt(estr, "id", pAccount->uID);
		formAppendSelectionStringValues(estr,
			"product", eaProductNames, eaProductNames, eaProductNames[0], eaSize(&eaProductNames));
		formAppendSubmit(estr, "Associate Product");
		formAppendEnd(estr);
		eaDestroyEString(&eaProductNames);
	}

	HTML_HEADING(estr, "Subscription Stats");
	appendFancyTableHeader(estr, 2, "Internal Product Name", "Subscribed Time");
	{
		SubHistoryStats *pStats = getAllSubStats(pAccount);

		if (pStats)
		{
			EARRAY_CONST_FOREACH_BEGIN(pStats->eaStats, iCurStat, iNumStats);
			{
				SubHistoryStat *pStat = pStats->eaStats[iCurStat];

				if (!devassert(pStat)) continue;

				appendFancyTableEntry(estr, iCurStat % 2, 2, pStat->pInternalProductName, GetPrettyDurationString(pStat->uTotalSeconds));
			}
			EARRAY_FOREACH_END;

			StructDestroy(parse_SubHistoryStats, pStats);
		}
	}
	appendFancyTableFooter(estr);

	if(pAccount->pCachedSubscriptionList)
	{
		timeMakeLocalDateStringFromSecondsSince2000_s(lastUpdatedStr, 512, pAccount->pCachedSubscriptionList->lastUpdatedSS2000);
	}
	else
	{
		strcpy(lastUpdatedStr, "Unknown");
	}

	wiAppendInternalSubscriptions(estr, pAccount->uID);

	HTML_HEADING(estr, "Cached Vindicia Subscriptions");
	if (pAccount)
	{
		estrConcatf(estr, "[<a href=\"refreshSubscriptionCache?name=%s\">Refresh</a>] (Last Updated: %s)", 
			pAccount->accountName, lastUpdatedStr);

		if (pAccount->eaExpectedSubs && eaSize(&pAccount->eaExpectedSubs) > 0)
		{
			estrConcatf(estr, "[<a href=\"removeExpected?name=%s\">Remove Expected Subs</a>]",
				pAccount->accountName);
		}
	}

	if(pAccount->pCachedSubscriptionList && (eaSize(&pAccount->pCachedSubscriptionList->ppList) > 0))
	{
		char expBuffer[512];
		char nextBillingDate[512];
		appendFancyTableHeader(estr, 8, "Sub Name", "Internal Name", "CC Last Digits", "CC Expiration", "VID", "Status", "Next Billing", "Game Card");
		EARRAY_FOREACH_BEGIN(pAccount->pCachedSubscriptionList->ppList, j);
		{
			CachedAccountSubscription *pCachedSub = pAccount->pCachedSubscriptionList->ppList[j];

			if(pCachedSub->nextBillingDateSS2000)
			{
				timeMakeLocalDateStringFromSecondsSince2000_s(nextBillingDate, 512, pCachedSub->nextBillingDateSS2000);
			}
			else
			{
				strcpy(nextBillingDate, "Unknown");
			}

			sprintf(expBuffer, "%d/%d", pCachedSub->creditCardExpirationMonth, pCachedSub->creditCardExpirationYear);
			appendFancyTableEntry(estr, j%2, 8, pCachedSub->name, 
				                                  pCachedSub->internalName, 
												  pCachedSub->creditCardLastDigits, 
												  expBuffer, 
												  pCachedSub->vindiciaID, 
												  subStatusToString(getCachedSubscriptionStatus(pCachedSub)),
												  nextBillingDate,
												  pCachedSub->gameCard ? "Yes" : "No");
		}
		EARRAY_FOREACH_END;
		appendFancyTableFooter(estr);
	}

	HTML_HEADING(estr, "Refunded Subscriptions");
	appendFancyTableHeader(estr, 2, "VID", "Refunded Date/Time");
	EARRAY_CONST_FOREACH_BEGIN(pAccount->ppRefundedSubscriptions, iRefund, size);
		const RefundedSubscription *pRefund = pAccount->ppRefundedSubscriptions[iRefund];

		if (pRefund)
		{
			char dateTime[64];

			timeMakeLocalDateStringFromSecondsSince2000(dateTime, pRefund->uRefundedSS2000);

			appendFancyTableEntry(estr, iRefund%2, 2, pRefund->pSubscriptionVID, dateTime);
		}
	EARRAY_FOREACH_END;
	appendFancyTableFooter(estr);

	wiAppendSubHistoryTable(estr, pAccount);

	if(pAccount->ppPermissionCache)
	{
		timeMakeLocalDateStringFromSecondsSince2000(lastUpdatedStr, pAccount->uPermissionCachedTimeSS2000);
	}
	else
	{
		strcpy(lastUpdatedStr, "Unknown");
	}

	HTML_HEADING(estr, "Cached Permissions");
	HTML_DESCRIPTION(estr, "These permissions are given by a combination of product and subscription and allow access to other systems.");
	estrConcatf(estr, "[<a href=\"refreshPermissionCache?name=%s\">Refresh</a>] (Last Updated: %s)\n",
		pAccount ? pAccount->accountName : "",
		lastUpdatedStr);
	appendFancyTableHeader(estr, 3, "Product", "String", "Access Level");
	for (i=0; i<eaSize(&pAccount->ppPermissionCache); i++)
	{
		static char *pAccessLevel = NULL;

		estrPrintf(&pAccessLevel, "%d", pAccount->ppPermissionCache[i]->iAccessLevel);

		appendFancyTableEntry(estr, i%2, 3,
			pAccount->ppPermissionCache[i]->pProductName,
			pAccount->ppPermissionCache[i]->pPermissionString, 
			pAccessLevel);
	}
	appendFancyTableFooter(estr);

	HTML_HEADING(estr, "Playtimes");
	HTML_DESCRIPTION(estr, "This is a list of known playtime by product and shard.");
	appendFancyTableHeader(estr, 3, "Internal Product Name", "Shard Category", "Total Play Time");
	for (i=0; i<eaSize(&pAccount->ppGameMetadata); i++)
	{
		char* timeString = NULL;
		timeSecondsDurationToPrettyEString(pAccount->ppGameMetadata[i]->playtime.uPlayTime, &timeString);
		appendFancyTableEntry(estr, i%2, 3,
			pAccount->ppGameMetadata[i]->product, 
			pAccount->ppGameMetadata[i]->shard, 
			timeString);
		estrDestroy(&timeString);
	}
	appendFancyTableFooter(estr);
	
	wiAppendAccountInfo_KeyValues(estr, pAccount);

	// Secret questions
	HTML_HEADING(estr, "Account Secret Questions");
	HTML_DESCRIPTION(estr, "These secret questions each have a corresponding one-way hashed answer.");
	if (eaSize(&pAccount->personalInfo.secretQuestionsAnswers.questions))
	{
		estrAppend2(estr, "<ol>\n");
		EARRAY_FOREACH_BEGIN(pAccount->personalInfo.secretQuestionsAnswers.questions, j);
		estrConcatf(estr, "<li>%s</li>", pAccount->personalInfo.secretQuestionsAnswers.questions[j]);
		EARRAY_FOREACH_END;
		estrAppend2(estr, "</ol>\n");
	}
	else
	{
		estrAppend2(estr, "<p><i>None</i></p>");
	}

	HTML_HEADING(estr, "Purchases");
	HTML_DESCRIPTION(estr, "This is the 100 most recent purchases.");
	eaPurchaseLog = AccountTransactionGetPurchaseLog(0, 100, pAccount->uID);
	appendFancyTableHeader(estr, 8, "Time", "Product", "Price", "Currency", "Transaction ID", "Order ID", "Provider", "Token Key-value Changes");
	EARRAY_CONST_FOREACH_BEGIN(eaPurchaseLog, iCurLogEntry, n);
	{
		const PurchaseLog *pPurchaseLog = eaPurchaseLog[iCurLogEntry];
		char pPurchaseTime[512];
		char *pKeyValueChangeString = NULL;

		timeMakeLocalDateStringFromSecondsSince2000_s(pPurchaseTime, sizeof(pPurchaseTime), pPurchaseLog->uTimestampSS2000);

		EARRAY_CONST_FOREACH_BEGIN(pPurchaseLog->eaKeyValues, iCurKeyValue, iNumKeyValues);
		{
			const ProductKeyValueChangeContainer *pKeyValueChange = pPurchaseLog->eaKeyValues[iCurKeyValue];
			estrAppend2(&pKeyValueChangeString, pKeyValueChange->pKey);

			if (pKeyValueChange->change)
			{
				estrAppend2(&pKeyValueChangeString, " += ");
			}
			else
			{
				estrAppend2(&pKeyValueChangeString, " = ");
			}

			estrAppend2(&pKeyValueChangeString, pKeyValueChange->pValue);

			estrConcatStatic(&pKeyValueChangeString, "; ");
		}
		EARRAY_FOREACH_END;

		appendFancyTableEntry(estr, iCurLogEntry%2, 8,
			pPurchaseTime,
			pPurchaseLog->pProduct, 
			pPurchaseLog->pPrice, 
			pPurchaseLog->pCurrency,
			pPurchaseLog->pMerchantTransactionId,
			pPurchaseLog->pOrderID,
			StaticDefineIntRevLookupNonNull(TransactionProviderEnum, pPurchaseLog->eProvider),
			pKeyValueChangeString);

		estrDestroy(&pKeyValueChangeString);
	}
	EARRAY_FOREACH_END;
	appendFancyTableFooter(estr);

	wiAppendMachineLocking(estr, pAccount);

	eaDestroyStruct(&eaPurchaseLog, parse_PurchaseLog);
}

static void wiAppendAccountActivityLog(char **estr, const AccountInfo *pAccount)
{
	EARRAY_OF(const AccountLogEntry) eaEntries = NULL;

	if (!pAccount) return;

	accountGetLogEntries(pAccount, &eaEntries, 0, 0);

	appendPaginatedFancyTableHeader(estr, 2, "Time", "Message");
	EARRAY_CONST_FOREACH_BEGIN(eaEntries, i, s);
	{
		char timeString[256];
		char *html = wikiToHTML(eaEntries[i]->pMessage);
		timeMakeLocalDateStringFromSecondsSince2000(timeString, eaEntries[i]->uSecondsSince2000);
		appendFancyTableEntry(estr, i%2, 2, timeString, html);
		estrDestroy(&html);
	}
	EARRAY_FOREACH_END;
	appendFancyTableFooter(estr);

	eaDestroy(&eaEntries);
}

static void wiRefreshPermissionCache(DEFAULT_HTTPGET_PARAMS)
{
	int i;
	char *pAccountName = NULL;
	AccountInfo *pAccount = NULL;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "name") == 0)
		{
			pAccountName = values[i];
		}
	}

	pAccount = findAccountByName(pAccountName);
	if(pAccount)
	{
		accountClearPermissionsCache (pAccount);
		accountConstructPermissions (pAccount);
	}

	if(pAccountName)
	{
		wiRedirectf(link,  "/detail?accountname=%s", pAccountName);
	}
	else
	{
		wiRedirectf(link, "/detail");
	}
}


static void wiRefreshSubscriptionCache(DEFAULT_HTTPGET_PARAMS)
{
	int i;
	char *pAccountName = NULL;
	AccountInfo *pAccount = NULL;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "name") == 0)
		{
			pAccountName = values[i];
		}
	}

	pAccount = findAccountByName(pAccountName);
	if(pAccount)
	{
		btUpdateActiveSubscriptions(pAccount->uID, NULL, NULL, NULL);
	}

	if(pAccountName)
	{
		wiRedirectf(link, "/detail?accountname=%s", pAccountName);
	}
	else
	{
		wiRedirectf(link, "/detail");
	}
}

static void wiRemoveExpected(DEFAULT_HTTPGET_PARAMS)
{
	int i;
	char *pAccountName = NULL;
	AccountInfo *pAccount = NULL;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "name") == 0)
		{
			pAccountName = values[i];
		}
	}

	pAccount = findAccountByName(pAccountName);
	if(pAccount)
	{
		EARRAY_CONST_FOREACH_BEGIN(pAccount->eaExpectedSubs, iCurExpectedSub, iNumExpected);
		{
			accountRemoveExpectedSub(pAccount, pAccount->eaExpectedSubs[iCurExpectedSub]->pInternalName);
		}
		EARRAY_FOREACH_END;
	}

	if(pAccountName)
	{
		wiRedirectf(link, "/detail?accountname=%s", pAccountName);
	}
	else
	{
		wiRedirectf(link, "/detail");
	}
}

static void wiAccountDetailBlank(DEFAULT_HTTPGET_PARAMS)
{
	static char *s = NULL;
	int i;
	AccountInfo *pAccount = NULL;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "accountname") == 0)
		{
			pAccount = findAccountByName(values[i]);
		}
		if (stricmp(args[i], "guid") == 0)
		{
			pAccount = findAccountByGUID(values[i]);
		}
		if (stricmp(args[i], "id") == 0)
		{
			pAccount = findAccountByID(atoi(values[i]));
		}
	}
	

	estrCopy2(&s, "");
	wiAppendAccountInfo(&s, pAccount, 0);
	httpSendWrappedString(link, s, pCookies);
}

static void wiAccountActivityLog(DEFAULT_HTTPGET_PARAMS)
{
	static char *s = NULL;
	int i;
	char *pAccountName = NULL;
	const AccountInfo *pAccount = NULL;

	for (i = 0; i < count; i++)
	{
		if (stricmp(args[i], "accountname") == 0)
		{
			pAccountName = values[i];
		}
	}
	pAccount = findAccountByName(pAccountName);
	estrClear(&s);
	wiAppendAccountActivityLog(&s, pAccount);
	httpSendWrappedString(link, s, pCookies);
}

static void wiAccountDetail(DEFAULT_HTTPPOST_PARAMS)
{
	static char *s = NULL;
	const char *pAccountName = urlFindValue(args, "accountname");
	Container *con;
	
	estrCopy2(&s, "");
	if (!pAccountName)
	{
		wiAppendAccountInfo(&s, NULL, 0);
		httpSendWrappedString(link, s, pCookies);
		return;
	}

	con = findAccountContainerByName(pAccountName);
	if (!con || !con->containerData)
	{
		wiAppendAccountInfo(&s, NULL, ACCOUNT_PARAM_INVALID);
	}
	else
	{
		wiAppendAccountInfo(&s, (AccountInfo*) con->containerData, 0);
	}
	httpSendWrappedString(link, s, pCookies);
}

static void wiAppendSubHistory(SA_PRE_NN_NN_VALID char **estr,
							   SA_PARAM_NN_VALID const AccountInfo *pAccount,
							   SA_PARAM_NN_VALID const SubscriptionHistory *pHistory,
							   SA_PARAM_NN_VALID const SubscriptionHistoryEntry *pEntry)
{
	HTML_HEADING(estr, "Account Name");
	HTML_DESCRIPTION(estr, "The account to which this entry belongs.");
	estrConcatf(estr, "<a href=\"detail?accountname=%s\">%s</a>", pAccount->accountName, pAccount->accountName);

	wiAppendSubHistoryTable(estr, pAccount);

	HTML_HEADING(estr, "Product Internal Name");
	HTML_DESCRIPTION(estr, "The product internal name this entry granted access to.");
	estrConcatf(estr, "%s", pHistory->pProductInternalName);

	HTML_HEADING(estr, "Entry ID");
	HTML_DESCRIPTION(estr, "ID of this entry.");
	estrConcatf(estr, "%d", pEntry->uID);

	HTML_HEADING(estr, "Entry Reason");
	HTML_DESCRIPTION(estr, "Why this entry was created.");
	estrConcatf(estr, "%s", StaticDefineIntRevLookupNonNull(SubscriptionHistoryEntryReasonEnum, pEntry->eReason));

	HTML_HEADING(estr, "Enabled");
	HTML_DESCRIPTION(estr, "Whether or not this entry contributes to total calculated times.");
	estrConcatf(estr, "%s ", pEntry->bEnabled ? "Yes" : "No");
	estrConcatf(estr, "[<a href=\"toggleSubHistory?account=%d&product=%s&id=%d\">Toggle</a>]",
		pAccount->uID, pHistory->pProductInternalName, pEntry->uID);

	HTML_HEADING(estr, "Adjusted Duration");
	HTML_DESCRIPTION(estr, "Adjusted duration of this entry.");
	estrConcatf(estr, "%s", GetPrettyDurationString(subHistoryEntrySeconds(pEntry)));

	HTML_HEADING(estr, "Adjusted Start Time");
	HTML_DESCRIPTION(estr, "Start time adjusted to remove overlaps with other entries.");
	estrConcatf(estr, "%s", timeGetLocalDateStringFromSecondsSince2000(pEntry->uAdjustedStartSS2000));
		
	HTML_HEADING(estr, "Adjusted End Time");
	HTML_DESCRIPTION(estr, "End time adjusted to remove overlaps with other entries.");
	estrConcatf(estr, "%s", timeGetLocalDateStringFromSecondsSince2000(pEntry->uAdjustedEndSS2000));

	HTML_HEADING(estr, "Actual Start Time");
	HTML_DESCRIPTION(estr, "Actual start time of this entry.");
	estrConcatf(estr, "%s", timeGetLocalDateStringFromSecondsSince2000(pEntry->uStartTimeSS2000));

	HTML_HEADING(estr, "Actual End Time");
	HTML_DESCRIPTION(estr, "Actual end time of this entry.");
	estrConcatf(estr, "%s", timeGetLocalDateStringFromSecondsSince2000(pEntry->uEndTimeSS2000));

	HTML_HEADING(estr, "Calculated Time");
	HTML_DESCRIPTION(estr, "When the adjusted times were last calculated.");
	estrConcatf(estr, "%s", timeGetLocalDateStringFromSecondsSince2000(pEntry->uLastCalculatedSS2000));

	HTML_HEADING(estr, "Archived Time");
	HTML_DESCRIPTION(estr, "When this entry was added to the archive.");
	estrConcatf(estr, "%s", timeGetLocalDateStringFromSecondsSince2000(pEntry->uCreatedSS2000));

	HTML_HEADING(estr, "Possible Problems");
	if (!pEntry->uProblemFlags)
	{
		estrConcatf(estr, "None");
	}
	else
	{
		if (pEntry->uProblemFlags & SHEP_NOT_EXACT)
		{
			estrConcatf(estr, "Not Exact<br />");
		}
		if (pEntry->uProblemFlags & SHEP_OVERLAPS)
		{
			estrConcatf(estr, "Overlaps<br />");
		}
	}

	HTML_HEADING(estr, "Subscription Internal Name");
	HTML_DESCRIPTION(estr, "Internal name of the subscription that gave access for this entry.");
	estrConcatf(estr, "%s", pEntry->pSubInternalName);

	HTML_HEADING(estr, "Subscription VID");
	HTML_DESCRIPTION(estr, "VID of the subscription that gave access for this entry.");
	estrConcatf(estr, "%s", pEntry->pSubscriptionVID ? pEntry->pSubscriptionVID : "Internal Subscription");

	HTML_HEADING(estr, "Source");
	HTML_DESCRIPTION(estr, "Source of the entry.");
	estrConcatf(estr, "%s", StaticDefineIntRevLookupNonNull(SubscriptionTimeSourceEnum, pEntry->eSubTimeSource));
}

static void wiSubHistory(DEFAULT_HTTPGET_PARAMS)
{
	U32 uAccountID = 0;
	U32 uID = 0;
	const char *pProductInternalName = NULL;
	int i;
	const AccountInfo *pAccount;
	const SubscriptionHistory *pHistory;
	const SubscriptionHistoryEntry *pEntry;
	static char *s = NULL;

	for (i = 0; i < count; i++)
	{
		if (stricmp(args[i], "account") == 0)
		{
			uAccountID = atoi(values[i]);
		}
		else if(stricmp(args[i], "product") == 0)
		{
			pProductInternalName = values[i];
		}
		else if(stricmp(args[i], "id") == 0)
		{
			uID = atoi(values[i]);
		}
	}

	pAccount = findAccountByID(uAccountID);

	if (!pAccount || !uID || !pProductInternalName)
	{
		wiRedirectf(link, "/");
		return;
	}

	i = eaIndexedFindUsingString(&pAccount->ppSubscriptionHistory, pProductInternalName);

	if (i < 0)
	{
		wiRedirectf(link, "/");
		return;
	}

	pHistory = pAccount->ppSubscriptionHistory[i];

	i = eaIndexedFindUsingInt(&pHistory->eaArchivedEntries, uID);

	if (i < 0)
	{
		wiRedirectf(link, "/");
		return;
	}

	pEntry = pHistory->eaArchivedEntries[i];

	estrClear(&s);

	wiAppendSubHistory(&s, pAccount, pHistory, pEntry);

	httpSendWrappedString(link, s, pCookies);
}

static void wiRecalculateSubHistory(DEFAULT_HTTPGET_PARAMS)
{
	U32 uAccountID = 0;
	int i;
	const AccountInfo *pAccount;

	for (i = 0; i < count; i++)
	{
		if (stricmp(args[i], "account") == 0)
		{
			uAccountID = atoi(values[i]);
		}
	}

	pAccount = findAccountByID(uAccountID);

	if (!pAccount)
	{
		wiRedirectf(link, "/");
		return;
	}

	accountRecalculateAllArchivedSubHistory(pAccount->uID);

	wiRedirectf(link, pReferer);
}

static void wiToggleSubHistory(DEFAULT_HTTPGET_PARAMS)
{
	U32 uAccountID = 0;
	U32 uID = 0;
	const char *pProductInternalName = NULL;
	int i;
	const AccountInfo *pAccount;
	const SubscriptionHistory *pHistory;
	const SubscriptionHistoryEntry *pEntry;

	for (i = 0; i < count; i++)
	{
		if (stricmp(args[i], "account") == 0)
		{
			uAccountID = atoi(values[i]);
		}
		else if(stricmp(args[i], "product") == 0)
		{
			pProductInternalName = values[i];
		}
		else if(stricmp(args[i], "id") == 0)
		{
			uID = atoi(values[i]);
		}
	}

	pAccount = findAccountByID(uAccountID);

	if (!pAccount || !uID || !pProductInternalName)
	{
		wiRedirectf(link, "/");
		return;
	}

	i = eaIndexedFindUsingString(&pAccount->ppSubscriptionHistory, pProductInternalName);

	if (i < 0)
	{
		wiRedirectf(link, "/");
		return;
	}

	pHistory = pAccount->ppSubscriptionHistory[i];

	i = eaIndexedFindUsingInt(&pHistory->eaArchivedEntries, uID);

	if (i < 0)
	{
		wiRedirectf(link, "/");
		return;
	}

	pEntry = pHistory->eaArchivedEntries[i];

	accountEnableArchivedSubHistory(pAccount->uID, pProductInternalName, uID, !pEntry->bEnabled);

	wiRedirectf(link, pReferer);
}


// Helper macro for wiStats()
#define STATS_EMPTY_TO_NONE(STR) (STR && *STR ? STR : "<i>none</i>")

// Helper function for wiStats()
static void wiHtmlizeKeyActivations(char **s, U64Container *const *ppKeyActivations, U32 uStarted)
{
	char started[512];

	// Print starting time.
	if (uStarted)
	{
		timeMakeLocalDateStringFromSecondsSince2000_s(started, sizeof(started), uStarted);
		estrConcatf(s, "<p><b>Started:</b> %s</p>\n", started);
	}

	// Print activations table.
	estrAppend2(s, "<table border=\"1\"><tr><td><b>Batch</b></td><td><b>Keys Activated</b></td></tr>\n");
	EARRAY_CONST_FOREACH_BEGIN(ppKeyActivations, i, n);
		if (ppKeyActivations[i] && ppKeyActivations[i]->uValue)
		{
			ProductKeyBatch *batch = getKeyBatchByID(i);
			if (batch)
				estrConcatf(s, "<tr><td><a href=\"batchView?id=%d\">%s</a></td><td>%"FORM_LL"d</td></tr>\n", i,
					batch->pBatchName, ppKeyActivations[i]->uValue);
		}
	EARRAY_FOREACH_END;
	estrAppend2(s, "</table>\n");
}

// Helper function for wiStats()
static void wiHtmlizePurchases(char **s, CONST_EARRAY_OF(CurrencyPurchases) ppPurchases, U32 uStarted)
{
	char started[512];
	if (uStarted)
	{
		timeMakeLocalDateStringFromSecondsSince2000_s(started, sizeof(started), uStarted);
		estrConcatf(s, "<p><b>Started:</b> %s</p>\n", started);
	}
	estrAppend2(s, "<table border=\"1\"><tr><td><b>Currency</b></td><td><b>Locale</b></td><td><b>Product</b></td>"
		"<td><b>Number of Purchases</b></td><td><b>Monetary Total of Purchases</b></td></tr>\n");
	EARRAY_CONST_FOREACH_BEGIN(ppPurchases, i, n);
		const CurrencyPurchases *purchase = ppPurchases[i];
		estrConcatf(s, "<tr><td>%s</td><td>%s</td><td>%s</td><td>%"FORM_LL"d</td><td>%s</td></tr>\n", purchase->Currency,
			STATS_EMPTY_TO_NONE(purchase->Locale), purchase->Product, purchase->PurchaseCount, purchase->TotalPurchases);
	EARRAY_FOREACH_END;
	estrAppend2(s, "</table>\n");
}

// Format the stats information nicely, along with some helpful documentation.
static void wiStats(DEFAULT_HTTPGET_PARAMS)
{
	char *s = NULL;
	AccountStats *stats = GetAccountStats();
	char *buffer = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Print heading.
	estrStackCreate(&s);
	estrCopy2(&s, "<p>Note that if statistics have been disabled, some figures may be inaccurate or inconsistent."
		"  This can be corrected by rescanning or resetting the Account Server statistics.</p>\n");
	if (!stats->enabled)
		estrAppend2(&s, "<h1>Warning: Live statistics reporting is disabled.</h1>");
	estrAppend2(&s, "<table border=\"1\"><tr><td><b>Parameter</b></td><td><b>Description</b></td><td><b>Value</b></td></tr>\n");

// Table line formatting helpers.
#define STATS_TABLE_HEADING(TEXT) estrAppend2(&s, "<tr><td colspan=\"3\"><b>" TEXT "</b></td></tr>\n")
#define STATS_TABLE_LINE_EXPR(VARIABLE, DESCRIPTION, FORMAT, EXPR)																			\
	estrConcatf(&s, "<tr><td>" STRINGIZE(VARIABLE) "</td><td>" DESCRIPTION "</td><td>" FORMAT "</td></tr>\n", EXPR)
#define STATS_TABLE_LINE(VARIABLE, DESCRIPTION, FORMAT) \
	STATS_TABLE_LINE_EXPR(VARIABLE, DESCRIPTION, FORMAT, stats->VARIABLE)
#define STATS_TABLE_LINE_BOOL(VARIABLE, DESCRIPTION)																						\
	STATS_TABLE_LINE_EXPR(VARIABLE, DESCRIPTION, "%s", stats->VARIABLE ? "true" : "false")
#define STATS_TABLE_LINE_STR(VARIABLE, DESCRIPTION)																							\
		STATS_TABLE_LINE_EXPR(VARIABLE, DESCRIPTION, "%s", STATS_EMPTY_TO_NONE(stats->VARIABLE))
#define STATS_TABLE_LINE_S2000(VARIABLE, DESCRIPTION) do {char strTime[512];																\
	timeMakeLocalDateStringFromSecondsSince2000_s(strTime, sizeof(strTime), stats->VARIABLE);												\
	STATS_TABLE_LINE_EXPR(VARIABLE, DESCRIPTION, "%s", strTime);} while(0)
#define STATS_TABLE_LINE_SUB(SUBSTRUCT, VARIABLE, DESCRIPTION, FORMAT)																		\
	STATS_TABLE_LINE_EXPR(VARIABLE, DESCRIPTION, FORMAT, stats->SUBSTRUCT->VARIABLE)
#define STATS_TABLE_LINE_SUB_BOOL(SUBSTRUCT, VARIABLE, DESCRIPTION)																			\
		STATS_TABLE_LINE_EXPR(VARIABLE, DESCRIPTION, "%s", stats->SUBSTRUCT->VARIABLE ? "true" : "false")
#define STATS_TABLE_LINE_SUB_STR(SUBSTRUCT, VARIABLE, DESCRIPTION)																			\
	STATS_TABLE_LINE_EXPR(VARIABLE, DESCRIPTION, "%s", STATS_EMPTY_TO_NONE(stats->SUBSTRUCT->VARIABLE))
#define STATS_TABLE_LINE_SUB_S2000(SUBSTRUCT, VARIABLE, DESCRIPTION) do {char strTime[512];													\
	timeMakeLocalDateStringFromSecondsSince2000_s(strTime, sizeof(strTime), stats->SUBSTRUCT->VARIABLE);									\
	STATS_TABLE_LINE_EXPR(VARIABLE, DESCRIPTION, "%s", stats->SUBSTRUCT->VARIABLE ? strTime : "");} while(0)

	// General statistics
	STATS_TABLE_HEADING("General Account Server Statistics");
	STATS_TABLE_LINE_BOOL(enabled, "Reporting status; This is false if reporting has been disabled");
	STATS_TABLE_LINE(numAccounts, "Total number of accounts known to Account Server", "%lu");
	STATS_TABLE_LINE(SlowLogins, "Count of old-style slow logins since the Account Server has been started (obsolete)", "%d");

	// Build information
	STATS_TABLE_HEADING("Build Information");
	STATS_TABLE_LINE_STR(AppType, "Global application type, should be AccountServer");
	STATS_TABLE_LINE_STR(ProductionVersion, "Production builder patch version of AccountServer");
	STATS_TABLE_LINE_STR(UsefulVersion, "\"Useful\" version string, same as production version for production builds");
	STATS_TABLE_LINE(BuildVersion, "AccountServer SVN build version", "%d");
	STATS_TABLE_LINE_BOOL(BuildFullDebug, "Built with full debugging support");
	STATS_TABLE_LINE_STR(BuildBranch, "AccountServer build branch");
	STATS_TABLE_LINE_BOOL(BuildWin64Bit, "Built for Windows 64-bit");
	STATS_TABLE_LINE(ProxyProtocolVersion, "Proxy protocol version", "%d");

	// Billing settings
	STATS_TABLE_HEADING("Configuration Settings");
	STATS_TABLE_LINE_EXPR(ProductionMode, "Server Mode", "%s", stats->ProductionMode ? "Production" : "Development");
	if (!stats->BillingConfig)
		stats->BillingConfig = StructCreate(parse_BillingConfiguration);
	STATS_TABLE_LINE_EXPR(serverType, "Server Type", "%s",
		stats->BillingConfig->serverType == BillingServerType_Official ? "Official"
		: (stats->BillingConfig->serverType == BillingServerType_Development ? "Development" : "Unknown"));
	STATS_TABLE_LINE_SUB_STR(BillingConfig, prefix, "String prefix for all names sent to Vindicia.  This should be empty for the official server.");
	STATS_TABLE_LINE_SUB(BillingConfig, connectTimeoutSeconds, "Time to wait for connections to Vindicia to open before giving up (seconds)", "%lu");
	STATS_TABLE_LINE_SUB(BillingConfig, connectAlertSeconds, "Time to wait for a Vindicia connection to open before alerting (seconds)", "%lu");
	STATS_TABLE_LINE_SUB(BillingConfig, replyTimeoutSeconds, "Time to wait for a reply from Vindicia before giving up (seconds)", "%lu");
	STATS_TABLE_LINE_SUB(BillingConfig, replyAlertSeconds, "Time to wait for a reply from Vindicia before alerting (seconds)", "%lu");
	STATS_TABLE_LINE_SUB(BillingConfig, completedMaxAgeSeconds, "Time for which web transactions will persist to allow the website to query "
		"their result (seconds)", "%lu");
	STATS_TABLE_LINE_EXPR(authFailThreshold, "Minimum ratio of billing authorization failures before tiggering an alert (percent)",
		"%f%%", stats->BillingConfig->authFailThreshold * 100);
	STATS_TABLE_LINE_SUB(BillingConfig, idleTransactionsQueuedOnStartup, "", "%lu");
	STATS_TABLE_LINE_SUB(BillingConfig, idleTransactionCountLimit, "", "%lu");
	STATS_TABLE_LINE_SUB(BillingConfig, idleTransactionsSkipNonBillingAccounts, "", "%lu");
	STATS_TABLE_LINE_SUB(BillingConfig, debug, "1 if debug mode is enabled; 0 if debug mode is disabled", "%lu");
	STATS_TABLE_LINE_SUB_STR(BillingConfig, validationProduct, "The name of the product used for payment method validation");
	STATS_TABLE_LINE_SUB(BillingConfig, deactivatePaymentMethodTries, "", "%lu");
	STATS_TABLE_LINE_SUB(BillingConfig, nightlyProcessingHour, "", "%lu");
	STATS_TABLE_LINE_SUB_STR(BillingConfig, vindiciaLogin, "Username used to login to Vindicia");
	// Vindicia password is not printed for security reasons.
	STATS_TABLE_LINE_SUB_STR(BillingConfig, stunnelHost, "Hostname of Vindicia (This is probably the name of the machine running stunnel.)");
	STATS_TABLE_LINE_EXPR(nightlyMode, "", "%s", StaticDefineIntRevLookupNonNull(BillingNightlyModeEnum, stats->BillingConfig->nightlyMode));
	STATS_TABLE_LINE_SUB(BillingConfig, vindiciaLogLevel, "Logging level for Vindicia activity", "%d");
	// fraudSettings omitted for their general verbosity
	estrStackCreate(&buffer);
	EARRAY_CONST_FOREACH_BEGIN(stats->BillingConfig->PurchaseLogKeys, i, n);
		if (i)
			estrAppend2(&buffer, ", ");
		estrAppend2(&buffer, stats->BillingConfig->PurchaseLogKeys[i]);
	EARRAY_FOREACH_END;
	STATS_TABLE_LINE_EXPR(PurchaseLogKeys, "List of keys to log purchase information about, on the assumption that they are a points type", "%s", buffer);

	// Instantaneously-calculated performance statistics
	STATS_TABLE_HEADING("Instantaneously-Calculated Performance Statistics");
	STATS_TABLE_LINE(LoginsLast60Secs, "Number of logins in last 60.0 seconds", "%d");
	STATS_TABLE_LINE(Last10000Logins, "Last 10000 logins took (seconds)", "%f");
	STATS_TABLE_LINE(MostLoginsInSec, "Most logins in any 1.0 seconds (count)", "%lu");
	STATS_TABLE_LINE(ShortestTime100Logins, "Shortest time for any 100 logins (seconds)", "%f");
	STATS_TABLE_LINE(UniqueLoginsHourlyCurrent, "Unique logins in the past hour (count)", "%lu");
	STATS_TABLE_LINE(UniqueLoginsHourlyPrevious, "Unique logins in the past hour (count)", "%lu");
	STATS_TABLE_LINE(QueuedTransactionCount, "Number of queued transactions", "%d");
	STATS_TABLE_LINE(IdleTransactionCount, "Number of idle transactions", "%d");
	STATS_TABLE_LINE(ActiveTransactionCount, "Number of active transactions", "%d");
	STATS_TABLE_LINE(CompletedTransactionCount, "When did we last fill spAccountIDUpdateQueue?", "%d");
	STATS_TABLE_LINE_S2000(LastIdleStart, "When did spAccountIDUpdateQueue completely empty last?");
	STATS_TABLE_LINE_S2000(LastIdleComplete, "Last idle completion time");

	// Tracked performance statistics
	STATS_TABLE_HEADING("Tracked Performance Statistics");
	if (!stats->global)
		stats->global = StructCreate(parse_GlobalAccountStatsContainer);
	STATS_TABLE_LINE_SUB_S2000(global, LastReset, "Time of last statistics reset");
	STATS_TABLE_LINE_SUB_S2000(global, LastEnabled, "Last time statistics reporting was enabled");
	STATS_TABLE_LINE_SUB_S2000(global, LastDisabled, "Last time statistics reporting was disabled.");
	STATS_TABLE_LINE_SUB_S2000(global, HourlyCurrentStart, "Start of today's hourly reporting period");
	STATS_TABLE_LINE_SUB_S2000(global, HourlyPreviousStart, "Start of yesterday's hourly reporting period");
	STATS_TABLE_LINE_SUB_S2000(global, DailyCurrentStart, "Start of today's daily reporting period");
	STATS_TABLE_LINE_SUB_S2000(global, DailyPreviousStart, "Start of yesterday's daily reporting period");
	STATS_TABLE_LINE_SUB(global, TotalPurchases, "Count of total purchases transactions, since the last reset", "%"FORM_LL"d");
	STATS_TABLE_LINE_SUB(global, TotalLogins, "Total number of logins, since last reset", "%"FORM_LL"d");

	// Fetch delta stats
	STATS_TABLE_HEADING("Fetch Delta Statistics (AutoBills)");
	STATS_TABLE_LINE_SUB_S2000(global, AutoBillFetchDelta.Time, "Fetched Since");
	STATS_TABLE_LINE_SUB(global, AutoBillFetchDelta.NumProcessed, "Number Processed", "%"FORM_LL"u");
	STATS_TABLE_LINE_SUB(global, AutoBillFetchDelta.NumTotal, "Number Received", "%"FORM_LL"u");
	STATS_TABLE_LINE_SUB_S2000(global, AutoBillFetchDelta.StartedTime, "Started Time");
	STATS_TABLE_LINE_SUB(global, AutoBillFetchDelta.PageSize, "Page Size", "%"FORM_LL"u");

	STATS_TABLE_HEADING("Fetch Delta Statistics (Entitlements)");
	STATS_TABLE_LINE_SUB_S2000(global, EntitlementFetchDelta.Time, "Fetched Since");
	STATS_TABLE_LINE_SUB(global, EntitlementFetchDelta.NumProcessed, "Number Processed", "%"FORM_LL"u");
	STATS_TABLE_LINE_SUB(global, EntitlementFetchDelta.NumTotal, "Number Received", "%"FORM_LL"u");
	STATS_TABLE_LINE_SUB_S2000(global, EntitlementFetchDelta.StartedTime, "Started Time");
	STATS_TABLE_LINE_SUB(global, EntitlementFetchDelta.PageSize, "Page Size", "%"FORM_LL"u");

	STATS_TABLE_HEADING("Perfect World Updates");
	STATS_TABLE_LINE(PerfectWorldUsefulUpdates, "Hourly Useful Updates", "%d");
	STATS_TABLE_LINE(PerfectWorldUselessUpdates, "Hourly Useless Updates", "%d");
	STATS_TABLE_LINE(PerfectWorldFailedUpdates, "Hourly Failed Updates", "%d");

	// Finish general stats table
	estrAppend2(&s, "</table>\n");
#undef STATS_TABLE_HEADING
#undef STATS_TABLE_LINE_EXPR
#undef STATS_TABLE_LINE
#undef STATS_TABLE_LINE_BOOL
#undef STATS_TABLE_LINE_S2000
#undef STATS_TABLE_LINE_SUB
#undef STATS_TABLE_LINE_SUB_BOOL
#undef STATS_TABLE_LINE_SUB_S2000

	// Activated keys
	HTML_HEADING(&s, "Keys Activations");
	HTML_DESCRIPTION(&s, "The total number of keys activated, by batch, since the last reset");
	wiHtmlizeKeyActivations(&s, stats->KeyActivations, 0);
	HTML_HEADING(&s, "Activated Keys (Today)");
	wiHtmlizeKeyActivations(&s, stats->KeyActivationsDailyCurrent, stats->global->DailyCurrentStart);
	HTML_HEADING(&s, "Activated Keys (Yesterday)");
	wiHtmlizeKeyActivations(&s, stats->KeyActivationsDailyPrevious, stats->global->DailyPreviousStart);

	// New and used keys, by batch
	HTML_HEADING(&s, "Available and Activated Keys");
	HTML_DESCRIPTION(&s, "For each batch, the number of available (new) and activated (used) keys");
	estrAppend2(&s, "<table border=\"1\"><tr><td><b>Batch</b></td><td><b>Available</b></td>"
		"<td><b>Activated</b></td></tr>\n");
	EARRAY_CONST_FOREACH_BEGIN(stats->KeyCounts, i, n);
		const BatchKeyCounts *counts = stats->KeyCounts[i];
		if (counts && (counts->ActivatedKeys || counts->AvailableKeys))
		{
			ProductKeyBatch *batch = getKeyBatchByID(i);
			if (batch)
				estrConcatf(&s, "<tr><td><a href=\"batchView?id=%d\">%s</a></td><td>%"FORM_LL"u</td><td>%"FORM_LL"u</td></tr>\n", i,
					batch->pBatchName, counts->AvailableKeys, counts->ActivatedKeys);
		}
	EARRAY_FOREACH_END;
	estrAppend2(&s, "</table>\n");

	// Play time table
	HTML_HEADING(&s, "Play Time");
	HTML_DESCRIPTION(&s, "The total amount of time played by each category of player, since the last reset");
	estrAppend2(&s, "<table border=\"1\"><tr><td><b>Locale</b></td><td><b>Product</b></td>"
		"<td><b>Shard</b></td><td><b>Total Log Outs</b></td><td><b>Total Play Time (seconds)</b></td></tr>\n");
	EARRAY_CONST_FOREACH_BEGIN(stats->PlayTime, i, n);
		const PlayTimeContainer *playTime = stats->PlayTime[i];
		estrConcatf(&s, "<tr><td>%s</td><td>%s</td><td>%s</td><td>%"FORM_LL"u</td><td>%"FORM_LL"u</td></tr>\n",
			STATS_EMPTY_TO_NONE(playTime->Locale), playTime->Product, playTime->Shard, playTime->TotalLogouts, playTime->TotalPlayTime);
	EARRAY_FOREACH_END;
	estrAppend2(&s, "</table>\n");

	// Purchases tables
	HTML_HEADING(&s, "Purchases");
	HTML_DESCRIPTION(&s, "The total counts and amounts of purchases, since the last reset");
	wiHtmlizePurchases(&s, stats->Purchases, 0);
	HTML_HEADING(&s, "Purchases (This Hour)");
	wiHtmlizePurchases(&s, stats->PurchasesHourlyCurrent, stats->global->HourlyCurrentStart);
	HTML_HEADING(&s, "Purchases (Previous Hour)");
	wiHtmlizePurchases(&s, stats->PurchasesHourlyPrevious, stats->global->HourlyPreviousStart);

	// Points balances table
	HTML_HEADING(&s, "Points Balances");
	HTML_DESCRIPTION(&s, "The instantaneous and current points balances for all accounts in each category."
		"  Note: Balances from stuck keys (normally the result of a bug or crash) are not included in this.");
	estrAppend2(&s, "<table border=\"1\"><tr><td><b>Points Type</b></td><td><b>Total Points Balances</b></td>"
		"<td><b>Accounts With Points Balance</b></td></tr>\n");
	EARRAY_CONST_FOREACH_BEGIN(stats->TotalAccountsPointsBalance, i, n);
		const TotalPointsBalance *balance = stats->TotalAccountsPointsBalance[i];
		estrConcatf(&s, "<tr><td>%s</td><td>%s</td><td>%lu</td></tr>\n", balance->Currency, balance->TotalBalances, balance->AccountsWithPoints);
	EARRAY_FOREACH_END;
	estrAppend2(&s, "</table>\n");

	// Transaction response code table.
	HTML_HEADING(&s, "Transaction Response Codes");
	HTML_DESCRIPTION(&s, "Response code distribution from all billing transactions, including billing transactions that did not"
		" result in successful transactions, since the last reset");
	estrAppend2(&s, "<table border=\"1\"><tr><td>Locale</td><td>Currency</td><td>Transaction Reason Code</td><td>AVS Code</td>"
		"<td>CVN Code</td><td>Count</td></tr>\n");
	EARRAY_CONST_FOREACH_BEGIN(stats->TransactionCodes, i, n);
		const TransactionCodesContainer *combination = stats->TransactionCodes[i];
		estrConcatf(&s, "<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%"FORM_LL"u</td></tr>\n",
			STATS_EMPTY_TO_NONE(combination->Locale), combination->Currency, STATS_EMPTY_TO_NONE(combination->AuthCode),
			STATS_EMPTY_TO_NONE(combination->AvsCode), STATS_EMPTY_TO_NONE(combination->CvnCode), combination->Count);
	EARRAY_FOREACH_END;
	estrAppend2(&s, "</table>\n");

	// Recent billing authorization transactions.
	HTML_HEADING(&s, "Recent Billing Authorization Transactions");
	HTML_DESCRIPTION(&s, "This is a list of all billing authorization transactions that have occurred in the past hour.  These transactions are"
		"attempts to authorize charges on a customer's credit card.  If the failure rate is too high, the Account Server will automatically alert."
		"The information below may be incomplete; its purpose is only to speed up debugging of billing problems.");
	estrAppend2(&s, "<table border=\"1\"><tr><td>Success</td><td>Logging Transaction ID</td><td>XML-RPC ID</td><td>Result String</td>"
		"<td>Account ID</td><td>Transaction Recorded</td></tr>\n");
	EARRAY_CONST_FOREACH_BEGIN(stats->AuthTransactions, i, n);
		AuthTransaction *trans = stats->AuthTransactions[i];
		char strRecorded[512];
		if (trans->uRecorded)
			timeMakeLocalDateStringFromSecondsSince2000_s(strRecorded, sizeof(strRecorded), trans->uRecorded);
		else
			strRecorded[0] = 0;
		estrConcatf(&s, "<tr><td>%s</td><td>%lu</td><td>%s</td><td>%s</td><td>%lu</td><td>%s</td></tr>\n",
			trans->bSuccess ? "Yes" : "No", trans->uTransId, NULL_TO_EMPTY(trans->pWebUid),
			NULL_TO_EMPTY(trans->pResultString), trans->uAccountId, strRecorded);
	EARRAY_FOREACH_END;
	estrAppend2(&s, "</table>\n");

	// Finish
	StructDestroy(parse_AccountStats, stats);
	httpSendWrappedString(link, s, pCookies);
	estrDestroy(&buffer);
	estrDestroy(&s);
	PERFINFO_AUTO_STOP_FUNC();
}

#undef STATS_EMPTY_TO_NONE

static void wiMainPage(NetLink *link, const char *pReferer, CookieList *pCookies)
{
	//httpSendWrappedString(link, "<a href=\"search\">View All Accounts</a>", pCookies);
	wiRedirectf(link, "/detail");
}

static void wiAccountAppendSummary(char **estr, AccountInfo *account, bool bOddRow)
{
	appendFancyTableEntry(estr, bOddRow, 6,
		STACK_SPRINTF("%d", account->uID),
		STACK_SPRINTF("<a href=\"detail?accountname=%s\">%s</a>", account->accountName, account->accountName),
		account->displayName,
		account->personalInfo.email,
		account->personalInfo.firstName,
		account->personalInfo.lastName
		);
}

// Maximum number of results returned when doing a search for accounts on the web interface
static int giMaxAccountSearchResults = 100;
AUTO_CMD_INT(giMaxAccountSearchResults, MaxAccountSearchResults) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

static void wiAccountsSearch(DEFAULT_HTTPGET_PARAMS)
{
	static char *s = NULL;
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	int iCount = 0;
	int i;

	AccountSearchData sd = {0};
	int offset = 0;
	bool bSearch = count > 1;

	estrCopy2(&s, "");
	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "sort") == 0)
		{
			sd.uFlags |= SEARCHFLAG_SORT;
			sd.eSortOrder = (SortOrder)CLAMP(atoi(values[i]), 0, SORTORDER_MAX);
		}
		else if(stricmp(args[i], "asc") == 0)
		{
			sd.bSortDescending = !(atoi(values[i]) == 1);
		}
		else if(stricmp(args[i], "offset") == 0)
		{
			offset = atoi(values[i]);
		}
		else if (stricmp(args[i], "name") == 0)
		{
			if(values[i][0] != 0)
			{
				sd.name = values[i];
				sd.uFlags |= SEARCHFLAG_NAME;
				if (!sd.iNameFilter)
					sd.iNameFilter = SEARCHNAME_ACCOUNT; // default value
			}
		}
		else if (stricmp(args[i], "namef") == 0)
		{
			int value = atoi(values[i]);
			if (value)
				sd.iNameFilter = value;
		}
		else if (stricmp(args[i], "email") == 0)
		{
			if(values[i][0] != 0)
			{
				sd.email = values[i];
				sd.uFlags |= SEARCHFLAG_EMAIL;
			}
		}
		else if (stricmp(args[i], "firstn") == 0)
		{
			if(values[i][0] != 0)
			{
				sd.firstName = values[i];
				sd.uFlags |= SEARCHFLAG_FIRSTNAME;
			}
		}
		else if (stricmp(args[i], "lastn") == 0)
		{
			if(values[i][0] != 0)
			{
				sd.lastName = values[i];
				sd.uFlags |= SEARCHFLAG_LASTNAME;
			}
		}
		else if (stricmp(args[i], "prsub") == 0)
		{
			if(values[i][0] != 0)
			{
				sd.productSub = values[i];
				sd.uFlags |= SEARCHFLAG_PRODUCTSUB;
				if (!sd.iProductFilter)
					sd.iProductFilter = SEARCHPERMISSIONS_BOTH; // default value
			}
		}
		else if (stricmp(args[i], "prsubf") == 0)
		{
			int value = atoi(values[i]);
			if (value)
				sd.iProductFilter = value;
		}
		else if (stricmp(args[i], "pkey") == 0)
		{
			if(values[i][0] != 0)
			{
				sd.productKey = values[i];
				sd.uFlags |= SEARCHFLAG_PRODUCTKEY;
			}
		}
		else if(stricmp(args[i], "any") == 0) // The "Any" string
		{
			if(values[i][0] != 0)
			{
				sd.pAny = values[i];
				sd.uFlags |= SEARCHFLAG_ALL;
			}
		}
	}

	{ // Search form
		if (isProductionMode())
		{
			estrConcatf(&s, "<div style=\"font-size: 48pt; color: red; font-weight: bold\"><blink>DO NOT USE BY PAIN OF FAILCAT</blink></div>\n");
		}
		
		estrConcatf(&s, "<div class=\"formdata\">\n");
		wiFormAppendStart(&s, "/search", "GET", "searchform", NULL);

		// Start inner table
		estrConcatf(&s, "\n<table>\n"); // Column 1

		estrConcatf(&s, "<tr><td>Account or Display Name:  </td><td>");
		formAppendEdit(&s, 30, "name", sd.name ? sd.name : "");
		estrConcatf(&s, "</td></tr>");

		estrConcatf(&s, "<tr><td align=right>Search: </td><td>");
		formAppendEnum(&s, "namef", "--|Account Name only|Display Name only|Both", sd.iNameFilter);
		estrConcatf(&s, "</td></tr>");

		estrConcatf(&s, "<tr><td>Email: </td><td>");
		formAppendEdit(&s, 30, "email", sd.email ? sd.email : "");
		estrConcatf(&s, "</td></tr>");
		estrConcatf(&s, "<tr><td>First Name: </td><td>");
		formAppendEdit(&s, 30, "firstn", sd.firstName ? sd.firstName : "");
		estrConcatf(&s, "</td></tr>");
		estrConcatf(&s, "<tr><td>Last Name: </td><td>");
		formAppendEdit(&s, 30, "lastn", sd.lastName ? sd.lastName : "");
		estrConcatf(&s, "</td></tr>");

		estrConcatf(&s, "<tr><td>Products and Subscriptions:  </td><td>");
		formAppendEdit(&s, 30, "prsub", sd.productSub ? sd.productSub : "");
		estrConcatf(&s, "</td></tr>");

		estrConcatf(&s, "<tr><td align=right>Search: </td><td>");
		formAppendEnum(&s, "prsubf", "--|Product name only|Subscription name only|Both", sd.iProductFilter);
		estrConcatf(&s, "</td></tr>");

		estrConcatf(&s, "<tr><td>Product Key (Exact):  </td><td>");
		formAppendEdit(&s, 30, "pkey", sd.productKey ? sd.productKey : "");
		estrConcatf(&s, "</td></tr>");

		// End inner table
		estrConcatf(&s, "\n</table>\n");

		// End of the "Any" string
		//estrConcatf(&s, "\n</td><td><br>");
		formAppendSubmit(&s, "Search");

		formAppendEnd(&s);
		estrConcatf(&s, "</div>\n");
	}		

	if(bSearch)
	{
		U32 elapsedTimerID = timerAlloc();
		AccountInfo *account = NULL;
		F32 fElapsed = 0;
		
		timerStart(elapsedTimerID);

		account = searchFirst(&sd);

		estrConcatf(&s, "Search Results (%d max):<br>\n", giMaxAccountSearchResults);

		appendPaginatedFancyTableHeader(&s, 6, "ID", "Account Name", "Display Name", "E-mail Address", "First Name", "Last Name");
		if (!account)
		{
			estrConcatf(&s, "<tr><td colspan=6>No accounts found</td></tr>\n");
		}
		else
		{
			iCount = 0;
			while(account != NULL && iCount < giMaxAccountSearchResults)
			{
				wiAccountAppendSummary(&s, account, i%2);
				account = searchNext(&sd);
				iCount++;
			}
		}
		searchEnd(&sd);

		appendFancyTableFooter(&s);

		fElapsed = timerElapsed(elapsedTimerID);

		if (isProductionMode() && fElapsed > 1)
		{
			const char *loggedIn = httpFindAuthenticationUsername(pClientState);

			estrConcatf(&s, "<div style=\"font-size: 24pt; color: red; font-weight: bold\">You stalled the Account Server for %f seconds; NetOps will be delivering the "
				"Failcat shortly if this was not authorized.</div>\n",
				fElapsed);

			AssertOrAlert("SEARCH_FAILCAT", "The user %s has stalled the Account Server for %f seconds to do a search. "
				"Please give them the Failcat if this was not authorized.", loggedIn && *loggedIn ? loggedIn : "unknown", fElapsed);
		}

		timerFree(elapsedTimerID);
	}

	httpSendWrappedString(link, s, pCookies);
}

// ------------------------------------------------------
// Product Key Pages

static void appendProductKeyGroupDetails(char **estr, ProductKeyGroup *keyGroup)
{
	if (keyGroup)
	{
		char *productList = NULL;

		// Product prefix
		HTML_HEADING(estr, "Product Prefix");
		HTML_DESCRIPTION(estr, "All keys in this group will begin with the product prefix.");
		estrConcatf(estr, "%s\n", keyGroup->productPrefix);

		// Activated products
		HTML_HEADING(estr, "Activated Products");
		HTML_DESCRIPTION(estr, "Each product on this comma-separated list of products will be activated when a key from this group is activated.");
		estrStackCreate(&productList);
		EARRAY_CONST_FOREACH_BEGIN(keyGroup->ppProducts, i, n);
		{
			if (i)
				estrConcatChar(&productList, ',');
			estrAppend2(&productList, keyGroup->ppProducts[i]);
		}
		EARRAY_FOREACH_END;
		wiFormAppendStart(estr, "/keygroupSetProductList", "POST", "keygroupSetProductList", NULL);
		formAppendHidden(estr, "prefix", keyGroup->productPrefix);
		formAppendEdit(estr, 60, "productList", productList);
		formAppendSubmit(estr, "Change");
		formAppendEnd(estr);
		estrDestroy(&productList);
	}
}

// Set the product list for a key group.
static void wiKeyGroupSetProductList(DEFAULT_HTTPPOST_PARAMS)
{
	const char *prefix = urlFindSafeValue(args, "prefix");
	const char *productList = urlFindSafeValue(args, "productList");
	ProductKeyGroup *keyGroup = NULL;
	bool success = false;

	// Look up key group.
	if (prefix)
		keyGroup = findKeyGroupFromString(prefix);

	// Try to set product list for group.
	if (keyGroup)
		success = setKeyGroupProductNames(keyGroup->uID, productList);

	if (!success)
	{
		httpSendWrappedString(link, "Unable to set key group product list", pCookies);
		return;
	}

	// Return to key page.
	wiRedirectf(link, "/keygroupView?prefix=%s", prefix);
}

static void appendProductKeyBatchDetails(char **estr, const ProductKeyBatch *keyBatch)
{
	if (keyBatch)
	{
		char datetime[64];
		int i, size;
		EARRAY_OF(ProductKey) eaKeys;
		char *pExtraInfo = NULL;

		HTML_HEADING(estr, "Key Batch Name");
		HTML_DESCRIPTION(estr, "Internally used name of this key batch.");
		estrConcatf(estr, "%s\n", keyBatch->pBatchName);

		// Batch validity
		HTML_HEADING(estr, "Key Batch Valid");
		HTML_DESCRIPTION(estr, "If a batch is invalid, the unused keys within cannot be activated.");
		wiFormAppendStart(estr, "/batchInvalidate", "POST", "batchinvalidate", NULL);
		formAppendHidden(estr, "batch", STACK_SPRINTF("%lu", keyBatch->uID));
		if (keyBatch->batchInvalidated)
			formAppendSubmit(estr, "Mark as Valid (currently invalid)");
		else
			formAppendSubmit(estr, "Mark as Invalid (currently valid)");
		formAppendEnd(estr);

		// Batch Distribution
		HTML_HEADING(estr, "Key Batch Distribution");
		HTML_DESCRIPTION(estr, "If the batch is marked as distributed, keys within cannot be distributed automatically by the activation of a product.");
		wiFormAppendStart(estr, "/batchDistributed", "POST", "batchdistributed", NULL);
		formAppendHidden(estr, "batch", STACK_SPRINTF("%lu", keyBatch->uID));
		if (keyBatch->batchDistributed)
			formAppendSubmit(estr, "Mark as Not Distributed (currently distributed)");
		else
			formAppendSubmit(estr, "Mark as Distributed (currently not distributed)");
		formAppendEnd(estr);

		HTML_HEADING(estr, "Key Batch Description");
		HTML_DESCRIPTION(estr, "Internally used description of this batch.");
		estrConcatf(estr, "%s\n", keyBatch->pBatchDescription);

		timeMakeLocalDateStringFromSecondsSince2000 (datetime, keyBatch->timeCreated);
		HTML_HEADING(estr, "Date Created");
		HTML_DESCRIPTION(estr, "The date this batch was added.");
		estrConcatf(estr, "%s\n", datetime);

		if (keyBatch->timeDownloaded)
		{
			timeMakeLocalDateStringFromSecondsSince2000 (datetime, keyBatch->timeDownloaded);
			HTML_HEADING(estr, "Downloaded Date");
			estrConcatf(estr, "%s\n", datetime);
		}

		eaKeys = productKeysGetBatchKeysNew(keyBatch->uID);
		size = eaSize(&eaKeys);

		HTML_HEADING(estr, "Unused Product Keys");
		estrConcatf(estr, "<table border=0 cellpadding=3 cellspacing=0>\n");
		estrStackCreate(&pExtraInfo);
		for (i=0; i<size; i++)
		{
			char *key = NULL;
			estrStackCreate(&key);
			copyProductKeyName(&key, eaKeys[i]);
			estrConcatf(estr, "<td><a href=\"keyView?key=%s\" class=\"key\">%s</a></td>", key, key);
			estrDestroy(&key);

			estrClear(&pExtraInfo);
			if (productKeyIsDistributed(eaKeys[i]))
			{
				AccountInfo *pAccount = findAccountByID(getDistributedAccountId(eaKeys[i]));
			
				if (pAccount)
					estrConcatf(&pExtraInfo, "(distributed to <a href=\"detail?accountname=%s\">%s</a>) ", pAccount->accountName, pAccount->accountName);
			}
			if (productKeyIsLocked(eaKeys[i]))
			{
				estrConcatf(&pExtraInfo, "(locked) ");
			}

			estrConcatf(estr, "<td> %s </td></tr>\n", pExtraInfo);

		}
		estrDestroy(&pExtraInfo);
		estrConcatf(estr, "</table>");
		eaDestroy(&eaKeys); // Do not free contents

		eaKeys = productKeysGetBatchKeysUsed(keyBatch->uID);
		size = eaSize(&eaKeys);
		HTML_HEADING(estr, "Used Product Keys");
		estrConcatf(estr, "<table border=0 cellpadding=3 cellspacing=0>\n");
		for (i=0; i<size; i++)
		{
			AccountInfo *pAccount = findAccountByID(getActivatedAccountId(eaKeys[i]));
			char *key = NULL;
			estrStackCreate(&key);
			copyProductKeyName(&key, eaKeys[i]);
			estrConcatf(estr, "<tr><td><a href=\"keyView?key=%s\" class=\"key\">%s</a></td>", key, key);
			estrDestroy(&key);
			if (pAccount)
				estrConcatf(estr, "<td><a href=\"detail?accountname=%s\">%s</a></td></tr>\n", pAccount->accountName, pAccount->accountName);
			else
				estrConcatf(estr, "<td>Unknown</td></tr>\n");
		}
		eaDestroy(&eaKeys); // Do not free contents

		estrConcatf(estr, "</table>\n");
	}
}

static void wiCreateProductKeys (DEFAULT_HTTPPOST_PARAMS)
{
	const char *batchName = urlFindValue(args, "name");
	const char *batchDescription = urlFindValue(args, "description");
	const char *productPrefix = urlFindValue(args, "prefix");
	int count = atoi(urlFindSafeValue(args, "keycount"));
	int createErrs;
	const char *loggedIn = httpFindAuthenticationUsername(pClientState);

	if (!batchName || !batchName[0])
	{
		httpSendWrappedString(link, "A non-empty Batch Name must be entered.", pCookies);
		return;
	}
	if (count <= 0)
	{
		httpSendWrappedString(link, "Number of keys to create must be greater than 0!", pCookies);
		return;
	}

	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: createKeys(%s, %s, %d), %s", productPrefix, batchName, count, 
		loggedIn ? loggedIn : "Unknown User");
	createErrs = productKeysCreateBatch(productPrefix, count, batchName, batchDescription);
	if (createErrs)
	{
		// TODO error handling
		httpSendWrappedString(link, "Failed to create keys", pCookies);
	}
	else 
		httpSendWrappedString(link, STACK_SPRINTF("Created Key Batch of %d keys", count), pCookies);
}

static void wiProductKeyGroupView (DEFAULT_HTTPGET_PARAMS)
{
	static char *s = NULL;
	ProductKeyGroup *keyGroup = NULL;
	int prefix;

	for (prefix=0; prefix<count; prefix++)
	{
		if (stricmp(args[prefix], "prefix") == 0)
		{
			keyGroup = findKeyGroupFromString(values[prefix]);
		}
	}

	estrCopy2(&s, "");
	if (keyGroup)
	{
		appendProductKeyGroupDetails(&s, keyGroup);
		estrConcatf(&s, "<div class=\"heading\">Product Key Batches</div>\n");

		appendPaginatedFancyTableHeader(&s, 5, "Key Batch", "Unused Keys", "Used Keys", "Total Keys", "Product(s)");
		EARRAY_INT_CONST_FOREACH_BEGIN(keyGroup->keyBatches, i, n);
		{
			int j, size;
			static char *productString = NULL;
			U32 batchId = keyGroup->keyBatches[i];
			const ProductKeyBatch *batch = getKeyBatchByID(batchId);
			U32 unusedKeys = productKeysBatchSizeNew(batchId);
			U32 usedKeys = productKeysBatchSizeUsed(batchId);
			U32 totalKeys = productKeysBatchSize(batchId);
			static char *pUsedKeys = NULL;
			static char *pUnusedKeys = NULL;
			static char *pTotalKeys = NULL;
			static char *pLink = NULL;

			estrPrintf(&pUsedKeys, "%d", usedKeys);
			estrPrintf(&pUnusedKeys, "%d", unusedKeys);
			estrPrintf(&pTotalKeys, "%d", totalKeys);
			estrPrintf(&pLink, "<a href=\"batchView?id=%d\">%s</a>", batchId, batch->pBatchName);

			size = eaSize(&keyGroup->ppProducts);
			estrClear(&productString);
			for (j=0; j<size; j++)
			{
				estrConcatf(&productString, "%s<a href=\"productDetail?product=%s\">%s</a>", 
					j ? ", " : "", keyGroup->ppProducts[j], keyGroup->ppProducts[j]);
			}

			appendFancyTableEntry(&s, i%2, 5, pLink, pUnusedKeys, pUsedKeys, pTotalKeys, productString);
		}
		EARRAY_FOREACH_END;
		appendFancyTableFooter(&s);

		estrConcatf(&s, "<br>\n");
		if (isAccountServerMode(ASM_KeyGenerating))
		{
			wiFormAppendStart(&s, "/batchCreate", "POST", "batchcreate", NULL);
			formAppendHidden(&s, "prefix", keyGroup->productPrefix);
			formAppendSubmit(&s, "Add New Key Batch");
			formAppendEnd(&s);
		}
		httpSendWrappedString(link, s, pCookies);
	}
	else
	{
		httpSendWrappedString(link, "Could not find Product Key Group.", pCookies);
	}
}

static void wiProductKeyGroupList(DEFAULT_HTTPGET_PARAMS)
{
	static char *s = NULL;
	CONST_EARRAY_OF(ProductKeyGroup) eaKeyGroupList = getProductKeyGroupList();
	
	PERFINFO_AUTO_START_FUNC();

	estrClear(&s);

	appendPaginatedFancyTableHeader(&s, 5, "Prefix", "Batches", "Unused Keys", "Used Keys", "Total Keys");

	EARRAY_CONST_FOREACH_BEGIN(eaKeyGroupList, iCurGroup, iNumGroups);
	{
		int iNumUsed = 0, iNumUnused = 0;
		static char *pLink = NULL;
		const ProductKeyGroup *pKeyGroup = eaKeyGroupList[iCurGroup];
		static char *pNumKeyBatches = NULL;
		static char *pNumUnused = NULL;
		static char *pNumUsed = NULL;
		static char *pNumTotal = NULL;

		if (!devassert(pKeyGroup)) continue;

		estrPrintf(&pLink, "<a href=\"keygroupView?prefix=%s\">%s</a>", pKeyGroup->productPrefix, pKeyGroup->productPrefix);
		estrPrintf(&pNumKeyBatches, "%d", eaiSize(&pKeyGroup->keyBatches));


		EARRAY_INT_CONST_FOREACH_BEGIN(pKeyGroup->keyBatches, iCurBatch, iNumBatches);
		{
			U32 batchId = pKeyGroup->keyBatches[iCurBatch];
			const ProductKeyBatch *pBatch = getKeyBatchByID(batchId);

			if (!devassert(pBatch)) continue;

			iNumUsed += productKeysBatchSizeUsed(pBatch->uID);
			iNumUnused += productKeysBatchSizeNew(pBatch->uID);
		}
		EARRAY_FOREACH_END;

		estrPrintf(&pNumUnused, "%d", iNumUnused);
		estrPrintf(&pNumUsed, "%d", iNumUsed);
		estrPrintf(&pNumTotal, "%d", iNumUsed + iNumUnused);

		appendFancyTableEntry(&s, iCurGroup%2, 5, pLink,
			pNumKeyBatches,
			pNumUnused,
			pNumUsed,
			pNumTotal);
	}
	EARRAY_FOREACH_END;
	appendFancyTableFooter(&s);

	// Form for looking up activation keys.
	wiFormAppendStart(&s, "/keyView", "GET", "keyView", NULL);
	estrAppend2(&s, "Look up activation key: ");
	formAppendEdit(&s, 20, "key", "");
	formAppendSubmit(&s, "View");
	formAppendEnd(&s);

	if (!isAccountServerMode(ASM_KeyGenerating))
	{
		wiFormAppendStart(&s, "/keygroupCreate", "GET", "keygroupCreate", NULL);
		formAppendSubmit(&s, "Add New Key Group");
		formAppendEnd(&s);
	}
	else
	{
		wiFormAppendStart(&s, "/keygroupUpdate", "GET", "keygroupUpdate", NULL);
		formAppendSubmit(&s, "Update Product Key Groups and Products Lists");
		formAppendEnd(&s);
	}
	httpSendWrappedString(link, s, pCookies);

	PERFINFO_AUTO_STOP_FUNC();
}

static void wiKeyView(DEFAULT_HTTPGET_PARAMS)
{
	const char *keystring = NULL;
	char *s = NULL;
	ProductKey key = {0};
	ProductKeyBatch *batch;
	ProductKeyGroup *group;
	U32 accountId;
	AccountInfo *account = NULL;
	int arg;
	bool success;
	char *keyName = NULL;

	// Get key from args.
	for (arg=0; arg<count; arg++)
		if (stricmp(args[arg], "key") == 0)
			keystring = values[arg];
	if (!keystring)
	{
		httpSendWrappedString(link, "Form error", pCookies);
		return;
	}

	// Check if this key exists.
	success = findProductKey(&key, keystring);
	estrStackCreate(&s);
	if (!success)
	{
		estrPrintf(&s, "There is no product key \"%s\" in the database.", keystring);
		httpSendWrappedString(link, s, pCookies);
		estrDestroy(&s);
		return;
	}

	// Get key information.
	batch = getKeyBatchByID(key.uBatchId);
	group = keyGroupFromBatch(batch);
	devassert(batch && group);

	accountId = getActivatedAccountId(&key);
	if (accountId)
		account = findAccountByID(accountId);

	// Print key information.
	HTML_HEADING(&s, "Product Activation Key");
	estrStackCreate(&keyName);
	copyProductKeyName(&keyName, &key);
	estrConcatf(&s, "<span class=\"key\">%s</span>", keyName);
	
	HTML_HEADING(&s, "Associated Account");
	HTML_DESCRIPTION(&s, "If the product key is used, this is the associated account.");
	if (account && account->accountName)
		estrConcatf(&s, "<a href=\"detail?accountname=%s\">%s</a>", account->accountName, account->accountName);
	else
		estrAppend2(&s, "<i>unused</i>");

	HTML_HEADING(&s, "Distributed Account");
	HTML_DESCRIPTION(&s, "If this key has been distributed, this is the account it was distributed to.");
	if (productKeyIsDistributed(&key))
	{
		account = findAccountByID(getDistributedAccountId(&key));

		if (account && account->accountName)
			estrConcatf(&s, "<a href=\"detail?accountname=%s\">%s</a>", account->accountName, account->accountName);
		else
			estrAppend2(&s, "<i>not distributed</i>");
	}
	else
		estrAppend2(&s, "<i>not distributed</i>");

	HTML_HEADING(&s, "Key Batch");
	HTML_DESCRIPTION(&s, "The key batch that generated this key");
	estrConcatf(&s, "<a href=\"batchView?id=%ld\">%s</a>", batch->uID, batch->pBatchName);

	HTML_HEADING(&s, "Product Prefix");
	HTML_DESCRIPTION(&s, "The product prefix is the first few letters of the product key, shared by all keys in the product group.");
	estrConcatf(&s, "<a href=\"keygroupView?prefix=%s\">%s</a>", group->productPrefix,
		group->productPrefix);

	HTML_HEADING(&s, "Validity");
	HTML_DESCRIPTION(&s, "If the key or the key's batch has been marked as invalid, it may not be used to register products.");
	estrConcatf(&s, "%s", productKeyIsValidByPtr(&key) ? "Valid" : "Invalid");
	wiFormAppendStart(&s, "/keyInvalidate", "POST", "keyinvalidate", NULL);
	formAppendHidden(&s, "key", keyName);
	if (productKeyGetInvalid(keyName))
		formAppendSubmit(&s, "Mark key as Valid (currently invalid)");
	else
		formAppendSubmit(&s, "Mark key as Invalid (currently valid)");

	HTML_HEADING(&s, "Lock");
	HTML_DESCRIPTION(&s, "If a key is locked, it probably means something went wrong.");
	if (productKeyIsLocked(&key))
	{
		wiFormAppendStart(&s, "/productKeyUnlock", "POST", "productKeyUnlock", NULL);
		formAppendHidden(&s, "key", keyName);
		formAppendSubmit(&s, "Unlock");
		formAppendEnd(&s);
	}
	else
	{
		estrConcatf(&s, "Not locked");
	}
	estrDestroy(&keyName);

	HTML_HEADING(&s, "Key Products");
	HTML_DESCRIPTION(&s, "All products associated with this key");
	EARRAY_CONST_FOREACH_BEGIN(group->ppProducts, i, size);
		estrConcatf(&s, "<a href=\"productDetail?product=%s\">%s</a>\n", group->ppProducts[i], group->ppProducts[i]);
	EARRAY_FOREACH_END;
	httpSendWrappedString(link, s, pCookies);
	estrDestroy(&s);
}

static void wiProductKeyUnlock(DEFAULT_HTTPPOST_PARAMS)
{
	const char *pKeyString = urlFindSafeValue(args, "key");
	ProductKeyResult ret = unlockProductKey(pKeyString);
	const char *loggedIn = httpFindAuthenticationUsername(pClientState);

	switch (ret)
	{
	xcase PK_Success:
		wiRedirectf(link, "keyView?key=%s", pKeyString);
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: productKeyUnlock(%s), %s, Success", pKeyString, 
			loggedIn ? loggedIn : "Unknown User");
		return;
	xcase PK_KeyInvalid:
		httpSendWrappedString(link, "Key invalid", pCookies);
	xcase PK_KeyNotLocked:
		httpSendWrappedString(link, "Key not locked", pCookies);
	xdefault:
		httpSendWrappedString(link, "Could not unlock key", pCookies);
	}

	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: productKeyUnlock(%s), %s, Failed", pKeyString, 
		loggedIn ? loggedIn : "Unknown User");
}

static void wiProductKeyBatchView (DEFAULT_HTTPGET_PARAMS)
{
	static char *s = NULL;
	int i;
	const ProductKeyBatch *keyBatch = NULL;
	ProductKeyGroup *keyGroup;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "id") == 0)
		{
			keyBatch = getKeyBatchByID(atoi(values[i]));
		}
		else if (stricmp(args[i], "name") == 0)
		{
			keyBatch = getKeyBatchByName(values[i]);
		}
	}
	if (!keyBatch)
		return;

	keyGroup = keyGroupFromBatch(keyBatch);
	if (!keyGroup)
		return;

	estrCopy2(&s, "");

	estrConcatf(&s, "<a href=\"keygroupView?prefix=%s\">View Key Group</a><br>\n", keyGroup->productPrefix);
	appendProductKeyGroupDetails(&s, keyGroup);

	wiFormAppendStart(&s, "/batchDownload", "GET", "downloadbatch", NULL);
	formAppendHiddenInt(&s, "id", keyBatch->uID);
	formAppendSubmit(&s, "Download Unused Keys");
	formAppendEnd(&s);

	appendProductKeyBatchDetails(&s, keyBatch);

	httpSendWrappedString(link, s, pCookies);
}

static void appendProductKeyBatchCreationForm(char **estr, const char *prefix)
{
	estrCopy2(estr, "");
	if (isAccountServerMode(ASM_KeyGenerating))
	{
		estrConcatf(estr, "<div class=\"heading\">Create Key Batch</div>\n");
		wiFormAppendStart(estr, "/batchCreateAction", "POST", "batchcreation", NULL);
		estrConcatf(estr, "<table border=0 cellpadding=3 cellspacing=0>\n");
		
		estrConcatf(estr, "<tr><td>Product Key Prefix</td>\n<td>");
		formAppendEdit(estr, 20, "prefix", prefix ? prefix : "");
		estrConcatf(estr, "</td></tr>\n");

		estrConcatf(estr, "<tr><td>Batch Name:</td>\n<td>");
		formAppendEdit(estr, 20, "name", "");
		estrConcatf(estr, "</td></tr>\n"); 

		estrConcatf(estr, "<tr><td>Batch Description:</td>\n<td>");
		formAppendEdit(estr, 40, "description", "");
		estrConcatf(estr, "</td></tr>\n"); 

		/*estrConcatf(estr, "<tr><td>Batch Shard Categories:</td>\n<td>");
		formAppendEdit(estr, 40, "shard", "");
		estrConcatf(estr, "</td></tr>\n");

		estrConcatf(estr, "<tr><td>Batch Special:</td>\n<td>");
		formAppendEdit(estr, 20, "special", "");
		estrConcatf(estr, "(a list of valid special access tokens can be found <a href=\"http://crypticwiki:8081/display/Core/Product+Key+Batch+Special+Tokens\">here</a>)</td></tr>\n");

		estrConcatf(estr, "<tr><td>Batch Valid Start Date/Time\n(YYYY-MM-DD HH:MM:SS):</td>\n<td>");
		formAppendEdit(estr, 20, "startdate", "");
		estrConcatf(estr, "</td></tr>\n"); 

		estrConcatf(estr, "<tr><td>Batch Valid End Date/Time\n(YYYY-MM-DD HH:MM:SS):</td>\n<td>");
		formAppendEdit(estr, 20, "enddate", "");
		estrConcatf(estr, "</td></tr>\n");*/

		estrConcatf(estr, "<tr><td>Number of Keys in Batch</td>\n<td>");
		formAppendEdit(estr, 20, "keycount", "");
		estrConcatf(estr, "</td></tr>\n"
			"</table>\n"); 

		formAppendSubmit(estr, "Create Key Batch");
		formAppendEnd(estr);
	}
}

static void wiProductKeyBatchCreationPost (DEFAULT_HTTPPOST_PARAMS)
{
	static char *s = NULL;
	appendProductKeyBatchCreationForm(&s, urlFindValue(args, "prefix"));
	httpSendWrappedString(link, s, pCookies);
}

static void wiProductKeyBatchCreation (DEFAULT_HTTPGET_PARAMS)
{
	static char *s = NULL;
	appendProductKeyBatchCreationForm(&s, NULL);
	httpSendWrappedString(link, s, pCookies);
}

static void wiCreateProductKeyGroup(DEFAULT_HTTPPOST_PARAMS)
{
	const char *productName = urlFindValue(args, "name");
	const char *productPrefix = urlFindValue(args, "prefix");
	const char *loggedIn = httpFindAuthenticationUsername(pClientState);

	bool success = createNewKeyGroup(productPrefix, productName);

	if (!success)
	{
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: createProductKeyGroup(%s, %s), %s, Failed", productPrefix, productName, 
			loggedIn ? loggedIn : "Unknown User");
		httpSendWrappedString(link, "Failed to create new Product Key Group", pCookies);
		return;
	}
	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: createProductKeyGroup(%s, %s), %s, Success", productPrefix, productName, 
		loggedIn ? loggedIn : "Unknown User");
	wiRedirectf(link, "/keygroupList");
}

static void wiProductKeyGroupCreation(DEFAULT_HTTPGET_PARAMS)
{
	static char *s = NULL;

	estrCopy2(&s, "");

	estrConcatf(&s, "<div class=\"heading\">Create Key Group</div>\n");
	wiFormAppendStart(&s, "/keygroupCreate", "POST", "keygroupcreation", NULL);
	estrConcatf(&s, "<table border=0 cellpadding=3 cellspacing=0>\n");
	
	estrConcatf(&s, "<tr><td>Product Key Prefix:</td>\n<td>");
	formAppendEdit(&s, 5, "prefix", "");
	estrConcatf(&s, " (maximum 5 characters long, 0-9 or A-Z)</td>\n");
	estrConcatf(&s, "<td>Product:</td>\n<td>");
	formAppendEdit(&s, 60, "name", "");		
	estrConcatf(&s, "</td></tr>\n</table>\n");

	formAppendSubmit(&s, "Create Key Group");
	formAppendEnd(&s);
	httpSendWrappedString(link, s, pCookies);
}

void wiProductKeyBatchDownload(DEFAULT_HTTPGET_PARAMS)
{
	int i;
	U32 keyBatchID = 0;
	ProductKeyBatch *keyBatch;
	char batchFileName[MAX_PATH] = "";
	char batchName[MAX_PATH] = "";

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "id") == 0)
		{
			keyBatchID = atoi(values[i]);
		}
	}

	keyBatch = getKeyBatchByID(keyBatchID);
	if (!keyBatch)
		return;

	sprintf(batchName, "keybatch_%d.csv", keyBatch->uID);
	sprintf(batchFileName, "%s%s", dbAccountDataDir(), batchName);

	if (!fileExists(batchFileName))
	{
		FILE *file;
		EARRAY_OF(ProductKey) eaKeys = productKeysGetBatchKeysNew(keyBatch->uID);

		batchDownloaded(keyBatch);

		file = fopen(batchFileName, "wb");

		EARRAY_CONST_FOREACH_BEGIN(eaKeys, j, numKeys);
		{
			char *keyName = NULL;
			estrStackCreate(&keyName);
			copyProductKeyName(&keyName, eaKeys[j]);
			fprintf(file, "%s\r\n", keyName);
			estrDestroy(&keyName);
		}
		EARRAY_FOREACH_END;

		fclose(file);
	}
	sendTextFileToLink(link, batchFileName, batchName, "text/csv", true);
}

static void wiProductKeyActivate(DEFAULT_HTTPPOST_PARAMS)
{
	static char *s = NULL;
	const char *temp = urlFindValue(args, "accountID");
	const char *pKey = urlFindValue(args, "key");
	U32 uAccountID = temp ? atoi(temp) : 0;
	AccountInfo *account = findAccountByID(uAccountID);

	estrCopy2(&s, "");
	if (uAccountID && account && pKey)
	{
		ANALYSIS_ASSUME(account);
		ANALYSIS_ASSUME(pKey);
		// TODO logging
		switch(activateProductKey(account, pKey, NULL))
		{
		case PK_Success:
			estrConcatf(&s, "Successfully activated the key for <a href=\"detail?accountname=%s\">%s</a>!", 
				account->accountName, account->accountName);
		xcase PK_PrefixInvalid:
			estrConcatf(&s, "Invalid prefix for key");
		xcase PK_KeyExists:
			estrConcatf(&s, "Something here");
		xcase PK_KeyInvalid:
			estrConcatf(&s, "Invalid key");
		xcase PK_KeyUsed:
			estrConcatf(&s, "Key previously used");
		xcase PK_CouldNotActivateProduct:
			estrConcatf(&s, "Could not activate product given by key");
		xcase PK_KeyLocked:
			estrConcatf(&s, "Could not activate the key because it is locked");
		xdefault:
			estrConcatf(&s, "Could not activate key");
		}
	}
	httpSendWrappedString(link, s, NULL);
}

static void wiProductKeyDistribute(DEFAULT_HTTPPOST_PARAMS)
{
	static char *s = NULL;
	const char *pKey = urlFindValue(args, "key");
	U32 uAccountID;
	AccountInfo *account = NULL;

	urlFindUInt(args, "accountID", &uAccountID);
	account = uAccountID ? findAccountByID(uAccountID) : NULL;

	estrCopy2(&s, "");
	if (account && pKey)
	{
		U32 uLockTime=0;

		ANALYSIS_ASSUME(account);
		ANALYSIS_ASSUME(pKey);

		switch(distributeProductKeyLock(account, pKey, &uLockTime))
		{
		case PK_Success:
			devassert(distributeProductKeyCommit(account, pKey, uLockTime) == PK_Success);
			estrConcatf(&s, "Successfully distributed the key to <a href=\"detail?accountname=%s\">%s</a>!", 
				account->accountName, account->accountName);
		xcase PK_PrefixInvalid:
			estrConcatf(&s, "Invalid prefix for key");
		xcase PK_KeyInvalid:
			estrConcatf(&s, "Invalid key");
		xcase PK_KeyUsed:
			estrConcatf(&s, "Key previously used");
		xcase PK_KeyDistributed:
			estrConcatf(&s, "Key previously distributed");
		default:
			break;
		}

	}
	httpSendWrappedString(link, s, NULL);
}

static void wiProductSubscriptionView(DEFAULT_HTTPGET_PARAMS)
{
	wiRedirectf(link, "/productView");
}

static void buildProductPriceString(SA_PARAM_NN_STR char **estr, SA_PARAM_NN_VALID const ProductContainer *product)
{
	char *amount = 0;
	estrStackCreate(&amount);
	EARRAY_CONST_FOREACH_BEGIN(product->ppMoneyPrices, i, s);
		const Money *pPrice = moneyContainerToMoneyConst(product->ppMoneyPrices[i]);
		if(estr && *estr && **estr)
			estrConcatf(estr, ", ");

		estrFromMoneyRaw(&amount, pPrice);
		estrConcatf(estr, "%s %s", amount, moneyCurrency(pPrice));
	EARRAY_FOREACH_END;
	estrDestroy(&amount);
}

// Converts an array into a comma-separated list
static const char * arrayToString(CONST_STRING_EARRAY earray)
{
	static char *pString = NULL;

	estrClear(&pString);

	EARRAY_CONST_FOREACH_BEGIN(earray, i, n);
	{
		if (!devassert(earray[i] && *earray[i])) continue;
		if (estrLength(&pString)) estrConcatString(&pString, ", ", 2);
		estrConcatString(&pString, earray[i], (int)strlen(earray[i]));
	}
	EARRAY_FOREACH_END;

	return pString;
}

static void wiAppendProductLocalization(SA_PARAM_NN_VALID char **s, SA_PARAM_OP_VALID const ProductContainer *pProduct)
{
	if (!pProduct) return;

	HTML_HEADING(s, "Localization");
	HTML_DESCRIPTION(s, "Localized strings that are to be displayed to users.");

	appendFancyTableHeader(s, 4, "IETF Language Tag", "Name", "Description", "Action");
	EARRAY_CONST_FOREACH_BEGIN(pProduct->ppLocalizedInfo, i, size);
	{
		ProductLocalizedInfo *pInfo = pProduct->ppLocalizedInfo[i];
		static char *pProductID = NULL;

		estrPrintf(&pProductID, "%d", pProduct->uID);

		wiFormAppendStart(s, "/unlocalizeProduct", "post", "Unlocalize", NULL);
		formAppendHidden(s, "productID", pProductID);
		formAppendHidden(s, "languageTag", pInfo->pLanguageTag);
		appendFancyTableEntry(s, i%2, 4,
			pInfo->pLanguageTag,
			pInfo->pName,
			pInfo->pDescription,
			HTML_INSERT_SUBMIT("Remove"));
		formAppendEnd(s);
	}
	EARRAY_FOREACH_END;

	wiFormAppendStart(s, "/localizeProduct", "post", "Localize", NULL);
	formAppendHidden(s, "productID", STACK_SPRINTF("%d", pProduct->uID));
	appendFancyTableEntry(s, false, 4,
		HTML_INSERT_TEXTINPUT("languageTag", ""),
		HTML_INSERT_TEXTINPUT("name", ""),
		HTML_INSERT_TEXTINPUT("description", ""),
		HTML_INSERT_SUBMIT("Add/Replace"),
		HTML_INSERT_FORM_END());
	formAppendEnd(s);

	appendFancyTableFooter(s);
}

static void wiAppendProductKeyValues(SA_PARAM_NN_VALID char **s, SA_PARAM_OP_VALID const ProductContainer *pProduct)
{
	if (!pProduct) return;

	HTML_HEADING(s, "Read-Only Key-Values");
	HTML_DESCRIPTION(s, "These key-values can only be modified from here and are used by shards.");

	appendFancyTableHeader(s, 3, "Key", "Value", "Action");
	EARRAY_CONST_FOREACH_BEGIN(pProduct->ppKeyValues, i, size);
	{
		AccountProxyKeyValueInfoContainer *pInfo = pProduct->ppKeyValues[i];
		static char *pProductID = NULL;

		estrPrintf(&pProductID, "%d", pProduct->uID);
		
		wiFormAppendStart(s, "/setProductKeyValue", "post", "SetProductKeyValue", NULL);
		formAppendHidden(s, "productID", pProductID);
		formAppendHidden(s, "key", pInfo->pKey);
		formAppendHidden(s, "value", "");
		appendFancyTableEntry(s, i%2, 3,
			pInfo->pKey, pInfo->pValue,
			HTML_INSERT_SUBMIT("Remove"));
		formAppendEnd(s);
	}
	EARRAY_FOREACH_END;

	wiFormAppendStart(s, "/setProductKeyValue", "post", "SetProductKeyValue", NULL);
	formAppendHidden(s, "productID", STACK_SPRINTF("%d", pProduct->uID));
	appendFancyTableEntry(s, false, 4,
		HTML_INSERT_TEXTINPUT("key", ""),
		HTML_INSERT_TEXTINPUT("value", ""),
		HTML_INSERT_SUBMIT("Add/Replace"),
		HTML_INSERT_FORM_END());
	formAppendEnd(s);

	appendFancyTableFooter(s);
}

static void wiAppendProductForm(SA_PARAM_NN_VALID char **s, SA_PARAM_OP_VALID const ProductContainer *product)
{
	int i, size;
	static char *reqsubs, *permissions, *shards, *priceString, *daysGranted, *internalSubExpiration;

	estrCopy2(&permissions, "");
	estrCopy2(&shards, "");
	estrCopy2(&reqsubs, "");
	estrCopy2(&priceString, "");
	estrCopy2(&daysGranted, "");
	estrCopy2(&internalSubExpiration, "");

	HTML_HEADING(s, "Internal Product Name*");
	HTML_DESCRIPTION(s, "This is the name permissions are applied to.  It does not need to be unique.");
	formAppendEdit(s, 30, "internal", product ? product->pInternalName : "");

	HTML_HEADING(s, "Description");
	HTML_DESCRIPTION(s, "Description of the product.");
	formAppendEdit(s, 60, "description", product ? product->pDescription : "");

	HTML_HEADING(s, "Billing Statement Identifier");
	HTML_DESCRIPTION(s, "What will be seen on the customer's billing statement.  Must follow specific formatting.  Not required.");
	formAppendEdit(s, 60, "billingstatementidentifier", product ? product->pBillingStatementIdentifier : "");

	if (product)
	{
		size = eaSize(&product->ppShards);
		for (i=0; i<size; i++)
		{
			estrConcatf(&shards, "%s%s", i ? "," : "", product->ppShards[i]);
		}
	}

	HTML_HEADING(s, "Shards");
	HTML_DESCRIPTION(s, "Comma-separated list of shards this product grants permissions to.");
	formAppendEdit(s, 60, "shards", shards);

	if (product)
		concatPermissionString(product->ppPermissions, &permissions);
	
	HTML_HEADING(s, "Permission");
	HTML_DESCRIPTION(s, "Takes the form of \"Permission : Value\" and is a semicolon-separated list.");
	formAppendEdit(s, 60, "permissions", permissions);

	if (product)
	{
		size = eaSize(&product->ppRequiredSubscriptions);
		for (i=0; i<size; i++)
		{
			estrConcatf(&reqsubs, "%s%s", i ? "," : "", product->ppRequiredSubscriptions[i]);
		}
	}

	HTML_HEADING(s, "Required Subscriptions");
	HTML_DESCRIPTION(s, "Comma-separated list of subscriptions the owner of this product must have for permissions this product grants to have any effect.");
	formAppendEdit(s, 60, "reqsubs", reqsubs);

	HTML_HEADING(s, "No Required Subscriptions");
	HTML_DESCRIPTION(s, "Used to temporarily disable the required subscriptions listed above.");
	formAppendCheckBox(s, "requiresNoSubs", product ? product->bRequiresNoSubs : false);

	HTML_HEADING(s, "Subscriptions Category");
	HTML_DESCRIPTION(s, "The category of subscriptions that should be offered to fullfill the required subscriptions for this product.  Used by the web site.");
	formAppendEdit(s, 60, "subCategory", product ? product->pSubscriptionCategory : "");

	HTML_HEADING(s, "Access Level");
	HTML_DESCRIPTION(s, "Access level of the permissions granted by this product.");
	formAppendEnum(s, "alvl", "0|1|2|3|4|5|6|7|8|9", product ? product->uAccessLevel : 0);

	HTML_HEADING(s, "Billing Synced");
	HTML_DESCRIPTION(s, "This puts it in Vindicia.  This must be checked if it has a real currency or if it is the product a subscription uses.  Cannot be undone once checked.");
	
	if (product && (product->uFlags & PRODUCT_BILLING_SYNC))
		estrConcatf(s, "Yes (cannot be undone)\n");
	else
		formAppendCheckBox(s, "billingsync", product ? (product->uFlags & PRODUCT_BILLING_SYNC) : false);

	HTML_HEADING(s, "Categories");
	HTML_DESCRIPTION(s, "Comma-separated list of categories this product belongs to.  Some things query lists of products by category.");
	formAppendEdit(s, 60, "categories", product ? product->pCategoriesString : "");

	HTML_HEADING(s, "Do Not Associate On Activation");
	HTML_DESCRIPTION(s, "If this is checked, the product will not stick to the account after activation.  Good for one-time purchases, microtransactions, etc.  Do not check if the product grants permissions.");
	formAppendCheckBox(s, "dontAssociate", product ? (product->uFlags & PRODUCT_DONT_ASSOCIATE) : false);

	HTML_HEADING(s, "Mark Account Billed On Activation");
	HTML_DESCRIPTION(s, "If this is checked, the product will mark the account billed when it is activated. Use for products that will be sold outside of the Cryptic Account Server's knowledge.");
	formAppendCheckBox(s, "markBilled", product ? (product->uFlags & PRODUCT_MARK_BILLED) : false);

	HTML_HEADING(s, "Key/Value Changes");
	HTML_DESCRIPTION(s, "Comma-separated list of key value changes that take place upon product activation.  It currently supports \"+=\" and \"=\" only (but does support negative numbers).  Example: \"CrypticBucks += 50\".");
	formAppendEdit(s, 60, "keyValueChanges", product ? product->pKeyValueChangesString : "");

	HTML_HEADING(s, "Item ID");
	HTML_DESCRIPTION(s, "Freeform string used for microtransaction items.  It only means something to a game server, and can contain any string.");
	formAppendEdit(s, 60, "itemID", product ? product->pItemID : "");

	HTML_HEADING(s, "Prices");
	HTML_DESCRIPTION(s, "Comma-separated OR list of prices (not AND).  Example: \"19.29 USD, 2 _SomeKey\".  If a currency begins with an underscore, it is assumed to mean a key value instead (with the underscore being stripped).");

	if (product)
		buildProductPriceString(&priceString, product);
	
	formAppendEdit(s, 60, "prices", priceString);

	HTML_HEADING(s, "Tax Classification");
	HTML_DESCRIPTION(s, "Tax classification used for real-money purchases.");

	formAppendEnum(s, "taxClassification", taxClassificationEnumString(), product ? product->eTaxClassification : 0);

	HTML_HEADING(s, "Prerequisites");
	HTML_DESCRIPTION(s, "Key value requirements that must be met before the product can be activated.  If the result of the expression is a non-empty string that does not contain only \"0\", it will allow the purchase of the product.  Supports quoted literals.  Negative literals must be quoted.  Operations must be separated by spaces.");
	HTML_DESCRIPTION(s, "Example: \"canBuyShoes && !shoes1 && !shoes2 && shoesBought < 5 && shoesBought + itemsBought < itemBoughtCap\"");
	estrConcatf(s, FMT_DESCRIPTION, product &&
		product->pPrerequisitesHuman &&
		strlen(product->pPrerequisitesHuman) > 0 &&
		eaSize(&product->ppPrerequisites) == 1 ? " (Format Parsing Error!)" : "");
	formAppendEdit(s, 60, "prerequisites", product ? product->pPrerequisitesHuman : "");

	HTML_HEADING(s, "Days Granted");
	HTML_DESCRIPTION(s, "If this product is a game card, this is the number of days it will grant.  If this product grants an internal subscription, this is how many days it lasts (0 for forever).");
	estrPrintf(&daysGranted, "%d", product ? product->uDaysGranted : 0);
	formAppendEdit(s, 20, "daysGranted", daysGranted);

	HTML_HEADING(s, "Internal Subscription Name Granted");
	HTML_DESCRIPTION(s, "An optional internal subscription name to grant on product activation (internal only).  Good for lifetime or trial subs.");
	formAppendEdit(s, 20, "subGranted", product ? product->pInternalSubGranted : "");

	HTML_HEADING(s, "Activation Key Prefix");
	HTML_DESCRIPTION(s, "If this field is populated, a key with the given prefix will be distributed to the account upon the activation of this product.");
	formAppendEdit(s, 20, "activationKeyPrefix", product ? product->pActivationKeyPrefix : "");

	HTML_HEADING(s, "Expiration");
	HTML_DESCRIPTION(s, "If this field is non-zero, it is the number of days the product will be associated with the account.  It will then be disassociated with the account.");
	formAppendEdit(s, 20, "expireDays", STACK_SPRINTF("%d", product ? product->uExpireDays : 0));

	HTML_HEADING(s, "X-Box Offer ID");
	formAppendEdit(s, 20, "xboxOfferID", STACK_SPRINTF("% llu", product ? product->xbox.qwOfferID : 0));

	HTML_HEADING(s, "X-Box Content ID");
	HTML_DESCRIPTION(s, STACK_SPRINTF("Hexadecimal string of length: %d", XBOX_CONTENTID_SIZE * 2));
	{
		char *hexStr = product ? binStrToHexStr(product->xbox.contentId, XBOX_CONTENTID_SIZE) : NULL;

		formAppendEdit(s, XBOX_CONTENTID_SIZE * 2, "xboxContentID", product ? hexStr : XBOX_CONTENTID_DEFAULT);

		if (hexStr)
			free(hexStr);
	}

	HTML_HEADING(s, "Recruit Upgraded Product");
	HTML_DESCRIPTION(s, "The recruiter will get the following product(s) (comma-separated) when a recruit who has activated this product using a key upgrades (buys the game).");
	formAppendEdit(s, 20, "recruitUpgraded", product ? arrayToString(product->recruit.eaUpgradedProducts) : "");

	HTML_HEADING(s, "Recruit Billed Product");
	HTML_DESCRIPTION(s, "The recruiter will get the following product(s) (comma-separated) when a recruit who has activated this product using a key gets billed for the first time.");
	formAppendEdit(s, 20, "recruitBilled", product ? arrayToString(product->recruit.eaBilledProducts) : "");

	HTML_HEADING(s, "Referred Bonus");
	HTML_DESCRIPTION(s, "Product name of the product given to a referrer as a bonus for referring somebody to this product.");
	formAppendEdit(s, 20, "referredProduct", product ? product->pReferredProduct : "");

	HTML_HEADING(s, "No Spending Cap");
	HTML_DESCRIPTION(s, "If checked, this product will not contribute toward a spending cap.");
	formAppendCheckBox(s, "noSpendingCap", product ? product->bNoSpendingCap : false);
}

static void wiProductView(DEFAULT_HTTPGET_PARAMS)
{
	static char *s = NULL;
	EARRAY_OF(ProductContainer) ppProducts = getProductList(PRODUCTS_ALL);
	int i,size;

	estrCopy2(&s, "");

	appendPaginatedFancyTableHeader(&s, 8, "Name", "Categories", "Internal Name", "Shards", "Permissions", "Required Subs", "Sync", "Access Level");
	size = eaSize(&ppProducts);
	for (i=0; i<size; i++)
	{
		char *shards = NULL;
		char *permissions = NULL;
		char *reqsubs = NULL;
		char buffer[MAX_PATH*2];
		char alvl[10];
		int j,size2 = eaSize(&ppProducts[i]->ppShards);

		for (j=0; j<size2; j++)
		{
			estrConcatf(&shards, "%s%s", j ? "," : "", ppProducts[i]->ppShards[j]);
		}

		size2 = eaSize(&ppProducts[i]->ppRequiredSubscriptions);
		for (j=0; j<size2; j++)
		{
			estrConcatf(&reqsubs, "%s%s", j ? "," : "", ppProducts[i]->ppRequiredSubscriptions[j]);
		}

		concatPermissionString(ppProducts[i]->ppPermissions, &permissions);
		sprintf(buffer, "<a href=\"productDetail?product=%s\">%s</a>", ppProducts[i]->pName, ppProducts[i]->pName);
		sprintf(alvl, "%d", ppProducts[i]->uAccessLevel);

		appendFancyTableEntry(&s, i%2, 8, buffer, ppProducts[i]->pCategoriesString, ppProducts[i]->pInternalName, shards, permissions, reqsubs,
			(ppProducts[i]->uFlags & PRODUCT_BILLING_SYNC) ? "Yes" : "No", alvl);
		estrDestroy(&shards);
		estrDestroy(&permissions);
		estrDestroy(&reqsubs);
	}
	appendFancyTableFooter(&s);

	estrConcatf(&s, "<div class=\"heading\">Create New Product</div>\n");
	wiFormAppendStart(&s, "/productCreate", "POST", "productcreate", NULL);
	estrConcatf(&s, "<div class=\"heading\">Product Name* (string ID/alias)</div>\n");
	formAppendEdit(&s, 30, "name", "");
	
	wiAppendProductForm(&s, NULL);

	estrConcatf(&s, "<div class=\"heading\">");
	formAppendSubmit(&s, "Create Product");
	estrConcatf(&s, "</div>\n");
	formAppendEnd(&s);

	httpSendWrappedString(link, s, NULL);
	eaDestroy(&ppProducts); // DO NOT FREE CONTENTS
}

static void wiProductDetail(DEFAULT_HTTPGET_PARAMS)
{
	static char *s;
	char *productName = NULL;
	const ProductContainer *product = NULL;
	int iEdited = -1;
	int i;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "product") == 0)
		{
			productName = values[i];
		}
		else if (stricmp(args[i], "edit") == 0)
		{
			iEdited = atoi(values[i]);
		}
	}
	product = productName ? findProductByName(productName) : NULL;

	if (!product)
	{
		wiRedirectf(link, "/productView");
		return;
	}

	estrCopy2(&s, "");

	estrConcatf(&s, "<a href=\"productView\">Return to Products List</a>");
	if (iEdited != -1)
	{
		if (iEdited == 0)
			estrConcatf(&s, "<div class=\"error\">Product Successfully Edited.</div>\n",);
		else
			estrConcatf(&s, "<div class=\"error\">Product Edit Failed.</div>\n",);
	}
	wiFormAppendStart(&s, "/productEdit", "POST", "productcreate", NULL);
	estrConcatf(&s, "<div class=\"heading\">Product Name* (string ID/alias)</div>\n%s\n", product->pName);
	formAppendHidden(&s, "name", product->pName);

	HTML_HEADING(&s, "Numeric ID");
	HTML_DESCRIPTION(&s, "Internal numeric ID.");
	estrConcatf(&s, "%d\n", product->uID);

	wiAppendProductForm(&s, product);

	estrConcatf(&s, "<div class=\"heading\">");
	formAppendSubmit(&s, "Save Changes");
	estrConcatf(&s, "</div>\n");
	formAppendEnd(&s);

	wiAppendProductLocalization(&s, product);
	wiAppendProductKeyValues(&s, product);

	httpSendWrappedString(link, s, pCookies);
}

static bool arrayOfProductNamesIsValid(CONST_STRING_EARRAY eaProductNames)
{
	EARRAY_CONST_FOREACH_BEGIN(eaProductNames, iCurName, iNumNames);
	{
		const char *pProductName = eaProductNames[iCurName];
		const ProductContainer *pProduct = NULL;

		if (!(pProductName && *pProductName))
		{
			return false;
		}

		pProduct = findProductByName(pProductName);

		if (!pProduct)
		{
			return false;
		}
	}
	EARRAY_FOREACH_END;

	return true;
}

static const char * getPrereqFailMessage(SA_PARAM_NN_STR const char *pInfix, RPNParseResult eParseResult, int iErrorIndex)
{
	static char *pError = NULL;
	estrClear(&pError);
	estrPrintf(&pError, "Invalid key-value prerequisites: %s",
		StaticDefineIntRevLookupNonNull(RPNParseResultEnum, eParseResult));
	estrConcatf(&pError, "<pre style=\"color: red; font-weight: bold\">%s\n", pInfix);
	if (iErrorIndex != -1)
	{
		if (iErrorIndex > 0)
		{
			estrConcatCharCount(&pError, '-', iErrorIndex);
		}
		estrConcatChar(&pError, '^');
	}
	estrConcatf(&pError, "</pre>");
	return pError;
}

static void wiProductEdit(DEFAULT_HTTPPOST_PARAMS)
{
	const char *name = urlFindValue(args, "name");
	const char *internalName = urlFindValue(args, "internal");
	const char *shardString = urlFindValue(args, "shards");
	const char *permissionString = urlFindValue(args, "permissions");
	const char *reqsubsString = urlFindValue(args, "reqsubs");
	const char *description = urlFindValue(args, "description");
	const char *billingstatementidentifier = urlFindValue(args, "billingstatementidentifier");
	const char *categoriesString = urlFindValue(args, "categories");
	const char *keyValueChangesString = urlFindValue(args, "keyValueChanges");
	const char *itemID = urlFindValue(args, "itemID");
	const char *pricesString = urlFindValue(args, "prices");
	const char *prereqs = urlFindValue(args, "prerequisites");
	U32 accessLevel = atoi(urlFindSafeValue(args, "alvl"));
	int billingSync = atoi(urlFindSafeValue(args, "billingsync"));
	int dontAssociate = atoi(urlFindSafeValue(args, "dontAssociate"));
	int markBilled = atoi(urlFindSafeValue(args, "markBilled"));
	int daysGranted = atoi(urlFindSafeValue(args, "daysGranted"));
	U32 uFlags = 0;
	const char *loggedIn = httpFindAuthenticationUsername(pClientState);
	const char *subGranted = urlFindValue(args, "subGranted");
	const char *subCategory = urlFindValue(args, "subCategory");
	const char *activationKeyPrefix = urlFindValue(args, "activationKeyPrefix");
	TaxClassification eTaxClassification = atoi(urlFindSafeValue(args, "taxClassification"));
	U32 uExpireDays = atoi(urlFindSafeValue(args, "expireDays"));
	U64 qwOfferID = atoui64(urlFindSafeValue(args, "xboxOfferID"));
	const char *contentIDhex = urlFindSafeValue(args, "xboxContentID");
	U8 *contentID = hexStrToBinStr(contentIDhex, XBOX_CONTENTID_SIZE * 2);
	const char *recruitUpgraded = urlFindValue(args, "recruitUpgraded");
	const char *recruitBilled = urlFindValue(args, "recruitBilled");
	const char *referredProduct = urlFindValue(args, "referredProduct");
	bool bRequiresNoSubs = !!atoi(urlFindSafeValue(args, "requiresNoSubs"));
	bool bNoSpendingCap = !!atoi(urlFindSafeValue(args, "noSpendingCap"));

	if (!stricmp(internalName, ACCOUNT_SERVER_INTERNAL_NAME) && !wiAccess(ASAL_GRANT_ACCESS))
	{
		free(contentID);
		wiRedirectf(link, "/productView");
		return;
	}

	if (billingSync)
	{
		if (eTaxClassification == TCNotApplicable)
		{
			httpSendWrappedString(link, "Cannot set a product to be billing-synced with not-applicable tax.", pCookies);
			free(contentID);
			return;
		}

		if (!description || !*description)
		{
			httpSendWrappedString(link, "Cannot set a product to be billing-synced with no description.", pCookies);
			free(contentID);
			return;
		}
	}

	uFlags |= billingSync ? PRODUCT_BILLING_SYNC : 0;
	uFlags |= dontAssociate ? PRODUCT_DONT_ASSOCIATE : 0;
	uFlags |= markBilled ? PRODUCT_MARK_BILLED : 0;

	if (recruitUpgraded && *recruitUpgraded)
	{
		char *pRecruitUpgraded = strdup(recruitUpgraded);
		STRING_EARRAY eaProductNames = NULL;

		DoVariableListSeparation(&eaProductNames, pRecruitUpgraded, false);

		free(pRecruitUpgraded);

		if (!arrayOfProductNamesIsValid(eaProductNames))
		{
			httpSendWrappedString(link, "Invalid recruit upgraded product provided.", pCookies);
			return;
		}
	}

	if (referredProduct && *referredProduct)
	{
		if (!findProductByName(referredProduct)) 
		{
			httpSendWrappedString(link, "Invalid referred product.", pCookies);
			return;
		}
	}

	if (recruitBilled && *recruitBilled)
	{
		char *pRecruitBilled = strdup(recruitBilled);
		STRING_EARRAY eaProductNames = NULL;

		DoVariableListSeparation(&eaProductNames, pRecruitBilled, false);

		free(pRecruitBilled);

		if (!arrayOfProductNamesIsValid(eaProductNames))
		{
			httpSendWrappedString(link, "Invalid recruit billed product provided.", pCookies);
			return;
		}
	}

	if (prereqs && *prereqs)
	{
		STRING_EARRAY eaRPNStack = NULL;
		int iErrorIndex = 0;
		RPNParseResult eParseResult = infixToRPN(prereqs, &eaRPNStack, &iErrorIndex);
		eaDestroyEString(&eaRPNStack);

		if (eParseResult != RPNPR_Success)
		{
			httpSendWrappedString(link, getPrereqFailMessage(prereqs, eParseResult, iErrorIndex), pCookies);
			return;
		}
	}

	if (productSave(name,
		internalName, description, billingstatementidentifier,
		shardString, permissionString, reqsubsString,
		accessLevel, categoriesString,
		keyValueChangesString, itemID, pricesString, prereqs, daysGranted, uFlags, subGranted, subCategory,
		activationKeyPrefix,
		eTaxClassification,
		uExpireDays,
		qwOfferID, contentID,
		recruitUpgraded, recruitBilled,
		referredProduct, bRequiresNoSubs,
		bNoSpendingCap))
	{
		wiRedirectf(link, "/productDetail?product=%s&edit=0", name);
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: productEdit success.");
	}
	else
	{
		wiRedirectf(link, "/productDetail?product=%s&edit=1", name);
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: productEdit failure.");
	}

	free(contentID);

	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: editProduct(%s, %s, %s, %s, %s, %s, %s, %d, %d, '%s'), %s", 
		name, internalName, description, billingstatementidentifier, 
		shardString, permissionString, reqsubsString, 
		billingSync, accessLevel, 
		categoriesString ? categoriesString : "",
		loggedIn ? loggedIn : "Unknown User");
}

static void wiProductCreate(DEFAULT_HTTPPOST_PARAMS)
{
	const char *name = urlFindValue(args, "name");
	const char *internalName = urlFindValue(args, "internal");
	const char *description = urlFindValue(args, "description");
	const char *billingstatementidentifier = urlFindValue(args, "billingstatementidentifier");
	const char *shardString = urlFindValue(args, "shards");
	const char *permissionString = urlFindValue(args, "permissions");
	const char *reqsubsString = urlFindValue(args, "reqsubs");
	const char *categoriesString = urlFindValue(args, "categories");
	const char *keyValueChangesString = urlFindValue(args, "keyValueChanges");
	const char *itemID = urlFindValue(args, "itemID");
	const char *pricesString = urlFindValue(args, "prices");
	const char *prereqs = urlFindValue(args, "prerequisites");
	U32 accessLevel = atoi(urlFindSafeValue(args, "alvl"));
	int billingSync = atoi(urlFindSafeValue(args, "billingsync"));
	int dontAssociate = atoi(urlFindSafeValue(args, "dontAssociate"));
	int markBilled = atoi(urlFindSafeValue(args, "markBilled"));
	int daysGranted = atoi(urlFindSafeValue(args, "daysGranted"));
	U32 uFlags = 0;
	const char *loggedIn = httpFindAuthenticationUsername(pClientState);
	const char *subGranted = urlFindValue(args, "subGranted");
	const char *subCategory = urlFindValue(args, "subCategory");
	const char *activationKeyPrefix = urlFindValue(args, "activationKeyPrefix");
	TaxClassification eTaxClassification = atoi(urlFindSafeValue(args, "taxClassification"));
	U32 uExpireDays = atoi(urlFindSafeValue(args, "expireDays"));
	U64 qwOfferID = atoui64(urlFindSafeValue(args, "xboxOfferID"));
	const char *contentIDhex = urlFindSafeValue(args, "xboxContentID");
	U8 *contentID = hexStrToBinStr(contentIDhex, XBOX_CONTENTID_SIZE * 2);
	const char *recruitUpgraded = urlFindSafeValue(args, "recruitUpgraded");
	const char *recruitBilled = urlFindSafeValue(args, "recruitBilled");
	const char *referredProduct = urlFindValue(args, "referredProduct");
	bool bRequiresNoSubs = !!atoi(urlFindSafeValue(args, "requiresNoSubs"));
	bool bNoSpendingCap = !!atoi(urlFindSafeValue(args, "noSpendingCap"));

	if (!stricmp(internalName, ACCOUNT_SERVER_INTERNAL_NAME) && !wiAccess(ASAL_GRANT_ACCESS))
	{
		wiRedirectf(link, "/productView");
		free(contentID);
		return;
	}

	if (billingSync)
	{
		if (eTaxClassification == TCNotApplicable)
		{
			httpSendWrappedString(link, "Cannot create a billing-synced product with not-applicable tax.", pCookies);
			free(contentID);
			return;
		}

		if (!description || !*description)
		{
			httpSendWrappedString(link, "Cannot create a billing-synced product with no description.", pCookies);
			free(contentID);
			return;
		}
	}


	if (recruitUpgraded && *recruitUpgraded)
	{
		char *pRecruitUpgraded = strdup(recruitUpgraded);
		STRING_EARRAY eaProductNames = NULL;

		DoVariableListSeparation(&eaProductNames, pRecruitUpgraded, false);

		free(pRecruitUpgraded);

		if (!arrayOfProductNamesIsValid(eaProductNames))
		{
			httpSendWrappedString(link, "Invalid recruit upgraded product provided.", pCookies);
			free(contentID);
			return;
		}
	}

	if (referredProduct && *referredProduct)
	{
		if (!findProductByName(referredProduct)) 
		{
			httpSendWrappedString(link, "Invalid referred product.", pCookies);
			free(contentID);
			return;
		}
	}

	if (recruitBilled && *recruitBilled)
	{
		char *pRecruitBilled = strdup(recruitBilled);
		STRING_EARRAY eaProductNames = NULL;

		DoVariableListSeparation(&eaProductNames, pRecruitBilled, false);

		free(pRecruitBilled);

		if (!arrayOfProductNamesIsValid(eaProductNames))
		{
			httpSendWrappedString(link, "Invalid recruit billed product provided.", pCookies);
			free(contentID);
			return;
		}
	}

	if (prereqs && *prereqs)
	{
		STRING_EARRAY eaRPNStack = NULL;
		int iErrorIndex = 0;
		RPNParseResult eParseResult = infixToRPN(prereqs, &eaRPNStack, &iErrorIndex);
		eaDestroyEString(&eaRPNStack);

		if (eParseResult != RPNPR_Success)
		{
			httpSendWrappedString(link, getPrereqFailMessage(prereqs, eParseResult, iErrorIndex), pCookies);
			return;
		}
	}

	uFlags |= billingSync ? PRODUCT_BILLING_SYNC : 0;
	uFlags |= dontAssociate ? PRODUCT_DONT_ASSOCIATE : 0;
	uFlags |= markBilled ? PRODUCT_MARK_BILLED : 0;

	if (name && internalName && *name && *internalName)
	{
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: createProduct(%s, %s, %s, %s, %s, %s, %s, %d, %d, '%s'), %s", 
			name, internalName, description, billingstatementidentifier, 
			shardString, permissionString, reqsubsString, 
			billingSync, accessLevel, 
			categoriesString ? categoriesString : "",
			loggedIn ? loggedIn : "Unknown User");

		if (productSave(name,
			internalName, description, billingstatementidentifier,
			shardString, permissionString, reqsubsString,
			accessLevel, categoriesString,
			keyValueChangesString, itemID, pricesString, prereqs, daysGranted, uFlags, subGranted, subCategory,
			activationKeyPrefix,
			eTaxClassification,
			uExpireDays,
			qwOfferID, contentID,
			recruitUpgraded, recruitBilled,
			referredProduct, bRequiresNoSubs, bNoSpendingCap))
		{
			log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: productAdd success.");
		}
		else
		{
			log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: productAdd failure.");
		}
	}

	wiRedirectf(link, "/productView");
	free(contentID);
}

static void wiAccountAddProduct(DEFAULT_HTTPPOST_PARAMS)
{
	U32 uID = atoi(urlFindSafeValue(args, "id"));
	const char *pProductName = urlFindSafeValue(args, "product");
	const ProductContainer *product = pProductName ? findProductByName(pProductName) : NULL;
	AccountInfo *account = findAccountByID(uID);
	const char *loggedIn = httpFindAuthenticationUsername(pClientState);
	AccountProxyProductActivation *activation = NULL;
	const char *pError = NULL;
	ActivateProductResult eActivationResult = APR_Failure;

	if (!product)
	{
		pError = "Invalid product.";
		goto error;
	}

	if (!account)
	{
		pError = "Invalid account.";
		goto error;
	}

	if (!stricmp(product->pInternalName, ACCOUNT_SERVER_INTERNAL_NAME) && !wiAccess(ASAL_GRANT_ACCESS))
	{
		pError = "Your account permissions do not allow you to give this product.";
		goto error;
	}

	eActivationResult = accountActivateProductLock(account, product, NULL, NULL, NULL, NULL, &activation);
	if (eActivationResult != APR_Success)
	{
		pError = StaticDefineIntRevLookupNonNull(ActivateProductResultEnum, eActivationResult);
		goto error;
	}

	eActivationResult = accountActivateProductCommit(account, product, NULL, 0, activation, NULL, false);
	if (eActivationResult != APR_Success)
	{
		pError = StaticDefineIntRevLookupNonNull(ActivateProductResultEnum, eActivationResult);
		goto error;
	}

	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: addProductToAccount(%s, %s), %s, %s", 
		account->accountName, product->pName, 
		loggedIn ? loggedIn : "Unknown User", "Success");
	wiRedirectf(link, "/detail?accountname=%s", account->accountName);

	return;

error:
	if (pError)
		httpSendWrappedString(link, pError, pCookies);
	else
		wiRedirectf(link, "detail");
}

static void wiAccountRemoveProduct(DEFAULT_HTTPPOST_PARAMS)
{
	U32 uID = atoi(urlFindSafeValue(args, "id"));
	const char *name = urlFindValue(args, "product");
	const ProductContainer *product = name ? findProductByName(name) : NULL;
	AccountInfo *account = findAccountByID(uID);
	char buffer[256];
	const char *loggedIn = httpFindAuthenticationUsername(pClientState);

	if (product && !stricmp(product->pInternalName, ACCOUNT_SERVER_INTERNAL_NAME) && !wiAccess(ASAL_GRANT_ACCESS))
	{
		sprintf(buffer, "/detail?accountname=%s", account->accountName);
		wiRedirectf(link, buffer);
		return;
	}
	
	if (account && product)
	{
		accountRemoveProduct(account, product, "removed via account server web interface");
		accountClearPermissionsCache(account);
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: removeProductFromAccount(%s, %s), %s, %s", 
			account->accountName, product->pName, 
			loggedIn ? loggedIn : "Unknown User", "Success");
		sprintf(buffer, "/detail?accountname=%s", account->accountName);
	}
	else
	{
		sprintf(buffer, "detail");
	}
	wiRedirectf(link, buffer);
}

static void wiAccountAddKeyValuePair(DEFAULT_HTTPPOST_PARAMS)
{
	U32 uID = atoi(urlFindSafeValue(args, "id"));
	const char *value = urlFindSafeValue(args, "value");
	char buffer[256];
	const char *key = urlFindSafeValue(args, "key");
	AccountInfo *account = findAccountByID(uID);
	AccountKeyValueResult result = AKV_FAILURE;

	const char *loggedIn = httpFindAuthenticationUsername(pClientState);
	if (account)
	{
		char *lock = NULL;
		S64 iValue = value ? atoi64(value) : 0;

		result = AccountKeyValue_Set(account, key, iValue, &lock);
		if (result == AKV_SUCCESS)
			result = AccountKeyValue_Commit(account, key, lock);

		estrDestroy(&lock);

		if (result == AKV_SUCCESS)
		{
			log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: setKeyValuePair(%s, %s, %s), %s, %s", 
				account->accountName, key, value,
				loggedIn ? loggedIn : "Unknown User", "Success");
			sprintf(buffer, "/detail?accountname=%s", account->accountName);
		}
		else
		{
			static char *s = NULL;

			estrCopy2(&s, "");
			switch (result)
			{
				case AKV_INVALID_KEY:
					estrConcatf(&s, "Invalid key for <a href=\"detail?accountname=%s\">%s</a>.", account->accountName, account->accountName);
					break;
				case AKV_INVALID_RANGE:
					estrConcatf(&s, "Invalid range for the key %s for <a href=\"detail?accountname=%s\">%s</a>.",
						key, account->accountName, account->accountName);
					break;
				case AKV_LOCKED:
					estrConcatf(&s, "Key/value pair for <a href=\"detail?accountname=%s\">%s</a> is locked.",
						account->accountName, account->accountName);
					break;
				default:
				case AKV_FAILURE:
					estrConcatf(&s, "Could not add key/value pair for <a href=\"detail?accountname=%s\">%s</a>.",
						account->accountName, account->accountName);
					break;
			}

			httpSendWrappedString(link, s, NULL);
			return;
		}
	}
	else if (account)
	{
		static char *s = NULL;

		estrCopy2(&s, "");
		estrConcatf(&s, "Invalid key for <a href=\"detail?accountname=%s\">%s</a>.", account->accountName, account->accountName);
		httpSendWrappedString(link, s, NULL);
		return;
	}
	else
		sprintf(buffer, "detail");
	wiRedirectf(link, buffer);
}

static void wiAccountRemoveKeyValuePair(DEFAULT_HTTPPOST_PARAMS)
{
	U32 uID = atoi(urlFindSafeValue(args, "id"));
	const char *key = urlFindValue(args, "key");
	AccountInfo *account = findAccountByID(uID);
	char buffer[256];

	const char *loggedIn = httpFindAuthenticationUsername(pClientState);
	if (account)
	{
		AccountKeyValue_Remove(account, key);
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: removeKeyValueFromAccount(%s, %s), %s, %s", 
			account->accountName, key,  
			loggedIn ? loggedIn : "Unknown User", "Success");
		sprintf(buffer, "/detail?accountname=%s", account->accountName);
	}
	else
	{
		sprintf(buffer, "detail");
	}
	wiRedirectf(link, buffer);
}

static void wiAccountUnlockKeyValuePair(DEFAULT_HTTPPOST_PARAMS)
{
	U32 uID = atoi(urlFindSafeValue(args, "id"));
	const char *key = urlFindValue(args, "key");
	AccountInfo *account = findAccountByID(uID);
	char buffer[256];

	const char *loggedIn = httpFindAuthenticationUsername(pClientState);
	if (account)
	{
		AccountKeyValue_Unlock(account, key);
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: unlockKeyValueFromAccount(%s, %s), %s, %s", 
			account->accountName, key,  
			loggedIn ? loggedIn : "Unknown User", "Success");
		sprintf(buffer, "/detail?accountname=%s", account->accountName);
	}
	else
	{
		sprintf(buffer, "detail");
	}
	wiRedirectf(link, buffer);
}

// Subscription stuff

static void buildPriceString(SA_PARAM_NN_VALID char **estr, SA_PARAM_NN_VALID const SubscriptionContainer *subscription)
{
	int count = eaSize(&subscription->ppMoneyPrices);
	int i;
	char *amount = 0;

	estrStackCreate(&amount);
	for(i=0; i<count; i++)
	{
		const Money *pPrice = moneyContainerToMoneyConst(subscription->ppMoneyPrices[i]);
		if(*estr && **estr)
			estrConcatf(estr, ", ");

		estrFromMoneyRaw(&amount, pPrice);
		estrConcatf(estr, "%s %s", amount, moneyCurrency(pPrice));
	}
	estrDestroy(&amount);
}

static void wiAppendSubscriptionForm(SA_PARAM_NN_VALID char **s, SA_PARAM_OP_VALID const SubscriptionContainer *pSubscription)
{
	static char *strPrices = NULL;
	static char *strCategories = NULL;
	estrClear(&strPrices);
	estrDestroy(&strCategories);

	if (pSubscription)
		buildPriceString(&strPrices, pSubscription);

	HTML_HEADING(s, "Internal Name*");
	HTML_DESCRIPTION(s, "The internal category of subscription this belongs to. This is used to map products to subscriptions.  Does not need to be unique.");
	formAppendEdit(s, 30, "internal", pSubscription ? pSubscription->pInternalName : "");

	HTML_HEADING(s, "Description");
	HTML_DESCRIPTION(s, "Description of the subscription.");
	formAppendEdit(s, 60, "description", pSubscription ? pSubscription->pDescription : "");

	HTML_HEADING(s, "Billing Statement Identifier");
	HTML_DESCRIPTION(s, "Must follow specific formatting rules.  Not required.");
	formAppendEdit(s, 60, "billingstatementidentifier", pSubscription ? pSubscription->pBillingStatementIdentifier : "");

	HTML_HEADING(s, "Product Name");
	HTML_DESCRIPTION(s, "This must be the name (NOT internal name) of a billing-synced product.  This is to meet a Vindicia requirement and it doesn't matter which product is listed here, as long as it is billing-synced.  Alternatively, if this subscription is a mock product, this is the product to activate.");
	
	if (pSubscription)
	{
		const ProductContainer *pProduct = findProductByName(pSubscription->pProductName);

		if (!pProduct)
		{
			HTML_ERROR(s, "Warning: Product does not exist!");
		}
		else if (!(pProduct->uFlags & PRODUCT_BILLING_SYNC))
		{
			HTML_ERROR(s, "Warning: Product found but not billing synced!");
		}
	}

	formAppendEdit(s, 60, "productname", pSubscription ? pSubscription->pProductName : "");

	HTML_HEADING(s, "Period Type");
	HTML_DESCRIPTION(s, "The term of the subscription.  Cannot be changed after creation.");
	if (!pSubscription)
		formAppendEnum(s, "periodtype", "Year|Month|Day", pSubscription ? pSubscription->periodType : 1);
	else
	{
		estrConcatf(s, "%s", getSubscriptionPeriodName(pSubscription->periodType));
	}

	HTML_HEADING(s, "Period Amount");
	HTML_DESCRIPTION(s, "Duration of term between billings.  Set the type to month and this to 1 for monthly.  Cannot be changed after creation.");
	if (!pSubscription)
		formAppendEditInt(s, 30, "periodamount", 1);
	else
		estrConcatf(s, "%d", pSubscription->iPeriodAmount);

	HTML_HEADING(s, "Initial Free Days");
	HTML_DESCRIPTION(s, "How many initial free days the subscriber may receive if it is their first subscription they've had with the same internal subscription name.");
	formAppendEditInt(s, 30, "initialfreedays", pSubscription ? pSubscription->iInitialFreeDays : 0);

	HTML_HEADING(s, "Prices Per Period");
	HTML_DESCRIPTION(s, "Comma-separated list of prices and currencies. Example: \"15.99 USD, 13.99 CAD\".");
	formAppendEdit(s, 60, "prices", strPrices);

	HTML_HEADING(s, "Game Card Plan");
	HTML_DESCRIPTION(s, "If this is checked, no payment method is required to activate it.  It should also be for exactly 1 day, and cannot be created unless it is created while activating a game card.  Cannot be changed after creation.");
	if (!pSubscription)
		formAppendCheckBox(s, "gamecard", false);
	else
		estrConcatf(s, "%s", pSubscription->gameCard ? "Yes" : "No");

	HTML_HEADING(s, "Mock Product");
	HTML_DESCRIPTION(s, "If this is checked, this is really just a mock product.  Activating this subscription will instead purchase the product listed above.");
	formAppendCheckBox(s, "mockProduct", pSubscription ? pSubscription->uFlags & SUBSCRIPTION_MOCKPRODUCT : false);

	HTML_HEADING(s, "Categories");
	HTML_DESCRIPTION(s, "Comma-separated list of categories this subscription belongs to.  Used by the web site.");
	if (pSubscription)
		strCategories = getSubscriptionCategoryString(pSubscription);
	formAppendEdit(s, 60, "categories", strCategories ? strCategories : "");

	HTML_HEADING(s, "Billed Product");
	HTML_DESCRIPTION(s, "The name of a product a user should receive if they are billed with this subscription plan (invalid for game cards and mock subscriptions).");
	formAppendEdit(s, 20, "billedProduct", pSubscription ? pSubscription->pBilledProductName : "");
}

static void wiSubscriptionView(DEFAULT_HTTPGET_PARAMS)
{
	static char *s = NULL;
	EARRAY_OF(ProductContainer) ppProducts = getProductList(PRODUCTS_ALL);
	EARRAY_OF(SubscriptionContainer) ppSubscriptions = getSubscriptionList();
	int i,size;

	estrCopy2(&s, "");

	appendPaginatedFancyTableHeader(&s, 4, "Subscription Plan", "Internal Name", "Period", "Description");
	size = eaSize(&ppSubscriptions);
	for (i=0; i<size; i++)
	{
		static char *pLink = NULL;
		static char *pPeriod = NULL;

		estrPrintf(&pLink, "<a href=\"subscriptionDetail?subscription=%s\">%s</a>", ppSubscriptions[i]->pName, ppSubscriptions[i]->pName);
		estrPrintf(&pPeriod, "%d %s", ppSubscriptions[i]->iPeriodAmount, getSubscriptionPeriodName(ppSubscriptions[i]->periodType));

		appendFancyTableEntry(&s, i%2, 4,
			pLink, ppSubscriptions[i]->pInternalName,
			pPeriod, ppSubscriptions[i]->pDescription);
	}
	appendFancyTableFooter(&s);

	HTML_HEADING(&s, "Create New Subscription Plan");
	wiFormAppendStart(&s, "/subscriptionCreate", "POST", "subscriptioncreate", NULL);

	HTML_HEADING(&s, "Subscription Plan Name*");
	formAppendEdit(&s, 30, "name", "");

	wiAppendSubscriptionForm(&s, NULL);

	estrConcatf(&s, "<div class=\"heading\">");
	formAppendSubmit(&s, "Create Subscription Plan");
	estrConcatf(&s, "</div>\n");
	formAppendEnd(&s);

	httpSendWrappedString(link, s, NULL);
	eaDestroy(&ppSubscriptions); // DO NOT FREE CONTENTS
	eaDestroy(&ppProducts); // DO NOT FREE CONTENTS
}

static void wiAppendSubscriptionLocalization(SA_PARAM_NN_VALID char **s, SA_PARAM_OP_VALID const SubscriptionContainer *pSubscription)
{
	if (!pSubscription) return;

	HTML_HEADING(s, "Localization");
	HTML_DESCRIPTION(s, "Localized strings that are to be displayed to users.");

	appendFancyTableHeader(s, 4, "IETF Language Tag", "Name", "Description", "Action");
	EARRAY_CONST_FOREACH_BEGIN(pSubscription->ppLocalizedInfo, i, size);
	{
		SubscriptionLocalizedInfo *pInfo = pSubscription->ppLocalizedInfo[i];
		static char *pSubID = NULL;

		estrPrintf(&pSubID, "%d", pSubscription->uID);
		
		wiFormAppendStart(s, "/unlocalizeSubscription", "post", "Unlocalize", NULL);
		formAppendHidden(s, "subscriptionID", pSubID);
		formAppendHidden(s, "languageTag", pInfo->pLanguageTag);
		appendFancyTableEntry(s, i%2, 4,
			pInfo->pLanguageTag,
			pInfo->pName,
			pInfo->pDescription,
			HTML_INSERT_SUBMIT("Remove"));
		formAppendEnd(s);
	}
	EARRAY_FOREACH_END;

	wiFormAppendStart(s, "/localizeSubscription", "post", "Localize", NULL);
	formAppendHidden(s, "subscriptionID", STACK_SPRINTF("%d", pSubscription->uID));
	appendFancyTableEntry(s, false, 4,
		HTML_INSERT_TEXTINPUT("languageTag", ""),
		HTML_INSERT_TEXTINPUT("name", ""),
		HTML_INSERT_TEXTINPUT("description", ""),
		HTML_INSERT_SUBMIT("Add/Replace"),
		HTML_INSERT_FORM_END());
	formAppendEnd(s);

	appendFancyTableFooter(s);
}

static void wiSubscriptionDetail(DEFAULT_HTTPGET_PARAMS)
{
	static char *s, *permissions, *shards;
	int i;
	char *subscriptionName = NULL;
	const SubscriptionContainer *subscription = NULL;
	int iEdited = -1;
	char *strPrices = NULL;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "subscription") == 0)
		{
			subscriptionName = values[i];
		}
		else if (stricmp(args[i], "edit") == 0)
		{
			iEdited = atoi(values[i]);
		}
	}

	if (subscriptionName) subscription = findSubscriptionByName(subscriptionName);

	if (!subscription)
	{
		wiRedirectf(link, "/subscriptionView");
		return;
	}

	estrCopy2(&s, "");
	estrCopy2(&permissions, "");
	estrCopy2(&shards, "");

	estrConcatf(&s, "<a href=\"subscriptionView\">Return to Subscription Plan List</a>");
	if (iEdited != -1)
	{
		if (iEdited == 0)
			estrConcatf(&s, "<div class=\"error\">Subscription Plan Successfully Edited.</div>\n",);
		else
			estrConcatf(&s, "<div class=\"error\">Subscription Plan Edit Failed.</div>\n",);
	}
	wiFormAppendStart(&s, "/subscriptionEdit", "POST", "subscriptioncreate", NULL);
	estrConcatf(&s, "<div class=\"heading\">Subscription Plan Name</div>\n%s\n", subscription->pName);
	formAppendHidden(&s, "name", subscription->pName);

	wiAppendSubscriptionForm(&s, subscription);

	estrConcatf(&s, "<div class=\"heading\">");
	formAppendSubmit(&s, "Save Subscription Plan");
	estrConcatf(&s, "</div>\n");
	formAppendEnd(&s);

	wiAppendSubscriptionLocalization(&s, subscription);

	httpSendWrappedString(link, s, pCookies);
	estrDestroy(&strPrices);
}

static void wiSubscriptionEditCB(SA_PARAM_OP_VALID const SubscriptionContainer *pSubscription, bool bSuccess, SA_PARAM_OP_VALID NetLink *link,
								 SA_PARAM_OP_STR const char *pReason)
{
	char buffer[256];
	if (bSuccess && pSubscription)
	{
		sprintf(buffer, "/subscriptionDetail?subscription=%s&edit=0", pSubscription->pName);
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: subscriptionEdit success.");
	}
	else if (pSubscription)
	{
		sprintf(buffer, "/subscriptionDetail?subscription=%s&edit=1", pSubscription->pName);
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: subscriptionEdit failure: %s", NULL_TO_EMPTY(pReason));
	}
	else
	{
		sprintf(buffer, "/subscriptionView");
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: subscriptionEdit failure: %s", NULL_TO_EMPTY(pReason));
	}
	
	wiRedirectf(link, buffer);
}

static void wiSubscriptionEdit(DEFAULT_HTTPPOST_PARAMS)
{
	const char *name = urlFindValue(args, "name");
	const char *internalName = urlFindValue(args, "internal");
	const char *description = urlFindValue(args, "description");
	const char *billingstatementidentifier = urlFindValue(args, "billingstatementidentifier");
	const char *productName = urlFindValue(args, "productname");
	const char *initialfreedays = urlFindSafeValue(args, "initialfreedays");
	const char *prices = urlFindValue(args, "prices");
	const char *loggedIn = httpFindAuthenticationUsername(pClientState);
	const char *categories = urlFindValue(args, "categories");
	const char *billedProduct = urlFindValue(args, "billedProduct");
	bool bMockProduct = atoi(urlFindSafeValue(args, "mockProduct"));
	U32 uFlags = 0;

	uFlags = bMockProduct ? uFlags | SUBSCRIPTION_MOCKPRODUCT : uFlags;

	if (billedProduct && *billedProduct)
	{
		const ProductContainer *pProduct = findProductByName(billedProduct);
		if (!pProduct)
		{
			httpSendWrappedString(link, "Billed product does not exist.", pCookies);
		}
	}

	subscriptionEdit(name, internalName, description, billingstatementidentifier, productName, atoi(initialfreedays), prices,
		categories, uFlags, billedProduct, wiSubscriptionEditCB, link);

	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: subscriptionEdit(%s, %s, %s, %s, %s, %s, %s), %s", 
		name, internalName, description, billingstatementidentifier, 
		initialfreedays, prices, categories,
		loggedIn ? loggedIn : "Unknown User");
}

static void wiSubscriptionCreateCB(SA_PARAM_OP_VALID const SubscriptionContainer *pSubscription, bool bSuccess, SA_PARAM_OP_VALID NetLink *link,
								   SA_PARAM_OP_STR const char *pReason)
{
	if (bSuccess)
	{
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: subscriptionAdd success.");
	}
	else
	{
		char *s = NULL;
		estrStackCreate(&s);
		estrPrintf(&s, "Unable to create subscription: %s\n", NULL_TO_EMPTY(pReason));
		httpSendWrappedString(link, s, NULL);
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: subscriptionAdd failure: %s", NULL_TO_EMPTY(pReason));
		estrDestroy(&s);
		return;
	}

	wiRedirectf(link, "/subscriptionView");
}

static void wiSubscriptionCreate(DEFAULT_HTTPPOST_PARAMS)
{
	const char *name = urlFindValue(args, "name");
	const char *internalName = urlFindValue(args, "internal");
	const char *description = urlFindValue(args, "description");
	const char *billingstatementidentifier = urlFindValue(args, "billingstatementidentifier");
	const char *productName = urlFindValue(args, "productname");
	const char *periodtype = urlFindSafeValue(args, "periodtype");
	const char *periodamount = urlFindSafeValue(args, "periodamount");
	const char *initialfreedays = urlFindSafeValue(args, "initialfreedays");
	const char *prices = urlFindValue(args, "prices");
	const char *billedProduct = urlFindValue(args, "billedProduct");
	SubscriptionPeriodType ePeriodType = atoi(periodtype);
	int gameCard = atoi(urlFindSafeValue(args, "gamecard"));
	const char *categories = urlFindValue(args, "categories");
	bool bMockProduct = atoi(urlFindSafeValue(args, "mockProduct"));
	U32 uFlags = 0;
	const char *loggedIn = httpFindAuthenticationUsername(pClientState);

	uFlags = bMockProduct ? uFlags | SUBSCRIPTION_MOCKPRODUCT : uFlags;

	if (billedProduct && *billedProduct)
	{
		const ProductContainer *pProduct = findProductByName(billedProduct);
		if (!pProduct)
		{
			httpSendWrappedString(link, "Billed product does not exist.", pCookies);
		}
	}

	if (name && internalName && *name && *internalName)
	{
		subscriptionAdd(name, internalName, description, billingstatementidentifier, productName, 
			ePeriodType, atoi(periodamount), atoi(initialfreedays), prices, gameCard, categories, 
			uFlags, billedProduct, wiSubscriptionCreateCB, link);
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Web: subscriptionAdd(%s, %s, %s, %s, %s, %s, %s, %s, %d, %d, %s), %s", 
			name, internalName, description, billingstatementidentifier, 
			periodtype, periodamount, initialfreedays, prices, 
			ePeriodType, gameCard, categories,
			loggedIn ? loggedIn : "Unknown User");
	}
	else
	{
		wiRedirectf(link, "/subscriptionView");
	}
}

// ------------------------------------------------------
// Machine ID

void wiMachineLockEnable(DEFAULT_HTTPGET_PARAMS)
{
	int i;
	U32 accountID = 0;
	bool bEnable = false;
	AccountInfo *pAccount;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "accountid") == 0)
		{
			accountID = atoi(values[i]);
		}
		else if (stricmp(args[i], "flag") == 0)
		{
			bEnable = atoi(values[i]);
		}
	}
	pAccount = findAccountByID(accountID);
	if (!pAccount)
		return;
	accountMachineLockingEnable(pAccount, bEnable);
	if (strstri(pReferer, "detail"))
		wiRedirectf(link, "/detail?accountName=%s", pAccount->accountName);
	else
		wiRedirectf(link, "/");
}

void wiMachineLockSaveNext(DEFAULT_HTTPGET_PARAMS)
{
	int i;
	U32 accountID = 0;
	bool bEnable = false;
	AccountInfo *pAccount;
	MachineType eType = MachineType_All;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "accountid") == 0)
		{
			accountID = atoi(values[i]);
		}
		else if (stricmp(args[i], "flag") == 0)
		{
			bEnable = atoi(values[i]);
		}
		else if (stricmp(args[i], "type") == 0)
		{
			eType = atoi(values[i]);
		}
	}
	pAccount = findAccountByID(accountID);
	if (!pAccount)
		return;
	if (accountMachineLockingIsEnabled(pAccount))
		accountMachineLockingSaveNext(pAccount, bEnable, eType, false);
	if (strstri(pReferer, "detail"))
		wiRedirectf(link, "/detail?accountName=%s", pAccount->accountName);
	else
		wiRedirectf(link, "/");
}

void wiSavedMachineRemove(DEFAULT_HTTPGET_PARAMS)
{
	int i;
	U32 accountID = 0;
	const char *pMachineID = NULL;
	AccountInfo *pAccount;
	MachineType eType = MachineType_CrypticClient;

	for (i=0; i<count; i++)
	{
		if (stricmp(args[i], "accountid") == 0)
		{
			accountID = atoi(values[i]);
		}
		else if (stricmp(args[i], "machineid") == 0)
		{
			pMachineID = values[i];
		}
		else if (stricmp(args[i], "type") == 0)
		{
			eType = atoi(values[i]);
		}
	}
	pAccount = findAccountByID(accountID);
	if (!pAccount || nullStr(pMachineID))
		return;
	accountRemoveSavedMachine(pAccount, pMachineID, eType);
	if (strstri(pReferer, "detail"))
		wiRedirectf(link, "/detail?accountName=%s", pAccount->accountName);
	else
		wiRedirectf(link, "/");
}

// ------------------------------------------------------
// HTTP Message Handling


static void httpRequestComplete(DWORD start)
{
	servLog(LOG_ACCOUNT_SERVER_WEB, "WebInterface", "time %lu", timeGetTime() - start);
}

void httpLegacyHandlePost(NetLink *link, HttpClientStateDefault *pClientState)
{
	char *pErr  = NULL;
	char *arg   = NULL;
	char *value = NULL;
	char *pReferer = NULL;
	char *url = NULL;
	bool setCookies = false;
	CookieList *pCookies;
	UrlArgumentList *arglist;
	char *pAuth = NULL;
	int iAccessLevel;
	DWORD start;

	PERFINFO_AUTO_START_FUNC();

	// Start timing.
	start = timeGetTime();

	pCookies = StructCreate(parse_CookieList);

	httpParseHeaderAuth(pClientState->pPostHeaderString, "POST", &url, &pReferer, pCookies, &pAuth);
	iAccessLevel = httpFindAuthenticationAccessLevel(link, pAuth ? cryptPasswordHashString(pAuth) : 0, NULL);
	estrDestroy(&pAuth);

	// Record this request.
	log_printf(LOG_ACCOUNT_SERVER_WEB, "HTTP POST: URL: \"%s\", Referrer: \"%s\", Auth: \"%s\", Body: \"%s\"", billingRedact(NULL_TO_EMPTY(url)),
		billingRedact(NULL_TO_EMPTY(pReferer)), NULL_TO_EMPTY(httpFindAuthenticationUsername(pClientState)),
		billingRedact(NULL_TO_EMPTY(pClientState->pPostDataString)));

	if(!url)
	{
		estrDestroy(&pReferer);
		StructDestroy(parse_CookieList, pCookies);

		httpRequestComplete(start);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if(iAccessLevel < 0)
	{
		estrDestroy(&pReferer);
		StructDestroy(parse_CookieList, pCookies);
		estrDestroy(&url);
		httpSendAuthRequiredHeader(link);

		httpRequestComplete(start);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	setCurrentAccessLevel(iAccessLevel);

	arglist = urlToUrlArgumentList(pClientState->pPostDataString);

	if (strEndsWith(url, "/batchCreate") && wiAccess(ASAL_KEY_ACCESS))
	{
		wiProductKeyBatchCreationPost(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/batchCreateAction"))
	{
		wiCreateProductKeys(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (isAccountServerMode(ASM_KeyGenerating))
	{
		// everything below is invalid for key generation mode
		httpSendWrappedString(link, "Unknown Command or filename.", pCookies);
	}
	else if (strEndsWith(url, "/keyInvalidate") && wiAccess(ASAL_KEY_ACCESS))
	{
		wiKeyInvalidate(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/batchInvalidate") && wiAccess(ASAL_KEY_ACCESS))
	{
		wiBatchInvalidate(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/batchDistributed") && wiAccess(ASAL_KEY_ACCESS))
	{
		wiBatchDistributed(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/create"))
	{
		wiCreateAccount(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/changePassword"))
	{
		wiChangePassword(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/changeAddress"))
	{
		wiChangeAddress(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/detail"))
	{
		wiAccountDetail(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/keygroupCreate") && wiAccess(ASAL_KEY_ACCESS))
	{
		wiCreateProductKeyGroup(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/activateKey"))
	{
		wiProductKeyActivate(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/distributeKey") && wiAccess(ASAL_KEY_ACCESS))
	{
		wiProductKeyDistribute(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/productCreate"))
	{
		wiProductCreate(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/productEdit"))
	{
		wiProductEdit(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/accountAddProduct"))
	{
		wiAccountAddProduct(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/accountRemoveProduct"))
	{
		wiAccountRemoveProduct(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/accountAddKeyValuePair"))
	{
		wiAccountAddKeyValuePair(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/accountRemoveKeyValuePair"))
	{
		wiAccountRemoveKeyValuePair(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/accountUnlockKeyValuePair"))
	{
		wiAccountUnlockKeyValuePair(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/subscriptionCreate"))
	{
		wiSubscriptionCreate(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/subscriptionEdit"))
	{
		wiSubscriptionEdit(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/updatePaymentMethod"))
	{
		wiUpdatePaymentMethod(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/localizeProduct"))
	{
		wiLocalizeProduct(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/unlocalizeProduct"))
	{
		wiUnlocalizeProduct(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/localizeSubscription"))
	{
		wiLocalizeSubscription(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/unlocalizeSubscription"))
	{
		wiUnlocalizeSubscription(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/setProductKeyValue"))
	{
		wiSetProductKeyValue(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/productKeyUnlock"))
	{
		wiProductKeyUnlock(link, pClientState, arglist, pReferer, pCookies);
	}
	else if (strEndsWith(url, "/keygroupSetProductList"))
	{
		wiKeyGroupSetProductList(link, pClientState, arglist, pReferer, pCookies);
	}
	else
	{
		httpSendWrappedString(link, "Access denied.  You may have an insufficient access level.", pCookies);
	}

	StructDestroy(parse_CookieList, pCookies);
	StructDestroy(parse_UrlArgumentList, arglist);
	estrDestroy(&pReferer);
	estrDestroy(&url);

	httpRequestComplete(start);
	PERFINFO_AUTO_STOP_FUNC();
}

void httpLegacyHandleGet(char *data, NetLink *link, HttpClientStateDefault *pClientState)
{
	char	*url_esc = NULL,*args[100] = {0},*values[100] = {0};
	int		count, i;
	char *pReferer = NULL;
	CookieList *pCookies;
	char *pAuth = NULL;
	int iAccessLevel;
	U32 start;

	PERFINFO_AUTO_START_FUNC();

	// Start timing.
	start = timeGetTime();

	pCookies = StructCreate(parse_CookieList);

	httpParseHeaderAuth(data, "GET", &url_esc, &pReferer, pCookies, &pAuth);
	iAccessLevel = httpFindAuthenticationAccessLevel(link, pAuth ? cryptPasswordHashString(pAuth) : 0, NULL);
	estrDestroy(&pAuth);

	// Record this request.
	log_printf(LOG_ACCOUNT_SERVER_WEB, "HTTP GET: URL: \"%s\", Referrer: \"%s\", Auth: \"%s\"", billingRedact(NULL_TO_EMPTY(url_esc)),
		billingRedact(NULL_TO_EMPTY(pReferer)), NULL_TO_EMPTY(httpFindAuthenticationUsername(pClientState)));

	if (!url_esc)
	{
		estrDestroy(&pReferer);
		StructDestroy(parse_CookieList, pCookies);

		httpRequestComplete(start);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	
	if(iAccessLevel < 0)
	{
		estrDestroy(&pReferer);
		estrDestroy(&url_esc);
		StructDestroy(parse_CookieList, pCookies);
		httpSendAuthRequiredHeader(link);

		httpRequestComplete(start);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	setCurrentAccessLevel(iAccessLevel);

	count = urlToArgs(url_esc,args,values,ARRAY_SIZE(args));

	if (strEndsWith(args[0],"/"))
	{
		if (isAccountServerMode(ASM_KeyGenerating))
			wiRedirectf(link, "/keygroupList");
		else
		wiMainPage(link, pReferer, pCookies);
	}
	else if (isAccountServerMode(ASM_KeyGenerating) && strEndsWith(args[0], "/keygroupUpdate") && wiAccess(ASAL_KEY_ACCESS))
	{
		initializeProductKeyCreation();
		wiRedirectf(link, "/keygroupList");
	}
	else if (strEndsWith(args[0], "/keygroupView") && wiAccess(ASAL_KEY_ACCESS))
	{
		wiProductKeyGroupView(link, pClientState, count, args, values, pReferer, pCookies);
	} 
	else if (strEndsWith(args[0], "/keygroupList") && wiAccess(ASAL_KEY_ACCESS))
	{
		wiProductKeyGroupList(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/keyView"))
	{
		wiKeyView(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/productView"))
	{
		wiProductView(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/productDetail"))
	{
		wiProductDetail(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/subscriptionView"))
	{
		wiSubscriptionView(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/subscriptionDetail"))
	{
		wiSubscriptionDetail(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/productSubscriptions"))
	{
		wiProductSubscriptionView(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (isAccountServerMode(ASM_KeyGenerating))
	{
		if (!httpSendFileSafe(link, args[0]))
		{
			httpSendWrappedString(link, "This entity is unavailable in key generation mode.", pCookies);
		}
		// everything below is invalid for key generation mode
	}
	else if (strEndsWith(args[0], "/search"))
	{
		wiAccountsSearch(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/create"))
	{
		wiCreateAccountBlank(link, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/changePassword"))
	{
		wiChangePasswordBlank(link, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/editPaymentMethod"))
	{
		wiEditPaymentMethod(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/deleteAccount"))
	{
		wiDeleteAccountBlank(link, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/refreshPermissionCache"))
	{
		wiRefreshPermissionCache(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/refreshSubscriptionCache"))
	{
		wiRefreshSubscriptionCache(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/removeExpected"))
	{
		wiRemoveExpected(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/detail"))
	{
		wiAccountDetailBlank(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/activityLog"))
	{
		wiAccountActivityLog(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/internal"))
	{
		wiToggleInternalUseOnlyFlag(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/disableLoginFlag"))
	{
		wiToggleLoginDisabledFlag(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/batchCreate") && wiAccess(ASAL_KEY_ACCESS))
	{
		wiProductKeyBatchCreation(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/batchView") && wiAccess(ASAL_KEY_ACCESS))
	{
		wiProductKeyBatchView(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/keygroupCreate") && wiAccess(ASAL_KEY_ACCESS))
	{
		wiProductKeyGroupCreation(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/batchDownload") && wiAccess(ASAL_KEY_ACCESS))
	{
		wiProductKeyBatchDownload(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/stats"))
	{
		wiStats(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/logout"))
	{
		httpSendAuthRequiredHeader(link);
	}
	else if (strEndsWith(args[0], "/subHistory"))
	{
		wiSubHistory(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/recalculateSubHistory"))
	{
		wiRecalculateSubHistory(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/toggleSubHistory"))
	{
		wiToggleSubHistory(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/machinelocknext"))
	{
		wiMachineLockSaveNext(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/machinelock"))
	{
		wiMachineLockEnable(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (strEndsWith(args[0], "/removemachine"))
	{
		wiSavedMachineRemove(link, pClientState, count, args, values, pReferer, pCookies);
	}
	else if (!httpSendFileSafe(link, args[0]))
	{
		httpSendWrappedString(link, "Access denied.  You may have an insufficient access level.", pCookies);
	}
	StructDestroy(parse_CookieList, pCookies);

	estrDestroy(&url_esc);
	estrDestroy(&pReferer);
	for (i=0; i<ARRAY_SIZE(args); i++)
	{
		estrDestroy(&args[i]);
		estrDestroy(&values[i]);
	}

	httpRequestComplete(start);
	PERFINFO_AUTO_STOP_FUNC();
}

// ----------------------------------------------------------------------------------
// File sending code

static int httpDisconnect(NetLink* link, HttpClientStateDefault *pClientState)
{
	LinkState *pLinkState = findLinkState(link);

	httpDefaultDisconnectHandler(link, pClientState);

	if(!pLinkState)
	{
		return 0;
	}

	printf("Cancelling File Transfer for Link: 0x%x\n", (INT_PTR)link);
	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Cancelling File Transfer for Link: 0x%x", (INT_PTR)link);
	removeLinkState(pLinkState);
	return 0;
}