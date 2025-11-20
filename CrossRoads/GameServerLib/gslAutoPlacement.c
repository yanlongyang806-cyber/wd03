#ifndef NO_EDITORS

#include "autoPlacementCommon.h"

#include "mathutil.h"
#include "bounds.h"
#include "earray.h"

#include "GlobalTypes.h"

#include "error.h"
#include "errornet.h"
#include "ticketnet.h"

#include "estring.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"

#include "Entity.h"
#include "beacon.h"
#include "beaconPath.h"
#include "../beaconPrivate.h" // cheating the system!


#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "rand.h"
#include "StringCache.h"
#include "Expression.h"
#include "Materials.h"
#include "wlPhysicalProperties.h"
#include "oldencounter_common.h"
#include "gslEncounter.h"

// -------------------------------------------------------------------------------------
// types
// -------------------------------------------------------------------------------------

typedef struct PotentialObjectPos
{
	Vec3	vPos;
	
} PotentialObjectPos;

typedef struct APPotentialObjectPos
{
	Vec3		vPos;
	const PotentialLineSegment	*pParentSegment;
	
	F32			*apCachedFitnessValues;	// an array of cached fitness values for the group/objects 

	U32			bInvalidPos : 1;	// after used, or rejected for whatever reason
} APPotentialObjectPos;

typedef struct ProximityParams
{
	F32 fProximitySQ;
	F32 fMinProximitySQ;
	F32 fRangeSQ;
} ProximityParams;

typedef struct APRandomPlacedObject
{
	S32		groupIdx;
	S32		objectIdx;

	AutoPlacementObject *pRandomObject;
	AutoPlacementGroup *pRandomGroup;

} APRandomPlacedObject;


static bool apPreValidatePosition(int iPartitionIdx, const AutoPlacementParams *pParams, const Vec3 vPosition);


// -------------------------------------------------------------------------------------
// global / static vars
#define MAX_PERCENTAGE 100.0f
#define UNINITIALIZED_FITNESS	(-1.0f)
#define INVALID_FITNESS (-2.0f)

static EArray64Handle			s_eaIdxYawList = NULL;
static F32 s_placedObjectCount = 0;
static bool s_bPositionOutOfWorld = false;

extern AutoPlacementExpressionData g_AutoPlacementExprData;

static const F32 s_fExpressionFitnessThreshold = 0.925f; 

// this could probably become a parameter or based on either of the object's collision
static const F32 s_fMinDistanceToExisting = 6.0f;

struct 
{
	APPotentialObjectPos	**eaPotentialPositions;

	PotentialObjectPos		**eaSiblingAutoPlacedObjects;	
	
	F32						*pFitnessCacheBuffer;

	S32						iNumGroups;
	S32						*paiGroupOffsets;	// offsets to each group, for faster lookup
} s_APState;




// -------------------------------------------------------------------------------------
typedef struct APLineSegmentRandomIterator
{
	Vec3 vPt1;
	Vec3 vPt2;
	Vec3 vDirPt1ToPt2;
	F32 fLength;

	F32 fRange;
	F32 fStep;

} APLineSegmentRandomIterator;

// -------------------------------------------------------------------------------------
void apInitializeLineSegmentIterator(APLineSegmentRandomIterator *pData, const PotentialLineSegment *pSegment, F32 fRange, F32 fStep)
{
	copyVec3(pSegment->vPt1, pData->vPt1);
	copyVec3(pSegment->vPt2, pData->vPt2);
	subVec3(pSegment->vPt2, pSegment->vPt1, pData->vDirPt1ToPt2);
	pData->fLength = normalVec3(pData->vDirPt1ToPt2);
	pData->fRange = fRange;
	pData->fStep = fStep;
}


// -------------------------------------------------------------------------------------
// gets a random position in the range on a line from a given end on the segment
// The pNewSegment AutoPlaceLineSegment is a clipped version of the segment. 
// Creates a degenerate line segment if the clipped line is 
//
// returns false if the line segment is degenerate
bool apGetRandomPosAndPruneLineSegment(APLineSegmentRandomIterator *pData, Vec3 dstPos)
{
	F32 randDistance;

	if (pData->fLength <= 0.0001f)
	{
		return false;
	}

	// get the random distance to create the position
	randDistance = randomPositiveF32() * pData->fRange;
	if (randDistance > pData->fLength)
	{
		randDistance = pData->fLength;
	}

	if (randomBool())
	{	// from point1

		scaleAddVec3(pData->vDirPt1ToPt2, randDistance, pData->vPt1, dstPos);

		// move point1 in by the random distance plus the range. 
		randDistance = randDistance + pData->fStep;
		scaleAddVec3(pData->vDirPt1ToPt2, randDistance, pData->vPt1, pData->vPt1);
	}
	else
	{	// from point2
		scaleAddVec3(pData->vDirPt1ToPt2, -randDistance, pData->vPt2, dstPos);

		// move point2 in by the random distance plus the range. 
		randDistance = randDistance + pData->fStep;
		scaleAddVec3(pData->vDirPt1ToPt2, -randDistance, pData->vPt2, pData->vPt2);
	}

	pData->fLength -= randDistance;
	return true;
}



// -------------------------------------------------------------------------------------
static bool groupTreeTraverseCallback(void *user_data, GroupDef *def, GroupInfo *info, 
							   GroupInheritedInfo *inherited_info, bool needs_entry)
{
#define AUTO_PLACEGROUP_DEPTH	3

	S32 numParents = eaSize(&inherited_info->parent_defs);
	
	if (numParents > AUTO_PLACEGROUP_DEPTH)
	{
		GroupDef *pParent = inherited_info->parent_defs[numParents-AUTO_PLACEGROUP_DEPTH];
		if (strncmp(pParent->name_str, "AutoPlacementGroup", strlen("AutoPlacementGroup")) == 0)
		{
			PotentialObjectPos *pPos = malloc(sizeof(PotentialObjectPos));
			copyVec3(info->world_matrix[3], pPos->vPos);
			eaPush(&s_APState.eaSiblingAutoPlacedObjects, pPos);
		}
	}

	return true;
}

// -------------------------------------------------------------------------------------
static void apGetSiblingAutoPlacedObjects()
{
	int i;
	S32 numLayers = zmapGetLayerCount(NULL);
	for (i = 0; i < numLayers; i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		if (layer)
		{
			layerGroupTreeTraverse(layer, groupTreeTraverseCallback, NULL, false, true);
		}
	}
}


// -------------------------------------------------------------------------------------
__forceinline static U64 packIdxYaw(S32 i, F32 f)
{
	U64 ui = *((U32*)&f);
	ui |= ((U64)i) << 32;
	return ui;
}

#define getYawFromPU64(iy)	(*((F32*)(iy)))
#define getYawFromU64(iy)	(*((F32*)&(iy)))
#define getIdxFromU64(iy)	((S32)((iy) >> 32))

// -------------------------------------------------------------------------------------
static int idxYaw64SortComparator(const U64 *f1, const U64 *f2)
{
	if (getYawFromPU64(f1) < getYawFromPU64(f2))
	{
		return -1;
	}
	else if (getYawFromPU64(f1) > getYawFromPU64(f2))
	{
		return 1;
	}

	return 0;
}

// -------------------------------------------------------------------------------------
static void destroyBeaconYawList()
{
	eai64Destroy(&s_eaIdxYawList);
}

// -------------------------------------------------------------------------------------
static F32 apGetBeaconConnectionMaxYaw(const Beacon *pBeacon, S32 *pConn1, S32 *pConn2)
{
	S32 i;
	F32 maxYaw, yaw;
	S32 maxYawConn1, maxYawConn2;

	maxYawConn1 = 0;
	maxYawConn2 = 0;
	maxYaw = 0.0f;

	eai64Clear(&s_eaIdxYawList);

	if (pBeacon->gbConns.size <= 1)
		return 0.0f;

	// first compute all the yaws to the connections
	for(i = 0; i < pBeacon->gbConns.size; i++)
	{
		Vec3 vDir;
		U64 idxYawPair;
		BeaconConnection *pConn =  pBeacon->gbConns.storage[i];
		Beacon* pConnBeacon = pConn->destBeacon;

		subVec3(pConnBeacon->pos, pBeacon->pos, vDir);
		yaw = getVec3Yaw(vDir);
		yaw = SIMPLEANGLE(yaw);

		idxYawPair = packIdxYaw(i, yaw);
		eai64Push(&s_eaIdxYawList, idxYawPair);
	}

	// now sort the array from lowest to highest.
	ea64QSort(s_eaIdxYawList, idxYaw64SortComparator);

	// walk the array, and get the largest angle difference between the yaws
	{
		S32 size = eai64Size(&s_eaIdxYawList);
		S32 nexti = 1;
		for(i = 0; i < size; i++)
		{
			yaw = getYawFromU64(s_eaIdxYawList[nexti]) - getYawFromU64(s_eaIdxYawList[i]);

			if (yaw < 0.0f)
			{
				yaw += RAD(360.0f);
			}

			if (yaw > maxYaw)
			{
				maxYaw = yaw;
				maxYawConn1 = getIdxFromU64(s_eaIdxYawList[i]);
				maxYawConn2 = getIdxFromU64(s_eaIdxYawList[nexti]);
			}


			nexti = (nexti + 1) % size;
		}
	}

	if (pConn1) *pConn1 = maxYawConn1;
	if (pConn2) *pConn2 = maxYawConn2;
	
	return maxYaw;
}

// -------------------------------------------------------------------------------------
__forceinline static eConnectionType apClassifyYaw(F32 fYaw)
{
#define EXTERNAL_CORNER_MIN		RAD(120.0f)
#define EDGE_MIN				RAD(145.0f)
#define INTERNAL_CORNER_MIN		RAD(225.0f)

	if (fYaw < EXTERNAL_CORNER_MIN)
	{
		return eConnectionType_NONE;
	}
	else if (fYaw > EXTERNAL_CORNER_MIN && fYaw <= EDGE_MIN)
	{
		return eConnectionType_EXTERNAL_CORNER;
	}
	else if (fYaw > EDGE_MIN && fYaw <= INTERNAL_CORNER_MIN)
	{
		return eConnectionType_EDGE;
	}
	else// if (fYaw > INTERNAL_CORNER_MIN)
	{
		return eConnectionType_INTERNAL_CORNER;
	}
}

// -------------------------------------------------------------------------------------
static eConnectionType apClassifyConnectionType(const Beacon *pBeacon, S32 iConnection)
{
	S32 iConn1, iConn2;
	F32 fMaxYaw;

	fMaxYaw = apGetBeaconConnectionMaxYaw(pBeacon, &iConn1, &iConn2);
	if (iConnection == iConn1 || iConnection == iConn2)
	{
		return apClassifyYaw(fMaxYaw);
	}

	return eConnectionType_NONE;
}


// -------------------------------------------------------------------------------------
__forceinline static bool apIsPointInAVolume(const AutoPlacementVolume** ppVolumeList, const Vec3 vPt)
{
	int i;
	for (i = 0; i < eaSize(&ppVolumeList); i++)
	{
		const AutoPlacementVolume *pVolume = ppVolumeList[i];

		if (apvPointInVolume(pVolume, vPt))
			return true;
	}
	
	return false;
}

// -------------------------------------------------------------------------------------
static void apGetCenterRadiusFromVolumes( const AutoPlacementVolume** ppVolumeList, Vec3 vCenter, F32 *pfRadius )
{
	Vec3 vMin, vMax, vTmpMin, vTmpMax;
	int i;

	assert(pfRadius);
	setVec3(vMin, FLT_MAX, FLT_MAX, FLT_MAX);
	setVec3(vMax, -FLT_MAX, -FLT_MAX, -FLT_MAX);
	
	for (i = 0; i < eaSize(&ppVolumeList); i++)
	{
		const AutoPlacementVolume *pVolume = ppVolumeList[i];
		
		if (pVolume->bAsCube)
		{
			addVec3(pVolume->vPos, pVolume->vMin, vTmpMin);
			addVec3(pVolume->vPos, pVolume->vMax, vTmpMax);
		}
		else
		{
			subVec3same(pVolume->vPos, pVolume->fRadius, vTmpMin);
			addVec3same(pVolume->vPos, pVolume->fRadius, vTmpMax);
		}

		MINVEC3(vMin, vTmpMin, vMin);
		MAXVEC3(vMax, vTmpMax, vMax);
	}

	*pfRadius = boxCalcMid(vMin, vMax, vCenter);
}


// -------------------------------------------------------------------------------------
// returns the index of the beacon connection that the given beacon is connected to pConnecetedBeacon
// returns -1 if no connection exists
__forceinline static int apFindIndexBeaconConnectedToBeaconViaGround(const Beacon* beacon, const Beacon* pConnecetedBeacon)
{
	int x;
	assert(pConnecetedBeacon);
	for (x = 0; x < pConnecetedBeacon->gbConns.size; x++)
	{
		BeaconConnection *pConnection = pConnecetedBeacon->gbConns.storage[x];
		if (pConnection->destBeacon == beacon)
			return x;
	}

	return -1;
}

#define MINIMUM_OUTGOING_GROUND_CONNECTIONS	(3)
#define MINIMUM_INCOMING_GROUND_CONNECTIONS (3)
static const S32 MINIMUM_GALAXY_SIZE = 60;

__forceinline static bool apHasMinimumIncomingGroundConnections(const Beacon* beacon)
{
	int iIncomingGroundConnections = 0;
	int i;

	// check all our ground connections 
	for (i = 0; i < beacon->gbConns.size; i++)
	{
		BeaconConnection *pConnection = beacon->gbConns.storage[i];

		if (apFindIndexBeaconConnectedToBeaconViaGround(beacon, pConnection->destBeacon) != -1)
		{
			if (++iIncomingGroundConnections >= MINIMUM_INCOMING_GROUND_CONNECTIONS )
				return true;
		}
	}

#if 0
	// check all our raised connections 
	for (i = 0; i < beacon->rbConns.size; i++)
	{
		BeaconConnection *pConnection = beacon->rbConns.storage[i];
		Beacon *pDstBeacon = pConnection->destBeacon;

		// find out if it is connected via ground
		for (x = 0; x < pDstBeacon->gbConns.size; x++)
		{
			pConnection = pDstBeacon->gbConns.storage[x];
			if (pConnection->destBeacon == beacon)
			{
				if (++iIncomingGroundConnections >= MINIMUM_INCOMING_GROUND_CONNECTIONS )
					return true;

				break;
			}
		}
	}
#endif

	return false;
}


// -------------------------------------------------------------------------------------
void beaconRebuildBlocks(int requireValid, int quiet, int partitionId);
static bool apFilterBeaconsOutOfVolumes(Beacon* beacon, AutoPlacementParams* pParams)
{
	// check to see if this beacon has enough ground connections
	// 
	if (beacon->gbConns.size >= MINIMUM_OUTGOING_GROUND_CONNECTIONS)
	{
		if (apHasMinimumIncomingGroundConnections(beacon))
		{
			if (apIsPointInAVolume(pParams->ppAutoPlacementVolumes, beacon->pos))
			{
				BeaconPartitionData *beaconPartition = beaconGetPartitionData(beacon, 0, false);
				if (beaconPartition->block && beaconPartition->block->galaxy)
				{
					BeaconBlock *pGalaxy = beaconPartition->block->galaxy;
					S32 i, beaconCount = 0;

					for (i = 0; i < pGalaxy->subBlockArray.size; i++)
					{
						BeaconBlock *pBlock = pGalaxy->subBlockArray.storage[i];
						beaconCount += pBlock->beaconArray.size;
					}

					return (beaconCount > MINIMUM_GALAXY_SIZE);
				}
				else
				{
					return true;
				}
			}
		}
	}
	return false;
}

// -------------------------------------------------------------------------------------
static const F32 BEACON_OFFSET_DISTANCE = 5.0f;
static const F32 BEACON_DISTANCE_THRESHOLD_SQ = SQR(7.0f);
static const S32 PATHFIND_DETOUR_THRESHOLD = 5;

typedef struct BeaconConnectionStash
{
	const Beacon*	pBeacon1;
	const Beacon*	pBeacon2;
} BeaconConnectionStash;

__forceinline void initBeaconConnectionStash(const Beacon *pB1, const Beacon *pB2, BeaconConnectionStash *pConn)
{
	if (pB1 < pB2)
	{
		pConn->pBeacon1 = pB1;
		pConn->pBeacon2 = pB2;
	}
	else
	{
		pConn->pBeacon1 = pB2;
		pConn->pBeacon2 = pB1;
	}
}

void freeBeaconConnectionStash(void *pConn)
{
	free(pConn);
}

// -------------------------------------------------------------------------------------
static void apGetPotentialPlacementFromBeacons(int iPartitionIdx, const AutoPlacementParams *pParams, PotentialLineSegment ***ppapLineSegmentList)
{
	Vec3 vSearchPos;
	F32 fSearchRadius;
	Beacon **ppBeaconList = NULL;
	StashTable processedConnectionsStash;
	int i;
	
	
	// first refresh the dynamic connections to get the most up-to-date connections
	beaconCheckDynConnQueue();

	loadstart_printf("(Auto-Placement) Getting potential placement from beacons. Qualifying beacons & connections.\n" );
	
	// get the list of beacons we will be dealing with
	apGetCenterRadiusFromVolumes(pParams->ppAutoPlacementVolumes, vSearchPos, &fSearchRadius);
	
	beaconRebuildBlocks(0, 1, 0);

	beaconGetNearbyBeacons(&ppBeaconList, vSearchPos, fSearchRadius, (FilterBeaconFunc)apFilterBeaconsOutOfVolumes, (void*)pParams);
	
	if (! eaSize(&ppBeaconList))
	{
		loadend_printf("done processing beacons.");
		printf("\nWarning: (Auto-Placement) Could not find beacons in volumes. Check to make sure beacons are generated for the map and that you are not running with the '-noai' option.\n");
		return;
	}

	
	processedConnectionsStash = stashTableCreate(eaSize(&ppBeaconList), StashDefault, StashKeyTypeFixedSize, sizeof(BeaconConnectionStash));
		
	
	for( i = 0; i < eaSize(&ppBeaconList); i++)
	{
		int x;
		Beacon* pBeacon = (Beacon*)ppBeaconList[i];
		
		for (x = 0; x < pBeacon->gbConns.size; x++)
		{
			//	. for each ground connection (not necessarily in this order)
			//		. make sure we have not processed this connection yet
			//		. check if the distance to this beacon is greater than the beacon offset distance
			//		. find if the beacon I'm connected to is connected to me via ground.
			//		. if kosher, the segment to the list of segments
			Vec3 vToConnectedBeacon;
			bool isKosher = true;
			S32 incomingConnectionIdx;
			
			BeaconConnection *pConnection = pBeacon->gbConns.storage[x];
			Beacon* pConnectedBeacon = pConnection->destBeacon;
			
			incomingConnectionIdx = apFindIndexBeaconConnectedToBeaconViaGround(pBeacon, pConnectedBeacon);
			if (incomingConnectionIdx == -1)
			{
				// these beacons are not mutually connected via ground
				continue;
			}

			// check if this connection was already processed
			{
				BeaconConnectionStash connectionStash, *pNewStash;

				initBeaconConnectionStash(pBeacon, pConnectedBeacon, &connectionStash);

				if (stashFindPointer(processedConnectionsStash, &connectionStash, NULL))
				{
					// this connection was already processed
					continue;
				}

				pNewStash = malloc(sizeof(BeaconConnectionStash));
				pNewStash->pBeacon1 = connectionStash.pBeacon1;
				pNewStash->pBeacon2 = connectionStash.pBeacon2;

				stashAddPointer(processedConnectionsStash, pNewStash, pNewStash, false);
			}

			subVec3(pConnectedBeacon->pos, pBeacon->pos, vToConnectedBeacon);
			if (lengthVec3Squared(vToConnectedBeacon) <= BEACON_DISTANCE_THRESHOLD_SQ)
			{
				// these beacons are too close to each other. 
				isKosher = false;
			}
			
			// 
			if (isKosher)
			{
				// check if this path is blocked, make sure we can still get to the destination via other means
				NavPath path = {0};
				S32 numWaypoints;
				
				beaconSetPathFindEntityAsParameters(0, 0, 0, 1, 0, 0);

				// sever the connection, preserve the order
				arrayRemoveAndShift(&pBeacon->gbConns, x);
				// find a path
				beaconPathFindBeacon(iPartitionIdx, &path, ((Beacon*)pBeacon), pConnectedBeacon);
				// restore the connection, preserving the order
				arrayInsert(&pBeacon->gbConns, x, pConnection);
				
				numWaypoints = eaSize(&path.waypoints);
				navPathClear(&path);
				if (numWaypoints == 0 || numWaypoints > PATHFIND_DETOUR_THRESHOLD)
				{
					// could not find a path, or detour path was too large
					isKosher = false;	
				}
			}
			

			// normalize the vector from the beacon to its connection
			normalVec3(vToConnectedBeacon);
			
			// create the potential segment
			{
				PotentialLineSegment *pPotentialLineSegment;

				pPotentialLineSegment = calloc(1, sizeof(PotentialLineSegment));
				
				pPotentialLineSegment->isKosher = isKosher;

				// non-kosher line segments will serve just to provide topographical information
				if (isKosher)
				{
					// offset the position BEACON_OFFSET_DISTANCE away from each of the beacon's positions
					scaleAddVec3(vToConnectedBeacon, BEACON_OFFSET_DISTANCE, pBeacon->pos, pPotentialLineSegment->vPt1);
					scaleAddVec3(vToConnectedBeacon, -BEACON_OFFSET_DISTANCE, pConnectedBeacon->pos, pPotentialLineSegment->vPt2);
				}
				else
				{
					copyVec3(pBeacon->pos, pPotentialLineSegment->vPt1);
					copyVec3(pConnectedBeacon->pos, pPotentialLineSegment->vPt2);
				}
				
				// get the connection classification for each point
				pPotentialLineSegment->pt1Type = apClassifyConnectionType(pBeacon, x);
				pPotentialLineSegment->pt2Type = apClassifyConnectionType(pConnectedBeacon, incomingConnectionIdx);
				
				eaPush(ppapLineSegmentList, pPotentialLineSegment);
			}
			
		}
		
		
	}
	
	// note: we have a list of line segments that may be extending out of the list of volumes
	//	we may want to clip the lines to the volume, however for now every time a potential position is
	//	created, we'll test if the point is in a volume 

	stashTableDestroyEx(processedConnectionsStash, NULL, freeBeaconConnectionStash);
	eaDestroy(&ppBeaconList);

	destroyBeaconYawList();

	loadend_printf("done processing beacons.");
}

// -------------------------------------------------------------------------------------
static void apGetRandomPlacementPositions(int iPartitionIdx, APPotentialObjectPos ***peaPotentialPositions, const PotentialLineSegment **eaPotentialLineSegments, const AutoPlacementParams *pParams)
{
	S32 i, numLines;
	F32 fRange, fStep;

	loadstart_printf("(Auto-Placement) Generating and validating random positions from qualified beacons." );

	fRange = pParams->pPlacementProperties->proximity - pParams->pPlacementProperties->variance; 
	fStep = pParams->pPlacementProperties->proximity;

	numLines = eaSize(&eaPotentialLineSegments);

	for (i = 0; i < numLines; i++)
	{
		Vec3 vPotentialPostion;
		APLineSegmentRandomIterator	lineSegIt;
		const PotentialLineSegment *pLineSegment = eaPotentialLineSegments[i];

		if (pLineSegment->isKosher == false)
			continue;

		apInitializeLineSegmentIterator(&lineSegIt, pLineSegment, fRange, fStep);
		
		// get random positions over this line segment
		while(apGetRandomPosAndPruneLineSegment(&lineSegIt, vPotentialPostion))
		{
			if (apPreValidatePosition(iPartitionIdx, pParams, vPotentialPostion))
			{
				// this position has been pre-validated, put it on our list
				APPotentialObjectPos *pAPPos = calloc(1, sizeof(APPotentialObjectPos));
				//
				pAPPos->pParentSegment = pLineSegment;
				copyVec3(vPotentialPostion, pAPPos->vPos);
				// 
				eaPush(peaPotentialPositions, pAPPos);
			}
		}
	}

	loadend_printf("done.");
}

// -------------------------------------------------------------------------------------
static void apAllocateCachedFitnessBuffers(const AutoPlacementParams *pParams, APPotentialObjectPos **eaPotentialPositions)
{
	S32 iGroupObjectCount, iAllocationCount;
	S32 iOffset;
	S32 i;
	F32 *pfCurBuffer;

	
	iGroupObjectCount = eaSize(&pParams->pPlacementProperties->auto_place_group);
	
	iOffset = 0;
	s_APState.iNumGroups = iGroupObjectCount;
	s_APState.paiGroupOffsets = malloc(sizeof(S32) * iGroupObjectCount);

	for (i = 0; i < eaSize(&pParams->pPlacementProperties->auto_place_group); i++)
	{
		AutoPlacementGroup *pGroup = pParams->pPlacementProperties->auto_place_group[i];

		s_APState.paiGroupOffsets[i] = iOffset;

		iOffset += eaSize(&pGroup->auto_place_objects) + 1;
		iGroupObjectCount += eaSize(&pGroup->auto_place_objects);
	}
	

	iAllocationCount = iGroupObjectCount * eaSize(&eaPotentialPositions);
	s_APState.pFitnessCacheBuffer = malloc(sizeof(F32*) * iAllocationCount);
	if (!s_APState.pFitnessCacheBuffer)
		return;
	
	// initialize all the fitness values to -1
	for(i = 0; i < iAllocationCount; i++)
	{
		s_APState.pFitnessCacheBuffer[i] = UNINITIALIZED_FITNESS;
	}
	
	// assign the object positions their cache buffer
	pfCurBuffer = s_APState.pFitnessCacheBuffer;
	for(i = 0; i < eaSize(&eaPotentialPositions); i++, pfCurBuffer += iGroupObjectCount)
	{
		APPotentialObjectPos *pAPPos = eaPotentialPositions[i];
		pAPPos->apCachedFitnessValues = pfCurBuffer;
	}
}

// -------------------------------------------------------------------------------------
static void apGetCachedFitnessValues(const APPotentialObjectPos *pAPPos, S32 groupIdx, S32 objectIdx, F32 *pfGroupFitness, F32 *pfObjectFitness)
{
	if (s_APState.pFitnessCacheBuffer)
	{
		S32 iGroupOffset;
		devassert(groupIdx < s_APState.iNumGroups);

		iGroupOffset = s_APState.paiGroupOffsets[groupIdx];

		*pfGroupFitness = pAPPos->apCachedFitnessValues[iGroupOffset];
		*pfObjectFitness = pAPPos->apCachedFitnessValues[iGroupOffset + objectIdx + 1];
	}
	else
	{
		*pfGroupFitness = UNINITIALIZED_FITNESS;
		*pfObjectFitness = UNINITIALIZED_FITNESS;
	}
}

// -------------------------------------------------------------------------------------
static void apCacheGroupFitnesValue(APPotentialObjectPos *pAPPos, S32 groupIdx, F32 fGroupFitness)
{
	if (s_APState.pFitnessCacheBuffer)
	{
		S32 iGroupOffset;
		devassert(groupIdx < s_APState.iNumGroups);

		iGroupOffset = s_APState.paiGroupOffsets[groupIdx];

		pAPPos->apCachedFitnessValues[iGroupOffset] = fGroupFitness;
	}
}

// -------------------------------------------------------------------------------------
static void apCacheObjectFitnesValue(APPotentialObjectPos *pAPPos, S32 groupIdx, S32 objectIdx, F32 fObjectFitness)
{
	if (s_APState.pFitnessCacheBuffer)
	{
		S32 iOffset;
		devassert(groupIdx < s_APState.iNumGroups);

		iOffset = s_APState.paiGroupOffsets[groupIdx] + objectIdx + 1;

		pAPPos->apCachedFitnessValues[iOffset] = fObjectFitness;
	}
}


#if 0
// -------------------------------------------------------------------------------------
static void getPotentialPointsFromBeacons(const AutoPlacementParams *pParams, PotentialObjectPos ***pppPotentialPointsList)
{
	Vec3 vSearchPos;
	F32 fSearchRadius;
	const Beacon **ppBeaconList;
	int i;

	apGetCenterRadiusFromVolumes(pParams->ppAutoPlacementVolumes, vSearchPos, &fSearchRadius);

	ppBeaconList = beaconGetNearbyBeacons(vSearchPos, fSearchRadius, (FilterBeaconFunc)apFilterBeaconsOutOfVolumes, (void*)pParams);
	
	if (! eaSize(&ppBeaconList))
		return;
	
	// it seems dumb that I am copying the beacon positions right into the potential positions
	// but at some point we will not be using the actual beacons, and potential positions will be based on 
	// something else. This way, I won't have to change the way the algorithm
	eaSetCapacity(pppPotentialPointsList, eaSize(&ppBeaconList));

	for( i = 0; i < eaSize(&ppBeaconList); i++)
	{
		const Beacon* pBeacon = ppBeaconList[i];
		PotentialObjectPos *pAPPos = calloc(1, sizeof(PotentialObjectPos));
		assert(pAPPos);
		copyVec3(pBeacon->pos, pAPPos->vPos);
		eaPush(pppPotentialPointsList, pAPPos);
	}

	eaDestroy(&ppBeaconList);
}
#endif 

// -------------------------------------------------------------------------------------
static bool apDoesExpressionValidatePosition(Expression *pExpression)
{
	MultiVal answer;
	
	exprEvaluate(pExpression, getAutoPlacementExprContext(), &answer);
	
	switch(answer.type)
	{
		acase MULTI_INT:
			return answer.intval != 0;
		xcase MULTI_FLOAT:
			return answer.floatval != 0.0f;
		xdefault:
			return true;
	}
}

// -------------------------------------------------------------------------------------
// integer returns get converted into boolean 0.0f or 1.0f
static F32 apEvaluateFitnessExpression(Expression *pExpression)
{
	MultiVal answer;

	exprEvaluate(pExpression, getAutoPlacementExprContext(), &answer);

	switch(answer.type)
	{
		acase MULTI_INT:
			return (F32)(answer.intval != 0);
		xcase MULTI_FLOAT:
			return answer.floatval;
		xdefault:
			return 0.0f;
	}
}

// -------------------------------------------------------------------------------------
// this function makes sure that the position is not within or underneath the geometry/terrain
static bool apIsPositionWithinWorld(int iPartitionIdx, const Vec3 vPos) 
{
	const F32 fCAST_DISTANCE = 20000.0f;
	static Vec3 s_avecTests[] =
	{
		{   0.0f,   1.0f,   0.0f }, // up
		{   0.0f,  -1.0f,   0.0f }, // down
		{   1.0f,   0.0f,   0.0f }, // left
		{  -1.0f,   0.0f,   0.0f }, // right
		{   0.0f,   0.0f,   1.0f }, // forward
		{   0.0f,   0.0f,  -1.0f }  // back
	};

	S32 i;
	Vec3 vTestPos;

	copyVec3(vPos, vTestPos);
	vTestPos[1] += 0.25f;
	
	// 
	for(i=0; i<ARRAY_SIZE(s_avecTests); i++)
	{
		WorldCollCollideResults rayCastResults;
		Vec3 vStart, vEnd;

		copyVec3(vTestPos, vStart);
		scaleAddVec3(s_avecTests[i], fCAST_DISTANCE, vTestPos, vEnd);
		if (worldCollideRay(iPartitionIdx, vStart, vEnd, WC_FILTER_BIT_MOVEMENT, &rayCastResults ))
		{
			const F32 fPUSH_DIST = 2.0f/12.0f; // 2 inches

			// we hit something, we need to cast from the impact point, so push it back in the direction of 
			// the cast to ensure we don't hit the impact point
			scaleAddVec3(s_avecTests[i], -fPUSH_DIST, rayCastResults.posWorldImpact, vEnd);
		}
		

		// cast backwards to see if we reach our original start position
		// if we hit something before we get to it, we started inside an object
		if (worldCollideRay(iPartitionIdx, vEnd, vStart, WC_FILTER_BIT_MOVEMENT, &rayCastResults))
		{
			// if we hit something, we will reject the point if the impact point is too far from the start
			if (distance3Squared(rayCastResults.posWorldImpact, vStart) > 1.0f)
			{
				s_bPositionOutOfWorld = true;
				return false;
			}
		}
	}

	return true;
}



// -------------------------------------------------------------------------------------
static bool apPreValidatePosition(int iPartitionIdx, const AutoPlacementParams *pParams, const Vec3 vPosition)
{
	S32 x, numSiblings; 

	if (! apIsPointInAVolume(pParams->ppAutoPlacementVolumes, vPosition))
	{
		return false;
	}

	numSiblings = eaSize(&s_APState.eaSiblingAutoPlacedObjects);
	// go through the positions of the autoPLacedObjects from other sets that we have gathered
	for (x = 0; x < numSiblings; x++)
	{
		const PotentialObjectPos *pPosData = s_APState.eaSiblingAutoPlacedObjects[x];

		F32 fDistSq = distance3Squared(pPosData->vPos, vPosition);
		if (fDistSq < SQR(s_fMinDistanceToExisting))
		{
			return false;
		}
	}

	if (! apIsPositionWithinWorld(iPartitionIdx, vPosition))
	{
		return false;
	}

	return true;
}

// -------------------------------------------------------------------------------------
static bool apIsPositionNearPlaced(const Vec3 vPosition,
								   const ProximityParams *pProximityParams, 
								   const AutoPlacePosition **ppPlacedPositionList,
								   S32 *pbRollFailed )
{
	const F32 MIN_CHANCE = 20.0f;
	const F32 MAX_CHANCE = 90.0f;
	const F32 CHANCE_RANGE = MAX_CHANCE - MIN_CHANCE;

	S32 x;

	*pbRollFailed = false;

	// go through the placed positions and check for proximity to other objects
	for (x = 0; x < eaSize(&ppPlacedPositionList); x++)
	{
		const AutoPlacePosition *pPosData = ppPlacedPositionList[x];

		F32 fDistSq = distance3Squared(pPosData->vPos, vPosition);
		if (fDistSq < pProximityParams->fProximitySQ)
		{
			F32 fPercentChance;
			if (fDistSq < pProximityParams->fMinProximitySQ)
			{
				// too close to another position, skip this pos
				return true;
			}

			// calculate the percentage chance that this is a valid position
			fPercentChance = (fDistSq - pProximityParams->fMinProximitySQ) / pProximityParams->fRangeSQ;
			fPercentChance = (fPercentChance * CHANCE_RANGE) + MIN_CHANCE;
			if (rule30FloatPct() > fPercentChance)
			{
				// roll failed, we are too close to another position, skip this pos
				*pbRollFailed = true;
				return true;
			}

		}
	}

	return false;
}

// -------------------------------------------------------------------------------------
typedef enum EPositionInvalidReason
{
	EPositionInvalidReason_NONE = 0,
	EPositionInvalidReason_REQUIREDEXPR_OBJECT,
	EPositionInvalidReason_REQUIREDEXPR_GROUP,

	EPositionInvalidReason_INVALIDATE_POS_marker,
	EPositionInvalidReason_PROXIMITY = EPositionInvalidReason_INVALIDATE_POS_marker,

} EPositionInvalidReason;

// -------------------------------------------------------------------------------------
// returns true if the position is valid for something to be placed at
static bool apIsObjectPlacementValid(const APRandomPlacedObject* pObject, 
							bool bValidateGroupExpr, bool bValidateObjectExpr,
							EPositionInvalidReason *eInvalidReason )
{

	/*
	if (! apIsPointInAVolume(pParams->ppAutoPlacementVolumes, vPosition))
	{
		return false;
	}
		
	// go through the positions of the autoPLacedObjects from other sets that we have gathered
	for (x = 0; x < eaSize(&s_APState.eaSiblingAutoPlacedObjects); x++)
	{
		const PotentialObjectPos *pPosData = s_APState.eaSiblingAutoPlacedObjects[x];
		
		F32 fDistSq = distance3Squared(pPosData->vPos, vPosition);
		if (fDistSq < SQR(s_fMinDistanceToExisting))
		{
			return false;
		}
	}
	*/
	
	// evaluate the expressions to see if this is a valid placement
	if (bValidateGroupExpr && pObject->pRandomGroup->required_condition)
	{
		if (! apDoesExpressionValidatePosition(pObject->pRandomGroup->required_condition))
		{
			*eInvalidReason = EPositionInvalidReason_REQUIREDEXPR_GROUP;
			return false;
		}
	}

	if (bValidateObjectExpr && pObject->pRandomObject->required_condition)
	{
		if (! apDoesExpressionValidatePosition(pObject->pRandomObject->required_condition))
		{
			*eInvalidReason = EPositionInvalidReason_REQUIREDEXPR_OBJECT;
			return false;
		}
	}

	/*
	if (! apIsPositionWithinWorld(vPosition))
	{
		return false;
	}
	*/

	*eInvalidReason = EPositionInvalidReason_NONE;


	return true;
}

// -------------------------------------------------------------------------------------
static void apFixupAutoPlacementProperties(AutoPlacementSet *pAutoPlaceSetProperties)
{
	F32 fGroupWeightSum;
	int i;

	fGroupWeightSum = 0.0f;
	// we are doing a few things to fix up the parameters:
	// . convert the weights to percentages (0 - 100)
	// . generate the expressions with our context
	// . calculate the target percentage
	for (i = 0; i < eaSize(&pAutoPlaceSetProperties->auto_place_group); i++)
	{
		AutoPlacementGroup *pAutoPlaceGroup = pAutoPlaceSetProperties->auto_place_group[i];
		
		if (eaSize(&pAutoPlaceGroup->auto_place_objects) > 0)
		{
			// initialize group variables
			pAutoPlaceGroup->can_place = true;
			exprGenerate(pAutoPlaceGroup->required_condition, getAutoPlacementExprContext());
			exprGenerate(pAutoPlaceGroup->fitness_expression, getAutoPlacementExprContext());

			fGroupWeightSum += pAutoPlaceGroup->weight;

			// process the objects in this group
			{
				F32 fObjectWeightSum = 0.0f;

				int x;
				// First go through all the Objects and initialize the expressions
				// as well as calculate the sum of all the weights
				for (x = 0; x < eaSize(&pAutoPlaceGroup->auto_place_objects); x++)
				{
					AutoPlacementObject *pAutoPlaceObject = pAutoPlaceGroup->auto_place_objects[x];

					pAutoPlaceObject->can_place = true;
					// generate the object's expression with our context
					exprGenerate(pAutoPlaceObject->required_condition, getAutoPlacementExprContext());
					exprGenerate(pAutoPlaceObject->fitness_expression, getAutoPlacementExprContext());

					fObjectWeightSum += pAutoPlaceObject->weight;
				}


				if (fObjectWeightSum > 0.0f)
				{
					F32 fSum = 0.0f;
					for (x = 0; x < eaSize(&pAutoPlaceGroup->auto_place_objects); x++)
					{
						AutoPlacementObject *pAutoPlaceObject = pAutoPlaceGroup->auto_place_objects[x];
						// calculate the percentage chance.
						pAutoPlaceObject->target_percentage = pAutoPlaceObject->weight / fObjectWeightSum;
						// add up the weight
						fSum += pAutoPlaceObject->target_percentage * MAX_PERCENTAGE;
						pAutoPlaceObject->weight = fSum;

						pAutoPlaceObject->priority = pAutoPlaceObject->target_percentage;
					}
				}
				else
				{	// all of the object weights were zero... give them all an equal probability
					F32 fEqualProbability = MAX_PERCENTAGE / (F32)eaSize(&pAutoPlaceGroup->auto_place_objects);

					for (x = 0; x < eaSize(&pAutoPlaceGroup->auto_place_objects); x++)
					{
						AutoPlacementObject *pAutoPlaceObject = pAutoPlaceGroup->auto_place_objects[x];

						pAutoPlaceObject->target_percentage = fEqualProbability;
						pAutoPlaceObject->weight = fEqualProbability * x;

						pAutoPlaceObject->priority = pAutoPlaceObject->target_percentage;
					}
				}
			}
			
		}
		
	}
	


	if (fGroupWeightSum > 0.0f)
	{
		F32 fSum = 0.0f;
		for (i = 0; i < eaSize(&pAutoPlaceSetProperties->auto_place_group); i++)
		{
			AutoPlacementGroup *pAutoPlaceGroup = pAutoPlaceSetProperties->auto_place_group[i];
			
			// calculate the target_percentage chance.
			pAutoPlaceGroup->target_percentage = pAutoPlaceGroup->weight / fGroupWeightSum;

			fSum += pAutoPlaceGroup->target_percentage * MAX_PERCENTAGE;
			pAutoPlaceGroup->weight = fSum;

			pAutoPlaceGroup->priority = pAutoPlaceGroup->target_percentage;

		}
	}
	else
	{
		// all of the group weights were zero... give them all an equal probability
		F32 fEqualProbability = MAX_PERCENTAGE / (F32)eaSize(&pAutoPlaceSetProperties->auto_place_group);
		for (i = 0; i < eaSize(&pAutoPlaceSetProperties->auto_place_group); i++)
		{
			AutoPlacementGroup *pAutoPlaceGroup = pAutoPlaceSetProperties->auto_place_group[i];

			pAutoPlaceGroup->target_percentage = fEqualProbability;
			pAutoPlaceGroup->weight = fEqualProbability * i;

			pAutoPlaceGroup->priority = pAutoPlaceGroup->target_percentage;
		}
	}

}

#if 0
// -------------------------------------------------------------------------------------
// finds the next non-empty group from the given index
__forceinline static S32 getNextNonEmptyGroup(AutoPlacementGroup **papAutoPlaceGroupList, S32 idx)
{
	int count = 0;

	do {
		AutoPlacementGroup *pGroup;

		if (++idx >= eaSize(&papAutoPlaceGroupList))
		{
			idx = 0;
		}

		pGroup = eaGet(&papAutoPlaceGroupList, idx);
		if (pGroup && eaSize(&pGroup->auto_place_objects) > 0)
		{
			return idx;
		}

		// fail safe- make sure we don't go into an infinite
		// though, I make sure we enter here only if there is a group that has some valid objects
	} while(++count < eaSize(&papAutoPlaceGroupList));

	return -1;
}


// -------------------------------------------------------------------------------------
__forceinline static S32 getGroupIndexByPercent(AutoPlacementGroup **papAutoPlaceGroupList, F32 fPercent)
{
	int i;
	for (i = 0; i < eaSize(&papAutoPlaceGroupList); i++)
	{
		AutoPlacementGroup *pGroup = papAutoPlaceGroupList[i];
		
		if (fPercent <= pGroup->weight)
		{
			if (eaSize(&pGroup->auto_place_objects) > 0)
			{
				// this group has valid objects in it
				return i;
			}

			// the group was empty, shouldn't happen if the data is configured correctly, 
			// but if this happens, just get the next non-empty group
			return getNextNonEmptyGroup(papAutoPlaceGroupList, i);
		}
	}

	// if we get here somehow the weight given wasn't within the group's max sum... 
	// just give back the first valid group
	return getNextNonEmptyGroup(papAutoPlaceGroupList, -1);
}


// -------------------------------------------------------------------------------------
__forceinline static S32 getObjectIndexByPercent(AutoPlacementObject **papAutoPlaceObjectList, F32 fPercent)
{
	int i;
	for (i = 0; i < eaSize(&papAutoPlaceObjectList); i++)
	{
		AutoPlacementObject *pObject = papAutoPlaceObjectList[i];
		if (fPercent <= pObject->weight)
		{
			return i;
		}
	}

	return 0;
}


// -------------------------------------------------------------------------------------
// 
static void getRandomPlacedObject(APRandomPlacedObject *pRandObj, const AutoPlacementSet *pAutoPlacedSetProperties)
{
	// get the random object and the group data needed to create the object
	
	pRandObj->groupIdx = getGroupIndexByPercent(pAutoPlacedSetProperties->auto_place_group, randomPct());
	// first get a random group to create an object from
	assert(pRandObj->groupIdx != -1);
	pRandObj->pRandomGroup = pAutoPlacedSetProperties->auto_place_group[pRandObj->groupIdx];
	
	// now get a random object in the group
	pRandObj->objectIdx = getObjectIndexByPercent(pRandObj->pRandomGroup->auto_place_objects, randomPct());
	pRandObj->pRandomObject = pRandObj->pRandomGroup->auto_place_objects[pRandObj->objectIdx];
}
#endif

// -------------------------------------------------------------------------------------
// get the next placed object
static bool apUpdateObjectPriority_GetBestPlacedObject(const AutoPlacementSet *pAutoPlacedSetProperties, APRandomPlacedObject *pNextObject)
{
	S32 i;
	F32 fMaxPriority = -FLT_MAX;
	
	assert(pNextObject);
	ZeroStruct(pNextObject);
	
	// 
	for (i = 0; i < eaSize(&pAutoPlacedSetProperties->auto_place_group); i++)
	{
		AutoPlacementGroup *pGroup = pAutoPlacedSetProperties->auto_place_group[i];

		if (pGroup->can_place)
		{
			if (pGroup->priority > fMaxPriority)
			{
				fMaxPriority = pGroup->priority;
				pNextObject->groupIdx = i;
				pNextObject->pRandomGroup = pGroup;
			}

			pGroup->priority += pGroup->target_percentage;
		}
	}

	if (pNextObject->pRandomGroup)
	{
		AutoPlacementGroup *pGroup = pNextObject->pRandomGroup;

		fMaxPriority = -FLT_MAX;
		for (i = 0; i < eaSize(&pGroup->auto_place_objects); i++)
		{
			AutoPlacementObject *pObject = pGroup->auto_place_objects[i];

			if (pObject->can_place)
			{
				if (pObject->priority > fMaxPriority)
				{
					fMaxPriority = pObject->priority;
					pNextObject->objectIdx = i;
					pNextObject->pRandomObject = pObject;
				}
				
				pObject->priority += pObject->target_percentage;
			}
		}
		
		if (pNextObject->pRandomObject)
		{
			// the object that we picked, deduct its priority
			pNextObject->pRandomObject->priority -= pNextObject->pRandomObject->target_percentage * 1.5f;		
			pNextObject->pRandomGroup->priority -= pNextObject->pRandomGroup->target_percentage  * 1.5f;
			return true;
		}
	}

	return false;
}

// -------------------------------------------------------------------------------------
// also snaps the object to the ground
static bool apGatherGroundInformation(int iPartitionIdx, Vec3 vPos)
{
	WorldCollCollideResults rayCastResults;
	Vec3 vCastPosEnd;
	
	copyVec3(vPos, vCastPosEnd);
	vCastPosEnd[1] -= 10.0f;

	if (!wcRayCollide(worldGetActiveColl(iPartitionIdx), vPos, vCastPosEnd, WC_FILTER_BIT_MOVEMENT, &rayCastResults ))
	{
		// no ground found? 
		return false;
	}

	copyVec3(rayCastResults.posWorldImpact, vPos);
	copyVec3(rayCastResults.normalWorld, g_AutoPlacementExprData.ground_normal);
	
	// 
	g_AutoPlacementExprData.elevation = rayCastResults.posWorldImpact[1];

	// get the angle between the normal of the ground and the UP vector
	// this is the slope (in degrees)
	{
		
		F32 fDot = dotVec3(rayCastResults.normalWorld, upvec);
		fDot = CLAMP(fDot, -1.0f, 1.0f);
		g_AutoPlacementExprData.slope = DEG(acosf(fDot));
	}

	g_AutoPlacementExprData.groundMaterialName = NULL;
	g_AutoPlacementExprData.materialPhysicalProperty = NULL;

	// get the material, physical property, and also check if we are on an object we shouldn't be
	if (rayCastResults.wco)
	{
		WorldCollisionEntry *entry;
		PhysicalProperties *phys_prop;

		if (wcoGetUserPointer(rayCastResults.wco, entryCollObjectMsgHandler, &entry))
		{
			Model *model = SAFE_MEMBER(entry, model);
			Material* mat = modelGetCollisionMaterialByTri(model, rayCastResults.tri.index);
			g_AutoPlacementExprData.groundMaterialName = SAFE_MEMBER(mat, material_name);

			// check if we are on an object we shouldn't be placing stuff on
			if (entry)
			{
				if (entry->base_entry_data.parent_entry)
				{
					if (WCENT_INTERACTION == entry->base_entry_data.parent_entry->base_entry.type)
						return false; // do not place things on interaction nodes
				}

			}

		}

		phys_prop = wcoGetPhysicalProperties(rayCastResults.wco, rayCastResults.tri.index, rayCastResults.posWorldImpact, NULL);
		if (phys_prop)
		{
			g_AutoPlacementExprData.materialPhysicalProperty = physicalPropertiesGetName(phys_prop);
		}
		
	}
	

	return true;

}

// -------------------------------------------------------------------------------------
#define ADDITIONAL_SEARCH_RADIUS	(400.0f)

static void apGatherNearbyEncounters(const AutoPlacementParams *pParams, GameEncounter*** peaEncounters)
{
	S32 i;
	Vec3 vSearchPos;
	F32 fSearchRadius;
	GameEncounter **eaEncounters;

	apGetCenterRadiusFromVolumes(pParams->ppAutoPlacementVolumes, vSearchPos, &fSearchRadius);

	fSearchRadius += ADDITIONAL_SEARCH_RADIUS;

	eaEncounters = encounter_GetEncountersWithinDistance(vSearchPos, fSearchRadius);
	for (i = 0; i < eaSize(&eaEncounters); i++)
	{
		eaPush(peaEncounters, eaEncounters[i]);
	}
}

static void apGatherNearbyOldEncounters(const AutoPlacementParams *pParams, OldStaticEncounter*** peaEncounters)
{
	S32 i;
	Vec3 vSearchPos;
	F32 fSearchRadiusSQ;

	if (! g_EncounterMasterLayer)
		return;
	
	apGetCenterRadiusFromVolumes(pParams->ppAutoPlacementVolumes, vSearchPos, &fSearchRadiusSQ);

	fSearchRadiusSQ += ADDITIONAL_SEARCH_RADIUS;
	fSearchRadiusSQ = SQR(fSearchRadiusSQ);

	for (i = 0; i < eaSize(&g_EncounterMasterLayer->encLayers); i++)
	{
		S32 x;
		EncounterLayer *layer = g_EncounterMasterLayer->encLayers[i];

		for (x = 0; x < eaSize(&layer->staticEncounters); x++)
		{
			OldStaticEncounter *enc = layer->staticEncounters[x];

			if (enc)
			{
				if (distance3Squared(enc->encPos, vSearchPos) <= fSearchRadiusSQ)
				{
					eaPush(peaEncounters, enc);
				}
			}
		}
	}
}


// -------------------------------------------------------------------------------------
static bool apValidateAndGetFitnessValues(int iPartitionIdx,
										  const AutoPlacementParams *pParams,  
										  const ProximityParams	*pProximityParams, 
										  const AutoPlaceObjectData	*pAutoPlaceData, 
										  const APRandomPlacedObject *pRandPlacedObj,
										  APPotentialObjectPos *pAPPos, F32 *pfFitness_group, F32 *pfFitness_object)
{
	EPositionInvalidReason	invalidReason;
	S32 bRollFailed;

	*pfFitness_group = UNINITIALIZED_FITNESS;
	*pfFitness_object = UNINITIALIZED_FITNESS;

	if (apIsPositionNearPlaced(pAPPos->vPos, pProximityParams, pAutoPlaceData->pPositionList, &bRollFailed) )
	{
		if (! bRollFailed)
		{
			pAPPos->bInvalidPos = true;
		}
		
		return false;
	}

	if (s_APState.pFitnessCacheBuffer)
	{
		apGetCachedFitnessValues(pAPPos, pRandPlacedObj->groupIdx, pRandPlacedObj->objectIdx, pfFitness_group, pfFitness_object);
		if (*pfFitness_group == INVALID_FITNESS || *pfFitness_object == INVALID_FITNESS)
			return false; // either the group or object fitness is invalid, cannot use this osition

		if (*pfFitness_group != UNINITIALIZED_FITNESS && *pfFitness_object != UNINITIALIZED_FITNESS)
		{	// we've already evaluated this position and it is kosher
			return true;
		}
	}

	if (! apGatherGroundInformation(iPartitionIdx, pAPPos->vPos) )
	{	
		pAPPos->bInvalidPos = true;
		return false;
	}

	copyVec3(pAPPos->vPos, g_AutoPlacementExprData.currentPotentialPos);

	if (! apIsObjectPlacementValid(pRandPlacedObj, (*pfFitness_group == UNINITIALIZED_FITNESS), (*pfFitness_object == UNINITIALIZED_FITNESS), &invalidReason))
	{
		if (invalidReason >= EPositionInvalidReason_INVALIDATE_POS_marker)
		{	// this position was rejected for a reason that will always yield it invalid
			pAPPos->bInvalidPos = true;
		}
		else if (invalidReason == EPositionInvalidReason_REQUIREDEXPR_GROUP)
		{
			apCacheGroupFitnesValue(pAPPos, pRandPlacedObj->groupIdx, INVALID_FITNESS);
		}
		else if (invalidReason == EPositionInvalidReason_REQUIREDEXPR_OBJECT)
		{
			apCacheObjectFitnesValue(pAPPos, pRandPlacedObj->groupIdx, pRandPlacedObj->objectIdx, INVALID_FITNESS);
		}
		return false;
	}

	// Evaluate our fitness expressions for the group and object
	if (*pfFitness_group == UNINITIALIZED_FITNESS)
	{
		if ( ! pRandPlacedObj->pRandomGroup->fitness_expression)
		{
			*pfFitness_group = s_fExpressionFitnessThreshold;
		}
		else
		{
			*pfFitness_group = apEvaluateFitnessExpression(pRandPlacedObj->pRandomGroup->fitness_expression);
		}
		apCacheGroupFitnesValue(pAPPos, pRandPlacedObj->groupIdx, *pfFitness_group);
	}
	

	if (*pfFitness_object == UNINITIALIZED_FITNESS)
	{
		if (! pRandPlacedObj->pRandomObject->fitness_expression)
		{
			*pfFitness_object = s_fExpressionFitnessThreshold;
		}
		else
		{
			*pfFitness_object = apEvaluateFitnessExpression(pRandPlacedObj->pRandomObject->fitness_expression);
		}

		apCacheObjectFitnesValue(pAPPos, pRandPlacedObj->groupIdx, pRandPlacedObj->objectIdx, *pfFitness_object);
	}
	
	return (*pfFitness_group != INVALID_FITNESS && *pfFitness_object != INVALID_FITNESS);
}


// -------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void autoGenerateObjectPlacement(Entity *pEnt, AutoPlacementParams *pParams)
{
	ProximityParams	proximityParams = {0};
	AutoPlaceObjectData	autoPlaceData = {0};
	APRandomPlacedObject randPlacedObj = {0};
	F32 fMinProx;
	int iPartitionIdx = entGetPartitionIdx(pEnt);
		
	printf("\n\n(Auto-Placement) Starting process.\n");

	// initialize our auto-placement data
	{
		s_placedObjectCount = 1;
		s_bPositionOutOfWorld = false;

		wcForceSimulationUpdate();
		
		g_AutoPlacementExprData.eaPotentialLineSegmentList = NULL;
		apGetPotentialPlacementFromBeacons(iPartitionIdx, pParams, &g_AutoPlacementExprData.eaPotentialLineSegmentList);

		// Fix-up the auto-placement properties
		apFixupAutoPlacementProperties(pParams->pPlacementProperties);
		apGetSiblingAutoPlacedObjects();

		// 
		// get the potential positions from the potential line segment list		
		apGetRandomPlacementPositions(iPartitionIdx, &s_APState.eaPotentialPositions, g_AutoPlacementExprData.eaPotentialLineSegmentList, pParams);
		
		apAllocateCachedFitnessBuffers(pParams, s_APState.eaPotentialPositions);

		g_AutoPlacementExprData.eaNearbyEncounters = NULL;
		apGatherNearbyEncounters(pParams, &g_AutoPlacementExprData.eaNearbyEncounters);
		g_AutoPlacementExprData.eaNearbyOldEncounters = NULL;
		if (gConf.bAllowOldEncounterData)
			apGatherNearbyOldEncounters(pParams, &g_AutoPlacementExprData.eaNearbyOldEncounters);
		
	}
	//
	
	fMinProx = pParams->pPlacementProperties->proximity - pParams->pPlacementProperties->variance;
	proximityParams.fProximitySQ = SQR(pParams->pPlacementProperties->proximity);
	proximityParams.fMinProximitySQ = SQR(pParams->pPlacementProperties->proximity - pParams->pPlacementProperties->variance);
	proximityParams.fRangeSQ = proximityParams.fProximitySQ - proximityParams.fMinProximitySQ;

		
	loadstart_printf("(Auto-Placement) Calculating object positions based off potential sources...\n" );

	// get the next prioritized object to be placed
	while( apUpdateObjectPriority_GetBestPlacedObject(pParams->pPlacementProperties, &randPlacedObj) )
	{
		S32 i;
		Vec3 vBestPotentialPosition;
		
		// Note: a group's fitness takes priority over object fitness.
		//	A fitness of 0 means 'cannot be placed'
		F32 fBestFitness_Group = UNINITIALIZED_FITNESS;
		F32 fBestFitness_Object = UNINITIALIZED_FITNESS;

		APPotentialObjectPos *pBestPotentialPos = NULL;
		S32 iNumPositions = eaSize(&s_APState.eaPotentialPositions);
		bool bFoundPosition = false;
		
		// go through all of our potential positions to find where to put this object
		
		for( i = 0; bFoundPosition == false && i < iNumPositions; i++)
		{
			APPotentialObjectPos *pAPPos = s_APState.eaPotentialPositions[i];
			F32 fFitness_group, fFitness_object;

			if (pAPPos->bInvalidPos)
			{
				continue;
			}

			g_AutoPlacementExprData.pCurrentLineSegment = pAPPos->pParentSegment;

			if (! apValidateAndGetFitnessValues(iPartitionIdx, pParams, &proximityParams, &autoPlaceData, &randPlacedObj, pAPPos, &fFitness_group, &fFitness_object))
			{
				continue;
			}

			if (fFitness_group >= fBestFitness_Group && fFitness_object >= fBestFitness_Object)
			{
				fBestFitness_Group = fFitness_group;
				fBestFitness_Object = fFitness_object;
				copyVec3(pAPPos->vPos, vBestPotentialPosition);
				pBestPotentialPos = pAPPos;

				//! if our fitness is at the max on both, break out because our position is good
				if (fBestFitness_Group >= s_fExpressionFitnessThreshold && fBestFitness_Object >= s_fExpressionFitnessThreshold)
				{
					bFoundPosition = true;
					break; 
				}
			}
			
		}

		
		if (bFoundPosition || (fBestFitness_Group >= 0.0f && fBestFitness_Object >= 0.0f))
		{
			// increase the counts for this object
			AutoPlacementGroup *pGroup;
			AutoPlacementObject *pObject;

			pGroup = eaGet(&pParams->pPlacementProperties->auto_place_group, randPlacedObj.groupIdx);
			assert(pGroup);
			pObject = eaGet(&pGroup->auto_place_objects, randPlacedObj.objectIdx);
			assert(pObject);

			pObject->count++;
			pGroup->count++;
			s_placedObjectCount++;

			// invalidate this position as it has already placed an object
			pBestPotentialPos->bInvalidPos = true;

			// create and initialize the new position
			{
				AutoPlacePosition *pPosData;

				pPosData = calloc(1, sizeof(AutoPlacePosition));

				copyVec3(vBestPotentialPosition, pPosData->vPos);
				copyVec3(g_AutoPlacementExprData.ground_normal, pPosData->vNormal);

				pPosData->groupIdx = randPlacedObj.groupIdx;
				pPosData->objectIdx = randPlacedObj.objectIdx;

				eaPush(&autoPlaceData.pPositionList, pPosData);

				if ((eaSize(&autoPlaceData.pPositionList) % 20) == 0)
				{
					printf(" Created %d positions so far...\n", eaSize(&autoPlaceData.pPositionList));
				}
			}
		}
		else 
		{
			// The object could not be placed - make sure we do not try to place it anymore 
			randPlacedObj.pRandomObject->can_place = false;
			
			// check the group- the group can be placed if any object in the group can be placed
			randPlacedObj.pRandomGroup->can_place = false;
			for (i = 0; i < eaSize(&randPlacedObj.pRandomGroup->auto_place_objects); i++)
			{
				AutoPlacementObject *pObject = randPlacedObj.pRandomGroup->auto_place_objects[i];
				if (pObject->can_place)
				{
					randPlacedObj.pRandomGroup->can_place = true;
					break;
				}
			}
		}
		
		
		ZeroStruct(&randPlacedObj);
	}
	

	g_AutoPlacementExprData.pCurrentLineSegment = NULL;

	loadend_printf(" done (%d positions generated).", eaSize(&autoPlaceData.pPositionList));
	
	if (s_bPositionOutOfWorld)
	{
		printf("\n*** WARNING: Auto-Placement detected some potential positions were out of the world!\n\n");

	}


	// send the list back to the client
	ClientCmd_receiveAutoPlacementData(pEnt, &autoPlaceData);

	eaDestroyEx(&autoPlaceData.pPositionList, NULL);
	eaDestroyEx(&g_AutoPlacementExprData.eaPotentialLineSegmentList, NULL);
	eaDestroy(&g_AutoPlacementExprData.eaNearbyEncounters);
	eaDestroyEx(&s_APState.eaSiblingAutoPlacedObjects, NULL);
	eaDestroyEx(&s_APState.eaPotentialPositions, NULL);
	if (s_APState.pFitnessCacheBuffer)
	{
		free(s_APState.pFitnessCacheBuffer);
	}
	if (s_APState.paiGroupOffsets)
	{
		free(s_APState.paiGroupOffsets);
	}

	ZeroStruct(&s_APState);
}

#endif





