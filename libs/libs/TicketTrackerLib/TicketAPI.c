#include "TicketAPI.h"

#include "ManageTickets.h"
#include "AutoGen/ManageTickets_h_ast.h"
#include "TicketTracker.h"
#include "TicketTrackerConfig.h"
#include "TicketEntry.h"
#include "AutoGen\tickettracker_h_ast.h"
#include "AutoGen\TicketEntry_h_ast.h"

//possibly need these later
//#include "TicketAssignment.h"
//#include "TicketAssignment_h_ast.h"

#include "Authentication.h"
#include "Authentication_h_ast.h"

#include "Category.h"

#include "Search.h"

#include "Message.h"

#include "language\AppLocale.h"
#include "autogen\applocale_h_ast.h"

#include "error.h"
#include "file.h"
#include "EntityDescriptor.h"
#include "ticketnet.h"
#include "AutoGen\ticketenums_h_ast.h"
#include "mathutil.h"
#include "jira.h"
#include "objContainer.h"

#include "XMLRPC.h"

extern TimingHistory *gSearchHistory;
extern char gTicketTrackerAltDataDir[MAX_PATH];
extern ParseTable parse_TicketData[];
#define TYPE_parse_TicketData TicketData

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
ContainerID TT_CreateTicket(const char *actorName, TicketData *data, int groupid, char *pComment)
{
	TicketData *copy;
	if (data->pAccountName == NULL || !(*data->pAccountName))
	{
		Errorf("No account name attached to ticket.");
		return 0;
	}
	copy = StructClone(parse_TicketData, data); // needs to create a copy to own it so the transaction can free it later.
	return createTicket(copy, 0);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
ACMD_STATIC_RETURN TicketEntryConst *TT_GetTicket(U32 id)
{
	TicketEntryConst *te = (TicketEntryConst*)findTicketEntryByID(id);
	if (!te)
	{
		Errorf("Could not find ticket: id %u", id);
		return NULL;
	}
	return te;
}

void AppendToGroupListCB (TicketUserGroup *pGroup, void *userData)
{
	TT_NameAndIdList *list = (TT_NameAndIdList *)userData;
	TT_NameAndId *pair = StructCreate(parse_TT_NameAndId);
	estrPrintf(&pair->name, "%s", pGroup->pName);
	pair->id = pGroup->uID;
	eaPush(&list->list, pair);
}

//User groups
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
TT_NameAndIdList* TT_GetTicketGroups()
{
	TT_NameAndIdList *list = StructCreate(parse_TT_NameAndIdList);
	iterateOverTicketGroups(AppendToGroupListCB, list);
	return list;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
ACMD_STATIC_RETURN TicketUserGroup *TT_GetGroup(U32 groupid)
{
	TicketUserGroup *pGroup = findTicketGroupByID(groupid);
	if (!pGroup)
	{
		Errorf("Could not find TicketGroupID:%u", groupid);
		return NULL;
	}
	return pGroup;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_RenameGroup(const char *actorName, U32 groupid, const char *groupName)
{
	NOCONST(TicketUserGroup) *pGroup = CONTAINER_NOCONST(TicketUserGroup, findTicketGroupByID(groupid));
	if (!pGroup)
	{
		Errorf("Could not find TicketGroupID:%u", groupid);
		return false;
	}
	if (findTicketGroupByName(groupName))
	{
		Errorf("Another group is already using the name '%s'", groupName);
		return false;
	}
	if (pGroup->pName)
		free(pGroup->pName);
	pGroup->pName = StructAllocString(groupName);
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
ACMD_STATIC_RETURN TicketUserGroup *TT_CreateGroup(const char *actorName, const char *groupName)
{
	TicketUserGroup *pGroup = createTicketGroup(groupName);
	if (!pGroup)
	{
		Errorf("Could not create TicketGroup: '%s'", groupName);
		return NULL;
	}
	return pGroup;
}


/*AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
void TT_GroupAddUsers(U32 groupid, XMLStringList *usernames)
{
	TicketUserGroup *pGroup = findUserGroupByID(groupid);
	XMLStringList *successlist = StructCreate(parse_XMLStringList);
	EARRAY_FOREACH_BEGIN(usernames->list,i);
	{
		


	}
	EARRAY_FOREACH_END;
}*/


//Status Enum
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
TT_KeyValueList* TT_GetStatusNames()
{
	TT_KeyValueList *list = StructCreate(parse_TT_KeyValueList);
	int i = 0;
	while (TicketStatusEnum[i].key)
	{
		if (PTR_TO_U32(TicketStatusEnum[i].key) > 4096)
		{
			TT_KeyValue *kv;
			const char *key = getStatusString(TicketStatusEnum[i].value);
			const char *val = TranslateMessageKey(key);
			if (val) 
			{
				kv = StructCreate(parse_TT_KeyValue);
				estrPrintf(&kv->value, "%s", val);
				estrPrintf(&kv->key, "%s", key);
				eaPush(&list->list, kv);
			}
		}
		i++;
	}
	return list;
}
//Internal Status Enum
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
TT_KeyValueList* TT_GetInternalStatusNames()
{
	TT_KeyValueList *list = StructCreate(parse_TT_KeyValueList);
	int i = 0;
	while (TicketInternalStatusEnum[i].key)
	{
		if (PTR_TO_U32(TicketInternalStatusEnum[i].key) > 4096)
		{
			TT_KeyValue *kv;
			const char *key = getInternalStatusString(TicketInternalStatusEnum[i].value);
			const char *val = TranslateMessageKey(key);
			if (val) 
			{
				kv = StructCreate(parse_TT_KeyValue);
				estrPrintf(&kv->value, "%s", val);
				estrPrintf(&kv->key, "%s", key);
				eaPush(&list->list, kv);
			}
		}
		i++;
	}
	return list;
}
//Visibility Enum
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
TT_KeyValueList* TT_GetVisibilityNames()
{
	TT_KeyValueList *list = StructCreate(parse_TT_KeyValueList);
	int i = 0;
	while (TicketStatusEnum[i].key)
	{
		if (PTR_TO_U32(TicketVisibilityEnum[i].key) > 4096)
		{
			TT_KeyValue *kv;
			const char *key = getVisibilityString(TicketVisibilityEnum[i].value);
			const char *val = TranslateMessageKey(key);
			if (val) 
			{
				kv = StructCreate(parse_TT_KeyValue);
				estrPrintf(&kv->value, "%s", val);
				estrPrintf(&kv->key, "%s", key);
				eaPush(&list->list, kv);
			}
		}
		i++;
	}
	return list;
}

//Resolution Enum
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
TT_KeyValueList* TT_GetResolutionNames()
{
	TT_KeyValueList *list = StructCreate(parse_TT_KeyValueList);
	int i = 0;
	while (TicketResolutionEnum[i].key)
	{
		if (PTR_TO_U32(TicketResolutionEnum[i].key) > 4096)
		{
			TT_KeyValue *kv;
			const char *key = getResolutionString(TicketResolutionEnum[i].value);
			const char *val = TranslateMessageKey(key);
			if (val) 
			{
				kv = StructCreate(parse_TT_KeyValue);
				estrPrintf(&kv->value, "%s", val);
				estrPrintf(&kv->key, "%s", key);
				eaPush(&list->list, kv);
			}
		}
		i++;
	}
	return list;
}
//Platforms Enum
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
TT_NameAndIdList* TT_GetPlatformNames()
{
	TT_NameAndIdList *list = StructCreate(parse_TT_NameAndIdList);
	int i = 0;
	while (PlatformEnum[i].key)
	{
		if (PTR_TO_U32(PlatformEnum[i].key) > 4096)
		{
			TT_NameAndId *item = StructCreate(parse_TT_NameAndId);
			item->id = PlatformEnum[i].value;
			estrPrintf(&item->name, "%s", PlatformEnum[i].key);
			eaPush(&list->list, item);
		}
		i++;
	}
	return list;
}

//Language Enum
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
TT_NameAndIdList* TT_GetLanguages()
{
	TT_NameAndIdList *list = StructCreate(parse_TT_NameAndIdList);
	int i = 0;
	while (LanguageEnum[i].key)
	{
		if (PTR_TO_U32(LanguageEnum[i].key) > 4096)
		{
			int iLocale = locGetIDByLanguage(LanguageEnum[i].value);
			if (iLocale != DEFAULT_LOCALE_ID || 
				LanguageEnum[i].value == LANGUAGE_DEFAULT ||
				LanguageEnum[i].value == LANGUAGE_ENGLISH)
			{
				TT_NameAndId *item = StructCreate(parse_TT_NameAndId);
				item->id = LanguageEnum[i].value;
				estrPrintf(&item->name, "%s", locGetName(iLocale));
				eaPush(&list->list, item);
			}
		}
		i++;
	}
	return list;
}

//Sets the ticket assignee
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_SetTicketUser(const char *actorName, U32 ticketid, const char *pAccountName)
{
	TicketEntry *pEntry = findTicketEntryByID(ticketid);
	
	if (!pEntry)
	{
		Errorf("Please supply a Ticket 'id' to assign.");
		return false;
	}

	ticketAssignToAccountName(pEntry, pAccountName);
	return true;
}

//Sets the ticket category
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_SetTicketCategory(const char *actorName, U32 ticketid, const char *maincat, const char *cat)
{
	TicketEntry *pEntry = findTicketEntryByID(ticketid);
	int maincatidx, subcatidx;

	if (!pEntry)
	{
		Errorf("Could not find ticket id: %u", ticketid);
		return false;
	}
	
	maincatidx = categoryGetIndex(maincat);
	if (maincatidx < 0)
	{
		Errorf("Could not find ticket category:%s", maincat);
		return false;
	}

	subcatidx = subcategoryGetIndex(maincatidx, cat);
	if (cat && *cat && subcatidx < 0)
	{
		Errorf("Could not find ticket category:%s - %s", maincat, cat);
		return false;
	}

	ticketChangeCategory(pEntry, actorName, maincat, cat);
	return true;
}

//Sets the ticket status
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_SetTicketStatus(const char *actorName, U32 ticketid, ACMD_NAMELIST(TicketStatusEnum, STATICDEFINE) TicketStatus status)
{
	TicketEntry *pEntry = findTicketEntryByID(ticketid);
	
	if (!pEntry)
	{
		Errorf("Could not find ticket id: %u", ticketid); 
		return false;
	}
	if (pEntry->eStatus == TICKETSTATUS_MERGED)
	{
		Errorf("Ticket #%d has been merged and its status cannot be changed.", ticketid); 
		return false;
	}
	if (ticketEntryChangeStatus(actorName, pEntry, status))
	{
		Errorf("Invalid status specified."); 
		return false;
	}
	return true;
}

//Sets the ticket status
/*AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_SetTicketInternalStatus(const char *actorName, U32 ticketid, ACMD_NAMELIST(TicketInternalStatusEnum, STATICDEFINE) TicketInternalStatus status)
{
	TicketEntry *pEntry = findTicketEntryByID(ticketid);
	
	if (!pEntry)
	{
		Errorf("Could not find ticket id: %u", ticketid); 
		return false;
	}
	ticketEntryChangeInternalStatus(actorName, pEntry, status);
	return true;
}*/

//Sets the ticket priority
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_SetTicketPriority(const char *actorName, U32 ticketid, int priority)
{
	TicketEntry *pEntry = findTicketEntryByID(ticketid);
	
	if (!pEntry)
	{
		Errorf("Could not find ticket id: %u", ticketid); 
		return false;
	}
	if (priority >= 10)
	{
		Errorf("Invalid priority specified.");
		return false;
	}
	pEntry->uPriority = priority;
	return true;
}


//Sets the ticket group
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_SetTicketGroup(const char *actorName, U32 ticketid, U32 groupid)
{
	TicketEntry *pEntry = findTicketEntryByID(ticketid);
	TicketUserGroup *pGroup = findTicketGroupByID(groupid);
	if (!pEntry)
	{
		Errorf("Could not find ticket id: %u", ticketid); 
		return false;
	}
	if(!pGroup && groupid)
	{
		// iGroupID == 0 means group is being cleared, so ignore that
		Errorf("Ticket Group not found for that ID.");
		return false;
	}

	pEntry->uGroupID = groupid;
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_SetTicketResponse(const char *actorName, U32 ticketID, const char *response)
{
	TicketEntry *pEntry = findTicketEntryByID(ticketID);

	if (!pEntry)
	{
		Errorf("Could not find ticket id: %u", ticketID); 
		return false;
	}
	ticketChangeCSRResponse(pEntry, actorName, response);
	return true;
}

static bool TT_StringMatch (const char *st1, const char *st2)
{
	if (!st1 && st2)
		return false;
	if (st1 && !st2)
		return false;
	if (!st1 && !st2)
		return true;
	return (stricmp(st1, st2) == 0);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_BatchAction(const char *actorName, TT_NameAndIdList *ticketIDs, TT_BatchActionStruct *actions)
{
	TicketEntry **ppEntries = NULL;
	int i,size;

	size = eaSize(&ticketIDs->list);
	for (i=0; i<size; i++)
	{
		TicketEntry *pEntry = findTicketEntryByID(ticketIDs->list[i]->id);
		if (pEntry)
			eaPush(&ppEntries, pEntry);
	}

	size = eaSize(&ppEntries);
	for (i=0; i<size; i++)
	{
		if (actions->iPriority > -1 && ppEntries[i]->uPriority  != actions->iPriority)
			ppEntries[i]->uPriority  = actions->iPriority;
		if (actions->eStatus && ppEntries[i]->eStatus != actions->eStatus)
			ticketEntryChangeStatus(actorName, ppEntries[i], actions->eStatus);
		if (actions->assignTo && !TT_StringMatch(actions->assignTo, ppEntries[i]->pRepAccountName))
			ticketAssignToAccountName(ppEntries[i], actions->assignTo);
		if (actions->eVisibility > -1 && ppEntries[i]->eVisibility != actions->eVisibility)
			changeTicketTrackerEntryVisible(actorName, ppEntries[i], actions->eVisibility);
		if (actions->mainCategory && *actions->mainCategory)
		{
			if (stricmp(ppEntries[i]->pMainCategory, actions->mainCategory) != 0 ||
				!TT_StringMatch(ppEntries[i]->pCategory, actions->subcategory))
			{
				ticketChangeCategory(ppEntries[i], actorName, actions->mainCategory, actions->subcategory);
			}
		}
		if (actions->jiraKey)
		{
			if (ppEntries[i]->pJiraIssue && TT_StringMatch(((JiraIssue*) ppEntries[i]->pJiraIssue)->key, actions->jiraKey) ||
				*actions->jiraKey && !ppEntries[i]->pJiraIssue)
			{
				setJiraKey(ppEntries[i], actorName, actions->jiraKey);
			}
		}
		if (actions->kbSolution)
		{
			if (!TT_StringMatch(ppEntries[i]->pSolutionKey, actions->kbSolution))
			{
				setTicketSolution(ppEntries[i], actorName, actions->kbSolution);
			}
		}
		if (actions->rcSolution)
		{
			if (!TT_StringMatch(ppEntries[i]->pPhoneResKey, actions->rcSolution))
			{
				setTicketPhoneResolution(ppEntries[i], actorName, actions->rcSolution);
			}
		}
		if (actions->ticketResponse)
		{
			ticketChangeCSRResponse(ppEntries[i], actorName, actions->ticketResponse);
		}
	}
	eaDestroy(&ppEntries);
	return true;
}

/*AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9);
bool TT_BatchActionTest(void)
{
	TT_NameAndIdList ticketIDs = {0};
	TT_BatchActions actions = {0};
	ContainerIterator iter = {0};
	Container *currCon = NULL;

	objInitContainerIteratorFromType(GLOBALTYPE_TICKETENTRY, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		TicketEntry *pEntry = CONTAINER_ENTRY(currCon);
		TT_NameAndId *id = StructCreate(parse_TT_NameAndId);
		id->id = pEntry->uID;
		eaPush(&ticketIDs.list, id);
		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
	TT_BatchAction("tchao", &ticketIDs, 8, "tchao-me", 
		TICKETSTATUS_IN_PROGRESS, TICKETINTERNALSTATUS_QA, TICKETVISIBLE_HIDDEN+1, 
		"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Missions", 
		"COR-4114", "KB-142", "RC-1", 
		"Closed, sucka!");
	StructDeInit(parse_TT_NameAndIdList, &ticketIDs);
	return true;
}*/

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_ChangeVisibility(const char *actorName, U32 ticketID, ACMD_NAMELIST(TicketVisibilityEnum, STATICDEFINE) TicketVisibility eVisibility)
{
	TicketEntry *pEntry = findTicketEntryByID(ticketID);

	if (!pEntry)
	{
		Errorf("Could not find ticket id: %u", ticketID); 
		return false;
	}

	changeTicketTrackerEntryVisible(actorName, pEntry, eVisibility);
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_AssociateJiraIssue (const char *actorName, U32 ticketID, const char *pJiraKey)
{
	TicketEntry *pEntry = findTicketEntryByID(ticketID);
	if (!pEntry)
	{
		Errorf("Could not find ticket id: %u", ticketID); 
		return false;
	}
	return setJiraKey(pEntry, actorName, pJiraKey);
}

static void setSearchDataFlags(SearchData *sd)
{
	if (sd->pSummaryDescription)		sd->uFlags |= SEARCHFLAG_SUMMARY_DESCRIPTION;
	if (sd->pTrivia)					sd->uFlags |= SEARCHFLAG_TRIVIA;
	if (sd->iOccurrences)				sd->uFlags |= SEARCHFLAG_OCCURRENCES;
	if (sd->pAccountName)				sd->uFlags |= SEARCHFLAG_ACCOUNT_NAME;
	if (sd->pCharacterName)				sd->uFlags |= SEARCHFLAG_CHARACTER_NAME;
	if (sd->pShardInfoString)			sd->uFlags |= SEARCHFLAG_SHARD;
	if (sd->pVersion)					sd->uFlags |= SEARCHFLAG_VERSION;
	
	//Not implemented by tickettracker
	//if (sd->pIP)						sd->uFlags |= SEARCHFLAG_IP;

	if (sd->eStatus || eaiSize(&sd->peStatuses))
		sd->uFlags |= SEARCHFLAG_STATUS;
	if (sd->iVisible)
	{
		// 0 = do not search, 1 = private, 2 = public, 3= hidden
		sd->uFlags |= SEARCHFLAG_VISIBILITY;
	}
	// TODO change assignments search

	if (sd->pMainCategory)
	{
		sd->uFlags |= SEARCHFLAG_CATEGORY;
		sd->iMainCategory = categoryGetIndex(sd->pMainCategory);
		if (sd->pCategory)
			sd->iCategory = subcategoryGetIndex(sd->iMainCategory, sd->pCategory);
		else
			sd->iCategory = -1;
	}

	if (sd->pRepAccountName || sd->iIsAssigned)
	{
		sd->uFlags |= SEARCHFLAG_ASSIGNED;
	}
	if (sd->pGroupName)
	{
		TicketUserGroup *group = findTicketGroupByName(sd->pGroupName);
		if (group)
			sd->uGroupID = group->uID;
		else
		{
			Errorf("Could not find TicketGroup named '%s'.", sd->pGroupName);
			sd->uGroupID = 0;
		}
	}
	if (sd->uGroupID)
	{
		sd->uFlags |= SEARCHFLAG_GROUP;
	}

	if (sd->pProductName)				sd->uFlags |= SEARCHFLAG_PRODUCT_NAME;

	//Not implemented by tickettracker
	//if (sd->ePlatform)
	//	sd->uFlags |= SEARCHFLAG_PLATFORM;

	if (sd->uDateStart)					sd->uFlags |= SEARCHFLAG_DATESTART;
	if (sd->uDateEnd)					sd->uFlags |= SEARCHFLAG_DATEEND;
	if (sd->uStatusDateStart || sd->uStatusDateEnd)
		sd->uFlags |= SEARCHFLAG_STATUSUPDATE;
	if (sd->uModifiedDateStart || sd->uModifiedDateEnd)
		sd->uFlags |= SEARCHFLAG_MODIFIED;

	if (sd->uClosedStart || sd->uClosedEnd)
		sd->uFlags |= SEARCHFLAG_CLOSEDTIME;

	if (sd->iLocaleID)
		sd->uFlags |= SEARCHFLAG_LANGUAGE;

	if (sd->iLimit < 1) sd->iLimit = 200;

	//enable descending/ascending
	sd->uFlags |= SEARCHFLAG_SORT;
}

//Search for tickets
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
SearchData* TT_SearchTickets(const char *actorName, SearchData *sd)
{
	SearchData *result = StructCreate(parse_SearchData);
	TicketEntry *pEntry;

	setSearchDataFlags(sd);
	
	pEntry = searchFirst(sd);

	sd->iNumberOfResults = eaSize(&sd->ppSortedEntries);

	//some validation is important.
	if (sd->iOffset < 0)
		sd->iOffset = 0;

	if (sd->iOffset > sd->iNumberOfResults)
		sd->iOffset = sd->iNumberOfResults;

	if (sd->iOffset)
		eaRemoveRange(&sd->ppSortedEntries, 0, sd->iOffset);

	if (sd->iLimit && sd->iLimit < eaSize(&sd->ppSortedEntries))
		eaSetSize(&sd->ppSortedEntries, sd->iLimit);

	StructCopy(parse_SearchData, sd, result,0,0,0);
	result->ppSortedEntries = sd->ppSortedEntries;
	sd->ppSortedEntries = NULL;

	timingHistoryPush(gSearchHistory);

	return result;
}

//Search for tickets, only get count
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
int TT_SearchTicketsCount(const char *actorName, SearchData *sd)
{
	static U32 suSortDisable = ~SEARCHFLAG_SORT;
	int results;
	setSearchDataFlags(sd);
	sd->uFlags = sd->uFlags & suSortDisable; // disable sorting
	searchFirst(sd);

	results = eaSize(&sd->ppSortedEntries);
	eaDestroy(&sd->ppSortedEntries);
	timingHistoryPush(gSearchHistory);
	return results;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
int TT_GetTicketCount(const char *mainCategoryKey, const char *subcategoryKey)
{
	return searchCacheGetCategoryCount(mainCategoryKey, subcategoryKey, TICKETSTATUS_COUNT);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
int TT_GetTicketCountByLocale(const char *mainCategoryKey, const char *subcategoryKey, int localeID)
{
	return searchCacheGetCategoryCountForLocale(mainCategoryKey, subcategoryKey, TICKETSTATUS_OPEN, localeID-1);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
int TT_GetTicketCountByStatus(TicketStatus eStatus, const char *mainCategoryKey, const char *subcategoryKey)
{
	return searchCacheGetCategoryCount(mainCategoryKey, subcategoryKey, eStatus);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
int TT_GetTicketCountByProductLocale(TicketStatus eStatus, int localeID, const char *productName, const char *mainCategoryKey, const char *subcategoryKey)
{
	return searchCacheGetCategoryCountForProductLocale(mainCategoryKey, subcategoryKey, eStatus, localeID, productName);
}

// Finds first iNumTickets UNASSIGNED tickets from the search and assigns them to actor
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
SearchData* TT_GetNextTickets(const char *actorName, SearchData *sd, int iNumTickets)
{
	SearchData *result;
	TicketEntry *pEntry;
	int iCount = 0;

	setSearchDataFlags(sd);
	if (iNumTickets <= 0)
	{
		Errorf("Must request a positive, non-zero number of tickets!");
		return NULL;
	}
	if (iNumTickets > MAX_TICKET_REQUEST)
	{
		Errorf("Cannot request more than %d tickets at one time!", MAX_TICKET_REQUEST);
		return NULL;
	}
	result = StructCreate(parse_SearchData);
	sd->iIsAssigned = SearchToggle_No;
	sd->uFlags |= SEARCHFLAG_ASSIGNED; // this MUST be set
	sd->iLimit = iNumTickets;

	StructCopy(parse_SearchData, sd, result,0,0,0);

	pEntry = searchFirst(sd);
	while (pEntry && iCount < iNumTickets)
	{
		ticketAssignToAccountName(pEntry, actorName);
		eaPush(&result->ppSortedEntries, pEntry);
		iCount++;
		pEntry = searchNext(sd);
	}
	searchEnd(sd);

	result->iNumberOfResults = iCount;
	return result;
}

// Returns the Entity info as an XML string
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
char * TT_GetCharacterInfo (U32 id)
{
	TicketEntry *pEntry = findTicketEntryByID(id);
	char *result = NULL;

	if (!pEntry)
	{
		Errorf("Ticket ID #%d does not exist.", id);
		return NULL;
	}

	if (pEntry->pEntityFileName)
		appendEntityParseTable(&result, pEntry->uID, pEntry->uEntityDescriptorID, pEntry->pEntityFileName, true);
	else
	{
		Errorf("Ticket ID #%d does not have any attached Character Info", pEntry->uID);
		return NULL;
	}
	return result;
}

// Returns the valid locale search options as ID Index + Name
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
TT_NameAndIdList * TT_GetLocales(void)
{
	int i;
	TT_NameAndIdList *list = StructCreate(parse_TT_NameAndIdList);
	static char *sUnsupportedLanguage = "[Unsupported]";
	
	for (i=0; i<LOCALE_ID_COUNT; i++)
	{
		TT_NameAndId *locale = StructCreate(parse_TT_NameAndId);
		locale->id = i+1;

		if (locIDIsValid(i))
			estrCopy2(&locale->name, locGetName(i));
		else
			estrCopy2(&locale->name, sUnsupportedLanguage);
		eaPush(&list->list, locale);
	}
	return list;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_SubscribeToTicket (U32 uTicketID, const char *accountName, const char *displayName, const char *characterName, 
						   const char *websiteName)
{
	TicketData data = {0};
	TicketEntry *pEntry = findTicketEntryByID(uTicketID);

	if (!pEntry)
	{
		Errorf("Ticket ID #%d does not exist", uTicketID);
		return false;
	}
	if (!accountName)
	{
		Errorf("Must provide an account name to subscribe to a ticket");
		return false;
	}
	data.iMergeID = uTicketID;
	data.pAccountName = StructAllocString(accountName);
	data.pDisplayName = StructAllocString(displayName);
	data.pCharacterName = StructAllocString(characterName);
	data.pShardInfoString = StructAllocString(websiteName);

	mergeDataIntoExistingTicket(0, pEntry, &data);
	StructDeInit(parse_TicketData, &data);
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_EditTicket (const char *actorAccountName, U32 uTicketID, const char *pNewSummary, const char *pNewDescription)
{
	TicketEntry *pEntry = findTicketEntryByID(uTicketID);
	if (!pEntry)
	{
		Errorf("Ticket ID #%d does not exist", uTicketID);
		return false;
	}
	ticketUserEdit(NULL, pEntry, actorAccountName, 
		pNewSummary && *pNewSummary ? pNewSummary : NULL, 
		pNewDescription && *pNewDescription ? pNewDescription : NULL, false);
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_EditTicketAdmin (const char *actorAccountName, U32 uTicketID, const char *pNewSummary, const char *pNewDescription)
{
	TicketEntry *pEntry = findTicketEntryByID(uTicketID);
	if (!pEntry)
	{
		Errorf("Ticket ID #%d does not exist", uTicketID);
		return false;
	}
	ticketUserEdit(NULL, pEntry, actorAccountName, 
		pNewSummary && *pNewSummary ? pNewSummary : NULL, 
		pNewDescription && *pNewDescription ? pNewDescription : NULL, true);
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_MergeMultipleTickets (const char *actorAccountName, U32 uTicketID, TT_IdList *uMergeeIDs, const char *pNewSummary, const char *pNewDescription)
{
	TicketEntry *pEntry = findTicketEntryByID(uTicketID), **ppMergees = NULL;
	int i, size;
	if (!pEntry)
	{
		Errorf("Ticket ID #%d does not exist", uTicketID);
		return false;
	}
	if (pEntry->eStatus == TICKETSTATUS_MERGED)
	{
		Errorf("Ticket ID #%d was merged and cannot be merged into", uTicketID);
		eaDestroy(&ppMergees);
		return false;
	}
	size = eaiSize(&uMergeeIDs->uIDs);
	for (i=0; i<size; i++)
	{
		TicketEntry *pMergee = findTicketEntryByID(uMergeeIDs->uIDs[i]);
		if (!pMergee)
		{
			Errorf("Ticket ID #%d does not exist", uMergeeIDs->uIDs[i]);
			eaDestroy(&ppMergees);
			return false;
		}
		if (pEntry == pMergee)
		{
			Errorf("Cannot merge ticket into itself.");
			eaDestroy(&ppMergees);
			return false;
		}
		if (pMergee->eStatus == TICKETSTATUS_MERGED)
		{
			Errorf("Ticket ID #%d was already merged", uMergeeIDs->uIDs[i]);
			eaDestroy(&ppMergees);
			return false;
		}
		eaPush(&ppMergees, pMergee);
	}
	ticketMergeMultiple(actorAccountName, pEntry, ppMergees, pNewSummary, pNewDescription);
	eaDestroy(&ppMergees);
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_MergeTickets (const char *actorAccountName, U32 uTicketID, U32 uMergeeID, const char *pNewSummary, const char *pNewDescription)
{
	TicketEntry *pEntry = findTicketEntryByID(uTicketID), *pMergee;
	if (!pEntry)
	{
		Errorf("Ticket ID #%d does not exist", uTicketID);
		return false;
	}
	pMergee = findTicketEntryByID(uMergeeID);
	if (!pMergee)
	{
		Errorf("Ticket ID #%d does not exist", uMergeeID);
		return false;
	}
	if (pEntry == pMergee)
	{
		Errorf("Cannot merge ticket into itself.");
		return false;
	}
	if (pEntry->eStatus == TICKETSTATUS_MERGED)
	{
		Errorf("Ticket ID #%d was merged and cannot be merged into", uTicketID);
		return false;
	}
	if (pMergee->eStatus == TICKETSTATUS_MERGED)
	{
		Errorf("Ticket ID #%d was already merged", uMergeeID);
		return false;
	}
	ticketMergeExisting(actorAccountName, NULL, pEntry, pMergee, pNewSummary, pNewDescription, true);
	return true;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_UserCanEditTicket(const char *accountName, U32 uTicketID)
{
	TicketEntry *pEntry = findTicketEntryByID(uTicketID);
	if (!pEntry)
	{
		Errorf("Ticket ID #%d does not exist", uTicketID);
		return false;
	}
	return ticketUserCanEdit(pEntry, accountName);
}

//JiraProjectList * TT_GetJiraProjectList(); is in jira.c
//CategoryList* TT_GetCategorieList(); is in Category.c
//TT_NameAndIdList* TT_GetProductsList(); is in ProductNames.c

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
char * TT_GetCSVString (SearchData *sd)
{
	char *result = NULL;
	setSearchDataFlags(sd);
	timingHistoryPush(gSearchHistory);
	TicketTracker_DumpSearchToCSVString(&result, sd);
	if (result)
	{
		char *copy = NULL;
		estrCopyWithHTMLEscaping(&copy, result, false);
		estrDestroy(&result);
		result = copy;
	}
	return result;
}

// Gets the Top 10 Jiras filed from START to END time, based on number of tickets associated to it
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
TicketJiraReport *TT_GenerateTopTenJiras (U32 uStartTime, U32 uEndTime)
{
	TicketJiraReport *report = StructCreate(parse_TicketJiraReport);
	TicketTracker_GenerateJiraReport(&report->ppJiras, uStartTime, uEndTime);
	return report;
}

// Gets the Top 10 Knowledge Base Solutions filed from START to END time, based on number of tickets associated to it
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
TicketJiraReport *TT_GenerateTopTenSolutions (U32 uStartTime, U32 uEndTime)
{
	TicketJiraReport *report = StructCreate(parse_TicketJiraReport);
	TicketTracker_GenerateSolutionReport(&report->ppJiras, uStartTime, uEndTime);
	return report;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_SetSolutionKey (const char *actorAccountName, U32 uTicketID, U32 uSolutionID)
{
	TicketEntry *pEntry = findTicketEntryByID(uTicketID);
	if (!pEntry)
	{
		Errorf("Ticket ID #%d does not exist", uTicketID);
		return false;
	}
	return setTicketSolution(pEntry, actorAccountName, STACK_SPRINTF("KB-%d", uSolutionID));
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_SetSolutionJiraKey (const char *actorAccountName, U32 uTicketID, const char *pSolutionKey)
{
	TicketEntry *pEntry = findTicketEntryByID(uTicketID);
	if (!pEntry)
	{
		Errorf("Ticket ID #%d does not exist", uTicketID);
		return false;
	}
	return setTicketSolution(pEntry, actorAccountName, pSolutionKey);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_SetPhoneResolutionKey (const char *actorAccountName, U32 uTicketID, const char *pSolutionKey)
{
	TicketEntry *pEntry = findTicketEntryByID(uTicketID);
	if (!pEntry)
	{
		Errorf("Ticket ID #%d does not exist", uTicketID);
		return false;
	}
	return setTicketPhoneResolution(pEntry, actorAccountName, pSolutionKey);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
SearchData *TT_GetRandomTickets(const char *accountName, U32 uStartTime, U32 uEndTime, int iNumTickets)
{
	SearchData *sd = StructCreate(parse_SearchData);
	TicketEntry **randomEntries = NULL;
	int *eaiRandomIndices = NULL;
	int i;

	searchDataInitialize(sd);
	if (uStartTime || uEndTime)
	{
		sd->uStatusDateStart = uStartTime;
		sd->uStatusDateEnd = uEndTime;
		sd->uFlags |= SEARCHFLAG_STATUSUPDATE;
	}
	if (accountName && *accountName)
	{
		sd->pRepAccountName = StructAllocString(accountName);
		sd->uFlags |= SEARCHFLAG_ASSIGNED;
	}
	else
	{
		Errorf("Must specify an account name.");
		return NULL;
	}
	sd->eAdminSearch = SEARCH_ADMIN;

	searchFirst(sd);

	sd->iNumberOfResults = eaSize(&sd->ppSortedEntries);
	if (iNumTickets >= sd->iNumberOfResults)
	{
		// Return everything
		return sd;
	}

	while (eaiSize(&eaiRandomIndices) < iNumTickets)
	{
		eaiPushUnique(&eaiRandomIndices, randInt(sd->iNumberOfResults));
	}
	for (i=0; i<iNumTickets; i++)
	{
		eaPush(&randomEntries, sd->ppSortedEntries[eaiRandomIndices[i]]);
	}
	searchEnd(sd);
	sd->iNumberOfResults = iNumTickets;
	sd->ppSortedEntries = randomEntries;
	return sd;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
char * TT_GetChatLog(U32 id)
{
	TicketEntry *pEntry = findTicketEntryByID(id);
	char *result = NULL;
	ParseTable **ppParseTables = NULL;
	void *pData = NULL;
	char *ptiName = NULL;
	int i;
	TicketUserData userdata = {0};
	char filepath[MAX_PATH];
	int iNumDescriptors;

	if (!pEntry)
	{
		Errorf("Ticket ID #%d does not exist.", id);
		return NULL;
	}

	if (!pEntry->pUserDataFilename)
		return NULL;
	
	sprintf(filepath, "%s\\%s", GetTicketFileParentDirectory(), pEntry->pUserDataFilename);
	mkdirtree(filepath);

	ParserReadTextFile(filepath, parse_TicketUserData, &userdata, 0);

	iNumDescriptors = eaiSize(&pEntry->eaiUserDataDescriptorIDs);
	for (i=eaSize(&userdata.eaUserDataStrings)-1; i>=0; i--)
	{
		if (i < iNumDescriptors)
		{
			bool loaded = loadParseTableAndStruct (&ppParseTables, &pData, &ptiName, pEntry->eaiUserDataDescriptorIDs[i], 
				userdata.eaUserDataStrings[i]);
			if (loaded)
			{
				if (stricmp(ptiName, "CBugChatLog") == 0)
				{
					char *tempXML = NULL;
					estrStackCreate(&tempXML);
					ParserWriteXML(&tempXML, ppParseTables[0], pData);
					estrCopyWithHTMLEscaping(&result, tempXML, false);
					estrDestroy(&tempXML);
				}
				destroyParseTableAndStruct(&ppParseTables, &pData);
				if (result)
					break;
			}
		}
	}
	StructDeInit(parse_TicketUserData, &userdata);
	return result;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
int TT_WriteDXDiag(U32 uID, const char *dxdiag)
{
	TicketEntry *pEntry = findTicketEntryByID(uID);
	if (pEntry)
	{
		TicketEntry_WriteDxDiag(pEntry, dxdiag);
		return (int) strlen(dxdiag);
	}
	Errorf("Ticket ID #%d does not exist.", uID);
	return -1;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
bool TT_HasDXDiag(U32 uID)
{
	TicketEntry *pEntry = findTicketEntryByID(uID);
	if (pEntry)
		return pEntry->pDxDiagFilename != NULL;
	Errorf("Ticket ID #%d does not exist.", uID);
	return false;
}


#include "autogen/ticketapi_h_ast.c"