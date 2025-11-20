#include "Authentication.h"
#include "Authentication_h_ast.h"
#include "TicketTracker.h"
#include "TicketTracker_h_ast.h"
#include "TicketAssignment.h"
#include "TicketAssignment_h_ast.h"

#include "Search.h"
#include "earray.h"
#include "estring.h"
#include "objContainerIO.h"
#include "qsortG.h"

#define CONTAINER_ENTRY_ID 1

TicketUserGroup * findTicketGroupByID(U32 uID)
{
	Container * con = objGetContainer(GLOBALTYPE_TICKETGROUP, uID);
	if (con)
		return CONTAINER_GROUP(con);
	return NULL;
}

TicketUserGroup * findTicketGroupByName(const char * pGroupName)
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;

	objInitContainerIteratorFromType(GLOBALTYPE_TICKETGROUP, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		TicketUserGroup *pGroup = CONTAINER_GROUP(currCon);
		if (stricmp(pGroup->pName, pGroupName) == 0)
		{
			objClearContainerIterator(&iter);
			return CONTAINER_GROUP(currCon);
		}
		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
	return NULL;
}

TicketUserGroup * createTicketGroup(const char *pName)
{
	Container * con = NULL;
	NOCONST(TicketUserGroup) *pGroup;
	if (findTicketGroupByName(pName))
		return NULL;
	if (!pName || !*pName)
		return NULL;

	pGroup = StructCreateNoConst(parse_TicketUserGroup);
	pGroup->pName = StructAllocString(pName);
	con = objAddExistingContainerToRepository(GLOBALTYPE_TICKETGROUP, 0, pGroup);
	objChangeContainerState(con, CONTAINERSTATE_OWNED, GLOBALTYPE_TICKETGROUP, CONTAINER_ENTRY_ID);
	return (TicketUserGroup*) pGroup;
}

void iterateOverTicketGroups (SA_PARAM_NN_VALID TicketGroupIteratorFunc pFunc, void *userData)
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	objInitContainerIteratorFromType(GLOBALTYPE_TICKETGROUP, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		TicketUserGroup *pGroup = CONTAINER_GROUP(currCon);	
		pFunc(pGroup, userData);
		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
}

int getTicketGroupCount(void)
{
	return objCountTotalContainersWithType(GLOBALTYPE_TICKETGROUP);
}

TicketTrackerUser * findUserByID(U32 id)
{
	Container * con = objGetContainer(GLOBALTYPE_TICKETUSER, id);
	if (con)
		return CONTAINER_USER(con);
	return NULL;
}

#include "Authentication_h_ast.c"