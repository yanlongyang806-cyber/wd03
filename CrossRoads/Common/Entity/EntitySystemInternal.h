/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef ENTITY_SYSTEM_INTERNAL_H_
#define ENTITY_SYSTEM_INTERNAL_H_
GCC_SYSTEM

#include "stdtypes.h"
#include "Entity.h"
#include "GlobalTypeEnum.h"


#define ENTITYTYPE_INVALID ((U32)0xffffffff) //used as end of list marker

//the number of unique ID bits per EntityRef
#define ENTITY_REF_ID_BITS ( sizeof(EntityRef) * 8 - MAX_ENTITIES_BITS)

//initialize the entity system. Must be called before anything else happens
void EntSystem_Init(bool bIsServer, bool bReset);

//go through all entity lists and check all connections from all sides. Make sure no entities are lost.
void EntSystem_CheckForCorruption(void);

//Gets an fresh new entity, removes it from free list, puts it in acive lists
//
//comment gets passed along to StructInit and ends up potentially set as a creation comment
Entity *EntSystem_LowLevelGetEmptyEntity(GlobalType eType, char *pComment);

//deletes an entity from all lists
#define EntSystem_LowLevelDeleteEntity(pEntity) EntSystem_LowLevelDeleteEntityEx(pEntity, __FILE__, __LINE__)
void EntSystem_LowLevelDeleteEntityEx(Entity *pEntity, const char* file, int line);

//create an entity in a specified slot with a specified reference. When entities are created on the server,
//they are duplicated on the client with this function
Entity *EntSystem_LowLevelGetEmptyEntity_SpecifyReference(EntityRef eRef, GlobalType eType, char *pComment);


//takes an existing entity that was StructCreated and puts it into the entity system. Only should be
//done on gameserver, and only by map transfer
void EntSystem_LowLevelRegisterExisting(Entity *pEntity);

typedef struct EntityListNode
{
struct EntityListNode *pNext;
struct EntityListNode *pPrev;
int iNextID; //the next ID that an entity in this slot will use as part of its entref
} EntityListNode;

Entity *gpMainEntityList[MAX_ENTITIES_PRIVATE];
Entity **gpLastFrameEntityList;

extern EntityListNode gEntityListNodes[MAX_ENTITIES_PRIVATE];

extern EntityListNode *gpEntityTypeLists[GLOBALTYPE_MAXTYPES];

//the free list node list is used to track free entities, as there is no point in also having a free entity list.
extern EntityListNode *gpFirstFreeEntListNode;

extern bool gbEntSystemInitted;
extern bool gbAmGameServer;

extern int gHighestActiveEntityIndex;

#define ASSERT_INITTED() assert(gbEntSystemInitted)


//ENT_SEND_FLAGS, which the entity sending code uses to track various things about the state of entity sending
//
//WARNING: THESE GET RESET EVERY FRAME. Do not add new ones unless you are certain you know what you are doing
enum {
	ENT_SEND_FLAG_LASTFRAME_COPY_EXISTS = (1 << 0),
	ENT_SEND_FLAG_NEVER_SENT = (1 << 1),
	ENT_SEND_FLAG_FULL_NEEDED = (1 << 2), // Set if we know we need a full resend, due to crazy debugging commands

	//what ENT_SEND_FLAGS an entity is created with
	STARTING_ENT_SEND_FLAGS = ( ENT_SEND_FLAG_NEVER_SENT ),
};


#define INDEX_FROM_REFERENCE(iRef) ( (iRef) & ( (1 << MAX_ENTITIES_BITS) - 1))
#define ID_FROM_REFERENCE(iRef) ( (iRef) >> MAX_ENTITIES_BITS )
#define REFERENCE_FROM_INDEX_AND_ID(index, id) ((index) | ((id) << MAX_ENTITIES_BITS))

#define ENTITY_FROM_INDEX(iIndex) (gpMainEntityList[iIndex])

/*
static __forceinline Entity *ENTITY_FROM_INDEX(int iIndex)
{
	return (Entity *)(gpEntityStorage + iIndex * gEntitySizeInArray);
}
*/

static __forceinline Entity *SAFE_ENTITY_FROM_INDEX(int iIndex)
{
	if(iIndex >= 0 && iIndex <= gHighestActiveEntityIndex)
	{
		Entity* e = gpMainEntityList[iIndex];
		return (!e || e->myEntityType == GLOBALTYPE_NONE) ? NULL : e;
	}
	return NULL;
}

static __forceinline Entity *LASTFRAME_ENTITY_FROM_INDEX(int iIndex)
{
	return gpLastFrameEntityList[iIndex];
}

static __forceinline int INDEX_FROM_ENTITY(const Entity *pEntity)
{
	return INDEX_FROM_REFERENCE(pEntity->myRef);
}

#define ENTITY_FROM_LISTNODE(pListNode) ((pListNode) ? ENTITY_FROM_INDEX((pListNode) - gEntityListNodes) : NULL)
/*
static __forceinline Entity *ENTITY_FROM_LISTNODE(EntityListNode *pListNode)
{
	if (pListNode)
	{
		return ENTITY_FROM_INDEX(pListNode - gEntityListNodes);
	}
	return NULL;
}
*/

static __forceinline EntityRef MakeEntityRef(EntityRef iIndex, EntityRef iID)
{
	return iIndex + (iID << MAX_ENTITIES_BITS);
}


Entity *EntSystem_CreateLastFrameEntityFromIndex(int iIndex);
void EntSystem_DeleteLastFrameEntityFromIndex(int iIndex);

typedef struct EntityIterator
{
	int iPartitionIdx;			 //only entities in this partition will be returned, PARTITION_ANY returns all
	EntityFlags iFlagsToRequire; //only entities which match any of these flags will be returned
	EntityFlags iFlagsToExclude; //entities which match any of these flags will NOT be returned

	U32 eCurType; //ENTITYTYPE_INVALID if inactive
	
	union
	{
		EntityListNode *pLastNodeReturned; 
		struct EntityIterator *pNext;
	} ptr;

	U32 eTypeList[GLOBALTYPE_MAXTYPES];
	U32 eTypeListIndex;

	//for debugging purposes
	char *pDbgFile;
	int iDbgLine;
} EntityIterator;

#define MAX_ENTITY_ITERATORS 8

extern EntityIterator gEntityIterators[MAX_ENTITY_ITERATORS];
extern EntityIterator *gpFirstFreeEntityIterator;


void EntityIterator_Backup(EntityIterator *pIterator);



#endif