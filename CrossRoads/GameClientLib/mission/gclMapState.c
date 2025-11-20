/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "entCritter.h"
#include "Entity.h"
#include "Expression.h"
#include "gclMapState.h"
#include "mapstate_common.h"
#include "net/net.h"
#include "ReferenceSystem.h"
#include "Sound_common.h"
#include "Soundlib.h"
#include "StringCache.h"
#include "structNet.h"
#include "UIGen.h"
#include "wlInteraction.h"
#include "wlTime.h"
#include "WorldGrid.h"
#include "gclDemo.h"
#include "objPath.h"

#include "mapstate_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


//-----------------------------------------------------------------------
//   Declarations and types
//-----------------------------------------------------------------------

static struct {
	U32 packetIDWhenCleared;
	U32 packetIDWhenCreated;
	U32 packetIDWhenDiffed;
} mapStateReceive;

// This is the master copy of the MapState on the client
MapState *gClientMapState = NULL;

// This is the previous data so we can compare for changes after receiving an update
// to the MapState from the server
static NodeMapStateData *s_pPreviousData = NULL;

// This is the previous data on the map paused state
static bool s_bPreviousMapPaused = false;

// These are nodes that per-ent visibility has set to be visible on the client
static const char **s_eaPerEntVisibleNodes = NULL;

// These are nodes that are hidden but waiting for an entity to spawn
static const char **s_eaWaitingForEntVisibleNodes = NULL;

// This is a feature used by the UI to ask the system to reset the visible child status of
// the map after a short time.
static int s_iFramesUntilResetVisibleChild = 0;

// These are nodes created during the current frame that need to be processed
static const char **s_eaRecentlyCreatedNodes = NULL;

#define FRAMES_TO_WAIT_FOR_ENT  30

//-----------------------------------------------------------------------
//   Network receive logic
//-----------------------------------------------------------------------

static bool mapState_UpdateNodeEntry(NodeMapStateEntry *pEntry)
{
	WorldInteractionEntry *pWorldEntry = RefSystem_ReferentFromString(ENTRY_DICTIONARY, pEntry->pcNodeName);

	if (pWorldEntry) {
		// Figure out if hidden or visible
		const char *pcNodeName = allocAddString(pEntry->pcNodeName);
		bool bHidden = pEntry->bHidden;
		if (eaFind(&s_eaPerEntVisibleNodes, pcNodeName) >= 0) {
			bHidden = false;  // If per ent visibility, this supersedes the bHidden and disabled states
		} else if (eaFind(&s_eaWaitingForEntVisibleNodes, pcNodeName) >= 0) {
			bHidden = false;  // If waiting for ent, then keep it visible until the ent appears
		} else if (pEntry->bDisabled) {
			bHidden = true;   // If the server says it is disabled
		}

		// Apply to world entry
		worldInteractionEntrySetDisabled(PARTITION_CLIENT, pWorldEntry, bHidden);
		return true;
	}

	// Node not found so wait until it shows up
	return false;
}

static void mapState_UpdateHiddenNodes(bool bWasFullUpdate)
{
	MapState *pState = mapStateClient_Get();
	NodeMapStateData *pData = SAFE_MEMBER(pState,pNodeData);
	int i;

	assertmsgf(pState, "Requires a valid MapState");

	if (!s_pPreviousData) {
		s_pPreviousData = StructCreate(parse_NodeMapStateData);
	}

	if (!mapStateClient_Get()->pNodeData) {
		return;
	}

	// TODO: Should find a more performant way to do this.

	for(i=eaSize(&s_pPreviousData->eaNodeEntries)-1; i>=0; --i) {
		NodeMapStateEntry *pEntry = s_pPreviousData->eaNodeEntries[i];

		int iIndex = eaIndexedFindUsingString(&pData->eaNodeEntries, pEntry->pcNodeName);
		if (iIndex == -1) {
			// If not found on update, it's newly removed from the state.  
			const char *pcNodeName = allocAddString(pEntry->pcNodeName);
			
			// Clear it
			pEntry->bDisabled = false;
			pEntry->bHidden = false;
			mapState_UpdateNodeEntry(pEntry);

			// Remove from the "waiting for ent" list
			eaFindAndRemove(&s_eaWaitingForEntVisibleNodes, pcNodeName);

			// Destroy the data
			StructDestroy(parse_NodeMapStateEntry, pEntry);
			eaRemove(&s_pPreviousData->eaNodeEntries, i);

		} else {
			// Found to already exist so compare state
			NodeMapStateEntry *pNewEntry = pData->eaNodeEntries[iIndex];
			if (bWasFullUpdate || StructCompare(parse_NodeMapStateEntry, pEntry, pNewEntry, 0, 0 ,0) != 0) {
				// It changed (or this is full update) so update it

				// Update "waiting for ent" state
				if (bWasFullUpdate || (pEntry->uEntToWaitFor != pNewEntry->uEntToWaitFor)) {
					const char *pcNodeName = allocAddString(pEntry->pcNodeName);
					if (pNewEntry->uEntToWaitFor) {
						pNewEntry->iWaitingForEnt = FRAMES_TO_WAIT_FOR_ENT;
						eaPushUnique(&s_eaWaitingForEntVisibleNodes, pcNodeName);
					} else {
						eaFindAndRemove(&s_eaWaitingForEntVisibleNodes, pcNodeName);
					}
				}

				// Update hidden state
				if (bWasFullUpdate || (pNewEntry->bHidden != pEntry->bHidden) || (pNewEntry->bDisabled != pEntry->bDisabled)) {
					// Same node but state changed
					mapState_UpdateNodeEntry(pNewEntry);
				}

				// Update previous state
				StructCopy(parse_NodeMapStateEntry, pNewEntry, pEntry, 0, 0, 0);
			}
		}
	}
	for(i=eaSize(&pData->eaNodeEntries)-1; i>=0; --i) {
		NodeMapStateEntry *pEntry = pData->eaNodeEntries[i];
		if (eaIndexedFindUsingString(&s_pPreviousData->eaNodeEntries, pEntry->pcNodeName) == -1) {
			// If not found locally, it's newly added to the state.  

			// Deal with "waiting for ent" situation
			if (pEntry->uEntToWaitFor) {
				const char *pcNodeName = allocAddString(pEntry->pcNodeName);
				pEntry->iWaitingForEnt = FRAMES_TO_WAIT_FOR_ENT;
				eaPushUnique(&s_eaWaitingForEntVisibleNodes, pcNodeName);
			}
			
			// Apply it.
			mapState_UpdateNodeEntry(pEntry);

			// Add to the previous data
			eaIndexedAdd(&s_pPreviousData->eaNodeEntries, StructClone(parse_NodeMapStateEntry, pEntry));
		}
	}
}


// Called whenever a new MapState packet is received
static void mapState_MapStateUpdated(bool bWasFullUpdate)
{
	MapState *pState = mapStateClient_Get();

	if (!pState) {
		return;
	}

	// Update the delta for timeServerSecondsSince2000
	timeSetServerDelta(pState->uServerTimeSecondsSince2000);

	// Update hidden state of nodes
	mapState_UpdateHiddenNodes(bWasFullUpdate);

	// Check the map paused state
	if (s_bPreviousMapPaused && !pState->bPaused) {
		// Pause ended
		wlTimeSetStepScaleLocal(1.0);
		s_bPreviousMapPaused = false;
	} else if (!s_bPreviousMapPaused && pState->bPaused) {
		// Pause started
		wlTimeSetStepScaleLocal(0.0);
		s_bPreviousMapPaused = true;
	}
}


void mapState_ClientReceiveMapStateFromPacket(Packet* pak)
{
	// See if there's actually any map information.  The bit will be 1 if so
	if (pktGetBits(pak, 1)) {
		// Is this a diff or a full send?
		if (pktGetBits(pak, 1)) {
			StructDestroySafe(parse_MapState, &gClientMapState);
			gClientMapState = pktGetStruct(pak, parse_MapState);
			mapStateReceive.packetIDWhenCreated = pktGetID(pak);
			
			demo_RecordMapStateFull(gClientMapState, mapStateReceive.packetIDWhenCreated);
			mapState_MapStateUpdated(true);
		} else {
			devassert(gClientMapState);
			demo_RecordMapStateDiffBefore(gClientMapState);
			if (gClientMapState) {
				ParserRecv(parse_MapState, pak, gClientMapState, RECVDIFF_FLAG_COMPAREBEFORESENDING);
			}
			mapStateReceive.packetIDWhenDiffed = pktGetID(pak);
			
			demo_RecordMapStateDiff(gClientMapState, mapStateReceive.packetIDWhenDiffed);				
			mapState_MapStateUpdated(false);
		}
	} else {
		StructDestroySafe(parse_MapState, &gClientMapState);
		mapStateReceive.packetIDWhenCleared = pktGetID(pak);
		
		demo_RecordMapStateDestroy(mapStateReceive.packetIDWhenCleared);
	}
}

void mapState_InitialRecordForDemo(void)
{
	demo_RecordMapStateFull(gClientMapState, mapStateReceive.packetIDWhenCreated);
}

void mapState_ClientReceiveMapStateFullFromDemo(MapState* mapState, U32 id)
{
	StructDestroySafe(parse_MapState, &gClientMapState);
	gClientMapState = StructClone(parse_MapState, mapState);
	mapStateReceive.packetIDWhenCreated = id;
	
	mapState_MapStateUpdated(true);
}

void mapState_ClientReceiveMapStateDiffFromDemo(char* diffString, U32 id)
{
	devassert(gClientMapState);
	objPathParseAndApplyOperations(parse_MapState, gClientMapState, diffString);
	mapStateReceive.packetIDWhenDiffed = id;

	mapState_MapStateUpdated(false);
}

void mapState_ClientReceiveMapStateDestroyFromDemo(U32 id)
{
	StructDestroySafe(parse_MapState, &gClientMapState);
	mapStateReceive.packetIDWhenCleared = id;
}


//-----------------------------------------------------------------------
//   Client Node State Access
//-----------------------------------------------------------------------

bool mapState_IsNodeHiddenOrDisabled(WorldInteractionNode *pNode)
{
	NodeMapStateData *pData = SAFE_MEMBER(gClientMapState,pNodeData);
	const char *pcName;
	int iIndex;

	if (!pData) {
		return false;
	}

	pcName = wlInteractionNodeGetKey(pNode);
	iIndex = eaIndexedFindUsingString(&pData->eaNodeEntries, pcName);
	if (iIndex >=0) {
		NodeMapStateEntry *pEntry = pData->eaNodeEntries[iIndex];
		return (pEntry->bHidden || pEntry->bDisabled);
	}

	return false;
}


void mapState_ClientResetVisibleChildAllNextFrame(void)
{
	s_iFramesUntilResetVisibleChild = 10;
}


void mapState_SetNodeVisibleOverride(WorldInteractionNode *pNode, bool bVisible)
{
	NodeMapStateData *pData = SAFE_MEMBER(gClientMapState,pNodeData);
	const char *pcNodeName = allocAddString(wlInteractionNodeGetKey(pNode));

	if (bVisible) {
		// Add to list if not already there
		if (eaFind(&s_eaPerEntVisibleNodes, pcNodeName) < 0) {
			eaPush(&s_eaPerEntVisibleNodes, pcNodeName);
		}
	} else {
		// Remove from list if there
		eaFindAndRemove(&s_eaPerEntVisibleNodes, pcNodeName);
	}
	
	if (pData) {
		int iIndex = eaIndexedFindUsingString(&pData->eaNodeEntries, pcNodeName);
		mapState_UpdateNodeEntry(pData->eaNodeEntries[iIndex]);
	}
}


//-----------------------------------------------------------------------
//   Client definitions for some common functions
//-----------------------------------------------------------------------

MapState* mapState_FromEnt(Entity *pEnt)
{
	return gClientMapState;
}

MapState* mapState_FromPartitionIdx(int iPartitionIdx)
{
	return gClientMapState;
}


MapState *mapStateClient_Get()
{
	return gClientMapState;
}


//-----------------------------------------------------------------------
//   Dictionary Change Listener
//-----------------------------------------------------------------------

static void mapState_DictChanged(enumResourceEventType eType, const char *pDictName, const char *pcNodeName, Referent pReferent, void *pUserData)
{
	if (gClientMapState && gClientMapState->pNodeData) {
		// When a resource is added, see if it is being tracked and if so, add the entry to the list to process next tick
		int iIndex = eaIndexedFindUsingString(&gClientMapState->pNodeData->eaNodeEntries, pcNodeName);
		if (iIndex >= 0) {
			NodeMapStateEntry *pEntry = gClientMapState->pNodeData->eaNodeEntries[iIndex];
			if (pEntry) {
				eaPushUnique(&s_eaRecentlyCreatedNodes, allocAddString(pcNodeName));
			}
		}
	}
}

void mapState_RegisterDictListeners(void)
{
	static bool bRegisteredListener = false;
	if (!bRegisteredListener) {
		resDictRegisterEventCallback(INTERACTION_DICTIONARY, mapState_DictChanged, NULL);
		resDictRegisterEventCallback(ENTRY_DICTIONARY, mapState_DictChanged, NULL);
		bRegisteredListener = true;
	}
}


//-----------------------------------------------------------------------
//   Client tick function for maintaining node state properly
//-----------------------------------------------------------------------

void mapState_OncePerFrame(void)
{
	int i;

	if (gClientMapState && gClientMapState->pNodeData) {
		// Handle "waiting for ent" situations
		for(i=eaSize(&s_eaWaitingForEntVisibleNodes)-1; i>=0; --i) {
			const char *pcNodeName = s_eaWaitingForEntVisibleNodes[i];
			int iIndex = eaIndexedFindUsingString(&gClientMapState->pNodeData->eaNodeEntries, pcNodeName);
			if (iIndex >=0) {
				NodeMapStateEntry *pEntry = gClientMapState->pNodeData->eaNodeEntries[iIndex];
				Entity *pEnt = NULL;

				if (entHasRefExistedRecently(pEntry->uEntToWaitFor)) {
					pEnt = entFromEntityRefAnyPartition(pEntry->uEntToWaitFor);
				}

				if (!pEnt) {
					// Only wait so long for the entity to actually exist
					--pEntry->iWaitingForEnt;
					if (pEntry->iWaitingForEnt > 0) {
						continue;
					}
				}
				if (pEnt && !GET_REF(pEnt->hWLCostume)) {
					continue; // Skip if costume not ready even if the entity is present
				}

				// Entity exists now so clear tracking and update the node
				pEntry->iWaitingForEnt = 0;
				eaRemove(&s_eaWaitingForEntVisibleNodes, i);
				mapState_UpdateNodeEntry(pEntry);
			}
		}

		// Re-process nodes that were detected as recently created by the dictionary listener
		for(i=eaSize(&s_eaRecentlyCreatedNodes)-1; i>=0; --i) {
			int iIndex = eaIndexedFindUsingString(&gClientMapState->pNodeData->eaNodeEntries, s_eaRecentlyCreatedNodes[i]);
			if (iIndex >= 0) {
				NodeMapStateEntry *pEntry = gClientMapState->pNodeData->eaNodeEntries[iIndex];
				if (pEntry) {
					mapState_UpdateNodeEntry(pEntry);
				}
			}
		}
		eaClearFast(&s_eaRecentlyCreatedNodes);
	}

	// Special code used by UI to reset the visible children
	if (s_iFramesUntilResetVisibleChild > 0) {
		--s_iFramesUntilResetVisibleChild;
		if (s_iFramesUntilResetVisibleChild <= 0) {
			wlInteractionClientSetVisibleChildAll(0);
		}
	}

	mapState_RegisterDictListeners();
}


//-----------------------------------------------------------------------
//   Map lifecycle
//-----------------------------------------------------------------------

void mapState_MapLoad(void)
{
	// Force re-processing of map state as if received full update
	mapState_MapStateUpdated(true);
}


void mapState_MapUnload(void)
{
	// Clear map state on map unload
	StructDestroySafe(parse_NodeMapStateData, &s_pPreviousData);
	eaDestroy(&s_eaWaitingForEntVisibleNodes);
	sndMapUnload();
	// not clearing "s_eaPerEntVisibleNodes" since that is managed by other code
}


//-----------------------------------------------------------------------
//   Client logic for supporting world layer node queries
//-----------------------------------------------------------------------

int mapState_IsNodeHiddenCB(int iPartitionIdx, WorldInteractionNode *pNode)
{
	NodeMapStateData *pData = SAFE_MEMBER(gClientMapState,pNodeData);
	const char *pcName;
	int iIndex;

	if (!pData) {
		return false;
	}

	pcName = wlInteractionNodeGetKey(pNode);
	iIndex = eaIndexedFindUsingString(&pData->eaNodeEntries, pcName);
	if (iIndex >=0) {
		NodeMapStateEntry *pEntry = pData->eaNodeEntries[iIndex];
		return (pEntry->bHidden);
	}

	return false;
}


int mapState_IsNodeDisabledCB(int iPartitionIdx, WorldInteractionNode *pNode)
{
	NodeMapStateData *pData = SAFE_MEMBER(gClientMapState,pNodeData);
	const char *pcName;
	int iIndex;

	if (!pData) {
		return false;
	}

	pcName = wlInteractionNodeGetKey(pNode);
	iIndex = eaIndexedFindUsingString(&pData->eaNodeEntries, pcName);
	if (iIndex >=0) {
		NodeMapStateEntry *pEntry = pData->eaNodeEntries[iIndex];
		return (pEntry->bDisabled);
	}

	return false;
}


AUTO_RUN;
void mapState_InitializeClient(void)
{
	wlInteractionRegisterCallbacks(mapState_IsNodeHiddenCB, mapState_IsNodeDisabledCB);
}


// ********************************************************************
// **
// ** test commands 
// **
// *********************************************************************

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME("MapStateFix");
void gclMapState_DebugUpdateMapState(void)
{
	mapState_MapStateUpdated(true);
}
