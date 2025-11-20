/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "referencesystem.h"
#include "Message.h"

typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct WorldInteractionEntry WorldInteractionEntry;

AUTO_ENUM;
typedef enum LogoffCancelType
{
	kLogoffCancel_Requested,
	// Logoff canceled by request

	kLogoffCancel_Movement,
	// Logoff canceled because of movement

	kLogoffCancel_CombatDamage,
	// Logoff canceled by canceled damage, knocks 

	kLogoffCancel_CombatState,
	// Logoff canceled by being in the power's combat state

	kLogoffCancel_Interact,
	// Logoff canceled by an interaction
} LogoffCancelType;


AUTO_STRUCT;
typedef struct MapList
{
	STRING_EARRAY eaMapNames;	AST( POOL_STRING )
	STRING_EARRAY eaMapVars;	AST( POOL_STRING )
} MapList;

AUTO_STRUCT;
typedef struct MapDoorNodeRef
{
	REF_TO(WorldInteractionNode) hNode; AST(REFDICT(InteractionDictionary))
} MapDoorNodeRef;

AUTO_STRUCT;
typedef struct MapSummary
{
	const char*				pchMapName;				AST( POOL_STRING USERFLAG(TOK_USEROPTIONBIT_1) )
	const char*				pchMapVars;				AST( POOL_STRING USERFLAG(TOK_USEROPTIONBIT_1) )
	MapDoorNodeRef**		eaNodes;				AST( USERFLAG(TOK_USEROPTIONBIT_1) )
	
	S32						iPropIndex;				AST( USERFLAG(TOK_USEROPTIONBIT_1) ) // This is local as well
	S32						iNumInstances;
	S32						iNumNonFullInstances;
	S32						iNumPlayers;
	S32						iNumEnabledOpenInstancing;
} MapSummary;

AUTO_STRUCT;
typedef struct MapSummaryList
{
	EARRAY_OF(MapSummary) eaList;
} MapSummaryList;

AUTO_STRUCT;
typedef struct NodeSummary
{
	REF_TO(WorldInteractionNode)	hNode;		AST(REFDICT(InteractionDictionary) KEY)
	EARRAY_OF(MapSummary)			eaDestinations;	//NOTE: the elements of this list are treated as unowned on the server 
	bool							bDirty;		NO_AST			
} NodeSummary;

AUTO_STRUCT;
typedef struct NodeSummaryList
{
	EARRAY_OF(NodeSummary) eaNodes;
} NodeSummaryList;


void			mechanics_ResetCalculatedMapSummaryData( MapSummary* pData );

MapList*		mechanics_CreateMapListFromMapSummaryList( MapSummaryList* pSummaryList );
MapSummaryList* mechanics_CreateMapSummaryListFromMapList( MapList* pList );

bool			mechanics_MapSummaryHasNode( MapSummary* pData, WorldInteractionNode* pNode );