/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "PowersMovement.h"

#include "dynFxInfo.h"
#include "../../WorldLib/AutoGen/dynFxInfo_h_ast.h"
#include "EntityLib.h"
#include "EntityMovementDefault.h"
#include "EntityMovementDragon.h"
#include "EntityMovementFlight.h"
#include "EntityMovementFx.h"
#include "EntityMovementManager.h"
#include "EntityMovementProjectile.h"
#include "EntityMovementSwing.h"
#include "EntityMovementTactical.h"
#include "estring.h"
#include "file.h"
#include "logging.h"
#include "Player.h"
#include "CostumeCommon.h"
#include "CostumeCommonEntity.h"
#include "AutoGen/PowersMovement_c_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "TriCube/vec.h"
#include "ThreadSafeMemoryPool.h"
#include "dynRagdoll.h"
#include "wlCostume.h"
#include "wlSkelInfo.h"

#include "Character.h"
#include "Character_Target.h"
#include "CharacterClass.h"
#include "CombatCallbacks.h"
#include "ControlScheme.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowerAnimFX_h_ast.h"
#include "dynAnimGraph.h"
#include "qsortG.h"

#if GAMESERVER
	#include "aiMovement.h"
	#include "aiLib.h"
	#include "gslNamedPoint.h"
	#include "gslEntity.h"
	#include "RegionRules.h"
#elif GAMECLIENT
	#include "RegionRules.h"
#endif

#include "aiConfig.h"
#include "aiStruct.h"
#include "CombatConfig.h"
#include "stringcache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static int bUseRagdollForKnockBack = -1; 
static int bUseRagdollOnClient = 1;

AUTO_COMMAND ACMD_CATEGORY(dynAnimation) ACMD_SERVERCMD;
void enableRagdoll(int i)
{
	bUseRagdollForKnockBack = i;
#ifdef GAMECLIENT
	ServerCmd_enableRagdoll(bUseRagdollForKnockBack);
#endif
}

AUTO_COMMAND ACMD_CATEGORY(dynAnimation) ACMD_COMMANDLINE ACMD_NAME(Ragdoll) ACMD_SERVERCMD;
void enableRagdollCommandline(int i)
{
	bUseRagdollForKnockBack = i;
}

AUTO_COMMAND ACMD_CATEGORY(dynAnimation) ACMD_SERVERCMD;
void danimEnableClientRagdoll(int i)
{
	bUseRagdollOnClient = i;
}

//Toggle switch for making FX hit at capsule collision
static bool bFXCollision = false;

AUTO_COMMAND ACMD_NAME(pmToggleFXCollision);
void pmToggleFXCollision(void)
{
	bFXCollision = !bFXCollision;
}

// defaults to tracking FlashedFX
static bool s_bTrackFlashedFX = true;

AUTO_COMMAND ACMD_COMMANDLINE;
void PowersTrackFlashedFX(int i)
{
	s_bTrackFlashedFX = !!i;
}


// TODO(JW): Turn fake knockdown back on?
/*#ifdef GAMESERVER
#endif*/

typedef enum PowersDebugPrint {
	PDBG_BITS = 1 << 0,
	PDBG_STICKY = 1 << 1,
	PDBG_FLASH = 1 << 2,
	PDBG_START = 1 << 3,
	PDBG_STOP = 1 << 4,
	PDBG_CANCEL = 1 << 5,
	PDBG_POWERS = 1 << 6,
	PDBG_STATE = 1 << 7,			// Death/Root/Etc
	PDBG_IGIN = 1 << 8,
	PDBG_FX = 1 << 9,
	PDBG_OTHER = 1 << 10,
	PDBG_EVENT = 1 << 11,
} PowersDebugPrint;
static int s_iPMDebug = 0;
static int s_iPMDebugType = 0;//PDBG_BITS|PDBG_START|PDBG_STOP|PDBG_FLASH;
#define PMprintf(level,type,fmt,...) if(level<=s_iPMDebug && (((type) & s_iPMDebugType)==(type))) printf(fmt, ##__VA_ARGS__)

#define DEF_ENUM(enum) {#enum, enum}

StaticDefineInt s_dbgtypelist[] = {
	DEFINE_INT	
	DEF_ENUM(PDBG_BITS),
	DEF_ENUM(PDBG_STICKY),
	DEF_ENUM(PDBG_FLASH),
	DEF_ENUM(PDBG_START),
	DEF_ENUM(PDBG_STOP),
	DEF_ENUM(PDBG_POWERS),
	DEF_ENUM(PDBG_STATE),
	DEF_ENUM(PDBG_IGIN),
	DEF_ENUM(PDBG_FX),
	DEF_ENUM(PDBG_OTHER),
	DEF_ENUM(PDBG_EVENT),
	DEFINE_END
};

#undef DEF_ENUM

AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	powersMovementMsgHandler,
											"PowersMovement",
											PowersMovement);

#define PAFXTYPE(type) StaticDefineIntRevLookup(PowerAnimFXTypeEnum,(type))

#if 0

#define pmLog(mr,msg,tag,fmt,...) { \
	Entity *ePMLOG = NULL; \
	mrGetManagerUserPointer(mr,&ePMLOG); \
	mrmGetManagerUserPointerFG(msg,&ePMLOG); \
	if(ePMLOG) { \
		if(entGetType(ePMLOG)==GLOBALTYPE_ENTITYPLAYER) \
        entLog(LOG_MOVEMENT,ePMLOG,tag,fmt,##__VA_ARGS__); \
	} \
	else \
        objLog(LOG_MOVEMENT,GLOBALTYPE_ENTITY,0,"",NULL, "",tag,NULL,fmt,##__VA_ARGS__); \
}

#else

#define pmLog(mr,msg,tag,fmt,...) {}

#endif


//#define pmError(tag,fmt,...) PowersError(fmt,__VA_ARGS__)
//#define pmPrintf(level,type,tag,fmt,...) if(level<=s_iPMDebug && (((type) & s_iPMDebugType)==(type))) { printf("%d %s ",pmTimestamp(0),tag); printf(fmt, __VA_ARGS__); }



// Threadsafe MemoryPools

TSMP_DEFINE(PMBitsStances);
TSMP_DEFINE(PMBits);
TSMP_DEFINE(PMFx);
TSMP_DEFINE(PMReleaseAnim);

AUTO_STRUCT;
typedef struct PMConstantForce
{
	U32			id;
	U32			spcStart;
	U32			spcStop;
	Vec3		vec;
	F32			speed;				
	F32			yawOffset;			AST(NAME("YawOffset"))
	EntityRef	erRepeler;
} PMConstantForce;

AUTO_STRUCT;
typedef struct PMEvent
{
	U32		id;
	U32		time;
	U32		userid;
} PMEvent;

AUTO_STRUCT AST_THREADSAFE_MEMPOOL;
typedef struct PMBitsStances {
	U32				stanceHandles[4];
	U32*			stanceHandlesOverflow;
} PMBitsStances;

// If you add anything to this structure, you must update the appropriate start, cancel, clone and destroy functions
AUTO_STRUCT AST_THREADSAFE_MEMPOOL;
typedef struct PMBits
{
	// Identifying information
	U32		id;
	U32		subid;
	PowerAnimFXType type;
	EntityRef source;

	U32				bitHandles[4];		NO_AST
	const char*		apchBits[4];		AST(POOL_STRING AUTO_INDEX(apchBits))
	const char**	ppchBitsOverflow;	AST(POOL_STRING)

	U32 timeStart;
	U32 timeStop;
	
	U32 start : 1;
	U32 stop : 1;
	
	U32 stopped : 1;

	U32 flash : 1;
	U32 isKeyword : 1;
	U32 isFlag : 1;
	U32 assumeOwnership : 1;
	U32 forceDetailFlag : 1;

	U32 trigger : 1;
	U32 triggerIsEntityID : 1;
	U32 triggerMultiHit : 1;

	U32 toBG : 1;	// Not sure this is really useful anymore
} PMBits;

#define PMBITSIZE TYPE_ARRAY_SIZE(PMBits, apchBits)
STATIC_ASSERT(TYPE_ARRAY_SIZE(PMBits, apchBits) == PMBITSIZE);
STATIC_ASSERT(TYPE_ARRAY_SIZE(PMBits, bitHandles) == PMBITSIZE);
STATIC_ASSERT(TYPE_ARRAY_SIZE(PMBitsStances, stanceHandles) == PMBITSIZE);

AUTO_STRUCT AST_THREADSAFE_MEMPOOL;
typedef struct PMReleaseAnim {
	U32			spc;
	U32			id;
} PMReleaseAnim;

AUTO_STRUCT AST_THREADSAFE_MEMPOOL;
typedef struct PMFx
{
	U32 id;
	U32 subid;
	PowerAnimFXType type;

	U32 ahFX[6];

	U32 timeStart;
	U32 timeStop;

	const char *apchNames[6]; AST(POOL_STRING AUTO_INDEX(apchNames))
	DynParamBlock *pBlock;
	EntityRef source;
	EntityRef target;
	Vec3 vecSource;
	Vec3 vecTarget;
	Quat quatTarget;
	F32 fHue;
	F32 fRange;
	F32 fArc;
	F32 fYaw;
	PowerAnimNodeSelectionType nodeSelectionType;

	U32 start : 1;
	U32 stop : 1;

	U32 flash : 1;

	U32 miss : 1;

	U32 trigger : 1;
	U32	triggerIsEntityID : 1;
	U32 triggerMultiHit : 1;

	U32 useTargetNode : 1;
	U32 alwaysSelectSameNode : 1;
	U32 doNotTrackFlashed : 1;
} PMFx;

#define PMFXSIZE TYPE_ARRAY_SIZE(PMFx, apchNames)

AUTO_STRUCT;
typedef struct PMHitReact
{
	U32		id;
	U32		subid;
	EntityRef source;

	char **ppchBits;
	const char **ppchFX;
	DynParamBlock *pBlock;
	F32 fHue;

	U32 spcTimeout;
		// If it gets to this time without getting triggered, something has gone wrong

} PMHitReact;


// Used to prevent player-driven movement, usually during power activation, but
//  also for death and crowd control.
//  The "root" from this could be overridden by an intentional movement caused
//  by the same activation?
AUTO_STRUCT;
typedef struct PMIgnore
{
	U32 id;
	PowerAnimFXType type;

	// Timing and tracking
	U32 timeStart;		// When to start
	U32 timeStop;		// When to stop
	U32 start : 1;		// Requesting to be started at timeStart
	U32 stop : 1;		// Requesting to be stopped at timeStop
	U32 cancel : 1;		// Requesting to be canceled immediately
	U32 started : 1;	// Has been started
//	U32 startedLastFrame : 1;	// Flag for the BG to remember to pass the change to the FG
//	U32 stopped : 1;	// Has been stopped

	char *cause;		// Debugging field indicating the cause
} PMIgnore;


// Used to cause bounded movement or rotation, usually during power activation.
// Stopping time is always known in advance, to change any parameters of a move
//  you must make a complete new move with the same id and pass it in.
AUTO_STRUCT;
typedef struct PMMove
{
	U32 id;					// ID of the activation
	PowerMoveType type;		AST(INT) // Type of movement

	// Movement inputs
	F32 fSpeed;				// Speed to move
	EntityRef erTarget;		// If valid, move towards this entity
	Vec3 vecTarget;			// If erTarget wasn't valid, we move towards this point
	Vec3 vMoveDir;
	F32 fMoveYawOffset;		// when the movement direction is calculated, apply this final offset
							// The intended facing direction will not change

	// Timing and tracking
	U32 timeStart;			// When to start
	U32 timeNotify;			// When to notify the fg (used for lunges)
	F32 distNotify;			// When to notify the fg, if reached before timeNotify (used for lunges)
	U32 timeStop;			// When to stop
	F32 distStop;			// When to stop, if reached before timeStop (used for lunges)

	F32 entCollisionCapsuleBuffer;	//Additional buffer around other ent capsules
	F32 lurchTraveled;
	
	// Used for lurch animations
	const char *pchAnimGraphName;
	Vec3 vUsed;					//the amount of distance already applied between lurch anim. keys
	Vec3 vRemainder;			//the amount of distance that still needs to be applied between lurch anim. keys
	S32 iRemainderStartFrame;	//the lower key for vUsed & vRemainder
	S32 iRemainderEndFrame;		//the upper key for vUsed & vRemainder

	U32 bDefaultDir : 1;			// If er and vec weren't valid, we move in whatever direction we were facing
	U32 started : 1;				// Has been started
	U32 firstLurchTickProcessed : 1;		
	U32 faceAway : 1;				// Face away, instead of towards the target
	U32 stopLurching : 1;			// Were listening for entity collision and we found one
	U32 bFaceActivateSticky : 1;	// Face the target during the entire power activation
	U32 bUseVecAsDirection : 1;
	U32 bHit : 1;					// Used in lurch animations to choose which animation to play (default vs hitpause)
	U32 bIgnoreCollision : 1;		// 
	U32 bLungeHorizontal : 1;
} PMMove;

AUTO_STRUCT;
typedef struct PowersMovementLurchUpdate
{
	U8 uchActID;
	U32 bHit : 1;
} PowersMovementLurchUpdate;

AUTO_STRUCT;
typedef struct PowersMovementFG 
{
	PMBits			**ppBitsCancels;
	PMBits			**ppBitsUpdates;	
	PMBits			**ppBitsFlashed;
	PMBits			**ppBitsStickied;	
	PMReleaseAnim	**eaReleaseAnim;
	PMFx			**ppFxCancels;
	PMFx			**ppFxUpdates;
	PMFx			**ppFxFlashed;
	PMFx			**ppFxStickied;
	PMConstantForce **ppConstantForces;
	
	PMHitReact	**ppHitReacts;
	PMMove		**ppMove;
	PMIgnore	**ppIgnores;

	PMEvent		**ppEvents;

	Vec3 vecPush;
	U32	 uiTimePush;

	EntityRef erSelectedTarget;

	// Lurch update
	PowersMovementLurchUpdate lurchUpdate;

	U32 bTargetUpdate : 1;
	U32 reset : 1;
} PowersMovementFG;

AUTO_STRUCT;
typedef struct PowersMovementBG 
{
	Vec3					dir;
	bool					hasanim;
	bool					hasposition;
	bool					hasrotation;

	union {
		PMBits**			ppBitsPendingMutable;	
		PMBits*const*const	ppBitsPending;				NO_AST
	};

	union {
		PMBits**				ppBitsStickyMutable;	
		PMBits*const*const		ppBitsSticky;			NO_AST
	};

	union {
		PMReleaseAnim**				eaReleaseAnimMutable;
		PMReleaseAnim*const*const 	eaReleaseAnim;		NO_AST
	};

	union {
		PMFx**					ppFxPendingMutable;
		PMFx*const*const		ppFxPending;			NO_AST
	};

	union {
		PMFx**					ppFxStickyMutable;
		PMFx*const*const		ppFxSticky;				NO_AST
	};
	
	union {
		PMMove**				ppMoveMutable;
		PMMove*const*const		ppMove;					NO_AST
	};

	union {
		PMIgnore**				ppIgnoresMutable;
		PMIgnore*const*const	ppIgnores;				NO_AST
	};

	U32 animStartID;
	Vec3 vecPush;
	U32	 uiTimePush;
	
	EntityRef erSelectedTarget;

	U32 knockDown : 1;

	U32 ignoringInput : 1;	// True if there is an active ignore input
	U32 rooted : 1;			// True if there is an active ignore input of type root or hold
	U32 moving : 1;			// True if there is an active move
	U32 stoppingMove : 1;	// True if there was an active move that just stopped
	U32 assumedOwnership : 1; // True if an anim that assumes ownership was started
} PowersMovementBG;

AUTO_STRUCT;
typedef struct PowersMovementLocalBG 
{
	union {
		PMBits**					ppBitsFlashMutable;
		PMBits*const*const			ppBitsFlash;			NO_AST
	};

	union {
		PMBits**					ppBitsPendingMutable;
		PMBits*const*const			ppBitsPending;			NO_AST
	};

	union {
		PMFx**						ppFxFlashMutable;
		PMFx*const*const			ppFxFlash;				NO_AST
	};

	union {
		PMFx**						ppFxFlashPastMutable;
		PMFx*const*const			ppFxFlashPast;		NO_AST
	};

	union {
		PMFx**						ppFxPendingMutable;
		PMFx*const*const			ppFxPending;			NO_AST
	};

	union {
		PMHitReact**				ppHitReactsMutable;
		PMHitReact*const*const		ppHitReacts;			NO_AST
	};

	union {
		PMBitsStances**				ppBitsStancesMutable;
		PMBitsStances*const*const	ppBitsStances;		NO_AST
	};

	PMConstantForce					**ppConstantForcesQueued;
	PMConstantForce					**ppConstantForcesActive;


} PowersMovementLocalBG;

AUTO_STRUCT;
typedef struct PowersMovementToFG 
{
	PMIgnore		**ppIgnores;
	
	U32 knockDown : 1;

	// Lunge data that should be read by the fg
	U32 idLungeActivate;
	U32 idLungeFinished;
	Vec3 vecLungeFinished;

} PowersMovementToFG;

AUTO_STRUCT;
typedef struct PowersMovementToBG {
	PMBits			**ppBitsCancels;
	PMBits			**ppBitsUpdates;
	PMReleaseAnim	**eaReleaseAnim;
	PMFx			**ppFxCancels;
	PMFx			**ppFxUpdates;
	
	PMHitReact		**ppHitReacts;
	PMMove			**ppMove;
	PMIgnore		**ppIgnores;
	PMConstantForce **ppConstantForces;

	// TODO(RP): There is a potential problem here-
	//	if multiple pushes get activated, uiTimePush will get reset on each push
	//  causing potential problems if multiple repels get added in within several frames of one another.
	Vec3 vecPush;
	U32	 uiTimePush;

	EntityRef erSelectedTarget;

	PowersMovementLurchUpdate lurchUpdate;

	U32 bTargetUpdate : 1;
	U32 reset : 1;
} PowersMovementToBG;

AUTO_STRUCT;
typedef struct PowersMovementSync {
	int unused;
} PowersMovementSync;

void powersMovementMsgHandler(const MovementRequesterMsg* msg);


static const char *s_pchFlagInterrupt;


AUTO_RUN;
void PowersMovementInit(void)
{
#ifdef GAMESERVER
	TSMP_CREATE(PMBitsStances,100);
	TSMP_CREATE(PMBits,100);
	TSMP_CREATE(PMFx,100);
	TSMP_CREATE(PMReleaseAnim,100);
#else
	TSMP_CREATE(PMBitsStances,5);
	TSMP_CREATE(PMBits,5);
	TSMP_CREATE(PMFx,5);
	TSMP_CREATE(PMReleaseAnim,5);
#endif
	s_pchFlagInterrupt	 = allocAddStaticString("Interrupt");
}



// Utility function to get the fg from the requester
static PowersMovementFG* PMRequesterGetFG(MovementRequester* mr){
	PowersMovementFG* fg;

	if(mrGetFG(mr, powersMovementMsgHandler, &fg)){
		return fg;
	}

	return NULL;
}

static S32 PMCreateToBGCheckProcessCount(const MovementRequesterMsg *msg, U32 uiTime)
{
	if(uiTime <= msg->in.fg.createToBG.spc.last)
	{
		if(uiTime < msg->in.fg.createToBG.spc.first)
		{
			PowersError("Powers Programmer needs to know: PMCreateToBGCheckProcessCount: Time happening in the past");
		}
		return true;
	}
	return false;
}


static void PMPreDestroyFG(PowersMovementFG *fg)
{
	int i;
	// Makes sure there aren't any duplicate structure pointers in the updates arrays
	for(i=eaSize(&fg->ppBitsUpdates)-1; i>=0; i--)
	{
		const PMBits* b = fg->ppBitsUpdates[i];
		if(b->flash)
			eaFindAndRemoveFast(&fg->ppBitsFlashed,b);
		else
			eaFindAndRemoveFast(&fg->ppBitsStickied,b);
	}
	for(i=eaSize(&fg->ppFxUpdates)-1; i>=0; i--)
	{
		const PMFx* fx = fg->ppFxUpdates[i];
		if(fx->flash)
			eaFindAndRemoveFast(&fg->ppFxFlashed,fx);
		else
			eaFindAndRemoveFast(&fg->ppFxStickied,fx);
	}
}


/***** Events *****/


#define PMEventID(pEvent) (pEvent)->userid,(pEvent)->time,(pEvent)->id

static void PMEventPrint(char **estr, const PMEvent *pEvent)
{
	estrConcatf(estr,"Event (%d %d %d)\n",PMEventID(pEvent));
}

static void PMEventPrintArray(char **estr, char *pchName, const PMEvent *const*ppEvent)
{
	int i;
	if(!eaSize(&ppEvent)){
		return;
	}
	estrConcatf(estr,"-= %s =-\n",pchName);
	for(i=0; i<eaSize(&ppEvent); i++)
	{
		PMEventPrint(estr,ppEvent[i]);
	}
}

static void PMEventDestroy(PMEvent **ppEvent)
{
	StructDestroySafe(parse_PMEvent, ppEvent);
}

static void PMEventDestroyUnsafe(PMEvent *pEvent)
{
	PMEventDestroy(&pEvent);
}

static void PMEvent_FG_CREATE_TOBG(	const MovementRequesterMsg* msg, 
									PowersMovementFG *fg)
{
	int i;

	if(eaSize(&fg->ppEvents))
	{
		PERFINFO_AUTO_START_FUNC();

		// Checks for all events
		for(i=eaSize(&fg->ppEvents)-1; i>=0; i--)
		{
			PMEvent *pEvent = fg->ppEvents[i];
			if(PMCreateToBGCheckProcessCount(msg,pEvent->time))
			{
				Entity *pent = NULL;
				mrmLog(msg,NULL,"[PM.Event] Triggered (%d %d %d)\n",PMEventID(pEvent));

				mrmGetManagerUserPointerFG(msg,&pent);
				if(combatcbHandlePMEvent) combatcbHandlePMEvent(pent,pEvent->id,pEvent->userid);

				eaRemoveFast(&fg->ppEvents,i);
				StructDestroySafe(parse_PMEvent,&pEvent);
			}
		}

		PERFINFO_AUTO_STOP();
	}
}

// Creates an event for the given time, passes back out the id
void pmEventCreate(MovementRequester *mr,
				   U32 uiUserID,
				   U32 uiTime,
				   U32 *puiIDOut)
{
	static U32 s_uiEventID = 0;
	PowersMovementFG* fg = PMRequesterGetFG(mr);
	PMEvent *pEvent;

	if (!fg)
	{
		return;
	}

	mrEnableMsgCreateToBG(mr);

	pEvent = StructAlloc(parse_PMEvent);

	pEvent->userid = uiUserID;
	pEvent->time = uiTime;

	s_uiEventID++;
	if(!s_uiEventID) s_uiEventID = 1;
	*puiIDOut = pEvent->id = s_uiEventID;

	eaPush(&fg->ppEvents,pEvent);
	PMprintf(1, PDBG_EVENT,"%d EVENT CREATE (%d %d %d)\n",pmTimestamp(0),PMEventID(pEvent));
	mrLog(mr,NULL,"[PM.Event] Create (%d %d %d)\n",PMEventID(pEvent));
}

/***** End Events *****/



/***** Bits *****/

#define PMBITSID_PARAMS(id,subid,source,type,trigger) id,subid,source,PAFXTYPE(type),((trigger)?" trigger":"")
#define PMBITSID(pBits)	PMBITSID_PARAMS((pBits)->id,(pBits)->subid,(pBits)->source,(pBits)->type,(pBits)->trigger)
#define PMBITSID_FORMAT	"(%d %d 0x%x %s%s)"

static void PMBitsPrint(char **estr, const PMBits *pBits)
{
	int i;
	estrConcatf(estr,"Bits "PMBITSID_FORMAT,PMBITSID(pBits));
	if(pBits->start)
	{
		estrConcatf(estr," Start %d",pBits->timeStart);
	}
	if(pBits->stop)
	{
		estrConcatf(estr," Stop %d",pBits->timeStop);
	}

	estrConcatf(estr," %s (",pBits->flash?"Flash":"Sticky");
	for(i=0; i<PMBITSIZE; i++)
	{
		if(pBits->apchBits[i])
		{
			if(i) estrConcatChar(estr,',');
			estrAppend2(estr,pBits->apchBits[i]);
		}
	}
	for(i=0; i<eaSize(&pBits->ppchBitsOverflow); i++)
	{
		estrConcatChar(estr,',');
		estrAppend2(estr,pBits->ppchBitsOverflow[i]);
	}

	estrAppend2(estr,")\n");
}

static void PMBitsPrintArray(char **estr, char *pchName, const PMBits *const*ppbits)
{
	int i;
	if(!eaSize(&ppbits)){
		return;
	}
	estrConcatf(estr,"-= %s =-\n",pchName);
	for(i=0; i<eaSize(&ppbits); i++)
	{
		PMBitsPrint(estr,ppbits[i]);
	}
}


static PMBits *PMBitsClone(PMBits *pBits)
{
	PMBits *pNew = TSMP_ALLOC(PMBits);

	memcpy_fast(pNew,pBits,sizeof(PMBits));
	pNew->ppchBitsOverflow = NULL;
	eaCopy(&pNew->ppchBitsOverflow,&pBits->ppchBitsOverflow);

	return pNew;
}

static void PMBitsDestroy(PMBits **ppBits)
{
	if(ppBits && *ppBits)
	{
		eaDestroy(&(*ppBits)->ppchBitsOverflow);
		TSMP_FREE(PMBits,(*ppBits));
	}
}

static void PMBitsDestroyUnsafe(PMBits *pBits)
{
	PMBitsDestroy(&pBits);
}


static bool PMBitsCompare(const PMBits *pBitsA, const PMBits *pBitsB)
{
	// Everything must match except id, which considers 0
	//  a wildcard
	if(pBitsA->type==pBitsB->type
		&& pBitsA->source==pBitsB->source
		&& pBitsA->subid==pBitsB->subid
		&& (pBitsA->id==pBitsB->id
			|| !(pBitsA->id && pBitsB->id)))
	{
		return true;
	}
	return false;
}

static PMBits *PMBitsFind(const PMBits *pBits, PMBits*const*ppBitsArray, bool bOnlyStarts)
{
	int i;
	for(i=eaSize(&ppBitsArray)-1; i>=0; i--)
	{
		if(bOnlyStarts && !ppBitsArray[i]->start)
			continue;

		if(PMBitsCompare(pBits,ppBitsArray[i]))
			return ppBitsArray[i];
	}
	return NULL;
}

static void PMBitsStancesDestroy(	const MovementRequesterMsg* msg,
									PMBitsStances** sInOut)
{
	PMBitsStances* s = *sInOut;

	*sInOut = NULL;

	ARRAY_FOREACH_BEGIN(s->stanceHandles, i);
	{
		if(s->stanceHandles[i]){
			mrmAnimStanceDestroyBG(msg, &s->stanceHandles[i]);
		}
	}
	ARRAY_FOREACH_END;

	EARRAY_INT_CONST_FOREACH_BEGIN(s->stanceHandlesOverflow, i, isize);
	{
		mrmAnimStanceDestroyBG(msg, &s->stanceHandlesOverflow[i]);
	}
	EARRAY_FOREACH_END;

	eaiDestroy(&s->stanceHandlesOverflow);

	StructDestroySafe(parse_PMBitsStances, &s);
}

static void PMBitsStancesCreate(const MovementRequesterMsg* msg,
								PMBitsStances** sOut,
								PMBits* b)
{
	PMBitsStances* s = *sOut = StructCreate(parse_PMBitsStances);

	ARRAY_FOREACH_BEGIN(b->apchBits, i);
	{
		if(!b->apchBits[i]){
			if(s->stanceHandles[i]){
				mrmAnimStanceDestroyBG(msg, &s->stanceHandles[i]);
			}
			continue;
		}

		if(!s->stanceHandles[i]){
			if(!b->bitHandles[i]){
				b->bitHandles[i] = mmGetAnimBitHandleByName(b->apchBits[i],0);
			}

			mrmAnimStanceCreateBG(msg, &s->stanceHandles[i], b->bitHandles[i]);
		}
	}
	ARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(b->ppchBitsOverflow, i, isize);
	{
		if(i == eaiSize(&s->stanceHandlesOverflow)){
			U32 handle = 0;
			mrmAnimStanceCreateBG(	msg,
									&handle,
									mmGetAnimBitHandleByName(b->ppchBitsOverflow[i],0));
			eaiPush(&s->stanceHandlesOverflow, handle);
		}
	}
	EARRAY_FOREACH_END;

	while(eaiUSize(&s->stanceHandlesOverflow) > eaUSize(&b->ppchBitsOverflow)){
		U32 handle = eaiPop(&s->stanceHandlesOverflow);

		mrmAnimStanceDestroyBG(msg, &handle);
	}
}

static void PMBitsCancelList(	const MovementRequesterMsg* msg,
								const PMBits*const*ppBitsCancels,
								PMBits ***pppBitsTargets,
								PMBitsStances*** pppStances,
								PMBits ***pppBitsCanceled)
{
	int i;
	for(i=eaSize(&ppBitsCancels)-1; i>=0; i--)
	{
		const PMBits *pBitsCancel = ppBitsCancels[i];
		int j;
		for(j=eaSize(pppBitsTargets)-1; j>=0; j--)
		{
			PMBits *pBitsTarget = (*pppBitsTargets)[j];
			if(PMBitsCompare(pBitsCancel,pBitsTarget))
			{
				if(	gConf.bNewAnimationSystem &&
					pppStances)
				{
					PMBitsStances* s;
					assert(eaSize(pppStances) == eaSize(pppBitsTargets));
					s = eaRemove(pppStances,j);
					PMBitsStancesDestroy(msg, &s);
				}
				gConf.bNewAnimationSystem ?
					eaRemove(pppBitsTargets,j) :
					eaRemoveFast(pppBitsTargets,j);
				eaPush(pppBitsCanceled,pBitsTarget);
			}
		}
	}
}

static void PMBits_FG_CREATE_TOBG(	const MovementRequesterMsg* msg, 
									PowersMovementFG *fg, 
									PowersMovementToBG *toBG,
									S32* updatedToBGOut,
									S32* needsCreateToBGOut)
{
	int i;
	PMBits **ppBitsCanceled = NULL;
	U32 time = pmTimestamp(0);

	PERFINFO_AUTO_START_FUNC();

	// Clear bit updates that made it to BG - because we're not assured a full cycle
	// TODO(JW): Movement: Why do we do this?  Seems like this could cause data loss?
	if(eaSize(&toBG->ppBitsUpdates))
	{
		*updatedToBGOut = 1;
		eaClearFast(&toBG->ppBitsUpdates);
	}

	if(eaSize(&fg->ppBitsCancels))
	{
		pmLog(0,msg,"PM.Bits","Checking toBG %d cancels against %d updates\n",eaSize(&fg->ppBitsCancels),eaSize(&fg->ppBitsUpdates));
	}
	// Apply cancels
	PMBitsCancelList(NULL,fg->ppBitsCancels,&fg->ppBitsUpdates,NULL,&ppBitsCanceled);
	for(i=eaSize(&ppBitsCanceled)-1; i>=0; i--)
	{
		int j = -1;
		PMBits *pBitsCanceled = ppBitsCanceled[i];

		// Clean out the other fg lists
		if(pBitsCanceled->flash)
		{
			j = gConf.bNewAnimationSystem ?
					eaFindAndRemove(&fg->ppBitsFlashed,pBitsCanceled) :
					eaFindAndRemoveFast(&fg->ppBitsFlashed,pBitsCanceled);
		}
		else
		{
			j = gConf.bNewAnimationSystem ?
				eaFindAndRemove(&fg->ppBitsStickied,pBitsCanceled) :
				eaFindAndRemoveFast(&fg->ppBitsStickied,pBitsCanceled);
		}

		if(j<0)
		{
			mrmLog(msg,NULL,"[PM.Bits.Error] Cancel toBG Bits "PMBITSID_FORMAT" not in fg list\n",PMBITSID(pBitsCanceled));
			pmLog(0,msg,"PM.Bits.Error","Cancel toBG Bits "PMBITSID_FORMAT" not in fg list\n",PMBITSID(pBitsCanceled));
			PowersError("Cancel toBG Bits "PMBITSID_FORMAT" not in fg list\n",PMBITSID(pBitsCanceled));
		}
		else
		{
			mrmLog(msg,NULL,"[PM.Bits] Cancel toBG Bits "PMBITSID_FORMAT"\n",PMBITSID(pBitsCanceled));
			pmLog(0,msg,"PM.Bits","Cancel toBG Bits "PMBITSID_FORMAT"\n",PMBITSID(pBitsCanceled));
		}
	}
	PMBitsCancelList(NULL,fg->ppBitsCancels,&fg->ppBitsFlashed,NULL,&ppBitsCanceled);
	PMBitsCancelList(NULL,fg->ppBitsCancels,&fg->ppBitsStickied,NULL,&ppBitsCanceled);
	// Clean up the canceled bits
	eaDestroyEx(&ppBitsCanceled,PMBitsDestroyUnsafe);
	// Move cancels into toBG
	if(eaSize(&fg->ppBitsCancels))
	{
		*updatedToBGOut = 1;
		eaCopy(&toBG->ppBitsCancels,&fg->ppBitsCancels);
		eaClear(&fg->ppBitsCancels);
	}


	// Sends bit updates to BG - walk the array forward to be consistent about ordering
	for(i=0; i<eaSize(&fg->ppBitsUpdates); i++)
	{
		PMBits *pBits = fg->ppBitsUpdates[i];
		PMBits *pBitsToBG;
		U32 uiTime = pBits->start ? pBits->timeStart : pBits->timeStop;

		devassert(pBits->toBG);

		if(PMCreateToBGCheckProcessCount(msg,uiTime))
		{
			// Check for a pre-existing update.  If this is a start, only look for pre-existing start
			//  updates (which generally would be bad), since a pre-existing stop update could mean
			//  we're trying to replace bits.
			pBitsToBG = PMBitsFind(pBits,toBG->ppBitsUpdates,pBits->start);
			if(pBitsToBG)
			{
				// TODO(JW): Movement: Same bits, make sure this is a reasonable update, not 
				//  sure if I should assert when I get an odd update, or just be smart 
				//  about the time - when can this happen?
				if(pBits->start)
				{
					if (pBitsToBG->start)
					{
						ErrorDetailsf("Bits: %s, Type: %s, Start Times: %d, %d",
							pBits->apchBits[0],PAFXTYPE(pBits->type),pBits->timeStart,pBitsToBG->timeStart);
						devassertmsg(0, "pmBits: Found duplicate start in to-BG Bit updates");
					}
					pBitsToBG->start = true;
					pBitsToBG->timeStart = pBits->timeStart;
				}
				if(pBits->stop)
				{
					if (pBitsToBG->stop)
					{
						ErrorDetailsf("Bits: %s, Type: %s, Stop Times: %d, %d",
							pBits->apchBits[0],PAFXTYPE(pBits->type),pBits->timeStop,pBitsToBG->timeStop);
						devassertmsg(0, "pmBits: Found duplicate stop in to-BG Bit updates");
					}
					pBitsToBG->stop = true;
					pBitsToBG->timeStop = pBits->timeStop;
				}
				mrmLog(msg,NULL,"[PM.Bits] Updating fg Bits "PMBITSID_FORMAT" to pre-existing toBG (%s)\n",PMBITSID(pBits),pBits->start?"Start":"Stop");
				pmLog(0,msg,"PM.Bits","Updating fg Bits "PMBITSID_FORMAT" to pre-existing toBG (%s)\n",PMBITSID(pBits),pBits->start?"Start":"Stop");
			}
			else
			{
				PMBits *pCopy = PMBitsClone(pBits);
				mrmLog(msg,NULL,"[PM.Bits] Updating fg Bits "PMBITSID_FORMAT" to new toBG (%s)\n",PMBITSID(pBits),pBits->start?"Start":"Stop");
				pmLog(0,msg,"PM.Bits","Updating fg Bits "PMBITSID_FORMAT" to new toBG (%s)\n",PMBITSID(pBits),pBits->start?"Start":"Stop");
				eaPush(&toBG->ppBitsUpdates, pCopy);
				*updatedToBGOut = 1;
			}

			pBits->toBG = 0;
			if(pBits->stop)
			{
				// Remove from sticky just to be safe
				gConf.bNewAnimationSystem ?
					eaFindAndRemove(&fg->ppBitsStickied,pBits) :
					eaFindAndRemoveFast(&fg->ppBitsStickied,pBits);
				// Destroy
				StructDestroy(parse_PMBits,pBits);
			}
			eaRemove(&fg->ppBitsUpdates,i);
			i--;
		}
		else
		{
			*needsCreateToBGOut = 1;
		}
	}

	// Clear any flash bits older than 5s
	for(i=eaSize(&fg->ppBitsFlashed)-1; i>=0; i--)
	{
		U32 timeClear = pmTimestampFrom(fg->ppBitsFlashed[i]->timeStart,5.0f);
		if(timeClear < time)
		{
			//Remove from ppBitsUpdates to be safe
			gConf.bNewAnimationSystem ?
				eaFindAndRemove(&fg->ppBitsUpdates,fg->ppBitsFlashed[i]) :
				eaFindAndRemoveFast(&fg->ppBitsUpdates,fg->ppBitsFlashed[i]);
			PMBitsDestroy(&fg->ppBitsFlashed[i]);
			gConf.bNewAnimationSystem ?
				eaRemove(&fg->ppBitsFlashed,i) :
				eaRemoveFast(&fg->ppBitsFlashed,i);
		}
		else
		{
			*needsCreateToBGOut = 1;
		}
	}

	if(eaSize(&fg->eaReleaseAnim)){
		SWAPP(fg->eaReleaseAnim, toBG->eaReleaseAnim);
		assert(!eaSize(&fg->eaReleaseAnim));
		*updatedToBGOut = 1;
	}

	PERFINFO_AUTO_STOP();
}

static void PMBits_BG_HANDLE_UPDATED_TOBG(	const MovementRequesterMsg* msg, 
											PowersMovementToBG *toBG,
											PowersMovementBG *bg,
											PowersMovementLocalBG *localBG)
{
	PMBits **ppBitsCanceled = NULL;
	bool bRepredict = mmIsRepredictingBG();

	// Apply cancels
	if(eaSize(&toBG->ppBitsCancels))
	{
		pmLog(0,msg,"PM.Bits","Checking bg %d cancels against %d pending %d sticky %d local pending %d flash\n",eaSize(&toBG->ppBitsCancels),eaSize(&bg->ppBitsPending),eaSize(&bg->ppBitsSticky),eaSize(&localBG->ppBitsPending),eaSize(&localBG->ppBitsFlash));
		PMBitsCancelList(NULL,toBG->ppBitsCancels,&bg->ppBitsPendingMutable,NULL,&ppBitsCanceled);
		PMBitsCancelList(msg,toBG->ppBitsCancels,&bg->ppBitsStickyMutable,&localBG->ppBitsStancesMutable,&ppBitsCanceled);
		PMBitsCancelList(NULL,toBG->ppBitsCancels,&localBG->ppBitsPendingMutable,NULL,&ppBitsCanceled);
		// TODO(JW): Bits: Does canceled bits in the flash list make sense?
		PMBitsCancelList(NULL,toBG->ppBitsCancels,&localBG->ppBitsFlashMutable,NULL,&ppBitsCanceled);

		EARRAY_FOREACH_REVERSE_BEGIN(ppBitsCanceled, i);
		{
			mrmLog(msg,NULL,"[PM.Bits] Cancel bg Bits "PMBITSID_FORMAT"\n",PMBITSID(ppBitsCanceled[i]));
			pmLog(0,msg,"PM.Bits","Cancel bg Bits "PMBITSID_FORMAT"\n",PMBITSID(ppBitsCanceled[i]));
		}
		EARRAY_FOREACH_END;

		// Clean up the canceled bits
		eaDestroyEx(&ppBitsCanceled,PMBitsDestroyUnsafe);
		eaDestroyEx(&toBG->ppBitsCancels,PMBitsDestroyUnsafe);
	}

	// Apply bits updates starts to BG
	EARRAY_CONST_FOREACH_BEGIN(toBG->ppBitsUpdates, i, isize);
	{
		bool bPushed = false;
		PMBits *pBits = toBG->ppBitsUpdates[i];

		if(pBits->start)
		{
			// Start updates just move the struct over to the bg, but we check to see
			//  if it's already there first
			if(pBits->flash)
			{
				if(PMBitsFind(pBits,localBG->ppBitsPending,false))
				{
					if(!bRepredict)
					{
						mrmLog(msg,NULL,"[PM.Bits.Error] Updating toBG Bits "PMBITSID_FORMAT" already pending\n",PMBITSID(pBits));
						pmLog(0,msg,"PM.Bits.Error","Updating toBG Bits "PMBITSID_FORMAT" already pending\n",PMBITSID(pBits));
						PowersError("Updating toBG Bits "PMBITSID_FORMAT" already pending\n",PMBITSID(pBits));
					}
				}
				else if(PMBitsFind(pBits,localBG->ppBitsFlash,false))
				{
					if(!bRepredict)
					{
						mrmLog(msg,NULL,"[PM.Bits.Error] Updating toBG Bits "PMBITSID_FORMAT" already active\n",PMBITSID(pBits));
						pmLog(0,msg,"PM.Bits.Error","Updating toBG Bits "PMBITSID_FORMAT" already active\n",PMBITSID(pBits));
						PowersError("Updating toBG Bits "PMBITSID_FORMAT" already active\n",PMBITSID(pBits));
					}
				}
				else
				{
					eaPush(&localBG->ppBitsPendingMutable,pBits);
					mrmLog(msg,NULL,"[PM.Bits] Updating toBG Bits "PMBITSID_FORMAT"\n",PMBITSID(pBits));
					pmLog(0,msg,"PM.Bits","Updating toBG Bits "PMBITSID_FORMAT"\n",PMBITSID(pBits));
				}
			}
			else
			{
				if(PMBitsFind(pBits,bg->ppBitsPending,true))
				{
					if(!bRepredict)
					{
						mrmLog(msg,NULL,"[PM.Bits.Error] Updating toBG Bits "PMBITSID_FORMAT" already pending\n",PMBITSID(pBits));
						pmLog(0,msg,"PM.Bits.Error","Updating toBG Bits "PMBITSID_FORMAT" already pending\n",PMBITSID(pBits));
						PowersError("Updating toBG Bits "PMBITSID_FORMAT" already pending\n",PMBITSID(pBits));
					}
				}
				else if(PMBitsFind(pBits,bg->ppBitsSticky,true))
				{
					if(!bRepredict)
					{
						mrmLog(msg,NULL,"[PM.Bits.Error] Updating toBG Bits "PMBITSID_FORMAT" already active\n",PMBITSID(pBits));
						pmLog(0,msg,"PM.Bits.Error","Updating toBG Bits "PMBITSID_FORMAT" already active\n",PMBITSID(pBits));
						PowersError("Updating toBG Bits "PMBITSID_FORMAT" already active\n",PMBITSID(pBits));
					}
				}
				else
				{
					eaPush(&bg->ppBitsPendingMutable,pBits);
					mrmLog(msg,NULL,"[PM.Bits] Updating toBG Bits "PMBITSID_FORMAT"\n",PMBITSID(pBits));
					pmLog(0,msg,"PM.Bits","Updating toBG Bits "PMBITSID_FORMAT"\n",PMBITSID(pBits));
				}
			}

			bPushed = true;
			if(!bRepredict)
				PMprintf(3, PDBG_BITS|PDBG_START, "toBG to bg: Bits "PMBITSID_FORMAT" start %d\n", PMBITSID(pBits), pBits->timeStart);
		}

		if(pBits->stop)
		{
			devassert(!pBits->flash);
			if(bPushed)
			{
				// Start and stop in same package
				mrmLog(msg,NULL,"[PM.Bits] Updating toBG Bits "PMBITSID_FORMAT" stop already pushed\n",PMBITSID(pBits));
				if(!bRepredict)
					PMprintf(3, PDBG_BITS|PDBG_STOP, "toBG to bg: Bits "PMBITSID_FORMAT" stop %d (already pushed)\n", PMBITSID(pBits), pBits->timeStop);
			}
			else
			{
				int j,t;
				PMBits *pBitsBG = NULL;

				t = eaSize(&bg->ppBitsPending);
				for(j=0; j<t; j++)
				{
					if(PMBitsCompare(pBits,bg->ppBitsPending[j]))
					{
						if(pBitsBG)
						{
							PowersError("Powers Programmer needs to know: PMBits_BG_HANDLE_UPDATED_TOBG: Stop toBG bits found more than one instance");
							mrmLog(msg,NULL,"[PM.Bits.Error] Updating toBG Bits "PMBITSID_FORMAT" more than one instance\n",PMBITSID(pBits));
						}
						pBitsBG = bg->ppBitsPending[j];
						if(pBitsBG->stop)
						{
							mrmLog(msg,NULL,"[PM.Bits] Updating toBG Bits "PMBITSID_FORMAT" stop pending "PMBITSID_FORMAT" %d\n",PMBITSID(pBits),PMBITSID(pBitsBG),pBitsBG->timeStop);
							pBitsBG->timeStop = MIN(pBitsBG->timeStop,pBits->timeStop);
						}
						else
						{
							mrmLog(msg,NULL,"[PM.Bits] Updating toBG Bits "PMBITSID_FORMAT" stop pending\n",PMBITSID(pBits));
							pBitsBG->stop = true;
							pBitsBG->timeStop = pBits->timeStop;
						}
					}
				}

				t = eaSize(&bg->ppBitsSticky);
				for(j=0; j<t; j++)
				{
					if(PMBitsCompare(pBits,bg->ppBitsSticky[j]))
					{
						if(pBitsBG)
						{
							PowersError("Powers Programmer needs to know: PMBits_BG_HANDLE_UPDATED_TOBG: Stop toBG bits found more than one instance");
							mrmLog(msg,NULL,"[PM.Bits.Error] Updating toBG Bits "PMBITSID_FORMAT" more than one instance\n",PMBITSID(pBits));
						}
						pBitsBG = bg->ppBitsSticky[j];
						if(pBitsBG->stop)
						{
							mrmLog(msg,NULL,"[PM.Bits] Updating toBG Bits "PMBITSID_FORMAT" stop active "PMBITSID_FORMAT" %d\n",PMBITSID(pBits),PMBITSID(pBitsBG),pBitsBG->timeStop);
							pBitsBG->timeStop = MIN(pBitsBG->timeStop,pBits->timeStop);
						}
						else
						{
							mrmLog(msg,NULL,"[PM.Bits] Updating toBG Bits "PMBITSID_FORMAT" stop active\n",PMBITSID(pBits));
							pBitsBG->stop = true;
							pBitsBG->timeStop = pBits->timeStop;
						}
					}
				}

				if(!bRepredict)
					PMprintf(3, PDBG_BITS|PDBG_STOP, "toBG to bg: Bits "PMBITSID_FORMAT" stop %d\n", PMBITSID(pBits), pBits->timeStop);

				// Get rid of it
				PMBitsDestroy(&pBits);
			}
		}
	}
	EARRAY_FOREACH_END;
	
	eaClear(&toBG->ppBitsUpdates);

	eaPushEArray(&bg->eaReleaseAnimMutable, &toBG->eaReleaseAnim);
	eaClear(&toBG->eaReleaseAnim);
}

static S32 PMBits_IsHitReact(const PMBits* pBits){
	return	pBits->type==kPowerAnimFXType_HitFlash ||
			pBits->type == kPowerAnimFXType_HitFlag;
}

static void PMBits_BG_DISCUSS_DATA_OWNERSHIP(const MovementRequesterMsg* msg, 
											 PowersMovementBG *bg,
											 PowersMovementLocalBG *localBG,
											 bool* pbHasBitsOut)
{
	int i;

	// Move all bg pending bits into active lists if they're ready to start
	for(i=0; i<eaSize(&bg->ppBitsPending); i++)
	{
		PMBits *pBits = bg->ppBitsPending[i];
		devassert(!pBits->flash && pBits->start);	// No flash or non-start bits in our bg pending list, right?
		if(mrmProcessCountHasPassedBG(msg, pBits->timeStart))
		{
			PMBits *pBitsSticky = PMBitsFind(pBits,bg->ppBitsSticky,true);
			devassert(pBitsSticky==NULL);
			pBits->start = 0;
			eaRemove(&bg->ppBitsPendingMutable,i);
			i--;
			if(gConf.bNewAnimationSystem){
				PMBitsStances* s;
				assert(eaSize(&bg->ppBitsSticky) == eaSize(&localBG->ppBitsStances));
				PMBitsStancesCreate(msg, &s, pBits);
				eaPush(&localBG->ppBitsStancesMutable, s);
			}
			eaPush(&bg->ppBitsStickyMutable,pBits);
			mrmLog(msg,NULL,"[PM.Bits] Updating bg Bits "PMBITSID_FORMAT" sticky started\n",PMBITSID(pBits));
			if(!mmIsRepredictingBG())
				PMprintf(2,PDBG_BITS|PDBG_STICKY," Sticky bits added to list %p: %p %d %d\n",bg->ppBitsPending, pBits, pBits->timeStart, pBits->id);
		}
	}

	// Check all the currently sticky bits for stops - can do this in reverse
	for(i=eaSize(&bg->ppBitsSticky)-1; i>=0; i--)
	{
		PMBits *pBits = bg->ppBitsSticky[i];
		devassert(!pBits->flash);	// No flash bits in our bg sticky list, right?
		if(pBits->stop && mrmProcessCountHasPassedBG(msg, pBits->timeStop))
		{
			mrmLog(msg,NULL,"[PM.Bits] Updating bg Bits "PMBITSID_FORMAT" sticky stopped\n",PMBITSID(pBits));
			if(gConf.bNewAnimationSystem){
				PMBitsStances* s;
				assert(eaSize(&bg->ppBitsSticky) == eaSize(&localBG->ppBitsStances));
				s = eaRemove(&localBG->ppBitsStancesMutable,i);
				PMBitsStancesDestroy(msg, &s);
			}
			gConf.bNewAnimationSystem ?
				eaRemove(&bg->ppBitsStickyMutable,i) :
				eaRemoveFast(&bg->ppBitsStickyMutable,i);
			PMBitsDestroy(&pBits);
		}
	}

	// Clean up the old flash bits
	if(!gConf.bNewAnimationSystem &&
		eaSize(&localBG->ppBitsFlash))
	{
		eaDestroyEx(&localBG->ppBitsFlashMutable,PMBitsDestroyUnsafe);
	}

	// Move all local bg pending bits into active lists if they're ready to start
	for(i=0; i<eaSize(&localBG->ppBitsPending); i++)
	{
		PMBits *pBits = localBG->ppBitsPending[i];
		devassert(pBits->flash && pBits->start);	// All starting flash bits
		if(mrmProcessCountHasPassedBG(msg, pBits->timeStart))
		{
			PMBits *pBitsFlash = PMBitsFind(pBits,localBG->ppBitsFlash,false);
			devassert(pBitsFlash==NULL);
			pBits->start = 0;
			eaRemove(&localBG->ppBitsPendingMutable,i);
			i--;
			eaPush(&localBG->ppBitsFlashMutable,pBits);
			if(	gConf.bNewAnimationSystem &&
				!PMBits_IsHitReact(pBits))
			{
				*pbHasBitsOut = 1;
			}
			mrmLog(msg,NULL,"[PM.Bits] Updating localBG Bits "PMBITSID_FORMAT" flash started\n",PMBITSID(pBits));
			if(!mmIsRepredictingBG())
				PMprintf(2, PDBG_BITS|PDBG_FLASH," Flash bits added to list: %d\n",pBits->timeStart);
		}
	}
}

static void PMBits_BG_DISCUSS_RELEASING_DATA_OWNERSHIP(	const MovementRequesterMsg* msg, 
														PowersMovementBG *bg,
														PowersMovementLocalBG *localBG,
														bool* pbReleaseBitsOut)
{
	EARRAY_FOREACH_REVERSE_BEGIN(bg->eaReleaseAnim, j);
	{
		if(mrmProcessCountHasPassedBG(msg, bg->eaReleaseAnim[j]->spc)){
			PMReleaseAnim* ra = eaRemove(&bg->eaReleaseAnimMutable, j);
			if(ra->id == bg->animStartID){
				bg->assumedOwnership = 0;
			}
			TSMP_FREE(PMReleaseAnim, ra);
		}
	}
	EARRAY_FOREACH_END;

	if (bg->assumedOwnership == 0) {
		*pbReleaseBitsOut = 1;
	}
}


static void PMBitsAddAnimBitsBG(const MovementRequesterMsg* msg,
								PowersMovementBG* bg,
								PMBits *pBits,
								S32 isFlash)
{
	ARRAY_FOREACH_BEGIN(pBits->apchBits, i);
	{
		if(!pBits->apchBits[i])
		{
			break;
		}

		mrmLog(	msg,
				NULL,
				"[PM.Bits] Adding bg Bits "PMBITSID_FORMAT" %s",
				PMBITSID(pBits),
				pBits->apchBits[i]);
		if(!pBits->bitHandles[i]){
			pBits->bitHandles[i] = mmGetAnimBitHandleByName(pBits->apchBits[i],isFlash);
		}
		if(gConf.bNewAnimationSystem){
			if(pBits->isKeyword){
				mrmAnimStartBG(	msg,
								pBits->bitHandles[i],
								pBits->id);
				if (pBits->assumeOwnership) {
					bg->assumedOwnership = 1;
					bg->animStartID = pBits->id;
				}
			}
			else if(pBits->isFlag){
				if (!pBits->forceDetailFlag) {
					mrmAnimPlayFlagBG(	msg,
										pBits->bitHandles[i],
										pBits->assumeOwnership ? pBits->id : 0);
				} else {
					mrmAnimPlayForcedDetailFlagBG(	msg,
													pBits->bitHandles[i],
													pBits->assumeOwnership ? pBits->id : 0);
				}
			}
		}else{
			mrmAnimAddBitBG(msg,pBits->bitHandles[i]);
		}
	}
	ARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(pBits->ppchBitsOverflow, i, isize);
	{
		mrmLog(	msg,
				NULL,
				"[PM.Bits] Adding bg Bits "PMBITSID_FORMAT" %s",
				PMBITSID(pBits),
				pBits->ppchBitsOverflow[i]);
		if(gConf.bNewAnimationSystem){
			if(pBits->isKeyword){
				mrmAnimStartBG(	msg,
								mmGetAnimBitHandleByName(pBits->ppchBitsOverflow[i],isFlash),
								pBits->id);
				if (pBits->assumeOwnership) {
					bg->assumedOwnership = 1;
					bg->animStartID = pBits->id;
				}
			}
			else if(pBits->isFlag){
				if(!pBits->forceDetailFlag){
					mrmAnimPlayFlagBG(	msg,
										mmGetAnimBitHandleByName(pBits->ppchBitsOverflow[i],isFlash),
										pBits->assumeOwnership ? pBits->id : 0);
				} else {
					mrmAnimPlayForcedDetailFlagBG(	msg,
													mmGetAnimBitHandleByName(pBits->ppchBitsOverflow[i],isFlash),
													pBits->assumeOwnership ? pBits->id : 0);
				}
			}
		}else{
			mrmAnimAddBitBG(msg,mmGetAnimBitHandleByName(pBits->ppchBitsOverflow[i],isFlash));
		}
	}
	EARRAY_FOREACH_END;
}

static U32 PM_GetTriggerID(EntityRef er, U32 id, U32 subID, S32 isEntityID)
{
	if(isEntityID)
	{
		return MM_ENTITY_HIT_REACT_ID(er, id);
	}else{
		return ((INDEX_FROM_REFERENCE(er) + 1) << 16) |
				((id & 0xff) << 8) |
				(subID & 0xff);
	}
}

static void PMBitsAddHitReactBG(const MovementRequesterMsg* msg, PMBits *pBits)
{
	int i,s;
	MMRHitReactConstant hrConstant = {0};

	for(i=0; i<PMBITSIZE; i++)
	{
		if(pBits->apchBits[i])
		{
			U32 bitHandle = mmGetAnimBitHandleByName(pBits->apchBits[i],0);
			mrmLog(	msg,
				NULL,
				"[PM.Bits] Adding bg hr Bits "PMBITSID_FORMAT" %s",
				PMBITSID(pBits),
				pBits->apchBits[i]);
			ea32Push(&hrConstant.animBits,bitHandle);
		}
		else
		{
			break;
		}
	}

	s = eaSize(&pBits->ppchBitsOverflow);
	for(i=0; i<s; i++)
	{
		U32 bitHandle = mmGetAnimBitHandleByName(pBits->ppchBitsOverflow[i],0);
		mrmLog(	msg,
				NULL,
				"[PM.Bits] Adding bg hr Bits "PMBITSID_FORMAT" %s",
				PMBITSID(pBits),
				pBits->ppchBitsOverflow[i]);
		ea32Push(&hrConstant.animBits,bitHandle);
	}

	hrConstant.triggerID = PM_GetTriggerID(pBits->source, pBits->id, pBits->subid, pBits->triggerIsEntityID);
	hrConstant.triggerIsEntityID = !!pBits->triggerIsEntityID;

	hrConstant.waitForTrigger = true;

	mmrHitReactCreateBG(msg, NULL, &hrConstant, NULL);
	
	eaiDestroy(&hrConstant.animBits);
}

static void PMBits_BG_CREATE_DETAILS(const MovementRequesterMsg* msg, 
									 PowersMovementBG *bg,
									 PowersMovementLocalBG *localBG)
{
	int i,s;

	if(!gConf.bNewAnimationSystem){
		s = eaSize(&bg->ppBitsSticky);
		for(i=0; i<s; i++)
		{
			PMBitsAddAnimBitsBG(msg,bg,bg->ppBitsSticky[i],0);
		}
	}

	s = eaSize(&localBG->ppBitsFlash);
	for(i=0; i<s; i++)
	{
		PMBits* pBits = localBG->ppBitsFlash[i];

		if(	gConf.bNewAnimationSystem &&
			!PMBits_IsHitReact(pBits))
		{
			continue;
		}

		if(pBits->trigger && PMBits_IsHitReact(pBits))
		{
			PMBitsAddHitReactBG(msg,pBits);
		}
		else
		{
			PMBitsAddAnimBitsBG(msg,bg,pBits,1);
		}

		if (gConf.bNewAnimationSystem) {
			PMBitsDestroy(&localBG->ppBitsFlashMutable[i]);
			eaRemove(&localBG->ppBitsFlashMutable, i);
			s--;
			i--;
		}
	}

}

static void pmBitsStart(MovementRequester* mr, 
						U32 id, U32 subid, PowerAnimFXType type, EntityRef source, 
						U32 time, 
						const char **ppchBits, 
						bool flash,
						bool trigger,
						bool triggerIsEntityID,
						bool triggerMultiHit,
						bool bIsKeyword,
						bool bIsFlag,
						bool bNeverCancel,
						bool bAssumeOwnership,
						bool bForceDetailFlag)
{
	PowersMovementFG* fg = PMRequesterGetFG(mr);

	PERFINFO_AUTO_START_FUNC();

	mrEnableMsgCreateToBG(mr);

	if(!id)
	{
		PowersError("Powers Programmer needs to know: pmBitsStart: id is 0");
	}

	if(time<pmTimestamp(0))
	{
		U32 uiTimeNow = pmTimestamp(0);
		if(!entIsServer() || time+2<uiTimeNow)
		{
			PowersError("Powers Programmer needs to know: pmBitsStart: Negative timestamp");
		}
	}

	if(fg)
	{
		int i,s;
		PMBits *pBits = TSMP_ALLOC(PMBits);

		pBits->id = id;
		pBits->subid = subid;
		pBits->type = type;
		pBits->source = source;
		pBits->timeStart = time;
		pBits->timeStop = 0;
		pBits->start = true;
		pBits->stop = false;
		pBits->stopped = false;
		pBits->flash = !!flash;
		pBits->isKeyword = !!bIsKeyword;
		pBits->isFlag = !!bIsFlag;
		pBits->assumeOwnership = bAssumeOwnership;
		pBits->forceDetailFlag = bForceDetailFlag;
		pBits->trigger = !!trigger;
		pBits->triggerIsEntityID = !!triggerIsEntityID;
		pBits->triggerMultiHit = !!triggerMultiHit;
		pBits->toBG = true;
		pBits->ppchBitsOverflow = NULL;

		s = eaSize(&ppchBits);
		for(i=0; i<PMBITSIZE && i<s; i++)
		{
			pBits->apchBits[i] = ppchBits[i];
			pBits->bitHandles[i] = 0;
		}
		if(i<PMBITSIZE)
		{
			for(; i<PMBITSIZE; i++)
			{
				pBits->apchBits[i] = NULL;
				pBits->bitHandles[i] = 0;
			}
		}
		else
		{
			int dif = s-PMBITSIZE;
			if(dif>0)
			{
				eaSetCapacity(&pBits->ppchBitsOverflow, dif);
				for(; i<s; i++)
				{
					eaPush(&pBits->ppchBitsOverflow, ppchBits[i]);
				}
			}
		}

		if(MRLOG_IS_ENABLED(mr))
		{
			for(i = 0; i < s; i++)
			{
				mrLog(	mr,
						NULL,
						"[PM.Bits] Adding fg Bits "PMBITSID_FORMAT" %u %s \"%s\"",
						PMBITSID(pBits),
						time,
						flash ? "flash" : "sticky",
						ppchBits[i]);
			}
		}

		PMprintf(1,PDBG_BITS|PDBG_START,"%d %s BITS START "PMBITSID_FORMAT" %d %p %s\n",pmTimestamp(0),flash?"FLASH":"STICK",PMBITSID(pBits),time,pBits,ppchBits?ppchBits[0]:"");

		if(PMBitsFind(pBits,fg->ppBitsUpdates,true))
		{
			PowersError("Powers Programmer needs to know: pmBitsStart: Duplicate start found in fg updates");
			mrLog(	mr,
					NULL,
					"[PM.Bits.Error] Duplicate fg Bits "PMBITSID_FORMAT" %s in updates",
					PMBITSID(pBits),
					flash ? "flash" : "sticky");
			PMBitsDestroy(&pBits);
		}
		else
		{
			if(flash)
			{
				if(PMBitsFind(pBits,fg->ppBitsFlashed,false))
				{
					PowersError("Powers Programmer needs to know: pmBitsStart: Duplicate start found in fg flashed");
					mrLog(	mr,
							NULL,
							"[PM.Bits.Error] Duplicate fg Bits "PMBITSID_FORMAT" in flashed",
							PMBITSID(pBits));
					PMBitsDestroy(&pBits);
				}
				else
				{
					if (!bNeverCancel)
					{
						eaPush(&fg->ppBitsFlashed, pBits);
					}
					eaPush(&fg->ppBitsUpdates, pBits);
				}
			}
			else
			{
				if(PMBitsFind(pBits,fg->ppBitsStickied,false))
				{
					PowersError("Powers Programmer needs to know: pmBitsStart: Duplicate start found in fg stickied");
					mrLog(	mr,
							NULL,
							"[PM.Bits.Error] Duplicate fg Bits "PMBITSID_FORMAT" in stickied",
							PMBITSID(pBits));
					PMBitsDestroy(&pBits);
				}
				else
				{
					eaPush(&fg->ppBitsStickied, pBits);
					eaPush(&fg->ppBitsUpdates, pBits);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

void pmBitsStartFlash(	MovementRequester* mr, 
						U32 id, U32 subid, PowerAnimFXType type, EntityRef source, 
						U32 time, 
						const char **ppchBits, 
						bool trigger,
						bool triggerIsEntityID,
						bool triggerMultiHit,
						bool bIsKeyword,
						bool bIsFlag,
						bool bNeverCancel,
						bool bAssumeOwnership,
						bool bForceDetailFlag)
{
	pmBitsStart(mr,
				id,
				subid,
				type,
				source,
				time,
				ppchBits,
				true,
				trigger,
				triggerIsEntityID,
				triggerMultiHit,
				bIsKeyword,
				bIsFlag,
				bNeverCancel,
				bAssumeOwnership,
				bForceDetailFlag);
}

void pmBitsStartSticky(	MovementRequester* mr, 
						U32 id, U32 subid, PowerAnimFXType type, EntityRef source, 
						U32 time, 
						const char **ppchBits, 
						bool trigger,
						bool triggerIsEntityID,
						bool triggerMultiHit)
{
	pmBitsStart(mr,
				id,
				subid,
				type,
				source,
				time,
				ppchBits,
				false,
				trigger,
				triggerIsEntityID,
				triggerMultiHit,
				false,
				false,
				false,
				false,
				false);
}

static bool BitsMatch(	const PMBits *pBits,
						U32 uiID,
						U32 uiSubID,
						PowerAnimFXType eType,
						EntityRef source)
{
	// Standard check
	if(pBits->type==eType
		&& pBits->source==source
		&& pBits->subid==uiSubID
		&& (pBits->id==uiID
			|| !(pBits->id && uiID)))
	{
		return true;
	}
	return false;
}

void pmBitsStop(MovementRequester* mr,
				U32 id,
				U32 subid,
				PowerAnimFXType type,
				EntityRef source,
				U32 time,
				bool bKeep)
{
	PowersMovementFG* fg = PMRequesterGetFG(mr);

	mrLog(	mr,
			NULL,
			"[PM.Bits] "__FUNCTION__": "PMBITSID_FORMAT,
			PMBITSID_PARAMS(id, subid, source, type, 0));

	mrEnableMsgCreateToBG(mr);

	if(time<pmTimestamp(0))
	{
		U32 uiTimeNow = pmTimestamp(0);
		if(!entIsServer() || time+2<uiTimeNow)
		{
			PowersError("Powers Programmer needs to know: pmBitsStop: Negative timestamp");
		}
	}

	if(fg)
	{
		bool bFound = false;
		int i;

		for(i=0; i<eaSize(&fg->ppBitsStickied); i++)
		{
			PMBits *pBits = fg->ppBitsStickied[i];
			if(BitsMatch(pBits,id,subid,type,source))
			{
				int j;
				bFound = true;
				pBits->timeStop = time;
				pBits->stop = 1;

				eaRemove(&fg->ppBitsStickied, i);
				PMprintf(4, PDBG_BITS, "Removing bit fg:started: %p %d\n", pBits, pBits->id);

				if(eaFind(&fg->ppBitsUpdates, pBits)!=-1)
				{
					// We already had this in the update list, must have been a start
					devassert(pBits->start && pBits->toBG);
				}
				else
				{
					// Same pointer wasn't in the list of updates already
					pBits->toBG = 1;
					devassert(pBits->start);
					pBits->start = 0;
					for(j=0; j<eaSize(&fg->ppBitsUpdates); j++)
					{
						// Make sure there isn't some other copy of this in the list already
						devassert(!PMBitsCompare(pBits,fg->ppBitsUpdates[j]));
					}
					PMprintf(4, PDBG_BITS, "Adding bit fg:updates: %p %d\n", pBits, pBits->id);
					eaPush(&fg->ppBitsUpdates, pBits);
				}
				PMprintf(1,PDBG_BITS|PDBG_STOP|PDBG_STICKY,"%d STICK BITS STOP  "PMBITSID_FORMAT" %d %p\n",pmTimestamp(0),PMBITSID(pBits),time,pBits);

				// Find more if id is 0
				if(id!=0)
					break;
				else
					i--;
			}
		}

		if(!bFound)
		{
			PMprintf(1,PDBG_BITS|PDBG_STOP|PDBG_STICKY,"%d STICK BITS STOP FAILED (not started): "PMBITSID_FORMAT"\n",pmTimestamp(0),PMBITSID_PARAMS(id,subid,source,type,0));
		}
	}
}

void pmBitsCancel(MovementRequester* mr, 
				  U32 id, U32 subid, PowerAnimFXType type, EntityRef source)
{
	PowersMovementFG* fg = PMRequesterGetFG(mr);

	mrEnableMsgCreateToBG(mr);

	if(fg)
	{
		PMBits *pBits = TSMP_ALLOC(PMBits);

		pBits->id = id;
		pBits->subid = subid;
		pBits->type = type;
		pBits->source = source;
		pBits->ppchBitsOverflow = NULL;
		// Don't need to clear the other fields, they're not used for matching

		if(PMBitsFind(pBits,fg->ppBitsCancels,false))
		{
			PowersError("Powers Programmer needs to know: pmBitsCancel: Duplicate cancel found in fg cancels");
			mrLog(	mr,
					NULL,
					"[PM.Bits.Error] Duplicate fg Bits "PMBITSID_FORMAT" in cancels",
					PMBITSID(pBits));
			PMBitsDestroy(&pBits);
		}
		else
		{
			eaPush(&fg->ppBitsCancels,pBits);
			PMprintf(1, PDBG_BITS|PDBG_CANCEL,"%d       BITS CANCEL "PMBITSID_FORMAT"\n",pmTimestamp(0),PMBITSID(pBits));
		}
	}
}

void pmReleaseAnim(	MovementRequester* mr,
					U32 spc,
					U32 id,
					const char* reason)
{
	PowersMovementFG*	fg;
	PMReleaseAnim*		ra;

	if(!gConf.bNewAnimationSystem){
		return;
	}
	
	fg = PMRequesterGetFG(mr);

	if(!fg){
		return;
	}

	mrEnableMsgCreateToBG(mr);

	ra = TSMP_ALLOC(PMReleaseAnim);
	ZeroStruct(ra);
	ra->spc = spc;
	ra->id = id;
	eaPush(&fg->eaReleaseAnim, ra);

	mrLog(	mr,
			NULL,
			"Queued ReleaseAnim, spc %u, id %u (%s).",
			spc,
			id,
			reason);
}


/***** End Bits *****/


/***** Fx *****/


#define PMFXID(pFX)		(pFX)->id,(pFX)->subid,(pFX)->source,(pFX)->target,PAFXTYPE((pFX)->type)
#define MPFXID_FORMAT	"(%d %d 0x%x 0x%x %s)"

static void PMFxPrint(char **estr, const PMFx *pfx)
{
	int i;

	estrConcatf(estr,"FX "MPFXID_FORMAT,PMFXID(pfx));
	if(pfx->start)
	{
		estrConcatf(estr," Start %d",pfx->timeStart);
	}
	if(pfx->stop)
	{
		estrConcatf(estr," Stop %d",pfx->timeStop);
	}

	estrAppend2(estr,pfx->flash?" Flash":" Sticky");

	for(i=0; i<PMFXSIZE; i++)
	{
		if(pfx->apchNames[i])
		{
			estrConcatChar(estr,' ');
			estrAppend2(estr,pfx->apchNames[i]);
		}
	}

	if(pfx->source)
	{
		estrConcatf(estr," from %d",pfx->source);
	}
	else
	{
		estrConcatf(estr," from (%.2f %.2f %.2f)",vecParamsXYZ(pfx->vecSource));
	}

	if(pfx->target)
	{
		estrConcatf(estr," to %d",pfx->target);
	}
	else
	{
		estrConcatf(estr," to (%.2f %.2f %.2f)",vecParamsXYZ(pfx->vecTarget));
	}

	estrAppend2(estr,"\n");
}

static void PMFxPrintArray(char **estr, char *pchName, const PMFx *const*ppfx)
{
	int i;
	if(!eaSize(&ppfx)){
		return;
	}
	estrConcatf(estr,"-= %s =-\n",pchName);
	for(i=0; i<eaSize(&ppfx); i++)
	{
		PMFxPrint(estr,ppfx[i]);
	}
}

static PMFx *PMFxClone(PMFx *pFX)
{
	PMFx *pNew = TSMP_ALLOC(PMFx);

	memcpy_fast(pNew,pFX,sizeof(PMFx));
	if(pFX->pBlock)
	{
		pNew->pBlock = dynParamBlockCopy(pFX->pBlock);
	}

	return pNew;
}

static void PMFxDestroy(PMFx **ppFX)
{
	if(ppFX && *ppFX)
	{
		dynParamBlockFree((*ppFX)->pBlock);
		TSMP_FREE(PMFx,(*ppFX));
	}
}

static void PMFxDestroyUnsafe(PMFx *pFX)
{
	PMFxDestroy(&pFX);
}

static bool PMFxCompare(PMFx *pFxA, PMFx *pFxB, bool bTargetCanBeZero)
{
	// Everything must match except id, which considers 0
	//  a wildcard
	if(pFxA->type==pFxB->type
		&& pFxA->source==pFxB->source
		&& ((bTargetCanBeZero && (!pFxA->target || !pFxB->target)) || pFxA->target==pFxB->target)
		&& pFxA->subid==pFxB->subid
		&& (pFxA->id==pFxB->id 
			|| !(pFxA->id && pFxB->id))
//		&& pFxA->apchNames[0]==pFxB->apchNames[0]	// Not sure the names need to match anymore
//		&& pFxA->apchNames[1]==pFxB->apchNames[1]
//		&& pFxA->apchNames[2]==pFxB->apchNames[2]
//		&& pFxA->apchNames[3]==pFxB->apchNames[3]
		)
	{
		return true;
	}
	return false;
}

static PMFx *PMFxFind(PMFx *pFx, PMFx*const*ppFxArray, bool bTargetCanBeZero, bool bOnlyStarts)
{
	int i;
	for(i=eaSize(&ppFxArray)-1; i>=0; i--)
	{
		if(bOnlyStarts && !ppFxArray[i]->start)
			continue;

		if(PMFxCompare(pFx,ppFxArray[i],bTargetCanBeZero))
			return ppFxArray[i];
	}
	return NULL;
}

static void PMFxCancelList(PMFx **ppFxCancels, PMFx ***pppFxTargets, PMFx ***pppFxCanceled)
{
	int i;
	for(i=eaSize(&ppFxCancels)-1; i>=0; i--)
	{
		PMFx *pFxCancel = ppFxCancels[i];
		int j;
		for(j=eaSize(pppFxTargets)-1; j>=0; j--)
		{
			PMFx *pFxTarget = (*pppFxTargets)[j];
			if(PMFxCompare(pFxCancel,pFxTarget,true))
			{
				eaRemoveFast(pppFxTargets,j);
				eaPush(pppFxCanceled,pFxTarget);
			}
		}
	}
}


static void PMFx_FG_CREATE_TOBG(const MovementRequesterMsg* msg, 
								PowersMovementFG *fg, 
								PowersMovementToBG *toBG,
								S32* updatedToBGOut,
								S32* needsCreateToBGOut)
{
	int i;
	U32 time = pmTimestamp(0);
	PMFx **ppFxCanceled = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Clear fx updates that made it to BG - because we're not assured a full cycle
	// TODO(JW): Movement: Why do we do this?  Seems like this could cause data loss?
	if(eaSize(&toBG->ppFxUpdates))
	{
		*updatedToBGOut = 1;
		eaClearFast(&toBG->ppFxUpdates);
	}

	// Apply cancels
	PMFxCancelList(fg->ppFxCancels,&fg->ppFxUpdates,&ppFxCanceled);
	for(i=eaSize(&ppFxCanceled)-1; i>=0; i--)
	{
		int j = -1;
		PMFx *pFxCanceled = ppFxCanceled[i];
		// Clean out the other fg lists
		if(pFxCanceled->flash)
		{
			j = eaFindAndRemoveFast(&fg->ppFxFlashed,pFxCanceled);
		}
		else
		{
			j = eaFindAndRemoveFast(&fg->ppFxStickied,pFxCanceled);
		}
		if(j<0)
		{
			PowersError("Powers Programmer needs to know: PMFx_FG_CREATE_TOBG: Cancel fg fx couldn't find in proper fg list");
			mrmLog(msg,NULL,"[PM.Fx.Error] Cancel toBG Fx "MPFXID_FORMAT" (couldn't find in proper fg list)\n",PMFXID(pFxCanceled));
		}
		else
		{
			mrmLog(msg,NULL,"[PM.Fx] Cancel toBG Fx "MPFXID_FORMAT"\n",PMFXID(pFxCanceled));
		}
	}
	PMFxCancelList(fg->ppFxCancels,&fg->ppFxFlashed,&ppFxCanceled);
	PMFxCancelList(fg->ppFxCancels,&fg->ppFxStickied,&ppFxCanceled);
	// Clean up the canceled fx
	eaDestroyEx(&ppFxCanceled,PMFxDestroyUnsafe);
	// Move cancels into toBG
	if(eaSize(&fg->ppFxCancels))
	{
		*updatedToBGOut = 1;
		eaCopy(&toBG->ppFxCancels,&fg->ppFxCancels);
		eaClear(&fg->ppFxCancels);
	}


	// Sends Fx updates to BG - walk the array forward to be consistent about ordering
	for(i=0; i<eaSize(&fg->ppFxUpdates); i++)
	{
		PMFx *pFx = fg->ppFxUpdates[i];
		PMFx *pFxToBG;
		U32 uiTime = pFx->start ? pFx->timeStart : pFx->timeStop;

		if(PMCreateToBGCheckProcessCount(msg,uiTime))
		{
			// Check for a pre-existing update.  If this is a start, only look for pre-existing start
			//  updates (which generally would be bad), since a pre-existing stop update could mean
			//  we're trying to replace an fx.
			pFxToBG = PMFxFind(pFx,toBG->ppFxUpdates,true,pFx->start);
			if(pFxToBG)
			{
				// TODO(JW): Movement: Same fx, make sure this is a reasonable update, not 
				//  sure if I should assert when I get an odd update, or just be smart 
				//  about the time - when can this happen?
				if(pFx->start)
				{
					if (pFxToBG->start)
					{
						ErrorDetailsf("FX: %s, Type: %s, Start Times: %d, %d",
							pFx->apchNames[0],PAFXTYPE(pFx->type),pFx->timeStart,pFxToBG->timeStart);
						devassertmsgf(0, "pmFx FX: %s, Type: %s: Found duplicate start in to-BG Fx updates", 
										pFx->apchNames[0], PAFXTYPE(pFx->type));
					}
					pFxToBG->start = true;
					pFxToBG->timeStart = pFx->timeStart;
				}
				if(pFx->stop)
				{
					if (pFxToBG->stop)
					{
						ErrorDetailsf("FX: %s, Type: %s, Stop Times: %d, %d",
							pFx->apchNames[0],PAFXTYPE(pFx->type),pFx->timeStop,pFxToBG->timeStop);
						devassertmsgf(0, "pmFx FX: %s, Type: %s: Found duplicate stop in to-BG Fx updates", 
										pFx->apchNames[0], PAFXTYPE(pFx->type));
					}
					pFxToBG->stop = true;
					pFxToBG->timeStop = pFx->timeStop;
				}
				mrmLog(msg,NULL,"[PM.Fx] Updating fg Fx "MPFXID_FORMAT" to pre-existing toBG (%s)\n",PMFXID(pFx),pFx->start?"Start":"Stop");
			}
			else
			{
				PMFx *pCopy = PMFxClone(pFx);
				mrmLog(msg,NULL,"[PM.Fx] Updating fg Fx "MPFXID_FORMAT" to new toBG (%s)\n",PMFXID(pFx),pFx->start?"Start":"Stop");
				*updatedToBGOut = 1;
				eaPush(&toBG->ppFxUpdates, pCopy);
			}

			if(pFx->stop)
			{
				// Remove from sticky just to be safe
				eaFindAndRemoveFast(&fg->ppFxStickied,pFx);
				
				// Destroy
				if(pFx->pBlock) {
					dynParamBlockFree(pFx->pBlock);
					pFx->pBlock = NULL;
				}
				StructDestroy(parse_PMFx,pFx);
			}
			eaRemove(&fg->ppFxUpdates,i);
			i--;
		}
		else
		{
			*needsCreateToBGOut = 1;
		}
	}

	// Clear any flash fx older than 5s
	for(i=eaSize(&fg->ppFxFlashed)-1; i>=0; i--)
	{
		U32 timeClear = pmTimestampFrom(fg->ppFxFlashed[i]->timeStart,5.0f);
		if(timeClear < time)
		{
			//Remove from ppFxUpdates to be safe
			eaFindAndRemoveFast(&fg->ppFxUpdates, fg->ppFxFlashed[i]);
			PMFxDestroy(&fg->ppFxFlashed[i]);
			eaRemoveFast(&fg->ppFxFlashed,i);
		}
		else
		{
			*needsCreateToBGOut = 1;
		}
	}

	PERFINFO_AUTO_STOP();
}


static void PMFx_BG_HANDLE_UPDATED_TOBG(const MovementRequesterMsg* msg, 
										PowersMovementToBG *toBG,
										PowersMovementBG *bg,
										PowersMovementLocalBG *localBG)
{
	int i,s;
	bool bRepredict = mmIsRepredictingBG();
	PMFx **ppFxCanceled = NULL;

	// Apply cancels
	if(eaSize(&toBG->ppFxCancels))
	{
		PMFxCancelList(toBG->ppFxCancels,&bg->ppFxPendingMutable,&ppFxCanceled);
		PMFxCancelList(toBG->ppFxCancels,&bg->ppFxStickyMutable,&ppFxCanceled);
		PMFxCancelList(toBG->ppFxCancels,&localBG->ppFxPendingMutable,&ppFxCanceled);
		PMFxCancelList(toBG->ppFxCancels,&localBG->ppFxFlashMutable,&ppFxCanceled);
		PMFxCancelList(toBG->ppFxCancels,&localBG->ppFxFlashPastMutable,&ppFxCanceled);

		for(i=eaSize(&ppFxCanceled)-1; i>=0; i--)
		{
			PMFx *pFxCanceled = ppFxCanceled[i];
			// Anything with a handle needs to be destroyed via the mm
			if(pFxCanceled->ahFX[0])
			{
				int j;
				for(j=0; j<PMFXSIZE; j++)
				{
					if(pFxCanceled->ahFX[j])
						mmrFxDestroyBG(msg,&pFxCanceled->ahFX[j]);
				}
			}
			mrmLog(msg,NULL,"[PM.Fx] Cancel bg Fx "MPFXID_FORMAT"\n",PMFXID(pFxCanceled));
		}
		// Clean up the canceled fx
		eaDestroyEx(&ppFxCanceled,PMFxDestroyUnsafe);
		eaDestroyEx(&toBG->ppFxCancels,PMFxDestroyUnsafe);
	}

	// Apply fx updates to BG
	s = eaSize(&toBG->ppFxUpdates);
	for(i=0; i<s; i++)
	{
		bool bPushed = false;
		PMFx *pFx = toBG->ppFxUpdates[i];
		if(pFx->start)
		{
			// Start updates just move the struct over to the bg, but we check to see
			//  if it's already there first
			if(pFx->flash)
			{
				if(PMFxFind(pFx,localBG->ppFxPending,false,false))
				{
					if(!bRepredict)
					{
						PowersError("Powers Programmer needs to know: PMFx_BG_HANDLE_UPDATED_TOBG: Start toBG fx already in local pending");
						mrmLog(msg,NULL,"[PM.Fx.Error] Updating toBG Fx "MPFXID_FORMAT" to localBG (localBG already has this pending)\n",PMFXID(pFx));
					}
				}
				else if(PMFxFind(pFx,localBG->ppFxFlash,false,false))
				{
					if(!bRepredict)
					{
						PowersError("Powers Programmer needs to know: PMFx_BG_HANDLE_UPDATED_TOBG: Start toBG fx already in local flash");
						mrmLog(msg,NULL,"[PM.Fx.Error] Updating toBG Fx "MPFXID_FORMAT" to localBG (localBG already has this flashing)\n",PMFXID(pFx));
					}
				}
				else
				{
					eaPush(&localBG->ppFxPendingMutable,pFx);
					mrmLog(msg,NULL,"[PM.Fx] Updating toBG Fx "MPFXID_FORMAT" to localBG (Start flash)\n",PMFXID(pFx));
				}
			}
			else
			{
				if(PMFxFind(pFx,bg->ppFxPending,false,true))
				{
					if(!bRepredict)
					{
						PowersError("Powers Programmer needs to know: PMFx_BG_HANDLE_UPDATED_TOBG: Start toBG fx already in pending");
						mrmLog(msg,NULL,"[PM.Fx.Error] Updating toBG Fx "MPFXID_FORMAT" to bg (bg already has this pending)\n",PMFXID(pFx));
					}
				}
				else if(PMFxFind(pFx,bg->ppFxSticky,false,true))
				{
					if(!bRepredict)
					{
						PowersError("Powers Programmer needs to know: PMFx_BG_HANDLE_UPDATED_TOBG: Start toBG fx already in sticky");
						mrmLog(msg,NULL,"[PM.Fx.Error] Updating toBG Fx "MPFXID_FORMAT" to bg (bg already has this flashing)\n",PMFXID(pFx));
					}
				}
				else
				{
					eaPush(&bg->ppFxPendingMutable,pFx);
					mrmLog(msg,NULL,"[PM.Fx] Updating toBG Fx "MPFXID_FORMAT" to bg (Start sticky)\n",PMFXID(pFx));
				}
			}

			bPushed = true;
			if(!bRepredict)
				PMprintf(3, PDBG_FX|PDBG_START, "toBG to bg: Fx "MPFXID_FORMAT" start %d\n", PMFXID(pFx), pFx->timeStart);
		}

		if(pFx->stop)
		{
			devassert(!pFx->flash);
			if(bPushed)
			{
				// Start and stop in same package
				mrmLog(msg,NULL,"[PM.Fx] Updating toBG Fx "MPFXID_FORMAT" to bg (Stop sticky, already pushed)\n",PMFXID(pFx));
				if(!bRepredict)
					PMprintf(3, PDBG_FX|PDBG_STOP, "toBG to bg: Fx "MPFXID_FORMAT" stop %d (already pushed)\n", PMFXID(pFx), pFx->timeStop);
			}
			else
			{
				int j,t;
				PMFx *pFxBG = NULL;

				t = eaSize(&bg->ppFxPending);
				for(j=0; j<t; j++)
				{
					if(PMFxCompare(pFx,bg->ppFxPending[j],true))
					{
						if(pFxBG)
						{
							PowersError("Powers Programmer needs to know: PMFx_BG_HANDLE_UPDATED_TOBG: Stop toBG fx found more than one instance");
							mrmLog(msg,NULL,"[PM.Fx.Error] Updating toBG fx "MPFXID_FORMAT" to bg stop (bg has more than one instance)\n",PMFXID(pFx));
						}
						pFxBG = bg->ppFxPending[j];
						if(pFxBG->stop)
						{
							mrmLog(msg,NULL,"[PM.Fx] Updating toBG fx "MPFXID_FORMAT" to bg (Stop sticky, stop already pending "MPFXID_FORMAT" stop %d)\n",PMFXID(pFx),PMFXID(pFxBG),pFxBG->timeStop);
							pFxBG->timeStop = MIN(pFxBG->timeStop,pFx->timeStop);
						}
						else
						{
							mrmLog(msg,NULL,"[PM.Fx] Updating toBG fx "MPFXID_FORMAT" to bg (Stop sticky, start pending)\n",PMFXID(pFx));
							pFxBG->stop = true;
							pFxBG->timeStop = pFx->timeStop;
						}
					}
				}

				t = eaSize(&bg->ppFxSticky);
				for(j=0; j<t; j++)
				{
					if(PMFxCompare(pFx,bg->ppFxSticky[j],true))
					{
						if(pFxBG)
						{
							PowersError("Powers Programmer needs to know: PMFx_BG_HANDLE_UPDATED_TOBG: Stop toBG fx found more than one instance");
							mrmLog(msg,NULL,"[PM.Fx.Error] Updating toBG fx "MPFXID_FORMAT" to bg stop (bg has more than one instance)\n",PMFXID(pFx));
						}
						pFxBG = bg->ppFxSticky[j];
						if(pFxBG->stop)
						{
							mrmLog(msg,NULL,"[PM.Fx] Updating toBG fx "MPFXID_FORMAT" to bg (Stop sticky, stop already sticky "MPFXID_FORMAT" stop %d)\n",PMFXID(pFx),PMFXID(pFxBG),pFxBG->timeStop);
							pFxBG->timeStop = MIN(pFxBG->timeStop,pFx->timeStop);
						}
						else
						{
							mrmLog(msg,NULL,"[PM.Fx] Updating toBG fx "MPFXID_FORMAT" to bg (Stop sticky, start stickied)\n",PMFXID(pFx));
							pFxBG->stop = true;
							pFxBG->timeStop = pFx->timeStop;
						}
					}
				}

				if(!bRepredict)
					PMprintf(3, PDBG_FX|PDBG_STOP, "toBG to bg: Fx "MPFXID_FORMAT" stop %d\n", PMFXID(pFx), pFx->timeStop);

				// Get rid of it
				PMFxDestroy(&pFx);
			}
		}
	}

	eaClear(&toBG->ppFxUpdates);
}

static bool PMFx_BG_DISCUSS_DATA_OWNERSHIP(const MovementRequesterMsg* msg, 
										   PowersMovementBG *bg,
										   PowersMovementLocalBG *localBG)
{
	int i;
	bool bHasFx = false;

	// Move all bg pending fx into active lists if they're ready to start
	for(i=0; i<eaSize(&bg->ppFxPending); i++)
	{
		PMFx *pFx = bg->ppFxPending[i];
		devassert(!pFx->flash && pFx->start);	// No flash or non-start fx in our bg pending list, right?
		if(mrmProcessCountHasPassedBG(msg, pFx->timeStart))
		{
			PMFx *pFxSticky = PMFxFind(pFx,bg->ppFxSticky,false,true);
			devassert(pFxSticky==NULL);
			pFx->start = 0;
			eaRemove(&bg->ppFxPendingMutable,i);
			i--;
			eaPush(&bg->ppFxStickyMutable,pFx);
			mrmLog(msg,NULL,"[PM.Fx] Updating bg Fx "MPFXID_FORMAT" sticky started\n",PMFXID(pFx));
			bHasFx = true;
		}
	}

	// Check all the currently sticky fx for stops - can do this in reverse
	for(i=eaSize(&bg->ppFxSticky)-1; i>=0; i--)
	{
		PMFx *pFx = bg->ppFxSticky[i];
		devassert(!pFx->flash);	// No flash fx in our bg sticky list, right?
		if(pFx->stop && mrmProcessCountHasPassedBG(msg, pFx->timeStop))
		{
			int j;
			for(j=0; j<PMFXSIZE; j++)
			{
				mmrFxDestroyBG(msg, &pFx->ahFX[j]);
			}
			mrmLog(msg,NULL,"[PM.Fx] Updating bg Fx "MPFXID_FORMAT" sticky stopped\n",PMFXID(pFx));
			eaRemoveFast(&bg->ppFxStickyMutable,i);
			PMFxDestroy(&pFx);
		}
		else
		{
			bHasFx = true;
		}
	}

	// clean out any past flashFX older than 4s
	for(i=eaSize(&localBG->ppFxFlashPast)-1; i>=0; i--)
	{
		PMFx *pFx = localBG->ppFxFlashPast[i];
		if(mrmProcessCountPlusSecondsHasPassedBG(msg, pFx->timeStart, 4.f))
		{
			int j;
			for(j=0; j<PMFXSIZE; j++)
			{
				if (pFx->ahFX[j])
				{
					mmrFxClearBG(msg, &pFx->ahFX[j]);
				}
				else break;
			}
			
			eaRemoveFast(&localBG->ppFxFlashPastMutable, i);
			PMFxDestroyUnsafe(pFx);
		}
	}
	

	// Clean up the old flash fx
	if(eaSize(&localBG->ppFxFlash))
	{
		// add these FX to the past list 
		if (s_bTrackFlashedFX)
		{
			eaPushEArray(&localBG->ppFxFlashPastMutable, &localBG->ppFxFlashMutable);
			eaClear(&localBG->ppFxFlashMutable);
		}
		else
		{
			eaDestroyEx(&localBG->ppFxFlashMutable,PMFxDestroyUnsafe);
		}
	}

	// Move all local bg pending fx into active lists if they're ready to start
	for(i=0; i<eaSize(&localBG->ppFxPending); i++)
	{
		PMFx *pFx = localBG->ppFxPending[i];
		devassert(pFx->flash && pFx->start);	// All starting flash fx
		if(mrmProcessCountHasPassedBG(msg, pFx->timeStart))
		{
			PMFx *pFxFlash = PMFxFind(pFx,localBG->ppFxFlash,false,false);
			devassert(pFxFlash==NULL);
			pFx->start = 0;
			eaRemove(&localBG->ppFxPendingMutable,i);
			i--;
			eaPush(&localBG->ppFxFlashMutable,pFx);
			mrmLog(msg,NULL,"[PM.Fx] Updating localBG Fx "MPFXID_FORMAT" flash started\n",PMFXID(pFx));
			bHasFx = true;
		}
	}

	return bHasFx;
}

static void PMFx_SetTrigger(PMFx* pFx,
							MMRFxConstant* fxConstant)
{
	if(	pFx->type == kPowerAnimFXType_HitFlash ||
		pFx->type == kPowerAnimFXType_HitFlag)
	{
		// pfx->target is the source of the attack.
		
		fxConstant->triggerID = PM_GetTriggerID(pFx->target, pFx->id, pFx->subid, pFx->triggerIsEntityID);
		fxConstant->triggerIsEntityID = !!pFx->triggerIsEntityID;

		fxConstant->waitForTrigger = 1;
	}
	else if(pFx->type == kPowerAnimFXType_ActivateFlash ||
			pFx->type == kPowerAnimFXType_ActivateSticky)
	{
		fxConstant->triggerID = PM_GetTriggerID(pFx->source, pFx->id, pFx->subid, pFx->triggerIsEntityID);

		fxConstant->sendTrigger = 1;
	}
}

static void PMFx_BG_CREATE_DETAILS(const MovementRequesterMsg* msg, 
								   PowersMovementBG *bg,
								   PowersMovementLocalBG *localBG)
{
	int i,j,s;

	s = eaSize(&bg->ppFxSticky);
	for(i=0; i<s; i++)
	{
		MMRFxConstant fxConstant = {0};
		MMRFxConstantNP fxConstantNP = {0};
		PMFx *pFx = bg->ppFxSticky[i];
		if(!pFx->ahFX[0])
		{
			mrmLog(	msg,
				NULL,
				"[PM.Fx] Adding bg Fx "MPFXID_FORMAT" sticky",
				PMFXID(pFx));

			fxConstant.pmID = pFx->id;
			fxConstant.pmSubID = pFx->subid;
			fxConstant.pmType = pFx->type;
			//fxConstant.fxName = pFx->pchName;
			fxConstant.erSource = pFx->source;
			fxConstant.erTarget = pFx->target;
			fxConstant.miss = pFx->miss;
			fxConstant.useTargetNode = pFx->useTargetNode;
			fxConstant.nodeSelectionType = pFx->nodeSelectionType;
			fxConstant.alwaysSelectSameNode = pFx->alwaysSelectSameNode;
			fxConstant.fRange = pFx->fRange;
			fxConstant.fArc = pFx->fArc;
			fxConstant.fYaw = pFx->fYaw;
			copyVec3(pFx->vecTarget, fxConstantNP.vecLastKnownTarget);
		
			if(!pFx->target)
			{
				copyVec3(pFx->vecTarget, fxConstant.vecTarget);
				copyQuat(pFx->quatTarget, fxConstant.quatTarget);
			}
			if(!pFx->source)
			{
				copyVec3(pFx->vecSource, fxConstant.vecSource);
				fxConstant.noSourceEnt = 1;
			}
			fxConstantNP.fxParams = pFx->pBlock;
			fxConstant.fHue = pFx->fHue;

			if(pFx->trigger)
			{
				PMFx_SetTrigger(pFx, &fxConstant);
			}

			for(j=0; j<PMFXSIZE; j++)
			{
				if(!pFx->apchNames[j])
					break;

				fxConstant.fxName = pFx->apchNames[j];
				mmrFxCreateBG(msg, &pFx->ahFX[j], &fxConstant, &fxConstantNP);
			}
		}
	}

	s = eaSize(&localBG->ppFxFlash);
	for(i=0; i<s; i++)
	{
		MMRFxConstant fxConstant = {0};
		MMRFxConstantNP fxConstantNP = {0};
		PMFx *pFx = localBG->ppFxFlash[i];

		mrmLog(	msg,
			NULL,
			"[PM.Fx] Adding bg Fx "MPFXID_FORMAT" flash",
			PMFXID(pFx));

		fxConstant.pmID = pFx->id;
		fxConstant.pmSubID = pFx->subid;
		fxConstant.pmType = pFx->type;
		//fxConstant.fxName = pFx->pchName;
		fxConstant.erSource = pFx->source;
		fxConstant.erTarget = pFx->target;
		fxConstant.miss = pFx->miss;
		fxConstant.useTargetNode = pFx->useTargetNode;
		fxConstant.nodeSelectionType = pFx->nodeSelectionType;
		fxConstant.alwaysSelectSameNode = pFx->alwaysSelectSameNode;
		fxConstant.fRange = pFx->fRange;
		fxConstant.fArc = pFx->fArc;
		fxConstant.fYaw = pFx->fYaw;
		fxConstant.isFlashedFX = true;

		copyVec3(pFx->vecTarget,fxConstantNP.vecLastKnownTarget);
		if(!pFx->target)
		{
			copyVec3(pFx->vecTarget, fxConstant.vecTarget);
			copyQuat(pFx->quatTarget, fxConstant.quatTarget);
		}
		if(!pFx->source)
		{
			copyVec3(pFx->vecSource, fxConstant.vecSource);
			fxConstant.noSourceEnt = 1;
		}
		fxConstantNP.fxParams = pFx->pBlock;
		fxConstant.fHue = pFx->fHue;

		if(pFx->trigger)
		{
			PMFx_SetTrigger(pFx, &fxConstant);
		}

		for(j=0; j<PMFXSIZE; j++)
		{
			U32 *pFXHandle = NULL;
			if(!pFx->apchNames[j])
				break;

			fxConstant.fxName = pFx->apchNames[j];
			if (s_bTrackFlashedFX && !pFx->doNotTrackFlashed)
			{
				pFXHandle = &pFx->ahFX[j];
			}

			mmrFxCreateBG(msg, pFXHandle, &fxConstant, &fxConstantNP);
		}
	}
}

bool pmFxStart(	MovementRequester* mr,
				U32 id, U32 subid, PowerAnimFXType type, EntityRef source, EntityRef target,
				U32 time,
				const char **ppchNames,
				DynParamBlock *params,
				F32 hue,
				F32 fRange,
				F32 fArc,
				F32 fYaw,
				const Vec3 vecSource,
				const Vec3 vecTarget,
				Quat quatTarget,
				EPMFXStartFlags eFXFlags,
				S32 eNodeSelectType)
{
	bool bSuccess = false;
	int i,s;

	PowersMovementFG* fg = PMRequesterGetFG(mr);

	PERFINFO_AUTO_START_FUNC();

	mrEnableMsgCreateToBG(mr);

	if(!id)
	{
		PowersError("Powers Programmer needs to know: pmFxStart: id is 0");
	}


	if(time<pmTimestamp(0))
	{
		U32 uiTimeNow = pmTimestamp(0);
		if(!entIsServer() || time+2<uiTimeNow)
		{
			PowersError("Powers Programmer needs to know: pmFxStart: Negative timestamp");
		}
	}

	s = eaSize(&ppchNames);
	for(i=0; i<s; i++)
	{
		if(!(ppchNames[i] && dynFxInfoExists(ppchNames[i])))
		{
			PowersError("Failed to find valid FX: %s", ppchNames[i]);
			break;
		}
	}

	if(fg && i==s)
	{
		PMFx *pFX = TSMP_ALLOC(PMFx);

		bSuccess = true;

		pFX->id = id;
		pFX->subid = subid;
		pFX->type = type;

		zeroVec4(pFX->ahFX);

		pFX->timeStart = time;
		pFX->timeStop = 0;

		for(i=0; i<PMFXSIZE && i<s; i++)
		{
			pFX->apchNames[i] = ppchNames[i];
		}

		for(; i<PMFXSIZE; i++)
		{
			pFX->apchNames[i] = NULL;
		}

		if(i<s)
		{
			if(isDevelopmentMode())
			{
				Errorf("Too many FX (%d) for a single event (%s), current limit is %d",s,PAFXTYPE(type),PMFXSIZE);
			}
		}
		
		pFX->pBlock = params;

		pFX->source = source;
		pFX->target = target;
		pFX->useTargetNode = !target && CHECK_FLAG(eFXFlags, EPMFXStartFlags_USE_TARGET_NODE);
		pFX->nodeSelectionType = (PowerAnimNodeSelectionType)eNodeSelectType;
		pFX->alwaysSelectSameNode = ARE_FLAGS_SET(eFXFlags, EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE);
		pFX->fRange = fRange;
		pFX->fArc = fArc;
		pFX->fYaw = fYaw;

		if(vecSource && CHECK_FLAG(eFXFlags,EPowerFXFlags_FROM_SOURCE_VEC))
		{
			pFX->source = 0;
			copyVec3(vecSource,pFX->vecSource);
		}
		else
		{
			zeroVec3(pFX->vecSource);
		}

		if(quatTarget)
			copyQuat(quatTarget,pFX->quatTarget);
		else
			zeroQuat(pFX->quatTarget);

		if(vecTarget)
		{
			copyVec3(vecTarget,pFX->vecTarget);
#ifdef GAMESERVER
			{
				// Try to limit this point lookup as much as possible
				Entity *eSource = source? entFromEntityRefAnyPartition(source) : NULL;
				if(!eSource || !entCheckFlag(eSource,ENTITYFLAG_IS_PLAYER))
				{
					GameNamedPoint *pPoint = namedpoint_GetByPosition(vecTarget);
					if(pPoint)
					{
						namedpoint_GetPosition(pPoint, NULL, pFX->quatTarget);
					}
				}
			}
#endif
		}
		else
		{
			zeroVec3(pFX->vecTarget);
		}

		// These have been coming up bad in the background, so I'm moving the assert up to see if I can catch it
		assert(FINITEVEC3(pFX->vecSource));
		assert(FINITEVEC3(pFX->vecTarget));
		assert(FINITEQUAT(pFX->quatTarget));

		pFX->fHue = hue;

		pFX->start = true;
		pFX->stop = false;

		pFX->flash = ARE_FLAGS_SET(eFXFlags, EPMFXStartFlags_FLASH);
		pFX->doNotTrackFlashed = ARE_FLAGS_SET(eFXFlags, EPowerFXFlags_DO_NOT_TRACK_FLASHED);

		pFX->miss = ARE_FLAGS_SET(eFXFlags, EPowerFXFlags_MISS);

		pFX->trigger = ARE_FLAGS_SET(eFXFlags, EPowerFXFlags_TRIGGER);
		pFX->triggerIsEntityID = ARE_FLAGS_SET(eFXFlags, EPowerFXFlags_TRIGGER_IS_ENTITY_ID);
		pFX->triggerMultiHit = ARE_FLAGS_SET(eFXFlags, EPowerFXFlags_TRIGGER_MULTI_HIT);

		
		PMprintf(1, PDBG_FX|PDBG_START,"%d %s FX   START "MPFXID_FORMAT" %d\n",
						pmTimestamp(0),
						(pFX->flash?"FLASH":"STICK"),
						PMFXID(pFX),
						time);

		if(PMFxFind(pFX,fg->ppFxUpdates,false,true))
		{
			PowersError("Powers Programmer needs to know: pmFxStart: Duplicate start found in fg updates");
			mrLog(	mr,
					NULL,
					"[PM.Fx.Error] Duplicate fg Fx "MPFXID_FORMAT" %s in updates",
					PMFXID(pFX),
					(pFX->flash ? "flash" : "sticky"));
			PMFxDestroy(&pFX);
		}
		else
		{
			if(pFX->flash)
			{
				if(PMFxFind(pFX,fg->ppFxFlashed,false,false))
				{
					PowersError("Powers Programmer needs to know: pmFxStart: Duplicate start found in fg flashed");
					mrLog(	mr,
							NULL,
							"[PM.Fx.Error] Duplicate fg Fx "MPFXID_FORMAT" in flashed",
							PMFXID(pFX));
					PMFxDestroy(&pFX);
				}
				else
				{
					eaPush(&fg->ppFxFlashed, pFX);
					eaPush(&fg->ppFxUpdates, pFX);
				}
			}
			else
			{
				if(PMFxFind(pFX,fg->ppFxStickied,false,false))
				{
					PowersError("Powers Programmer needs to know: pmFxStart: Duplicate start found in fg stickied");
					mrLog(	mr,
							NULL,
							"[PM.Fx.Error] Duplicate fg Fx "MPFXID_FORMAT" in stickied",
							PMFXID(pFX));
					PMFxDestroy(&pFX);
				}
				else
				{
					eaPush(&fg->ppFxStickied, pFX);
					eaPush(&fg->ppFxUpdates, pFX);
				}
			}
		}
	}

	if(!bSuccess && params)
	{
		dynParamBlockFree(params);
	}

	PERFINFO_AUTO_STOP();

	return bSuccess;
}

static bool FxMatch(PMFx *pFx, U32 uiID, U32 uiSubID, PowerAnimFXType eType, EntityRef source, EntityRef target, const char *pchName)
{
	// Standard check, except target can be null and still match
	if(pFx->type==eType
		&& pFx->source==source
		&& (!target || pFx->target==target)
		&& pFx->subid==uiSubID
		&& (pFx->id==uiID
			|| !(pFx->id && uiID))
		&& (!pchName || pFx->apchNames[0]==pchName))
	{
		return true;
	}
	return false;
}

//
// This looks for a sticky FX which matches everything except for the FX name.
// If  a match is found and the FX isn't the same one, it is stopped and a new
//   one started with the new FX name.
// If the FX name is the same, nothing is done.
// If NULL is given as the FX name, the matched FX is stopped but a new one is not
//   started.
//
bool pmFxReplaceOrStart(MovementRequester* mr,
						U32 id, U32 subid, PowerAnimFXType type, EntityRef source, EntityRef target,
						U32 time,
						const char **ppchNames,
						DynParamBlock *params,
						F32 hue,
						bool flash,
						Vec3 vecSource,
						Vec3 vecTarget,
						bool fromSourceVec)
{
	bool bFound = false;
	PowersMovementFG* fg = PMRequesterGetFG(mr);

	mrEnableMsgCreateToBG(mr);

	if(fg)
	{
		int i;

		for(i=0; i<eaSize(&fg->ppFxStickied); i++)
		{
			PMFx *pFx = fg->ppFxStickied[i];
			if(FxMatch(pFx,id,subid,type,source,target,NULL))
			{
				if(!ppchNames || pFx->apchNames[0]!=ppchNames[0])
				{
					pmFxStop(mr, id, subid, type, source, target, time, pFx->apchNames[0]);
				}
				else
				{
					bFound = true;
				}
			}
		}
	}

	if(!bFound && ppchNames && ppchNames[0])
	{
		EPMFXStartFlags flags = 0;
		if (flash)
			flags |= EPMFXStartFlags_FLASH;
		if (fromSourceVec)
			flags |= EPowerFXFlags_FROM_SOURCE_VEC;

		return pmFxStart(mr, id, subid, type, source, target, time, ppchNames, params, hue, 0.0f, 0.0f, 0.0f, vecSource, vecTarget, NULL, flags, 0);
	}
	return false;
}

void pmFxStop(MovementRequester* mr, 
			  U32 id, U32 subid, PowerAnimFXType type, EntityRef source, EntityRef target,
			  U32 time, 
			  const char *pchName)
{
	PowersMovementFG* fg = PMRequesterGetFG(mr);

	mrEnableMsgCreateToBG(mr);

	if(time<pmTimestamp(0))
	{
		U32 uiTimeNow = pmTimestamp(0);
		if(!entIsServer() || time+2<uiTimeNow)
		{
			PowersError("Powers Programmer needs to know: pmFxStop: Negative timestamp");
		}
	}

	if(fg)
	{
		bool bFound = false;
		int i;

		for(i=0; i<eaSize(&fg->ppFxStickied); i++)
		{
			PMFx *pFx = fg->ppFxStickied[i];
			if(FxMatch(pFx,id,subid,type,source,target,pchName))
			{
				int j;
				bFound = true;
				pFx->timeStop = time;
				pFx->stop = 1;

				eaRemove(&fg->ppFxStickied, i);

				if((j=eaFind(&fg->ppFxUpdates, pFx))!=-1)
				{
					// We already had this in the update list, must have been a start
					devassert(pFx->start);
				}
				else
				{
					// Same pointer wasn't in the list of updates already
					devassert(pFx->start);
					pFx->start = 0;
					for(j=0; j<eaSize(&fg->ppFxUpdates); j++)
					{
						// Make sure there isn't some other copy of this in the list already
						devassert(!PMFxCompare(pFx,fg->ppFxUpdates[j],false));
					}
					eaPush(&fg->ppFxUpdates, pFx);
				}
				PMprintf(1,PDBG_FX|PDBG_STOP|PDBG_STICKY,"%d STICK FX   STOP  "MPFXID_FORMAT" %d %s\n",pmTimestamp(0),PMFXID(pFx),time,pchName);

				// Find more if id is 0
				if(id!=0)
					break;
				else
					i--;
			}
		}

		if(!bFound)
		{
			PMprintf(1,PDBG_FX|PDBG_STOP|PDBG_STICKY,"%d STICK FX   STOP FAILED (not started): "MPFXID_FORMAT"\n",pmTimestamp(0),id,subid,source,target,PAFXTYPE(type));
		}
	}
}


void pmFxCancel(MovementRequester* mr, 
				U32 id, U32 subid, PowerAnimFXType type, EntityRef source, EntityRef target)
{
	PowersMovementFG* fg = PMRequesterGetFG(mr);

	mrEnableMsgCreateToBG(mr);

	if(fg)
	{
		PMFx *pFx = TSMP_ALLOC(PMFx);

		pFx->id = id;
		pFx->subid = subid;
		pFx->type = type;
		pFx->source = source;
		pFx->target = target;
		pFx->pBlock = NULL;
		// Only set these fields, nothing else is used for matching

		if(PMFxFind(pFx,fg->ppFxCancels,true,false))
		{
			PowersError("Powers Programmer needs to know: pmFxCancel: Duplicate cancel found in fg cancels");
			mrLog(	mr,
					NULL,
					"[PM.Fx.Error] Duplicate fg Fx "MPFXID_FORMAT" in cancels",
					PMFXID(pFx));
			PMFxDestroy(&pFx);
		}
		else
		{
			eaPush(&fg->ppFxCancels,pFx);
			PMprintf(1, PDBG_FX|PDBG_CANCEL,"%d       FX   CANCEL "MPFXID_FORMAT"\n",pmTimestamp(0),PMFXID(pFx));
		}
	}
}


/***** End Fx *****/


/***** HitReact *****/

#define PMHITREACTID(pHit) (pHit)->id,(pHit)->subid,(pHit)->source

static void PMHitReactPrint(char **estr, const PMHitReact *pHit)
{
	estrConcatf(estr,"Hit (%d %d %d)",PMHITREACTID(pHit));

	estrAppend2(estr,"\n");
}

static void PMHitReactPrintArray(char **estr, char *pchName, const PMHitReact *const*ppHit)
{
	int i;
	if(!eaSize(&ppHit)){
		return;
	}
	estrConcatf(estr,"-= %s =-\n",pchName);
	for(i=0; i<eaSize(&ppHit); i++)
	{
		PMHitReactPrint(estr,ppHit[i]);
	}
}

static void PMHitReactDestroy(PMHitReact **ppHit)
{
	StructDestroySafe(parse_PMHitReact, ppHit);
}

static void PMHitReactDestroyUnsafe(PMHitReact *pHit)
{
	PMHitReactDestroy(&pHit);
}


static void PMHitReact_FG_CREATE_TOBG(const MovementRequesterMsg* msg, 
									  PowersMovementFG *fg, 
									  PowersMovementToBG *toBG,
									  S32* updatedToBGOut,
									  S32* needsCreateToBGOut)
{
	int i;
	U32 time = pmTimestamp(0);

	if(eaSize(&fg->ppHitReacts))
	{
		PERFINFO_AUTO_START_FUNC();

		// Sends HitReact updates to BG - walk the array forward to be consistent about ordering
		for(i=0; i<eaSize(&fg->ppHitReacts); i++)
		{
			PMHitReact *pHit = fg->ppHitReacts[i];

			// Once again, skipping all the paranoid dupe checking
			eaPush(&toBG->ppHitReacts, pHit);
			*updatedToBGOut = 1;

			eaRemove(&fg->ppHitReacts,i);
			i--;
		}

		PERFINFO_AUTO_STOP();
	}
}

static void PMHitReact_BG_HANDLE_UPDATED_TOBG(	const MovementRequesterMsg* msg, 
												PowersMovementToBG *toBG,
												PowersMovementBG *bg,
												PowersMovementLocalBG *localBG)
{
	int i,s;
	bool bRepredict = mmIsRepredictingBG();

	// Apply HitReact updates to BG
	s = eaSize(&toBG->ppHitReacts);
	for(i=0; i<s; i++)
	{
		bool bPushed = false;
		PMHitReact *pHit = toBG->ppHitReacts[i];
		
		// Skipping paranoid dupe checking
		eaPush(&localBG->ppHitReactsMutable,pHit);
	}

	eaClear(&toBG->ppHitReacts);
}

static void PMHitReact_BG_CREATE_DETAILS(const MovementRequesterMsg* msg, 
										 PowersMovementBG *bg,
										 PowersMovementLocalBG *localBG)
{
	int i,s;

	s = eaSize(&localBG->ppHitReacts);
	for(i=0; i<s; i++)
	{
		PMHitReact *pHit = localBG->ppHitReacts[i];

		mrmLog(	msg,
			NULL,
			"[PM.HitReact] Adding bg HitReact (%d %d %d)",
			PMHITREACTID(pHit));

		// TODO: Send stuff to movement resource system here
	}

	eaDestroyEx(&localBG->ppHitReactsMutable,PMHitReactDestroyUnsafe);
}


void pmHitReactStart(MovementRequester* mr,
					 U32 id, U32 subid, EntityRef source,
					 const char **ppchBitNames,
					 const char **ppchFxNames,
					 DynParamBlock *params,
					 F32 hue,
					 U32 spcTimeout)
{
	PowersMovementFG* fg = PMRequesterGetFG(mr);
	int i,s;

	mrEnableMsgCreateToBG(mr);

	if(!id)
	{
		PowersError("Powers Programmer needs to know: pmHitReactStart: id is 0");
	}

	if(fg && (eaSize(&ppchFxNames) || eaSize(&ppchBitNames)))
	{
		PMHitReact *pHit = StructAlloc(parse_PMHitReact);

		pHit->id = id;
		pHit->subid = subid;
		pHit->source = source;

		s = eaSize(&ppchBitNames);
		if(s)
		{
			for(i=0; i<s; i++)
			{
				eaPush(&pHit->ppchBits,StructAllocString(ppchBitNames[i]));
			}
		}

		s = eaSize(&ppchFxNames);
		if(s)
		{
			for(i=0; i<s; i++)
			{
				if(!dynFxInfoExists(ppchFxNames[i]))
				{
					PowersError("Failed to find valid FX: %s", ppchFxNames[i]);
				}
				else
				{
					eaPush(&pHit->ppchFX,StructAllocString(ppchFxNames[i]));
				}
			}
		}

		pHit->pBlock = params;

		/* Sourced from a vec not yet supported
		if(vecSource && fromSourceVec)
		{
			pFX->source = 0;
			copyVec3(vecSource,pFX->vecSource);
		}
		*/
		
		pHit->fHue = hue;
		pHit->spcTimeout = spcTimeout;

		PMprintf(1, PDBG_FX|PDBG_BITS|PDBG_START,"%d HitR (%d %d %d)\n",pmTimestamp(0),PMHITREACTID(pHit));

		// I'm skipping all the hyper-aggressive dupe checks for these for now
		eaPush(&fg->ppHitReacts, pHit);
	}
}


/***** End HitReact *****/


/***** Ignore Input *****/


#define PMIGNOREID(pIgnore) (pIgnore)->id,PAFXTYPE((pIgnore)->type)

static void PMIgnorePrint(char **estr, const PMIgnore *pIgnore)
{
	estrConcatf(estr,"Ignore (%d %s)",PMIGNOREID(pIgnore));
	if(pIgnore->cancel) estrConcatf(estr," Cancel");
	if(pIgnore->start) estrConcatf(estr," Start %d",pIgnore->timeStart);
	if(pIgnore->started) estrConcatf(estr," Started %d",pIgnore->timeStart);
	if(pIgnore->stop) estrConcatf(estr," Stop %d",pIgnore->timeStop);
//	if(pIgnore->stopped) estrConcatf(estr," Stopped %d",pIgnore->timeStop);
	estrConcatChar(estr,'\n');
}

static void PMIgnorePrintArray(char **estr, char *pchName, const PMIgnore *const*ppIgnore)
{
	int i;
	if(!eaSize(&ppIgnore)){
		return;
	}
	estrConcatf(estr,"-= %s =-\n",pchName);
	for(i=0; i<eaSize(&ppIgnore); i++)
	{
		PMIgnorePrint(estr,ppIgnore[i]);
	}
}

static PMIgnore *PMIgnoreClone(PMIgnore *pIgnore)
{
	PMIgnore *pNew = StructAlloc(parse_PMIgnore);
	StructCopyAll(parse_PMIgnore,pIgnore,pNew);
	return pNew;
}

static void PMIgnoreDestroy(PMIgnore **ppIgnore)
{
	StructDestroySafe(parse_PMIgnore, ppIgnore);
}

static void PMIgnoreDestroyUnsafe(PMIgnore *pIgnore)
{
	PMIgnoreDestroy(&pIgnore);
}


// Takes an update-style or new ignore input, and properly updates the destination earray
static void PMIgnoreUpdate(const MovementRequesterMsg* msg,
						   PMIgnore *pUpdate,
						   PMIgnore ***pppDestHandle)
{
	int i;
	PMIgnore **ppDest = *pppDestHandle;
	bool bWildcardID = !pUpdate->id;
	bool bWildcardType = !pUpdate->type;

	if((bWildcardID || bWildcardType) && !pUpdate->cancel)
	{
		PowersError("Powers Programmer needs to know: PMIgnoreUpdate: Non-cancel ignore with wildcard fields");
	}

	// See if we just want to update an existing
	for(i=eaSize(pppDestHandle)-1; i>=0; i--)
	{
		PMIgnore *pTarget = ppDest[i];

		// Handle cancel update first
		if(pUpdate->cancel)
		{
			if((bWildcardID || pTarget->id==pUpdate->id)
				&& (bWildcardType || pTarget->type==pUpdate->type))
			{
				// Just yank it out
				if(msg) mrmLog(msg,NULL,"[PM.Ignore] Cancelling Ignore (%d %s)\n",PMIGNOREID(pTarget));
				eaRemoveFast(pppDestHandle,i);
				PMIgnoreDestroy(&pTarget);
			}
		}
		else
		{
			// No wildcards for normal updates
			if(pTarget->id == pUpdate->id && pTarget->type == pUpdate->type)
			{
				// Updating an existing ignore with new information
				// Update the appropriate fields and break
				//if(pUpdate->started) pTarget->started = 1;
				//if(pUpdate->stopped) pTarget->stopped = 1;
				// We got a start update for something that hasn't started yet
				if(pUpdate->start)// && !pTarget->started)
				{
					pTarget->start = 1;
					pTarget->timeStart = pUpdate->timeStart;
				}
				// We got a stop update for something that hasn't stopped yet
				if(pUpdate->stop)// && !pTarget->stopped)
				{
					pTarget->stop = 1;
					pTarget->timeStop = pUpdate->timeStop;
				}
				break;
			}
		}
	}

	// For cancels or stuff that didn't find a match, push a clone
	if(i<0)
	{
		PMIgnore *pNew = PMIgnoreClone(pUpdate);
		eaPush(pppDestHandle,pNew);
	}
}

static void PMIgnore_FG_CREATE_TOBG(const MovementRequesterMsg* msg, 
									PowersMovementFG *fg, 
									PowersMovementToBG *toBG,
									S32* updatedToBGOut,
									S32* needsCreateToBGOut)
{
	int i;

	if(eaSize(&fg->ppIgnores))
	{
		PERFINFO_AUTO_START_FUNC();

		// Send all ignore updates to the BG; order doesn't matter
		for(i=eaSize(&fg->ppIgnores)-1; i>=0; i--)
		{
			PMIgnore *pUpdate = fg->ppIgnores[i];
			U32 uiTime = pUpdate->start ? pUpdate->timeStart : pUpdate->timeStop;

			if(PMCreateToBGCheckProcessCount(msg,uiTime))
			{
				//		if(pUpdate->start || pUpdate->stop)
				{
					mrmLog(msg,NULL,"[PM.Ignore] Updating fg Ignore (%d %s) to toBG (%s)\n",PMIGNOREID(pUpdate),pUpdate->start?"Start":"Stop");
					PMIgnoreUpdate(msg,pUpdate,&toBG->ppIgnores);
					*updatedToBGOut = 1;
					eaRemoveFast(&fg->ppIgnores,i);
					PMIgnoreDestroy(&pUpdate);
					//			pUpdate->start = pUpdate->stop = 0;
				}
			}
			else
			{
				*needsCreateToBGOut = 1;
			}
		}

		PERFINFO_AUTO_STOP();
	}
}

static void PMIgnore_BG_HANDLE_UPDATED_TOBG(const MovementRequesterMsg* msg, 
											PowersMovementToBG *toBG,
											PowersMovementBG *bg,
											PowersMovementLocalBG *localBG)
{
	int i;
	bool bRepredict = mmIsRepredictingBG();
/*
	// bg->toFG: Note any just started or stopped and update them
	{
		int print = 0;
		for(i=eaSize(&bg->ppIgnores)-1; i>=0; i--)
		{
			PMIgnore *pII = bg->ppIgnores[i];
			if(pII->startedLastFrame)
			{
				if(!print)
				{
					print = 1;
					if(!mmIsRepredictingBG())
						PMprintf(2, PDBG_IGIN, "Processing BG:");
				}
				if(!mmIsRepredictingBG())
					PMprintf(2, PDBG_IGIN, " 0|%d (%d)", pII->id, pII->timeStart);

				mrmLog(msg,NULL,"[PM.Ignore] Updating started bg IgnoreInput %d to toFG\n",pII->id);
				PMIgnoreUpdate(pII,&toFG->ppIgnores);

				pII->startedLastFrame = 0;
			}
		}

		for(i=eaSize(&bg->ppIgnores)-1; i>=0; i--)
		{
			PMIgnore *pII = bg->ppIgnores[i];
			if(pII->stopped)
			{
				if(!print)
				{
					print = 1;
					if(!mmIsRepredictingBG())
						PMprintf(2, PDBG_IGIN, "Processing BG:");
				}
				if(!mmIsRepredictingBG())
					PMprintf(2, PDBG_IGIN, " 1|%d (%d)", pII->id, pII->timeStop);

				mrmLog(msg,NULL,"[PM.Ignore] Updating stopped bg IgnoreInput %d to toFG\n",pII->id);
				PMIgnoreUpdate(pII,&toFG->ppIgnores);
				StructDestroy(parse_PMIgnore,pII);
				eaRemoveFast(&bg->ppIgnores,i);
			}
		}
		if(print) if(!mmIsRepredictingBG()) PMprintf(2, PDBG_IGIN, "\n");
	}
*/
	// toBG->bg: Update any submitted changes, order doesn't matter
	for(i=eaSize(&toBG->ppIgnores)-1; i>=0; i--)
	{
		mrmLog(msg,NULL,"[PM.Ignore] Updating toBG Ignore (%d %s) to bg\n",PMIGNOREID(toBG->ppIgnores[i]));
		PMIgnoreUpdate(msg,toBG->ppIgnores[i],&bg->ppIgnoresMutable);
	}
	eaDestroyStruct(&toBG->ppIgnores,parse_PMIgnore);
}


static bool PMIgnore_BG_DISCUSS_DATA_OWNERSHIP(const MovementRequesterMsg* msg, 
											   PowersMovementBG *bg,
											   PowersMovementLocalBG *localBG)
{
	int i;
	bool bIgnore = false;
	for(i=eaSize(&bg->ppIgnores)-1; i>=0; i--)
	{
		PMIgnore *pIgnore = bg->ppIgnores[i];

		if(pIgnore->cancel)
		{
			mrmLog(msg,NULL,"[PM.Ignore] Removing bg IgnoreInput cancel (%d %s)\n",PMIGNOREID(pIgnore));
			eaRemoveFast(&bg->ppIgnoresMutable,i);
			PMIgnoreDestroy(&pIgnore);
		}
		else
		{
			bool bStopped = false;
			if(pIgnore->start && mrmProcessCountHasPassedBG(msg,pIgnore->timeStart))
			{
				mrmLog(msg,NULL,"[PM.Ignore] Starting bg IgnoreInput (%d %s)\n",PMIGNOREID(pIgnore));
				if(!mmIsRepredictingBG()) PMprintf(2, PDBG_IGIN, "Start Ignore: (%d %s)\n",PMIGNOREID(pIgnore));
				pIgnore->start = 0;
				pIgnore->started = 1;
				//pIgnore->startedLastFrame = 1;
			}
			if(pIgnore->stop && mrmProcessCountHasPassedBG(msg,pIgnore->timeStop))
			{
				mrmLog(msg,NULL,"[PM.Ignore] Stopping bg IgnoreInput (%d %s)\n",PMIGNOREID(pIgnore));
				if(!mmIsRepredictingBG()) PMprintf(2, PDBG_IGIN, "Stop  Ignore: (%d %s)\n",PMIGNOREID(pIgnore));
				bStopped = true;
				eaRemoveFast(&bg->ppIgnoresMutable,i);
				PMIgnoreDestroy(&pIgnore);
				//			pIgnore->stop = 0;
				//			pIgnore->stopped = 1;
			}

			bIgnore |= (!bStopped && pIgnore->started);
			bg->rooted |= (!bStopped && pIgnore->started && (pIgnore->id==PMOVE_NEARDEATH || pIgnore->id==PMOVE_ROOT || pIgnore->id==PMOVE_HOLD));
		}
	}

	return bIgnore;
}

// Starts ignoring movement input.  ID is either a power activation ID, 
//  or a predefined value indicating the external cause (death, root, etc).
void pmIgnoreStart(	Character *c,
					MovementRequester *mr,
					U32 uiID, PowerAnimFXType eType,
					U32 uiTime,
					char *pchCause)
{
	PowersMovementFG* fg = PMRequesterGetFG(mr);
	if (fg)
	{
		PMIgnore *pIgnore = StructAlloc(parse_PMIgnore);

		if (uiID == PMOVE_HOLD) {
			//sending the interrupt flag here on holds is a white lie, but is also behavior requested by the animators to simplify their templates
			character_BitsCancel(c, uiID, 2, eType, entGetRef(c->pEntParent));
			character_SendAnimKeywordOrFlag(c, uiID, 1, eType, entGetRef(c->pEntParent), s_pchFlagInterrupt, NULL, uiTime, false, false, false, false, false, false);
		}
		else if (uiID == PMOVE_NEARDEATH) {
			character_BitsCancel(c, uiID, 2, eType, entGetRef(c->pEntParent));
			character_SendAnimKeywordOrFlag(c, uiID, 1, eType, entGetRef(c->pEntParent), s_pchFlagInterrupt, NULL, uiTime, false, false, false, false, false, true);
		}

		mrEnableMsgCreateToBG(mr);
		pIgnore->id = uiID;
		pIgnore->type = eType;
		if(pchCause) pIgnore->cause = StructAllocString(pchCause);
		pIgnore->start = 1;
		pIgnore->timeStart = uiTime;
		PMprintf(1, PDBG_IGIN,"%d STICK IGIN START (%d %s) %d\n",pmTimestamp(0),PMIGNOREID(pIgnore),uiTime);
		PMIgnoreUpdate(NULL,pIgnore,&fg->ppIgnores);
		PMIgnoreDestroy(&pIgnore);
	}
}

// Stops ignoring movement input.  ID is either a power activation ID, 
//  or a predefined value indicating the external cause (death, root, etc).
void pmIgnoreStop(	Character *c,
					MovementRequester *mr,
					U32 uiID, PowerAnimFXType eType,
					U32 uiTime)
{
	PowersMovementFG* fg = PMRequesterGetFG(mr);
	if (fg)
	{
		PMIgnore *pIgnore = StructAlloc(parse_PMIgnore);

		if (uiID == PMOVE_HOLD) {
			character_BitsCancel(c, uiID, 1, eType, entGetRef(c->pEntParent));
		}
		else if (uiID == PMOVE_NEARDEATH) {
			character_BitsCancel(c, uiID, 1, eType, entGetRef(c->pEntParent));
		}

		mrEnableMsgCreateToBG(mr);
		pIgnore->id = uiID;
		pIgnore->type = eType;
		pIgnore->stop = 1;
		pIgnore->timeStop = uiTime;
		PMprintf(1, PDBG_IGIN,"%d STICK IGIN STOP  (%d %s) %d\n",pmTimestamp(0),PMIGNOREID(pIgnore),uiTime);
		PMIgnoreUpdate(NULL,pIgnore,&fg->ppIgnores);
		PMIgnoreDestroy(&pIgnore);
	}
}

// Cancels the ignoring of movement input.  ID is either a power activation ID, 
//  or a predefined value indicating the external cause (death, root, etc).
void pmIgnoreCancel(MovementRequester *mr,
					U32 uiID, PowerAnimFXType eType)
{
	PowersMovementFG* fg = PMRequesterGetFG(mr);
	if (fg)
	{
		PMIgnore *pIgnore = StructAlloc(parse_PMIgnore);
		mrEnableMsgCreateToBG(mr);
		pIgnore->id = uiID;
		pIgnore->type = eType;
		pIgnore->cancel = 1;
		PMprintf(1, PDBG_IGIN,"%d STICK IGIN CANCEL (%d %s)\n",pmTimestamp(0),PMIGNOREID(pIgnore));
		PMIgnoreUpdate(NULL,pIgnore,&fg->ppIgnores);
		PMIgnoreDestroy(&pIgnore);
	}
}



/***** End Ignore Input *****/



/***** Move *****/

#define PMMOVEID(pMove) (pMove)->id,(pMove)->type

static void PMMovePrint(char **estr, const PMMove *pMove)
{
	estrConcatf(estr,"Move (%d %d)",PMMOVEID(pMove));
	
	if(pMove->timeStart) estrConcatf(estr," Start%s %d",pMove->started ? "(ed)" : "", pMove->timeStart);
	if(pMove->timeStop) estrConcatf(estr," Stop %d",pMove->timeStop);
	
	if(pMove->erTarget) estrConcatf(estr," to %d",pMove->erTarget);
	else estrConcatf(estr," to (%.2f %.2f %.2f)",vecParamsXYZ(pMove->vecTarget));
	estrConcatChar(estr,'\n');
}

static void PMMovePrintArray(char **estr, char *pchName, const PMMove *const*ppMove)
{
	int i;
	if(!eaSize(&ppMove)){
		return;
	}
	estrConcatf(estr,"-= %s =-\n",pchName);
	for(i=0; i<eaSize(&ppMove); i++)
	{
		PMMovePrint(estr,ppMove[i]);
	}
}

static PMMove *PMMoveClone(const PMMove *pMove)
{
	PMMove *pNew = StructAlloc(parse_PMMove);
	StructCopyAll(parse_PMMove,pMove,pNew);
	return pNew;
}

static void PMMoveDestroy(PMMove **ppMove)
{
	StructDestroySafe(parse_PMMove, ppMove);
}

static void PMMoveDestroyUnsafe(PMMove *pMove)
{
	PMMoveDestroy(&pMove);
}

static PMMove * PMFindMove(U8 uchActID, PowerMoveType eType, PMMove **ppMoves)
{
	S32 i;

	// See if we just want to overwrite an existing move(s)
	for (i = 0; i < eaSize(&ppMoves); i++)
	{
		if(ppMoves[i]->id == uchActID && ppMoves[i]->type == eType)
		{
			return ppMoves[i];
		}
	}

	return NULL;
}

// Takes a move and properly updates the destination earray
static void PMMoveUpdate(const PMMove *pMove, PMMove ***pppDestHandle)
{
	int i;
	PMMove **ppDest = *pppDestHandle;
	PMMove *pNew = PMMoveClone(pMove);

	// See if we just want to overwrite an existing move(s)
	for(i=eaSize(pppDestHandle)-1; i>=0; i--)
	{
		if(ppDest[i]->id == pMove->id
			&& (!pMove->type || ppDest[i]->type == pMove->type))
		{
			// Delete the existing move
			PMMoveDestroy(&ppDest[i]);
			eaRemoveFast(pppDestHandle,i);
			// If this wasn't intended as a cancel there should only be one match
			if(pMove->type)
				break;
		}
	}

	eaPush(pppDestHandle,pNew);
}

static void PMMove_FG_CREATE_TOBG(	const MovementRequesterMsg* msg, 
									PowersMovementFG *fg, 
									PowersMovementToBG *toBG,
									S32* updatedToBGOut,
									S32* needsCreateToBGOut)
{
	int i;

	if(eaSize(&fg->ppMove))
	{
		PERFINFO_AUTO_START_FUNC();

		// Send all ignore updates to the BG; order doesn't matter
		for(i=eaSize(&fg->ppMove)-1; i>=0; i--)
		{
			PMMove *pUpdate = fg->ppMove[i];
			U32 uiTime = pUpdate->timeStart;

			if(PMCreateToBGCheckProcessCount(msg,uiTime))
			{
				mrmLog(msg,NULL,"[PM.Move] Updating fg Move (%d %d) to toBG\n",PMMOVEID(pUpdate));
				PMMoveUpdate(pUpdate,&toBG->ppMove);
				*updatedToBGOut = 1;
				eaRemoveFast(&fg->ppMove,i);
				PMMoveDestroy(&pUpdate);
			}
			else
			{
				*needsCreateToBGOut = 1;
			}
		}

		PERFINFO_AUTO_STOP();
	}
}

static void PMMove_BG_HANDLE_UPDATED_TOBG(	const MovementRequesterMsg* msg, 
											PowersMovementToBG *toBG,
											PowersMovementBG *bg,
											PowersMovementLocalBG *localBG)
{
	int i;
	bool bRepredict = mmIsRepredictingBG();
	// toBG->bg: Update any submitted changes, order doesn't matter
	for(i=eaSize(&toBG->ppMove)-1; i>=0; i--)
	{
		mrmLog(msg,NULL,"[PM.Move] Updating toBG Move (%d %d) to bg\n",PMMOVEID(toBG->ppMove[i]));
		PMMoveUpdate(toBG->ppMove[i],&bg->ppMoveMutable);
	}
	eaDestroyStruct(&toBG->ppMove,parse_PMMove);
}

static void PMMove_BG_DISCUSS_DATA_OWNERSHIP(const MovementRequesterMsg* msg, 
											 PowersMovementBG *bg,
											 PowersMovementLocalBG *localBG,
											 PowersMovementToFG *toFG,
											 bool *pbHasMoveOut,
											 bool *pbHasRotOut,
											 bool *pbHasBitsOut,
											 bool *bCheckCollideEnt)
{
	int i;

	PMMove *pMostRecentActiveLurch = NULL;

	for (i = 0; i < eaSize(&bg->ppMove); i++)
	{
		PMMove *pMove = bg->ppMove[i];
		if(!pMove->started && mrmProcessCountHasPassedBG(msg,pMove->timeStart))
		{
			mrmLog(msg,NULL,"[PM.Move] Starting bg Move (%d %d)\n",PMMOVEID(pMove));
			pMove->started = true;
		}

		// Find the most recent active lurch
		if (pMove->type == kPowerMoveType_Lurch && 
			pMove->started && 
			(pMostRecentActiveLurch == NULL || pMove->timeStart > pMostRecentActiveLurch->timeStart))
		{
			pMostRecentActiveLurch = pMove;
		}
	}

	// Update the moves and see if we're actively moving
	bg->moving = 0;
	bg->stoppingMove = 0;
	for(i=eaSize(&bg->ppMove)-1; i>=0; i--)
	{
		PMMove *pMove = bg->ppMove[i];
				
		if (!pMove->bIgnoreCollision && 
			pMove->type == kPowerMoveType_Lurch && 
			g_CombatConfig.lurch.bStopOnEntityCollision)
		{
			*bCheckCollideEnt = true;
		}
	
		// Special code for lunge to notify the fg
		if(pMove->type==kPowerMoveType_Lunge)
		{
			int bNotify = false;

			if(pMove->timeNotify && mrmProcessCountHasPassedBG(msg,pMove->timeNotify))
			{
				bNotify = true;
			}
			else if(pMove->distNotify)
			{
				Vec3 vecDistance;
				mrmGetPositionBG(msg,vecDistance);
				subVec3(pMove->vecTarget,vecDistance,vecDistance);
				if(lengthVec3Squared(vecDistance) < pMove->distNotify*pMove->distNotify)
				{
					bNotify = true;
				}
			}

			if(bNotify)
			{
				mrmEnableMsgUpdatedToFG(msg);
				toFG->idLungeActivate = pMove->id;
				pMove->timeNotify = 0;
				pMove->distNotify = 0;
			}
		}

		if(pMove->started)
		{
			int bStop = false;

			if(pMove->type==kPowerMoveType_Lunge)
			{
				if (g_CombatConfig.bLungeIgnoresCollisionCapsules)
				{
					mrmIgnoreCollisionWithEntsBG(msg, true);
				}

				if(pMove->erTarget)
				{
					S32 bKeepTarget = false;
					Vec3 vecTargetCurrent;
					if(mrmGetEntityPositionBG(msg,pMove->erTarget,vecTargetCurrent))
					{
						Vec3 vecTargetMoved;
						subVec3(vecTargetCurrent,pMove->vecTarget,vecTargetMoved);
						if(lengthVec3Squared(vecTargetMoved) < 2500)
						{
							bKeepTarget = true;
							copyVec3(vecTargetCurrent,pMove->vecTarget);
						}
					}

					if(!bKeepTarget)
					{
						pMove->erTarget = 0;
					}
				}

				if(pMove->distStop)
				{
					Vec3 vecDistance;
					mrmGetPositionBG(msg,vecDistance);
					subVec3(pMove->vecTarget,vecDistance,vecDistance);
					if(lengthVec3Squared(vecDistance) < SQR(pMove->distStop))
					{
						bStop = true;
					}
				}
			}
			else if (pMove->type==kPowerMoveType_Lurch)
			{
				if (pMove->bIgnoreCollision)
				{
					mrmIgnoreCollisionWithEntsBG(msg, true);
				}

				// Stop if there is a newer lurch which already started
				if (pMostRecentActiveLurch && pMove != pMostRecentActiveLurch)
				{
					bStop = true;
				}
				// Stop based on travelled distance only if an animation graph is not used
				else if (pMove->pchAnimGraphName == NULL && 
					pMove->lurchTraveled >= pMove->distStop)
				{
					bStop = true;
				}
			}
			else if(g_CombatConfig.bFaceActivateSoft && pMove->type==kPowerMoveType_Face)
			{	// If we're soft-facing, and this is face, and the entity is trying to move, stop			
				if(mrmGetInputValueBitBG(msg, MIVI_BIT_FORWARD)
					|| mrmGetInputValueBitBG(msg, MIVI_BIT_BACKWARD)
					|| mrmGetInputValueBitBG(msg, MIVI_BIT_LEFT)
					|| mrmGetInputValueBitBG(msg, MIVI_BIT_RIGHT))
				{
					bStop = true;
				}
			}
				
			if(!bStop)
			{
				if(pMove->stopLurching || 
					(pMove->timeStop && mrmProcessCountHasPassedBG(msg,pMove->timeStop)))
				{
					bStop = true;
				}
			}
			
			if(bStop)
			{
				// Save that we're finishing a move, so we need to come to a hard stop
				if(pMove->fSpeed > 0.0f)
				{
					bg->stoppingMove = true;
					*pbHasMoveOut = true;
				}

				if(pMove->type==kPowerMoveType_Lunge)
				{
					mrmEnableMsgUpdatedToFG(msg);
					toFG->idLungeFinished = pMove->id;
					mrmGetPositionBG(msg,toFG->vecLungeFinished);
					if (g_CombatConfig.bLungeIgnoresCollisionCapsules)
					{
						mrmIgnoreCollisionWithEntsBG(msg, false);
					}
				}
				else if (pMove->type == kPowerMoveType_Lurch && pMove->bIgnoreCollision)
				{
					mrmIgnoreCollisionWithEntsBG(msg, false);
				}

				mrmLog(msg,NULL,"[PM.Move] Stopping bg Move (%d %d)\n",PMMOVEID(pMove));
				eaRemoveFast(&bg->ppMoveMutable,i);
				PMMoveDestroy(&pMove);
			}
		}

		if(pMove && pMove->started)
		{
			bg->moving = true;
			if(pMove->type!=kPowerMoveType_Face)  // Face doesn't need position target
				*pbHasMoveOut = true;
			*pbHasRotOut = true; // Currently all moves face // pMove->bFace;
			if(pMove->fSpeed > 0.f){
				*pbHasBitsOut = 1;
			}
		}
	}
}

// --------------------------------------------------------------------------------------------------------------------
static PMMove* pmGetCurrentPMMove(PMMove *const*ppMove)
{
	S32 i;
	PMMove *pMove = NULL;
	// Find the oldest active move, except higher types override
	for(i=eaSize(&ppMove)-1; i>=0; i--)
	{
		if(ppMove[i]->started)
		{
			if(!pMove
				|| ppMove[i]->type > pMove->type
				|| (ppMove[i]->type == pMove->type
					&& ppMove[i]->timeStart < pMove->timeStart))
			{
				pMove = ppMove[i];
			}
		}
	}

	return pMove;
}

// --------------------------------------------------------------------------------------------------------------------
static void pmMoveQueueFG(MovementRequester *mr, const PMMove *pMove)
{
	PowersMovementFG* fg = PMRequesterGetFG(mr);
	if(fg)
	{
		mrEnableMsgCreateToBG(mr);

		PMprintf(1,PDBG_OTHER,"      MOVE       (%d %d) %d %d\n", PMMOVEID(pMove), pMove->timeStart, pMove->timeStop);

		PMMoveUpdate(pMove, &fg->ppMove);
	}

}

// --------------------------------------------------------------------------------------------------------------------
void pmLungeStart(	MovementRequester *mr, 
					U32 uiID, 
					U32 timeToStart, 
					U32 timeToStop,
					F32 distToStop,
					F32 fSpeed, 
					U32 timeNotify, 
					F32 distNotify,
					EntityRef erTarget, 
					const Vec3 vecTarget,
					S32 bHorizontalLunge,
					S32 bReverseLurch)
{
	Entity *pTargetEnt;
	PMMove move = {0};

	pTargetEnt = entFromEntityRefAnyPartition(erTarget);
	
	move.id = uiID;
	move.type = kPowerMoveType_Lunge;
	move.bFaceActivateSticky = g_CombatConfig.bFaceActivateSticky;
	move.timeStart = timeToStart;
	move.timeStop = timeToStop;
	move.distStop = distToStop;
	move.fSpeed = fSpeed;
	move.timeNotify = timeNotify;
	move.distNotify = distNotify;
	move.erTarget = erTarget;
	move.bLungeHorizontal = !!bHorizontalLunge;
	
	if(pTargetEnt && !IS_HANDLE_ACTIVE(pTargetEnt->hCreatorNode))
	{
		entGetPos(pTargetEnt, move.vecTarget);
	}
	else if(vecTarget && !ISZEROVEC3(vecTarget))
	{
		copyVec3(vecTarget,move.vecTarget);
	}
	else
	{
		move.bDefaultDir = 1;
	}
	move.faceAway = !!bReverseLurch;

	pmMoveQueueFG(mr, &move);
}

// --------------------------------------------------------------------------------------------------------------------
void pmFaceStart(	MovementRequester *mr, 
					U32 uiID, 
					U32 timeToStart,
					U32 timeToStop,
					EntityRef erTarget, 
					const Vec3 vecTarget,
					S32 bFaceActivateSticky,
					S32 bUseVecAsDirection)
{
	Entity *pTargetEnt;
	PMMove move = {0};

	move.id = uiID;
	move.type = kPowerMoveType_Face;
	move.bFaceActivateSticky = bFaceActivateSticky;
	move.timeStart = timeToStart;
	move.timeStop = timeToStop;
	move.erTarget = erTarget;
	
	if (bUseVecAsDirection)
	{
		move.bUseVecAsDirection = true; 
		copyVec3(vecTarget, move.vecTarget);
	}
	else
	{
		pTargetEnt = entFromEntityRefAnyPartition(erTarget);

		if(pTargetEnt && !IS_HANDLE_ACTIVE(pTargetEnt->hCreatorNode))
		{
			entGetPos(pTargetEnt, move.vecTarget);
		}
		else if(vecTarget && !ISZEROVEC3(vecTarget))
		{
			copyVec3(vecTarget, move.vecTarget);
		}
		else
		{
			move.bDefaultDir = 1;
		}
	}

	pmMoveQueueFG(mr, &move);
}

// --------------------------------------------------------------------------------------------------------------------
void pmLurchStart(	MovementRequester *mr, 
					U32 uiID, 
					U32 timeToStart,
					U32 timeToStop,
					F32 distToStop,
					F32 fSpeed, 
					F32 fMoveYawOffset,
					EntityRef erTarget, 
					const Vec3 vecDirection,
					F32 entCollisionCapsuleBuffer,
					S32 bFaceActivateSticky,
					S32 bFaceAway,
					S32 bIgnoreCollision)
{
	PMMove move = {0};

	move.id = uiID;
	move.type = kPowerMoveType_Lurch;
	move.bFaceActivateSticky = bFaceActivateSticky;
	move.timeStart = timeToStart;
	move.timeStop = timeToStop;
	move.fSpeed = fSpeed;
	move.fMoveYawOffset = fMoveYawOffset;
	move.distStop = distToStop;
	move.faceAway = !!bFaceAway;
	move.entCollisionCapsuleBuffer = entCollisionCapsuleBuffer;
	move.bIgnoreCollision = !!bIgnoreCollision;
	move.bUseVecAsDirection = true; 
	move.erTarget = erTarget;
	copyVec3(vecDirection, move.vecTarget);
		
	pmMoveQueueFG(mr, &move);
}

// --------------------------------------------------------------------------------------------------------------------
void pmLurchSetHitFlag(SA_PARAM_NN_VALID MovementRequester *mr,
	U8 uchActID, 
	bool bHit)
{
	PowersMovementFG* fg = PMRequesterGetFG(mr);
	if (fg)
	{
		fg->lurchUpdate.uchActID = uchActID;
		fg->lurchUpdate.bHit = bHit;
		mrEnableMsgCreateToBG(mr);
	}
}

// --------------------------------------------------------------------------------------------------------------------
void pmLurchAnimStart(	MovementRequester *mr, 
						U32 uiID,
						const char *pchAnimGraphName,
						U32 timeToStart,
						U32 timeToStop,
						F32 fMoveYawOffset,
						EntityRef erTarget, 
						const Vec3 vecDirection,
						F32 entCollisionCapsuleBuffer,
						S32 bFaceActivateSticky,
						S32 bIgnoreCollision)
{
	PMMove move = {0};

	move.id = uiID;
	move.type = kPowerMoveType_Lurch;
	move.pchAnimGraphName = allocAddString(pchAnimGraphName); // Pooled string
	move.bFaceActivateSticky = bFaceActivateSticky;
	move.timeStart = timeToStart;
	move.timeStop = timeToStop;
	move.faceAway = 0;
	move.entCollisionCapsuleBuffer = entCollisionCapsuleBuffer;
	move.bIgnoreCollision = !!bIgnoreCollision;
	move.bUseVecAsDirection = true; 
	move.fMoveYawOffset = fMoveYawOffset;
	move.erTarget = erTarget;
	move.iRemainderStartFrame = -1;
	move.iRemainderEndFrame = -1;
	copyVec3(vecDirection, move.vecTarget);

	pmMoveQueueFG(mr, &move);
}


// --------------------------------------------------------------------------------------------------------------------
void pmMoveCancel(MovementRequester *mr, U32 uiID, PowerMoveType eType)
{
	int i;
	PowersMovementFG* fg = PMRequesterGetFG(mr);

	if(!fg){
		return;
	}

	mrEnableMsgCreateToBG(mr);
	PMprintf(1,PDBG_OTHER,"      MOVE CANCEL (%d %d)\n",uiID,eType);

	// Destroy any in the fg that haven't been sent to BG yet.  This may be unnecessary because...
	for(i=eaSize(&fg->ppMove)-1; i>=0; i--)
	{
		if((!uiID || fg->ppMove[i]->id == uiID)
			&& (!eType || fg->ppMove[i]->type == eType))
		{
			PMMoveDestroy(&fg->ppMove[i]);
			eaRemoveFast(&fg->ppMove,i);
		}
	}

	// ... we send a "fake" move back with a stop time of 1, which will blow away any matching
	//  moves it finds and then stop immediately once it reaches the BG.  This is necessary
	//  because it's possible in certain circumstances to create PMMoves with a timeStop of 0.
	{
		PMMove move = {0};
		move.id = uiID;
		move.type = eType;
		move.timeStop = 1;
		pmMoveQueueFG(mr, &move);
	}
}

// --------------------------------------------------------------------------------------------------------------------
/***** End Move *****/
// --------------------------------------------------------------------------------------------------------------------


// --------------------------------------------------------------------------------------------------------------------
// PMConstantForce
// --------------------------------------------------------------------------------------------------------------------
static void PMConstantForce_Print(char **estr, const PMConstantForce *pRepel)
{
	estrConcatf(estr,"ConstantForce %d", pRepel->id);

	estrConcatf(estr," Start %d, Stop %d", pRepel->spcStart, pRepel->spcStop);
	if (pRepel->erRepeler)
	{
		estrConcatf(estr," speed (%.2f) dir (%.2f %.2f %.2f) Ent (%d)\n", pRepel->speed, vecParamsXYZ(pRepel->vec), pRepel->erRepeler);
	}
	else
	{
		estrConcatf(estr," force (%.2f %.2f %.2f)\n", vecParamsXYZ(pRepel->vec));
	}
}

static void PMConstantForce_PrintArray(char **estr, char *pchName, const PMConstantForce *const*ppConstantForce)
{
	int i;
	if(!eaSize(&ppConstantForce))
	{
		return;
	}
	estrConcatf(estr,"-= %s =-\n",pchName);
	for(i=0; i<eaSize(&ppConstantForce); i++)
	{
		PMConstantForce_Print(estr,ppConstantForce[i]);
	}
}

static void PMConstantForce_FG_CREATE_TOBG(	const MovementRequesterMsg* msg, 
											PowersMovementFG *fg, 
											PowersMovementToBG *toBG,
											S32* updatedToBGOut)
{
	if (eaSize(&fg->ppConstantForces))
	{
		toBG->ppConstantForces = fg->ppConstantForces;
		fg->ppConstantForces = NULL;
		*updatedToBGOut = true;
	}
}


// --------------------------------------------------------------------------------------------------------------------
static void PMConstantForce_BG_HANDLE_UPDATED_TOBG(	const MovementRequesterMsg* msg, 
													PowersMovementToBG *toBG,
													PowersMovementLocalBG *localBG)
{
	if (toBG->ppConstantForces)
	{
		eaPushEArray(&localBG->ppConstantForcesQueued, &toBG->ppConstantForces);
		eaDestroy(&toBG->ppConstantForces);
		mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
	}

}

// --------------------------------------------------------------------------------------------------------------------
static void PMConstantForce_BG_DISCUSS_DATA_OWNERSHIP(	SA_PARAM_NN_VALID const MovementRequesterMsg* msg, 
														SA_PARAM_NN_VALID PowersMovementBG *bg,
														SA_PARAM_NN_VALID PowersMovementLocalBG *localBG,
														SA_PARAM_NN_VALID PowersMovementToFG *toFG)
{
	bool bNewRepel = false;
	bool bHadRepel = !!eaSize(&localBG->ppConstantForcesActive);

	// check the queued repels and see what should go active this tick
	EARRAY_CONST_FOREACH_BEGIN(localBG->ppConstantForcesQueued, i, isize);
	{
		PMConstantForce* pNewRepel = localBG->ppConstantForcesQueued[i];

#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pNewRepel'"
		if(!mrmProcessCountHasPassedBG(msg, pNewRepel->spcStart))
			continue;
				
		eaRemove(&localBG->ppConstantForcesQueued, i);
		i--;
		isize--;
				
		if (pNewRepel->spcStart == pNewRepel->spcStop)
		{	// this is a cancel request for the repel
			// go through my active repels and remove any that matc the ID
			FOR_EACH_IN_EARRAY(localBG->ppConstantForcesActive, PMConstantForce, pRepel)
			{
				if (pRepel->id == pNewRepel->id)
				{
					pRepel->spcStop = pNewRepel->spcStop;
				}
			}
			FOR_EACH_END

			free(pNewRepel);
		}
		else 
		{
			bool bFoundDupe = false;
			FOR_EACH_IN_EARRAY(localBG->ppConstantForcesActive, PMConstantForce, pRepel)
			{
				if (pRepel->id == pNewRepel->id)
				{
					bFoundDupe = true;
					break;
				}
			}
			FOR_EACH_END

			if (!bFoundDupe)
				bNewRepel = true;
							
			eaPush(&localBG->ppConstantForcesActive, pNewRepel);
		}

	}
	EARRAY_FOREACH_END;

	// go through the active repels and calculate the actual velocity then set it with the MM
	if (eaSize(&localBG->ppConstantForcesActive))
	{
		Vec3 vRepelVelocity = {0}; 
		bool bHasRepel = false;

		FOR_EACH_IN_EARRAY(localBG->ppConstantForcesActive, PMConstantForce, pRepel)
		{
			if(!mrmProcessCountHasPassedBG(msg, pRepel->spcStop))
			{
				if (!pRepel->erRepeler)
				{
					addVec3(pRepel->vec, vRepelVelocity, vRepelVelocity);
					bHasRepel = true;
				}
				else
				{	// this repel is relative to another entity
					// todo: FG might want to handle the case of the caster dying
					Vec3 vTargetPos;
					if(mrmGetEntityPositionBG(msg, pRepel->erRepeler, vTargetPos))
					{
						Vec3 vDir;
						mrmGetPositionBG(msg, vDir);
						subVec3(vDir, vTargetPos, vDir);
						normalVec3(vDir);
						if (pRepel->yawOffset)
						{
							rotateXZ(pRepel->yawOffset, vDir, vDir + 2);
						}
						scaleAddVec3(vDir, pRepel->speed, vRepelVelocity, vRepelVelocity);

						bHasRepel = true;
					}
					else
					{	// couldn't get the entity, stop the repel
						eaRemoveFast(&localBG->ppConstantForcesActive, FOR_EACH_IDX(-,pRepel));
						free(pRepel);
					}
				}
			}
			else
			{
				eaRemoveFast(&localBG->ppConstantForcesActive, FOR_EACH_IDX(-,pRepel));
				free(pRepel);
			}
		}
		FOR_EACH_END
	
		if (bHasRepel)
		{
			mrmSetConstantPushVelBG(msg, vRepelVelocity);

			//if (!bNewRepel)
			if (bHadRepel)
			{
				mrmDoNotRestartPrediction(msg);
			}
		}
	}
	
}


// --------------------------------------------------------------------------------------------------------------------
// End PMConstantForce
// --------------------------------------------------------------------------------------------------------------------




static void PMReleaseAnimPrintArray(char** estr, const PMReleaseAnim*const* eaReleaseAnim)
{
	if(!eaSize(&eaReleaseAnim)){
		return;
	}

	estrConcatf(estr, "Release anims: ");

	EARRAY_CONST_FOREACH_BEGIN(eaReleaseAnim, i, isize);
	{
		const PMReleaseAnim* ra = eaReleaseAnim[i];
		estrConcatf(estr, "%s(spc %u, id %u)", i ? ", " : "", ra->spc, ra->id);
	}
	EARRAY_FOREACH_END;

	estrConcatf(estr, "\n");
}

static void PrintPowersMovementBG(char **estr, PowersMovementBG *bg, PowersMovementLocalBG* localBG)
{
	PMBitsPrintArray(estr,"Pending Bits",bg->ppBitsPending);
	PMBitsPrintArray(estr,"Local Pending Bits",localBG->ppBitsPending);
	PMBitsPrintArray(estr,"Flash Bits",localBG->ppBitsFlash);
	PMBitsPrintArray(estr,"Sticky Bits",bg->ppBitsSticky);
	PMReleaseAnimPrintArray(estr,bg->eaReleaseAnim);
	
	PMFxPrintArray(estr,"Pending FX",bg->ppFxPending);
	PMFxPrintArray(estr,"Local Pending FX",localBG->ppFxPending);
	PMFxPrintArray(estr,"Flash FX",localBG->ppFxFlash);
	PMFxPrintArray(estr,"Sticky FX",bg->ppFxSticky);

	PMIgnorePrintArray(estr,"Ignores",bg->ppIgnores);
	PMMovePrintArray(estr,"Moves",bg->ppMove);
	PMConstantForce_PrintArray(estr, "Active ConstantForces", localBG->ppConstantForcesActive);
	PMConstantForce_PrintArray(estr, "Queued ConstantForces", localBG->ppConstantForcesQueued);
}

static void PrintPowersMovementFG(char **estr, PowersMovementFG *fg, PowersMovementToBG* toBG)
{
	PMBitsPrintArray(estr,"Cancel Bits",fg->ppBitsCancels);
	PMBitsPrintArray(estr,"Update Bits",fg->ppBitsUpdates);
	PMBitsPrintArray(estr,"Flashed Bits",fg->ppBitsFlashed);
	PMBitsPrintArray(estr,"Stickied Bits",fg->ppBitsStickied);
	PMFxPrintArray(estr,"Cancel FX",fg->ppFxCancels);
	PMFxPrintArray(estr,"Update FX",fg->ppFxUpdates);
	PMFxPrintArray(estr,"Flashed FX",fg->ppFxFlashed);
	PMFxPrintArray(estr,"Stickied FX",fg->ppFxStickied);
	PMHitReactPrintArray(estr,"HitReacts",fg->ppHitReacts);
	PMIgnorePrintArray(estr,"Ignores",fg->ppIgnores);
	PMMovePrintArray(estr,"Moves",fg->ppMove);
	PMConstantForce_PrintArray(estr, "ConstantForces", fg->ppConstantForces);

	PMBitsPrintArray(estr,"toBG Cancel Bits",toBG->ppBitsCancels);
	PMBitsPrintArray(estr,"toBG Update Bits",toBG->ppBitsUpdates);
	PMFxPrintArray(estr,"toBG Cancel FX",toBG->ppFxCancels);
	PMFxPrintArray(estr,"toBG Update FX",toBG->ppFxUpdates);
	PMHitReactPrintArray(estr,"toBG HitReacts",toBG->ppHitReacts);
	PMIgnorePrintArray(estr,"toBG Ignores",toBG->ppIgnores);
	PMMovePrintArray(estr,"toBG Moves",toBG->ppMove);
	PMConstantForce_PrintArray(estr, "toBG ConstantForces", toBG->ppConstantForces);
	
}

static void pmHandleMsgDiscussDataOwnership(const MovementRequesterMsg* msg,
											PowersMovementBG* bg,
											PowersMovementLocalBG* localBG,
											PowersMovementToFG* toFG)
{
	bool bHasBits=0, bHasMove=0, bHasRot=0, bHasFx=0, bCheckCollideEnt=0;
	U32 onGround = 0;
			
	if(!vec3IsZero(bg->vecPush) && mrmProcessCountHasPassedBG(msg, bg->uiTimePush))
	{
		Vec3 pos;
		mrmGetPositionBG(msg, pos);

		mrmSetAdditionalVelBG(msg, bg->vecPush, 1, 0);
		setVec3same(bg->vecPush,0);
	}

	PMConstantForce_BG_DISCUSS_DATA_OWNERSHIP(msg, bg, localBG, toFG);

	PMBits_BG_DISCUSS_DATA_OWNERSHIP(msg,bg,localBG,&bHasBits);

	bHasFx = PMFx_BG_DISCUSS_DATA_OWNERSHIP(msg,bg,localBG);

	// Update the ignore inputs times and see if we're actively ignoring input
	bg->rooted = 0;
	bg->ignoringInput = PMIgnore_BG_DISCUSS_DATA_OWNERSHIP(msg,bg,localBG);
	if(bg->ignoringInput)
	{
		bHasMove = 1;
		bHasRot = 1;
	}

	PMMove_BG_DISCUSS_DATA_OWNERSHIP(msg,bg,localBG,toFG,&bHasMove,&bHasRot,&bHasBits,&bCheckCollideEnt);

	// See if targeting wants anything
	if(!bHasRot && bg->erSelectedTarget)
	{
		Entity *e = entFromEntityRefAnyPartition(bg->erSelectedTarget);
		if(e)
			bHasRot = 1;
	}

	if(bHasBits)
	{
		if(mrmAcquireDataOwnershipBG(msg, MDC_BIT_ANIMATION, 1, NULL, NULL))
		{
			bg->hasanim = 1;
		}
		else
		{
			PMprintf(3,PDBG_OTHER,"Ownership of Animation bit was denied\n");
		}
	}
	else
	{
		bool bReleaseBits=0;

		PMBits_BG_DISCUSS_RELEASING_DATA_OWNERSHIP(msg,bg,localBG,&bReleaseBits);

		if(bReleaseBits || !gConf.bNewAnimationSystem) {
			mrmReleaseDataOwnershipBG(msg, MDC_BIT_ANIMATION);
			bg->hasanim = 0;
		}
	}

	if(bHasMove)
	{
		if(mrmAcquireDataOwnershipBG(msg, MDC_BIT_POSITION_TARGET, 1, NULL, NULL))
		{
			bg->hasposition = 1;
			if (bCheckCollideEnt)
				mrmEnableMsgCollidedEntBG(msg, true);
		}
		else
		{
			PMprintf(3,PDBG_OTHER,"Ownership of Position_Target bit was denied\n");
		}
	}
	else
	{
		mrmReleaseDataOwnershipBG(msg, MDC_BIT_POSITION_TARGET);
		bg->hasposition = 0;

		mrmEnableMsgCollidedEntBG(msg, false);
				
	}

	if(bHasRot)
	{
		if(mrmAcquireDataOwnershipBG(msg, MDC_BIT_ROTATION_TARGET, 1, NULL, NULL))
		{
			bg->hasrotation = 1;
		}
		else
		{
			PMprintf(3,PDBG_OTHER,"Ownership of Rotation_Target bit was denied\n");
		}
	}
	else
	{
		mrmReleaseDataOwnershipBG(msg, MDC_BIT_ROTATION_TARGET);
		bg->hasrotation = 0;
	}
}

static S32 pmCompareMovementFrames(const S32 *piTime, const DynPowerMovementFrame** ppFrame)
{
	return *piTime - (*ppFrame)->iFrame;
}

static void pmCalculateFirstCreateOutputStuff(	const MovementRequesterMsg* msg,
												PowersMovementBG* bg,
												PowersMovementLocalBG* localBG,
												PMMove *pMove)
{
	Vec3 pos;

	mrmGetPositionBG(msg, pos);

	if(pMove->bDefaultDir)
	{
		Vec2 pyFace;
		mrmGetFacePitchYawBG(msg, pyFace);
		// Move towards wherever we were already facing
		createMat3_2_YP(pMove->vMoveDir, pyFace);
		if(pMove->type==kPowerMoveType_Lunge)
		{
			normalVec3(pMove->vMoveDir);
		}
		else
		{
			normalVec3XZ(pMove->vMoveDir);
		}
		scaleVec3(pMove->vMoveDir, 20.f, pMove->vMoveDir); // Moving towards a point 20' in front of me?
	}
	else
	{
		F32 len;

		// get the movement direction. 
		if(pMove->erTarget && (pMove->type==kPowerMoveType_Face || pMove->type==kPowerMoveType_Lurch))
		{
			Vec3 vecTargetPos;
			if(mrmGetEntityPositionBG(msg, pMove->erTarget, vecTargetPos))
			{
				subVec3(vecTargetPos, pos, pMove->vMoveDir);
				if (pMove->bUseVecAsDirection)
				{
					pMove->vMoveDir[1] = 0.f;
					normalVec3XZ(pMove->vMoveDir);
				}
				
				if (pMove->faceAway)
					negateVec3(pMove->vMoveDir, pMove->vMoveDir);
			}
			else
			{	// could not get the entity direction, use the give vecTarget
				if (!pMove->bUseVecAsDirection)
				{
					subVec3(pMove->vecTarget, pos, pMove->vMoveDir);
				}
				else
				{
					copyVec3(pMove->vecTarget, pMove->vMoveDir);
				}
			}
		}
		else if (pMove->bUseVecAsDirection)
		{
			copyVec3(pMove->vecTarget, pMove->vMoveDir);
		}
		else
		{
			subVec3(pMove->vecTarget,pos,pMove->vMoveDir);
			if (pMove->bLungeHorizontal)
				pMove->vMoveDir[1] = 0.f;
		}
		// 
				
		if(pMove->type == kPowerMoveType_Lunge)
		{
			F32 fDeltaMove = pMove->fSpeed * MM_SECONDS_PER_STEP;
			
			len = normalVec3(pMove->vMoveDir);
			if (len - fDeltaMove < pMove->distStop)
			{
				len = len - pMove->distStop;
			}
		}
		else if(pMove->type == kPowerMoveType_Lurch)
		{
			if (pMove->pchAnimGraphName)
			{
				// TODO: Use a thread safe struct here as this might cause crashes in edit mode.
				DynAnimGraph *pGraph = RefSystem_ReferentFromString(hAnimGraphDict, pMove->pchAnimGraphName);

				len = 0.f;

				if (pGraph &&
					pGraph->pPowerMovementInfo &&
					pGraph->pPowerMovementInfo->pDefaultMovement)
				{
					U32 uiCurrentTime;
					S32 iFrameIndex;
					F32 fTimePassed;
					F32 fDistanceToTarget;
					DynPowerMovementFrame **eaFrames;
					S32 iFrameCount;

					if (pMove->bHit && pGraph->pPowerMovementInfo->pHitPauseMovement)
					{
						eaFrames = pGraph->pPowerMovementInfo->pHitPauseMovement->eaFrameList;
					}
					else
					{
						eaFrames = pGraph->pPowerMovementInfo->pDefaultMovement->eaFrameList;
					}
					iFrameCount = eaSize(&eaFrames);

					if (iFrameCount > 0)
					{
						Vec3 vLurchDir, vTargetPoint;
						Mat3 mat3Rotation;
						S32 iFrameToSearch;
						bool bRemainderApplied = false;
						
						// Calculate the rotation
						unitDirVec3ToMat3(pMove->vMoveDir, mat3Rotation);

						mrmGetProcessCountBG(msg, &uiCurrentTime);
						if (uiCurrentTime >= pMove->timeStart) // Sanity check
						{
							fTimePassed = uiCurrentTime - pMove->timeStart;
						}
						else
						{
							fTimePassed = 0.f;
						}

						// Convert to actual frames
						fTimePassed *= MM_SECONDS_PER_PROCESS_COUNT * PAFX_FPS;

						iFrameToSearch = ceilf(fTimePassed);

						// Find the closest frame
						iFrameIndex = (S32)eaBFind(eaFrames, pmCompareMovementFrames, iFrameToSearch);

						if (iFrameCount == 1 || iFrameIndex >= iFrameCount)
						{
							if (0 <= pMove->iRemainderEndFrame) {
								//use remaining distance
								copyVec3(pMove->vRemainder, vTargetPoint);
								pMove->iRemainderEndFrame = -1;
								bRemainderApplied = true;
							} else {
								//use last frame position
								copyVec3(eaFrames[iFrameCount - 1]->vPos, vTargetPoint);
							}
							mulVecMat3(vTargetPoint, mat3Rotation, vLurchDir);
						}
						else
						{
							S32 iFrameBefore;
							S32 iFrameAfter;
							Vec3 vDir, vTemp;
							const F32 *pvPosBefore;
							const F32 *pvPosAfter;
							DynPowerMovementFrame *pFrameAfter = eaFrames[iFrameIndex];
							F32 fTime;
							F32 fTimeBetweenFrames;

							iFrameAfter = pFrameAfter->iFrame;
							pvPosAfter = pFrameAfter->vPos;

							if (iFrameIndex == 0)
							{
								// Since we're looking at the first frame,
								// there is no previous frame. Assume a frame 0 with no translation.
								iFrameBefore = 0;
								pvPosBefore = zerovec3;
							}
							else
							{
								DynPowerMovementFrame *pFrameBefore = eaFrames[iFrameIndex - 1];
								iFrameBefore = pFrameBefore->iFrame;
								pvPosBefore = pFrameBefore->vPos;
							}
							fTimeBetweenFrames = (iFrameAfter - iFrameBefore);
							if (fTimeBetweenFrames == 0.f)
							{
								fTime = 0.f;
							}
							else
							{
								fTime = (fTimePassed - iFrameBefore) / fTimeBetweenFrames;
							}
							
							interpVec3(fTime, pvPosBefore, pvPosAfter, vTargetPoint);
							subVec3(vTargetPoint, pvPosBefore, vDir);
							copyVec3(vDir, vTemp);
							if (pMove->iRemainderStartFrame == iFrameBefore) {
								subVec3(vDir, pMove->vUsed, vDir);
							}
							if (pMove->iRemainderEndFrame >= 0 &&
								pMove->iRemainderEndFrame <= iFrameBefore) {
								//need to make sure remainder was from previous key range,
								//this should always be ==, < would need extra code but
								//setting here anyhow since it shouldn't happen
								addVec3(vDir, pMove->vRemainder, vDir);
							}
							mulVecMat3(vDir, mat3Rotation, vLurchDir);

							copyVec3(vTemp, pMove->vUsed);
							subVec3(pvPosAfter, vTargetPoint, pMove->vRemainder);
							pMove->iRemainderStartFrame = iFrameBefore;
							pMove->iRemainderEndFrame = iFrameAfter;
						}

						// Find the world space target position
						addVec3(vLurchDir, pos, vTargetPoint);

						// Use the Y position from the current position
						vTargetPoint[1] = pos[1];

						// Find the distance we need to travel
						fDistanceToTarget = distance3(vTargetPoint, pos);

						// Find the speed we need to travel at
						if (!bRemainderApplied && (iFrameCount == 1 || iFrameIndex >= iFrameCount)) {
							pMove->fSpeed = 0.f;
						} else {
							pMove->fSpeed = MM_STEPS_PER_SECOND * fDistanceToTarget;
						}

						len = normalizeCopyVec3(vLurchDir, pMove->vMoveDir);
					}
				}
			}
			else
			{
				F32 fDeltaMove = pMove->fSpeed * MM_SECONDS_PER_STEP;
				len = pMove->fSpeed;

				if (pMove->lurchTraveled + fDeltaMove < pMove->distStop)
				{
					pMove->lurchTraveled += fDeltaMove;
				}
				else
				{
					len = pMove->distStop - pMove->lurchTraveled;
					pMove->lurchTraveled = pMove->distStop;
				}
			}
				
			if (pMove->fMoveYawOffset)
				rotateXZ(pMove->fMoveYawOffset, pMove->vMoveDir, pMove->vMoveDir+2);
			
			if (!pMove->firstLurchTickProcessed && 
				!pMove->bIgnoreCollision && 
				g_CombatConfig.lurch.bStopOnEntityCollision && 
				len > 0.f)
			{
				Vec3 vTestPos, vDirInOut;
				F32 fCollisionCapsuleBuffer = pMove->entCollisionCapsuleBuffer;

				copyVec3(pMove->vMoveDir, vDirInOut);

				// Test our current location and see if we're already colliding with an entity.
				// move the test position back by the pMove->vMoveDir because in mrmCheckCollisionWithOthersBG
				// it will move the position by vDirInOut. 
				// We need to pass in the direction because we want to ignore entities we are moving away from 
				// (the argument, bIgnoreMovingAwayFromEnts, to mrmCheckCollisionWithOthersBG)
				subVec3(pos, vDirInOut, vTestPos);

				// always use some buffer if we don't have one from the PMMove
				if (!fCollisionCapsuleBuffer)
					fCollisionCapsuleBuffer = 0.1f;

				if (mrmCheckCollisionWithOthersBG(msg, vTestPos, vDirInOut, fCollisionCapsuleBuffer, pMove->erTarget))
				{
					len = 0.f;
					pMove->stopLurching = true;
				}

				pMove->firstLurchTickProcessed = true;
			}
			
			if (!pMove->stopLurching && 
				(g_CombatConfig.lurch.bStopOnLedges || g_CombatConfig.lurch.bDisableInAir))
			{
				S32 onGround = true;
				mrmGetOnGroundBG(msg, &onGround, NULL);
			
				if (g_CombatConfig.lurch.bStopOnLedges && 
					onGround && 
					!mrmCheckGroundAheadBG(msg, pos, pMove->vMoveDir))
				{
					len = 0.f;
					pMove->stopLurching = true;
				}
				
				if (g_CombatConfig.lurch.bDisableInAir && !onGround)
				{
					len = 0.f;
					pMove->stopLurching = true;
				}
			}

			
		}
		else
		{
			// Face is just full distance
			len = normalVec3(pMove->vMoveDir);
		}

		if(len <= 0.f)
		{
			// Don't move
			if(pMove->type!=kPowerMoveType_Face)
			{
				pMove->fSpeed = 0.f;
			}
		}
		else
		{
			scaleVec3(pMove->vMoveDir, len, pMove->vMoveDir);
		}
	}

}

// the "target" in the msg is the output of this function, despite the "const" here
static void pmHandleMsgCreateOutputPositionTarget(	const MovementRequesterMsg* msg,
													PowersMovementBG* bg,
													PowersMovementLocalBG* localBG)
{
	// If we're ignoring input, stop
	if(bg->ignoringInput)
	{
		mrmTargetSetAsStoppedBG(msg);
		mrmTargetSetSpeedAsNormalBG(msg);
	}
				
	// An active move involving speed just stopped this frame, so we want to force a stop
	if(bg->stoppingMove)
	{
		Vec3 pos;
		mrmGetPositionBG(msg, pos);
		mrmTargetSetAsPointBG(msg, pos);
		mrmTargetSetUseYBG(msg, 1);
		mrmTargetSetSpeedAsConstantBG(msg, 0.f);
	}

	if(bg->moving)
	{
		PMMove *pMove = pmGetCurrentPMMove(bg->ppMove);

		// Only one move at a time?
		//  In theory, we could have several moves interfering with each other, or some
		//  moves controlling position while others control rotation.  But maybe that
		//  will be handled more gracefully with other categories of movement, like
		//  knockback.
		if(pMove && !pMove->stopLurching)
		{
			if(msg->in.bg.createOutput.flags.isFirstCreateOutputOnThisStep)
			{
				pmCalculateFirstCreateOutputStuff(msg, bg, localBG, pMove);
			}

			if(pMove->fSpeed > 0.f)
			{
				Vec3 pos;

				mrmGetPositionBG(msg, pos);
								
				addVec3(pos, pMove->vMoveDir, pos);
								
				mrmTargetSetAsPointBG(msg, pos);
				mrmTargetSetSpeedAsConstantBG(msg, pMove->fSpeed);
				if(pMove->type==kPowerMoveType_Lunge)
				{
					mrmTargetSetUseYBG(msg,1);
				}
			}
			else if (pMove->type == kPowerMoveType_Lunge)
			{
				mrmTargetSetSpeedAsConstantBG(msg, 0.f);
			}
		}
		else if (pMove->stopLurching)
		{
			mrmTargetSetSpeedAsConstantBG(msg, 0.f);
		}
	}
}

static void pmHandleMsgCreateOutputRotationTarget(	const MovementRequesterMsg* msg,
													PowersMovementBG* bg,
													PowersMovementLocalBG* localBG)
{
	// If we're not rooted, and we've got rotation and a target, set it as the target
	if(!bg->rooted && bg->hasrotation && bg->erSelectedTarget)
	{
		Vec3 vecTargetPos;
		if(mrmGetEntityPositionBG(msg,bg->erSelectedTarget,vecTargetPos))
		{
			mrmRotationTargetSetAsPointBG(msg,vecTargetPos);
			mrmLog(msg, NULL, "[PM.Move] Setting rotation target as point (EntityRef %d) %f %f %f", 
				bg->erSelectedTarget, vecParamsXYZ(vecTargetPos));
		}
	}

	if(bg->moving)
	{
		PMMove *pMove = pmGetCurrentPMMove(bg->ppMove);

		// Only one move at a time?
		//  In theory, we could have several moves interfering with each other, or some
		//  moves controlling position while others control rotation.  But maybe that
		//  will be handled more gracefully with other categories of movement, like
		//  knockback.
		if(pMove && !pMove->stopLurching)
		{
			if(msg->in.bg.createOutput.flags.isFirstCreateOutputOnThisStep)
			{
				pmCalculateFirstCreateOutputStuff(msg, bg, localBG, pMove);
			}

			if (!bg->erSelectedTarget || 
				pMove->type != kPowerMoveType_Face || 
				pMove->bFaceActivateSticky)
			{
				Vec3 vDir;
				if (pMove->type == kPowerMoveType_Lurch && 
					pMove->pchAnimGraphName)
				{
					copyVec3(pMove->vecTarget, vDir);
				}
				else
				{
					copyVec3(pMove->vMoveDir, vDir);

					// if we have a movement offset rotation, 
					// apply the inverse of the rotation so we still face the original direction.
					if (pMove->fMoveYawOffset)
					{
						rotateXZ(-pMove->fMoveYawOffset, vDir, vDir+2);
					}
				}
				
				if(pMove->faceAway)
				{
					scaleVec3(vDir, -1, vDir);
					
				}
				mrmRotationTargetSetAsDirectionBG(msg, vDir);
			}
		}
	}
}

static void pmHandleMsgCreateOutputAnimation(	const MovementRequesterMsg* msg,
												PowersMovementBG* bg,
												PowersMovementLocalBG* localBG)
{
	int i, s;

	if(!gConf.bNewAnimationSystem){
		return;
	}

	s = eaSize(&localBG->ppBitsFlash);
	for(i=0; i<s; i++)
	{
		PMBits* pBits = localBG->ppBitsFlash[i];
		if(!PMBits_IsHitReact(pBits))
		{
			assert(pBits->isKeyword || pBits->isFlag);
			PMBitsAddAnimBitsBG(msg,bg,pBits,1);

			if (gConf.bNewAnimationSystem) {
				PMBitsDestroy(&localBG->ppBitsFlashMutable[i]);
				eaRemove(&localBG->ppBitsFlashMutable, i);
				s--;
				i--;
			}
		}
	}
}

static void pmHandleMsgUpdatedToBG(	const MovementRequesterMsg* msg,
									PowersMovementBG* bg,
									PowersMovementLocalBG* localBG,
									PowersMovementToFG* toFG,
									PowersMovementToBG* toBG)
{
	U32 curtime;
	mrmGetProcessCountBG(msg,&curtime);

	if(toBG->reset)
	{
		char *pchReset = NULL;
		toBG->reset = 0;
				
		estrStackCreate(&pchReset);
		PrintPowersMovementBG(&pchReset,bg,localBG);
		Errorf("PM.Reset.BG @ %d: %s",curtime,pchReset);
		mrmLog(msg,NULL,"[PM.Reset.BG] %s\n",pchReset);
		pmLog(0,msg,"PM.Reset.BG","%s\n",pchReset);
		estrDestroy(&pchReset);

		eaDestroyEx(&bg->ppBitsPendingMutable,PMBitsDestroyUnsafe);
		eaDestroyEx(&bg->ppBitsStickyMutable,PMBitsDestroyUnsafe);
		while(eaSize(&localBG->ppBitsStances)){
			PMBitsStances* s = eaPop(&localBG->ppBitsStancesMutable);
			PMBitsStancesDestroy(msg, &s);
		}
		eaDestroyEx(&bg->ppFxPendingMutable,PMFxDestroyUnsafe);
		//eaDestroyEx(&bg->ppFxSticky,PMFxDestroyUnsafe);
		// Slight special case for existing sticky fx.  Mark them to stop immediately instead
		//  of destroying them outright
		{
			int i;
			for(i=eaSize(&bg->ppFxSticky)-1; i>=0; i--)
			{
				bg->ppFxSticky[i]->stop = true;
				bg->ppFxSticky[i]->timeStop = 0;
			}
		}
		eaDestroyEx(&bg->ppMoveMutable,PMMoveDestroyUnsafe);
		eaDestroyEx(&bg->ppIgnoresMutable,PMIgnoreDestroyUnsafe);
		setVec3same(bg->vecPush,0);

		eaDestroyEx(&localBG->ppBitsFlashMutable,PMBitsDestroyUnsafe);
		eaDestroyEx(&localBG->ppBitsPendingMutable,PMBitsDestroyUnsafe);
		eaDestroyEx(&localBG->ppFxFlashMutable,PMFxDestroyUnsafe);
		eaDestroyEx(&localBG->ppFxFlashPastMutable,PMFxDestroyUnsafe);
		eaDestroyEx(&localBG->ppFxPendingMutable,PMFxDestroyUnsafe);
		eaDestroyEx(&localBG->ppConstantForcesQueued,NULL);
		eaDestroyEx(&localBG->ppConstantForcesActive,NULL);
		

		mrmReleaseAllDataOwnershipBG(msg);
				
		mrmHandledMsgsRemoveBG(	msg,
								MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP |
									MR_HANDLED_MSG_CREATE_DETAILS);
	}
	else
	{
		// Target update
		if(toBG->bTargetUpdate)
		{
			bg->erSelectedTarget = toBG->erSelectedTarget;
			toBG->bTargetUpdate = false;
		}

		if(bg->knockDown)
		{
			mrmEnableMsgUpdatedToFG(msg);
			toFG->knockDown = bg->knockDown;
			bg->knockDown = 0;
		}

		PMBits_BG_HANDLE_UPDATED_TOBG(msg,toBG,bg,localBG);

		PMFx_BG_HANDLE_UPDATED_TOBG(msg,toBG,bg,localBG);

		PMHitReact_BG_HANDLE_UPDATED_TOBG(msg,toBG,bg,localBG);

		PMIgnore_BG_HANDLE_UPDATED_TOBG(msg,toBG,bg,localBG);

		PMMove_BG_HANDLE_UPDATED_TOBG(msg,toBG,bg,localBG);

		/***** Move *****/
		if(!vec3IsZero(toBG->vecPush))
		{
			addVec3(toBG->vecPush,bg->vecPush,bg->vecPush);
			bg->uiTimePush = toBG->uiTimePush;
		}

		PMConstantForce_BG_HANDLE_UPDATED_TOBG(msg, toBG, localBG);
		
		// Lurch update
		if (toBG->lurchUpdate.uchActID)
		{
			PMMove *pMove = PMFindMove(toBG->lurchUpdate.uchActID, kPowerMoveType_Lurch, bg->ppMoveMutable);
			if (pMove)
			{
				// TODO: Use a thread safe struct here as this might cause crashes in edit mode.
				DynAnimGraph *pGraph = RefSystem_ReferentFromString(hAnimGraphDict, pMove->pchAnimGraphName);

				// Copy the flags from toBG
				pMove->bHit = toBG->lurchUpdate.bHit;

				// Recalculate the stop time of the animation since
				// the length is based on hitpause condition
				if (pGraph && pGraph->pPowerMovementInfo->pHitPauseMovement)
				{
					DynPowerMovementFrame **eaFrameList = NULL;
					S32 iFrameCount;
					if (pMove->bHit)
					{
						eaFrameList = pGraph->pPowerMovementInfo->pHitPauseMovement->eaFrameList;
					}
					else
					{
						eaFrameList = pGraph->pPowerMovementInfo->pDefaultMovement->eaFrameList;
					}
					iFrameCount = eaSize(&eaFrameList);
					if (iFrameCount > 0)
					{
						pMove->timeStop = pmTimestampFrom(pMove->timeStart, (eaFrameList[iFrameCount - 1]->iFrame+1)/PAFX_FPS);
					}
					else
					{
						pMove->timeStop = pMove->timeStart;
					}
				}

			}

			StructReset(parse_PowersMovementLurchUpdate, &toBG->lurchUpdate);
		}
			
		if(	!eaSize(&bg->ppBitsPending) &&
			!eaSize(&bg->ppBitsSticky) &&
			!eaSize(&bg->eaReleaseAnim) &&
			!eaSize(&bg->ppFxPending) &&
			!eaSize(&bg->ppFxSticky) &&
			!eaSize(&bg->ppMove) &&
			!eaSize(&bg->ppIgnores) &&
			vec3IsZero(bg->vecPush) &&
			!eaSize(&localBG->ppBitsFlash) &&
			!eaSize(&localBG->ppBitsPending) &&
			!eaSize(&localBG->ppFxFlash) &&
			!eaSize(&localBG->ppFxPending) &&
			!eaSize(&localBG->ppHitReacts) &&
			!eaSize(&localBG->ppConstantForcesQueued) &&
			!eaSize(&localBG->ppConstantForcesActive) &&
			!bg->erSelectedTarget)
		{
			mrmReleaseAllDataOwnershipBG(msg);

			mrmHandledMsgsRemoveBG(	msg,
									MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP |
										MR_HANDLED_MSG_CREATE_DETAILS);
		}else{
			mrmHandledMsgsAddBG(msg,
								MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP |
									MR_HANDLED_MSG_CREATE_DETAILS);
		}
	}
	setVec3same(toBG->vecPush,0);
}

static void pmHandleMsgCreateToBG(	const MovementRequesterMsg* msg,
									PowersMovementFG* fg,
									PowersMovementToBG* toBG)
{
	Entity *pent = NULL;

	if(fg->reset)
	{
		char *pchReset = NULL;
		PERFINFO_AUTO_START("reset", 1);
		mrmEnableMsgUpdatedToBG(msg);
				
		estrStackCreate(&pchReset);
		PrintPowersMovementFG(&pchReset,fg,toBG);
		mrmGetManagerUserPointerFG(msg,&pent);
		Errorf("PM.Reset.FG: %s: %s", pent?ENTDEBUGNAME(pent):"NULL", pchReset);
				
		mrmLog(msg,NULL,"[PM.Reset.FG] %s\n",pchReset);
		pmLog(0,msg,"PM.Reset.FG","%s\n",pchReset);
		estrDestroy(&pchReset);

		fg->reset = 0;
		PMPreDestroyFG(fg);	// Safety call to deal with duplicate pointers
		eaDestroyEx(&fg->ppBitsCancels,PMBitsDestroyUnsafe);
		eaDestroyEx(&fg->ppBitsUpdates,PMBitsDestroyUnsafe);
		eaDestroyEx(&fg->ppBitsFlashed,PMBitsDestroyUnsafe);
		eaDestroyEx(&fg->ppBitsStickied,PMBitsDestroyUnsafe);
		eaDestroyEx(&fg->ppFxCancels,PMFxDestroyUnsafe);
		eaDestroyEx(&fg->ppFxUpdates,PMFxDestroyUnsafe);
		eaDestroyEx(&fg->ppFxFlashed,PMFxDestroyUnsafe);
		eaDestroyEx(&fg->ppFxStickied,PMFxDestroyUnsafe);
		eaDestroyEx(&fg->ppHitReacts,PMHitReactDestroyUnsafe);
		eaDestroyEx(&fg->ppMove,PMMoveDestroyUnsafe);
		eaDestroyEx(&fg->ppIgnores,PMIgnoreDestroyUnsafe);
		eaDestroyEx(&fg->ppEvents,PMEventDestroyUnsafe);
		eaDestroyEx(&fg->ppConstantForces,NULL);
		
		toBG->reset = 1;
		eaDestroyEx(&toBG->ppBitsCancels,PMBitsDestroyUnsafe);
		eaDestroyEx(&toBG->ppBitsUpdates,PMBitsDestroyUnsafe);
		eaDestroyEx(&toBG->ppFxCancels,PMFxDestroyUnsafe);
		eaDestroyEx(&toBG->ppFxUpdates,PMFxDestroyUnsafe);
		eaDestroyEx(&toBG->ppHitReacts,PMHitReactDestroyUnsafe);
		eaDestroyEx(&toBG->ppMove,PMMoveDestroyUnsafe);
		eaDestroyEx(&toBG->ppIgnores,PMIgnoreDestroyUnsafe);
		eaDestroyEx(&toBG->ppConstantForces,NULL);
		PERFINFO_AUTO_STOP();
	}
	else
	{
		S32 needsCreateToBG = 0;
		S32 updatedToBG = 0;
				
		// Check events first
		PMEvent_FG_CREATE_TOBG(msg,fg);

		// Target update
		if(fg->bTargetUpdate)
		{
			toBG->erSelectedTarget = fg->erSelectedTarget;
			toBG->bTargetUpdate = true;
			updatedToBG = true;
			fg->bTargetUpdate = false;
		}

		// Lurch update
		if (fg->lurchUpdate.uchActID)
		{
			StructCopyAll(parse_PowersMovementLurchUpdate, &fg->lurchUpdate, &toBG->lurchUpdate);
			StructReset(parse_PowersMovementLurchUpdate, &fg->lurchUpdate);
		}

		PMBits_FG_CREATE_TOBG(msg,fg,toBG,&updatedToBG,&needsCreateToBG);

		PMFx_FG_CREATE_TOBG(msg,fg,toBG,&updatedToBG,&needsCreateToBG);

		PMHitReact_FG_CREATE_TOBG(msg,fg,toBG,&updatedToBG,&needsCreateToBG);

		PMIgnore_FG_CREATE_TOBG(msg,fg,toBG,&updatedToBG,&needsCreateToBG);

		PMMove_FG_CREATE_TOBG(msg,fg,toBG,&updatedToBG,&needsCreateToBG);

		if(!vec3IsZero(fg->vecPush))
		{
			addVec3(fg->vecPush,toBG->vecPush,toBG->vecPush);
			toBG->uiTimePush = fg->uiTimePush;
			updatedToBG = 1;
		}

		PMConstantForce_FG_CREATE_TOBG(msg, fg, toBG, &updatedToBG);
		
		if(updatedToBG){
			mrmEnableMsgUpdatedToBG(msg);
		}
				
		if(needsCreateToBG){
			mrmEnableMsgCreateToBG(msg);
		}
	}
	setVec3same(fg->vecPush,0);
}

static void pmHandleMsgUpdatedToFG(	const MovementRequesterMsg* msg,
									PowersMovementFG* fg,
									PowersMovementToFG* toFG)
{
	Entity *pent = NULL;

	if(toFG->idLungeActivate)
	{
		if(mrmGetManagerUserPointerFG(msg,&pent) && pent && pent->pChar)
		{
			character_ActLungeActivate(pent->pChar,toFG->idLungeActivate);
		}
		toFG->idLungeActivate = 0;
	}

	if(toFG->idLungeFinished)
	{
		if(mrmGetManagerUserPointerFG(msg,&pent) && pent && pent->pChar)
		{
			character_ActLungeFinished(pent->pChar,toFG->idLungeFinished,toFG->vecLungeFinished);
		}
		toFG->idLungeFinished = 0;
	}
}

// This function is one of several handlers.  It's basically a big virtual table for a plug-in system.
// It can be called from either the foreground or the background thread.
void powersMovementMsgHandler(const MovementRequesterMsg* msg){
	PowersMovementFG*		fg;
	PowersMovementBG*		bg;
	PowersMovementLocalBG*	localBG;
	PowersMovementToFG*		toFG;
	PowersMovementToBG*		toBG;
	PowersMovementSync*		sync;
	
	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, PowersMovement);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_BEFORE_DESTROY:{
			PMPreDestroyFG(fg);
		}

		xcase MR_MSG_FG_UPDATED_TOFG:{
			pmHandleMsgUpdatedToFG(msg, fg, toFG);
		}

		xcase MR_MSG_FG_CREATE_TOBG:{
			pmHandleMsgCreateToBG(msg, fg, toBG);
		}

		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;
			char*	estr = NULL;
			
			estrStackCreate(&estr);
			PrintPowersMovementBG(&estr,bg,localBG);

			snprintf_s(	buffer,
						bufferLen,
						"%s",
						estr);

			estrDestroy(&estr);
		}

		xcase MR_MSG_BG_UPDATED_TOBG:{
			pmHandleMsgUpdatedToBG(msg, bg, localBG, toFG, toBG);
		}

		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			if(!msg->in.bg.discussDataOwnership.flags.isDuringCreateOutput){
				pmHandleMsgDiscussDataOwnership(msg, bg, localBG, toFG);
			}
		}

		xcase MR_MSG_BG_COLLIDED_ENT:{
			if(bg->moving){
				PMMove *pMove = pmGetCurrentPMMove (bg->ppMove);

				if(pMove){
					pMove->stopLurching = true;
				}
			}
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:{
			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_POSITION_TARGET:{
					pmHandleMsgCreateOutputPositionTarget(msg, bg, localBG);
				}

				xcase MDC_BIT_ROTATION_TARGET:{
					pmHandleMsgCreateOutputRotationTarget(msg, bg, localBG);
				}

				xcase MDC_BIT_ANIMATION:{
					pmHandleMsgCreateOutputAnimation(msg, bg, localBG);
				}

				xdefault:{
					assert(0);
				}
			}
		}

		xcase MR_MSG_BG_CREATE_DETAILS:{
			// Create details actually sets all the bits and fx in motion
			PMBits_BG_CREATE_DETAILS(msg,bg,localBG);
			PMFx_BG_CREATE_DETAILS(msg,bg,localBG);
			PMHitReact_BG_CREATE_DETAILS(msg,bg,localBG);
		}

		xcase MR_MSG_BG_BEFORE_REPREDICT:{
			while(eaSize(&localBG->ppBitsStances)){
				PMBitsStances* s = eaPop(&localBG->ppBitsStancesMutable);
				PMBitsStancesDestroy(NULL, &s);
			}

			EARRAY_CONST_FOREACH_BEGIN(bg->ppBitsSticky, i, isize);
			{
				PMBitsStances* s;
				PMBitsStancesCreate(msg, &s, bg->ppBitsSticky[i]);
				eaPush(&localBG->ppBitsStancesMutable, s);
			}
			EARRAY_FOREACH_END;
		}

		xcase MR_MSG_BG_QUERY_LURCH_INFO:
		{
			PMMove* pMove = pmGetCurrentPMMove(bg->ppMove);
			if (pMove && pMove->type == kPowerMoveType_Lurch)
			{
				msg->out->bg.queryLurchInfo.erTarget = pMove->erTarget;
				msg->out->bg.queryLurchInfo.addedRadius = pMove->entCollisionCapsuleBuffer;
			}
			else
			{
				msg->out->bg.queryLurchInfo.addedRadius = 0.f;
				msg->out->bg.queryLurchInfo.erTarget = 0;
			}
		}
	}
}

void powersMovementCreate(	MovementRequester** mrOut,
							MovementManager* mm)
{
	mmRequesterCreateBasic(mm, mrOut, powersMovementMsgHandler);
}

void pmReset(MovementRequester *mr)
{
	PowersMovementFG* fg = PMRequesterGetFG(mr);
	if(fg)
	{
		fg->reset = 1;
		mrEnableMsgCreateToBG(mr);
	}
}


S32 pmShouldUseRagdoll(Entity *e){
	#if !GAMESERVER
	if(!bUseRagdollOnClient)
	{
		return 0;
	}
	else
	#endif
	{
		if (bUseRagdollForKnockBack == 0)
			return 0;
		else if (bUseRagdollForKnockBack < 0 && !g_CombatConfig.bRagdollAvailable)
			return 0;
	}

	if (e->pPlayer)
		return 0;

	{
		PlayerCostume* pCostume = costumeEntity_GetEffectiveCostume(e);
		if (pCostume && pCostume->pArtistData && pCostume->pArtistData->bNoRagdoll)
			return 0;
	}


	return 1;
}

void calcBallisticVelocity(const Vec3 xzDir, F32 fDistance, F32 fG, F32 fTanAngle, Vec3 vOut)
{
	F32 fHorizontalVel;
	F32 fVerticalVel;

	fHorizontalVel = sqrtf( (fDistance * fG) / fTanAngle );
	fVerticalVel = fHorizontalVel * fTanAngle;
	copyVec3(xzDir, vOut);

	vOut[1] = 0.0f;
	normalVec3(vOut);

	scaleVec3(vOut, fHorizontalVel, vOut);
	vOut[1] = fVerticalVel;
	
	/*
	F32 fVel, fYaw;
	//F32 fVerticalVel;
	
	fTanAngle = sinf(2 * fTanAngle);
	fVel = sqrtf( (fDistance / fTanAngle) * fG );
	
	fYaw = getVec3Yaw(xzDir);
	sphericalCoordsToVec3(vOut, fYaw, fTanAngle, fVel);
	*/
}

// creates the projectile movement requester, and if a civilian makes sure we have a default movement requester
// 
static void pmCreateProjectileMovementRequester(Entity *e, MovementRequester **mrOut)
{
	mmRequesterCreateBasicByName(e->mm.movement, mrOut, "ProjectileMovement");
	
	if (entIsCivilian(e))
	{
		if (!e->mm.mrSurface) 
		{
			mrSurfaceCreate(e->mm.movement, &e->mm.mrSurface);
		}
		else
		{
			mrSurfaceSetEnabled(e->mm.mrSurface, true);
		}

#ifdef GAMESERVER
		aiCivilianSetKnocked(e);
#endif 
	}

}


// Starts a xy directional projectile requester on the given entity
void pmKnockBackStart(Entity *e, 
					  Vec3 vecDir, 
					  F32 fMagnitude,
					  U32 uiTime,
					  bool bInstantFacePlant,
					  bool bProneAtEnd,
					  F32 fTimer,
					  bool bIgnoreTravelTime)
{
	MovementRequester *mr;
	bool bRagdoll = false;
	Vec3 vecDirLocal;

	// Ballistic velocity code doesn't handle negative distance, so make a local copy of the dir and
	//  negate it manually.
	copyVec3(vecDir,vecDirLocal);
	if(fMagnitude<0)
	{
		fMagnitude *= -1;
		scaleVec3(vecDirLocal,-1,vecDirLocal);
	}

	if(	pmShouldUseRagdoll(e) &&
		!mrFlightGetEnabled(e->mm.mrFlight))
	{
		// If it already exists, just add the new velocity to it
		if (mmRequesterGetByNameFG(e->mm.movement, "RagdollMovement", &mr) && !mrRagdollEnded(mr))
		{
			Vec3 vecVelocity;
			// apparently fMagnitude is how far we should travel, and vecdir is the direction
			// solving the quadratic formula gives us a 
			calcBallisticVelocity(vecDirLocal, fMagnitude, RAGDOLL_GRAVITY, tanf(RAD(g_CombatConfig.fKnockPitch)), vecVelocity);

			bRagdoll = true;
			mrRagdollSetVelocity(mr,e,vecVelocity,zerovec3, 20.0f, uiTime);
		}
		else if (mmRequesterCreateBasicByName(e->mm.movement,&mr,"RagdollMovement"))
		{
			if (!mrRagdollSetup(mr, e, uiTime))
			{
				mrDestroy(&mr);
			}
			else
			{
				Vec3 vecVelocity;
				// apparently fMagnitude is how far we should travel, and vecdir is the direction
				// solving the quadratic formula gives us a 
				calcBallisticVelocity(vecDirLocal, fMagnitude, RAGDOLL_GRAVITY, tanf(RAD(g_CombatConfig.fKnockPitch)), vecVelocity);

				bRagdoll = true;
				mrRagdollSetVelocity(mr,e,vecVelocity,zerovec3, 20.0f, uiTime);
			}
		}
	}

	if (!bRagdoll)
	{
#if 0
		// Target-based knockback
		Vec3 vecStart;
		Vec3 vecTarget;
		entGetCombatPosDir(e,NULL,vecStart,NULL);
		copyVec3(vecDirLocal,vecTarget);
		vecTarget[1] = 0;
		normalVec3(vecTarget);
		scaleVec3(vecTarget,fMagnitude,vecTarget);
		addVec3(vecStart,vecTarget,vecTarget);
		mmRequesterCreateBasicByName(e->movement,&mr,"ProjectileMovement");
		mrProjectileStartWithTarget(mr,vecTarget,uiTime,g_CombatConfig.bKnockLowAngle,fTimer);
#else
		// Velocity-based knockback
		Vec3 vecVelocity;
		F32 fPitch = g_CombatConfig.fKnockPitch;
		
		if(g_CombatConfig.bKnockLowAngle)
		{
			// Simple low-pitch calculation (should be removed once a reasonable pitch is found for
			//  projects that want low pitch).  Does not get a magnitude bonus because the low pitch
			//  makes the target slide much farther on the ground.  Although apparently if the net vertical
			//  velocity result isn't high enough the target slides on the ground the whole way, which actually
			//  reduces the net distance.  No idea where that new feature came from.
			fPitch /= 2;
			fMagnitude *= 0.5f;
		}
		else if (fMagnitude <= 30)
		{
			// since the ballistic calculations don't take friction into account, I have to 
			// arbitrarily scale back the magnitude for anything under 30. 
			// hacky, yes. 
			fMagnitude *= 0.8f;
		}

		// apparently fMagnitude is how far we should travel, and vecdir is the direction
		// solving the quadratic formula gives us a 
		calcBallisticVelocity(vecDirLocal, fMagnitude, 80.f, tanf(RAD(fPitch)), vecVelocity);
		pmCreateProjectileMovementRequester(e, &mr);
		mrProjectileStartWithVelocity(mr,e,vecVelocity,uiTime, bInstantFacePlant, bProneAtEnd, fTimer, bIgnoreTravelTime);
#endif

	}
}

// Starts a vertical projectile requester on the given entity
void pmKnockUpStart(Entity *e, 
					F32 fMagnitude,
					U32 uiTime,
					bool bInstantFacePlant,
					bool bProneAtEnd,
					F32 fTimer,
					bool bIgnoreTravelTime)
{
	MovementRequester *mr;
	Vec3 vecVelocity;
	F32 fTime = sqrt(fMagnitude / 40.f);
	F32 fVel = (fMagnitude + 40.f * fTime * fTime) / fTime;
	bool bRagdoll = false;
	setVec3(vecVelocity,0,fVel,0);

	if (pmShouldUseRagdoll(e) && !mrFlightGetEnabled(e->mm.mrFlight))
	{
#ifdef GAMESERVER
		// If it already exists, just add the new velocity to it
		if (mmRequesterGetByNameFG(e->mm.movement, "RagdollMovement", &mr) && !mrRagdollEnded(mr))
		{
			bRagdoll = true;
			mrRagdollSetVelocity(mr,e,vecVelocity,zerovec3, 20.0f, uiTime);
		}
		else if (mmRequesterCreateBasicByName(e->mm.movement,&mr,"RagdollMovement"))
		{
			if (!mrRagdollSetup(mr, e, uiTime))
			{
				mrDestroy(&mr);
			}
			else
			{
				bRagdoll = true;
				mrRagdollSetVelocity(mr,e,vecVelocity,zerovec3, 20.0f, uiTime);
			}
		}
#else
		bRagdoll = true;
#endif
	}

	if (!bRagdoll)
	{
		pmCreateProjectileMovementRequester(e, &mr);
		mrProjectileStartWithVelocity(mr, e, vecVelocity, uiTime, bInstantFacePlant, bProneAtEnd, fTimer, bIgnoreTravelTime);
	}
}

// Starts a direct projectile requester on the given entity
void pmKnockToStart(Entity *e,
					Vec3 vecTarget,
					U32 uiTime,
					bool bInstantFacePlant,
					bool bProneAtEnd,
					F32 fTimer,
					bool bIgnoreTravelTime)
{
	MovementRequester *mr;
	/*
	bool bRagdoll = false;

	if (bUseRagdollForKnockBack)
	{
		if (mmRequesterCreateBasicByName(e->movement,&mr,"RagdollMovement"))
		{
			if (mrRagdollSetup(mr, e, uiTime))
			{
				bRagdoll = true;
				//mrProjectileStartWithVelocity(mr,vecVelocity,uiTime);
			}
		}
	}

	if (!bRagdoll)
	*/
	{
		pmCreateProjectileMovementRequester(e, &mr);
		mrProjectileStartWithTarget(mr, e, 
									vecTarget,
									uiTime,
									g_CombatConfig.bKnockLowAngle,
									mrFlightGetEnabled(e->mm.mrFlight), 
									bInstantFacePlant,
									bProneAtEnd,
									fTimer,
									bIgnoreTravelTime);
	}
}

// Returns true if this entity is controlled by a projectile requested
bool pmKnockIsActive(Entity *e)
{
	MovementRequester *mr;
	mmRequesterGetByNameFG(e->mm.movement, "RagdollMovement", &mr);
	if (!mr)
		mmRequesterGetByNameFG(e->mm.movement, "ProjectileMovement", &mr);
	return (NULL!=mr);
}

// Starts a push requester on the given entity
void pmPushStart(Entity *be,
				 Vec3 vecDir,
				 F32 fMagnitude,
				 U32 uiTime)
{
	MovementRequester *mr;
	Vec3 vecVelocity;
	copyVec3(vecDir,vecVelocity);
	normalVec3(vecVelocity);
	if(fMagnitude < 0)
	{
		scaleVec3(vecVelocity,-1,vecVelocity);
		fMagnitude *= -1;
	}
	fMagnitude *= 10.f / sqrt(MAX(fMagnitude,1.f));
	scaleVec3(vecVelocity,fMagnitude,vecVelocity);

	if(be->pChar)
	{
		PowersMovementFG* fg;
		PM_CREATE_SAFE(be->pChar);
		mr = be->pChar->pPowersMovement;
		fg = PMRequesterGetFG(mr);
		if(fg){
			mrEnableMsgCreateToBG(mr);
			PMprintf(1,PDBG_OTHER,"      PUSH (%.2f %.2f %.2f)\n",vecParamsXYZ(vecVelocity));
			addVec3(vecVelocity,fg->vecPush,fg->vecPush);
			fg->uiTimePush = uiTime;
		}
	}

/*
	mmRequesterCreateBasicByName(&mr,be->movement,"PushMovement");
	mmPushStartWithVelocity(mr,vecVelocity,uiTime);
*/
}

void pmConstantForceStart(	Entity *pEnt,
							U32 uiID, 
							U32 uiStartTime,
							U32 uiStopTime,
							Vec3 vForce)
{
	MovementRequester *mr;
	PowersMovementFG* fg;

	if (!pEnt || !pEnt->pChar)
		return;

	PM_CREATE_SAFE(pEnt->pChar);
	
	mr = pEnt->pChar->pPowersMovement;
	fg = PMRequesterGetFG(mr);

	if (fg)
	{
		PMConstantForce	*repel = calloc(1, sizeof(PMConstantForce));

		repel->id = uiID;
		repel->spcStart = uiStartTime;
		repel->spcStop = uiStopTime;
		copyVec3(vForce, repel->vec);

		eaPush(&fg->ppConstantForces, repel);
		
		mrEnableMsgCreateToBG(mr);
	}
}

// will start a repel that will be relative to the given repeler entity
void pmConstantForceStartWithRepeller(	SA_PARAM_NN_VALID Entity *pEnt,
										U32 uiID, 
										U32 uiStartTime,
										U32 uiStopTime,
										EntityRef erRepeler,
										F32 fYawOffset,
										F32 fSpeed)
{
	MovementRequester *mr;
	PowersMovementFG* fg;

	if (!pEnt || !pEnt->pChar)
		return;

	PM_CREATE_SAFE(pEnt->pChar);
	
	mr = pEnt->pChar->pPowersMovement;
	fg = PMRequesterGetFG(mr);

	if (fg)
	{
		PMConstantForce	*repel = calloc(1, sizeof(PMConstantForce));

		repel->speed = fSpeed;
		repel->id = uiID;
		repel->spcStart = uiStartTime;
		repel->spcStop = uiStopTime;
		repel->erRepeler = erRepeler;
		repel->yawOffset = fYawOffset;
		
		eaPush(&fg->ppConstantForces, repel);
		
		mrEnableMsgCreateToBG(mr);
	}
}

void pmConstantForceStop(	SA_PARAM_NN_VALID Entity *pEnt,
							U32 uiID, 
							U32 uiTime)
{
	MovementRequester *mr;
	PowersMovementFG* fg;

	if (!pEnt || !pEnt->pChar)
		return;

	PM_CREATE_SAFE(pEnt->pChar);

	mr = pEnt->pChar->pPowersMovement;
	fg = PMRequesterGetFG(mr);

	if (fg)
	{
		PMConstantForce	*repel = calloc(1, sizeof(PMConstantForce));

		repel->id = uiID;
		repel->spcStart = uiTime;
		repel->spcStop = uiTime;

		eaPush(&fg->ppConstantForces, repel);

		mrEnableMsgCreateToBG(mr);
	}
}


// Turns entity to entity collisions on
void pmSetCollisionsEnabled(SA_PARAM_NN_VALID Entity *e)
{
	if(e->mm.mnchPowers){
		mmNoCollHandleDestroyFG(&e->mm.mnchPowers);
	}
}

// Turns entity to entity collisions off
void pmSetCollisionsDisabled(SA_PARAM_NN_VALID Entity *e)
{
	if(!e->mm.mnchPowers){
		mmNoCollHandleCreateFG(e->mm.movement, &e->mm.mnchPowers, __FILE__, __LINE__);
	}
}

// Sets flight mode on and applies the parameters
void pmSetFlightEnabled(Entity *e)
{
	// Objects don't get to fly
	if(IS_HANDLE_ACTIVE(e->hCreatorNode))
	{
		return;
	}

#ifdef GAMESERVER 
	if(!e->mm.mrFlight)
	{
		gslEntMovementCreateFlightRequester(e);
	}

	mrFlightSetEnabled(e->mm.mrFlight, true);

	pmUpdateFlightThrottle(e);
	pmInitializeFlightParams(e);

	if(e->aibase)
	{
		aiMovementSetFlying(e,e->aibase,true);
	}
#endif
}

// Sets flight mode off
void pmSetFlightDisabled(Entity *e)
{
	// Objects don't get to fly
	if(IS_HANDLE_ACTIVE(e->hCreatorNode))
	{
		return;
	}

#ifdef GAMESERVER
	if(!e->mm.mrFlight)
	{
		gslEntMovementCreateFlightRequester(e);
	}

	mrFlightSetEnabled(e->mm.mrFlight, false);

	if(e->aibase)
	{
		aiMovementSetFlying(e,e->aibase,false);
	}
#endif
}

// Update the movement thottle
void pmUpdateFlightThrottle(Entity* e)
{
	if(e->pPlayer)
	{
		mrFlightSetThrottle(e->mm.mrFlight,e->pPlayer->fMovementThrottle);
	}
	else
	{
		mrFlightSetThrottle(e->mm.mrFlight,1.f);
	}
}

// Updates flight parameters
void pmInitializeFlightParams(Entity *e)
{
	bool bIgnorePitchAllRots = g_CombatConfig.bFlightAllRotationTargetTypesIgnorePitch;
	bool bIgnorePitchPDRots = g_CombatConfig.bFlightPointAndDirectionRotationTypesIgnorePitch;
	AIConfig* config;
	RegionRules *pRegionRules;

	if(e->pPlayer)
	{
		mrFlightSetFakeRoll(e->mm.mrFlight,true);
	}
	else if (e->aibase && (config = aiGetConfig(e, e->aibase)))
	{
		mrFlightSetFakeRoll(e->mm.mrFlight,config->movementParams.bankWhenMoving);
	}
	else 
	{
		mrFlightSetFakeRoll(e->mm.mrFlight,false);
	}

	if (pRegionRules = getRegionRulesFromEnt(e))
	{
		if (pRegionRules->bFlightRotationIgnorePitch)
		{
			bIgnorePitchAllRots = true;
		}
	}

	mrFlightSetPointAndDirectionRotationsIgnorePitch(e->mm.mrFlight, bIgnorePitchPDRots);
	mrFlightSetAllRotationTypesIgnorePitch(e->mm.mrFlight, bIgnorePitchAllRots);
}

void pmUpdateFlightParams(Entity *e, bool useFakeRoll, bool ignorePitch, bool useJumpBit, bool constantForward)
{
	mrFlightSetFakeRoll(e->mm.mrFlight,useFakeRoll);
	mrFlightSetAllRotationTypesIgnorePitch(e->mm.mrFlight, ignorePitch);
	mrFlightSetUseJumpBit(e->mm.mrFlight, useJumpBit);
	mrFlightSetGlide(e->mm.mrFlight,constantForward);
}

// Sets the friction of the entity
void pmSetFriction(Entity *e, F32 f)
{
	mrSurfaceSetFriction(e->mm.mrSurface, f);
}

// Sets the traction of the entity
void pmSetTraction(Entity *e, F32 f)
{
	mrSurfaceSetTraction(e->mm.mrSurface, f);
}

// Sets the non-jumping gravity of the entity
void pmSetGravity(Entity *e, F32 f)
{
#if GAMECLIENT || GAMESERVER
	RegionRules *pRules = getRegionRulesFromEnt(e);

	if(pRules && pRules->fGravityMulti)
		f *= pRules->fGravityMulti;
#endif
	mrSurfaceSetGravity(e->mm.mrSurface, f);
}

// Updates the sprint parameters based on the combat state
void pmUpdateTacticalRunParams(Entity *e, F32 f, bool bIsInCombat)
{
	F32 fSpeedSprint, fRunCooldown, fRunDuration;
	TacticalRequesterSprintDef *pSprintDef = mrRequesterDef_GetSprintDefForEntity(e, NULL);

	if(entIsPlayer(e) || !g_CombatConfig.tactical.sprint.bCrittersIgnoreRunTimeouts)
	{
		if (bIsInCombat)
		{
			fRunCooldown = pSprintDef->fRunCooldownCombat;
			fRunDuration = pSprintDef->fRunMaxDurationSecondsCombat;
		}
		else
		{
			fRunCooldown = pSprintDef->fRunCooldown;
			fRunDuration = pSprintDef->fRunMaxDurationSeconds;
		}
	}
	else
	{
		fRunCooldown = 0.f;
		fRunDuration = 0.f;
	}

	if (bIsInCombat)
	{
		if (pSprintDef->fSpeedSprintCombat)
		{
			fSpeedSprint = pSprintDef->fSpeedSprintCombat;
		}
		else
		{
			fSpeedSprint = f * pSprintDef->fSpeedScaleSprintCombat;
		}		
	}
	else
	{
		if (pSprintDef->fSpeedSprint)
		{
			fSpeedSprint = pSprintDef->fSpeedSprint;
		}
		else
		{
			fSpeedSprint = f * pSprintDef->fSpeedScaleSprint;
		}		
	}
		
	mrTacticalSetRunParams(	e->mm.mrTactical, 
							!pSprintDef->bSprintDisabled,
							fSpeedSprint, 
							fRunDuration, 
							fRunCooldown,
							pSprintDef->bAutoSprint,
							g_CombatConfig.tactical.sprint.bSprintToggles);

	mrTacticalSetRunFuel(	e->mm.mrTactical,
							pSprintDef->fRunFuelRefillRate ? true : false,
							pSprintDef->fRunFuelRefillRate,
							pSprintDef->fRunFuelDelay);
}

// Updates a special 'InCombat' anim bit based on the combat state of the entity
void pmUpdateCombatAnimBit(Entity *e, bool bIsInCombat)
{
	if (e && e->pChar && g_CombatConfig.bEnableInCombatAnimBit)
	{
		if (bIsInCombat)
		{
			static const char** s_ppchAnimBits = NULL;
			if (!s_ppchAnimBits)
			{
				eaPush(&s_ppchAnimBits, allocAddString("INCOMBAT"));
			}
			mrSurfaceSetInCombat(e->mm.mrSurface, true);
			pmBitsStartSticky(e->pChar->pPowersMovement, 0, 0,
							  kPowerAnimFXType_Combat,
							  entGetRef(e),
							  pmTimestamp(0),
							  s_ppchAnimBits,
							  false,
							  false,
							  false);
		}
		else
		{
			mrSurfaceSetInCombat(e->mm.mrSurface, false);
			pmBitsStop(e->pChar->pPowersMovement, 0, 0, 
					   kPowerAnimFXType_Combat,
					   entGetRef(e),
					   pmTimestamp(0),
					   false);
		}
	}
}

void pmSetFlourishData(Entity *e, bool bEnabled, F32 fTimer)
{
	mrSurfaceSetFlourishData(	e->mm.mrSurface,
								bEnabled,
								fTimer);
}

// Sets the general speed? of the entity
void pmSetSpeed(Entity *e, F32 f)
{
	if(e->mm.mrSurface)
	{
		F32 fSpeedSlow = FIRST_IF_SET(	g_CombatConfig.fSlowMovementSpeed,
										MR_SURFACE_DEFAULT_SPEED_SLOW);
		
		mrSurfaceSetSpeed(e->mm.mrSurface, MR_SURFACE_SPEED_SLOW, fSpeedSlow);

		if(g_CombatConfig.bMakeMedRunSameAsFastRun)
		{
			mrSurfaceSetSpeed(e->mm.mrSurface, MR_SURFACE_SPEED_MEDIUM, f);
		}
		else
		{
			mrSurfaceSetSpeed(e->mm.mrSurface, MR_SURFACE_SPEED_MEDIUM, fSpeedSlow);
		}

		mrSurfaceSetSpeed(e->mm.mrSurface, MR_SURFACE_SPEED_FAST, f);

		mrSurfaceSetBackScale(e->mm.mrSurface,g_CombatConfig.fBackwardsRunScale);

		if (e->pChar)
		{
			CharacterClass *pCharacterClass = character_GetClassCurrent(e->pChar);
			if (pCharacterClass)
			{
				mrSurfaceSetSpeed(e->mm.mrSurface, MR_SURFACE_SPEED_NATIVE, pCharacterClass->fNativeSpeedRunning);
			}
		}
	}
	

#ifdef GAMESERVER
	if (entIsCivilian(e))
	{
		aiCivilianSetSpeed(e, f);
	}
#endif

	if(e->pChar && character_ClassTypeInTypes(e->pChar, g_CombatConfig.peTacticalMovementClassTypes)) 
	{
		CharacterClass *pCharacterClass = character_GetClassCurrent(e->pChar);
		TacticalRequesterRollDef *pRollDef = mrRequesterDef_GetRollDefForEntity(e, pCharacterClass);
		ControlSchemes *pControlSchemes = (e->pPlayer && e->pPlayer->pUI) ? e->pPlayer->pUI->pSchemes : NULL;
		bool bDoubleTapRoll = !g_CombatConfig.tactical.roll.bDisableDoubleTapRoll;
				
		#if GAMESERVER
			if(!e->mm.mrTactical){
				gslEntMovementCreateTacticalRequester(e);
			}
		#endif
		// todo: the speed scale can probably be set for the whole tactical requester instead of prescaling every different speed
		//	and resending it to the requester. 
		//	this will also allow me to send the whole def struct like the roll, instead of param-by-param

		mrTacticalIgnoreInput(e->mm.mrTactical, g_CombatConfig.tactical.bIgnoreAllTacticalInput);

		mrTacticalSetGlobalCooldown(e->mm.mrTactical, g_CombatConfig.tactical.fTacticalMoveGlobalCooldown);

		// If the combat config allows double tap to roll, then check to see if the player has it enabled in their control scheme
		if (bDoubleTapRoll && pControlSchemes && pControlSchemes->pchCurrent)
		{
			ControlScheme *pCurrentControlScheme = schemes_FindScheme(pControlSchemes, pControlSchemes->pchCurrent);
			if (pCurrentControlScheme)
			{
				bDoubleTapRoll = pCurrentControlScheme->bDoubleTapDirToRoll;
			}
		}

		mrTacticalSetRollParams(e->mm.mrTactical, 
								pRollDef, 
								g_CombatConfig.tactical.roll.bRollIgnoresGlobalCooldown,
								bDoubleTapRoll);

		pmUpdateTacticalRunParams(e, f, entIsInCombat(e));

		// update aim/crouching
		{
			TacticalRequesterAimDef *pAimDef = mrRequesterDef_GetAimDefForEntity(e, pCharacterClass);
			F32 fSpeedCrouch = f * pAimDef->fSpeedScaleCrouch;

			mrTacticalSetAimParams(	e->mm.mrTactical,
									pAimDef,
									fSpeedCrouch,
									true,
									g_CombatConfig.tactical.roll.bRollWhileCrouching,
									g_CombatConfig.tactical.aim.bAimIgnoresGlobalCooldown,
									g_CombatConfig.tactical.aim.bAimStrafes,
									g_CombatConfig.tactical.aim.bAimDisablesJump);

			mrTacticalSetCrouchParams(	e->mm.mrTactical,
										fSpeedCrouch,
										true);
		}
		
		character_updateTacticalRequirements(e->pChar);
	}
}

// Sets the jump height of the entity
void pmSetJumpHeight(Entity *e, F32 f)
{
	mrSurfaceSetJumpHeight(e->mm.mrSurface, f);
}

// Sets the jump traction of the entity
void pmSetJumpTraction(Entity *e, F32 f)
{
	mrSurfaceSetJumpTraction(e->mm.mrSurface, f);
}

// Sets the jump speed of the entity
void pmSetJumpSpeed(Entity *e, F32 f)
{
	mrSurfaceSetJumpSpeed(e->mm.mrSurface, f);
}

// Sets the jump gravity of the entity
void pmSetJumpGravity(Entity *e, F32 fUp, F32 fDown)
{
	mrSurfaceSetJumpGravity(e->mm.mrSurface, fUp, fDown);
}

// Sets the flight speed of the entity
void pmSetFlightSpeed(Entity *e, F32 f)
{
	mrFlightSetMaxSpeed(e->mm.mrFlight, f);
}

// Sets flight traction
void pmSetFlightTraction(Entity *e, F32 f)
{
	mrFlightSetTraction(e->mm.mrFlight, f);
}

// Sets flight friction
void pmSetFlightFriction(Entity *e, F32 f)
{
	mrFlightSetFriction(e->mm.mrFlight, f);
}

// Sets flight turn rate
void pmSetFlightTurnRate(Entity *e, F32 f)
{
	mrFlightSetTurnRate(e->mm.mrFlight, f);
}

// Sets flight gravity
void pmSetFlightGravity(Entity *e, F32 fGravityUp, F32 fGravityDown)
{
	mrFlightSetGravity(e->mm.mrFlight, fGravityUp, fGravityDown);
}

// Sets flight glide decent rate
void pmSetGlideDecent(Entity *e, F32 fGlideDecent)
{
	mrFlightSetGlideDecent(e->mm.mrFlight, fGlideDecent);
}

__forceinline static F32 character_GetFrictionRunning(Character *pchar)
{
	F32 f = pchar->pattrBasic->fFrictionRunning;
	if(f<=0)
	{
		f = !entIsPlayer(pchar->pEntParent) ?	MR_SURFACE_DEFAULT_CRITTER_FRICTION :
												MR_SURFACE_DEFAULT_PLAYER_FRICTION;
	}
	return f;
}

__forceinline static F32 character_GetTractionRunning(Character *pchar)
{
	F32 f = pchar->pattrBasic->fTractionRunning;
	if(f<=0)
	{
		f = !entIsPlayer(pchar->pEntParent) ?	MR_SURFACE_DEFAULT_CRITTER_TRACTION : 
												MR_SURFACE_DEFAULT_PLAYER_TRACTION;
	}
	return f;
}

// Sets all movement and control data for the Entity, given an optional set of attributes to delta against
void character_UpdateMovement(Character *pchar, CharacterAttribs *pAttribsOld)
{
	S32 bFlightChanged;

	PERFINFO_AUTO_START_FUNC();

	// Generic "surface" movement
	if(!pAttribsOld || pAttribsOld->fSpeedRunning != pchar->pattrBasic->fSpeedRunning)
	{
		F32 f = pchar->pattrBasic->fSpeedRunning;
		pmSetSpeed(pchar->pEntParent,f);
	}
	if(!pAttribsOld || pAttribsOld->fFrictionRunning != pchar->pattrBasic->fFrictionRunning)
	{
		pmSetFriction(pchar->pEntParent,character_GetFrictionRunning(pchar));
	}
	if(!pAttribsOld || pAttribsOld->fTractionRunning != pchar->pattrBasic->fTractionRunning)
	{
		pmSetTraction(pchar->pEntParent,character_GetTractionRunning(pchar));
	}
	if(!pAttribsOld || pAttribsOld->fGravity != pchar->pattrBasic->fGravity)
	{
		F32 f = MR_SURFACE_DEFAULT_GRAVITY * pchar->pattrBasic->fGravity;
		pmSetGravity(pchar->pEntParent,f);
	}

	// Jump movement
	if(!pAttribsOld || pAttribsOld->fHeightJumping != pchar->pattrBasic->fHeightJumping)
	{
		F32 f = pchar->pattrBasic->fHeightJumping;
		pmSetJumpHeight(pchar->pEntParent,f);
	}
	if(!pAttribsOld || pAttribsOld->fSpeedJumping != pchar->pattrBasic->fSpeedJumping)
	{
		F32 f = pchar->pattrBasic->fSpeedJumping;
		pmSetJumpSpeed(pchar->pEntParent,f);
	}
	if(!pAttribsOld || pAttribsOld->fTractionJumping != pchar->pattrBasic->fTractionJumping)
	{
		F32 f = pchar->pattrBasic->fTractionJumping;
		if(f>0)
		{
			pmSetJumpTraction(pchar->pEntParent,f);
		}
	}
	if(!pAttribsOld
		|| pAttribsOld->fGravityJumpingUp != pchar->pattrBasic->fGravityJumpingUp
		|| pAttribsOld->fGravityJumpingDown != pchar->pattrBasic->fGravityJumpingDown)
	{
		F32 fUp = MR_SURFACE_DEFAULT_GRAVITY * pchar->pattrBasic->fGravityJumpingUp;
		F32 fDown = MR_SURFACE_DEFAULT_GRAVITY * pchar->pattrBasic->fGravityJumpingDown;
		pmSetJumpGravity(pchar->pEntParent,fUp,fDown);
	}

	// Flight movement
	bFlightChanged = !pAttribsOld || (pAttribsOld->fFlight>0) != (pchar->pattrBasic->fFlight>0);
	if(pchar->pattrBasic->fFlight>0)
	{
		if(bFlightChanged)
		{
			pmSetFlightEnabled(pchar->pEntParent);
		}
		else if(pchar->bUpdateFlightParams)
			pmUpdateFlightThrottle(pchar->pEntParent);

		pchar->bUpdateFlightParams = false;

		if(bFlightChanged || pAttribsOld->fSpeedFlying != pchar->pattrBasic->fSpeedFlying)
		{
			F32 f = pchar->pattrBasic->fSpeedFlying;
			pmSetFlightSpeed(pchar->pEntParent, MAX(0, f));
		}
		if(bFlightChanged || pAttribsOld->fTractionFlying != pchar->pattrBasic->fTractionFlying)
		{
			F32 f = pchar->pattrBasic->fTractionFlying ? pchar->pattrBasic->fTractionFlying : 0.5f;
			pmSetFlightTraction(pchar->pEntParent, MAX(0, f));
		}
		if(bFlightChanged || pAttribsOld->fFrictionFlying != pchar->pattrBasic->fFrictionFlying)
		{
			F32 f = pchar->pattrBasic->fFrictionFlying ? pchar->pattrBasic->fFrictionFlying : 1.f;
			pmSetFlightFriction(pchar->pEntParent, MAX(0, f));
		}
		if(bFlightChanged || pAttribsOld->fTurnRateFlying != pchar->pattrBasic->fTurnRateFlying)
		{
			F32 f = pchar->pattrBasic->fTurnRateFlying;
			pmSetFlightTurnRate(pchar->pEntParent, MAX(0, RAD(f)));
		}
		if(bFlightChanged
			|| pAttribsOld->fGravityJumpingUp != pchar->pattrBasic->fGravityJumpingUp 
			|| pAttribsOld->fGravityJumpingDown != pchar->pattrBasic->fGravityJumpingDown)
		{
			F32 fUp = pchar->pattrBasic->fGravityJumpingUp == 1.0f ? 0.0f : pchar->pattrBasic->fGravityJumpingUp;
			F32 fDown = pchar->pattrBasic->fGravityJumpingDown == 1.0f ? 0.0f : pchar->pattrBasic->fGravityJumpingDown;
			pmSetFlightGravity(pchar->pEntParent, fUp, fDown);
		}
		if(bFlightChanged
			|| pAttribsOld->fFlightGlideDecent != pchar->pattrBasic->fFlightGlideDecent)
		{
			pmSetGlideDecent(pchar->pEntParent,pchar->pattrBasic->fFlightGlideDecent == 1.0f ? 0.0f : pchar->pattrBasic->fFlightGlideDecent);
		}

		
	}
	else if(bFlightChanged)
	{
		pmSetFlightDisabled(pchar->pEntParent);
		mrFlightSetUseThrottle(pchar->pEntParent->mm.mrFlight, false);
		pmUpdateFlightParams(pchar->pEntParent, false, false, false, false);
	}

	// Swinging movement
	if(!pAttribsOld || (pAttribsOld->fSwinging>0) != (pchar->pattrBasic->fSwinging>0))
	{
		if(pchar->pattrBasic->fSwinging>0)
		{
			MovementRequester*	swingMr = NULL;
			mmRequesterCreateBasicByName(SAFE_MEMBER(pchar->pEntParent, mm.movement), &swingMr, "SwingMovement");
			if (swingMr)
			{
				mrSwingSetMaxSpeed(swingMr, pchar->pattrBasic->fSpeedFlying);
				mrSwingSetFx(swingMr, pchar->pcSwingingFX);
			}
		}
		else
		{
			MovementRequester*	swingMr;
			if(mmRequesterGetByNameFG(SAFE_MEMBER(pchar->pEntParent, mm.movement), "SwingMovement", &swingMr))
			{
				mrDestroy(&swingMr);
			}
		}
	}
	else if (pchar->pattrBasic->fSwinging>0 && pAttribsOld->fSpeedFlying != pchar->pattrBasic->fSpeedFlying)
	{
		MovementRequester*	swingMr;
		if(mmRequesterGetByNameFG(SAFE_MEMBER(pchar->pEntParent, mm.movement), "SwingMovement", &swingMr))
		{
			mrSwingSetMaxSpeed(swingMr, pchar->pattrBasic->fSpeedFlying);
			mrSwingSetFx(swingMr, pchar->pcSwingingFX);
		}
	}

	if(	entIsAlive(pchar->pEntParent) && 
		(!pAttribsOld || (pAttribsOld->fNoCollision>0) != (pchar->pattrBasic->fNoCollision>0)) )
	{
		if(pchar->pattrBasic->fNoCollision>0)
		{
			pmSetCollisionsDisabled(pchar->pEntParent);
		}
		else
		{
			pmSetCollisionsEnabled(pchar->pEntParent);
		}
	}

	if (pchar->pEntParent->mm.mrDragon)
	{
		if (!pAttribsOld || (pAttribsOld->fSpeedRunning != pchar->pattrBasic->fSpeedRunning))
		{
			mrDragon_SetMaxSpeed(pchar->pEntParent->mm.mrDragon, pchar->pattrBasic->fSpeedRunning);
		}
		if (!pAttribsOld || (pAttribsOld->fTractionRunning != pchar->pattrBasic->fTractionRunning))
		{
			mrDragon_SetTraction(pchar->pEntParent->mm.mrDragon, character_GetTractionRunning(pchar));
		}
		if (!pAttribsOld || (pAttribsOld->fFrictionRunning != pchar->pattrBasic->fFrictionRunning))
		{
			mrDragon_SetFriction(pchar->pEntParent->mm.mrDragon, character_GetFrictionRunning(pchar));
		}
	}
	
	PERFINFO_AUTO_STOP();
}

static S32 EntCanTorsoPoint(Entity *e)
{
	S32 bTorsoPointing = false;
	
	PlayerCostume *pCostume = costumeEntity_GetEffectiveCostume(e);
	if(pCostume)
	{
		PCSkeletonDef *pSkelDef = GET_REF(pCostume->hSkeleton);
		if(pSkelDef)
		{
			bTorsoPointing = pSkelDef->bTorsoPointing;
		}
	}
	return bTorsoPointing;
}

static bool pmHasSelectedTarget(SA_PARAM_NN_VALID Entity *e)
{
	if (e && e->pChar && e->pChar->pPowersMovement)
	{
		MovementRequester *mr = e->pChar->pPowersMovement;
		PowersMovementFG* fg = PMRequesterGetFG(mr);

		return fg && fg->erSelectedTarget;
	}

	return false;
}

// Updates PowersMovement's selected target for selected facing.
//  This will tend to cause PowersMovement to try and set the Entity's
//  facing towards the Entity's selected target.
// Note that this is NOT used to control facing with respect to any Power activation.
//  Activate facing is handled with the PMMove structure, which overrides select facing.
void pmUpdateSelectedTarget(Entity *e, S32 bForce)
{
	if(e && e->pChar)
	{
		bool bUpdate = false;
		bool bFacingSupported = !e->pChar->bDisableFaceSelected && EntCanTorsoPoint(e); 
		bool bIgnoreTarget = e->pChar->bFaceSelectedIgnoreTarget;// || e->pChar->bFaceSelectedIgnoreTargetSystem;
		EntityRef erTarget = 0;

		if(bForce
			|| bFacingSupported
			|| !e->pChar->currentTargetRef 
			|| ((bFacingSupported|| bIgnoreTarget) && pmHasSelectedTarget(e)))
		{
			bool bFaceTarget = false;
			if (!bIgnoreTarget)
			{
				int iPartitionIdx = entGetPartitionIdx(e);
				Entity *peTarget = entFromEntityRef(iPartitionIdx, e->pChar->currentTargetRef);

				if (!peTarget || 
					(!entIsCivilian(peTarget) && 
					 !character_TargetMatchesTypeRequire(iPartitionIdx, e->pChar, peTarget->pChar, kTargetType_Friend)))
				{
					bFaceTarget = true;
				}
			}
			erTarget = bFaceTarget ? e->pChar->currentTargetRef : 0;
			bUpdate = true;
		}

		if(bUpdate)
		{
			PowersMovementFG* fg;
			MovementRequester *mr;
			PM_CREATE_SAFE(e->pChar);
			mr = e->pChar->pPowersMovement;
			fg = PMRequesterGetFG(mr);
			if (fg)
			{
				fg->erSelectedTarget = erTarget;
				fg->bTargetUpdate = true;
				mrEnableMsgCreateToBG(mr);
			}
		}		
	}
}

// Changes the state of various flags on the Entity that controls how it does selected facing
void pmEnableFaceSelected(SA_PARAM_OP_VALID Entity *e, S32 bEnable)
{
	if(e && e->pChar)
	{
		// FIXME JW: This function confuses the hell out of me because
		//  it's setting both the flags that control selected
		//  facing instead of just one of them, but since only
		//  the AI uses it I'm going to ignore it for now.
		U32 bDisabled = !bEnable;
		if(	bDisabled != e->pChar->bFaceSelectedIgnoreTarget || 
			bDisabled != e->pChar->bDisableFaceSelected )
		{
			e->pChar->bFaceSelectedIgnoreTarget = bDisabled;
			e->pChar->bDisableFaceSelected = bDisabled;
			entity_SetDirtyBit(e, parse_Character, e->pChar, false);
			pmUpdateSelectedTarget(e, true);
		}
	}
}

// Returns the Entity's selected target according to the foreground
EntityRef pmGetSelectedTarget(SA_PARAM_NN_VALID Entity *e)
{
	if (e && e->pChar && e->pChar->pPowersMovement)
	{
		MovementRequester *mr;
		PowersMovementFG* fg;

		mr = e->pChar->pPowersMovement;
		fg = PMRequesterGetFG(mr);
		return SAFE_MEMBER(fg, erSelectedTarget);
	}

	return 0;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE;
void powersSetDebugLevelRemoteC(int d)
{
	s_iPMDebug = d;
	printf("New PDBG Level: %d\n", d);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void powersSetDebugLevelRemoteS(int d)
{
	s_iPMDebug = d;
	printf("New PDBG Level: %d\n", d);
}


AUTO_COMMAND ACMD_NAME(pDebugLevel) ACMD_CATEGORY(Debug, Powers);
void powersSetDebugLevel(Entity *e, int d)
{
#ifdef GAMECLIENT
	ServerCmd_powersSetDebugLevelRemoteS(d);
	powersSetDebugLevelRemoteC(d);
#elif defined(GAMESERVER)
	ClientCmd_powersSetDebugLevelRemoteC(e, d);
	powersSetDebugLevelRemoteS(d);
#endif
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void powersToggleDebugTypeRemoteS(int type)
{
	char bits[MAX_PATH];
	s_iPMDebugType = s_iPMDebugType ^ type;	

	strcpy_s(bits, MAX_PATH, "DebugTypes:");
	for(type=1; type<ARRAY_SIZE(s_dbgtypelist)-1; type++)
	{
		StaticDefineInt *sdi = &s_dbgtypelist[type];
		if(sdi->value & s_iPMDebugType)
		{
			strcat_s(bits, MAX_PATH, " ");
			strcat_s(bits, MAX_PATH, sdi->key);
		}
	}

	strcat_s(bits, MAX_PATH, "\n");

	printf("%s", bits);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE;
void powersToggleDebugTypeRemoteC(int type)
{
	char bits[MAX_PATH];
	s_iPMDebugType = s_iPMDebugType ^ type;	

	strcpy_s(bits, MAX_PATH, "DebugTypes:");
	for(type=1; type<ARRAY_SIZE(s_dbgtypelist)-1; type++)
	{
		StaticDefineInt *sdi = &s_dbgtypelist[type];
		if(sdi->value & s_iPMDebugType)
		{
			strcat_s(bits, MAX_PATH, " ");
			strcat_s(bits, MAX_PATH, sdi->key);
		}
	}

	strcat_s(bits, MAX_PATH, "\n");

	printf("%s", bits);
}



AUTO_COMMAND ACMD_NAME(pDebugToggle) ACMD_CATEGORY(Debug, Powers);
void powersToggleDebugType(Entity *e, ACMD_NAMELIST(s_dbgtypelist, STATICDEFINE) char *str)
{
	PowersDebugPrint type = StaticDefineIntGetInt(s_dbgtypelist, str);
	
#ifdef GAMECLIENT
	ServerCmd_powersToggleDebugTypeRemoteS(type);
	powersToggleDebugTypeRemoteC(type);
#elif defined(GAMESERVER)
	ClientCmd_powersToggleDebugTypeRemoteC(e, type);
	powersToggleDebugTypeRemoteS(type);
#endif
}

// Powers timing functions - Should be used across the powers system for 
//  non-persisted timestamps.

#define TIMESTAMP_TICKS_PER_SECOND (MM_PROCESS_COUNTS_PER_SECOND)


// Returns timestamp offset by the given number of seconds from now
// This timestamp is based on when this server started and should not be used for anything that might change servers.
U32 pmTimestamp(F32 fSecondsOffset)
{
	return mmGetProcessCountAfterSecondsFG(fSecondsOffset);
}

// Returns timestamp offset by the given number of seconds from the input time
// This timestamp is based on when this server started and should not be used for anything that might change servers.
U32 pmTimestampFrom(U32 uiTimestampFrom, F32 fSecondsOffset)
{
	return (U32)(uiTimestampFrom + TIMESTAMP_TICKS_PER_SECOND * fSecondsOffset);
}

// Returns difference between now and input time, in seconds.
//  Positive values indicate the given timestamp is in the future.
//  This timestamp is based on when this server started and should not be used for anything that might change servers.
F32 pmTimeUntil(U32 uiTimestampFrom)
{
	return ((S64)uiTimestampFrom - (S64)pmTimestamp(0))/(F32)TIMESTAMP_TICKS_PER_SECOND;
}

#include "AutoGen/PowersMovement_c_ast.c"
