#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"
#include "net/net.h"

typedef enum CommandServingFlags CommandServingFlags;

//enum describing how struct-ified xpaths are persisted on the MCP. In particular, big things that never
//change (like, for instance, the list of all commands on a game server), should be persisted forever, so that
//the same multi-hundred-K query doesn't need to be repeatedly sent back and forth, whereas most stuff should just
//be persisted for a short time (but not zero, so that the handshaking for executing a command doesn't send off 3 
//requests in 0.1 seconds)
AUTO_ENUM;
typedef enum
{
	HTTPXPATHPERSIST_DYNAMIC,
	HTTPXPATHPERSIST_STATIC
} enumHttpXPathPersistType;

typedef struct UrlArgumentList UrlArgumentList;
typedef struct StructInfoForHttpXpath StructInfoForHttpXpath;
typedef enum enumCmdContextHowCalled enumCmdContextHowCalled;

typedef struct GetJpegCache
{
	int iRequestID;
	ContainerID iMCPID;
	NetLink *pNetLink;
} GetJpegCache;



typedef struct HttpGlobObjWrapper HttpGlobObjWrapper;

AUTO_STRUCT AST_RUNTIME_MODIFIED;
struct HttpGlobObjWrapper
{
	char *pLabel; AST(ESTRING)
	HttpGlobObjWrapper *pStruct; AST(INDEX_DEFINE)//fake struct type, gets replaced in the TPI by 
		//the referent's actual struct
		//whenever this container struct is used
};

//An HTTP response for an xpath request.
AUTO_STRUCT;
typedef struct StructInfoForHttpXpath
{
	//if the error string is set, then an error occurred, ignore everything else
	char *pErrorString; AST(ESTRING)

	//if the redirect string is set, then instead of trying to display anything, we'll just
	//send an httpRedirect
	char *pRedirectString; AST(ESTRING)

	char *pTPIString; AST(ESTRING)
	char *pStructString; AST(ESTRING)
	int iColumn;
	int iIndex;
	bool bIsRootStruct;
	bool bIsArray;
	enumHttpXPathPersistType ePersistType;

	char *pPrefixString; AST(ESTRING) //raw HTML string to prepend to the page
	char *pSuffixString; AST(ESTRING) //raw HTML string to postpend
	char *pExtraStylesheets; AST(ESTRING) // HTML fragment to append to stylesheet section of header
	char *pExtraScripts; AST(ESTRING) // HTML fragment to append to script section of header

	ParseTable *pLocalTPI; NO_AST   //if these are set, then this struct is returning fully locally, and these
	void *pLocalStruct; NO_AST //should be used instead of TPIString and StructString
			

} StructInfoForHttpXpath;

extern ParseTable parse_StructInfoForHttpXpath[];
#define TYPE_parse_StructInfoForHttpXpath StructInfoForHttpXpath

AUTO_STRUCT;
typedef struct GlobObjWrapper
{
	char *pName; AST(ESTRING, FORMATSTRING(HTML=1))
	int iNumObjects;
} GlobObjWrapper;

AUTO_STRUCT;
typedef struct CommandCategoryWrapper
{
	char *pName; AST(ESTRING, FORMATSTRING(HTML=1))
} CommandCategoryWrapper;

AUTO_STRUCT;
typedef struct GenericServerInfoForHttpXpath
{
	char *pSummary; AST(ESTRING FORMATSTRING(HTML_NO_HEADER=1, HTML_CLASS="structheader"))
	char serverType[256]; AST(FORMATSTRING(HTML_SKIP=1)) 
	ContainerID iID; AST(FORMATSTRING(HTML_SKIP=1)) 
	char *pCommandLine; AST(ESTRING)
	char *pDomainLinks; AST(ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1))
	char *pLoggingStatus; AST(ESTRING)
	CommandCategoryWrapper **ppCommandCategories; AST(FORMATSTRING(HTML_COLLAPSED_ARRAY = 1))
	GlobObjWrapper **ppMiscObjects;
	GlobObjWrapper **ppSystemObjects;
	GlobObjWrapper **ppContainers;
	GlobObjWrapper **ppRefDicts;
	char **ppImplementedLocales;

//	char *pAccessLevel; AST(ESTRING)


	char *pCommand1; AST(ESTRING, FORMATSTRING(command=1))
	char *pCommand2; AST(ESTRING, FORMATSTRING(command=1))
	char *pCommand3; AST(ESTRING, FORMATSTRING(command=1))
	char *pCommand4; AST(ESTRING, FORMATSTRING(command=1))
	char *pCommand5; AST(ESTRING, FORMATSTRING(command=1))
	char *pCommand6; AST(ESTRING, FORMATSTRING(command=1))
	char *pCommand7; AST(ESTRING, FORMATSTRING(command=1))
	char *pCommand8; AST(ESTRING, FORMATSTRING(command=1))

//	AST_COMMAND("testCommand", "SuperHappyFunCommand $INT(What should the int be on the $FIELD(serverType) $FIELD(ID)?) $FIELD(serverType)", "NOT $FIELD(ID) = 22" ) 
//	char *pOtherTestCommand; AST(ESTRING, FORMATSTRING(command=1, commandExpr="$FIELD(ID) = 22"))
} GenericServerInfoForHttpXpath;

AUTO_STRUCT;
typedef struct GenericLinkForHttpXpath
{
	char *pLink; AST(ESTRING, FORMATSTRING(HTML=1))
	char *pName; AST(ESTRING, INDEX_DEFINE, FORMATSTRING(HTML=1))
} GenericLinkForHttpXpath;

AUTO_STRUCT;
typedef struct GenericListOfLinksForHttpXpath
{
	char ListName[256];
	int offset;							AST(FORMATSTRING(HTML_SVR_PARAM=1))
	int limit;							AST(FORMATSTRING(HTML_SVR_PARAM=1))
	int more;							AST(FORMATSTRING(HTML_SVR_PARAM=1))
	int count;							AST(FORMATSTRING(HTML_SVR_PARAM=1))
	GenericLinkForHttpXpath **ppLinks; AST(FORMATSTRING(TEST_FILTER = "svr"))
	char *pComment;						AST(FORMATSTRING(HTML=1, HTML_NO_HEADER=1) POOL_STRING)
	char *pExtraCommand;				AST(FORMATSTRING(command=1), ESTRING)
	char *pExtraLink;					AST(FORMATSTRING(HTML=1, HTML_NO_HEADER=1), ESTRING)
} GenericListOfLinksForHttpXpath;

LATELINK;
void GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct);

AUTO_ENUM;
typedef enum GetHttpFlags
{
	GETHTTPFLAG_FULLY_LOCAL_SERVERING = 1 << 0, // the serving is happening on the same .exe as
		//the structs and tpis. This means that it is possible (though not necessary) to do a fully
		//local return
		//This is set by the mid-level serving code (ie, GenericHTTPServing)

	GETHTTPFLAG_STATIC_STRUCT_OK_FOR_LOCAL_RETURN = 1 << 1, //the struct being provided is static
		//and can be served in fully local mode, if appropriate. This is set by 
		//the function that generates the struct and TPI



} GetHttpFlags;

typedef void GetStructForHttpXpath_CB(U32 iReqID1, U32 iReqID2, StructInfoForHttpXpath *pStructInfo);



void GetStructForHttpXpath(UrlArgumentList *pURL, int iAccessLevel, U32 iReqID1, U32 iReqID2, GetStructForHttpXpath_CB *pCB, GetHttpFlags eFlags);



LATELINK;
void HandleMonitoringCommandRequestFromPacket(Packet *pPacket, NetLink *pNetLink);

void HandleMonitoringJpegRequestFromPacket(Packet *pPacket, NetLink *pNetLink);
void HandleMonitoringInfoRequestFromPacket(Packet *pPak, NetLink *pNetLink);


//returns a string containing an HTTP link to the server on which it is run, ie,
// "/xpath=GameServer[17]"
char *LinkToThisServer(void);


//given a string, returns a tiny struct containing just that string... makes it easy to 
//report http errors
void GetMessageForHttpXpath(char *pMessage, StructInfoForHttpXpath *pStructInfo, bool bError);

//given a raw HTML string, returns a struct containing just that string as raw HTML
void GetRawHTMLForHttpXpath(char *pRawHTML, StructInfoForHttpXpath *pStructInfo);
	
typedef enum ProcessStructForHttpFlags
{
	SERVERMON_FIXUP_ALREADY_DONE = 1<<0, //FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED has already been called on this struct, don't do it again
} ProcessStructForHttpFlags;

typedef enum GetHttpFlags GetHttpFlags;

bool ProcessStructIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, void *pStruct, ParseTable *pTPI, int iAccessLevel, ProcessStructForHttpFlags eProcessFlags, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eGetFlags);


//there are several "domains" generically hanging off any server, which
//are disambiguated immediately via these strings. So an actual monitoring URL
//will look something like http://AWERNER/viewxpath=GameServer[21].generic.foo

//the generic root-level struct
#define GENERIC_DOMAIN_NAME ".generic"

//the custom server-specific root-level struct(if there isn't one, we redirect to ".generic")
#define CUSTOM_DOMAIN_NAME ".custom"


//a category of commands... lists all commands in that category
#define COMMANDCATEGORY_DOMAIN_NAME ".commandcategory."

//a command. Provides a page describing that command, from which that command can be executed
#define COMMANDS_DOMAIN_NAME ".command."

//view a global object dictionary, or something in one. Must always be followed up by pDictName and then
//possibly by [objName] and then possibly by xpath inside that reference
//
//for instance http://AWERNER/viewxpath=GameServer[21].globObj[Power] or
//http://AWERNER/viewxpath=GameServer[21].globObj.EntityPlayer[4].foo
#define GLOBALOBJECTS_DOMAIN_NAME ".globObj."

//a server can register its own "domain name", and provide a callback for processing it
//returns true if the local xpath was properly processed
typedef bool CustomXpathProcessingCB_Immediate(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags);
typedef void CustomXpathProcessingCB_Delayed(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, U32 iReqID1, U32 iReqID2, GetStructForHttpXpath_CB *pCB, GetHttpFlags eFlags);

//assumes that pDomainName is static, as it's most likely a literal string
void RegisterCustomXPathDomain(char *pDomainName, CustomXpathProcessingCB_Immediate *pImmediateCB, CustomXpathProcessingCB_Delayed *pDelayedCB);

//executes the command and, success or failure, prints into an EString an html-compatible description of what
//happened, including return string if any (currently only implemented in GSL)
void cmdParseIntoVerboseHtmlString(char *pCommandString, char **ppEString, enumCmdContextHowCalled eHow);



//during writing of structs into strings to send for monitoring, this is the access level of the viewing client
int GetHTMLAccessLevel(void);

void SetHTMLAccessLevel(int level);

//when you're in the middle of HTML-ifying some object, and want to know what the server type and ID is that
//the object lives on, call these functions. They will work correctly even when the MCP is doing the actual serving
LATELINK;
GlobalType XPathSupport_GetServerType(void);

LATELINK;
ContainerID XPathSupport_GetContainerID(void);

LATELINK;
void GetCommandsForGenericServerMonitoringPage(GenericServerInfoForHttpXpath *pInfo);

//default 200. Default number of items per page in filtered view
extern int gHttpXpathListCutoffSize;

void DoSlowReturn_NetLink(ContainerID iMCPID, int iRequestID, int iClientID, CommandServingFlags eFlags, char *pMessageString, void *pUserData);

int NumberOfCmdLists();

//sets a cap on the number of objects in a dictionary that can be browsed through with the normal filtered dictionary
//view. Defaults to 10000
void HttpSetMaxDictSizeForNormalBrowsing(int iVal);