#ifndef _AUTHENTICATION_H_
#define _AUTHENTICATION_H_

#define TICKET_USER_CONTAINERS 1

typedef struct TicketEntryConst_AutoGen_NoConst TicketEntry;
typedef struct Container Container;

// -------------------------------
// Access Level
AUTO_STRUCT;
typedef struct AccessLevelStruct
{
	/*NONE = 0,
	USER,
	ADMIN,*/
	U32 uLevel;
	char *pLevelName;
} AccessLevelStruct;

#define ALVL_USER         1
#define ALVL_GROUPADMIN   2
#define ALVL_ADMIN        3
#define ALVL_ADMIN_LVL2   4

AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER AST_IGNORE(iMemberIDs) AST_IGNORE(eRule) AST_IGNORE(uLastAssignedUserID);
typedef struct TicketUserGroup
{
	const U32 uID; AST(KEY)
	CONST_STRING_MODIFIABLE pName;
} TicketUserGroup;
AST_PREFIX();

#define CONTAINER_GROUP(pContainer) ((TicketUserGroup*) pContainer->containerData)
TicketUserGroup * findTicketGroupByID(U32 uID);
TicketUserGroup * findTicketGroupByName(const char * pGroupName);
TicketUserGroup * createTicketGroup(const char *pName);
int getTicketGroupCount(void);

typedef void (*TicketGroupIteratorFunc) (TicketUserGroup *pGroup, void *userData);
void iterateOverTicketGroups (SA_PARAM_NN_VALID TicketGroupIteratorFunc pFunc, void *userData);

#define GROUPNAME(uID) (findTicketGroupByID(uID) ? findTicketGroupByID(uID)->pName : "--")

AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER AST_IGNORE(pJiraUsername) AST_IGNORE(pJiraPassword) AST_IGNORE(pPassword) AST_IGNORE(iUserGroups) AST_IGNORE(uAccessLevel);
typedef struct TicketTrackerUser
{
	const U32 uID; AST(KEY)
	CONST_STRING_MODIFIABLE pUsername;
	
	char username[256]; NO_AST
	char password[256]; NO_AST
} TicketTrackerUser;
AST_PREFIX();

#define CONTAINER_USER(pContainer) ((TicketTrackerUser*) pContainer->containerData)
TicketTrackerUser * findUserByID(U32 id);

// -------------------------------
// Groups
/*typedef enum GroupAssignmentRule GroupAssignmentRule;
AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER;
typedef struct TicketUserGroup
{
	const U32 uID; AST(KEY)
	CONST_STRING_MODIFIABLE pName;

	CONST_INT_EARRAY iMemberIDs;
	const GroupAssignmentRule eRule;
	const U32 uLastAssignedUserID; // used for round-robin assignment rules
} TicketUserGroup;
AST_PREFIX();


#define CONTAINER_GROUP(pContainer) ((TicketUserGroup*) pContainer->containerData)

TicketUserGroup * findUserGroupByID(U32 uID);
TicketUserGroup * createUserGroup(const char *pName);
void addUsersToGroup(int *pNewMemberIDs);

// -------------------------------
// Users
AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER;
typedef struct TicketTrackerUser
{
	const U32 uID; AST(KEY)
	const U32 uAccessLevel;
	CONST_STRING_MODIFIABLE pUsername;
	CONST_STRING_MODIFIABLE pPassword;
	CONST_INT_EARRAY iUserGroups;
	
	CONST_STRING_MODIFIABLE pJiraUsername; AST(ESTRING)
	CONST_STRING_MODIFIABLE pJiraPassword; AST(ESTRING)

	//AST_COMMAND("Delete User");
	AST_COMMAND("Change Jira Login", "changeJiraLogin $FIELD(uID) $STRING(Username) $STRING(Password)")
	AST_COMMAND("Change Password", "changePassword $FIELD(uID) $STRING(New Password)")
	AST_COMMAND("Change Access Level", "changeAccess $FIELD(uID) $INT(New Access Level)")
	AST_COMMAND("Delete User", "deleteUser $FIELD(uID)")
} TicketTrackerUser;
AST_PREFIX();


#define ACTION_NONE   0x0
#define ACTION_CREATE 0x1
#define ACTION_EDIT   0x2
#define ACTION_DELETE 0x4
#define ACTION_ASSIGN 0x8
#define ACTION_INVITE 0x10

#define CSRTYPE_UNKNOWN 0
#define CSRTYPE_USER    1
#define CSRTYPE_GROUP   2

AUTO_STRUCT;
typedef struct CSRAction
{
	U32 uFlags; // valid subactions for action
	U32 uAccessLevel;
} CSRAction;

AUTO_STRUCT;
typedef struct Action
{
	char *pActionName;
	U32 uValidFlags;
	CSRAction **ppPermitted;
} Action;

AUTO_STRUCT;
typedef struct TicketTrackerUserList
{
	TicketTrackerUser **ppUsers; // user groups and representative share IDs
	TicketUserGroup **ppUserGroups;

	Action **ppActions;
	AccessLevelStruct **ppAccessLevels;

	U32 uNextID;

	AST_COMMAND("Add User", "addUser $STRING(Username) $STRING(Password) $INT(Access Level (0=None,1=User,2=Group Admin,3=Full Admin)\n)")
} TicketTrackerUserList;

// -------------------------------
typedef void (*TicketUserIteratorFunc)(TicketTrackerUser *pUser, void *userData);

#define USER_ACCESSLEVEL(pRep) (pRep == NULL ? NULL : findAccessLevel(pRep->uAccessLevel))
#define ACCESSLEVEL_NAME(pAccessLevel) (pAccessLevel == NULL ? "None" : pAccessLevel->pLevelName)
#define USERNAME(uID) (findUserByID(uID) ? findUserByID(uID)->pUsername : "--")

const char * getSubActionString (U32 uFlag);

TicketTrackerUser * findUserByName(const char * pUsername);
Container * findUserContainerByID(U32 id);
Container * findUserContainerByName(const char * pUsername);

int getUserCount(void);
void iterateOverUsers (SA_PARAM_NN_VALID TicketUserIteratorFunc pFunc, void *userData);
void importUsers(void);

int getUserGroupCount(void);
void importGroups(void);

TicketUserGroup * findUserGroupByID(U32 id);
TicketUserGroup * findUserGroupByName(const char * pGroupName);

AccessLevelStruct * findAccessLevel(U32 uLevel);
AccessLevelStruct * findAccessLevelByName(const char * pName);

void changeJiraLogin(U32 userID, const char *pUsername, const char *pPassword);
bool deleteUser(TicketTrackerUser *pUser);
bool addUser(const char * pUsername, const char * pPassword, const AccessLevelStruct *pAccess, char **ppErrorString);
bool addUserGroup(const char * pGroupname, int *iMemberIDs, char **ppErrorString);
bool addUserToGroup(TicketTrackerUser * pUser, TicketUserGroup * pGroup);
bool removeUserFromGroup(TicketTrackerUser * pUser, TicketUserGroup * pGroup, int idx);

Action * findAction(const char *pActionName);
CSRAction * findActionPermissions(const char *pActionName, const AccessLevelStruct *pAccess);

CSRAction * addPermissions(const char *pActionName, const AccessLevelStruct *pAccess, U32 uFlags);

TicketTrackerUser * verifyLogin (const char *pUsername, const char *pPassword);

bool hasAccessLevel(TicketTrackerUser *pRep, const char *pActionName, U32 uFlag);

bool changePassword(TicketTrackerUser *pRep, const char * pNewPassword);
bool changeAccessLevel(TicketTrackerUser *pRep, const AccessLevelStruct *pAccess);

void initializeEmptyCSR(void);

bool hasGroupAdminAccess(TicketTrackerUser *pUser, TicketUserGroup *pGroup);
bool hasGroupAccess(TicketTrackerUser *pUser, TicketEntry *pEntry);
bool hasUserAccess(TicketTrackerUser *pUser, TicketEntry *pEntry);
bool canAssignToSelf(TicketTrackerUser *pUser, TicketEntry *pEntry);/**/

#endif