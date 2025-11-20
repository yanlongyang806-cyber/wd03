/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include <stdarg.h>
#include "windefinclude.h"

#include "Character.h"
#include "entitysysteminternal.h"
#include "entityiterator.h"
#include "EntitySavedData.h"
#include "Estring.h"
#include "referencesystem.h"
#include "team.h"

#ifdef GAMECLIENT
#include "gclEntity.h"
#endif

#include "ResourceSystem_Internal.h"
#include "AutoGen/Entity_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


Entity *gpMainEntityList[MAX_ENTITIES_PRIVATE] = {0};
Entity **gpLastFrameEntityList = NULL;

EntityListNode gEntityListNodes[MAX_ENTITIES_PRIVATE] = {0};


EntityListNode *gpEntityTypeLists[GLOBALTYPE_MAXTYPES] = {0};

//the free list node list is used to track free entities, as there is no point in also having a free entity list.
EntityListNode *gpFirstFreeEntListNode = NULL;

bool gbAmGameServer = false;
bool gbEntSystemInitted = false;

//the highest index of an active entity. Hopefully not MUCH higher than the total number of entities
int gHighestActiveEntityIndex;

// Global entity structure list
Entity **g_eaEntityStructs = NULL;

#ifdef GAMESERVER
	EntityInfo entInfo[MAX_ENTITIES_PRIVATE];
#endif


void ASSERT_ENTITY_OK(Entity *pEntity)
{
	U32 index;

	assert(pEntity);

	index = INDEX_FROM_ENTITY(pEntity);

	assert(index >= 0 && index < MAX_ENTITIES_PRIVATE);
	
	assert(pEntity == ENTITY_FROM_INDEX(index));

	assert(pEntity->myRef);
	assert(INDEX_FROM_REFERENCE(pEntity->myRef) == index);

	assert((int)index <= gHighestActiveEntityIndex);
}


void EntSystem_FreeEntity(Entity **ppEntity)
{
	PERFINFO_AUTO_START_FUNC();
	StructDestroy(parse_Entity, *ppEntity);
	*ppEntity = NULL;
	PERFINFO_AUTO_STOP();
}

Entity *EntSystem_AllocEntity(char *pComment)
{
	Entity* e;
	
	PERFINFO_AUTO_START_FUNC();
	e = StructCreateWithComment(parse_Entity, pComment);
	PERFINFO_AUTO_STOP();
	
	return e;
}


//entities are added to the free list in a sorted fashion, to try to keep entities clustered near the low indices whenever possible
static void AddEntityToFreeList(EntityListNode *pListNode)
{
	if (!gpFirstFreeEntListNode)
	{
		pListNode->pNext = NULL;
		pListNode->pPrev = NULL;
		gpFirstFreeEntListNode = pListNode;
	}
	else
	{
		if (pListNode < gpFirstFreeEntListNode)
		{
			pListNode->pNext = gpFirstFreeEntListNode;
			gpFirstFreeEntListNode->pPrev = pListNode;
			pListNode->pPrev = NULL;
			gpFirstFreeEntListNode = pListNode;
		}
		else
		{
			EntityListNode *pNodeToInsertAfter = gpFirstFreeEntListNode;

			while (pNodeToInsertAfter->pNext && pNodeToInsertAfter->pNext < pListNode)
			{
				pNodeToInsertAfter = pNodeToInsertAfter->pNext;
			}
		
			pListNode->pNext = pNodeToInsertAfter->pNext;
			pListNode->pPrev = pNodeToInsertAfter;
			pNodeToInsertAfter->pNext = pListNode;
			if (pListNode->pNext)
			{
				pListNode->pNext->pPrev = pListNode;
			}
		}
	}
}



void EntSystem_LowLevelDeleteEntityEx(Entity *pEntity, const char* file, int line)
{
	int i;
	int iMyIndex;
	EntityListNode *pMyListNode;
	NOCONST(Entity) *pModEntity = CONTAINER_NOCONST(Entity, pEntity);

	ASSERT_INITTED();
	ASSERT_ENTITY_OK(pEntity);

	iMyIndex = INDEX_FROM_REFERENCE(pEntity->myRef);
	pMyListNode = gEntityListNodes + iMyIndex;

	//first check for any EntityIterator that is currently pointing at this entity
	for (i=0; i < MAX_ENTITY_ITERATORS; i++)
	{
		if (gEntityIterators[i].eCurType != ENTITYTYPE_INVALID)
		{
			if (gEntityIterators[i].ptr.pLastNodeReturned == pMyListNode)
			{
				EntityIterator_Backup(&gEntityIterators[i]);
			}
		}
	}

	//delete backup copy of the entity for diff sending
	if (pEntity->mySendFlags & ENT_SEND_FLAG_LASTFRAME_COPY_EXISTS)
	{
		StructDeInit(parse_Entity, gpLastFrameEntityList[iMyIndex]);
		EntSystem_FreeEntity(&gpLastFrameEntityList[iMyIndex]);

	}

	//destroy textparser bits of this entity
	StructDeInit(parse_Entity,pEntity); 

	//now remove entity from entity lists
	if (pMyListNode->pNext)
	{
		pMyListNode->pNext->pPrev = pMyListNode->pPrev;
	}

	if (pMyListNode->pPrev)
	{
		pMyListNode->pPrev->pNext = pMyListNode->pNext;
	}
	else
	{
		gpEntityTypeLists[pEntity->myEntityType] = pMyListNode->pNext;
	}

	//now add the entity to the free list
	AddEntityToFreeList(pMyListNode);


	if (iMyIndex == gHighestActiveEntityIndex)
	{
		do
		{
			gHighestActiveEntityIndex--;
		}
		while (gHighestActiveEntityIndex >= 0 && !ENTITY_FROM_INDEX(gHighestActiveEntityIndex));
	}

	//zero out this entity in the list(s)
	EntSystem_FreeEntity(&gpMainEntityList[iMyIndex]);

}

Entity *EntSystem_LowLevelGetEmptyEntity(GlobalType eType, char *pComment)
{

	EntityListNode *pMyListNode;
	Entity *pEntity;
	int iMyIndex;
	NOCONST(Entity) *pModEntity;


	ASSERT_INITTED();


	//if this assert is hit, it's beacuse we have too many total entities
	assert(gpFirstFreeEntListNode);

	assert(eType > GLOBALTYPE_NONE && eType < GLOBALTYPE_MAXTYPES);

	pMyListNode = gpFirstFreeEntListNode;
	iMyIndex = pMyListNode - gEntityListNodes;

	assert(gpMainEntityList[iMyIndex] == NULL);
	gpMainEntityList[iMyIndex] = EntSystem_AllocEntity(pComment);

	pEntity = ENTITY_FROM_INDEX(iMyIndex);
	pModEntity = CONTAINER_NOCONST(Entity, pEntity);

	pEntity->myRef = REFERENCE_FROM_INDEX_AND_ID(iMyIndex, pMyListNode->iNextID++);
	assert(pEntity->myRef);

	//in case so many entities have been created in this slot that we have wrapped around to zero
	if (!REFERENCE_FROM_INDEX_AND_ID(0, pMyListNode->iNextID))
	{
		pMyListNode->iNextID = 1;
	}

	pModEntity->myEntityType = eType;

	//initialize send flags
	pEntity->mySendFlags = STARTING_ENT_SEND_FLAGS;


	//remove entity from free list
	gpFirstFreeEntListNode = pMyListNode->pNext;
	if (gpFirstFreeEntListNode)
	{
		gpFirstFreeEntListNode->pPrev = NULL;
	}


	//add entity to sorted-by-type list. This can't break iterators because we always add at the beginning of a list, and 
	//the iterator either points to a list node, or to space between lists
	if (gpEntityTypeLists[eType])
	{
		gpEntityTypeLists[eType]->pPrev = pMyListNode;
		pMyListNode->pNext = gpEntityTypeLists[eType];
		pMyListNode->pPrev = NULL;

		gpEntityTypeLists[eType] = pMyListNode;
	}
	else
	{
		gpEntityTypeLists[eType] = pMyListNode;
		pMyListNode->pNext = pMyListNode->pPrev = NULL;
	}

	if (iMyIndex > gHighestActiveEntityIndex)
	{
		gHighestActiveEntityIndex = iMyIndex;
	}

#ifdef GAMESERVER
	entSetActive(pEntity);
#endif

	return pEntity;
}


void EntSystem_LowLevelRegisterExisting(Entity *pEntity)
{

	EntityListNode *pMyListNode;
	int iMyIndex;

	NOCONST(Entity) *pModEntity;

	GlobalType eType = pEntity->myEntityType;


	ASSERT_INITTED();


	//if this assert is hit, it's beacuse we have too many total entities
	assert(gpFirstFreeEntListNode);

	assert(eType > GLOBALTYPE_NONE && eType < GLOBALTYPE_MAXTYPES);

	pMyListNode = gpFirstFreeEntListNode;
	iMyIndex = pMyListNode - gEntityListNodes;

	assert(gpMainEntityList[iMyIndex] == NULL);
	gpMainEntityList[iMyIndex] = pEntity;
	pModEntity = CONTAINER_NOCONST(Entity, pEntity);

	pEntity->myRef = REFERENCE_FROM_INDEX_AND_ID(iMyIndex, pMyListNode->iNextID++);
	assert(pEntity->myRef);

	//in case so many entities have been created in this slot that we have wrapped around to zero
	if (!REFERENCE_FROM_INDEX_AND_ID(0, pMyListNode->iNextID))
	{
		pMyListNode->iNextID = 1;
	}

	//initialize send flags
	pEntity->mySendFlags = STARTING_ENT_SEND_FLAGS;


	//remove entity from free list
	gpFirstFreeEntListNode = pMyListNode->pNext;
	if (gpFirstFreeEntListNode)
	{
		gpFirstFreeEntListNode->pPrev = NULL;
	}


	//add entity to sorted-by-type list. This can't break iterators because we always add at the beginning of a list, and 
	//the iterator either points to a list node, or to space between lists
	if (gpEntityTypeLists[eType])
	{
		gpEntityTypeLists[eType]->pPrev = pMyListNode;
		pMyListNode->pNext = gpEntityTypeLists[eType];
		pMyListNode->pPrev = NULL;

		gpEntityTypeLists[eType] = pMyListNode;
	}
	else
	{
		gpEntityTypeLists[eType] = pMyListNode;
		pMyListNode->pNext = pMyListNode->pPrev = NULL;
	}

	if (iMyIndex > gHighestActiveEntityIndex)
	{
		gHighestActiveEntityIndex = iMyIndex;
	}

#ifdef GAMESERVER
	entSetActive(pEntity);
#endif
}

Entity *EntSystem_LowLevelGetEmptyEntity_SpecifyReference(EntityRef eRef, GlobalType eType, char *pComment)
{
	EntityListNode *pMyListNode;
	Entity *pEntity;
	NOCONST(Entity) *pModEntity;
	int iMyIndex;

	ASSERT_INITTED();

	assert(eRef);

	iMyIndex = INDEX_FROM_REFERENCE(eRef);

	//if this assert is hit, someone is trying to create a specified entity which already exists, probably
	//due to server/client out-of-synch-ness
	assert(ENTITY_FROM_INDEX(iMyIndex) == NULL);

	gpMainEntityList[iMyIndex] = EntSystem_AllocEntity(pComment);

	pEntity = ENTITY_FROM_INDEX(iMyIndex);
	pModEntity = CONTAINER_NOCONST(Entity, pEntity);

	pMyListNode = gEntityListNodes + iMyIndex;
	pMyListNode->iNextID = ID_FROM_REFERENCE(eRef) + 1;

	//in case so many entities have been created in this slot that we have wrapped around to zero
	if (!REFERENCE_FROM_INDEX_AND_ID(0, pMyListNode->iNextID))
	{
		pMyListNode->iNextID = 1;
	}

	pEntity->myRef = eRef;
	pModEntity->myEntityType = eType;

	//initialize send flags
	pEntity->mySendFlags = STARTING_ENT_SEND_FLAGS;

	//remove entity from free list
	if (pMyListNode->pPrev)
	{
		pMyListNode->pPrev->pNext = pMyListNode->pNext;
	}
	else
	{
		assert(pMyListNode == gpFirstFreeEntListNode);
		gpFirstFreeEntListNode = pMyListNode->pNext;
	}

	if (pMyListNode->pNext)
	{
		pMyListNode->pNext->pPrev = pMyListNode->pPrev;
	}

	//add entity to sorted-by-type list. This can't break iterators because we always add at the beginning of a list, and 
	//the iterator either points to a list node, or to space between lists
	if (gpEntityTypeLists[eType])
	{
		gpEntityTypeLists[eType]->pPrev = pMyListNode;
		pMyListNode->pNext = gpEntityTypeLists[eType];
		pMyListNode->pPrev = NULL;

		gpEntityTypeLists[eType] = pMyListNode;
	}
	else
	{
		gpEntityTypeLists[eType] = pMyListNode;
		pMyListNode->pNext = pMyListNode->pPrev = NULL;
	}

	if (iMyIndex > gHighestActiveEntityIndex)
	{
		gHighestActiveEntityIndex = iMyIndex;
	}


	return pEntity;
}




void EntSystem_Init(bool bIsServer, bool bReset)
{
	int i;

	assert(bReset || !gbEntSystemInitted);

	gbEntSystemInitted = true;

	assert(MAX_ENTITIES_PRIVATE <= (1 << MAX_ENTITIES_BITS));

	if (bReset)
	{
		for (i=0; i < MAX_ENTITIES_PRIVATE; i++)
		{
			if (gpMainEntityList[i])
			{
				EntSystem_FreeEntity(&gpMainEntityList[i]);
			}

			if (gpLastFrameEntityList && gpLastFrameEntityList[i])
			{
				EntSystem_FreeEntity(&gpLastFrameEntityList[i]);

			}
		}

	}
	else
	{	
		gpLastFrameEntityList = calloc(MAX_ENTITIES_PRIVATE * sizeof(Entity*), 1);
	}

	for (i=0; i < MAX_ENTITIES_PRIVATE; i++)
	{
		gEntityListNodes[i].iNextID = 1;
		gEntityListNodes[i].pNext = ( i == MAX_ENTITIES_PRIVATE-1 ? NULL : &gEntityListNodes[i+1]);
		if (i == 0)
		{
			gEntityListNodes[i].pPrev = NULL;
		}
		else
		{
			ANALYSIS_ASSUME(i > 0);
#pragma warning(suppress:6200) // /analyze fails to recognize ANALYSIS_ASSUME() above
			gEntityListNodes[i].pPrev = &gEntityListNodes[i-1];
		}
	}

	gpFirstFreeEntListNode = &gEntityListNodes[0];

	for (i=0; i < MAX_ENTITY_ITERATORS; i++)
	{
		gEntityIterators[i].eCurType = ENTITYTYPE_INVALID;
		gEntityIterators[i].ptr.pNext = (i == MAX_ENTITY_ITERATORS - 1 ? NULL : &gEntityIterators[i+1]);
	}

	gpFirstFreeEntityIterator = &gEntityIterators[0];

	gHighestActiveEntityIndex = -1;
}

void EntSystem_CheckForCorruption(void)
{
	int iCount = 0;
	EntityListNode *pListNode;
	Entity *pEntity;
	U32 myIndex;
	U32 eType;

	ASSERT_INITTED();

	pListNode = gpFirstFreeEntListNode;

	while (pListNode)
	{
		assert(pListNode >= gEntityListNodes);
		
		myIndex = pListNode - gEntityListNodes;

		assert(myIndex < MAX_ENTITIES_PRIVATE);
		assert(pListNode == gEntityListNodes + myIndex);

		pEntity = ENTITY_FROM_INDEX(myIndex);
		assert(!pEntity);

		iCount++;
		assert(iCount <= MAX_ENTITIES_PRIVATE);

		pListNode = pListNode->pNext;
	}

	for (eType = GLOBALTYPE_NONE + 1; eType < GLOBALTYPE_MAXTYPES; eType++)
	{
		pListNode = gpEntityTypeLists[eType];

		while (pListNode)
		{
			assert(pListNode >= gEntityListNodes);
			
			myIndex = pListNode - gEntityListNodes;

			assert(myIndex < MAX_ENTITIES_PRIVATE);
			assert(pListNode == gEntityListNodes + myIndex);

			pEntity = ENTITY_FROM_INDEX(myIndex);
			assert(pEntity);

			assert(INDEX_FROM_REFERENCE(pEntity->myRef) == myIndex);
			assert(pEntity->myEntityType == (GlobalType)eType);

			iCount++;
			assert(iCount <= MAX_ENTITIES_PRIVATE);

			pListNode = pListNode->pNext;
		}
	}

	assert(iCount == MAX_ENTITIES_PRIVATE);
}
/*
#define NUM_TEST_ITERATORS 4

#define RANDOM_TYPE ( rand() % (GLOBALTYPE_MAXTYPES - 1) + 1)

void EntSystem_RunTest(void)
{
	struct EntityIterator *pIterators[NUM_TEST_ITERATORS] = { NULL, NULL, NULL, NULL };

	do
	{
		int iNumToCreate = rand() % (MAX_ENTITIES_PRIVATE - 100) + 100;
		int iNumToDestroy = rand() % (MAX_ENTITIES_PRIVATE - 100) + 100;
//		int iNumToCreate = 10;
//		int iNumToDestroy = 10;
		int iNumActuallyDestroyed = 0;

		int i, j;

		printf("Going to create %d entities, then delete %d entities\n", iNumToCreate, iNumToDestroy);

		for (i=0; i < iNumToCreate; i++)
		{
			if (gpFirstFreeEntListNode)
			{
				Entity *pEntity = EntSystem_LowLevelGetEmptyEntity(RANDOM_TYPE);
				if (rand() % 100 > 50)
				{
					static U32 iNextID = 1;
					EntSystem_SetEntContainerID(pEntity, ++iNextID);
				}
			}
		}

		EntSystem_CheckForCorruption();

		for (i=0; i < iNumToDestroy; i++)
		{
			int iIteratorNum = rand() % NUM_TEST_ITERATORS;

			int iNumToSkip = rand() % 100 + 1;

			Entity *pEntity;

			if (!pIterators[iIteratorNum])
			{
				pIterators[iIteratorNum] = entGetIteratorAllTypes(0, 0);
				assert(pIterators[iIteratorNum]);
			}

			for (j=0; j < iNumToSkip; j++)
			{
				pEntity = EntityIteratorGetNext(pIterators[iIteratorNum]);

				if (!pEntity)
				{
					EntityIteratorRelease(pIterators[iIteratorNum]);
					pIterators[iIteratorNum] = entGetIteratorAllTypes(0, 0);
					assert(pIterators[iIteratorNum]);
				}
			}

			if (pEntity)
			{
				iNumActuallyDestroyed++;
				EntSystem_LowLevelDeleteEntity(pEntity);
			}
		}

		printf("Actually destroyed %d\n", iNumActuallyDestroyed);

		EntSystem_CheckForCorruption();
	} while (1);
}*/

Entity *entFromEntityRef(int iPartitionIdx, EntityRef iRef)
{
	if (!gbEntSystemInitted)
	{
		return NULL;
	}
	else
	{
		int iMyIndex = INDEX_FROM_REFERENCE(iRef);
		Entity *pEntity;

		if (iMyIndex < 0)
			return NULL;

		if(iMyIndex >= MAX_ENTITIES_PRIVATE)
		{
			#ifdef GAMECLIENT
				return gclClientOnlyEntFromEntityRef(iRef);
			#else
				return NULL;
			#endif
		}

		pEntity = ENTITY_FROM_INDEX(iMyIndex);

		if (pEntity && pEntity->myRef == iRef && (pEntity->iPartitionIdx_UseAccessor == iPartitionIdx))
		{
			return pEntity;
		}

		return NULL;
	}
}

Entity *entFromEntityRefAnyPartition(EntityRef iRef)
{
	if (!gbEntSystemInitted)
	{
		return NULL;
	}
	else
	{
		int iMyIndex = INDEX_FROM_REFERENCE(iRef);
		Entity *pEntity;

		if (iMyIndex < 0)
			return NULL;

		if(iMyIndex >= MAX_ENTITIES_PRIVATE)
		{
			#ifdef GAMECLIENT
				return gclClientOnlyEntFromEntityRef(iRef);
			#else
				return NULL;
			#endif
		}

		pEntity = ENTITY_FROM_INDEX(iMyIndex);

		if (pEntity && pEntity->myRef == iRef)
		{
			return pEntity;
		}

		return NULL;
	}
}

bool entHasRefExistedRecently(EntityRef iRef)
{
	if (!gbEntSystemInitted)
	{
		return false;
	}
	else
	{
		int iMyIndex = INDEX_FROM_REFERENCE(iRef);
		int iMyID = ID_FROM_REFERENCE(iRef);
		EntityListNode *pMyListNode;
		int iDif;

		if (iMyIndex < 0 || iMyIndex >= MAX_ENTITIES_PRIVATE)
			return false;

		if (iMyID == 0)
		{
			return false;
		}

		pMyListNode = gEntityListNodes + iMyIndex;

		iDif = (pMyListNode->iNextID + MAX_IDS_PER_SLOT - iMyID) % MAX_IDS_PER_SLOT;
		if (iDif < ENT_CREATIONS_PER_SLOT_THAT_COUNT_AS_RECENT)
		{
			return true;
		}
		
		return false;
	}
}


Entity *EntSystem_CreateLastFrameEntityFromIndex(int iIndex)
{
	assert(iIndex >= 0 && iIndex < MAX_ENTITIES_PRIVATE);
	assert(gpLastFrameEntityList[iIndex] == NULL);
	return gpLastFrameEntityList[iIndex] = EntSystem_AllocEntity("created in CreateLastFrameEntityFromIndex");
}

void EntSystem_DeleteLastFrameEntityFromIndex(int iIndex)
{
	assert(iIndex >= 0 && iIndex < MAX_ENTITIES_PRIVATE);
	
	if(gpLastFrameEntityList[iIndex])
	{
		StructDeInit(parse_Entity, gpLastFrameEntityList[iIndex]);
		EntSystem_FreeEntity(&gpLastFrameEntityList[iIndex]);
	}
}

Entity* entGetClientTarget(	Entity* e,
							const char* target,
							EntityRef* erTargetOut)
{
	Entity* eTarget = NULL;
	EntityRef erTarget = 0;

	if(target){
		if(!stricmp(target, "selected")){
			erTarget = e->pChar->currentTargetRef ? e->pChar->currentTargetRef : e->pChar->erTargetDual;
			eTarget = entFromEntityRef(entGetPartitionIdx(e), erTarget);
		}
		else if(!stricmp(target, "selected2")){
			erTarget = e->pChar->erTargetDual;
			eTarget = entFromEntityRef(entGetPartitionIdx(e), erTarget);
		}
		else if(!stricmp(target, "focus")){
			erTarget = e->pChar->erTargetFocus;
			eTarget = entFromEntityRef(entGetPartitionIdx(e), erTarget);
		}
		else if(!stricmp(target, "me")){
			if(e){
				erTarget = entGetRef(e);
				eTarget = e;
			}
		}
		else if(!stricmp(target, "off")){
			erTarget = 0;
			eTarget = NULL;
		}else{
			if(strStartsWith(target, "0x")){
				const char* cur = target + 2;
				erTarget = 0;
				while(cur[0]){
					char c = tolower(cur[0]);
					erTarget <<= 4;
					if(c >= '0' && c <= '9'){
						erTarget += c - '0';
					}
					else if(c >= 'a' && c <= 'f'){
						erTarget += 10 + c - 'a';
					}else{
						erTarget = 0;
						break;
					}
					cur++;
				}
			}else{
				erTarget = atoi(target);
			}
			if(	INDEX_FROM_REFERENCE(erTarget) == erTarget &&
				!entFromEntityRef(entGetPartitionIdx(e), erTarget))
			{
				eTarget = ENTITY_FROM_INDEX(erTarget);
				if(eTarget){
					erTarget = entGetRef(eTarget);
				}
			}else{
				eTarget = entFromEntityRef(entGetPartitionIdx(e), erTarget);
			}
		}
	}

	if(erTargetOut){
		*erTargetOut = erTarget;
	}

	return eTarget;
}

#ifdef GAMESERVER
extern Entity **ppOfflinePets;
#endif

AUTO_FIXUPFUNC;
TextParserResult EntityFixupFunc(Entity *pEntity, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
		case FIXUPTYPE_CONSTRUCTOR:
			eaPush(&g_eaEntityStructs, pEntity);
			break;
		case FIXUPTYPE_DESTRUCTOR:
			eaFindAndRemoveFast(&g_eaEntityStructs, pEntity);
			break;
	}

	return true;
}

void trackPetEntities(Entity *pEntity, Entity ***peaPetEntities)
{
	if (pEntity->pSaved) {
		int i;
		for(i=eaSize(&pEntity->pSaved->ppOwnedContainers)-1; i>=0; --i) {
			Entity *pPetEnt = GET_REF(pEntity->pSaved->ppOwnedContainers[i]->hPetRef);
			if (pPetEnt){
				eaPush(peaPetEntities, pPetEnt);
			}
		}
	}
}

int countPetEntities(Entity ***peaPetEntities, Entity *pPetToCount)
{
	int count = 0;
	int i;
	for(i=eaSize(peaPetEntities)-1; i>=0; --i) {
		if ((*peaPetEntities)[i] == pPetToCount) {
			++count;
			eaRemove(peaPetEntities, i);
		}
	}
	return count;
}

void createEntityReport(char **pestrBuffer)
{
	EntityIterator *pIter;
	Entity **eaEntities = NULL;
	Entity *pEntity;
	int *eaiIndexes = NULL;
	int i;
	int iNumPlayers = 0, iNumCritters = 0, iNumSavedPet = 0, iNumOther = 0;
	int iNumBackup = 0, iNumATBackup = 0, iNumLastFrame = 0, iNumDictLastFrame = 0;
	int iNumSubPlayer = 0, iNumSubPet = 0, iNumOffline = 0, iNumTotal = 0;
	Team **eaTeams = NULL;
	Entity **eaTeamEntities = NULL;
	Entity **eaPetEntities = NULL;
	Entity **eaTeamPetEntities = NULL;

	estrConcatf(pestrBuffer, "----ENTITY REPORT----\n");

	// Make working copy of entity structure list
	eaCopy(&eaEntities, &g_eaEntityStructs);
	iNumTotal = eaSize(&eaEntities);

	// Iterate live entities to collect info
	estrConcatf(pestrBuffer, "LIVE ENTITIES:\n");
	pIter = entGetIteratorAllTypesAllPartitions(0,0);
	while(pEntity = EntityIteratorGetNext(pIter)){
		Entity *pLastEntity;
		int iIndex;

		trackPetEntities(pEntity, &eaPetEntities);

		// Remove the entity from the working list
		if (eaFindAndRemoveFast(&eaEntities, pEntity) < 0) {
			estrConcatf(pestrBuffer, "    **Warning: Entity '%s' is live but is not being tracked.", pEntity->debugName);
		} else {
			estrConcatf(pestrBuffer, "    Live entity '%s'", pEntity->debugName);
		}
		if (pEntity->pSaved && pEntity->pSaved->pEntityBackup){ 
			trackPetEntities(pEntity->pSaved->pEntityBackup, &eaPetEntities);
			if (eaFindAndRemoveFast(&eaEntities, pEntity->pSaved->pEntityBackup) < 0) {
				estrConcatf(pestrBuffer, " <UNTRACKED Backup>");
			} else {
				estrConcatf(pestrBuffer, " <Backup>");
			}
			++iNumBackup;
		}
		if (pEntity->pSaved && pEntity->pSaved->pAutoTransBackup){ 
			trackPetEntities(pEntity->pSaved->pAutoTransBackup, &eaPetEntities);
			if (eaFindAndRemoveFast(&eaEntities, pEntity->pSaved->pAutoTransBackup) < 0) {
				estrConcatf(pestrBuffer, " <UNTRACKED AutoTransBackup>");
			} else {
				estrConcatf(pestrBuffer, " <AutoTransBackup>");
			}
			++iNumATBackup;
		}

		// Look at the entity from last frame
		iIndex = INDEX_FROM_ENTITY(pEntity);
		eaiPush(&eaiIndexes, iIndex);
		pLastEntity = LASTFRAME_ENTITY_FROM_INDEX(iIndex);
		if (pLastEntity) {
			trackPetEntities(pLastEntity, &eaPetEntities);
			if (eaFindAndRemoveFast(&eaEntities, pLastEntity) < 0) {
				estrConcatf(pestrBuffer, " <UNTRACKED LastFrame entry '%s'>", pLastEntity->debugName);
			} else {
				estrConcatf(pestrBuffer, " <LastFrame>");
			}
			++iNumLastFrame;
		}

		estrConcatf(pestrBuffer, "\n");

		// Count number of entities by type
		if (pEntity->pPlayer) {
			++iNumPlayers;
		} else if (pEntity->pSaved) {
			++iNumSavedPet;
		} else if (pEntity->pCritter) {
			++iNumCritters;
		} else {
			++iNumOther;
		}

		// Record the entity's team
		if (pEntity->pTeam && GET_REF(pEntity->pTeam->hTeam)) {
			eaPush(&eaTeams, GET_REF(pEntity->pTeam->hTeam));
		}
	}
	EntityIteratorRelease(pIter);
	estrConcatf(pestrBuffer, "\n");

	// See if any unreferenced last frame entries
	for(i=0; i<gHighestActiveEntityIndex; ++i) {
		if ((eaiFind(&eaiIndexes, i) < 0) && (gpLastFrameEntityList[i] != NULL)) {
			estrConcatf(pestrBuffer, "    **Warning: Unreferenced last frame entity '%s'\n", gpLastFrameEntityList[i]->debugName);
		}
	}

	// Iterate teams to find ents referenced by teams
	{
		RefDictIterator iter = {0};
		const char *pcDictName = GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM);
		ResourceDictionary *pDictionary = resGetDictionary(pcDictName);
		char *pcName = NULL;
		Team *pTeam = NULL;

		RefSystem_InitRefDictIterator(pcDictName, &iter);
		while( true ) {
			RefSystem_GetNextReferentAndRefDataFromIterator(&iter, &pTeam, &pcName);
			if (!pTeam) {
				if (!pcName) {
					break;
				}
				continue;
			}
			for(i=eaSize(&pTeam->eaMembers)-1; i>=0; --i) {
				Entity *pTeamEnt = GET_REF(pTeam->eaMembers[i]->hEnt);
				if (pTeamEnt) {
					trackPetEntities(pTeamEnt, &eaTeamPetEntities);
					eaPush(&eaTeamEntities, pTeamEnt);
				}
			}
			for(i=eaSize(&pTeam->eaInvites)-1; i>=0; --i) {
				Entity *pTeamEnt = GET_REF(pTeam->eaInvites[i]->hEnt);
				if (pTeamEnt) {
					trackPetEntities(pTeamEnt, &eaTeamPetEntities);
					eaPush(&eaTeamEntities, pTeamEnt);
				}
			}
			for(i=eaSize(&pTeam->eaRequests)-1; i>=0; --i) {
				Entity *pTeamEnt = GET_REF(pTeam->eaRequests[i]->hEnt);
				if (pTeamEnt) {
					trackPetEntities(pTeamEnt, &eaTeamPetEntities);
					eaPush(&eaTeamEntities, pTeamEnt);
				}
			}
		}
	}

	estrConcatf(pestrBuffer, "SUBSCRIBED ENTITIES:\n");

	// Iterate entities in the player container subscription dictionary
	{
		RefDictIterator iter = {0};
		char *pcName = NULL;
		const char *pcDictName = GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER);
		ResourceDictionary *pDictionary = resGetDictionary(pcDictName);
		ReferentPrevious *pPrev;

		RefSystem_InitRefDictIterator(pcDictName, &iter);
		while( true ) {
			RefSystem_GetNextReferentAndRefDataFromIterator(&iter, &pEntity, &pcName);
			if (!pEntity) {
				if (!pcName) {
					break;
				}
				continue;
			}

			// Remove the entity from the working list
			if (eaFindAndRemoveFast(&eaEntities, pEntity) < 0) {
				estrConcatf(pestrBuffer, "    **Warning: Entity '%s' is in player container sub dictionary but is not being tracked", pEntity->debugName);
			} else {
				estrConcatf(pestrBuffer, "    Subscribed player '%s'", pEntity->debugName);
			}
			++iNumSubPlayer;

			// Check for diffing version
			pPrev = resGetObjectPreviousFromDict(pDictionary, pcName);
			if (pPrev && pPrev->referentPrevious) {
				Entity *pEntity2 = pPrev->referentPrevious;
				if (pEntity2 && (pEntity2 != pEntity)) {
					trackPetEntities(pEntity2, &eaTeamPetEntities);
					if (eaFindAndRemoveFast(&eaEntities, pEntity2) < 0) {
						estrConcatf(pestrBuffer, " <UNTRACKED LastFrame version>");
					} else {
						estrConcatf(pestrBuffer, " <LastFrame>");
					}
					++iNumDictLastFrame;
				}
			}

			// Report references by teams
			estrConcatf(pestrBuffer, " [%d team refs]", countPetEntities(&eaTeamEntities, pEntity));

			estrConcatf(pestrBuffer, "\n");
		}
	}

	// Iterate entities in the saved pet container subscription dictionary
	{
		RefDictIterator iter = {0};
		char *pcName = NULL;
		const char *pcDictName = GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET);
		ResourceDictionary *pDictionary = resGetDictionary(pcDictName);
		ReferentPrevious *pPrev;

		RefSystem_InitRefDictIterator(pcDictName, &iter);
		while( true ) {
			RefSystem_GetNextReferentAndRefDataFromIterator(&iter, &pEntity, &pcName);
			if (!pEntity) {
				if (!pcName) {
					break;
				}
				continue;
			}

			// Remove the entity from the working list
			if (eaFindAndRemoveFast(&eaEntities, pEntity) < 0) {
				estrConcatf(pestrBuffer, "    **Warning: Entity '%s' is in saved pet container sub dictionary but is not being tracked", pEntity->debugName);
			} else {
				estrConcatf(pestrBuffer, "    Subscribed saved pet '%s'", pEntity->debugName);
			}
			++iNumSubPet;

			// Check for diffing version
			pPrev = resGetObjectPreviousFromDict(pDictionary, pcName);
			if (pPrev && pPrev->referentPrevious) {
				Entity *pEntity2 = pPrev->referentPrevious;
				if (pEntity2 && (pEntity2 != pEntity)) {
					if (eaFindAndRemoveFast(&eaEntities, pEntity2) < 0) {
						estrConcatf(pestrBuffer, " <UNTRACKED LastFrame version>");
					} else {
						estrConcatf(pestrBuffer, " <LastFrame>");
					}
					++iNumDictLastFrame;
				}
			}

			// Report references by players and teams
			estrConcatf(pestrBuffer, " [%d refs, %d team refs]", countPetEntities(&eaPetEntities, pEntity), countPetEntities(&eaTeamPetEntities, pEntity));

			estrConcatf(pestrBuffer, "\n");
		}
	}

	if (iNumSubPlayer+iNumSubPet > 0) {
		estrConcatf(pestrBuffer, "\n");
	}

#ifdef GAMESERVER
	// Iterate entities in the offline pet list
	if (eaSize(&ppOfflinePets) > 0) {
		estrConcatf(pestrBuffer, "OFFLINE ENTITIES:\n");
		for(i=0; i<eaSize(&ppOfflinePets); ++i){
			pEntity = ppOfflinePets[i];

			// Remove the entity from the working list
			if (eaFindAndRemoveFast(&eaEntities, pEntity) < 0) {
				estrConcatf(pestrBuffer, "    **Warning: Entity '%s' is in offline pet list but is not being tracked\n", pEntity->debugName);
			} else {
				estrConcatf(pestrBuffer, "    Offline pet '%s'\n", pEntity->debugName);
			}
			++iNumOffline;
		}
		estrConcatf(pestrBuffer, "\n");
	}
#endif

	// Report on leaked entities
	if (eaSize(&eaEntities) > 0) {
		estrConcatf(pestrBuffer, "LEAKED ENTITIES:\n");
		for(i=eaSize(&eaEntities)-1; i>=0; --i) {
			pEntity = eaEntities[i];

			estrConcatf(pestrBuffer, "    Leaked entity '%s'", pEntity->debugName);

			if (!pEntity->pCreationComment) {
				estrConcatf(pestrBuffer, " <MissingReason>");
			} else {
				estrConcatf(pestrBuffer, " <%s>", pEntity->pCreationComment);
			}
			estrConcatf(pestrBuffer, "\n");
		}
		estrConcatf(pestrBuffer, "\n");
	}

	// Report what we learned
	estrConcatf(pestrBuffer, "SUMMARY:\n");
	estrConcatf(pestrBuffer, "    %d TOTAL entity structures\n", iNumTotal);
	estrConcatf(pestrBuffer, "    %d live entities (%d Player, %d Critter, %d SavedPet, %d other)\n",
		iNumPlayers+iNumCritters+iNumSavedPet+iNumOther, iNumPlayers, iNumCritters, iNumSavedPet, iNumOther);
	estrConcatf(pestrBuffer, "    %d subscribed entities (%d player, %d savedpet)\n",
		iNumSubPlayer+iNumSubPet, iNumSubPlayer, iNumSubPet);
	estrConcatf(pestrBuffer, "    %d reference entities (%d Backup, %d AutoTransBackup, %d LastFrame, %d DictLastFrame)\n",
		iNumBackup+iNumATBackup+iNumLastFrame+iNumDictLastFrame, iNumBackup, iNumATBackup, iNumLastFrame, iNumDictLastFrame);
	estrConcatf(pestrBuffer, "    %d offline savedpet entities\n", iNumOffline);
	estrConcatf(pestrBuffer, "    %d leaked entities\n", eaSize(&eaEntities));

	estrConcatf(pestrBuffer, "---------------------\n");

	eaDestroy(&eaEntities);
	eaDestroy(&eaTeams);
	eaDestroy(&eaPetEntities);
	eaDestroy(&eaTeamEntities);
	eaDestroy(&eaTeamPetEntities);
	eaiDestroy(&eaiIndexes);
}

AUTO_COMMAND;
void printEntityReport(void)
{
	char *estrReport = NULL;
	
	createEntityReport(&estrReport);
	printf("%s", estrReport);
	estrDestroy(&estrReport);
}
