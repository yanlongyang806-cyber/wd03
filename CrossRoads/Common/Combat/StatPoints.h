/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef StatPoints_h__
#define StatPoints_h__
GCC_SYSTEM

#include "Message.h"

typedef struct Entity				Entity;
typedef struct NOCONST(Entity)		NOCONST(Entity);
typedef struct Character			Character;

typedef enum AttribType AttribType;

extern StaticDefineInt AttribTypeEnum[];

extern DictionaryHandle g_hStatPointPoolDict;

AUTO_STRUCT;
typedef struct StatPointDef
{
	const char *pchAttribName;			AST(POOL_STRING)
		// The name of the attribute
		
	DisplayMessage displayName;		AST(STRUCT(parse_DisplayMessage) NAME(DisplayName))
		// Translated display name which players see
		
	DisplayMessage description;		AST(STRUCT(parse_DisplayMessage) NAME(Description))	
		// Translated description which players see
} StatPointDef;

AUTO_STRUCT;
typedef struct StatPointPoolDef
{
	const char *pchName;				AST( STRUCTPARAM KEY POOL_STRING )
		// The name of this stat point pool
		
	const char *pchFile;				AST(CURRENTFILE)	
		// Current file (required for reloading)

	const char *pchPowerTableName;		AST(NAME(PowerTableName), POOL_STRING)
		// The name of the power table which determines the number of points available at a certain level

	StatPointDef **ppValidAttribs;		AST(NAME(ValidAttrib))
		// The list of the valid attributes which points can be spent on this specific stat point pool
} StatPointPoolDef;

AUTO_STRUCT AST_CONTAINER;
typedef struct AssignedStats
{
	const AttribType	eType;		AST(PERSIST SUBSCRIBE NAME(Type) SUBTABLE(AttribTypeEnum))
		//The Type of attrib that is being modified
	
	const int			iPoints;	AST(PERSIST SUBSCRIBE NAME(Points))
		//The amount of points added to that specific attrib
	
	const int			iPointPenalty;	AST(PERSIST SUBSCRIBE NAME(PointPenalty))
		// The penalty cost. The actual number of stat points spent is iPoints + iPointPenalty
}AssignedStats;

AUTO_STRUCT AST_CONTAINER;
typedef struct SavedAttribStats
{
	CONST_STRING_POOLED				pchPresetName;		AST(PERSIST SUBSCRIBE POOL_STRING)

	CONST_EARRAY_OF(AssignedStats)	ppAssignedStats;	AST(PERSIST SUBSCRIBE)
} SavedAttribStats;

AUTO_STRUCT;
typedef struct StatPointCartItem
{
	AttribType	eType;
	S32			iPoints;
} StatPointCartItem;

AUTO_STRUCT;
typedef struct StatPointCart
{
	StatPointCartItem **eaItems;
} StatPointCart;

#define STAT_POINT_POOL_DEFAULT "Default"

int character_StatPointsSpentPerAttrib(Character *pChar, AttribType eType);
//#define character_StatPointsSpentPerAttrib(pChar, eType) character_StatPointsSpentPerAttribEx(pChar, eType, STAT_POINT_POOL_DEFAULT)

bool character_IsValidStatPoint(AttribType eAttrib, SA_PARAM_NN_STR const char *pchStatPointPoolName);

// Returns the number of AssignedStat points actually assigned
int entity_GetAssignedStatAssigned(NOCONST(Entity) *pEnt, SA_PARAM_NN_STR const char *pchStatPointPoolName);

// Returns the number of AssignedStat points the entity is allowed to spend.
//  Does not exclude points already assigned.
//  Lookup is hard-coded to ASSIGNEDSTAT_TABLE table.
int entity_GetAssignedStatAllowed(NOCONST(Entity) *pEnt, SA_PARAM_NN_STR const char *pchStatPointPoolName);

// Returns the number of unspent AssignedStat points (just a wrapper for (allowed - assigned))
int entity_GetAssignedStatUnspent(NOCONST(Entity) *pEnt, SA_PARAM_NN_STR const char *pchStatPointPoolName);

// Determines if the given attrib is valid in the given pool
bool StatPointPool_ContainsAttrib(SA_PARAM_NN_VALID StatPointPoolDef *pDef, AttribType eAttribType);

// Returns the dictionary item for the given stat point pool name
StatPointPoolDef * StatPointPool_DefFromName(SA_PARAM_NN_STR const char *pchStatPointPoolName);

// Returns the stat point pool def the attribute is contained within
StatPointPoolDef * StatPointPool_DefFromAttrib(AttribType eAttribType);

#endif