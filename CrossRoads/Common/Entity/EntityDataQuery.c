/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityDataQuery.h"
#include "objContainer.h"

// Returns a ContainerRefArrray of all containers of the same type. Useful for TestClient
AUTO_COMMAND ACMD_CATEGORY(Test);
ContainerRefArray *GetAllContainersOfType(int type)
{
	ContainerRefArray *refArray = CreateContainerRefArray();
	ContainerIterator iter;
	Container *con;

	objInitContainerIteratorFromType(type,&iter);

	while (con = objGetNextContainerFromIterator(&iter))
	{
		AddToContainerRefArray(refArray,type,con->containerID);
	}
	objClearContainerIterator(&iter);
	return refArray;
}