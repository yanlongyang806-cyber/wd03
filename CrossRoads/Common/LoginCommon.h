#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"
#include "referencesystem.h"
#include "MultiVal.h"

#include "structDefines.h"	// For StaticDefineInt

#define MAX_NAME_LEN 128
#define MAX_DESCRIPTION_LEN 2048


typedef struct BasicTexture BasicTexture;
typedef struct CharacterClass CharacterClass;
typedef struct CharacterPath CharacterPath;
typedef struct GameAccountData GameAccountData;
typedef struct NamedPathQueryAndResult NamedPathQueryAndResult;
typedef struct PCMood PCMood;
typedef struct PetDef PetDef;
typedef struct PlayerCostume PlayerCostume;
typedef struct NOCONST(PlayerCostume) NOCONST(PlayerCostume);
typedef struct PlayerShowOverhead PlayerShowOverhead;
typedef struct PowerTreeDef PowerTreeDef;
typedef struct SpeciesDef SpeciesDef;
typedef struct AssignedStats AssignedStats;
typedef struct NOCONST(AssignedStats) NOCONST(AssignedStats);
typedef struct AccountProxyProduct AccountProxyProduct;
typedef struct MicroTransactionProductList MicroTransactionProductList;

// These are intended to roughly correspond to ISM states, 
// but are restricted to what should be displayed to other players
AUTO_ENUM;
typedef enum LoginLinkState
{
	LOGINLINKSTATE_DEFAULT = 0,
	LOGINLINKSTATE_CHARACTER_SELECT,
	LOGINLINKSTATE_CHARACTER_CREATION,
	LOGINLINKSTATE_MAP_SELECT, // Map Selection for users who can do this
	LOGINLINKSTATE_LOBBY,
	LOGINLINKSTATE_LOBBYPARTYSCREEN,
	LOGINLINKSTATE_UGC_PROJECT_SELECT,

	LOGINLINK_STATE_MAX, EIGNORE
} LoginLinkState;

AUTO_STRUCT;
typedef struct SentCostume
{
	GlobalType type;
	ContainerID id;
	union
	{
		NOCONST(PlayerCostume) *pCostume; NO_AST
		PlayerCostume *pConstCostume; AST(LATEBIND)
	};
} SentCostume;

AUTO_STRUCT;
typedef struct LoginPetInfo
{
	char*			pchType;
	char*			pchName;
	PetDef*			pPetDef; NO_AST

	SpeciesDef*		pSpecies; NO_AST
	U32				iPreset;
	bool			bIsCustomSpecies;

	union
	{
		NOCONST(PlayerCostume) *pCostume; NO_AST
		PlayerCostume *pConstCostume; AST(LATEBIND)
	};
} LoginPetInfo;

extern ParseTable parse_LoginPetInfo[];
#define TYPE_parse_LoginPetInfo LoginPetInfo


AUTO_STRUCT;
typedef struct LoginPuppetInfo
{
	char* pchType;
	char* pchName;

} LoginPuppetInfo;

extern ParseTable parse_LoginPuppetInfo[];
#define TYPE_parse_LoginPuppetInfo LoginPuppetInfo

typedef struct PossibleCharacterChoice PossibleCharacterChoice;

AUTO_STRUCT;
typedef struct PossibleCharacterCostume {
	const char *pcCostume;					AST(STRUCTPARAM POOL_STRING NAME("Costume") NAME("hCostume")) // Only used at creation time, gets cloned later
	int iSlotID;							AST(NAME("ID"))
	const char *pcSlotType;					AST(POOL_STRING NAME("SlotType"))

	union
	{
		NOCONST(PlayerCostume) *pCostume;	NO_AST
		PlayerCostume *pConstCostume;		AST(LATEBIND)
	};
} PossibleCharacterCostume;

AUTO_STRUCT;
typedef struct PossibleCharacterNumeric
{
	const char* pchNumericItemDef; AST(POOL_STRING KEY)
	S32 iNumericValue;
} PossibleCharacterNumeric;

AUTO_STRUCT;
typedef struct PossibleCharacterNumerics
{
	EARRAY_OF(PossibleCharacterNumeric) eaNumerics;
} PossibleCharacterNumerics;

AUTO_STRUCT;
typedef struct PossibleCharacterChoice
{
	// --- The following fields are set by the login server when retrieving login choices ---

	GlobalType iType;
	ContainerID iID;

	// Subscribed entity
	REF_TO(Entity) hEnt;				AST(COPYDICT(ENTITYPLAYER))

	ContainerID iTeamID;
    ContainerID iGuildID;
	ContainerID iVirtualShardID;
	U64 xuid;
	U32 ePlayerType;
	U8 uiRespecConversions;
		// The times this character has been respec'ed due to player type conversion

	U16 uFixupVersion;
	U16 uGameSpecificFixupVersion;

	U32 uiLastPlayedTime;
	U32 uiCreatedTime;
	U32 uiDeletedTime;
	int iLevel;
	float fTotalPlayTime; 

	bool bUGCAllowed;

	//if true, this is a "minimal" character choice, because the object DB was under stress
	//so only filled in some information. 
	bool bMinimal;
	bool bFullRequested; AST(NO_NETSEND) //used locally on game client to indicate whether the full
		//characterChoice has been requested from the loginserver/objectDB

	bool bSafeLoginRecommended; AST(NO_NETSEND) //set on client... the last time this character map transferred, the
		//transfer failed. A safe login might help.
	
	// Login choices such as bad names, map selection (skip tutorial)
	bool bBadName;							AST(NAME("BadName"))
	// what the old bad name was
	char *esOldName;						AST(ESTRING)
	bool bSkipTutorial;						AST(NAME("SkipTutorial"))

	NamedPathQueryAndResult **eaInfo;		AST(NAME("Info"))

	// The map the player was last on
	const char *pcMapName;					AST(NAME("MapName") POOL_STRING)

	// These define which puppet is currently active on the player's entity
	GlobalType iPuppetType;
	ContainerID iPuppetID;

	// This is the puppet pet type and is only set on puppet entities
	U32 iPetType;

	// These are the active puppets
	PossibleCharacterChoice **eaPuppets;	AST(NAME("Puppet") )


	// --- The following fields are used BOTH by the Login server when retrieving choices ---
	// --- and are provided by character creation when saving a new character ---

	char name[MAX_NAME_LEN];
	char subName[MAX_NAME_LEN];
	char description[MAX_DESCRIPTION_LEN];

	PossibleCharacterCostume **eaCostumes;	AST(NAME("Costumes") NAME("Costume") NAME("hCostume"))
	REF_TO(CharacterClass) hClass;			AST(NAME("Class"))
	REF_TO(CharacterPath) hCharacterPath;	AST(NAME("CharacterPath"))
	REF_TO(PCMood) hMood;					AST(NAME("Mood"))
	const char *pcAllegianceName;			AST(POOL_STRING NAME("FactionName") NAME("AllegianceName") NAME("hCritterFaction"))

	PossibleCharacterNumeric **eaNumerics;	AST(NAME("Numeric"))

	// --- The following fields are provided by character creation when saving a new character ---

	SpeciesDef*	pSpecies; NO_AST
	U32			iPreset;
	bool		bIsCustomSpecies;

	const char *pcClassName;				AST(POOL_STRING NAME("ClassName") NAME("hClass"))
	const char *pchCharacterPathName;		AST(POOL_STRING NAME("CharacterPathName"))
	const char *pcMoodName;					AST(POOL_STRING NAME("MoodName") NAME("hMood"))
	const char *pcSpeciesName;				AST(POOL_STRING NAME("SpeciesName") NAME("hSpecies"))
	const char *pcPowerTree;				AST(POOL_STRING NAME("PowerTree") NAME("hPowerTree"))
	char **eaPowerNodes;

	//An additional choice which is fully handled on a per-project basis.
	char* pchGameSpecificChoice;

	LoginPetInfo**		eaPetInfo;
	LoginPuppetInfo**	eaPuppetInfo;

	// --- This field is used between DB server and Login Server... otherwise NULL ---

	const char *pcStringCostume;


	//the object DB now tells the login server who owns each container
	GlobalType eOwnerType;
	ContainerID iOwnerID;

	// Assigned stats during character creation
	union
	{
		NOCONST(AssignedStats) **ppAssignedStats; NO_AST
		AssignedStats **ppConstAssignedStats; AST(LATEBIND NAME("AssignedStat"))
	};


} PossibleCharacterChoice;

extern ParseTable parse_PossibleCharacterChoice[];
#define TYPE_parse_PossibleCharacterChoice PossibleCharacterChoice

// Provides data necessary for converting a Character from one PlayerType to another.
//  In practice each project will have different needs.
AUTO_STRUCT;
typedef struct PlayerTypeConversion
{
	// Common

	int iPlayerTypeNew;
		// What PlayerType we're switching to - make sure you validate this value and the old value
	
	char *pchCharacterPath;
		// The desired CharacterPath

	S32 bConvertToFreeform : 1;
		// FC Standard to Premium
	
	S32 bSideConvert : 1;
		// FC Same to Same (Allows converting the character to freeform or a different AT)

	S32 bPayWithGADToken : 1;
		// If this is not free, pay with a GAD token

} PlayerTypeConversion;

AUTO_STRUCT;
typedef struct PossibleVirtualShard
{
	U32 iContainerID;
	char *pName;
	bool bUGCShard;
	// The virtual shard is disabled, so you can't log in or create characters, but you can delete characters.
	bool bDisabled;
	// Currently only used for UGC shards, this returns true if the shard is available to the player
	bool bAvailable;
	int iNumCharacterSlotsLeft;
	int iNumCharacterSlotsExtra;
} PossibleVirtualShard;


AUTO_STRUCT;
typedef struct UnlockableAllegianceNames
{
	const char **pchNames; AST(NAME(UnlockableName))

} UnlockableAllegianceNames;

extern DefineContext *g_pUnlockedAllegianceFlags;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pUnlockedAllegianceFlags);
typedef enum UnlockedAllegianceFlags
{
	kUnlockedAllegianceFlags_None = 0
	//
} UnlockedAllegianceFlags;

AUTO_STRUCT;
typedef struct UnlockableCreateNames
{
	const char **pchNames; AST(NAME(CreateUnlockableName))

} UnlockableCreateNames;

extern DefineContext *g_pUnlockedCreateFlags;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pUnlockedCreateFlags);
typedef enum UnlockedCreateFlags
{
	kUnlockedCreateFlags_None = 0
	//
} UnlockedCreateFlags;

AUTO_STRUCT;
typedef struct LoginSpeciesUnlockedRef
{
	REF_TO(SpeciesDef) hSpecies;
} LoginSpeciesUnlockedRef;

AUTO_ENUM;
typedef enum CharSlotRestrictFlag
{
    CharSlotRestrictFlag_None = 0,
    CharSlotRestrictFlag_Allegiance = 1<<1,
    CharSlotRestrictFlag_VirtualShard = 1<<2,
} CharSlotRestrictFlag;

AUTO_STRUCT;
typedef struct CharSlotRestriction
{
    // how many slots have these restrictions
    int numSlots;

    // flags indicating which fields to restrict
    CharSlotRestrictFlag flags;
    
    // used with CharSlotRestrictFlag_VirtualShard to indicate which virtual shard the slot is restricted to
    ContainerID virtualShardID;

    // the name of the AllegianceDef that this slot is restricted to if CharSlotRestrictFlag_Allegiance is set
    STRING_POOLED allegianceName;   AST(POOL_STRING)
} CharSlotRestriction;

AUTO_STRUCT;
typedef struct AvailableCharSlots
{
    // total number of character slots that might be available(depending on restrictions).  This number should be
    //  the sum of numUnrestrictedSlots and all slots represented by eaSlotRestrictions.
    int numTotalSlots;

    // number of unrestricted slots available
    int numUnrestrictedSlots;

    // description of restricted slots
    CharSlotRestriction **eaSlotRestrictions;
} AvailableCharSlots;

extern ParseTable parse_AvailableCharSlots[];
#define TYPE_parse_AvailableCharSlots AvailableCharSlots

AUTO_STRUCT;
typedef struct PossibleCharacterChoices
{
	bool bRetry;
	PossibleCharacterChoice **ppChoices;
	PossibleCharacterChoice **ppDeleted;
	GameAccountData *pGameAccount;
		// The player's game account data
	REF_TO(GameAccountData) hGAD;		AST(COPYDICT(GameAccountData))


	bool dirty;
	bool bHasCompletedTutorial;

	bool bUGCCharactersOnly;
		// Only ugc character

	int iNumCharacterSlotsLeft;
		// How many more character slots available
	int iNumCharacterSlotsExtra;
		// How many character slots that were earned or purchased
	int iBaseUGCSlots;
		// How many default slots the character gets

	int iNumSuperPremiumLeft;
		// The number of special characters that this account can still create

    // new implementation of available character slots
    AvailableCharSlots *pAvailableCharSlots;
   
	UnlockedAllegianceFlags eUnlockAllegianceFlags;		AST(FLAGS)
	UnlockedCreateFlags eUnlockCreateFlags;				AST(FLAGS)
	LoginSpeciesUnlockedRef **eaUnlockedSpecies;
	
	GlobalType serverType;				NO_AST
	ContainerID serverID;				NO_AST

	U32 bInitialLogin : 1;
		// This login is the initial login to this login server.  Used to control the clients response to updated character lists.

	PossibleVirtualShard **ppVirtualShardsForNewCharacter;
} PossibleCharacterChoices;

AUTO_STRUCT;
typedef struct CharacterCreationRef
{
	REF_TO(PowerTreeDef) hPowerTreeDef; // Add other ref types if needed
	REF_TO(CharacterClass) hClass;
	REF_TO(PetDef) hPet;
	REF_TO(SpeciesDef) hSpecies;
	REF_TO(PlayerCostume) hCostume;
} CharacterCreationRef;

AUTO_STRUCT;
typedef struct CharacterCreationDataHolder
{
	CharacterCreationRef ** ppRefs;
} CharacterCreationDataHolder;

AUTO_STRUCT;
typedef struct AccountFlagUpdate
{
	union {
		char *accountName; AST(REDUNDANTNAME)
		char *displayName;
	};

	U32 uFlags;
	U32 uDuration;
	bool bClearFlag;
	bool bOverride;
	U32 uCurrentExpiration;

	bool bSuccess;
	char *pErrorString;
} AccountFlagUpdate;

AUTO_STRUCT;
typedef struct LoginData
{
	U32 uAccountID;
	S32 iAccessLevel;
	U32 uAccountPlayerType;
	const char *pAccountName;
	const char *pPwAccountName; AST(NAME("pwAccountName"))
	const char *pDisplayName;
	const char *pShardInfoString;
	char *pShardClusterName;
	char *pUgcShardName;
	U32 uiLastTermsOfUse;
	U32 uiServerTimeSS2000;
} LoginData;

//gets the registry key name for the key that is set when a player begins a login or map transfer and is removed when it
//completes safely
char *GetLoginBeganKeyName(const char *pCharName);

// Match the most restrictive available slot for the given virtual shard and allegiance
// If a match is found optionally remove the slot from availableSlots.
bool CharSlots_MatchSlot(AvailableCharSlots *availableSlots, U32 virtualShardID, const char *allegianceName, bool remove);

// Add character slots, with or without restrictions, to the AvailableCharSlots struct.
void CharSlots_AddSlots(AvailableCharSlots *availableSlots, int numSlots, CharSlotRestrictFlag flags, U32 virtualShardID, char *allegianceName);


//////////////////
//
// Added for Gateway. Likely to be superseded when clustering code is done and formally 
// splits getting character lists from logging in.
// 

AUTO_STRUCT;
typedef struct SimpleCharacterChoice
{
	U32 id;                   AST(KEY)
		// Container ID
	char *name;               AST(STRUCTPARAM ESTRING)
		// Character name

} SimpleCharacterChoice;

AUTO_STRUCT;
typedef struct SimpleCharacterChoiceList
{
	U32 uAccountID;                              AST(NAME(accountID))
	char *estrPublicAccountName;                 AST(NAME(publicAccountName) ESTRING)
	EARRAY_OF(SimpleCharacterChoice) eaOnline;   AST(NAME(Online) FORMATSTRING(XML_DECODE_KEY = 1))
	EARRAY_OF(SimpleCharacterChoice) eaOffline;  AST(NAME(Offline) FORMATSTRING(XML_DECODE_KEY = 1))
	EARRAY_OF(SimpleCharacterChoice) eaDeleted;  AST(NAME(Deleted) FORMATSTRING(XML_DECODE_KEY = 1))
} SimpleCharacterChoiceList;

//
// End Gateway Only
//
/////////////////

// End of File
