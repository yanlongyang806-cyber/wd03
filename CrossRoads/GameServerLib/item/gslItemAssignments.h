#ifndef GSL_ITEMASSIGNMENTS_H
#define GSL_ITEMASSIGNMENTS_H

#include "ItemAssignments.h"

typedef struct Entity Entity;
typedef struct ItemAssignmentSlotUI ItemAssignmentSlotUI;
typedef struct InventorySlot InventorySlot;

typedef struct ItemAssignmentLocationData
{
	const char* pchMapName; // Pooled
	const char* pchVolumeName; // Pooled
	ZoneMapType eMapType;
	WorldRegionType eRegionType;
	bool bInValidVolume;
} ItemAssignmentLocationData;

void gslItemAssignmentDef_Fixup(ItemAssignmentDef* pDef);

// Update uNextUpdateTime so that the player tick function knows when to update next 
// without requiring a full traversal of all assignments each tick
void gslItemAssignments_PlayerUpdateNextProcessTime(Entity* pEnt);

// Filter out assignments that should not be shown in the player's available assignment list
bool gslItemAssignments_IsValidAvailableAssignment(Entity* pEnt, ItemAssignmentDef* pDef, ItemAssignmentLocationData* pLocationData);

// Returns true if the assignment has a featured activity specified and it is active
bool gslItemAssignment_IsFeatured(ItemAssignmentDef* pDef);

// Update active and personal assignments on the player
void gslItemAssignments_UpdatePlayerAssignments(Entity* pEnt);

// Update the players granted personal assignment list
bool gslItemAssignments_UpdateGrantedAssignments(Entity* pEnt, ItemAssignmentDef* pDef, S32 eOperation);

// Generate an item assignment list
void gslItemAssignments_GenerateAssignmentList(Entity* pEnt,
											   ItemAssignmentRarityCountType* peRarityCounts,
											   ItemAssignmentLocationData* pLocationData,
											   U32* pSeed,
											   ItemAssignmentDefRef*** peaAssignmentsOut);

// Gets data about the player's current location
void gslItemAssignments_GetPlayerLocationDataEx(SA_PARAM_NN_VALID Entity* pEnt, bool bInteriorCheck, SA_PARAM_NN_VALID ItemAssignmentLocationData* pData);
#define gslItemAssignments_GetPlayerLocationData(pEnt, pData) gslItemAssignments_GetPlayerLocationDataEx(pEnt, false, pData)

void gslItemAssignment_AddRemoteAssignment(ItemAssignmentDef* pDef);
void gslItemAssignment_AddTrackedActivity(ItemAssignmentDef* pDef);
void gslItemAssignments_NotifyActivityStarted(const char* pchActivityName);

U32 gslItemAssignment_GetRefreshIndexOffset(void);

void gslItemAssignments_CollectRewards(Entity* pEnt, U32 uAssignmentID);

void gslRequestItemAssignments(Entity* pEnt);

void gslItemAssignments_StartNewAssignment(Entity* pEnt, const char* pchAssignmentDef, S32 iAssignmentSlot, ItemAssignmentSlots* pSlots);

void gslItemAssignments_FillOutcomeRewardRequest(Entity* pEnt, 
	ItemAssignment* pAssignment,
	ItemAssignmentCompletedDetails* pDetails,
	ItemAssignmentDef* pDef,
	ItemAssignmentOutcome* pOutcome,
	ItemAssignmentOutcomeRewardRequest* pRequest);

bool gslItemAssignments_CompleteAssignment(Entity* pEnt, ItemAssignment* pAssignment, const char* pchForceOutcome, bool bForce, bool bUseToken);

void gslItemAssignments_CancelActiveAssignment(Entity* pEnt, U32 uAssignmentID);

bool gslUpdatePersistedItemAssignmentList(Entity *pEnt, bool *bUpdatedPersonalAssignments);

void gslItemAssignments_CheckExpressionSlots(Entity *pEnt, ItemAssignmentCompletedDetails *pCompletedDetails);
#endif //GSL_ITEMASSIGNMENTS_H