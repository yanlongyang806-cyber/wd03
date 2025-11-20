#include "StringCache.h"
#include "StashTable.h"
#include "StashSet.h"
#include "MemoryMonitor.h"
#include "TimedCallback.h"

#include "wininclude.h"
#include "sharedmemory.h"
#include "StringTable.h"
#include "timing.h"
#include "utils.h"
#include "ScratchStack.h"
#include "file.h"
#include "StringUtil.h"
#include "MemTrack.h"
#include "earray.h"
#include "Alerts.h"
#include "GlobalTypes.h"
#include "tokenstore.h"
#include "UnitSpec.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc);); // Just the hashtable size is here, the strings are charged to the callers

#define TRACKING_MEM_COST (sizeof(void*)*5/4) // 1 ptr + assume optimal 75% full stashtable
#define MAX_SHARED_CHUNKS 200

static int linenum_for_string_table;
static int linenum_for_stash_table;

typedef struct SharedDataChunkHeader
{
	const char *pChunkName; // name passed to sharedMemoryAcquire
	const char *pDictName; // Resource dictionary for this chunk, if any
} SharedDataChunkHeader;

typedef struct SharedStringCacheHeader
{
	U32 numStrings;
	U32 totalSize;
	U32 bytesAlloced;
	SharedDataChunkHeader sharedChunks[MAX_SHARED_CHUNKS];
} SharedStringCacheHeader;

typedef struct SharedDataChunk
{
	const char *pChunkName; // name passed to sharedMemoryAcquire
	const char *pDictName; // Resource dictionary for this chunk, if any

	SharedMemoryHandle *pHandle;
} SharedDataChunk;

typedef struct StringCacheSource
{
	char *pFileLine;
	int iCount;
	int iStringSize;
	int iStashSize;
} StringCacheSource;

SharedDataChunk **ppSharedChunks;

#define EXTRA_CHUNK "ExtraChunk"

static U32 alloc_add_string_initial_size = 16*1024;
static bool ever_called_stringCacheSetInitialSize=false;
static bool string_cache_no_warnings=false;
static bool string_cache_grow_fast=false;
static int lockless_read_active;
StashSet	alloc_add_string_table = NULL;
StashTable	alloc_add_string_values;
StashTable	alloc_add_string_sources = NULL;

static CRITICAL_SECTION scCritSect;
static int raw_stringcache_size=0;
static int stringcache_estimated_stashtable_size=0;
static int shared_size;
static SharedMemoryHandle *shared_handle = 0;
static bool shared_locked, shared_first;
static SharedStringCacheHeader *shared_header;
static StringTable stringcache_stringtable;
static U32 isInsideAssertThreadID;

#define SET_INSIDE_ASSERT	isInsideAssertThreadID = GetCurrentThreadId()
#define CLEAR_INSIDE_ASSERT	isInsideAssertThreadID = 0
#define IS_INSIDE_ASSERT	(isInsideAssertThreadID && isInsideAssertThreadID != GetCurrentThreadId())

#define STRINGCACHE_VALUE_NORMAL 0
#define STRINGCACHE_VALUE_FROMSHARED -1
#define STRINGCACHE_VALUE_STATICOVERRIDE -3 // -2 is not allowed by stashtable

bool g_disallow_static_strings;
#if !_PS3
bool g_ccase_string_cache; // Should only be set in an AUTO_RUN_FIRST or AUTO_RUN_SECOND
bool g_assert_verify_ccase_string_cache;
// Used for disenabling, as it's enabled in most apps
AUTO_CMD_INT(g_ccase_string_cache, CCaseStringCache) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_EARLYCOMMANDLINE;
AUTO_CMD_INT(g_assert_verify_ccase_string_cache, AssertVerifyCCaseStringCache) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_EARLYCOMMANDLINE;
#endif

static bool g_ignored;
// Now ignored
AUTO_CMD_INT(g_ignored, LowerCaseFilenamesInBinFiles) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_EARLYCOMMANDLINE;

static bool disableStringCacheAccounting;
// Disables accounting the string cache allocations to their callers
AUTO_CMD_INT(disableStringCacheAccounting, disableStringCacheAccounting) ACMD_CMDLINE ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CATEGORY(Debug);
// Disable extra string cache per file+line allocation debug information
static bool enableStringCacheFileLine = false;
AUTO_CMD_INT(enableStringCacheFileLine, enableStringCacheFileLine) ACMD_CMDLINE ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CATEGORY(Debug);

__forceinline static void initSCCriticalSection(void)
{
	ATOMIC_INIT_BEGIN;
	{
		InitializeCriticalSection(&scCritSect);
	}
	ATOMIC_INIT_END;
}

AUTO_RUN_FILE; // Really just want it to be _LATE, but things happen after _LATE
void allocAddStringDisallowStaticStrings(void)
{
	g_disallow_static_strings = true;
}

void stringCacheDisableWarnings(void)
{
	string_cache_no_warnings = true;
}

void stringCacheSetGrowFast(void) // Grow fast instead of space efficiently
{
	if (alloc_add_string_table)
		stashSetSetGrowFast(alloc_add_string_table, true);
	string_cache_grow_fast = true;
}

bool stringCacheIsNearlyFull()
{
	return stashSetGetOccupiedSlots(alloc_add_string_table) > (stashSetGetResizeSize(alloc_add_string_table) * 9 / 10);
}

static bool g_setting_case_override=false;
static bool g_static_strings_inited=false;
static void allocAddStringInitStaticCase(void)
{
	if(!allocAddStringManualLock())
	{
		assert(0);
	}

	if (!g_static_strings_inited)
	{
		static const char *static_cstrings[] = 
		{
			"AccessLevel",
			"AccountID",
			"AccountName",
			"Action",
			"ActionType",
			"Activate",
			"ActivityType",
			"ActorName",
			"AerosolAtmosphereThickness",
			"AerosolParticleColorHSV",
			"Affects",
			"Aggro",
			"AI",
			"AIMaxRange",
			"AIMinRange",
			"Alert",
            "AllegianceName",
			"Alternates",
			"AltPivot",
			"AlphaIfDXT5nm",
			"AlphaRef",
			"Anchor",
			"AmbientCube",
			"Arg",
			"ArgString",
			"Array",
			"Atmosphere",
			"AtmosphereRadiusSquared",
			"AtmosphereThickness",
			"Author",
			"AuthorID",
            "AvailableCharSlots",
			"BackLightBleed",
			"BackLightColor",
			"Bar",
			"BaseDef",
			"BitName",
			"Bits",
			"Blend",
			"Block",
			"Blocks",
			"Body",
			"BodySock",
			"BoneName",
			"bonusDef",
			"bonusList",
			"BottomPad",
			"Branch",
            "Buckets",
			"Buffer",
            "BytesRead",
			"Cancel",
			"CanPickup",
			"Category",
			"Cells",
			"ChannelAccess", 
			"Channels",
			"CharacterName",
			"ChatConfig",
			"CheckMode",
			"Children",
			"cmd",
			"Code",
			"Color",
			"Colors",
			"Color0",
			"Color1",
			"Color2",
			"Color3",
			"Color_0",
			"Color_1",
			"Color_2",
			"Color_3",
			"ColorValue",
			"Column",
			"CombatEvent",
			"CombatEventResponse",
			"CombatTrackerNetList",
			"Command",
			"CommandList",
			"CommandString",
			"Comment",
			"Comments",
			"Complete",
			"Connected",
			"ContactDef",
			"ContactType",
			"ContainerID",
			"ContainerState",
			"ContainerType",
            "Content",
			"CooldownTime",
			"Cost",
			"Costume",
			"CostumeData",
			"CostumeName",
            "CostumePreset",
			"CostumeRef",
			"Costumes",
			"Count",
			"creationTime",
			"Criteria",			
			"CritterDef",
			"CritterFaction",
			"CritterGroup",
			"CritterOverrideDef",
			"CritterRank",
			"CritterSubRank",
			"CritterTags",
			"CritterVar",
			"Current",
			"CurStateTracker",
			"Curve",
			"CutsceneDef",
			"data",
			"Dead",
			"Debris",
			"DebugName",
			"Def",
			"Default",
			"DefaultTemplate",
			"Delay",
			"DemoPlayback",
			"Dependencies",
			"Depth",
			"Desc",
			"Description",
			"DetailTexture",
			"DialogBlock",
			"DiaryDisplayHeaders",
			"DiaryHeaders",
			"Diffuse_1Normal_Fallback",
			"Diffuse_2Color",
			"Diffuse_Lumi_Fallback",
			"Diffuse_NoAlphaCutout",
			"DiffuseWarp",
			"DiffuseWarpY",
			"Dir",
			"Direct3D9",	// See xdevice.h; these must match.
			"Direct3D9Ex",
			"Direct3D11",
			"Dirty",
			"Disabled",
			"DiscountDef",
			"DiscountPercent",
			"DisplayMessageList",
			"DisplayName",
			"DisplayNameMsg",
			"DisplayString",
			"DOFValues",
			"DonationTaskDef",
			"Duration",
			"DynFx",
			"Echo",
			"EffectArea",
			"Elements",
			"EMAILSENDINGFAILED",
			"Enabled",
			"EncounterData",
			"End",
			"EndPos",
			"EndTime",
			"EngineMisc",
			"Ent",
			"EntContainerID",
			"EntID",
			"EntityRef",
			"EntityAttach",
			"EntityType",
			"Entries",
			"EntryType",
			"EntType",
			"EP",
			"Error",		
			"ErrorMessage",
			"ErrorString",
			"EveryoneHatesMe",
			"EXE",
			"Expr",
			"ExprBlock",
			"ExprRequires",
			"ExprRequiresBlock",
			"ExprLine",
			"F",
			"Faction",
			"Factor",
			"Falling",
			"FallOff",
			"FarDist",
			"FeetL",
			"FeetR",
			"Fields",
			"File",
			"FileName",
			"FileNames",
			"Files",
			"FileSpec",
			"FileSpecs",
			"FileSystem",
			"filters",
			"FilterProfanity",
			"Finished",
			"FixupVersion",
			"Flag",
			"Flags",
			"FloatValue",
			"FocusedGen",
			"FolderName",
			"Folders",
			"Foo",
			"ForceTeamSize",
			"FormattedText",
			"FPS",
			"Friction",
			"From",
			"FSM",
			"FSMContext",
			"FSMName",
			"FSMs",
			"FullName",
			"FuncName",
			"Funcs",
			"FValue",
			"FX",
			"FxName",
			"FXSystem",
			"FXTexture1",
			"FXTexture2",
			"GAMESERVERNEVERSTARTED",
			"GAMESERVERNOTRESPONDING",
			"GAMESERVERRUNNINGSLOW",
            "gameSpecificChoice",
			"GameSystems",
			"GenData",
			"Genesis",
			"GenInstanceColumn",
			"GenInstanceColumnCount",
			"GenInstanceCount",
			"GenInstanceData",
			"GenInstanceNumber",
			"GenInstanceRow",
			"GenInstanceRowCount",
			"GeoType",
			"Gravity",
			"Group",
			"GroupName",
			"Groups",
			"GUID",
			"Guildmate",
			"H",
			"HandL",
			"Handle",
			"HandR",
            "hasCompletedTutorial",
			"hDef",
			"HealthState",
			"Height",
			"hFX",
			"Hidden",
			"HoldTime",
			"Html",
			"IconName",
			"ID",
			"IDString",
			"InBetweenSubs",
			"Index",
			"Info",
			"Input",
			"Instances",
			"IntensityMultiplier",
			"InteractDist",
			"InteractTargets",
			"InteractType",
			"Int",
			"InteriorData",
			"IntValue",
			"Inventory",
			"ip",
            "isCurrent",
			"IsOpen",
			"IsOwned",
			"IsPlayer",
            "IsReady",
			"IsStrafing",
			"IsTalking",
			"IsTarget",
			"ItemDef",
			"ItemPowerDef",
			"ItemQuality",
			"ItemTag",
			"Item",
			"Items",
			"ItemType",
			"Key",
			"KeyBind",
			"KILLINGNONRESPONSIVEGAMESERVER",
			"KillType",
			"Label",
			"LastPos",
			"LastUpdateTime",
			"LayerName",
			"LeftPad",
			"Level",
			"LevelUp",
			"LifeSpan",
			"LightType",
			"Link",
			"LimitName",
			"Lines",
			"List",
			"LitColor",
			"localdata",
			"LOD",
			"LODFar",
			"LODNear",
			"LODScale",
			"LogoutTimerType",
            "Logs",
			"LongTermData",
			"Machine",
			"MapDescription",
			"MapName",
			"MapVariables",
			"MaterialName",
			"Max",
			"MaxDist",
			"MaxDuration",
			"MaxLevel",
			"MaxSpeed",
			"Members",
			"Message",
			"MessageKey",
			"Messages",
			"MieScattering",
			"MiePhaseAssymetry",
			"Min",
			"MinionCostumeSet",
			"MinionPowerSet",
			"MinLevel",
			"Miss",
			"Mission",
			"MissionDef",
			"MissionInfo",
			"missionLockoutState",
			"MissionName",
			"MissionRefString",
			"Missionstate",
			"MissionTemplate",
			"missionType",
			"ModArray",
			"ModDef",
			"Mode",
			"ModelName",
			"ModuleName",
			"MOTD",
			"MouseX",
			"MouseY",
			"MouseZ",
			"MoveTime",
			"Moving",
			"Multiplier",
			"MuscleWeight",
			"Muted",
			"MyPID",
			"MyRef",
			"Name",
			"NameSpace",
			"NemesisName",
			"Nemesisstate",
			"NewGroup",
			"NewFSM",
			"NewParticle",
			"Node",
			"NodeName",
			"Nodes",
			"NormalizedViewHeight",
			"NoSuchFile",
			"Notes",
			"NotifyQueueItem",
			"NumToSpawn",
			"ObjName",
			"Officerskillpoint",
			"Offset",
			"OffsetFrom",
			"OnDeathPower",
			"Open",
			"Operation",
            "operationType",
			"Option",
			"Options",
			"Orderid",
			"Origin",
			"OrigStr",
			"Overall",
			"overrideList",
			"OwnerType",
			"OwnerID",
			"Pack",
			"pActivation",
			"Pairs",
			"Param",
			"Parameters",
			"Params",
			"Parent",
			"ParentFX",
			"ParentParentFX",
			"ParticleColorHSV",
			"Partitions",
			"Parts",
			"PatchInfo",
			"Path",
			"PCSlotSet",
			"PCSlotType",
			"PetDef",
			"PetInfo",
			"PetType",
			"PlanetRadius",
			"PlayerAccountID",
			"PlayerActivity",
			"playerName",	
			"players",
			"PlayerType",
			"port",
            "portNum",
			"Pos",
			"Position",
			"PosOffset",
			"PowerDef",
			"PowerMode",
			"PowerName",
            "powerNodes",
			"PowerTable",
			"PowerTreeDef",
			"Powinfo",
			"Price",
			"Priority",
			"ProductName",
			"Project",
			"ProjectID",
			"ProjectSpecialParam",
			"ProjectName",
			"ProjName",
			"Properties",
            "puppetInfo",
			"PVPDuelState",
			"PVPFlag",
			"Pvp_Resources",
			"pvpTeamA",
			"pvpTeamB",
			"PYR",
			"Quality",
			"QueueName",
			"Radius",
			"RayleighScattering",
			"Reason",
			"Ref",
			"ReferenceHandles",
			"Reflection",
			"ReflectionAddPercent",
			"ReflectionColorMask",
			"ReflectionCube",
			"ReflectionMIPBias",
			"ReflectionTexture",
			"ReflectionWeight",
			"RegionName",
			"Regions",
			"RegionType",
			"RenderingHacks",
			"Request",
			"RequestID",
			"RequiredNumericName",
			"ResourceID",
			"ResourceName",
			"Resources",
			"Restitution",
			"Result",
			"ReturnType",
			"Rev",
			"Reverb",
			"RightPad",
			"Room",
			"RoomName",
			"Rooms",
			"Root",
			"Rot",
			"Rotation",
			"RouteType",
			"Rows",
			"RP",
			"SandBox",
			"Scale",
			"SchemaType",
			"Scope",
			"Scrollbar",
			"Seed",
			"Sequence",
			"SeriesID",
			"Server",
			"ServerType",
			"Settings",
			"Shadows",
			"Shard",
			"shardName",
            "shardType",
			"Shared",
			"SiblingFX",
			"SingleTexture",
			"Size",
			"SkillPoint",
			"SkillType",
			"SkinColor",
            "skipTutorial",
			"Sky",
			"SkyGroup",
			"Slot",
			"SoftParticles",
			"SoundEffect",
			"Source",
			"Sources",
			"SpawnAnim",
			"SpawnPoint",
			"SpawnTarget",
			"SpawnWeight",
			"Spec",
			"SpecificValue",
			"SpecularWeight",
			"Speed",
			"Spline",
			"Sprite_2Texture_Add_AlphaMask_Fallback",
			"Sprite_2Texture_Intersect_AlphaMask_Fallback",
			"Sprite_Default",
			"Sprite_DualColor_AlphaMask",
			"Sprite_TexChannel_AlphaSwitch",
			"Stack",
			"Stars",
			"Start",
			"StartPos",
			"StartTime",
			"State",
			"StatePath",
			"States",
			"Stats",
			"Status",
			"SteamID",
			"Step",
			"Stopped",
			"Str",
			"Strength",
			"String",
			"Strings",
			"StringValue",
			"StringVar",
			"SubFSMOverride",
			"Subject",
			"SubMenu",
			"Success",
			"SVal",
			"SValue",
			"SystemReserved",
			"Tag",
			"Tags",
			"Target",
			"TargetDist",
			"targetEnt",
			"targetStatus",
			"Team",
			"TeamID",
			"TeamSize",
			"Text",
			"Texture",
			"TextureName",
			"TexXfrm",
			"Thread",
			"Time",
			"timeout",
			"TimeOverride",
			"Timer",
			"timeSince",
			"TimeStamp",
			"Tint",
			"Title",
			"To",
			"Total",
			"TotalCount",
			"TotalDuration",
			"ToolTip",
			"TopPad",
			"TotalCount",
			"Tracking",
			"Traction",
			"Transparent_Fresnel_2Texture_Fallback",
			"Trigger",
			"TurnRate",
			"Type",
			"UGCAchievementClientEvent",
			"UGCAchievementInfo",
			"UGCAchievementServerEvent",
			"UGCAchievementEvent",
			"UGCAchievementClientFilter",
			"UGCAchievementServerFilter",
			"UGCAchievementFilter",
			"UGCAchievementName",
			"UGCPlayableNameSpaceData",
			"UGCProjectID",
			"UIColor",
			"UID",
			"uiItemID",
			"UISystem",
			"UNASSIGNED_BIT",
			"UnlitColor",
            "unlockCreateFlags",
            "unlockedSpecies",
			"UsageRestrictionCategory",
			"User",
			"userID",
			"UserName",
			"UserStatus",
			"Val",
			"Value",
			"Values",
			"Variable",
			"Variables",
			"varName",
			"Vars",
			"VarTableDependency",
			"VarTableValue",
			"varValue",
			"Vec",
			"Version",
			"Versions",
			"VERSIONMISMATCH",
            "VirtualShardID",
			"Visible",
			"VoiceEnabled",
			"Volume",
			"W",
			"Waypoints",
			"Weight",
			"Weights",
			"white",
			"Width",
			"X",
			"XP",
			"Y",
			"ZoneName",
			"Currency",
			"Account",
			"EmailAddress",
			"Country",
			"MapContainerID"
		};

		int i;
		g_static_strings_inited = true;
		g_setting_case_override = true;

		for (i=0; i<ARRAY_SIZE(static_cstrings); i++)
			allocAddStringWhileLocked_dbg(static_cstrings[i], false, true, __FILE__, __LINE__);

		g_setting_case_override = false;
	}
	allocAddStringManualUnlock();
}

void stringCacheSetInitialSize(U32 numStrings)
{
	assertmsg(!ever_called_stringCacheSetInitialSize || (alloc_add_string_initial_size == numStrings * 13/10), "Two different callers to stringCacheSetInitialSize()");
	assertmsg(!alloc_add_string_table, "Something added to the StringCache before calling stringCacheSetInitialSize().");
	alloc_add_string_initial_size = numStrings * 13/10; // + 30%
	ever_called_stringCacheSetInitialSize = 1;
}

void stringCacheDoNotWarnOnResize(void)
{
	ever_called_stringCacheSetInitialSize = 0;
}

bool stringCacheIsLocklessReadActive(void)
{
	return lockless_read_active;
}

void stringCacheEnableLocklessRead()
{
	lockless_read_active = 1;
	assert(!alloc_add_string_table);
}

void stringCacheDisableLocklessRead()
{
	lockless_read_active = 0;
	if (alloc_add_string_table)
	{
		stashSetSetThreadSafeLookups(alloc_add_string_table, 0);
		stashSetSetCantResize(alloc_add_string_table, 0);
	}
}

static void initForLocklessReadIfActive()
{
	if (string_cache_grow_fast)
		stashSetSetGrowFast(alloc_add_string_table, true);
	if (lockless_read_active)
	{
		stashSetSetThreadSafeLookups(alloc_add_string_table, 1);
		stashSetSetCantResize(alloc_add_string_table, 1);
	}
}

void stringCacheInitializeShared(const char *sharedMemoryName, U32 totalSize, U32 numStrings, bool bWillBeIdentical)
{
	SM_AcquireResult ret;

	stringCacheSetInitialSize(numStrings);

	ret = sharedMemoryAcquire(&shared_handle, sharedMemoryName);

	if (ret == SMAR_FirstCaller)
	{
		disableStringCacheAccounting = true;
		shared_locked = 1;
		shared_size = totalSize;
		if (!sharedMemorySetSize(shared_handle, totalSize + sizeof(SharedStringCacheHeader)))
		{
			shared_handle = 0;
			shared_header = 0;
			shared_locked = 0;
			return;
		}
		shared_header = sharedMemoryAlloc(shared_handle, sizeof(SharedStringCacheHeader));
		shared_header->totalSize = totalSize;
		sharedMemoryCommitButKeepLocked(shared_handle);
		shared_first = 1;
	}
	else if (ret == SMAR_DataAcquired)
	{
		U32 i = 0, poolSize = sizeof(SharedStringCacheHeader);
		char *curString;

		if (!bWillBeIdentical)
		{
			sharedMemoryLock(shared_handle);
			shared_locked = 1;
		}
		shared_size = totalSize;		
		shared_header = (SharedStringCacheHeader *)sharedMemoryGetDataPtr(shared_handle);

		sharedMemorySetBytesAlloced(shared_handle, shared_header->bytesAlloced);

		if (!shared_header)
		{
			shared_handle = 0;
			shared_header = 0;
			shared_locked = 0;
			return;
		}

		curString = (char *)shared_header + sizeof(SharedStringCacheHeader);

		if (!alloc_add_string_table)
		{
			alloc_add_string_table= stashSetCreate(alloc_add_string_initial_size, __FILE__, __LINE__);
			initForLocklessReadIfActive();
			alloc_add_string_values= stashTableCreateAddress(1024);
		}
		while (i < shared_header->numStrings && poolSize < shared_header->totalSize)
		{
			int size = (int)strlen(curString) + 1;

			if(!stashSetAdd(alloc_add_string_table, curString, false, NULL))
			{
				SET_INSIDE_ASSERT;
				assert(0);
				CLEAR_INSIDE_ASSERT;
			}

			curString += size;
			i++;
			poolSize += size;

			raw_stringcache_size += size;
			stringcache_estimated_stashtable_size += sizeof(void*)*3;
		}

		for (i = 0; i < MAX_SHARED_CHUNKS; i++)
		{
			if (shared_header->sharedChunks[i].pChunkName)
			{
				SharedDataChunk *pChunk = calloc(sizeof(SharedDataChunk),1);
				pChunk->pChunkName = shared_header->sharedChunks[i].pChunkName;
				pChunk->pDictName = shared_header->sharedChunks[i].pDictName;

				if (stricmp(pChunk->pChunkName, EXTRA_CHUNK) != 0)
				{				
					ret = sharedMemoryAcquire(&pChunk->pHandle, pChunk->pChunkName);

					if (ret != SMAR_DataAcquired)
					{
						int j;
						char *estrError = NULL;
						estrPrintf(&estrError, "Failed to acquire share data chunk %s for the string cache, shared memory disabled\n", pChunk->pChunkName);
						printf("%s", estrError);
						if (isProductionMode()) {
							sharedMemoryQueueAlertString(estrError);
						}
						estrDestroy(&estrError);
						if (sharedMemoryIsLocked(pChunk->pHandle))
						{
							sharedMemoryUnlock(pChunk->pHandle);
						}
						for (j = 0; j < eaSize(&ppSharedChunks); j++)
						{
							if (sharedMemoryIsLocked(ppSharedChunks[j]->pHandle))
								sharedMemoryUnlock(ppSharedChunks[j]->pHandle);
							// Free references to the shared memory chunk in the OS, so that they don't interfere with future processes starting up
							sharedMemoryDestroy(ppSharedChunks[j]->pHandle);
						}
						if (shared_locked)
						{
							sharedMemoryUnlock(shared_handle);
							shared_locked = 0;
						}
						// Free references to the shared memory chunk in the OS, so that they don't interfere with future processes starting up
						sharedMemoryDestroy(shared_handle);
						shared_handle = 0;
						shared_header = 0;

						// finally, free reference to the manager segment
						sharedMemoryDestroyManager();
						break;
					}
				}
				eaPush(&ppSharedChunks, pChunk);
			}
		}

	}
	else if (ret == SMAR_Error)
	{
		shared_handle = 0;		
	}
	if (!g_static_strings_inited)
		allocAddStringInitStaticCase();
}

bool stringCacheSharingEnabled(void)
{
	return !!shared_handle;
}

bool stringCacheReadOnly(void)
{
	// String cache is available but is not locked, means we can't modify it.
	return !!shared_header && !shared_locked; 
}

void stringCacheFinalizeShared(void)
{
	if (shared_handle && shared_locked)
	{
		int i;		
		if (shared_first)
		{		
			assertmsg(eaSize(&ppSharedChunks) < MAX_SHARED_CHUNKS, "Too many shared chunks, raise size of MAX_SHARED_CHUNKS");
			for (i = 0; i < eaSize(&ppSharedChunks); i++)
			{
				shared_header->sharedChunks[i].pChunkName = ppSharedChunks[i]->pChunkName;
				shared_header->sharedChunks[i].pDictName = ppSharedChunks[i]->pDictName;
			}
		}
		else
		{
			for (i = 0; i <eaSize(&ppSharedChunks); i++)
			{
				assert(!sharedMemoryIsLocked(ppSharedChunks[i]->pHandle));
			}
		}
		shared_header->bytesAlloced = (U32)sharedMemoryBytesAlloced(shared_handle);
		sharedMemoryUnlock(shared_handle);		
		disableStringCacheAccounting = false;
	}
	shared_header = 0;
	shared_locked = 0;
}

SharedDataChunk *findDataChunk(const char *pRealName)
{
	int i;
	for (i = 0; i < eaSize(&ppSharedChunks); i++)
	{
		if (ppSharedChunks[i]->pChunkName == pRealName)
		{
			return ppSharedChunks[i];
		}
	}
	return NULL;
}

const char *stringCacheSharedMemoryChunkForDict(const char *pDictName)
{
	const char *pRealName = allocFindString(pDictName);
	int i;
	for (i = 0; i < eaSize(&ppSharedChunks); i++)
	{
		if (ppSharedChunks[i]->pDictName == pRealName)
		{
			return ppSharedChunks[i]->pChunkName;
		}
	}
	return NULL;
}

void stringCacheSharedMemoryRegisterExtra(const char *pDictName)
{
	if (shared_header && shared_first)
	{	
		SharedDataChunk *pChunk = calloc(sizeof(SharedDataChunk),1);
		pChunk->pChunkName = allocAddString(EXTRA_CHUNK);
		pChunk->pDictName = allocAddString(pDictName);

		eaPush(&ppSharedChunks, pChunk);
	}
}

SM_AcquireResult stringCacheSharedMemoryAcquire(SharedMemoryHandle **ppHandle, const char *name, const char *pDictName)
{
	const char *pRealName = allocAddString(name);
	if (shared_header && !shared_first)
	{	
		SharedDataChunk *pChunk = findDataChunk(pRealName);
		assertmsgf(pChunk,"Failed to find shared chunk %s, all servers must load identical shared memory chunks",name);
		*ppHandle = pChunk->pHandle;
		return SMAR_DataAcquired;
	}
	else
	{	
		SM_AcquireResult res = sharedMemoryAcquire(ppHandle, pRealName);
		if (res == SMAR_FirstCaller)
		{
			SharedDataChunk *pChunk = calloc(sizeof(SharedDataChunk),1);
			pChunk->pChunkName = pRealName;
			pChunk->pDictName = allocAddString(pDictName);
			pChunk->pHandle = *ppHandle;

			eaPush(&ppSharedChunks, pChunk);
		}
		return res;
	}
}

int allocAddStringCheck(void) // Checks for corruption
{
	int ret = 0;
	if(!IS_INSIDE_ASSERT)
	{
		initSCCriticalSection();
		EnterCriticalSection(&scCritSect);
		ret = stashSetVerifyStringKeys(alloc_add_string_table);
		LeaveCriticalSection(&scCritSect);
	}
	return ret;
}


S32 allocAddStringManualLock(void)
{
	if(IS_INSIDE_ASSERT)
	{
		return 0;
	}

	initSCCriticalSection();
	EnterCriticalSection(&scCritSect);
	if (!alloc_add_string_table)
	{
		alloc_add_string_table= stashSetCreate(alloc_add_string_initial_size, __FILE__, (linenum_for_stash_table=__LINE__));
		initForLocklessReadIfActive();
		alloc_add_string_values= stashTableCreateAddress(1024);
	}
	if (!shared_locked && !stringcache_stringtable)
		stringcache_stringtable = strTableCreateEx(StrTableDefault, 32*1024, __FILE__, (linenum_for_string_table=__LINE__));

	return 1;
}

typedef struct CCaseLookup {
	unsigned char map_normal;
	signed char map_firstletter_delta;
	unsigned char is_firstletter;
	unsigned char pad_UNUSED;
} CCaseLookup;

CCaseLookup lookup[256] = {
	{0,0,0},{1,0,0},{2,0,0},{3,0,0},{4,0,0},{5,0,0},{6,0,0},{7,0,0},
	{8,0,0},{9,0,0},{10,0,0},{11,0,0},{12,0,0},{13,0,0},{14,0,0},{15,0,0},
	{16,0,0},{17,0,0},{18,0,0},{19,0,0},{20,0,0},{21,0,0},{22,0,0},{23,0,0},
	{24,0,0},{25,0,0},{26,0,0},{27,0,0},{28,0,0},{29,0,0},{30,0,0},{31,0,0},
	{32,0,1},{33,0,0},{34,0,0},{35,0,0},{36,0,0},{37,0,0},{38,0,0},{39,0,0},
	{40,0,0},{41,0,0},{42,0,0},{43,0,0},{44,0,0},{45,0,0},{46,0,1},{47,0,1},
	{48,0,0},{49,0,0},{50,0,0},{51,0,0},{52,0,0},{53,0,0},{54,0,0},{55,0,0},
	{56,0,0},{57,0,0},{58,0,1},{59,0,0},{60,0,0},{61,0,0},{62,0,0},{63,0,0},
	{64,0,0},{97,-32,0},{98,-32,0},{99,-32,0},{100,-32,0},{101,-32,0},{102,-32,0},{103,-32,0},
	{104,-32,0},{105,-32,0},{106,-32,0},{107,-32,0},{108,-32,0},{109,-32,0},{110,-32,0},{111,-32,0},
	{112,-32,0},{113,-32,0},{114,-32,0},{115,-32,0},{116,-32,0},{117,-32,0},{118,-32,0},{119,-32,0},
	{120,-32,0},{121,-32,0},{122,-32,0},{91,0,0},{92,0,0},{93,0,0},{94,0,0},{95,0,1},
	{96,0,0},{97,-32,0},{98,-32,0},{99,-32,0},{100,-32,0},{101,-32,0},{102,-32,0},{103,-32,0},
	{104,-32,0},{105,-32,0},{106,-32,0},{107,-32,0},{108,-32,0},{109,-32,0},{110,-32,0},{111,-32,0},
	{112,-32,0},{113,-32,0},{114,-32,0},{115,-32,0},{116,-32,0},{117,-32,0},{118,-32,0},{119,-32,0},
	{120,-32,0},{121,-32,0},{122,-32,0},{123,0,0},{124,0,0},{125,0,0},{126,0,0},{127,0,0},
	{128,0,0},{129,0,0},{130,0,0},{131,0,0},{132,0,0},{133,0,0},{134,0,0},{135,0,0},
	{136,0,0},{137,0,0},{138,0,0},{139,0,0},{140,0,0},{141,0,0},{142,0,0},{143,0,0},
	{144,0,0},{145,0,0},{146,0,0},{147,0,0},{148,0,0},{149,0,0},{150,0,0},{151,0,0},
	{152,0,0},{153,0,0},{154,0,0},{155,0,0},{156,0,0},{157,0,0},{158,0,0},{159,0,0},
	{160,0,0},{161,0,0},{162,0,0},{163,0,0},{164,0,0},{165,0,0},{166,0,0},{167,0,0},
	{168,0,0},{169,0,0},{170,0,0},{171,0,0},{172,0,0},{173,0,0},{174,0,0},{175,0,0},
	{176,0,0},{177,0,0},{178,0,0},{179,0,0},{180,0,0},{181,0,0},{182,0,0},{183,0,0},
	{184,0,0},{185,0,0},{186,0,0},{187,0,0},{188,0,0},{189,0,0},{190,0,0},{191,0,0},
	{192,0,0},{193,0,0},{194,0,0},{195,0,0},{196,0,0},{197,0,0},{198,0,0},{199,0,0},
	{200,0,0},{201,0,0},{202,0,0},{203,0,0},{204,0,0},{205,0,0},{206,0,0},{207,0,0},
	{208,0,0},{209,0,0},{210,0,0},{211,0,0},{212,0,0},{213,0,0},{214,0,0},{215,0,0},
	{216,0,0},{217,0,0},{218,0,0},{219,0,0},{220,0,0},{221,0,0},{222,0,0},{223,0,0},
	{224,0,0},{225,0,0},{226,0,0},{227,0,0},{228,0,0},{229,0,0},{230,0,0},{231,0,0},
	{232,0,0},{233,0,0},{234,0,0},{235,0,0},{236,0,0},{237,0,0},{238,0,0},{239,0,0},
	{240,0,0},{241,0,0},{242,0,0},{243,0,0},{244,0,0},{245,0,0},{246,0,0},{247,0,0},
	{248,0,0},{249,0,0},{250,0,0},{251,0,0},{252,0,0},{253,0,0},{254,0,0},{255,0,0},
};

static void ccaseGenerateLookup(void)
{
	int i;
	for (i=0; i<256; i++)
	{
		int normal = tolower(i);
		int firstletter = toupper(i);
		int delta = firstletter - normal;
		assert(delta>-127 && delta < 127);
		printf("{%d,%d,%d},",
			normal,
			delta,
			(i==' ' || i == '_' || i == ':' || i == '.' || i == '/')?1:0);
		if (i%8==7)
			printf("\n");
	}
}


__forceinline void stringCacheFixCase(unsigned char *s)
{
	int bFirstLetter = 1;
	while (*s)
	{
		ANALYSIS_ASSUME(*s>=0 && *s<=255); // As opposed to the other values an unsigned char could possibly be... -_-
		{
			CCaseLookup lu = lookup[*s];
			*s = (unsigned char)(lu.map_normal + bFirstLetter * lu.map_firstletter_delta);
			bFirstLetter = lu.is_firstletter;
			s++;
		}
	}
}

bool StringIsCCase(const char *_s)
{
	const unsigned char *s = _s;
	int bFirstLetter = 1;
	while (*s)
	{
		ANALYSIS_ASSUME(*s>=0 && *s<=255); // As opposed to the other values an unsigned char could possibly be... -_-
		{
			CCaseLookup lu = lookup[*s];
			if (*s != (unsigned char)(lu.map_normal + bFirstLetter * lu.map_firstletter_delta))
				return false;
			bFirstLetter = lu.is_firstletter;
			s++;
		}
	}
	return true;
}

void StringToCCase(char *s)
{
	stringCacheFixCase(s);
}

#if 0
AUTO_RUN;
void fixupStringCache(void)
{
	// This is dangerous, it breaks any case-sensitive stashtables (g_saved_filenames)
	// So, only do it if -AssertVerifyCCaseStringCache is specified as well
	// Also, haven't rewritten it to work with the StashSet
	if (g_ccase_string_cache && g_assert_verify_ccase_string_cache)
	{
		// Some things added before AUTO_RUN_EARLY's DoCommandLineParsing may not have had case fixed
		if(!allocAddStringManualLock())
		{
			assert(0);
		}
		FOR_EACH_IN_STASHTABLE2(alloc_add_string_table, elem)
		{
			const char *s = stashElementGetStringKey(elem);
			if (!StringIsCCase(s))
			{
				// Doesn't work - parsetables break
				// 				char *newString;
				// 				newString = strTableAddString(stringcache_stringtable, s);
				// 				stringCacheFixCase(newString);
				// 				stashRemoveInt(alloc_add_string_table, s, NULL);
				// 				stashAddInt(alloc_add_string_table, newString, 1, true);
				#if !PLATFORM_CONSOLE
					DWORD old;
					VirtualProtect((void*)s, 1024, PAGE_WRITECOPY, &old);
				#endif
				stringCacheFixCase((char*)s);
			}
		}
		FOR_EACH_END;
		allocAddStringManualUnlock();
	}
}
#endif

// #define LOG_ALLOCADDSTRINGS
#ifdef LOG_ALLOCADDSTRINGS
int num_strings_pooled;
int num_memchecks;
#endif

#if !_PS3
static const char *allocAddString_last_filename = NULL;
static int	allocAddString_currfile_size_stash;
static int	allocAddString_currfile_size_string;
static int	allocAddString_last_linenum=0;
static int	allocAddString_last_count=0;
#endif

void allocAddStringFlushAccountingCache(void)
{
	allocAddStringManualLock();
	if (allocAddString_last_filename && !disableStringCacheAccounting)
	{
		memTrackUpdateStatsByName(__FILE__, linenum_for_stash_table, -allocAddString_currfile_size_stash, -allocAddString_last_count);
		memTrackUpdateStatsByName(__FILE__, linenum_for_string_table, -allocAddString_currfile_size_string, 0);
		memTrackUpdateStatsByName(allocAddString_last_filename, allocAddString_last_linenum, allocAddString_currfile_size_stash+allocAddString_currfile_size_string, allocAddString_last_count);
		allocAddString_last_filename = NULL;
		allocAddString_last_linenum = 0;
		allocAddString_currfile_size_stash = 0;
		allocAddString_currfile_size_string = 0;
		allocAddString_last_count = 0;
	}
	allocAddStringManualUnlock();
}

void allocAddStringMapRecentMemory(const char *src, const char *dst_fname, int dst_line)
{
#if !_PS3
	allocAddStringManualLock();
	//devassert(stricmp(src, allocAddString_last_filename)==0); // Something else must have flushed it otherwise, need a cleverer solution (set up the map beforehand?)
	if (stricmp(src, allocAddString_last_filename)==0 && !disableStringCacheAccounting)
	{
		memTrackUpdateStatsByName(__FILE__, linenum_for_stash_table, -allocAddString_currfile_size_stash, -allocAddString_last_count);
		memTrackUpdateStatsByName(__FILE__, linenum_for_string_table, -allocAddString_currfile_size_string, 0);
		memTrackUpdateStatsByName(dst_fname, dst_line, allocAddString_currfile_size_stash+allocAddString_currfile_size_string, allocAddString_last_count);
		allocAddString_last_filename = NULL;
		allocAddString_last_linenum = 0;
		allocAddString_currfile_size_stash = 0;
		allocAddString_currfile_size_string = 0;
		allocAddString_last_count = 0;
	}
	allocAddStringManualUnlock();
#endif
}

static int stringCacheError_resizeCount;
typedef enum StringCacheErrorType
{
	StringCacheError_Resize,
	StringCacheError_NearFull,
} StringCacheErrorType;
static void stringCacheError(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	StringCacheErrorType error = (StringCacheErrorType)(intptr_t)userData;
	if (error == StringCacheError_Resize)
	{
		if (--stringCacheError_resizeCount == 0)
		{
			Errorf("StringCache StashTable resized (contains %d strings).  A programmer must increase the valued passed to stringCacheSetInitialSize().", stashSetGetValidElementCount(alloc_add_string_table));
		}
	} else if (error == StringCacheError_NearFull)
	{
		Errorf("Shared string cache is getting full.  A programmer must increase pool size in stringCacheInitializeShared.");
	}
}

//__forceinline 
const char *allocAddStringWhileLocked_dbg(const char *s, bool bFixCase, bool bStaticString, const char *caller_fname, int line)
{
	const char *ret=NULL;
	U32 oldMaxSize;
	S32 gotLock = 1;

	if(bStaticString && g_disallow_static_strings)
	{
		SET_INSIDE_ASSERT;
		if (isDevelopmentMode())
			devassertmsgf(0, "Attempted to add static string \"%s\" after AUTO_RUNs, not allowed", s);
		else // Can't call devassert, it might spawn an ErrorTracker report thread, causing more errors/crash in memory budgets
			printf("Attempted to add static string \"%s\" after AUTO_RUNs, not allowed\n", s);
		CLEAR_INSIDE_ASSERT;
	}

	oldMaxSize = stashSetGetMaxSize(alloc_add_string_table);
	if (!stashSetFind(alloc_add_string_table, s, &ret))
	{
		char *newString;
		const char *newStringConst;
		int size;

#ifdef LOG_ALLOCADDSTRINGS
		{
			static FILE	*all_files,*all,*fnames,*notfiles;

			if (!all)
				all = fopen("c:/temp/all.txt","wb");
			fprintf(all,"%s %s\n",s, caller_fname);
			if (strchr(s,'/') && strchr(s,'.'))
			{
				if (!all_files)
					all_files = fopen("c:/temp/all_files.txt","wb");
				fprintf(all_files,"%s\n",s);

				if (!fnames)
					fnames = fopen("c:/temp/names.txt","wb");
				fprintf(fnames,"%s\n",getFileName((char*)s));
			}
			else
			{
				if (!notfiles)
					notfiles = fopen("c:/temp/notfiles.txt","wb");
				fprintf(notfiles,"%s\n",s);
			}
		}
#endif

		PERFINFO_AUTO_START("allocAddString not found", 1);
		size = (int)strlen(s) + 1;

		// Not found, add it!
		if (shared_locked)
		{
			static bool bWarned=false;
			if (!bWarned)
			{
				size_t shared_string_size = sharedMemoryGetSize(shared_handle);
				size_t allocated_size = sharedMemoryBytesAlloced(shared_handle);
				if (allocated_size >= shared_string_size*0.9 || allocated_size + size >= shared_string_size)
				{
					bWarned = true;
					//Errorf("Shared string cache is getting full.  A programmer must increase pool size in stringCacheInitializeShared.");
					// Can't call Errorf in here, it may need to call allocAddString too!
					TimedCallback_Run(stringCacheError, (UserData)StringCacheError_NearFull, 0);
				}
			}

			newString = sharedMemoryAlloc(shared_handle, size);
			assertmsg(newString, "Failed to allocate string from shared pool! Increase pool size in stringCacheInitializeShared");
			strcpy_s(newString, size, s);
			if ((bFixCase || g_ccase_string_cache) && !bStaticString)
				stringCacheFixCase(newString);
			newStringConst = newString;
		}
		else
		{
			if (bStaticString)
			{
				size = 0;
				newStringConst = s; // Just use the memory that's already static, can't modify it
			} else {
				newString = strTableAddString(stringcache_stringtable, s);
				if (shared_header)
				{
					if (isProductionMode())
					{
						// It is in fact okay to add cached strings when the shared string cache is available and not locked. This is ONLY safe because in this state there is no chance of new shared memory chunks being added.
						//assertmsgf(0, "You can't add new cachced string %s during load if shared memory is enabled. That string must be allocated by the first instance", s);
					}
					else
					{
						// In dev, disable shared memory, this can happen if you change data between runs of the server/testclient
						sharedMemorySetMode(SMM_DISABLED);
					}
				}
				if (bFixCase || g_ccase_string_cache)
				{
					stringCacheFixCase(newString);
				}
				newStringConst = newString;
			}
		}
		if (stashSetAdd(alloc_add_string_table, newStringConst, false, &ret))
		{
			int stashsize = 0;

			if (g_setting_case_override)
			{
				verify(stashAddressAddInt(alloc_add_string_values, newStringConst, STRINGCACHE_VALUE_STATICOVERRIDE, false));
			}
#if !_PS3
			// Add in estimated stashtable footprint
			stashsize += TRACKING_MEM_COST;

			if (memMonitorBreakOnAlloc[0] && strEndsWith(caller_fname, memMonitorBreakOnAlloc) &&
				(!memMonitorBreakOnAllocLine || memMonitorBreakOnAllocLine == line))
			{
				_DbgBreak();
				if (!memMonitorBreakOnAllocDisableReset)
					memMonitorBreakOnAlloc[0]='\0';
			}

			if (!disableStringCacheAccounting)
			{
				if (allocAddString_last_filename && (allocAddString_last_filename != caller_fname || allocAddString_last_linenum != line))
				{
					if (allocAddString_last_linenum == LINENUM_FOR_STRINGS) {
						allocAddString_last_linenum = LINENUM_FOR_POOLED_STRINGS;
					}
					memTrackUpdateStatsByName(__FILE__, linenum_for_stash_table, -allocAddString_currfile_size_stash, -allocAddString_last_count);
					memTrackUpdateStatsByName(__FILE__, linenum_for_string_table, -allocAddString_currfile_size_string, 0);
					memTrackUpdateStatsByName(allocAddString_last_filename, allocAddString_last_linenum, allocAddString_currfile_size_stash+allocAddString_currfile_size_string, allocAddString_last_count);
					allocAddString_currfile_size_stash = 0;
					allocAddString_currfile_size_string = 0;
					allocAddString_last_count = 0;
					//num_memchecks++;
				}
				allocAddString_last_filename = caller_fname;
				allocAddString_last_linenum = line;
				allocAddString_currfile_size_string += size;
				allocAddString_currfile_size_stash += stashsize;
				allocAddString_last_count++;

				if (enableStringCacheFileLine)
				{
					char buffer[256];
					StringCacheSource *pSource;
					sprintf(buffer, "%s:%d", caller_fname, line);
					if (!alloc_add_string_sources)
						alloc_add_string_sources = stashTableCreateWithStringKeys(512, StashDefault);
					if (!stashFindPointer(alloc_add_string_sources, buffer, &pSource))
					{
						pSource = calloc(1, sizeof(StringCacheSource));
						pSource->pFileLine = strdup(buffer);
						stashAddPointer(alloc_add_string_sources, pSource->pFileLine, pSource, false);
					}
					pSource->iCount++;
					pSource->iStringSize += size;
					pSource->iStashSize += stashsize;
				}
			}
#endif
			//num_strings_pooled++;
			raw_stringcache_size += size;
			stringcache_estimated_stashtable_size += TRACKING_MEM_COST;
			if (shared_header && shared_locked)
			{
				if (raw_stringcache_size > (shared_size * 9 / 10))
				{
					printf("Approaching max size of shared string cache. Get a programmer to raise it");
					shared_size *= 2; // To hide further messages
				}
				shared_header->numStrings++;
			}
		} 
		else 
		{
			// Someone else added it at the same time, this leaks a bit of memory
			if (!stashSetFind(alloc_add_string_table, s, &ret))
			{
				SET_INSIDE_ASSERT;
				assert(0);
				CLEAR_INSIDE_ASSERT;
			}
		}

		if (stashSetGetMaxSize(alloc_add_string_table) != oldMaxSize)
		{
			if (ever_called_stringCacheSetInitialSize && !string_cache_no_warnings)
			{
				//Errorf("StringCache StashTable resized (contains %d strings).  A programmer must increase the valued passed to stringCacheSetInitialSize().", stashSetGetValidElementCount(alloc_add_string_table));
				stringCacheError_resizeCount++;
				TimedCallback_Run(stringCacheError, (UserData)StringCacheError_Resize, 0);
			}
		}

		PERFINFO_AUTO_STOP();
	}
    if (gotLock) {
		assert(ret);
	    if (bStaticString && g_ccase_string_cache)
	    {
		    if (strcmp(ret, s)!=0)
		    {
				int override_value;
				if (!stashAddressFindInt(alloc_add_string_values, ret, &override_value))
					override_value=STRINGCACHE_VALUE_NORMAL;
				if (!override_value)
					if (isSharedMemory(ret))
						override_value=STRINGCACHE_VALUE_FROMSHARED;

			    assertmsg(!g_setting_case_override, "Two static override strings with different case?");
			    //printf("StaticStrings", "Two static strings with different case: \"%s\" \"%s\", one will be used at random\n", ret, s);
			    if (override_value==STRINGCACHE_VALUE_STATICOVERRIDE ||
				    override_value==STRINGCACHE_VALUE_FROMSHARED)
			    {
				    // It's okay, we have a hard-coded override for this string, use it
			    } else {
				    SET_INSIDE_ASSERT;
				    devassertmsgf(0, "The static string \"%s\" is referenced with two difference cases (\"%s\" and \"%s\"), this is invalid.  Either change the case of the newly added string to match, or add the canonical case to allocAddStringInitStaticCase().",
					    ret, s, ret);
				    CLEAR_INSIDE_ASSERT;
			    }
		    }
		    //devassertmsgf(strcmp(ret, s)==0, "Two static strings with different case: \"%s\" \"%s\", one will be used at random", ret, s);
	    }
    } else 
        ret = "cannotLockStringCache";
	return ret;
}

void allocAddStringManualUnlock(void)
{
	LeaveCriticalSection(&scCritSect);
}

const char* allocAddString_dbg(const char * s, bool bFixCase, bool bFixSlashes, bool bStaticString, const char *caller_fname, int line )
{
	char *temp=NULL;
	const char *ret=0;
	if (!s)
		return NULL;
	PERFINFO_AUTO_START_FUNC_L2();

	if (!g_static_strings_inited)
		allocAddStringInitStaticCase();

	if (bFixSlashes)
	{
		const char *c;
		int len=0;
		bool bNeedFix=false;
		// Check to see if we need a fix
		for (c=s; *c; c++, len++)
			if (*c=='\\' || c[0]==c[1] && c[0]=='/' && !(c==s)) // Has a backslash, or consecutive slashes (but not as the first characters, since //server/path is valid)
				bNeedFix = true;
		if (bNeedFix)
		{
			temp = ScratchAlloc(len+1);
			strcpy_s(temp, len+1, s);
			forwardSlashes(temp);
			fixDoubleSlashes(temp+1);
			s = temp;
		}
	}

	if (lockless_read_active)
	{
		stashSetFind(alloc_add_string_table, s, &ret);
	}

	if (!ret)
	{
		// WARNING: Although we grab and release this lock in this function, it may
		//  get released inside of allocAddStringWhileLocked_dbg() while doing slow
		//  operations, therefore you can NOT place statics in this function and
		//  expect them to be threadsafe.
		if(!allocAddStringManualLock())
		{
			ret = "cannotLockStringCache";
		}
		else
		{
			ret = allocAddStringWhileLocked_dbg(s, bFixCase, bStaticString, caller_fname, line);
			allocAddStringManualUnlock();
		}
	}
	if (temp)
		ScratchFree(temp);

	PERFINFO_AUTO_STOP_L2();
	return ret;
}

const char* allocFindString(const char * s)
{
	const char *ret;
	if (!s)
		return NULL;
	if(IS_INSIDE_ASSERT)
	{
		return "cannotLockStringCache";
	}
	PERFINFO_AUTO_START_FUNC_L2();
	initSCCriticalSection();
	EnterCriticalSection(&scCritSect);
	if (!alloc_add_string_table)
	{
		ret = NULL;
	}
	else
	{
		if (!stashSetFind(alloc_add_string_table, s, &ret))
			ret = NULL;
	}
	LeaveCriticalSection(&scCritSect);
	PERFINFO_AUTO_STOP_L2();
	return ret;
}

static StashTable	alloc_add_case_sensitive_string_table;
const char* allocAddCaseSensitiveString( const char * s )
{
	StashElement element;
	const char *temp;
	if (!s)
		return NULL;
	if (temp = allocFindString(s)) {
		if (strcmp(temp, s)==0) // The correct case is in the regular string table
			return temp;
	}
	EnterCriticalSection(&scCritSect);
	if (!alloc_add_case_sensitive_string_table)
		alloc_add_case_sensitive_string_table = stashTableCreateWithStringKeys(128, StashDefault|StashDeepCopyKeys_NeverRelease|StashCaseSensitive);
	if (!stashFindElement(alloc_add_case_sensitive_string_table, s, &element))
	{
		// Not found, add it!
		if (!stashAddIntAndGetElement(alloc_add_case_sensitive_string_table, s, 1, false, &element))
		{
			SET_INSIDE_ASSERT;
			assert(0);
			CLEAR_INSIDE_ASSERT;
		}
	}
	temp = stashElementGetStringKey(element);
	LeaveCriticalSection(&scCritSect);
	return temp;
}

AUTO_COMMAND;
void PrintStringCacheStats(void)
{
	printf("String Cache:\n");
	printf("String Count %d\n", stashSetGetValidElementCount(alloc_add_string_table));
	printf("Raw Memory %d\n", raw_stringcache_size);
	printf("Estimated Stashtable Memory %d\n", stringcache_estimated_stashtable_size);
}

AUTO_COMMAND;
void PrintStringCacheSources(void)
{
	if (!disableStringCacheAccounting && enableStringCacheFileLine)
	{
		StashTableIterator iter;
		StashElement elem;
		char buf[64];
		char sizebuf[64];
		char sizebuf2[64];
		int iTotalCount = 0;
		int iTotalSize = 0;
		int iTotalStashSize = 0;
		int iTotalTrackingSize = 0;
		printf("String Cache Sources:\n");
		stashGetIterator(alloc_add_string_sources, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			StringCacheSource *pSource = (StringCacheSource*) stashElementGetPointer(elem);
			filenameWithStructMappingInFixedSizeBuffer(stashElementGetStringKey(elem), 44, SAFESTR(buf));
			printf("%44.44s %10d %10s %10s\n", buf, pSource->iCount, friendlyLazyBytesBuf(pSource->iStashSize, sizebuf), 
				friendlyLazyBytesBuf(pSource->iStringSize, sizebuf2));
			iTotalCount += pSource->iCount;
			iTotalSize += pSource->iStringSize;
			iTotalStashSize += pSource->iStashSize;
			iTotalTrackingSize += (int) strlen(pSource->pFileLine)+1;
		}
		printf("Totals:    %10d %10s %10s\n", iTotalCount, friendlyLazyBytesBuf(iTotalSize, sizebuf), 
				friendlyLazyBytesBuf(iTotalStashSize, sizebuf2));
		iTotalTrackingSize += sizeof(StringCacheSource)*stashGetCount(alloc_add_string_sources);
		printf("Tracking Stash Table Size: %s\n", friendlyLazyBytesBuf(iTotalTrackingSize, sizebuf));
	}
	else
		printf("String Cache File+Line Accounting is disabled.\n");
}

// Add a bunch of garbage to the string cache to force an overflow
AUTO_COMMAND ACMD_COMMANDLINE;
void DebugFillStringCache(int size)
{
	char newString[128];
	int i;
	for (i = 0; i < size; i++)
	{
		sprintf(newString,"ThisIsALongStringToFillTheCache%d", i);
		allocAddString(newString);
	}
}