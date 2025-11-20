#include "auto_float.h"
#include "cpu_count.h"
#include "endian.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "LineDist.h"
#include "MemoryBudget.h"
#include "MemoryPool.h"
#include "MultiWorkerThread.h"
#include "qsortG.h"
#include "rand.h"
#include "StringCache.h"
#include "strings_opt.h"
#include "ThreadSafeMemoryPool.h"
#include "XboxThreads.h"
#include "WorldGrid.h"
#include "mathutil.inl"

#include "dynAction.h"
#include "dynAnimExpression.h"
#include "dynAnimGraph.h"
#include "dynAnimGraphUpdater.h"
#include "dynAnimInterface.h"
#include "dynAnimNodeAlias.h"
#include "dynAnimOverride.h"
#include "dynAnimPhysics.h"
#include "dynAnimPhysInfo.h"
#include "dynAnimTrack.h"
#include "dynDraw.h"
#include "dynFx.h"
#include "dynFxDebug.h"
#include "dynFxParticle.h"
#include "dynFxPhysics.h"
#include "dynGroundReg.h"
#include "dynMoveTransition.h"
#include "dynNodeInline.h"
#include "dynRagdollData.h"
#include "dynSeqData.h"
#include "dynSequencer.h"
#include "dynSkeleton.h"
#include "dynSkeletonMovement.h"
#include "dynStrand.h"
#include "dynWind.h"

#include "wlCostume.h"
#include "wlPerf.h"
#include "wlState.h"

#include "autogen/dynSkeleton_h_ast.h"
#include "WorldLib_autogen_QueuedFuncs.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

#define DEFAULT_DYN_BASESKELETON_COUNT 32
#define DEFAULT_DYN_SKELETON_COUNT 32

#define SKEL_RADIUS_HACK 10.0f

#define MAX_ANIM_THREADS 6

#define BONE_TABLE_STARTING_SIZE 0x120

F32 fDefaultMountRiderScaleBlend = 0.8f; //default value to use when not set in CostumeConfig or on the CSkel

U32 uiMaxBonesPerSkeleton = 0;
static U32 uiMemPoolBoneCount = 200; // this is the max number of bones a mempooled skeleton node list can have. should automatically calculate it, but for now code it to 200
static U32 uiUnpooledSkeletonCount = 0;
const char* pcLargestSkel = NULL;

static U32 uiMaxRagdollCount = 100;
static U32 uiRagdollCount = 0; //be careful of threading when updating this

static MemoryPool mpSkeletonNodes = 0;
static bool bInDynSkeletonUpdateAll = false;

MultiWorkerThreadManager* pAnimThreadManager;

F32 fSkelUpdateDeltaTime;

DictionaryHandle hBaseSkeleton;

MP_DEFINE(DynBaseSkeleton);
MP_DEFINE(DynSkeleton);
TSMP_DEFINE(AnimUpdateOutputCommand);

DynSkeleton** eaDynSkeletons;

DynSkeleton* pPlayerSkeleton;
StashTable stPlayerBoneTable;

const char* pcExtents;
const char* pcFacespace;
const char* pcNodeNameHips;
const char* pcNodeNameBase;
const char* pcNodeNameFootL;
const char* pcNodeNameFootR;
const char* pcNodeNameToeL;
const char* pcNodeNameToeR;
const char* pcNodeNameSoleL;
const char* pcNodeNameSoleR;
const char* pcNodeNameHandL;
const char* pcNodeNameHandR;
const char* pcNodeNameWepL;
const char* pcNodeNameWepR;
const char* pcStanceTurnLeft;
const char* pcStanceTurnRight;
const char* pcStanceBankLeft;
const char* pcStanceBankRight;
const char* pcStanceTerrainTiltUp;
const char* pcStanceTerrainTiltDown;
const char* pcStanceRuntimeFreeze;
const char* pcStanceDeath;
const char* pcStanceNearDeath;
const char* pcStanceDisallowRagdoll;
const char* pcStanceJumping;
const char* pcStanceRising;
const char* pcStanceFalling;
const char* pcStanceJumpFalling;
const char* pcStanceJumpRising;
const char* pcStanceDragonTurn;
const char* pcStanceLunging;
const char* pcStanceLurching;
const char* pcStanceLanded;
const char* pcInterrupt;
const char* pcGruntJump;
const char* pcGruntLand;
const char* pcGruntKnockback;
const char* pcFlagStopped;

SkelBoneVisibilitySets g_SkelBoneVisSets = {0};

static CRITICAL_SECTION s_dynSkeletonFxSystemCriticalSection;

AUTO_RUN;
void dynSkeletonInitCriticalSections(void) {
	InitializeCriticalSection(&s_dynSkeletonFxSystemCriticalSection);
}

static void dynSkeletonLockFX(void) {
	EnterCriticalSection(&s_dynSkeletonFxSystemCriticalSection);
}

static void dynSkeletonUnlockFX(void) {
	LeaveCriticalSection(&s_dynSkeletonFxSystemCriticalSection);
}

typedef struct AnimUpdateOutputCommand
{
	ThreadDataProcessFunc pSubFunc;
	union
	{
		struct
		{
			DynFxManager* pFxManager;
			const char* pcFx;
		} call_fx;
		struct
		{
			Vec3 vPos;
			Quat qRot;
			DynPhysicsObject* pDPO;
		} physics_update;
		struct  
		{
			Mat4 mat;
			DynPhysicsObject* pDPO;
		} testragdoll_update;
		struct  
		{
			Vec3 vPos;
			Vec3 vVel;
			F32 fRadius;
		} wind_update;
		struct  
		{
			WLCostume* pMyCostume;
		} bodysock_update;
		struct
		{
			Vec3 vBonePos;
			Vec3 vImpactDir;
			DynSkeleton* pSkeleton;
			U32	uid;
		} react_trigger;
		WorldPerfFrameCounts perf_info;
	};
} AnimUpdateOutputCommand;

typedef struct DynSkeletonAnimOverride {
	U32							handle;
	const char*					keyword;
	const char**				stances;
	U32							playedKeyword : 1;
} DynSkeletonAnimOverride;

AUTO_RUN;
void dynSkeletonInitMemoryPools(void)
{
	TSMP_CREATE(AnimUpdateOutputCommand, DEFAULT_DYN_SKELETON_COUNT);
	MP_CREATE(DynBaseSkeleton, DEFAULT_DYN_BASESKELETON_COUNT);

    MP_CREATE_ALIGNED(DynSkeleton, DEFAULT_DYN_SKELETON_COUNT, 16);
}


AUTO_RUN;
void dynSkeleton_InitStrings(void)
{
	pcExtents   = allocAddStaticString("Extents");
	pcFacespace = allocAddStaticString("Facespace");

	pcNodeNameHips  = allocAddStaticString("Hips");
	pcNodeNameBase  = allocAddStaticString("Base");
	pcNodeNameFootL = allocAddStaticString("FootL");
	pcNodeNameFootR = allocAddStaticString("FootR");
	pcNodeNameToeL  = allocAddStaticString("ToeL");
	pcNodeNameToeR  = allocAddStaticString("ToeR");
	pcNodeNameSoleL = allocAddStaticString("Sole_L");
	pcNodeNameSoleR = allocAddStaticString("Sole_R");
	pcNodeNameHandL = allocAddStaticString("HandL");
	pcNodeNameHandR = allocAddStaticString("HandR");
	pcNodeNameWepL  = allocAddStaticString("WepL");
	pcNodeNameWepR  = allocAddStaticString("WepR");

	//using non-static version to avoid complaints from anim bit registry
	//also adding them as non-static strings

	pcStanceTurnLeft  = allocAddString("TurnLeft");
	pcStanceTurnRight = allocAddString("TurnRight");

	pcStanceBankLeft  = allocAddString("BankLeft");
	pcStanceBankRight = allocAddString("BankRight");

	pcStanceTerrainTiltDown = allocAddString("Down_Hill");
	pcStanceTerrainTiltUp   = allocAddString("Up_Hill");

	pcStanceDeath = allocAddString("Death");
	pcStanceNearDeath = allocAddString("NearDeath");
	pcStanceDisallowRagdoll = allocAddString("Disallow_Ragdoll");

	pcStanceJumping = allocAddString("Jumping");
	pcStanceRising  = allocAddString("Rising");
	pcStanceFalling = allocAddString("Falling");
	pcStanceLanded  = allocAddString("Landed");
	pcStanceJumpFalling = allocAddString("JumpFalling");
	pcStanceJumpRising  = allocAddString("JumpRising");

	pcStanceDragonTurn = allocAddString("DragonTurn");

	pcStanceLunging = allocAddString("Lunge");
	pcStanceLurching = allocAddString("Lurch");

	pcStanceRuntimeFreeze = allocAddString("RuntimeFreeze");
	pcInterrupt = allocAddString("Interrupt");

	pcGruntJump = allocAddString("Jump_Grunt");
	pcGruntLand = allocAddString("Land_Grunt");
	pcGruntKnockback = allocAddString("Knockback_Grunt");

	pcFlagStopped = allocAddString("Stopped");
}

AUTO_CMD_INT(uiMaxRagdollCount, danimSetMaxRagdollCount) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);

AUTO_COMMAND;
void danimCycleDebugSkeleton(void)
{
	bool bSetNext = false;
	FOR_EACH_IN_EARRAY_FORWARDS(eaDynSkeletons, DynSkeleton, pSkeleton)
	{
		if (bSetNext)
		{
			dynDebugState.pDebugSkeleton = pSkeleton;
			return;
		}
		if (dynDebugState.pDebugSkeleton == pSkeleton)
			bSetNext = true;
	}
	FOR_EACH_END;

	// If we got here we either wrapped around or never had one set, so use the first one (if it exists)
	dynDebugState.pDebugSkeleton = eaSize(&eaDynSkeletons) > 0 ? eaDynSkeletons[0] : NULL;
}


__forceinline static bool shouldUseMemPool(U32 uiNumBones)
{
	return ( (uiNumBones <= uiMemPoolBoneCount) && uiNumBones > (uiMemPoolBoneCount/2) );
}

static void dynSkeletonClearStanceWordInSet(DynSkeleton* pSkeleton, DynSkeletonStanceSetType set, const char* pcStance);
static void dynSkeletonClearStanceWordsInSet(DynSkeleton*, DynSkeletonStanceSetType set);
static void dynSkeletonClearNonListedStanceWordsInSet(DynSkeleton *pSkeleton, DynSkeletonStanceSetType set, const char **ppchExcludeStances);

static void dynSkeletonSetStanceWordInSet(DynSkeleton* pSkeleton, DynSkeletonStanceSetType set, const char* pcStance);
static void dynSkeletonSetStanceWordInternal(DynSkeleton* pSkeleton, const char* pcStance, U32 uiMovement);

#define dynSkeletonSwapStanceWordsInSet(pSkeleton, set, pcStanceAdd, pcStanceRemove) \
	dynSkeletonSetStanceWordInSet(pSkeleton, set, pcStanceAdd); \
	dynSkeletonClearStanceWordInSet(pSkeleton, set, pcStanceRemove)

static void dynSkeletonUpdateStanceState(DynSkeleton *pSkeleton);

static void dynSkeletonQueuePhysicsUpdate(DynSkeleton* pSkeleton);
static void dynSkeletonQueueTestRagdollUpdate(DynSkeleton* pSkeleton);
static void dynSkeletonPopulateStashTable(StashTable stBoneTable, SA_PARAM_NN_VALID DynNode* pRoot);
static void dynSkeletonQueueWindUpdate(DynSkeleton* pSkeleton);

static void dynSkeletonCalcTerrainPitchSetup(DynSkeleton *pSkeleton);
static void dynSkeletonCalcTerrainPitch(DynSkeleton *pSkeleton, F32 fDeltaTime);

static void dynSkeletonPlayAnimWordFeeds(DynSkeleton *pSkeleton);

static void dynSkeletonCreateGroundRegLimbs(DynSkeleton *pSkeleton, const DynGroundRegData *pGroundRegData);

static void dynSkeletonFreeNodeExpressions(DynSkeleton *pSkeleton);
static void dynSkeletonCreateNodeExpressions(DynSkeleton *pSkeleton, const DynAnimExpressionSet *pExpressionSet);
static void dynSkeletonRecreateNodeExpressions(DynSkeleton *pSkeleton, const DynAnimExpressionSet *pExpressionSet);

static void dynSkeletonCreateStrands(DynSkeleton *pSkeleton, const DynStrandDataSet *pStrandDataSet, bool bHeadshot);
static void dynSkeletonUpdateStrands(DynSkeleton *pSkeleton, F32 fDeltaTime);
static void dynSkeletonDestroyStrands(DynSkeleton *pSkeleton);

static S32 dynSkeletonClientSideRagdollCreate(DynSkeleton *pSkeleton, bool bCollisionTester);
static void dynSkeletonClientSideRagdollFree(DynSkeleton *pSkeleton);

static void dynSkeletonEndDeathAnimation(DynSkeleton *pSkeleton);

static void dynSkeletonBroadcastFXMessage(DynFxManager *pManager, const char *pcMessage);

static void dynSkeletonCreateBoneTable(StashTable* stOut)
{
	if(!*stOut)
	{
		*stOut = stashTableCreateWithStringKeys(BONE_TABLE_STARTING_SIZE, StashDefault);
	}
}

static void dynSkeletonShrinkBoneTable(StashTable* stInOut)
{
	StashTable stOld = SAFE_DEREF(stInOut);
	
	if(!stOld)
	{
		return;
	}
	
	if(stashGetCount(stOld) < 3 * stashGetMaxSize(stOld) / 4)
	{
		U32					newSize;
		StashTable			st;
		StashTableIterator	it;
		StashElement		elem;
		
		PERFINFO_AUTO_START_FUNC();

		newSize = stashGetCount(stOld) * 4 / 3;
		st = stashTableCreateWithStringKeys(newSize, StashDefault);

		stashGetIterator(stOld, &it);
		
		while(stashGetNextElement(&it, &elem))
		{
			if(!stashAddPointer(st,
								stashElementGetStringKey(elem),
								stashElementGetPointer(elem),
								false))
			{
				assert(0);
			}
		}
		
		stashTableDestroy(stOld);
		*stInOut = st;
		
		PERFINFO_AUTO_STOP();
	}
}

static bool dynSkeletonLoadFile(SA_PARAM_NN_STR const char* pcFileName)
{
	DynBaseSkeleton* pSkeleton;
	char* const pcOrigFileData = fileAlloc(pcFileName, NULL);
	const char* pcFileData = pcOrigFileData;

	
	if ( !pcOrigFileData )
	{
		Errorf("Failed to open file %s\n", pcFileName);
		return false;
	}

	pSkeleton = MP_ALLOC(DynBaseSkeleton);
	pSkeleton->pcFileName = allocAddString(pcFileName);

	// Read name
	{
		char pcName[128];
		U32 uiLen;
		fa_read(&uiLen, pcFileData, sizeof(uiLen));
		xbEndianSwapU32(uiLen);
		
		assertmsgf(uiLen < sizeof(pcName), "Skeleton file %s appears to be corrupt", 
			pcFileName);

		fa_read(pcName, pcFileData, uiLen+1);

		pSkeleton->pcName = allocAddString(pcName);
		RefSystem_AddReferent(hBaseSkeleton, pSkeleton->pcName, pSkeleton);

	}

	// Read tree
    {
		DynNode* pLoadRoot = dynNodeReadTree(&pcFileData, NULL, &pSkeleton->uiNumBones);
	    if (pLoadRoot->pSibling)
	    {
		    CharacterFileError(pcFileName, "In Skeleton %s, bone %s is not parented to the first root bone found: %s. If the first root bone found (%s) is not the root, check that it's correctly linked to the root. There can only be one root.", pSkeleton->pcName, pLoadRoot->pSibling->pcTag, pLoadRoot->pcTag, pLoadRoot->pcTag);
	    }

		// Insert scale helper node
		{
			DynNode* pExtentsNode = dynNodeAlloc();
			dynNodeParent(pExtentsNode, pLoadRoot);
			pExtentsNode->pcTag = pcExtents;
			pExtentsNode->uiTreeBranch = 1;
			++pSkeleton->uiNumBones;
		}

		// Insert facespace helper node
		if (gConf.bNewAnimationSystem)
		{
			DynNode *pFacespaceNode = dynNodeAlloc();
			dynNodeParent(pFacespaceNode, pLoadRoot);
			pFacespaceNode->pcTag = pcFacespace;
			pFacespaceNode->uiTreeBranch = 1;
			pFacespaceNode->uiTransformFlags &= ~ednAll;
			++pSkeleton->uiNumBones;
		}

	    // Copy tree into linear array
	    {
            DynNode* pNewRoot;
			DynNode* pCurrentNode;
/*
		    if (shouldUseMemPool(pSkeleton->uiNumBones))
			    pNewRoot = mpAlloc(mpSkeletonNodes);
		    else
*/
		    {
			    pNewRoot = aligned_calloc(sizeof(DynNode), pSkeleton->uiNumBones, 16);
			    ++uiUnpooledSkeletonCount;
		    }

            if (pSkeleton->uiNumBones > uiMaxBonesPerSkeleton)
		    {
			    uiMaxBonesPerSkeleton = pSkeleton->uiNumBones;
			    pcLargestSkel = allocAddString(pSkeleton->pcFileName);
		    }
			pCurrentNode = pNewRoot;
		    pSkeleton->pRoot = dynNodeTreeCopy(pLoadRoot, NULL, true, NULL, dynNodeLinearAllocator, &pCurrentNode);

			if ( (intptr_t)(pCurrentNode - pNewRoot) != (intptr_t)(pSkeleton->uiNumBones) )
			{
				FatalErrorf("Buffer overrun while allocating a base skeleton at load time.");
			}

            dynNodeFreeTree(pLoadRoot);
	    }
    }

	free(pcOrigFileData);

	if (!pSkeleton->pcName)
	{
		FatalErrorf("Failed to set name for skeleton %s\n", pcFileName);
		return false;
	}

	/*
	if (stricmp(pSkeleton->pcName, "CoreDefault/Skel_Core_Default")==0)
	{
		DynNode* pBase = pSkeleton->pRoot;
		DynNode* pHips = dynNodeFindByName(pSkeleton->pRoot, "Hips");
		DynNode* pRunAndGun = dynNodeAlloc(pHips);
		//DynNode* pHips2 = dynNodeAlloc();
		DynNode* pWaist = dynNodeFindByName(pSkeleton->pRoot, "Waist");
		pRunAndGun->pcTag = allocAddString("RunAndGun");
		//pHips2->pcTag = allocAddString("Hips2");

		dynNodeParent(pRunAndGun, pHips);
		//pRunAndGun->uiTransformFlags &= ~ednRot;
		//dynNodeParent(pHips2, pRunAndGun);
		dynNodeParent(pWaist, pRunAndGun);
	}
	*/

	dynSkeletonCreateBoneTable(&pSkeleton->stBoneTable);
	dynSkeletonPopulateStashTable(pSkeleton->stBoneTable, pSkeleton->pRoot);
	dynSkeletonShrinkBoneTable(&pSkeleton->stBoneTable);

	dynNodeCalculateLODLevels(pSkeleton->pRoot, NULL);

	pSkeleton->fHipsHeightAdjustmentFactor = 1.0f;

	return true;
}

static bool dynSkeletonReload(SA_PARAM_NN_STR const char* fullpath)
{
	char skelName[MAX_PATH];
	const char* s;
	DynBaseSkeleton* pOldRef;
	s = strstriConst(fullpath, "skeletons/") + 10;
	strcpy(skelName, s);
	strstriReplace(skelName, ".skel", "");
	pOldRef = RefSystem_ReferentFromString(hBaseSkeleton, skelName);
	if (pOldRef)
		RefSystem_RemoveReferent(pOldRef, false);
	// Note we don't free the old thing, in case it's being used
	// Just leak the skeleton (they are tiny, and this should only happen in development mode)
	return dynSkeletonLoadFile(fullpath);
}

static void dynSkeletonReloadCallback(SA_PARAM_NN_STR const char *relpath, int when)
{
	if (strstr(relpath, "/_"))
	{
		return;
	}

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);


	if (!dynSkeletonReload(relpath)) // reload file
	{
		ErrorFilenamef(relpath, "Error reloading .skel file: %s", relpath);
	}
	else
	{
		// nothing to do here
	}
}

static FileScanAction scanSkeletonFiles(char *dir, struct _finddata32_t *data, void *pUserData)
{
	static char *ext = ".skel";
	static int ext_len = 5; // strlen(ext);
	char filename[MAX_PATH];
	int len;

	if (data->name[0]=='_') return FSA_NO_EXPLORE_DIRECTORY;
	if (data->attrib & _A_SUBDIR)
		return FSA_EXPLORE_DIRECTORY;
	len = (int)strlen(data->name);
	if (len<ext_len || strnicmp(data->name + len - ext_len, ext, ext_len)!=0) // not a .wtex file
		return FSA_EXPLORE_DIRECTORY;

	STR_COMBINE_SSS(filename, dir, "/", data->name);

	// Do actual processing
	dynSkeletonLoadFile(filename);


	return FSA_EXPLORE_DIRECTORY;
}

AUTO_RUN;
void registerBaseSkeletonDictionary(void)
{
	hBaseSkeleton = RefSystem_RegisterSelfDefiningDictionary("BaseSkeleton", false, parse_DynBaseSkeleton, true, false, NULL);
}

DefineContext *g_pDefineSkelBoneVisSets = NULL;

void dynLoadSkelBoneVisibilitySets(void)
{
	int i, s;
	g_pDefineSkelBoneVisSets = DefineCreate();

	loadstart_printf("Loading SkelBoneVisibilitySets...");
	ParserLoadFiles(NULL, "defs/config/SkelBoneVisSets.def", "SkelBoneVisSets.bin", PARSER_OPTIONALFLAG, parse_SkelBoneVisibilitySets, &g_SkelBoneVisSets);
	s = eaSize(&g_SkelBoneVisSets.ppSetInfo);
	for(i=kSkelBoneVisSet_FIRST_DATA_DEFINED; i<s+kSkelBoneVisSet_FIRST_DATA_DEFINED; i++)
	{
		DefineAddInt(g_pDefineSkelBoneVisSets,g_SkelBoneVisSets.ppSetInfo[i]->pchName, i);
	}

	loadend_printf("done");
}


void dynLoadAllBaseSkeletons(void)
{
	char *pcDir="animation_library/skeletons";

	loadstart_printf("Loading Skeletons...");

	//dynAnimLoadFilesInDir(pcDir, pcFileType, )
	fileScanAllDataDirs(pcDir, scanSkeletonFiles, NULL);

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "animation_library/skeletons/*.skel", dynSkeletonReloadCallback);

	dynDebugState.fAnimRate = 1.0f;

	mpSkeletonNodes = createMemoryPoolNamed("mpSkeletonNodes" MEM_DBG_PARMS_INIT);
	mpSetChunkAlignment(mpSkeletonNodes, sizeof(DynNode));
	initMemoryPool(mpSkeletonNodes, sizeof(DynNode) * uiMemPoolBoneCount, DEFAULT_DYN_SKELETON_COUNT);
	mpSetMode(mpSkeletonNodes, ZeroMemoryBit);

	loadend_printf(" done (%d Skeletons).", RefSystem_GetDictionaryNumberOfReferents(hBaseSkeleton));
}


static void dynSkeletonPopulateStashTable(StashTable stBoneTable, DynNode* pRoot)
{
	DynNode* pLink = pRoot->pChild;
	if ( pRoot->pcTag )
		stashAddPointer(stBoneTable, pRoot->pcTag, pRoot, false);
	while ( pLink )
	{
		dynSkeletonPopulateStashTable(stBoneTable, pLink);
		pLink = pLink->pSibling;
	}
}


const DynBaseSkeleton* dynBaseSkeletonFind(const char* pcBaseSkeletonName)
{
	return RefSystem_ReferentFromString(hBaseSkeleton, pcBaseSkeletonName);
}

static void dynSkeletonSetInitialTorsoPointing(const SkelBlendInfo* pBlendInfo, DynSkeleton* pSkeleton)
{
	if (pBlendInfo)
	{
		pSkeleton->bUseTorsoDirections = pSkeleton->bTorsoDirections = pBlendInfo->bTorsoDirections;
		pSkeleton->bUseTorsoPointing   = pSkeleton->bTorsoPointing   = pBlendInfo->bTorsoPointing;
		pSkeleton->bMovementBlending   = (pBlendInfo->bMovementBlending || pBlendInfo->bTorsoPointing);
	} else {
		pSkeleton->bUseTorsoDirections = pSkeleton->bTorsoDirections = false;
		pSkeleton->bUseTorsoPointing   = pSkeleton->bTorsoPointing   = false;
		pSkeleton->bMovementBlending   = false;
	}
	zeroVec3(pSkeleton->vOldToTargetWS);
	pSkeleton->fRunAndGunMultiJointBlend = 1.f;
	pSkeleton->fMovementYawStopped = 0.f;
}

static void dynSkeletonSetRunAndGunBones(const SkelBlendInfo* pBlendInfo, DynSkeleton *pSkeleton)
{
	bool foundPrimary = false;

	if (pBlendInfo) {
		pSkeleton->bPreventRunAndGunFootShuffle	= pBlendInfo->bPreventRunAndGunFootShuffle;
		pSkeleton->bPreventRunAndGunUpperBody	= pBlendInfo->bPreventRunAndGunUpperBody;

		if (eaSize(&pBlendInfo->eaRunAndGunBone))
		{
			FOR_EACH_IN_EARRAY(pBlendInfo->eaRunAndGunBone, SkelBlendRunAndGunBone, pRunAndGunBone)
			{
				//find the node
				const DynNode *pSkelNode = dynSkeletonFindNode(pSkeleton, pRunAndGunBone->pcName);

				//create a run'n'gun bone
				DynSkeletonRunAndGunBone *pSkelRGBone = StructCreate(parse_DynSkeletonRunAndGunBone);
				unitQuat(pSkelRGBone->qPostRoot);

				//set the skeleton's run & gun bone
				if (pRunAndGunBone->bSecondary) {
					bool addedIt = false;
					pSkelRGBone->pcRGBoneName		= pSkelNode->pcTag;
					pSkelRGBone->pcRGParentBoneName	= pSkelNode->pParent->pcTag;
					pSkelRGBone->bSecondary			= true;
					pSkelRGBone->fLimitAngle		= pRunAndGunBone->fLimitAngle;
					FOR_EACH_IN_EARRAY_FORWARDS(pSkeleton->eaRunAndGunBones, DynSkeletonRunAndGunBone, pCheckBone)
					{
						if (pCheckBone->bSecondary && pSkelRGBone->fLimitAngle > pCheckBone->fLimitAngle)
						{
							eaInsert(&pSkeleton->eaRunAndGunBones, pSkelRGBone, ipCheckBoneIndex);
							addedIt = true;
							break;
						}
					}
					FOR_EACH_END;
					if (!addedIt) {
						eaPush(&pSkeleton->eaRunAndGunBones, pSkelRGBone);
					}
				} else {
					eaInsert(&pSkeleton->eaRunAndGunBones, pSkelRGBone, 0);
					pSkelRGBone->pcRGBoneName		= pSkelNode->pcTag;
					pSkelRGBone->pcRGParentBoneName	= pSkelNode->pParent->pcTag;
					pSkelRGBone->bSecondary			= false;
					pSkelRGBone->fLimitAngle		= pRunAndGunBone->fLimitAngle;
					foundPrimary = true;
				}
			}
			FOR_EACH_END;	
		}
	}

	if (!foundPrimary)
	{
		//create and store a run'n'gun bone
		DynSkeletonRunAndGunBone *pSkelRGBone = StructCreate(parse_DynSkeletonRunAndGunBone);
		unitQuat(pSkelRGBone->qPostRoot);
		eaInsert(&pSkeleton->eaRunAndGunBones, pSkelRGBone, 0);

		//set some default values
		pSkelRGBone->pcRGBoneName		= pcWaistName;
		pSkelRGBone->pcRGParentBoneName	= pcHipsName;
		pSkelRGBone->bSecondary			= false;
		pSkelRGBone->fLimitAngle		= 110;
	}

	//compute the enable angles
	FOR_EACH_IN_EARRAY_FORWARDS(pSkeleton->eaRunAndGunBones, DynSkeletonRunAndGunBone, pRGBone)
	{
		pRGBone->fEnableAngle = pSkeleton->eaRunAndGunBones[0]->fLimitAngle - pRGBone->fLimitAngle;
	}
	FOR_EACH_END;
}

void dynSkeletonResetSequencers(DynSkeleton *pSkeleton)
{
	if (gConf.bNewAnimationSystem) {
		FOR_EACH_IN_EARRAY(pSkeleton->eaAGUpdater, DynAnimGraphUpdater, pUpdater)
		{
			dynAnimGraphUpdaterReset(pSkeleton, pUpdater);
		}
		FOR_EACH_END;
		pSkeleton->fGroundRegBlendFactor = 1.0f;
		pSkeleton->fGroundRegBlendFactorUpperBody = 1.0f;
		pSkeleton->fTorsoPointingBlendFactor = 1.0f;

		FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton)
		{
			dynSkeletonResetSequencers(pChildSkeleton);
		}
		FOR_EACH_END;
	}
}

void dynSkeletonCreateSequencersHelper(DynSkeleton* pSkeleton)
{
	const WLCostume* pCostume = GET_REF(pSkeleton->hCostume);
	const SkelInfo* pSkelInfo = pCostume?GET_REF(pCostume->hSkelInfo):NULL;
	const SkelBlendInfo* pBlendInfo = pSkelInfo?GET_REF(pSkelInfo->hBlendInfo):NULL;

	if(gConf.bNewAnimationSystem)
	{
		DynAnimChartRunTime* pDefaultChart;

		if (pBlendInfo && (pDefaultChart = GET_REF(pBlendInfo->hDefaultChart)))
		{
			DynAnimChartRunTime* pMountedChart;
			DynAnimChartStack* pChartStack;

			if (!pSkeleton->bHeadshot &&
				(pSkeleton->bRider || pSkeleton->bRiderChild) &&
				(pMountedChart = GET_REF(pBlendInfo->hMountedChart)))
			{
				pDefaultChart = pMountedChart;
			}

			if (!gConf.bUseMovementGraphs) dynMovementStateDeinit(&pSkeleton->movement);
			eaDestroyEx(&pSkeleton->eaAGUpdaterMutable, dynAnimGraphUpdaterDestroy);
			eaDestroyEx(&pSkeleton->eaAnimChartStacks, dynAnimChartStackDestroy);

			pChartStack = dynAnimChartStackCreate(pDefaultChart);
			eaPush(&pSkeleton->eaAnimChartStacks, pChartStack);
			eaPush(&pSkeleton->eaAGUpdaterMutable, dynAnimGraphUpdaterCreate(pSkeleton, pChartStack, false, false));
			if (!gConf.bUseMovementGraphs) dynMovementStateInit(&pSkeleton->movement, pSkeleton, pChartStack, pSkelInfo);
			else                           eaPush(&pSkeleton->eaAGUpdaterMutable, dynAnimGraphUpdaterCreate(pSkeleton, pChartStack, true, false));

			pSkeleton->iOverlaySeqIndex = 0;
			FOR_EACH_IN_EARRAY_FORWARDS(pBlendInfo->eaSequencer, SkelBlendSeqInfo, pSeqInfo)
			{
				DynAnimChartRunTime* pChart = GET_REF(pSeqInfo->hChart);
				if (pChart)
				{
					int iNewSqrIdx;
					
					pChartStack = dynAnimChartStackFindByBaseChart(pSkeleton->eaAnimChartStacks, pChart);
					if (!pChartStack) {
						pChartStack = dynAnimChartStackCreate(pChart);
						eaPush(&pSkeleton->eaAnimChartStacks, pChartStack);
					}

					iNewSqrIdx = eaPush(&pSkeleton->eaAGUpdaterMutable, dynAnimGraphUpdaterCreate(pSkeleton, pChartStack, false, pSeqInfo->bOverlay || pSeqInfo->bSubOverlay));
					pSeqInfo->iAGUpdaterIndex = iNewSqrIdx;
					if (pSeqInfo->bOverlay)
					{
						pSkeleton->iOverlaySeqIndex = iNewSqrIdx;
					}
				}
				else
				{
					pSeqInfo->iAGUpdaterIndex = 0;
				}
			}
			FOR_EACH_END;
			pSkeleton->bAnimDisabled = false;
		}
		else if (pSkelInfo && (pDefaultChart = GET_REF(pSkelInfo->hDefaultChart)))
		{
			DynAnimChartRunTime* pMountedChart;
			DynAnimChartStack *pChartStack;

			if (!pSkeleton->bHeadshot &&
				(pSkeleton->bRider || pSkeleton->bRiderChild) &&
				(pMountedChart = GET_REF(pSkelInfo->hMountedChart)))
			{
				pDefaultChart = pMountedChart;
			}

			if (!gConf.bUseMovementGraphs) dynMovementStateDeinit(&pSkeleton->movement);
			eaDestroyEx(&pSkeleton->eaAGUpdaterMutable, dynAnimGraphUpdaterDestroy);
			eaDestroyEx(&pSkeleton->eaAnimChartStacks, dynAnimChartStackDestroy);

			pChartStack = dynAnimChartStackCreate(pDefaultChart);
			eaPush(&pSkeleton->eaAnimChartStacks, pChartStack);
			eaPush(&pSkeleton->eaAGUpdaterMutable, dynAnimGraphUpdaterCreate(pSkeleton, pChartStack, false, false));
			if (!gConf.bUseMovementGraphs) dynMovementStateInit(&pSkeleton->movement, pSkeleton, pChartStack, pSkelInfo);
			else                           eaPush(&pSkeleton->eaAGUpdaterMutable, dynAnimGraphUpdaterCreate(pSkeleton, pChartStack, true, false));
			
			pSkeleton->iOverlaySeqIndex = 0;
			pSkeleton->bAnimDisabled = false;
		}
		else
		{
			if (!gConf.bUseMovementGraphs) dynMovementStateDeinit(&pSkeleton->movement);
			eaDestroyEx(&pSkeleton->eaAGUpdaterMutable, dynAnimGraphUpdaterDestroy);
			eaDestroyEx(&pSkeleton->eaAnimChartStacks, dynAnimChartStackDestroy);
			
			pSkeleton->bAnimDisabled = true;
		}
	}

	EARRAY_CONST_FOREACH_BEGIN(pSkeleton->eaDependentSkeletons, i, isize);
	{
		dynSkeletonCreateSequencersHelper(pSkeleton->eaDependentSkeletons[i]);
	}
	EARRAY_FOREACH_END;
}

void dynSkeletonApplyCostumeBits(DynSkeleton *pSkeleton, const WLCostume* pCostume)
{
	U32 uiIndex;

	if (!gConf.bNewAnimationSystem)
	{
		dynBitFieldClear(&pSkeleton->costumeBits);
		dynBitFieldSetAllFromBitFieldStatic(&pSkeleton->costumeBits, &pCostume->constantBits);
	} else {
		if (eaSize(&pSkeleton->eaAGUpdater)) // else hasn't been initialized yet, will get called later
		{
			dynSkeletonClearCostumeStances(pSkeleton);
			for (uiIndex = 0; uiIndex < pCostume->constantBits.uiNumBits; uiIndex++)
			{
				dynSkeletonSetCostumeStanceWord(pSkeleton, allocAddString(pCostume->constantBits.ppcBits[uiIndex]));
			}
		}
	}
}

void dynSkeletonCreateSequencers(DynSkeleton* pSkeleton)
{
	const WLCostume* pCostume = GET_REF(pSkeleton->hCostume);
	if (pCostume)
	{
		const SkelInfo* pSkelInfo;
		const SkelBlendInfo* pBlendInfo;

		ANALYSIS_ASSUME(pCostume);
		pSkelInfo = GET_REF(pCostume->hSkelInfo);
		pBlendInfo = pSkelInfo?GET_REF(pSkelInfo->hBlendInfo):NULL;

		pSkeleton->bRequestResetOnPreUpdate = 1;

		if (!gConf.bNewAnimationSystem)
		{
			if (pBlendInfo)
			{
				eaPush(&pSkeleton->eaSqr, dynSequencerCreate(pCostume, pBlendInfo->pcMainSequencer, MAX_WORLD_REGION_LOD_LEVELS - 1, false, false));
				FOR_EACH_IN_EARRAY_FORWARDS(pBlendInfo->eaSequencer, SkelBlendSeqInfo, pSeqInfo)
				{
					int iNewSqrIdx = eaPush(&pSkeleton->eaSqr, dynSequencerCreate(pCostume, pSeqInfo->pcSeqName, pSeqInfo->uiLODLevel, pSeqInfo->bNeverOverride, pSeqInfo->bOverlay || pSeqInfo->bSubOverlay));
					if (pSeqInfo->bOverlay)
						pSkeleton->iOverlaySeqIndex = iNewSqrIdx;
				}
				FOR_EACH_END;
			}
			else
			{
				eaPush(&pSkeleton->eaSqr, dynSequencerCreate(pCostume, pSkelInfo->pcSequencer, MAX_WORLD_REGION_LOD_LEVELS - 1, false, false));
			}
		}
		else
		{
			dynSkeletonCreateSequencersHelper(pSkeleton);

			if (!pSkeleton->bAnimDisabled)
			{
				ARRAY_FOREACH_BEGIN(pSkeleton->eaStances, i);
				{
					FOR_EACH_IN_EARRAY(pSkeleton->eaStances[i], const char, pcStance)
					{
						dynSkeletonSetStanceWordInternal(pSkeleton, pcStance, DS_STANCE_SET_CONTAINS_MOVEMENT(i));
					}
					FOR_EACH_END;
				}
				ARRAY_FOREACH_END;

				dynSkeletonApplyCostumeBits(pSkeleton, pCostume);
			}

			dynSkeletonResetSequencers(pSkeleton);
		}

		// Put the glr on anything new.

		dynSkeletonSetLogReceiver(pSkeleton, pSkeleton->glr);
	}
}

// Assumes skelinfo is the same!!
bool dynSkeletonChangeCostumeEx(DynSkeleton* pSkeleton, const WLCostume* pCostume, bool bIgnoreDependents)
{
	const WLCostume* pOldCostume = GET_REF(pSkeleton->hCostume);
	if (!bIgnoreDependents && pOldCostume && GET_REF(pOldCostume->hSkelInfo) != GET_REF(pCostume->hSkelInfo))
		Errorf("Not allowed to call dynSkeletonChangeCostume if SkelInfo Reference also changed! Call dynSkeletonDestroy/Create instead!");
	REMOVE_HANDLE(pSkeleton->hCostume);
	if (!SET_HANDLE_FROM_REFERENT("Costume", (WLCostume*)pCostume, pSkeleton->hCostume))
	{
		Errorf("Failed to create reference to Costume %s!", pCostume->pcName);
		return false;
	}

	dynSkeletonApplyCostumeBits(pSkeleton, pCostume);
	pSkeleton->bMount = pCostume->bMount;
	pSkeleton->bRider = pCostume->bRider;
	pSkeleton->bRiderChild = pCostume->bRiderChild;
	pSkeleton->bInitTerrainPitch = true;

	return true;
}

bool dynSkeletonCostumeBitsMatch(DynSkeleton *pSkeleton, const WLCostume *pCostume, const char *pcExtraStanceWords)
{
	char buf[1024];
	char *s;
	char *context = NULL;
	U32 uiIndex;
	U32 uiOtherCount = 0;
	
	if (pcExtraStanceWords)
	{
		strcpy(buf, pcExtraStanceWords);
		s = strtok_s(buf, " ", &context);
		while (s)
		{
			if (eaFind(&pSkeleton->eaStances[DS_STANCE_SET_COSTUME], allocAddString(s)) < 0) {
				return false;
			} else {
				uiOtherCount++;
				s = strtok_s(NULL, " ", &context);
			}
		}
	}

	if (eaSize(&pSkeleton->eaStances[DS_STANCE_SET_COSTUME]) != pCostume->constantBits.uiNumBits + uiOtherCount)
		return false;

	for (uiIndex = 0; uiIndex < pCostume->constantBits.uiNumBits; uiIndex++)
	{
		if (eaFind(&pSkeleton->eaStances[DS_STANCE_SET_COSTUME], allocAddString(pCostume->constantBits.ppcBits[uiIndex])) < 0)
			return false;
	}

	return true;
}

void dynSkeletonFreeDependence(DynSkeleton* pChild)
{
	DynSkeleton *pParent = pChild->pParentSkeleton;

	DynSkeleton *pClearDebug = pChild;
	while (pClearDebug) {
		if (pClearDebug == dynDebugState.pDebugSkeleton) {
			dynDebugStateSetSkeleton(NULL);
			break;
		}
		pClearDebug = pClearDebug->pParentSkeleton;
	}

	if (!pChild || !pChild->pParentSkeleton)
		return;

	if (pChild->bRider) {
		pChild->pRoot->pParent->uiTransformFlags |= ednScale;
	}

	eaFindAndRemoveFast(&pChild->pParentSkeleton->eaDependentSkeletons, pChild);
	pChild->pParentSkeleton = NULL;
	pChild->pGenesisSkeleton = pChild;
	pChild->attachmentBit = 0;

	if (gConf.bNewAnimationSystem &&
		pChild->bInheritBits)
	{
		U32 i;
		for (i = 0; i < DS_STANCE_SET_COUNT; i++) {
			FOR_EACH_IN_EARRAY(pParent->eaStances[i], const char, pcStance) {
				dynSkeletonClearStanceWordInSet(pChild, i, pcStance);
			} FOR_EACH_END;
		}
	}
}

AUTO_COMMAND;
void danimSetMountRiderScaleBlend(F32 fBlend)
{
	//running this command should overpower any data-defined blends that are on-screen
	fDefaultMountRiderScaleBlend = CLAMPF(fBlend, 0.0, 1.0);

	FOR_EACH_IN_EARRAY(eaDynSkeletons, DynSkeleton, pSkel) {
		if (pSkel->bMount) {
			FOR_EACH_IN_EARRAY(pSkel->eaDependentSkeletons, DynSkeleton, pChild) {
				if (pChild->bRider &&
					SAFE_MEMBER(pChild->pRoot,uiCriticalBone) &&
					SAFE_MEMBER(pSkel->pRoot,uiCriticalBone))
				{
					DynAnimBoneInfo *pMountBoneInfo = &pSkel->pAnimBoneInfos[pSkel->pRoot->iCriticalBoneIndex];
					DynAnimBoneInfo *pRiderBoneInfo = &pChild->pAnimBoneInfos[pChild->pRoot->iCriticalBoneIndex];
					lerpVec3(pMountBoneInfo->xBaseTransform.vScale, fDefaultMountRiderScaleBlend, pRiderBoneInfo->xBaseTransform.vScale, pSkel->vAppliedRiderScale);
					pSkel->vAppliedRiderScale[0] /= pMountBoneInfo->xBaseTransform.vScale[0];
					pSkel->vAppliedRiderScale[1] /= pMountBoneInfo->xBaseTransform.vScale[1];
					pSkel->vAppliedRiderScale[2] /= pMountBoneInfo->xBaseTransform.vScale[2];
					break;
				}
			} FOR_EACH_END;
		}
	} FOR_EACH_END;
}

void dynSkeletonPushDependentSkeleton(DynSkeleton* pParent, DynSkeleton* pChild, bool bInheritBits, bool bInsert)
{
	dynSkeletonFreeDependence(pChild);

	if (bInsert) {
		eaInsert(&pParent->eaDependentSkeletons, pChild, 0);
	} else {
		eaPush(&pParent->eaDependentSkeletons, pChild);
	}
	pChild->pParentSkeleton = pParent;
	pChild->pGenesisSkeleton = pParent->pGenesisSkeleton;
	pChild->bInheritBits = bInheritBits;

	if (pChild->bRider)
	{
		pChild->pRoot->pParent->uiTransformFlags &= ~ednScale;

		if (pParent->pRoot->uiCriticalBone &&
			pChild->pRoot->uiCriticalBone)
		{
			DynAnimBoneInfo *pParentBoneInfo = &pParent->pAnimBoneInfos[pParent->pRoot->iCriticalBoneIndex];
			WLCostume *pCostume = GET_REF(pParent->hCostume);

			if (pCostume->fMountScaleOverride > 0.f)
			{
				setVec3same(pParent->vAppliedRiderScale, pCostume->fMountScaleOverride);
			}
			else
			{
				DynAnimBoneInfo *pChildBoneInfo = &pChild->pAnimBoneInfos[pChild->pRoot->iCriticalBoneIndex];
				F32 fApplyBlend;

				if (pCostume && pCostume->fMountRiderScaleBlend >= 0.f) {
					fApplyBlend = pCostume->fMountRiderScaleBlend;
				} else {
					fApplyBlend = fDefaultMountRiderScaleBlend;
				}

				lerpVec3(pParentBoneInfo->xBaseTransform.vScale, fApplyBlend, pChildBoneInfo->xBaseTransform.vScale, pParent->vAppliedRiderScale);
				pParent->vAppliedRiderScale[0] /= pParentBoneInfo->xBaseTransform.vScale[0]; 
				pParent->vAppliedRiderScale[1] /= pParentBoneInfo->xBaseTransform.vScale[1]; 
				pParent->vAppliedRiderScale[2] /= pParentBoneInfo->xBaseTransform.vScale[2]; 				
			}
		}
	}

	if (gConf.bNewAnimationSystem &&
		pChild->bInheritBits)
	{
		U32 i;
		for (i = 0; i < DS_STANCE_SET_COUNT; i++) {
			FOR_EACH_IN_EARRAY(pParent->eaStances[i], const char, pcStance) {
				dynSkeletonSetStanceWordInSet(pChild, i, pcStance);
			} FOR_EACH_END;
		}
	}
}

F32 dynNodeTreeFindMaxLength(DynNode* pNode)
{
	Vec3 vPos;
	Vec3 vScale;
	F32 fMaxLength = 0.0f;
	DynNode* pChild = pNode->pChild;

	while (pChild)
	{
		F32 fNewChildLength = dynNodeTreeFindMaxLength(pChild);
		if (fNewChildLength > fMaxLength)
			fMaxLength = fNewChildLength;
		pChild = pChild->pSibling;
	}

	dynNodeGetLocalPosInline(pNode, vPos);
	dynNodeGetWorldSpaceScale(pNode, vScale);
	mulVecVec3(vPos, vScale, vPos);
	fMaxLength += lengthVec3(vPos);

	return fMaxLength;
}

void dynSkeletonReprocessEx(DynSkeleton* pSkeleton, const WLCostume* pCostume, bool bIgnoreDependents)
{
	DynBaseSkeleton* pScaledBase;
	DynBaseSkeleton* pBaseSkeleton;

	if (GET_REF(pSkeleton->hCostume) != pCostume)
		dynSkeletonChangeCostumeEx(pSkeleton, pCostume, bIgnoreDependents);

	pScaledBase = dynScaledBaseSkeletonCreate(pSkeleton);
	pBaseSkeleton = GET_REF(pSkeleton->hBaseSkeleton);
	assert(pBaseSkeleton);

	pSkeleton->fStaticVisibilityRadius = pScaledBase?dynNodeTreeFindMaxLength(pScaledBase->pRoot):dynNodeTreeFindMaxLength(pBaseSkeleton->pRoot);
	dynScaleCollectionClear(&pSkeleton->scaleCollection);
	dynScaleCollectionInit(pScaledBase?pScaledBase:pBaseSkeleton, &pSkeleton->scaleCollection);
	dynBitFieldClear(&pSkeleton->talkingBits);

	dynSkeletonApplyCostumeBits(pSkeleton, pCostume);

	FOR_EACH_IN_EARRAY(pSkeleton->eaBouncerUpdaters, DynBouncerUpdater, pBounceUpdater) {
		pBounceUpdater->bReprocessedSkeleton = true;
	} EARRAY_FOREACH_END;

	if (pSkeleton->bHasStrands) {
		pSkeleton->bInitStrands = 1;
	}

	dynBitFieldClear(&pSkeleton->actionFlashBits);
	
	if (!bIgnoreDependents && eaSize(&pSkeleton->eaDependentSkeletons) != eaSize(&pCostume->eaSubCostumes))
	{
		// Costume size mismatch, should never get here
		//ignore dependents added for special case of reprocessing a skeleton which is having costume parts both changed and added
		Errorf("Invalid subskeleton vs. subcostume count mismatch");
	}
	else
	{
		FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pDependentSkeleton)
		{
			//order of sub-costumes and dependent skeletons should match before it gets here
			const WLCostume* pSubCostume = GET_REF(pCostume->eaSubCostumes[ipDependentSkeletonIndex]->hSubCostume);
			if (pSubCostume)
				dynSkeletonReprocessEx(pDependentSkeleton, pSubCostume, bIgnoreDependents);
			else
				Errorf("Invalid subcostume %s", REF_STRING_FROM_HANDLE(pCostume->eaSubCostumes[ipDependentSkeletonIndex]->hSubCostume));
		}
		FOR_EACH_END;
	}

	if (pScaledBase)
	{
		dynBaseSkeletonFree(pScaledBase);
	}
}

static void dynSkeletonSetupBouncers(DynSkeleton *pNew, const SkelInfo *pSkelInfo)
{
	U32 i;
	// Clear uiHasBouncer on existing nodes (in case of reload)
	if (GET_REF(pNew->hBaseSkeleton))
	{
		DynBouncerGroupInfo* pBouncerGroupInfo = GET_REF(pSkelInfo->hBouncerInfo);
		for (i=0; i<GET_REF(pNew->hBaseSkeleton)->uiNumBones; i++)
		{
			pNew->pRoot[i].uiHasBouncer = 0;
		}
		if (pBouncerGroupInfo)
		{
			FOR_EACH_IN_EARRAY(pBouncerGroupInfo->eaBouncer, DynBouncerInfo, pBouncerInfo)
			{
				DynNode* pNode = dynSkeletonFindNodeNonConst(pNew, pBouncerInfo->pcBoneName);
				if (pNew && pNode)
				{
					DynBouncerUpdater* pUpdater = dynBouncerUpdaterCreate(pBouncerInfo);
					eaPush(&pNew->eaBouncerUpdaters, pUpdater);

					pNode->uiHasBouncer = 1;
				} else if (!pNode) {
					Errorf("Bone '%s' cannot be set to bounce since it does not exist in skeleton '%s'", pBouncerInfo->pcBoneName, GET_REF(pNew->hBaseSkeleton)->pcName );
				}
			}
			FOR_EACH_END;
		}
	}
}

DynSkeleton* dynSkeletonCreate(const WLCostume* pCostume, bool bLocalPlayer, bool bUnmanaged, bool bDontCreateSequencers, bool bForceNoScale, bool bHeadshot, const char *pcCostumeFxTag)
{
	DynSkeleton* pNew;
	const SkelInfo* pSkelInfo;
	const SkelBlendInfo *pBlendInfo;
	const DynGroundRegData *pGroundRegData;
	DynBaseSkeleton *pNewBaseSkeleton;
	DynBaseSkeleton* pScaledBase = NULL;
	
	if (!pCostume)
	{
		Errorf("Can't create DynSkeleton without a costume!");
		return NULL;
	}
	if (!(pSkelInfo = GET_REF(pCostume->hSkelInfo)))
	{
		Errorf("Can't create skeleton from invalid costume: %s!", pCostume->pcName);
		return NULL;
	}
	pBlendInfo = GET_REF(pSkelInfo->hBlendInfo);
	pGroundRegData = GET_REF(pSkelInfo->hGroundRegData);

	pNew = MP_ALLOC(DynSkeleton);
	pNew->pGenesisSkeleton = pNew;
	pNew->pcCostumeFxTag = pcCostumeFxTag;
	pNew->bHeadshot = bHeadshot;

	if (!dynSkeletonChangeCostume(pNew, pCostume))
	{
		MP_FREE(DynSkeleton, pNew);
		globMovementLog("[dyn] Failed to create skeleton because dynSkeletonChangeCostume failed.");
		return NULL;
	}

	COPY_HANDLE(pNew->hBaseSkeleton, pSkelInfo->hBaseSkeleton);
	if (!(pNewBaseSkeleton = GET_REF(pNew->hBaseSkeleton)))
	{
		const DynBaseSkeleton *pBaseSkeleton = GET_REF(pSkelInfo->hBaseSkeleton);
		Errorf("Failed to create reference to Base Skeleton %s!", SAFE_MEMBER(pBaseSkeleton,pcName));
		MP_FREE(DynSkeleton, pNew);
		return NULL;
	}

	if (!bForceNoScale)
		pScaledBase = dynScaledBaseSkeletonCreate(pNew);

	pNew->fStaticVisibilityRadius = pScaledBase?dynNodeTreeFindMaxLength(pScaledBase->pRoot):dynNodeTreeFindMaxLength(pNewBaseSkeleton->pRoot);

	dynScaleCollectionInit(pScaledBase?pScaledBase:pNewBaseSkeleton, &pNew->scaleCollection);

	{
		if (shouldUseMemPool(pNewBaseSkeleton->uiNumBones))
			pNew->pRoot = mpAlloc(mpSkeletonNodes);
		else
		{
			pNew->pRoot = aligned_calloc(sizeof(DynNode), pNewBaseSkeleton->uiNumBones, 16);
			++uiUnpooledSkeletonCount;
		}

		dynNodeTreeLinearCopy(pNewBaseSkeleton->pRoot, pNew->pRoot, pNewBaseSkeleton->uiNumBones, pNew);
		//pNew->pRoot = dynNodeTreeCopy(pNewBaseSkeleton->pRoot, NULL, true, pNew, dynNodeLinearAllocator, &pNewRoot);
	}

	unitQuat(pNew->qTorso);

	dynBitFieldClear(&pNew->talkingBits);

	dynBitFieldClear(&pNew->actionFlashBits);

	pNew->pcBankingOverrideNode = pSkelInfo->pcAltBankingNodeAlias;
	pNew->uiBankingOverrideStanceCount = 0;
	pNew->fBankingOverrideTimeActive =  0.f;

	pNew->uiRandomSeed = (U8)randomU32();
	if (!bDontCreateSequencers)
	{
		dynSkeletonCreateSequencers(pNew);
	}	
	dynSkeletonSetInitialTorsoPointing(pBlendInfo, pNew);

	if (!gConf.bNewAnimationSystem) { //new does as part of create sequencers during an anim disable check
		dynSkeletonApplyCostumeBits(pNew, pCostume); // after dynSkeletonCreateSequencers
	}

	dynSkeletonCreateBoneTable(&pNew->stBoneTable);
	dynSkeletonPopulateStashTable(pNew->stBoneTable, pNew->pRoot);
	dynSkeletonShrinkBoneTable(&pNew->stBoneTable);

	if (pNew->pcBankingOverrideNode &&
		!dynSkeletonFindNode(pNew, pNew->pcBankingOverrideNode))
	{
		Errorf(	"Failed to find the AltBankingNodeAlias %s specified by SkelInfo %s when creating skeleton with base %s\n",
				pSkelInfo->pcAltBankingNodeAlias,
				pSkelInfo->pcSkelInfoName,
				pNewBaseSkeleton->pcName);
		pNew->pcBankingOverrideNode = NULL;
	}

	dynSkeletonSetRunAndGunBones(pBlendInfo, pNew);

	pNew->bWasForceVisible = false;

	pNew->bIsDragonTurn = 0;

	pNew->bIsLurching = 0;
	pNew->bIsLunging = 0;

	pNew->bIsRising  = 0;
	pNew->bIsFalling = 0;
	pNew->bIsJumping = 0;

	pNew->bIsDead = 0;
	pNew->bIsNearDead = 0;
	pNew->bHasClientSideRagdoll = 0;
	pNew->bHasClientSideTestRagdoll = 0;
	pNew->bCreateClientSideRagdoll = 0;
	pNew->bCreateClientSideTestRagdoll = 0;
	pNew->bSleepingClientSideRagdoll = 0;
	pNew->bIsPlayingDeathAnim = 0;
	pNew->bEndDeathAnimation = 0;

	pNew->bFrozen = false;
	pNew->fFrozenTimeScale = 1.f;

	pNew->eaMatchBaseSkelAnimJoints = NULL;
	pNew->eaAnimGraphUpdaterMatchJoints = NULL;
	pNew->eaSkeletalMovementMatchJoints = NULL;

	pNew->fTerrainPitch = 0.f;
	unitQuat(pNew->qTerrainPitch);
	pNew->fTerrainOffsetZ = 0.f;
	pNew->fTerrainTiltBlend = 0.f;
	//pNew->fTerrainHeightBump = 0.f;

	pNew->bStartedKeywordGraph = false;

	pNew->bSavedOnCostumeChange = 0;

	pNew->fWepRegisterBlend = 0.f;
	pNew->fIKBothHandsBlend = 0.f;

	pNew->eaCachedAnimGraphFx = NULL;
	pNew->eaCachedSkelMoveFx  = NULL;

	pNew->pExtentsNode = dynSkeletonFindNodeNonConst(pNew, pcExtents);
	if (gConf.bNewAnimationSystem) {
		pNew->pFacespaceNode	= dynSkeletonFindNodeNonConst(pNew, pcFacespace);
		pNew->pWepLNode			= dynSkeletonFindNodeNonConst(pNew, pcNodeNameWepL);
		pNew->pWepRNode			= dynSkeletonFindNodeNonConst(pNew, pcNodeNameWepR);
		pNew->pHipsNode = pGroundRegData ?
							dynSkeletonFindNodeNonConst(pNew, pGroundRegData->pcHipsNode) :
							dynSkeletonFindNodeNonConst(pNew, pcNodeNameHips);
	}

	if ( bLocalPlayer )
	{
		pNew->bPlayer = true;
		if (!gConf.bNewAnimationSystem)
			dynDebugSetSequencer(pNew);
		dynDebugStateSetSkeleton(pNew);
		pPlayerSkeleton = pNew;
		stPlayerBoneTable = pNew->stBoneTable;
	} else {
		pNew->bPlayer = false;
	}

	//debug data
	{
		DynSkeletonDebugNewAnimSys *newAnimSysDebugSkeleton = StructCreate(parse_DynSkeletonDebugNewAnimSys);
		while (eaSize(&newAnimSysDebugSkeleton->eaGraphUpdaters) < eaSize(&pNew->eaAGUpdater))
		{
			DynSkeletonDebugGraphUpdater *newDebugGraphUpdater = StructCreate(parse_DynSkeletonDebugGraphUpdater);
			eaPush(&newAnimSysDebugSkeleton->eaGraphUpdaters, newDebugGraphUpdater);
		}
		pNew->pDebugSkeleton = newAnimSysDebugSkeleton;
	}

	if (!bDontCreateSequencers)
	{
		dynSkeletonSetupBouncers(pNew, pSkelInfo);
	}

	if (!bUnmanaged)
	{
		eaPush(&eaDynSkeletons, pNew);
		dynAnimPhysicsCreateObject(pNew);
	}
	else
	{
		pNew->bUnmanaged = true;
		pNew->bVisible = true;
		pNew->uiLODLevel = 0;
	}

	dynSkeletonCreateStrands(pNew, GET_REF(pSkelInfo->hStrandDataSet), bHeadshot);
	dynSkeletonCreateGroundRegLimbs(pNew, pGroundRegData);
	dynSkeletonCreateNodeExpressions(pNew, GET_REF(pSkelInfo->hExpressionSet));

	if (pScaledBase)
		dynBaseSkeletonFree(pScaledBase);

	globMovementLog("[dyn] Created skeleton 0x%p", pNew);

	return pNew;
}

void dynBaseSkeletonFree(DynBaseSkeleton* pToFree)
{
	stashTableDestroy(pToFree->stBoneTable);
	dynNodeClearTree(pToFree->pRoot);
	if (shouldUseMemPool(pToFree->uiNumBones))
		mpFree(mpSkeletonNodes, pToFree->pRoot);
	else
	{
		free(pToFree->pRoot);
		--uiUnpooledSkeletonCount;
	}
	MP_FREE(DynBaseSkeleton, pToFree);
}

void dynDependentSkeletonFree(DynSkeleton* pDependent)
{
	DynSkeleton *pClearDebug = pDependent;
	while (pClearDebug) {
		if (pClearDebug == dynDebugState.pDebugSkeleton) {
			dynDebugStateSetSkeleton(NULL);
			break;
		}
		pClearDebug = pClearDebug->pParentSkeleton;
	}

	pDependent->pParentSkeleton = NULL;
	pDependent->pGenesisSkeleton = pDependent;

	// Some skeletons are our responsibility (sub-skeletons)
	if (pDependent->bOwnedByParent)
	{
		dynNodeFree(pDependent->pRoot->pParent); // this is an 'InBetween' node to buffer recursive functions between the two skeletons
		dynSkeletonFree(pDependent);
	}
}

void dynSkeletonFree(DynSkeleton* pToFree)
{
	dynSkeletonFreeDependence(pToFree);
	FOR_EACH_IN_EARRAY(pToFree->eaDependentSkeletons, DynSkeleton, pDependent)
		dynDependentSkeletonFree(pDependent);
	FOR_EACH_END;
	eaDestroy(&pToFree->eaDependentSkeletons);
	pToFree->pGenesisSkeleton = NULL;
	if (pToFree == dynDebugState.pDebugSkeleton)
		dynDebugStateSetSkeleton(NULL);
	SAFE_FREE(pToFree->pAnimBoneInfos);
	stashTableDestroy(pToFree->stBoneTable);
	if (pToFree->pDPO)
		dynAnimPhysicsFreeObject(pToFree);
	dynSkeletonDestroyStrands(pToFree);
	eaDestroyEx(&pToFree->eaGroundRegLimbs, NULL);
	REMOVE_HANDLE(pToFree->hCostume);
	REMOVE_HANDLE(pToFree->hBaseSkeleton);
	if (!gConf.bNewAnimationSystem)
	{
		FOR_EACH_IN_EARRAY(pToFree->eaSqr, DynSequencer, pSqr)
			dynSequencerFree(pSqr);
		FOR_EACH_END;
		eaDestroy(&pToFree->eaSqr);
	}
	else
	{
		dynSkeletonFreeNodeExpressions(pToFree);
		if (!gConf.bUseMovementGraphs) dynMovementStateDeinit(&pToFree->movement);
		eaDestroyEx(&pToFree->eaAGUpdaterMutable, dynAnimGraphUpdaterDestroy);
		eaDestroyEx(&pToFree->eaAnimChartStacks, dynAnimChartStackDestroy);

		if (!pToFree->bSleepingClientSideRagdoll)
			dynSkeletonClientSideRagdollFree(pToFree);
	}
	{
		U32 uiRagdollPart;
		for (uiRagdollPart = 0; uiRagdollPart < pToFree->ragdollState.uiNumParts; uiRagdollPart++) {
			if (pToFree->ragdollState.aParts[uiRagdollPart].pSplat && wl_state.gfx_splat_destroy_callback)
				wl_state.gfx_splat_destroy_callback(pToFree->ragdollState.aParts[uiRagdollPart].pSplat);
		}
		SAFE_FREE(pToFree->ragdollState.aParts);
		dynAnimTrackHeaderDecrementPermanentRefCount(pToFree->pClientSideRagdollPoseAnimTrackHeader);
	}
	eaDestroy(&pToFree->eaBitFieldFeeds);
	eaDestroy(&pToFree->eaAnimWordFeeds);
	dynNodeClearTree(pToFree->pRoot);
	if (shouldUseMemPool(pToFree->scaleCollection.uiNumTransforms))
		mpFree(mpSkeletonNodes, pToFree->pRoot);
	else
	{
		free(pToFree->pRoot);
		--uiUnpooledSkeletonCount;
	}
	eaFindAndRemoveFast(&eaDynSkeletons, pToFree);
	dynScaleCollectionClear(&pToFree->scaleCollection);
	if (pToFree->guid)
		dynSkeletonRemoveGuid(pToFree->guid);
	RefSystem_RemoveReferent(pToFree, true);
	ARRAY_FOREACH_BEGIN(pToFree->eaStances, i);
	{
		eaDestroy(&pToFree->eaStances[i]);
		eafDestroy(&pToFree->eaStanceTimers[i]);
	}
	ARRAY_FOREACH_END;
	eaDestroy(&pToFree->eaStancesCleared);
	eafDestroy(&pToFree->eaStanceTimersCleared);
	eaDestroyEx(&pToFree->eaBouncerUpdaters, dynBouncerUpdaterDestroy);
	eaDestroyEx(&pToFree->eaAnimOverrides, NULL);
	eaDestroyEx(&pToFree->eaRunAndGunBones, NULL);
	eaDestroyEx(&pToFree->eaMatchBaseSkelAnimJoints, NULL);
	eaDestroyEx(&pToFree->eaAnimGraphUpdaterMatchJoints, NULL);
	eaDestroyEx(&pToFree->eaSkeletalMovementMatchJoints, NULL);
	eaDestroyEx(&pToFree->eaCachedAnimGraphFx, NULL);
	eaDestroyEx(&pToFree->eaCachedSkelMoveFx, NULL);
	FOR_EACH_IN_EARRAY(pToFree->pDebugSkeleton->eaGraphUpdaters, DynSkeletonDebugGraphUpdater, dsdGu)
	{
		eaDestroy(&dsdGu->eaFlags);
		eaDestroy(&dsdGu->eaFX);
		eaDestroy(&dsdGu->eaKeywords);
		eaDestroy(&dsdGu->eaNodes);
	}
	FOR_EACH_END;
	eaDestroy(&pToFree->pDebugSkeleton->eaDebugStances);
	eaDestroy(&pToFree->pDebugSkeleton->eaGraphUpdaters);
	eaDestroy(&pToFree->pDebugSkeleton->eaMovementBlocks);
	eaDestroy(&pToFree->pDebugSkeleton->eaMovementFX);
	MP_FREE(DynSkeleton, pToFree);
}

static U32 uiGlobLODLevel = 0;
static U32 uiGlobMinLODLevel = 0;
static U32 uiGlobMinPlayerLODLevel = 0;

AUTO_CMD_INT(uiGlobLODLevel, danimTestLOD) ACMD_CATEGORY(dynAnimation);
AUTO_CMD_INT(uiGlobMinLODLevel, danimMinLOD) ACMD_CATEGORY(dynAnimation) ACMD_CMDLINE;
AUTO_CMD_INT(uiGlobMinPlayerLODLevel, danimMinPlayerLOD) ACMD_CATEGORY(dynAnimation) ACMD_CMDLINE;

AUTO_CMD_INT(dynDebugState.bAnimLODOff, danimLODOff) ACMD_CATEGORY(dynAnimation);

AUTO_CMD_INT(dynDebugState.bLODPlayer, danimPlayerLOD) ACMD_CATEGORY(dynAnimation);


static void dynSkeletonCalculateDistanceTraveled( DynSkeleton* pSkeleton, F32 fDeltaTime )
{
	Vec3 vDist;
	Vec3 vCurrentPos;
	dynNodeGetWorldSpacePos(pSkeleton->pRoot, vCurrentPos);
	subVec3(vCurrentPos, pSkeleton->vOldPos, vDist);
	pSkeleton->fDistanceTraveledXZ = lengthVec3XZ(vDist);
	pSkeleton->fDistanceTraveledY = vecY(vCurrentPos) - vecY(pSkeleton->vOldPos);
	if(fDeltaTime){
		pSkeleton->fCurrentSpeedXZ = pSkeleton->fDistanceTraveledXZ / fDeltaTime;
		pSkeleton->fCurrentSpeedY = pSkeleton->fDistanceTraveledY / fDeltaTime;
	}
	if (pSkeleton->bMount)
	{
		pSkeleton->fHeightScale = pSkeleton->scaleCollection.pLocalTransforms[0].vScale[1] * pSkeleton->vAppliedRiderScale[1] * pSkeleton->scaleCollection.fHipsHeightAdjustmentFactor;
	}
	else if (pSkeleton->scaleCollection.uiNumTransforms > 0)
	{
		pSkeleton->fHeightScale = pSkeleton->scaleCollection.pLocalTransforms[0].vScale[1] * pSkeleton->scaleCollection.fHipsHeightAdjustmentFactor;
	}
	else
	{
		pSkeleton->fHeightScale = 1.0;
	}
}

void dynSkeletonSetExtentsNode( DynSkeleton* pSkeleton ) 
{
	// this should really be known as the group extents node
	// I'm leaving it as that so I don't change other systems behavior
	if (pSkeleton->pExtentsNode)
	{
		Vec3 vPreScale,    vApplyScale;
		Vec3 vPrePosition, vApplyPosition;
		subVec3(pSkeleton->vCurrentGroupExtentsMax, pSkeleton->vCurrentGroupExtentsMin, vPreScale);
		scaleVec3(vPreScale, 1.25, vApplyScale); //slightly super sized
		addVec3(pSkeleton->vCurrentGroupExtentsMin, pSkeleton->vCurrentGroupExtentsMax, vPrePosition);
		scaleVec3(vPrePosition, 0.5, vApplyPosition);
		dynNodeSetPosInline(pSkeleton->pExtentsNode, vApplyPosition);
		dynNodeSetScaleInline(pSkeleton->pExtentsNode, vApplyScale);
		pSkeleton->pExtentsNode->uiTransformFlags &= ~ednScale; //disabled since already used to compute min & max which set scale for this node
		pSkeleton->pExtentsNode->uiDirtyBits = 1;
	}
}

// All the various constants are at the top of dynDraw.c
static void dynSkeletonCalculateLOD(SA_PARAM_NN_VALID DynSkeleton* pSkeleton)
{
	const WorldRegionLODSettings* pLODSettings;
	U32 uiLODLevel = 0;
	F32 fSkeletonDistanceSquared = pSkeleton->fCurrentCamDistanceSquared * pSkeleton->pDrawSkel->fSendDistance; // note that this multiply is to undo the divide by send distance we use for sorting.
	F32 fDetailAdjustment = 1.0f;

	if (pSkeleton->bEverUpdated) {
		pSkeleton->bWasVisible = pSkeleton->bVisible;
	} else {
		pSkeleton->bWasVisible = false;
	}

	pSkeleton->bVisibilityChecked = false;
	pSkeleton->bOcclusionChecked = false;
	pSkeleton->iVisibilityClipValue = 0;
	pSkeleton->bVisible = true;

	pLODSettings = worldLibGetLODSettings();
	assert(pLODSettings);

	if (wl_state.gfx_setting_world_detail_callback && pSkeleton->pDrawSkel && pSkeleton->pDrawSkel->bWorldLighting)
		fDetailAdjustment = wl_state.gfx_setting_world_detail_callback();
	else if (wl_state.gfx_setting_character_detail_callback)
		fDetailAdjustment = wl_state.gfx_setting_character_detail_callback();


	if (pSkeleton->pDrawSkel->fTotalAlpha == 0.0f)
	{
		pSkeleton->bVisibilityChecked = true;
		pSkeleton->bVisible = false;
	}
	else if (dynDebugState.bAnimLODOff
		|| (!gConf.bNewAnimationSystem && dynSequencersDemandNoLOD(pSkeleton))
		|| !pSkeleton->bEverUpdated || pSkeleton->bUnmanaged)
	{
		uiLODLevel = 0;
		if (!pSkeleton->bEverUpdated)
		{
			pSkeleton->bEverUpdated = true;
		}
		if (pSkeleton->pFxManager)
			pSkeleton->pFxManager->bWaitingForSkelUpdate = false;
	}
	else if (uiGlobLODLevel)
	{
		uiLODLevel = uiGlobLODLevel;
	}
	else
	{
		// If they are out of frustrum, bump the lodlevel up one.
		pSkeleton->bVisible = ( pSkeleton->bForceVisible || pSkeleton->bWasForceVisible || (pSkeleton->pParentSkeleton && pSkeleton->pParentSkeleton->bVisible) ) ? true : ( wl_state.check_skeleton_visibility_func?wl_state.check_skeleton_visibility_func(pSkeleton):true );
		pSkeleton->bVisibilityChecked = !!wl_state.check_skeleton_visibility_func;

		if (pSkeleton->bVisible)
		{
			if (pSkeleton->pDrawSkel->bIsLocalPlayer && !dynDebugState.bLODPlayer)
				uiLODLevel = 0;
			else
			{

				// Calculate the LOD
				const F32 fBaseSendDistance = 300.0f; // we might want to hook this up to some global constant instead of just copy and pasting 300 everywhere
				// rescale the distances based on the new e->fEntitySendDistance and on the character detail level
				const F32 fScaleFactor = fDetailAdjustment * pSkeleton->pDrawSkel->fSendDistance / fBaseSendDistance;
				U32 uiIdealLOD;
				bool bFoundSlot = false;

				uiLODLevel = pLODSettings->uiNumLODLevels - 1;

				// Calculate which LOD it would ideally be
				while (uiLODLevel > 0)
				{
					F32 fLODDistance = pLODSettings->LodDistance[uiLODLevel] * fScaleFactor;
					if (fSkeletonDistanceSquared > SQR(fLODDistance))
					{
						break;
					}
					--uiLODLevel;
				}
				uiIdealLOD = uiLODLevel;

				// FIRST, see if our desired slot has room
				if (LODSkeletonSlots[uiLODLevel] < pLODSettings->MaxLODSkelSlots[uiLODLevel])
				{
					++LODSkeletonSlots[uiLODLevel];
					bFoundSlot = true;
				}

				// Now, if our desired slot is used up, check higher LOD slots we didn't draw.  If so, use up their slot, but not their actual LOD
				if (!bFoundSlot)
				{
					int iLOD;
					for (iLOD = uiLODLevel - 1; iLOD >= 0; --iLOD)
					{
						if (LODSkeletonSlots[iLOD] < pLODSettings->MaxLODSkelSlots[iLOD])
						{
							++LODSkeletonSlots[iLOD]; // use this slot
							uiLODLevel = uiIdealLOD; // but draw us at our ideal LOD
							bFoundSlot = true;
							break;
						}
					}
				}

				if (!bFoundSlot)
				{
					// We are going to have to sacrifice visual quality for performance now

					// Try to find a slot to fit it into, dropping down in detail slots until we're out of them
					while (uiLODLevel <= pLODSettings->uiMaxLODLevel)
					{
						if (LODSkeletonSlots[uiLODLevel] < pLODSettings->MaxLODSkelSlots[uiLODLevel])
						{
							++LODSkeletonSlots[uiLODLevel];
							bFoundSlot = true;
							break;
						}
						// Could not find a slot, so bump it higher
						++uiLODLevel;
					}
				}

				if (!bFoundSlot) // if we still haven't found a slot, don't draw it
				{
					uiLODLevel = pLODSettings->uiMaxLODLevel;
					pSkeleton->bVisible = false;
					pSkeleton->bVisibilityChecked = true;
				}
			}
		}
	}


	uiLODLevel = CLAMP(uiLODLevel, pSkeleton->pDrawSkel->bIsLocalPlayer?uiGlobMinPlayerLODLevel:uiGlobMinLODLevel, pLODSettings->uiMaxLODLevel);
	if (!pSkeleton->bVisible)
	{
		uiLODLevel = pLODSettings->uiMaxLODLevel;
		++dynDebugState.uiNumCulledSkels;
	}
	else if (dynDebugState.bNoAnimation)
	{
		uiLODLevel = pLODSettings->uiMaxLODLevel;
		if (!pSkeleton->pParentSkeleton)
			++dynDebugState.uiNumUpdatedSkels[MIN(uiLODLevel, MAX_WORLD_REGION_LOD_LEVELS-1)];
	}
	else
	{
		if (!pSkeleton->pParentSkeleton)
			++dynDebugState.uiNumUpdatedSkels[MIN(uiLODLevel, MAX_WORLD_REGION_LOD_LEVELS-1)];
	}

	FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pSubSkeleton)
	{
		pSubSkeleton->uiLODLevel = pSkeleton->uiLODLevel;
		pSubSkeleton->bBodySock = false;
		pSubSkeleton->bVisibilityChecked = pSkeleton->bVisibilityChecked;
		pSubSkeleton->bVisible = pSkeleton->bVisible;
	}
	FOR_EACH_END;


	pSkeleton->bBodySock = (uiLODLevel >= pLODSettings->uiBodySockLODLevel);

	pSkeleton->uiLODLevel = uiLODLevel;
}

typedef struct dSkelUpdateInfo
{
	DynSkeleton* pSkel;
	U32 count;
	S32 moreUpdates;
	bool bDone;
} dSkelUpdateInfo;

#define MAX_DEPENDENT_SKELETONS 4


static F32 findNormalYawDiff(const Vec3 vA, const Vec3 vB)
{
	F32 fYawA = getVec3Yaw(vA);
	F32 fYawB = getVec3Yaw(vB);
	F32 fDiffYaw = fYawB - fYawA;

	if (fDiffYaw > PI)
		fDiffYaw -= TWOPI;
	else if (fDiffYaw < -PI)
		fDiffYaw += TWOPI;

	return fDiffYaw;
}

static void dynSkeletonUpdateRoot(DynSkeleton* pSkeleton, F32 fDeltaTime)
{
	bool bPointToTarget;

	DynTransform xLocationWS;
	Vec3 vUpVectorWS;
	Quat qFaceSpace;
	Vec3 vToTargetWS;

	Mat3 mFaceSpace;

	if (pSkeleton->ragdollStateFunc)
		pSkeleton->ragdollState.bRagdollOn = !!pSkeleton->ragdollStateFunc(pSkeleton, pSkeleton->preUpdateData);

	bPointToTarget = (pSkeleton->bUseTorsoPointing || pSkeleton->bUseTorsoDirections) && pSkeleton->bHasTarget && !pSkeleton->bIsDead && !pSkeleton->ragdollState.bRagdollOn;

	if (pSkeleton->pLocation)
		dynNodeGetWorldSpaceTransform(pSkeleton->pLocation, &xLocationWS);
	else
	{
		pSkeleton->pRoot->uiDirtyBits = 1;
		dynNodeGetWorldSpaceTransform(pSkeleton->pRoot, &xLocationWS);
	}

	if (pSkeleton->bHasTarget && !pSkeleton->ragdollState.bRagdollOn)
	{
		subVec3(pSkeleton->vTargetPos, xLocationWS.vPos, vToTargetWS);
		if (!pSkeleton->bTorsoPointing && (pSkeleton->bUseTorsoPointing || pSkeleton->bUseTorsoDirections))
			vToTargetWS[1] = 0.0f;

		// Calc up vector
		{
			quatRotateVec3Inline(xLocationWS.qRot, upvec, vUpVectorWS);
			normalVec3(vToTargetWS);

			// Make sure vToTarget is not colinear w/ vUpVector
			if (fabsf(dotVec3(vUpVectorWS, vToTargetWS)) > 0.9999f)
			{
				// for now just snap the vToTargetWS to something else
				vToTargetWS[0] = vUpVectorWS[1];
				vToTargetWS[1] = vUpVectorWS[2];
				vToTargetWS[2] = vUpVectorWS[0];
			}

		}

		// Now, using up vector and non-colinear to-target vector, create an orthonormal rotation we call "face space"
		{
			orientMat3ToNormalAndForward(mFaceSpace, vUpVectorWS, vToTargetWS);
			mat3ToQuat(mFaceSpace, qFaceSpace);
		}
	}

	if (dynDebugState.bDrawTorsoPointing)
	{
		{
			//draw a line from the skeleton's location to the target's position
			Vec3 ptA, ptB, ptC;
			copyVec3(xLocationWS.vPos, ptA);
			copyVec3(pSkeleton->vTargetPos, ptB);
			copyVec3(pSkeleton->vTargetPos, ptC);
			ptA[1] += 0.1f;
			ptB[1] = ptA[1];
			ptC[1] += 0.1f;
			if (bPointToTarget) {
				wlDrawLine3D_2(ptA, 0xFF00FF00, ptB, 0xFF00FF00);
				wlDrawLine3D_2(ptA, 0xFF008800, ptC, 0xFF008800);
			}
			else {
				wlDrawLine3D_2(ptA, 0xFFFF0000, ptB, 0xFFFF0000);
				wlDrawLine3D_2(ptA, 0xFF880000, ptC, 0xFF880000);
			}
		}
		{
			//draw the face space vectors
			Vec3 vFsX, vFsY, vFsZ;
			int color = 0xFFFFFF00;
			scaleAddVec3(mFaceSpace[0], 5, xLocationWS.vPos, vFsX);
			scaleAddVec3(mFaceSpace[1], 5, xLocationWS.vPos, vFsY);
			scaleAddVec3(mFaceSpace[2], 5, xLocationWS.vPos, vFsZ);
			wlDrawLine3D_2(xLocationWS.vPos, color, vFsX, color);
			wlDrawLine3D_2(xLocationWS.vPos, color, vFsY, color);
			wlDrawLine3D_2(xLocationWS.vPos, color, vFsZ, color);
		}
	}

	if (bPointToTarget)
	{
		Quat qFaceSpaceInv;
		F32 fMovementYawFS;
		F32 fFacingPitch;
		bool bMovementDirChanged = false;
		quatInverse(qFaceSpace, qFaceSpaceInv);

		// Calculate pitch from facing
		{
			const DynAction* pCurrentAction = (pSkeleton->eaSqr && pSkeleton->eaSqr[0])?dynSeqGetCurrentAction(pSkeleton->eaSqr[0]):NULL;
			if (pCurrentAction && pCurrentAction->bPitchToTarget)
			{
				Vec3 vToTargetFS;
				quatRotateVec3Inline(qFaceSpaceInv, vToTargetWS, vToTargetFS);
				fFacingPitch = getVec3Pitch(vToTargetFS);
				fFacingPitch = CLAMP(fFacingPitch, -QUARTERPI, QUARTERPI);
			}
			else
				fFacingPitch = 0.0f;
		}

		// Now, calculate what the movement yaw must be by projecting the movement dir into face space
		{
			Vec3 vMovementDir;
			Vec3 vMovementDirFS;
			quatRotateVec3Inline(xLocationWS.qRot, forwardvec, vMovementDir);
			quatRotateVec3Inline(qFaceSpaceInv, vMovementDir, vMovementDirFS);
			fMovementYawFS = getVec3Yaw(vMovementDirFS);
			assert(FINITE(fMovementYawFS)); //leads to later crash during render (since it sets bad orientation data on the root node's parent and skeleton torso)
			if (!sameVec3(vMovementDir, pSkeleton->vOldMovementDir))
				bMovementDirChanged = true;
			copyVec3(vMovementDir, pSkeleton->vOldMovementDir);

			if (dynDebugState.bDrawTorsoPointing)
			{
				Vec3 ptA, ptB;
				copyVec3(xLocationWS.vPos, ptA);
				scaleAddVec3(pSkeleton->vOldMovementDir, 10.f, ptA, ptB);
				ptA[1] += 0.1f;
				ptB[1] = ptA[1];
				wlDrawLine3D_2(ptA, 0xFF0000FF, ptB, 0xFF0000FF);
			}

		}

		// Calculate the movement direction bits from the movement yaw (relative to facing direction)
		{
			F32 fAbsYaw = fabsf(fMovementYawFS);
			if (fAbsYaw < (PI * 0.125f))
			{
				pSkeleton->eTargetDir = ETargetDir_Forward;
			}
			else if (fAbsYaw > (PI * 0.875f))
			{
				pSkeleton->eTargetDir = ETargetDir_Back;
			}
			else
			{
				bool bLeft = (fMovementYawFS < 0.0f);
				if (fAbsYaw > (PI * 0.625f))
				{
					pSkeleton->eTargetDir = bLeft?ETargetDir_LeftBack:ETargetDir_RightBack;
				}
				else if (fAbsYaw > (PI * 0.375f))
				{
					pSkeleton->eTargetDir = bLeft?ETargetDir_Left:ETargetDir_Right;
				}
				else
				{
					pSkeleton->eTargetDir = bLeft?ETargetDir_LeftForward:ETargetDir_RightForward;
				}
			}
		}

		{
			const F32 fMaxTwistTime = 4.0f;
			const F32 fIdleYawRate = 1.5f;
			F32 fMoveYawRate = 4.0f;

			pSkeleton->eTurnState = ETurnState_None;

			// Pop the target yaw by multiples of PI/4 rads
			if (bMovementDirChanged || pSkeleton->fCurrentSpeedXZ > 0.0f)
			{
				bool bWasStopped = false;

				if (dynDebugState.bEnableOldAnimTorsoPointingFix &&
					eaSize(&pSkeleton->eaSqr) > 1)
				{
					if (dynSeqMovementWasStopped(pSkeleton->eaSqr[1]))
					{
						F32 fRound = ((F32)round(fMovementYawFS / QUARTERPI)) * QUARTERPI;
						fMovementYawFS -= fRound;
						bWasStopped = true;
						//printfColor(COLOR_RED,"Stopped\n");
					}
					else
					{
						//old movement dir should actually be the current movement dir at this point
						F32 fTargetDot = dotVec3(pSkeleton->vOldMovementDir, mFaceSpace[2]);
						F32 fTargetSign = SIGN(dotVec3(pSkeleton->vOldMovementDir, mFaceSpace[0]));
						F32 fTerm1 = -1.f*dynSeqMovementCalcBlockYaw(pSkeleton->eaSqr[1], &fMoveYawRate, &pSkeleton->fMovementYawStopped, pSkeleton->eTargetDir);
						F32 fTerm2 = fTargetSign*acosf(MINMAX(fTargetDot,-1.f,1.f));
						fMovementYawFS = fTerm1 + fTerm2;
						//printfColor(COLOR_RED, "Moving %f = %f + %f\n", DEG(fMovementYawFS), DEG(fTerm1), DEG(fTerm2));
					}
					
					//apply torso pointing damper (noise reduction)
					{
						F32 fOldAngle = pSkeleton->fMovementYawFS;
						F32 fNewAngle = fMovementYawFS;
						F32 fUseAngle;

						if (fOldAngle < 0) fOldAngle += TWOPI;
						if (fNewAngle < 0) fNewAngle += TWOPI;

						if (fNewAngle >= fOldAngle)
						{
							if (fNewAngle - fOldAngle > PI)
							{
								fOldAngle += TWOPI;
								fUseAngle = fOldAngle - fNewAngle;
							}
							else
							{
								fUseAngle = fNewAngle - fOldAngle;
							}
						}
						else 
						{
							if (fOldAngle - fNewAngle > PI)
							{
								fNewAngle += TWOPI;
								fUseAngle = fNewAngle - fOldAngle;
							}
							else
							{
								fUseAngle = fOldAngle - fNewAngle;
							}
						}

						if (fUseAngle > PI)
						{
							fUseAngle -= TWOPI;
						}

						if (fNewAngle > fOldAngle)
							fMovementYawFS = pSkeleton->fMovementYawFS + 0.5*fUseAngle;
						else
							fMovementYawFS = pSkeleton->fMovementYawFS - 0.5*fUseAngle;

						//if (fNewAngle > PI) fNewAngle -= TWOPI;
						//if (fOldAngle > PI) fOldAngle -= TWOPI;
						//printfColor(COLOR_BLUE, "new: %f, old: %f, use: %f, fMovementYawFS: %f, fMovementYawFSOLD: %f\n", DEG(fNewAngle), DEG(fOldAngle), DEG(fUseAngle), DEG(fMovementYawFS), DEG(pSkeleton->fMovementYawFS));
					}
				}
				else
				{
					F32 fRound = ((F32)round(fMovementYawFS / QUARTERPI)) * QUARTERPI;
					fMovementYawFS -= fRound;
				}

				{
					F32 fYawDiff = fMovementYawFS - pSkeleton->fMovementYawFS;
					if (fYawDiff > PI)
						fYawDiff -= TWOPI;
					else if (fYawDiff < -PI)
						fYawDiff += TWOPI;
					{
						F32 fYawChange = SIGN(fYawDiff) * fMoveYawRate * fDeltaTime;
						if (fabsf(fYawChange) > fabsf(fYawDiff))
							pSkeleton->fMovementYawFS = fMovementYawFS;
						else
							pSkeleton->fMovementYawFS += fYawChange;
					}

					if (bWasStopped)
						pSkeleton->fMovementYawStopped = pSkeleton->fMovementYawFS;
				}

				pSkeleton->fYawInterp = -1.0f;
				pSkeleton->iYawMovementState = 1;
				pSkeleton->fTimeIdle = 0.0f;
				pSkeleton->fMovementYawOffsetFS = pSkeleton->fMovementYawOffsetFSOld = 0.0f;
			}
			else
			{
				F32 fAdjustedYaw;
				if (pSkeleton->iYawMovementState)
				{
					pSkeleton->fMovementYawOffsetFS = fMovementYawFS - pSkeleton->fMovementYawFS;
					pSkeleton->iYawMovementState = 0;
					pSkeleton->fYawInterp = -1.0f;
				}

				fAdjustedYaw = fMovementYawFS - pSkeleton->fMovementYawOffsetFS;

				if (pSkeleton->fYawInterp < 0.0f)
				{
					pSkeleton->fTimeIdle += fDeltaTime;
					if (fabsf(fAdjustedYaw) > RAD(35) || pSkeleton->fTimeIdle > fMaxTwistTime)
					{
						pSkeleton->fYawInterp = 0.0f;
						pSkeleton->fMovementYawOffsetFSOld = pSkeleton->fMovementYawOffsetFS;
						pSkeleton->fTimeIdle = 0.0f;
					}
				}


				if (pSkeleton->fYawInterp >= 0.0f)
				{
					pSkeleton->fYawInterp += fDeltaTime * fIdleYawRate;
					if (pSkeleton->fYawInterp > 1.0f)
					{
						pSkeleton->fMovementYawOffsetFS = fMovementYawFS;
						pSkeleton->fYawInterp = -1.0f;
					}
					else
					{
						F32 fInterpValue = pSkeleton->fYawInterp;
						F32 fYawDiff = fMovementYawFS - pSkeleton->fMovementYawOffsetFSOld;
						if (fYawDiff > PI)
							fYawDiff -= TWOPI;
						else if (fYawDiff < -PI)
							fYawDiff += TWOPI;

						if (fInterpValue < 0.5f)
						{
							F32 fTwiceInterp = fInterpValue * 2.0f;
							fInterpValue = 0.5f * CUBE(fTwiceInterp);
						}
						else
						{
							F32 fTwiceOneMinusInput = 2.0f * (1.0f - fInterpValue);
							fInterpValue = 1.0f - (0.5f * CUBE(fTwiceOneMinusInput));
						}

						if (fYawDiff > 0)
							pSkeleton->eTurnState = ETurnState_Left;
						else
							pSkeleton->eTurnState = ETurnState_Right;

						pSkeleton->fMovementYawFS = fMovementYawFS - (pSkeleton->fMovementYawOffsetFSOld + interpF32(fInterpValue, 0.0f, fYawDiff));
					}
				}
				else
				{
					pSkeleton->fMovementYawFS = fAdjustedYaw;
				}

				if (eaSize(&pSkeleton->eaSqr) > 1) {
					dynSeqSetStoppedYaw(pSkeleton->eaSqr[1]);
				}
			}
		}

		if (pSkeleton->fMovementYawFS > PI)
			pSkeleton->fMovementYawFS -= TWOPI;
		else if (pSkeleton->fMovementYawFS < -PI)
			pSkeleton->fMovementYawFS += TWOPI;


		// Prevent max twist from ever getting above 55 degrees.
		if (fabsf(pSkeleton->fMovementYawFS) > RAD(55))
		{
			pSkeleton->fMovementYawFS = RAD(55) * SIGN(pSkeleton->fMovementYawFS);
		}

		// Update the current facing pitch of the skeleton
		{
			const F32 fPitchRate = 1.5f;
			F32 fPitchDiff = fFacingPitch - pSkeleton->fFacingPitch;
			if (fPitchDiff > PI)
				fPitchDiff -= TWOPI;
			else if (fPitchDiff < -PI)
				fPitchDiff += TWOPI;
			{
				F32 fPitchChange = SIGN(fPitchDiff) * fPitchRate * fDeltaTime;
				if (fabsf(fPitchChange) > fabsf(fPitchDiff))
					pSkeleton->fFacingPitch = fFacingPitch;
				else
					pSkeleton->fFacingPitch += fPitchChange;
			}
		}

		if (!pSkeleton->bTorsoPointing)
		{
			pSkeleton->fMovementYawFS = 0.0;
			pSkeleton->fFacingPitch = 0.0;
		}
		else
		{
			F32 fUseTime;

			if (dynDebugState.bDisableTorsoPointing ||
				eaSize(&pSkeleton->eaSqr) == 0)
			{
					pSkeleton->fTorsoPointingBlendFactor = 0.0f;
			}
			else if ((fUseTime = dynSeqDisableTorsoPointingTime(pSkeleton->eaSqr[0])) > 0.0f)
			{
				pSkeleton->fTorsoPointingBlendFactor -= fDeltaTime / fUseTime;
				MAX1(pSkeleton->fTorsoPointingBlendFactor, 0.0f);
			}
			else if ((fUseTime = dynSeqEnableTorsoPointingTime(pSkeleton->eaSqr[0])) > 0.0f)
			{
				pSkeleton->fTorsoPointingBlendFactor += fDeltaTime / fUseTime;
				MIN1(pSkeleton->fTorsoPointingBlendFactor, 1.0f);
			}
			else
			{
				pSkeleton->fTorsoPointingBlendFactor += 4.0f * fDeltaTime;
				MIN1(pSkeleton->fTorsoPointingBlendFactor, 1.0f);
			}

			pSkeleton->fMovementYawFS *= pSkeleton->fTorsoPointingBlendFactor;
		}

		// Now, determine the root transform from the movement yaw and face space
		// and the 'upper quat' in that space
		{
			Quat qMovementYawFS;
			Quat qRootWS;
			Quat qYaw, qPitch;

			yawQuat(-pSkeleton->fMovementYawFS, qMovementYawFS);
			quatMultiply(qFaceSpace, qMovementYawFS, qRootWS);

			dynNodeSetRotInline(pSkeleton->pRoot->pParent, qRootWS);
			pSkeleton->pRoot->pParent->uiDirtyBits = 1;

			yawQuat(pSkeleton->fMovementYawFS, qYaw);
			pitchQuat(pSkeleton->fFacingPitch, qPitch);
			quatMultiply(qYaw, qPitch, pSkeleton->qTorso);

			if (dynDebugState.bDrawTorsoPointing)
			{
				Vec3 vLine[2];
				Vec3 vRootForward;
				copyVec3(xLocationWS.vPos, vLine[0]);
				vLine[0][1] += 0.1f;
				quatRotateVec3Inline(qRootWS, forwardvec, vRootForward);
				scaleAddVec3(vRootForward, 10.0f, vLine[0], vLine[1]);
				wlDrawLine3D_2(vLine[0], 0xFFFFFFFF, vLine[1], 0xFFFFFFFF);
			}
		}
	}
	else
	{
		if (pSkeleton->ragdollState.bRagdollOn)
		{
			Vec3 vNewRoot;
			Quat qNewRoot;

			// first, use the hips as the root
			{
				U32 uiPart;
				for (uiPart=0; uiPart<pSkeleton->ragdollState.uiNumParts; ++uiPart)
				{
					DynRagdollPartState* pPart = &pSkeleton->ragdollState.aParts[uiPart];
					if (!pPart->pcParentBoneName)
					{
						copyQuat(pPart->qWorldSpace, xLocationWS.qRot);
						break;
					}
				}
				copyVec3(pSkeleton->ragdollState.vHipsWorldSpace, xLocationWS.vPos);
			}

			quatRotateVec3Inline(xLocationWS.qRot, pSkeleton->ragdollState.vRootOffset, vNewRoot);
			addVec3(vNewRoot, xLocationWS.vPos, vNewRoot);
			dynNodeSetPosInline(pSkeleton->pRoot->pParent, vNewRoot);

			quatMultiply(xLocationWS.qRot, pSkeleton->ragdollState.qRootOffset, qNewRoot);
			dynNodeSetRotInline(pSkeleton->pRoot->pParent, qNewRoot);
			pSkeleton->pRoot->uiDirtyBits = 1;
		}
		else if (pSkeleton->bHasTarget && !pSkeleton->ragdollState.bRagdollOn)
		{
			dynNodeSetRotInline(pSkeleton->pRoot->pParent, qFaceSpace);
		}
		else if (pSkeleton->pLocation)
			dynNodeSetRotInline(pSkeleton->pRoot->pParent, xLocationWS.qRot);

		pSkeleton->pRoot->pParent->uiDirtyBits = 1;
		unitQuat(pSkeleton->qTorso);
		pSkeleton->eTargetDir = ETargetDir_Forward;
	}
}

static void dynSkeletonUpdateClientSideRagdollState(DynSkeleton *pSkeleton, F32 fDeltaTime, U32 uiThreaded)
{
	assert(pSkeleton->bHasClientSideRagdoll ^ pSkeleton->bHasClientSideTestRagdoll);
	
	//update the ragdoll state parts from the client side bodies, note that the ragdoll
	//state parts can also be used to receive data when using server-based version
	if (!pSkeleton->bSleepingClientSideRagdoll)
	{
		U32 uiBodyCount = eaSize(&pSkeleton->eaClientSideRagdollBodies);
		bool bOutOfRange = false;

		if (uiBodyCount != pSkeleton->ragdollState.uiNumParts){
			SAFE_FREE(pSkeleton->ragdollState.aParts);
			pSkeleton->ragdollState.uiNumParts = uiBodyCount;
			pSkeleton->ragdollState.aParts = calloc(sizeof(DynRagdollPartState), uiBodyCount);
			EARRAY_CONST_FOREACH_BEGIN(pSkeleton->eaClientSideRagdollBodies, i, isize);
			{
				DynClientSideRagdollBody *pBody = pSkeleton->eaClientSideRagdollBodies[i]->body.pBody;

				pSkeleton->ragdollState.aParts[i].pcBoneName = pBody->pcBone;
				pSkeleton->ragdollState.aParts[i].pcParentBoneName = pBody->pcParentBone;
				pSkeleton->ragdollState.aParts[i].eShape = pBody->eShape;
				pSkeleton->ragdollState.aParts[i].pSplat = NULL;
				copyVec3(pBody->vCenterOfGravity, pSkeleton->ragdollState.aParts[i].vCenterOfGravity);

				if (pBody->eShape == eRagdollShape_Box) {
					copyVec3(pBody->vBoxDimensions, pSkeleton->ragdollState.aParts[i].vBoxDimensions);
				} else if (pBody->eShape == eRagdollShape_Capsule) {
					copyVec3(pBody->vCapsuleDir, pSkeleton->ragdollState.aParts[i].vCapsuleDir);
					pSkeleton->ragdollState.aParts[i].fCapsuleLength = pBody->fCapsuleLength;
					pSkeleton->ragdollState.aParts[i].fCapsuleRadius = pBody->fCapsuleRadius;
				}
			}
			EARRAY_FOREACH_END;
		}

		EARRAY_CONST_FOREACH_BEGIN(pSkeleton->eaClientSideRagdollBodies, i, isize);
		{
			DynClientSideRagdollBody *pBody = pSkeleton->eaClientSideRagdollBodies[i]->body.pBody;
			if (15000.f <= fabsf(pBody->vWorldSpace[0]) ||
				15000.f <= fabsf(pBody->vWorldSpace[1]) ||
				15000.f <= fabsf(pBody->vWorldSpace[2]) )
			{
				bOutOfRange = true;
				if (!uiThreaded) {
					pSkeleton->bSleepingClientSideRagdoll = true;
					dynSkeletonClientSideRagdollFree(pSkeleton);
				}
				break;
			}
			else if (pBody->uiTesterCollided)
			{
				bOutOfRange = true;
				pSkeleton->bCreateClientSideRagdoll = true;
				pSkeleton->fClientSideRagdollAnimTime = pSkeleton->fClientSideRagdollAnimTimePreCollision;
				break;
			}
		}
		EARRAY_FOREACH_END;

		pSkeleton->fClientSideRagdollAnimTimePreCollision = pSkeleton->fClientSideRagdollAnimTime;
		pSkeleton->fClientSideRagdollAnimTime += fDeltaTime;

		if (!bOutOfRange) {
			bool bAllPartsSleeping = true;

			EARRAY_CONST_FOREACH_BEGIN(pSkeleton->eaClientSideRagdollBodies, i, isize);
			{
				DynClientSideRagdollBody *pBody = pSkeleton->eaClientSideRagdollBodies[i]->body.pBody;

				copyQuat(pBody->qWorldSpace, pSkeleton->ragdollState.aParts[i].qWorldSpace);
				copyVec3(pBody->vWorldSpace, pSkeleton->ragdollState.aParts[i].vWorldSpace);

				if (!pBody->uiSleeping) {
					bAllPartsSleeping = false;
				}

				if (!pBody->pcParentBone)
				{
					copyVec3(pBody->vWorldSpace, pSkeleton->ragdollState.vHipsWorldSpace);
					PYRToQuat(pBody->pyrOffsetToAnimRoot, pSkeleton->ragdollState.qRootOffset);
					copyVec3(pBody->posOffsetToAnimRoot, pSkeleton->ragdollState.vRootOffset);
				}

				if (dynDebugState.bDebugPhysics)
				{
					Vec3 vCenterOfGravityWS;
					Vec3 vBoxMin, vBoxMax;
					Mat4 mDebugCG;

					vBoxMin[0] = vBoxMin[1] = vBoxMin[2] = -0.15f;
					vBoxMax[0] = vBoxMax[1] = vBoxMax[2] =  0.15f;

					//draw a red box to represent the joint
					quatToMat(pBody->qWorldSpace, mDebugCG);
					copyVec3(pBody->vWorldSpace, mDebugCG[3]);
					wlDrawBox3D(vBoxMin, vBoxMax, mDebugCG, !pBody->uiSleeping?0xFFFF0000:0xFF880000, 5.0f);

					//draw a yellow box to represent the center of mass
					mulVecMat4(pBody->vCenterOfGravity, mDebugCG, vCenterOfGravityWS);
					copyVec3(vCenterOfGravityWS, mDebugCG[3]);
					wlDrawBox3D(vBoxMin, vBoxMax, mDebugCG, !pBody->uiSleeping?0xFFFFFF00:0xFF888800, 5.0f);
				}
			}
			EARRAY_FOREACH_END;

			//if the physics simulation went to sleep, remove it as an optimization
			if (!uiThreaded &&
				bAllPartsSleeping &&
				pSkeleton->ragdollState.bRagdollOn)
			{
				pSkeleton->bSleepingClientSideRagdoll = true;
				dynSkeletonClientSideRagdollFree(pSkeleton);
			}

			//modify relevant data based on the ragdoll state parts
			{
				DynTransform xLocationWS;
				Vec3 vNewRoot;
				Quat qNewRoot;
				U32 uiPart;

				if (pSkeleton->ragdollState.bRagdollOn)
				{
					//determine the hips world space transform
					for (uiPart = 0; uiPart < pSkeleton->ragdollState.uiNumParts; ++uiPart)
					{
						DynRagdollPartState* pPart = &pSkeleton->ragdollState.aParts[uiPart];
						if (!pPart->pcParentBoneName)
						{
							copyQuat(pPart->qWorldSpace, xLocationWS.qRot);
							break;
						}
					}
					copyVec3(pSkeleton->ragdollState.vHipsWorldSpace, xLocationWS.vPos);

					//set the skeleton's root based on offsetting from the hips physics simulation location
					quatRotateVec3Inline(xLocationWS.qRot, pSkeleton->ragdollState.vRootOffset, vNewRoot);
					addVec3(vNewRoot, xLocationWS.vPos, vNewRoot);
					dynNodeSetPosInline(pSkeleton->pRoot->pParent, vNewRoot);

					quatMultiply(xLocationWS.qRot, pSkeleton->ragdollState.qRootOffset, qNewRoot);
					dynNodeSetRotInline(pSkeleton->pRoot->pParent, qNewRoot);
					pSkeleton->pRoot->pParent->uiDirtyBits = 1;
				}
				

				//convert the joint rotations from world space to local space
				for (uiPart=0; uiPart<pSkeleton->ragdollState.uiNumParts; ++uiPart)
				{
					DynRagdollPartState* pPart = &pSkeleton->ragdollState.aParts[uiPart];
					if (pPart->pcParentBoneName)
					{
						U32 uiPart2;
						for (uiPart2=0; uiPart2<pSkeleton->ragdollState.uiNumParts; ++uiPart2)
						{
							DynRagdollPartState* pPart2 = &pSkeleton->ragdollState.aParts[uiPart2];
							if (pPart2->pcBoneName == pPart->pcParentBoneName)
							{
								// Found parent.
								Quat qParentInv;
								quatInverse(pPart2->qWorldSpace, qParentInv);
								quatMultiply(pPart->qWorldSpace, qParentInv, pPart->qLocalSpace);
								break;
							}
						}
					}
				}
			}
		}
	}
}

static void dynSkeletonUpdateMovementDirection(DynSkeleton *pSkeleton, F32 fDeltaTime)
{
	const DynAnimGraph *pCurrentGraph = eaSize(&pSkeleton->eaAGUpdater)?pSkeleton->eaAGUpdater[0]->pCurrentGraph:NULL;
	const WLCostume *wlCostume = GET_REF(pSkeleton->hCostume);
	Vec3 vToTargetWS, vUpVectorWS;
	DynTransform xLocationWS;
	bool bPointToTarget;
	
	//set the ragdoll's on/off bit
	if (!pSkeleton->bHasClientSideRagdoll &&
		!pSkeleton->bSleepingClientSideRagdoll &&
		pSkeleton->ragdollStateFunc)
	{
		pSkeleton->ragdollState.bRagdollOn = !!pSkeleton->ragdollStateFunc(pSkeleton, pSkeleton->preUpdateData);
	}

	//set the point to target bit
	bPointToTarget =	pSkeleton->bUseTorsoPointing		&&
						pSkeleton->bHasTarget				&&
						!pSkeleton->bIsDead					&&
						!pSkeleton->ragdollState.bRagdollOn;

	//grab the skeleton's world space data
	if (pSkeleton->pLocation)
		dynNodeGetWorldSpaceTransform(pSkeleton->pLocation, &xLocationWS);
	else
	{
		pSkeleton->pRoot->uiDirtyBits = 1;
		dynNodeGetWorldSpaceTransform(pSkeleton->pRoot, &xLocationWS);
	}

	//compute the orientation from the skeleton to the target
	if (pSkeleton->bHasTarget &&
		!pSkeleton->ragdollState.bRagdollOn)
	{
		//compute a vector pointing to the target
		subVec3(pSkeleton->vTargetPos, xLocationWS.vPos, vToTargetWS);
		if (!pSkeleton->bTorsoPointing && pSkeleton->bUseTorsoPointing)
			vToTargetWS[1] = 0.0f;

		//compute a vector pointing up
		{
			quatRotateVec3Inline(xLocationWS.qRot, upvec, vUpVectorWS);
			normalVec3(vToTargetWS);

			// Make sure vToTarget is not colinear w/ vUpVector
			if (fabsf(dotVec3(vUpVectorWS, vToTargetWS)) > 0.9999f)
			{
				// for now just snap the vToTargetWS to something else
				vToTargetWS[0] = vUpVectorWS[1];
				vToTargetWS[1] = vUpVectorWS[2];
				vToTargetWS[2] = vUpVectorWS[0];
			}
		}

		// Now, using up vector and non-colinear to-target vector, create an orthonormal rotation we call "face space"
		{
			orientMat3ToNormalAndForward(pSkeleton->mFaceSpace, vUpVectorWS, vToTargetWS);
			mat3ToQuat(pSkeleton->mFaceSpace, pSkeleton->qFaceSpace);
		}
	}else{
		//no torso pointing, just set it straight ahead
		zeroVec3(vToTargetWS);
		vToTargetWS[2] = 1.0f;
		unitQuat(pSkeleton->qFaceSpace);
	}

	//set the pitch for terrain tilting as part of the face space
	dynSkeletonCalcTerrainPitch(pSkeleton, fDeltaTime);
	if (SAFE_MEMBER(wlCostume, bTerrainTiltModifyRoot))
	{
		Quat qPitchedFaceSpace;
		quatMultiply(pSkeleton->qTerrainPitch, pSkeleton->qFaceSpace, qPitchedFaceSpace);
		copyQuat(qPitchedFaceSpace,pSkeleton->qFaceSpace);
	}

	//pre-compute the face space inverse
	quatInverse(pSkeleton->qFaceSpace, pSkeleton->qFaceSpaceInv);

	//update the skeleton's target
	copyVec3(vToTargetWS, pSkeleton->vToTargetWS);

	//update the old to target
	if (!dynDebugState.bDisableTorsoPointingFix)
	{
		assert(FINITEVEC3(pSkeleton->vOldToTargetWS));
		assert(FINITEVEC3(pSkeleton->vToTargetWS));
		scaleAddVec3(pSkeleton->vOldToTargetWS, 2.0, pSkeleton->vToTargetWS, pSkeleton->vOldToTargetWS);
		normalVec3(pSkeleton->vOldToTargetWS);
	}

	//determine the movement direction
	quatRotateVec3Inline(xLocationWS.qRot, forwardvec, pSkeleton->vMovementDir);
	pSkeleton->bMovementDirChanged = !sameQuat(xLocationWS.qRot, pSkeleton->qPrevLocation);
	copyQuat(xLocationWS.qRot, pSkeleton->qPrevLocation);

	if (bPointToTarget)
	{
		F32 fFacingPitch = 0.0f;
		F32  fMovementYawFS = 0.0f;
		Vec3 vMovementDirFS;
		quatRotateVec3Inline(pSkeleton->qFaceSpaceInv, pSkeleton->vMovementDir, vMovementDirFS);
		fMovementYawFS = getVec3Yaw(vMovementDirFS);
		assert(FINITE(fMovementYawFS)); //leads to later crash during rendering (since it sets bad orientation data on the root node's parent and skeleton torso)
		if (fMovementYawFS > PI)
			fMovementYawFS -= TWOPI;
		else if (fMovementYawFS < -PI)
			fMovementYawFS += TWOPI;
		pSkeleton->fMovementAngleOld = pSkeleton->fMovementAngle;
		pSkeleton->fMovementAngle = fMovementYawFS;

		// Calculate the movement direction bits from the movement yaw (relative to facing direction)
		{
			F32 fAbsYaw = fabsf(fMovementYawFS);
			if (fAbsYaw < (PI * 0.125f))
			{
				pSkeleton->eTargetDir = ETargetDir_Forward;
			}
			else if (fAbsYaw > (PI * 0.875f))
			{
				pSkeleton->eTargetDir = ETargetDir_Back;
			}
			else
			{
				bool bLeft = (fMovementYawFS < 0.0f);
				if (fAbsYaw > (PI * 0.625f))
				{
					pSkeleton->eTargetDir = bLeft?ETargetDir_LeftBack:ETargetDir_RightBack;
				}
				else if (fAbsYaw > (PI * 0.375f))
				{
					pSkeleton->eTargetDir = bLeft?ETargetDir_Left:ETargetDir_Right;
				}
				else
				{
					pSkeleton->eTargetDir = bLeft?ETargetDir_LeftForward:ETargetDir_RightForward;
				}
			}
		}
	}
	else
	{
		if (!pSkeleton->ragdollState.bRagdollOn)
		{
			if (pSkeleton->bHasTarget)
			{
				dynNodeSetRotInline(pSkeleton->pRoot->pParent, pSkeleton->qFaceSpace);
			}
			else if (pSkeleton->pLocation)
			{
				if (SAFE_MEMBER(wlCostume, bTerrainTiltModifyRoot))
				{
					Quat qPitchedLoc;
					quatMultiply(pSkeleton->qTerrainPitch, xLocationWS.qRot, qPitchedLoc);
					dynNodeSetRotInline(pSkeleton->pRoot->pParent, qPitchedLoc);
				}
				else
				{
					dynNodeSetRotInline(pSkeleton->pRoot->pParent, xLocationWS.qRot);
				}
			}
		}

		pSkeleton->pRoot->pParent->uiDirtyBits = 1;
		unitQuat(pSkeleton->qTorso);
		FOR_EACH_IN_EARRAY_FORWARDS(pSkeleton->eaRunAndGunBones, DynSkeletonRunAndGunBone, pRGBone)
		{
			unitQuat(pRGBone->qPostRoot);
		}
		FOR_EACH_END;
	}

	if (!pSkeleton->bHasClientSideRagdoll &&
		!pSkeleton->bSleepingClientSideRagdoll)
	{
		Vec3 vPos, vScale;
		if (pSkeleton->pParentSkeleton) {
			dynNodeGetLocalPosInline(pSkeleton->pGenesisSkeleton->pFacespaceNode, vPos);
			dynNodeGetLocalScaleInline(pSkeleton->pGenesisSkeleton->pFacespaceNode, vScale);
			dynNodeSetRotInline(pSkeleton->pFacespaceNode, pSkeleton->pGenesisSkeleton->qFaceSpace);
		} else {
			dynNodeGetLocalPosInline(pSkeleton->pRoot->pParent, vPos);
			dynNodeGetLocalScaleInline(pSkeleton->pRoot->pParent, vScale);
			dynNodeSetRotInline(pSkeleton->pFacespaceNode, pSkeleton->qFaceSpace);
		}
		dynNodeSetPosInline(pSkeleton->pFacespaceNode, vPos);
		dynNodeSetScaleInline(pSkeleton->pFacespaceNode, vScale);
		pSkeleton->pFacespaceNode->uiDirtyBits = 1;
	}
}

static void dynSkeletonUpdateRootNew(DynSkeleton* pSkeleton, F32 fDeltaTime)
{
	const DynAnimGraph *pCurrentGraph = NULL;
	U32 uiNumMovementDirections = 0;
	DynTransform xLocationWS;
	bool bPointToTarget;

	if (eaSize(&pSkeleton->eaAGUpdater)) {
		pCurrentGraph = pSkeleton->eaAGUpdater[0]->pCurrentGraph;
	}

	if (!gConf.bUseMovementGraphs) {
		if (eaSize(&pSkeleton->movement.eaBlocks))
			uiNumMovementDirections = SAFE_MEMBER(pSkeleton->movement.eaBlocks[0]->pChart,uiNumMovementDirections);
	} else {
		if (eaSize(&pSkeleton->eaAGUpdater) > 1)
			uiNumMovementDirections = pSkeleton->eaAGUpdater[1]->nodes[0].pGraphNode->eNumDirections;
	}

	//set terrain tilting stances & overlay blend
	if (pSkeleton->fTerrainPitch < 0.f) {
		dynSkeletonSwapStanceWordsInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceTerrainTiltDown, pcStanceTerrainTiltUp);
	} else {
		dynSkeletonSwapStanceWordsInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceTerrainTiltUp, pcStanceTerrainTiltDown);
	}

	//set the point to target bit
	bPointToTarget =	pSkeleton->bUseTorsoPointing		&&
						pSkeleton->bHasTarget				&&
						!pSkeleton->bIsDead					&&
						!pSkeleton->ragdollState.bRagdollOn;

	//grab the skeleton's world space data
	if (pSkeleton->pLocation)
		dynNodeGetWorldSpaceTransform(pSkeleton->pLocation, &xLocationWS);
	else
	{
		pSkeleton->pRoot->uiDirtyBits = 1;
		dynNodeGetWorldSpaceTransform(pSkeleton->pRoot, &xLocationWS);
	}

	//update the recent location data
	copyVec2(pSkeleton->vMovementPosXZ[1], pSkeleton->vMovementPosXZ[2]);
	copyVec2(pSkeleton->vMovementPosXZ[0], pSkeleton->vMovementPosXZ[1]);
	pSkeleton->vMovementPosXZ[0][0] = xLocationWS.vPos[0];
	pSkeleton->vMovementPosXZ[0][1] = xLocationWS.vPos[2];

	if (dynDebugState.bDrawTorsoPointing)
	{
		{
			//draw a line from the skeleton's location to the target's position
			Vec3 ptA, ptB, ptC;
			copyVec3(xLocationWS.vPos, ptA);
			copyVec3(pSkeleton->vTargetPos, ptB);
			copyVec3(pSkeleton->vTargetPos, ptC);
			ptA[1] += 0.01;
			ptB[1] = ptA[1];
			ptC[1] += 0.01;
			if (bPointToTarget) {
				wlDrawLine3D_2(ptA, 0xFF00FF00, ptB, 0xFF00FF00);
				wlDrawLine3D_2(ptA, 0xFF008800, ptC, 0xFF008800);
			}
			else {
				wlDrawLine3D_2(ptA, 0xFFFF0000, ptB, 0xFFFF0000);
				wlDrawLine3D_2(ptA, 0xFF880000, ptC, 0xFF880000);
			}
		}
		{
			//draw the face space vectors
			Vec3 vFsX, vFsY, vFsZ;
			int color;

			if (!dynDebugState.bDisableTorsoPointingFix) {
				if (uiNumMovementDirections == 1) {
					color = 0xFFFF0000; //red == 1D
				} else if (uiNumMovementDirections == 8) {
					color = 0xFF0000FF; //blue == 8D
				} else {
					color = 0xFF00FF00; // green = everything else
				}
			} else {
				color = 0xFF00FF00; //green == everything else
			}

			scaleAddVec3(pSkeleton->mFaceSpace[0], 5, xLocationWS.vPos, vFsX);
			scaleAddVec3(pSkeleton->mFaceSpace[1], 5, xLocationWS.vPos, vFsY);
			scaleAddVec3(pSkeleton->mFaceSpace[2], 5, xLocationWS.vPos, vFsZ);
			wlDrawLine3D_2(xLocationWS.vPos, color, vFsX, color);
			wlDrawLine3D_2(xLocationWS.vPos, color, vFsY, color);
			wlDrawLine3D_2(xLocationWS.vPos, color, vFsZ, color);
		}
	}

	//banking
	{
		bool bBlendBankIn = false;
		F32 fBank = 0.0f;

		pSkeleton->fMovementBank = 0.0f;

		if (pSkeleton->fCurrentSpeedXZ > 0.0f)
		{
			Vec2 v1, v2;
			F32 fDot;
			F32 fBankMaxAngle, fBankScale;
			F32 fBankCrossY, fBankDot;
			F32 fBankNew;

			//decide if we're blending in or out
			subVec2(pSkeleton->vMovementPosXZ[0], pSkeleton->vMovementPosXZ[1], v1);
			subVec2(pSkeleton->vMovementPosXZ[1], pSkeleton->vMovementPosXZ[2], v2);
			normalVec2(v1);
			normalVec2(v2);
			fDot = dotVec2(v1,v2);
			if (acosf(MINMAX(fDot,-1.f,1.f)) > RAD(0.5)) {
				bBlendBankIn = true;
			}

			//determine the coordinate system change data
			if      (!gConf.bUseMovementGraphs)            dynSkeletonMovementCalcBlockBank(&pSkeleton->movement, &fBankMaxAngle, &fBankScale);
			else if (eaSize(&pSkeleton->eaAGUpdater) > 1)  dynAnimGraphUpdaterCalcNodeBank(pSkeleton->eaAGUpdater[1], &fBankMaxAngle, &fBankScale);
			else {
				fBankMaxAngle = 0.f;
				fBankScale = 0.f;
			}

			fBankCrossY =	pSkeleton->vMovementDir[2] * pSkeleton->vOldMovementDir[0] -
							pSkeleton->vMovementDir[0] * pSkeleton->vOldMovementDir[2];
			fBankDot = dotVec3(pSkeleton->vMovementDir, pSkeleton->vOldMovementDir);

			//determine the difference in what the skeleton's bank should be from the last displayed frame
			fBank = fBankScale * SIGN(fBankCrossY) * -1.0f*acosf(MINMAX(fBankDot,-1.f,1.f))/PI;
			MINMAX1(fBank, RAD(-fBankMaxAngle), RAD(fBankMaxAngle));
			fBankNew = lerp(fBank, pSkeleton->fMovementBankOld, 0.5); //lerp for smoothing
			if (fabsf(fBankNew) <= fabsf(pSkeleton->fMovementBank) &&
				(	fBankNew <= 0 && pSkeleton->fMovementBank <= 0 ||
					fBankNew >= 0 && pSkeleton->fMovementBank >= 0 ))
			{
				//instantly goto the bank
				pSkeleton->fMovementBank = fBankNew;
			}
			else
			{
				//ease into the bank
				F32 fBankDiff, fBankRateLimit;
				fBankDiff = fBankNew - pSkeleton->fMovementBankOld;
				fBankRateLimit = RAD(90.0f) * fDeltaTime;
				MINMAX1(fBankDiff, -fBankRateLimit, fBankRateLimit);
				pSkeleton->fMovementBank = pSkeleton->fMovementBankOld + fBankDiff;
			}
		}

		//set the stance word
		if (bBlendBankIn) {
			if (fBank > RAD(0.5f)) {
				dynSkeletonSwapStanceWordsInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceBankRight, pcStanceBankLeft);
			} else if (fBank < -RAD(0.5f)) {
				dynSkeletonSwapStanceWordsInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceBankLeft, pcStanceBankRight);
			} else {
				dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceBankLeft);
				dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceBankRight);
			}
		} else {
			dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceBankLeft);
			dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceBankRight);
		}

		//Blend banking
		if (!gConf.bUseMovementGraphs && eaSize(&pSkeleton->movement.eaBlocks) == 0 ||
			 gConf.bUseMovementGraphs && eaSize(&pSkeleton->eaAGUpdater) < 1)
		{
			pSkeleton->fMovementBankBlendFactor = 0.f;
		} else if (bBlendBankIn) {
			pSkeleton->fMovementBankBlendFactor += 4.0f*fDeltaTime;
			MIN1(pSkeleton->fMovementBankBlendFactor, 1.0f);
		} else {
			pSkeleton->fMovementBankBlendFactor -= 4.0f*fDeltaTime;
			MAX1(pSkeleton->fMovementBankBlendFactor, 0.0f);
		}
	}

	if (bPointToTarget)
	{
		F32  fFacingPitch   = 0.0f;
		F32  fMovementYawFS = 0.0f;
		bool b1DCritter = false;
		
		//compute the pitch
		if (pCurrentGraph && pCurrentGraph->bPitchToTarget)
		{
			Vec3 vToTargetFS;
			quatRotateVec3Inline(pSkeleton->qFaceSpaceInv, pSkeleton->vToTargetWS, vToTargetFS);
			fFacingPitch = getVec3Pitch(vToTargetFS);
			fFacingPitch = CLAMP(fFacingPitch, -QUARTERPI, QUARTERPI);
		}

		// Update the current facing pitch of the skeleton
		{
			const F32 fPitchRate = 1.5f;
			F32 fPitchDiff = fFacingPitch - pSkeleton->fFacingPitch;
			if (fPitchDiff > PI)
				fPitchDiff -= TWOPI;
			else if (fPitchDiff < -PI)
				fPitchDiff += TWOPI;
			{
				F32 fPitchChange = SIGN(fPitchDiff) * fPitchRate * fDeltaTime;
				if (fabsf(fPitchChange) > fabsf(fPitchDiff))
					pSkeleton->fFacingPitch = fFacingPitch;
				else
					pSkeleton->fFacingPitch += fPitchChange;
			}
		}

		//compute the yaw in face space
		{
			fMovementYawFS = pSkeleton->fMovementAngle;

			//draw lines pointing in the movement directions
			if (dynDebugState.bDrawTorsoPointing) {
				Vec3 ptB;

				scaleAddVec3(pSkeleton->vMovementDir, 5, xLocationWS.vPos, ptB);
				wlDrawLine3D_2(xLocationWS.vPos, 0xFF00FFFF, ptB, 0xFF00FFFF);

				scaleAddVec3(pSkeleton->vOldMovementDir, 5, xLocationWS.vPos, ptB);
				wlDrawLine3D_2(xLocationWS.vPos, 0xFF008888, ptB, 0xFF008888);
			}
		}

		//adjust the amount of yaw being applied based on the skeleton's activity (moving, standing, time passed)
		{
			bool bPlanarMotion = pSkeleton->fCurrentSpeedXZ > 0.0f;
			bool bVerticalMotion = pSkeleton->fCurrentSpeedY > 0.00001f;
			const F32 fMaxTwistTime = 4.0f;
			F32 fMoveYawRate = 4.0f;
			const F32 fIdleYawRate = 1.5f;

			pSkeleton->eTurnState = ETurnState_None;

			if (pSkeleton->bMovementDirChanged || bPlanarMotion || bVerticalMotion)
			{
				//case when the character is moving
				{
					F32 fYawDiff, fYawChange;

					if (!dynDebugState.bDisableTorsoPointingFix)
					{
						if (!bPlanarMotion					&&
							!bVerticalMotion				&&
							pSkeleton->bIsDragonTurn		&&
							pSkeleton->bPreventRunAndGunFootShuffle)
						{
							F32 fTargetDot = dotVec3(pSkeleton->vMovementDir, pSkeleton->mFaceSpace[2]);
							F32 fTargetSign = SIGN(dotVec3(pSkeleton->vMovementDir, pSkeleton->mFaceSpace[0]));
							fMovementYawFS = fTargetSign*acos(MINMAX(fTargetDot,-1.f,1.f));
						}
						else if (uiNumMovementDirections == 1 &&
								!pSkeleton->bPlayer &&
								(	!gConf.bUseMovementGraphs &&
									!dynSkeletonMovementIsStopped(&pSkeleton->movement))
								||
								(	gConf.bUseMovementGraphs &&
									eaSize(&pSkeleton->eaAGUpdater) > 1 &&
									dynAnimGraphUpdaterMovementIsStopped(pSkeleton->eaAGUpdater[1])))
						{
							//the most important thing here is to have the critter's feet point along the movement direct
							F32 fTargetDot = dotVec3(pSkeleton->vMovementDir, pSkeleton->mFaceSpace[2]);
							F32 fTargetSign = SIGN(dotVec3(pSkeleton->vMovementDir, pSkeleton->mFaceSpace[0]));
							fMovementYawFS = fTargetSign*acos(MINMAX(fTargetDot,-1.f,1.f));
							b1DCritter = true;
						}
						else if(uiNumMovementDirections == 1 ||
								(	!gConf.bUseMovementGraphs &&
									dynSkeletonMovementIsStopped(&pSkeleton->movement))
								||
								(	gConf.bUseMovementGraphs &&
									eaSize(&pSkeleton->eaAGUpdater) > 1 &&
									dynAnimGraphUpdaterMovementIsStopped(pSkeleton->eaAGUpdater[1])))
						{
							F32 fTargetCrossY =	pSkeleton->vToTargetWS[2] * pSkeleton->vOldToTargetWS[0] -
												pSkeleton->vToTargetWS[0] * pSkeleton->vOldToTargetWS[2];
							F32 fTargetDot = dotVec3(pSkeleton->vToTargetWS, pSkeleton->vOldToTargetWS);
							fMovementYawFS = SIGN(fTargetCrossY)*acosf(MINMAX(fTargetDot,-1.f,1.f));
						}
						else if (pSkeleton->bPreventRunAndGunFootShuffle)
						{
							//pretty sure we can get rid of this
							F32 fTargetDot = dotVec3(pSkeleton->vMovementDir, pSkeleton->mFaceSpace[2]);
							F32 fTargetSign = SIGN(dotVec3(pSkeleton->vMovementDir, pSkeleton->mFaceSpace[0]));
							fMovementYawFS = fTargetSign*acos(MINMAX(fTargetDot,-1.f,1.f));
						}
						else
						{
							F32 fTargetDot = dotVec3(pSkeleton->vMovementDir, pSkeleton->mFaceSpace[2]);
							F32 fTargetSign = SIGN(dotVec3(pSkeleton->vMovementDir, pSkeleton->mFaceSpace[0]));
							if (!gConf.bUseMovementGraphs) fMovementYawFS = -1.f*dynSkeletonMovementCalcBlockYaw(&pSkeleton->movement,      &fMoveYawRate, &pSkeleton->fMovementYawStopped) + fTargetSign*acosf(MINMAX(fTargetDot,-1.f,1.f));
							else                           fMovementYawFS = -1.f*dynAnimGraphUpdaterCalcBlockYaw(pSkeleton->eaAGUpdater[1], &fMoveYawRate, &pSkeleton->fMovementYawStopped) + fTargetSign*acosf(MINMAX(fTargetDot,-1.f,1.f));
						}
						pSkeleton->iYawMovementState = uiNumMovementDirections;
					}
					else
					{
						//bound the movement yaw by +- PI/4
						F32 fRound = ((F32)round(fMovementYawFS / QUARTERPI)) * QUARTERPI;
						fMovementYawFS -= fRound;
						pSkeleton->iYawMovementState = 1;
					}

					//determine how much yaw is required to realign the skeleton in this new direction
					fYawDiff = fMovementYawFS - pSkeleton->fMovementYawFS;
					if (fYawDiff > PI)
						fYawDiff -= TWOPI;
					else if (fYawDiff < -PI)
						fYawDiff += TWOPI;

					//ease the skeleton towards the new yaw 
					fYawChange = SIGN(fYawDiff) * fMoveYawRate * fDeltaTime;
					if (fabsf(fYawChange) > fabsf(fYawDiff))
						pSkeleton->fMovementYawFS = fMovementYawFS;
					else
						pSkeleton->fMovementYawFS += fYawChange;
				}

				//store some variables for later use
				pSkeleton->fYawInterp = -1.0f;
				pSkeleton->fTimeIdle = 0.0f;
				pSkeleton->fMovementYawOffsetFS = pSkeleton->fMovementYawOffsetFSOld = 0.0f;
			}
			else
			{
				//case when the character is not moving

				F32 fAdjustedYaw;
				bool bAdjustFootStance;
				bool bStationaryGaze;

				//if the character was just moving
				if (pSkeleton->iYawMovementState)
				{
					//remember the yaw heading when movement ended
					pSkeleton->fMovementYawOffsetFS = fMovementYawFS - pSkeleton->fMovementYawFS;
					pSkeleton->fMovementYawOffsetFSAccum = 0.0f;
					pSkeleton->iYawMovementState = 0;
					pSkeleton->fYawInterp = -1.0f;
				}

				//determine the yaw to ignore from buggy / lagged movement direction
				{
					F32 fMovementCrossY =	pSkeleton->vMovementDir[2] * pSkeleton->vOldMovementDir[0] -
											pSkeleton->vMovementDir[0] * pSkeleton->vOldMovementDir[2];
					F32 fMovementDot = dotVec3(pSkeleton->vMovementDir, pSkeleton->vOldMovementDir);
					F32 fIgnoreYaw = SIGN(fMovementCrossY)*acosf(MINMAX(fMovementDot, -1.f, 1.f));
					
					if (fabsf(fIgnoreYaw) >= 0.001f)
						pSkeleton->fMovementYawOffsetFS -= fIgnoreYaw;
				}

				//compute the difference in yaw from the last stable foot stance
				fAdjustedYaw = fMovementYawFS - pSkeleton->fMovementYawOffsetFS;
				if (fabsf(fAdjustedYaw) > PI) {
					if (fMovementYawFS < 0)
						fMovementYawFS += TWOPI;
					else
						fMovementYawFS -= TWOPI;
					fAdjustedYaw = fMovementYawFS - pSkeleton->fMovementYawOffsetFS;
				}

				//check if the foot stance is to far away
				if (pSkeleton->bPreventRunAndGunFootShuffle) {
					bAdjustFootStance = fabsf(fAdjustedYaw) > RAD(0.5*pSkeleton->eaRunAndGunBones[0]->fLimitAngle);
				} else {
					//odd fraction taken from old hard codes
					//55.0 deg was the max twist amount, and 35.0 deg was the max twist before the feet would shuffle
					//now the max twist is based on data, with the same ratio used to determine when the feet will shuffle
					bAdjustFootStance = fabsf(fAdjustedYaw) > RAD(0.5*(35.0/55.0)*pSkeleton->eaRunAndGunBones[0]->fLimitAngle);
				}
				bStationaryGaze = fabsf(pSkeleton->fMovementAngle - pSkeleton->fMovementAngleOld) < 0.01f;

				if (pSkeleton->fYawInterp < 0.0f)
				{
					if (bStationaryGaze) {
						//stationary gaze
						pSkeleton->fTimeIdle += fDeltaTime * pSkeleton->fFrozenTimeScale;
						pSkeleton->fMovementYawOffsetFSAccum = 0.0f;
						dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceTurnLeft);
						dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceTurnRight);
						if (!pSkeleton->bPreventRunAndGunFootShuffle &&
							(	bAdjustFootStance ||
								pSkeleton->fTimeIdle > fMaxTwistTime))
						{
							//start to look for a new stable foot stance
							pSkeleton->fYawInterp = 0.0f;
							pSkeleton->fMovementYawOffsetFSOld = pSkeleton->fMovementYawOffsetFS;
							pSkeleton->fTimeIdle = 0.0f;
						}
					} else {
						//shifting gaze
						pSkeleton->fTimeIdle = 0.0f;
						if (bAdjustFootStance)
						{
							F32 fTemp = pSkeleton->fMovementYawOffsetFS;

							//adjust the feet to keep up
							if (pSkeleton->bPreventRunAndGunFootShuffle) {
								pSkeleton->fMovementYawOffsetFS = fMovementYawFS - SIGN(fAdjustedYaw)*RAD(0.5*pSkeleton->eaRunAndGunBones[0]->fLimitAngle);
							} else {
								//odd fraction taken from old hard codes
								//55.0 deg was the max twist amount, and 35.0 deg was the max twist before the feet would shuffle
								//now the max twist is based on data, with the same ratio used to determine when the feet shuffle
								pSkeleton->fMovementYawOffsetFS = fMovementYawFS - SIGN(fAdjustedYaw)*RAD(0.5*(35.0/55.0)*pSkeleton->eaRunAndGunBones[0]->fLimitAngle);
							}
							
							//set the turning state
							pSkeleton->fMovementYawOffsetFSAccum += pSkeleton->fMovementYawOffsetFS - fTemp;
							if (pSkeleton->fMovementYawOffsetFSAccum > RAD(5.0)) { //must be more than a 5 degree turn
								dynSkeletonSwapStanceWordsInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceTurnLeft, pcStanceTurnRight);
								pSkeleton->eTurnState = ETurnState_Left;
							}
							else if (pSkeleton->fMovementYawOffsetFSAccum < RAD(-5.0)) { //must be more than a 5 degree turn
								dynSkeletonSwapStanceWordsInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceTurnRight, pcStanceTurnLeft);
								pSkeleton->eTurnState = ETurnState_Right;
							}
						} else {
							pSkeleton->fMovementYawOffsetFSAccum = 0.0f;
							dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceTurnLeft);
							dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceTurnRight);
						}
					}					
				}

				if (pSkeleton->fYawInterp >= 0.0f)
				{
					//case to find a new stable foot stance
					pSkeleton->fYawInterp += fDeltaTime * fIdleYawRate * pSkeleton->fFrozenTimeScale;
					if (pSkeleton->fYawInterp > 1.0f)
					{
						//found a new stable foot stance
						pSkeleton->fMovementYawOffsetFS = fMovementYawFS;
						pSkeleton->fYawInterp = -1.0f;
						dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceTurnLeft);
						dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceTurnRight);
					}
					else
					{
						//shuffle the feet

						F32 fInterpValue = pSkeleton->fYawInterp;

						F32 fYawDiff = fMovementYawFS - pSkeleton->fMovementYawOffsetFSOld;
						if (fYawDiff > PI)
							fYawDiff -= TWOPI;
						else if (fYawDiff < -PI)
							fYawDiff += TWOPI;

						if (fInterpValue < 0.5f)
						{
							F32 fTwiceInterp = fInterpValue * 2.0f;
							fInterpValue = 0.5f * CUBE(fTwiceInterp);
						}
						else
						{
							F32 fTwiceOneMinusInput = 2.0f * (1.0f - fInterpValue);
							fInterpValue = 1.0f - (0.5f * CUBE(fTwiceOneMinusInput));
						}

						if (fYawDiff > RAD(5.0)) { //must be more than a 5 degree turn
							dynSkeletonSwapStanceWordsInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceTurnLeft, pcStanceTurnRight);
							pSkeleton->eTurnState = ETurnState_Left;
						}
						else if (fYawDiff < RAD(-5.0)) { //must be more than a 5 degree turn
							dynSkeletonSwapStanceWordsInSet(pSkeleton, DS_STANCE_SET_BASE, pcStanceTurnRight, pcStanceTurnLeft);
							pSkeleton->eTurnState = ETurnState_Right;
						}

						pSkeleton->fMovementYawFS = fMovementYawFS - (pSkeleton->fMovementYawOffsetFSOld + interpF32(fInterpValue, 0.0f, fYawDiff));

						if (!bStationaryGaze)
						{
							pSkeleton->fYawInterp = -1.0f;
							pSkeleton->fMovementYawOffsetFS = (pSkeleton->fMovementYawOffsetFSOld + interpF32(fInterpValue, 0.0f, fYawDiff));
						}
					}
				}
				else
				{
					//case to leave feet planted
					pSkeleton->fMovementYawFS = fAdjustedYaw;
				}
			}
		}

		//keep the yaw bounded by +- PI
		if (pSkeleton->fMovementYawFS > PI)
			pSkeleton->fMovementYawFS -= TWOPI;
		else if (pSkeleton->fMovementYawFS < -PI)
			pSkeleton->fMovementYawFS += TWOPI;

		if (pSkeleton->fMovementYawOffsetFS > PI)
			pSkeleton->fMovementYawOffsetFS -= TWOPI;
		else if (pSkeleton->fMovementYawOffsetFS < -PI)
			pSkeleton->fMovementYawOffsetFS += TWOPI;

		// Prevent max twist from ever getting above the animator specified value (was 55 degrees when hard coded)
		if (!b1DCritter &&
			fabsf(pSkeleton->fMovementYawFS) > RAD(0.5*pSkeleton->eaRunAndGunBones[0]->fLimitAngle))
		{
			pSkeleton->fMovementYawFS = SIGN(pSkeleton->fMovementYawFS) * RAD(0.5*pSkeleton->eaRunAndGunBones[0]->fLimitAngle);
		}

		// Blend in/out based on animation disables
		if (dynDebugState.bDisableTorsoPointing
			||
			(	!gConf.bUseMovementGraphs &&
				eaSize(&pSkeleton->movement.eaBlocks) == 0)
			||
			(	gConf.bUseMovementGraphs &&
				eaSize(&pSkeleton->eaAGUpdater) < 1))
		{
			pSkeleton->fTorsoPointingBlendFactor = 0.f;
		}
		else
		{
			F32 fGraphTimer = 0.f;
			F32 fMoveTimer  = 0.f;

			if (pSkeleton->fMovementSystemOverrideFactor < 1.f)
			{
				fGraphTimer = SAFE_MEMBER(pCurrentGraph, fDisableTorsoPointingTimeout);

				if (pCurrentGraph && fGraphTimer <= 0.f)
				{
					const DynAnimGraphNode *pGraphNode = pSkeleton->eaAGUpdater[0]->nodes[0].pGraphNode;
					fGraphTimer = SAFE_MEMBER(pGraphNode, fDisableTorsoPointingTimeout);

					if (pGraphNode && fGraphTimer <= 0.f)
					{
						const DynMoveSeq *pGraphMoveSeq = pSkeleton->eaAGUpdater[0]->nodes[0].pMoveSeq;
						fGraphTimer = SAFE_MEMBER(pGraphMoveSeq, fDisableTorsoPointingTimeout);
					}
				}
			}
			
			if (!gConf.bUseMovementGraphs)
			{
				if (pSkeleton->fOverrideAllBlendFactor < 1.f
					&&
					(	pSkeleton->fMovementSystemOverrideFactor > 0.f ||
						(	pSkeleton->bMovementBlending &&
							pSkeleton->fLowerBodyBlendFactor > 0.f))
					&&
					eaSize(&pSkeleton->movement.eaBlocks))
				{
					if (pSkeleton->movement.eaBlocks[0]->inTransition) {
						fMoveTimer = MAX(0.01, SAFE_MEMBER(pSkeleton->movement.eaBlocks[0]->pMoveSeqTransition, fDisableTorsoPointingTimeout));
					} else {
						fMoveTimer = SAFE_MEMBER(pSkeleton->movement.eaBlocks[0]->pMoveSeqCycle, fDisableTorsoPointingTimeout);
					}
				}
			}
			else
			{
				if (pSkeleton->fOverrideAllBlendFactor < 1.f
					&&
					(	pSkeleton->fMovementSystemOverrideFactor > 0.f ||
						(	pSkeleton->bMovementBlending &&
							pSkeleton->fLowerBodyBlendFactor > 0.f))
					&&
					eaSize(&pSkeleton->eaAGUpdater) > 1)
				{
					fMoveTimer = SAFE_MEMBER(pSkeleton->eaAGUpdater[1]->pCurrentGraph, fDisableTorsoPointingTimeout);

					if (pCurrentGraph && fGraphTimer <= 0.f)
					{
						const DynAnimGraphNode *pGraphNode = pSkeleton->eaAGUpdater[1]->nodes[0].pGraphNode;
						fMoveTimer = SAFE_MEMBER(pGraphNode, fDisableTorsoPointingTimeout);

						if (pGraphNode && fGraphTimer <= 0.f)
						{
							const DynMoveSeq *pGraphMoveSeq = pSkeleton->eaAGUpdater[1]->nodes[0].pMoveSeq;
							fMoveTimer = SAFE_MEMBER(pGraphMoveSeq, fDisableTorsoPointingTimeout);
						}
					}
				}
			}
			
			if (fGraphTimer > 0.f && fMoveTimer  > 0.f)
			{
				pSkeleton->fTorsoPointingBlendFactor -= fDeltaTime / MIN(fGraphTimer,fMoveTimer);
				MAX1(pSkeleton->fTorsoPointingBlendFactor, 0.0f);
			}
			else if (fGraphTimer > 0.f)
			{
				pSkeleton->fTorsoPointingBlendFactor -= fDeltaTime / fGraphTimer;
				MAX1(pSkeleton->fTorsoPointingBlendFactor, 0.0f);
			}
			else if (fMoveTimer > 0.f)
			{
				pSkeleton->fTorsoPointingBlendFactor -= fDeltaTime / fMoveTimer;
				MAX1(pSkeleton->fTorsoPointingBlendFactor, 0.0f);
			}
			else
			{
				pSkeleton->fTorsoPointingBlendFactor += 4.0f * fDeltaTime;
				MIN1(pSkeleton->fTorsoPointingBlendFactor, 1.0f);
			}
		}
		
		if (nearSameF32(pSkeleton->fTorsoPointingBlendFactor,0.f)) {
			pSkeleton->fMovementYawOffsetFS = fMovementYawFS;
			pSkeleton->fMovementYawOffsetFSOld = fMovementYawFS;
			pSkeleton->fMovementYawOffsetFSAccum = 0.f;
			pSkeleton->fYawInterp = -1.f;
			pSkeleton->fTimeIdle = 0.f;
		}
		pSkeleton->fMovementYawFS *= pSkeleton->fTorsoPointingBlendFactor;

		//Blend run'n'gun multijoint
		if (!pSkeleton->bPreventRunAndGunFootShuffle &&
			pSkeleton->iYawMovementState == 8)
		{
			pSkeleton->fRunAndGunMultiJointBlend -= 4.0f*fDeltaTime;
			MAX1(pSkeleton->fRunAndGunMultiJointBlend, 0.0f);
		} else {
			pSkeleton->fRunAndGunMultiJointBlend += 4.0f*fDeltaTime;
			MIN1(pSkeleton->fRunAndGunMultiJointBlend, 1.0f);
		}

		// Now, determine the root transform from the movement yaw and face space
		// and the 'upper quat' in that space
		{
			Quat qMovementYawFS, qMovementBank, qRootTemp, qRootWS;
			F32 fAccumYaw = 0.0, fDesiredYaw;

			if (!dynDebugState.bDisableAutoBanking &&
				pSkeleton->uiBankingOverrideStanceCount == 0)
			{
				//add in the banking
				rollQuat(pSkeleton->fMovementBankBlendFactor*pSkeleton->fMovementBank, qMovementBank);
				quatMultiply(qMovementBank, pSkeleton->qFaceSpace, qRootTemp);
			} else {
				//skip any banking
				copyQuat(pSkeleton->qFaceSpace, qRootTemp);
			}

			//take the movement yaw off of the face space orientation
			yawQuat(-pSkeleton->fMovementYawFS, qMovementYawFS);
			quatMultiply(qRootTemp, qMovementYawFS, qRootWS);

			//apply the modified orientation to the character's root node
			dynNodeSetRotInline(pSkeleton->pRoot->pParent, qRootWS);
			pSkeleton->pRoot->pParent->uiDirtyBits = 1;

			//determine the pitch quaternion
			pitchQuat(pSkeleton->fFacingPitch, pSkeleton->qTorso);

			//save the yaw to a special quaternion for later use
			FOR_EACH_IN_EARRAY(pSkeleton->eaRunAndGunBones, DynSkeletonRunAndGunBone, pRGBone)
			{
				if (ipRGBoneIndex == 0)
				{
					if (b1DCritter) {
						if (fabsf(pSkeleton->fMovementYawFS) < fabsf(RAD(0.5f*pRGBone->fLimitAngle))) {
							fDesiredYaw = pSkeleton->fMovementYawFS;
						} else {
							fDesiredYaw = SIGN(pSkeleton->fMovementYawFS)*RAD(0.5f*pRGBone->fLimitAngle);
						}
					} else {
						fDesiredYaw = pSkeleton->fMovementYawFS;
					}
					yawQuat(fDesiredYaw - fAccumYaw, pRGBone->qPostRoot);
					fAccumYaw = fDesiredYaw;
				}
				else if (fabsf(pSkeleton->fMovementYawFS) > RAD(0.5*pRGBone->fEnableAngle)*pSkeleton->fRunAndGunMultiJointBlend)
				{
					if (b1DCritter) {
						if (fabsf(pSkeleton->fMovementYawFS) < fabsf(RAD(0.5f*pRGBone->fLimitAngle))) {
							fDesiredYaw = pSkeleton->fMovementYawFS;
						} else {
							fDesiredYaw = SIGN(pSkeleton->fMovementYawFS)*RAD(0.5f*pRGBone->fLimitAngle);
						}
					} else {
						fDesiredYaw = (pSkeleton->fMovementYawFS - SIGN(pSkeleton->fMovementYawFS)*RAD(0.5*pRGBone->fEnableAngle)*pSkeleton->fRunAndGunMultiJointBlend);
					}
					yawQuat(fDesiredYaw - fAccumYaw, pRGBone->qPostRoot);
					fAccumYaw = fDesiredYaw;
				}
				else
				{
					unitQuat(pRGBone->qPostRoot);
				}
			}
			FOR_EACH_END;

			if (dynDebugState.bDrawTorsoPointing)
			{
				Vec3 vLine[2];
				Vec3 vRootForward;
				copyVec3(xLocationWS.vPos, vLine[0]);
				vLine[0][1] += 0.02f;

				//new root orientation
				quatRotateVec3Inline(qRootWS, forwardvec, vRootForward);
				scaleAddVec3(vRootForward, 10.0f, vLine[0], vLine[1]);
				wlDrawLine3D_2(vLine[0], 0xFFFFFFFF, vLine[1], 0xFFFFFFFF);
			}
		}
	}
	else //torso pointing is not enabled
	{
		if (!dynDebugState.bDisableAutoBanking &&
			pSkeleton->uiBankingOverrideStanceCount == 0)
		{
			//add in the banking
			Quat qMovementBank, qRootWS;
			DynTransform xInitForm;
			dynNodeGetLocalTransformInline(pSkeleton->pRoot->pParent, &xInitForm);
			rollQuat(pSkeleton->fMovementBankBlendFactor*pSkeleton->fMovementBank, qMovementBank);
			quatMultiply(qMovementBank, xInitForm.qRot, qRootWS);
			dynNodeSetRotInline(pSkeleton->pRoot->pParent, qRootWS);
			pSkeleton->pRoot->pParent->uiDirtyBits = 1;
		}
	}

	//update the old direction and bank values
	copyVec3(pSkeleton->vMovementDir, pSkeleton->vOldMovementDir);
	pSkeleton->fMovementBankOld = pSkeleton->fMovementBank;
}

bool dynSkeletonMergeBitStream( DynBitField* pBitField, DynSkeleton* pSkeleton ) 
{
	bool bFlashed = false;
	dynBitFieldSetAllFromBitField(pBitField, &pSkeleton->costumeBits);

	dynBitFieldSetAllFromBitField(pBitField, &pSkeleton->talkingBits);

	if (pSkeleton->actionFlashBits.uiNumBits > 0)
		bFlashed = true;
	dynBitFieldSetAllFromBitField(pBitField, &pSkeleton->actionFlashBits);
	dynBitFieldClear(&pSkeleton->actionFlashBits);

	FOR_EACH_IN_EARRAY(pSkeleton->eaBitFieldFeeds, DynBitFieldGroup, pBFGroup)
	{
		dynBitFieldSetAllFromBitField(pBitField, &pBFGroup->toggleBits);
		if (pBFGroup->flashBits.uiNumBits > 0)
			bFlashed = true;
		dynBitFieldSetAllFromBitField(pBitField, &pBFGroup->flashBits);
		dynBitFieldClear(&pBFGroup->flashBits);
	}
	FOR_EACH_END;

	if (pSkeleton->attachmentBit)
		dynBitFieldBitSet(pBitField, pSkeleton->attachmentBit);
	return bFlashed;
}

static F32 fLowerBlendSpeed = 3.0f;
AUTO_CMD_FLOAT(fLowerBlendSpeed, dynMovementLowerBlendSpeedOld);

#define SAFE_ACT_ON_REF_MEMBER(r, m, a) GET_REF(r)?a(GET_REF(r)->m):NULL;

static bool dynSkeletonUpdatePreTransforms(DynSkeleton* pSkeleton, F32 fDeltaTime, DynBitField* pParentBitFieldStream, U32 uiParentBitFieldNum) 
{
	bool bFlying = false;
	const U32 MAX_BITFIELDS_IN_STREAM = ARRAY_SIZE(pSkeleton->aBitFieldStream);
	U32 uiNumBitFields = 0;
	DynBitField *const aBitFieldStream = pSkeleton->aBitFieldStream;

	assert(pSkeleton->pDrawSkel);

	if (dynDebugState.costumeShowSkeletonFiles &&
		pSkeleton->pGenesisSkeleton == pSkeleton &&
		pSkeleton->getAudioDebugInfoFunc)
	{
		pSkeleton->getAudioDebugInfoFunc(pSkeleton);
	}

	pSkeleton->uiFrame = wl_state.frame_count;
	dynSkeletonUpdateRoot(pSkeleton, fDeltaTime);

	//return mount points to their original position
	if (!pSkeleton->bSnapshot) {
		FOR_EACH_IN_EARRAY(pSkeleton->eaMatchBaseSkelAnimJoints, DynJointBlend, pMatchJoint)
		{
			DynNode *pBone = dynSkeletonFindNodeNonConst(pSkeleton, pMatchJoint->pcName);
			if (SAFE_MEMBER(pBone,uiLocked)) {
				dynNodeSetPos(pBone, pMatchJoint->vOriginalPos);
			}
		}
		FOR_EACH_END;
	}

	PERFINFO_AUTO_START("Update Sequencers", 1);
	if (!dynDebugState.bNoAnimation)
	{
		DynAnimOverrideList* pOverrideList = NULL;
		FOR_EACH_IN_EARRAY(pSkeleton->eaSqr, DynSequencer, pSqr)
			dynSequencerFlushLog(pSqr);
		FOR_EACH_END;

		// Gather up override info
		{
			WLCostume* pCostume = GET_REF(pSkeleton->hCostume);
			if (pCostume)
			{
				SkelInfo* pSkelInfo = GET_REF(pCostume->hSkelInfo);
				if (pSkelInfo)
				{
					SkelBlendInfo* pBlendInfo = GET_REF(pSkelInfo->hBlendInfo);
					if (pBlendInfo)
					{
						pOverrideList = GET_REF(pBlendInfo->hOverrideList);
					}
				}
			}
		}

		if(pSkeleton->preUpdateFunc)
		{
			bool bMoreUpdates = true;
			int count = 0;


			// Clear current override updater's active status
			FOR_EACH_IN_EARRAY(pSkeleton->eaSqr, DynSequencer, pSqr)
			{
				dynSeqClearOverrides(pSqr);
			}
			FOR_EACH_END;

			if (pSkeleton->ragdollState.bRagdollOn)
			{
				// make rotations in local space
				U32 uiPart;
				for (uiPart=0; uiPart<pSkeleton->ragdollState.uiNumParts; ++uiPart)
				{
					DynRagdollPartState* pPart = &pSkeleton->ragdollState.aParts[uiPart];
					if (pPart->pcParentBoneName)
					{
						U32 uiPart2;
						for (uiPart2=0; uiPart2<pSkeleton->ragdollState.uiNumParts; ++uiPart2)
						{
							DynRagdollPartState* pPart2 = &pSkeleton->ragdollState.aParts[uiPart2];
							if (pPart2->pcBoneName == pPart->pcParentBoneName)
							{
								// Found parent.
								Quat qParentInv;
								quatInverse(pPart2->qWorldSpace, qParentInv);
								quatMultiply(pPart->qWorldSpace, qParentInv, pPart->qLocalSpace);
							}
						}
					}
				}
			}
			PERFINFO_AUTO_START("PreUpdateFunc", 1);
			{
				DynSkeletonPreUpdateParams params = {0};
				params.skeleton = pSkeleton;
				params.userData = pSkeleton->preUpdateData;
				params.targetBitField = &pSkeleton->entityBits;

				if (pSkeleton->entityBitsOverride.uiNumBits > 0) // Ignore the entity bits				
				{
					DynBitField* pNewBitField = &aBitFieldStream[uiNumBitFields++];
					while (bMoreUpdates)
					{
						params.callCountOnThisFrame = count++;
						bMoreUpdates = pSkeleton->preUpdateFunc(&params);
					}
					dynBitFieldCopy(&pSkeleton->entityBitsOverride, pNewBitField);
					if (pSkeleton->ragdollState.bRagdollOn)
					{
						DynBit ragdollBit = dynBitFromName("RAGDOLL");
						dynBitFieldBitSet(pNewBitField, ragdollBit);
					}
				}
				else // Respect the entity bits
				{
					while (bMoreUpdates && uiNumBitFields < MAX_BITFIELDS_IN_STREAM)
					{
						DynBitField* pNewBitField = &aBitFieldStream[uiNumBitFields++];
						params.callCountOnThisFrame = count++;
						bMoreUpdates = pSkeleton->preUpdateFunc(&params);

						if (pSkeleton->entityBitsOverride.uiNumBits <= 0)
							dynBitFieldCopy(&pSkeleton->entityBits, pNewBitField);
						if (pSkeleton->ragdollState.bRagdollOn)
						{
							DynBit ragdollBit = dynBitFromName("RAGDOLL");
							dynBitFieldBitSet(pNewBitField, ragdollBit);
						}
					}
				}

			}
			PERFINFO_AUTO_STOP_CHECKED("PreUpdateFunc");
		}

		dynSkeletonCalculateDistanceTraveled(pSkeleton, fDeltaTime);

		// Now we have a 'bit stream'

		// First, if we were passed a bit stream, merge it with the one we have
		if (pParentBitFieldStream)
		{
			int iNumA, iNumB;
			int iIndexA, iIndexB;
			iNumA = uiNumBitFields;
			iNumB = uiParentBitFieldNum;
			while (iNumB > iNumA && uiNumBitFields < MAX_BITFIELDS_IN_STREAM)
			{
				dynBitFieldClear(&aBitFieldStream[uiNumBitFields++]);
				++iNumA;
			}
			for (iIndexA=0,iIndexB=0; iIndexA < iNumA; ++iIndexA)
			{
				dynBitFieldSetAllFromBitField(&aBitFieldStream[iIndexA], &pParentBitFieldStream[iIndexB]);
				if (iIndexB+1 < iNumB)
					++iIndexB;
			}
		}

		// We also need to merge this bit stream with our other sources of bits
		{
			bool bNeedToAddOne = true;
			U32 uiStreamIndex;
			DynBitField prevBitField = {0};
			for (uiStreamIndex=0; uiStreamIndex< uiNumBitFields; ++uiStreamIndex)
			{
				if (uiStreamIndex+1 == uiNumBitFields)
					dynBitFieldCopy(&aBitFieldStream[uiStreamIndex], &prevBitField);
				bNeedToAddOne = dynSkeletonMergeBitStream(&aBitFieldStream[uiStreamIndex], pSkeleton);
			}
			while (bNeedToAddOne && (uiNumBitFields+1) < MAX_BITFIELDS_IN_STREAM)
			{
				// we flashed on the last frame of our stream, so we need to add one more and run it
				DynBitField* pNewBitField = &aBitFieldStream[uiNumBitFields++];
				dynBitFieldCopy(&prevBitField, pNewBitField);
				bNeedToAddOne = dynSkeletonMergeBitStream(pNewBitField, pSkeleton);
			}
		}

		// Merge any two bitfields in a row that are the same
		// Eventually this will not be necessary
		{
			int iIndex = uiNumBitFields - 2;
			while (iIndex >= 0)
			{
				if (dynBitFieldsAreEqual(&aBitFieldStream[iIndex], &aBitFieldStream[iIndex+1]))
				{
					// remove the 2nd one
					U32 uiCopyIndex = iIndex+1;
					while (uiCopyIndex+1 < uiNumBitFields)
					{
						dynBitFieldCopy(&aBitFieldStream[uiCopyIndex+1], &aBitFieldStream[uiCopyIndex]);
						++uiCopyIndex;
					}
					--uiNumBitFields;
				}
				--iIndex;
			}
		}

		if (pSkeleton->bTorsoPointing || pSkeleton->bTorsoDirections)
		{
			U32 uiStreamIndex;
			bool bMovedThisFrame = false;
			bool bDiedThisFrame = false;
			DynBit moveBit = dynBitFromName("MOVE");
			DynBit deathBit = dynBitFromName("DEATH");
			DynBit flightBit = dynBitFromName("FLIGHT");
			const DynAction* pCurrentAction = NULL;
			for (uiStreamIndex=0; uiStreamIndex< uiNumBitFields; ++uiStreamIndex)
			{
				if (dynBitFieldBitTest(&aBitFieldStream[uiStreamIndex], moveBit))
				{
					bMovedThisFrame = true;
				}
				if (dynBitFieldBitTest(&aBitFieldStream[uiStreamIndex], deathBit))
				{
					bDiedThisFrame = true;
				}
				if (dynBitFieldBitTest(&aBitFieldStream[uiStreamIndex], flightBit))
				{
					bFlying = true;
				}
			}

			pSkeleton->bIsDead = bDiedThisFrame;


			// If any of these are true, turn off the lower body sequencer
			{
				bool bOverrideSeqs = false;
				bool bForceLowerBody = false;
				FOR_EACH_IN_EARRAY(pSkeleton->eaSqr, DynSequencer, pSequencer)
				{
					const DynAction* pAction = dynSeqGetCurrentAction(pSequencer);
					if (ipSequencerIndex == 0 && pAction->bOverrideAll) // only the default sequencer can set override seqs
						bOverrideSeqs = true;
					if (pAction->bForceLowerBody) // any sequencer can cause a force lower body to happen
						bForceLowerBody = true;
				}
				FOR_EACH_END;


				if(	bDiedThisFrame
					||
					!bMovedThisFrame &&
					!bFlying &&
					!bForceLowerBody
					||
					pSkeleton->eaSqr &&
					pSkeleton->eaSqr[0] &&
					(pCurrentAction = dynSeqGetCurrentAction(pSkeleton->eaSqr[0])) &&
					pCurrentAction->bOverrideSeqs)
				{
					if (pSkeleton->fLowerBodyBlendFactor > 0.0f)
					{
						pSkeleton->fLowerBodyBlendFactorMutable -= fLowerBlendSpeed * fDeltaTime;
						MAX1(pSkeleton->fLowerBodyBlendFactorMutable, 0.0f);
					}
				}
				else
				{
					if (pSkeleton->fLowerBodyBlendFactor < 1.0f)
					{
						pSkeleton->fLowerBodyBlendFactorMutable += fLowerBlendSpeed * fDeltaTime;
						MIN1(pSkeleton->fLowerBodyBlendFactorMutable, 1.0f);
					}
				}
			}
		}

		{
			U32 uiStreamIndex;
			DynBit actionModeBit = dynBitFromName("TARGETED");
			DynBit flightBit = dynBitFromName("FLIGHT");
			bool bActionModeThisFrame = false;

			pSkeleton->bMovementSyncSet = false;

			for (uiStreamIndex=0; uiStreamIndex<uiNumBitFields; ++uiStreamIndex)
			{
				bool bMoreUpdates = (uiStreamIndex+1) < uiNumBitFields;
				if (pSkeleton == dynDebugState.pDebugSkeleton)
					dynDebugSkeletonUpdateBits(pSkeleton, bMoreUpdates?0.0f:fDeltaTime);

				dynBitFieldSetAllFromBitField(&aBitFieldStream[uiStreamIndex], &pSkeleton->actionFlashBits);
				dynBitFieldClear(&pSkeleton->actionFlashBits);

				if (dynBitFieldBitTest(&aBitFieldStream[uiStreamIndex], actionModeBit))
				{
					bActionModeThisFrame = true;
				}
				if (dynBitFieldBitTest(&aBitFieldStream[uiStreamIndex], flightBit))
				{
					bFlying = true;
				}

				// Do a check for anim overrides
				if (pOverrideList)
				{
					FOR_EACH_IN_EARRAY(pOverrideList->eaAnimOverride, DynAnimOverride, pOverride)
					{
						if (!dynBitFieldCheckForExtraBits(&pOverride->bits,  &aBitFieldStream[uiStreamIndex]))
						{
							// we matched all of the bits in the override bit list, so we have to add it to the sequencer updater list
							FOR_EACH_IN_EARRAY(pSkeleton->eaSqr, DynSequencer, pSqr)
							{
								dynSeqPushOverride(pSqr, pOverride);
							}
							FOR_EACH_END;
						}
					}
					FOR_EACH_END;
				}

				pSkeleton->bHasLastPassIK		= false;
				pSkeleton->bIKBothHands			= false;
				pSkeleton->bRegisterWep			= false;
				pSkeleton->bIKMeleeMode			= false;
				pSkeleton->bEnableIKSliding		= false;
				pSkeleton->bDisableIKLeftWrist	= false;
				pSkeleton->bDisableIKRightArm	= false;
				pSkeleton->pcIKTarget			= NULL;
				pSkeleton->pcIKTargetNodeLeft	= NULL;
				pSkeleton->pcIKTargetNodeRight	= NULL;
				pSkeleton->bOverrideAll			= false;
				pSkeleton->bSnapOverrideAllOld	= false;
				pSkeleton->bForceVisible		= false;
				FOR_EACH_IN_EARRAY(pSkeleton->eaSqr, DynSequencer, pSqr)
					dynSeqUpdate(pSqr, &aBitFieldStream[uiStreamIndex], bMoreUpdates?0.0f:fDeltaTime, pSkeleton, !bMoreUpdates, ipSqrIndex);
				FOR_EACH_END;
			}

			if (!pSkeleton->bMovementSyncSet)
				pSkeleton->fMovementSyncPercentage = 0.0f;

			{
				F32 fOverrideBlendSpeed = 1.0f;
				if ((pSkeleton->bOverrideAll || pSkeleton->bSnapOverrideAllOld) && pSkeleton->iOverrideSeqIndex >= 0)
				{
					if (pSkeleton->bSnapOverrideAllOld)
						pSkeleton->fOverrideAllBlendFactor = 1.0f;
					else {
						pSkeleton->fOverrideAllBlendFactor += fOverrideBlendSpeed * fDeltaTime;
						MIN1(pSkeleton->fOverrideAllBlendFactor, 1.0f);
					}
				}
				else
				{
					pSkeleton->fOverrideAllBlendFactor -= fOverrideBlendSpeed * fDeltaTime;
					MAX1(pSkeleton->fOverrideAllBlendFactor, 0.0f);
				}
			}

			pSkeleton->bActionModeLastFrame = bActionModeThisFrame;
		}
	}

	PERFINFO_AUTO_STOP_START("Update Draw Skeleton LOD", 1);
	//if (pSkeleton->bVisible)
	dynDrawSkeletonUpdateLODLevel(pSkeleton->pDrawSkel, pSkeleton->bBodySock);
	if (pSkeleton->pDrawSkel->bUpdateDrawInfo)
		dynDrawSkeletonUpdateDrawInfo(pSkeleton->pDrawSkel);

	// Check to see if we need to blend in or out of wep registration
	if (pSkeleton->bRegisterWep || pSkeleton->fWepRegisterBlend > 0.0f)
	{
		const F32 fWepRegisterBlendRate = 4.0f;
		if (pSkeleton->bRegisterWep && pSkeleton->uiLODLevel <= worldLibGetLODSettings()->uiIKLODLevel)
		{
			pSkeleton->fWepRegisterBlend += fWepRegisterBlendRate * fDeltaTime;
		}
		else
		{
			pSkeleton->fWepRegisterBlend -= fWepRegisterBlendRate * fDeltaTime;
		}
		pSkeleton->fWepRegisterBlend = CLAMP(pSkeleton->fWepRegisterBlend, 0.0f, 1.0f);
	}

	if (pSkeleton->bIKBothHands || pSkeleton->fIKBothHandsBlend > 0.f)
	{
		const F32 fIKBothHandsBlendRate = 4.f;

		if (pSkeleton->bIKBothHands && pSkeleton->uiLODLevel <= worldLibGetLODSettings()->uiIKLODLevel)
		{
			pSkeleton->fIKBothHandsBlend += fIKBothHandsBlendRate * fDeltaTime;
		}
		else
		{
			pSkeleton->fIKBothHandsBlend -= fIKBothHandsBlendRate * fDeltaTime;
		}
		pSkeleton->fIKBothHandsBlend = CLAMP(pSkeleton->fIKBothHandsBlend, 0.f, 1.f);
	}

	//check to see if we need to blend in or our of base skel anim joint matches
	FOR_EACH_IN_EARRAY(pSkeleton->eaMatchBaseSkelAnimJoints, DynJointBlend, pMatchJoint)
	{
		DynNode *pBone = dynSkeletonFindNodeNonConst(pSkeleton, pMatchJoint->pcName);
		if (pBone && pMatchJoint->bActive && pSkeleton->uiLODLevel <= worldLibGetLODSettings()->uiIKLODLevel)
		{
			if (pMatchJoint->bPlayBlendOutWhileActive) {
				pMatchJoint->fBlend -= pMatchJoint->fBlendOutRate * fDeltaTime;
				pMatchJoint->bActive = false;	
				MAX1(pMatchJoint->fBlend, 0.f); //don't delete here, otherwise it'll recreate itself in the dynSequencer code
			} else {
				pMatchJoint->fBlend += pMatchJoint->fBlendInRate * fDeltaTime;
				pMatchJoint->bActive = false;	
				MIN1(pMatchJoint->fBlend, 1.f);
			}
		}
		else
		{
			pMatchJoint->fBlend -= pMatchJoint->fBlendOutRate * fDeltaTime;
			pMatchJoint->bActive = false;
			if (!pBone || pMatchJoint->fBlend <= 0) {
				eaRemoveFast(&pSkeleton->eaMatchBaseSkelAnimJoints, ipMatchJointIndex);
				SAFE_FREE(pMatchJoint);
			}
		}
	}
	FOR_EACH_END;

	pSkeleton->bFlying = bFlying;
	pSkeleton->uiNumBitFields = uiNumBitFields;

	// Do the actual transform calculation, animation and skinning... all of the heavy lifting
	if (pSkeleton->bVisible || pSkeleton->bForceTransformUpdate)
	{
		pSkeleton->bForceTransformUpdate = 0;

		if (!dynDebugState.bNoAnimation && !pSkeleton->bAnimDisabled) {

			DynFxDrivenScaleModifier *const aScaleModifiers = pSkeleton->aScaleModifiers;
			U32 uiNumScaleModifiers = 0;

			dynSkeletonLockFX(); {

				// Do prep steps first
				FOR_EACH_IN_EARRAY(pSkeleton->pDrawSkel->eaDynFxRefs, DynFxRef, pRef)
				{
					const DynFx* pFx = GET_REF(pRef->hDynFx);
					if ( pFx && pFx->pParticle && pFx->pParticle->pDraw->iEntScaleMode != edesmNone && uiNumScaleModifiers < MAX_SCALE_MODIFIERS)
					{
						DynFxDrivenScaleModifier* pModifier = &aScaleModifiers[uiNumScaleModifiers++];
						pModifier->uiNumBones = 0;
						copyVec3(pFx->pParticle->pDraw->vScale, pModifier->vScale);
						FOR_EACH_IN_EARRAY(pFx->eaEntCostumeParts, const char, pcBoneName)
						{
							pModifier->apcBoneName[pModifier->uiNumBones++] = pcBoneName;
							if (pModifier->uiNumBones >= MAX_SCALE_MODIFIER_BONES)
								break;
						}
						FOR_EACH_END;
					}
				}
				FOR_EACH_END;

			} dynSkeletonUnlockFX();

			pSkeleton->uiNumScaleModifiers = uiNumScaleModifiers;

			//if (pSkeleton->pDrawSkel)
			dynDrawSkeletonAllocSkinningMats(pSkeleton->pDrawSkel);

			PERFINFO_AUTO_STOP_CHECKED("Update Draw Skeleton LOD");
			return 1;
		}
		else
		{
			dynDrawSkeletonBasePose(pSkeleton->pDrawSkel);
		}
	}
	PERFINFO_AUTO_STOP_CHECKED("Update Draw Skeleton LOD");
	return 0;
}

static void dynSkeletonSetPreUpdateStanceWord(DynSkeleton* pSkeleton, const char* pcStance)
{
	dynSkeletonSetStanceWordInSet(pSkeleton, DS_STANCE_SET_PRE_UPDATE, pcStance);
}

static void dynSkeletonClearPreUpdateStanceWord(DynSkeleton* pSkeleton, const char* pcStance)
{
	dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_PRE_UPDATE, pcStance);
}

static void dynSkeletonUpdateStanceTimers(	DynSkeleton *pSkeleton,
											F32 fDeltaTime)
{
	ARRAY_FOREACH_BEGIN(pSkeleton->eaStanceTimers, i);
	{
		int j, s = eafSize(&pSkeleton->eaStanceTimers[i]);
		for (j = 0; j < s; j++)
			pSkeleton->eaStanceTimers[i][j] += fDeltaTime;
	}
	ARRAY_FOREACH_END;

	eaClear(&pSkeleton->eaStancesCleared);
	eafClear(&pSkeleton->eaStanceTimersCleared);
}

static void dynSkeletonUpdateDebugStances(DynSkeleton *pSkeleton)
{
	if (pSkeleton == dynDebugState.pDebugSkeleton)
	{
		dynSkeletonClearNonListedStanceWordsInSet(pSkeleton, DS_STANCE_SET_DEBUG, dynDebugState.eaDebugStances);
		FOR_EACH_IN_EARRAY_FORWARDS(dynDebugState.eaDebugStances, const char, pcStance) {
			dynSkeletonSetStanceWordInSet(pSkeleton, DS_STANCE_SET_DEBUG, pcStance);
		} FOR_EACH_END;
	}
}

static void dynSkeletonUpdateGraphStances(DynSkeleton *pSkeleton)
{
	const char **ppchStances = NULL;

	FOR_EACH_IN_EARRAY_FORWARDS(pSkeleton->eaAGUpdater, DynAnimGraphUpdater, pUpdater) {		
		FOR_EACH_IN_EARRAY_FORWARDS(pUpdater->pCurrentGraph->eaStance, DynAnimGraphStance, pStance) {
			eaPush(&ppchStances, pStance->pcStance);
		} FOR_EACH_END;
	} FOR_EACH_END;

	dynSkeletonClearNonListedStanceWordsInSet(pSkeleton, DS_STANCE_SET_GRAPH, ppchStances);

	FOR_EACH_IN_EARRAY_FORWARDS(ppchStances, const char, pcStance) {
		dynSkeletonSetStanceWordInSet(pSkeleton, DS_STANCE_SET_GRAPH, pcStance);
	} FOR_EACH_END;

	eaDestroy(&ppchStances);
}

static void dynSkeletonDoNothingWithString(DynSkeleton* pSkeleton, const char* pcIgnored){}
static void dynSkeletonDoNothingWithStringId(DynSkeleton* pSkeleton, const char* pcIgnored, U32 uid){}
static void dynSkeletonDoNothingWithF32U32(DynSkeleton *pSkeleton, F32 fIgnored, U32 uiIgnored){}

static void dynSkeletonCallPreUpdateFuncNew(DynSkeleton* pSkeleton)
{
	if(!pSkeleton->preUpdateFunc)
	{
		return;
	}

	if (gConf.bUseMovementGraphs)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pSkeleton->eaAnimChartStacks, DynAnimChartStack, pChartStack) {
			assert(	eaSize(&pChartStack->eaStanceKeys)  == 0 &&
					eaSize(&pChartStack->eaStanceFlags) == 0);
			eaClear(&pChartStack->eaRemovedStanceKeys);
			eaClear(&pChartStack->eaRemovedStanceFlags);
			pChartStack->bStopped = false;
		} FOR_EACH_END;
	}

	PERFINFO_AUTO_START("PreUpdateFunc", 1);
	{
		DynSkeletonPreUpdateParams	params = {0};
		DynSkeletonAnimOverride*	o = eaTail(&pSkeleton->eaAnimOverrides);

		params.skeleton = pSkeleton;
		params.userData = pSkeleton->preUpdateData;

		if(o){
			if(FALSE_THEN_SET(pSkeleton->wasInOverride)){
				dynSkeletonClearStanceWordsInSet(pSkeleton, DS_STANCE_SET_PRE_UPDATE);
			}

			params.func.clearStance = dynSkeletonDoNothingWithString;
			params.func.setStance = dynSkeletonDoNothingWithString;
			params.func.playFlag = dynSkeletonDoNothingWithStringId;
			params.func.playDetailFlag = dynSkeletonDoNothingWithStringId;
			params.func.startGraph = dynSkeletonDoNothingWithStringId;
			params.func.startDetailGraph = dynSkeletonDoNothingWithStringId;
			params.func.setOverrideTime = dynSkeletonDoNothingWithF32U32;
		}else{
			S32 resetPostOverride = 0;

			if(TRUE_THEN_RESET(pSkeleton->wasInOverride)){
				params.doReset = 1;
				pSkeleton->bStartedGraphInPreUpdate = 0;
				resetPostOverride = 1;
			}

			if(TRUE_THEN_RESET(pSkeleton->bRequestResetOnPreUpdate)){
				params.doReset = 1;
			}

			params.func.clearStance = dynSkeletonClearPreUpdateStanceWord;
			params.func.setStance = dynSkeletonSetPreUpdateStanceWord;
			params.func.playFlag = dynSkeletonSetFlag;
			params.func.playDetailFlag = dynSkeletonSetDetailFlag;
			params.func.startGraph = dynSkeletonStartGraph;
			params.func.startDetailGraph = dynSkeletonStartDetailGraph;
			params.func.setOverrideTime = dynSkeletonSetOverrideTime;

			if(	resetPostOverride &&
				pSkeleton->bStartedGraphInPreUpdate)
			{
				dynSkeletonResetToADefaultGraph(pSkeleton, "preupdate override", 0, 1);
			}
		}

		pSkeleton->preUpdateFunc(&params);

		//update the debug and graph stances
		dynSkeletonUpdateDebugStances(pSkeleton);
		dynSkeletonUpdateGraphStances(pSkeleton); // we should really be doing this after changing a graph during the animGraphUpdater

		if(o){
			if(!o->playedKeyword){
				EARRAY_CONST_FOREACH_BEGIN(pSkeleton->eaAnimOverrides, i, isize);
				{
					pSkeleton->eaAnimOverrides[i]->playedKeyword = 0;
				}
				EARRAY_FOREACH_END;

				o->playedKeyword = 1;
				dynSkeletonStartGraph(pSkeleton, o->keyword, 0);
			}
		}
	}
	PERFINFO_AUTO_STOP_CHECKED("PreUpdateFunc");
}

static void dynSkeletonDoTransformsAndSkinning(DynSkeleton* pSkeleton)
{
	DynFxDrivenScaleModifier *const aScaleModifiers = pSkeleton->aScaleModifiers;
	U32 uiNumScaleModifiers = 0;

	dynSkeletonLockFX(); {

		// Do prep steps first
		FOR_EACH_IN_EARRAY(pSkeleton->pDrawSkel->eaDynFxRefs, DynFxRef, pRef)
		{
			const DynFx* pFx = GET_REF(pRef->hDynFx);
			if ( pFx && pFx->pParticle && pFx->pParticle->pDraw->iEntScaleMode != edesmNone && uiNumScaleModifiers < MAX_SCALE_MODIFIERS)
			{
				DynFxDrivenScaleModifier* pModifier = &aScaleModifiers[uiNumScaleModifiers++];
				pModifier->uiNumBones = 0;
				copyVec3(pFx->pParticle->pDraw->vScale, pModifier->vScale);
				FOR_EACH_IN_EARRAY(pFx->eaEntCostumeParts, const char, pcBoneName)
				{
					pModifier->apcBoneName[pModifier->uiNumBones++] = pcBoneName;
					if (pModifier->uiNumBones >= MAX_SCALE_MODIFIER_BONES)
						break;
				}
				FOR_EACH_END;
			}
		}
		FOR_EACH_END;

	} dynSkeletonUnlockFX();

	pSkeleton->uiNumScaleModifiers = uiNumScaleModifiers;

	//if (pSkeleton->pDrawSkel)
	dynDrawSkeletonAllocSkinningMats(pSkeleton->pDrawSkel);
}

static F32 fOverrideAllBlendPerSecond = 3.0f;
AUTO_CMD_FLOAT(fOverrideAllBlendPerSecond, dynOverrideAllBlendPerSecond);

static F32 fMovementBlendOutPerSecond = 3.0f;
AUTO_CMD_FLOAT(fMovementBlendOutPerSecond, dynMovementBlendOutPerSecond);
static F32 fMovementBlendInPerSecond = 3.0f;
AUTO_CMD_FLOAT(fMovementBlendInPerSecond, dynMovementBlendInPerSecond);

static void dynSkeletonUpdateBlends(DynSkeleton* pSkeleton,
									F32 fDeltaTime)
{
	if (pSkeleton->bOverrideAll)
	{
		if (pSkeleton->bSnapOverrideAll) {
			pSkeleton->fOverrideAllBlendFactor = 1.0f;
		} else {
			pSkeleton->fOverrideAllBlendFactor += fOverrideAllBlendPerSecond * fDeltaTime * pSkeleton->fFrozenTimeScale;
			MIN1(pSkeleton->fOverrideAllBlendFactor, 1.0f);
		}
	}
	else
	{
		pSkeleton->fOverrideAllBlendFactor -= fOverrideAllBlendPerSecond * fDeltaTime * pSkeleton->fFrozenTimeScale;
		MAX1(pSkeleton->fOverrideAllBlendFactor, 0.0f);
	}
	
	pSkeleton->fMovementSystemOverrideFactorOldMutable = pSkeleton->fMovementSystemOverrideFactor;

	if (gConf.bUseMovementGraphs)
	{
		DynAnimGraphUpdater* pDefaultUpdater = eaGet(&pSkeleton->eaAGUpdater, 0);
		DynAnimGraphUpdater* pMovementUpdater = eaGet(&pSkeleton->eaAGUpdater, 1);

		if (pDefaultUpdater &&
			(	pDefaultUpdater->bOnDefaultGraph ||
				pDefaultUpdater->bInPostIdle))
		{
			if(	pSkeleton->bOverrideDefaultMove ||
				(	!pMovementUpdater->bOnMovementGraph &&
					(	pMovementUpdater->bOnDefaultGraph ||
						pMovementUpdater->bInPostIdle)))
			{
				if (pSkeleton->bOverrideDefaultMove && pSkeleton->bSnapOverrideDefaultMove) {
					pSkeleton->fMovementSystemOverrideFactorMutable = 0.0f;
				} else {
					pSkeleton->fMovementSystemOverrideFactorMutable -= fMovementBlendOutPerSecond * fDeltaTime * pSkeleton->fFrozenTimeScale;
					MAX1(pSkeleton->fMovementSystemOverrideFactorMutable, 0.0f);
				}
			} else {
				pSkeleton->fMovementSystemOverrideFactorMutable += fMovementBlendInPerSecond * fDeltaTime * pSkeleton->fFrozenTimeScale;
				MIN1(pSkeleton->fMovementSystemOverrideFactorMutable, 1.0f);
			}
		}else{
			pSkeleton->fMovementSystemOverrideFactorMutable = 0.0f;
		}
	}
	else
	{
		DynAnimGraphUpdater* pDefaultUpdater = eaGet(&pSkeleton->eaAGUpdater, 0);
		DynMovementState* m = &pSkeleton->movement;

		if (pDefaultUpdater &&
			(	pDefaultUpdater->bOnDefaultGraph ||
				pDefaultUpdater->bInDefaultPostIdle))
		{
			if(	pSkeleton->bOverrideDefaultMove
				||
				m &&
				!(SAFE_MEMBER(m->pChartStack,interruptingMovementStanceCount))	&&
				!(SAFE_MEMBER(m->pChartStack,directionMovementStanceCount))		&&
				(	eaSize(&m->eaBlocks) == 0 ||
					!m->eaBlocks[0]->inTransition))
			{
				if (pSkeleton->bOverrideDefaultMove && pSkeleton->bSnapOverrideDefaultMove) {
					pSkeleton->fMovementSystemOverrideFactorMutable = 0.0f;
				} else {
					// On default graph, and movement stopped, want to use graph
					pSkeleton->fMovementSystemOverrideFactorMutable -= fMovementBlendOutPerSecond * fDeltaTime * pSkeleton->fFrozenTimeScale;
					MAX1(pSkeleton->fMovementSystemOverrideFactorMutable, 0.0f);
				}
			} else {
				// On default graph, but moving, override it
				pSkeleton->fMovementSystemOverrideFactorMutable += fMovementBlendInPerSecond * fDeltaTime * pSkeleton->fFrozenTimeScale;
				MIN1(pSkeleton->fMovementSystemOverrideFactorMutable, 1.0f);
			}
		}else{
			pSkeleton->fMovementSystemOverrideFactorMutable = 0.0f;
		}
	}
}

static bool dynSkeletonUpdatePreTransformsNew(DynSkeleton* pSkeleton, F32 fDeltaTime, DynBitField* pParentBitFieldStream, U32 uiParentBitFieldNum) 
{
	glrLog(pSkeleton->glr, "[dyn.AGU] %s BEGIN.", __FUNCTION__);

	if (pSkeleton->pAnimExpressionSet)
	{
		const WLCostume				*pCostume		= GET_REF(pSkeleton->hCostume);
		const SkelInfo				*pSkelInfo		= pCostume ? GET_REF(pCostume->hSkelInfo) : NULL;
		const DynAnimExpressionSet	*pAnimExprSet	= pSkelInfo ? GET_REF(pSkelInfo->hExpressionSet) : NULL;
		if (pAnimExprSet != pSkeleton->pAnimExpressionSet) {
			dynSkeletonRecreateNodeExpressions(pSkeleton, pAnimExprSet);
		}
	}

	pSkeleton->uiFrame = wl_state.frame_count;
	pSkeleton->fBankingOverrideTimeActive += fDeltaTime;
	dynSkeletonUpdateStanceTimers(pSkeleton, fDeltaTime);
	dynSkeletonUpdateMovementDirection(pSkeleton, fDeltaTime);
	dynSkeletonCalculateDistanceTraveled(pSkeleton, fDeltaTime);

	pSkeleton->bOverrideAll			= pSkeleton->bSnapOverrideAll			= false;
	pSkeleton->bOverrideMovement	= pSkeleton->bSnapOverrideMovement		= false;
	pSkeleton->bOverrideDefaultMove	= pSkeleton->bSnapOverrideDefaultMove	= false;
	pSkeleton->bForceVisible = false;

	dynSkeletonCallPreUpdateFuncNew(pSkeleton);

	glrLog(	pSkeleton->glr,
			"[dyn.AGU] Blends before: Override All: %.3f,  LowerBody: %.3f, Movement: %.3f",
			pSkeleton->fOverrideAllBlendFactor,
			pSkeleton->fLowerBodyBlendFactor,
			pSkeleton->fMovementSystemOverrideFactor);

	dynSkeletonPlayAnimWordFeeds(pSkeleton);

	if(TRUE_THEN_RESET(pSkeleton->bNotifyChartStackChanged)){
		dynSkeletonUpdateStanceState(pSkeleton);
		FOR_EACH_IN_EARRAY(pSkeleton->eaAGUpdater, DynAnimGraphUpdater, pUpdater)
		{
			dynAnimGraphUpdaterChartStackChanged(pSkeleton, pUpdater);
		}
		FOR_EACH_END;
	}

	FOR_EACH_IN_EARRAY(pSkeleton->eaAGUpdater, DynAnimGraphUpdater, pUpdater)
	{		
		if (!gConf.bUseMovementGraphs || ipUpdaterIndex != 1)
			dynAnimGraphUpdaterUpdate(pUpdater, fDeltaTime, pSkeleton, 0);
	}
	FOR_EACH_END;

	if (!gConf.bUseMovementGraphs)
	{
		if (eaSize(&pSkeleton->movement.eaBlocks) > 0) {
			dynSkeletonMovementUpdate(pSkeleton, fDeltaTime);
		}
	}
	else if (eaSize(&pSkeleton->eaAGUpdater) > 1)
	{
		DynAnimGraphUpdater* pUpdater = pSkeleton->eaAGUpdater[1];
		DynAnimChartStack* pChartStack = pUpdater->pChartStack;

		if (pSkeleton->bMovementBlending)
		{
			if (pSkeleton->bOverrideMovement ||
				(	!pUpdater->bOnMovementGraph &&
					(	pUpdater->bOnDefaultGraph ||
						pUpdater->bInPostIdle)))
			{
				// Blend out of movement.
				if (pSkeleton->bOverrideMovement && pSkeleton->bSnapOverrideMovement) {
						pSkeleton->fLowerBodyBlendFactorMutable = 0.0f;
				} else if(pSkeleton->fLowerBodyBlendFactor > 0.0f){
					pSkeleton->fLowerBodyBlendFactorMutable -= 3.f * fDeltaTime * pSkeleton->fFrozenTimeScale;
					MAX1(pSkeleton->fLowerBodyBlendFactorMutable, 0.0f);
				}
			}
			else
			{
				// Blend into movement.
				if(pSkeleton->fLowerBodyBlendFactor < 1.0f){
					pSkeleton->fLowerBodyBlendFactorMutable += 3.f * fDeltaTime * pSkeleton->fFrozenTimeScale;
					MIN1(pSkeleton->fLowerBodyBlendFactorMutable, 1.0f);
				}
			}
		}

		if (pChartStack)
		{
			bool bStartedGraph = false;

			if (eaSize(&pChartStack->eaStanceKeys))
			{
				eaQSort(pChartStack->eaStanceKeys, dynAnimCompareStanceWordPriority);

				FOR_EACH_IN_EARRAY_FORWARDS(pChartStack->eaStanceKeys, const char, pcStanceKey)
				{
					if (dynAnimGraphUpdaterStartGraph(pSkeleton, pUpdater, pcStanceKey, 0, 0))
					{
						pChartStack->pcPlayingStanceKey = pcStanceKey;
						bStartedGraph = true;

						eaCopy(&pChartStack->eaStanceFlags, &pSkeleton->eaStances[DS_STANCE_SET_PRE_UPDATE]);
						eaPushEArray(&pChartStack->eaStanceFlags, &pSkeleton->eaStances[DS_STANCE_SET_DEBUG]);
						if (TRUE_THEN_RESET(pSkeleton->bLanded))
							eaPush(&pChartStack->eaStanceFlags, pcStanceLanded);

						if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits) {
							dynDebugSkeletonStartGraph(pSkeleton, pUpdater, pcStanceKey, pUpdater->uidCurrentGraph);
						}

						break;
					}
				}
				FOR_EACH_END;

				eaClear(&pChartStack->eaStanceKeys);
			}

			if (!bStartedGraph && pChartStack->bStopped) {
				if (dynAnimGraphUpdaterSetFlag(pUpdater, pcFlagStopped, 0) &&
					(	dynDebugState.danimShowBits ||
						dynDebugState.audioShowAnimBits))
				{
					dynDebugSkeletonSetFlag(pSkeleton, pUpdater, pcFlagStopped, 0);
				}
			}

			while (eaSize(&pChartStack->eaStanceFlags))
			{
				const char* pcStanceFlag = eaPop(&pChartStack->eaStanceFlags);
				if (dynAnimGraphUpdaterSetFlag(pUpdater, pcStanceFlag, 0) &&
					(	dynDebugState.danimShowBits ||
						dynDebugState.audioShowAnimBits))
				{
					dynDebugSkeletonSetFlag(pSkeleton, pUpdater, pcStanceFlag, 0);
				}
			}
		}

		dynAnimGraphUpdaterUpdate(pUpdater, fDeltaTime, pSkeleton, 0);
	}

	dynSkeletonUpdateBlends(pSkeleton, fDeltaTime);

	{
		bool bSignalMovementStopped =	pSkeleton->fMovementSystemOverrideFactor < 1.f &&
										1.f <= pSkeleton->fMovementSystemOverrideFactorOld;

		pSkeleton->bIsPlayingDeathAnim = 0;
		FOR_EACH_IN_EARRAY(pSkeleton->eaAGUpdater, DynAnimGraphUpdater, pUpdater)
		{
			dynAnimGraphUpdaterSignalMovementStopped(pSkeleton, pUpdater, bSignalMovementStopped);

			pSkeleton->bIsPlayingDeathAnim |= pUpdater->bIsPlayingDeathAnim;

			dynAnimGraphUpdaterDoExitFX(pSkeleton, pUpdater, false, pSkeleton->fMovementSystemOverrideFactor, pSkeleton->fMovementSystemOverrideFactorOld);
			dynAnimGraphUpdaterDoEnterFX(pSkeleton, pUpdater, false, pSkeleton->fMovementSystemOverrideFactor, pSkeleton->fMovementSystemOverrideFactorOld);

			if (ipUpdaterIndex == 0) {
				dynAnimGraphUpdaterInitMatchJoints(pSkeleton, pUpdater, &pSkeleton->eaAnimGraphUpdaterMatchJoints);
			} else if (gConf.bUseMovementGraphs && ipUpdaterIndex == 1) {
				dynAnimGraphUpdaterInitMatchJoints(pSkeleton, pUpdater, &pSkeleton->eaSkeletalMovementMatchJoints);
			}
		}
		FOR_EACH_END;

		if (!gConf.bUseMovementGraphs) dynSkeletonMovementInitMatchJoints(pSkeleton, &pSkeleton->movement);
	}

	dynSkeletonUpdateRootNew(pSkeleton, fDeltaTime);

	glrLog(	pSkeleton->glr,
			"[dyn.AGU] Blends after: Override All: %.3f,  LowerBody: %.3f, Movement: %.3f",
			pSkeleton->fOverrideAllBlendFactor,
			pSkeleton->fLowerBodyBlendFactor,
			pSkeleton->fMovementSystemOverrideFactor);

	dynDrawSkeletonUpdateLODLevel(pSkeleton->pDrawSkel, pSkeleton->bBodySock);
	if (pSkeleton->pDrawSkel->bUpdateDrawInfo)
		dynDrawSkeletonUpdateDrawInfo(pSkeleton->pDrawSkel);

	//guess that we're not going to use IK for weapons
	pSkeleton->bHasLastPassIK		= false;
	pSkeleton->bIKBothHands			= false;
	pSkeleton->bRegisterWep			= false;
	pSkeleton->bIKMeleeMode			= false;
	pSkeleton->bEnableIKSliding		= false;
	pSkeleton->bDisableIKLeftWrist	= false;
	pSkeleton->bDisableIKRightArm	= false;
	pSkeleton->pcIKTarget			= NULL;
	pSkeleton->pcIKTargetNodeLeft	= NULL;
	pSkeleton->pcIKTargetNodeRight	= NULL;

	//check the WepL joint's sequencer for IK registration
	if (pSkeleton->pWepLNode)
	{
		DynAnimBoneInfo* pWepLInfo = &pSkeleton->pAnimBoneInfos[pSkeleton->pWepLNode->iCriticalBoneIndex];
		int iSeqIndex = pWepLInfo->iSeqAGUpdaterIndex;
		const DynMoveSeq *useSeq = pSkeleton->eaAGUpdater[iSeqIndex]->nodes[0].pMoveSeq;
		if (useSeq)
		{
			pSkeleton->bIKBothHands			= useSeq->bIKBothHands;
			pSkeleton->bRegisterWep			= useSeq->bRegisterWep;
			pSkeleton->bIKMeleeMode			= useSeq->bIKMeleeMode;
			pSkeleton->bEnableIKSliding		= useSeq->bEnableIKSliding;
			pSkeleton->bDisableIKLeftWrist	= useSeq->bDisableIKLeftWrist;
			pSkeleton->bDisableIKRightArm	= useSeq->bDisableIKRightArm;
			pSkeleton->pcIKTarget			= FIRST_IF_SET(useSeq->pcIKTarget,			pSkeleton->pcIKTarget);
			pSkeleton->pcIKTargetNodeLeft	= FIRST_IF_SET(useSeq->pcIKTargetNodeLeft,	pSkeleton->pcIKTargetNodeLeft);
			pSkeleton->pcIKTargetNodeRight	= FIRST_IF_SET(useSeq->pcIKTargetNodeRight,	pSkeleton->pcIKTargetNodeRight);
		}
	}

	//modify the ragdoll blend
	if (pSkeleton->ragdollState.bRagdollOn)	pSkeleton->ragdollState.fBlend += 5.f*fDeltaTime;
	else									pSkeleton->ragdollState.fBlend -= 5.f*fDeltaTime;
	pSkeleton->ragdollState.fBlend = CLAMP(pSkeleton->ragdollState.fBlend, 0.f, 1.f);

	//modify weapon registration blend
	if (pSkeleton->bRegisterWep || pSkeleton->fWepRegisterBlend > 0.0f)
	{
		const F32 fWepRegisterBlendRate = 4.0f;
		if (pSkeleton->bRegisterWep && pSkeleton->uiLODLevel <= worldLibGetLODSettings()->uiIKLODLevel)
		{
			pSkeleton->fWepRegisterBlend += fWepRegisterBlendRate * fDeltaTime;
		}
		else
		{
			pSkeleton->fWepRegisterBlend -= fWepRegisterBlendRate * fDeltaTime;
		}
		pSkeleton->fWepRegisterBlend = CLAMP(pSkeleton->fWepRegisterBlend, 0.0f, 1.0f);
	}

	//modify 2 handed IK registration blend
	if (pSkeleton->bIKBothHands || pSkeleton->fIKBothHandsBlend > 0.0f)
	{
		const F32 fIKBothHandsBlendRate = 4.0f;
		if (pSkeleton->bIKBothHands && pSkeleton->uiLODLevel <= worldLibGetLODSettings()->uiIKLODLevel)
		{
			pSkeleton->fIKBothHandsBlend += fIKBothHandsBlendRate * fDeltaTime;
		}
		else
		{
			pSkeleton->fIKBothHandsBlend -= fIKBothHandsBlendRate * fDeltaTime;
		}
		pSkeleton->fWepRegisterBlend = CLAMP(pSkeleton->fWepRegisterBlend, 0.0f, 1.0f);
	}

	dynAnimGraphUpdaterUpdateMatchJoints(pSkeleton, &pSkeleton->eaAnimGraphUpdaterMatchJoints, fDeltaTime);
	if (!gConf.bUseMovementGraphs) dynSkeletonMovementUpdateMatchJoints(pSkeleton, fDeltaTime);
	else                           dynAnimGraphUpdaterUpdateMatchJoints(pSkeleton, &pSkeleton->eaSkeletalMovementMatchJoints, fDeltaTime);

	EARRAY_CONST_FOREACH_BEGIN(pSkeleton->eaCachedAnimGraphFx, i, numCachedAnimGraphFx);
	{
		DynAnimGraphUpdater *pUpdater = pSkeleton->eaCachedAnimGraphFx[i]->pUpdater;
		if (!pSkeleton->pFxManager
			||
			(	!pUpdater->bOverlay &&
				(	(	pSkeleton->fMovementSystemOverrideFactor >= 1.f &&
						(	pSkeleton->fOverrideAllBlendFactor <= 0.f ||
							pSkeleton->eaAGUpdater[pSkeleton->iOverrideSeqIndex] != pUpdater))
					||
					(	pSkeleton->ragdollState.bRagdollOn &&
						pSkeleton->ragdollState.fBlend >= 1.f)
					||
					(	pSkeleton->fOverrideAllBlendFactor >= 1.f &&
						pSkeleton->eaAGUpdater[pSkeleton->iOverrideSeqIndex] != pUpdater)))
			||
			(	pUpdater->bOverlay &&
				(	pSkeleton->ragdollState.bRagdollOn ||
					pUpdater->fOverlayBlend <= 0.f)))
		{
			// don't trigger the FX since 0% of the animation will be blended in
			if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits)
			{
				dynDebugSkeletonStartGraphFX(	pSkeleton,
												pSkeleton->eaCachedAnimGraphFx[i]->pUpdater,
												pSkeleton->eaCachedAnimGraphFx[i]->pcName,
												0);
			}
		}
		else
		{
			// trigger the FX since more than 0% of the animation will be blended
			if (pSkeleton->eaCachedAnimGraphFx[i]->bMessage) {
				dynSkeletonBroadcastFXMessage(
					pSkeleton->pFxManager,
					pSkeleton->eaCachedAnimGraphFx[i]->pcName);
			} else {
				dynSkeletonQueueAnimationFx(pSkeleton,
											pSkeleton->pFxManager,
											pSkeleton->eaCachedAnimGraphFx[i]->pcName);
			}

			if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits)
			{
				dynDebugSkeletonStartGraphFX(	pSkeleton,
												pSkeleton->eaCachedAnimGraphFx[i]->pUpdater,
												pSkeleton->eaCachedAnimGraphFx[i]->pcName,
												1);
			}
		}
	}
	FOR_EACH_END;
	eaClearEx(&pSkeleton->eaCachedAnimGraphFx,NULL);

	EARRAY_CONST_FOREACH_BEGIN(pSkeleton->eaCachedSkelMoveFx, i, numCachedSkelMoveFx);
	{
		if ((	!pSkeleton->pFxManager )
			||
			(	pSkeleton->ragdollState.bRagdollOn &&
				pSkeleton->ragdollState.fBlend >= 1.f )
			||
			(	pSkeleton->bMovementBlending &&
				pSkeleton->fLowerBodyBlendFactor <= 0.f )
			||
			(	!pSkeleton->bMovementBlending &&
				pSkeleton->fMovementSystemOverrideFactor <= 0.f )
			||
			(	pSkeleton->fOverrideAllBlendFactor >= 1.f ))
		{
			// don't trigger the FX since 0% of the movement will be blended in
			if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits)
			{
				dynDebugSkeletonStartMoveFX(pSkeleton,
											pSkeleton->eaCachedSkelMoveFx[i]->pcName,
											0);
			}
		}
		else
		{
			// trigger the FX since more than 0% of the animation will be blended in
			if (pSkeleton->eaCachedSkelMoveFx[i]->bMessage) {
				dynSkeletonBroadcastFXMessage(
					pSkeleton->pFxManager,
					pSkeleton->eaCachedSkelMoveFx[i]->pcName);
			} else {
				dynSkeletonQueueAnimationFx(pSkeleton,
											pSkeleton->pFxManager,
											pSkeleton->eaCachedSkelMoveFx[i]->pcName);
			}

			if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits)
			{
				dynDebugSkeletonStartMoveFX(pSkeleton,
											pSkeleton->eaCachedSkelMoveFx[i]->pcName,
											1);
			}
		}
	}
	FOR_EACH_END;
	eaClearEx(&pSkeleton->eaCachedSkelMoveFx,NULL);

	if (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits)
		dynDebugSkeletonUpdateNewAnimSysData(pSkeleton, fDeltaTime);

	if (dynDebugState.costumeShowSkeletonFiles &&
		pSkeleton->pGenesisSkeleton == pSkeleton &&
		pSkeleton->getAudioDebugInfoFunc)
	{
		pSkeleton->getAudioDebugInfoFunc(pSkeleton);
	}

	if (!pSkeleton->bVisible &&
		!pSkeleton->bForceTransformUpdate)
	{
		glrLog(	pSkeleton->glr,
				"[dyn.AGU] %s END (not visible and not forced updated).",
				__FUNCTION__);

		dynAnimExpressionInvalidateData(pSkeleton, NULL);

		pSkeleton->bWasVisible |= pSkeleton->bForceVisible;

		return false;
	}

	pSkeleton->bForceTransformUpdate = 0;

	if(	dynDebugState.bNoAnimation ||
		pSkeleton->bAnimDisabled)
	{
		glrLog(	pSkeleton->glr,
				"[dyn.AGU] %s END (animation disabled).",
				__FUNCTION__);

		dynAnimExpressionInvalidateData(pSkeleton, NULL);
		dynDrawSkeletonBasePose(pSkeleton->pDrawSkel);

		pSkeleton->bWasForceVisible |= pSkeleton->bForceVisible;

		return false;
	}
	
	// Do the actual transform calculation, animation and skinning... all of the heavy lifting
	
	dynSkeletonDoTransformsAndSkinning(pSkeleton);

	pSkeleton->bWasForceVisible = pSkeleton->bForceVisible;

	glrLog(pSkeleton->glr, "[dyn.AGU] %s END.", __FUNCTION__);
	return true;
}

static void dynSkeletonCalcRunAndGunTransform(DynSkeleton* pSkeleton, DynNode* pWaist, DynTransform* pHipsTransform, DynTransform *pRootUpperBodyTransform, DynSkeletonRunAndGunBone *pRGBone, F32 fDeltaTime)
{
	// Note that quaternion multiplication is associative but not commutative,
	// and that it is 'back multiplied', meaning that you have to multiply from the top (waist) down (root)
	//
	// So: Order is Root -> Torso adjustment -> Hips -> Waist
	// Have to do torso adjustment before hips in case hips yaw character and we pitch

	DynTransform xWaist;
	Quat qTemp, qTemp2;

	dynNodeGetLocalTransformInline(pWaist, &xWaist);
	
	if (gConf.bNewAnimationSystem && pRGBone)
	{
		Quat qParent, qParentInv;
		const DynNode *pParentNode = pWaist->pParent;
		dynNodeGetWorldSpaceRot(pParentNode, qParent);
		quatInverse(qParent, qParentInv);
		
		quatMultiply(xWaist.qRot, qParent, qTemp);
		quatMultiply(qTemp, pRGBone->qPostRoot, qTemp2);
		quatMultiply(qTemp2, qParentInv, qTemp);

		dynNodeSetRotInline(pWaist, qTemp);
	}
	else //new anim pitch or old animation system
	{
		Quat qResult, qRoot;
		
		dynNodeGetWorldSpaceRot(pSkeleton->pRoot, qRoot);

		if (pRootUpperBodyTransform) {
			quatMultiply(xWaist.qRot, pRootUpperBodyTransform->qRot, qTemp2);
			quatMultiply(qTemp2, pHipsTransform->qRot, qTemp);
		} else {
			quatMultiply(xWaist.qRot, pHipsTransform->qRot, qTemp);
		}
		quatMultiply(qTemp, pSkeleton->qTorso, qTemp2);
		quatMultiply(qTemp2, qRoot, qResult);

		pWaist->uiTransformFlags &= ~ednRot;
		dynNodeSetRotInline(pWaist, qResult);
	}
}


__forceinline static bool shouldUpdateIntermittantBone(DynNode* pBone, U32 uiLODLevel, U8 uiRandomSeed)
{
	U32 uiFrameMask = (0x1 << (uiLODLevel - pBone->uiLODLevel)) - 1;
	if ( (wl_state.frame_count & uiFrameMask) == (uiRandomSeed & 0x1) )
	{

		return true;
	}
	return false;
}

static void dynSkeletonUpdateTransforms(DynSkeleton* pSkeleton, F32 fDeltaTime)
{
	DynNode*	nodeStack[100];
	S32			stackPos = 0;
	S32			first = 1;
	Mat4		mRoot;
	bool		bRoot = true;

	int iSkinningBoneIndex = 0;
	/*
	int iCorrect = 0;
	int iWrong = 0;
	*/
	DynNode* pPrefetch = NULL;
	DynTransform hipsTransform;
	Quat qRootRotInv;
	Vec3 vRootPos;
	bool bSetExtents = false;

	assert(pSkeleton->pAnimBoneInfos);

	if (pSkeleton->bTorsoPointing || pSkeleton->bTorsoDirections)
		dynTransformClearInline(&hipsTransform);

	PERFINFO_AUTO_START_FUNC();

	/*
	dynNodeGetWorldSpacePos(pSkeleton->pRoot, vWSExtentsMin);
	copyVec3(vWSExtentsMin, vWSExtentsMax);
	*/
	if (!pSkeleton->bDontUpdateExtents)
	{
		Quat qTemp;
		dynNodeGetWorldSpacePos(pSkeleton->pGenesisSkeleton->pRoot, vRootPos);
		dynNodeGetWorldSpaceRot(pSkeleton->pGenesisSkeleton->pRoot, qTemp);
		quatInverse(qTemp, qRootRotInv);

		setVec3same(pSkeleton->vCurrentExtentsMax, -FLT_MAX);
		setVec3same(pSkeleton->vCurrentExtentsMin, FLT_MAX);
	}

	//dynNodeSetDirtyInline(pBone);
	nodeStack[stackPos++] = pSkeleton->pRoot;

	while(stackPos){
		DynNode*	pBone = nodeStack[--stackPos];

#if PLATFORM_CONSOLE
		if (pSkeleton->pDrawSkel && pSkeleton->pDrawSkel->pCurrentSkinningMatSet)
			PREFETCH(pSkeleton->pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[iSkinningBoneIndex+1]);
#endif

		/*
		if (pBone == pPrefetch)
		iCorrect++;
		else
		iWrong++;
		*/
		assert(pBone);
		// look at the two actions, and blend them
		// for now, just the first one
		if (
			!pBone->uiCriticalBone ||
			!pBone->uiSkeletonBone ||
			!pBone->pcTag ||
			pBone->uiLocked || 
			pBone->uiMaxLODLevelBelow + MAX_LOD_UPDATE_DEPTH < pSkeleton->uiLODLevel
			)
		{
			// We aren't going to process this bone, but we need to still do some work

			if (pBone->uiCriticalBone)
			{
				if (!pSkeleton->bSnapshot)
				{
					FOR_EACH_IN_EARRAY(pSkeleton->eaMatchBaseSkelAnimJoints, DynJointBlend, pMatchJoint) {
						if (pBone->pcTag == pMatchJoint->pcName)
							dynSeqCalcMatchJoint(pSkeleton, pMatchJoint);
					} FOR_EACH_END;
				}

				dynNodeCalcWorldSpaceOneNode(pBone);
				assert(pBone->pcTag);
				if (pSkeleton->pDrawSkel && pBone->iSkinningBoneIndex >= 0)
				{
					Vec3 vRotPos, vPos;
					iSkinningBoneIndex = pBone->iSkinningBoneIndex;
					++dynDebugState.uiNumSkinnedBones;
					if (pSkeleton->pDrawSkel->pCurrentSkinningMatSet) //otherwise the skeleton is not visible & transforms were not force updated, so there are no skinning matrices
						dynNodeCreateSkinningMat(pSkeleton->pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[pBone->iSkinningBoneIndex], pBone, pSkeleton->pAnimBoneInfos[pBone->iCriticalBoneIndex].vBaseOffset, mRoot);
					if (!pSkeleton->bDontUpdateExtents)
					{
						Vec3 vScale;
						subVec3(dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vRootPos, vPos);
						quatRotateVec3Inline(qRootRotInv, vPos, vRotPos);
						dynNodeGetWorldSpaceScale(pBone, vScale);
						vec3RunningMinMaxWithRadius(vRotPos,
													pSkeleton->vCurrentExtentsMin,
													pSkeleton->vCurrentExtentsMax,
													dynDrawSkeletonGetNodeRadius(pSkeleton->pDrawSkel, pBone->pcTag) * vec3MaxComponent(vScale));
						bSetExtents = true;
					}
				}
				if (pBone->pCriticalChild 
					&& stackPos < ARRAY_SIZE(nodeStack))
				{
					nodeStack[stackPos++] = pBone->pCriticalChild;
				}
			}

			if(first)
			{
				first = 0;
			}
			else
			{
				if(	pBone->pCriticalSibling 
					&& stackPos < ARRAY_SIZE(nodeStack))
				{
					nodeStack[stackPos++] = pBone->pCriticalSibling;
				}
			}

			continue;
		}

		if(	pBone->pCriticalChild &&
			stackPos < ARRAY_SIZE(nodeStack))
		{
			nodeStack[stackPos++] = pBone->pCriticalChild;
		}

		if(first)
		{
			first = 0;
		}
		else
		{
			if(	pBone->pCriticalSibling
				&& stackPos < ARRAY_SIZE(nodeStack))
			{
				nodeStack[stackPos++] = pBone->pCriticalSibling;
			}
		}

		if (stackPos > 0)
		{
#if PLATFORM_CONSOLE
			PREFETCH(nodeStack[stackPos - 1]);
#endif
			pPrefetch = nodeStack[stackPos - 1];
		}

		// Do we calc this one bone?
		if (!pSkeleton->bSnapshot &&
			(	pBone->uiLODLevel >= pSkeleton->uiLODLevel ||
				pBone->uiLODLevel+MAX_LOD_UPDATE_DEPTH >= pSkeleton->uiLODLevel && shouldUpdateIntermittantBone(pBone, pSkeleton->uiLODLevel, pSkeleton->uiRandomSeed)))
		{
			DynAnimBoneInfo* pBoneInfo = &pSkeleton->pAnimBoneInfos[pBone->iCriticalBoneIndex];
			const DynTransform* pxBaseTransform = &pBoneInfo->xBaseTransform;
			assert(pBone->iCriticalBoneIndex >= 0);

			if (pxBaseTransform)
			{
				// Calculate the current transforms for the bone, for each sequencer
				int iSeqIndex = pSkeleton->pAnimBoneInfos[pBone->iCriticalBoneIndex].iSeqIndex;
				DynTransform mainTransform;
				++dynDebugState.uiNumAnimatedBones;

				if ((pSkeleton->bTorsoPointing || pSkeleton->bTorsoDirections) && pSkeleton->fOverrideAllBlendFactor > 0.0f && !dynSeqNeverOverride(pSkeleton->eaSqr[iSeqIndex]))
				{
					if (pSkeleton->fOverrideAllBlendFactor < 1.0f && iSeqIndex != pSkeleton->iOverrideSeqIndex)
					{
						// Blend 
						DynTransform weightedTransforms[2];

						dynSeqUpdateBone(pSkeleton->eaSqr[iSeqIndex], &weightedTransforms[0], pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						dynSeqUpdateBone(pSkeleton->eaSqr[pSkeleton->iOverrideSeqIndex], &weightedTransforms[1], pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						dynTransformInterp(pSkeleton->fOverrideAllBlendFactor, &weightedTransforms[0], &weightedTransforms[1], &mainTransform);
						dynNodeSetFromTransformInline(pBone, &mainTransform);
					}
					else // 100% override
					{
						dynSeqUpdateBone(pSkeleton->eaSqr[pSkeleton->iOverrideSeqIndex], &mainTransform, pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						dynNodeSetFromTransformInline(pBone, &mainTransform);
					}
				}
				else if ((pSkeleton->bTorsoPointing || pSkeleton->bTorsoDirections) && iSeqIndex == 1 && pSkeleton->fLowerBodyBlendFactor < 1.0f)
				{
					if (pSkeleton->fLowerBodyBlendFactor > 0.0f)
					{
						// Blend
						DynTransform weightedTransforms[2];

						dynSeqUpdateBone(pSkeleton->eaSqr[0], &weightedTransforms[0], pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						dynSeqUpdateBone(pSkeleton->eaSqr[1], &weightedTransforms[1], pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						dynTransformInterp(pSkeleton->fLowerBodyBlendFactor, &weightedTransforms[0], &weightedTransforms[1], &mainTransform);
						dynNodeSetFromTransformInline(pBone, &mainTransform);
					}
					else // 100% override
					{
						dynSeqUpdateBone(pSkeleton->eaSqr[0], &mainTransform, pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						dynNodeSetFromTransformInline(pBone, &mainTransform);
					}
				}
				else
				{
					dynSeqUpdateBone(pSkeleton->eaSqr[iSeqIndex], &mainTransform, pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
					dynNodeSetFromTransformInline(pBone, &mainTransform);
				}


				// This is some code that fixes up the foot height. It has to be done here, so that it propagates to the child bones and skinning matrices.
				if (pBone->pcTag == pcHipsName)
				{
					if (pSkeleton->bUseTorsoPointing || pSkeleton->bUseTorsoDirections)
					{
						dynSeqUpdateBone(pSkeleton->eaSqr[0], &hipsTransform, pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
					}

					if (pSkeleton->scaleCollection.fHipsHeightAdjustmentFactor != 1.0f)
					{
						Vec3 vPos, vBasePos, vDiff;
						dynNodeGetLocalPosInline(pBone, vPos);
						copyVec3(pxBaseTransform->vPos, vBasePos);
						subVec3(vPos, vBasePos, vDiff);
						scaleVec3(vPos, pSkeleton->scaleCollection.fHipsHeightAdjustmentFactor, vPos);
						//addVec3(vBasePos, vDiff, vPos);
						dynNodeSetPosInline(pBone, vPos);
					}
				}

				// Test pointing the torso towards something
				if ((pSkeleton->bUseTorsoPointing || pSkeleton->bUseTorsoDirections) && pBone->pcTag == pcWaistName && !pSkeleton->ragdollState.bRagdollOn)
				{
					dynSkeletonCalcRunAndGunTransform(pSkeleton, pBone, &hipsTransform, NULL, NULL, fDeltaTime);
				}
				else if (pBone->pcTag == pcWaistName)
				{
					pBone->uiTransformFlags |= ednRot;
				}


				// Overlay sequencers
				if (!pBoneInfo->bIgnoreMasterOverlay	&&
					pSkeleton->iOverlaySeqIndex > 0		&&
					dynSeqGetBlend(pSkeleton->eaSqr[pSkeleton->iOverlaySeqIndex]) > 0.0f)
				{
					DynTransform xTemp, xOverlay, xCurrent;
					dynSeqUpdateBone(pSkeleton->eaSqr[pSkeleton->iOverlaySeqIndex], &xTemp, pBone->pcTag, pBone->uiLODLevel, NULL, &pSkeleton->ragdollState, pSkeleton);
					dynTransformInterp(dynSeqGetBlend(pSkeleton->eaSqr[pSkeleton->iOverlaySeqIndex]), &xIdentity, &xTemp, &xOverlay);
					dynNodeGetLocalTransformInline(pBone, &xCurrent);
					dynTransformMultiply(&xOverlay, &xCurrent, &xTemp);
					dynNodeSetFromTransformInline(pBone, &xTemp);
				}
				if (pBoneInfo->iOverlayIndex > 0 &&
					dynSeqGetBlend(pSkeleton->eaSqr[pBoneInfo->iOverlayIndex]) > 0)
				{
					DynTransform xTemp, xOverlay, xCurrent;
					dynSeqUpdateBone(pSkeleton->eaSqr[pBoneInfo->iOverlayIndex], &xTemp, pBone->pcTag, pBone->uiLODLevel, NULL, &pSkeleton->ragdollState, pSkeleton);
					dynTransformInterp(dynSeqGetBlend(pSkeleton->eaSqr[pBoneInfo->iOverlayIndex]), &xIdentity, &xTemp, &xOverlay);
					dynNodeGetLocalTransformInline(pBone, &xCurrent);
					dynTransformMultiply(&xOverlay, &xCurrent, &xTemp);
					dynNodeSetFromTransformInline(pBone, &xTemp);
				}

				FOR_EACH_IN_EARRAY(pSkeleton->eaMatchBaseSkelAnimJoints, DynJointBlend, pMatchJoint) {
					if (pBone->pcTag == pMatchJoint->pcName)
						dynSeqCalcMatchJoint(pSkeleton, pMatchJoint);
				} FOR_EACH_END;
			}
			else
			{
				Errorf("Unable to find basebone %s, should not be trying to animate it.", pBone->pcTag);
			}

			if (pSkeleton->uiNumScaleModifiers > 0)
			{
				U32 uiIndex;
				Vec3 vScaleModifier;
				bool bScale = false;
				// Actually use parent's scale modifiers, they may have already been handled
				// Parent should only have some if we also have some.
				DynSkeleton *pEffSkeleton = pSkeleton->pGenesisSkeleton;

				unitVec3(vScaleModifier);
				for (uiIndex=0; uiIndex<pEffSkeleton->uiNumScaleModifiers; ++uiIndex)
				{
					U32 uiBoneNum;
					DynFxDrivenScaleModifier *const pModifier = &pEffSkeleton->aScaleModifiers[uiIndex];
					for (uiBoneNum=0; uiBoneNum < pModifier->uiNumBones; ++uiBoneNum)
					{
						if (pModifier->apcBoneName[uiBoneNum] == pBone->pcTag)
						{
							bScale = true;
							mulVecVec3(vScaleModifier, pModifier->vScale, vScaleModifier);
							// Prevent applying the same modifier twice on a frame, they are scaling
							//  "Base" and both the main skeleton and a child skeleton have a bone
							//  named that.
							// Remove from lists to speed up the rest of the evaluations later
							// Or flag it if we can't remove it
							if (uiBoneNum == pModifier->uiNumBones-1)
							{
								pModifier->uiNumBones--;
								if (pModifier->uiNumBones == 0 && uiIndex == pEffSkeleton->uiNumScaleModifiers - 1)
									pEffSkeleton->uiNumScaleModifiers--;
							} else
								pModifier->apcBoneName[uiBoneNum] = "AlreadyAppliedThisFrame";
							break;
						}
					}
				}
				if (bScale)
				{
					Vec3 vScale;
					dynNodeGetLocalScale(pBone, vScale);
					mulVecVec3(vScale, vScaleModifier, vScale);
					dynNodeSetScaleInline(pBone, vScale);
				}
			}
		}

		if (bRoot)
		{
			pBone->uiDirtyBits = 1;
			dynNodeGetWorldSpaceMat(pBone, mRoot, false);
			bRoot = false;
		}
		else
		{
			dynNodeCalcWorldSpaceOneNode(pBone);
		}

		if (pBone->uiHasBouncer &&
			!pSkeleton->bSnapshot)
		{
			FOR_EACH_IN_EARRAY(pSkeleton->eaBouncerUpdaters, DynBouncerUpdater, pUpdater)
			{
				if (pUpdater->pInfo->pcBoneName == pBone->pcTag)
				{
					dynBouncerUpdaterUpdateBone(pUpdater, pBone, fDeltaTime);
				}
			}
			FOR_EACH_END;
		}

		if (pSkeleton->pDrawSkel && pBone->iSkinningBoneIndex >= 0 && pBone->uiSkinningBone)
		{
			Vec3 vRotPos, vPos;
			iSkinningBoneIndex = pBone->iSkinningBoneIndex;
			++dynDebugState.uiNumSkinnedBones;
			assert(pBone->iCriticalBoneIndex >= 0);
			dynNodeCreateSkinningMat(pSkeleton->pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[pBone->iSkinningBoneIndex], pBone, pSkeleton->pAnimBoneInfos[pBone->iCriticalBoneIndex].vBaseOffset, mRoot);
			if (!pSkeleton->bDontUpdateExtents)
			{
				Vec3 vScale;
				subVec3(dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vRootPos, vPos);
				quatRotateVec3Inline(qRootRotInv, vPos, vRotPos);
				dynNodeGetWorldSpaceScale(pBone, vScale);
				vec3RunningMinMaxWithRadius(vRotPos,
											pSkeleton->vCurrentExtentsMin,
											pSkeleton->vCurrentExtentsMax,
											dynDrawSkeletonGetNodeRadius(pSkeleton->pDrawSkel, pBone->pcTag) * vec3MaxComponent(vScale));
				bSetExtents = true;
			}
		}
	}

	if (!pSkeleton->bDontUpdateExtents)
	{
		DynSkeleton *pActiveSkeleton = pSkeleton;
		DynSkeleton *pParentSkeleton = pSkeleton->pParentSkeleton;

		 if (!bSetExtents) {
			 zeroVec3(pSkeleton->vCurrentExtentsMax);
			 zeroVec3(pSkeleton->vCurrentExtentsMin);
		 }
		 copyVec3(pSkeleton->vCurrentExtentsMax, pSkeleton->vCurrentGroupExtentsMax);
		 copyVec3(pSkeleton->vCurrentExtentsMin, pSkeleton->vCurrentGroupExtentsMin);

		 while (pParentSkeleton && !pParentSkeleton->bDontUpdateExtents)
		 {
			 MAXVEC3(pParentSkeleton->vCurrentGroupExtentsMax, pActiveSkeleton->vCurrentGroupExtentsMax, pParentSkeleton->vCurrentGroupExtentsMax);
			 MINVEC3(pParentSkeleton->vCurrentGroupExtentsMin, pActiveSkeleton->vCurrentGroupExtentsMin, pParentSkeleton->vCurrentGroupExtentsMin);
			 pActiveSkeleton = pParentSkeleton;
			 pParentSkeleton = pParentSkeleton->pParentSkeleton;
		 }
	}

	PERFINFO_AUTO_STOP_CHECKED(__FUNCTION__);
}

static void dynSkeletonLogBlendSources(DynSkeleton* pSkeleton)
{
	glrLog(	pSkeleton->glr,
			"[dyn.AGU_BeforeBlend] Blend sources BEGIN.");

	glrLog(	pSkeleton->glr,
			"[dyn.AGU_BeforeBlend] Override all blend: %1.3f.",
			pSkeleton->fOverrideAllBlendFactor);

	glrLog(	pSkeleton->glr,
			"[dyn.AGU_BeforeBlend] Lower body blend: %1.3f.",
			pSkeleton->fLowerBodyBlendFactor);

	glrLog(	pSkeleton->glr,
			"[dyn.AGU_BeforeBlend] Movement blend: %1.3f.",
			pSkeleton->fMovementSystemOverrideFactor);

	EARRAY_CONST_FOREACH_BEGIN(pSkeleton->eaAGUpdater, i, isize);
	{
		const DynAnimGraphUpdater* u = pSkeleton->eaAGUpdater[i];

		glrLog(	pSkeleton->glr,
				"[dyn.AGU_BeforeBlend] AGU: %s.",
				u->pChartStack && eaSize(&u->pChartStack->eaChartStack) ?
					u->pChartStack->eaChartStack[0]->pcName :
					"no charts in stack");

		ARRAY_FOREACH_BEGIN(u->nodes, j);
		{
			const DynAnimGraphUpdaterNode* n = u->nodes + j;

			glrLog(	pSkeleton->glr,
					"[dyn.AGU_BeforeBlend] Node %u: %s/%s, blend %1.3f (%1.3fs), frame %1.3f/%1.3f, .",
					j,
					n->pGraph->pcName,
					n->pGraphNode->pcName,
					n->fBlendFactor,
					n->fTimeOnBlend,
					n->fTime,
					n->pMoveSeq->fLength);

			if(n->fBlendFactor >= 1.f){
				break;
			}
		}
		ARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	if (!gConf.bUseMovementGraphs)
	{
		glrLog(	pSkeleton->glr,
				"[dyn.AGU_BeforeBlend] Movement:");

		EARRAY_CONST_FOREACH_BEGIN(pSkeleton->movement.eaBlocks, i, isize);
		{
			const DynMovementBlock* b = pSkeleton->movement.eaBlocks[i];
			char					stanceWords[200];

			dynAnimChartGetStanceWords(b->pChart, SAFESTR(stanceWords));

			if(!b->inTransition){
				glrLog(	pSkeleton->glr,
						"[dyn.AGU_BeforeBlend] Block %u: %s (%s), blend %1.3f, frame %1.3f/%1.3f.",
						i,
						b->pMoveSeqCycle->pDynMove->pcName,
						stanceWords,
						b->fBlendFactor,
						b->fFrameTime,
						b->pMoveSeqCycle->fLength);
			}else{
				char transitionDesc[200];

				dynMovementBlockGetTransitionString(dynMovementBlockGetTransition(b), SAFESTR(transitionDesc));

				glrLog(	pSkeleton->glr,
						"[dyn.AGU_BeforeBlend] Block %u: %s (%s), blend %1.3f, frame %1.3f/%1.3f TRANSITION(%s).",
						i,
						b->pMoveSeqTransition->pDynMove->pcName,
						stanceWords,
						b->fBlendFactor,
						b->fFrameTime,
						b->pMoveSeqTransition->fLength,
						transitionDesc);
			}
		}
		EARRAY_FOREACH_END;
	}

	glrLog(	pSkeleton->glr,
			"[dyn.AGU_BeforeBlend] Blend sources END.");
}

static S32 showGroundRegBaseSkeleton;
AUTO_CMD_INT(showGroundRegBaseSkeleton, dynAnimShowGroundRegBaseSkeleton);

static F32 dynSkeletonGetBaseSkeletonTransformHeight(
	const DynSkeleton *pSkeleton, const DynScaleCollection* pScaleCollection, const DynBaseSkeleton* pBaseSkeleton, DynNode* pEndJoint,
	bool bFindHyperExtension, const Vec3 vHyperAxis, F32 *fHyperAngle
	)
{
	//used for determining the base skeleton
	DynNode* aNodes[32];
	int iNumNodes;
	DynTransform xRunning;
	int i;

	//used for determining hyper-extension
	Vec3 vHyperAxisWS;
	Vec3 vBendingJointPos;

	//used for debug visuals
	Vec3 vHyperAxisWS_x, vHyperAxisWS_y, vHyperAxisWS_z;

	iNumNodes = 1;
	aNodes[0] = pEndJoint;
	while (aNodes[0] != pSkeleton->pRoot) {
		aNodes[0] = aNodes[0]->pParent;
		iNumNodes++;
	}

	aNodes[iNumNodes-1] = pEndJoint;
	for (i=(iNumNodes-2); i>=0; --i)
	{
		aNodes[i] = aNodes[i+1]->pParent;
	}
	assert(aNodes[0] == pSkeleton->pRoot);

	dynNodeCalcWorldSpaceOneNode(aNodes[0]);
	dynNodeGetWorldSpaceTransform(aNodes[0], &xRunning);
	unitVec3(xRunning.vScale);

	if (pBaseSkeleton && pScaleCollection)
	{
		if (pScaleCollection->uiNumTransforms > 0) {
			xRunning.vScale[0] = pScaleCollection->pLocalTransforms[0].vScale[0];
			xRunning.vScale[1] = pScaleCollection->pLocalTransforms[0].vScale[1];
			xRunning.vScale[2] = pScaleCollection->pLocalTransforms[0].vScale[2];
		}

		for (i=1; i<iNumNodes; ++i)
		{
			DynTransform xNode, xBaseNode, xRegNode;
			DynTransform xBaseInv, xTemp;
			Vec3 vPos, vPosOld;
			Quat qRot;

			copyVec3(xRunning.vPos, vPosOld);

			dynNodeGetLocalTransformInline(aNodes[i], &xNode);
			dynTransformCopy(dynScaleCollectionFindTransform(pScaleCollection, aNodes[i]->pcTag), &xBaseNode);
			dynNodeGetLocalTransformInline(dynBaseSkeletonFindNode(pBaseSkeleton, aNodes[i]->pcTag), &xRegNode);
			dynTransformInverse(&xBaseNode, &xBaseInv);

			if (i == 1) {
				xNode.vPos[1] -= pSkeleton->fHeightBump;
			}

			if (!(aNodes[i]->uiTransformFlags & ednRot))   unitQuat(xRunning.qRot);
			//if (!(aNodes[i]->uiTransformFlags & ednScale)) unitVec3(xRunning.vScale); Do NOT allow for this since it'll mess up the base skeleton aspect
			if (!(aNodes[i]->uiTransformFlags & ednTrans)) zeroVec3(xRunning.vPos);

			dynTransformMultiply(&xNode, &xBaseInv, &xTemp);
			dynTransformMultiply(&xTemp, &xRegNode, &xNode);
			
			if (bFindHyperExtension &&
				i == iNumNodes-3)
			{
				quatRotateVec3Inline(xRunning.qRot, vHyperAxis, vHyperAxisWS);
				if (showGroundRegBaseSkeleton) {
					quatRotateVec3Inline(xRunning.qRot, sidevec,    vHyperAxisWS_x);
					quatRotateVec3Inline(xRunning.qRot, upvec,	    vHyperAxisWS_y);
					quatRotateVec3Inline(xRunning.qRot, forwardvec, vHyperAxisWS_z);
				}
			}

			mulVecVec3(xNode.vPos, xRunning.vScale, xNode.vPos);
			if (aNodes[i]->uiTransformFlags & ednLocalRot) {
				quatMultiplyInline(xNode.qRot, xRunning.qRot, qRot);
				copyQuat(qRot, xRunning.qRot);
				quatRotateVec3Inline(xRunning.qRot, xNode.vPos, vPos);
			} else {
				quatRotateVec3Inline(xRunning.qRot, xNode.vPos, vPos);
				quatMultiplyInline(xNode.qRot, xRunning.qRot, qRot);
				copyQuat(qRot, xRunning.qRot);
			}
			addVec3(vPos, xRunning.vPos, xRunning.vPos);

			if (bFindHyperExtension)
			{
				if (i == iNumNodes - 3)
				{
					copyVec3(xRunning.vPos, vBendingJointPos);
				}
				else if (i == iNumNodes - 2)
				{
					Vec3 vHyperDetectionBone;
					subVec3(xRunning.vPos, vBendingJointPos, vHyperDetectionBone);
					normalVec3(vHyperDetectionBone);
					*fHyperAngle = dotVec3(vHyperDetectionBone, vHyperAxisWS);

					//debug render the hyper extension axis and coordinate system
					if (showGroundRegBaseSkeleton) {
						Vec3 vTarget;
						U32 uiColor;
						
						uiColor = (*fHyperAngle < 0.05 ? 0xFFAA6611 : 0xFFFF9933);
						addVec3(vHyperAxisWS, vBendingJointPos, vTarget);
						wl_state.drawLine3D_2_func(vBendingJointPos, vTarget, uiColor, uiColor);

						scaleAddVec3(vHyperAxisWS_x, 0.5, vBendingJointPos, vTarget);
						wl_state.drawLine3D_2_func(vBendingJointPos, vTarget, 0xFFFF0000, 0xFFFF0000);
						scaleAddVec3(vHyperAxisWS_y, 0.5, vBendingJointPos, vTarget);
						wl_state.drawLine3D_2_func(vBendingJointPos, vTarget, 0xFF00FF00, 0xFF00FF00);
						scaleAddVec3(vHyperAxisWS_z, 0.5, vBendingJointPos, vTarget);
						wl_state.drawLine3D_2_func(vBendingJointPos, vTarget, 0xFF0000FF, 0xFF0000FF);
					}
				}
			}

			if (showGroundRegBaseSkeleton) {
				wl_state.drawLine3D_2_func(vPosOld, xRunning.vPos, 0xFFFFFFFF, 0xFFFFFFFF);
			}
		}
	}

	return xRunning.vPos[1];
}

static F32 dynSkeletonGetTransformHeight(DynNode *pBone)
{
	if (pBone){
		Vec3 vBonePos;
		dynNodeGetWorldSpacePos(pBone, vBonePos);
		return vBonePos[1];
	}
	return 0.0;
}

static void dynSkeletonUpdateBoneChainTransforms(DynSkeleton *pSkeleton, DynNode *pStartJoint, DynNode *pEndJoint)
{
	DynNode* aNodes[32];
	int iNumNodes, i;

	iNumNodes = 1;
	aNodes[0] = pEndJoint;
	while (aNodes[iNumNodes-1] != pStartJoint) {
		aNodes[iNumNodes] = aNodes[iNumNodes-1]->pParent;
		iNumNodes++;
	}

	for (i = iNumNodes-1; i >= 0; i--) {
		dynNodeCalcWorldSpaceOneNode(aNodes[i]);
	}
}

static void dynSkeletonDrawBoneChain(DynSkeleton *pSkeleton, DynNode *pStartNode, DynNode *pEndJoint, U32 uiColor)
{
	DynTransform xCurrent, xNext;
	DynNode* aNodes[32];
	int iNumNodes, i;

	iNumNodes = 1;
	aNodes[0] = pEndJoint;
	while (aNodes[iNumNodes-1] != pStartNode) {
		aNodes[iNumNodes] = aNodes[iNumNodes-1]->pParent;
		iNumNodes++;
	}

	dynNodeCalcWorldSpaceOneNode(aNodes[iNumNodes-1]);
	dynNodeGetWorldSpaceTransform(aNodes[iNumNodes-1], &xCurrent);
	for (i = iNumNodes-2; i >= 0; i--) {
		dynNodeCalcWorldSpaceOneNode(aNodes[i]);
		dynNodeGetWorldSpaceTransform(aNodes[i], &xNext);
		wl_state.drawLine3D_2_func(xCurrent.vPos, xNext.vPos, uiColor, uiColor);
		dynTransformCopy(&xNext, &xCurrent);
	}
}

static S32 disableGroundRegHeightBump;
AUTO_CMD_INT(disableGroundRegHeightBump, dynAnimDisableGroundRegHeightBump);

static S32 disableGroundRegHyperExPrevention;
AUTO_CMD_INT(disableGroundRegHyperExPrevention, dynAnimDisableGroundRegHyperExPrevention);

static bool dynSkeletonRagdollUpdateBoneWithPhysics(const char *pcBoneTag,
													DynTransform *pTransform,
													const DynTransform *pxBaseTransform,
													DynRagdollState *pRagdollState,
													DynNode *pRoot)
{
	U32 uiPart;

	copyVec3(pxBaseTransform->vPos, pTransform->vPos);
	copyQuat(pxBaseTransform->qRot, pTransform->qRot);
	copyVec3(pxBaseTransform->vScale, pTransform->vScale);

	for (uiPart = 0; uiPart < pRagdollState->uiNumParts; ++uiPart)
	{
		DynRagdollPartState* pPart = &pRagdollState->aParts[uiPart];
		if (pPart->pcBoneName == pcBoneTag)
		{
			if (!pPart->pcParentBoneName)
			{
				//Rotation
				DynTransform xRoot;
				Quat qInv;
				dynNodeGetWorldSpaceTransform(pRoot, &xRoot);
				quatInverse(xRoot.qRot, qInv);

				quatMultiply(pPart->qWorldSpace, qInv, pTransform->qRot);

				// Translation
				{
					Vec3 vPos;
					subVec3(pRagdollState->vHipsWorldSpace, xRoot.vPos, vPos);
					vPos[0] /= xRoot.vScale[0];
					vPos[1] /= xRoot.vScale[1];
					vPos[2] /= xRoot.vScale[2];
					quatRotateVec3Inline(qInv, vPos, pTransform->vPos);
				}
			}
			else
			{
				copyQuat(pPart->qLocalSpace, pTransform->qRot);
			}
			return true;
		}
	}

	return false;
}

static bool dynSkeletonRagdollUpdateBoneWithPose(	DynSkeleton *pSkeleton,
													const char *pcBoneTag,
													DynTransform *pTransform,
													const DynTransform *pxBaseTransform)
{
	if (pSkeleton->pClientSideRagdollPoseAnimTrackHeader)
	{
		if (dynAnimTrackHeaderRequest(pSkeleton->pClientSideRagdollPoseAnimTrackHeader))
		{
			bool bUpdatedBone = dynBoneTrackUpdate(	pSkeleton->pClientSideRagdollPoseAnimTrackHeader->pAnimTrack,
													pSkeleton->fClientSideRagdollPoseAnimTrackFrame,
													pTransform,
													pcBoneTag,
													pxBaseTransform,
													true);
			return bUpdatedBone;
		}
		// else something has gone horribly wrong
	}
	//else the ragdoll data setup just didn't specify a pose animation track

	dynTransformCopy(pxBaseTransform, pTransform);

	return false;
}

static void dynSkeletonUpdateTransformsNew(DynSkeleton* pSkeleton, F32 fDeltaTime)
{
	DynNode*		nodeStack[100];
	S32				stackPos;
	S32				first;
	Mat4			mRoot;
	bool			bRoot;
	int				iSkinningBoneIndex;
	DynTransform	hipsTransform;
	DynTransform	rootUpperBodyTransform;
	bool			bSetExtents = false;
	bool			bHasRootUpperBody;
	int				iBonesSkinned = 0;
	int				iSkeletonTotalBones=0,iPrefetchBone=0;

	PERFINFO_AUTO_START_FUNC();

	if(pSkeleton->glr){
		dynSkeletonLogBlendSources(pSkeleton);
	}

	assert(pSkeleton->pAnimBoneInfos);

	if(pSkeleton->bMovementBlending){
		dynTransformClearInline(&hipsTransform);
		dynTransformClearInline(&rootUpperBodyTransform);
		bHasRootUpperBody = false;
	}

	#if 0
	dynNodeGetWorldSpacePos(pSkeleton->pRoot, vWSExtentsMin);
	copyVec3(vWSExtentsMin, vWSExtentsMax);
	#endif

	//init the height bump
	pSkeleton->fHeightBump = 0.0f;
	pSkeleton->bUpdatedGroundRegHips = false;

	//dynNodeSetDirtyInline(pBone);
	stackPos = 0;
	first = 1;
	bRoot = true;
	iSkinningBoneIndex = 0;
	nodeStack[stackPos++] = pSkeleton->pRoot;

	// It was very challenging to determine if this was doing any good and how much.  I think I have definitely proven that it can,
	// but not by much.  I think there's a lot of improvement to be had in terms of data flow, in conjunction with streamlining the code.
	// I doubt there is any "prefetch" left on the table.
	{
		const DynBaseSkeleton* pBaseSkeleton = GET_REF(pSkeleton->hBaseSkeleton);
		iSkeletonTotalBones = (int)pBaseSkeleton->uiNumBones;
		for (iPrefetchBone=0;iPrefetchBone<MIN(iSkeletonTotalBones,5);iPrefetchBone++)
		{
			PREFETCH(&pSkeleton->pRoot[iPrefetchBone]);
		//	PREFETCH(((U8 *)&pSkeleton->pRoot[iPrefetchBone])+64);
		}
	}

	while(stackPos){
		DynNode*	pBone = nodeStack[--stackPos];
		assert(pBone);

		// fetch one more bone
		if (iPrefetchBone < iSkeletonTotalBones)
		{
			PREFETCH(&pSkeleton->pRoot[iPrefetchBone]);
		//	PREFETCH(((U8 *)&pSkeleton->pRoot[iPrefetchBone])+64);
			iPrefetchBone++;
		}

	//	if (SAFE_MEMBER(pSkeleton->pDrawSkel, pCurrentSkinningMatSet)){
		//	PREFETCH(pSkeleton->pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[iSkinningBoneIndex+1]);
	//	}

		// look at the two actions, and blend them
		// for now, just the first one
		if(	!pBone->uiCriticalBone ||
			!pBone->uiSkeletonBone ||
			!pBone->pcTag ||
			pBone->uiLocked || 
			pBone->uiMaxLODLevelBelow + MAX_LOD_UPDATE_DEPTH < pSkeleton->uiLODLevel)
		{
			// We aren't going to process this bone, but we need to still do some work

			if (pBone->uiCriticalBone)
			{
				if (pBone->pCriticalChild 
					&& stackPos < ARRAY_SIZE(nodeStack))
				{
					nodeStack[stackPos++] = pBone->pCriticalChild;
				}
			}

			if(first)
			{
				first = 0;
			}
			else if(pBone->pCriticalSibling &&
					stackPos < ARRAY_SIZE(nodeStack))
			{
				nodeStack[stackPos++] = pBone->pCriticalSibling;
			}

			dynAnimExpressionInvalidateData(pSkeleton, pBone);

			if (pBone->uiHasBouncer)
			{
				//set the physics to re-init since the bouncer has basically gone away
				FOR_EACH_IN_EARRAY(pSkeleton->eaBouncerUpdaters, DynBouncerUpdater, pUpdater) {
					if (pUpdater->pInfo->pcBoneName == pBone->pcTag) {
						pUpdater->bNeedsInit = true;
					}
				} FOR_EACH_END;
			}
			pBone->uiUpdatedThisAnim = 0;
			continue;
		}

		pBone->uiUpdatedThisAnim = 0;
		if(	pBone->pCriticalChild &&
			stackPos < ARRAY_SIZE(nodeStack))
		{
			nodeStack[stackPos++] = pBone->pCriticalChild;
		}

		if(!TRUE_THEN_RESET(first))
		{
			if(	pBone->pCriticalSibling &&
				stackPos < ARRAY_SIZE(nodeStack))
			{
				nodeStack[stackPos++] = pBone->pCriticalSibling;
			}
		}

		// Do we calc this one bone?
		if( pBone->uiLODLevel >= pSkeleton->uiLODLevel
			||
			pBone->uiLODLevel+MAX_LOD_UPDATE_DEPTH >= pSkeleton->uiLODLevel &&
			shouldUpdateIntermittantBone(pBone, pSkeleton->uiLODLevel, pSkeleton->uiRandomSeed))
		{
			DynAnimBoneInfo* pBoneInfo = &pSkeleton->pAnimBoneInfos[pBone->iCriticalBoneIndex];
			const DynTransform* pxBaseTransform = &pBoneInfo->xBaseTransform;

			assert(pBone->iCriticalBoneIndex >= 0);
			pBone->uiUpdatedThisAnim = 1;

			if (pxBaseTransform)
			{
				// Calculate the current transforms for the bone, for each sequencer
				int iSeqIndex = pBoneInfo->iSeqAGUpdaterIndex;//->iSeqIndex;
				DynTransform mainTransform;
				++iBonesSkinned;

				// This big if / else / else (...) statement is basically where all per-bone blending decisions are made.
				// This could probably be cleaned up using sub functions, macros, or some other dark magic, but for now is fairly easy to follow in a debugger, so it stays.
				if (pSkeleton->ragdollState.bRagdollOn &&
					pSkeleton->ragdollState.fBlend >= 1.0f)
				{
					if (pBone->uiRagdollPoisonBit) {
						dynSkeletonRagdollUpdateBoneWithPhysics(pBone->pcTag, &mainTransform, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton->pRoot);
					} else {
						dynSkeletonRagdollUpdateBoneWithPose(pSkeleton, pBone->pcTag, &mainTransform, pxBaseTransform);
					}
				}
				else if(pSkeleton->bMissingAnimData)
				{
					//missing animation data, will likely lead to crash, just use the bone's previous value
					dynNodeGetLocalTransformInline(pBone, &mainTransform);
				}
				else if(pSkeleton->bMovementBlending &&
						pBoneInfo->bMovement &&
						pSkeleton->fLowerBodyBlendFactor < 1.0f) 
				{
					// movement bone, but lower body blend factor has dropped (probably not moving)
					if (pSkeleton->fLowerBodyBlendFactor > 0.0f)
					{
						DynTransform weightedTransforms[2]; // Blend
						dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[iSeqIndex], &weightedTransforms[0], pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						if (!gConf.bUseMovementGraphs) dynSkeletonMovementUpdateBone(&pSkeleton->movement,      &weightedTransforms[1], pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						else                           dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[1], &weightedTransforms[1], pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						dynTransformInterp(pSkeleton->fLowerBodyBlendFactor, &weightedTransforms[0], &weightedTransforms[1], &mainTransform);
					}
					else // 100% override
						dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[iSeqIndex], &mainTransform, pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
				}
				else if(pSkeleton->bMovementBlending &&
						!pBoneInfo->bMovement &&
						pSkeleton->fMovementSystemOverrideFactor > 0.0f &&
						iSeqIndex == 0)
				{
					// default sequencer bone, but movement is overriding it
					if(pSkeleton->fMovementSystemOverrideFactor < 1.0f)
					{
						DynTransform weightedTransforms[2]; // Blend
						dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[iSeqIndex], &weightedTransforms[0], pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						if (!gConf.bUseMovementGraphs) dynSkeletonMovementUpdateBone(&pSkeleton->movement,      &weightedTransforms[1], pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						else                           dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[1], &weightedTransforms[1], pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						dynTransformInterp(pSkeleton->fMovementSystemOverrideFactor, &weightedTransforms[0], &weightedTransforms[1], &mainTransform);
					}
					else // 100% override
					{
						if (!gConf.bUseMovementGraphs) dynSkeletonMovementUpdateBone(&pSkeleton->movement,      &mainTransform, pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						else                           dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[1], &mainTransform, pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
					}
				}
				else if (pSkeleton->bMovementBlending){
					// normal, torso pointing skeleton

					if (pBoneInfo->bMovement) {
						if (!gConf.bUseMovementGraphs) dynSkeletonMovementUpdateBone(&pSkeleton->movement,      &mainTransform, pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						else                           dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[1], &mainTransform, pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
					} else
						dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[iSeqIndex], &mainTransform, pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
				}else{
					// non torso pointing character (single agupdater + movement updater)

					if (pSkeleton->fMovementSystemOverrideFactor == 1.0f){ // 100% 
						if (!gConf.bUseMovementGraphs) dynSkeletonMovementUpdateBone(&pSkeleton->movement,      &mainTransform, pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						else                           dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[1], &mainTransform, pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
					}
					else if(pSkeleton->fMovementSystemOverrideFactor == 0.0f){ // 0%
						dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[iSeqIndex], &mainTransform, pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
					}else{
						DynTransform weightedTransforms[2]; // Blend
						dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[iSeqIndex], &weightedTransforms[0], pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						if (!gConf.bUseMovementGraphs) dynSkeletonMovementUpdateBone(&pSkeleton->movement,      &weightedTransforms[1], pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						else                           dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[1], &weightedTransforms[1], pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						dynTransformInterp(pSkeleton->fMovementSystemOverrideFactor, &weightedTransforms[0], &weightedTransforms[1], &mainTransform);
					}
				}

				// After normal logic, check override all
				if (pSkeleton->fOverrideAllBlendFactor > 0.0f &&
					!pSkeleton->bMissingAnimData){
					//override all is occurring

					if(pSkeleton->fOverrideAllBlendFactor < 1.0f){
						DynTransform weightedTransforms[2]; // Blend 
						weightedTransforms[0] = mainTransform;
						dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[pSkeleton->iOverrideSeqIndex], &weightedTransforms[1], pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
						dynTransformInterp(pSkeleton->fOverrideAllBlendFactor, &weightedTransforms[0], &weightedTransforms[1], &mainTransform);
					}else{
						// 100% override
						dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[pSkeleton->iOverrideSeqIndex], &mainTransform, pBone->pcTag, pBone->uiLODLevel, pxBaseTransform, &pSkeleton->ragdollState, pSkeleton);
					}
				}

				//apply the riders scale
				if (pBone == pSkeleton->pRoot) {
					if (pSkeleton->bMount) {
						mulVecVec3(mainTransform.vScale, pSkeleton->vAppliedRiderScale, mainTransform.vScale);
					}
					if (!dynDebugState.bDisableTerrainTiltOffset) {
						//mainTransform.vPos[1] += pSkeleton->fTerrainHeightBump;
						mainTransform.vPos[2] += pSkeleton->fTerrainOffsetZ;
					}
				}

				//apply movement banking to non-root nodes when overridden
				if (pBone->pcTag == pSkeleton->pcBankingOverrideNode &&
					pSkeleton->uiBankingOverrideStanceCount > 0)
				{
					Quat qMovementBank;
					Quat qTemp;
					copyQuat(mainTransform.qRot, qTemp);
					rollQuat(pSkeleton->fMovementBankBlendFactor*pSkeleton->fMovementBank, qMovementBank);
					quatMultiply(qMovementBank, qTemp, mainTransform.qRot);
				}

				// All the above functions do is set the maintransform. This sets it on the node
				dynNodeSetFromTransformInline(pBone, &mainTransform);

				//run and gun (calc and apply)
				if (!pSkeleton->ragdollState.bRagdollOn		&&
					!pSkeleton->bPreventRunAndGunUpperBody	&&
					!pSkeleton->bMissingAnimData			&&
					pSkeleton->bUseTorsoPointing			)
				{
					if (pBone->pcTag == pcWaistName)
						dynSkeletonCalcRunAndGunTransform(pSkeleton, pBone, &hipsTransform, bHasRootUpperBody?&rootUpperBodyTransform:NULL, NULL, fDeltaTime);

					FOR_EACH_IN_EARRAY(pSkeleton->eaRunAndGunBones, DynSkeletonRunAndGunBone, pRGBone)
					{
						if (pBone->pcTag == pRGBone->pcRGBoneName) {
							dynSkeletonCalcRunAndGunTransform(pSkeleton, pBone, NULL, NULL, pRGBone, fDeltaTime);
						}
					}
					FOR_EACH_END;
				}
				else if (pBone->pcTag == pcWaistName) {
					pBone->uiTransformFlags |= ednRot;
				}

				if (!pSkeleton->ragdollState.bRagdollOn &&
					!pSkeleton->bMissingAnimData		&&
					pBone == pSkeleton->pHipsNode)
				{
					pSkeleton->bUpdatedGroundRegHips = true;

					copyVec3(mainTransform.vPos,   hipsTransform.vPos  );
					copyQuat(mainTransform.qRot,   hipsTransform.qRot  );
					copyVec3(mainTransform.vScale, hipsTransform.vScale);

					// This is some code that fixes up the foot height. It has to be done here, so that it propagates to the child bones and skinning matrices.
					//handles static foot registration, the height adjustment factor is also used to modify an animations playback speed elsewhere
					if (!disableGroundRegHeightBump	&&
						!pSkeleton->bRider			&&
						pSkeleton->scaleCollection.fHipsHeightAdjustmentFactor != 1.0f)
					{
						Vec3 vPosPre, vPosPost;
						dynNodeGetLocalPosInline(pBone, vPosPre);
						scaleVec3(vPosPre, pSkeleton->scaleCollection.fHipsHeightAdjustmentFactor, vPosPost);
						dynNodeSetPosInline(pBone, vPosPost);
						pSkeleton->fHeightBump = vPosPost[1] - vPosPre[1];
					}
				}

				if (!pSkeleton->ragdollState.bRagdollOn &&
					pBone->pcTag == pcRootUpperbodyName)
				{
					bHasRootUpperBody = true;
					copyVec3(mainTransform.vPos,   rootUpperBodyTransform.vPos  );
					copyQuat(mainTransform.qRot,   rootUpperBodyTransform.qRot  );
					copyVec3(mainTransform.vScale, rootUpperBodyTransform.vScale);
				}

				// Overlay sequencers
				if (!pSkeleton->bMissingAnimData)
				{
					if (!pSkeleton->ragdollState.bRagdollOn)
					{
						if (!pBoneInfo->bIgnoreMasterOverlay &&
							pSkeleton->iOverlaySeqIndex > 0  &&
							pSkeleton->eaAGUpdater[pSkeleton->iOverlaySeqIndex]->fOverlayBlend > 0.0f)
						{
							DynTransform xTemp, xOverlay, xCurrent;

							dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[pSkeleton->iOverlaySeqIndex], &xTemp, pBone->pcTag, pBone->uiLODLevel, NULL, &pSkeleton->ragdollState, pSkeleton);
							
							dynTransformInterp(pSkeleton->eaAGUpdater[pSkeleton->iOverlaySeqIndex]->fOverlayBlend, &xIdentity, &xTemp, &xOverlay);
							dynNodeGetLocalTransformInline(pBone, &xCurrent);
							dynTransformMultiply(&xOverlay, &xCurrent, &xTemp);
							dynNodeSetFromTransformInline(pBone, &xTemp);
						}
						if (pBoneInfo->iOverlayIndex > 0 &&
							pSkeleton->eaAGUpdater[pBoneInfo->iOverlayAGUpdaterIndex]->fOverlayBlend > 0)
						{
							DynTransform xTemp, xOverlay, xCurrent;

							dynAnimGraphUpdaterUpdateBone(pSkeleton->eaAGUpdater[pBoneInfo->iOverlayAGUpdaterIndex], &xTemp, pBone->pcTag, pBone->uiLODLevel, NULL, &pSkeleton->ragdollState, pSkeleton);

							dynTransformInterp(pSkeleton->eaAGUpdater[pBoneInfo->iOverlayAGUpdaterIndex]->fOverlayBlend, &xIdentity, &xTemp, &xOverlay);
							dynNodeGetLocalTransformInline(pBone, &xCurrent);
							dynTransformMultiply(&xOverlay, &xCurrent, &xTemp);
							dynNodeSetFromTransformInline(pBone, &xTemp);
						}
					}
					else if(!pBone->uiRagdollPoisonBit &&
							pSkeleton->ragdollState.fBlend < 1.f)
					{
						//doing this here instead of through update graph bone or update movement bone so the xform lookups will
						//only be made once & it'll apply to all bones regardless of what sub/overlay sequencer the bone is on
						DynTransform xOverlay;
						bool bUsesPose = dynSkeletonRagdollUpdateBoneWithPose(pSkeleton, pBone->pcTag, &xOverlay, pxBaseTransform);
						if (bUsesPose)
						{
								DynTransform xCurrent, xResult;
								dynNodeGetLocalTransformInline(pBone, &xCurrent);
								dynTransformInterp(pSkeleton->ragdollState.fBlend, &xCurrent, &xOverlay, &xResult);
								dynNodeSetFromTransformInline(pBone, &xResult);
						}
					}
				}
			}
			else
			{
				Errorf("Unable to find basebone %s, should not be trying to animate it.", pBone->pcTag);
			}
		}

		if (pSkeleton->uiNumScaleModifiers > 0)
		{
			U32 uiIndex;
			Vec3 vScaleModifier;
			bool bScale = false;
			// Actually use parent's scale modifiers, they may have already been handled
			// Parent should only have some if we also have some.
			DynSkeleton *pEffSkeleton = pSkeleton->pGenesisSkeleton;

			unitVec3(vScaleModifier);
			for (uiIndex=0; uiIndex<pEffSkeleton->uiNumScaleModifiers; ++uiIndex)
			{
				U32 uiBoneNum;
				DynFxDrivenScaleModifier *const pModifier = &pEffSkeleton->aScaleModifiers[uiIndex];
				for (uiBoneNum=0; uiBoneNum < pModifier->uiNumBones; ++uiBoneNum)
				{
					if (pModifier->apcBoneName[uiBoneNum] == pBone->pcTag)
					{
						bScale = true;
						mulVecVec3(vScaleModifier, pModifier->vScale, vScaleModifier);
						// Prevent applying the same modifier twice on a frame, they are scaling
						//  "Base" and both the main skeleton and a child skeleton have abone
						//  named that.
						// Remove from lists to speed up the rest of the evaluations later
						// Or flag it if we can't remove it
						if (uiBoneNum == pModifier->uiNumBones-1)
						{
							pModifier->uiNumBones--;
							if (pModifier->uiNumBones == 0 && uiIndex == pEffSkeleton->uiNumScaleModifiers - 1)
								pEffSkeleton->uiNumScaleModifiers--;
						} else
							pModifier->apcBoneName[uiBoneNum] = "AlreadyAppliedThisFrame";
						break;
					}
				}
			}
			if (bScale)
			{
				Vec3 vScale;
				dynNodeGetLocalScale(pBone, vScale);
				mulVecVec3(vScale, vScaleModifier, vScale);
				dynNodeSetScaleInline(pBone, vScale);
			}
		}


		if (bRoot)
		{
			pBone->uiDirtyBits = 1;
			dynNodeGetWorldSpaceMat(pBone, mRoot, false);
			bRoot = false;
		}
		else
		{
			dynNodeCalcWorldSpaceOneNode(pBone);
		}

		dynAnimExpressionRun(pSkeleton, pBone, fDeltaTime);
		dynAnimExpressionUpdateData(pSkeleton, pBone);

		if (pBone->uiHasBouncer)
		{
			FOR_EACH_IN_EARRAY(pSkeleton->eaBouncerUpdaters, DynBouncerUpdater, pUpdater)
			{
				if (pUpdater->pInfo->pcBoneName == pBone->pcTag)
				{
					dynBouncerUpdaterUpdateBoneNew(pUpdater, pBone, fDeltaTime);
				}
			}
			FOR_EACH_END;
		}
	}

	dynDebugState.uiNumSkinnedBones += iBonesSkinned;

	PERFINFO_AUTO_STOP_CHECKED(__FUNCTION__);
}

static S32 disableGroundRegIK;
AUTO_CMD_INT(disableGroundRegIK, dynAnimDiableGroundRegIK);

const char *dynSkeletonGetGroundRegLimbName(DynGroundRegLimb *pLimb) {
	return SAFE_MEMBER2(pLimb,pEndEffectorNode,pcTag);
}
F32 dynSkeletonGetGroundRegLimbRelWeight(DynGroundRegLimb *pLimb) {
	return SAFE_MEMBER(pLimb,fPerFrame_RelWeight);
}
F32 dynSkeletonGetGroundRegLimbOffset(DynGroundRegLimb *pLimb) {
	return SAFE_MEMBER(pLimb,fPerFrame_DiffPosition);
}

static void dynSkeletonUpdateGroundRegistrationNew(DynSkeleton *pSkeleton, F32 fDeltaTime)
{
	// NOTE : this function adjust the overall body to best fit the limb end-effectors
    // and also tweaks the limbs when they're near the ground. This function does NOT
	// do the static leg length case which should have already been handled by the
	// time we get here

	const DynBaseSkeleton *pBaseSkeleton = dynSkeletonGetBaseSkeleton(pSkeleton);
	static const char **eaRuntimePartNoDefErrors = NULL;
	
	PERFINFO_AUTO_START_FUNC();

	//compute the dynamic ground registration blend
	{
		const DynAnimGraph *pCurrentGraph = eaSize(&pSkeleton->eaAGUpdater)?pSkeleton->eaAGUpdater[0]->pCurrentGraph:NULL;
		const DynAnimGraphNode *pCurrentGraphNode = pCurrentGraph?pSkeleton->eaAGUpdater[0]->nodes[0].pGraphNode:NULL;

		if (SAFE_MEMBER(pCurrentGraph, fDisableGroundRegTimeout) > 0.0f) {
			pSkeleton->fGroundRegBlendFactor -= fDeltaTime / pCurrentGraph->fDisableGroundRegTimeout;
			MAX1(pSkeleton->fGroundRegBlendFactor, 0.0f);
		} else if (SAFE_MEMBER(pCurrentGraphNode, fDisableGroundRegTimeout) > 0.0f) {
			pSkeleton->fGroundRegBlendFactor -= fDeltaTime / pCurrentGraphNode->fDisableGroundRegTimeout;
			MAX1(pSkeleton->fGroundRegBlendFactor, 0.0f);
		} else {
			pSkeleton->fGroundRegBlendFactor += 4.0f * fDeltaTime;
			MIN1(pSkeleton->fGroundRegBlendFactor, 1.0f);
		}

		if (SAFE_MEMBER(pCurrentGraph, fDisableUpperBodyGroundRegTimeout) > 0.0f) {
			pSkeleton->fGroundRegBlendFactorUpperBody -= fDeltaTime / pCurrentGraph->fDisableUpperBodyGroundRegTimeout;
			MAX1(pSkeleton->fGroundRegBlendFactorUpperBody, 0.0f);
		} else if (SAFE_MEMBER(pCurrentGraphNode, fDisableUpperBodyGroundRegTimeout) > 0.0f) {
			pSkeleton->fGroundRegBlendFactorUpperBody -= fDeltaTime / pCurrentGraphNode->fDisableUpperBodyGroundRegTimeout;
			MAX1(pSkeleton->fGroundRegBlendFactorUpperBody, 0.0f);
		} else {
			pSkeleton->fGroundRegBlendFactorUpperBody += 4.0f * fDeltaTime;
			MIN1(pSkeleton->fGroundRegBlendFactorUpperBody, 1.0f);
		}
	}

	//Begin dynamic ground registration height calculations
	if (!pBaseSkeleton						|| // required for math
		!pSkeleton->pRoot					|| // required for math
		!pSkeleton->pHipsNode				|| // required for math
		!pSkeleton->bUpdatedGroundRegHips	|| // LoD frame skipping
		pSkeleton->pParentSkeleton			|| // no tails & wings & riders & such
		pSkeleton->ragdollState.bRagdollOn	|| // no ragdoll physx rigs
		pSkeleton->bMissingAnimData			|| // no bad animation data
		pSkeleton->fGroundRegBlendFactor <= 0.0f || // no data driven disable
		!eaSize(&pSkeleton->eaGroundRegLimbs)) // required for math
	{
		FOR_EACH_IN_EARRAY(pSkeleton->eaGroundRegLimbs, DynGroundRegLimb, pLimb) {
			pLimb->fPerFrame_RelWeight = 0.f;
		} FOR_EACH_END;
	}
	else
	{
		Vec3 vBasePos;
		Vec3 vHipsPos;

		F32 yAniBase;
		F32 yHyperBase;

		F32 yBodyScale;
		F32 floorThreshold1 = pSkeleton->fGroundRegFloorDeltaNear;
		F32 floorThreshold2 = pSkeleton->fGroundRegFloorDeltaFar;
		F32 yRelSum = 0.f;
		F32 yfuh = 0.f; //fuh = fix up height
		F32 yMinAbsHeight;
		F32 yMinDifHeight;
		bool bSetMin = false;

		assert(pSkeleton->pRoot->pcTag == pcNodeNameBase);

		yBodyScale = (pSkeleton->scaleCollection.uiNumTransforms > 0) ? pSkeleton->scaleCollection.pLocalTransforms[0].vScale[1] : 1.0f;
		floorThreshold1 *= yBodyScale;
		floorThreshold2 *= yBodyScale;

		//find the bodies basic positioning
		if (pSkeleton->pRoot->pParent) {
			dynNodeCalcWorldSpaceOneNode(pSkeleton->pRoot);
		} // else we may have gotten here with a DynCost FX or other non-game-entity skeleton
		dynNodeGetWorldSpacePos(pSkeleton->pRoot, vBasePos);
		dynNodeGetLocalPosInline(pSkeleton->pHipsNode, vHipsPos);

		//determine the position of the base skeleton's base node (the floor plane)
		yAniBase = dynSkeletonGetBaseSkeletonTransformHeight(pSkeleton, &pSkeleton->scaleCollection, pBaseSkeleton, pSkeleton->pRoot, false, NULL, &yHyperBase);

		FOR_EACH_IN_EARRAY(pSkeleton->eaGroundRegLimbs, DynGroundRegLimb, pLimb) {
			if (dynGroundRegLimbIsSafe(pLimb))
			{
				F32 yAnimHeight;
				F32 yAbsHeight;
				F32 ySclHeight;

				//mark the limb as safe for the later iterations this frame
				pLimb->bPerFrame_IsSafe = true;

				//determine the distance between the base skeleton joint and the floor plane
				yAnimHeight = dynSkeletonGetBaseSkeletonTransformHeight(pSkeleton, &pSkeleton->scaleCollection, pBaseSkeleton, pLimb->pHeightFixupNode, pLimb->pStaticLimbData->bMinimizeHyperExtension, pLimb->pStaticLimbData->vHyperExtAxis, &pLimb->fPerFrame_HyperExtension) - yAniBase;
				yAbsHeight = fabs(yAnimHeight);

				//determine the relative weighting based on distance to the floor plane on an animated version of the base skeleton
				//then scale the relative weighting by any channel based ground registration enables/disables
				pLimb->fPerFrame_RelWeight = (yAbsHeight < floorThreshold1) ? 1.0 : (yAbsHeight-floorThreshold1 < floorThreshold2) ? max(1.0 - (yAbsHeight-floorThreshold1)/floorThreshold2, 0.0) : 0.0;
				if (pLimb->pStaticLimbData->bUpperBody) {
					pLimb->fPerFrame_RelWeight *= pSkeleton->fGroundRegBlendFactorUpperBody;
				}
				yRelSum += pLimb->fPerFrame_RelWeight;

				//determine the distance between the scaled skeleton joint and the floor plane
				ySclHeight = dynSkeletonGetTransformHeight(pLimb->pHeightFixupNode) - vBasePos[1];

				//determine the differences in joint position between the base & scaled skeleton
				pLimb->fPerFrame_DiffPosition = yAnimHeight - ySclHeight;

				//keep track of the relative position desires
				yfuh += pLimb->fPerFrame_RelWeight * pLimb->fPerFrame_DiffPosition;

				if (FALSE_THEN_SET(bSetMin) ||
					yAbsHeight <= yMinAbsHeight)
				{
					yMinAbsHeight = yAbsHeight;
					yMinDifHeight = pLimb->fPerFrame_DiffPosition;
				}

				//debug - blue = initial version as output by basic animation
				if (showGroundRegBaseSkeleton) {
					dynSkeletonDrawBoneChain(pSkeleton, pSkeleton->pRoot, pLimb->pEndEffectorNode, 0xFF8888FF);
				}
			}
			else
			{
				pLimb->bPerFrame_IsSafe = false;
			}
		} FOR_EACH_END;

		if (bSetMin)
		{
			//determine the average displacement
			if (yRelSum > 0.0) {
				yfuh /= yRelSum;
			} else {
				yfuh = yMinDifHeight;
			}
			yfuh *= pSkeleton->fGroundRegBlendFactor/yBodyScale;

			if (!disableGroundRegHeightBump)
			{
				Vec3 vNew;

				FOR_EACH_IN_EARRAY(pSkeleton->eaGroundRegLimbs, DynGroundRegLimb, pLimb) {
					if (pLimb->bPerFrame_IsSafe)
					{
						#define curHyper pLimb->fPerFrame_HyperExtension
						#define hyperSmall 0.05f
						#define hyperLarge 0.15f

						//modify IK-weights based on hyper-extension
						if (!disableGroundRegHyperExPrevention &&
							pLimb->pStaticLimbData->bMinimizeHyperExtension)
						{
							pLimb->fPerFrame_RelWeight *= (curHyper < hyperSmall) ? 0.0 : (curHyper < hyperLarge) ? (curHyper - hyperSmall)/(hyperLarge - hyperSmall) : 1.0;
						}
						pLimb->fPerFrame_RelWeight *= pSkeleton->fGroundRegBlendFactor;

						//target positions will be based on the parent nodes since we're using a 3 joint analytic IK-solver
						//during which the end effector nodes will be rotated to have their initial alignment
						dynNodeGetWorldSpaceTransform(pLimb->pEndEffectorNode->pParent, &pLimb->xPerFrame_TargetPosition);
						pLimb->xPerFrame_TargetPosition.vPos[1] += pLimb->fPerFrame_RelWeight * pLimb->fPerFrame_DiffPosition;

						//debug
						if (showGroundRegBaseSkeleton) {
							wl_state.drawAxesFromTransform_func(&pLimb->xPerFrame_TargetPosition, .2f);
						}

						#undef curHyper
						#undef hyperSmall
						#undef hyperLarge
					}
				} FOR_EACH_END;

				//apply the closest distance to ground offset to the scaled character's hips
				vNew[0] = vHipsPos[0];
				vNew[1] = vHipsPos[1] + yfuh;
				vNew[2] = vHipsPos[2];
				dynNodeSetPosInline(pSkeleton->pHipsNode, vNew);

				//cache some values for displaying debug data
				pSkeleton->fHeightBump += yfuh;

				if (!disableGroundRegIK)
				{
					FOR_EACH_IN_EARRAY(pSkeleton->eaGroundRegLimbs, DynGroundRegLimb, pLimb) {
						if (pLimb->bPerFrame_IsSafe &&
							pLimb->pEndEffectorNode->pParent &&
							pLimb->pEndEffectorNode->pParent->uiCriticalBone &&
							pLimb->fPerFrame_RelWeight)
						{
							//tweak the limb's posture
							dynSkeletonUpdateBoneChainTransforms(pSkeleton, pSkeleton->pRoot, pLimb->pEndEffectorNode);
							dynAnimFixupArm(pLimb->pEndEffectorNode, &pLimb->xPerFrame_TargetPosition, pLimb->fPerFrame_RelWeight, true, false, false, true);

							//debug - pink = version after dynamic ground registration
							if (showGroundRegBaseSkeleton) {
								dynSkeletonDrawBoneChain(pSkeleton, pSkeleton->pRoot, pLimb->pEndEffectorNode, 0xFFFF8888);
							}
						}
					} FOR_EACH_END;
				}
			}
		}
	}
	//End dynamic ground registration height calculation

	PERFINFO_AUTO_STOP_CHECKED(__FUNCTION__);
}

static void dynSkeletonApplyMatchJointOffset(DynSkeleton *pSkeleton, DynNode *pBone)
{
	//note that this function doesn't currently support override alls or overlays

	if (!pSkeleton->bMissingAnimData &&
		!pSkeleton->bSnapshot
		&&
		(	!pSkeleton->ragdollState.bRagdollOn ||
			pSkeleton->ragdollState.fBlend < 1.f)
		&&
		(	eaSize(&pSkeleton->eaAnimGraphUpdaterMatchJoints) ||
			eaSize(&pSkeleton->eaSkeletalMovementMatchJoints)))
	{
		DynAnimGraphUpdater* pUseUpdater;
		bool bNeedsApply = false;
		Vec3 vAnimGraphOffset;
		Vec3 vSkelMoveOffset;
		Vec3 vApplyOffset;
		Vec3 vPosLS;

		zeroVec3(vAnimGraphOffset);
		zeroVec3(vSkelMoveOffset);

		if (eaSize(&pSkeleton->eaAGUpdater) > 0) {
			pUseUpdater = pSkeleton->eaAGUpdater[0];
		} else {
			pUseUpdater = NULL;
		}

		FOR_EACH_IN_EARRAY(pSkeleton->eaAnimGraphUpdaterMatchJoints, DynJointBlend, pMatchJoint) {
			if (pBone->pcTag == pMatchJoint->pcName) {
				dynAnimGraphUpdaterCalcMatchJointOffset(pSkeleton, pUseUpdater, pMatchJoint, vAnimGraphOffset);
				bNeedsApply = true;
				break;
			}
		} FOR_EACH_END;

		if (eaSize(&pSkeleton->eaAGUpdater) > 1) {
			pUseUpdater = pSkeleton->eaAGUpdater[1];
		} else {
			pUseUpdater = NULL;
		}

		FOR_EACH_IN_EARRAY(pSkeleton->eaSkeletalMovementMatchJoints, DynJointBlend, pMatchJoint) {
			if (pBone->pcTag == pMatchJoint->pcName) {
				if (!gConf.bUseMovementGraphs) dynSkeletonMovementCalcMatchJointOffset(pSkeleton, pMatchJoint, vSkelMoveOffset);
				else                           dynAnimGraphUpdaterCalcMatchJointOffset(pSkeleton, pUseUpdater, pMatchJoint, vSkelMoveOffset);
				bNeedsApply = true;
				break;
			}
		} FOR_EACH_END;

		if (bNeedsApply)
		{
			DynAnimBoneInfo* pBoneInfo = NULL;
			if (pBone->uiCriticalBone) {
				pBoneInfo = &pSkeleton->pAnimBoneInfos[pBone->iCriticalBoneIndex];
			}

			if (pSkeleton->bMovementBlending &&
				pBoneInfo)
			{
				if (pBoneInfo->bMovement &&
					pSkeleton->fLowerBodyBlendFactor < 1.0f) 
				{
					// movement bone, but lower body blend factor has dropped (probably not moving)
					if (pSkeleton->fLowerBodyBlendFactor > 0.0f) {
						lerpVec3(vSkelMoveOffset, pSkeleton->fLowerBodyBlendFactor, vAnimGraphOffset, vApplyOffset);
					} else {
						copyVec3(vAnimGraphOffset, vApplyOffset);
					}
				}
				else if(!pBoneInfo->bMovement &&
						pSkeleton->fMovementSystemOverrideFactor > 0.0f) //&&
					//iSeqIndex == 0) know this must be true since only the default sequencer can create match joints
				{
					// default sequencer bone, but movement is overriding it
					if (pSkeleton->fMovementSystemOverrideFactor < 1.0f) {
						lerpVec3(vSkelMoveOffset, pSkeleton->fMovementSystemOverrideFactor, vAnimGraphOffset, vApplyOffset);
					} else {
						copyVec3(vSkelMoveOffset, vApplyOffset);
					}
				}
				else
				{
					// normal, torso pointing skeleton
					if (pBoneInfo->bMovement) {
						copyVec3(vSkelMoveOffset, vApplyOffset);
					} else {
						copyVec3(vAnimGraphOffset, vApplyOffset);
					}
				}
			}
			else
			{
				// non torso pointing character (single agupdater + movement updater)
				if (pSkeleton->fMovementSystemOverrideFactor == 1.0f) {
					copyVec3(vSkelMoveOffset, vApplyOffset);
				} else if (pSkeleton->fMovementSystemOverrideFactor == 0.0f) {
					copyVec3(vAnimGraphOffset, vApplyOffset);
				} else {
					lerpVec3(vSkelMoveOffset, pSkeleton->fMovementSystemOverrideFactor, vAnimGraphOffset, vApplyOffset);
				}
			}

			if(pSkeleton->ragdollState.bRagdollOn) {
				scaleVec3(vApplyOffset, 1.f-pSkeleton->ragdollState.fBlend, vApplyOffset);
			}

			dynNodeGetLocalPos(pBone, vPosLS);
			addVec3(vPosLS, vApplyOffset, vPosLS);
			dynNodeSetPos(pBone, vPosLS);
		}
	}
}

static void dynSkeletonUpdateSkinningNew(DynSkeleton *pSkeleton, F32 fDeltaTime)
{
	DynNode*		nodeStack[100];
	S32				stackPos;
	S32				first;
	Mat4			mRoot;
	bool			bRoot;
	int				iSkinningBoneIndex;
	Quat			qRootRotInv;
	Vec3			vRootPos;
	bool			bSetExtents = false;

	PERFINFO_AUTO_START_FUNC();

	stackPos = 0;
	first = 1;
	bRoot = true;
	iSkinningBoneIndex = 0;
	nodeStack[stackPos++] = pSkeleton->pRoot;

	if (!pSkeleton->bDontUpdateExtents)
	{
		Quat qTemp;
		dynNodeGetWorldSpacePos(pSkeleton->pGenesisSkeleton->pRoot, vRootPos);
		dynNodeGetWorldSpaceRot(pSkeleton->pGenesisSkeleton->pRoot, qTemp);
		quatInverse(qTemp, qRootRotInv);

		setVec3same(pSkeleton->vCurrentExtentsMax, -FLT_MAX);
		setVec3same(pSkeleton->vCurrentExtentsMin, FLT_MAX);
	}

	while(stackPos){
		DynNode*	pBone = nodeStack[--stackPos];
		assert(pBone);

#if PLATFORM_CONSOLE
		if (pSkeleton->pDrawSkel && pSkeleton->pDrawSkel->pCurrentSkinningMatSet)
			PREFETCH(pSkeleton->pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[iSkinningBoneIndex+1]);
#endif

		if (
			!pBone->uiCriticalBone ||
			!pBone->uiSkeletonBone ||
			!pBone->pcTag ||
			pBone->uiLocked || 
			pBone->uiMaxLODLevelBelow + MAX_LOD_UPDATE_DEPTH < pSkeleton->uiLODLevel
			)
		{
			if (pBone->uiCriticalBone)
			{
				dynSkeletonApplyMatchJointOffset(pSkeleton,pBone);
				dynNodeCalcWorldSpaceOneNode(pBone);
				assert(pBone->pcTag);
				if (pSkeleton->pDrawSkel
					&& pBone->iSkinningBoneIndex >= 0)
				{
					Vec3 vRotPos, vPos;
					iSkinningBoneIndex = pBone->iSkinningBoneIndex;
					++dynDebugState.uiNumSkinnedBones;
					if (pSkeleton->pDrawSkel->pCurrentSkinningMatSet) //otherwise the skeleton is not visible & transforms were not force updated, so there are no skinning matrices
						dynNodeCreateSkinningMat(pSkeleton->pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[pBone->iSkinningBoneIndex], pBone, pSkeleton->pAnimBoneInfos[pBone->iCriticalBoneIndex].vBaseOffset, mRoot);
					if (!pSkeleton->bDontUpdateExtents)
					{
						Vec3 vScale;
						subVec3(dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vRootPos, vPos);
						quatRotateVec3Inline(qRootRotInv, vPos, vRotPos);
						dynNodeGetWorldSpaceScale(pBone, vScale);
						vec3RunningMinMaxWithRadius(vRotPos,
													pSkeleton->vCurrentExtentsMin,
													pSkeleton->vCurrentExtentsMax,
													dynDrawSkeletonGetNodeRadius(pSkeleton->pDrawSkel, pBone->pcTag) * vec3MaxComponent(vScale));
						bSetExtents = true;
					}
				}
				if (pBone->pCriticalChild 
					&& stackPos < ARRAY_SIZE(nodeStack))
				{
					nodeStack[stackPos++] = pBone->pCriticalChild;
				}
			}

			if(first)
			{
				first = 0;
			}
			else
			{
				if(	pBone->pCriticalSibling 
					&& stackPos < ARRAY_SIZE(nodeStack))
				{
					nodeStack[stackPos++] = pBone->pCriticalSibling;
				}
			}

			continue;
		}

		if(	pBone->pCriticalChild
			&& stackPos < ARRAY_SIZE(nodeStack))
		{
			nodeStack[stackPos++] = pBone->pCriticalChild;
		}

		if(first)
		{
			first = 0;
		}
		else
		{
			if(	pBone->pCriticalSibling
				&& stackPos < ARRAY_SIZE(nodeStack))
			{
				nodeStack[stackPos++] = pBone->pCriticalSibling;
			}
		}

		#if PLATFORM_CONSOLE
		if (stackPos > 0)
		{
			PREFETCH(nodeStack[stackPos - 1]);
		}
		#endif

		#if 0
		pPrefetch = nodeStack[stackPos - 1];
		#endif

		if (bRoot)
		{
			pBone->uiDirtyBits = 1;
			dynNodeGetWorldSpaceMat(pBone, mRoot, false);
			bRoot = false;
		}
		else
		{
			dynSkeletonApplyMatchJointOffset(pSkeleton,pBone);
			dynNodeCalcWorldSpaceOneNode(pBone);
		}

		if (pSkeleton->pDrawSkel && pBone->iSkinningBoneIndex >= 0 && pBone->uiSkinningBone)
		{
			Vec3 vRotPos, vPos;
			iSkinningBoneIndex = pBone->iSkinningBoneIndex;
			++dynDebugState.uiNumSkinnedBones;
			assert(pBone->iCriticalBoneIndex >= 0);
			dynNodeCreateSkinningMat(pSkeleton->pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[pBone->iSkinningBoneIndex], pBone, pSkeleton->pAnimBoneInfos[pBone->iCriticalBoneIndex].vBaseOffset, mRoot);
			if (!pSkeleton->bDontUpdateExtents)
			{
				Vec3 vScale;
				subVec3(dynNodeAssumeCleanGetWorldSpacePosRefInline(pBone), vRootPos, vPos);
				quatRotateVec3Inline(qRootRotInv, vPos, vRotPos);
				dynNodeGetWorldSpaceScale(pBone, vScale);
				vec3RunningMinMaxWithRadius(vRotPos,
											pSkeleton->vCurrentExtentsMin,
											pSkeleton->vCurrentExtentsMax,
											dynDrawSkeletonGetNodeRadius(pSkeleton->pDrawSkel, pBone->pcTag) * vec3MaxComponent(vScale));
				bSetExtents = true;
			}
		}
	}

	if (!pSkeleton->bDontUpdateExtents)
	{
		DynSkeleton *pActiveSkeleton = pSkeleton;
		DynSkeleton *pParentSkeleton = pSkeleton->pParentSkeleton;

		bSetExtents |= dynDrawSkeletonCalculateNonSkinnedExtents(pSkeleton->pDrawSkel, true);

		if (!bSetExtents) {
			zeroVec3(pSkeleton->vCurrentExtentsMax);
			zeroVec3(pSkeleton->vCurrentExtentsMin);	
		}

		copyVec3(pSkeleton->vCurrentExtentsMax, pSkeleton->vCurrentGroupExtentsMax);
		copyVec3(pSkeleton->vCurrentExtentsMin, pSkeleton->vCurrentGroupExtentsMin);
		
		while (pParentSkeleton && !pParentSkeleton->bDontUpdateExtents)
		{
			MAXVEC3(pParentSkeleton->vCurrentGroupExtentsMax, pActiveSkeleton->vCurrentGroupExtentsMax, pParentSkeleton->vCurrentGroupExtentsMax);
			MINVEC3(pParentSkeleton->vCurrentGroupExtentsMin, pActiveSkeleton->vCurrentGroupExtentsMin, pParentSkeleton->vCurrentGroupExtentsMin);
			pActiveSkeleton = pParentSkeleton;
			pParentSkeleton = pParentSkeleton->pParentSkeleton;
		}
	}

	PERFINFO_AUTO_STOP_CHECKED(__FUNCTION__);
}


static void dynSkeletonUpdatePostTransforms(DynSkeleton* pSkeleton, F32 fDeltaTime, WorldPerfFrameCounts *perf_info)
{
	Vec3 vCurrentPos;
    
    const U32 uiNumBitFields = pSkeleton->uiNumBitFields;
    DynBitField *const aBitFieldStream = pSkeleton->aBitFieldStream;

	pSkeleton->pDrawSkel->uiLODLevel = pSkeleton->uiLODLevel;

	// Update the physical presence of this skeleton in the FX physics sim (for kicking rocks around)
	dynSkeletonQueuePhysicsUpdate(pSkeleton);

	// Update the test ragdoll for collision detection
	dynSkeletonQueueTestRagdollUpdate(pSkeleton);

	// Limit wind forces to high LOD skeletons
	if (pSkeleton->uiLODLevel <= worldLibGetLODSettings()->uiWindLODLevel && !pSkeleton->bFlying)
		dynSkeletonQueueWindUpdate(pSkeleton);

	dynNodeGetWorldSpacePos(pSkeleton->pRoot, vCurrentPos);

	if (perf_info)
		wlPerfStartClothBudget(perf_info);
	// Update any cloth attached to the skeleton
	PERFINFO_AUTO_START("Update Cloth", 1);
	if (pSkeleton->bVisible && !dynDebugState.bNoAnimation)
	{
		Vec3 vDist;

		subVec3(vCurrentPos, pSkeleton->vOldPos, vDist);
		if (!vec3IsZero(vDist)) {
			pSkeleton->bClothMoving = true;
		} else {
			pSkeleton->bClothMoving = false;
		}

		dynDrawSkeletonUpdateCloth(	pSkeleton->pDrawSkel, fDeltaTime, vDist,
									pSkeleton->pGenesisSkeleton->bClothMoving,
									pSkeleton->bRider || pSkeleton->bRiderChild);
	}
	PERFINFO_AUTO_STOP();
	if (perf_info)
		wlPerfEndClothBudget(perf_info);

	// For alpha fade out
	dynDrawSkeletonUpdate(pSkeleton->pDrawSkel, fDeltaTime);

	// Record old position
	copyVec3(vCurrentPos, pSkeleton->vOldPos);

	// Update children
	FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton)
	{
		// First, make a copy of the bit field stream
		DynBitField* pNewBitFieldStream = NULL;
		if (uiNumBitFields > 0)
		{
			pNewBitFieldStream = _alloca(sizeof(DynBitField) * uiNumBitFields);
			memcpy(pNewBitFieldStream, aBitFieldStream, sizeof(DynBitField) * uiNumBitFields);
		}

		if (!gConf.bNewAnimationSystem)
		{
			if(dynSkeletonUpdatePreTransforms(pChildSkeleton, fDeltaTime, pNewBitFieldStream, uiNumBitFields))
			{
				dynSkeletonUpdateTransforms(pChildSkeleton, fDeltaTime);
				if (!pChildSkeleton->bSnapshot)
					dynSeqCalcIK(pChildSkeleton, true);
			}
		}
		else
		{
			if(dynSkeletonUpdatePreTransformsNew(pChildSkeleton, fDeltaTime, pNewBitFieldStream, uiNumBitFields))
			{
				if (!pChildSkeleton->bSnapshot)
				{
					dynSkeletonUpdateTransformsNew(pChildSkeleton, fDeltaTime);
					dynSkeletonUpdateGroundRegistrationNew(pChildSkeleton, fDeltaTime);
					if (!pChildSkeleton->ragdollState.bRagdollOn)
						dynSeqCalcIK(pChildSkeleton, false);
					if (pChildSkeleton->bHasStrands)
						dynSkeletonUpdateStrands(pChildSkeleton, fDeltaTime);
				}
				dynSkeletonUpdateSkinningNew(pChildSkeleton, fDeltaTime);
			}
		}
		dynSkeletonUpdatePostTransforms(pChildSkeleton, fDeltaTime, perf_info);
	}
	FOR_EACH_END;

	if (!pSkeleton->pParentSkeleton)
	{
		if (!pSkeleton->bDontUpdateExtents) {
			dynSkeletonSetExtentsNode(pSkeleton);
		} else {
			dynDrawSkeletonCalculateNonSkinnedExtents(pSkeleton->pDrawSkel, false);
		}
	}
}

static void dynSkeletonUpdateLastPassIK(DynSkeleton *pSkeleton)
{
	//this does not recompute the skeletons extents

	if (pSkeleton->bHasLastPassIK) {
		dynSeqCalcIK(pSkeleton, true);
	}

	FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton)
	{
		dynSkeletonUpdateLastPassIK(pChildSkeleton);
	}
	FOR_EACH_END;
}

static bool danimSentMissingDataError = false;
static void dynSkeletonCheckForAnimData(DynSkeleton *pSkeleton)
{	
	pSkeleton->bMissingAnimData = false;

	//check if there are any moves
	if (!gConf.bUseMovementGraphs)
	{
		if (eaSize(&pSkeleton->movement.eaBlocks) == 0) {
			pSkeleton->bMissingAnimData = true;
		} else {
			FOR_EACH_IN_CONST_EARRAY(pSkeleton->movement.eaBlocks, DynMovementBlock, pMovementBlock) {
				//check for transition moves & animation data
				if (pMovementBlock->inTransition) {
					if (!SAFE_MEMBER(pMovementBlock,pMoveSeqTransition)) {
						pSkeleton->bMissingAnimData = true;
						break;
					} else if (!pMovementBlock->pMoveSeqTransition->dynMoveAnimTrack.pAnimTrackHeader) {
						pSkeleton->bMissingAnimData = true;
						break;
					}
				}
				//check for regular moves & animation data
				if (!SAFE_MEMBER(pMovementBlock,pMoveSeqCycle)) {
					pSkeleton->bMissingAnimData = true;
					break;
				} else if (!pMovementBlock->pMoveSeqCycle->dynMoveAnimTrack.pAnimTrackHeader) {
					pSkeleton->bMissingAnimData = true;
					break;
				} else if (pMovementBlock->fBlendFactor >= 1.f) {
					break;
				}
			} FOR_EACH_END;
		}
	}

	//check if there are any graphs
	if (eaSize(&pSkeleton->eaAGUpdater) == 0) {
		pSkeleton->bMissingAnimData = true;
	} else {
		FOR_EACH_IN_CONST_EARRAY(pSkeleton->eaAGUpdater, DynAnimGraphUpdater, pAGUpdater) {
			U32 i;
			for (i = 0; i < MAX_UPDATER_NODES; i++) {
				//check for move & animation frame data
				if (!pAGUpdater->nodes[i].pMoveSeq) {
					pSkeleton->bMissingAnimData = true;
					break;
				} else if (!pAGUpdater->nodes[i].pMoveSeq->dynMoveAnimTrack.pAnimTrackHeader) {
					pSkeleton->bMissingAnimData = true;
					break;
				} else if (pAGUpdater->nodes[i].fBlendFactor >= 1.f) {
					break;
				}
			}
		} FOR_EACH_END;
	}

	if (pSkeleton->bMissingAnimData && FALSE_THEN_SET(danimSentMissingDataError)) {
		Errorf("Caught skeleton without animation data in %s, it probably failed to load!\n", __FUNCTION__);
	}
}

void dynSkeletonUpdate(DynSkeleton* pSkeleton, F32 fDeltaTime, WorldPerfFrameCounts *perf_info)
{	
	const WLCostume	*pCostume	= GET_REF(pSkeleton->hCostume);
	const SkelInfo	*pSkelInfo	= pCostume ? GET_REF(pCostume->hSkelInfo) : NULL;

	glrLog(pSkeleton->glr, "[dyn.AGU] %s BEGIN.", __FUNCTION__);

	if (pSkelInfo)
	{
		if (!gConf.bNewAnimationSystem)
		{
			if(dynSkeletonUpdatePreTransforms(pSkeleton, fDeltaTime, NULL, 0))
			{
				dynSkeletonUpdateTransforms(pSkeleton, fDeltaTime);
				if (!pSkeleton->bSnapshot)
					dynSeqCalcIK(pSkeleton, true);
			}
		}
		else
		{
			if (!bInDynSkeletonUpdateAll)
			{
				dynAnimCheckDataReload();
			}

			if (!pSkeleton->bAnimDisabled)
			{
				if(dynSkeletonUpdatePreTransformsNew(pSkeleton, fDeltaTime, NULL, 0))
				{
					dynSkeletonCheckForAnimData(pSkeleton);
					if (!pSkeleton->bSnapshot)
					{
						dynSkeletonUpdateTransformsNew(pSkeleton, fDeltaTime);
						dynSkeletonUpdateGroundRegistrationNew(pSkeleton, fDeltaTime);
						if (!pSkeleton->ragdollState.bRagdollOn)
							dynSeqCalcIK(pSkeleton, false);
						if (pSkeleton->bHasStrands)
							dynSkeletonUpdateStrands(pSkeleton, fDeltaTime);
					}
					dynSkeletonUpdateSkinningNew(pSkeleton, fDeltaTime);
				}
			}
		}

		//both animation engines
	    dynSkeletonUpdatePostTransforms(pSkeleton, fDeltaTime, perf_info);
		if (!pSkeleton->ragdollState.bRagdollOn && !pSkeleton->bSnapshot) {
			dynSkeletonUpdateLastPassIK(pSkeleton);
		}
			
	}
	else
	{
		Alertf("Error: attempted to update skeleton with %s%s\n",
			pCostume?"no SkelInfo on costume ":"no Costume",
			pCostume?pCostume->pcName:"");
	}

	glrLog(pSkeleton->glr, "[dyn.AGU] %s END.", __FUNCTION__);
}

static int compareSkeletonDistance(const DynSkeleton **a, const DynSkeleton **b)
{
	if ((*a)->fCurrentCamDistanceSquared < (*b)->fCurrentCamDistanceSquared)
		return -1;
	return !((*a)->fCurrentCamDistanceSquared == (*b)->fCurrentCamDistanceSquared);
}

static void dynSkeletonAccumulateWorldPerfCounts(AnimUpdateOutputCommand* pCommand)
{
	world_perf_info.pCounts->time_cloth	+= pCommand->perf_info.time_cloth;
	world_perf_info.pCounts->time_skel	+= pCommand->perf_info.time_skel;
}

void dynSkeletonQueuePerfResults(DynSkeleton* pSkeleton, WorldPerfFrameCounts * perf_info)
{
	if (!pSkeleton->pGenesisSkeleton->bUnmanaged) {
		AnimUpdateOutputCommand* pCommand = TSMP_ALLOC(AnimUpdateOutputCommand);
		pCommand->pSubFunc = dynSkeletonAccumulateWorldPerfCounts;
		pCommand->perf_info = *perf_info;

		//catch bad animation timers while we still have access to skeleton data
		devassert(	perf_info->time_skel  >= 0 &&
					perf_info->time_cloth >= 0	);

		if (!mwtQueueOutput(pAnimThreadManager, pCommand))
		{
			TSMP_FREE(AnimUpdateOutputCommand, pCommand);
		}
	}
}

void dynSkeletonThreadedUpdate(DynSkeleton* pSkeleton)
{
	WorldPerfFrameCounts perf_info = { 0 };
	wlPerfStartSkelBudget(&perf_info);
	assert(bInDynSkeletonUpdateAll);
	dynSkeletonUpdate(pSkeleton, fSkelUpdateDeltaTime, &perf_info);
	wlPerfEndSkelBudget(&perf_info);

	dynSkeletonQueuePerfResults(pSkeleton, &perf_info);
}

static void dynSkeletonProcessOutputFunc(AnimUpdateOutputCommand* pCommand)
{
	pCommand->pSubFunc(pCommand);
	TSMP_FREE(AnimUpdateOutputCommand, pCommand);
}


static void dynSkeletonProcessAnimationFx(AnimUpdateOutputCommand* pCommand)
{
	DynAddFxParams params = {0};
	params.eSource = eDynFxSource_Animation;

	dynSkeletonLockFX(); {
		dynAddFx(pCommand->call_fx.pFxManager, pCommand->call_fx.pcFx, &params);
	} dynSkeletonUnlockFX();
}

static void dynSkeletonProcessAnimReactTrigger(AnimUpdateOutputCommand* pCommand)
{
	if (dynDebugState.bDrawImpactTriggers)
		dynFxLogTriggerImpact(pCommand->react_trigger.vBonePos, pCommand->react_trigger.vImpactDir);
	if (wl_state.danim_hit_react_impact_func)
	{
		wl_state.danim_hit_react_impact_func(	pCommand->react_trigger.pSkeleton->uiEntRef, 
												pCommand->react_trigger.uid,
												pCommand->react_trigger.vBonePos, 
												pCommand->react_trigger.vImpactDir);
	}
}

void dynSkeletonQueueAnimationFx(DynSkeleton* pSkeleton, DynFxManager* pFxManager, const char* pcFxName)
{
	if (!pSkeleton->pGenesisSkeleton->bUnmanaged) {
		AnimUpdateOutputCommand* pCommand = TSMP_ALLOC(AnimUpdateOutputCommand);
		pCommand->pSubFunc = dynSkeletonProcessAnimationFx;
		pCommand->call_fx.pFxManager = pFxManager;
		pCommand->call_fx.pcFx = pcFxName;
		if (!mwtQueueOutput(pAnimThreadManager, pCommand))
		{
			TSMP_FREE(AnimUpdateOutputCommand, pCommand);
		}
	}
}

void dynSkeletonQueueAnimReactTrigger(Vec3 vBonePos, Vec3 vImpactDir, DynSkeleton* pSkeleton, U32 uid)
{
	if (!pSkeleton->pGenesisSkeleton->bUnmanaged) {
		AnimUpdateOutputCommand* pCommand = TSMP_ALLOC(AnimUpdateOutputCommand);
		pCommand->pSubFunc = dynSkeletonProcessAnimReactTrigger;
		copyVec3(vBonePos, pCommand->react_trigger.vBonePos);
		copyVec3(vImpactDir, pCommand->react_trigger.vImpactDir);
		pCommand->react_trigger.pSkeleton = pSkeleton;
		pCommand->react_trigger.uid = uid;
		if (!mwtQueueOutput(pAnimThreadManager, pCommand))
		{
			TSMP_FREE(AnimUpdateOutputCommand, pCommand);
		}
	}
}

static void dynSkeletonProcessPhysicsUpdate(AnimUpdateOutputCommand* pCommand)
{
	QueuedCommand_dpoMovePositionRotation(pCommand->physics_update.pDPO, pCommand->physics_update.vPos, pCommand->physics_update.qRot);
}

static void dynSkeletonQueuePhysicsUpdate(DynSkeleton* pSkeleton)
{
	if (!pSkeleton->pGenesisSkeleton->bUnmanaged &&
		dynAnimPhysicsIsFullSimulation(pSkeleton))
	{
		AnimUpdateOutputCommand* pCommand = TSMP_ALLOC(AnimUpdateOutputCommand);
		dynNodeGetWorldSpacePos(pSkeleton->pRoot, pCommand->physics_update.vPos);
		dynNodeGetWorldSpaceRot(pSkeleton->pRoot, pCommand->physics_update.qRot);
		pCommand->physics_update.pDPO = pSkeleton->pDPO;
		pCommand->pSubFunc = dynSkeletonProcessPhysicsUpdate;

		if (!mwtQueueOutput(pAnimThreadManager, pCommand))
		{
			TSMP_FREE(AnimUpdateOutputCommand, pCommand);
		}
	}
}

static void dynSkeletonProcessTestRagdollUpdate(AnimUpdateOutputCommand *pCommand)
{
	QueuedCommand_dpoSetMat(pCommand->testragdoll_update.pDPO, pCommand->testragdoll_update.mat);
}

static void dynSkeletonQueueTestRagdollUpdate(DynSkeleton* pSkeleton)
{
	if (gConf.bNewAnimationSystem &&
		!pSkeleton->pGenesisSkeleton->bUnmanaged &&
		pSkeleton->bHasClientSideTestRagdoll &&
		dynAnimPhysicsIsFullSimulation(pSkeleton))
	{
		FOR_EACH_IN_EARRAY(pSkeleton->eaClientSideRagdollBodies, DynPhysicsObject, pRagdollBody)
		{
			if (pRagdollBody->body.pBody->uiCollisionTester)
			{
				const DynNode *pNode = dynSkeletonFindNode(pSkeleton, pRagdollBody->body.pBody->pcBone);
				AnimUpdateOutputCommand *pCommand = TSMP_ALLOC(AnimUpdateOutputCommand);
				dynNodeGetWorldSpaceMat(pNode, pCommand->testragdoll_update.mat, true);
				pCommand->testragdoll_update.pDPO = pRagdollBody;
				pCommand->pSubFunc = dynSkeletonProcessTestRagdollUpdate;

				if (!mwtQueueOutput(pAnimThreadManager, pCommand))
				{
					TSMP_FREE(AnimUpdateOutputCommand, pCommand);
				}
			}
		}
		FOR_EACH_END;
	}
}

static void dynSkeletonProcessWindUpdate(AnimUpdateOutputCommand* pCommand)
{
	dynWindQueueMovingObjectForce(pCommand->wind_update.vPos, pCommand->wind_update.vVel, pCommand->wind_update.fRadius, true);
}

static void dynSkeletonQueueWindUpdate(DynSkeleton* pSkeleton)
{
	if (!pSkeleton->pGenesisSkeleton->bUnmanaged) {
		AnimUpdateOutputCommand* pCommand;
		//dont bother if we are going too slow anyway
		if (nearSameF32(pSkeleton->fCurrentSpeedXZ, 0)) return;

		pCommand = TSMP_ALLOC(AnimUpdateOutputCommand);
		dynNodeGetWorldSpacePos(pSkeleton->pRoot, pCommand->wind_update.vPos);
		scaleVec3(pSkeleton->vOldMovementDir, pSkeleton->fCurrentSpeedXZ, pCommand->wind_update.vVel);
		pCommand->wind_update.fRadius = MAX(pSkeleton->vCurrentGroupExtentsMax[0] - pSkeleton->vCurrentGroupExtentsMin[0], pSkeleton->vCurrentGroupExtentsMax[2] - pSkeleton->vCurrentGroupExtentsMin[2]);
		pCommand->pSubFunc = dynSkeletonProcessWindUpdate;

		if (!mwtQueueOutput(pAnimThreadManager, pCommand))
		{
			TSMP_FREE(AnimUpdateOutputCommand, pCommand);
		}
	}
}

static void dynSkeletonProcessBodysockUpdate(AnimUpdateOutputCommand* pCommand)
{
	dynDrawSkeletonSetupBodysockTexture(pCommand->bodysock_update.pMyCostume);
}

void dynSkeletonQueueBodysockUpdate(DynSkeleton* pSkeleton, WLCostume* pMyCostume)
{
	if (!pSkeleton->pGenesisSkeleton->bUnmanaged) {
		AnimUpdateOutputCommand* pCommand;
		pCommand = TSMP_ALLOC(AnimUpdateOutputCommand);
		pCommand->pSubFunc = dynSkeletonProcessBodysockUpdate;
		pCommand->bodysock_update.pMyCostume = pMyCostume;

		if (!mwtQueueOutput(pAnimThreadManager, pCommand))
		{
			TSMP_FREE(AnimUpdateOutputCommand, pCommand);
		}
	}
}


void dynAnimThreadManagerInit(void)
{
	int* piThreadIndices = NULL;
	int iNumThreads;

	int* piThreadPriorities;

#if _XBOX
	int aiXboxProcessorIndices[5] =
	{
		THREADINDEX_ANIMTHREAD_0,
		THREADINDEX_ANIMTHREAD_1,
		THREADINDEX_ANIMTHREAD_2,
		THREADINDEX_ANIMTHREAD_3,
		THREADINDEX_ANIMTHREAD_4,
	};
	piThreadIndices = aiXboxProcessorIndices;

	if (dynDebugState.bAnimThreadsDefaultChanged)
		iNumThreads = MIN(dynDebugState.iNumAnimThreads, 5);
	else
		iNumThreads = 2;
#else
	if (dynDebugState.bAnimThreadsDefaultChanged)
		iNumThreads = dynDebugState.iNumAnimThreads;
	else
		iNumThreads = getNumRealCpus() - 1;
#endif

	if (pAnimThreadManager)
	{
		mwtDestroy(pAnimThreadManager);
		pAnimThreadManager = NULL;
	}


	iNumThreads = CLAMP(iNumThreads, 0, MAX_ANIM_THREADS);

    // Set thread priorities slightly above normal so they don't get interrupted (say, during a critical section) and stall out all the threads
	// UPDATE: I'm not doing this.  It doesn't seem like a good idea.  I don't understand why they would stall out all the threads.  [RMARR - 5/29/12]
    piThreadPriorities = _alloca(sizeof(int) * iNumThreads);
    {
	    int i;
	    for (i=0; i<iNumThreads; ++i)
		    piThreadPriorities[i] = THREAD_PRIORITY_NORMAL;//THREAD_PRIORITY_ABOVE_NORMAL;
    }

	dynDebugState.iNumAnimThreads = iNumThreads;
	pAnimThreadManager = mwtCreate(256, 256, iNumThreads, piThreadIndices, piThreadPriorities, dynSkeletonThreadedUpdate, dynSkeletonProcessOutputFunc, "AnimationThread");
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;
void animThreads(int iNum)
{
	dynDebugState.iNumAnimThreads = iNum;
	dynDebugState.bAnimThreadsDefaultChanged = true;

	if (pAnimThreadManager)
	{
		mwtDestroy(pAnimThreadManager);
		pAnimThreadManager = NULL;
		dynAnimThreadManagerInit();
	}
}

AUTO_RUN;
void dynSetupAnimThreadMemBudgets(void)
{
	char buffer[128];
	int i;

	for (i = 0; i < MAX_ANIM_THREADS; ++i)
	{
		sprintf(buffer, "ThreadStack:AnimationThread:%d", i);
		memBudgetAddMapping(allocAddString(buffer), BUDGET_EngineMisc);
	}
}

static U32 uiTestEndDeathAnimations = 0;
AUTO_CMD_INT(uiTestEndDeathAnimations,danimEndDeathAnimations) ACMD_CATEGORY(dynAnimation);

void dynSkeletonUpdateAll(F32 fDeltaTime)
{
	U32 i;
	static DynSkeleton** eaSortedSkels = NULL;
	DynSkeleton* pLocalPlayerSkel = NULL;
	bInDynSkeletonUpdateAll = true;

	PERFINFO_AUTO_START_PIX("dynSkeletonUpdateAll", eaSize(&eaDynSkeletons));

	dynAnimTrackBufferUpdate();
	dynAnimCheckDataReload();

	dynDebugState.uiNumSeqDataCacheMisses[wl_state.frame_count & 15] = 0;
	for (i=0; i<MAX_WORLD_REGION_LOD_LEVELS; ++i)
	{
		dynDebugState.uiNumBonesUpdated[i] = dynDebugState.uiNumUpdatedSkels[i] = 0;
		LODSkeletonSlots[i] = 0;
	}
	dynDebugState.uiNumCulledSkels = 0;
	dynDebugState.uiNumAnimatedBones = 0;
	dynDebugState.uiNumSkinnedBones = 0;

	if (!gConf.bNewAnimationSystem)
		dynSeqDataCheckReloads();

	FOR_EACH_IN_EARRAY(eaDynSkeletons, DynSkeleton, pSkel)
	{
		if (pSkel->pParentSkeleton)
		{
            //assert(!eaSize(&pSkel->eaDependentSkeletons));

            // do nothing, since this skeleton will be updated by the skeleton that it depends upon
			continue;
		}

		if (TRUE_THEN_RESET(pSkel->bEndDeathAnimation) ||
			(	pSkel->bIsPlayingDeathAnim &&
				uiTestEndDeathAnimations))
		{
			dynSkeletonEndDeathAnimation(pSkel);
		}

		// First, copy the parent
		if (pSkel->pRoot->pParent)
		{
			if (!pSkel->ragdollState.bRagdollOn && pSkel->pLocation)
			{
				Vec3 vWorldSpacePos;
				dynNodeGetWorldSpacePos(pSkel->pLocation, vWorldSpacePos);
				dynNodeSetPosInline(pSkel->pRoot->pParent, vWorldSpacePos);
				pSkel->pRoot->pParent->uiDirtyBits = 1;
			}

			if (pSkel->bHasClientSideRagdoll ||
				pSkel->bHasClientSideTestRagdoll)
			{
				dynSkeletonUpdateClientSideRagdollState(pSkel,fDeltaTime,0);
			}
		}

		// Calculate distance
		assert( pSkel->pRoot );
		{
			Vec3 vSkelPos;
			pSkel->pRoot->uiDirtyBits = 1;
			dynNodeGetWorldSpacePos(pSkel->pRoot, vSkelPos);
			pSkel->fCurrentCamDistanceSquared = distance3Squared(wl_state.last_camera_frustum.cammat[3], vSkelPos);
			if (pSkel->pDrawSkel->fSendDistance > 0.0f)
				pSkel->fCurrentCamDistanceSquared /= pSkel->pDrawSkel->fSendDistance;
			else
			{
				//Errorf("Entity %s has invalid send distance %.2f", REF_STRING_FROM_HANDLE(pSkel->hCostume), pSkel->pDrawSkel->fSendDistance);
			}

			pSkel->pWorldRegion = worldGetWorldRegionByPos(vSkelPos);
		}
		// Add it to array
		if (!pSkel->pDrawSkel->bIsLocalPlayer)
			eaPush(&eaSortedSkels, pSkel);
		else
			pLocalPlayerSkel = pSkel;
	}
	FOR_EACH_END;

	// sort array by distance
	eaQSortG(eaSortedSkels, compareSkeletonDistance);

	if (!pAnimThreadManager)
		dynAnimThreadManagerInit();

	fSkelUpdateDeltaTime = fDeltaTime;

	// First, update the player 
	if (pLocalPlayerSkel)
	{
			dynSkeletonCalculateLOD(pLocalPlayerSkel);

		if (gConf.bNewAnimationSystem)
		{
			dynSkeletonCalcTerrainPitchSetup(pLocalPlayerSkel);
		}

		mwtQueueInput(pAnimThreadManager, pLocalPlayerSkel, false);
	}

	// Process the first skeletons
	FOR_EACH_IN_EARRAY_FORWARDS(eaSortedSkels, DynSkeleton, pSkel)
	{
		dynSkeletonCalculateLOD(pSkel);

		if (gConf.bNewAnimationSystem)
		{
			if (pSkel->bCreateClientSideRagdoll ||
				pSkel->bCreateClientSideTestRagdoll)
			{
				const WLCostume *pCostume = pSkel ? GET_REF(pSkel->hCostume) : NULL;
				bool bHadTestRagdoll = false;

				if (pSkel->bHasClientSideTestRagdoll) {
					dynSkeletonClientSideRagdollFree(pSkel);
					bHadTestRagdoll = true;
				} else {
					pSkel->fClientSideRagdollAnimTimePreCollision = pSkel->fClientSideRagdollAnimTime;
					pSkel->fClientSideRagdollAnimTime += fDeltaTime;
				}

				if (pCostume &&
						(pSkel->bVisible ||
						 bHadTestRagdoll))
				{
					if (dynSkeletonClientSideRagdollCreate(pSkel, pSkel->bCreateClientSideTestRagdoll)) {
						dynSkeletonUpdateClientSideRagdollState(pSkel,0.f,0);
					}
				}

				pSkel->bCreateClientSideRagdoll = false;
				pSkel->bCreateClientSideTestRagdoll = false;
			}

			if (dynDebugState.bDisableClientSideRagdoll ||
				uiMaxRagdollCount <= uiRagdollCount		||
				!pSkel->bVisible ||
				pSkel->bDisallowRagdoll) {
				dynSkeletonSetStanceWordInSet(pSkel, DS_STANCE_SET_RAGDOLL, pcStanceDisallowRagdoll);
			} else {
				dynSkeletonClearStanceWordInSet(pSkel, DS_STANCE_SET_RAGDOLL, pcStanceDisallowRagdoll);
			}

			dynSkeletonCalcTerrainPitchSetup(pSkel);
		}

		mwtQueueInput(pAnimThreadManager, pSkel, false);
	}
	FOR_EACH_END;

    eaClear(&eaSortedSkels);

	// Now that the main thread is waiting, flush out the queue here as well
	mwtWakeThreads(pAnimThreadManager);
	while (mwtProcessQueueDirect(pAnimThreadManager))
	{
	}
	mwtSleepUntilDone(pAnimThreadManager);

    mwtProcessOutputQueue(pAnimThreadManager);

	// Update the cache now that our threads are asleep
	dynAnimTrackCacheUpdate();
	if (!gConf.bNewAnimationSystem)
		dynSeqDataCacheUpdate();

	bInDynSkeletonUpdateAll = false;
    PERFINFO_AUTO_STOP_CHECKED_PIX("dynSkeletonUpdateAll");
}

void dynServerUpdateAllSkeletons(F32 fDeltaTime)
{
	// Nothing now that calc world space is on demand
}

static bool dynSkeletonIsNodeAttachedHelper(const DynNode *pCheck, const DynNode *pFind)
{
	if (pCheck == pFind)
		return true;
	else
	{
		if (pCheck->pChild && dynSkeletonIsNodeAttachedHelper(pCheck->pChild, pFind))
			return true;
		if (pCheck->pSibling && dynSkeletonIsNodeAttachedHelper(pCheck->pSibling, pFind))
			return true;
	}
	return false;
}

bool dynSkeletonIsNodeAttached(const DynSkeleton *pSkeleton, const DynNode *pNode)
{
	if (pSkeleton)
	{
		if (pSkeleton->pRoot && dynSkeletonIsNodeAttachedHelper(pSkeleton->pRoot, pNode))
			return true;
		FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pDepSkel)
		{
			if (pDepSkel && pDepSkel->pRoot && dynSkeletonIsNodeAttachedHelper(pDepSkel->pRoot, pNode))
				return true;
		}
		FOR_EACH_END;
	}
	return false;
}


const DynNode* dynSkeletonFindNode(const DynSkeleton* pSkeleton, const char* pcTag)
{
	const DynNode* pNode;
	if ( stashFindPointerConst(pSkeleton->stBoneTable, pcTag, &pNode) )
		return pNode;
	return NULL;
}

static const DynNode *dynSkeletonFindNodeCheckChildernHelper(const DynSkeleton *pSkeleton, const char *pcTag)
{
	const DynNode *pNode;

	if (stashFindPointerConst(pSkeleton->stBoneTable, pcTag, &pNode))
		return pNode;
	
	FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton)
	{
		if (pNode = dynSkeletonFindNodeCheckChildernHelper(pChildSkeleton, pcTag))
			return pNode;
	}
	FOR_EACH_END;

	return NULL;
}

const DynNode *dynSkeletonFindNodeCheckChildren(const DynSkeleton *pSkeleton, const char *pcTag)
{
	return dynSkeletonFindNodeCheckChildernHelper(pSkeleton->pGenesisSkeleton, pcTag);
}

DynNode* dynSkeletonFindNodeNonConst(DynSkeleton* pSkeleton, const char* pcTag)
{
	DynNode* pNode;
	if ( stashFindPointerConst(pSkeleton->stBoneTable, pcTag, &pNode) )
		return pNode;
	return NULL;
}

static const DynTransform *dynSkeletonGetNodeAuxTransform(const WLCostume *pCostume, const char *pcTag)
{
	if (pCostume && pCostume->bHasNodeAuxTransforms) {
		FOR_EACH_IN_EARRAY(pCostume->eaCostumeParts, WLCostumePart, pCostumePart) {
			const DynTransform *pTransform = dynAnimNodeAuxTransform(GET_REF(pCostumePart->hAnimNodeAuxTransformList), pcTag);
			if (pTransform) {
				return pTransform;
			}
		} FOR_EACH_END;
	}
	return NULL; // bad input or not found
}

static void dynScaledBaseBoneCalculateScale(DynNode* pBone, const DynBaseSkeleton* pBaseSkeleton, const WLCostume *pCostume, const SkelScaleInfo* pScaleInfo, const WLScaleAnimInterp* const* const* peaScaleAnimInterp, StashTable stBoneScaleTable, const Vec3 vParentScale)
{
	DynNode* pLink = pBone->pChild;
	const DynNode* pBaseBone = dynBaseSkeletonFindNode(pBaseSkeleton, pBone->pcTag);
	const CalcBoneScale* pCalcBoneScale = NULL;

	if (pBaseBone)
	{
		DynTransform* pTransforms = _alloca(sizeof(DynTransform) * eaSize(&pScaleInfo->eaScaleAnimTrack));
		U32 uiNumTransforms = 0;
		DynTransform base;
		dynNodeGetLocalTransformInline(pBaseBone, &base);

		FOR_EACH_IN_EARRAY_FORWARDS(pScaleInfo->eaScaleAnimTrack, const SkelScaleAnimTrack, pScaleAnimTrack)
		{
			FOR_EACH_IN_EARRAY_FORWARDS((*peaScaleAnimInterp), const WLScaleAnimInterp, pScaleAnimInterp)
			{
				if (pScaleAnimTrack->pcName == pScaleAnimInterp->pcName)
				{
					// Match!
					if (!pScaleAnimTrack->pScaleTrackHeader->bLoaded)
					{
						Errorf("Failed to preload anim track %s somehow. Not scaling character.", pScaleAnimTrack->pScaleTrackHeader->pcName);
						dynTransformClearInline(&pTransforms[uiNumTransforms++]);
					}
					else
					{
						F32 fFixedInterpValue = CLAMP((pScaleAnimInterp->fValue + 1.0f ) * 0.5f, 0.0f, 1.0f);
						F32 fFrameNum = interpF32(fFixedInterpValue, 0.0f, pScaleAnimTrack->pScaleTrackHeader->pAnimTrack->uiTotalFrames-1);
						PERFINFO_AUTO_START("dynBoneTrackUpdate", 1);
						dynBoneTrackUpdate(pScaleAnimTrack->pScaleTrackHeader->pAnimTrack, fFrameNum, &pTransforms[uiNumTransforms++], pBone->pcTag, NULL, false);
						PERFINFO_AUTO_STOP();
					}
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;

		{
			U32 uiTransform;
			for (uiTransform = 0; uiTransform < uiNumTransforms; ++uiTransform)
			{
				DynTransform temp;
				dynTransformMultiply(&pTransforms[uiTransform], &base, &temp);
				dynTransformCopy(&temp, &base);
			}
			dynNodeSetFromTransformInline(pBone, &base);
		}
	}

	{
		const DynTransform *pExtraTransform;
		Vec3 vScale;
		Vec3 vTrans;
		int i;
		
		dynNodeGetLocalScaleInline(pBone, vScale);
		dynNodeGetLocalPosInline(pBone, vTrans);
		for (i=0; i<3; ++i)
			vScale[i] /= vParentScale[i];
		if (stBoneScaleTable && stashFindPointerConst(stBoneScaleTable, pBone->pcTag, &pCalcBoneScale))
		{
			mulVecVec3(pCalcBoneScale->vScale, vScale, vScale);
			mulVecVec3(pCalcBoneScale->vUniversalScale, vScale, vScale);
			addVec3(pCalcBoneScale->vTrans, vTrans, vTrans);
		}
		dynNodeSetScale(pBone, vScale);
		dynNodeSetPos(pBone, vTrans);
		
		if (pExtraTransform = dynSkeletonGetNodeAuxTransform(pCostume, pBone->pcTag)) {
			DynTransform xForm, xFormNew;
			dynNodeGetLocalTransformInline(pBone, &xForm);
			dynTransformMultiply(pExtraTransform, &xForm, &xFormNew);
			dynNodeSetFromTransformInline(pBone, &xFormNew);
			//dynNodeSetDirtyInline(pNode); // no point since this is the scaled base skeleton
		}
	}

	while ( pLink )
	{
		dynScaledBaseBoneCalculateScale(pLink, pBaseSkeleton, pCostume, pScaleInfo, peaScaleAnimInterp, stBoneScaleTable, pCalcBoneScale?pCalcBoneScale->vScale:onevec3);
		pLink = pLink->pSibling;
	}
}

// need this because free() is redefined in memcheck
static void Free(void* p) { free(p); }


// Use the skelinfo to scale the base skeleton pBase for this individual skeleton
U32 uiDisableHeightFixupFix = 0;
AUTO_CMD_INT(uiDisableHeightFixupFix, danimDisableHeightFixupFix);
DynBaseSkeleton* dynScaledBaseSkeletonCreate(const DynSkeleton* pSkeleton)
{
	DynBaseSkeleton* pScaledBase;
	const WLCostume* pCostume = GET_REF(pSkeleton->hCostume);
	const SkelInfo* pSkelInfo = pCostume?GET_REF(pCostume->hSkelInfo):NULL;
	const DynBaseSkeleton* pBaseSkeleton = pSkeleton?GET_REF(pSkeleton->hBaseSkeleton):NULL;
	StashTable stBoneScaleTable;
	const SkelScaleInfo* pScaleInfo = pSkelInfo?GET_REF(pSkelInfo->hScaleInfo):NULL;
	const DynGroundRegData *pGroundRegData = pSkelInfo ? GET_REF(pSkelInfo->hGroundRegData) : NULL;

	if (!pScaleInfo || (eaSize(&pScaleInfo->eaScaleAnimTrack) == 0 && eaSize(&pScaleInfo->eaScaleGroup) == 0))
		return NULL;

	pScaledBase = MP_ALLOC(DynBaseSkeleton);
	pScaledBase->uiNumBones = pBaseSkeleton->uiNumBones;

	{
		DynNode* pNewRoot;
		if (shouldUseMemPool(pBaseSkeleton->uiNumBones))
			pScaledBase->pRoot = mpAlloc(mpSkeletonNodes);
		else
		{
			pScaledBase->pRoot = aligned_calloc(sizeof(DynNode), pBaseSkeleton->uiNumBones, 16);
			++uiUnpooledSkeletonCount;
		}

		pNewRoot = pScaledBase->pRoot;
		dynNodeTreeLinearCopy(pBaseSkeleton->pRoot, pScaledBase->pRoot, pBaseSkeleton->uiNumBones, NULL);
	}

	dynSkeletonCreateBoneTable(&pScaledBase->stBoneTable);
	dynSkeletonPopulateStashTable(pScaledBase->stBoneTable, pScaledBase->pRoot);
	dynSkeletonShrinkBoneTable(&pScaledBase->stBoneTable);

	wlCostumeGenerateBoneScaleTable(pCostume, &stBoneScaleTable);

	dynScaledBaseBoneCalculateScale(pScaledBase->pRoot, pBaseSkeleton, pCostume, pScaleInfo, &pCostume->eaScaleAnimInterp, stBoneScaleTable, onevec3);

	stashTableDestroyEx(stBoneScaleTable, NULL, Free);

	// Calculate hip scaling factor to keep feet on ground
	{
		DynNode* pBaseHips = NULL;
		DynNode* pBaseSole = NULL;
		DynNode* pScaledHips = NULL;
		DynNode* pScaledSole = NULL;

		if (pGroundRegData) {
			pBaseHips = dynNodeFindByName(pBaseSkeleton->pRoot, pGroundRegData->pcHipsNode);
			pBaseSole = dynNodeFindByName(pBaseSkeleton->pRoot, pGroundRegData->pcHeightFixupNode);
			pScaledHips = dynNodeFindByName(pScaledBase->pRoot, pGroundRegData->pcHipsNode);
			pScaledSole = dynNodeFindByName(pScaledBase->pRoot, pGroundRegData->pcHeightFixupNode);
		} else {
			pBaseHips = dynNodeFindByName(pBaseSkeleton->pRoot, pcNodeNameHips);
			pBaseSole = dynNodeFindByName(pBaseSkeleton->pRoot, pScaleInfo->pcHeightFixupBone ? pScaleInfo->pcHeightFixupBone : pcNodeNameSoleR);
			pScaledHips = dynNodeFindByName(pScaledBase->pRoot, pcNodeNameHips);
			pScaledSole = dynNodeFindByName(pScaledBase->pRoot, pScaleInfo->pcHeightFixupBone ? pScaleInfo->pcHeightFixupBone : pcNodeNameSoleR);
		}

		if (pBaseHips && pBaseSole && pScaledHips && pScaledSole)
		{
			Vec3 vBaseHipsPos, vBaseSolePos;
			Vec3 vScaledHipsPos, vScaledSolePos;
			dynNodeGetWorldSpacePos(pBaseHips, vBaseHipsPos);
			dynNodeGetWorldSpacePos(pBaseSole, vBaseSolePos);
			dynNodeGetWorldSpacePos(pScaledHips, vScaledHipsPos);
			dynNodeGetWorldSpacePos(pScaledSole, vScaledSolePos);
			pScaledBase->fHipsHeightAdjustmentFactor = uiDisableHeightFixupFix ?
														(vScaledHipsPos[1] - vScaledSolePos[1]) / vScaledHipsPos[1] :
														(1.f - (vScaledSolePos[1]/vScaledHipsPos[1])) / (1.f - (vBaseSolePos[1]/vBaseHipsPos[1]));
		}
		else
			pScaledBase->fHipsHeightAdjustmentFactor = 1.0f;
	}

	return pScaledBase;
}

void dynScaleCollectionInit(DynBaseSkeleton* pScaledBase, DynScaleCollection* pCollection)
{
	pCollection->uiNumTransforms = pScaledBase->uiNumBones;
	pCollection->fHipsHeightAdjustmentFactor = pScaledBase->fHipsHeightAdjustmentFactor;
	pCollection->pLocalTransforms = calloc(sizeof(DynTransform), pCollection->uiNumTransforms);
	pCollection->stBoneLookup = stashTableCreateWithStringKeys(pCollection->uiNumTransforms, StashDefault);
	{
		U32 uiIndex;
		for (uiIndex=0; uiIndex<pScaledBase->uiNumBones; ++uiIndex)
		{
			DynNode* pScaleNode = &pScaledBase->pRoot[uiIndex];
			dynNodeGetLocalTransformInline(pScaleNode, &pCollection->pLocalTransforms[uiIndex]);
			if (!stashAddInt(pCollection->stBoneLookup, pScaleNode->pcTag, uiIndex, false))
			{
				Errorf("Somehow added bone %s to DynScaleCollection table twice!", pScaleNode->pcTag);
			}
		}
	}
}

void dynScaleCollectionClear(DynScaleCollection* pCollection)
{
	stashTableDestroy(pCollection->stBoneLookup);
	free(pCollection->pLocalTransforms);
	pCollection->pLocalTransforms = NULL;
	pCollection->uiNumTransforms = 0;
}

const DynTransform* dynScaleCollectionFindTransform(const DynScaleCollection* pCollection, const char* pcName)
{
	int iIndex = -1;
	if (stashFindInt(pCollection->stBoneLookup, pcName, &iIndex))
		return &pCollection->pLocalTransforms[iIndex];
	return NULL;
}

void dynSkeletonReloadSkelInfo( DynSkeleton* pSkeleton)
{
	const WLCostume* pCostume = GET_REF(pSkeleton->hCostume);
	const SkelInfo* pSkelInfo = pCostume?GET_REF(pCostume->hSkelInfo):NULL;
	assert(pSkelInfo);
	REMOVE_HANDLE(pSkeleton->hBaseSkeleton);
	COPY_HANDLE(pSkeleton->hBaseSkeleton, pSkelInfo->hBaseSkeleton);
	{
		DynBaseSkeleton* pScaledBase = dynScaledBaseSkeletonCreate(pSkeleton);
		dynScaleCollectionClear(&pSkeleton->scaleCollection);
		if (pScaledBase)
		{
			dynScaleCollectionInit(pScaledBase, &pSkeleton->scaleCollection);
			dynBaseSkeletonFree(pScaledBase);
		}
		else
		{
			dynScaleCollectionInit(GET_REF(pSkeleton->hBaseSkeleton), &pSkeleton->scaleCollection);
		}
	}
	if (pCostume)
	{
		if (!gConf.bNewAnimationSystem)
		{
			FOR_EACH_IN_EARRAY(pSkeleton->eaSqr, DynSequencer, pSqr)
				dynSequencerFree(pSqr);
			FOR_EACH_END;
			eaClear(&pSkeleton->eaSqr);
			dynSkeletonCreateSequencers(pSkeleton);
		}
	}
	pSkeleton->bInvalid = false;

	/*
	dynNodeFreeTree(pSkeleton->pRoot);
	pSkeleton->pRoot = dynNodeAllocTreeCopy(pSkeleton->pBase->pRoot, NULL);
	*/
}



void dynSkeletonReloadAllUsingCostume(const WLCostume* pCostume, enumResourceEventType eType)
{
	const U32 uiNumSkeletons = eaSize(&eaDynSkeletons);
	U32 uiSkeleton;
	for (uiSkeleton=0; uiSkeleton<uiNumSkeletons; ++uiSkeleton)
	{
		DynSkeleton* pSkeleton = eaDynSkeletons[uiSkeleton];
		if (GET_REF(pSkeleton->hCostume) == pCostume)
		{
			if (eType != RESEVENT_RESOURCE_REMOVED)
				dynSkeletonReloadSkelInfo(pSkeleton); // Reload
			else
				pSkeleton->bInvalid = true;
		}
	}
}



U32 dynSkeletonGetTotalCount( void )
{
	return eaSize(&eaDynSkeletons);
}

U32 dynSkeletonGetAllocationCount( void )
{
	return (U32)mpGetAllocatedCount(mpSkeletonNodes) + uiUnpooledSkeletonCount;
}

U32 dynSkeletonGetUnpooledCount( void )
{
	return uiUnpooledSkeletonCount;
}


DynNode** eaNode[5];

void danimPrintSkeletonHelper(DynNode* pRoot)
{
	DynNode* pNode = pRoot;
	while (pNode)
	{
		eaPush(&eaNode[MIN(pNode->uiLODLevel,MAX_WORLD_REGION_LOD_LEVELS-1)], pNode);
		danimPrintSkeletonHelper(pNode->pChild);
		pNode = pNode->pSibling;
	}
}

AUTO_COMMAND;
void danimPrintPlayerBones(void)
{
	if (pPlayerSkeleton)
	{
		int i, count;
		danimPrintSkeletonHelper(pPlayerSkeleton->pRoot);
		for (i=0; i<5; ++i)
		{
			printf("Level %d\n", i);
			count = 0;
			FOR_EACH_IN_EARRAY(eaNode[i], DynNode, pNode)
				printf("%s\n", pNode->pcTag);
				++count;
			FOR_EACH_END;
			printf("%d total\n", count);
			eaDestroy(&eaNode[i]);
		}
	}
}

DynNode** eaUnderNode;

void danimPrintSkeletonUnderHelper(DynNode* pRoot, const char* pcUnderTag, bool bUnder)
{
	DynNode* pNode = pRoot;
	while (pNode)
	{
		if (bUnder)
			eaPush(&eaUnderNode, pNode);
		danimPrintSkeletonUnderHelper(pNode->pChild, pcUnderTag, (bUnder || pNode->pcTag == pcUnderTag));
		pNode = pNode->pSibling;
	}
}

AUTO_COMMAND;
void danimPrintPlayerBonesUnder(const char* bonename ACMD_NAMELIST(stPlayerBoneTable, STASHTABLE))
{
	if (pPlayerSkeleton)
	{
		int count=0;
		danimPrintSkeletonUnderHelper(pPlayerSkeleton->pRoot, allocAddString(bonename), false);
		FOR_EACH_IN_EARRAY_FORWARDS(eaUnderNode, DynNode, pNode)
			printf("%s\n", pNode->pcTag);
		++count;
		FOR_EACH_END;
		printf("%d total\n", count);
		eaDestroy(&eaUnderNode);
	}
}


// Pass in a function of this form , and have it return true if you want to exclude that node from consideration as the closest bone to a line
bool ExcludeMuscleBones(const DynNode* pNode, void *pUserData)
{
	if (pNode->pcTag && strnicmp(pNode->pcTag, "Muscle", 6)==0)
		return true;
	return false;
}


static const DynNode* findClosestBoneToLine(const DynNode* pRoot, const Vec3 vLineStart, const Vec3 vLineEnd, ExcludeNode excludeNode, void *pUserData)
{
	const DynNode*	nodeStack[100];
	S32			stackPos = 0;
	const DynNode* pClosest = NULL;
	F32 fClosest = FLT_MAX;
	Vec3 vPos[2];

	nodeStack[stackPos++] = pRoot;
	
	while(stackPos)
	{
		const DynNode*	pNode = nodeStack[--stackPos];
		if (pNode->pSibling)
			nodeStack[stackPos++] = pNode->pSibling;
		if (pNode->pChild)
			nodeStack[stackPos++] = pNode->pChild;

		if (pNode->pParent && (!excludeNode || !excludeNode(pNode, pUserData)))
		{
			// Check distance
			Vec3 vDiffBone, vDiffLine;
			F32 fBoneLen, fLineLen, fDist;
			Vec3 vSect[2];
			dynNodeGetWorldSpacePos(pNode->pParent, vPos[0]);
			dynNodeGetWorldSpacePos(pNode, vPos[1]);
			subVec3(vLineEnd, vLineStart, vDiffLine);
			subVec3(vPos[1], vPos[0], vDiffBone);
			fBoneLen = normalVec3(vDiffBone);
			fLineLen = normalVec3(vDiffLine);
			if (fBoneLen >= 0.0001f)
				fDist = LineLineDistSquared(vPos[0], vDiffBone, fBoneLen, vSect[0], vLineStart, vDiffLine, fLineLen, vSect[1]);
			else
				fDist = PointLineDistSquared(vPos[0], vLineStart, vDiffLine, fLineLen, NULL);
			if (fDist < fClosest)
			{
				fClosest = fDist;
				pClosest = pNode;
			}
		}
	}

	return pClosest;
}

void dynSkeletonGetBoneWorldPosByName(const DynNode* pRoot, const char* pcBone, Vec3 vPosOut)
{
	const DynNode*	nodeStack[100];
	S32			stackPos = 0;

	nodeStack[stackPos++] = pRoot;

	while(stackPos)
	{
		const DynNode*	pNode = nodeStack[--stackPos];
		if (pNode->pSibling)
			nodeStack[stackPos++] = pNode->pSibling;
		if (pNode->pChild)
			nodeStack[stackPos++] = pNode->pChild;

		if (pNode && vPosOut && pNode->pcTag && strcmp(pNode->pcTag, pcBone) == 0)
		{
			dynNodeGetWorldSpacePos(pNode, vPosOut);
			return;
		}
	}
}

const DynNode* dynSkeletonGetClosestBoneToLineSegment(const DynSkeleton* pSkeleton, const Vec3 vCursorStart, const Vec3 vCursorEnd, ExcludeNode excludeNode, void *pUserData)
{
	return findClosestBoneToLine(pSkeleton->pRoot, vCursorStart, vCursorEnd, excludeNode, pUserData);
}

// This is fast, and just returns the precalculated extents based off of the costume's collision data
void dynSkeletonGetCollisionExtents(const DynSkeleton* pSkeleton, Vec3 vMin, Vec3 vMax)
{
	WLCostume* pCostume = GET_REF(pSkeleton->hCostume);
	Vec3 vSkelPos;
	dynNodeGetWorldSpacePos(pSkeleton->pRoot, vSkelPos);
	if (pCostume)
	{
		addVec3(vSkelPos, pCostume->vExtentsMin, vMin);
		addVec3(vSkelPos, pCostume->vExtentsMax, vMax);
	}
	else
	{
		copyVec3(vSkelPos, vMin);
		copyVec3(vSkelPos, vMax);
	}
}

typedef struct CriticalExtentData
{
	Vec3 vMin;
	Vec3 vMax;
} CriticalExtentData;


bool findSkeletonCriticalExtents(DynNode* pNode, CriticalExtentData* pData)
{
	if (pNode->uiCriticalBone && pNode->uiSkinningBone)
	{
		Vec3 vPos;
		dynNodeGetWorldSpacePos(pNode, vPos);
		MAXVEC3(pData->vMax,vPos,pData->vMax);
		MINVEC3(pData->vMin,vPos,pData->vMin);
	}
	return (!!pNode->uiCriticalChildren); 
}


// This is slow and expensive, but is calculated at the moment it is called. It uses the position of each bone for which geometry is skinned to
void dynSkeletonGetExpensiveExtents(const DynSkeleton* pSkeleton, Vec3 vMin, Vec3 vMax, F32 fFudgeFactor)
{
	CriticalExtentData extents;
	//dynNodeGetWorldSpacePos(pSkeleton->pRoot, extents.vMin);
	//copyVec3(extents.vMin, extents.vMax);
	extents.vMin[0] = FLT_MAX;
	extents.vMin[1] = FLT_MAX;
	extents.vMin[2] = FLT_MAX;
	extents.vMax[0] = -FLT_MAX;
	extents.vMax[1] = -FLT_MAX;
	extents.vMax[2] = -FLT_MAX;

	dynNodeProcessTree(pSkeleton->pRoot, findSkeletonCriticalExtents, &extents);

	if(extents.vMin[0] == FLT_MAX) {
		dynNodeGetWorldSpacePos(pSkeleton->pRoot, extents.vMin);
		copyVec3(extents.vMin, extents.vMax);
	}

	copyVec3(extents.vMin, vMin);
	copyVec3(extents.vMax, vMax);

	if (fFudgeFactor)
	{
		addVec3same(vMin, -fFudgeFactor, vMin);
		addVec3same(vMax, fFudgeFactor, vMax);
	}
}

void dynSkeletonGetVisibilityExtents(const DynSkeleton* pSkeleton, Vec3 vMin, Vec3 vMax, bool bIncludeSubskeletons)
{
	if (pSkeleton->bDontUpdateExtents)
	{
		if (bIncludeSubskeletons) {
			copyVec3(pSkeleton->vCurrentGroupExtentsMax, vMax);
			copyVec3(pSkeleton->vCurrentGroupExtentsMin, vMin);
		} else {
			copyVec3(pSkeleton->vCurrentExtentsMax, vMax);
			copyVec3(pSkeleton->vCurrentExtentsMin, vMin);
		}
		return;
	}

	// First, find center, then scale out from there
	
	{
		F32 fScaleFactor = DEFAULT_VISIBILITY_EXTENT_SCALEFACTOR;
		int i = 0;
		if (bIncludeSubskeletons)
		{
			for (i=0; i<3; ++i)
			{
				F32 fHalfLength = (pSkeleton->vCurrentGroupExtentsMax[i] - pSkeleton->vCurrentGroupExtentsMin[i]) * 0.5f;
				F32 fCenter = pSkeleton->vCurrentGroupExtentsMin[i] + fHalfLength;
				vMin[i] = fCenter - fHalfLength * fScaleFactor;
				vMax[i] = fCenter + fHalfLength * fScaleFactor;
			}
		} else {
			for (i=0; i<3; ++i)
			{
				F32 fHalfLength = (pSkeleton->vCurrentExtentsMax[i] - pSkeleton->vCurrentExtentsMin[i]) * 0.5f;
				F32 fCenter = pSkeleton->vCurrentExtentsMin[i] + fHalfLength;
				vMin[i] = fCenter - fHalfLength * fScaleFactor;
				vMax[i] = fCenter + fHalfLength * fScaleFactor;
			}
		}
	}
}

bool dynSkeletonIsForceVisible(const DynSkeleton *pSkeleton)
{
	return 	pSkeleton &&
			(	pSkeleton->bForceVisible	||
				pSkeleton->bWasForceVisible	);
}

typedef struct ForceAnimationData
{
	DynSkeleton* pSkeleton;
	DynDrawSkeleton* pDrawSkel;
	DynAnimTrackHeader* pAnimTrackHeader;
	F32 fHipsHeightAdjustmentFactor;
	F32 fFrame;
	Mat4 mRoot;
	bool bRoot;
	bool bUseBaseSkeleton;
	bool bApplyHeightBump;
} ForceAnimationData;

bool dynSkeletonForceAnimationHelper(DynNode* pBone, ForceAnimationData* pData)
{
	if (pBone->uiCriticalBone)
	{
		DynTransform xform;
		const DynTransform* pxScaledBaseTransform = NULL;
		DynTransform xBaseTransform;
		if (pData->pSkeleton->pAnimBoneInfos && !pData->bUseBaseSkeleton)
			pxScaledBaseTransform = &pData->pSkeleton->pAnimBoneInfos[pBone->iCriticalBoneIndex].xBaseTransform;
		else
		{
			DynBaseSkeleton* pBaseSkeleton = GET_REF(pData->pSkeleton->hBaseSkeleton);
			const DynNode* pBaseNode;
			if (pBaseSkeleton && ( pBaseNode = dynBaseSkeletonFindNode(pBaseSkeleton, pBone->pcTag)) )
			{
				dynNodeGetLocalTransformInline(pBaseNode, &xBaseTransform);
				pxScaledBaseTransform = &xBaseTransform;
			}
		}
		dynBoneTrackUpdate(pData->pAnimTrackHeader->pAnimTrack, pData->fFrame, &xform, pBone->pcTag, pxScaledBaseTransform, false);
		if (pData->bApplyHeightBump &&
			pBone->pcTag == pcHipsName)
		{
			scaleVec3(xform.vPos, pData->fHipsHeightAdjustmentFactor, xform.vPos);
		}
		dynNodeSetFromTransformInline(pBone, &xform);
		if (pData->bRoot)
		{
			pData->pSkeleton->pRoot->uiDirtyBits = 1;
			dynNodeGetWorldSpaceMat(pData->pSkeleton->pRoot, pData->mRoot, false);
			pData->bRoot = false;
		}
		else
		{
			dynNodeCalcWorldSpaceOneNode(pBone);
		}

		if (pData->pDrawSkel && pBone->iSkinningBoneIndex >= 0 && pBone->uiSkinningBone)
			dynNodeCreateSkinningMat(pData->pDrawSkel->pCurrentSkinningMatSet->pSkinningMats[pBone->iSkinningBoneIndex], pBone, pData->pSkeleton->pAnimBoneInfos[pBone->iCriticalBoneIndex].vBaseOffset, pData->mRoot);
	}
	return (!!pBone->uiCriticalChildren); 
}


bool dynSkeletonForceAnimationEx(DynSkeleton* pSkeleton, const char* pcTrackname, F32 fFrame, U32 uiUseBaseSkeleton, U32 uiApplyHeightBump)
{
	ForceAnimationData data;
	data.fFrame = fFrame;
	data.pAnimTrackHeader = dynAnimTrackHeaderFind(pcTrackname);
	data.bRoot = true;
	data.pDrawSkel = pSkeleton->pDrawSkel;
	data.pSkeleton = pSkeleton;
	data.bUseBaseSkeleton = !!uiUseBaseSkeleton;
	data.bApplyHeightBump = !!uiApplyHeightBump;
	data.fHipsHeightAdjustmentFactor = pSkeleton->scaleCollection.fHipsHeightAdjustmentFactor;

	if (!data.pAnimTrackHeader || !dynAnimTrackHeaderRequest(data.pAnimTrackHeader))
		return false;

	if (pSkeleton->pDrawSkel)
		dynDrawSkeletonAllocSkinningMats(pSkeleton->pDrawSkel);

	dynNodeProcessTree(pSkeleton->pRoot, dynSkeletonForceAnimationHelper, &data);

	return true;
}

bool dynSkeletonForceAnimationPrepare(const char* pcTrackname)
{
	DynAnimTrackHeader* pHeader = dynAnimTrackHeaderFind(pcTrackname);
	if (!pHeader)
		return false;
	return dynAnimTrackHeaderRequest(pHeader);
}

const DynBaseSkeleton* dynSkeletonGetRegSkeleton(const DynSkeleton* pSkeleton)
{
	WLCostume* pCostume = GET_REF(pSkeleton->hCostume);
	if (pCostume)
	{
		SkelInfo* pSkelInfo = GET_REF(pCostume->hSkelInfo);
		if (pSkelInfo)
		{
			return GET_REF(pSkelInfo->hRegistrationSkeleton);
		}
	}
	return NULL;
}

const DynBaseSkeleton* dynSkeletonGetBaseSkeleton(const DynSkeleton* pSkeleton)
{
	return GET_REF(pSkeleton->hBaseSkeleton);
}

void dynDebugSkeletonUpdateBits(const DynSkeleton* pSkeleton, F32 fDeltaTime)
{
	if (!gConf.bNewAnimationSystem && eaSize(&pSkeleton->eaSqr))
	{
		const DynBitField* pBF = dynSeqGetBits(pSkeleton->eaSqr[0]);
		char cBuf[256];
		char* pcBit = strTokWithSpacesAndPunctuation(NULL, NULL);
		dynBitFieldWriteBitString(SAFESTR(cBuf), pBF);

		pcBit = strTokWithSpacesAndPunctuation(cBuf, " ");

		FOR_EACH_IN_EARRAY(dynDebugState.eaSkelBits, DynDebugBit, pDebugBit)
		{
			if (pDebugBit->fTimeSinceSet < 0.0f)
				pDebugBit->fTimeSinceSet = 0.0f;
		}
		FOR_EACH_END;

		while (pcBit)
		{
			bool bFound = false;
			FOR_EACH_IN_EARRAY(dynDebugState.eaSkelBits, DynDebugBit, pDebugBit)
				if (stricmp(pcBit, pDebugBit->pcBitName)==0)
				{
					bFound = true;
					pDebugBit->fTimeSinceSet = -1.0f;
					break;
				}
				FOR_EACH_END;
				if (!bFound)
				{
					DynDebugBit* pNewBit = calloc(sizeof(DynDebugBit), 1);
					pNewBit->pcBitName = allocAddString(pcBit);
					pNewBit->fTimeSinceSet = -1.0f;
					eaPush(&dynDebugState.eaSkelBits, pNewBit);
				}
				pcBit = strTokWithSpacesAndPunctuation(cBuf, " ");
		}

		FOR_EACH_IN_EARRAY(dynDebugState.eaSkelBits, DynDebugBit, pDebugBit)
		{
			if (pDebugBit->fTimeSinceSet >= 0.0f)
				pDebugBit->fTimeSinceSet += fDeltaTime;
			if (pDebugBit->fTimeSinceSet > DYN_DEBUG_MAX_BIT_DISPLAY_TIME)
			{
				eaRemoveFast(&dynDebugState.eaSkelBits, ipDebugBitIndex);
				free(pDebugBit);
			}
		}
		FOR_EACH_END;
	}
}

static int dynDebugCmpStances(const void **pa, const void **pb)
{
	const DynSkeletonDebugStance *a = *(const DynSkeletonDebugStance **)pa;
	const DynSkeletonDebugStance *b = *(const DynSkeletonDebugStance **)pb;
	if (a->iState != DDNAS_STANCE_STATE_OLD)
	{
		if (b->iState == DDNAS_STANCE_STATE_OLD) return -1;    //a is active, b is old
		else return stricmp(a->pcStanceName, b->pcStanceName); //they are both active, alphabetize
	}
	else
	{
		if (b->iState != DDNAS_STANCE_STATE_OLD) return 1; //a is old, b is active
		else
		{
			if      (a->fTimeInState < b->fTimeInState) return -1; //both old, a is younger
			else if (a->fTimeInState > b->fTimeInState) return  1; //both old, b is younger
			else return stricmp(a->pcStanceName, b->pcStanceName); //identical age, alphabetic
		}
	}
}

static int dynSkeletonGetUpdaterIndex(const DynSkeleton *pSkeleton, const DynAnimGraphUpdater *pUpdater)
{
	if (pSkeleton && pUpdater) {
		FOR_EACH_IN_EARRAY(pSkeleton->eaAGUpdater, DynAnimGraphUpdater, pChkUpdater) {
			if (pChkUpdater == pUpdater)
				return ipChkUpdaterIndex;
		} FOR_EACH_END;
	}
	return -1;
}

void dynDebugSkeletonStartGraph(const DynSkeleton *pSkeleton, const DynAnimGraphUpdater *pUpdater, const char *pcKeyword, U32 uid)
{
	DynSkeletonDebugNewAnimSys *pDebugSkeleton = SAFE_MEMBER(pSkeleton, pDebugSkeleton);
	int i = dynSkeletonGetUpdaterIndex(pSkeleton, pUpdater);

	if (pDebugSkeleton && -1 < i && i < eaSize(&pDebugSkeleton->eaGraphUpdaters)) {
		DynSkeletonDebugKeyword *pNewKeyword = StructCreate(parse_DynSkeletonDebugKeyword);
		pNewKeyword->pcKeyword = pcKeyword;
		pNewKeyword->uid = uid;
		pNewKeyword->fTimeSinceTriggered = 0.0f;
		if (eaSize(&pDebugSkeleton->eaGraphUpdaters[i]->eaKeywords))
			pDebugSkeleton->eaGraphUpdaters[i]->eaKeywords[0]->fTimeSinceTriggered += 0.001f;
		eaInsert(&pDebugSkeleton->eaGraphUpdaters[i]->eaKeywords, pNewKeyword, 0);
	}
}

void dynDebugSkeletonStartNode(const DynSkeleton *pSkeleton, const DynAnimGraphUpdater *pUpdater, const DynAnimGraphUpdaterNode *pNode)
{
	if (pNode) {
		DynSkeletonDebugNewAnimSys *pDebugSkeleton = SAFE_MEMBER(pSkeleton, pDebugSkeleton);
		int i = dynSkeletonGetUpdaterIndex(pSkeleton, pUpdater);

		if (pDebugSkeleton && -1 < i && i < eaSize(&pDebugSkeleton->eaGraphUpdaters)) {
			DynSkeletonDebugGraphNode *newNode = StructCreate(parse_DynSkeletonDebugGraphNode);
			int curNumNodes = eaSize(&pDebugSkeleton->eaGraphUpdaters[i]->eaNodes);
			newNode->pcGraphName	= dynAnimGraphNodeGetGraphName(pNode);
			newNode->pcNodeName		= dynAnimGraphNodeGetName(pNode);
			newNode->pcMoveName		= dynAnimGraphNodeGetMoveName(pNode);
			newNode->reason			= dynAnimGraphNodeGetReason(pNode);
			newNode->reasonDetails	= dynAnimGraphNodeGetReasonDetails(pNode);
			if (0 == curNumNodes) {
				eaPush(&pDebugSkeleton->eaGraphUpdaters[i]->eaNodes, newNode);
			} else {
				eaInsert(&pDebugSkeleton->eaGraphUpdaters[i]->eaNodes, newNode, 0);
			}

			while (eaSize(&pDebugSkeleton->eaGraphUpdaters[i]->eaNodes) >  DDNAS_ANIMGRAPH_MOVE_MAXCOUNT)
			{
				DynSkeletonDebugGraphNode *delNode = pDebugSkeleton->eaGraphUpdaters[i]->eaNodes[DDNAS_ANIMGRAPH_MOVE_MAXCOUNT];
				eaRemoveFast(&pDebugSkeleton->eaGraphUpdaters[i]->eaNodes, DDNAS_ANIMGRAPH_MOVE_MAXCOUNT);
				free(delNode);
			}
		}
	}
}

void dynDebugSkeletonSetFlag(const DynSkeleton *pSkeleton, const DynAnimGraphUpdater *pUpdater, const char *pcFlag, U32 uid)
{
	DynSkeletonDebugNewAnimSys *pDebugSkeleton = SAFE_MEMBER(pSkeleton, pDebugSkeleton);
	int i = dynSkeletonGetUpdaterIndex(pSkeleton, pUpdater);

	if (pDebugSkeleton && -1 < i && i < eaSize(&pDebugSkeleton->eaGraphUpdaters))
	{
		DynSkeletonDebugFlag *pNewFlag = StructCreate(parse_DynSkeletonDebugFlag);
		pNewFlag->pcFlag = pcFlag;
		pNewFlag->uid = uid;
		pNewFlag->fTimeSinceTriggered = 0.0f;
		eaInsert(&pDebugSkeleton->eaGraphUpdaters[i]->eaFlags, pNewFlag, 0);
	}
}

void dynDebugSkeletonUseFlag(const DynSkeleton *pSkeleton, const DynAnimGraphUpdater *pUpdater, const char *pcFlag)
{
	DynSkeletonDebugNewAnimSys *pDebugSkeleton = SAFE_MEMBER(pSkeleton, pDebugSkeleton);
	int i = dynSkeletonGetUpdaterIndex(pSkeleton, pUpdater);

	if (pDebugSkeleton && -1 < i && i < eaSize(&pDebugSkeleton->eaGraphUpdaters))
	{
		FOR_EACH_IN_EARRAY(pDebugSkeleton->eaGraphUpdaters[i]->eaFlags, DynSkeletonDebugFlag, pCheckFlag) {
			if (pCheckFlag->pcFlag == pcFlag && pCheckFlag->fTimeSinceTriggered <= 0.0f) {
				pCheckFlag->fTimeSinceTriggered = 0.001f;
				break;
			}
		} FOR_EACH_END;
	}
}

void dynDebugSkeletonClearFlags(const DynSkeleton *pSkeleton, const DynAnimGraphUpdater *pUpdater)
{
	DynSkeletonDebugNewAnimSys *pDebugSkeleton = SAFE_MEMBER(pSkeleton, pDebugSkeleton);
	int i = dynSkeletonGetUpdaterIndex(pSkeleton, pUpdater);

	if (pDebugSkeleton && -1 < i && i < eaSize(&pDebugSkeleton->eaGraphUpdaters)) {
		FOR_EACH_IN_EARRAY(pDebugSkeleton->eaGraphUpdaters[i]->eaFlags, DynSkeletonDebugFlag, pCheckFlag) {
			if (pCheckFlag->fTimeSinceTriggered <= 0.0f)
				pCheckFlag->fTimeSinceTriggered = 0.001f;
		} FOR_EACH_END;
	}
}

void dynDebugSkeletonStartGraphFX(const DynSkeleton *pSkeleton, const DynAnimGraphUpdater *pUpdater, const char *pcFX, bool bPlayed)
{
	DynSkeletonDebugNewAnimSys *pDebugSkeleton = SAFE_MEMBER(pSkeleton, pDebugSkeleton);
	int i = dynSkeletonGetUpdaterIndex(pSkeleton, pUpdater);

	if (pDebugSkeleton && -1 < i && i < eaSize(&pDebugSkeleton->eaGraphUpdaters)) {
		DynSkeletonDebugFX *pNewFX = StructCreate(parse_DynSkeletonDebugFX);
		pNewFX->pcFXName = pcFX;
		pNewFX->fTimeSinceTriggered = 0.0;
		pNewFX->bPlayed = bPlayed;
		eaInsert(&pDebugSkeleton->eaGraphUpdaters[i]->eaFX, pNewFX, 0);

		if (eaSize(&pDebugSkeleton->eaGraphUpdaters[i]->eaFX) > DDNAS_FX_MAXSAVECOUNT) {
			eaRemoveTail(&pDebugSkeleton->eaGraphUpdaters[i]->eaFX, DDNAS_FX_MAXSAVECOUNT);
		}
	}
}

void dynDebugSkeletonStartMoveFX(const DynSkeleton *pSkeleton, const char *pcFX, bool bPlayed)
{
	DynSkeletonDebugNewAnimSys *pDebugSkeleton = SAFE_MEMBER(pSkeleton, pDebugSkeleton);

	if (pDebugSkeleton) {	
		DynSkeletonDebugFX *pNewMoveFX = StructCreate(parse_DynSkeletonDebugFX);
		pNewMoveFX->pcFXName = pcFX;
		pNewMoveFX->fTimeSinceTriggered = 0.0;
		pNewMoveFX->bPlayed = bPlayed;
		eaInsert(&pSkeleton->pDebugSkeleton->eaMovementFX, pNewMoveFX, 0);

		if (eaSize(&pDebugSkeleton->eaMovementFX) > DDNAS_FX_MAXSAVECOUNT) {
			eaRemoveTail(&pDebugSkeleton->eaMovementFX, DDNAS_FX_MAXSAVECOUNT);
		}
	}
}

void dynDebugSkeletonUpdateNewAnimSysData(const DynSkeleton *pSkeleton, F32 fDeltaTime)
{
	DynSkeletonDebugNewAnimSys *pDebugSkeleton = SAFE_MEMBER(pSkeleton, pDebugSkeleton);

	PERFINFO_AUTO_START_FUNC();

	//add new stances & set timers for current stances
	ARRAY_FOREACH_BEGIN(pSkeleton->eaStances, i);
	{
		//add new stance words
		FOR_EACH_IN_EARRAY(pSkeleton->eaStances[i], const char, pcStance)
		{
			bool foundStance = false;

			//check if the stance already exists
			FOR_EACH_IN_EARRAY(pDebugSkeleton->eaDebugStances, DynSkeletonDebugStance, testStance)
			{
				if (testStance->pcStanceName == pcStance &&
					(testStance->iState == DDNAS_STANCE_STATE_NEW || testStance->iState == DDNAS_STANCE_STATE_CURRENT))
				{
					testStance->fTimeActive = pSkeleton->eaStanceTimers[i][ipcStanceIndex];
					foundStance = true;
					break;
				}
			}
			FOR_EACH_END;

			//insert new stance names
			if (!foundStance)
			{
				//create per stance data block
				DynSkeletonDebugStance *newStance = StructCreate(parse_DynSkeletonDebugStance);
				newStance->pcStanceName = pcStance;
				newStance->iState = DDNAS_STANCE_STATE_NEW;
				newStance->fTimeInState = 0.0f;
				newStance->fTimeActive = pSkeleton->eaStanceTimers[i][ipcStanceIndex];

				//add it to our list
				eaPush(&pDebugSkeleton->eaDebugStances, newStance);
			}
		}
		FOR_EACH_END;
	}
	ARRAY_FOREACH_END;

	//update stance words
	FOR_EACH_IN_EARRAY(pDebugSkeleton->eaDebugStances, DynSkeletonDebugStance, updateStance)
	{
		//check if the stance is currently on the skeleton
		bool bStanceOnSkel = false;
		ARRAY_FOREACH_BEGIN(pSkeleton->eaStances, i);
		if (eaFindString(&pSkeleton->eaStances[i], updateStance->pcStanceName) >= 0) {
			bStanceOnSkel = true;
			break;
		}
		ARRAY_FOREACH_END;

		//mark passage of time
		updateStance->fTimeInState += fDeltaTime;

		//change states
		if (updateStance->iState == DDNAS_STANCE_STATE_NEW)
		{
			if (!bStanceOnSkel)
			{
				updateStance->iState = DDNAS_STANCE_STATE_OLD;
				updateStance->fTimeInState = fDeltaTime;
			}
			else if (updateStance->fTimeInState > DDNAS_STANCE_MAXTIME_NEW)
			{
				updateStance->iState = DDNAS_STANCE_STATE_CURRENT;
				updateStance->fTimeInState = fDeltaTime;
			}
		}
		else if (updateStance->iState == DDNAS_STANCE_STATE_CURRENT)
		{
			if (!bStanceOnSkel)
			{
				updateStance->iState = DDNAS_STANCE_STATE_OLD;
				updateStance->fTimeInState = fDeltaTime;
			}
		}
		else if (updateStance->iState == DDNAS_STANCE_STATE_OLD)
		{
			if (updateStance->fTimeInState > DDNAS_STANCE_MAXTIME_OLD)
			{
				eaRemoveFast(&pDebugSkeleton->eaDebugStances, iupdateStanceIndex);
				free(updateStance);
			}
		}
	}
	FOR_EACH_END;

	//sort the stance words
	eaQSort(pDebugSkeleton->eaDebugStances, dynDebugCmpStances);

	//animation graph section
	EARRAY_CONST_FOREACH_BEGIN(pSkeleton->eaAGUpdater, i, iSize);
	{
		if (eaSize(&pDebugSkeleton->eaGraphUpdaters)) //safety check for login screen
		{
			//update keyword timings
			FOR_EACH_IN_EARRAY(pDebugSkeleton->eaGraphUpdaters[i]->eaKeywords, DynSkeletonDebugKeyword, updateKeyword)
			{
				if (updateKeyword->fTimeSinceTriggered > 0.0f) {
					updateKeyword->fTimeSinceTriggered += fDeltaTime;
					if (updateKeyword->fTimeSinceTriggered > DDNAS_KEYWORD_MAXTIME) {
						eaRemoveFast(&pDebugSkeleton->eaGraphUpdaters[i]->eaKeywords, iupdateKeywordIndex);
						free(updateKeyword);
					}
				}
			}
			FOR_EACH_END;

			//update flag timings
			FOR_EACH_IN_EARRAY(pDebugSkeleton->eaGraphUpdaters[i]->eaFlags, DynSkeletonDebugFlag, updateFlag)
			{
				if (updateFlag->fTimeSinceTriggered > 0.0f) {
					updateFlag->fTimeSinceTriggered += fDeltaTime;
					if (updateFlag->fTimeSinceTriggered > DDNAS_FLAG_MAXTIME)
					{
						eaRemoveFast(&pDebugSkeleton->eaGraphUpdaters[i]->eaFlags, iupdateFlagIndex);
						free(updateFlag);
					}
				}
			}
			FOR_EACH_END;

			//update FX timings
			FOR_EACH_IN_EARRAY(pDebugSkeleton->eaGraphUpdaters[i]->eaFX, DynSkeletonDebugFX, updateFX)
			{
				updateFX->fTimeSinceTriggered += fDeltaTime;
				if (updateFX->fTimeSinceTriggered > DDNAS_FX_MAXTIME)
				{
					eaRemoveFast(&pDebugSkeleton->eaGraphUpdaters[i]->eaFX, iupdateFXIndex);
					free(updateFX);
				}
			}
			FOR_EACH_END;
		}
	}
	EARRAY_FOREACH_END;

	if (!gConf.bUseMovementGraphs)
	{
		//add new movement blocks
		EARRAY_CONST_FOREACH_BEGIN(pSkeleton->movement.eaBlocks, k, ksize);
		{
			const char *blockName = dynMovementBlockGetMoveName(pSkeleton->movement.eaBlocks[k]);
			const char *blockType = dynMovementBlockGetMovementType(pSkeleton->movement.eaBlocks[k]);
			const DynMoveTransition   *blockTrans = dynMovementBlockGetTransition(pSkeleton->movement.eaBlocks[k]);
			const DynAnimChartRunTime *blockChart = dynMovementBlockGetChart(pSkeleton->movement.eaBlocks[k]);
			int curNumMovementBlocks = eaSize(&pDebugSkeleton->eaMovementBlocks);
			if (k == curNumMovementBlocks)
			{
				DynSkeletonDebugMovementBlock *newBlock = StructCreate(parse_DynSkeletonDebugMovementBlock);
				newBlock->pcName = blockName;
				newBlock->pcType = blockType;
				newBlock->pcTDes = SAFE_MEMBER(blockTrans,pcName);
				newBlock->pcCDes = SAFE_MEMBER(blockChart,pcName);
				eaPush(&pDebugSkeleton->eaMovementBlocks, newBlock);
			}
			else if (k < curNumMovementBlocks &&
					(	pDebugSkeleton->eaMovementBlocks[k]->pcName != blockName ||
						pDebugSkeleton->eaMovementBlocks[k]->pcType != blockType))
			{
				DynSkeletonDebugMovementBlock *newBlock = StructCreate(parse_DynSkeletonDebugMovementBlock);
				newBlock->pcName = blockName;
				newBlock->pcType = blockType;
				newBlock->pcTDes = SAFE_MEMBER(blockTrans,pcName);
				newBlock->pcCDes = SAFE_MEMBER(blockChart,pcName);
				eaInsert(&pDebugSkeleton->eaMovementBlocks, newBlock, k);
			}
		}
		EARRAY_FOREACH_END;

		//free the oldest movement block
		while (eaSize(&pDebugSkeleton->eaMovementBlocks) > DDNAS_MOVEMENTBLOCK_MAXCOUNT)
		{
			DynSkeletonDebugMovementBlock *delBlock = pDebugSkeleton->eaMovementBlocks[DDNAS_MOVEMENTBLOCK_MAXCOUNT];
			eaRemoveFast(&pDebugSkeleton->eaMovementBlocks, DDNAS_MOVEMENTBLOCK_MAXCOUNT);
			free(delBlock);
		}
	}

	//update move FX timings
	FOR_EACH_IN_EARRAY(pDebugSkeleton->eaMovementFX, DynSkeletonDebugFX, updateMoveFX)
	{
		updateMoveFX->fTimeSinceTriggered += fDeltaTime;
		if (updateMoveFX->fTimeSinceTriggered > DDNAS_FX_MAXTIME)
		{
			eaRemoveFast(&pDebugSkeleton->eaMovementFX, iupdateMoveFXIndex);
			free(updateMoveFX);
		}
	}
	FOR_EACH_END;

	PERFINFO_AUTO_STOP();
}

void dynDebugStateSetSkeleton(DynSkeleton* pSkeleton)
{
	if (pSkeleton == dynDebugState.pDebugSkeleton ||
		dynDebugState.bLockDebugSkeleton && pSkeleton != NULL)
		return;
	
	dynDebugState.pDebugSkeleton = pSkeleton;
 
	if (pSkeleton == NULL)
		dynDebugState.pBoneUnderMouse = NULL;

	if (!gConf.bNewAnimationSystem)
	{
		eaClearEx(&dynDebugState.eaSkelBits, NULL);
		if (pSkeleton) dynDebugSkeletonUpdateBits(pSkeleton, 0.0f);
	}
}

S32 getNumEntities(void)
{
	if (wl_state.get_num_entities_func)
		return wl_state.get_num_entities_func();
	return -1;
}

S32 getNumClientOnlyEntities(void)
{
	if (wl_state.get_num_client_only_entities_func)
		return wl_state.get_num_client_only_entities_func();
	return -1;
}

void dynSkeletonShowModelsAttachedToBone(DynSkeleton* pSkeleton, const char *pcBoneTagName, bool bShow)
{
	if (pSkeleton && pSkeleton->pDrawSkel)
	{
		dynDrawSkeletonShowModelsAttachedToBone(pSkeleton->pDrawSkel, pcBoneTagName, bShow);
	}
}

void dynSkeletonSetTalkBit(DynSkeleton* pSkeleton, bool bEnabled)
{
	DynBit bit = dynBitFromName("TALKING");
	if(bit)
	{
		if(bEnabled)
		{
			dynBitFieldBitSet(&pSkeleton->talkingBits, bit);
		}
		else
		{
			dynBitFieldBitClear(&pSkeleton->talkingBits, bit);
		}
	}
}

static bool danimEchoKeywords=false;
// Echos keywords as they are played
AUTO_CMD_INT(danimEchoKeywords, danimEchoKeywords) ACMD_COMMANDLINE;

static bool dynSkeletonStartGraphInternal(	DynSkeleton* pSkeleton,
											const char* pcGraph,
											U32 uid,
											S32 mustBeDetailGraph)
{
	bool bRet=false;
	if (danimEchoKeywords && pSkeleton == dynDebugState.pDebugSkeleton)
	{
		const char** eaStances = NULL;
		ARRAY_FOREACH_BEGIN(pSkeleton->eaStances, i);
		{
			EARRAY_CONST_FOREACH_BEGIN(pSkeleton->eaStances[i], j, jsize);
			{
				eaPushUnique(&eaStances, pSkeleton->eaStances[i][j]);
			}
			EARRAY_FOREACH_END;
		}
		ARRAY_FOREACH_END;
		printf("%p: %s  Stances:", pSkeleton, pcGraph);
		FOR_EACH_IN_EARRAY(eaStances, const char, pcStance)
		{
			printf("%s ", pcStance);
		}
		FOR_EACH_END;
		printf("\n");
		eaDestroy(&eaStances);
	}
	assert(gConf.bNewAnimationSystem);
	if(!pcGraph){
		if(!mustBeDetailGraph){
			dynSkeletonResetToADefaultGraph(pSkeleton, "passed NULL keyword", 1, 0);
		}
	}else{
		FOR_EACH_IN_EARRAY_FORWARDS(pSkeleton->eaAGUpdater, DynAnimGraphUpdater, pUpdater)
		{
			bool bStartGraph;

			if (gConf.bUseMovementGraphs &&
				ipUpdaterIndex == 1
				||
				gConf.bUseNWOMovementAndAnimationOptions &&
				bRet &&
				pUpdater->bOverlay)
			{
				// Don't trigger movement graphs through this mechanism
				// Already started playing a regular animation, don't play on the overlay
				continue; 
			}

			bRet |= bStartGraph = dynAnimGraphUpdaterStartGraph(pSkeleton, pUpdater, pcGraph, uid, mustBeDetailGraph);

			if (bStartGraph && (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits)) {
				dynDebugSkeletonStartGraph(pSkeleton, pUpdater, pcGraph, pUpdater->uidCurrentGraph);
			}
		}
		FOR_EACH_END;
	}
	return bRet;
}

bool dynSkeletonStartGraph(DynSkeleton* pSkeleton, const char* pcGraph, U32 uid)
{
	bool bRet;
	pSkeleton->bStartedGraphInPreUpdate = true;
	bRet = dynSkeletonStartGraphInternal(pSkeleton, pcGraph, uid, 0);
	
	FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton) {
		if (pChildSkeleton->bInheritBits) {
			dynSkeletonStartGraphInternal(pChildSkeleton, pcGraph, uid, 0);
		}
	} FOR_EACH_END;
	
	return bRet;
}

bool dynSkeletonStartDetailGraph(DynSkeleton* pSkeleton, const char* pcGraph, U32 uid)
{
	bool bRet = dynSkeletonStartGraphInternal(pSkeleton, pcGraph, uid, 1);

	FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton) {
		if (pChildSkeleton->bInheritBits) {
			dynSkeletonStartGraphInternal(pChildSkeleton, pcGraph, uid, 1);
		}
	} FOR_EACH_END;

	return bRet;
}

void dynSkeletonResetToADefaultGraph(	DynSkeleton* pSkeleton,
										const char *pcCallersReason,
										S32 onlyIfLooping,
										S32 callChildren )
{
	assert(gConf.bNewAnimationSystem);

	FOR_EACH_IN_EARRAY(pSkeleton->eaAGUpdater, DynAnimGraphUpdater, pUpdater) {
		dynAnimGraphUpdaterResetToADefaultGraph(pSkeleton, pUpdater, NULL, pcCallersReason, onlyIfLooping, 0, 0);
	} FOR_EACH_END;

	if (callChildren) {
		FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton) {
			if (pChildSkeleton->bInheritBits) {
				dynSkeletonResetToADefaultGraph(pChildSkeleton, pcCallersReason, onlyIfLooping, callChildren);
			}
		} FOR_EACH_END;
	}
}

void dynSkeletonSetFlag(DynSkeleton* pSkeleton, const char* pcFlag, U32 uid)
{
	assert(gConf.bNewAnimationSystem);

	FOR_EACH_IN_EARRAY(pSkeleton->eaAGUpdater, DynAnimGraphUpdater, pUpdater)
	{
		if (!gConf.bUseMovementGraphs || ipUpdaterIndex != 1) {
			bool bSetFlag = dynAnimGraphUpdaterSetFlag(pUpdater, pcFlag, uid);
			if (bSetFlag && (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits)) {
				dynDebugSkeletonSetFlag(pSkeleton, pUpdater, pcFlag, uid);
			}
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton) {
		if (pChildSkeleton->bInheritBits) {
			dynSkeletonSetFlag(pChildSkeleton, pcFlag, uid);
		}
	} FOR_EACH_END;
}

void dynSkeletonSetDetailFlag(DynSkeleton *pSkeleton, const char *pcFlag, U32 uid)
{
	assert(gConf.bNewAnimationSystem);

	FOR_EACH_IN_EARRAY(pSkeleton->eaAGUpdater, DynAnimGraphUpdater, pUpdater)
	{
		if (!gConf.bUseMovementGraphs || ipUpdaterIndex != -1) {
			bool bSetFlag = dynAnimGraphUpdaterSetDetailFlag(pUpdater, pcFlag, uid);
			if (bSetFlag && (dynDebugState.danimShowBits || dynDebugState.audioShowAnimBits)) {
				dynDebugSkeletonSetFlag(pSkeleton, pUpdater, pcFlag, uid);
			}
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton) {
		if (pChildSkeleton->bInheritBits) {
			dynSkeletonSetDetailFlag(pChildSkeleton, pcFlag, uid);
		}
	} FOR_EACH_END;
}

void dynSkeletonSetOverrideTime(DynSkeleton *pSkeleton, F32 fTime, U32 uiApply)
{
	assert(gConf.bNewAnimationSystem);
	FOR_EACH_IN_EARRAY(pSkeleton->eaAGUpdater, DynAnimGraphUpdater, pUpdater)
	{
		if (!gConf.bUseMovementGraphs || ipUpdaterIndex != 1) {
			dynAnimGraphUpdaterSetOverrideTime(pUpdater, fTime, uiApply);
		}
	}
	FOR_EACH_END;
}

// for when animation data reloads
static void dynSkeletonResetAnims(DynSkeleton* pSkeleton)
{
	dynSkeletonCreateSequencers(pSkeleton);
}

static void dynSkeletonResetCB(MemoryPool pool, void *data, void *userData)
{
	dynSkeletonResetAnims(data);
}

void dynSkeletonResetAllAnims(void)
{
	// Must do this instead of iterating on eaDynSkeletons because unmanaged skeletons are not in the list
	//dynDebugStateSetSkeleton(NULL);
	mpForEachAllocation(MP_NAME(DynSkeleton), dynSkeletonResetCB, NULL);
	dynSkeletonValidateAll();
}


static void dynSkeletonResetBouncersOnSkel(DynSkeleton* pSkeleton)
{
	const WLCostume* pCostume = GET_REF(pSkeleton->hCostume);
	const SkelInfo* pSkelInfo = pCostume?GET_REF(pCostume->hSkelInfo):NULL;
	if (pSkelInfo)
		dynSkeletonSetupBouncers(pSkeleton, pSkelInfo);
}

static void dynSkeletonResetBouncersCB(MemoryPool pool, void *data, void *userData)
{
	dynSkeletonResetBouncersOnSkel(data);
}

void dynSkeletonResetBouncers(void)
{
	// Must do this instead of iterating on eaDynSkeletons because unmanaged skeletons are not in the list
	mpForEachAllocation(MP_NAME(DynSkeleton), dynSkeletonResetBouncersCB, NULL);
}

static void dynSkeletonSetStanceWordInternal(DynSkeleton* pSkeleton, const char* pcStance, U32 uiMovement)
{
	DynAnimStanceData *stance_data = NULL;

	pSkeleton->bNotifyChartStackChanged = 1;

	if (pcStance == pcStanceRuntimeFreeze) {
		pSkeleton->bFrozen = true;
	}
	else if(gConf.bUseMovementGraphs &&
			(	pcStance == pcStanceFalling ||
				pcStance == pcStanceJumpFalling))
	{
		pSkeleton->bLanded = false;
		FOR_EACH_IN_EARRAY(pSkeleton->eaAnimChartStacks, DynAnimChartStack, pChartStack) {
			eaFindAndRemoveFast(&pChartStack->eaStanceFlags, pcStanceLanded);
		} FOR_EACH_END;
	}

	FOR_EACH_IN_EARRAY(pSkeleton->eaAnimChartStacks, DynAnimChartStack, pChartStack) {
		dynAnimChartStackSetStanceWord(pChartStack, pcStance, pSkeleton->eaStances[DS_STANCE_SET_PRE_UPDATE], pSkeleton->eaStances[DS_STANCE_SET_DEBUG], uiMovement);
	} FOR_EACH_END;

	//Update our bone visibility flags and banking override node if necessary.
	if (stashFindPointer(stance_list.stStances, pcStance, &stance_data))
	{
		if (stance_data->eVisSet >= 0) {
			dynSkeletonUpdateBoneVisibility(pSkeleton, NULL);
		}

		if (pSkeleton->pcBankingOverrideNode &&
			stance_data->pcBankingNodeOverride)
		{
			pSkeleton->uiBankingOverrideStanceCount++;
			if (pSkeleton->uiBankingOverrideStanceCount == 1) {
				pSkeleton->fBankingOverrideTimeActive = 0.f;
			}
		}
	}
}

static void dynSkeletonClearStanceWordInternal(DynSkeleton* pSkeleton, const char* pcStance, U32 uiMovement)
{
	DynAnimStanceData *stance_data = NULL;

	pSkeleton->bNotifyChartStackChanged = 1;

	if (pcStance == pcStanceRuntimeFreeze)
	{
		pSkeleton->bFrozen = false;
		dynSkeletonSetFlag(pSkeleton, pcInterrupt, 0);
	}
	else if(gConf.bUseMovementGraphs &&
			(	pcStance == pcStanceFalling ||
				pcStance == pcStanceJumpFalling))
	{
		pSkeleton->bLanded = true;
		FOR_EACH_IN_EARRAY(pSkeleton->eaAnimChartStacks, DynAnimChartStack, pChartStack) {
			if (pChartStack->bMovement)
				eaPush(&pChartStack->eaStanceFlags, pcStanceLanded);
		} FOR_EACH_END;
	}

	FOR_EACH_IN_EARRAY(pSkeleton->eaAnimChartStacks, DynAnimChartStack, pChartStack) {
		dynAnimChartStackClearStanceWord(pChartStack, pcStance, pSkeleton->eaStances[DS_STANCE_SET_PRE_UPDATE], pSkeleton->eaStances[DS_STANCE_SET_PRE_UPDATE], uiMovement);
	} FOR_EACH_END;

	//Update our bone visibility flags and banking override node if necessary.
	if (stashFindPointer(stance_list.stStances, pcStance, &stance_data))
	{
		if (stance_data->eVisSet >= 0) {
			dynSkeletonUpdateBoneVisibility(pSkeleton, NULL);
		}

		if (pSkeleton->pcBankingOverrideNode &&
			stance_data->pcBankingNodeOverride)
		{
			pSkeleton->uiBankingOverrideStanceCount--;
			if (pSkeleton->uiBankingOverrideStanceCount == 0) {
				pSkeleton->fBankingOverrideTimeActive = 0.f;
			}
		}
	}
}

void dynSkeletonCopyStanceWords(	const DynSkeleton *pSkeleton,
									const char ***pDestWords)
{
	ARRAY_FOREACH_BEGIN(pSkeleton->eaStances, i);
		eaPushEArray(pDestWords, &pSkeleton->eaStances[i]);
	ARRAY_FOREACH_END;
}

static S32 dynSkeletonStanceIsSet(	const DynSkeleton* pSkeleton,
									DynSkeletonStanceSetType setToIgnore,
									const char* pcStance)
{
	ARRAY_FOREACH_BEGIN(pSkeleton->eaStances, i);
	{
		if(	i != setToIgnore &&
			eaFind(&pSkeleton->eaStances[i], pcStance) >= 0)
		{
			return 1;
		}
	}
	ARRAY_FOREACH_END;
	return 0;
}

static void dynSkeletonSetStanceWordInSet(	DynSkeleton* pSkeleton,
											DynSkeletonStanceSetType set,
											const char* pcStance)
{
	assert(set >= 0 && set < ARRAY_SIZE(pSkeleton->eaStances));

	if (eaFind(&pSkeleton->eaStances[set], pcStance) < 0)
	{
		eaPush(&pSkeleton->eaStances[set], pcStance);
		eafPush(&pSkeleton->eaStanceTimers[set], 0.0f);

		if(!dynSkeletonStanceIsSet(pSkeleton, set, pcStance)){
			dynSkeletonSetStanceWordInternal(pSkeleton, pcStance, DS_STANCE_SET_CONTAINS_MOVEMENT(set));
		}
	}

	FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton) {
		if (pChildSkeleton->bInheritBits) {
			dynSkeletonSetStanceWordInSet(pChildSkeleton, set, pcStance);
		}
	} FOR_EACH_END;
}

static void dynSkeletonClearStanceWordInSet(DynSkeleton* pSkeleton,
											DynSkeletonStanceSetType set,
											const char* pcStance)
{
	int i;

	assert(set >= 0 && set < ARRAY_SIZE(pSkeleton->eaStances));

	i = eaFindAndRemove(&pSkeleton->eaStances[set], pcStance);

	if (i >= 0)
	{
		eaPush(&pSkeleton->eaStancesCleared, pcStance);
		eafPush(&pSkeleton->eaStanceTimersCleared, pSkeleton->eaStanceTimers[set][i]);
		eafRemove(&pSkeleton->eaStanceTimers[set], i);

		if (!dynSkeletonStanceIsSet(pSkeleton, set, pcStance)) {
			dynSkeletonClearStanceWordInternal(pSkeleton, pcStance, DS_STANCE_SET_CONTAINS_MOVEMENT(set));
		}		
	}

	FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton) {
		if (pChildSkeleton->bInheritBits) {
			dynSkeletonClearStanceWordInSet(pChildSkeleton, set, pcStance);
		}
	} FOR_EACH_END;
}

static void dynSkeletonClearStanceWordsInSet(	DynSkeleton* pSkeleton,
												DynSkeletonStanceSetType set)
{
	assert(set >= 0 && set < ARRAY_SIZE(pSkeleton->eaStances));
	
	while(eaSize(&pSkeleton->eaStances[set])){
		dynSkeletonClearStanceWordInSet(pSkeleton,
										set,
										pSkeleton->eaStances[set][0]);
	}
}

static void dynSkeletonClearNonListedStanceWordsInSet(	DynSkeleton *pSkeleton,
														DynSkeletonStanceSetType set,
														const char **ppchExcludeStances)
{
	int i;

	assert(set >= 0 && set < ARRAY_SIZE(pSkeleton->eaStances));

	for (i = eaSize(&pSkeleton->eaStances[set]) - 1; i >= 0; i--) {
		if (eaFind(&ppchExcludeStances, pSkeleton->eaStances[set][i]) < 0) {
			dynSkeletonClearStanceWordInSet(pSkeleton, set, pSkeleton->eaStances[set][i]);
		}
	}
}

void dynSkeletonSetAudioStanceWord(DynSkeleton* pSkeleton, const char* pcStance)
{
	dynSkeletonSetStanceWordInSet(pSkeleton, DS_STANCE_SET_AUDIO, pcStance);
}
void dynSkeletonClearAudioStanceWord(DynSkeleton* pSkeleton, const char* pcStance)
{
	dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_AUDIO, pcStance);
}
void dynSkeletonClearAudioStances(DynSkeleton* pSkeleton)
{
	dynSkeletonClearStanceWordsInSet(pSkeleton, DS_STANCE_SET_AUDIO);
}

void dynSkeletonSetCostumeStanceWord(DynSkeleton* pSkeleton, const char* pcStance)
{
	dynSkeletonSetStanceWordInSet(pSkeleton, DS_STANCE_SET_COSTUME, pcStance);
}
void dynSkeletonClearCostumeStanceWord(DynSkeleton* pSkeleton, const char* pcStance)
{
	dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_COSTUME, pcStance);
}
void dynSkeletonClearCostumeStances(DynSkeleton* pSkeleton)
{
	dynSkeletonClearStanceWordsInSet(pSkeleton, DS_STANCE_SET_COSTUME);
}

void dynSkeletonSetCutsceneStanceWord(DynSkeleton* pSkeleton, const char* pcStance)
{
	dynSkeletonSetStanceWordInSet(pSkeleton, DS_STANCE_SET_CUTSCENE, pcStance);
}
void dynSkeletonClearCutsceneStanceWord(DynSkeleton* pSkeleton, const char* pcStance)
{
	dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_CUTSCENE, pcStance);
}
void dynSkeletonClearCutsceneStances(DynSkeleton* pSkeleton)
{
	dynSkeletonClearStanceWordsInSet(pSkeleton, DS_STANCE_SET_CUTSCENE);
}

void dynSkeletonSetCritterStanceWord(DynSkeleton* pSkeleton, const char* pcStance)
{
	dynSkeletonSetStanceWordInSet(pSkeleton, DS_STANCE_SET_CRITTER, pcStance);
}
void dynSkeletonClearCritterStanceWord(DynSkeleton* pSkeleton, const char* pcStance)
{
	dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_CRITTER, pcStance);
}
void dynSkeletonClearCritterStances(DynSkeleton* pSkeleton)
{
	dynSkeletonClearStanceWordsInSet(pSkeleton, DS_STANCE_SET_CRITTER);
}

static void dynSkeletonUpdateStanceState(DynSkeleton *pSkeleton)
{
	bool bWasFalling = pSkeleton->bIsFalling;
	bool bWasJumping = pSkeleton->bIsJumping;

	pSkeleton->bWasLunging = pSkeleton->bIsLunging;
	pSkeleton->bWasLurching = pSkeleton->bIsLurching;

	pSkeleton->bIsDead		= false;
	pSkeleton->bIsNearDead	= false;
	pSkeleton->bIsRising	= false;
	pSkeleton->bIsFalling	= false;
	pSkeleton->bIsJumping	= false;
	pSkeleton->bIsDragonTurn= false;
	pSkeleton->bIsLunging	= false;
	pSkeleton->bIsLurching	= false;

	ARRAY_FOREACH_BEGIN(pSkeleton->eaStances, i);
	{
		FOR_EACH_IN_CONST_EARRAY(pSkeleton->eaStances[i], char, pcStance)
		{
			if      (pcStance == pcStanceDeath)			pSkeleton->bIsDead		= true;
			else if (pcStance == pcStanceNearDeath)		pSkeleton->bIsNearDead	= true;
			else if (pcStance == pcStanceRising)		pSkeleton->bIsRising	= true;
			else if (pcStance == pcStanceJumpRising)	pSkeleton->bIsRising	= true;
			else if (pcStance == pcStanceFalling)		pSkeleton->bIsFalling	= true;
			else if (pcStance == pcStanceJumpFalling)	pSkeleton->bIsFalling	= true;
			else if (pcStance == pcStanceJumping)		pSkeleton->bIsJumping	= true;
			else if (pcStance == pcStanceDragonTurn)	pSkeleton->bIsDragonTurn= true;
			else if (pcStance == pcStanceLunging)		pSkeleton->bIsLunging	= true;
			else if (pcStance == pcStanceLurching)		pSkeleton->bIsLurching	= true;
		}
		FOR_EACH_END;
	}
	ARRAY_FOREACH_END;

	if (pSkeleton->pFxManager)
	{
		if (!bWasJumping)
		{
			if (pSkeleton->bIsJumping) {
				dynSkeletonBroadcastFXMessage(pSkeleton->pFxManager, pcGruntJump);
			}
			if (bWasFalling && !pSkeleton->bIsFalling) {
				dynSkeletonBroadcastFXMessage(pSkeleton->pFxManager, pcGruntKnockback);
			}
		}
		else //if (bWasJumping)
		{ 
			if (!pSkeleton->bIsJumping) {
				dynSkeletonBroadcastFXMessage(pSkeleton->pFxManager, pcGruntLand);
			}
		}
	}
}

void dynSkeletonValidateCB(MemoryPool pool, void *data, void *userData)
{
	DynSkeleton *pSkeleton = data;
	if (gConf.bNewAnimationSystem)
	{
		char buf[1024];
		#define VALIDATE_STRING(str) strcpy(buf, str)
		FOR_EACH_IN_EARRAY_FORWARDS(pSkeleton->eaAGUpdater, DynAnimGraphUpdater, pUpdater)
		{
			const DynAnimChartStack* pChartStack = dynAnimGraphUpdaterGetChartStack(pUpdater);
			if (pChartStack)
			{
				// update the chart stack (has internal check for dirty bit)
				dynAnimChartStackSetFromStanceWords(pUpdater->pChartStack);

				FOR_EACH_IN_EARRAY_FORWARDS(pChartStack->eaChartStack, const DynAnimChartRunTime, pChart)
				{
					VALIDATE_STRING(dynAnimChartGetName(pChart));
				}
				FOR_EACH_END;
			}

			VALIDATE_STRING(dynAnimGraphUpdaterGetCurrentGraphName(pUpdater));
			{
				DynAnimGraphUpdaterNode* pCurrentNode = dynAnimGraphUpdaterGetCurrentNode(pUpdater);
				if (pCurrentNode)
				{
					VALIDATE_STRING(dynAnimGraphNodeGetMoveName(pCurrentNode));
					dynAnimGraphNodeGetFrameTime(pCurrentNode);
				}
			}
		}
		FOR_EACH_END;
		if (!gConf.bUseMovementGraphs)
		{
			EARRAY_CONST_FOREACH_BEGIN(pSkeleton->movement.eaBlocks, i, isize);
			{
				DynMovementBlock* b = pSkeleton->movement.eaBlocks[i];
				VALIDATE_STRING(dynMovementBlockGetMovementType(b));
				VALIDATE_STRING(dynMovementBlockGetMoveName(b));
				dynMovementBlockGetBlendFactor(b);
				dynMovementBlockGetFrameTime(b);
			}
			EARRAY_FOREACH_END;
		}
		#undef VALIDATE_STRING
	}
}

AUTO_COMMAND;
void dynSkeletonValidateAll(void)
{
	PERFINFO_AUTO_START_FUNC();
	mpForEachAllocation(MP_NAME(DynSkeleton), dynSkeletonValidateCB, NULL);
	PERFINFO_AUTO_STOP();
}

void dynSkeletonSetLogReceiver(	DynSkeleton* s,
								GenericLogReceiver* glr)
{
	if(!s){
		return;
	}

	s->glr = glr;

	EARRAY_CONST_FOREACH_BEGIN(s->eaSqr, i, isize);
	{
		dynSeqSetLogReceiver(s->eaSqr[i], glr);
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(s->eaAGUpdater, i, isize);
	{
		s->eaAGUpdater[i]->glr = glr;
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(s->eaDependentSkeletons, i, isize);
	{
		dynSkeletonSetLogReceiver(s->eaDependentSkeletons[i], glr);
	}
	EARRAY_FOREACH_END;
}

static DynSkeletonAnimOverride* dynSkeletonAnimOverrideFind(DynSkeleton* pSkeleton, U32 handle)
{
	EARRAY_CONST_FOREACH_BEGIN(pSkeleton->eaAnimOverrides, i, isize);
	{
		DynSkeletonAnimOverride* o = pSkeleton->eaAnimOverrides[i];

		if(o->handle == handle){
			return o;
		}
	}
	EARRAY_FOREACH_END;

	return NULL;
}

U32 dynSkeletonAnimOverrideCreate(DynSkeleton* pSkeleton)
{
	static U32 prevHandle;
	DynSkeletonAnimOverride* o = callocStruct(DynSkeletonAnimOverride);

	while(	!++prevHandle ||
			dynSkeletonAnimOverrideFind(pSkeleton, prevHandle));

	o->handle = prevHandle;
	eaPush(&pSkeleton->eaAnimOverrides, o);
	return o->handle;
}

void dynSkeletonAnimOverrideDestroy(DynSkeleton* pSkeleton,
									U32* overrideHandleInOut)
{
	DynSkeletonAnimOverride* o = dynSkeletonAnimOverrideFind(pSkeleton, *overrideHandleInOut);

	*overrideHandleInOut = 0;

	if(o){
		eaFindAndRemove(&pSkeleton->eaAnimOverrides, o);
		SAFE_FREE(o);

		if(!eaSize(&pSkeleton->eaAnimOverrides)){
			eaDestroy(&pSkeleton->eaAnimOverrides);
		}
	}
}

void dynSkeletonAnimOverrideStartGraph(	DynSkeleton* pSkeleton,
										U32 overrideHandle,
										const char* keyword,
										S32 onlyRestartIfNew)
{
	DynSkeletonAnimOverride* o = dynSkeletonAnimOverrideFind(pSkeleton, overrideHandle);

	if(o){
		keyword = allocAddString(keyword);

		if(	!onlyRestartIfNew ||
			keyword != o->keyword)
		{
			o->keyword = keyword;
			o->playedKeyword = 0;
		}
	}
}

void dynSkeletonAnimOverrideSetStance(	DynSkeleton* pSkeleton,
										U32 overrideHandle,
										const char* stance)
{
	DynSkeletonAnimOverride* o = dynSkeletonAnimOverrideFind(pSkeleton, overrideHandle);

	if(o){
		stance = allocAddString(stance);
		eaPushUnique(&o->stances, stance);
	}
}

void dynSkeletonAnimOverrideClearStance(DynSkeleton* pSkeleton,
										U32 overrideHandle,
										const char* stance)
{
	DynSkeletonAnimOverride* o = dynSkeletonAnimOverrideFind(pSkeleton, overrideHandle);

	if(o){
		stance = allocAddString(stance);
		eaFindAndRemove(&o->stances, stance);
	}
}

#define TERRAIN_PITCH_RATE RAD(180)
#define TERRAIN_AIRBORNE_PITCH_RATE RAD(45)

static void dynSkeletonCalcTerrainPitchSetup(DynSkeleton *pSkeleton)
{
	WLCostume *wlCostume = GET_REF(pSkeleton->hCostume);

	if (!dynDebugState.bDisableTerrainTilt	&&
		pSkeleton->bVisible					&&
		pSkeleton->pRoot->pParent			&&
		!pSkeleton->ragdollState.bRagdollOn	&&
		SAFE_MEMBER(wlCostume, bTerrainTiltApply))
	{
		Quat qRoot;
		Vec3 vRootPos;
		Vec3 vScale;

		dynNodeGetWorldSpacePos(pSkeleton->pRoot->pParent, vRootPos);
		dynNodeGetWorldSpaceRot(pSkeleton->pRoot, qRoot);
		dynNodeGetWorldSpaceScale(pSkeleton->pRoot, vScale);
		quatRotateVec3Inline(qRoot, forwardvec, pSkeleton->vTerrainLook);

		if (pSkeleton->vTerrainLook[1] >= 0.9999f) {
			copyVec3(forwardvec, pSkeleton->vTerrainLook);
		} else {
			pSkeleton->vTerrainLook[1] = 0.f;
			normalVec3(pSkeleton->vTerrainLook);
		}

		if (wlCostume->fTerrainTiltBaseLength > 0.f)
		{
			Vec3 vPosA, vPosB, vPosC;
			F32 fHalfBaseLength;
			Vec3 vImpactNorm;

			fHalfBaseLength = .5f * wlCostume->fTerrainTiltBaseLength * vScale[2];
			scaleAddVec3(pSkeleton->vTerrainLook,  fHalfBaseLength, vRootPos, vPosA);
			copyVec3(vRootPos, vPosB);
			scaleAddVec3(pSkeleton->vTerrainLook, -fHalfBaseLength, vRootPos, vPosC);

			if (dynDebugState.bDrawTerrainTilt &&
				wl_state.drawLine3D_2_func)
			{
				wl_state.drawLine3D_2_func(vPosA, vPosB, 0xFFFF0000, 0xFFFF0000);
				wl_state.drawLine3D_2_func(vPosB, vPosC, 0xFFFF0000, 0xFFFF0000);
			}

			//+3.0f represents the max slope uphill, -1.f represents the max slop downhill (hence the 4*fHalfBaseLength)
			//posB is only nudged slightly since I just want it to be inside of the collision capsule & check if the actual bottom point is different
			vPosA[1] += 3.f*fHalfBaseLength;
			vPosB[1] += 0.1f;
			vPosC[1] += 3.f*fHalfBaseLength;

			pSkeleton->bTerrainHitPosA = dynAnimPhysicsRaycastToGround(0, vPosA, 4*fHalfBaseLength,   pSkeleton->vTerrainImpactPosA, vImpactNorm);
			pSkeleton->bTerrainHitPosB = dynAnimPhysicsRaycastToGround(0, vPosB, fHalfBaseLength+.1f, pSkeleton->vTerrainImpactPosB, pSkeleton->vTerrainNorm);
			pSkeleton->bTerrainHitPosC = dynAnimPhysicsRaycastToGround(0, vPosC, 4*fHalfBaseLength,   pSkeleton->vTerrainImpactPosC, vImpactNorm);

			if (!pSkeleton->bTerrainHitPosA) {copyVec3(vPosA, pSkeleton->vTerrainImpactPosA); pSkeleton->vTerrainImpactPosA[1] -= 4*fHalfBaseLength;}
			if (!pSkeleton->bTerrainHitPosB) {copyVec3(vPosB, pSkeleton->vTerrainImpactPosB); pSkeleton->vTerrainImpactPosB[1] -= fHalfBaseLength+.1f; copyVec3(upvec,pSkeleton->vTerrainNorm);}
			if (!pSkeleton->bTerrainHitPosC) {copyVec3(vPosC, pSkeleton->vTerrainImpactPosC); pSkeleton->vTerrainImpactPosC[1] -= 4*fHalfBaseLength;}

			if (dynDebugState.bDrawTerrainTilt)
			{
				Vec3 vMin, vMax, vUnit;
				Mat4 mI;

				wl_state.drawLine3D_2_func(vPosA, vPosC, 0xFFFF0000, 0xFFFF0000);
				wl_state.drawLine3D_2_func(vPosA, vPosB, 0xFF880000, 0xFF880000);
				wl_state.drawLine3D_2_func(vPosB, vPosC, 0xFF880000, 0xFF880000);

				wl_state.drawLine3D_2_func(pSkeleton->vTerrainImpactPosA, pSkeleton->vTerrainImpactPosB, 0xFF0000FF, 0xFF0000FF);
				wl_state.drawLine3D_2_func(pSkeleton->vTerrainImpactPosB, pSkeleton->vTerrainImpactPosC, 0xFF0000FF, 0xFF0000FF);

				wl_state.drawLine3D_2_func(pSkeleton->vTerrainImpactPosA, pSkeleton->vTerrainImpactPosC, 0xFF00FFFF, 0xFF00FFFF);

				identityMat4(mI);
				unitVec3(vUnit);
				scaleAddVec3(vUnit,  0.2f, vRootPos, vMax);
				scaleAddVec3(vUnit, -0.2f, vRootPos, vMin);
				wl_state.drawBox3D_func(vMin, vMax, mI, 0xFFFF00FF, 1.f);
			}
		}
		else
		{
			Vec3 vPosB;

			//posB is only nudged slightly since I just want it to be inside of the collision capsule & check if the actual bottom point is different
			copyVec3(vRootPos, vPosB);
			vPosB[1] += 0.1f;

			pSkeleton->bTerrainHitPosB = dynAnimPhysicsRaycastToGround(0, vPosB, 3.f*vScale[2]+.1f, pSkeleton->vTerrainImpactPosB, pSkeleton->vTerrainNorm);

			if (!pSkeleton->bTerrainHitPosB) {
				copyVec3(upvec, pSkeleton->vTerrainNorm);
				copyVec3(vPosB, pSkeleton->vTerrainImpactPosB);
				pSkeleton->vTerrainImpactPosB[1] -= 3.f*vScale[2]+.1f;
			}

			if (dynDebugState.bDrawTerrainTilt)
			{
				Vec3 vMin, vMax, vUnit;
				Mat4 mI;

				identityMat4(mI);
				unitVec3(vUnit);
				scaleAddVec3(vUnit,  0.2f, vRootPos, vMax);
				scaleAddVec3(vUnit, -0.2f, vRootPos, vMin);
				wl_state.drawBox3D_func(vMin, vMax, mI, 0xFFFF00FF, 1.f);
			}
		}
	}
	else
	{
		pSkeleton->bTerrainHitPosA = false;
		pSkeleton->bTerrainHitPosB = false;
		pSkeleton->bTerrainHitPosC = false;
	}
}

static void dynSkeletonCalcTerrainPitch(DynSkeleton *pSkeleton, F32 fDeltaTime)
{
	WLCostume *wlCostume = GET_REF(pSkeleton->hCostume);

	if (pSkeleton->pParentSkeleton)
	{
		//copy terrain pitch physics (should only be used for debugging & setting stances)
		//sub-skeleton physics should only be used for debugging & setting stances, which
		//is why I don't also copy fTerrainOffsetZ & qTerrainPitch here
		pSkeleton->fTerrainPitch = pSkeleton->pParentSkeleton->fTerrainPitch;

		//compute sub-skeleton blend on based on physics and sub-costume
		if (wlCostume->fTerrainTiltMaxBlendAngle > 0.01f) {
			pSkeleton->fTerrainTiltBlend = MIN(fabsf(pSkeleton->fTerrainPitch)/wlCostume->fTerrainTiltMaxBlendAngle, 1.f);
		} else {
			pSkeleton->fTerrainTiltBlend = 0.f;
		}
	}
	else if(!dynDebugState.bDisableTerrainTilt	&&
			pSkeleton->bVisible &&
			!pSkeleton->ragdollState.bRagdollOn &&
			SAFE_MEMBER(wlCostume, bTerrainTiltApply))
	{
		Vec3 vSnapPosA, vSnapPosA_Rotated;
		Vec3 vSnapPosC, vSnapPosC_Rotated;
		Vec3 vScale;
		Vec3 vLook;
		F32 fPitch;

		if (wlCostume->fTerrainTiltBaseLength > 0.f)
		{
			if (pSkeleton->bTerrainHitPosB &&
				(	pSkeleton->bTerrainHitPosA ||
					pSkeleton->bTerrainHitPosC))
			{
				Vec3 vOver, vUp;
				Vec3 v1, v2;
				F32 fAddPitch;

				//find the slopes relative to the center
				subVec3(pSkeleton->vTerrainImpactPosA, pSkeleton->vTerrainImpactPosB, v1);
				subVec3(pSkeleton->vTerrainImpactPosB, pSkeleton->vTerrainImpactPosC, v2);

				//determine the look vector for the critter
				addVec3(v1, v2, vLook); //use an averaged version
				normalVec3(v1);
				normalVec3(v2);
				normalVec3(vLook);
				if (fabsf(dotVec3(v1,v2)) < 0.9063)
				{ //more than approx 25 deg diff between the slopes
					if (fabsf(v1[1]) < fabsf(v2[1]) &&
						fabsf(dotVec3(v1,vLook)) < 0.9063)
					{
						copyVec3(v1, vLook); //v1 is flatter, use it +/- the 25 deg diff limit
						fAddPitch = v2[1] > 0.f ? RAD(25.f) : RAD(-25.f);
					}
					else if (fabsf(dotVec3(v2,vLook)) < 0.9063)
					{
						copyVec3(v2, vLook); //v2 is flatter, use it +/- the 25 deg diff limit
						fAddPitch = v1[1] > 0.f ? RAD(25.f) : RAD(-25.f);
					}
					else
					{
						fAddPitch = 0.f;
					}
				}
				else {
					fAddPitch = 0.f;
				}

				//determine the critters orientation
				if (vLook[1] >= 0.9999f) {
					copyVec3(forwardvec, vLook);
				}
				crossVec3Up(vLook, vOver);
				crossVec3(vLook, vOver, vUp);
				normalVec3(vUp);

				// +/- rotation based on up/down hill direction
				fPitch = SIGN(vLook[1]) * wlCostume->fTerrainTiltStrength * acosf((MINMAX(dotVec3(vUp,upvec),-1.f,1.f))) + fAddPitch;
			}
			else
			{
				copyVec3(pSkeleton->vTerrainLook, vLook);
				fPitch = 0.f;
			}
		}
		else
		{
			Vec3 vOver, vUp;
			crossVec3(pSkeleton->vTerrainNorm, pSkeleton->vTerrainLook, vOver);
			crossVec3(vOver, pSkeleton->vTerrainNorm, vLook);
			crossVec3Up(vLook, vOver);
			crossVec3(vLook, vOver, vUp);
			normalVec3(vUp);
			
			// +/- rotation based on up/down hill direction
			fPitch = SIGN(vLook[1]) * wlCostume->fTerrainTiltStrength * acosf((MINMAX(dotVec3(vUp,upvec),-1.f,1.f)));
		}

		if (TRUE_THEN_RESET(pSkeleton->bInitTerrainPitch) ||
			(	pSkeleton->bVisible &&
				!pSkeleton->bWasVisible))
		{
			//seeing skeleton for the 1st time, assume the current pitch
			pSkeleton->fTerrainPitch = fPitch;
		}
		else
		{
			F32 fPitchDiff, fPitchChange, fPitchRate;

			//don't pitch as quickly in the air since it looks dumb, technically this will lag by a frame
			if (pSkeleton->bIsRising || pSkeleton->bIsFalling) {
				fPitchRate = TERRAIN_AIRBORNE_PITCH_RATE;
			} else {
				fPitchRate = TERRAIN_PITCH_RATE;
			}

			//determine how much pitch is required to realign the skeleton in this new direction
			fPitchDiff = fPitch - pSkeleton->fTerrainPitch;

			//ease the skeleton towards the new pitch
			fPitchChange = SIGN(fPitchDiff) * fPitchRate * fDeltaTime;
			if (fabsf(fPitchChange) > fabsf(fPitchDiff))
				pSkeleton->fTerrainPitch = fPitch;
			else
				pSkeleton->fTerrainPitch += fPitchChange;
		}

		//set the overlay blend
		if (wlCostume->fTerrainTiltMaxBlendAngle > 0.01f) {
			pSkeleton->fTerrainTiltBlend = MIN(fabsf(pSkeleton->fTerrainPitch)/wlCostume->fTerrainTiltMaxBlendAngle, 1.f);
		} else {
			pSkeleton->fTerrainTiltBlend = 0.f;
		}

		//determine the pitch quaternion value
		pitchQuat(pSkeleton->fTerrainPitch, pSkeleton->qTerrainPitch);

		//determine value used for computing the offset
		dynNodeGetWorldSpaceScale(pSkeleton->pRoot, vScale);
		vSnapPosA[0] = vSnapPosC[0] = 0.f;
		vSnapPosA[1] = vSnapPosC[1] = 0.f;
		vSnapPosA[2] = 0.5f * wlCostume->fTerrainTiltBaseLength * vScale[2];
		vSnapPosC[2] = -.5f * wlCostume->fTerrainTiltBaseLength * vScale[2];
		quatRotateVec3Inline(pSkeleton->qTerrainPitch, vSnapPosA, vSnapPosA_Rotated);
		quatRotateVec3Inline(pSkeleton->qTerrainPitch, vSnapPosC, vSnapPosC_Rotated);

		//apply offset changes
		pSkeleton->fTerrainOffsetZ = 0.5f * SIGN(vLook[1]) *
									(	(vSnapPosA[2] - vSnapPosA_Rotated[2]) +
										(vSnapPosC_Rotated[2] - vSnapPosC[2]));

		//pSkeleton->fTerrainHeightBump = .5f*
		//						(	(pSkeleton->vTerrainImpactPosA[1]-pSkeleton->vTerrainImpactPosB[1] - vSnapPosA_Rotated[1]) +
		//							(pSkeleton->vTerrainImpactPosC[1]-pSkeleton->vTerrainImpactPosB[1] - vSnapPosC_Rotated[1]));
	}
	else
	{
		pSkeleton->fTerrainPitch = 0.f;
		unitQuat(pSkeleton->qTerrainPitch);
		pSkeleton->fTerrainOffsetZ = 0.f;
		pSkeleton->fTerrainTiltBlend = 0.f;
		//pSkeleton->fTerrainHeightBump = 0.f;
	}
}

#undef TERRAIN_AIRBORNE_PITCH_RATE
#undef TERRAIN_PITCH_RATE

static S32 dynSkeletonClientSideRagdollCreate(	DynSkeleton *pSkeleton,
												bool bCollisionTester)
{
#define CLIENTSIDE_RAGDOLL_SKINWIDTH (0.025f/0.3048f)//0.025f is the PhysX skinwidth default in meters, 0.3048 converts it to feet
#define CLIENTSIDE_RAGDOLL_MAXVMSCALE 25.0f //once the volume scale exceeds this factor, the density of the body is dropped to prevent super heavy objects (they tend to explode in the physics simulation if leg collision geo spawns part way through uneven ground)
#define CLIENTSIDE_RAGDOLL_MINVMSCALE 0.5f  //once the volume scale exceeds this factor, the density of the body is raised to prevent super light weight objects (they tend to jitter in the physics simulation)
#define CLIENTSIDE_RAGDOLL_MINBOXSCALE 0.75f  //smallest scale factor to allow on boxes, below this things start to get jittery
#define CLIENTSIDE_RAGDOLL_MINCAPSCALE 0.75f  //smallest scale factor to allow on capsules, below this things start to get jittery

	const WLCostume *pCostume = pSkeleton ? GET_REF(pSkeleton->hCostume) : NULL;
	const SkelInfo *pSkelInfo = pCostume  ? GET_REF(pCostume->hSkelInfo) : NULL;
	const DynRagdollData* pRagdollData = NULL;

	if (pSkelInfo) {
		if (REF_HANDLE_IS_ACTIVE(pSkelInfo->hRagdollDataHD))
			pRagdollData = GET_REF(pSkelInfo->hRagdollDataHD);
		else
			pRagdollData = GET_REF(pSkelInfo->hRagdollData);
	}

	if (pRagdollData)
	{
		DynSkeleton *pPoseSkeleton = NULL;
		bool bCreatedPoseSkeleton = false;
		bool bPoseAnimTrackReady = false;
		F32 fPoseAnimTrackFrame;

		DynBaseSkeleton* pBaseSkeleton = NULL;
		bool bCreatedScaledBaseSkeleton = false;
		
		if (pRagdollData->pcPoseAnimTrack)
		{
			int iNumPoses = eafSize(&pRagdollData->eaPoseFrames);
			pPoseSkeleton = dynSkeletonCreate(pCostume, false, true, true, false, false, NULL);
			FOR_EACH_IN_EARRAY(pRagdollData->eaShapes, DynRagdollShape, pShape) {
				DynNode* pNode = dynSkeletonFindNodeNonConst(pPoseSkeleton, pShape->pcBone);
				dynNodeSetCriticalBit(pNode);
			} FOR_EACH_END;
			dynNodeFindCriticalTree(pPoseSkeleton->pRoot);
			dynDrawSetupAnimBoneInfo(pPoseSkeleton, true, false);
			bCreatedPoseSkeleton = true;
			if (iNumPoses) {
				U32 uiPoseFrame = ((U32)pSkeleton->vOldPos[0]) % iNumPoses;
				fPoseAnimTrackFrame = pRagdollData->eaPoseFrames[uiPoseFrame]-1;
			} else {
				fPoseAnimTrackFrame = 1.f;
			}
			bPoseAnimTrackReady = dynSkeletonForceAnimation(pPoseSkeleton, pRagdollData->pcPoseAnimTrack, fPoseAnimTrackFrame);
		}

		if (!bPoseAnimTrackReady)
		{
			 if (pBaseSkeleton = dynScaledBaseSkeletonCreate(pSkeleton)) {
				 bCreatedScaledBaseSkeleton = true;
			 } else {
				 pBaseSkeleton = pSkeleton ? GET_REF(pSkeleton->hBaseSkeleton) : NULL;
				 Errorf("%s failed to create scaled base skeleton, using non-scaled version!", __FUNCTION__);
			 }
		}

		if (bPoseAnimTrackReady || bCreatedScaledBaseSkeleton)
		{
			//if we're not using a test ragdoll skeleton, this should move us forward to the correct moment in time
			//otherwise, this should move us backward to the frame of animation prior to the collision being detected
			{
				dynSkeletonForceAnimationEx(pSkeleton,
											pSkeleton->pcClientSideRagdollAnimTrack,
											pSkeleton->fClientSideRagdollAnimTime,
											0, 1);
			}

			pSkeleton->ragdollState.bRagdollOn = !bCollisionTester;
			pSkeleton->bHasClientSideRagdoll = !bCollisionTester;
			pSkeleton->bHasClientSideTestRagdoll = bCollisionTester;
			pSkeleton->ragdollState.fBlend = 0.0f;
			uiRagdollCount++;

			if (bPoseAnimTrackReady) {
				pSkeleton->pClientSideRagdollPoseAnimTrackHeader = dynAnimTrackHeaderFind(pRagdollData->pcPoseAnimTrack);
				dynAnimTrackHeaderIncrementPermanentRefCount(pSkeleton->pClientSideRagdollPoseAnimTrackHeader);
				pSkeleton->fClientSideRagdollPoseAnimTrackFrame = fPoseAnimTrackFrame;
			} else {
				pSkeleton->pClientSideRagdollPoseAnimTrackHeader = NULL;
				pSkeleton->fClientSideRagdollPoseAnimTrackFrame = -1.f;
			}

			FOR_EACH_IN_EARRAY_FORWARDS(pRagdollData->eaShapes, DynRagdollShape, pRagdollShape)
			{
				const DynNode *pPoseBone = bPoseAnimTrackReady ? dynSkeletonFindNode(pPoseSkeleton, pRagdollShape->pcBone) : NULL;
				const DynNode *pBaseBone = bCreatedScaledBaseSkeleton ? dynBaseSkeletonFindNode(pBaseSkeleton, pRagdollShape->pcBone) : NULL;
				DynNode* pBone = dynSkeletonFindNodeNonConst(pSkeleton, pRagdollShape->pcBone);
				DynTransform xPoseBoneWS, xBaseBoneWS, xBoneWS;
				DynClientSideRagdollBody *pCSRB = NULL;

				dynAnimPhysicsCreateRagdollBody(pSkeleton, &pCSRB);
				if (pPoseBone) dynNodeGetWorldSpaceTransform(pPoseBone, &xPoseBoneWS);
				if (pBaseBone) dynNodeGetWorldSpaceTransform(pBaseBone, &xBaseBoneWS);
				if (pBone)
				{
					DynNode *pBoneSetBit = pBone;
					dynNodeGetWorldSpaceTransform(pBone, &xBoneWS);
					while (pBoneSetBit) {
						pBoneSetBit->uiRagdollPoisonBit = 1;
						pBoneSetBit = pBoneSetBit->pParent;
					}
				}

				//overall setup variables
				pCSRB->pcBone				= pRagdollShape->pcBone;
				pCSRB->pcParentBone			= pRagdollShape->pcParentBone;
				pCSRB->uiUseCustomJointAxis	= pRagdollShape->bUseCustomJointAxis;
				pCSRB->pcJointTargetBone	= pRagdollShape->pcJointTargetBone;
				pCSRB->pcPhysicalProperties	= pRagdollData->pcPhysicalProperties;
				pCSRB->pTuning				= &pRagdollShape->tuning;
				pCSRB->eShape				= pRagdollShape->eShape;
				pCSRB->fDensity				= pRagdollShape->fDensity;
				if (bCollisionTester) {
					pCSRB->iParentIndex = -1;
					pCSRB->pParentDPO = NULL;
				} else {
					pCSRB->iParentIndex = pRagdollShape->iParentIndex;
					assert(pCSRB->iParentIndex < eaSize(&pSkeleton->eaClientSideRagdollBodies));
					pCSRB->pParentDPO = pSkeleton->eaClientSideRagdollBodies[pCSRB->iParentIndex];
				}
				pCSRB->uiTorsoBone = pRagdollShape->bTorsoBone;
				pCSRB->uiCollisionTester = bCollisionTester;
				pCSRB->uiTesterCollided = 0;
				pCSRB->uiSleeping = 0;

				//additional gravity
				pCSRB->vAdditionalGravity[0] = 0.f;
				pCSRB->vAdditionalGravity[1] = bCollisionTester ? 0.f: -(pSkeleton->fClientSideRagdollAdditionalGravity);
				pCSRB->vAdditionalGravity[2] = 0.f;

				switch (pCSRB->eShape)
				{
					xcase eRagdollShape_Box:
					{
						Vec3 vNodeScale;
						Vec3 vScaledOffset;
						Vec3 vRotatedScaledOffset, vBaseRotatedScaledOffset;
						Mat4 mBase;

						// Now calculate original position in base skeleton
						if (pPoseBone && pBone)
						{
							const DynNode* pHelperNode;

							copyVec3(xPoseBoneWS.vPos, pCSRB->vBasePos);
							copyQuat(xPoseBoneWS.qRot, pCSRB->qBaseRot);
							copyVec3(xPoseBoneWS.vScale, vNodeScale);

							if (pCSRB->pcParentBone &&
								(pHelperNode = dynSkeletonFindNode(pPoseSkeleton, pCSRB->pcParentBone)))
							{
								DynTransform parentTrans, parentTransInv, localTrans;
								dynNodeGetWorldSpaceTransform(pHelperNode, &parentTrans);
								dynTransformInverse(&parentTrans, &parentTransInv);
								dynTransformMultiply(&xPoseBoneWS, &parentTransInv, &localTrans);
								copyVec3(localTrans.vPos, pCSRB->vParentAnchor);
							}
							else
							{
								copyVec3(xPoseBoneWS.vPos, pCSRB->vParentAnchor);
							}

							if (pCSRB->uiUseCustomJointAxis)
							{
								if (pCSRB->pTuning->jointType == eJointType_Spherical &&
									(pHelperNode = dynSkeletonFindNode(pPoseSkeleton, pCSRB->pcJointTargetBone)))
								{
									Vec3 vTargetPos;
									dynNodeGetWorldSpacePos(pHelperNode, vTargetPos);
									subVec3(vTargetPos, xPoseBoneWS.vPos, pCSRB->vJointAxis);
								}
								else if (pCSRB->pTuning->jointType == eJointType_Revolute)
								{
									Mat3 mPoseBoneR;
									quatToMat(xPoseBoneWS.qRot, mPoseBoneR);
									copyVec3(mPoseBoneR[pCSRB->pTuning->axis], pCSRB->vJointAxis);
								}
							}

							zeroVec3(pCSRB->vSelfAnchor);
						}
						else if (pBaseBone && pBone)
						{
							const DynNode* pHelperNode;

							copyVec3(xBaseBoneWS.vPos, pCSRB->vBasePos);
							copyQuat(xBaseBoneWS.qRot, pCSRB->qBaseRot);
							copyVec3(xBaseBoneWS.vScale, vNodeScale);

							if (pCSRB->pcParentBone && (pHelperNode = dynBaseSkeletonFindNode(pBaseSkeleton, pCSRB->pcParentBone)))
							{
								DynTransform parentTrans, parentTransInv, localTrans;
								dynNodeGetWorldSpaceTransform(pHelperNode, &parentTrans);
								dynTransformInverse(&parentTrans, &parentTransInv);
								dynTransformMultiply(&xBaseBoneWS, &parentTransInv, &localTrans);
								copyVec3(localTrans.vPos, pCSRB->vParentAnchor);
							}
							else
							{
								copyVec3(xBaseBoneWS.vPos, pCSRB->vParentAnchor);
							}

							if (pCSRB->uiUseCustomJointAxis)
							{
								if (pCSRB->pTuning->jointType == eJointType_Spherical &&
									(pHelperNode = dynBaseSkeletonFindNode(pBaseSkeleton, pCSRB->pcJointTargetBone)))
								{
									Vec3 vTargetPos;
									dynNodeGetWorldSpacePos(pHelperNode, vTargetPos);
									subVec3(vTargetPos, xBaseBoneWS.vPos, pCSRB->vJointAxis);
								}
								else if (pCSRB->pTuning->jointType == eJointType_Revolute)
								{
									Mat3 mBaseBoneR;
									quatToMat(xBaseBoneWS.qRot, mBaseBoneR);
									copyVec3(mBaseBoneR[pCSRB->pTuning->axis], pCSRB->vJointAxis);
								}
							}

							zeroVec3(pCSRB->vSelfAnchor);
						}
						else
						{
							zeroVec3(pCSRB->vBasePos);
							unitQuat(pCSRB->qBaseRot);
							zeroVec3(pCSRB->vSelfAnchor);
							unitVec3(vNodeScale);
						}

						mulVecVec3(vNodeScale, pRagdollShape->vOffset, vScaledOffset);
						quatRotateVec3(pRagdollShape->qRotation, vScaledOffset, vBaseRotatedScaledOffset);
						quatToMat(pRagdollShape->qRotation, mBase);
						copyVec3(vBaseRotatedScaledOffset, mBase[3]);

						if (pPoseBone) {
							Quat qRotation;
							copyQuat(xPoseBoneWS.qRot, pCSRB->qBindPose);
							quatMultiply(pRagdollShape->qRotation, xPoseBoneWS.qRot, qRotation);
							quatRotateVec3(qRotation, vScaledOffset, vRotatedScaledOffset);
							quatToMat(qRotation, pCSRB->mBox);
							copyVec3(vRotatedScaledOffset, pCSRB->mBox[3]);
						} else {
							unitQuat(pCSRB->qBindPose);
							copyVec3(vBaseRotatedScaledOffset, vRotatedScaledOffset);
							quatToMat(pRagdollShape->qRotation, mBase);
							copyMat4(mBase, pCSRB->mBox);
						}

						{
							Vec3 vCenter;
							Vec3 vCenterInBoxSpace, vCenterInBaseSpace;
							Vec3 vScaledMin, vScaledMax;
							mulVecVec3(pRagdollShape->vMax, vNodeScale, vScaledMax);
							mulVecVec3(pRagdollShape->vMin, vNodeScale, vScaledMin);
							addVec3(vScaledMax, vScaledMin, vCenter);
							scaleVec3(vCenter, 0.5f, vCenter);
							mulVecMat3(vCenter, pCSRB->mBox, vCenterInBoxSpace);
							addVec3(pCSRB->mBox[3], vCenterInBoxSpace, pCSRB->mBox[3]);
							mulVecMat3(vCenter, mBase, vCenterInBaseSpace);
							copyVec3(vCenterInBaseSpace, pCSRB->vCenterOfGravity);
							subVec3(vScaledMax, vScaledMin, pCSRB->vBoxDimensions);
						}

						{
							Vec3 vBoxDimensionsUnscaled;
							subVec3(pRagdollShape->vMax, pRagdollShape->vMin, vBoxDimensionsUnscaled);
							pCSRB->fVolumeMassScale =	(pCSRB->vBoxDimensions[0] *pCSRB->vBoxDimensions[1] *pCSRB->vBoxDimensions[2] ) /
														(vBoxDimensionsUnscaled[0]*vBoxDimensionsUnscaled[1]*vBoxDimensionsUnscaled[2]);
						}

						pCSRB->fSkinWidth = MAX(CLIENTSIDE_RAGDOLL_MINBOXSCALE,MIN(vNodeScale[0], MIN(vNodeScale[1], vNodeScale[2]))) * CLIENTSIDE_RAGDOLL_SKINWIDTH;
					}

					xcase eRagdollShape_Capsule:
					{
						Vec3 vDir, vBaseDir;
						Vec3 vNodeScale;
						Vec3 vScaledOffset;
						Vec3 vRotatedScaledOffset, vBaseRotatedScaledOffset;

						if (pPoseBone && pBone)
						{
							const DynNode* pHelperNode;

							copyVec3(xPoseBoneWS.vPos, pCSRB->vBasePos);
							copyQuat(xPoseBoneWS.qRot, pCSRB->qBaseRot);
							copyVec3(xBoneWS.vScale, vNodeScale);

							if (pCSRB->pcParentBone &&
								(pHelperNode = dynSkeletonFindNode(pPoseSkeleton, pCSRB->pcParentBone)))
							{
								DynTransform parentTrans, parentTransInv, localTrans;
								dynNodeGetWorldSpaceTransform(pHelperNode, &parentTrans);
								dynTransformInverse(&parentTrans, &parentTransInv);
								dynTransformMultiply(&xPoseBoneWS, &parentTransInv, &localTrans);
								copyVec3(localTrans.vPos, pCSRB->vParentAnchor);
							}
							else
							{
								copyVec3(xPoseBoneWS.vPos, pCSRB->vParentAnchor);
							}

							if (pCSRB->uiUseCustomJointAxis)
							{
								if (pCSRB->pTuning->jointType == eJointType_Spherical &&
									(pHelperNode = dynSkeletonFindNode(pPoseSkeleton, pCSRB->pcJointTargetBone)))
								{
									Vec3 vTargetPos;
									dynNodeGetWorldSpacePos(pHelperNode, vTargetPos);
									subVec3(vTargetPos, xPoseBoneWS.vPos, pCSRB->vJointAxis);
								}
								else if (pCSRB->pTuning->jointType == eJointType_Revolute)
								{
									Mat3 mPoseBoneR;
									quatToMat(xPoseBoneWS.qRot, mPoseBoneR);
									copyVec3(mPoseBoneR[pCSRB->pTuning->axis], pCSRB->vJointAxis);
								}
							}

							setVec3(pCSRB->vSelfAnchor, 0, 0, 0);
						}
						else if (pBaseBone && pBone)
						{
							const DynNode* pHelperNode;

							copyVec3(xBaseBoneWS.vPos, pCSRB->vBasePos);
							copyQuat(xBaseBoneWS.qRot, pCSRB->qBaseRot);
							copyVec3(xBoneWS.vScale, vNodeScale);

							if (pCSRB->pcParentBone &&
								(pHelperNode = dynBaseSkeletonFindNode(pBaseSkeleton, pCSRB->pcParentBone)))
							{
								DynTransform parentTrans, parentTransInv, localTrans;
								dynNodeGetWorldSpaceTransform(pHelperNode, &parentTrans);
								dynTransformInverse(&parentTrans, &parentTransInv);
								dynTransformMultiply(&xBaseBoneWS, &parentTransInv, &localTrans);
								copyVec3(localTrans.vPos, pCSRB->vParentAnchor);
								subVec3(xBaseBoneWS.vPos, parentTrans.vPos, pCSRB->vJointAxis);
								normalVec3(pCSRB->vJointAxis);
							}
							else
							{
								copyVec3(xBaseBoneWS.vPos, pCSRB->vParentAnchor);
							}

							if (pCSRB->uiUseCustomJointAxis)
							{
								if (pCSRB->pTuning->jointType == eJointType_Spherical &&
									(pHelperNode = dynBaseSkeletonFindNode(pBaseSkeleton, pCSRB->pcJointTargetBone)))
								{
									Vec3 vTargetPos;
									dynNodeGetWorldSpacePos(pHelperNode, vTargetPos);
									subVec3(vTargetPos, xBaseBoneWS.vPos, pCSRB->vJointAxis);
								}
								else if (pCSRB->pTuning->jointType == eJointType_Revolute)
								{
									Mat3 mBaseBoneR;
									quatToMat(xBaseBoneWS.qRot, mBaseBoneR);
									copyVec3(mBaseBoneR[pCSRB->pTuning->axis], pCSRB->vJointAxis);
								}
							}

							setVec3(pCSRB->vSelfAnchor, 0, 0, 0);
						}
						else
						{
							zeroVec3(pCSRB->vBasePos);
							unitQuat(pCSRB->qBaseRot);
							zeroVec3(pCSRB->vSelfAnchor);
							unitVec3(vNodeScale);
						}
							
						mulVecVec3(vNodeScale, pRagdollShape->vOffset, vScaledOffset);
						quatRotateVec3(pRagdollShape->qRotation, vScaledOffset, vBaseRotatedScaledOffset);
						quatRotateVec3(pRagdollShape->qRotation, upvec, vBaseDir);

						if (pPoseBone) {
							Quat qRotation;
							copyQuat(xPoseBoneWS.qRot, pCSRB->qBindPose);
							quatMultiply(pRagdollShape->qRotation, xPoseBoneWS.qRot, qRotation);
							quatRotateVec3(qRotation, vScaledOffset, vRotatedScaledOffset);
							quatRotateVec3(qRotation, upvec, vDir);
						} else {
							unitQuat(pCSRB->qBindPose);
							copyVec3(vBaseRotatedScaledOffset, vRotatedScaledOffset);
							copyVec3(vBaseDir, vDir);
						}

						{
							F32 fScaleDirDot = dotVec3(vBaseDir, vNodeScale);
							F32 fMaxWidthScale;
							F32 fVolume, fVolumeScaled;
							F32 fLength;
							Vec3 vAntiDir;
							Vec3 vWidthScale;

							fLength = pRagdollShape->fHeightMax - pRagdollShape->fHeightMin;
							if (bCollisionTester && !pRagdollShape->iNumChildren) {
								fLength *= 0.333f; //foreshorten limbs that could be touching the ground correctly
							}
							pCSRB->fCapsuleLength = fabsf(fLength * fScaleDirDot);

							// width is length of projection of scale vector into width plane
							scaleVec3(vBaseDir, -fScaleDirDot, vAntiDir);
							addVec3(vNodeScale, vAntiDir, vWidthScale);
							fMaxWidthScale = MAX(CLIENTSIDE_RAGDOLL_MINCAPSCALE,vec3MaxComponent(vWidthScale));
							pCSRB->fCapsuleRadius = fMaxWidthScale * pRagdollShape->fRadius;

							fVolume = (PI*pRagdollShape->fRadius*pRagdollShape->fRadius) * (4.0f/3.0f*pRagdollShape->fRadius + fLength);
							fVolumeScaled = (PI*pCSRB->fCapsuleRadius*pCSRB->fCapsuleRadius) * (4.0f/3.0f*pCSRB->fCapsuleRadius + pCSRB->fCapsuleLength);
							pCSRB->fVolumeMassScale  = fVolumeScaled/fVolume;

							pCSRB->fSkinWidth = fMaxWidthScale * CLIENTSIDE_RAGDOLL_SKINWIDTH;
						}

						copyVec3(vDir, pCSRB->vCapsuleDir);
						copyVec3(vRotatedScaledOffset, pCSRB->vCapsuleStart);
						scaleAddVec3(vBaseDir, 0.5f*pCSRB->fCapsuleLength, vBaseRotatedScaledOffset, pCSRB->vCenterOfGravity);
					}
				}

				if(pCSRB)
				{
					if (pCSRB->fVolumeMassScale > CLIENTSIDE_RAGDOLL_MAXVMSCALE) {
						pCSRB->fDensity *= CLIENTSIDE_RAGDOLL_MAXVMSCALE/pCSRB->fVolumeMassScale;
						pCSRB->fVolumeMassScale = CLIENTSIDE_RAGDOLL_MAXVMSCALE;
					}
					else if (pCSRB->fVolumeMassScale < CLIENTSIDE_RAGDOLL_MINVMSCALE) {
						pCSRB->fDensity *= CLIENTSIDE_RAGDOLL_MINVMSCALE/pCSRB->fVolumeMassScale;
						pCSRB->fVolumeMassScale = CLIENTSIDE_RAGDOLL_MINVMSCALE;
					}

					if (pBone)
					{
						const DynNode *pNode = pSkeleton->pRoot->pParent;
						dynNodeGetWorldSpaceMat(pNode, pCSRB->mEntityWS, false);

						copyVec3(xBoneWS.vPos, pCSRB->vPosePos);
						copyQuat(xBoneWS.qRot, pCSRB->qPoseRot);

						copyVec3(xBoneWS.vPos, pCSRB->vWorldSpace);
						copyQuat(xBoneWS.qRot, pCSRB->qWorldSpace);

						if (!pCSRB->pcParentBone) {
							Mat4 mActor, mActorInv;
							Mat4 mEntityActorSpace;

							quatToMat(pCSRB->qWorldSpace, mActor);
							copyVec3(pCSRB->vWorldSpace, mActor[3]);
							invertMat4(mActor, mActorInv);
							mulMat4(mActorInv, pCSRB->mEntityWS, mEntityActorSpace);

							getMat3YPR(mEntityActorSpace, pCSRB->pyrOffsetToAnimRoot);
							copyVec3(mEntityActorSpace[3], pCSRB->posOffsetToAnimRoot);
						}

						zeroVec3(pCSRB->vInitVel);
						zeroVec3(pCSRB->vInitAngVel);
					}
					else
					{
						zeroVec3(pCSRB->vPosePos);
						unitQuat(pCSRB->qPoseRot);

						zeroVec3(pCSRB->vWorldSpace);
						unitQuat(pCSRB->qWorldSpace);

						zeroVec3(pCSRB->vInitVel);
						zeroVec3(pCSRB->vInitAngVel);
					}
				}
			} FOR_EACH_END;

			if (!dynDebugState.bDisableClientSideRagdollInitialVelocities &&
				!bCollisionTester)
			{
				//create the frame skeleton used to compute initial velocities
				DynSkeleton *pFrameSkeleton = dynSkeletonCreate(pCostume, false, true, true, true, false, NULL);
				F32 *eafPrevFrame, *eafCurrFrame, *eafNextFrame;
				DynNode entFrameNode = {0};
				Vec3 vRootPos;
				Quat qRootRot;
				dynNodeInitPersisted(&entFrameNode);
				dynNodeGetWorldSpacePos(pSkeleton->pRoot->pParent, vRootPos);
				dynNodeGetWorldSpaceRot(pSkeleton->pRoot->pParent, qRootRot);
				dynNodeSetRot(&entFrameNode, qRootRot);
				dynNodeSetPos(&entFrameNode, vRootPos);
				dynNodeParent(pFrameSkeleton->pRoot, &entFrameNode);
				FOR_EACH_IN_EARRAY(pRagdollData->eaShapes, DynRagdollShape, pShape) {
					DynNode* pNode = dynSkeletonFindNodeNonConst(pFrameSkeleton, pShape->pcBone);
					dynNodeSetCriticalBit(pNode);
				} FOR_EACH_END;
				dynNodeFindCriticalTree(pFrameSkeleton->pRoot);
				dynDrawSetupAnimBoneInfo(pFrameSkeleton, true, false);
				dynNodeCleanDirtyBits(&entFrameNode);
				
				//backwards 0.8 frames
				eafPrevFrame = NULL;
				dynSkeletonForceAnimation(	pFrameSkeleton,
											pSkeleton->pcClientSideRagdollAnimTrack,
											MAX(0.f, pSkeleton->fClientSideRagdollAnimTime - 0.8f));
				FOR_EACH_IN_EARRAY(pSkeleton->eaClientSideRagdollBodies, DynPhysicsObject, pDPO) {
					DynClientSideRagdollBody *pCSRB = pDPO->body.pBody;
					const DynNode *pNode = dynSkeletonFindNode(pFrameSkeleton, pCSRB->pcBone);
					Vec3 vCogPos;
					Mat4 mPrev;
					dynNodeGetWorldSpaceMat(pNode, mPrev, false);
					mulVecMat4(pCSRB->vCenterOfGravity, mPrev, vCogPos);
					eafPush3(&eafPrevFrame, mPrev[3]);
					eafPush3(&eafPrevFrame, vCogPos);
				} FOR_EACH_END;

				//current frame
				eafCurrFrame = NULL;
				dynSkeletonForceAnimation(	pFrameSkeleton,
											pSkeleton->pcClientSideRagdollAnimTrack,
											pSkeleton->fClientSideRagdollAnimTime);
				FOR_EACH_IN_EARRAY(pSkeleton->eaClientSideRagdollBodies, DynPhysicsObject, pDPO) {
					DynClientSideRagdollBody *pCSRB = pDPO->body.pBody;
					const DynNode *pNode = dynSkeletonFindNode(pFrameSkeleton, pCSRB->pcBone);
					Vec3 vCogPos;
					Mat4 mCurr;
					dynNodeGetWorldSpaceMat(pNode, mCurr, false);
					mulVecMat4(pCSRB->vCenterOfGravity, mCurr, vCogPos);
					eafPush3(&eafCurrFrame, mCurr[3]);
					eafPush3(&eafCurrFrame, vCogPos);
				} FOR_EACH_END;

				//forwards 0.8 frames
				eafNextFrame = NULL;
				dynSkeletonForceAnimation(	pFrameSkeleton,
											pSkeleton->pcClientSideRagdollAnimTrack,
											MAX(0.f, pSkeleton->fClientSideRagdollAnimTime + 0.8f));
				FOR_EACH_IN_EARRAY(pSkeleton->eaClientSideRagdollBodies, DynPhysicsObject, pDPO) {
					DynClientSideRagdollBody *pCSRB = pDPO->body.pBody;
					const DynNode *pNode = dynSkeletonFindNode(pFrameSkeleton, pCSRB->pcBone);
					Vec3 vCogPos;
					Mat4 mNext;
					dynNodeGetWorldSpaceMat(pNode, mNext, false);
					mulVecMat4(pCSRB->vCenterOfGravity, mNext, vCogPos);
					eafPush3(&eafNextFrame, mNext[3]);
					eafPush3(&eafNextFrame, vCogPos);
				} FOR_EACH_END;

				//compute linear & angular velocities
				FOR_EACH_IN_EARRAY(pSkeleton->eaClientSideRagdollBodies, DynPhysicsObject, pDPO) {
					DynClientSideRagdollBody *pCSRB = pDPO->body.pBody;
					Vec3 vPrevPos, vCurrPos, vNextPos;
					Vec3 vPrevCog, vCurrCog, vNextCog;
					Vec3 vJointVel, vJointBar, vJointAng;

					vPrevPos[0] = eafGet(&eafPrevFrame,6*ipDPOIndex+0);
					vPrevPos[1] = eafGet(&eafPrevFrame,6*ipDPOIndex+1);
					vPrevPos[2] = eafGet(&eafPrevFrame,6*ipDPOIndex+2);
					vPrevCog[0] = eafGet(&eafPrevFrame,6*ipDPOIndex+3);
					vPrevCog[1] = eafGet(&eafPrevFrame,6*ipDPOIndex+4);
					vPrevCog[2] = eafGet(&eafPrevFrame,6*ipDPOIndex+5);

					vCurrPos[0] = eafGet(&eafCurrFrame,6*ipDPOIndex+0);
					vCurrPos[1] = eafGet(&eafCurrFrame,6*ipDPOIndex+1);
					vCurrPos[2] = eafGet(&eafCurrFrame,6*ipDPOIndex+2);
					vCurrCog[0] = eafGet(&eafCurrFrame,6*ipDPOIndex+3);
					vCurrCog[1] = eafGet(&eafCurrFrame,6*ipDPOIndex+4);
					vCurrCog[2] = eafGet(&eafCurrFrame,6*ipDPOIndex+5);

					vNextPos[0] = eafGet(&eafNextFrame,6*ipDPOIndex+0);
					vNextPos[1] = eafGet(&eafNextFrame,6*ipDPOIndex+1);
					vNextPos[2] = eafGet(&eafNextFrame,6*ipDPOIndex+2);
					vNextCog[0] = eafGet(&eafNextFrame,6*ipDPOIndex+3);
					vNextCog[1] = eafGet(&eafNextFrame,6*ipDPOIndex+4);
					vNextCog[2] = eafGet(&eafNextFrame,6*ipDPOIndex+5);

					//linear velocity calc
					subVec3(vNextCog, vPrevCog, pCSRB->vInitVel);
					scaleVec3(pCSRB->vInitVel, 30.f/1.6f, pCSRB->vInitVel); //30 is animation data framerate, 1.6 is sample spacing in frames

					//angular velocity calc
					subVec3(vCurrPos, vCurrCog, vJointBar);
					subVec3(vNextPos, vPrevPos, vJointVel);
					if (dotVec3(vJointBar,vJointBar) > 0.01f && dotVec3(vJointVel,vJointVel) > 0.01f) {
						scaleVec3(vJointVel, 30.f/1.6f, vJointVel); //30 is animation data framerate, 1.6 is sample spacing in frames
						crossVec3(vJointBar, vJointVel, vJointAng);
						scaleVec3(vJointAng, 1.f/dotVec3(vJointBar,vJointBar), pCSRB->vInitAngVel);
					} else {
						zeroVec3(pCSRB->vInitAngVel);
					}
				} FOR_EACH_END;

				//release memory
				dynSkeletonFree(pFrameSkeleton);
				eafDestroy(&eafPrevFrame);
				eafDestroy(&eafCurrFrame);
				eafDestroy(&eafNextFrame);
			}
		}
		
		if (bCreatedScaledBaseSkeleton && pBaseSkeleton)
			dynBaseSkeletonFree(pBaseSkeleton);

		if (bCreatedPoseSkeleton && pPoseSkeleton)
			dynSkeletonFree(pPoseSkeleton);
	}

	return (eaSize(&pSkeleton->eaClientSideRagdollBodies));
}

static void dynSkeletonClientSideRagdollFree(DynSkeleton *pSkeleton)
{
	if (SAFE_MEMBER(pSkeleton,bHasClientSideRagdoll) ||
		SAFE_MEMBER(pSkeleton,bHasClientSideTestRagdoll))
	{
		uiRagdollCount--;
		pSkeleton->bHasClientSideRagdoll = 0;
		pSkeleton->bHasClientSideTestRagdoll = 0;
		dynAnimPhysicsFreeRagdollBodies(pSkeleton);
	}
}

static void dynSkeletonEndDeathAnimation(DynSkeleton *pSkeleton)
{
	//don't call this on a thread
	dynSkeletonClientSideRagdollFree(pSkeleton);
	pSkeleton->bCreateClientSideTestRagdoll = false;
	pSkeleton->bCreateClientSideRagdoll = false;
	pSkeleton->bSleepingClientSideRagdoll = false;
	pSkeleton->ragdollState.bRagdollOn = false;
	pSkeleton->ragdollState.fBlend = 0.f;
	pSkeleton->ragdollState.uiNumParts = 0;
	SAFE_FREE(pSkeleton->ragdollState.aParts);

	//pop out of the death animation
	dynSkeletonResetToADefaultGraph(pSkeleton, "end death", 0, 1);
}

static bool bDisableSkeletonStrands = false;
AUTO_CMD_INT(bDisableSkeletonStrands, danimDisableSkeletonStrands) ACMD_CATEGORY(Debug) ACMD_CATEGORY(dynAnimation);

static void dynSkeletonCreateStrand(DynSkeleton *pSkeleton,
									const DynStrandData *pStrandData)
{
	DynNode *pFromNode;
	DynNode *pBreakNode;
	DynNode *pToNode;
	DynNode *pNodeCounter;
	U32 uiNumToJoints;
	U32 uiNumBreakJoints;

	//grab the node names
	pFromNode = dynSkeletonFindNodeNonConst(pSkeleton, pStrandData->pcStartNodeName);
	pToNode   = dynSkeletonFindNodeNonConst(pSkeleton, pStrandData->pcEndNodeName);
	if (pStrandData->pcBreakNodeName) {
		pBreakNode = dynSkeletonFindNodeNonConst(pSkeleton, pStrandData->pcBreakNodeName);
	} else {
		pBreakNode = NULL;
	}

	//count the number of joints between the from and to node
	uiNumToJoints = 1;
	pNodeCounter = pToNode;
	while (	pNodeCounter != pFromNode &&
		pNodeCounter != pSkeleton->pRoot->pParent)
	{
		pNodeCounter = pNodeCounter->pParent;
		uiNumToJoints++;
	}
	if (pNodeCounter == pSkeleton->pRoot->pParent) {
		return;
	}

	//count the number of joints between the from and break node
	uiNumBreakJoints = 0;
	if (pNodeCounter = pBreakNode) {
		uiNumBreakJoints = 1;
		while (	pNodeCounter != pFromNode &&
			pNodeCounter != pSkeleton->pRoot->pParent)
		{
			pNodeCounter = pNodeCounter->pParent;
			uiNumBreakJoints++;
		}
	}
	if (pBreakNode &&
		pNodeCounter == pSkeleton->pRoot->pParent) {
		return;
	}

	if (pFromNode && pToNode)
	{
		DynStrand *pStrand = StructCreate(parse_DynStrand);
		F32 fAxisLengthSqr;

		//mark the presence of strand data
		pSkeleton->bHasStrands = 1;
		pSkeleton->bInitStrands = 1;

		//set the basic data
		pStrand->pRootNode = pFromNode;
		pStrand->pEndNode = pToNode;
		pStrand->uiNumJoints = uiNumToJoints;
		pStrand->fSelfSpringK = pStrandData->fSelfSpringK;
		pStrand->fSelfDamperC = pStrandData->fSelfDamperC;

		pStrand->fStrength			= pStrandData->fApplicationStrength;
		pStrand->fMaxJointAngle		= pStrandData->fMaxJointAngle;
		pStrand->fWindResistance	= pStrandData->fWindResistance;
		pStrand->fGravity			= pStrandData->fGravity;
		pStrand->fTorsionRatio		= pStrandData->fTorsionRatio;

		pStrand->bAxisIsInWorldSpace		= pStrandData->bAxisIsInWorldSpace;
		pStrand->bPrealignToProceduralAxis	= pStrandData->bPrealignToProceduralAxis;
		pStrand->bUseEulerIntegration		= pStrandData->bUseEulerIntegration;
		pStrand->bFullGroundReg				= pStrandData->bFullGroundReg;
		pStrand->bPartialGroundReg			= pStrandData->bPartialGroundReg;

		fAxisLengthSqr = lengthVec3Squared(pStrandData->vAxis);
		if (fAxisLengthSqr > 0.01) {
			scaleVec3(pStrandData->vAxis,1.f/sqrtf(fAxisLengthSqr),pStrand->vAxis);
		} else {
			pStrand->vAxis[0] =  0.f;
			pStrand->vAxis[1] =  0.f;
			pStrand->vAxis[2] = -1.f;
		}

		pStrand->eaJoints = NULL;
		pNodeCounter = pToNode;
		eaPush(&pStrand->eaJoints, pNodeCounter);
		while (pNodeCounter != pFromNode) {
			pNodeCounter = pNodeCounter->pParent;
			eaInsert(&pStrand->eaJoints, pNodeCounter, 0);
		}
		assert(pNodeCounter == pFromNode);
		assert(eaSize(&pStrand->eaJoints) == uiNumToJoints);

		if (pStrand->bHasWeakPoint = (pBreakNode != NULL))
		{
			pStrand->strongPoint.pFromNode = pFromNode;
			pStrand->strongPoint.pToNode = pBreakNode;
			pStrand->strongPoint.uiNumJoints = uiNumBreakJoints;
			pStrand->strongPoint.fSpringK = pStrandData->fAnimSpringK;
			pStrand->strongPoint.fDamperC = pStrandData->fAnimDamperC;
			pStrand->strongPoint.fMassInv = 1.f/pStrandData->fBreakPointMass;

			pStrand->weakPoint.pFromNode = pBreakNode;
			pStrand->weakPoint.pToNode = pToNode;
			pStrand->weakPoint.uiNumJoints = uiNumToJoints - uiNumBreakJoints;
			pStrand->weakPoint.fSpringK = pStrandData->fAnimSpringK;
			pStrand->weakPoint.fDamperC = pStrandData->fAnimDamperC;
			pStrand->weakPoint.fMassInv = 1.f/pStrandData->fEndPointMass;
		}
		else
		{
			pStrand->strongPoint.pFromNode = pFromNode;
			pStrand->strongPoint.pToNode = pToNode;
			pStrand->strongPoint.uiNumJoints = uiNumToJoints;
			pStrand->strongPoint.fSpringK = pStrandData->fAnimSpringK;
			pStrand->strongPoint.fDamperC = pStrandData->fAnimDamperC;
			pStrand->strongPoint.fMassInv = 1.f/pStrandData->fEndPointMass;
		}

		eaPush(&pSkeleton->eaStrands, pStrand);
	}
}

static void dynSkeletonCreateStrands(	DynSkeleton *pSkeleton,
										const DynStrandDataSet *pStrandDataSet,
										bool bHeadshot)
{
	//setup basic values that are always needed
	pSkeleton->bHasStrands = 0;
	pSkeleton->bInitStrands = 0;
	pSkeleton->bInitStrandsDelay = 0;
	pSkeleton->eaStrands = NULL;

	//when appropriate, do the actual creation
	if (!bHeadshot && pStrandDataSet)
	{
		FOR_EACH_IN_EARRAY(pStrandDataSet->eaDynStrandData, DynStrandData, pData)
		{
			dynSkeletonCreateStrand(pSkeleton, pData);
		}
		FOR_EACH_END;
	}
}

static void dynSkeletonInitStrands(DynSkeleton *pSkeleton)
{
	//mark the initialization as done
	pSkeleton->bInitStrands = 0;

	FOR_EACH_IN_EARRAY(pSkeleton->eaStrands, DynStrand, pStrand)
	{
		DynTransform xRootBone;

		//init the strand physics
		dynNodeGetWorldSpaceTransform(pStrand->pRootNode, &xRootBone);
		copyVec3(xRootBone.vPos, pStrand->vRootPos);
		dynNodeGetWorldSpacePos(pStrand->pEndNode, pStrand->vEndPos);

		//init the point physics
		dynStrandInitStrandPoint(pStrand, &pStrand->strongPoint, &xRootBone, 0.f);
		if (pStrand->bHasWeakPoint) {
			dynStrandInitStrandPoint(pStrand, &pStrand->weakPoint, &xRootBone, pStrand->strongPoint.fRestLength);
		}

		if (pStrand->bPrealignToProceduralAxis) {
			dynStrandPrealignToProceduralAxis(pStrand);
		}
	}
	FOR_EACH_END;
}

static void dynSkeletonSimulateStrands(	DynSkeleton *pSkeleton,
										F32 fDeltaTime)
{
	F32 fDeltaTimeSafe = CLAMP(fDeltaTime, 0.0001f, 0.05f);
	Vec3 vBasePos;

	//find the base position used for ground registration
	dynNodeGetWorldSpacePos(pSkeleton->pGenesisSkeleton->pRoot, vBasePos);

	//simulate each strand on the skeleton
	FOR_EACH_IN_EARRAY(pSkeleton->eaStrands, DynStrand, pStrand)
	{
		//render the strand before deformation
		dynStrandDebugRenderStrand(pStrand, 0xFFFF00FF);

		//deform the strand
		dynStrandDeform(pStrand, fDeltaTimeSafe);

		//render the strand after deformation
		dynStrandDebugRenderStrandPoint(&pStrand->strongPoint, pStrand->vRootPos, pStrand->vRootPos);
		if (pStrand->bHasWeakPoint) {
			dynStrandDebugRenderStrandPoint(&pStrand->weakPoint, pStrand->strongPoint.vPos, pStrand->vRootPos);
		}
		dynStrandDebugRenderStrand(pStrand, 0xFFAA00AA);

		//perform ground registration
		if (pStrand->bFullGroundReg) {
			dynStrandGroundRegStrandFull(pStrand, vBasePos);
		} else if (pStrand->bPartialGroundReg) {
			dynStrandGroundRegStrandQuick(pStrand, vBasePos);
		}

		//render the strand after ground registration
		dynStrandDebugRenderStrand(pStrand, 0xFF880088);
	}
	FOR_EACH_END;
}

static void dynSkeletonUpdateStrands(	DynSkeleton *pSkeleton,
										F32 fDeltaTime)
{
	if (pSkeleton->bInitStrands ||
		pSkeleton->bInitStrandsDelay)
	{
		pSkeleton->bInitStrandsDelay = pSkeleton->bInitStrands;
		dynSkeletonInitStrands(pSkeleton);
	}
	else if (!bDisableSkeletonStrands)
	{
		dynSkeletonSimulateStrands(pSkeleton, fDeltaTime);
	}
}

static void dynSkeletonDestroyStrands(DynSkeleton *pSkeleton)
{
	pSkeleton->bInitStrands = 0;
	pSkeleton->bInitStrandsDelay = 0;
	if (TRUE_THEN_RESET(pSkeleton->bHasStrands)) {
		FOR_EACH_IN_EARRAY(pSkeleton->eaStrands, DynStrand, pStrand) {
			eaDestroy(&pStrand->eaJoints);
		} FOR_EACH_END;
		eaDestroyEx(&pSkeleton->eaStrands,NULL);
	}
}

static void dynSkeletonCreateGroundRegLimbs(DynSkeleton *pSkeleton,
											const DynGroundRegData *pGroundRegData)
{
	if (pGroundRegData)
	{
		//when appropriate, do the actual creation
		pSkeleton->eaGroundRegLimbs = NULL;
		pSkeleton->fGroundRegFloorDeltaNear = pGroundRegData->fFloorDeltaNear;
		pSkeleton->fGroundRegFloorDeltaFar  = pGroundRegData->fFloorDeltaFar;

		FOR_EACH_IN_EARRAY(pGroundRegData->eaLimbs, DynGroundRegDataLimb, pLimbData)
		{
			DynGroundRegLimb *pLimb = StructCreate(parse_DynGroundRegLimb);
			bool bFailed = false;

			pLimb->pStaticLimbData = pLimbData;
			pLimb->pHeightFixupNode = dynSkeletonFindNodeNonConst(pSkeleton, pLimbData->pcHeightFixupNode);
			pLimb->pEndEffectorNode = dynSkeletonFindNodeNonConst(pSkeleton, pLimbData->pcEndEffectorNode);

			if (!pLimb->pHeightFixupNode) {
				Errorf("Failed to find HeightFixupNode %s for ground registration %s when creating skeleton", pLimbData->pcHeightFixupNode, pGroundRegData->pcFileName);
				bFailed = true;
			}
			if (!pLimb->pEndEffectorNode) {
				Errorf("Failed to find EndEffectorNode %s for ground registration %s when creating skeleton", pLimbData->pcEndEffectorNode, pGroundRegData->pcFileName);
				bFailed = true;
			}

			if (bFailed) {
				free(pLimb);
			} else {
				eaPush(&pSkeleton->eaGroundRegLimbs, pLimb);
			}
		}
		FOR_EACH_END;
	}
	else
	{
		//setup basic values that are always needed
		pSkeleton->eaGroundRegLimbs = NULL;
		pSkeleton->fGroundRegFloorDeltaNear = 0.f;
		pSkeleton->fGroundRegFloorDeltaFar  = 0.f;
	}
}

static void dynSkeletonCreateNodeExpressions(	DynSkeleton *pSkeleton,
												const DynAnimExpressionSet *pExpressionSet )
{
	//setup basic values that are always needed
	//pSkeleton->bHasExpressions = 0;
	//pSkeleton->eaNodeExpressions = NULL;

	//when appropriate, do the actual creation
	if (pExpressionSet)
	{
		pSkeleton->pAnimExpressionSet = pExpressionSet;
		pSkeleton->eaAnimExpressionData = NULL;
	}
	else
	{
		pSkeleton->pAnimExpressionSet = NULL;
		pSkeleton->eaAnimExpressionData = NULL;
	}
}

static void dynSkeletonFreeNodeExpressions(DynSkeleton *pSkeleton)
{
	eaDestroyEx(&pSkeleton->eaAnimExpressionData, NULL);
}

static void dynSkeletonRecreateNodeExpressions(	DynSkeleton *pSkeleton,
												const DynAnimExpressionSet *pExpressionSet )
{
	dynSkeletonFreeNodeExpressions(pSkeleton);
	dynSkeletonCreateNodeExpressions(pSkeleton, pExpressionSet);
}

void dynSkeletonPushAnimWordFeed(DynSkeleton *pSkeleton, DynAnimWordFeed *pAnimWordFeed)
{
	pAnimWordFeed->pcKeyword = NULL;
	pAnimWordFeed->uiNumStanceActions = 0;
	pAnimWordFeed->uiNumStances = 0;
	pAnimWordFeed->uiNumFlags = 0;
	eaPush(&pSkeleton->eaAnimWordFeeds, pAnimWordFeed);
}

static void dynSkeletonPlayAnimWordFeeds(DynSkeleton *pSkeleton)
{
	FOR_EACH_IN_EARRAY(pSkeleton->eaAnimWordFeeds, DynAnimWordFeed, pAnimWordFeed)
	{
		U32 i;

		for (i = 0; i < pAnimWordFeed->uiNumStanceActions; i++) {
			if (pAnimWordFeed->pStanceActions[i].bSet) {
				dynSkeletonSetStanceWordInSet(pSkeleton, DS_STANCE_SET_FX_FEED, pAnimWordFeed->pStanceActions[i].pcWord);
			} else {
				dynSkeletonClearStanceWordInSet(pSkeleton, DS_STANCE_SET_FX_FEED, pAnimWordFeed->pStanceActions[i].pcWord);
			}
		}
		pAnimWordFeed->uiNumStanceActions = 0;

		if (pAnimWordFeed->pcKeyword) {
			dynSkeletonStartGraph(pSkeleton, pAnimWordFeed->pcKeyword, 0);
		}
		pAnimWordFeed->pcKeyword = NULL;

		for (i = 0; i < pAnimWordFeed->uiNumFlags; i++) {
			dynSkeletonSetFlag(pSkeleton, pAnimWordFeed->ppcFlags[i], 0);
		}
		pAnimWordFeed->uiNumFlags = 0;
	}
	FOR_EACH_END;
}

void dynSkeletonPlayKeywordInAnimWordFeed(DynAnimWordFeed *pAnimWordFeed, const char *pcKeyword)
{
	pAnimWordFeed->pcKeyword = pcKeyword;
}

bool dynSkeletonPlayFlagInAnimWordFeed(DynAnimWordFeed *pAnimWordFeed, const char *pcFlag)
{
	assert(0 <= pAnimWordFeed->uiNumFlags && pAnimWordFeed->uiNumFlags <= DAWF_MaxFlags);

	//add the stance
	if (pAnimWordFeed->uiNumFlags == DAWF_MaxFlags) {
		Errorf("Attempted to play too many flags in a DynAnimWordFeed (while on flag %s)!\n", pcFlag);
		return false;
	} else {
		pAnimWordFeed->ppcFlags[pAnimWordFeed->uiNumFlags] = pcFlag;
		pAnimWordFeed->uiNumFlags++;
		return true;
	}
}

bool dynSkeletonSetStanceInAnimWordFeed(DynAnimWordFeed *pAnimWordFeed, const char *pcStance)
{
	U32 i;

	assert(0 <= pAnimWordFeed->uiNumStances && pAnimWordFeed->uiNumStances <= DAWF_MaxActiveStances);
	assert(0 <= pAnimWordFeed->uiNumStanceActions && pAnimWordFeed->uiNumStanceActions <= DAWF_MaxStanceChanges);

	//check if the stance already exist
	for (i = 0; i < pAnimWordFeed->uiNumStances; i++)
	{
		if (pAnimWordFeed->ppcStances[i] == pcStance)
		{
			//don't add it twice
			Errorf("Attempted to add a stance (%s) that was already present to a DynAnimWordFeed!\n", pcStance);
			return false;
		}
	}

	//add the stance
	if (pAnimWordFeed->uiNumStances == DAWF_MaxActiveStances) {
		Errorf("Attempted to set too many stances in a DynAnimWordFeed (while on stance %s)!\n", pcStance);
		return false;
	} else if (pAnimWordFeed->uiNumStanceActions == DAWF_MaxStanceChanges) {
		Errorf("Attempted to modify too many stances in a DynAnimWordFeed (while adding stance %s)!\n", pcStance);
		return false;
	} else {
		pAnimWordFeed->ppcStances[pAnimWordFeed->uiNumStances] = pcStance;
		pAnimWordFeed->uiNumStances++;
		pAnimWordFeed->pStanceActions[pAnimWordFeed->uiNumStanceActions].pcWord = pcStance;
		pAnimWordFeed->pStanceActions[pAnimWordFeed->uiNumStanceActions].bSet = true;
		pAnimWordFeed->uiNumStanceActions++;
		return true;
	}
}

bool dynSkeletonClearStanceInAnimWordFeed(DynAnimWordFeed *pAnimWordFeed, const char *pcStance)
{
	U32 i;

	assert(0 <= pAnimWordFeed->uiNumStances && pAnimWordFeed->uiNumStances <= DAWF_MaxActiveStances);
	assert(0 <= pAnimWordFeed->uiNumStanceActions && pAnimWordFeed->uiNumStanceActions <= DAWF_MaxStanceChanges);

	//check if the stance already exist
	for (i = 0; i < pAnimWordFeed->uiNumStances; i++)
	{
		if (pAnimWordFeed->ppcStances[i] == pcStance)
		{
			//remove it
			if (pAnimWordFeed->uiNumStanceActions == DAWF_MaxStanceChanges) {
				Errorf("Attempted to modify too many stances in a DynAnimWordFeed (while clearing stance %s)!\n", pcStance);
				return false;
			} else {
				pAnimWordFeed->uiNumStances--;
				while (i < pAnimWordFeed->uiNumStances) {
					pAnimWordFeed->ppcStances[i] = pAnimWordFeed->ppcStances[i+1];
					i++;
				}
				pAnimWordFeed->pStanceActions[pAnimWordFeed->uiNumStanceActions].pcWord = pcStance;
				pAnimWordFeed->pStanceActions[pAnimWordFeed->uiNumStanceActions].bSet = false;
				pAnimWordFeed->uiNumStanceActions++;
				return true;
			}
		}
	}

	//stance wasn't present
	Errorf("Attempted to remove a stance (%s) that wasn't part of a DynAnimWordFeed!\n", pcStance);
	return false;
}

bool dynSkeletonToggleStanceInAnimWordFeed(DynAnimWordFeed *pAnimWordFeed, const char *pcStance)
{
	U32 i;

	assert(0 <= pAnimWordFeed->uiNumStances && pAnimWordFeed->uiNumStances <= DAWF_MaxActiveStances);
	assert(0 <= pAnimWordFeed->uiNumStanceActions && pAnimWordFeed->uiNumStanceActions <= DAWF_MaxStanceChanges);

	//check to see if the stance already exist
	for (i = 0; i < pAnimWordFeed->uiNumStances; i++)
	{
		if (pAnimWordFeed->ppcStances[i] == pcStance)
		{
			//remove a found stance
			if (pAnimWordFeed->uiNumStanceActions == DAWF_MaxStanceChanges) {
				Errorf("Attempted to modify too many stances in a DynAnimWordFeed (while toggling stance %s off)!\n", pcStance);
				return false;
			} else {
				pAnimWordFeed->uiNumStances--;
				while (i < pAnimWordFeed->uiNumStances) {
					pAnimWordFeed->ppcStances[i] = pAnimWordFeed->ppcStances[i+1];
					i++;
				}
				pAnimWordFeed->pStanceActions[pAnimWordFeed->uiNumStanceActions].pcWord = pcStance;
				pAnimWordFeed->pStanceActions[pAnimWordFeed->uiNumStanceActions].bSet = false;
				pAnimWordFeed->uiNumStanceActions++;
				return true;
			}
		}
	}

	//add a missing stance
	if (pAnimWordFeed->uiNumStances == DAWF_MaxActiveStances) {
		Errorf("Attempted to set too many stances on a DynAnimWordFeed (while toggling stance %s)!\n", pcStance);
		return false;
	} else if (pAnimWordFeed->uiNumStanceActions == DAWF_MaxStanceChanges) {
		Errorf("Attempted to modify too many stances in a DynAnimWordFeed (while toggling stance %s on)!\n", pcStance);
		return false;
	} else {
		pAnimWordFeed->ppcStances[pAnimWordFeed->uiNumStances] = pcStance;
		pAnimWordFeed->uiNumStances++;
		pAnimWordFeed->pStanceActions[pAnimWordFeed->uiNumStanceActions].pcWord = pcStance;
		pAnimWordFeed->pStanceActions[pAnimWordFeed->uiNumStanceActions].bSet = true;
		pAnimWordFeed->uiNumStanceActions++;
		return true;
	}
}

const char* dynSkeletonGetNodeAlias(const DynSkeleton* pSkeleton, const char* pcTag, bool bUseMountNodeAliases)
{
	// default to the main character's skeleton, unless we're handed a mount specific fx
	const DynSkeleton *pRiderSkeleton = pSkeleton->bMount && !bUseMountNodeAliases ? dynSkeletonGetRider(pSkeleton) : NULL;
	const DynSkeleton *pLookupSkeleton = FIRST_IF_SET(pRiderSkeleton, pSkeleton);
	
	// refine the node alias based on the lookup skeleton
	const WLCostume *pCost = GET_REF(pLookupSkeleton->hCostume);
	const SkelInfo *pSkelInfo = pCost ? GET_REF(pCost->hSkelInfo) : NULL;
	const DynAnimNodeAliasList *pSkelAliasList = pSkelInfo ? GET_REF(pSkelInfo->hAnimNodeAliasList) : NULL;
	const char *pcAlias = dynAnimNodeFxAlias(pSkelAliasList, pcTag);

	// refine the node alias based on the character's costume
	if (pCost && pCost->bHasNodeAliases) {
		FOR_EACH_IN_EARRAY(pCost->eaCostumeParts, WLCostumePart, pCostPart) {
			pcAlias = dynAnimNodeFxAlias(GET_REF(pCostPart->hAnimNodeAliasList), pcAlias);
		} FOR_EACH_END;
	}

	return pcAlias;
}

const char* dynSkeletonGetDefaultNodeAlias(const DynSkeleton* pSkeleton, bool bUseMountNodeAliases)
{
	// default to the main character's skeleton, unless we're handed a mount specific fx
	const DynSkeleton *pRiderSkeleton = pSkeleton->bMount && !bUseMountNodeAliases ? dynSkeletonGetRider(pSkeleton) : NULL;
	const DynSkeleton *pLookupSkeleton = FIRST_IF_SET(pRiderSkeleton, pSkeleton);

	// refine the node alias based on the lookup skeleton
	const WLCostume *pCost = GET_REF(pLookupSkeleton->hCostume);
	const SkelInfo *pSkelInfo = pCost ? GET_REF(pCost->hSkelInfo) : NULL;
	const DynAnimNodeAliasList *pSkelAliasList = pSkelInfo ? GET_REF(pSkelInfo->hAnimNodeAliasList) : NULL;
	const char *pcAlias = dynAnimNodeFxDefaultAlias(pSkelAliasList);

	return pcAlias;
}

static void dynSkeletonBroadcastFXMessage(DynFxManager *pManager, const char *pcMessage) {
	dynSkeletonLockFX(); {
		dynFxManBroadcastMessage(pManager, pcMessage);
	} dynSkeletonUnlockFX();
}

void dynSkeletonSetSnapshot(const DynSkeleton *pSrcSkeleton, DynSkeleton *pTgtSkeleton)
{
	WLCostume* pSrcCostume, *pTgtCostume;

	if ((pSrcCostume = GET_REF(pSrcSkeleton->hCostume)) &&
		(pTgtCostume = GET_REF(pTgtSkeleton->hCostume))	&&
		GET_REF(pSrcCostume->hSkelInfo) == GET_REF(pTgtCostume->hSkelInfo))
	{
		pTgtSkeleton->bSnapshot = true;
		dynNodeTreeCopyTransformsOnLocalSkeletonOnly(pSrcSkeleton->pRoot, pTgtSkeleton->pRoot);

		// orders might be different due to attachments (such as added FX skeletons)
		// outer loop cycles target skeletons so each one is hit 1x max
		FOR_EACH_IN_EARRAY(pTgtSkeleton->eaDependentSkeletons, DynSkeleton, pTgtChildSkeleton) {
			FOR_EACH_IN_EARRAY(pSrcSkeleton->eaDependentSkeletons, DynSkeleton, pSrcChildSkeleton) {
				dynSkeletonSetSnapshot(pSrcChildSkeleton, pTgtChildSkeleton);
			} FOR_EACH_END;
		} FOR_EACH_END;
	}
}

void dynSkeletonReleaseSnapshot(DynSkeleton* pSkeleton)
{
	if (TRUE_THEN_RESET(pSkeleton->bSnapshot))
	{
		FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton) {
			dynSkeletonReleaseSnapshot(pChildSkeleton);
		} FOR_EACH_END;
	}
}

DynSkeleton* dynSkeletonGetRider(const DynSkeleton* pSkeleton)
{
	if (pSkeleton->bMount)
	{
		FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton)
		{
			if (pChildSkeleton->bRider)
				return pChildSkeleton;
		}
		FOR_EACH_END;
	}

	return NULL;
}

const DynSkeleton* dynSkeletonFindByCostumeTag(const DynSkeleton* pSkeleton, const char* pcCostumeFxTag)
{
	if (pSkeleton->pcCostumeFxTag == pcCostumeFxTag)
	{
		return pSkeleton;
	}
	else
	{
		FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, const DynSkeleton, pSubSkeleton)
		{
			const DynSkeleton* pResult = dynSkeletonFindByCostumeTag(pSubSkeleton, pcCostumeFxTag);
			if (pResult)
				return pResult;
		}
		FOR_EACH_END;
	}

	return NULL;
}

#include "autogen/dynSkeleton_h_ast.c"
