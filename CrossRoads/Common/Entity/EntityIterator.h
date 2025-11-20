#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "partition_enums.h"

//An "entity iterator" is the ONLY way to traverse the basic lists of entities. It quickly loops over
//lists to find all entities of one or more types with given flags-to-match specifications. You must
//call GetEntityIterator (which comes in several varieties) to get an entity iterator, and
//EntityIterator_Release when you are done.
typedef struct EntityIterator EntityIterator;

//Gets an EntityIterator which will find entities of all types
EntityIterator *entGetIteratorAllTypesEx(char *pFile, int iLine, int iPartitionIdx, EntityFlags iFlagsToRequire, EntityFlags iFlagsToExclude);
#define entGetIteratorAllTypes(iPartitionIdx, iFlagsToRequire, iFlagsToExclude) entGetIteratorAllTypesEx(__FILE__, __LINE__, iPartitionIdx, iFlagsToRequire, iFlagsToExclude)
#define entGetIteratorAllTypesAllPartitions(iFlagsToRequire, iFlagsToExclude) entGetIteratorAllTypesEx(__FILE__, __LINE__, PARTITION_ANY, iFlagsToRequire, iFlagsToExclude)


//Gets an EntityIterator which will find entities of a single type
EntityIterator *entGetIteratorSingleTypeEx(char *pFile, int iLine, int iPartitionIdx, EntityFlags iFlagsToRequire, EntityFlags iFlagsToExclude,
	 GlobalType eType);
#define entGetIteratorSingleType(iPartitionIdx, iFlagsToRequire, iFlagsToExclude, eType) entGetIteratorSingleTypeEx(__FILE__, __LINE__, iPartitionIdx, iFlagsToRequire, iFlagsToExclude, eType)
#define entGetIteratorSingleTypeAllPartitions(iFlagsToRequire, iFlagsToExclude, eType) entGetIteratorSingleTypeEx(__FILE__, __LINE__, PARTITION_ANY, iFlagsToRequire, iFlagsToExclude, eType)

//Gets an EntityIterator which will find entities of several types. The list of types MUST BE TERMINATED
//BY ENTITYTYPE_INVALID
EntityIterator *entGetIteratorMultipleTypesEx(char *pFile, int iLine, int iPartitionIdx, EntityFlags iFlagsToRequire, EntityFlags iFlagsToExclude, ...);
#define entGetIteratorMultipleTypes(iPartitionIdx, iFlagsToRequire, iFlagsToExclude, ...) entGetIteratorMultipleTypesEx(__FILE__, __LINE__, iPartitionIdx, iFlagsToRequire, iFlagsToExclude, __VA_ARGS__)
#define entGetIteratorMultipleTypesAllPartitions(iFlagsToRequire, iFlagsToExclude, ...) entGetIteratorMultipleTypesEx(__FILE__, __LINE__, PARTITION_ANY, iFlagsToRequire, iFlagsToExclude, __VA_ARGS__)

//gets the next entity from an entity iterator. If NULL, the iterator has gotten all entities
Entity *EntityIteratorGetNext(EntityIterator *pIterator);

//release an entity iterator when you're done with it. 
void EntityIteratorRelease(EntityIterator *pEntityIterator);

int CountEntitiesOfType(GlobalType eType);


