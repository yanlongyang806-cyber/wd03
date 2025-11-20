#ifndef JIRA_H
#define JIRA_H

AST_PREFIX( PERSIST )
AUTO_STRUCT AST_CONTAINER;
typedef struct JiraIssue
{
	CONST_STRING_MODIFIABLE key; AST(ESTRING)
	CONST_STRING_MODIFIABLE assignee; AST(ESTRING)
	const int status;
	const int resolution; // default = -1 (no resolution)
	const int prevStatus; NO_AST
	const int prevResolution; NO_AST
} JiraIssue;
AST_PREFIX()

// ---------------------------------------------------------

AUTO_STRUCT;
typedef struct JiraUser
{
	char *pUserName;     AST(ESTRING)
	char *pFullName;     AST(ESTRING)
	char *pEmailAddress; AST(ESTRING)
} JiraUser;

AUTO_STRUCT;
typedef struct JiraUserList
{
	JiraUser **ppUsers;
} JiraUserList;

// ---------------------------------------------------------

AUTO_STRUCT;
typedef struct JiraProject
{
	char *pID;   AST(ESTRING)
	char *pKey;  AST(ESTRING)
	char *pName; AST(ESTRING)
} JiraProject;

AUTO_STRUCT;
typedef struct JiraProjectList
{
	JiraProject **ppProjects;		AST(FORMATSTRING(XML_UNWRAP_ARRAY = 1))
} JiraProjectList;

AUTO_STRUCT;
typedef struct JiraResolution
{
	int id;
	char *pName; AST(ESTRING)
	char *pDescription; AST(ESTRING)
} JiraResolution;

AUTO_STRUCT;
typedef struct JiraStatus
{
	int id;
	char *pName; AST(ESTRING)
	char *pDescription; AST(ESTRING)
} JiraStatus;

// ---------------------------------------------------------

AUTO_STRUCT;
typedef struct JiraComponent
{
	char *pID;   AST(ESTRING)
	char *pName; AST(ESTRING)
} JiraComponent;

AUTO_STRUCT;
typedef struct JiraComponentList
{
	JiraComponent **ppComponents;
} JiraComponentList;

// ---------------------------------------------------------

void jiraSetDefaultAddress (char *host, int port);
const char *jiraGetDefaultURL(void);
void jiraSetDefaults(const char *pProject, int iType, const char *pCreateIssueAdditionalXML);
const char *jiraGetAddress(void);
int jiraGetPort(void);

bool jiraDefaultLogin(void);
bool jiraLogin(const char *pServer, int port, const char *pUserName, const char *pPassword);
void jiraLogout(void);
bool jiraIsLoggedIn(void);

bool jiraGetUsers(JiraUserList *pList, const char *pGroupName);
bool jiraGetProjects(JiraProjectList *pList);
bool jiraGetComponents(JiraProject* pProject, JiraComponentList *pList);

bool jiraCreateIssue(const char *pProject, const char *pSummary, const char *pDescription, const char *pAssignee, 
					 int iPriority, int iOrd, const JiraComponent* pComponent, const char *pLabel, char *outputKey,int iBufferSize);

void formAppendJiraUsers(char **estr, const char *pVarName);
void formAppendJiraComponents(char **estr, const char *pVarName, JiraComponentList *pComponentsList);
void formAppendJiraProjects(char **estr, const char *pVarName);

bool loadJiraData(void);
void unloadJiraData(void);
JiraProject *findJiraProjectByKey(const char *pProjectKey);

typedef struct NetComm NetComm;
bool jiraGetIssue(JiraIssue *pConstJiraIssue, NetComm *comm);
const char * jiraGetStatusString(int status);
const char * jiraGetResolutionString(int resolution);
void jiraFixupString (char **estr);

// Find a substring like "[COR-12345]" in a string.
const char *jiraFindIssueString(const char *string, size_t max_match_distance, const char **end, const char **project_key, const char **project_key_end, U64 *issue_number);

#endif
