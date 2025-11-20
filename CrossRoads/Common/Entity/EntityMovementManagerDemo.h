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

// Structures for demo recorder
// Only moved to seperate file for budget tracking, include EntityMovementManager.h

AUTO_STRUCT;
typedef struct RecordedAnimBit
{
	const char* bitName; AST( POOL_STRING )
} RecordedAnimBit;
extern ParseTable parse_RecordedAnimBit[];
#define TYPE_parse_RecordedAnimBit RecordedAnimBit

AUTO_STRUCT;
typedef struct RecordedMMRState
{
	U32 type;
	U32 processCount;
	char* state;
} RecordedMMRState;
extern ParseTable parse_RecordedMMRState[];
#define TYPE_parse_RecordedMMRState RecordedMMRState

AUTO_STRUCT; 
typedef struct RecordedResource
{
	U32	handle;
	U32 id;
	char* constant;		AST(ESTRING) // Parsed struct in escaped text format
	char* constantNP;	AST(ESTRING) // Parsed struct in escaped text format
	RecordedMMRState** states;
} RecordedResource;
extern ParseTable parse_RecordedResource[];
#define TYPE_parse_RecordedResource RecordedResource

AUTO_STRUCT;
typedef struct RecordedEntityPos
{
	Vec3 pos;
	Quat rot;
	Vec2 pyFace;

	RecordedAnimBit** animBits;	AST(NO_INDEX)
} RecordedEntityPos;
extern ParseTable parse_RecordedEntityPos[];
#define TYPE_parse_RecordedEntityPos RecordedEntityPos

AUTO_STRUCT;
typedef struct RecordedEntityUpdate
{
	U32 entityRef;
	U32 dead;
	S32 isLocalPlayer;

	RecordedEntityPos** positions; AST(NO_INDEX)
	RecordedResource** resources; AST(NO_INDEX)
} RecordedEntityUpdate;
extern ParseTable parse_RecordedEntityUpdate[];
#define TYPE_parse_RecordedEntityUpdate RecordedEntityUpdate

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_DemoPlayback););
