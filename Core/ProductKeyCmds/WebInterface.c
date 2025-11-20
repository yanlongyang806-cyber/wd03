#include "AccountServerCommands.h"

#include "file.h"
#include "estring.h"
#include "utils.h"
#include "sysutil.h"
#include "sock.h"
#include "net/net.h"
#include "net/nethttp.h"
#include "net/netlink.h"
#include "htmlForms.h"
#include "Autogen/nethttp_h_ast.h"
#include "accountCommon.h"
#include "zutils.h"
#include "accountnet.h"

static int giWebInterfacePort = 80;
AUTO_CMD_INT(giWebInterfacePort, httpPort) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
static char gWebDirPath[MAX_PATH] = "C:\\Core\\data\\server\\AccountServer\\WebRoot\\";
AUTO_CMD_STRING(gWebDirPath, WebDirectory) ACMD_CMDLINE;

static void httpHandlePost(char *header, char *data, NetLink *link);
static void httpHandleGet(char *data, NetLink *link);
static int httpDisconnect(NetLink* link, void *pIgnored);

NetComm * spWebComm = NULL;
NetComm * getWebComm(void)
{
	if (!spWebComm)
		spWebComm = commCreate(0, 1);
	return spWebComm;
}

bool initWebInterface(void)
{
	NetListen *listen_socket;
	int tries = 0;
	
	for(;;)
	{
		setHttpDefaultPostHandler(httpHandlePost);
		setHttpDefaultGetHandler(httpHandleGet);

		listen_socket = commListen(getWebComm(),LINK_HTTP, giWebInterfacePort,
			httpDefaultMsgHandler, httpDefaultConnectHandler, httpDisconnect,sizeof(HttpClientStateDefault));
		if (listen_socket)
			break;
		Sleep(1);
		if(0==(tries++%5000))
			printf("failed to listen on port %i: %s",giWebInterfacePort,lastWinErr());
	}
	printf("Listening for web connections on port %d.\n", giWebInterfacePort);
	return true;
}

// ------------------------------------------------------
// File Transfer

static void sendFileToLink(NetLink* link, const char *pFilename, const char *pContentType, bool bZipped)
{
	int len;
	FILE *pFile;

	bool bFileExists = fileExists(pFilename);
	if(!bFileExists)
	{
		httpSendFileNotFoundError(link, pFilename);
		return;
	}

	if(bZipped)
	{
		len = calcZipFileSize(pFilename);
	}
	else
	{
		len = fileSize(pFilename);
	}

	httpSendBasicHeader(link, len, pContentType);

	if(len < WOULD_FIT_IN_SEND_BUFFER)
	{
		// Just send it immediately
		printf("Immediately sending file %s to link 0x%p\n", pFilename, link);
		httpSendFileRaw(link, pFilename);
		return;
	}
	
	pFile = fopen(pFilename, (bZipped) ? "rbz" : "rb");

	if(!pFile)
	{
		httpSendFileNotFoundError(link, pFilename);
		return;
	}

	// Add a new LinkState for this
	printf("Sending large file %s to link 0x%p\n", pFilename, link);
	addLinkState(link, pFile);
}

// ------------------------------------------------------
// Webpages
static char *spNewAccountName = "accountname";
static char *spNewAccountPassword = "password";
static char *spNewAccountConfirm = "pwdconfirm";
static char *spExistingAccountName = "accountname_old";
static char *spExistingAccountPassword = "password_old";
static char *spProductKey = "product_key";

static char *spNewSubmitName = "newsubmit";
static char *spExistingSubmitName = "oldsubmit";

#define ACCOUNT_WEBVAR(str) (STACK_SPRINTF("$%s$", str))
#define ACCOUNT_REPLACE_WEBVAR(estr, arglist, str) estrReplaceOccurrences(estr, ACCOUNT_WEBVAR(str), urlFindSafeValue(arglist, str))

static void wiLoadHTMLFile(char **estr, const char *pFilePath)
{
	FILE *file = NULL;
	char *text = NULL;
	int size = 0;

	file = fopen(pFilePath, "rb");
	size = fileGetSize(file);
	assert (file && size > 0);

	text = (char *) malloc(size + 1);
	fread(text, sizeof(char), size, file);
	text[size] = 0;

	estrConcatf(estr, "%s", text);
	free(text);
	fclose(file);
}

#define CLEAR_COOKIES 1
#define SET_COOKIES 2

static void SendHTML(NetLink *link, char *pHTMLString, CookieList *pCookies, int iCookiesOption)
{
	int iTotalLength  = (int) strlen(pHTMLString);
	if (iCookiesOption == CLEAR_COOKIES)
		httpSendBasicHeaderClearCookies(link, iTotalLength, NULL, pCookies);
	else if (iCookiesOption == SET_COOKIES)
		httpSendBasicHeaderPlusCookies(link, iTotalLength, NULL, pCookies);
	else
		httpSendBasicHeader(link, iTotalLength, NULL);
	httpSendBytesRaw(link, pHTMLString, iTotalLength);
}

static void wiActivateKey(NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	const char *pAccountName = NULL;
	const char *pPassword = NULL;
	const char *pKey = urlFindValue(args, spProductKey);
	bool bCreateNewAccount = false;
	char *pResult = NULL;

	if (urlFindValue(args, spNewSubmitName))
	{
		const char *pPasswordConfirm = urlFindValue(args, spNewAccountConfirm);
		bCreateNewAccount = true;
		pAccountName = urlFindValue(args, spNewAccountName);
		pPassword = urlFindValue(args, spNewAccountPassword);

		if (!pPasswordConfirm || !pPassword || strcmp(pPasswordConfirm, pPassword) != 0)
			pPassword = NULL; // passwords don't match, make it fail
	}
	else if (urlFindValue(args, spExistingSubmitName))
	{
		pAccountName = urlFindValue(args, spExistingAccountName);
		pPassword = urlFindValue(args, spExistingAccountPassword);
	}

	if (pAccountName && pPassword && pKey && pAccountName[0] && pPassword[0] && (bCreateNewAccount || pKey[0]))
	{
		if (urlFindValue(args, "ishashed"))
		{
			pResult = assignKeyWithPassword(pAccountName, pPassword, pKey, bCreateNewAccount);
		}
		else
		{
			char hashedPassword[128] = "";
			if (bCreateNewAccount)
			{
				strcpy(hashedPassword, pPassword);
			}
			else 
				accountHashPassword(pPassword, hashedPassword);
			pResult = assignKeyWithPassword(pAccountName, hashedPassword, pKey, bCreateNewAccount);
		}
	} else
	{
		httpRedirect(link, "/activateKeyForm");
	}

	{
		static char *s = NULL;
		char webPath[MAX_PATH];

		estrCopy2(&s, "");
		sprintf(webPath, "%s%s", gWebDirPath, "activateKeyResult.html");
		wiLoadHTMLFile(&s, webPath);

		estrReplaceOccurrences(&s, "$activation_result$", pResult ? pResult : "No response!");

		SendHTML(link, s, pCookies, 0);
	}
}

static void wiActivateKeyForm (NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	char webPath[MAX_PATH];
	const char *pAccountName = findCookieValue(pCookies, "accountname");
	const char *pPassword = findCookieValue(pCookies, "password");

	estrCopy2(&s, "");
	if (pAccountName && pPassword)
	{
		wiLoadHTMLFile(&s, STACK_SPRINTF("%s%s", gWebDirPath, "activateKey2.html"));
		ACCOUNT_REPLACE_WEBVAR(&s, args, spProductKey);
		estrReplaceOccurrences(&s, ACCOUNT_WEBVAR(spExistingAccountName), pAccountName);
		estrReplaceOccurrences(&s, ACCOUNT_WEBVAR(spExistingAccountPassword), pPassword);
	}
	else
	{
		sprintf(webPath, "%s%s", gWebDirPath, "activateKey.html");
		wiLoadHTMLFile(&s, webPath);

		// Passwords NEVER have a default value
		ACCOUNT_REPLACE_WEBVAR(&s, args, spNewAccountName);
		ACCOUNT_REPLACE_WEBVAR(&s, args, spExistingAccountName);
		ACCOUNT_REPLACE_WEBVAR(&s, args, spProductKey);
	}
	SendHTML(link, s, pCookies, 0);
}

static void wiViewAccountInfo(NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;
	static char *spProductKeyTable = NULL;
	const char *pAccountName = findCookieValue(pCookies, "accountname");
	const char *pPassword = findCookieValue(pCookies, "password");
	AccountInfoStripped *pAccountInfo = NULL;
	int i;

	estrCopy2(&s, "");

	if (pAccountName && pPassword && pAccountName[0] && pPassword[0])
	{
		char *pResult = confirmLogin(pAccountName, pPassword);
		pAccountInfo = WebInterfaceGetAccountInfo(pAccountName);
		if (pResult && !pResult[0] || !pAccountInfo)
		{
			wiLoadHTMLFile(&s, STACK_SPRINTF("%s%s", gWebDirPath, "activateKeyResult.html"));
			estrReplaceOccurrences(&s, "$activation_result$", pResult ? pResult : "No response!");

			SendHTML(link, s, pCookies, CLEAR_COOKIES);
			return;
		}
	}
	else
	{
		httpRedirect(link, "/login");
		return;
	}

	wiLoadHTMLFile(&s, STACK_SPRINTF("%s%s", gWebDirPath, "accountInfo.html"));
	estrReplaceOccurrences(&s, "$accountname$", pAccountInfo->accountName);

	estrCopy2(&spProductKeyTable, "");
	estrConcatf(&spProductKeyTable, "<table>\n");
	estrConcatf(&spProductKeyTable, "<tr><td>ProductKey</td>\n"
		"<td>Client Download Link</td>\n"
		"</tr>\n");
	for (i=0; i<eaSize(&pAccountInfo->ppProductKeys); i++)
	{
		// TODO somethingggggggggg
		estrConcatf(&spProductKeyTable, "<tr><td>%s</td>\n<td>\n", pAccountInfo->ppProductKeys[i]);
		if (strStartsWith(pAccountInfo->ppProductKeys[i], "FC"))
		{
			estrConcatf(&spProductKeyTable, "<a href=\"StandaloneMCP.exe\">Download Game Client</a>\n");
		}
		estrConcatf(&spProductKeyTable, "</td></tr>\n");
	}
	estrConcatf(&spProductKeyTable, "</table>\n");

	estrReplaceOccurrences(&s, "$prodkey_table$", spProductKeyTable);
	SendHTML(link, s, pCookies, 0);
}

static void wiAccountLogin(NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	// TODO login button, create account link
	static char *s = NULL;

	estrCopy2(&s, "");
	wiLoadHTMLFile(&s, STACK_SPRINTF("%s%s", gWebDirPath, "login.html"));

	ACCOUNT_REPLACE_WEBVAR(&s, args, spNewAccountName);
	
	SendHTML(link, s, pCookies, 0);
}

static void wiAccountLogout(NetLink *link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies)
{
	static char *s = NULL;

	estrCopy2(&s, "");
	estrConcatf(&s, "<META HTTP-EQUIV=\"Refresh\" CONTENT=\"1; URL=%s\">\n", "/login");
	wiLoadHTMLFile(&s, STACK_SPRINTF("%s%s", gWebDirPath, "activateKeyResult.html"));
	estrReplaceOccurrences(&s, "$activation_result$", "Logging out of Account");

	SendHTML(link, s, pCookies, CLEAR_COOKIES);
}

static void wiAccountValidateLogin(NetLink * link, UrlArgumentList *args, const char *pReferer, CookieList *pCookies, bool bSetCookies)
{
	static char *s = NULL;
	const char *pAccountName = findCookieValue(pCookies, "accountname");
	const char *pPassword = findCookieValue(pCookies, "password");
	char webPath[MAX_PATH];

	estrCopy2(&s, "");
	sprintf(webPath, "%s%s", gWebDirPath, "activateKeyResult.html");

	if (pAccountName && pPassword && pAccountName[0] && pPassword[0])
	{
		char *pResult = confirmLogin(pAccountName, pPassword);
		if (pResult && pResult[0])
		{
			wiLoadHTMLFile(&s, webPath);
			estrReplaceOccurrences(&s, "$activation_result$", pResult ? pResult : "No response!");

			SendHTML(link, s, pCookies, bSetCookies ? 0 : CLEAR_COOKIES);
			return;
		}
	}
	else
	{
		wiAccountLogin(link, args, pReferer, pCookies);
		return;
	}
	
	estrConcatf(&s, "<META HTTP-EQUIV=\"Refresh\" CONTENT=\"1; URL=%s\">\n", "/viewAccount");
	wiLoadHTMLFile(&s, webPath);
	estrReplaceOccurrences(&s, "$activation_result$", "Login validated");

	SendHTML(link, s, pCookies, bSetCookies ? SET_COOKIES : 0);
}

// ------------------------------------------------------
// HTTP Message Handling
static int httpDisconnect(NetLink* link, HttpClientStateDefault *pClientState)
{
	LinkState *pLinkState = findLinkState(link);

	// Cleanup our HttpClientStateDefault
	if (pClientState)
	{
		if(pClientState->pPostHeaderString)
			estrDestroy(&pClientState->pPostHeaderString);
		if(pClientState->pPostDataString)
			estrDestroy(&pClientState->pPostDataString);
	}

	if(!pLinkState)
	{
		return 0;
	}

	printf("Cancelling File Transfer for Link: 0x%x\n", (INT_PTR)link);
	removeLinkState(pLinkState);
	return 0;
}

void updateWebInterface(void)
{
	updateLinkStates();
}


static void httpHandlePost(char *header, char *data, NetLink *link)
{
	char *pErr  = NULL;
	char *arg   = NULL;
	char *value = NULL;
	char *pReferer = NULL;
	char *url = NULL;
	bool setCookies = false;
	CookieList *pCookies = StructCreate(parse_CookieList);
	UrlArgumentList *arglist;
	httpParseHeader(header, "POST", &url, &pReferer, pCookies);

	if(!url)
	{
		StructDestroy(parse_CookieList, pCookies);
		return;
	}

	arglist = urlToUrlArgumentList(data);

	if (!(hasCookie(pCookies, "accountname") && hasCookie(pCookies, "password")))
	{
		const char *pUsername;
		const char *pPassword;

		pUsername = urlFindValue(arglist, "accountname");
		pPassword = urlFindValue(arglist, "password");

		if (pUsername && pPassword)
		{
			Cookie *pNewCookie = StructCreate(parse_Cookie);
			char hashedPassword[128] = "";
			pNewCookie->pName = strdup("accountname");
			pNewCookie->pValue = strdup(pUsername);
			eaPush(&pCookies->ppCookies, pNewCookie);

			pNewCookie = StructCreate(parse_Cookie);
			pNewCookie->pName = strdup("password");
			accountHashPassword(pPassword, hashedPassword);
			pNewCookie->pValue = strdup(hashedPassword);
			eaPush(&pCookies->ppCookies, pNewCookie);
			setCookies = true;
		}
	}
	
	if (stricmp(url, "/activateKey") == 0)
	{
		wiActivateKeyForm(link, arglist, pReferer, pCookies);
	}
	else if (stricmp(url, "/activateKeyResult") == 0)
	{
		wiActivateKey(link, arglist, pReferer, pCookies);
	}
	else if (stricmp(url, "/login") == 0)
	{
		wiAccountValidateLogin(link, arglist, pReferer, pCookies, setCookies);
	}
	else if (stricmp(url, "/viewAccount") == 0)
	{
		wiViewAccountInfo(link, arglist, pReferer, pCookies);
	}
	else
	{
		httpSendFileNotFoundError(link, "Unknown command.");
	}

	StructDestroy(parse_CookieList, pCookies);
	StructDestroy(parse_UrlArgumentList, arglist);
}

typedef struct WebFileMapping {
	char filePath[MAX_PATH];
	char webName[128];
} WebFileMapping;
static WebFileMapping **sFileMapping = NULL;
AUTO_RUN_LATE;
void webInitializeFileMappings(void)
{
	WebFileMapping *mapping;

	mapping = malloc(sizeof(WebFileMapping));
	strcpy(mapping->filePath, "C:\\FightClub\\tools\\bin\\StandaloneMCP.exe");
	strcpy(mapping->webName, "StandaloneMCP.exe");
	eaPush(&sFileMapping, mapping);
}

static const char * getFileMapping(const char *webName)
{
	int i;
	for (i=0; i<eaSize(&sFileMapping); i++)
	{
		if (stricmp(sFileMapping[i]->webName, webName) == 0)
			return sFileMapping[i]->filePath;
	}
	return "";
}

static void httpHandleGet(char *data, NetLink *link)
{
	char	*url_esc = NULL,*args[100] = {0},*values[100] = {0};
	char	buf[MAX_PATH];
	int		count;
	int		rawDataRemaining = linkGetRawDataRemaining(link);
	bool	bIsPostData = false;
	char *pReferer = NULL;
	CookieList *pCookies = StructCreate(parse_CookieList);

	httpParseHeader(data, "GET", &url_esc, &pReferer, pCookies);

	if (!url_esc)
	{
		StructDestroy(parse_CookieList, pCookies);
		return;
	}
	count = urlToArgs(url_esc,args,values,ARRAY_SIZE(args));

	strcpy_s(buf, MAX_PATH, args[0]);

	if (stricmp(args[0],"/")==0)
	{
		if (findCookieValue(pCookies, "accountname") && findCookieValue(pCookies, "password"))
		{
			httpRedirect(link, "/viewAccount");
			StructDestroy(parse_CookieList, pCookies);
			return;
		}
		else
		{
			httpRedirect(link, "/login");
			StructDestroy(parse_CookieList, pCookies);
			return;
		}
	}
	if (stricmp(args[0], "/activateKey") == 0)
	{
		wiActivateKeyForm(link, NULL, pReferer, pCookies);
	}
	else if (stricmp(args[0], "/login") == 0)
	{
		wiAccountLogin(link, NULL, pReferer, pCookies);
	}
	else if (stricmp(args[0], "/viewAccount") == 0)
	{
		wiViewAccountInfo(link, NULL, pReferer, pCookies);
	}
	else if (stricmp(args[0], "/logout") == 0)
	{
		wiAccountLogout(link, NULL, pReferer, pCookies);
	}
	else if (args[0] && strstri(args[0], ".exe") != 0)
	{
		sendFileToLink(link, getFileMapping(*args[0] == '/' ? args[0]+1 : args[0]), "application/octet-stream", false);
	}
	else 
	{
		httpSendFileNotFoundError(link, "Unknown command.");
	}
	StructDestroy(parse_CookieList, pCookies);
}
