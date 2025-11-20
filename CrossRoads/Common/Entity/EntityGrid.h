#pragma once
GCC_SYSTEM

/***************************************************************************
*     Copyright (c) 2005-Present, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#if !GAMESERVER && !GAMECLIENT
	#error No Movement code allowed here.
#endif

#include "stdtypes.h"

typedef struct Entity Entity;
typedef struct EntityGridNode EntityGridNode;
typedef U32 EntityFlags;

typedef struct EntAndDist
{
	Entity* e;
	F32 maxDistSQR;
	F32 maxCollRadius;
}EntAndDist;

typedef struct EntGridPosCopy {
	IVec3	posGrid;
	S32		bitsRadius;
} EntGridPosCopy;

void entGridUpdate(Entity* e, int create);
void entGridFree(Entity *e);

int entGridProximityLookup(int iPartitionIdx, const Vec3 pos, Entity** result, int playersOnly);
int entGridProximityLookupEArray(int iPartitionIdx, const Vec3 pos, Entity*** result, int playersOnly);
int entGridProximityLookupEx(int iPartitionIdx, const Vec3 pos, Entity** result, F32 distance, EntityFlags entFlagsToMatch, EntityFlags entFlagsToExclude, Entity* sourceEnt);
int entGridProximityLookupExEArray(int iPartitionIdx, const Vec3 pos, Entity*** result, F32 distance, EntityFlags entFlagsToMatch, EntityFlags entFlagsToExclude, Entity* sourceEnt);
int entGridProximityLookupDynArraySaveDist(int iPartitionIdx, const Vec3 pos, EntAndDist** result, int* count, int* maxCount, F32 distance, EntityFlags entFlagsToMatch, EntityFlags entFlagsToExclude, Entity* sourceEnt);

S32 entGridIsGridPosDifferent(	const EntGridPosCopy* c,
								const Vec3 posF32);

void entGridCopyGridPos(const EntityGridNode* node,
						EntGridPosCopy* gridPosCopy);
