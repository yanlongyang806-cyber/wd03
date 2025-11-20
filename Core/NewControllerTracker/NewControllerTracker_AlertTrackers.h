#pragma once
#include "alerts.h"

typedef struct EventCounter EventCounter;
typedef struct Alert Alert;
typedef struct CriticalSystem_Status CriticalSystem_Status;

AUTO_STRUCT;
typedef struct AlertCounts_SingleCritSys
{
	char *pCritSysName; AST(KEY)
	char *pLink; AST(ESTRING, FORMATSTRING(HTML=1))
	int iTotalCount;
	int iLast15Minutes;
	int iLastHour;
	int iLast6Hours;
	int iLastDay;
	EventCounter *pCounter; NO_AST
} AlertCounts_SingleCritSys;

AUTO_STRUCT;
typedef struct AlertTextsPerSystem
{
	char *pCritSysName; AST(KEY)
	char **ppRecentFullTexts; AST(FORMATSTRING(HTML_PREFORMATTED = 1))
} AlertTextsPerSystem;

AUTO_ENUM;
typedef enum AlertTrackerNetopsCommentType
{
	COMMENTTYPE_PERMANENT,
	COMMENTTYPE_TIMED,
	COMMENTTYPE_TIMED_RESETTING,
} AlertTrackerNetopsCommentType;

AUTO_STRUCT;
typedef struct AlertTrackerNetopsComment
{
	const char *pParentKey; AST(POOL_STRING FORMATSTRING(HTML_SKIP=1))
	int iID; AST(FORMATSTRING(HTML_SKIP=1)) //unique to this alerttracker
	AlertTrackerNetopsCommentType eType;
	U32 iLifespan; AST(FORMATSTRING(HTML_SECS_DURATION=1))
	U32 iCommentCreationTime; AST(FORMATSTRING(HTML_SECS_AGO=1)) //resets every time the comment happens if type is COMMENTTYPE_TIMED_RESETTING
	char *pText;
	char **ppSystemOrCategoryNames;
	AST_COMMAND("Remove", "RemoveComment $FIELD(ParentKey) $FIELD(ID) $CONFIRM(Really remove this comment?)")
} AlertTrackerNetopsComment;


AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "SetRedirect, AddComment, Comment, CommentType, Systems, Level, TotalCount, RedirectAddress");
typedef struct AlertTracker
{
	const char *pAlertKey; AST(POOL_STRING KEY)
	enumAlertCategory eCategory;
	enumAlertLevel eLevel;
	int iTotalCount; AST(NO_TEXT_SAVE)
	int iLast15Minutes; AST(NO_TEXT_SAVE)
	int iLastHour; AST(NO_TEXT_SAVE)
	int iLast6Hours; AST(NO_TEXT_SAVE)
	int iLastDay; AST(NO_TEXT_SAVE)
	EventCounter *pCounter; NO_AST
	AlertCounts_SingleCritSys **ppCountsPerSystem; AST(NO_TEXT_SAVE)
	AlertTextsPerSystem **ppTextsPerSystem; AST(NO_TEXT_SAVE)
	AlertTrackerNetopsComment **ppNetopsComments;

	char *pComment_ForServerMonitoring; AST(NAME(Comment), NO_TEXT_SAVE, ESTRING)
	char *pCommentType_ForServerMonitoring; AST(NAME(CommentType), NO_TEXT_SAVE, ESTRING)
	char *pSystemsString_ForServerMonitoring; AST(NAME(Systems), NO_TEXT_SAVE, ESTRING)

	//if set, then redirect any email about this alert to only this address
	char *pRedirectAddress; AST(ESTRING)

	AST_COMMAND("SetRedirect", "SetRedirectForAlertTracker $FIELD(AlertKey) $STRING(Redirect these alerts to... or empty for none)")
	AST_COMMAND("AddComment", "AddCommentForAlertTracker $FIELD(AlertKey) $SELECT(Comment type|NAMELIST_AlertTrackerNetopsCommentType) $INT(Days duration) \"$STRING(System or category names, or all)\" \"$STRING(Comment text)\"")
} AlertTracker;

AUTO_STRUCT;
typedef struct AlertTrackerList
{
	AlertTracker **ppTrackers;
} AlertTrackerList;

void AlertTrackers_TrackAlert(Alert *pAlert, CriticalSystem_Status *pSystem);
void AlertTrackers_InitSystem(void);

//ppSystemOrCategoryNames is an earray of strings, will get all comments that match any system or category
//in that list. If bExpandList is true, then it does one pass of expanding the list by adding in all categories 
//belonging to systems in the list, and all systems belonging to categories in the list
char *AlertTrackers_GetCommentsForAlert(const char *pAlertKey, char **ppSystemOrCategoryNames, bool bExpandList);
char *AlertTrackers_GetCommentsForAlert_OneSystemOrCategory(const char *pAlertKey, char *pSystemOrCategoryName, bool bExpandList);

char *AlertTrackers_GetRedirectAddressForAlert(const char *pAlertKey);