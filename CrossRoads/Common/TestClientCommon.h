#pragma once

#include "GlobalTypes.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_STRUCT;
typedef struct TestClientSpawnRequest
{
	char			cScriptName[260];
	char			cMapName[1024];
	int				iInstanceIndex;
	Vec3			spawnPos;

	ContainerID		iOwnerID;
	int				iNumToSpawn;
	F32				fSpawnDelay;
	F32				fCurDelay; NO_AST

	char			*pcCmdLine; AST(ESTRING)
} TestClientSpawnRequest;

AUTO_STRUCT;
typedef struct TestClientEntity
{
	EntityRef	iRef; AST(KEY)
	EntityRef	iRefIfTeamed;
	char		*pchKey; // node name if object
	ContainerID	iID;
	char		*pchName; AST(ESTRING)
	char		*pchMapName; AST(ESTRING)

	F32			fHP;
	F32			fMaxHP;
	F32			fShields[4];

	int			iLevel;
	Vec3		vPos;
	Vec3		vPyr;
	F32			fDistance;

	bool		bDead;
	bool		bHostile;
	bool		bCasting;

	EntityRef	iTarget;
	EntityRef	*piNearbyFriends;
	EntityRef	*piNearbyHostiles;
	char		**ppNearbyObjects;

	int			iUpdateDepth; NO_AST
} TestClientEntity;

AUTO_STRUCT;
typedef struct TestClientObject
{
	char		*pchKey; AST(KEY)
	const char	*pchName; AST(POOL_STRING)
	EntityRef	iRef; // if there is one

	Vec3		vPos;
	F32			fDistance;

	bool		bDestructible;
	bool		bClickable;
	bool		bDoor;
} TestClientObject;

AUTO_STRUCT;
typedef struct TestClientPower
{
	const char	*pchName; AST(POOL_STRING)
	U32			iID;
	bool		bAttack;
	F32			fRange;
} TestClientPower;

AUTO_STRUCT;
typedef struct TestClientInteract
{
	char		*pchString;
	EntityRef	iRef;
} TestClientInteract;

AUTO_STRUCT;
typedef struct TestClientContact
{
	EntityRef	iRef;
	bool		bIsImportant;
	bool		bHasMission;
	bool		bHasMissionComplete;
} TestClientContact;

AUTO_STRUCT;
typedef struct TestClientContactDialogOption
{
	char		*pchKey;
	char		*pchName; AST(ESTRING)
	bool		bIsMission;
	bool		bIsMissionComplete;
} TestClientContactDialogOption;

AUTO_STRUCT;
typedef struct TestClientContactDialog
{
	EARRAY_OF(TestClientContactDialogOption)	ppOptions;
	bool										bHasReward;
	const char									**rewardChoices; AST(POOL_STRING)
	char										*pchText1; AST(ESTRING)
	char										*pchText2; AST(ESTRING)
} TestClientContactDialog;

AUTO_STRUCT;
typedef struct TestClientMission
{
	char							*pchIndexName; AST(ESTRING KEY)
	const char						*pchName; AST(POOL_STRING)
	bool							bIsChild;
	bool							bInProgress;
	bool							bCompleted;
	bool							bSucceeded;
	bool							bNeedsReturn;

	int								iLevel;

	char							*pchParent; AST(ESTRING)
	char 							**ppChildren;
} TestClientMission;

AUTO_STRUCT;
typedef struct TestClientStateUpdate
{
	char								*pchState; AST(ESTRING)

	// All interesting entities and personal powers
	EARRAY_OF(TestClientEntity)			ppEnts;
	EARRAY_OF(TestClientObject)			ppObjects;
	EARRAY_OF(TestClientPower)			ppPowers;

	// Personal entity info
	ContainerID							iID;
	EntityRef							iMyRef;
	TestClientEntity					*pMyEnt; NO_AST
	bool								bSTOSpaceshipMovement; // Should always be false in FC

	// Map info
	char								*pchMapName; AST(ESTRING)
	int									iInstanceIndex;

	// Team info
	bool								bIsTeamed;
	bool								bIsInvited;
	bool								bIsRequested;
	int									iNumMembers;
	int									iNumRequests;

	// Mission info
	EARRAY_OF(TestClientMission)		ppMyMissions;

	// Interaction options
	EARRAY_OF(TestClientInteract)		ppInteracts;
	EARRAY_OF(TestClientContact)		ppContacts;
	TestClientContactDialog				*pContactDialog;
} TestClientStateUpdate;