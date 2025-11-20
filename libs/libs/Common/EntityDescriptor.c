#include "EntityDescriptor.h"
#include "EntityDescriptor_h_ast.h"

#include "estring.h"
#include "crypt.h"

#include "objContainer.h"
#include "objTransactions.h"

static EntityDescriptorList *gpEntityList = NULL;

static bool sbUsingContainers = false;

////////////////////////////////////////////
// Container Functions
#define CONTAINER_ENTRY_ID 1

void setEntityDescriptorContainers(bool bIsUsing)
{
	sbUsingContainers = bIsUsing;
}

// Import from list into containers
void importEntityDescriptors (EntityDescriptorList *pList)
{
	int i,size;
	if (!devassert(sbUsingContainers))
		return;
	if (!pList)
		return;

	size = eaSize(&pList->ppEntities);
	for (i=0; i<size; i++)
	{
		if (findEntityDescriptorByID(pList->ppEntities[i]->uID) == NULL)
		{
			Container *con = objAddExistingContainerToRepository(GLOBALTYPE_ENTITYDESCRIPTOR, pList->ppEntities[i]->uID, pList->ppEntities[i]);
			objChangeContainerState(con, CONTAINERSTATE_OWNED, GLOBALTYPE_ENTITYDESCRIPTOR, CONTAINER_ENTRY_ID);
		}
	}
}

////////////////////////////////////
// General Functions

void loadEntityDescriptorList (EntityDescriptorList *pList)
{
	gpEntityList = pList;
}
EntityDescriptorList *getEntityDescriptorList(void)
{
	return gpEntityList;
}

static bool hashMatchesU32(const U32 *h1, const U32 *h2)
{
	return (
		(h1[0] == h2[0])
	&&  (h1[1] == h2[1])
	&&  (h1[2] == h2[2])
	&&  (h1[3] == h2[3]));
}

EntityDescriptor *findEntityDescriptorByID(U32 uID)
{
	if (sbUsingContainers)
	{
		Container *con = objGetContainer(GLOBALTYPE_ENTITYDESCRIPTOR, uID);
		if (con)
			return (EntityDescriptor*) con->containerData;
	}
	else
	{
		int i;
		if (!gpEntityList)
			return NULL;
		for (i=0; i<eaSize(&gpEntityList->ppEntities); i++)
		{
			if (gpEntityList->ppEntities[i]->uID == uID)
				return gpEntityList->ppEntities[i];
		}
	}
	return NULL;
}

U32 getNextEntityDescriptorID(void)
{
	if (!gpEntityList)
		return 0;
	if (!gpEntityList->uNextEntityID)
		gpEntityList->uNextEntityID++;

	do 
	{
		gpEntityList->uNextEntityID++;
	} while (findEntityDescriptorByID(gpEntityList->uNextEntityID));

	return gpEntityList->uNextEntityID;
}

U32 addEntityDescriptor(char *pEntityPTIStr)
{
	NOCONST(EntityDescriptor) *pNewEnt = StructCreateNoConst(parse_EntityDescriptor);

	cryptMD5(pEntityPTIStr, (int) strlen(pEntityPTIStr), pNewEnt->aiUniqueHash);

	if (sbUsingContainers)
	{
		ContainerIterator iter = {0};
		Container *currCon = NULL;

		// TODO(Theo) index this lookup
		objInitContainerIteratorFromType(GLOBALTYPE_ENTITYDESCRIPTOR, &iter);
		currCon = objGetNextContainerFromIterator(&iter);
		while (currCon)
		{
			EntityDescriptor *descriptor = (EntityDescriptor*) currCon->containerData;
			if (hashMatchesU32(descriptor->aiUniqueHash, pNewEnt->aiUniqueHash))
			{
				StructDestroyNoConst(parse_EntityDescriptor, pNewEnt);
				return descriptor->uID;
			}
			currCon = objGetNextContainerFromIterator(&iter);
		}
		objClearContainerIterator(&iter);
	}
	else
	{
		int i;
		if (!gpEntityList)
			gpEntityList = StructCreate(parse_EntityDescriptorList);
		for (i=0; i<eaSize(&gpEntityList->ppEntities); i++)
		{
			if (hashMatchesU32(gpEntityList->ppEntities[i]->aiUniqueHash, pNewEnt->aiUniqueHash))
			{
				StructDestroyNoConst(parse_EntityDescriptor, pNewEnt);
				return gpEntityList->ppEntities[i]->uID;
			}
		}
		pNewEnt->uID = getNextEntityDescriptorID();
	}
	pNewEnt->pEntityPTIStr = strdup(pEntityPTIStr);
	if (sbUsingContainers)
	{
		objRequestContainerCreateLocal(NULL, GLOBALTYPE_ENTITYDESCRIPTOR, pNewEnt);
		StructDestroyNoConst(parse_EntityDescriptor, pNewEnt);
		// The new maxID is the same as the new EntityDescriptor's ID
		return objContainerGetMaxID(GLOBALTYPE_ENTITYDESCRIPTOR);
	}
	else
	{
		eaPush(&gpEntityList->ppEntities, (EntityDescriptor*) pNewEnt);
		return pNewEnt->uID;
	}
}

bool loadParseTableAndStruct (ParseTable ***pti, void ** pData, char **estrName, U32 uDescriptorID, const char *pStructStr)
{
	int size = 0;
	EntityDescriptor *pEnt = findEntityDescriptorByID(uDescriptorID);

	if (pEnt)
	{
		ParseTableReadText(pEnt->pEntityPTIStr, pti, &size, estrName, 
			PARSETABLESENDFLAG_CONVERT_BITFIELDS_TO_APPROPRIATE_SIZED_INTS);
		if (size > 0)
		{
			if (pData)
			{
				*pData = StructCreateVoid((*pti)[0]);
				ParserReadText(pStructStr, (*pti)[0], *pData, 0);
				return true;
			}
		}
		ParseTableFree(pti);
	}
	return false;
}

void getEntityDescriptorName (char **estr, U32 uDescriptorID)
{
	EntityDescriptor *pEnt = findEntityDescriptorByID(uDescriptorID);
	ParseTable **pti = NULL;
	int size = 0;
	
	if (pEnt)
	{
		ParseTableReadText(pEnt->pEntityPTIStr, &pti, &size, estr, 
			PARSETABLESENDFLAG_CONVERT_BITFIELDS_TO_APPROPRIATE_SIZED_INTS);
		ParseTableFree(&pti);
	}
}

void destroyParseTableAndStruct (ParseTable ***pti, void ** pData)
{
	if (pData)
	{
		StructDestroyVoid((*pti)[0], *pData);
		*pData = NULL;
	}
	ParseTableFree(pti);
}

bool entitySearchForSubstring(const char * pEntityStr, const char *pKey)
{
	char * pTemp, *pCur;
	char * pContext = NULL;
	bool bFound = false;
	
	if (!pEntityStr || !pKey)
		return false;

	pTemp = strdup(pEntityStr);
	pCur = strtok_s(pTemp, "\r\n", &pContext);
	while (pCur != NULL && !bFound)
	{
		if (pCur[0])
		{
			char *pLine = NULL;
			char *pTrimmed = NULL;
			estrCopy2(&pLine, pCur);
			estrTrimLeadingAndTrailingWhitespace(&pLine);
			pTrimmed = pLine;
			while (pTrimmed[0] != 0 && pTrimmed[0] != ' ')
				pTrimmed++;
			while (pTrimmed[0] == ' ')
				pTrimmed++;
			if (strstri(pTrimmed, pKey))
			{
				bFound = true;
			}
			estrDestroy(&pLine);
		}
		pCur = strtok_s(NULL, "\r\n", &pContext);
	}
	free(pTemp);
	return bFound;
}

#include "EntityDescriptor_h_ast.c"