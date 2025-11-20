/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GlobalTypeEnum.h"

typedef struct Entity Entity;
typedef struct EntityInteriorData EntityInteriorData;
typedef struct NOCONST(EntityInteriorData) NOCONST(EntityInteriorData);
typedef struct GameInteractable GameInteractable;
typedef struct InteriorInvite InteriorInvite;

//
// A per partition instance of this struct is maintained by the gameserver for interior maps.
//
AUTO_STRUCT;
typedef struct InteriorPartitionState
{
	// true when the owner is entering the map and we are waiting for the pet references to be satisfied
	bool ownerEntering;

	// copied from the owner when they enter the map and whenever they change an option while on the map
	union
	{
		NOCONST(EntityInteriorData) *noconstInteriorData;	NO_AST
		EntityInteriorData *interiorData;
	};

	// copied from the owner when they initially enter the map
	STRING_POOLED ownerReturnMapName;				AST(POOL_STRING)
	STRING_POOLED ownerLastItemAssignmentVolume;	AST(POOL_STRING)

    // keep a copy of the partition index here for sanity checking
	int iPartitionIdx;
} InteriorPartitionState;

void gslInterior_SetInterior(Entity *pEnt, ContainerID petContainer, const char *interiorDefName);
void gslInterior_SetInteriorAndOption(Entity *pEnt, ContainerID petContainer, const char *interiorDefName, const char *optionName, const char *choiceName);
void gslInterior_SetInteriorAndSetting(Entity *pEnt, ContainerID petContainer, const char *interiorDefName, const char *settingName);
void gslInterior_MoveToActiveInterior(Entity *playerEnt, const char* pchSet);
bool gslInterior_IsCurrentMapPlayerCurrentInterior(Entity *playerEnt);
bool gslInterior_InviteeInvite(ContainerID inviteeID, InteriorInvite *inviteIn, U32 inviterAccountID);
void gslInterior_InviteByName(Entity *playerEnt, const char *inviteeName);
void gslInterior_AcceptInvite(Entity *pEnt);
void gslInterior_DeclineInvite(Entity *pEnt);
void gslInterior_ExpelGuest(Entity *playerEnt, EntityRef guestRef);
void gslInterior_PlayerEntering(Entity *pEnt);
void gslInterior_Tick(void);
const char *gslInterior_GetMapOwnerReturnMap(int iPartitionIdx);
const char *gslInterior_GetOwnerLastItemAssignmentVolume(int iPartitionIdx);
S32 gslInterior_GetMapOptionChoiceValue(int iPartitionIdx, const char *optionName);
bool gslInterior_InteriorOptionChoiceActiveByValue(Entity *pEnt, const char *optionName, S32 value);
void gslInterior_InteriorOptionChoiceSetByValue(Entity *pEnt, const char *optionName, S32 value);
void gslInterior_MapLoad(void);
void gslInterior_MapUnload(void);
void gslInterior_PartitionLoad(int iPartitionIdx);
void gslInterior_PartitionUnload(int iPartitionIdx);
void gslInterior_MapValidate(void);
void gslInterior_ClearData(Entity *pEnt, ContainerID petContainer);
void gslInterior_UseFreePurchase(Entity *pEnt, const char *pchSetting);
void gslInterior_Randomize(Entity *pEnt);
