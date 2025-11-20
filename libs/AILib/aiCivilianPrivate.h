#pragma once

#include "mathutil.h"
#include "aiCivilian.h"
#include "stdtypes.h"
#include "textparser.h"
#include "windefinclude.h"
#include "referencesystem.h"

typedef struct AICivilianBucket AICivilianBucket;
typedef struct AICivilianWorldLegGrid AICivilianWorldLegGrid;
typedef struct AICivPlayerKillEventManager AICivPlayerKillEventManager;
typedef struct AICivPOIManager AICivPOIManager;
typedef struct AICivCrosswalkUser AICivCrosswalkUser;
typedef struct AICivPOIUser AICivPOIUser;
typedef struct AICivStopSignUser AICivStopSignUser;
typedef struct AICivilianPedestrian AICivilianPedestrian;
typedef struct AICivilianTrafficQuery AICivilianTrafficQuery;
typedef struct AICivRegenReport AICivRegenReport;
typedef struct AICivIntersectionManager AICivIntersectionManager;
typedef struct AICivCrossTrafficManager AICivCrossTrafficManager;
typedef struct AICivCarBlockManager AICivCarBlockManager;
typedef struct AICivCrosswalkManager AICivCrosswalkManager;
typedef struct AICivilianSpawnVolume AICivilianSpawnVolume;
typedef struct AICivilianTrolley AICivilianTrolley;
typedef struct AICivilianPathLeg AICivilianPathLeg;
typedef struct AICivilianPathIntersection AICivilianPathIntersection;
typedef struct AICivilianPathInfo AICivilianPathInfo;
typedef struct AICivilianMapDef AICivilianMapDef;
typedef struct AICivilianPathMapInfo AICivilianPathMapInfo;
typedef struct AICivilianPathPoint AICivilianPathPoint;
typedef struct AICivilian AICivilian;
typedef struct AICivWanderArea AICivWanderArea;
typedef struct AICivLegDef AICivLegDef;
typedef struct ACGDelNode ACGDelNode;
typedef struct ACGDelLine ACGDelLine;
typedef struct AITeam AITeam;
typedef struct CommandQueue CommandQueue;
typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct GameEvent GameEvent;
typedef struct MovementRequester MovementRequester;
typedef struct WorldVolumeEntry WorldVolumeEntry;
typedef struct WorldInteractionEntry WorldInteractionEntry;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct gslHeatMapCBHandle gslHeatMapCBHandle;
typedef struct AICivDisplayNameData AICivDisplayNameData;
typedef struct WorldRegion WorldRegion;
#define MAX_PATHLEG_MID_INTERSECTIONS 2

AUTO_ENUM;
typedef enum EAICivilianType 
{
	EAICivilianType_NULL = -1,
	EAICivilianType_PERSON = 0,
	EAICivilianType_CAR,
	EAICivilianType_TROLLEY,
	EAICivilianType_COUNT
} EAICivilianType;

extern F32 g_CivTickRates[EAICivilianType_COUNT];

// 
// ---------------------------------------------------------------
typedef enum EAICivilianLegType 
{
	EAICivilianLegType_NONE = -1,
	EAICivilianLegType_PERSON = 0,
	EAICivilianLegType_CAR,
	EAICivilianLegType_TROLLEY,
	EAICivilianLegType_PARKING,
	EAICivilianLegType_INTERSECTION,
	EAICivilianLegType_COUNT
	
} EAICivilianLegType;


// ---------------------------------------------------------------
AUTO_ENUM;
typedef enum CivGenState {
	CGS_NONE = 0,
	CGS_GRID,
	CGS_EDGE,
	CGS_EDGE2,
	CGS_EDGE3,
	CGS_LINE,
	CGS_LINE2,
	CGS_LINE3,
	CGS_POSTLINE,
	CGS_PAIR,
	CGS_PAIR2,
	CGS_LANE,
	CGS_LEG,
	CGS_LEG2,
	CGS_LEG3,
	CGS_LEG4,
	CGS_INT0,
	CGS_INT1,
	CGS_INT2,
	CGS_INT3,
	CGS_INT4,
	CGS_SPLIT,
	CGS_MIN,
	CGS_COPLANAR,
	CGS_CROSSWALK,
	CGS_PATHPOINTS,
	CGS_CURVEFITTING,
	CGS_FIN,
	CGS_FILE,
	CGS_DONE,
	CGS_COUNT
} CivGenState;

// ---------------------------------------------------------------
typedef enum EAICivCarMove
{
	EAICivCarMove_STRAIGHT = 0,
	EAICivCarMove_GENERIC_TURN,
	EAICivCarMove_RIGHT_HAND_TURN,
	EAICivCarMove_LEFT_HAND_TURN,
	EAICivCarMove_COUNT,
} EAICivCarMove;

// ---------------------------------------------------------------
typedef enum EAICivCarStreetIsect
{
	EAICivCarStreetIsect_NONE = 0,
	EAICivCarStreetIsect_MID_EDGETOSIDE,
	EAICivCarStreetIsect_MID_SIDETOEDGE,

} EAICivCarStreetIsect;


// ---------------------------------------------------------------
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct AICivilianPathLeg 
{
	S32 index;
	Vec3 start;
	Vec3 end;
	Vec3 dir;
	F32 len;
	Vec3 perp;
	F32 width;
	S32 max_lanes;
	F32 median_width;
	F32 lane_width;
	S32 leg_set;


	F32 fSkewedAngle_Start;
	F32 fSkewedLength_Start;

	EAICivilianLegType type;					AST(INT)
	Vec3 normal;								NO_AST
	
	CivGenState deleted;
	const char *deleteReason;					AST(POOL_STRING)

	StashTable tracked_ents;					NO_AST

	// Only one leg can be directly next or previous
	AICivilianPathLeg *next;					NO_AST
	AICivilianPathLeg *prev;					NO_AST
	S32 nextByIndex;
	S32 prevByIndex;

	// Only one intersection on each end
	AICivilianPathIntersection *nextInt;		NO_AST
	AICivilianPathIntersection *prevInt;		NO_AST
	S32 nextIntByIndex;
	S32 prevIntByIndex;

	// Unlimited number of intersections in the middle
	AICivilianPathIntersection **midInts;		NO_AST
	INT_EARRAY midIntsByIndex;

	AICivilianPathLeg **eaCrosswalkLegs;		NO_AST	
	INT_EARRAY eaCrosswalkLegsByIndex;			// list of crosswalks that are near this leg

	// For crosswalks
	AICivCrosswalkUser **eaCrosswalkUsers;		NO_AST
	U32	iCrosswalkTimer;						NO_AST
	AICivilianTrafficQuery* pXTrafficQuery;		NO_AST	// for crosswalks not linked to traffic control
	AICivilianPathLeg* pCrosswalkNearestRoad;	NO_AST
	U32	uLanesBlockedMask;						NO_AST
	S32	crosswalkNearestRoadByIndex;
	F32 crosswalkRoadStartDist;
	
	// Stash table to represent directionality
	StashTable flowStash;						NO_AST
	
	// an index into the
	const char*	pchLegTag;						AST(POOL_STRING)
	const AICivLegDef *pLegDef;					NO_AST
	
	U32 bIsGroundCoplanar : 1;	// if set, the leg is assumed to be co-planar with ground beneath it
	U32 bIsCrosswalk : 1;		
	U32 bIsXingStopLight : 1;			
	U32 bSkewed_Start : 1;

	U32 bIsOneWay : 1;							NO_AST
	U32 bIsForcedLeg : 1;						NO_AST
	U32 bForcedLegAsIs : 1;						NO_AST
	U32 doneStart : 1;							NO_AST
	U32 doneEnd : 1;							NO_AST
	U32 bIsCrosswalkOpen : 1;					NO_AST	// flag for crosswalk
		
	U32 track_active : 1;						NO_AST	// used for block tracking
	U32 track_active_added : 1;					NO_AST	// used for block tracking
	U32 bSpawningDisabled : 1;					NO_AST
} AICivilianPathLeg;



// ---------------------------------------------------------------
typedef enum EIntersectionType
{
	EIntersectionType_NONE,
	EIntersectionType_SIDESTREET_STOPSIGN,
	EIntersectionType_STOPSIGN,

	EIntersectionType_marker_STOPLIGHT,
	EIntersectionType_2WAY_STOPLIGHT = EIntersectionType_marker_STOPLIGHT,
	EIntersectionType_3WAY_STOPLIGHT,
	EIntersectionType_4WAY_STOPLIGHT,
	EIntersectionType_COUNT,
} EIntersectionType;

// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct PathLegStopLight
{
	REF_TO(WorldInteractionNode)	hStopLight;

} PathLegStopLight;

// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct PathLegIntersect 
{
	AICivilianPathLeg *leg;								NO_AST
	AICivilianPathLeg *pCrosswalkLeg;					NO_AST
	S32 legByIndex;
	S32 crosswalkByIndex;

	Vec3 intersect;
			
	PathLegStopLight	**eaStopLights;

	// used for civilian path generation
	U32 continue_isvalid : 1;							NO_AST
} PathLegIntersect;

// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct AICivilianPathIntersection 
{
	Vec3 min;
	Vec3 max;

	S32 index;
	PathLegIntersect **legIntersects;
	EIntersectionType isectionType;						AST(INT)

	StashTable tracked_ents;							NO_AST
			
	// stop sign data
	AICivStopSignUser** eaStopSignUsers;				NO_AST
	// stop light
	S64 timeOfLastChange;								NO_AST
	S32 iOpenDirectionIdx;								NO_AST
	

	// 
	EntityRef		hFirstComeTrolley;			NO_AST
	
	U32 bIsMidIntersection : 1;

	U32 intersection_disabled : 1;						NO_AST
	U32	trolley_is_crossing : 1;						NO_AST
	U32 light_is_yellow : 1;							NO_AST	
	U32 track_active : 1;								NO_AST	// used for block tracking
	U32 track_active_added : 1;							NO_AST	// used for block tracking
} AICivilianPathIntersection;



AUTO_STRUCT;
typedef struct AICivilianPathPointIntersection
{
	S32						myIndex;
	S32						*eaPathPointIndicies;

	AST_STOP
	AICivilianPathPoint		**eaPathPoints;
	S32						iUseIdx;
} AICivilianPathPointIntersection;

// ---------------------------------------------------------------
// Used for things like the trolleys
AUTO_STRUCT;
typedef struct AICivilianPathPoint
{
	Vec3 vPos;
	
	S32 myIndex;
	S32 nextPathPointIndex;
	S32 intersectionIndex;
	S32 pathPointIntersectionIndex;
	
	S32 bIsReversalPoint;
	
	AST_STOP

	AICivilianPathPoint			*pNextPathPoint;
	AICivilianPathPoint			*pPrevPathPoint;
	AICivilianPathIntersection	*pIntersection;
	AICivilianPathPointIntersection *pPathPointIntersection;
	U32 deleted : 1;
	U32 isectionFixupFlag : 1;
} AICivilianPathPoint;

typedef U64 ACGCurveKey;

AUTO_STRUCT;
typedef struct AICivIntersectionCurve
{
	Vec3				vCurvePoint;

	AICivilianPathLeg	*legSource;			NO_AST
	S32					legSourceIndex;
	AICivilianPathLeg	*legDest;			NO_AST
	S32					legDestIndex;
	
	S32					sourceLane;
	S32					destLane;
	S32					bRightTurn;
	ACGCurveKey			key;				NO_AST
} AICivIntersectionCurve;

ACGCurveKey CreateKeyFromCurve(const AICivIntersectionCurve*);
ACGCurveKey CreateKey(const AICivilianPathLeg *pLegSrc, const AICivilianPathLeg *pLegDest, S32 srcLane, S32 dstLane, S32 bRightTurn);


// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct CivilianGenerator 
{
	Vec3 world_pos;

	EAICivilianType type; AST(INT)
} CivilianGenerator;

// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct CivilianPathBox 
{
	Vec3 local_min;
	Vec3 local_max;
	Mat4 world_mat;

	U32 person	: 1;
	U32 car		: 1;
} CivilianPathBox;


// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct AICivilianPathMapInfo {
	U32 mapCRC;

	U32 procVersion;
} AICivilianPathMapInfo;


// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct AICivEmoteReactInfo
{
	// the emote that the civilian is to react to
	const char	*pszEmoteName;						AST(POOL_STRING NAME("EmoteName"))

	// the FSM that will be run, currently this is required if the default one is not specified
	const char	*pszFsm;							AST(POOL_STRING NAME("FSM"))

	// list of animations that will be picked at random
	const char	**eaAnimReactions;					AST(POOL_STRING NAME("AnimReactions"))

} AICivEmoteReactInfo;

// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct AICivEmoteReactDef
{
	// the default FSM if an emote does not have one
	const char			*pszDefaultFsm;				AST(POOL_STRING NAME("DefaultFSM"))
	
	// default list of animations
	const char			**eaDefaultAnimReactions;	AST(POOL_STRING NAME("DefaultAnimReactions"))

	// list of emote reactions
	AICivEmoteReactInfo	**eaEmoteReaction;			AST(NAME("EmoteReaction"))

	// the radius of getting a civilian to react
	F32			fRadius;							AST(DEFAULT(20))

	// the maximum number of civilians that will react. This is ignored if UseSelectedCivOnly is true
	S32			iMaxCivsToReact;					AST(DEFAULT(1))

	// if true, will only consider the entity's target entity as the focus of the emote
	bool		bUseSelectedCivOnly;
		
} AICivEmoteReactDef;


// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct AIPedestrianOnClickInfo
{
	//  the FSM that is run when clicked on
	const char *pchFsm;					AST(POOL_STRING NAME("FSM"))

	// the weighted chance this FSM will be chosen
	U32			weight;					AST(DEFAULT(1))

} AIPedestrianOnClickInfo;

// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct AIPedestrianOnClickDef
{
	// list of on click FSMs and their weights
	AIPedestrianOnClickInfo		**eaOnClickInfo;	AST( NAME("OnClickInfo"))

	AST_STOP
	U32							iOnClickMaxWeight;	
} AIPedestrianOnClickDef;

// ---------------------------------------------------------------
// .civ_def structs
// ---------------------------------------------------------------
AST_PREFIX(WIKI(AUTO))


AUTO_STRUCT;
typedef struct AICivilianDef
{
	// the name of this civilian
	const char *pchCivDefName;					AST(POOL_STRING NAME("CivilianDefName"))

	// the name of the CRITTER def 
	const char *pchCritterDef;					AST(POOL_STRING NAME("CritterDef"))
	
	// the minimum speed the civilian will normally move at
	F32			fSpeedMin;						AST(NAME("MinimumSpeed") DEFAULT(5))

	// the range of speed from the minimum 
	F32			fSpeedRange;					AST(NAME("SpeedRange") DEFAULT(5.6))
	
	S32			iSpawnChanceWeight;				AST(NAME("SpawnChanceWeight") DEFAULT(1))
	
	AST_STOP
	
	// for strict distribution
	F32 fTargetDistributionPercent;
} AICivilianDef;

#define GET_CIV_DEF_NAME(p)		((p)->pchCivDefName ? (p)->pchCivDefName : "UNNAMED")


AUTO_STRUCT;
typedef struct AICivPedestrianScaredParams
{
	// the message that the pedestrians will use when scared
	const char *pchScaredPedestrianMessage;		AST(NAME("PedestrainScaredMessage") POOL_STRING)

	// default scared behavior: The amount of time the civilian will be scared for
	F32			fScaredTime;					AST(DEFAULT(14))

	// after being scared, the timeout before civilians can be scared again
	F32			fRescareTime;					AST(DEFAULT(20))

	// when scared by something in front of you, the chance that the ped will cower
	// Value should be between 0 - 1
	F32			fCowerChance;					AST(DEFAULT(0.25))

	// time the pedestrian will cower before running
	F32			fCowerTime;						AST(DEFAULT(4))

	// the chance when scared that they will say something
	F32			fMessageChance;					AST(DEFAULT(0.6))

	F32			fMinimumSpeed;					AST(DEFAULT(13))
	F32			fSpeedRange;					AST(DEFAULT(3))

	// civilian will ignore scaring objects beyond this height
	F32			fScaredHeight;					AST(DEFAULT(15.f))

	// When scared, uses the critter speed. Allows civilians to respond to movement impairing powers
	U32			bUseCritterSpeed : 1;			AST(NAME("UseCritterSpeed"))


	// An optional expression that is run when the pedestrian becomes scared
	Expression* pExprOnScared;					AST(NAME("ExprBlockOnScared"), REDUNDANT_STRUCT("ExprOnScared", parse_Expression_StructParam), LATEBIND)

	U32 usedFields[1];							AST(USEDFIELD)

} AICivPedestrianScaredParams;

// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct AICivPedestrianDef
{
	AICivilianDef	base;						AST(EMBEDDED_FLAT)

	// optional FSM to override the scared behavior
	const char *pchFearBehaviorFSMOverride;		AST(POOL_STRING)

	// an anim list that is applied to the civilian when it is in its default behavior
	const char *pchAnimListDefault;				AST(POOL_STRING NAME("AnimListDefault"))

	// this is an override chatter message to the default chatter message in pedestrian type
	const char *pchPedestrianChatterMessage;	AST(NAME("PedestrianChatterMessage") POOL_STRING)

	// An optional expression that is run when the pedestrian becomes scared
	// this is an override to the AICivPedestrianTypeDef's AICivPedestrianScaredParams
	Expression* pExprOnScared;					AST(NAME("ExprBlockOnScared"), REDUNDANT_STRUCT("ExprOnScared", parse_Expression_StructParam), LATEBIND)

	// override on click behaviors 
	AIPedestrianOnClickDef onClick;	
	
	U32 usedFields[1];							AST(USEDFIELD)
	
} AICivPedestrianDef;

#define AI_CIV_DEFBASE(civDef)	(civDef->base)

AUTO_STRUCT;
typedef struct AICivPedestrianPOIParams
{
	// the duration that the jobs that use animations will run for
	F32				fAmbientAnimDuration;

	// the time before the job can be used again in seconds after it was finished being used last
	F32				fAmbientJobCooldownTime;			AST(DEFAULT(20.f))

	// the radius from the POI that civilians will be searched for
	F32				fAmbientJobCalloutRange;			AST(DEFAULT(40.f))

	// if the ambient job needs line of sight to the pedestrian to have the pedestrian use it
	U32				bAmbientJobCheckLineOfSight;		AST(DEFAULT(1))
	
	U32 usedFields[1];									AST(USEDFIELD)

} AICivPedestrianPOIParams;

// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct AICivPedestrianWanderParams
{
	// the minimum time the pedestrian will wander
	F32				fWanderTimeMin;			AST(DEFAULT(20.f))

	// the random range of time from the minimum that the pedestrian will wander
	F32				fWanderTimeRange;		AST(DEFAULT(10.f))
	
	// The average time the pedestrian will stay idle at a wander point
	F32				fTimeAtPointAvg;		AST(DEFAULT(3.f))
	
	U32 usedFields[1];						AST(USEDFIELD)

} AICivPedestrianWanderParams;

// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct AICivPedestrianTypeDef
{
	// List of civilian type definitions
	AICivPedestrianDef		**eaCivDefs;		AST(NAME("CivDef"))
	
	// the parameters for how the civilian are scared
	AICivPedestrianScaredParams	scaredParams;

	// The point of interest / ambient job parameters
	AICivPedestrianPOIParams	POIParams;		AST(NAME("POIParams"))

	// the parameters that pertain to wander behavior
	AICivPedestrianWanderParams	wanderParams;

	// the emote reaction def
	AICivEmoteReactDef *emoteReaction;		

	// the on click def
	AIPedestrianOnClickDef *onClick;
	
	// the message that is played when the pedestrians randomly chatter
	const char *pchPedestrianChatterMessage;	AST(NAME("PedestrianChatterMessage") POOL_STRING)
	

	// The FSM that runs when a pedestrian kills a nearby critter
	const char *pchPlayerKillCritterFSM;		AST(NAME("PlayerKillCritterFSM") POOL_STRING)

	// boolean value- if true, then when spawning a new civilian it will try and keep the ratios defined 
	S32 bUseStrictDistribution;					AST(NAME("UseStrictDistribution"))
	
	U32 usedFields[1];							AST(USEDFIELD)

	AST_STOP
	// runtime value to keep track of the weight basis for 
	S32			iCivDefsMaxWeight;
} AICivPedestrianTypeDef;

// ---------------------------------------------------------------
// currently used for cars and trolleys
AUTO_STRUCT;
typedef struct AICivVehicleTypeDef
{
	// List of civilian type definitions.
	// Note that the parameters of the "CivDef" are the same as the ones in the pedestrian 
	// but the auto-wiki is not duplicating the structure information
	AICivilianDef		**eaCivDefs;			AST(NAME("CivDef"))

	// the sound that is played when a car honks
	const char *pchCarHonkSoundName;			AST(NAME("CarHonkSoundName") POOL_STRING)

	U32 usedFields[1];							AST(USEDFIELD)

	AST_STOP
	// runtime value to keep track of the weight basis for 
	S32			iCivDefsMaxWeight;
} AICivVehicleTypeDef;

// ---------------------------------------------------------------
// 
AUTO_STRUCT;
typedef struct AICivLegDef
{
	// The name of this leg definition to be referenced 
	const char *pchName;						AST(POOL_STRING REQUIRED)
	
	// The chance that the leg will be chosen when pathing
	// Range is 0.0 - 1.0, default is 1
	F32		fLegUseChance;						AST(DEFAULT(1))
	
	U32 usedFields[1];							AST(USEDFIELD)

} AICivLegDef;

AUTO_STRUCT;
typedef struct AICivMapDefLegInfo
{
	// List of leg definitions
	AICivLegDef				**eaLegDef;					AST(NAME("LegDef"))

	U32 usedFields[1];									AST(USEDFIELD)

} AICivMapDefLegInfo;

// ---------------------------------------------------------------

AUTO_STRUCT WIKI("AICivilianMapDef");
typedef struct AICivilianMapDef 
{
	// the name of the civilian def
	const char* pchName;								AST(KEY)

	// AIConfigs that this inherits from - applied in order, so later ones override earlier
	const char **inheritConfigs;						AST(ADDNAMES("InheritConfig"), SIMPLE_INHERITANCE)

	// pedestrian type parameters
	AICivPedestrianTypeDef	pedestrian;					AST(ADDNAMES("npc"))

	// car type parameters. 
	AICivVehicleTypeDef		car;					

	// trolley type parameters
	AICivVehicleTypeDef		trolley;
	
	// leg definition info
	AICivMapDefLegInfo		legInfo;					AST(NAME("leg"))

	// the absolute number civilians that should be spawned for a given type
	int desired[EAICivilianType_COUNT];					AST(INDEX(0, npcs) INDEX(1, cars) INDEX(2, trolleys))

	// a density setting, number of units per type. 
	// The sum of the lengths of all the legs is used to calculate the desired number of civilian.
	// the desired count is calculated by: leg distance / unitsPerType
	// note: a non-zero desired amount will override this density  
	int desiredUnitsPerType[EAICivilianType_COUNT];		AST(INDEX(0, unitsPerNPC) INDEX(1, unitsPerCar) INDEX(2, unitsPerTrolley))

	F32 carLaneWidth;									AST(DEFAULT(20))

	// the faction that pedestrians will use when determining if the pedestrian will be scared by another character
	const char *pszPedestrianFaction;					AST(NAME("PedestrianFaction") POOL_STRING)

	// if true, legs will start off disabled until a player enters the region
	U32 bEnableLegsWhenPlayerEntersRegion : 1;
	
	char* pchFilename;									AST(CURRENTFILE)
	U32 usedFields[1];									AST(USEDFIELD)
	
} AICivilianMapDef;

const AICivLegDef* aiCivMapDef_GetCivLegDef(const AICivilianMapDef *pCivMapDef, const char *pchName);


// ---------------------------------------------------------------
// END .civ_def structs
// ---------------------------------------------------------------






// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct AICivilianWaypoint 
{
	union {
		const AICivilianPathLeg *leg;
		const AICivilianPathIntersection *acpi;
		const AICivilianPathPoint *pPathPoint;
	};											NO_AST

	Vec3 pos;

	
	AICivCrosswalkUser	*pCrosswalkUser;			NO_AST
	AICivilianPathIntersection *pPathPointIsect;	NO_AST
	
	F32 medianDist;
	F32 distFromLeg;
	S16 lane;

	U32 car_street_isect_type : 4;
	U32 car_move_type : 4; 
	U32 bIsLeg : 1;
	U32 bIsUTurn : 1;
	U32 bReverse : 1;
	U32 bStop : 1; // used for scheduled stopping 
	U32 bWasStop : 1;
	U32 bIsCrosswalk : 1;
	
} AICivilianWaypoint;

// ---------------------------------------------------------------
typedef struct AICivilianPath 
{
	AICivilianWaypoint**	eaWaypoints;
	AICivilianWaypoint**	eaAddedWaypoints;
	AICivilianWaypoint**	eaWaypointsToDelete;

	S32						curWp;			
	U32						waypointClearID;
} AICivilianPath;

// ---------------------------------------------------------------
typedef struct AICivilianSpawnVolume
{
	Vec3	min;
	Vec3	max;
	Vec3	vMid;

	S32		refCount;
	U64		volumePropertyID;
} AICivilianSpawnVolume;

// ---------------------------------------------------------------
typedef struct AICivilianSpawningData
{
	Vec3				pos;
	AICivilianPathPoint *pPathPoint;
	AICivilianPathLeg	*leg;
	bool				forward;
} AICivilianSpawningData;


typedef void (*fpCivInitialize)(Entity *e, AICivilian *civ, const AICivilianSpawningData *spawnData);
typedef void (*fpCivFree)(AICivilian *civ);
typedef F32 (*fpCivGetDesiredDistFromLeg)(const AICivilian *civ, const AICivilianWaypoint *relativeWaypt, int forward);
typedef F32 (*fpCivInitNextWayPtDistFromLeg)(const AICivilian *civ, const AICivilianPathLeg *leg, AICivilianWaypoint *pNewWaypoint, int forward);
typedef bool (*fpCivContinuePath)(AICivilian *civ);
typedef void (*fpCivReachedWaypoint)(AICivilian *civ, AICivilianWaypoint *curWp);
typedef void (*fpCivTick)(Entity *e, AICivilian *civ);
typedef void (*fpCivProcessPath)(Entity *e, AICivilian *civ, const Vec3 vPos, const Vec3 vFacingDir);

typedef struct AICivilianFunctionSet
{
	fpCivInitialize						CivInitialize;
	fpCivFree							CivFree;
	fpCivGetDesiredDistFromLeg			CivGetDesiredDistFromLeg;
	fpCivInitNextWayPtDistFromLeg		CivInitNextWayPtDistFromLeg;
	fpCivContinuePath					CivContinuePath;
	fpCivReachedWaypoint				CivReachedWaypoint;
	fpCivProcessPath					CivProcessPath;
} AICivilianFunctionSet;

// ---------------------------------------------------------------
// the base run-time civilian struct
typedef struct AICivilian 
{
	S32						iPartitionIdx;
	AICivilianFunctionSet	*fp;
	
	S64						lastUpdateTime;

	U32 forward : 1;
	U32 flaggedForKill : 1;
	U32 pathInitialized : 1;
	U32 bPaused : 1;
	U32 v2PosIsCurrent : 1;
	U32 v2DirIsCurrent : 1;
	U32 bIsKnocked : 1;

	Vec2					v2Pos;
	Vec3					prevPos;
	Vec2					v2Dir;

	EAICivilianType			type;
	EntityRef				myEntRef;

	AICivilianSpawnVolume	*spawnVolume;
	AICivilianDef			*civDef;

	AICivilianPath			path;
	MovementRequester		*requester;

	AICivilianBucket		*entBucket;

	S64						timeatLastDefaultRequester;
} AICivilian;

// Any derived civilian types need to have AICivilian as the first member of the struct
// and be named civBase
#define CIV_BASE(civ)	((civ)->civBase)
#define CIV_BASEPTR(civ)	(&(civ)->civBase)

#define AI_CIV_DEF(civ)		((civ)->civBase.civDef)

typedef enum EBlockType
{
	EBlockType_NONE,
	EBlockType_CAUTION, 
	EBlockType_CLOSE,
	EBlockType_CRITICAL

} EBlockType;

typedef struct AICivilianTypeDefRuntimeInfo
{
	const char *pchCivDefName;			// pooled string

	// Runtime count of the number of civilians of this type. 
	// used for the strict distribution counting
	S32 iCurrentCount;

} AICivilianTypeDefRuntimeInfo;

typedef struct AICivilianTypeRuntimeInfo
{
	AICivilianTypeDefRuntimeInfo **eaCivilianTypes;

	S32 targetCount;

} AICivilianTypeRuntimeInfo;

typedef struct AICivilianRuntimePathInfo
{
	// Pathing Information
	// most of this data can be shared, however there are a few things scattered across these 
	// structures that need to be per-partition. Until memory becomes an issue, this is the quick and dirty way to partition this
	AICivilianPathLeg **legs[EAICivilianLegType_COUNT];
	AICivilianPathPoint **eaPathPoints;
	AICivilianPathPointIntersection **eaPathPointIntersects;
	AICivilianPathIntersection **intersects;
	AICivIntersectionCurve **eaIntersectionCurves;
	StashTable stashIntersectionCurves;

	AICivilianPathInfo *pCivilianPathInfoCopy;
} AICivilianRuntimePathInfo;

typedef struct AICivilianActiveAreas
{
		// for unnamed regions, pointer is not accessed
	WorldRegion *pRegion; 
		 
	const char *pchRegionName;
} AICivilianActiveAreas;

typedef struct AICivilianBucket 
{
	S64 nextUpdate;
	AICivilian **civilians;
} AICivilianBucket;

// all the runtime partitionable data
typedef struct AICivilianPartitionState
{
	AICivilian **civilians[EAICivilianType_COUNT];

	AICivilian **eaDeferredCivilianFree;
		
	AICivilianRuntimePathInfo pathInfo;
	
	AICivilianWorldLegGrid *pWorldLegGrid;
	AICivPlayerKillEventManager	*pKillEventManager;
	AICivPOIManager	*pPOIManager;

	AICivIntersectionManager *pIntersectionManager;
	AICivCrossTrafficManager *pCrossTrafficManager;
	AICivCarBlockManager *pCarBlockManager;
	AICivCrosswalkManager *pCrosswalkManager;
	
	AICivWanderArea		**eaWanderAreas;

	AICivilianActiveAreas	**eaActivePopulationAreas;
	
	AICivilianTypeRuntimeInfo	civTypeRuntimeInfo[EAICivilianType_COUNT];

	AITeam*	pCivTeam;
	GameEvent* pEmoteGameEvent;

	S64 iLastDeadCivUpdateTime;
	S64 iLastActiveAreaCheck;

	S32 iPartitionIndex;
	S32 iNormalCivilianCount;

	AICivilianBucket entBuckets[EAICivilianType_COUNT][30];
} AICivilianPartitionState;

typedef struct AICivilianSharedState
{
	AICivilianPathInfo *pCivPathInfo;

	AIPedestrianOnClickDef *onclick_def;
	AICivilianPathMapInfo *map_info;
	AICivilianSpawnVolume **eaSpawnVolumes; 


	REF_TO(AICivilianMapDef) hMapDef;
	AICivilianMapDef *pMapDef;
	U32 mapDefOwned;
	AICivDisplayNameData *pDisplayNameData;

	AICivilian **eaDeferredCivilianFree;

	// leg generation data
	//AICivilianPathLeg **deletedLegs;
	//ACGDelNode **deletedNodes;
	//ACGDelLine **deletedLines;

	// World Cell info
	WorldVolumeEntry **eaCivilianVolumes;
	WorldVolumeEntry **eaCivVolumeForcedLegs;
	WorldVolumeEntry **eaPedestrianWanderVolumes;
	WorldVolumeEntry **eaPlayableVolumes;

	S64		lastDeadCivUpdateTime;
	S32		partialLoad;

	U32 bIsDisabled : 1;
	U32 bCarsDisabled : 1;
	U32 bInit : 1;
	U32 clearingAllData : 1;
	U32 queuedCivReload : 1;
	
} AICivilianSharedState;


// ---------------------------------------------------------------
typedef struct AICivPathSelectionInput
{
	const AICivilianPathLeg *pLeg;
	const AICivilianPathIntersection ***eaMids;
	const AICivilianPathIntersection *pAcpi;
} AICivPathSelectionInput;

// ---------------------------------------------------------------
typedef struct AICivPathSelectionOutput
{
	const AICivilianPathLeg *pLeg;
	const AICivilianPathIntersection *pAcpi;
} AICivPathSelectionOutput;


#define civCopyVec3ToVec2(src,dst)			(((dst)[0] = (src)[0]), ((dst)[1] = (src)[2]))


// ---------------------------------------------------------------
// functions
AICivilianPartitionState* aiCivilian_GetPartitionState(int iPartitionIdx);
AICivilianPartitionState* aiCivilian_GetAnyValidPartition();
void aiCivilian_ClearAllPartitionData();
void aiCivilian_LoadAllPartitionData();
void aiCivilian_LoadCivLegsAndMapDef();
void aiCivilian_UnloadCivLegsAndMapDef();

void aiCivilianAddToBucket(AICivilian *civ);
void aiCivilianRemoveFromBucket(AICivilian *civ);

int aiCivilian_CreateAndInitSharedData();
void aiCivPostProcess();
void aiCivMapDef_Startup();

const char* aiCivilianGetSuffixedFileName(int server, int bin, const char* suffix);

bool aiCivilian_LoadMapDef();
void aiCivMapDef_Free();

const char* aiCivOnClickDef_GetRandomFSM(AIPedestrianOnClickDef *pOnClickDef);

void civRegen();

void aiCivilianBeCheap(Entity *e, AICivilian *civ);
void aiCivilianBeNormal(Entity *e, AICivilian *civ);
void aiCiv_SendQueuedWaypoints(AICivilian *civ);
void aiCivilianPauseMovement(AICivilian *civ, bool on);

void aiCivEditing_ReportCivMapDefUnload();
void acgReloadMap(int iPartitionIdx);

// ---------------------------------------------------------------
// acgLeg
F32 acgPointLegDistSquared(const Vec3 pt, const AICivilianPathLeg *leg, Vec3 leg_pt);
F32 acgLegLegDistSquared(const AICivilianPathLeg *leg, Vec3 leg_pt, const AICivilianPathLeg *other, Vec3 other_pt);
bool acgPointInLeg(const Vec3 vPos, const AICivilianPathLeg *pLeg);
void aiCivLeg_GetLegCornerPoints(const AICivilianPathLeg *leg, S32 bStart, Vec2 vStart, Vec2 vEnd);

void acgSendLeg(Entity *debugger, AICivilianPathLeg *leg, U32 color);
void aiCivilian_HeatmapDumpPartitionLines(AICivilianWorldLegGrid *pWorldLegGrid, gslHeatMapCBHandle *pHandle, char **ppErrorString);
bool aiCivilianHeatmapGatherCivs(gslHeatMapCBHandle *pHandle, char **ppErrorString);

AICivilianPathLeg* aiCivilian_GetLegByPos(AICivilianWorldLegGrid *pWorldLegGrid, EAICivilianType type, const Vec3 pos, Vec3 pvClosestPt);

//  AICivilianPathIntersection
void aiCivIntersection_GetBoundingMidPos(const AICivilianPathIntersection *acpi, Vec3 vMid);


// WorldGrid
void aiCivilian_WorldLegGridGetGridsBoundingBox(AICivilianWorldLegGrid *pWorldLegGrid, S32 x, S32 z, S32 xRun, S32 zRun, Vec3 vMin_out, Vec3 vMax_out);
void aiCivilian_WorldLegGridGetBoundingMin(AICivilianWorldLegGrid *pWorldLegGrid, Vec3 vMin);
void aiCivilian_WorldLegGridGetBoundingSizeAndUnitsPerPixel(Vec3 min, Vec3 max, S32 *unitsPerPixel, bool playable_area);

// utility
bool lineSegLineSeg2dIntersection(const Vec3 l1_st, const Vec3 l1_end, const Vec3 l2_st, const Vec3 l2_end, Vec3 vIsectPos);
bool randomChance(F32 fPercentChance);
void aiCivilian_ClearWaypointList(AICivilian *civ);

void aiCiv_GetCurrentWayPointDirection(Entity *e, AICivilian *pCiv, Vec3 vDir);
bool aiCivilianContinuePath(AICivilian *civ);
void aiCivilianProcessPath(Entity *e, AICivilian *civ, const Vec3 vPos, const Vec3 vFacingDir);
void aiCivilianWaypointFree(AICivilianWaypoint *wp);
void aiCivilianMakeWpPos(const AICivilian *civ, const AICivilianPathLeg *leg, AICivilianWaypoint *wp, int end);
void aiCivMidIntersection_GetLegAndIsect(const AICivilianPathIntersection *mid_acpi, PathLegIntersect **intPli, PathLegIntersect **legPli);
void civContinuePath_GetRandomPath(const AICivPathSelectionInput *pInput, AICivPathSelectionOutput *pOutput);
bool isLegKosherForSpawning(const AICivilianPathLeg *leg, bool bForOrientedType, bool desired_forward);
void aiCivilianCheckReachedWpAndContinue(Entity *e, AICivilian *civ, const Vec3 pos);
void aiCivHeatmapGetBoundingSizeAndUnitsPerPixel(Vec3 min, Vec3 max, S32 *unitsPerPixel, bool playable_area);

void acgPathPoints_GetLeafPathDir(const AICivilianPathPoint *pPathPoint, Vec3 vDir);

bool acgLineLine2dIntersection(const Vec3 l1_pt, const Vec3 l1_dir, const Vec3 l2_pt, const Vec3 l2_dir, Vec3 vIsectPos);

void acgReport_AddLocation(AICivRegenReport *pReport, const Vec3 vPos, const char *pchProblemDescription, S32 bIsAnError, F32 fProblemAreaSize);

void aiCivilianPruneOldWaypoints(AICivilian *civ);
bool aiCivOnClickDef_Validate(AIPedestrianOnClickDef *pDef, const char *pszFilename);
void aiCivOnClickDef_Fixup(AIPedestrianOnClickDef *pDef);

AICivEmoteReactInfo *aiCivEmoteReactDef_FindReactInfo(AICivEmoteReactDef *emoteReactDef, const char *pchEmote);

// wander area
void aiCivWanderArea_RemoveVolume(AICivilianPartitionState *pPartition, WorldVolumeEntry *pEntry);
void aiCivWanderArea_AddVolume(AICivilianPartitionState *pPartition, WorldVolumeEntry *pEntry);
void aiCivWanderArea_Shutdown(AICivilianPartitionState *pPartition);
void aiCivWanderArea_Update(AICivilianPartitionState *pPartition);
void aiCivWanderArea_AddVolumeToEachPartition(WorldVolumeEntry *pEntry);
void aiCivWanderArea_RemoveVolumeFromEachPartition(WorldVolumeEntry *pEntry);

void aiCivWanderArea_AddAllVolumesToPartition(AICivilianPartitionState *pPartition);


void aiCiv_DestroyCivilianTypeRuntimeInfo();
void aiCivilian_CalculateDesiredCivCounts();
void aiCivilian_SetCountForEachPartition(S32 type, S32 count);

// ---------------------------------------------------------------
// Extern'd
extern ParseTable parse_CivilianGenerator[];
#define TYPE_parse_CivilianGenerator CivilianGenerator
extern ParseTable parse_CivilianPathBox[];
#define TYPE_parse_CivilianPathBox CivilianPathBox

extern AICivilianSharedState g_civSharedState;

extern EntityRef aiCivDebugRef;
extern int g_bAICivVerbose;

typedef bool (*fpUpdateFunc)(S32 iPartitionIdx, void *data);

// Utility for throttling the updating by max update count
// TODO: create one that throttles by CPU time
S32 eaForEachPartial(S32 iPartitionIdx, EArrayHandle list, S32 aiCurIdx, S32 iMaxUpdates, fpUpdateFunc func);

void aiCivilian_DestroyRuntimePathInfo(AICivilianRuntimePathInfo *pRTPathInfo);


#include "aiCivilianPrivate.inl"