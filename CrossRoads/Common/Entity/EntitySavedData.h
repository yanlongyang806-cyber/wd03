#pragma once
GCC_SYSTEM

#include "entEnums.h"
#include "GlobalTypes.h"
#include "ReferenceSystem.h"

typedef struct AlwaysPropSlot			AlwaysPropSlot;
typedef struct AttribMod				AttribMod;
typedef struct Entity 					Entity;
typedef struct EntityBuild				EntityBuild;
typedef struct PetDef					PetDef;
typedef struct PetDefRefCont			PetDefRefCont;
typedef struct PlayerCostume			PlayerCostume;
typedef struct PlayerCostumeV0			PlayerCostumeV0;
typedef struct SavedAttribute			SavedAttribute;
typedef struct SavedMapDescription		SavedMapDescription;
typedef struct obsolete_SavedMapDescription		obsolete_SavedMapDescription;
typedef struct TrayElem					TrayElem;
typedef struct TrayElemOld				TrayElemOld;
typedef struct PlayerDiary				PlayerDiary;
typedef struct EntityInteriorData		EntityInteriorData;
typedef struct ActivityLogEntry			ActivityLogEntry;
typedef struct AccountProxyKeyValueInfoListContainer AccountProxyKeyValueInfoListContainer;
typedef struct LeaderboardStats			LeaderboardStats;
typedef struct InventoryBag				InventoryBag;
typedef struct CharClassCategorySet		CharClassCategorySet;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(xp) AST_IGNORE(bSummoned) AST_IGNORE(bDead);
typedef struct ActiveSuperCritterPet
{
	CONST_OPTIONAL_STRUCT(InventoryBag) pEquipment;	AST(FORCE_CONTAINER PERSIST SUBSCRIBE)
	U32	uiTimeFinishTraining;		AST(PERSIST NO_TRANSACT)
	U32 uiLastLevelUpTransactionRequest; AST(SERVER_ONLY)
} ActiveSuperCritterPet;

AUTO_STRUCT AST_CONTAINER;
typedef struct EntitySavedSCPData
{
	CONST_EARRAY_OF(ActiveSuperCritterPet)	ppSuperCritterPets;		AST(PERSIST SUBSCRIBE FORCE_CONTAINER)
	F32							fCachedPetBonusXPPct;				AST(PERSIST SOMETIMES_TRANSACT SELF_ONLY)
	const S32							iSummonedSCP;				AST(PERSIST SUBSCRIBE DEFAULT(-1))
	EntityRef erSCP;												AST(PERSIST SOMETIMES_TRANSACT)		//if 0 transactions don't give pet xp.
	
	//cache server-only value to limit "Has training finish yet?" polling
	bool bTrainingActive;											NO_AST
} EntitySavedSCPData;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(eaAlwaysPropEnts);
typedef struct PetRelationship
{
	const PetRelationshipType eRelationship;			AST(PERSIST SUBSCRIBE)
		// Type of relationship to the target container

	const OwnedContainerState eState;					AST(PERSIST SUBSCRIBE SUBTABLE(OwnedContainerStateEnum))
		// The state of a specific owned container
	
	const PetRelationshipStatus eStatus_Deprecated;		AST(PERSIST SUBSCRIBE FLAGS ADDNAMES(eStatus) SUBTABLE(PetRelationshipStatusEnum))
		// The status of this pet - i.e. TeamRequest or AlwaysProp

	const bool bTeamRequest;							AST(PERSIST SUBSCRIBE)
	// Whether or not this pet is on an away team

	const ContainerID conID;							AST(ADDNAMES(hPet) PERSIST SUBSCRIBE FORMATSTRING(DEPENDENT_CONTAINER_TYPE = "EntitySavedPet"))
		// ContainerID of the pet, implicitly type GLOBALTYPE_ENTITYSAVEDPET
		//  Previously stored in the DB as a CONST_REF_TO(Entity) called hPet

	REF_TO(Entity) hPetRef;								AST(NAME(hPetRef) COPYDICT(EntitySavedPet))
		// Subscription to pet Entity, not available in transactions or from another subscribed Entity, 
		//  and may be invalid or arbitrarily delayed

	const U32 uiPetID;									AST(PERSIST SUBSCRIBE)
		// Unique ID of the Pet.  Matches the value on the pet itself at Entity->pSaved->iPetID.  NOT the container ID.

	Entity *curEntity;									NO_AST
		// The current active entity, used for power propagation. Used only on the server
		// No_AST so freeing the entity doesn't request this entity to be freed

	U32 uiPowerTreeModCount;							NO_AST
		// The last known number used for the powerTreeModCount on this pet. If changed, rebuild powers
		// and attribs for the main character

	bool bDeferLoginHandling;							NO_AST
	CONST_INT_EARRAY eaPurposes;						AST(PERSIST)
} PetRelationship;

AUTO_ENUM;
typedef enum PuppetContainerState
{
	PUPPETSTATE_OFFLINE,	// Container is offline, and should stay offline unless summoned
	PUPPETSTATE_ACTIVE,		// Container is active
	PUPPETSTATE_INCONTROL, //This is the current ent that the player is in control of
	PUPPETSTATE_STATIC, // Container exists in the world, but is not controlled by the player
} PuppetContainerState;

AUTO_STRUCT AST_CONTAINER;
typedef struct SavedTray
{
	int* piUITrayIndex;						AST(PERSIST NO_TRANSACT SELF_ONLY)
	U32* puiNoSlotPowerIDs;					AST(PERSIST NO_TRANSACT SELF_ONLY)
		// A list of power IDs that should not be auto-slotted in the tray during 'ResetPowersArray'
	EARRAY_OF(TrayElem) ppTrayElems;		AST(PERSIST NO_TRANSACT SELF_ONLY FORCE_CONTAINER)
	EARRAY_OF(TrayElem) ppAutoAttackElems;	AST(PERSIST NO_TRANSACT SELF_ONLY FORCE_CONTAINER)
		// ORDERED list of "TrayElems" for use in autoattack.  These aren't real TrayElems in the sense that
		//  they don't show up in the UI, but they're built out of the same stuff so that it's easy for the
		//  autoattack system to support autoattacking in a variety of systems (inventory slots, power slots,
		//  specific powers, etc).  The server has no particular interest in validating this list, it just
		//  provides transacted storage.
} SavedTray;

AUTO_STRUCT AST_CONTAINER;
typedef struct SavedActiveSlots
{
	U32 eBagID;								AST(PERSIST SUBSCRIBE NO_TRANSACT)
		// The bag with the active slots

	int *eaiActiveSlots;					AST(NAME("ActiveSlot") PERSIST SUBSCRIBE NO_TRANSACT)
		// The slots
} SavedActiveSlots;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(hEntity);
typedef struct PuppetEntity
{
	const PuppetContainerState eState;		AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY)
		// The state of the owned container

	const U32 eType;						AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY)
		// The CharClassTypes this is, as defined by the pet def

	REF_TO(Entity) hEntityRef;				AST(SELF_AND_TEAM_ONLY COPYDICT(EntitySavedPet))
		// Subscription to puppet Entity, not available in transactions or from another subscribed Entity,
		//  and may be invalid or arbitrarily delayed

	char* pchLooseUI_Obsolete;				AST(PERSIST NO_TRANSACT SERVER_ONLY ESTRING NAME(pchLooseUI))
	SavedTray PuppetTray;					AST(EMBEDDED_FLAT)
		// Saved tray information for this puppet

	EARRAY_OF(SavedActiveSlots) ppActiveSlotsSaved;	AST(PERSIST NO_TRANSACT SUBSCRIBE SERVER_ONLY)
		// Saved active slots for this puppet

	EARRAY_OF(AttribMod) ppModsSaved;		AST(PERSIST NO_TRANSACT FORCE_CONTAINER SERVER_ONLY)
		// Saved AttribMods

	CONST_EARRAY_OF(AlwaysPropSlot)	ppSavedPropSlots; AST(PERSIST FORCE_CONTAINER SERVER_ONLY)
		// Saved AlwaysPropSlot array

	const GlobalType curType;				AST(PERSIST SELF_ONLY)
		// The GlobalType of the puppet, which absolutely must be GLOBALTYPE_ENTITYSAVEDPET

	const ContainerID curID;				AST(PERSIST SUBSCRIBE SELF_ONLY FORMATSTRING(FIXUP_CONTAINER_TYPE = ".curType"))
		// The ContainerID of the puppet, if the type (specified by curType) isn't GLOBALTYPE_ENTITYSAVEDPET then the world will explode
} PuppetEntity;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(hDisplayName);
typedef struct TempPuppetEntity
{
	CONST_REF_TO(PetDef) hPetDef;		AST(PERSIST)
		// The pet def that created this puppet

	REF_TO(PlayerCostume) hCostume;		AST(REFDICT(PlayerCostume) PERSIST)
		// The costume chosen from the creation process

	CONST_EARRAY_OF(SavedAttribute) ppSavedAttributes; AST(PERSIST FORCE_CONTAINER)
		// The saved attributes of this puppet entity

	const int uiID;						AST(PERSIST)
		// We don't have container ID's, because this information is not persisted
		// so created our own ID here. Must be unique though all TempPuppetEntites on
		// the owning entity

	SavedTray PuppetTray;				AST(EMBEDDED_FLAT)
		// Saved tray information for this puppet

	EARRAY_OF(AttribMod) ppModsSaved;	AST(PERSIST NO_TRANSACT FORCE_CONTAINER SERVER_ONLY)
		// Saved AttribMods
}TempPuppetEntity;


AUTO_STRUCT AST_CONTAINER;
typedef struct PuppetRequest
{
	CONST_STRING_MODIFIABLE pchType;	AST(PERSIST SERVER_ONLY)
	CONST_STRING_MODIFIABLE	pchName;	AST(PERSIST SERVER_ONLY)
	bool bCreateRequest;				AST(SERVER_ONLY)

} PuppetRequest;

AUTO_STRUCT AST_CONTAINER;
typedef struct PuppetMaster
{
	const EntityRef mainRef;							AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY FORMATSTRING(FIXUP_CONTAINER_TYPE = "EntityPlayer"))
		// The main entity ref

	const ContainerID curID;							AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY FORMATSTRING(FIXUP_CONTAINER_TYPE = ".curType"))
		// The current entity container ID

	const GlobalType curType;							AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY)
		// The current entity type

	const ContainerID curTempID;						AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY)
		// The current temporary puppet ID

	const ContainerID lastActiveID;						AST(PERSIST SUBSCRIBE)
		// The last active puppet ID

	ContainerID expectedID;
		// The expected current container ID

	GlobalType expectedType;
		// The expected current entity type

	STRING_MODIFIABLE	curPuppetName;					AST(USERFLAG(TOK_PUPPET_NO_COPY))
		// If this entity is swapped with a puppet we keep the name of the puppet swapped with so it can be available to the clients

	CONST_EARRAY_OF(PuppetEntity) ppPuppets;			AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY)
		// An array of puppets to be used by the master entity

	CONST_EARRAY_OF(TempPuppetEntity) ppTempPuppets;	AST(PERSIST, SELF_AND_TEAM_ONLY)

	CONST_EARRAY_OF(PuppetRequest) ppPuppetRequests;	AST(PERSIST, SERVER_ONLY)

	REF_TO(CharClassCategorySet) hPreferredCategorySet; AST(PERSIST, SELF_ONLY)

	const U32 uPuppetSwapVersion;						AST(PERSIST SERVER_ONLY)
	U32 uSavedModsVersion;								AST(PERSIST NO_TRANSACT SERVER_ONLY)

	U32 bPuppetTransformFailed : 1;						AST(PERSIST NO_TRANSACT SERVER_ONLY)
	U32 bPuppetTransformLogoutCheck : 1;				NO_AST
	U32 bPuppetCheckPassed : 1;							NO_AST
	U32 bSkippedSuccessOnLogin : 1;						NO_AST
	U32 bPuppetMasterCreateInProgress : 1;				NO_AST
}PuppetMaster;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerCostumeRef
{
	REF_TO(PlayerCostume) hCostume;								AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY NAME("Costume") FORCE_CONTAINER)
} PlayerCostumeRef;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerCostumeSlot
{
	const S32 iSlotID;											AST(PERSIST SUBSCRIBE SELF_ONLY NAME("SlotID") )
	CONST_STRING_POOLED pcSlotType;								AST(PERSIST SUBSCRIBE SELF_ONLY NAME("SlotType") POOL_STRING)
	CONST_OPTIONAL_STRUCT(PlayerCostume) pCostume;				AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY NAME("Costume") FORCE_CONTAINER)
} PlayerCostumeSlot;

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerCostumeData
{
	DirtyBit dirtyBit;											AST(NO_NETSEND)
	const U8 iActiveCostume;									AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY NAME("ActiveCostume"))
	const U8 iSlotSetVersion;									AST(PERSIST SUBSCRIBE SELF_ONLY NAME("SlotSetVersion"))
	bool bUnlockAll;											NO_AST
	CONST_STRING_POOLED pcSlotSet;								AST(PERSIST SUBSCRIBE SELF_ONLY NAME("SlotSet") POOL_STRING)
	
	CONST_EARRAY_OF(PlayerCostumeSlot) eaCostumeSlots;			AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY NAME("CostumeSlot") FORCE_CONTAINER NO_INDEX)
	CONST_EARRAY_OF(PlayerCostumeRef) eaUnlockedCostumeRefs;	AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY NAME("UnlockedCostume") FORCE_CONTAINER USERFLAG(TOK_PUPPET_NO_COPY))

	// The validate tag currently protects all values exist the "eaUnlockedCostumeRefs".
	// That value is not really worth protecting in this way given how it is used
	const U32 uiValidateTag;									AST(PERSIST SUBSCRIBE SERVER_ONLY NAME("ValidateTag"))
} PlayerCostumeData;

extern ParseTable parse_PlayerCostumeData[];
#define TYPE_parse_PlayerCostumeData PlayerCostumeData

AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerCostumeRefV0
{
	REF_TO(PlayerCostume) hCostume;					AST(PERSIST SELF_ONLY FORCE_CONTAINER)
} PlayerCostumeRefV0;

// WARNING: This structure is no longer used.  It is here simply so we
// can load costumes from entity fixup version 0.
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerCostumeListsV0
{
	DirtyBit dirtyBit;									AST(NO_NETSEND)
	const U8 activeCostumeType;							AST(PERSIST SELF_ONLY)
	const U8 activePrimaryCostume;						AST(PERSIST SELF_ONLY)
	const U8 activeSecondaryCostume;					AST(PERSIST SELF_ONLY)
	
	CONST_EARRAY_OF(PlayerCostumeV0) eaPrimaryCostumes;			AST(PERSIST SELF_ONLY FORCE_CONTAINER NO_INDEX)
	CONST_EARRAY_OF(PlayerCostumeV0) eaSecondaryCostumes;		AST(PERSIST SELF_ONLY FORCE_CONTAINER NO_INDEX)
	CONST_EARRAY_OF(PlayerCostumeRefV0) eaUnlockedCostumeRefs;	AST(PERSIST SELF_ONLY FORCE_CONTAINER)
	
	bool bUnlockAll;									NO_AST
} PlayerCostumeListsV0;

extern ParseTable parse_PlayerCostumeListsV0[];
#define TYPE_parse_PlayerCostumeListsV0 PlayerCostumeListsV0

AUTO_STRUCT AST_CONTAINER;
typedef struct PetRequest
{
	CONST_STRING_MODIFIABLE pchType;		AST(PERSIST SERVER_ONLY)
	CONST_STRING_MODIFIABLE pchName;		AST(PERSIST SERVER_ONLY)

	CONST_OPTIONAL_STRUCT(PlayerCostume) pCostume;	AST(PERSIST SERVER_ONLY FORCE_CONTAINER)

} PetRequest;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(uiPetRef);
typedef struct CritterPetRelationship
{
	REF_TO(PetDef) hPetDef;				AST(REFDICT(PetDef) SELF_ONLY PERSIST NO_TRANSACT)
	REF_TO(PlayerCostume) hCostume;		AST(REFDICT(PlayerCostume) SELF_ONLY PERSIST NO_TRANSACT)
	REF_TO(Message) hDisplayName;		AST(REFDICT(Message) SELF_ONLY PERSIST NO_TRANSACT)
	U32 uiPetID;						AST(PERSIST NO_TRANSACT)
	U32 erPet;
	Entity *pEntity;					NO_AST

}CritterPetRelationship;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(iSummonedSCP) AST_IGNORE_STRUCT(ppSuperCritterPets) AST_IGNORE(rotOffset) AST_IGNORE(uFreeCostumeChange) AST_IGNORE(curPuppetName) AST_IGNORE(bFixupTray) AST_FORMATSTRING(DEMO_NO_IGNORE_FIELDS = "savedName, savedSubName, savedDescription");
typedef struct SavedEntityData
{
	DirtyBit							dirtyBit;					AST(NO_NETSEND)

	const U32							uFixupVersion;				AST(PERSIST SUBSCRIBE SERVER_ONLY NAME("FixupVersion") USERFLAG(TOK_PUPPET_NO_COPY))
		// The version for entity fixup

	const U32							uGameSpecificFixupVersion;	AST(PERSIST SUBSCRIBE NAME("GameSpecificFixupVersion") USERFLAG(TOK_PUPPET_NO_COPY))
		// The game specific version for entity fixup

	const U32							uGameSpecificPreLoginFixupVersion; AST(PERSIST SUBSCRIBE NAME("GameSpecificPreLoginFixupVersion") USERFLAG(TOK_PUPPET_NO_COPY))
		// The game specific preload version for the entity fixup
	
	const char							savedName[MAX_NAME_LEN];	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE USERFLAG(TOK_PUPPET_NO_COPY) CASE_SENSITIVE) 
		// Name of this saved object

	CONST_STRING_MODIFIABLE				savedSubName;				AST(PERSIST SUBSCRIBE USERFLAG(TOK_PUPPET_NO_COPY) CASE_SENSITIVE)  
		// Entities can have a sub-name, such as a ship registry ID

	CONST_STRING_MODIFIABLE				savedDescription;			AST(PERSIST SUBSCRIBE SELF_ONLY USERFLAG(TOK_PUPPET_NO_COPY))  
		
	const SavedContainerRef				conOwner;					AST(PERSIST SUBSCRIBE USERFLAG(TOK_PUPPET_NO_COPY) FORMATSTRING(FIXUP_CONTAINER_REF = 1))
		// Saved entity that owns this entity

	CONST_EARRAY_OF(PetRelationship)	ppOwnedContainers;			AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY USERFLAG(TOK_PUPPET_NO_COPY))
		// Saved entities owned by this entity

	CONST_EARRAY_OF(PetDefRefCont)		ppAllowedCritterPets;		AST(PERSIST SELF_ONLY FORCE_CONTAINER USERFLAG(TOK_PUPPET_NO_COPY))
		// Array of allowed critter pets for the entity
	
	CritterPetRelationship				**ppCritterPets;			AST(PERSIST NO_TRANSACT SELF_AND_TEAM_ONLY USERFLAG(TOK_PUPPET_NO_COPY))

	CONST_OPTIONAL_STRUCT(EntitySavedSCPData) pSCPData;				AST(PERSIST SUBSCRIBE FORCE_CONTAINER)
	
	INT_EARRAY							ppRequestedPetIDs;			AST(PERSIST NO_TRANSACT SERVER_ONLY )
	INT_EARRAY							ppRequestedCritterIDs;		AST(PERSIST NO_TRANSACT SERVER_ONLY )
		// Lists of pet ids to spawn (these lists are adjusted by spawn rules)

	INT_EARRAY							ppAwayTeamPetID;			AST(SELF_AND_TEAM_ONLY)

	CONST_INT_EARRAY					ppPreferredPetIDs;			AST(SUBSCRIBE PERSIST SELF_ONLY FORMATSTRING(FIXUP_CONTAINER_TYPE = "EntitySavedPet") USERFLAG(TOK_PUPPET_NO_COPY))

	CONST_EARRAY_OF(AlwaysPropSlot)		ppAlwaysPropSlots;			AST(SUBSCRIBE PERSIST SELF_ONLY FORCE_CONTAINER USERFLAG(TOK_PUPPET_NO_COPY))

	const U32							iPetIDMax;					AST(PERSIST SERVER_ONLY USERFLAG(TOK_PUPPET_NO_COPY))
		// The current highest Pet ID
		// Changes rarely

	const U32							iPetID;						AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY USERFLAG(TOK_PUPPET_NO_COPY))
		// The Pet ID of this pet. Must be unique though all pets on a single master entity

	CONST_INT_EARRAY					piPetIDsRemovedFixup;		AST(PERSIST SERVER_ONLY USERFLAG(TOK_PUPPET_NO_COPY))
		// iPetIDs that have been removed (via trading or destroying).  This list is not a long-term list,
		//  it's for fixup.  After a transaction that adds something to this list, or upon login with items
		//  in this list, the Entity and all its OwnedContainers are fixed and this list is deleted.

	CONST_EARRAY_OF(PetRequest)			ppPetRequests;				AST(PERSIST SERVER_ONLY USERFLAG(TOK_PUPPET_NO_COPY))

	U32*								uiRemovedAwayTeamPetIDs;	NO_AST
		// List of PetIDs that have been removed by Pets Disabled volumes. Used to restore the correct pets when leaving the volume

	bool								bCheckPets : 1;				AST(SERVER_ONLY)
	bool								bPowerPropFail : 1;			AST(SERVER_ONLY)
	bool								bFixingOwner : 1;			NO_AST

	CONST_OPTIONAL_STRUCT(PuppetMaster)	pPuppetMaster;				AST(PERSIST SUBSCRIBE FORCE_CONTAINER, USERFLAG(TOK_PUPPET_NO_COPY))
		// Entites that this master entity can become

	// Build data
	CONST_EARRAY_OF(EntityBuild)		ppBuilds;					AST(PERSIST SELF_ONLY FORCE_CONTAINER)
		// EArray of builds for this entity
	
	const U32							uiIndexBuild;				AST(PERSIST SELF_ONLY)
		// The currently used index into the the builds array

	const U32							uiTimestampBuildSet;		AST(PERSIST SELF_ONLY)
		// The timestamp when the build index was last set
	
	const U32							uiTimestampCostumeSet;		AST(PERSIST SELF_ONLY)
		// The timestamp when the costume was last changed

	const U32							uiBuildValidateTag;			AST(PERSIST SERVER_ONLY)
		// Validate tag for build-related fields
	
	SavedTray*							pTray;						AST(PERSIST NO_TRANSACT SELF_ONLY FORCE_CONTAINER NAME(Tray,pSavedTray))
		//This tray is used if the entity has no puppets. If this entity has puppets, the tray is stored on the PuppetEntity structure

	Entity*								pEntityBackup;				NO_AST 
		// Backup copy of the entity, used by the server to compute diffs

	Entity*								pAutoTransBackup;			NO_AST
		// Backup used to make auto transactions more efficient

	PlayerCostumeData					costumeData;				AST(PERSIST SUBSCRIBE SELF_AND_TEAM_ONLY NAME("CostumeData"))
		// Costume storage

	CONST_OPTIONAL_STRUCT(PlayerCostumeListsV0)	obsolete_costumeData;	AST(PERSIST SELF_ONLY NAME("Costumes"))
		// This field is here to ensure that V0 entities can be loaded

	CONST_OPTIONAL_STRUCT(SavedMapDescription) lastStaticMap;		AST(PERSIST SUBSCRIBE SELF_ONLY FORCE_CONTAINER, USERFLAG(TOK_PUPPET_NO_COPY))
		// The last static map the player was on.  May be the current map.  

	CONST_OPTIONAL_STRUCT(SavedMapDescription) lastNonStaticMap;	AST(PERSIST SUBSCRIBE SELF_ONLY FORCE_CONTAINER, USERFLAG(TOK_PUPPET_NO_COPY))
		// The last non-static(mission) map the player was on.  May be the current map.

    const U32 transferDestinationMapID;                             AST(PERSIST, USERFLAG(TOK_PUPPET_NO_COPY))
        // When doing a map transfer, this is the containerID of the destination map
    CONST_STRING_POOLED transferDestinationSpawnPoint;              AST(PERSIST, USERFLAG(TOK_PUPPET_NO_COPY), POOL_STRING)
        // When doing a map transfer, this is the target spawn point on the destination map
	const Vec3 transferDestinationSpawnPos;							AST(PERSIST, USERFLAG(TOK_PUPPET_NO_COPY))
		// When doing a map transfer, if the spawn point is set to a position, this defines the position
	const Vec3 transferDestinationSpawnPYR;							AST(PERSIST, USERFLAG(TOK_PUPPET_NO_COPY))
		// When doing a map transfer, if the spawn point is set to a position, this defines the pitch, yaw and the roll
	const bool forceTransferDestinationSpawnPoint;					AST(PERSIST, USERFLAG(TOK_PUPPET_NO_COPY))
		// When doing a map transfer, if this flag is set the spawn point set in transferDestinationSpawnPoint is always respected
	const bool bSpawnPosSkipBeaconCheck;							AST(PERSIST, USERFLAG(TOK_PUPPET_NO_COPY))
		// If this flag is set, spawn position is not snapped to the nearest beacon

	const bool bLastMapStatic;										AST(PERSIST SUBSCRIBE SELF_ONLY, USERFLAG(TOK_PUPPET_NO_COPY))
		// If true the last map was static.

	CONST_EARRAY_OF(obsolete_SavedMapDescription) obsolete_mapHistory;	AST(PERSIST SELF_ONLY FORCE_CONTAINER NAME("Maphistory"), USERFLAG(TOK_PUPPET_NO_COPY))
		// We no longer use this field.  It is here so we can do fixup to populate the new fields the first time the player logs in.

	const bool							bBadName;					AST(PERSIST SUBSCRIBE SELF_ONLY)
		// Bad name set by CSR, player must add new name

	CONST_STRING_MODIFIABLE esOldName;								AST(PERSIST SUBSCRIBE SELF_ONLY ESTRING)
	// used with badname to prevent same or similar name

	CONST_REF_TO(PlayerDiary)			hDiary;						AST(PERSIST SUBSCRIBE SELF_ONLY SERVER_ONLY COPYDICT(PlayerDiary) USERFLAG(TOK_PUPPET_NO_COPY), FORMATSTRING(DEPENDENT_CONTAINER_TYPE = "PlayerDiary"))
	// the player's diary info

	CONST_EARRAY_OF(ActivityLogEntry)	activityLogEntries;			AST(PERSIST SUBSCRIBE SELF_ONLY FORCE_CONTAINER USERFLAG(TOK_PUPPET_NO_COPY))
	// persisted player activity log entries

	const U32							nextActivityLogID;			AST(PERSIST SUBSCRIBE SELF_ONLY USERFLAG(TOK_PUPPET_NO_COPY))
	// counter to keep the activity log IDs unique per player

	CONST_OPTIONAL_STRUCT(EntityInteriorData) interiorData;			AST(PERSIST SUBSCRIBE SELF_ONLY FORCE_CONTAINER USERFLAG(TOK_PUPPET_NO_COPY))
	// Store interior data for the entity.  Used on starship pets in STO for ship interiors.  Used on players in champs for hideouts

	CONST_OPTIONAL_STRUCT(AccountProxyKeyValueInfoListContainer) pPermissionsOnMostRecentLogin; AST(PERSIST SELF_ONLY SERVER_ONLY USERFLAG(TOK_PUPPET_NO_COPY))

	// Obsolete tray data
	int*								piUITrayIndex_Obsolete;		AST(NAME(piUITrayIndex) PERSIST NO_TRANSACT SELF_ONLY)
	CONST_EARRAY_OF(TrayElemOld)		ppTrayElems_Obsolete;		AST(NAME(ppTrayElems) PERSIST SUBSCRIBE SELF_ONLY FORCE_CONTAINER)
	CONST_EARRAY_OF(TrayElemOld)		ppAutoAttackElems_Obsolete;	AST(NAME(ppAutoAttackElems) PERSIST SUBSCRIBE SELF_ONLY FORCE_CONTAINER)

	LeaderboardStats					**ppLeaderboardStats;		AST(NAME(LeaderboardStats) PERSIST NO_TRANSACT SERVER_ONLY FORCE_CONTAINER)

	const U32							timeLastImported;			AST(PERSIST SUBSCRIBE USERFLAG(TOK_PUPPET_NO_COPY))
	const U32							timeLastRestored;			AST(PERSIST SUBSCRIBE USERFLAG(TOK_PUPPET_NO_COPY))

	const bool							bDisablePersistStanceInactive; AST(PERSIST SUBSCRIBE)
	// Set to true when the Character does not want to use their inactive persistent stance

	U32									uNewPetID;					AST(SELF_ONLY)
	// The ID of the last pet that the player acquired
	
	// Keep track of time that this player entered the current map
	U32 timeEnteredMap;
	bool bValidatedOwnedContainers;									NO_AST

	U8 iCostumeSetIndexToShow;										AST(PERSIST NO_TRANSACT SELF_ONLY SUBSCRIBE LOGIN_SUBSCRIBE) 

} SavedEntityData;

extern ParseTable parse_SavedEntityData[];
